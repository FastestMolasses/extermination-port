/* em_player_use_dispatch.h - the player's Use dispatcher and the two small
 * routines around it (docs/PLAYER_USE_DISPATCH.md).
 *
 * Translations of the original routines (boot ELF SCUS-97112), each
 * byte-matched in the decomp (the compiled C is identical to the original
 * bytes), not models of them:
 *   00160220  the Use dispatcher, polled by the idle and walk states
 *             (00161020 +6 = 1/2, 001612D0 +6 = 0/1). On a Use press it
 *             tries, in this order: the interaction scan 00184BA0 (a winner
 *             -> 001798D0, +5 = 0x25); area 0x15's classifier 001AAC00
 *             (+5 = 0x23); the surface actions 0015D4C0 (ladders, +5 = 0xB
 *             in AREA11); unless the actor stands in one of three areas'
 *             trigger boxes, the ledge probe 0015DF10 at the body yaw, then
 *             yaw - pi/4, then yaw + pi/4 (climb +5 = 2 or vault 3); the
 *             running jump 0015EC50 (+5 = 6); and the aim solver 0015FDF0
 *             (+5 = 0x24). Returns 1 when one of them took the press.
 *   001798D0  the accepted-Use reset: +38 = 0, +21C = 0, +25C = 0,
 *             00174A50(p, 0.0), then +5 = +6 = +1F0 = 0.
 *   0017C440  the gait re-entry request (0017C030 mode 4 and several state
 *             routines): +25C = +23F - 1, +38 = D_00248870[+25C],
 *             00178B90(p, arg), clip = 0017B490(p, 1, +235, +25C),
 *             0x70003A20 = (float)001C61D0(+40, clip), then
 *             anim_clip_arbiter(p, clip, 4.0, 0x70003A20 - 18 (tier 2) or
 *             - 46), and +1F0 = 1.
 *
 * Every routine works on the raw 0x320-byte player record
 * (EmPlayerLiveActor, em_player_floor.h) by its original offsets; only
 * `bytes` is written. Every original callee is a worker. Fail-stop: an entry
 * point first checks every worker (and scene / scratch pointer) it can
 * reach and returns -1 before its first write when one is missing; a worker
 * that returns a negative value is a fault too, and the routine stops at
 * once with -1, leaving the writes made before the call, as the original
 * order leaves them. Arithmetic (the two +-pi/4 sums, the int-to-float
 * conversion and the frame subtraction) is the EE's, bit for bit, through
 * game/em_ee_float.h; the trigger-box comparisons use its c.lt / c.le.
 *
 * Oracle: tools/test_player_use_dispatch_reference.py executes the original
 * instructions of the three routines (callees hooked, scripted and
 * recorded) and compares all 0x320 record bytes, the 0x70003A20 word, the
 * return values and the worker calls with the record bytes at each call;
 * EM_TEST_WORLD=1 replays route beats 05 (both box climbs), 12 (the running
 * jump) and 13 (the high ledge climb) over the captured RAM with these
 * routines in place of the originals. */
#ifndef EM_PLAYER_USE_DISPATCH_H
#define EM_PLAYER_USE_DISPATCH_H

#include <stdint.h>

#include "game/em_player_floor.h"

/* The words 00160220 reads outside the record. The binder keeps them
 * current; the dispatcher reads `area` again at each place the original
 * loads D_00810700 (after 00184BA0 and after 0015D4C0). */
typedef struct EmPlayerUseScene {
    uint16_t d810E74;    /* D_00810E74: the pad block's pressed-this-frame word */
    uint16_t spad3B76;   /* 0x70003B76: the configured Use bit (default 0x0040) */
    uint8_t area;        /* D_00810700 */
} EmPlayerUseScene;

