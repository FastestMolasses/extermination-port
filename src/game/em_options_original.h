/* em_options_original.h - the options screen SELECT opens in gameplay and
 * the memory-card load screen of its load row (and of the title's load
 * entry), translated from the original instructions (boot ELF
 * SCUS-97112). Docs: docs/OPTIONS.md. Prefix em_options_.
 *
 * Hand translations (the decomp's C where it is byte-matched; the
 * instructions where it is NEARMISS or an asm body):
 *
 *   the options screen (0x1AE040 state 2; the gameplay task's record is
 *   *0x70003B6C: +0xC its state, +0xD the sub-state, +0x12, +0x13 the
 *   saved byte, +0x1C the cursor halfword, +0x1E the blink timer)
 *     0022A650()            the screen: nine rows (the action table
 *                           D_002672E0), the row screens below; returns
 *                           1 close, 2 a game loaded, 3 quit, else 0
 *     0022A590()            state 10: screen module 0x2B through the
 *                           loader (001FF080(0, 0x2B)); 1 once loaded
 *     0022AEA0(w, row, open) the list: the title, nine rows and their
 *                           values (D_00810118 = w)
 *     00201720(w, x, colours, actions)  vibration / sound toggle
 *     00201C50(w, x, colours, actions)  the default prompt
 *     00201F70(w)           screen position (0x70003B94 / 96)
 *     00202BA0()            the brightness picture
 *     00202D10(w)           button config (001AF470 on Cross)
 *     0022B420(w, x, colours, actions)  the quit prompt (returns 1 for
 *                           Circle and 2 for Triangle; the decomp's
 *                           NEARMISS C has the two swapped)
 *     001AF6F0()            00121A28(D_00810040, 0, 0xD4)
 *     001AF1C0()            the settings into the progress block, then
 *                           001AF470 (0x1AE040 state 2, r == 1)
 *     001AF150()            the progress block into the settings and the
 *                           screen offset, then 001AF470 (r == 2)
 *   their text (the help container *D_0028A498, group by group)
 *     001FCBD0(x, y, group, line, style)  one line; group 8 splices a
 *                           two-digit hex token into a copy of the line
 *     001FCE30(x, y, group, line, style)  one line; group 8 centred
 *   the memory-card screen (its record D_00810040; +0 state, +1 sub-state,
 *   +0x14 the mode, +0x15 the slot screen's state, +0x16 the result)
 *     00225AC0(mode)        0 load (screen module 0x2A), 1 save
 *     00225720(m)           its frame
 *     00225D20(m)           the slot screen
 *     00226070(m)           the card poll and the slot choice
 *     00225A20(), 00225CF0(m, lo, hi), 002256E0(), 00225700(),
 *     00226010(m), 001FE9A0(mode, cmd, result), 001FECB0(...),
 *     001FE920(), 001FE8D0()  the card helpers over the SDK's
 *                           00114988 (GetInfo) and 00114848 (Sync)
 *
 * Every other original is a callee reached through EmArea01Ui.call by its
 * original address (em_area01_ui.h): the 2D layer 00207D00 / 00207E40 /
 * 00207F80, 0020A7A0, the cues 0020CD40 / 60 / A0, float_to_int 001281C0,
 * the rumble 001B61C0, the loader 001FF080, 001FBC50 / 001FABB0, the fades
 * 001AEE10 / 001AEDE0, 00200970, the string leaves 00121A28, 00122EF0,
 * 00123168, 00123280, 001232E0, 00123418, the text 001FE480, 001CC170,
 * 001CC1E0, 001FC770, 001C5FB0, 001CBA50, the render context 001D2830,
 * the memory card SDK 00114848 / 00114988 and, where the screen goes past
 * the first level's recordings, 002267A0 and 00227300.
 *
 * 001AF470 runs as its one translation, em_slg_001AF470
 * (em_startup_load_gaps.c), over the halfwords 0x70003B74..0x70003B83.
 *
 * Memory, frames, the fail-stop latch and the float model are the AREA01
 * UI lane's (em_area01_ui.h). Oracle: tools/test_options_reference.py. */
#ifndef EM_OPTIONS_ORIGINAL_H
#define EM_OPTIONS_ORIGINAL_H

#include <stdint.h>

#include "game/em_area01_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_OPTIONS_SETTINGS 0x00810118u  /* D_00810118: the settings block */
#define EM_OPTIONS_MC_RECORD 0x00810040u /* D_00810040: the card screen's record (0xD4 bytes) */

/* All entries: 0, or -1 on a (latched) fault. *result is the original's
 * v0 where it has one. */
int em_options_0022A650(EmArea01Ui *s, uint32_t *result);
int em_options_0022A590(EmArea01Ui *s, uint32_t *result);
int em_options_0022AEA0(EmArea01Ui *s, uint32_t w, int32_t row, int32_t open);
int em_options_00201720(EmArea01Ui *s, uint32_t w, int32_t x, uint32_t colours, uint32_t actions,
                        uint32_t *result);
int em_options_00201C50(EmArea01Ui *s, uint32_t w, int32_t x, uint32_t colours, uint32_t actions,
                        uint32_t *result);
int em_options_00201F70(EmArea01Ui *s, uint32_t w, uint32_t *result);
int em_options_00202BA0(EmArea01Ui *s, uint32_t *result);
int em_options_00202D10(EmArea01Ui *s, uint32_t w, uint32_t *result);
int em_options_0022B420(EmArea01Ui *s, uint32_t w, int32_t x, uint32_t colours, uint32_t actions,
                        uint32_t *result);
int em_options_001AF6F0(EmArea01Ui *s);
int em_options_001AF1C0(EmArea01Ui *s);
int em_options_001AF150(EmArea01Ui *s);
int em_options_001AF470(EmArea01Ui *s, uint32_t type);
/* x, y, group and line as the 64-bit register images the caller holds;
 * style is the packed word (low 24 bits the colour, high byte the glyph). */
int em_options_001FCBD0(EmArea01Ui *s, uint64_t x, uint64_t y, uint64_t group, uint64_t line,
                        uint64_t style);
int em_options_001FCE30(EmArea01Ui *s, uint64_t x, uint64_t y, uint64_t group, uint64_t line,
                        uint64_t style);

int em_options_00225AC0(EmArea01Ui *s, uint32_t mode, uint32_t *result);
int em_options_00225720(EmArea01Ui *s, uint32_t m);
int em_options_00225D20(EmArea01Ui *s, uint32_t m, uint32_t *result);
int em_options_00226070(EmArea01Ui *s, uint32_t m, uint32_t *result);
int em_options_00225A20(EmArea01Ui *s);
int em_options_00225CF0(EmArea01Ui *s, uint32_t m, uint32_t lo, uint32_t hi);
int em_options_002256E0(EmArea01Ui *s);
int em_options_00225700(EmArea01Ui *s);
int em_options_00226010(EmArea01Ui *s, uint32_t m, uint32_t *result);
int em_options_001FE9A0(EmArea01Ui *s, uint64_t mode, uint64_t cmd, uint64_t res, uint32_t *result);
/* 001FECB0 passes its five argument registers on to 00114988 unchanged. */
int em_options_001FECB0(EmArea01Ui *s, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t t0,
                        uint32_t *result);
/* 001FE920(a0, a1): a0 / a1 reach 00114988 as they are (port, slot). */
int em_options_001FE920(EmArea01Ui *s, uint64_t a0, uint64_t a1, uint32_t *result);
int em_options_001FE8D0(EmArea01Ui *s);

#ifdef __cplusplus
}
#endif

#endif /* EM_OPTIONS_ORIGINAL_H */
