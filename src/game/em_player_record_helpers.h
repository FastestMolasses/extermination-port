/* em_player_record_helpers.h - the player helpers shared by the climb, the
 * slide, the fall, the recovery, the hang and the ladder states, over the
 * raw player record (docs/PLAYER_RECORD_HELPERS.md).
 *
 * The one translation of each of these original routines, not a model of
 * it:
 *   001755B0  the stick-versus-body heading test (the fall's tier-3 edge
 *             path, 0017C580's gait-3 landings): 0x70003A20 = |error|,
 *             1 when it is above pi/2
 *   00177510  the ledge frame from the probe hit it follows (0x70003050
 *             point, 0x70003060 normal, 0x700031E4 heading, 0x70003070 its
 *             yaw matrix)
 *   001775E0  the sweep across the ledge lip
 *   001776E0  the side sweeps around a high ledge
 *   00177CF0  the hand sweeps at a high ledge
 *   0019A180  the column-table entry attribute
 *   0017F320  the hang clearance sweeps (1 when all four are clear)
 *   00188550  the hang row clip D_002754C0[+235 & 1]
 *   00174FD0  the stick quadrant +24C (and +23F, +244, +248, 0x70003A20)
 * em_player_climb.c and em_player_slide.c call these bodies; nothing else
 * translates them.
 *
 * Two forms of each:
 *   - the body (em_player_helper_*), over the ledge frame / values it reads,
 *     with its callees in an EmPlayerHelperCalls table (used by the climb
 *     and the slide over their mirrors);
 *   - the record entry (em_player_record_*), over EmPlayerLiveActor.bytes
 *     with an EmPlayerRecordHelpers context, in the worker-slot shapes of
 *     the other state modules (the comment on each names the slots).
 *
 * Callees that are reused translations (none is re-translated here):
 *   001B1470 angle wrap      em_player_001B1470 (em_player_stage_workers.c),
 *                            bounded as em_script_host_workers.h states: an
 *                            argument with |x| >= 4096.0 faults instead of
 *                            looping
 *   0011DF78 fabsf           em_sdk_math_original_0011DF78
 *   001029C0 / 00102BB0 / 00102918   em_owner_services_original
 *   001026A0 matrix x vector em_effect_original_001026A0
 * The worker callees are 0019AFE0 (the sweep), 0011E620 (atan2f) and
 * 0011DE90 (cosf).
 *
 * Arithmetic: every COP1 operation and compare goes through em_ee_float.h
 * on raw bit patterns (docs/EE_FLOAT_MODEL.md); 001028B8 is the VU0 VADD.xyzw
 * form.
 *
 * Fail-stop: a missing worker, pointer or scratch faults with -1; a worker
 * that returns a negative value faults (-1) at once, leaving the writes made
 * before the call, as the original order leaves them.
 *
 * Oracle: tools/test_player_record_helpers_reference.py executes the
 * original instructions of every routine above on the measured float model
 * (every worker hooked and scripted) and compares all 0x320 record bytes,
 * the scratch words 0x700038A0..AC and 0x70003A20, the ledge frame, the
 * return values and the worker calls with their arguments. */
#ifndef EM_PLAYER_RECORD_HELPERS_H
#define EM_PLAYER_RECORD_HELPERS_H

#include <stdint.h>

#include "game/em_player_floor.h"
#include "game/em_player_fall.h"
#include "game/em_player_recovery.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- The bodies ------------------------------------------------------------
 * Callee table for the bodies. Values cross as raw bit patterns. */
typedef struct EmPlayerHelperCalls {
    void *context;
    /* 0019AFE0(p, from, to, mask): the result (>= 0), or -1 on a fault.
     * *node receives the hit record's +1A halfword when the result is
     * nonzero (00177CF0 reads its low byte). */
    int (*sweep)(void *context, const uint32_t from[4], const uint32_t to[4], unsigned mask,
                 uint16_t *node);
    /* 0011E620(y, x) and 0011DE90(x): 0, or -1 on a fault. */
    int (*atan2)(void *context, uint32_t y, uint32_t x, uint32_t *out);
    int (*cosine)(void *context, uint32_t x, uint32_t *out);
} EmPlayerHelperCalls;

/* 001B1470 through its owner, bounded: -1 for |x| >= 4096.0. */
int em_player_helper_wrap(uint32_t x, uint32_t *out);

/* The SDK call sequences the climb and slide routines make, through the
 * owners named above (each 0, or -1 when em_ee_float.h refuses a form):
 *   apply        001026A0(out, M, v) = v x M
 *   yaw_point    001029C0, 00102BB0(M, M, angle), 00102918(M, M, base)
 *                when base is not NULL, then 001026A0(out, M, local)
 *   ledge_offset 001026A0(t, M, local), 001028B8(out, base, t) (all four
 *                lanes), then out.w = 1.0
 *   trs          build_trs_matrix(out, position, rotation, scale) */
int em_player_helper_apply(const uint32_t matrix[16], const uint32_t v[4], uint32_t out[4]);
int em_player_helper_yaw_point(uint32_t angle, const uint32_t base[3], const uint32_t local[4],
                               uint32_t out[4]);
int em_player_helper_ledge_offset(const uint32_t matrix[16], const uint32_t base[4],
                                  const uint32_t local[4], uint32_t out[4]);
int em_player_helper_trs(float out[16], const float position[3], const float rotation[3],
                         const float scale[3]);

/* 00177510 over the hit's point (0x700031B0) and normal (the node +24).
 * Needs atan2. */
int em_player_helper_00177510(const EmPlayerHelperCalls *c, const float point[3],
                              const float normal[3], EmPlayerRecoveryLedge *ledge);
