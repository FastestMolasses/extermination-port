# Launcher options (registry)

The finished port will have a **launcher** where players turn on enhancements,
bug fixes, graphics settings, cut content and similar options (user plan,
2026-09-27). This file is the single list of every option decided or proposed
so far, so none is forgotten when the launcher is built. It also holds the
**decisions the user still has to review**.

Rules (from `PORT_PROFILES.md`):

- Every option has an **Original value**. With every option at its Original
  value the port is the Original profile, which is the default and the only
  thing fidelity work measures (`FIDELITY_FEATURES.md`).
- Options never fork the game logic. A gameplay option is a patch applied on
  top of a verified translated function, behind its switch.
- Rendering-only options (resolution, filtering, presentation) are kept apart
  from options that change gameplay (controls, bug fixes, cut content).
- Cut content comes from the disc only, and only entries marked **decoded** in
  the decomp's `docs/CURIOSITIES.md` may become options.

Status tags: **REVIEW** (the user has to decide), **DECIDED** (decided, not
built yet), **BUILT** (implemented behind its switch), **CANDIDATE** (a source
lists it; needs a decision and, for cut content, a decode).

Maintenance: whenever work adds, decides or builds an option, or finds a
candidate (for example an original bug that could get an optional fix), update
this file in the same commit.

---

## Decisions for the user to review

| Option | Choices | Notes | Status |
|---|---|---|---|
| **Field presentation** (Original profile) | (a) line-double each 512x224 field and place it at its correct interlaced height, so the game's half-line draw offset cancels and the image is stable; (b) plain line-doubling (edges shimmer by one line every field); (c) combine each field with the previous one (sharper on still scenes, combs on motion) | Pure platform-layer presentation after the GS framebuffer: the game code and the frame bytes stay exact with any choice, and a future PS2 build is unaffected. The user wants to see the looks side by side before choosing; this may become a launcher option. Measured facts: decomp `docs/CAPTURES_C7.md` 5/5b. The first frames the port draws as a real 512x224 GS buffer are the load veil's (2026-09-27, `em_gfx_gs_frame`); they are shown spread over the 4:3 rectangle, nearest-neighbour, like every other GS-mapped draw, until this is decided. | REVIEW |

---

## Timing and authenticity

