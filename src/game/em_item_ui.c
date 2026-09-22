#include "game/em_item_ui.h"
#include "game/em_hud.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t u, v, w, h;
} Sprite;

typedef struct {
    const char *string;
    const uint8_t *spans;
    uint32_t span_count;
} Text;

struct EmItemUI {
    uint8_t *data;
    const uint8_t *pixels, *commands[6];
    uint32_t width, height, counts[6], white_index;
    Sprite sprites[18];
    Text texts[5];
    int uploaded;
};

static uint32_t word(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

EmItemUI *em_item_ui_load(const char *path)
{
    FILE *file = path ? fopen(path, "rb") : NULL;
    if (!file)
        return NULL;
    uint8_t header[48];
    if (fread(header, 1, sizeof header, file) != sizeof header || memcmp(header, "EMIR", 4) ||
        word(header + 4) != 2 || word(header + 16) != 18 || word(header + 24) != 6 ||
        word(header + 32) != 5 || word(header + 40) != 17 || word(header + 44)) {
        fclose(file);
        return NULL;
    }
    uint32_t width = word(header + 8), height = word(header + 12);
    uint32_t count = word(header + 20), size = word(header + 28), texts = word(header + 36);
    uint64_t expected =
        18 * 24 + 6 * 4 + (uint64_t)count * 32 + texts + (uint64_t)width * height * 4;
    if (!width || !height || width > 4096 || height > 4096 || count > 128 || texts > 16384 ||
        expected != size) {
        fclose(file);
        return NULL;
    }
    EmItemUI *ui = calloc(1, sizeof *ui);
    if (!ui) {
        fclose(file);
        return NULL;
    }
    ui->data = malloc(size);
    if (!ui->data || fread(ui->data, 1, size, file) != size || fgetc(file) != EOF) {
        fclose(file);
        em_item_ui_free(ui);
        return NULL;
    }
    fclose(file);
    ui->width = width;
    ui->height = height;
    ui->white_index = 17;
    const uint8_t *p = ui->data;
    for (unsigned i = 0; i < 18; ++i, p += 24) {
        Sprite *sprite = &ui->sprites[i];
        sprite->u = word(p);
        sprite->v = word(p + 4);
        sprite->w = word(p + 8);
        sprite->h = word(p + 12);
        if (!sprite->w || !sprite->h || sprite->w > width || sprite->h > height ||
            sprite->u > width - sprite->w || sprite->v > height - sprite->h)
            goto invalid;
    }
    unsigned total = 0;
    for (unsigned i = 0; i < 6; ++i, p += 4) {
        ui->counts[i] = word(p);
        if (ui->counts[i] < 1 || ui->counts[i] > 24 || ui->counts[i] > count - total)
            goto invalid;
        total += ui->counts[i];
    }
    if (total != count)
        goto invalid;
    for (unsigned layout = 0; layout < 6; ++layout) {
        ui->commands[layout] = p;
        unsigned trails = 0, backgrounds = 0;
        for (unsigned i = 0; i < ui->counts[layout]; ++i, p += 32) {
            unsigned kind = word(p), mode = word(p + 4), sprite = word(p + 8);
            if (kind > 2 || mode > 3 || sprite >= 17)
                goto invalid;
            if (kind == 1) {
                if (i || mode)
                    goto invalid;
                ++backgrounds;
            } else if (kind == 2) {
                if (mode != 1)
                    goto invalid;
                ++trails;
            } else if (word(p + 12) > 65535 || word(p + 16) > 65535 || !word(p + 20) ||
                       !word(p + 24) || word(p + 20) > 1024 || word(p + 24) > 1024) {
                goto invalid;
            }
        }
        if (trails != 1 || backgrounds != 1)
            goto invalid;
    }
    const uint8_t *end = p + texts;
    for (unsigned i = 0; i < 5; ++i) {
        if (end - p < 8)
            goto invalid;
        uint32_t length = word(p), spans = word(p + 4);
        p += 8;
        if (!length || length > 1024 || spans > 32 ||
            (uint64_t)length + spans * 8 > (uint64_t)(end - p))
            goto invalid;
        Text *text = &ui->texts[i];
        text->span_count = spans;
        text->spans = p;
        p += spans * 8;
        text->string = (const char *)p;
        if (p[length - 1] || memchr(p, 0, length - 1))
            goto invalid;
        for (unsigned span = 0; span < spans; ++span) {
            uint32_t at = word(text->spans + span * 8);
            if (at >= length || (span && at < word(text->spans + (span - 1) * 8)))
                goto invalid;
        }
        p += length;
    }
    if (p != end)
        goto invalid;
    ui->pixels = p;
    return ui;
invalid:
    em_item_ui_free(ui);
    return NULL;
}

void em_item_ui_deactivate(EmItemUI *ui)
{
    if (ui && ui->uploaded) {
        em_hud_decor_invalidate();
        ui->uploaded = 0;
    }
}

void em_item_ui_free(EmItemUI *ui)
{
    if (!ui)
        return;
    em_item_ui_deactivate(ui);
    free(ui->data);
    free(ui);
}

static void help_text(EmItemUI *ui, EmGfx *gfx, unsigned line)
{
    /*001FCA10 mode4 calls001FCB90(138,168,group,line); vertical
     * coordinates double on the native512x448 canvas. */
    Text *text = &ui->texts[line];
    unsigned span = 0;
    uint32_t color = 0x606060;
    float x = 138, y = 336;
    for (unsigned i = 0; text->string[i]; ++i) {
        while (span < text->span_count && word(text->spans + span * 8) == i)
            color = word(text->spans + span++ * 8 + 4);
        if (text->string[i] == '\n') {
            x = 138;
            y += 24;
            continue;
        }
        char glyph[2] = {text->string[i], 0};
        em_hud_text_color(gfx, x, y, glyph, EM_HUD_TEXT_TALL, color);
        x += em_hud_text_width(glyph, EM_HUD_TEXT_TALL);
    }
}

int em_item_ui_render(EmItemUI *ui, EmGfx *gfx, unsigned hover, int line, EmItemTrailRenderer trail,
                      void *context)
{
    if (!ui || !gfx || !trail || hover > 5 || line < -1 || line > 4)
        return 0;
    if (!ui->uploaded) {
        if (!em_gfx_overlay_texture_set(gfx, EM_GFX_OVERLAY_TEX_UI, ui->pixels, ui->width,
                                        ui->height))
            return 0;
        em_hud_decor_invalidate();
        ui->uploaded = 1;
    }
    em_gfx_overlay_canvas(gfx, 512, 448);
    const uint8_t *p = ui->commands[hover];
    for (unsigned i = 0; i < ui->counts[hover]; ++i, p += 32) {
        unsigned kind = word(p);
        Sprite *sprite = &ui->sprites[word(p + 8)];
        if (kind == 1) {
            em_hud_background_sprite(gfx, sprite->u, sprite->v, sprite->w, sprite->h);
        } else if (kind == 2) {
            Sprite *white = &ui->sprites[ui->white_index];
            if (!trail(context, gfx, word(p + 12), word(p + 16), white->u + 0.5f,
                       white->v + 0.5f)) {
                em_gfx_overlay_canvas(gfx, EM_GFX_OVERLAY_W, EM_GFX_OVERLAY_H);
                return 0;
            }
        } else {
            uint32_t packed = word(p + 28);
            const float color[4] = {(packed & 255) / 128.0f, ((packed >> 8) & 255) / 128.0f,
                                    ((packed >> 16) & 255) / 128.0f,
                                    ((packed >> 24) & 255) / 128.0f};
            em_gfx_overlay_sprite_blend(
                gfx, word(p + 12) / 16.0f - 1792, (word(p + 16) / 16.0f - 1936) * 2, word(p + 20),
                word(p + 24), sprite->u, sprite->v, sprite->u + sprite->w, sprite->v + sprite->h,
                color, (EmGfxOverlayBlend)word(p + 4));
        }
    }
    if (line >= 0)
        help_text(ui, gfx, (unsigned)line);
    em_gfx_overlay_canvas(gfx, EM_GFX_OVERLAY_W, EM_GFX_OVERLAY_H);
    return 1;
}
