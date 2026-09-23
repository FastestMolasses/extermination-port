/* Message line layout and draw (001FD950 draw prefix, 001FE070, 001FE530,
 * 001FE480/460/4B0/4D0, 001CC170, 001FC770, 001232E0). See the header and
 * docs/MESSAGE_DRAW.md. Each routine below names the original it
 * translates; 001FE070 is NEARMISS in the decomp, so it follows the .s. */
#include "game/em_message_draw_original.h"

#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(EmMessageTextStyle) == 8, "D_00275C50 style block");
_Static_assert(offsetof(EmMessageTextStyle, glyph) == 4, "D_00275C54");
_Static_assert(offsetof(EmMessageTextStyle, flag) == 5, "D_00275C55");

static int fail(EmMessageDraw *d, const char *why)
{
    if (!d->fault) d->fault = why;
    return -1;
}

/* Arithmetic right shift of a 32-bit value (sra). */
static int32_t sra32(uint32_t value, unsigned shift)
{
    return (int32_t)(value & 0x80000000u ? ~(~value >> shift) : value >> shift);
}

/* lw from the bank: the bank starts word-aligned in EE RAM, so an offset
 * that is not a multiple of 4 would be an address error on the EE. */
static int rd32(const EmMessageBank *b, uint32_t off, uint32_t *value)
{
    if (!b->bytes || b->size < 4 || off > b->size - 4 || (off & 3)) return 0;
    const uint8_t *p = b->bytes + off;
    *value = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    return 1;
}

static int rd8(const EmMessageBank *b, uint32_t off, uint8_t *value)
{
    if (!b->bytes || off >= b->size) return 0;
    *value = b->bytes[off];
    return 1;
}

/* H = tbl + tbl[0] + tbl[2]: the string section header. */
static int string_section(const EmMessageBank *b, uint32_t *h)
{
    uint32_t w0, w2;
    if (!rd32(b, 0, &w0) || !rd32(b, 8, &w2)) return 0;
    *h = w0 + w2;
    return 1;
}

/* 001FE460: *(H + 4). */
int em_message_bank_count(const EmMessageBank *b, int32_t *count)
{
    uint32_t h, v;
    if (!b || !string_section(b, &h) || !rd32(b, h + 4, &v)) return 0;
    *count = (int32_t)v;
    return 1;
}

/* 001FE480: H + *H + *(H + 0x10 + (index << 4)). */
int em_message_bank_string(const EmMessageBank *b, int32_t index, uint32_t *offset)
{
    uint32_t h, pool, entry;
    if (!b || !string_section(b, &h) || !rd32(b, h, &pool) ||
        !rd32(b, h + 0x10 + ((uint32_t)index << 4), &entry))
        return 0;
    *offset = h + pool + entry;
    return 1;
}

/* 001FE4B0: *(tbl + 0x10 + (index << 4) + 0xC) >> 4 (srl). */
int em_message_bank_records(const EmMessageBank *b, int32_t index, uint32_t *count)
{
    uint32_t v;
    if (!b || !rd32(b, 0x10 + ((uint32_t)index << 4) + 0xC, &v)) return 0;
    *count = v >> 4;
    return 1;
}

/* 001FE4D0: 0 when index >= tbl[1] (signed) or record >= the line's
 * count (unsigned); else tbl + tbl[0] + e[0] + (record << 4). */
int em_message_bank_record(const EmMessageBank *b, int32_t index, uint32_t record,
                           int *found, uint32_t *offset)
{
    uint32_t base, lines, e0, e3;
    if (!b || !rd32(b, 0, &base) || !rd32(b, 4, &lines)) return 0;
    *found = 0;
    if (index >= (int32_t)lines) return 1;
    uint32_t e = ((uint32_t)index << 4) + 0x10;
    if (!rd32(b, e + 0xC, &e3)) return 0;
    if (record >= e3 >> 4) return 1;
    if (!rd32(b, e, &e0)) return 0;
    *found = 1;
    *offset = base + e0 + (record << 4);
    return 1;
}

/* 001232E0: bytes before the first NUL. */
int em_message_draw_strlen(const uint8_t *s, uint32_t avail, uint32_t *length)
{
    if (!s) return 0;
    const uint8_t *end = memchr(s, 0, avail);
    if (!end) return 0;
    *length = (uint32_t)(end - s);
    return 1;
}

/* 001CC170: strlen, then the sum of 001CBE10(c) over the bytes c >= 0x20
 * (bytes below 0x20 add nothing but still count down the length). */
int em_message_draw_cc170(EmMessageDraw *d, const uint8_t *s, uint32_t avail, int32_t *width)
{
    uint32_t length, sum = 0;
    if (d->fault) return -1;
    if (!em_message_draw_strlen(s, avail, &length)) return fail(d, "string without terminator");
    for (uint32_t k = 0; k < length; k++) {
        uint8_t c = s[k];
        if (c < 0x20) continue;
        int32_t advance = 0;
        if (!d->workers.glyph_advance) return fail(d, "glyph_advance worker missing");
        if (!d->workers.glyph_advance(d->workers.context, c, &advance))
            return fail(d, "glyph_advance worker failed");
        sum += (uint32_t)advance;
    }
    *width = (int32_t)sum;
    return 0;
}

