# Player climb, vault and slope slide (AREA11 crates and hill)

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "climb-slide". This document records which original routines climb the
AREA11 crates and slide the player down the short snow hill, what triggers
them, the evidence, the translations and how they run in the live player:
the ledge climb since the Boxes step (census L04/L25) and the slope slide
since census L03 (section 6).

## 1. What the original does

### Climbing: Use in front of a ledge

Ledges that block the move probe (for example the 14-unit AREA11 crates) are
climbed or vaulted only on the Use press edge (`D_00810E74 & spad
0x70003B76`; 0x40 in the captured config) via 00160220 -> 0015DF10. Small
rises are still taken without Use: the floor service 00175900 settles the
feet onto any floor its vertical probe 0019AB20 finds from 13.8 above the
feet down to them (PLAYER_FLOOR.md P17, step 2). The idle
callback 00161020 (cases 1/2) and the walk callback 001612D0 (case 1) call
00160220, which tries, in order:

1. 00184BA0, the interaction use scan (panel, elevator, pickups).
2. Area 0x15 only: 001AAC00.
3. 0015D4C0, the surface-attribute actions. 00176F90 refreshes +23B from a
   short vertical probe, then it switches on +23B (0x37/0x38, 0x32, 0x3B,
   0x33, 0x3A, 0x20, 0x3D). Most AREA11 grid nodes carry 5, 4, 0x5A or 0x51,
   which no case handles; 12 carry 0x32 (section 7) and 2 carry 0x3C.
4. The trigger boxes of areas 1, 4 and 0xD.
5. With +236 == 0 and +23B != 0x35: **0015DF10**, the ledge probe, at the body
   yaw with mode 0, then at yaw -45 and +45 degrees with mode 1. The matrix
   +D0 is rebuilt with build_trs_matrix before each call and the yaw restored
   after.
6. 0015EC50 (state 6, +1F0 0xC: the running jump of route beats 12/14,
   section 8), then 0015FDF0 (an aim solver toward the 001AA4E0 target; when
   it returns 1, 00160220 itself writes +5 = 0x24, +1F0 = 0x3A, +0 = 3).

0015DF10 probes forward with 0019AD00 at 18 and 4.01 above the feet (6 units
ahead, times 0.75 x 2/4/6 by tier +25C in mode 0; 5 units in mode 1). The
nearer wall face within 30 degrees of the requested yaw becomes the ledge
frame (00177510: point, normal, heading, yaw matrix). 0019BC40 then builds the
column table half a unit behind the face. Walking it from the top down, an
entry is a ledge when:

- flag bit 0 is set, the 0019A180 attribute is not 0x46;
- its height is 4.01 < dy <= 32 above the feet (D_002488A0/B4);
- aux < 0.62831855 and the lip sweep 001775E0 is clear;
- dy <= 24 (D_002488B0): no found entry within 14 above; the top sweep may hit
  only a class-4 pickup box (behaviour 00219550); both side sweeps clear; the
  depth test 00177F40 finds a top; three 18-unit columns (001760C0) are clear.
  The grab point is 4.5 out from the face.
- dy > 24 (mode 0 only): 001776E0 and 00177CF0 hand/side sweeps and two columns
  of dy+2; the grab point is 1.5 out; +1F1 = 1.

The commit writes +254 = dy, +C4 = +218 = the ledge heading, +25F = 1. With
+2F2 set (mode 0) and the grab point more than 5 (8 for +1F1 = 1) away,
00177460 chooses the running vault: +5 = 3, +1F0 = 9, grab = the face point,
+290/+298 = the normal. Otherwise +5 = 2, +1F0 = 8.

