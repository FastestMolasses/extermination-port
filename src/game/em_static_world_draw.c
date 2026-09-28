/* em_static_world_draw.c - the static world's channel-0 run as the DMA sends
 * it (see em_static_world_draw.h, docs/STATIC_WORLD.md section 7). */
#include "game/em_static_world_draw.h"

#include <stdlib.h>
#include <string.h>

#include "game/em_object_unit.h"
#include "game/em_vu1_shadow_clip.h"

typedef uint32_t u32;
typedef uint64_t u64;

#define QW 16u

/* The two kernel packets' VIF codes (read from the ELF and asserted by
 * tools/test_static_world_draw_reference.py): STCYCL 4,4, BASE 0x190 and the
 * OFFSET that returns the double buffer to BASE. */
#define KERNEL_CL 4u
#define KERNEL_WL 4u

/* GS registers (A+D addresses / PACKED REGS). */
#define GS_PRIM     0x00u
#define GS_RGBAQ    0x01u
#define GS_ST       0x02u
#define GS_XYZF2    0x04u
#define GS_TEX0_1   0x06u
#define GS_CLAMP_1  0x08u
#define GS_TEX1_1   0x14u
#define GS_TEXFLUSH 0x3Fu
#define GS_ALPHA_1  0x42u
#define GS_COLCLAMP 0x46u
#define GS_TEST_1   0x47u
#define GS_ZBUF_1   0x4Eu
#define REG_AD      0x0Eu
#define REG_NOP     0x0Fu

typedef struct {
    EmStaticWorldDraw *d;
    u32 program;              /* the CALLed kernel; 0 before the first CALL */
    u32 base, offset, dbf, tops;
    u32 cl, wl;
    /* GS */
    u32 prim, have_prim;
    u64 tex0, clamp, tex1, alpha, test, colclamp, zbuf;
    u32 set;                  /* EM_GFX_GS_* written so far */
    u32 zbuf_set;
    u32 s, t, q;              /* ST, and the Q PACKED ST sets */
    uint8_t rgba[4];          /* RGBAQ */
    EmGfxGsVertex queue[3];
    u32 queued;
} Walk;

static u32 rd32(const uint8_t *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }

static int fault(Walk *w, u32 code, u32 address, u32 detail)
{
    EmStaticWorldDraw *d = w->d;
    if (!d->fault) {
        d->fault = code;
        d->fault_address = address;
        d->fault_detail = detail;
    }
    return -1;
}

static const uint8_t *fetch(Walk *w, u32 address, u32 size)
{
    const uint8_t *p = w->d->read(w->d->read_ctx, address, size);
    if (!p) fault(w, EM_SWD_FAULT_READ, address, size);
    return p;
}

/* ---- GS ------------------------------------------------------------------ */

static int push_triangle(Walk *w, const EmGfxGsVertex *a, const EmGfxGsVertex *b,
                         const EmGfxGsVertex *c, int clip)
{
    EmStaticWorldDraw *d = w->d;
    if (d->prim_count == d->prim_capacity) {
        const u32 cap = d->prim_capacity ? d->prim_capacity * 2u : 4096u;
        EmGfxGsPrim *grown = realloc(d->prims, sizeof *grown * cap);
        if (!grown) return fault(w, EM_SWD_FAULT_CAPACITY, 0, cap);
        d->prims = grown;
        d->prim_capacity = cap;
    }
    EmGfxGsPrim *p = &d->prims[d->prim_count++];
    memset(p, 0, sizeof *p);
    p->prim = w->prim;
    p->set = w->set;
    p->tex0 = w->tex0;
    p->clamp = w->clamp;
    p->tex1 = w->tex1;
    p->alpha = w->alpha;
    p->test = w->test;
    p->colclamp = w->colclamp;
    p->count = 3;
    p->v[0] = *a;
    p->v[1] = *b;
    p->v[2] = *c;
    d->counts.triangles[clip ? 1 : 0]++;
    return 0;
}

static void prim_write(Walk *w, u32 prim)
{
    w->prim = prim & 0x7FFu;
    w->have_prim = 1;
    w->queued = 0;
}

/* One XYZF2 write: the vertex enters the queue; a drawing kick (ADC clear)
 * of a strip draws the last three, of a triangle list every third. */
