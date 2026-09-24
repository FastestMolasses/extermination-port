/* em_status_ui_leftovers.c - see em_status_ui_leftovers.h and
 * docs/STATUS_UI_LEFTOVERS.md. Every routine cites its original address;
 * the comments describe what the original does, never its instructions. */
#include "game/em_status_ui_leftovers.h"

#include "game/em_ee_float.h"

#include <string.h>

/* ------------------------------------------------------------------------
 * Shared plumbing
 * ---------------------------------------------------------------------- */

/* One execution of a routine. In a dry run nothing is written and no
 * worker is called; it only proves that every address the real run will
 * touch is available (the control flow of the list and the title does not
 * depend on any worker result). */
typedef struct {
    const EmSulWorkers *w;
    const EmSulMemory *mem;
    int dry;
    int fault;
} Run;

static const uint8_t *region_at(const EmSulMemory *mem, uint32_t address, uint32_t size)
{
    if (!mem || !mem->regions)
        return NULL;
    for (unsigned i = 0; i < mem->count; ++i) {
        const EmSulRegion *r = &mem->regions[i];
        if (!r->bytes || address < r->base)
            continue;
        uint32_t at = address - r->base;
        if (at <= r->size && size <= r->size - at)
            return r->bytes + at;
    }
    return NULL;
}

static uint32_t mem_u8(Run *run, uint32_t address)
{
    const uint8_t *p = region_at(run->mem, address, 1);
    if (!p) {
        run->fault = 1;
        return 0;
    }
    return p[0];
}

