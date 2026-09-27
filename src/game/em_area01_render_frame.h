/* AREA01 render lane, module frame: 0015B610, the one census subsystem
 * frame_update routine of the AREA01 main line that the port does not
 * translate yet (its caller 0015BA50 reaches it for its a0 byte value 5;
 * tools/test_player_floor_reference.py lists it as MAJORS[5]). Docs:
 * docs/AREA01_RENDER.md.
 *
 * Hand translation of the original function (boot ELF SCUS-97112; the decomp
 * holds it as asm words, so the original instructions were followed):
 *   0015B610(a)  b = scratchpad byte 0x70003B8D. When b is neither 0 nor 4
 *                and 00182B30(a, b) returns 0:
 *                  a +5 == 3: a +4 = 4, +5 = 0xC, +6 = 0, +0x1F0 = 0x17;
 *                  a +5 == 1: 00174A50(a, 8.0), then a +4 = 4, +5 = 0,
 *                             +6 = 0, +0x1F0 = 0x41;
 *                  otherwise: a +4 = 4, +5 = 0, +6 = 0, +0x1F0 = 0x41;
 *                then 00182D70(a).
 *                Otherwise a +5 selects 0 -> 00183240(a), 1 -> 00183250(a),
 *                2 -> 001833F0(a), 3 -> 00183440(a), 4 -> 001834E0(a); any
 *                other value calls nothing.
 * Every callee is a worker. */
#ifndef EM_AREA01_RENDER_FRAME_H
#define EM_AREA01_RENDER_FRAME_H

#include <stdint.h>

#include "game/em_area01_render_mem.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_A01R_SPAD_3B8D 0x70003B8Du

typedef struct {
    void *ctx;
    /* 00182B30(a, b): *v0 = its result (b is the scratchpad byte). */
    int (*w_00182B30)(void *ctx, uint32_t a, uint32_t b, int32_t *v0);
    /* 00174A50(a, f12): f12 = 8.0 bits. */
    int (*w_00174A50)(void *ctx, uint32_t a, uint32_t f12);
    /* 00182D70(a). */
    int (*w_00182D70)(void *ctx, uint32_t a);
    /* The five +5 routines (a). */
    int (*w_00183240)(void *ctx, uint32_t a);
    int (*w_00183250)(void *ctx, uint32_t a);
    int (*w_001833F0)(void *ctx, uint32_t a);
    int (*w_00183440)(void *ctx, uint32_t a);
    int (*w_001834E0)(void *ctx, uint32_t a);
} EmArea01RenderFrameWorkers;

typedef struct {
    EmArea01RenderCore core;
    EmArea01RenderFrameWorkers workers;
} EmArea01RenderFrame;

/* 0 on success, -1 on a fault. */
int em_area01_render_0015B610(EmArea01RenderFrame *s, uint32_t a);

#ifdef __cplusplus
}
#endif

#endif /* EM_AREA01_RENDER_FRAME_H */
