# AREA01 revisit and AREA02: fourth-level route, original ground truth

Lane "NEXT" (capture), 2026-09-28 (session s88). Original executable SHA-256
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

AREA00's progression exit (THIRD_LEVEL_ROUTE.md, beat a00_10) ends in AREA01
sub 0, spawn entry 0, with D_0081075E = 0xFF. This document records what the
original does from there: the AREA01 revisit (the event in the train room, the
lowered bridge, the north room and its door into AREA02) and AREA02 from its
arrival to its progression exit, the area change to AREA04. The tool, the
conventions and the row format are those of SECOND_LEVEL_ROUTE.md and
THIRD_LEVEL_ROUTE.md: hidden PCSX2, exact one-frame steps, pad input only, no
teleports and no memory writes, one trace and one resumable snapshot per beat.
Nothing here is port behaviour.

**Status: both progression exits are reached.** Main line: a01r_00..a01r_03
(AREA01 revisit, ending with the area change into AREA02 sub 1) and
a02_00..a02_05 (AREA02, ending with the area change into AREA04 entry 0 and
control there): 10 beats, 18,375 frames, counters 44089..62479. Side beats:
a01r_s0 (the pickup the event spawns), a01r_s1 (door [14], locked) and a02_s0
(the bed, where the item the event gives cures the infection). Section 9 has
the census replays and the a02 delta.

In-game text (messages, prompts, titles, item descriptions) is described, never
quoted. Positions are world units; yaw is the player's +0xC4 (X = sin, Z = cos).

## 1. Tool, outputs and conventions

The beats are two opt-in groups of `../Extermination/tools/route_capture.py`:
`a01r` (the revisit) and `a02` (AREA02). `--beats all` is unchanged (beats
00..14); the AREA01, AREA00 and C7 groups are unchanged.

```sh
# decomp repo, .venv python, repo root
.venv/bin/python tools/route_capture.py run --beats a01r          # the revisit group, in order
.venv/bin/python tools/route_capture.py run --beats a02           # the AREA02 group, in order
.venv/bin/python tools/route_capture.py run --beats a02_04,a02_05 # some beats
.venv/bin/python tools/route_capture.py events --beats a01r_02    # change log of one trace
.venv/bin/python tools/route_capture.py verify --beats a01r,a02   # every snapshot resumes
```

- **Session, resuming, navigation, Use**: as SECOND_LEVEL_ROUTE.md section 1.
  a01r_00 starts from `build/s87/route_a00/a00_10_progression_exit/state.p2s`.
- **Outputs**: `../Extermination/build/s87/route_a01r/<beat>/` and
  `../Extermination/build/s87/route_a02/<beat>/` (ignored): `trace.json`,
  `state.p2s`, `eeMemory.bin`, `gs.bin`, `scratchpad.bin`, `original.png`,
  `snapshot.json`.
- **Row format**: the AREA00 row (THIRD_LEVEL_ROUTE.md section 1: player,
  camera, request, fade, message, `hp`, `d9`, `f759`, `story758`, `area4`,
  `slots`, `bd8`, `ovl`, `taken`, `docs`, `msgrec`, `locks`, `wpn`, `d7dc`)
  plus `story760` (D_00810760..63), `s784` (D_00810784..87), `df`
  (D_008107DF, the revisit event's stage), `e0` (D_008107E0, the bridge
  script's counter), `e1` (D_008107E1, AREA02's switch, car and gate bits),
  `inf` (player +0x228, infection, 0..100), `infected` (player +0x234) and, in
  a02 rows, `item20` (D_00810C84, the count of item 0x20). Each owner also
  carries `rot` (+0xC0..CF). `events` logs `df`, `e0`, `e1`, `story760`,
  `s784` and `inf` besides the AREA00 keys.
- **Long frames and a killed emulator.** Some frames of the revisit event and
  of AREA02's scripts take more than the 30 s default of host time. Every new
  beat calls `next_long_frames` (route_capture.py): all DebugServer calls retry
  for up to 1800 s and fail at once if the emulator process has gone. In the
  exploration runs of this session two runs stopped on the default timeouts
  during such a frame, and twice the emulator process disappeared mid-run
  (after a frame of about 260 s) without a crash report, while other lanes
  loaded the host to a load average of about 150; none of the kept captures
  was affected.

### 1.1 Owners sampled

**Revisit** (`A01R_OWNERS`, AREA01 sub 0 reloaded by a00_10; measured in the
a00_10 end snapshot). The pool differs from the first visit's
(SECOND_LEVEL_ROUTE.md section 1): the condition-3 records of the deferred
group 0x828A00 are spawned first, and the first visit's NPC [36] (0x825350) is
not in this load.

| Row key | Node | Callback | Record | Role on the route |
|---|---|---|---|---|
| `shaft_door_r12` | 0x7ADD60 | overlay 0x823580 | [12] | the shaft door (not used) |
| `r13_158D30` | 0x7AE050 | 00158D30 | [13] | beside it |
| `door_r14` | 0x7AE340 | 001BC350, model 0x15 | [14] (-20.5, 0, -192), id 1\|0x80 | north room: locked (a01r_s1) |
| `door_r15` | 0x7AE630 | 001BC350 | [15] | control-room door (not used) |
| `door_r16` | 0x7AE920 | 001BC350, model 0x03 | [16] (50, 0, -220.5), id 3\|0x80 | north room: into AREA02 (a01r_03) |
| `bridge_r41` | 0x7B3290 | overlay 0x8261A0 (+0x0D 2) | [41] (0, 3, -525) | the south bridge half |
| `bridge_r42` | 0x7B3580 | overlay 0x8261A0 (+0x0D 3) | [42] (0, 3, -315) | the north bridge half |
| `r45_8267C0` | 0x7B3E50 | overlay 0x8267C0 | [45] (0, 156.9, -337.9) | not sampled beyond its header |
| `g5_825950` | 0x7A64F0 | overlay 0x825950, model byte +0xD 0x47 | 0x828A00[5] (-9, 0, -576) | runs the event (D_008107DF) |
| `g7_825950` | 0x7A6AD0 | overlay 0x825950, +0xD 0x4B | 0x828A00[7] (-5, 0, -543) | the second character of the event |
| `g6_826CF0` | 0x7A67E0 | overlay 0x826CF0 | 0x828A00[6] | |
| `g0_823CD0`..`g4_823CD0` | 0x7A5640, 0x7A5930, 0x7A5C20, 0x7A5F10, 0x7A6200 | overlay 0x823CD0 | 0x828A00[0..4] (-45.6, 30..50, -568.3) | the event's creatures (freed by its end; the nodes are reused) |

