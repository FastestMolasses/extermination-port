/* The mode-3 cue line walker 001FDDB0 (NEARMISS in the decomp: this follows
 * the .s) and the presenters' data file. See the header and
 * docs/MESSAGE_PRESENTER_REST.md. */
#include "game/em_message_presenter_rest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(EmMessageBlock) == 0x9C, "D_002821B0 request block");
_Static_assert(sizeof(EmMessageTextStyle) == 8, "style block");

/* ======================================================================
 * 001FDDB0
 * ====================================================================== */

static int fail(EmMprLineDraw *l, const char *why)
{
    if (!l->fault) l->fault = why;
    return -1;
}

static int draw_fault(EmMprLineDraw *l)
{
    return fail(l, l->draw->fault ? l->draw->fault : "message draw fault");
}

static uint32_t le32(const uint8_t *q)
{
    return (uint32_t)q[0] | (uint32_t)q[1] << 8 | (uint32_t)q[2] << 16 | (uint32_t)q[3] << 24;
}

static uint32_t blk(const EmMessageBlock *b, unsigned off)
{
    return le32((const uint8_t *)b + off);
}

static void set_blk(EmMessageBlock *b, unsigned off, uint32_t v)
{
    uint8_t *q = (uint8_t *)b + off;
    q[0] = (uint8_t)v;
    q[1] = (uint8_t)(v >> 8);
    q[2] = (uint8_t)(v >> 16);
    q[3] = (uint8_t)(v >> 24);
}

/* A cue record's word / byte through the slot value (the original's
 * pointer): the offset is slot - cue_address; the bank image starts
 * word-aligned, so a word at an offset that is not a multiple of 4 would be
 * an address error on the EE. */
static int record_word(EmMprLineDraw *l, uint32_t slot, uint32_t at, uint32_t *v)
{
    const EmMessageBank *b = &l->data->cue;
    const uint32_t off = slot - l->data->cue_address + at;
    if (!b->bytes || b->size < 4 || off > b->size - 4 || (off & 3))
        return fail(l, "cue record outside the cue bank");
    *v = le32(b->bytes + off);
    return 0;
}

static int record_byte(EmMprLineDraw *l, uint32_t slot, uint32_t at, uint8_t *v)
{
    const EmMessageBank *b = &l->data->cue;
    const uint32_t off = slot - l->data->cue_address + at;
    if (!b->bytes || off >= b->size) return fail(l, "cue record outside the cue bank");
    *v = b->bytes[off];
    return 0;
}

/* D_0026EC10[index] (the index is shifted left 2 and added as a word). */
static int color_at(EmMprLineDraw *l, uint32_t index, int32_t *color)
{
    const EmMessageDrawData *d = l->draw->data;
    if (!d->colors || index >= d->color_count) return fail(l, "D_0026EC10 index outside table");
    *color = d->colors[index];
    return 0;
}

/* The config block +0x20 names: 0 lets 001FC770 substitute its template;
 * &D_00264C90 (what 001FD0E0 stores there) is the supplied config. */
static int config_at(EmMprLineDraw *l, uint32_t address, const EmMessageDrawConfig **cfg)
{
    if (address == 0) {
        *cfg = NULL;
        return 0;
    }
    if (l->data->config_264C90 && address == l->data->config_264C90_address) {
        *cfg = l->data->config_264C90;
        return 0;
    }
    return fail(l, "block +0x20 names no supplied config");
}

int em_mpr_line_draw_init(EmMprLineDraw *l, EmMessageDraw *draw, const EmCsMessageData *data,
                          void *context,
                          int (*sound)(void *, int32_t, int32_t, int32_t, int32_t))
{
    if (!l || !draw || !draw->data || !data) return 0;
    memset(l, 0, sizeof *l);
    l->draw = draw;
    l->data = data;
    l->context = context;
    l->sound = sound;
    return 1;
}

