/* AREA01 render lane, module hud: the routines of census subsystem
 * hud_objects that the AREA01 main line reaches. Docs: docs/AREA01_RENDER.md.
 *
 * Hand translation of the original functions (boot ELF SCUS-97112; the
 * decomp C of all but 001EAF80 is a NEARMISS, so the original instructions
 * were followed; where they differ from the C, the instructions win and
 * the doc lists the differences):
 *   001E8B90(p, f12)   unless the byte D_00810700 is
 *                      0x15 or 0x10, each of the four 0xA060-byte records at
 *                      *D_00275C20 whose +0x54 word is nonzero and whose box
 *                      (x in (+0, +0 + +0x30], z in (+8, +8 + +0x34], y
 *                      below +4 + 11) holds p maps p to a 32 x 32 cell and
 *                      adds -7 * f12 to that cell, -5 * f12 to its four
 *                      edge neighbours and -3 * f12 to its four corner
 *                      neighbours of the float grid at +0x9060 (column
 *                      stride 4, row stride 0x80, indices clamped to 0..31).
 *                      The mapped x and z replace p's for the later records.
 *   001E9E60(a0, a1)   the six 0x1A-quadword GIF packets of the record
 *                      *D_00275C1C + a1 * 0xA060 (each copies the +0x60
 *                      quadwords of three consecutive 0x200-byte segments),
 *                      a 5-quadword packet of the 4 quadwords at 0x70003AC0,
 *                      a 9-quadword packet (record +0x48..+0x50, 60 + a0
 *                      +0xB4, the record's +0x10..+0x18 blended toward
 *                      D_0026E9B0..B8 by a0 +0x80 with the MULA/MADD pair,
 *                      record +0x1C * a0 +0x8C, record +0x3C/+0x38/+0x40/
 *                      +0x44, D_008105D0, a fixed register pair, context
 *                      +0xA0/+0x2220/+0x2230), then 001CB950 (the word pair
 *                      chosen by record byte +0x5C == 1), 001CB6B0 and
 *                      001CB760.
 *   001EAF00, 001EAF80, 001EB020, 001EC270 (a0 = node + 0xD0 and a1 passed
 *                      through, in the captured calls): D_00255434 table
 *                      entries that 001EA240 calls. Each fills the block
 *                      D_0081F8F0 through 001CFB50(D_0081F8F0, 0, a0, f12,
 *                      f13, 1.0, 1e-6, f16) and calls 001CFBE0(a1, kind,
 *                      table, D_0081F8F0, 0); f12 = work +0x54 (work =
 *                      *D_00275C34). 001EAF00: f13 = work +0x5C, f16 = 5,
 *                      one call (D_002557D0, 1). 001EAF80: f13 = work
 *                      +0x5C, f16 = 9, D_00255860 and D_002558F0 (kind 1).
 *                      001EB020 (three rounds) and 001EC270 (two rounds):
 *                      each round f13 = ((work +4) >> 16 & 0xFFFF) / 65535
 *                      + 1e-4 and work +4 = work +4 * 37 + 11; 001EB020 f16
 *                      = 9 with D_00255980 / D_00255A10 / D_00255AA0 (kind
 *                      1); 001EC270 f16 = 5 with D_00256790 (kind 1) and
 *                      D_00256820 (kind 0).
 * and, inline, 00102948 and 00102958. Every other callee is a worker; float
 * arguments are binary32 bit patterns. */
#ifndef EM_AREA01_RENDER_HUD_H
#define EM_AREA01_RENDER_HUD_H

#include <stdint.h>

#include "game/em_area01_render_mem.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_A01R_D_00810700 0x00810700u /* area byte */
#define EM_A01R_D_00275C20 0x00275C20u /* 001E8B90's record array */
#define EM_A01R_D_00275C1C 0x00275C1Cu /* 001E9E60's record array */
#define EM_A01R_D_00275C34 0x00275C34u /* the effect work block address */
#define EM_A01R_D_00275674 0x00275674u
#define EM_A01R_D_0081F8F0 0x0081F8F0u /* transform block (001CFB50 dst) */

typedef struct {
    void *ctx;
    /* 001281C0 float_to_int(f12): *v0 = its result. */
    int (*w_001281C0)(void *ctx, uint32_t f12, int32_t *v0);
    /* 001CB5F0(table, a1, qwc): *v0 = the packet address. */
    int (*w_001CB5F0)(void *ctx, uint32_t table, int32_t a1, int32_t qwc, uint32_t *v0);
    /* 001CB950(table, a1, a2): a2 is the full 64-bit register. */
    int (*w_001CB950)(void *ctx, uint32_t table, int32_t a1, uint64_t a2);
    /* 001CB6B0(table, a1, a2, a3). */
    int (*w_001CB6B0)(void *ctx, uint32_t table, int32_t a1, int32_t a2, uint32_t a3);
    /* 001CB760(table, a1, a2). */
    int (*w_001CB760)(void *ctx, uint32_t table, int32_t a1, uint32_t a2);
    /* 001CFB50(dst, a1, src, f12, f13, f14, f15, f16). */
    int (*w_001CFB50)(void *ctx, uint32_t dst, int32_t a1, uint32_t src, uint32_t f12, uint32_t f13,
                      uint32_t f14, uint32_t f15, uint32_t f16);
    /* 001CFBE0(a0, kind, table, xf, t0). */
    int (*w_001CFBE0)(void *ctx, uint32_t a0, int32_t kind, uint32_t table, uint32_t xf, int32_t t0);
} EmArea01RenderHudWorkers;

typedef struct {
    EmArea01RenderCore core;
    EmArea01RenderHudWorkers workers;
} EmArea01RenderHud;

/* 0 on success, -1 on a fault. */
int em_area01_render_001E8B90(EmArea01RenderHud *s, uint32_t p, uint32_t f12);
int em_area01_render_001E9E60(EmArea01RenderHud *s, uint32_t a0, int32_t a1);
int em_area01_render_001EAF00(EmArea01RenderHud *s, uint32_t a0, uint32_t a1);
int em_area01_render_001EAF80(EmArea01RenderHud *s, uint32_t a0, uint32_t a1);
int em_area01_render_001EB020(EmArea01RenderHud *s, uint32_t a0, uint32_t a1);
int em_area01_render_001EC270(EmArea01RenderHud *s, uint32_t a0, uint32_t a1);
int em_area01_render_001EB7F0(EmArea01RenderHud *s, uint32_t a0, uint32_t a1);

#ifdef __cplusplus
}
#endif

#endif /* EM_AREA01_RENDER_HUD_H */