**AREA02** (`A02_OWNERS`: the sub-1 table `s1_*` measured at the a01r_03
arrival, the sub-0 table `s0_*` after a02_00's sub change; the two loads
reuse the pool addresses, so every a02 row samples both and only the resident
sub's keys name real nodes).

| Row key | Node | Callback | Record (sub) | Role |
|---|---|---|---|---|
| `s1_pick_g0..g2` | 0x7A5640, 0x7A5930, 0x7A5C20 | 0015AFA0 | 0x825B50[0..2] (sub 1) | three pickups (not taken on the kept route) |
| `s1_bed_r0` | 0x7A5F10 | 00159620, model 0x36 | [0] (75, 0, -188.2) | the bed (a02_s0) |
| `s1_r1_1C4AF0` | 0x7A6200 | 001C4AF0 | [1], same position | |
| `s1_r2_159970` | 0x7A64F0 | 00159970, model 0x37 | [2] (57.5, 15, -292.6) | not used |
| `s1_save_r3` | 0x7A67E0 | 00159B90 | [3] (116.2, 8, -184) | save terminal, not used |
| `s1_panel_r4` | 0x7A6AD0 | 00159210, model 0x2C | [4] (80.1, 8.2, -244) | not used |
| `s1_door_r5`, `s1_door_r6` | 0x7A6DC0, 0x7A70B0 | 001BC350 | [5] id 1\|0x80, [6] id 2 | back to AREA01 / room move (not used) |
| `s0_lock_r21` | 0x7AD780 | 001582E0 | [21] (-38.6, 7.9, -187.2) | beside door [22] |
| `s0_door_r22` | 0x7ADA70 | 001BC350, model 0x15 | [22] (-30.5, 0, -187.3), id 0\|0x80 | to AREA01 entry 3 (not used) |
| `s0_panel_r24` | 0x7AE050 | 00158EC0, model 0x14 | [24] (424.5, 30, 100) | the battery panel (a02_04) |
| `s0_door_r25` | 0x7AE340 | 001BB860, model 0x17 | [25] (440.2, 15, 109.9), id 3\|0x80 | the progression exit (a02_05) |
| `s0_lamp_r26` | 0x7AE630 | 00158BD0 | [26] (440, 38.4, 100) | |
| `s0_r27_825100` | 0x7AE920 | overlay 0x825100, +0xD 0x15 | [27] (-78.3, 0, -55.8) | not used |
| `s0_turntable_r28` | 0x7AEC10 | overlay 0x825100, +0xD 9 | [28] (-0.2, -4, 10) | the junction's floor (ground node on the way north) |
| `s0_r31_823980` | 0x7AF4E0 | overlay 0x823930 -> 0x823980 | [31] (140, 0, -25), class 0x89 | the switch (a02_01) |
| `s0_k7_r32` | 0x7AF7D0 | overlay 0x823930 -> 0x824020, kind 7 | [32] (-320, 0, 10), model 0x54 | the car (a02_02) |
| `s0_k8_r33`, `s0_k11_r34`, `s0_k10_r35` | 0x7AFAC0, 0x7AFDB0, 0x7B00A0 | same, kinds 8, 11, 10 | [33], [34], [35] (210.9, 0, 9.1) | [35] is climbed in a02_03 |
| `s0_gate_r36`, `s0_gate_r37`, `s0_gate_r38` | 0x7B0390, 0x7B0680, 0x7B0970 | overlay 0x823D70, kinds 16, 15, 14 | [36] x -280.9, [37] and [38] x 185 | the tunnel gates |

## 2. The levels, from the original's own data

AREA02 tables below are from `../Extermination/tools/area_overview.py --area 2
--ram <snapshot>/eeMemory.bin --out <file>` over the a01r_03 end snapshot (sub
1) and a02_00's end snapshot (sub 0); the outputs were scratch and are
regenerated by the same command. The code descriptions are from the decomp's
C (byte-identical unless a row says NEARMISS; the AREA02 overlay C is the
uncommitted work of lane A02C in the decomp's working tree,
docs/AREA02_OVERLAY.md: 16 of the 17 functions have byte-identical C, 15 of
them link from their compiled C objects; 008254E0 (runtime 0x825520) is
marked NEARMISS and its link takes the function from its splat pieces, and
00823500 (runtime 0x823540) is the asm entry pad).

### 2.1 The AREA01 revisit

- **The event.** The deferred group 0x828A00 has eight records with condition
  3 (AREA01_OVERVIEW.md section 7.2); in the revisit's load they are live
  (their condition is not met on the first visit). 0x825950 (+0xD 0x47, g[5])
  runs the event by D_008107DF (byte-identical C, overlay 0x825950 and its
  callees 0x825BE0, 0x825D30, 0x825EA0, 0x825F00):
  - stage 0/1: in state 1 it waits for the player inside the quad 0x82B090
    (001B1EA0; a trapezoid, its four corners in RAM order (x, z) = (100,
    -640), (100, -600), (-41, -600), (-75, -640), y 0), then D_008107DF = 1 and script
    0x82AA90; at its end D_008107DF = 2;
  - the five 0x823CD0 creatures (their run state tests D_008107DF == 2) are
    moved by 0x824770 along the spot table 0x829DE0; it sets D_008107DF =
    0x10 when the node whose event byte +0xE2 is 0 reaches its spot;
  - 0x10: script 0x82AC10; at its end D_008107DF = 0x40, byte +4 of the
    node at its +0x1C is set to 3 and script 0x82AD10 starts; 0x40 -> 0x80 at
    that script's end;
  - 0x80: script 0x82AD90 (ops 1A, 13, 0C, 0A, 14 (spawns the group
    0x82A900), 00, 10, 0D/0, 18, 01, 0D/2, 07/5), whose op0D sub 0 waits for
    the camera path time (camera +0x74) to reach its duration (+0x78); at its
    end D_008107DF = 0xFF, 001C47A0(0x20) (item 0x20 given, its status page
    opened), 001C4760(5, 1), the groups 0x829220 (six 0012A5D0 creatures) and
    0x8291C0 (one 0015AFA0 pickup) spawned, 001FAE70(0).
  The second character (+0xD 0x4B, g[7]) goes to state 3 when D_00810845 bit
  5 is set, and in state 1 only calls 0x825FC0 once D_008107DF is 0xFF.
- **The bridge.** With D_0081075E = 0xFF the two 0x8261A0 halves take their
  state-0 tilts (SECOND_LEVEL_ROUTE.md section 9.3): [41] pi/18 (measured
  0.17453 in a01r_02 row 0) and [42] 0. [41] is a ramp up from the train
  room's north edge; its north end is 17.8 above the floor (the player leaves
  it at (1.4, 17.78, -418.6), a01r_02 f422) and drops onto [42] at y 0. On
  [42], inside the quad 0x82CC60 at y <= 2, [42] starts script 0x82B0D0
  (SECOND_LEVEL_ROUTE.md section 9.3): D_008107E0 = 1, 0xE0, 2, then 0xFF with
  D_00810760 = 0xFF, and [41] turns up to 0.87266 (50 degrees) behind the
  player. The way back south is closed from then on in this load.
