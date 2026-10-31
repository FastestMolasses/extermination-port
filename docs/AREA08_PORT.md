# AREA08 and AREA07 in the shared composition (lane P1)

Lane P1, 2026-10-31, branch `area08`. AREA08 (subs 0, 2, 3) and AREA07 (subs 0,
2, 3) run in the shared AREA01 composition, as AREA15 and AREA03 do
(docs/AREA15_PORT.md). The route's ground truth is SIXTEENTH_LEVEL_ROUTE.md and
SEVENTEENTH_LEVEL_ROUTE.md. This is a start-card binding: nothing here is
row-exact from New Game.

## What runs where

- **Overlay state** (`em_area01_state.c`, `k_mid`): AREA08 id 8, text 0x1BC0, data
  0x2E00, BSS 0x827F00 + 0x140C00; AREA07 id 7, text 0x2640, data 0x2500, BSS
  0x828080 + 0x140C00. 001E7780's keys 0x800..0x806 and 0x700..0x704 run the init
  (0x8241F0, 0x823970: the same four stores as AREA15's).
  `make test-area15-area03-state-reference` runs the original over both areas.
- **Owners** (`em_area01_runtime_area08.inc`, `dispatch_a08`):
  - `em_area08_route`: AREA08's 0x823580, 0x8235E0, 0x823630, 0x8237B0, 0x823870
    (door [6]), 0x823AC0 (sub 2's emitter), 0x824210, 0x824460, 0x824490, 0x8245E0
    (terminal [2]), 0x824790, 0x824870, 0x8249F0 (sliders), 0x824BC0 (the flame
    columns); AREA07's 0x823DA0, 0x823990 (terminal [7]), 0x823B40, 0x823C20; boot
    0012DD70, 00130AB0, 00133CD0, 00157360, 00159FC0, 0015A070, 0015A750, 001BD370,
    001BD460. `make test-area08-route-reference`.
  - `em_area07_route`: 0x823F80 (the turret), 0x825940, 0x8259D0, from the AREA03
    twin. `make test-area07-route-reference`.
  - `em_area07_probe`: 0x825470. `make test-area07-probe-reference`.
- **Boot translations elsewhere**: 001C9570 (`em_owner_services_original`, in
  test_owner_services_reference), 001C6910 (`em_pose_host_workers`), 001ED450
  (`em_level14_port`, in test_level14_port_reference), 001F9180
  (`em_shadow_actor_route`, in test_shadow_actor_route_reference), 0019A440 with
  001A1B80 and 0019DB50 (`em_coll_segment_walkers`' third mode, in
  test_coll_segment_walkers_reference),
  001FBE80 (`em_area01_audio_services`: composed from the translated 001FBF50 and
  the driver's status, request and stop; **no oracle case of its own yet**).
- **Scripts** (`em_area01_script_live.c`): AREA08's seven chains, window 0x825880 ..
  0x8274F0; AREA07's four, 0x8264B0 .. 0x827520. The boot panels window now starts
  at D_00246A20, the switch's script.
- **Assets** (`tools/export_area08_assets.py`, over export_area08_common.py, the
  AREA13 lane's exporters unchanged): `assets/area08/sub{0,2,3}/`,
  `assets/area07/sub{0,2,3}/`, `assets/levels/08`, `assets/levels/07` (receivers,
  world textures, player variants). AREA07's (0, 0) texture catalog is the disc
  replay alone: every AREA07 recording has the player's packet at mode 1.

## Shared code this changed

- `em_sfx_bank`: registry limits 2048 samples, 32768 entries.
- `em_player_stage_live`: D_00810770 is read from the progress block (it refused
  AREA08 sub 2 before).
- `em_effects_live`: D_00248200 (0015A750 rewrites it) is writable and spans two
  windows.
- `em_sfx`: the track words D_00281B70 / D_00281C30 are readable by an owner that
  keeps its own voice (0x824BC0).
- `em_scene_restore_bugs` (comparison only): in AREA08 and AREA07 also 0012E3A0 and
  001383C0 nodes.
- `em_start_at`: `EM_START_AT_RAW`.

## Stand-ins and open items

- **Method 001CB1F0 (kind 10) is not drawn** in AREA08 and AREA07: the boxes
  (001546C0 -> 00154F00) take the method J1 left undrawn in AREA19, as its VU1
  program has no translation. Logged once; only the picture depends on it.
- **Card runs are not the recording's machine state.** A card reloads the bugs and
  their spawners, so a beat's bites, health and infection differ. See the beat
  table.
- **a08_04's hand-over-hand** depends on the heading reached under the stick; the
  run's heading leaves the recording from row 24 (the walk's first clip change)
  with the camera equal, cause not found.
- 001FBE80 needs an oracle case.

## Beats

<!-- BEATS -->
