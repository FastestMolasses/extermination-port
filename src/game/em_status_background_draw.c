/* The live 0020A7A0 (em_status_background.h): one shared state and the
 * worker bindings for the native renderer. */
#include "em_gfx.h"
#include "game/em_random.h"
#include "game/em_sdk_math_original.h"
#include "game/em_status_background.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    EmGfx *gfx;
    float u, v, w, h;
    EmSdkMathContext *sdk;
} Draw;

/* D_002655A0 is initialised by the ELF's .data at boot and written only by
 * 0020A7A0, so one state serves every status page for the whole run. */
static EmStatusBackground s_state;
static int s_ready;
static unsigned long s_steps; /* completed live 0020A7A0 calls */

/* 0011E2A8 is the translated original sinf (em_sdk_math_original.c) over
 * the tables the user's ELF holds at D_0026C170..D_0026C658
 * (tools/export_sdk_math_tables.py). A fault is recorded in the context
 * and fails the call (no value is substituted). */
static EmSdkMathTables s_tables;
static EmSdkMathContext s_sdk = {.tables = &s_tables};
static int s_sdk_ready;

int em_status_background_load_sdk(const char *path)
{
    int ok = em_sdk_math_original_load_export(path, &s_tables, NULL) == 0;
    s_sdk_ready = ok;
    if (!ok)
        fprintf(stderr, "status background: the SDK sine tables %s are missing or invalid "
                        "(tools/export_sdk_math_tables.py)\n", path ? path : "(null)");
    return ok;
}

static float sine(void *context, float angle)
{
    return em_sdk_math_original_float_0011E2A8(((Draw *)context)->sdk, angle);
}

static int32_t random_word(void *context)
{
    (void)context;
    return (int32_t)em_random_next();
}

/* 00207D00(1, 0): slot 1's blend mode 0, the mode every backdrop quad is
 * drawn with. */
static int mode(void *context, int slot, int value)
{
    (void)context;
    return slot == 1 && value == 0;
}

/* 00207E40's sprite on the 512x448 status canvas: GS x (12.4) minus the
 * 1792 offset, field y (12.4) minus 1936 doubled to frame lines; the tile's
 * full texture (TEX0 TW/TH) over w canvas pixels by h frame lines (8 * h
 * GS units is h / 2 field lines). The atlas orientation is the one every
 * status sprite uses (em_battery_ui.c sprite()). Sprites wholly outside the
 * canvas cover no pixel and are not queued. */
static int sprite(void *context, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t rgba)
{
    Draw *draw = context;
    float left = x / 16.0f - 1792.0f, top = (y / 16.0f - 1936.0f) * 2.0f;
    if (left >= EM_GFX_STATUS_W || top >= EM_GFX_STATUS_H || left + (float)w <= 0.0f ||
        top + (float)h <= 0.0f)
        return 1;
    const float color[4] = {(rgba & 255) / 128.0f, ((rgba >> 8) & 255) / 128.0f,
                            ((rgba >> 16) & 255) / 128.0f, ((rgba >> 24) & 255) / 128.0f};
    em_gfx_overlay_backdrop(draw->gfx, left, top, (float)w, (float)h, draw->u, draw->v,
                            draw->u + draw->w, draw->v + draw->h, color);
    return 1;
}

int em_status_background_render(EmGfx *gfx, float u, float v, float w, float h)
{
    if (!gfx || !s_sdk_ready)
        return 0;
    if (!s_ready) {
        em_status_background_init(&s_state);
        s_ready = 1;
    }
    Draw draw = {gfx, u, v, w, h, &s_sdk};
    const EmStatusBackgroundWorkers workers = {&draw, sine, random_word, mode, sprite};
    s_sdk.fault = 0;
    if (em_status_background_step(&s_state, &workers) != 1 || s_sdk.fault) {
        if (s_sdk.fault)
            fprintf(stderr, "status background: 0011E2A8 faulted at %08X\n",
                    (unsigned)s_sdk.fault);
        return 0;
    }
    ++s_steps;
    return 1;
}

unsigned long em_status_background_live_steps(void)
{
    return s_steps;
}

void em_status_background_frame(EmGfx *gfx)
{
    static const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (gfx)
        em_gfx_overlay_backdrop_fill(gfx, black);
}
