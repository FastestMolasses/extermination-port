/* em_options_live.c - see em_options_live.h (docs/OPTIONS.md section 4). */
#include "game/em_options_live.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/em_area01_ui.h"
#include "game/em_area_title.h"
#include "game/em_card_original.h"
#include "game/em_memcard.h"
#include "game/em_message_draw_original.h"
#include "game/em_message_live.h"
#include "game/em_options_original.h"
#include "game/em_page_draw.h"
#include "game/em_player_stage_workers.h"
#include "game/em_scene_state.h"
#include "game/em_status_draw.h"
#include "game/em_status_ui_leftovers.h"

enum { MAX_VIEWS = 160, STACK_BASE = 0x01FF0000u, STACK_SIZE = 0x10000u, CALLS = 64 };

#define D_00275828 0x00275828u /* the text style the two text routines set */
#define D_00264CB0 0x00264CB0u /* 001FC770's config for their lines */
#define STRING_BASE 0x002862C0u
#define STRING_SIZE 0x40u
#define NUMBER_BASE 0x008111D0u /* 001C5FB0's buffer */
#define NUMBER_SIZE 0x10u
/* The card screen past the slot choice and the save / load code below it
 * (em_card_original, em_save_original; docs/SAVE_GAME.md): their .bss and
 * .sbss buffers, held here (nothing else reads them). */
#define D_00821500 0x00821500u /* GetDir's table, 0x12 entries of 0x40 */
#define D_00821980 0x00821980u /* the header file's 0x48 bytes */
#define D_008219D0 0x008219D0u /* a slot's staging copy (a new directory) */
#define D_00822010 0x00822010u /* the slot path the load enumeration builds */
#define D_00822050 0x00822050u /* the enumeration's slot staging block */
#define D_00822690 0x00822690u /* 00229680's staging copy */
#define D_00275C94 0x00275C94u /* the slot rows' text, 4 bytes, then D_00275C98 */
#define CARD_TEXT_SIZE 12u     /* D_00275C94..D_00275C9F (see docs/OPTIONS.md 4.1) */
#define D_002671A0 0x002671A0u /* the slot rows' 001FC770 config */
#define BLOCK_SIZE 0x640u
#define TITLE_REGIONS 8u

struct EmOptionsLive {
    EmOptionsHost host;
    EmGsTexture *gs;
    EmPageDraw *draw;
    EmArea01Ui s;
    EmArea01RenderView views[MAX_VIEWS];
    uint32_t view_count;
    int overflow;
    uint8_t *data; /* writable copies of the .data windows (compared after a call) */
    const EmOptionsFrame *frame;
    /* binder-owned storage of originals no other port module holds */
    uint8_t style[8];   /* D_00275828..2F: +0 colour, +4 glyph, +5 flag */
    uint8_t string[STRING_SIZE], number[NUMBER_SIZE];
    uint8_t getdir[0x480], header[0x48], name[0x40], card_text[CARD_TEXT_SIZE];
    uint8_t stage_load[BLOCK_SIZE], stage_save[BLOCK_SIZE], stage_new[BLOCK_SIZE];
    uint8_t stack[STACK_SIZE];
    /* the area-title module's bytes (the place names and D_00289B40),
     * copied per call and compared after it */
    EmAreaTitleRegion titles[TITLE_REGIONS];
    unsigned title_count;
    uint8_t *title_copy;
    /* per-call copies of read-only originals (compared after the call) */
    uint8_t slot[4], pads[3][2], pad_mode[2], pad_phase, phase, fade[2], b90, b93;
    uint32_t callees[CALLS][2];
    unsigned callee_count;
    uint32_t entry;
    EmSulWorkers sul;
    EmOptionsTrace trace; /* tests only (em_options_live_set_trace) */
    void *trace_context;
};

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static int report(EmOptionsLive *l, uint32_t callee, const char *why)
{
    fprintf(stderr, "options: %08X called %08X, %s\n", (unsigned)l->entry, (unsigned)callee, why);
    return -1;
}

static void view(EmOptionsLive *l, uint32_t address, uint32_t size, uint8_t *bytes)
{
    if (!bytes || !size)
        return;
    if (l->view_count >= MAX_VIEWS) {
        l->overflow = 1; /* reported by run(): no view may be dropped */
        return;
    }
    l->views[l->view_count++] = (EmArea01RenderView){address, size, bytes};
}

static uint8_t *at(EmOptionsLive *l, uint32_t address, uint32_t size)
{
    for (uint32_t i = 0; i < l->view_count; ++i) {
        const EmArea01RenderView *v = &l->views[i];
        if (address >= v->address && (uint64_t)address + size <= (uint64_t)v->address + v->size)
            return v->bytes + (address - v->address);
    }
    return NULL;
}

/* The bytes from `address` to the end of its view (a string's room). */
static uint32_t room(EmOptionsLive *l, uint32_t address, uint8_t **p)
{
    for (uint32_t i = 0; i < l->view_count; ++i) {
        const EmArea01RenderView *v = &l->views[i];
        if (address >= v->address && address < v->address + v->size) {
            *p = v->bytes + (address - v->address);
            return v->address + v->size - address;
        }
    }
    *p = NULL;
    return 0;
}

