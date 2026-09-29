/* em_chain_page.c - the chain page's DMA / VIF1 / VU1 / GIF / GS path
 * (em_chain_page.h, docs/CHAIN_PAGE.md). */
#include "game/em_chain_page.h"

#include <string.h>

#define SEG_MAX 16384u       /* DMA transfers per page (captured: < 1300) */
#define DIRECT_MAX 4096u     /* qwords of one DIRECT packet               */
#define TAGS_MAX 100000u     /* DMA tags per page (a looping chain faults) */

typedef struct {
    uint32_t address;        /* source address of the transferred data   */
    uint32_t qwc;
    const uint8_t *bytes;
} Seg;

/* The walk's state: the transfers in order and a cursor over their words. */
typedef struct {
    EmChainPage *p;
    Seg seg[SEG_MAX];
    uint32_t nseg;
    uint32_t si, wi;         /* the next word: segment si, word wi         */
    /* VIF1 */
    uint32_t cl, wl, cycle_set;
    uint32_t base, offset;
    uint32_t program;        /* the uploaded program (0: none)            */
    uint32_t mpg_parts;      /* sprite / snow program: MPG parts uploaded */
    uint32_t mpg_first;      /* the first part's source address           */
    /* GS */
    uint32_t prim;
    uint32_t set;
    uint64_t tex0, clamp, tex1, alpha, test, colclamp;
    uint8_t rgba[4];
    uint32_t rq;             /* RGBAQ.Q                                    */
    uint32_t rq_known;
    uint32_t q;              /* the internal Q PACKED ST holds             */
    uint32_t q_known;
    uint32_t s, t;
    uint16_t u, v;
    EmGfxGsVertex queue[3];
    uint8_t queue_q[3];
    uint32_t nq;
    /* list mode: the GS environment registers (context 1) */
    int list;
    EmGfxGsEnv env;
} Walk;

static Walk W;   /* one page at a time (the frame close is single-threaded) */

static int fault(Walk *w, uint32_t code, uint32_t address, uint32_t detail)
{
    if (!w->p->fault) {
        w->p->fault = code;
        w->p->fault_address = address;
        w->p->fault_detail = detail;
    }
    return -1;
}

