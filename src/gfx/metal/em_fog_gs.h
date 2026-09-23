/* em_fog_gs.h — the original GS distance-fog arithmetic, shared by the Metal
 * backend (em_gfx_metal.m) and tools/test_area11_fog_reference.py.
 *
 * Original chain (all addresses in the boot ELF):
 *   - 001D8FD0 (byte-matched) reads the per-area light/fog record: rec+4/+8
 *     near/far floats, rec+0xC/10/14 fog colour INTS.
 *   - 0021B970 -> 0021B920 stores the VU fog constants at render-ctx +0xA0:
 *     (255.0, 2048.0, far * s, -s) with s = 255 / (far - near).
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
#include <string.h>

/* 0021B920: the two VU fog coefficients in binary32, same operation order
 * (one division, one product, one negation). out[0] = A, out[1] = B. */
static inline void em_fog_gs_coefficients(float near_z, float far_z,
                                          float out[2])
{
    const float scale = 255.0f / (far_z - near_z);
    out[0] = far_z * scale;
    out[1] = -scale;
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
