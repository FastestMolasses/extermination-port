/* em_hud.h — native STATUS SCREEN: the legacy player status display that
 * the scene coordinator opens and closes (see "STATUS OPEN/CLOSE" below).
 *
 * FAITHFULNESS (FINDINGS.md "STATUS SCREEN LAYOUT", session-25 full
 * draw-chain decode + the earlier "INVENTORY LOCATED" captures): the
 * original game shows NO persistent HUD during play. The status display
 * is a status SCREEN opened by TWO buttons, modelled here as TRIANGLE
 * or START, with no separate pause menu. The port mirrors that: hidden
 * by default; since S11b the original frame machine opens and closes it
 * (em_hud_status_open / em_hud_status_tick).
 *
 * PROVENANCE (corrected): this used to read "verified: both route to
 * the same controller, func_0020CDC0". func_0020CDC0 (the status-screen
 * controller anim_frame_top_b runs in state 3) is now NEARMISS readable
 * C, but it is not what classifies the buttons. That is
 * func_001AE7E0 (NEARMISS, body-correct), the mode classifier: it
 * returns 2 = "enter status mode" for `(D_00810E74 & 0x800) ||
 * (D_00810E74 & 0x10)` — two distinct edge bits of one button word —
 * and, separately, for `D_008106C5 != 0 || D_008106B0 != 0` (external
 * request byte / posted pickup). So "two buttons, one screen, plus an
 * external auto-open" is source-derived; the identification of those
 * two bits as TRIANGLE and START is inference from the sibling walk
 * func_001AC480 (0x4000 forward, 0x1000 back, 0x840 confirm) and is
 * FLAGGED. The close mask "0x830" formerly quoted here came from the
 * stub and has been removed.
 *
 * The screen composes on the engine's 512x448 UI canvas (GS offsets
 * 0x700/0x790), which em_hud selects via em_gfx_overlay_canvas for its
 * draws and restores afterwards. Faithful hub elements rendered today:
 * the HEALTH ring gauge at (208,196) (the byte-matched func_00208AD0 is
 * called as func_00208AD0(ctx, 0xD0, 0xC4) and maps a canvas point to
 * GS as (x + 0x700, (y >> 1) + 0x790), so the centre is (208, 196) —
 * "197" was a one-pixel guess) with the <=35 red bg-ring state and the
 * stepped 1 s rotating highlight (12 deg every 2 frames).
 *
 * RING FILL CORRECTED (2026-07 audit): the port used to draw a
 * light-blue arc r24-56 GROWING from 180 deg with health. func_00208AD0
 * re-uses ONE arc block (D_00265390) for two of the three ring passes
 * and writes the pair (180, 540) for the first and (-180 + 3.6*hp, 180)
 * for the second; the two rotating-highlight blocks in the same
 * byte-matched function get (ang - 60, ang) and (ang, ang + 60) at the
 * identical offsets, which settles the pair as (start, end). So the
 * second pass is the DEPLETED span 180 + 3.6*hp .. 540, and it shrinks
 * to nothing at full health. Two further corrections (2026-07, second
 * audit pass): the sweep divides by the LITERAL 100.0f, NOT by the
 * displayed maximum (the port rescaled it and so drew a full ring under
 * the infected 60-cap), and the arc is the LAST primitive the function
 * submits — after the rotating highlight, which it therefore paints
 * over (the port drew it first).
 *
 * BLOCK SPLIT CORRECTED (2026-07, THIRD audit pass): the second pass's
 * radii/colour were then read off the WRONG block. func_00208AD0 writes
 * the health gradient into D_00265410..D_0026544C, which is
 * D_002653F0 + 0x20..+0x5C — the gradient belongs to arc block
 * D_002653F0, a SEPARATE prim drawn after D_00265390, not to the block
 * the depleted pass re-uses. FINDINGS.md item 7 has both blocks' static
 * data from the same VRAM capture the r36-56 figure came from:
 * D_002653F0 = r36-56 (the coloured ring), D_00265390 = r24-56 light
 * blue (0,153,255,128). So the gauge is a light-blue TRACK annulus
 * r24-56, the coloured ring r36-56 over its outer half, the highlight,
 * and then the track colour re-painted over the depleted span — the
 * dark "unlit" stand-in and the r36-56 depleted radii are both gone.
 * Drawn through the em_gfx annular-arc
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
 *    inside the ring gauge; it does NOT dim a live gameplay frame. With
 *    assets/ui.emui present the port now renders the REAL background:
 *    the engine's animated UI background (FINDINGS.md "STATUS SCREEN
 *    BACKGROUND", the universal drawer func_0020A7A0) — a black base
 *    frame (the UI-camera scene stand-in) under THREE animated layers
 *    of the screen's 128x64 background tile (two 256x128-px tilings,
 *    one scrolling left 0.5 px/frame, one scrolling up 1 px/frame with
 *    a sin-pulsed alpha, plus the periodic full-screen "zoom burst"),
 *    composited through the em_gfx backdrop queue so it sits UNDER all
 *    panels. The hub uses the ui.emui BACKDROP record (TBP 0x1E40);
 *    each entered page uses its own ui_pageN.emui BACKDROP record (the
 *    engine passes a per-screen tile token). Without a BACKDROP record
 *    (old/missing asset) the full-screen dim rect remains the flagged
 *    fallback. The ROTATING PLAYER MODEL is RENDERED (FINDINGS.md
 *    "STATUS SCREEN UI SCENE", func_0020E6F0 — BYTE-MATCHED, and every
 *    figure below is CONFIRMED in it): em_game draws the UI-camera 3D
 *    scene (black backplate + the player at the engine's view-space
 *    transform with the yaw spin `*(float *)(arg0 + 0xC4) += 0.01f`
 *    per frame, wrapping past pi by -2pi; clip 0x1C2 = 450 when
 *    D_00810858 > 35 and 0xA = 10 otherwise; the infection tint
 *    +0x80/0x84/0x88 scaled by the 1.0<->1.3 breathe ramp at +0x38,
 *    stepped 0.01/frame, with +0x84 clamped at -127)
 *    in place of the world whenever the screen is visible AND the
 *    active sheet carries a BACKDROP record; em_hud_scene_3d() tells
 *    background_render to skip its opaque base fill so the player
 *    shows between the black frame and the translucent tile layers —
 *    the engine's exact draw order (3D scene, background, panels).
 *    Needs a player.emdl exported with menu clips 450,10 appended to
 *    the recorded --clips list; an older asset falls back to the
 *    breathing idle pose (flagged in the loader printout).
 *  - The ring's rotating highlight is additive (blend mode 1) on the
 *    GS; the overlay pass is alpha-blend only, so it approximates with
 *    white at low alpha.
 *  - TEXT renders through the REAL game fonts when assets/font.emfn is
 *    present (produced by the decomp repo's tools/export_font.py from
 *    the user's own EE-RAM dump; format documented there and in the
 *    loader below). The engine streams 1bpp glyphs from EE RAM into a
 *    PSMT4 strip at GS block 0x1B00 and draws one batched sprite
 *    (FINDINGS.md "UI FONT"); the port pre-bakes the same glyphs into
 *    one RGBA8 sheet and draws per-glyph textured overlay quads —
 *    bilinear + modulate, the strip's GS state. Both engine fonts are
 *    carried: the 12x20 tall font (proportional advance, func_001CBE10)
 *    and the 16x16 small font (fixed cell, GS-scaled to 12 px labels /
 *    12x16 numbers). WITHOUT the asset, every label and number falls
 *    back to the old positioned placeholder rect at the real glyph
 *    metrics — a missing font never regresses the frame.
 *  - DECOR renders through the REAL game textures when assets/ui.emui
 *    is present (produced by the decomp repo's tools/export_ui.py from
 *    the user's own GS-VRAM dump — FINDINGS.md "STATUS SCREEN UI
 *    TEXTURES"): the "MAIN" title art at (16,0), the 128x128 button
 *    legend at (0,320) (the session-25 audit's "portrait" slot — the
 *    texture is the triangle-EXIT/circle-BACK/cross-OK legend), and the
 *    four 32x32 page-arrow icons at (11,304)/(484,304)/(417,10)/
 *    (417,406). With the asset present the hub also composes the
 *    PAGER-DIAMOND markers around (432,320) (white fading disc r0-16 +
 *    blue gradient rings r10-12/r14-16 per marker, the engine's arc
 *    blocks 0x265270/0x2652D0/0x265330, idle blue — hover/green needs
 *    page input, not yet modeled) and the PROFILE bio block ("DENNIS
 *    RILEY" 12x16 blue at (16,56); gray 10x10 rows at (24,74/86/98);
 *    4x6 blue ticks at x=18) when the font is also loaded. WITHOUT
 *    ui.emui none of this queues — the frame is identical to the
 *    pre-decor build (missing asset = no regression). Engine blend
 *    mode 3 on the icons is approximated with standard alpha blend.
 *  - PAGE NAVIGATION (FINDINGS.md "STATUS SUB-PAGES", session 31) is
 *    modeled as a skeleton: left-stick hover among the pager diamonds
 *    (deflection > 0.8, quadrant -> hover 1 down / 2 right / 3 up /
 *    4 left, engine func_0020D930; the hovered marker's rings render
 *    the engine's GREEN state), X enters the hovered page through the
 *    controller's remap (func_0020CDC0: down->DATABASE, right->SPR4,
 *    up->MAP, left->ITEM; X with no hover enters nothing — the engine
 *    buzzes), Circle or Triangle returns to the hub, and at the hub
 *    Triangle/Start/Circle (engine edge mask 0x830) closes the screen.
 *    An entered page draws its exported background/decor textures from
 *    assets/ui_pageN.emui (decomp repo tools/export_ui.py --page N,
 *    run by the user against their own extract/ chunks; page 0 ITEM =
 *    chunk 0x1F, 1 MAP = 0x1E, 2 SPR4 = 0x2C, 3 DATABASE = 0x24) at
 *    the recorded anchors — title art is asm-anchored at (8,0);
 *    background-tile/legend anchors are flagged ASSUMED in the
 *    exporter; sheet-only records (no statically known position) are
 *    skipped. Page INTERIORS (item lists, map cursor, weapon
 *    customization, database records) are mostly NOT modeled: every
 *    page view carries an amber flag strip ("CONTENT TBD", or
 *    "PARTIAL: AMMO/BATTERY ONLY" on the ITEM page when its basic
 *    interior renders), and a missing page asset falls back to a
 *    flagged placeholder panel. The engine's passcode keypad pages 4/5
 *    (chunks 0x25/0x26, entered only via the external request byte
 *    D_008106C5, never from the diamond) are not reachable in the
 *    port either.
 *  - MESSAGE-BANK TEXT renders when assets/messages.emsg is present
 *    (decomp repo tools/export_ui.py --messages, run by the user
 *    against their own extract/chunk00; the engine's group/line text
 *    bank, FINDINGS.md "STATUS SUB-PAGES" -> "The message bank"):
 *      - the hub HELP PANEL shows the engine's real help line
 *        (func_0020CDC0 selection): the hovered page's name (group-0
 *        lines 0/9/2/1 for hover down/right/up/left) or, idle, the
 *        infection-graded diary line (100−infection thresholds
 *        0x51/0x33/0x1F/0xB; infection 0 = no line, 100 = "Dennis
 *        Infected"), tall font at the engine's (138,336) anchor,
 *        24 px '\n' steps;
 *      - the ITEM page renders a basic real interior: the engine's
 *        category labels (bank group 1 — BATTERY/EQUIPMENT/EVENT/
 *        HEALING ITEMS + MAIN MENU; row layout ASSUMED) plus the only
 *        item counts the port models: the carried battery pack (bank
 *        catalog name by capacity, charge from EmPlayerStatus) and
 *        "SPR4 MAGAZINE" x reserve/30 (PORT LABEL, derived count —
 *        the engine's per-type inventory array D_00810C64 is not
 *        translated yet; everything else stays flagged).
 *    Missing bank (or font) => none of this queues — frames identical
 *    to the pre-bank build.
 *  - Not yet composed: the page-tab strips, the spinning cyan double
 *    ring + sparkle emitter (animated; cadence unverified), page
 *    sub-screen interiors (see above), the rotating player model.
 *
 * EmPlayerStatus mirrors the engine's canonical status storage:
 *
 *   health      player actor +0x220 (0x008104D0), float — " 75 / 100"
 *               (display max swaps to 60 under flag 0x8104E4 — not
 *               modeled yet)
 *   infection   player actor +0x228 (0x008104D8), float — " 60%"
 *   mag         D_00810C62, u8 — rounds in the current SPR4 magazine.
 *               NOT shown on the status hub (the real screen has no
 *               magazine display); kept for the future page-2 view.
 *   reserve     D_00810CB4, s16 — SPR4 reserve rounds (the "120")
 *   battery     the status screen's "04/06" pair in DISPLAY units.
 *               Engine storage is HALF-units: current at 0x810CB2,
 *               max at 0x810CB7, both shown >>1; the segment bar draws
 *               one square per half-unit (battery * 2). The 0x810C7F
 *               gate covers ONLY the caption + segment grid — the 8x8
 *               marker and the "BATTERY" label draw unconditionally in
 *               func_00209280 (corrected 2026-07; the port used to hide
 *               the whole block). The port stands in for that byte with
 *               battery_max != 0. Because EmPlayerStatus carries the
 *               DISPLAY units, an odd half-unit count is lost: the port
 *               draws battery*2 squares where the engine draws the raw
 *               0x810CB2 (a known, flagged model loss).
 *
 * NUMBER FORMATTING — CONFIRMED against func_001C5FB0 (NEARMISS,
 * body-correct), the single formatter every readout goes through (see
 * engine_digits() in em_hud.c). Its loop runs exactly arg1 places off a
 * divisor table, and its suppression test is
 *   `if (arg2 && !started && digit == 0 && i != arg1 - 1) *p++ = 0x20;`
 * — so a set blank flag writes SPACES for the leading zeros, the last
 * place always prints, and a clear flag zero-pads. Verified at all four
 * call sites: health func_001C5FB0(hp, 3, 1) in func_00208AD0
 * (BYTE-MATCHED); reserve func_001C5FB0(D_00810CB4, 4, 1) in
 * func_00209860; infection func_001C5FB0(inf, 3, 1) followed by the
 * '%'-string concat in func_00209DF0 (BYTE-MATCHED); battery
 * func_001C5FB0(0x810CB2>>1, 2, 0) + separator +
 * func_001C5FB0(0x810CB7>>1, 2, 0) in func_00209280 — blank flag CLEAR
 * on both halves, so the battery pair really is zero-padded.
 * That is why "BATTERY 04/06" pads with zeros while the health row
 * reads " 75 / 100" — the port previously guessed zero-padding for
 * health and left-packing for reserve/infection.
 *
 * The GLYPH CELL each of those four readouts draws in is a separate
 * question and was also wrong; see the style table below for the cells
 * actually read out of the four call sites.
 *
 * EM_HUD_FORCE=1 in the environment (checked once, on first render call)
 * forces the status screen VISIBLE regardless of the toggle — for
 * headless capture tests of the overlay itself. Hidden is the default,
 * so the default frame is already the status-screen-free one.
 * EM_HUD_PAGE=<0..3> (with FORCE) starts with that page entered;
 * EM_HUD_HOVER=<1..4> (with FORCE) holds that pager hover at the hub —
 * both are capture hooks; without them navigation only changes on
 * input, so the forced hub capture is byte-identical to pre-nav builds.
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
    const uint8_t *items; /* D_00810C64 mirror: u8 count per item TYPE
                           * (em_pickup_items(), 256 entries). NULL
                           * keeps the pre-inventory derived rows; set,
                           * the ITEM page's magazine row reads
                           * items[0x10] — the REAL pack count
                           * (2026-06-11 pickup decode) */
} EmPlayerStatus;

