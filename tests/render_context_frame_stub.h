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
#include "game/em_packet_chain_original.h"
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

/* The fog programmer and the context reads the snow and the AREA11 effect
 * make on the one render context: the real translation em_packet_chain_0021B9A0
 * over the fixture context (its presets from fixture_fog_area). */
static EmPacketChain s_fixture_pc;
static EmPacketChainRegion s_fixture_regions[1];

static void fixture_chain(void)
{
    s_fixture_regions[0] = (EmPacketChainRegion){EM_RCL_CONTEXT, sizeof s_fixture_ctx, s_fixture_ctx};
    em_packet_chain_init(&s_fixture_pc, s_fixture_regions, 1, EM_RCL_CONTEXT, 0);
}

/* 001D8FD0's 0021B970(near, far) for the fixture's area (AREA11: -209 /
 * 304), latching the mode-0 / mode-1 pairs as the area load leaves them. */
static void fixture_fog_area(float near_z, float far_z)
{
    uint32_t n, f;
    memcpy(&n, &near_z, 4);
    memcpy(&f, &far_z, 4);
    fixture_chain();
    assert(em_packet_chain_0021B970(&s_fixture_pc, n, f) == 0);
    memcpy(s_fixture_ctx + 0xE0, s_fixture_ctx + 0xA0, 0x20);   /* 0021B8E0 */
}

int em_rcl_0021B9A0(int32_t mode, uint32_t scale, uint32_t bias)
{
    fixture_chain();
    return em_packet_chain_0021B9A0(&s_fixture_pc, mode, scale, bias);
}

const uint8_t *em_rcl_bytes(uint32_t address, uint32_t size)
{
    if (address < EM_RCL_CONTEXT || size > sizeof s_fixture_ctx ||
        address - EM_RCL_CONTEXT > sizeof s_fixture_ctx - size)
        return NULL;
    return s_fixture_ctx + (address - EM_RCL_CONTEXT);
}

#endif
