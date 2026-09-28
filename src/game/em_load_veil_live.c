/* em_load_veil_live.c - the load veil's frames drawn at step V
 * (em_load_veil_live.h, docs/LOAD_VEIL_PARTICLES.md section 3). */
#include "game/em_load_veil_live.h"

#include <stdio.h>
#include <string.h>

#include "game/em_frame.h"
#include "game/em_load_veil_particles.h"
#include "game/em_render_context_live.h"

#define GS_BLOCKS 0x00814220u   /* the value of D_00275674 */

static struct {
    EmChainPage page;
    EmGfxGsPrim prims[EM_LOAD_VEIL_LIVE_PRIMS];
    EmGfxGsEnv env[EM_LOAD_VEIL_LIVE_PRIMS];
    uint32_t fault;
    EmLoadVeilLiveLog log;
    uint8_t run[EM_LOAD_VEIL_PARTICLES_0021B1B0_BYTES];
    uint32_t run_size;
    /* the walk's transfers inside the veil's run */
    uint32_t start, end, covered;
} S;

static int fail(uint32_t address, const char *what)
{
    if (!S.fault) {
        S.fault = address;
        fprintf(stderr, "load veil: %s (at %08X)\n", what, (unsigned)address);
    }
    return -1;
}

uint32_t em_load_veil_live_fault(void) { return S.fault; }

void em_load_veil_live_log(EmLoadVeilLiveLog *out)
{
    if (out) *out = S.log;
}

const uint8_t *em_load_veil_live_run(uint32_t *size)
{
    if (size) *size = S.run_size;
    return S.run_size ? S.run : NULL;
}

/* The list's memory is the render context's storage (the arena, the
 * context, the GS blocks, the .data it exports). Reads inside the veil's
 * run are counted. */
static const uint8_t *reader(void *ctx, uint32_t address, uint32_t size)
{
    (void)ctx;
    const uint8_t *p = em_rcl_bytes(address, size);
    if (p && address >= S.start && address < S.end)
        S.covered += size <= S.end - address ? size : S.end - address;
    return p;
}

static uint32_t fnv(uint32_t h, uint32_t w)
{
    for (unsigned k = 0; k < 4; ++k) {
        h ^= (w >> (8 * k)) & 0xFFu;
        h *= 16777619u;
    }
    return h;
}

static uint32_t fnv64(uint32_t h, uint64_t v) { return fnv(fnv(h, (uint32_t)v), (uint32_t)(v >> 32)); }

static uint32_t digest(uint32_t n)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; ++i) {
        const EmGfxGsPrim *p = &S.prims[i];
        const EmGfxGsEnv *e = &S.env[i];
        h = fnv(h, p->prim);
        h = fnv(h, p->set);
        h = fnv64(h, p->tex0); h = fnv64(h, p->clamp); h = fnv64(h, p->tex1);
        h = fnv64(h, p->alpha); h = fnv64(h, p->test); h = fnv64(h, p->colclamp);
        h = fnv(h, e->set);
        h = fnv64(h, e->frame); h = fnv64(h, e->zbuf); h = fnv64(h, e->xyoffset); h = fnv64(h, e->scissor);
        h = fnv(h, p->count);
        for (uint32_t k = 0; k < p->count; ++k) {
            const EmGfxGsVertex *v = &p->v[k];
            h = fnv(h, (uint32_t)v->x | (uint32_t)v->y << 16);
            h = fnv(h, v->z);
            h = fnv(h, (uint32_t)v->rgba[0] | (uint32_t)v->rgba[1] << 8 | (uint32_t)v->rgba[2] << 16 |
                           (uint32_t)v->rgba[3] << 24);
            h = fnv(h, v->q); h = fnv(h, v->s); h = fnv(h, v->t);
            h = fnv(h, (uint32_t)v->u | (uint32_t)v->v << 16);
        }
    }
    return h;
}

static uint64_t rd64(const uint8_t *b)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = v << 8 | b[i];
    return v;
}

int em_load_veil_live_draw(EmGfx *gfx)
{
    if (S.fault) return -1;
    uint32_t start, end, slot, chain, kicks;
    if (em_rcl_veil_span(&start, &end, &slot) < 0) return 0;   /* no veil this frame */
    if (em_rcl_kick(&chain, &kicks) < 0) return fail(0x001D2300u, "no kicked list");
    if (end <= start || end - start > sizeof S.run || ((end - start) & 15u))
        return fail(0x0021B1B0u, "the veil's channel-0 run is malformed");
    /* The slot's draw environment (bank A): the buffer the list draws into. */
    const uint8_t *env = em_rcl_bytes(GS_BLOCKS + 0x20u + 0x190u * slot, 0x60u);
    if (slot > 1u || !env) return fail(0x001D2300u, "no draw environment for the buffer index");
    const uint64_t frame = rd64(env + 0x20), scissor = rd64(env + 0x50);

    memset(&S.page.counts, 0, sizeof S.page.counts);
    S.page.read = reader;
    S.page.read_ctx = NULL;
    S.page.skip_calls = NULL;
    S.page.skip_count = 0;
    S.page.prims = S.prims;
    S.page.prim_q = NULL;
    S.page.prim_env = S.env;
    S.page.prim_capacity = EM_LOAD_VEIL_LIVE_PRIMS;
    S.start = start;
    S.end = end;
    S.covered = 0;
    if (em_chain_page_run_list(&S.page, chain) < 0) {
        fprintf(stderr, "load veil: the frame list faults: %s at %08X (%08X)\n",
                em_chain_page_fault_name(S.page.fault), (unsigned)S.page.fault_address,
                (unsigned)S.page.fault_detail);
        return fail(0x001D2300u, "the frame list does not walk");
    }
    if (S.covered != end - start) return fail(0x0021B1B0u, "the kicked list did not send the veil's run");
    if (!S.page.prim_count || S.env[0].frame != frame)
        return fail(0x001D2300u, "the list did not draw into the slot's frame buffer");

    const uint8_t *run = em_rcl_bytes(start, end - start);
    if (!run) return fail(0x0021B1B0u, "the veil's run is not in the arena");
    memcpy(S.run, run, end - start);
    S.run_size = end - start;
    uint32_t max_rgb = 0;
    for (uint32_t i = 0; i < S.page.prim_count; ++i) {
        const EmGfxGsPrim *p = &S.prims[i];
        if ((p->prim & 7u) != 1u && (p->prim & 7u) != 2u) continue;
        for (uint32_t k = 0; k < p->count; ++k)
            for (unsigned c = 0; c < 3u; ++c)
                if (p->v[k].rgba[c] > max_rgb) max_rgb = p->v[k].rgba[c];
    }
    S.log.frame = em_frame_counter();
    S.log.draws++;
    S.log.start = start;
    S.log.end = end;
    S.log.slot = slot;
    S.log.chain = chain;
    S.log.display_frame = frame;
    S.log.counts = S.page.counts;
    S.log.digest = digest(S.page.prim_count);
    S.log.max_line_rgb = max_rgb;
    if (em_gfx_gs_frame(gfx, S.prims, S.env, S.page.prim_count, frame, scissor) < 0)
        return fail(0x001D2300u, "the GS frame stage refused the veil's list");
    return 0;
}