/* Text styles — each pairs one of the two engine fonts with the glyph
 * cell + style color the status screen uses (style records 0x265510..).
 *
 * GLYPH CELLS CORRECTED (2026-07 audit): every cell below is now the
 * literal (w, h) pair the engine hands func_001CBA50 at the matching
 * call site, read out of the recovered C — the port previously used one
 * 12x16 "NUM16" cell for four readouts that the engine draws at three
 * different sizes:
 *   health value / '/' / max  func_00208AD0 (BYTE-MATCHED): 0xC, 0xC
 *   battery caption "04/06"   func_00209280 (NEARMISS):     16, 16
 *   SPR4 reserve count        func_00209860 (NEARMISS):     0x10, 0x10
 *   infection "NN%"           func_00209DF0 (BYTE-MATCHED): 0x10, 0x10
 *   "DENNIS RILEY"            func_00209DF0 (BYTE-MATCHED): 0xC, 0x10
 *   profile bio rows          func_00209DF0 (BYTE-MATCHED): 0xA, 0xA
 *   every LABEL row           func_00208AD0/9280/9860:      0xC, 0xC
 *
 *   LABEL12      small font, 12x12 cell, white  — "HEALTH", "BATTERY"...
 *   NUM12        small font, 12x12 cell, white  — health row (" 75 / 100")
 *   NUM12_RED    small font, 12x12 cell, red    — low-health value
 *   NUM16        small font, 16x16 cell, white  — battery / reserve /
 *                                                 infection readouts
 *   NUM16_RED    small font, 16x16 cell, red    — (no engine call site
 *                                                 known; kept for callers)
 *   TALL         tall font, 1:1 (h 20), white   — "INFECTION"
 *   TALL_DARKRED tall font, 1:1 (h 20), dark red— "INFECTED"
 *   NAME12_BLUE  small font, 12x16 cell, blue   — "DENNIS RILEY"
 *                (style 0x265538; the marker/name blue 0,96,206)
 *   PROFILE10    small font, 10x10 cell, gray   — profile bio rows
 *                (style 0x265530; gray 80,80,80)
 *   TALL_GRAY    tall font, 1:1 (h 20), gray    — radio/examine text
 *                (the DEFAULT text color D_0026EC10[0] = 0x606060,
 *                reset by func_001FC9B0; the slot-0x16 bank carries no
 *                markup records, so the default is what shows) */
