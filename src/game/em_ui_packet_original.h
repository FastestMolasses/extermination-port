/* em_ui_packet_original.h - the status pages' GS packet builders, byte for
 * byte (docs/STATUS_FIELD.md section 5, step 2). Prefix em_uip_.
 *
 * Hand translations of the original leaf builders the status pages' draw
 * code calls (boot ELF SCUS-97112). Each appends the original's DMA / VIF /
 * GIF packet at a render-context slot cursor (*D_00275670 + 0x10 + 4 *
 * slot) and advances that cursor, writing exactly the bytes the original
 * writes and no others (a byte the original leaves alone keeps whatever the
 * buffer held, as in the original's double-buffered slot memory):
 *   00207D00  blend preset: a REF of 8 quadwords to GS block bank F
 *             (D_00275674 + 0x6A0 / 0x720 / 0x7A0 / 0x820 by mode;
 *             readable C)
 *   00207D90  SCISSOR_1: a CNT packet, one A+D write (byte-matched C)
 *   00207E40  UV sprite: a CNT packet, PRIM 0x156, TEX1_1 5, TEX0_1, two
 *             vertices, TEX1_1 0 (NEARMISS C; the .s followed)
 *   00207F80  flat rectangle: a CNT packet, PRIM 0x146 (byte-matched C)
 *   00208040  flat triangle: a CNT packet, PRIM 0x143, the three vertices'
 *             X / Z through float_to_int 001281C0 (byte-matched C; the
 *             conversion is a worker)
 *   001CC8A0  glyph texel upload (mode 0: the small font, 16 x 16 PSMT4;
 *             mode 1: the tall font, 32 x 32 PSMT4, of which the original
 *             fills 12 x 20 texels) with 001CCE80's transfer header, to GS
 *             block 0x1B00 (NEARMISS C; the .s followed: the header's
 *             arguments are the strip x / y and the mode's size)
 *   001CBC20  small-font strip flush: REFs of D_002511C0 and D_00250FC0,
 *             one UV sprite (NEARMISS C; the .s followed)
 *   001CC3B0  tall-font strip flush: REFs of D_002511C0 and D_002510C0 (or
 *             D_00251140), the five passes as em_message_glyph_cc3b0 packs
 *             their register values (that module's translation)
 *   001CBA50  small-font text run: one 001CC8A0 mode-0 upload per byte,
 *             001CBC20 flushes (NEARMISS C; the .s followed)
 *
 * The REF targets are original addresses (the GS blocks at D_00275674,
 * em_gs_blocks_original; the glyph packets in the ELF's data): the packets
 * carry them as the original's do, and a reader of the chain resolves them.
 *
 * Memory: every byte is reached through EmUipMemory by its ORIGINAL
 * address (the slot cursor word, the packet). The fonts (*D_0028A490 and
 * *D_0028A494, the resource table's slots 0 and 1) and the nibble table
 * D_0026E350 come from the caller.
 *
 * Fail-stop: a builder checks, before its first write, the cursor word, the
 * whole packet range (which must not hold the cursor word), the font bytes
 * it reads and the worker it calls. A failure latches `fault`, writes
 * nothing and returns -1; later calls refuse.
 *
 * Oracle: tools/test_ui_packet_reference.py executes the original
 * instructions over the decomp's fork captures and compares every byte (the
 * bytes the original leaves alone included) and the cursor, and rebuilds
 * the captured status pages' slot-1 chains from their decoded calls.
 *
 * stdint only; no dependency on any port subsystem but the glyph flush's
 * register packing. */
#ifndef EM_UI_PACKET_ORIGINAL_H
#define EM_UI_PACKET_ORIGINAL_H

#include <stddef.h>
#include <stdint.h>

#include "game/em_message_draw_original.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Original data addresses the packets REF (ELF data). */
#define EM_UIP_D_002511C0 0x002511C0u /* the glyph flushes' head packet (3 quadwords) */
#define EM_UIP_D_002510C0 0x002510C0u /* 001CC3B0's sprite packet (8 quadwords)     */
#define EM_UIP_D_00251140 0x00251140u /* 001CC3B0's triangle strip packet (8)       */
#define EM_UIP_D_00250FC0 0x00250FC0u /* 001CBC20's sprite packet (8)               */

/* Bytes each builder advances its cursor by. */
enum {
    EM_UIP_00207D00_BYTES = 0x10,
    EM_UIP_00207D90_BYTES = 0x40,
    EM_UIP_00207E40_BYTES = 0x80,
    EM_UIP_00207F80_BYTES = 0x50,
    EM_UIP_00208040_BYTES = 0x60,
    EM_UIP_001CC8A0_SMALL_BYTES = 0x100,   /* mode 0 */
    EM_UIP_001CC8A0_TALL_BYTES = 0x280,    /* mode 1 */
    EM_UIP_001CBC20_BYTES = 0x80,
    EM_UIP_001CC3B0_SPRITE_BYTES = 0x180,
    EM_UIP_001CC3B0_STRIP_BYTES = 0x220,
    EM_UIP_001CBA50_CELLS = 0x20           /* cells per small strip (0x200 / 16) */
};

