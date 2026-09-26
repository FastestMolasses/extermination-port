# AREA01 second-level route: original ground truth

Lane "a01-route", 2026-09-25 (session s87); revised the same day after review
(lane "route-doc": census regrouped by real overlay function at decomp commit
bdd40fb, story-gate and message claims narrowed to what the captures show).
Original executable SHA-256
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

This is the second level as the original plays it: AREA01, the underground
tunnel area that the first level's exit leads into (FIRST_LEVEL_EXIT.md, beat 15),
from that arrival to its exit into AREA00. Every beat was played in the original
game (hidden PCSX2, exact one-frame steps) by `../Extermination/tools/route_capture.py`,
with pad input only: no teleports and no memory writes. Each beat has a
per-frame trace and a snapshot that later sessions can resume from. Nothing here
is port behaviour, and nothing in the port consumes it yet: the first level is
still the only goal (CLAUDE.md). The document exists so that AREA01 work, when it
starts, starts from facts.

It names addresses, values and frames only; in-game text (dialogue, page
titles) is described, never quoted. Positions are world units; yaw is the
player's +0xC4 (X = sin, Z = cos, FIRST_CONTROL.md). Trace facts are sampled
once per frame, so a change that is undone within one frame is not seen.

## 1. Tool, outputs and conventions

The AREA01 beats are an opt-in group of `route_capture.py`. `--beats all` still
runs only beats 00..14, and beat 15 stays opt-in; beats 00..15 are unchanged.

```sh
# decomp repo, .venv python, repo root
.venv/bin/python tools/route_capture.py run --beats a01              # every AREA01 beat, in order
.venv/bin/python tools/route_capture.py run --beats a01_03,a01_s1    # some beats
.venv/bin/python tools/route_capture.py events --beats a01_05        # change log of one trace
.venv/bin/python tools/route_capture.py verify --beats a01           # every snapshot resumes
.venv/bin/python tools/route_census.py run --segments a01 --pass A01 # census replay (section 6)
.venv/bin/python tools/route_census.py a01-delta --passes A01        # AREA01 beyond the first level
```

- **Session, snapshots, resuming, navigation**: exactly as FIRST_LEVEL_ROUTE.md
  section 1 (closed-loop stick toward world targets, Use = Cross, the status
  screen closes with Triangle, every snapshot reloaded once to prove it resumes).
- **Outputs**: `../Extermination/build/s87/route_a01/<beat>/` (ignored):
  `trace.json`, `state.p2s`, `eeMemory.bin`, `gs.bin`, `scratchpad.bin`,
  `original.png`, `snapshot.json`.
- **Row format**: the first-level row (FIRST_LEVEL_ROUTE.md section 1) without
  the AREA11 owners, plus: `hp` (player +0x220 health, float), `d9`
  (D_008107D9, taken from `d2` = D_008107D8..817), `f759` and `story758`
  (D_00810758..5F), `area4` (D_00810700..703: area, sub, entry, and the last
  byte), `slots` (task slots 0..2 at 0x28A750, 0x20 bytes each), `bd8`
  (D_00275BD8), `ovl` (overlay header at 0x823500: magic and id; 2 = AREA01,
  1 = AREA00), `taken` (0x810860..89F), `docs` (0x810D00..1F), `msgrec`
  (0x282210..223, the message service's record counters), and for each AREA01
  owner `h` (+0x00..0F), `pos` (+0xB0), `s1F0`, `t2DC` and `cb` (+0x10 callback).
  D_00810760 and D_00810784 are not sampled.
- **Owners sampled** (pool nodes of the AREA01 sub-0 load; the same node
  addresses and callbacks in every state captured from the beat-15 arrival up to
  the AREA00 load, except where the table says otherwise):

  | Row key | Node | Callback | Placement | What the capture shows |
  |---|---|---|---|---|
  | `npc_r36` | 0x7B0390 | overlay 0x825350, class 10 | table [36], (81, 0, -521), yaw -0.96 | the control-room NPC: both conversations run on it |
  | `r37_826CF0` | 0x7B0680 | overlay 0x826CF0, class 8 | [37] (table position 0, 0, 0) | above the NPC's console; its position moves within x 72.8..83.9, y 17.7..20.9, z -524.8..-518.8 on every beat; its header never changed |
  | `shaft_door_r12` | 0x7ABD10 | overlay 0x823580, class 5 | [12], (-35.5, -35, -1276.5), door id 0\|0x80 | the shaft door: locked try, then the exit |
  | `r13_158D30` | 0x7AC000 | 00158D30, class 8 | [13], (-40, -13, -1277) | next to the shaft door; header byte +1 toggles 0/1 as the player moves, and +0xB became 1 at a01_05 f3930, the frame after D_008107D9 became 0x81 (meaning not identified) |
  | `door_r15` | 0x7AC5E0 | 001BC350, class 5 | [15], (60.5, 0.5, -559), id 2 | the control-room room-move door |
  | `doc_g0_1` | 0x7A5930 | 00219550 | deferred g0.1 (live slot 1), (12.5, -29, -984.5) | the DATA BASE pickup (item 0x48); after the take the node is freed and reused by a 0018A6B0 node (a01_s1 f367) |
  | `n7A70B0_826D40`, `n7A7690_826D40`, `n7A7C70_826D40` | as named | overlay 0x826D40 | deferred, (-45, -3, -1140), (-45, 37, -900), (30, 17, -1020) | header byte +1 toggles 0/1 as the player moves (meaning not identified); nothing else sampled changed |
  | `r41_8261A0`, `r42_8261A0` | 0x7B1240, 0x7B1530 | overlay 0x8261A0 | [41] (0, 3, -525), [42] (0, 3, -315) | nothing sampled changed on the route |

  The overlay 0x826D40 nodes each have an 0x828850 partner node at the same
  position and a 001C5680 indicator node at the same position. The tunnel
  pickup opens a DATA BASE page (message 04/0x48); its on-screen content was
  seen only in the exploratory run (section 8), where it concerned sentry guns.
  That the 0x826D40 nodes are those guns is a guess from that page and their
  wall positions, not an observation.

## 2. The level, from the original's own data

Read from the beat-15 end snapshot (the arrival), the AREA01 overlay and the
boot data; every item below was then exercised on the route unless it says
otherwise.

- **Placement.** AREA01's placement descriptor D_0024D7C0[1] = 0x2758D0 begins
  0x82BD50, 0x82C5F0 (subs 0 and 1; AREA01_OVERVIEW.md finds only these two
  sub-states, the next word 0x825B50 is not a table). With D_00810701 = 0 the
  arrival spawned **table 0x82BD50 (54 records)**. This settles the open item
  in the decomp's FINDINGS (s45: "AREA01 sub-state -> table mapping (A vs B) is
  a flagged presumption"): sub 0 is table A, now measured.
- **Pool at the arrival.** 91 nodes: 9 deferred pickups (00219550 ×2,
  0015AFA0 ×7), three 0x826D40 + 0x828850 pairs, six 00128C10 class-2 nodes
  at x 83..134, y 60..83, z -384..-672 over the east side, one 001C02E0 node
  and one 001BFFD0 node at the same position, three deferred fire nodes, the
  table's records 2..53 except [38]..[40] (doors, fixtures, fires, the NPC,
  the 00159B90 and 001E7D20 nodes), the area-title actor 001C5930, seven
  player children 0018A6B0, two 001E2560 nodes and 001C5680 indicators.
