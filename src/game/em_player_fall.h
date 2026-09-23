/* em_player_fall.h - the player's fall and landing states (docs/PLAYER_FALL.md).
 *
 * Translations of the original routines, not models of them. Each works on
 * the raw 0x320-byte player record (EmPlayerLiveActor) by its original
 * offsets:
 *   00162DB0  +4 = 1, +5 = 5 fall            (0015B130 state[5])
 *   001639E0  +4 = 1, +5 = 7 drop            (0015B130 state[7])
 *   00163B40  +4 = 1, +5 = 8 landing         (0015B130 state[8]), and its
 *             sub-state routines by +6: 00164220 (2), 00163E90 (3),
 *             00163D50 (4), 00163C10 (5), 001643B0 (0xA)
 *   0017C580  land: +5 = 8 and the landing reaction in +6
 *   00224290  the landing hit check (sub-state +7)
 *   0021D250  surface 0x5D: +4 = 2, +5 = 0x16
 *   0021D2E0  the +6 = 0x63 / 1 / 2 "teleport" wait (fade after +28 frames)
 *   00179880  the drop accumulator (a leaf every routine above shares)
 *
 * Every other original callee is a worker (EmPlayerLandWorkers). A worker
 * that is missing is a fault: each entry point checks the whole worker set
 * and returns -1 before its first write. A worker that returns a negative
 * value is a fault too: the routine stops at once and returns -1, leaving
 * the writes made before the call, as the original order leaves them.
 *
 * Arithmetic: every COP1 operation goes through em_ee_float.h (the measured
 * EE model, docs/EE_FLOAT_MODEL.md), on raw bit patterns.
 *
 * Oracle: tools/test_player_fall_reference.py executes the original
 * instructions of every routine above from the user's pinned ELF (callees
 * hooked and recorded) and compares all 0x320 actor bytes, the scratchpad
 * words, the return values and the worker call sequence; EM_TEST_WORLD=1
 * replays the route beats that fall and land (10, 11, 12, 14) over the
 * captured RAM with these routines in place of the originals. */
#ifndef EM_PLAYER_FALL_H
#define EM_PLAYER_FALL_H

#include <stdint.h>

#include "game/em_player_floor.h"

/* The scratchpad words these routines write and read back. The binder owns
 * one instance and must hand the SAME instance to every worker whose
 * original writes these words (00174AC0, 001755B0 and 0017D080 store
 * 0x70003A20 in some paths): 0017C580 stores its drop at 0x70003A20, calls
 * 00174AC0, then reloads the word (0017C710). */
typedef struct EmPlayerLandScratch {
    uint32_t s38A0[4];   /* 0x700038A0: probe point / effect point (raw bits) */
    uint32_t s3A20;      /* 0x70003A20 (raw bits) */
} EmPlayerLandScratch;

#define EM_PLAYER_LAND_S1_ZERO   0
#define EM_PLAYER_LAND_S1_RECORD 1
#define EM_PLAYER_LAND_S1_CALLER 2

