/* em_hud.c — native STATUS SCREEN rendering (see em_hud.h for the
 * engine mapping and the faithfulness notes).
 *
 * Composition follows the decoded draw chain of the real screen on the
 * engine's own 512x448 UI canvas (origin top-left, y down).  Every
 * anchor below was re-derived (2026-07 audit) from the recovered C of
 * the hub drawer func_00209DF0 (BYTE-MATCHED) and the three block
 * drawers it calls — func_00208AD0 (BYTE-MATCHED), func_00209280 and
 * func_00209860 (both NEARMISS, body-correct) — using their own
 * canvas->GS mapping (x + 0x700, (y >> 1) + 0x790).  FINDINGS.md
 * "STATUS SCREEN LAYOUT" (session 25) is the narrative write-up; the
 * numbers here come from the functions.
 *
 *   (208,196)  HEALTH ring gauge — TWO annuli, one per arc block:
 *              the TRACK r24-56 light blue (block D_00265390, drawn at
 *              180..540) and the coloured ring r36-56 over it
 *              (block D_002653F0, yellow-green/orange radial gradient;
 *              red pair when health <= 35).  Then the rotating 120-deg
 *              highlight (two 60-deg gradient arcs, 12 deg per 2 frames
 *              = one revolution per second), and last the DEPLETED arc
 *              — the TRACK block again over 180+3.6*hp .. 540, masking
 *              the coloured ring where health is missing (it shrinks to
 *              nothing at full health).  Anchors, angles and the block
 *              split all re-read from the byte-matched func_00208AD0 /
 *              func_00209DF0 call pair; the blocks' static radii and the
 *              track colour are FINDINGS item 7's VRAM capture.
 *   (16,118)   BATTERY block — 8x8 squares, one per internal HALF-unit,
 *              12 per row, right-to-left from x=104 at y=134, per-square
 *              magenta->yellow gradient steps, -1 px stagger on even
 *              columns. Gated on battery_max != 0 (engine: 0x810C7F).
 *   (16,190)   SPR4 block — reserve count only (the real screen shows NO
 *              magazine display; the old tick marks + /240 reserve bar
 *              were port inventions and are gone).
 *   (296,260)  INFECTION — text only on the real screen (there is no
 *              bar); label + "NN%" value at (296,288), or "INFECTED"
 *              at (290,260) when the display copy reads exactly 100.
 *   (128,336)  help panel — translucent gray rect to (384,432), exact;
 *              with assets/messages.emsg + the font it carries the REAL
 *              engine help line (hovered page name / infection diary,
 *              message bank group 0 — see hub_help_line).
 *
 * DECOR renders through the REAL game textures when assets/ui.emui is
 * present (see the "UI decor sheet" block below): the "MAIN" title art
 * at (16,0), the button legend at (0,320), the four page-arrow icons,
 * plus the pager-diamond marker rings and the profile bio block.
 * Without the asset the decor pass queues nothing (no regression).
 *
 * PAGE NAVIGATION (skeleton — see the nav block + em_hud.h): stick
 * hover among the pager diamonds (green hovered marker), X enters the
 * hovered page (ITEM/MAP/SPR4/DATABASE via the engine's remap), the
 * page view draws its exported ui_pageN.emui background textures plus
 * an amber flag strip, Circle/Triangle exits back to the hub. The ITEM
 * page additionally renders a basic real interior (bank category
 * labels + the two modeled item counts — see page_render).
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
 * BACKGROUND: with a BACKDROP record in the active sheet the screen
 * draws the engine's REAL animated UI background (func_0020A7A0 — see
 * background_render below): black UI-scene base + two scrolling tilings
 * + the periodic zoom burst of the screen's own 128x64 tile, through
 * the em_gfx backdrop queue (bottom of the overlay pass). The hub tile
 * is the dark blue-gray circuit-board texture at GS TBP 0x1E40; each
 * page carries its own tile in ui_pageN.emui.
 *
 * STAND-INS (flagged, see em_hud.h): the scene-dim rect remains the
 * FALLBACK background when no BACKDROP record exists (old/missing
 * asset); the rotating highlight uses standard alpha blend toward white
 * as the stand-in for the engine's blend-mode-1 additive pass (the
 * overlay pipeline is alpha-blend only). The ROTATING PLAYER MODEL is
 * no longer a stand-in: em_game renders the UI-camera 3D scene under
 * this overlay (em_hud_scene_3d / em_hud_backdrop_ready below).
 *
 * All primitives go through the em_gfx overlay pass (rects + the
 * em_gfx_overlay_arc4 annular-arc primitive — the translation of the
 * engine's 0x60-block arc func_002082B0). */