typedef enum {
    EM_HUD_TEXT_LABEL12,
    EM_HUD_TEXT_NUM12,
    EM_HUD_TEXT_NUM12_RED,
    EM_HUD_TEXT_NUM16,
    EM_HUD_TEXT_NUM16_RED,
    EM_HUD_TEXT_TALL,
    EM_HUD_TEXT_TALL_DARKRED,
    EM_HUD_TEXT_NAME12_BLUE,
    EM_HUD_TEXT_PROFILE10,
    EM_HUD_TEXT_TALL_GRAY
} EmHudTextStyle;

/* Draw `str` at (x, y) on the current overlay canvas in `style`, through
 * the loaded font sheet (em_gfx_overlay_glyph). Glyph rule = the
 * engine's: index = ascii - 0x20, '$' remaps to 0x89 (tall) / 0x87
 * (small); small-font advance = the style's cell width, tall advance =
 * the per-glyph proportional table. No-op (queues nothing) while the
 * font asset is missing — callers keep their placeholder fallback. */
void em_hud_text(EmGfx *gfx, float x, float y, const char *str,
                 EmHudTextStyle style);

/* Same glyph metrics, with original packed 0xBBGGRR message color.
 * Used by recovered tag2 text spans; GS color128 is unity. */
void em_hud_text_color(EmGfx *gfx, float x, float y, const char *str,
                        EmHudTextStyle style, uint32_t rgb);