- **Doors and exits** (table 0x82BD50; destination table D_0024E140[1] =
  0x24DFA0, one 4-byte record per door id):

  | Record | Callback | Position | Door id | Destination record | Route |
  |---|---|---|---|---|---|
  | [12] | overlay 0x823580 | (-35.5, -35, -1276.5) | 0\|0x80 | 00 00 00 00 | locked, then the **level exit** (beats a01_03, a01_07) |
  | [14] | 001BC350 | (-20.5, 0, -192) | 1\|0x80 | 02 00 00 00 | north room beyond the bridges; not reached |
  | [15] | 001BC350 | (60.5, 0.5, -559) | 2 | 01 02 00 00 (room move) | control room, both ways (a01_04, a01_06, a01_s0) |
  | [16] | 001BC350 | (50, 0, -220.5) | 3\|0x80 | 02 01 01 01 | north room; not reached |
  | [17] | 001BB860 | (128.6, 0, -610) | 4 | 09 08 00 00 (room move) | east corridor; not tried |
  | [18] | 001BB860 | (120, 60, -318.8) | 5\|0x80 | 16 05 00 00 | upper floor; not reached |
  | [19] | 001BC350 | (-109.5, 60, -674.5) | 6\|0x80 | 06 00 00 00 | upper floor; not reached |

- **Spawn table** (sub 0, 0x24B1A0, 0x30-byte records): entry 4 (41, 0, -565.6)
  yaw -1.43411 is the arrival; the room-move door uses entry 1 (64, 0, -563) yaw
  pi/2 inside the control room and entry 2 (50, 0, -563) yaw -pi/2 outside.
- **The story gate is one byte, D_008107D9.** It is D_008107D8[1], one slot of
  a generic script-counter array (D_008107D8[0] = 0x81 throughout, left by the
  first level's fan exit; during AREA00's arrival script, slot 2, D_008107DA,
  went 0 → 1 → 2 at a01_07 f610/f1886, after the op06 handler 001BA080 first
  ran at census f532).
  - **Observed writes on the route**: exactly two changes, 0 → 0x80 at a01_03
    f296 (counter 18537) and 0x80 → 0x81 at a01_05 f3929 (counter 25122). No
    other frame of any AREA01 beat changes it.
  - **0x80 at a01_03 f296** comes with the shaft door's switch from the
    locked-door script 0x829860 to script 0x8298E0 in the same frame. The shaft
    door (0x823580, splat `overlay_AREA01_func_00823540`, still assembly at
    bdd40fb) is one of two AREA01 overlay functions whose code stores to the
    byte by name (a symbol search of the overlay; 0x825350 only reads it). On
    Use with D_008107D9 == 0x81 it runs the ordinary door kickoff and opens;
    with any other value it plays the locked-door program and then, when the
    byte is 0 or 0x80, stores 0x80 and starts script 0x8298E0. Open lead: its
    last block reads the byte again and, when it is 0x81, sets byte +0xB of
    another node it was given to 1. That fits r13's +0xB
    becoming 1 at a01_05 f3930, but that node being r13 is not proven.
  - **0x81 at a01_05 f3929** comes with the end of the NPC's script 0x829FA0
    (its last record, 0x82A620, in the same frame) and D_00810759 = 0xFF in the
    same frame. The other overlay writer is the NPC's 0x80 branch 0x825590
    (splat `func_overlay_AREA01_00825550`, byte-identical C): when its script
    ends it stores D_00810759 = 0xFF, calls 001C4760(2, 1) and stores
    D_008107D9 = 0x81. The NPC owner 0x825350 (`func_overlay_AREA01_00825310`)
    only reads the byte: state 0 sends the NPC to its teardown state 3 when
    D_0081075A != 0 (0 in every AREA01 frame); state 1 dispatches on D_008107D9:
    0 → 0x8254B0 (Use starts script 0x829E60), 0x80 → 0x825590 (Use starts
    script 0x829FA0), 0x81 → 0x825670 (Use starts script 0x82A660; not played).
  - **Other writers exist and were not excluded.** Boot code writes the array
    by index from script data: the op06 handler 001BA080 (sub 3 stores
    rec+0x18, sub 5 increments, sub 6 decrements D_008107D8[rec+0x14]) and the
    op07 handler 001B82D0 (sub 6 stores D_008107D8[ev+0x14] = ev+0x18). Any
    script record or door event with slot 1, in AREA01's data or any other
    area's, can change the byte. What the captures support: none of the script
    records the route executed (the door programs 0x24DC00..0x24DF40, the take
    program 0x266620..0x2667A0 and the AREA01 scripts 0x829860, 0x8298E0,
    0x829E60, 0x829FA0) is an op06 or op07/6 record, and the census never saw
    001BA080 run in AREA01 (its first hit is a01_07 census f532, in AREA00).
    A symbol search of the boot disassembly finds these two writers and two
    readers (001BDFC0, 001C4A00); splat can miss a hoisted %hi, and block
    copies or clears of the save area (new game, load) were not examined. No
    write watchpoint was run, so "only these two writers" is not claimed.
  - The main path is therefore: locked shaft door → NPC → shaft door opens.
    The first NPC conversation (D_008107D9 == 0) is optional; it changes no
    sampled progress byte (beat a01_s0).
- **The two 0x8261A0 owners** at z -525 and -315 (the bridges over the gap
  north of the arrival room, per FINDINGS; not observed moving) dispatch on
  their +0xD: [41] (2) to 0x826200 and [42] (3) to 0x826440. Both read the
  bytes D_0081075E, D_00810760 and D_00810784 and the byte D_008107E0, and
  0x826440 writes the last one. In every AREA01 frame D_0081075E and
  D_008107E0 stayed 0; the other two are not sampled. Neither owner's sampled
  state changed, and the north room (doors [14], [16]) was not reached in this
  visit.
- **A legacy label corrected.** The decomp FINDINGS (s69 examine table, s74
  drawbridge decode) and the retired drawbridge export called placement [36]
  "the drawbridge crank". The capture shows a class-10 character standing at
  the console who holds two conversations (the a01_s0 end image); nothing on
  the route lowers a bridge.
- **Scripts on the route** (0x40-byte records read from the overlay data,
  op/sub as in AREA_SCRIPT.md; parentheses: the operand at +0x14 of op0A/op0C,
  the callback of op09):
  - 0x829E60 (NPC, first talk): 07/8, 15, 18, 04/1, 07/4.
  - 0x829860 (shaft door, locked): 0D/3, 07/4, preceded by the boot-data door
    program records 0x24DEC0, 0x24DF00, 0x24DF40, then 0x24DCC0..0x24DE00 (the
    locked subtype, clip 0x46).
  - 0x8298E0 (after the locked try): 07/8, 00/0, 0A/0 (0x155), 05/8, 02, 12/0,
    0A/0 (0x15E), 0A/3, 0C/1 (0x40), 00/1, 02, 0A/0 (0x15F), 0A/3, 0A/0, 0C/2,
    18, 00/0, 0A/0, 07/4.
  - 0x829FA0 (NPC, second talk): 07/8, 00/0, 09 (0x825130), 0B/0, 0A/0
    (0x164), 0A/3, 0A/0 (0x167), 0C/1 (0xA), 02, 0A/0 (0x166), 02, 00/5, 02,
    00/0, 02, 00/0, 09 (0x825240), 02, 09 (0x825130), 0B/0, 0C/2, 09
    (0x825240), 0A/0 (0x165), 0A/3, 18, 04/1, 07/4.
  - The open door uses the ordinary door program records from 0x24DE40
    (0x24DE40, 0x24DE80, then 0x24DC00..0x24DC80; clip 0x45 from the tunnel
    side and at the shaft door, 0x43 from inside the control room).