#include "game/em_hud.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "em_input.h"   /* EM_PAD_TRIANGLE / EM_PAD_START — toggle bits */
#include "game/em_door.h"   /* em_door_menu_locked — the open gate
                             * (the engine's menu poll func_001AE7E0
                             * refuses while the fade machine runs or
                             * scripted mode is active; em_door owns the
                             * decoded transit window — em_door.h "THE
                             * TWO LOCKS") */

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

/* The ring's TRACK — arc block D_00265390, which func_00208AD0 draws
 * TWICE (see health_gauge): once as the full 180..540 annulus and once
 * again over the depleted span, masking the coloured ring there.
 *
 * COLOUR/RADII CORRECTED (2026-07, third audit pass).  The previous pass
 * reasoned "same block as the ring above => r36-56, colour unknown" and
 * used a dark stand-in.  That conflated two different blocks: the
 * byte-matched func_00208AD0 writes the health gradient into
 * D_00265410..D_0026544C, which is D_002653F0 + 0x20..+0x5C — i.e. the
 * gradient belongs to block D_002653F0, NOT to D_00265390.  FINDINGS.md
 * "STATUS SCREEN LAYOUT" item 7 has the two blocks' static data from a
 * VRAM capture: D_002653F0 = r36-56 (the coloured ring) and D_00265390 =
 * r24-56 light blue (0,153,255,128) (the track).  Same capture the
 * r36-56 figure itself comes from. */
static const float kRingTrack[4]   = GS(0, 153, 255, 128);

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

/* Pager-diamond marker colors.  The three arc blocks are 0x265270 (disc
 * r0-16), 0x2652D0 (ring r10-12) and 0x265330 (ring r14-16); the radii
 * come from FINDINGS "STATUS SCREEN LAYOUT" item 2, the GREEN/BLUE pairs
 * below from func_00209DF0 (BYTE-MATCHED), which writes only the
 * 0x265330 block's G/B components: idle (128,255) then (64,64), hovered
 * (240,0) then (200,0).  R and A are static data we have not exported —
 * the 0 red and 128 alpha here are the port's assumption. */
static const float kDiscWhite[4]   = GS(255, 255, 255, 128);
static const float kDiscFade[4]    = GS(  0,   0,   0,   0);
static const float kRingIn[4]      = GS(  0, 128, 255, 128);
static const float kRingOut[4]     = GS(  0,  64,  64, 128);
static const float kRingHovIn[4]   = GS(  0, 240,   0, 128);
static const float kRingHovOut[4]  = GS(  0, 200,   0, 128);

/* Page-view placeholder fill (flagged stand-in — used when the page's
 * ui_pageN.emui asset is missing, and as the keypad page's permanent
 * fill until its data-driven textures are decoded). */
static const float kPagePanel[4]   = GS( 16,  20,  48, 96);
/* "Content TBD" flag strip color (amber, clearly non-authentic). */
static const float kTbdAmber[3]    = { 1.0f, 0.75f, 0.2f };

/* Decor sprites draw white-modulated (the engine passes 0x80808080). */
static const float kSpriteWhite[4] = GS(255, 255, 255, 128);

/* Text-style colors (8-byte style records at 0x265510..) — used at
 * PLACEHOLDER_ALPHA for the unrenderable text blocks. */
#define PLACEHOLDER_ALPHA 0.22f
static const float kTextWhite[3]   = { 1.0f, 1.0f, 1.0f };     /* 0x265510 */
static const float kTextRed[3]     = { 1.0f, 0.0f, 0.0f };     /* 0x265528 */
static const float kTextDarkRed[3] = { 96.0f / 255.0f, 8.0f / 255.0f,
                                       16.0f / 255.0f };       /* 0x265520 */

/* Scene dim — the FALLBACK for the engine's real background when the
 * active sheet has no BACKDROP record (old/missing asset). With the
 * record present, background_render below draws the real thing. */
static const float kSceneDim[4]    = { 0.0f, 0.0f, 0.0f, 0.60f };

/* The UI-camera base frame behind the background layers: black (the
 * engine's identity-camera scene is empty except the rotating player
 * model). Queued only when em_game did NOT render the real UI-camera
 * 3D scene this frame (em_hud_scene_3d below). */
static const float kUiSceneBlack[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

/* Background-layer modulate color: the engine passes (96,96,96, 64*sin)
 * — GS modulate is Ct*Cs/128, so 96 = 0.75 brightness, 64 = 0.5 alpha. */
#define BG_MOD   (96.0f / 128.0f)
#define BG_ALPHA (64.0f / 128.0f)

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
        if (!gfx || !em_gfx_overlay_texture_set(gfx,
                                                EM_GFX_OVERLAY_TEX_FONT,
                                                s_font.sheet,
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

/* --- message bank (assets/messages.emsg) ------------------------------
 *
 * The status screen's text lines (hub help, ITEM categories, SPR4
 * components, prompts, map names, item names/descriptions) come from
 * ONE engine bank file: boot chunk asset slot 2 (runtime pointer
 * D_0028A498), resolved by group + line (func_001FCB90 ->
 * func_001FE070; FINDINGS.md "STATUS SUB-PAGES" -> "The message
 * bank"). The decomp repo's tools/export_ui.py --messages flattens the
 * user's own extract/chunk00/f02_id02.bin into this simple form.
 *
 * .emsg v1 layout (little-endian; must match export_ui.py):
 *   0   "EMSG"            12  u32 line_count (total)
 *   4   u32 version (= 1) 16  u32 blob_size
 *   8   u32 group_count
 *   then group_count * { u32 first (line-table index), u32 count }
 *   then line_count * u32 blob offsets
 *   then blob_size bytes of NUL-terminated strings ('\n' = in-entry
 *   line break)
 *
 * Missing/invalid asset => every bank-driven draw (hub help text, the
 * ITEM page interior) queues nothing — no regression. */
static struct {
    int       state;        /* 0 = untried, 1 = parsed, -1 = unavailable */
    uint32_t  group_count, line_count, blob_size;
    uint32_t *groups;       /* 2 u32 per group: first, count */
    uint32_t *offsets;      /* line index -> blob offset */
    char     *blob;
} s_msg;

static void msg_parse(void)
{
    if (s_msg.state) return;
    s_msg.state = -1;                        /* sticky failure default */
    FILE *f = fopen("assets/messages.emsg", "rb");
    if (!f) return;                          /* missing = no bank text */
    uint8_t hdr[20];
    if (fread(hdr, 1, 20, f) != 20 || memcmp(hdr, "EMSG", 4) != 0 ||
        font_u32(hdr + 4) != 1) {
        fprintf(stderr, "hud: assets/messages.emsg: bad header\n");
        fclose(f);
        return;
    }
    s_msg.group_count = font_u32(hdr + 8);
    s_msg.line_count  = font_u32(hdr + 12);
    s_msg.blob_size   = font_u32(hdr + 16);
    if (!s_msg.group_count || !s_msg.line_count || !s_msg.blob_size ||
        s_msg.group_count > 64 || s_msg.line_count > 4096 ||
        s_msg.blob_size > (1u << 20)) {
        fprintf(stderr, "hud: assets/messages.emsg: implausible sizes\n");
        fclose(f);
        return;
    }
    size_t gb = (size_t)s_msg.group_count * 8;
    size_t lb = (size_t)s_msg.line_count * 4;
    uint8_t *raw = (uint8_t *)malloc(gb + lb);
    s_msg.groups  = (uint32_t *)calloc(s_msg.group_count, 8);
    s_msg.offsets = (uint32_t *)calloc(s_msg.line_count, 4);
    s_msg.blob    = (char *)malloc(s_msg.blob_size);
    int ok = raw && s_msg.groups && s_msg.offsets && s_msg.blob &&
             fread(raw, 1, gb + lb, f) == gb + lb &&
             fread(s_msg.blob, 1, s_msg.blob_size, f) == s_msg.blob_size &&
             s_msg.blob[s_msg.blob_size - 1] == '\0';
    fclose(f);
    if (ok) {
        for (uint32_t g = 0; g < s_msg.group_count; g++) {
            s_msg.groups[g * 2]     = font_u32(raw + (size_t)g * 8);
            s_msg.groups[g * 2 + 1] = font_u32(raw + (size_t)g * 8 + 4);
            if (s_msg.groups[g * 2] + s_msg.groups[g * 2 + 1] >
                s_msg.line_count)
                ok = 0;
        }
        for (uint32_t i = 0; i < s_msg.line_count; i++) {
            s_msg.offsets[i] = font_u32(raw + gb + (size_t)i * 4);
            if (s_msg.offsets[i] >= s_msg.blob_size) ok = 0;
        }
    }
    free(raw);
    if (ok) {
        s_msg.state = 1;
        fprintf(stderr, "hud: message bank: %u groups, %u lines\n",
                s_msg.group_count, s_msg.line_count);
    } else {
        if (raw)
            fprintf(stderr,
                    "hud: assets/messages.emsg: truncated/inconsistent\n");
        free(s_msg.groups);  s_msg.groups  = NULL;
        free(s_msg.offsets); s_msg.offsets = NULL;
        free(s_msg.blob);    s_msg.blob    = NULL;
    }
}

/* Bank line `line` of `group` (the engine's func_001FCB90 indexing), or
 * NULL when out of range / the asset is missing. Strings may carry '\n'
 * in-entry line breaks (draw with msg_text). */
static const char *msg_line(uint32_t group, uint32_t line)
{
    msg_parse();
    if (s_msg.state != 1 || group >= s_msg.group_count) return NULL;
    if (line >= s_msg.groups[group * 2 + 1]) return NULL;
    return s_msg.blob + s_msg.offsets[s_msg.groups[group * 2] + line];
}

/* --- UI decor sheet (assets/ui.emui) ----------------------------------
 *
 * The status hub's textured decor sprites (title art, button legend,
 * page-arrow icons) are PSMT4 textures resident in GS VRAM with
 * 16-entry CLUTs (FINDINGS.md "STATUS SCREEN UI TEXTURES"); the decomp
 * repo's tools/export_ui.py decodes them from the user's own GS-VRAM
 * dump into one RGBA8 sheet plus per-sprite records that carry BOTH the
 * sheet UVs and the audited 512x448-canvas draw anchors — so the layout
 * travels with the asset and this loader just draws every record.
 *
 * .emui v1 layout (little-endian; must match export_ui.py):
 *   0   "EMUI"            12  u32 sheet_h
 *   4   u32 version (= 1) 16  u32 sprite_count
 *   8   u32 sheet_w
 *   then sprite_count * { u16 u, v, w, h; s16 x, y; u16 dw, dh }
 *   then sheet_w * sheet_h * 4 RGBA8 texels (rows top-down)
 *
 * Two x/y sentinels (never drawn as positioned sprites):
 *   -32768  plain sheet-only record (no statically known anchor)
 *   -32767  BACKDROP record — the screen's animated-background tile
 *           (engine func_0020A7A0; dw/dh carry the 2x tile draw size);
 *           background_render picks it up for the bottom layer
 *
 * Missing/invalid asset => s_ui.state = -1 and the WHOLE decor pass
 * (sprites, pager-diamond arcs, profile text) queues nothing — the
 * frame stays identical to the pre-decor build. */
#define SHEET_ONLY_XY (-32768)   /* exporter's plain sheet-only sentinel */
#define BACKDROP_XY   (-32767)   /* exporter's backdrop-tile sentinel    */
typedef struct {
    uint16_t u, v, w, h;
    int16_t  x, y;
    uint16_t dw, dh;
} UiSprite;

typedef struct {
    int       state;        /* 0 = untried, 1 = parsed, -1 = unavailable */
    UiSprite *sprites;
    uint32_t  sprite_count;
    uint8_t  *sheet;        /* RGBA8 — KEPT after registration so the
                             * single UI slot can swap hub<->page sheets
                             * on navigation (the engine's transient
                             * per-page texture re-stream, s27) */
    uint32_t  sheet_w, sheet_h;
} UiSheet;

static UiSheet s_ui;                 /* hub decor (assets/ui.emui) */
static UiSheet s_page_ui[4];         /* pages 0-3 (assets/ui_pageN.emui) */

/* Which sheet the EM_GFX_OVERLAY_TEX_UI slot currently holds:
 * -2 = none yet, -1 = hub decor, 0..3 = that page's sheet. */
#define SLOT_NONE (-2)
#define SLOT_HUB  (-1)
static int s_slot_owner = SLOT_NONE;

/* Parse one .emui file (same v1 format for the hub and page sheets). */
static void emui_parse(UiSheet *ui, const char *path, uint32_t max_dim)
{
    if (ui->state) return;
    ui->state = -1;                          /* sticky failure default */
    FILE *f = fopen(path, "rb");
    if (!f) return;                          /* missing = no decor pass */
    uint8_t hdr[20];
    if (fread(hdr, 1, 20, f) != 20 || memcmp(hdr, "EMUI", 4) != 0 ||
        font_u32(hdr + 4) != 1) {
        fprintf(stderr, "hud: %s: bad header\n", path);
        fclose(f);
        return;
    }
    ui->sheet_w      = font_u32(hdr + 8);
    ui->sheet_h      = font_u32(hdr + 12);
    ui->sprite_count = font_u32(hdr + 16);
    if (!ui->sheet_w || !ui->sheet_h || !ui->sprite_count ||
        ui->sheet_w > max_dim || ui->sheet_h > max_dim ||
        ui->sprite_count > 256) {
        fprintf(stderr, "hud: %s: implausible sizes\n", path);
        fclose(f);
        return;
    }
    size_t   rec_bytes   = (size_t)ui->sprite_count * 16;
    size_t   sheet_bytes = (size_t)ui->sheet_w * ui->sheet_h * 4;
    uint8_t *rraw = (uint8_t *)malloc(rec_bytes);
    ui->sprites = (UiSprite *)calloc(ui->sprite_count, sizeof(UiSprite));
    ui->sheet   = (uint8_t *)malloc(sheet_bytes);
    int ok = rraw && ui->sprites && ui->sheet &&
             fread(rraw, 1, rec_bytes, f) == rec_bytes &&
             fread(ui->sheet, 1, sheet_bytes, f) == sheet_bytes;
    fclose(f);
    if (ok) {
        for (uint32_t i = 0; i < ui->sprite_count; i++) {
            const uint8_t *p = rraw + (size_t)i * 16;
            UiSprite      *s = &ui->sprites[i];
            s->u  = (uint16_t)(p[0]  | p[1]  << 8);
            s->v  = (uint16_t)(p[2]  | p[3]  << 8);
            s->w  = (uint16_t)(p[4]  | p[5]  << 8);
            s->h  = (uint16_t)(p[6]  | p[7]  << 8);
            s->x  = (int16_t)(p[8]   | p[9]  << 8);
            s->y  = (int16_t)(p[10]  | p[11] << 8);
            s->dw = (uint16_t)(p[12] | p[13] << 8);
            s->dh = (uint16_t)(p[14] | p[15] << 8);
            if ((uint32_t)s->u + s->w > ui->sheet_w ||
                (uint32_t)s->v + s->h > ui->sheet_h)
                ok = 0;
        }
    }
    if (ok) {
        ui->state = 1;
        fprintf(stderr, "hud: %s: sheet %ux%u, %u sprites\n",
                path, ui->sheet_w, ui->sheet_h, ui->sprite_count);
    } else if (rraw) {
        fprintf(stderr, "hud: %s: truncated/inconsistent\n", path);
    }
    free(rraw);
    if (ui->state != 1) {
        free(ui->sprites); ui->sprites = NULL;
        free(ui->sheet);   ui->sheet   = NULL;
    }
}

/* Make `owner`'s sheet the resident UI-slot texture (re-registering on
 * hub<->page transitions, the port's version of the engine's per-page
 * texture re-stream). Returns the parsed sheet, or NULL if its asset is
 * unavailable / the slot could not be (re)registered. */
static UiSheet *slot_ensure(EmGfx *gfx, int owner)
{
    UiSheet *ui;
    if (owner == SLOT_HUB) {
        ui = &s_ui;
        emui_parse(ui, "assets/ui.emui", 4096);
    } else {
        char path[64];
        ui = &s_page_ui[owner];
        snprintf(path, sizeof path, "assets/ui_page%d.emui", owner);
        emui_parse(ui, path, 4096);
    }
    if (ui->state != 1) return NULL;
    if (s_slot_owner != owner) {
        if (!gfx || !em_gfx_overlay_texture_set(gfx, EM_GFX_OVERLAY_TEX_UI,
                                                ui->sheet,
                                                ui->sheet_w, ui->sheet_h))
            return NULL;
        s_slot_owner = owner;
    }
    return ui;
}

/* Hub decor availability (compat wrapper for the hub pass). */
static int ui_ensure(EmGfx *gfx)
{
    return slot_ensure(gfx, SLOT_HUB) != NULL;
}

/* --- animated UI background (engine func_0020A7A0) --------------------
 *
 * Decoded draw chain (FINDINGS.md "STATUS SCREEN BACKGROUND"): every
 * status/UI screen calls the universal background drawer with ONE
 * per-screen 128x64 PSMT4 tile token before its panels. It composites
 * THREE layers over the black UI-camera frame, each with persistent
 * state (the engine's three 0x20-byte blocks at D_002655A0; .data init
 * {0,0,90,0} / {0,0,90,0} / {0,0,0,0} — state persists across opens,
 * there is no per-open reset):
 *
 *   layer 0  full-screen tiling of the tile at 2x (256x128 canvas px),
 *            scrolling LEFT 0.5 px/frame (+0x00 -= 0.5, wrap 0 -> 255);
 *            alpha 64*sin(phase deg) with phase pinned at 90 = constant
 *            64. Grid phase: cols at x === int(p0) (mod 256), rows at
 *            y === 96 (mod 128) — the engine's GS grid (x 0x400..0xC00
 *            step 0x100, field y 0x400..0xC00 step 0x40) lands on those
 *            residues inside the 512x448 canvas.
 *   layer 1  same tiling, scrolling UP 1 canvas px/frame (+0x04 -= 0.5
 *            field lines, wrap 0 -> 255; rows at y === 96 + 2*int(p1)
 *            mod 128), alpha PULSED: while timer >= 0 it counts down
 *            (alpha 0 — phase was reset); then phase += 0.25/frame and
 *            alpha = 64*sin(phase) (12 s in/out breath); at phase 180
 *            -> timer = 60 + 3*(rand()%60), phase/scroll reset.
 *   layer 2  periodic full-screen ZOOM BURST: same pulse timer; while
 *            active the tile stretches over the whole canvas expanding
 *            0.3 px/frame per side past each edge (quad (-p0,-p1,
 *            512+2*p0, 448+2*p1), p += 0.3/frame) at alpha 64*sin(phase).
 *
 * All layers draw the tile with modulate (96,96,96) at blend mode 0.
 * The port queues only the grid cells intersecting the canvas (the
 * engine emits the full off-screen grid; identical pixels) and skips
 * zero-alpha layers. rand() is a fixed-seed LCG — deterministic, so
 * headless captures reproduce. */
typedef struct {
    float p0, p1;       /* block +0x00/+0x04 — scroll / burst offsets */
    float phase;        /* block +0x08 — alpha phase, degrees         */
    int   timer;        /* block +0x0C — pulse cooldown; < 0 = active */
} BgLayer;

static BgLayer s_bgl[3] = {
    { 0.0f, 0.0f, 90.0f, 0 },     /* D_002655A0 .data init values */
    { 0.0f, 0.0f, 90.0f, 0 },
    { 0.0f, 0.0f,  0.0f, 0 },
};

/* Deterministic stand-in for the engine's rand() in the pulse re-seed. */
static uint32_t bg_rand(void)
{
    static uint32_t s = 0x2655A0u;   /* fixed seed: reproducible runs */
    s = s * 1103515245u + 12345u;
    return s >> 16;
}

/* Find the active sheet's BACKDROP record (x == -32767), or NULL. */
static const UiSprite *backdrop_record(const UiSheet *ui)
{
    if (!ui || ui->state != 1) return NULL;
    for (uint32_t i = 0; i < ui->sprite_count; i++)
        if (ui->sprites[i].x == BACKDROP_XY)
            return &ui->sprites[i];
    return NULL;
}

/* Did em_game render the UI-camera 3D scene this frame? (the black frame
 * + the rotating player model — the engine's identity-camera pass under
 * every status screen, func_0020CDC0 open / behavior func_0020E6F0).
 * When set, background_render skips its opaque black base fill: the 3D
 * pass already laid down the black frame WITH the player on it, and the
 * translucent tile layers composite over both — the engine's exact
 * draw order (3D scene, then func_0020A7A0's layers, then panels). */
static int s_scene3d = 0;

void em_hud_scene_3d(int rendered)
{
    s_scene3d = rendered ? 1 : 0;
}

/* Queue one frame of the animated background through the em_gfx
 * backdrop layer (bottom of the overlay pass). Returns 1 when drawn;
 * 0 when the sheet has no BACKDROP record (caller dims instead). */
static int background_render(EmGfx *gfx, const UiSheet *ui)
{
    const UiSprite *bd = backdrop_record(ui);
    if (!bd || !bd->dw || !bd->dh) return 0;   /* malformed record: dim */

    /* The black base frame. When em_game rendered the UI-camera 3D
     * scene this frame (em_hud_scene_3d: black backplate + the rotating
     * player model), the 3D pass IS the base — queuing the opaque fill
     * here would paint over the player (the overlay flushes after every
     * 3D draw). Without the 3D scene (no player asset) the fill remains
     * the flagged stand-in for the engine's UI-camera frame. */
    if (!s_scene3d)
        em_gfx_overlay_backdrop_fill(gfx, kUiSceneBlack);

    const float u0 = (float)bd->u, v0 = (float)bd->v;
    const float u1 = (float)(bd->u + bd->w), v1 = (float)(bd->v + bd->h);
    const float tw = (float)bd->dw, th = (float)bd->dh;  /* 256x128 */
    const float deg2rad = 0.01745329252f;

    for (int i = 0; i < 3; i++) {
        BgLayer *L = &s_bgl[i];

        /* pulse timer — layers 1/2 only (engine .L0020A810) */
        if (i > 0) {
            if (L->timer >= 0) {
                L->timer--;
            } else {
                L->phase += 0.25f;
                if (L->phase >= 180.0f) {
                    L->timer = 60 + 3 * (int)(bg_rand() % 60u);
                    L->phase = 0.0f;
                    L->p0 = L->p1 = 0.0f;
                }
            }
        }

        if (i == 2) {
            /* zoom burst — only while active (engine .L0020A9D8) */
            if (L->timer >= 0) continue;
            L->p0 += 0.3f;
            L->p1 += 0.3f;
            float a = BG_ALPHA * sinf(L->phase * deg2rad);
            if (a <= 0.0f) continue;
            const float c[4] = { BG_MOD, BG_MOD, BG_MOD, a };
            em_gfx_overlay_backdrop(gfx, -L->p0, -L->p1,
                                    EM_GFX_STATUS_W + 2.0f * L->p0,
                                    EM_GFX_STATUS_H + 2.0f * L->p1,
                                    u0, v0, u1, v1, c);
            continue;
        }

        /* tiled scroll layers (engine grid loop) */
        if (i == 0) {                       /* .L0020A8B8: left scroll  */
            L->p0 -= 0.5f;
            if (L->p0 < 0.0f) L->p0 = 255.0f;
        } else {                            /* .L0020A948: up scroll    */
            L->p1 -= 0.5f;
            if (L->p1 < 0.0f) L->p1 = 255.0f;
        }
        float a = BG_ALPHA * sinf(L->phase * deg2rad);
        if (a <= 0.0f) continue;
        const float c[4] = { BG_MOD, BG_MOD, BG_MOD, a };
        float xph = (float)(((int)L->p0) % (int)tw);
        float yph = (float)((96 + 2 * (int)L->p1) % (int)th);
        for (float y = yph - th; y < EM_GFX_STATUS_H; y += th)
            for (float x = xph - tw; x < EM_GFX_STATUS_W; x += tw)
                em_gfx_overlay_backdrop(gfx, x, y, tw, th,
                                        u0, v0, u1, v1, c);
    }
    return 1;
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
    [EM_HUD_TEXT_NUM12]        = { FONT_SMALL, 12.0f, 12.0f,
                                   { 1.0f, 1.0f, 1.0f, 1.0f } },
    [EM_HUD_TEXT_NUM12_RED]    = { FONT_SMALL, 12.0f, 12.0f,
                                   { 1.0f, 0.0f, 0.0f, 1.0f } },
    [EM_HUD_TEXT_NUM16]        = { FONT_SMALL, 16.0f, 16.0f,
                                   { 1.0f, 1.0f, 1.0f, 1.0f } },
    [EM_HUD_TEXT_NUM16_RED]    = { FONT_SMALL, 16.0f, 16.0f,
                                   { 1.0f, 0.0f, 0.0f, 1.0f } },
    [EM_HUD_TEXT_TALL]         = { FONT_TALL,  0.0f, 20.0f,
                                   { 1.0f, 1.0f, 1.0f, 1.0f } },
    [EM_HUD_TEXT_TALL_DARKRED] = { FONT_TALL,  0.0f, 20.0f,
                                   { 96.0f / 128.0f, 8.0f / 128.0f,
                                     16.0f / 128.0f, 1.0f } },
    [EM_HUD_TEXT_NAME12_BLUE]  = { FONT_SMALL, 12.0f, 16.0f,
                                   { 0.0f, 96.0f / 128.0f,
                                     206.0f / 128.0f, 1.0f } },
    [EM_HUD_TEXT_PROFILE10]    = { FONT_SMALL, 10.0f, 10.0f,
                                   { 80.0f / 128.0f, 80.0f / 128.0f,
                                     80.0f / 128.0f, 1.0f } },
    [EM_HUD_TEXT_TALL_GRAY]    = { FONT_TALL,  0.0f, 20.0f,
                                   { 96.0f / 128.0f, 96.0f / 128.0f,
                                     96.0f / 128.0f, 1.0f } },
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

/* Multi-line bank text: draw `str` at (x, y), starting a new line at
 * each '\n'.
 *
 * LINE ADVANCE — PORT FIGURE, flagged (matches em_hud.h).  The engine's
 * step is read but not resolvable: func_001FE070 does
 * `pen += (D_00264CD8 + D_00264CE0) >> 1` and func_001FC7B0 does the
 * same thing as `arg1 + ((cfg[4] + cfg[2]) >> 1)` on the D_00264CD0
 * config block — the two agree, but D_00264CD8/D_00264CE0 are font-
 * metric DATA we have not exported, so the numeric step is unknown.
 * `pen` is in the GS half-height units (func_001FC7B0 adds it to 0x790
 * unhalved), so the canvas step is D_00264CD8 + D_00264CE0.  24 canvas
 * px is the port's stand-in.  No-op without the font, like em_hud_text. */
static void msg_text(EmGfx *gfx, float x, float y, const char *str,
                     EmHudTextStyle style)
{
    char seg[96];
    if (!str) return;
    while (*str) {
        size_t n = 0;
        while (str[n] && str[n] != '\n') n++;
        size_t c = n < sizeof seg - 1 ? n : sizeof seg - 1;
        memcpy(seg, str, c);
        seg[c] = '\0';
        if (c) em_hud_text(gfx, x, y, seg, style);
        y   += 24.0f;
        str += n + (str[n] == '\n');
    }
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

/* THE ENGINE NUMBER FORMATTER — DECODED from func_001C5FB0(value,
 * digits, blank), the one routine every HUD readout formats through.
 * It walks a per-place divisor table for `digits` places, so the field
 * is ALWAYS `digits` wide and the number is right-aligned in it:
 *
 *   blank != 0  leading zeros come out as SPACES (0x20), not '0', and
 *               the last place always prints a digit — 75 in 3 places
 *               is " 75", 0 is "  0" (NOT a trimmed "75"/"0", and NOT
 *               a zero-padded "075": the digits stay pinned to the
 *               same right-hand column while the lead goes blank);
 *   blank == 0  no suppression at all — genuine zero-padding ("04").
 *
 * Overflow/negatives are clamped here: the engine's divisor walk runs
 * off the end of its digit range for those and emits junk glyphs (a
 * '-' spliced mid-number for negatives), which is not behaviour worth
 * reproducing. */
static void engine_digits(char *buf, size_t cap, int v, int digits,
                          int blank)
{
    int lim = 1;
    for (int i = 0; i < digits; i++) lim *= 10;
    if (v < 0)    v = 0;
    if (v >= lim) v = lim - 1;
    snprintf(buf, cap, blank ? "%*d" : "%0*d", digits, v);
}

/* Placeholder-path companion: how many of `digits` places the blank
 * suppression leaves empty, i.e. how far right the inked digits start
 * (font-less fallback only — with the font the spaces are in the
 * string and em_hud_text does this on its own). */
static int engine_blank_lead(int v, int digits)
{
    int lead = digits - decimal_digits(v);
    return lead > 0 ? lead : 0;
}

/* Status-screen visibility — hidden by default (the original shows no
 * persistent HUD), flipped by a Triangle OR Start edge (both buttons
 * open the same screen).  The "verified identical memory diff" that
 * used to be cited here is an OBSERVATION, not a decode.  The
 * source-derived part is func_001AE7E0's two-bit open test
 * `(D_00810E74 & 0x800) || (D_00810E74 & 0x10)` — two buttons, one
 * screen; naming them TRIANGLE and START is inference (em_hud.h). */
static int s_shown = 0;

/* MENU INHIBIT (em_hud.h) — the engine's D_008106B3 byte, mirrored
 * here by em_game's per-frame write: while set, the open press below
 * is dropped (the engine's player spine sets it while hit-reacting/
 * dying; the port adds the game-over screen, where START restarts). */
static int s_menu_inhibit = 0;

void em_hud_menu_inhibit(int inhibit)
{
    s_menu_inhibit = inhibit;
}

/* --- page navigation — PORT MODEL, provenance DOWNGRADED -------------
 *
 * PROVENANCE FIRST: every mapping below was previously written up as
 * "the controller's REMAP at func_0020CDC0 .L0020D294".  func_0020CDC0
 * is still an INCLUDE_ASM stub in the decomp — it has never been
 * decompiled, so nothing here is source-derived.  Treat the whole block
 * as OBSERVED behaviour plus port choices until that function lands.
 *
 * What IS decoded and does support the surrounding design:
 *   - func_001AE7E0 (NEARMISS, body-correct) is the mode classifier.
 *     It returns 2 — "enter the status/menu mode" — for
 *     `D_008106C5 != 0 || D_008106B0 != 0` (an external request or a
 *     posted pickup) and, further down, for
 *     `(D_00810E74 & 0x800) || (D_00810E74 & 0x10)` — two distinct
 *     button-edge bits, which is the real basis for "two buttons open
 *     the same screen".  Which two is inferred, not read: the same
 *     edge word drives func_001AC480's menu walk, where 0x4000 steps
 *     the selector forward and 0x1000 steps it back, so the low nibble
 *     0x10/0x20/0x40/0x80 reads as the face buttons and 0x800 as
 *     START.  Inference, flagged.
 *   - func_0020CD80 (byte-matched) is a one-line thunk,
 *     func_001FB9F0(2, 0x1000, 0x1000, 0x1000) — the same cue call the
 *     menu walk uses for its move (cue 5) and confirm (0x5DD..0x5DF)
 *     sounds.  So it fires cue 2.  That it is the *no-hover buzz* is
 *     an assumption inherited from the stub write-up.
 *   - func_0020D930 (NEARMISS, body-correct) IS the hover quantizer,
 *     and its mode-0 arm settles the quadrant mapping the port uses.
 *     It reads the stick angle from the scratchpad float 0x700038AC and
 *     writes ctx[0x11].  Its full mode-0 ladder, re-read verbatim:
 *       ang <  -2.670354                        -> 1
 *       ang <  -2.3561945 (-3pi/4)              -> 2
 *       ang <  -0.7853982 (-pi/4)               -> 3
 *       ang <   0.7853982 ( pi/4)               -> 2
 *       ang <   2.3561945 (3pi/4)               -> 1
 *       otherwise                               -> 4
 *     i.e. right = 2, down = 1, left = 4, up = 3 for a y-down atan2 —
 *     the port's quadrant table, and it lines up with func_00209DF0's
 *     `i == arg0[0x11] - 1` marker order (bottom, right, top, left).
 *     A state CHANGE fires cue 5; a failed gate resets ctx[0x11] to 0
 *     ("released" -> no hover), which the port mirrors.
 *     NOT from the source: the 0.8 deflection figure.  The engine gates
 *     on func_00128350(0x700038A8) + func_00100130(), neither of which
 *     is decompiled; 0.8 is the port's noise floor.
 *     ALSO NOT reproduced: the two asymmetric wedges at the -pi seam.
 *     A pure y-down atan2 quadrant split would give 4 (left) across
 *     the whole -pi..-3pi/4 band, but the engine hands out 2 over
 *     [-2.670354, -2.3561945) and 1 below -2.670354.  Whether that is
 *     a deliberate bias or a different `ang` convention is UNRESOLVED
 *     (0x700038AC is filled by func_001B62C0, not decompiled), so the
 *     port keeps the plain quadrant split and this stays flagged.
 *
 * PORT MODEL (unchanged behaviour, honestly labelled):
 *   hub hover = left stick, deflection > 0.8 (PORT FIGURE), quadrant ->
 *   1 down, 2 right, 3 up, 4 left, releasing back to 0.  X enters:
 *     hover 1 (down)  -> page 3  DATABASE SCREEN  (chunk 0x24)
 *     hover 2 (right) -> page 2  SPR4 SCREEN      (chunk 0x2C)
 *     hover 3 (up)    -> page 1  MAP SCREEN       (chunk 0x1E)
 *     hover 4 (left)  -> page 0  ITEM SCREEN      (chunk 0x1F)
 *   X with no hover enters nothing; Circle or Triangle backs out of a
 *   page; Triangle/Start/Circle closes at the hub.  The "engine edge
 *   mask 0x830" that used to be quoted for the close is likewise from
 *   the stub — unverified.  Pages 4/5 (passcode keypads, chunks
 *   0x25/0x26) are not diamond-reachable in the port either. */
static int s_hover = 0;     /* 0 none, 1 down, 2 right, 3 up, 4 left */
static int s_page  = -1;    /* -1 = hub, 0..3 = entered page */

static const int kHoverToPage[5] = { -1, 3, 2, 1, 0 };

static const char *kPageNames[4] = {
    "ITEM SCREEN", "MAP SCREEN", "SPR4 SCREEN", "DATABASE SCREEN"
};

/* Hub help line — PROVENANCE DOWNGRADED.  This selection was recorded
 * as "the engine's selection in func_0020CDC0 .L0020D1AC", but
 * func_0020CDC0 is an undecompiled INCLUDE_ASM stub: neither the
 * hover->line mapping nor the 0x51/0x33/0x1F/0xB thresholds have been
 * read out of recovered C.  Kept as the port's observed model, NOT as
 * source-derived data.  A hover shows the hovered page's name (group-0
 * lines: 1 down -> 0, 2 right -> 9, 3 up -> 2, 4 left -> 1); idle
 * shows the infection-graded diary line keyed on
 * v = 100 - (int)displayed-infection.  Returns the group-0 line id, or
 * -1 = no help text. */
static int hub_help_line(float inf_disp)
{
    static const int kHoverLine[5] = { -1, 0, 9, 2, 1 };
    if (s_hover > 0) return kHoverLine[s_hover];
    int v = 100 - (int)inf_disp;
    if (v == 100)  return -1;
    if (v >= 0x51) return 4;
    if (v >= 0x33) return 5;
    if (v >= 0x1F) return 6;
    if (v >= 0xB)  return 7;
    if (v > 0)     return 8;
    return 3;
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

/* EM_HUD_PAGE=<0..3> — start with that page entered (test hook for
 * page-view captures; meaningful together with EM_HUD_FORCE=1). */
static int hud_forced_page(void)
{
    static int page = -2;
    if (page == -2) {
        const char *e = getenv("EM_HUD_PAGE");
        page = (e && e[0] >= '0' && e[0] <= '3' && !e[1]) ? e[0] - '0'
                                                          : -1;
    }
    return page;
}

/* EM_HUD_HOVER=<1..4> — hold that pager hover at the hub (test hook for
 * the green hovered-marker state; meaningful with EM_HUD_FORCE=1). */
static int hud_forced_hover(void)
{
    static int hov = -1;
    if (hov < 0) {
        const char *e = getenv("EM_HUD_HOVER");
        hov = (e && e[0] >= '1' && e[0] <= '4' && !e[1]) ? e[0] - '0' : 0;
    }
    return hov;
}

/* Display copies of health/infection (the engine's 0x810858/0x81085C):
 * step +-1 per frame toward the player-actor targets — the screen's
 * count-up animation. Sentinel < 0 = snap to target on first sight. */
static float s_disp_health    = -1.0f;
static float s_disp_infection = -1.0f;

/* Frame counter while visible — drives the highlight rotation.  The
 * engine's own counter is the status ctx word counter[8], bumped once
 * per drawn frame and used as 12.0f * ((counter[8] >> 1) % 30)
 * (func_00208AD0, byte-matched): 30 stepped positions, 1 s per turn. */
static uint32_t s_frames = 0;

void em_hud_update(const EmFrameInput *in)
{
    /* One-time nav init for forced captures: EM_HUD_FORCE bypasses the
     * open edge, so apply EM_HUD_PAGE here. */
    static int nav_init = 0;
    if (!nav_init) {
        nav_init = 1;
        if (hud_forced()) s_page = hud_forced_page();
    }

    if (!in) return;

    if (!em_hud_visible()) {
        /* Closed: Triangle or Start opens the screen at the hub —
         * UNLESS a lock holds.  CONFIRMED against func_001AE7E0
         * (NEARMISS, body-correct), which is a pure classifier with no
         * latch of its own: it returns 0 (blocked) for D_008106B8,
         * D_008106B9, the fade word D_0028A9A0, the scratchpad gate
         * *0x70003B8D and the menu-inhibit byte D_008106B3, and only
         * reaches its open test `(D_00810E74 & 0x800) ||
         * (D_00810E74 & 0x10)` after all of them.  A blocked press is
         * simply dropped.  em_door owns the fade/scripted half of that
         * list (em_door.h "THE TWO LOCKS"), s_menu_inhibit the
         * D_008106B3 half.
         *
         * Not modelled here: func_001AE7E0 also returns 1 whenever
         * D_00810E50 != 4, i.e. the open test is unreachable outside
         * that game state. */
        if ((in->pressed & (EM_PAD_TRIANGLE | EM_PAD_START)) &&
            !em_door_menu_locked() && !s_menu_inhibit) {
            s_shown = 1;
            s_hover = 0;
            s_page  = hud_forced_page();   /* -1 unless EM_HUD_PAGE */
        }
        return;
    }

    if (s_page >= 0) {
        /* Page view: Circle (engine exit path) or Triangle returns to
         * the hub; page content input is not modeled yet. */
        if (in->pressed & (EM_PAD_CIRCLE | EM_PAD_TRIANGLE))
            s_page = -1;
        return;
    }

    /* Hub: Triangle/Start/Circle closes.  (The "engine edge mask 0x830"
     * this used to cite comes from the func_0020CDC0 stub — unverified;
     * the two OPEN bits 0x800|0x10 are the only part func_001AE7E0
     * actually shows.) */
    if (in->pressed & (EM_PAD_TRIANGLE | EM_PAD_START | EM_PAD_CIRCLE)) {
        s_shown = 0;
        s_hover = 0;
        return;
    }

    /* Stick hover among the pager diamonds — the quadrant mapping is
     * func_0020D930's mode-0 arm (NEARMISS; thresholds +-pi/4, +-3pi/4
     * -> right 2 / down 1 / left 4 / up 3), the 0.8 deflection floor is
     * the port's.  Raw bytes are 0x80-centered, 0x00 = left/up. */
    {
        float dx = ((float)in->lx - 128.0f) / 128.0f;
        float dy = ((float)in->ly - 128.0f) / 128.0f;
        if (dx * dx + dy * dy > 0.8f * 0.8f) {
            if (dx >  0.0f && dx >=  dy && dx >= -dy)      s_hover = 2;
            else if (dx < 0.0f && -dx >= dy && -dx >= -dy) s_hover = 4;
            else if (dy > 0.0f)                            s_hover = 1;
            else                                           s_hover = 3;
        } else {
            s_hover = hud_forced_hover();   /* 0 unless EM_HUD_HOVER */
        }
    }

    /* X enters the hovered page; with no hover, nothing.  (The "buzz"
     * on the empty press is func_0020CD80 = func_001FB9F0(2,...) — the
     * thunk is byte-matched, but that the empty press is what CALLS it
     * comes from the func_0020CDC0 stub.  The port plays no cue.) */
    if ((in->pressed & EM_PAD_CROSS) && s_hover > 0)
        s_page = kHoverToPage[s_hover];
}

int em_hud_visible(void)
{
    return s_shown || hud_forced();
}

/* The pause-gate query (em_hud.h): the REAL toggle only — em_game halts
 * the world simulation on this while the menu is up (the engine's open
 * flag 0x8106C4). EM_HUD_FORCE is deliberately excluded: it is a
 * render-only capture hook and the headless overlay captures need
 * gameplay to keep running underneath. */
int em_hud_is_open(void)
{
    return s_shown;
}

/* Can the ACTIVE sheet (the hub's ui.emui, or the entered page's
 * ui_pageN.emui) draw the real animated background? em_game gates the
 * UI-camera 3D scene on this: without a BACKDROP record the screen
 * falls back to the translucent scene-dim over the live frame (the old
 * stand-in), where a player-only 3D pass would be wrong — and the
 * asset-absent forced capture must stay byte-identical to the pre-3D
 * builds. */
int em_hud_backdrop_ready(EmGfx *gfx)
{
    return backdrop_record(slot_ensure(gfx, s_page >= 0 ? s_page
                                                        : SLOT_HUB)) != NULL;
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
 * cx=208; ring centre (208,196), label row y 118, value row y 262 —
 * all three re-read from the byte-matched call site below; the "197"
 * this line used to carry was the old one-pixel guess). */
static void health_gauge(EmGfx *gfx, float hp, float hp_max)
{
    /* Anchors re-derived from the BYTE-MATCHED call site: func_00209DF0
     * calls func_00208AD0(ctx, 0xD0, 0xC4), and func_00208AD0 maps a
     * canvas point to GS as (x + 0x700, (y >> 1) + 0x790).  So the ring
     * centre is canvas (0xD0, 2 * (0xC4 >> 1)) = (208, 196) — the old
     * 197 was a one-pixel guess. */
    const float cx = 208.0f, cy = 196.0f;

    /* label: "HEALTH" centered on x=208 at y=118, marker 12 px left.
     * The marker is the engine's own 8x8 underline rect: func_00208AD0
     * calls func_00207F80(1, labelW+0x6F4, ((py-0x4C)>>1)+0x790,
     * labelW+0x6FC, ((py-0x44)>>1)+0x790, 0x80CE6000) = canvas
     * (labelW-12, 120) to (labelW-4, 128), colour 0x80CE6000 =
     * (r 0, g 0x60, b 0xCE, a 0x80) — kMarkerBlue exactly.  y was 122
     * here; the byte-matched arithmetic gives 120.
     * Engine quirk kept (func_00208AD0): the centering width comes from
     * the TALL-font width helper func_001CC170 (proportional — 54 px
     * for "HEALTH") while the label draws in the SMALL font at 12x12
     * cells (72 px), so the drawn text sits a little right of true
     * center, exactly like the original. */
    if (em_hud_font_ready()) {
        const float label_x =
            cx - em_hud_text_width("HEALTH", EM_HUD_TEXT_TALL) * 0.5f;
        marker(gfx, label_x - 12.0f, 120.0f);
        em_hud_text(gfx, label_x, 118.0f, "HEALTH", EM_HUD_TEXT_LABEL12);
    } else {
        const float label_x = cx - (6 * 12.0f) * 0.5f;
        marker(gfx, label_x - 12.0f, 120.0f);
        text_placeholder(gfx, label_x, 118.0f, 6, 12.0f, 12.0f,
                         kTextWhite);
    }

    /* TRACK ring r24-56, full circle — arc block D_00265390, the FIRST
     * of the two prims func_00208AD0 (BYTE-MATCHED) submits before the
     * highlight:
     *   D_00265398 = 180.0f; D_0026539C = 540.0f;   // start, end
     *   D_00265390/94 = centre;  func_002082B0(1, &D_00265390);
     * BLOCK SPLIT CORRECTED (2026-07, third pass): the port used to draw
     * ONE ring here and give it the health gradient.  There are two
     * separate arc prims: this track, and the coloured ring below whose
     * block is D_002653F0 (see kRingTrack at the top of this file). */
    em_gfx_overlay_arc(gfx, cx, cy, 24.0f, 56.0f, 180.0f, 540.0f,
                       kRingTrack);

    /* Coloured ring r36-56, full circle — arc block D_002653F0, the
     * SECOND prim.  func_00208AD0 writes only its four gradient RGBA
     * slots (D_00265410..D_0026544C = D_002653F0 + 0x20..+0x5C) and its
     * centre; the angles/radii are the block's static data (FINDINGS
     * item 7: r36-56, 180..540).  Red pair when health <= 35 —
     * CONFIRMED against the byte-matched function:
     * `if (D_00810858 > 35.0f)` selects the bright pair
     * 192/224/0/128 + 224/128/24/128, the else arm the dim pair
     * 160/0/0/128 + 192/0/0/128 (the kRingNorm / kRingLow pairs
     * declared at the top of this file). */
    const float *ca = (hp <= 35.0f) ? kRingLowA : kRingNormA;
    const float *cb = (hp <= 35.0f) ? kRingLowB : kRingNormB;
    em_gfx_overlay_arc4(gfx, cx, cy, 36.0f, 56.0f, 180.0f, 540.0f,
                        ca, cb, ca, cb);

    /* Rotating 120-deg highlight: two 60-deg arcs r36-56.  DECODED from
     * the byte-matched func_00208AD0:
     *   ang = 180.0f + 12.0f * (float)((counter[8] >> 1) % 30);
     *   arc A spans (ang - 60, ang), arc B spans (ang, ang + 60);
     * counter[8] is bumped once per drawn frame.  So the sweep STEPS
     * 12 deg every 2 frames (30 steps = 1 revolution per second), it
     * does not glide 6 deg/frame, and the leading edge of the pair sits
     * at `ang`, not `ang + 120`.  Both were port approximations. */
    float a = 180.0f + 12.0f * (float)((s_frames >> 1) % 30u);
    em_gfx_overlay_arc4(gfx, cx, cy, 36.0f, 56.0f, a - 60.0f, a,
                        kHiliteOff, kHiliteOff, kHiliteOn, kHiliteOn);
    em_gfx_overlay_arc4(gfx, cx, cy, 36.0f, 56.0f, a, a + 60.0f,
                        kHiliteOn, kHiliteOn, kHiliteOff, kHiliteOff);

    /* DEPLETED (empty) arc — the LAST prim, a second pass over the TRACK
     * block D_00265390 that re-paints the track colour over the depleted
     * part of the coloured ring.
     *
     * func_00208AD0 (BYTE-MATCHED) re-uses the SAME 0x60-byte arc block
     * (D_00265390) it drew the track with, writing only
     *   D_00265398 = -180.0f + 360.0f * (D_00810858 / 100.0f);
     *   D_0026539C = 180.0f;
     * where the first pass wrote (180, 540).
     *
     * +0x08/+0x0C of these blocks is a (start, end) ANGLE pair, and that
     * is settled inside this same byte-matched function: the two
     * rotating-highlight blocks get D_00265458/5C = (ang - 60, ang) and
     * D_002654B8/BC = (ang, ang + 60) at the identical offsets — a pair
     * that tracks the sweep every frame cannot be "angle, radius".
     *
     * So the second pass spans (-180 + 3.6*hp) .. 180, i.e. (mod 360)
     * 180 + 3.6*hp .. 540: the TAIL of the first pass's full sweep.  It
     * SHRINKS to nothing at hp 100 and covers the whole ring at hp 0 —
     * the EMPTY segment, re-painting the TRACK over the depleted part of
     * the coloured ring.  Same block as the track => same radii, r24-56.
     *
     * DRAW ORDER CORRECTED (2026-07 audit, second pass): this arc is the
     * LAST primitive func_00208AD0 submits — after func_00207D00(1, 1),
     * the two highlight blocks and func_00207D00(1, 0).  The port drew
     * it BEFORE the highlight, so the rotating sweep shone through the
     * depleted part of the ring; in the original the depleted arc paints
     * over it.  The order below is now the engine's.
     *
     * DIVISOR CORRECTED: the engine divides by the LITERAL 100.0f, not
     * by the displayed maximum.  When the infected cap latches the
     * display max to 60 (EmPlayerStatus.health_max = 60), the port used
     * to rescale the sweep and drew a FULL ring at 60/60; the original
     * still draws the 60% ring.  hp is clamped to the same 0..100 the
     * engine's own 0-100 meter assumes.
     *
     * COLOUR RESOLVED (2026-07, third pass): it is the TRACK block's own
     * colour, light blue (0,153,255,128) — see kRingTrack.  The dark
     * "unlit" stand-in that used to sit here came from mis-assigning the
     * health gradient to this block; the gradient is D_002653F0's. */
    float frac = clamp01f(hp / 100.0f);
    if (frac < 1.0f)
        em_gfx_overlay_arc(gfx, cx, cy, 24.0f, 56.0f,
                           180.0f + 360.0f * frac, 540.0f, kRingTrack);

    /* value row " 75 / 100" at y=262 — every anchor here is the
     * byte-matched func_00208AD0's own arithmetic with (px, py) =
     * (0xD0, 0xC4): digits at px-0x2A = 166, the '/' string D_00273568
     * at px+0x6FA-0x700 = 202, the max string at px+0x706-0x700 = 214,
     * all on row ((py+0x42)>>1)*2 = 262.
     *
     * Red style when health <= 60 — CONFIRMED: func_00208AD0 picks the
     * red glyph table D_00265528 when `D_008104E4 != 0` OR
     * `D_00810858 <= 60.0f`, else the white D_00265510.  (The 35 that
     * drives the ring above is a DIFFERENT threshold; do not merge the
     * two.)  The D_008104E4 latch also swaps the max string from
     * D_00273560 to D_00273558 and reddens it — still not modelled.
     *
     * The value is 3 places with leading zeros BLANKED, not zero-padded
     * — DECODED from func_00208AD0, which formats it as
     * func_001C5FB0(health, 3, 1): the blank flag is set, so 75 draws
     * as " 75" (right-aligned, lead column empty) and only a full 100
     * fills all three places. The old "075" was a port guess.
     *
     * CELL CORRECTED: all three runs here go out as func_001CBA50(...,
     * 0xC, 0xC, ...) — a 12x12 cell, the same one the labels use, NOT
     * the 12x16 the port was drawing them at. */
    int hp_i = (int)hp;
    if (hp_i < 0) hp_i = 0;
    if (em_hud_font_ready()) {
        char buf[8];
        engine_digits(buf, sizeof buf, hp_i, 3, 1);
        em_hud_text(gfx, 166.0f, 262.0f, buf,
                    hp <= 60.0f ? EM_HUD_TEXT_NUM12_RED
                                : EM_HUD_TEXT_NUM12);
        em_hud_text(gfx, 202.0f, 262.0f, "/", EM_HUD_TEXT_NUM12);
        snprintf(buf, sizeof buf, "%d", (int)hp_max);
        em_hud_text(gfx, 214.0f, 262.0f, buf, EM_HUD_TEXT_NUM12);
    } else {
        const float *style = (hp <= 60.0f) ? kTextRed : kTextWhite;
        int lead = engine_blank_lead(hp_i, 3);
        text_placeholder(gfx, 166.0f + 12.0f * (float)lead, 262.0f,
                         3 - lead, 12.0f, 12.0f, style);
        text_placeholder(gfx, 202.0f, 262.0f, 1, 12.0f, 12.0f,
                         kTextWhite);
        text_placeholder(gfx, 214.0f, 262.0f, 3, 12.0f, 12.0f,
                         kTextWhite);
    }
}

/* BATTERY block at (16,118) (engine func_00209280, mode 0). */
static void battery_block(EmGfx *gfx, uint8_t cur, uint8_t max)
{
    /* GATE SCOPE CORRECTED (2026-07 audit): func_00209280 draws the 8x8
     * underline marker and the "BATTERY" label in its `flag == 0` arm
     * BEFORE it ever tests D_00810C7F — only the "cur/max" caption and
     * the segment grid sit inside `if (D_00810C7F != 0)`.  The port used
     * to return early on battery_max == 0 and so hid the whole block,
     * including the marker + label the original keeps on screen. */
    marker(gfx, 16.0f, 120.0f);
    if (em_hud_font_ready())
        em_hud_text(gfx, 28.0f, 118.0f, "BATTERY", EM_HUD_TEXT_LABEL12);
    else
        text_placeholder(gfx, 28.0f, 118.0f, 7, 12.0f, 12.0f, kTextWhite);

    /* Engine gate D_00810C7F == 0: caption + grid suppressed.  The port
     * has no mirror of that byte, so battery_max != 0 stands in for it
     * (PORT EQUIVALENT, flagged). */
    if (!max) return;

    /* segment bar: one 8x8 square per internal HALF-unit (storage
     * 0x810CB2 holds half-units; the EmPlayerStatus fields are the
     * displayed units = half-units >> 1, so squares = cur * 2), 12 per
     * row wrapping below, drawn right-to-left, colour stepping
     * magenta -> yellow in 1/12 increments along the row (the ramp is
     * re-seeded per ROW in the engine, so col — not i — drives it).
     *
     * COLUMN X CORRECTED against func_00209280 (mode 0, x=0x10,
     * y=0x76, w=8): it sets `x = x + w*11` = 104 and then, per cell,
     * `xo = (i & 1) ? x - xOff : (x - xOff) - (w >> 3)` with
     * xOff = col * w.  So the RIGHTMOST cell's left edge is 104 - 1 =
     * 103 and each column steps 8 px left of that — the port was
     * subtracting 8*(col+1) and so drew the whole bar one cell (8 px)
     * too far left.  Row origin: yBase = y + 0x10 = 134, +8 per row. */
    int half_units = (int)cur * 2;
    for (int i = 0; i < half_units; i++) {
        int   col = i % 12, row = i / 12;
        float t   = (float)col / 12.0f;
        float c[4];
        for (int k = 0; k < 4; k++)
            c[k] = kBattMagenta[k] + (kBattYellow[k] - kBattMagenta[k]) * t;
        float x = 104.0f - 8.0f * (float)col
                  - ((col % 2) == 0 ? 1.0f : 0.0f);
        em_gfx_overlay_rect(gfx, x, 134.0f + 8.0f * (float)row,
                            8.0f, 8.0f, c);
    }

    /* "04/06" at (32,170): 2-digit ZERO-padded current + '/' + 2-digit
     * zero-padded max. DECODED from func_00209280, which builds the
     * caption as func_001C5FB0(0x810CB2>>1, 2, 0) + D_00273568 ("/") +
     * func_001C5FB0(0x810CB7>>1, 2, 0) — the blank flag is CLEAR here,
     * so unlike the health/reserve/infection readouts this one really
     * does print "04", not " 4".
     *
     * Anchor from the same function with (x, y) = (0x10, 0x76): canvas
     * x = x + 0x10 = 32, row ((y + 0x34) >> 1) * 2 = 170.  CELL
     * CORRECTED: the blit is func_001CBA50(..., 16, 16, ...) — a 16x16
     * cell, not the 12x16 the port was using. */
    if (em_hud_font_ready()) {
        char buf[12];
        char lo[8], hi[8];
        engine_digits(lo, sizeof lo, (int)cur, 2, 0);
        engine_digits(hi, sizeof hi, (int)max, 2, 0);
        snprintf(buf, sizeof buf, "%s/%s", lo, hi);
        em_hud_text(gfx, 32.0f, 170.0f, buf, EM_HUD_TEXT_NUM16);
    } else {
        int dd = decimal_digits(cur) > 2 ? decimal_digits(cur) : 2;
        text_placeholder(gfx, 32.0f, 170.0f, dd + 1 + 2, 16.0f, 16.0f,
                         kTextWhite);
    }
}

/* Did this frame's decor pass draw (ui.emui loaded + registered)? Set by
 * decor(); spr4_block uses it to drop the bullet-icon placeholder when
 * the real icon sprite (a ui.emui record at (16,262)) is on screen. */
static int s_decor_active = 0;

/* SPR4 block at (16,190) (engine func_00209860): reserve count ONLY —
 * the real screen shows no magazine state at all. */
static void spr4_block(EmGfx *gfx, int16_t reserve)
{
    marker(gfx, 16.0f, 192.0f);
    if (em_hud_font_ready())
        em_hud_text(gfx, 28.0f, 190.0f, "SPR4", EM_HUD_TEXT_LABEL12);
    else
        text_placeholder(gfx, 28.0f, 190.0f, 4, 12.0f, 12.0f, kTextWhite);

    /* bullet icon 24x24 at (16,262): the real sprite is a ui.emui decor
     * record (token ..2196, GS-scaled 32->24 — drawn by decor());
     * without the asset, the old neutral placeholder block. */
    if (!s_decor_active)
        text_placeholder(gfx, 16.0f, 262.0f, 1, 24.0f, 24.0f, kTextWhite);

    /* reserve count at (42,266): a 4-place field whose leading zeros are
     * BLANKED — DECODED from func_00209860, which formats D_00810CB4 as
     * func_001C5FB0(reserve, 4, 1). The blank flag means the field keeps
     * all 4 places and the digits sit right-aligned in it, so 120 draws
     * as " 120" starting one cell in, NOT left-aligned at x=42 (the old
     * port reading of "trim").
     *
     * Anchor from the same function with (arg0, arg1) = (0x10, 0xBE):
     * canvas x = arg0 + 0x1A = 42, row ((arg1 + 0x4C) >> 1) * 2 = 266.
     * CELL CORRECTED: the blit is func_001CBA50(..., 0x10, 0x10, ...) —
     * a 16x16 cell, not 12x16. */
    int res_i = reserve;
    if (res_i < 0) res_i = 0;
    if (em_hud_font_ready()) {
        char buf[8];
        engine_digits(buf, sizeof buf, res_i, 4, 1);
        em_hud_text(gfx, 42.0f, 266.0f, buf, EM_HUD_TEXT_NUM16);
    } else {
        int lead = engine_blank_lead(res_i, 4);
        text_placeholder(gfx, 42.0f + 16.0f * (float)lead, 266.0f,
                         4 - lead, 16.0f, 16.0f, kTextWhite);
    }

    /* NOT MODELLED (read in func_00209860, recorded so nobody thinks the
     * block is complete): a SECOND weapon row below this one — a 24x24
     * icon at (16, 286) and a second func_001C5FB0(n, 4, 1) count at
     * (42, 290), same 16x16 cell.  It is gated on the weapon-slot bytes
     * D_00810CA4/D_00810CA6 (slot 2 shows D_00810CB0; sub-types 1/2/3
     * show D_00810CA8/D_00810CAA; sub-type 4 shows
     * D_00810CAE + 100 * D_00810CAC with a trailing "%" string and a
     * left-shifted anchor when the field's lead place is blank).  The
     * port carries no second-weapon state, so nothing is drawn. */
}

/* INFECTION — text only (the real screen has NO infection bar): either
 * "INFECTED" tall-font dark red at (290,260) when at 100, or the
 * "INFECTION" label (296,260) + "NN%" value (296,288). Placeholder
 * blocks at the real tall-font (10x20) / number (16 px) metrics. */
static void infection_block(EmGfx *gfx, float infection)
{
    /* The "INFECTED" branch is an EQUALITY test in the byte-matched
     * func_00209DF0: `n = float_to_int(D_0081085C); if (n == 0x64)`.
     * The port's old `>= 100.0f` also caught 101+, which the engine
     * formats as a normal readout. */
    if ((int)infection == 100) {
        if (em_hud_font_ready())
            em_hud_text(gfx, 290.0f, 260.0f, "INFECTED",
                        EM_HUD_TEXT_TALL_DARKRED);
        else
            text_placeholder(gfx, 290.0f, 260.0f, 8, 10.0f, 20.0f,
                             kTextDarkRed);
        return;
    }
    /* value = a 3-place blank-suppressed field with '%' appended —
     * DECODED from func_00209DF0 (BYTE-MATCHED), which draws this row as
     *   func_00123168(D_002862C0, func_001C5FB0(n, 3, 1));
     *   func_00122EF0(D_002862C0, D_00273570);          // the "%"
     *   func_001CBA50(1, 0x828, 0x820, 0x10, 0x10, ...);
     * So 60 renders " 60%" (lead cell blank, digits pinned to the same
     * columns as a 3-digit value), not a left-packed "60%".
     * Anchors: canvas x 0x828 - 0x700 = 296, row (0x820 - 0x790) * 2 =
     * 288.  CELL CORRECTED to the engine's 16x16 (was 12x16).
     *
     * The 100%% branch is the one above: func_001CC1E0(1, 0x822, 0x812,
     * 0xA, 0x14, D_00267290, D_00265520) = tall font 10x20 at canvas
     * (290, 260) in the D_00265520 style.  The label branch here is
     * func_001CC1E0(1, 0x828, 0x812, 0xA, 0x14, D_00267294, 0) — canvas
     * (296, 260), and note the style argument is 0, i.e. the engine's
     * DEFAULT text colour rather than an explicit style record.  The
     * port draws it white; that colour is UNRESOLVED. */
    int inf_i = (int)infection;
    if (inf_i < 0) inf_i = 0;
    if (em_hud_font_ready()) {
        char num[8], buf[12];
        em_hud_text(gfx, 296.0f, 260.0f, "INFECTION", EM_HUD_TEXT_TALL);
        engine_digits(num, sizeof num, inf_i, 3, 1);
        snprintf(buf, sizeof buf, "%s%%", num);
        em_hud_text(gfx, 296.0f, 288.0f, buf, EM_HUD_TEXT_NUM16);
    } else {
        text_placeholder(gfx, 296.0f, 260.0f, 9, 10.0f, 20.0f,
                         kTextWhite);
        int lead = engine_blank_lead(inf_i, 3);
        text_placeholder(gfx, 296.0f + 16.0f * (float)lead, 288.0f,
                         (3 - lead) + 1, 16.0f, 16.0f,
                         kTextWhite);   /* "NN" + "%" */
    }
}

/* DECOR pass — only when assets/ui.emui is loaded (missing asset = the
 * exact pre-decor frame). Queue order mirrors the engine hub drawer
 * func_00209DF0: pager-diamond markers (arcs, untextured queue), then
 * the textured sprites (title/legend/icons — their queue flushes after
 * the untextured one, so the icons composite over the marker rings the
 * way the engine's later draw does), then the profile text (glyph
 * queue, flushes last). */
static void decor(EmGfx *gfx)
{
    s_decor_active = ui_ensure(gfx);
    if (!s_decor_active) return;

    /* Page-selector diamond around (432,320): markers bottom/right/top/
     * left (= stick hover ids 1/2/3/4), each = white fading disc r0-16
     * + two gradient rings.
     *
     * MARKER POSITIONS CONFIRMED against func_00209DF0 (BYTE-MATCHED):
     * its i = 0..3 loop writes the trio D_00265270/2D0/330 to
     * 16*(w + 0x700) / 16*((h >> 1) + 0x790) with (w, h) =
     * (0x1B0,0x178), (0x1DC,0x140), (0x1B0,0x108), (0x184,0x140) —
     * canvas (432,376), (476,320), (432,264), (388,320), in that order.
     * The same loop keys its highlight on `i == arg0[0x11] - 1`, and
     * arg0[0x11] is exactly the byte func_0020D930 writes (see the nav
     * block), so marker index i == hover id i+1.
     *
     * HOVER RECOLOUR CORRECTED: the port used to recolour BOTH rings.
     * func_00209DF0 writes colour only into the D_00265330 block —
     * D_00265354/58 (quad 0 G,B) and D_00265364/68 (quad 1 G,B), plus
     * the mirrored 0x265374/78 and 0x265384/88 — leaving D_00265270 and
     * D_002652D0 with their static colours (it only re-anchors them).
     * So only the OUTER ring changes state; the inner ring stays blue.
     * The values themselves are read straight out of that function:
     * idle (G,B) = (128,255) and (64,64), hovered (240,0) and (200,0) —
     * the kRingIn/kRingOut and kRingHovIn/kRingHovOut pairs below.  (The
     * R and A components are not written there and remain the port's
     * assumption.) */
    static const float kMarker[4][2] = {
        { 432.0f, 376.0f },   /* hover 1 (bottom) -> DATABASE */
        { 476.0f, 320.0f },   /* hover 2 (right)  -> SPR4     */
        { 432.0f, 264.0f },   /* hover 3 (top)    -> MAP      */
        { 388.0f, 320.0f },   /* hover 4 (left)   -> ITEM     */
    };
    for (int i = 0; i < 4; i++) {
        const float  cx = kMarker[i][0], cy = kMarker[i][1];
        const int    hov = (s_hover == i + 1);
        const float *rin  = hov ? kRingHovIn  : kRingIn;
        const float *rout = hov ? kRingHovOut : kRingOut;
        em_gfx_overlay_arc4(gfx, cx, cy,  0.0f, 16.0f, 0.0f, 360.0f,
                            kDiscWhite, kDiscFade, kDiscWhite, kDiscFade);
        em_gfx_overlay_arc4(gfx, cx, cy, 10.0f, 12.0f, 0.0f, 360.0f,
                            kRingIn, kRingOut, kRingIn, kRingOut);
        em_gfx_overlay_arc4(gfx, cx, cy, 14.0f, 16.0f, 0.0f, 360.0f,
                            rin, rout, rin, rout);
    }

    /* Textured decor sprites — every anchored .emui record carries its
     * sheet UVs AND its audited canvas anchor, drawn 1:1 white-modulated
     * (sentinel records — sheet-only and the backdrop tile — skip; the
     * backdrop is drawn by background_render underneath everything). */
    for (uint32_t i = 0; i < s_ui.sprite_count; i++) {
        const UiSprite *s = &s_ui.sprites[i];
        if (s->x <= BACKDROP_XY) continue;       /* both sentinels */
        em_gfx_overlay_sprite(gfx, (float)s->x, (float)s->y,
                              (float)s->dw, (float)s->dh,
                              (float)s->u, (float)s->v,
                              (float)(s->u + s->w), (float)(s->v + s->h),
                              kSpriteWhite);
    }

    /* Profile bio block under the title art (strings from the engine's
     * label table 0x267290 entries 5..8; name blue 12x16 at (16,56),
     * gray 10x10 rows at (24,74/86/98), 4x6 blue slant-tick stand-ins
     * (drawn square) at x=18 beside each row). Needs the font. */
    if (font_ensure(gfx)) {
        em_hud_text(gfx, 16.0f, 56.0f, "DENNIS RILEY",
                    EM_HUD_TEXT_NAME12_BLUE);
        em_hud_text(gfx, 24.0f, 74.0f, "BIRTHDAY     :10.25.1981",
                    EM_HUD_TEXT_PROFILE10);
        em_hud_text(gfx, 24.0f, 86.0f, "HEIGHT/WEIGHT:5'11\"/154lbs",
                    EM_HUD_TEXT_PROFILE10);
        em_hud_text(gfx, 24.0f, 98.0f, "NATIONALITY  :USA",
                    EM_HUD_TEXT_PROFILE10);
        em_gfx_overlay_rect(gfx, 18.0f,  78.0f, 4.0f, 6.0f, kMarkerBlue);
        em_gfx_overlay_rect(gfx, 18.0f,  90.0f, 4.0f, 6.0f, kMarkerBlue);
        em_gfx_overlay_rect(gfx, 18.0f, 102.0f, 4.0f, 6.0f, kMarkerBlue);
    }
}

/* PAGE VIEW (skeleton) — the entered sub-screen. Draws the page's
 * exported background/decor records (assets/ui_pageN.emui, produced by
 * the decomp repo's tools/export_ui.py --page N from the user's own
 * extract/ chunks) at their recorded anchors; records exported
 * sheet-only (x = -32768, no statically known canvas position) are
 * skipped. Pages without an asset — and page content itself — render
 * as a clearly flagged placeholder: the dark panel fill and the amber
 * CONTENT TBD strip are deliberate non-authentic markers, not guesses
 * at the real layout. */
static void page_render(EmGfx *gfx, int page, const EmPlayerStatus *st)
{
    UiSheet *ui = slot_ensure(gfx, page);
    int      partial = 0;       /* page 0: real interior rows drawn */

    if (ui) {
        for (uint32_t i = 0; i < ui->sprite_count; i++) {
            const UiSprite *s = &ui->sprites[i];
            if (s->x <= BACKDROP_XY) continue;   /* sheet-only/backdrop */
            em_gfx_overlay_sprite(gfx, (float)s->x, (float)s->y,
                                  (float)s->dw, (float)s->dh,
                                  (float)s->u, (float)s->v,
                                  (float)(s->u + s->w),
                                  (float)(s->v + s->h), kSpriteWhite);
        }
    } else {
        /* No asset: flagged placeholder fill + title-area block. */
        em_gfx_overlay_rect(gfx, 8.0f, 8.0f, 496.0f, 432.0f, kPagePanel);
        text_placeholder(gfx, 8.0f, 0.0f, 8, 16.0f, 64.0f / 4.0f,
                         kTextWhite);
    }

    /* ITEM page (0) basic interior — the engine's category hub
     * (view func_0020EE50, drawer func_0020F2A0) titles its banners
     * with the message bank's group-1 entries; the port draws those
     * REAL labels (first in-entry line of each) as a list. Row order
     * and positions are ASSUMED (the engine's banner anchors exported
     * sheet-only). Item COUNTS: the engine keeps a per-type u8 count
     * array (D_00810C64, FINDINGS "INVENTORY LOCATED") which the port
     * does not model yet — only the ammo/battery state in
     * EmPlayerStatus exists, so exactly two item rows render, flagged:
     *  - BATTERY ITEMS: the carried pack as the bank's catalog name
     *    (group 3 lines 27/28/29 = 6/18/24 GAUGE, picked by the pack's
     *    display capacity) + its charge "cur/max";
     *  - EQUIPMENT ITEMS: "SPR4 MAGAZINE" (PORT LABEL — the engine's
     *    catalog has no magazine entry; ammo types are "\" placeholder
     *    lines) x full-magazine equivalents = reserve / 30 (the
     *    engine tracks the pack count separately in D_00810C63; this
     *    is a derived stand-in). */
    if (page == 0 && st && em_hud_font_ready()) {
        static const int kCatRows[5] = { 0, 1, 2, 4, 3 };  /* BATTERY,
            EQUIPMENT, EVENT, HEALING, MAIN MENU (group-1 lines;
            display order ASSUMED) */
        float y = 88.0f;
        for (int i = 0; i < 5; i++) {
            const char *cat = msg_line(1, (uint32_t)kCatRows[i]);
            if (!cat) break;                 /* bank missing: no rows */
            partial = 1;
            /* label = the entry's first '\n' line */
            char label[40];
            size_t n = 0;
            while (cat[n] && cat[n] != '\n' && n < sizeof label - 1) {
                label[n] = cat[n];
                n++;
            }
            label[n] = '\0';
            em_hud_text(gfx, 48.0f, y, label, EM_HUD_TEXT_TALL);
            y += 24.0f;
            char row[48];
            row[0] = '\0';
            if (kCatRows[i] == 0) {          /* BATTERY ITEMS */
                const char *pk =
                    st->battery_max ==  6 ? msg_line(3, 27) :
                    st->battery_max == 18 ? msg_line(3, 28) :
                    st->battery_max == 24 ? msg_line(3, 29) : NULL;
                char name[28] = "BATTERY PACK";   /* fallback */
                if (pk) {
                    size_t m = 0;
                    while (pk[m] && pk[m] != '\n' && m < sizeof name - 1) {
                        name[m] = pk[m];
                        m++;
                    }
                    name[m] = '\0';
                }
                snprintf(row, sizeof row, "%s x01 %02u/%02u", name,
                         (unsigned)st->battery,
                         (unsigned)st->battery_max);
            } else if (kCatRows[i] == 1) {   /* EQUIPMENT ITEMS */
                /* 2026-06-11 pickup decode: with the inventory array
                 * present (st->items = em_pickup_items(), the
                 * D_00810C64 mirror) the row shows the REAL catalog
                 * name (message-bank group 4 entry 0x10's name line —
                 * group 3's 0x10 is a placeholder glyph) and the real
                 * per-type count[0x10] (= the D_00810C63 pack
                 * counter's value through case 0x10). The derived
                 * reserve/30 stand-in stays the array-less fallback. */
                const char *nm = msg_line(4, 0x10);
                char name[28] = "SPR4 MAGAZINE";   /* PORT LABEL fallback */
                const char *p = nm ? strchr(nm, '\n') : NULL;
                if (p) {                           /* skip "Found:" */
                    size_t m = 0;
                    p++;
                    while (p[m] && p[m] != '\n' && m < sizeof name - 1) {
                        name[m] = p[m];
                        m++;
                    }
                    name[m] = '\0';
                }
                int mags = st->items ? st->items[0x10]
                         : st->reserve > 0 ? st->reserve / 30 : 0;
                snprintf(row, sizeof row, "%s x%02d", name, mags);
            }
            if (row[0]) {
                em_hud_text(gfx, 64.0f, y, row, EM_HUD_TEXT_NUM16);
                y += 24.0f;
            }
            y += 16.0f;
        }
    }

    /* Amber flag strip — page interiors are placeholder (CONTENT TBD)
     * or, on the ITEM page with the bank loaded, deliberately partial
     * (only the modeled ammo/battery rows). */
    {
        const float strip[4] = { kTbdAmber[0], kTbdAmber[1], kTbdAmber[2],
                                 0.25f };
        em_gfx_overlay_rect(gfx, 128.0f, 392.0f, 256.0f, 24.0f, strip);
        if (em_hud_font_ready()) {
            char label[48];
            if (partial)
                snprintf(label, sizeof label,
                         "PARTIAL: AMMO/BATTERY ONLY");
            else
                snprintf(label, sizeof label, "%s - CONTENT TBD",
                         kPageNames[page]);
            float w = em_hud_text_width(label, EM_HUD_TEXT_TALL);
            em_hud_text(gfx, 256.0f - w * 0.5f, 394.0f, label,
                        EM_HUD_TEXT_TALL);
        }
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

    /* The REAL background (the engine's animated UI background,
     * func_0020A7A0) when the active sheet — the hub's ui.emui or the
     * entered page's ui_pageN.emui — carries a BACKDROP record: black
     * base + the three animated tile layers through the em_gfx backdrop
     * queue, under every panel. No record (old/missing asset) => the
     * flagged translucent scene-dim fallback. */
    if (!background_render(gfx,
                           slot_ensure(gfx, s_page >= 0 ? s_page
                                                        : SLOT_HUB)))
        em_gfx_overlay_rect(gfx, 0.0f, 0.0f, EM_GFX_STATUS_W,
                            EM_GFX_STATUS_H, kSceneDim);

    /* Entered page (stick hover + X on the hub diamond): the page view
     * replaces the hub composition entirely, exactly like the engine's
     * controller state 3. */
    if (s_page >= 0) {
        /* s_frames is the engine's counter[8], bumped by func_00208AD0
         * (byte-matched, first statement).
         *
         * CORRECTED (2026-07, third audit pass): the note here used to
         * say func_00209DF0 is the ONLY caller of func_00208AD0, so an
         * entered page never bumps the counter.  That is FALSE —
         * func_0020AE40 (byte-matched) also calls
         * func_00208AD0(ctx, 0x1B6, 0x6E) plus
         * func_00209280(ctx, 0x96, 0xB4, ..., 1), and func_0020AE40 is
         * the sub-page HUD strip drawer invoked from the page tasks
         * (func_00214570 / func_00215870 / func_00217090 / func_002177B0
         * / func_00217FA0 / func_00218640 / func_00218D90).  So the real
         * pages DO re-draw a repositioned health gauge + battery block
         * and the sweep keeps turning while a page is open.
         *
         * Neither is modelled: the port's page views are placeholders,
         * and it is not established which of the four pages the port
         * exposes route through func_0020AE40.  s_frames is therefore
         * left frozen here rather than guessing — UNRESOLVED, not a
         * decoded behaviour. */
        page_render(gfx, s_page, st);
        em_gfx_overlay_canvas(gfx, EM_GFX_OVERLAY_W, EM_GFX_OVERLAY_H);
        return;
    }

    /* Decor: title art, button legend, page icons, pager diamond,
     * profile block — only with assets/ui.emui (see decor above). */
    decor(gfx);

    /* Count-up display copies (0x810858/0x81085C semantics). */
    float hp  = count_up(&s_disp_health, st->health);
    float inf = count_up(&s_disp_infection, st->infection);

    /* Help panel (128,336)-(384,432) — exact. The help LINE on it is
     * the real engine text (message bank group 0, hub_help_line above),
     * drawn tall-font white at the engine's anchor: func_001FCA10
     * passes (0x8A, 0xA8) = canvas x 138, field y 168 -> canvas y 336.
     * Needs both assets/messages.emsg and the font; missing either
     * queues nothing (the bare panel, the pre-bank frame). */
    em_gfx_overlay_rect(gfx, 128.0f, 336.0f, 256.0f, 96.0f, kHelpPanel);
    {
        int line = hub_help_line(inf);
        if (line >= 0)
            msg_text(gfx, 138.0f, 336.0f, msg_line(0, (uint32_t)line),
                     EM_HUD_TEXT_TALL);
    }

    /* PHASE CORRECTED (audit): func_00208AD0 (BYTE-MATCHED) opens with
     * `counter[8] = counter[8] + 1;` and only THEN forms
     * `180 + 12 * ((counter[8] >> 1) % 30)` — the bump precedes the
     * draw, so the first drawn frame already uses counter 1.  The port
     * bumped after the draws and so ran the whole rotation one frame
     * out of phase with the original (180,180,192,192,... instead of
     * 180,192,192,204,...). */
    s_frames++;

    battery_block(gfx, st->battery, st->battery_max);
    spr4_block(gfx, st->reserve);
    health_gauge(gfx, hp, st->health_max);
    infection_block(gfx, inf);

    em_gfx_overlay_canvas(gfx, EM_GFX_OVERLAY_W, EM_GFX_OVERLAY_H);
}

/* GAME-OVER + CONTINUE PRESENTATION — the FLAGGED module stand-ins
 * (em_hud.h: the flow chain is decoded s66/s70; only the two screen
 * modules' ART is unexported). Both draw on the default 640x448
 * overlay canvas, queued by em_game BEFORE the fade rect so the fade
 * machine owns them exactly like the engine's GS fade. */

/* Screen module 0x27 stand-in: the GAME OVER art screen the wait
 * state launches.  CONFIRMED against func_001AD4E0 (BYTE-MATCHED):
 * state 0 seeds the u16 at slot+0x18 to 0xF0 = 240 frames, state 1
 * calls func_001FF080(0, 0x27) (module 0x27), and state 3 leaves only
 * when `D_0028A9A0 == 0 && (counter == 0 || (D_00810E74 & 0x40))` —
 * a 240-frame hold that one button edge can cut short.  The art
 * itself is unexported, so the black + title below is a stand-in. */
void em_hud_game_over(EmGfx *gfx)
{
    static const float kBlack[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    if (!gfx) return;
    /* opaque base — hides the frozen dead world underneath (the
     * engine's module owns the whole frame) */
    em_gfx_overlay_rect(gfx, 0.0f, 0.0f, EM_GFX_OVERLAY_W,
                        EM_GFX_OVERLAY_H, kBlack);
    if (!em_hud_font_ready()) return;   /* font-less: black only */
    {
        const char *title = "GAME OVER";
        float w = em_hud_text_width(title, EM_HUD_TEXT_TALL_DARKRED);
        em_hud_text(gfx, (EM_GFX_OVERLAY_W - w) * 0.5f, 200.0f, title,
                    EM_HUD_TEXT_TALL_DARKRED);
    }
}

/* Screen module 1 stand-in: the continue prompt (decoded flow; the
 * option LABELS are port guesses — em_hud.h).
 *
 * THREE options, cursor 0..2 — CONFIRMED in func_001AC480 (NEARMISS,
 * body-correct).  Its case 2 reads the selector at ctx+0xF:
 *   forward edge: `if ((int)ctx[0xF] < 2) { cue 5; ctx[0xF]++; }`
 *   back edge:    `if (*pf != 0)          { (*pf)--; cue 5; }`
 * so the selector is bounded to 0..2 and the menu is exactly three
 * options.  The clamp below mirrors that bound rather than dropping
 * the highlight.  (Also decoded there, for whoever wires em_game: the
 * initial selector is 0 normally but 1 when D_00275BDC is set, and the
 * idle counter ctx[0xB] is seeded to 0x4B0 = 1200 frames and only
 * ticks down while no button is held.) */
void em_hud_continue(EmGfx *gfx, int cursor)
{
    static const float kBlack[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    static const char *kOpts[3]  = { "CONTINUE", "LOAD GAME",
                                     "OPTIONS" };
    if (!gfx) return;
    if (cursor < 0) cursor = 0;
    if (cursor > 2) cursor = 2;
    em_gfx_overlay_rect(gfx, 0.0f, 0.0f, EM_GFX_OVERLAY_W,
                        EM_GFX_OVERLAY_H, kBlack);
    if (!em_hud_font_ready()) return;   /* font-less: black only */
    {
        const char *title = "CONTINUE?";
        float w = em_hud_text_width(title, EM_HUD_TEXT_TALL);
        em_hud_text(gfx, (EM_GFX_OVERLAY_W - w) * 0.5f, 150.0f, title,
                    EM_HUD_TEXT_TALL);
    }
    for (int i = 0; i < 3; i++) {
        /* highlighted option = tall white, the rest tall gray (port
         * styling — the module's real highlight is unexported) */
        EmHudTextStyle st = (i == cursor) ? EM_HUD_TEXT_TALL
                                          : EM_HUD_TEXT_TALL_GRAY;
        float w = em_hud_text_width(kOpts[i], st);
        em_hud_text(gfx, (EM_GFX_OVERLAY_W - w) * 0.5f,
                    220.0f + 40.0f * (float)i, kOpts[i], st);
    }
}

/* FOUND LINE — PORT STAND-IN, FLAGGED (em_hud.h; 2026-06-11 pickup
 * decode). The engine auto-opens the status screen at the collected
 * item's record (D_008106B0/B1 -> func_001AE7E0 mode 2); its text is
 * message-bank group 4: "Found:\n<NAME>\n<description>", indexed by
 * item TYPE. The port draws the "Found: <NAME>" composite as a
 * transient line over gameplay instead. */
#define FOUND_FRAMES 150          /* ~2.5 s — PORT cadence */

static int s_found_type  = -1;
static int s_found_timer = 0;

void em_hud_found_show(int item_type)
{
    s_found_type  = item_type;
    s_found_timer = FOUND_FRAMES;
}

/* --- RADIO/EXAMINE MESSAGE MACHINE (engine mode 2 — em_hud.h) --------
 *
 * CONFIRMED against func_001FCA10 (BYTE-MATCHED) and func_001FD950
 * (NEARMISS, body-correct):
 *   - func_001FCA10 dispatches on D_002821B4 and, in sub-state 2,
 *     `if (D_002821BC > 0) D_002821BC--; else if (func_001FDB80(0,2)
 *     == 1) D_002821B4 = 2;` — the machine ends by posting mode 2, and
 *     it reads no pad anywhere.  Timer-only dismissal, as claimed.
 *   - func_001FD950 picks the bank by bit 31 of the record's field
 *     0x34: set -> D_0028A4E8[0] (the GLOBAL/examine slot), clear ->
 *     D_0028A594[0].  It measures the first TWO segments with
 *     func_001CC170 and draws at `x = 0x100 - (max >> 1)`, `y = 0xC2`
 *     — i.e. centred on 256 of the 512-wide canvas, at the same
 *     half-height y units the rest of the UI uses (RADIO_Y below).
 *     It then does `if (rec[0x6C] > 0) { ...; rec[0x6C]--; return 0; }`
 *     — the full string is re-emitted every frame for the record's
 *     duration.  No typewriter, no panel, no pad read.
 *
 * The engine machine (struct at D_002821B0, ticked by func_001FCA10
 * mode 2 -> func_001FDB80 -> func_001FD950): each frame increments the
 * frame counter, draws the line's FULL text (no per-char reveal exists
 * anywhere in the chain — func_001FE070/func_001FC7B0 render the whole
 * string), and decrements the record duration; when it expires, the
 * record index advances to the terminal record (dur 0, wait-stream 1)
 * whose bank line is the next — EMPTY — line, func_001FD950 finishes
 * it in one bookkeeping frame, and the machine posts done
 * (D_002821B4 = 2). Per-record flag-mailbox writes (D_008106D4[idx])
 * exist for script handshakes, but every GLOBAL record's flag byte is
 * 0xFF (none) — not modeled. */

/* Per-line durations — the GLOBAL record table D_00264DD0[0] = 0x272DF0
 * (8-byte records {u16 dur, s16 voice_cue, u8 flag_idx, u8 wait_stream}).
 * PROVENANCE: these are DATA values read out of the user's local ELF,
 * NOT recovered C — the machine that consumes them (func_001FD950,
 * NEARMISS) is decompiled, the table contents are a capture.  Lines
 * 0/2/4/8/0xE = 148 frames, 6/0xA/0xC = 118.  All six jtbl_0026E1A0
 * locked-door lines are covered (sel 0..5 -> lines 6/0/2/8/0xA/4); every
 * cue byte in the captured prefix is -1 = silent.  Lines past that
 * prefix use the table's common 148 (FLAGGED default). */
static int radio_duration(int line)
{
    switch (line) {
    case 0x6: case 0xA: case 0xC: return 118;
    default:                      return 148;
    }
}

#define RADIO_GROUP 9       /* the slot-0x16 bank rides messages.emsg as
                             * group 9 (tools/export_ui.py --messages) */
/* func_001FD950 passes y = 0xC2 = 194 to the blitter.  UPGRADED from
 * DERIVED to READ: func_001FC770 (BYTE-MATCHED) forwards straight to
 * func_001FC7B0 (NEARMISS, body-correct), whose draw is
 *   func_001CC1E0(1, base + 0x700, arg1 + 0x790, 0xA, 0x14, buf, cfg[5])
 * — the y argument is added to 0x790 with NO halving, so it is already
 * in the GS half-height units, and canvas y = 194 * 2 = 388: near the
 * bottom of the 448-tall canvas, where a subtitle belongs.  (The same
 * call fixes the tall 10x20 cell and confirms x is plain canvas: the
 * pen is base + 0x700.) */
#define RADIO_Y     388.0f

static struct {
    int active;     /* the engine's D_002821B4 == 1 window */
    int line;       /* GLOBAL bank line (bit-31 word low bits) */
    int left;       /* frames left on the text record (+0x6C) */
    int shown;      /* text-record display frames so far (+0x68) */
    int total;      /* last completed message's display frames */
} s_radio;

void em_hud_radio(int line_id)
{
    s_radio.active = 1;
    s_radio.line   = line_id;
    s_radio.left   = radio_duration(line_id);
    s_radio.shown  = 0;
    printf("hud: radio/examine message line %d (%d frames)\n",
           line_id, s_radio.left);
}

int em_hud_radio_active(void)
{
    return s_radio.active;
}

int em_hud_radio_frames(void)
{
    return s_radio.active ? s_radio.shown : s_radio.total;
}

/* One machine tick + draw (called every gameplay frame from
 * em_hud_found_render — the close-out's transient-text hook). The
 * TIMER always runs (engine truth — em_door's locked finish blocks on
 * it like the pumped op09 native); the draw needs the bank + font and
 * skips while the status screen owns the display. */
static void radio_tick_render(EmGfx *gfx)
{
    if (!s_radio.active) return;
    if (s_radio.left <= 0) {
        /* terminal record: dur 0, wait-stream 1 — the stream flags
         * (D_00282155/156) are idle for text-only lines, so one
         * bookkeeping frame and the machine reports done */
        s_radio.active = 0;
        s_radio.total  = s_radio.shown;
        return;
    }
    s_radio.left--;
    s_radio.shown++;
    if (!gfx || em_hud_visible()) return;   /* timer ran; no draw */
    const char *str = msg_line(RADIO_GROUP, (uint32_t)s_radio.line);
    if (!str || !str[0] || !em_hud_font_ready()) return;

    /* centering (func_001FD950): measure the first TWO '\n' segments
     * (func_001CC170) and center the WIDER one on the 512-px canvas —
     * x = 256 - max(w0, w1)/2; every line draws at the same x. */
    float wmax = 0.0f;
    {
        const char *p = str;
        for (int i = 0; i < 2; i++) {
            char  seg[96];
            size_t n = 0;
            while (p[n] && p[n] != '\n' && n < sizeof seg - 1) {
                seg[n] = p[n];
                n++;
            }
            seg[n] = '\0';
            float w = em_hud_text_width(seg, EM_HUD_TEXT_TALL_GRAY);
            if (w > wmax) wmax = w;
            while (p[n] && p[n] != '\n') n++;
            if (!p[n]) break;
            p += n + 1;
        }
    }
    em_gfx_overlay_canvas(gfx, EM_GFX_STATUS_W, EM_GFX_STATUS_H);
    msg_text(gfx, 256.0f - wmax * 0.5f, RADIO_Y, str,
             EM_HUD_TEXT_TALL_GRAY);
    em_gfx_overlay_canvas(gfx, EM_GFX_OVERLAY_W, EM_GFX_OVERLAY_H);
}

void em_hud_found_render(EmGfx *gfx)
{
    /* the mode-2 radio/examine machine pumps every frame, found line
     * or not (this function is the close-out's per-frame text hook) */
    radio_tick_render(gfx);

    if (s_found_timer <= 0 || !gfx) return;
    if (em_hud_visible()) return;        /* the menu owns the screen */
    s_found_timer--;
    if (!em_hud_font_ready()) return;

    char line[64];
    const char *e = msg_line(4, (uint32_t)s_found_type);
    const char *p = e ? strchr(e, '\n') : NULL;
    if (p) {
        /* "Found:" + the entry's NAME line */
        char name[40];
        size_t m = 0;
        p++;
        while (p[m] && p[m] != '\n' && m < sizeof name - 1) {
            name[m] = p[m];
            m++;
        }
        name[m] = '\0';
        snprintf(line, sizeof line, "Found: %s", name);
    } else {
        snprintf(line, sizeof line, "Found: ITEM %02X",
                 (unsigned)s_found_type & 0xFF);
    }
    float w = em_hud_text_width(line, EM_HUD_TEXT_TALL);
    em_hud_text(gfx, (EM_GFX_OVERLAY_W - w) * 0.5f, 384.0f, line,
                EM_HUD_TEXT_TALL);
}

/* AREA-TITLE CARD — the opening "FORT STEWART - REAR ENTRANCE" placard.
 * A one-shot, top-centred title riding the gameplay HUD frame
 * INDEPENDENTLY of the cinematic director.
 *
 * CORRECTED against func_001C5930 (the area-title actor behaviour;
 * NEARMISS, body-correct):
 *   - state 0 seeds the show counter `*(short *)(arg0 + 0x28) = 0x12C`
 *     = 300 frames, and state 1 draws the string then decrements it,
 *     stopping when it reaches 0.  The card therefore runs FIVE
 *     seconds at 60 Hz — the port's 24+150+36 = 210-frame envelope was
 *     invented.
 *   - the draw is one unconditional
 *       func_001CC1E0(1, 0x800 - (w >> 1), 0x7A2, 0xA, 0x14, str, 0)
 *     per frame.  Nothing in the function varies alpha, so there is NO
 *     fade-in/fade-out here; the port's envelope was invented too.  If
 *     a fade is observed on hardware it comes from a global fade pass,
 *     not from this card — do not re-add one here without evidence.
 *   - the anchor decodes as canvas (256 - w/2, 36): x 0x800 - 0x700 =
 *     256 is the centre of the 512-wide UI canvas (the port centres on
 *     its own canvas, which is equivalent), and y (0x7A2 - 0x790) * 2 =
 *     36 — the port was drawing at 96.
 *
 * NOT MODELLED (read in the same function, no port data) — and the old
 * description of it was wrong.  After the first 300 frames (arg0[5]
 * reaches 1) the engine runs a SECOND 300-frame line at canvas x
 * 0x896 - 0x700 - w/2 = 406 - w/2 on the same row 36, from
 * D_0026726C[band].  `band` is NOT a sub-location id: it is
 * func_001C5860() (BYTE-MATCHED), which buckets 100.0f - D_008104D8 —
 * the INFECTION value — into 0..5 by the descending thresholds
 * 80/50/30/10/0.  The line is skipped entirely while band == 0 (i.e.
 * infection under 20), and the 300-frame timer restarts whenever the
 * band changes, so it is an infection-status placard, not a place name.
 *
 * ALSO NOT MODELLED: the first line's draw is gated on the scratchpad
 * game-mode byte 0x70003B8D being 2 or 3; outside that the counter
 * still runs but nothing is drawn.  The port's equivalent gate is the
 * "status screen owns the UI" check in the render below.
 *
 * STRING SOURCE — DOWNGRADED.  func_001C5930 does not read a
 * 32-byte-stride table at 0x00273B80; it indexes a POINTER array
 * D_002671C0[] with `D_00289B40[D_00810700][0] + D_00810701` (per-area
 * base + sub-area byte).  The literal area-11 text below is retained as
 * an OBSERVED capture, not as a decoded table lookup. */
#define AREA_TITLE_FRAMES   300   /* func_001C5930: 0x12C, DECODED       */
#define AREA_TITLE_Y        36.0f /* canvas y from GS 0x7A2, DECODED     */

/* The area-title string. OBSERVED (see above), not table-decoded: only
 * the AREA-11 opening line is known; NULL = no card for that area. */
static const char *area_title_string(int area)
{
    switch (area) {
    case 11: return "FORT STEWART - REAR ENTRANCE";  /* OBSERVED */
    default: return NULL;
    }
}

static struct {
    const char *str;   /* the title string (NULL = inactive) */
    int         t;     /* frames elapsed since arm */
} s_area_title;

void em_hud_area_title(int area)
{
    const char *str = area_title_string(area);
    if (!str) return;                 /* no observed string -> never shows */
    s_area_title.str = str;
    s_area_title.t   = 0;
    printf("hud: AREA-TITLE CARD armed — \"%s\" (area %d, %d-frame card)\n",
           str, area, AREA_TITLE_FRAMES);
}

int em_hud_area_title_active(void)
{
    return s_area_title.str != NULL;
}

/* Draw a TALL-font string at (x, y) modulated to `alpha` — the fade-card
 * path (em_hud_text uses the style's fixed alpha). Mirrors em_hud_text's
 * tall branch: proportional advance, first `advance` texel columns. */
static void title_text_alpha(EmGfx *gfx, float x, float y, const char *str,
                             float alpha)
{
    if (!str || !font_ensure(gfx)) return;
    const float cell_h = kTextStyles[EM_HUD_TEXT_TALL].cell_h;
    const float rgba[4] = { 1.0f, 1.0f, 1.0f, alpha };
    for (; *str; str++) {
        unsigned char    c = (unsigned char)*str;
        if (c < 0x20) continue;
        const FontGlyph *g = font_glyph(FONT_TALL, c);
        float adv = g ? (float)g->advance : 9.0f;
        if (g)
            em_gfx_overlay_glyph(gfx, x, y, adv, cell_h,
                                 g->u, g->v, g->u + adv, g->v + g->h,
                                 rgba);
        x += adv;
    }
}

void em_hud_area_title_render(EmGfx *gfx)
{
    if (!s_area_title.str) return;
    if (s_area_title.t >= AREA_TITLE_FRAMES) {  /* one-shot done */
        s_area_title.str = NULL;
        return;
    }
    s_area_title.t++;
    if (!gfx) return;
    if (em_hud_visible()) return;          /* the status screen owns the UI */
    if (!em_hud_font_ready()) return;      /* no font -> queue nothing       */

    /* Constant opacity for the whole 300-frame run — func_001C5930
     * issues the same func_001CC1E0 call every frame with no alpha
     * term.  (title_text_alpha is kept so the draw path stays the
     * tall-font one; the alpha it is handed is simply 1.)
     *
     * CANVAS CORRECTED (audit): the card goes out through the SAME
     * blitter as the radio/examine text — func_001CC1E0(1, x + 0x700,
     * y + 0x790, 0xA, 0x14, ...), read in func_001FC7B0 (NEARMISS) —
     * so its x is a coordinate on the engine's 512-wide UI canvas, not
     * on the 640-wide gameplay overlay.  func_001C5930 centres it with
     * `cx = 0x800 - (w >> 1)` = canvas 256 - w/2.  The port was
     * laying it out on the 640x448 overlay canvas, which stretches to
     * the same screen: every glyph came out 512/640 = 0.8x too narrow
     * (and the whole line 20% short) next to the radio text, which
     * already selects the 512 canvas.  Height is unaffected (both
     * canvases are 448 tall), so AREA_TITLE_Y stays 36.
     *
     * COLOUR still UNRESOLVED: func_001C5930 passes 0 as the style
     * argument (no style record), and func_001CC1E0 is hand-written
     * asm in the decomp — what "no record" resolves to is not readable
     * from recovered C.  White is the port's stand-in. */
    em_gfx_overlay_canvas(gfx, EM_GFX_STATUS_W, EM_GFX_STATUS_H);
    float w = em_hud_text_width(s_area_title.str, EM_HUD_TEXT_TALL);
    title_text_alpha(gfx, (EM_GFX_STATUS_W - w) * 0.5f, AREA_TITLE_Y,
                     s_area_title.str, 1.0f);
    em_gfx_overlay_canvas(gfx, EM_GFX_OVERLAY_W, EM_GFX_OVERLAY_H);
}
