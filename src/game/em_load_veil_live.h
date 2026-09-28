/* em_load_veil_live.h - the load veil's frames drawn live (docs/
 * LOAD_VEIL_PARTICLES.md section 3).
 *
 * While 001ADF50 loads an area, its veil state machine 0021B550 calls the
 * veil draw 0021B1B0 on the ticks of state 1 and state 2 sub 0
 * (em_scene_bindings: em_rcl_0021B1B0, em_load_veil_particles over the
 * render context), which writes the veil's packets at the channel-0 cursor:
 * the 512 Gouraud line segments and the two lens passes (the frame copied
 * into the 256 x 256 texture at GS 0x258000 and drawn back through a
 * 15 x 15 grid of textured strips, opaque, then added at half). Main-loop
 * step V (001D2300) builds the frame's list: the draw environment of the
 * frame buffer, the clear (the black one: 0021B550 registers render flag 3
 * every such tick), channel 0, the page and channel 1, closed by the END tag
 * at the GS block + 0x10; its kick sends that list to the GS.
 *
 * em_load_veil_live_draw runs right after step V in every frame. In a frame
 * whose 0021B1B0 ran (em_rcl_veil_span), it walks the kicked list as the DMA
 * sends it (em_chain_page_run_list over the render context's storage) and
 * draws it with the GS frame stage (em_gfx_gs_frame): the frame's whole
 * image comes from that list, as on the PS2. It checks that the walk
 * transferred every qword of the veil's channel-0 run, and shows the frame
 * buffer of the slot the list drew (bank A's environment at D_00275674 +
 * 0x20 + 0x190 * slot). Frames without the veil are not touched.
 *
 * Fail-stop: a walk fault, a primitive the GS frame stage refuses, or a run
 * the walk did not transfer latches the fault (the step V caller stops the
 * loop). */
#ifndef EM_LOAD_VEIL_LIVE_H
#define EM_LOAD_VEIL_LIVE_H

#include <stdint.h>

#include "em_gfx.h"
#include "game/em_chain_page.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_LOAD_VEIL_LIVE_PRIMS 4096u

/* After step V: draw this frame's veil list, if the veil drew. 0 (drawn, or
 * no veil this frame), or -1 (latched). */
int em_load_veil_live_draw(EmGfx *gfx);

/* The latched fault's original address (0x001D2300 for the list walk or
 * the GS frame, 0x0021B1B0 for an untransferred run), or 0. */
uint32_t em_load_veil_live_fault(void);

/* The level smoke's view of the last veil frame (the tick log's "veil_draw"). */
typedef struct {
    uint32_t frame;            /* em_frame_counter() at the draw           */
    uint32_t draws;            /* veil frames drawn so far                */
    uint32_t start, end, slot; /* the channel-0 run and the buffer index  */
    uint32_t chain;            /* the list step V kicked                  */
    uint64_t display_frame;    /* FRAME_1 of the slot's draw environment  */
    EmChainPageCounts counts;
    uint32_t digest;           /* FNV-1a of the primitives and environments */
    uint32_t max_line_rgb;     /* the brightest line colour byte (0: black) */
} EmLoadVeilLiveLog;
void em_load_veil_live_log(EmLoadVeilLiveLog *out);

/* The last veil frame's channel-0 run as the kick sent it (end - start
 * bytes), or NULL. */
const uint8_t *em_load_veil_live_run(uint32_t *size);

#ifdef __cplusplus
}
#endif

#endif
