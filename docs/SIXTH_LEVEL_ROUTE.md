# AREA22: sixth-level route, original ground truth

Lane "NEXT" (capture), 2026-09-28 (session s88). Original executable SHA-256
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

AREA04's progression exit (FIFTH_LEVEL_ROUTE.md, beat a04_05) ends in AREA22
(area byte 0x16, overlay id 0x13) sub 0 at spawn entry 0, with control at
(335.7, 155, 483.1) facing pi. This document records what the original does
from there to AREA22's progression exit, the area change into AREA01 at its
spawn entry 6. The tool, the conventions and the row format are those of
SECOND_LEVEL_ROUTE.md .. FIFTH_LEVEL_ROUTE.md: hidden PCSX2, exact one-frame
steps, pad input only, no teleports and no memory writes, one trace and one
resumable snapshot per beat. Nothing here is port behaviour.

**Status: the progression exit is reached.** Main line a22_00..a22_02: door
[7] (a room move) in the north arm, the corridor and its south arm, and door
[8], the area change to AREA01 entry 6 (the upper floor that
SECOND_LEVEL_ROUTE.md section 9.4 found unreachable from AREA01's ground)
and control there. 3 beats, 1,963 frames, counters 71146..73113. Side
beats: a22_s0 (a document pickup), a22_s1 (the ladder and the card reader
[9], refused), a22_s2 (door [10], locked) and a22_s3 (door [6], back to
AREA04 entry 1). Section 9 has the census replays and the a22 delta.

**Which exit the story takes** (section 2.3, from the code, then measured):
AREA22 has two area-change doors, [6] back to AREA04 entry 1 and [8] to
AREA01 entry 6; neither model is lock-gated. AREA22's overlay code is the
entry pad and a load-time area init (runtime 0x823580, called by boot
001E7780 at the area load; it sets four boot words, installs no owner
and starts no script); AREA22 has no script chains, and no route byte (story, lock, taken or
counter bytes) changes on any main-line beat. Door [8] is therefore the
only open forward area change out of AREA22 in this state; the only
lock-gated door, [10], is a room move inside AREA22 whose key (item 0x23)
has a known source behind AREA04's locked door [45] (section 2.3). Whether
AREA01 entry 6 leads on is not established here.

In-game text (messages, prompts, title cards, item descriptions) is
described, never quoted. Positions are world units; yaw is the player's
+0xC4 (X = sin, Z = cos).

## 1. Tool, outputs and conventions

The beats are the opt-in group `a22` of `../Extermination/tools/route_capture.py`.
`--beats all` is unchanged (beats 00..14); the AREA01, AREA00, C7, revisit,
AREA02 and AREA04 groups are unchanged.

```sh
# decomp repo, .venv python, repo root
.venv/bin/python tools/route_capture.py run --beats a22              # every AREA22 beat, in order
.venv/bin/python tools/route_capture.py run --beats a22_01,a22_s1    # some beats (each from its source)
.venv/bin/python tools/route_capture.py events --beats a22_00        # change log of one trace
.venv/bin/python tools/route_capture.py verify --beats a22           # every snapshot resumes
```

- **Session, resuming, navigation, Use**: as SECOND_LEVEL_ROUTE.md section 1.
  a22_00 and a22_s3 start from
  `build/s87/route_a04/a04_05_progression_exit/state.p2s`. Every beat calls
  `next_long_frames` (FOURTH_LEVEL_ROUTE.md section 1).
- **Bugs.** AREA22 holds 0012A5D0 and 00128C10 bug nodes (section 2.1). The
  beats walk with AREA04's `a04_go` and press with `a04_use`
  (FIFTH_LEVEL_ROUTE.md section 1): a grab (action 0x3B, a bug on the back;
  0x3E, a bite) interrupts the walk, the stick is rocked until the bug lets
  go, and the walk resumes. One grab happened on a kept beat (a22_s1 f65,
  section 5).
- **Outputs**: `../Extermination/build/s87/route_a22/<beat>/` (ignored):
  `trace.json`, `state.p2s`, `eeMemory.bin`, `gs.bin`, `scratchpad.bin`,
  `original.png`, `snapshot.json`.
- **Row format**: the AREA04 row (FIFTH_LEVEL_ROUTE.md section 1, with
  `story764`, `s830`, `e4`, `e5`, `e9`, `ea`) with the AREA22 owners (each
  with `cb` and `rot`), plus `s768` (D_00810768..6B; D_0081076A is
  00158810's model-0x2F latch), `l857` (D_00810857 = D_00810841[0x16],
  AREA22's door-lock byte), `c87` (D_00810C87, the count of item 0x23, the
  key the reader [9] tests) and `c88` (D_00810C88, item 0x24). `events` logs
  `l857`, `c87`, `c88` and `s768` besides the earlier keys.

### 1.1 Owners sampled

`A22_OWNERS`, pool nodes of the AREA22 sub-0 load, measured in the a04_05 end
snapshot; [n] is placement table 0x823A20 record n, g[n] deferred group
0x8236B0 record n.

| Row key | Node | Callback | Record | Role on the route |
|---|---|---|---|---|
| `pick_g0` | 0x7A5640 | 00219550 (class 0x84) | g[0] (132.7, 239.1, 286.6) | in the room behind door [10] (not reached) |
| `pick_g1` | 0x7A5930 | 00219550 | g[1] (348.2, 155, 206.3) | at the crate pair [2]/[5] (not taken) |
| `pick_g2` | 0x7A5C20 | 00219550 | g[2] (317.9, 155, 339.4) | the document taken in a22_s0 |
| `pick_g3` | 0x7A5F10 | 00219550 | g[3] (348, 155, 400.1) | beside crate [4] (not taken; section 7) |
| `pick_g4` | 0x7A6200 | 0015AFA0 (class 0xC7) | g[4] (99.8, 247.9, 301.4) | behind door [10] (not reached) |
| `bug_g6` | 0x7A67E0 | 00128C10 | g[6] (276.2, 153.4, 227.1) | in the corridor |
| `bug_g10` | 0x7A73A0 | 00128C10 | g[10] (138.2, 161.1, 231.5) | freed at a22_01 f545 (section 6) |
| `door6_r6` | 0x7A9FB0 | 001BC350, model 0x03 | [6] (339.7, 154.9, 510.5), id 0\|0x80 | back to AREA04 entry 1 (a22_s3) |
| `door7_r7` | 0x7AA2A0 | 001BB860, model 0x09 | [7] (335.1, 155, 361), id 1 | room move, entries 1 / 2 (a22_00) |
| `door8_r8` | 0x7AA590 | 001BB860, model 0x09 | [8] (120, 159.5, 78.5), id 2\|0x80 | the progression exit (a22_02) |
| `reader_r9` | 0x7AA880 | 00158810 (NM), model 0x12 | [9] (105.5, 254, 280), +0x2E = 3 | the card reader on the landing (a22_s1) |
| `door10_r10` | 0x7AAB70 | 001BB860, model 0x16 | [10] (115, 239, 281.5), id 3 | room move, entries 3 / 4: locked (a22_s2) |
| `lamp_r11` | 0x7AAE60 | 00158BD0 | [11] (115, 262, 280), +0x2E = 3 | above door [10] |
| `r13_158D30` | 0x7AB440 | 00158D30 | [13] (115, 262, 283) | beside door [10] |
| `r17_156F30` | 0x7AC000 | 00156F30, model 0x2B | [17] (347.7, 155, 293) | north arm |
| `r18_156F30` | 0x7AC2F0 | 00156F30, model 0x2B | [18] (324, 155, 279.1) | north arm |
| `r19_1C1A80` | 0x7AC5E0 | 001C1A80, model 0x52 | [19] (111.2, 155, 165.7) | south arm; contact on the way (section 2.4) |
| `r20_1C1A80` | 0x7AC8D0 | 001C1A80, model 0x52 | [20] (247.9, 155, 230.2) | corridor; contact on the way (section 2.4) |

## 2. The level, from the original's own data

The tables are from `../Extermination/tools/area_overview.py --area 22 --ram
<snapshot>/eeMemory.bin --out <file>` over the a04_05 end snapshot, and the
geometry from the RAM collision grid at D_0028A598 decoded as in
`tools/export_collision.py` (both outputs were scratch and are regenerated by
the same commands). Code descriptions are from the decomp's C; each boot
function's marker is given where a claim depends on it (BM byte-matched, NM
NEARMISS: body-correct, not byte-identical).

