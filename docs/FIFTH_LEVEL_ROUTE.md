# AREA04: fifth-level route, original ground truth

Lane "NEXT" (capture), 2026-09-28 (session s88). Original executable SHA-256
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

AREA02's progression exit (FOURTH_LEVEL_ROUTE.md, beat a02_05) ends in AREA04
(overlay id 5) sub 0 at spawn entry 0, with control at (440.1, 14.9, 356.4)
facing pi and D_008107E4 = 1. This document records what the original does
from there to AREA04's progression exit, the area change into AREA22. The
tool, the conventions and the row format are those of SECOND_LEVEL_ROUTE.md,
THIRD_LEVEL_ROUTE.md and FOURTH_LEVEL_ROUTE.md: hidden PCSX2, exact one-frame
steps, pad input only, no teleports and no memory writes, one trace and one
resumable snapshot per beat. Nothing here is port behaviour.

**Status: the progression exit is reached.** Main line a04_00..a04_05: the
director's event at door [45] unlocks door [40]; behind it, in the store
room, console [69] sends the cable reel [70] down the conveyor that it was
blocking; the conveyor leads up to the east balcony and door [37], the area
change to AREA22 entry 0 and control there. 6 beats, 8,659 frames, counters
62480..71145. Side beats: a04_s0 (door [45], locked), a04_s1 (the reel
blocks the conveyor before the console), a04_s2 (a pickup in the store room)
and a04_s3 (door [42] to AREA20, locked). Section 9 has the census replays
and the a04 delta.

"Main line" here means the route the capture found from the arrival to the
first area change that the original allows: of AREA04's five area-change
doors, only [35] (back to AREA02) and [37] (to AREA22) are not lock-gated in
this state. The capture reached [37] only up the conveyor, after the
console had moved the reel away (section 2.4). That is the only way up the
capture found; it is not proven the only one: the ladder to the tower top
and the lifts [51] (001BD560 at (561.2, 54.9, 260), beside the east
balcony), [56], [62] and [65] were not tried (section 8).
Whether the story continues through AREA22 (whose door [8] leads to AREA01
entry 6) was not played; section 8 lists what is open.

In-game text (messages, prompts, title cards, item descriptions) is
described, never quoted. Positions are world units; yaw is the player's
+0xC4 (X = sin, Z = cos).

## 1. Tool, outputs and conventions

The beats are the opt-in group `a04` of `../Extermination/tools/route_capture.py`.
`--beats all` is unchanged (beats 00..14); the AREA01, AREA00, C7, revisit
and AREA02 groups are unchanged.

```sh
# decomp repo, .venv python, repo root
.venv/bin/python tools/route_capture.py run --beats a04              # every AREA04 beat, in order
.venv/bin/python tools/route_capture.py run --beats a04_02,a04_s1    # some beats (each from its source)
.venv/bin/python tools/route_capture.py events --beats a04_00        # change log of one trace
.venv/bin/python tools/route_capture.py verify --beats a04           # every snapshot resumes
```

- **Session, resuming, navigation, Use**: as SECOND_LEVEL_ROUTE.md section 1.
  a04_00 starts from `build/s87/route_a02/a02_05_progression_exit/state.p2s`.
  Every beat calls `next_long_frames` (FOURTH_LEVEL_ROUTE.md section 1).
- **Bugs.** AREA04's hall holds 0012A5D0 bugs. The beats walk with
  `a04_go`, which interrupts a walk when a bug grabs the player (action 0x3B,
  a bug on the back, released by rocking the left stick; action 0x3E, a
  bite, which plays out) and resumes it; `a04_use` repeats the approach and
  the press when a grab takes it. No grab happened on any kept beat.
- **Outputs**: `../Extermination/build/s87/route_a04/<beat>/` (ignored):
  `trace.json`, `state.p2s`, `eeMemory.bin`, `gs.bin`, `scratchpad.bin`,
  `original.png`, `snapshot.json`.
