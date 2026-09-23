/* em_player_hang.h - the player's ledge hang: state +5 = 9 (docs/PLAYER_HANG.md).
 *
 * Translations of the original routines, not models of them:
 *   001647D0  state 9 callback (0015B130's table entry 9). Its sub-state +6:
 *             0/1 hang entry and hold, 2/3/4/5 pull-up and landing, 0xA/0xB
 *             drop to the fall (+5 = 7), 0x14 shimmy, 0x1E..0x20 and
 *             0x28..0x2A the two edge-turn chains, 0x21..0x23 the corner
 *             transfer (exits to +5 = 9/0xC/0xE/0x18), 0x27/0x31 turn back.
 *   0017F240  hang reaction test: a pending hit (+224/+22C) or +F bit 1
 *             enters +4 = 2, +5 = 6 (0015B770's 00222AD0) and returns 1.
 *   001028B8  VU0 vector add (vadd.xyzw), the 0x39 surface push of sub-state 3.
 *
 * Entry: the fall 00162DB0 stores +5 = 9 at 00163290; 001647D0's own sub-state
 * 0x23 re-enters +5 = 9, +6 = 0 (00165828).
 *
 * The routine operates on the live actor (EmPlayerLiveActor, the raw 0x320-byte
 * player record) by original offset. Every original callee except 0017F240 is
 * an explicit worker of EmPlayerHangWorkers; em_player_hang_state refuses to
 * run (-1, nothing written) unless every worker is bound, and a worker that
 * returns a negative value stops the routine with -1, leaving the writes made
 * before the call, as the original order leaves them. EE COP1/VU0 arithmetic
 * goes through em_ee_float.h (docs/EE_FLOAT_MODEL.md).
 *
 * Oracle: tools/test_player_hang_reference.py executes the original
 * instructions of 001647D0, 0017F240 and 001028B8 from the user's pinned ELF
 * (COP1/VU0 through tools/ee_float_model.py), every 001647D0 callee except
 * 0017F240 hooked, and compares all 0x320 actor bytes, the return value and
 * the callee sequence with every argument. */
#ifndef EM_PLAYER_HANG_H
#define EM_PLAYER_HANG_H

#include <stdint.h>

#include "game/em_player_floor.h"

/* Values 001647D0 reads outside the actor that no worker it calls writes.
 * Read once, at entry, through EmPlayerHangWorkers.scene. */
typedef struct EmPlayerHangScene {
    uint8_t area;        /* D_00810700 (sub-state 0: 0x11 enables the +316 radius test) */
    uint8_t scripted;    /* spad 0x70003B8D (sub-states 1 and 3) */
    uint16_t pad;        /* D_00810E74[0] (sub-states 1, 0x20, 0x2A) */
    uint16_t use_mask;   /* spad 0x70003B76, ANDed with the pad halfword */
} EmPlayerHangScene;

