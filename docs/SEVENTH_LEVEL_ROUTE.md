# AREA01 upper floor and AREA06: seventh-level route, original ground truth

Lane "NEXT" (capture), 2026-09-28 (session s88). Original executable SHA-256
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

AREA22's progression exit (SIXTH_LEVEL_ROUTE.md, beat a22_02) ends in AREA01
sub 0 at spawn entry 6, the upper floor at door [18], with control at
(119, 60, -349.1) facing pi. This document records what the original does
from there: the AREA01 upper floor (group `a01u`), the area change through
door [19] into AREA06, and AREA06 (group `a06`) as far as the original
lets this state go. The tool, the conventions and the row format are those
of SECOND_LEVEL_ROUTE.md .. SIXTH_LEVEL_ROUTE.md: hidden PCSX2, exact
one-frame steps, pad input only, no teleports and no memory writes, one
trace and one resumable snapshot per beat. Nothing here is port behaviour.

**Status.**

- **AREA01 upper floor: done.** The platform at door [18] and the catwalk
  to door [19] are joined by a running jump over the gap
  (z -400..-440), the same Use-press jump as the first level's crevice
  (0015EC50). No item and no lock is involved: item 0x23 and AREA04's door
  [45] are **not** needed to reach AREA06. Main line a01u_00..a01u_02:
  the jump, a scripted event on the catwalk (placement [45], script
  0x82B590, D_00810767 = 0xFF) and door [19], the area change to AREA06
  entry 0. 3 beats, 2,738 frames, counters 73114..75854. Side beats:
  a01u_s0 (a pickup on the catwalk) and a01u_s1 (door [18] back to AREA22
  entry 5).