- **Row format**: the AREA02 row (FOURTH_LEVEL_ROUTE.md section 1: player,
  camera, request, fade, message, `hp`, `d9`, `f759`, `story758`, `story760`,
  `s784`, `area4`, `slots`, `bd8`, `ovl`, `taken`, `docs`, `msgrec`, `locks`,
  `wpn`, `d7dc`, `df`, `e0`, `e1`, `inf`, `infected`, `item20`) with the
  AREA04 owners (each with `cb` and `rot`), plus `story764`
  (D_00810764..67), `s830` (D_00810830..3F), `e4` (D_008107E4, the
  director's counter), `e5` (D_008107E5), `e9` (D_008107E9) and `ea`
  (D_008107EA). `events` logs these besides the earlier keys.

### 1.1 Owners sampled

`A04_OWNERS`, the pool nodes of the AREA04 sub-0 load, measured in the
a02_05 end snapshot; [n] is placement table 0x82A130 record n, g[n] group
0x826930 record n.

| Row key | Node | Callback | Record | Role on the route |
|---|---|---|---|---|
| `director_r1` | 0x7A9FB0 | overlay 0x823B90 (class 8) | [1] | D_008107E4, the event at door [45]; frees itself at the event's end (the node is then reused) |
| `npc_r2` | 0x7AA2A0 | overlay 0x824320 (class 0xAA) | [2] (400, 15, 100) | behind door [45] (not reached) |
| `r3_8246B0` | 0x7AA590 | overlay 0x8246B0 | [3] (322, 15, 224) | not used |
| `r4_824DC0` | 0x7AA880 | overlay 0x824DC0 | [4] (560, 69.9, 284) | not used |
| `door35_r35` | 0x7B0390 | 001BB860, model 0x09 | [35] (440, 14.9, 380.8), id 8\|0x80 | back to AREA02 (not used) |
| `door37_r37` | 0x7B0970 | 001BC350, model 0x03 | [37] (540.5, 54.9, 410), id 1\|0x80 | the progression exit (a04_05) |
| `door38_r38` | 0x7B0C60 | 001BC350, model 0x15 | [38] (320, 54.9, 360.5), id 2\|0x80 | to AREA03 (not used) |
| `door40_r40` | 0x7B1240 | 001BC350, model 0x15 | [40] (501.5, 14.9, 162), id 3 | room move, the store room (a04_01, a04_03) |
| `door42_r42` | 0x7B1820 | 001BC350, model 0x15 | [42] (560.5, 14.9, 153), id 4\|0x80 | to AREA20: locked (a04_s3) |
| `door45_r45` | 0x7B20F0 | overlay 0x823700, model 0x16 | [45] (440, 14.9, 109.3), id 5 | room move: locked (a04_s0) |
| `r47_823EE0` | 0x7B26D0 | overlay 0x823EE0 | [47] (440, 14.9, 109.3) | at door [45] |
| `lock_r48` | 0x7B29C0 | 001581A0 (class 0x44) | [48] (493.9, 22.6, 350.5) | beside door [49] (not used) |
| `door49_r49` | 0x7B2CB0 | 001BC350, model 0x15 | [49] (486.3, 14.9, 349.8), id 6 | room move (not used) |
| `lift_r51` | 0x7B3290 | 001BD560, variant 0x0B | [51] (561.2, 54.9, 260) | not used |
| `lift_r62` | 0x7B52E0 | overlay 0x825510, subtype 0x11 | [62] (317, 14.9, 260) | not used |
| `console_r69` | 0x7B6770 | overlay 0x825DF0 (class 0x84) | [69] (556.7, 29.9, 193) | the console (a04_02) |
| `reel_r70` | 0x7B6A60 | overlay 0x8260C0 | [70] (509, 54.9, 260) | the cable reel on the conveyor (a04_02, a04_s1) |
| `pick_g8` | 0x7A6DC0 | 0015AFA0 | g[8] (542.3, 20.6, 156.2) | store room (not taken) |
| `pick_g12` | 0x7A7690 | 0015AFA0 (class 0x87) | g[12] (514.3, 30, 192.9) | store room shelf (a04_s2) |

## 2. The level, from the original's own data

The tables are from `../Extermination/tools/area_overview.py --area 4 --ram
<snapshot>/eeMemory.bin --out <file>` over the a02_05 end snapshot (the
outputs were scratch and are regenerated by the same command). Code
descriptions are from the decomp's C. The AREA04 overlay C is the
uncommitted work of lane A04C in the decomp's working tree
(`../Extermination/docs/AREA04_OVERLAY.md`: 31 of the 32 functions have
byte-identical C; 00823500 is the asm entry pad); the boot functions' marker
is given where a claim depends on one (NEARMISS: body-correct, not
byte-identical).

### 2.1 Registries

- Overlay `AREA04.BIN`, header id 5, text 0x823540..0x826600, 31 functions
  from 39 splat pieces (one slot, 00825240, holds two functions; the second,
  runtime 0x825310, has no splat piece). Two sub-states: placement tables
  0x82A130 (sub 0, 90 records) and 0x82AF70 (sub 1, 88). Sub 0 defers groups
  0x826930 (26), 0x8268D0 (1) and 0x826600 (8); sub 1 group 0x826DE0 (24).
  Level data: INDEX sector 8 (`chunk08`, nested `chunk08.n0/.n1`).
- **Spawn table** (13 records, 0x30 bytes; the subs share positions and
  yaws). The rows the route used or names:

  | Entry | Position | Yaw | Used by |
  |---:|---|---:|---|
  | 0 | 440, 14.9, 370 | pi | AREA02 door [25]: the arrival (a02_05) |
  | 1 | 545.5, 54.9, 398 | pi | AREA22's door back (not played) |
  | 3 | 489.5, 14.9, 157 | -pi/2 | door [40], hall side (a04_03) |
  | 4 | 516.5, 14.9, 157 | pi/2 | door [40], store-room side (a04_01) |
  | 8 / 9 | 440.5, 14.9, 121 / 440.5, 14.9, 97 | 0 / pi | door [45] (not opened) |
  | 11 / 12 | 491.3, 14.9, 364.8 / 491.3, 14.9, 337.8 | 0 / pi | door [49] (not opened) |

- **Doors** (destination table D_0024E140[4] = 0x24E000, 10 records):
  id 1 = 16 00 00 00 ([37], AREA22 entry 0 sub 0, model 0x03); id 2 = 03 00 01
  01 ([38], AREA03 entry 0 sub 1; model 0x15 in sub 0, 0x03 in sub 1); id 3 =
  04 03 00 00 ([40], room move, entries 4 / 3); id 4 = 14 00 00 00 ([42],
  AREA20 entry 0); id 5 = 08 09 00 00 ([45], room move, entries 8 / 9; the
  overlay door 0x823700 in sub 0, 001BB860 in sub 1); id 6 = 0B 0C 00 00
  ([49], room move, entries 11 / 12); id 8 = 02 04 00 00 ([35], AREA02 entry
  4); ids 0, 7 and 9 are used by no placement. The lock byte is
  D_00810841[4] = D_00810845, bit = door id: 0x823700 tests it for every
  model (its C), 001BC350 model 0x15 is gated (measured here: [42] stays
  locked with bit 4 clear, [40] opens once bit 3 is set), and model 0x03
  is not ([37] opens with bit 1 clear). For 001BB860 FOURTH_LEVEL_ROUTE.md
  section 2.4 lists the gated models 0x16, 0x17 and 0x3E; [35] (model 0x09)
  was not played.
- **Geometry** (level collision, the RAM grid at D_0028A598, decoded as in
  `tools/export_collision.py`): the hall's floor is y 14.9 over x 345..500,
  z 150..380, with a sunken floor (y 10..14) at x 355..459, z 172..242. A
  tower stands at x 400..460, z 238..300; its north face carries a ladder
  (attribute 0x32, x 435..445, z 282.1, y 15 -> 67). The conveyor runs east
  along the tower's south side: the walk steps onto it at x 406 (y 18.9,
  a04_04), and a ramp (normal y 0.894) climbs over x 430..500, z
  249.5..270.5, from y 18.9 to 53.9, between side strips (attribute 4) at
  z 245..249.5 and 270.5..275; the wall under its north
  side (z 275, x 430..500) closes the hall's north half off from the south
  east of the tower. Balconies at y 54.9: west (x 325..345), south (x
  345..405, z 145..165) and east (x 535..555, z 145..410, widening to x 500
  at z 245..300, where the conveyor arrives); door [37] closes a corridor at
  the east balcony's north end. The store room behind door [40] is x
  502..560, z 140..200 at y 14.9, with a block at x 523..533, z 145..173.