static void build_views(EmOptionsLive *l, const EmOptionsFrame *f)
{
    l->view_count = 0;
    view(l, EM_OPTIONS_SETTINGS, 0x10, f->settings);
    view(l, f->task_address + 8u, 24, f->task);
    view(l, EM_OPTIONS_MC_RECORD, 0xD4, f->mc);
    for (uint32_t a = EM_SCENE_PROGRESS_BASE; a < EM_SCENE_PROGRESS_BASE + EM_SCENE_PROGRESS_SIZE;) {
        if (!em_scene_progress_canonical(a, 1)) {
            ++a;
            continue;
        }
        uint32_t end = a;
        while (end < EM_SCENE_PROGRESS_BASE + EM_SCENE_PROGRESS_SIZE && em_scene_progress_canonical(end, 1))
            ++end;
        view(l, a, end - a, f->progress + (a - EM_SCENE_PROGRESS_BASE));
        a = end;
    }
    view(l, 0x002821B0u, (uint32_t)sizeof(EmMessageBlock), (uint8_t *)f->message);
    view(l, 0x00275BD8u, 1, f->busy);
    view(l, 0x00282157u, 1, &l->phase);
    view(l, 0x0028A9A0u, 2, l->fade);
    view(l, 0x00810E50u, 1, &l->pad_phase);
    view(l, 0x00810E6Au, 2, l->pad_mode);
    view(l, 0x00810E70u, 2, l->pads[0]);
    view(l, 0x00810E74u, 2, l->pads[1]);
    view(l, 0x00810E78u, 2, l->pads[2]);
    view(l, 0x70003B6Cu, 4, l->slot);
    view(l, 0x70003B74u, 16, (uint8_t *)f->masks);
    view(l, 0x70003B90u, 1, &l->b90);
    view(l, 0x70003B93u, 1, &l->b93);
    view(l, 0x70003B94u, 4, (uint8_t *)f->offset);
    view(l, D_00275828, 8, l->style);
    view(l, STRING_BASE, STRING_SIZE, l->string);
    view(l, NUMBER_BASE, NUMBER_SIZE, l->number);
    view(l, 0x00275C58u, 0x18, em_memcard_globals(0x00275C58u, 0x18));
    view(l, 0x00264E30u, 0x0C, em_memcard_globals(0x00264E30u, 0x0C));
    view(l, 0x00275840u, 0x08, em_memcard_globals(0x00275840u, 0x08));
    view(l, D_00821500, sizeof l->getdir, l->getdir);
    view(l, D_00821980, sizeof l->header, l->header);
    view(l, D_008219D0, BLOCK_SIZE, l->stage_save);
    view(l, D_00822010, sizeof l->name, l->name);
    view(l, D_00822050, BLOCK_SIZE, l->stage_load);
    view(l, D_00822690, BLOCK_SIZE, l->stage_new);
    view(l, D_00275C94, CARD_TEXT_SIZE, l->card_text);
    uint32_t toff = 0;
    for (unsigned i = 0; i < l->title_count; ++i) {
        memcpy(l->title_copy + toff, l->titles[i].bytes, l->titles[i].size);
        view(l, l->titles[i].address, l->titles[i].size, l->title_copy + toff);
        toff += l->titles[i].size;
    }
    const EmGsDataWindow *w;
    const unsigned n = em_gs_texture_windows(l->gs, &w);
    uint32_t off = 0;
    for (unsigned i = 0; i < n; ++i) {
        view(l, w[i].address, w[i].size, l->data + off);
        off += w[i].size;
    }
    view(l, STACK_BASE, STACK_SIZE, l->stack);
    l->s.core.world = (EmArea01RenderWorld){.views = l->views, .view_count = l->view_count};
}

/* The read-only originals after a call: the .data windows and the
 * per-call copies must hold what they held (a store into one would be
 * kept by the port and not by the original's memory). */
static int check_readonly(EmOptionsLive *l, const EmOptionsFrame *f)
{
    const EmGsDataWindow *w;
    const unsigned n = em_gs_texture_windows(l->gs, &w);
    uint32_t off = 0;
    for (unsigned i = 0; i < n; ++i) {
        if (memcmp(l->data + off, w[i].bytes, w[i].size))
            return report(l, w[i].address, "stored into a read-only .data window");
        off += w[i].size;
    }
    uint32_t toff = 0;
    for (unsigned i = 0; i < l->title_count; ++i) {
        if (memcmp(l->title_copy + toff, l->titles[i].bytes, l->titles[i].size))
            return report(l, l->titles[i].address, "stored into the area-title module's read-only bytes");
        toff += l->titles[i].size;
    }
    uint8_t want[4];
    put32(want, f->task_address);
    if (memcmp(l->slot, want, 4) || l->phase != f->read_phase || l->pad_phase != f->pad_phase ||
        l->b90 != f->spad3B90 || l->b93 != f->spad3B93 ||
        (uint16_t)(l->fade[0] | l->fade[1] << 8) != (uint16_t)f->fade ||
        (uint16_t)(l->pad_mode[0] | l->pad_mode[1] << 8) != f->pad_mode ||
        (uint16_t)(l->pads[0][0] | l->pads[0][1] << 8) != f->held ||
        (uint16_t)(l->pads[1][0] | l->pads[1][1] << 8) != f->pressed ||
        (uint16_t)(l->pads[2][0] | l->pads[2][1] << 8) != f->repeat)
        return report(l, 0, "stored into an original the frame only lends (a pad word, the pad "
                            "mode, the fade, the read phase or the scratchpad bytes)");
    return 0;
}