- **The north room** (level collision, `tools/export_collision.py`'s decoder
  over the RAM grid at D_0028A598): floor y 0 over x -50..50, z -320..-189.5;
  the bridge's rails continue north (attribute 4, x -8..8); raised strips at y 9
  along x 30..40 with a recess at z -235..-215 (x 40..50) that holds door
  [16]; the north wall at z -194 holds door [14]. The drums [3] (15.1, 0, -270)
  and [4] (33.8, 0, -293.9) (00156620) stand in the room. Three of the six
  0x829220 creatures are placed here, (-1.7, 18.6, -192.4), (2, 15.9, -192)
  and (22, 3.6, -277); the other three at (-12.4, -1.6, -547), (11.2, -1.6,
  -549) and (17.9, -1.6, -548), south of the bridge.
- **Doors** (AREA01_OVERVIEW.md section 5): [16] = door id 3, record
  02 01 01 01, area change to AREA02 entry 1 sub 1; [14] = id 1, record
  02 00 00 00, AREA02 entry 0 sub 0, model 0x15, lock-gated on D_00810841[1]
  bit 1 (0 in every capture).

### 2.2 AREA02 registries

- Overlay `AREA02.BIN`, header id 3. Three sub-states: placement tables
  0x827830 (sub 0, 58 records), 0x828170 (sub 1, 14) and 0x8283D0 (sub 2,
  53). Level data: INDEX sector 6 (`chunk06`, nested `chunk06.n0/.n1/.n2`).
- **Spawn table** (0x30-byte records; subs share positions and yaws):

  | Entry | Position | Yaw | Used by |
  |---:|---|---:|---|
  | 0 | -35, 0, -178 | 0 | AREA01 door [14] (not played) |
  | 1 | 65, 0, -225 | pi/2 | AREA01 door [16]: the sub-1 arrival (a01r_03) |
  | 2 / 3 | 104, 0, -245 / 104, 0, -259 | 0 / pi | sub-1 room move door [6] |
  | 4 | 439, 15, 99 | pi | AREA04's door back (not played) |
  | 5 | 40, 0, -146 | -pi/2 | the duct's sub-0 end (a02_00); record +0x14 = 2 |
  | 6 | 90, 0, -170 | pi | the duct's sub-1 end; +0x14 = 2 |

- **Doors** (destination table D_0024E140[2] = 0x24DFC0): id 0 = 01 03 00 00
  (sub-0/2 record [22], AREA01 entry 3; model 0x15), id 1 = 01 05 00 00
  (sub-1 [5], AREA01 entry 5), id 2 = 03 02 (sub-1 [6], room move), id 3 =
  04 00 00 00 (sub-0/2 [25], AREA04 entry 0; slider model 0x17, lock-gated on
  D_00810841[2] bit 3).
- **Sub 1** (the room behind door [16]; the arrival shows the area's title card
  for this sub): two rooms, north (x 60..120, z -250..-170) and south (z
  -305..-250), joined by door [6]. The north room holds the bed [0] and
  001C4AF0 [1], the save terminal [3], the console [4] and door [5]; the south
  room holds [2] (00159970) and two of the three pickups. An attribute-0x37
  duct square lies at x 85.4..95.4, z -180..-170 (node axis +z); the duct
  runs north through the wall to z -150..-143 (a turn square, attribute 0x1F)
  and west to x 40.
- **Sub 0** (after the duct): a north-south tunnel (x -40..40) and an
  east-west tunnel (z -25..45) crossing at [28] (0, 10); rails along both. [28] is overlay
  0x825100 with +0xD 9; its callee 0x825520 turns a player standing on a
  +0xD-9 object about the object's centre, so [28] reads as a turntable; it
  was not seen turning on the route. East of the junction [31] stands at (140, -25); the gates [37],
  [38] cross the east-west tunnel at x 185, [36] at x -280.9. The east end
  (x 285..487, z 35..175) is a platform at y 15 above the tunnel floor, with
  the battery panel [24] and door [25] on its north wall. A ladder (attribute
  0x32, x 122..136, z 38.6..49, axis +z) on the tunnel's north side leads to a
  ledge at y 50 (x 125..160, z 50..80).

### 2.3 Story and progress bytes