### 2.1 Registries

- Overlay `AREA22.BIN`, header id 0x13 (19), text 0x823540..0x823600 (0xC0
  bytes), two splat pieces: the 0x40-byte entry pad at 0x823540 and a
  0x20-byte area init at 0x823580 (its C,
  `../Extermination/src/overlays/AREA22/overlay_AREA22_func_00823540.c`, is
  byte-identical; `../Extermination/docs/AREA22_OVERLAY.md`). Boot 001E7780
  (the area-key dispatcher, run at an area load) zeroes D_00275C18..2C and,
  for key 0x1600 (area 22, sub 0), calls runtime 0x823580, which sets
  D_00275C28 = 0x20, D_00275C1C = 0x823E00 (the base of the overlay's .bss,
  a record base that 001E9580 and 001E9E60 index) and D_00275C24 =
  D_00275C2C = 0; with D_00275C2C = 0 001E7780's closing record loop does not
  run. The a04_05 end snapshot holds exactly those values. (area_overview
  groups both pieces into one function named after the pad and labels it
  "nop sled, no code"; that label is wrong for the second piece.) Every
  placement and deferred owner is a boot behaviour; the overlay contributes
  only this init and data. One sub-state: placement table 0x823A20 (21 records), deferred
  group 0x8236B0 (19), nest group 0x823600 (3). No script chains (0), no
  area music rows. Level data: INDEX sector 26 (`chunk26`, 7 files).
- **Placements** (behaviours): six crates 001551B0 ([0]..[5]; [2] stands on
  [5] at (348, 169 / 155, 208)), three drums 00156620 ([14]..[16] at
  (351, 442), (320, 396), (335.1, 427.5)), the doors [6], [7], [8] and
  [10], the reader [9], the lamp [11], 001C4820 [12], 00158D30 [13], two
  00156F30 [17], [18] and two 001C1A80 [19], [20]. The deferred group holds
  four 00219550 pickups g[0]..g[3], two 0015AFA0 pickups g[4], g[5] (both
  in the room behind door [10]), three 00128C10 nodes (g[6], g[8], g[10]) and ten
  0012A5D0 nodes (placed at y 161.5..186.1, over the corridor and the south
  arm).