## 3. Route table

Frames `f` are trace frames (f = 0 is the first sampled frame); `c` is the
main-loop counter 0x70003B64. "Presses" lists the frames of Use/Triangle
presses; a press the original did not take is retried after a 10-frame settle
and recorded like any other input.

| Beat (folder) | Source | Frames | Counters | Presses | What happens | Owners / scripts |
|---|---|---|---|---|---|---|
| `a01_00_train_room` | 15_level_exit | 780 | 16563..17343 | f301 | arrival → around the crates → ledge grab on the crate stack, pull-up, drop off its south side → tunnel mouth | player only |
| `a01_01_tunnel` | a01_00 | 305 | 17344..17649 | — | tunnel mouth → mid tunnel beside the DATA BASE box | — |
| `a01_02_shaft_landing` | a01_01 | 590 | 17650..18240 | — | lower tunnel (y -60) → west stairs → shaft landing (y -35) | — |
| `a01_03_shaft_locked` | a01_02 | 991 | 18241..19232 | f17 (not taken), f69 | locked try, message, conversation script; **D_008107D9 = 0x80** | shaft door r12; door program 0x24DEC0.., 0x829860, 0x8298E0 |
| `a01_04_return_north` | a01_03 | 1959 (the last 23 of them tail idle) | 19233..21192 | f1035, f1637 | landing → tunnel → crate stack from the south → control-room door → entry 1 | door r15 (room move) |
| `a01_05_npc_bridge_talk` | a01_04 | 3959 | 21193..25152 | f130 | second NPC conversation; **D_00810759 = 0xFF, D_008107D9 = 0x81** | NPC r36, script 0x829FA0 |
| `a01_06_return_south` | a01_05 | 2646 | 25153..27799 | f150, f744 | control room → entry 2 → crate stack → tunnel → landing | door r15 |
| `a01_07_level_exit` | a01_06 | 4546 | 27800..32346 | f16 (not taken), f68 | door opens; **area change to AREA00 sub 0 entry 0**; AREA00 arrival script; control | shaft door r12 (door program 0x24DE40..) |
| `a01_s0_npc_first_talk` | 15_level_exit | 1434 | 16563..17997 | f79 (not taken), f131, f560 | arrival → control-room door → first NPC conversation | door r15, NPC r36, script 0x829E60 |
| `a01_s1_sentry_doc` | a01_01 | 396 | 17650..18046 | f228, f359 (Triangle) | take the DATA BASE pickup (item 0x48) | g0.1 00219550 |
| `a01_s2_control_room_items` | a01_s0 | 679 | 18003..18682 | f183, f335 (Tri), f494, f642 (Tri) | take the two control-room pickups | 0015AFA0 g0.8 / g0.7 |
| `a01_s3_fire_contact` | 15_level_exit | 261 | 16563..16824 | — | walk into the fire east of the crates: burn, health 100 → 95 | player reaction 0x3E |

Main line: 15 → a01_00 → … → a01_07 (15,776 frames, counters 16563..32346).
Health stays 100.0 on the whole main line.

## 4. Main-line beats

### a01_00 train room (780 frames)

The arrival room (x -75..40, z -520..-800) holds a derailed train across its
west half, crates, and burning fires (below). From (41, 0, -565.6) the stick
follows (33, -571), (26, -594), (20, -617), (30, -650), (33, -668), (33, -692),
(18, -697), then a half-stick approach that stops at (15.8, 1.0, -706.4) and a
turn to yaw pi.

| Frame | Event |
|---|---|
| f301 | Use against the crate stack (x 8..23, z -709..-730): action 8 (f304), clips 0x70 (f305), 0x71 (f325), 0x7A (f336). |
| f357 | Hang: +5 = 9, action 0x10, clip 0x7B (PLAYER_HANG.md). Stick forward from f357. |
| f360 | Pull-up: action 0x11, clip 0x7C; on top at y 28 from f464. |
| f491 | Walking on south off the stack: fall (action 0x0B; clip 0x73 at f493) … f519 landing (action 0x0F; clip 0x74 at f520) at y 0.24, z -740.2. |
| f567..f780 | Walk to the tunnel mouth; end at (-13.06, -1.91, -821.47). |

The route crosses over the stack to keep clear of the fires at (-12, -724),
(0, -722) and (1, -734); whether a floor path between them is burn-free was not
tested in a kept capture (section 5). The closest the main line came to any
fire node was 11.8 units (horizontal) from (31, 0, -710) at a01_00 f204, with
no burn.

### a01_01 tunnel, a01_02 shaft landing (305 + 590 frames)

The tunnel runs south from z -800 and falls from y 0 to -60, with steel frames,
fires on the tracks and a pickup box. Waypoints: (-26, -822), (-26, -848),
(-14, -856), (-14, -885), (14, -905), (15, -970), then (7, -975), (3, -988) —
**west of the DATA BASE box at (12.5, -984.5)** (that the box blocks the east
half until it is taken comes from the exploratory run) — (5, -1060),
(5, -1160), (-12, -1175), (-26, -1185), (-30, -1212), (-41, -1220), up the west
stairs (y -57.6 at z -1220 to y -35 by z -1258) to (-40.5, -1271.5). The
closed loop circled near (-30, -1212) for about 130 frames (f345..f480 of
a01_02, player action 6 with clip 6 at f450..f475) before it reached that
waypoint; a01_06 repeats the detour (action 6 at f2506). a01_01 ends at
(9.57, -27.94, -977.66); a01_02 at (-40.65, -35, -1272.30). No input but the
stick; health 100 throughout.

### a01_03 shaft door, locked (991 frames)

| Frame (counter) | Event |
|---|---|
| f17 | Use not taken (the player had just stopped). |
| f69 | Use taken. f72: 3B8D = 2, camera byte 1, bars on; the door owner runs the door program 0x24DEC0, 0x24DF00, 0x24DF40, 0x24DCC0.., player action 0x41 (f73), **clip 0x46** (locked front) at f78. |
| f143..f293 | Message 0x80000008 (msg mode 2). |
| f294..f296 | Script 0x829860 (op0D/3, 07/4), then 0x8298E0 starts: **D_008107D9 = 0x80 at f296** (18537). |
| f300 | Clip 0x155 (341), f432 clip 0x15E (350), f834 clip 0x15F (351): the scripted conversation. Message line 0x40 f499..f959. |
| f961 (19202) | Control (3B8D = 0, bars off). End at (-40.5, -35, -1271.5), yaw 0.7854. |