State 2 (00161790) classifies +254 against D_002488A0..B0 (4.01, 7, 12, 17,
24): sub-state 0xA (low step), 0x14 with +2F1 = 0/1/2 (clips 0x77/0x78/0x79 =
D_002754A0), or 0x1E (hang, above 24: clip 0x71, 0x7A, then state 9 hang with
clip 0x7B/0x8E from 00188550). 0017D800/0017D8D0 rise to the grab point over
+28 ticks; the pull-up (sub-states 12/22) evaluates the skeleton and places
+B4 at the hip node's world y minus 10/11, requests clip 0x8D/0x8C and runs the
floor service; 13/23 stand (or hand on to the walk through 0017C440 when the
gait is above 1). 0017DEB0 plays 00182870(p, 0) and the surface puff. 00161690
corrects one AREA11 grab point (x 400..410, z 236..246, top 285..587) to
(405, 241), yaw 0x3FBBA866.

State 3 (00162190) is the vault: 0017D940 (low), 0017DAF0 (+25C = 1) or
0017DC80 (high) set an arc of at least 8 or 12 ticks with a lift +26C that
decays by +270; clips 0x69 (tier 3) or 0x6A, 0x6B/0x6C, 0x7D, 0x8C; 00162080
is the vault's AREA11 grab correction (x 397..427, z 227..257 to (411, 240),
yaw 0x3FB2B8C3).

### Sliding: an authored slope node under the feet

00175CF0 (the floor-hit apply of 00175900) records the class `*(u16 *)(record
+ 0x1A) & 0xFF00` in +238. For a floor contact of class 0x1000 (not attribute
0x39) it sets +237 = 1 and +218 = the downhill heading. 001796C0 then enters
the slide: +5 = 0x1C, +6 = 0, +1F0 = 0x30 (unless +5 is 0x1D/0x1E).

For a grid hit the record at 0x700031D0 is the grid node itself (0019ED80
stores it at 0x700031D0), so the class is the node's **authored** byte +0x1B.
Only cell n-gon hits get the 001A4030 normal-ratio class (staged at
0x700030CA, record 0x700030B0). In the captured AREA11 grid (3,099 nodes) 18
nodes carry 0x1000, all in one region: x 209..275, z 305..363, y 220.7 down to
183.3. That is the hill. The normal-ratio rule would classify 250 grid nodes as
slopes; the original returns authored 0x4000 or 0x2005 for many of them (for
example under (338.2, 95.2) and (289.9, 224.9)).

State 0x1C (0016C6A0):

- 0: 0016C570 side probes (0019AD00 mask 0x80000006 at +-4.5 across the
  downhill heading), +B4 -= 0.6, 00175900(p, 0), speed 0.2, clip 0x5E at
  frames-15 (blend 8), loop sound 0x12E (+31B handle, +31A, +31C).
- 1/2: lean +C0 toward the slope angle +9C and turn +C4 toward +218 at
  0.10471976 per tick (001B12B0), 001791D0 wall sweeps from the hip node.
- 3: 00224B80 (damage while sliding; 2 ends the slide), 0017F5F0 steering
  (00174FD0 stick quadrant: clips 0x61 speed up, 0x62 slow down, 0x63/0x64
  lean, 0x5F neutral; speed += 0.01 sin(slope) x (1 +- D_00248790[gait]),
  capped at 1.5) and 0016CD70 motion (move along +C4 when within 30 degrees of
  +218, else along +218; +B4 -= speed sin(slope) + 0.6 - +2EC; effect
  0x80000065 on snow every 8 ticks; re-arm sound 0x12E).
- Landing on a non-slope floor: +26C = speed/0.75 (<= 1), sub-state 0xA: clip
  0x60, 00182870(p, 1); 0xB/0xC: clips 0x65, root-motion speed from the root
  node, footstep sounds at clip frames 24/13/2, then state 0.
- Airborne for 5 ticks: sub-state 0x14/0x15 (clip 0x73 landing, 00224290,
  0017C580 or clip 0x6D).

0015BCF0 stops the 0x12E loop when the player leaves state 0x1C.

## 2. Whole-world original evidence

`tools/test_player_slide_reference.py` includes `EE`, a bounded EE interpreter
backed by the full 32 MB RAM image and the 16 KB scratchpad. It runs the
original player stage 0015BCF0, unmodified, over the captured AREA11 world
(`../Extermination/build/startup-reference/playable_ee.bin` = state04 RAM, and
the state04 scratchpad). Only the player placement and pad bytes are seeded;
sound/effect submissions are recorded. One stage takes about 0.35 s.

