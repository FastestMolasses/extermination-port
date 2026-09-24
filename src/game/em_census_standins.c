/* Translations of the originals behind the census stand-ins that had none:
 * 00102CD0, 001FCB90, 001FCF60, 001FCF90, 001FE660, 001FD0E0, 0020CCB0,
 * 0021BAE0. See the header and docs/CENSUS_STANDINS.md. Each routine below
 * names the original it translates; 001FCF90 and 001FD0E0 are NEARMISS in
 * the decomp, so they follow the .s. */
#include "game/em_census_standins.h"

#include "game/em_ee_float.h"
#include "game/em_effect_original.h"
#include "game/em_owner_services_original.h"
#include "game/em_render_verify_rest.h"

#include <string.h>

_Static_assert(sizeof(EmMessageBlock) == 0x9C, "D_002821B0 request block");

/* ======================================================================
 * 00102CD0
 * ====================================================================== */

/* The frame's 20-word buffer: the matrix m (words 0..15) and the scratch
 * vector t (words 16..19). */
int em_cs_00102CD0(uint32_t out[16], const uint32_t pos[4], const uint32_t fwd[4],
                   const uint32_t up[4])
{
    float m[16], t[4], p[4], f[4], u[4];
    uint32_t mb[16];
    EmRvrFault fault = {0, 0};
    if (!out || !pos || !fwd || !up) return -1;
    memcpy(p, pos, sizeof p);
    memcpy(f, fwd, sizeof f);
    memcpy(u, up, sizeof u);
    memset(t, 0, sizeof t);
    /* 001029C0(m): the identity */
    if (em_owner_services_identity_001029C0(m) != EM_EE_FLOAT_OK) return -1;
    /* 00102718(t, up, fwd) */
    em_effect_original_00102718(t, u, f);
    /* 00102760(m row 0, t) */
    em_effect_original_00102760(m + 0, t);
    /* 00102760(m row 2, fwd) */
    em_effect_original_00102760(m + 8, f);
    /* 00102718(m row 1, m row 2, m row 0) */
    em_effect_original_00102718(m + 4, m + 8, m + 0);
    /* 00102918(m, m, pos): row 3 xyz += pos */
    if (em_owner_services_translate_00102918(m, m, p) != EM_EE_FLOAT_OK) return -1;
    /* 001027E0(out, m) */
    memcpy(mb, m, sizeof mb);
    if (em_rvr_001027E0(out, mb, &fault) != 0) return -1;
    return 0;
}

void em_cs_view_to_native(float native[16], const uint32_t view[16])
{
    uint32_t w[16];
    for (int k = 0; k < 16; ++k)
        w[k] = view[k] ^ ((k & 3) == 1 || (k & 3) == 2 ? EM_EE_SIGN : 0u);
    memcpy(native, w, sizeof w);
}

/* ======================================================================
 * Bank access (the original's absolute addresses become offsets from the
 * start of the supplied image; the images start word-aligned)
 * ====================================================================== */

static int fail(EmCsPresenters *p, const char *why)
{
    if (!p->fault) p->fault = why;
    return -1;
}

static int bank_word(const EmMessageBank *b, uint32_t off, uint32_t *value)
{
    if (!b->bytes || b->size < 4 || off > b->size - 4 || (off & 3)) return 0;
    const uint8_t *q = b->bytes + off;
    *value = (uint32_t)q[0] | (uint32_t)q[1] << 8 | (uint32_t)q[2] << 16 | (uint32_t)q[3] << 24;
    return 1;
}

static int bank_byte(const EmMessageBank *b, uint32_t off, uint8_t *value)
{
    if (!b->bytes || off >= b->size) return 0;
    *value = b->bytes[off];
    return 1;
}

/* A container entry: container + container[0] + *(container + entry). The
 * sub-bank starts word-aligned or the original's first lw of it would be
 * an address error. */
