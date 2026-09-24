/* em_player_reaction.h - the player's +4 = 2 reaction states
 * (docs/PLAYER_REACTION.md).
 *
 * Translations of the original routines, not models of them. 0015B770
 * dispatches these by +5 once the stage worker 0021C440 (or 0021D6C0, which
 * it calls) has entered +4 = 2. The entry conditions below are 0021C440's
 * (+F is its pending request code, +220 health, +224/+22C pending amounts):
 *
 *   +5 0 / 0x17   0021D800  a pending amount, a +23A 6/0x5B or +23B 0xA
 *                           contact, health > 0 (0x17 from +5 0x1D/0x1E)
 *   +5 1          0021E240  health <= 0 with +234 != 1 after a pending
 *                           amount, a contact or +F 6
 *   +5 2 / 0x18   0021E490  a pending amount with +228 >= 100 and D_008106F1
 *   +5 0xA        00223C70  0021D6C0 (a pending amount while 0021D640)
 *   +5 0xB        0021F330  D_0081083C set
 *   +5 0xC        0021F850  +F 1
 *   +5 0xF        002202C0  +F 3 or 5
 *   +5 0x10       0021DBB0  +F 2 outside 0021C440's +1F0 list and 0021D640
 *   +5 0x11       0021E9C0  +F 6 with health > 0
 *   +5 0x12/0x13  0021EAD0  +F 7 (+1F0 != 0x17) / +F 0xA
 *   +5 0x14       0021EF30  +F 0xB
 *
 * The stage worker 0021C440 (EmPlayerStageWorkers.reaction) and 0015D100
 * (.drain) are translated by lane player-stage-workers
 * (em_player_stage_workers.c), as are 0021C270 and 0021C350, which 0021F330
 * calls: they are workers here (w0021C270 / w0021C350).
 *
 * The helpers the states reach (executed by the oracle; the major2 states
 * bind several as their workers, see the adapters below). Translated here:
 *   0017C540 +4 1 with +5 0 or 1 (by +25C)
 *   0021D530 +4 1 with +5 0x1C / 0x1E / 0x1D, or 0017C540
 *   00182870 a sound id by +23A
 * and run from their one translation in em_player_fall.c (the fall lane owns
 * them; em_player_reaction_0021D250 / _0021D2E0 adapt this lane's workers):
 *   0021D250 +4 2 +5 0x16                 0021D2E0 the +7 countdown to 001AEDE0(4, 0)
 *   00179880 the +2EC drop into +B4
 *   0021D490 sound 0x14E / 0x14F by +234  0021C120 +31F = 0x3C, D_008106F0 = 1
 *   0021C190 the +31F countdown           0021D1A0 the +70/+78 bearing vs +C4
 *   0021D600 +1F1 in {1, 3, 4}            001754E0 the +28 input count
 *   001B1470 angle wrap
 *
 * Every routine works on the live player record (EmPlayerLiveActor, the
 * original 0x320-byte layout) by its original offsets. Every original
 * callee not translated here is an explicit worker. A worker that is NULL
 * or returns a negative value is a fault: the routine stops and returns -1,
 * leaving the writes made before the call, as the original order leaves
 * them; the live adapters refuse (-1) before any write when any worker is
 * unbound. Float arithmetic is the EE's (em_ee_float.h, docs/EE_FLOAT_MODEL.md).
 *
 * Oracle: tools/test_player_reaction_reference.py executes the original
 * instructions from the user's pinned ELF and compares all 0x320 actor
 * bytes, the scene bytes and every worker call (order and arguments). */
#ifndef EM_PLAYER_REACTION_H
#define EM_PLAYER_REACTION_H

#include <stdint.h>

#include "game/em_player_fall.h"
#include "game/em_player_floor.h"