/* 001775E0(p, y, wide). *result = 0 or 1. s38A0 receives the from vector
 * it leaves at 0x700038A0. Needs sweep. */
int em_player_helper_001775E0(const EmPlayerHelperCalls *c, const EmPlayerRecoveryLedge *ledge,
                              uint32_t y, int wide, uint32_t s38A0[4], int *result);
/* 001776E0(p, y). *result = the hit mask, 0 (a side sweep blocked) or -1
 * (the original's value when neither side is blocked). */
int em_player_helper_001776E0(const EmPlayerHelperCalls *c, const EmPlayerRecoveryLedge *ledge,
                              uint32_t y, uint32_t s38A0[4], int *result);
/* 00177CF0(p, y). *result = 0 or 1. */
int em_player_helper_00177CF0(const EmPlayerHelperCalls *c, const EmPlayerRecoveryLedge *ledge,
                              uint32_t y, uint32_t s38A0[4], int *result);
/* 0019A180(0, index) over the table 0019BC40 built: the sign-extended
 * halfword it returns. */
int em_player_helper_0019A180(const EmPlayerClimbTable *table, int index);
/* 0017F320 over the matrix at +D0. *result = 1 when all four sweeps are
 * clear, else 0. */
int em_player_helper_0017F320(const EmPlayerHelperCalls *c, const uint32_t matrix[16],
                              uint32_t s38A0[4], int *result);
/* 00188550: D_002754C0[row & 1] (the row byte +235). */
int em_player_helper_00188550(uint8_t row);

/* ---- The record entries ---------------------------------------------------
 * What the entries read outside the record, in the binder's canonical
 * storage (pointers, so each read sees the value the original would read at
 * that moment), and the workers they call. */
typedef struct EmPlayerRecordHelpers {
    void *context;
    /* 0019AFE0(p, from, to, mask) over the record: the shape of
     * EmPlayerRecoveryWorkers.sweep. */
    int (*sweep)(void *context, EmPlayerLiveActor *actor, const float from[4], const float to[4],
                 unsigned mask, EmPlayerProbeHit *hit);
    /* 0011E620 atan2f(y, x) and 0011DE90 cosf (em_sdk_math_original's float
     * adapters in the port). */
    float (*atan2)(void *context, float y, float x);
    float (*cosine)(void *context, float x);
    const uint8_t *spad3B8D;   /* 0x70003B8D (00174FD0) */
    const uint8_t *d810E57;    /* pad gait byte (00174FD0) */
    const uint8_t *d810E64;    /* stick X byte (00174FD0) */
    const uint8_t *d810E65;    /* stick Y byte (00174FD0) */
    const uint32_t *d8106A0;   /* camera yaw, raw bits (001755B0) */
    /* 0x700038A0..AC and 0x70003A20: the SAME scratch every other writer
     * and reader is bound to (the fall's EmPlayerLandScratch). */
    EmPlayerLandScratch *scratch;
} EmPlayerRecordHelpers;

/* 001755B0(p): EmPlayerLandWorkers.test_001755B0. Reads +24C (a float), +C4
 * and D_008106A0; writes 0x70003A20. *result = 0 or 1. */
int em_player_record_001755B0(void *helpers, EmPlayerLiveActor *actor, int *result);
/* 0017F320(p): EmPlayerLandWorkers.test_0017F320, EmPlayerHangWorkers.
 * hang_clear, EmPlayerRecoveryWorkers.hang_clear. Writes 0x700038A0..AC. */
int em_player_record_0017F320(void *helpers, EmPlayerLiveActor *actor, int *result);
/* 00188550(p): EmPlayerLandWorkers.pose_clip, EmPlayerHangWorkers /
 * EmPlayerLadderClimbWorkers / EmPlayerRecoveryWorkers hang_row or
 * clip_row, EmPlayerMajor2Workers.w00188550. No worker; helpers may be
 * NULL. */
int em_player_record_00188550(void *helpers, EmPlayerLiveActor *actor, int *clip);
/* 00174FD0(p): EmPlayerHangWorkers / EmPlayerLadderClimbWorkers
 * steer_input, EmPlayerClosure0E18 steer. Writes +23F, +24C, +244, +248
 * and 0x70003A20. */
int em_player_record_00174FD0(void *helpers, EmPlayerLiveActor *actor);
/* 00177510(): EmPlayerRecoveryWorkers.ledge, EmPlayerRunningJumpWorkers.
 * ledge. The hit is the probe it follows. */
int em_player_record_00177510(void *helpers, const EmPlayerProbeHit *hit,
                              EmPlayerRecoveryLedge *ledge);
/* 001775E0(p, wide, y), 001776E0(p, y), 00177CF0(p, y):
 * EmPlayerRecoveryWorkers.lip / .sides / .hands. *result is the original's
 * return value. Each writes 0x700038A0..AC. */
int em_player_record_001775E0(void *helpers, EmPlayerLiveActor *actor,
                              const EmPlayerRecoveryLedge *ledge, int wide, float y, int *result);
int em_player_record_001776E0(void *helpers, EmPlayerLiveActor *actor,
                              const EmPlayerRecoveryLedge *ledge, float y, int *result);
int em_player_record_00177CF0(void *helpers, EmPlayerLiveActor *actor,
                              const EmPlayerRecoveryLedge *ledge, float y, int *result);
/* 0019A180(0, index): EmPlayerRecoveryWorkers.attribute. No worker;
 * helpers may be NULL. */
int em_player_record_0019A180(void *helpers, const EmPlayerClimbTable *table, int index,
                              int *attribute);

#ifdef __cplusplus
}
#endif

#endif
