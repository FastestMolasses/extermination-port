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
 *    "STATUS SCREEN UI SCENE", func_0020E6F0): em_game draws the
 *    UI-camera 3D scene (black backplate + the player at the engine's
 *    view-space transform with the decoded 0.01 rad/frame yaw spin,
 *    menu pose clip 0x1C2 / low-health idle 0xA, infection tint pulse)
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
 *               one square per half-unit (battery * 2). Block gated on
 *               0x810C7F != 0 — the port gates on battery_max != 0.
 *
 * NUMBER FORMATTING (DECODED from func_001C5FB0, the single formatter
 * every readout goes through — see engine_digits() in em_hud.c): each
 * readout is a fixed-width field of N places with the number RIGHT-
 * aligned in it. A set blank flag turns the leading zeros into SPACES
 * (the last place always prints), a clear one zero-pads. The call
 * sites decode as: health 3 places blanked (func_00208AD0), reserve 4
 * blanked (func_00209860), infection 3 blanked + '%' (func_00209DF0),
 * battery 2 ZERO-padded on each side of the '/' (func_00209280).
 * That is why "BATTERY 04/06" pads with zeros while the health row
 * reads " 75 / 100" — the port previously guessed zero-padding for
 * health and left-packing for reserve/infection.
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
 * cell + style color the status screen uses (style records 0x265510..):
 *   LABEL12      small font, 12x12 cell, white  — "HEALTH", "BATTERY"...
 *   NUM16        small font, 12x16 cell, white  — numbers ("075 / 100")
 *   NUM16_RED    small font, 12x16 cell, red    — low-health value
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

/* Pixel width `str` would occupy in `style` (the engine's func_001CC170
 * centering helper). 0 while the font asset is missing. */
float em_hud_text_width(const char *str, EmHudTextStyle style);

/* Is the font sheet loaded? (placeholder rects are the fallback) */
int em_hud_font_ready(void);

/* Per-frame toggle poll: a TRIANGLE or START press (edge, in->pressed)
 * flips the status screen's visibility — both buttons open the same
 * screen in the engine. Call once per gameplay frame with the frame
 * input block. */
void em_hud_update(const EmFrameInput *in);

/* MENU INHIBIT — the engine's D_008106B3 byte, rewritten every frame
 * by the player spine (func_0015BA50 tail): nonzero while the player
 * is hit-reacting/dying (state 2 outside the allowed subs, the
 * knockdown anim, the infected latch window...), and the
 * Triangle/Start open press is simply dropped. em_game writes it once
 * per frame from the player damage state (it also covers the
 * game-over/continue screens, where START/CROSS belong to the
 * decoded prompt machine). */
void em_hud_menu_inhibit(int inhibit);

