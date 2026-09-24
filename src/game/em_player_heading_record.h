/* em_player_heading_record.h - 00174AC0, the player's gait latch and body
 * turn toward the stick, over the raw player record (docs/PLAYER_HEADING_RECORD.md).
 *
 * A translation of the original routine, not a model of it. It works on the
 * 0x320-byte record (EmPlayerLiveActor) by its original offsets:
 *   +23F gait byte, +240 gait target speed, +244 / +248 the two stick
 *   cosines, +24C the stick angle, +218 the recorded heading (arg 2),
 *   +C4 facing yaw, +38 scalar speed, +5 player state, +1F0 / +1F1 the
 *   reversal mode and its side, +25D the standing-turn flag.
 * and on the globals the binder points it at: the scratchpad selector byte
 * 0x70003B8D, the pad gait byte D_00810E57, the stick bytes D_00810E64 (X)
 * and D_00810E65 (Y), the camera yaw D_008106A0, and the scratchpad word
 * 0x70003A20 it stores the heading error into (0017C580 reloads that word
 * after calling 00174AC0; docs/PLAYER_FALL.md "The scratch").
 *
 * Callees, all reused translations (none is re-translated here):
 *   0011DE90 cosf, 0011E620 atan2f, 0011DF78 fabsf  em_sdk_math_original.c
 *   001B1470 angle wrap                             em_player_001B1470
 *                                                   (em_player_stage_workers.c)
 *   001B12B0 turn toward                            em_script_host_001B12B0
 *                                                   (em_script_host_workers.c)
 * 001B1470's loop is bounded exactly as em_script_host_workers.h "001B1470
 * domain" states: an argument with |x| >= 4096.0 faults at 0x001B1470
 * instead of spinning. 001B12B0 wraps its own arguments the same way.
 *
 * Fail-stop. The routine checks, before its first write, that the record,
 * the result, every global pointer and the SDK tables and world are bound,
 * and returns -1 otherwise (fault_address = 0x00174AC0). A failing callee
 * stops it with -1 and its address in fault_address; the writes made before
 * the call stay, as the original order leaves them. The atan2f error path
 * (both operands zero, which two float cosines of byte angles never give)
 * is left to the SDK module, which faults at its unbound worker.
 *
 * Arithmetic and float compares go through em_ee_float.h on raw bit
 * patterns, as the COP1 instructions execute (docs/EE_FLOAT_MODEL.md).
 *
 * Oracle: tools/test_player_heading_record_reference.py executes the
 * original instructions (with 0011DE90, 0011E620, 0011DF78, 001B1470 and
 * 001B12B0 running as original code) over synthetic records and over every
 * startup-reference capture and route beat 00..14, and compares all 0x320
 * record bytes, the 0x70003A20 word, every other written byte and the
 * return value. */
#ifndef EM_PLAYER_HEADING_RECORD_H
#define EM_PLAYER_HEADING_RECORD_H

#include <stdint.h>

#include "game/em_player_floor.h"
#include "game/em_sdk_math_original.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The globals 00174AC0 reads and the scratch word it writes, in the
 * binder's canonical storage (pointers, so the routine reads the value the
 * original would read at that moment). */
typedef struct EmPlayerHeadingRecordWorld {
    const uint8_t *spad3B8D;     /* 0x70003B8D: scripted selector (read) */
    const uint8_t *d810E57;      /* pad gait byte 0..3 (001B5CC0's quantizer) */
    const uint8_t *d810E64;      /* stick X byte */
    const uint8_t *d810E65;      /* stick Y byte */
    const uint32_t *d8106A0;     /* camera yaw, raw bits */
    /* 0x70003A20, raw bits: the SAME word every other writer and reader of
     * 0x70003A20 is bound to (EmPlayerLandScratch.s3A20 and the others,
     * docs/PLAYER_HEADING_RECORD.md section 4). */
    uint32_t *spad3A20;
    /* 0011DE90 / 0011E620 (docs/SDK_MATH_ORIGINAL.md). sdk_workers serves
     * only the atan2f error path and may be NULL. */
    const EmSdkMathTables *sdk_tables;
    const EmSdkMathWorld *sdk_world;
    const EmSdkMathWorkers *sdk_workers;
} EmPlayerHeadingRecordWorld;

typedef struct EmPlayerHeadingRecord {
    EmPlayerHeadingRecordWorld world;
    uint32_t fault_address;      /* 0, or the first fault */
} EmPlayerHeadingRecord;

/* 00174AC0(p, arg). *result receives the original v0 (the latched gait
 * byte +23F, or 0 on the two early returns). 0, or -1 on a fault. */
int em_player_heading_record_00174AC0(EmPlayerHeadingRecord *h, EmPlayerLiveActor *actor,
                                      int32_t arg, int32_t *result);

/* Worker-slot adapters (context = EmPlayerHeadingRecord *).
 * The result form fits EmLocoWorkers.heading, EmPlayerLandWorkers.heading,
 * EmPlayerRunningJumpWorkers.heading and EmPlayerWeaponBWorkers.heading;
 * the plain form (return value not read) fits EmPlayerHangWorkers,
 * EmPlayerLadderClimbWorkers, EmPlayerReactionWorkers,
 * EmPlayerRecoveryWorkers and EmPlayerWeaponWorkers .heading. Each returns
 * 0, or -1 on a fault. */
int em_player_heading_record_worker_result(void *context, EmPlayerLiveActor *actor, int arg,
                                           int *result);
int em_player_heading_record_worker(void *context, EmPlayerLiveActor *actor, int arg);

#ifdef __cplusplus
}
#endif

#endif
