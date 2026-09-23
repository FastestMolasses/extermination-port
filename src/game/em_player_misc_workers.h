/* em_player_misc_workers.h - player worker routines the hang, recovery,
 * major2, reaction, fall, ladder and closure translations name but did not
 * translate (docs/PLAYER_MISC_WORKERS.md).
 *
 * Translations of the original routines, not models of them:
 *   00122BB8  rand: the one shared LCG state (em_random.c), no second copy
 *   001FBD50  play a sound at an object (001FBF50, then 001FB9F0)
 *   001FBF50  positional gain: distance fade and camera pan, both channels
 *   001B15D0  distance (001028D0 into 0x70003600, then 0011E748), for 001FBF50
 *   00182250  hang aim track: snap +B0/+B8/+C4 to the ledge the probe hit
 *   0017E250  ledge-ahead probe (001760C0 column, then three 0019AFE0 sweeps)
 *   0017E510  ledge-above probe (three 001760C0 columns)
 *   0017E7C0  hang side probe: returns 0, 1, 2 or 0xA (+1F1, +D, +2E0..)
 *   0017DF70, 0017DFB0, 0017E0D0, 0017E150, 0017E1D0  side clip requests
 *   0017FF80  clip request from 00188570 / 00188590 by +2F1
 *   00182AF0  sound 00179B90() + 0x100 at range 300
 *   00177B80  ledge depth probe (two 0019AB20 ground probes)
 *   0021E650  the +7 countdown of +3C (rumble, sounds)
 *   0015C1F0  player model kind (+2FF), 001CA6E0 bind, +C, +96, 00200890
 *   001EFE00  effect spawned at the actor (001EF9D0), linked to it
 * Inline: 00102948 (quadword copy), 001031E0 (three-word copy), 0011DF78
 * (sign-bit clear). Reused directly: 001281C0 float_to_int
 * (em_player_float_to_int, em_player_stage_workers.c).
 *
 * Not translated here because another lane already did: 0021C120, 0021C190
 * and 0021D490 are em_player_reaction.c's (em_player_reaction_w0021C120 /
 * _w0021C190 / _w0021D490); this module supplies their untranslated callees
 * 001FBD50, 001EFE00 and 0015C1F0.
 *
 * Every routine works on the raw 0x320-byte player record
 * (EmPlayerLiveActor) by original offsets. Every original callee that is not
 * translated here is an explicit worker of EmPlayerMiscWorkers; a routine
 * checks, before its first write, that every worker it can reach is bound
 * (and the scene / scratch it reads), and returns -1 otherwise. A worker
 * that returns a negative value stops the routine with -1, leaving the
 * writes made before the call, as the original order leaves them. EE COP1
 * arithmetic goes through em_ee_float.h (docs/EE_FLOAT_MODEL.md); values the
 * original only moves are copied as bits.
 *
 * Oracle: tools/test_player_misc_workers_reference.py executes the original
 * instructions (COP1/VU0 through tools/ee_float_model.py) with every callee
 * that is a worker here hooked, and compares all 0x320 record bytes, the
 * scratchpad words, the outputs and every worker call. */
#ifndef EM_PLAYER_MISC_WORKERS_H
#define EM_PLAYER_MISC_WORKERS_H

#include <stdint.h>

#include "game/em_player_floor.h"
#include "game/em_player_recovery.h"

/* The EE scratchpad temporaries these routines write and pass to workers
 * (and read back). Raw words in float storage; the routines copy them as
 * bits. Shared with whatever else uses the same scratch in the frame. */
