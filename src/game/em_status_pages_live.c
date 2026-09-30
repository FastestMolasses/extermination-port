/* em_status_pages_live.c - see em_status_pages_live.h. */
#include "game/em_status_pages_live.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/em_census_standins.h"
#include "game/em_ee_float.h"
#include "game/em_menu_hover.h"
#include "game/em_message_live.h"
#include "game/em_page_draw.h"
#include "game/em_pickup_items_original.h"
#include "game/em_player_stage_workers.h"
#include "game/em_render_verify_rest.h"
#include "game/em_scene_state.h"
#include "game/em_status_page_record.h"
#include "game/em_status_models.h"
#include "game/em_status_pages.h"
#include "game/em_status_ui_leftovers.h"
#include "game/em_effect_original.h"
#include "game/em_owner_services_original.h"
#include "game/em_render_context_live.h"
#include "game/em_sdk_vu0.h"

_Static_assert(sizeof(EmMessageBlock) == 0x9C, "D_002821B0 is 0x9C bytes");
_Static_assert(offsetof(EmMessageBlock, aux_mode) == 0x90, "D_00282240 is +0x90");

enum { MAX_VIEWS = 96, STACK_BASE = 0x01FF0000u, STACK_SIZE = 0x10000u, SPAD_BASE = 0x70003400u,
       SPAD_SIZE = 0x800u, STRING_BASE = 0x002862C0u, STRING_SIZE = 0x40u, CALLS = 64,
       NUMBER_BASE = 0x008111D0u, NUMBER_SIZE = 0x10u };

#define T_UI 0x00810130u
#define PLAYER 0x008102B0u
#define POOL 0x0028B020u          /* D_0028B020: the UI pool (em_status_models) */
#define POOL_SIZE (24u * 0x2F0u)
#define D_0028A570 0x0028A570u    /* the MAP bank word (module 0x1E slot 0x38) */
#define NODE_002101C0 0x002101C0u /* 0020F950's node behaviour */

struct EmStatusPagesLive {
    EmStatusPagesHost host;
    EmGsTexture *gs;
    EmPageDraw *draw;
    const EmStatusPagesFrame *frame;
    EmArea01Ui s;
    EmArea01RenderView views[MAX_VIEWS];
    EmSulRegion regions[MAX_VIEWS];
    EmSulMemory mem;
    EmSulWorkers sul;
    EmCsRectWorkers rect;
    EmRvrMessageWorkers message;
    EmSprWorkers spr;
    uint8_t *data;                     /* writable copies of the .data windows */
    uint32_t view_count;
    uint8_t stack[STACK_SIZE], spad[SPAD_SIZE], string[STRING_SIZE];
    uint8_t pads[4][2], stick[2], counter[4], vitals[8], player220[4], probe;
    uint8_t c61, number_buffer[NUMBER_SIZE];
    char number[8];
    /* MAP: the read-only views' bytes (D_0028A570, the player record's
     * +0xA0..+0xA8 and +0xC4, the render context word and its +0x2468) and
     * D_00275BCC (the free bone slots), each built per call */
    uint8_t d28A570[4], player_a0[12], player_c4[4], ctx_word[4], ctx_zoom[4], bcc[2];
    uint32_t dispatch_sp; /* the sp of the running 001B0000 dispatch */
    uint32_t callees[CALLS][2];
    unsigned callee_count;
    uint32_t page;
    /* EM_STATUS_PAGES_TRACE: every call's inputs, callee entries (with the
     * bytes each callee wrote) and outputs, for the original-instruction
     * replay tools/test_status_pages_live.py. */
    FILE *trace;
    uint8_t *shadow;       /* the traced views' bytes before a callee */
    size_t shadow_size;
    /* A MAP node's call (002101C0, inside 001B0000's dispatch) is its own
     * T..E record, kept in `nodes` and appended after the page's E record;
     * its callee snapshots use `shadow_node`. `node` is the traced record
     * (its pool view is that record alone, its stack the 0x200 bytes below
     * `node_sp`). */
    uint8_t *nodes, *shadow_node;
    size_t node_len, node_cap;
    int to_nodes;
    uint32_t node, node_sp;
};

static void put32(uint8_t *p, uint32_t v);

/* The views a trace snapshots: all but the read-only .data windows (and
 * the record container, whose bytes the replay's image holds and which
 * every call checks unchanged: check_readonly), the stack only from 0x3000
 * below the entry sp. A node record's call snapshots its pool record and
 * the 0x200 stack bytes below its sp instead. */
static int traced(const EmStatusPagesLive *l, uint32_t i, uint32_t *address, uint32_t *size)
{
    const EmArea01RenderView *v = &l->views[i];
    const EmGsDataWindow *w;
    const unsigned n = em_gs_texture_windows(l->gs, &w);
    for (unsigned k = 0; k < n; ++k)
        if (v->address == w[k].address)
            return 0;
    *address = v->address;
    *size = v->size;
    if (v->address == STACK_BASE) {
        *address = l->node ? l->node_sp - 0x200u : STACK_BASE + STACK_SIZE - 0x3000u;
        *size = l->node ? 0x200u : 0x3000u;
    } else if (v->address == POOL && l->node) {
        *address = l->node;
        *size = 0x2F0u;
    }
    return 1;
}

static void trace_put(EmStatusPagesLive *l, const void *bytes, size_t n)
{
    if (!l->to_nodes) {
        fwrite(bytes, 1, n, l->trace);
        return;
    }
    if (l->node_len + n > l->node_cap) {
        size_t cap = l->node_cap ? l->node_cap : 1u << 20;
        while (cap < l->node_len + n)
            cap *= 2;
        uint8_t *grown = realloc(l->nodes, cap);
        if (!grown)
            return; /* the file then fails to parse: the replay reports it */
        l->nodes = grown;
        l->node_cap = cap;
    }
    memcpy(l->nodes + l->node_len, bytes, n);
    l->node_len += n;
}

static void trace_byte(EmStatusPagesLive *l, uint8_t b)
{
    trace_put(l, &b, 1);
}

static void trace_u32(EmStatusPagesLive *l, uint32_t v)
{
    uint8_t b[4];
    put32(b, v);
    trace_put(l, b, 4);
}