/* A recovered independent page temporarily owns the shared decor slot.
 * Invalidate after replacing it so the next hub/page draw reloads its
 * sheet. Background state remains shared across page transitions. */
void em_hud_decor_invalidate(void);
void em_hud_background_sprite(EmGfx *gfx, float u, float v,
                              float width, float height);

/* Pixel width `str` would occupy in `style` (the engine's func_001CC170
 * centering helper). 0 while the font asset is missing. */
float em_hud_text_width(const char *str, EmHudTextStyle style);

/* Original 001FD950 centering and 001CC3B0 five-pass outlined tall text.
 * First two lines determine the common x; markup top_skew is in pixels.
 * rgb/outline are original packed 0xBBGGRR colors, not invented styling. */
void em_hud_subtitle(EmGfx *gfx, const char *str, float y, float line_height,
                     float top_skew, uint32_t rgb, uint32_t outline);

/* Is the font sheet loaded? (placeholder rects are the fallback) */
int em_hud_font_ready(void);

/* STATUS OPEN/CLOSE (S11b; SCENE_COORDINATOR_DESIGN.md section 5,
 * "Status"). The screen no longer toggles itself. The scene coordinator
 * runs the original frame machine: 001AE7E0 returns 2 on a START/
 * TRIANGLE edge (D_00810E74 & 0x810) or a B0/C5 request, unless B8, B9,
 * the fade, 3B8D or the menu-inhibit byte B3 blocks it; the r == 2 arm
 * then calls 0020E060 and frame-machine state 3 calls 0020CDC0 every
 * frame (world frozen) until it returns nonzero, then state 5 returns to
 * state 1. Until WP-5 binds those positions to em_status_runtime, the
 * bindings (em_scene_bindings.c) bind them to this legacy screen:
 *   em_hud_status_open  = the 0020E060 position: shows the screen at the
 *                         hub (the original clears its 0xA0-byte status
 *                         block D_00810130 there);
 *   em_hud_status_tick  = the 0020CDC0 position: this frame's legacy
 *                         navigation; returns 1 when the hub closed
 *                         (TRIANGLE/START/CIRCLE, the original hub's 0x830
 *                         edge mask), else 0, and -1 when the screen is
 *                         not open (0020CDC0 is only reached while it is).
 * The legacy page views and their content remain the port's (H9, WP-5).
 *
 * The menu-inhibit byte B3 (D_008106B3) is canonical in EmSceneState; its
 * original writer is the player spine's tail (func_0015BA50, BYTE-MATCHED),
 * which clears it and sets it to 1 when ANY of D_008106F1 != 0, the
 * knockdown word *(short *)(p + 0x276) != 0, the anim id p[0x1F0] == 0x33,
 * D_00810CB6 != 0, or p[4] == 2 && p[5] in {0xB..0xF} && p[0x1F1] == 1.
 * The port writes it at the player stage (em_player_frame.c). */