/* ---- the text --------------------------------------------------------- */

/* A style record by its address: D_00275828 (the binder's storage, which
 * 001FCBD0 / 001FCE30 set) or another one in a view (the card screen's
 * D_00275898, a .data window). */
static int text_style(EmOptionsLive *l, uint32_t address, EmMessageTextStyle *out)
{
    const uint8_t *p = address == D_00275828 ? l->style : at(l, address, 8);
    if (!p) return 0;
    out->color = (int32_t)get32(p);
    out->glyph = p[4];
    out->flag = p[5];
    out->pad[0] = p[6];
    out->pad[1] = p[7];
    return 1;
}

/* 001FC770's config: D_00264CB0 (001FCBD0 / 001FCE30) or D_002671A0 (the
 * card screen's slot rows), each a .data window, with its style pointer
 * (+0x14) a style record text_style reads, or 0. *cfg_out is NULL for the
 * original's 0 (001FC770 then copies its template). */
static int line_config(EmOptionsLive *l, uint32_t address, EmMessageDrawConfig *cfg, EmMessageTextStyle *style,
                       const EmMessageDrawConfig **cfg_out)
{
    if (address == 0) {
        *cfg_out = NULL;
        return 1;
    }
    const uint8_t *p = at(l, address, 0x18);
    if ((address != D_00264CB0 && address != D_002671A0) || !p) return 0;
    const uint32_t style_at = get32(p + 0x14);
    if (style_at && !text_style(l, style_at, style)) return 0;
    for (unsigned k = 0; k < 5; ++k) cfg->word[k] = (int32_t)get32(p + 4 * k);
    cfg->style = style_at ? style : NULL;
    *cfg_out = cfg;
    return 1;
}

/* ---- EE memory as the card server and the leaves reach it -------------- */

/* Every byte of [address, address + size) in a view (a range may span
 * views). */
static int mem_check(EmOptionsLive *l, uint32_t address, uint32_t size)
{
    for (uint32_t i = 0; i < size;) {
        uint8_t *p;
        const uint32_t n = room(l, address + i, &p);
        if (!p) return 0;
        i += n < size - i ? n : size - i;
    }
    return 1;
}

static int mem_read(void *ctx, uint32_t address, void *dst, uint32_t size)
{
    EmOptionsLive *l = ctx;
    if (!mem_check(l, address, size)) return -1;
    for (uint32_t i = 0; i < size;) {
        uint8_t *p;
        uint32_t n = room(l, address + i, &p);
        if (n > size - i) n = size - i;
        memcpy((uint8_t *)dst + i, p, n);
        i += n;
    }
    return 0;
}

static int mem_write(void *ctx, uint32_t address, const void *src, uint32_t size)
{
    EmOptionsLive *l = ctx;
    if (!mem_check(l, address, size)) return -1; /* nothing stored unless every byte is a view's */
    for (uint32_t i = 0; i < size;) {
        uint8_t *p;
        uint32_t n = room(l, address + i, &p);
        if (n > size - i) n = size - i;
        memcpy(p, (const uint8_t *)src + i, n);
        i += n;
    }
    return 0;
}

/* sprintf 00122D10(dst, format, ...) (the SDK's, no length limit) for the
 * formats the card screen and the load enumeration pass (D_00273998 "%02d"
 * style, D_002739A0, D_00273960): flags '-' and '0', a width, and the
 * conversions d and s (a string in a view); any other conversion faults.
 * The integer arguments are the 32-bit images the registers hold; the NUL
 * is stored at the end. *v0 the count (no caller reads it). */
static int sprintf_00122D10(EmOptionsLive *l, const uint64_t *a, unsigned na, uint64_t *v0)
{
    uint8_t *fmt;
    const uint32_t nf = room(l, (uint32_t)a[1], &fmt);
    const uint8_t *z = fmt ? memchr(fmt, 0, nf) : NULL;
    if (!z) return report(l, 0x00122D10u, "has its format outside the views");
    char out[256];
    size_t len = 0;
    unsigned arg = 2;
    for (const uint8_t *c = fmt; c < z;) {
        if (*c != '%') {
            if (len + 1 >= sizeof out) return report(l, 0x00122D10u, "formats more than the binding holds");
            out[len++] = (char)*c++;
            continue;
        }
        char spec[16];
        size_t k = 0;
        spec[k++] = (char)*c++;
        while (c < z && (*c == '-' || *c == '0') && k < 4) spec[k++] = (char)*c++;
        while (c < z && *c >= '0' && *c <= '9' && k < 8) spec[k++] = (char)*c++;
        if (c >= z || (*c != 'd' && *c != 's'))
            return report(l, 0x00122D10u, "with a conversion the binding does not implement");
        const char conv = (char)*c++;
        spec[k++] = conv;
        spec[k] = 0;
        if (arg >= na) return report(l, 0x00122D10u, "with more conversions than argument registers");
        char piece[128];
        int n;
        if (conv == 'd') {
            n = snprintf(piece, sizeof piece, spec, (int)(int32_t)(uint32_t)a[arg++]);
        } else {
            uint8_t *sp;
            const uint32_t ns = room(l, (uint32_t)a[arg++], &sp);
            const uint8_t *sz = sp ? memchr(sp, 0, ns) : NULL;
            if (!sz) return report(l, 0x00122D10u, "has a %s argument outside the views");
            char text[96];
            if ((size_t)(sz - sp) >= sizeof text) return report(l, 0x00122D10u, "has a long %s argument");
            memcpy(text, sp, (size_t)(sz - sp) + 1);
            n = snprintf(piece, sizeof piece, spec, text);
        }
        if (n < 0 || (size_t)n >= sizeof piece || len + (size_t)n >= sizeof out)
            return report(l, 0x00122D10u, "formats more than the binding holds");
        memcpy(out + len, piece, (size_t)n);
        len += (size_t)n;
    }
    out[len] = 0;
    if (mem_write(l, (uint32_t)a[0], out, (uint32_t)len + 1u) < 0)
        return report(l, 0x00122D10u, "writes outside the views");
    *v0 = (uint64_t)len;
    return 0;
}

