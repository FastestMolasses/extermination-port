/* Test support for the fixtures whose module draws with the render
 * context's frame matrices (em_rcl_frame_matrices): P, the 001CD370(0)
 * clip projection and K, built by the real translation em_frh_001D2960
 * (em_frame_render_heads.c) over a fixture context from the fixture's own
 * view (native convention) and zoom. Include in exactly one file per
 * fixture and call fixture_frame_head before each draw. The cull-plane
 * square roots use the host sqrtf: the fixtures' draws do not read the
 * planes. */
#ifndef RENDER_CONTEXT_FRAME_STUB_H
#define RENDER_CONTEXT_FRAME_STUB_H

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "game/em_frame_render_heads.h"
#include "game/em_render_context_live.h"

static uint8_t s_fixture_ctx[0x2470];
static uint8_t s_fixture_view[0x40];
static uint32_t s_fixture_d275670 = EM_RCL_CONTEXT;
static int s_fixture_head;

static int fixture_sqrt(void *ctx, uint32_t x, uint32_t *out)
{
    (void)ctx;
    float f, r;
    memcpy(&f, &x, 4);
    r = sqrtf(f);
    memcpy(out, &r, 4);
    return 0;
}

/* The frame head's projection over `view` (native: rows 1 and 2 negated
 * from the original D_00810610 layout) at `zoom`. */
static void fixture_frame_head(const float view[16], float zoom)
{
    uint32_t w[16];
    memcpy(w, view, sizeof w);
    for (int k = 0; k < 16; ++k)
        if ((k & 3) == 1 || (k & 3) == 2) w[k] ^= UINT32_C(0x80000000);
    memcpy(s_fixture_view, w, sizeof w);
    memcpy(s_fixture_ctx + 0x2468, &zoom, 4);
    const EmFrhView views[3] = {
        {EM_FRH_D_00275670, 4, (uint8_t *)&s_fixture_d275670, 0},
        {EM_FRH_D_00810610, 0x40, s_fixture_view, 0},
        {EM_RCL_CONTEXT, sizeof s_fixture_ctx, s_fixture_ctx, 1},
    };
    EmFrh h;
    memset(&h, 0, sizeof h);
    h.views = views;
    h.view_count = 3;
    h.workers.w_0011E748 = fixture_sqrt;
    assert(em_frh_001D2960(&h, EM_FRH_D_00810610) == 0);
    s_fixture_head = 1;
}

int em_rcl_frame_matrices(uint32_t p[16], uint32_t clip[16], uint32_t k[16])
{
    if (!s_fixture_head) return -1;
    memcpy(p, s_fixture_ctx + 0x2340, 0x40);
    memcpy(clip, s_fixture_ctx + 0x2240, 0x40);
    memcpy(k, s_fixture_ctx + 0x23C0, 0x40);
    return 0;
}

#endif
