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
static int own_steps, drawn_steps, step_result = 1;
static uint32_t drawn_token;
static float drawn_uvwh[4], own_uvwh[4];
int em_status_background_render(struct EmGfx *gfx, float u, float v, float width, float height)
{
    (void)gfx;
    ++own_steps;
    own_uvwh[0] = u;
    own_uvwh[1] = v;
    own_uvwh[2] = width;
    own_uvwh[3] = height;
    printf("B %.9g %.9g %.9g %.9g\n", u, v, width, height);
    return 1;
}
/* The GPU renderer's path with the item sink bound: the render draws the
 * step 0020F170 made at call time (em_item_ui_background_stepped) instead
 * of stepping D_002655A0 again. Only the stepped section below reaches it. */
int em_status_background_render_step(struct EmGfx *gfx, uint32_t token, float u, float v, float width,
                                     float height)
{
    assert(gfx);
    ++drawn_steps;
    drawn_token = token;
    drawn_uvwh[0] = u;
    drawn_uvwh[1] = v;
    drawn_uvwh[2] = width;
    drawn_uvwh[3] = height;
    return step_result;
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
    /* Stepped: one render draws that step (its token, the ITEM tile the own
     * step was given) and never steps itself; the token is consumed, so the
     * next render steps on its own again. A refused draw fails the render
     * and is consumed too. */
    assert(own_steps > 0 && !drawn_steps);
    const int own_before = own_steps;
    em_item_ui_background_stepped(ui, 0x1234u);
    assert(em_item_ui_render(ui, (EmGfx *)1, 0, -1, trail, NULL));
    assert(drawn_steps == 1 && drawn_token == 0x1234u && own_steps == own_before);
    for (int i = 0; i < 4; i++)
        assert(drawn_uvwh[i] == own_uvwh[i]);
    assert(em_item_ui_render(ui, (EmGfx *)1, 0, -1, trail, NULL));
    assert(drawn_steps == 1 && own_steps == own_before + 1);
    step_result = 0;
    em_item_ui_background_stepped(ui, 0x1235u);
    assert(!em_item_ui_render(ui, (EmGfx *)1, 0, -1, trail, NULL));
    assert(drawn_steps == 2 && drawn_token == 0x1235u && own_steps == own_before + 1);
    assert(em_item_ui_render(ui, (EmGfx *)1, 0, -1, trail, NULL));
    assert(drawn_steps == 2 && own_steps == own_before + 2);
    em_item_ui_free(ui);
    puts("PASS");
}
