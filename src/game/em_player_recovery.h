/* em_player_recovery.h - player helpers shared by the fall, slide, climb and
 * hang states (docs/PLAYER_RECOVERY.md).
 *
 * Translations of the original routines, not models of them:
 *   00178B90  translate along the body yaw (+C4) by +38, optionally running
 *             the wall probes 001764E0 after each step
 *   00178EC0  sideways impulse by the stick quadrant +24C (2 / 3)
 *   001751A0  stick quadrant relative to the camera heading (+24C)
 *   002243F0  hit/damage sub-state machine on +7 (sub-states 0 and 1)
 *   00224B80  hit/damage sub-state machine on +7 (the slide's 00224B80)
 *   0017D080  ledge catch below/behind the feet (00162DB0's fall)
 *   0017C860  ledge grab ahead; on success +5 = 4, +6 = 0, +1F0 = 9
 *   00162A40  the +5 = 4 state callback (0015B130's state[4])
 * and the pure leaves they call, translated inline and executed unhooked by
 * the oracle: 0011DF78 (fabs), float_to_int (001281C0), 001B1470 (angle
 * wrap), 001026A0 (matrix x vector, VU0), 001028B8 (vector add, VU0),
 * 00102948 (quadword copy), 0017D040 (owner test) and 00128350 + 001000C0
 * (the soft-float double compare).
 *
 * Every routine works on the raw 0x320-byte player record (EmPlayerLiveActor,
 * em_player_floor.h) by its original offsets. The routines read and write only
 * `bytes`; the +214/+308 pointer words are never touched. Every other original
 * callee is a worker. A missing worker faults with -1 before any write; a
 * worker that returns a negative value faults (-1), leaving the writes made
 * before the call, as the original order leaves them. Arithmetic is the EE's,
 * bit for bit, through game/em_ee_float.h (EE COP1 and VU0 macro rules).
 *
 * Oracle: tools/test_player_recovery_reference.py executes the original
 * instructions of every routine above (with every worker callee hooked and
 * recorded) and compares all 0x320 record bytes, the scratchpad words below,
 * the return values and the worker call sequence with its arguments. */
#ifndef EM_PLAYER_RECOVERY_H
#define EM_PLAYER_RECOVERY_H

#include <stdint.h>

#include "game/em_player_floor.h"
#include "game/em_player_climb.h"
#include "game/em_player_slide.h"

/* The values the routines read outside the record. D_008106F1 is a pointer
 * at its one canonical byte (00224B80 reads it after its 0021C270 worker,
 * which can set it); the routines that read it refuse (-1, nothing written)
 * without it. */
typedef struct EmPlayerRecoveryScene {
    float camera_yaw;     /* D_008106A0 (001751A0) */
    const uint8_t *d8106F1; /* D_008106F1 (0017C860, 0017D080, 00224B80) */
    uint8_t spad3B8D;     /* 0x70003B8D (001751A0) */
    uint8_t pad_gait;     /* D_00810E57 (001751A0) */
    uint8_t pad_x;        /* D_00810E64 (001751A0) */
    uint8_t pad_y;        /* D_00810E65 (001751A0) */
    uint8_t area;         /* D_00810700 (0017D080) */
} EmPlayerRecoveryScene;

/* The scratchpad words the routines leave for later code (in/out: a routine
 * writes only the words its original writes). */
typedef struct EmPlayerRecoveryScratch {
    float spad3A20;
    float spad3A24;
    float spad3A28;
    float spad3A2C;
} EmPlayerRecoveryScratch;

/* The ledge frame 00177510 publishes from the probe hit it follows:
 * 0x70003050 hit point, 0x70003060 the hit node's normal (+24), 0x700031E4
 * the heading wrap(3pi/2 + atan2(-n.z, n.x)) and 0x70003070 its yaw matrix.
 * The ledge sweeps (001775E0, 001776E0, 00177CF0, 00177B80) read it; none of
 * them writes it (docs/PLAYER_RECOVERY.md). */