/* GAME-OVER + CONTINUE PRESENTATION — the FLAGGED module stand-ins
 * for the two DECODED screens (s66/s70, em_game's PLAYER DAMAGE &
 * DEATH block doc — the trigger/flow chain is now fully decoded;
 * only the screens' ART is unexported):
 *
 *   em_hud_game_over  = screen module 0x27 (the GAME OVER art the
 *     wait state func_001AD4E0 launches and fades in): an opaque
 *     black base + "GAME OVER" centered in the tall font (dark red —
 *     the engine's INFECTED text style, the only red tall style it
 *     ships). No prompt line: the engine screen carries none we know
 *     of (the hold is CROSS-skippable, silently). The old blinking
 *     "PRESS START" invention is retired with the START-restart.
 *
 *   em_hud_continue   = screen module 1 (the title/continue screen
 *     the machine func_001AC070 launches): opaque black base +
 *     "CONTINUE?" header + the 3 prompt options. Option LABELS are
 *     port guesses (module text undecoded, FLAGGED): "CONTINUE" /
 *     "LOAD GAME" / "OPTIONS". `cursor` = the highlighted option
 *     (tall white; others tall gray).
 *
 *     Everything about the WALK and the DISPATCH is now DECODED:
 *       - func_001AC480 is the menu walk. The selector is a single
 *         byte, moved down only while it is < 2 and up only while it
 *         is nonzero — so it is exactly 3 options, 0..2, and
 *         em_hud_continue clamps to that. Its confirm/move edges also
 *         carry the per-slot cue ids and a 1200-frame (20 s at 60 Hz)
 *         idle timeout that drops the menu out to the attract path —
 *         facts for whoever wires em_game's machine, not this
 *         presenter.
 *       - func_001AC070 case 2 dispatches the confirmed selector:
 *         0 hands off to the gameplay task func_001ACEC0 with the
 *         "loaded a save" flag D_00275BE0 CLEAR; 1 runs
 *         func_00225A00 (which resets the 212-byte save/load context
 *         at D_00810040) and enters the func_00225AC0 card poll — a
 *         successful load lands on the SAME gameplay task with that
 *         flag SET; 2 enters the func_00200A40 sub-screen, which
 *         returns to this menu when it completes.
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
 * -> func_001C5930), INDEPENDENT of the cinematic director: a hardcoded
 * 32-byte-stride string table at 0x00273B80 (AREA 11 -> idx1 = the whole
 * "FORT STEWART - REAR ENTRANCE" string) drawn top-centred as bitmap-font
 * sprites with a staggered fade over a dimmed scene.
 *
 * em_hud_area_title(area) arms the card ONCE for the given area on scene
 * entry (em_game's scene-load hook). Only area 11 has a decoded string;
 * any other area is a silent no-op (the card never shows). em_hud_area_
 * title_render(gfx) runs the fade machine and draws the centred title for
 * one show, then clears itself. It is NOT a persistent HUD (one-shot) and
 * does NOT touch the status screen or the cinematic letterbox. Hidden
 * while the status screen is open; missing font queues nothing (no
 * regression). The fade/hold frame counts are FLAGGED (the engine's exact
 * opening staggered-fade counts are un-stopwatched, doc §F) — a
 * reasonable ~short-fade / ~2.5 s-hold stand-in for live tuning. Call
 * _render once per frame from the close-out (after em_hud_found_render).
 *
 * em_hud_area_title_active() = the card is still showing (for tests). */
void em_hud_area_title(int area);
void em_hud_area_title_render(EmGfx *gfx);
int  em_hud_area_title_active(void);

/* RADIO/EXAMINE MESSAGE MACHINE — the engine's mode-2 message machine
 * (D_002821B0 = 2; FINDINGS.md "RADIO-MESSAGE MACHINE DECODED",
 * 2026-06-11): the locked-door "VO", station/corpse examine text and
 * scripted radio subtitles all run through ONE global presenter.
 * Decode verdict (full .s read of func_001FCA10/001FDB80/001FD790/
 * 001FD950/001FE070/001FC7B0): there is NO typewriter — the line's
 * FULL text renders every frame for the record's duration:
 *
 *   - text     = the GLOBAL examine bank, asset slot 0x16 (D_0028A4E8
 *                = extract/chunk03/f14_id16.bin; bit-31 line words) —
 *                exported as .emsg group 9 by tools/export_ui.py
 *                --messages (decomp repo, run against the user's own
 *                extract/);
 *   - position = horizontally centered: x = 256 - max(line widths)/2
 *                on the 512-wide UI canvas (func_001FD950 measures the
 *                first TWO '\n' segments via func_001CC170); y = field
 *                0xC2 (194) = canvas 388, '\n' step 24 canvas px;
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
 * decoded per-line values of the table at 0x272DF0 for the lines the
 * port can reach; unlisted lines use the bank's common 148-frame value
 * (FLAGGED default). The machine runs without assets (the timer
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

/* Is the status screen currently shown? (toggle state OR EM_HUD_FORCE) */
int em_hud_visible(void);

/* Is the status screen OPEN — the hub or any entered page — by the real
 * Triangle/Start toggle? The PAUSE-GATE query: the engine halts gameplay
 * while its menu is up (flag 0x8106C4 = 1 between open and close), and
 * em_game gates the world simulation on this. Unlike em_hud_visible()
 * this deliberately EXCLUDES the EM_HUD_FORCE capture hook: FORCE is a
 * render-only overlay switch, and the headless overlay captures rely on
 * gameplay still reaching its capture frame underneath. */
int em_hud_is_open(void);

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
