/* Bounded GPU fixture for the four recovered00207D00 UI blend modes. */
#include "em_gfx.h"
#include <assert.h>
#include <stdio.h>

static void quad(EmGfx *gfx, int column, const float color[4], EmGfxOverlayBlend blend,
                 int transparent_texel)
{
    float u = transparent_texel ? 1.5f : 0.5f;
    em_gfx_overlay_sprite_blend(gfx, column * 80.0f, 0, 80, 448, u, 0.5f, u, 0.5f,
                               color, blend);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    EmWindow *window = em_window_create("Original UI blend regression", 640, 480);
    assert(window);
    EmGfx *gfx = em_gfx_create(window);
    assert(gfx);
    const unsigned char texture[] = {255, 255, 255, 255, 255, 255, 255, 0};
    assert(em_gfx_overlay_texture_set(gfx, EM_GFX_OVERLAY_TEX_UI, texture, 2, 1));
    const float color[] = {0.1f, 0.2f, 0.3f, 0.5f};
    const float bright[] = {0.9f, 0.9f, 0.9f, 0};
    const float dark[] = {0.4f, 0.4f, 0.4f, 0};
    for (int frame = 0; frame < 3; ++frame) {
        EmEvent event;
        while (em_window_poll(window, &event)) {
        }
        em_gfx_begin_frame(gfx, 0.2f, 0.4f, 0.6f, 1);
        em_gfx_overlay_canvas(gfx, 640, 448);
        quad(gfx, 0, color, EM_GFX_UI_ALPHA, 0);
        quad(gfx, 1, color, EM_GFX_UI_ADD, 0);
        quad(gfx, 2, color, EM_GFX_UI_SUBTRACT, 0);
        quad(gfx, 3, color, EM_GFX_UI_OPAQUE, 0);
        quad(gfx, 4, bright, EM_GFX_UI_ADD, 0);
        quad(gfx, 4, dark, EM_GFX_UI_SUBTRACT, 0);
        quad(gfx, 5, dark, EM_GFX_UI_SUBTRACT, 0);
        quad(gfx, 5, bright, EM_GFX_UI_ADD, 0);
        quad(gfx, 6, color, EM_GFX_UI_OPAQUE, 1);
        quad(gfx, 7, color, EM_GFX_UI_ADD, 1);
        if (frame == 2)
            em_gfx_request_capture(gfx, argv[1]);
        em_gfx_end_frame(gfx);
    }
    em_gfx_destroy(gfx);
    em_window_destroy(window);
    puts("Original UI blend GPU fixture rendered; inspect captured pixel comparison.");
    return 0;
}