static int sub_bank(EmCsPresenters *p, const EmMessageBank *c, uint32_t entry, EmMessageBank *out)
{
    uint32_t w0, e;
    if (!bank_word(c, 0, &w0) || !bank_word(c, entry, &e))
        return fail(p, "container entry outside the container");
    uint32_t off = w0 + e;
    if (off >= c->size || (off & 3)) return fail(p, "sub-bank outside the container");
    out->bytes = c->bytes + off;
    out->size = c->size - off;
    return 0;
}

static int ready(EmCsPresenters *p)
{
    if (!p) return 0;
    if (p->fault) return 0;
    if (!p->draw || !p->data) { fail(p, "presenters not bound"); return 0; }
    if (p->draw->fault) { fail(p, "message draw fault latched"); return 0; }
    return 1;
}

static int draw_fault(EmCsPresenters *p)
{
    return fail(p, p->draw->fault ? p->draw->fault : "message draw fault");
}

int em_cs_presenters_init(EmCsPresenters *p, EmMessageDraw *draw, const EmCsMessageData *data,
                          const EmCsMode3Workers *mode3, int32_t *d282228)
{
    if (!p || !draw || !data) return 0;
    memset(p, 0, sizeof *p);
    p->draw = draw;
    p->data = data;
    if (mode3) p->mode3 = *mode3;
    p->d282228 = d282228;
    return 1;
}

/* ======================================================================
 * 001FCB90 / 001FCF60
 * ====================================================================== */

/* 001FCB90(x, y, group, line): the entry word is at +0x10 + (group << 4)
 * (a 32-bit shift and add), then a tail call of 001FE070(sub, line, x, y). */
int em_cs_001FCB90(EmCsPresenters *p, int32_t x, int32_t y, int32_t group, int32_t line,
                   int32_t *result)
{
    EmMessageBank sub;
    if (!ready(p)) return -1;
    if (sub_bank(p, &p->data->help, 0x10u + ((uint32_t)group << 4), &sub)) return -1;
    int r = em_message_draw_fe070(p->draw, &sub, line, x, y);
    if (r < 0) return draw_fault(p);
    if (result) *result = r;
    return 0;
}

/* 001FCF60(line, x, y): the entry word at +0x20, then the tail call. */
int em_cs_001FCF60(EmCsPresenters *p, int32_t line, int32_t x, int32_t y, int32_t *result)
{
    EmMessageBank sub;
    if (!ready(p)) return -1;
    if (sub_bank(p, &p->data->records, 0x20u, &sub)) return -1;
    int r = em_message_draw_fe070(p->draw, &sub, line, x, y);
    if (r < 0) return draw_fault(p);
    if (result) *result = r;
    return 0;
}

/* ======================================================================
 * 001FE660 / 001FCF90
 * ====================================================================== */

/* 001FE660(q): while the (signed) byte at q is not 0, q = 001FE530(NULL,
 * q, 0) and the count goes up. */
int em_cs_001FE660(EmCsPresenters *p, const EmMessageBank *bank, uint32_t offset,
                   int32_t *count)
{
    int32_t n = 0;
    uint8_t c;
    if (!ready(p)) return -1;
    if (!bank) return fail(p, "001FE660 bank missing");
    for (;;) {
        if (!bank_byte(bank, offset, &c)) return fail(p, "001FE660 read outside the bank");
        if (c == 0) break;
        uint32_t next;
        if (em_message_draw_fe530(p->draw, NULL, 0, bank->bytes + offset, bank->size - offset, 0,
                                  &next) < 0)
            return draw_fault(p);
        offset += next;
        n++;
    }
    if (count) *count = n;
    return 0;
}