static int vertex_kick(Walk *w, const u32 q[4], u32 address, int clip)
{
    if (!w->have_prim) return fault(w, EM_SWD_FAULT_GIF, address, GS_XYZF2);
    const u32 type = w->prim & 7u;
    if (type != 3u && type != 4u) return fault(w, EM_SWD_FAULT_GIF, address, w->prim);
    EmGfxGsVertex v;
    memset(&v, 0, sizeof v);
    v.x = (uint16_t)q[0];
    v.y = (uint16_t)q[1];
    v.z = (q[2] >> 4) & 0xFFFFFFu;
    v.f = (uint8_t)(q[3] >> 4);
    v.has_f = 1;
    const u32 adc = (q[3] >> 15) & 1u;
    v.s = w->s;
    v.t = w->t;
    v.q = w->q;
    memcpy(v.rgba, w->rgba, 4);
    if (type == 4u) {
        w->queue[0] = w->queue[1];
        w->queue[1] = w->queue[2];
        w->queue[2] = v;
        if (w->queued < 3u) w->queued++;
        if (w->queued == 3u && !adc)
            return push_triangle(w, &w->queue[0], &w->queue[1], &w->queue[2], clip);
        return 0;
    }
    /* triangle list: the clip kernel's, which never sets ADC */
    if (adc) return fault(w, EM_SWD_FAULT_GIF, address, q[3]);
    w->queue[w->queued] = v;
    if (++w->queued == 3u) {
        w->queued = 0;
        return push_triangle(w, &w->queue[0], &w->queue[1], &w->queue[2], clip);
    }
    return 0;
}

static int ad_write(Walk *w, u64 data, u32 reg, u32 address)
{
    switch (reg) {
    case GS_PRIM: prim_write(w, (u32)data); return 0;
    case GS_TEX0_1: w->tex0 = data; w->set |= EM_GFX_GS_TEX0; return 0;
    case GS_CLAMP_1: w->clamp = data; w->set |= EM_GFX_GS_CLAMP; return 0;
    case GS_TEX1_1: w->tex1 = data; w->set |= EM_GFX_GS_TEX1; return 0;
    case GS_ALPHA_1: w->alpha = data; w->set |= EM_GFX_GS_ALPHA; return 0;
    case GS_TEST_1: w->test = data; w->set |= EM_GFX_GS_TEST; return 0;
    case GS_COLCLAMP: w->colclamp = data; w->set |= EM_GFX_GS_COLCLAMP; return 0;
    case GS_ZBUF_1: w->zbuf = data; w->zbuf_set = 1; return 0;
    case GS_TEXFLUSH: return 0;
    default: return fault(w, EM_SWD_FAULT_GIF, address, reg);
    }
}

/* One GIF packet (PACKED tags up to EOP) from `qw` (count qwords, first at
 * original `address`, or a dmem address for a kick). Returns the qwords
 * consumed, or -1. */
static int gif_packet(Walk *w, const uint8_t *qw, u32 count, u32 address, int clip)
{
    u32 at = 0;
    for (;;) {
        if (at >= count) return fault(w, EM_SWD_FAULT_GIF, address, at);
        const uint8_t *tag = qw + at * QW;
        const u64 lo = (u64)rd32(tag) | (u64)rd32(tag + 4) << 32;
        const u64 regs = (u64)rd32(tag + 8) | (u64)rd32(tag + 12) << 32;
        const u32 nloop = (u32)(lo & 0x7FFFu), eop = (u32)(lo >> 15) & 1u;
        const u32 pre = (u32)(lo >> 46) & 1u, flg = (u32)(lo >> 58) & 3u;
        u32 nreg = (u32)(lo >> 60) & 15u;
        if (!nreg) nreg = 16u;
        if (flg != 0u) return fault(w, EM_SWD_FAULT_GIF, address + at * QW, (u32)(lo >> 32));
        if (pre) prim_write(w, (u32)(lo >> 47));
        ++at;
        if (at + nloop * nreg > count) return fault(w, EM_SWD_FAULT_GIF, address, count);
        for (u32 l = 0; l < nloop; ++l) {
            for (u32 g = 0; g < nreg; ++g, ++at) {
                const uint8_t *p = qw + at * QW;
                const u32 q[4] = { rd32(p), rd32(p + 4), rd32(p + 8), rd32(p + 12) };
                const u32 reg = (u32)(regs >> (4u * g)) & 15u;
                switch (reg) {
                case REG_NOP: break;
                case GS_TEX0_1:
                    w->tex0 = (u64)q[0] | (u64)q[1] << 32;
                    w->set |= EM_GFX_GS_TEX0;
                    break;
                case GS_ST:
                    w->s = q[0]; w->t = q[1]; w->q = q[2];
                    break;
                case GS_RGBAQ:              /* each lane's low byte; Q stays ST's */
                    for (unsigned k = 0; k < 4; ++k) w->rgba[k] = (uint8_t)q[k];
                    break;
                case GS_XYZF2:
                    if (vertex_kick(w, q, address + at * QW, clip) < 0) return -1;
                    break;
                case REG_AD:
                    if (ad_write(w, (u64)q[0] | (u64)q[1] << 32, q[2] & 0xFFu, address + at * QW) < 0)
                        return -1;
                    break;
                default:
                    return fault(w, EM_SWD_FAULT_GIF, address + at * QW, reg);
                }
            }
        }
        if (eop) return (int)at;
    }
}

