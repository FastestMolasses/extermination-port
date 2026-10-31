# AREA00 first-visit binding

2026-10-13, written on branch `level3` (based on port `d86f73c`, worked in
the `../extermination-port-level3` worktree, merged into main 2026-10-14). AREA00 now loads through the shaft exit,
but **arrival and first world frames do not pass**. That checkpoint stopped at
arrival frame 0 because its 579-key texture catalog exceeded the
then-protected 512-slot graphics registry. The 2026-10-18 capacity follow-up
is below; historical smoke rows have not been relabelled as passing. On the
2026-10-13 `level3` branch no protected file, decomp source, README or user
test runner was edited, nothing was merged or pushed, and no emulator was
launched. On the 2026-10-18 `levels` branch, protected-file edits stayed
within the authorized object-texture registry exception, and fork
recordings were made through the decomp's hidden, run-locked `route_capture`.

## Current state

**2026-10-30, lane H1 (branch `area13`): New Game row-exact through the lift's
AREA13 load, the lobby, the outside, the hatch and the shaft, to AREA19's load.**
`--run --until a13_05` (the bed branch; drive tables `build/drive_replay_bed`,
anchors `build/rand_replay_bed/route_a13_20261030/route.txt`): 85 live
phases, about 20 minutes.

| Beat | Rows | What it covers |
|---|---|---|
| a04b_04 | 2,083 | the lift past the live creature, AREA13's load (state 0 at row 1751), the car, control |
| a13_00 | 7,063 | out of the car, the lobby, door [8] (entry 2), [3]'s scene (camera scene 7), the document page |
| a13_01 | 572 | button [16] of door [14] (entry 8, outside): the weather and the breath start |
| a13_02 | 1,098 | outdoors, the cold's point of health, door [17] (entry 4), [4]'s scene, item 0x1A |
| a13_03 | 562 | pickup g[5], item 0x27; the holes [5] / [6] end |
| a13_04 | 812 | the ledge climb, hatch [62], its script, counter 0x61 |
| a13_05 | 176 of 925 | the ladder down to y 143.1, 00193EB0's request 13 00 09 01 (ROW_LIMIT: rows before AREA19's load) |

What the lift's load and AREA13 needed, each from original code:

- **Area state:** `em_area01_state` (MidOverlay: id 10, text 0x67C0, data
  0x4580, BSS 0x82E280 + 0x140C00); key 0xD00's init 0x8236E0 through
  em_level10_port's translation. `tools/test_area13_state_reference.py`: the
  original 001E7780 / 0x8236E0 over the AREA13 fork captures (24 runs, the
  globals, data and BSS equal).
- **Runtime:** `em_area01_runtime_area13.inc`, dispatch_a13: em_area13_route's
  four owners, em_level9_port's and em_level11_port's overlay entries and the
  boot entries 001BA7F0, 001BDCA0 / 001BDD70, 001E7050 through the generated
  hooks (gen_mid_route_bridge: m09, a13). em_level8_port's and the boss
  family's boot entries, em_area06_port's (the strip owner 00219870 of g[11])
  and em_area02_math's serve area 0x0D too; their tables' overlay addresses
  are AREA04's and AREA06's, so only the boot ones. Added: 0x824060, 0x824160
  and 0x8247B0 (em_level11_port, with oracle cases), 0x828F30 (empty).
- **Scene:** collision, cells, roster (deferred groups 0x829D00 / 0x82A230 via
  001B6660), world models, message bank, script window 0x82A360..0x82D190 and
  the op09 callbacks 0x823A10, 0x826FB0, 0x826FC0, 0x8246D0, 0x8247B0,
  0x8248C0, 0x8249F0; no dynamic list (id 0x45 is not relocated); the owner
  allowlist; `background background.embg` in scene.txt (the sky: flags
  0x20 / 0x21 of 001C1F50's key 0xD00).
- **001E1E60's boot data D_00253560** came only with AREA11's static bank, so a
  loaded game drew its background from zeros: em_rcl_static_world_block_load.
- **Camera:** 00195130's area-0xD case: 0x823FE0 over the camera's map (the
  overlay's quads through the host's memory view), 001B0C60 through the host
  (`request_area`); scene 7's subtitle track D_0026ACB0 (0022EC30, em_area01_
  timeline, its window in timeline.emsp).
- **Outdoors (entries 8..10, D_008106C8 = 0x30099944):** 001C1EA0's weather
  node runs em_snow_runtime_tick_outdoor: em_weather_tick with the flag word
  and the area key, D_008106BF, 001E5AC0 through em_level9_port over an image of
  the node's block, the fog calls, the close of the list. 001DE920's packets
  (already bound) are a slot-0xFFF pass: its 001CB760 goes through f_001CB760
  now, which records the pass. 001B07C0's flag bits 4 and 0x60: 001EFE00
  (0x80000018) at the placed player: the breath 001E7050; +0x304 and +0x1C live
  in the record, leaving the outdoors sets the object's +4 = 2. **The cold's
  health loss** is the original owner's, one point every 360 frames outdoors
  while item 0x1A is not held; a13_02's rows are equal, 45 -> 44 in the
  recording's numbers.
- **The coat:** a room move's 001B07C0 sets D_00810C60 before 0015C1F0, whose
  00200890 sends the player's texture packet: the status byte is stored first
  and the packet goes up in state 4 (at state 0 the loader's state 7 sends
  it).
- **Chain page:** REGLIST GIF tags (FLG 1) and two packets in one page DIRECT:
  001E6F60's quad, 0x829AA0's 24-frame fade (001CB5F0, depth 0xFFE000).
- **Sound:** 0x44F is absent in AREA13 (remap 0xFF, D_00282164 = -1 in the
  captures): 001FC280 goes on, 0011A070 takes the -1. The wind 0x44E needs the
  pan slide and the script loop; both in the registry and the driver, in
  lockstep with the original through the first jump (test_area11_sfx_reference
  pass V, 7,772 ticks).

Free roam from the cards with scripted pads (no fail-stop): door [8] back
(entry 1), button [15] from outside (entry 3), door [17] out from the east room
(entry 9, with the coat), the lift back to AREA04 entry 7, a walk through the
lobby with the bugs' bites, the crate row. Not reached: door [20], hole [6] and
hatch [63] (the south field lies behind [44]'s event, TENTH / ELEVENTH_LEVEL_
ROUTE.md: no fork state to build a card from). The shaft's AREA19 load stops at
the module pack (00112440): AREA19 is not hosted.

**2026-10-30, lane E1 (branch `route-chain`): New Game row-exact through
AREA04's second visit, the pistol fight and the lift, to AREA13's load.**
Whole-run checks, each with the rand() order and rand-replay anchors:

- `--run --until a22b_01` (canonical recordings): 73 live phases, about 14.5
  minutes; `--until a06_05` adds the beam's collapse (sides a06_04, a06_05).
- `--run --until a04b_03` (the bed branch below): 78 live phases, about 17.5
  minutes. Every row equal, every beat's end progress block equal, and at
  a04b_03's last row the creature's record (0012E3A0 at 0x7C2660) equal to the
  end RAM.
- `--until a04b_s7`, the pistol recording from a04b_03's end, with no anchor
  after the area entries: a04b_s5 1,937 rows (the fight, the creature dead at
  f1166), a04b_s6 288 (the drop), a04b_s7 1,764 of its 2,410 (to the row before
  AREA13's load, which has no host). Every row holds the creature and event
  [3]'s four bugs: the New Game route's creature is the recording's, position,
  state, block and timer words, callback. `--until a04b_04`: the route's own
  lift ride past the live creature, 1,437 of its 2,083 rows (to the same load).

| Beat | Canonical rows | Bed branch rows | What it covers |
|---|---|---|---|
| a22_00 | 626 | 626 | the north arm, door [7] (a room move, entry 1) |
| a22_01 | 663 | 663 | the corridor with its bugs, the south arm |
| a22_02 | 683 | 683 | door [8], the AREA01 load (entry 6), control |
| a01u_00 | 235 | 235 | the running jump over the gap |
| a01u_01 | 1,446 | 1,446 | the catwalk event (placement [45], script 0x82B590) |
| a01u_02 | 1,064 | 1,064 | door [19], the AREA06 load (entry 0), control |
| a06_00 | 357 | 357 | the east floor, over the beam [11] |
| a06_01 | 719 | 679 | the crate, a bug's spit hits (infection +10), door [2] |
| a06_02 | 1,699 | 1,617 | the keypad [6]: D_00810845 bit 5 |
| a06_03 | 395 | 314 | door [2] back out |
| a06_04 (side) | 526 | - | the beam's collapse |
| a06_05 (side) | 1,011 | - | the pit, the turn at infection 100, door [3] locked |
| a06b_00 | 780 | 780 | the running jump over the beam's trigger |
| a06b_01 | 822 | 740 | door [1], the AREA01 load (entry 7), control |
| a01v_00 | 555 | 555 | the catwalk east |
| a01v_01 | 177 | 177 | the jump north over the gap |
| a01v_02 | 771 | 690 | door [18], the AREA22 load (entry 5), control |
| a22b_00 | 1,099 | 1,018 | the corridor back, door [7] from the south |
| a22b_01 | 886 | 886 | door [6], the AREA04 load (entry 1), control |
| a04b_00 | - | 1,242 | door [45] (entry 9) |
| a04b_01 | - | 8,690 | the NPC's script 0x827D90, item 0x23, counter 0x11 |
| a04b_02 | - | 714 | door [45] out (entry 8) |
| a04b_03 | - | 2,414 | reader [50], lock bit 7, event [3], the creature |

Before them the bed path plays a01r_03, a02_s0 (the bed, 1,008) and its own
recordings a02_00 (1,448), a02_01 (630), a02_02 (1,016), a02_03 (831), a02_04
(1,114), a02_05 (1,726), a04_00 (3,615), a04_01 (1,818), a04_02 (942), a04_03
(1,015), a04_04 (553), a04_05 (829).

**The bed branch.** AREA04's second visit is recorded only on the fork
continuation `levels-recordings-20261018-10` (EIGHTH_LEVEL_ROUTE.md "Fork
continuation work"), which leaves the canonical line at the bed a02_s0 (health
100, infection 0) and re-records a02_00 .. a22b_01 from there. A run `--until
a04b_*` plays that path: C `side_played` adds a02_s0, Python selects BED_ROOT's
recordings for BED_PHASES, drive tables from `build/drive_replay_bed` (34 beats,
0 replay differences), anchors from
`build/rand_replay_bed/route_bed_20261030/route.txt`. Any other `--until` keeps
the canonical recordings. Two recording facts of that branch are modelled in the
harness, never in the game:

- a02_00's row 0 is a02_s0's last row (gap 0; `a01_route_begin` aligns on that
  tick).
- The a04_04 -> a04_05 join: a04_04's last row sets a stick, then neutral. The
  canonical a04_05 stops on row 1 (m1F1 2); this one keeps row 0's stick through
  row 1 (m1F1 1, yaw unchanged) and stops on row 2.

**The one gameplay fault in the seven-plus-twelve beats:** the spat hazard
001E2E80 hit the player and did nothing. A bug's spit reaches the head at
a06_01 row 217 on both sides (hull test equal, rand() equal to the state word
through row 217, the sampled bone positions equal). Original: 001E3378 stores +0x22C =
10.0 (fork memwatch on 0x8104DC), the next stage's 0021C2A8 moves it to the
infection and action 0x3E follows. Port: the store landed in the record's byte
copy, which the stage's vitals_load overwrites from g.pd_pend_inf. The aim/fire
world's views now map +0x220..+0x22F to their storages (the record's word is
not the storage between two stages, em_area11_bindings player_vitals) first.

**00182D70's +1C object** (a06_05 row 775, the takeover at door [3] after the
turn): 0021C190 stores the infection node (0x80000048) in the player's +0x1C;
00182D70 sets its +4 to 2 and 00183090 to 1. It was a fail-stop;
em_scene_bindings_pool_set_u4 writes the pool record's byte.

**The lift's camera step** (a04b_04 row 558, a04b_s7 row 885): 001BC560 turns
from 0x70003B50, which 0015BCF0's tail publishes every stage (the player's
+0xC0, here yaw -pi/2); the runtime's view of it was the scene's spad3B40[4..7],
written only by 001B07C0, so the eye came out at the wrong angle and the solve
slid it off the lift's wall (cam +7 = 3, the original's 0). em_player_0015BCF0
publishes it now. test_boss_fight compares no camera, so it could not show this.

**Lane F1's overlap:** its `player_states_pending_infection_collect` copied the
record's +0x22C to g.pd_pend_inf after each hazard tick. With the views the store
is already at its storage and the record's word is stale, so that copy would zero
it, and any pending infection another node left in the walk. Removed. F1's tick
log key `a04b` (the bugs) stays; the route's owner observation is `a04r`.

**Harness:**

- A22 / A01U / A06 / A01V / A22B / A04B owner tables; tick log observations
  `a22`, `a01u`, `a06`, `a01v`, `a22b`, `a04b` with D_00810768..6B, D_00810857,
  D_00810C87 / C88, D_00810CCA, D_0081076C..6F.
- `check_block_end` (tick log `blk`): D_00810700..D_00810D3F equals each beat's
  end RAM, only the frame count D_00810750..53 excluded (the first level is
  navigated). This covers health and the infection word, the start cards'
  differences, and D_0081084C (the a04b rows' `l84c` is empty, the recorder's
  span ends before it).
- `check_creature_end`: a04b_03's creature against the end RAM. A04S_OWNERS adds
  the creature and the four bugs (the pistol branch's nodes) to the rows.
- ROW_LIMIT: a04b_04 and a04b_s7 ended before AREA13's load until lane H1.
- The return phases a22_02, a01u_02, a06b_01, a01v_02, a22b_01; the C driver
  reads a return's tail from the version 2 pad header.
- rand_order: AREA06's owners, 0021AE90, 001B8AB0, 001BF6B0, 0012A5D0, 0x8239A0,
  00133640, the creature family, 0012D940, 001B41F0's hit (renamed statics),
  001F4840, 001F7E40, 001F88C0.

The rows do not sample the keypad's page object (`kpad`) or the owners'
rotations (`rot`), and pixels and sound are not compared. a04b_02's and
a04b_03's Cross presses are door [45] and the reader: no pickup is taken on the
route (the shotguns behind door [45] are lane F1's; its a04b_g beats are another
branch and not phases here). AREA13's host is lane H1's, above.

**2026-10-30, lane D1 (branch `area04-boss`): AREA04's main line row-exact
from New Game, through door [37] into AREA22.** The whole-run checker passes
56 live phases through a04_05 (`--run --until a04_05`):

| Beat | Rows | What it covers |
|---|---|---|
| a04_00 | 3,615 / 3,615 | the walk to door [45], the director's script 0x8278D0 with its 32-page message, D_00810845 bit 3 |
| a04_01 | 1,818 / 1,818 | door [40] into the store room |
| a04_02 | 942 / 942 | console [69]: the reel rolls down the conveyor |
| a04_03 | 1,015 / 1,015 | door [40] back to the hall |
| a04_04 | 553 / 553 | up the conveyor to the east balcony |
| a04_05 | 723 / 723 | door [37], the AREA22 load, the arrival, control |

Tables: drive `build/drive_replay/route_a04_20261030` (a04_05's kept as
`.reads.diverged`, named in tools/drive_replay.py DIVERGED_ACCEPTED: the
fork's re-drive differs from the trace in the walk to the door, rows
1..163 under 0.2 units, and equals it from the door's program on); rand()
`build/rand_replay/route_a04_05_20261030` (its anchors plus `entry22`). Pass
`--rand-replay` when several tables have one mtime (the newest wins).

What it needed, each from original code with its evidence:

- **The tick log's `a04` observation** (em_scene_bindings.c log_area04): the
  19 A04_OWNERS, D_00810764..67, D_00810830..3F; `a04c`, the creature's
  record wherever the load placed it.
- **The camera timeline** (em_area01_timeline): every scene 0022EC30 binds
  no track for and 0022EEF0's switch has no case for (0, 1, 3, 4, 9, 11, 14,
  18, 36: 4 is the director's event), scene 5 (D_0026ACA0, the NPC's script)
  and scene 6 (D_0026AB00, D_0026AC80, event [3]'s script).
  test_area01_timeline_reference runs the original over the a04_00, a04b_01
  and a04b_03 tracks, every event timestamp; timeline.emsp gains two windows.
- **001BAD40's command 3** (em_area01_runtime_revisit.inc): the camera
  block's words and 0022EC30 through the runtime (0x8278D0's script actor).
- **001CA6E0 on a typed shared owner** ([47]'s state 2): the boxes' 001CA5E0
  with kind 0 (decomp func_001CA6E0.c).
- **The GS recorder's barrier** before a span-ending FRAME_1 write
  (GS_EXACT.md section 9, cherry-picked to main by lane G1).
- **Boot data** (export_area00_runtime_tables MID_WINDOWS): the panel [66]'s
  descriptor D_00275928 (the Use scan reads it wherever [66] is listed, so
  a04_03's press faulted without it).
- **Library model 0x5D** (0x8278D0's script actor; +0x44 in the a04_00 end
  RAM; export_area04_library grows the 0x57 span).
- **The driver**: a command set on a phase's last rows is still on its way
  at the next phase's row 0 (a04_04 stops the stick on its last row); a
  non-neutral one carries over (`a01_route_frame`).

**AREA04's second visit** runs from start cards (EIGHTH_LEVEL_ROUTE.md
"Native status"). What it needed, on top:

- em_level8_port's entries through `m08_hooks` (dispatch_level8, area 4; the
  frames read from each first instruction; EIGHTH_NEST keeps nested hooks
  below the deepest in-module chain).
- Pool owners 0x823920 / 0x8239A0 / 0x823A90 and 0012E3A0.
- 001F9660, 0012D940 in area 4; 001B15D0, 0019A310 (byte-matched C) in the
  runtime, 00128350 / 001000E0 in the live worker; 001EFF10 (em_area00_fx)
  in the light service; handler 001ED100 in em_effects_live.
- 0019BC40's column table and 001B2E50 over it (byte windows for the count,
  the flags, the heights and the slopes).
- 0019AD00's hull lock for a non-class-0 mover: the player's call-local
  record, as 0019AA80's; the shove 0x700031C0 as a quadword read.
- The lift: its scripts as the boot image lift.emsc (0x24E1A0..0x24E960;
  the script binding refuses a tree without it), op09 001BC560 / 001BC6D0,
  0018D7B0 (em_camera_live_solve), 001AEBA0 (the screen fade), the script
  workers (001B8AB0's camera shake and the others) in area 4.
- Assets: library models 0x53..0x56, bank 0x3A beside the door bank, the
  kind 0x34 proxy assets/npc34_shadow.emdl (19 nodes, the script actor of
  0x828450; the a04b_03 end RAM's D_0028A490[0x34] holds the file, no end
  capture holds its REF), the creature family's tables, D_00275610.

The creature family calls 31 hooks with no service in this composition; the
a04b census reaches eight of them (all served now, 001B2E50 too, off the
recorded path). The others (0012F980, 00131210 and the like) fail-stop at
their address if a fight reaches them.

**2026-10-30, lane B2 (branch `a02-items`): HEALING from the menu, the
medicines' count-up and the car's sound.** Three fork recordings of the
menu use (tools/capture_heal_beats.py, run with the decomp's venv; beats
in `../Extermination/build/fork_refs/s87/route_*/`, drive tables in
`build/drive_replay/route_heal_20261030`) are side phases, each row-exact
from New Game:

| Beat (source) | Rows | What it covers |
|---|---|---|
| a02_s1 (a01r_03) | 963 / 963 | at the bed: START, ITEM, HEALING, Cross on 0x20 (00185420 finds the bed), Yes: the bed, health 90 -> 100, infection 40 -> 0 through 0020CDC0 phase 2 with the HEALING hover |
| a01r_s2 (a01r_02) | 461 / 461 | AREA01: the same use finds no device: the banner 0x19, back out, START |
| a01_s8 (a01_s5) | 1,028 / 1,028 | out of the control room, the fire's burn (health 95), START, ITEM, HEALING, 0x1E, Yes: D_00810858 counts 95 -> 100 while +0x220 (the tick log's health) stays 95 until the commit |

- **00185420 / 00184D20** for every item id (em_item_device_find;
  ITEM_DEVICE_LOOKUP.md, 7,594 oracle cases).
- **HEALING state 5:** em_status_pages_live keeps the counted D_00810858
  apart from the scene word (+0x220) while the page stays in state 5;
  the commit's 0015C700 hands the target over (STATUS_PAGES.md).
