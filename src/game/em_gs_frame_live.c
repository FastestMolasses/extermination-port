/* em_gs_frame_live.c - see em_gs_frame_live.h. */
#include "game/em_gs_frame_live.h"

#include <stdio.h>
#include <string.h>

#include "em_settings.h"
#include "game/em_frame.h"
#include "game/em_effects_live.h"
#include "game/em_render_context_live.h"

#define HEAD_MAX 0x800u

/* The original memory the GS frame's state blocks are read from. */
static const uint8_t *reader(void *ctx, uint32_t address, uint32_t size)
{
    (void)ctx;
    const uint8_t *p = em_rcl_bytes(address, size);
    return p ? p : em_effects_live_window(address, size);
}

int em_gs_frame_live_install(EmGfx *gfx)
{
    if (em_settings()->gpu_renderer)
        return 0;
    if (em_gfx_gs_world_enable(gfx, 1, reader, NULL) < 0) {
        fprintf(stderr, "gs frame: the renderer cannot run the GS frame\n");
        return -1;
    }
    if (em_gfx_gs_memory_load(gfx, EM_GS_FRAME_LIVE_MEMORY) < 0) {
        fprintf(stderr, "gs frame: %s is missing or malformed (run tools/export_gs_memory.py)\n",
                EM_GS_FRAME_LIVE_MEMORY);
        return -1;
    }
    return 0;
}

int em_gs_frame_live_kick(EmGfx *gfx)
{
    if (!em_gfx_gs_world_enabled(gfx)) return 0;
    static uint8_t env[HEAD_MAX], clear[HEAD_MAX];
    uint32_t env_bytes = 0, clear_bytes = 0;
    const char *why = NULL;
    if (em_rcl_kick_head(env, sizeof env, &env_bytes, clear, sizeof clear, &clear_bytes, &why) < 0) {
        fprintf(stderr, "gs frame: the kicked list's head is not 001D2300's: %s\n", why ? why : "?");
        return -1;
    }
    if (em_gfx_gs_world_kick(gfx, env, env_bytes, clear, clear_bytes) < 0) {
        fprintf(stderr, "gs frame: the frame faulted: %s\n", em_gfx_gs_world_fault(gfx));
        return -1;
    }
    return 0;
}

int em_gs_frame_live_cost(void *gfx, struct EmFrameGsCost *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof *out);
    EmGfxGsCost c;
    EmGfxPresentCost pc;
    const int gs = em_gfx_gs_world_cost((EmGfx *)gfx, &c) == 0;
    const int present = em_gfx_present_cost((EmGfx *)gfx, &pc) == 0;
    if (!gs && !present) return -1;
    if (gs) {
        out->wall_ns = c.wall_ns;
        out->cpu_max_ns = c.cpu_max_ns;
        out->runs = c.runs;
        out->workers = c.workers;
    }
    if (present) {
        out->has_present = 1;
        out->display_wait_ns = pc.display_wait_ns;
        out->field_wait_ns = pc.field_wait_ns;
    }
    return 0;
}
