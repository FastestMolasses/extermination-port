/* em_player_major2.h - the player's +4 = 2 states of the FLOOR closure and
 * the routines that enter them (docs/PLAYER_MAJOR2.md).
 *
 * Translations of the original routines, not models of them:
 *   0021E830  +4 2 / +5 3    (0015B770 case 3)
 *   00221FC0  +4 2 / +5 4    (0015B770 case 4)
 *   00222580  +4 2 / +5 5    (0015B770 case 5)
 *   00222AD0  +4 2 / +5 6    (0015B770 case 6)
 *   002230A0  +4 2 / +5 7    (0015B770 case 7)
 *   00225570  +4 2 / +5 0x16 (0015B770 case 22)
 *   002255C0  +4 2 / +5 0x19 (0015B770 case 25, after its own +1 = 0)
 *   00181110  enter 2/4      00181180  enter 2/5
 *   00181D70  enter 2/7      001823E0  enter 2/0x19
 * 0015B770's +5 = 23 and 24 aliases go to 0021D800 and 0021E490 (states 0
 * and 2), not to any routine here.
 *
 * Every routine works on the raw 0x320-byte player record (EmPlayerLiveActor)
 * by its original offsets. Every original callee is an explicit worker. A
 * state routine checks, before its first write, that every worker it can
 * reach is bound, and faults (-1) otherwise; a worker that returns a
 * negative value is a fault too, and the writes made before it stay, as the
 * original order leaves them. Arithmetic and float compares follow the EE
 * COP1 model (src/game/em_ee_float.h, docs/EE_FLOAT_MODEL.md).
 *
 * Oracle: tools/test_player_major2_reference.py executes the original
 * instructions over the captured AREA11 RAM and compares all 0x320 record
 * bytes, the globals and every callee call (order and arguments). */
#ifndef EM_PLAYER_MAJOR2_H
#define EM_PLAYER_MAJOR2_H

#include <stdint.h>

#include "game/em_player_floor.h"

/* Globals these routines read or write outside the record. D_008106F1 and
 * D_00810707 are pointers at their one canonical byte (the same bytes the
 * stage scene and the stage globals point at; the w0021C270 worker can set
 * both in the middle of a routine): a routine refuses (-1, nothing
 * written) when either is missing. */
typedef struct EmPlayerMajor2Scene {
    const uint8_t *d8106F1; /* D_008106F1[0]: read by the state-0 alternate-exit test
                               (00221FC0/00222580/00222AD0/002230A0) */
    uint8_t *d810707;       /* D_00810707: 0021E830 sub-state 1 writes 2 */
    int32_t d275B14;  /* D_00275B14: 00181D70 writes 0x34/0x36/0x1E, 002230A0 reads.
                         Shared with 001696A0, 0016ADE0 and 0016B8A0. */
} EmPlayerMajor2Scene;

/* The original callees. Each returns 0, or a negative value on a fault.
 * `actor` is the record the original passes in $a0. */
