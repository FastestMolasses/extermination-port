# Original startup and first-level opening

The native entry point now runs the original startup sequence: warning, Sony,
Deep Space, E900 movie, and the three-choice title. New Game requests E900 again
before loading AREA11/sub0/entry0. Two developer switches start elsewhere
(section "Developer switches"): `EM_NEW_GAME=1` enters the original New Game
route without the frontend, and `EM_SKIP_STARTUP=1` selects the older debug
fixture, which is not the New Game route.

Source evidence is maintained in the sibling decomp's local docs/STARTUP.md.
The original PS2 executable and local runtime are authoritative. The old and
current ports both contain approximations and incorrect translations.

## Local assets

Every asset is generated locally from the user's own disc and boot ELF,
and stays in the ignored `assets/` tree; nothing here is committed. The list
below is the one ordered list of every export step the first level uses
(housekeeping step HK, 2026-09-23). Commands run from the port directory
unless they start with `cd ../Extermination`.

**Inputs** (all local, all the user's own): the extracted disc in
`../Extermination/extract/` (the decomp's extraction tools), the pinned boot
ELF `../Extermination/config/SCUS_971.12` and the disc image (`--iso`,
default `../Extermination/Extermination-rebuilt.iso`; INDEX.IDX gives the
loaders' descriptors, and the movie and streams are read from it).

**No PCSX2 capture and no RAM dump is needed** (chain step "every
first-level asset from the disc", 2026-09-28; DISC_TEXTURES.md section 9),
with two exceptions still open: step 30 (`interaction.emis`) and step 40
(`background.embg`), which read runtime state from a capture (see those
rows). Every other step below names a command that reads only the disc,
the extract and the ELF. The textures come from the first level's GS
memory rebuilt from the disc with the original's own upload sequence (step
4 writes it to `build/disc_textures/first_level_gs.bin` for the steps that
take a `--gs` / `--p2s` texel source); the resource table D_0028A490 is
rebuilt with the loaders' rules (`ResourceTable`). The PCSX2 captures in
`../Extermination/build/startup-reference/` and `build/s87/route/` are
oracle inputs for the reference tests and optional cross-checks of the
exporters (`--capture`, `--verify-ram`, `--route-captures`: run when the
developer has them, never required); they are never deleted.
`make test-disc-textures-reference test-disc-assets-reference` compare the
disc-only outputs with the capture-derived files, and the whole level smoke
passed on a tree rebuilt by these commands with no capture visible
(DISC_TEXTURES.md 9).

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
| 4 | `python3 tools/export_disc_textures.py [--iso /path/to/owned.iso]` (DISC_TEXTURES.md; after the decomp's extract; replaces the decomp's `export_font.py --ee <an EE RAM dump>`) | `font.emfn`; also steps 34, 51 and 52's files and the texture files of step 32 (`battery.emba`, `item_root.emir`, `status_hub_atlas.emha`), each byte-identical to its tool's own output; `build/disc_textures/first_level_gs.bin` (the rebuilt GS memory the later `--gs` / `--p2s` steps read) | R | the message glyph draw (tall glyphs), em_hud (module 0's resource slots 0 and 1, loaded by the boot's 001FF1E0(0)) |
| 5 | `python3 tools/export_message_data.py` | `message/message_data.emmd`, `message/message_presenters.emmp` | R | em_message_live (step F, 001FCA10); the .emmp its mode-3 / mode-4 presenters (em_message_presenters_live; since the status UI step every status page's lines need it: a status screen faults without it) |
| 6 | `cd ../Extermination && .venv/bin/python tools/export_native.py --attach --no-glow --mesh extract/chunk28/f00_id3b.bin --anim extract/chunk28/f01_id3c.bin --clips <the clip list of export_native.py's usage> --p2s ../extermination-port/build/disc_textures/first_level_gs.bin --out ../extermination-port/assets/player.emdl` (after step 4; `--p2s` over the rebuilt GS memory replaces the GS dump `--gsdump extract/gsdump/frame1.gs`: the two bakes are byte-identical, test-disc-assets-reference full mode) | `player.emdl` | R | the player model and pose bank |
| 7 | `python3 tools/export_player_clips.py` (PLAYER_CLIPS.md; the bank == RAM check runs only when captures are present), then `python3 tools/export_player_pose_channels.py` | `player_clips_full.bank`, `player_clips_full.empx`; `player_channels.empc` | R (`.bank`); T (`.empx`, `.empc`) | the player's one pose owner (em_player_record_pose: the bank at the record's +40; a missing file latches a scene fault at 0x0015BA50 since the display step); the `.empx` is em_pose_chain's (unbound) and the `.empc` the em_player_pose tests' |
| 8 | `python3 tools/export_player_stop_clips.py`, `export_elevator_clip.py`, `export_interaction_idle.py`, `export_panel_clip.py`, `export_door_player_clips.py`, `export_pickup_player_clips.py`, `export_player_reversal_clips.py --install`, `export_player_climb_slide_clips.py --install` (the elevator, idle, panel, door and pickup clip tools bake from the disc bank; their captured-palette / bank checks run only when the captures are present) | clips appended to `player.emdl` / `player_channels.empc` | R (with 6: the `player.emdl` clips); T (the `player_channels.empc` clips) | the interaction runtime's baked timing, the legacy display of the port's stand-ins and of the scenes without an original world (the idle/walk display is the record's since L12), pickup, the legacy door's (not read in AREA11 since census L18), stop clips (the reversal clips are no longer read); the channel appends are read by tests only since the display step; nothing live reads the climb/slide appends: the live climb (L04) and slide (L03) play from step 7's bank |
| 9 | `python3 tools/export_player_tables.py` | `player_clip_rates.emcr` (EMCR v2, rows -1..458: row -1 is what a cutscene skip's +20C = -1 reads; an older v1 file loads but faults on a skip), `player_clip_row0.emch`, `player_loco_tables.emrg` (D_00248740..D_00248ACC: 0017B490's clip rows, 0017C440's and 0017BC40's tier tables, 0017B5C0's and 0017B910's rows, read by the record pose since the Boxes step and by the idle / walk states since L12) | R | 0015BA50's clip-rate worker (em_player_stage_live, since L01) and D_00248C90's +0 column (0015BCF0's evaluator choice, 00182DF0's release; em_player_record_pose since the display step); a missing file latches a scene fault at 0x0015BA50 |
| 10 | `cd ../Extermination && python3 tools/export_level.py ... --area 11 --sub 0 --overlay extract/OVERLAY/AREA11.BIN` with its manifest passes (`--spawn`, `--camregions`, `--lightrig`, `--pickups`, `--examine`; FINDINGS s78) | `scene_snow/*.emdl` level parts, `scene.txt` | R (`scene.txt`); X (the level parts: AREA11 loads no scene EMDL since the static-world step, 2026-09-28; the level is drawn from its original packets, step 58) | em_scene manifest |
| 11 | `cd ../Extermination && .venv/bin/python tools/export_level.py --gs-materials ../extermination-port/assets/scene_snow` (LEVEL_MATERIALS.md) | material words in the level EMDLs, `*.gsmat.json` reports | X (since the static-world step; the reports stay test inputs: test_level_material_reference) | no live reader on the first level |
| 12 | `cd ../Extermination && python3 tools/export_props.py ... --p2s ../extermination-port/build/disc_textures/first_level_gs.bin` (library, `--gibs`, `--fx`, `--cone`, `--crate`, `--egg`, `--area-items`, `--doors`; MODDING.md; after step 4: `--p2s` over the rebuilt GS memory in place of `--gsdump`; the `--fx` sheets are byte-identical either way, and the texels of `props/area_item_0b.emdl`, `area_item_13.emdl` and `area_parachute.emdl` equal the rebuilt memory's) | `scene_snow/props/`, `doors/`, `fx/`, `gibs/`, `enemy_*.emdl`, `tendril.emdl` | L; B for `props/item_73.emdl`; X for `doors/door_m03.emdl` (AREA11's fence door is its original owner since census L18); R for `props/area_elevator.emdl` and `props/area_item_04.emdl`; X for `props/area_truck.emdl` (the truck owner draws the bank's model 9 through the object units since 2026-09-25, OWNER_DRAW.md; the manifest's `truck` line is no longer read); X for `scene_snow/props/enemy_crate.emdl` and `enemy_egg.emdl` in AREA11 (the crates and drums draw the bank's models the same way) | manifest props, pickups, legacy enemies and weapon effects |
| 13 | `cd ../Extermination && python3 tools/export_collision.py <chunk15 f07..f12> -o ../extermination-port/assets/scene_snow/snow.emcl --at 218.592,201.789 --node-class` (optional check: `--verify-ram build/s87/route/06_hill_slide/eeMemory.bin`; COLL_PROBES.md 3; step 13b folded in: since census L07 the file must carry the node class and the rank section, flags 7; since census L09 `--node-class` also writes the grid axis section, node +0x34..+0x3F, flags 0xF) | `scene_snow/snow.emcl` | R | em_collision; the collision world's grid walkers (a flags-1 file faults at 0x001AFCA0); the ladder actions 0015D4C0 / 00177030 (a file without the axis section faults at the first ladder Use, route 10) |
| 14 | `python3 tools/export_opening_scenery.py` | `scene.txt` (canopy line) | R (via scene.txt) | em_scene |
| 15 | `python3 tools/export_area11_props.py` (texels from the rebuilt GS memory; `--gs FILE` for another freeze) | switch/elevator/indicator models, `scene.txt` | R/L (see 12) | manifest props |
| 16 | `python3 tools/export_pickup_lights.py` (texels as step 15) | pickup bodies and lights, `scene.txt` | L/B | pickup-light children |
| 17 | `python3 tools/export_area11_panel_collision.py` | `props/panel_cell18.emcb` | R | the panel's collision cell 18 |
| 18 | `python3 tools/export_door_original.py` (texels from the rebuilt GS memory, the door's .data from the ELF; the playable capture, when present, is checked), then `python3 tools/export_door_program.py` | `door_original/` | R (census L18: without them the fence door's node faults) | em_area11_door (the resources, the model drawn, the ELF program 0x24DE40 the AREA11 script host runs) |
| 19 | `../Extermination/.venv/bin/python tools/export_area11_effect.py` (optional checks: `--ee <an AREA11 EE capture> --vu <its VU1 dump>`) | `area11_effect.emef`, `scene.txt` | R | the 008235F0 flame (its placement and descriptor; since chain C8b FLAMESNOW its texture is a page texture, step 52, and no `.emtx` is written or read) |
| 20 | `python3 tools/export_point_lights.py` | `point_lights.emlp` | R | em_point_light |
| 21 | `python3 tools/export_snow.py` (the weather bits from 001B0250's room record D_0024D650[11][0] + 0x1C; optional check: `--reference-ee <an AREA11 EE capture>`) | `snow.emsn`, `scene.txt` | L | the weather node 001C1EA0 (em_snow_runtime; since chain C8b FLAMESNOW its texture is a page texture, step 52, and no `.emtx` is written or read) |
| 22 | `python3 tools/export_streams.py [--iso /path/to/owned.iso]` (IOP_STREAM.md "Stream exporter") | `streams/streams.emst`, `streams.json` | R (since WP-8b: the boot stops without it, fail-stop) | em_stream_live: the stream lanes' clip rows and the IOP backend's disc sectors (music cues 13, 25, 29, 54, 63, 0x18, 0x1B; voice cues 143..151). (The former step 22, `export_area11_flow.py`, fed the legacy director stand-in, deleted with it in WP-8b.) |
| 23 | `python3 tools/export_area11_opening.py` | `opening.emsc` | R | the opening controller 00823E80's script 0x828FC0 and op14's placement list on the AREA11 script host (OPENING_ORIGINAL.md) |
| 24 | `python3 tools/export_opening_camera.py --source ../Extermination/extract/chunk15/f12_id44.bin --bank-offset 0xD0800 --out assets/scene_snow/opening_camera.emcc` | `opening_camera.emcc` | R | the opening camera |
| 25 | `python3 tools/export_opening_media.py --decomp-root ../Extermination --iso /path/to/owned.iso --out assets/scene_snow` | `opening.wav`, `opening.emfx`, `opening_resume.wav` | R (`opening.emfx`); X (the two WAVs: the streams play from step 22 since WP-8b) | em_opening_media's fade track |
| 26 | (retired in chain C8b OPENING: the decomp's `export_opening_actors.py`) | `opening/player.emdl`, `roger.emdl`, `equipment_6b.emdl` | X | nothing reads them: the opening's actors are records drawing their original units (OPENING_ORIGINAL.md) |
| 27 | (retired in chain C8b OPENING: the decomp's `export_opening_faces.py`) | `opening/*_face.emdl/.emfm` | X | nothing reads them: the faces are 001CB3C0's face units |
| 28 | `python3 tools/export_area11_roster.py` | `roster.emro` | R | 001B6990 (the state-0 roster spawn) |
| 29 | `python3 tools/export_spawn_table.py` | `spawn/spawn_table.emsp` | R | 001B07C0 |
| 30 | `python3 tools/export_interaction_scan.py` (**still needs a capture**: `--ee`, default the first-control capture `playable_ee.bin`; see DISC_TEXTURES.md 9.4) | `interaction.emis` | R | the AREA11 interaction host |
| 31 | `python3 tools/export_elevator.py` | `elevator.emsc` | R | the host (terminal 00827B10) |
| 32 | `python3 tools/export_panel.py`, `python3 tools/export_item_root.py`, `python3 tools/export_status_hub.py` (all from the disc: the page states' GS memory rebuilt with the BATTERY / ITEM root modules' uploads, the hub run over the ELF image plus the loads' resident regions; optional `--capture DIR` cross-checks against `build/startup-reference/panel`, `panel/root`, `status-hub`) | `panel/` | R | the host's panel, BATTERY/ITEM pages and hub |
| 33 | `python3 tools/export_sdk_math_tables.py` | `sdk_math_tables.emsm`, `sdk_soft_float.emsf` | R | the status background's SDK sine (0011E2A8); the collision world's SDK context (0011E748, D_0026C5D0; the soft-float workers' D_0024295C and errno word from `sdk_soft_float.emsf`, whose absence faults the AREA11 build at 0x001AFCA0) |
| 34 | written by step 4, or `python3 tools/export_status_models.py` alone (texels from the rebuilt GS memory; optional `--capture` against the status-hub capture) | `status_models/` | R | the status hub models |
| 35 | `python3 tools/export_pickup_programs.py` | `scene_snow/pickup_*.emsc` | R | the pickup owners 0015AFA0 / 00219550 |
| 36 | `python3 tools/export_area11_sfx.py` | `sfx/area11/panel_sfx.*` | R | the panel cues (the host loads them) |
| 37 | `python3 tools/export_sfx_registry.py` | `sfx/sfx_registry.emsr` | L | em_sfx |
| 38 | `python3 tools/export_area11_scripts.py` | `area11_scripts/` | R (since census L23: the truck trigger's script start faults without it) | the AREA11 script host (em_area11_script_host: the truck preview 0x8292C0, Roger's scripts, and since WP-8b the director's scripts 0x8294C0 / 0x829A40 / 0x829CC0 and quads) |
| 39 | `python3 tools/export_roger_resources.py`, `export_roger_encounter_actor.py`, `export_roger_cinematic.py`, `export_roger_media.py --iso /path/to/owned.iso` (from the disc; the texels of `roger.emdl` from the rebuilt GS memory; the RAM checks run only when captures are present) | `scene_snow/roger/` | R (since census L22: `trigger.empg`, `programs.emsc`, `encounter_camera.emcc`, `camera_projection.emcp`; `roger.emdl` is not read since chain C8b's FACE step (Roger draws his original unit from step 47's export); `encounter.wav` is not read since WP-8b (cue 29 plays on the stream lanes, step 22); the decoded `*.empc` banks are read only by tests) | em_area11_roger (trigger quad), em_area11_script_host (Roger's programs, the bank 0x96 camera track) |
| 40 | `cd ../Extermination && python3 tools/export_level.py --background ../extermination-port/assets/scene_snow --area 11 --sub 0 --iso <owned.iso> --capture-ee ... --capture-gs ...` (BACKGROUND.md; **still needs a capture** for the render context's draw state, `--capture-ee`; the texels are the disc's; see DISC_TEXTURES.md 9.4) | `background.embg`, manifest line | R (since the render + UI step: the AREA11 manifest load quits without it) | em_background_gs: the level background every world frame draws first (em_scene.c, em_render_frame.c) |
| 41 | `cd ../Extermination && python3 tools/export_shadow_receivers.py --out ../extermination-port/assets/scene_snow/shadow_receivers.emsr` (SHADOW_ORIGINAL.md) | `shadow_receivers.emsr` | R (since census L29: the AREA11 area build faults without it) | em_shadow_live: 001DA6A0's static-object grid, the receiver objects and the box models |
| 42 | `cd ../Extermination && python3 tools/export_shadow_proxy.py --player-emdl ../extermination-port/assets/player.emdl --out ../extermination-port/assets/player_shadow.emdl` (SHADOW_ORIGINAL.md) | `player_shadow.emdl` | R (since census L29: the AREA11 area build faults without it) | em_shadow_live: the silhouette's proxy mesh D_0028A490[0x28] |
| 43 | `python3 tools/test_actor_collision_reference.py --export` (ACTOR_COLLISION.md 3; a stand-in until a dedicated exporter exists) | `scene_snow/area11_cells.bin` | R (since census L07; a missing file faults at 0x001AFCA0) | the collision world's cell directory (em_actor_cells_load) |
| 44 | `python3 tools/export_world_models.py` (OWNER_DRAW.md 4) | `scene_snow/world_models.emwm`, `world_models.json` | R (since census L25: the crates' and drums' first tick faults without it) | 001B0EA0's model bank *D_0028A59C (em_area11_boxes); the owner units' model blocks (em_owner_draw_live) |
| 45 | `python3 tools/export_box_tables.py` (CRATES_DRUMS_ORIGINAL.md "Binding") | `scene_snow/box_tables.emrg` | R (since census L25) | D_002468B0 / D_00246A00 / D_00246A10 (em_area11_boxes) |
| 46 | `python3 tools/export_pad_tables.py` (TRUCK_ORIGINAL.md "Binding") | `pad_rumble.emrg` | R (since census L23: the truck's arm rumble faults without it) | D_0024D6F0, 001B1E20's rumble records (em_pad_actuator) |
| 47 | `python3 tools/export_roger_banks.py` (ROGER_ACTOR_ORIGINAL.md section 4; after step 39's decomp extract; the table D_0028A490[0 .. 0xAF) rebuilt from the disc, EMRS version 2 since 2026-09-28: re-export, a version-1 file is refused) | `scene_snow/roger/resources.emrs` | R (since census L22: Roger's lifecycle 0 faults without it) | D_0028A490's table, the clip banks 0x96 / 0x4A, model 0x47 and the models of D_0028A56C the live owners bind (0x6B, Roger's; 0x2F..0x3D, 0x40, 0x6A..0x6D, the ids 0018A8D0 binds; since the status UI step 2026-09-26 the span runs to 0x7A: the indicator children's 0x73..0x75 and 0x7A, em_indicator_bind_live; since chain C8b's FACE step also Dennis's face resource D_0028A490[0x18] from extract/chunk03/f16_id18.bin, 001CB3C0's face on the player: re-run it) at their EE addresses (em_area11_roger: Roger's and his equipment's models and face resource 0x88 for their 001CAA00 / 001CB3C0 units; em_equipment_live; em_owner_draw_live's attachment regions; bank 0x96 also mapped into the player record's pose host) |
| 48 | `python3 tools/export_camera_tables.py` (CAMERA_LIVE.md section 3) | `camera_tables.emrg` | R (since census L13..L16: without it the live camera does not bind and the area build faults at 0x0018B9C0) | D_0024A4B0 / D_0024A5F0: 001B1EA0's quads for 00190F20 and 00194D10 (em_camera_live) |
| 49 | `python3 tools/export_render_context.py` (RENDER_CONTEXT.md section 8) | `render_context.emrc` | R (since the render context step: the game does not start without it, fail-stop; re-export after 2026-09-27: the load veil step added the D_0026E880 block, and an older file is refused) | em_render_context_live (the .data the render context's routines read: D_00275670.., the room table D_00251C50, the skin record templates D_002514D0.., D_00250F30, D_00253170, D_0026E510, D_0026E850, D_0026E880, D_00241010) |
| 50 | `python3 tools/export_effect_tables.py` (EFFECT_MANAGER.md section 8.1) | `effect_tables.emet` | R (since the effects step: without it the effect and equipment binders do not attach and the area build faults; re-export after 2026-09-26: the D_0025DAE0 window was added and an older file fails the shadow's bind; again since WP-13: the two VU1 program packets 0x00231770 and D_00233290 were added, without which the first chain page faults; again since chain C8b FLAMESNOW: the snow program packet D_00233800 was added, and an export without the three program packets is refused at load; since chain step AIMCAM's fix round the impact handlers' source blocks D_00255620 and D_002560D0..D_002561EF are in it, without which a fired round's effect faults behind the aim/fire gate) | the effect originals' ELF windows (the entity tables 0x257C90.., D_00255430, the manager's and handlers' tables and sources, D_0024A220.., D_00248B98 / D_00248C78; em_effects_live, em_equipment_live) and D_0025DAE0 / D_0025DAF0 (the 0015BF90 decal's colour and facing, em_shadow_live); the sprite program's and the lane program's DMA packets (em_chain_page_live) |
| 51 | written by step 4, or `python3 tools/export_object_textures.py` alone (OWNER_DRAW.md 7.3; texels from the rebuilt GS memory; optional `--route-captures` cross-check; re-run since the owners step) | `scene_snow/object_textures.emot`, `object_textures.json` | R (since the object-unit step: the first drawn owner unit faults without it) | the 465 TEX0 textures of the bank's, the player's, the equipment models', the items' model 0x72's, Roger's model 0x47's and his equipment 0x6B's blocks, of the face resources 0x88 (Roger) and 0x18 (Dennis) and, since the static-world step, the 119 MODULATE textures of the static-object bank's blocks (em_owner_draw_live -> em_gfx_object_texture; re-run since chain C8b's FACE step: Roger's first drawn unit faults on an older export; re-run since the static-world step (2026-09-28): the level's first drawn triangle faults on an older export) |
| 52 | written by step 4, or `python3 tools/export_page_textures.py` alone (CHAIN_PAGE.md section 7; the TEX0 set from the ELF, the overlay and three code constants, texels from the rebuilt GS memory; optional `--route-captures` cross-check; replaces `export_shadow_decal_texture.py` and `shadow_decal.emdt` since WP-13) | `scene_snow/page_textures.emot`, `page_textures.json` | R (since WP-13: the first chain page that draws a texture faults without it) | the chain page's textures: every TEX0 a captured page draws, the TEX0 rows of 001CFBE0's source blocks D_00253670 and D_002565E0.., the weather descriptor D_00255170's (since chain C8b FLAMESNOW: the snow; re-run this step or tools/export_disc_textures.py), the 0015BF90 decal's (seven, resident and identical in every route capture) and, since chain step AIMCAM, the laser dot's 0x20045BA5154222DC (001854E0 / 00185760; resident and identical in the route captures, drawn by no captured page; re-run this step) (eight; em_chain_page_live -> em_gfx_gs_texture) |
| 53 | `python3 tools/export_player_model.py` (OWNER_DRAW.md section 10; after step 44's decomp extract; the address is checked against D_0028A490[0x3B] rebuilt from the disc) | `scene_snow/player_model.emom`, `player_model.json` | R (since the player step: the spawn's 0015C1F0 faults without it) | the player's model, resource 0x3B at 0x00D1C1C0 (em_player_draw_live: 0015C1F0's 001C6150 and 0015C160's +0x4C unit) |
| 54 | `python3 tools/export_status_pages.py [--iso /path/to/owned.iso]` (STATUS_PAGES.md section 7; the disc model of DISC_TEXTURES.md and the user's ELF) | `status_pages/status_pages.emsp`, `status_pages_source.json` | R (the level smoke's status_pages run fails without it; the main route does not open a page: without it the status pages MAP / SPR4 / DATABASE / EQUIPMENT / EVENT / HEALING stay unbound and fault when opened, as before chain C8b; reported at the host's load) | em_status_pages_live: the pages' .data windows, the record container and the GS memory the page textures decode from (em_gs_texture); version 2 (chain C8b's MAP step: re-export) adds module 0x1E's relocated slot word D_0028A570, the MAP bank's address (a version-1 file loads, and MAP then faults when a node binds a map model) |
| 55 | `python3 tools/export_status_map.py [--iso /path/to/owned.iso]` (STATUS_PAGES.md section 7, "MAP"; the disc model of DISC_TEXTURES.md) | `status_map/map_XX.emdl` (22), `status_map/map_models.emmp` | R for the MAP page with a map owned (the map take 0x08 opens it; without the export the node that binds map 8's model faults at 001C6120, reported at the host's load); MAP with no map owned does not read it | em_status_models' MAP bank D_0028A570 (module 0x1E slot 0x38): the 22 map models with their texels (module 0x1E's upload applied to the first level's GS memory), directory offsets, node counts, radii and skeleton records |
| 56 | `python3 tools/export_module_loader.py [--iso /path/to/owned.iso] [--elf ../Extermination/config/SCUS_971.12]` (MODULE_LOADER.md section 4; the disc image and the pinned ELF) | `module_loader/modules.emml` (EMML version 2 since chain step H7; since chain step PAGELOADS it holds the page modules too: re-export), `modules.json` | R (since chain C8b LOADER: the game does not start without it, fail-stop) | em_module_loader: the disc sectors the screen-module loader reads (module 0x21's header and chunk; since chain step H7 module 3 and AREA11's overlay, header, sound bank, A entry and resident region; since chain step PAGELOADS every status page module's header, chunk and payload, 0x1E..0x24 and 0x2C..0x31; 14.7 MB), the INDEX.IDX / DATA.DAT descriptors, the loader's cursor seeds (MODULE_LOADER.md 1.8) and the ELF's D_0028A3C0 / D_00275304 / D_00264890 |
| 57 | `cd ../Extermination && python3 tools/export_shadow_proxy.py --kind 0x29 --player-emdl ../extermination-port/assets/player.emdl --out ../extermination-port/assets/roger_shadow.emdl` (optional check: `--verify-ram build/s87/route/14_roger_encounter/eeMemory.bin`; SHADOW_ORIGINAL.md "Roger"; after step 42) | `roger_shadow.emdl` | R (since chain C8b's FACE step: the AREA11 area build faults without it) | em_shadow_live: Roger's silhouette proxy D_0028A490[0x29] (001BA580's 001DA6A0, kind 0x29) |
| 58 | `python3 tools/export_static_world.py` (STATIC_WORLD.md section 3; after step 44's decomp extract, with the pinned ELF) | `scene_snow/static_world.emsw` | R (since the static-world step, 2026-09-28: the AREA11 area bind faults without it, fail-stop) | em_render_context_live: the static-object bank *D_0028A5A0 (0x01516F40, 0x2D6FE0 bytes, read only) and 001E1E60's .data D_00253560..EF (001C1D00's tree, 001D52E0) |
| 59 | `python3 tools/export_aim_fire_tables.py [--elf ../Extermination/config/SCUS_971.12]` (AIM_FIRE.md section 2; the pinned ELF, SHA-256 checked) | `aim_fire_tables.emaf` (EMAF v4; re-run after chain step AIMCAM: the loader refuses v3) | T (read only behind the aim/fire diagnostic gate; without it the gated workers fault at their first table read) | em_aim_fire_tables: the player weapon's clip / sound / tint rows D_00248680..D_00248CFF, the reticle templates D_002533D0..D_0025348F, the beam reference D_00253720..D_0025373F, the cable state templates D_00266930..D_00266ADF and the R2 aim camera's eye offset D_002754E8..D_002754F3 (00198440; em_camera_live through its `memory` view) |

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
- Steps 1, 6, 10 and 12 give the tool and its recorded mode; their full
  argument lists are in the decomp docs cited. For 6, SHADOW_ORIGINAL.md
  notes that eight clips of the installed `player.emdl` (0, 64..67, 69, 71,
  348) differ from a fresh `export_native.py` bake and what produced them is
  not established (the texture blob is the disc's either way:
  test-disc-assets-reference).
- On 2026-09-28 every step except 6, 8, 10, 11, 12, 30 and 40 was re-run
  from a copy of the tree whose `../Extermination` held no `build/` (no
  captures), and every file the full level smoke opens was byte-identical to
  the installed one except `roger/resources.emrs` (EMRS v2, the table's
  0xAF words), `panel/status_hub.emhs` (the arc words 00208AD0 rewrites
  before reading) and `scene.txt` (the same lines in another order); the
  full smoke passed on that tree (DISC_TEXTURES.md 9.3).
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
  textures come from the first level's GS memory rebuilt from the disc (the
  hub loads no page module; DISC_TEXTURES.md; `make test-status-models`).

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
- The opening's actors are records (chain C8b OPENING, OPENING_ORIGINAL.md):
  the script 0x828FC0 runs on the AREA11 script host, its op14 spawns the
  two 001BB0E0 records (Roger's opening body on bank 0x98's clip 2 and the
  model-0x6B node on it), and the player plays bank 0x98's clip 1 on its own
  stage; their node matrices equal the opening capture's bit for bit.
  Missing required resources fail explicitly.
- Random arithmetic matches the original SDK leaf, including 32-bit stored
  state and 31-bit output. The call order is audited against the C7
  per-call capture (RAND_ORDER.md): from the area entry the port equals the
  original call for call up to the opening's actors' spawn, which the
  stream request's wait moves (the security gun's AE+1 draw and the faces
  of Roger's owner and the player are among the equal calls), and every
  frame's fixed-schedule callers equal the original's.
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
make test-collision test-script test-area11-opening
make test-cinematic-camera test-opening-media
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

## Developer switches

Neither is a launcher option (LAUNCHER_OPTIONS.md "Not launcher options").
Both are read from the environment at launch.

### EM_NEW_GAME=1: straight to the AREA11 opening

Added 2026-09-30 (workflow chain C10's New Game switch step; the request
came from the workflow's contract): skip the startup frontend and land at
the start of the AREA11 opening with normal control afterwards. Its name
does not end in TEST, so the window stays (`EM_HEADLESS=1` still makes the
run headless). `src/game/em_new_game_switch.{h,c}`, `em_frontend_install_new_game`.

- **What does not run:** the frontend, `em_startup.c` (001AB7E0, 001AB9D0,
  001ABC60, 001ABE10, 001AC070 states 0..3, 001AC3B0, 001AC480): the warning,
  Sony and Deep Space screens, the attract E900 movie, the title menu and
  their sounds.
- **What runs:** the title's New Game handoff, 001AC070 state 4 with
  D_00275BE0 = 0, through the same `em_game_install_new` the frontend's
  `EM_STARTUP_NEW_GAME` event calls; then the whole New Game chain as on the
  title route (001AD1A0, 001AD230, 001AD360, 001AD250, 001ADF50 and the
  state-0 rebuild). The intro movie 001AD360 step 1 requests is played by
  the same movie service and skipped through the original skip test
  (002036E0: held & 0x800, at the decoder's completed-picture index 11):
  the switch holds START from the movie's first picture, so the skip is
  taken at the first picture the original accepts it (picture 11, about
  0.37 s; the title fixtures hold it from 2 s). The hold is one-shot: it
  ends when that first game-task movie closes, so any later 001AD360 step 1
  movie in the same run plays and skips by the player's pad alone. The
  shared audio device opens at the boot, as the frontend's boot resources
  open it.
- **What the frontend leaves that the game reads:** nothing that differs.
  The boot loads and banks: the port's frontend loads only its own screens
  and title sounds (the screen modules 0x28 / 0x29 / 1 and the resident
  banks 0x1B / 0x1C are not loaded by the port on either route; 001AD1A0's
  library packet comes from the disc export). The loader seeds, the stream
  boot and the sound driver run in `main` before either route. D_00275BE0
  is 0 from `em_game_install_new` on both. The settings are the launch
  switches (em_settings) on both; the title's Option page is not entered.
  The fade the title leaves (black) is cleared by 001ADF50's 001AED80
  before the opening on both routes. The title's 001AC3B0 state 0 sends
  001FBC50's two IOP commands 0x16 (effect-return volume 0x1999 on both
  cores) that the switch route does not send; both routes send the same
  pair again after the AREA11 sound-bank upload (command 0x20), so the
  driver state is equal and only the drained command ring's history
  differs (21 commands against 19 at the opening's first frame).
- **Proof:** `make test-new-game-switch` (tools/test_new_game_switch.py,
  about 9 s): five headless runs write a state image at the opening's
  first frame (`EM_NEW_GAME_STATE_TEST`): the title route
  (`EM_STARTUP_TEST=newgame`) with the title held 0..3 frames longer
  (`EM_STARTUP_MENU_WAIT=0..3`), and the switch. The image is every writable
  static section of the executable (every module's static state: the scene
  state with the D_00810700 block and the request bytes, the player record,
  the task table, the stream lanes and sound bank, the sfx driver, the
  render context, the fades), the module loader object and its modelled
  original bytes, the IOP and the field parity D_00810E80. The parity at
  the opening depends on the frame New Game is pressed, and every buffer
  it selects differs with it, so the switch is compared with the title run
  of its own parity (title+k), and the words that count the frames since
  the boot are taken from title+k against title+k+2 (the same route two
  frames later). A word may differ between title+k and the switch only as
  a pointer in both runs; host-only state (the frontend's own state, whose
  game-facing part, the movie service and the title sequencer's pending
  sounds, is compared and equal in all five runs; the sfx driver's host
  clock; the frame pacing's wall-clock deadline); the drained IOP command
  ring; the audio thread's mixer ring and its digest; or one of those
  frame-count words, which the test pins per symbol: the main-loop counter
  (1 word), the stream's field counter D_00810E90, a lane's field stamp and
  the chain page log's main-loop stamp (3), step V's kick count (1), the
  loader's drive clock (1) and the IOP clock with its SPU2 DMA stamp (4).
  A new symbol or more words fail the test, as does anything else, with
  its symbol. Measured 2026-10-01: nothing else differs, and the module
  loader's modelled bytes are equal. Negative controls: a switch that does
  not open the audio device (or, 2026-09-30, sets D_00275BDC) fails. The
  refusals are checked too. EM_TEST_FULL=1 adds newgame-control on both
  routes: the same PASS line (locked_ticks 1301, displacement 9.599849,
  census 49).
- **Refusals** (exit 1 before any window): a value other than 0 / 1; with
  `EM_SKIP_STARTUP=1`; with an `EM_STARTUP_TEST` fixture that tests the
  frontend itself. The New Game fixtures (`newgame`, `newgame-control`,
  `newgame-skip`, `newgame-level`) combine with it (only their title part
  does not run): `EM_NEW_GAME=1 EM_STARTUP_TEST=newgame-control` passes with
  the title route's numbers.
- **Not equal by design:** the main-loop counter at the opening is smaller
  than any title route gives (the port reads it only as a same-frame
  stamp), with the other frame-count words above; and the field parity is
  whichever the switch reaches (both values occur on the title, depending
  on the frame the player presses New Game).

```sh
EM_NEW_GAME=1 build/extermination
```

### EM_SKIP_STARTUP=1: the older debug fixture

`em_game_install` (em_game.c): no frontend and no New Game. It reads the
staged fixture scene (`assets/scene`, or the one `EM_SCENE` stages) at once
through the native area read and enters the game task where a completed
001ADF50 leaves it (state 0 next tick). It skips 001AD1A0, 001AD230 (the
001AF2C0 reset, apart from the inventory wipe), 001AD360 (the area bytes
and the intro movie) and the loader's steps, and it keeps the demo status
values (health 75, infection 60, mag 4, reserve 120). It serves the
legacy self-tests (`tests/run_suite.sh` and the EM_*_TEST fixtures of
em_game_selftest.c); it is not the original route and does not reach the
AREA11 opening.

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
  script starts and resumes cue 25 at the end. The area-entry call is bound
  (with the room move's `001FAE70(0)`; RAND_ORDER.md section 2): it draws
  the first `rand()` after New Game and issues cue 25's read, which the
  opening's prefill then waits for, as in the original. Every other stream call runs on the original stream lanes
  since WP-8b (em_stream_live over em_stream_lanes_original and the IOP
  backend em_iop_stream; STREAM_LANES.md, IOP_STREAM.md): the status
  close's `001FAE70(1)` (0x1AE040 state 5), the stream stop `001FABB0` at
  the status open and at 001AD360 step 0, the `00119828` IOP commands 0x16
  of `001FBC50` and `001FC280` (the driver's effect-return volume, kept but
  inaudible without SPU2 reverb), and `001FAE70`'s infected override
  (cue 0x18, exported). The former `EM_BGM` debug override is gone.

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
console, so that false pickup/examine placement has been removed. See
docs/OPENING_SCENERY.md. The current status of the first level's owners
(the panel, the weather node 001E55F0 and the snow among them, all live) is
in docs/FIRST_LEVEL_AUDIT.md and docs/FIRST_LEVEL_CENSUS.md.

The title audio sequencer preserves the original events, waits, pitches and
sample data, but its current dry mixer does not reproduce SPU2 ADSR, Gaussian
interpolation, reverb or hardware voice allocation. Native storage currently
maps the successful card-check path to a local directory; full save/load and
space handling remain incomplete. Load Game, Option and attract services remain
pending rather than manufacturing an outcome. Windows/Linux movie backends and
other existing gameplay approximations also remain unfinished.
