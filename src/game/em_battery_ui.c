/* em_battery_ui.c - see em_battery_ui.h. */
#include "game/em_battery_ui.h"
#include "game/em_hud.h"
#include "game/em_status_background.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SPRITES = 28, WHITE = 27, CALLS = 256, ARENA = 1024 };
enum { CALL_BACKGROUND = 1, CALL_BLEND, CALL_SPRITE, CALL_RECTANGLE, CALL_TEXT };

typedef struct {
    uint32_t u, v, w, h;
    uint64_t tex0;
} Sprite;

typedef struct {
    uint8_t kind, mode, proportional;
    int32_t xy[4];
    uint32_t rgba, text;
    uint64_t tex0;
} Call;

struct EmBatteryUI {
    uint8_t *data, *pixels;
    uint32_t width, height;
    Sprite sprites[SPRITES];
    Call calls[CALLS];
    unsigned count, mode, arena_used;
    char arena[ARENA];
    int refused, uploaded;
};

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t u64(const uint8_t *p)
{
    return (uint64_t)u32(p) | (uint64_t)u32(p + 4) << 32;
}

/* EMBA version 2 (tools/export_panel.py): a 32-byte header, 28 records
 * {id, u, v, w, h, 0, TEX0}, the text records (not used: the page's lines
 * are the message service's), the 96-byte background state and the RGBA8
 * sheet. */
EmBatteryUI *em_battery_ui_load(const char *path)
{
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f)
        return NULL;
    uint8_t header[32];
    if (fread(header, 1, 32, f) != 32 || memcmp(header, "EMBA", 4) || u32(header + 4) != 2 ||
        u32(header + 16) != SPRITES || u32(header + 20) != 11) {
        fclose(f);
        return NULL;
    }
    uint32_t w = u32(header + 8), h = u32(header + 12), texts = u32(header + 24),
             size = u32(header + 28);
    if (!w || !h || w > 4096 || h > 4096 || texts > 16384 ||
        (uint64_t)w * h * 4 + SPRITES * 32 + texts + 96 != size) {
        fclose(f);
        return NULL;
    }
    EmBatteryUI *ui = calloc(1, sizeof *ui);
    if (!ui) {
        fclose(f);
        return NULL;
    }
    ui->data = malloc(size);
    if (!ui->data || fread(ui->data, 1, size, f) != size || fgetc(f) != EOF) {
        fclose(f);
        em_battery_ui_free(ui);
        return NULL;
    }
    fclose(f);
    ui->width = w;
    ui->height = h;
    const uint8_t *p = ui->data;
    for (unsigned i = 0; i < SPRITES; i++, p += 32) {
        Sprite *s = &ui->sprites[i];
        s->u = u32(p + 4);
        s->v = u32(p + 8);
        s->w = u32(p + 12);
        s->h = u32(p + 16);
        s->tex0 = u64(p + 24);
        if (u32(p) != i || !s->w || !s->h || s->w > w || s->h > h || s->u > w - s->w ||
            s->v > h - s->h || (i == WHITE) != (s->tex0 == 0)) {
            em_battery_ui_free(ui);
            return NULL;
        }
    }
    ui->pixels = ui->data + SPRITES * 32 + texts + 96;
    return ui;
}

void em_battery_ui_free(EmBatteryUI *ui)
{
    if (!ui)
        return;
    em_battery_ui_deactivate(ui);
    free(ui->data);
    free(ui);
}

int em_battery_ui_tables(const EmBatteryUI *ui, uint8_t frame[EM_BATTERY_UI_FRAME_TABLE_SIZE],
                         uint8_t rows[EM_BATTERY_UI_ROW_TABLE_SIZE])
{
    if (!ui || !frame || !rows)
        return 0;
    for (unsigned i = 0; i < 25; ++i) {
        uint8_t *out = i < 16 ? frame + 8 * i : rows + 8 * (i - 16);
        for (unsigned b = 0; b < 8; ++b)
            out[b] = (uint8_t)(ui->sprites[i].tex0 >> (8 * b));
    }
    return 1;
}