### a01_04 return north (1959 frames, the last 23 of them tail idle)

The reverse path, then the crate stack from the south: Use at f1035 from
(13.8, 1.0, -729.5), yaw -0.06 → action 8 (f1038), hang 0x10 (f1091), pull-up
0x11 (f1094), walk off the north side: fall 0x0B (f1225), landing 0x0F (f1253).
Then (33, -692), (32, -668), (22, -632), (19, -612), (30, -585), (52, -563.5),
Use at f1637 from (54.5, -564.8), yaw 1.549: door r15's program from 0x24DE40
(3B8D = 2 at f1640, action 0x41 at f1641, clip 0x45 at f1644), fade out f1738
with the room-move request (B7/B8 = 01/02), D_00810702 = 1 at f1802 and
control at f1803 at spawn entry 1. The first capture of this beat was discarded
because its resume check could not connect to the emulator (connection
refused); the kept capture is a fresh run of the beat.

### a01_05 NPC, second conversation (3959 frames)

From (76.2, -528.3), facing the NPC (yaw 0.533), Use at f130: 3B8D = 3 then 2
at f133/f134, script **0x829FA0** on the NPC from f133, clips 0x164 (356) f186,
0x167 (359) f233, 0x166 (358) f477, message line 0x0A from **f234 to f3836
(3602 frames)**, clip 0x165 (357) f3882, control at **f3929 (25122)** with
**D_008107D9 = 0x81 and D_00810759 = 0xFF** in the same frame.

While the line is shown, the script keeps running its records (op09 callbacks
0x825130 at f137 and f2738, 0x825240 at f2489 and f3836) and the message service's record
index at 0x282210 steps from 0 to 45 (f283 to f3790); record 45 holds from
f3790 until the line ends on its own at f3836. What each record shows on screen
is not recorded in the trace. The captured beat has no input after f130. In
the exploratory run (section 8), Cross pressed during the line changed nothing,
and Start skipped the scene (3B91 = 2, fade) and reached the same bytes.

### a01_06 return south (2646 frames)

Door r15 from inside, Use at f150 from (66.2, -562.1), yaw -1.609: program
0x24DE40 (clip 0x43 at f157), fade f231, request B7/B8 = 02/02, D_00810702 = 2
at f295 and control at f296 at spawn entry 2. Then the crate stack from the
north again (Use f744, action 8 at f747, hang f800, pull-up f803, fall f934,
landing f962), the tunnel and the stairs to (-40.65, -35, -1272.22).

### a01_07 level exit (4546 frames)

| Frame (counter) | Event |
|---|---|
| f16 | Use not taken. |
| f68 | Use taken; f71 3B8D = 2, door program 0x24DE40.., action 0x41 (f72), **clip 0x45** (open front) at f75. |
| f169 (27969) | Fade out; request block D_008106B0.. = 00 00 00 00 00 00 FF 00 01 00 (B6 = 0xFF, B8 = 1). |
| f233 | D_00810700..703 = 00 00 00 01; slot 0 +9 = 5. |
| f234..f447 | Area load (214 frames): slot 2 +8 = 1 with +9 stepping 1..11 over f234..f446 (step 8 with its +A/+B sub-steps 0/4, 0/5, 1/2, 1/3, the same order as beat 15's AREA01 load), +8 = 0x63 at f447; D_00275BD8 = 1 over f234..f447, 0 from f448. |
| f240 | Overlay header id 2 → **1 (AREA00)**. f244: D_00810703 = 00. |
| f449..f528 | Slot 0 +A = 2 (post-load wait). |
| f530 (28330) | Arrival rebuild: player at **(-40, -35, -1290), yaw pi** = AREA00 sub-0 spawn entry 0 (table 0x24AA50), control for this one frame. |
| f531..f533 | 3B8D = 4, then 2 (f533): the AREA00 arrival script. The player is placed at (-41.8, -35, -1260) at f535. |
| f535..f4281 | Message (AREA00 line 0) for 3746 frames; bars on. D_0081075A = 1 from f536. |
| f4485 | Player placed at (-39.7, -35, -1340.8), yaw -2.0944. |
| **f4486 (32286)** | Control; D_0081075A = 0xFF. End (f4546) at (-40.35, -35, -1340.62). |

From the door Use to AREA00 control: 4418 frames, of which 3953 are the AREA00
arrival script. In the exploratory run, AREA00 node 0x7A8250 carried that
script (records 0x828D60 .. 0x8294A0 of the AREA00 overlay) and freed itself
at its end.

## 5. Side beats and hazards

- **a01_s0 first NPC conversation** (1434 frames): door r15 (Use f79 not
  taken, f131 taken; entry 1 at f296, control f297), NPC Use at f560, 3B8D = 3
  then 2 at f563/f564, script 0x829E60, clip 0x166 at f612, message line 0
  f612..f1327, control at f1404. D_008107D9 stays 0.
- **a01_s1 DATA BASE pickup** (396 frames): Use at f228 on g0.1 (00219550,
  item 0x48): action 0x41, the take program 0x266620.. (the same boot-data
  program as the first level's battery pickup), the status request B0/B1 =
  03/48 (f298), the DATA BASE page (message 04/0x48 from f329), Triangle at
  f359, control at f366. D_00810D0B = 1 (f298) and the taken bit 0x810881
  bit 0 (f366). The pickup node is freed at f367.
- **a01_s2 control-room pickups** (679 frames): g0.8 (node 0x7A6DC0) at (61.8, 15, -549.6)
  (Use f183, request B0 = 2, message 04/00, Triangle f335, taken bit 0x810881
  bit 3), then g0.7 (node 0x7A6AD0) at (62.1, 16, -574.4) (Use f494, request B0/B1 = 03/20,
  message 04/20, Triangle f642, bit 1).
- **a01_s3 fire contact** (261 frames): walking from the arrival toward
  (34, -708) east of the crate stack, the player is burned at f209 (action
  0x3E, health 100 → 95, clip 0x20 at f210), standing 6.5 units (horizontal)
  from the fire node at (31, 0, -710). The fire nodes are the 001E3D90 nodes
  (class 13): the 14 table records [21]..[34] plus three deferred ones near the
  west wall. In the exploratory run the same fires burned the player for 7.5
  at 8.9..12.9 units and never within about 60 frames of a previous burn;
  that range does not fit the kept captures (a 6.5-unit burn here, and 11.8
  and 12.4 units from the same fire without a burn in a01_00 and a01_06), so
  the burn distance is open.
  Which function applies the burn was not identified. 001E3D90 (NEARMISS; its
  decomp comment calls it a muzzle-flash driver, a label that is not evidence)
  is the fire nodes' callback and installs 001E3D20 as their +0x34 tick
  callback; see section 6 for the frame evidence.

## 6. Census: what AREA01 executes beyond the first level

`route_census.py run --segments a01 --pass A01` replayed every AREA01 beat
(main and side) from its recorded source snapshot with the one-shot breakpoint on
all 3019 candidates: the 2957 boot functions of FUNCTIONS.csv and the 62 splat
pieces of the AREA01 overlay (runtime = splat label + 0x40, as for AREA11).
21 of those 62 pieces are not function entries but splits inside the 41 real
functions (decomp commit bdd40fb, `tools/overlay/overlay_match.py`), so
`route_census.py a01-delta --passes A01` folds a hit on a split piece into its
function and reports the real functions' sizes (one such hit: 0x8256B0, 0x40
into 0x825670, at a01_05). It then compared the result with the first-level
census (`build/s87/census/route_functions.json`: startup S0..S3 and beats
00..14, passes A and B; 1184 functions) and flagged the functions beat 15
already saw (`runs/A/15_level_exit.json`). Output:
`../Extermination/build/s87/census/a01_delta.json` and `runs/A01/<beat>.json`
(ignored). Neither command reads or changes the first-level outputs:
`report` and `exit-delta` never read the `a01_*` labels.

**Replay fidelity.** Four replays reproduced their recorded trace row for row
(a01_00 781/781, a01_s0 1435/1435, a01_s1 397/397, a01_s3 262/262). The others
did not: some started with a counter offset (a01_01 +1, a01_02 +1, a01_07 +6,
a01_s2 -5) and some took the closed loop a few frames differently (frames
replayed against recorded: a01_02 738/590, a01_03 992/991, a01_04 1966/1959,
a01_05 3961/3959, a01_06 2703/2646, a01_s2 683/679; identical rows: a01_03
511/992, a01_04 408/1960, a01_05 91/3960, a01_06 466/2647, the rest 0). All
twelve completed with their checks (landing reached, D_008107D9 = 0x80 / 0x81,
AREA00 control). Their function sets are the functions of the same route shape,
not of the exact recorded frames. The exit replay placed its area-change
consumer 001AD010 at f233 and the arrival placement 001B07C0 at f530, the same
frames as the recorded trace.

**Totals.** The AREA01 beats ran **943 functions** (929 boot, 14 AREA01
overlay). **789 of them are already in the first-level census; 154 are new
(97,392 bytes): 140 boot, 14 AREA01 overlay.**

| Group | New | Bytes | Flagged "Beat 15" | Decomp status |
|---|---:|---:|---:|---|
| In AREA01 play on the main line | 89 | 56,440 | 44 | NM 33, BM 31, OC 12, AW 6, AI 3, AU 2, CL 1, no source 1 |
| Only in side beats | 37 | 22,760 | 0 | BM 21, NM 10, AI 4, AW 2 |
| Only in the exit's change, load or AREA00 arrival | 28 | 18,192 | 9 | BM 14, NM 10, AW 2, CL 1, AI 1 |

- **The "Beat 15" column** marks 53 of the 154. They are:
  - 49 of the 53 AREA01-arrival functions of FIRST_LEVEL_EXIT.md section 5
    (41 in AREA01 play, 8 only after the AREA00 arrival in a01_07). The other
    four arrival functions, 001C4FA0, 001C50B0, 001D0C80 and 001D0D40, never
    run in any a01 beat;
  - three of beat 15's change-and-load functions: 001B0C00 and 001FAD70 (first
    at the a01_07 request frame, f169, so they count as main-line) and
    001195A8 (the AREA00 load, f282);
  - 0x823580 (see the next item).

  That 53 equals 53 is a coincidence.
