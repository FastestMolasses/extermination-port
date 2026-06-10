/* em_hud.c — native STATUS SCREEN rendering (see em_hud.h for the
 * engine mapping and the Triangle-toggle faithfulness notes).
 *
 * Layout, on the 640x448 virtual canvas (PS2 NTSC frame; origin
 * top-left, y down), inside a 32-unit safe margin like the original
 * GS-sprite status overlay:
 *
 *   top-left      HEALTH bar (180x10): fill color lerps green -> red as
 *                 health drops; INFECTION bar (180x7, purple) below it.
 *   bottom-left   AMMO: one tick mark per magazine round (mag_max ticks,
 *                 lit = rounds left), with the RESERVE bar (amber)
 *                 underneath, normalized to EM_HUD_RESERVE_FULL.
 *   bottom-right  BATTERY: battery_max segments, lit = current charge
 *                 (the status screen's "04/06"), teal.
 *
 * Every element sits on a dark translucent backplate so it reads over
 * any scene. All quads go through em_gfx_overlay_rect; no text (no font
 * renderer yet — documented gap in em_hud.h). */
#include "game/em_hud.h"

#include <stdlib.h>

#include "em_input.h"   /* EM_PAD_TRIANGLE — the toggle button bit */

/* Safe-area margin and element metrics (canvas units). */
#define HUD_MARGIN     32.0f
#define HUD_PAD         2.0f   /* backplate border around each element */
#define HUD_BAR_W     180.0f
#define HUD_HP_H       10.0f
#define HUD_INF_H       7.0f
#define HUD_TICK_W      4.0f
#define HUD_TICK_H     12.0f
#define HUD_TICK_GAP    2.0f
#define HUD_RES_H       5.0f
#define HUD_SEG_W      18.0f
#define HUD_SEG_H      10.0f
#define HUD_SEG_GAP     4.0f

/* Reserve-bar display ceiling: 240 rounds = 8 full SPR4 magazines (the
 * engine's own 240 cap shows up in the 0x001418F0 refill tick —
 * FINDINGS.md; a display normalization, not an inventory limit). */
#define EM_HUD_RESERVE_FULL 240.0f

/* Full-screen scene dim while the status screen is up (the pause/status
 * look: the 3D frame keeps rendering — and the game keeps running, see
 * em_hud.h — but reads clearly as "menu over the world"). */
static const float kSceneDim[4]  = { 0.0f,  0.0f,  0.0f,  0.60f };
static const float kBackplate[4] = { 0.0f,  0.0f,  0.0f,  0.55f };
static const float kEmptySlot[4] = { 0.35f, 0.35f, 0.35f, 0.35f };
static const float kHpFull[4]    = { 0.15f, 0.85f, 0.20f, 0.90f };
static const float kHpEmpty[4]   = { 0.90f, 0.10f, 0.10f, 0.90f };
static const float kInfect[4]    = { 0.62f, 0.20f, 0.80f, 0.90f };
static const float kAmmoTick[4]  = { 0.95f, 0.90f, 0.55f, 0.95f };
static const float kReserve[4]   = { 0.90f, 0.65f, 0.20f, 0.90f };
static const float kBattery[4]   = { 0.25f, 0.80f, 0.95f, 0.90f };

static float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/* Backplate behind an element box (HUD_PAD border on every side). */
static void plate(EmGfx *gfx, float x, float y, float w, float h)
{
    em_gfx_overlay_rect(gfx, x - HUD_PAD, y - HUD_PAD,
                        w + 2.0f * HUD_PAD, h + 2.0f * HUD_PAD,
                        kBackplate);
}

/* Horizontal fill bar: dark track + `frac` of it in `color`. */
static void bar(EmGfx *gfx, float x, float y, float w, float h,
                float frac, const float color[4])
{
    plate(gfx, x, y, w, h);
    em_gfx_overlay_rect(gfx, x, y, w, h, kEmptySlot);
    frac = clamp01(frac);
    if (frac > 0.0f)
        em_gfx_overlay_rect(gfx, x, y, w * frac, h, color);
}