void em_battery_ui_begin_frame(EmBatteryUI *ui)
{
    if (!ui)
        return;
    ui->count = ui->arena_used = 0;
    ui->mode = 0;
    ui->refused = 0;
}

static Call *append(EmBatteryUI *ui, unsigned kind)
{
    if (!ui || ui->refused)
        return NULL;
    if (ui->count >= CALLS) {
        ui->refused = 1;
        return NULL;
    }
    Call *c = &ui->calls[ui->count++];
    memset(c, 0, sizeof *c);
    c->kind = (uint8_t)kind;
    c->mode = (uint8_t)ui->mode;
    return c;
}

static int refuse(EmBatteryUI *ui)
{
    if (ui)
        ui->refused = 1;
    return 0;
}

int em_battery_ui_background(EmBatteryUI *ui, uint64_t tex0)
{
    Call *c = append(ui, CALL_BACKGROUND);
    if (!c)
        return 0;
    c->tex0 = tex0;
    return 1;
}

int em_battery_ui_blend(EmBatteryUI *ui, int32_t slot, int32_t mode)
{
    if (slot != 1 || mode < 0 || mode > 3)
        return refuse(ui);
    Call *c = append(ui, CALL_BLEND);
    if (!c)
        return 0;
    ui->mode = (unsigned)mode;
    c->mode = (uint8_t)mode;
    return 1;
}

int em_battery_ui_sprite(EmBatteryUI *ui, int32_t slot, int32_t x, int32_t y, int32_t w, int32_t h,
                         uint32_t rgba, uint64_t tex0)
{
    if (slot != 1 || w <= 0 || h <= 0 || w > 1024 || h > 1024)
        return refuse(ui);
    Call *c = append(ui, CALL_SPRITE);
    if (!c)
        return 0;
    c->xy[0] = x;
    c->xy[1] = y;
    c->xy[2] = w;
    c->xy[3] = h;
    c->rgba = rgba;
    c->tex0 = tex0;
    return 1;
}

int em_battery_ui_rectangle(EmBatteryUI *ui, int32_t slot, int32_t x0, int32_t y0, int32_t x1,
                            int32_t y1, uint32_t rgba)
{
    if (slot != 1)
        return refuse(ui);
    Call *c = append(ui, CALL_RECTANGLE);
    if (!c)
        return 0;
    c->xy[0] = x0;
    c->xy[1] = y0;
    c->xy[2] = x1;
    c->xy[3] = y1;
    c->rgba = rgba;
    return 1;
}

/* The glyph cells the original 001CBA50 / 001CC1E0 calls pass (the status
 * hub's mapping, em_status_hub_ui.c). */
static int text_style(int proportional, int32_t w, int32_t h, EmHudTextStyle *style)
{
    if (proportional) {
        *style = EM_HUD_TEXT_TALL;
        return w == 10 && h == 20;
    }
    if (w == 12 && h == 12)
        *style = EM_HUD_TEXT_NUM12;
    else if (w == 16 && h == 16)
        *style = EM_HUD_TEXT_NUM16;
    else
        return 0;
    return 1;
}

int em_battery_ui_text(EmBatteryUI *ui, int proportional, int32_t x, int32_t y, int32_t w,
                       int32_t h, const char *text, uint64_t style)
{
    EmHudTextStyle unused;
    size_t length = text ? strlen(text) + 1 : 0;
    if (!text || !text_style(proportional, w, h, &unused) || !ui ||
        length > ARENA - ui->arena_used)
        return refuse(ui);
    Call *c = append(ui, CALL_TEXT);
    if (!c)
        return 0;
    c->proportional = (uint8_t)(proportional != 0);
    c->xy[0] = x;
    c->xy[1] = y;
    c->xy[2] = w;
    c->xy[3] = h;
    c->tex0 = style;
    c->text = ui->arena_used;
    memcpy(ui->arena + ui->arena_used, text, length);
    ui->arena_used += (unsigned)length;
    return 1;
}

unsigned em_battery_ui_count(const EmBatteryUI *ui)
{
    return ui ? ui->count : 0;
}