/* ---- the test log (em_options_live_set_trace) -------------------------- */

typedef struct {
    uint8_t b[512];
    uint32_t n;
    int full;
} Record;

static void rec_bytes(Record *r, const void *p, uint32_t n)
{
    if (r->n + n > sizeof r->b) {
        r->full = 1;
        return;
    }
    memcpy(r->b + r->n, p, n);
    r->n += n;
}

static void rec_word(Record *r, uint32_t v)
{
    uint8_t w[4];
    put32(w, v);
    rec_bytes(r, w, 4);
}

static void rec_dword(Record *r, uint64_t v)
{
    rec_word(r, (uint32_t)v);
    rec_word(r, (uint32_t)(v >> 32));
}

static void rec_text(Record *r, const uint8_t *p, uint32_t n)
{
    const uint8_t *z = memchr(p, 0, n);
    const uint32_t len = z ? (uint32_t)(z - p) : n;
    rec_word(r, len);
    rec_bytes(r, p, len);
}

static void rec_style(Record *r, const EmMessageTextStyle *s)
{
    const uint8_t present = s != NULL;
    rec_bytes(r, &present, 1);
    if (!s) return;
    rec_word(r, (uint32_t)s->color);
    const uint8_t b[4] = {s->glyph, s->flag, s->pad[0], s->pad[1]};
    rec_bytes(r, b, 4);
}

static void emit(EmOptionsLive *l, uint32_t callee, Record *r)
{
    if (l->trace) l->trace(l->trace_context, callee, r->full ? NULL : r->b, r->full ? 0 : r->n);
}

/* The traced calls. Each takes the values the dispatcher hands on as its
 * own parameters, logs exactly those parameters and forwards them
 * unchanged, so the log is what the callee receives: a wrong argument in a
 * dispatcher's call line reaches the log too. */
static int out_blend(EmOptionsLive *l, int32_t slot, int32_t mode)
{
    if (l->trace) {
        Record r = {.n = 0};
        rec_word(&r, (uint32_t)slot);
        rec_word(&r, (uint32_t)mode);
        emit(l, 0x00207D00u, &r);
    }
    return em_page_draw_blend(l->draw, slot, mode);
}

static int out_sprite(EmOptionsLive *l, int32_t slot, int32_t x, int32_t y, int32_t w, int32_t h,
                      uint32_t rgba, uint64_t tex0)
{
    if (l->trace) {
        Record r = {.n = 0};
        const int32_t v[5] = {slot, x, y, w, h};
        for (unsigned i = 0; i < 5; ++i) rec_word(&r, (uint32_t)v[i]);
        rec_word(&r, rgba);
        rec_dword(&r, tex0);
        emit(l, 0x00207E40u, &r);
    }
    return em_page_draw_sprite(l->draw, slot, x, y, w, h, rgba, tex0);
}

static int out_rectangle(EmOptionsLive *l, int32_t slot, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                         uint32_t rgba)
{
    if (l->trace) {
        Record r = {.n = 0};
        const int32_t v[5] = {slot, x0, y0, x1, y1};
        for (unsigned i = 0; i < 5; ++i) rec_word(&r, (uint32_t)v[i]);
        rec_word(&r, rgba);
        emit(l, 0x00207F80u, &r);
    }
    return em_page_draw_rectangle(l->draw, slot, x0, y0, x1, y1, rgba);
}

static int out_background(EmOptionsLive *l, uint64_t tex0)
{
    if (l->trace) {
        Record r = {.n = 0};
        rec_dword(&r, tex0);
        emit(l, 0x0020A7A0u, &r);
    }
    return em_page_draw_background(l->draw, tex0);
}

static int out_cc1e0(EmOptionsLive *l, int32_t slot, int32_t x, int32_t y, int32_t unused, int32_t h,
                     const uint8_t *text, uint32_t avail, const EmMessageTextStyle *style)
{
    if (l->trace) {
        Record r = {.n = 0};
        const int32_t v[5] = {slot, x, y, unused, h};
        for (unsigned i = 0; i < 5; ++i) rec_word(&r, (uint32_t)v[i]);
        rec_text(&r, text, avail);
        rec_style(&r, style);
        emit(l, 0x001CC1E0u, &r);
    }
    return em_message_live_cc1e0(slot, x, y, unused, h, text, avail, style);
}

