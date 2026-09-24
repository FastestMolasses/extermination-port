/* em_player_running_jump.h - the player's running jump (+5 = 6), the Use
 * chain's last two tests and the +5 = 0x24 state (docs/PLAYER_RUNNING_JUMP.md).
 *
 * Translations of the original routines, not models of them:
 *   0015EC50  the running-jump probe of the Use chain 00160220 (gap ahead,
 *             a floor more than 4.01 below the far side): +5 = 6, +6 = 0,
 *             +1F0 = 0xC and 1, or 0. NEARMISS in the decomp: read from
 *             the instructions.
 *   0015FDF0  the aim solver of the Use chain (turns +C4 toward the target
 *             001AA4E0 finds): 1 when there is a target, else 0. NEARMISS.
 *   001AA4E0  the nearest qualifying D_00275B8C object (byte-matched C).
 *   001634A0  the +5 = 6 state callback (0015B130's state[6]). NEARMISS.
 *   001747F0  the +5 = 0x24 state callback (0015B130's state[0x24]),
 *             entered by 00160220 when 0015FDF0 returns 1 (byte-matched C).
 *   00179880  the drop accumulator, here on +2E4 (001634A0 case 3 passes
 *             p + 0x2E4): not translated here; it runs from its one
 *             translation, em_player_fall_00179880 (em_player_fall.h).
 * The SDK leaves they reach come from the modules that translate them:
 * 001029C0 / 00102BB0 / 00102918 from em_owner_services_original.h,
 * 001B1470 from em_player_recovery.h (em_player_recovery_wrap); 001026A0
 * (four VU0 macro ops), 00102948 (a quadword copy) and 0011DF78 (fabs) are
 * written inline on em_ee_float.h.
 *
 * 00161790 (+5 2, ledge climb) and 00162190 (+5 3, vault) are translated by
 * em_player_climb.h (em_player_climb_live_state); they are not repeated here.
 *
 * Every routine works on the raw 0x320-byte player record (EmPlayerLiveActor,
 * em_player_floor.h) by its original offsets. Only `bytes` is written; the
 * +308 pointer word is read through `link_prev`. Every other original callee
 * is a worker. A missing worker (or scratch) faults with -1 before any
 * write; a worker that returns a negative value faults (-1) at once,
 * leaving the writes made before the call, as the original order leaves
 * them. Arithmetic is the EE's, bit for bit, through game/em_ee_float.h.
 *
 * Oracle: tools/test_player_running_jump_reference.py executes the original
 * instructions of every routine above (callees hooked, scripted and
 * recorded) and compares all 0x320 record bytes, the scratch words below,
 * the return values and the worker calls; EM_TEST_WORLD=1 replays the route
 * beats with the running jump (12, 14) over the captured RAM with these
 * routines in place of the originals. */
#ifndef EM_PLAYER_RUNNING_JUMP_H
#define EM_PLAYER_RUNNING_JUMP_H

#include <stdint.h>

#include "game/em_player_floor.h"
#include "game/em_player_climb.h"
#include "game/em_player_recovery.h"

/* The values the routines read outside the record and the workers. */
typedef struct EmPlayerRunningJumpScene {
    uint8_t area;          /* D_00810700 (0015EC50's per-area boxes) */
    uint8_t subarea;       /* D_00810701 */
    uint8_t spad3B8D;      /* 0x70003B8D (001AA4E0 finds nothing while set) */
    int16_t target_count;  /* D_00275B94: the D_00275B8C list length */
} EmPlayerRunningJumpScene;

/* The scratchpad words the routines write (raw bits). The binder owns one
 * instance; s3A20 is the 0x70003A20 word other modules also write and read
 * back (001AA2A0 writes the distance 001AA4E0 reads there). */
typedef struct EmPlayerRunningJumpScratch {
    uint32_t s36A0[16];    /* 0x700036A0: the yaw matrix of 0015EC50 / 0015FDF0 */
    uint32_t s38A0[16];    /* 0x700038A0..0x700038DC: vectors A0, B0, C0, D0 */
    uint32_t s3A20;        /* 0x70003A20 */
    uint32_t s3A28;        /* 0x70003A28 */
    uint32_t s3A2C;        /* 0x70003A2C */
} EmPlayerRunningJumpScratch;

/* One D_00275B8C entry as 001AA4E0 reads it. */
typedef struct EmPlayerRunningJumpTarget {
    const void *object;    /* the entry (the pool record: an EmActor * in the port) */
    uint8_t flags;         /* object +2 (qualifies when & 0x1F == 2) */
    uint8_t type;          /* object +3 (1, 2, 4, 5, 6, 7, 8 or 0xC qualify) */
    int16_t field34;       /* object +34 (qualifies when nonzero) */
} EmPlayerRunningJumpTarget;