typedef struct EmPlayerMajor2Workers {
    void *context;
    /* 001749A0(p, clip, flags, blend). */
    int (*request)(void *context, EmPlayerLiveActor *actor, int clip, int flags, float blend);
    /* 001FBD50(p, id, a2, radius); its return value is not used here. */
    int (*sound)(void *context, EmPlayerLiveActor *actor, int id, int a2, float radius);
    /* 001B61C0(a0, a1, a2, a3). */
    int (*cue)(void *context, int a0, int a1, int a2, int a3);
    /* 001EFE00(id, p). */
    int (*w001EFE00)(void *context, uint32_t id, EmPlayerLiveActor *actor);
    /* 0015C1F0(p). */
    int (*w0015C1F0)(void *context, EmPlayerLiveActor *actor);
    /* 0021E650(p). */
    int (*w0021E650)(void *context, EmPlayerLiveActor *actor);
    /* 0021D2E0(p, a1, a2). */
    int (*w0021D2E0)(void *context, EmPlayerLiveActor *actor, int a1, int a2);
    /* 00179880(p, p + 0x2EC): the drop accumulator. */
    int (*drop)(void *context, EmPlayerLiveActor *actor);
    /* 00175900(p, search): the floor service; *result is its return value.
     * The same signature as EmPlayerSlideLive.floor
     * (player_states_floor_service). */
    int (*floor)(void *context, EmPlayerLiveActor *actor, int search, int *result);
    /* 00178B90(p, arg). */
    int (*translate)(void *context, EmPlayerLiveActor *actor, int arg);
    /* 00182870(p, tier). */
    int (*land_sound)(void *context, EmPlayerLiveActor *actor, int tier);
    /* 0021D250(p, a1). */
    int (*w0021D250)(void *context, EmPlayerLiveActor *actor, int a1);
    /* 0021D490(p), 0021C120(p), 0021C200(p), 0021C270(p), 0021C350(p). */
    int (*w0021D490)(void *context, EmPlayerLiveActor *actor);
    int (*w0021C120)(void *context, EmPlayerLiveActor *actor);
    int (*w0021C200)(void *context, EmPlayerLiveActor *actor);
    int (*w0021C270)(void *context, EmPlayerLiveActor *actor);
    int (*w0021C350)(void *context, EmPlayerLiveActor *actor);
    /* 0021C190(p); *result is its return value. */
    int (*w0021C190)(void *context, EmPlayerLiveActor *actor, int *result);
    /* 0017FC80(p, blend) and 0017FF80(p, blend). */
    int (*w0017FC80)(void *context, EmPlayerLiveActor *actor, float blend);
    int (*w0017FF80)(void *context, EmPlayerLiveActor *actor, float blend);
    /* 00122BB8(): the SDK rand(); *value is its return value. */
    int (*random)(void *context, uint32_t *value);
    /* 00188550(p) and 001885B0(p): *clip is the return value, which the
     * caller passes on to 001749A0. */
    int (*w00188550)(void *context, EmPlayerLiveActor *actor, int *clip);
    int (*w001885B0)(void *context, EmPlayerLiveActor *actor, int *clip);
    /* The word at *(*D_00275B40) + offset (offset 4 or 8): a float of the
     * skeleton's first node, read at the point the original loads it. */
    int (*root_node)(void *context, unsigned offset, uint32_t *bits);
} EmPlayerMajor2Workers;

/* The context of the state callbacks below. */
typedef struct EmPlayerMajor2 {
    const EmPlayerMajor2Workers *workers;
    EmPlayerMajor2Scene *scene;
} EmPlayerMajor2;

/* 0015B770 state2[] callbacks (EmPlayerStateCallback); context is an
 * EmPlayerMajor2. Each returns 0, or -1 on a fault. */
int em_player_major2_0021E830(void *context, EmPlayerLiveActor *actor);   /* state2[3] */
int em_player_major2_00221FC0(void *context, EmPlayerLiveActor *actor);   /* state2[4] */
int em_player_major2_00222580(void *context, EmPlayerLiveActor *actor);   /* state2[5] */
int em_player_major2_00222AD0(void *context, EmPlayerLiveActor *actor);   /* state2[6] */
int em_player_major2_002230A0(void *context, EmPlayerLiveActor *actor);   /* state2[7] */
int em_player_major2_00225570(void *context, EmPlayerLiveActor *actor);   /* state2[0x16] */
int em_player_major2_002255C0(void *context, EmPlayerLiveActor *actor);   /* state2[0x19] */

/* The entry routines. Each returns the original's return value, 1 (the
 * state was entered) or 0, and -1 only for a NULL argument.
 * 00181110(p, a1): 2/4 and +302 = a1 (callers 001662D0).
 * 00181180(p, a1): 2/5 and +302 = a1 (callers 00168050).
 * 00181D70(p):     2/7 and D_00275B14 (callers 00169730, 0016AE40).
 * 001823E0(p):     2/0x19 (caller 0016DE40). */
int em_player_major2_00181110(EmPlayerLiveActor *actor, int a1);
int em_player_major2_00181180(EmPlayerLiveActor *actor, int a1);
int em_player_major2_00181D70(EmPlayerLiveActor *actor, EmPlayerMajor2Scene *scene);
int em_player_major2_001823E0(EmPlayerLiveActor *actor);

#endif
