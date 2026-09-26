/* em_battery_page_live.c - see em_battery_page_live.h. */
#include "game/em_battery_page_live.h"

#include <string.h>

#include "game/em_census_standins.h"
#include "game/em_player_stage_workers.h"
#include "game/em_render_verify_rest.h"
#include "game/em_status_ui_leftovers.h"

typedef struct {
    const EmBatteryPageHost *host;
    const EmBatteryPageCall *call;
    EmSulWorkers sul;
    EmSulMemory mem;
    EmSulRegion regions[4];
    uint8_t frame_table[EM_BATTERY_UI_FRAME_TABLE_SIZE];
    uint8_t row_table[EM_BATTERY_UI_ROW_TABLE_SIZE];
    uint8_t d810E70[2], d810E78[2];
    EmCsRectWorkers rect;
    EmRvrMessageWorkers message;
} Ctx;

/* ---- the 2D leaves (em_battery_ui) --------------------------------------- */

static int leaf(int ok)
{
    return ok ? 0 : -1;
}

static int l_blend(void *c, int32_t slot, int32_t mode)
{
    return leaf(em_battery_ui_blend(((Ctx *)c)->call->draw, slot, mode));
}

static int l_sprite(void *c, int32_t slot, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgba,
                    uint64_t tex0)
{
    return leaf(em_battery_ui_sprite(((Ctx *)c)->call->draw, slot, x, y, w, h, rgba, tex0));
}

static int l_rectangle(void *c, int32_t slot, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                       uint32_t rgba)
{
    return leaf(em_battery_ui_rectangle(((Ctx *)c)->call->draw, slot, x0, y0, x1, y1, rgba));
}

/* 001281C0 */
static int l_float_to_int(void *c, uint32_t bits, int32_t *value)
{
    (void)c;
    *value = em_player_float_to_int(bits);
    return 0;
}

/* 00209280's own leaves (em_status_draw workers: 1 = accepted, slot 1). */
static int g_blend(void *c, unsigned mode)
{
    return em_battery_ui_blend(((Ctx *)c)->call->draw, 1, (int32_t)mode);
}

static int g_rectangle(void *c, int x0, int y0, int x1, int y1, uint32_t rgba)
{
    return em_battery_ui_rectangle(((Ctx *)c)->call->draw, 1, x0, y0, x1, y1, rgba);
}

static int g_text(void *c, int proportional, int x, int y, int w, int h, const char *value,
                  uint64_t style)
{
    return em_battery_ui_text(((Ctx *)c)->call->draw, proportional, x, y, w, h, value, style);
}

static int g_arc(void *c, const float descriptor[24])
{
    (void)c;
    (void)descriptor;
    return 0; /* 00209280 draws no arc */
}

static int g_sprite(void *c, int x, int y, int w, int h, uint32_t rgba, uint64_t tex0)
{
    return em_battery_ui_sprite(((Ctx *)c)->call->draw, 1, x, y, w, h, rgba, tex0);
}

/* 0020AE40's 00209280(page, x, y, tex0, compact): the gauge over the
 * charge D_00810CB2 (half-units), the capacity D_00810CB7 and D_00810C7F. */
static int s_battery(void *c, uint32_t page, int32_t x, int32_t y, uint64_t tex0, int32_t compact)
{
    Ctx *ctx = c;
    const EmSprRecords *r = &ctx->call->records;
    if (page != EM_SPR_PAGE_ADDRESS || !ctx->call->gauge)
        return -1;
    const uint16_t charge = (uint16_t)(r->d810CB2[0] | r->d810CB2[1] << 8);
    const EmStatusDrawWorkers draw = {ctx, g_blend, g_rectangle, g_text, g_arc, g_sprite};
    return em_status_battery_draw(charge, *r->d810CB7, r->d810C7F[0], x, y, tex0, compact,
                                  ctx->call->gauge, &draw) == 1
               ? 0
               : -1;
}

/* 001FB9F0 */
static int s_sound(void *c, int32_t id, int32_t a1, int32_t a2, int32_t a3)
{
    const EmBatteryPageHost *h = ((Ctx *)c)->host;
    return h->sound(h->context, id, a1, a2, a3);
}

/* Callees of the em_sul routines that the BATTERY page's flag words never
 * reach (flag 8's text and health, the node and context routines): a
 * call is a fault. */
