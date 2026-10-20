/* em_card_original.h - the memory-card screen past the slot choice: the
 * card check, the slot list, the confirmation, the save or load it runs
 * and the card's error screens, translated from the original instructions
 * (boot ELF SCUS-97112). Docs: docs/SAVE_GAME.md section 6. Prefix
 * em_card_. Not wired yet: the options binding (em_options_live) still
 * faults at 002267A0 and 00227300.
 *
 * The record is the card screen's, D_00810040 (docs/OPTIONS.md, SAVE_GAME.md
 * section 2.2): +1 the screen (0 the card check, 1 the load enumeration, 2
 * the slot list, 4 an error screen), +2 its step, +3 / +4 a step's state,
 * +7 the card poll's alternation, +9 the Yes / No cursor, +0xA the chosen
 * slot, +0x10 a frame counter, +0x14 the mode (1 save, 2 load), +0x15 the
 * slot screen's state (00225D20), +0x20 the card check's retry count,
 * +0x28 / +0x2C the two ports' card flags, +0x48 the chosen port, +0x4C the
 * card slot, +0x50 / +0x54 the Sync cells, +0x58 / +0x5C / +0x60
 * GetInfo's type / free / format words, +0x6C + n slot n's presence byte
 * (0 empty, 1 saved, 2 unusable) and +0x74 + 12 n its record.
 *
 * Hand translations (the decomp's C where it is byte-matched; the
 * instructions for the NEARMISS 002280F0, 00228320, 002293B0, 002267A0 and
 * the asm body 00227C60):
 *
 *   00227300(m)   the screen on m[1]: the port's title line, then 0 the
 *                 card check 002280F0, 1 the load enumeration 002283B0, 2
 *                 the slot list (002293B0) on m[2]: 0 the cursor 00229170,
 *                 1 the confirmation 00227980, 2 the save 00228A90 (save
 *                 mode; 00229680 over an unusable slot) or LOAD GAME's read
 *                 00228B60, 3 the done message 00227010, 4 the failure
 *                 message 00227160; 4 the error screens on m[2]: 00226E90,
 *                 00226B90, 002267A0, 00226D50. Returns 1 back to the
 *                 card choice, 2 out, 3 done (a game saved or loaded)
 *   002280F0(m)   the card check on m[2]: GetInfo (the format word), the
 *                 game's directory (GetDir), GetInfo with the free count
 *   00228320(m)   the free count covers a new directory
 *   002293B0(m)   the five slot rows: number, place and play time
 *   00229170(m)   the slot cursor (Up / Down, Cross, Circle, Triangle)
 *   00227980(m)   the Yes / No confirmation
 *   00227010(m)   the done message, 45 frames
 *   00227160(m)   the failure message
 *   00226E90(m)   error screen 0 (the card check's 2: the format word
 *                 never 1): save mode the format prompt 00227C60, load
 *                 mode the slot list and the cursor again
 *   00227C60(m)   the format step on m[3]: 00227DE0, the format 00228070,
 *                 00227270
 *   00227DE0(m)   the format prompt and its Yes / No
 *   00228070(m)   the format command 001FED40, gated by the Sync poll
 *   001FED40(port, slot)  the libmc Format command (00114D18) on the card
 *                 helpers' phase D_00275C5C = 0x10
 *   00227270()    the failed format's message
 *   00226B90(m)   error screen 1 (the card check's 3: the free count short)
 *   002267A0(m)   error screen 2 (the card check's 4 or the enumeration's
 *                 2): the two ports polled in turn for a card of type 2
 *   00226D50(m)   error screen 3 (the enumeration's 3: the card refused)
 *
 * Callees run as their translations: 001FCBD0, 001FE9A0, 001FECB0,
 * 001FE8D0, 00225CF0, 00225A20, 00226010, 002256E0, 00225700
 * (em_options_original), 002283B0, 00228A90, 00229680, 00228B60, 001FEC20
 * (em_save_original). Every other original is reached through
 * EmArea01Ui.call by its address: the cues 0020CD40 / 60 / A0, the text
 * 001CC1E0 / 001FC770, sprintf 00122D10, 00121A28, the libmc SDK.
 *
 * Memory, frames, the fail-stop latch: the AREA01 UI lane's
 * (em_area01_ui.h). Oracle: tools/test_card_screen_reference.py. */
#ifndef EM_CARD_ORIGINAL_H
#define EM_CARD_ORIGINAL_H

#include <stdint.h>

#include "game/em_area01_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* All entries: 0, or -1 on a (latched) fault. *result is the original's
 * v0 where it has one (NULL allowed). */
int em_card_00227300(EmArea01Ui *s, uint32_t m, uint32_t *result);
int em_card_002280F0(EmArea01Ui *s, uint32_t m, uint32_t *result);
int em_card_00228320(EmArea01Ui *s, uint32_t m, uint32_t *result);
int em_card_002293B0(EmArea01Ui *s, uint32_t m);
int em_card_00229170(EmArea01Ui *s, uint32_t m, uint32_t *result);
int em_card_00227980(EmArea01Ui *s, uint32_t m, uint32_t *result);
int em_card_00227010(EmArea01Ui *s, uint32_t m, uint32_t *result);
int em_card_00227160(EmArea01Ui *s, uint32_t m, uint32_t *result);
int em_card_00226E90(EmArea01Ui *s, uint32_t m, uint32_t *result);
int em_card_00227C60(EmArea01Ui *s, uint32_t m, uint32_t *result);
int em_card_00227DE0(EmArea01Ui *s, uint32_t m, uint32_t *result);
int em_card_00228070(EmArea01Ui *s, uint32_t m, uint32_t *result);
/* 001FED40 passes its argument registers on to 00114D18 unchanged. */
int em_card_001FED40(EmArea01Ui *s, uint64_t a0, uint64_t a1, uint32_t *result);
int em_card_00227270(EmArea01Ui *s, uint32_t *result);
int em_card_00226B90(EmArea01Ui *s, uint32_t m, uint32_t *result);
int em_card_002267A0(EmArea01Ui *s, uint32_t m, uint32_t *result);
int em_card_00226D50(EmArea01Ui *s, uint32_t m, uint32_t *result);

#ifdef __cplusplus
}
#endif

#endif /* EM_CARD_ORIGINAL_H */
