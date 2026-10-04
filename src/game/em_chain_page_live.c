/* em_chain_page_live.c - the chain page drawn live at the frame close
 * (em_chain_page_live.h, docs/CHAIN_PAGE.md section 7). */
#include "game/em_chain_page_live.h"
#include "game/em_object_unit.h"
#include "game/em_owner_draw_live.h"
#include "game/em_world_textures_live.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "game/em_effects_live.h"
#include "game/em_frame.h"
#include "game/em_render_context_live.h"
#include "game/em_shadow_decal_original.h"
#include "game/em_shadow_live.h"

#define CLD_MASK (~(UINT64_C(7) << 61))
#define READS_MAX 40000u

static struct {
    EmChainPage page;
    EmGfxGsPrim prims[EM_CHAIN_PAGE_LIVE_PRIMS];
    EmChainPageQ q[EM_CHAIN_PAGE_LIVE_PRIMS];
    uint32_t skip[1];
    uint32_t fault;
    EmChainPageLiveLog log;
    struct { uint32_t address, size; } reads[READS_MAX];
    uint32_t nreads;
    uint32_t overlay_reads;
} S;

static int fail(uint32_t address, const char *what)
{
    if (!S.fault) {
        S.fault = address;
        fprintf(stderr, "chain page: %s (at %08X)\n", what, (unsigned)address);
    }
    return -1;
}

uint32_t em_chain_page_live_fault(void) { return S.fault; }

/* Original memory the page reads: the render context's storage (the arena,
 * the chain table, the context, the GS blocks, the .data it exports), then
 * the effect-table export's ELF blocks (the program packets, 001CFBE0's
 * source blocks) and the overlay source blocks 001D04B0 was handed (the
 * AREA11 flame's D_00828340). Nothing else is mapped. */
const uint8_t *em_chain_page_live_read(uint32_t address, uint32_t size)
{
    const uint8_t *p = em_rcl_bytes(address, size);
    return p ? p : em_effects_live_window(address, size);
}

static const uint8_t *reader(void *ctx, uint32_t address, uint32_t size)
{
    (void)ctx;
    const uint8_t *p = em_chain_page_live_read(address, size);
    if (p && address >= 0x00800000u && !em_rcl_bytes(address, size)) S.overlay_reads++;
    if (p && S.nreads < READS_MAX) {
        S.reads[S.nreads].address = address;
        S.reads[S.nreads].size = size;
        S.nreads++;
    }
    return p;
}

int em_chain_page_live_textures(EmGfx *gfx)
{
    return em_world_textures_live_ensure(gfx);
}

/* FNV-1a over the primitives as the consumer handed them (before the Q
 * substitution), the layout tools/test_level_smoke.py rebuilds. */
static uint32_t fnv(uint32_t h, uint32_t w)
{
    for (unsigned k = 0; k < 4; ++k) {
        h ^= (w >> (8 * k)) & 0xFFu;
        h *= 16777619u;
    }
    return h;
}

static uint32_t digest_more(uint32_t h, const EmGfxGsPrim *p, const EmChainPageQ *q, uint32_t n);

static uint32_t digest(const EmGfxGsPrim *p, const EmChainPageQ *q, uint32_t n)
{
    return digest_more(2166136261u, p, q, n);
}

static uint32_t digest_more(uint32_t h, const EmGfxGsPrim *p, const EmChainPageQ *q, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i) {
        const uint64_t st[6] = { p[i].tex0, p[i].clamp, p[i].tex1, p[i].alpha, p[i].test, p[i].colclamp };
        h = fnv(h, p[i].prim);
        h = fnv(h, p[i].set);
        for (unsigned k = 0; k < 6; ++k) {
            h = fnv(h, (uint32_t)st[k]);
            h = fnv(h, (uint32_t)(st[k] >> 32));
        }
        h = fnv(h, p[i].count);
        for (uint32_t k = 0; k < p[i].count; ++k) {
            const EmGfxGsVertex *v = &p[i].v[k];
            h = fnv(h, v->x);
            h = fnv(h, v->y);
            h = fnv(h, v->z);
            h = fnv(h, v->f);
            h = fnv(h, v->has_f);
            h = fnv(h, (uint32_t)v->rgba[0] | (uint32_t)v->rgba[1] << 8 | (uint32_t)v->rgba[2] << 16 |
                       (uint32_t)v->rgba[3] << 24);
            h = fnv(h, v->q);
            h = fnv(h, v->s);
            h = fnv(h, v->t);
            h = fnv(h, v->u);
            h = fnv(h, v->v);
            h = fnv(h, q[i].q_known[k]);
        }
    }
    return h;
}