/* 001FE530(dst, src, skip): with skip != 0, move src past the skip-th
 * 0x0A/0x0C of its strlen (unchanged when there are fewer); then copy up
 * to the next 0x0A, 0x0C or NUL into dst (when dst != 0) and terminate it.
 * Returns the byte after that 0x0A/0x0C, or the NUL itself. */
int em_message_draw_fe530(EmMessageDraw *d, uint8_t *dst, uint32_t dst_size,
                          const uint8_t *src, uint32_t avail, int32_t skip, uint32_t *next)
{
    uint32_t base = 0, i;
    uint8_t c;
    if (d->fault) return -1;
    if (!src) return fail(d, "segment source missing");
    if (skip != 0) {
        uint32_t length;
        if (!em_message_draw_strlen(src, avail, &length)) return fail(d, "string without terminator");
        int32_t count = 0;
        for (i = 0; i < length; i++) {
            c = src[i];
            if (c != 0x0A && c != 0x0C) continue;
            count++;
            if (count == skip) { base = i + 1; break; }
        }
    }
    i = 0;
    for (;;) {
        if (base + i >= avail) return fail(d, "segment read outside source");
        c = src[base + i];
        if (c == 0x0A || c == 0x0C || c == 0) break;
        if (dst) {
            if (i >= dst_size) return fail(d, "segment store outside buffer");
            dst[i] = src[base + i];
        }
        i++;
    }
    if (dst) {
        if (i >= dst_size) return fail(d, "segment store outside buffer");
        dst[i] = 0;
    }
    *next = c != 0 ? base + i + 1 : base + i;
    return 0;
}

/* 001FC770(x, y, text, cfg): a NULL cfg becomes a stack copy of the
 * D_00264BF0 template; then 001FC7B0(x, y, text, cfg). */
int em_message_draw_fc770(EmMessageDraw *d, int32_t x, int32_t y, const uint8_t *text,
                          const EmMessageDrawConfig *cfg)
{
    EmMessageDrawConfig copy;
    if (d->fault) return -1;
    if (!cfg) {
        if (!d->data->fallback) return fail(d, "D_00264BF0 template missing");
        copy = *d->data->fallback;
        cfg = &copy;
    }
    if (!d->workers.draw_text) return fail(d, "draw_text worker missing");
    if (!d->workers.draw_text(d->workers.context, x, y, text, cfg))
        return fail(d, "draw_text worker failed");
    return 0;
}

/* 001FE070 flushes the D_00820F90 run through 001FC770(.., &D_00264CD0). */
static int flush(EmMessageDraw *d, int32_t x, int32_t y)
{
    if (!memchr(d->line, 0, sizeof d->line)) return fail(d, "line buffer without terminator");
    return em_message_draw_fc770(d, x, y, d->line, d->data->line_config);
}

/* The per-byte step shared by both 001FE070 walks. */
static int walk_byte(EmMessageDraw *d, const EmMessageBank *b, uint32_t text, uint32_t i,
                     int32_t x, int32_t *line, int32_t *pen, int32_t *lit, uint32_t *used)
{
    uint8_t c;
    if (!rd8(b, text + i, &c)) return fail(d, "line text outside bank");
    if (c == 0x0A) {
        if (flush(d, *line, *pen)) return -1;
        const EmMessageDrawConfig *cfg = d->data->line_config;
        *line = x;
        *lit = 0;
        *used = 0;
        *pen = (int32_t)((uint32_t)*pen + (uint32_t)sra32((uint32_t)cfg->word[2] + (uint32_t)cfg->word[4], 1));
        memset(d->line, 0, sizeof d->line);
        return 0;
    }
    const uint8_t pair[2] = { c, 0 };
    int32_t width;
    if (em_message_draw_cc170(d, pair, sizeof pair, &width)) return -1;
    *lit = (int32_t)((uint32_t)*lit + (uint32_t)width);
    if (*used >= sizeof d->line) return fail(d, "line store outside buffer");
    d->line[(*used)++] = c;
    return 0;
}

static int record_word(EmMessageDraw *d, const EmMessageBank *b, uint32_t at, uint32_t *v)
{
    return rd32(b, at, v) ? 0 : fail(d, "line record outside bank");
}