static int out_fc770(EmOptionsLive *l, int32_t x, int32_t y, const uint8_t *text, uint32_t avail,
                     const EmMessageDrawConfig *cfg)
{
    if (l->trace) {
        Record r = {.n = 0};
        rec_word(&r, (uint32_t)x);
        rec_word(&r, (uint32_t)y);
        rec_text(&r, text, avail);
        const uint8_t present = cfg != NULL;
        rec_bytes(&r, &present, 1);
        if (cfg) {
            for (unsigned k = 0; k < 5; ++k) rec_word(&r, (uint32_t)cfg->word[k]);
            rec_style(&r, cfg->style);
        }
        emit(l, 0x001FC770u, &r);
    }
    return em_message_live_fc770(x, y, text, avail, cfg);
}

/* ---- the dispatcher ---------------------------------------------------- */

static void count(EmOptionsLive *l, uint32_t callee)
{
    for (unsigned i = 0; i < l->callee_count; ++i)
        if (l->callees[i][0] == callee) {
            ++l->callees[i][1];
            return;
        }
    if (l->callee_count < CALLS) {
        l->callees[l->callee_count][0] = callee;
        l->callees[l->callee_count++][1] = 1;
    }
}

static int ok0(int accepted) { return accepted ? 0 : -1; }

/* 001FB9F0 as 0020CD40 / 60 / A0 call it: the host's sound. Not logged
 * here: a test logs the cue in its own host.sound, where it arrives. */
static int sul_sound(void *c, int32_t id, int32_t a1, int32_t a2, int32_t a3)
{
    EmOptionsLive *l = c;
    return l->host.sound && l->host.sound(l->host.context, id, a1, a2, a3) >= 0 ? 0 : -1;
}