typedef struct EmPlayerHangWorkers {
    void *context;
    /* The EmPlayerHangScene bytes, once per callback. */
    int (*scene)(void *context, EmPlayerHangScene *scene);
    /* *(float *)(*(D_00275B40 + 4 * node) + offset): a skeleton node word,
     * read when the original reads it (after 001C68C0 in sub-state 3, around
     * 00178B90 in sub-state 0xB). Reads: node 0 +4/+8, node 1 +8/+C4. */
    int (*node)(void *context, int node, unsigned offset, float *value);

    /* Callees taking the actor; each may read and write any actor byte. */
    int (*aim_track)(void *context, EmPlayerLiveActor *actor);                  /* 00182250(p) */
    int (*hang_clear)(void *context, EmPlayerLiveActor *actor, int *result);    /* 0017F320(p) */
    int (*steer_input)(void *context, EmPlayerLiveActor *actor);                /* 00174FD0(p) */
    /* 0017E250(p, p + B0), 0017E510(p), 0017E7C0(p, side). */
    int (*ledge_ahead)(void *context, EmPlayerLiveActor *actor, int *result);
    int (*ledge_above)(void *context, EmPlayerLiveActor *actor, int *result);
    int (*ledge_side)(void *context, EmPlayerLiveActor *actor, int side, int *result);
    /* 001749A0(p, clip, force, blend). */
    int (*request)(void *context, EmPlayerLiveActor *actor, int clip, int force, float blend);
    /* 001FBD50(p, id, flags, range); its return value is not used here. */
    int (*sound)(void *context, EmPlayerLiveActor *actor, int id, int flags, float range);
    int (*skeleton)(void *context, EmPlayerLiveActor *actor);                   /* 001C68C0(p) */
    int (*translate)(void *context, EmPlayerLiveActor *actor, int arg);         /* 00178B90(p, arg) */
    /* 00175900(p, search): *result = its return value (+A). */
    int (*floor)(void *context, EmPlayerLiveActor *actor, int search, int *result);
    /* 001760C0(p, at, arg, height): at is the actor's own +B0 here. */
    int (*column)(void *context, EmPlayerLiveActor *actor, const float at[4], int arg,
                  float height, int *result);
    int (*land_sound)(void *context, EmPlayerLiveActor *actor, int tier);       /* 00182870(p, tier) */
    int (*heading)(void *context, EmPlayerLiveActor *actor, int arg);           /* 00174AC0(p, arg) */
    int (*reentry)(void *context, EmPlayerLiveActor *actor, int arg);           /* 0017C440(p, arg) */
    int (*handoff)(void *context, EmPlayerLiveActor *actor);                    /* 0017C540(p) */
    int (*fall)(void *context, EmPlayerLiveActor *actor);                       /* 001796C0(p) */
    /* The side clip requests (p, side, blend): 0017DF70, 0017DFB0, 0017E0D0,
     * 0017E150, 0017E1D0. side is passed as the original passes it (a byte,
     * or 1 - byte in sub-state 0x21). */
    int (*clip_DF70)(void *context, EmPlayerLiveActor *actor, int side, float blend);
    int (*clip_DFB0)(void *context, EmPlayerLiveActor *actor, int side, float blend);
    int (*clip_E0D0)(void *context, EmPlayerLiveActor *actor, int side, float blend);
    int (*clip_E150)(void *context, EmPlayerLiveActor *actor, int side, float blend);
    int (*clip_E1D0)(void *context, EmPlayerLiveActor *actor, int side, float blend);
    /* 0017FC80(p, blend) and 0017FF80(p, blend). */
    int (*clip_FC80)(void *context, EmPlayerLiveActor *actor, float blend);
    int (*clip_FF80)(void *context, EmPlayerLiveActor *actor, float blend);
    /* 00188550(p): *clip = its (sign-extended halfword) return value. */
    int (*clip_row)(void *context, EmPlayerLiveActor *actor, int *clip);
    int (*sound_100)(void *context, EmPlayerLiveActor *actor);                  /* 00182AF0(p) */
    int (*sound_109)(void *context, EmPlayerLiveActor *actor);                  /* 00182A70(p) */
    /* 0019AFE0(p, from, to, mask): *result = its return value. It must leave
     * its hit in the state the following 00178910 reads (spad 0x700031B0,
     * 0x700031D0): the binder shares that through the two workers' context. */
    int (*sweep)(void *context, EmPlayerLiveActor *actor, const float from[4], const float to[4],
                 unsigned mask, int *result);
    int (*ledge_top)(void *context, EmPlayerLiveActor *actor, int arg, int *result); /* 00178910 */

    /* Callees on vectors (the original passes spad temporaries). */
    /* 001026A0(out, M, in) with M = the actor's +D0 matrix. */
    int (*transform)(void *context, float out[4], const float matrix[16], const float in[4]);
    /* 001028B8(out, a, b); em_player_hang_vadd fits (see below). */
    int (*vadd)(void *context, float out[4], const float a[4], const float b[4]);

    /* SDK scalar calls: 0011E748 sqrt, 0011DE90 cos, 0011E2A8 sin, 001B1470
     * wrap, 001B12B0(target, current, rate) approach. */
    float (*sqrt)(void *context, float x);
    float (*cosine)(void *context, float x);
    float (*sine)(void *context, float x);
    float (*wrap)(void *context, float x);
    float (*approach)(void *context, float target, float current, float rate);
} EmPlayerHangWorkers;

/* 0017F240(p, arg): 1 when the hang reacts (+302 = +5, +4 = 2, +5 = 6,
 * +6 = 0), else 0; with arg != 0 and no reaction it may set +204 = +3C - 2.0
 * (+3C >= 3.0). 001647D0 always passes 0. */
int em_player_hang_0017F240(EmPlayerLiveActor *actor, int arg);

/* 001028B8(out, a, b): out = a + b in all four lanes (VU0 vadd.xyzw). out may
 * alias a or b. Returns 0, or -1 if em_ee_float.h refuses the form. Fits
 * EmPlayerHangWorkers.vadd (context unused). */
int em_player_hang_vadd(void *context, float out[4], const float a[4], const float b[4]);

/* 001647D0 over the live actor. context is a const EmPlayerHangWorkers *.
 * Bind as EmPlayerStageWorkers.state[9] (0015B130's table). Returns 0, or
 * -1 when a worker is missing (nothing written), a worker faults, or a
 * table index is outside the original table (+23F > 3 or +2F1 > 1: the
 * original would read neighbouring data; the native refuses). */
int em_player_hang_state(void *context, EmPlayerLiveActor *actor);

#endif
