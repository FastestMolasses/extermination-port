/* em_player_ladder_climb.h - the player's ladder climb: state +5 = 0xC
 * (docs/PLAYER_LADDER_CLIMB.md).
 *
 * Translations of the original routines, not models of them:
 *   001662D0  state 0xC callback (0015B130's table entry 0xC), dispatched
 *             on the sub-state +6: 0..4 (a cycle ends with +B4 = +294 + 3),
 *             0xA..0xC (+B4 = +294 - 3), 0x14..0x16 and 0x1E..0x20 (clips
 *             0xF0 / 0xF1, exits to +4 1 / +5 0), 0x28..0x2D (exit +5 = 7),
 *             0x32..0x35 and 0x3C..0x3F (side clips, D0-matrix steps),
 *             0x46..0x48 (8-step move, exits +5 = 9 / 0x18), 0x50..0x52
 *             (12-step move, exit +5 = 0x10).
 *   0017FD00 / 0017FD40              clip 0xE8/0xEA and 0xE9/0xEB by +2F1
 *   0017FD80 / 0017FE00 / 0017FE80 / 0017FF00   clip pairs by side and +2F1
 *   00180460  3 when 001760C0 hits over +B0 raised 4 (height 18), else the
 *             00180300 probe from (+290, +B4 + 18, +298)
 *   00180530  2 when 0019AB20 hits at +290 lowered 4.5, else 00180300 from
 *             +290 lowered 3 (nonzero -> 1)
 *   00180600  two points of the +D0 matrix and one 0019AFE0 sweep
 *   001809B0  three matrix-box sweeps; returns 0, 1, 2, 3, 5 or 7
 * and these byte-matched leaves they reach, translated here as well:
 *   0017FC80  hold clip D_002754D0 / D_002754D4 [+235 & 1] by +2F1
 *             (001885D0 / 001885F0 inline)
 *   00180420  +290 = the +D0 matrix applied to (0, 0, -3, 1)
 *   00174AB0  001749A0(p, 0, 1, 0.0)
 *   001031E0  three-word copy; 00102948 quadword copy; 0011DF78 fabs
 * 00181110 is em_player_major2_00181110 (em_player_major2.c), reused.
 *
 * The routines operate on the live actor (EmPlayerLiveActor, the raw
 * 0x320-byte player record) by original offset. Every other original callee
 * is an explicit worker of EmPlayerLadderClimbWorkers. The state refuses to run
 * (-1, nothing written) unless every worker is bound, and a worker that
 * returns a negative value stops the routine with -1, leaving the writes
 * made before the call, as the original order leaves them. EE COP1
 * arithmetic goes through em_ee_float.h (docs/EE_FLOAT_MODEL.md).
 *
 * The scratchpad words these routines store and pass by address
 * (0x700038A0..0x700038DF, 0x70003A20..0x70003A2F) live in
 * EmPlayerLadderClimbScene and are written exactly as the original writes the
 * scratchpad; vector workers receive pointers into them where the original
 * passes the scratchpad address.
 *
 * Oracle: tools/test_player_ladder_climb_reference.py executes the original
 * instructions of every routine above from the user's pinned ELF (COP1
 * through tools/ee_float_model.py), every other callee hooked, and compares
 * all 0x320 actor bytes, the scratchpad words, the globals, the return
 * values and the callee sequence with every argument. */
#ifndef EM_PLAYER_LADDER_CLIMB_H
#define EM_PLAYER_LADDER_CLIMB_H

#include <stdint.h>

#include "game/em_player_floor.h"

/* The globals and scratchpad words these routines read or write outside the
 * actor. Read and written in place, at the original's access time. */
typedef struct EmPlayerLadderClimbScene {
    uint8_t area;        /* D_00810700: the tail's 00176DC0 test (== 2); 001809B0 (== 8) */
    uint8_t area_sub;    /* D_00810701: 001809B0 (== 3) */
    uint8_t d8106F2;     /* D_008106F2[0]: written 5 / 4 by sub-states 0x34 / 0x3E */
    uint8_t pad0;
    uint16_t pad;        /* D_00810E74[0], the pad edge halfword (0x34, 0x3E) */
    uint16_t use_mask;   /* spad 0x70003B76, ANDed with the pad halfword */
    /* spad 0x700038A0..0x700038DF: the four vectors A0, B0, C0, D0 (raw
     * binary32 words stored as floats; the routines copy bits). */
    float spad38A0[16];
    /* spad 0x70003A20..0x70003A2F. */
    float spad3A20[4];
} EmPlayerLadderClimbScene;

/* The original callees. Each returns 0, or a negative value on a fault,
 * except the scalar float workers, which cannot fault. `actor` is the
 * record the original passes in $a0. A vector argument that the original
 * passes as a scratchpad address points into the EmPlayerLadderClimbScene spad
 * arrays; one it passes as an actor or stack address points to a copy. */
