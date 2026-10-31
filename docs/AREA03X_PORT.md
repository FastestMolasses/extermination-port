# AREA03 subs 1 and 2 and AREA04 sub 1 in the shared composition (lane N1)

Lane N1, 2026-10-31, branch `area03x`. AREA03's two later subs and AREA04's sub 1
run in the shared composition as AREA03 sub 0 does (AREA15_PORT.md). The route's
ground truth is SIXTEENTH_LEVEL_ROUTE.md and SEVENTEENTH_LEVEL_ROUTE.md. This is a
start-card binding: row-exact New Game chaining is a chain lane's.

## Beats

`python3 tools/mid_route_beats.py --single <beat>` from each beat's own card, unless the
row says chain. "Passes" is the tool's whole check: recorded pad and path, no fail-stop,
end block, end position within 4 units. No row of these was compared: every walk is
steered, and no group here ran from New Game.

| Beat | Status | Stops and why |
|---|---|---|
| a03b_00 | no fault, door [13] reached, end position good | end block: health 5 for 65, infection 50 for 30 (steered back into the steam, below) |
| a03b_01 | fails | the crawl overshoots the junction (557, 735.5) and ends at (582.9, 55, 735.5); health 25 for 45. a03b_02 and a03c_01 cross the same ducts |
| a03b_02 .. a03b_05 | **pass** | [22]'s script 0x827170, flag 0x29, items, the ducts, the curtains, the slider |
| a03b_06 | **passes** (the lift, AREA08's load, with lane P1's assets) | |
| a03c_00 | **passes** (chain and card) | |
| a03c_01 | end position good | end block: health 70 for 90 (a leap the recording does not take) |
| a03c_02 | not passed | the creatures kill the player: superseded by a03d_00 |
| a03c_03 | end position good, AREA04 sub 1 loads | end block: health 58 for 56 |
| a03d_00 | **passes** (chain and card) | the seal [23] |
| a03d_01 | **passes** from its card (door [24], AREA04 sub 1's load); the chain a03d, which runs on from a03d_00, fails it: the player is bitten at the door and door [24] is not opened | |
| a04c_00 | fail-stop | see "the held face" |
| a04c_01 | not passed | the creatures kill the player: superseded by a04d_01 |
| a04c_02 | not re-run since AREA07's assets came in | it played to the load before |
| a04d_00 | **passes** (chain and card) | 0x825040's polygon, script 0x829020 (8,155 frames), the items |
| a04d_01 | end position good | end block: health 52 for 60, infection 100 for 85 (the 00147390 creatures' contacts) |
| a04d_02 | **passes** (the lift, AREA07's load) | |

## How the beats are run

`python3 tools/mid_route_beats.py` with `build/beat_rands/<beat>.txt`
(`tools/record_beat_rands.py`, lane P1's, recorded for every beat here) now passes
three comparison anchors. None of them writes anything outside a comparison run:

- `EM_START_AT_RANDS`: the recording's rand() word per script frame. A card's draws
  of a frame need not fall in the original's order across owners.
- `EM_START_AT_LOADS`: at the fade-in after each room change the script's clock
  jumps to the recording's row: the host drive ends a load sooner than the
  recording's, and a clock left behind gives the world frames the recording
  does not have (a03c_00: the creature bit the waiting player).
- `EM_START_AT_RESTORE` on `RESTORE_STEERED` beats: the source state's records of
  the creatures 0013D2D0 and 00147390 at frame 0.

`LEAD['a03c_00'] = 210`: AREA03 sub 2 entry 1 replays the lift's arrival at a loaded
game. `EM_START_AT_TRACE=<file>` writes the player, vitals, rand() word and creature
states per script frame.

A steered card is not exact. While a reaction holds the player the script's clock
waits and the world does not, and the steering pushes back toward the recorded
place: a03b_00's recorded single steam contact becomes a run of contacts, each the
original's -5, so that beat ends at a lower health than the recording's. The
position, the area bytes and every progress byte but the vitals are the
recording's. Those differences are the harness's; they are listed per beat.

## What runs where

- **Exports** (`tools/export_area03x_common.py`, `_level`, `_tables`, `_sfx`,
  `_textures`, `_split`): the AREA13 lane's exporters over the sixteenth level's
  captures, keys `03_1` (eight captures), `03_2` (a03b_05) and `04_1` (three). They
  write `build/area03x/assets/tree/` and `_split` places `assets/levels/03/sub1`,
  `sub2` and `assets/levels/04/sub1`; every side file equals the existing export.
  001D40E0's and 001D4440's colour words are run-time, both stores read back at once
  (COLOUR_WORDS). Sub 2's second load keeps another stale D_0028A5A4 word: it is not
  used, a03b_05 is the capture.
- **Creatures:** `em_level16_creature` (the 0013D2D0 states 0013D980 .. 00140A90,
  001416D0) and `em_level16_beast` (00147390's family, 26 entries, 00148B40 among
  them), through the fallback dispatch. Oracles: `test_level16_creature_reference`,
  `test_level16_beast_reference`, every reachable word.
- **Boot and overlay owners** (`em_level16_boot`, `tools/gen_level16_boot.py`, oracle
  `test_level16_boot_reference`, every reachable word): 001B3670, 001B3C30, 001C82A0,
  00158430 (the seal), 0021C040; AREA03's 0x8235A0, 0x823810, 0x825200; AREA04's
  0x824EF0 (with 0x825040, 0x8251C0) and 0x825310 (with 0x825280), through
  `em_area01_runtime_area03x.inc`.
- **Elsewhere, the other lanes' one definition:** 001ED450 and 001BD460 (lane P1:
  `em_level14_port`, `em_area08_route`; the first stays in this module's oracle),
  0021BED0 (`em_level8_port`), 0021BF90 and 0017C370 (lane J1's), 0019A440, 001A1B80,
  0019DB50 (lane J1's `em_coll_segment_walkers`; `test_coll_segment_walkers_reference`
  keeps this lane's added cases), the SDK's 0011CB90 and 0011E080 (`em_area01_live`).
- **This lane's outside the module:** 001C81C0 (`em_pose_host_workers`) with
  `em_area01_model_clip_pose`; 001C94B0 through `em_owner_services`.
- **Storage:** `scratch_3900` (0x70003900..0x70003A23), `pose_buffers`
  (0x00286340..0x00289B3F, 00148B40's four 0xE00-byte buffers).
- **Boot data:** `AREA04X_WINDOWS` in export_area00_runtime_tables.py; boot constants
  for area 4 (`em_boot_constants_select`), groups a03b, a03c, a04c, a03d, a04d.
- **Scene:** `mid_area` takes AREA03 subs 0..2 and AREA04 sub 1; `mid_file` the level
  pack's tree; AREA04 sub 1's boot owners bind as AREA15's.

## Shared code this changed

`em_world_textures_live`: AREA15's and AREA03's mode-0 catalog was the first level's
path (`assets/scene_snow`): fixed, they read `assets/levels/..`. `em_sfx_bank`:
EMSR_MAX_SAMPLES 4096, EMSR_MAX_ENTRIES 65536 (each loaded sub adds a scope).
`em_start_at`: `EM_START_AT_LOADS`, `EM_START_AT_TRACE`; a rand anchor skipped by a
clock jump is passed over. `tools/mid_route_beats.py`: a beat may start on the
previous beat's last counter. `export_level_world_textures`: 001D5370's dynamic pass
in (3, 1) and (4, 1).

## Not drawn, stand-ins, open items

- **Stand-in:** the tendrils 001546C0 in AREA03 and AREA04 are not drawn (001CB1F0 /
  001CB130 / 001CB060 have no VU1 translation, as AREA19's: the log says so once). Their
  logic runs.
- **001C81C0 has no oracle** of its own; its parts are em_pose_host's (001C8480,
  001C8F10, 001C90D0, 001C92C0, 001CA0A0, 001CA1C0, proven there), the loop is new.
- **The held face** (a04c_00, fail-stop): in AREA04 sub 1's event with the player at
  infection 100 (mode 1), 001CB3C0 of the player faults at 001D3F08: the face a script
  holds is not in the regions. It stays a fail-stop; a04d_00 (infection 85) does not
  reach it. A run at infection 100 stops there.
- a03b_01's crawl, the chains a03b and a04c (they stop where the single beats stop).
- 0019DB50's no-span path (the original walks its caller's registers) returns -1, as
  0019D770 does.
- AREA03 sub 2's registry has 49 ids `unbound bank` (group 4 bank 2's handle was freed
  in a03b_05): a sound of that bank is silent.