typedef struct EmPlayerRecoveryLedge {
    float point[3];       /* 0x70003050 */
    float normal[3];      /* 0x70003060 */
    float heading;        /* 0x700031E4 */
    float matrix[16];     /* 0x70003070 */
} EmPlayerRecoveryLedge;

typedef struct EmPlayerRecoveryWorkers {
    void *context;
    /* SDK transcendental calls: 0011E2A8 sin, 0011DE90 cos, 0011E620
     * atan2(y = $f12, x = $f13), 0011E748 sqrt. */
    float (*sine)(void *context, float x);
    float (*cosine)(void *context, float x);
    float (*atan2)(void *context, float y, float x);
    float (*sqrt)(void *context, float x);
    /* 001764E0(p): the radial wall probes (player_states_wall_probes). */
    int (*probes)(void *context, EmPlayerLiveActor *actor);
    /* 001749A0(p, clip, force, blend $f12). */
    int (*request)(void *context, EmPlayerLiveActor *actor, int clip, int force, float blend);
    /* anim_clip_arbiter(p, clip, blend $f12, frame $f13). */
    int (*arbiter)(void *context, EmPlayerLiveActor *actor, int clip, float blend, float frame);
    /* 001C61D0(p->+40 clip bank, clip): the clip's frame count. */
    int (*clip_frames)(void *context, EmPlayerLiveActor *actor, int clip, int *frames);
    /* 001FBD50(p, id, 0, 300.0) (its handle is not used here). */
    int (*sound)(void *context, EmPlayerLiveActor *actor, unsigned id);
    /* 001B61C0(a0, a1, a2, a3). */
    int (*shake)(void *context, int a0, int a1, int a2, int a3);
    /* 00122BB8: rand(). */
    int (*random)(void *context, uint32_t *value);
    /* The reaction workers of 00224B80 / 002243F0, each (p):
     * 0021C350, 0021C270, 0021C120, 0021D490, and 0021C190 (*result). */
    int (*react_0021C350)(void *context, EmPlayerLiveActor *actor);
    int (*react_0021C270)(void *context, EmPlayerLiveActor *actor);
    int (*react_0021C120)(void *context, EmPlayerLiveActor *actor);
    int (*react_0021C190)(void *context, EmPlayerLiveActor *actor, int *result);
    int (*react_0021D490)(void *context, EmPlayerLiveActor *actor);
    /* 0019AD00(p, target, mask): its return value is the result; the hit
     * fields are what it leaves at 0x700031B0 (point), 0x700031D0 (node
     * record: +1A halfword, +24 normal) and 0x700031D4 (owner, with the
     * owner's +2/+3 bytes). */
    int (*move)(void *context, EmPlayerLiveActor *actor, const float target[4], unsigned mask,
                EmPlayerProbeHit *hit);
    /* 0019AFE0(p, from, to, mask). */
    int (*sweep)(void *context, EmPlayerLiveActor *actor, const float from[4], const float to[4],
                 unsigned mask, EmPlayerProbeHit *hit);
    /* 0019A570(from, to, mask, id) -> *result. It reads only x/y/z. */
    int (*segment)(void *context, const float from[4], const float to[4], unsigned mask, int id,
                   int *result);
    /* 0019BC40(at): the column table (D_70003170 flags, D_700030F0
     * heights, D_00282250 aux, count 0x700031E0). More than
     * EM_PLAYER_CLIMB_TABLE_MAX entries is a fault (the original's arrays
     * alias past 16). */
    int (*table)(void *context, const float at[4], EmPlayerClimbTable *table);
    /* 0019A180(0, index) over the table 0019BC40 just built -> *attribute
     * (the returned halfword, sign-extended). */
    int (*attribute)(void *context, const EmPlayerClimbTable *table, int index, int *attribute);
    /* 00177510(): the ledge frame from the probe hit it follows. */
    int (*ledge)(void *context, const EmPlayerProbeHit *hit, EmPlayerRecoveryLedge *ledge);
    /* 001775E0(p, wide, y), 001776E0(p, y), 00177CF0(p, y), 00177B80(p, y)
     * over the ledge frame: *result is the original's return value (any
     * nonzero value, including 001776E0's -1, rejects). */
    int (*lip)(void *context, EmPlayerLiveActor *actor, const EmPlayerRecoveryLedge *ledge,
               int wide, float y, int *result);
    int (*sides)(void *context, EmPlayerLiveActor *actor, const EmPlayerRecoveryLedge *ledge,
                 float y, int *result);
    int (*hands)(void *context, EmPlayerLiveActor *actor, const EmPlayerRecoveryLedge *ledge,
                 float y, int *result);
    int (*depth)(void *context, EmPlayerLiveActor *actor, const EmPlayerRecoveryLedge *ledge,
                 float y, int *result);
    /* 00162A40's callees: 0017F320(p) (*result), 00188550(p) (*clip),
     * anim_eval_skeleton(p) then node 1's world translation
     * *(D_00275B40 + 4) + C0..CC, 001760C0(p, at, 1, height) (*result),
     * 00174AC0(p, arg), 0017C440(p, arg), 0017C540(p), 00175900(p, search)
     * (*result = its return value) and 001796C0(p). */
    int (*hang_clear)(void *context, EmPlayerLiveActor *actor, int *result);
    int (*hang_row)(void *context, EmPlayerLiveActor *actor, int *clip);
    int (*skeleton)(void *context, EmPlayerLiveActor *actor, float node1[4]);
    int (*column)(void *context, EmPlayerLiveActor *actor, const float at[4], float height,
                  int *result);
    int (*heading)(void *context, EmPlayerLiveActor *actor, int arg);
    int (*reentry)(void *context, EmPlayerLiveActor *actor, int arg);
    int (*handoff)(void *context, EmPlayerLiveActor *actor);
    int (*floor)(void *context, EmPlayerLiveActor *actor, int search, int *result);
    int (*fall)(void *context, EmPlayerLiveActor *actor);
} EmPlayerRecoveryWorkers;