| Route | Input | Original result |
|---|---|---|
| crate | stand at (228.8, 189.9, 280), yaw 0, press Use | state 2, +1F0 8, sub-state 0x14, +2F1 1: clip 0x70, 0x78, 0x8C; sounds 0x74, 0x12B, 0xEC; effect 0x80000028; on the crate top (228.797, 203.776, 288.268) after 79 callbacks |
| stack | on that crate at (228.8, 203.8, 292), yaw -pi/2, press Use | onto the stacked crate 0x7A7980: (219.233, 217.786, 292.0) |
| vault | run from z 250 (tier 3, speed 0.8), press Use 18 units away | state 3, +1F0 9, 71 callbacks, lands on the crate (228.761, 203.776, 287.768) |
| slide | from (238, 220.5, 308) run toward +z | contact class 0x1000 at z 325.6: state 0x1C; clips 0x5E, 0x61 (speed 0.2 up to 0.79), 0x60, 0x65; 116 callbacks; idle at (257.334, 185.386, 376.373) |

Original column tables (0019BC40) at the crates: crate 0x7A7F60 and 0x7A7980
stacked at x 207.7..221.7 (tops 203.79, 217.79), crate 0x7A7C70 at x
221.8..235.8 (top 203.78), ground 189.84, plateau 217.86..219.8 at z 300..304.
The crates' collision is the compact cells uid 7..10 (six type-0x2000 faces
each, bboxes above), published by their class-4 owners exactly like the panel's
cell 18.

## 3. Translations

| File | Original routines |
|---|---|
| `src/game/em_player_slide.c` | 0016C6A0, 0016C570, 0016C520, 0016CD70, 0017F5F0, 00174FD0, 001791D0, 00179880, 001B12B0 |
| `src/game/em_player_climb.c` | 0015DF10, 0015DEC0, 00177510, 001775E0, 00177F40, 00177460, 001776E0, 00177CF0, 0019A180, 00161790, 00161690, 0017D800, 0017D8D0, 0017DEB0, 0017F320, 00188550, 00162190, 00162080, 0017D940, 0017DAF0, 0017DC80, 0017DE20 |
| `src/game/em_player_floor.c` | build_trs_matrix and 00102B08/00102A60 (shared SDK helpers added: `em_player_sdk_trs`, `em_player_sdk_yaw_matrix`, `em_player_sdk_apply`, `em_player_sdk_yaw_transform`) |
| `src/game/em_collision.c` | 0019BC40 column table (`em_collision_column_table`) with 001A5760 (face column) and 0019F330 (grid node column); authored grid node class (`EM_COLL_FLAG_NODE_CLASS`) |

Every other callee is a worker; a missing worker faults. The modules take a
mirror of the actor bytes each routine reads and writes.

Corrections found against the instructions (the readable C is not trusted):

- 0016C6A0 (NEARMISS): the sub-state 0xB/0xC calls are 00178B90(p, 1); the
  readable C shows (1, 0).
- 0019BC40 keeps up to 20 candidates but its result arrays are 0x40 bytes apart
  (0x700030F0 heights, 0x70003130 objects, 0x70003170 flags): past 16
  survivors the compaction writes alias. The native table holds 16 and faults
  above that instead of reproducing the aliasing.
- 00162190 case 23 (vault landing) snaps the feet with 00102948(p+B0,
  node1+C0), an lq/sq pair: all four lanes are copied, so +BC takes the
  node's w as well. `EmPlayerClimbActor.position` therefore mirrors +B0..+BC.
- 0016C6A0 passes constants to its two external hooks: 0021D250(p, 0) and
  0021D2E0(p, 0x78, 0). The native workers take no arguments, so the binder
  supplies them; the slide oracle asserts the recorded original arguments are
  exactly these.
- The asm-void 001B12B0 returns wrap(current) for a zero difference (not the
  target) and the target when |difference| <= rate.