typedef struct EmPlayerUseWorkers {
    void *context;
    const EmPlayerUseScene *scene;
    /* 00184BA0(p): the interaction scan; *result nonzero = an owner won. */
    int (*scan)(void *context, EmPlayerLiveActor *actor, int *result);
    /* 00174A50(p, blend): the idle row request (001798D0 passes 0.0).
     * em_player_stage_row_request and EmLocoWorkers.row_request fit. */
    int (*row_request)(void *context, EmPlayerLiveActor *actor, float blend);
    /* 001AAC00(p, p + 0x290, p + 0x218): area 0x15's classifier; *result
     * is its return (1, 2, 3 or another nonzero value take the press). */
    int (*classify)(void *context, EmPlayerLiveActor *actor, int *result);
    /* 0015D4C0(p): the surface actions; *result nonzero = one started. */
    int (*surface)(void *context, EmPlayerLiveActor *actor, int *result);
    /* build_trs_matrix 001C94B0(p + D0, p + B0, p + C0, p + 60), raw words
     * (EmPlayerLadderWorkers.trs and em_pose_host_build_trs_matrix fit);
     * `out` is written back to +D0..+10C. */
    int (*trs)(void *context, uint32_t out[16], const uint32_t position[4],
               const uint32_t rotation[4], const uint32_t scale[4]);
    /* 0015DF10(p, mode, angle $f12 raw bits): the ledge probe; *result
     * nonzero = a climb or vault started. */
    int (*ledge)(void *context, EmPlayerLiveActor *actor, int mode, uint32_t angle, int *result);
    /* 001B1470(x): the angle wrap, raw bits in and out. */
    int (*wrap)(void *context, uint32_t x, uint32_t *out);
    /* 0015EC50(p) and 0015FDF0(p) (em_player_running_jump_use_probe /
     * _use_aim fit with an EmPlayerRunningJumpLive context). */
    int (*jump)(void *context, EmPlayerLiveActor *actor, int *result);
    int (*aim)(void *context, EmPlayerLiveActor *actor, int *result);
} EmPlayerUseWorkers;

/* 1 when the scene and every worker 00160220 can reach are bound. */
int em_player_use_workers_bound(const EmPlayerUseWorkers *workers);

/* 00160220(p). *result is its return value (1: the press was taken). 0, or
 * -1 on a fault. */
int em_player_use_00160220(const EmPlayerUseWorkers *workers, EmPlayerLiveActor *actor,
                           int *result);
/* 001798D0(p). Needs only `row_request`. 0, or -1 on a fault. */
int em_player_use_001798D0(const EmPlayerUseWorkers *workers, EmPlayerLiveActor *actor);

/* Worker-slot adapters (context = const EmPlayerUseWorkers *):
 * EmLocoWorkers.ladder (00160220) and EmLocoWorkers.use_accepted (001798D0). */
int em_player_use_dispatch_worker(void *workers, EmPlayerLiveActor *actor, int *result);
int em_player_use_accepted_worker(void *workers, EmPlayerLiveActor *actor);

/* ---- 0017C440 ------------------------------------------------------------ */
typedef struct EmPlayerReentryWorkers {
    void *context;
    /* 0x70003A20 (raw bits): the scratch word 0017C440 writes and reads back;
     * the binder hands the same storage to the other modules that share it
     * (e.g. EmPlayerLandScratch.s3A20). */
    uint32_t *spad3A20;
    /* D_00248870[tier]: a data read (raw bits). The original indexes the
     * table by the byte +25C without a bound (tier 0xFF when +23F is 0);
     * the binder faults on an address it does not map. */
    int (*speed)(void *context, unsigned tier, uint32_t *bits);
    /* 00178B90(p, arg): the translation along +C4 by +38. */
    int (*translate)(void *context, EmPlayerLiveActor *actor, int arg);
    /* 0017B490(p, cmd, idx, tbl): *clip is the sign-extended halfword
     * (em_loco_0017B490 fits through a one-line adapter). */
    int (*select)(void *context, EmPlayerLiveActor *actor, int cmd, int idx, int tbl,
                  int16_t *clip);
    /* 001C61D0(word +40, clip): the clip's frame count. */
    int (*clip_frames)(void *context, uint32_t bank, int clip, int32_t *frames);
    /* anim_clip_arbiter 001749F0(p, clip, blend $f12, frame $f13). The
     * return value is not read. */
    int (*arbiter)(void *context, EmPlayerLiveActor *actor, int clip, float blend, float frame);
} EmPlayerReentryWorkers;

int em_player_reentry_workers_bound(const EmPlayerReentryWorkers *workers);
/* 0017C440(p, arg). 0, or -1 on a fault. */
int em_player_reentry_0017C440(const EmPlayerReentryWorkers *workers, EmPlayerLiveActor *actor,
                               int arg);
/* Worker-slot adapter (context = const EmPlayerReentryWorkers *):
 * EmLocoWorkers.reentry, EmPlayerLandWorkers.reentry and the other
 * int (*)(void *, EmPlayerLiveActor *, int) 0017C440 slots. */
int em_player_reentry_worker(void *workers, EmPlayerLiveActor *actor, int arg);

#endif