/* ---- VU1 ----------------------------------------------------------------- */

/* The packet an XGKICK at dmem `a` sends: its qwords up to the EOP tag. */
static int kick_extent(const EmVu1ObjQword *dmem, u32 a, u32 *count)
{
    u32 q = a;
    for (unsigned t = 0; t < 64u; ++t) {
        const EmVu1ObjQword *tag = &dmem[q & 1023u];
        const u32 nloop = tag->w[0] & 0x7FFFu, flg = (tag->w[1] >> 26) & 3u;
        u32 nreg = tag->w[1] >> 28;
        if (!nreg) nreg = 16u;
        q += 1u + (flg == 0u ? nloop * nreg : flg == 1u ? (nloop * nreg + 1u) / 2u : nloop);
        if ((tag->w[0] >> 15) & 1u) {
            *count = q - a;
            return 0;
        }
    }
    return -1;
}

static int kicked(Walk *w, u32 program, u32 top, const EmVu1ObjQword *qw, u32 count, u32 address,
                  int clip)
{
    EmStaticWorldDraw *d = w->d;
    d->counts.kicks++;
    uint8_t packet[1024 * QW];
    if (count > 1024u) return fault(w, EM_SWD_FAULT_GIF, address, count);
    for (u32 i = 0; i < count; ++i)
        for (unsigned k = 0; k < 4; ++k) {
            const u32 v = qw[i].w[k];
            packet[i * QW + 4 * k] = (uint8_t)v;
            packet[i * QW + 4 * k + 1] = (uint8_t)(v >> 8);
            packet[i * QW + 4 * k + 2] = (uint8_t)(v >> 16);
            packet[i * QW + 4 * k + 3] = (uint8_t)(v >> 24);
        }
    if (d->kick) d->kick(d->kick_ctx, program, top, &qw[0].w[0], count);
    const int used = gif_packet(w, packet, count, address, clip);
    if (used < 0) return -1;
    if ((u32)used != count) return fault(w, EM_SWD_FAULT_GIF, address, (u32)used);
    return 0;
}

static int level_batch(Walk *w, u32 top, u32 address)
{
    EmStaticWorldDraw *d = w->d;
    EmVu1LvlBatch b;
    if (em_vu1_level_kernel_batch_host(&d->level, d->dmem, top, &b) < 0)
        return fault(w, EM_SWD_FAULT_VU, address, b.fault << 8 | b.fault_vertex);
    for (u32 i = 0; i < EM_VU1_LVL_VERTICES; ++i)
        if (b.why[i] == EM_VU1_LVL_ADC_CULL) d->counts.culled++;
    u32 count;
    if (kick_extent(d->dmem, b.kick, &count) < 0) return fault(w, EM_SWD_FAULT_GIF, address, b.kick);
    EmVu1ObjQword packet[1024];
    for (u32 i = 0; i < count; ++i) packet[i] = d->dmem[(b.kick + i) & 1023u];
    return kicked(w, EM_SWD_LEVEL_KERNEL, top, packet, count, address, 0);
}