- **Spawn table** (6 records, 0x30 bytes, at 0x24D4E0):

  | Entry | Position | Yaw | Used by |
  |---:|---|---:|---|
  | 0 | 335.7, 155, 496.2 | pi | AREA04 door [37]: the arrival (a04_05) |
  | 1 | 335.1, 155, 349.2 | pi | door [7], south side (a22_00) |
  | 2 | 335.1, 155, 373.2 | 0 | door [7], north side (not used) |
  | 3 | 115, 239, 269.5 | pi | door [10], landing side; also written as the entry byte during the ladder climb (a22_s1) |
  | 4 | 115, 239, 293.5 | 0 | door [10], room side (not reached) |
  | 5 | 120, 159.5, 91.5 | 0 | AREA01 door [18]'s arrival (not played) |

- **Doors** (destination table D_0024E140[0x16] = 0x24E130; four records
  used): id 0 = 04 01 00 00 ([6], AREA04 entry 1 sub 0); id 1 = 01 02 00 00
  ([7], room move, entries 1 / 2); id 2 = 01 06 00 00 ([8], AREA01 entry 6
  sub 0); id 3 = 03 04 00 00 ([10], room move, entries 3 / 4). Ids 4..15 are
  used by no placement. The lock byte is D_00810841[0x16] = D_00810857, bit
  = door id; it is 0x00 on every beat.
- **Geometry**: an L of corridors at y 155. The north arm, x 315..355 from
  z 510 (door [6]) south to z 200, is split at z 362.5 by the wall of door
  [7]. The corridor runs west along z 200..240 from x 355 to x 100, with
  boxes (tops at y 176) at x 280..290 and 190..209.4 in its north half and
  x 240..250 in its south half. The south arm, x 100..137.8 (a wall with
  attribute 0x51 at x 137.8), runs from z 240 south to z 120, where a ramp
  (normal y 0.976) climbs to y 159.5 over z 120..100 and a landing reaches
  door [8] at z 78.5; boxes stand at x 120.5..140.5, z 160..170 and
  x 100..120, z 118.4..131.2. At the south arm's north end a ladder
  (attribute 0x32: the wall z 239 at x 111.5..118.5, y 155 -> 239, and
  floors of that attribute at its foot, z 229..239, and its top, z 239..249)
  leads to a landing at y 239 (x 100..130, z 240..280) in front of door
  [10]; behind it the room x 95..135, z 283.5..323 (y 239) holds g[0], g[4]
  and g[5].

### 2.2 Story and progress bytes