/* 00178B90(p, probe). 0, or -1 on a fault. */
int em_player_recovery_translate(EmPlayerLiveActor *actor, int probe,
                                 const EmPlayerRecoveryWorkers *workers);
/* 00178EC0(p). No workers. -1 (nothing written) when the +24C case needs
 * D_00248730[+23F] and +23F > 3. */
int em_player_recovery_strafe(EmPlayerLiveActor *actor);
/* 001751A0(p). */
int em_player_recovery_stick_quadrant(EmPlayerLiveActor *actor, const EmPlayerRecoveryScene *scene,
                                      EmPlayerRecoveryScratch *scratch,
                                      const EmPlayerRecoveryWorkers *workers);
/* 002243F0(p) -> *result (0 or 1). */
int em_player_recovery_react_002243F0(EmPlayerLiveActor *actor, EmPlayerRecoveryScratch *scratch,
                                      const EmPlayerRecoveryWorkers *workers, int *result);
/* 00224B80(p) -> *result (0, 1 or 2). */
int em_player_recovery_react_00224B80(EmPlayerLiveActor *actor, const EmPlayerRecoveryScene *scene,
                                      const EmPlayerRecoveryWorkers *workers, int *result);
/* 0017D080(p) -> *result (1 caught, else 0). */
int em_player_recovery_ledge_catch(EmPlayerLiveActor *actor, const EmPlayerRecoveryScene *scene,
                                   EmPlayerRecoveryScratch *scratch,
                                   const EmPlayerRecoveryWorkers *workers, int *result);
/* 0017C860(p, reach $f12) -> *result (1 grabbed, else 0). */
int em_player_recovery_ledge_grab(EmPlayerLiveActor *actor, float reach,
                                  const EmPlayerRecoveryScene *scene,
                                  EmPlayerRecoveryScratch *scratch,
                                  const EmPlayerRecoveryWorkers *workers, int *result);