typedef struct EmPlayerRunningJumpWorkers {
    void *context;
    EmPlayerRunningJumpScratch *scratch;
    /* SDK: 0011E2A8 sin, 0011DE90 cos, 0011E620 atan2(y = $f12, x = $f13),
     * 0011E748 sqrt. */
    float (*sine)(void *context, float x);
    float (*cosine)(void *context, float x);
    float (*atan2)(void *context, float y, float x);
    float (*sqrt)(void *context, float x);
    /* 0019AD00(p, target, mask) and 0019AFE0(p, from, to, mask): the return
     * value is the probe's (>= 0; negative is a fault); `hit` is what the
     * probe leaves at 0x700031B0 (point) and 0x700031D0 (node record: +1A
     * halfword, +24 normal). 0019AFE0 reads only x/y/z of its vectors. */
    int (*move)(void *context, EmPlayerLiveActor *actor, const float target[4], unsigned mask,
                EmPlayerProbeHit *hit);
    int (*sweep)(void *context, EmPlayerLiveActor *actor, const float from[4], const float to[4],
                 unsigned mask, EmPlayerProbeHit *hit);
    /* 0019BC40(at): the column table (count 0x700031E0, D_70003170 flags,
     * D_700030F0 heights). A count outside 0..EM_PLAYER_CLIMB_TABLE_MAX
     * faults (the original's arrays alias past 16). */
    int (*table)(void *context, const float at[4], EmPlayerClimbTable *table);
    /* 00177510(): the ledge frame from the probe hit it follows; 0015EC50
     * reads its point (0x70003050 x, 0x70003058 z). */
    int (*ledge)(void *context, const EmPlayerProbeHit *hit, EmPlayerRecoveryLedge *ledge);
    /* 001760C0(p, at, arg, height) -> *result; at is the record's +B0. */
    int (*column)(void *context, EmPlayerLiveActor *actor, const float at[4], int arg, float height,
                  int *result);
    /* (+308)+3: the type byte of the previous stage's link owner
     * (EmPlayerLiveActor.link_prev); called only when link_prev is set. */
    int (*link_type)(void *context, const void *owner, uint8_t *type);
    /* D_00275B8C[index] (index < target_count). */
    int (*target)(void *context, int index, EmPlayerRunningJumpTarget *out);
    /* The chosen object's +B0 and +B8 (read by 0015FDF0). */
    int (*target_xz)(void *context, const void *object, float *x, float *z);
    /* 001AA410(object) -> $f0. */
    int (*target_radius)(void *context, const void *object, float *radius);
    /* 001AA2A0(p, object, radius) -> *result. On a nonzero result 001AA4E0
     * reads 0x70003A20 (scratch->s3A20), which 001AA2A0 writes. */
    int (*target_sight)(void *context, EmPlayerLiveActor *actor, const void *object, float radius,
                        int *result);
    /* 00174AC0(p, arg): the turn toward the stick; *result is its return. */
    int (*heading)(void *context, EmPlayerLiveActor *actor, int arg, int *result);
    /* 001749A0(p, clip, force, blend $f12). */
    int (*request)(void *context, EmPlayerLiveActor *actor, int clip, int force, float blend);
    /* anim_clip_arbiter(p, clip, blend $f12, frame $f13) (0x1749F0). */
    int (*arbiter)(void *context, EmPlayerLiveActor *actor, int clip, float blend, float frame);
    /* 001C61D0(word +40, clip): the clip's frame count. */
    int (*clip_frames)(void *context, uint32_t bank, int clip, int32_t *frames);
    /* 001FBD50(p, id, 0, 300.0). */
    int (*sound)(void *context, EmPlayerLiveActor *actor, int id);
    /* 00178B90(p, arg), 00178EC0(p), 001751A0(p) (em_player_recovery.h
     * *_worker adapters fit these). */
    int (*translate)(void *context, EmPlayerLiveActor *actor, int arg);
    int (*strafe)(void *context, EmPlayerLiveActor *actor);
    int (*quadrant)(void *context, EmPlayerLiveActor *actor);
    /* 002243F0(p) -> *result; 0017C860(p, reach $f12 raw bits) -> *result. */
    int (*react)(void *context, EmPlayerLiveActor *actor, int *result);
    int (*grab)(void *context, EmPlayerLiveActor *actor, uint32_t reach, int *result);
    /* 0017DEB0(p): the climb dust and sound. */
    int (*dust)(void *context, EmPlayerLiveActor *actor);
    /* 0017C580(p), 0021D250(p, arg), 0021D2E0(p, frames, hold)
     * (em_player_fall.h translates all three). */
    int (*land)(void *context, EmPlayerLiveActor *actor);
    int (*surface5d)(void *context, EmPlayerLiveActor *actor, int arg);
    int (*teleport)(void *context, EmPlayerLiveActor *actor, int frames, int hold);
    /* 001764E0(p) with the caller's $s1 (EM_PLAYER_LAND_S1_CALLER: 001747F0
     * does not set $s1, so 001764E0 sees 0015B130's), 00175900(p, search)
     * (*result is its return) and 001796C0(p). */
    int (*probes)(void *context, EmPlayerLiveActor *actor, int s1);
    int (*floor)(void *context, EmPlayerLiveActor *actor, int search, int *result);
    int (*fall_check)(void *context, EmPlayerLiveActor *actor);
    /* *(*D_00275B40 + 8): the word 001747F0 reads (twice) from the first
     * skeleton node (raw bits). */
    int (*root_clock)(void *context, uint32_t *value);
} EmPlayerRunningJumpWorkers;