typedef struct EmPlayerMiscScratch {
    float s3400[16];   /* 0x70003400: 001FBF50's yaw matrix */
    float s3600[4];    /* 0x70003600: 001FBF50 (listener, then the facing vector) */
    float s3610[4];    /* 0x70003610: 001FBF50 (source, then the eye->source direction) */
    /* 0x700036A0: the actor matrix of 00182250 / 0017E250 / 0017E510. Its
     * row 3 (+0x30, i.e. 0x700036D0) is also the point 0017E250 / 0017E510
     * copy there (001031E0: x, y, z; w left as it is), which makes that point
     * the matrix translation for the transforms that follow. */
    float s36A0[16];
    float s38A0[4];    /* 0x700038A0 */
    float s38B0[4];    /* 0x700038B0 */
    float s38C0[4];    /* 0x700038C0 */
    float s38D0[4];    /* 0x700038D0 */
    float s38E0[4];    /* 0x700038E0 (0017E7C0) */
    float s38F0[4];    /* 0x700038F0 (0017E7C0) */
    float s3900[4];    /* 0x70003900 (0017E7C0) */
    float s3910[4];    /* 0x70003910 (0017E7C0) */
    float s3A20;       /* 0x70003A20: 00182250 stores the atan2 there */
    float s3A24;       /* 0x70003A24: 00182250 stores the wrapped yaw error */
} EmPlayerMiscScratch;

/* Globals the routines read, at the moment the original reads them (the
 * binder keeps the struct current; the routines never write it). */
typedef struct EmPlayerMiscScene {
    uint8_t d810700;        /* D_00810700 area (0017E7C0) */
    uint8_t d810701;        /* D_00810701 (0017E7C0: area 8 room 3 box) */
    uint8_t d810C60;        /* D_00810C60 (0015C1F0 kind select) */
    uint8_t d28215B;        /* D_0028215B: 1 = mono sound option (001FBF50) */
    float d810360[4];       /* D_00810360: the listener quad (the player position) */
    float d81027C;          /* D_0081027C: the camera yaw */
    float d8105D0[4];       /* D_008105D0: the camera eye */
    /* D_0028A490: the model handle table 0015C1F0 indexes by +2FF. An index
     * at or beyond the count faults (the original would read past it). */
    const uint32_t *d28A490;
    uint32_t d28A490_count;
} EmPlayerMiscScene;

/* The bytes of an effect record 001EFE00 writes after 001EF9D0 returned it
 * (the spawn worker points these at the record's own storage). */
typedef struct EmPlayerMiscEffectView {
    uint32_t *w24;          /* +0x24: the actor's +0x14 word */
    float *pos;             /* +0xB0..+0xBF: the actor's +0xB0 quadword */
    float *rot;             /* +0xC0..+0xCF: the actor's +0xC0 quadword */
} EmPlayerMiscEffectView;

/* The original callees that are not translated here. Each returns 0, or a
 * negative value on a fault; *result is the original's return value. */