/* A CALL's target that is this frame's 001CABA0 unit (em_owner_draw_live):
 * its triangles (em_object_unit_run: the object kernel and the clip
 * program's translations) as GS primitives with the class-2 state, each
 * with its PRIM (the template's strip, the clip pass's list). */
static int page_unit(void *ctx, uint32_t address, EmGfxGsPrim *out, uint32_t capacity, uint32_t *count)
{
    (void)ctx;
    static EmObjectUnitResult r;
    *count = 0;
    const EmObjectUnitPieces *q = em_owner_draw_live_page_unit(address);
    if (!q) return 0;
    if (q->unit.gs_class != 2u || em_object_unit_run(&q->unit, &r) < 0 || r.count > capacity) {
        fprintf(stderr, "chain page: the class-2 unit at %08X is refused: %s\n", (unsigned)address,
                r.why ? r.why : "not class 2, or too many triangles");
        return -1;
    }
    uint64_t alpha, test, tex1, clamp, colclamp;
    em_object_unit_gs_state(2u, &alpha, &test, &tex1, &clamp, &colclamp);
    for (uint32_t i = 0; i < r.count; ++i) {
        const EmObjectUnitTriangle *t = &r.tri[i];
        EmGfxGsPrim *o = &out[i];
        memset(o, 0, sizeof *o);
        o->prim = t->pass ? r.prim - 1u : r.prim;
        o->set = EM_GFX_GS_TEX0 | EM_GFX_GS_CLAMP | EM_GFX_GS_TEX1 | EM_GFX_GS_ALPHA | EM_GFX_GS_TEST |
                 EM_GFX_GS_COLCLAMP;
        o->tex0 = t->tex0;
        o->clamp = clamp;
        o->tex1 = tex1;
        o->alpha = alpha;
        o->test = test;
        o->colclamp = colclamp;
        o->count = 3;
        for (unsigned c = 0; c < 3u; ++c) {
            EmGfxGsVertex *g = &o->v[c];
            g->x = t->v[c].x;
            g->y = t->v[c].y;
            g->z = t->v[c].z;
            g->f = t->v[c].f;
            g->has_f = 1;
            memcpy(g->rgba, t->v[c].rgba, 4);
            g->q = t->v[c].q;
            g->s = t->v[c].s;
            g->t = t->v[c].t;
        }
    }
    *count = r.count;
    return 1;
}