| Byte | Written by | Measured |
|---|---|---|
| D_00810700..703 | door [7]; door [8]; door [6]; the ladder climb (entry byte only) | 16 00 00 16 at the a04_05 end; entry byte 1 at a22_00 f463; 01 00 06 16 at a22_02 f175, D_00810703 = 01 at f186; entry byte 3 at a22_s1 f935 (the climb's top-out, action 0x18; the writer was not traced); 04 00 01 16 at a22_s3 f325, D_00810703 = 04 at f336 |
| D_00810D0F (document byte 0x0F) | the pickup g[2] (00219550) | 0 -> 1 at a22_s0 f321 |
| D_0081083C | during the bug grab | 0 -> 1 at a22_s1 f64, back to 0 at f89 (the writer was not traced) |
| D_00810857 | on the reader's route, 001580C0 (BM), the op09 callback of record 0x246E20 in the reader's set-branch script 0x246C20: bit (byte +0x2E = 3) for any model but 0x2C; the other lock-byte writers and why they cannot set it here are in section 2.3 | 0x00 on every beat (the key item 0x23 is not held, so that script never runs) |

D_00810758..5F, D_00810760..67, D_008107D8..817 (so D_008107E4 = 2 from
AREA04), D_00810784..87, D_00810830..3F (apart from D_0081083C above),
D_00810768..6B, the lock bytes D_00810840..57, the taken bytes
0x810860..9F, item 0x20 (1), items 0x23 and 0x24 (0) and the weapon bytes
stay constant on every beat. Health stays 90 and infection 60 (the a04_05
values) on every beat; the infected latch (+0x234) stays 0.

### 2.3 Which exit the story takes (code)

- **No area director.** AREA22's overlay text is the entry pad and the
  load-time area init 0x823580 (section 2.1). The init only sets four boot
  words at the area load; it installs no owner, starts no script and writes
  no route byte. There is no director, trigger or script chain in the
  overlay. The init runs inside 001E7780 at the AREA22 load (a04_05's exit
  phase, before any a22 beat starts); the census did not record that call
  (section 9 says why), so its having run rests on 001E7780's C (key 0x1600)
  and on the snapshot values, not on a breakpoint hit. No AREA22 overlay
  address was hit with AREA22 resident during the a22 replays themselves.
- **Area-change doors.** Two placements carry an area-change door id (bit
  0x80): [6] (001BC350 model 0x03, id 0|0x80 -> AREA04 entry 1) and [8]
  (001BB860 model 0x09, id 2|0x80 -> AREA01 entry 6).
- **Lock gating.** 001BB860 (NM) tests D_00810841[D_00810700] against the
  bit (door id at +0x34) only for models 0x16, 0x17 and 0x3E; model 0x09
  goes straight to the open branch (001BB560 with 0). 001BC350 model 0x03
  is not lock-gated (FIFTH_LEVEL_ROUTE.md section 2.1, measured at AREA04
  door [37]; here door [6] opened with D_00810857 = 0). So both area changes
  are open in this state; measured: [8] in a22_02, [6] in a22_s3.
- **The one gated door.** [10] (001BB860 model 0x16, id 3) is a room move to
  entries 3 / 4, gated on D_00810857 bit 3. The reader [9] (00158810 model
  0x12, NM) calls 001576E0 (BM) at its Use; for models 0x12 and 0x2F that
  function tests D_00810C87 (item 0x23): set, it starts the script 0x246C20,
  whose op09 record 0x246E20 calls 001580C0 (BM), which ORs 1 << byte +0x2E
  (= 3) into D_00810841[D_00810700]; clear, it starts the script 0x246F20
  (one message). 00158810's own bit write (its state 2) is reached only for
  model 0x2F. D_00810C87 is 0 on every beat, so only the clear branch was
  seen (a22_s1).
- **Other writers of the lock byte.** The decomp C has seven functions
  that OR a bit into D_00810841[D_00810700]: 001580C0 (above), 00158810
  (state 2, model 0x2F only), 00157360 (NM, its state 2), 001581A0 (NM),
  001582E0 (NM), 00158430 and 00159E70. None has a direct call site in the
  RAM; they are reached through pointer words. In the a04_05 end snapshot
  and in every AREA22 beat's end snapshot, 001581A0, 001582E0, 00158430 and
  00159E70 have no pointer word anywhere in RAM (they do in a22_s3's, with
  AREA04 resident, in AREA04 data and nodes). 00158810's only pointers are
  the reader [9]'s node and placement record. 001580C0 is named by three
  script records (0x246E24 in the reader's script 0x246C20, and 0x247AA4
  and 0x247D24), and 00157360 by one (0x246BA4); whether any AREA22 owner
  can start the scripts holding the last three was not established. The
  measurement is what closes it for these beats: D_00810857 stayed 0x00 on
  every one.
- **Where the key item comes from (a lead, not played).** AREA04 overlay
  code at runtime 0x824490 (`func_overlay_AREA04_00824450.c`,
  byte-identical; the NPC [2]'s first sub-state, called from its behaviour
  0x824320, FIFTH_LEVEL_ROUTE.md section 1.1 and open item 4) calls
  001C47A0(0x23, 1) at the end of its script; 001C47A0 (BM) passes both
  arguments to 001C40B0 (NM), whose default case adds 1 to D_00810C64[0x23]
  = D_00810C87 and clamps it to 99. The NPC [2] stands in the room behind
  AREA04's door [45], which took its locked branch when tried (a04_s0,
  D_00810845 bit 5 clear) and was opened on no AREA04 beat
  (FIFTH_LEVEL_ROUTE.md open item 3). So the reader [9] and the
  room behind door [10] depend on an item from behind a door that is locked
  in the current state. Whether another source of item 0x23 exists was not
  searched.
- **No story byte is written in AREA22** on the main line (section 2.2).
- **Therefore** door [8], into AREA01 at entry 6 (119, 60, -336), is the
  only open forward area change out of AREA22 in this state. Entry 6 is the
  upper floor at AREA01's door [18], whose own area-change doors are [18]
  (back to AREA22 entry 5) and [19] (001BC350 model 0x03, id 6|0x80, to
  AREA06 entry 0; SECOND_LEVEL_ROUTE.md section 2). Whether the platform at
  door [18] connects to door [19] (SECOND_LEVEL_ROUTE.md section 9.4 notes a
  gap in the catwalk between z -400 and -440) was not played (section 8).
  This narrows FIFTH_LEVEL_ROUTE.md open item 5 without settling it: from
  AREA04's door [37] the route continues through AREA22 to AREA01 entry 6
  without a locked door, but whether entry 6 leads on, or is a dead end
  until item 0x23 (door [45]) or another locked door is dealt with, remains
  open.

### 2.4 What decides each step (code, then capture)

- **Door [7]** (001BB860 model 0x09, room move id 1): not lock-gated; its Use
  plays the door program 0x24D900 .. 0x24DA00, request B7..B8 = 01 02, and
  the room move to entry 1 (a22_00).
- **The corridor.** Nothing gates it; the walk goes round the drums, the
  crates and the boxes (section 2.1 geometry). The two 001C1A80 nodes (NM;
  model 0x52, animated, a sound every 300..479 frames) push the player back
  on contact. By their C, the first contact frame (contact byte +0xA still 0)
  only sets the byte and copies the player's +0xA0 vector into the node
  (00102948); on each further contact frame the player is moved through
  00183010 by -0.7 times the offset of his +0xA0 vector from the node's copy,
  and the copy is refreshed. Their contact byte +0xA was 1 while the walk
  passed about 6 units from them (a22_01 f272..f297 at [20], f510..f540 at
  [19]; a22_s1 f226..f252 at [19]). The walk went on past both.
- **Door [8]** (001BB860 model 0x09, id 2|0x80, record 01 06 00 00): not
  lock-gated; its Use plays the door program 0x24D900 .. 0x24DA00, request
  B5..B8 = 01 FF 06 01 and the area change (a22_02).
