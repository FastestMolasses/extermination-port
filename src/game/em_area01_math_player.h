/* em_area01_math_player.h - AREA01 lane "math": three player-record
 * routines first reached on the AREA01 route (docs/AREA01_MATH.md).
 *
 * Hand translations of these original functions (boot ELF SCUS-97112):
 *   00183250  one frame of a timed player sub-machine on the player record
 *             (byte +6 phase, half +0x28 timer): phase 0 requests a clip
 *             (0017B490 then 001749A0) and arms the timer at 50; phases 1
 *             and 2 count down (30 more frames each), phase 2 and 3 move the
 *             record (00178B90); phase 3 decays +0x38 by 0.01137 and, when it
 *             goes negative, requests 00174A50(p, 12.0); phase 3 at timer 0
 *             returns the record to state 1/0 and clears the scratchpad byte
 *             0x70003B8D. Every phase ends with +0xB4 += -0.2 and
 *             00175900(p, 1). Read from the original instructions (the
 *             decomp has it as an asm-word body).
 *   00187DE0  copies SPR 0x700031B0 to 0x700038B0, starts effect 0x80000016
 *             there (001EFD90 with the record's +0xC0), 001E8B90(0x700038B0,
 *             5.0) unless the area byte D_00810700 is 0x15, then sound 0xCA
 *             (record byte +0x23C == 1) or 0xDB through 001FB9F0 with
 *             0x1000, 0x1000, 0x1000 (byte-matched C)
 *   00187EC0  D_008102BB = 1, D_008104EA = a0, D_008105CE = a1 (low bytes)
 *             (byte-matched C)
 * Each returns 0, or -1 when a fault is latched (em_area01_math_core.h). */
#ifndef EM_AREA01_MATH_PLAYER_H
#define EM_AREA01_MATH_PLAYER_H

#include <stdint.h>

#include "game/em_area01_math_core.h"

#ifdef __cplusplus
extern "C" {
#endif

int em_area01_math_00183250(EmA01Math *m, uint32_t record);
int em_area01_math_00187DE0(EmA01Math *m, uint32_t record);
int em_area01_math_00187EC0(EmA01Math *m, uint32_t a0, uint32_t a1);

#ifdef __cplusplus
}
#endif

#endif