| Byte | Written by | Measured |
|---|---|---|
| D_008107DF (counter 7) | 0x825950 and its callees, 0x824770 (= 0x10) | 0 -> 1 at a01r_01 f45, 2 f214, 0x10 f567, 0x40 f1065, 0x80 f1072, 0xFF f8063 |
| D_0081075F (flag 7) | script 0x82AD90's last record (op07 sub 5, slot 7) | 0xFF at a01r_01 f8063 |
| item 0x20 (D_00810C84) | 001C47A0(0x20) at the event's end; used up at the bed | 1 at a01r_01 end; 0 at a02_s0 f618 |
| D_008107E0 (counter 8) | [42]'s script 0x82B0D0 and [42]'s code | 1 at a01r_02 f446, 0xE0 f511, 2 f982, 0xFF f984 |
| D_00810760 (flag 8) | script 0x82B0D0 (op07 sub 5, slot 8) | 0xFF at a01r_02 f984 |
| D_00810701/702 (sub, entry) | the door record; 0016DE40 in the duct | 1/1 at the AREA02 arrival (a01r_03 f565); 0/5 at a02_00 f1021 (no overlay reload) |
| D_00810761 (flag 9) | 0x823980 (= 1 at [31]'s Use); 0x824020 (= 0xFF when the car finishes) | 1 at a02_01 f485; 0xFF at a02_02 f487 |
| D_008107E1 (counter 9) | 0x823980 (= 1, then \|= 2 at script 0x826780's end); 0x8242F0 (the car: bit 2 past x -280, bit 3 past x 140, bit 7 at its stage 6); kind 8 (bit 6); 0x824020 (= 0xFF) | 1 a02_01 f485, 3 f609; 7 a02_02 f3, 0x47 f163, 0x4F f405, 0xCF f486, 0xFF f487 |
| D_00810843 (D_00810841[2]) | the battery panel [24] | 0x08 at a02_04 f984 |
| D_00810CB2 (battery charge, half units) | the panel's prompt (Yes) | 8 -> 0 over a02_04 f619..f709 (2 per 30 frames) |
| D_008107E4 (counter 0xC) | AREA04's arrival (not traced) | 1 at a02_05 f1630, the frame control returns in AREA04 |
| infection (player +0x228) | the bugs' grab (player action 0x3E) | 40 at the a00_10 end (it became 40 in AREA00's a00_09); 50 and 60 in a02_03 (f659, f851); 0 at the bed (a02_s0 f860) |

Health stays 90 through the whole main line (the one hit of a00_09), and
reaches 100 only at the bed.

### 2.4 What decides each step (code, then capture)

- **[31] and the car** (0x823930 dispatch by +2: kind 9 -> 0x823980, kind 4
  -> 0x824020). [31]'s Use (+0xB bit 2) sets D_00810761 = D_008107E1 = 1 and
  starts script 0x826780 (ops 07/2, 0F/1, 00/0, 01/9, 02/0, 12/0, 03/1,
  07/4); at its end D_008107E1 \|= 2. The kind-7 node waits for bit 1, then
  0x8242F0 runs it along the rails (scripts 0x826980..0x826A80, turns
  toward 0.314 .. -0.319) and sets bit 2 once +0xB0 > -280, bit 3 once > 140
  (with sounds 0x8B3, 0x8B4, 0x8B5 and an effect), bit 7 at its stage 6;
  when it finishes, 0x824020 sets D_00810761 = D_008107E1 = 0xFF. Kind 10
  ([35]) waits for bit 7 and spawns the group 0x825C00 (eight 0012A5D0
  creatures); kind 11 waits for bit 6.
- **The gates** (0x823D70): kind 14 at x > 170 ([38]) waits for bit 3, counts
  5 frames and frees itself, calling 0019C6F0(0x1D, 1) and (0x1E, 1); kind 15
  ([37]) waits for bit 3 and then animates; kind 16 ([36]) waits for bit 2;
  kind 14 at x < -270 frees itself on bit 2. With D_00810761 already set at
  a later load, kind 14 frees itself at state 0 and the others start open.
- **The car kills.** In an exploration run the player stayed on the rails at
  (121, 0, 12), where script 0x826780 leaves him: the car reached him at x
  ~111 and health went to 0 (player action 0x3E, +5 = 0x12). The ladder north
  of that spot is the way out; nothing else on the route was tried.
- **The battery panel** (00158EC0, byte-identical C, model 0x14): its Use
  runs 00157860(p, anim, 1), whose arg2 == 1 path tests the battery item
  D_00810C7F (1 on this route since AREA11's beat 01); the capture then shows
  the battery page's prompt, and after Yes the charge is spent and
  D_00810841[2] bit 3 (door [25]'s id) is set (a02_04). Which instruction
  writes the bit was not traced.
- **Door [25]** (001BB860, NEARMISS C; SECOND_LEVEL_ROUTE.md section 9.1):
  model 0x17 is one of the lock-gated models (0x16, 0x17, 0x3E), so it opens
  only once D_00810841[2] bit 3 is set.
- **The duct's sub change.** The crawl (0016DE40, the duct states of
  THIRD_LEVEL_ROUTE.md section 2.5) switches the load from sub 1 to sub 0 in
  the middle of the duct, with a fade and without an overlay reload
  (a02_00 f957..f1269), and the exit puts the player at entry 5. Which
  instruction selects sub 0 was not traced.

## 3. Route table

Frames `f` are trace frames; `c` is the main-loop counter 0x70003B64.

| Beat (folder) | Source | Frames | Counters | Presses | What happens |
|---|---|---|---|---|---|
| `a01r_00_to_train_room` | a00_10_progression_exit | 1381 | 44089..45470 | Cross f962 | entry 0 -> tunnel -> the crate stack (ledge grab, pull-up, drop) -> (32.2, 0, -645.5), just south of the event's quad; taken byte 0x81088C 0x00 -> 0x40 (f208) -> 0x60 (f948) |
| `a01r_01_event` | a01r_00 | 8121 | 45471..53592 | Triangle f8096 | the event: **D_008107DF 1 .. 0xFF**, **D_0081075F = 0xFF**, item 0x20 given (page closed with Triangle) |
| `a01r_02_bridge` | a01r_01 | 1023 | 53593..54616 | — | up [41] (pi/18), onto [42]: script 0x82B0D0, **D_008107E0 -> 0xFF, D_00810760 = 0xFF**, [41] to 50 degrees |
| `a01r_03_door16` | a01r_02 | 937 | 54617..55554 | Cross f400 | north room -> door [16]: **AREA02 sub 1 entry 1**, overlay id 3 |
| `a02_00_duct` | a01r_03 | 1412 | 55555..56967 | Cross f225 | sub-1 duct square -> crawl north and west -> **sub 0, entry 5** at (35, 0, -146) |
| `a02_01_switch` | a02_00 | 629 | 56968..57597 | Cross f482 | south tunnel -> junction -> [31]: script 0x826780, **D_00810761 = 1, D_008107E1 = 3**; left on the rails at (121, 0, 12) |
| `a02_02_ladder_escape` | a02_01 | 1015 | 57598..58613 | Cross f95, f637 | up the ladder while the car passes; **D_008107E1 = D_00810761 = 0xFF**; back down |
| `a02_03_over_wreck` | a02_02 | 1021 | 58614..59635 | Cross f241, f752 (not taken), f794 (not taken), f929 | east on the rails, climb onto [35] (y ~21), drop at x ~268, ledge climb onto the east platform (y 15); infection 40 -> 60 |
| `a02_04_panel` | a02_03 | 1117 | 59636..60753 | Cross f375, Left f593, Cross f615 | panel [24]: battery prompt, Yes: charge 8 -> 0, **D_00810841[2] = 0x08** |
| `a02_05_progression_exit` | a02_04 | 1719 | 60760..62479 | Cross f171 | door [25]: **AREA04 entry 0**, overlay id 5, arrival script, control f1630 |
| `a01r_s0_pickup` | a01r_01 | 373 | 53593..53966 | Cross f185, Triangle f346 | the event's pickup 0x8291C0[0] |
| `a01r_s1_door14_locked` | a01r_02 | 759 | 54619..55378 | Cross f479 | door [14] from the north room: the locked-door program |
| `a02_s0_mts_bed` | a01r_03 | 1007 | 55555..56562 | Cross f324 (not taken), f376, Left f593, Cross f615 | the bed [0] with item 0x20: use-item prompt, Yes: **infection 40 -> 0, health 90 -> 100** |

Main line a00_10 -> a01r_00 -> ... -> a02_05: 18,375 frames. Every snapshot
was reloaded after its capture and resumed on the first check (no tail idle).
The counter gap between a02_04's end (60753) and a02_05's start (60760) is the
snapshot reload.

## 4. The revisit (a01r)

### a01r_00 to the train room (1381 frames)

From (-40, -35, -1261.9) the first visit's paths (SECOND_LEVEL_ROUTE.md
a01_04: the tunnel, then the crate stack from the south: the walk toward the
stack stops at (13.8, 1.0, -729.45) (f930); Cross at f962 there, yaw -0.058
(facing +z), grabs the ledge (action 8, clip 112 from f968, rising from f988),
stick up pulls up, walking on drops off the north side) and north to (32.2, 0,
-645.5). D_008107DF stays 0. The taken bytes change in this beat: 0x81088C
(the `taken` field, base 0x810860, +0x2C) goes 0x00 -> 0x40 at f208 (44297,
the player in the tunnel at (-18.4, -60, -1193.2)) and 0x40 -> 0x60 at f948
(45037, the player standing at (13.8, 1.0, -729.4) before the crate climb,
no button pressed). Which code writes them was not traced. They stay 0x60
through a01r_01.

### a01r_01 the event (8121 frames)

| Frame (counter) | Event |
|---|---|
| f45 (45516) | the player enters the quad at (19.0, 1, -639.7): D_008107DF = 1, g[5]'s script pointer 0x82AA90; 3B8D = 3 then 2 (f46, f47), action 0x41 |
| f49 | the player is placed at (9, 0, -620), yaw pi |
| f214 | D_008107DF = 2 (script 0x82AA90 done) |
| f567 | D_008107DF = 0x10; script 0x82AC10 (records 0x82AC10 .. 0x82ACD0) |
| f571..f1091 | a message (message mode 02; 00 again at f1092) |
| f1065, f1072 | D_008107DF = 0x40 (script 0x82AD10), then 0x80 (script 0x82AD90) |
| f1138..f7998 | a second message (mode 02; 00 again at f7999) |
| f1139..f8062 | the camera cinematic of 0x82AD90 (camera +4 = 3; the script waits in its op0D sub 0 record 0x82AF50 from f1139 to f8061) |
| f8062..f8063 (53534) | the player at (6, 0, -624); D_008107DF = 0xFF, D_0081075F = 0xFF, 3B8D = 0 |
| f8066 | the status screen opens on the healing-items page with item 0x20 (count 1); its description is about curing infection |
| f8093..f8099 | message mode 04 (00 again at f8100) |
| f8096 | Triangle; control at f8101 |

The pool after the event (a01r_01 end): g[5] freed (+4 = 3), the 0x823CD0
nodes freed and reused, the six 0x829220 creatures and the 0x8291C0 pickup
live. The taken bytes are constant through this beat (0x81088C = 0x60 on
every row, set in a01r_00).

### a01r_02 the bridge (1023 frames)

North along x ~0: on [41] (ground 0x7B3290) from f290, its top at f422
(1.4, 17.78, -418.6), a fall (action 0x0B) and the landing on [42] (ground
0x7B3580, action 0x0F) at f446 at (-4.9, 0, -410.1), inside the quad
0x82CC60: D_008107E0 = 1 and 3B8D = 2 (f447), 0xE0 (f511), a message line
(f512..f983), [41]'s tilt rising from 0.17453 (f600 0.264, f900 0.787) to
0.87266 (f984), D_008107E0 = 2 (f982), and at f984 (54577) D_00810760 = 0xFF,
D_008107E0 = 0xFF, the player moved to (-4.93, 0, -369.98), control. [42]
stays at 0 throughout.

### a01r_03 door [16] (937 frames)

North along x 0 to z -232, east into the recess; Use at f400 from (45.5,
-222.7) facing +x: the player aligned to (45, 0, -225.5), action 0x41, door
clip 0x45 (f407), fade from f501, the request block B5..B8 = 02 01 01 01,
D_00810700..702 = 02 01 01 at f565, overlay id 3 at f572, the arrival at entry
1 (65, 0, -225) at f733, the walk-out (3B8D = 4, f734..f847) to (78.1, 0,
-225) and control at f848. No creature reached the player on the kept route
(an exploration run that went east of the drums was held at (7.5, -274) and
bitten: health 90 -> 46).

## 5. AREA02 (a02)

### a02_00 the duct (1412 frames)

From (78.1, -225) east of the bed (whose collision stops a walk from the south
at z -199.2) to (91.0, -174.5); Use at f225 facing +z: snapped to (90.4, 0.05,
-175), +5 = 0x18, action 0x2C (clip 338), a fade (f310..f373), the crawl (+5 =
0x19, action 0x2D) north to (90.15, -146.5), a quarter turn (stick x) to yaw
-pi/2, west along z -146.5 to x 47.9 (f957), a fade, D_00810701/702 = 0/5 at
f1021, the duct exit (action 0x2E, +5 = 0x1A) at spawn entry 5 (40, 0, -146)
at f1270, out to (35, 0.12, -146) and control at f1344. The overlay stays id
3 (no reload) and the sub-0 pool replaces sub 1's.

### a02_01 the switch [31] (629 frames)

North through the south tunnel (the east walkway, x 30..38), across the
turntable [28] (ground 0x7AEC10 from z ~-40), east along the rails to (140.5,
-19.5); Use at f482 facing -z: D_00810761 = 1 and D_008107E1 = 1 (f485), 3B8D
= 3 then 2, script 0x826780; at f609 D_008107E1 = 3, the player at (121, 0.1,
12) facing -x, control.

### a02_02 the ladder, while the car runs (1015 frames)

The car [32] starts at x -284.4 (row 0) and runs east at about 1.8 units a
frame. North to (129, 44.5); Cross at f95: the ladder grab (+5 = 0x0B,
action 0x15), the climb (0x17) from f157 at y 3.1 to 33.2 by f405, the top-out
(0x18) and the ledge at (129.5, 50, 60.8) at f486. Meanwhile: D_008107E1 = 7
(f3, bit 2), 0x47 (f163, bit 6), 0x4F (f405, the car at x 140.9: bit 3; the
gate [38] frees itself at f410..f411 and its node is reused), 0xCF (f486, bit
7), and at f487 the car stops at (210.9, 0, 9.1) and frees itself:
D_008107E1 = 0xFF, D_00810761 = 0xFF. Back down: Cross at f637 facing -z at
the ladder top (action 0x16), stick down, at the foot (129, 0.1, 39.7) f1004.

### a02_03 over the wreck (1021 frames)

East along the rails; the walk stops at (193.4, 0.06, 4.9) against the stopped
car and [35]. Cross at f241 facing +z: a ledge climb (action 8, clips 112 ..
140) onto [35] (ground 0x7B00A0) at (199.1, 21.4, 7.7) at f335. East along
its top (y 21..23) to x ~260; a fall (action 0x0B, f449) and the landing
(0x0F) at (271.5, 0.05, 21.9) at f474. At the platform's south edge (x ~290,
z ~25..30) two creatures grab the player (action 0x3E at f659 and f851;
infection 40 -> 50 -> 60; the grabs pushed the player back to z 24.5 and
26.2, and the Cross presses at f752 and f794 were not taken); Cross at f929
facing +z: the ledge climb onto the platform at (289.8, 15, 37.5), f1010.