### 2.2 Story and progress bytes

| Byte | Written by | Measured |
|---|---|---|
| D_008107E4 (counter 0xC) | 1: at the end of the arrival script 0x8275A0 (FOURTH_LEVEL_ROUTE.md a02_05 f1630; the director's C never writes 1, the record was not traced); 2: the director 0x823B90 | 1 from the a02_05 end; 2 at a04_00 f379 |
| D_00810845 (D_00810841[4]) | 0x823B40 (the callback of script 0x8278D0's op09 record 0x827B50): sets bit 3 once and calls 001FB9F0(0x3EE, ...) | 0x00 -> 0x08 at a04_00 f3484 |
| D_00810764 | at the end of script 0x8278D0 (the record was not traced) | 0 -> 0xFF at a04_00 f3550 |
| D_0081083B | the console 0x825DF0, 180 frames after its Use | 0 -> 0xFF at a04_02 f379 |
| D_00810CB9 | the pickup g[12] (0015AFA0) | 0 -> 1 at a04_s2 (0 in the a04_01 end dump it starts from, 1 in the a04_s2 end dump; the trace's inventory before/after agree); what the byte means is not established |
| D_00810700..703 | door [40]; door [37] | 04 00 04 04 at a04_01 f1767, 04 00 03 04 at a04_03 f851; 16 00 00 04 at a04_05 f323, D_00810703 = 0x16 at f334 |

D_008107E5, D_008107E9, D_008107EA, D_0081075F..63, D_00810784..87, item
0x20 (1), the taken bytes 0x810860..9F and the document bytes stay constant
on every beat. Health stays 90 and infection 60 (the a02_05 values) on every
beat; the infected latch (+0x234) stays 0.

### 2.3 The director (0x823B90, [1])

Byte-identical C (lane A04C). State 0 at load: with D_00810845 bit 5 set,
or with func_001BA1C0(self, 0xC) != 0 (that byte-identical boot function
tests D_00810758[0xC] == 0xFF, i.e. D_00810764 already 0xFF, so the event
has already ended), it sets D_00810764 = 0xFF and state 3 and ends; otherwise it starts script 0x8275A0 (the
arrival, when D_008107E4 is 0) or 0x8278D0. In state 1 with D_008107E4 = 0
it waits for the arrival script to end, then places the player at (440.1,
14.9, 356.4) facing pi (the a02_05 control position, measured), starts
script 0x8278D0, spawns group 0x8268D0 (one 0x824100 node at (440.7, 16.5,
105.2)) and calls 001FB0B0(0xF). With D_008107E4 = 1 or 2, sub-state 0
waits for the player at y <= 16 inside the quad 0x827CD0 (corners in RAM
order (x, z): (430, 137), (450, 137), (450, 115), (430, 115)): D_008107E4 =
2, 001FABB0, D_70003B84 = 0. Sub-state 1 waits for the script's end, places
the player at (440.4, 14.9, 114.4) facing 0, sets state 3 and calls
001C4760(6, 1) and 001FAE70(0).

### 2.4 What decides each step (code, then capture)

- **Door [40]** (001BC350 model 0x15, id 3): lock-gated on D_00810845 bit
  3, which the event sets (a04_00 f3484, the frame the director's script
  pointer moves past record 0x827B50).
- **The reel blocks the conveyor.** The reel [70] (0x8260C0) stands at the
  top of the conveyor (509, 54.9, 260). Walking up the conveyor before the
  console stops at (495.2, 51.5, 260.0) (a04_s1; both sides were tried in an
  exploration run and stop at z 247.3 and 272.5). With D_0081083B set at
  load, 0x8260C0's state 0 places the reel at (380, 14.9).
- **The console [69]** (0x825DF0, byte-identical C): its Use starts script
  0x82C3B0 and sets two countdowns, 120 and 60 frames. At the end of the
  first it calls 001FBD50(self, 0x19A, 0, 300) and sets the child it
  spawned at load (001C5570, kept at +0x2EC) to state 3; at the end of the
  second it calls 001FBD50(self, 0x455, 0, 300), sets D_0081083B = 0xFF and
  the reel's state to 1: measured at f379, 180 frames after the script
  started (f199). The reel (0x8260C0) then rolls down the conveyor (a04_02:
  x 509 at f379, 489.7 at f480, 446.7 at f720, 415.2 at f890). At the
  script's end the console sets the reel's state to 0x64, which places it
  at (380, 14.9, 260) in state 4: measured at f892 (the script pointer's
  last step is at f891..f892), with the reel at x 415.2 the frame before.
- **The conveyor.** With the reel gone the walk goes up it without a
  press: off the floor at (406.4, 18.9, 262.6) (a04_04 f232), at y 54.9 at
  (500.1, 54.9, 260.4) (f350).
- **Door [37]** (001BC350 model 0x03, id 1|0x80, record 16 00 00 00): not
  lock-gated; its Use plays the door program, request B5..B8 = 16 FF 00 01,
  and the area change.
- **Door [45]** (0x823700, byte-identical C, model 0x16, id 5): with
  D_00810845 bit 5 clear its Use calls the talk turn 0x823580, which starts
  script 0x8272A0 (a04_s0).
- **Door [42]** (001BC350 model 0x15, id 4|0x80): bit 4 clear, the
  locked-door program (clip 0x44 and one message, a04_s3).
- **The hall's two halves.** The wall under the conveyor's north side
  (z 275, x 430..500) separates the hall north of the conveyor from the
  part south-east of the tower: an exploration walk south at x 475 stopped
  at z 279.1. Both beats that cross the hall go round the tower's west side.

## 3. Route table

Frames `f` are trace frames; `c` is the main-loop counter 0x70003B64.

| Beat (folder) | Source | Frames | Counters | Presses | What happens |
|---|---|---|---|---|---|
| `a04_00_door45_event` | a02_05_progression_exit | 3619 | 62480..66099 | — | arrival -> west round the tower -> the quad at door [45]: **D_008107E4 = 2**, script 0x8278D0 (one message), **D_00810845 = 0x08**, D_00810764 = 0xFF, player at (440.4, 14.9, 114.4) |
| `a04_01_door40` | a04_00 | 1817 | 66100..67917 | Cross f350, f664, f978, f1292 (not taken), f1602 | door [40]: room move, **spawn entry 4** in the store room |
| `a04_02_console` | a04_01 | 941 | 67918..68859 | Cross f196 | console [69]: script 0x82C3B0, **D_0081083B = 0xFF**, the reel rolls down and is placed at (380, 14.9, 260) |
| `a04_03_back_to_hall` | a04_02 | 1014 | 68861..69875 | Cross f706 | door [40] from the store room: **spawn entry 3** in the hall |
| `a04_04_conveyor` | a04_03 | 552 | 69876..70428 | — | west round the sunken floor, up the conveyor, north along the east balcony to (541.8, 54.9, 393.8) |
| `a04_05_progression_exit` | a04_04 | 716 | 70429..71145 | Cross f66 (not taken), f158 | door [37]: **AREA22 entry 0**, overlay id 0x13, control f627 |
| `a04_s0_door45_locked` | a04_00 | 324 | 66100..66424 | Cross f78 | door [45], bit 5 clear: talk turn, script 0x8272A0, one message |
| `a04_s1_reel_blocks` | a04_00 | 437 | 66100..66537 | — | up the conveyor before the console: the walk stops at (495.2, 51.5, 260.0) against the reel |
| `a04_s2_pickup` | a04_01 | 357 | 67918..68275 | Cross f149, Triangle f301 | the pickup g[12] on the store room's shelf: item page, D_00810CB9 0 -> 1 |
| `a04_s3_door42_locked` | a04_01 | 473 | 67918..68391 | Cross f193 | door [42] (to AREA20), bit 4 clear: locked-door program, one message |

Main line a02_05 -> a04_00 -> ... -> a04_05: 8,659 frames. Every snapshot
was reloaded after its capture and resumed on the first check (no tail
idle). The counter gap between a04_02's end (68859) and a04_03's start
(68861) is the snapshot reload. The presses that were not taken were
repeated by the closed loop (`a04_use`): in a04_01 the player stood at
(495.0, 14.97, 162.2) facing 1.533 for the first four and at yaw 1.677 for
the fifth; why the first four were not taken was not traced.

## 4. The main line

### a04_00 the event at door [45] (3619 frames)

From (440.1, 356.4) facing pi: west along z ~350 to (385, 330), south past
the tower's west side (x 380..390) and over the sunken floor to (440, 150),
then south into the quad. At f379 (62859) the player is at (439.3, 14.9,
136.4): D_008107E4 = 2, 3B8D = 2 (f380), action 0x41 (f381), a fade (f380..
f461), the letterbox (f444), the camera in its cinematic mode (cam +4 = 3)
and one message line (mode 02, f446..f3176). The director's script pointer
moves through 0x8279D0 (f446), 0x827A10 (f507), then 0x827A50..0x827B10
(f3328..f3451); at f3329 the player is moved to (441.1, 14.6, 114.8) facing
-3.09; at f3484 the pointer passes 0x827B50 and D_00810845 = 0x08; at f3550
(66030) the script ends: the player at (440.4, 14.9, 114.4) facing 0,
D_00810764 = 0xFF, the director in state 3 (freed at f3551), control. [47]
(0x823EE0) enters its state 2 at f1819. Health 90 and infection 60
throughout.

### a04_01 door [40] (1817 frames)

North-east to (492, 162) and the approach to (495.0, 14.97, 162.2) facing
+x (the approach stops there). The Cross press was taken at the fifth try
(f1602): 3B8D = 2 and +5 = 0x25 at f1605 with the player aligned to (496.5,
14.97, 157.0), action 0x41, the door clip 0x45 (f1609), the request B7..B8 = 04 02 and a fade (f1703), D_00810702 = 4 at
f1767 and control at f1768 at spawn entry 4 (516.5, 14.9, 157) facing pi/2.
(An exploration run's screenshot 20 frames after this arrival showed the room's title card.)

### a04_02 the console [69] (941 frames)

From entry 4 north of the block (518, 165), (518, 178), (536, 178), east to
(553, 176), north to (555.5, 188.5) facing +z; Cross at f196: the console's
script 0x82C3B0 (f199, 3B8D = 2, action 0x41, letterbox), D_0081083B = 0xFF
and the reel's state 1 at f379, the reel rolling down (section 2.4), the
script's end at f891..f892 (the reel placed at (380, 14.9, 260), state 4)
and control at f892 at (555.6, 14.9, 187.0) facing 0 (the beat ends at
x 555.5).

### a04_03 back to the hall (1014 frames)

West along the north of the block to (518, 177), south to (516, 162),
(511, 161.5) and the approach toward (506, 161.5) facing -x; Cross at f706
from (506.5, 159.5): door program at f709, clip 0x43, request B7..B8 = 03 02
(f787), D_00810702 = 3 at f851, the player at spawn entry 3 (489.5, 14.96,
157) facing -pi/2 at f852, walking out (action 0x41) to (476.4, 14.96, 157),
and control at f965.

### a04_04 the conveyor (552 frames)

West and north over the sunken floor to (394, 260), east up the conveyor
(f232 off the floor, f350 at y 54.9), onto the east balcony and north to
(541.8, 54.9, 393.8), the corridor in front of door [37]. The director's
freed node 0x7A9FB0 is reused four times by short-lived nodes on the way.

### a04_05 the progression exit (716 frames)

The approach to (540.5, 404.5) facing +z; the first Cross (f66) is not
taken, the second (f158) is: the player aligned to (545.5, 54.9, 405.0),
3B8D = 2 (f161), action 0x41, door clip 0x45 (f165), a fade and the request
B5..B8 = 16 FF 00 01 (f259), D_00810700..703 = 16 00 00 04 at f323 (70752),
overlay id 0x13 (AREA22) at f330, D_00810703 = 0x16 at f334, the arrival at
AREA22 entry 0 (335.7, 155, 496.2) facing pi at f512, the walk-in (3B8D = 4,
f513..f626) to (335.7, 155, 483.1) and control at f627 (71056). (An
exploration run's screenshot after the same arrival showed AREA22's title
card, naming a corridor.)

## 5. Side beats

- **a04_s0 door [45] locked** (324 frames): from the a04_00 end, turned to
  face pi; Cross at f78 from (440.4, 114.5): the player aligned to (440.0,
  14.9, 115.3), 3B8D = 2 (f81), door [45]'s talk block runs script
  0x8272A0 (f81..f254), one message line (f107..f251), control at f255 with
  the player at (439.0, 14.9, 122.3) facing 0. D_00810845 stays 0x08.
- **a04_s1 the reel blocks the conveyor** (437 frames): from the a04_00 end
  (the reel still at (509, 54.9, 260)), the a04_04 path: off the floor at
  f256, then the walk stops at (495.2, 51.5, 260.0) (the closed loop's
  blocked test, 45 frames under 0.3 units).
- **a04_s2 the store room's pickup** (357 frames): from entry 4 north to
  the shelf, facing +z (the approach toward (514.3, 188) stopped at (509.4,
  188.5), where the press was taken); Cross at f149: 3B8D = 3 then 1 (f152,
  f153), action 0x41, the pickup's script (0x2482C0 onward), the status
  screen's item page (ui byte 1 = 3 from f211, 5 at f304) with a message
  (mode 04), Triangle at f301, control at f308. D_00810CB9 0 -> 1; the taken bytes 0x810860..9F do
  not change (where the pickup's taken bit is, was not looked for).
- **a04_s3 door [42] locked** (473 frames): from entry 4 round the block to
  (555.5, 150.9) facing +x; Cross at f193: the door program (f196), the locked-door clip
  0x44 (f202), one message line (f267..f386), control at f404 at (555.5,
  14.9, 158.0). The area does not change.

## 6. Owners on the route

- The director (0x7A9FB0) runs its state 1 from the a02_05 end to a04_00
  f3550 and is freed at f3551.
- The reel (0x7B6A60): state 4 at (509, 54.9, 260) until a04_02 f379;
  state 1 while rolling; state 4 at (380, 14.9, 260) from f892.
- The console (0x7B6770): state 1 until its Use, then state 4 with the
  script pointer 0x82C3B0 .. 0x82C5F0 (f199..f892).
- Doors [40] and [37] run the door programs 0x24DE40 .. 0x24DC80 at their
  Use; door [42] 0x24DEC0 .. 0x24DBC0 (the locked variant); door [45]
  0x8272A0 .. 0x827460.

## 7. Tried and not kept (exploration runs, this session)

Their snapshots were scratch (`build/s88_a04/`) and are deleted at the end
of the session; none of these facts comes from a kept beat, so none of them
can be re-checked from kept data. Treat them as leads.

- **Bugs.** Standing at the ladder's foot (440, 287) facing -z, a Cross
  press left the player in action 0x3B with +5 = 0x0B and a bug on his
  back; rocking the left stick released it about 90 frames later. Bites
  (action 0x3E) near the tower's west side raised infection 60 -> 70 and,
  with a second grab, to 100; the infected latch (+0x234) became 1, the
  screen showed the infected notice and health fell slowly (60 -> 58 in
  about 200 frames). The kept route walks without stopping and saw no grab.
- **The ladder** (440, 282, attribute 0x32) was not climbed (the grab above).
- **The reel's Use.** After the console, an approach to (388, 260) facing
  -x stopped at x 394; the Cross press there was taken by a bite. The reel's
  own Use (script 0x82C650) was not played.
- **The conveyor, both sides.** With the reel at the top, walks along z 250
  and z 270 stopped at (494.9, 52.4, 247.3) and (496.0, 52.9, 272.5).
- **Store-room paths.** The block at x 523..533, z 145..173 stops a direct
  walk between entry 4 and the console; an approach to door [40] from
  (513.5, 169.6) toward (506, 161.5) did not move the player, the approach
  from the east (511, 161.5) did.
- **AREA22.** Only its arrival was seen (a corridor with a crate and a drum;
  its door [8], id 2|0x80, leads to AREA01 entry 6).

## 8. Open

1. Which record of the arrival script 0x8275A0 sets D_008107E4 = 1, and
   which record of script 0x8278D0 sets D_00810764 = 0xFF (both measured
   only at their script's end).
2. The event's message and cinematic (script 0x8278D0) were not decoded
   record by record; what 001C4760(6, 1) at its end records was not read.
3. What unlocks door [45] (D_00810845 bit 5; the director and the
   AREA01 revisit's g[7] both test it), [42] (bit 4, AREA20), [38] (bit 2,
   AREA03) and [49] (bit 6, the padlock [48] beside it).
4. Not visited: the room behind door [45] with the NPC [2] (0x824320,
   whose callee 0x824490 calls 001C47A0(0x23, 1) and sets D_008107E9 = 1 at
   the end of its script, by its C), [3] (0x8246B0,
   D_008107EA / D_0081076A), [4] (0x824DC0, D_0081076C), the lifts [51]
   and [56] (001BD560), [62] (0x825510, waits for D_0081083D) and [65]
   (0x825B00), the panel [66] (0x825880), the west and south balconies,
   the ladder and the tower top, door [35] back to AREA02, the pickup g[8],
   sub 1 (with 0x825310, which has no splat piece), and AREA22 beyond its
   arrival.
5. Whether the story's next area is AREA22 -> AREA01 (entry 6) or needs one
   of the locked doors first.

## 9. Census: what AREA04 executes beyond the first level, AREA01, AREA00, the revisit and AREA02

Lane NEXT replayed every a04 beat with the census tool's one-shot
breakpoints (hidden PCSX2, one session per beat, nothing written to a save
slot) and computed the delta.

**Tool** (`../Extermination/tools/route_census.py`, additive; the default
segments, `report`, `exit-delta`, `a01-delta`, `a00-delta` and `a02-delta`
are unchanged):

- `run --segments a04 --pass A04` replays each AREA04 beat from its recorded
  source snapshot with route_capture's own closed-loop beat function, the
  breakpoints on every boot function and every AREA04 splat piece (runtime
  = splat label + 0x40). The second function of the slot 00825240 (runtime
  0x825310) has no splat piece and is not armed.
- `a04-delta --passes A04 --a02-passes A02 --a01r-passes A01R --a00-passes
  A00 --a01-passes A01` folds the pieces into real functions
  (`tools/overlay/overlay_match.py`, AREA04: 31) and lists what AREA04 runs
  that **neither the first level, nor AREA01 (first visit), nor AREA00, nor
  the revisit, nor AREA02 ran**. "Already ran" = the first-level census
  (`route_functions.json`), beat 15, and every beat of the A01, A00, A01R and
  A02 passes in all phases (a02_05's exit phase is AREA04's load and
  arrival); an AREA04 overlay function counts as already run when a02_05's
  exit phase paused inside it (an AREA02 candidate address inside an AREA04
  function with overlay id 5 resident, mapped by range). Groups, a function
  counting in the first that ran it:
  - **a04**: the main line (a04_00..a04_05, a04_05 up to its area-change
    consumer 001AD010, census f323);
  - **a04_side**: a04_s0..a04_s3;
  - **a04_exit**: a04_05 from 001AD010 on (the AREA22 load and arrival).
    Its overlay-range hits would be AREA22 code at AREA04 candidate
    addresses and are listed apart; there were none.
  Output: `../Extermination/build/s87/census/a04_delta.json`
  (`new_functions`, `functions`, `per_beat`, `overlay_AREA04_not_run`).

**Replay fidelity.** All 10 replays completed their beat's own checks on the
first attempt. Row for row: a04_00 3620/3620, a04_s0 325/325, a04_s2
358/358 and a04_s3 474/474 identical; a04_01 1816 frames against 1817
(first difference at row 337: the closed loop's press retries fell
differently), a04_04 553 against 552 (row 53), a04_05 718 against 716 (row
369), a04_s1 437/437 frames (row 337); a04_02 and a04_03 started two and
one counters off (0 identical rows; a04_02 took 939 frames against 941).
The player's end state equals the recorded snapshot's in every beat except
a04_01, a04_04 and a04_s1 (walks that end a little elsewhere). The
globals end digest differs from the recorded one in 9 of 10 replays (all
but a04_s0): in a04_00, a04_02, a04_05, a04_s2 and a04_s3 by the one word
0x810D98, in a04_01 and a04_03 by 0x810750 and 0x810D98, in a04_04 and
a04_s1 by 32 and 31 words from 0x810600 on. The owners digest also differs
in a04_01, a04_03, a04_04 and a04_s1. As for the
earlier levels, the function sets are those of the same route shape, not of
the exact recorded frames.

**Totals.** The ten beats run **879 functions**: 862 boot and 17 of
AREA04's 31 overlay functions. Already ran: 737 in the first level, 43 in
beat 15, 42 in AREA01 play, 24 in AREA00 play, 4 in the revisit, 10 in
AREA02 play, and 3 AREA04 overlay functions in a02_05's exit phase
(0x823580, 0x824320, 0x825510). **16 are new (7,756 bytes): 2 boot, 14
AREA04 overlay.**

| Group | New | Bytes | Boot | AREA04 ovl | Decomp status |
|---|---:|---:|---:|---:|---|
| a04 (main line) | 14 | 6,924 | 0 | 14 | OC 14 |
| a04_side | 0 | 0 | 0 | 0 | — |
| a04_exit (AREA22 arrival) | 2 | 832 | 2 | 0 | NM 1, AI 1 |

Status abbreviations as in SECOND_LEVEL_ROUTE.md section 6 (NM NEARMISS, AI
inline asm) plus OC overlay C; the statuses are the census's classification
of the decomp's working tree at the run (the AREA04 overlay C is lane
A04C's uncommitted work, byte-identical by its doc).

**New functions by the beat that first runs them** (census frames):

- **a04_00** (14, 6,924): from f1, the per-frame callbacks of AREA04's
  owners: 0x823700 (door [45]), 0x823B90 (the director), 0x823EE0 ([47]),
  0x824100 (the group 0x8268D0 node), 0x8241F0 ([61]), 0x824490 (the NPC
  [2]'s first sub-state, called from 0x824320), 0x8246B0 ([3]), 0x824DC0
  ([4]), 0x825880 ([66]), 0x825B00 ([65]), 0x825D60 ([68]), 0x825DF0 (the
  console [69]) and 0x8260C0 (the reel [70]); and 0x823B40 at f3484 (the
  op09 callback that sets D_00810845 bit 3). The ones hit at f1 also ran in
  a02_05's arrival frames, where no AREA04 address was armed, so the prior
  census could not see them; they are new to the census, not new to the
  game at a04_00.
- **a04_05 exit phase** (2, 832): NM 001C1A80 (f515) and AI 001BE5F0
  (f516), the frames right after the AREA22 arrival (f512).

No boot function is new on the AREA04 main line or its side beats: every
boot function they run already ran in the first level, beat 15, AREA01,
AREA00, the revisit or AREA02.

**The AREA04 overlay.** 17 of its 31 functions ran. The 14 that did not:
0x823540 (the entry pad), 0x823B10 (reached from boot 001E7780's area
dispatch; it can only have run at the load, in a02_05's exit phase, where no
AREA04 address was armed), 0x823920 and 0x823A90 (behaviours of the group
0x828260's records), 0x8239A0 (no reacher found by area_overview), 0x8245F0 (the NPC [2]'s second sub-state), 0x824830,
0x824930, 0x824A40 and 0x824A90 ([3]'s sub-states and the callbacks of its
script 0x8286D0), 0x824EF0, 0x825040 and 0x8251C0 (sub 1's placement [1])
and 0x825280 (the slot 00825240; with 0x825310, sub 1's placement [3]).
"Not run" means not seen by these replays.

**Reproduce** (hidden PCSX2; the replays' own run times sum to 17.7
minutes for the main line and 5.3 for the side beats, about 19 and 6
including the roughly 13 s of arming per beat):

```sh
# decomp repo, .venv python, repo root
.venv/bin/python tools/route_census.py run --segments a04 --pass A04
.venv/bin/python tools/route_census.py a04-delta --passes A04 --a02-passes A02 \
    --a01r-passes A01R --a00-passes A00 --a01-passes A01
```