/* Status-screen visibility — hidden by default (the original shows no
 * persistent HUD), flipped by the Triangle edge in em_hud_update. */
static int s_shown = 0;

void em_hud_update(const EmFrameInput *in)
{
    if (in && (in->pressed & EM_PAD_TRIANGLE))
        s_shown = !s_shown;
}

/* EM_HUD_FORCE=1 — force the status screen visible (checked once; test
 * hook for headless overlay captures, see em_hud.h). */
static int hud_forced(void)
{
    static int force = -1;
    if (force < 0) {
        const char *e = getenv("EM_HUD_FORCE");
        force = (e && e[0] == '1') ? 1 : 0;
    }
    return force;
}

int em_hud_visible(void)
{
    return s_shown || hud_forced();
}

void em_hud_render(EmGfx *gfx, const EmPlayerStatus *st)
{
    /* Hidden (the default): queue NOTHING — the frame is byte-identical
     * to a build with no status screen at all. */
    if (!em_hud_visible() || !gfx || !st) return;

    /* Scene dim — the full-screen translucent backdrop behind the
     * status elements (queued first, so everything below draws over it). */
    em_gfx_overlay_rect(gfx, 0.0f, 0.0f, EM_GFX_OVERLAY_W,
                        EM_GFX_OVERLAY_H, kSceneDim);

    /* HEALTH — green at full, red at empty (linear color lerp). */
    {
        float frac = (st->health_max > 0.0f)
                         ? clamp01(st->health / st->health_max) : 0.0f;
        float col[4];
        for (int i = 0; i < 4; i++)
            col[i] = kHpEmpty[i] + (kHpFull[i] - kHpEmpty[i]) * frac;
        bar(gfx, HUD_MARGIN, HUD_MARGIN, HUD_BAR_W, HUD_HP_H, frac, col);
    }

    /* INFECTION — purple, percent of 100. */
    bar(gfx, HUD_MARGIN, HUD_MARGIN + HUD_HP_H + 2.0f * HUD_PAD + 4.0f,
        HUD_BAR_W, HUD_INF_H, st->infection / 100.0f, kInfect);

    /* AMMO — magazine tick marks + reserve bar, bottom-left. */
    if (st->mag_max) {
        float ticks_w = st->mag_max * (HUD_TICK_W + HUD_TICK_GAP)
                        - HUD_TICK_GAP;
        float res_y   = EM_GFX_OVERLAY_H - HUD_MARGIN - HUD_RES_H;
        float tick_y  = res_y - 2.0f * HUD_PAD - 4.0f - HUD_TICK_H;
        plate(gfx, HUD_MARGIN, tick_y, ticks_w, HUD_TICK_H);
        for (uint8_t i = 0; i < st->mag_max; i++)
            em_gfx_overlay_rect(gfx,
                                HUD_MARGIN + i * (HUD_TICK_W + HUD_TICK_GAP),
                                tick_y, HUD_TICK_W, HUD_TICK_H,
                                (i < st->mag) ? kAmmoTick : kEmptySlot);
        float res = (st->reserve > 0)
                        ? (float)st->reserve / EM_HUD_RESERVE_FULL : 0.0f;
        bar(gfx, HUD_MARGIN, res_y, ticks_w, HUD_RES_H, res, kReserve);
    }

    /* BATTERY — segments, bottom-right, right-aligned ("04/06"). */
    if (st->battery_max) {
        float segs_w = st->battery_max * (HUD_SEG_W + HUD_SEG_GAP)
                       - HUD_SEG_GAP;
        float x0     = EM_GFX_OVERLAY_W - HUD_MARGIN - segs_w;
        float y0     = EM_GFX_OVERLAY_H - HUD_MARGIN - HUD_SEG_H;
        plate(gfx, x0, y0, segs_w, HUD_SEG_H);
        for (uint8_t i = 0; i < st->battery_max; i++)
            em_gfx_overlay_rect(gfx, x0 + i * (HUD_SEG_W + HUD_SEG_GAP),
                                y0, HUD_SEG_W, HUD_SEG_H,
                                (i < st->battery) ? kBattery : kEmptySlot);
    }
}
