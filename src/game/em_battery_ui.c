#include "game/em_battery_ui.h"
#include "game/em_effect_color.h"
#include "game/em_hud.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t u, v, w, h;
} Sprite;
typedef struct {
    char *string;
    uint8_t *spans;
    uint32_t count;
} Text;
struct EmBatteryUI {
    uint8_t *data, *pixels;
    uint32_t width, height;
    Sprite sprites[28];
    Text text[8];
    EmPanel *owner;
    EmPanelBatteryMenu menu;
    EmPanelMenuPhase draw_phase;
    int active, uploaded, kind, draw_charge, error_timer, draw_error;
};

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

EmBatteryUI *em_battery_ui_load(const char *path)
{
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f)
        return NULL;
    uint8_t header[32];
    if (fread(header, 1, 32, f) != 32 || memcmp(header, "EMBA", 4) || u32(header + 4) != 1 ||
        u32(header + 16) != 28 || u32(header + 20) != 8) {
        fclose(f);
        return NULL;
    }
    uint32_t w = u32(header + 8), h = u32(header + 12), texts = u32(header + 24),
             size = u32(header + 28);
    if (!w || !h || w > 4096 || h > 4096 || texts > 16384 ||
        (uint64_t)w * h * 4 + 28 * 32 + texts + 96 != size) {
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
    uint8_t *p = ui->data;
    for (unsigned i = 0; i < 28; i++, p += 32) {
        Sprite *s = &ui->sprites[i];
        s->u = u32(p + 4);
        s->v = u32(p + 8);
        s->w = u32(p + 12);
        s->h = u32(p + 16);
        if (u32(p) != i || !s->w || !s->h || s->w > w || s->h > h || s->u > w - s->w ||
            s->v > h - s->h)
            goto invalid;
    }
    uint8_t *end = p + texts;
    for (unsigned i = 0; i < 8; i++) {
        if (end - p < 8)
            goto invalid;
        uint32_t length = u32(p), count = u32(p + 4);
        p += 8;
        if (!length || length > 1024 || count > 32 ||
            (uint64_t)length + count * 8 > (uint64_t)(end - p))
            goto invalid;
        ui->text[i].spans = p;
        ui->text[i].count = count;
        p += count * 8;
        ui->text[i].string = (char *)p;
        if (p[length - 1] || memchr(p, 0, length - 1))
            goto invalid;
        for (uint32_t j = 0; j < count; j++) {
            uint32_t at = u32(ui->text[i].spans + j * 8);
            if (at >= length || (j && at < u32(ui->text[i].spans + (j - 1) * 8)))
                goto invalid;
        }
        p += length;
    }
    if (p != end)
        goto invalid;
    ui->pixels = end + 96;
    return ui;
invalid:
    em_battery_ui_free(ui);
    return NULL;
}

void em_battery_ui_free(EmBatteryUI *ui)
{
    if (!ui)
        return;
    em_battery_ui_close(ui);
    free(ui->data);
    free(ui);
}

int em_battery_ui_begin(EmBatteryUI *ui, EmPanel *owner, int charge, int kind)
{
    if (!ui || !owner || kind < 0 || kind > 2 || charge < 0 || charge > 255)
        return 0;
    ui->owner = owner;
    ui->kind = kind;
    ui->active = 1;
    ui->uploaded = 0;
    ui->error_timer = ui->draw_error = 0;
    em_panel_battery_begin(&ui->menu, charge);
    ui->draw_charge = charge;
    ui->draw_phase = ui->menu.phase;
    return 1;
}

unsigned em_battery_ui_tick(EmBatteryUI *ui, unsigned buttons, int *charge, int owner_available)
{
    if (!ui || !ui->active || !charge)
        return 0;
    ui->draw_phase = ui->menu.phase;
    ui->draw_charge = *charge;
    ui->draw_error = ui->error_timer != 0;
    if (ui->error_timer) {
        if (buttons & 0x60) {
            ui->error_timer = 0;
            return EM_PANEL_MENU_CANCEL;
        }
        --ui->error_timer;
        return 0;
    }
    if (ui->menu.phase == EM_PANEL_MENU_BROWSE) {
        if (buttons & 0x20)
            return EM_BATTERY_BACK_TO_STATUS | EM_PANEL_MENU_CANCEL;
        if (buttons & 0x40) {
            /*00185420 may no longer find an eligible associated device. */
            if (!owner_available) {
                ui->error_timer = 240;
                return EM_BATTERY_NO_DEVICE_SOUND;
            }
            em_panel_battery_begin(&ui->menu, *charge);
            return EM_PANEL_MENU_ACCEPT;
        }
        return 0;
    }
    return em_panel_battery_step(ui->owner, &ui->menu, buttons, charge);
}