static int dispatch(void *ctx, uint32_t target, uint32_t sp, const uint64_t *a, unsigned na,
                    const uint32_t *f, unsigned nf, uint64_t *v0, uint32_t *f0)
{
    EmOptionsLive *l = ctx;
    const EmOptionsHost *h = &l->host;
    (void)sp;
    *v0 = 0;
    *f0 = 0;
    count(l, target);
#define A(i) ((i) < na ? a[i] : 0)
#define I(i) ((int32_t)(uint32_t)A(i))
#define U(i) ((uint32_t)A(i))
    switch (target) {
    case 0x00207D00u:
        return ok0(out_blend(l, I(0), I(1)));
    case 0x00207E40u:
        return ok0(out_sprite(l, I(0), I(1), I(2), I(3), I(4), U(5), A(6)));
    case 0x00207F80u:
        return ok0(out_rectangle(l, I(0), I(1), I(2), I(3), I(4), U(5)));
    case 0x0020A7A0u:
        return ok0(out_background(l, A(0)));
    case 0x001281C0u:
        if (nf < 1) return -1;
        *v0 = (uint64_t)(int64_t)em_player_float_to_int(f[0]);
        return 0;
    case 0x0020CD40u:
        return em_sul_0020CD40(&l->sul);
    case 0x0020CD60u:
        return em_sul_0020CD60(&l->sul);
    case 0x0020CDA0u:
        return em_sul_0020CDA0(&l->sul);
    case 0x001D2830u:
        return h->draw_context && h->draw_context(h->context, I(0), I(1)) == 1 ? 0 : -1;
    case 0x00200970u:
        if (I(0) != 1) return report(l, target, "with an argument other than 1 (not reached here)");
        return h->restore && h->restore(h->context) == 1 ? 0 : -1;
    case 0x001AEE10u:
        return h->fade && h->fade(h->context, 0, I(0), I(1)) == 1 ? 0 : -1;
    case 0x001AEDE0u:
        return h->fade && h->fade(h->context, 1, I(0), I(1)) == 1 ? 0 : -1;
    case 0x001FBC50u:
        return h->stop_sounds && h->stop_sounds(h->context) == 1 ? 0 : -1;
    case 0x001FABB0u:
        return h->stop_streams && h->stop_streams(h->context) == 1 ? 0 : -1;
    case 0x001FF080u:
        if (I(0) != 0) return report(l, target, "with a first argument other than 0");
        return h->module_load && h->module_load(h->context, U(1)) == 1 ? 0 : -1;
    case 0x001B61C0u:
        return h->rumble && h->rumble(h->context, I(0), I(1), I(2), I(3)) == 1 ? 0 : -1;
    /* the libc leaves over the views */
    case 0x00121A28u: { /* memset(dst, c, n) */
        uint8_t *d = at(l, U(0), U(2));
        if (!d && U(2)) return report(l, target, "fills outside the views");
        if (U(2)) memset(d, (int)(uint8_t)U(1), U(2));
        *v0 = A(0);
        return 0;
    }
    case 0x00123168u:   /* strcpy(dst, src) */
    case 0x00122EF0u: { /* strcat(dst, src) */
        uint8_t *src, *dst;
        const uint32_t ns = room(l, U(1), &src), nd = room(l, U(0), &dst);
        const uint8_t *z = src ? memchr(src, 0, ns) : NULL;
        if (!z || !dst) return report(l, target, "copies outside the views");
        const uint32_t len = (uint32_t)(z - src) + 1;
        uint32_t start = 0;
        if (target == 0x00122EF0u) {
            const uint8_t *e = memchr(dst, 0, nd);
            if (!e) return report(l, target, "appends to a string without its NUL in the views");
            start = (uint32_t)(e - dst);
        }
        if (start + len > nd) return report(l, target, "writes past its view");
        memmove(dst + start, src, len);
        *v0 = A(0);
        return 0;
    }
    case 0x00123418u: { /* strncpy(dst, src, n): NUL padding to n */
        uint8_t *src, *dst;
        const uint32_t ns = room(l, U(1), &src), nd = room(l, U(0), &dst);
        const uint32_t n = U(2);
        if (!src || !dst || n > nd) return report(l, target, "copies outside the views");
        uint32_t i = 0;
        for (; i < n; ++i) {
            if (i >= ns) return report(l, target, "reads outside the views");
            dst[i] = src[i];
            if (!src[i]) break;
        }
        for (; i < n; ++i) dst[i] = 0;
        *v0 = A(0);
        return 0;
    }
    case 0x001232E0u: { /* strlen */
        uint8_t *p;
        const uint32_t n = room(l, U(0), &p);
        const uint8_t *z = p ? memchr(p, 0, n) : NULL;
        if (!z) return report(l, target, "reads outside the views");
        *v0 = (uint64_t)(z - p);
        return 0;
    }
    case 0x00123280u: { /* strcspn(s, set) */
        uint8_t *p, *q;
        const uint32_t np = room(l, U(0), &p), nq = room(l, U(1), &q);
        const uint8_t *zp = p ? memchr(p, 0, np) : NULL, *zq = q ? memchr(q, 0, nq) : NULL;
        if (!zp || !zq) return report(l, target, "reads outside the views");
        size_t i = 0;
        while (p[i] && !memchr(q, p[i], (size_t)(zq - q))) ++i;
        *v0 = (uint64_t)i;
        return 0;
    }
    /* the numbers: 001C5FB0(value, width, blank) into its buffer, and
     * 001CBA50's small-font text (translated, em_page_draw_cba50) */
    case 0x001C5FB0u: {
        char text[8];
        if (!em_status_draw_001C5FB0(text, I(0), I(1), I(2))) return -1;
        memcpy(l->number, text, strlen(text) + 1);
        *v0 = (uint64_t)(int64_t)(int32_t)NUMBER_BASE;
        return 0;
    }
    case 0x001CBA50u: {
        uint8_t *p, *record = NULL;
        const uint32_t n = room(l, U(5), &p);
        if (!p || !memchr(p, 0, n)) return report(l, target, "reads its text outside the views");
        /* style 0: 001CBC20's default colour */
        if (U(6) && !(record = at(l, U(6), 8))) return report(l, target, "reads its style outside the views");
        EmMessageTextStyle style;
        if (record) {
            style.color = (int32_t)get32(record);
            style.glyph = record[4];
            style.flag = record[5];
            style.pad[0] = record[6];
            style.pad[1] = record[7];
        }
        return ok0(em_page_draw_cba50(l->draw, I(0), I(1), I(2), I(3), I(4), p, n, record ? &style : NULL));
    }
    /* the message lines */
    case 0x001FE480u: {
        uint8_t *p;
        const uint32_t n = room(l, U(0), &p);
        const EmMessageBank bank = {p, n};
        uint32_t offset;
        if (!p || !em_message_bank_string(&bank, I(1), &offset)) return report(l, target, "reads outside its bank");
        *v0 = (uint64_t)(int64_t)(int32_t)(U(0) + offset);
        return 0;
    }
    case 0x001CC170u: {
        uint8_t *p;
        const uint32_t n = room(l, U(0), &p);
        int32_t width = 0;
        if (!p || em_message_live_cc170(p, n, &width) < 0) return -1;
        *v0 = (uint64_t)(int64_t)width;
        return 0;
    }
    case 0x001CC1E0u: {
        uint8_t *p;
        const uint32_t n = room(l, U(5), &p);
        EmMessageTextStyle style;
        if (!p || (U(6) && !text_style(l, U(6), &style)))
            return report(l, target, "has no text or a style outside the views");
        const EmMessageTextStyle *use = U(6) ? &style : NULL;
        return out_cc1e0(l, I(0), I(1), I(2), I(3), I(4), p, n, use) < 0 ? -1 : 0;
    }
    case 0x001FC770u: {
        uint8_t *p;
        const uint32_t n = room(l, U(2), &p);
        EmMessageDrawConfig cfg;
        EmMessageTextStyle style;
        const EmMessageDrawConfig *use = NULL;
        if (!p || !line_config(l, U(3), &cfg, &style, &use))
            return report(l, target, "has no text or a config other than D_00264CB0 / D_002671A0 / 0");
        return out_fc770(l, I(0), I(1), p, n, use) < 0 ? -1 : 0;
    }
    /* the memory card: the eight libmc entries em_memcard answers (GetInfo,
     * Sync and the file commands), over the views */
    case 0x00114988u: case 0x00114848u: case 0x00114360u: case 0x00114448u:
    case 0x00114480u: case 0x00114708u: case 0x00114630u: case 0x00114AC0u: {
        const EmMemcardMemory mem = {l, mem_read, mem_write};
        return em_memcard_call(target, a, na, &mem, v0) < 0 ? -1 : 0;
    }
    case 0x00114D18u:
        return report(l, target, "(libmc Format, the save mode's unformatted card): no recording shows the "
                                 "card server's answer (docs/SAVE_GAME.md section 6.2)");
    /* the card screen past the slot choice (em_card_original): 00226070's
     * card poll 002267A0 and 00225D20's 00227300 */
    case 0x002267A0u:
    case 0x00227300u: {
        uint32_t r = 0;
        const int rc = target == 0x002267A0u ? em_card_002267A0(&l->s, U(0), &r) : em_card_00227300(&l->s, U(0), &r);
        if (rc < 0) return -1;
        *v0 = (uint64_t)(int64_t)(int32_t)r;
        return 0;
    }
    case 0x00122D10u:
        return sprintf_00122D10(l, a, na, v0);
    case 0x00123020u: { /* strcmp(a, b): the first differing bytes' difference (00228C90 tests 0) */
        uint8_t *p, *q;
        const uint32_t np = room(l, U(0), &p), nq = room(l, U(1), &q);
        for (uint32_t i = 0;; ++i) {
            if (i >= np || i >= nq) return report(l, target, "reads outside the views");
            if (p[i] != q[i] || !p[i]) {
                *v0 = (uint64_t)(int64_t)((int32_t)p[i] - (int32_t)q[i]);
                return 0;
            }
        }
    }
    default:
        return report(l, target, "which is not bound");
    }
#undef A
#undef I
#undef U
}