typedef struct {
    void *ctx;
    /* Writable bytes of [address, address + size), or NULL when no storage
     * covers the whole range. */
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);
    /* 001281C0 float_to_int(f12) on the raw binary32 bits: *result. 1, or
     * 0 (failure). Needed by 00208040 only. */
    int (*to_int)(void *ctx, uint32_t bits, int32_t *result);
} EmUipMemory;

typedef struct {
    EmUipMemory mem;
    uint32_t context;          /* *D_00275670 (the render context)            */
    uint32_t gs_blocks;        /* D_00275674 (the GS blocks, 0x814220)         */
    const uint16_t *nibbles;   /* D_0026E350: 16 halfwords                     */
    const uint8_t *font_tall;  /* *D_0028A490 (001CC8A0 mode 1)                */
    uint32_t font_tall_size;
    const uint8_t *font_small; /* *D_0028A494 (001CC8A0 mode 0)                */
    uint32_t font_small_size;
    const char *fault;         /* latched; later calls refuse                  */
} EmUip;

/* Binds memory and data and clears the fault. 1, or 0 (NULL). */
int em_uip_init(EmUip *u, const EmUipMemory *mem, uint32_t context, uint32_t gs_blocks);

/* Each: 0, or -1 (the fault latched, nothing written). */
int em_uip_00207D00(EmUip *u, int32_t slot, int32_t mode);
/* 00207D90(slot, x0, y0, x1, y1): SCISSOR_1 = x0 | x1 << 16 | y0 << 32 |
 * y1 << 48, each lane sign-extended before its shift, as the original. */
int em_uip_00207D90(EmUip *u, int32_t slot, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
int em_uip_00207E40(EmUip *u, int32_t slot, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgba,
                    uint64_t tex0);
int em_uip_00207F80(EmUip *u, int32_t slot, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t rgba);
/* 00208040(slot, a, b, c, rgba): a, b, c each the binary32 bits of the
 * vector's +0 (X) and +8 (Z) floats; rgba the low word of the 64-bit
 * argument. */
int em_uip_00208040(EmUip *u, int32_t slot, const uint32_t a[2], const uint32_t b[2], const uint32_t c[2],
                    uint32_t rgba);
/* 001CC8A0(mode, x, y, offset): always slot 1. A mode other than 0 or 1
 * writes nothing and returns 0, as the original. */
int em_uip_001CC8A0(EmUip *u, int32_t mode, int32_t x, int32_t y, int32_t offset);
/* 001CBC20(slot, x, y, w, h, u, v, style): style NULL is the original's 0
 * (colour 0x80808080, no alpha bits). */
int em_uip_001CBC20(EmUip *u, int32_t slot, int32_t x, int32_t y, int32_t w, int32_t h, int32_t uu, int32_t vv,
                    const EmMessageTextStyle *style);
/* 001CC3B0(slot, x, y, u_end, v_end, width, height, style). */
int em_uip_001CC3B0(EmUip *u, int32_t slot, int32_t x, int32_t y, int32_t u_end, int32_t v_end, int32_t width,
                    int32_t height, const EmMessageTextStyle *style);
/* 001CBA50(slot, x, y, advance, height, text, style): the small-font run.
 * 001CCB00 (empty) resets the strip; every byte of the text (its length by
 * 001232E0) uploads one 16 x 16 cell with 001CC8A0(0, column, 0, glyph *
 * 32): a byte below 0x20 the blank glyph 0, '$' (0x24) glyph 0x87, any
 * other byte - 0x20. The column steps by 16 texels and the drawn width by
 * `advance`; at column 0x200 the strip is flushed with 001CBC20(slot, x, y,
 * width, height, 0x200, 0x10, style) and restarted at x + width. The rest
 * is flushed the same way at the end (an empty text flushes a sprite of
 * width 0). `text` must hold its NUL within `avail` bytes. Every packet of
 * the run (the uploads on slot 1's cursor, the flushes on `slot`'s) and
 * every glyph is checked before the first write, so a refusal writes
 * nothing. */
int em_uip_001CBA50(EmUip *u, int32_t slot, int32_t x, int32_t y, int32_t advance, int32_t height,
                    const uint8_t *text, uint32_t avail, const EmMessageTextStyle *style);

/* 001CCB10's GS uploads (the boot's glyph strip page and its CLUT: GS blocks
 * 0x1B00..0x1B80 cleared, the grey ramp at 0x1B67) as one upload chain for
 * em_gfx_gs_upload: its 130 kicks, each 001CCE80's packet and 64 entries
 * (0x17 quadwords) behind a DMA tag (CNT, the last END). Writes
 * EM_UIP_001CCB10_CHAIN_BYTES bytes and returns that, or 0 (cap too small). */
enum {
    EM_UIP_001CCB10_KICKS = 0x1B80 - 0x1B00 + 2,
    EM_UIP_001CCB10_KICK_BYTES = 16 + 0x170,
    EM_UIP_001CCB10_CHAIN_BYTES = EM_UIP_001CCB10_KICKS * EM_UIP_001CCB10_KICK_BYTES
};
size_t em_uip_001CCB10_chain(uint8_t *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif
