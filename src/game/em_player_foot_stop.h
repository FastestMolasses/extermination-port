#ifndef EM_PLAYER_FOOT_STOP_H
#define EM_PLAYER_FOOT_STOP_H

#include <stdint.h>

#include "game/em_player_floor.h"

/*0017B910 entry and0017C030 mode5. The caller supplies the evaluated
 * original source pose's world-space foot nodes17/18, not a display blend. */
typedef struct EmPlayerFootStop {
    float step_x, step_z, remaining;
    unsigned tier;
    int active;
} EmPlayerFootStop;

int em_player_foot_stop_begin(EmPlayerFootStop *stop, unsigned tier,
    float clip_remaining, const float foot17[3], const float foot18[3],
    const float position[3], const float euler[3]);

/* Returns1 while mode5 continues,0 when it returns to idle,-1 invalid.
 * The caller requests clip4/default-frame0 with blend10 for tier2 only. */
int em_player_foot_stop_tick(EmPlayerFootStop *stop, unsigned animation_flags,
    float position[3], float *animation_rate);

/* ---- 0017B910 over the raw player record --------------------------------
 * The record-level translation (NEARMISS C src/func_0017B910.c; the
 * instructions were followed): 0017C030 +1F0 = 3 below tier 3 calls it.
 *   - anim_eval_skeleton(p) first.
 *   - +236 == 0: 0x70003A20 = (float)001C61D0(+40, 0017B490(p, 1, +235,
 *     +25C)); the clock +3C against the row D_0024875C[+235][+25C] picks
 *     the planted foot (node 18 below the limit, node 17 otherwise: the
 *     words +C0 / +C8 of *(D_00275B40 + 0x48 / 0x44)) and the residual
 *     0x70003A24; tier 1 halves it (at least 1.0) into +268, otherwise +268
 *     = 10.0 and 001749A0(p, 0017B490(p, 6, +235, +25C), 0, 10.0). Then
 *     dx / dz into 0x70003A20 / 24, sqrt(dx*dx + dz*dz) (the COP1
 *     accumulator product and multiply-add, then 0011E748) into 0x70003A28,
 *     0x700036A0 = identity rotated by the Euler angles +C0 (001029C0,
 *     00102C58), 0x700038A0 = (0, 0, distance, 0), 0x700038B0 = that vector
 *     through the matrix (001026A0), +260 / +264 = 0x700038B0 / 38B8 over
 *     +268, and +1F0 = 5.
 *   - +236 != 0: the row default 0017B490(p, 0, +235, 0) against +20C:
 *     equal, +1F0 = +25C = 0 unless +200 & 0x8000; otherwise
 *     001749A0(p, clip, 0, 14.0).
 * Every COP1 operation goes through em_ee_float.h; the VU0 leaves are the
 * verified translations (em_owner_services_original's 001029C0 / 00102C58,
 * em_effect_original's 001026A0). EE words (the D_0024875C row, the
 * D_00275B40 array, the node words) are read through `read`.
 * Fail-stop: every worker, scratch view and pointer is checked before the
 * first call; a worker or read that fails returns -1 at once, leaving the
 * writes made before it, in the original's order.
 * Oracle: tools/test_player_loco_workers_reference.py. */
typedef struct EmPlayerFootStopWorkers {
    void *context;
    int (*eval_skeleton)(void *context, EmPlayerLiveActor *actor);            /* anim_eval_skeleton */
    int (*select)(void *context, EmPlayerLiveActor *actor, int cmd, int idx, int tbl,
                  int16_t *clip);                                              /* 0017B490 */
    int (*clip_frames)(void *context, uint32_t bank, int clip, int32_t *frames); /* 001C61D0 */
    int (*request)(void *context, EmPlayerLiveActor *actor, int clip, int flags,
                   float blend);                                               /* 001749A0 */
    int (*sqrt)(void *context, uint32_t x, uint32_t *result);                  /* 0011E748 */
    int (*read)(void *context, uint32_t address, uint32_t *word);              /* an EE word */
} EmPlayerFootStopWorkers;

/* The scratchpad words it writes (raw bits). s3A20 and s38A0 are the one
 * storage the other player routines share (EmPlayerLandScratch); the rest
 * have no other reader on the route and may be private to the binder. */
typedef struct EmPlayerFootStopScratch {
    uint32_t *s3A20;      /* 0x70003A20 */
    uint32_t *s3A24;      /* 0x70003A24, 28, 2C (3 words) */
    uint32_t *s36A0;      /* 0x700036A0 .. DF (16 words) */
    uint32_t *s38A0;      /* 0x700038A0 .. AF (4 words) */
    uint32_t *s38B0;      /* 0x700038B0 .. BF (4 words) */
} EmPlayerFootStopScratch;

/* d275B40: the D_00275B40 word (the record's node-pointer array address). */
int em_player_foot_stop_0017B910(const EmPlayerFootStopWorkers *workers,
                                 const EmPlayerFootStopScratch *scratch, const uint32_t *d275B40,
                                 EmPlayerLiveActor *actor);

#endif