typedef struct EmPlayerLandWorkers {
    void *context;
    EmPlayerLandScratch *scratch;
    /* 001749A0(p, clip, force, blend). The return value is not read. */
    int (*request)(void *context, EmPlayerLiveActor *actor, int clip, int force, float blend);
    /* anim_clip_arbiter(p, clip, blend, frame). */
    int (*arbiter)(void *context, EmPlayerLiveActor *actor, int clip, float blend, float frame);
    /* 001C61D0(word +40, clip): the clip's frame count. */
    int (*clip_frames)(void *context, uint32_t bank, int clip, int32_t *frames);
    /* 001FBD50(p, id, 0, 300.0). The return value is not read. */
    int (*sound)(void *context, EmPlayerLiveActor *actor, int id);
    /* 001B61C0(a, b, c, d): the pad vibration request. */
    int (*rumble)(void *context, int a, int b, int c, int d);
    /* 001EFD90(id, point, p+B0): point is the 0x700038A0 vector (x, y, z,
     * 1.0); at is the four words at p+B0 (001EFD90 reads +B0..+BC). */
    int (*effect)(void *context, uint32_t id, const uint32_t point[4], const uint32_t at[4]);
    /* 001AEDE0(a, b): the screen fade request. */
    int (*fade)(void *context, int a, int b);
    /* anim_eval_skeleton(p). */
    int (*skeleton)(void *context, EmPlayerLiveActor *actor);
    /* The node *(D_00275B40 + 4): its world x (+C0) and z (+C8), read after
     * anim_eval_skeleton (raw bits). */
    int (*hip)(void *context, uint32_t *x, uint32_t *z);
    /* 00174AC0(p, arg): the turn toward the stick; *result is its return. */
    int (*heading)(void *context, EmPlayerLiveActor *actor, int arg, int *result);
    /* 001755B0(p), 0017D080(p), 0017F320(p), 0021C190(p): tests whose
     * return value the routines branch on. */
    int (*test_001755B0)(void *context, EmPlayerLiveActor *actor, int *result);
    int (*test_0017D080)(void *context, EmPlayerLiveActor *actor, int *result);
    int (*test_0017F320)(void *context, EmPlayerLiveActor *actor, int *result);
    int (*test_0021C190)(void *context, EmPlayerLiveActor *actor, int *result);
    /* 00188550(p): the clip 00162DB0 sub-state 4 requests. */
    int (*pose_clip)(void *context, EmPlayerLiveActor *actor, int *clip);
    /* 001B1470(x) (angle wrap) and 001B12B0(target, current, rate). */
    int (*wrap)(void *context, uint32_t x, uint32_t *out);
    int (*approach)(void *context, uint32_t target, uint32_t current, uint32_t rate,
                    uint32_t *out);
    /* build_trs_matrix(out, p+B0, p+C0, p+60): out is written to p+D0. */
    int (*trs)(void *context, uint32_t out[16], const uint32_t position[3],
               const uint32_t rotation[3], const uint32_t scale[3]);
    /* 001026A0(out, M, v): out = v x M (M = the 16 words at p+D0). */
    int (*apply)(void *context, const uint32_t matrix[16], const uint32_t v[4], uint32_t out[4]);
    /* 00179450(p, point): the column-table floor query; writes +258;
     * *result is 0, 1 or 2. */
    int (*floor_query)(void *context, EmPlayerLiveActor *actor, const uint32_t point[3],
                       int *result);
    /* 00182870(p, tier): the landing sound. */
    int (*land_sound)(void *context, EmPlayerLiveActor *actor, int tier);
    /* 00178B90(p, arg): the translation. */
    int (*translate)(void *context, EmPlayerLiveActor *actor, int arg);
    /* 001764E0(p): the radial wall probes. 001764E0 also tests ($s1 & 4) of
     * its caller without setting $s1 (em_player_floor.h EmPlayerProbeScene):
     * `s1` says what the caller's $s1 holds at the call:
     * EM_PLAYER_LAND_S1_ZERO (00162DB0 sets $s1 = 0 on entry, 00162DD8),
     * EM_PLAYER_LAND_S1_RECORD (001639E0 keeps the record address in $s1,
     * 001639FC) or EM_PLAYER_LAND_S1_CALLER (00163B40's sub-state routines
     * leave $s1 as 0015B130 left it). */
    int (*probes)(void *context, EmPlayerLiveActor *actor, int s1);
    /* 00175900(p, search): the floor service; *result is its return (+A). */
    int (*floor)(void *context, EmPlayerLiveActor *actor, int search, int *result);
    /* 001796C0(p): the fall check. */
    int (*fall_check)(void *context, EmPlayerLiveActor *actor);
    /* 0017C860(p, drop) with drop = the float at +2EC: nonzero when it took
     * the actor into the ledge state (+5 = 4). */
    int (*ledge)(void *context, EmPlayerLiveActor *actor, uint32_t drop, int *result);
    /* 0017C440(p, arg) and 0017C540(p): the walk re-entry and hand-off. */
    int (*reentry)(void *context, EmPlayerLiveActor *actor, int arg);
    int (*handoff)(void *context, EmPlayerLiveActor *actor);
    /* 0021C120(p), 0021C350(p), 0021C270(p): the damage reactions. */
    int (*react_0021C120)(void *context, EmPlayerLiveActor *actor);
    int (*react_0021C350)(void *context, EmPlayerLiveActor *actor);
    int (*react_0021C270)(void *context, EmPlayerLiveActor *actor);
    /* 00128350(+220): *result is its int return. 001000E0(a, b): *result is
     * its return. */
    int (*convert_00128350)(void *context, uint32_t value, int *result);
    int (*test_001000E0)(void *context, int a, int b, int *result);
    /* D_008106F1, read by 0017C580 after 00174AC0. */
    int (*progress_8106F1)(void *context, uint8_t *value);
} EmPlayerLandWorkers;

/* 1 when every worker and the scratch are bound. */
int em_player_fall_workers_bound(const EmPlayerLandWorkers *workers);

/* 00179880(p, p+2EC): +2EC += -0.04, clamped at -4.0; +B4 += +2EC;
 * +25F = 2. */
void em_player_fall_drop(EmPlayerLiveActor *actor);

/* 0017C580(p). Returns 0, or -1 on a fault. */
int em_player_fall_land(const EmPlayerLandWorkers *workers, EmPlayerLiveActor *actor);
/* 00224290(p). *result is its return value (0 or 1). */
int em_player_fall_land_check(const EmPlayerLandWorkers *workers, EmPlayerLiveActor *actor,
                              int *result);
/* 0021D250(p, arg). */
int em_player_fall_surface5d(const EmPlayerLandWorkers *workers, EmPlayerLiveActor *actor,
                             int arg);
/* 0021D2E0(p, frames, hold): frames is the halfword stored at +28. */
int em_player_fall_teleport(const EmPlayerLandWorkers *workers, EmPlayerLiveActor *actor,
                            int frames, int hold);
/* The landing sub-state routines (00163B40 dispatches them by +6). */
int em_player_fall_00163C10(const EmPlayerLandWorkers *workers, EmPlayerLiveActor *actor);
int em_player_fall_00163D50(const EmPlayerLandWorkers *workers, EmPlayerLiveActor *actor);
int em_player_fall_00163E90(const EmPlayerLandWorkers *workers, EmPlayerLiveActor *actor);
int em_player_fall_00164220(const EmPlayerLandWorkers *workers, EmPlayerLiveActor *actor);
int em_player_fall_001643B0(const EmPlayerLandWorkers *workers, EmPlayerLiveActor *actor);

/* The state callbacks (EmPlayerStateCallback): context is a
 * const EmPlayerLandWorkers *. Bind as EmPlayerStageWorkers.state[5],
 * state[7] and state[8]. Each returns 0, or -1 on a fault. */
int em_player_fall_state5(void *workers, EmPlayerLiveActor *actor);   /* 00162DB0 */
int em_player_fall_state7(void *workers, EmPlayerLiveActor *actor);   /* 001639E0 */
int em_player_fall_state8(void *workers, EmPlayerLiveActor *actor);   /* 00163B40 */

#endif