void em_hud_status_open(void);
int em_hud_status_tick(const EmFrameInput *in);

/* EM_HUD_FORCE debug hook: while the forced screen shows and the real one
 * is closed, apply EM_HUD_PAGE/EM_HUD_HOVER and the legacy navigation.
 * No effect without EM_HUD_FORCE=1. */
void em_hud_forced_update(const EmFrameInput *in);

/* GAME-OVER + CONTINUE PRESENTATION — the FLAGGED module stand-ins
 * for the two screens whose FLOW is decoded (s66/s70, em_game's PLAYER
 * DAMAGE & DEATH block doc); only the screens' ART is unexported:
 *
 *   em_hud_game_over  = screen module 0x27 — CONFIRMED in
 *     func_001AD4E0 (BYTE-MATCHED): state 1 calls
 *     func_001FF080(0, 0x27), state 0 seeds the u16 at slot+0x18 to
 *     0xF0 = 240 frames, and state 3 exits on
 *     `D_0028A9A0 == 0 && (counter == 0 || (D_00810E74 & 0x40))` —
 *     a 240-frame hold that one button edge cuts short. It draws an
 *     opaque black base + "GAME OVER" centered in the tall font (dark red —
 *     the engine's INFECTED text style, the only red tall style it
 *     ships). No prompt line: the engine screen carries none we know
 *     of (the hold is skippable by the 0x40 edge bit — calling that bit
 *     CROSS is the same flagged inference as the open bits above, and
 *     the skip is silent). The old blinking
 *     "PRESS START" invention is retired with the START-restart.
 *
 *   em_hud_continue   = screen module 1 — CONFIRMED: func_001AC480
 *     state 0 calls func_001FF080(0, 1), and func_001AC070 is the
 *     machine that drives it. Opaque black base +
 *     "CONTINUE?" header + the 3 prompt options. Option LABELS are
 *     port guesses (module text undecoded, FLAGGED): "CONTINUE" /
 *     "LOAD GAME" / "OPTIONS". `cursor` = the highlighted option
 *     (tall white; others tall gray).
 *
 *     The WALK and the DISPATCH are CONFIRMED (func_001AC480 and
 *     func_001AC070, both NEARMISS / body-correct):
 *       - func_001AC480 is the menu walk. Its case 2 moves the
 *         selector at ctx+0xF forward only under
 *         `if ((int)ctx[0xF] < 2)` and back only under
 *         `if (*pf != 0)` — so it is exactly 3 options, 0..2, and
 *         em_hud_continue clamps to that. Also read there, for
 *         whoever wires em_game's machine rather than this presenter:
 *         the confirm edge (mask 0x840) fires cue 0x5DD/0x5DE/0x5DF
 *         by slot and each move fires cue 5; the initial selector is
 *         0, or 1 when D_00275BDC is set; and ctx[0xB] is seeded to
 *         0x4B0 = 1200 frames (20 s at 60 Hz) and decrements only
 *         while no button is held, dropping the menu out on zero.
 *       - func_001AC070 case 2 dispatches the confirmed selector
 *         (re-read in the recovered C: on func_001AC480 returning 1 it
 *         reads the selector GS[0xF] and sets its own state GS[8]):
 *         0 -> state 4 = func_001AB790(func_001ACEC0), the gameplay
 *         task, with the "loaded a save" flag D_00275BE0 CLEAR;
 *         1 -> func_00225A00() (resets the 212-byte save/load context
 *         at D_00810040), D_00275BE0 = 1, state 5 = the
 *         func_00225AC0(0) card poll — its return 2 runs func_001AF150
 *         and lands on state 4, the SAME gameplay task, flag SET
 *         (return 1 = cancel, back to state 2, this menu);
 *         2 -> state 6 = func_00200A40(), the sub-screen, which
 *         returns to state 2 (this menu) when it reports nonzero.
 *
 * Missing font asset: the black base only. Both are queued by em_game
 * BEFORE the fade rect — the fade machine owns the screens exactly
 * like the engine. */