int em_mpr_001FDDB0(EmMprLineDraw *l, const uint8_t *slots, int32_t count, const uint8_t *line,
                    EmMessageBlock *b, int32_t *result)
{
    if (!l) return -1;
    if (l->fault) return -1;
    if (!l->draw || !l->data || !l->draw->data) return fail(l, "line walker not bound");
    if (l->draw->fault) return fail(l, "message draw fault latched");
    if (!slots || !line || !b || !result) return fail(l, "001FDDB0 argument missing");
    EmMessageDraw *d = l->draw;
    EmMessageTextStyle *text = d->data->text;
    uint8_t *run = d->measure + EM_MPR_RUN;            /* D_00820F50 */
    int32_t x = (int32_t)blk(b, 0x24);
    const int32_t y = (int32_t)blk(b, 0x28);
    uint32_t width = 0, used = 0;
    memset(run, 0, EM_MPR_LINE);                       /* 00121A28 */

    for (int32_t i = 0;; ++i, ++used) {
        if (count < i || i >= EM_MPR_SLOTS) {
            *result = 1;
            return 0;
        }
        const uint32_t slot = le32(slots + 4 * i);
        if (slot != 0) {
            /* Draw the run so far at the pen, then move the pen by its width. */
            const EmMessageDrawConfig *cfg;
            if (config_at(l, blk(b, 0x20), &cfg)) return -1;
            if (em_message_draw_fc770(d, x, y, run, cfg) < 0) return draw_fault(l);
            x = (int32_t)((uint32_t)x + width);
            width = 0;
            used = 0;
            memset(run, 0, EM_MPR_LINE);               /* 00121A28 */

            uint32_t tag, arg, trigger;
            uint8_t byte;
            int32_t color;
            if (record_word(l, slot, 0, &tag)) return -1;
            if (tag == 0x20) {
                *result = 0;
                return 0;
            } else if (tag == 4) {
                if (record_word(l, slot, 4, &arg) || color_at(l, arg, &color)) return -1;
                text->color = color;
                if (record_byte(l, slot, 0xC, &byte)) return -1;
                text->flag = (uint8_t)(byte << 3);
            } else if (tag == 3) {
                if (record_byte(l, slot, 4, &byte)) return -1;
                text->flag = (uint8_t)(byte << 3);
            } else if (tag == 2) {
                if (record_word(l, slot, 4, &arg) || color_at(l, arg, &color)) return -1;
                text->color = color;
            } else if (tag == 1) {
                if (record_word(l, slot, 8, &trigger)) return -1;
                if (!(trigger < blk(b, 0x40))) {
                    const uint32_t mode = blk(b, 0x48);
                    if (mode == 1) {
                        set_blk(b, 0x58, blk(b, 0x58) - 1u);
                        if ((int32_t)blk(b, 0x58) > 0) {
                            *result = 0;
                            return 0;
                        }
                        set_blk(b, 0x48, 0);
                        set_blk(b, 0x58, 0);
                        set_blk(b, 0x40, blk(b, 0x40) + 1u);
                        continue;                      /* glyph i is skipped */
                    }
                    if (mode != 0) {
                        *result = 0;
                        return 0;
                    }
                    if (record_word(l, slot, 4, &arg)) return -1;
                    set_blk(b, 0x58, arg);
                    set_blk(b, 0x40, trigger);
                    if (blk(b, 0x58) == 0) {
                        set_blk(b, 0x40, blk(b, 0x40) + 1u);
                        continue;                      /* glyph i is skipped */
                    }
                    set_blk(b, 0x48, 1);
                    if (!l->sound) return fail(l, "001FB9F0 worker missing");
                    if (l->sound(l->context, 0x8C9, 0x1000, 0x1000, 0x1000) < 0)
                        return fail(l, "001FB9F0 worker failed");
                    *result = 0;
                    return 0;
                }
            }
            /* Tag 0, 2, 3, 4, any other tag and a passed gate: glyph i. */
        }
        /* Emit glyph i of the line into the run; its width through
         * 001CC170 of the two bytes {glyph, 0}. */
        if (used >= EM_MPR_LINE) return fail(l, "run store outside D_00820F50");
        const uint8_t pair[2] = { line[i], 0 };
        run[used] = pair[0];
        int32_t w;
        if (em_message_draw_cc170(d, pair, sizeof pair, &w) < 0) return draw_fault(l);
        width += (uint32_t)w;
    }
}