typedef struct EmPlayerLadderClimbWorkers {
    void *context;
    /* *(uint32_t *)(*(D_00275B40 + 4 * node) + offset): a skeleton node
     * word, read when the original reads it. Reads: node 0 +0/+4/+8,
     * node 1 +C0/+C4/+C8/+CC. */
    int (*node)(void *context, int node, unsigned offset, uint32_t *bits);
    /* The last collision hit (spad 0x700031D0 / 0x700031B4), as the
     * previous sweep left it: the byte at *(0x700031D0) + 0x1A, and the
     * word at 0x700031B4. */
    int (*hit_kind)(void *context, int *kind);
    int (*hit_y)(void *context, uint32_t *bits);

    /* 001749A0(p, clip, flags, blend). */
    int (*request)(void *context, EmPlayerLiveActor *actor, int clip, int flags, float blend);
    /* 001FBD50(p, id, flags, range); its return value is not used here. */
    int (*sound)(void *context, EmPlayerLiveActor *actor, int id, int flags, float range);
    /* 001FB9F0(id, a1, a2, a3) and 001B61C0(a0, a1, a2, a3). */
    int (*sfx)(void *context, int id, int a1, int a2, int a3);
    int (*cue)(void *context, int a0, int a1, int a2, int a3);
    int (*sound_109)(void *context, EmPlayerLiveActor *actor);                  /* 00182A70(p) */
    int (*steer_input)(void *context, EmPlayerLiveActor *actor);                /* 00174FD0(p) */
    int (*heading)(void *context, EmPlayerLiveActor *actor, int arg);           /* 00174AC0(p, arg) */
    int (*skeleton)(void *context, EmPlayerLiveActor *actor);                   /* 001C68C0(p) */
    /* 00175900(p, search): *result = its return value. */
    int (*floor)(void *context, EmPlayerLiveActor *actor, int search, int *result);
    int (*footstep)(void *context, EmPlayerLiveActor *actor, int arg);          /* 00182430(p, arg) */
    /* 00187EE0(p, p + 0xB0, p + 0xD0): the original always passes the
     * actor's own position and matrix. */
    int (*ground_effect)(void *context, EmPlayerLiveActor *actor);
    int (*translate)(void *context, EmPlayerLiveActor *actor, int arg);         /* 00178B90(p, arg) */
    int (*reentry)(void *context, EmPlayerLiveActor *actor, int arg);           /* 0017C440(p, arg) */
    int (*handoff)(void *context, EmPlayerLiveActor *actor);                    /* 0017C540(p) */
    int (*w0021C270)(void *context, EmPlayerLiveActor *actor);                  /* 0021C270(p) */
    int (*w0021C350)(void *context, EmPlayerLiveActor *actor);                  /* 0021C350(p) */
    int (*camera)(void *context, EmPlayerLiveActor *actor);                     /* 00176DC0(p) */
    /* 00188550(p): *clip = its (sign-extended halfword) return value. */
    int (*clip_row)(void *context, EmPlayerLiveActor *actor, int *clip);
    /* 001C61D0(word +40, (short)+20C): *result = its return value. */
    int (*clip_frames)(void *context, uint32_t a0, int a1, int *result);

    /* Collision callees. */
    /* 00180300(p, at, kind): at = spad 0x700038A0. */
    int (*probe)(void *context, EmPlayerLiveActor *actor, float at[4], int kind, int *result);
    /* 001760C0(p, at, arg, height): at = spad 0x700038A0. */
    int (*column)(void *context, EmPlayerLiveActor *actor, float at[4], int arg, float height,
                  int *result);
    /* 0019AB20(p, at, p + 0x280, mask): at = spad 0x700038B0. */
    int (*wall)(void *context, EmPlayerLiveActor *actor, float at[4], int mask, int *result);
    /* 0019AFE0(p, from, to, mask): from / to = spad 0x700038C0 / 0x700038D0. */
    int (*sweep)(void *context, EmPlayerLiveActor *actor, float from[4], float to[4],
                 unsigned mask, int *result);
    /* 0019A570(a, b, c, d): a / b = spad 0x700038B0 / 0x700038C0. */
    int (*sweep_box)(void *context, float a[4], float b[4], int c, int d, int *result);
    /* 00199FA0(a, b): two stack vectors the callee fills; the original reads
     * b[1] when the result is nonzero. */
    int (*hit_probe)(void *context, float a[4], float b[4], int *result);
    /* 00199DB0(out): out = spad 0x700038A0. */
    int (*hit_point)(void *context, float out[4]);
    /* 001026A0(out, M, in): out = in x M. M is a copy of the actor's +D0
     * matrix; out is a spad vector or (00180420) a copy of +290 that the
     * routine stores back into the actor right after the call. */
    int (*transform)(void *context, float out[4], const float matrix[16], const float in[4]);
    /* 0017E250(p, v): v a copy of the stack vector (+2E0, +2E4, +2E8, 1.0). */
    int (*ledge_ahead)(void *context, EmPlayerLiveActor *actor, const float v[4], int *result);
    int (*grab_check)(void *context, EmPlayerLiveActor *actor, int *result);    /* 00178390(p) */
    int (*dash)(void *context, EmPlayerLiveActor *actor, int arg);              /* 00177030(p, arg) */
    int (*grab)(void *context, EmPlayerLiveActor *actor, int *result);          /* 001782A0(p) */
    int (*hit_react)(void *context, EmPlayerLiveActor *actor, int *result);     /* 00178080(p) */

    /* SDK scalar calls: 0011E748 sqrt, 0011E2A8 sin, 0011DE90 cos, 001B1470
     * wrap, 001B12B0(target, current, rate) approach, 001281C0 float_to_int. */
    float (*sqrt)(void *context, float x);
    float (*sine)(void *context, float x);
    float (*cosine)(void *context, float x);
    float (*wrap)(void *context, float x);
    float (*approach)(void *context, float target, float current, float rate);
    int32_t (*to_int)(void *context, float x);
} EmPlayerLadderClimbWorkers;

