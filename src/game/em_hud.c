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

/* Pager-diamond marker colors (engine arc blocks 0x265270 disc r0-16,
 * 0x2652D0 ring r10-12, 0x265330 ring r14-16 — inner->outer radial
 * gradients read from the live param blocks; idle BLUE state, hovered
 * GREEN state — FINDINGS "STATUS SCREEN LAYOUT" item 2). */
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
    [EM_HUD_TEXT_NUM16]        = { FONT_SMALL, 12.0f, 16.0f,
                                   { 1.0f, 1.0f, 1.0f, 1.0f } },
    [EM_HUD_TEXT_NUM16_RED]    = { FONT_SMALL, 12.0f, 16.0f,
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
 * each '\n'. Line advance 24 px = the engine's func_001FE070 newline
 * step ((D_00264CD8 20 + D_00264CE0 4) / 2 field lines = 24 canvas
 * px). No-op without the font, like em_hud_text. */
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

/* Status-screen visibility — hidden by default (the original shows no
 * persistent HUD), flipped by a Triangle OR Start edge (both buttons
 * open the same screen — verified identical memory diff, FINDINGS). */
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

/* --- page navigation (FINDINGS "STATUS SUB-PAGES", session 31) --------
 *
 * Hub hover = left stick (engine func_0020D930 mode 0): deflection
 * > 0.8, quadrant -> ctx +0x11: 1 down, 2 right, 3 up, 4 left; releases
 * back to 0 (hover, not latch). The hovered marker's rings swap blue ->
 * green. X (engine internal bit 0x40) enters the hovered page through
 * the controller's REMAP at func_0020CDC0 .L0020D294:
 *
 *     hover 1 (down)  -> page 3  DATABASE SCREEN  (chunk 0x24)
 *     hover 2 (right) -> page 2  SPR4 SCREEN      (chunk 0x2C)
 *     hover 3 (up)    -> page 1  MAP SCREEN       (chunk 0x1E)
 *     hover 4 (left)  -> page 0  ITEM SCREEN      (chunk 0x1F)
 *
 * X with NO hover buzzes (func_0020CD80) and enters nothing. Inside a
 * page, Circle (0x20) returns to the hub (page views' exit path,
 * +0x10 <- 0x63); at the hub, Triangle/Start/Circle (mask 0x830)
 * closes the screen. The port adds Triangle as a page->hub back too
 * (the engine's per-page Triangle behavior is page-specific and not
 * fully decoded; flagged). Pages 4/5 (passcode keypads, chunks
 * 0x25/0x26) are NOT diamond-reachable — only the external request
 * byte D_008106C5 enters them — so the port's nav covers pages 0-3. */
static int s_hover = 0;     /* 0 none, 1 down, 2 right, 3 up, 4 left */
static int s_page  = -1;    /* -1 = hub, 0..3 = entered page */

static const int kHoverToPage[5] = { -1, 3, 2, 1, 0 };

static const char *kPageNames[4] = {
    "ITEM SCREEN", "MAP SCREEN", "SPR4 SCREEN", "DATABASE SCREEN"
};

/* Hub help line — the engine's selection in func_0020CDC0: a hover
 * shows the hovered page's name (group-0 lines: 1 down -> 0 DATABASE
 * SCREEN, 2 right -> 9 SPR4 SCREEN, 3 up -> 2 MAP SCREEN, 4 left -> 1
 * ITEM SCREEN); idle shows the infection-graded diary line keyed on
 * v = 100 - (int)displayed-infection (.L0020D1AC thresholds): v == 100
 * -> NO line (D_002821B4 = 0), v >= 0x51 -> 4, >= 0x33 -> 5, >= 0x1F
 * -> 6, >= 0xB -> 7, > 0 -> 8, else (infection 100) -> 3 "Dennis
 * Infected". Returns the group-0 line id, or -1 = no help text. */
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

/* Frame counter while visible — drives the highlight rotation (6 deg per
 * frame = the engine's 12 deg per 2 frames = 1 s per revolution). */
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
         * UNLESS the door-transit MENU lock holds (the engine's open
         * poll func_001AE7E0 returns 0 while the fade machine is not
         * idle / scripted mode runs; the press is simply dropped, no
         * latch). The lock clears at fade-in completion, so the menu
         * already works mid arrival-walk-out, exactly like the
         * original (em_door.h "THE TWO LOCKS"). */
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

    /* Hub: Triangle/Start/Circle (engine edge mask 0x830) closes. */
    if (in->pressed & (EM_PAD_TRIANGLE | EM_PAD_START | EM_PAD_CIRCLE)) {
        s_shown = 0;
        s_hover = 0;
        return;
    }

    /* Stick hover among the pager diamonds (engine func_0020D930
     * mode 0: deflection > 0.8, atan2 quadrant; raw bytes are
     * 0x80-centered, 0x00 = left/up). */
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

    /* X enters the hovered page (no hover = the engine buzzes and
     * enters nothing). */
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
     * + two gradient rings — blue idle, GREEN while stick-hovered (the
     * engine's live hover state, FINDINGS item 2). */
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
                            rin, rout, rin, rout);
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
        page_render(gfx, s_page, st);
        s_frames++;
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

    battery_block(gfx, st->battery, st->battery_max);
    spr4_block(gfx, st->reserve);
    health_gauge(gfx, hp, st->health_max);
    infection_block(gfx, inf);

    s_frames++;

    em_gfx_overlay_canvas(gfx, EM_GFX_OVERLAY_W, EM_GFX_OVERLAY_H);
}

/* GAME-OVER PRESENTATION — PORT STAND-IN, FLAGGED (em_hud.h; the
 * engine's DATA.DAT game-over screen module + its dead-player trigger
 * are undecoded — em_game's PLAYER DAMAGE & DEATH block). Draws on
 * the default 640x448 overlay canvas, queued by em_game AFTER the
 * fade rect so it reads over the hold-black frame. */
void em_hud_game_over(EmGfx *gfx, int frames)
{
    static const float kBlack[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    if (!gfx) return;
    /* opaque base — the frame is at full subtractive black already;
     * this pins the presentation even if something glows through */
    em_gfx_overlay_rect(gfx, 0.0f, 0.0f, EM_GFX_OVERLAY_W,
                        EM_GFX_OVERLAY_H, kBlack);
    if (!em_hud_font_ready()) return;   /* font-less: black only */
    {
        const char *title = "GAME OVER";
        float w = em_hud_text_width(title, EM_HUD_TEXT_TALL_DARKRED);
        em_hud_text(gfx, (EM_GFX_OVERLAY_W - w) * 0.5f, 200.0f, title,
                    EM_HUD_TEXT_TALL_DARKRED);
    }
    /* blinking prompt: 32-frame cycle, lit half (a PORT cadence —
     * no engine reference exists for this stand-in) */
    if ((frames >> 4) & 1) {
        const char *prompt = "PRESS START";
        float w = em_hud_text_width(prompt, EM_HUD_TEXT_LABEL12);
        em_hud_text(gfx, (EM_GFX_OVERLAY_W - w) * 0.5f, 250.0f, prompt,
                    EM_HUD_TEXT_LABEL12);
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

void em_hud_found_render(EmGfx *gfx)
{
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
