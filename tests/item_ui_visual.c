/* Bounded original-artwork capture. Transcendental workers use host libm;
 * this fixture does not assert whole-SDK or status-lifecycle equivalence. */
#include "game/em_item_trail.h"
#include "game/em_item_ui.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

typedef struct {
    EmGfx *gfx;
    EmItemMath math;
    EmItemTrail trail;
    float u, v;
} Fixture;

static float sine(void *context, float value)
{
    (void)context;
    return sinf(value);
}
static float cosine(void *context, float value)
{
    (void)context;
    return cosf(value);
}
static float square_root(void *context, float value)
{
    (void)context;
    return sqrtf(value);
}
static float arctangent(void *context, float y, float x)
{
    (void)context;
    return atan2f(y, x);
}

static int triangle(void *context, const int32_t source[3][2], unsigned intensity)
{
    Fixture *fixture = context;
    float xy[3][2];
    for (unsigned i = 0; i < 3; ++i) {
        xy[i][0] = source[i][0] / 16.0f - 1792;
        xy[i][1] = (source[i][1] / 16.0f - 1936) * 2;
    }
    float value = intensity / 255.0f;
    const float colors[3][4] = {{value, value, value, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
    return em_gfx_overlay_triangle(fixture->gfx, xy, colors, fixture->u, fixture->v, EM_GFX_UI_ADD);
}

static int draw_trail(void *context, EmGfx *gfx, float x, float y, float u, float v)
{
    Fixture *fixture = context;
    fixture->gfx = gfx;
    fixture->u = u;
    fixture->v = v;
    EmItemStick neutral;
    if (!em_item_stick_sample(&neutral, 128, 128, &fixture->math))
        return 0;
    return em_item_trail_step(&fixture->trail, &neutral, x, y, &fixture->math, triangle, fixture);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    EmItemUI *ui = em_item_ui_load(argv[1]);
    assert(ui);
    EmWindow *window = em_window_create("Original ITEM artwork regression", 640, 480);
    assert(window);
    EmGfx *gfx = em_gfx_create(window);
    assert(gfx);
    Fixture fixture = {.math = {NULL, sine, cosine, arctangent, square_root}};
    for (unsigned frame = 0; frame < 3; ++frame) {
        EmEvent event;
        while (em_window_poll(window, &event)) {
        }
        em_gfx_begin_frame(gfx, 0, 0, 0, 1);
        assert(em_item_ui_render(ui, gfx, 0, -1, draw_trail, &fixture));
        if (frame == 2)
            em_gfx_request_capture(gfx, argv[2]);
        em_gfx_end_frame(gfx);
    }
    em_item_ui_free(ui);
    em_gfx_destroy(gfx);
    em_window_destroy(window);
    puts("ITEM original-artwork capture completed");
    return 0;
}