int em_mpr_worker_line_draw(void *l, const uint8_t *slots, int32_t count, uint8_t *line,
                            EmMessageBlock *block, int32_t *result)
{
    return em_mpr_001FDDB0(l, slots, count, line, block, result) == 0;
}

/* ======================================================================
 * .emmp v1 (tools/export_message_data.py)
 *
 *   0  "EMMP"   4 u32 version 1
 *   8  u32 help_size   12 u32 records_size   16 u32 cue_size
 *   20 u32 &D_00264C90 (the address token 001FD0E0 stores at +0x20)
 *   24 i32 D_00264CF0 words 0..4          44 i32 D_00264C90 words 0..4
 *   64 u8  D_00275830[8] (the style D_00264CF0 +0x14 points at)
 *   72 u8  D_00275820[8] (the style D_00264C90 +0x14 points at)
 *   80 u32 D_00264DB0[6]
 *   104 the help container *D_0028A498, the record container *D_0028A49C,
 *       the cue bank *D_0028A4EC
 * ====================================================================== */

enum { EMMP_HEADER = 104 };

static void style_from(EmMessageTextStyle *s, const uint8_t *q)
{
    s->color = (int32_t)le32(q);
    s->glyph = q[4];
    s->flag = q[5];
    s->pad[0] = q[6];
    s->pad[1] = q[7];
}

int em_mpr_data_parse(EmMprData *out, const uint8_t *image, size_t size, uint32_t cue_address)
{
    if (!out) return 0;
    memset(out, 0, sizeof *out);
    if (!image || size < EMMP_HEADER || memcmp(image, "EMMP", 4) || le32(image + 4) != 1) return 0;
    const uint32_t help = le32(image + 8), records = le32(image + 12), cue = le32(image + 16);
    if ((uint64_t)EMMP_HEADER + help + records + cue != size || (help & 3) || (records & 3) ||
        help < 16 || records < 16 || cue < 16)
        return 0;
    EmCsMessageData *cs = &out->cs;
    cs->help.bytes = image + EMMP_HEADER;
    cs->help.size = help;
    cs->records.bytes = cs->help.bytes + help;
    cs->records.size = records;
    cs->cue.bytes = cs->records.bytes + records;
    cs->cue.size = cue;
    cs->cue_address = cue_address;
    cs->config_264C90_address = le32(image + 20);
    for (int k = 0; k < 5; ++k) {
        out->config_264CF0.word[k] = (int32_t)le32(image + 24 + 4 * k);
        out->config_264C90.word[k] = (int32_t)le32(image + 44 + 4 * k);
    }
    style_from(&out->style_275830, image + 64);
    style_from(&out->style_275820, image + 72);
    out->config_264CF0.style = &out->style_275830;
    out->config_264C90.style = &out->style_275820;
    cs->config_264CF0 = &out->config_264CF0;
    cs->config_264C90 = &out->config_264C90;
    for (int k = 0; k < 6; ++k) cs->d264DB0[k] = le32(image + 80 + 4 * k);
    return 1;
}

int em_mpr_data_load(EmMprData *out, const char *path, uint32_t cue_address)
{
    if (!out) return 0;
    memset(out, 0, sizeof *out);
    if (!path) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint8_t *image = NULL;
    long size = -1;
    if (fseek(f, 0, SEEK_END) == 0) size = ftell(f);
    if (size > 0 && fseek(f, 0, SEEK_SET) == 0) {
        image = malloc((size_t)size);
        if (image && fread(image, 1, (size_t)size, f) != (size_t)size) {
            free(image);
            image = NULL;
        }
    }
    fclose(f);
    if (!image) return 0;
    if (!em_mpr_data_parse(out, image, (size_t)size, cue_address)) {
        free(image);
        return 0;
    }
    out->owned = image;
    return 1;
}

void em_mpr_data_free(EmMprData *data)
{
    if (!data) return;
    free(data->owned);
    memset(data, 0, sizeof *data);
}
