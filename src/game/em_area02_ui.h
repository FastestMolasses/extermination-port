/* Level-4 side track (AREA01 revisit + AREA02), lane L4UI: the new boot
 * functions of the census a02 delta whose subsystem labels are ui_screens,
 * ui_menu_lib and draw2d (labels only; the roles below come from the code).
 * Docs: docs/AREA02_UI.md. Nothing here is wired.
 *
 * Of the 18 functions, 15 already have verified port translations, which
 * this lane reuses and re-checks over the level-4 captures (docs section 2).
 * The three without one are translated here:
 *
 *   00209DF0(ui)          the status hub's 2D layer: four marker glows
 *                         (00208750), the analog trail (0020AC70), eight
 *                         wheel arcs (002082B0), the health / battery /
 *                         ammunition readouts (00208AD0 / 00209280 /
 *                         00209860), the infection figure, sprites, labels
 *                         and rectangles, in the original's call order
 *   00208750(n, xy, rgb)  nine offset copies of one n-vertex quadratic
 *                         curve, written as GIF packets on packet slot 1
 *   00208AB0(a0, a1, a2)  forwards to 001D66A0(1, a0, a1, a2) with f12
 *
 * Contract: the AREA01 UI lane's (em_area01_ui.h, reused): the state is an
 * EmArea01Ui, every original byte is reached by its original address
 * through core.world, every callee outside this lane goes through `call`
 * (integer argument registers as 64-bit images, float arguments as binary32
 * bits), the routines carve their original frames below `sp`, and the
 * fail-stop latch is sticky (codes 1, 2, 4 and 6 as there).
 *
 * Code 6 here: 00209DF0 faults instead of calling 00209860 when the
 * ammunition selectors D_00810CA4 / D_00810CA6 select the path where
 * 00209860 uses its caller's saved register s0 as a texture word (a value
 * the call interface does not carry; docs section 1).
 *
 * Arithmetic: every COP1 operation through em_ee_float.h on bit patterns.
 * stdint only. */
#ifndef EM_AREA02_UI_H
#define EM_AREA02_UI_H

#include <stdint.h>

#include "game/em_area01_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* All entries: 0 on success, -1 on a fault (latched). */
int em_area02_ui_00209DF0(EmArea01Ui *s, uint32_t ui);
int em_area02_ui_00208750(EmArea01Ui *s, int32_t n, uint32_t xy, uint32_t rgb);
/* v0: 001D66A0's result (the EE tail call returns it unchanged). */
int em_area02_ui_00208AB0(EmArea01Ui *s, uint64_t a0, uint64_t a1, uint64_t a2, uint32_t f12, uint32_t *v0);

#ifdef __cplusplus
}
#endif

#endif /* EM_AREA02_UI_H */
