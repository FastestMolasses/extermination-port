/* em_gs_blocks_original.h - the blend-preset bank the boot builder
 * sub_EXTERMINATION (001D0F20, NEARMISS in the decomp; the .s followed)
 * writes at D_00275674 + 0x6A0 (docs/CHAIN_PAGE.md section 4).
 *
 * 001D0F20 builds seven banks of GS packets at boot. The chain page
 * references one of them: 001CB900(table, id, mode) appends a REF of
 * 001CB9B0(mode) = D_00275674 + 0x6A0 + 0x80 * mode (modes 0..4), and the
 * DMA sends that preset to VIF1 with the drawing after it. This module is
 * the loop that fills that bank, ten 0x80-byte presets (i = 0..9):
 *
 *   +0x00  words 0, 0, 0x11000000 (FLUSH), 0x50000007 (DIRECT 7)
 *   +0x10  GIF tag: NLOOP 6, EOP, NREG 1, REGS A+D
 *   +0x20  A+D PRIM 0x17E
 *   +0x30  A+D TEX1_1 0x60
 *   +0x40  A+D TEST_1  (per preset)
 *   +0x50  A+D ALPHA_1 (per preset)
 *   +0x60  A+D CLAMP_1 0
 *   +0x70  A+D COLCLAMP 1
 *
 * with (TEST_1, ALPHA_1) for i = 0..9: (0x5000D, 0x80000000A8),
 * (0x53001, 0x44), (0x53001, 0x8000000068), (0x5000D, 0x8000000062),
 * (0x53001, 0x49), (0x5C00D, 0x49), (0x5C00D, 0x44), (0x50003,
 * 0x80000000A8), (0x52001, 0x80000000A8), (0x51001, 0x80000000A9).
 * Every byte of the bank is written; the rest of 001D0F20 (the arena fill,
 * the other six banks, the video set-up) is not translated.
 *
 * Evidence: tools/test_chain_page_reference.py compares the bank with every
 * route capture's (written once at boot, never after) and, in full mode,
 * with the bytes the ORIGINAL 001D0F20 writes when executed. */
#ifndef EM_GS_BLOCKS_ORIGINAL_H
#define EM_GS_BLOCKS_ORIGINAL_H

#include <stdint.h>

#define EM_GS_BLOCKS_PRESETS_OFFSET 0x6A0u   /* from D_00275674 */
#define EM_GS_BLOCKS_PRESETS_SIZE   0x500u   /* ten presets of 0x80 bytes */

/* Fill the bank: `bank` is D_00275674 + 0x6A0, EM_GS_BLOCKS_PRESETS_SIZE
 * bytes. 0, or -1 (NULL). */
int em_gs_blocks_001D0F20_presets(uint8_t *bank);

#endif