/* What the routines read or write outside the actor. */
typedef struct EmPlayerReactionScene {
    /* Read. */
    float root4;          /* *(*D_00275B40 + 4): the root node's vertical translation */
    float root8;          /* *(*D_00275B40 + 8): the root node's forward translation */
    float node2[4];       /* D_00275B40[2] + C0..CC (0021EAD0 / 0021EF30 effect points) */
    float node3[4];       /* D_00275B40[3] + C0..CC */
    float node7[4];       /* D_00275B40[7] + C0..CC */
    uint16_t pad_held;    /* D_00810E70 */
    uint16_t pad_pressed; /* D_00810E74 */
    uint16_t spad3B76;    /* 0x70003B76 (001754E0's button mask) */
    uint16_t spad3B7C;    /* 0x70003B7C (0021D530 / 00223C70) */
    uint16_t spad3B7E;    /* 0x70003B7E */
    uint8_t scripted;     /* 0x70003B8D */
    uint8_t d81083C;      /* D_0081083C (0021C440 enters +5 0xB on it; 0021F330 leaves on 0) */
    /* Read and written: D_008106F1, the one byte every owner shares (the
     * stage's EmPlayerStageScene.d8106F1, which 0021C270 sets and 0021C440
     * reads). 0021C190 clears it; the states read it after w0021C270. */
    uint8_t *d8106F1;
    /* Written. */
    uint8_t d8106F0;      /* D_008106F0 (0021C120 = 1, 0021C190 = 0) */
    uint8_t d8106BC;      /* D_008106BC (0021F330 / 0021F850) */
    int32_t d275B08;      /* (&D_00275B00)[2] (00223C70 sub-state 0x1F = 1) */
} EmPlayerReactionScene;

typedef struct EmPlayerReactionWorkers {
    void *context;
    /* 001749A0(p, clip, force, blend). */
    int (*request)(void *context, EmPlayerLiveActor *actor, int clip, int force, float blend);
    /* anim_clip_arbiter (001749F0)(p, clip, blend, frame). */
    int (*arbiter)(void *context, EmPlayerLiveActor *actor, int clip, float blend, float frame);
    /* 001C61D0(+40, clip): the clip's frame count. */
    int (*clip_frames)(void *context, EmPlayerLiveActor *actor, int clip, int *frames);
    /* 001FBD50(p, id, 0, 300.0). */
    int (*sound)(void *context, EmPlayerLiveActor *actor, unsigned id);
    /* 001B61C0(a, b, c, d): pad vibration. */
    int (*rumble)(void *context, int a, int b, int c, int d);
    /* 00122BB8: the SDK rand() value. */
    int (*random)(void *context, uint32_t *value);
    /* 001EFD90(id, position, rotation): both are the four words at the
     * original's argument pointers. */
    int (*effect)(void *context, uint32_t id, const float position[4], const float rotation[4]);
    /* 001EFE00(id, p): an effect attached at the actor; *handle is its
     * return value (0021C190 stores it in +1C). */
    int (*attach)(void *context, EmPlayerLiveActor *actor, uint32_t id, uint32_t *handle);
    /* 00175900(p, search): the floor service; *result is its return value.
     * player_states_floor_service fits. */
    int (*floor)(void *context, EmPlayerLiveActor *actor, int search, int *result);
    /* 00178B90(p, arg). */
    int (*translate)(void *context, EmPlayerLiveActor *actor, int arg);
    /* 001764E0(p). player_states_wall_probes fits. */
    int (*probes)(void *context, EmPlayerLiveActor *actor);
    /* 00174AC0(p, arg). */
    int (*heading)(void *context, EmPlayerLiveActor *actor, int arg);
    /* anim_eval_skeleton (001C6DA0)(p), then node1 = D_00275B40[1] + C0 / C4
     * / C8 as it left them (0021D2E0 reads +C0 and +C8). */
    int (*skeleton)(void *context, EmPlayerLiveActor *actor, float node1[3]);
    /* 001AEDE0(a0, a1). */
    int (*fade)(void *context, int a0, int a1);
    /* 0015C1F0(p) (0021C190): +2FF, 001CA6E0, +C, +96, 00200890. */
    int (*model_refresh)(void *context, EmPlayerLiveActor *actor);
    /* 001FAFD0() (0021C190). */
    int (*stream_check)(void *context);
    /* SDK 0011E620 atan2(y, x) (0021D1A0). The port and the oracle bind
     * the same host model, as for the floor and slide workers. */
    float (*atan2)(void *context, float y, float x);
    /* 0021C270(p) and 0021C350(p) (0021F330): em_player_0021C270 /
     * em_player_0021C350 of em_player_stage_workers.c fit, with its host as
     * this context. 0021C270 may set D_008106F1 (*scene.d8106F1). */
    int (*w0021C270)(void *context, EmPlayerLiveActor *actor);
    int (*w0021C350)(void *context, EmPlayerLiveActor *actor);
    /* The scratchpad words 0x700038A0.. / 0x70003A20: 0021D2E0 builds its
     * 001EFD90 point at 0x700038A0. The binder's one instance, shared with
     * the fall lane (EmPlayerLandWorkers.scratch). */
    EmPlayerLandScratch *scratch;
} EmPlayerReactionWorkers;