static int clip_batch(Walk *w, u32 top, u32 address)
{
    EmStaticWorldDraw *d = w->d;
    /* em_vu1_shadow_clip_run works on its own qword type: run it on a copy
     * of the data memory and copy back what it wrote. */
    static EmVu1Qword mem[EM_VU1_DMEM_QWORDS];
    static EmVu1ClipResult res;
    for (u32 i = 0; i < EM_VU1_DMEM_QWORDS; ++i) memcpy(mem[i].w, d->dmem[i].w, QW);
    const int rc = em_vu1_shadow_clip_run(EM_VU1_CLIP_BOX, mem, top, &res);
    for (u32 i = 0; i < EM_VU1_DMEM_QWORDS; ++i) memcpy(d->dmem[i].w, mem[i].w, QW);
    /* The clip program leaves its own values in the registers the level
     * kernel carries. */
    em_vu1_level_kernel_forget(&d->level);
    if (rc < 0) return fault(w, EM_SWD_FAULT_VU, address, 0x100u | res.fault);
    for (u32 k = 0; k < res.kicks; ++k) {
        const EmVu1ClipKick *kick = &res.kick[k];
        EmVu1ObjQword packet[EM_VU1_CLIP_MAX_PACKET + 4];
        if (kick->count > sizeof packet / sizeof packet[0])
            return fault(w, EM_SWD_FAULT_GIF, address, kick->count);
        for (u32 i = 0; i < kick->count; ++i) memcpy(packet[i].w, res.qw[kick->first + i].w, QW);
        if (kicked(w, EM_SWD_CLIP_KERNEL, top, packet, kick->count, address, 1) < 0) return -1;
    }
    return 0;
}

static int program_run(Walk *w, u32 address)
{
    EmStaticWorldDraw *d = w->d;
    const u32 top = w->tops;
    w->dbf ^= 1u;
    w->tops = w->base + (w->dbf ? w->offset : 0u);
    if (w->program == EM_SWD_LEVEL_KERNEL) {
        d->counts.batches[0]++;
        return level_batch(w, top, address);
    }
    if (w->program == EM_SWD_CLIP_KERNEL) {
        d->counts.batches[1]++;
        return clip_batch(w, top, address);
    }
    return fault(w, EM_SWD_FAULT_PROGRAM, address, w->program);
}

/* ---- VIF1 ---------------------------------------------------------------- */

/* The words at [address, address + size) as VIF1 receives them. `gs_state`:
 * the REF of 001D1F80(0, 1, 0), the only source of DIRECT. */
static int vif(Walk *w, u32 address, u32 size, int gs_state)
{
    const uint8_t *b = fetch(w, address, size);
    if (!b) return -1;
    u32 i = 0;
    while (i < size) {
        if (i + 4u > size) return fault(w, EM_SWD_FAULT_VIF, address + i, 0);
        const u32 v = rd32(b + i);
        const u32 cmd = (v >> 24) & 0x7Fu, num = (v >> 16) & 0xFFu, imm = v & 0xFFFFu;
        const u32 at = address + i;
        if ((cmd & 0x60u) == 0x60u) {                         /* UNPACK */
            if ((cmd & 0x1Fu) != 0x0Cu || (imm & 0x4000u)) return fault(w, EM_SWD_FAULT_VIF, at, v);
            if (w->cl != w->wl || !w->wl) return fault(w, EM_SWD_FAULT_VIF, at, w->cl << 8 | w->wl);
            const u32 n = num ? num : 256u;
            if (i + 4u + n * QW > size) return fault(w, EM_SWD_FAULT_VIF, at, v);
            const u32 dst = (imm & 0x3FFu) + ((imm & 0x8000u) ? w->tops : 0u);
            for (u32 k = 0; k < n; ++k) {
                const uint8_t *p = b + i + 4u + k * QW;
                EmVu1ObjQword *q = &w->d->dmem[(dst + k) & 1023u];
                q->w[0] = rd32(p); q->w[1] = rd32(p + 4); q->w[2] = rd32(p + 8); q->w[3] = rd32(p + 12);
            }
            i += 4u + n * QW;
            continue;
        }
        switch (cmd) {
        case 0x00: case 0x10: case 0x11: case 0x13:           /* NOP, FLUSHE, FLUSH, FLUSHA */
            i += 4;
            continue;
        case 0x01:                                            /* STCYCL */
            w->cl = imm & 0xFFu;
            w->wl = (imm >> 8) & 0xFFu;
            i += 4;
            continue;
        case 0x14:                                            /* MSCAL */
            if (imm != 0u) return fault(w, EM_SWD_FAULT_PROGRAM, at, v);
            /* fall through */
        case 0x17:                                            /* MSCNT */
            if (program_run(w, at) < 0) return -1;
            i += 4;
            continue;
        case 0x50: {                                          /* DIRECT */
            if (!gs_state) return fault(w, EM_SWD_FAULT_VIF, at, v);
            const u32 n = imm ? imm : 65536u;
            const u32 data = (i + 4u + 15u) & ~15u;
            if (data + n * QW > size) return fault(w, EM_SWD_FAULT_VIF, at, v);
            const int used = gif_packet(w, b + data, n, address + data, 0);
            if (used < 0) return -1;
            if ((u32)used != n) return fault(w, EM_SWD_FAULT_GIF, at, (u32)used);
            i = data + n * QW;
            continue;
        }
        default:
            return fault(w, EM_SWD_FAULT_VIF, at, v);
        }
    }
    return 0;
}