### a02_04 the battery panel [24] (1117 frames)

East and north round the crate [6] (407.3, 15, 72.7) to (427.4, 15, 94.9),
yaw -0.023; Use at f375: the player aligned to (424.2, 15, 91), yaw 0, at
f378, action 0x41 from f379, 3B8D = 2; the battery page's prompt (spend 4 units,
cursor on No) at f536; Left at f593 moves the cursor to Yes; Cross at f615:
the charge D_00810CB2 falls 8 -> 6 -> 4 -> 2 -> 0 (f619..f709), the page
closes (f739..f742), D_00810843 = 0x08 at f984, control at f1048.

### a02_05 the progression exit (1719 frames)

To (439.6, 15, 104.6) below door [25] facing +z; Cross at f171: the player
aligned to (440.2, 15, 103.9), action 0x41, 3B8D = 2 (f174), the slider
opens and the player is moved through the doorway to z 117.9 (f261), a fade,
D_00810700..703 = 04 00 00 02 at f326, overlay id 5 (AREA04) at f333,
D_00810703 = 4 at f337; the arrival at AREA04 spawn entry 0 (440, 14.9, 370)
yaw pi at f644, an arrival script (3B8D = 4, then 2, f645..f1629) with one
message (mode 02, f667..f1507), control at f1630 (62390) at (440.1, 14.9, 356.4)
with D_008107E4 = 1.

