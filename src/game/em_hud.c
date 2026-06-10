/* em_hud.c — native STATUS SCREEN rendering (see em_hud.h for the
 * engine mapping and the faithfulness notes).
 *
 * Composition follows the decoded draw chain of the real screen
 * (FINDINGS.md "STATUS SCREEN LAYOUT", session 25) on the engine's own
 * 512x448 UI canvas (origin top-left, y down):
 *
 *   (208,197)  HEALTH ring gauge — bg ring r36-56 (yellow-green/orange
 *              radial gradient; red pair when health <= 35), light-blue
 *              fill arc r24-56 sweeping 360*hp/100 deg from 180, and the
 *              rotating 120-deg highlight (two 60-deg gradient arcs,
 *              12 deg per 2 frames = one revolution per second).
 *   (16,118)   BATTERY block — 8x8 squares, one per internal HALF-unit,
 *              12 per row, right-to-left from x=104 at y=134, per-square
 *              magenta->yellow gradient steps, -1 px stagger on even
 *              columns. Gated on battery_max != 0 (engine: 0x810C7F).
 *   (16,190)   SPR4 block — reserve count only (the real screen shows NO
 *              magazine display; the old tick marks + /240 reserve bar
 *              were port inventions and are gone).
 *   (296,260)  INFECTION — text only on the real screen (there is no
 *              bar); placeholder blocks until a font renderer lands.
 *   (128,336)  help panel — translucent gray rect to (384,432), exact.
 *
 * TEXT renders through the REAL game fonts when assets/font.emfn is
 * present (see the "UI font" block below): every label/number ("HEALTH",
 * "075 / 100", "04/06", "INFECTION 60%", reserve count) draws as
 * textured overlay glyphs in the engine's metrics and style colors.
 * Without the asset each run falls back to the old positioned
 * PLACEHOLDER rect at the real glyph-cell metrics — no regression. The
 * 8x8 blue block markers ARE the real elements (solid GS sprites) and
 * render faithfully either way.
 *
 * STAND-INS (flagged, see em_hud.h): the scene-dim rect stands in for
 * the engine's UI-camera swap (identity camera + rotating player model
 * inside the ring); the rotating highlight uses standard alpha blend
 * toward white as the stand-in for the engine's blend-mode-1 additive
 * pass (the overlay pipeline is alpha-blend only).
 *
 * All primitives go through the em_gfx overlay pass (rects + the
 * em_gfx_overlay_arc4 annular-arc primitive — the translation of the
 * engine's 0x60-block arc func_002082B0). */
#include "game/em_hud.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "em_input.h"   /* EM_PAD_TRIANGLE / EM_PAD_START — toggle bits */

/* GS color -> float rgba: components are /255; GS alpha 0x80 = 1.0. */
#define GS(r, g, b, a) { (r) / 255.0f, (g) / 255.0f, (b) / 255.0f, \
                         (a) / 128.0f }

/* Block-marker blue (the screen's 8x8 element markers / name color). */
static const float kMarkerBlue[4]  = GS(0, 96, 206, 128);

/* HEALTH ring colors (FINDINGS: blocks 0x2653F0 / 0x265390). The bg-ring
 * pair is applied inner->outer (radial) — the 0x60 block carries 4
 * gradient slots and the corner assignment is unverified without a pixel
 * capture; radial is the documented assumption. */
static const float kRingNormA[4]   = GS(192, 224,  0, 128); /* yellow-green */
static const float kRingNormB[4]   = GS(224, 128, 24, 128); /* orange */
static const float kRingLowA[4]    = GS(160,   0,  0, 128); /* red pair */
static const float kRingLowB[4]    = GS(192,   0,  0, 128);
static const float kRingFill[4]    = GS(  0, 153, 255, 128); /* light blue */

/* Rotating-highlight stand-in: the engine draws transparent<->(80,80,80)
 * arcs in additive blend mode 1; with the alpha-blend-only overlay pass
 * the closest brightening approximation is white at alpha 80/255. */
static const float kHiliteOn[4]    = { 1.0f, 1.0f, 1.0f, 80.0f / 255.0f };
static const float kHiliteOff[4]   = { 1.0f, 1.0f, 1.0f, 0.0f };

