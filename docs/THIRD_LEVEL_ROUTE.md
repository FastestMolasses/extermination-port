# AREA00 third-level route: original ground truth (phase 1)

Lane "capture", 2026-09-28 (session s88). Original executable SHA-256
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

AREA00 is the level the AREA01 shaft door leads to (SECOND_LEVEL_ROUTE.md,
beat a01_07: area change to AREA00 sub 0 entry 0, the arrival script, then
control). This document records AREA00 as the original plays it from that
arrival, with the same tool and conventions as SECOND_LEVEL_ROUTE.md: hidden
PCSX2, exact one-frame steps, pad input only, no teleports and no memory
writes, one trace and one resumable snapshot per beat. Nothing here is port
behaviour.

**Status: partial.** The kept beats reach the cage-room terminal, which sends
the level's ferry to the east side of the pit. AREA00's progression exit, the
shaft door after story flag D_0081075D is set, was **not** reached: the room
that sets that flag (the north-east room behind door [51]) was not entered.
Section 2.5 gives the chain the original code requires for it, and section 6
what was tried. The plain exit of the shaft door (back to AREA01 before the
flag) is recorded as side beat a00_s0.

In-game text (messages, prompts, page titles) is described, never quoted.
Positions are world units; yaw is the player's +0xC4 (X = sin, Z = cos).

## 1. Tool, outputs and conventions

The AREA00 beats are a new opt-in group `a00` of
`../Extermination/tools/route_capture.py`. `--beats all` is unchanged (beats
00..14); the AREA01 and C7 groups are unchanged.

```sh
# decomp repo, .venv python, repo root
.venv/bin/python tools/route_capture.py run --beats a00              # every AREA00 beat, in order
.venv/bin/python tools/route_capture.py run --beats a00_03,a00_s0    # some beats
.venv/bin/python tools/route_capture.py events --beats a00_04        # change log of one trace
.venv/bin/python tools/route_capture.py verify --beats a00           # every snapshot resumes
```

- **Session, resuming, navigation, Use**: as SECOND_LEVEL_ROUTE.md section 1.
  a00_00 and a00_s0 start from `build/s87/route_a01/a01_07_level_exit/state.p2s`.
- **Outputs**: `../Extermination/build/s87/route_a00/<beat>/` (ignored):
  `trace.json`, `state.p2s`, `eeMemory.bin`, `gs.bin`, `scratchpad.bin`,
  `original.png`, `snapshot.json`.