/* 1 when every worker and the scratch are bound. */
int em_player_running_jump_workers_bound(const EmPlayerRunningJumpWorkers *workers);

/* 0015EC50(p) -> *result (1: +5 = 6 was written). 0, or -1 on a fault. */
int em_player_running_jump_probe(EmPlayerLiveActor *actor, const EmPlayerRunningJumpScene *scene,
                                 const EmPlayerRunningJumpWorkers *workers, int *result);
/* 0015FDF0(p) -> *result (1 when 001AA4E0 found a target). */
int em_player_running_jump_aim(EmPlayerLiveActor *actor, const EmPlayerRunningJumpScene *scene,
                               const EmPlayerRunningJumpWorkers *workers, int *result);
/* 001AA4E0(p) -> *target (NULL when none). A negative target_count faults
 * (the original would walk the list for 2^32 entries). */
int em_player_running_jump_target(EmPlayerLiveActor *actor, const EmPlayerRunningJumpScene *scene,
                                  const EmPlayerRunningJumpWorkers *workers, const void **target);
/* 001634A0(p) and 001747F0(p). 0, or -1 on a fault (001634A0 case 1 also
 * faults when +25C > 3: its D_002485B0 / D_002485D0 rows end at tier 3). */
int em_player_running_jump_tick(EmPlayerLiveActor *actor, const EmPlayerRunningJumpWorkers *workers);
int em_player_running_jump_state24_tick(EmPlayerLiveActor *actor,
                                        const EmPlayerRunningJumpWorkers *workers);

/* ---- Binding (docs/PLAYER_RUNNING_JUMP.md "Binding") --------------------
 * One context for the adapters: the workers, the scene provider (called
 * before 0015EC50 / 0015FDF0; a missing one faults before any write) and an
 * optional shared 0x70003A20 word (raw bits, e.g. em_player_fall.h
 * EmPlayerLandScratch.s3A20): when set, scratch->s3A20 is loaded from it
 * before a routine and stored back after. */
typedef struct EmPlayerRunningJumpLive {
    EmPlayerRunningJumpWorkers workers;
    int (*scene)(void *context, EmPlayerRunningJumpScene *scene);
    void *scene_context;
    uint32_t *shared3A20;
} EmPlayerRunningJumpLive;

/* EmPlayerStageWorkers.state[6] = em_player_running_jump_state6 and
 * .state[0x24] = em_player_running_jump_state24, each with an
 * EmPlayerRunningJumpLive context. */
int em_player_running_jump_state6(void *context, EmPlayerLiveActor *actor);   /* 001634A0 */
int em_player_running_jump_state24(void *context, EmPlayerLiveActor *actor);  /* 001747F0 */
/* The Use chain 00160220's calls of 0015EC50 (at 0016075C) and 0015FDF0 (at
 * 00160778), over an EmPlayerRunningJumpLive context: *result is the routine's return.
 * When use_aim returns 1, 00160220 itself writes +5 = 0x24, +6 = 0,
 * +1F0 = 0x3A, +0 = 3 (the caller's job, not this module's). */
int em_player_running_jump_use_probe(void *context, EmPlayerLiveActor *actor, int *result);
int em_player_running_jump_use_aim(void *context, EmPlayerLiveActor *actor, int *result);

#endif
