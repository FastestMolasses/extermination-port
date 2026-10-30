/* em_chain_page_live.c - the chain page drawn live at the frame close
 * (em_chain_page_live.h, docs/CHAIN_PAGE.md section 7). */
#include "game/em_chain_page_live.h"
#include "game/em_area11_roger.h"
#include "game/em_aim_fire_tables.h"
#include "game/em_aim_fire_world_live.h"
#include "game/em_diag.h"
#include "game/em_object_unit.h"
#include "game/em_owner_draw_live.h"
#include "game/em_world_textures_live.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
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
    EmGfxGsEnv env[EM_CHAIN_PAGE_LIVE_PRIMS];
    uint32_t skip[EM_CHAIN_PAGE_PASSES_MAX];
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
    if (!p) p = em_effects_live_window(address, size);
    /* The beam's (001E2BA0 / 001E2800) REF of D_00253720, the immutable
     * ELF packet the aim/fire tables hold (em_aim_fire_tables, its one
     * export): the AREA01 revisit's 0x8282F0 draws it into the page. */
    if (!p && em_aim_fire_tables_contains(address, size)) {
        p = em_aim_fire_tables_bytes(address, size);
        if (!p && em_aim_fire_tables_load() == 0) p = em_aim_fire_tables_bytes(address, size);
    }
    /* The library models' blocks (the Roger export's D_0028A56C spans,
     * read only): the gun lamp's cone shells (001D9530's 001D4F30 /
     * 001D4A90 REF the blocks of models 0x10 / 0x11 / 0x16 into channel 3). */
    if (!p) {
        uint32_t rest = 0;
        const uint8_t *q = em_area11_roger_library_rest(address, &rest);
        if (q && size <= rest) p = q;
    }
    return p;
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

/* FNV-1a over the pass's environments and environment-again marks (the
 * layout tools/level_smoke_chain_page.py rebuilds from the model's walk):
 * per primitive the set word and the ten registers (low, high), then the
 * mark count and each mark's index relative to the pass. */
static uint32_t pass_digest(const EmGfxGsEnv *e, uint32_t n, const uint32_t *again, uint32_t again_count)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; ++i) {
        const uint64_t r[10] = { e[i].frame, e[i].zbuf, e[i].xyoffset, e[i].scissor, e[i].prmodecont, e[i].dthe,
                                 e[i].fba, e[i].pabe, e[i].texa, e[i].scanmsk };
        h = fnv(h, e[i].set);
        for (unsigned k = 0; k < 10u; ++k) {
            h = fnv(h, (uint32_t)r[k]);
            h = fnv(h, (uint32_t)(r[k] >> 32));
        }
    }
    h = fnv(h, again_count);
    for (uint32_t k = 0; k < again_count; ++k) h = fnv(h, again[k]);
    return h;
}

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

/* A CALL's target that is this frame's 001CABA0 (class 2) or AREA02 001CAE40
 * (class 1) unit (em_owner_draw_live): its triangles (em_object_unit_run:
 * the object kernel and the clip program's translations) as GS primitives
 * with the class's state, each
 * with its PRIM (the template's strip, the clip pass's list). */
