/* Asset-backed original 209DF0 hub adapter fixture. The renderer and font
 * are recording stubs: tools/test_status_hub_ui_reference.py compares the
 * recorded stream with original execution; main() is the ASan/UBSan
 * lifecycle and failure-boundary check. Build with -DHUB_UI_LIBRARY to
 * expose only the recorder to the Python reference test. */
#include "game/em_hud.h"
#include "game/em_status_hub_ui.h"

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *log_text;
static size_t log_size, log_capacity;
static int font_ready = 1, texture_result = 1, fail_triangle = -1;
static unsigned uploads, invalidations, triangles;

static void record(const char *format, ...)
{
    char line[512];
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(line, sizeof line, format, arguments);
    va_end(arguments);
    assert(length > 0 && (size_t)length < sizeof line);
    if (log_size + (size_t)length + 2 > log_capacity) {
        log_capacity = (log_size + (size_t)length + 2) * 2;
        log_text = realloc(log_text, log_capacity);
        assert(log_text);
    }
    memcpy(log_text + log_size, line, (size_t)length);
    log_size += (size_t)length;
    log_text[log_size++] = '\n';
    log_text[log_size] = 0;
}

void hub_test_reset(int font, int texture, int triangle)
{
    log_size = 0;
    if (log_text)
        log_text[0] = 0;
    font_ready = font;
    texture_result = texture;
    fail_triangle = triangle;
    uploads = invalidations = triangles = 0;
}

const char *hub_test_log(void)
{
    return log_text ? log_text : "";
}

unsigned hub_test_uploads(void)
{
    return uploads;
}

int em_gfx_overlay_texture_set(EmGfx *gfx, int slot, const uint8_t *rgba, uint32_t w, uint32_t h)
{
    assert(gfx && slot == EM_GFX_OVERLAY_TEX_UI && rgba && w && h);
    ++uploads;
    record("U %u %u", w, h);
    return texture_result;
}

void em_gfx_overlay_canvas(EmGfx *gfx, float w, float h)
{
    assert(gfx);
    record("C %.9g %.9g", w, h);
}

void em_hud_decor_invalidate(void)
{
    ++invalidations;
    record("I");
}

void em_hud_text_color(EmGfx *gfx, float x, float y, const char *text, EmHudTextStyle style,
                       uint32_t rgb)
{
    assert(gfx && text && strlen(text) < 256);
    record("X %d %.9g %.9g %06x %s", (int)style, x, y, rgb, text);
}

float em_hud_text_width(const char *text, EmHudTextStyle style)
{
    assert(text);
    (void)style;
    return font_ready ? 12.0f * (float)strlen(text) : 0.0f;
}

void em_gfx_overlay_sprite_blend(EmGfx *gfx, float x, float y, float w, float h, float u, float v,
                                 float u1, float v1, const float c[4], EmGfxOverlayBlend mode)
{
    assert(gfx && mode <= EM_GFX_UI_OPAQUE);
    record("Q %d %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g", (int)mode, x, y, w, h,
           u, v, u1, v1, c[0], c[1], c[2], c[3]);
}

int em_gfx_overlay_triangle(EmGfx *gfx, const float xy[3][2], const float c[3][4], float u, float v,
                            EmGfxOverlayBlend mode)
{
    assert(gfx && mode <= EM_GFX_UI_OPAQUE);
    if ((int)triangles++ == fail_triangle)
        return 0;
    record("T %d %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g "
           "%.9g %.9g %.9g %.9g",
           (int)mode, xy[0][0], xy[0][1], xy[1][0], xy[1][1], xy[2][0], xy[2][1], c[0][0], c[0][1],
           c[0][2], c[0][3], c[1][0], c[1][1], c[1][2], c[1][3], c[2][0], c[2][1], c[2][2], c[2][3],
           u, v);
    return 1;
}

#ifndef HUB_UI_LIBRARY
/* Host libm values stand in for the SDK workers, as in the trail fixture. */
static float sine(void *context, float x)
{
    (void)context;
    return sinf(x);
}
static float cosine(void *context, float x)
{
    (void)context;
    return cosf(x);
}
static float arctangent(void *context, float y, float x)
{
    (void)context;
    return atan2f(y, x);
}
static float root(void *context, float x)
{
    (void)context;
    return sqrtf(x);
}

static EmStatusHubDisplay display(void)
{
    EmStatusHubDisplay value;
    memset(&value, 0, sizeof value);
    value.health = 100;
    value.infection = 0;
    value.battery_equipped = 1;
    value.charge = 48;
    value.capacity = 48;
    value.ammo.primary = 0;
    value.ammo.secondary = 1;
    value.ammo.amount[0] = 12;
    value.ammo.reserve = 60;
    return value;
}