- **The hub with UI+0x11 = 5** (0020CDC0 phase 2 after HEALING's Yes):
  twelve hub layouts (STATUS_HUB.md; re-export status_hub.emhs).
- **00184BA0 after a crawling take** (the duct's 0x1E): the port's owner
  token, never released (no 00182DF0 for a crawling player), ended the next
  claim with a fault; it now ends at that claim, which also no longer tests
  3B8F (the original's is 1 there too).
- **The car's 0x8B4** (and AREA00's 0x89E / 0x8A0): the B0 07 volume slide
  (SFX_SEQUENCER.md pass V: lockstep with the original). In the a02_04 run
  the engine's two voices in the tick log equal the original's voice
  records (a fork re-drive of a02_02 sampling D_0027CCC0) on all 1,016 rows.
  Re-export sfx_registry.emsr / .json.
- a01_s8 runs with `--rand-replay` (the recording's rand() state at its
  row 0): the native stream leaves a01_s5 at another state, and the burn's
  reaction draws rand() & 1 for its clip.

**2026-10-30, lane B1 (branch `a02-exit`): a02_05 row-exact, door [25]
into AREA04 to control.** From New Game the whole-run checker passes 50
live phases through a02_05 (`--run --until a02_05`; `a02_05: PASS 1726
native rows`, the loader's 19 states, 83 original chain steps, 84 veil
steps, the 25 recorded owners (AREA04's records at those nodes after its
state 0), ending camera bytes equal to saved RAM). The phase's fork drive
table is `build/drive_replay/route_a02_05_20261030` (0 replay
differences) and its rand() anchor `entry04` is in
`build/rand_replay/route_a02_05_20261030/table.txt` (record_rand_replay.py
now records the a02_05 beat; the four earlier anchors re-recorded equal).
The C phase list has the AREA04 boundary: AREA04 entry 0's state 0
(recorded row 649), then the 1076 recorded rows of the arrival script
0x8275A0 and control, to the trace end.

AREA04 runs in the shared AREA01 composition (area 4), as AREA02 does
(docs/AREA02_PRETRANS.md section 2); what it needed, each from original
code with its evidence:

- **Area state** (`em_area04_state`): the overlay header (MWo3 id 5,
  text 0x30C0, data 0x6300, BSS 0x14AC80 at 0x82C900) and 001E7780's area
  init 0x823B10 (decomp func_overlay_AREA04_00823AD0.c: five stores);
  `tools/test_area04_state_reference.py` runs the original 001E7780 /
  0x823B10 over the AREA04 fork captures (24 runs, the globals, data and
  BSS equal).
- **Runtime** (`em_area01_runtime`, area 4): em_area04_port's 21 owners
  (AREA04_PORT.md, oracle-checked) through `em_area04_runtime_overlay.inc`,
  one typed hook per callee, generated from the register map of
  `test_area04_port_reference.py`'s HOOKS; each entry's frame from its
  first instruction. em_area02_math's boot routines and 001F4E40 serve
  area 4 too; 001BAC00 (the arrival script's op14) and the 001BB0E0 script
  actors run over AREA04's data window 0x826600..0x82C900; 001B6F80 and
  001FB0B0 (both byte-matched) are translated in the runtime.
- **Scene**: AREA04 sub 0's collision, cells, roster (deferred groups
  0x826600 / 0x8268D0 / 0x826930 through 001B6660), world models, message
  bank, dynamic list (slot 0x45) and pool-owner allowlist; the area script
  window 0x8272A0..0x82C8D0 (op09 0x823B40).
- **001F6210's list loop** (AREA04 is the first area with a 001F5CA0 list:
  its area model sprites; byte-matched, EFFECT_MANAGER.md, oracle over every
  list): 001D8C20, 001C6120, 001D3D90 (a raw-model channel-3 submit,
  `em_owner_draw_live_001D3D90_at`) and 001CAAC0 over the context's +0x1C
  cursor, the one storage; the units carry the clip pass
  (`em_object_unit_parse_light_clip`); the frame's page-unit cache holds 64.
- **The drop shadow of AREA04's NPC [2]** (0x824320, +0x96 = 0x2A, 20
  nodes): em_shadow_live takes the actor's node count (21, or 20 for kind
  0x2A) and the proxy D_0028A490[0x2A] (`assets/npc2a_shadow.emdl`, the
  decomp's `export_shadow_proxy.py --kind 0x2A`, verified against the
  a02_05 end RAM: the captured silhouette pass REFs it).
- **The camera timeline** accepts scene 3 (AREA04's arrival): 0022EC30
  binds no track for it and 0022EEF0's scene switch has no case for it;
  `test_area01_timeline_reference.py` now also runs the original 0022EC30 /
  0022EEF0 over the arrival's track (78 exact ticks).
- 001B1D20 (the class-4 list push) through the shared services.

**Why the scripted EM_START_AT runs did not take door [25]** (lane A2):
door [25] (001BB860, model 0x17, byte-matched) publishes itself to the Use
scan (001B1DE0) only within 20 units of the player (3D), and only with its
lock bit D_00810843 bit 3 set; 00183EF0's class-0 kind-5 branch (subtype
0x17, not 3 / 0x15) then takes a player within 10 units (planar) of the
door's centre (440.2, 109.9) and within 8 in height, and, from the south
side, only while the player's yaw is within pi/4 of the door's (0, +z).
In the run with the recorded pad alone, the loaded game's placement clip
(the player's clock counts down from 78 for about 65 frames after control)
swallowed the first 40 frames of stick, so the player ended at (421.3,
93.97), 24 units away, unpublished. With EM_START_AT_PATH the steering
aims the stick at the recorded position 8 frames ahead; the recording's
turn in place before the Use (rows 105..170, yaw -1.96 to -0.06 while the
position moves 1.4 units) became a walk, and the player pressed Cross at
(439.24, 102.81), 7.2 units from the centre but facing yaw -2.455, outside
the cone. With the recorded pad delayed by 66 frames (the clip) the Use is
taken (3B8D = 2 at (440.2, 106.7)) and the area change, the AREA04 arrival
and control at (440.1, 14.9, 356.4) follow.

**2026-10-30, lane A2 (branch `area02`): AREA02 row-exact from door [16]
through the battery panel, and the bed.** From New Game the whole-run
checker passes 49 live phases through a02_04 (`--run --until a02_04`,
627 s), and 45 through the side beat a02_s0:

| Beat | Rows | What it covers |
|---|---|---|
| a02_s0 | 1,008 / 1,008 | the bed 00159620 with item 0x20: prompt, Yes, health 90 -> 100, infection 40 -> 0 |
| a02_00 | 1,423 / 1,423 | the duct (unchanged) |
| a02_01 | 630 / 630 | switch [31]: the lamps 001F4E40, the car movie E010 (selector 5), music 0x13 |
| a02_02 | 1,016 / 1,016 | the car's run, gate [38] freed at x 140.9, the car stopped and freed, the ladder escape |
| a02_03 | 831 / 831 | over the wreck, the two grabs (infection 40 -> 60), the ledge climb |
| a02_04 | 1,577 / 1,577 | battery panel [24]: the page's prompt, Yes, charge 8 -> 0, D_00810843 = 8 |

The fail-stops on the way, each closed with a translation of original code
(byte-matched or NEARMISS decomp C, logic recovered) and its evidence:

- **The bed's scripts** (00157B30's D_00247E20 / D_00247EE0 / D_00247FA0 /
  D_00247FE0, patched at D_00247E74 / D_00247F34): the panels boot-script
  window now runs to D_00248120 (export_area01_boot_scripts.py; lockstep
  against the original in test_area01_script_host_reference.py, 8 cases);
  their op09 callbacks 00157F30 / 001575E0 are allowed.
- **Player bank rows 0x96..0x98:** a row no area file relocated is 0 (LOAD
  GAME straight into AREA02 leaves 0x97 / 0x98 clear) and is not mapped.
