# AREA15 sub 1's event, the truck in AREA19's hall and AREA03: fifteenth-level route, original ground truth

Lane STORY (capture), 2026-10-02 (session s95). Original executable
SHA-256 `ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

`FOURTEENTH_LEVEL_ROUTE.md` left the story in AREA15 sub 1 at entry 0, with
sub 1's event ([4] / [6], 0x823850) not played. This document derives the
event and what follows it from the code and the grids (section 2) and
records it as three opt-in beat groups of `../Extermination/tools/route_capture.py`:
`a15b` (5 beats: the event, the forced return to AREA15 sub 0, its two
scripts, the bed, door [14]), `a19e` (8 beats on the main line and 2 side
beats: AREA19 sub 1's hall, the ceiling, the truck [46] opened with the key
item 0x25, its 18-gauge battery pack, back through the tower to sub 0's
deck, the panel [24], door [25] into AREA03) and `a03` (1 beat: AREA03 sub
0's lift). Hidden PCSX2, exact one-frame steps, pad input only, no
teleports and no memory writes, one trace and one resumable snapshot per
beat. Nothing here is port behaviour.

**Status.**

- **AREA03 was reached** (a new area: no earlier level entered it): door
  [25] of AREA19 sub 0 gave AREA03 sub 0 entry 1 (a19e_07), and AREA03's
  lift was ridden down to entry 2 (a03_00). **AREA03 was not recorded to
  an exit.** Its ways on (sub 0's door [13] to sub 1, the NPC [22] and item
  0x2A, sub 2's lift to AREA08, sub 1's seal [23] and door [24] to AREA04)
  lie beyond six 0013D2D0 creatures in sub 0 (section 6); with 2 rounds
  held, the exploration walk to door [13] died at the top of the stair 32
  (section 6).
- **AREA15 sub 1's event** (FOURTEENTH_LEVEL_ROUTE.md section 7 item 1) is
  played: on the mezzanine 0x8239F0 (C) sets flag 0x23 = 1 and starts
  [4]'s script 0x827400, then the 900-frame count, then script 0x8277C0
  (0x823C80, C; at its end items C64 0x06 and CC3 0x0E), the forced return
  to AREA15 sub 0 entry 0 (0x823C80: counter 0x23 = 0xFF and the five
  areas' sub-states), [1]'s script (0x824240, C: at its end counter 0x24 =
  1 and item 0x25) and [2]'s script (0x824350, C: counter 0x24 = 2). Also
  measured, with their writers not read: counter 0x23 = 3 and flag 0x23 =
  0xFF while script 0x8277C0 ran, and flag 0x24 = 0xFF at the end of [1]'s
  script (section 2).
- **Why AREA03 next** (code, section 2): AREA19's door [25] needs
  D_00810854 bit 1 from the panel [24], which costs 2 * 0x10 = 32
  half-units of battery charge; the 6-gauge pack holds at most 12 (measured
  refusal, a19e_s1). The 18-gauge pack (item 0x1C, capacity 0x24) is the
  pickup g[0] inside the truck [46] in AREA19 sub 1's hall, whose door
  takes item 0x25, the item AREA15 sub 0's [1] gives after the event.
  (AREA19 sub 0's g[4], a 24-gauge pack, item 0x1D at (877.2, 210, 976),
  is another source; it is not on this route and was not reached.)
- **Health and infection.** The bed [16] in AREA15 sub 0 with item 0x20
  set health 100 and infection 0 (a15b_03). In the kept beats every
  health loss is a hit (action 0x3E: a19e_06 f249 / f542, a03_00 f100);
  at infection 70 health stayed 44 through a15b_00..a15b_03 (about 17,000
  frames, about 1,600 of them walking) until the bed. A loss with no bite
  was seen only in a deleted exploration run at infection about 80
  (section 6; rule not read, section 7). Health 44 -> 100 (the bed) -> 80
  at AREA03's arrival -> 70 at a03_00's end; infection 70 -> 0 -> 25.
- Groups: a15b 5 beats, 20,337 frames, counters 180769..201130; a19e 8
  main-line beats, 9,843 frames, counters 201135..210987, and 2 side beats,
  3,028 frames; a03 1 beat, 2,574 frames, counters 210988..213562.
- **Census** (section 5): `fifteenth-delta` against every earlier pass,
  the first level's C10 capture passes included: **51 functions (32,080
  bytes) ran in no earlier census pass**: 30 boot, 6 AREA15 overlay
  (0x823B40, 0x823C80, 0x823F60, 0x824240, 0x824350, 0x8243E0: the event's
  end and AREA15 sub 0's [1] / [2]) and 15 AREA03 overlay functions; no
  AREA19 overlay function is new.
- **Web leads:** the walkthroughs named in FOURTEENTH_LEVEL_ROUTE.md (GameFAQs,
  SuperCheats) answered this session's fetches with HTTP 403; nothing here
  comes from them.

In-game text (messages, captions, page entries) is described, never
quoted. Positions are world units; yaw is the player's +0xC4 (X = sin, Z =
cos).

## 1. Tool, outputs and conventions

```sh
# decomp repo, .venv python, repo root (hidden PCSX2; hold build/.pcsx2.lock)
.venv/bin/python tools/route_capture.py run --beats a15b,a19e,a03
.venv/bin/python tools/route_capture.py run --beats a19e_s0_down694,a19e_s1_panel   # side beats, by name
.venv/bin/python tools/route_capture.py events --beats a15b,a19e,a03
.venv/bin/python tools/route_capture.py verify --beats a15b,a19e,a03
```

a15b_00 starts from `build/s87/route_a15/a15_01_door51/state.p2s`; each
later beat from the previous beat's snapshot (the side beat a19e_s0 from
a15b_04). Outputs: `../Extermination/build/s87/route_a15b/<beat>/`,
`route_a19e/<beat>/` and `route_a03/<beat>/` (ignored): `trace.json`,
`state.p2s`, `eeMemory.bin`, `gs.bin`, `scratchpad.bin`, `original.png`,
`snapshot.json`. Every kept snapshot was reloaded after its capture
(`run_beat`'s check) and resumed on the first check (`tail_idle_frames` 0).

- **Rows** (`decode_a15b`): the a19d row without owners plus `s838`
  (D_00810838..3B: the truck's door byte first), `t130` (the status block
  D_00810130..5F: page ids, the hub / ITEM hover byte D_00810141, the list
  cursor D_00810147), `batt` (D_00810CB0..B7: the charge D_00810CB2 and the
  capacity D_00810CB7, half-units) and `inf` (the player's +0x228,
  infection). The a19e_00..04 rows also carry `A19E_OWNERS`: the two
  0012E3A0 creatures of this load (nodes 0x7A70B0 / 0x7A73A0, `k` = their
  +0x30..+0x3F: +0x34 = 180 hit points) and the truck [46] (0x7AF1F0,
  behaviour 0x829A70), checked at a19e_00's first row (behaviour pointers
  and positions).
- **Helpers** (route_capture.py): `a15b_walk` (a19d_go_rock per point,
  ending at once when a predicate holds), `a15b_close_page` (a page the
  game opened: 60 frames, then Triangle), `a15b_status_open` (START until
  D_00810131 = D_00810132 = 1), `a15b_status_pick` (the hub and the ITEM
  root select with the stick held, then Cross with it held), `a19e_take`.
- **Determinism.** As in the earlier levels, each beat is closed loop from
  its source snapshot. Two kept beats differ from their exploration runs:
  a19e_02 drops onto the truck's roof (the exploration dropped beside the
  truck and walked round its south-west end; the first capture attempt of
  that walk was killed at the Use point by a creature at the stair's foot,
  section 6) and a19e_03 keeps item 0x1F (at health 100 the HEALING page
  refuses it: the first capture attempt stalled on the refusal).

## 2. What decides each step (code, then the grids, then the capture)

Evidence marks as in decomp docs/WORLD_GRAPH.md: BM byte-matched C, NM
NEARMISS C, C overlay C linked byte-identical, ins read from the original
instructions. Grids: read from the captured RAM (the world-section header
at *0x0028A598, the 64-byte nodes; node numbers are that RAM's).

- **AREA15 sub 1's event ([4], 0x823850, C).** State 0 goes to state 3
  when flag 0x23 == 0xFF or flag 0x22 != 0xFF (001BA1C0, BM, tests
  D_00810758[id] == 0xFF; during the event flag 0x23 is 1, which does not
  pass that test). [4] (+0xD 0x54, at (804.5,
  325, 938)) runs the event; [6] (+0xD 0x5D, at (823.5, 315, 971)) is a
  second model. By D_008107FB: 0 -> 0x8239F0 (C): while the player's y is
  below 310 nothing; at y >= 310 inside the polygon 0x827C80 (001B1EA0 mode
  0; six corners (890, 995), (750, 995), (750, 825), (793, 825), (793,
  906), (890, 906), read from the overlay image) +5 = 1, flag 0x23
  (D_0081077B) = 1 and script 0x827400; at its end 001FAE70(0),
  D_008107FB = 1 and +0x28 = 900. 1 -> 0x823B40 (C): D_008107FB = 2 when
  the player's y < 291, or x > 893 with y > 329 and z > 940, or the
  900-frame count ends. 2..4 -> 0x823C80 (C): script 0x8277C0; at its end
  001C47A0(6, 1), 001C4760(0xE, 1), D_70003B8D = 3; then 001FABB0,
  001FBC50, 001AEDE0(0xFF, 0) and 60 frames; then D_008107FB = 0xFF, the
  request D_008106B5..B8 = 0F 00 00 01 and D_00810730[0, 1, 2, 4, 6] =
  0x82, 0x81, 0x82, 0x81, 0x81.
- **The mezzanine.** Grid floor 1437 (y 315.1, x 766.7..890, z 825..995;
  its underside 1436 at y 305.1 is the low ceiling the fourteenth level
  met at x 864.5). From entry 0 (906, 290.1, 925.3): the stair 794
  (attribute 0x35; x 891.1..904.3, from y 290.1 at z 898.1 up to 339.6 at
  z 844.9), the platform 319 / 320 (y 339.6, x 872.4..905, z 828..845), the
  stair 792 (attribute 0x35; x 838.8..872.4, down west to y 315 at x
  838.8), then west on 1437 into the polygon (x < 793). Measured
  (a15b_00): flag 0x23 = 1 at (792.8, 315.1, 841.9) f465; script 0x827400
  f467..f1210 (D_008107FB = 1 f1210); no input until the count ended
  (D_008107FB = 2 f2110) and script 0x8277C0 started (f2113). Nothing hit
  the player during the 900 frames.
- **The return (a15b_01).** Script 0x8277C0 ran on; measured: counter
  0x23 = 3 at a15b_01 f647 and flag 0x23 = 0xFF at f3259 (writers not
  read: 0x823C80's C writes neither; both fall while the script runs), item
  C64 0x06 at f3259 (0x823C80's 001C47A0(6, 1) at the script's end; the
  player had been placed at (882, 330.5, 964)); 001C47A0(6, 1) posted the take request (B0
  = 1, B1 = 6), which opens the SPR4 page on its UPPER U.R.S. part
  (STATUS_PAGES.md section 1: B1 5..6) listing the new part; the beat
  closes it with Triangle (f3322; the page waits for input). 60 frames
  later the request (0x823C80's state 3): area bytes 0F 00 00 0F at f3390
  (counter 0x23 = 0xFF), AREA15 sub 0 entry 0 (907, 240, 928) at f3740.
- **AREA15 sub 0's [1] / [2] (0x824070, NM; 0x824240 / 0x824350, C).**
  0x824070's state 0 keeps [1] (+0xD 0x64, (831.6, 240, 888.8)) and [2]
  (+0xD 0x50, (832.8, 240, 858.8)) only with flag 0x23 == 0xFF and flags
  0x28 / 0x29 != 0xFF (001BA1C0). With D_008107FC (counter 0x24) 0, [1]
  runs 0x824240: flag 0x23 == 0xFF -> script 0x827D70; at its end D_008107FC = 1,
  001C47A0(0x25, 1), 001FAE70(0). With counter 0x24 = 1, [2] runs
  0x824350: script 0x8281F0, at its end D_008107FC = 2 and
  001C4760(0xF, 1). Measured (a15b_01 f3743 .. a15b_02 f11304): [1]'s
  script started 3 frames after the arrival's control and ran 9,312
  main-loop counters (c186640..c195952); counter 0x24 = 1 and item 0x25
  (0x824240) and flag 0x24 = 0xFF (measured; writer not read: 0x824240's
  C does not write it) at a15b_02 f9276;
  the take opened the ITEM > EVENT page (Triangle, f9339); [2]'s script
  f9346..f11304; counter 0x24 = 2.
- **The bed [16]** (00159620, BM, model 0x36, at (972, 240, 844.5), yaw
  -pi/2) is on entry 1's side of the slider [13] (001BB860, NM, room move
  id 1: entries 2 / 1). Its Use opens the HEALING page's prompt for item
  0x20; Yes runs 002160B0's kind 2 (BM: health 100, the infection target
  0, the device hand-off 0015C750). Measured (a15b_03): the slider facing
  2.48 from entry 2 (D_00810702 = 1 at f447), the bed's Use from (970.4,
  240, 855.5) at f1160, Yes f1396, item 0x20 taken f1399, health 44 -> 100
  and infection 70 -> 0 at f1731. Away from the bed the same item is
  refused (exploration, section 6).
- **AREA19 sub 1 after the event: two creatures.** 001B6660 (BM) spawns a
  deferred record of condition 6 only when the story byte D_00810758 +
  (+2 >> 8) is 0xFF and, with flag 0x20 (D_00810778) = 0xFF, the record's
  +8 has bit 7 set (with flag 0x20 not 0xFF, bit 7 clear).
  Sub 1's deferred records 15 and 16 (condition 6, +2 = 0x2378 / 0x2379:
  flag 0x23; +8 = 0x81) are two 0012E3A0 creatures at (882.4, 370.8,
  833.1) and (977.4, 370.8, 876.3); records 17..20 (the same condition)
  are four 0012A5D0 nodes high in the hall. Measured (a19e_00 first row):
  the two creatures are live with +0x34 = 180 (the a19d creatures had
  100). The run kept clear of them: down the tower, door [49] (room move to
  entry 3), west along the tower wall, round the ladder's column, up the
  ladder at (892, 929.5) and onto the ceiling (as a19d_01). They did not
  follow onto the ladder.
- **The truck [46]** (0x829A70, C; model 0x21, at (841.4, 369.9, 956.3),
  yaw 0.778) and its cell (sub 1 placement [3], class 0x0B, key 5, kind 3,
  same origin). The grid shows a box rotated by 45 degrees: the roof (floor
  372, y 403.2; corners (827.1, 928.3), (813.6, 941.6), (841.6, 970.1),
  (855.2, 956.7)), the inside floor (388 / 389 / 396, y 380.6), its walls
  of attribute 0x52 (612..615; FOURTEENTH_LEVEL_ROUTE.md section 2 reads
  attribute-0x52 walls as query class 2, the creatures), and at its south-west end
  the step 401 (y 379.9) with the stair 404 (attribute 0x35) down to the
  hall. [46]'s state 0 with D_00810838 = 0 sets +8 = 1, +0x30 = 0x82F790
  (its descriptor: point (817.5, 379, 931.4), radius 5, height 6, yaw
  -2.0944, read from RAM) and +0xE |= 0xFF00. State 1: the Use (+0xB bit
  2) with +0xB bit 0 sets D_00810838 = 1, takes item 0x25
  (001C47E0(0x25, 1)), calls 0019C6F0(5, 0) (not read) and turns the two
  objects at *(D_00275B40 + 8) / + 0xC to -pi/2 / +pi/2; without bit 0 it
  plays script 0x82F690 (exploration: Cross at the Use point played a
  script, 3B8D 2 for about 200 frames, and left D_00810838 at 0). Bit 0 comes from
  the status screen: 00215870 (BM; STATUS_PAGES.md, ITEM > EVENT) asks 00185420 for a device of the item; 00184D20 (BM) accepts
  for item 0x25 a class-4 owner of model 0x21 (the shape-1 sphere of the
  descriptor and the pi/4 facing test); on Yes the device's +0xB = 5.
  Measured (a19e_02): over the roof on the ceiling, the drop onto it
  (y 403.2), south-west across it and off its edge onto the step (379.9),
  the Use point (817.5, 379.9, 931.4) facing 1.067; START (f1001), the
  hub's ITEM with the stick left (the hover byte D_00810141 is 4 only while
  the stick is held; up 3, right 2, down 1), the ITEM root's EVENT with the
  stick up and left (the ITEM hovers: down 1, right 2, up 3, up-left 4,
  left 5), the cursor down to the second entry (item 0x25), Cross, the
  prompt, Left, Cross (f1228): D_00810838 = 1 and item 0x25 taken at
  f1236.
- **The battery pack g[0]** (sub 1 deferred record 0, 00219550 NM, at
  (831.6, 380.1, 946.3); its item 0x1C, decomp WORLD_GRAPH.md's AREA19
  pickups) and **g[7]** (record 7, 0015AFA0, item 0x1F). 001C40B0 (NM)
  case 0x1C adds 0x24 half-units and raises D_00810CB7 to at least 0x24.
  Measured (a19e_03): g[0] taken f155 (charge 4 -> 0x24, capacity 0x0C ->
  0x24), g[7] f406 (item 0x1F, kept).
- **The panel [24]** (00158EC0, BM, model 0x22 at (1013, 280, 1126.9)):
  its state 0 stores +0x34 = 0x10 for model 0x22 (4 for model 0x14, the
  AREA02 panel); the BATTERY page 002149F0 (NM; FINDINGS "battery") costs
  2 * +0x34 half-units. Measured: with 4 half-units the prompt's Yes is
  refused (a19e_s1: the page's refusal state, D_00810854 and the charge
  unchanged); with 0x24 the charge drains 2 every 30 frames from 0x24 to 4
  (a19e_06 f930..f1380) and D_00810854 0x0C -> 0x0E (bit 1) at f1655.
- **Door [25]** (001BB860, NM, model 0x16, id 0x81, gate D_00810854 bit 1):
  AREA03 entry 1 sub D_00810730[3]. Measured (a19e_07): area bytes 03 00
  01 13 at f312, 03 00 01 03 at f319, AREA03 sub 0 entry 1 (610.5, 80,
  331.6), control at (601.2, 80, 340.9) f640.
- **The way between the truck and the tower.** North of the truck the
  enclosure of attribute-0x53 walls (459..464, y 370..435.5, round the
  floor 341 at y 394.5) meets the hall's north wall (z 985): no way east
  there (exploration). South of it drums (0x156620 nodes, +0x34 = 1) stand
  at (825, 905), (847, 921), (855, 914), (861, 904), (866, 910), (877.5,
  891), (850, 940), (865, 955) and (870, 949) (live pool of an exploration
  snapshot), and the lift [36]'s column fills x 800.7..868.8, z
  861.1..896. The kept a19e_04 walk passes between the truck's south-east
  face and the drums, south of the ladder's column, to door [49]
  (room move to entry 2, the tower). The creatures closed in: four hits
  (action 0x3E) raised infection 5 -> 25 with health unchanged.
- **AREA03 sub 0's lift** ([7] 0x825430 / [8] 0x8256E0, the car's doors;
  [9] 0x825980, [10] 0x825DF0, [11] 0x826340, [12] 0x8265D0, all C). The
  descriptors (read from RAM): [11] 0x828FF0 (545.3, 80, 407.9), radius 5,
  height 20, yaw -2.356 (the player faces 0.785); [9] 0x828BB0 (552.5,
  79.5, 416.8), yaw 0.785 (the player faces -2.356); [10] 0x828B90 is
  [9]'s twin at y -25. [11]'s Use plays script 0x828CD0 with D_00275CA0
  0 (0x828BD0 with it set). [7] holds its two objects' +0x7C at -5 / +5
  with D_00275CA0 set and [8] with it clear, each sliding over 60 frames
  when it changes (C: the doors). 0x826860 (C) inverts D_00275CA0; the
  census saw it first at a03_00 f1416 (after [11]'s Use at f1092, before
  [9]'s at f1900; its caller was not read, and the census keeps only the first frame). [9]'s Use plays
  script 0x8282D0, the ride, whose callback 0x826270 (C) moves to entry 3
  (y + 104.5) with D_00275CA0 set and to entry 2 (y - 104.5) with it clear;
  its D_00810702 = 2 at f2304 therefore means D_00275CA0 was 0 there, and
  the captured RAM reads 0 at a19e_07's and a03_00's ends. The kept a03
  sampler does not read 0x275CA0: D_00275CA0 = 1 after [11]'s Use was seen
  only in a deleted exploration run (exploration-only, not kept), and
  whether it went back to 0 before f2304 by a second inversion is not
  recorded. Measured (a03_00): [11] at f1092, through the
  doorway (grid 238) into the car (252), [9] at f1900; D_00810702 = 2 at
  f2304 (a room move: entry 2 (554, -25, 417.5)); control f2515.

## 3. Route table

Frames `f` are trace frames; `c` is the main-loop counter 0x70003B64.

| Beat (folder) | Source | Frames | Counters | Presses | What happens |
|---|---|---|---|---|---|
| `a15b_00_event` | a15_01 | 2,118 | 180769..182887 | - | stair 794, platform, stair 792, the mezzanine west: flag 0x23 = 1 f465 at (792.8, 315.1, 841.9); script 0x827400 f467..f1210 (D_008107FB 1); the count ends f2110 (D_008107FB 2); script 0x8277C0 f2113 |
| `a15b_01_return` | a15b_00 | 3,773 | 182897..186670 | Triangle f3322 | counter 0x23 = 3 f647; flag 0x23 = 0xFF and item C64 0x06 f3259 (player at (882, 330.5, 964)); the U.R.S. page (Triangle); area bytes 0F 00 00 0F f3390 (counter 0x23 = 0xFF); AREA15 sub 0 entry 0 f3740; [1]'s script f3743 |
| `a15b_02_scripts` | a15b_01 | 11,363 | 186676..198039 | Triangle f9339 | [1]'s script to f9276: flag 0x24 = 0xFF, counter 0x24 = 1, item 0x25; the EVENT page (Triangle); [2]'s script f9346..f11304: counter 0x24 = 2; control at (879.5, 240, 959) |
| `a15b_03_bed` | a15b_02 | 1,819 | 198041..199860 | Cross f292 (slider), f1157 (bed), Left f1374, Cross f1396 | the slider [13] facing 2.48: entry 1 f447; the bed [16] from (970.4, 855.5): the prompt, Yes: item 0x20 taken f1399; health 44 -> 100, infection 70 -> 0 f1731 |
| `a15b_04_door14` | a15b_03 | 1,264 | 199866..201130 | Cross f336, f746 | the slider facing -0.66: entry 2 f491; door [14] facing +z: 13 01 04 0F f911, 13 01 04 13 f918; control on the tower's y-450 landing (907, 450, 956) f1205 |
| `a19e_00_hall` | a15b_04 | 1,094 | 201135..202229 | Cross f889 | the flights down to y 370; door [49] from the tower side facing -z: room move, entry 3 (978, 370, 928) f1035 |
| `a19e_01_ceiling` | a19e_00 | 2,156 | 202230..204386 | Cross f447, f858, f1049 | west along the tower wall, round the ladder's column; a hit f350 (infection 0 -> 5); the ladder at (892, 929.5); the box; the ceiling hang west, south and north to (851.2, 426, 943.8) |
| `a19e_02_truck` | a19e_01 | 1,295 | 204387..205682 | Cross f497 (drop), START f1001, stick left + Cross f1037, stick up-left + Cross f1099, Down f1151, Cross f1173, Left f1206, Cross f1228 | over the roof, the drop (y 403.2), off the roof's south-west edge onto the step; the Use point facing 1.067; the EVENT page, item 0x25, Yes: **D_00810838 = 1**, item 0x25 taken f1236 |
| `a19e_03_battery` | a19e_02 | 501 | 205684..206185 | Cross f100, f351, Triangle f218, f469 | g[0]: item 0x1C, charge 4 -> 0x24, capacity 0x0C -> 0x24 f155; g[7]: item 0x1F f406 (kept: health 100) |
| `a19e_04_tower` | a19e_03 | 1,287 | 206186..207473 | Cross f1062 | out by the step and the stair; between the truck and the drums; four hits (infection 5 -> 25); door [49] facing +z: room move, entry 2 (978, 370, 955) f1228 |
| `a19e_05_down` | a19e_04 | 1,082 | 207475..208557 | Cross f223 | the pocket; the ladder 694 down: 13 00 0B at y 354 f389; sub 0 entry 0xB, the deck (907, 265, 969.5) |
| `a19e_06_panel24` | a19e_05 | 1,729 | 208558..210287 | Cross f686, Left f904, Cross f926 | the deck east and north (two hits: health 100 -> 90 -> 80); the panel [24] facing +z; the prompt, Yes; the charge 0x24 -> 4 (f930..f1380); **D_00810854 = 0x0E** f1655 |
| `a19e_07_door25` | a19e_06 | 699 | 210288..210987 | Cross f157 | door [25] facing +z: 03 00 01 13 f312, 03 00 01 03 f319; **AREA03 sub 0 entry 1**; control (601.2, 80, 340.9) f640 |
| `a03_00_elevator` | a19e_07 | 2,574 | 210988..213562 | Cross f1089, f1897 | a hit f100 (80 -> 70); the lift's outer button [11] facing 0.785: doors open; into the car, [9] facing -2.356: the ride, room move to entry 2 (554, -25, 417.5) f2304; control f2515 |
| `a19e_s0_down694` (side) | a15b_04 | 1,746 | 201132..202878 | Cross f882 | the flights down, the pocket, the ladder 694 down: 13 00 0B f1048; the deck (907, 265, 969.5) |
| `a19e_s1_panel` (side) | a19e_s0 | 1,282 | 202879..204161 | Cross f672, Left f890, Cross f912, Triangle f1245 | two hits (100 -> 80); the panel [24] with 4 half-units: the prompt, Yes, refused (the page's refusal state); D_00810854 0x0C and the charge unchanged |

Main line a15_01 -> a15b_00 .. a15b_04 -> a19e_00 .. a19e_07 -> a03_00:
32,754 frames, counters 180769..213562 (each beat starts 1..10 counters
after the previous beat's last: the resume).

## 4. Story bytes, health and items (measured)

| Byte | Before | After | Where |
|---|---|---|---|
| D_0081077B (flag 0x23) | 0 | 0xFF | 1 a15b_00 f465; 0xFF a15b_01 f3259 |
| D_008107FB (counter 0x23) | 0 | 0xFF | 1 a15b_00 f1210; 2 f2110; 3 a15b_01 f647; 0xFF f3390 |
| D_0081077C (flag 0x24) | 0 | 0xFF | a15b_02 f9276 |
| D_008107FC (counter 0x24) | 0 | 2 | 1 a15b_02 f9276; 2 f11304 |
| D_00810730[0, 1, 2, 4, 6] | (not sampled) | 0x82, 0x81, 0x82, 0x81, 0x81 | by 0x823C80 (C), with the request |
| D_00810838 (the truck's door) | 0 | 1 | a19e_02 f1236 |
| D_00810854 (AREA19 lock byte) | 0x0C | 0x0E | a19e_06 f1655 |
| D_00810CB2 / CB7 (charge / capacity, half-units) | 4 / 0x0C | 4 / 0x24 | 0x24 / 0x24 a19e_03 f155; drained to 4 a19e_06 f930..f1380 |
| D_00810700..703 | 0F 01 00 0F | 03 00 02 03 | 0F 00 00 0F a15b_01 f3390; 0F 00 01 0F a15b_03 f447; 0F 00 02 0F a15b_04 f491; 13 01 04 0F / 13 01 04 13 f911 / f918; 13 01 03 13 a19e_00 f1034; 13 01 02 13 a19e_04 f1227; 13 00 0B 13 a19e_05 f389; 03 00 01 13 / 03 00 01 03 a19e_07 f312 / f319; 03 00 02 03 a03_00 f2304 |
| items (D_00810C64 + id) | | | 0x06 +1 a15b_01 f3259; 0x25 +1 a15b_02 f9276, -1 a19e_02 f1236; 0x20 -1 a15b_03 f1399; 0x1C +1 a19e_03 f155; 0x1F +1 f406 |
| health (D_008104D0) | 44 | 70 | 100 a15b_03 f1731; 90 / 80 a19e_06 f249 / f542; 70 a03_00 f100 |
| infection (player +0x228) | 70 | 25 | 0 a15b_03 f1731; 5 a19e_01 f350; 10 / 15 / 20 / 25 a19e_04 f413 / f525 / f622 / f734 |

Items by code (CC3 lies outside the rows): CC3 0x0E (0x823C80), CC3 0x0F
(0x824350). Rounds held: 2 throughout (D_00810CB4).

The scenes: [4]'s first script shows a creature on the mezzanine; its
second ends with the U.R.S. page listing a new upper part (a receiver for
the marines' identity tags, by its page entry); [1]'s script is a long
conversation in the medical room; [2]'s a shorter one. The panel's page
shows the charge counting down as the battery powers the panel.

## 5. Census

**Tool** (`../Extermination/tools/route_census.py`, additive): `run
--segments a15b --pass A15B` replays each a15b beat from its recorded
source snapshot with route_capture's own beat function and one-shot
breakpoints on every boot function and on the AREA15 overlay (id 12; the
sub change inside a15b_01 keeps the overlay); a15b_04 (AREA15 -> AREA19)
runs under the `OverlayCensusSession` (boot + AREA15 armed, AREA15's
remaining breakpoints swapped for AREA19's, id 16, at the first frame
boundary where the resident id changes). `run --segments a19e --pass A19E`
and `run --segments a19e_s0_down694,a19e_s1_panel --pass A19E` arm boot +
AREA19; a19e_07 (AREA19 -> AREA03) swaps AREA19's for AREA03's (id 4).
`run --segments a03 --pass A03` arms boot + AREA03. `fifteenth-delta` runs
`chain_delta` over all three groups against every earlier census pass:
the first-level census, beat 15, the A01, A00, A01R, A02, A04, A22, A01U
and A06 passes, the eighth- to eleventh-level groups, A13D, a19b (A19B,
A19BX), A19C, the fourteenth level's A19D and A15, **and the first level's
C10 capture passes** (AIM, BR / BRB, DMG / DMGB, OPT / OPTB, EXIT / EXITB,
82 run files), once for each overlay (AREA15, AREA19, AREA03). Output:
`../Extermination/build/s87/census/a15b_delta.json`, `a19e_delta.json`,
`a03_delta.json` (and `runs/A15B/`, `runs/A19E/`, `runs/A03/`).

```sh
# decomp repo, .venv python, repo root (hidden PCSX2; hold build/.pcsx2.lock for `run`)
.venv/bin/python tools/route_census.py run --segments a15b --pass A15B
.venv/bin/python tools/route_census.py run --segments a19e --pass A19E
.venv/bin/python tools/route_census.py run --segments a19e_s0_down694,a19e_s1_panel --pass A19E
.venv/bin/python tools/route_census.py run --segments a03 --pass A03
.venv/bin/python tools/route_census.py fifteenth-delta
```

**Replay fidelity.** All 16 replays completed their beat's own checks
(`beats_incomplete` empty, first attempts). Rows are compared by the
main-loop counter; the start offset is the replay's first counter minus
the recording's.

| Beat | Frames replay / recorded | Start offset | Rows identical by counter | Overlay swap |
|---|---|---:|---|---|
| a15b_00_event | 2,118 / 2,118 | 0 | 2,119 / 2,119 (exact) | - |
| a15b_01_return | 3,779 / 3,773 | -6 | 3,774 / 3,774 (exact) | - |
| a15b_02_scripts | 11,365 / 11,363 | -2 | 11,364 / 11,364 (exact) | - |
| a15b_03_bed | 1,818 / 1,819 | 9 | 1 / 1,811 | - |
| a15b_04_door14 | 1,265 / 1,264 | -5 | 254 / 1,261 | after f915: id 12 -> 16 (40 removed, 49 armed) |
| a19e_00_hall | 1,131 / 1,094 | -3 | 3 / 1,095 | - |
| a19e_01_ceiling | 2,155 / 2,156 | 0 | 1,097 / 2,156 | - |
| a19e_02_truck | 1,296 / 1,295 | 7 | 0 / 1,289 | - |
| a19e_03_battery | 501 / 501 | 5 | 0 / 497 | - |
| a19e_04_tower | 1,179 / 1,287 | 0 | 3 / 1,180 | - |
| a19e_05_down | 1,084 / 1,082 | -1 | 289 / 1,083 | - |
| a19e_06_panel24 | 1,636 / 1,729 | 0 | 239 / 1,637 | - |
| a19e_07_door25 | 699 / 699 | 0 | 700 / 700 (exact) | after f315: id 16 -> 4 (36 removed, 23 armed) |
| a19e_s0_down694 | 1,780 / 1,746 | 0 | 6 / 1,747 | - |
| a19e_s1_panel | 1,282 / 1,282 | 0 | 1,283 / 1,283 (exact) | - |
| a03_00_elevator | 2,574 / 2,574 | 0 | 1,954 / 2,575 | - |

The beats are closed loop, so the other replays are different walks of the
same route shape (a19e_04's replay reached the tower 108 frames sooner);
as for the earlier levels, the census sets are those of the route shape,
not of the exact frames.

**Totals.** Executed: 1,473 boot functions in all three deltas, 25 of
AREA15's 29 overlay functions, 24 of AREA19's 43 and 15 of AREA03's 19. No
hit is unattributed. **New in any census pass (the C10 passes included):
51 functions, 32,080 bytes**: 30 boot (18,272 bytes; byte-matched C 20,
NEARMISS 6, asm word 3, C non-perfect 1), 6 AREA15 overlay (1,716 bytes,
C), 15 AREA03 overlay (12,092 bytes; C 14, asm word 1: 0x823540) and no
AREA19 overlay function. None of the 30 boot functions ran in a C10 pass
(the deltas' `already_ran` sources name no `c10_*` pass).

Functions new in any pass (census frames; the captions name the beat's
event at that frame, not the function's role):

| Functions | Status | Subsystem | First |
|---|---|---|---|
| overlay AREA15 0x823B40, 0x823C80, 0x823F60 | C | overlay_AREA15 | a15b_00 f1210 / f2110 / f2115 (the count, its end, script 0x8277C0) |
| 00153B50 | BM | entity_logic | a15b_00 f2115 |
| 00153EA0, 001CB200, 001CB2B0, 001D4440, 001D4640, 001D6580 | BM 5, NM 1 | entity_logic, anim_runtime, render_vif | a15b_01 f645..f654 (script 0x8277C0) |
| 00217090 | NM | ui_screens | a15b_01 f3294 (the U.R.S. page) |
| overlay AREA15 0x824240, 0x824350, 0x8243E0 | C | overlay_AREA15 | a15b_01 f3748 ([1]'s script); a15b_02 f9278 ([2]'s), f11307 (after it) |
| 0012F6C0 | BM | lowmem | a19e_01 f91 |
| 0015A750 | NM | entity_logic | a19e_01 f350 (a hit) |
| 00181F60 | NM | actor_anim | a19e_02 f136 (the drop) |
| 0020F170, 0020F2A0 | BM | ui_screens | a19e_02 f1064 (the status screen) |
| 00184D20, 00185420 | BM | actor_anim | a19e_02 f1177 (the EVENT page's device lookup) |
| 001A91C0, 001C2690 | BM | unknown_01, math_vector | a19e_s1 f249 / f252 (a hit on the deck; side) |
| 0013D2D0, 0013D490, 0013D600, 001BEB70, 001BEC40, 001BECC0, 001BF3C0 | asm word 1, BM 4, NM 2 | entity_logic, math_vector | a19e_07 f526..f527 (AREA03 sub 0's load) |
| overlay AREA03 0x823540, 0x823930, 0x824D30, 0x825290, 0x825430, 0x8256E0, 0x825980, 0x825DF0, 0x826340, 0x8265D0 | C 9, asm word 1 | overlay_AREA03 | a19e_07 f525..f527 (the load: the lift's [7]..[12], the deferred group members) |
| 0013D850, 00140C30, 00140F80, 001418F0, 00141A30, 00141C90 | asm word 2, BM 3, C non-perfect 1 | entity_logic | a03_00 f2516 (the ride's end, entry 2) |
| overlay AREA03 0x8268B0, 0x826880, 0x826860, 0x826330, 0x826270 | C | overlay_AREA03 | a03_00 f1391..f2304 (the lift's script callbacks) |

To check a port translation, grep the port (`src` and `tools`,
case-insensitive, `grep -rlI -i`) for the address string: boot functions
as 8 hex digits, overlay functions as 6. Measured 2026-10-02 with that
rule: **23 of the 51** addresses appear: 13 boot (0012F6C0, 00153B50,
00153EA0, 0015A750, 00181F60, 00184D20, 00185420, 001A91C0, 001C2690,
001CB2B0, 0020F170, 0020F2A0, 00217090), 5 AREA15 (0x823B40, 0x823C80,
0x824240, 0x824350, 0x8243E0: in the untracked `em_level14_port_area15.c`
of another lane) and 5 AREA03 addresses that are other areas' (0x823540,
0x823930, 0x825430, 0x825980, 0x825DF0). A match is only the address
string. No AREA03 overlay function is translated. Not referenced: 0013D2D0,
0013D490, 0013D600, 0013D850, 00140C30, 00140F80, 001418F0, 00141A30,
00141C90, 001BEB70, 001BEC40, 001BECC0, 001BF3C0, 001CB200, 001D4440,
001D4640, 001D6580, AREA15 0x823F60, and AREA03 0x824D30, 0x825290,
0x8256E0, 0x826270, 0x826330, 0x826340, 0x8265D0, 0x826860, 0x826880,
0x8268B0.

## 6. Tried and not kept (exploration runs, this session)

Scratch snapshots under `../Extermination/build/fifteenthcap/probe/`
(deleted at the session's end); leads, not ground truth.

- **The panel before the truck.** From the tower's pocket down the ladder
  694 and along the deck to the panel [24] with the 6-gauge pack (charge
  4): the prompt's Yes was refused (kept as the side beats a19e_s0 / s1).
- **The hall's creatures.** Entering the hall from door [49] and walking
  west along the tower wall, the player stopped at x 897.5 (the ladder's
  column) and both creatures (+0x34 = 180) killed him (two runs, health 44
  -> 16 -> 0). With 2 rounds held no attempt to shoot them was made.
- **The truck's cab enclosure.** A drop from the ceiling at (857.7, 967.4)
  landed on the floor 341 (y 394.5) inside the attribute-0x53 walls; every
  walk out of it was stopped (the player stayed at about (846..850,
  972..975)).
- **Beside the truck.** A drop at (851.4, 944.9) landed on a ledge at y
  382 and slid to the hall floor; the walk round the truck's south-west end
  to the stair 404 and up to the Use point worked once (exploration) and was
  killed once (the first capture of a19e_02: health 44 -> 39 -> 11 -> 0,
  one creature reached the stair's foot at (807.7, 926.4)).
- **The truck's door without the item page.** Cross at the Use point (the
  refusal script 0x82F690); D_00810838 unchanged.
- **Item 0x20 away from the bed.** The HEALING page refused it (its state
  6; health 44, inside the truck).
- **Health and infection (exploration only, not kept).** In a deleted
  exploration run at infection about 80 (AREA03 sub 0, reached by the panel
  route without the bed) health fell 22 -> 2 in about 1,300 frames with no
  bite seen, 2 at a time. The kept traces do not show this at infection 70:
  health stayed 44 through a15b_00..a15b_03 (about 17,000 frames) until the
  bed, and every kept loss is a hit (action 0x3E).
- **North of the truck.** A walk from the truck's west side north and east
  along the hall's north wall stopped at (827, 973) (the enclosure's walls
  463 / 464); a creature killed the player there.
- **AREA03 sub 0 to door [13].** From the lift's foot (553.9, -25, 417.5)
  west along the tunnel 213, the floor 214 and the stair 32 (attribute
  0x35, y -25 -> 0) toward the stair 33 (y 0 -> 25), the corridor 23 (y
  25) and door [13] (437, 25, 597.5): two of the six 0013D2D0 creatures of
  sub 0 (+0x34 = 60; at (495.1, -24.8, 455.6) and (432.1, -24.8, 455.1)
  at the load, four more at (294.2, 0.2, 474.4), (305.7, 0, 503.4),
  (439.3, 25.4, 532.6), (422.4, 25.2, 558.5)) followed and killed the
  player at the stair 32's top, (369.6, 0, 467.1) (health 70 -> 30 -> 10
  -> 0).

## 7. Open

1. **AREA03 to an exit.** Sub 0's door [13] (001BC350, id 0x85: AREA03
   entry 1 sub 1), sub 1's g[3] (0x823810, C: its Use sets flag 0x29 = 1)
   and [22] (0x8235A0, C: with flag 0x29 = 1 script 0x827170, at its end
   items C64 0x2A and CC3 0x12 and counter 0x29 = 0xFF), sub 1's slider [29] to sub 2, the
   socket s2[6] (00159E70, model 0x3C: item 0x2A) and the lift [7] to
   AREA08; or sub 1's seal [23] (00158430) and door [24] to AREA04 entry 2.
   The obstacle is sub 0's six 0013D2D0 creatures; the player holds 2
   rounds. Magazine pickups (item 0x10, +30 rounds by 001C40B0) on the
   route: AREA15 sub 1 g[0] (852.5, 290, 961.2, under the mezzanine) and
   AREA19 sub 0 g[0] (y 130, behind door [27]); neither was taken. The
   0013D2D0 and 0012E3A0 hit rules were not read.
2. **The truck's other pickup g[6]** (item 0x20 at (835.8, 380.4, 941))
   was not taken.
3. **The health loss with no bite** (2 at a time) was seen only in a
   deleted exploration run at infection about 80; none at 70 in the kept
   beats. Its rule (threshold or other condition) was not read.
4. **AREA19 sub 1's [53]** (0x827550: item 0x24) waits for flag 0x25
   (AREA07 / AREA08), as before.
5. Memory cards (closed): after this lane's last session
   `portable-data/memcards/Mcd001.ps2` / `Mcd002.ps2` carry mtimes
   2026-10-02 10:12 (PCSX2's shutdown), but their content is unchanged:
   SHA-256 757d6f77... / 47ebe237..., equal to the pre-copies recorded in
   decomp docs/GS_CONFORMANCE.md, and SHA-1 equal to
   `build/s87/c7cap/fb2/pre/portable-data_memcards_Mcd00{1,2}.ps2`.
