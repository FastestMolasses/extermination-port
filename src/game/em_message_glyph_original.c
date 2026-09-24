/* Tall-font glyph runs: 001FC7B0, 001CC1E0, 001CBE10, 001CC3B0 (see the
 * header and docs/MESSAGE_GLYPH.md). 001FC7B0 and 001CC3B0 are NEARMISS in
 * the decomp and 001CC1E0 is asm there, so all three follow the .s. */
#include "game/em_message_glyph_original.h"

#include <stddef.h>
#include <string.h>

static int fail(EmMessageGlyph *g, const char *why)
{
    if (!g->fault) g->fault = why;
    return -1;
}

/* A 32-bit result as the EE holds it in a 64-bit register: sign-extended
 * (every 32-bit shift, load and immediate result, and the explicit
 * re-extension 001CC3B0 applies before its 64-bit stores). */
static uint64_t sext32(uint32_t value)
{
    return (uint64_t)(int64_t)(int32_t)value;
}

/* A wrapping 32-bit add, as the original's integer adds compute it. */
static uint32_t add32(int32_t a, int32_t b)
{
    return (uint32_t)a + (uint32_t)b;
}

/* Arithmetic (sign-preserving) right shift, as the original shifts. */
static int32_t sra32(int32_t value, unsigned shift)
{
    uint32_t v = (uint32_t)value;
    return (int32_t)(v & 0x80000000u ? ~(~v >> shift) : v >> shift);
}

int em_message_glyph_init(EmMessageGlyph *g, const EmMessageGlyphWorkers *workers)
{
    if (!g || !workers) return 0;
    memset(g, 0, sizeof *g);
    g->workers = *workers;
    return 1;
}

/* 001CBE10: a switch over the byte value; every other value is 9. */
int32_t em_message_glyph_advance(int32_t c)
{
    switch (c) {
    case 0x20: return 5;
    case 0x21: return 4;
    case 0x27: return 4;
    case 0x28: return 8;
    case 0x29: return 8;
    case 0x2C: return 6;
    case 0x2E: return 5;
    case 0x2F: return 8;
    case 0x3A: return 7;
    case 0x3B: return 8;
    case 0x49: return 4;
    case 0x4A: return 7;
    case 0x4D: return 0xC;
    case 0x57: return 0xC;
    case 0x5B: return 8;
    case 0x5C: return 8;
    case 0x5D: return 8;
    case 0x60: return 5;
    case 0x66: return 8;
    case 0x69: return 4;
    case 0x6A: return 6;
    case 0x6C: return 4;
    case 0x6D: return 0xC;
    case 0x72: return 9;
    case 0x77: return 0xC;
    case 0x82: return 6;
    case 0x84: return 8;
    case 0x8B: return 9;
    case 0x91: return 6;
    case 0x92: return 6;
    case 0x93: return 8;
    case 0x94: return 8;
    case 0x9B: return 9;
    case 0xA1: return 4;
    case 0xA6: return 6;
    default: return 9;
    }
}

/* 001232E0 inside `avail` bytes: 1 and the length, or 0 without a NUL. */
static int bounded_strlen(const uint8_t *s, uint32_t avail, uint32_t *length)
{
    if (!s) return 0;
    const uint8_t *end = memchr(s, 0, avail);
    if (!end) return 0;
    *length = (uint32_t)(end - s);
    return 1;
}

/* 001CC3B0. Arguments in the original's order: slot, x, y, u_end, v_end,
 * width, height, style. */
