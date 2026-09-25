# Original startup and first-level opening

The native entry point now runs the original startup sequence: warning, Sony,
Deep Space, E900 movie, and the three-choice title. New Game requests E900 again
before loading AREA11/sub0/entry0. `EM_SKIP_STARTUP=1` selects the older gameplay
fixture. It is a test override, not the normal opening.

Source evidence is maintained in the sibling decomp's local docs/STARTUP.md.
The original PS2 executable and local runtime are authoritative. The old and
current ports both contain approximations and incorrect translations.

## Local assets

Every asset is generated locally from the user's own disc, boot ELF and
PCSX2 captures, and stays in the ignored `assets/` tree; nothing here is
committed. The list below is the one ordered list of every export step the
first level uses (housekeeping step HK, 2026-09-23). Commands run from the
port directory unless they start with `cd ../Extermination`.

**Inputs** (all local, all the user's own): the extracted disc in
`../Extermination/extract/` (the decomp's extraction tools), the pinned boot
ELF `../Extermination/config/SCUS_971.12`, the disc image for the movie and
streams, and the PCSX2 captures in `../Extermination/build/startup-reference/`
(`opening_ee.bin`, `opening_gs.bin`, `playable_ee.bin`, `panel/`,
`status-hub/`, ...), which are oracle inputs and are never deleted.

**Classes.** Measured on 2026-09-23 by removing each file the live route
reads, one at a time, and running `EM_STARTUP_TEST=newgame-level` (New Game
through the elevator ride) headless from a copy of the tree:
- **R** required: without it the startup or New Game stops, a scene fault
  latches (fail-stop), or the level smoke fails.
- **L** live: read by the route; without it the route still passes with an
  identical tick log, but something is missing or a placeholder is drawn.
- **B** live, behaviour: without it the route passes but the tick log changes.
- **T** not read by the live game yet: read by reference tests and by
  translations that are still unbound; it becomes required when the named
  lane binds them.
- **X** not read by the first level (legacy fixtures, other scenes, reports).

Order matters where a later step patches an earlier output (the scene
manifest `scene_snow/scene.txt`, `player.emdl`, `player_channels.empc`).

| # | Step | Writes | Class | Read by |
|---:|---|---|---|---|
| 1 | `cd ../Extermination && python3 tools/export_startup.py --extract extract --out ../extermination-port/assets/startup` (see `--help`) | `startup/logo_*.emui`, `title_*.emui` | R | em_frontend (boot resources) |
| 2 | `python3 tools/export_movie.py --iso /path/to/owned.iso --out assets/startup/intro.mov` | `startup/intro.mov` | R | em_frontend (E900) |
| 3 | `python3 tools/export_startup_audio.py --decomp-root ../Extermination` | `startup_audio/` | R | em_startup_audio |
| 4 | `cd ../Extermination && python3 tools/export_font.py --ee <an EE RAM dump> --out ../extermination-port/assets/font.emfn` (FINDINGS "UI FONT") | `font.emfn` | R | the message glyph draw (tall glyphs), em_hud |
| 5 | `python3 tools/export_message_data.py` | `message/message_data.emmd` | R | em_message_live (step F, 001FCA10) |
| 6 | `cd ../Extermination && python3 tools/export_native.py --attach --no-glow --mesh extract/chunk28/f00_id3b.bin --anim extract/chunk28/f01_id3c.bin --clips ... --gsdump ... --out ../extermination-port/assets/player.emdl` | `player.emdl` | R | the player model and pose bank |
| 7 | `python3 tools/export_player_clips.py` (PLAYER_CLIPS.md), then `python3 tools/export_player_pose_channels.py` | `player_clips_full.bank`, `player_clips_full.empx`; `player_channels.empc` | R (`.bank`); T (`.empx`, `.empc`) | the player's one pose owner (em_player_record_pose: the bank at the record's +40; a missing file latches a scene fault at 0x0015BA50 since the display step); the `.empx` is em_pose_chain's (unbound) and the `.empc` the em_player_pose tests' |
| 8 | `python3 tools/export_player_stop_clips.py`, `export_elevator_clip.py`, `export_interaction_idle.py`, `export_panel_clip.py`, `export_door_player_clips.py`, `export_pickup_player_clips.py`, `export_player_reversal_clips.py --install`, `export_player_climb_slide_clips.py --install` | clips appended to `player.emdl` / `player_channels.empc` | R (with 6: the `player.emdl` clips); T (the `player_channels.empc` clips) | the interaction runtime's baked timing, the legacy display of the port's stand-ins and of the scenes without an original world (the idle/walk display is the record's since L12), pickup, door, stop clips (the reversal clips are no longer read); the channel appends are read by tests only since the display step; nothing live reads the climb/slide appends: the live climb (L04) and slide (L03) play from step 7's bank |
| 9 | `python3 tools/export_player_tables.py` | `player_clip_rates.emcr`, `player_clip_row0.emch`, `player_loco_tables.emrg` (D_00248740..D_00248ACC: 0017B490's clip rows, 0017C440's and 0017BC40's tier tables, 0017B5C0's and 0017B910's rows, read by the record pose since the Boxes step and by the idle / walk states since L12) | R | 0015BA50's clip-rate worker (em_player_stage_live, since L01) and D_00248C90's +0 column (0015BCF0's evaluator choice, 00182DF0's release; em_player_record_pose since the display step); a missing file latches a scene fault at 0x0015BA50 |
| 10 | `cd ../Extermination && python3 tools/export_level.py ... --area 11 --sub 0 --overlay extract/OVERLAY/AREA11.BIN` with its manifest passes (`--spawn`, `--camregions`, `--lightrig`, `--pickups`, `--examine`; FINDINGS s78) | `scene_snow/*.emdl` level parts, `scene.txt` | R (`scene.txt`), L (level parts) | em_scene manifest, the level draw |
| 11 | `cd ../Extermination && .venv/bin/python tools/export_level.py --gs-materials ../extermination-port/assets/scene_snow` (LEVEL_MATERIALS.md) | material words in the level EMDLs, `*.gsmat.json` reports | L | the level draw |
| 12 | `cd ../Extermination && python3 tools/export_props.py ...` (library, `--gibs`, `--fx`, `--crate`, `--egg`, `--area-items`, `--doors`; MODDING.md) | `scene_snow/props/`, `doors/`, `fx/`, `gibs/`, `enemy_*.emdl`, `tendril.emdl` | L; B for `doors/door_m03.emdl` and `props/item_73.emdl`; R for `props/area_elevator.emdl` and `props/area_item_04.emdl`; L for `props/area_truck.emdl` (the truck owner's draw since census L23; the manifest's `truck` line is no longer read) | manifest props, pickups, legacy enemies and weapon effects |
| 13 | `cd ../Extermination && python3 tools/export_collision.py <chunk15 f07..f12> -o ../extermination-port/assets/scene_snow/snow.emcl --at 218.592,201.789 --node-class --verify-ram build/s87/route/06_hill_slide/eeMemory.bin` (COLL_PROBES.md 3; step 13b folded in: since census L07 the file must carry the node class and the rank section, flags 7; since census L09 `--node-class` also writes the grid axis section, node +0x34..+0x3F, flags 0xF) | `scene_snow/snow.emcl` | R | em_collision; the collision world's grid walkers (a flags-1 file faults at 0x001AFCA0); the ladder actions 0015D4C0 / 00177030 (a file without the axis section faults at the first ladder Use, route 10) |
| 14 | `python3 tools/export_opening_scenery.py` | `scene.txt` (canopy line) | R (via scene.txt) | em_scene |
| 15 | `python3 tools/export_area11_props.py` | switch/elevator/indicator models, `scene.txt` | R/L (see 12) | manifest props |
| 16 | `python3 tools/export_pickup_lights.py` | pickup bodies and lights, `scene.txt` | L/B | pickup-light children |
| 17 | `python3 tools/export_area11_panel_collision.py` | `props/panel_cell18.emcb` | R | the panel's collision cell 18 |
| 18 | `python3 tools/export_door_original.py`, then `python3 tools/export_door_program.py` | `door_original/` | L (`source.emdo`); T (model, channels, program; WP-7) | the door binding; the original door runtime |
| 19 | `../Extermination/.venv/bin/python tools/export_area11_effect.py --ee ../Extermination/build/startup-reference/opening_ee.bin --gs ../Extermination/build/startup-reference/opening_gs.bin --vu ../Extermination/build/weather_reference/original_vu1.bin` | `area11_effect.emef/.emtx`, `scene.txt` | R | the 008235F0 effect owner |
| 20 | `python3 tools/export_point_lights.py` | `point_lights.emlp` | R | em_point_light |
| 21 | `python3 tools/export_snow.py --gs ../Extermination/build/startup-reference/opening_gs.bin --reference-ee ../Extermination/build/startup-reference/opening_ee.bin` | `snow.emsn`, `snow.emtx` | L | the weather node 001C1EA0 (em_snow_runtime) |
| 22 | `python3 tools/export_area11_flow.py` | `area11_flow.emaf` | L | the legacy director stand-in's triggers (until WP-10) |
| 23 | `python3 tools/export_area11_opening.py` | `opening.emsc` | R | em_opening_runtime |
| 24 | `python3 tools/export_opening_camera.py --source ../Extermination/extract/chunk15/f12_id44.bin --bank-offset 0xD0800 --out assets/scene_snow/opening_camera.emcc` | `opening_camera.emcc` | R | the opening camera |
| 25 | `python3 tools/export_opening_media.py --decomp-root ../Extermination --iso /path/to/owned.iso --out assets/scene_snow` | `opening.wav`, `opening.emfx`, `opening_resume.wav` | R | em_opening_media, the resumed cue 25 |
| 26 | `cd ../Extermination && python3 tools/export_opening_actors.py --gs build/startup-reference/opening_gs.bin --reference-ee build/startup-reference/opening_ee.bin --out ../extermination-port/assets/scene_snow/opening --report build/area11_original/opening_export.json` (decomp OPENING_ACTORS.md) | `opening/player.emdl`, `roger.emdl`, `equipment_6b.emdl` | R | the opening actors |
| 27 | `cd ../Extermination && python3 tools/export_opening_faces.py` with the same inputs (decomp OPENING_ACTORS.md) | `opening/*_face.emdl/.emfm` | R | the opening faces |
| 28 | `python3 tools/export_area11_roster.py` | `roster.emro` | R | 001B6990 (the state-0 roster spawn) |
| 29 | `python3 tools/export_spawn_table.py` | `spawn/spawn_table.emsp` | R | 001B07C0 |
| 30 | `python3 tools/export_interaction_scan.py` | `interaction.emis` | R | the AREA11 interaction host |
| 31 | `python3 tools/export_elevator.py` | `elevator.emsc` | R | the host (terminal 00827B10) |
| 32 | `python3 tools/export_panel.py`, `python3 tools/export_item_root.py`, `python3 tools/export_status_hub.py` (captures under `build/startup-reference/panel`, `panel/root`, `status-hub`) | `panel/` | R | the host's panel, BATTERY/ITEM pages and hub |
| 33 | `python3 tools/export_sdk_math_tables.py` | `sdk_math_tables.emsm`, `sdk_soft_float.emsf` | R | the status background's SDK sine (0011E2A8); the collision world's SDK context (0011E748, D_0026C5D0; the soft-float workers' D_0024295C and errno word from `sdk_soft_float.emsf`, whose absence faults the AREA11 build at 0x001AFCA0) |
| 34 | `python3 tools/export_status_models.py` | `status_models/` | R | the status hub models |
| 35 | `python3 tools/export_pickup_programs.py` | `scene_snow/pickup_*.emsc` | R | the pickup owners 0015AFA0 / 00219550 |
| 36 | `python3 tools/export_area11_sfx.py` | `sfx/area11/panel_sfx.*` | R | the panel cues (the host loads them) |
| 37 | `python3 tools/export_sfx_registry.py` | `sfx/sfx_registry.emsr` | L | em_sfx |
| 38 | `python3 tools/export_area11_scripts.py` | `area11_scripts/` | R (since census L23: the truck trigger's script start faults without it) | the AREA11 script host (em_area11_script_host: the truck preview 0x8292C0; the director scripts and quads wait on L21) |
| 39 | `python3 tools/export_roger_resources.py`, `export_roger_encounter_actor.py`, `export_roger_cinematic.py`, `export_roger_media.py --iso /path/to/owned.iso` | `scene_snow/roger/` | R (since census L22: `roger.emdl`, `trigger.empg`, `programs.emsc`, `encounter_camera.emcc`, `camera_projection.emcp`, `encounter.wav`; the decoded `*.empc` banks are read only by tests) | em_area11_roger (mesh, trigger quad), em_area11_script_host (Roger's programs, the bank 0x96 camera track), em_opening_media (cue 29) |
| 40 | `cd ../Extermination && python3 tools/export_level.py --background ../extermination-port/assets/scene_snow --area 11 --sub 0 --iso <owned.iso> --capture-ee ... --capture-gs ...` (BACKGROUND.md) | `background.embg`, manifest line | R (since the render + UI step: the AREA11 manifest load quits without it) | em_background_gs: the level background every world frame draws first (em_scene.c, em_render_frame.c) |
| 41 | `cd ../Extermination && python3 tools/export_shadow_receivers.py --out ../extermination-port/assets/scene_snow/shadow_receivers.emsr` (SHADOW_ORIGINAL.md) | `shadow_receivers.emsr` | T (L29b) | em_shadow_original receiver passes |
| 42 | `cd ../Extermination && python3 tools/export_shadow_proxy.py` (SHADOW_ORIGINAL.md) | `player_shadow.emdl` | T (L29) | the shadow silhouette |
| 43 | `python3 tools/test_actor_collision_reference.py --export` (ACTOR_COLLISION.md 3; a stand-in until a dedicated exporter exists) | `scene_snow/area11_cells.bin` | R (since census L07; a missing file faults at 0x001AFCA0) | the collision world's cell directory (em_actor_cells_load) |
| 44 | `python3 tools/export_world_models.py` (OWNER_DRAW.md 4) | `scene_snow/world_models.emwm`, `world_models.json` | R (since census L25: the crates' and drums' first tick faults without it) | 001B0EA0's model bank *D_0028A59C (em_area11_boxes) |
| 45 | `python3 tools/export_box_tables.py` (CRATES_DRUMS_ORIGINAL.md "Binding") | `scene_snow/box_tables.emrg` | R (since census L25) | D_002468B0 / D_00246A00 / D_00246A10 (em_area11_boxes) |
| 46 | `python3 tools/export_pad_tables.py` (TRUCK_ORIGINAL.md "Binding") | `pad_rumble.emrg` | R (since census L23: the truck's arm rumble faults without it) | D_0024D6F0, 001B1E20's rumble records (em_pad_actuator) |
| 47 | `python3 tools/export_roger_banks.py` (ROGER_ACTOR_ORIGINAL.md section 4; after step 39's decomp extract) | `scene_snow/roger/resources.emrs` | R (since census L22: Roger's lifecycle 0 faults without it) | D_0028A490's table, the clip banks 0x96 / 0x4A, model 0x47 and the equipment model 0x6B at their EE addresses (em_area11_roger; bank 0x96 also mapped into the player record's pose host) |
| 48 | `python3 tools/export_camera_tables.py` (CAMERA_LIVE.md section 3) | `camera_tables.emrg` | R (since census L13..L16: without it the live camera does not bind and the area build faults at 0x0018B9C0) | D_0024A4B0 / D_0024A5F0: 001B1EA0's quads for 00190F20 and 00194D10 (em_camera_live) |

Not read by the first level (X): `ui.emui`, `ui_page*.emui`, `messages.emsg`,
`title.emui`, `gameover.emui` (decomp `export_ui.py` / `export_screen_modules.py`,
the legacy HUD and fixtures), `assets/scene`, `scene_office0`,
`scene_drawbridge` (the EM_SKIP_STARTUP fixtures), `scene_snow/snow_bgm.wav`
(the manifest `bgm` line is ignored), `assets/opening/` (an older default
`--out` of step 25) and the exporters' JSON reports.

Honest limits of this list:
- It is the order of dependencies, not a proven one-pass rebuild: no test
  re-runs all steps from an empty tree. Steps 18 (program), 21, 35, 36 and 39
  (resources, encounter actor, cinematic) were re-run on 2026-09-23 and wrote
  byte-identical files.
- Steps 1, 4, 6, 10 and 12 give the tool and its recorded mode; their full
  argument lists are in the decomp docs cited. For 6, SHADOW_ORIGINAL.md
  notes that eight clips of the installed `player.emdl` (0, 64..67, 69, 71,
  348) differ from a fresh `export_native.py` bake and what produced them is
  not established.
- A latched scene fault stops the game task but, in the `newgame-level`
  smoke, does not end the process: a missing R asset of the interaction host
  or the roster makes that run hang instead of exit.

The message service (step F, WP-8; docs/MESSAGE_SERVICE.md) reads
`assets/message/message_data.emmd`: the ELF's message tables and the global
and AREA11 message banks from `extract/`. Without it the title runs, but New
Game faults at its first 001FC9B0 (the state-0 rebuild's message reset).

The startup exporter replays the GS uploads, including their on-disc palettes,
and composes the actual sprites. The movie exporter preserves MPEG-2 access
units, presentation timestamps, and every PCM sample; macOS plays the result
through its system media frameworks. The port adds no third-party runtime.

The first-level animation/camera bank is embedded in chunk15/f12_id44 at 0xD0800,
not at the start of the unrelated file named f06_id98. Its three directory
entries contain the camera, Dennis animation, and Roger animation. Runtime
resource slot 0x98 points at this bank. The camera has 646 source frames and 647
samples including lookahead, and advances 0.5 per ordinary tick.

- `assets/sdk_math_tables.emsm` is the boot ELF's SDK float-math window
  D_0026C170..D_0026C658. The status background 0020A7A0 draws its sine
  through the translated SDK sinf 0011E2A8 over it (docs/SDK_MATH_ORIGINAL.md;
  `make test-sdk-math-original test-sdk-math-original-reference`).
- `assets/sdk_soft_float.emsf` holds D_0024295C and the errno word it names
  (0x00242670, initial 0), from the same ELF. The collision world binds the
  soft-float workers of 0011E620 / 0011E748 over them (docs/SDK_SOFT_FLOAT.md
  section 4; `make test-sdk-soft-float test-sdk-soft-float-reference`).
- `assets/status_models/` holds the status hub's 3D models: the menu player,
  its two clips and the equipment letter models of D_0028A56C. Their
  textures come from the status-hub capture's GS memory
  (../Extermination/build/startup-reference/status-hub), like the opening
  actors (docs/STATUS_SCENE.md section 7; `make test-status-models`).

## Behavior and verification

- The task table preserves the original trailing bytes when replacing a task.
- Fullscreen fades and letterbox fades are separate original state machines.
- During movies the ordinary task loop, fade updates, frame count and parity
  suspend. Movie input still runs. The skip gate uses completed picture index11.
- Startup screen holds use the original post-decrement counter (301 draw ticks).
  Title navigation clamps, accepts START/CROSS, and respects the fade gate.
- Native scene asset lookup follows the active scene directory, including New
  Game and room transitions; it does not depend on the EM_SCENE symlink fixture.
- Collision queries distinguish camera, movement, and general segment filters.
  Their attribute predicates were checked against original ELF branch execution.
- The opening camera holds roll, respects negative-FOV cut markers, and rounds
  its finite arithmetic toward zero at each original operation. At one captured
  opening frame its six eye/target floats match original runtime bytes exactly.
  Cinematic mode 3 uses the authored eye without the gameplay forward push.
  Its native view matrix agrees with the captured original within 0.000031.
- Body animation uses the original stateful half-tick cursors, cut flags and
  unnormalized quaternion blend. Original rifle and knife child meshes attach
  to Dennis bones 4 and 14; Roger equipment follows his bone 1.
- The opening runs its exported script and original dialogue/fade tracks. Both
  normal and skip paths restore the final script position/camera and story
  flags once. Missing required resources fail explicitly.
- Random arithmetic matches the original SDK leaf, including 32-bit stored
  state and 31-bit output. Whole-game RNG call ordering is still unaudited.
- The original head meshes carry seven morph channels. Blink/mouth state and
  vertex blending pass original instruction comparisons; exact pooled initial
  weights and separate head-light selection remain work.
- Subtitle text draws after the letterbox subtraction and before fullscreen
  fades. The aligned source134.5 GPU capture now shows the original “Look up.”
- The canopy uses the original placement record. Six pickup-light children use
  original model73, owner transforms, random color arithmetic and additive draw.

Tests are asset-free unless explicitly described as reference comparisons:

```sh
make test-input test-task test-fade test-startup test-movie-export test-startup-audio
make test-area11-flow test-collision test-script test-area11-opening
make test-cinematic-camera test-opening-actor test-opening-media test-bgm-ticks
make test-opening-runtime
python3 tools/test_random_reference.py
python3 tools/test_continue_reset_reference.py
python3 tools/test_collision_reference.py --help
python3 tools/test_area_title_reference.py --help
```

Local runtime evidence lives in ignored build/startup_natural, startup_skip,
startup_newgame, opening_visual, opening_control, and reference_runtime.md.
A complete E900 playback and interactive menu succeeded. New Game tests skip
both E900 requests after two seconds, then run the entire in-engine opening.
The full GPU run completed the opening at gameplay frame 1304 and captured
normal gameplay at frame 1400. This is not a claim of complete visual fidelity.

The actual-input regression passed: held forward input produced zero movement
through 1,303 opening ticks. After correcting a 90-degree stick-heading error,
the original motor and final script camera now give 9.599989 units over30 input
ticks, versus9.599849 in the original. The horizontal endpoint differs by
0.000168. The heading helper passes2,360 original instruction cases and the
motor11,482 cases. Run release now plays original stop clip5 and restores idle;
other locomotion branches and pose blending remain under comparison. See
`FIRST_CONTROL.md` for the trace, original addresses and remaining limits.
Run from the port directory:

```sh
EM_STARTUP_TEST=newgame-control EM_CAPTURE=build/after_opening.bmp build/extermination
```

The frame/controller/media/actor integration passes AddressSanitizer and
UndefinedBehaviorSanitizer with real exported assets. Additional oracles execute
original ELF instructions for collision, script sequencing, and random state.
Host actor matrix differences from EE/VU arithmetic are measured up to 0.000092.
The native prefill handshake is faster than the original disc/IOP wait; align
camera/actor cursors when comparing screenshots, rather than scene frame alone.

## New Game, Continue and music

- The title's New Game and the game-over prompt's option 0 take the same
  original route: `func_001AC070` state 4 reinstalls `func_001ACEC0` with
  `D_00275BE0=0`, so `001AD230` runs `func_001AF2C0`, and `001AD360` commits
  area 0x0B / sub 0 / entry 0. Both native paths apply the same reset:
  `game_state_new_game` plus `em_pickup_reset`. The old Continue values
  (75/60/4/120/4-6) were copied from a debug fixture and have been removed.
  Continue now restarts in AREA11 wherever the player died. The memset
  clears event byte `D_00810791` (`D_00810758[0x39]`), and the opening
  controller 00823E80 skips its script only when that byte is 0xFF
  (`001BA1C0(.., 0x39)` in its state 1); it does not read `D_00810811`. So
  the rebuilt area should replay the opening, but that is inferred from the
  instructions: no original Continue has been captured.
  `tools/test_continue_reset_reference.py` runs `001AF2C0` on captured AREA11
  RAM and compares 11 mirrored `EmGameState` fields. `em_pickup_reset` now
  applies the inventory seeds (item counts 0/5/7/0x17 = 1, 0x10 = 2, magazine
  packs 2, primary 0xFF), and the same test compares them, with item counts
  0x00..0x3F and the other em_pickup fields, against the executed original.
  `CA5`/`CA7` (5/7) and `D20..D23` (1) are not mirrored by any port module
  yet. Continue resets only these
  mirrored fields (plus the death/prompt state and the pickup table); unlike
  New Game it does not memset the whole native game state, so other port
  state (for example the legacy director's per-beat transient) carries over.
  The D2 progress region (the director step `D_00810813`, the key bytes
  `D_00810CC3`, `D_00810707` and the other migrated bytes) is cleared by
  `em_scene_progress_reset_001AF2C0` on both paths.
  The `001AD360` step 0 stream stop (`001FABB0`) is mirrored with
  `em_bgm_stop(0)`. Not mirrored yet: the `001D1EF0` calls in steps 0-2 and
  the step 1 E900 movie request (Continue does not replay E900).
- `D_00810811` is the opening-complete byte (`g.opening_complete`), not a
  battery flag. The opening controller 00823E80 stores 0xFF there at
  0x00823F74..80, when script 0x828FC0 ends. The same test executes that
  slice. The fabricated `battery_terminal` examine path, the `battery`
  pickup marker, the type-0x11 take hook in `em_pickup.c` and
  `em_game_set_battery` / `em_game_has_battery` have been removed.
- No music starts from scene data. The manifest `bgm` line is ignored.
  Original area music is chosen by `001FAE70` from `D_008106C8` bits 8..15;
  the AREA11 captures give cue 25. `anim_frame_top_b` state 0 calls
  `001FAE70(1)` at area entry. The opening controller stops streams when its
  script starts and resumes cue 25 at the end. The area-entry call is not
  mirrored yet: it also draws one `rand()`, and whole-game RNG order is
  unaudited. The status close's `001FAE70(1)` (0x1AE040 state 5) is
  translated since WP-5 (em_scene_bindings.c w_001FAE70), as is the stream
  stop `001FABB0` at the status open and at 001AD360 step 0. The stream
  volume is not matched: `001FBC50` (status open) and `001FC280` (status
  close, the spawn record's +0x20 low half) set stream channels 0/1 to
  0x1999 in AREA11, and the port's streams have no per-channel gain, so
  those `00119828` calls are reported (UM_00119828) and the resumed cue 25
  plays at full scale. `001FAE70`'s infected override (D_008104E4 == 1:
  cue 0x18) faults. `EM_BGM` remains a debug-only override.

## Remaining fidelity work

The first-level checkpoint remains under comparison. Exact face initial state,
whole-game random ordering, snowfall, other scenery behavior, lighting/materials
and ordinary player animation need further work. The old direct-to-control opening and fabricated
battery pickup were incorrect and have been removed from the normal path.
Existing later traversal cutscenes and Game Over presentation still contain
approximations and must not be treated as original behavior.

The previous “grate” was the original static switch panel. Its invented blocker
and slide have been removed; the elevator body and two indicator meshes now use
the original resource bindings. Roger's body was also mislabeled as a battery
console, so that false pickup/examine placement has been removed. Original panel
interaction scripts are still being connected. See docs/OPENING_SCENERY.md.

Weather controller C is present as an isolated verified module:12,000 original
instruction comparisons check state and random-call ordering. It is not yet
connected to the frame loop, pending recovery of the original snow renderer.

The title audio sequencer preserves the original events, waits, pitches and
sample data, but its current dry mixer does not reproduce SPU2 ADSR, Gaussian
interpolation, reverb or hardware voice allocation. Native storage currently
maps the successful card-check path to a local directory; full save/load and
space handling remain incomplete. Load Game, Option and attract services remain
pending rather than manufacturing an outcome. Windows/Linux movie backends and
other existing gameplay approximations also remain unfinished.