/* 001FE070 (NEARMISS; from the .s). */
int em_message_draw_fe070(EmMessageDraw *d, const EmMessageBank *b, int32_t index,
                          int32_t x, int32_t y)
{
    int32_t count;
    uint32_t records, text, length, rec = 0;
    int found = 0;
    if (d->fault) return -1;
    if (!b) return fail(d, "message bank missing");
    if (!em_message_bank_count(b, &count)) return fail(d, "bank header outside bank");
    if (index < 0 || index >= count) return 0;
    if (!em_message_bank_records(b, index, &records) || !em_message_bank_string(b, index, &text) ||
        !em_message_bank_record(b, index, 0, &found, &rec))
        return fail(d, "bank line entry outside bank");
    if (text >= b->size || !em_message_draw_strlen(b->bytes + text, b->size - text, &length))
        return fail(d, "line text outside bank");
    if (records == 0) {
        if (em_message_draw_fc770(d, x, y, b->bytes + text, d->data->line_config)) return -1;
        return 1;
    }
    if (!found) return fail(d, "line records missing"); /* the original would read address 0 */

    memset(d->line, 0, sizeof d->line);
    uint32_t i = 0, used = 0, rc = 0;
    int32_t lit = 0, line = x, pen = y;
    while (rc < records) {
        uint32_t r = rec + rc * 0x10, trigger;
        if (record_word(d, b, r + 8, &trigger)) return -1;
        if ((int32_t)i == (int32_t)trigger) {
            uint32_t tag, arg;
            uint8_t byte;
            if (flush(d, line, pen)) return -1;
            if (record_word(d, b, r, &tag)) return -1;
            if (tag == 4) {
                if (record_word(d, b, r + 4, &arg)) return -1;
                if ((int32_t)arg < 0 || arg >= d->data->color_count)
                    return fail(d, "D_0026EC10 index outside table");
                d->data->text->color = d->data->colors[arg];
                if (!rd8(b, r + 0xC, &byte)) return fail(d, "line record outside bank");
                d->data->text->flag = (uint8_t)(byte << 3);
            } else if (tag == 3) {
                if (!rd8(b, r + 4, &byte)) return fail(d, "line record outside bank");
                if (!d->data->line_config->style) return fail(d, "D_00264CE4 style missing");
                d->data->line_config->style->flag = (uint8_t)(byte << 3);
            } else if (tag == 2) {
                if (record_word(d, b, r + 4, &arg)) return -1;
                if ((int32_t)arg < 0 || arg >= d->data->color_count)
                    return fail(d, "D_0026EC10 index outside table");
                if (!d->data->line_config->style) return fail(d, "D_00264CE4 style missing");
                d->data->line_config->style->color = d->data->colors[arg];
            }
            rc++;
            line = (int32_t)((uint32_t)x + (uint32_t)lit);
            used = 0;
            memset(d->line, 0, sizeof d->line);
            /* Later records with the same trigger are skipped unapplied. */
            while (rc < records) {
                uint32_t before, here;
                if (record_word(d, b, rec + (rc - 1) * 0x10 + 8, &before) ||
                    record_word(d, b, rec + rc * 0x10 + 8, &here))
                    return -1;
                if (before != here) break;
                rc++;
            }
        }
        if (walk_byte(d, b, text, i, x, &line, &pen, &lit, &used)) return -1;
        i++;
    }
    while ((int32_t)i < (int32_t)length) {
        if (walk_byte(d, b, text, i, x, &line, &pen, &lit, &used)) return -1;
        i++;
    }
    if (flush(d, line, pen)) return -1;
    return 1;
}

int em_message_draw_line(void *context, int global, uint32_t index)
{
    EmMessageDraw *d = context;
    if (!d || !d->data) return 0;
    if (d->fault) return 0;
    /* 001FD950: the bank word, the index with bit 31 cleared. */
    const EmMessageBank *b = global ? &d->data->global : &d->data->area;
    int32_t line = (int32_t)(index & 0x7FFFFFFFu);
    uint32_t at;
    int32_t w[2];
    if (!em_message_bank_string(b, line, &at)) {
        fail(d, "bank line entry outside bank");
        return 0;
    }
    for (int k = 0; k < 2; k++) {
        uint32_t next;
        if (at >= b->size) {
            fail(d, "line text outside bank");
            return 0;
        }
        if (em_message_draw_fe530(d, d->measure, sizeof d->measure, b->bytes + at, b->size - at, 0, &next))
            return 0;
        at += next;
        if (em_message_draw_cc170(d, d->measure, sizeof d->measure, &w[k])) return 0;
    }
    int32_t x = w[0] < w[1] ? (int32_t)(0x100u - (uint32_t)sra32((uint32_t)w[1], 1))
                            : (int32_t)(0x100u - (uint32_t)sra32((uint32_t)w[0], 1));
    return em_message_draw_fe070(d, b, line, x, 0xC2) < 0 ? 0 : 1;
}

int em_message_draw_init(EmMessageDraw *d, const EmMessageDrawData *data,
                         const EmMessageDrawWorkers *workers)
{
    if (!d || !data || !data->line_config || !data->text || (data->color_count && !data->colors))
        return 0;
    d->data = data;
    if (workers) d->workers = *workers;
    else memset(&d->workers, 0, sizeof d->workers);
    memset(d->measure, 0, sizeof d->measure);
    memset(d->line, 0, sizeof d->line);
    d->fault = NULL;
    return 1;
}