- 0019BC40 builds v2 = pos + (0, 1, 0) and 0019F330 intersects along v2 - v1,
  so the direction's y is (y + 1) - y in EE arithmetic, not exactly 1 (it
  moves the crossing height by one ULP at some points).
- The grid rank tables are **not** a pure prune. 0019BC40 visits only the
  nodes whose rank bounds (node +0x0C) admit the point ranks 0019F1A0 computes;
  near some edges a node the exact 0019F330 test accepts is excluded. The
  native query tests every node (the EMCL carries no rank tables), so it can
  report an extra entry there. Over 5,939 random AREA11 columns this happened
  at 4 points, all off the route (x 112-208, z 434-478); every crate and hill
  column matched. The same holds for the port's other grid queries, which also
  brute-force. Exactness needs the rank bounds and the 0019F1A0 tables in the
  EMCL.

## 4. Verification

- `python3 tools/test_player_slide_reference.py`: the original 0016C6A0 chain
  against em_player_slide.c, every field and every worker call in order.
  Quick: 3,282 approach, 900 tick, 400 motion, 400 steer, 200 steer-input, 120
  sweep, 120 side-probe and 60 release cases, about 5 s. `EM_TEST_FULL=1`:
  43,915 approach and 32,000 routine cases. Mutation checks on boundaries
  (approach `<=`, step clocks, drop limits) are caught.
- **Write coverage** (both unit oracles): every actor byte the original
  stores during a case (its instructions and the scripted hooks) is recorded,
  and each must lie in the compared field set, so a field the original writes
  but the native mirror lacks fails the test instead of passing silently.
  (This found the missing +BC of the vault landing: removing +BC from the
  compared set, or copying three lanes, fails the quick run.)
- `python3 tools/test_player_climb_reference.py`: 0015DF10, 00161790, 00162190
  and 0017F320 against em_player_climb.c, plus build_trs_matrix. Quick: 300
  TRS, 1,500 probe (about 300 reaching each of the four climb starts), 700
  state-2, 700 vault and 80 hang cases, then a 240-point column-table world
  sample (below; half inside the crate and hill boxes), about 7 s.
  `EM_TEST_FULL=1`: 3,000 TRS, 37,000 cases and 2,000 column points.
  Boundary cases hit dy == 4.01, the 14-unit found gap, the exact 5/8 vault
  distance and the AREA11 correction boxes; the vault landing's node w is
  1.0 or 0.5 so the +BC copy is exercised.
- **World mode** (`EM_TEST_WORLD=1`, 2-3 minutes each): two copies of the
  original stage run the routes above; in one the native module replaces the
  original callback while its workers call the original routines (00175900,
  0019AD00, 0019BC40, anim_eval_skeleton, ...) on the same world. After every
  frame the full 0x320-byte player actor and every sound/effect call must be
  identical. Results: slide 175 frames with 116 native 0x1C callbacks; crate
  124 frames with 79 native state-2 callbacks; stack 124 frames; vault 164
  frames with 71 native state-3 callbacks. All identical.
- **Column table** (default sample of 240 points, or `EM_TEST_WORLD=1`, route
  `column`, `EM_WORLD_POINTS`, default 2,000): the original
  0019BC40 over the captured world against `em_collision_column_table` over
  `assets/scene_snow/snow.emcl` plus the captured type-0x2000 owner cells
  (crates uid 7..10, panel 18, pickup 17), at 1,500 crate/hill columns and
  4,500 level-wide columns: 5,939 columns with 9,316 entries identical (flags,
  height, aux, 0019A180 object byte); the 4 rank-table exceptions above.
  Owners whose cells are n-gons (elevator uid 4, truck uid 14, pickup boxes
  uid 19/23, prim header 0x1800) cannot be represented by `EmCollCell` and are
  excluded from the comparison; 001A58B0 (their column test) is untranslated.
