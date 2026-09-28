/* em_gs_blocks_original.h - the GS register blocks the boot builder
 * sub_EXTERMINATION (001D0F20, NEARMISS in the decomp; the .s followed)
 * writes at D_00275674 (0x814220 in every capture), and the SDK routines
 * that fill its stack templates (docs/RENDER_CONTEXT.md section 8.3,
 * docs/CHAIN_PAGE.md section 4, docs/LOAD_VEIL_PARTICLES.md section 3).
 *
 * The blocks are DMA data the frame's packets REF (001D1F20, 001D1F80,
 * 001D1FF0, 001D2040, 001CB900, step V's 001D2300): each is a FLUSH /
 * DIRECT qword, an A+D GIF tag and GS register writes.
 *
 *   +0x000  header: words 0, 0, 0, 0x11000000; +0x10 = 0x70000000 (the END
 *           tag step V's list closes on), +0x14 = 0
 *   +0x020  bank A, 2 x 0x190: the two draw environments (FRAME / ZBUF /
 *           XYOFFSET / SCISSOR / PRMODECONT / COLCLAMP / DTHE / TEST of both
 *           contexts, then CLAMP_1 0, COLCLAMP 1, FBA_1 0, PABE 0, SCANMSK
 *           0, TEX1_1 0x60, TEXA 1 << 32), from the templates 00101898 fills
 *   +0x340  bank B, 2 x 0x30: FOGCOL (001D1C50 copies the context's +0xB0
 *           into +0x360 + 0x30 * slot every frame)
 *   +0x3A0  bank C, 2 x 0x80: the clear sprites (001008C0's template, TEST
 *           0x32001 = Z only, 0x30000 = colour and Z)
 *   +0x4A0  bank D, 4 x 0x40: PRIM 0 and CLAMP_1 (CLAMP, REPEAT and two
 *           REGION_CLAMP windows)
 *   +0x5A0  bank E, 4 x 0x40: TEST_1 and ZBUF_1 (ZMSK per variant)
 *   +0x6A0  bank F, 10 x 0x80: the blend presets (PRIM 0x17E, TEX1_1 0x60,
 *           TEST_1, ALPHA_1, CLAMP_1 0, COLCLAMP 1)
 *   +0xBA0  bank G, 4 x 10 x 0x90: the per-pass presets (PRIM 0x100, TEX1_1
 *           0x60, TEST_1 with the pass's Z test, ZBUF_1 with the pass's
 *           ZMSK, ALPHA_1, CLAMP_1 0, COLCLAMP 1)
 *
 * Translated here: 001D0F20's bank loops and header stores; 00101898 (the
 * SDK double-buffer set-up, word asm, decoded from its instructions) for the
 * part the banks read: its two 001006D8 and two 00101630 draw environments,
 * its two 001008C0 clears, its two GIF tags and its frame-pointer patch;
 * 00101630 (hybrid asm: the context-2 draw environment) and 001008C0 (word
 * asm: the clear). 001006D8 and 00100610 are em_load_veil_particles'
 * translations (one owner). Not produced: 00101898's two display
 * environments (001002E0 at +0x00 / +0x28, and its patch of +0x38), which
 * no bank reads, and the rest of 001D0F20 (the arena fill, the flag
 * registrations, 001D25F0 / 001DEDE0, 0021B970 / 0021BA80, the context
 * copies, 001CB5C0: em_rcl_init and the area load, RENDER_CONTEXT.md 8.3).
 *
 * The stack template's stale words: 001006D8 and 00101630 read back three
 * dwords of their block (PRMODECONT, COLCLAMP, DTHE) and change only bit 0.
 * The original's templates sit on the boot stack, so the other bits of
 * those 12 dwords (bank A, env + 0x60 / 0x70 / 0x80 / 0xE0 / 0xF0 / 0x100)
 * are whatever the stack held (in every capture bank A0's context-1 set
 * holds 0xFFFFFFFFA0021BC1, 0x20277401, 0xFFFFFFFFA0000000 and bank A1's
 * context-2 set other stale values); the port's templates start zero. The
 * GS reads only bit 0 of those three registers.
 *
 * Evidence: tools/test_gs_blocks_reference.py (make test-gs-blocks-reference)
 * compares every byte with the bytes the ORIGINAL 001D0F20 writes when
 * executed with 00101898 and its callees running (twice, over two stack
 * fills: exactly those 12 dwords' upper bits follow the stack), and with
 * every route capture outside the words the frame rewrites (bank A's
 * XYOFFSETs, step V; bank B's +0x360 / +0x390, 001D1C50) and the stale
 * stack bits above. */