int em_battery_ui_begin_browse(EmBatteryUI *ui, EmPanel *owner, int charge, int kind)
{
    if (!em_battery_ui_begin(ui, owner, charge, kind))
        return 0;
    ui->menu.phase = ui->draw_phase = EM_PANEL_MENU_BROWSE;
    return 1;
}

unsigned em_battery_ui_original_step(const EmBatteryUI *ui)
{
    if (!ui || !ui->active)
        return 0;
    if (ui->error_timer)
        return 8;
    switch (ui->menu.phase) {
    case EM_PANEL_MENU_BROWSE:
        return 1;
    case EM_PANEL_MENU_CONFIRM:
        return 4;
    case EM_PANEL_MENU_INSUFFICIENT:
        return 5;
    case EM_PANEL_MENU_DISCHARGE:
    case EM_PANEL_MENU_COMPLETE:
        return 6;
    }
    return 0;
}

static void sprite(EmBatteryUI *ui, EmGfx *gfx, int id, float x, float y, float w, float h,
                   uint32_t rgba)
{
    Sprite *s = &ui->sprites[id];
    const float color[4] = {(rgba & 255) / 128.0f, ((rgba >> 8) & 255) / 128.0f,
                            ((rgba >> 16) & 255) / 128.0f, ((rgba >> 24) & 255) / 128.0f};
    em_gfx_overlay_sprite(gfx, x, y, w, h, s->u, s->v, s->u + s->w, s->v + s->h, color);
}

static void gs_sprite(EmBatteryUI *ui, EmGfx *gfx, int id, int x, int y, int w, int h,
                      uint32_t rgba)
{
    sprite(ui, gfx, id, x / 16.0f - 1792, (y / 16.0f - 1936) * 2, w, h, rgba);
}

static void text(EmBatteryUI *ui, EmGfx *gfx, int index, float x, float y)
{
    Text *t = &ui->text[index];
    uint32_t span = 0, color = 0x606060;
    float pen = x;
    for (unsigned i = 0; t->string[i]; i++) {
        while (span < t->count && u32(t->spans + span * 8) == i) {
            color = u32(t->spans + span * 8 + 4);
            span++;
        }
        char c = t->string[i];
        if (c == '\n') {
            pen = x;
            y += 24;
            continue;
        }
        char glyph[2] = {c, 0};
        em_hud_text_color(gfx, pen, y, glyph, EM_HUD_TEXT_TALL, color);
        pen += em_hud_text_width(glyph, EM_HUD_TEXT_TALL);
    }
}

static void battery(EmBatteryUI *ui, EmGfx *gfx, int capacity)
{
    char caption[16];
    snprintf(caption, sizeof caption, "%02d/%02d", ui->draw_charge >> 1, capacity >> 1);
    em_hud_text(gfx, 220, 260, caption, EM_HUD_TEXT_NUM16);
    for (int row = 0; row < 4; row++) {
        float color[4] = {163, 54, 160, 128};
        const float goal[4] = {255, 230, 52, 128};
        /* Literal IEEE words43230000/42580000/43200000 and
         * 437F0000/43660000/42500000 in00209280, not stale comments. */
        float delta[4];
        for (int c = 0; c < 4; c++)
            delta[c] = em_effect_float32((double)(goal[c] - color[c]) / 12.0);
        for (int column = 0; column < 12 && row * 12 + column < ui->draw_charge; column++) {
            uint32_t rgba = 0;
            for (int c = 0; c < 4; c++)
                rgba |= (uint32_t)(unsigned)color[c] << (c * 8);
            sprite(ui, gfx, 14, 320 - column * 12 - (!(column & 1)), 202 + row * 12, 12, 12, rgba);
            for (int c = 0; c < 4; c++)
                color[c] = em_effect_float32((double)color[c] + delta[c]);
        }
    }
}