- **Real captures** (`EM_TEST_WORLD=1`, route `capture` of each test;
  inputs: the PCSX2 route beats `../Extermination/build/s87/route/<beat>/`,
  FIRST_LEVEL_ROUTE.md). Each beat runs on the RAM and scratchpad snapshot
  its trace resumed from (`trace['source']`), so the world is the one the
  original met. Both compare every trace row (state +5, +1F0 and clip +20C
  exact; feet +A0, body +B0, yaw +C4 and clip clock +3C within the trace's
  5-decimal printing plus the replay's inputs), and in both an original
  stage runs beside the native one: every frame the two 0x320-byte actors
  and their sound/effect calls must be identical.
  - **Climb** (`test_player_climb_reference.py`, about 60 s): every Use
    climb from idle on the route, i.e. every frame whose +5 goes from 0 to
    2, one per Cross input and 1-8 frames after it (so a re-capture does
    not break the test). The stage is seeded from the row before the press
    (feet, body, yaw) and the press is injected on the climb row. Tolerances
    5e-4 (feet, body), 2e-5 (yaw), 1e-3 (clock); measured at most 9.3e-5,
    4.3e-6 and 0. The press frame's body and clock come from the idle pose
    the seed does not carry (13_east_tower presses 4 frames after a walk
    stop), so they are compared from the next frame.

    | Beat | Press (counter) | Frames | Clips | Native calls | End (feet) |
    |---|---|---|---|---|---|
    | 05_boxes crate r4 | 6428 | 80 | 0x70, 0x78, 0x8C | probe 1, state2 79 | (228.785, 203.776, 288.268) |
    | 05_boxes crate r3 | 6646 | 80 | 0x70, 0x78, 0x8C | probe 1, state2 79 | (219.233, 217.786, 288.309) |
    | 11 tank (22.4 units) | 11697 | 93 | 0x70, 0x79, 0x8C | probe 1, state2 92 | (411.567, 286.090, 241.690) |
    | 11 pipe end | 12167 | 65 | 0x70, 0x77, 0x8C | probe 1, state2 64 | (471.321, 279.900, 283.192) |
    | 13 east tower | 13569 | 94 | 0x70, 0x79, 0x8C | probe 1, state2 93 | (437.646, 289.750, 179.776) |

    All five reach sub-states 0x14..0x17 (+2F1 0, 1 and 2 between them).
  - **Slide** (`test_player_slide_reference.py`, about 105 s, the original
    and native replays in two forked workers): the whole beat 06_hill_slide
    (206 rows: walk off the ledge, slide, skid out, idle) replayed from the
    05_boxes snapshot by `RouteReplay`. Per frame it runs what the real frame
    feeds the player stage:
    - the original pad unpack 001B5940 on the recorded pad input, through
      the libpad read 00110B38. An input recorded at trace frame f reaches
      the stage 3 frames later (05_boxes: Cross at f172, climb row f175);
    - the camera the stage reads, from the previous row: eye, target,
      forward and the heading D_008106A0. The heading is computed as the
      byte-matched camera commit 0018C0D0 computes it: the original atan2
      0011E620 of (-forward.z, forward.x). The trace prints 5 decimals, so
      the heading is close, not bit-exact. (This replaces the earlier note
      that 06 could not be replayed without D_008106A0.);
    - the counters 0x70003B64 (row n runs with n - 1) and
      0x810750/0x70003B68 (+1 before the stage).

    Result: every row's state, action and clip exact, the original and
    native actors identical every frame, 109 native state-0x1C callbacks,
    25 identical sound/effect calls, end (265.699, 185.28, 373.673).
    Tolerances 1e-3 (feet, body), 2e-5 (yaw), 1e-3 (clock); measured at most
    3.7e-4, 8.6e-6 and 0. The walk carries the heading's rounding into the
    positions. A mutation (entry speed 0.2 to 0.21 in em_player_slide.c)
    fails at row 7018.
  - **RouteReplay limits** (measured with the original stage only, not
    tests). Only the player stage runs, with no camera stage, owners or
    scripts. The replay is exact only while the captured pad delivery
    matches the recorded inputs:
    - 11_crevice_prompt, rows 0-706 (walk, tank climb, pipes, the state 5/8
      drop, pipe-end climb): every row's state, action and clip matches,
      but the feet drift up to 0.36 on the pipes;
    - 05_boxes: one row (f122) requests clip 2 one frame before the trace;
    - 13_east_tower: at f133 the trace's player stops (+1F0 5, then idle)
      while the recorded stick stays deflected, and the replay walks on.

    So the climb comparisons use the seeded press, and only the slide beat
    is replayed whole.
  World checks (the default column sample and `EM_TEST_WORLD=1`) read
  `../Extermination/build/startup-reference/playable_ee.bin` and the state04
  scratchpad, which the tests extract on first use from the startup-reference
  save state `SCUS-97112 (0AE679AF).04.p2s` (its eeMemory.bin is
  byte-identical to playable_ee.bin) with the decomp venv's zstd reader into
  `build/player_world_reference/state04_scratchpad.bin`. Without those
  inputs the default column sample is skipped with a message.

## 5. Clips

`tools/export_player_climb_slide_clips.py` appends the 29 terminal clips these
routines request (slide 0x5F-0x68, 0x6D, 0x72; climb 0x70, 0x71, 0x77-0x7B,
0x8A-0x8E; vault 0x69-0x6C, 0x7D) to the EMPC channel bank and the EMDL
display model, preserving every existing byte. It stages to
`build/player_climb_slide_export/` (1,015 frames); `--install` replaces the
assets, and the EMPC clip count pinned by `tools/test_pose_bank_reference.py`
must change with it. Clips 0x5E and 0x73 are chained in the original bank
(next 0x5F / 0x72 with a blend flag) and em_pose_bank.c rejects chained clips,
so they are not exported.

## 6. Live binding

**State adapters.** `em_player_slide_live_state` (+5 0x1C) and
`em_player_climb_live_state` (+5 2 and 3) run the translated callbacks over
the live record; `em_player_climb_live_probe` runs 0015DF10 for the Use
chain. em_player_closure_live.c binds them in 0015B130's table
(`EmPlayerStatesBinding.stage.state[]`) with every other FLOOR closure
state, since the Boxes step (climb) and census L03 (slide):

- Each adapter keeps the module's worker table. Around every worker call it
  stores the mirror into the record and reads it back, so a worker that
  reads or writes the record sees what the original would.
- The floor, fall and probe workers (00175900, 001796C0, 001764E0) run over
  the record through `player_states_floor_service`,
  `player_states_fall_check` and `player_states_wall_probes`; the move and
  sweep walkers 0019AD00 / 0019AFE0 through em_coll_move (the original
  walkers over the collision world, em_coll_grid_hull for 0019CB60 /
  001A6440). The floor walkers report the grid nodes' authored class, so
  the hill's class-0x1000 nodes enter the slide live.
- The slide's record-level slots: 001749A0 / anim_clip_arbiter / 001C61D0
  on the one pose owner (chained 0x5E -> 0x5F plays from PLAYER_CLIPS.md's
  bank), 00178B90 (em_player_recovery), 00224B80
  (em_player_recovery_react_00224B80_worker), 00224290 / 0017C580 /
  0021D250 / 0021D2E0 (em_player_fall), 00182870 (em_player_reaction),
  00182430 (em_player_floor.c em_player_step_sounds, the footstep's one
  translation), 001FBD50 / 0011A070 (em_sfx tracks), 001B12B0
  (em_script_host_001B12B0), 00174FD0 (em_player_record_00174FD0), the SDK
  sine / cosine / atan2 (em_sdk_math_original).
- A missing worker faults before the record is touched; the mirrors
  (`em_player_*_actor_from_live` / `_to_live`) are checked by both reference
  tests against their original-verified offset tables.

**Live evidence.** The level smoke's `boxes` phase equals route 05's two
climbs row for row and its `slide` phase equals route 06 from the slide
entry through the idle return (LEVEL_SMOKE.md "boxes", "slide"). On the
route the slide runs 109 native 0x1C callbacks, as in the world-mode
replay of section 4.

**Remaining gaps on these paths:**

1. **Effects.** 001EFD90 (the slide's 0x80000065 every 8 ticks, the climb's
   surface puff) runs the live effect binder (em_effects_live, census L26);
   the chains it builds are not drawn yet (EFFECT_MANAGER.md 8.3).
2. **Sounds.** The slide loop 0x12E, the skid and landing steps and the
   climb's 0x74 / 0x12B / 0xEC are not in the exported sfx registry
   (tools/export_sfx_registry.py takes its ids from the decomp's scene
   lists; WP-14). 001FBD50 then returns -1: the record's +31B stays -1 and
   0016CD70 re-requests 0x12E on every motion tick (57 silent requests on
   the route) where the original holds one track.
3. **Not reached on the route:** the slide's airborne exits (sub-states
   0x14 / 0x15: 00224290, 0017C580, clip 0x6D), 0021D250 / 0021D2E0
   (surface 0x5D, sub-state 0x1E), the vault (state 3) and the hang
   (state 9). They are bound; nothing live has exercised them.