| Option | Original value | Other values | Notes | Status |
|---|---|---|---|---|
| **PS2 disc-drive timing** | Off: the disc answers at host speed (loads, module loads and streamed audio) | On: the drive timing measured from the PCSX2 recordings (C7 VOICELAT model), so voiced lines start and end on the PS2's frames | User decision 2026-09-27 ("make it a switch"). Built 2026-09-27: `EmSettings.ps2_disc_drive_timing` in `src/em_settings.h` (the one settings struct; `em_settings_original` holds 0), read at launch from the environment (`EM_PS2_DISC_DRIVE_TIMING=1` turns it on) until the launcher sets it; `em_stream_live` applies it to the IOP stream backend (`em_iop_stream_set_ps2_drive_timing`). Off: every stream read is done at the first query after its issue; on: the model in `IOP_STREAM.md` "Drive model". Off moves first control 10 frames earlier than on (newgame-control locked_ticks 1301 against 1311) and makes each voiced line's read 6 fields shorter than the recording's (the drive's 7 fields against host speed's 1), so the line keys on and ends 6 fields earlier. The level smoke checks both: `make test-level-smoke-full` (off) and `make test-level-smoke-ps2-drive` (on), `LEVEL_SMOKE.md` "The stream drive's two modes". Since chain C8b LOADER (2026-09-28) it also drives the screen-module loader (`em_module_loader`, MODULE_LOADER.md 1.7): off, module 0x21 (the BATTERY page) loads in the loader's 10 host-speed dispatches; on, its two reads take the recorded busy fields and the load the captured 24 frames (the level smoke's `check_module_load` compares both). Every other module read is host speed either way (no recording; counted as unmeasured): since chain step PAGELOADS (2026-09-30) that includes every other status page module (0x1E..0x24, 0x2C..0x31), which now load through the loader's 10 dispatches in both modes (no capture of their drive fields exists). Measured since (chain step AIMCAP, 2026-10-02, the AIM capture aim_05_burst_fire, which opens the status screen): the original waits on the SPR4 module 0x2C for 28 main-loop rows, on the SELECTOR's 0x31 for 18 and on the SPR4 reload for 23, where the port at host speed waits 11, 10 and 11 (LEVEL_SMOKE.md "The AIM side runs' whole records"); the capture holds the page record, not the drive's busy fields, so these are page-wait lengths, not a drive model, and the switch still loads these pages at host speed (a lead decision whether to record their reads). Since chain step DAMAGE (2026-10-02) the game-over screen module 0x27 (001AD4E0's 001FF080(0, 0x27), DAMAGE.md section 5) loads through the loader too, in 10 host-speed ticks in both modes: the DAMAGE recordings hold its disc time (D_00275BD8 set for 22 frames, the load 23 ticks), but its reads' busy fields are not modelled under the switch yet, so it counts as unmeasured like the page modules (the DAMAGE side runs align the game over on the load's end and report both lengths). Since chain step H7 (2026-09-29) the New Game's module-3 load and area load 001FF080(1, 0) run the loader's own steps too: off, 8 + 20 dispatches and a 55-frame load veil; on, their seven reads take the busy fields recorded in the PCSX2 New Game capture (MODULE_LOADER.md 1.7), 68 + 185 dispatches and a 257-frame veil where the PS2 drew 258 (the one frame is the PS2's ninth sound-bank call, most likely SIF DMA time: not a disc read, so not part of this switch; see "Not launcher options"). With the switch on the New Game's area entry comes 262 ticks later than off (the frame-order windows move with it: LEVEL_SMOKE.md "Frame order"); newgame-control's locked_ticks (1301 off, 1311 on) count from the opening and do not change. The opening music's extra seek from the intro movie's disc position is not modelled either way (the code does not model it). Since chain C11 EXIT (2026-10-02) the level exit's AREA01 load (001FF080(1, 0) for area 1 room 0) runs the loader's steps too: no recording of its reads exists (the EXIT capture samples the loader's states per frame, not its read times), so it answers at host speed in both modes (37 dispatches and a 55-frame veil, where the PCSX2 recording took 212 and 80; LEVEL_SMOKE.md "exit"). Re-checked 2026-09-29 (chain step H7): off, `make test-level-smoke-full` passes through roger with its side runs; on, `make test-level-smoke-ps2-drive` passes through roger; newgame-control gives 9.599849 both ways (locked_ticks 1301 off, 1311 on), and the frame order passes with no allowed difference at native index 1393 / 1455 (walk04) / 1384 (st03) / 89 (cut02) off and 1665 / 1727 / 1656 / 361 on (1330 / 1392 / 1321 / 26 and 1340 / 1402 / 1331 / 36 before the New Game's loads took ticks). | BUILT |

The 59.94 Hz game tick is not an option: all game logic counts fields.

---

## Graphics and display

| Option | Original value | Other values | Notes | Status |
|---|---|---|---|---|
| Resolution | The exact GS framebuffer (512x224 fields shown as 448 lines), 4:3 | Native / higher resolutions | `PORT_PROFILES.md` | CANDIDATE |
| Texture filtering | None (nearest) | Bilinear and better | | CANDIDATE |
| Anti-aliasing | None | The user's choice | | CANDIDATE |
| Widescreen / aspect ratio | 4:3 | Wider aspect ratios | Needs the camera's projection widened, a rendering-only change | CANDIDATE |
| Display frame rate | 59.94 Hz | Higher rates | Logic and streamed audio stay at 59.94 Hz; rendering is decoupled (`IOP_STREAM.md` "Clock domains") | CANDIDATE |
| Field presentation | see "Decisions for the user to review" | | | REVIEW |

Not planned: CRT, scanline or interlace simulation (user, 2026-09-23 and
2026-09-27).

---

## Controls and quality of life

From the "Future Enhancements" list in the port's `README.md` (the user's
file; read, never edit):

| Option | Original value | Enhanced value | Status |
|---|---|---|---|
| Slide down ladders | Climb down only | Slide down | CANDIDATE |
| Inverted aiming | Inverted (original) | Not inverted | CANDIDATE |
| Camera control | Classic (original) | Modern camera | CANDIDATE |
| Move while aiming | Not possible (original) | Allowed | CANDIDATE |
| Door transitions | Full black fade in/out on some doors (original) | No full black fade | CANDIDATE |
| Window follows the OS theme | Fixed | macOS window updates on dark/light theme changes | CANDIDATE |
| Subtitle fixes | Original subtitles | Corrected subtitles | CANDIDATE |

---

## Bug fixes (original behaviour is the default)

Places where the original code reads values its caller happened to leave in
registers. The Original value reproduces the original behaviour where the
port can; a fix option would give a defined result instead.

| Candidate | Original behaviour | Notes | Status |
|---|---|---|---|
| 0019D770 camera grid walker, no-span path | Walks using the $s1/$s2/$s4 its caller left (undefined in C) | Decomp FINDINGS "NEARMISS body corrections from the AREA01 lanes"; not shown to be reachable on the shipped data. The port currently refuses this path (a divergence to resolve first). | CANDIDATE |
| 0022BBC0 trail period / burst kind | With seq[0xD] >= 10 and a live actor, divides by the caller's $s1; a burst kind >= 6 reuses the previous actor's burst | Decomp FINDINGS "NEARMISS body corrections from the AREA01 wave-2 lanes" | CANDIDATE |
| 0017B300 manual reload top-up | A manual reload (L3, mode 2) with fewer rounds left than a full magazine still loads 30: it compares the total D_00810CB4 (which counts the loaded rounds too) against 30 minus the magazine, so magazine 16 with total 17 becomes magazine 30, total 17 | Found 2026-10-01 (AIM_FIRE.md section 8): the byte-matched decomp C and the port's em_aim_fire_control (test-aim-fire-control-reference) agree; the C10 AIM capture's reloads (aim_06, aim_07) do not reach the case. Off the recorded route; what the extra rounds do later (the total going below zero) is not traced. | CANDIDATE |