/* 001FCF90(line, page): followed from the .s. The sub-bank is entry +0x30
 * of *D_0028A49C; 001FE460 is called and its count discarded; q =
 * 001FE480(sub, line); a string whose first byte is 0 draws nothing. Else
 * n = 001FE660(q) and, for i = 0 .. 9 while page * 10 + i < n (a 32-bit
 * sum, signed compare), segment i goes into the i-th 0x40 bytes of the
 * 0x280-byte buffer (the first pass skips page * 10 segments, the later
 * ones none) and is drawn by 001FC770(0x38, y, line, &D_00264CF0), y from
 * 0x2F, +0xC before every pass after the first. Returns 1. */
int em_cs_001FCF90(EmCsPresenters *p, int32_t line, int32_t page, int32_t *result)
{
    EmMessageBank sub;
    int32_t count;
    uint32_t q;
    uint8_t first;
    if (!ready(p)) return -1;
    if (!p->data->config_264CF0) return fail(p, "D_00264CF0 config missing");
    const int32_t base = (int32_t)((uint32_t)page * 10u);
    int32_t label = 0x2F;
    if (sub_bank(p, &p->data->records, 0x30u, &sub)) return -1;
    if (!em_message_bank_count(&sub, &count)) return fail(p, "001FE460 read outside the bank");
    if (!em_message_bank_string(&sub, line, &q)) return fail(p, "001FE480 read outside the bank");
    if (!bank_byte(&sub, q, &first)) return fail(p, "001FCF90 read outside the bank");
    if (first != 0) {
        int32_t n;
        if (em_cs_001FE660(p, &sub, q, &n) < 0) return -1;
        for (int32_t i = 0; i < 10 && (int32_t)((uint32_t)base + (uint32_t)i) < n; ++i) {
            uint8_t *dst = p->list + 0x40 * i;
            uint32_t next;
            if (q >= sub.size) return fail(p, "001FE530 source outside the bank");
            if (i != 0) label += 0xC;
            if (em_message_draw_fe530(p->draw, dst, (uint32_t)(EM_CS_FCF90_BUFFER - 0x40 * i),
                                      sub.bytes + q, sub.size - q, i ? 0 : base, &next) < 0)
                return draw_fault(p);
            q += next;
            if (em_message_draw_fc770(p->draw, 0x38, label, dst, p->data->config_264CF0) < 0)
                return draw_fault(p);
        }
    }
    if (result) *result = 1;
    return 0;
}

/* ======================================================================
 * 001FD0E0
 * ====================================================================== */

/* Frame layout (offsets from sp + 0xC0, the original's frame): */
enum { F_LINES = 0x000, F_SLOTS = 0x180, F_COUNTS = 0x780 };

static uint32_t blk(const EmMessageBlock *b, unsigned off)
{
    uint32_t v;
    memcpy(&v, (const uint8_t *)b + off, 4);
    return v;
}

static void set_blk(EmMessageBlock *b, unsigned off, uint32_t v)
{
    memcpy((uint8_t *)b + off, &v, 4);
}

static uint32_t frame_word(const EmCsPresenters *p, uint32_t off)
{
    uint32_t v;
    memcpy(&v, p->frame + off, 4);
    return v;
}

static int frame_store(EmCsPresenters *p, uint32_t off, const void *v, uint32_t size)
{
    if (off > EM_CS_FD0E0_FRAME || size > EM_CS_FD0E0_FRAME - off)
        return fail(p, "001FD0E0 store outside its frame");
    memcpy(p->frame + off, v, size);
    return 0;
}

/* 001FE480(*D_0028A4EC, index) as the original's pointer value. */
static int cue_string(EmCsPresenters *p, int32_t index, uint32_t *pointer)
{
    uint32_t off;
    if (!em_message_bank_string(&p->data->cue, index, &off))
        return fail(p, "001FE480 read outside the cue bank");
    *pointer = p->data->cue_address + off;
    return 0;
}