4. **Camera.** 00191390 (the climb states' height row) and 00193EB0 (the
   per-area climb cinematics) are not bound: the follow camera is the
   legacy em_camera.c (WP-16).
5. **Around the slide** the idle / walk states are the original's since
   census L12. The hand-back from a translated state keeps the
   record's +1F1 (00161020 case 0 and 0017C030 do not write it; route 06
   f181..: +1F1 stays 1).
6. The column table's n-gon owner cells (001A58B0) and the grid rank tables
   of section 3 are unchanged.

## 7. Attribute 0x32 columns (identified, not translated)

AREA11 has three columns of attribute-0x32 grid nodes, each a floor node at
the foot, a wall node and a floor node at the top: x 359.8-360 (y 185 to
264.9, z 252-292.5), x 316.9-325.6 (y 185 to 249.9, z 215-218) and x
467.6-476.7 (y 270 to 355, z 402-405). A Use press on one reaches 0015D4C0
case 0x32: 00177030(p, 4) gates it, then +5 = 0xB and +1F0 = 0x15 or 0x16
(00180300 at 10 units above the feet decides). The state-0xB callback
00165B60 requests clip 0xE3 or 0xE4, later D_002754D0[0], and hands on to
state 0xC (001662D0). None of these routines are translated yet. The
remaining route from the hill to Roger ((354, 300.9, 190.8)) climbs well above
the hill floor, so these columns are the likely next climb work. Attribute
0x46 marks 8 nodes whose column entries the ledge probe skips.

