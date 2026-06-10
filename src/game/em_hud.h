/* em_hud.h — native HUD: the player status display over the 3D frame.
 *
 * Renders the game's status data through the em_gfx 2D overlay pass
 * (em_gfx_overlay_rect — solid alpha-blended quads on the 640x448
 * virtual canvas). EmPlayerStatus mirrors the engine's canonical status
 * storage, located live in the decomp repo (FINDINGS.md "INVENTORY
 * LOCATED", 2026-06-10):
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
 * EM_NO_HUD=1 in the environment disables rendering entirely (checked
 * once, on first call): with the HUD off no overlay rect is ever queued,
 * so frame output is bit-identical to pre-HUD builds.
 */
#ifndef EM_HUD_H
#define EM_HUD_H

#include <stdint.h>

#include "em_gfx.h"

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

/* Queue this frame's HUD into the overlay pass. Call once per gameplay
 * frame, between em_gfx_begin_frame and em_gfx_end_frame, after the 3D
 * draws are recorded (the overlay flushes last regardless). No-op when
 * EM_NO_HUD=1 or st is NULL. */
void em_hud_render(EmGfx *gfx, const EmPlayerStatus *st);

#ifdef __cplusplus
}
#endif

#endif /* EM_HUD_H */