/* BATTERY square gradient endpoints (computed per-square in 1/12 steps
 * by the engine, scratchpad 0x700038A0). */
static const float kBattMagenta[4] = GS(163,  54, 160, 128);
static const float kBattYellow[4]  = GS(255, 230,  52, 128);

/* Help panel (64,64,64,0x40) — exact. */
static const float kHelpPanel[4]   = GS(64, 64, 64, 64);

/* Text-style colors (8-byte style records at 0x265510..) — used at
 * PLACEHOLDER_ALPHA for the unrenderable text blocks. */
#define PLACEHOLDER_ALPHA 0.22f
static const float kTextWhite[3]   = { 1.0f, 1.0f, 1.0f };     /* 0x265510 */
static const float kTextRed[3]     = { 1.0f, 0.0f, 0.0f };     /* 0x265528 */
static const float kTextDarkRed[3] = { 96.0f / 255.0f, 8.0f / 255.0f,
                                       16.0f / 255.0f };       /* 0x265520 */

/* Scene dim — the documented STAND-IN for the engine's UI-camera swap
 * (the real screen replaces the world view with an identity-camera
 * scene: rotating player model inside the ring). Until the port can
 * re-camera the 3D pass, the dim keeps the "menu over the world" read. */
static const float kSceneDim[4]    = { 0.0f, 0.0f, 0.0f, 0.60f };

