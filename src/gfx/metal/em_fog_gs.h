/* em_fog_gs.h — the original GS distance-fog arithmetic, shared by the Metal
 * backend (em_gfx_metal.m) and tools/test_area11_fog_reference.py.
 *
 * Original chain (all addresses in the boot ELF):
 *   - 001D8FD0 (byte-matched) reads the per-area light/fog record: rec+4/+8
 *     near/far floats, rec+0xC/10/14 fog colour INTS.
 *   - 0021B970 -> 0021B920 stores the VU fog constants at render-ctx +0xA0:
 *     (255.0, 2048.0, far * s, -s) with s = 255 / (far - near). The one
 *     translation of 0021B920 is em_packet_chain_0021B920
 *     (em_packet_chain_original.c, docs/PACKET_CHAIN.md): SUB.S and MUL.S
 *     chop, DIV.S rounds to nearest. em_fog_gs_coefficients below is a thin
 *     call of it; it used to recompute the pair in host binary32, which
 *     differs from the EE result by up to 3 ulp for most (near, far) pairs
 *     (PACKET_CHAIN.md 6.4).
 *   - 0021BA80 packs the ints into GS FOGCOL (r | g << 8 | b << 16) at
 *     render-ctx +0xB0; 001D1C50 puts it in the frame's register list.
 *   - The VU1 skinning kernel at 0023C780 evaluates, per vertex,
 *     F = min(A + B * clip_w, 255), then max(F, 0), then ftoi4 into the
 *     XYZF2 F field (bits 4..11 of the fixed-point value = floor(F)).
 *   - The GS then blends each pixel toward FOGCOL with F interpolated
 *     linearly across the primitive (F = 255 keeps the colour, F = 0 is
 *     pure FOGCOL).
 * AREA11 (key 0x0B00): near -209, far 304, FOGCOL 48,48,48; RAM captures
 * hold exactly these constants and the opening GS state holds FOGCOL
 * 0x303030 (checked by tools/test_area11_fog_reference.py).
 */
#ifndef EM_FOG_GS_H
#define EM_FOG_GS_H

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "game/em_packet_chain_original.h"

/* The render context address the route captures hold in D_00275670. The
 * block below is private to one call; the address only names it for the
 * translation's region check. */
#define EM_FOG_GS_CONTEXT 0x00811CC0u

/* 0021B920(near, far) through its translation, on a private context block;
 * out[0] = A (+0xA8), out[1] = B (+0xAC), bit for bit. Returns 0, or -1 if
 * the translation faulted (it cannot: the block it addresses is mapped),
 * leaving out[] unwritten. */
static inline int em_fog_gs_coefficients(float near_z, float far_z, float out[2])
{
    uint8_t block[EM_PACKET_CHAIN_CONTEXT_SPAN];
    memset(block, 0, sizeof block);
    const EmPacketChainRegion region = { EM_FOG_GS_CONTEXT, (uint32_t)sizeof block, block };
    EmPacketChain chain;
    uint32_t near_bits, far_bits;
    memcpy(&near_bits, &near_z, sizeof near_bits);
    memcpy(&far_bits, &far_z, sizeof far_bits);
    em_packet_chain_init(&chain, &region, 1u, EM_FOG_GS_CONTEXT, 0u);
    if (em_packet_chain_0021B920(&chain, near_bits, far_bits) != 0) return -1;
    memcpy(&out[0], block + EM_PACKET_CHAIN_FOG + 8u, sizeof out[0]);
    memcpy(&out[1], block + EM_PACKET_CHAIN_FOG + 12u, sizeof out[1]);
    return 0;
}

/* GS FOGCOL channel (0..255, framebuffer units) as the native [0,1] colour
 * the Metal target stores (1.0 == GS 255). */
static inline float em_fog_gs_color_unit(float fogcol_channel)
{
    return fogcol_channel / 255.0f;
}

/* The F field the 0023C780 kernel emits for one vertex, with the VU's
 * binary32 truncation of the product and the sum. The Metal vertex shader
 * mirrors this formula with ordinary rounding, which can differ by one F
 * step exactly at an integer boundary. */
static inline float em_fog_gs_vu_trunc(double value)
{
    float result = (float)value;
    if (fabs((double)result) > fabs(value)) {
        unsigned int bits;
        memcpy(&bits, &result, sizeof bits);
        bits -= 1u;
        memcpy(&result, &bits, sizeof result);
    }
    return result;
}

static inline float em_fog_gs_factor(const float coef[2], float clip_w)
{
    float f = em_fog_gs_vu_trunc((double)coef[0] +
                                 (double)em_fog_gs_vu_trunc(
                                     (double)coef[1] * clip_w));
    f = fminf(f, 255.0f);
    f = fmaxf(f, 0.0f);
    return floorf(f);
}

#endif /* EM_FOG_GS_H */