static void trace_views(EmStatusPagesLive *l)
{
    uint32_t count = 0, address, size;
    for (uint32_t i = 0; i < l->view_count; ++i)
        count += (uint32_t)traced(l, i, &address, &size);
    trace_u32(l, count);
    for (uint32_t i = 0; i < l->view_count; ++i)
        if (traced(l, i, &address, &size)) {
            trace_u32(l, address);
            trace_u32(l, size);
            trace_put(l, l->views[i].bytes + (address - l->views[i].address), size);
        }
}

static void shadow_take(EmStatusPagesLive *l)
{
    size_t at = 0;
    uint32_t address, size;
    for (uint32_t i = 0; i < l->view_count; ++i)
        if (traced(l, i, &address, &size)) {
            if (at + size <= l->shadow_size)
                memcpy(l->shadow + at, l->views[i].bytes + (address - l->views[i].address), size);
            at += size;
        }
}

/* The runs of bytes that changed since shadow_take: the count, then each
 * run (two passes: the file is opened for appending). */
static uint32_t diff_runs(EmStatusPagesLive *l, int write)
{
    size_t at = 0;
    uint32_t address, size, runs = 0;
    for (uint32_t i = 0; i < l->view_count; ++i)
        if (traced(l, i, &address, &size)) {
            const uint8_t *now = l->views[i].bytes + (address - l->views[i].address);
            for (uint32_t k = 0; k < size;) {
                if (at + k >= l->shadow_size || now[k] == l->shadow[at + k]) {
                    ++k;
                    continue;
                }
                uint32_t e = k;
                while (e < size && at + e < l->shadow_size && now[e] != l->shadow[at + e])
                    ++e;
                if (write) {
                    trace_u32(l, address + k);
                    trace_u32(l, e - k);
                    trace_put(l, now + k, e - k);
                }
                ++runs;
                k = e;
            }
            at += size;
        }
    return runs;
}