static int u_text_fixed(void *c, int32_t s, int32_t x, int32_t y, int32_t w, int32_t h,
                        const char *t, uint32_t st)
{
    (void)c; (void)s; (void)x; (void)y; (void)w; (void)h; (void)t; (void)st;
    return -1;
}
static int u_text_proportional(void *c, int32_t s, int32_t x, int32_t y, int32_t w, int32_t h,
                               uint32_t t, uint32_t st)
{
    (void)c; (void)s; (void)x; (void)y; (void)w; (void)h; (void)t; (void)st;
    return -1;
}
static int u_text_width(void *c, uint32_t t, int32_t *w)
{
    (void)c; (void)t; (void)w;
    return -1;
}
static int u_health(void *c, uint32_t p, int32_t x, int32_t y)
{
    (void)c; (void)p; (void)x; (void)y;
    return -1;
}
static int u_format(void *c, int32_t v, int32_t a1, int32_t a2, const char **t)
{
    (void)c; (void)v; (void)a1; (void)a2; (void)t;
    return -1;
}
static int u_copy(void *c, char *d, size_t n, const char *s)
{
    (void)c; (void)d; (void)n; (void)s;
    return -1;
}
static int u_block_copy(void *c, uint8_t *b, uint32_t d, uint32_t s, int32_t n)
{
    (void)c; (void)b; (void)d; (void)s; (void)n;
    return -1;
}
static int u_actor(void *c, uint8_t *a)
{
    (void)c; (void)a;
    return -1;
}
static int u_model_bind(void *c, uint8_t *a, int32_t *r)
{
    (void)c; (void)a; (void)r;
    return -1;
}
static int u_method(void *c, uint8_t *a, uint32_t m)
{
    (void)c; (void)a; (void)m;
    return -1;
}

/* ---- 002149F0's workers --------------------------------------------------- */

static int p_background(void *c, uint64_t tex0)
{
    return leaf(em_battery_ui_background(((Ctx *)c)->call->draw, tex0));
}

static int p_frame(void *c, uint8_t *page, uint32_t table, int32_t flags)
{
    Ctx *ctx = c;
    if (page != ctx->call->records.page)
        return -1;
    return em_sul_0020AE40(&ctx->sul, &ctx->mem, EM_SPR_PAGE_ADDRESS, table, flags);
}

static int p_list(void *c, uint8_t *page, uint32_t table, uint64_t glyph, int32_t flags,
                  int32_t *result)
{
    Ctx *ctx = c;
    const EmSprRecords *r = &ctx->call->records;
    if (page != r->page)
        return -1;
    EmSulListGlobals g = {*r->d2821B4, *r->d2821B8, *r->d282240};
    int rc = em_sul_0020B210(&ctx->sul, &ctx->mem, page, r->page_size, table, glyph, flags, &g,
                             result);
    *r->d2821B4 = g.d2821B4;
    *r->d2821B8 = g.d2821B8;
    *r->d282240 = g.d282240;
    return rc;
}

static int p_arrows(void *c, uint8_t *page, uint32_t table)
{
    Ctx *ctx = c;
    if (page != ctx->call->records.page)
        return -1;
    return em_sul_0020B0D0(&ctx->sul, &ctx->mem, table);
}

/* 0020BBE0 / 0020BC50: unreachable on this page (see the header). */
static int p_list_refill(void *c, uint8_t *page, int32_t n)
{
    (void)c; (void)page; (void)n;
    return -1;
}

static int p_list_scroll(void *c, uint8_t *page, uint32_t table, uint64_t glyph, int32_t flags,
                         int32_t *result)
{
    (void)c; (void)page; (void)table; (void)glyph; (void)flags; (void)result;
    return -1;
}

static int p_marker(void *c, uint8_t *page)
{
    Ctx *ctx = c;
    if (page != ctx->call->records.page)
        return -1;
    return em_cs_0020CCB0(&ctx->rect, page, ctx->call->records.page_size);
}

static int p_cue_accept(void *c)
{
    return em_sul_0020CD40(&((Ctx *)c)->sul);
}

static int p_cue_back(void *c)
{
    return em_sul_0020CD60(&((Ctx *)c)->sul);
}

static int p_cue_cursor(void *c)
{
    return em_sul_0020CDA0(&((Ctx *)c)->sul);
}

static int p_sound(void *c, int32_t id, int32_t a1, int32_t a2, int32_t a3)
{
    return s_sound(c, id, a1, a2, a3);
}

/* 0020CD80 is em_spr_0020CD80 over this same worker set. */
static int p_cue_refuse(void *c);