int em_battery_ui_render(EmBatteryUI *ui, EmGfx *gfx, int capacity, unsigned held)
{
    if (!ui || !ui->active || !gfx)
        return 0;
    if (!ui->uploaded) {
        if (!em_gfx_overlay_texture_set(gfx, EM_GFX_OVERLAY_TEX_UI, ui->pixels, ui->width,
                                        ui->height))
            return 0;
        em_hud_decor_invalidate();
        ui->uploaded = 1;
    }
    em_gfx_overlay_canvas(gfx, 512, 448);
    Sprite *bg = &ui->sprites[26];
    em_hud_background_sprite(gfx, bg->u, bg->v, bg->w, bg->h);
    /* Original0020AE40 flags2 in call order. */
    gs_sprite(ui, gfx, 0, 0x7000, 0x7B40, 256, 128, 0x40808080);
    gs_sprite(ui, gfx, 2, 0x7000, 0x7F40, 256, 128, 0x40808080);
    gs_sprite(ui, gfx, 1, 0x8000, 0x7B40, 256, 128, 0x40808080);
    gs_sprite(ui, gfx, 3, 0x8000, 0x7F40, 256, 128, 0x40808080);
    gs_sprite(ui, gfx, 4, 0x7000, 0x8300, 256, 128, 0x40808080);
    gs_sprite(ui, gfx, 5, 0x8000, 0x8300, 256, 128, 0x40808080);
    gs_sprite(ui, gfx, 15, 0x7800, 0x7E00, 256, 128, 0x80808080);
    battery(ui, gfx, capacity);
    gs_sprite(ui, gfx, 7, 0x7000, 0x8300, 128, 128, 0x80808080);
    gs_sprite(ui, gfx, 6, 0x8780, 0x8300, 128, 128, 0x80808080);
    gs_sprite(ui, gfx, 13, 0x7100, 0x7900, 256, 64, 0x80808080);
    /*0020B210 flags402, the highest available pack is the only row. */
    gs_sprite(ui, gfx, 16 + ui->kind * 3, 0x77F0, 0x7C50, 128, 64, 0x80808080);
    gs_sprite(ui, gfx, 17 + ui->kind * 3, 0x7FF0, 0x7C50, 128, 64, 0x80808080);
    gs_sprite(ui, gfx, 25, 0x77F0, 0x7C50, 256, 64, 0x20808080);
    gs_sprite(ui, gfx, 18 + ui->kind * 3, 0x89F0, 0x83E0, 64, 64, 0x40808080);
    gs_sprite(ui, gfx, (held & 0x1000) ? 11 : 9, 0x7800, 0x7B30, 32, 32, 0x80808080);
    gs_sprite(ui, gfx, (held & 0x4000) ? 12 : 10, 0x7800, 0x8240, 32, 32, 0x80808080);
    if (ui->draw_error)
        text(ui, gfx, 7, 138, 336);
    else if (ui->draw_phase == EM_PANEL_MENU_CONFIRM) {
        text(ui, gfx, 1, 138, 336);
        text(ui, gfx, 0, 270, 408);
        sprite(ui, gfx, 27, ui->menu.no_selected ? 335 : 253, 412, 12, 12, 0x80CE6000);
    } else if (ui->draw_phase == EM_PANEL_MENU_INSUFFICIENT)
        text(ui, gfx, 2, 138, 336);
    else if (ui->draw_phase == EM_PANEL_MENU_BROWSE)
        text(ui, gfx, 3 + ui->kind, 138, 336);
    em_gfx_overlay_canvas(gfx, EM_GFX_OVERLAY_W, EM_GFX_OVERLAY_H);
    return 1;
}

void em_battery_ui_close(EmBatteryUI *ui)
{
    if (!ui)
        return;
    if (ui->uploaded)
        em_hud_decor_invalidate();
    ui->active = ui->uploaded = 0;
    ui->owner = NULL;
}

EmPanelMenuPhase em_battery_ui_phase(const EmBatteryUI *ui)
{
    return ui ? ui->menu.phase : EM_PANEL_MENU_BROWSE;
}
const char *em_battery_ui_terminal_text(const EmBatteryUI *ui)
{
    return ui ? ui->text[6].string : NULL;
}