static void trace_writes(EmStatusPagesLive *l)
{
    trace_u32(l, diff_runs(l, 0));
    (void)diff_runs(l, 1);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void view(EmStatusPagesLive *l, uint32_t address, uint32_t size, uint8_t *bytes)
{
    if (l->view_count < MAX_VIEWS && bytes && size)
        l->views[l->view_count++] = (EmArea01RenderView){address, size, bytes};
}

/* The views of one call, in the order the header lists. The scratchpad
 * work area is split around the counter and the mode byte, which have
 * views of their own (no byte lies in two views). */
static void build_views(EmStatusPagesLive *l, const EmStatusPagesFrame *f)
{
    l->view_count = 0;
    view(l, T_UI, 0xA0, f->ui);
    view(l, EM_SCENE_REQ_BASE, EM_SCENE_REQ_SIZE, f->req);
    for (uint32_t a = EM_SCENE_PROGRESS_BASE; a < EM_SCENE_PROGRESS_BASE + EM_SCENE_PROGRESS_SIZE;) {
        if (!em_scene_progress_canonical(a, 1)) {
            ++a;
            continue;
        }
        uint32_t end = a;
        while (end < EM_SCENE_PROGRESS_BASE + EM_SCENE_PROGRESS_SIZE &&
               em_scene_progress_canonical(end, 1))
            ++end;
        view(l, a, end - a, f->progress + (a - EM_SCENE_PROGRESS_BASE));
        a = end;
    }
    view(l, 0x00810858u, 8, l->vitals);
    view(l, 0x008104D0u, 4, l->player220);
    view(l, 0x00810C61u, 1, &l->c61);
    view(l, 0x00810C62u, 1, f->c62);
    view(l, 0x00810CB4u, 2, (uint8_t *)f->cb4);
    view(l, NUMBER_BASE, NUMBER_SIZE, l->number_buffer);
    view(l, 0x00810E64u, 2, l->stick);
    view(l, 0x00810E70u, 2, l->pads[0]);
    view(l, 0x00810E74u, 2, l->pads[1]);
    view(l, 0x00810E78u, 2, l->pads[2]);
    view(l, 0x002821B0u, (uint32_t)sizeof(EmMessageBlock), (uint8_t *)f->message);
    view(l, 0x00275BD8u, 1, f->busy);
    const EmGsDataWindow *w;
    const unsigned n = em_gs_texture_windows(l->gs, &w);
    uint32_t at = 0;
    for (unsigned i = 0; i < n; ++i) {
        view(l, w[i].address, w[i].size, l->data + at);
        at += w[i].size;
    }
    view(l, STRING_BASE, STRING_SIZE, l->string);
    view(l, 0x70003B64u, 4, l->counter);
    view(l, 0x70003B8Du, 1, f->spad3B8D);
    view(l, SPAD_BASE, 0x70003B64u - SPAD_BASE, l->spad);
    view(l, 0x70003B68u, 0x70003B8Du - 0x70003B68u, l->spad + (0x70003B68u - SPAD_BASE));
    view(l, 0x70003B8Eu, SPAD_BASE + SPAD_SIZE - 0x70003B8Eu, l->spad + (0x70003B8Eu - SPAD_BASE));
    view(l, 0x010202B0u, 1, &l->probe);
    if (f->models) {
        /* MAP (0020F950, its nodes' 002101C0 and helpers). A source the
         * port does not hold leaves its view out, so a read faults. */
        uint32_t word = 0;
        if (f->d28A570)
            word = *f->d28A570;
        else
            (void)em_gs_texture_word(l->gs, D_0028A570, &word);
        put32(l->d28A570, word);
        const unsigned free_slots = em_status_models_free_slots(f->models);
        l->bcc[0] = (uint8_t)free_slots;
        l->bcc[1] = (uint8_t)(free_slots >> 8);
        view(l, 0x00810700u, 3, f->area);
        view(l, POOL, POOL_SIZE, em_status_models_pool_bytes(f->models));
        view(l, 0x00810610u, 0x40u, em_status_models_view_bytes(f->models));
        view(l, D_0028A570, 4, l->d28A570);
        view(l, 0x00275BCCu, 2, l->bcc);
        if (f->player_position && f->player_yaw) {
            memcpy(l->player_a0, f->player_position, 12);
            memcpy(l->player_c4, f->player_yaw, 4);
            view(l, 0x00810350u, 12, l->player_a0);
            view(l, 0x00810374u, 4, l->player_c4);
        }
        const uint8_t *word_bytes = em_rcl_bytes(0x00275670u, 4);
        const uint8_t *zoom = em_rcl_bytes(EM_RCL_CONTEXT + 0x2468u, 4);
        if (word_bytes && zoom) {
            memcpy(l->ctx_word, word_bytes, 4);
            memcpy(l->ctx_zoom, zoom, 4);
            view(l, 0x00275670u, 4, l->ctx_word);
            view(l, EM_RCL_CONTEXT + 0x2468u, 4, l->ctx_zoom);
        }
    }
    view(l, STACK_BASE, STACK_SIZE, l->stack);
    for (uint32_t i = 0; i < l->view_count; ++i)
        l->regions[i] = (EmSulRegion){l->views[i].address, l->views[i].size, l->views[i].bytes};
    l->mem = (EmSulMemory){l->regions, l->view_count};
    l->s.core.world = (EmArea01RenderWorld){l->views, l->view_count};
}

static uint8_t *at_byte(EmStatusPagesLive *l, uint32_t address, uint32_t size)
{
    for (uint32_t i = 0; i < l->view_count; ++i) {
        const EmArea01RenderView *v = &l->views[i];
        if (address >= v->address && (uint64_t)address + size <= (uint64_t)v->address + v->size)
            return v->bytes + (address - v->address);
    }
    return NULL;
}

/* The read-only views after a call: the .data windows (the pages' copy
 * against the file's), D_0028A570, D_00275BCC, the player record's bytes
 * and the render context's. A store into one would be kept by the port and
 * not by the original's image, so it faults. 0, or -1 (reported). */
static int check_readonly(EmStatusPagesLive *l, const EmStatusPagesFrame *f)
{
    const EmGsDataWindow *w;
    const unsigned n = em_gs_texture_windows(l->gs, &w);
    uint32_t at = 0;
    for (unsigned i = 0; i < n; ++i) {
        if (memcmp(l->data + at, w[i].bytes, w[i].size)) {
            fprintf(stderr, "status pages: %08X stored into the read-only .data window %08X\n",
                    (unsigned)l->page, (unsigned)w[i].address);
            return -1;
        }
        at += w[i].size;
    }
    if (!f->models)
        return 0;
    uint32_t word = 0;
    if (f->d28A570)
        word = *f->d28A570;
    else
        (void)em_gs_texture_word(l->gs, D_0028A570, &word);
    const unsigned free_slots = em_status_models_free_slots(f->models);
    const uint8_t *word_bytes = em_rcl_bytes(0x00275670u, 4);
    const uint8_t *zoom = em_rcl_bytes(EM_RCL_CONTEXT + 0x2468u, 4);
    uint8_t d28A570[4];
    put32(d28A570, word);
    if (memcmp(l->d28A570, d28A570, 4) || l->bcc[0] != (uint8_t)free_slots ||
        l->bcc[1] != (uint8_t)(free_slots >> 8) ||
        (f->player_position && (memcmp(l->player_a0, f->player_position, 12) ||
                                memcmp(l->player_c4, f->player_yaw, 4))) ||
        (word_bytes && zoom && (memcmp(l->ctx_word, word_bytes, 4) || memcmp(l->ctx_zoom, zoom, 4)))) {
        fprintf(stderr, "status pages: %08X stored into a read-only view (D_0028A570, D_00275BCC, "
                        "the player record or the render context)\n", (unsigned)l->page);
        return -1;
    }
    return 0;
}

static int report(EmStatusPagesLive *l, uint32_t callee, const char *why)
{
    fprintf(stderr, "status pages: %08X called %08X, %s\n", (unsigned)l->page, (unsigned)callee,
            why);
    return -1;
}

/* ---- the 2D layer ---------------------------------------------------- */

static int ok0(int accepted)
{
    return accepted ? 0 : -1;
}

static int sul_blend(void *c, int32_t slot, int32_t mode)
{
    return ok0(em_page_draw_blend(((EmStatusPagesLive *)c)->draw, slot, mode));
}

static int sul_sprite(void *c, int32_t slot, int32_t x, int32_t y, int32_t w, int32_t h,
                      uint32_t rgba, uint64_t tex0)
{
    return ok0(em_page_draw_sprite(((EmStatusPagesLive *)c)->draw, slot, x, y, w, h, rgba, tex0));
}

/* A style record's colour: the low 24 bits of its first word (0 = none:
 * 001CC3B0 fills 0x80808080). */
static int style_rgb(EmStatusPagesLive *l, uint32_t style, uint32_t *rgb)
{
    if (!style) {
        *rgb = 0x808080u;
        return 1;
    }
    const uint8_t *p = at_byte(l, style, 8);
    if (!p)
        return 0;
    *rgb = get32(p) & 0xFFFFFFu;
    return 1;
}

static int sul_text_fixed(void *c, int32_t slot, int32_t x, int32_t y, int32_t w, int32_t h,
                          const char *text, uint32_t style)
{
    EmStatusPagesLive *l = c;
    uint32_t rgb;
    if (slot != 1 || !style_rgb(l, style, &rgb))
        return -1;
    return ok0(em_page_draw_text(l->draw, 0, x, y, w, h, text, rgb));
}

static int sul_text_proportional(void *c, int32_t slot, int32_t x, int32_t y, int32_t w,
                                 int32_t h, uint32_t text, uint32_t style)
{
    EmStatusPagesLive *l = c;
    char buffer[128];
    uint32_t rgb;
    if (slot != 1 || !style_rgb(l, style, &rgb))
        return -1;
    for (unsigned i = 0;; ++i) {
        const uint8_t *p = i < sizeof buffer ? at_byte(l, text + i, 1) : NULL;
        if (!p)
            return -1;
        buffer[i] = (char)*p;
        if (!*p)
            break;
    }
    return ok0(em_page_draw_text(l->draw, 1, x, y, w, h, buffer, rgb));
}

/* 00208AD0's own leaves (em_status_draw workers: 1 = accepted). */
static int d_blend(void *c, unsigned mode)
{
    return em_page_draw_blend(((EmStatusPagesLive *)c)->draw, 1, (int32_t)mode);
}

static int d_rectangle(void *c, int x0, int y0, int x1, int y1, uint32_t rgba)
{
    return em_page_draw_rectangle(((EmStatusPagesLive *)c)->draw, 1, x0, y0, x1, y1, rgba);
}

static int d_text(void *c, int proportional, int x, int y, int w, int h, const char *value,
                  uint64_t style)
{
    return em_page_draw_text(((EmStatusPagesLive *)c)->draw, proportional, x, y, w, h, value,
                             (uint32_t)style & 0xFFFFFFu);
}

static int d_arc(void *c, const float descriptor[24])
{
    return em_page_draw_arc(((EmStatusPagesLive *)c)->draw, descriptor);
}

static int d_sprite(void *c, int x, int y, int w, int h, uint32_t rgba, uint64_t tex0)
{
    return em_page_draw_sprite(((EmStatusPagesLive *)c)->draw, 1, x, y, w, h, rgba, tex0);
}

/* 00208AD0(page, x, y): the health gauge over D_00810858 and the shared
 * UI+0x20 clock it advances. */
static int sul_health(void *c, uint32_t page, int32_t x, int32_t y)
{
    EmStatusPagesLive *l = c;
    const EmStatusPagesFrame *f = l->frame;
    if (page != T_UI || !f->health_data || !f->ui_clock)
        return -1;
    float health;
    memcpy(&health, l->vitals, 4);
    uint32_t clock = get32(f->ui + 0x20);
    const EmStatusDrawWorkers workers = {l, d_blend, d_rectangle, d_text, d_arc, d_sprite};
    if (em_status_health_draw(&clock, health, f->warning, x, y, f->health_data, &workers) != 1)
        return -1;
    put32(f->ui + 0x20, clock);
    return 0;
}

static int sul_float_to_int(void *c, uint32_t bits, int32_t *value)
{
    (void)c;
    *value = em_player_float_to_int(bits);
    return 0;
}

static int sul_format(void *c, int32_t value, int32_t a1, int32_t a2, const char **text)
{
    EmStatusPagesLive *l = c;
    if (!em_status_draw_001C5FB0(l->number, value, a1, a2))
        return -1;
    *text = l->number;
    return 0;
}

static int sul_copy(void *c, char *dst, size_t dst_size, const char *src)
{
    (void)c;
    const size_t n = src ? strlen(src) + 1 : 0;
    if (!src || n > dst_size)
        return -1;
    memcpy(dst, src, n);
    return 0;
}

static int sul_sound(void *c, int32_t id, int32_t a1, int32_t a2, int32_t a3)
{
    EmStatusPagesLive *l = c;
    return l->host.sound && l->host.sound(l->host.context, id, a1, a2, a3) >= 0 ? 0 : -1;
}

static int rect_float_to_int(void *c, uint32_t bits, int32_t *value)
{
    return sul_float_to_int(c, bits, value);
}

static int rect_rectangle(void *c, int32_t slot, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                          uint32_t rgba)
{
    return ok0(em_page_draw_rectangle(((EmStatusPagesLive *)c)->draw, slot, x0, y0, x1, y1, rgba));
}

static int message_present(void *c, int32_t a0, int32_t a1, int32_t a2, int32_t a3)
{
    EmStatusPagesLive *l = c;
    return l->host.present && l->host.present(l->host.context, a0, a1, a2, a3) >= 0 ? 0 : -1;
}

static int spr_sound(void *c, int32_t id, int32_t a1, int32_t a2, int32_t a3)
{
    return sul_sound(c, id, a1, a2, a3);
}

/* 001C47E0's bytes: the migrated item block. */
static uint8_t *consume_at(void *c, uint32_t address, uint32_t size)
{
    EmStatusPagesLive *l = c;
    if (!em_scene_progress_canonical(address, size))
        return NULL;
    return l->frame->progress + (address - EM_SCENE_PROGRESS_BASE);
}

/* A .data halfword by original address (0015C7C0's clip pairs). */
static int table16(void *c, uint32_t address, int16_t *value)
{
    EmStatusPagesLive *l = c;
    const EmGsDataWindow *w;
    const unsigned n = em_gs_texture_windows(l->gs, &w);
    for (unsigned i = 0; i < n; ++i)
        if (address >= w[i].address && address + 2 <= w[i].address + w[i].size) {
            const uint8_t *p = w[i].bytes + (address - w[i].address);
            *value = (int16_t)(uint16_t)(p[0] | p[1] << 8);
            return 0;
        }
    return -1;
}

/* ---- per-call state -------------------------------------------------- */

static void vitals_in(EmStatusPagesLive *l, const EmStatusPagesFrame *f)
{
    memcpy(l->vitals, f->health, 4);
    memcpy(l->vitals + 4, f->infection, 4);
    memcpy(l->player220, f->health, 4);
}

/* The page's health stores back to the port's one copy (g.status). The
 * port holds the player record's +0x220 and D_00810858 as that one value
 * (em_player.c vitals_load), so a +0x220 store that D_00810858 does not
 * hold faults (002160B0 stores both with the same value). */
static int vitals_out(EmStatusPagesLive *l, const EmStatusPagesFrame *f)
{
    uint32_t health, player, before;
    memcpy(&health, l->vitals, 4);
    memcpy(&player, l->player220, 4);
    memcpy(&before, f->health, 4);
    if (player != before && player != health)
        return -1;
    memcpy(f->health, l->vitals, 4);
    memcpy(f->infection, l->vitals + 4, 4);
    memcpy(l->player220, l->vitals, 4);
    return 0;
}

/* 0020AC70's triangles (00208AB0's fans). */
static int trail_emit(void *c, const int32_t xy[3][2], unsigned intensity)
{
    return em_page_draw_triangle(((EmStatusPagesLive *)c)->draw, xy, intensity);
}

/* ---- the dispatcher -------------------------------------------------- */

static void count(EmStatusPagesLive *l, uint32_t callee)
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

static int dispatch_call(void *ctx, uint32_t target, uint32_t sp, const uint64_t *a, unsigned na,
                         const uint32_t *f, unsigned nf, uint64_t *v0, uint32_t *f0);

/* The dispatcher, traced: 'C', target, sp, the argument registers, the
 * result, v0 / f0 and the bytes the callee wrote. A callee a traced callee
 * reaches through the dispatcher (an AREA01 page's helpers) is inside it. */
static int dispatch(void *ctx, uint32_t target, uint32_t sp, const uint64_t *a, unsigned na,
                    const uint32_t *f, unsigned nf, uint64_t *v0, uint32_t *f0)
{
    EmStatusPagesLive *l = ctx;
    if (!l->trace || l->shadow_size == (size_t)-1)
        return dispatch_call(ctx, target, sp, a, na, f, nf, v0, f0);
    shadow_take(l);
    const size_t saved = l->shadow_size;
    l->shadow_size = (size_t)-1; /* nested dispatches are part of this one */
    *v0 = 0;
    *f0 = 0;
    const int rc = dispatch_call(ctx, target, sp, a, na, f, nf, v0, f0);
    l->shadow_size = saved;
    trace_byte(l, 'C');
    trace_u32(l, target);
    trace_u32(l, sp);
    trace_u32(l, na);
    for (unsigned i = 0; i < na; ++i) {
        trace_u32(l, (uint32_t)a[i]);
        trace_u32(l, (uint32_t)(a[i] >> 32));
    }
    trace_u32(l, nf);
    for (unsigned i = 0; i < nf; ++i)
        trace_u32(l, f[i]);
    trace_u32(l, (uint32_t)rc);
    trace_u32(l, (uint32_t)*v0);
    trace_u32(l, (uint32_t)(*v0 >> 32));
    trace_u32(l, *f0);
    trace_writes(l);
    return rc;
}

static int dispatch_call(void *ctx, uint32_t target, uint32_t sp, const uint64_t *a, unsigned na,
                         const uint32_t *f, unsigned nf, uint64_t *v0, uint32_t *f0)
{
    EmStatusPagesLive *l = ctx;
    const EmStatusPagesFrame *fr = l->frame;
    count(l, target);
#define A(i) ((i) < na ? a[i] : 0)
#define I(i) ((int32_t)(uint32_t)A(i))
#define U(i) ((uint32_t)A(i))
    switch (target) {
    case 0x00207D00u:
        return ok0(em_page_draw_blend(l->draw, I(0), I(1)));
    case 0x00207E40u:
        return ok0(em_page_draw_sprite(l->draw, I(0), I(1), I(2), I(3), I(4), U(5), A(6)));
    case 0x00207D90u: /* (slot, x0, y0, x1, y1): SCISSOR_1's SCAX0, SCAY0, SCAX1, SCAY1 */
        return ok0(em_page_draw_scissor(l->draw, I(0), I(1), I(2), I(3), I(4)));
    case 0x001281C0u:
        if (nf < 1)
            return -1;
        *v0 = (uint64_t)(int64_t)em_player_float_to_int(f[0]);
        return 0;
    case 0x0020A7A0u:
        return ok0(em_page_draw_background(l->draw, A(0)));
    case 0x0020AE40u:
        return em_sul_0020AE40(&l->sul, &l->mem, U(0), U(1), I(2));
    case 0x0020B0D0u:
        return em_sul_0020B0D0(&l->sul, &l->mem, U(1));
    case 0x0020B210u: {
        if (U(0) != T_UI)
            return report(l, target, "on a page other than D_00810130");
        EmSulListGlobals g = {fr->message->phase, (int32_t)fr->message->line,
                              fr->message->aux_mode};
        int32_t result = 0;
        const int rc = em_sul_0020B210(&l->sul, &l->mem, fr->ui, 0xA0, U(1), A(2), I(3), &g, &result);
        fr->message->phase = g.d2821B4;
        fr->message->line = (uint32_t)g.d2821B8;
        fr->message->aux_mode = g.d282240;
        *v0 = (uint64_t)(int64_t)result;
        return rc;
    }
    case 0x0020BEF0u: {
        int32_t index = 0;
        if (U(0) != T_UI || em_sul_0020BEF0(fr->ui, 0xA0, &index) < 0)
            return -1;
        *v0 = (uint64_t)(int64_t)index;
        return 0;
    }
    case 0x0020CD40u:
        return em_sul_0020CD40(&l->sul);
    case 0x0020CD60u:
        return em_sul_0020CD60(&l->sul);
    case 0x0020CDA0u:
        return em_sul_0020CDA0(&l->sul);
    case 0x0020CD80u:
        return em_spr_0020CD80(&l->spr);
    case 0x0020CCB0u:
        if (U(0) != T_UI)
            return -1;
        return em_cs_0020CCB0(&l->rect, fr->ui, 0xA0);
    case 0x001FCF10u: {
        EmRvrFault fault = {0, 0};
        return em_rvr_001FCF10(&l->message, &fault);
    }
    case 0x001FB9F0u:
        return sul_sound(l, I(0), I(1), I(2), I(3));
    case 0x001FF080u:
        if (A(0) != 0 || !fr->module_load)
            return -1;
        return fr->module_load(fr->module_context, U(1)) == 1 ? 0 : -1;
    case 0x00185420u: {
        uint32_t owner = 0;
        if (!l->host.find_device || l->host.find_device(l->host.context, I(0), &owner) < 0)
            return report(l, target, "found no bound device owner");
        *v0 = (uint64_t)(int64_t)(int32_t)owner;
        return 0;
    }
    case 0x001C47E0u: {
        int32_t result = 0;
        if (em_pickup_items_001C47E0(consume_at, l, I(0), I(1), &result) < 0)
            return -1;
        *v0 = (uint64_t)(int64_t)result;
        return 0;
    }
    case 0x0015C700u:
        /* 002160B0 state 5 stored the health first: hand it to the port's
         * one copy before the player worker reads it. */
        if (U(0) != PLAYER || !l->host.player_0015C700 || vitals_out(l, fr) < 0)
            return -1;
        return l->host.player_0015C700(l->host.context, table16, l) >= 0 ? 0 : -1;
    /* the number fields: 001C5FB0(value, width, blank) into its buffer
     * D_008111D0 (returned), 00123168 strcpy(dst, src), 001CBA50(slot, x,
     * y, w, h, text, style) */
    case 0x001C5FB0u: {
        char text[8];
        if (!em_status_draw_001C5FB0(text, I(0), I(1), I(2)))
            return -1;
        memcpy(l->number_buffer, text, strlen(text) + 1);
        *v0 = (uint64_t)(int64_t)(int32_t)NUMBER_BASE;
        return 0;
    }
    case 0x00123168u: {
        for (uint32_t i = 0;; ++i) {
            const uint8_t *src = at_byte(l, U(1) + i, 1);
            uint8_t *dst = at_byte(l, U(0) + i, 1);
            if (!src || !dst || i >= 256)
                return report(l, target, "copies outside the views");
            *dst = *src;
            if (!*src)
                break;
        }
        *v0 = A(0);
        return 0;
    }
    case 0x001CBA50u: {
        char text[128];
        for (unsigned i = 0;; ++i) {
            const uint8_t *p = i < sizeof text ? at_byte(l, U(5) + i, 1) : NULL;
            if (!p)
                return report(l, target, "reads its text outside the views");
            text[i] = (char)*p;
            if (!*p)
                break;
        }
        return sul_text_fixed(l, I(0), I(1), I(2), I(3), I(4), text, U(6));
    }
    /* the record container's lines: 001FCF60(line, x, y) and 001FE070(bank,
     * index, x, y) over the container view at its original address */
    case 0x001FCF60u:
        return em_message_live_record_draw(I(0), I(1), I(2)) < 0 ? -1 : 0;
    case 0x001FE070u: {
        uint8_t *bank = at_byte(l, U(0), 1);
        uint32_t size = 0;
        for (uint32_t i = 0; i < l->view_count && bank; ++i)
            if (U(0) >= l->views[i].address && U(0) < l->views[i].address + l->views[i].size)
                size = l->views[i].address + l->views[i].size - U(0);
        if (!bank)
            return report(l, target, "names a bank outside the views");
        const EmMessageBank sub = {bank, size};
        int32_t result = 0;
        if (em_message_live_fe070(&sub, I(1), I(2), I(3), &result) < 0)
            return -1;
        *v0 = (uint64_t)(int64_t)result;
        return 0;
    }
    /* the SPR4 selector's stick: 0020D930(t, mode) with 001B62C0's
     * record at 0x700038A0, 0020E020 (the trail reset) and 0020AC70(t,
     * base, a2) (the trail; a2 != 0 zeroes the new ring slot instead of
     * sampling; the base floats get 1792 / 1824 added in place) */
    case 0x0020D930u: {
        EmItemStick stick;
        uint8_t *hover = at_byte(l, U(0) + 0x11u, 1);
        uint8_t *record = at_byte(l, 0x700038A0u, 16);
        if (!hover || !record || !fr->math ||
            !em_item_stick_sample(&stick, fr->stick_x, fr->stick_y, fr->math))
            return -1;
        memcpy(record, &stick.x, 4);
        memcpy(record + 4, &stick.y, 4);
        memcpy(record + 8, &stick.magnitude, 4);
        memcpy(record + 12, &stick.angle, 4);
        if (em_menu_hover_0020D930(hover, I(1), stick.magnitude, stick.angle))
            return sul_sound(l, 5, 0x1000, 0x1000, 0x1000);
        return 0;
    }
    case 0x0020E020u:
        if (!fr->trail)
            return -1;
        em_item_trail_reset(fr->trail);
        return 0;
    case 0x0020AC70u: {
        EmItemStick stick = {0, 0, 0, 0};
        uint8_t *base = at_byte(l, U(1), 8);
        if (!base || !fr->trail || !fr->math ||
            (A(2) == 0 && !em_item_stick_sample(&stick, fr->stick_x, fr->stick_y, fr->math)))
            return -1;
        uint32_t x, y;
        memcpy(&x, base, 4);
        memcpy(&y, base + 4, 4);
        float fx, fy;
        memcpy(&fx, &x, 4);
        memcpy(&fy, &y, 4);
        if (!em_item_trail_step(fr->trail, &stick, fx, fy, fr->math, trail_emit, l))
            return -1;
        x = em_ee_add_bits(0x44E00000u, x); /* 1792.0 + x */
        y = em_ee_add_bits(0x44E40000u, y); /* 1824.0 + y */
        memcpy(base, &x, 4);
        memcpy(base + 4, &y, 4);
        return 0;
    }
    /* MAP: the UI pool and its models (em_status_models, by original
     * address); 001B0000 also marks the models' place in the draw stream.
     * D_00275BCC follows the port's free slots after a pop or a push. */
    case 0x001AFF10u:
    case 0x001AFF90u:
    case 0x001C6120u:
    case 0x001CA5E0u:
    case 0x001C6150u:
    case 0x001AF7C0u:
    case 0x001CB5B0u:
    case 0x001C62C0u:
    case 0x001C6380u:
    case 0x001CB480u:
    case 0x001B0000u: {
        if (!fr->models)
            return report(l, target, "has no UI pool (no em_status_models)");
        const uint32_t saved = l->dispatch_sp;
        if (target == 0x001B0000u) {
            if (!em_page_draw_models(l->draw))
                return -1;
            l->dispatch_sp = sp;
        }
        const int rc = em_status_models_call(fr->models, target, a, na, v0);
        l->dispatch_sp = saved;
        const unsigned free_slots = em_status_models_free_slots(fr->models);
        l->bcc[0] = (uint8_t)free_slots;
        l->bcc[1] = (uint8_t)(free_slots >> 8);
        return rc;
    }
    /* 00210F30's marker: 00208040(1, a, b, c, rgba), each corner's XYZF2
     * = sx32(float_to_int(x) | float_to_int(z) << 16) (z converted first):
     * X its low halfword, Y its high one. */
    case 0x00208040u: {
        int32_t xy[3][2];
        for (unsigned i = 0; i < 3; ++i) {
            const uint8_t *v = at_byte(l, U(1 + i), 12);
            if (!v)
                return report(l, target, "reads a corner outside the views");
            const uint32_t hi = (uint32_t)em_player_float_to_int(get32(v + 8)) << 16;
            const uint32_t word = (uint32_t)em_player_float_to_int(get32(v)) | hi;
            xy[i][0] = (int32_t)(word & 0xFFFFu);
            xy[i][1] = (int32_t)(word >> 16);
        }
        return ok0(em_page_draw_flat(l->draw, I(0), xy, U(4)));
    }
    /* The SDK leaves MAP reaches, over the views by original address. */
    case 0x001029C0u: {
        uint8_t *m = at_byte(l, U(0), 64);
        float f16[16];
        if (!m || em_owner_services_identity_001029C0(f16))
            return report(l, target, "names a matrix outside the views");
        memcpy(m, f16, 64);
        return 0;
    }
    case 0x00102BB0u: {
        uint8_t *d = at_byte(l, U(0), 64);
        const uint8_t *src = at_byte(l, U(1), 64);
        float fd[16], fs[16];
        if (!d || !src || nf < 1)
            return report(l, target, "names a matrix outside the views");
        memcpy(fs, src, 64);
        if (em_owner_services_rotate_y_00102BB0(fd, fs, f[0]))
            return report(l, target, "an unmeasured VU form");
        memcpy(d, fd, 64);
        return 0;
    }
    case 0x001026A0u: {
        uint8_t *out = at_byte(l, U(0), 16);
        const uint8_t *mat = at_byte(l, U(1), 64), *vec = at_byte(l, U(2), 16);
        float fm[16], fv[4], fo[4];
        if (!out || !mat || !vec)
            return report(l, target, "names a vector outside the views");
        memcpy(fm, mat, 64);
        memcpy(fv, vec, 16);
        em_effect_original_001026A0(fo, fm, fv);
        memcpy(out, fo, 16);
        return 0;
    }
    case 0x001026D0u: {
        uint8_t *out = at_byte(l, U(0), 64);
        const uint8_t *ma = at_byte(l, U(1), 64), *mb = at_byte(l, U(2), 64);
        uint32_t wa[16], wb[16], r[16];
        if (!out || !ma || !mb)
            return report(l, target, "names a matrix outside the views");
        memcpy(wa, ma, 64);
        memcpy(wb, mb, 64);
        if (em_sdk_vu0_001026D0(r, wa, wb) != EM_EE_FLOAT_OK)
            return report(l, target, "an unmeasured VU form");
        memcpy(out, r, 64);
        return 0;
    }
    case 0x001031E0u: { /* xyz of a1 into a0 */
        uint8_t *d = at_byte(l, U(0), 12);
        const uint8_t *src = at_byte(l, U(1), 12);
        if (!d || !src)
            return report(l, target, "names a vector outside the views");
        memmove(d, src, 12);
        return 0;
    }
    case 0x001B1470u:
        if (nf < 1)
            return -1;
        *f0 = em_player_001B1470(f[0]);
        return 0;
    /* the lane's helpers when an AREA01 page (MAP, DATABASE) calls them */
    case 0x00211240u:
        return em_status_pages_00211240(&l->s, I(0));
    case 0x00211310u:
        return em_status_pages_00211310(&l->s, U(0));
    case 0x002117D0u:
        return em_status_pages_002117D0(&l->s, A(0), U(1), I(2), I(3));
    case 0x00213A00u: {
        uint32_t r = 0;
        const int rc = em_status_pages_00213A00(&l->s, U(0), I(1), &r);
        *v0 = (uint64_t)(int64_t)(int32_t)r;
        return rc;
    }
    case 0x00213C50u:
        return em_status_pages_00213C50(&l->s, U(0), I(1));
    case 0x00213CC0u: {
        uint32_t r = 0;
        const int rc = em_status_pages_00213CC0(&l->s, U(0), &r);
        *v0 = (uint64_t)(int64_t)(int32_t)r;
        return rc;
    }
    case 0x001FCF30u: {
        uint32_t r = 0;
        const int rc = em_status_pages_001FCF30(&l->s, A(0), A(1), A(2), &r);
        *v0 = (uint64_t)(int64_t)(int32_t)r;
        return rc;
    }
    case 0x00182B30u:
    case 0x0015C750u:
        return report(l, target, "which the first level cannot reach (docs/STATUS_PAGES.md "
                                 "section 1), is not bound");
    default:
        return report(l, target, "which is not bound");
    }
#undef A
#undef I
#undef U
}

/* ---- MAP's nodes ------------------------------------------------------ */

/* 002101C0(record), from the walk 001B0000 dispatched: the translation
 * over the same views (its record in the pool view), with the stack
 * pointer 001B0000's frame (0x30 bytes) leaves below the dispatch's.
 * Traced as its own T..E record (page 0x002101C0, a0 = the record), kept
 * until the page's E record is written. */
static int node_002101C0(void *ctx, uint32_t fn, uint32_t record)
{
    EmStatusPagesLive *l = ctx;
    if (fn != NODE_002101C0 || !l->frame)
        return -1;
    const uint32_t saved_sp = l->s.sp;
    l->s.sp = l->dispatch_sp - 0x30u;
    uint8_t *const saved_shadow = l->shadow;
    const size_t saved_size = l->shadow_size;
    if (l->trace) {
        l->to_nodes = 1;
        l->node = record;
        l->node_sp = l->s.sp;
        l->shadow = l->shadow_node;
        l->shadow_size = 0x40000;
        trace_byte(l, 'T');
        trace_u32(l, fn);
        trace_u32(l, record);
        trace_u32(l, 0);
        trace_u32(l, l->s.sp);
        trace_views(l);
    }
    const int rc = em_area01_ui_002101C0(&l->s, record);
    if (l->trace) {
        trace_byte(l, 'E');
        trace_u32(l, (uint32_t)rc);
        trace_views(l);
        l->shadow = saved_shadow;
        l->shadow_size = saved_size;
        l->node = 0;
        l->to_nodes = 0;
    }
    l->s.sp = saved_sp;
    if (rc < 0 || em_a01r_latched(&l->s.core) || check_readonly(l, l->frame) < 0)
        return -1;
    return 0;
}

/* The page stream's 001B0000 place: the host draws the walk's models. */
static int models_draw(void *ctx, EmGfx *gfx, const float clip[4])
{
    EmStatusPagesLive *l = ctx;
    return l->host.models_draw ? l->host.models_draw(l->host.context, gfx, clip) : 0;
}

/* ---- entries ---------------------------------------------------------- */

EmStatusPagesLive *em_status_pages_live_load(const char *path, const EmStatusPagesHost *host)
{
    if (!host)
        return NULL;
    EmStatusPagesLive *l = calloc(1, sizeof *l);
    if (!l)
        return NULL;
    l->host = *host;
    l->gs = em_gs_texture_load(path);
    l->draw = l->gs ? em_page_draw_create(l->gs) : NULL;
    const EmGsDataWindow *w;
    const unsigned n = l->gs ? em_gs_texture_windows(l->gs, &w) : 0;
    size_t size = 0;
    for (unsigned i = 0; i < n; ++i)
        size += w[i].size;
    if (!l->draw || !n || !(l->data = malloc(size))) {
        em_status_pages_live_free(l);
        return NULL;
    }
    size = 0;
    for (unsigned i = 0; i < n; ++i) {
        memcpy(l->data + size, w[i].bytes, w[i].size);
        size += w[i].size;
    }
    l->sul = (EmSulWorkers){.context = l, .blend = sul_blend, .sprite = sul_sprite,
                            .text_fixed = sul_text_fixed,
                            .text_proportional = sul_text_proportional, .health = sul_health,
                            .float_to_int = sul_float_to_int, .format = sul_format,
                            .copy = sul_copy, .sound = sul_sound};
    l->rect = (EmCsRectWorkers){l, rect_float_to_int, rect_rectangle};
    l->message = (EmRvrMessageWorkers){l, message_present};
    memset(&l->spr, 0, sizeof l->spr);
    l->spr.context = l;
    l->spr.sound = spr_sound;
    l->probe = 0x2B;
    em_page_draw_set_models(l->draw, models_draw, l);
    const char *trace = getenv("EM_STATUS_PAGES_TRACE");
    if (trace && *trace) {
        l->trace = fopen(trace, "ab"); /* each load appends (the caller clears it) */
        l->shadow_size = 0x40000;
        l->shadow = malloc(l->shadow_size);
        l->shadow_node = malloc(l->shadow_size);
        if (!l->trace || !l->shadow || !l->shadow_node) {
            em_status_pages_live_free(l);
            return NULL;
        }
    }
    return l;
}

void em_status_pages_live_free(EmStatusPagesLive *l)
{
    if (!l)
        return;
    em_page_draw_free(l->draw);
    em_gs_texture_free(l->gs);
    if (l->trace)
        fclose(l->trace);
    free(l->shadow);
    free(l->shadow_node);
    free(l->nodes);
    free(l->data);
    free(l);
}

EmGsTexture *em_status_pages_live_gs(EmStatusPagesLive *l)
{
    return l ? l->gs : NULL;
}

int em_status_pages_live_tick(EmStatusPagesLive *l, uint32_t page, const EmStatusPagesFrame *f)
{
    if (!l || !f || !f->ui || !f->ui_clock || !f->busy || !f->req || !f->progress ||
        !f->message || !f->spad3B8D || !f->health || !f->infection)
        return -1;
    l->frame = f;
    l->page = page;
    l->callee_count = 0;
    build_views(l, f);
    vitals_in(l, f);
    put32(f->ui + 0x20, *f->ui_clock);
    l->pads[0][0] = (uint8_t)f->held;
    l->pads[0][1] = (uint8_t)(f->held >> 8);
    l->pads[1][0] = (uint8_t)f->pressed;
    l->pads[1][1] = (uint8_t)(f->pressed >> 8);
    l->pads[2][0] = (uint8_t)f->repeat;
    l->pads[2][1] = (uint8_t)(f->repeat >> 8);
    l->stick[0] = f->stick_x;
    l->stick[1] = f->stick_y;
    put32(l->counter, f->counter);
    if (!f->c62 || !f->cb4 || !f->fire_mode || !f->set_fire_mode)
        return -1;
    l->c61 = f->fire_mode();
    const uint8_t c61 = l->c61;
    em_page_draw_begin(l->draw);
    memset(&l->s.core.fault, 0, sizeof l->s.core.fault);
    l->s.core.function = 0;
    l->s.call = dispatch;
    l->s.ctx = l;
    l->s.sp = STACK_BASE + STACK_SIZE - 0x100;
    l->s.leaf_calls = 1; /* 0020F950 / 00210F30's 00207D90 / 00208040 reach em_page_draw */
    if (page != 0x00214570u && page != 0x00215870u && page != 0x002160B0u &&
        page != 0x00211970u && page != 0x00214020u && page != 0x0020F950u) {
        fprintf(stderr, "status pages: the page %08X is not bound\n", (unsigned)page);
        return -1;
    }
    if (page == 0x0020F950u && !f->models) {
        fprintf(stderr, "status pages: MAP 0020F950 has no UI pool (em_status_models)\n");
        return -1;
    }
    if (f->models)
        em_status_models_set_node(f->models, NODE_002101C0, node_002101C0, l);
    if (l->trace) {
        trace_byte(l, 'T');
        trace_u32(l, page);
        trace_u32(l, T_UI);
        trace_u32(l, page == 0x002160B0u ? T_UI : 0);
        trace_u32(l, l->s.sp);
        trace_views(l);
    }
    int rc;
    switch (page) {
    case 0x00214570u:
        rc = em_status_pages_00214570(&l->s, T_UI);
        break;
    case 0x00215870u:
        rc = em_status_pages_00215870(&l->s, T_UI);
        break;
    case 0x002160B0u:
        rc = em_status_pages_002160B0(&l->s, T_UI, T_UI);
        break;
    case 0x00211970u:
        rc = em_status_pages_00211970(&l->s, T_UI);
        break;
    case 0x00214020u:
        rc = em_area01_ui_00214020(&l->s, T_UI);
        break;
    case 0x0020F950u:
        rc = em_area01_ui_0020F950(&l->s, T_UI);
        break;
    default:
        fprintf(stderr, "status pages: the page %08X is not bound\n", (unsigned)page);
        return -1;
    }
    if (l->trace) {
        trace_byte(l, 'E');
        trace_u32(l, (uint32_t)rc);
        trace_views(l);
        /* the page's node calls, each its own record */
        if (l->node_len)
            fwrite(l->nodes, 1, l->node_len, l->trace);
        l->node_len = 0;
        fflush(l->trace);
    }
    if (f->models)
        em_status_models_set_node(f->models, 0, NULL, NULL);
    if (rc >= 0 && check_readonly(l, f) < 0)
        rc = -1;
    *f->ui_clock = get32(f->ui + 0x20);
    if (rc < 0 || em_a01r_latched(&l->s.core)) {
        fprintf(stderr, "status pages: %08X faulted in %08X (code %d, detail %08X)\n",
                (unsigned)page, (unsigned)l->s.core.fault.address, (int)l->s.core.fault.code,
                (unsigned)l->s.core.fault.detail);
        return -1;
    }
    if (l->c61 != c61) {
        /* SELECTOR's Yes: the fire mode em_weapon holds. */
        f->set_fire_mode(l->c61);
        if (f->fire_mode() != l->c61) {
            fprintf(stderr, "status pages: %08X stored D_00810C61 = %u, which em_weapon refuses\n",
                    (unsigned)page, (unsigned)l->c61);
            return -1;
        }
    }
    return vitals_out(l, f);
}

int em_status_pages_live_render(EmStatusPagesLive *l, EmGfx *gfx)
{
    return l && em_page_draw_render(l->draw, gfx) == 1 ? 1 : 0;
}

void em_status_pages_live_deactivate(EmStatusPagesLive *l)
{
    if (l)
        em_page_draw_deactivate(l->draw);
}

unsigned em_status_pages_live_calls(const EmStatusPagesLive *l, uint32_t callee)
{
    if (!l)
        return 0;
    for (unsigned i = 0; i < l->callee_count; ++i)
        if (l->callees[i][0] == callee)
            return l->callees[i][1];
    return 0;
}