- **Row format**: the AREA01 row (SECOND_LEVEL_ROUTE.md section 1: player,
  camera, request, fade, message, `hp`, `d9`, `f759`, `story758`, `area4`,
  `slots`, `bd8`, `ovl`, `taken`, `docs`, `msgrec`) with AREA00 owners, plus
  `locks` (D_00810840..847; byte 1 is D_00810841[0], AREA00's door-lock
  bits), `wpn` (D_00810C60..C67; C62 = rounds in the weapon) and `d7dc`
  (D_008107DC, the ferry's position bits). `events` also logs `locks` and
  `d7dc`.
- **Owners sampled** (pool nodes of the AREA00 sub-0 load, measured in the
  a01_07 end snapshot; placement table 0x82BB50 record [n]):

  | Row key | Node | Callback | Placement | Role on the route |
  |---|---|---|---|---|
  | `shaft_door_r52` | 0x7B7C00 | overlay 0x823580 | [52] (-35.5, -35, -1281.8), door id 0\|0x80 | the shaft door (a00_s0) |
  | `door_r56` | 0x7B87C0 | 001BC350 | [56] (-48, -60, -1495.6), id 1 | west-room door; not used |
  | `door_r51` | 0x7B7910 | overlay 0x825170 | [51] (123, -60, -1458.3), id 3 | north-east room door: locked try (a00_01) |
  | `lock_r54` | 0x7B81E0 | 001581A0 | [54] (214.8, -52.2, -1688.6), class 0x44 | padlock of the cage door (a00_03); freed when broken, the node is then reused by short-lived nodes |
  | `door_r55` | 0x7B84D0 | 001BC350 | [55] (215.3, -60, -1681.2), model 0x15, id 2 | cage door (a00_04) |
  | `terminal_r40` | 0x7B58C0 | overlay 0x825480 | [40] (219, -34.5, -1655) | cage-room terminal (a00_04) |
  | `ferry_cab_r47` | 0x7B6D50 | overlay 0x8263C0 | [47] (50, -59, -1590) | examine object riding on the ferry |
  | `ferry_r48` | 0x7B7040 | overlay 0x825600 | [48] (50, -60, -1590), +0x0D = 2 | the ferry over the pit (a00_04) |
  | `r43_825920` | 0x7B6190 | overlay 0x825920 | [43] (149.4, -45, -1410.9), class 0x88 | the D_0081075D owner (section 2.5); not reached |
  | `beam_r60` | 0x7B9380 | overlay 0x8266A0 | [60] (0, -5, -1389) | reads D_0081075E; not reached |

## 2. The level, from the original's own data

Read from the a01_07 end snapshot (the arrival), the AREA00 overlay and the
boot data (`../Extermination/tools/area_overview.py --area 0 --ram
build/s87/route_a01/a01_07_level_exit/eeMemory.bin --out <file>`; the
output was not kept, the command regenerates it; AREA00_OVERVIEW.md now keeps
the generated tables), the overlay code (splat output; all AREA00
behaviour functions named here except 0x8247C0, 0x824E00, 0x8260B0 and
0x826CB0 are still undecompiled assembly at decomp fa4b42c, so their
descriptions below come from reading the original instructions, not from C)
and the level collision (`tools/export_collision.py extract/chunk04.n0/f06_id44.bin
-o <file> --node-class --verify-ram build/s87/route_a01/a01_07_level_exit/eeMemory.bin`:
769 grid nodes equal to the RAM; also regenerated on demand).

### 2.1 Registries

- Overlay `AREA00.BIN`, header id 1. Three sub-states: subs 0 and 1 share the
  placement table 0x82BB50 (69 records), sub 2 has 0x82C640 (41). The a01_07
  arrival loaded sub 0 (D_00810701 = 0).
- **Spawn table** (sub 0, 0x24AA50, 0x30-byte records):

  | Entry | Position | Yaw | Used by |
  |---:|---|---:|---|
  | 0 | -40, -35, -1290 | pi | the AREA01 shaft door's arrival (a01_07) |
  | 1 / 2 | -55, -60, -1492 / -36, -60, -1492 | -pi/2 / pi/2 | door [56] room move |
  | 3 / 4 | 222, -60, -1686 / 208, -60, -1686 | pi/2 / -pi/2 | door [55] room move (a00_04) |
  | 5 | 183, -60, -1438 | 0 | no door record; inside the north-east room |
  | 6 / 7 | 118, -60, -1445 / 118, -60, -1467 | 0 / pi | door [51] room move |
  | 8 | -72.5, -90, -1530 | -pi/2 | at an attribute-0x37 floor square (lower west room) |
  | 9 | 33.5, -70, -1530 | pi/2 | at an attribute-0x37 square (the pit's north dock) |
  | 10 | 186, -19.5, -1463.5 | pi | at an attribute-0x37 square (the mezzanine) |
  | 11 / 12 | -197.7, -60, -1492.2 / -173.7, -60, -1492.2 | -pi/2 / pi/2 | slider [58] room move |

- **Doors** (destination table D_0024E140[0] = 0x24DF80): id 0 = record
  01 00 00 00, the shaft door [52] (area change to AREA01 entry 0; sub from
  D_00810730[1] because the record has no sub byte, B6 = 0xFF); id 1 = 01 02
  ([56]); id 2 = 03 04 ([55], model 0x15, lock-gated on D_00810841[0] bit 2);
  id 3 = 07 06 ([51], overlay door); id 4 = 0C 0B ([58], slider model 0x16,
  lock-gated on bit 4). Sub 0 has **no other area change**: the only other
  001B0C60 caller in the overlay is the sub-2 owner 0x826790 (record [28] of
  table 0x82C640), whose script 0x82D070 ends in 001B0C60(0x0E, 0, 0), a move
  to AREA14; what selects sub 2 was not found.

### 2.2 Story bytes

A scan of the AREA00 overlay's 0x40-byte script records for flag and counter
stores (op06 sub 0/1/3, op07 sub 5), plus the code's stores by name:

| Byte | Written by | Measured |
|---|---|---|
| D_0081075A (flag 2) | 0x828E20 (= 1) and 0x8294A0 (= 0xFF): the arrival script 0x828D60 | 1 at a01_07 f536, 0xFF at f4486 (control) |
| D_008107DA (counter 2) | 0x828EE0 (= 1, arrival script); 0x8286A0 (= 2, the shaft door's script 0x8284E0, which first waits for 1) | 1 at a01_07 f610, 2 at f1886 |
| D_0081075B (flag 3) | door [51] 0x825170 by name: 1 after its locked script, 0xFF when the player's entry is 5 | 1 at a00_01 f1242 |
| D_008107DB (counter 3) | door [51] by name: 0xFF with the entry-5 opening | not reached |
| D_0081075C (flag 4) | 0x829A20: the terminal's script 0x8299E0 | 0xFF at a00_04 f792 |
| D_008107DC (counter 4) | the terminal 0x825480 by name (\|= 1 when its script ends); 0x82A060 (= 2) and 0x82A4A0 (= 0) at the ends of the ferry scripts | 1 at a00_04 f995, 2 at f1985 |
| D_0081075D (flag 5) | 0x82A6C0: script 0x82A540 of the 0x825920 owner | not reached |
| D_0081075E (flag 6) | 0x8287A0: the shaft door's script 0x8286E0 | not reached |
| D_00810841[0] (lock bits) | the padlock 001581A0 (\|= 1 << +0x2E) | 0x04 at a00_03 f105 |

With D_0081075E set, AREA01's bridge halves take the tilts [42] 0 and [41]
pi/18 in their state 0 (byte-identical C, SECOND_LEVEL_ROUTE.md section 9.3;
that this opens the north room was not measured); the scan of every overlay
found no other writer of it.

### 2.3 The shaft door [52] (overlay 0x823580): AREA00's exits

State 0 is 001BBDA0 plus, while D_0081075A != 0xFF (the arrival not done),
sub-state 5 with script 0x8284E0 (it waits for D_008107DA == 1, plays sound
0x401 and sets D_008107DA = 2). When that script ends: +0 = 1, +0xB = 0,
001C67E0, sub-state 0. Sub-state 0 then decides each Use:

- D_0081075E == 0xFF: sub-state 1 (and 0019C6F0(2, 0), 0019C6F0(0, 1)), then
  the ordinary door (001BBE40, 001BC0E0, 001BC240, 001BC290).
- else D_0081075D == 0xFF: sub-state 6 (and 0019C6F0(2, 0)); a Use (+0xB bit 2)
  starts script 0x8286E0 (ops 07/0, 10/3, 0F/1, 06/1: D_0081075E = 0xFF) and
  at its end 001B0C60(1, 0xFF, 0): AREA01, sub D_00810730[1] & 0x7F, entry 0.
  **This is the progression exit.**
- else: the ordinary door straight away (door id 0: AREA01 sub 0 entry 0).
  Measured in a00_s0.

### 2.4 The route's owners

- **Door [51]** (overlay 0x825170, room move 07 06): at state 0 it starts
  script 0x8294E0 unless D_0081075B == 0xFF. Sub-state 0: when D_00810702
  (the player's current spawn entry) is 5 it stores D_0081075B = 0xFF and,
  once its kickoff is taken, D_008107DB = 0xFF, and opens; when
  D_0081075B == 0xFF it opens as an ordinary door; otherwise a Use plays
  0x8294E0 (ops include sound 0x3F2 and one message line) and at its end,
  if D_0081075B was 0, stores 1 and calls 001C4760(4, 1).
- **Padlock [54]** (001581A0, NEARMISS C checked against the original): state
  0 frees itself when its door bit (+0x2E, here 2) is already set, else sets
  up with the aura 001F1110(self, 2). State 1: a non-zero halfword +0x36 (a
  hit) sets D_00810841[area] |= 1 << 2, plays sound 0x3F1 and frees the node.
  The player's light melee (001735C0, byte-identical C) writes +0x36 into the
  object at the player's +0x18.
- **Cage door [55]** (001BC350, model 0x15): opens only with bit 2 set.
- **Terminal [40]** (overlay 0x825480): a Use starts script 0x8299E0 (ops 07/2,
  06/1 flag 4, 04/8, 00/0, 0A/1, 02, 09 with callback 0x8253E0, 02); at its
  end D_008107DC \|= 1.
- **Ferry** (overlay 0x825600): [48] (+0x0D = 2) is the ferry, [49]
  (+0x0D = 0x15, (34.6, -60, -1628.9)) follows it through its +0x18 link, [50]
  (+0x0D = 7, (155, -85, -1590)) sits below. At state 0 [48] places itself at
  (155, -48, -1590) when D_008107DC bit 1 is set. In state 1, sub-state 0,
  whenever D_008107DC bit 0 is set it starts 0x82A0E0 (bit 1 set: back west,
  ends D_008107DC = 0) or 0x829BE0 (east, ends D_008107DC = 2). The terminal is
  the only code that sets bit 0, so each terminal Use moves the ferry once.
  [47] (overlay 0x8263C0) rides on it and plays an examine line on Use.

### 2.5 What the progression exit needs (not reached)

- D_0081075D = 0xFF comes from script 0x82A540 (ops 07/0, 10/3, 0F/1, 11/2,
  10/0, 0B/0, 07/5 slot 5). The class-0x88 member of the 0x825920 trio ([43]
  at (149.4, -45, -1410.9); [41] and [42] at x 152.5 and 150.9) starts it at
  its state 0 when D_0081075D != 0xFF (and 001B0F60(self, 7) is 0). Its code
  is byte-identical C (the decomp's
  src/overlays/AREA00/func_overlay_AREA00_008258E0.c). In state 1 the
  sub-state switch runs every frame, whatever the entry: sub 0 waits for
  +0xB bit 2; sub 1 waits for the script's end, then places the player
  (00182F90, 5 units south of itself) and spawns effect 001EFD20 at
  (-7.55, -5.69, -1391.65), next to [60]'s beam. Only the tail block depends
  on D_00810702 being 5 or 6: 001C68C0 (pose matrix), 001B17A0 (per-object
  tick), the +0x4C callback and the 001F4E20 aura.
- Why the entry still matters (reasoning over byte-identical code, not a
  measurement): +0xB = 4 is written by the player's use scan 00184BA0, which
  walks only the list that 001AAD00 hands over from the previous frame's
  publications (001B1DE0, called from 001B1B70 for class flag 0x80). The
  only call in 0x825920 that can publish [43] is 001B17A0, which reaches
  001B1B70 when its 001B1630 test is nonzero, and that call sits inside the
  entry-5/6 block. So [43] takes a Use only at entry 5 or 6, unless other
  code publishes it; other publishers were not searched for.
- Measured: in every AREA00 frame of the six a00 traces (entries 0 and 3),
  [43] is in state 1 sub 0 with +0xB = 0 and script 0x82A540 loaded at
  +0x1F8.
- The trio stands in the north-east room (x 100..200, z -1390..-1460). Its
  walls leave two openings: door [51] (frame x 113..124 at z -1458/-1459) and a
  chute at x 182.5..189.5 whose walls rise from y -60 to -19.5, under the
  attribute-0x37 square of spawn entry 10 on the mezzanine. Door [51] stays
  locked from the south until the player's entry is 5, and entry 5
  (183, -60, -1438) lies inside the room below the chute. So the room is
  entered from above; that the chute's duct places the player at entry 5 is a
  reading of these positions, not a measurement.
- The mezzanine (y -20, x 135..215, z -1460..-1520) was not reached. It
  adjoins a container stack (top y -10, x 178..215, z -1520..-1595), a 40-high
  box (x 195..215, z -1500..-1520) and floor strips with attribute 0x46
  (x 135..140 at y -19.9; x 210..215 at y 5..10 along z -1460..-1700). The
  ground under it (a grating at y -59.1) is fenced on every side (walls with
  attribute 0x50 up to y -30 at x 133 and z -1516.5, the pit fence at
  x 178.5, the container and box faces).

## 3. Route table

Frames `f` are trace frames (f = 0 is the first sampled frame); `c` is the
main-loop counter 0x70003B64.

| Beat (folder) | Source | Frames | Counters | Presses | What happens |
|---|---|---|---|---|---|
| `a00_00_descend` | a01_07_level_exit | 362 | 32347..32709 | — | arrival platform (y -35) → west stairs → floor (y -60) by door [56] |
| `a00_01_door51_locked` | a00_00 | 1272 | 32710..33982 | f317 | east along z -1459; door [51] from the south: locked script, **D_0081075B = 1** |
| `a00_02_south_route` | a00_01 | 600 | 33983..34583 | — | across the ferry deck, down the pit's west walkway, along the south edge to the cage door [55] |
| `a00_03_padlock` | a00_02 | 143 | 34584..34727 | f16 (not taken), f88 (Circle) | light melee at the padlock: **D_00810841[0] = 0x04** |
| `a00_04_cage_terminal` | a00_03 | 2016 | 34728..36744 | f183, f786 | cage door → entry 3 → stairs → terminal: **D_0081075C = 0xFF**, ferry east, **D_008107DC = 2** |
| `a00_s0_shaft_door_back` | a01_07_level_exit | 734 | 32347..33081 | f118 | shaft door before D_0081075D: plain door to **AREA01 sub 0 entry 0** |

Main line so far: a01_07 → a00_00 → … → a00_04 (4393 frames, counters
32347..36744). Health stays 100 on every kept beat; the weapon was not fired.

## 4. Main-line beats

### a00_00 descend (362 frames)

From the a01_07 end (-40.35, -35, -1340.62), stick only: (-45, -1380),
(-45, -1430), (-100, -1440), (-110, -1455) to the top of the stairs (a floor
with attribute 0x35, x -105.5..-55.5, z -1460..-1477, falling eastward from
y -35 to -60), down them (at (-59.8, -57.8, -1467.4) at f307) and on to the
floor. End at (-36.88, -60, -1482.98); the nearest fire stayed 17.1 units away
(f348).

### a00_01 door [51], locked (1272 frames)

East along z -1459..-1466, at least 14.2 units from the fire row (seven
001E3D90 nodes at z -1473..-1488, x -20..52; nearest (8.6, -1473.4) at f103). Use at f317 from (121.5, -1463.5)
facing +z:

| Frame (counter) | Event |
|---|---|
| f320..f322 | 3B8D = 3, then 2; action 0x41; door [51] runs script 0x8294E0 (its record pointer steps 0x829520 .. 0x8298E0 over f322..f1241) |
| f531..f1241 | one message line (0x23) |
| f1242 (33952) | control; the player stands at (118.9, -60, -1472.6), the point of the script's two op01 sub 9 records; D_0081075B = 1 |

### a00_02 south route (600 frames)

From (118.9, -1472.6): south-west onto the ferry (at its west position its deck
is the floor at y -60 around (38.2, -1527.0), f150, where the static collision
has only the dock ledge at y -69), down the pit's west walkway
(x 21.5..30) to (29, -1656), east along the pit's south edge (z -1659..-1662),
round the crates [11]/[12] by z -1667..-1670, then (185, -1685), (200, -1690)
to (209.2, -60, -1689.9). The path stays about 30 units south of the creature
nest at (195..205, -1655) (six 00128C10 class-2 nodes, one 0012A5D0 node,
the message owner [44] 0x825C80); nothing attacked.

### a00_03 padlock (143 frames)

Facing the padlock (yaw 1.30): Circle at f16 is not taken; Circle at f88:
action 0x36 (the light melee, +5 = 0x21) at f91; at f105 D_00810841[0] = 0x04
and the padlock node's state byte becomes 2; it is freed at f122. Control
throughout except the melee.

### a00_04 cage room terminal (2016 frames)

| Frame (counter) | Event |
|---|---|
| f183 | Use at door [55] from (209.5, -1681.5) facing +x: door program 0x24DE40.., action 0x41, clip 0x45 at f190 |
| f284 | fade; request B7/B8 = 03/02 |
| f348..f349 (35077) | D_00810702 = 3; control at (222, -60, -1686) |
| f421..f785 | up the cage room's stairs (attribute 0x35, x 254.5..265, rising north to y -34.4) and west on the platform to (227, -34.5, -1655.5) |
| f786 | Use at the terminal: 3B8D = 3 at f789, 2 at f790; script 0x8299E0 |
| f792 | D_0081075C = 0xFF |
| f995 | the terminal's script ends: D_008107DC = 1; the ferry starts 0x829BE0 |
| f1183..f1851 | one message line (0x29) while the camera watches the ferry |
| f1184..f1543 | the ferry [48] moves from x 50 to 155 at y -60 (about 0.29 units a frame); the cab [47] one frame later (f1185..f1544) |
| f1739..f1948 | the ferry rises from y -60 to -48; the cab one frame later (f1740..f1949) |
| f1985..f1986 (36714) | D_008107DC = 2; control |

## 5. Side beat: a00_s0 shaft door back to AREA01 (734 frames)

From the a01_07 end: walk to (-40.5, -1288) and Use facing +z at f118: the
ordinary door program 0x24DE40.. (clip 0x43 at f125), fade f199 with the
request block B5..B8 = 01 FF 00 01 (area 1, sub 0xFF = from D_00810730[1],
entry 0), D_00810700 = 1 at f263, overlay id 2 at f270, the AREA01 load, the
arrival at AREA01 spawn entry 0 (-40, -35, -1275) at f559, the walk-out
(3B8D = 4) and control at f674 (33021). This is the exit every AREA00 visit
has before D_0081075D is set; it does not advance the story.

## 6. Tried and not kept (exploratory runs, this session)

Their snapshots were scratch and were deleted at the end of the session; none
of these facts comes from a kept beat.

- **Fires.** Walking the fire row burned the player twice: 8.6 units from the
  fire at (-19.8, -60, -1483.4) and 5.6 units from (8.6, -60, -1473.4)
  (health 100 → 90). The kept beats pass at 14.2 units or more.
- **Creatures.** At (185.0, -60, -1626.0), 30 units from the nest, the player
  was hit (action 0x3B, health 90 → 80); the attacker was not sampled.
- **Weapon.** R1 held gives action 0x31, R2 held 0x32 with a first-person
  view; Circle while aiming fires (D_00810C62 counts down from 0x1E; action
  0x33 reloads at 0). Shots at the padlock from 5 to 33 units, with R1 and
  with R2, did not change its +0x36; the melee did.
- **Ferry.** A second terminal Use sends the ferry back west (D_008107DC = 0).
  With the ferry east, a slow step east from the west walkway at (26, -1528)
  lands on the dock ledge (y -70) and the attribute-0x37 square of entry 9
  (x 30..37, z -1526..-1533); Use there facing west, north or east was not
  taken (facing south the player jumps into the pit, y -85). From the pit
  floor, Use against the west wall grabs the walkway edge (action 8, hang
  0x10) and stick up pulls up.
- **Not visited:** the west room behind door [56], the slider [58] (lock bit 4;
  the panel [57] 00158810 with model 0x13 only plays a script, its bit write
  is on the model-0x2F path that needs item D_00810C87), the lower west room
  (entry 8), the ladder at x -22.6 (attribute 0x32, y 2.9..40) to [46] and the
  pickup at (-39.7, 40.1, -1723.5), the two DATA BASE pickups (00219550 at
  (-68.6, -89.9, -1562.7) and (60.3, -28, -1706.4)) and the save terminal
  (00159B90 at (-219.9, -49, -1476.4), sub 2 only).

## 7. Open

1. How the player reaches the mezzanine or otherwise enters the north-east
   room (section 2.5): the next capture should try the ducts behind the three
   attribute-0x37 squares with other facings and positions (the AREA01 duct
   took Use from inside its square facing into the duct), the ferry at its
   east position, and the west room.
2. Then: Use at [43] (script 0x82A540, D_0081075D = 0xFF), the effect at the
   beam, back to the shaft door (sub-state 6, script 0x8286E0,
   D_0081075E = 0xFF, 001B0C60(1, 0xFF, 0)), and the AREA01 revisit with the
   bridge halves at their D_0081075E tilts (SECOND_LEVEL_ROUTE.md section
   9.3).
3. No census replay of the a00 beats yet. `route_census.py` has the a00
   segment and the `a00-delta` report since lane "census" (s88); section 9
   gives the arrival's census and the command.

## 8. Reproduce

```sh
.venv/bin/python tools/route_capture.py run --beats a00        # 6 beats, a few minutes of host time
.venv/bin/python tools/route_capture.py events --beats a00
```

Every snapshot was reloaded once after its capture (the tool's check) and
resumed on the first try. The first run of a00_00 stopped when the emulator
closed its debug connection mid-beat (no error in its log); the rerun is the
kept capture. The first a00_s0 attempt stopped short of the door (its approach
limit) and was discarded.

## 9. Census: what AREA00 executes beyond the first level and AREA01

Lane "census", 2026-09-28 (s88). No emulator was run for this section: it
reads the recorded census replays.

**Tool.** `../Extermination/tools/route_census.py` gained (additively; the
first-level `report`, `exit-delta` and `a01-delta` outputs are unchanged apart
from new fields):

- `run --segments a00 --pass A00`: replays the a00 beats from their recorded
  source snapshots with the one-shot breakpoint on every boot function and on
  every AREA00 splat piece (runtime = splat label + 0x40; AREA00 overlay id 1).
- `a00-delta --passes A00 --a01-passes A01`: folds the pieces into the 34 real
  functions (`tools/overlay/overlay_match.py`), and lists the functions AREA00
  runs that **neither the first level nor AREA01 ran**. "Already ran" = the
  first-level census (`route_functions.json`, 1184 functions), beat 15 (every
  hit: the AREA11 exit and the AREA01 arrival) and every AREA01 beat in its
  AREA01 phase (a01_07 before its area-change consumer 001AD010), 1291 boot
  functions in all. Segments and groups:
  - **arrival** (main line): a01_07 from 001AD010 (census f233) on, i.e. the
    AREA00 load, the arrival script 0x828D60 and first control;
  - **main**: a00_00..a00_04;
  - **side**: a00_s0 before its own area change;
  - **exit**: a00_s0 from its area change on (the door back to AREA01, the
    AREA01 load and arrival).
  Output: `../Extermination/build/s87/census/a00_delta.json` (and
  `a00_arrival.json`, the arrival's new boot functions for
  `tools/area_overview.py`); statuses of AREA00 overlay functions there come
  from the decomp working tree at the time of the run.

**Recorded so far: the arrival only.** The six a00 beats have no census replay
(`segments_missing` lists them); the a01_07 replay supplies the arrival. That
replay armed the AREA01 overlay, not AREA00's, so AREA00 code shows only where
an AREA01 entry address falls inside an AREA00 function while overlay id 1 is
resident: 13 such hits (census f531..f1734) in 6 functions.

| Arrival (a01_07 from f233) | Functions | Boot | AREA00 overlay | Bytes |
|---|---:|---:|---:|---:|
| Executed | 284 | 278 | 6 (inside points) | |
| Already ran before AREA00 | 259 | 259 | — | |
| — of them first-level census / beat 15 / AREA01 play | 239 / 15 / 5 | | | |
| **New** (main line; side and exit groups are empty until the a00 beats are replayed) | **25** | **19** | **6** | **16,080** |

The 19 new boot functions are exactly the 19 that SECOND_LEVEL_ROUTE.md
section 6 lists under "Only in the exit's area change, load or AREA00 arrival"
without a "Beat 15" mark (the other 9 of that list ran in beat 15). Decomp
status (FUNCTIONS.csv / src markers of the working tree, the census's own
classification): NM 9 (00113478, 0012ADC0, 00156F30, 001581A0, 00158810,
00158BD0, 001E8E80, 001E9280, 0022DCD0), BM 8 (00128600, 00128640, 0012A5D0,
0012AFC0, 001576E0, 0015AB00, 001C2430, 001C6160), AW 1 (0015B030), AI 1
(001D0400). The first frame of each is in the SECOND_LEVEL_ROUTE.md table
(a01_07 census frames); 00113478 (f3468) is the one the static reach of
AREA00_OVERVIEW.md section 11 does not contain.

The 6 AREA00 overlay functions with an executed inside point: 0x824EA0
(deferred record 0x826F80[25], starts the arrival script), 0x825480 (the
terminal [40]), 0x825600 (the ferry [48]..[50]), 0x825920 (the trio
[41]..[43]), 0x8261E0 ([45]) and 0x8263C0 ([47]); all 6 are undecompiled
assembly at decomp fa4b42c (another lane's uncommitted C for them was in the
working tree when `a00-delta` ran, so the JSON calls them C). The other 28
overlay functions were not observed, which says nothing: no AREA00 entry was
armed.

**Static side.** AREA00_OVERVIEW.md (new, same generator as AREA01's) measures
the static reach against the same "already ran" set: 497 boot functions
reachable from AREA00's owners, 337 of them already ran, a **static delta of
160 functions (73,708 bytes)**, 111 of them from the subs-0/1 and nest owners;
18 of the arrival's 19 new boot functions are inside it.

**Next (needs the emulator, so a capture-lane step):**

```sh
# decomp repo, .venv python, repo root
.venv/bin/python tools/route_census.py run --segments a00 --pass A00
.venv/bin/python tools/route_census.py a00-delta --passes A00 --a01-passes A01
.venv/bin/python tools/area_overview.py --area 0 \
    --ram build/s87/route_a01/a01_07_level_exit/eeMemory.bin \
    --exit-delta build/s87/census/a00_arrival.json --arrival-name "a01_07 from 001AD010" \
    --compile-check --doc ../extermination-port/docs/AREA00_OVERVIEW.md
```

The AREA01 room beats of SECOND_LEVEL_ROUTE.md section 9 (a01_s4..s7) need the
same step (`run --segments a01_s4,a01_s5,a01_s6,a01_s7 --pass A01`, then
`a01-delta`, which now also counts the functions only those beats ran) before
the "already ran" set above includes them; `a00-delta` reads whatever AREA01
beats the A01 pass holds.
