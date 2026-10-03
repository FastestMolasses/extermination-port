/* em_gs_frame_live - the game side of the Original profile's GS frame
 * (docs/GS_EXACT.md section 9; the model: src/gs/em_gs_world.h; the
 * renderer contract: em_gfx.h "The Original profile's GS frame").
 *
 * - At start-up, unless the GPU renderer is chosen (em_settings.h
 *   gpu_renderer), the GS frame is enabled with the reader of the original
 *   memory the game owns (the render context's storage and GS blocks,
 *   em_rcl_bytes; the ELF windows of the effect tables,
 *   em_effects_live_window) and the boot library's GS memory image is
 *   installed (assets/gs_library.emgm, tools/export_gs_memory.py). Without
 *   the image the game does not start (fail-stop). The area load's uploads
 *   reach the model through the loader's area consumer
 *   (em_scene_bindings.c loader_area_chain, em_gfx_gs_upload).
 * - The frame close's world flush declares the world frame
 *   (em_gfx_gs_world_frame, em_render_frame.c).
 * - Step V, after 001D2300's kick: the kicked list's head (its draw
 *   environment and clear, em_rcl_kick_head) goes to the model, which runs
 *   the frame. */
#ifndef EM_GS_FRAME_LIVE_H
#define EM_GS_FRAME_LIVE_H

#include "em_gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_GS_FRAME_LIVE_MEMORY "assets/gs_library.emgm"

/* 0 (enabled, or the GPU renderer chosen), or -1 (reported). */
int em_gs_frame_live_install(EmGfx *gfx);
/* Step V after 001D2300: 0, or -1 (reported: the fault of the GS frame). */
int em_gs_frame_live_kick(EmGfx *gfx);
/* EM_FRAME_TIMING's probe (em_frame_set_timing_probe; ctx = the EmGfx). */
struct EmFrameGsCost;
int em_gs_frame_live_cost(void *gfx, struct EmFrameGsCost *out);

#ifdef __cplusplus
}
#endif

#endif