/* ---- The state routines (0015B770's table): 0, or -1 on a fault. ------- */
int em_player_reaction_0021D800(EmPlayerLiveActor *a, EmPlayerReactionScene *s,
                                const EmPlayerReactionWorkers *w);
int em_player_reaction_0021E240(EmPlayerLiveActor *a, EmPlayerReactionScene *s,
                                const EmPlayerReactionWorkers *w);
int em_player_reaction_0021E490(EmPlayerLiveActor *a, EmPlayerReactionScene *s,
                                const EmPlayerReactionWorkers *w);
int em_player_reaction_00223C70(EmPlayerLiveActor *a, EmPlayerReactionScene *s,
                                const EmPlayerReactionWorkers *w);
int em_player_reaction_0021F330(EmPlayerLiveActor *a, EmPlayerReactionScene *s,
                                const EmPlayerReactionWorkers *w);
int em_player_reaction_0021F850(EmPlayerLiveActor *a, EmPlayerReactionScene *s,
                                const EmPlayerReactionWorkers *w);
int em_player_reaction_002202C0(EmPlayerLiveActor *a, EmPlayerReactionScene *s,
                                const EmPlayerReactionWorkers *w);
int em_player_reaction_0021DBB0(EmPlayerLiveActor *a, EmPlayerReactionScene *s,
                                const EmPlayerReactionWorkers *w);
int em_player_reaction_0021E9C0(EmPlayerLiveActor *a, EmPlayerReactionScene *s,
                                const EmPlayerReactionWorkers *w);
int em_player_reaction_0021EAD0(EmPlayerLiveActor *a, EmPlayerReactionScene *s,
                                const EmPlayerReactionWorkers *w);
int em_player_reaction_0021EF30(EmPlayerLiveActor *a, EmPlayerReactionScene *s,
                                const EmPlayerReactionWorkers *w);

/* ---- Helpers (other state routines call them too) ---------------------- */
/* 001B1470. */
float em_player_reaction_wrap(float angle);
/* 0017C540. */
void em_player_reaction_0017C540(EmPlayerLiveActor *a);
/* 0021D530. */
void em_player_reaction_0021D530(EmPlayerLiveActor *a, const EmPlayerReactionScene *s);
/* 0021D600: 1 when +1F1 is 1, 3 or 4. */
int em_player_reaction_0021D600(const EmPlayerLiveActor *a);
/* The rest return 0 or -1 (worker fault); *result where a routine returns
 * a value. 0021D250 and 0021D2E0 run the fall lane's translation
 * (em_player_fall_0021D250 / _0021D2E0) over this lane's workers; 00179880
 * is em_player_fall_drop (em_player_fall.h). */
int em_player_reaction_0021D250(EmPlayerLiveActor *a, int a1, const EmPlayerReactionWorkers *w);
int em_player_reaction_0021D2E0(EmPlayerLiveActor *a, int16_t a1, int a2,
                                const EmPlayerReactionWorkers *w);
int em_player_reaction_00182870(EmPlayerLiveActor *a, int a1, const EmPlayerReactionWorkers *w);
int em_player_reaction_0021D490(EmPlayerLiveActor *a, const EmPlayerReactionWorkers *w);
int em_player_reaction_0021C120(EmPlayerLiveActor *a, EmPlayerReactionScene *s,
                                const EmPlayerReactionWorkers *w);