/* ---- DMA ----------------------------------------------------------------- */

static int call_kernel(Walk *w, u32 target, u32 at)
{
    EmStaticWorldDraw *d = w->d;
    if (target == EM_SWD_LEVEL_KERNEL) {
        w->offset = EM_VU1_LVL_OFFSET;
        d->counts.calls[0]++;
    } else if (target == EM_SWD_CLIP_KERNEL) {
        w->offset = EM_SWD_CLIP_OFFSET;
        d->counts.calls[1]++;
    } else {
        return fault(w, EM_SWD_FAULT_PROGRAM, at, target);
    }
    w->program = target;
    w->cl = KERNEL_CL;
    w->wl = KERNEL_WL;
    w->base = EM_VU1_LVL_BASE;
    w->dbf = 0;
    w->tops = w->base;
    return 0;
}

int em_static_world_draw_run(EmStaticWorldDraw *d, u32 start, u32 end)
{
    if (!d) return -1;
    d->prim_count = 0;
    memset(&d->counts, 0, sizeof d->counts);
    d->fault = EM_SWD_OK;
    d->fault_address = d->fault_detail = 0;
    Walk w;
    memset(&w, 0, sizeof w);
    w.d = d;
    w.cl = d->cl;
    w.wl = d->wl;
    if (!d->read || end < start || ((start | end) & 15u)) return fault(&w, EM_SWD_FAULT_ARGS, start, end);
    u32 a = start;
    while (a < end) {
        const uint8_t *tag = fetch(&w, a, QW);
        if (!tag) return -1;
        const u32 w0 = rd32(tag), addr = rd32(tag + 4);
        const u32 id = (w0 >> 28) & 7u, qwc = w0 & 0xFFFFu;
        d->counts.tags++;
        switch (id) {
        case 1:                                               /* CNT */
            if (qwc && vif(&w, a + QW, qwc * QW, 0) < 0) return -1;
            a += QW * (1u + qwc);
            break;
        case 3:                                               /* REF */
            if (addr == EM_SWD_GS_STATE) {
                const char *why = NULL;
                const uint8_t *state = fetch(&w, addr, qwc * QW);
                if (!state) return -1;
                if (qwc != 9u || em_object_unit_gs_state_check(state, &why) < 0)
                    return fault(&w, EM_SWD_FAULT_GS_STATE, a, qwc);
                d->counts.gs_states++;
            }
            if (qwc && vif(&w, addr & ~15u, qwc * QW, addr == EM_SWD_GS_STATE) < 0) return -1;
            a += QW;
            break;
        case 5:                                               /* CALL */
            if (qwc != 0u) return fault(&w, EM_SWD_FAULT_DMA, a, w0);
            if (call_kernel(&w, addr, a) < 0) return -1;
            a += QW;
            break;
        default:
            return fault(&w, EM_SWD_FAULT_DMA, a, w0);
        }
    }
    if (a != end) return fault(&w, EM_SWD_FAULT_DMA, a, end);
    d->cl = (uint8_t)w.cl;
    d->wl = (uint8_t)w.wl;
    return 0;
}

void em_static_world_draw_free(EmStaticWorldDraw *d)
{
    if (!d) return;
    free(d->prims);
    d->prims = NULL;
    d->prim_count = d->prim_capacity = 0;
}

const char *em_static_world_draw_fault_name(u32 fault)
{
    switch (fault) {
    case EM_SWD_OK: return "none";
    case EM_SWD_FAULT_ARGS: return "arguments";
    case EM_SWD_FAULT_READ: return "unmapped address";
    case EM_SWD_FAULT_DMA: return "DMA tag";
    case EM_SWD_FAULT_VIF: return "VIF code";
    case EM_SWD_FAULT_PROGRAM: return "VU1 program";
    case EM_SWD_FAULT_VU: return "kernel translation";
    case EM_SWD_FAULT_GIF: return "GIF packet";
    case EM_SWD_FAULT_GS_STATE: return "GS state";
    case EM_SWD_FAULT_CAPACITY: return "out of memory";
    default: return "unknown";
    }
}