Add new original bugs here as they are found. Subtitle fixes are listed
under quality of life.

---

## Cut and hidden content

The source of truth is the decomp's `docs/CURIOSITIES.md` (reviewed
2026-09-27). Only its entries of a **restorable** kind (hidden system, hidden
or unreached UI, unreached content, cut content that left something on the
disc) may become options, and only once they are **decoded**. Each restored
item gets its own switch (Original value: off). Engine quirks are Original
behaviour; an optional fix for one belongs under "Bug fixes" above.

Restorable candidates today (none is decoded yet, so none is eligible):

| CURIOSITIES entry | Kind | Status |
|---|---|---|
| 1. Light and enemy perception (the old "light-based stealth system" premise is withdrawn; what remains is small) | hidden system | partial |
| 4. Passcode keypads with codes in memory | hidden or unreached UI | partial |
| 17. Area flag bit 2: alternate locomotion rows and a slow health drain | hidden system | partial |
| 23. Content on the disc not reached on the recorded routes | unreached content | partial |

Not candidates (corrected 2026-09-27): the "hidden animation entries" are
ordinary player clips, two of them on the first-level route (entry 3); the
15 dogtags, the infection diary and the 7th config row are shipped features
(entries 9, 10, 12). Re-read CURIOSITIES before any item becomes an option;
its statuses are the ones that count.

---

## Not launcher options (recorded so they are not mistaken for options)

- **The PS2's DMA transfer time** (found 2026-09-29, chain step H7). The
  New Game capture shows one more sound-bank call (001FB370) than the port
  makes, most likely the EE-to-IOP SIF DMA of the 0x492D0-byte bank not
  finished at 001FB910's first status query. The port's SIF DMA completes
  at host speed (hardware timing, not code: port CLAUDE.md 2026-09-27), and
  the disc-drive switch models only the disc. So the New Game's veil is one
  frame shorter than the PS2's in both switch positions (257 against 258
  with the switch on). No option is proposed; recorded so the one frame is
  not taken for a missing drive measurement.

- **Developer switches** (environment variables for development, not for
  players; STARTUP.md "Developer switches"):
  - `EM_NEW_GAME=1` (2026-09-30, workflow chain C10): skips the startup frontend
    and enters the original New Game route at once (001AC070 state 4,
    `em_game_install_new`), skipping the intro movie through the original
    START skip (one-shot: later movies skip by the pad alone); the player
    lands at the start of the AREA11 opening with normal control afterwards. `make test-new-game-switch` proves the
    opening starts from the title route's state. Not a launcher option: the
    Original profile always shows the frontend.
  - `EM_AIM_FIRE_TEST=r1`, `r2`, `r1hold` or `r2hold` with
    `EM_STARTUP_TEST=newgame-control` (since 2026-10-02 without a switch;
    AIM_FIRE.md section 1): the aim / fire input fixture after first
    control (aim, optionally fire, release). A test fixture, not a launcher
    option. The diagnostic gate `EM_AIM_FIRE_ORIGINAL=1` it used to need
    (2026-10-01, chain step AIM) is gone: the original aim / fire path is
    the only one (AIM_FIRE.md section 10).
  - `EM_SKIP_STARTUP=1`: the older debug fixture (`em_game_install`): a
    staged fixture scene read at once with demo status values; not the New
    Game route, and it does not reach the AREA11 opening. Kept for the
    legacy self-tests.

- **PS2 compile target:** the user eventually wants to compile the port's game
  code for the PS2 and put it in the ELF to test it in the emulator. That is a
  build target, not a player option.
- **Windows and Linux support:** platform backends, not options.

Last updated: 2026-10-02 (chain step EXIT: the PS2 disc-drive timing switch's scope gains the level exit's AREA01 load, host speed in both modes (no recording); the departure movie's skip is the original's START skip, not an option; no option found or decided. Before, chain step AIMLIVE's fix round: the developer gate EM_AIM_FIRE_ORIGINAL removed (the original aim / fire path is the only one), the EM_AIM_FIRE_TEST fixture kept as a test fixture; "Inverted aiming" and "Move while aiming" stay CANDIDATE: the Original profile now plays the original stances (row for row against the AIM captures); no option decided. Before, chain step AIMLIVE: the developer gate's description (melee selected, where r1 / r2 now stop); "Inverted aiming" and "Move while aiming" stay CANDIDATE: the original stances run row for row against the AIM captures behind the gate, ordinary play is unchanged, no option decided. Before, 2026-10-01, chain step AIMCAM's fix round: the gate's fixture name `smoke` for the level smoke's aim side runs; no option found or decided. Before, chain step AIMCAM: the developer switch's hold modes r1hold / r2hold; no option found or decided. Earlier, chain step AIM: the bug-fix candidate "0017B300 manual reload top-up" and the developer switch EM_AIM_FIRE_ORIGINAL recorded; earlier the same day the developer switch EM_NEW_GAME=1 and the older EM_SKIP_STARTUP=1 fixture; no option decided).