int em_cs_001FD0E0(EmCsPresenters *p, EmMessageBlock *b)
{
    if (!ready(p)) return -1;
    if (!b) return fail(p, "001FD0E0 block missing");
    const EmCsMessageData *d = p->data;
    const EmMessageBank *cue = &d->cue;
    /* Entry: the counter block D_00264DB0 into the frame, every state. */
    memcpy(p->frame + F_COUNTS, d->d264DB0, sizeof d->d264DB0);
    const uint32_t state = blk(b, 0x78);

    if (state == 0) {
        if (!p->mode3.reset) return fail(p, "001FC9B0 worker missing");
        for (int i = 0; i < 4; ++i) p->d820EC0[i] = blk(b, 4u * i);
        if (!p->mode3.reset(p->mode3.context)) return fail(p, "001FC9B0 worker failed");
        for (int i = 0; i < 4; ++i) set_blk(b, 4u * i, p->d820EC0[i]);
        uint32_t s;
        if (cue_string(p, (int32_t)blk(b, 0x34), &s)) return -1;
        set_blk(b, 0x18, s);
        set_blk(b, 0x3C, blk(b, 0x08));
        set_blk(b, 0x78, 1);
        set_blk(b, 0x48, 0);
        return 0;
    }
    if (state != 1) return 0;   /* 2: consumed; any other value: nothing */

    if (!p->mode3.line_draw) return fail(p, "001FDDB0 worker missing");
    if (!p->d282228) return fail(p, "D_00282228 missing");
    if (!d->config_264C90) return fail(p, "D_00264C90 config missing");
    if (blk(b, 0x3C) != blk(b, 0x08)) {
        *p->d282228 = 0;
        return 0;
    }
    memset(p->frame + F_LINES, 0, 0x180);   /* 00121A28 (C runtime memset) */
    memset(p->frame + F_SLOTS, 0, 0x600);
    set_blk(b, 0x34, blk(b, 0x08));
    uint32_t text;
    if (cue_string(p, (int32_t)blk(b, 0x34), &text)) return -1;
    set_blk(b, 0x1C, text);

    /* First pass: four lines. `lines` is the stop counter (4, one less per
     * 0x0A); 0x0C and 0 zero it. `off` runs over the whole string. */
    int32_t lines = 4, consumed = 0;
    uint32_t off = 0;
    for (uint32_t line = 0; line < 4; ++line) {
        const uint32_t counter = F_COUNTS + 4 * line;
        while (lines != 0) {
            int found = 0;
            uint32_t roff = 0, trigger;
            if (!em_message_bank_record(cue, (int32_t)blk(b, 0x34), blk(b, 0x44), &found, &roff))
                return fail(p, "001FE4D0 read outside the cue bank");
            if (found) {
                if (!bank_word(cue, roff + 8, &trigger))
                    return fail(p, "cue record outside the cue bank");
                if (trigger == blk(b, 0x38) + off) {
                    const uint32_t slot = d->cue_address + roff;
                    consumed += 1;
                    if (frame_store(p, F_SLOTS + 0x100 * line + (frame_word(p, counter) << 2),
                                    &slot, 4))
                        return -1;
                    set_blk(b, 0x44, blk(b, 0x44) + 1);
                }
            }
            uint8_t c;
            if (!bank_byte(cue, blk(b, 0x1C) + off - d->cue_address, &c))
                return fail(p, "cue text outside the cue bank");
            if (c == 0) { lines = 0; break; }
            if (c == 0x0C) { off += 1; lines = 0; break; }
            if (c == 0x0A) { off += 1; lines -= 1; break; }
            const uint32_t k = frame_word(p, counter);
            if (frame_store(p, F_LINES + 0x40 * line + k, &c, 1)) return -1;
            const uint32_t k1 = frame_word(p, counter) + 1;
            if (frame_store(p, counter, &k1, 4)) return -1;
            off += 1;
        }
    }
    set_blk(b, 0x44, blk(b, 0x44) - (uint32_t)consumed);
    set_blk(b, 0x24, 0x37);
    set_blk(b, 0x28, 0x1C);
    set_blk(b, 0x20, d->config_264C90_address);

    /* Second pass: 001FDDB0 per line. */
    for (uint32_t line = 0; line < 4; ++line) {
        int32_t r = 0;
        set_blk(b, 0x2C, p->frame_address + 0x40 * line);
        if (!p->mode3.line_draw(p->mode3.context, p->frame + F_SLOTS + 0x100 * line,
                                (int32_t)frame_word(p, F_COUNTS + 4 * line),
                                p->frame + F_LINES + 0x40 * line, b, &r))
            return fail(p, "001FDDB0 worker failed");
        if (r == 2 || r == 0) return 0;
        const EmMessageDrawConfig *cfg = d->config_264C90;
        const uint32_t sum = (uint32_t)cfg->word[2] + (uint32_t)cfg->word[4];
        const int32_t half = (int32_t)(sum & 0x80000000u ? ~(~sum >> 1) : sum >> 1);
        set_blk(b, 0x28, blk(b, 0x28) + (uint32_t)half);
    }
    set_blk(b, 0x34, blk(b, 0x08) + 1);
    if (em_message_draw_fe070(p->draw, cue, (int32_t)blk(b, 0x34), 0x39, 0x95) < 0)
        return draw_fault(p);
    set_blk(b, 0x10, 1);
    return 0;
}