## 6. Side beats

- **a01r_s0 the event's pickup** (373 frames): from the a01r_01 end, Use at
  f185 from (-8.0, -605.6) at the 0x8291C0 pickup (-11.4, 0.2, -607.4): 3B8D
  = 3 then 1, an item page (status ui page 9) from f256, Triangle f346,
  control f353. It is ammunition: D_00810C63 2 -> 3, D_00810C74 2 -> 3 and
  D_00810CB4 60 -> 90; taken byte 0x810881 bit 2.
- **a01r_s1 door [14] locked** (759 frames): from the a01r_02 end north to
  (-20.4, -198.5); Use at f479: aligned to (-25.5, 0, -197), action 0x41, the
  locked door clip 0x44 (f488), one message line (f553..f672), control at
  f690. D_00810841[1] stays 0; the area does not change.
- **a02_s0 the bed** (1007 frames): from the AREA02 arrival to (75.5, -199.2)
  facing +z (the bed's collision stops the walk there); the first Cross (f324)
  is not taken, the second (f376) is: 3B8D = 2, the healing-items page with
  its use-item prompt for item 0x20 (cursor on No) at f536; Left f593, Cross
  f615: item 0x20 0 at f618, at f860 health 90 -> 100 and infection 40 -> 0;
  the page closes and control returns at f938 with the player at (70, 0,
  -198.2) facing -z.

## 7. Tried and not kept (exploration runs, this session)

Their snapshots were scratch (`build/s88_next/`) and were deleted at the end of
the session; none of these facts comes from a kept beat, so none of them can
be re-checked from kept data. Treat them as leads for a future capture, not
as measurements.

- **The car.** Standing on the rails where script 0x826780 leaves the player
  (121, 0, 12), the car hit him at x ~111: health 90 -> 0 (game over).
- **Fighting.** Shooting a bug with R1 held and Circle (action 0x31) took 5 of
  its 15 points (+0x34) per hit; one bug took 18 rounds at 20..40 units. A
  loop that stopped to shoot every bug that came near ended with health 8 and
  infection 100 (the infected latch) after about 7,600 frames. Walking past the
  bugs, as the kept beats do, costs infection only.
- **The north room's east side.** Walking from the bridge toward door [16] past
  the drums at (15.1, -270) stopped at (7.5, -274); bugs bit the player there
  (health 90 -> 46, infection rising). The kept route goes north along x 0 first.
- **Door [5] (sub 1).** Use at (64.5, -222.6) facing -x was taken by the
  pickup 0x825B50[0] on the shelf at (62.2, 14, -216.2) instead (a page of the
  status screen that stayed open); door [5] itself was not played.
- **The first platform climb** at (176.4, 45.5) (x 180, before the gates) was
  not taken: the platform (y 15) starts at x 285.
- **A frame longer than 30 s** at [31]'s Use (a02_01) stopped the first capture
  attempt; `next_long_frames` was added to every new beat after it.
- **Capture retries.** a01r_00 once stopped on its own position check (it ends
  9.6 units past its last waypoint, still outside the quad; the check was
  widened), a01r_01 once on a tool bug after the whole event (an unaligned
  Pine read), a02_03 once when a bug's grab knocked the climb off its line
  (the climb now repeats its push), a02_05 twice when a grab took the Cross
  press (it now waits for the door program, 3B8D != 0 or action 0x41, and
  approaches squarely). The failed attempts wrote only `_failed` traces,
  which were deleted.

## 8. Open

1. The revisit event's cinematic (script 0x82AD90, about 6,900 frames of
   camera path) and its message lines were not decoded record by record.
