/* em_background_live.h - the level background drawn live from its original
 * list (docs/BACKGROUND.md "Port").
 *
 * 001C1D00 -> 001E0CF0 -> 001E1E60 (em_static_world, on the render
 * context) rebuilds render channel 3's list every world frame and stores its
 * start at context +0x1D8; step V's 001D2300 CALLs it (001E0DF0) right
 * after the Z-only clear, before the level. At the frame close
 * em_background_live_draw walks that list as the DMA sends it
 * (em_chain_page_run_call over the render context's storage and the
 * effect-table export's copy of the grid packet 0x0023C990): the GS state
 * its A+D packets write (001D1F80(3, 0, 7), 001D1FF0, 001D6F60, 001D7080),
 * the four 001D7100 uploads, the CALL of the grid packet and 001D71A0's
 * MSCAL, which runs the grid program's translation
 * (em_vu1_grid_program_mscal); every triangle of the 31 kicked strips goes
 * to em_gfx_background_prims, which draws them with the loaded asset's
 * texels. The list's ZBUF_1 must mask Z (the asset's ZMSK 1).
 *
 * Fail-stop: a walk fault, a list with no grid MSCAL or a refused triangle
 * latches (em_background_live_fault); the caller faults the scene. */
#ifndef EM_BACKGROUND_LIVE_H
#define EM_BACKGROUND_LIVE_H

#include <stdint.h>

#include "em_gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Walk the list at context +0x1D8 and draw its triangles. Call only when
 * step V's gate CALLs it (em_render_frame.c background_gate). 0, or -1
 * (latched). */
int em_background_live_draw(EmGfx *gfx);
uint32_t em_background_live_fault(void);

/* The level smoke's view (the tick log's "background"). */
typedef struct {
    uint32_t frame;            /* em_frame_counter() of the last draw        */
    uint32_t draws;            /* cumulative                                 */
    uint32_t start;            /* the last list's start (+0x1D8)             */
    uint32_t prims;            /* its triangles                              */
    uint32_t digest;           /* FNV-1a of every vertex's X, Y, S, T words  */
    uint32_t upload[11][4];    /* dmem 0x000, 0x081, 0x102, 0x200..0x207     */
} EmBackgroundLiveLog;
void em_background_live_log(EmBackgroundLiveLog *out);

#ifdef __cplusplus
}
#endif

#endif /* EM_BACKGROUND_LIVE_H */