static int page_unit(void *ctx, uint32_t address, EmGfxGsPrim *out, uint32_t capacity, uint32_t *count)
{
    (void)ctx;
    static EmObjectUnitResult r;
    *count = 0;
    const EmObjectUnitPieces *q = em_owner_draw_live_page_unit(address);
    if (!q) return 0;
    if ((q->unit.gs_class != 1u && q->unit.gs_class != 2u) || em_object_unit_run(&q->unit, &r) < 0 ||
        r.count > capacity) {
        fprintf(stderr, "chain page: the class-%u unit at %08X is refused: %s\n", (unsigned)q->unit.gs_class,
                (unsigned)address, r.why ? r.why : "not class 1 or 2, or too many triangles");
        return -1;
    }
    uint64_t alpha, test, tex1, clamp, colclamp;
    em_object_unit_gs_state(q->unit.gs_class, &alpha, &test, &tex1, &clamp, &colclamp);
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

/* Verification aid (observe only): EM_LAMP_PAGE_DUMP=<file> appends, for
 * the first three pages that ran the level kernel 00237180 (the gun lamp's
 * cone shells, 001D9530), one JSON line (for every later one, its frame,
 * its kernel counts and the 0x700030D0 count only): the start tag, the slot-0xFFF
 * CALLs (and whether they were walked as passes, with the draw
 * environments), the class-2 unit CALLs with the state they leave, the
 * counts, the digest of the primitives outside the units and every
 * (address, bytes) the walk read, for tools/test_aim_fire_lamp_cone.py to
 * walk again with the ORIGINAL microcode (tools/chain_page_model.py). */
static void lamp_dump(const EmChainPage *p, uint32_t start, int pass, const uint32_t *targets, uint32_t npass,
                      uint32_t bank, uint32_t slot)
{
    static int on = -1;
    static unsigned dumped;
    if (on < 0) on = getenv("EM_LAMP_PAGE_DUMP") && *getenv("EM_LAMP_PAGE_DUMP");
    if (!on || !p->counts.mscal_level) return;
    FILE *f = fopen(getenv("EM_LAMP_PAGE_DUMP"), dumped ? "a" : "w");
    if (!f) return;
    if (dumped >= 3u) {
        /* later cone pages: their counts only */
        fprintf(f, "{\"frame\": %u, \"mscal_level\": %u, \"mscal_box_clip\": %u, \"cell_quadwords\": %u}\n",
                (unsigned)em_frame_counter(), (unsigned)p->counts.mscal_level, (unsigned)p->counts.mscal_box_clip,
                (unsigned)em_aim_fire_world_live_cell_quadwords());
        fclose(f);
        return;
    }
    dumped++;
    uint32_t h = 2166136261u, from = 0, unit_prims = 0;
    for (uint32_t k = 0; k <= p->counts.units; ++k) {
        const uint32_t to = k < p->counts.units ? p->unit_first[k] : p->prim_count;
        h = digest_more(h, p->prims + from, S.q + from, to - from);
        if (k < p->counts.units) {
            from = p->unit_first[k] + p->unit_prims[k];
            unit_prims += p->unit_prims[k];
        }
    }
    fprintf(f, "{\"frame\": %u, \"start\": %u, \"pass\": %d, \"draw_envs\": %u, \"slot\": %u, \"targets\": [",
            (unsigned)em_frame_counter(), (unsigned)start, pass, (unsigned)bank, (unsigned)slot);
    for (uint32_t k = 0; k < npass; ++k) fprintf(f, "%s%u", k ? ", " : "", (unsigned)targets[k]);
    fputs("], \"units\": [", f);
    /* each unit's CALL, the TEX0 and PRIM it leaves and its class's state
     * (ALPHA, TEST, TEX1, CLAMP, COLCLAMP: class 2's, or AREA02's class 1) */
    for (uint32_t k = 0; k < p->counts.units && k < EM_CHAIN_PAGE_UNITS_MAX; ++k) {
        const EmGfxGsPrim *g = p->unit_prims[k] ? &p->prims[p->unit_first[k] + p->unit_prims[k] - 1u] : NULL;
        fprintf(f, "%s[%u, %llu, %u", k ? ", " : "", (unsigned)p->unit_call[k],
                (unsigned long long)p->unit_tex0[k], (unsigned)p->unit_prim[k]);
        if (g)
            fprintf(f, ", %llu, %llu, %llu, %llu, %llu", (unsigned long long)g->alpha, (unsigned long long)g->test,
                    (unsigned long long)g->tex1, (unsigned long long)g->clamp, (unsigned long long)g->colclamp);
        fputc(']', f);
    }
    fprintf(f, "], \"prims\": %u, \"directs\": %u, \"kicks\": %u, \"mscal_level\": %u, \"mscal_box_clip\": %u, "
               "\"digest\": %u, \"cell_quadwords\": %u, \"prim_digests\": [",
            (unsigned)(p->prim_count - unit_prims), (unsigned)p->counts.direct, (unsigned)p->counts.kicks,
            (unsigned)p->counts.mscal_level, (unsigned)p->counts.mscal_box_clip, (unsigned)h,
            (unsigned)em_aim_fire_world_live_cell_quadwords());
    int written = 0;
    for (uint32_t k = 0, i = 0; i < p->prim_count; ++i) {
        while (k < p->counts.units && i >= p->unit_first[k] + p->unit_prims[k]) ++k;
        if (k < p->counts.units && i >= p->unit_first[k]) continue;
        fprintf(f, "%s%u", written++ ? ", " : "", (unsigned)digest_more(2166136261u, p->prims + i, S.q + i, 1));
    }
    fputs("], \"reads\": [", f);
    written = 0;
    for (uint32_t r = 0; r < S.nreads; ++r) {
        int seen = 0;
        for (uint32_t k = 0; k < r && !seen; ++k)
            seen = S.reads[k].address == S.reads[r].address && S.reads[k].size == S.reads[r].size;
        if (seen) continue;
        const uint8_t *b = em_chain_page_live_read(S.reads[r].address, S.reads[r].size);
        if (!b) continue;
        fprintf(f, "%s[%u, \"", written++ ? ", " : "", (unsigned)S.reads[r].address);
        for (uint32_t i = 0; i < S.reads[r].size; ++i) fprintf(f, "%02x", b[i]);
        fputs("\"]", f);
    }
    fputs("]}\n", f);
    fclose(f);
}

int em_chain_page_live_draw(EmGfx *gfx)
{
    if (S.fault) return -1;
    uint32_t start, targets[EM_CHAIN_PAGE_PASSES_MAX], npass;
    if (em_rcl_page(&start, targets, &npass) < 0) return 0;     /* no kick since the last draw */
    if (!gfx) return fail(0x001CB800u, "no frame to draw the page in");
    if (em_chain_page_live_textures(gfx) < 0) return fail(0x001CB800u, "no page textures");
    /* The frame's slot-0xFFF CALLs (CHAIN_PAGE.md section 6.2: 001DDE10's
     * depth-of-field pass, the ramp lists of 001DF110 / 001DF180): the GS
     * model draws each in the Original profile's recorded world frame (pass
     * mode); the GPU renderer walks over them. With the GS frame on, a pass
     * outside a recorded world frame cannot be drawn: a fault. */
    const uint32_t four = npass ? targets[npass - 1u] : 0u;
    const int pass = npass && em_gfx_gs_world_recording(gfx);
    if (npass && !pass && em_gfx_gs_world_enabled(gfx))
        return fail(four, "a slot-0xFFF pass outside a recorded GS world frame");
    uint32_t bank = 0, slot = 0;
    uint64_t field_frame = 0, field_scissor = 0;
    if (pass && em_rcl_draw_env(&bank, &slot, &field_frame, &field_scissor) < 0)
        return fail(0x001D1F20u, "no draw environment for the pass (bank A, context +0x9C)");
    EmChainPage *p = &S.page;
    memset(p, 0, offsetof(EmChainPage, regs));
    p->read = reader;
    p->prims = S.prims;
    p->prim_q = S.q;
    p->prim_env = S.env;
    p->prim_capacity = EM_CHAIN_PAGE_LIVE_PRIMS;
    memcpy(S.skip, targets, sizeof S.skip);
    p->skip_calls = S.skip;
    p->skip_count = pass ? 0u : npass;
    p->pass_calls = S.skip;
    p->pass_count = pass ? npass : 0u;
    p->draw_envs = bank;
    p->draw_env_slot = slot;
    p->unit = page_unit;
    S.nreads = 0;
    S.overlay_reads = 0;
    if (em_chain_page_run(p, start) < 0) {
        fprintf(stderr, "chain page: %s fault at %08X (%08X)\n", em_chain_page_fault_name(p->fault),
                (unsigned)p->fault_address, (unsigned)p->fault_detail);
        return fail(0x001CB800u, "the page cannot be walked as the original's");
    }
    if (npass && (pass ? p->counts.passes != npass || p->counts.skipped : p->counts.skipped != npass))
        return fail(four, "a slot-0xFFF CALL of the frame is not in the page");
    /* Each pass's primitives and marks (their own: relative to the pass). */
    const uint32_t passes = pass ? p->counts.passes : 0u;
    uint32_t again[EM_CHAIN_PAGE_AGAIN_MAX], from = 0;
    for (uint32_t k = 0; k < passes; ++k) {
        if (p->pass_first[k] < from || p->pass_first[k] + p->pass_prims[k] > p->prim_count)
            return fail(p->pass_target[k], "a pass's primitives are not in the page");
        from = p->pass_first[k] + p->pass_prims[k];
    }
    for (uint32_t m = 0; m < p->again_count; ++m) {
        const uint32_t k = p->again_pass[m];
        if (k >= passes || p->again[m] < p->pass_first[k] || p->again[m] > p->pass_first[k] + p->pass_prims[k])
            return fail(0x001D1F20u, "a draw-environment REF outside its pass's primitives");
        again[m] = p->again[m] - p->pass_first[k];
    }
    const uint32_t pass_first = passes ? p->pass_first[0] : 0u, pass_prims = p->counts.pass_prims;
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
    S.log.pass_target_count = npass;
    memcpy(S.log.pass_targets, targets, sizeof S.log.pass_targets);
    S.log.pass = passes;
    S.log.pass_first = pass_first;
    S.log.pass_prims = pass_prims;
    S.log.again_count = p->again_count;
    /* the marks relative to the first pass (one pass: to the pass) */
    for (uint32_t m = 0; m < p->again_count; ++m) S.log.again[m] = p->again[m] - pass_first;
    S.log.draw_envs = bank;
    S.log.draw_env_slot = slot;
    /* The digests are the tick log's only (em_diag.h): 0 without it. */
    const int digests = em_diag_digests();
    /* one pass: its digest; several: each pass's (its own marks), folded */
    S.log.pass_digest = 0;
    if (passes == 1u && digests)
        S.log.pass_digest = pass_digest(S.env + pass_first, pass_prims, again, p->again_count);
    else if (passes && digests) {
        uint32_t h = 2166136261u;
        for (uint32_t k = 0, m = 0; k < passes; ++k) {
            const uint32_t m0 = m;
            while (m < p->again_count && p->again_pass[m] == k) ++m;
            h = fnv(h, pass_digest(S.env + p->pass_first[k], p->pass_prims[k], again + m0, m - m0));
        }
        S.log.pass_digest = h;
    }
    S.log.counts = p->counts;
    S.log.decal_triangles = decal;
    S.log.flare_sprites = flare;
    S.log.weather = em_rcl_page_weather();
    S.log.overlay_reads = S.overlay_reads;
    S.log.digest = digests ? digest(p->prims, S.q, p->prim_count) : 0u;
    /* The same digest over the primitives the unit CALLs did not draw. */
    S.log.units = p->counts.units;
    {
        uint32_t h = 2166136261u, from = 0;
        for (uint32_t k = 0; k <= p->counts.units; ++k) {
            const uint32_t to = k < p->counts.units ? p->unit_first[k] : p->prim_count;
            if (digests) h = digest_more(h, p->prims + from, S.q + from, to - from);
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
        S.log.digest_without_units = digests ? h : 0u;
    }
    lamp_dump(p, start, pass, targets, npass, bank, slot);
    S.log.total_prims += p->prim_count;
    S.log.total_stale_q += p->counts.stale_q;
    S.log.total_skipped += p->counts.skipped;
    S.log.total_passes += p->counts.passes;
    if (em_shadow_live_bound() && em_shadow_live_page_drew(decal) < 0)
        return fail(0x001CE300u, "the decal the page drew is not the 0015BF90 route's");
    /* Every vertex's Q is its GIF tag's (em_chain_page_live.h). */
    if (p->counts.stale_q)
        return fail(0x001CB800u, "a page vertex has no Q");
    if (!passes) {
        if (em_gfx_gs_prims(gfx, p->prims, p->prim_count) < 0)
            return fail(0x001CB800u, "a page primitive cannot be drawn exactly");
        return 0;
    }
    /* the page up to each pass, the pass with its environment, the rest */
    uint32_t at = 0, m = 0;
    for (uint32_t k = 0; k < passes; ++k) {
        if (em_gfx_gs_prims(gfx, p->prims + at, p->pass_first[k] - at) < 0)
            return fail(0x001CB800u, "a page primitive cannot be drawn exactly");
        const uint32_t m0 = m;
        while (m < p->again_count && p->again_pass[m] == k) ++m;
        if (em_gfx_gs_page_pass(gfx, p->prims + p->pass_first[k], S.env + p->pass_first[k], p->pass_prims[k],
                                again + m0, m - m0, field_frame, field_scissor) < 0)
            return fail(p->pass_target[k], "a slot-0xFFF pass cannot be drawn exactly");
        at = p->pass_first[k] + p->pass_prims[k];
    }
    if (m != p->again_count) return fail(0x001D1F20u, "a draw-environment REF out of its pass's order");
    if (em_gfx_gs_prims(gfx, p->prims + at, p->prim_count - at) < 0)
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
