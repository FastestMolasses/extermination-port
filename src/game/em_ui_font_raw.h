/* em_ui_font_raw.h - the two UI fonts' 1bpp resource bytes, rebuilt from
 * the user's font export (docs/STATUS_FIELD.md section 5, step 3). Prefix
 * em_ufr_.
 *
 * 001CC8A0 (em_uip_001CC8A0) reads the fonts as the resources the boot
 * loader puts at *D_0028A490 (tall: 30 bytes a glyph, 12 x 20) and
 * *D_0028A494 (small: 32 bytes a glyph, 16 x 16). The port's
 * assets/font.emfn (the decomp's tools/export_font.py, from the user's own
 * EE RAM) holds every glyph slot of both resources as ink coverage, one
 * texel per bit, so the bytes are recovered exactly by inverting the
 * exporter's layout (FINDINGS.md "UI FONT"): small, 2 bytes a row, bit 0
 * of each nibble the leftmost texel, low nibble first; tall, 3 bytes per
 * two 12-texel rows (byte 0 low and high nibble and byte 1 low nibble the
 * even row, byte 1 high nibble and byte 2 the odd row). A glyph record of
 * another size, a coverage value other than 0 or 255, or a count other
 * than the resources' (409 tall, 256 small) refuses the file. No font data
 * is embedded here.
 *
 * Checked by tools/test_ui_packet_reference.py against the fonts in the
 * decomp's fork captures (every byte of both resources). stdint and stdio
 * only. */
#ifndef EM_UI_FONT_RAW_H
#define EM_UI_FONT_RAW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    EM_UFR_TALL_GLYPHS = 409,                    /* (0xB03000 - 0xB00000) / 30 */
    EM_UFR_SMALL_GLYPHS = 256,                   /* (0xB05000 - 0xB03000) / 32 */
    EM_UFR_TALL_BYTES = EM_UFR_TALL_GLYPHS * 30, /* 12270 */
    EM_UFR_SMALL_BYTES = EM_UFR_SMALL_GLYPHS * 32
};

typedef struct {
    uint8_t tall[EM_UFR_TALL_BYTES];
    uint8_t small[EM_UFR_SMALL_BYTES];
} EmUfrFonts;

/* Rebuild both fonts from an EMFN v1 file. 0, or -1 (missing or not the
 * resources' layout; `why` names the reason when not NULL). */
int em_ufr_load(const char *path, EmUfrFonts *out, const char **why);

/* D_0026E350: the halfword whose nibble i is 0xF when bit i of n is set,
 * for n = 0..15 (001CC8A0's texel expansion; the test checks it against
 * the ELF's table). */
void em_ufr_nibbles(uint16_t out[16]);

#ifdef __cplusplus
}
#endif

#endif