typedef struct EmPlayerMiscWorkers {
    void *context;

    /* ---- SDK VU0 leaves (out may alias an input) ---- */
    int (*identity)(void *context, float m[16]);                                   /* 001029C0 */
    /* 00102C58(out, in, angles): 00102A60(z), 00102BB0(y), 00102B08(x). */
    int (*euler)(void *context, float out[16], const float in[16], const float angles[4]);
    /* 00102918(out, in, v): rows 0..2 copied, row 3 xyz += v. */
    int (*translate)(void *context, float out[16], const float in[16], const float v[4]);
    int (*transform)(void *context, float out[4], const float m[16], const float v[4]); /* 001026A0 */
    int (*vadd)(void *context, float out[4], const float a[4], const float b[4]);  /* 001028B8 */
    int (*vsub)(void *context, float out[4], const float a[4], const float b[4]);  /* 001028D0 */
    int (*normalize)(void *context, float out[4], const float in[4]);              /* 00102760 */
    int (*dot)(void *context, const float a[4], const float b[4], float *result);  /* 00102738 */

    /* ---- SDK scalars ---- */
    int (*sine)(void *context, float x, float *result);                            /* 0011E2A8 */
    int (*atan2)(void *context, float y, float x, float *result);                  /* 0011E620 */
    int (*sqrt)(void *context, float x, float *result);                            /* 0011E748 */
    int (*wrap)(void *context, float x, float *result);                            /* 001B1470 */
    /* 001B1380(from, to, yaw): 1 when wrap(atan2(from.x - to.x, from.z -
     * to.z) - yaw) >= 0. The em_script_host_w_001B1380 shape. */
    int (*side)(void *context, const float from[4], const float to[4], float yaw,
                int32_t *result);

    /* ---- sound ---- */
    /* 001FB9F0(id, a1, a2, a3): the submit; *result is its v0. */
    int (*submit)(void *context, int32_t id, int32_t a1, int32_t a2, int32_t a3, int32_t *result);
    int (*sound_base)(void *context, EmPlayerLiveActor *actor, int32_t *base);      /* 00179B90 */

    /* ---- clips ---- */
    /* 001749A0(p, clip, force, blend). */
    int (*request)(void *context, EmPlayerLiveActor *actor, int32_t clip, int32_t force, float blend);
    int (*clip_2F1_0)(void *context, EmPlayerLiveActor *actor, int32_t *clip);      /* 00188570 */
    int (*clip_2F1_1)(void *context, EmPlayerLiveActor *actor, int32_t *clip);      /* 00188590 */
    /* 0017F1C0(p): the probe ahead of the actor (0019AD00 mask 7). */
    int (*ahead)(void *context, EmPlayerLiveActor *actor, int32_t *result);

    /* ---- collision (each may leave the hit record the readers see) ---- */
    /* 0019AD00(p, target, mask). */
    int (*move)(void *context, EmPlayerLiveActor *actor, const float target[4], uint32_t mask,
                int32_t *result);
    /* 0019AFE0(p, from, to, mask). */
    int (*sweep)(void *context, EmPlayerLiveActor *actor, const float from[4], const float to[4],
                 uint32_t mask, int32_t *result);
    /* 001760C0(p, at, arg, height). */
    int (*column)(void *context, EmPlayerLiveActor *actor, const float at[4], int32_t arg,
                  float height, int32_t *result);
    /* 0019AB20(p, at, p + 0x280, mask): it writes the record's +280.. itself. */
    int (*ground)(void *context, EmPlayerLiveActor *actor, const float at[4], uint32_t mask,
                  int32_t *result);
    /* The hit the last probe left: the word / byte at *(0x700031D0) +
     * offset (the node record) and the word at 0x700031B0 + offset. */
    int (*hit_node_word)(void *context, uint32_t offset, uint32_t *bits);
    int (*hit_node_byte)(void *context, uint32_t offset, uint8_t *value);
    int (*hit_point_word)(void *context, uint32_t offset, uint32_t *bits);
    /* 0017E6E0(p, side, a, b): the edge probe. */
    int (*edge)(void *context, EmPlayerLiveActor *actor, int32_t side, float a, float b,
                int32_t *result);
    int (*grab)(void *context, EmPlayerLiveActor *actor, int32_t *result);          /* 001782A0 */
    int (*grab_33)(void *context, EmPlayerLiveActor *actor, int32_t *result);       /* 00178440 */
    int (*reach)(void *context, EmPlayerLiveActor *actor, int32_t *result);         /* 001784E0 */
    /* 00178910(p, arg). */
    int (*ledge_top)(void *context, EmPlayerLiveActor *actor, int32_t arg, int32_t *result);
    /* 0017F130(p, side): side is the a1 the original leaves for 0017E6E0. */
    int (*blocked)(void *context, EmPlayerLiveActor *actor, int32_t side, int32_t *result);

    /* ---- 0021E650 ---- */
    int (*cue)(void *context, int32_t a0, int32_t a1, int32_t a2, int32_t a3);      /* 001B61C0 */
    int (*land_sound)(void *context, EmPlayerLiveActor *actor, int32_t tier);       /* 00182870 */
    int (*w0021D490)(void *context, EmPlayerLiveActor *actor);                      /* 0021D490 */

    /* ---- 0015C1F0 ---- */
    /* 001CA6E0(p, handle) = 001CA5E0(p, handle, 0): binds the model; it
     * writes the record's +44 (read after it). */
    int (*bind_model)(void *context, EmPlayerLiveActor *actor, uint32_t handle);
    /* 001C6150(model): the byte at model + 8. `model` is the +44 word. */
    int (*bone_count)(void *context, uint32_t model, uint8_t *count);
    int (*w00200890)(void *context);                                                /* 00200890() */

    /* ---- 001EFE00 ---- */
    /* 001EF9D0(id, pos, f12): *node is its v0 (0: nothing spawned); when it
     * is nonzero, *view must point at the record's +24/+B0/+C0 storage. */
    int (*spawn)(void *context, uint32_t id, const float pos[4], float f12, uint32_t *node,
                 EmPlayerMiscEffectView *view);
} EmPlayerMiscWorkers;