/* ---- entries ---------------------------------------------------------- */

EmOptionsLive *em_options_live_create(EmGsTexture *gs, const EmOptionsHost *host)
{
    if (!gs || !host) return NULL;
    EmOptionsLive *l = calloc(1, sizeof *l);
    if (!l) return NULL;
    l->host = *host;
    l->gs = gs;
    l->draw = em_page_draw_create(gs);
    const EmGsDataWindow *w;
    const unsigned n = em_gs_texture_windows(gs, &w);
    size_t size = 0;
    for (unsigned i = 0; i < n; ++i) size += w[i].size;
    if (!l->draw || !n || !(l->data = malloc(size))) {
        em_options_live_free(l);
        return NULL;
    }
    size = 0;
    for (unsigned i = 0; i < n; ++i) {
        memcpy(l->data + size, w[i].bytes, w[i].size);
        size += w[i].size;
    }
    /* D_00275828's boot value: the ELF's .sdata bytes, which the EMSP
     * carries as a window (only 001FCBD0 / 001FCE30 store to it; the
     * binder's storage is the one the calls see). */
    const uint8_t *init = NULL;
    for (unsigned i = 0; i < n; ++i)
        if (w[i].address <= D_00275828 && w[i].address + w[i].size >= D_00275828 + 8u)
            init = w[i].bytes + (D_00275828 - w[i].address);
    if (!init) {
        fprintf(stderr, "options: the EMSP has no D_00275828 window (python3 tools/export_status_pages.py)\n");
        em_options_live_free(l);
        return NULL;
    }
    memcpy(l->style, init, sizeof l->style);
    /* the place names the card screen's slot rows read (em_area_title); a
     * region the EMSP already carries as a window (the style D_00265520) is
     * read there */
    EmAreaTitleRegion all[TITLE_REGIONS];
    const unsigned got = em_area_title_regions(all, TITLE_REGIONS);
    for (unsigned i = 0; i < got; ++i) {
        int inside = 0;
        for (unsigned k = 0; k < n; ++k)
            if (all[i].address >= w[k].address && (uint64_t)all[i].address + all[i].size <=
                                                       (uint64_t)w[k].address + w[k].size)
                inside = 1;
        if (!inside) l->titles[l->title_count++] = all[i];
    }
    size_t tsize = 0;
    for (unsigned i = 0; i < l->title_count; ++i) tsize += l->titles[i].size;
    if (!l->title_count || !(l->title_copy = malloc(tsize))) {
        fprintf(stderr, "options: the area-title export (the card screen's place names) is missing "
                        "(python3 tools/export_area_title.py)\n");
        em_options_live_free(l);
        return NULL;
    }
    memset(&l->sul, 0, sizeof l->sul);
    l->sul.context = l;
    l->sul.sound = sul_sound;
    return l;
}

void em_options_live_free(EmOptionsLive *l)
{
    if (!l) return;
    em_page_draw_free(l->draw);
    free(l->data);
    free(l->title_copy);
    free(l);
}

typedef enum { RUN_0022A650, RUN_00225AC0, RUN_001AF1C0, RUN_001AF150 } Run;