- **The ladder** (attribute 0x32): Use facing +z at its foot: grab (action
  0x15, clip 227), climb (0x17, clips 230..234), top-out (0x18, clip 240),
  the landing at y 239 (a22_s1).
- **The reader [9]** (00158810 model 0x12): its Use, with item 0x23 not
  held, takes 001576E0's clear branch: the script 0x246F20 (one message
  line) and state 1 sub-state 3 back to 0; D_00810857 stays 0 (a22_s1).
- **Door [10]** (001BB860 model 0x16, id 3, bit 3 clear): its Use takes the
  locked branch (001BB560 with 1): the program 0x24DA40 .. 0x24DB40, one
  message line, no room move (a22_s2).
- **Door [6]** (001BC350 model 0x03, id 0|0x80, record 04 01 00 00): not
  lock-gated; the door program 0x24DE40 .. 0x24DC80 (the same program as
  AREA04's door [37]), request B5..B8 = 04 FF 01 01 and the area change
  back to AREA04 entry 1 (a22_s3).

## 3. Route table

Frames `f` are trace frames; `c` is the main-loop counter 0x70003B64.

| Beat (folder) | Source | Frames | Counters | Presses | What happens |
|---|---|---|---|---|---|
| `a22_00_door7` | a04_05_progression_exit | 626 | 71146..71772 | Cross f216 (not taken), f308 | the north arm round the drums and a crate, door [7]: room move, **spawn entry 1** |
| `a22_01_corridor` | a22_00 | 662 | 71774..72436 | — | south along the north arm, west along the corridor round its boxes, south along the south arm to door [8]'s front (120.1, 159.5, 84.5) |
| `a22_02_progression_exit` | a22_01 | 675 | 72438..73113 | Cross f20 | door [8]: **AREA01 entry 6**, overlay id 2, control f586 at (119, 60, -349.1) |
| `a22_s0_pickup` | a22_00 | 441 | 71773..72214 | Cross f266, Triangle f414 | the pickup g[2]: a document page, **D_00810D0F = 1** |
| `a22_s1_ladder_reader` | a22_01 | 1435 | 72437..73872 | Cross f423, f1156 (not taken), f1208 | back north, a bug's grab (f65..f148), up the ladder to the landing, the reader [9]: refused, one message |
| `a22_s2_door10_locked` | a22_s1 | 529 | 73873..74402 | Cross f260 | door [10], bit 3 clear: locked, one message |
| `a22_s3_door6_back` | a04_05_progression_exit | 847 | 71146..71993 | Cross f88 (not taken), f180 | door [6]: **AREA04 entry 1**, overlay id 5, control f758 at (545.5, 54.9, 384.9) |

Main line a04_05 -> a22_00 -> a22_01 -> a22_02: 1,963 frames. Every snapshot
was reloaded after its capture and resumed on the first check (no tail
idle). The counter gaps between consecutive beats (one counter) are the
snapshot reload.

## 4. The main line

### a22_00 door [7] (626 frames)

From (335.7, 483.1) facing pi: south along x 341 (between the drums at
(335.1, 427.5) and (351, 442)), then x 334 (between the drum at (320, 396)
and the crate [4] at (347.5, 398.4)) to (335.0, 367.0). The first Cross
(f216) is not taken, the second (f308) is: the player aligned to (335.1,
155, 367) facing pi, 3B8D = 2 (f311), door [7]'s program 0x24D900
(f311) .. 0x24DA00 (f361), action 0x41 (f312), the request B7..B8 = 01 02
and a fade (f399), D_00810702 = 1 at f463 (c71609), the player at spawn
entry 1 (335.1, 155, 349.2) facing pi at f464, walking out to (335.1, 155,
336.1) and control at f577 (c71723).

### a22_01 the corridor (662 frames)

South along x 336 to (336, 232), south-west to (318, 213), west along
z 213 to (270, 213), north of the box at x 240..250 through (255, 224) and
(238, 224) (contact with [20] f272..f297), back to z 212 south of the box at
x 190..209, west to (125, 208), then south along the south arm through
(125, 190), (118, 176), (118, 150) (contact with [19] f510..f540), east of
the box at x 100..120, up the ramp to (122, 100) and (120, 88); the beat ends
at (120.1, 159.5, 84.5) facing 3.08. The bug g[10]'s node 0x7A73A0 enters
its state 3 at f545 and is freed at f546 (what ended it was not traced).
No grab.

### a22_02 the progression exit (675 frames)

Facing pi at (120.0, 84.5); Cross at f20, taken at f23: 3B8D = 2, door [8]'s
program 0x24D900 (f23) .. 0x24DA00 (f73), action 0x41 (f24), the request
B5..B8 = 01 FF 06 01 and a fade (f111; the player walked to (120, 159.5,
70.5)), D_00810700..703 = 01 00 06 16 at f175 (c72613), overlay id 2 (AREA01)
at f182, D_00810703 = 01 at f186, the arrival at AREA01 entry 6 (119, 60,
-336) facing pi at f471 (c72909), the walk-in (3B8D = 4 from f472) to
(119, 60, -349.1) and control at f586 (c73024). (An exploration run's
screenshot after the same arrival showed AREA01's title card, naming a
tunnel area, over the upper-floor platform.)

## 5. Side beats

- **a22_s0 the pickup g[2]** (441 frames): from entry 1 west to (325.3,
  339.4), facing -x (the approach toward (322, 339.4) stopped at x 325.3,
  where the press was taken); Cross at f266, taken at f269: 3B8D = 3 then 1,
  action 0x41, the pickup's script (0x266620 .. 0x2667A0), clip 66, request
  byte 1 = 0x4C and the document byte D_00810D0F 0 -> 1 at f321, the status
  screen's page (ui byte 1 = 3 from f324, sub-page 02 04 with a message of
  mode 04 from f352), Triangle at f414, control at f421. The pickup's node is
  freed and reused. The item counts 0x810C60..BF and the taken bytes
  0x810860..9F do not change.
