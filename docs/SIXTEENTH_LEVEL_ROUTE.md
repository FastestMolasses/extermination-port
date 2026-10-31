# AREA03 past the creatures, AREA08, and the way to AREA04 sub 1: sixteenth-level route, original ground truth

Lane L1 (capture), 2026-10-30 / 31. Original executable SHA-256
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

`FIFTEENTH_LEVEL_ROUTE.md` ended at a03_00 (AREA03 sub 0, the lift's foot): the
walk to door [13] had died at the top of the stair 32, and AREA03 had no recorded
exit. This document derives what lay beyond from the code and the grids and records
it as opt-in beat groups of `../Extermination/tools/route_capture.py`. Hidden
PCSX2 fork, exact one-frame steps, pad input only, no teleports and no memory
writes (the recorder rejects a write), one trace and one resumable snapshot per
beat. Nothing here is port behaviour.

In-game text is described, never quoted. Positions are world units; yaw is the
player's +0xC4 (X = sin, Z = cos). Frames `f` are trace frames, `c` the main-loop
counter 0x70003B64.

**Status.** See section 3 for the table and section 8 for where the recording stops.

- **AREA03 is recorded to both exits.** Door [13] to sub 1 (a03b_00); flag 0x29, items
  C64 0x2A and CC3 0x12 (a03b_03); the slider [29] and sub 2's lift to AREA08
  (a03b_05, a03b_06); the seal [23] and door [24] to AREA04 entry 2 (a03c_01, a03c_02).
- **AREA08 is recorded**: subs 0, 2 and 3, the switch [21], the magazine g[3], the
  crossing of sub 2's live floor, [13]'s script in sub 3 (flag 0x18 = 0xFF, counter
  0x18 = 0xFF, CC3 0x13) and the lift back (a08_00..a08_06). Sub 0's door [6] and the
  flag 0x17 scenes behind it were **not** played (section 6).
- **The recordings' player does not hold 2 rounds.** That figure was the v2.6.3
  chain. The fork chain the port uses arrives at a03_00 with 21 rounds in the gun
  and 21 held (D_00810C62 = D_00810CB4 = 21), item 0x10 = 2. No branch back for
  ammunition was needed; one magazine was taken in AREA08 (rounds 21 -> 51).
- Output: `../Extermination/build/fork_refs/levels-recordings-20261030-1/`
  (`s87/route_a03b/`, `s87/route_a08/`, `s87/route_a03c/`, each with `manifest.json`;
  the run log per beat and `progress.json` at the top; `_state_links/` holds the
  state links). Existing recordings and `build/startup-reference/` were not touched.

## 1. Tool and conventions

```sh
# macOS arm64 host (the fork runs x86_64 under Rosetta); from the decomp repo root, decomp .venv.
# record_level_routes.py is the port's recorder; --append adds beats to a set.
.venv/bin/python ../port-route16/tools/record_level_routes.py \
    --out-root build/fork_refs/levels-recordings-20261030-1 --append \
    --source-root build/fork_refs/levels-recordings-20261018-26 --beats a03b_00_door13
.venv/bin/python tools/route_capture.py events --beats a03b,a08,a03c    # needs the set under build/fork_refs/s87 to print
```

- Beats are declared at `SIXTEENTH_BEATS` in route_capture.py; `SIXTEENTH_GROUPS`
  names the groups, each writing `build/s87/route_<group>/<beat>/` (mirrored under the
  set). They are fork-only: no v2.6.3 capture exists, so no lead-in is replayed
  (`lead_in` 0, `tail` 0 in the manifests).
- Every kept snapshot was reloaded by `run_beat` and stepped (`resumes`).
- **Rows** (`use_sixteenth_sampler`): the a15b row plus `fl` (all story flags
  D_00810758..D7), `ct2` (D_00810818..57), `lk` (the lock bytes D_00810840..5F) and
  `cc` (D_00810CC0..FF). a03b_00 also samples the six creatures of sub 0
  (`A03B_OWNERS`, nodes 0x7A6DC0 .. 0x7A7C70).
- **Helpers:** `sixteenth_run` (full stick, grabs rocked off), `sixteenth_crawl_to`,
  `sixteenth_crawl_out`, `sixteenth_hatch_drop`, `sixteenth_hatch_in`, `sixteenth_heal`,
  `sixteenth_cut`, `sixteenth_melee_until`, `sixteenth_trav`, `flag`.
- **Determinism:** each beat is closed loop from its source; the same inputs from the
  same snapshot reproduced the exploration's positions and hit frames in every beat
  checked (a03b_00, a03b_06, a03c_01: identical health steps).

## 2. What decides each step (code, then the grids, then the capture)

Marks as in decomp docs/WORLD_GRAPH.md: BM byte-matched C, NM NEARMISS C, C overlay C
linked byte-identical. Grid node numbers are that RAM's (the header at *0x0028A598).

### 2.1 The 0013D2D0 creatures (hit rules)

- **Waking.** 001418F0 (BM): with the timer st+0x70 clear, a hit (e[0xA] bit 0) arms it to
  0xF0; otherwise 001B3F10 (BM) must pass for D_002753C8[difficulty] frames (90 at
  D_0081050C = 0). 001B3F10 for type 5: within +-45 degrees of the head's facing the
  radius is D_0024D740 (180), elsewhere D_0024D750 (80); |dy| <= 18; a clear ray. The
  timer falls when the player is beyond 70 units for 180 frames.
- **Attacks.** 0013DD40 (BM, state 4) attacks only inside 20 units: |dy| < 13.5 picks
  state 7 or 8 at random, else D_002458F0's table. State 8 (0013F240, NM) is the leap:
  20 health with D_0081070A clear (D_008104D4 = 20.0), and it moves the player. Measured
  -20 on every leap; each kept leap raised neither infection nor anything else.
- **Hit points and damage.** 0013D490 (BM): +0x34 = 60 (100 with bit 7 of +0xD, set when
  D_00810808 == 0xFF; 100 / 200 with D_0081070A). A round is `dmg 5` (001861C0, BM);
  001B41F0 (BM) on type 5 stores `5 | 0x8000` when the hit polygon's +0x1C is non-zero,
  else 5; 00141A30 (NM... C) multiplies a 0x8000 hit by 5. Measured (exploration, 30
  units, R1 lock): every hit took 5, so 12 rounds per creature; 25 of damage stunned it
  (e[4] = 2) for about 24 frames. A hit of 25 was not seen. The knife's +0x36 is 3
  (AIM_FIRE.md), so a melee is 3, or 15 with the 0x8000 bit.
- **Choice.** Six creatures at 12 rounds each did not fit the 21 rounds, and a leap needs a
  90-frame wake-up plus the approach. The route runs past them: a03b_00 reached door [13]
  with one 5-point steam contact (the rising hazard 001BECC0) and no creature hit.

### 2.2 AREA03 sub 0: the way to door [13]

The stair 32 (x 371.5..421.8, z 449..472) tops out on the floor y 0 at x 371. The block
x 367..417.5, z 471..502 (walls 219..221, full height) and the player-only walls 122..125
(attribute 0x51, read as query class 0 in COLL_MOVE.md; measured: the walk stops at
z 467 north of wall 123) close the east end of the room. The fallen beam (floors of
attribute 8, its walls start at y 3.9..7.2) runs from (365, 480) to (267, 505). So the
stair 33 (x 363.8..413.8, z 502..527) is reached only round the beam's west end, x < 266:
exploration walked straight south at (370, 467) and stopped there. This is why the
earlier walk died: it stood at the stair's top with the way closed.

### 2.3 AREA03 sub 1: the ducts

- The room of door [13] rises by the stair 165 (+z, y 25 -> 45), the platform 72, the stair
  164 (-z, y 45 -> 55) to the floors 68, 64 and 69. The attribute-0x37 square (x 491.6..497.5,
  z 732..739, y 55.1) is the duct's mouth: Cross facing +x from x >= 491.8.
- **The duct network has walls across it** (grid nodes 27 / 299 at x 647..648, z 732..739;
  29 / 155 at x 667, z 732..739; 28 / 156 at x 667, z 799..806; 22 / 157 at x 518, z
  837..844). Three separate parts, measured by crawling into each:
  1. West: the mouth, the junction (557, 735.5), north to the hatch over door [26]'s
     room, south to the junction (557, 802.5), east to the hatch over room A's west side,
     east along z 735.7 to the dead end at x 643.5 with g[11] (item 0x15; taken in
     exploration, not kept).
  2. Middle: the hatch on room A's east side, east to the junction (658, 802.5), north
     past the junction (658, 735.5) to the hatch into [22]'s room.
  3. East (not used): the hatch in the east room (721, 709), joined to z 802.5, with g[6]
     (item 0x1F, (670.5, 802.5)) beyond the wall at x 667.
- **Hatches.** Attribute-0x38 squares on the floor below. The crawl's end (action 0x2E)
  leaves the player hanging (0016D130 +5 0x18, +D 2: action 0x2C, clip 123). 00174FD0's
  quadrant of the stick decides: **stick down + Use** drops (quadrant 1, +6 0x14), Use
  alone does nothing (exploration); **stick up** climbs in (quadrant 0, +6 0xA). From the
  floor, Cross facing the wall jumps to the hang; the climb-in needs the stick up.
- **Turning** (`a01_turn_crawl`) recentres only inside an attribute-0x1F junction square;
  the crawl coasts 1.5 units after the stick is released, so `sixteenth_crawl_to` stops
  short (733.0 for the junction at 735.5). A stop past the square was seen at z 741.
- **[22]'s room** has no door. g[3] (0x823810, C) stands inside the player-only hull 569..573:
  it is used from (649.8, 684) facing -z. Measured: D_00810781 = 1 f100; counter 0x29 = 1
  f173; script 0x827170 to f3186, then flag and counter 0x29 = 0xFF, items C64 0x2A and CC3
  0x12 (0x8235A0, C); the page closes on Triangle.

### 2.4 AREA03 sub 1: the corridor's curtains

001C1030 (BM; class 4, type 0x51, y 45, +0x34 = 1) nodes at (639.6, 45, 646.9) and (677,
45, 646.9) stop the player at x 632 and x 669 although they hang 20 above the floor.
00189EC0 returns 2 for type 0x51 and 00189FE0 sets +0x36 |= 0x1000, and 001C1030 counts
only hits carrying 0x1000 (a bullet's +0x36 has it clear). One light melee (Circle) from
7.5 units cuts one (+4 = 2) and the walk passes at once. **They stand again when sub 1
reloads** (the second visit, a03c_01: node 0x7A8830 first, from the east).

The player-only wall 444 (x 616.5..662.5, z 654.2) keeps the walk north of z 654 there.

### 2.5 AREA03 sub 2 and the lift

Sub 2 holds no creatures. The socket s2[6] (00159E70, BM) needs no Use: at the sub's load
it sees item 0x2A and sets D_00810844 bit 7 (a03b_05 f1312; item 0x2A is not spent). The bar
28 (x 780.5..783.5, z 661..687, y 25..50) stops a straight walk at x 776: the way is round
its south end. The outer button [10] (001BDFC0) from (834, 677) facing +x; the inner
button [11] from (865.5, 650) facing -z; request 08 FF 00 01; AREA08 entry 0.

### 2.6 AREA08 sub 0

- The lift's car door [25] stays open at the arrival; on the way back it is closed and
  stops the walk at z 677.5 until the outer button [27] is used from (395, 674.5) facing +z.
- Door [17] (hinge (359, 649.5), yaw -pi/2) takes Use from (366, 646), not (366, 654):
  the door's centre is 4.5 from the hinge along its local x (Cross refused at 654, taken at
  646, as door [16] in sub 3: hinge (181, 90.5), yaw +pi/2, taken at z 95.3 and at z 86 Cross
  took the pickup g[1] instead).
- **The boxes** (001551B0, model 0x1E, about 14 wide) close the west room from z 620 to z
  675 at x 310..333. [41] from the east, then the next melee broke [38] and [42]; the
  stacked [40] and [43] fell (their +0x0A alert; 15 units down) and broke. [37] holds the
  pickup g[3] (00219550, item 0x10): rounds 21 -> 51, item 0x10 = 3.
- **Bugs** (0012A5D0, +0x34 = 15; four in the lift room and west room). R1 and Circle at them
  cost 19 rounds for 2 kills (exploration, not kept): they move across the lock. A bite adds
  10 infection and no health; the rock frees (A19D_GRABS). The kept beats run.
- The switch [21] (0015A070) from (270, 660) facing -x: D_00810849 bit 1 (a08_02 f308).
- **The north corridor is not reached from the west room**: the nook x 318..358, z 592..620
  is closed by the wall z 591.5. The way to the slider [19] is door [17] back, the corridor
  x 362..388 north and z 578 west.

### 2.7 AREA08 sub 2: the live floor

Floor nodes 74 / 76 (attribute 0x0A, y 221; x 334.5..367 and x 235..367) cost 8 health about
every 110 frames while walked: exploration crossed on foot from 90 to 18. Over them: the box
(335..345, 292..307) by a ledge climb from the safe floor south of z 307.5; its
attribute-0x3A pad and the attribute-0x1E ceiling (hand over hand, +5 0x12); the ceiling
stops at z 246.5 where the attribute-0x36 edge (node 129, z 243.9..246.6) turns the hang
into the swing (0016A4B0, action 0x28); with the stick held 120 frames, Cross releases and
the player lands on the next box (341.5, 235.1, 216.5). Its pad, the ceiling east to x 355
(x 335..350 has no ceiling north of z 212), north, then west at z 162..158 to x 228, a drop
onto the safe floor west of x 235. Health unchanged. After a08_05 (D_008107F0 = 0xFF) the
same floor is harmless: 0x823AC0 (C) ends at its load and a08_06 walks it with no health change.
Which code reads attribute 0x0A was not read.

### 2.8 AREA08 sub 3

The rail at z 125..126 forces the stair 52 (attribute 0x35, x 153..183, y 220 -> 250) from its
west end. Door [16] from (188.5, 94.5) facing -x; [13] (0x8245E0, C) from (161.5, 81) facing
-z. D_0081076D (flag 0x15) is not 0xFF, so its half 0x824790 (C) ran: script 0x826420, flag
0x18 1 (a08_05 f955) then 0xFF, CC3 0x13, counter 0x18 = 0xFF (f1474). **Flag 0x25 was not set**
(the other half, 0x824870, needs flag 0x15: whichever of AREA07 / AREA08 is second sets it).

### 2.9 AREA03 sub 1, second visit, and AREA04

The creatures respawn at their load positions and the curtains stand. The run west took
three leaps (health 90 -> 70 -> 45 -> 25) and one steam contact (-5). The seal [23] (00158430,
BM): one melee from (235, 633.5) facing -x: D_00810844 bit 0 and D_00810845 bit 2 (a03c_01
f1650). Door [24] (hinge (228.9, 641.5), yaw -pi/2) from (235, 637): AREA04 entry 2, sub 1
(D_00810730[4] = 0x81 since 0x823C80), (332, 54.9, 365.5). Infection reached 100 on the way
(a03c_02 f109).

## 3. Route table

| Beat | Source | Frames | Counters | Presses | End |
|---|---|---:|---|---:|---|
| `a03b_00_door13` | a03_00_elevator | 1,574 | 217003..218577 | 1 | sub 1 entry 1 (432, 25, 625.6); health 65 |
| `a03b_01_duct_west` | a03b_00 | 1,976 | 218577..220553 | 2 | room A (584.8, 25, 802.4), entry 0; health 45 |
| `a03b_02_duct_east` | a03b_01 | 1,284 | 220553..221837 | 2 | [22]'s room (657.9, 25, 707.1), entry 7 |
| `a03b_03_item2a` | a03b_02 | 3,697 | 221837..225534 | 9 | flag 0x29 = 0xFF, C64 0x2A, CC3 0x12; item 0x1F used, health 100 |
| `a03b_04_back` | a03b_03 | 2,612 | 225534..228146 | 4 | door [26]'s room (557, 25, 707.6), entry 2 |
| `a03b_05_slider` | a03b_04 | 1,369 | 228146..229515 | 4 | curtains cut; sub 2 entry 0 (770, 25, 672) |
| `a03b_06_lift` | a03b_05 | 1,770 | 229515..231285 | 2 | **AREA08** entry 0 (381, 220, 700) |
| `a08_00_door17` | a03b_06 | 405 | 231285..231690 | 1 | entry 3 (347, 220, 644.5) |
| `a08_01_magazine` | a08_00 | 1,185 | 231690..232875 | 11 | rounds 51, infection 60 |
| `a08_02_switch` | a08_01 | 371 | 232875..233246 | 1 | D_00810849 = 2 |
| `a08_03_slider` | a08_02 | 1,222 | 233246..234468 | 2 | sub 2 entry 0 (350, 220, 326.5); health 90 |
| `a08_04_ceiling` | a08_03 | 2,759 | 234468..237227 | 7 | sub 3 entry 0 (191, 220, 165) |
| `a08_05_terminal` | a08_04 | 1,474 | 237227..238701 | 2 | flag 0x18 = 0xFF, counter 0x18 = 0xFF, CC3 0x13 |
| `a08_06_back` | a08_05 | 4,072 | 238701..242773 | 6 | **AREA03** sub 2 entry 1 (863.7, 25, 661.6); infection 85 |
| `a03c_00_slider` | a08_06 | 1,243 | 242773..244016 | 2 | sub 1 entry 6 (732.9, 25, 672) |
| `a03c_01_seal` | a03c_00 | 1,680 | 244016..245696 | 3 | D_00810844 = 0x81, D_00810845 = 0xAC; health 25 |
| `a03c_02_door24` | a03c_01 | 804 | 245696..246500 | 1 | **AREA04** sub 1 entry 2 (332, 54.9, 365.5); infection 100 |

Main line a03_00 -> a03b_00..06 -> a08_00..06 -> a03c_00..02: 29,497 frames, each beat starting
on the previous beat's last counter.

## 4. Story bytes, health and items (measured)

| Byte | Before | After | Where |
|---|---|---|---|
| D_00810781 (flag 0x29) | 0 | 0xFF | 1 a03b_03 f100; 0xFF f3186 |
| D_00810801 (counter 0x29) | 0 | 0xFF | 1 f173; 0xFF f3186 |
| D_00810770 (flag 0x18) | 0 | 0xFF | 1 a08_05 f955; 0xFF f1474 |
| D_008107F0 (counter 0x18) | 0 | 0xFF | a08_05 f1474 |
| D_00810844 (AREA03 locks) | 0 | 0x81 | bit 7 a03b_05 f1312; bit 0 a03c_01 f1650 |
| D_00810845 (AREA04 locks) | 0xA8 | 0xAC | bit 2 a03c_01 f1650 |
| D_00810849 (AREA08 locks) | 0 | 2 | a08_02 f308 |
| items C64 | | | 0x2A +1 a03b_03 f3186; 0x1F -1 f3494; 0x10 +1 a08_01 f1095 |
| items CC3 | | | 0x12 +1 a03b_03 f3186; 0x13 +1 a08_05 f1474 |
| health | 70 | 25 | 65 a03b_00; 45 a03b_01 f284; 100 a03b_03 f3660; 90 a08_03 f580; 70 / 65 / 45 / 25 a03c_01 f496 / f656 / f1097 / f1282 |
| infection | 30 | 100 | 40 / 50 / 60 a08_01 f630 / f902 / f993; 75 / 85 a08_06 f760 / f2735; 100 a03c_02 f109 |
| rounds | 21 | 51 | a08_01 f1095; the magazine stays 21 |

## 6. Tried and not kept

Scratch under `../Extermination/build/route16cap/` (deleted at the end); leads, not ground truth.

- **Shooting the creatures.** R1 lock from 34 units in AREA03 sub 0: 12 hits, 5 each, a leap
  (-20) between. R1 from 44 units in sub 1's corridor: 5 rounds, no hit recorded, a leap, and
  the stance's reaction cost about 300 frames and took health 90 -> 40, infection 85 -> 100.
- **Walking sub 2's live floor**: reached sub 3 at health 18.
- **Going straight south from the stair 32's top**: stopped at (370.5, 466.8).
- **The door [6] of AREA08 sub 0 from the west room** (291.5, 625): not reached in the kept
  order; a walk from the switch stopped at (275.5, 659) on the hull 520..523 and took two bites
  (infection 60 -> 80). Its scenes were not played (section 7).
- **Crawling east at z 735.7 and z 802.5**: stopped at x 643.5 and x 663.7 by the walls above.
- **Use while hanging from a hatch**: refused without the stick down.

## 7. Open

1. **AREA08 sub 0's door [6]** (0x823870, C): its Use plays script 0x825880 (flag 0x17), [12]
   0x824210 then script 0x825AA0 and the group 0x825420; the door then works, and at entry 6
   script 0x825E60 gives CC3 0x63. Not played. No code outside AREA08 tests flag 0x17 by
   001BA1C0 (the graph's scan), so it did not block the route; that scan does not see every
   direct read of D_0081076F (see the file list in `grep D_0081076F src`).
2. **Pickups not taken** in AREA08 sub 3's pit (C64 0x21 g[4], C64 0x15 g[2], C64 0x12 g[3] / g[5]);
   how the player reaches the pit floor (y 130) was not looked at.
3. **Which code hurts on attribute 0x0A and why D_008107F0 ends it** was not read; only the 8-point
   contacts and their end were measured.
4. **Lock with bugs**: why 19 rounds gave 2 kills was not analysed.