void em_hud_game_over(EmGfx *gfx);
void em_hud_continue(EmGfx *gfx, int cursor);

/* FOUND LINE (2026-06-11 pickup decode — em_pickup.h): in the ENGINE a
 * collected item posts D_008106B0/B1 and the main-mode controller
 * func_001AE7E0 AUTO-OPENS the status screen at the item's record,
 * whose text is message-bank group 4 — the literal "Found:\n<NAME>\n
 * <description>" entries, indexed by item TYPE. The port's page
 * interiors are CONTENT TBD, so the stand-in (FLAGGED) is a transient
 * in-world line: em_hud_found_show(type) arms it and
 * em_hud_found_render draws "Found: <NAME>" (the group-4 entry's name
 * line, tall font, centered low) for ~2.5 s of gameplay frames.
 * Hidden while the status screen is open; missing bank falls back to
 * "Found: ITEM <type>"; missing font queues nothing. Call _render once
 * per frame from the close-out (after em_hud_render). It is also the
 * per-frame tick + draw hook of the RADIO/EXAMINE message machine
 * below. */
void em_hud_found_show(int item_type);
void em_hud_found_render(EmGfx *gfx);

/* AREA-TITLE CARD — the opening "FORT STEWART - REAR ENTRANCE" placard
 * (INVESTIGATION_area11_director.md §4.4 / FINDINGS s81). In the ENGINE
 * the title rides the gameplay HUD frame (func_001AE5E0 -> func_001AFD70
 * -> func_001C5930), INDEPENDENT of the cinematic director.
 *
 * CORRECTED against func_001C5930 (NEARMISS, body-correct):
 *   - it is a 300-frame card, not ~2.5 s: state 0 sets
 *     `*(short *)(arg0 + 0x28) = 0x12C` and state 1 draws-then-
 *     decrements, stopping at zero. Five seconds at 60 Hz.
 *   - there is NO fade. The whole draw is one
 *     `func_001CC1E0(1, 0x800 - (w >> 1), 0x7A2, 0xA, 0x14, str, 0)`
 *     per frame, with no alpha term anywhere in the function. The
 *     port's fade-in/hold/fade-out envelope was invented and is gone.
 *   - the anchor decodes to canvas (256 - w/2, 36) on the 512-wide UI
 *     canvas — top-centred, but at y 36, not the port's old 96.
 *     CANVAS CORRECTED (later audit): that "512-wide UI canvas" is
 *     load-bearing and the port was ignoring it. The card's blitter is
 *     the one func_001FC7B0 (NEARMISS) shows —
 *     func_001CC1E0(1, x + 0x700, y + 0x790, 0xA, 0x14, str, 0) — the
 *     same one the radio/examine text uses, so em_hud_area_title_render
 *     now selects EM_GFX_STATUS_W/H like the radio path does instead of
 *     centring on the 640-wide gameplay overlay (which drew every glyph
 *     0.8x too narrow). y is unchanged: both canvases are 448 tall.
 *   - STRING SOURCE DOWNGRADED: the function does NOT read a 32-byte-
 *     stride table at 0x00273B80. It indexes the pointer array
 *     D_002671C0[] with `D_00289B40[D_00810700][0] + D_00810701`
 *     (per-area base + sub-area byte). The area-11 text the port
 *     carries is an OBSERVED capture, not a decoded table entry.
 *   - NOT MODELLED (read in the same function), and RE-READ: once the
 *     300 frames elapse a SECOND 300-frame line draws centred on canvas
 *     x 406, row 36, from D_0026726C[band].  It is NOT a sub-location
 *     name: `band` is func_001C5860() (BYTE-MATCHED), which buckets
 *     100 - infection (D_008104D8) into 0..5 at 80/50/30/10/0, the line
 *     is skipped while band == 0, and its timer restarts on a band
 *     change — an infection-status placard.
 *
 * em_hud_area_title(area) arms the card ONCE for the given area on scene
 * entry (em_game's scene-load hook). Only area 11 has a known string;
 * any other area is a silent no-op (the card never shows). em_hud_area_
 * title_render(gfx) counts the 300 frames and draws the centred title at
 * constant opacity for one show, then clears itself. It is NOT a
 * persistent HUD (one-shot) and does NOT touch the status screen or the
 * cinematic letterbox. Hidden while the status screen is open; missing
 * font queues nothing (no regression). Call _render once per frame from
 * the close-out (after em_hud_found_render).
 *
 * em_hud_area_title_active() = the card is still showing (for tests). */