static int m_present(void *c, int32_t a0, int32_t a1, int32_t a2, int32_t a3)
{
    const EmBatteryPageHost *h = ((Ctx *)c)->host;
    return h->present(h->context, a0, a1, a2, a3);
}

static int p_message_line(void *c)
{
    EmRvrFault fault = {0, 0};
    return em_rvr_001FCF10(&((Ctx *)c)->message, &fault);
}

static int p_find_device(void *c, int32_t item, uint32_t *owner)
{
    const EmBatteryPageHost *h = ((Ctx *)c)->host;
    return h->find_device(h->context, item, owner);
}

static int p_owner_read(void *c, uint32_t owner, uint32_t offset, uint32_t size, int32_t *value)
{
    const EmBatteryPageHost *h = ((Ctx *)c)->host;
    return h->owner_read(h->context, owner, offset, size, value);
}

static int p_owner_write(void *c, uint32_t owner, uint32_t offset, uint8_t value)
{
    const EmBatteryPageHost *h = ((Ctx *)c)->host;
    return h->owner_write(h->context, owner, offset, value);
}

static void page_workers(Ctx *ctx, EmSprWorkers *w)
{
    memset(w, 0, sizeof *w);
    w->context = ctx;
    w->background = p_background;
    w->frame = p_frame;
    w->list = p_list;
    w->arrows = p_arrows;
    w->list_refill = p_list_refill;
    w->list_scroll = p_list_scroll;
    w->marker = p_marker;
    w->cue_accept = p_cue_accept;
    w->cue_back = p_cue_back;
    w->cue_refuse = p_cue_refuse;
    w->cue_cursor = p_cue_cursor;
    w->message_line = p_message_line;
    w->blend = l_blend;
    w->sound = p_sound;
    w->find_device = p_find_device;
    w->owner_read = p_owner_read;
    w->owner_write = p_owner_write;
}

static int p_cue_refuse(void *c)
{
    EmSprWorkers w;
    page_workers((Ctx *)c, &w);
    return em_spr_0020CD80(&w);
}

static void put16(uint8_t out[2], uint16_t v)
{
    out[0] = (uint8_t)v;
    out[1] = (uint8_t)(v >> 8);
}

int em_battery_page_live_tick(const EmBatteryPageHost *host, const EmBatteryPageCall *call,
                              EmSprFault *fault)
{
    if (fault)
        *fault = (EmSprFault){0, 0};
    if (!host || !call || !call->draw || !call->gauge || !host->sound || !host->find_device ||
        !host->owner_read || !host->owner_write || !host->present) {
        if (fault)
            *fault = (EmSprFault){0x002149F0u, EM_SPR_FAULT_NULL};
        return -1;
    }
    Ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.host = host;
    ctx.call = call;
    if (!em_battery_ui_tables(call->draw, ctx.frame_table, ctx.row_table)) {
        if (fault)
            *fault = (EmSprFault){0x002149F0u, EM_SPR_FAULT_NULL};
        return -1;
    }
    put16(ctx.d810E70, call->held);
    put16(ctx.d810E78, call->repeat);
    ctx.regions[0] = (EmSulRegion){EM_SPR_FRAME_TABLE, EM_BATTERY_UI_FRAME_TABLE_SIZE,
                                   ctx.frame_table};
    ctx.regions[1] = (EmSulRegion){EM_SPR_ROW_TABLE, EM_BATTERY_UI_ROW_TABLE_SIZE, ctx.row_table};
    ctx.regions[2] = (EmSulRegion){0x00810E70u, 2, ctx.d810E70};
    ctx.regions[3] = (EmSulRegion){0x00810E78u, 2, ctx.d810E78};
    ctx.mem = (EmSulMemory){ctx.regions, 4};
    ctx.sul = (EmSulWorkers){&ctx,          l_blend,        l_sprite,      u_text_fixed,
                             u_text_proportional, u_text_width, u_health,    s_battery,
                             l_float_to_int, u_format,      u_copy,        s_sound,
                             u_block_copy,  u_actor,        u_model_bind,  u_actor,
                             u_actor,       u_method};
    ctx.rect = (EmCsRectWorkers){&ctx, l_float_to_int, l_rectangle};
    ctx.message = (EmRvrMessageWorkers){&ctx, m_present};
    EmSprWorkers w;
    page_workers(&ctx, &w);
    em_battery_ui_begin_frame(call->draw);
    return em_spr_002149F0(&w, &call->records, fault);
}
