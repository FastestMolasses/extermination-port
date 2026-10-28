/* em_player_hull_shove.h - 00176180, the player's class-2 hull shove after
 * a mask-bit-0 wall hit (00176390's call; docs/PLAYER_FLOOR.md "P16").
 *
 * Translated from the byte-matched decomp C (src/func_00176180.c). The
 * actor is the radial probes' EmPlayerProbeActor (its +0xB0 position, +4,
 * +5 and +0x1F0 bytes); `delta` is D_00281B50, which 00176390 copies from
 * the first hit's 0x700031C0 delta before its 0019AD00 probe. Unless the
 * actor's +0x1F0 byte is 0x3C:
 *   - 0011E748(00102738(delta, delta)) is called and its result unused;
 *   - n = 00102760(delta) (the xyz normal); p = position + delta (00102948,
 *     001028B8: the four-lane VU0 add);
 *   - the probe end v = (p.x + 4.5 n.x, target.y, p.z + 4.5 n.z) (FPU
 *     products and sums);
 *   - 0019AFE0(actor, target, v, 6): on a hit p.x / p.z += its
 *     0x700031C0 / 0x700031C8 delta;
 *   - position.x / .z = p.x / p.z (y unchanged);
 *   - 0x700031F0 = 1 when +4 is 1 and +5 is 0x1D.
 * The quadwords' w lanes (+0xBC, 0x700031CC) reach no output: the dot and
 * the normal read xyz, and p.w is never read.
 *
 * Arithmetic: em_ee_float.h (FPU), em_sdk_vu0.h (00102738), the VU0 model
 * (001028B8) and em_effect_original_00102760.
 *
 * Oracle: tools/test_player_hull_shove_reference.py executes the original
 * 00176180 with its SDK callees over scripted 0019AFE0 results. */
#ifndef EM_PLAYER_HULL_SHOVE_H
#define EM_PLAYER_HULL_SHOVE_H

#include <stdint.h>

#include "game/em_player_floor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EmPlayerHullShoveWorkers {
    void *context;
    /* 0019AFE0(actor, from, to, 6) without the 0x80000000 bit (the probe
     * set's sweep). */
    int (*sweep)(void *context, const float from[3], const float to[3], unsigned mask,
                 EmPlayerProbeHit *hit);
    /* 0011E748: its result is unused; a fault is the binder's to report. */
    float (*sqrt)(void *context, float x);
    /* The scratchpad word 0x700031F0 (the carry flag's one storage); NULL
     * faults when the +4 / +5 test sets it. */
    int32_t *carry31F0;
} EmPlayerHullShoveWorkers;

/* 00176180(actor, kind, target) with D_00281B50 = delta. 0, or -1 on a
 * worker fault (a sweep fault leaves the position unchanged). */
int em_player_00176180(EmPlayerProbeActor *actor, const float delta[3], const float target[3],
                       const EmPlayerHullShoveWorkers *w);

#ifdef __cplusplus
}
#endif

#endif /* EM_PLAYER_HULL_SHOVE_H */