/* The context of every routine and adapter below. */
typedef struct EmPlayerMiscHost {
    const EmPlayerMiscWorkers *workers;
    const EmPlayerMiscScene *scene;
    EmPlayerMiscScratch *scratch;
} EmPlayerMiscHost;

/* ---- The routines: 0, or -1 (missing worker/scene/scratch before any
 * write; a worker fault; a table index outside the table). ---- */

/* 001FBF50(obj, &a, &b, flat, radius, scale): `obj` is the object's +B0
 * quadword (read three times, as the original reads it). *a / *b the
 * channel gains, *result the return value (0 out of range, else 1). */
int em_player_misc_001FBF50(EmPlayerMiscHost *h, const float obj[4], int32_t *a, int32_t *b,
                            int32_t flat, float radius, float scale, int32_t *result);
/* 001FBD50(obj, id, flat, radius): *result = 001FB9F0's v0, or -1 when
 * 001FBF50 returned 0 (out of range). */
int em_player_misc_001FBD50(EmPlayerMiscHost *h, const float obj[4], int32_t id, int32_t flat,
                            float radius, int32_t *result);
/* 001B15D0(a, b): 0x70003600 = a - b, *distance = sqrt(x*x + y*y + z*z). */
int em_player_misc_001B15D0(EmPlayerMiscHost *h, const float a[4], const float b[4],
                            float *distance);
int em_player_misc_00182250(EmPlayerMiscHost *h, EmPlayerLiveActor *actor);
/* 0017E250(p, v): v is the arg1 vector (its x, y, z are read). */
int em_player_misc_0017E250(EmPlayerMiscHost *h, EmPlayerLiveActor *actor, const float v[4],
                            int32_t *result);
int em_player_misc_0017E510(EmPlayerMiscHost *h, EmPlayerLiveActor *actor, int32_t *result);
int em_player_misc_0017E7C0(EmPlayerMiscHost *h, EmPlayerLiveActor *actor, int32_t side,
                            int32_t *result);
int em_player_misc_0017DF70(EmPlayerMiscHost *h, EmPlayerLiveActor *actor, int32_t side, float blend);
int em_player_misc_0017DFB0(EmPlayerMiscHost *h, EmPlayerLiveActor *actor, int32_t side, float blend);
int em_player_misc_0017E0D0(EmPlayerMiscHost *h, EmPlayerLiveActor *actor, int32_t side, float blend);
int em_player_misc_0017E150(EmPlayerMiscHost *h, EmPlayerLiveActor *actor, int32_t side, float blend);
int em_player_misc_0017E1D0(EmPlayerMiscHost *h, EmPlayerLiveActor *actor, int32_t side, float blend);
int em_player_misc_0017FF80(EmPlayerMiscHost *h, EmPlayerLiveActor *actor, float blend);
int em_player_misc_00182AF0(EmPlayerMiscHost *h, EmPlayerLiveActor *actor);
/* 00177B80(p, y) over the ledge frame 00177510 published (0x70003050
 * point, 0x70003060 normal, 0x70003070 matrix). */
int em_player_misc_00177B80(EmPlayerMiscHost *h, EmPlayerLiveActor *actor,
                            const EmPlayerRecoveryLedge *ledge, float y, int32_t *result);
int em_player_misc_0021E650(EmPlayerMiscHost *h, EmPlayerLiveActor *actor);
int em_player_misc_0015C1F0(EmPlayerMiscHost *h, EmPlayerLiveActor *actor);
/* 001EFE00(id, p): *node = the spawned record (0 when none). */
int em_player_misc_001EFE00(EmPlayerMiscHost *h, uint32_t id, EmPlayerLiveActor *actor,
                            uint32_t *node);

/* ---- 00122BB8 over the one shared state (em_random_next) ---- */
int em_player_misc_random(void *context, uint32_t *value);   /* context unused */
int em_player_misc_random_i32(void *context, int32_t *value);