- **a22_s1 the ladder and the reader [9]** (1435 frames): from the front of
  door [8] north along the south arm; at f65 on the ramp (126.8, 155.4,
  118.4) a bug grabs the player (action 0x3B, +5 = 0x0B, clips 44 and 45,
  request byte 3 = 1, D_0081083C = 1 from f64 to f88); the rocked stick
  frees him at f148 with health 90 and infection 60 unchanged. On north to
  the ladder foot (116.6, 155.0, 234.5); Cross at f423: grab at f426 (the
  player aligned to (115, 155, 234)), climb from f485 (y 158.0) to the
  top-out at f935 (y 221.0; D_00810702 = 3 that frame), control on the
  landing at (115.0, 239, 251.0) at f1064. West-north to (105.4, 239,
  275.5) facing +z; the first Cross (f1156) is not taken, the second (f1208)
  is at f1211: the player aligned to (105.2, 239, 275.0), 3B8D = 2, the
  camera mode 1 with the letterbox, the reader's script 0x246F20 .. 0x246FE0,
  one message line (mode 02, f1215..f1364), control at f1366. D_00810857
  stays 0. (The end screenshot shows a red lamp above door [10]; 00158BD0
  colours it from the bit, AREA01_EXITB.md.)
- **a22_s2 door [10] locked** (529 frames): from the reader east to (112.7,
  239, 275.5) facing +z; Cross at f260, taken at f263: the player aligned to
  (115, 239, 275.5), 3B8D = 2, door [10]'s program 0x24DA40 .. 0x24DB40,
  letterbox, one message line (mode 02, f309..f458), control at f460 in the
  same place. The area bytes stay 16 00 03 16.
- **a22_s3 door [6] back to AREA04** (847 frames): from the arrival north to
  (339.8, 505.5) facing +z; the first Cross (f88) is not taken, the second
  (f180) is at f183: the player aligned to (334.7, 155, 505.5), door [6]'s
  program 0x24DE40 .. 0x24DC80, clip 67, the request B5..B8 = 04 FF 01 01 and
  a fade (f261), D_00810700..703 = 04 00 01 16 at f325 (c71471), overlay id
  5 at f332, D_00810703 = 04 at f336, the arrival at AREA04 entry 1 (545.5,
  54.9, 398) facing pi at f643 (3B8D = 4 from f644), the walk-in to
  (545.5, 54.9, 384.9) and
  control at f758. D_008107E4 stays 2 and D_00810764 0xFF (AREA04's
  director does not restart its event).

## 6. Owners on the route

- Doors [7] and [8] run the 001BB860 open program 0x24D900 .. 0x24DA00; door
  [10] the locked program 0x24DA40 .. 0x24DB40; door [6] the 001BC350 program
  0x24DE40, 0x24DE80, 0x24DC00 .. 0x24DC80.
- The reader [9] (0x7AA880) runs its state 1; its Use moves it to sub-state
  3 (the script pointer 0x246F20 .. 0x246FE0) and back to 0 at f1366.
- The pickup g[2] (0x7A5C20) is freed at its take (a22_s0 f422) and the node
  is reused.
- The bug g[10] (0x7A73A0) is freed at a22_01 f546; afterwards the node is
  reused by short-lived nodes (in a22_s1 several times, one during the
  grab).
- The 001C1A80 nodes [19] and [20] set their contact byte while the player
  passes (section 2.4).

## 7. Tried and not kept (exploration runs, this session)

Their snapshots were scratch (`build/s88_a22/`) and are deleted at the end
of the session; none of these facts comes from a kept beat. Treat them as
leads.

- **The crate [4] and the pickup g[3].** From (347.5, 409.9) facing -z, a
  Cross press made the player climb onto the crate (action 8); two Square
  presses then broke it; a later Use freed g[3]'s node, but neither the
  document bytes nor the item counts 0x810C60..BF changed and the status
  byte and request byte 1 showed other values (ui 00 00 00 00 09 00 B2 00,
  request 00 11). What g[3] gives was not established.
- **The main line's paths** were first walked in exploration runs from the
  same snapshots; the kept beats repeat them.

## 8. Open

1. Item 0x23 (D_00810C87, the reader's key): one source is known from the
   code, AREA04's NPC [2] behind the locked door [45] (runtime 0x824490
   calls 001C47A0(0x23, 1); section 2.3), not played. Whether any other
   source exists, and what unlocks door [45], are open. The room behind
   door [10] (g[0], g[4], g[5], spawn entries 3 / 4) was not entered.
2. Who writes D_00810702 = 3 during the ladder climb (a22_s1 f935), and
   D_0081083C during the bug grab.
3. What the pickups g[1] and g[3] (beside crates) give, and whether the
   crates hold them.
