# [7]'s room, door [27] and AREA19 sub 1: thirteenth-level route, original ground truth

Lane STORY (capture), 2026-10-01 (session s93). Original executable
SHA-256 `ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

`TWELFTH_LEVEL_ROUTE.md` left the story in AREA19 at the top of the ladder
at z 974 (a19b_02: [7]'s first sequence and [6]'s first stage had run), with
[7]'s room, its Use (0x825930, door [27]'s lock bit 2) and [6]'s second stage
(0x825420) not reached, and a census blind spot in a19b_00. This document
closes the blind spot (section 5.1), derives the way into [7]'s room from the
code and the level's collision (section 2) and records the story from there
as one opt-in beat group of `../Extermination/tools/route_capture.py`, `a19c`
(8 beats, AREA19), from a19b_02's end. Hidden PCSX2, exact one-frame steps,
pad input only, no teleports and no memory writes, one trace and one
resumable snapshot per beat. Nothing here is port behaviour.

**Status.**

- **[7]'s room was reached and [7] used.** The way: the bar over the y-172
  floor (attribute 0x34, entered from its attribute-0x3A pad on the walkway),
  the swing at its end and the release onto the y-195 platform; the second
  bar from the platform west over the pit into [7]'s room. Entering the room
  ran [6]'s second stage (script 0x82BA00: flag 0x1D and counter 0x1D =
  0xFF); [7]'s Use ran script 0x82BD90 (counter 0x1E -> 0xFF, flag 0x1E 1 ->
  0xFF) and set door [27]'s lock bit 2 (D_00810854 = 0x04).
- **Door [27] was opened from its east side** (room move to entry 2) and the
  region behind it (the floor west of door [27], the stair up to the y-210
  floor) played; its ladder at z 859.5 led into **AREA19 sub 1** (entry 7),
  which no earlier level reached; sub 1's door [52] gave entry 1, whose
  arrival script ([34], 0x8279E0) set flag 0x46 and counter 0x46 to 0xFF.
- **No new area (overlay) was reached.** AREA19 sub 1's doors to AREA15
  ([50] / [51]) lie in a stair tower whose door [49] from the hall is locked
  (bit 3, the seal [48], which no hit from the hall side changed; section 6);
  the way into the tower is derived, not played (section 7).
- **Health:** 90 at the start, 72 after one hit on the ladder at z 859.5
  (a19c_06 f1131, source not read), 72 at the end. Infection 0.
- Group: 8 beats, 16,403 frames, counters 135407..151817 (no counter gap:
  each beat starts on the counter after the previous beat's last).
- **Census** (section 5): **36 functions (23,628 bytes) that no earlier
  level ran**: 22 boot and 14 AREA19 overlay (10 of them sub 1's
  placements). The twelfth level's blind spot is closed: a19b_00 ran
  nothing new in its unmeasured frames, so its counts are exact.
- **Web leads:** the public walkthroughs (GameFAQs guides by MACisBack,
  VicViper_Mk2 and _PSXTREME_, the Neoseeker and SuperCheats pages, the
  Cheat Code Central page) answered this session's fetches with HTTP 403 or
  were not fetchable; nothing here comes from them.

In-game text (messages, captions, prompts) is described, never quoted.
Positions are world units; yaw is the player's +0xC4 (X = sin, Z = cos).

## 1. Tool, outputs and conventions

```sh
# decomp repo, .venv python, repo root (hidden PCSX2; hold build/.pcsx2.lock)
.venv/bin/python tools/route_capture.py run --beats a19c
.venv/bin/python tools/route_capture.py events --beats a19c
.venv/bin/python tools/route_capture.py verify --beats a19c
```

`--beats all` and every earlier group are unchanged. a19c_00 starts from
`build/s87/route_a19b/a19b_02_ladder1023/state.p2s`; each later beat from the
previous beat's snapshot. Outputs: `../Extermination/build/s87/route_a19c/<beat>/`
(ignored): `trace.json`, `state.p2s`, `eeMemory.bin`, `gs.bin`,
`scratchpad.bin`, `original.png`, `snapshot.json`. Every kept snapshot was
reloaded after its capture (`run_beat`'s check) and resumed on the first
check (`tail_idle_frames` 0). The group was captured in one lock hold
through a scratch wrapper of this lane that raises the DebugServer / Pine
socket timeouts to 60 s in its own process (as TENTH_LEVEL_ROUTE.md
section 1).

- **Rows**: the a19 decoder (TENTH_LEVEL_ROUTE.md) with a19b's owners
  (`A19C_OWNERS` = `A19B_OWNERS`, the same AREA19 sub-0 load). After the
  sub change (a19c_06 f1524) the owner rows read sub 1's nodes at those
  addresses and are not the named owners. `s770` [10:12] flag 0x1D, [12:14]
  flag 0x1E (D_00810776); `s7f0` [10:12] counter 0x1D, [12:14] counter 0x1E;
  `s79c` [4:6] flag 0x46 (D_0081079E); `s81c` [4:6] counter 0x46
  (D_0081081E); `l854` D_00810854; `area4` D_00810700..703; `hp`
  D_008104D0.
- **Helpers** (route_capture.py): `a19c_bar_grab` (Cross facing a yaw on an
  attribute-0x3A pad until +5 0xF / 0x10), `a19c_traverse` (the stick toward
  a point until the hang holds still for 20 frames at the bar's end, or the
  swing, action 0x28), `a19c_drop` (Cross while hanging until +5 leaves
  0xF / 0x10), `a19c_until_control` (no input; step until 60 frames of
  control and a story predicate; the scripts of this group play their
  message pages on their own timers, section 6). The a13 / a19 / a13d
  helpers as before.

## 2. What decides each step (code, then collision, then capture)

Evidence marks as in decomp docs/WORLD_GRAPH.md: BM byte-matched C, NM
NEARMISS C, C overlay C linked byte-identical. Collision: AREA19 sub 0's grid
(the port's local `assets/area19/sub0/area19.emcl`, export_area19_level.py,
node bytes verified there against captured RAM), AREA19 sub 1's grid read
from the captured RAM (the scratchpad pointers 0x700031FC..0x7000320C, the
same 64-byte nodes), and the cell directory (*0x70003250) of the captured
RAM.

- **Why [7]'s room, and where it is.** Door [27] (0x823580, C) opens only
  with D_00810854 bit 2, which [7]'s second sequence 0x825930 (C) sets when
  counter 0x1E reaches 0xFF in script 0x82BD90; 0x825930 starts that script
  on [7]'s +0xB bit 2 (its Use) once counter 0x1E != 0
  (TWELFTH_LEVEL_ROUTE.md section 2). [7] (913.7, 198.2, 919.6), class 0x84,
  descriptor 0x82C6D0 (10.0, 20.0, ...) stands in a raised room (grid floor
  node 197 at y 195.1, x 885..935, z 910..960; ceiling y 225; low
  attribute-0x50 railings on all sides; player-only attribute-0x51 walls
  (#1049 / #1056) at its south-east corner where the pipe leaves it). [6]'s
  second stage's area 0x82BD40 (read from the overlay image: x 885..933, z
  912..959, y 195) is this room's floor; 0x825420 (C) also moves [6] to
  the point after it, (954.5, 210.5, 986.7).
- **The way into the room: the bar** (its other sides carry railings and
  player-only walls, read; the y-230 floor above is the room's ceiling
  slab). An attribute-0x34 face
  (node 841) at y 223.2 runs x 890.7..1003.2, z 925.4..935.4, with
  attribute-0x3A pads under both ends: node 866 (x 888.7..902.7, y 197) in
  the room and node 913 (x 991.7..1004.1, y 198, on a box on the y-195
  platform). 0015D4C0 (NM) case 0x3A probes 40 up and, on attribute 0x34,
  enters the hang (+5 0xF, action 0x20; then +5 0x10 / 00169730, NM, action
  0x21). Under the bar lies the pit: an attribute-0x5D grid floor at y
  140.3 (the attribute whose floor contact sets health 0 through 0021D250:
  AREA11's truck pit, decomp docs/CAPTURES_C10.md dmg_07) with the
  kind-0x5B cell 5 above it (a water surface at y 160, x 770..990, z
  820..1025; see the water item below). The pit was not entered.
- **The y-195 platform** (nodes 144 / 145, x 990..1033, z 915..945) is
  sealed for the player except from above: its north slope (nodes 153 /
  158) carries the slide class 0x1000; its west edge has player-only walls
  (#1204 / #1205 / #1206 / #1461, attribute 0x51); its doorway in the wall x
  1033..1036 (z 929..944, y 195..220.3) has the player-only wall #1196 on
  its basement side; its south strip (x 990..1020, z 915..924) is fenced by
  #1048 / #1050. Measured: a walk up the slope from the y-172 floor stops
  at z 972.4 (exploration at x 1015 and x 1027: section 6).
- **The bar over the y-172 floor and its swing.** An attribute-0x34 face
  (node 840) at y 253.4 over x 1012.9..1022.9, z 972.4..1039.9 has its
  attribute-0x3A pad on the walkway (node 914, x 1012.4..1023.4, z
  1026.3..1039, y 223). Its south end (z 972.4) has no wall within 20
  ahead, so 00169730's stick-forward case (+24C 0) goes to +6 0x50 /
  0016A4B0 (NM): action 0x28, the swing. In the swing (0016A4B0 +7 2) Use
  moves to +7 3, which at the swing's end (+2E == 3, +28 >= 0x18) releases:
  speed D_00248630[+25C] (0, 0.2, 0.4, 0.8 for +25C 0..3, read from RAM)
  decreasing by 1/120 of it a frame along the heading, 00179880 with +2EC =
  0.4, until a floor (00175900 -> 0017C580). Measured (a19c_00): Cross at
  f764 with the stick held south since the swing began (f644); the trace
  then shows the player moving south from z 958 (y about 239, f767) to z
  929.7 (f815), 5.5 north of the attribute-0x51 fence #1046 (z 924.2; which
  test ended the forward motion was not read), and landing on the platform
  at (1017.9, 195, 929.7) f828 (action 0x0F).
- **[6]'s second stage, then [7]'s Use** (a19c_01, a19c_02): the drop from
  the bar's west end (Cross: 00169730 +6 0xA) ran 0x825420's script
  0x82BA00 from f804, the frame the falling player reached y 199.6 inside
  the area; its message pages ran on their own (3B8D 2 from f815 to
  f7777). [7]'s Use from (913.7, 195.1, 928.8) facing -z (Cross f98) ran
  0x82BD90.
- **The water behind door [27].** The cell directory's static entries 0..5
  belong to the class-0x0B placements [0]..[5]; [2]..[5] have kind 0x5B
  (water, kept by the floor probe's walker 001A2AE0, COLL_PROBES.md) and
  keys 0x21, 0x0B, 0x0A, 0x09 for 0019C6F0 (NM: key = the record's param;
  on clears / off sets the entry's bit 0x40000000). Entry 2 is a surface at
  y 191 over x 933..1033, z 823..918 (the room behind door [27]), entry 4 a
  surface at y 132 under it. [9] (0x825C70, C) calls 0019C6F0(0x21, 1) at
  init while flag 0x1E != 0xFF (measured: entry 2's bit 30 clear in a19b_02
  and a19c_01), runs its 560-frame sequence once flag 0x1E is 1 and calls
  0019C6F0(0x21, 0) when flag 0x1E is 0xFF (measured: bit 30 set in a19c_02
  and a19c_05). The route walked that room's floors (y 170, 159) after it.
- **Door [27]** (0x823580, C, room move id 2): Use facing -x from (1040.5,
  837.3) with bit 2 set: entry 2 (1022.2, 170, 832.5). A first try from
  (1045.1, 835.4), 11 units from the door's centre, started nothing
  (exploration).
- **Sub 1.** The ladder at z 859.5 (grid nodes 959 / 960, x 921.1..929.1,
  y 210 -> 357) is one of 00196970's circles (BM; decomp WORLD_GRAPH.md
  "00196970 ... sub 1 entries 8 / 7 / 6"): request 13 01 07 01 at y 314
  (a19c_06 f1460), sub 1 entry 7 at (925.1, 380, 852.1). Sub 1's entry-7
  platform (y 380, x 910..990, z 835..885) is railed (attribute 0x50 / 0x53)
  all round; door [52] (001BC350, BM, model 0x03, room move id 8, at (969.7,
  380, 859.3) in its south rail) is its exit: entry 1 (956.8, 370, 883.4).
  [34] (0x8279E0, C) starts script 0x82E090 for spawn entry 1 (D_00810702
  == 1) and at its end gives CC3 item 0x0B (001C4760(0xB, 1)) and sets
  D_0081081E (counter 0x46) = 0xFF; the script's op06 / op07 set flag 0x46
  (D_0081079E) 1 / 0xFF (measured a19c_07 f313 .. f565).

## 3. Route table

Frames `f` are trace frames; `c` is the main-loop counter 0x70003B64.

| Beat (folder) | Source | Frames | Counters | Presses | What happens |
|---|---|---|---|---|---|
| `a19c_00_bar840` | a19b_02 | 869 | 135407..136276 | Cross f291, f764 | the walkway west to the pad (1018, 223, 1033); Cross facing -z: hang (action 0x20 f294, 0x21 / +5 0x10 f337); the traverse south to the bar's end, the swing (action 0x28 f644); Cross f764; the flight south and the landing on the y-195 platform (1017.9, 195, 929.7) f828 |
| `a19c_01_bar841` | a19c_00 | 7837 | 136277..144114 | Cross f162, f232, f788 | onto the platform's pad box (y 198); Cross facing -x: hang (f235 / f278); the traverse west to x 890.7; Cross f788: the drop (action 0x23 f791); [6]'s script 0x82BA00 from f804, landing f814, 3B8D 2 f815..f7777, message pages f882..f7695; **flag 0x1D = counter 0x1D = 0xFF** f7778; control at (918, 195.1, 953) |
| `a19c_02_use7` | a19c_01 | 1550 | 144115..145665 | Cross f98 | north of [7] (913.7, 195.1, 928.8) facing -z; script 0x82BD90 f101 (3B8D 2 f102); flag 0x1E = 1 f104; counter 0x1E 3 f325, 4 f409, 5 f410, 3 f412; message f413..f808; fades f1051..f1243; **counter 0x1E = 0xFF** f1367; **D_00810854 = 0x04** f1368; **flag 0x1E = 0xFF** and control f1491 |
| `a19c_03_slide` | a19c_02 | 922 | 145666..146588 | Cross f118, f683, f731 | the room's pad facing +x, the bar east to its end (action 0x28 at f678 without a swing; the second Cross drops, action 0x23 f734, onto the pad box (1003.2, 198, 930.4) f760); north onto the slope: the slide (action 0x30, +5 0x1C f836) to the y-172 floor, (1003.6, 172, 976.7) |
| `a19c_04_walkway` | a19c_03 | 1082 | 146589..147671 | Cross f231, f852 | the stair and the ladder at z 1024 up to the walkway; east; the ladder at z 974 down to the y-190 platform (1070.5, 190, 964.7) |
| `a19c_05_door27` | a19c_04 | 1541 | 147672..149213 | Cross f1266, f1336 | off the y-190 platform's edge (action 0x0B / +5 5 f129) onto the y-170 step (landing f156); south along the attribute-0x39 ledge (the shuffle, action 0x2F / +5 0x1B, f361 at (1097.5, 170, 950) to f929 at (1095.5, 170, 868.8)); west to door [27]; Use facing -x (3B8D 2 f1339): **entry 2** (13 00 02 13) f1481, control (1022.2, 170, 832.5) |
| `a19c_06_ladder959` | a19c_05 | 1978 | 149214..151192 | Cross f584 | west along the floor behind door [27], the slope down to y 159, the stair up west to the y-210 floor, the ladder at z 859.5 facing -z (action 0x15 f587); a hit on the ladder at y 281 (f1131, health 90 -> 72); request 13 01 07 01 at y 314 f1460; **AREA19 sub 1 entry 7** f1524; control (925.1, 380, 852.1) |
| `a19c_07_door52` | a19c_06 | 624 | 151193..151817 | Cross f163 | door [52] from the north facing +z (3B8D 2 f166): **entry 1** (13 01 01 13) f308; [34]'s script 0x82E090: flag 0x46 1 f313, counter 0x46 1 f376, both 0xFF f565; control at (956.8, 370, 883.4) |

Main line a19b_02 -> a19c_00 .. a19c_07: 16,403 frames, counters
135407..151817.

## 4. Story bytes, health and items (measured)

| Byte | Before | After | Where |
|---|---|---|---|
| D_00810775 (flag 0x1D) | 1 | 0xFF | a19c_01 f7778 |
| D_008107F5 (counter 0x1D) | 3 | 0xFF | a19c_01 f7778 |
| D_00810776 (flag 0x1E) | 0 | 0xFF | 1 a19c_02 f104; 0xFF f1491 |
| D_008107F6 (counter 0x1E) | 1 | 0xFF | 3 f325, 4 f409, 5 f410, 3 f412, 0xFF f1367 (a19c_02) |
| D_00810854 (AREA19 lock byte) | 0 | 0x04 | a19c_02 f1368 |
| cell directory entry 2 (water y 191) | enabled | disabled (bit 30) | between a19c_01's and a19c_02's end snapshots ([9], section 2) |
| D_00810700..703 | 13 00 0A 13 | 13 01 01 13 | 13 00 02 13 a19c_05 f1481; 13 01 07 13 a19c_06 f1524; 13 01 01 13 a19c_07 f308 |
| D_0081079E (flag 0x46) | 0 | 0xFF | 1 a19c_07 f313, 0xFF f565 |
| D_0081081E (counter 0x46) | 0 | 0xFF | 1 a19c_07 f376, 0xFF f565 |
| player health (D_008104D0) | 90 | 72 | a19c_06 f1131 |

The scripts' captions and dialogue: entering [7]'s room starts a long scene
in which a wounded officer of the base introduces himself and talks to the
player; [7]'s Use starts a second, shorter scene with fades; after door
[27] the room's name appears as a caption (exploration screenshots).

## 5. Census

### 5.1 The twelfth level's blind spot, closed

`route_census.py run --segments a19bx --pass A19BX` replays a19b_00 under
the EXIT group's `OverlayCensusSession`: boot + AREA13 (id 10) armed at the
start, AREA13's remaining breakpoints swapped for AREA19's (id 16) at the
first frame boundary where the resident id changes. `a19bx-delta` writes
`a19b_area13_delta.json` and `a19b_swap_delta.json` (A19B and A19BX
together).

- The replay completed its checks: 1,795 frames (recorded 1,794), start
  counter 130855 (recorded 130850), 0 rows identical by counter (the
  replay's pad input is not the recording's); end words differing: player 0,
  globals 1, owners 177.
- The swap came after frame 183 (counter 131038), 55 AREA13 breakpoints
  removed, 49 AREA19 armed. The first AREA19 hit is at f484 (0x8250C0), so
  no AREA19 code ran before the swap was armed.
- **AREA13 code in a19b_00's AREA13 frames:** 8 AREA13 overlay functions
  (0x823580, 0x824BB0, 0x8266A0, 0x826850, 0x826FF0, 0x827150, 0x8293A0,
  0x8299E0), all hit at f1, **none new** (all ran in the eighth (a04b) or
  ninth (a13) level).
- **The four unmeasured AREA19 addresses:** 0x823580 ran (f485, door [27]'s
  behaviour; not new); 0x8293B0 ran (f486, a piece of 0x829370; not new);
  **0x824BE0 did not run in a19b_00** (its first run stays a19b_02 f1204);
  **0x826840 did not run in a19b_00** (it is in no level's run so far).
- One boot function ran only in this replay (0x1C2FF0; not new).
- **So the twelfth level's counts are exact, not lower bounds:** a19b 30
  new functions (14,944 bytes), the twelfth level 39 (18,004 bytes);
  `a19b_swap_delta.json` gives the same new set as `a19b_delta.json`.

### 5.2 The thirteenth level

**Tool** (`../Extermination/tools/route_census.py`, additive):
`run --segments a19c --pass A19C` replays each beat from its recorded source
snapshot with route_capture's own beat function and one-shot breakpoints on
every boot function and on the AREA19 overlay (id 16; sub 1 is the same
overlay, so the sub change needs no swap). `thirteenth-delta` runs
`chain_delta` against the first-level census, beat 15, the A01, A00, A01R,
A02, A04, A22, A01U and A06 passes, the eighth- to eleventh-level groups,
A13D, and a19b with both passes (A19B, A19BX). Output:
`../Extermination/build/s87/census/a19c_delta.json` (and `runs/A19C/`).

```sh
# decomp repo, .venv python, repo root (hidden PCSX2; hold build/.pcsx2.lock for `run`)
.venv/bin/python tools/route_census.py run --segments a19bx --pass A19BX
.venv/bin/python tools/route_census.py a19bx-delta
.venv/bin/python tools/route_census.py run --segments a19c --pass A19C
.venv/bin/python tools/route_census.py thirteenth-delta
```

**Replay fidelity.** All eight replays completed their beat's own checks
(`beats_incomplete` empty; a19c_05's first attempt stopped at its start
with a refused DebugServer connection and the retry is the kept run). Rows
are compared by the main-loop counter; the start offset is the replay's
first counter minus the recording's.

| Beat | Frames replay / recorded | Start offset | Rows identical by counter | End words differing (player / globals / owners) |
|---|---|---:|---|---|
| a19c_00 | 872 / 869 | 0 | 340 / 870 | 0 / 2 / 260 |
| a19c_01 | 7,838 / 7,837 | 1 | 272 / 7,837 | 0 / 2 / 318 |
| a19c_02 | 1,550 / 1,550 | 1 | 552 / 1,550 | 0 / 2 / 182 |
| a19c_03 | 921 / 922 | 0 | 3 / 922 | 0 / 2 / 420 |
| a19c_04 | 1,082 / 1,082 | 0 | 1,083 / 1,083 (exact) | 0 / 1 / 0 |
| a19c_05 | 1,541 / 1,541 | 0 | 1,542 / 1,542 (exact) | 0 / 1 / 0 |
| a19c_06 | 1,876 / 1,978 | 0 | 568 / 1,877 | 1 / 3 / 14 |
| a19c_07 | 624 / 624 | 0 | 625 / 625 (exact) | 0 / 1 / 0 |

The beats are closed-loop, so the other replays are different walks of
the same route shape (a19c_06's replay climbed the ladder in 102 fewer
frames); as for the earlier levels, the census sets are those of the route
shape, not of the exact frames.

**Totals.** 1,062 functions executed (1,033 boot, 29 of AREA19's 43); **36
new, 23,628 bytes**: 22 boot (byte-matched C 13, NEARMISS 7, asm word 2) and
14 AREA19 overlay (C 9, NEARMISS 5). No hit is unattributed and no
overlay hit came from another overlay.

New functions (census frames; the captions name the beat's event at that
frame, not the function's role):

| Function | Bytes | Status | Subsystem | First |
|---|---:|---|---|---|
| 00194240 | 620 | BM | init_io | a19c_00 f295 (recording: the hang begins f294) |
| 001818D0, 0016A4B0, 0016A8B0 | 128, 1,020, 916 | BM, NM, NM | actor_anim, entity_update (2) | a19c_00 f647, f648, f698 (recording: the swing begins f644) |
| 00181950, 00181A70 | 280, 268 | BM | actor_anim | a19c_00 f774 (recording: Cross f764, landing f828) |
| overlay 0x829840 | 132 | C | overlay_AREA19 | a19c_01 f8 |
| 00139240 | 1,960 | NM | entity_logic | a19c_02 f58 |
| overlay 0x824A90 | 328 | C | overlay_AREA19 | a19c_02 f405 ([7]'s script) |
| 00139E00 | 1,444 | NM | entity_logic | a19c_03 f207 |
| 001545B0, 00154F00 | 264, 676 | BM, asm word | entity_logic | a19c_03 f913 (near the beat's end, on the y-172 floor) |
| 0013A3B0 | 3,992 | BM | entity_logic | a19c_04 f1 |
| 001CB140, 001CB1F0, 001D42E0, 001D4430 | 164, 8, 332, 16 | BM, BM, NM, BM | anim_runtime (2), render_vif (2) | a19c_04 f9 |
| 001B34F0 | 140 | BM | math_vector | a19c_04 f113 |
| 001B2B80, 001B3250, 001B37D0 | 100, 156, 536 | BM, asm word, NM | math_vector | a19c_05 f1144 |
| 001F6AC0 / 00197390 | 16 / 244 | BM / NM | fx_render / init_io | a19c_06 f1721 / f1722 (after the sub change; 001AD010 at f1422 in this replay) |
| overlay 0x823780, 0x823D10, 0x826570, 0x826840, 0x826C10, 0x827430, 0x827550, 0x8279E0, 0x827B60, 0x829A70 | 1,312, 2,428, 716, 748, 2,072, 260, 564, 292, 612, 800 | NM, NM, C, C, NM, NM, C, C, C, NM | overlay_AREA19 | a19c_06 f1721 (sub 1's placements: [40], [39], [38], [37], [36], [35], [53], [34], the 0x827B60 objects, [46]) |
| overlay 0x827B10 | 16 | C | overlay_AREA19 | a19c_07 f376 |
| 001F6B90 / overlay 0x827B20 | 12 / 56 | BM / C | fx_render / overlay_AREA19 | a19c_07 f501 ([34]'s script, the callback 0x827B20) |

0x826840 ([37], sub 1) is the function the twelfth level could not measure
in a19b_00 (section 5.1: it did not run there); its first run is here.

To check a port translation, grep the port (`grep -rlI -i <address> src
tools`). Referenced there (whether as a translation or as a hook slot is
not judged here): 00194240 (em_area06_port_*); 001818D0, 0016A4B0,
0016A8B0, 00181950, 00181A70 (em_player_closure_10_12_19.c,
PLAYER_CLOSURE_10_12_19.md); 00139240, 00139E00, 0013A3B0
(em_level9_port_exit.c); 001545B0, 00154F00 (em_level9_port_exit.c,
em_enemy.c); 001CB1F0 (em_status_models.c); 001B3250 (em_level10_port_boot.c,
em_level8_port_creature.c); 001B37D0 (em_level8_port_creature.c,
em_level11_port_internal.h); 001F6AC0 (em_effect_manager.c); 00197390
(em_camera_leftovers.c). The AREA19 overlay addresses' port hits are other
areas' functions at the same addresses (em_startup_load_gaps.c,
em_security_gun_rest.c, em_examine.c and the AREA11 binding headers) or test
fixtures (test_area13_assets_reference.py; test_area19_assets_reference.py
names the callback 0x827B20 as a fixture); none is an AREA19 translation.
Not referenced: 001CB140, 001D42E0, 001D4430, 001B34F0, 001B2B80, 001F6B90.

AREA19 overlay functions run in no level so far (`already_ran` null in
`overlay_AREA19_not_run`): 0x823540 (the 4-byte entry pad), 0x823770,
0x823CA0, 0x826470, 0x826500, 0x826540, 0x826B30, 0x827540.

## 6. Tried and not kept (exploration runs, this session)

Scratch snapshots under `../Extermination/build/thirteenthcap/probe/`
(deleted at the session's end); leads, not ground truth.

- **The slope from below.** Walks up the slope from the y-172 floor (x
  1015 and x 1027) stopped at z 972.4 (the slope's foot is z 968); at x
  1015 a creature that rises from the floor there (a 00153F10 node at (1015.9,
  172, 974.7), spawned by the 0015A2C0 generator placement [55]) bit the
  player (action 0x3E, health 90 -> 85); at x 1027 a Cross facing -z
  started no climb and a bug on the player's back (action 0x3B) cost 10
  health.
- **The walkway's south rail** (x 970..994.9, attribute 0x50) stops a walk
  south at z 1029.9: no drop from the walkway into the pit.
- **Hits while hanging.** On one exploration traverse of the second bar the
  player was hit twice while hanging at its west end (health 90 -> 72 ->
  54 -> 36, 18 each, one hit while waiting there for ~880 frames); the kept
  traverse (a19c_01) was not hit. The source was not read (two 00156620
  placements, [16] and [17], stand on the y-230 floor above the room).
- **A Cross right after a snapshot resume** while hanging did not drop the
  player; the kept route uses `a19c_drop` (repeated presses).
- **Message pages.** The exploration pressed Cross every 46 frames through
  most of [6]'s scene: in those 5,167 frames the message record's page
  timer reset 47 times, 5 of them within 3 frames after a press (about what
  chance gives), and the scene (3B8D 2) lasted 6,962 frames, exactly as in
  the kept beat a19c_01, which presses nothing (f815..f7777). The kept beats
  press nothing during scripts.
- **Door [49]'s seal [48]** (001581A0, NM, class 0x44 model 0x0E at (980.7,
  377.7, 940.6), yaw 0; its hull was not read): the hall side of door [49] is a grid wall at z 939
  (node 2, x 893.8..989.5, y 370..450), which holds the player at z 934.5.
  From there the light melee (Circle, action 0x36), the light combo, the
  heavy melee (Square) from five positions and yaws, and gunfire in the R1
  (6 rounds, stick centred, up and down) and R2 stances (4 rounds) left its
  +0x36 at 0 and D_00810854 at 0x04 (the third level's padlock, the same
  behaviour, broke to the light melee from 5.75 units: THIRD_LEVEL_ROUTE.md).
  The seal's yaw 0 faces the tower's inside.
- **Sub 1's ladder down to sub 0 (x 848..862, z 844.7).** Its top lies in
  an enclosure (x 846..863.5, z 835..861) railed (attribute 0x50) on the
  east; a walk from the east stopped at x 868; the enclosure opens north
  under the lift [36]'s platform (section 7). Health 72 -> 62 -> 57 on the
  way (two hits near (906, 893) and (903, 846); 0012A5D0 creatures stand
  near the ladder).
- **Sub 1's valve [38] and the lift [36]** (from a19c_07's end): Use at
  [38] (890, 385, 836.2) from (890, 841.2) facing -z ran script 0x82CA20:
  counter 0x20 (D_008107F8) 1, then flag 0x20 (D_00810778) and counter 0x20
  0xFF (492 frames with 3B8D 2); about 900 frames later no node with the
  fire [39]'s behaviour 0x823D10 was in the pool (0x823D10 shrinks the fire
  once D_008107F8 != 0). A walk west onto the lift's platform
  ([36] at (835.5, 370, 878.5), rest height 370) stopped at x 873 from z
  862..877, and Cross facing -x there started no climb.

## 7. Open

1. **AREA15.** Sub 1's doors [50] (902, 450, 941) and [51] (902, 500, 941)
   (001BC350, area change to AREA15 entry 0 sub 0 / sub 1) open from the
   landings of the stair tower (x 897..990, z 942..985: flights of
   attribute 0x35 from y 370 to 500.5, landings at y 395.5, 421, 450,
   475.5, 500.5). The tower's door from the hall, [49] (model 0x15, lock bit
   3), carries the seal [48] (section 6). Leads (read, not played):
   - The tower's ground floor holds the top of the ladder from sub 0's
     y-265 deck (sub 1 grid nodes 145 / 146, x 900..913.9, z 950.7..960.7,
     inside the tower's walls); sub 0's ladder 694 / 695 (x 902.9..911, z
     960, y 265 -> 349) is 00196970's circle for sub 1 entry 8. So the
     tower is entered from the deck, and the seal faces the tower's inside.
   - Sub 1's ladder at x 848..862, z 844.7 goes down (y 370 -> 330); sub
     0's spawn entry 13 (855.1, 309.4, 851.3) is the top of sub 0's ladder
     776 / 781 to the deck's part x 814.7..911.8, z 825.9..865. How a
     descent of sub 1's ladder hands over to sub 0 was not read (00193EB0's
     bands at y 356 / 365, which request sub 0 entries 0xD / 0xB / 0xC,
     act only with D_00810701 == 0, i.e. in sub 0; its other case for area
     0x13 with D_00810701 != 0 sets its record's +6 = 0xD and requests
     nothing). The ladder's top enclosure opens under the lift [36] (0x826C10, C: the
     player in the box 850 < x < 859.6, 850.5 < z < 855 starts script
     0x82D290). How the deck's part at z 825..865 joins the part with
     ladder 694 (nodes 222 / 227, x 895..935, z 940..980) was not read.
   - Sub 1's hall also holds the valves [37] / [38] (0x826840 / 0x826570,
     C). [38]'s script ends the fires (flag 0x20, D_008107F8; played in
     exploration, section 6). [37]'s Use sets the lift [36]'s +0x2EC to
     0x8A, which [36] counts down before moving 15 units over 180 frames
     (read); [37] (819, 385, 836.2) is on the hall's north wall west of
     the box x 828..846 beside the ladder's enclosure (not tried). Item
     0x24's giver
     [53] (0x827550, C) stands in the tower at y 473.8 (it acts for 445 <=
     y <= 460 and needs flag 0x25). Decomp WORLD_GRAPH.md section 7 steps
     5 / 6: AREA15's sub-0 / sub-1 events.
2. **The zipline.** Sub 0's catwalk (y 290) has an attribute-0x20 pad under
   an attribute-0x3C cable (y 317.8 -> 300.8, z 931.3 -> 790.6); 0015D4C0's
   case 0x20 needs D_00810C7C (item 0x18, held 0); [18] (0x826100, C) rides
   it to z 796.6 (flag 0x1F). Where item 0x18 comes from was not found.
3. **The hits** on the ladder at z 859.5 (a19c_06 f1131) and while hanging
   (section 6): sources not read.
4. Memory cards: as TWELFTH_LEVEL_ROUTE.md section 7 item 7. After this
   lane's last session the shared install's `inis/PCSX2.ini` and
   `portable-data/memcards/Mcd001.ps2` / `Mcd002.ps2` carry mtimes
   2026-10-01 23:46 (PCSX2's shutdown); no in-game save was made, but that
   the cards' content is unchanged is not proven.
