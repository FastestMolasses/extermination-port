/* em_save_original.h - the memory-card save writer and reader: the
 * routines that write a saved game to the card and read it back,
 * translated from the original
 * instructions (boot ELF SCUS-97112). Docs: docs/SAVE_GAME.md. Prefix
 * em_save_. The card screen 00227300 (em_card_original) runs it, wired
 * into the options binding em_options_live (docs/SAVE_GAME.md section
 * 6.1), which answers its libmc callees through em_memcard_call
 * (docs/SAVE_GAME.md section 5): live in load mode up to LOAD GAME's read
 * (00228B60), which fail-stops; the save mode is not reachable live.
 *
 * The record is the card screen's, D_00810040 (docs/OPTIONS.md): +3 the
 * writer's state, +4 its step, +5, +0xA the save slot (0..4, the file
 * EX_DATA.0n), +0x48 the card port, +0x4C the card slot, +0x50 / +0x54
 * the Sync command / result cells, +0x64 the file phase of 001FE6B0,
 * +0x68 the slot iterator of 002285A0, +0x6C + n the slot's presence
 * byte, +0x74 + 12n the slot's header (the progress block's bytes 0..5,
 * +6 its byte 0xA, +8 its word 0x50).
 *
 * Hand translations (the decomp's C where it is byte-matched; the
 * instructions where it is NEARMISS):
 *
 *   00228A90(e)   one frame of the save: 001FE9A0's Sync poll gates it,
 *                 then the state e[3]: 0 00228530, 1 002285A0, 2
 *                 002288A0, 3 done (1); else 2 when the card helpers'
 *                 error flag D_00275C58 is set, 0
 *   00228530(e)   the game's directory on the card (GetDir): e[3] = 1
 *                 when there is none, 2 when there is
 *   002285A0(e)   a new directory: Mkdir, the 0x48-byte header file, the
 *                 icon (00229640), icon.sys, then the five slot files (the
 *                 chosen one the live progress block, the others the
 *                 block New Game makes, 001AF220)
 *   002288A0(e)   the chosen slot's file over an existing directory
 *   00229640(e)   the icon file EX.ico from the icon container *D_0028A4A0
 *   00229680(e)   the chosen slot's file from the New Game block (gated by
 *                 the Sync poll; 1 done, 2 error, else 0)
 *   002284F0(p)   the block's check byte: p[0xF] = 1, p[0x63F] = 0x100 -
 *                 (the 8-bit sum of p[0..0x63E] taken before that store)
 *   001AF220()    the New Game block at the scratchpad 0x70000000 (the
 *                 live block kept at 0x70000640 and put back)
 *   001FE6B0(st, port, slot, name, buf, size)  one file: Open (0x203),
 *                 Write, Close, a frame each; 1 done, -1 an error or a
 *                 short write, 0 pending
 *   001FE9E0 / 001FEB90 / 001FEA70 / 001FEDD0 / 001FEC20 / 001FEB00: the libmc
 *                 commands Open / Write / Close / Mkdir / GetDir / Read on
 *                 the card helpers' phase D_00275C5C (2 / 6 / 3 / 0xB / 0xD
 *                 / 5): the first call issues the command (00114360 /
 *                 00114708 / 00114480 / 00114448 / 00114AC0 / 00114630),
 *                 the next takes the result 001FE9A0 kept in D_00275C64
 *
 * The load side (the card screen's):
 *
 *   002283B0(e)   the load enumeration on e[2]: GetDir of the game's
 *                 directory (none: 1 with no slot present), then 00228EB0
 *                 until it reports; 1 done, 2 an error, 3 a bad card
 *   00228EB0(e)   the slot enumeration on e[5]: GetDir of the directory's
 *                 files, the name check 00228C90, each EX_DATA slot read
 *                 (001FE7C0) into D_00822050 and checked (00228C50), its
 *                 record e + 12n + 0x74, then GetInfo (001FECB0)
 *   00228B60(e)   LOAD GAME's read: the slot e[0xA]'s file read straight
 *                 into the live progress block D_00810700
 *   00228C90(n)   the directory holds all eight files (strcmp 00123020)
 *   00228C50(p)   a slot's bytes do not sum to zero (mod 256)
 *   001FE7C0(st, port, slot, name, buf, size)  one file read: Open (1),
 *                 Read, Close, a frame each; 1 done, -1 an error or a
 *                 short read, 0 pending
 *
 * Every other original is a callee reached through EmArea01Ui.call by its
 * original address: the libmc SDK 00114360 / 00114708 / 00114480 / 00114448 / 00114AC0, the
 * copy and string leaves 00121870 (block_copy), 00121A28, 00123168,
 * 00123020 (strcmp), 00122D10 (sprintf) and New Game's progress reset
 * 001AF2C0; the libmc Read 00114630. The card helpers 001FECB0 (GetInfo),
 * 001FE8D0 and the leaf 00225CF0 run as their options translations.
 *
 * The Sync poll 001FE9A0 runs as its translation, em_options_001FE9A0
 * (em_options_original.c), over the SDK's Sync 00114848.
 *
 * Memory, frames, the fail-stop latch: the AREA01 UI lane's
 * (em_area01_ui.h). Oracle: tools/test_save_reference.py. */