## 8. Player states along the captured route to Roger

The PCSX2 route capture (`../Extermination/build/s87/route/*/trace.json`,
another lane's) records the player state +5 and action +1F0 per frame. The
state/action sequences show which player mechanisms the rest of the level
needs (only states 2, 3 and 0x1C are translated here):

| Beat | What | +5/+1F0 sequence |
|---|---|---|
| 05_boxes | Cross-climb crate r4, then r3 | 1/0x1, 1/0x5, 2/0x8 twice |
| 06_hill_slide | walk off the ledge down the hill | 1/0x1, 28/0x30, 28/0x0 |
| 10_cage_roof_roger | cage ladders to the roof | 5/0xB, 8/0xF, 11/0x15, 12/0x17, 12/0x18 |
| 11_crevice_prompt | tank climb, pipes, pipe-end climb | 2/0x8, 5/0xB, 8/0xF, 2/0x8 |
| 12_crevice_jump | running jump across the crevice | 5/0xB, 8/0xF, 6/0xC, 8/0xF |
| 13_east_tower | high ledge climb | 2/0x8 |
| 14_roger_encounter | running jump between towers | 6/0xC, 8/0xF |

The state-2 climbs of 05, 11 (tank and pipe end) and 13, and the whole
06 slide, are compared frame by frame with the native modules (section 4,
real captures). State 5 is the fall (00162DB0, PLAYER_FLOOR.md P18), 8 the landing (00163B40,
entered by 0017C580), 6 the running jump reached from the Use chain
(0015EC50, callback 001634A0), 11/12 the attribute-0x32 columns (section 7).
