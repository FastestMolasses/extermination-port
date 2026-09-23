/* Bounded original MAIN 2D fixture; status models and moving background
 * phase are explicit visual boundaries. Generated commands stay ignored. */

#include "em_gfx.h"
#include "game/em_hud.h"
#include "game/em_status_background.h"
#include "game/em_item_geometry.h"
#include "game/em_item_sdk_math.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t token;
    uint32_t x, y, w, h;
} Sprite;
static Sprite sprites[64];
static unsigned sprite_count, white_index;
static EmGfx *gfx;
static EmItemSdkMath sdk;
static EmItemMath math_workers;
static EmItemTrail trail_state;
static float white_u, white_v;
static uint32_t word(const uint8_t *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t wide(const uint8_t *p)
{
    return word(p) | ((uint64_t)word(p + 4) << 32);
}
static void load_atlas(void)
{
    FILE *f = fopen("assets/scene_snow/panel/status_hub_atlas.emha", "rb");
    assert(f);
    uint8_t h[28];
    assert(fread(h, 1, sizeof h, f) == sizeof h);
    assert(!memcmp(h, "EMHA", 4) && word(h + 4) == 1);
    uint32_t w = word(h + 8), height = word(h + 12), size = word(h + 24);
    sprite_count = word(h + 16);
    white_index = word(h + 20);
    assert(sprite_count <= 64 && white_index < sprite_count && size == (uint64_t)w * height * 4);
    for (unsigned i = 0; i < sprite_count; i++) {
        uint8_t b[24];
        assert(fread(b, 1, 24, f) == 24);
        sprites[i] = (Sprite){wide(b), word(b + 8), word(b + 12), word(b + 16), word(b + 20)};
    }
    uint8_t *pixels = malloc(size);
    assert(pixels && fread(pixels, 1, size, f) == size);
    fclose(f);
    assert(em_gfx_overlay_texture_set(gfx, EM_GFX_OVERLAY_TEX_UI, pixels, w, height));
    free(pixels);
    white_u = sprites[white_index].x + .5f;
    white_v = sprites[white_index].y + .5f;
    EmInteractionMath coefficients;
    f = fopen("assets/scene_snow/interaction.emis", "rb");
    assert(f && fseek(f, 20, SEEK_SET) == 0);
    assert(fread(&coefficients, 1, sizeof coefficients, f) == sizeof coefficients);
    fclose(f);
    assert(em_item_sdk_math_bind(&sdk, &coefficients, &math_workers));
}
static const Sprite *find_sprite(uint64_t token)
{
    for (unsigned i = 0; i < sprite_count; i++)
        if (sprites[i].token == token)
            return &sprites[i];
    assert(!"Missing original TEX0");
    return NULL;
}
static void source_color(uint32_t rgba, float out[4], int textured)
{
    for (unsigned i = 0; i < 3; i++)
        out[i] = ((rgba >> (8 * i)) & 255) / (textured ? 128.0f : 255.0f);
    out[3] = (rgba >> 24) / 128.0f;
}
static void sprite(unsigned mode, float x, float y, float w, float h, uint32_t rgba, uint64_t token)
{
    const Sprite *s = find_sprite(token);
    float color[4];
    source_color(rgba, color, 1);
    em_gfx_overlay_sprite_blend(gfx, x / 16 - 1792, (y / 16 - 1936) * 2, w, h, s->x, s->y,
                                s->x + s->w, s->y + s->h, color, mode);
}
static void rectangle(unsigned mode, float x, float y, float x1, float y1, uint32_t rgba)
{
    float color[4];
    source_color(rgba, color, 0);
    em_gfx_overlay_sprite_blend(gfx, x / 16 - 1792, (y / 16 - 1936) * 2, (x1 - x) / 16,
                                (y1 - y) / 8, white_u, white_v, white_u, white_v, color, mode);
}
static void triangle(unsigned mode, const EmItemVertex *a, const EmItemVertex *b,
                     const EmItemVertex *c)
{
    const EmItemVertex *v[3] = {a, b, c};
    float xy[3][2], color[3][4];
    for (unsigned i = 0; i < 3; i++) {
        xy[i][0] = v[i]->x / 16.0f - 1792;
        xy[i][1] = (v[i]->y / 16.0f - 1936) * 2;
        source_color(v[i]->rgba, color[i], 0);
    }
    assert(em_gfx_overlay_triangle(gfx, xy, color, white_u, white_v, mode));
}
static void arc(unsigned mode, const float descriptor[24])
{
    EmItemVertex v[256];
    size_t count;
    assert(em_item_geometry_arc(descriptor, v, 256, &count));
    for (size_t i = 2; i < count; i++)
        triangle(mode, &v[i - 2], &v[i - 1], &v[i]);
}
/* Original marker endpoints retain their fixed-point rounding. This
 * fixture expands each segment to a one-GS-pixel parallelogram; exact
 * endpoint coverage remains a GS/Metal rasterization boundary. */
static void line(unsigned mode, const uint32_t a[6], const uint32_t b[6])
{
    float xy[4][2], color[4][4];
    float x = a[4] / 16.0f - 1792, y = (a[5] / 16.0f - 1936) * 2;
    float x1 = b[4] / 16.0f - 1792, y1 = (b[5] / 16.0f - 1936) * 2;
    float dx = x1 - x, dy = (y1 - y) * .5f, nx, ny;
    if (dx == 0 && dy == 0)
        return;
    if (fabsf(dx) < fabsf(dy)) {
        nx = 1;
        ny = -dx / dy;
    } else {
        nx = -dy / dx;
        ny = 1;
    }
    xy[0][0] = x;
    xy[0][1] = y;
    xy[1][0] = x1;
    xy[1][1] = y1;
    xy[2][0] = x1 + nx;
    xy[2][1] = y1 + ny * 2;
    xy[3][0] = x + nx;
    xy[3][1] = y + ny * 2;
    for (unsigned i = 0; i < 4; i++)
        for (unsigned channel = 0; channel < 4; channel++)
            color[i][channel] =
                (i == 0 || i == 3 ? a[channel] : b[channel]) / (channel == 3 ? 128.0f : 255.0f);
    const unsigned indices[2][3] = {{0, 1, 2}, {0, 2, 3}};
    for (unsigned t = 0; t < 2; t++) {
        float points[3][2], colors[3][4];
        for (unsigned i = 0; i < 3; i++) {
            memcpy(points[i], xy[indices[t][i]], sizeof points[i]);
            memcpy(colors[i], color[indices[t][i]], sizeof colors[i]);
        }
        assert(em_gfx_overlay_triangle(gfx, points, colors, white_u, white_v, mode));
    }
}
static void text_original(int proportional, float x, float y, int w, int h, const char *value,
                          uint32_t rgba)
{
    EmHudTextStyle style;
    if (proportional) {
        assert(h == 20);
        style = EM_HUD_TEXT_TALL;
        if (!rgba)
            rgba = 0x80808080;
    } else if (w == 12 && h == 12)
        style = EM_HUD_TEXT_NUM12;
    else if (w == 16 && h == 16)
        style = EM_HUD_TEXT_NUM16;
    else if (w == 12 && h == 16)
        style = EM_HUD_TEXT_NAME12_BLUE;
    else {
        assert(w == 10 && h == 10);
        style = EM_HUD_TEXT_PROFILE10;
    }
    em_hud_text_color(gfx, x - 1792, (y - 1936) * 2, value, style, rgba & 0xffffff);
}
static int trail_triangle(void *context, const int32_t source[3][2], unsigned intensity)
{
    (void)context;
    float xy[3][2];
    for (unsigned i = 0; i < 3; i++) {
        xy[i][0] = source[i][0] / 16.0f - 1792;
        xy[i][1] = (source[i][1] / 16.0f - 1936) * 2;
    }
    float k = intensity / 255.0f;
    const float color[3][4] = {{k, k, k, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
    return em_gfx_overlay_triangle(gfx, xy, color, white_u, white_v, EM_GFX_UI_ADD);
}
static void trail(float x, float y)
{
    EmItemStick stick;
    assert(em_item_stick_sample(&stick, 128, 128, &math_workers));
    assert(em_item_trail_step(&trail_state, &stick, x, y, &math_workers, trail_triangle, NULL));
}
static void render(void)
{
    em_gfx_overlay_canvas(gfx, 512, 448);
    const Sprite *background = find_sprite(0x20045ee59d421e40ULL);
    em_status_background_frame(gfx);
    assert(em_status_background_render(gfx, background->x, background->y, background->w,
                                       background->h));

#include EM_STATUS_HUB_COMMANDS

    em_gfx_overlay_canvas(gfx, EM_GFX_OVERLAY_W, EM_GFX_OVERLAY_H);
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    EmWindow *window = em_window_create("Original MAIN 2D worker fixture", 640, 480);
    assert(window);
    gfx = em_gfx_create(window);
    assert(gfx);
    load_atlas();
    /* 0020A7A0's sine 0011E2A8 reads the SDK tables of the user's ELF
     * (tools/export_sdk_math_tables.py). */
    assert(em_status_background_load_sdk("assets/sdk_math_tables.emsm"));
    em_gfx_begin_frame(gfx, 0, 0, 0, 1);
    const float empty_xy[3][2] = {{0, 0}, {0, 0}, {0, 0}}, empty_color[3][4] = {{0}};
    for (unsigned i = 0; i < EM_GFX_DECOR_MAX; i++)
        assert(
            em_gfx_overlay_triangle(gfx, empty_xy, empty_color, white_u, white_v, EM_GFX_UI_ALPHA));
    assert(!em_gfx_overlay_triangle(gfx, empty_xy, empty_color, white_u, white_v, EM_GFX_UI_ALPHA));
    em_gfx_end_frame(gfx);
    puts("Decor capacity:4096 accepted, next rejected; original records render after frame reset");
    for (unsigned frame = 0; frame < 3; frame++) {
        EmEvent event;
        while (em_window_poll(window, &event)) {
        }
        em_gfx_begin_frame(gfx, 0, 0, 0, 1);
        render();
        if (frame == 2)
            em_gfx_request_capture(gfx, argv[1]);
        em_gfx_end_frame(gfx);
    }
    em_gfx_destroy(gfx);
    em_window_destroy(window);
    puts("Original MAIN 2D fixture completed; models and background phase remain explicit "
         "boundaries");
    return 0;
}
