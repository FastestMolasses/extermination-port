/* em_hud.h — native STATUS SCREEN: the Triangle/Start-toggled player
 * status display.
 *
 * FAITHFULNESS (FINDINGS.md "STATUS SCREEN LAYOUT", session-25 full
 * draw-chain decode + the earlier "INVENTORY LOCATED" captures): the
 * original game shows NO persistent HUD during play. The status display
 * is a status SCREEN opened by TRIANGLE **or START** (verified: both
 * route to the same controller, func_0020CDC0 — there is no separate
 * pause menu). The port mirrors that: hidden by default, em_hud_update()
 * flips visibility on either button's edge.
 *
 * The screen composes on the engine's 512x448 UI canvas (GS offsets
 * 0x700/0x790), which em_hud selects via em_gfx_overlay_canvas for its
 * draws and restores afterwards. Faithful hub elements rendered today:
 * the HEALTH ring gauge at (208,197) (bg ring r36-56 with the <=35 red
 * state, light-blue fill arc r24-56 = 360deg*hp/100 from 180deg, the
 * rotating 1 s highlight wedge) through the em_gfx annular-arc
 * primitive; the BATTERY half-unit square bar at (16,118); the SPR4
 * reserve row at (16,190) (the real screen shows NO magazine state —
 * reserve only); INFECTION as text positions only (there is NO infection
 * bar in the original); the 8x8 blue block markers; the help panel rect
 * (128,336)-(384,432); and the +-1/frame count-up of the displayed
 * health/infection values (engine display copies 0x810858/0x81085C).
 *
 * STAND-INS (explicitly flagged):
 *  - The real screen swaps to an identity UI CAMERA — the 3D view
 *    becomes the status screen itself, with the rotating player model
 *    inside the ring gauge; it does NOT dim a live gameplay frame. The
 *    port's full-screen dim rect is the documented stand-in until the
 *    3D pass can be re-cameraed (whether the world SIMULATION also
 *    halts is still unverified in the engine; the port keeps simulating).
 *  - The ring's rotating highlight is additive (blend mode 1) on the
 *    GS; the overlay pass is alpha-blend only, so it approximates with
 *    white at low alpha.
 *  - TEXT IS A DOCUMENTED GAP: no font/glyph renderer yet, so every
 *    label and number ("HEALTH", "075 / 100", "04/06", "INFECTION 60%",
 *    the reserve count) renders as a positioned placeholder rect at the
 *    real glyph metrics (12 px labels, 16 px numbers, 10x20 tall font)
 *    in the real style color at low alpha. A font renderer is the
 *    screen's single biggest remaining fidelity unlock.
 *  - Not yet composed: profile bio block, portrait, title art, the
 *    page-selector diamond/rings/sparkles and page sub-screens (all
 *    texture- or text-dependent, or pending their own decode passes).
 *
 * EmPlayerStatus mirrors the engine's canonical status storage:
 *
 *   health      player actor +0x220 (0x008104D0), float — "075 / 100"
 *               (display max swaps to 60 under flag 0x8104E4 — not
 *               modeled yet)
 *   infection   player actor +0x228 (0x008104D8), float — "60%"
 *   mag         D_00810C62, u8 — rounds in the current SPR4 magazine.
 *               NOT shown on the status hub (the real screen has no
 *               magazine display); kept for the future page-2 view.
 *   reserve     D_00810CB4, s16 — SPR4 reserve rounds (the "120")
 *   battery     the status screen's "04/06" pair in DISPLAY units.
 *               Engine storage is HALF-units: current at 0x810CB2,
 *               max at 0x810CB7, both shown >>1; the segment bar draws
 *               one square per half-unit (battery * 2). Block gated on
 *               0x810C7F != 0 — the port gates on battery_max != 0.
 *
 * EM_HUD_FORCE=1 in the environment (checked once, on first render call)
 * forces the status screen VISIBLE regardless of the toggle — for
 * headless capture tests of the overlay itself. Hidden is the default,
 * so the default frame is already the status-screen-free one.
 */
#ifndef EM_HUD_H
#define EM_HUD_H

#include <stdint.h>

#include "em_gfx.h"
#include "game/em_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Native mirror of the engine's player status fields (see above for the
 * PS2 addresses each one shadows). */
typedef struct {
    float   health;       /* player +0x220 — current HP */
    float   health_max;   /* display maximum (the game shows /100) */
    float   infection;    /* player +0x228 — percent, 0..100 */
    uint8_t mag;          /* D_00810C62 — magazine rounds (page view only) */
    uint8_t mag_max;      /* magazine capacity (SPR4: 30) */
    int16_t reserve;      /* D_00810CB4 — reserve rounds */
    uint8_t battery;      /* 0x810CB2 >> 1 — battery, display units */
    uint8_t battery_max;  /* 0x810CB7 >> 1 — battery max, display units */
} EmPlayerStatus;

/* Per-frame toggle poll: a TRIANGLE or START press (edge, in->pressed)
 * flips the status screen's visibility — both buttons open the same
 * screen in the engine. Call once per gameplay frame with the frame
 * input block. */
void em_hud_update(const EmFrameInput *in);

/* Is the status screen currently shown? (toggle state OR EM_HUD_FORCE) */
int em_hud_visible(void);

/* Queue this frame's status screen into the overlay pass — the dim
 * backdrop first, then the status elements. Call once per gameplay
 * frame, between em_gfx_begin_frame and em_gfx_end_frame, after the 3D
 * draws are recorded (the overlay flushes last regardless). No-op while
 * hidden (the default) or st is NULL — a hidden status screen queues
 * NOTHING, so the default frame is byte-identical to a HUD-free build.
 * Selects the 512x448 status canvas for its primitives and restores the
 * 640x448 default before returning. */
void em_hud_render(EmGfx *gfx, const EmPlayerStatus *st);

#ifdef __cplusplus
}
#endif

#endif /* EM_HUD_H */
