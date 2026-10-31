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
  001F28D0 (`em_area08_route`, the particle ring AREA07 sub 0 and AREA19 meet,
  155 of 155 reachable words),
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
- `em_status_models`: 0020E6F0's other model words load (0x3D, 0x3F, 0x40, each
  with its own GS image, tools/export_status_models.py VARIANTS). An infected
  player (D_008104E4 == 1) opening the status screen stopped at 001CA6E0 in every
  area. The coat stays tools/export_status_coat.py's.

## Stand-ins and open items

- **Method 001CB1F0 (kind 10) is not drawn** in AREA08 and AREA07: the boxes
  (001546C0 -> 00154F00) take the method J1 left undrawn in AREA19, as its VU1
  program has no translation. Logged once; only the picture depends on it.
- **The infected menu player's glow is not drawn** (`w_001CD520` in
  `em_status_models`): 001F4BF0's random draw stays, nothing reads the result, and
  the line is logged once. Only the picture depends on it.
- **Card runs are not the recording's machine state.** A card reloads the bugs and
  their spawners, so a beat's bites, health and infection differ. See the beat
  table.
- **a08_04's hand-over-hand** depends on the heading reached under the stick; the
  run's heading leaves the recording from row 24 (the walk's first clip change)
  with the camera equal, cause not found. a08_05, a07b_04 and a07b_05 leave by a
  small heading step at rows 19..25 the same way; a card's pose may be why (the
  idle clip's phase is not the recording's), which is not proven.
- **Bug spawn timing**: in a08_01 the bug 0x7B0C60 appears at row 415, the
  recording's at 417: a spawner's timer is not on the card, and the bug restore
  does not carry it.
- **Restored 001BF6B0 nodes** (anchor and both modes only): a08_02 and a08_03 fault
  at 001D0D60 over a restored node's track pointer. Steered runs do not restore, so
  those beats run in steer mode.
- 001FBE80 needs an oracle case.

## Beats

Each beat runs from its card through `tools/mid_route_beats.py --single`, in the
mode `BEAT_MODE` gives it (`EM_MID_ROUTE_MODE` overrides): **anchor** is the
recorded pad, the source's bugs and the rand() table; **steer** is the steering
alone; **both** is both. Anchors are comparison only. Rows are
`tools/test_area15_rows.py <beat>`: position and heading within 0.001 and the action,
aligned at the longest identical opening. A steered beat's rows do not measure
anything: the steering changes the stick. Results of 2026-10-31.

| Beat | Mode | End | Identical opening, total rows |
|---|---|---|---|
| a08_00 | anchor | PASS | 10, 172 of 406 |
| a08_01 | anchor | position; infection differs (D_0081085E) | 43, 547 of 1,186 |
| a08_02 | steer | position; D_00810707, health, infection differ | steered |
| a08_03 | steer | no end: game over at engine frame 9024 | steered |
| a08_04 | steer | misses the swing, 180 units off, falls to the live floor | steered |
| a08_05 | anchor | PASS (terminal, flag 0x18, CC3 0x13) | 25, 824 of 1,475 |
| a08_06 | steer | no end: dies at (126, 220, 205), no lift, no AREA03 load | steered |
| a07_00 | steer | position; health differs (D_0081085A) | steered |
| a07b_00 | both | position; health, infection differ | steered |
| a07b_01 | anchor | PASS | 7, 321 of 2,053 |
| a07b_02 | anchor | PASS | 897, 1,075 of 1,389 |
| a07b_03 | both | PASS | steered |
| a07b_04 | anchor | PASS (flag 0x25, 0x15, CC3 0x11, 0x14) | 22, 3,835 of 4,299 |
| a07b_05 | anchor | PASS | 19, 481 of 1,507 |
| a07b_06 | anchor | PASS | 1,786, 2,061 of 2,419 |
| a07b_07 | both | stops at the lift's load: the pack has no AREA04 sub 1 (lane N1) | steered |

Nothing here is row-exact from New Game, and no beat is row-exact to its end. In
anchor mode a08_01's rows hold within 0.05 to row 630, where a bite the port does
not take ends them. a08_04's rows were read in anchor mode too, exact through row 23.