- **AREA06: its progression exit is not reached in this state.** AREA06
  (overlay id 6, title card naming a command centre) has three doors:
  [1] back to AREA01 entry 7, the room move [2], and [3] to AREA16
  entry 0, which is lock-gated on D_00810847 bit 2 (by 001BB860's
  NEARMISS C; the capture shows the door locked with that bit clear). No AREA06-resident
  code sets that bit as far as the decomp's C shows, and the only explicit
  reference to D_00810847 in the boot ELF and the overlay binaries is in
  AREA15's overlay (section 3.4). The main line a06_00..a06_05 plays what AREA06
  does allow: the beam over the pit, the crate that closes the corridor,
  door [2], the keypad [6] (the correct string sets **D_00810845 bit 5**,
  the lock bit of AREA04's door [45]), the way back, the beam's collapse
  into the pit, and door [3], which takes its locked branch. 6 beats,
  4,589 frames, counters 75856..80453. Side beats: a06_s0 (door [1] back
  to AREA01 entry 7, the one open area change) and a06_s1 (the overhead
  bar).

**What the story needs next** (sections 3.4 and 8, from the code; not
played): the keypad's bit is the one AREA04's door [45] tests
(FIFTH_LEVEL_ROUTE.md open item 3), behind which the NPC [2] gives item
0x23, the AREA22 reader's key (SIXTH_LEVEL_ROUTE.md section 2.3); AREA06's
own exit, door [3], waits for AREA15's write. But after the keypad,
crossing back over the beam drops the player into the pit, where door [3]
is the only door, and no way out of the pit was found (section 7), so in
this state the keypad appears to strand the player in AREA06. The order
the game intends (AREA15 before AREA06's keypad, reached through AREA19
from AREA03 by the door tables) is open.

In-game text (messages, prompts, title cards, item descriptions, the
keypad's expected string) is described, never quoted. Positions are world
units; yaw is the player's +0xC4 (X = sin, Z = cos).

## 1. Tool, outputs and conventions

The beats are the opt-in groups `a01u` and `a06` of
`../Extermination/tools/route_capture.py`. `--beats all` is unchanged
(beats 00..14); the AREA01, AREA00, C7, revisit, AREA02, AREA04 and AREA22
groups are unchanged.

```sh
# decomp repo, .venv python, repo root
.venv/bin/python tools/route_capture.py run --beats a01u             # every AREA01 upper-floor beat, in order
.venv/bin/python tools/route_capture.py run --beats a06              # every AREA06 beat, in order
.venv/bin/python tools/route_capture.py run --beats a06_02,a06_s1    # some beats (each from its source)
.venv/bin/python tools/route_capture.py events --beats a06_02        # change log of one trace
.venv/bin/python tools/route_capture.py verify --beats a01u,a06      # every snapshot resumes
```

- **Session, resuming, navigation, Use**: as SECOND_LEVEL_ROUTE.md section 1.
  a01u_00 and a01u_s1 start from
  `build/s87/route_a22/a22_02_progression_exit/state.p2s`; a06_00 and a06_s0
  from `build/s87/route_a01u/a01u_02_progression_exit/state.p2s`. Every
  beat calls `next_long_frames` (FOURTH_LEVEL_ROUTE.md section 1).
- **Walking and bugs.** The beats walk with AREA04's `a04_go` (a bug's
  grab, action 0x3B, is shaken off by rocking the stick; FIFTH_LEVEL_ROUTE.md
  section 1). The a06 beats use `a06_settle`, `a06_face`, `a06_approach`,
  `a06_use` and `a06_control`, the same helpers with the shake in every
  wait: in exploration runs a grab that was not shaken off ended in a
  bite (action 0x3E) that set infection to 100 (section 7).
- **The running jump** (`a01u_running_jump`): the stick held toward -z and
  Cross pressed once the player is past z -397.5, as in beat
  12_crevice_jump (FIRST_LEVEL_ROUTE.md).
- **The keypad** (`a06_keypad_entry`): waits until the page object at
  0x28B020 (callback 00207350) is live (+4..6 = 1, 0, 2), reads the
  compared string through D_00275858[slot] and the cell digits from
  D_00265010 **from RAM at run time** (the tool holds no game data), and
  moves the cursor and presses Cross in a closed loop on the object's
  cursor (+0x28) and character count (+0xA); then OK (cell 10). The
  traces hold the cursor path, so they carry the string; they stay in the
  ignored build tree.
- **Outputs**: `../Extermination/build/s87/route_a01u/<beat>/` and
  `../Extermination/build/s87/route_a06/<beat>/` (ignored): `trace.json`,
  `state.p2s`, `eeMemory.bin`, `gs.bin`, `scratchpad.bin`, `original.png`,
  `snapshot.json`.
- **Row format**: the AREA22 row (SIXTH_LEVEL_ROUTE.md section 1) with the
  owners below (each with `cb` and `rot`), plus `s766` (D_00810766,
  001C02E0's 0xFF latch), `l845` (D_00810845, AREA04's lock byte, whose bit
  5 the keypad sets) and `l847` (D_00810847 = D_00810841[6], AREA06's lock
  byte). The a06 rows add `cca` (D_00810CCA, which 001C4760(7, 1) raises)
  and `kpad` (the keypad page object: its state bytes +4..6, the count
  +0xA, the slot +0xB and the cursor +0x28). `events` logs these keys
  besides the earlier ones.

### 1.1 Owners sampled

`A01U_OWNERS`, pool nodes of the AREA01 sub-0 load made by the a22_02
arrival (these addresses differ from the first visit's and the
revisit's); [n] is placement table 0x82BD50 record n, g[n] deferred group
0x828A00 record n.

| Row key | Node | Callback | Record | Role on the route |
|---|---|---|---|---|
| `door18_r18` | 0x7AFAC0 | 001BB860 model 0x09 | [18] (120, 60, -318.8), id 5\|0x80 | back to AREA22 entry 5 (a01u_s1) |
| `door19_r19` | 0x7AFDB0 | 001BC350 model 0x03 | [19] (-109.5, 60, -674.5), id 6\|0x80 | the area change to AREA06 (a01u_02) |
| `pick_g11` | 0x7A7690 | 0015AFA0 (class 0xC7) | g[11] (136.5, 60.1, -473.1) | taken in a01u_s0 |
| `pick_g13` | 0x7A7C70 | 0015AFA0 | g[13] (142.8, 2.1, -416.2) | on the ground under the gap (not taken) |
| `n_g36_1C02E0` | 0x7ABA20 | 001C02E0 (kind 0x12) | g[36] (140.2, 66.5, -450.1) | at the catwalk end of the gap; its header did not change on any beat |
| `n_1BFFD0` | 0x7B8DA0 | 001BFFD0 | the companion 001C02E0 spawned | no change |
| `bug_g30`, `bug_g31` | 0x7AA880, 0x7AAB70 | 00128C10 | g[30] (128.9, 82.6, -671.5), g[31] (82.8, 75.9, -662.4) | over the catwalk |
| `bug_g32`..`bug_g35` | 0x7AAE60, 0x7AB150, 0x7AB440, 0x7AB730 | 00128C10 | g[32..35] at y 60.4, x 125..134, z -383..-398 | on the platform's south end; freed and reused while the player passes |

`A06_OWNERS`, pool nodes of the AREA06 sub-0 load, measured in the a01u_02
end snapshot; [n] is placement table 0x827AC0 record n, g[n] deferred group
0x826000 record n.

| Row key | Node | Callback | Record | Role on the route |
|---|---|---|---|---|
| `door1_r1` | 0x7AB730 | 001BC350 model 0x03 | [1] (-109.8, 60, -674.5), id 0\|0x80 | back to AREA01 entry 7 (a06_s0) |
| `door2_r2` | 0x7ABA20 | 001BC350 model 0x03 | [2] (-348.8, 60, -654.5), id 1 | room move, entries 2 / 1 (a06_01, a06_03) |
| `door3_r3` | 0x7ABD10 | 001BB860 model 0x3E | [3] (-240, 20, -800.5), id 2\|0x80 | to AREA16 entry 0; locked (a06_05) |
| `lamp_r4` | 0x7AC000 | 00158BD0 | [4] (-240, 51, -799) | above door [3] (red in a06_05's end image) |
| `keypad_r6` | 0x7AC5E0 | overlay 0x824340 (class 0x86) | [6] (-306, 68, -650) | the keypad (a06_02) |
| `r7_1C50B0` | 0x7AC8D0 | 001C50B0 | [7] (-306.5, 70, -650) | beside the keypad |
| `r8_823B50` | 0x7ACBC0 | overlay 0x823B50 | [8] (-352.9, 75, -611) | at the cable bundle over the corridor; its +4 cycles 0, 1, 2 |
| `r10_22DCD0` | 0x7AD1A0 | 0022DCD0 | [10] (-340, 64.2, -591.5) | on the machine block |
| `beam_r11` | 0x7AD490 | overlay 0x824560 | [11] (-239.9, 50, -584) | the beam over the pit (a06_00, a06_04) |
| `crate_r31` | 0x7B0C60 | 001551B0 model 0x1E | [31] (-339.6, 60, -609.6) | closes the corridor; broken in a06_01 |
| `save_r43` | 0x7B2FA0 | 00159B90 | [43] (-333.2, 68, -627.3) | in the room (not used) |
| `r47_1E7D20` | 0x7B3B60 | 001E7D20 | [47] (-260, 18.5, -781) | in the pit |
| `r54_15A2C0` | 0x7B4FF0 | 0015A2C0 | [54] (-330.7, 60, -601.5) | beside the crate; its +0x56 is 0, so by its C it counts nothing |
| `r56_1C1A80` | 0x7B55D0 | 001C1A80 model 0x52 | [56] (-250, 18.3, -778.5) | in the pit |
| `doc_g2` | 0x7A5C20 | 00219550 | g[2] (-343.7, 60, -674.5) | in the room (not taken) |
| `pick_g8` | 0x7A6DC0 | 0015AFA0 | g[8] (-366.3, 60.4, -652) | outside door [2] (not taken) |
| `g11_219870` | 0x7A7690 | 00219870 (class 4, model 0x30) | g[11] (-350.5, 70, -670) | outside the room's west wall; cycles through states 2, 3, 0, 1 when the player is near (a06_s1) |
| `bug_g27`..`bug_g29` | 0x7AA590, 0x7AA880, 0x7AAB70 | 00128C10, 00128C10, 0012A5D0 | g[27..29] at z -570..-573, x -346..-328 | on the north wall over the west floor |

## 2. The AREA01 upper floor, from the original's own data

From the a22_02 end snapshot: the collision grid at D_0028A598 decoded as
in `tools/export_collision.py` and the pool by `tools/area_overview.py
--area 1 --ram <snapshot>/eeMemory.bin` (both outputs were scratch and are
regenerated by the same commands). The AREA01 registries, doors and spawn
table are those of SECOND_LEVEL_ROUTE.md section 2 (same overlay, same
sub 0).

### 2.1 Geometry

- **The platform** at door [18] (y 60): x 101..140 from z -320 to -379 and
  x 121..140 from z -379 to -400, with low walls (attribute 0x50) at
  x 101 and x 121 and floors of attribute 0x03 south of z -350.
- **The gap**, z -400..-440 over x 120..140: no floor at y 60; the ground
  below is y 0. Both lips carry a thin floor of attribute 0x78 at y 60.5
  (z -383..-400 and z -440..-457).
- **The catwalk** (y 60): x 121..140 from z -440 south to z -590 (floors of
  attribute 0x08, 0x03 and 0x00), x 120..140 to z -640, then west along
  z -662..-678 (x 40..138 and, as attribute 0x03, x -75..40) to x -110 at
  door [19].
- **A ladder and a rail** join the two sides as well (not played): a
  ladder face of attribute 0x3B on the catwalk side (x 139.6,
  z -481..-489, y 60 to 109) and a strip of attribute 0x3D at y 107..110
  (x 135.5..141) from z -342.5 to -477.5, over the gap. By 0015D4C0's
  cases (PLAYER_LADDER_ENTRY.md) 0x3B is a ladder entry (+D = 1) and 0x3D a
  ledge hang (+5 = 0xA); SECOND_LEVEL_ROUTE.md section 9.4 found no way up
  to y 60 from AREA01's ground.

### 2.2 What decides each step (code, then capture)

- **The running jump.** 0015EC50 (PLAYER_RUNNING_JUMP.md section 1) returns
  early only for its listed area boxes (areas 4, 0xD, 0xF, 0x10, 0x13,
  0x16); AREA01 is area 1, so a Use press running at the lip with a floor
  more than 4.01 below enters +5 = 6. Measured in a01u_00: Cross at f126,
  action 0x0C and state 6 at f129, clips 0x69 and 0x6B, the apex y 69.8,
  the landing (action 0x0F, state 8) at f175 and clip 110 at f176, on the
  catwalk at (126.7, 60, -442.2).
- **The catwalk event.** Placement [45], overlay 0x8267C0
  (`func_overlay_AREA01_00826780.c`, byte-identical C), in its state 1:
  while story flag 15 is clear (001BA1C0 tests D_00810758[n] == 0xFF; flag
  15 = D_00810767) and flag 7 is set (D_0081075F, 0xFF in every a01u
  snapshot), it starts script 0x82B590 once 001B1EA0 finds the player in
  the quad 0x82CCA0 (corners x 21 and 35, z -660 and -680) in both of its
  tests. The script's last record is an op07/5; measured, D_00810767
  becomes 0xFF at its end (a01u_01 f1378).
- **Door [19]** (001BC350 model 0x03, id 6|0x80, record 06 00 00 00):
  001BC350 tests a lock bit only for model 0x15 (its byte-identical C), so
  the door opens: the door program 0x24DE40 .. 0x24DC80, request
  B5..B8 = 06 FF 00 01 and the area change (a01u_02).
- **Door [18]** (001BB860 model 0x09, id 5|0x80, record 16 05 00 00): not
  lock-gated; by 001BB860's C (NEARMISS, objdiff 57.68%, not byte-identical)
  only models 0x16, 0x17 and 0x3E are, and the capture confirms that this
  model 0x09 door opened; the program
  0x24D900 .. 0x24DA00 and the area change to AREA22 entry 5 (a01u_s1).
- **The node at the gap** (001C02E0, g[36], kind 0x12): by its C
  (NEARMISS), state 0 tests D_00810845 bit 5; clear (every a01u beat), it
  spawns the 001BFFD0 companion and runs state 1 (a proximity test
  001BF630 and the sound 0x444); set, it stores D_00810766 = 0xFF, installs
  001BF6B0 and calls 001B6660(0x829110, 2, 0). Its sampled header did not
  change while the player jumped past it. The keypad in AREA06 (a06_02)
  sets that bit; what the node does on a later AREA01 load was not played.

## 3. AREA06, from the original's own data

From `tools/area_overview.py --area 6 --ram <snapshot>/eeMemory.bin` over
the a01u_02 end snapshot and the collision grid decoded as above. Code
descriptions for boot functions are from the decomp's C (BM byte-matched,
NM NEARMISS). At decomp commit c3d4ca7 AREA06's overlay was assembly apart
from its area init; this lane read the overlay functions 0x824340,
0x8242C0, 0x824560 and 0x825E20 from their instructions (described, not
reproduced). During the session another lane ("A06C") added C for them to
the decomp's working tree (untracked at the time of writing; its headers
say byte-identical by `tools/overlay/overlay_match.py`); where this section
relies on that C it says so. Every claim is checked against the captures
where a beat exercises it.

### 3.1 Registries

- Overlay `AREA06.BIN`, header id 6, text 0x823540..0x826000, ten splat
  pieces. The area init at runtime 0x824290
  (`func_overlay_AREA06_00824250.c`, committed, byte-identical): boot
  001E7780 calls it for key 0x600 and it sets D_00275C2C = 1, D_00275C28 = 0x20,
  D_00275C20 = 0x828F00, D_00275C24 = 0 and D_00275C1C = 0x832F80.
- Two sub-states: placement tables 0x827AC0 (sub 0, 57 records) and
  0x8283D0 (sub 1, 68), deferred groups 0x826000 (33) and 0x8265E0 (23).
  Every beat here is sub 0 (D_00810701 = 0 throughout). Level data: INDEX
  sector 10 (`chunk10`, nested blocks n0 and n1). Four script chains:
  0x826D40 and 0x827040 (started by 0x824340), 0x827180 (by 0x824560) and
  0x8276C0 (by 0x825E20, sub 1 only).
- **Spawn table** (sub 0, 0x24C0D0):

  | Entry | Position | Yaw | Used by |
  |---:|---|---:|---|
  | 0 | -117, 60, -670.9 | -pi/2 | AREA01 door [19]: the arrival (a01u_02) |
  | 1 | -336.8, 60, -649.5 | pi/2 | door [2], inside the room (a06_01) |
  | 2 | -363.8, 60, -649.5 | -pi/2 | door [2], outside (a06_03) |
  | 3 | -240, 20, -788.5 | 0 | door [3] from AREA16 (not played) |

- **Doors** (destination table D_0024E140[6] = 0x24E028): id 0 = 01 07 00 00
  ([1], AREA01 entry 7); id 1 = 02 01 00 00 ([2], room move, entries 2 / 1);
  id 2 = 10 00 00 00 ([3], AREA16 entry 0). The lock byte is
  D_00810841[6] = D_00810847; it is 0x00 on every beat. (AREA16's own
  table, D_0024E140[16], holds a record 06 03 00 00: AREA06 entry 3, the
  other side of door [3].)
- **Placements** (behaviours, sub 0): 14 crates 001551B0 ([20]..[33]; [22]
  ..[25] on the room's roof, [26]..[30] stacked at z -766.5), four drums
  00156620 ([13]..[16]), three 00156F30 ([17]..[19]), eight 0015A2C0 nodes
  ([48]..[55]), 13 static 001C4820 records (12 live), the doors [1]..[3], the lamp
  [4], the keypad owner [6], 001C50B0 [7], the overlay owners [8]
  (0x823B50), [9] (0x823580) and [11] (0x824560), 0022DCD0 [10], the save
  point 00159B90 [43], 001C48C0 [45], 001E7D20 [47] and 001C1A80 [56]. The
  deferred group holds four 00219550 and seven 0015AFA0 pickups, one
  00219870 node (g[11]), seven 00128C10 and fourteen 0012A5D0 nodes.

### 3.2 Geometry

- **Two floors at y 60 and a pit between them.** The east floor x -180..-110
  (z -570..-770) holds the arrival; a cable bundle (attribute 0x51,
  x -158.3..-148.7) runs overhead along it and comes down to the floor at
  z -675..-639; a block (x -159.5..-110, z -620..-610, top y 84.5) crosses
  it, leaving the passage x -180..-160. The west floor is x -370..-300.
  The pit between (x -270..-211, z -560..-810) has its floor at y 17 with
  45-degree walls up to y 60 on both sides; a surface of attribute 0x5B at
  y 18.9 covers most of it (001E7D20 [47], whose C comment calls it a
  water surface, stands there). Railings (attribute 0x50) line both rims.
- **The pads and the beam.** A pad sticks out over the pit from each floor
  at z -600..-570 (east x -200..-180, west x -300..-280; a floor of
  attribute 0x78 at y 63 over each). The beam [11] (a dynamic object, not
  in the static grid) spans the pit between them at z about -584.
- **The west floor.** The room x -350.5..-299.5, z -680.5..-619.5 (walls to
  y 85; roof at y 85, crates on it; its floor, attribute 0x01, at y 60)
  holds the keypad [6] on its east desk and the save point [43]. Door [2]
  is in its west wall. North of the room a machine block (x -370..-340,
  z -600..-583, top y 82, higher parts to y 119 at the north wall) fills
  the floor's north end, so the only way from the west pad to door [2] is
  the corridor between the block and the room (z -600..-619.5). The crate
  [31] closes its east mouth; the cable bundle x -357.6..-348.5,
  z -619.5..-600 hangs down to y 74.1 above it (standing on the crate, the
  walk west stops at the bundle; in the exploration runs the player could
  not climb from the crate to the room's roof).
- **The bar.** A raised block (x -345..-331, z -760..-750, top y 63,
  attribute 0x3A) stands under an overhead bar (attribute 0x34, facing
  down, y 95.5, x -350..-201.3, z -760..-750). A ladder (attribute 0x32,
  x -345..-338, z -681.4) climbs the room's south wall to the roof. A
  slide (attribute 0x35, x -300..-257, z -769..-751) runs from the west
  floor down into the pit.
- **Door [3]** stands at the pit's south end (x -240, z -800.5, on a ramp
  to y 20).

### 3.3 What decides each step (code, then capture)

- **The beam [11]** (overlay 0x824560; A06C's C,
  `func_overlay_AREA06_00824520.c`). State 0 initialises it (with story
  flag 16, D_00810768, already set it goes straight to its fallen pose and
  state 2); it then takes state 1 when D_00810845 bit 5 is set and state 4
  when it is clear. By the C, state 4 plays a 240-frame run of the beam's
  pose (and random effect spawns) when the player stands at y >= 55 inside
  the quad 0x827640, and moves to state 1 once bit 5 is set. Measured:
  state 4 at the arrival; the player crossed it in a06_00 with no script
  and no hit. (In the exploration runs
  a line nearer the beam's south edge stepped down onto a lower part at
  x -251, and in one census replay went off the beam into the pit; the
  kept beat walks its middle, z -584.) The node went to state 1 at a06_02
  f1569, right after the keypad set the bit (state 4 re-reads it). In
  state 1, by the C, with the player at y >= 55 inside the quad 0x827680
  (x -273..-269, z -600..-570), D_008106B9 = 0 and the player's +5 below 2
  or in 29..34, it gives itself script 0x827180 (19 records, among them two
  op03/2 and two op12/0 that the port's script host does not admit,
  AREA_SCRIPT.md), stores D_00810768 = 1, places the player at
  (-270, 55, -583) facing +-pi/2 and plays sound 0x8CC; its sub-state 1
  moves the beam and the player down in steps with effect spawns, and its
  state 2 stores D_00810768 = 0xFF. Measured in a06_04: the player at
  (-270, 55, -583) and D_00810768 = 1 at f240, the script 0x827180 ..
  0x827540, a letterboxed camera, clips 343, 115 and 117 while the player
  falls from y 57 to y 29, the node's state 2 at f428, D_00810768 = 0xFF at
  f429, control at f456; the player ends on the fallen beam at
  (-268.5, 38.8, -583.2), inside the pit.
- **The crate [31]** (001551B0 model 0x1E) blocks the corridor. The second
  melee (Square, action 0x37) breaks it with one press (a06_01 f126; its
  state byte 4 -> 2 at f138), as for AREA00's floor boxes (THIRD_LEVEL_ROUTE.md).
- **Door [2]** (001BC350 model 0x03, room move id 1): not lock-gated; the
  program 0x24DE40, 0x24DE80, 0x24DC00 .. 0x24DC80, clip 67 (inward) or 69
  (outward), request B7..B8 = 01 02 / 02 02, the room move (a06_01, a06_03).
- **The keypad [6]** (overlay 0x824340, class 0x86). Its set-up (after
  001B0FD0 and 001C6380) gives it script 0x826D40 (001BA1A0) when
  D_00810845 bit 5 is clear; with the bit set it gives it script 0x827040
  when item 0x23's count D_00810C87 is 0, and puts its sub-state +5 at 2
  (where it does nothing more) when the item is held. Sub-state 0 waits
  for Use (the node's +0xB bit 2), turns the player (001B6F00) and moves
  to sub-state 1; sub-state 1 waits for the script's end (001BA1F0); then,
  with bit 5 set, it calls 001C4760(7, 1) once (while D_00810CCA is 0) and
  gives the node script 0x827040, otherwise script 0x826D40 again, and
  returns to sub-state 0. Script 0x826D40 (12 records) holds an op09 record 0x826EC0 whose callback is
  overlay 0x8242C0: its first call stores D_008106C5 = 1 (the request
  that opens the passcode page; a value other than 2 selects page 4, slot
  0; status pages 4/5 of 0020CDC0, FINDINGS "PASSCODE KEYPADS") and returns
  0; its next call returns 1, after calling 001FABB0 and 001FB0B0(0) when
  D_00810845 bit 5 is set. The page's callback
  00207350 (BM) compares the typed string with D_00275858[slot] and, for
  slot 0, sets D_00810845 |= 0x20. Measured in a06_02 (section 5): the
  op09 record at f337, the page from f341 (a map screen with a numeric keypad; the map marks the present
  area and one other place in red), live at f629, the string entered
  f647..f752, OK, and **D_00810845 0x08 -> 0x28** at f768 with the page's
  result line; the page closed at f1564, the script's remaining records to
  0x827040, D_00810CCA 0 -> 1 at f1629 with control.
- **Door [3]** (001BB860 model 0x3E, id 2|0x80): by 001BB860's C
  (NEARMISS, objdiff 57.68%; the lock test is read from that C, and the
  capture confirms only the locked outcome for this model 0x3E door), it tests
  D_00810841[D_00810700] against 1 << +0x34 for models 0x16, 0x17 and 0x3E;
  here +0x34 = 0x82, so the bit is 2 of D_00810847 (the shift amount is
  taken modulo 32). Clear on every beat: the locked branch (001BB560 with
  1), the program 0x24DA40 .. 0x24DB40, one message line (a06_05).
- **Door [1]** (001BC350 model 0x03, id 0|0x80, record 01 07 00 00): not
  lock-gated; the program 0x24DE40 .. 0x24DC80, request B5..B8 = 01 FF 07 01
  and the area change to AREA01 entry 7 (a06_s0).
- **The bar**: Use on the 0x3A block top (0015D4C0 case 0x3A, a probe 40 up
  finds attribute 0x34) is the pole entry (action 0x20, state 0xF, then
  0x21 / 0x10 hanging at y 75); the stick moves the player along the bar;
  Cross lets go (action 0x23, then a fall, action 0x0D, and the landing)
  (a06_s1).
- **Other AREA06 code met on the route.** The sub-1 owner 0x825E20 (not
  loaded in these beats; A06C's C `func_overlay_AREA06_00825DE0.c`) tears
  itself down when flag 0x2A (D_00810782) is set; otherwise it starts
  script 0x8276C0 once flag 0x31 (D_00810789) is set while 3B8D is not 4,
  and at the script's end stores
  D_00810802 = 0xFF, sets D_00810845 |= 0x10 (the lock bit of AREA04's door
  [42], to AREA20) and calls 001C4760(0x1F, 1). The 0015A2C0 nodes read a
  per-node kind (+0x56); the eight AREA06 nodes all hold 0, a value their
  C does nothing with in state 1.

### 3.4 Which exit the story takes (code)

- **Area changes.** Door [1] (open; AREA01 entry 7) and door [3] (AREA16
  entry 0; lock-gated on D_00810847 bit 2 by 001BB860's NEARMISS C,
  section 3.3). Door [2] is a room move.
- **No AREA06 writer of D_00810847.** The decomp's C has seven functions
  that OR a bit into D_00810841[D_00810700] (SIXTH_LEVEL_ROUTE.md section
  2.3); none of them is a behaviour in AREA06's pool, and none of AREA06's
  four scripts holds an op09 record naming one. A scan of the boot ELF
  text and of every `extract/OVERLAY/AREA*.BIN` for loads and stores whose
  immediate is D_00810847's low half found one place: **AREA15's overlay**,
  in the function at runtime 0x824B80 (splat
  `func_overlay_AREA15_00824B40`, assembly). At the end of its script
  0x828CA0 it calls 001C47E0(0x2B, 1), stores 0xFFFF in its halfword +0x2E, calls
  001C4760(0x1C, 1), stores D_008107FF = 1 and ORs 4 into D_00810847 (then
  001FB0B0(0)). The scan does not see a write through a computed address
  (for example a script op indexing D_008107D8 far enough), so "only
  AREA15" is what the scan shows, not a proof.
- **Measured**: D_00810847 = 0x00 on every a06 beat; door [3] took its
  locked branch (a06_05).
- **Therefore** AREA06's forward area change, door [3], is closed in this
  state; AREA06's one open area change is door [1] back to AREA01 (a06_s0,
  played from the arrival, before the keypad). What AREA06 contributes in
  this state is D_00810845 bit 5 from the keypad, which AREA04's door [45]
  (0x823700, model 0x16, lock bit 5; FIFTH_LEVEL_ROUTE.md) and AREA01's
  node 001C02E0 (section 2.2) test.

## 4. Route table

Frames `f` are trace frames; `c` is the main-loop counter 0x70003B64.

| Beat (folder) | Source | Frames | Counters | Presses | What happens |
|---|---|---|---|---|---|
| `a01u_00_gap_jump` | a22_02_progression_exit | 234 | 73114..73348 | Cross f126 | south along the platform, running jump over the gap onto the catwalk, control f233 at (126.2, 60, -446.8) |
| `a01u_01_catwalk_event` | a01u_00 | 1447 | 73349..74796 | — | south and west along the catwalk into [45]'s quad: script 0x82B590 (f408..f1378), **D_00810767 = 0xFF** |
| `a01u_02_progression_exit` | a01u_01 | 1057 | 74797..75854 | Cross f430 | door [19]: **AREA06 entry 0**, overlay id 6, control f968 at (-130.1, 60, -670.9) |
| `a01u_s0_pickup` | a01u_00 | 358 | 73349..73707 | Cross f174, Triangle f331 | the pickup g[11] on the catwalk: its item page |
| `a01u_s1_door18_back` | a22_02_progression_exit | 963 | 73114..74077 | Cross f225 (not taken), f415 | door [18]: **AREA22 entry 5**, overlay id 0x13, control f874 at (120, 158.5, 104.6) |
| `a06_00_beam` | a01u_02_progression_exit | 356 | 75856..76212 | — | east floor to the north-east pad, over the beam (state 4) to the west pad |
| `a06_01_crate_door2` | a06_00 | 682 | 76214..76896 | Square f123, Cross f487 | the crate [31] broken; the corridor; door [2]: entry 1 in the room |
| `a06_02_keypad` | a06_01 | 1698 | 76899..78597 | Cross f95 (not taken), f182; the keypad presses f629..f767 | the keypad [6]: **D_00810845 = 0x28**, D_00810CCA = 1 |
| `a06_03_room_out` | a06_02 | 397 | 78598..78995 | Cross f95 (not taken), f182 | door [2] from inside: entry 2 |
| `a06_04_beam_collapse` | a06_03 | 525 | 78996..79521 | — | back through the corridor onto the beam: script 0x827180, **D_00810768 = 0xFF**, the fall into the pit |
| `a06_05_door3_locked` | a06_04 | 931 | 79522..80453 | Cross f556 (not taken), f692 | down into the pit, south to door [3]: locked, one message |
| `a06_s0_door1_back` | a01u_02_progression_exit | 718 | 75855..76573 | Cross f73 | door [1]: **AREA01 entry 7**, overlay id 2, control f629 at (-84.4, 60, -670.3) |
| `a06_s1_bar` | a06_03 | 1175 | 78998..80173 | Cross f305, f1031 | the overhead bar west to east, let go over the pit's east side |

Main line a22_02 -> a01u_00 -> a01u_01 -> a01u_02 -> a06_00 .. a06_05:
7,327 frames. Every snapshot was reloaded after its capture and resumed on
the first check (no tail idle). The counter gap between a beat's last row
and the next beat's first row is the snapshot reload and is 1 to 3
counters: 1 for a22_02 -> a01u_00 -> a01u_01 -> a01u_02, a01u_02 -> a06_s0
and a06_02 -> a06_03 -> a06_04 -> a06_05; 2 for a01u_02 -> a06_00 and
a06_00 -> a06_01; 3 for a06_01 -> a06_02 and a06_03 -> a06_s1.

## 5. The main line

### a01u_00 the gap jump (234 frames)

From (119, 60, -349.1) facing pi, south to (129.7, 60, -383.6), facing pi,
the stick held toward -z; past z -397.5 Cross at f126: action 0x0C, state 6
at f129 at (130.9, 60, -399.0), clip 0x69 then 0x6B (f138), the apex at
about y 69.8 (f156), the landing (action 0x0F, state 8) at f175 (clip 110 from f176) at
(126.7, 60, -442.2), control at f233 at (126.2, 60, -446.8). The four
00128C10 nodes on the platform's south end (g[32]..g[35]) were freed and
their nodes reused as the player passed (taken bytes 0x81088F |= 0x40,
0x810890 |= 1).

### a01u_01 the catwalk event (1447 frames)

South along x 130 to z -650, west along z -670. At (33.6, 60, -669.9), inside
the quad 0x82CCA0, [45] starts script 0x82B590: 3B8D = 3 at f408 and 2 at
f409, action 0x41, camera mode 1 with the letterbox, clips 375, 376 and 377,
one message line (mode 02, f641..f1189; the cutscene shows a large clawed
creature close up), control at f1378 (c74727) at (12.8, 60, -665) with
D_00810764..67 = FF 00 00 FF (D_00810767 0 -> 0xFF). The taken bytes
0x81088F and 0x810890 gained bits 0xA0 and 2 (the catwalk's 00128C10 nodes).

### a01u_02 door [19] (1057 frames)

West along z -670 to (-98.9, 60, -670); Cross at f430, taken at f433: 3B8D
= 2, door [19]'s program 0x24DE40, 0x24DE80, 0x24DC00 .., action 0x41, the
request B5..B8 = 06 FF 00 01 and a fade (f531), D_00810700..703 =
06 00 00 01 at f595 (c75392), overlay id 6 at f602, D_00810703 = 06 at f606,
the arrival at AREA06 entry 0 (-117, 60, -670.9) facing -pi/2 at f853
(c75650), the walk-in (3B8D = 4 from f854) to (-130.1, 60, -670.9) and
control at f968 (c75765). AREA06's title card names a command centre.

### a06_00 the beam (356 frames)

From the arrival north round the cable bundle's foot (x -140, z -665 ..
-632), west to (-176, -625), north through the passage x -180..-160 to
z -600 and onto the north-east pad at (-195, -585); onto the beam at f210
(x -200.5) and west along its middle (z -582.4..-587.7, y 57.3 and above)
to the west pad at f319 (-285.5, 60, -585.1); the beat ends at
(-299.9, 60, -582.7). The beam stays in state 4. No hit, no grab.

### a06_01 the crate and door [2] (682 frames)

West off the pad and south to (-327, 60, -608) facing -x; Square at f123:
the second melee (action 0x37, state 34, clip 270) at f126, the crate
[31]'s state byte 4 -> 2 at f138. West along z -611 under the cable bundle,
south along x -362 to door [2]'s outside, facing +x; Cross at f487, taken
at f490: door [2]'s program 0x24DE40 .. 0x24DC80, clip 67, request B7..B8 =
01 02 and a fade (f568), the room move to entry 1 (D_00810702 = 1 at f632)
and control at f633 at (-336.8, 60, -649.5).

### a06_02 the keypad (1698 frames)

East across the room to (-314.5, 60, -650.1) facing +x; the first Cross
(f95) is not taken, the second (f182) is: 3B8D = 3 at f185 and 2 at f186,
action 0x41, the player turned to (-315.5, 60, -650) facing pi/2, the
keypad's script from 0x826D80 (f187), clip 65, a fade (f273..f336), the
op09 record 0x826EC0 at f337 and the page (ui byte 1 = 3) from f341; the
page object's state bytes go 01 00 00 (f368), 01 00 01 (f369) and 01 00 02
(live) at f629. The eight characters were entered f647..f752 (count +0xA
1..8), the cursor moved to OK and Cross pressed: +5 = 1 at f767, +5 = 3 and
**D_00810845 0x08 -> 0x28** at f768 (c77667) with the page's result line
(message mode 03, record 4), +5 = 8 at f933, a second line (record 6) with
+5 = 4 at f1023, +5 = 5 at f1323, the shutdown (+4 = 2) at f1563, the page
closed f1564..f1567, the beam [11]'s state 4 -> 1 at f1569, the script's
records 0x826F00 .. 0x827040 and control at f1629 (c78528) with
D_00810CCA 0 -> 1. Health and infection did not change.

### a06_03 out of the room (397 frames)

West to (-343, -654.3) facing -x; the first Cross (f95) is not taken, the
second (f182) is at f185: door [2]'s program, clip 69, request B7..B8 = 02
02, a fade (f283), D_00810702 = 2 at f347 and control at f348 at spawn
entry 2 (-363.8, 60, -649.5).

### a06_04 the beam's collapse (525 frames)

North along x -362 to z -611, east along z -611 under the cable bundle and
through the broken crate's place, north-east to the west pad and onto the
beam; at (-270, 55, -583) on f240 D_00810768 = 1 and the beam starts
script 0x827180 (3B8D = 3 at f241, 2 at f242, action 0x41, camera mode 1
with the letterbox, clip 343), clip 115 at f347 and 117 at f373 while the
player drops from y 58 to 28.9, the beam node's state 2 at f428, D_00810768
= 0xFF at f429 (c79425), control at f456 (c79452). The beat ends with the
player on the fallen beam at (-268.5, 38.8, -583.2). No hit.

### a06_05 door [3] locked (931 frames)

South down the fallen beam into the pit (two drops, action 0x0B then
0x0F, f65..f74 and f136..f149, to the floor y 17 at z -640), south along
x -240; at (-240.1, 17, -703.1) the player is hit once (action 0x3E, f257,
health 90 -> 82; the source was not traced). Up the ramp to door [3];
the first Cross (f556) is not taken, the second (f692) is at f695: the
player aligned to (-240, 19.6, -794.5), 3B8D = 2, camera mode 1 with the
letterbox, door [3]'s locked program 0x24DA40 .. 0x24DB40, one message line
(mode 02, f741..f861), control at f862 in the same place. D_00810847 stays
0x00; the end image shows the lamp above the door red, and a small pickup
(g[3], 00219550) beside the door.

## 6. Side beats

- **a01u_s0 the pickup g[11]** (358 frames): from the landing south to
  (131, -473.1) facing +x; Cross at f174, taken at f177: 3B8D = 3 then 1,
  action 0x41, the pickup's grab program 0x2482C0 .. 0x248440 (clip 66),
  request byte 1 = 0x33 at f238, the item page (ui byte 1 = 3 from f241,
  a message of mode 04, record 0x33, from f269), Triangle at f331, control
  at f338. The taken byte 0x810880 gained bit 2 (f339). The taken byte
  0x810890 also went 0x01 -> 0x03 at f31, the frame at which the
  00128C10 node g[35] (callback 0x128C10 in the sample, rising at
  (139.6, 119.9, -373.6)) had its header zeroed after a state change at
  f30: a freed 00128C10 node, as in a01u_00 and a01u_01. The item counts
  0x810C60..BF and the document bytes did not change (what the item is
  was not established).
- **a01u_s1 door [18] back to AREA22** (963 frames): from entry 6's
  walk-in end north to (120, -324) facing 0; the first Cross (f225) is not
  taken, the second (f415) is at f418: door [18]'s program 0x24D900 ..
  0x24DA00, the request B5..B8 = 16 FF 05 01 and a fade (f506), D_00810700..703
  = 16 00 05 01 at f570, overlay id 0x13 at f577, D_00810703 = 0x16 at f581,
  the arrival at AREA22 entry 5 (120, 159.5, 91.5) facing 0 at f759 (the
  walk-in, 3B8D = 4, from f760) and control at f874 at (120, 158.5, 104.6).
- **a06_s0 door [1] back to AREA01** (718 frames): from the arrival east to
  (-115, -674.5) facing +x; Cross at f73, taken at f76: the program
  0x24DE40 .. 0x24DC80, clip 67, the request B5..B8 = 01 FF 07 01 and a
  fade (f154), D_00810700..703 = 01 00 07 06 at f218, overlay id 2 at f225,
  D_00810703 = 01 at f229, the arrival at AREA01 entry 7 (-97.5, 60,
  -670.3) facing pi/2 at f514 and control at f629 at (-84.4, 60, -670.3),
  on the catwalk west of door [19]. This is the one area change open to
  AREA06 in this state, played before the keypad.
- **a06_s1 the bar** (1175 frames): from entry 2 south along x -362; near
  (-360.2, 60, -676.4) the player is hit (action 0x3E, f64, health
  90 -> 80) while the node g[11] (00219870, beside the room's west wall)
  cycles through its states 2, 3, 0 and 1 (it does so repeatedly while
  the player is near; what it spawns was not traced). South-east to the
  0x3A block and onto it (-338.7, 63, -756); Cross at f305: action 0x20,
  state 0xF at f308, hanging (action 0x21, state 0x10, clip 187) at f353
  at (-336.4, 75, -755). The stick moves the player east along the bar
  (action 0x24 at f356, then 0x21 with clips 188, 191, 193 and
  197..199) to its end, x -201.5, at f1001; Cross at f1031: action 0x23 at f1034, the fall (action 0x0D,
  state 7) from f1056, the landing on the pit's east wall at y 40.6
  (f1076) and a slide (action 0x30, state 0x1C) down to (-222.7, 24.5,
  -759.1) at the foot of a debris ramp inside the pit (control f1174).
  Infection stayed 60. (In an exploration run of the same bar the player
  was hit while hanging, state 7 at f439, and infection rose to 70.)

## 7. Tried and not kept (exploration runs, this session)

Their snapshots were scratch (`build/s88_a01u/`) and are deleted at the
end of the session; none of these facts comes from a kept beat. Treat
them as leads.

- **Ways out of the pit** (from the beam's collapse): walking up the pit's
  east wall stops at x -221.6 (y 18.8); walking back up the fallen beam
  toward the west pad stops at (-272.7, 37.1); the debris ramp at
  z -750..-770 is cut by a pillar (x -228..-220) and by a wall at x about
  -209 from its flat top (y 31.1). From the bar's end the drop slides into
  the pit (a06_s1). Not tried: the pit's north end (a hazard-striped
  shutter overgrown by a fleshy mass), grabbing ledges on the pit walls,
  the west slide in reverse. No way out was found.
- **Hazards met.** Near the crate and the cable bundle, and in the pit,
  the player was hit several times (action 0x3E) with infection +10 each
  time; bugs from the north wall (g[27]..g[29]) grab the player (action
  0x3B) on the west floor; a grab left on (the explorer's approach helper
  did not shake it off) ended after about 330 frames in a bite that set
  infection to 100 (the infected latch +0x234 = 1, then health -3 about
  every 182 frames, down to 0 in one run). The kept beats shake every grab
  off at once; none of them has a grab.
- **The crate** could be climbed (action 8, top at y 74); from its top the
  walk west stops at the cable bundle and Cross toward the room's roof was
  not taken.
- **The keypad before its page is live**: presses sent while the page
  object's +6 was still 1 were not taken; the kept beat waits for 2.

## 8. Open

1. What script 0x827180's op03/2 and op12/0 records do (the beam itself
   writes D_00810768 = 1 and 0xFF, by A06C's C; section 3.3).
2. A way out of the pit in this state (section 7), or confirmation that the
   keypad before AREA15 strands the player.
3. AREA15's function at 0x824B80: when it runs (it tests the entry byte
   D_00810702 and its node's +0xD = 0x5A) and where item 0x2B comes from;
   how AREA15 is reached (the door tables show AREA19 -> AREA15 and
   AREA03 -> AREA19; AREA03 from AREA04's door [38] (lock bit 2) or AREA02).
4. What the story order is: AREA04's door [45] with the keypad's bit (the
   NPC [2], item 0x23, AREA22's reader [9] and the room behind its door
   [10]) versus AREA15 first. Not played.
5. What the node g[11] (00219870) beside the room does, what hit the
   player in the pit and on the bar, and what the pickups g[11] (AREA01)
   and g[3] (AREA06, beside door [3]) give.
6. AREA01 from entry 7 after the keypad: 001C02E0 at the gap takes its
   D_00810766 branch on a later AREA01 load (section 2.2); not played.
7. The ladder (0x3B) and the rail (0x3D) over AREA01's gap, the ladder to
   AREA06's room roof, the save point [43], and AREA06's sub 1 (the
   director 0x825E20 that unlocks AREA04's door [42]).

## 9. Census: what the AREA01 upper floor and AREA06 execute beyond every earlier level

Lane NEXT replayed every a01u and a06 beat with the census tool's one-shot
breakpoints (hidden PCSX2, one session per beat, nothing written to a save
slot) and computed two deltas.

**Tool** (`../Extermination/tools/route_census.py`, additive; the default
segments, `report`, `exit-delta` and the `a01-` .. `a22-delta` commands are
unchanged):

- `run --segments a01u --pass A01U` replays each AREA01 upper-floor beat from
  its recorded source snapshot with route_capture's own beat function, the
  breakpoints on every boot function and every AREA01 overlay function
  (runtime = splat label + 0x40); `run --segments a06 --pass A06` does the
  same for the AREA06 beats with the AREA06 overlay.
- `a01u-delta --passes A01U` and `a06-delta --passes A06` (one generic
  `chain_delta`) list what the group runs that **no earlier level ran**.
  "Already ran" = the first-level census (`route_functions.json`), beat 15,
  and every beat of the A01, A00, A01R, A02, A04 and A22 passes in all
  phases (the `--a01-passes` .. `--a22-passes` options, defaults A01 .. A22),
  and for a06 also the A01U pass (`--a01u-passes`, default A01U). An overlay
  function of the group's area counts as already run when an earlier pass
  paused inside it with that area's overlay resident (a hit at the earlier
  group's candidate address mapped by range into this overlay). Groups, a
  function counting in the first that ran it: `<g>` (the main line, a beat
  that leaves the area up to its area-change consumer 001AD010),
  `<g>_side`, `<g>_exit` and `<g>_side_exit` (from 001AD010 on). Outputs:
  `../Extermination/build/s87/census/a01u_delta.json` and `a06_delta.json`
  (`new_functions`, `functions`, `per_beat`, `overlay_<AREA>_not_run`,
  `overlay_hits_other_overlay`).

**Replay fidelity.** Every replay completed its beat's own checks (no
retry in A01U or in the final A06 pass). A01U: a01u_00 235/235,
a01u_01 1448/1448 and a01u_s1 964/964 rows identical to the recording;
a01u_02 and a01u_s0 started one counter late (0 identical rows; a01u_02
585 identical ignoring the counter). A06: a06_05 932/932 identical; the
others started 0..2 counters off and share few or no identical rows
(a06_04 52, a06_03 2); a06_02 took 1710 frames against 1698 (the keypad's
closed loop waits for the page), a06_03 393 against 397, a06_04 527 against
525, a06_s0 717 against 718 and a06_s1 1177 against 1175. The player's end
state equals the recording in all but a06_00, a06_04 and a06_s1 (a06_00
ended on the west pad a few units from the recorded end). The replays' run
times sum to 23.5 minutes (A01U) and 17.8 minutes (A06). A first A06 pass
of an earlier a06_00 (a beam line nearer the south edge) fell off the beam
in all three of its replays; the beat was re-captured on the beam's middle
and every a06 beat re-captured from it before the final pass. That first
pass left no retained files (census/runs/A06 holds only the final pass and
there is no `_failed` folder), so this statement is the lane's own account
and cannot be checked from the outputs. As for the
earlier levels, the function sets are those of the same route shape, not
of the exact recorded frames.

**Totals.**

- **a01u**: the five beats run **895 functions** (883 boot, 12 AREA01
  overlay). Already ran: 762 in the first level, 57 in beat 15, 39 in AREA01
  play (8 of them overlay), 23 in AREA00, 3 in the revisit (overlay), 2 in
  AREA02 and 2 in AREA04. **7 are new (6,280 bytes): 6 boot, 1 AREA01
  overlay.** Five AREA06 overlay addresses were hit at AREA01 candidate
  addresses in a01u_02's exit phase with AREA06 resident (0x824340,
  0x824380, 0x8240E0, 0x824120 and 0x823CD0: inside AREA06's 0x824340,
  0x8240E0 and 0x823B50); the a06 delta counts those three functions as
  already run.
- **a06**: the eight beats run **991 functions** (985 boot, 6 AREA06
  overlay). Already ran: 778 in the first level, 57 in beat 15, 58 in
  AREA00, 43 in AREA01 play, 10 in AREA02, 4 in AREA22, 2 in AREA04 and 7 on
  the AREA01 upper floor (4 boot; 3 overlay, from a01u_02's exit phase).
  **32 are new (23,788 bytes): 29 boot, 3 AREA06 overlay.** No overlay hit
  with another overlay resident; no unattributed hit.

| Group | New | Bytes | Boot | Overlay | Decomp status |
|---|---:|---:|---:|---:|---|
| a01u (main line) | 1 | 580 | 0 | 1 | C 1 (AREA01 overlay, byte-identical) |
| a01u_side | 1 | 344 | 1 | 0 | NM 1 |
| a01u_exit (AREA06 load and arrival) | 5 | 5,356 | 5 | 0 | BM 4, NM 1 |
| a01u_side_exit (AREA22 reload) | 0 | 0 | 0 | 0 | — |
| a06 (main line) | 19 | 16,552 | 16 | 3 | BM 6, NM 6, AI 2, AW 1, AU 1, C 3 |
| a06_side | 13 | 7,236 | 13 | 0 | BM 7, NM 4, AW 2 |
| a06_exit | 0 | 0 | 0 | 0 | — (no main-line beat leaves AREA06) |
| a06_side_exit (AREA01 reload at entry 7) | 0 | 0 | 0 | 0 | — |

Status abbreviations as in SECOND_LEVEL_ROUTE.md section 6 (BM byte-matched
C, NM NEARMISS, AW word assembly, AI inline asm, AU undecompiled assembly,
C overlay C); the statuses are the census's classification of the decomp's
working tree at the run. The three AREA06 overlay functions count as C
because of lane A06C's untracked files (section 3); at commit c3d4ca7
they were assembly.

**New functions by the beat that first runs them** (census frames). No
port-coverage column is given: which port files name an address changes as
other lanes add untracked files, and a name is not evidence of a verified
translation. To check an address, run in the port
`grep -rlI -i <address> src tools` (for an overlay function, the runtime
address without the 0x; 0x823580 is shared by other areas' overlays, so a
hit there is not specific to AREA06):

AREA01 upper floor (A01U):

| Function | Bytes | Status | Subsystem | First |
|---|---:|---|---|---|
| overlay 0x826950 | 580 | C | overlay_AREA01 | a01u_01 f413 |
| 00219870 | 1,748 | NM | unknown_06 | a01u_02 f854 |
| 00219F50 | 548 | BM | unknown_06 | a01u_02 f854 |
| 0021A180 | 700 | BM | unknown_06 | a01u_02 f855 |
| 001ECA20 | 220 | BM | hud_objects | a01u_02 f872 |
| 001944B0 | 2,140 | BM | init_io | a01u_02 f969 |
| 00176180 | 344 | NM | actor_anim | a01u_s0 f25 |

- a01u_01 f413: 0x826950 (`func_overlay_AREA01_00826910.c`) is the op09
  callback of script 0x82B590: the script's record at runtime 0x82B650 is
  an op09 whose callback word is 0x826950 (AREA01_OVERVIEW.md, the table
  row and the script-callback line). It is not a method of placement
  [45]; [45]'s code (0x8267C0) only starts the script. It first ran after
  the catwalk script started (f408/f409 in the recording).
- a01u_02 f854..f969: the AREA06 load and arrival (the recording's arrival
  frame is f853). 00219870 is the behaviour of AREA06's deferred node g[11].
- a01u_s0 f25: the walk toward the pickup (before the press at f174).

AREA06 (A06):

| Function | Bytes | Status | Subsystem | First |
|---|---:|---|---|---|
| overlay 0x823580 | 1,420 | C | overlay_AREA06 | a06_00 f1 |
| overlay 0x824560 | 6,332 | C | overlay_AREA06 | a06_00 f1 |
| 001EA210 | 40 | BM | hud_objects | a06_00 f37 |
| 001CE860 | 1,656 | AU | gs_upload | a06_00 f39 |
| 0021A500 | 1,468 | NM | unknown_06 | a06_00 f39 |
| 001EBE10 | 124 | NM | hud_objects | a06_00 f296 |
| overlay 0x8242c0 | 128 | C | overlay_AREA06 | a06_02 f340 |
| 002072C0 | 136 | BM | draw2d | a06_02 f369 |
| 00207350 | 1,692 | BM | draw2d | a06_02 f370 |
| 001FD0E0 | 908 | NM | audio | a06_02 f371 |
| 002079F0 | 436 | NM | draw2d | a06_02 f371 |
| 001FDDB0 | 696 | NM | audio | a06_02 f372 |
| 00207BB0 | 240 | BM | draw2d | a06_02 f632 |
| 002072A0 | 20 | BM | draw2d | a06_02 f644 |
| 00123020 | 324 | AW | lowmem | a06_02 f780 |
| 00207CA0 | 36 | AI | draw2d | a06_02 f1036 |
| 00207CD0 | 36 | AI | draw2d | a06_02 f1056 |
| 001AFF90 | 108 | BM | entity_sys | a06_02 f1576 |
| 0021AE90 | 752 | NM | unknown_06 | a06_05 f257 |
| 0021A440 | 184 | BM | unknown_06 | a06_s1 f54 |
| 001A8F40 | 188 | NM | unknown_01 | a06_s1 f65 |
| 00169250 | 1,104 | NM | entity_update | a06_s1 f311 |
| 0019F680 | 168 | AW | level_world | a06_s1 f313 |
| 001885B0 | 28 | BM | actor_anim | a06_s1 f355 |
| 00169730 | 3,448 | NM | entity_update | a06_s1 f356 |
| 001696A0 | 140 | BM | entity_update | a06_s1 f357 |
| 00181D70 | 168 | BM | actor_anim | a06_s1 f357 |
| 001814E0 | 584 | NM | actor_anim | a06_s1 f392 |
| 00181B80 | 20 | BM | actor_anim | a06_s1 f393 |
| 001811F0 | 572 | BM | actor_anim | a06_s1 f436 |
| 00181BA0 | 460 | BM | actor_anim | a06_s1 f436 |
| 00181430 | 172 | AW | actor_anim | a06_s1 f977 |

- a06_00 f1: the AREA06 owners 0x823580 ([9]) and 0x824560 (the beam [11])
  run every frame; 001CE860 and 0021A500 first ran at f39 on the east floor.
- a06_02 f340..f1576: the keypad page. 002072C0 installs the page
  callback 00207350 (STATUS_PAGES.md); by 00207350's C it calls 002079F0
  every frame, 00207BB0 while the pad is live, 002072A0 on each cursor
  move, 00123020 to
  compare the typed string (census f780; the recording's OK press is at
  f767) and 001AFF90 at its shutdown; 00207CA0 and 00207CD0 ran in its
  result sub-states. 001FD0E0 and 001FDDB0 (audio) first ran as the page
  opened.
- a06_05 f257: 0021AE90, at the census frame of the recording's hit in
  the pit (f257); what ran it was not traced.
- a06_s1 f54..f977: 0021A440 and 001A8F40 around the hit near g[11]
  (recording f64); the bar: the pole entry and hanging (f311..f357:
  00169250, 0019F680, 001885B0, 00169730, 001696A0, 00181D70), moving along
  it (001814E0, 00181B80, 001811F0, 00181BA0) and 00181430 at f977.

No boot function is new in either exit phase: the AREA22 reload at entry 5
(a01u_s1) and the AREA01 reload at entry 7 (a06_s0) run only functions that
already ran in the earlier levels; the AREA06 load and arrival (a01u_02's
exit phase) is where AREA06's first five new functions ran.

**The AREA06 overlay.** Of its ten candidate functions, six ran with
AREA06 resident: 0x823580, 0x824560 and 0x8242C0 are new here; 0x823B50,
0x8240E0 and 0x824340 already ran in a01u_02's exit phase. Not hit in the
a06 replays: 0x823540 (the entry pad), 0x823B10 (a 64-byte piece reached
through a code pointer in 0x823B50), 0x824290 (the area init, which runs at
the AREA06 load inside 001E7780, key 0x600, during a01u_02's exit phase;
the A01U pass armed AREA01's candidates, so no breakpoint could catch it)
and 0x825E20 (the sub-1 owner; sub 1 was not loaded).

**Reproduce** (hidden PCSX2):

```sh
# decomp repo, .venv python, repo root
.venv/bin/python tools/route_census.py run --segments a01u --pass A01U
.venv/bin/python tools/route_census.py a01u-delta --passes A01U
.venv/bin/python tools/route_census.py run --segments a06 --pass A06
.venv/bin/python tools/route_census.py a06-delta --passes A06
```