/* ======================================================================
 * EmMessageWorkers adapters
 * ====================================================================== */

int em_cs_worker_mode3_present(void *p, EmMessageBlock *block)
{
    return em_cs_001FD0E0(p, block) == 0;
}

int em_cs_worker_help_draw(void *p, int x, int y, int32_t group, uint32_t line)
{
    return em_cs_001FCB90(p, x, y, group, (int32_t)line, NULL) == 0;
}

int em_cs_worker_record_setup(void *p, uint32_t line, int32_t page, int32_t group,
                              int32_t *result)
{
    (void)group;
    return em_cs_001FCF90(p, (int32_t)line, page, result) == 0;
}

int em_cs_worker_record_draw(void *p, uint32_t line, int x, int y)
{
    return em_cs_001FCF60(p, (int32_t)line, x, y, NULL) == 0;
}

/* ======================================================================
 * 0020CCB0
 * ====================================================================== */

int em_cs_0020CCB0(const EmCsRectWorkers *w, const uint8_t *page, size_t page_size)
{
    if (!w || !w->float_to_int || !w->rectangle || !page || page_size < 7) return -1;
    const int32_t n = page[6] == 0 ? 0xFD : 0x14F;
    const uint32_t sixteen = UINT32_C(0x41800000);
    int32_t x0, x1;
    /* mul.s 16.0 * cvt.s.w(n + 0x700), then float_to_int */
    if (w->float_to_int(w->context, em_ee_mul_bits(sixteen, em_ee_cvt_s_w_bits((uint32_t)(n + 0x700))),
                        &x0) < 0)
        return -1;
    if (w->float_to_int(w->context, em_ee_mul_bits(sixteen, em_ee_cvt_s_w_bits((uint32_t)(n + 0x70C))),
                        &x1) < 0)
        return -1;
    return w->rectangle(w->context, 1, x0, 0x85E0, x1, 0x8640, UINT32_C(0x80CE6000)) < 0 ? -1 : 0;
}

/* ======================================================================
 * 0021BAE0
 * ====================================================================== */

int em_cs_0021BAE0(const EmSulWorkers *w, uint8_t *context, size_t context_size, int32_t slot)
{
    /* the slot is shifted left 5 as a 32-bit word before the add */
    const int64_t src = (int64_t)(int32_t)((uint32_t)slot << 5) + 0x120;
    if (!w || !w->block_copy || !context || context_size < 0xC0 || src < 0 ||
        (uint64_t)src + 0x20u > context_size)
        return -1;
    return w->block_copy(w->context, context, 0xA0u, (uint32_t)src, 0x20) < 0 ? -1 : 0;
}