static uint32_t mem_u16(Run *run, uint32_t address)
{
    const uint8_t *p = region_at(run->mem, address, 2);
    if (!p || (address & 1u)) { /* a misaligned halfword load raises on the EE */
        run->fault = 1;
        return 0;
    }
    return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static uint32_t mem_u32(Run *run, uint32_t address)
{
    const uint8_t *p = region_at(run->mem, address, 4);
    if (!p || (address & 3u)) {
        run->fault = 1;
        return 0;
    }
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t mem_u64(Run *run, uint32_t address)
{
    const uint8_t *p = region_at(run->mem, address, 8);
    if (!p || (address & 7u)) {
        run->fault = 1;
        return 0;
    }
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i)
        v = v << 8 | p[i];
    return v;
}

/* Byte access to a caller block (page, actor record). */
static uint32_t blk_u8(Run *run, const uint8_t *block, size_t size, size_t at)
{
    if (at >= size) {
        run->fault = 1;
        return 0;
    }
    return block[at];
}

static void blk_put8(Run *run, uint8_t *block, size_t size, size_t at, uint32_t value)
{
    if (at >= size) {
        run->fault = 1;
        return;
    }
    block[at] = (uint8_t)value;
}

static int32_t blk_s16(Run *run, const uint8_t *block, size_t size, size_t at)
{
    if (at + 2 > size || (at & 1u)) {
        run->fault = 1;
        return 0;
    }
    return (int16_t)(uint16_t)(block[at] | block[at + 1] << 8);
}

static void blk_put16(Run *run, uint8_t *block, size_t size, size_t at, uint32_t value)
{
    if (at + 2 > size || (at & 1u)) {
        run->fault = 1;
        return;
    }
    block[at] = (uint8_t)value;
    block[at + 1] = (uint8_t)(value >> 8);
}

static int32_t blk_s32(Run *run, const uint8_t *block, size_t size, size_t at)
{
    if (at + 4 > size || (at & 3u)) {
        run->fault = 1;
        return 0;
    }
    return (int32_t)((uint32_t)block[at] | (uint32_t)block[at + 1] << 8 |
                     (uint32_t)block[at + 2] << 16 | (uint32_t)block[at + 3] << 24);
}

static void blk_put32(Run *run, uint8_t *block, size_t size, size_t at, uint32_t value)
{
    if (at + 4 > size || (at & 3u)) {
        run->fault = 1;
        return;
    }
    for (int i = 0; i < 4; ++i)
        block[at + (size_t)i] = (uint8_t)(value >> (8 * i));
}

/* Worker call: skipped in a dry run, a negative result is a fault. */
#define WORK(run, call)                                                                        \
    do {                                                                                       \
        if (!(run)->dry && !(run)->fault && (call) < 0)                                        \
            (run)->fault = 1;                                                                  \
    } while (0)

#define FAILED(run) ((run)->fault)

/* 16.0f * (float)n through the EE COP1 model (the list's y coordinate:
 * the integer is converted, then multiplied by 16.0f). */
static uint32_t list_y_bits(int32_t n)
{
    return em_ee_mul_bits(UINT32_C(0x41800000), em_ee_cvt_s_w_bits((uint32_t)n));
}

/* ------------------------------------------------------------------------
 * UI cues 0020CD40 / 0020CD60 / 0020CDA0: each is one tail call of
 * 001FB9F0(id, 0x1000, 0x1000, 0x1000) with id 0, 1 and 4.
 * ---------------------------------------------------------------------- */

static int cue(const EmSulWorkers *w, int32_t id)
{
    if (!w || !w->sound)
        return -1;
    return w->sound(w->context, id, 0x1000, 0x1000, 0x1000) < 0 ? -1 : 0;
}

int em_sul_0020CD40(const EmSulWorkers *w) { return cue(w, 0); }
int em_sul_0020CD60(const EmSulWorkers *w) { return cue(w, 1); }
int em_sul_0020CDA0(const EmSulWorkers *w) { return cue(w, 4); }

static void run_cue_0020CDA0(Run *run)
{
    WORK(run, em_sul_0020CDA0(run->w));
}

/* ------------------------------------------------------------------------
 * 0020BEF0: v = page[+0x17] + page[+0x19]; when v is not below page[+0x18]
 * it is reduced by page[+0x18] once (a signed compare of the byte sums).
 * ---------------------------------------------------------------------- */

static int32_t ring_index(Run *run, const uint8_t *page, size_t size)
{
    int32_t cursor = (int32_t)blk_u8(run, page, size, 0x17);
    int32_t head = (int32_t)blk_u8(run, page, size, 0x19);
    int32_t count = (int32_t)blk_u8(run, page, size, 0x18);
    int32_t v = cursor + head;
    if (!(v < count))
        v -= count;
    return v;
}

int em_sul_0020BEF0(const uint8_t *page, size_t page_size, int32_t *index)
{
    Run run = {0};
    if (!page || !index)
        return -1;
    int32_t v = ring_index(&run, page, page_size);
    if (run.fault)
        return -1;
    *index = v;
    return 0;
}

/* ------------------------------------------------------------------------
 * 0020AE40(page, table, flags)
 * ---------------------------------------------------------------------- */

int em_sul_0020AE40(const EmSulWorkers *w, const EmSulMemory *mem, uint32_t page,
                    uint32_t table, int32_t flags)
{
    if (!w || !w->blend || !w->sprite)
        return -1;
    if ((flags & 0x8) && !w->health)
        return -1;
    if (!(flags & 0x8) && (flags & 0x2) && !w->battery)
        return -1;
    Run run = {w, mem, 0, 0};
    /* Every TEX0 word the call sequence will read, read up front. */
    uint64_t t[16];
    for (unsigned i = 0; i < 16; ++i) {
        unsigned off = i * 8u;
        int used = off == 0x00 || off == 0x08 || off == 0x10 || off == 0x18 || off == 0x38 ||
                   off == 0x68 || ((flags & 0x40) && off == 0x40) ||
                   (!(flags & 0x40) && (off == 0x20 || off == 0x28 || off == 0x30)) ||
                   ((flags & 0x2) && off == 0x78) ||
                   (!(flags & 0x8) && (flags & 0x2) && off == 0x70);
        t[i] = used ? mem_u64(&run, table + off) : 0;
    }
    if (run.fault)
        return -1;
#define T(off) t[(off) / 8]
    WORK(&run, w->blend(w->context, 1, 0));
    WORK(&run, w->sprite(w->context, 1, 0x7000, 0x7B40, 0x100, 0x80, 0x40808080u, T(0x00)));
    WORK(&run, w->sprite(w->context, 1, 0x7000, 0x7F40, 0x100, 0x80, 0x40808080u, T(0x10)));
    WORK(&run, w->sprite(w->context, 1, 0x8000, 0x7B40, 0x100, 0x80, 0x40808080u, T(0x08)));
    WORK(&run, w->sprite(w->context, 1, 0x8000, 0x7F40, 0x100, 0x80, 0x40808080u, T(0x18)));
    int wide = flags & 0x40;
    if (wide) {
        WORK(&run, w->sprite(w->context, 1, 0x7800, 0x8300, 0x100, 0x80, 0x40808080u, T(0x40)));
    } else {
        WORK(&run, w->sprite(w->context, 1, 0x7000, 0x8300, 0x100, 0x80, 0x40808080u, T(0x20)));
        WORK(&run, w->sprite(w->context, 1, 0x8000, 0x8300, 0x100, 0x80, 0x40808080u, T(0x28)));
    }
    int gauge = flags & 0x2;
    if (gauge)
        WORK(&run, w->sprite(w->context, 1, 0x7800, 0x7E00, 0x100, 0x80, 0x80808080u, T(0x78)));
    if (flags & 0x8)
        WORK(&run, w->health(w->context, page, 0x1B6, 0x6E));
    else if (gauge)
        WORK(&run, w->battery(w->context, page, 0x96, 0xB4, T(0x70), 1));
    WORK(&run, w->blend(w->context, 1, 3));
    WORK(&run, w->sprite(w->context, 1, 0x7000, 0x8300, 0x80, 0x80, 0x80808080u, T(0x38)));
    if (!wide)
        WORK(&run, w->sprite(w->context, 1, 0x8780, 0x8300, 0x80, 0x80, 0x80808080u, T(0x30)));
    /* the whole word is compared: exactly 0x20 moves the plate right */
    WORK(&run, w->sprite(w->context, 1, flags == 0x20 ? 0x71D0 : 0x7100, 0x7900, 0x100, 0x40,
                         0x80808080u, T(0x68)));
#undef T
    return run.fault ? -1 : 0;
}

/* ------------------------------------------------------------------------
 * 0020B0D0(unused, table): 00207D00(1, 3), then the up and down arrows.
 * Held bit 0x1000 selects +0x58 (up lit) with +0x50; else 0x4000 selects
 * +0x48 with +0x60 (down lit); else +0x48 with +0x50.
 * ---------------------------------------------------------------------- */

int em_sul_0020B0D0(const EmSulWorkers *w, const EmSulMemory *mem, uint32_t table)
{
    if (!w || !w->blend || !w->sprite)
        return -1;
    Run run = {w, mem, 0, 0};
    uint32_t held = mem_u16(&run, 0x00810E70u);
    uint32_t up = (held & 0x1000) ? 0x58 : 0x48;
    uint32_t down = (!(held & 0x1000) && (held & 0x4000)) ? 0x60 : 0x50;
    uint64_t a = mem_u64(&run, table + up), b = mem_u64(&run, table + down);
    if (run.fault)
        return -1;
    WORK(&run, w->blend(w->context, 1, 3));
    WORK(&run, w->sprite(w->context, 1, 0x7800, 0x7B30, 0x20, 0x20, 0x80808080u, a));
    WORK(&run, w->sprite(w->context, 1, 0x7800, 0x8240, 0x20, 0x20, 0x80808080u, b));
    return run.fault ? -1 : 0;
}

/* ------------------------------------------------------------------------
 * 0020B210(page, table, glyph, flags)
 * ---------------------------------------------------------------------- */

typedef struct {
    uint8_t *p;
    size_t n;
    uint32_t table;
    uint64_t glyph;
    int32_t flags;
    EmSulListGlobals *g;
    int32_t result;
} List;

#define P(at) blk_u8(run, L->p, L->n, (size_t)(at))
#define PUT(at, v) blk_put8(run, L->p, L->n, (size_t)(at), (uint32_t)(v))

/* The two row sprites at x 0x77F0 / 0x7FF0 of visible row j (the row
 * record's +0 and +8 TEX0; float_to_int is called once per sprite). */
static void list_row(Run *run, List *L, int32_t j, uint32_t y_bits, uint32_t color)
{
    const EmSulWorkers *w = run->w;
    int32_t y = 0;
    WORK(run, w->float_to_int(w->context, y_bits, &y));
    uint64_t t0 = mem_u64(run, L->table + P(0x90 + j) * 24u);
    WORK(run, w->sprite(w->context, 1, 0x77F0, y, 0x80, 0x40, color, t0));
    WORK(run, w->float_to_int(w->context, y_bits, &y));
    uint64_t t1 = mem_u64(run, L->table + P(0x90 + j) * 24u + 8u);
    WORK(run, w->sprite(w->context, 1, 0x7FF0, y, 0x80, 0x40, color, t1));
}

static void list_body(Run *run, List *L)
{
    const EmSulWorkers *w = run->w;
    int32_t flags = L->flags;
    int32_t spB0 = 0, spC0 = 0, k = 0;

    /* Phase 1: cursor input (D_00810E78 bit 0x1000 up, 0x4000 down). */
    PUT(0x1A, 0);
    int32_t n = (int32_t)P(0x18);
    if (n != 0 && !(flags & 0x400)) {
        uint32_t edge = mem_u16(run, 0x00810E78u);
        if (edge & 0x1000) {
            if (P(0x17) == 0) {
                spC0 = 1;
                PUT(0x1A, spC0);
                if (!((int32_t)P(0x18) < 5))
                    run_cue_0020CDA0(run);
            } else {
                run_cue_0020CDA0(run);
                PUT(0x17, P(0x17) - 1u);
            }
        } else if (edge & 0x4000) {
            if (n < 4) {
                int32_t last = n - 1;
                int32_t c = (int32_t)P(0x17);
                if (c < last) {
                    PUT(0x17, c + 1);
                    run_cue_0020CDA0(run);
                } else {
                    PUT(0x17, last);
                }
            } else {
                int32_t c = (int32_t)P(0x17);
                if (!(c < 3)) {
                    PUT(0x17, 3);
                    PUT(0x1A, 2);
                    spC0 = 1;
                    if (!((int32_t)P(0x18) < 5))
                        run_cue_0020CDA0(run);
                } else {
                    PUT(0x17, c + 1);
                    run_cue_0020CDA0(run);
                }
            }
        }
    }

    /* Phase 2: refill up to five visible rows +0x90.. from the ring +0x50..
     * starting at the head +0x19, wrapping at the count +0x18. */
    int32_t ring = (int32_t)P(0x19);
    flags &= ~0x400;
    for (int32_t i = 0; i < (int32_t)P(0x18) && i < 5 && !FAILED(run); ++i) {
        uint32_t v = P(0x50 + ring);
        ring += 1;
        PUT(0x90 + i, v);
        if (!(ring < (int32_t)P(0x18)))
            ring = 0;
    }

    /* Phase 3: up to four rows, 0x30 GS units apart. */
    WORK(run, w->blend(w->context, 1, 0));
    if (flags & 0x1F0) {
        /* compare modes: the committed value, 0xFF when +0x12 is 0xFF */
        uint32_t st = P(0x12) == 0xFF ? 0xFFu : ((P(0x12) - P(0x1E)) & 0xFFu);
        if (flags & 0x20) {
            /* tri-state: value 2 lights every row at or above it */
            for (int32_t j = 0; j < (int32_t)P(0x18) && j < 4 && !FAILED(run); ++j) {
                uint32_t v = P(0x90 + j), color;
                if (st == 2) {
                    if (P(0x17) == (uint32_t)j)
                        color = (int32_t)v < (int32_t)st ? 0x80808080u : 0x80208080u;
                    else
                        color = (int32_t)v < (int32_t)st ? 0x40404040u : 0x40208080u;
                } else {
                    if (P(0x17) == (uint32_t)j)
                        color = v != st ? 0x80808080u : 0x80208080u;
                    else
                        color = v != st ? 0x40404040u : 0x40208080u;
                }
                list_row(run, L, j, list_y_bits(((k + 0x6A) >> 1) + 0x790), color);
                k += 0x30;
            }
        } else {
            for (int32_t j = 0; j < (int32_t)P(0x18) && j < 4 && !FAILED(run); ++j) {
                uint32_t v = P(0x90 + j), color;
                if (P(0x17) == (uint32_t)j)
                    color = v != st ? 0x80808080u : 0x80208080u;
                else
                    color = v != st ? 0x40404040u : 0x40208080u;
                list_row(run, L, j, list_y_bits(((k + 0x6A) >> 1) + 0x790), color);
                k += 0x30;
            }
        }
    } else {
        /* plain mode, with the per-row number when flags & 8 */
        for (int32_t j = 0; j < (int32_t)P(0x18) && j < 4 && !FAILED(run); ++j) {
            uint32_t color, style;
            if (P(0x17) == (uint32_t)j) {
                color = 0x80808080u;
                style = EM_SUL_STYLE_00265510;
            } else {
                color = 0x40404040u;
                style = EM_SUL_STYLE_00265518;
            }
            list_row(run, L, j, list_y_bits(((k + 0x6A) >> 1) + 0x790), color);
            if (flags & 8) {
                /* the byte at D_00810700 + row value + (short)+0x1E + 0x564 */
                uint32_t address = 0x00810700u + P(0x90 + j) +
                                   (uint32_t)blk_s16(run, L->p, L->n, 0x1E) + 0x564u;
                int32_t value = (int32_t)mem_u8(run, address);
                const char *text = NULL;
                char spE0[16];
                memset(spE0, 0, sizeof spE0);
                WORK(run, w->format(w->context, value, 2, 0, &text));
                if (!run->dry && !run->fault && !text)
                    run->fault = 1;
                WORK(run, w->copy(w->context, spE0, sizeof spE0, text));
                int32_t y = 0;
                WORK(run, w->float_to_int(w->context, list_y_bits(((k + 0x86) >> 1) + 0x790), &y));
                WORK(run, w->text_fixed(w->context, 1, 0x7AF, y >> 4, 0xC, 0xC, spE0, style));
            }
            k += 0x30;
        }
    }

    /* Phase 4: commit, cursor highlight, selected-row marker. */
    if (P(0x18) != 0) {
        if (!(flags & 0x200)) {
            if (!run->dry)
                L->g->d2821B4 = 1;
            uint32_t row = P(0x90 + P(0x17));
            int32_t b8;
            if (flags & 0x40) {
                if (row == 2)
                    b8 = blk_s16(run, L->p, L->n, 0x1E) + (int32_t)row;
                else
                    b8 = row == 0 ? 0x3D : 0x3E;
            } else if (flags & 0x20) {
                b8 = blk_s16(run, L->p, L->n, 0x1E) + (int32_t)row;
                if (b8 == 0xC || (uint32_t)(b8 - 0xD) < 2u) {
                    spB0 = (int32_t)mem_u8(run, 0x00810C70u) + (int32_t)mem_u8(run, 0x00810C71u);
                    spB0 = spB0 + (int32_t)mem_u8(run, 0x00810C72u);
                    if (spB0 == 3)
                        b8 = 0x3C;
                }
            } else {
                b8 = blk_s16(run, L->p, L->n, 0x1E) + (int32_t)row;
            }
            if (!run->dry)
                L->g->d2821B8 = b8;
        }
        int32_t ring_at = ring_index(run, L->p, L->n);
        WORK(run, w->blend(w->context, 1, 0));
        int32_t y = 0;
        WORK(run, w->float_to_int(w->context,
                                  list_y_bits((((int32_t)P(0x17) * 0x30 + 0x6A) >> 1) + 0x790), &y));
        WORK(run, w->sprite(w->context, 1, 0x77F0, y, 0x100, 0x40, 0x20808080u, L->glyph));
        int marker = !(flags & 0x40) && (!(flags & 0x20) || spB0 == 0 || spB0 == 3);
        if (marker) {
            uint64_t t = mem_u64(run, L->table + P(0x50 + ring_at) * 24u + 0x10u);
            WORK(run, w->sprite(w->context, 1, 0x89F0, 0x83E0, 0x40, 0x40, 0x40808080u, t));
        }
        if (L->g->d282240 == 4 && P(0x1B) != P(0x50 + ring_at) && !run->dry)
            L->g->d282240 = 3;
    } else if (!run->dry) {
        L->g->d2821B4 = 0;
    }
    L->result = (int32_t)P(0x18) < 5 ? 0 : spC0;
}

#undef P
#undef PUT

int em_sul_0020B210(const EmSulWorkers *w, const EmSulMemory *mem, uint8_t *page,
                    size_t page_size, uint32_t table, uint64_t glyph, int32_t flags,
                    EmSulListGlobals *globals, int32_t *result)
{
    if (!w || !w->blend || !w->sprite || !w->float_to_int || !w->sound || !page || !globals ||
        !result || page_size > EM_SUL_PAGE_MAX)
        return -1;
    if (!(flags & 0x1F0) && (flags & 8) && (!w->format || !w->copy || !w->text_fixed))
        return -1;
    /* Dry run over a copy of the page and globals: every address first. */
    uint8_t copy[EM_SUL_PAGE_MAX];
    memcpy(copy, page, page_size);
    EmSulListGlobals g = *globals;
    Run dry = {w, mem, 1, 0};
    List L = {copy, page_size, table, glyph, flags, &g, 0};
    list_body(&dry, &L);
    if (dry.fault)
        return -1;
    Run run = {w, mem, 0, 0};
    List live = {page, page_size, table, glyph, flags, globals, 0};
    list_body(&run, &live);
    if (run.fault)
        return -1;
    *result = live.result;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001C5860: f = 100.0f - D_008104D8; the first of 80, 50, 30, 10, 0 that f
 * is not at or below gives 0..4; otherwise 5 (EE compares).
 * ---------------------------------------------------------------------- */

int32_t em_sul_001C5860(uint32_t d8104D8)
{
    static const uint32_t limit[5] = {0x42A00000u, 0x42480000u, 0x41F00000u, 0x41200000u, 0u};
    uint32_t f = em_ee_sub_bits(UINT32_C(0x42C80000), d8104D8);
    for (int32_t band = 0; band < 5; ++band)
        if (!em_ee_c_le_bits(f, limit[band]))
            return band;
    return 5;
}

/* ------------------------------------------------------------------------
 * 001C5930(actor): the area-title node.
 *   +0x04 state: 0 init, 1 run, 2 / 3 free (001AFC10), other: nothing.
 *   +0x05 title line phase, +0x06 band line phase, +0x28 title timer,
 *   +0x2A title string index, +0x1F0 band, +0x1F4 band line timer.
 * ---------------------------------------------------------------------- */

typedef struct {
    uint8_t *a;
    size_t n;
} Node;

#define A8(at) blk_u8(run, N->a, N->n, (at))
#define APUT8(at, v) blk_put8(run, N->a, N->n, (at), (uint32_t)(v))

static int32_t band_now(Run *run)
{
    return em_sul_001C5860(mem_u32(run, 0x008104D8u));
}

/* One centred proportional line on row 0x7A2 (001CC170 then 001CC1E0). */
static void title_line(Run *run, uint32_t text, int32_t centre, uint32_t style)
{
    const EmSulWorkers *w = run->w;
    int32_t width = 0;
    WORK(run, w->text_width(w->context, text, &width));
    WORK(run, w->text_proportional(w->context, 1, centre - (width >> 1), 0x7A2, 0xA, 0x14, text,
                                   style));
}

static void title_body(Run *run, Node *N)
{
    const EmSulWorkers *w = run->w;
    uint32_t state = A8(4);
    if (state == 3 || state == 2) {
        WORK(run, w->free_actor(w->context, N->a));
        return;
    }
    if (state == 0) {
        blk_put16(run, N->a, N->n, 0x28, 0x12C);
        uint32_t area = mem_u8(run, 0x00810700u);
        uint32_t base = mem_u16(run, 0x00289B40u + area * 4u);
        blk_put16(run, N->a, N->n, 0x2A, base);
        uint32_t sub = mem_u8(run, 0x00810701u);
        int32_t index = blk_s16(run, N->a, N->n, 0x2A);
        blk_put16(run, N->a, N->n, 0x2A, (uint32_t)(index + (int32_t)sub));
        APUT8(4, A8(4) + 1u);
        int32_t band = band_now(run);
        blk_put32(run, N->a, N->n, 0x1F0, (uint32_t)band);
        blk_put16(run, N->a, N->n, 0x1F4, 0x12C);
        return;
    }
    if (state != 1)
        return;
    /* the scratchpad mode byte 1, 2 or 3 suppresses both lines */
    uint32_t mode = mem_u8(run, 0x70003B8Du);
    int quiet = mode == 1 || (uint32_t)(mode - 2u) < 2u;
    uint32_t phase = A8(5);
    if (phase == 0) {
        if (!quiet) {
            int32_t index = blk_s16(run, N->a, N->n, 0x2A);
            uint32_t text = mem_u32(run, 0x002671C0u + (uint32_t)index * 4u);
            title_line(run, text, 0x800, 0);
        }
        int32_t timer = blk_s16(run, N->a, N->n, 0x28) - 1;
        blk_put16(run, N->a, N->n, 0x28, (uint32_t)timer);
        if ((int16_t)timer == 0)
            APUT8(5, A8(5) + 1u);
    }
    if (phase == 0 || phase == 1) {
        if (mem_u8(run, 0x008106B8u) != 0)
            APUT8(4, 3);
    }
    if (quiet)
        return;
    uint32_t line = A8(6);
    if (line == 1) {
        int32_t band = band_now(run);
        if (blk_s32(run, N->a, N->n, 0x1F0) != band) {
            blk_put32(run, N->a, N->n, 0x1F0, (uint32_t)band);
            APUT8(6, 0);
        }
        return;
    }
    if (line != 0)
        return;
    int32_t band = blk_s32(run, N->a, N->n, 0x1F0);
    if (band == 0) {
        blk_put16(run, N->a, N->n, 0x1F4, 0x12C);
        APUT8(6, A8(6) + 1u);
        return;
    }
    if (A8(5) != 1)
        return;
    uint32_t text = mem_u32(run, 0x0026726Cu + (uint32_t)band * 4u);
    title_line(run, text, 0x896, band == 5 ? EM_SUL_STYLE_00265520 : 0u);
    int32_t timer = blk_s16(run, N->a, N->n, 0x1F4) - 1;
    blk_put16(run, N->a, N->n, 0x1F4, (uint32_t)timer);
    if ((int16_t)timer == 0) {
        blk_put16(run, N->a, N->n, 0x1F4, 0x12C);
        APUT8(6, A8(6) + 1u);
        return;
    }
    int32_t now = band_now(run);
    if (blk_s32(run, N->a, N->n, 0x1F0) == now)
        return;
    blk_put32(run, N->a, N->n, 0x1F0, (uint32_t)now);
    blk_put16(run, N->a, N->n, 0x1F4, 0x12C);
}

#undef A8
#undef APUT8

int em_sul_001C5930(const EmSulWorkers *w, const EmSulMemory *mem, uint8_t *actor,
                    size_t actor_size)
{
    if (!w || !w->free_actor || !w->text_width || !w->text_proportional || !actor ||
        actor_size < 0x1F6 || actor_size > 0x1000)
        return -1;
    uint8_t copy[0x1000];
    memcpy(copy, actor, actor_size);
    Run dry = {w, mem, 1, 0};
    Node dn = {copy, actor_size};
    title_body(&dry, &dn);
    if (dry.fault)
        return -1;
    Run run = {w, mem, 0, 0};
    Node live = {actor, actor_size};
    title_body(&run, &live);
    return run.fault ? -1 : 0;
}

/* ------------------------------------------------------------------------
 * 001B0BA0: the per-area title base table D_00289B40.
 * ---------------------------------------------------------------------- */

int em_sul_001B0BA0(const int16_t counts[EM_SUL_AREA_COUNT], uint8_t table[EM_SUL_AREA_COUNT * 4])
{
    if (!counts || !table)
        return -1;
    int32_t base = 0;
    for (int i = 0; i < EM_SUL_AREA_COUNT; ++i) {
        uint32_t count = (uint16_t)counts[i];
        table[4 * i + 0] = (uint8_t)base;
        table[4 * i + 1] = (uint8_t)((uint32_t)base >> 8);
        table[4 * i + 2] = (uint8_t)count;
        table[4 * i + 3] = (uint8_t)(count >> 8);
        base += counts[i] != 0 ? counts[i] : 1;
    }
    return 0;
}

/* ------------------------------------------------------------------------
 * 001C4820(actor): +0x04 == 0: 001B0FD0(actor), and when it returns 0,
 * 001C6380(actor); == 1: 001B17A0(actor), then the +0x4C method(actor);
 * == 2 or 3: 001AFC10(actor); other states: nothing.
 * ---------------------------------------------------------------------- */

int em_sul_001C4820(const EmSulWorkers *w, uint8_t *actor, size_t actor_size)
{
    if (!w || !w->model_bind || !w->place || !w->publish || !w->method || !w->free_actor ||
        !actor || actor_size < 0x50)
        return -1;
    int32_t bound = 0;
    switch (actor[4]) {
    case 0:
        if (w->model_bind(w->context, actor, &bound) < 0)
            return -1;
        if (bound == 0 && w->place(w->context, actor) < 0)
            return -1;
        return 0;
    case 1: {
        if (w->publish(w->context, actor) < 0)
            return -1;
        /* the method word is read after 001B17A0 returns */
        uint32_t method = (uint32_t)actor[0x4C] | (uint32_t)actor[0x4D] << 8 |
                          (uint32_t)actor[0x4E] << 16 | (uint32_t)actor[0x4F] << 24;
        return w->method(w->context, actor, method) < 0 ? -1 : 0;
    }
    case 2:
    case 3:
        return w->free_actor(w->context, actor) < 0 ? -1 : 0;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------------
 * Render-context record saves (D_00275670 block): block_copy(dst, src,
 * 0x20) with src = context + 0xA0.
 * ---------------------------------------------------------------------- */

static int save_record(const EmSulWorkers *w, uint8_t *context, size_t size, int64_t dst)
{
    if (!w || !w->block_copy || !context || dst < 0 || (uint64_t)dst + 0x20u > size ||
        size < 0xC0)
        return -1;
    return w->block_copy(w->context, context, (uint32_t)dst, 0xA0u, 0x20) < 0 ? -1 : 0;
}

int em_sul_0021B8E0(const EmSulWorkers *w, uint8_t *context, size_t context_size)
{
    return save_record(w, context, context_size, 0xE0);
}

int em_sul_0021B900(const EmSulWorkers *w, uint8_t *context, size_t context_size)
{
    return save_record(w, context, context_size, 0xC0);
}

int em_sul_0021BAC0(const EmSulWorkers *w, uint8_t *context, size_t context_size, int32_t slot)
{
    /* the slot is shifted left 5 as a 32-bit word before the add */
    int32_t scaled = (int32_t)((uint32_t)slot << 5);
    return save_record(w, context, context_size, (int64_t)scaled + 0x120);
}

int em_sul_0021BA70(const EmSulWorkers *w, uint8_t *context, size_t context_size, uint64_t value)
{
    if (!w || !w->block_copy || !context || context_size < 0xE0)
        return -1;
    for (int i = 0; i < 8; ++i)
        context[0xB0 + i] = (uint8_t)(value >> (8 * i));
    return em_sul_0021B900(w, context, context_size);
}

int em_sul_0021BAB0(const uint8_t *context, size_t context_size, uint64_t *value)
{
    if (!context || !value || context_size < 0xB8)
        return -1;
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i)
        v = v << 8 | context[0xB0 + i];
    *value = v;
    return 0;
}

/* ------------------------------------------------------------------------
 * 0022EBE0
 * ---------------------------------------------------------------------- */

int32_t em_sul_0022EBE0(uint8_t d8101E4, uint8_t spad3B8D)
{
    if (d8101E4 == 3)
        return 1;
    if (spad3B8D == 0)
        return 0;
    return spad3B8D != 4;
}