4. Not played: door [7] from the south (entry 2), spawn entry 5 (AREA01's
   door [18] into AREA22), the 00156F30 nodes [17] / [18] and 001C4820 [12].
5. AREA01 from entry 6: whether the platform at door [18] joins the catwalk
   to door [19] (AREA06), the next area change, or whether the story first
   needs item 0x23 / door [45] or another locked door. Until this is played,
   FIFTH_LEVEL_ROUTE.md open item 5 is narrowed (door [8] is AREA22's only
   open forward area change) but not settled.
6. Which owners can start the scripts that hold the other 001580C0 records
   (0x247AA4, 0x247D24) and the 00157360 record (0x246BA4); with AREA22
   resident no lock-byte bit was set on any beat.

## 9. Census: what AREA22 executes beyond the first level, AREA01, AREA00, the revisit, AREA02 and AREA04

Lane NEXT replayed every a22 beat with the census tool's one-shot
breakpoints (hidden PCSX2, one session per beat, nothing written to a save
slot) and computed the delta.

**Tool** (`../Extermination/tools/route_census.py`, additive; the default
segments, `report`, `exit-delta`, `a01-delta`, `a00-delta`, `a02-delta` and
`a04-delta` are unchanged):

- `run --segments a22 --pass A22` replays each AREA22 beat from its recorded
  source snapshot with route_capture's own closed-loop beat function, the
  breakpoints on every boot function and both AREA22 splat pieces (runtime
  0x823540, the entry pad, and 0x823580, the area init; runtime = splat
  label + 0x40). The candidate list groups the two pieces into one 96-byte
  function named after the pad (`func_overlay_AREA22_00823500`, status
  asm_word, because the pad has no return); the init itself is byte-identical
  C (section 2.1).
- `a22-delta --passes A22 --a04-passes A04 --a02-passes A02 --a01r-passes
  A01R --a00-passes A00 --a01-passes A01` lists what AREA22 runs that **neither
  the first level, nor AREA01 (first visit), nor AREA00, nor the revisit, nor
  AREA02, nor AREA04 ran**. "Already ran" = the first-level census
  (`route_functions.json`), beat 15, and every beat of the A01, A00, A01R,
  A02 and A04 passes in all phases (a04_05's exit phase is AREA22's load and
  arrival); an AREA22 overlay function counts as already run when a04_05's
  exit phase paused inside it with overlay id 0x13 resident. Groups, a
  function counting in the first that ran it:
  - **a22**: the main line (a22_00..a22_02, a22_02 up to its area-change
    consumer 001AD010, census f175);
  - **a22_side**: a22_s0..a22_s3 (a22_s3 up to its 001AD010, census f325);
  - **a22_exit**: a22_02 from 001AD010 on (the AREA01 load and the arrival at
    entry 6);
  - **a22_side_exit**: a22_s3 from 001AD010 on (the AREA04 reload at entry 1).
  Overlay-range hits with another overlay resident are that area's code at
  AREA22 candidate addresses and are listed apart: two, both at 0x823580,
  a22_02 f472 (overlay id 2, the census frame after 001E7780 ran for the
  AREA01 arrival at f471) and a22_s3 f645 (overlay id 5, two frames after
  001E7780 ran for the AREA04 arrival at f643). Neither is an area init:
  by its C, 001E7780 sends key 0x100 (AREA01 sub 0, the a22_02 arrival) to
  runtime 0x823A50 and key 0x400 (AREA04 sub 0, the a22_s3 arrival) to
  runtime 0x823B10. At 0x823580 AREA01 has a node
  behaviour (`overlay_AREA01_func_00823540.c`, byte-identical; its header
  names it the owner of placement [12]) and AREA04 the talk-turn helper
  called from door [45]'s behaviour 0x823700
  (`overlay_AREA04_func_00823540.c`, byte-identical).
  Output: `../Extermination/build/s87/census/a22_delta.json`
  (`new_functions`, `functions`, `per_beat`, `overlay_AREA22_not_run`,
  `overlay_hits_other_overlay`).