int em_chain_page_live_draw(EmGfx *gfx)
{
    if (S.fault) return -1;
    uint32_t start, four;
    if (em_rcl_page(&start, &four) < 0) return 0;              /* no kick since the last draw */
    if (!gfx) return fail(0x001CB800u, "no frame to draw the page in");
    if (em_chain_page_live_textures(gfx) < 0) return fail(0x001CB800u, "no page textures");
    EmChainPage *p = &S.page;
    memset(p, 0, offsetof(EmChainPage, regs));
    p->read = reader;
    p->prims = S.prims;
    p->prim_q = S.q;
    p->prim_capacity = EM_CHAIN_PAGE_LIVE_PRIMS;
    S.skip[0] = four;
    p->skip_calls = S.skip;
    p->skip_count = four ? 1u : 0u;
    p->unit = page_unit;
    S.nreads = 0;
    S.overlay_reads = 0;
    if (em_chain_page_run(p, start) < 0) {
        fprintf(stderr, "chain page: %s fault at %08X (%08X)\n", em_chain_page_fault_name(p->fault),
                (unsigned)p->fault_address, (unsigned)p->fault_detail);
        return fail(0x001CB800u, "the page cannot be walked as the original's");
    }
    if (four && p->counts.skipped != 1u) return fail(0x001DDE10u, "001DDE10's slot-0xFFF CALL is not in the page");
    uint32_t decal = 0, flare = 0;
    for (uint32_t i = 0; i < p->prim_count; ++i) {
        if ((p->prims[i].prim & 7u) == 5u && ((p->prims[i].tex0 ^ EM_SHADOW_DECAL_TEX0) & CLD_MASK) == 0)
            decal++;
        /* 00187780's flare words (00187690's TEX0 row, modes 0 / 1). */
        if ((p->prims[i].prim & 7u) == 6u && (((p->prims[i].tex0 ^ UINT64_C(0x20045D05554221F6)) & CLD_MASK) == 0 ||
                                              ((p->prims[i].tex0 ^ UINT64_C(0x20048D0599422050)) & CLD_MASK) == 0))
            flare++;
    }
    S.log.frame = em_frame_counter();
    S.log.pages++;
    S.log.start = start;
    S.log.four_sprite = four;
    S.log.counts = p->counts;
    S.log.decal_triangles = decal;
    S.log.flare_sprites = flare;
    S.log.weather = em_rcl_page_weather();
    S.log.overlay_reads = S.overlay_reads;
    S.log.digest = digest(p->prims, S.q, p->prim_count);
    /* The same digest over the primitives the unit CALLs did not draw. */
    S.log.units = p->counts.units;
    {
        uint32_t h = 2166136261u, from = 0;
        for (uint32_t k = 0; k <= p->counts.units; ++k) {
            const uint32_t to = k < p->counts.units ? p->unit_first[k] : p->prim_count;
            h = digest_more(h, p->prims + from, S.q + from, to - from);
            if (k < p->counts.units) {
                S.log.unit_call[k] = p->unit_call[k];
                S.log.unit_prims[k] = p->unit_prims[k];
                S.log.unit_strips[k] = 0;
                S.log.unit_tex0[k] = p->unit_tex0[k];
                S.log.unit_prim[k] = p->unit_prim[k];
                for (uint32_t i = 0; i < p->unit_prims[k]; ++i)
                    S.log.unit_strips[k] += (p->prims[p->unit_first[k] + i].prim & 7u) == 4u;
                from = p->unit_first[k] + p->unit_prims[k];
            }
        }
        S.log.digest_without_units = h;
    }
    S.log.total_prims += p->prim_count;
    S.log.total_stale_q += p->counts.stale_q;
    S.log.total_skipped += p->counts.skipped;
    if (em_shadow_live_bound() && em_shadow_live_page_drew(decal) < 0)
        return fail(0x001CE300u, "the decal the page drew is not the 0015BF90 route's");
    /* Every vertex's Q is its GIF tag's (em_chain_page_live.h). */
    if (p->counts.stale_q)
        return fail(0x001CB800u, "a page vertex has no Q");
    if (em_gfx_gs_prims(gfx, p->prims, p->prim_count) < 0)
        return fail(0x001CB800u, "a page primitive cannot be drawn exactly");
    return 0;
}

void em_chain_page_live_log(EmChainPageLiveLog *out)
{
    if (out) *out = S.log;
}

const EmGfxGsPrim *em_chain_page_live_prims(uint32_t *count)
{
    if (count) *count = S.page.prim_count;
    return S.log.pages ? S.prims : NULL;
}

uint32_t em_chain_page_live_reads(const uint32_t **pairs)
{
    if (pairs) *pairs = &S.reads[0].address;
    return S.nreads;
}

const uint8_t *em_chain_page_live_q(void)
{
    return &S.q[0].q_known[0];
}