/* ---- Adapters in the consumers' worker shapes (context = EmPlayerMiscHost)
 * Each refuses (-1) before any write when a reachable worker is missing. */
/* 001FBD50(p, id, flags, range) with obj = the record: the `sound` slot of
 * EmPlayerStageCallees / EmPlayerHangWorkers / EmPlayerLadderWorkers /
 * EmPlayerClosureWorkers. The original's v0 is not returned (0 = done). */
int em_player_misc_w_sound(void *context, EmPlayerLiveActor *actor, int id, int flags, float range);
/* 001FBD50(p, id, 0, 300.0): the `sound` slot of EmPlayerReactionWorkers /
 * EmPlayerRecoveryWorkers. */
int em_player_misc_w_sound_300(void *context, EmPlayerLiveActor *actor, unsigned id);
/* The same with an int id: the `sound` slot of EmPlayerLandWorkers
 * (em_player_fall.h). */
int em_player_misc_w_sound_300_i(void *context, EmPlayerLiveActor *actor, int id);
/* The same, *handle = the original's v0 (001FB9F0's value, or -1 out of
 * range): the `sound` slot of EmPlayerWeaponStatesB-style workers
 * (em_player_weapon_states_b.h) that keep the handle. */
int em_player_misc_w_sound_300_handle(void *context, EmPlayerLiveActor *actor, int id,
                                      int *handle);
/* EmEffectOriginalWorkers.w_001FBF50: 001FBF50(scratch, &a, &b, 0, f12, f13)
 * where the scratch's +B0 is `pos`. */
int em_player_misc_w_001FBF50(void *context, const float pos[4], float f12, float f13, int32_t *a,
                              int32_t *b, int32_t *result);
int em_player_misc_w_aim_track(void *context, EmPlayerLiveActor *actor);              /* 00182250 */
/* 0017E250(p, p + B0) (hang) and 0017E250(p, v) (ladder climb). */
int em_player_misc_w_ledge_ahead_self(void *context, EmPlayerLiveActor *actor, int *result);
int em_player_misc_w_ledge_ahead(void *context, EmPlayerLiveActor *actor, const float v[4],
                                 int *result);
int em_player_misc_w_ledge_above(void *context, EmPlayerLiveActor *actor, int *result); /* 0017E510 */
int em_player_misc_w_ledge_side(void *context, EmPlayerLiveActor *actor, int side,
                                int *result);                                          /* 0017E7C0 */
int em_player_misc_w_clip_DF70(void *context, EmPlayerLiveActor *actor, int side, float blend);
int em_player_misc_w_clip_DFB0(void *context, EmPlayerLiveActor *actor, int side, float blend);
int em_player_misc_w_clip_E0D0(void *context, EmPlayerLiveActor *actor, int side, float blend);
int em_player_misc_w_clip_E150(void *context, EmPlayerLiveActor *actor, int side, float blend);
int em_player_misc_w_clip_E1D0(void *context, EmPlayerLiveActor *actor, int side, float blend);
int em_player_misc_w_clip_FF80(void *context, EmPlayerLiveActor *actor, float blend);
int em_player_misc_w_sound_100(void *context, EmPlayerLiveActor *actor);             /* 00182AF0 */
/* EmPlayerRecoveryWorkers.depth (00177B80). */
int em_player_misc_w_depth(void *context, EmPlayerLiveActor *actor,
                           const EmPlayerRecoveryLedge *ledge, float y, int *result);
int em_player_misc_w_0021E650(void *context, EmPlayerLiveActor *actor);
/* 0015C1F0(p): EmPlayerMajor2Workers.w0015C1F0 and
 * EmPlayerReactionWorkers.model_refresh. */
int em_player_misc_w_0015C1F0(void *context, EmPlayerLiveActor *actor);
/* 001EFE00(id, p): EmPlayerStageCallees.w001EFE00 / EmPlayerMajor2Workers
 * shape (result not returned) and EmPlayerReactionWorkers.attach (the
 * record handle in *handle). */
int em_player_misc_w_001EFE00(void *context, uint32_t id, EmPlayerLiveActor *actor);
int em_player_misc_w_attach(void *context, EmPlayerLiveActor *actor, uint32_t id, uint32_t *handle);

#endif