static unsigned count(const char *kind)
{
    unsigned total = 0;
    size_t length = strlen(kind);
    for (const char *line = hub_test_log(); *line; line = strchr(line, '\n') + 1)
        total += !strncmp(line, kind, length) && line[length] == ' ';
    return total;
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    const EmItemMath math = {NULL, sine, cosine, arctangent, root};
    const EmItemMath missing = {NULL, sine, NULL, arctangent, root};
    EmGfx *gfx = (EmGfx *)(uintptr_t)1;
    EmItemStick stick = {0, 0, 0, 0};
    EmItemTrail trail;
    em_item_trail_reset(&trail);
    uint32_t clock = 0;
    EmStatusHubDisplay shown = display();

    assert(!em_status_hub_ui_load(argv[1], argv[2], &missing));
    assert(!em_status_hub_ui_load(argv[1], "missing.emha", &math));
    assert(!em_status_hub_ui_load(argv[2], argv[2], &math));
    assert(!em_status_hub_ui_load(argv[1], argv[1], &math));

    /* Ordinary lifecycle: prepare once, render twice, page invalidation. */
    hub_test_reset(1, 1, -1);
    EmStatusHubUI *ui = em_status_hub_ui_load(argv[1], argv[2], &math);
    assert(ui && !em_status_hub_ui_render(ui, gfx, -1) && !uploads);
    assert(em_status_hub_ui_prepare(ui, &shown, &stick, &clock, &trail) && clock == 1);
    assert(trail.cursor == 1);
    EmStatusHubUICommand command;
    unsigned trail_commands = 0, commands = em_status_hub_ui_command_count(ui);
    assert(commands > 40 && em_status_hub_ui_command(ui, 0, &command) &&
           command.kind == EM_STATUS_HUB_UI_BLEND && command.mode == 0);
    for (unsigned i = 0; i < commands; ++i) {
        assert(em_status_hub_ui_command(ui, i, &command));
        if (command.kind == EM_STATUS_HUB_UI_TRAIL) {
            ++trail_commands;
            assert(command.trail_count == 512 && command.mode == 1);
        }
    }
    assert(trail_commands == 1 && !em_status_hub_ui_command(ui, commands, &command));
    assert(em_status_hub_ui_help_count(ui, 5) == 4 && !em_status_hub_ui_help_count(ui, 10));
    assert(em_status_hub_ui_render(ui, gfx, -1) && uploads == 1 && invalidations == 1);
    unsigned first = count("T"), first_text = count("X");
    assert(first > 512 && first_text > 0);
    assert(em_status_hub_ui_render(ui, gfx, 5) && uploads == 1 && clock == 1);
    assert(count("X") == first_text * 2 + 4 && count("T") == first * 2);
    em_status_hub_ui_deactivate(ui);
    assert(invalidations == 2);
    assert(em_status_hub_ui_render(ui, gfx, 9) && uploads == 2);
    /* A second 209DF0 call advances the same UI+20 clock and shared ring. */
    assert(em_status_hub_ui_prepare(ui, &shown, &stick, &clock, &trail) && clock == 2 &&
           trail.cursor == 2);
    /* An invalid presenter line is a renderer fault and remains latched. */
    assert(!em_status_hub_ui_render(ui, gfx, 10));
    assert(!em_status_hub_ui_prepare(ui, &shown, &stick, &clock, &trail) && clock == 2);
    assert(!em_status_hub_ui_render(ui, gfx, -1) && !em_status_hub_ui_command_count(ui));
    em_status_hub_ui_free(ui);

    /* Rejected display values never reach a draw worker. */
    const struct {
        unsigned hover;
        float infection;
        uint8_t secondary;
    } rejected[] = {{5, 0, 1}, {0, -1, 1}, {0, 100.5f, 1}, {0, NAN, 1}, {0, 0, 5}};
    for (unsigned i = 0; i < sizeof rejected / sizeof *rejected; ++i) {
        ui = em_status_hub_ui_load(argv[1], argv[2], &math);
        assert(ui);
        EmStatusHubDisplay bad = display();
        bad.hover = (uint8_t)rejected[i].hover;
        bad.infection = rejected[i].infection;
        bad.ammo.secondary = rejected[i].secondary;
        clock = 7;
        assert(!em_status_hub_ui_prepare(ui, &bad, &stick, &clock, &trail));
        assert(!em_status_hub_ui_prepare(ui, &shown, &stick, &clock, &trail));
        em_status_hub_ui_free(ui);
    }
    ui = em_status_hub_ui_load(argv[1], argv[2], &math);
    assert(ui && !em_status_hub_ui_prepare(ui, &shown, &stick, NULL, &trail));
    em_status_hub_ui_free(ui);
    ui = em_status_hub_ui_load(argv[1], argv[2], &math);
    assert(ui && !em_status_hub_ui_prepare(ui, &shown, &stick, &clock, NULL));
    em_status_hub_ui_free(ui);

    /* Required renderer workers: font, texture upload and each triangle. */
    const int faults[][3] = {{0, 1, -1}, {1, 0, -1}, {1, 1, 0}, {1, 1, 700}};
    for (unsigned i = 0; i < sizeof faults / sizeof *faults; ++i) {
        ui = em_status_hub_ui_load(argv[1], argv[2], &math);
        assert(ui && em_status_hub_ui_prepare(ui, &shown, &stick, &clock, &trail));
        hub_test_reset(faults[i][0], faults[i][1], faults[i][2]);
        assert(!em_status_hub_ui_render(ui, gfx, -1));
        hub_test_reset(1, 1, -1);
        assert(!em_status_hub_ui_render(ui, gfx, -1) && !uploads);
        em_status_hub_ui_free(ui);
    }
    free(log_text);
    puts("PASS");
    return 0;
}
#endif