**Replay fidelity.** a22_00's first attempt stopped at census f398 with a
skipped frame step (71545 -> 71547, the fade of door [7]; the host's load
average was about 120 at the time); the tool's retry completed it. Every
replay then completed its beat's own checks. Row for row: a22_00 627/627,
a22_s2 530/530 and a22_s3 848/848 identical; a22_s1 1436 frames against
1435 (first difference at row 489, in the ladder climb: the recording's
climb starts at f485 and shows clip 232 from f489; rows 0..488, which hold
the bug's grab f65..f148 and its release, are identical; the census keeps no
replay rows, so which field differs first is not known; the first input
difference is the stick release at recorded f1029, replay f1030) and
a22_s0 445 against 441 (row 3); a22_01 and a22_02 started one counter off
(-1 and +1; 0 identical rows; a22_01 took 661 frames against 662). The
player's end state equals the recorded snapshot's in all 7 replays. The
globals end digest differs in all 7: in a22_00, a22_s2 and a22_s3 by the one
word 0x810D98, in the other four by 0x810750 and 0x810D98 (as in the AREA04
replays). The owners digest also differs in a22_01, a22_02, a22_s0 and
a22_s1. The replays' run times sum to 18.5 minutes for the main line and
10.2 for the side beats (plus 10..16 s of arming per beat and the failed
first attempt's 9.5 minutes). As for the earlier levels, the function sets
are those of the same route shape, not of the exact recorded frames.

**Totals.** The seven beats run **908 functions**, all boot; no AREA22
overlay address was hit with AREA22 resident during these replays
(`executed_overlay_AREA22` 0). That is expected: AREA22's only code besides
the pad is the area init, which runs at the AREA22 load, before every a22
replay's start (section 2.1). Already ran: 759 in the first level, 56 in beat 15, 32 in AREA01
play, 36 in AREA00 play, 4 in the revisit, 8 in AREA02 play and 2 in AREA04
play. **11 are new (7,220 bytes), all boot.**

| Group | New | Bytes | Boot | AREA22 ovl | Decomp status |
|---|---:|---:|---:|---:|---|
| a22 (main line) | 6 | 3,884 | 6 | 0 | BM 2, NM 1, AW 2, AI 1 |
| a22_side | 5 | 3,336 | 5 | 0 | BM 3, NM 1, AW 1 |
| a22_exit (AREA01 arrival) | 0 | 0 | 0 | 0 | — |
| a22_side_exit (AREA04 reload) | 0 | 0 | 0 | 0 | — |

Status abbreviations as in SECOND_LEVEL_ROUTE.md section 6 (BM byte-matched
C, NM NEARMISS, AW word assembly, AI inline asm); the statuses are the
census's classification of the decomp's working tree at the run.

**New functions by the beat that first runs them** (census frames; "port
grep" = how many of the port's src/ and tools/ files name the address, with
examples, not a full list; a plain grep over the working tree at the time of
writing, including other lanes' untracked files, and not evidence of a
verified translation):

| Function | Bytes | Status | Subsystem | First | Port grep (files, e.g.) |
|---|---:|---|---|---|---|
| 001028E8 | 20 | AI | lowmem | a22_00 f83 | 3: em_coll_move_original.c, em_coll_segment_walkers.c |
| 001A44B0 | 412 | AW | level_world | a22_00 f83 | 9: em_actor_collision.c, em_coll_probe_original.h |
| 001A4830 | 1,248 | AW | level_world | a22_00 f83 | 3: em_coll_move_original.c |
| 001A5C30 | 2,056 | NM | level_world | a22_00 f108 | 8: em_coll_segment_walkers.h, em_area01_sys.c |
| 00100110 | 32 | BM | lowmem | a22_00 f160 | 2: em_coll_segment_walkers.c |
| 00183010 | 116 | BM | actor_anim | a22_01 f272 (also a22_s1) | 4: em_area04_port.c |
| 0012D940 | 1,064 | BM | lowmem | a22_s1 f89 | 2: em_enemy.h, em_area01_exita.c |
| 001963A0 | 1,476 | NM | init_io | a22_s1 f427 | 3: em_camera_leftovers.c |
| 0018C850 | 204 | AW | init_io | a22_s1 f428 | 0 |
| 0018C920 | 360 | BM | init_io | a22_s1 f428 | 0 |
| 001BB310 | 232 | BM | math_vector | a22_s2 f266 | 0 |

- a22_00 f83..f160: the walk between the drums (x 341, z 435 .. 381).
- a22_01 f272: in the replay, 00183010 first ran at census f272. In the
  recording, the 001C1A80 node [20]'s contact byte is 1 from f272 to f297;
  by 001C1A80's C, 00183010 (the -0.7 push) runs only on a repeat-contact
  frame, not on the first contact frame (section 2.4). The replay started
  one counter off and shares no identical row with the recording, so census
  f272 does not map exactly onto a recorded frame.
- a22_s1 f89: the frame D_0081083C returns to 0 during the bug's grab; f427
  and f428: the ladder grab (action 0x15 from f426).
- a22_s2 f266: door [10]'s locked branch (its program starts at f263).

No boot function is new in either exit phase: the AREA01 load and arrival at
entry 6 and the AREA04 reload at entry 1 run only functions that already
ran in the earlier levels.

**The AREA22 overlay.** `overlay_AREA22_not_run` in a22_delta.json lists
one 96-byte asm_word function at 0x823540 with the pieces 0x823540 and
0x823580. That entry merges the entry pad with the area init; read it as
"not hit during the a22 replays", not "never runs". The init runs at the
AREA22 load inside 001E7780 (key 0x1600; section 2.1), which happened in
a04_05's exit phase: in the A04 pass, 001E7780 ran at census f514 of
a04_05, right after the AREA22 arrival. The A04 census could not record the
init there because its one-shot breakpoint at 0x823580 had already been
spent at a04_05 census f1 by AREA04's own code at that address (overlay id
5). The a04-delta rule that counts an AREA22 overlay function as already
run therefore found nothing to count. The init's having run rests on
001E7780's C and on the a04_05 end snapshot, which holds the values the
init writes (D_00275C1C = 0x823E00, D_00275C28 = 0x20, D_00275C18 / 20 / 24
/ 2C = 0); no breakpoint caught the call.

**Reproduce** (hidden PCSX2):

```sh
# decomp repo, .venv python, repo root
.venv/bin/python tools/route_census.py run --segments a22 --pass A22
.venv/bin/python tools/route_census.py a22-delta --passes A22 --a04-passes A04 --a02-passes A02 \
    --a01r-passes A01R --a00-passes A00 --a01-passes A01
```