static float clamp01f(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/* --- UI font (assets/font.emfn) --------------------------------------
 *
 * The engine has no VRAM-resident font sheet: its text functions stream
 * 1bpp glyphs from EE RAM (tall 12x20, 30 B/glyph; small 16x16,
 * 32 B/glyph; pointer block 0x0028A490) through a nibble->PSMT4 LUT into
 * a 512-wide strip at GS block 0x1B00, then draw ONE batched sprite —
 * FINDINGS.md "UI FONT". The port pre-bakes those same glyphs into one
 * RGBA8 sheet (the decomp repo's tools/export_font.py, run by the user
 * against their own EE-RAM dump) and draws per-glyph textured overlay
 * quads with the strip's GS state (bilinear + modulate + alpha blend).
 *
 * .emfn v1 layout (little-endian; must match export_font.py):
 *   0   "EMFN"                      16  u32 font_count (= 2)
 *   4   u32 version (= 1)           20  u32 glyph_count
 *   8   u32 sheet_w                 24  font_count * { u32 first, count,
 *   12  u32 sheet_h                                    cell_w, cell_h }
 *   then glyph_count * { u16 u, v; u8 w, h, advance, pad }
 *   then sheet_w * sheet_h * 4 RGBA8 texels (rows top-down)
 * Font 0 = tall (12x20, proportional advance), font 1 = small (16x16,
 * fixed cell — the engine GS-scales it to the caller's cell). */
#define FONT_TALL  0
#define FONT_SMALL 1

typedef struct { uint16_t u, v; uint8_t w, h, advance, pad; } FontGlyph;
typedef struct { uint32_t first, count, cell_w, cell_h; }     FontFace;

static struct {
    int        state;       /* 0 = untried, 1 = parsed, -1 = unavailable */
    int        registered;  /* sheet handed to em_gfx_overlay_texture_set */
    FontFace   face[2];
    FontGlyph *glyphs;
    uint32_t   glyph_count;
    uint8_t   *sheet;       /* RGBA8, freed after GPU registration */
    uint32_t   sheet_w, sheet_h;
} s_font;

static uint32_t font_u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void font_parse(void)
{
    if (s_font.state) return;
    s_font.state = -1;                       /* sticky failure default */
    FILE *f = fopen("assets/font.emfn", "rb");
    if (!f) return;                          /* missing = placeholder mode */
    uint8_t hdr[24];
    if (fread(hdr, 1, 24, f) != 24 || memcmp(hdr, "EMFN", 4) != 0 ||
        font_u32(hdr + 4) != 1 || font_u32(hdr + 16) != 2) {
        fprintf(stderr, "hud: assets/font.emfn: bad header\n");
        fclose(f);
        return;
    }
    s_font.sheet_w     = font_u32(hdr + 8);
    s_font.sheet_h     = font_u32(hdr + 12);
    s_font.glyph_count = font_u32(hdr + 20);
    if (!s_font.sheet_w || !s_font.sheet_h || !s_font.glyph_count ||
        s_font.sheet_w > 4096 || s_font.sheet_h > 4096 ||
        s_font.glyph_count > 4096) {
        fprintf(stderr, "hud: assets/font.emfn: implausible sizes\n");
        fclose(f);
        return;
    }
    uint8_t face[2][16];
    size_t  glyph_bytes = (size_t)s_font.glyph_count * 8;
    size_t  sheet_bytes = (size_t)s_font.sheet_w * s_font.sheet_h * 4;
    uint8_t *graw = (uint8_t *)malloc(glyph_bytes);
    s_font.glyphs = (FontGlyph *)calloc(s_font.glyph_count,
                                        sizeof(FontGlyph));
    s_font.sheet  = (uint8_t *)malloc(sheet_bytes);
    int ok = graw && s_font.glyphs && s_font.sheet &&
             fread(face, 1, 32, f) == 32 &&
             fread(graw, 1, glyph_bytes, f) == glyph_bytes &&
             fread(s_font.sheet, 1, sheet_bytes, f) == sheet_bytes;
    fclose(f);
    if (ok) {
        for (int i = 0; i < 2; i++) {
            s_font.face[i].first  = font_u32(face[i]);
            s_font.face[i].count  = font_u32(face[i] + 4);
            s_font.face[i].cell_w = font_u32(face[i] + 8);
            s_font.face[i].cell_h = font_u32(face[i] + 12);
            if (s_font.face[i].first + s_font.face[i].count >
                s_font.glyph_count)
                ok = 0;
        }
    }
    if (ok) {
        for (uint32_t i = 0; i < s_font.glyph_count; i++) {
            const uint8_t *p = graw + (size_t)i * 8;
            s_font.glyphs[i].u       = (uint16_t)(p[0] | p[1] << 8);
            s_font.glyphs[i].v       = (uint16_t)(p[2] | p[3] << 8);
            s_font.glyphs[i].w       = p[4];
            s_font.glyphs[i].h       = p[5];
            s_font.glyphs[i].advance = p[6];
        }
        s_font.state = 1;
        fprintf(stderr, "hud: font sheet %ux%u, %u glyphs "
                "(%u tall + %u small)\n", s_font.sheet_w, s_font.sheet_h,
                s_font.glyph_count, s_font.face[0].count,
                s_font.face[1].count);
    } else if (graw) {   /* short read / inconsistent tables */
        fprintf(stderr, "hud: assets/font.emfn: truncated/inconsistent\n");
    }
    free(graw);
    if (s_font.state != 1) {
        free(s_font.glyphs); s_font.glyphs = NULL;
        free(s_font.sheet);  s_font.sheet  = NULL;
    }
}

/* Parse the asset and (once a gfx exists) register the sheet as the
 * overlay texture. Returns 1 when text can draw. */
static int font_ensure(EmGfx *gfx)
{
    font_parse();
    if (s_font.state != 1) return 0;
    if (!s_font.registered) {
        if (!gfx || !em_gfx_overlay_texture_set(gfx, s_font.sheet,
                                                s_font.sheet_w,
                                                s_font.sheet_h))
            return 0;
        s_font.registered = 1;
        free(s_font.sheet);          /* GPU owns a copy now */
        s_font.sheet = NULL;
    }
    return 1;
}

int em_hud_font_ready(void)
{
    font_parse();
    return s_font.state == 1;
}

/* Style table — font face + glyph cell + engine style color (8-byte
 * records 0x265510..; GS 0x80 = full intensity). The 12 px label and
 * 12x16 number cells are the status screen's func_001CBA50 parameters
 * (the small font's 16x16 texels GS-scale to the cell). */
static const struct {
    int   face;
    float cell_w, cell_h;
    float rgba[4];
} kTextStyles[] = {
    [EM_HUD_TEXT_LABEL12]      = { FONT_SMALL, 12.0f, 12.0f,
                                   { 1.0f, 1.0f, 1.0f, 1.0f } },
    [EM_HUD_TEXT_NUM16]        = { FONT_SMALL, 12.0f, 16.0f,
                                   { 1.0f, 1.0f, 1.0f, 1.0f } },
    [EM_HUD_TEXT_NUM16_RED]    = { FONT_SMALL, 12.0f, 16.0f,
                                   { 1.0f, 0.0f, 0.0f, 1.0f } },
    [EM_HUD_TEXT_TALL]         = { FONT_TALL,  0.0f, 20.0f,
                                   { 1.0f, 1.0f, 1.0f, 1.0f } },
    [EM_HUD_TEXT_TALL_DARKRED] = { FONT_TALL,  0.0f, 20.0f,
                                   { 96.0f / 128.0f, 8.0f / 128.0f,
                                     16.0f / 128.0f, 1.0f } },
};

/* Engine glyph rule: index = ascii - 0x20; '$' remaps to the extended
 * glyph 0x89 (tall, func_001CC1E0) / 0x87 (small, func_001CBA50);
 * control chars draw blank. Returns NULL for unmapped indices. */
static const FontGlyph *font_glyph(int face, unsigned char c)
{
    uint32_t idx;
    if (c == '$')      idx = (face == FONT_TALL) ? 0x89u : 0x87u;
    else if (c < 0x20) return NULL;
    else               idx = (uint32_t)c - 0x20u;
    if (idx >= s_font.face[face].count) return NULL;
    return &s_font.glyphs[s_font.face[face].first + idx];
}

void em_hud_text(EmGfx *gfx, float x, float y, const char *str,
                 EmHudTextStyle style)
{
    if (!str || !font_ensure(gfx)) return;
    int   face   = kTextStyles[style].face;
    float cell_w = kTextStyles[style].cell_w;
    float cell_h = kTextStyles[style].cell_h;
    for (; *str; str++) {
        unsigned char    c = (unsigned char)*str;
        const FontGlyph *g = font_glyph(face, c);
        if (face == FONT_TALL) {
            /* tall: 1:1 texel:pixel, proportional advance; the strip
             * overwrite rule means each glyph shows exactly its first
             * `advance` texel columns. Control chars advance 0 (the
             * engine resets both glyph and advance for c < 0x20). */
            if (c < 0x20) continue;
            float adv = g ? (float)g->advance : 9.0f;
            if (g)
                em_gfx_overlay_glyph(gfx, x, y, adv, cell_h,
                                     g->u, g->v,
                                     g->u + adv, g->v + g->h,
                                     kTextStyles[style].rgba);
            x += adv;
        } else {
            /* small: full 16x16 texel cell GS-scaled to the style cell;
             * advance = cell width (the engine's glyph_w parameter). */
            if (g)
                em_gfx_overlay_glyph(gfx, x, y, cell_w, cell_h,
                                     g->u, g->v,
                                     g->u + g->w, g->v + g->h,
                                     kTextStyles[style].rgba);
            x += cell_w;
        }
    }
}

float em_hud_text_width(const char *str, EmHudTextStyle style)
{
    if (!str || !em_hud_font_ready()) return 0.0f;
    int   face = kTextStyles[style].face;
    float w    = 0.0f;
    for (; *str; str++) {
        unsigned char c = (unsigned char)*str;
        if (face == FONT_TALL) {
            if (c < 0x20) continue;    /* func_001CC170 skips them */
            const FontGlyph *g = font_glyph(face, c);
            w += g ? (float)g->advance : 9.0f;
        } else {
            w += kTextStyles[style].cell_w;
        }
    }
    return w;
}

/* Placeholder block for an unrenderable text run: `glyphs` cells of
 * `cell_w` x `cell_h` at (x, y), in the real style color at low alpha —
 * marks the position/extent without pretending to be text. */
static void text_placeholder(EmGfx *gfx, float x, float y, int glyphs,
                             float cell_w, float cell_h, const float rgb[3])
{
    const float c[4] = { rgb[0], rgb[1], rgb[2], PLACEHOLDER_ALPHA };
    em_gfx_overlay_rect(gfx, x, y, glyphs * cell_w, cell_h, c);
}

/* The 8x8 blue block marker (a real, solid element — not a placeholder). */
static void marker(EmGfx *gfx, float x, float y)
{
    em_gfx_overlay_rect(gfx, x, y, 8.0f, 8.0f, kMarkerBlue);
}

static int decimal_digits(int v)
{
    int n = 1;
    if (v < 0) v = 0;
    while (v >= 10) { v /= 10; n++; }
    return n;
}

/* Status-screen visibility — hidden by default (the original shows no
 * persistent HUD), flipped by a Triangle OR Start edge (both buttons
 * open the same screen — verified identical memory diff, FINDINGS). */
static int s_shown = 0;

/* Display copies of health/infection (the engine's 0x810858/0x81085C):
 * step +-1 per frame toward the player-actor targets — the screen's
 * count-up animation. Sentinel < 0 = snap to target on first sight. */
static float s_disp_health    = -1.0f;
static float s_disp_infection = -1.0f;

/* Frame counter while visible — drives the highlight rotation (6 deg per
 * frame = the engine's 12 deg per 2 frames = 1 s per revolution). */
static uint32_t s_frames = 0;

void em_hud_update(const EmFrameInput *in)
{
    if (in && (in->pressed & (EM_PAD_TRIANGLE | EM_PAD_START)))
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

/* Step a display copy +-1/frame toward `target` (engine count-up). */
static float count_up(float *disp, float target)
{
    if (*disp < 0.0f)                *disp = target;
    else if (*disp < target - 1.0f)  *disp += 1.0f;
    else if (*disp > target + 1.0f)  *disp -= 1.0f;
    else                             *disp = target;
    return *disp;
}

/* HEALTH ring gauge at its real anchor (engine func_00208AD0 with
 * cx=208; ring center y 197, label row y 118, value row y 262). */
static void health_gauge(EmGfx *gfx, float hp, float hp_max)
{
    const float cx = 208.0f, cy = 197.0f;

    /* label: "HEALTH" centered on x=208 at y=118, marker 12 px left.
     * Engine quirk kept (func_00208AD0): the centering width comes from
     * the TALL-font width helper func_001CC170 (proportional — 54 px
     * for "HEALTH") while the label draws in the SMALL font at 12x12
     * cells (72 px), so the drawn text sits a little right of true
     * center, exactly like the original. */
    if (em_hud_font_ready()) {
        const float label_x =
            cx - em_hud_text_width("HEALTH", EM_HUD_TEXT_TALL) * 0.5f;
        marker(gfx, label_x - 12.0f, 122.0f);
        em_hud_text(gfx, label_x, 118.0f, "HEALTH", EM_HUD_TEXT_LABEL12);
    } else {
        const float label_x = cx - (6 * 12.0f) * 0.5f;
        marker(gfx, label_x - 12.0f, 122.0f);
        text_placeholder(gfx, label_x, 118.0f, 6, 12.0f, 12.0f,
                         kTextWhite);
    }

    /* background ring r36-56, full circle (the engine passes 180..540);
     * red pair when health <= 35. */
    const float *ca = (hp <= 35.0f) ? kRingLowA : kRingNormA;
    const float *cb = (hp <= 35.0f) ? kRingLowB : kRingNormB;
    em_gfx_overlay_arc4(gfx, cx, cy, 36.0f, 56.0f, 180.0f, 540.0f,
                        ca, cb, ca, cb);

    /* fill arc r24-56: sweep = 360 * hp/100 starting at 180 deg. */
    float frac = (hp_max > 0.0f) ? clamp01f(hp / hp_max) : 0.0f;
    if (frac > 0.0f)
        em_gfx_overlay_arc(gfx, cx, cy, 24.0f, 56.0f,
                           180.0f, 180.0f + 360.0f * frac, kRingFill);

    /* rotating 120-deg highlight: two 60-deg arcs r36-56, gradient
     * transparent -> bright -> transparent, advancing 6 deg/frame. */
    float a = 180.0f + 6.0f * (float)(s_frames % 60u);
    em_gfx_overlay_arc4(gfx, cx, cy, 36.0f, 56.0f, a, a + 60.0f,
                        kHiliteOff, kHiliteOff, kHiliteOn, kHiliteOn);
    em_gfx_overlay_arc4(gfx, cx, cy, 36.0f, 56.0f, a + 60.0f, a + 120.0f,
                        kHiliteOn, kHiliteOn, kHiliteOff, kHiliteOff);

    /* value row "075 / 100" 16 px at y=262: value x=166, "/" x=202,
     * max x=214 (the given x steps imply a 12 px advance for the 16 px
     * number font); value 3 digits zero-padded (func_001C5FB0 digits=3,
     * no trim), red style when health <= 60. */
    if (em_hud_font_ready()) {
        char buf[8];
        int  v = (int)hp;
        snprintf(buf, sizeof buf, "%03d", v < 0 ? 0 : v);
        em_hud_text(gfx, 166.0f, 262.0f, buf,
                    hp <= 60.0f ? EM_HUD_TEXT_NUM16_RED
                                : EM_HUD_TEXT_NUM16);
        em_hud_text(gfx, 202.0f, 262.0f, "/", EM_HUD_TEXT_NUM16);
        snprintf(buf, sizeof buf, "%d", (int)hp_max);
        em_hud_text(gfx, 214.0f, 262.0f, buf, EM_HUD_TEXT_NUM16);
    } else {
        const float *style = (hp <= 60.0f) ? kTextRed : kTextWhite;
        text_placeholder(gfx, 166.0f, 262.0f, 3, 12.0f, 16.0f, style);
        text_placeholder(gfx, 202.0f, 262.0f, 1, 12.0f, 16.0f,
                         kTextWhite);
        text_placeholder(gfx, 214.0f, 262.0f, 3, 12.0f, 16.0f,
                         kTextWhite);
    }
}

/* BATTERY block at (16,118) (engine func_00209280, mode 0). */
static void battery_block(EmGfx *gfx, uint8_t cur, uint8_t max)
{
    if (!max) return;   /* engine gate 0x810C7F == 0: block hidden */

    marker(gfx, 16.0f, 120.0f);
    if (em_hud_font_ready())
        em_hud_text(gfx, 28.0f, 118.0f, "BATTERY", EM_HUD_TEXT_LABEL12);
    else
        text_placeholder(gfx, 28.0f, 118.0f, 7, 12.0f, 12.0f, kTextWhite);

    /* segment bar: one 8x8 square per internal HALF-unit (storage
     * 0x810CB2 holds half-units; the EmPlayerStatus fields are the
     * displayed units = half-units >> 1, so squares = cur * 2), 12 per
     * row wrapping below, drawn right-to-left from right edge x=104 at
     * y=134, even columns staggered -1 px, color stepping
     * magenta -> yellow in 1/12 increments along the row. */
    int half_units = (int)cur * 2;
    for (int i = 0; i < half_units; i++) {
        int   col = i % 12, row = i / 12;
        float t   = (float)col / 12.0f;
        float c[4];
        for (int k = 0; k < 4; k++)
            c[k] = kBattMagenta[k] + (kBattYellow[k] - kBattMagenta[k]) * t;
        float x = 104.0f - 8.0f * (float)(col + 1)
                  - ((col % 2) == 0 ? 1.0f : 0.0f);
        em_gfx_overlay_rect(gfx, x, 134.0f + 8.0f * (float)row,
                            8.0f, 8.0f, c);
    }

    /* "04/06" 16 px at (32,170): 2-digit zero-padded current + '/' +
     * 2-digit max (the engine formats each side with digits=2). */
    if (em_hud_font_ready()) {
        char buf[12];
        snprintf(buf, sizeof buf, "%02u/%02u", (unsigned)cur,
                 (unsigned)max);
        em_hud_text(gfx, 32.0f, 170.0f, buf, EM_HUD_TEXT_NUM16);
    } else {
        int dd = decimal_digits(cur) > 2 ? decimal_digits(cur) : 2;
        text_placeholder(gfx, 32.0f, 170.0f, dd + 1 + 2, 12.0f, 16.0f,
                         kTextWhite);
    }
}

/* SPR4 block at (16,190) (engine func_00209860): reserve count ONLY —
 * the real screen shows no magazine state at all. */
static void spr4_block(EmGfx *gfx, int16_t reserve)
{
    marker(gfx, 16.0f, 192.0f);
    if (em_hud_font_ready())
        em_hud_text(gfx, 28.0f, 190.0f, "SPR4", EM_HUD_TEXT_LABEL12);
    else
        text_placeholder(gfx, 28.0f, 190.0f, 4, 12.0f, 12.0f, kTextWhite);

    /* bullet icon 24x24 at (16,262) — texture unresolved (needs a VRAM
     * capture); neutral placeholder block. */
    text_placeholder(gfx, 16.0f, 262.0f, 1, 24.0f, 24.0f, kTextWhite);

    /* reserve count, 16 px digits at (42,266), 4 digits trimmed (no
     * leading zeros — func_001C5FB0 digits=4 with trim). */
    if (em_hud_font_ready()) {
        char buf[8];
        int  v = reserve;
        if (v < 0)    v = 0;
        if (v > 9999) v = 9999;
        snprintf(buf, sizeof buf, "%d", v);
        em_hud_text(gfx, 42.0f, 266.0f, buf, EM_HUD_TEXT_NUM16);
    } else {
        int digits = decimal_digits(reserve);
        if (digits > 4) digits = 4;
        text_placeholder(gfx, 42.0f, 266.0f, digits, 12.0f, 16.0f,
                         kTextWhite);
    }
}

/* INFECTION — text only (the real screen has NO infection bar): either
 * "INFECTED" tall-font dark red at (290,260) when at 100, or the
 * "INFECTION" label (296,260) + "NN%" value (296,288). Placeholder
 * blocks at the real tall-font (10x20) / number (16 px) metrics. */
static void infection_block(EmGfx *gfx, float infection)
{
    if (infection >= 100.0f) {
        if (em_hud_font_ready())
            em_hud_text(gfx, 290.0f, 260.0f, "INFECTED",
                        EM_HUD_TEXT_TALL_DARKRED);
        else
            text_placeholder(gfx, 290.0f, 260.0f, 8, 10.0f, 20.0f,
                             kTextDarkRed);
        return;
    }
    if (em_hud_font_ready()) {
        char buf[8];
        em_hud_text(gfx, 296.0f, 260.0f, "INFECTION", EM_HUD_TEXT_TALL);
        snprintf(buf, sizeof buf, "%d%%", (int)infection);
        em_hud_text(gfx, 296.0f, 288.0f, buf, EM_HUD_TEXT_NUM16);
    } else {
        text_placeholder(gfx, 296.0f, 260.0f, 9, 10.0f, 20.0f,
                         kTextWhite);
        int digits = decimal_digits((int)infection);
        text_placeholder(gfx, 296.0f, 288.0f, digits + 1, 12.0f, 16.0f,
                         kTextWhite);   /* "NN" + "%" */
    }
}

void em_hud_render(EmGfx *gfx, const EmPlayerStatus *st)
{
    /* Hidden (the default): queue NOTHING — the frame is byte-identical
     * to a build with no status screen at all. */
    if (!em_hud_visible() || !gfx || !st) return;

    /* Everything below lays out on the status screen's own 512x448
     * canvas (GS 0x700/0x790 offsets); restored before returning so
     * later overlay callers (screen fade) keep the 640x448 default. */
    em_gfx_overlay_canvas(gfx, EM_GFX_STATUS_W, EM_GFX_STATUS_H);

    /* Scene dim — STAND-IN for the engine's UI-camera swap (see top). */
    em_gfx_overlay_rect(gfx, 0.0f, 0.0f, EM_GFX_STATUS_W,
                        EM_GFX_STATUS_H, kSceneDim);

    /* Count-up display copies (0x810858/0x81085C semantics). */
    float hp  = count_up(&s_disp_health, st->health);
    float inf = count_up(&s_disp_infection, st->infection);

    /* Help panel (128,336)-(384,432) — exact (the per-page help text on
     * it is unrenderable without a font). */
    em_gfx_overlay_rect(gfx, 128.0f, 336.0f, 256.0f, 96.0f, kHelpPanel);

    battery_block(gfx, st->battery, st->battery_max);
    spr4_block(gfx, st->reserve);
    health_gauge(gfx, hp, st->health_max);
    infection_block(gfx, inf);

    s_frames++;

    em_gfx_overlay_canvas(gfx, EM_GFX_OVERLAY_W, EM_GFX_OVERLAY_H);
}