static uint32_t rd32(const uint8_t *b)
{
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

static uint64_t rd64(const uint8_t *b)
{
    return (uint64_t)rd32(b) | (uint64_t)rd32(b + 4) << 32;
}

/* ------------------------------------------------------------------ DMA */

static int skipped(const EmChainPage *p, uint32_t addr)
{
    for (uint32_t k = 0; k < p->skip_count && k < EM_CHAIN_PAGE_SKIP_MAX; ++k)
        if (p->skip_calls[k] == addr) return 1;
    return 0;
}

static int dma(Walk *w, uint32_t start)
{
    EmChainPage *p = w->p;
    uint32_t stack[2], depth = 0, cur = start;
    const uint32_t end = start + 0x20u;
    for (uint32_t n = 0;; ++n) {
        if (!w->list && cur == end && depth == 0) return 0;
        if (n >= TAGS_MAX) return fault(w, EM_CHAIN_PAGE_FAULT_CAPACITY, cur, TAGS_MAX);
        const uint8_t *tag = p->read(p->read_ctx, cur, 16);
        if (!tag) return fault(w, EM_CHAIN_PAGE_FAULT_READ, cur, 16);
        const uint32_t lo = rd32(tag), hi = rd32(tag + 4);
        const uint32_t qwc = lo & 0xFFFFu, id = (lo >> 28) & 7u;
        /* PCE and IRQ never occur on the page; bits 16..25 are unused by
         * the DMAC (001D21B0 leaves stale bytes there). */
        if (lo & 0x8C000000u) return fault(w, EM_CHAIN_PAGE_FAULT_DMA, cur, lo);
        if (hi & 0x80000000u) return fault(w, EM_CHAIN_PAGE_FAULT_DMA, cur, hi);  /* SPR */
        const uint32_t addr = hi & 0x7FFFFFFFu;
        uint32_t data = cur + 16u, next;
        switch (id) {
        case 1: next = cur + 16u + 16u * qwc; break;                     /* CNT  */
        case 2: next = addr; break;                                       /* NEXT */
        case 3: data = addr; next = cur + 16u; break;                     /* REF  */
        case 5:                                                           /* CALL */
            if (depth == 0 && skipped(p, addr)) {
                p->counts.skipped++;
                next = cur + 16u + 16u * qwc;
                break;
            }
            if (depth >= 2) return fault(w, EM_CHAIN_PAGE_FAULT_DMA, cur, lo);
            stack[depth++] = cur + 16u + 16u * qwc;
            next = addr;
            break;
        case 6:                                                           /* RET  */
            if (depth == 0) return fault(w, EM_CHAIN_PAGE_FAULT_DMA, cur, lo);
            next = stack[--depth];
            break;
        case 7:                                                           /* END  */
            /* A frame list's end (list mode only; a page never holds one):
             * its data is transferred, then the chain stops. */
            if (!w->list || depth != 0) return fault(w, EM_CHAIN_PAGE_FAULT_DMA, cur, lo);
            next = 0;
            break;
        default:
            return fault(w, EM_CHAIN_PAGE_FAULT_DMA, cur, lo);
        }
        p->counts.transfers++;
        if (qwc) {
            if (w->nseg >= SEG_MAX) return fault(w, EM_CHAIN_PAGE_FAULT_CAPACITY, cur, SEG_MAX);
            const uint8_t *b = p->read(p->read_ctx, data, 16u * qwc);
            if (!b) return fault(w, EM_CHAIN_PAGE_FAULT_READ, data, 16u * qwc);
            w->seg[w->nseg++] = (Seg){ data, qwc, b };
            p->counts.qwords += qwc;
        }
        if (id == 7u) return 0;
        cur = next;
    }
}

/* The next transferred word and its source address; 0 at the end. */
static int word(Walk *w, uint32_t *value, uint32_t *address)
{
    while (w->si < w->nseg && w->wi >= 4u * w->seg[w->si].qwc) {
        w->si++;
        w->wi = 0;
    }
    if (w->si >= w->nseg) return 0;
    const Seg *s = &w->seg[w->si];
    *value = rd32(s->bytes + 4u * w->wi);
    *address = s->address + 4u * w->wi;
    w->wi++;
    return 1;
}

/* ------------------------------------------------------------------- GS */

static int emit(Walk *w, uint32_t count)
{
    EmChainPage *p = w->p;
    if (p->prim_count >= p->prim_capacity) return fault(w, EM_CHAIN_PAGE_FAULT_CAPACITY, 0, p->prim_capacity);
    EmGfxGsPrim *o = &p->prims[p->prim_count];
    memset(o, 0, sizeof *o);
    o->prim = w->prim;
    o->set = w->set;
    o->tex0 = w->tex0;
    o->clamp = w->clamp;
    o->tex1 = w->tex1;
    o->alpha = w->alpha;
    o->test = w->test;
    o->colclamp = w->colclamp;
    o->count = count;
    for (uint32_t k = 0; k < count; ++k) {
        o->v[k] = w->queue[k];
        if (!w->queue_q[k]) p->counts.stale_q++;
    }
    if (p->prim_q) {
        memset(&p->prim_q[p->prim_count], 0, sizeof p->prim_q[p->prim_count]);
        for (uint32_t k = 0; k < count; ++k) p->prim_q[p->prim_count].q_known[k] = w->queue_q[k];
    }
    if (w->list && p->prim_env) p->prim_env[p->prim_count] = w->env;
    p->prim_count++;
    p->counts.prims++;
    p->counts.prim_type[w->prim & 7u]++;
    return 0;
}

/* A vertex kick (XYZF2 / XYZ2): queue it and draw what the PRIM type
 * completes, unless ADC. */
static int vertex(Walk *w, const EmGfxGsVertex *v, uint32_t adc, uint32_t at)
{
    static const uint32_t need[8] = { 1, 2, 2, 3, 3, 3, 2, 0 };
    const uint32_t type = w->prim & 7u;
    if (type == 7u) return fault(w, EM_CHAIN_PAGE_FAULT_GIF, at, w->prim);
    const uint32_t n = need[type];
    if (w->nq == 3u) {                       /* only strips / fans keep 3 */
        if (type == 5u) { w->queue[1] = w->queue[2]; w->queue_q[1] = w->queue_q[2]; }
        else { w->queue[0] = w->queue[1]; w->queue[1] = w->queue[2];
               w->queue_q[0] = w->queue_q[1]; w->queue_q[1] = w->queue_q[2]; }
        w->nq = 2;
    }
    w->queue[w->nq] = *v;
    w->queue_q[w->nq] = (uint8_t)w->rq_known;
    w->nq++;
    if (type == 0u || type == 1u || type == 3u || type == 6u) {       /* lists */
        if (w->nq == n) {
            if (!adc && emit(w, n) < 0) return -1;
            w->nq = 0;
        }
    } else if (type == 2u) {                                           /* line strip */
        if (w->nq == 3u) {
            w->queue[0] = w->queue[1]; w->queue[1] = w->queue[2];
            w->queue_q[0] = w->queue_q[1]; w->queue_q[1] = w->queue_q[2];
            w->nq = 2;
        }
        if (w->nq == 2u && !adc && emit(w, 2) < 0) return -1;
    } else {                                                           /* strip, fan */
        if (w->nq == 3u && !adc && emit(w, 3) < 0) return -1;
    }
    return 0;
}

static void prim_write(Walk *w, uint32_t value)
{
    w->prim = value & 0x7FFu;
    w->nq = 0;
}

/* One PACKED register write (the qword at q). */
static int packed(Walk *w, uint32_t reg, const uint8_t *q, uint32_t at)
{
    const uint32_t a = rd32(q), b = rd32(q + 4), c = rd32(q + 8), d = rd32(q + 12);
    switch (reg) {
    case 0x01:                                                    /* RGBAQ */
        w->rgba[0] = (uint8_t)a; w->rgba[1] = (uint8_t)b; w->rgba[2] = (uint8_t)c; w->rgba[3] = (uint8_t)d;
        w->rq = w->q;
        w->rq_known = w->q_known;
        return 0;
    case 0x02:                                                    /* ST (Q held) */
        w->s = a; w->t = b; w->q = c; w->q_known = 1;
        return 0;
    case 0x03:                                                    /* UV */
        w->u = (uint16_t)(a & 0x3FFFu); w->v = (uint16_t)(b & 0x3FFFu);
        return 0;
    case 0x04: case 0x05: {                                       /* XYZF2 / XYZ2 */
        EmGfxGsVertex v;
        memset(&v, 0, sizeof v);
        v.x = (uint16_t)a; v.y = (uint16_t)b;
        if (reg == 0x04) { v.z = (c >> 4) & 0xFFFFFFu; v.f = (uint8_t)(d >> 4); v.has_f = 1; }
        else v.z = c;
        memcpy(v.rgba, w->rgba, 4);
        v.q = w->rq; v.s = w->s; v.t = w->t; v.u = w->u; v.v = w->v;
        return vertex(w, &v, (d >> 15) & 1u, at);
    }
    case 0x06:                                                    /* TEX0_1 */
        w->tex0 = rd64(q); w->set |= EM_GFX_GS_TEX0;
        return 0;
    case 0x0F:                                                    /* NOP */
        return 0;
    default:
        return fault(w, EM_CHAIN_PAGE_FAULT_GIF, at, reg);
    }
}

/* List mode's A+D writes beyond the page's: the context-1 environment
 * (recorded for the primitives after it), the context-2 set and FOGCOL
 * (accepted: no context-1 primitive reads them) and the vertex registers.
 * 1 when handled, 0 when not a list register, -1 on a fault. */
static int address_data_list(Walk *w, uint32_t reg, uint64_t data, uint32_t at)
{
    EmGfxGsEnv *e = &w->env;
    switch (reg) {
    case 0x01:                                                    /* RGBAQ */
        for (unsigned k = 0; k < 4u; ++k) w->rgba[k] = (uint8_t)(data >> (8u * k));
        w->rq = (uint32_t)(data >> 32);
        w->rq_known = 1;
        return 1;
    case 0x02:                                                    /* ST */
        w->s = (uint32_t)data; w->t = (uint32_t)(data >> 32);
        return 1;
    case 0x03:                                                    /* UV */
        w->u = (uint16_t)(data & 0x3FFFu); w->v = (uint16_t)((data >> 16) & 0x3FFFu);
        return 1;
    case 0x04: case 0x05: {                                       /* XYZF2 / XYZ2 */
        EmGfxGsVertex v;
        memset(&v, 0, sizeof v);
        v.x = (uint16_t)data; v.y = (uint16_t)(data >> 16);
        if (reg == 0x04) { v.z = (uint32_t)(data >> 32) & 0xFFFFFFu; v.f = (uint8_t)(data >> 56); v.has_f = 1; }
        else v.z = (uint32_t)(data >> 32);
        memcpy(v.rgba, w->rgba, 4);
        v.q = w->rq; v.s = w->s; v.t = w->t; v.u = w->u; v.v = w->v;
        return vertex(w, &v, 0, at) < 0 ? -1 : 1;
    }
    case 0x18: e->xyoffset = data;   e->set |= EM_GFX_GS_ENV_XYOFFSET;   return 1;
    case 0x1A: e->prmodecont = data; e->set |= EM_GFX_GS_ENV_PRMODECONT; return 1;
    case 0x22: e->scanmsk = data;    e->set |= EM_GFX_GS_ENV_SCANMSK;    return 1;
    case 0x3B: e->texa = data;       e->set |= EM_GFX_GS_ENV_TEXA;       return 1;
    case 0x40: e->scissor = data;    e->set |= EM_GFX_GS_ENV_SCISSOR;    return 1;
    case 0x45: e->dthe = data;       e->set |= EM_GFX_GS_ENV_DTHE;       return 1;
    case 0x49: e->pabe = data;       e->set |= EM_GFX_GS_ENV_PABE;       return 1;
    case 0x4A: e->fba = data;        e->set |= EM_GFX_GS_ENV_FBA;        return 1;
    case 0x4C: e->frame = data;      e->set |= EM_GFX_GS_ENV_FRAME;      return 1;
    case 0x4E: e->zbuf = data;       e->set |= EM_GFX_GS_ENV_ZBUF;       return 1;
    case 0x19: case 0x41: case 0x48: case 0x4D: case 0x4F:        /* context 2 */
    case 0x3D:                                                    /* FOGCOL */
        return 1;
    default:
        return 0;
    }
}

/* One A+D write. */
static int address_data(Walk *w, const uint8_t *q, uint32_t at)
{
    const uint64_t data = rd64(q);
    const uint32_t reg = q[8];
    if (w->list) {
        const int r = address_data_list(w, reg, data, at);
        if (r) return r < 0 ? -1 : 0;
    }
    switch (reg) {
    case 0x00: prim_write(w, (uint32_t)data); return 0;               /* PRIM */
    case 0x06: w->tex0 = data; w->set |= EM_GFX_GS_TEX0; return 0;     /* TEX0_1 */
    case 0x08: w->clamp = data; w->set |= EM_GFX_GS_CLAMP; return 0;   /* CLAMP_1 */
    case 0x14: w->tex1 = data; w->set |= EM_GFX_GS_TEX1; return 0;     /* TEX1_1 */
    case 0x3F: return 0;                                              /* TEXFLUSH */
    case 0x42: w->alpha = data; w->set |= EM_GFX_GS_ALPHA; return 0;   /* ALPHA_1 */
    case 0x46: w->colclamp = data; w->set |= EM_GFX_GS_COLCLAMP; return 0;
    case 0x47: w->test = data; w->set |= EM_GFX_GS_TEST; return 0;     /* TEST_1 */
    default: return fault(w, EM_CHAIN_PAGE_FAULT_GIF, at, reg);
    }
}

/* One GIF packet sequence up to EOP. fetch(i) is qword i of the packet;
 * `limit` qwords are available. Sets *used. */
typedef const uint8_t *(*Fetch)(Walk *w, uint32_t i, void *ctx);

static int gif(Walk *w, Fetch fetch, void *ctx, uint32_t limit, uint32_t at, uint32_t *used)
{
    uint32_t i = 0;
    for (;;) {
        if (i >= limit) return fault(w, EM_CHAIN_PAGE_FAULT_GIF, at, i);
        const uint8_t *tag = fetch(w, i++, ctx);
        const uint64_t lo = rd64(tag), hi = rd64(tag + 8);
        const uint32_t nloop = (uint32_t)(lo & 0x7FFFu), eop = (uint32_t)(lo >> 15) & 1u;
        const uint32_t pre = (uint32_t)(lo >> 46) & 1u, flg = (uint32_t)(lo >> 58) & 3u;
        uint32_t nreg = (uint32_t)(lo >> 60);
        if (!nreg) nreg = 16;
        if (flg != 0) return fault(w, EM_CHAIN_PAGE_FAULT_GIF, at, (uint32_t)lo);   /* PACKED only */
        if (pre) prim_write(w, (uint32_t)(lo >> 47) & 0x7FFu);
        for (uint32_t l = 0; l < nloop; ++l)
            for (uint32_t r = 0; r < nreg; ++r) {
                if (i >= limit) return fault(w, EM_CHAIN_PAGE_FAULT_GIF, at, i);
                const uint8_t *q = fetch(w, i++, ctx);
                const uint32_t reg = (uint32_t)(hi >> (4u * r)) & 15u;
                if (reg == 0x0E ? address_data(w, q, at) < 0 : packed(w, reg, q, at) < 0) return -1;
            }
        if (eop) {
            *used = i;
            return 0;
        }
    }
}

static const uint8_t *fetch_buffer(Walk *w, uint32_t i, void *ctx)
{
    (void)w;
    return (const uint8_t *)ctx + 16u * i;
}

static const uint8_t *fetch_dmem(Walk *w, uint32_t i, void *ctx)
{
    const uint32_t at = *(const uint32_t *)ctx;
    return (const uint8_t *)w->p->dmem[(at + i) & 1023u].w;
}

static int kick(void *ctx, const EmVu1PQword *dmem, uint32_t at)
{
    Walk *w = ctx;
    (void)dmem;
    uint32_t used;
    w->p->counts.kicks++;
    return gif(w, fetch_dmem, &at, EM_VU1P_DMEM_QWORDS, at, &used);
}

/* ------------------------------------------------------------------ VIF */

/* The three page programs' MPG uploads (source address of the code, micro
 * load address, instruction count). */
#define LANE_CODE    0x002332B8u
#define SPRITE_CODE0 0x00231798u
#define SPRITE_CODE1 0x00231FA0u
#define SNOW_CODE0   0x00233828u
#define SNOW_CODE1   0x00234030u

static int vif(Walk *w)
{
    EmChainPage *p = w->p;
    static uint8_t direct[DIRECT_MAX * 16u];
    uint32_t v, at;
    while (word(w, &v, &at)) {
        const uint32_t cmd = (v >> 24) & 0x7Fu, num = (v >> 16) & 0xFFu, imm = v & 0xFFFFu;
        if (v & 0x80000000u) return fault(w, EM_CHAIN_PAGE_FAULT_VIF, at, v);
        if (cmd >= 0x60u) {                                           /* UNPACK */
            const uint32_t vn = (cmd >> 2) & 3u, vl = cmd & 3u, cnt = num ? num : 256u;
            if ((cmd & 0x10u) || vl != 0u || (vn != 0u && vn != 3u) || (imm & 0xC000u) ||
                (w->cycle_set && (w->wl == 0u || w->wl > w->cl)))
                return fault(w, EM_CHAIN_PAGE_FAULT_VIF, at, v);
            /* Before the page's first STCYCL the cycle is the frame's (the
             * lane program packet's STCYCL sits in its DMA tag, which is not
             * transferred): CL == WL, the only setting under which that
             * program finds its constant rows 0..13 in one block. */
            if (!w->cycle_set) p->counts.cycle_inherited++;
            const uint32_t comps = vn + 1u, dst = imm & 0x3FFu;
            for (uint32_t k = 0; k < cnt; ++k) {
                uint32_t lane[4];
                for (uint32_t c = 0; c < comps; ++c) {
                    uint32_t a2;
                    if (!word(w, &lane[c], &a2)) return fault(w, EM_CHAIN_PAGE_FAULT_VIF, at, v);
                }
                const uint32_t d = (w->cycle_set ? dst + (k / w->wl) * w->cl + k % w->wl : dst + k) & 1023u;
                for (uint32_t c = 0; c < 4; ++c) p->dmem[d].w[c] = lane[comps == 4u ? c : 0u];
            }
            continue;
        }
        switch (cmd) {
        case 0x00: case 0x10: case 0x11: case 0x13: break;             /* NOP, FLUSH* */
        case 0x01:                                                     /* STCYCL */
            w->cl = imm & 0xFFu; w->wl = (imm >> 8) & 0xFFu; w->cycle_set = 1;
            break;
        case 0x02: w->offset = imm & 0x3FFu; break;                    /* OFFSET */
        case 0x03: w->base = imm & 0x3FFu; break;                      /* BASE */
        case 0x05:                                                     /* STMOD */
            if (imm & 3u) return fault(w, EM_CHAIN_PAGE_FAULT_VIF, at, v);
            break;
        case 0x20: {                                                   /* STMASK */
            uint32_t m, a2;
            if (!word(w, &m, &a2)) return fault(w, EM_CHAIN_PAGE_FAULT_VIF, at, v);
            break;
        }
        case 0x4A: {                                                   /* MPG */
            const uint32_t cnt = num ? num : 256u;
            uint32_t first = 0, a2, x;
            for (uint32_t k = 0; k < 2u * cnt; ++k) {
                if (!word(w, &x, &a2)) return fault(w, EM_CHAIN_PAGE_FAULT_VIF, at, v);
                if (k == 0) first = a2;
            }
            if (first == LANE_CODE && cnt == 138u && imm == 0u) {
                w->program = EM_CHAIN_PAGE_LANE; w->mpg_parts = 1;
            } else if ((first == SPRITE_CODE0 || first == SNOW_CODE0) && cnt == 256u && imm == 0u) {
                w->program = 0; w->mpg_parts = 1; w->mpg_first = first;
            } else if (first == SPRITE_CODE1 && cnt == 79u && imm == 0x100u && w->mpg_parts == 1u &&
                       w->program == 0u && w->mpg_first == SPRITE_CODE0) {
                w->program = EM_CHAIN_PAGE_SPRITE; w->mpg_parts = 2;
            } else if (first == SNOW_CODE1 && cnt == 81u && imm == 0x100u && w->mpg_parts == 1u &&
                       w->program == 0u && w->mpg_first == SNOW_CODE0) {
                w->program = EM_CHAIN_PAGE_SNOW; w->mpg_parts = 2;
            } else {
                w->program = 0; w->mpg_parts = 0;
                return fault(w, EM_CHAIN_PAGE_FAULT_PROGRAM, at, first);
            }
            break;
        }
        case 0x14: {                                                   /* MSCAL */
            if (imm != 0u || !w->program) return fault(w, EM_CHAIN_PAGE_FAULT_PROGRAM, at, v);
            int rc;
            if (w->program == EM_CHAIN_PAGE_LANE) {
                p->counts.mscal_lane++;
                rc = em_vu1_lane_program_mscal(&p->regs, p->dmem, kick, w);
            } else if (w->program == EM_CHAIN_PAGE_SNOW) {
                p->counts.mscal_snow++;
                rc = em_vu1_snow_program_mscal(&p->regs, p->dmem, kick, w);
            } else {
                p->counts.mscal_sprite++;
                rc = em_vu1_sprite_program_mscal(&p->regs, p->dmem, kick, w);
            }
            if (rc == EM_VU1P_FAULT_KICK) return -1;                   /* the GIF latched it */
            if (rc) return fault(w, EM_CHAIN_PAGE_FAULT_VU, at, (uint32_t)rc);
            break;
        }
        case 0x50: {                                                   /* DIRECT */
            const uint32_t cnt = imm ? imm : 65536u;
            if (cnt > DIRECT_MAX) return fault(w, EM_CHAIN_PAGE_FAULT_CAPACITY, at, cnt);
            for (uint32_t k = 0; k < 4u * cnt; ++k) {
                uint32_t x, a2;
                if (!word(w, &x, &a2)) return fault(w, EM_CHAIN_PAGE_FAULT_VIF, at, v);
                if (k == 0 && (a2 & 15u)) return fault(w, EM_CHAIN_PAGE_FAULT_VIF, at, v);
                direct[4u * k] = (uint8_t)x; direct[4u * k + 1u] = (uint8_t)(x >> 8);
                direct[4u * k + 2u] = (uint8_t)(x >> 16); direct[4u * k + 3u] = (uint8_t)(x >> 24);
            }
            uint32_t used;
            p->counts.direct++;
            if (gif(w, fetch_buffer, direct, cnt, at, &used) < 0) return -1;
            /* List mode: PATH2 goes on with the next GIF tag after an EOP
             * until the DIRECT data is used up (001D6930 sends two
             * packets in one DIRECT). The page's DIRECTs each hold one. */
            while (w->list && used < cnt) {
                uint32_t more;
                if (gif(w, fetch_buffer, direct + 16u * used, cnt - used, at, &more) < 0) return -1;
                used += more;
            }
            if (used != cnt) return fault(w, EM_CHAIN_PAGE_FAULT_GIF, at, used);
            break;
        }
        default:
            return fault(w, EM_CHAIN_PAGE_FAULT_VIF, at, v);
        }
    }
    return 0;
}

static int run(EmChainPage *p, uint32_t start, int list)
{
    if (!p) return -1;
    if (!p->read || !p->prims || (p->skip_count && !p->skip_calls) || p->skip_count > EM_CHAIN_PAGE_SKIP_MAX) {
        p->fault = EM_CHAIN_PAGE_FAULT_ARGS;
        return -1;
    }
    p->prim_count = 0;
    memset(&p->counts, 0, sizeof p->counts);
    p->fault = p->fault_address = p->fault_detail = 0;
    em_vu1p_regs_reset(&p->regs);
    memset(p->dmem, 0, sizeof p->dmem);
    Walk *w = &W;
    memset(w, 0, sizeof *w);
    w->p = p;
    w->list = list;
    if (dma(w, start) < 0) return -1;
    return vif(w);
}

int em_chain_page_run(EmChainPage *p, uint32_t start) { return run(p, start, 0); }
int em_chain_page_run_list(EmChainPage *p, uint32_t start) { return run(p, start, 1); }

const char *em_chain_page_fault_name(uint32_t fault)
{
    switch (fault) {
    case EM_CHAIN_PAGE_OK: return "none";
    case EM_CHAIN_PAGE_FAULT_ARGS: return "arguments";
    case EM_CHAIN_PAGE_FAULT_READ: return "unmapped address";
    case EM_CHAIN_PAGE_FAULT_DMA: return "DMA tag";
    case EM_CHAIN_PAGE_FAULT_VIF: return "VIF code";
    case EM_CHAIN_PAGE_FAULT_PROGRAM: return "VU1 program";
    case EM_CHAIN_PAGE_FAULT_VU: return "VU1 translation";
    case EM_CHAIN_PAGE_FAULT_GIF: return "GIF packet";
    case EM_CHAIN_PAGE_FAULT_CAPACITY: return "capacity";
    default: return "unknown";
    }
}
