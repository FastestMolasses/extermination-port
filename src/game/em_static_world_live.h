/* em_static_world_live.h - the static world drawn live from its original
 * packets (docs/STATIC_WORLD.md section 7).
 *
 * 001C1D00 (em_rcl_001C1D00) writes the static world's channel-0 run every
 * world frame: 001D5370's walk over the static-object bank emits, for each
 * object the view does not reject, the REFs of its blocks behind the level
 * kernel's CALL, and for an object partly outside the guard band a second
 * pass through the clip kernel. Step V's list sends channel 0 to VU1 right
 * after the channel-3 background. At the frame close frame_close_out calls
 * em_static_world_live_draw: em_static_world_draw walks this frame's run
 * over the render context's storage and the bank (em_rcl_bytes), runs both
 * kernels' translations, and em_gfx_gs_opaque draws every triangle the GS
 * would draw, in GS order. This replaces the port's legacy level meshes
 * (the scene_snow zone EMDLs), which no longer load in AREA11.
 *
 * The run's first UNPACK inherits the VIF cycle the list left: STCYCL 4,4
 * when step V CALLs the channel-3 list before channel 0 (001E0DF0 with
 * +0x1D8 non-zero: the list ends in the background kernel packet 0x0023C990,
 * whose STCYCL is 4,4). A frame without that CALL (the frame a movie played:
 * 001D1C10 sets render flag 4, which the port reads as em_frame_movie_active;
 * in the first level only the departure movie's frame, route beat 15)
 * inherits the cycle the previous frame's VIF1 stream left:
 * EM_STATIC_WORLD_FRAME_CYCLE = 1,1. Evidence: VIF1_CYCLE (0x10003C40 of the
 * EE hardware registers) is 0x0101 between frames in all 18 first-level save
 * states (route 00..15 and the exit captures exit_00 / exit_01), and every
 * STCYCL the first level's packets issue is 1,1 or 4,4 (the kernel packets,
 * the skin records and blocks, the object units, the weather and owner
 * services packets): CL == WL, under which an UNPACK writes contiguously,
 * the only property the walk uses. Nothing measures the cycle inside the
 * movie frame itself (no capture between its movie and step V).
 *
 * Fail-stop: a walk fault or a triangle the renderer does not implement
 * latches (em_static_world_live_fault); the caller faults the scene. */
#ifndef EM_STATIC_WORLD_LIVE_H
#define EM_STATIC_WORLD_LIVE_H

#include <stdint.h>

#include "em_gfx.h"

/* The VIF1 cycle (CL = WL) a frame without the channel-3 CALL inherits (see
 * above). */
#define EM_STATIC_WORLD_FRAME_CYCLE 1u

#ifdef __cplusplus
extern "C" {
#endif

/* Walk and draw the run this frame's 001C1D00 wrote (em_rcl_static_run).
 * 0 (drawn, or no run this frame), or -1 (latched). */
int em_static_world_live_draw(EmGfx *gfx);

/* The latched fault's original address (0x001D5370 for a walk fault, the
 * run's start), or 0. */
uint32_t em_static_world_live_fault(void);

/* The level smoke's view (the tick log's "static"). */
typedef struct {
    uint32_t frame;            /* em_frame_counter() of the last draw          */
    uint32_t runs;             /* cumulative runs drawn                         */
    uint32_t start, end;       /* the last run                                  */
    uint32_t digest;           /* FNV-1a of the last run's bytes                */
    uint32_t batches[2];       /* the last run's level / clip kernel batches    */
    uint32_t triangles[2];     /* the last run's triangles from each kernel     */
    uint32_t culled;           /* the last run's vertices culled by the kernel  */
    uint32_t prim_digest;      /* FNV-1a of the last run's triangles            */
    uint32_t total_triangles;  /* cumulative                                    */
} EmStaticWorldLiveLog;
void em_static_world_live_log(EmStaticWorldLiveLog *out);

/* The last run's bytes as the walk read them (the smoke compares sampled
 * runs with the original's), or NULL. */
const uint8_t *em_static_world_live_run(uint32_t *size);

/* The channel-3 background list at context +0x1D8 (001E1E60's, rebuilt
 * every world frame by 001C1D00 -> 001E0CF0) as its DMA sends it: the TEX0_1
 * and RGBAQ its A+D packets write (001D6F60's and 001D7080's), read from the
 * list's bytes up to its RET (the CALL of the kernel packet 0x0023C990 is
 * not entered). 1 (found; *tex0, *rgbaq = the register values, RGBAQ's R, G,
 * B, A in bytes 0..3), 0 (+0x1D8 is 0: no list this frame), -1 (a form the
 * list does not hold: reported). */
int em_static_world_live_background_state(uint64_t *tex0, uint32_t *rgbaq);

#ifdef __cplusplus
}
#endif

#endif
