#include "game/em_hud.h"
#include "game/em_item_ui.h"
#include <assert.h>
#include <stdio.h>

static int uploads, glyphs;
int em_gfx_overlay_texture_set(EmGfx *gfx, int slot, const uint8_t *pixels, uint32_t width,
                               uint32_t height)
{
    (void)gfx;
    assert(slot == 1 && pixels && width && height);
    ++uploads;
    return 1;
}
void em_gfx_overlay_canvas(EmGfx *gfx, float width, float height)
{
    (void)gfx;
    (void)width;
    (void)height;
}
void em_hud_decor_invalidate(void)
{
}
int em_status_background_render(struct EmGfx *gfx, float u, float v, float width, float height)
{
    (void)gfx;
    printf("B %.9g %.9g %.9g %.9g\n", u, v, width, height);
    return 1;
}
void em_status_background_frame(struct EmGfx *gfx)
{
    (void)gfx;
}
void em_hud_text_color(EmGfx *gfx, float x, float y, const char *text, EmHudTextStyle style,
                       uint32_t color)
{
    (void)gfx;
    (void)x;
    (void)color;
    assert(y >= 336 && text && style == EM_HUD_TEXT_TALL);
    ++glyphs;
}
float em_hud_text_width(const char *text, EmHudTextStyle style)
{
    (void)text;
    (void)style;
    return 0; /* Glyph metrics are a separate presenter boundary. */
}
void em_gfx_overlay_sprite_blend(EmGfx *gfx, float x, float y, float w, float h, float u, float v,
                                 float u1, float v1, const float color[4], EmGfxOverlayBlend mode)
{
    (void)gfx;
    printf("Q %u %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n", mode, x, y, w, h,
           u, v, u1, v1, color[0], color[1], color[2], color[3]);
}
static int trail(void *context, EmGfx *gfx, float x, float y, float u, float v)
{
    (void)context;
    (void)gfx;
    printf("T %.9g %.9g %.9g %.9g\n", x, y, u, v);
    return 1; /* Actual fan worker is an explicit ordered boundary. */
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    EmItemUI *ui = em_item_ui_load(argv[1]);
    assert(ui);
    assert(!em_item_ui_render(ui, (EmGfx *)1, 0, -1, NULL, NULL));
    assert(!uploads);
    for (unsigned selection = 0; selection < 6; ++selection) {
        printf("L %u\n", selection);
        assert(em_item_ui_render(ui, (EmGfx *)1, selection, (int)selection - 1, trail, NULL));
    }
    assert(uploads == 1 && glyphs > 0);
    em_item_ui_deactivate(ui);
    puts("R");
    assert(em_item_ui_render(ui, (EmGfx *)1, 0, -1, trail, NULL));
    assert(uploads == 2);
    em_item_ui_free(ui);
    puts("PASS");
}