void em_message_glyph_cc3b0(int32_t slot, int32_t x, int32_t y, int32_t u_end, int32_t v_end,
                            int32_t width, int32_t height, const EmMessageTextStyle *style,
                            EmMessageGlyphFlush *out)
{
    memset(out, 0, sizeof *out);
    out->slot = slot;
    out->x = x;
    out->y = y;
    out->u_end = u_end;
    out->v_end = v_end;
    out->width = width;
    out->height = height;
    out->style = style;

    /* The colours: with a style, alpha = style +4 in both, the outline RGB is
     * the constant 0x100505 and the fill is style word 0 ORed in; without
     * one, 0x80100505 and 0x80808080. flag = style +5, an unsigned byte. */
    uint32_t outline, fill, flag;
    if (style) {
        uint32_t hi = (uint32_t)style->glyph << 24;
        outline = hi | 0x100505u;
        fill = hi | (uint32_t)style->color;
        flag = style->flag;
    } else {
        outline = 0x80100505u;
        fill = 0x80808080u;
        flag = 0;
    }
    const uint64_t z = (uint64_t)0xFFFFFFu << 32;   /* XYZ2 Z field */

    for (int i = 0; i < EM_MESSAGE_GLYPH_PASSES; i++) {
        /* Pass order of the likely-branch chain: y - 1, x + 1, y + 1,
         * x - 1 in the outline colour, then the fill at (x, y). */
        int32_t px = x, py = y;   /* wrapping 32-bit adds, as the original */
        uint32_t colour = outline;
        switch (i) {
        case 0: py = (int32_t)((uint32_t)y - 1u); break;
        case 1: px = (int32_t)((uint32_t)x + 1u); break;
        case 2: py = (int32_t)((uint32_t)y + 1u); break;
        case 3: px = (int32_t)((uint32_t)x - 1u); break;
        default: colour = fill; break;
        }
        EmMessageGlyphPass *p = &out->pass[i];
        p->rgbaq = sext32(colour);
        p->uv[0] = 0;
        if (flag != 0) {
            /* Triangle strip, the top edge shifted right by the flag. */
            int32_t half = sra32(height, 1);
            int32_t top_x = (int32_t)add32(px, (int32_t)(flag & 0xFFu));
            p->xyz2[0] = sext32((uint32_t)py << 20 | (uint32_t)top_x << 4) | z;
            p->uv[1] = sext32((uint32_t)v_end << 20);
            p->xyz2[1] = sext32(add32(py, half) << 20 | (uint32_t)px << 4) | z;
            p->uv[2] = sext32((uint32_t)u_end << 4);
            p->xyz2[2] = sext32((uint32_t)py << 20 | add32(width, top_x) << 4) | z;
            p->uv[3] = sext32((uint32_t)u_end << 4 | (uint32_t)v_end << 20);
            p->xyz2[3] = sext32(add32(py, half) << 20 | add32(px, width) << 4) | z;
        } else {
            /* Sprite; the style's +7 byte (clamped to 0xF) is ORed into
             * both XYZ2 words at bit 16. */
            int32_t half = sra32(height, 1);
            p->xyz2[0] = sext32((uint32_t)px << 4 | (uint32_t)py << 20) | z;
            p->uv[1] = sext32((uint32_t)u_end << 4 | (uint32_t)v_end << 20);
            p->xyz2[1] = sext32(add32(px, width) << 4 | add32(py, half) << 20) | z;
            if (style) {
                uint32_t a = style->pad[1];
                if (a >= 0x10) a = 0xF;
                p->xyz2[0] |= (uint64_t)(a << 16);
                p->xyz2[1] |= (uint64_t)(a << 16);
            }
        }
    }
    out->kind = flag != 0 ? EM_MESSAGE_GLYPH_SKEWED : EM_MESSAGE_GLYPH_SPRITE;
    out->vertex_count = flag != 0 ? 4 : 2;
}

static int upload(EmMessageGlyph *g, int32_t x, int32_t y, uint32_t offset)
{
    if (g->strip_count >= EM_MESSAGE_GLYPH_UPLOADS) return fail(g, "glyph strip upload overflow");
    EmMessageGlyphUpload *u = &g->strip[g->strip_count++];
    u->x = x;
    u->y = y;
    u->offset = offset;
    if (!g->workers.upload) return fail(g, "upload worker missing");
    if (!g->workers.upload(g->workers.context, u)) return fail(g, "upload worker failed");
    return 0;
}

static int flush(EmMessageGlyph *g, int32_t slot, int32_t x, int32_t y, int32_t u_end,
                 int32_t v_end, int32_t width, int32_t height, const EmMessageTextStyle *style)
{
    EmMessageGlyphFlush out;
    em_message_glyph_cc3b0(slot, x, y, u_end, v_end, width, height, style, &out);
    out.uploads = g->strip;
    out.upload_count = g->strip_count;
    if (!g->workers.flush) return fail(g, "flush worker missing");
    if (!g->workers.flush(g->workers.context, &out)) return fail(g, "flush worker failed");
    return 0;
}

/* 001CC1E0. Arguments in the original's order: slot, x, y, an unused fourth
 * argument, the height step, text, style. */