2. Which instruction selects sub 0 during the AREA02 duct crawl (a02_00), and
   what the scripts 0x826B40 / 0x826F40 of [31] (0x824D50's turntable tests)
   do: not reached on the route.
3. Door [14] and AREA02 sub 0's door [22] (the padlock-like 001582E0 [21]
   beside it): which code sets D_00810841[1] bit 1 was not found; AREA02 entry
   0 was not played.
4. Not visited: sub 1's south room ([2] 00159970, two pickups), the save
   terminal [3] and console [4], door [5] back to AREA01 (entry 5), the ledge
   at y 50 beyond the ladder top, the turntable [28] and [27], the western
   tunnel (x < -80) with its nest of 00128C10 / 0012A5D0 creatures, the
   examine owner [40] (0x824FA0), AREA02 sub 2.
5. What sets D_008107E4 in AREA04's arrival.

## 9. Census: what the revisit and AREA02 execute beyond the first level, AREA01 and AREA00

Lane NEXT replayed every a01r and a02 beat with the census tool's one-shot
breakpoints (hidden PCSX2, one session per beat, nothing written to a save
slot) and computed the delta.

**Tool** (`../Extermination/tools/route_census.py`, additive; the default
segments, `report`, `exit-delta`, `a01-delta` and `a00-delta` are unchanged):

- `run --segments a01r --pass A01R` replays each revisit beat from its
  recorded source snapshot with route_capture's own closed-loop beat
  function, the breakpoints on every boot function and every AREA01 splat
  piece (runtime = splat label + 0x40); `run --segments a02 --pass A02` does
  the same for AREA02 with the AREA02 pieces.
- `a02-delta --passes A02 --a01r-passes A01R --a00-passes A00 --a01-passes
  A01` folds the pieces into real functions (`tools/overlay/overlay_match.py`;
  AREA01 41, AREA02 17) and lists what the revisit and AREA02 run that
  **neither the first level nor AREA01 (first visit) nor AREA00 ran**.
  "Already ran" = the first-level census (`route_functions.json`), beat 15,
  every AREA01 beat (all phases: a01_07's exit phase is AREA00's arrival) and
  every AREA00 beat (all phases: a00_10's exit phase is the revisit's
  arrival); an AREA01 overlay function counts as already run when the first
  visit ran it (overlay id 2) or when a00_10's exit phase paused inside it
  (mapped by range). Groups, a function counting in the first that ran it:
  - **a01r**: the revisit's AREA01 play (a01r_00..a01r_03, a01r_03 up to its
    area-change consumer 001AD010, census f565);
  - **a01r_side**: a01r_s0, a01r_s1;
  - **a02_arrival**: a01r_03 from 001AD010 on (the AREA02 load and the sub-1
    arrival). That replay armed AREA01's pieces, so AREA02 code shows only
    where an AREA01 candidate address falls inside an AREA02 function
    (overlay id 3 resident), mapped by range. The breakpoints are one-shot:
    13 AREA01 candidate addresses hit in that replay, 12 of them before the
    area change (f565), so only the remaining ones could still catch AREA02
    code after it (one did: 0x823900 at f733). The group's AREA02 count (1)
    is therefore a **lower bound**. The totals are not affected for code
    that runs after the arrival: a02_00 starts from the same sub-1 state
    with every AREA02 address armed and its first AREA02 hit is at f1270
    (0x823900, then the sub-0 load from f1271), so no AREA02 function runs in
    the sub-1 frames before that; an AREA02 function that ran only during
    the load and arrival frames of a01r_03 could still be missed;
  - **a02**: a02_00..a02_05, a02_05 up to its area change (census f329);
  - **a02_side**: a02_s0;
  - **a02_exit**: a02_05 from its area change on (the AREA04 load and
    arrival; its overlay-range hits are AREA04 code and are listed apart).
  Output: `../Extermination/build/s87/census/a02_delta.json`
  (`new_functions`, `functions`, `per_beat`, `overlay_AREA02_not_run`).

**Replay fidelity.** All 13 replays completed their beat's own checks (no
failed attempt). Row for row: a01r_00 1382/1382, a01r_03 938/938, a01r_s0
374/374, a02_03 1022/1022 and a02_04 1118/1118 identical; a01r_01 7770/8122
(first difference at row 740), a02_00 1102/1413, a02_01 414/630, a02_02
641/1016; a01r_02 and a02_05 started one and six counters off (0 identical
rows; a02_05 took 1722 frames against 1719), a01r_s1 two counters off,
a02_s0 102/1008 (1008 frames against 1007; a02_01 took 630 against 629). The player's end state equals the recorded snapshot's in every
beat except a01r_s1 and a02_s0. As for AREA01 and AREA00, the function sets
are those of the same route shape, not of the exact recorded frames.