- **HEALING kind 2 (the bed's Yes):** the device's +0x0A / +0x0B are a view
  of the pool record (em_scene_bindings_device_bytes); page id 8 goes to
  phase 2 (covered by test_status_page_reference's oracle); the count-up,
  0020CDC0 phases 1 / 2 sub-state 2 (NEARMISS, logic recovered), counts its
  own D_00810858 / D_0081085C while the player record's +0x220 / +0x228
  (the port's scene words) keep their values until the commit, as the
  recording shows (+0x220 stays 90 through the count-up).
- **Switch [31]:** em_area02_misc's 001F4E40 (lamps), 001AA700 (the car's
  sweep) over the area runtime's bytes; movie selector 5 \MOVIE\E010.PSS
  (feed end from the recording's vblank count, see em_frontend.c); music
  cue 0x13 exported (export_streams.py, with the AREA02 / AREA04 message
  voices and AREA04's music rows).
- **The car:** 001F02C0 (sounds, em_area00_fx's), 001F6B30 (the room's
  point lights again, em_effect_kinds'), the effect handlers 001EC9A0 and
  001EC5F0 (em_area02_misc's) with their sources, the spawned burst emitter
  001E4CE0 and 001E3D90 on the area runtime, the burst's tables
  (D_00253C50, D_002541F0..D_00254F2F; its mesh sets written in place),
  its contact callback 001E49F0, 00128250, 0x70003A24..A37, D_00282154..56.
- **The battery panel [24]:** 001580C0 (op09 of its script) translated in
  em_area02_math (20 oracle cases, full sweep PASS); the BATTERY page's
  owner reads / writes of an AREA01 / AREA02 battery panel (00158EC0).

- **Free roam** (EM_START_AT from the AREA02 cards, scripted pads): the
  wreck's fire 001EC820 (em_area02_misc, byte-matched C, oracle cases) and
  the console [4]'s BATTERY page (00185420 on the live composition's list,
  00159210 as a battery-panel owner). Ran without a fault: the bed, the
  console, door [6] into the south room, its pickups, walks to door [5] and
  the save terminal (neither Use was taken by the scripted approach), a
  kill from the a02_02 card (two dissolves, 504 glow ticks).

The a02_03 "4.2 units off the path" stop was an EM_START_AT discovery
artefact (a loaded game's state, the path clock): from New Game a02_03 is
row-exact. a02_05 (door [25] into AREA04) became a harness phase in lane
B1 (above); see LEVEL2_CRASHES.md "AREA02 (lane A2)".

**2026-10-29, lane G1 (branch `bug-glow`): a kill's dissolve no longer
fail-stops.** A user run (EM_START_AT=a01_07, through the shaft door into
AREA00, a kill at the cage-room nest) stopped with "FAULT at 001F6FB0" at
the dead creature's record a few seconds after the kill: the glow node's
last tick (state 3, which only frees the node) found its parent already
freed, and the composition mapped the parent for every tick. The parent's
views are now made only in states 0 and 1, the only states that read it
(AREA00_FX.md "The live dissolve"). `make test-glow-dissolve` kills two
creatures from the a00_04 card and compares the 0012A5D0 one's 251 ticks
with the original's capture (equal; both records freed on the same tick).
Behind it, a shot 0012A5D0 at the a02_04 start point reached 001C2770's
unbound probe 001C2690, now translated (AREA01_EXITB.md).

**2026-10-29, stop for review (after iteration 31): a02_s0 and
a02_01..a02_04 are revisit-harness phases now. The C rows, the sources
and a02_02's recorded late-input correction are in place, the drive
tables are recorded on the fork (`build/drive_replay/route_a02_rest_20261029`,
0 replay differences), and the contract passes. Two runs reached two
stops; neither is fixed:**

- **a02_01:** rows 0..484 are exact; the run stops at the switch [31]'s
  Use (row 485).
  - The failure: owner 00128C10 node 0x7A5640, at 00184BA0; the
    interaction view could not read 0x00275480.
  - The likely cause: export_effect_tables (R1's f4e2b28) serves
    D_00275478 and D_00275488 as two windows. 00183EF0 reads three floats
    at D_00275480, so that read crosses the boundary between them.
  - The candidate fix is one window D_00275458..D_00275490. It is saved,
    not run, in `../Extermination/build/agentkit/port/F2_wip.diff`.
- **a02_s0** (the bed, from a01r_03): rows 0..378 are exact.
  - The failure: owner 00159620 node 0x7A5F10, at 00247F34 (a runtime
    read).
  - This is the bed interaction R1 is on.
- a02_00 stays exact (1,259 / 1,259 aligned rows in the same run).

**2026-10-29, iteration 31 (revisit 51eec5b merged: R1's AREA02 host):
from New Game the revisit harness plays through a02_00, and the whole-run
checker (test_level_smoke.py) passes for the first time: "level smoke:
PASS (45 live phases ...)". That covers the first level, AREA01, AREA00,
the revisit, a01r_03 (941 / 941 rows, with the loader, chain and veil
replays and the AREA02 sub-1 arrival) and a02_00 (1,423 / 1,423 rows:
the duct, the sub change to sub 0, the arrival at entry 5 and control).
Every recorded side beat of the first three levels also passes row for
row.**

- **Side beats, each a full checker PASS from New Game:**

  | Beat | Rows | What changed |
  |---|---|---|
  | a01_s0 | 1,435 | nothing (run as the source of s2 and s5) |
  | a01_s1 | 397 | page loads replayed (below) |
  | a01_s2 | 680 | page loads replayed |
  | a01_s3 | 262 | nothing |
  | a01_s4 | 1,225 | page loads replayed |
  | a01_s5 | 3,118 | page loads replayed |
  | a01_s6 | 229 | nothing |
  | a01_s7 | 982 | nothing |
  | a00_s0 | 534 | nothing: the AREA01 return with its loader, chain and veil replays |
  | a01r_s0 | 374 | new harness phase |
  | a01r_s1 | 760 | new harness phase |

  - **Page loads:** the AREA01 harness now replays the side beats' page
    loads from the fork recordings (`drive_replay.loader_reads`, as the
    AREA00 and revisit harnesses do). Before, a01_s1, s2 and s5 stopped at
    the pickup's page request (LEVEL2_BINDING.md 2026-10-19).
  - **a01r_s0 and a01r_s1:** the event's pickup, from a01r_01, and door
    [14] locked, from a01r_02. Both are revisit-harness side phases. a01r_s1's
    first input is at f10; until then it holds the session's cleared pad,
    as route_capture does.
  - **Drive tables:** recorded on the fork into
    `build/drive_replay/route_a01r_sides_20261029` (9 and 21 reads, 0
    replay differences).
- **a02_00 in the revisit harness:**
  - **Phase:** a C phase row with its AREA02 sub-0 boundary (143 rows after
    state 0), `RETURN_AREA` `02000502`, and route_a02's recording.
  - **Owners:** the 25 recorded A02_OWNERS (FOURTH_LEVEL_ROUTE.md section
    1.1) are logged as the tick log's `a02` observation.
  - **Checker contract:** its mock boundary now takes each phase's
    destination.
  - **Drive table:** recorded on the fork
    (`build/drive_replay/route_a02_00_20261029`, 5 reads).
  - **The duct entry at rows 225..228** (R1's open question) is exact
    natively: the Use scan from 029fc00 suffices.
- **The commands across a02_00's load.** The recorder's crawl policy keeps
  toggling the stick through the sub change (f1029..f1284). The C driver
  keyed every command to native ticks, so after the shorter host-speed load
  the stick stayed up 142 ticks too long, and the player turned in place at
  row 1355 (clip 1, yaw falling by pi/8 per tick).
  - Pad scripts now have a version 2 header that carries the destination's
    state-0 row.
  - From the native boundary on, tick boundary + q takes the command of
    recorded row arrival + q. This is the same pairing the checker
    compares.
  - The harness contract rejects three malformed v2 headers.
- **00179910's point** (NEARMISS C, logic recovered): the area-2 exit
  boxes read D_0024D650[area][sub] + 0xF0 / + 0x120. These are spawn
  records 5 and 6, read through the scene's read-only spawn table window
  (`em_player_closure_live_set_spawn_read`). Before, the closure
  fail-stopped there when a02_00 reached the duct (row 469).
- **The AREA02 library models** (pickup 0x57 at sub 0's node 0x7A8830; the
  fault 001B1020 at row 1281) are R1's tools/export_area02_library.py
  (22e1722). F2's identical extension was dropped in favour of it.
- **The whole-run checker:**
  - `rand_order` now names the rand() callers of AREA00, the revisit and
    AREA02, each mapped to its original. Each original's decomp C calls
    00122BB8 itself: 0022DCD0, 001C50B0, 001DF180, 001E2E80, 0x8282F0,
    0x823820, 0012AFC0, 00128600, 0012B410, 001B8AB0, 0012D850, 0x8240E0,
    0x826010 and 00128C10.
  - It also names their call adapters as wrappers.
  - The AREA01 RNG continuity audit restarts the chain at the run's logged
    comparison-only rand() replay anchors.
  - The new AREA02 anchors are entry02 = 55925c31 and entry02.2 =
    411f8b31, in `build/rand_replay/route_a02_00_20261029`. Natively they
    are already equal, so they change nothing.
- Checks:
  - the three harness contracts;
  - `test-player-closure-10-12-19-reference`;
  - New Game control 9.599849.
- Receipts:
  - `build/frontier/f03`: the a01r_03 full pass.
  - `build/frontier/g06`: the a02_00 full pass.
  - `build/frontier/side_*`: logs and receipts only.

**2026-10-29, iteration 30 (revisit 6d212fb merged: R1's AREA02 area
read): a01r_03 passes the AREA02 load. Under the checker's return
alignment, all 639 paired rows are exact: rows 0..565, the 19 loader
states in the original's order (host-speed holds shortened, as for
a00_10), the AREA02 sub 1 state 0 at row 735 and rows 736..737. The run
then fail-stops in the first sub-1 owner draw: "em_area01: owner 001C4AF0
node 007A6200 failed at 001CA7B0" (unbound worker). This is the AREA02
host's raw-model draw (001C4AF0 -> 001CAF60 / 001CAE40 -> 001CA7B0),
which belongs to R1's lane.**

- **The area read** is R1's c9b7037 (revisit), merged unchanged. It covers:
  - `area_read` for area 2, sub 0 and sub 1 (`assets/area02/sub<N>`);
  - `em_world_textures_live` AREA02 (tools/export_area02_world_textures.py:
    sub 0 has 417 textures, sub 1 has 311, both equal to the GS freezes);
  - `em_shadow_live` AREA02 receivers
    (tools/export_area02_shadow_receivers.py, compared with the fork
    captures);
  - the area's roster, cells, world models and SFX scopes, and the
    001B6660 deferred spawn.

  The drawbridge worktree's private `assets/area02` and `assets/sfx` are
  now real directories (no longer symlinks into main), exported in R1's
  order.
- **The a01r observations through the load.** The tick log's a01r owners
  were logged only while D_00810700 = 1, so every row from 565 (D_00810700
  = 2) read "missing AREA01 revisit observations". The recording keeps
  reading the same addresses: the AREA01 records until AREA02's state 0
  rebuilds the pool, then zeros. `log_area01r` now also logs in area 2, as
  `log_area00` does for the AREA01 returns.
- **The loader states.** (3,5,1,0) runs the area streamer's 19 states in
  the recording's order; the native holds are host speed (1..8 ticks
  against 1..34). The veil's (3,5,2,0) takes 46 ticks against 71. a00_10's
  return is 25 short in the same way. The full checker replays the
  original veil and loader chain over these ticks (`return_pairs`), and
  that check still waits for a finished run.
- **Needed from the AREA02 host (R1):** 001CA7B0 bound in the area-2
  runtime composition for 001CAE40's call (em_area02_math f_1CAE40). The
  translation exists as em_owner_draw_live_001CA7B0. The rest of that draw
  is also needed: 001D8C20, 001C7420, 001D3CE0 / 001D3AC0 and 001CAAC0.
- Checks: the three harness contracts (AREA00, AREA01, revisit),
  world-textures, area-load and start-at references, New Game control
  9.599849. Rows: `build/frontier/tools/return_rows.py <out> a01r_03`.
  Receipt: `build/frontier/f03` (the iteration-31 run; f02 removed).

**2026-10-29, iteration 29 (port main 897188c merged): every camera-end
observation of AREA00 and of a01r_00..a01r_02 equals the beats' saved
RAM, and the revisit harness drives a01r_03 (door [16]): its rows 0..564
are exact, up to the AREA02 area read, where the port stops: "001FF080(1,
0): area 02 room 1 is not exported" (the port's area_read has no AREA02
scene, world-texture catalog or shadow receivers yet; R1's AREA02 host
lane).**

- **The camera's w lanes.** The camblk check compared the eye / target w
  words (camera +0x1C / +0x2C), which g.cam does not carry:
  - 0018CBD0 (the retarget, byte-matched) stores 001026A0's whole
    product at cam+0x10 (w 1.0) and copies the player's whole +0xA0
    quadword to cam+0x20; `camera_script_seed_0018CBD0` now stores both w
    lanes (a fork memwatch: a01r_01 row 8063's retarget writes +0x1C from
    pc 001026C8, ra in 0018CBD0).
  - 00182F90 (the script placement, byte-matched) runs its vsub / vadd on
    all four lanes: the delta's w (target.w - +AC) reaches +AC, +BC and
    0x70003B4C; `w_00182F90` now applies it, so the retarget after a
    placement copies w 0 (the original's cam+0x2C at a01r_01's end).
- **a01r_03 in the revisit harness:** the phase row (C) and its AREA02
  boundary (state 0 of area 02 room 1, then the recording's 205 rows), the
  return alignment generalised for the AREA02 destination
  (`level_smoke_area00.RETURN_AREA`), the mock driver and the checker
  contract (capture-shaped loader rows), and the drive replay table
  recorded on the fork (`build/drive_replay/route_a01r_03_20261028`, 14
  reads, 0 replay differences).
- The checker fixtures give camblk all four blocks.
- Checks: the three harness contracts (AREA00, AREA01, revisit),
  area-script, script-host-workers, camera-live, first-control and
  area-load references. Receipts: `build/frontier/f03` (iteration 31's run, later code; r13 and f02 removed).

**2026-10-28, iteration 28: the player record's +B0 is the original's
(the hip after the stage, a placement's position after 001B07C0); with it
the `lock_r54` class is gone and AREA00's arrival and a00_00..a00_04 are
exact on every row with nothing ignored, and the AREA00 camera-end checks
(all four camblk blocks against each beat's saved RAM) pass through
a00_04.**

- **0015BCF0's tail** (byte-matched src/func_0015BCF0.c) ends with +A0 =
  +B0 and then +B0 = the quadword at +0xC0 of the node in bone slot 1 (the
  hip's world translation after the animate step). The port kept the feet
  in +B0. Readers after the stage take the hip: 0018A1F0's knife reach
  `0019B2C0(D_00810360, ..)` (its segment from the hip to the knife tip
  crossed the padlock's face at z -1687.316 instead of -1687.104, so the
  puff 001EA240 that reuses the padlock's slot [54] sat 0.21 off: the
  `lock_r54` rows; a fork stop at 001EF9D0 in a00_03 row 105 with a watch
  table of the registers) and the camera's pre-pass 0018D330.
  `stage_position_publish` now stores the hip (em_player.c). The
  first-control reference compares +A0 and +B0 with the original (56
  callbacks; it compared the port's +B0 with the original's +A0 before).
- **001B07C0's position quads** (+A0 and +B0 = the spawn position) are now
  stored in the record (`spawn_commit`), and the camera's player view takes
  the record's +B0 when a pose is evaluated (em_camera_live
  `player_refresh`; before, the pose host's last hip): 0x1AE040 state 4's
  0018D7B0 at a00_04 row 349 (the cage door's room move) probed the
  ceiling from the stale hip and stored -10.000004 at the camera's +0x60
  (the original -10.0); a fork word watch of a00_04 showed every earlier
  row equal.
- **The camera-end checks compare all four camblk blocks** (the camera
  block, the forward, D_00810690.. and the eye / target / up quads
  D_008105D0..; the checks wanted exactly three, so every AREA00 phase end
  read as a missing observation).
- Checks: first-control, area-load and room-move references; AREA00
  harness through a00_04 (`build/frontier/f03`, a later run of the whole path; i27 removed).

**2026-10-28, iteration 27: from New Game the revisit run plays the whole
route through a01r_02 ("level smoke: PASS 43 live phases through
a01r_02"), and every revisit row is the original's: a01r_00 1,382 / 1,382,
a01r_01 8,122 / 8,122 (the event, the item page, the op0D cinematic with
the ramp frames), a01r_02 1,024 / 1,024 (over the lowered bridge, script
0x82B0D0). AREA00 and the first level are as in iterations 23..25.**

- **001798D0 on the player** (0x82B0D0's record handler at a01r_02 row
  983, through the area script's record call): the AREA01 composition had
  no worker for it. It is now em_player_use_001798D0 over the live record
  (`em_player_closure_live_001798D0`: the Use dispatcher's workers, its
  00174A50 the stage's row request), with the views published around it.
- **D_0028A580, the player's bank word,** for the script's op0A sub 2 (the
  same row: +1F2 = 0, +20C = -1, +2F3 = 3, +0x40 = D_0028A580): the AREA01
  script's resource reader served the module loader's table, which holds
  only the slots its loads relocate (word 0x3C was 0), so the next stage's
  00183090 bone_init read an unmapped bank. The reader now serves that
  word from the export the player stage's 00182DF0 reads
  (em_area11_roger_table_word; 0xD689C0, as the fork snapshots hold).
- **The first-level checker's whole-run assumptions** (test_level_smoke.py,
  which the revisit harness runs after the native run): the ladder count
  now counts the AREA11 ticks only, and the departure movie check the
  first level's game-task movies. It then stops at a00_arrival's
  "missing complete camera observation" (the AREA00 phases' camera-end
  check needs the live camera's block at that tick); the per-phase row
  comparison above (the harness's prefix report) is the measure.
- Receipts: `build/frontier/f03` (iteration 31's run: the same code plus iterations
  28 to 30; r07 removed).

**2026-10-28, iteration 26: the revisit run is exact through a01r_00
(1,382 rows) and all 8,122 rows of a01r_01 (the train-room event, the
item page, the op0D cinematic and its ramp frames), and a01r_02 through
row 448; it stops at a01r_02 frame 10065, owner 008261A0's node 007B3580
failing at 001798D0 (next unit).**

- **a01r_01 row 8079 (`msg` 4 against 0, D_008106B0 0 against 1) was the
  item page's module load.** The status page loads module 0x23 through the
  module loader (task slot 2, rows 8068..8090 in the recording); the
  revisit harness did not hand the a01r beats' bank loads to the drive
  replay, so the port's loader polled at host speed and the page's message
  came 14 rows early. `test_level_smoke_area01r.py` now passes the a01r
  traces to `drive_replay.write_table` (the "L" lines, as the AREA00
  harness does for a00_08): 2 more loader reads.
- **The node fault at a01r_01 frame 9597** (001BB0E0's node 0x7A5930
  failing on 0x7A64F0): the group 0x82A900's owner (record 5, 0x825950)
  frees itself in row 8103's tick, the tick its script actor (record 1)
  runs state 2 (a fork pool probe, `build/frontier/fork/a01r_01`); the
  revisit runtime's rv_001BB0E0 read the owner's bytes on every call.
  001BB0E0 reads the owner's +0x2E in states 0 / 1 only, so the runtime
  now reads it there only.
- **Build:** the binary now also depends on `src/game/*.inc` (an edit of
  `em_area01_runtime_revisit.inc` alone did not rebuild it).
- **The chain page's Python model takes several passes**
  (`chain_page_model.py` `pass_calls`, `pass_targets`, `again_pass`, the
  folded digest), the tick log's page record ends with the frame's
  slot-0xFFF targets, and `test-ramp-pass-reference` checks the model's
  walk of each ramp pass against the C walker and of all of a frame's
  passes in one page.
- Receipts: the iteration-31 run `build/frontier/f03` (the same code plus
  iteration 27's; r05d, a diagnostic build, removed).

**2026-10-28, iteration 25: the a00 -> a01r handoff plays. From New Game
the revisit harness (`tools/test_level_smoke_area01r.py --run`) runs the
whole AREA00 path, the return and a01r_00 exact on all 1,382 rows, and
a01r_01 exact for 1,631 rows, where it stops on the unbound 001C9E40 that
`revisit` binds (f814c9b; merged next).**

- **The rand() anchor is per entry.** em_level_smoke_test_area_entry keyed
  every AREA01 entry "entry01", so the revisit's entry re-seeded the first
  entry's state. Keys now count the run's entries into an area
  ("entry01", then "entry01.2"); tools/record_rand_replay.py also records
  a00_10's AREA01 entry (the fork: state 0x3944f333 before draw 23,903 of
  the beat, its rows equal to the recording; table
  `build/rand_replay/route15_a00_10_20261028`). The anchor is needed at the
  revisit because the port's a00_10 area read runs at host speed (iteration
  24): its load veil draws rand() for fewer ticks.
- **The revisit harness takes the AREA00 harness's comparison-only
  anchors:** the arrival's drive-replay anchor (`extra_anchors`) and
  EM_RAND_REPLAY (`--rand-replay`, `--no-rand-replay`); without them it
  diverged in a00_09 (the creatures) and faulted at 001E2E80.
- **After merging `revisit` (c450912: 001C9E40, 001EDAF0, the actor
  view's reused records) the same run is exact through a01r_00 (1,382
  rows) and a01r_01 rows 0..8078 of 8,122** (the op0D cinematic and the
  ramp frames included). At row 8079 the message and request bytes differ
  (`msg` 4 against 0, D_008106B0 0x00 against 0x01) and the run stops at
  frame 9597: owner 001BB0E0's node 007A5930 fails at 007A64F0 (the record
  view: a record no longer allocated or reused). That is the revisit's lane
  (R1, AREA01_REVISIT.md).
- Receipts: `build/frontier/r03` (r02, the run before the merge, removed).

**2026-10-28, iteration 24: AREA00 plays through a00_10 to its return to
AREA01 (the harness: "PASS 40 live phases through a00_10"). The arrival and
a00_00..a00_09 are exact on every row, and a00_10 on rows 0..714; rows
715..769 are the AREA01 area read (task slot 0 +8 = 3, +9 = 5), where the
port's read finishes at host speed (38 + 55 rows in step 1 / 2 against the
original's 221 + 80). Area reads are not on the drive replay (only stream
and module-loader reads are, `drive_replay.py`), as before; the return
boundary aligns the next phase.** Next: the a00 -> a01r handoff
(`tools/test_level_smoke_area01r.py --run`).

- **0x8266A0 ([60])'s model switch at a00_10 row 675** (D_0081075E = 0xFF:
  +0x0D 0x19 -> 0x1A, then 001AF800, 001CB5B0(+0x09), 001B0FD0, 001C6380)
  faulted in the record view: [60] is bound through the boxes' owner views,
  whose +0x110 words are the owner's own slots (read-only in the view), and
  the model view ran 001AF800 on a typed copy it cannot write back. A
  001AF800 on such a record now goes to the pool's release
  (`em_area11_boxes_001AF800`: em_roger_actor_001AF800 over the canonical
  slot stack) with the views published around it (em_area01_live).
- Receipts: `build/frontier/f03` (later code; i25, r13 and f02 removed). Quick tests: AREA01 runtime and revisit
  runtime references.

**2026-10-28, iteration 23: the AREA00 arrival is exact on all 4,017 rows,
a00_00..a00_09 on every row, and a00_10 through row 675; the run stops at
a00_10 frame 15254, where the owner 008266A0's node 007B9380 fails at
007B9490 right after the exit movie (next unit).**

- **The arrival's row-0 clock (the last arrival row) was the default
  pose's timing.** 001AF5C0's wipe leaves the record's pose words 0, and
  0015C420 writes the default pose (bone_init_default_2 on clip 0, the
  clock +3C = 80.0) on the first stage after it; the port wrote it at the
  area bind (player_pose_attach), one iteration early. The route captures
  show it at both arrivals: AREA00 a01_07 rows 539 / 540 and AREA01 route
  15 rows 749 / 750 hold 0.0 then 80.0. player_pose_attach still makes the
  pose (the frame's display and node channels need it) but puts the
  record's bytes back as the wipe left them; `player_pose_rebuild_default`
  writes them at the rebuild stage (em_player.c, before the entry stance).
  Checks: area-load reference (New Game's 13 task states), first-control
  reference (56 callbacks), New Game control 9.599849.
- **Movie selector 3 is AREA00's exit movie `\MOVIE\E007.PSS`** (the
  shaft door's script at a00_10 row 672), exported locally
  (`export_movie.py --disc-path /MOVIE/E007.PSS`, STARTUP.md step 71) with
  its field facts from a fork probe (decomp
  `build/fork_refs/probes_1028_e007`: 691 pairs, the last 2 after the feed
  loop's end, a 133-vblank stall, gate 686, end 1510; the replayed beat
  equal to the recording on all 1,095 rows).
- Receipts: the iteration-31 run `build/frontier/f03` (the same code plus
  iteration 24's; i24 removed).

**2026-10-28, iteration 22 (`revisit` merged, a fast-forward: R1's single
`em_coll_segment_0019AA80` and the one hull home `assets/player_hull.emom`
kept): a00_09 is exact on all 2,028 rows and a00_10 through row 672; the
run stops at a00_10 frame 15251, where the shaft door's script asks for
movie selector 3 (`E007.PSS`, not exported yet: next unit).** The arrival
and a00_00..a00_08 are as in iteration 21 (the arrival's row-0 clock
still differs).

- **The chain page draws every slot-0xFFF CALL (CHAIN_PAGE.md 6.3).** The
  a00_09 stop (iteration 21) and the AREA01 revisit's stall after the beam
  (R1: a01r_01's op0D camera wait) were the same gap: 001CB760 at slot
  0xFFF appends a block per call, and the flash node's ramp records add
  001DF180's grid (through 001DF5A0, from a00_09 row 793) and 001DF110's
  sprite (from row 794) beside 001DDE10's depth-of-field pass; the page
  followed only the last one recorded and walked the others as page
  packets. em_render_context_live now records every slot-0xFFF CALL of the
  frame (`f_001CB760`, used by 001DDB70 and 001DF110 too; at most 4, more
  faults), em_chain_page follows each in pass mode (`pass_calls`,
  `pass_count`; per pass its primitives, target and marks) and
  em_chain_page_live draws each with em_gfx_gs_page_pass in page order.
  The shape is the original's: fork capture `build/ramp_capture` in the
  decomp (`capture_ramp.py`, `analyze_ramp.py`: the beat replayed open
  loop, every row of 700..810 equal to the recording; the DMA cut at the
  ramp CALL's two boundaries as the depth-of-field capture does) and
  `make test-ramp-pass-reference`: the sprite bit-exact, the grid exact
  but for 188 / 75 field words of the model's open STQ residue
  (GS_EXACT.md 8.2), through the model and the Original profile's path.
- **The GS world keeps the head's Z buffer as drawn memory** (GS_EXACT.md
  section 9): 001DF110's sprite samples the other field with REPEAT, and
  its 256-row texture reaches the Z buffer's first rows; the world faulted
  on that read ("a texture that reads GS memory no upload wrote"). Unit
  case `z_reach` in `make test-gs-world`.
- **The revisit's stall (R1):** the same fix covers it (001DF110's list on
  channel 3 beside 001DDE10's: both are followed and drawn as passes).
- Checks: `test-chain-page` (two passes in one page, a target CALLed twice),
  `test-gs-world`, `test-dof-pass-reference`, `test-ramp-pass-reference`,
  New Game control 9.599849; not run: the first-level smokes and
  EM_TEST_FULL. Receipts: the iteration-31 run `build/frontier/f03` (the
  same code plus iterations 23 and 24; i23 and i24 removed).

**2026-10-28, iteration 21: a00_09 is exact through row 793 (the bug's
attack on the player included); the run stops at a00_09 frame 13345, where
the flash node `001E7310` (stop S4) draws its ramp and the chain page then
refuses the frame's packets** ("chain page: GIF packet fault at 0056401C",
at 001CB800; next unit). a00_00..a00_08 and the arrival are as in
iteration 20.

- **The bug's grab (a00_09 row 462)** runs through em_area00_low over the
  live composition: its hosts now hand the module the canonical grab-slot
  byte D_0081083C (`grab_bits`; 0012E0B0 / 0012E070 faulted on the NULL
  view), 0012CAA0's grab table D_00242DD0..D_00242EAF is exported
  (`CLASS2_WINDOWS`), and the SDK dispatcher serves 00102A60 (the z turn,
  em_owner_services' translation, beside 00102B08 / 00102BB0; a case in
  `test_aim_fire_sdk_memory_reference.py`).
- **D_008106B3 follows 0015BA50's tail.** The canonical byte took only the
  legacy gates; it now also takes the translated stage's result
  (`player_states_busy`: the player held by the bug, +4 = 2 with +5
  0xB..0xF and +0x1F1 = 1), which the recording sets from row 462 (the 332
  `req` rows that differed).
- **The flash node 001E7310 (stop S4)** ticks through
  em_aim_fire_runtime (em_area00_low's translation; binding row in
  em_area11_bindings): its 001D2830 and the ramp records' 001DEE80 /
  001DEEC0 / 001DF5A0 (em_area00_hud over the composition's views, the
  render context's among them), 001DF180's packet callees through new
  `em_rcl_001D6B60` / `001D6BA0` / `001D1FF0` / `001D1F20` / `001D7080` /
  `001CB760` (the veil module's builders over the context's channels), the
  ramp data D_0026E860 / D_0026E970 (exported) and a private stack view for
  001DF180's 0x10F0-byte frame (0x7E000000..0x7E0013FF, not game RAM).
  What the chain page refuses is the next unit: 001DF5A0 registers its
  channel-9 list (the grid pass, 0x00564000 in the run) as a slot-0xFFF
  CALL through 001CB760 (`em_rcl_001CB760` records it as the frame's
  four-sprite call), and the page walks it as plain packets, faulting on
  its FRAME_1 write (A+D register 0x4C at +0x1C): the page knows one
  slot-0xFFF pass (001DDE10's depth-of-field) and has no drawing for this
  one yet (drawing, not gameplay; resolved in iteration 22, whose run
  replaced the receipts r15 / r16).
- **Clean-up of the measurement:** every run of this iteration used the
  harness defaults (drive replay with the arrival anchor, the newest rand()
  table). Not run: the first-level smokes and EM_TEST_FULL (as instructed);
  New Game control 9.599849; quick tests: aim-fire-world-live,
  aim-fire-effects-live, aim-fire-sdk-memory reference, area01 runtime
  reference, area01 light live reference.

**2026-10-28, iteration 20: the AREA00 arrival and a00_00..a00_08 are exact
row for row, and a00_09 is exact through row 460; the run stops at a00_09
frame 13012, where a bug's sub-state machine `0012C490` faults (next unit).**
The one arrival row left is row 0's clock (the port 80.0 one tick early, the
original 0.0). Ignoring nothing else, every compared row of a00_00..a00_08
is exact, the `lock_r54` toggle class included (a00_03 / a00_04 keep 38 /
22 `lock_r54` rows), and a00_04's eight camera rows (the shake) are exact:
they followed the rand() stream. The fork's rand() return log of a01_07
(the AREA01 exit and the whole arrival, 168,984 draws) and the port's trace
now pair caller for caller with no difference.

- **The arrival's message phase (iteration 19's next unit) was the drive
  replay's anchor.** The arrival plays inside a01_07's recording, whose
  reads anchor at a01_07's row 0; across the AREA00 load the run's counter
  relation to the recording's changes (427 to 638 frames), so the arrival's
  voice reads were served off the recording's time and the message chain
  closed four ticks late (and the faces' talk draws moved). The AREA00
  harness now anchors the arrival again at its row 0 (`drive_replay.py`
  `write_table(extra_anchors=)`, the recording's counter 27563 at the run's
  26925).

**2026-10-28, iteration 19: the AREA00 arrival plays 4,011 of 4,017 rows
exact, and the creatures' rand() values are the original's through the
arrival's message phase.** The run still stops at a00_09 frame 12948
(001C2690), inside a00_09 rows that differ from row 152 (the taken bit 0x71);
a00_00..a00_08 are as before.

- **Why the creatures drew other values (iteration 18's open unit), in two
  parts.**
  1. *The first level is navigated, not replayed.* The level smoke drives
     AREA11 with its own navigation (em_level_smoke_test.c), so the rand()
     stream reaches the AREA01 arrival at another position than the route's
     recording: the per-call comparison (fork return-point logs of route
     beats 00..15 and a01_00..a01_07, every 00122BB8 return with its caller
     and result, against EM_RAND_TRACE) pairs other callers with the same
     values from route beat 00 on. The AREA01 / AREA00 phases replay the
     recording's pads, so one comparison-only anchor at the AREA01 area
     entry (the draw of 0x1AE040 state 0's 001FAE70(1), return address
     0x1FAF28) puts the recording's stream in place: with it, a01_00 ..
     a01_06 and a01_07 through the AREA00 load draw the original's values
     caller for caller. `tools/record_rand_replay.py` records the anchor on
     the fork (route 15: state 0x9f9c1222 before draw 8,983); the AREA00
     harness passes the newest `build/rand_replay/*/table.txt` as
     `EM_RAND_REPLAY` (`--rand-replay`, `--no-rand-replay`), and
     `em_level_smoke_test.c` seeds it at the area entry
     (`em_level_smoke_test_area_entry`, called from the scene bindings'
     `w_001FAE70`; inert without the variable).
  2. *The AREA00 arrival's entry stance was missing.* 001B07C0 copies the
     spawn record's +0x14 to the player's +0x0E and 0015C420 (byte-matched)
     switches on it at the first stage: entry 1 sets the selector
     0x70003B8D = 4, +4 = 5, +5 = 1, +0x1F0 = 1 (the walk-out, arrival rows
     540..541, then two footstep draws of 00179B90 that shifted the whole
     stream). The port now stores +0x0E (`spawn_commit`) and runs the
     switch at the rebuild stage (`em_player.c`
     `player_states_entry_stance`, every case of 0015C420). The walk-out's
     0015B610 then hands the player straight to the arrival script's
     takeover (+4 = 4, 00182B30 = 0), and 0015B130's prelude can do the
     same on a plain stage; the pose host's hold is now admitted on both
     paths (`em_player_stage_live.c` `w_0015B610`, `em_player.c`
     `live_major1`), or 0015B530's 00182DF0 refused the release at arrival
     row 4495. Arrival rows 0..80 (the stance) are now exact but row 0 (the
     clock: the port 80.0 one tick early, the original 0.0).
- **Next divergence:** the arrival's message phase, rows 3750..3754: the
  original's message block closes (+1 = 2, then 0) four ticks before the
  port's; the faces' 001D0720 draws there move with it (a01_07 draw
  136,761). Then a00_09 row 152.

**2026-10-28, iteration 18: `001EF510` bound; the AREA00 creatures'
divergence is an rand() phase lag, not a translation fault.** The run still
stops at a00_09 frame 13225 inside the diverged a00_09 (iteration 17).

- **`001EF510` (effect subtype 6, the box-break spray; stop S3)** runs as an
  em_effects_live handler over em_area00_fx's translation: its 001EFD90
  spawn through em_effect_original, 001CFB50 / 001CFBE0 through the
  module's workers, the direct 001EEEB0 through the hazard handler; views
  D_00275C30 / D_00275C34 and the work block's +0x04 / +0x54.
- **What makes the creatures differ.** Fork replays of a00_00..a00_04 and
  a01_07 (hidden, owned sessions; every row equal to the recording) logged
  every rand() call (return address) and the pool every frame; the native
  run logged its draws (EM_RAND_TRACE) and, in temporary builds, its
  class-2 records. Findings:
  - The two bugs 0x6A / 0x6E (records 43 / 46) are byte-identical to the
    original's through the arrival until arrival row 4496 (a01_07 row
    4496), where each enters 0012AFC0 state 0. 0012AFC0 draws rand() & 1,
    then 00128600(0) (one draw, D_00242ED0 + (rand & 15)), then a heading
    draw when 00128600 returned 0. The native creatures make these calls in
    the same order and frame as the original's (point light 4 draws, the
    bugs 5, 001E7D20 4, ...), but with other values: the original's first
    bug draw has state 0xBD4DA24D before it, which the native consumes two
    frames later (another caller); the native bug draws come 104 / 105
    draws earlier in the stream. So 00128600 answers differently and the
    two bugs take each other's branches (0x6A +6 = 3 instead of 1).
  - Within each replayed beat the number of draws per frame is equal
    (a01_00, a01_02, a01_07 including the AREA00 load, a00_00..a00_04, and
    AREA11 beats 12..14): the lag is gained outside the sampled rows (the
    frames between beats, the AREA11 -> AREA01 change, or beats not yet
    replayed: a01_01, a01_03..a01_06, 15_level_exit, whose fork replay
    stopped with an error). Locating it is the next unit; everything the
    creatures do in AREA00 (and the bugs of the a01r event) depends on it.
  - Tools (ignored, `build/frontier/`): `fork/fork_pool_probe.py` (the
    iteration-14 probe; `pool` / `nopool`, `calls`), `tools/syncdraws.py`
    (draws per row, fork vs native), `tools/frameorder.py` (consumer order
    within a frame), `tools/rareoff.py` (offset on named consumers),
    `tools/c2diff.py` (class-2 records by +0x9A), the temporary diagnostic
    patches `diag_scene_bindings.patch` / `diag_class7.patch`, fork data
    `fork/a00_00..a00_04`, `fork/a01_07`, `fork/l1_10..l1_15`.

**2026-10-28, iteration 17 (port main b4a4147 and `pretrans-a00` merged):
the C side runs a00_09 to frame 13225** (674 rows; a00_00..a00_08 unchanged),
where a bug's sub-state machine `0012C490` faults (node 0x7BDFE0). Not the
first unit: from row 152 the a00_09 rows differ (creature 0x71 dies late),
and from about row 462 the player is hit and the rows diverge (`hp`, `pos`,
`req`), so the stop sits inside a diverged run. The creature drift
(iteration 16) is the next unit.

- **`001F6FB0`, the glow node, plays through:** its source block
  D_0025D800 (exported, `CREATURE_EFFECT_WINDOWS`; mapped writable in the
  aim/fire composition when present, the glow rewrites +0x10..+0x18), its
  parent's work block (read-only copy, the halfword +0xF4) and the spark
  matrix 0x700036E0..0x7000371F (write-first storage in
  em_aim_fire_runtime) are views. docs/AREA00_PRETRANS.md stop S2.
- **`001ECFB0` (byte-matched decomp C), effect subtype 3, is bound** as an
  em_effects_live handler beside 001EEBA0 / 001EEEB0 (its sources
  D_002571B0 + 0x90 k exported). Stop S1.
- The 0x257870 window grows to 0x2D0 bytes (001EF510's tables D_00257990 /
  D_00257A20 / D_00257AB0, stop S3, adjacent); em_effects_live takes up to
  96 windows (65 exported). A private `effect_tables.emet` is rebuilt from
  scratch (`rm assets/effect_tables.emet` first) because a window changed
  size.

**2026-10-28, iteration 16: the C side plays a00_00..a00_08 and 429 rows of
a00_09, and stops at a00_09 frame 12980** (the creatures' glow node
`001F6FB0`, now bound but faulting on its parent's +0x2E4; next unit).
Ignoring `lock_r54`, a00_05..a00_08 are exact row for row (a00_04's eight
camera rows as in iteration 15). The first differing row is now a00_09 row
152: the original sets taken bit 0x71 there (creature 0x71, 00128C10 state
3, 001B1190 then 001AFC10), the port 57 rows later; and bit 0x1B (crate)
at row 193, the port at 200.

- **The spat hazard's hit test reads the cell record's class.** 001E2E80
  tests the byte +0x1A of the record a 0019A570 hit names (D_700030B0 +
  0x1A = 0x700030CA); the composition's region views now carry that
  halfword (read only) beside +0x1C / +0x20. This was the a00_06 frame 9758
  stop.
- **`001F6FB0` (NEARMISS decomp C, em_area00_fx_glow.c, now in the
  build),** the glow node 00129FC0's 001EFE00(0x8000000F, creature) makes,
  ticks through em_aim_fire_runtime's other-tick hook and
  em_aim_fire_world_live (binding row in em_area11_bindings). Its parent's
  header, +0x60, +0x80 and +0xB0 are the pool's fields and its +0xD0 a
  read-only copy from the AREA01 actor view
  (`em_scene_bindings_actor_snapshot`).
- **Why the creatures diverge (open).** Fork pool dumps (hidden, owned
  sessions, the iteration-14 probe replaying each beat from its recorded
  source state; 0 row differences) show the AREA00 bugs 0x6A / 0x6E already
  differ from the native ones at a00_04 row 0, and the native rand() stream
  is 266 draws ahead of the original's at a00_05 row 0 and 615 at a00_09
  row 0 (the creatures' headings, 00128C10 sub-state 1's
  001B1470(2 pi (rand & 0xF0) / 256), come from it). Over a00_05 the
  per-caller totals agree except 001F4BF0 (the creature glow 001288D0 ->
  001F4A00 -> 001F4BF0, one draw per call): original 327, native 218,
  because a glowing creature is not drawn (+1 = 0) on frames where the
  original's is. Receipts: ignored `build/frontier/fork/a00_00..a00_04`
  (pool.jsonl, rand_calls.bin); the native class-2 dumps (i16c2, a
  temporary EM_DIAG_CLASS2 build) were removed once iteration 19 resolved
  the drift.

**2026-10-28, iteration 15 (branch `drawbridge`): the C side plays
a00_00..a00_05 and 181 rows of a00_06, and stops at a00_06 frame 9758**
(001E2E80 reads the hit record's +0x1A at 0x700030CA, which the composition's
views do not carry; iteration 16). Not a capture pass. Ignoring `lock_r54`,
every compared row of a00_00..a00_05 is exact except a00_04 rows 1546..1553
(8 rows: camera `eye` / `tgt` about 0.4 off, then equal again; open
fidelity unit). The a00_04 frame 6540 stop is gone:

- **`001EFFD0` (byte-matched decomp C), the creatures' spray spawn,** is
  bound in `em_area01_light_live.c` over em_area00_fx's translation:
  001EF9D0 through the host (em_effects_live), then node +0xB0 / +0xC0
  copies, +0x20 = f12 and the halfword +0x94 = the tag. The +0x20 word now
  has a home: `EmActor.w20` (no pool function writes it; only the 001AF8E0
  memset clears it), serialized by `em_actor_pool_record_image`, owned by
  the AREA01 actor view (+0x00..+0x23) and mapped by the aim/fire pool view
  and `em_effects_live_node_field`. `test_area01_light_live_reference.py`
  runs the original 001EFFD0 with 001EF9D0 scripted (a free record or 0,
  tags 0xA / 0xB) and compares RAM, scratchpad and v0.
- **`001E2E80` (byte-matched decomp C), the spat hazard (entity
  0x80000008, subtype 3),** ticks through em_aim_fire_runtime's other-tick
  hook and em_aim_fire_world_live (em_area00_hud's translation over the
  composition's views; binding row in em_area11_bindings). Its callees:
  `001D04B0` with an ELF source block (`em_effects_live_001D04B0_elf`;
  D_00253740..D_00253A10 exported by `export_area00_runtime_tables.py`
  HAZARD_WINDOWS), `001F02C0` (em_area00_fx), and `0019AA80` (byte-matched
  decomp C) over the one probe state with `001A7280` against the player
  record (em_coll_grid_hull; the player's hull chain, resource 0x3A at
  D_0028A578[0] = 0xD1B9C0, exported by the new `tools/export_player_hull.py`
  from the user's extract and checked against 12 captures; 0x700031D4 names
  the player as 0x008102B0).
- **The hazard's two effects, `001EEBA0` / `001EEEB0` (byte-matched decomp
  C; entities 0x80000029 launch / 0x8000002A hit),** run as em_effects_live
  subtype handlers over em_area00_hud's translations; their descriptor
  windows D_00257510 (0x1B0) and D_00257870 (0x120) are exported
  (HAZARD_EFFECT_WINDOWS; the captures' rows D_00257530 / D_00257650 hold
  either the ELF words or 001EEEB0's stores, which the exporter checks).
- Assets: this worktree's `assets/` is a private directory (symlinks to the
  primary checkout's entries, private regular files `effect_tables.emet`,
  63 windows, and `player_hull.emom`).
- Receipts: the run i15e was removed (superseded by later iterations).

**2026-10-21, iteration 14 (port main 74e6c04 plus the creatures' ground
probe): the C side plays a00_00..a00_03 and 143 rows of a00_04, and stops
at a00_04 frame 6540 on the unbound effect spawn `001EFFD0`.** Not a
capture pass. Ignoring `lock_r54` only (no longer `progress`), every
compared row of a00_00..a00_04 is exact. Before this change the run reached
a00_09; the earlier stop now comes from creatures that behave as the
original's do, which reach workers the drifting ones never did.

- **The cause of the AREA00 creature divergences (the taken bits 0x65 in
  a00_05 and 0x71 / 0x72 in a00_09, and the row-462 attack): `001C25E0`
  dropped its result.** A fork replay of the a00_05 and a00_09 beats from
  their recorded source states with the recorded inputs (0 row
  differences, disposable probe, receipts below) dumped the pool every
  frame: the original's creatures 00128C10 / 0012A5D0 stay in sub-state 0
  at their placed positions (on the floor at y -59.6, on crates at -38.1,
  -33.7), while every native one entered sub-state 8 when visible and
  drifted (it fell off the crates: y -38.1 against -59.6). Sub-state 0 sends
  a visible creature to 8 when `001C25E0(e, (0, -1.4, 0, 1))` is 0. A
  native trace of `0019B4C0` showed it returning the original's hit mode
  (4, the floor face 0x01516DEC, the same scratch words as the original
  instructions over the fork's a00_00 RAM), but `em_area01_math_001C25E0`
  never handed it back: the decomp C declares `001C25E0` void, yet nothing
  after its last call writes v0, and the original returns 0019B4C0's v0
  (the original instructions return 4 over the same RAM). The translation
  and the runtime dispatch now return it; `test_area01_math_reference.py`
  compares it (`v0=True`; a translation returning 0 fails at
  `unit:a01_07_level_exit:0x7acbc0`, original v0 4). The live hand-back in
  `em_area01_runtime_dispatch.inc` has its own check:
  `tools/test_area01_runtime_reference.py` runs 001C25E0 through the
  runtime over the a01_07 capture (24 cases: 4 creature nodes x 6 probe
  vectors, hits and misses, every host worker call as original code) and
  compares RAM, scratchpad and the call's v0 with the original
  instructions. A dispatch that drops the hand-back fails at node
  0x7AC2F0, (0, -1.4, 0) (v0 4 expected).
- **`001C24D0` (byte-matched decomp C), the re-aim probe that 0012B970 /
  0012BE20 / 0012C490 run,** now bound in the live composition
  (`em_area01_light_live.c`, over em_area00_world's translation; its three
  callees forwarded; its v0 is 0019AD00's, passed through because nothing
  after that last call writes v0, and those callers ignore it): `make test-area01-light-live-reference` runs the original
  instructions with 001026A0 / 001028B8 as original code and 0019AD00
  scripted (v0 0, 1, 2, 4, 2^32, -1, the high word only), comparing each
  call, the record and scratchpad at each, memory after and the result;
  mutants that drop the v0 or pass a1 for a2 are rejected.
- **`0021BD60` (byte-matched decomp C), the grab test 0012E0B0 calls on
  the player record,** bound the same way (one read-only region, the
  record's bytes +0..+0x236): 104 quick cases (all tested state bytes)
  against the original leaf; a mutant returning 0 is rejected.
- **D_00281F30, the ten cue records,** are now in the AREA00 / AREA01
  runtime's bytes (`em_area01_live.c`, em_stream_live's storage): the
  creatures' 001FC580 (em_area00_low) reads and writes the record its id
  selects (0x1AA / 0x1B5: +0x20). This was also iteration 13's stop.
- **The stop: a00_04 frame 6540**, "owner 0012A5D0 node 007AD490 failed at
  001EFFD0" (byte-matched C: 001EF9D0(kind, a, 1.0), then node +0xB0 / +0xC0
  copies, +0x20 = f12, +0x94 = the tag). Its translation is
  `em_area00_fx_001EFFD0`; binding it needs a home for the effect node's
  +0x20 word, which `em_effects_live_node_field` does not map (next unit on
  front B; check which subtype handler reads it first).

The first level is unchanged up to the AREA01 arrival: the first-level
smokes' tick logs equal 74e6c04's for host 13,754 of 13,813 lines and drive
14,045 of 14,104, and the RNG traces and present logs are identical. The
last 59 lines (the AREA01 arrival frames, drive 14045..14103) differ in two
keys, `owner_units` (59 lines) and `snd` (55 lines), and both come from
the four AREA01 creatures 0x7A8830 / 0x7A8B20 / 0x7A8E10 / 0x7A9100
(00128C10):
  - `owner_units`: they now stay in sub-state 0 and draw (2,128 bytes)
    instead of drifting in sub-state 8 and drawing nothing.
  - `snd`: in 74e6c04 the four drifting creatures ran 00128C10's
    sub-state-8 call 001C2770(e, b, 2), whose not-grabbable branch plays
    001FBD50(e, 0x1AC, 0, 300.0). A native trace (temporary, env-gated,
    removed) shows those four calls at sound tick 13919 and a fifth on
    0x7A8E10 at 13964; no other sound-service call differs between the
    two builds.
    They keyed four voices (8..11, tracks 3..6 = sound 0x1AC, serials
    615..618) and later a fifth; every later `snd` difference is the same
    sounds on shifted voice slots and serials (cur / ser 587 / 619 against
    583 / 615). The new build makes none of these calls.
  - The original makes none either. A hidden fork replay of route 15 from
    its recorded source state with the recorded inputs (all 809 rows equal
    to the recording) logged every original call of 001FBD50 and 001C2770
    (inline log probes on their entries) and dumped the four records.
    00128C10 calls 001C2770 (return address 0x129520) on each of its six
    AREA01 creatures 0x7A8250 .. 0x7A9100 once per tick from tick 754 on,
    always with flags 0, the not-sub-state-8 call; it never passes flags 2.
    No 001FBD50 call in rows 0..809 has id 0x1AC; the arrival rows are
    749..809. The four records are 00128C10 creatures whose sub-state +5
    is 0 at row 749, 1 at 750 and 0 from 753 to 809, never 8. The
    original's 001FBD50 calls in the arrival window are ids 0x411..0x413
    (return address 0x1FC4D8) and one 0x19C (0x1556F0). So the new build's
    arrival sound state is the original's, and 74e6c04's extra voices came
    from the drift bug. Receipts are in
    `build/frontier2_i14_20261021_0300/fr_snd/`.
Both smokes
and their checker pass, newgame-control 9.599849;
`tools/test_level_smoke_area01.py --until a01_07` passes 28 live phases
(403 of 597 drive-replay reads served, as before). Receipts in ignored
`build/frontier2_i14_20261021_0300/`.

**2026-10-21, iteration 13 (port main bb4f5e9 plus the bug probe and the
knife strike): the C side plays 472 rows of a00_09 and stops at frame
13023; rows 0..461 are exact (ignoring `lock_r54` and `progress`), rows
462..471 are not.** Not a capture pass. In the order the run met them:

- **The stop at frame 12884 was a use-after-free, not a missing
  worker.** The bug 0012A5D0's probe (0019B4C0 flags 4 -> 0019CF50,
  NEARMISS decomp C; `em_area01_sys.c` `f_19CF50`) read its face list
  tables 0x70003210.. (in the collision file at 0x01512554..) as
  garbage (face index -4795, then 0x2C9E: the face 0x015C94EC). The
  original's tables there hold indices 0..768 in every AREA00 snapshot.
  Cause: the module loader's read of module 0x1D (sub 1's static bank, at
  D_0028A5A0 = 0x015CE380, inside room 0's resident region) copied the
  region's untouched prefix to new storage and freed the old one, which
  the collision view (bound at the arrival) still read. A diagnostic run
  dumped the view's file bytes: 112,917 of 137,216 bytes differed from
  the original's RAM. Two changes: `em_module_loader.c` `region_for`
  keeps the untouched prefix's storage (the bytes are unchanged); the
  collision view re-resolves its file mapping from the loader on every
  access (`EmArea01CollisionHost.file_rest`, bound in
  `em_area01_live.c`), so its extent ends where the bank begins. Checks:
  `make test-module-loader-replay` (a read inside a region: the prefix
  keeps its storage and bytes; the old loader fails it with an ASan
  heap-use-after-free), `make test-area01-collision-view-reference` (a
  re-mapped file is read through the current mapping, an absent mapping
  refuses; a view that keeps the bind-time mapping fails it).
- **The knife strike `00189FE0` (byte-matched decomp C) on the first
  bug** (a00_09 row 395, after the Square at row 383): kind 2 passes the word at
  (word 0x700031D0) + 0x1C to 001B41F0. After an entity hit the record is
  D_700030B0, so the word is 0x700030CC (the hull walkers' word). The
  aim / fire composition's views now include 0x700030CC and 0x700030D0,
  read only, when the record word is 0x700030B0
  (`em_aim_fire_world_live.c` `refresh`; `make
  test-aim-fire-world-live`: the strike's +0x36 follows the word (0x9000
  with 1, 0x1000 with 0); the old binding faults at 700030CC).
- **The bug's debris, model 0x06** (effect node 001F2BA0 -> 001F3E30:
  `D_0025A350` rows 2 / 10 / 19 / 23 have table 0x37, +0x54 = +0x58 = 6;
  the run's model handle 0x00BCB340 is entry 6 of D_0028A56C):
  `export_roger_banks.py` grows the span 0x07..0x0F to 0x06..0x0F (model
  0x06 precedes 0x07 in file order; the region count stays 16; the
  non-library regions are unchanged). The region equals RAM in all 16
  AREA11, 16 AREA01 and 12 AREA00 captures; `export_area00_library.py`
  re-applied after it.
- **`001CD390` (NEARMISS decomp C) from the piece's 001F3620 state 2**
  (001CD390(0x700036A0, piece + 0x50)): `em_aim_fire_sdk_memory.c` runs
  em_effect_original's translation (read from the .s) over the out matrix
  and the scratchpad 0x70003600..0x7000363F it builds the rows in;
  refused for an unaligned out or an out / v inside that block.
  `test_aim_fire_sdk_memory_reference.py`: 72 cases (8 with signed zeros
  and the x = z = 0 branch, 64 random) against the original instructions,
  the out matrix and all 16 scratchpad words, poisoned starts, 3
  refusals; a mutant that skips the last scratch quadword and one that
  tests v.y instead of v.z are both rejected. No dispatcher handled
  0x1CD390 before (every caller faulted), so no earlier path changes.
- **Rows 462..471 differ: the recording's player enters state 11 at row
  462 (a bug's attack: `player` (11, 59, ..) and `req` byte 3 = 1); the
  native player does not** and the positions drift from there. Not
  established: the likely cause is the kept divergence below (the
  recording's bug at record 49 dies at row 152, the native's at record
  50 at row 244), so the attacking bug's state differs; that needs the
  fork trace of the two bugs before any change.
- **The stop: a00_09 frame 13023** (row 472), "owner 0012A5D0 node
  007BD710 failed at 00281F50": the bug's runtime reads D_00281F30 + 0x20
  (the stream cue table, mapped for the aim / fire target sound but not in
  the AREA00 / AREA01 runtime's bytes). It lies after the row-462
  divergence, so it is not the next unit by itself.
- **Next unit on front B:** a00_09 row 152 (the taken bit 0x71: why the
  original's bug at record 49 dies there and the native's does not),
  from a fork trace of records 49 / 50 over rows 100..160; then rows 462+.

Windowed against route_a00 ignoring `lock_r54` and `progress`,
a00_00..a00_03 and a00_05..a00_08 stay exact, a00_04 differs only in the
shake rows 1546..1553, and a00_09's rows 0..461 are exact. The first-level
smokes' tick, RNG and present logs equal port main bb4f5e9's (identical
SHA-256 to iteration 12's receipts); newgame-control 9.599849. Receipts in
ignored `build/frontier2_i13_20261021_001202/`.

**2026-10-20, iteration 12 (port main c40818b plus the barricade crate's
break): the C side plays 333 rows of a00_09 and stops at frame 12884, in
the second crate child's collision probe.** Not a capture pass. In the
order the run met them:

- **The crate's nest children, `001AFA90` + the record copy** (001551B0
  state 2 at 00155FC0, NEARMISS decomp C; `001AFA90` byte-matched). Crate
  uid 2201 (model 0x50) has link 3: `D_0024D820[0]` = 0x8284C0, row
  `D_0024A850[0]` + 3 = 6, three records (puids 0x7D, 0x7E, 0x7F; class 2;
  behaviours 00128C10 / 0012A5D0 / 00128C10: the bugs). `em_area11_boxes.c`
  `h_spawn` now allocates from the canonical pool
  (`em_actor_pool_alloc_001AFA90`) and copies the fields in the original's
  order (+0x9A, +0x03, +0x2E, +0x0D, +0x9D / +0x9E or +0x0E, +0x54, +0x56,
  +0xB0.., +0xC0.., +0x10); the child fields themselves are
  `em_crate_original`'s, checked against the original instructions by
  `test_crate_original_reference.py` (`oracle_child`). The new binder hook
  `em_area11_boxes_bind_children` (bound with the registry in
  `area01_bind_live`) gives the child the AREA00 / AREA01 runtime binding
  (`area00_bind_deferred`'s); without it a child still fail-stops (AREA11).
- **Its taken bit, `001B1190`** (byte-matched): `h_set_taken` calls
  `em_area11_bindings_001B1190` (the verified `em_gun_rest_001B1190`).
  The run sets bits 0x1C (row 192), 0x1B (193) and 0x1D (266), each at the
  recorded row.
- **The break lift `001EBC30`** (effect 0x80000014, subtype 8; NEARMISS
  decomp C): wired into `em_effects_live.c` (`handler_001EBC30`, the
  translated `em_area00_hud_001EBC30`, 0x70003400 as the module's
  `spad3400_lift`). Its sources `D_00256280` / `D_00256310` are exported
  (`export_area00_runtime_tables.py` `BOX_BREAK_WINDOWS`, equal to the ELF
  in all 13 retained AREA00 snapshots). `make
  test-area00-box-lift-reference`: 96 packet chains (576 with
  `EM_TEST_FULL=1`, 12 captures) against the original instructions, whole
  RAM and scratchpad, both +0x38 values, two refusals.
- **The debris models 0x1F / 0x21** (effect 0x8000000B's node `001F2BA0`,
  subtype 1: `D_0025A350` row 1, table 0x37): `export_roger_banks.py`'s
  `BREAK_SPANS` now holds 0x1F..0x22 as one span (with the husk 0x22;
  still 16 regions), equal to RAM in all 16 AREA11, 16 AREA01 and 12 AREA00
  captures; `export_area00_library.py` re-applied after it.
- **The bug's sub-state machines** (0012A5D0, em_area01_exita, by +5):
  `em_area01_live.c` `low_entry` dispatches em_area00_low's 0012B410,
  0012B970, 0012BE20, 0012C490, 0012CAA0, 0012D240, 0012D850, 00129FC0 and
  0012E070 (`make test-area00-low-reference`, full sweep 2,141 cases); the
  run reached 0012D850 (word assembly).
- **`001027E0`** (asm-word SDK leaf) from 0012ADC0: `sdk_001027E0` over
  `em_rvr_001027E0` (`test_render_verify_rest_reference.py`).
- **The stop: a00_09 frame 12884** (row 333): the bug 0012A5D0 at node
  007BD710 calls `0019ED80(0x70003190, 0x015C94EC)`; 0x015C94EC lies in
  the static cell data (`D_0028A5A8` = 0x1523380 + 0xA616C), not in the grid
  hull nodes, and the collision view refuses it (code 3). Next unit on
  front B: 0019ED80 over a static cell record.
- **Divergence kept (not new):** a00_09 row 152, the recording sets taken
  bit 0x71 (the bug 00128C10 at record 49 dies); the native run does not,
  and at row 244 sets 0x72 (the bug at record 50). Hidden in the windowed
  counts because `progress` is ignored with `lock_r54`; also present in
  iteration 11's 191 rows.

Windowed against route_a00 ignoring `lock_r54` and `progress`, a00_00..
a00_03 and a00_05..a00_09 (333 rows) are exact and a00_04 differs only in
the shake rows 1546..1553, as in iteration 11. The first-level smokes'
tick, RNG and present logs equal port main c40818b's; newgame-control
9.599849. Receipts in ignored `build/frontier2_i12_20261020_212528/`.

**2026-10-20, iteration 11 (port main 8284849 plus AREA00's sub-area
switch): the C side plays a00_08 to its end, aligns a00_09 and stops 191
rows into it, at the first barricade crate's break.** Not a capture pass.
In the order the run met them:

- **Op 0x11, `001B7700` (NEARMISS decomp C).** `em_area_script.c`'s
  `execute` now sends op 0x11 to the area binding's handler, as ops 0x03,
  0x05, 0x0E, 0x12, 0x13, 0x17 and 0x1A; the AREA01 / AREA00 script
  worker already ran `em_area00_low_001B7700` (`make
  test-area00-low-reference`: 12 of 12 conditional outcomes). Record
  0082A600's mode word 2 stores D_008106CE = 2 and D_008106CF = 0x81 and
  raises D_00275BD8; the classifier `001AE7E0` (byte-matched) then returns
  3 and the frame machine enters state 6 (a00_08 row 362, as recorded).
- **State 6's `001FF030` (byte-matched decomp C).** `em_scene_bindings.c`
  binds it (state 6 only) to `em_area00_low_001FF030` over the scene
  state's bytes (`em_area01_scene_view`): D_00810730[0] = 0x81,
  D_00810701 = 1, D_00275BD8 = 1, and its tail `001FF080(0, 0x1D)` is the
  loader task's request. `001FEFE0` (D_008106CE other than 2:
  `001FF080(2, 0)`, the untranslated bank-set streamer `00200360`) stays
  unbound. The SFX registry scope follows D_00810701 there
  (`em_sfx_set_area`; 001FB9F0 pages its ids by D_00810700/701). State 6's
  return calls `001FAE70(1)`, now bound (`w_001FAE70`).
- **Module 0x1D.** Its header (read locally from the user's disc) names
  no chunks and no sections, a 1,873-sector payload and one relocation,
  slot 0x44 (D_0028A5A0) at offset 0: 001FF830's kind 2 delivers it at
  D_0028A5A0 and relocates the slot onto it. The exporter's
  `SUBAREA_MODULES` adds it to the module pack (MODULE_LOADER.md section
  4; 57 ranges, 43,201,712 bytes; ignored, never committed).
  Its 0x3A8800 payload bytes equal, byte for byte, the last 0x3A8800 bytes
  of AREA00 room 1's resident region (offset 0x1ED800; checked locally
  on the user's disc; nowhere in room 0's): AREA00_ASSETS.md's two nested
  blocks differ only in id 0x44, the static bank, so module 0x1D is sub
  1's static-object bank; the grid, the cell directory and the model bank
  are shared. After the load the first
  original reader is state 6's `001C1DC0` (001C1E70 -> 001D52E0 reads the
  bank's grid header): there `w_001C1DC0` rebinds the render context's
  copy of the bank from the drive's delivered bytes and selects the
  object-texture catalog and the shadow receivers of AREA00 sub 1 (no GS
  upload: module 0x1D has none).
- **The load's timing: the comparison-only drive replay now covers the
  module loader's bank loads** (IOP_STREAM.md "Drive replay"; the user's
  decision of 2026-10-19 covers each disc read). The a00_08 recording's
  slot-2 record (sampled once per tick, the task dispatching once per
  tick) shows the header read issued at row 363 and done at row 370 (6
  busy polls) and the payload issued at row 372 and done at 448 (75); no
  stream read is in flight then (the stream table's reads at counters
  40047 and 40172), so the gate D_00282157 is clear. `tools/drive_replay.py
  loader_reads` derives these "L" entries from the fork trace and the
  disc's module header; the AREA00 harness adds them to its table; the
  loader takes an entry with its file, sector and count within 2 counters
  (`em_module_loader_set_drive_replay`; `make test-module-loader-replay`).
  The run: state 6 at row 362, sub-step 1 at 363, D_00275BD8 clear at
  451, state 1 at 452, all as recorded.
- **The switch owner's puffs `00823820`** (func_overlay_AREA00_008237E0,
  byte-identical C; `em_area00_ovl_00823820`, `python3
  tools/test_area00_overlay_reference.py`: 22 cases, 235 of 235 reachable
  words). The switch 00825920's `001EFD20` (a00_08 row 456) allocates an
  effect node whose callback is this AREA00 owner; the effect binder only
  knew AREA11 rows. `em_area11_bindings_set_spawn_fallback` now offers an
  unknown callback to the scene's area fallback: in AREA00 a resident-overlay
  callback of `area01_rebind`'s AREA00 list runs through the same AREA00
  runtime as the roster's owners; elsewhere nothing changes. Its packets'
  sources D_008287E0 / D_00828870 are overlay tables: `001CFBE0` through
  `em_effects_live_001CFBE0_bytes` now keeps an otherwise unmapped
  overlay source block by address (as 001D04B0's), so the chain page's REF
  reads them.
- **The stop: a00_09 frame 12742** (row 191, counter 39759), "001551B0
  at record uid 2201 faulted": the barricade crate's break reaches
  `001AFA90` and its nest child copy (`em_area11_boxes.c` `h_spawn`,
  unbound). Next unit on front B: that child spawn.

Windowed against route_a00 (owners included), the counts before a00_08
are those of iteration 10 (a00_00 242/363 .. a00_05 655/1,160); ignoring
`lock_r54` and `progress`, a00_00..a00_03 and a00_05..a00_09 (487 and 191
rows) are exact row for row and a00_04 differs only in the shake rows
1546..1553. The first-level smokes' tick, RNG and present logs equal port
main 8284849's (sha256 2866e0de... / f8a37cb8... full, d2cb9bed... /
2fa87dc2... drive switch); newgame-control 9.599849;
`tools/test_level_smoke_area01.py --until a01_07` passes 28 live phases
with the replay's AREA01 counts unchanged (403 of 597 served). Receipts in
ignored `build/frontier2_i11_20261020_184329/`.

**2026-10-20, iteration 10 (port main b804f8c plus AREA00's switch movie):
the C side plays a00_08's movie and stops three frames after it, at the
area script host's op 0x11.** Not a capture pass.

- **`\MOVIE\E006S1.PSS`, selector 2** (the ELF's name table
  D_00264FB0[2], read locally: E900 / E001 / E006S1 / E007 at entries
  0..3). Exported from the user's disc with `tools/export_movie.py
  --disc-path /MOVIE/E006S1.PSS --out assets/movies/e006s1.mov` (STARTUP.md
  step 70; ignored, never committed): 741 pictures 640 x 480, 24.7 s of
  video, 27.7 s of 48 kHz PCM. `em_frontend.c`'s `k_movies[2]` names it.
- **Its field-clock facts, measured on the fork** (decomp
  `build/fork_refs/probes_1020_e006s1/`, its `tools/probe_e006s1.py` and
  `analyze_e006s1.py`; hidden, run-locked, the a00_08 beat replayed from
  a00_07's end state with the recorded inputs and pad correction, all 363
  replayed rows equal to the recording): 741 field-0 / field-1 send pairs,
  none repeated; the first wait's field 0; 002036E0's entry at vblank 359,
  the gate at 372; the last regular field-1 send 1,851; the feed loop's end
  1,976 (`feed_end` 1,604); the 2 held pictures sent from 1,978 after a
  127-vblank stall (D_007A55F8 125); the play loop's end 1,981; the closing
  waits on fields 0 and 1 (1,982 / 1,983). `k_movies[2]` = {741, 2, 1604};
  `tests/movie_field_test.c` reproduces every one of those offsets from the
  gate (`make test-movie-field`, which also decodes the export's 741
  pictures in order).
- **The level smoke skips it** after 2 s (the frontend's test skip, 63
  pictures, as for E900 and E001; the fork recording played it whole). The
  game sees one main-loop step either way: a00_08 rows 356 -> 357 and the
  rows after it compare exact.
- **The stop: op 0x11 (`001B7700`, NEARMISS decomp C) at a00_08 frame
  12515**, "001BA1F0 of 00825920 faulted at 0024D8C4 (record 0082A600)":
  the dispatch read of `ftab_0024D880[0x11]`; `em_area_script.c`'s
  `execute` has no case for it. Its translation `em_area00_low_001B7700`
  and the AREA01 / AREA00 script worker for it already exist (the binding
  admits `001B7700`). Record 0082A600 (a00_07's end snapshot): mode word 2,
  byte +0x14 = 1, so the command sets D_008106CE = 2, D_008106CF = 0x81 and
  D_00275BD8 = 1, then waits for D_00275BD8 == 0. The game task's
  classifier `001AE7E0` (byte-matched) then returns 3 and the frame machine
  enters state 6 (`em_scene_frame.c`, the unported arm): `001FF030(0x81)`
  (em_area00_low's checked translation: D_00810730[area] = 0x81,
  D_00810701 = 1, D_00275BD8 = 1, then 001FF080(0, 0x1D)). The recording
  shows the result: the area bytes become 00 / 01 at a00_08 row 363
  (counter 40082), six rows after the movie, AREA00 sub 1. Next unit: the
  op 0x11 dispatch, state 6's `w_001FF030` / `w_001FEFE0` and the sub-1
  load they start (module 0x1D through the loader task).

Windowed against route_a00 (owners included), the counts before the movie
are unchanged from iteration 9 (a00_00 242/363 .. a00_05 655/1,160) and
a00_08 now compares 360 rows (to the stop); ignoring `lock_r54` and
`progress`, a00_00..a00_03 and a00_05..a00_08 are exact row for row and
a00_04 differs only in the shake rows 1546..1553. The first-level smokes'
tick, RNG and present logs equal port main b804f8c's (sha256 2866e0de... /
f8a37cb8... full, d2cb9bed... / 2fa87dc2... drive switch); newgame-control
9.599849. Receipts in ignored `build/frontier2_i10_20261020/`.

**2026-10-20, iteration 9 (port main e88dc26 plus the workers a00_05 ..
a00_07 reach): the C side plays a00_05, a00_06 and a00_07 and aligns
a00_08; the stop moved to a00_08's movie.** Not a capture pass. In the
order the run met them:

- **`001288D0` (byte-matched decomp C, src/func_001288D0.c), 00128C10's
  glow** (a00_05 frame 9075: "owner 00128C10 node 007AC5E0 failed at
  001288D0"). `em_area01_live.c` now hands it, as `0012D580` already is,
  to its checked translation `em_area00_low_001288D0` (`make
  test-area00-low-reference`) over the live composition's byte views; its
  callees re-enter the runtime. Its tail call `001F4A00` (byte-matched,
  `src/func_001F4A00.c`: a frameless jump to `001F4BF0`) is served by the
  flame services as `001F4BF0` on the caller's stack pointer and
  arguments. `make test-area01-glow-services-reference` now also runs
  `001F4A00` through the services against the original instructions
  (`001F4A00` -> `001F4BF0` -> `00122BB8` / `001CD520`): every capture
  with 001288D0's two colour sets (byte +0xE1 zero / non-zero), the
  controlled clips and three refusals (quick 36 tail-call bodies); a frame
  of 0x50 instead of 0x40 for the tail call fails it.
- **Camera actions 9 and 11 (`00198CE0`, byte-matched; `00198F10`,
  NEARMISS) outside AREA11** (a00_07 frame 11842: "FAULT at 00198CE0").
  With the AREA01 / AREA00 camera worker set, `em_camera_live.c` bound only
  mode 1's `001B0300` and action 10 (forwarded). The recording runs the
  duct's 9 -> 10 -> 11 -> 0 (a00_07 rows 888, 1034, 1035, 1098; player
  states 44 / 45 / 46): 9 and 11 are now bound there to the same
  `low_run` as in AREA11 (CAMERA_LIVE.md section 5); 14 stays unbound.
- **`001B0460(1)` from the player closures** (a00_07 frame 11988: "player
  closure: reached 001B0460 (camera re-init)"). `0016D130` state 13 and
  `0016DE40` states 0 / 0x17 (both byte-matched C) re-seat the camera in
  area 0 after placing the player. `em_player_closure_live.c` binds both
  slots to `em_scene_bindings_001B0460` with the record's +0xA0 quad
  (D_00810350, the previous feet the stage keeps there), +0xC0 quad
  (D_00810370, the heading the state just stored) and +0x230; the 10 / 12
  / 19 / 1A closure publishes its scene copy first (0016DE40 stored
  D_00810702 = 5 and D_008106BE = 2 in it) and reloads D_00810702,
  D_008106BE, D_008106B5..B8 and D_008106C8 after.
- **001B0080's rotation 0x70003B50.** The first such re-seat (a00_07 row
  1035) seated the camera 90 degrees off: `rw_001B0080` read the scene
  state's spad3B40[4..7], 001B07C0's store at the AREA00 arrival. Only
  inside 001B07C0 does 001B0080 see that store; otherwise 0x70003B50 holds
  0015BCF0's tail copy of +C0..+CC (byte-matched src/func_0015BCF0.c),
  which the camera already loads per entry (`C.s3B50`). `EmCameraLiveRoom`
  now carries the quad: the spawn binding passes spad3B40[4..7], the script
  host and the closures NULL (the tail publication). Rows 1035..1036 are
  then exact.

Proof. The first-level smokes' tick, RNG and present logs equal port main
e88dc26's (sha256 2866e0de... / f8a37cb8... full, d2cb9bed... /
2fa87dc2... drive switch); newgame-control 9.599849; `make
test-level-smoke-full` and `make test-level-smoke-ps2-drive` pass;
`tools/test_level_smoke_area01.py --until a01_07` passes 28 live phases.
The AREA00 run's tick log equals the previous build's line for line up to
its faulting tick (34,744 lines), and the 0x70003B50 change alters nothing
before a00_07 row 1035. `make test-area00-low-reference`,
`test-area01-glow-services-reference`, `test-area01-flame-services-reference`,
`test-camera-live-reference`, `test-camera-interaction-fixture`,
`test-script-host-workers-reference`, `test-player-closure-0e-18-reference`,
`test-player-closure-10-12-19-reference`, `test-area-script-reference` and
`test-area01-script-workers-reference` pass. Receipts in ignored
`build/frontier2_i9_20261020_122303/`.

`--run --until a00_10` now plays a00_05 (1,159 frames), a00_06 (1,374)
and a00_07 (1,201), aligns a00_08 at counter 39081 and fault-stops at
frame 12512: the area script host's op 0x0F requests movie selector 2,
which the port does not export (`movie request D_00821058=1 with selector
2 is not exported`, owner 00825920, the switch). Selector 2 is
`\MOVIE\E006S1.PSS` (the ELF's name table D_00264FB0[2]); the recording
plays it whole (a00_08 rows 356 -> 357: one main-loop step over 1,626
vblanks). Next unit: export it (`tools/export_movie.py`) and record its
field-clock facts on the fork (pictures, held pictures and the feed loop's
end after the gate, as `k_movies` holds for E900 / E001; GS_EXACT.md
"The end-of-movie stall").

**Every difference the C side shows from the arrival's end through a00_08's
movie is in four classes** (windowed against route_a00, owners included):
a00_00 242/363, a00_01 938/1273, a00_02 424/601, a00_03 92/144, a00_04
1,797/2,017, a00_05 655/1,160, a00_06 0/1,375, a00_07 0/1,202, a00_08
0/357 rows exact. Ignoring the padlock [54]'s slot observation
(`lock_r54`) every row of a00_00..a00_03 is exact and a00_04 differs only
in the shake rows 1546..1553 (the RNG drift below). Ignoring `lock_r54`
and `progress` as well, a00_05..a00_08 (to the movie) are exact row for
row. The classes:

1. `lock_r54`: the observation of the padlock [54]'s pool slot (from
   a00_00 row 0; after the break, a00_05 rows 91..252, the original
   allocates a class-2 record there that the native allocates elsewhere).
2. The RNG drift (a00_04 rows 1546..1553; below).
3. **A taken bit the original never sets** (new finding): from a00_05
   row 815 (native counter 36158) the native's D_00810860 row 0 byte 12
   bit 5 (taken id 0x65) is set, so `progress` differs in every later row;
   the original sets no taken bit in AREA00 before a00_09 row 152. The
   setters reached in AREA00 are the class-2 creature brains' state 3
   (00128C10 / 0012A5D0: 001B1190(+0x9A) then 001AFC10): a native creature
   dies that the original's does not (likely downstream of the RNG drift;
   its record is not in the observed owner list, so which one is open).
4. The arrival's rows 0..80 and 3750..3754 (below).

**2026-10-20, iteration 8 (port main 6798a81 plus the a00_04 script
commands): the C side plays a00_04 to its end; the stop moved to a00_05.**
Not a capture pass (windowed counts below). The ferry owner `00825600`'s
scripts reached two script commands the area script host did not run, in
this order:

- **op 0x03 (`001B8AB0`, `ftab_0024D880[3]`; NEARMISS decomp C, src/func_001B8AB0.c).**
  The fault "001BA1F0 of 00825600 faulted at 0024D88C (record 00829DE0)"
  was the dispatch read of `ftab_0024D880[3]`: em_area_script.c had no
  case for it. The command is the camera shake: it saves D_008105D0 /
  D_008105E0 at rec +0x20 / +0x30, then for rec +0x0C counts adds a random
  offset (by the mode word rec +0x08 and the word rec +0x14, drawn with
  00122BB8) turned by D_00810650 on every odd count, and restores them.
  Its translation `em_area00_low_001B8AB0` already existed (checked against
  the original instructions by `make test-area00-low-reference`, 16/16
  conditional branches). It now runs as the other original-handler owners
  do: `execute` hands op 0x03 to the area binding's handler, the AREA01 /
  AREA00 binding (`em_area01_script_live.c` `record`) admits `001B8AB0`,
  and `em_area01_script_workers.c` runs the translation over rec
  +0x08..+0x17, the phase byte (block +4) and the scratch quadword
  0x70003600, its quadword copies, 001281C0, 00122BB8 (the live
  `em_random_next`), 001026A0 and 001028B8 going through the host's
  worker. Without a handler (AREA11) op 0x03 still fails stop.
- **op 0x0D sub 1 (`001B0460(1)` from `001B7B30`; byte-matched decomp C,
  src/func_001B0460.c).** Then "001BA1F0 of 00825600 faulted at 001B0460
  (record 0082A020)": the script host's `w_001B0460` was NULL. It is now
  bound (`em_area11_script_host.c`) to the camera re-seat the room move
  already runs, `em_camera_live_001B0460` (its translation
  `em_script_host_001B0460`, `make test-script-host-workers-reference`),
  through the new `em_scene_bindings_001B0460`: the room camera record of
  D_0024D650 from the spawn table window, 001B0250 on the canonical
  D_008106C8, and the player's D_00810350 (+0xA0, w +0xAC), D_00810370
  (+0xC0 with the heading) and D_008104E0 (+0x230) from the live record,
  published before the call and reloaded after it.

Proof. `make test-area01-script-workers-reference` now also runs 225
op 0x03 cases per capture (every third in quick mode) (modes 0, 1, 2, 3, 0xFF; phases 0..2; rec +0x14
0 / 7 / 0x40; timers 0, 1, 78, 79, 80 against 80) through the binding
(`em_area01_script_worker_call`) against the original instructions,
comparing every worker boundary (callee, arguments, stack pointer and the
compared memory, now including 0x70003600..0x7000361F and the rand state
word) and the result: quick 306 cases / 564 boundaries, `EM_TEST_FULL=1`
4,736 cases / 8,576 boundaries. Scratch negative controls each fail it:
mode 1's draw `& 7) - 4` changed to `- 3` in the translation, the scratch
region cut to 8 bytes, and the result inverted. The re-seat is checked by
the recording: the camera jumps at a00_04 row 1984 (eye (66.8, 11.7,
-1538.7) to (255, -24, -1694)) and rows 1984..2016 are exact, camera
included. The first-level smokes' tick, RNG and present logs equal port
main 6798a81's (sha256 2866e0de... / f8a37cb8... full, d2cb9bed... /
2fa87dc2... drive switch); newgame-control 9.599849;
`tools/test_level_smoke_area01.py --until a01_07` passes 28 live phases;
`make test-area-script`, `test-area-script-reference`,
`test-area01-script-host-reference`, `test-script-host-workers-reference`,
`test-area00-low-reference` and `tools/test_area00_script_binding_reference.py`
pass. The AREA00 run's tick log equals the previous build's up to the
faulting tick. Receipts in ignored `build/frontier2_i8_20261020_083957/`.

`--run --until a00_10` now plays a00_04 (2,016 frames), aligns a00_05 at
counter 35343 and fault-stops at frame 9075: "owner 00128C10 node 007AC5E0
failed at 001288D0", an unbound worker of the live composition (next
unit; `em_area00_low_001288D0` is its checked translation, and it calls
001F4A00, the tail call to 001F4BF0). Windowed (owners included): a00_04
1,797 of 2,017 rows exact (differing ranges 0..59, 75..112, 153..190,
444..481, 484..521, the carried [54] slot / lock_r54 class; and
1546..1553, the shake); a00_05 498 of 658 rows reached exact.

**The native RNG stream runs behind the original's (found 2026-10-20).**
The shake rows 1546..1553 differ only in eye / target (by up to about 0.9),
the rows before and after are exact: the offsets come from 00122BB8's
draws. At each fork beat end the original's rand state (the word +0x58 of
the block D_0024295C names, in the beat's eeMemory.bin) is the native
state at the same counter advanced by N draws of the LCG: a01_00 4,
a01_01 -1 (native one ahead), a01_02 12, a01_03 18, a01_04 27, a01_05 34,
the arrival's end 94, a00_00 79, a00_01 57, a00_02 129, a00_03 198 (the
native draws about 26..42 per frame there). The first values are within a
frame (the snapshot's place in the frame); from a01_02 on the native makes
fewer draws than the original, and the AREA01 rows do not observe them.
Finding which draws are missing (a consumer the port does not run, or runs
fewer times) is a fidelity unit before any phase that shows a random value
can pass; record the original's per-frame draw count on the fork around
a01_02 to locate it.

**2026-10-20, later (port main c4aa4a5 plus the knife's reach in a00_03):
the C side runs a00_03 without a fault; the stop moved to a00_04.** That is
not a capture pass: windowed against route_a00 with `compare_route_row`,
a00_03 has 92 of 144 rows exact (measured below). a00_03 (break the cage
padlock) reached three things on the knife's path, in this order:

- `001AA840` (NEARMISS decomp C) refreshes the aim / fire composition,
  whose host view list (1,024 entries, seven arrays on the C stack) was
  full. The list is host bookkeeping, not an original table: it is now
  `EM_AIM_FIRE_WORLD_VIEWS` = 256 fixed views + 256 pool records x 104
  (the enumerate's most for one record: an effect record's 40 node
  fields, 57 trail, 6 burst views and its +0x14 word; a `_Static_assert`
  in em_aim_fire_runtime.c ties the bound to those arrays), on the heap,
  one set per nesting depth of `em_aim_fire_world_live_call` and kept, so
  a nested call never touches its caller's views.
- `00189EC0` (NEARMISS) type 0x0E (the padlock [54], callback 001581A0):
  its facing test `001000C0(00128350(t), pi/2)` faulted in the aim / fire
  binding, which had no leaf for either. `00128350` (byte-matched eegcc
  C, libgcc __extendsfdf2) and `001000C0` (byte-matched C: 1 when
  `001274B0` of its two double words is negative) are now bound to
  `em_sdk_soft_float_00128350` / `em_sdk_soft_float_001274B0`, their
  existing translations (`make test-sdk-soft-float-reference`: 27,324
  cases equal to the original instructions). The binding's case dispatch
  is `em_aim_fire_soft_float_call` (src/game/em_aim_fire_soft_float.c):
  f12 in, the 64-bit v0 out for 00128350, a0 / a1 passed through as two
  64-bit double words for 001000C0. `make
  test-aim-fire-soft-float-reference` runs that dispatch, through the
  binding's EmAimFireTargetCall, against the original instructions of
  both functions and of the chain 001000C0(00128350(t), 0x3FF921FB60000000)
  (t below, at and above pi/2: 1, 0, 0; zeros, infinities, NaNs, random
  words and double pairs). Its negative controls (the comparison flipped,
  a0 / a1 swapped, a0 truncated to 32 bits) each fail it.
- The melee hit frees the padlock (001AFC10, byte-matched), and the same
  frame's `001AA840` walks the class-4 list published before the free, so
  it reads the freed record's +0, +2 and +3. The original memory then
  holds what 001AFC10 left (the header words +0x00..+0x0F cleared; the
  entry's class byte reads 0, so 001AA7A0 is not called). For a published
  class-4 entry that is no longer allocated, the composition's effect
  owners (the `EmArea00Fx` path, which runs 001AA840) resolve memory
  through one provider, `owner_view` in em_aim_fire_world_live.c: reads
  inside exactly those 16 bytes of the native record (which
  `em_actor_pool_free_001AFC10` clears the same way) succeed, a write to
  them or an access reaching past them gets nothing (a fault), and every
  other address resolves through the ordinary views as before. The other
  region-only owners (hit, side, math, misc) never see a freed header.
  `em_aim_fire_world_live_owner_view` exposes the innermost running
  call's resolution (for tests). Found with a temporary local diagnostic
  (record 102 = 0x7B81E0, class 0x44, type 0x0E, freed before the walk),
  removed.

Proof: `make test-aim-fire-world-live` (175 checks). Inside a 001AA840
call, a test 001AA7A0 worker probes the freed record: its 16 bytes read as
the cleared header, +0x10 and accesses reaching past either end fault,
and every write to +0x00..+0x0F faults; the worker then re-enters the
composition with other views for the same address, and afterwards the
outer call still resolves its own views and its walk reaches the second
allocated entry. A freed entry alone is walked without 001AA7A0 and is not
a general map; an allocated one without a view still faults. Negative
controls in scratch copies each fail it: the freed header widened to 0x40
bytes, made writable, and every nesting depth sharing one view set. The
change to a provider leaves the AREA00 run's tick and RNG logs
byte-identical to the previous round's build (sha256 ed83ac95...).
`--run --until a00_10` (drive replay) now runs a00_00 (362 frames),
a00_01 (1,272), a00_02 (600) and a00_03 (143) on the C side without a
fault (in-process phase lines, not capture passes), aligns a00_04
at counter 33323 and fault-stops at frame 7943 in the script host:
`001BA1F0 of 00825600 faulted at 0024D88C (record 00829DE0)` (next unit).
The arrival's capture check is unchanged (first difference row 539, tick
25667, the clock). The first-level smokes' tick, RNG and present logs
equal port main c4aa4a5's (sha256 2866e0de... / f8a37cb8... full,
d2cb9bed... / 2fa87dc2... drive switch); newgame-control 9.599849;
`make test-level-smoke-full` (its eleven aim side runs drive this
composition) and `make test-level-smoke-ps2-drive` pass;
`tools/test_level_smoke_area01.py --until a01_07` passes 28 live phases;
the bridge regressions `test_area01_target_hit_reference.py`,
`test_area01_water_reference.py`, `test_area00_world_reference.py` and
`test_player_equipment_reference.py` and the aim / fire live tests pass.
Receipts in ignored `build/frontier2_i7_20261020_024700/` and
`build/frontier2_i7_fix_20261020/`.

Measured comparison of that run, each phase windowed from its aligned
counter against route_a00 with `compare_route_row` (owners included),
counting every row rather than stopping at the first difference:

| phase | rows exact | first differing row ranges | first differing field |
|---|---|---|---|
| a00_00 | 242 / 363 | 0..2, 51..71, 121..141, 146..166, 218..238, 286..306, 350..362 | lock_r54 (the +0x2DC byte toggle) |
| a00_01 | 938 / 1,273 | 0..7, 60..80, 151..159, 172..180, 282..299, 303..320, ... | lock_r54 (the same toggle) |
| a00_02 | 424 / 601 | 19..38, 40..59, 144..164, 194..214, 308..328, 347..367, 441..461, 516..531, 537..552 | lock_r54 (the same toggle) |
| a00_03 | 92 / 144 | 49..55, 70..76; 106..143 | lock_r54 (the toggle); from row 106 the post-break record |
| a00_04 | 1,334 / 1,546 compared (2,017 recorded) | 0..59, 75..112, 153..190, 444..481, 484..521 | lock_r54 = the post-break record (z) |

Only the first differing rows of each phase were inspected field by field;
the later ranges are counted, not classified.

a00_03 row 106 (tick 32027) is the first divergence after the break: the
record re-allocated in [54]'s slot (header `02000c00 01...`, callback
0x1EA240) has z -1687.31641 where the recording has -1687.104, and its
+0x1F0 words read `ee5d880f ee5d880f` where the recording has `a6db8e6d
a6db8e6d` (the next two words agree). This is an open fidelity unit,
next to the a00_04 script-host stop (see "Remaining differences").

**2026-10-20 (port main e600fc5 plus the footstep handler): the C side
plays a00_00, a00_01 and a00_02; the stop moved to a00_03.** a00_00's
33rd frame reached the effect pass's handler `001EB980` (effect subtype
0x27, the footstep variant of entity 0x80000068; byte-matched decomp C,
`src/func_001EB980.c`): one 001CFB50 of the node's matrix with the work
block's +0x54 / +0x5C (accumulator, fraction) and 3.0, then 001CFBE0 mode 1
of `D_00255FB0` and of `D_00256040`. Its translation already existed and
was oracle-checked (`em_area00_hud_001EB980`, test_area00_hud_reference.py);
it is now wired into `em_effects_live.c` (`handler_001EB980`, the same view
pattern as 001ECB00: the pointer `D_00275C34` = node + 0x1F0 and the work
block's +0x244 / +0x24C), and its two source blocks are exported
(`tools/export_area00_runtime_tables.py` `FOOTSTEP_WINDOWS`, one 0x120-byte
window at `D_00255FB0`, equal to the pinned ELF in all 34 retained AREA00,
AREA01 and revisit captures; the export now holds 59 of the loader's 64
windows). Oracle: `make test-area00-footstep-reference`
(`tools/test_area00_footstep_reference.py`) executes the original
001EB980 with every transform and packet callee over the retained AREA00
captures' effect nodes (four accumulator / fraction variants, two depths)
and compares the production adapter's whole RAM and scratchpad (quick 48
cases, EM_TEST_FULL=1 288 over 12 captures), plus two refusals; swapping
the two emits fails it. No retained capture holds a subtype-0x27 node, so
the cases are synthetic work values over real matrices, not a recorded
footstep.

Result: `--run --until a00_10` (drive replay) plays a00_00 (362 frames),
a00_01 (1,272) and a00_02 (600) on the C side, aligns a00_03 at counter
33179 and fault-stops about 100 frames in at `001AA840`: `aim/fire runtime:
view list full at 007BD15E` / `aim/fire world: enumerate failed after 994`
/ `001AA840 refresh failed (30 views)`. The aim / fire world's host view
list (`EM_AIM_FIRE_WORLD_VIEWS` = 1,024 in em_aim_fire_world_live.h; the
Bridge's seven view arrays are on the C stack) is host bookkeeping, not an
original table: the next unit is to size it from the actor pool (the
enumerate adds at most 104 views for one effect record: 40 node fields, 57
trail, 6 burst and one +0x14 word) and move the arrays off the stack, with
proof the first level and AREA01 stay byte-identical. The arrival's 86
differing rows (below) are unchanged; no AREA00 phase is a capture pass
yet. Proof of this step: the first-level smokes' tick, RNG and present logs
equal port main e600fc5's (sha256 2866e0de... / f8a37cb8... full,
d2cb9bed... / 2fa87dc2... drive switch); newgame-control 9.599849; `make
test-level-smoke-full` and `make test-level-smoke-ps2-drive` pass;
`tools/test_level_smoke_area01.py --until a01_07` passes; the bridge
regressions `test_area01_target_hit_reference.py` and
`test_area01_water_reference.py` pass. Receipts in ignored
`build/frontier2_i6_20261020_002849/`.

**2026-10-19, night (port main 9b9c26b plus the arrival's script, bank,
voice and class-2 reads): the arrival runs to its end; the stop moved 33
frames into a00_00.** One change supplied what the arrival reached after
frame 203, in the order the run met it:

- `001B8020` op 0x0B (byte-matched C) subs 2 and 3 in `em_area_script.c`
  (the owner `00823580`'s records `008285E0` = sub 3, count 0x8C, and
  `00828660` = sub 2, mask 0x1000): st +0x0E = `001C64F0`(owner, rec
  +0x0C) through the new worker `w_001C64F0` (the area binding's
  `advance` hook, `em_area01_script_live.c`, to the live runtime's
  anim_advance_time); sub 3 sets rec +0x10 = (float) rec +0x14 and the
  phase byte to 1, then counts rec +0x10 down by 1.0 and advances at 0.0;
  sub 2 advances on a bit shared with rec +0x14; values outside 0..6
  advance without a call (sub 5 still faults). Oracle: `make
  test-area-script-reference` (the synthetic 'owner anim advance' script,
  12 lockstep ticks against the executed 001BA1F0 + 001B8020; quick 57
  scripts / 1,394 ticks, EM_TEST_FULL=1 6,068 ticks).
- `001C6120` (byte-matched C) over a bank other than Roger's library (the
  arrival owner `00824EA0`'s 001C2770 -> 001C39F0 -> 001C6160 with clip
  0x8006 over its bank `0113D1C0`): the live runtime now serves it with
  `em_pose_host_001C6120` (oracle-checked in
  `test-pose-host-workers-reference`) over the one directory word.
- The arrival's voices: `tools/export_streams.py` now also exports AREA00's
  `D_0026EC60` rows (music 45, 47) and every voice of its message table
  `D_00264DD0[1]` (19..28, 30, 31, 32); before, the first arrival voice
  read faulted at `00112610` (code 8). 14 extents, 25,603 sectors; the
  23,725 earlier sectors unchanged (`tools/test_area01_stream_export.py
  --previous`, every sector against the disc).
- `D_00242EB0..D_00242F20` (00128C10's halfword clip tables and
  00128600's byte rows, both byte-matched C): the class-2 owner
  `0012A5D0` -> `0012AFC0` -> `00128600(0)` read `00242EDB` at arrival
  frame 3956. Exported by `tools/export_area00_runtime_tables.py`
  (`CLASS2_WINDOWS`), equal to the ELF in all 52 retained captures
  (startup, AREA11 route, AREA01, AREA00, revisit); `make
  test-area00-arrival-bindings` serves it through the production loader.

The C side now plays the arrival's 4,017 rows and aligns a00_00 at counter
30942 (drive replay), then fault-stops 33 frames in at the untranslated
effect handler `001EB980` (`effects: untranslated effect handler at
001EB980`). The arrival's capture comparison is not a pass: 86 of its 4,017
rows differ, in two runs. Rows 0..80: the original's player runs an
arrival stance first (row 1: +0x1F0 = 1, byte 5 = 1, scratchpad 0x70003B8D
word 0x0400; row 2: clip 2, x -40.0 where the port has -40.5, camera
target and forward to match; row 3: +0x1F1 = 65), and its clock reads 0, 1,
8, 7 .. 1 before its 80 countdown starts at row 11, while the port's clock
counts down from 80 at row 0. Rows 3750..3754: the message phase (the
original's block goes 2 -> 0 at row 3751; the port's stays 1 until row
3754). Next AREA00 units: the arrival stance (read the original player
code the arrival's first rows run; a label is not evidence), the message
phase at row 3750, then `001EB980`. Proof of this step: the first-level
smokes' tick and present logs equal port main 9b9c26b's
(sha256 2866e0de... / f8a37cb8... full, d2cb9bed... / 2fa87dc2... drive
switch); newgame-control 9.599849; `make
test-level-smoke-full` and `make test-level-smoke-ps2-drive` pass;
`tools/test_level_smoke_area01.py --until a01_07` passes a01_arrival..a01_07
row for row. Receipts in ignored `build/frontier2_i5_20261019_205006/`.

**2026-10-19, evening (port main 0deae8b plus the surface program): the
arrival runs 203 frames; the stop moved to a script op.** 001E9280 (the
surface owner `0015AB00`'s packet, NEARMISS C in the decomp; its packet
bytes follow the C) CALLs the static program packet `D_00234FE0` (0x4B0
bytes with its RET), which is now exported from the pinned ELF into the
effect export (`tools/export_effect_tables.py` BLOCKS, installed through
`tools/export_area00_runtime_tables.py`; the window equals RAM in all 15
AREA11 route captures and the 13 AREA00 snapshots) and walked by the native
chain page (MPG of 142 instructions from ELF 0x00235010, FLG UNPACKs and
MSCNT after a batch of its own, count `mscal_surface`). Its VU1 program is
translated as `em_vu1_surface_program_mscal` (em_vu1_page_programs.h): the
batch code is the floor program's instruction for instruction; the vertex
is the floor vertex with the colour's alpha taken from the point's w (no
depth fade), the texture pair offset by VF20.x / VF20.y and no depth
scale. Oracle: `make test-area00-surface-vu-reference`
(`tools/test_area00_surface_vu_reference.py`) runs the original microcode
on the shared VU1 machine (tools/chain_page_model.py, which now knows the
packet) and compares every surface run of the retained snapshots through
the model and the native page (EM_TEST_FULL=1: 11 snapshots with surfaces,
88 runs, 528 batches and kicks, 4,020 primitives, every GIF byte), 1,500
synthetic MSCAL + MSCNT pairs on both arithmetic paths (all data memory and
registers; 14 cases where the original faults on an exponent-255 operand,
the native too; exponent-255 alphas pass through MOVE / MAX / MINI / FTOI
equal) and three fail-stops. Receipts in ignored
`build/frontier2_i4_20261019_184516/`.

The arrival now stops at frame 203: the AREA00 overlay owner `00823580`
(node `007B7C00`) runs its script through `001BA1F0`, and record
`008285E0` is op 0x0B with mode 3, which `em_area_script.c`'s `op0B`
(001B8020, byte-matched in the decomp) does not admit yet (it admits modes
0, 1, 4 and 6): `em_area11 script host: 001BA1F0 of 00823580 faulted at
001B8020 (record 008285E0)`. Next AREA00 unit: admit 001B8020's mode 3
(anim_advance_time, then the sub-phase byte +4; read the decomp C) with
its oracle cases, then rerun the arrival. Proof of this step: the
first-level smokes' tick, RNG and present logs are byte-identical to port
main 0deae8b's (EM_TEST_FULL main line and the drive switch to
a01_arrival); newgame-control 9.599849; `make test-level-smoke-full` and
`make test-level-smoke-ps2-drive` pass; the AREA00 harness (`--run --until
a00_00`, drive replay) passes a01_arrival..a01_07 again, and its tick
records before the old frame-1 stop equal the old build's.

**2026-10-19, later (port main ffd515c plus the arrival read views): frame
0 of the arrival completes; the stop moved to frame 1's chain page.** One
change supplied every read and worker the arrival's frames 0 and 1 reached
(each in the order the run met it):

- `0022DCD0` (`em_area01_exita`, NEARMISS C; oracle
  `tools/test_area01_exita_reference.py`, which covers its key-0 fill): its
  AREA00 record list `D_00269400` (six 64-byte records and the terminator
  word) and the 0x90-byte record `D_0026A900` that `001D0400` scales are
  exported from the pinned ELF into the effect export
  (`tools/export_area00_runtime_tables.py` `LIGHT_WINDOWS`, checked against
  the ELF and all 13 retained AREA00 snapshots); its tables live in the
  boot BSS `D_00822CF0..D_008234C0`, which no other decomp function names,
  now one process-lifetime block in `em_area01_live.c`; `001D0400`'s
  `00121870` block copy (assembly in the decomp) is a live worker with
  `em_static_world`'s piece order; `001CFB50` goes to
  `em_effects_live_001CFB50` over the stack block (its `001CFBE0` kinds 1
  and 6 were already live).
- `0015A2C0` (`em_area01_sys`, NEARMISS C): AREA00's floor fields use config
  rows 3 and 4 of `D_00248120` besides AREA01's 0, 1 and 5; rows 3 and 4 are
  exported (`FIELD_WINDOWS`; every retained owner names an exported row).
- `001F6BB0` (`em_effect_manager`, NEARMISS C): key 0 reads `D_0081075D`,
  canonical in the progress region since A01 (0 at the arrival, 0xFF from
  `a00_08`'s switch in the fork snapshots); `em_effects_live` refused key 0
  as if it had no owner. Key 0x1301 still refuses (no canonical copy of
  `D_00810778 / 77B / 79E`). Key 0's 0xFF path (`001F6760`, `001F66F0`) has
  no worker yet and fail-stops when `a00_08` reaches it.
- `0019AD00` (`em_coll_move_original`, NEARMISS C), called by `00129780`
  (00128C10's selectors 2 / 7): bound through `em_area01_collision_view`,
  which hands the walker the probe representation of the one scratchpad
  (`export_move`, the inverse of the existing `import_move`), marks the
  move scratch the last writer and writes bit 31's x / z back. Oracle:
  `tools/test_area01_collision_view_reference.py` now runs 120 original
  `0019AD00` calls (masks 2, 4, 6, 0x80000006) on the 15 AREA01 snapshots
  through the view and compares the scratchpad and the mover's position.
- `em_owner_draw_live`: `EM_OWNER_DRAW_LIVE_UNITS` 64 refused frame 1's
  draws; it is host bookkeeping (the original is bounded by the packet
  arena, which the module checks) and is now 512.

The arrival now stops at frame 1 in the chain page: `001E9280` (the
surface owner `0015AB00`'s packet, NEARMISS C) CALLs the program packet
`00234FE0`, which is neither exported nor translated (`chain page:
unmapped address fault at 00234FE0`). It uploads one MPG of 142
instructions (ELF 0x00235010; `tools/level_texture_page.py` already
replays it on the original microcode for the texture export) and runs six
batches like the floor program `D_002345E0`. Next AREA00 unit: export the
packet and translate that VU1 program into `em_vu1_page_programs.h` with
an original-microcode reference test like
`tools/test_level2_floor_vu_reference.py`. Proof of this step (receipts in
ignored `build/frontier2_i3_20261019_144851/`): against the build without
it (port main ffd515c, binary 2da192b5...), the first-level smokes' tick
and present logs are byte-identical and their RNG draws equal in frame and
value (EM_TEST_FULL main line 273,855 draws; the drive switch 274,032);
`make test-level-smoke-full` and `make test-level-smoke-ps2-drive` pass;
newgame-control 9.599849; the AREA00 harness run (`--run --until a00_00`,
drive replay) passes a01_arrival..a01_07 again and its first 25,669 tick
records equal the old build's, up to the record where the old build
fault-stopped (counter 26926). `make test-area00-arrival-bindings` checks
the added windows (30,836 slices, 8 refusals, 6 corruptions refused);
`make test-area01-collision-view-reference` the 0019AD00 binding.

**2026-10-19 (port main 479f387 plus the owner-view raise): AREA00 does
not play yet; the stop moved.** `em_area11_boxes.c`'s owner-view table now
holds `BOX_MAX = EM_ACTOR_POOL_CAPACITY` (256) views, one per record of the
original's actor pool, instead of 32 (the original arrival holds 26 crates
and 13 drums, at least 39 views; "Owner-view capacity (resolved)" below).
Ordinary New Game still reaches the arrival alignment (counter 26925 with
the drive-timing switch) and now passes the crate and drum callbacks; it
fault-stops at arrival frame 0 in the `[39]` owner `0022DCD0`
(`em_area01_exita`, NEARMISS C in the decomp), which reads an unexported
runtime address `00269400` (`em_area01: owner 0022DCD0 node 007B55D0
failed at 00269400`; ignored `build/frontier2_i1_20261019_065540/a00-arrival-new/`).
That read view is the next AREA00 unit. Zero AREA00 rows are certified: arrival, a00_00..a00_10 and the
side return a00_s0 are all open, and so is the AREA01 revisit that follows
a00_10 (AREA01_REVISIT.md). The harness itself passes its contract checks
(`tools/test_level_smoke_area00.py --verify-harness`: 13 C branch paths,
2,731 input commands, 16,504 capture-shaped rows, 179 rejected corruptions;
`build/merge-1019-post/area00-harness.log`), which is no gameplay claim.
The AREA01 prerequisite passes since the user's decision of 2026-10-19
("Comparison-only replay"): with the drive replay `EM_PS2_DRIVE_REPLAY`
a01_00..a01_07 pass row for row through the AREA00 arrival state 0
(LEVEL2_BINDING.md "State (2026-10-19, the drive replay)").
`tools/test_level_smoke_area00.py --run` now runs with the replay by
default (tables for a00_00..a00_10 and a00_s0 recorded; IOP_STREAM.md
"Drive replay"); its run to a00_00 passes every AREA01 phase and stops, as
before, at the arrival frame 0 in 0022DCD0 at 00269400 (aligned counter
26925; ignored `build/frontier2_i2_20261019_092911/a00_00_replay.out`).
The four unsampled AREA00 ticks (below) are lead-in frames of the fork
re-drives; the drive tables sample them, but their route rows still need
fork boundary receipts.

2026-10-18 asset follow-up: original page replay found an additional
`001E9E60` surface texture. Both canonical AREA00 catalogs now hold 580 fixed
keys, checked against every selected GS freeze and uploaded by the native
resource-loader fixture. The explicitly authorized graphics change raises
the registry to 1024 with a hash index. All four default/GPU smokes pass
with byte-identical ticks, RNG and presented-frame logs; control is 9.599849.
Metal-drawn (`gpu`) frames carry no pixel hash, so they are compared by
tick, RNG and frame order; the registry's pixel behaviour is proven by the
Metal fixture `tests/level_texture_registry_test.m` (LEVEL_ASSET_DELIVERY.md). This does not certify an AREA00 frame. The subsequent
surface and retained-library delivery work is described below. Full asset receipts and retained runtime-material limits are in
[LEVEL_ASSET_DELIVERY.md](LEVEL_ASSET_DELIVERY.md).

The stage-5 continuation adds the 40-byte `D_00248290` read view through
the existing effect pack, preserving its 50 earlier windows. The full
`0015AB00` oracle passes 160 cases and 30,000 worker calls. The unchanged
production read provider passes 820 exact subrange checks and five bounds
refusals under ASan/UBSan. All 13 retained fork snapshots match the table.

The next native fault was model `0x4F` at `00C7CBC0`, called by
`0015AFA0` through `001B1020`. AREA00's roster requires five library
models, of which `4F/59` were missing from the retained export. Extending
two existing read-only spans supplies both while preserving all earlier
bytes, flags and the 16-region limit. All five models match 13 snapshots;
55 original-instruction binding cases pass over the 11 AREA00-resident
snapshots. No native game source changed for either addition.
See [AREA00_ASSETS.md](AREA00_ASSETS.md) for reproduction and exact scope.

The following native attempt passed those resource reads and stopped at
the crate registry's AREA00 selector `0024A850` inside `001551B0`. Adding
that halfword and `0024D820`'s pointer to the same export supplies the
existing lazy resolver; group data remains loader-owned. The full crate
oracle passes 86 ticks and 442 worker calls, with both lazy access and
sticky missing-root faults checked. These additions still change no
native game source and do not promote an AREA00 phase by themselves.

With all three additions, ordinary New Game reaches arrival alignment at
counter 26925 with the drive-timing switch (`EM_PS2_DISC_DRIVE_TIMING=1`,
the documented command) or 26634 at host speed
(`build/levels/area00-arrival-hostspeed-review-1/`), then fails at arrival frame 0 in drum callback `00156620`:
the protected `em_area11_boxes.c` owner table has 32 slots. The original
arrival has 26 crates and 13 drums, so those callbacks alone require 39
views. That was the stop until 2026-10-19, when main raised the table (the
2026-10-13 branch's ownership rule had kept this AREA11 file unchanged);
see "Owner-view capacity (resolved 2026-10-19)". The 512-texture refusal
below is historical.

2026-10-18 standalone follow-up on branch `levels`: four missing AREA01 routines
on the AREA00 return (`00823A90`, `00823CD0`, `008240E0`, `00826010`) now have an
unwired translation in `em_area00_return.c`. Its original-instruction test
passes 521 cases and all 534 reachable non-branch words, including 30 captured
actor cases and the full fail-stop sweep. This is no AREA00 phase promotion.
See [LEVEL_ORACLE_AUDIT.md](LEVEL_ORACLE_AUDIT.md) for exact scope and receipts.

The historical capacity diagnostic's native scene selection reaches AREA00 after ordinary New Game, AREA11
and AREA01. The original load completes and selects AREA00 collision and
scene resources. At counter 26922 the existing shaft phase observes
AREA00 state 0, before rebuild. The following page-render request faults
at `001CB800` because the catalog cannot fit. This replaces the previous
`001FF080` “area 00 room 0 is not exported” stop; it is not a gameplay pass.

The current composition selects AREA00's original initializer, borrowed
loader data/BSS, roster, callbacks, scripts, messages, collision, static
bank and per-subarea resources. AREA01 wrappers keep their existing area-1
behavior. The shared owner remains authoritative for each global, actor,
player, weapon, model and collision span. Unknown workers still fault.

Completed prerequisites now include both verified 580-key texture catalogs,
shadow banks (sub 0: 1,100 objects / 1,802 blocks; sub 1: 1,099 / 1,783),
the full module deliveries and canonical SFX scopes for subareas 0/1.
The complete sound registry preserves the earlier entries and samples;
unsupported original sound cases remain explicitly unsupported.
The scene logger now emits the strict AREA00 observations from the
canonical owners. No captured state is installed in a native run.

[THIRD_LEVEL_CENSUS.md](THIRD_LEVEL_CENSUS.md) retains all 141 delta entries
and 19,012 instructions. None is promoted to AREA00-route live: the
required native row comparisons have not passed.

## Binding ledger

| Boundary | Existing owner / required binding | State |
|---|---|---|
| AREA01 shaft exit / module read | `em_scene_task`, `em_module_loader`, full AREA00 sub-0/1 pack entries | Native sub-0 delivery and scene selection reached; sub-1 sectors independently checked |
| Texture delivery | `em_world_textures_live`, the single graphics registry | Both selectors wired; corrected 580-key catalogs pass resource upload at capacity 1024; required default/GPU registry regressions pass |
| Shadow receivers | `em_shadow_live`, `em_shadow_original` | Both selectors wired and checked, with transactional failure cases |
| Collision and static bank | `em_collision_world`, loader resource `0028A5A0`, `em_render_context_live` | AREA00 paths selected; static bank remains loader-owned |
| Overlay initialization | existing `001E7780` plus `em_area00_state_00824E00` | Selected initializer, six common resets and both record clears verified against original instructions |
| Overlay data/BSS | AREA00 MWo3 id 1, data `826F80/6500`, BSS `82D480/295900` | Checked borrowed loader views; the existing six boot globals remain the sole storage |
| Progress | `EmProgress`, `em_scene_progress_at` | `75B/75C`, `7DA/7DB/7DC` and AREA00 lock byte `841` admitted with original writer evidence; no new storage/reset |
| Placed roster / actors | `em_actor_roster_spawn_001B6990`, canonical actor/model/collision adapters | Per-subarea placements/deferred records selected; no separate pool |
| Overlay behavior | `em_area00_overlay` | All 13 prepared entry adapters selected before AREA01 address dispatch; unknown overlay entries fault |
| Scripts | `em_area_script`, existing script host | AREA00 window `8284E0..82BB50`, mutable loader data and area-specific callbacks selected; arrival `824E40` checked against original instructions |
| Messages | `em_message_live` | AREA00 EMMD selected during rebuild; canonical request/presenter state retained |
| Sound | existing stream/SFX owners | Complete AREA00 sub-0/1 registry scopes; 3,278 combined entries / 234 samples; no unsupported-case bypass |
| Surface initialization | existing effect-table provider and `em_area01_exitb` | `D_00248290` added from pinned ELF; full initialization/packet-state oracle and production read bounds pass |
| Pickup models | existing retained library provider and shared model adapter | Five roster models delivered exactly within 16 regions; production provider and 55 original binding cases pass |
| Crate registry | existing box owner, effect windows and module loader | Six-byte AREA00 roots exported exactly; production lazy resolver passes 86 original crate ticks |
| Ferry / cab / shaft workers | existing roster, WORLD, FX and effect services | Direct `001B6660`, `0019C6F0`, `001B0C60`, marker/depth/transform adapters installed; standalone comparisons pass; native phases remain uncertified |
| Player and camera | existing player closures and camera owners | Canonical selected-area views installed; no AREA00 native row pass |
| Return / bridge | AREA01 overlay plus revisit translations | Main and plain side returns remain separate strict phases; neither reached |

Common `001E7780` clears its six globals, calls the selected initializer,
then clears `+54/+58` for each initialized record with stride `A060`.
The AREA00 initializer borrows those existing globals and the loader's
memory. It never clears a replacement BSS image or owns a duplicate reset.
The optional AREA00 read/write view preserves canonical permissions;
a refused write cannot fall back to a mutable legacy byte view.

## Phases and comparison results

| Requested phase | New smoke scope | Native comparison result |
|---|---|---|
| 1. Arrival / load / bindings | AREA01 exit boundary, then `a00_arrival` | Not passed: alignment at counter 26925 (drive timing) / 26634 (host speed), then (since the owner-view raise, 2026-10-19) a frame-0 fault in `0022DCD0` at the unexported read `00269400`; before it, the `00156620` fault of the 32-view table |
| 2. First world and neutral frames | Fork a01_07 f539..4555, including final 60 neutral rows | 0/4,017 rows certified; alignment reached, but the first owner walk still fails |
| 3. Movement / collision | `a00_00..a00_10` | Not reached |
| 4. Camera | Per-row eye/target/forward/flags plus whole endpoint camera RAM | Checker contract passes; native AREA00 camera not reached |
| 5. Drawing | Future original packet and displayed-field comparisons | Not implemented/certified; exact exported texels are not a pixel comparison |
| 6. Interactions | `a00_01` locked door, `03/04` padlock/terminal, `05` ferry, `06` cab, `07` stack/crate tops/duct, `08` switch, `09` barricades/door | Not reached |
| 7. Return to AREA01 | `a00_10` progression return; `a00_s0` plain side return | Not reached; separate loader/state/progress checks prepared |

Original route correction: `a00_s0` branches from the initial AREA00
arrival and returns without the progression flag. It is not a continuation
after `a00_10`. The main progression return is `a00_10`, with
`D_0081075E=FF`; `a00_s0` ends with that flag 0. The original endpoint
snapshot proves the lowered bridge after the main return; a full native
bridge/revisit sequence remains separate verification work.

## Smoke contracts and limits

The default `test_level_smoke_area00.py` is a short **harness contract
test**, not a game run. `--run` explicitly requests the long replay from
ordinary New Game. Pads contain only input and reference timing metadata;
no captured game state is installed into the native run. `--prepare`
exports pads and does not certify anything.

The C phases reuse the existing strict EMA1 reader. AREA01 still ends at
its existing boundary when selected alone. A level-3 continuation keeps
that exact boundary check, then checks the following actual scene tick.
Return phases detect actual AREA01 state 0. Only repeated loader waits
may be shortened; no gameplay tick may be dropped or realigned. The
existing original loader/veil oracle is reused on return runs. Recorded
pad corrections preserve the late first commands and the one-frame
dropout in `a00_04`; their source metadata is checked.

The scene logger supplies `a00` observations from canonical
owners: `hp` bits; progress windows `8107D8/64`, `810758/8`, `810860/64`,
`810D00/32`; locks `810840/8`; weapon bytes `810C60/8`; and the ten
owner records named in `level_smoke_area00.OWNERS` (header, position,
`+1F0`, `+2DC`, callback). `C61/C62` come from `em_weapon`, not the
progress region, which correctly refuses those separately owned bytes. The new checker rejects absent observations.
The arrival recording has only AREA01 owner samples, so AREA00 owner
comparison is explicitly absent during `a00_arrival`; gameplay phases
require all ten samples. Existing per-row and whole endpoint camera
observations are reused. An eventual row pass still does not certify
rendered pixels, sound, unrecorded owner fields or off-route behavior.

AREA01 currently has a known prerequisite difference at a01_03 f957
(voice ends one tick early with drive timing; LEVEL_SMOKE.md). The new
runner does not waive it. Even after opening AREA00, the complete
New-Game-to-AREA00 comparison must fix this difference at its original
owner or continue to report failure. The continuation's saved replay
confirms it with the existing strict prefix diagnostic: 957 of 992 rows
match, then message state is `(2, 2, 64)` instead of `(2, 1, 64)` at row
957 (`build/level3/area01-prefix-live.log`). This diagnostic is not a
complete phase pass.

## Historical capacity request and remaining main prerequisites

The following records the 2026-10-13 refusal. The user authorized this
capacity/index exception on 2026-10-18; its implementation and passing
regression proof are described above and in LEVEL_ASSET_DELIVERY.md.

**Historical texture registry refusal, confirmed by the production catalog parser.**
`src/em_gfx.h` then defined `EM_GFX_OBJECT_TEX_MAX=512`; the protected Metal
registry enforced that limit. Each AREA00 export then contained 579
distinct keys, following AREA01's eager whole-bank/shared-resource method.
`em_world_textures_live.c::catalog` rejects the header before uploading
anything. `test_area00_resources_reference.py` reproduces that refusal
using the production parser and records zero registry mutations. The
actual input-only replay now reaches the same refusal: after AREA00
selection, `chain page` reports no page textures and the scene faults at
`001CB800`, arrival frame 0. See `build/level3/live-arrival-1/run.log` and
its binary-hash receipt; no arrival alignment is reached.

Requested change: raise `EM_GFX_OBJECT_TEX_MAX` to **1024**, audit every
array/count consumer (including Metal), then run the existing registry,
GS and pixel regressions and this resource fixture. No change to original
game behavior, sampled texels or assertions is requested. The earlier `level3` branch
did not edit that header or any renderer file; the present `levels` exception
is limited to capacity and lookup.

This is a capacity requirement of the existing **eager export policy**,
not a proof that 579 textures are simultaneously needed by gameplay.
Removing only world models absent from the endpoint snapshots still
leaves 527 keys; equipment/page sources contain additional eager keys.
A smaller catalog would require a complete binding/reachability proof
(including intermediate frames and side actions), not silent truncation
to fit 512. The source audit found 505 non-equipment keys; valid equipment
variants can add nine or twelve more, and queued draws retain their own
model references. Thus choosing the actor's current equipment alone is
neither a complete capacity solution nor a proven lifetime policy. No
catalog pruning or residency redesign is claimed here.

No additional protected VU program gap was established. Current page
support includes the known sprite/streak/kind-2/kind-6/floor/ripple paths.
Later channel-9 workers reached in `a00_09` remain an integration audit
item, not a substantiated request to edit a protected file.

## Owner-view capacity (resolved 2026-10-19)

Applied on port main (iteration frontier2 i1): `BOX_MAX = EM_ACTOR_POOL_CAPACITY`
in `src/game/em_area11_boxes.c`, so the host view table can never refuse an
owner the original's 256-slot pool holds. The crate and drum callbacks
(`001551B0`, `00156620`, both NEARMISS C in the decomp) are unchanged; the
table is host bookkeeping. Slots are taken first-free in index order and
reused by the freed / generation checks, so every owner the first level and
AREA01 bind lands in the same slot as before (the fixture asserts slot n
for the n-th owner). Proof (ignored `build/frontier2_i1_20261019_065540/`):
`tools/test_area00_owner_capacity.py --require-supported --out ...` passes
(bound 256, 39 needed, ASan/UBSan refusal and reuse); newgame-control
9.599849; the AREA01 replay through a01_02 passes with its tick log
byte-identical to the 789b4b4 receipt; the drive-switch New Game replay to
the AREA00 arrival has every tick record byte-identical to the unchanged
binary's (25,669 of 25,670 rows; the last is the faulting arrival frame
itself, which now runs further) and identical RNG values; the full and
drive-switch level smokes pass (receipts in the same folder).

The original request follows for the record.

**Raise the protected shared owner-view capacity.** In
`src/game/em_area11_boxes.c`, `BOX_MAX=32` limits both the `Box` views and
draw-order indices. `box_for` refuses the 33rd simultaneously held view.
This is host bookkeeping; the original actor pool still has 256 slots.
The AREA00 arrival and nine subsequent resident captures contain 26 crates
and 13 drums (39 views before counting doors and other world owners).
The last resident capture retains 32 crates/drums. The live failure is
`more AREA11 boxes than slots`, from `00156620`, at arrival frame 0.

Proposed diff for main (applied 2026-10-19, above):

```diff
--- a/src/game/em_area11_boxes.c
+++ b/src/game/em_area11_boxes.c
@@
-    /* four crates, two drums, the truck and the door, and the other world
-     * owners bound through em_area11_boxes_owner_* (the terminal, the panel,
-     * the prop 001C4820, the item owners; room for the rest) */
-    BOX_MAX = 32,
+    /* At most one host view per live canonical actor slot. Generation and
+     * freed-slot checks retain the existing ownership and reuse rules. */
+    BOX_MAX = EM_ACTOR_POOL_CAPACITY,
```

`tools/test_area00_owner_capacity.py` compiles the unchanged protected
provider under ASan/UBSan. It confirms 32 successful views, refusal of the
33rd, and correct freed/generation reuse. Its independent capture census
confirms the 39-view minimum. `--require-supported` correctly fails with
the present capacity; its default diagnostic mode reports the mismatch.
The fixture exercises the configured bound and does not lock in 32 as
correct game behavior. Main should require acceptance through the
canonical pool capacity, then exercise full/reused draw-order
storage, default/GPU AREA11 and AREA01 `--until a01_02` exact logs and pixels,
control 9.599849, and this same AREA00 arrival replay. Neither increasing
the actor/bone pool nor reassigning or omitting owners is requested.
Private evidence: `build/levels/area00-owner-capacity/receipt.json` and
`build/levels/area00-arrival-crate-1/`.

**Keep the AREA01 voice prerequisite explicit.** The existing a01_03
row-957 discrepancy remains a strict failure. Its audio/movie/frame owner
is outside this worktree's authorization. No checker assertion or replay
row has been dropped or realigned to bypass it.

## Verification and reproduction

Run on native arm64 macOS in the port checkout (the receipts below were made
in the `level3` worktree and stayed there). Heavy jobs are
serialized at nice priority 15 using the approved host path. Two initial
quick state/runtime attempts printed a sandbox `setpriority` warning and
continued; the reported full tests below supersede them at actual nice 15.
Do not treat `nice`'s warning as successful priority control.

```sh
# in the port checkout
nice -n 15 make -j4
# Generated outputs must be private, never written through checkout symlinks.
nice -n 15 python3 tools/export_sfx_registry.py
nice -n 15 python3 tools/export_module_loader.py
# Full table export goes to private scratch; installed root/table delivery is tested below.
nice -n 15 python3 tools/export_area00_tables.py --out build/level3/area00-tables
nice -n 15 python3 tools/test_area00_delivery_reference.py
nice -n 15 python3 tools/test_area00_assets_reference.py
nice -n 15 python3 tools/test_area00_resources_reference.py
nice -n 15 env EM_TEST_FULL=1 python3 tools/test_area00_state_reference.py
nice -n 15 python3 tools/test_area00_runtime_reference.py
nice -n 15 python3 tools/test_area00_effects_binding_reference.py
nice -n 15 python3 tools/test_area00_script_binding_reference.py
nice -n 15 python3 tools/test_area00_spawn_binding_reference.py
nice -n 15 python3 tools/test_area01_state_reference.py
nice -n 15 python3 tools/test_area01_runtime_reference.py
nice -n 15 python3 tools/test_area01_effects_services_reference.py
nice -n 15 env EM_TEST_FULL=1 python3 tools/test_area00_world_reference.py
nice -n 15 python3 tools/test_area01_door_live_reference.py
nice -n 15 python3 tools/test_area01_sfx_registry.py
nice -n 15 python3 tools/test_world_textures_reference.py
nice -n 15 python3 tools/test_scene_no_shadow.py
nice -n 15 env EM_HEADLESS=1 EM_STARTUP_TEST=newgame-control build/extermination
# Long diagnostic: the arrival plays out (86 of 4,017 rows differ, LEVEL3_BINDING.md), then a00_00 stops 33 frames in at effect handler 001EB980.
nice -n 15 env EM_PS2_DISC_DRIVE_TIMING=1 python3 tools/test_level_smoke_area00.py --run --until a00_arrival --out build/level3/live-arrival-1
# Later full-route and independent side-return checks:
nice -n 15 env EM_PS2_DISC_DRIVE_TIMING=1 python3 tools/test_level_smoke_area00.py --run --until a00_10 --out build/level3/progression
nice -n 15 env EM_PS2_DISC_DRIVE_TIMING=1 python3 tools/test_level_smoke_area00.py --run --side a00_s0 --out build/level3/side-return
python3 tools/check_no_disassembly.py --staged
# Build each staged topic in isolation before committing; remove scratch afterward.
git checkout-index -a --prefix="$PWD/build/level3/index/"
(cd build/level3/index && nice -n 15 make -j4)
```

Continuation results of the 2026-10-13 `level3` branch (historical; ignored receipts under `build/level3/`):

| Check | Result | Receipt |
|---|---|---|
| AREA00 selected initializer/common dispatcher, full | PASS: 10 captures, 120 cases, 328,276,800 compared bytes; ordered stores, aliases, lifetime, bounds/faults | `state-dispatch-full.log` |
| AREA00 runtime overlay selection | PASS: 16 original paths, 13 worker boundaries, 5 canonical writes; overlap refusal, permissions and fault latch | `runtime-full.log` |
| AREA00 shared runtime/effects adapters | PASS: 19 original directory paths and marker tail call; 13 marker/depth/transform cases, 31 compared workers; canonical publish/partial-store/fault contracts under ASan/UBSan | `shared-runtime-full.log`, `effects-binding-full.log` |
| AREA00 script binding | PASS: 36 original worker cases, four exact clip boundaries, three failure cases; both AREA01/AREA00 resource fixtures under ASan/UBSan | `script-binding-live.log` |
| AREA00 direct deferred spawn | PASS: 28 original cases, 12 spawned records, four original allocation failures; whole pool/progress/list/return equality and live actor-view fixture | `spawn-binding.log` |
| Final AREA00 assets/root lookup | PASS: 15 loaders, 70 canary sections, 99 controls; immutable root/table comparison over all 11 retained captures | `placement-root-export.log`, `placement-root-assets-test.log` |
| AREA00 overlay view regression | PASS: 1,090 cases, 1,728 runs, 5,905 calls, all 1,298 reachable words | `overlay-view-regression.log` |
| AREA01 state/runtime/door regressions | PASS: 160 state cases / 219,528,960 bytes; runtime 32 paths + 6 camera + 34 sampler cases / 209 boundaries; door 60 cases / 172 workers / 4 fault cases | `area01-state-regression.log`, `area01-runtime-regression.log`, `area01-door-regression.log` |
| Texture/shadow selectors | PASS: real AREA00 paths and transactional receiver checks; both texture catalogs explicitly BLOCKED before registry mutation | `resources-live-test.log` |
| Shared texture regression | PASS: AREA11 487 keys, AREA01 460, six changed reused keys and eleven rejection cases | `world-textures-regression.log` |
| SFX/module delivery | PASS: 3,278 entries / 234 samples, all 1,278 prior entries / 188 samples unchanged; complete six disc reads per AREA00 subarea | `delivery-test.log` |
| AREA01 SFX regression | PASS: 16 bank captures, 1,001 lookups, 64 original dispatches, 6 sequencer cases / 10 key-ons; 4,096 accepted / 4,097 refused and malformed cases | `area01-sfx-regression.log` |
| AREA00 WORLD full regression | PASS: 1,033 cases, 4,005 variants, 14,679 compared workers, 3,203 field changes, all 64 branches; 74 existing variants incomparable because original execution left RAM | `shared-world-full.log` |
| Final AREA01 shared regressions | PASS: runtime 32 paths + 6 camera + 34 sampler cases; effects nine original spawn/reset/allocator chains and sanitized contracts | `shared-area01-runtime-regression.log`, `shared-area01-effects-regression.log` |
| Canonical-state audit G6 | PASS: 755 files, 26 migrated progress/request bytes | `scene-no-shadow-final.log` |
| Final native source build | PASS, zero compiler warnings | `live-build-final.log` |
| Final first-control regression | PASS: 1,301 locked ticks, zero motion, 30 movement ticks, displacement 9.599849 | `newgame-control-live.log` |
| Resource isolated staged-index build | PASS, zero compiler warnings | `index-resources-build.log` |
| Binding isolated staged-index build | PASS, zero compiler warnings | `index-bindings-build.log` |
| Integration build before native diagnostic | PASS, zero compiler warnings | `live-build-3.log` |
| Native arrival diagnostic | FAIL at `001CB800`, protected texture capacity; area-0 load completes, no arrival alignment/rows | `live-arrival-1.log`, `live-arrival-1/run.log`, `live-arrival-1/receipt.json` |

The diagnostic ran for 277.709 seconds, returned 1 and recorded
`native_parity=false`. The executable SHA-256 was
`5ddc7487154695900b96ba480c82dd42e551b709abc59128da175e23632be23f`.
Its in-process AREA01 phase completions are not independent capture
passes. The subsequent direct-spawn and shared WORLD/FX bindings are
standalone-tested additions beyond this first capacity stop; that
recording does not exercise them.

The earlier harness receipts remain valid because the driver and checker
assertions have not changed: fork AREA00 has 13 paths, 2,731 input commands,
18,331 driver frames, 16,504 capture-shaped rows and 179 corruption
rejections (`verify-fork/`); v2.6.3 has 18,303 frames and 16,490 rows with
the same paths/commands/rejections (`verify-v263/`). AREA01's harness has
16 paths, 4,447 commands, 20,110 frames, 19,565 rows and 150 rejections
(`verify-area01/`). These are harness contracts, not native gameplay.
The old load-refusal diagnostic remains historical in `native/`.

Generated assets and receipts stay ignored. `assets/` is a real directory;
changed SFX/module packages and AREA00 delivery files are private. The
remaining existing assets are linked to the primary checkout and are not
written through. `data/memcard/slot1`, `slot2` and `data/save` are private.

## Remaining differences and next step

The current reproducible stop (2026-10-20, iteration 8) is a00_05 frame
9075: the owner `00128C10` (node `007AC5E0`) reaches `001288D0`, a worker
the live composition does not bind ("Current state"). Before it the C side
plays a00_04 to its end (op 0x03 and op 0x0D sub 1 run since iteration 8).
Earlier, a00_04 frame 7943 in the script host: `001BA1F0` of the owner
`00825600` faulted at the read `0024D88C` (record `00829DE0`), the dispatch
of `ftab_0024D880[3]`. Before it the C side
plays the arrival's 4,017 rows (86 differ: the arrival stance in rows 0..80
and the message phase in rows 3750..3754) and a00_00..a00_03; the
footstep handler `001EB980` (a00_00 frame 33) and the knife's reach in
a00_03 (the composition's view list, the soft-float facing test of
00189EC0 and the freed padlock in the class-4 list) no longer fault.
Open fidelity units preserved from the measured comparison ("Current
state"): the lock_r54 +0x2DC toggle class (a00_00..a00_03, first at
a00_03 row 49) and the post-break record in [54]'s slot (callback
0x1EA240; first at a00_03 row 106, tick 32027: z -1687.31641 vs the
recording's -1687.104 and the +0x1F0 words), which carries into a00_04
from its row 0. Op 0x0B modes 2 / 3 (001B8020, record
`008285E0`), 001C6120 over the arrival owner's bank, the arrival's voice
sectors and `00242EDB` (00128600) are resolved, as are the chain page's
surface packet `00234FE0` (frame 1), the
`0022DCD0` read `00269400` (frame 0) and the `00156620` stop (the 32-view
owner table). Surface dimensions, retained pickup
models and crate roots are now supplied exactly, with no native game-code
changes. Arrival aligns and plays out, but still has zero certified rows
(the harness's comparison stops at its first differing row, row 0). Movement,
camera, interactions, drawing and both returns remain uncertified.
Standalone tests do not establish live call order or eliminate later
missing workers. The inherited AREA01 a01_03 row-957 difference (the disc
drive's per-read seek time) no longer applies to the comparison: with the
comparison-only drive replay (the user's decision of 2026-10-19,
"Comparison-only replay"; LEVEL2_BINDING.md) a01_00..a01_07 pass row for
row.

With the owner-view capacity, the arrival read views and the surface
program resolved (frames 0..202 run), admit 001B8020's mode 3 and rerun
arrival before expanding downstream phases. Preserve the first fault and bind remaining
workers to their existing owners. Then compare all
4,017 arrival/neutral rows and the ending camera memory, followed by each
route phase and the independent side return. Drawing still requires
original packet and displayed-field comparisons. Promote census rows
only after their native AREA00 behavior is exercised and compared.

Unsampled main-line ticks (review 2026-10-19). Four ticks of the fork
AREA00 main line have no row: counters 35978-35980 (a00_04 ends at 35977,
a00_05's four-frame lead-in starts its rows at 35981) and 38516 (a00_06
ends at 38515, a00_07's two-frame lead-in starts at 38517). Before a00_05
and a00_07 are certified, record them on the fork, hidden and under the
run lock:

```sh
# native arm64 macOS; the decomp's .venv supplies zstandard
nice -n 10 ../Extermination/.venv/bin/python tools/record_level_boundaries.py \
  --route-set build/fork_refs/s87/route_a00 --beats a00_05_ferry_deck,a00_07_duct_to_ne_room \
  --out-root ../Extermination/build/fork_refs/levels-boundaries-<new>
```

`--route-set` reads the set's `manifest.json` instead of the supplemental
index and applies the same replay and full endpoint check. Status:
**blocked**. The 2026-10-19 attempt
(`Extermination/build/fork_refs/levels-boundaries-20261019-2/`) resolved
a00_05's gap (4, three missing counters) and its source/target hashes, then
waited in the run lock: `build/.pcsx2.lock` is a 0-byte regular file dated
2026-10-18 13:33 with no `owner.json` and no open handle, not the lock
directory the session takes. The waiting run (no emulator started) was
stopped by its own process id. Only the lock's holder or the user may
remove that file.

Stage-5 native evidence, all on the unchanged executable built from
`11d2f10` (same native source as `ab1cd94`), with input-only replays:

| Private receipt under `build/levels/` | Result |
|---|---|
| `area00-arrival-before-1/` | `00248290` read refusal, arrival frame 0 |
| `area00-arrival-surface-1/run.log`, `receipt.json` | Surface read passes; `001B1020` reports missing retained model |
| `area00-arrival-model-diagnostic-1/run.log`, `area00-readview-preflight-1/diagnostic-parity.json` | Private logging build identifies caller `0015AFA0`, missing model address `00C7CBC0`; ticks and RNG counter/state match the preceding run |
| `area00-arrival-library-1/run.log`, `receipt.json` | Model construction passes; crate selector `0024A850` is missing |
| `area00-arrival-crate-1/` | Arrival aligns at 26925 (drive timing; 26634 at host speed in `area00-arrival-hostspeed-review-1/`, the same 291-tick offset as `a01_arrival`'s 15299 / 15008); 32-view capacity fails at `00156620`, frame 0; 254.403 seconds, native parity false |

The final resource pack has 53 effect windows and 16 retained model
regions. Generated assets are private regular files in this worktree.
No protected AREA11 or rendering source was changed during stage 5.
Intermediate native tick/RNG files are superseded by the final replay;
their hashes and the diagnostic comparison are retained in the private
preflight receipts. Original captures and the before/final native pair remain.

Final stage-5 regressions pass with the final private assets installed:
AREA11 through `a01_arrival` and AREA01 through `a01_02`, each with default
and GPU rendering. All twelve tick, RNG and presented-frame files are
byte-identical to the stage-3 after-capacity baseline (`gpu` frames compared
by tick, RNG and frame order, as above). Asset hashes stayed
unchanged throughout all four final runs. New Game control is 9.599849.
Combined receipt: `build/levels/area00-readview-preflight-1/final-regressions.json`;
default traces: `build/levels/area00-final-defaults-2/`; GPU traces:
`build/levels/area00-final-regressions-1/gpu1-a01_arrival/` and `gpu1-a01_02/`.
Those runs' `ticks.jsonl` files were byte-identical to
`capacity-before-smokes-2/`'s and were deleted by name; their hashes stay in
the combined receipt and in LEVEL_ASSET_DELIVERY.md. Since the 2026-10-19
review, every `build/levels/` receipt lives in the port checkout's ignored
`build/levels/` (moved out of the `levels` worktree).
The isolated staged-index native build has zero warnings
(`build/levels/area00-index-build-1.log`). Eighteen readiness tests and the
unchanged AREA00 harness contract also pass. These regressions do not
certify an AREA00 gameplay frame.

## 2026-10-30, lane F1: the first boss

AREA04's creature family runs on em_area01_runtime_boss.inc (dispatch_boss): the
later levels' translations through em_mid_route_bridge.inc (levels 10..15 added
to tools/gen_mid_route_bridge.py). Bindings it needed elsewhere: 001CB900 in
em_area01_rcl_workers; D_0028A9A0's fields to +8 (00131B10 reads +2); 001D04B0 over
ELF blocks; the chain page's raw-model units of 001F3E30 mode 2 (128 units a page);
the aim/fire scratch and pending caps; the segment's 001A0B10 / 0019D330 /
001A7280 steps for 001B3F10; the pending infection 001E2E80 stores; module loads
0x32..0x35 from the part page; em_sfx's attachment scope (EMSR areas 32..35).
EIGHTH_LEVEL_ROUTE.md section 8.

## Topic commits

Earlier prerequisite checkpoint:

- `d8b1916`: borrowed AREA00 initializer and canonical progress access.
- `b3f3b16`: verified texture/shadow exports and production parser checks.
- `c654a3b`: strict replay phases, checker and positive/negative harness.
- `595cea7`: binding ledger, census and smoke documentation.

Continuation:

- `74c2844`: AREA00 texture/shadow selectors, sound scopes, module deliveries,
  immutable placement lookup root, strict delivery/parser regressions.

- `6e43d7f`: canonical AREA00 scene/state/runtime/script bindings, shared
  spawn/world/effect adapters, logger and original-reference tests.

This ledger and the smoke/census updates form the following documentation
commit. Both isolated source checkouts and private fixture binaries were
removed after verification; cited logs, reports and native-run receipts
remain. No completed playable level is claimed.
