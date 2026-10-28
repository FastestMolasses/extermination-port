/* em_ui_font_raw.c - see em_ui_font_raw.h. */
#include "game/em_ui_font_raw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int refuse(const char **why, const char *reason)
{
    if (why) *why = reason;
    return -1;
}

/* The ink bit of texel (x, y) of a glyph cell: 1, 0, or -1 (a coverage
 * that is neither). */
static int ink(const uint8_t *sheet, uint32_t sheet_w, uint32_t u, uint32_t v, uint32_t x, uint32_t y)
{
    const uint8_t a = sheet[((size_t)(v + y) * sheet_w + u + x) * 4u + 3u];
    return a == 0xFF ? 1 : a == 0 ? 0 : -1;
}

/* Four texels of a row as one nibble (bit 0 the leftmost). */
static int nibble(const uint8_t *sheet, uint32_t sheet_w, uint32_t u, uint32_t v, uint32_t x, uint32_t y)
{
    int n = 0;
    for (uint32_t b = 0; b < 4; b++) {
        const int t = ink(sheet, sheet_w, u, v, x + b, y);
        if (t < 0) return -1;
        n |= t << b;
    }
    return n;
}

int em_ufr_load(const char *path, EmUfrFonts *out, const char **why)
{
    if (!out) return refuse(why, "no output");
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) return refuse(why, "font export missing");
    uint8_t *data = NULL;
    long size = -1;
    if (fseek(f, 0, SEEK_END) == 0) size = ftell(f);
    if (size < 56 || size > (64L << 20) || fseek(f, 0, SEEK_SET) != 0 || !(data = malloc((size_t)size)) ||
        fread(data, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(data);
        return refuse(why, "font export unreadable");
    }
    fclose(f);
    int rc = -1;
    const char *reason = "font export header";
    const uint32_t sheet_w = rd32(data + 8), sheet_h = rd32(data + 12), fonts = rd32(data + 16),
                   glyphs = rd32(data + 20);
    if (memcmp(data, "EMFN", 4) != 0 || rd32(data + 4) != 1 || fonts != 2 ||
        glyphs != EM_UFR_TALL_GLYPHS + EM_UFR_SMALL_GLYPHS || !sheet_w || !sheet_h || sheet_w > 4096 ||
        sheet_h > 4096 ||
        (uint64_t)size != 24u + 16u * fonts + 8u * (uint64_t)glyphs + (uint64_t)sheet_w * sheet_h * 4u)
        goto done;
    reason = "font export: not the resources' two fonts";
    const uint8_t *face = data + 24;
    if (rd32(face) != 0 || rd32(face + 4) != EM_UFR_TALL_GLYPHS || rd32(face + 8) != 12 || rd32(face + 12) != 20 ||
        rd32(face + 16) != EM_UFR_TALL_GLYPHS || rd32(face + 20) != EM_UFR_SMALL_GLYPHS || rd32(face + 24) != 16 ||
        rd32(face + 28) != 16)
        goto done;
    const uint8_t *rec = data + 24 + 16u * fonts;
    const uint8_t *sheet = rec + 8u * glyphs;
    for (uint32_t i = 0; i < glyphs; i++, rec += 8) {
        const int tall = i < EM_UFR_TALL_GLYPHS;
        const uint32_t u = (uint32_t)rec[0] | (uint32_t)rec[1] << 8, v = (uint32_t)rec[2] | (uint32_t)rec[3] << 8;
        const uint32_t w = rec[4], h = rec[5];
        reason = "font export: a glyph cell of another size or outside the sheet";
        if (w != (tall ? 12u : 16u) || h != (tall ? 20u : 16u) || u + w > sheet_w || v + h > sheet_h) goto done;
        reason = "font export: a coverage other than 0 or 255";
        if (tall) {
            uint8_t *g = out->tall + 30u * i;
            for (uint32_t r = 0; r < 10; r++) {
                int n[6];
                for (uint32_t k = 0; k < 3; k++) {
                    n[k] = nibble(sheet, sheet_w, u, v, 4 * k, 2 * r);
                    n[3 + k] = nibble(sheet, sheet_w, u, v, 4 * k, 2 * r + 1);
                }
                for (uint32_t k = 0; k < 6; k++)
                    if (n[k] < 0) goto done;
                g[3 * r + 0] = (uint8_t)(n[0] | n[1] << 4);
                g[3 * r + 1] = (uint8_t)(n[2] | n[3] << 4);
                g[3 * r + 2] = (uint8_t)(n[4] | n[5] << 4);
            }
        } else {
            uint8_t *g = out->small + 32u * (i - EM_UFR_TALL_GLYPHS);
            for (uint32_t r = 0; r < 16; r++)
                for (uint32_t b = 0; b < 2; b++) {
                    const int lo = nibble(sheet, sheet_w, u, v, 8 * b, r), hi = nibble(sheet, sheet_w, u, v, 8 * b + 4, r);
                    if (lo < 0 || hi < 0) goto done;
                    g[2 * r + b] = (uint8_t)(lo | hi << 4);
                }
        }
    }
    rc = 0;
    reason = NULL;
done:
    free(data);
    if (why) *why = reason;
    return rc;
}

void em_ufr_nibbles(uint16_t out[16])
{
    for (uint32_t n = 0; n < 16; n++) {
        uint32_t v = 0;
        for (uint32_t b = 0; b < 4; b++)
            if (n >> b & 1u) v |= 0xFu << (4 * b);
        out[n] = (uint16_t)v;
    }
}