#ifndef EM_GS_BLOCKS_ORIGINAL_H
#define EM_GS_BLOCKS_ORIGINAL_H

#include <stdint.h>

#define EM_GS_BLOCKS_SIZE           0x2220u  /* D_00275674 + 0x000 .. + 0x221F */
#define EM_GS_BLOCKS_PRESETS_OFFSET 0x6A0u   /* bank F, from D_00275674 */
#define EM_GS_BLOCKS_PRESETS_SIZE   0x500u   /* ten presets of 0x80 bytes */
#define EM_GS_BLOCKS_DBUFF_SIZE     0x330u   /* 00101898's descriptor (001D0F20's sp + 0x80) */

/* Fill bank F alone: `bank` is D_00275674 + 0x6A0, EM_GS_BLOCKS_PRESETS_SIZE
 * bytes. 0, or -1 (NULL). */
int em_gs_blocks_001D0F20_presets(uint8_t *bank);

/* 001008C0(clear, ztest, x, y, w, h, r, g, b, a, z): 0x60 bytes at `clear`
 * (TEST_1 0x30000, PRIM 6, RGBAQ with Q 1.0, XYZ2 (x, y, z) and (x + w,
 * y + h, z) in 12.4, TEST_1 of ztest). The halfword arguments are
 * sign-extended as the original's; r and g take their low byte, b and a are
 * the bytes the caller stored, z the word. 0, or -1 (NULL). */
int em_gs_blocks_001008C0(uint8_t *clear, int32_t ztest, int32_t x, int32_t y, int32_t w, int32_t h,
                          uint32_t r, uint32_t g, uint8_t b, uint8_t a, uint32_t z);

/* 00101630(env, psm, w, h, ztest, zpsm): the context-2 draw environment
 * (FRAME_2, ZBUF_2, XYOFFSET_2, SCISSOR_2, PRMODECONT, COLCLAMP, DTHE,
 * TEST_2): 0x80 bytes at `env`, three of them read back. `d00241010` is the
 * SDK parameter block's first 8 bytes (00100610). 0, or -1. */
int em_gs_blocks_00101630(uint8_t *env, int32_t psm, int32_t w, int32_t h, int32_t ztest,
                          int32_t zpsm, const uint8_t d00241010[8]);

/* 00101898(dbuff, psm, w, h, ztest, zpsm, clear) over the descriptor's
 * bytes +0x50 .. +0x32F (EM_GS_BLOCKS_DBUFF_SIZE bytes at `dbuff`; +0x00 ..
 * +0x4F, the display environments, are left as they are). 0, or -1. */
int em_gs_blocks_00101898(uint8_t *dbuff, int32_t psm, int32_t w, int32_t h, int32_t ztest,
                          int32_t zpsm, int32_t clear, const uint8_t d00241010[8]);

/* The GS blocks of 001D0F20: EM_GS_BLOCKS_SIZE bytes at `blocks` (the
 * address D_00275674 holds), banks A..G and the header, from a zeroed
 * descriptor: 00101898(dbuff, 0, 0x200, 0xE0, 2, 0x31, 1), then its two
 * stores of 0x80000000 at +0x180 / +0x2F0 (the clears' RGBAQ). Bytes the
 * original leaves unwritten (bank D's +0x20..+0x2F, the header's +0x18..
 * +0x1F) keep their value. 0, or -1 (NULL). */
int em_gs_blocks_001D0F20(uint8_t *blocks, const uint8_t d00241010[8]);

#endif