- **Beat 15 armed only AREA11 overlay addresses.** An AREA01 overlay row can
  show "yes" only where an AREA11 candidate shares its runtime address: that is
  the case for 0x823580, hit in beat 15 while AREA01 (id 2) was resident. The
  other 13 overlay rows say "not armed", which is not evidence that beat 15 did
  not run them (AREA01_OVERVIEW.md has 0x826D40 live at the arrival). So of the
  89 main-line functions, 45 are not flagged: 32 boot functions that beat 15 did
  not run and 13 overlay functions beat 15 could not see.
- Decomp status (FIRST_LEVEL_EXIT.md's abbreviations): BM byte-matched C, NM
  NEARMISS, AI/AW inline asm / `.word` asm, CL C linked from asm, AU
  undecompiled, "no source" no src file. **OC** is an AREA01 overlay C file; at
  decomp commit bdd40fb every one is byte-identical (the decomp's PROGRESS.md,
  2026-09-25: all 32 AREA01 C functions 100% in overlay_match.py, overlays
  byte-identical under verify_all). The tool marks any C file without a
  NEARMISS or asm marker as OC, so after that commit OC means byte-identical
  only once the overlay gate has passed again. The two AU overlay functions are
  0x823580 (a jr-table dispatcher) and 0x826D40 (not started). The boot
  statuses are those of the src tree when a01-delta last ran.
- 13 hits at AREA01 candidate addresses while the AREA00 overlay (id 1) was
  resident (a01_07 after f240) are AREA00 code and are excluded.
- **Frames** are the census replay's; a function that runs every frame shows
  "00 f1" (its first frame after arming). **Beats**: 00..07 main, s0..s3 side.
  An overlay name with "+ piece" is a function that splat split; the piece's
  runtime address is given.
- What the groups show, from the frames (a function's name is not evidence of
  its role): the per-frame AREA01 world (owners 00128C10, 00157CE0..0015A2C0,
  001BB560/001BB860 doors, 001C0004/001C02E0, the fires 001E3D90, 001E7D20,
  overlay 0x823580/0x825350/0x8261A0/0x8267C0/0x826D40/0x828850); the ledge
  grab, hang and pull-up (0017F320, 001647D0, 0017F240, 0017E250, 0017E510,
  00182250 at f336..f360 of a01_00); the locked-door program and the conversation script
  (001BBBF0, 001BBAE0, 001B9CF0, 001B76D0 in a01_03); the NPC's 0x80 branch
  0x825590 from a01_03 f296 (the frame D_008107D9 became 0x80), the
  second-talk callbacks 0x825130/0x825240 (the script's op09 records) and the
  0x81 branch 0x825670 from a01_05 f3932; the DATA BASE and item pages
  (0020F950.., 002131B0.., 00213F30.. in s1/s2); and the burn in s3:
  **001E3D20 and 001EFE00 first run at f208, one frame before the burn
  reaction** (0021BC40, 0021C350, 0021D800, 0015B770 at f209/f210; the s3
  replay is row-identical to its trace). 001E3D20 is the fire node's +0x34 tick
  callback (set by 001E3D90); its byte-matched C, when the second argument's
  flag bit 2 is clear and 0021BB00(D_008102B0) returns 0, posts event
  0x80000027 through 001EFE00 for that argument, sets its +0xF to 0xC and
  stores 0x3C (60) at the first argument's +0x1F0, which fits the burn and the
  exploratory run's 60-frame gap. That it applies the burn is a candidate, not a finding:
  the function that turns the event into the health loss was not traced.

#### In AREA01 play on the main line: 89

| Address | Name | Bytes | Decomp | First (beat, census frame) | Beats | Subsystem | Beat 15 |
|---|---|---:|---|---|---|---|---|
| 0x001287F0 | func_001287F0 | 56 | CL | 00 f1 | 00,01,02,03,04,06,07,s0,s1,s3 | lowmem | yes |
| 0x00128B80 | func_00128B80 | 132 | NM | 00 f1 | 00,01,02,03,04,06,07,s0,s1,s3 | lowmem | yes |
| 0x00128C10 | func_00128C10 | 2924 | NM | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | lowmem | yes |
| 0x00157CE0 | func_00157CE0 | 592 | BM | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | entity_logic | yes |
| 0x00158590 | func_00158590 | 632 | NM | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | entity_logic | yes |
| 0x00158D30 | func_00158D30 | 388 | NM | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | entity_logic | yes |
| 0x00159B90 | func_00159B90 | 724 | NM | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | entity_logic | yes |
| 0x0015A2C0 | func_0015A2C0 | 1164 | NM | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | entity_logic | yes |
| 0x0019B4C0 | func_0019B4C0 | 512 | NM | 00 f1 | 00,04,07,s0,s3 | level_world | yes |
| 0x0019CF50 | func_0019CF50 | 992 | NM | 00 f1 | 00,04,07,s0,s3 | level_world | yes |
| 0x001A06A0 | func_001A06A0 | 1132 | NM | 00 f1 | 00,04,07,s0,s3 | level_world | yes |
| 0x001B13F0 | func_001B13F0 | 124 | BM | 00 f1 | 00,01,02,03,04,06,07,s0,s1,s3 | math_vector | yes |
| 0x001B2140 | func_001B2140 | 2500 | NM | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | math_vector | yes |
| 0x001BB560 | func_001BB560 | 608 | BM | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | math_vector | yes |
| 0x001BB860 | func_001BB860 | 628 | NM | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | math_vector | yes |
| 0x001BF630 | func_001BF630 | 124 | AI | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | math_vector | yes |
| 0x001BFFD0 | func_001BFFD0 | 52 | NM | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | math_vector | yes |
| 0x001C0004 | func_001C0004 | 724 | no source | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | math_vector | yes |
| 0x001C02E0 | func_001C02E0 | 1016 | NM | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | math_vector | yes |
| 0x001C25E0 | func_001C25E0 | 168 | BM | 00 f1 | 00,04,07,s0,s3 | math_vector | yes |
| 0x001C2770 | func_001C2770 | 2176 | NM | 00 f1 | 00,01,02,03,04,06,07,s0,s1,s3 | math_vector | yes |
| 0x001C39F0 | func_001C39F0 | 488 | BM | 00 f1 | 00,01,02,03,04,06,07,s0,s1,s3 | math_vector | yes |
| 0x001C3BE0 | func_001C3BE0 | 384 | BM | 00 f1 | 00,01,02,03,04,06,07,s0,s1,s3 | math_vector | yes |
| 0x001C3D60 | func_001C3D60 | 68 | BM | 00 f1 | 00,01,02,03,04,06,07,s0,s1,s3 | math_vector | yes |
| 0x001C69A0 | func_001C69A0 | 1016 | NM | 00 f1 | 00,01,02,03,04,06,07,s0,s1,s3 | anim_runtime | yes |
| 0x001CB360 | func_001CB360 | 84 | BM | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | anim_runtime | yes |
| 0x001CD070 | func_001CD070 | 272 | NM | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | gs_upload | yes |
| 0x001CD180 | func_001CD180 | 304 | NM | 00 f1 | 00,01,02,04,05,06,07,s0,s1,s2,s3 | gs_upload | yes |
| 0x001CD2B0 | func_001CD2B0 | 192 | BM | 00 f1 | 00,01,02,04,05,06,07,s0,s1,s2,s3 | gs_upload | yes |
| 0x001D4FC0 | func_001D4FC0 | 424 | NM | 00 f1 | 00,04,05,06,s0,s2,s3 | render_vif | yes |
| 0x001D5A70 | func_001D5A70 | 344 | AW | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | render_vif | yes |
| 0x001D5BD0 | func_001D5BD0 | 164 | BM | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | render_vif | yes |
| 0x001E3D90 | func_001E3D90 | 2160 | NM | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | weapon_equip | yes |
| 0x001E7CB0 | func_001E7CB0 | 100 | BM | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | stream_archive | yes |
| 0x001E7D20 | func_001E7D20 | 3608 | NM | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | stream_archive | yes |
| 0x001E9E60 | func_001E9E60 | 932 | NM | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | hud_objects | yes |
| 0x001F4A10 | func_001F4A10 | 480 | NM | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | fx_render | yes |
| 0x001F4BF0 | func_001F4BF0 | 200 | AI | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | fx_render | yes |
| 0x001F4CC0 | func_001F4CC0 | 120 | BM | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | fx_render | yes |
| 0x00823580 | overlay_AREA01_func_00823540 | 588 | AU | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | overlay_AREA01 | yes |
| 0x00825350 | func_overlay_AREA01_00825310 | 352 | OC | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | overlay_AREA01 | not armed |
| 0x008254B0 | func_overlay_AREA01_00825470 (+ piece 0x008254F0) | 212 | OC | 00 f1 | 00,01,02,03,s0,s1,s2,s3 | overlay_AREA01 | not armed |
| 0x008261A0 | func_overlay_AREA01_00826160 | 96 | OC | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | overlay_AREA01 | not armed |
| 0x00826200 | func_overlay_AREA01_008261C0 (+ piece 0x00826240) | 572 | OC | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | overlay_AREA01 | not armed |
| 0x00826440 | func_overlay_AREA01_00826400 (+ piece 0x00826480) | 884 | OC | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | overlay_AREA01 | not armed |
| 0x008267C0 | func_overlay_AREA01_00826780 | 396 | OC | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | overlay_AREA01 | not armed |
| 0x00826CF0 | func_overlay_AREA01_00826CB0 | 68 | OC | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | overlay_AREA01 | not armed |
| 0x00826D40 | func_overlay_AREA01_00826D00 | 5544 | AU | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | overlay_AREA01 | not armed |
| 0x00828850 | func_overlay_AREA01_00828810 | 408 | OC | 00 f1 | 00,01,02,03,04,05,06,07,s0,s1,s2,s3 | overlay_AREA01 | not armed |
| 0x001A8840 | func_001A8840 | 304 | BM | 00 f6 | 00,01,02,03,04,05,06,07,s0,s1,s3 | unknown_01 |  |
| 0x001A9E00 | func_001A9E00 | 352 | BM | 00 f6 | 00,04,07,s0,s3 | unknown_01 |  |
| 0x001B0D80 | func_001B0D80 | 60 | BM | 00 f35 | 00,01,02,03,04,06,07,s0,s1,s3 | entity_sys | yes |
| 0x001D5170 | func_001D5170 | 368 | NM | 00 f53 | 00,04,05,06,s0,s2,s3 | render_vif |  |
| 0x001AA000 | func_001AA000 | 312 | AI | 00 f57 | 00,04,s3 | frame_main | yes |
| 0x00187EC0 | func_00187EC0 | 32 | BM | 00 f137 | 00,02,04,06,s3 | actor_anim |  |
| 0x001EAF00 | func_001EAF00 | 124 | NM | 00 f138 | 00,02,04,06,s3 | hud_objects |  |
| 0x001F0460 | func_001F0460 | 704 | BM | 00 f178 | 00,01,02,04,06,s3 | fx_render |  |
| 0x001776E0 | func_001776E0 | 1172 | NM | 00 f304 | 00,04,06 | actor_anim |  |
| 0x00177CF0 | func_00177CF0 | 592 | NM | 00 f304 | 00,04,06 | actor_anim |  |
| 0x0017F320 | func_0017F320 | 708 | NM | 00 f336 | 00,04,06 | actor_anim |  |
| 0x00188550 | func_00188550 | 28 | BM | 00 f357 | 00,04,06 | actor_anim |  |
| 0x00191120 | func_00191120 | 236 | AW | 00 f357 | 00,04,06 | init_io |  |
| 0x001647D0 | func_001647D0 | 5004 | NM | 00 f358 | 00,04,06 | frame_update |  |
| 0x0017F240 | func_0017F240 | 224 | AW | 00 f358 | 00,04,06 | actor_anim |  |
| 0x00182250 | func_00182250 | 400 | BM | 00 f358 | 00,04,06 | actor_anim |  |
| 0x0017E250 | func_0017E250 | 696 | NM | 00 f360 | 00,04,06 | actor_anim |  |
| 0x0017E510 | func_0017E510 | 460 | BM | 00 f360 | 00,04,06 | actor_anim |  |
| 0x001755B0 | func_001755B0 | 132 | BM | 00 f492 | 00,04,06 | actor_anim |  |
| 0x00164220 | func_00164220 | 388 | BM | 00 f520 | 00,06 | frame_update |  |
| 0x00187DE0 | func_00187DE0 | 180 | BM | 02 f39 | 02,04,06 | actor_anim |  |
| 0x001E8B90 | func_001E8B90 | 748 | NM | 02 f39 | 02,04,06 | hud_objects |  |
| 0x001EB020 | func_001EB020 | 560 | NM | 02 f39 | 02,04,06 | hud_objects |  |
| 0x001EAF80 | func_001EAF80 | 156 | BM | 02 f44 | 02,04,06 | hud_objects |  |
| 0x001EC270 | func_001EC270 | 384 | NM | 02 f599 | 02,06,s2 | hud_objects |  |
| 0x001BBBF0 | func_001BBBF0 | 300 | NM | 03 f75 | 03 | math_vector |  |
| 0x001B6D70 | func_001B6D70 | 208 | BM | 03 f142 | 03,07 | input_io |  |
| 0x001BBAE0 | func_001BBAE0 | 272 | BM | 03 f143 | 03 | math_vector |  |
| 0x00825590 | func_overlay_AREA01_00825550 (+ piece 0x008255D0) | 220 | OC | 03 f296 | 03,04,05 | overlay_AREA01 | not armed |
| 0x001B9CF0 | func_001B9CF0 | 764 | BM | 03 f301 | 03 | math_vector |  |
| 0x001B76D0 | func_001B76D0 | 36 | BM | 03 f431 | 03 | unknown_02 |  |
| 0x00163D50 | func_00163D50 | 316 | AW | 04 f1255 | 04 | frame_update |  |
| 0x0015B610 | func_0015B610 | 352 | AW | 04 f1810 | 04,06,07,s0 | frame_update |  |
| 0x00183250 | func_00183250 | 416 | AW | 04 f1810 | 04,06,07,s0 | actor_anim |  |
| 0x001B0300 | func_001B0300 | 344 | BM | 04 f1810 | 04,05,s0,s2 | entity_sys |  |
| 0x00825130 | func_overlay_AREA01_008250F0 | 260 | OC | 05 f139 | 05 | overlay_AREA01 | not armed |
| 0x00825240 | func_overlay_AREA01_00825200 | 260 | OC | 05 f2491 | 05 | overlay_AREA01 | not armed |
| 0x00825670 | func_overlay_AREA01_00825630 (+ piece 0x008256B0) | 208 | OC | 05 f3932 | 05,06,07 | overlay_AREA01 | not armed |
| 0x001B0C00 | func_001B0C00 | 88 | BM | 07 f169 | 07 | entity_sys | yes |
| 0x001FAD70 | func_001FAD70 | 244 | BM | 07 f169 | 07 | stream_music | yes |

#### Only in side beats: 37

| Address | Name | Bytes | Decomp | First (beat, census frame) | Beats | Subsystem | Beat 15 |
|---|---|---:|---|---|---|---|---|
| 0x00213F30 | func_00213F30 | 240 | BM | s1 f329 | s1,s2 | ui_screens |  |
| 0x00214020 | func_00214020 | 1352 | NM | s1 f329 | s1,s2 | ui_screens |  |
| 0x001FCF60 | func_001FCF60 | 44 | BM | s1 f330 | s1,s2 | audio |  |
| 0x001FCF90 | func_001FCF90 | 328 | NM | s1 f330 | s1,s2 | audio |  |
| 0x001FE660 | func_001FE660 | 80 | BM | s1 f330 | s1,s2 | stream_cd |  |
| 0x002131B0 | func_002131B0 | 776 | BM | s1 f330 | s1,s2 | ui_screens |  |
| 0x002134C0 | func_002134C0 | 1332 | NM | s1 f330 | s1,s2 | ui_screens |  |
| 0x001C4720 | func_001C4720 | 52 | BM | s2 f246 | s2 | math_vector |  |
| 0x001AFF10 | func_001AFF10 | 120 | BM | s2 f279 | s2 | entity_sys |  |
| 0x0020F950 | func_0020F950 | 1752 | NM | s2 f279 | s2 | ui_screens |  |
| 0x001AF7C0 | func_001AF7C0 | 52 | BM | s2 f280 | s2 | entity_sys |  |
| 0x001B0000 | func_001B0000 | 108 | BM | s2 f280 | s2 | entity_sys |  |
| 0x001CB480 | func_001CB480 | 112 | AI | s2 f280 | s2 | anim_runtime |  |
| 0x00207D90 | func_00207D90 | 172 | BM | s2 f280 | s2 | draw2d |  |
| 0x00208040 | func_00208040 | 352 | BM | s2 f280 | s2 | draw2d |  |
| 0x00210030 | func_00210030 | 392 | NM | s2 f280 | s2 | ui_screens |  |
| 0x002101C0 | func_002101C0 | 2108 | NM | s2 f280 | s2 | ui_screens |  |
| 0x00210A00 | func_00210A00 | 508 | BM | s2 f280 | s2 | ui_screens |  |
| 0x00210C00 | func_00210C00 | 812 | BM | s2 f280 | s2 | ui_screens |  |
| 0x00210F30 | func_00210F30 | 780 | NM | s2 f280 | s2 | ui_screens |  |
| 0x00211400 | func_00211400 | 976 | NM | s2 f280 | s2 | ui_screens |  |
| 0x001D8100 | func_001D8100 | 36 | BM | s3 f208 | s3 | render_vif |  |
| 0x001E3D20 | func_001E3D20 | 108 | BM | s3 f208 | s3 | weapon_equip |  |
| 0x001EFE00 | func_001EFE00 | 172 | AI | s3 f208 | s3 | fx_render |  |
| 0x0017C370 | func_0017C370 | 208 | AI | s3 f209 | s3 | actor_anim |  |
| 0x001F0190 | func_001F0190 | 256 | BM | s3 f209 | s3 | fx_render |  |
| 0x001F0290 | func_001F0290 | 48 | BM | s3 f209 | s3 | fx_render |  |
| 0x0021BC40 | func_0021BC40 | 200 | AW | s3 f209 | s3 | area_state |  |
| 0x0021C350 | func_0021C350 | 148 | AI | s3 f209 | s3 | area_logic |  |
| 0x0022B700 | func_0022B700 | 160 | BM | s3 f209 | s3 | ui_credits |  |
| 0x0022B7A0 | func_0022B7A0 | 972 | BM | s3 f209 | s3 | ui_credits |  |
| 0x0022BB70 | func_0022BB70 | 68 | BM | s3 f209 | s3 | ui_credits |  |
| 0x0022BBC0 | func_0022BBC0 | 6036 | NM | s3 f209 | s3 | ui_credits |  |
| 0x0015B770 | func_0015B770 | 728 | BM | s3 f210 | s3 | frame_update |  |
| 0x0021D1A0 | func_0021D1A0 | 172 | AW | s3 f210 | s3 | area_logic |  |
| 0x0021D600 | func_0021D600 | 56 | BM | s3 f210 | s3 | area_logic |  |
| 0x0021D800 | func_0021D800 | 944 | NM | s3 f210 | s3 | area_logic |  |

#### Only in the exit's area change, load or AREA00 arrival (a01_07 after f233): 28

| Address | Name | Bytes | Decomp | First (beat, census frame) | Beats | Subsystem | Beat 15 |
|---|---|---:|---|---|---|---|---|
| 0x001195A8 | func_001195A8 | 168 | BM | 07 f282 | 07 | lowmem | yes |
| 0x00128390 | func_00128390 | 52 | CL | 07 f531 | 07 | lowmem | yes |
| 0x001289C0 | func_001289C0 | 240 | BM | 07 f531 | 07 | lowmem | yes |
| 0x00128AB0 | func_00128AB0 | 204 | BM | 07 f531 | 07 | lowmem | yes |
| 0x0012A5D0 | func_0012A5D0 | 2032 | BM | 07 f531 | 07 | lowmem |  |
| 0x00156F30 | func_00156F30 | 1072 | NM | 07 f531 | 07 | entity_logic |  |
| 0x001581A0 | func_001581A0 | 316 | NM | 07 f531 | 07 | entity_logic |  |
| 0x00158810 | func_00158810 | 948 | NM | 07 f531 | 07 | entity_logic |  |
| 0x00158BD0 | func_00158BD0 | 340 | NM | 07 f531 | 07 | entity_logic |  |
| 0x0015AB00 | func_0015AB00 | 244 | BM | 07 f531 | 07 | entity_logic |  |
| 0x0015B030 | func_0015B030 | 248 | AW | 07 f531 | 07 | frame_update |  |
| 0x001BB520 | func_001BB520 | 56 | BM | 07 f531 | 07 | math_vector | yes |
| 0x001D0400 | func_001D0400 | 172 | AI | 07 f531 | 07 | obj_registry |  |
| 0x001E8E80 | func_001E8E80 | 1012 | NM | 07 f531 | 07 | hud_objects |  |
| 0x001E9580 | func_001E9580 | 2264 | BM | 07 f531 | 07 | hud_objects | yes |
| 0x0022DCD0 | func_0022DCD0 | 2396 | NM | 07 f531 | 07 | unknown_07 |  |
| 0x00129780 | func_00129780 | 1916 | BM | 07 f532 | 07 | lowmem | yes |
| 0x001576E0 | func_001576E0 | 384 | BM | 07 f532 | 07 | entity_logic |  |
| 0x001C2430 | func_001C2430 | 148 | BM | 07 f532 | 07 | math_vector |  |
| 0x001C2540 | func_001C2540 | 152 | AW | 07 f532 | 07 | math_vector | yes |
| 0x001C3DB0 | func_001C3DB0 | 764 | NM | 07 f532 | 07 | math_vector | yes |
| 0x001E9280 | func_001E9280 | 756 | NM | 07 f532 | 07 | hud_objects |  |
| 0x001C6160 | func_001C6160 | 44 | BM | 07 f755 | 07 | anim_runtime |  |
| 0x00113478 | func_00113478 | 180 | NM | 07 f3468 | 07 | lowmem |  |
| 0x00128640 | func_00128640 | 420 | BM | 07 f4486 | 07 | lowmem |  |
| 0x00128600 | func_00128600 | 64 | BM | 07 f4487 | 07 | lowmem |  |
| 0x0012AFC0 | func_0012AFC0 | 1092 | BM | 07 f4487 | 07 | lowmem |  |
| 0x0012ADC0 | func_0012ADC0 | 508 | NM | 07 f4488 | 07 | lowmem |  |

## 7. Not covered

- The north room beyond the two 0x8261A0 bridges (doors [14] and [16] to
  AREA02), the east corridor (room move [17]) and the upper floor at y 60
  (doors [18] to area 0x16 and [19] to AREA06, the six 00128C10 class-2 nodes).
  None is on the main path; the bridge owners' sampled state never changed.
- The NPC's third branch (D_008107D9 == 0x81, script 0x82A660), the second NPC
  record [38] (overlay 0x825740, not spawned in this load), the crate-top pickup
  at (14.8, 14.5, -717.3), the remaining deferred pickups
  (0015AFA0 at (136.5, 60.1, -473.1), (111.8, 8, -509.5), (142.8, 2.1, -416.2),
  (-69.3, 25.4, -731.1); 00219550 g0.0 at (-34.8, 14.8, -722.6)): none was
  taken. The 0x828850 nodes, the 00156620 nests and the 0015A2C0 nodes were
  not sampled; of the 0x826D40 nodes only the +1 toggle was seen (section 1).
- Combat: the player's health changed only in a01_s3 (the burn); no attack on
  the player was recorded.
- AREA00 after its arrival control.

## 8. Reproduce and limits

Decomp repo, `.venv` python, repo root. The emulator runs hidden, one session at
a time, and is closed after each beat; no save-state slot is written (each
snapshot slot file is moved into the beat folder).

```sh
.venv/bin/python tools/route_capture.py run --beats a01        # 12 beats; host time was not recorded
.venv/bin/python tools/route_capture.py events --beats a01
.venv/bin/python tools/route_census.py run --segments a01 --pass A01   # about 27 min in this run
.venv/bin/python tools/route_census.py a01-delta --passes A01
```

- Every beat is closed-loop from its source snapshot; beat a01_00 depends on
  `build/s87/route/15_level_exit/state.p2s` (FIRST_LEVEL_EXIT.md).
- Three presses were not taken by the original right after the player stopped
  (a01_03 f17, a01_07 f16, a01_s0 f79); the tool retries after a 10-frame
  settle, so the inputs list both.
- The route and the scripts were first learned in an exploratory session
  (not kept) that followed the same path; the facts it alone supplied are marked
  "exploratory run" above: the 7.5 burns and their distances, the DATA BASE
  page's content, the box blocking the tunnel, Cross and Start during the NPC's
  line, and the AREA00 script node 0x7A8250.
- a01-delta's overlay statuses and sizes come from the decomp tree it runs on
  (`src/overlays/AREA01`, `tools/overlay/overlay_match.py`); the numbers above
  are from a run on bdd40fb.
- Walkable space was planned from the level's collision polygons exported by the
  retired drawbridge exporter (the geometry only; every path was then walked in
  the original).
