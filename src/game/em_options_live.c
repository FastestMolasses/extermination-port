/* em_options_live.c - see em_options_live.h (docs/OPTIONS.md section 4). */
#include "game/em_options_live.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/em_area01_ui.h"
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
    uint8_t stack[STACK_SIZE];
    /* per-call copies of read-only originals (compared after the call) */
    uint8_t slot[4], pads[3][2], pad_mode[2], pad_phase, phase, fade[2], b90, b93;
    uint32_t callees[CALLS][2];
    unsigned callee_count;
    uint32_t entry;
    EmSulWorkers sul;
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

static int text_style(EmOptionsLive *l, uint32_t address, EmMessageTextStyle *out)
{
    if (address != D_00275828) return 0;
    out->color = (int32_t)get32(l->style);
    out->glyph = l->style[4];
    out->flag = l->style[5];
    out->pad[0] = l->style[6];
    out->pad[1] = l->style[7];
    return 1;
}

/* D_00264CB0 (the window) with its style pointer, which must be
 * D_00275828. */
static int line_config(EmOptionsLive *l, uint32_t address, EmMessageDrawConfig *cfg, EmMessageTextStyle *style)
{
    const uint8_t *p = at(l, address, 0x18);
    if (address != D_00264CB0 || !p || !text_style(l, get32(p + 0x14), style)) return 0;
    for (unsigned k = 0; k < 5; ++k) cfg->word[k] = (int32_t)get32(p + 4 * k);
    cfg->style = style;
    return 1;
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

static int store_words(EmOptionsLive *l, const EmMemcardStore *stores, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) {
        uint8_t *p = at(l, stores[i].address, 4);
        if (!p) return -1;
        put32(p, (uint32_t)stores[i].value);
    }
    return 0;
}

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
        return ok0(em_page_draw_blend(l->draw, I(0), I(1)));
    case 0x00207E40u:
        return ok0(em_page_draw_sprite(l->draw, I(0), I(1), I(2), I(3), I(4), U(5), A(6)));
    case 0x00207F80u:
        return ok0(em_page_draw_rectangle(l->draw, I(0), I(1), I(2), I(3), I(4), U(5)));
    case 0x0020A7A0u:
        return ok0(em_page_draw_background(l->draw, A(0)));
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
     * 001CBA50's fixed text (the status pages' em_page_draw text) */
    case 0x001C5FB0u: {
        char text[8];
        if (!em_status_draw_001C5FB0(text, I(0), I(1), I(2))) return -1;
        memcpy(l->number, text, strlen(text) + 1);
        *v0 = (uint64_t)(int64_t)(int32_t)NUMBER_BASE;
        return 0;
    }
    case 0x001CBA50u: {
        uint8_t *p, *style;
        const uint32_t n = room(l, U(5), &p);
        const uint8_t *z = p ? memchr(p, 0, n) : NULL;
        if (!z || (uint32_t)(z - p) > 127) return report(l, target, "reads its text outside the views");
        if (I(0) != 1 || !(style = at(l, U(6), 8))) return report(l, target, "has no style record");
        return ok0(em_page_draw_text(l->draw, 0, I(1), I(2), I(3), I(4), (const char *)p, get32(style) & 0xFFFFFFu));
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
        if (!p || !text_style(l, U(6), &style)) return report(l, target, "has no text or style D_00275828");
        return em_message_live_cc1e0(I(0), I(1), I(2), I(3), I(4), p, n, &style) < 0 ? -1 : 0;
    }
    case 0x001FC770u: {
        uint8_t *p;
        const uint32_t n = room(l, U(2), &p);
        EmMessageDrawConfig cfg;
        EmMessageTextStyle style;
        if (!p || !line_config(l, U(3), &cfg, &style))
            return report(l, target, "has no text or a config other than D_00264CB0");
        return em_message_live_fc770(I(0), I(1), p, n, &cfg) < 0 ? -1 : 0;
    }
    /* the memory card */
    case 0x00114988u: {
        EmMemcardStore stores[3];
        unsigned n = 0;
        int32_t r = 0;
        if (em_memcard_00114988(I(0), I(1), U(2), U(3), U(4), &r, stores, &n) < 0 ||
            store_words(l, stores, n) < 0)
            return -1;
        *v0 = (uint64_t)(int64_t)r;
        return 0;
    }
    case 0x00114848u: {
        EmMemcardStore stores[2];
        unsigned n = 0;
        int32_t r = 0;
        if (em_memcard_00114848(I(0), U(1), U(2), &r, stores, &n) < 0 || store_words(l, stores, n) < 0)
            return -1;
        *v0 = (uint64_t)(int64_t)r;
        return 0;
    }
    case 0x002267A0u:
    case 0x00227300u:
        return report(l, target, "the card screen past the slot choice, which the first level's recordings "
                                 "do not reach (docs/OPTIONS.md section 5): not translated");
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

unsigned em_options_live_calls(const EmOptionsLive *l, uint32_t callee)
{
    if (!l) return 0;
    for (unsigned i = 0; i < l->callee_count; ++i)
        if (l->callees[i][0] == callee) return l->callees[i][1];
    return 0;
}