int em_player_reaction_0021C190(EmPlayerLiveActor *a, EmPlayerReactionScene *s,
                                const EmPlayerReactionWorkers *w, int *result);
int em_player_reaction_0021D1A0(EmPlayerLiveActor *a, const EmPlayerReactionWorkers *w,
                                int *result);
/* scratch: 0x70003A20, which 001754E0 writes (the binder's shared word). */
int em_player_reaction_001754E0(EmPlayerLiveActor *a, const EmPlayerReactionScene *s,
                                EmPlayerLandScratch *scratch, int a1, int *result);
/* ---- The live binding (em_player.c "Live player states") ----------------
 * EmPlayerStatesBinding.stage.state2[+5] = em_player_reaction_live_XXXXXXXX
 * with state2_context[+5] = an EmPlayerReaction:
 *   [0] and [0x17] 0021D800   [1] 0021E240   [2] and [0x18] 0021E490
 *   [0xA] 00223C70   [0xB] 0021F330   [0xC] 0021F850   [0xF] 002202C0
 *   [0x10] 0021DBB0  [0x11] 0021E9C0  [0x12] and [0x13] 0021EAD0
 *   [0x14] 0021EF30
 * `scene` is the binder's; `refresh` fills its read fields (the skeleton's
 * root and node 2/3/7 points as the previous display stage left them, the
 * pad words, the scratchpad bytes and D_0081083C) before every callback, and
 * the binder copies the written fields (D_008106F0, D_008106BC, D_00275B08)
 * back to their owners after it. `scene->d8106F1` points at the one shared
 * D_008106F1 byte (EmPlayerStageScene.d8106F1). Every worker, `scene`,
 * `scene->d8106F1` and `refresh` are required: a missing one faults (-1)
 * before the routine writes anything. */
typedef struct EmPlayerReaction {
    EmPlayerReactionWorkers workers;
    EmPlayerReactionScene *scene;
    int (*refresh)(void *context, EmPlayerReactionScene *scene);
    void *refresh_context;
} EmPlayerReaction;

int em_player_reaction_live_0021D800(void *context, EmPlayerLiveActor *actor);
int em_player_reaction_live_0021E240(void *context, EmPlayerLiveActor *actor);
int em_player_reaction_live_0021E490(void *context, EmPlayerLiveActor *actor);
int em_player_reaction_live_00223C70(void *context, EmPlayerLiveActor *actor);
int em_player_reaction_live_0021F330(void *context, EmPlayerLiveActor *actor);
int em_player_reaction_live_0021F850(void *context, EmPlayerLiveActor *actor);
int em_player_reaction_live_002202C0(void *context, EmPlayerLiveActor *actor);
int em_player_reaction_live_0021DBB0(void *context, EmPlayerLiveActor *actor);
int em_player_reaction_live_0021E9C0(void *context, EmPlayerLiveActor *actor);
int em_player_reaction_live_0021EAD0(void *context, EmPlayerLiveActor *actor);
int em_player_reaction_live_0021EF30(void *context, EmPlayerLiveActor *actor);

/* Adapters with the worker signatures lane player-major2-states declares
 * (EmPlayerMajor2Workers.w0021D2E0 / w0021D250 / w0021D490 / w0021C120 /
 * w0021C190), context = an EmPlayerReaction. Each refreshes the scene first
 * and refuses (-1) before any write when a worker, the scene or the
 * refresh is missing. */
int em_player_reaction_w0021D2E0(void *context, EmPlayerLiveActor *actor, int a1, int a2);
int em_player_reaction_w0021D250(void *context, EmPlayerLiveActor *actor, int a1);
int em_player_reaction_w0021D490(void *context, EmPlayerLiveActor *actor);
int em_player_reaction_w0021C120(void *context, EmPlayerLiveActor *actor);
int em_player_reaction_w0021C190(void *context, EmPlayerLiveActor *actor, int *result);

/* 1 when every worker the state routines use is bound. */
int em_player_reaction_workers_bound(const EmPlayerReactionWorkers *w);

#endif
