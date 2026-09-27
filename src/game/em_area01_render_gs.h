/* AREA01 render lane, module gs: the projection helpers 001CD070 /
 * 001CD180 / 001CD2B0 (census a01 delta, subsystem gs_upload; 001CD070 and
 * 001CD2B0 are called as a pair by 001E3D90, 001E4CE0, 001E7050, 001F8350
 * and 0022BBC0) and the two routines of subsystem fx_render that 00158590 /
 * 00158BD0 / 00158D30 call, 001F4A10 and 001F4CC0. Docs:
 * docs/AREA01_RENDER.md.
 *
 * Hand translation of the original functions (boot ELF SCUS-97112):
 *   001CD070(p, mask)  clip test of p through 001CD370(0); a flag in mask
 *                      returns 0xFFFFFF; else p is projected through the
 *                      matrix at 0x70003AC0 with the quadword at context
 *                      +0xA0 into 0x70003600 (12.4 x, y, z and the clamped
 *                      w term), D_00275C04 = float_to_int(clip w), and the
 *                      12.4 z word is returned (NEARMISS C; the
 *                      instructions were followed)
 *   001CD180(f12, f13) the half extents (0.5 * f12, 0.5 * f13, D_00275C04)
 *                      projected through 0x70003A40 with its z row's x and
 *                      y cleared into 0x70003610; 0 when the box around the
 *                      last 0x70003600 point fails the four fixed 12.4
 *                      bound tests (docs/AREA01_RENDER.md section 1),
 *                      else (larger half extent >> 3) + 1, the larger one
 *                      stored back at 0x70003610 (NEARMISS C; instructions)
 *   001CD2B0(f12..f15) 0x70003680 = 0; D_00275C00 = 001CD180(f12, f13);
 *                      when nonzero 0x70003680 = 1 - clamp((n - f14)
 *                      / f15), n - f14 below 0 counting as 0 (exactly 0
 *                      still divides); returns 0x70003680 (byte-matched C;
 *                      its f12/f13 reach 001CD180 unchanged: the decomp's
 *                      extern prototype in src/func_001CD2B0.c is (void),
 *                      while the NEARMISS src/func_001CD180.c already
 *                      defines it with two float parameters)
 *   001F4A10(obj, v)   001C7900(obj, v.xyz * (b + b * rand / 2^31) / 32
 *                      with b = v.w / 128, 0x3F5, 3), 001D3990(001C6120(
 *                      *D_0028A56C, 0xC)), a 0x60 end tag on channel 3,
 *                      then the key float_to_int(0x4D7FFFFF / the
 *                      projected w word) of the object *D_00275B44 +0xB0,
 *                      projected as in 001CD070, handed with
 *                      the packet start to 001CB760(D_007635C0, key, pkt)
 *                      (NEARMISS C; instructions)
 *   001F4CC0(a0, a1)   0021B9A0(2, 0, 100000), 0021B9A0(3, 0, 1000000),
 *                      001F4BF0(a0, a1), 0021B9A0(1, 0, 0) (byte-matched C)
 * and, inline, 001CD370 and 00102948. Every other callee is a worker; float
 * arguments and results are binary32 bit patterns. */
#ifndef EM_AREA01_RENDER_GS_H
#define EM_AREA01_RENDER_GS_H

#include <stdint.h>

#include "game/em_area01_render_mem.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_A01R_D_00275C00 0x00275C00u /* 001CD2B0's 001CD180 result */
#define EM_A01R_D_00275C04 0x00275C04u /* float_to_int of 001CD070's clip w */
#define EM_A01R_D_00275B44 0x00275B44u /* the object word 001F4A10 projects */
#define EM_A01R_D_0028A56C 0x0028A56Cu /* 001F4A10's 001C6120 table word */
#define EM_A01R_SPAD_3600 0x70003600u  /* 12.4 projection of 001CD070 */
#define EM_A01R_SPAD_3610 0x70003610u  /* 001CD180's extent quadword */
#define EM_A01R_SPAD_3680 0x70003680u  /* 001CD2B0's result word */
#define EM_A01R_SPAD_3A40 0x70003A40u  /* 001CD180's matrix */
#define EM_A01R_SPAD_3AC0 0x70003AC0u  /* the matrix 001CD070 / 001F4A10 project through */

typedef struct {
    void *ctx;
    /* 001281C0 float_to_int(f12): *v0 = its result. */
    int (*w_001281C0)(void *ctx, uint32_t f12, int32_t *v0);
    /* 00122BB8(): the game's random number; *v0 = its result. */
    int (*w_00122BB8)(void *ctx, int32_t *v0);
    /* 001C7900(obj, &v, a2, a3): v is the caller's stack quadword. */
    int (*w_001C7900)(void *ctx, uint32_t obj, const uint32_t v[4], int32_t a2, int32_t a3);
    /* 001C6120(table, index): *v0 = the entry address. */
    int (*w_001C6120)(void *ctx, uint32_t table, int32_t index, uint32_t *v0);
    /* 001D3990(entry). */
    int (*w_001D3990)(void *ctx, uint32_t entry);
    /* 001CB760(table, key, packet). */
    int (*w_001CB760)(void *ctx, uint32_t table, int32_t key, uint32_t packet);
    /* 0021B9A0(a0, f12, f13). */
    int (*w_0021B9A0)(void *ctx, int32_t a0, uint32_t f12, uint32_t f13);
    /* 001F4BF0(a0, a1). */
    int (*w_001F4BF0)(void *ctx, uint32_t a0, uint32_t a1);
} EmArea01RenderGsWorkers;

typedef struct {
    EmArea01RenderCore core;
    EmArea01RenderGsWorkers workers;
} EmArea01RenderGs;

/* 0 on success, -1 on a fault; results (may be NULL) are the original's v0
 * (integer) or f0 (bits). */
int em_area01_render_001CD070(EmArea01RenderGs *s, uint32_t p, uint32_t mask, uint32_t *v0);
int em_area01_render_001CD180(EmArea01RenderGs *s, uint32_t f12, uint32_t f13, uint32_t *v0);
int em_area01_render_001CD2B0(EmArea01RenderGs *s, uint32_t f12, uint32_t f13, uint32_t f14, uint32_t f15,
                              uint32_t *f0);
int em_area01_render_001F4A10(EmArea01RenderGs *s, uint32_t obj, uint32_t v);
int em_area01_render_001F4CC0(EmArea01RenderGs *s, uint32_t a0, uint32_t a1);

#ifdef __cplusplus
}
#endif

#endif /* EM_AREA01_RENDER_GS_H */