static const Sprite *find_sprite(const EmBatteryUI *ui, uint64_t tex0)
{
    for (unsigned i = 0; i < SPRITES; ++i)
        if (i != WHITE && ui->sprites[i].tex0 == tex0)
            return &ui->sprites[i];
    return NULL;
}

/* GS modulate uses 128 as unity; untextured RGB is the raw 0..255 value. */
static void source_color(uint32_t rgba, int textured, float out[4])
{
    for (unsigned channel = 0; channel < 3; ++channel)
        out[channel] = (float)((rgba >> (8 * channel)) & 255) / (textured ? 128.0f : 255.0f);
    out[3] = (float)(rgba >> 24) / 128.0f;
}

static float canvas_x(float gs_x)
{
    return gs_x - 1792;
}

static float canvas_y(float gs_y)
{
    return (gs_y - 1936) * 2;
}

static int render_call(EmBatteryUI *ui, EmGfx *gfx, const Call *c)
{
    const Sprite *white = &ui->sprites[WHITE];
    float u = white->u + 0.5f, v = white->v + 0.5f, color[4];
    switch (c->kind) {
    case CALL_BACKGROUND: {
        /* 0020A7A0: the status frame's black, then the tile. */
        const Sprite *tile = find_sprite(ui, c->tex0);
        if (!tile)
            return 0;
        em_status_background_frame(gfx);
        return em_status_background_render(gfx, (float)tile->u, (float)tile->v, (float)tile->w,
                                           (float)tile->h);
    }
    case CALL_BLEND:
        return 1; /* each call carries the mode in effect */
    case CALL_SPRITE: {
        const Sprite *s = find_sprite(ui, c->tex0);
        if (!s)
            return 0;
        source_color(c->rgba, 1, color);
        em_gfx_overlay_sprite_blend(gfx, canvas_x(c->xy[0] / 16.0f), canvas_y(c->xy[1] / 16.0f),
                                    (float)c->xy[2], (float)c->xy[3], (float)s->u, (float)s->v,
                                    (float)(s->u + s->w), (float)(s->v + s->h), color,
                                    (EmGfxOverlayBlend)c->mode);
        return 1;
    }
    case CALL_RECTANGLE:
        source_color(c->rgba, 0, color);
        em_gfx_overlay_sprite_blend(gfx, canvas_x(c->xy[0] / 16.0f), canvas_y(c->xy[1] / 16.0f),
                                    (c->xy[2] - c->xy[0]) / 16.0f, (c->xy[3] - c->xy[1]) / 8.0f, u,
                                    v, u, v, color, (EmGfxOverlayBlend)c->mode);
        return 1;
    case CALL_TEXT: {
        EmHudTextStyle style;
        if (!text_style(c->proportional, c->xy[2], c->xy[3], &style))
            return 0;
        em_hud_text_color(gfx, canvas_x((float)c->xy[0]), canvas_y((float)c->xy[1]),
                          ui->arena + c->text, style, (uint32_t)c->tex0 & 0xFFFFFFu);
        return 1;
    }
    }
    return 0;
}

int em_battery_ui_render(EmBatteryUI *ui, EmGfx *gfx)
{
    if (!ui || !gfx || ui->refused)
        return 0;
    if (!ui->uploaded) {
        if (!em_gfx_overlay_texture_set(gfx, EM_GFX_OVERLAY_TEX_UI, ui->pixels, ui->width,
                                        ui->height))
            return 0;
        em_hud_decor_invalidate();
        ui->uploaded = 1;
    }
    em_gfx_overlay_canvas(gfx, EM_GFX_STATUS_W, EM_GFX_STATUS_H);
    int ok = 1;
    for (unsigned i = 0; ok && i < ui->count; ++i)
        ok = render_call(ui, gfx, &ui->calls[i]);
    em_gfx_overlay_canvas(gfx, EM_GFX_OVERLAY_W, EM_GFX_OVERLAY_H);
    return ok;
}

void em_battery_ui_deactivate(EmBatteryUI *ui)
{
    if (ui && ui->uploaded) {
        em_hud_decor_invalidate();
        ui->uploaded = 0;
    }
}