static int run(EmOptionsLive *l, const EmOptionsFrame *f, Run which, uint32_t mode, uint32_t *result)
{
    static const uint32_t names[4] = {0x0022A650u, 0x00225AC0u, 0x001AF1C0u, 0x001AF150u};
    if (!l || !f || !f->settings || !f->task || !f->mc || !f->progress || !f->message || !f->busy ||
        !f->masks || !f->offset)
        return -1;
    if (em_memcard_fault()) return -1;
    l->frame = f;
    l->entry = names[which];
    l->callee_count = 0;
    put32(l->slot, f->task_address);
    l->phase = f->read_phase;
    l->fade[0] = (uint8_t)f->fade;
    l->fade[1] = (uint8_t)((uint16_t)f->fade >> 8);
    l->pad_mode[0] = (uint8_t)f->pad_mode;
    l->pad_mode[1] = (uint8_t)(f->pad_mode >> 8);
    l->pad_phase = f->pad_phase;
    const uint16_t pads[3] = {f->held, f->pressed, f->repeat};
    for (unsigned i = 0; i < 3; ++i) {
        l->pads[i][0] = (uint8_t)pads[i];
        l->pads[i][1] = (uint8_t)(pads[i] >> 8);
    }
    l->b90 = f->spad3B90;
    l->b93 = f->spad3B93;
    l->overflow = 0;
    build_views(l, f);
    if (l->overflow) {
        fprintf(stderr, "options: more views than the binding holds\n");
        return -1;
    }
    if (which == RUN_0022A650 || which == RUN_00225AC0) em_page_draw_begin(l->draw);
    memset(&l->s.core.fault, 0, sizeof l->s.core.fault);
    l->s.core.function = 0;
    l->s.call = dispatch;
    l->s.ctx = l;
    l->s.sp = STACK_BASE + STACK_SIZE - 0x100;
    l->s.leaf_calls = 1;
    uint32_t r = 0;
    int rc;
    switch (which) {
    case RUN_0022A650: rc = em_options_0022A650(&l->s, &r); break;
    case RUN_00225AC0: rc = em_options_00225AC0(&l->s, mode, &r); break;
    case RUN_001AF1C0: rc = em_options_001AF1C0(&l->s); break;
    default: rc = em_options_001AF150(&l->s); break;
    }
    if (rc >= 0 && check_readonly(l, f) < 0) rc = -1;
    if (rc < 0 || em_a01r_latched(&l->s.core)) {
        fprintf(stderr, "options: %08X faulted in %08X (code %d, detail %08X)\n", (unsigned)l->entry,
                (unsigned)l->s.core.fault.address, (int)l->s.core.fault.code, (unsigned)l->s.core.fault.detail);
        return -1;
    }
    if (result) *result = r;
    return 0;
}

int em_options_live_0022A650(EmOptionsLive *l, const EmOptionsFrame *f, uint32_t *result)
{
    return run(l, f, RUN_0022A650, 0, result);
}

int em_options_live_00225AC0(EmOptionsLive *l, const EmOptionsFrame *f, uint32_t mode, uint32_t *result)
{
    return run(l, f, RUN_00225AC0, mode, result);
}

int em_options_live_001AF1C0(EmOptionsLive *l, const EmOptionsFrame *f) { return run(l, f, RUN_001AF1C0, 0, NULL); }
int em_options_live_001AF150(EmOptionsLive *l, const EmOptionsFrame *f) { return run(l, f, RUN_001AF150, 0, NULL); }

int em_options_live_render(EmOptionsLive *l, EmGfx *gfx)
{
    return l && em_page_draw_render(l->draw, gfx) == 1 ? 1 : 0;
}

void em_options_live_deactivate(EmOptionsLive *l)
{
    if (l) em_page_draw_deactivate(l->draw);
}

static uint8_t *card_buffer(const EmOptionsLive *cl, uint32_t address, uint32_t size)
{
    EmOptionsLive *l = (EmOptionsLive *)cl;
    const struct { uint32_t base, size; uint8_t *bytes; } own[] = {
        {D_00821500, sizeof l->getdir, l->getdir}, {D_00821980, sizeof l->header, l->header},
        {D_008219D0, BLOCK_SIZE, l->stage_save}, {D_00822010, sizeof l->name, l->name},
        {D_00822050, BLOCK_SIZE, l->stage_load}, {D_00822690, BLOCK_SIZE, l->stage_new},
        {D_00275C94, CARD_TEXT_SIZE, l->card_text}};
    for (unsigned i = 0; i < sizeof own / sizeof own[0]; ++i)
        if (address >= own[i].base && (uint64_t)address + size <= (uint64_t)own[i].base + own[i].size)
            return own[i].bytes + (address - own[i].base);
    return NULL;
}

int em_options_live_card_bytes(const EmOptionsLive *l, uint32_t address, void *dst, uint32_t size)
{
    const uint8_t *p = l && dst ? card_buffer(l, address, size) : NULL;
    if (!p) return -1;
    memcpy(dst, p, size);
    return 0;
}

int em_options_live_card_seed(EmOptionsLive *l, uint32_t address, const void *src, uint32_t size)
{
    uint8_t *p = l && src ? card_buffer(l, address, size) : NULL;
    if (!p) return -1;
    memcpy(p, src, size);
    return 0;
}

void em_options_live_set_trace(EmOptionsLive *l, EmOptionsTrace trace, void *context)
{
    if (!l) return;
    l->trace = trace;
    l->trace_context = context;
}

unsigned em_options_live_calls(const EmOptionsLive *l, uint32_t callee)
{
    if (!l) return 0;
    for (unsigned i = 0; i < l->callee_count; ++i)
        if (l->callees[i][0] == callee) return l->callees[i][1];
    return 0;
}