/* The context of em_player_ladder_climb_state and of the routines below. */
typedef struct EmPlayerLadderClimb {
    const EmPlayerLadderClimbWorkers *workers;
    EmPlayerLadderClimbScene *scene;
} EmPlayerLadderClimb;

/* 001662D0 over the live actor. context is an EmPlayerLadderClimb. Bind as
 * EmPlayerStageWorkers.state[0xC] (0015B130's table; the stage's
 * state_context[0xC] is the EmPlayerLadderClimb). Returns 0, or -1 when a
 * worker or the scene is missing (nothing written) or a worker faults. */
int em_player_ladder_climb_state(void *context, EmPlayerLiveActor *actor);

/* The helpers, for their other callers and the oracle. Each needs only the
 * workers it reaches (request for the clip helpers) and returns 0 or -1;
 * the probes store the original's return value in *result.
 * 0017FC80, 00174AB0 and 00180420 are translated only here: the closure
 * (em_player_closure_0e_18.c) and ladder-entry (em_player_ladder_entry.c)
 * lanes call these through a one-routine EmPlayerLadderClimb over their own
 * workers (docs/PLAYER_LADDER_CLIMB.md "One owner"). */
int em_player_ladder_climb_0017FC80(const EmPlayerLadderClimb *ladder, EmPlayerLiveActor *actor,
                                    float blend);
int em_player_ladder_climb_0017FD00(const EmPlayerLadderClimb *ladder, EmPlayerLiveActor *actor,
                                    float blend);
int em_player_ladder_climb_0017FD40(const EmPlayerLadderClimb *ladder, EmPlayerLiveActor *actor,
                                    float blend);
int em_player_ladder_climb_0017FD80(const EmPlayerLadderClimb *ladder, EmPlayerLiveActor *actor,
                                    int side, float blend);
int em_player_ladder_climb_0017FE00(const EmPlayerLadderClimb *ladder, EmPlayerLiveActor *actor,
                                    int side, float blend);
int em_player_ladder_climb_0017FE80(const EmPlayerLadderClimb *ladder, EmPlayerLiveActor *actor,
                                    int side, float blend);
int em_player_ladder_climb_0017FF00(const EmPlayerLadderClimb *ladder, EmPlayerLiveActor *actor,
                                    int side, float blend);
/* 00174AB0(p): 001749A0(p, 0, 1, 0.0) (request only). */
int em_player_ladder_climb_00174AB0(const EmPlayerLadderClimb *ladder, EmPlayerLiveActor *actor);
/* 00180420(p) (transform and the scene's spad A0 only). */
int em_player_ladder_climb_00180420(const EmPlayerLadderClimb *ladder, EmPlayerLiveActor *actor);
int em_player_ladder_climb_00180460(const EmPlayerLadderClimb *ladder, EmPlayerLiveActor *actor,
                                    int *result);
int em_player_ladder_climb_00180530(const EmPlayerLadderClimb *ladder, EmPlayerLiveActor *actor,
                                    int *result);
/* 00180600 has no return statement; its $v0 is the 0019AFE0 result it
 * leaves, which its callers test. */
int em_player_ladder_climb_00180600(const EmPlayerLadderClimb *ladder, EmPlayerLiveActor *actor,
                                    int side, float reach, float height, float depth, int *result);
int em_player_ladder_climb_001809B0(const EmPlayerLadderClimb *ladder, EmPlayerLiveActor *actor,
                                    int side, int *result);

#endif