/* 00162A40(p): the +5 = 4 state. 0, or -1 on a fault. */
int em_player_recovery_hang_entry(EmPlayerLiveActor *actor, const EmPlayerRecoveryWorkers *workers);

/* The inline leaves, exported so the oracle can sweep them against the
 * original instructions directly (raw binary32 words):
 *   float_to_int (001281C0);
 *   001B1470 (0, or -1 where the original's loop can never end);
 *   00128350 + 001000C0(d, 0x3FFE28C740000000): 1 when x < 0.6 pi. */
int32_t em_player_recovery_float_to_int(uint32_t x);
int em_player_recovery_wrap(uint32_t angle, uint32_t *out);
int em_player_recovery_below_0_6pi(uint32_t x);

/* ---- Binding (docs/PLAYER_RECOVERY.md "Binding") -------------------------
 * One context for the adapters below: the workers, the record the slide and
 * climb mirrors stand for (player_states_actor_mut()), the scene provider and
 * the scratch words kept between calls. */
typedef struct EmPlayerRecoveryLive {
    EmPlayerRecoveryWorkers workers;
    EmPlayerLiveActor *live;
    int (*scene)(void *context, EmPlayerRecoveryScene *scene);
    void *scene_context;
    EmPlayerRecoveryScratch scratch;
    /* Optional: another module's 0x70003A20 word (raw bits, e.g.
     * em_player_fall.h EmPlayerLandScratch.s3A20). When set, the adapters
     * load scratch.spad3A20 from it before a routine and store it back after,
     * so every writer of 0x70003A20 shares one word as in the original. */
    uint32_t *shared3A20;
} EmPlayerRecoveryLive;

/* EmPlayerStatesBinding.stage.state[4] = em_player_recovery_state4 with an
 * EmPlayerRecoveryLive context (00162A40 over the live record). */
int em_player_recovery_state4(void *context, EmPlayerLiveActor *actor);
/* EmPlayerSlideWorkers.translate (00178B90) and .damage (00224B80), and
 * EmPlayerClimbWorkers.translate (00178B90), with an EmPlayerRecoveryLive
 * context: the mirror is written into `live`, the routine runs on the record,
 * and the mirror is read back (the climb's link_kind is kept). */
int em_player_recovery_slide_translate(void *context, EmPlayerSlideActor *actor, int arg);
int em_player_recovery_slide_damage(void *context, EmPlayerSlideActor *actor, int *result);
int em_player_recovery_climb_translate(void *context, EmPlayerClimbActor *actor, int arg);
/* Worker-shaped adapters for the other state modules' worker tables
 * (em_player_fall.h, em_player_hang.h, em_player_reaction.h,
 * em_player_major2.h, ...), each with an EmPlayerRecoveryLive context:
 * 00178B90(p, arg), 00178EC0(p), 001751A0(p), 002243F0(p), 00224B80(p),
 * 0017D080(p) and 0017C860(p, reach) with the raw reach word. The routines
 * that read the scene call `scene` first (a missing one faults before any
 * write). */
int em_player_recovery_translate_worker(void *context, EmPlayerLiveActor *actor, int arg);
int em_player_recovery_strafe_worker(void *context, EmPlayerLiveActor *actor);
int em_player_recovery_stick_quadrant_worker(void *context, EmPlayerLiveActor *actor);
int em_player_recovery_react_002243F0_worker(void *context, EmPlayerLiveActor *actor, int *result);
int em_player_recovery_react_00224B80_worker(void *context, EmPlayerLiveActor *actor, int *result);
int em_player_recovery_ledge_catch_worker(void *context, EmPlayerLiveActor *actor, int *result);
int em_player_recovery_ledge_grab_worker(void *context, EmPlayerLiveActor *actor, uint32_t reach,
                                         int *result);

#endif