void em_hud_area_title(int area);
void em_hud_area_title_render(EmGfx *gfx);
int  em_hud_area_title_active(void);

/* RADIO/EXAMINE MESSAGE MACHINE — the engine's mode-2 message machine
 * (D_002821B0 = 2; FINDINGS.md "RADIO-MESSAGE MACHINE DECODED",
 * 2026-06-11): the locked-door "VO", station/corpse examine text and
 * scripted radio subtitles all run through ONE global presenter.
 * CONFIRMED against recovered C: there is NO typewriter — the line's
 * FULL text renders every frame for the record's duration.
 * func_001FE070 (NEARMISS) walks the whole buffer and blits it on
 * every call, and func_001FD950 does
 * `if (rec[0x6C] > 0) { ...; rec[0x6C]--; return 0; }` — one full
 * re-emission per remaining frame. func_001FCA10 (BYTE-MATCHED) ends
 * the run with `else if (func_001FDB80(0, 2) == 1) D_002821B4 = 2;`
 * and reads no pad anywhere, so dismissal really is timer-only:
 *
 *   - text     = the GLOBAL examine bank, asset slot 0x16 — CONFIRMED,
 *                func_001FD950 picks the bank off bit 31 of the
 *                record's field 0x34: set -> D_0028A4E8[0], clear ->
 *                D_0028A594[0] (D_0028A4E8
 *                = extract/chunk03/f14_id16.bin; bit-31 line words) —
 *                exported as .emsg group 9 by tools/export_ui.py
 *                --messages (decomp repo, run against the user's own
 *                extract/);
 *   - position = horizontally centered: CONFIRMED, func_001FD950
 *                measures the first TWO segments with func_001CC170
 *                and draws at `x = 0x100 - (max >> 1)` = 256 -
 *                max/2 on the 512-wide UI canvas, `y = 0xC2` (194).
 *                The doubling of 194 to canvas 388 is now READ, not
 *                derived: func_001FC770 (BYTE-MATCHED) is a passthrough
 *                to func_001FC7B0 (NEARMISS), whose draw is
 *                func_001CC1E0(1, base + 0x700, y + 0x790, 0xA, 0x14,
 *                buf, cfg[5]) — the y is added to 0x790 unhalved, so it
 *                is already in half-height units and canvas y = 388,
 *                and x is plain canvas. The '\n' step IS still
 *                DERIVED — func_001FE070 advances the pen by
 *                `(D_00264CD8 + D_00264CE0) >> 1`, font-metric data
 *                we have not exported; 24 canvas px is the port's
 *                figure, FLAGGED;
 *   - style    = the tall font in the DEFAULT text color 0x606060
 *                (D_0026EC10[0], reset by func_001FC9B0) — 75% gray;
 *   - sound    = NONE (every global record's voice cue is -1; no beep/
 *                static exists anywhere in the machine or its
 *                triggers) and NO panel/backdrop — bare text over the
 *                scene;
 *   - dismiss  = TIMER ONLY (no pad read in the machine): the record's
 *                u16 duration (line 6 "It's locked and won't open." =
 *                118 frames), then one bookkeeping frame on the
 *                terminal empty-line record (dur 0, wait-stream 1) and
 *                the machine reports done (D_002821B4 = 2).
 *
 * em_hud_radio(line) starts the machine on a GLOBAL bank line (a
 * 0x8000000X line word's low bits). The duration table carries the
 * per-line values captured from the DATA table at 0x272DF0 (an ELF
 * read, not recovered C) for the lines the port can reach; unlisted
 * lines use the bank's common 148-frame value (FLAGGED default). The machine runs without assets (the timer
 * semantics are engine truth; missing messages.emsg/font just draws
 * nothing — no regression), so script sequencing (em_door blocks its
 * locked finish on it, like the engine's pumped op09 native) is
 * asset-independent.
 *
 * em_hud_radio_active() = machine still presenting (the engine's
 * D_002821B4 == 1); em_hud_radio_frames() = display frames of the LAST
 * message's text record (EM_LOCKED_TEST asserts 118 for line 6). */
void em_hud_radio(int line_id);
int  em_hud_radio_active(void);
int  em_hud_radio_frames(void);

/* Is the status screen currently shown? (open OR EM_HUD_FORCE) */
int em_hud_visible(void);


/* Will the ACTIVE sheet (hub or entered page) draw the real animated
 * background this frame? (it carries a BACKDROP record — loads the
 * sheet lazily, so callable before em_hud_render). em_game gates the
 * UI-camera 3D scene on this: with no record the screen still uses the
 * translucent scene-dim fallback over the live frame, where a
 * player-only 3D pass would be wrong. */
int em_hud_backdrop_ready(EmGfx *gfx);

/* Tell em_hud whether em_game rendered the UI-camera 3D scene (black
 * backplate + rotating player) this frame, BEFORE em_hud_render: when
 * set, the background skips its opaque black base fill so the player
 * shows through under the translucent tile layers. Cleared/set every
 * frame by the close-out. */
void em_hud_scene_3d(int rendered);

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