int em_message_glyph_cc1e0(EmMessageGlyph *g, int32_t slot, int32_t x, int32_t y, int32_t unused,
                           int32_t h, const uint8_t *text, uint32_t avail,
                           const EmMessageTextStyle *style)
{
    (void)unused;
    if (!g || g->fault) return -1;
    uint32_t len;
    if (!bounded_strlen(text, avail, &len)) return fail(g, "glyph run without terminator");
    /* The strip cursor (x, y), the accumulated height and the run width;
     * 001CCB00, which the original calls here, is empty. */
    int32_t strip_y = 0, strip_x = 0, height = 0, run = 0;
    g->strip_count = 0;
    for (uint32_t i = 0; i < len; i++) {
        uint32_t c = text[i];
        int32_t glyph = 0, advance = 0;
        if (c >= 0x20) {
            advance = em_message_glyph_advance((int32_t)c);
            glyph = c == 0x24 ? 0x89 : (int32_t)c - 0x20;
        }
        /* 001CC8A0(1, strip x, strip y, glyph * 30). */
        if (upload(g, strip_x, strip_y, (uint32_t)(glyph * 30))) return -1;
        strip_x = (int32_t)add32(strip_x, advance);
        run = (int32_t)add32(run, advance);
        if (strip_x >= EM_MESSAGE_GLYPH_STRIP) {
            height = (int32_t)add32(height, h);
            if (flush(g, slot, x, y, (int32_t)add32(strip_x, 1), (int32_t)add32(strip_y, 0x14), run,
                      height, style))
                return -1;
            x = (int32_t)((uint32_t)x + (uint32_t)run);
            g->strip_count = 0;
            strip_y = strip_x = height = run = 0;
        }
    }
    height = (int32_t)add32(height, h);
    return flush(g, slot, x, y, (int32_t)add32(strip_x, 1), (int32_t)add32(strip_y, 0x14), run,
                 height, style);
}

/* 001FC7B0. The 0x80-byte template D_00264C10 it first copies into its run
 * buffer is dead: every run memsets the buffer before writing it. */
int em_message_glyph_fc7b0(EmMessageGlyph *g, int32_t x, int32_t y, const uint8_t *text,
                           uint32_t avail, const EmMessageDrawConfig *cfg)
{
    if (!g || g->fault) return -1;
    if (!cfg) return fail(g, "glyph run config missing");
    for (;;) {
        uint32_t len;
        if (!bounded_strlen(text, avail, &len)) return fail(g, "glyph text without terminator");
        uint32_t i = 0;
        int newline = 0;
        while (i < len) {
            uint32_t c = text[i];
            if (c < 0x20) {
                if (c == 0x0A) {
                    /* Recurse on the rest, one line lower, then return. */
                    y = (int32_t)((uint32_t)y + (uint32_t)sra32(
                            (int32_t)((uint32_t)cfg->word[4] + (uint32_t)cfg->word[2]), 1));
                    text += i + 1;
                    avail -= i + 1;
                    newline = 1;
                    break;
                }
                i++;
                continue;
            }
            if (c == 0x80 || (c >= 0xA0 && c < 0xE0) || c >= 0xF0) {
                i++;
                continue;
            }
            /* A run: base x = x + i * cfg word 1 (mult), buffer cleared. */
            int32_t base = (int32_t)((uint32_t)x + i * (uint32_t)cfg->word[1]);
            uint8_t run[EM_MESSAGE_GLYPH_RUN];
            memset(run, 0, sizeof run);
            uint32_t n = 0;
            while (c >= 0x20 && c < 0x100 && i < len) {
                if (c == 0x81) {
                    i++;
                    c = 0x20;
                }
                i++;
                if (n >= EM_MESSAGE_GLYPH_RUN - 1)
                    return fail(g, "glyph run longer than its buffer");
                run[n++] = (uint8_t)c;
                /* The next byte; past the NUL (a trailing 0x81) it is never
                 * used, since i >= len ends the run and the line. */
                c = i <= len ? text[i] : 0;
            }
            if ((int32_t)n > 0 &&
                em_message_glyph_cc1e0(g, 1, (int32_t)((uint32_t)base + 0x700u),
                                       (int32_t)((uint32_t)y + 0x790u), 0xA, 0x14, run,
                                       sizeof run, cfg->style))
                return -1;
        }
        if (!newline) return 0;
    }
}