#ifndef EM_SAVE_ORIGINAL_H
#define EM_SAVE_ORIGINAL_H

#include <stdint.h>

#include "game/em_area01_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_SAVE_BLOCK 0x00810700u      /* D_00810700: the progress block (0x640 bytes) */
#define EM_SAVE_BLOCK_SIZE 0x640u
#define EM_SAVE_SLOTS 5u               /* EX_DATA.00 .. EX_DATA.04 */

/* All entries: 0, or -1 on a (latched) fault. *result is the original's
 * v0 where it has one (NULL allowed). Register images: an argument the
 * original passes on unchanged is taken as the 64-bit image the caller
 * holds. */
int em_save_00228A90(EmArea01Ui *s, uint32_t e, uint32_t *result);
int em_save_00228530(EmArea01Ui *s, uint32_t e);
int em_save_002285A0(EmArea01Ui *s, uint32_t e);
int em_save_002288A0(EmArea01Ui *s, uint32_t e);
int em_save_00229640(EmArea01Ui *s, uint32_t e, uint32_t *result);
int em_save_00229680(EmArea01Ui *s, uint32_t e, uint32_t *result);
int em_save_002284F0(EmArea01Ui *s, uint32_t p);
int em_save_001AF220(EmArea01Ui *s);
int em_save_001FE6B0(EmArea01Ui *s, uint64_t st, uint64_t port, uint64_t slot, uint64_t name, uint64_t buf,
                     uint64_t size, uint32_t *result);
int em_save_001FE9E0(EmArea01Ui *s, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint32_t *result);
int em_save_001FEB90(EmArea01Ui *s, uint64_t a0, uint64_t a1, uint64_t a2, uint32_t *result);
int em_save_001FEA70(EmArea01Ui *s, uint64_t a0, uint32_t *result);
int em_save_001FEDD0(EmArea01Ui *s, uint64_t a0, uint64_t a1, uint64_t a2, uint32_t *result);
int em_save_001FEC20(EmArea01Ui *s, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t t0,
                     uint64_t t1, uint32_t *result);

/* The load side (docs/SAVE_GAME.md section 4). */
int em_save_002283B0(EmArea01Ui *s, uint32_t e, uint32_t *result);
int em_save_00228EB0(EmArea01Ui *s, uint32_t e, uint32_t *result);
int em_save_00228B60(EmArea01Ui *s, uint32_t e, uint32_t *result);
int em_save_00228C90(EmArea01Ui *s, uint64_t n, uint32_t *result);
int em_save_00228C50(EmArea01Ui *s, uint32_t p, uint32_t *result);
int em_save_001FE7C0(EmArea01Ui *s, uint64_t st, uint64_t port, uint64_t slot, uint64_t name, uint64_t buf,
                     uint64_t size, uint32_t *result);
int em_save_001FEB00(EmArea01Ui *s, uint64_t a0, uint64_t a1, uint64_t a2, uint32_t *result);

#ifdef __cplusplus
}
#endif

#endif /* EM_SAVE_ORIGINAL_H */
