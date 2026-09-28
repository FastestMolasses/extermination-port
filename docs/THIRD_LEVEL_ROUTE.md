# AREA00 third-level route: original ground truth

Lane "capture", 2026-09-28 (session s88; the progression route and the
census replays were added in round 2 of the same session). Original
executable SHA-256
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

AREA00 is the level the AREA01 shaft door leads to (SECOND_LEVEL_ROUTE.md,
beat a01_07: area change to AREA00 sub 0 entry 0, the arrival script, then
control). This document records AREA00 as the original plays it from that
arrival, with the same tool and conventions as SECOND_LEVEL_ROUTE.md: hidden
PCSX2, exact one-frame steps, pad input only, no teleports and no memory
writes, one trace and one resumable snapshot per beat. Nothing here is port
behaviour.

**Status: the progression exit is reached.** The main line runs from the
arrival (a01_07's end) to AREA00's exit, the area change back to AREA01 with
D_0081075E = 0xFF (beats a00_00..a00_10, sections 3, 4 and 4a): the cage-room
terminal sends the ferry east and up; the ferry's cab is climbed, a running
jump reaches the container stack and the crate tops (the "mezzanine"), whose
duct square drops the player into the north-east room (spawn entry 5); the
switch [43] there sets D_0081075D; three barricade crates are broken to leave
through door [51]; and the shaft door, now in sub-state 6, plays its script
and changes the area. Section 2.5 gives the code that decides each step. The
plain exit of the shaft door (back to AREA01 before the flag) is side beat
a00_s0. Section 9 has the census of every a00 beat.

In-game text (messages, prompts, page titles) is described, never quoted.
Positions are world units; yaw is the player's +0xC4 (X = sin, Z = cos).

## 1. Tool, outputs and conventions

The AREA00 beats are the opt-in group `a00` of
`../Extermination/tools/route_capture.py` (a00_00..a00_10 main line, a00_s0
side). `--beats all` is unchanged (beats 00..14); the AREA01 and C7 groups
are unchanged.

```sh
# decomp repo, .venv python, repo root
.venv/bin/python tools/route_capture.py run --beats a00              # every AREA00 beat, in order
.venv/bin/python tools/route_capture.py run --beats a00_03,a00_s0    # some beats
.venv/bin/python tools/route_capture.py run --beats a00_05,a00_06,a00_07,a00_08,a00_09,a00_10  # the progression route
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
  | `r43_825920` | 0x7B6190 | overlay 0x825920 | [43] (149.4, -45, -1410.9), class 0x88 | the D_0081075D owner (section 2.5): the switch of a00_08 |
  | `beam_r60` | 0x7B9380 | overlay 0x8266A0 | [60] (0, -5, -1389) | reads D_0081075E; not sampled beyond its header |

  The r43 row's node is the switch of a00_08 and door_r51's is the door of
  a00_07/a00_09. The barricade crates of a00_09 are not sampled per frame;
  the beat reads their node headers (0x7AAB70, 0x7AAE60, 0x7AB150 upper;
  0x7AB440, 0x7AB730, 0x7ABA20 floor) through Pine to know when each is
  broken, and the trace's `taken` bytes show each break (section 4a).

## 2. The level, from the original's own data

Read from the a01_07 end snapshot (the arrival), the AREA00 overlay and the
boot data (`../Extermination/tools/area_overview.py --area 0 --ram
build/s87/route_a01/a01_07_level_exit/eeMemory.bin --out <file>`; the
output was not kept, the command regenerates it; AREA00_OVERVIEW.md now keeps
the generated tables), the overlay code (round 1 read the original
instructions: at decomp fa4b42c all AREA00 behaviour functions named here
except 0x8247C0, 0x824E00, 0x8260B0 and 0x826CB0 were undecompiled; since
708bf0d all 34 have C, byte-identical except the NEARMISS rows of
docs/AREA00_OVERLAY.md, and round 2 read the C)
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
| D_0081075B (flag 3) | door [51] 0x825170 by name: 1 after its locked script, 0xFF when the player's entry is 5 | 1 at a00_01 f1242; 0xFF at a00_07 f1035 (the frame D_00810702 becomes 5) |
| D_008107DB (counter 3) | door [51] by name: 0xFF with the entry-5 opening | 0xFF at a00_09 f1731 (the Use of door [51] from the north) |
| D_0081075C (flag 4) | 0x829A20: the terminal's script 0x8299E0 | 0xFF at a00_04 f792 |
| D_008107DC (counter 4) | the terminal 0x825480 by name (\|= 1 when its script ends); 0x82A060 (= 2) and 0x82A4A0 (= 0) at the ends of the ferry scripts | 1 at a00_04 f995, 2 at f1985 |
| D_0081075D (flag 5) | 0x82A6C0: script 0x82A540 of the 0x825920 owner | 0xFF at a00_08 f456 (record pointer at 0x82A6C0 from f455) |
| D_0081075E (flag 6) | 0x8287A0: the shaft door's script 0x8286E0 | 0xFF at a00_10 f675 (record pointer at 0x8287A0 from f674) |
| D_00810701 (sub-state of the area) | written while script 0x82A540 runs; which record writes it was not decoded | 0 → 1 at a00_08 f363; stays 1 through a00_09 and a00_10 until the area change (AREA01 then loads sub 0) |
| D_00810841[0] (lock bits) | the padlock 001581A0 (\|= 1 << +0x2E) | 0x04 at a00_03 f105 |

With D_0081075E set, AREA01's bridge halves take the tilts [42] 0 and [41]
pi/18 in their state 0 (byte-identical C, SECOND_LEVEL_ROUTE.md section 9.3);
the scan of every overlay found no other writer of it. **Measured** in the
a00_10 end snapshot (AREA01 sub 0 loaded with D_0081075E = 0xFF,
D_00810760 = D_00810784 = 0): [41] (node 0x7B3290, z -525) +0xC0 = 0.1745
(pi/18) and [42] (node 0x7B3580, z -315) +0xC0 = 0; in a01_s6 both were
+-1.0472. Whether the north room is now walkable was not played.

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
  **This is the progression exit**, measured in a00_10 (section 4a).
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

### 2.5 How the progression exit is reached (the code, then the capture)

Every step below was first read from the original code (byte-identical C
where the decomp has it; for NEARMISS functions the claims were checked
against the original instructions, `build/.asmnorm`) and then played in
beats a00_05..a00_10.

**The duct squares (attribute 0x37).** The Use dispatcher's surface step
0015D4C0 (NEARMISS; checked) takes attribute 0x37 under the feet (00176F90,
byte-identical: the floor node under the feet, its +0x1A) when 00177030
case 1 (NEARMISS; checked) passes: the body yaw must lie within ±pi/4 of
the direction built from the floor node's axis (+0x34, +0x3C), which it then
snaps to. It enters +5 = 0x18 (0016D130, byte-identical), action 0x2C, and
+0xD = 0, the straight duct entry, then +5 = 0x19 (0016DE40, byte-identical),
the crawl. The three squares and the facing each needs (axis from the level
collision, `tools/export_collision.py ... --node-class`):

| Square (node) | Where | Facing |
|---|---|---|
| 218 | x 30..37, z -1533..-1526, y -70 (entry 9, the pit's north dock) | yaw -pi/2 (-x, into the duct under the west walkway) |
| 219 | x -72..-65, z -1533..-1526, y -90 (entry 8, the lower west room) | yaw +pi/2 (+x) |
| 220 | x 182.5..189.5, z -1467..-1460, y -19.5 (entry 10, the crate tops) | yaw 0 (+z, into the chute) |

0016DE40 state 0 decides what the entry does in AREA00: when D_00810700 is 0
and the feet are north of z -1470 (only square 220), it skips the crawl:
position (185.8, y - 0.2, -1450), action 0x2E (+5 = 0x1A, the duct exit),
+0xD = 2, D_00810702 = 5, 001B0460(1). The exit state 0016EBA0 (NEARMISS)
then lowers the player out of the chute's north mouth, and the drop ends on
the north-east room's floor. Squares 218 and 219 are the two ends of one
physical duct (collision: a sloped duct floor from (-65, -90) to (15, -70)
along z -1526..-1533); a crawl that reaches an attribute-0x37 square in
AREA00 sets entry 8 when y <= -85 and entry 9 when y <= -65 (0016DE40 state
0x15). So the chute of square 220 is the only way into the north-east room,
and it is one way.

(0016D130 state 13 also has an AREA00 branch that places the player on square
220 at spawn entry 10 facing pi, the climb back up into a duct. Its state 10
is entered only from state 0 with +0xD == 2 outside AREA00; in AREA00 that
case goes to state 0x14, the drop. So this route does not reach entry 10;
other ways into state 10 were not searched.)

**The crate tops ("mezzanine").** The floors around square 220 are crate tops:
y -20 over x 135..215, z -1460..-1500 and x 195..215 to z -1520, with a
14-high crate (y -46) in the notch x 174.8..195, z -1480..-1494. On the ground
they are closed in: 30-high fences with attribute 0x50 at x 133, z -1516.5 and
x 178.5 (the grid walker's attribute gate, COLL_GRID_HULL.md, exempts 0x51..0x53
for some query classes and everything from 0x5A up, not 0x50), the north-east room's wall at z -1460 and the crate faces; the
west strip of the crate tops (x 135..140) and the high beam at x 210..215 have
attribute 0x46, which the ledge probe 0015DF10 (NEARMISS; checked) and its
wall test 0015DEC0 refuse. The container stack south of them is 10 higher (y
-10, x 182.1..215.5, z -1520..-1595) and 50 above the ground; the ledge probe
reaches floors at most 32 above the feet (D_002488B4; 24 for a climb,
D_002488B0, above that a grab).

**The ferry makes the step.** The terminal's second effect (a00_04) sends the
ferry [48] east and raises it to y -48 (0x825600 state 0 also places it at
(155, -48, -1590) on any later load with D_008107DC bit 1). On it ride the
step [49] (+0x0D = 0x15, top y -40) and the cab [47] (0x8263C0) whose roof is
at y -19.5. From the pit's south ledge (y -70) the deck is a 22-unit climb;
from the deck the cab's side is a grab (+5 = 9, action 0x10) and a pull-up
onto the roof; from the roof's east edge (x ~168) a running jump (Use while
running, 0015EC50, +5 = 6) crosses the 10-unit gap to the container stack
(y -10, 9.5 higher), and the crate tops are a 10-unit step down from there.
None of this uses a door, a script or a story byte besides D_008107DC.

**[43] and door [51].** At entry 5, [43] (0x825920, byte-identical) runs its
entry-5/6 block, publishes itself and takes a Use: script 0x82A540
(D_0081075D = 0xFF; D_00810701 becomes 1 during it; the screen fades out
and back in around it),
then it places the player 5 units south of itself. Door [51] (0x825170; C whose text and
jump table match the original under runtime placement, NEARMISS at 708bf0d
only for the overlay link) stores D_0081075B = 0xFF as soon as
D_00810702 is 5 and opens as an ordinary door from either side; its Use from
the north sets D_008107DB = 0xFF and moves the player to entry 7.

**The barricade.** Door [51]'s north side is blocked by eight 001551B0 boxes
(NEARMISS; model 0x50) from the sub-0/1 deferred group 0x826F80, records
[39..46], each with condition 1 on its own taken bit (0x1B..0x22): three at
y -53 on top of five at y -60. D_008106BE is 1 in the room (measured in
every north-east-room snapshot, 0 outside it; 001B0B50 sets it from
D_008106C8 bit 0 when a room is entered), and 0015DF10 returns at once when D_008106BE has bit 0 and
not bit 7, so no ledge climb or vault works in this room. Measured (a00_09): the
light melee (Circle, action 0x36) reaches the upper boxes (the first press
is followed by the taken bits of [40] and [39], the second by [41]'s; a break
wakes the boxes not on world ground, CURIOSITIES.md section 18); only the
second melee (Square, action 0x37) breaks the floor boxes.
Gunfire changed no box in the exploration runs.

**The exit.** With D_0081075D = 0xFF the shaft door [52] enters sub-state 6 at
once (a00_08 f456: its +5 becomes 6); its Use starts script 0x8286E0
(D_0081075E = 0xFF) and 001B0C60(1, 0xFF, 0): the request block B5..B8 =
01 FF 00 01, AREA01 sub 0 entry 0.

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
| `a00_05_ferry_deck` | a00_04 | 1159 | 36748..37907 | f414, f1054 | cage stairs → door [55] (entry 4, f559) → pit south ledge → ledge climb onto the raised ferry's deck (y -48) |
| `a00_06_cab_roof` | a00_05 | 1374 | 37908..39282 | f202 (not taken), f254, f1152 | step [49] (y -40) → deck walkway → grab at the cab [47] → its roof (y -19.5) |
| `a00_07_duct_to_ne_room` | a00_06 | 1201 | 39284..40485 | f189 (Cross while running), f885 | running jump onto the container stack (y -10) → crate tops (y -20) → duct square 220 → north-east room, **D_00810702 = 5, D_0081075B = 0xFF** (f1035) |
| `a00_08_switch` | a00_07 | 486 | 40486..40972 | f345 | Use at [43]: script 0x82A540, **D_00810701 = 1** (f363), **D_0081075D = 0xFF** (f456); shaft door to sub-state 6 |
| `a00_09_ne_room_out` | a00_08 | 2027 | 40973..43000 | Circle f173, f245; Square f331 (not taken), f383, f1198, f1487 (not taken), f1539; Cross f1728 | barricade broken; door [51] from the north (**D_008107DB = 0xFF**, f1731) → entry 7 (f1893) |
| `a00_10_progression_exit` | a00_09 | 1087 | 43001..44088 | f662 | back to the shaft door; script 0x8286E0: **D_0081075E = 0xFF** (f675), 001B0C60(1, 0xFF, 0), AREA01 sub 0 entry 0, control f1087 |
| `a00_s0_shaft_door_back` | a01_07_level_exit | 734 | 32347..33081 | f118 | shaft door before D_0081075D: plain door to **AREA01 sub 0 entry 0** |

Main line: a01_07 → a00_00 → … → a00_10 (11,727 frames, counters
32347..44088). Health stays 100 until a00_09, where the player is hit once in
the north-east room (100 → 90 at f520) and ends at 90; the weapon was not fired in
a kept beat. Every snapshot was reloaded after its capture and resumed on the
first check (no tail idle).

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

## 4a. The progression route (round 2)

### a00_05 ferry deck (1159 frames)

From the terminal platform east and down the cage room's stairs to door [55]'s
inside; Use at f414 from (220.3, -1686.2) facing -x: action 0x41, request
B7/B8 = 04/02 (f495), D_00810702 = 4 at f559, control at (208, -60, -1686).
West past the crates [11]/[12] and the fence end at x 127, a step down onto
the pit's south ledge (y -70) at f886, east along z -1654 until the ferry's
side stops the walk at x 130.5. Use at f1054 facing +x: a ledge climb
(action 8, +5 = 2, f1057); at f1148 the player stands on the deck at
(137.1, -48, -1654.3) with the ferry node (0x7B7040) as ground (+0x214).

### a00_06 cab roof (1374 frames)

Use facing +z at (139.7, -1640.6) (f254; the press at f202, made while
pressed against the step, was not taken): climb onto the step [49] (ground
0x7B7330, y -40.04) at f322. North off its end (landing at f457, z -1618.6),
north along the deck's west walkway to (138.2, -1558.3). Use facing +x at
f1152: action 8, then the grab (action 0x10, +5 = 9) at f1211 hanging at
(141.2, -40), stick up: action 0x11 (f1214), on the roof at f1318
(144.8, -19.5, -1558.3), ground = the cab node 0x7B6D50. The beat ends at
(149.5, -19.5, -1558.3).

### a00_07 duct to the north-east room (1201 frames)

Full stick east along z -1558.8 from x 146; Cross at f189 with x >= 160:
the running jump (+5 = 6, action 0xC) at f192 from (162.3, -19.5); the
landing (+5 = 8) at f210 on the container stack at (180.3, -10, -1558.2).
North across it, a 10-unit drop onto the crate tops (landing f453 at
(202.4, -20, -1512.9)), west along z -1471 to square 220. Use at f885 facing
+z: +5 = 0x18, action 0x2C at f888 with the player snapped to (186, -19.46,
-1463.5); fade out f970; the crawl state (+5 = 0x19, action 0x2D) for one
frame at f1034; at f1035 action 0x2E at (185.8, -19.66, -1450),
**D_00810702 = 5 and D_0081075B = 0xFF**; lowered to y -40.16 by f1097, the
hang (+5 = 7, action 0xD) at f1119, landing at f1130 on the room's floor at
(185.8, -60, -1442.7), then control.

### a00_08 the switch [43] (486 frames)

From (146.8, -1416.3) facing +z, Use at f345: 3B8D = 3 then 2 (f348, f349),
action 0x41, [43]'s +5 = 1 and script 0x82A540 (record pointer 0x82A580 at
f350 .. 0x82A6C0 at f455). D_00810701 = 1 at f363. At f456 **D_0081075D =
0xFF**, [43]'s +5 = 2, control with the player placed at (149.4, -60,
-1415.9), and the shaft door [52]'s +5 = 6. No message line plays. The
screen fades out at f351 and back in from f454; the end snapshot's
screenshot is black because that fade-in is still running (the fade block
reaches 0 at a00_09 f30, and the room is lit again in later screenshots).

### a00_09 out of the north-east room (2027 frames)

To (134.0, -1445.5), the corridor between the barricade and the fenced
alcove at x 139.5. Circle at f173 and f245 (action 0x36): taken bits of the
upper boxes [40], [39] (f192, f193) and [41] (f266). Square at f331 is not taken;
at f383 (action 0x37 from f386): floor box [44] (124.3, -1446.5) at f400. The player is hit at
f462 (action 0x3B, health 100 → 90 at f520) and shows action 0x3E from f794;
the attacker was not identified (an exploration run found a 0012A5D0 node
moving in the room). Square at f1198: the taken bits of [45] (123, -1439.5)
and [43] (118.6, -1453.9) at f1213 and f1216; Square at f1487 (not taken) and f1539:
[42] (113.1, -1446.4) at f1556. Box [46] stays. The walk into the doorway
ends at (118.0, -1453.3); Use at f1728 facing -z: door [51] +5 = 3 and
**D_008107DB = 0xFF** (f1731), request B7/B8 = 07/02 (f1829), D_00810702 = 7
at f1893, control at (118, -60, -1480.1).

During the beat the taken bits of four creature records of the same deferred
group are also set: [52] (00128C10, the nest) at f152, [53] (0012A5D0) at
f549, [57] (00128C10) at f684 and [56] (0012A5D0) at f794; what sets them was
not traced.

### a00_10 the progression exit (1087 frames)

West along z -1459..-1468 (clear of the fire row), up the west stairs, north
across the arrival platform to (-40.5, -1287.5). Use at f662 facing +z: 3B8D
= 3 then 2, action 0x41, the shaft door's +5 = 7 and script 0x8286E0 (record
pointer 0x828720 at f667, 0x828760 at f669, 0x8287A0 at f674). At f675
**D_0081075E = 0xFF** and the request block B5..B8 = 01 FF 00 01; D_00810700
= 1, D_00810701 = 0, D_00810702 = 0 at f676; overlay id 2 (AREA01) resident
at f683; the fade ends at f972 with the player at AREA01 spawn entry 0
(-40, -35, -1275), the walk-out (3B8D = 4) from f973 and control at f1087
(44088) at (-40, -35, -1261.9). No message line plays in the script. The
AREA01 bridge halves in the end snapshot are the D_0081075E tilts (section
2.2).

Host time: in the exploration run that found this exit, one frame after the
Use took about 110 s of host time (the emulator answered neither Pine nor
the DebugServer meanwhile); in the kept capture the whole beat, with its
snapshot check, took 74 s. The beat raises the frame and socket timeouts
(`a00_long_frames`), as does a00_08, where one capture attempt stopped on a
frame longer than the default 30 s.

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

Round 2 (the progression route):

- **The north-east room's pickup.** Use at (132.3, -1443.1) facing -x takes
  the deferred pickup [35] (0015AFA0, (133.6, -42.9, -1449.9), taken bit
  0x14) before any climb: a two-page DATA BASE notice to all personnel about
  an infection vaccine and a treatment bed in a supply room of an underground
  tunnel area. The kept a00_09 never presses Use there.
- **The barricade.** Gunfire (R1 aim with Circle, and R2 first person pitched
  down) changed no box; with the upper boxes gone the light melee reached no
  floor box (+0x36 stayed 0); Use against the boxes never climbed (the
  D_008106BE gate of section 2.5). Only Square (action 0x37) broke floor
  boxes. Circle, gunfire and Square near the boxes were each followed within
  about 60 frames by the hit reaction 0x3B, and the player was hit several
  times (health 100 → 90 per hit); a moving 0012A5D0 node was seen at
  (137.2, -59.6, -1425.5) and a 00128C10 node from record [60] woke (its
  +4/+5 went from 1/0 to 1/1) and climbed the walls; neither was proven to
  be the attacker.
- **The ferry deck.** Walking north along the deck's west walkway at x 139
  reaches z -1521 without stopping; at x 138.2 one run stopped at z -1558.8,
  others passed it. Use facing +x at (139.1, -1521) was not taken; at
  (138.2, -1558.8) it grabs the cab (kept in a00_06). Pressed against the step
  [49] at (139.7, -1638.4), Use was not taken 23 times in a row and was taken
  once the player had been pushed back to z -1641; the kept beat therefore
  stands at z -1641.
- **The long frame.** One frame after the shaft door's Use took about 110 s of
  host time in the exploration run (section 4a).
- **Entry 9's square.** Its node axis requires yaw -pi/2 (section 2.5); the
  round-1 attempts below that faced west were not retested, so why they were
  not taken is open.

Round 1:

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

1. The AREA01 revisit with D_0081075E set: a00_10 ends there with the bridge
   halves at their D_0081075E tilts (section 2.2); whether the north room and
   AREA02 behind it are walkable was not played.
2. D_00810701 = 1 after the switch (a00_08 f363). Sub 1 (placement table
   0x82BB50, level data `chunk04.n1`) was played: the a00_08 and a00_09 end
   images hold the `chunk04.n1` load, not the `chunk04.n0` one (their RAM
   differs from the n1 load map in 519 16-byte rows, all in the cell
   directory, and from the n0 map in 225,791; a00_07's is the reverse,
   518 / 225,790). Measured by the port's
   `export_area00_common.loaded_sub_proof` (AREA00_ASSETS.md finding 1). The
   frame of the reload inside a00_08 was not located. What selects sub 2 is
   still not found; sub 2 was not played.
3. The attacker in the north-east room and the taken bits of the creature
   records [52], [53], [56], [57] set during a00_09.
4. Not visited (round 1 list below): the west room behind door [56], the
   slider [58], the lower west room (entry 8, the other end of the duct from
   entry 9), the ladder at x -22.6 and its ledge, the DATA BASE pickups and the
   save terminal of sub 2.
5. The effect 001EFD20 that [43] spawns next to the beam [60], and [60]'s own
   reaction to D_0081075E, were not sampled.

## 8. Reproduce

```sh
.venv/bin/python tools/route_capture.py run --beats a00        # 12 beats (a00_00..a00_10, a00_s0)
.venv/bin/python tools/route_capture.py events --beats a00
.venv/bin/python tools/route_capture.py verify --beats a00
```

Every snapshot was reloaded once after its capture (the tool's check) and
resumed on the first try. The first run of a00_00 stopped when the emulator
closed its debug connection mid-beat (no error in its log); the rerun is the
kept capture. The first a00_s0 attempt stopped short of the door (its approach
limit) and was discarded. Round 2: a00_06 needed seven attempts (a wrong x
for the step, Use pressed against the step, the walkway stop twice, a bug in
the tool's stop test and a too-tight check, section 6), a00_08 two more
(a frame longer than the 30 s default before the timeouts were raised) and
a00_09 two more (a floor box out of the kick's reach, then the upper boxes
blocking the walk until they are broken first); the discarded attempts wrote
only `_failed` traces, which were deleted.

## 9. Census: what AREA00 executes beyond the first level and AREA01

Lane "census" (s88 round 1) built the tool; lane "capture" (round 2) replayed
every a00 beat with it. Hidden PCSX2, one session per beat, nothing written
to a save slot.

**Tool.** `../Extermination/tools/route_census.py`:

- `run --segments a00 --pass A00` replays each a00 beat from its recorded
  source snapshot with route_capture's own closed-loop beat function and the
  one-shot breakpoint on every boot function and every AREA00 splat piece
  (runtime = splat label + 0x40; AREA00 overlay id 1).
- `a00-delta --passes A00 --a01-passes A01` folds the pieces into the 34 real
  functions (`tools/overlay/overlay_match.py`) and lists the functions AREA00
  runs that **neither the first level nor AREA01 ran**. "Already ran" = the
  first-level census (`route_functions.json`), beat 15 (every hit) and every
  AREA01 beat in its AREA01 phase (a01_07 before its area-change consumer
  001AD010), now with the four room beats of SECOND_LEVEL_ROUTE.md section
  9.7. Groups:
  - **main**: the arrival (a01_07 from 001AD010, census f233, on) and
    a00_00..a00_10, a00_10 up to its own area change;
  - **side**: a00_s0 before its area change;
  - **exit**: from the area change on in the two beats that leave AREA00,
    a00_10 (the progression exit) and a00_s0 (the plain door back).
  A function run in several groups counts in the first of main, side, exit.
  Output: `../Extermination/build/s87/census/a00_delta.json` (and
  `a00_arrival.json`, the arrival's new boot functions).

**Tool changes in round 2** (additive): the exit group now takes both
area-change beats (`A00_CHANGE_BEATS`; the summary's `new_exit_by_beat`), and
the census session treats a pause 4 bytes before an armed address as that
address's hit. After an area change the armed AREA00 entries 0x823CF0 and
0x826790 are branch delay slots in the AREA01 code then resident; the
emulator reported those pauses at the branch (0x823CEC, 0x82678C), the
one-shot never cleared, and the first a00_10 and a00_s0 replays stopped after
50 such pauses (three attempts each). The rerun records them as hits with
`reported_pc` (overlay id 2, so `a00-delta` lists them under
`overlay_hits_other_overlay` and counts nothing for AREA00).

**Replay fidelity.** All twelve replays completed their beat's own checks
(the landing, the flag bytes, the room moves, the AREA01 arrival). Row for
row they did not all reproduce: a00_00 362/363 identical rows and a00_08
487/487 (both with the end state of the recorded snapshot); a00_01 150/1271,
a00_02 487/601, a00_04 100/2013, a00_10 596/1088, a00_s0 533/735; a00_03,
a00_05 and a00_07 started one to three counters off (0, 0 and 0 identical
rows); a00_06 2/1375; a00_09 took the closed loop in 2280 frames against 2027
(79 identical rows). The player's end state equals the recorded one in a00_00,
a00_04, a00_06, a00_07, a00_08, a00_10 and a00_s0. As for AREA01
(SECOND_LEVEL_ROUTE.md section 6), the function sets are those of the same
route shape, not of the exact recorded frames.

**Totals.** AREA00 (arrival plus the twelve beats) runs **1270 functions**:
1257 boot and 13 of the 34 AREA00 overlay functions. 1129 boot functions
already ran (first level 1041, beat 15 58, AREA01 play 30). **141 are new
(76,048 bytes): 128 boot, 13 overlay.**

| Group | New | Bytes | Boot | Overlay | Decomp status |
|---|---:|---:|---:|---:|---|
| Main line (arrival, a00_00..a00_10 before the change) | 128 | 70,712 | 115 | 13 | BM 53, NM 38, AI 10, AW 10, CL 2, AU 1, no source 1; overlay 13 |
| Side (a00_s0 before its change) | 0 | 0 | 0 | 0 | — |
| Exit (after the area change) | 13 | 5,336 | 13 | 0 | NM 6, BM 4, AI 2, CL 1 |

Status abbreviations as in SECOND_LEVEL_ROUTE.md section 6 (BM byte-matched
C, NM NEARMISS, AI inline asm, AW word asm, CL C linked from asm, AU
undecompiled); the boot statuses are the census's classification of the
working tree at the run. All 13 exit functions ran only in a00_10's exit
phase, first at census f973..f980, the first frames of the AREA01 arrival
with D_0081075E set; the plain door of a00_s0 adds nothing that the first
level, AREA01 or AREA00 play had not run.

**New functions by the beat that first runs them** (addresses; boot unless
noted):

- **Arrival** (a01_07, 25, 16,080 bytes; round 1's list, unchanged): NM
  00113478, 0012ADC0, 00156F30, 001581A0, 00158810, 00158BD0, 001E8E80,
  001E9280, 0022DCD0; BM 00128600, 00128640, 0012A5D0, 0012AFC0, 001576E0,
  0015AB00, 001C2430, 001C6160; AW 0015B030; AI 001D0400; overlay 0x824EA0,
  0x825480, 0x825600, 0x825920, 0x8261E0, 0x8263C0.
- **a00_00** (6, 2,144): BM 001EB980; overlay 0x823580, 0x825170, 0x825C80,
  0x8262D0, 0x8266A0.
- **a00_01** (1, 1,076): NM 0012B410.
- **a00_02** (15, 10,520): BM 00128830, 001288D0, 0012BE20, 0012E2C0,
  001B5360, 001C24D0, 001E2E80, 001EFEB0, 001F4A00, 001FC580; NM 0019AA80,
  001A7280, 001EEBA0; AI 001EFFD0; no source 001C2FF0.
- **a00_03** (20, 9,652): BM 001000C0, 0012C490, 0012D580, 001735C0 (the
  light melee), 00189FE0, 0018A180, 001D80E0, 001EAB50, 001F02C0, 001F1550,
  001F18C0; NM 00189EC0, 0019B2C0, 001AA840, 001ECB00, 001EEEB0, 001F15F0;
  AI 00102990, 001EFF10, 001F00A0.
- **a00_04** (8, 2,920): BM 00119080, 0012E0B0, 0012E260; NM 001181B0,
  001B8AB0; AI 001B7670; AW 0021BD60; overlay 0x8253E0.
- **a00_05** (2, 2,072): BM 0012B970; AW 001A58B0.
- **a00_06**: none (its climbs and the grab run functions AREA11 and AREA01
  already ran).
- **a00_07** (4, 1,312): BM 00198CE0, 001F4E20; NM 00198F10; AW 001639E0.
  (The duct states 0016D130 / 0016DE40 / 0016EBA0 are not new here: the
  AREA01 duct beat a01_s5 ran them.)
- **a00_08** (4, 1,776): BM 001FF030 (first at f363, the frame D_00810701
  becomes 1); NM 0019C6F0, 001B7700; overlay 0x823820.
- **a00_09** (43, 23,160): BM 00129F00, 00129FC0, 0012CAA0, 00173E60,
  001754E0, 001DF020, 001DF110, 001E7310, 001F0060, 001F2E90, 001F2F90,
  001F3E30, 0021C270, 0021D4E0, 0021F330, 0021F850; NM 0012D240, 0012DE90,
  00173DD0, 001B41F0, 001D7510, 001D7A80, 001DF180, 001EBC30, 001ECFB0,
  001ED7A0, 001EF510, 001F2BA0, 001F3340, 001F3620; AW 0012D850, 00193D90,
  001CA4D0, 001D6DD0, 001D7000, 001DEDB0; AI 001C6200, 001CA3B0, 001DEE80,
  001DEEC0; CL 0012E070, 001DF5A0; AU 001F6FB0. (The beat's content: the second melee, the box breaks,
  the hits on the player; these functions were not traced to callers.)
- **a00_10 exit phase** (13, 5,336): NM 001CD940, 001E2800, 001E2BA0,
  001EBBB0, 001F5040, 001F5F60; BM 001B0CD0, 001C63D0, 001F4F90, 001F5490;
  AI 00102870, 001C5050; CL 001F4F40.

**The AREA00 overlay.** 13 of its 34 functions ran: the 6 of the arrival,
0x823580 (the shaft door), 0x825170 (door [51]), 0x825C80, 0x8262D0 and
0x8266A0 (a00_00), 0x8253E0 (a00_04, the terminal's script callback) and
0x823820 (a00_08). At decomp HEAD 708bf0d (docs/AREA00_OVERLAY.md) ten of
them are byte-identical C; 0x823580 and 0x825170 are NEARMISS only for the
overlay link's jump-table placement, and 0x823820 is NEARMISS (94.22%).
The 21 that did not run: 0x823540 (the entry pad), 0x823C50, 0x823CF0,
0x823E10, 0x823EB0, 0x824130, 0x8241B0, 0x8247C0, 0x8247D0, 0x824BB0,
0x824E00, 0x824E40, 0x825D70, 0x825E80, 0x825FC0, 0x8260B0, 0x8260F0,
0x826790 (the sub-2 owner with the AREA14 move), 0x826BE0, 0x826CB0 and
0x826CC0. Sub 1 was played: a00_08 (after the switch; the reload frame was
not located) and a00_09 run with `chunk04.n1` loaded (section 7 item 2), so
the 21 did not run in sub 0 or in sub 1 as played. Sub 2 was not played.

**Reproduce** (hidden PCSX2; the twelve replays took 34 minutes of beat time):

```sh
# decomp repo, .venv python, repo root
.venv/bin/python tools/route_census.py run --segments a00 --pass A00
.venv/bin/python tools/route_census.py a00-delta --passes A00 --a01-passes A01
```

AREA00_OVERVIEW.md's generated Route and arrival columns still show round
1's arrival-only data; regenerating them (its section 0 command, with
`--compile-check` under the decomp build lock) was not part of this lane.