**Totals.** The revisit and AREA02 (13 beats) run **1430 functions**: 1386
boot, 29 of AREA01's 41 overlay functions and 15 of AREA02's 17. Already ran:
1102 in the first level, 55 in beat 15, 83 in AREA01 play, 75 in AREA00 play
and 4 AREA01 overlay functions in a00_10's exit phase. **111 are new (57,804
bytes): 79 boot, 17 AREA01 overlay, 15 AREA02 overlay.**

| Group | New | Bytes | Boot | AREA01 ovl | AREA02 ovl | Decomp status |
|---|---:|---:|---:|---:|---:|---|
| a01r (revisit play) | 26 | 10,724 | 9 | 17 | 0 | OC 17, NM 5, BM 3, AW 1 |
| a01r_side | 9 | 10,768 | 9 | 0 | 0 | BM 7, NM 1, AI 1 |
| a02_arrival | 9 | 2,460 | 8 | 0 | 1 | BM 6, NM 1, AW 1, OC 1 |
| a02 (main line) | 42 | 18,680 | 28 | 0 | 14 | BM 14, OC 14, NM 9, AI 3, AW 1, CL 1 |
| a02_side | 19 | 9,848 | 19 | 0 | 0 | BM 13, NM 5, AI 1 |
| a02_exit (AREA04 arrival) | 6 | 5,324 | 6 | 0 | 0 | BM 4, NM 1, AW 1 |

Status abbreviations as in SECOND_LEVEL_ROUTE.md section 6 (BM byte-matched
C, NM NEARMISS, AI inline asm, AW word asm, CL C linked from asm), plus OC
overlay C; the statuses are the census's classification of the decomp's
working tree at the run (the AREA02 overlay C is lane A02C's uncommitted
work). By status: BM 47, OC 32, NM 22, AI 5, AW 4, CL 1.

**New functions by the beat that first runs them** (census frames; boot
unless marked):

- **a01r_00** (5, 3,384): AW 0011C128; NM 0011E520 (both f379); AREA01
  0x825950, 0x825BE0, 0x8282F0 (from f1: the condition-3 group's owner and
  its per-frame callees run in every revisit frame).
- **a01r_01** (20, 7,252): BM 001B6AE0 (f1074), 001C8140; NM 001C7EB0,
  001EDAF0, 001F4010, 001F9660; AREA01 0x8237D0, 0x823900, 0x8239C0 (f1139,
  the records of group 0x82A900 that script 0x82AD90's op14 spawns), 0x824340,
  0x824770, 0x824D50, 0x824F70, 0x824FE0, 0x825040 (the 0x823CD0 family),
  0x825910 (the op09 callback of script 0x82AC10), 0x825D30, 0x825EA0,
  0x825F00, 0x825FC0 (the event's stages).
- **a01r_02** (1, 88): BM 001FC520 (f1012, at the end of [42]'s script).
- **a01r_s0** (9, 10,768): BM 00211970, 0020AC70, 0020BF20, 002121A0,
  002125B0, 00212B60, 00212F30; NM 001D66A0; AI 00208AB0 (f286..f287, the
  pickup's item page).
- **a02 arrival** (a01r_03 from f733, 9, 2,460): AREA02 0x823900 (the area
  init); BM 00159620 (the bed), 00157B30, 001CAE40, 001CAF60, 001D3A30,
  001D3AC0; NM 00159970; AW 001C4AF0.
- **a02_00** (14, 6,952): BM 001D3C40, 001D3CE0 (f59); from f1271, the
  sub-0 load: NM 001582E0, 001B18F0; BM 00158EC0 (the battery panel),
  001C4CB0; AW 001C4960; AREA02 0x823930, 0x823980, 0x823D70, 0x824020,
  0x824800, 0x824FA0, 0x825100.
- **a02_01** (10, 3,644): AREA02 0x824D50 (f486, [31]'s Use), 0x8242F0
  (f611, the car), 0x824910, 0x824AC0, 0x824C40, 0x824CD0 (its callees);
  AI 001F4E40, 001AA640; BM 001AA700; NM 001EC9A0.
- **a02_02** (15, 7,248): NM 001EBD20, 001E4A00, 001E4CE0, 00199FA0,
  001A9360; BM 00176DC0 (f99, the ladder), 00183C40, 001A8970, 001F6B30,
  001E49F0, 00180530, 0017FD40; AI 001A8E80; CL 0021BD10; AREA02 0x823580
  (f405, the frame the car passes x 140).
- **a02_03** (2, 736): NM 001EC5F0; BM 001ECEF0.
- **a02_04** (1, 100): BM 00158050 (f951).
- **a02_s0** (19, 9,848): the bed's use-item page and the cure (f533..f865):
  BM 00157F30, 0015C700, 0015C750, 0015C7C0, 001C47E0, 001CB580, 00209DF0,
  0020E1E0, 0020E250, 0020E3A0, 0020E6F0, 0020EC80, 00215FE0; NM 001575E0,
  001B62C0, 00208750, 00209860, 0020E460; AI 001CB4F0.
- **a02_05 exit phase** (6, 5,324): BM 00159E70, 001BC960, 001BDFC0 (f648,
  the AREA04 arrival), 001FB0B0 (f1633); NM 001BD560; AW 001C48C0.

The first-hit frame says when a function first ran in the replay, not which
caller ran it; only the rows marked with an owner or a script were read from
code.

**The AREA02 overlay.** 15 of its 17 functions ran. The two that did not:
0x823540 (the entry pad) and 0x825520 (the player rotation about a +0xD-9
object, reached from 0x825100; the route never stood on the turning [28]).
Because of the a02_arrival lower bound above, "not run" for 0x823540 means
not seen: a run during the AREA02 load frames of a01r_03 would not have been
caught.

**Reproduce** (hidden PCSX2; the replays took 23 minutes of beat time for
a01r and 20 for a02):

```sh
# decomp repo, .venv python, repo root
.venv/bin/python tools/route_census.py run --segments a01r --pass A01R
.venv/bin/python tools/route_census.py run --segments a02 --pass A02
.venv/bin/python tools/route_census.py a02-delta --passes A02 --a01r-passes A01R \
    --a00-passes A00 --a01-passes A01
```
