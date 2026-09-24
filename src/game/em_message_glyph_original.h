/* Tall-font glyph runs: the draw half below the message line layout.
 * Docs: docs/MESSAGE_GLYPH.md.
 *
 * Hand translation of the original routines the message draw module's
 * `draw_text` and `glyph_advance` workers stand for
 * (em_message_draw_original.h, EmMessageDrawWorkers):
 *
 *   001FC7B0  (NEARMISS; translated from the .s) run splitter: newline
 *             recursion, skipped bytes, 0x81 expansion, fixed-stride run
 *             base, then 001CC1E0 for every run
 *   001CC1E0  (asm in the decomp; translated from the .s) tall-font strip
 *             layout: glyph index, proportional advance, 512-texel strip
 *             wrap, strip flushes
 *   001CBE10  tall-font advance of a byte (byte-matched switch)
 *   001CC3B0  (NEARMISS; translated from the .s) strip flush: the five
 *             outline/fill passes, as the GS register values it packs
 *   001CCB00  empty (the strip reset is 001CC1E0's own zeroing)
 *
 * Boundaries (workers, fail-stop):
 *   001CC8A0(1, x, y, glyph * 30): the glyph texel upload into the strip at
 *     GS block 0x1B00. The native side reports (x, y, byte offset); the
 *     texels are the port's glyph atlas (the user's own font export).
 *   the GS: the flush worker receives every register value 001CC3B0 packs
 *     (RGBAQ, UV, XYZ2 per vertex, per pass), the prebuilt packet it
 *     references (D_002510C0 sprite / D_00251140 triangle strip) and the
 *     strip contents since the last reset. Drawing them is the renderer's.
 *
 * Verified by tools/test_message_glyph_reference.py, which executes the
 * original instructions (001FC7B0 down to the packet writes) over captured
 * EE RAM and compares every upload call and every packed register value.
 *
 * No font, colour or packet data is embedded: the style block and the run
 * config come from the caller (D_00275C50 / D_00264CD0 in the user's ELF).
 * stdint only; no dependency on any port subsystem. */
#ifndef EM_MESSAGE_GLYPH_ORIGINAL_H
#define EM_MESSAGE_GLYPH_ORIGINAL_H

#include <stdint.h>

#include "game/em_message_draw_original.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    EM_MESSAGE_GLYPH_PASSES = 5,     /* 001CC3B0: four outline passes, one fill */
    EM_MESSAGE_GLYPH_RUN = 0x80,     /* 001FC7B0's on-stack run buffer          */
    EM_MESSAGE_GLYPH_STRIP = 0x200,  /* 001CC1E0 flushes when the strip x reaches it */
    EM_MESSAGE_GLYPH_UPLOADS = 0x100 /* native bound on uploads kept per strip   */
};

/* The prebuilt GS packet 001CC3B0 references before its passes. */
typedef enum {
    EM_MESSAGE_GLYPH_SPRITE = 0,   /* style flag 0: D_002510C0, 2 vertices     */
    EM_MESSAGE_GLYPH_SKEWED = 1    /* style flag != 0: D_00251140, 4 vertices  */
} EmMessageGlyphKind;

/* 001CC8A0(1, x, y, offset): offset = glyph index * 30 into the tall font. */
typedef struct {
    int32_t x, y;
    uint32_t offset;
} EmMessageGlyphUpload;

/* One pass as 001CC3B0 packs it (GIF register values, 64-bit). A sprite
 * uses vertices 0 and 1; the skewed strip uses 0..3 in its original order
 * (top-left, bottom-left, top-right, bottom-right). */
typedef struct {
    uint64_t rgbaq;
    uint64_t uv[4];
    uint64_t xyz2[4];
} EmMessageGlyphPass;

typedef struct {
    int32_t slot;                  /* argument 1: render-context slot     */
    int32_t x, y;                  /* arguments 2, 3: GS pixel coordinates */
    int32_t u_end, v_end;          /* arguments 4, 5: strip texel extent   */
    int32_t width, height;         /* arguments 6, 7                       */
    const EmMessageTextStyle *style; /* argument 8 (NULL: default colours) */
    EmMessageGlyphKind kind;
    int32_t vertex_count;          /* 2 (sprite) or 4 (skewed)            */
    EmMessageGlyphPass pass[EM_MESSAGE_GLYPH_PASSES];
    /* The strip contents at the flush: every upload since the last reset,
     * in upload order (a later upload covers an earlier one's texels). */
    const EmMessageGlyphUpload *uploads;
    uint32_t upload_count;
} EmMessageGlyphFlush;

typedef struct {
    void *context;
    int (*upload)(void *context, const EmMessageGlyphUpload *upload);
    int (*flush)(void *context, const EmMessageGlyphFlush *flush);
} EmMessageGlyphWorkers;

typedef struct {
    EmMessageGlyphWorkers workers;
    EmMessageGlyphUpload strip[EM_MESSAGE_GLYPH_UPLOADS];
    uint32_t strip_count;
    const char *fault;             /* latched; later calls fail */
} EmMessageGlyph;

/* Binds the workers and clears the strip and the fault. 1, or 0 for NULL. */
int em_message_glyph_init(EmMessageGlyph *glyph, const EmMessageGlyphWorkers *workers);

/* 001CBE10(c): the tall-font advance of byte value c. */
int32_t em_message_glyph_advance(int32_t c);

/* 001FC7B0(x, y, text, cfg): `text` points at `avail` readable bytes and
 * must hold its NUL inside them. cfg words 1, 2 and 4 and its style
 * pointer (+0x14) are read. 0 ok, -1 fault. */
int em_message_glyph_fc7b0(EmMessageGlyph *glyph, int32_t x, int32_t y, const uint8_t *text,
                           uint32_t avail, const EmMessageDrawConfig *cfg);

/* 001CC1E0(slot, x, y, unused, h, text, style): the fourth argument is
 * not read by the original. 0 ok, -1 fault. */
int em_message_glyph_cc1e0(EmMessageGlyph *glyph, int32_t slot, int32_t x, int32_t y,
                           int32_t unused, int32_t h, const uint8_t *text, uint32_t avail,
                           const EmMessageTextStyle *style);

/* 001CC3B0(slot, x, y, u_end, v_end, width, height, style): fills `out`
 * (uploads and upload_count are left to the caller). Pure. */
void em_message_glyph_cc3b0(int32_t slot, int32_t x, int32_t y, int32_t u_end, int32_t v_end,
                            int32_t width, int32_t height, const EmMessageTextStyle *style,
                            EmMessageGlyphFlush *out);

#ifdef __cplusplus
}
#endif

#endif
