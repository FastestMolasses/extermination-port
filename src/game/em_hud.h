/* em_hud.h — native STATUS SCREEN: the Triangle-toggled player status
 * display.
 *
 * FAITHFULNESS (FINDINGS.md "INVENTORY LOCATED" / battery section, live
 * captures 2026-06-10): the original game shows NO persistent HUD during
 * play. The status display ("04/06 SPR4", bullet-icon count, battery
 * bar, health, infection) is a STATUS OVERLAY toggled with TRIANGLE.
 * The port mirrors that: hidden by default, em_hud_update() flips
 * visibility on a Triangle press (edge-triggered), and while shown the
 * 3D scene behind it is dimmed by a full-screen dark translucent overlay
 * rect, like a pause/status screen. ASSUMPTION (stated, from the live
 * pad-injection captures): the engine's status overlay does NOT
 * hard-pause the game — gameplay keeps running underneath; the port
 * keeps simulating too.
 *
 * Rendering goes through the em_gfx 2D overlay pass (em_gfx_overlay_rect
 * — solid alpha-blended quads on the 640x448 virtual canvas).
 * EmPlayerStatus mirrors the engine's canonical status storage:
 *
 *   health     player actor +0x220 (0x008104D0), float — "75/100"
 *   infection  player actor +0x228 (0x008104D8), float — "60%"
 *   mag        D_00810C62, u8 — rounds in the current SPR4 magazine (max 30)
 *   reserve    D_00810CB4, s16 — SPR4 reserve rounds (the "120")
 *   battery    the status screen's "04/06" pair (current/max); engine
 *              storage address not yet located (FINDINGS open item)
 *
 * Style is PS2-era minimal: bars, tick marks and segments only — NO TEXT.
 * Known gap: the port has no font/glyph renderer yet, so the numeric
 * readouts of the original status overlay ("75/100", "120", "04/06")
 * have no native equivalent until one lands; the bar geometry carries
 * the same information graphically.
 *
 * EM_HUD_FORCE=1 in the environment (checked once, on first render call)
 * forces the status screen VISIBLE regardless of the toggle — for
 * headless capture tests of the overlay itself. (The old EM_NO_HUD knob
 * is gone: hidden is now the default, so the default frame is already
 * the HUD-free one.)
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
    uint8_t mag;          /* D_00810C62 — rounds in the magazine */
    uint8_t mag_max;      /* magazine capacity (SPR4: 30) */
    int16_t reserve;      /* D_00810CB4 — reserve rounds */
    uint8_t battery;      /* status-screen battery, current segments */
    uint8_t battery_max;  /* status-screen battery, total segments */
} EmPlayerStatus;

/* Per-frame toggle poll: a TRIANGLE press (edge, in->pressed) flips the
 * status screen's visibility. Call once per gameplay frame with the
 * frame input block. */
void em_hud_update(const EmFrameInput *in);

/* Is the status screen currently shown? (toggle state OR EM_HUD_FORCE) */
int em_hud_visible(void);

/* Queue this frame's status screen into the overlay pass — the dim
 * backdrop first, then the status elements. Call once per gameplay
 * frame, between em_gfx_begin_frame and em_gfx_end_frame, after the 3D
 * draws are recorded (the overlay flushes last regardless). No-op while
 * hidden (the default) or st is NULL — a hidden status screen queues
 * NOTHING, so the default frame is byte-identical to a HUD-free build. */
void em_hud_render(EmGfx *gfx, const EmPlayerStatus *st);

#ifdef __cplusplus
}
#endif

#endif /* EM_HUD_H */
