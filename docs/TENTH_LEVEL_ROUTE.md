# AREA19 entry 9 and back to AREA04: tenth-level route, original ground truth

Lane A19CAP (capture), 2026-10-01 (session s90). Original executable
SHA-256 `ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

`NINTH_LEVEL_ROUTE.md` ends with the player in control at the foot of
AREA19's entry-9 ladder (a13_05, (710, 240, 1256.8)). The decomp's
`docs/WORLD_GRAPH.md` section 7 step 5 inferred that AREA19 leads on to
AREA03 (door [25], opened by the panel [24] with item 0x1B) or, from its
sub-state 1, to AREA15. This document records the original from a13_05's
end as two opt-in beat groups of `../Extermination/tools/route_capture.py`:
`a19` (AREA19's entry-9 rooms) and `a13b` (back up into AREA13 and down its
lift [10] to AREA04). Hidden PCSX2, exact one-frame steps, pad input only,
no teleports and no memory writes, one trace and one resumable snapshot per
beat. Nothing here is port behaviour.

**Status.**

- **The entry-9 rooms are a closed pocket on this route.** The ladder room,
  the duct to the room behind door [32] and that room were played (a19_00
  .. a19_02). Their one other door, [22], needs item 0x24 at the reader
  [21] (code, section 2); D_00810C88 (item 0x24) is 0. The panel [24], door
  [25] (to AREA03), door [27] and the ladders to sub-state 1 (to AREA15)
  lie in the parts of AREA19 behind entries 0, 1 / 2 and 10, which the
  entry-9 rooms do not reach.
- **The story's way on: probably AREA19 entry 10, not reached.** Door [27]
  opens only with AREA19's lock bit 2, which [7] sets at the end of the
  sequence that starts only when the spawn entry byte D_00810702 is 0xA
  (code, section 2). That 0xA means an arrival through AREA13's hatch [63]
  (00193EB0, the other fall) is **inferred**: D_00810702 also changes
  inside an area (9 -> 8 in the duct, section 2), no AREA19 door record
  moves to entry 10, but the other in-area room moves were not enumerated
  (section 2). The hatch [63] room is entered only through AREA13's door
  [20] from its north side; no way there was found on foot from AREA13's
  field (section 6).
- **Played instead: the way back.** The AREA19 ladder up into AREA13 (entry
  6), out through door [17], round to door [14] and the lobby, and the lift
  [10] down to **AREA04 entry 7** (a13b_00 .. a13b_05): control at (570.7,
  54.9, 244.6). Side beat a13b_s0: the AREA13 roof ladder.
- **The player did not die.** Health 44 throughout (item 0x1A stops the
  outdoor drain, section 4); a bite in the lift car raised the infection
  80 -> 90 (a13b_05).
- Groups: a19 3 beats, 3,616 frames, counters 111049..114667 (contiguous);
  a13b 6 main beats, 4,664 frames, within counters 114668..119358 (21 of
  those counters fall between beats and are not recorded, section 3);
  a13b_s0 889 frames, counters 116115..117004.
- The census count (section 5: 18 new functions, 8,640 bytes) is a
  **lower bound**: the AREA19 frames of a13_05 (ninth level) and of
  a13b_00 (f1..f425) were replayed with only AREA13's overlay armed, and
  seven AREA13 functions could not be seen in a13b_00's AREA13 frames
  (section 5.1, measured gaps).

In-game text (messages, prompts, item names) is described, never quoted.
Positions are world units; yaw is the player's +0xC4 (X = sin, Z = cos).

## 1. Tool, outputs and conventions

```sh
# decomp repo, .venv python, repo root
.venv/bin/python tools/route_capture.py run --beats a19,a13b      # both groups, in order
.venv/bin/python tools/route_capture.py run --beats a13b_s0        # the side beat
.venv/bin/python tools/route_capture.py events --beats a19,a13b
.venv/bin/python tools/route_capture.py verify --beats a19,a13b
```

`--beats all` and every earlier group are unchanged. a19_00 starts from
`build/s87/route_a13/a13_05_shaft/state.p2s`, a13b_00 from a19_02's
snapshot, a13b_s0 from a13b_01's; each other beat from the previous beat's
snapshot. Outputs: `../Extermination/build/s87/route_a19/<beat>/` and
`route_a13b/<beat>/` (ignored): `trace.json`, `state.p2s`, `eeMemory.bin`,
`gs.bin`, `scratchpad.bin`, `original.png`, `snapshot.json`. Every snapshot
was reloaded after its capture (`run_beat`'s check); no beat needed a
tail. The capture and census runs of this lane were started through a
scratch wrapper that holds the shared PCSX2 lock and raises the
DebugServer / Pine socket timeouts from 5 s to 60 s in its own process
(the host was saturated by other lanes' workers; a 5 s status call then
timed out mid-step); the tools themselves are unchanged in that respect.

- **Rows**: the NINTH_LEVEL_ROUTE.md row (decode_a13) plus, in AREA19,
  `s778` / `s7f8` (flags / counters 0x20..0x23), `s79c` / `s81c` (0x44..0x47),
  `l854` (D_00810854, AREA19's lock byte) and `ca8` (the halfword
  D_00810CA8). The a13b rows use the a13 decoder with their own owners.
- **Helpers**: the a13 helpers (`a13_settle`, `a13_face`, `a13_use`,
  `a13_control`), the AREA01 duct helpers (`a01_crawl`, `a01_turn_crawl`),
  `a19_press_until` (Cross after 3 idle frames, retried) and `a19_page`
  (a status page: 90 frames, then Triangle).

### 1.1 Owners sampled

AREA19 (`A19_OWNERS`, `tools/area_overview.py --area 19 --ram
build/s87/route_a13/a13_05_shaft/eeMemory.bin`; placement table 0x82E3D0
[n], deferred group 0x829E00 g[n]):

| Row key | Node | Behaviour | Record |
|---|---|---|---|
| `r6_8250F0` | 0x7AD490 | overlay 0x8250F0 at (956, 210, 1006.5) | [6]: D_008107F5 |
| `r7_8257C0` | 0x7AD780 | overlay 0x8257C0 at (913.7, 198.2, 919.6) | [7]: entry 0xA, D_008107F6 |
| `r10_827790` | 0x7AE050 | overlay 0x827790 (class 9) | [10]: entry 0xD |
| `reader_r21` / `door22_r22` | 0x7B00A0 / 0x7B0390 | 00158810 model 0x13 / 001BB860 model 0x16 | [21] (770, 235, 1044) / [22] (771.5, 220, 1035.8) |
| `panel_r24` / `door25_r25` | 0x7B0970 / 0x7B0C60 | 00158EC0 model 0x22 / 001BB860 model 0x16 | [24] (1013, 280, 1126.9) / [25] (1022.5, 265, 1128.3) |
| `door27_r27` / `door32_r32` | 0x7B1240 / 0x7B20F0 | overlay 0x823580 / 001BC350 model 0x03 | [27] (1034.2, 170, 837.5) / [32] (739.5, 200, 1081.8) |
| `r33_159970` | 0x7B23E0 | 00159970 model 0x37 | [33] (717.5, 212.8, 1055.5) |
| `pick_g2` / `pick_g12` | 0x7A5C20 / 0x7A7980 | 00219550 / 0015AFA0 | g[2] (750.3, 200, 1051.8) / g[12] (649.4, 231.2, 1143.4) |
| `g41_1C1030` | 0x7ACEB0 | 001C1030 model 0x51 | g[41] (735, 260, 1209.3) |

AREA13 again (`A13B_OWNERS`, measured on the first snapshot of this
load): [3] and [4] free themselves at the load (flags 0x19 / 0x1A are
0xFF), so every later placement sits lower in the pool than in the a13
load: door [8] 0x7AB730, lift [10] 0x7ABD10, buttons [12] 0x7AC2F0 / [13]
0x7AC5E0, door [14] 0x7AC8D0, buttons [15] 0x7ACBC0 / [16] 0x7ACEB0, door
[17] 0x7AD1A0, door [20] 0x7ADA70, [44] 0x7B20F0, hatches [62] 0x7B4FF0
(open, state 2) / [63] 0x7B52E0.

## 2. What decides each step (code, then capture)

- **The duct** (the a01_s5 kind): the ladder room (x 697..745, z
  1225..1270, y 240) and the room behind door [32] (x 715..770, z
  1020..1090, y 200) are joined by a duct whose entry square is a grid
  floor of attribute 0x37 (x 697..705, z 1220..1230). Cross facing -z there
  starts the crawl (action 0x2C, then 0x2D with a camera fade, a19_00
  f172..f318); the stick up crawls; the stick's x turns in 90-degree steps.
  The crawl runs south to (703, 220, 1068); turned east it leaves through
  an exit square into the room (action 0x2E, a second fade). Inside the
  duct the spawn entry byte D_00810702 changes from 9 to 8 (f1453), and
  back to 9 on the way north (a19_02 f1390). A walk south on the ramp
  corridor east of the duct stopped at (731.7, 242, 1217.4) (exploration,
  scratch deleted, not kept as evidence). The review's reachability scan
  over a19_00's captured collision (scratch, a lead) reaches door [32]'s
  north side (entry 7, y 200) from the ladder room, so that stop is not a
  proof that the corridor is closed. Door [32] (001BC350, room move id 5:
  entries 8 / 7; model 0x03, for which 001BC350 tests no lock, decomp
  WORLD_GRAPH.md section 3) was never opened.
- **The pickup g[2]** (00219550, its record +2 = 0x84): Cross facing it
  from (744.7, 1057.7): its Use (3B8D = 3, then action 0x41, the Use action),
  D_00810CA8 0 -> 12 and the status request 01 12 with its page (a19_01
  f194; Triangle f287).
- **The reader [21] and door [22]** (not played; the platform at y 220
  east of the room). The earlier "a walk toward it stopped at (744.8, 200,
  1057.8)" is withdrawn: that is a19_01's end, the g[2] Use spot (744.7,
  200, 1057.7), and no walk from there toward the reader is kept. The
  review's reachability scan (scratch, a lead) reaches the y-220 platform
  with the reader [21] and entry 5 from this room, so the refusal is
  probably playable; it was not played. Door [22] is a slider (001BB860 model 0x16) that tests
  D_00810854 bit 0 (decomp WORLD_GRAPH.md section 3); the reader [21]
  (00158810, NM) sets that bit after its Use script; 001576E0 (BM) starts
  the accepting script for model 0x13 only when D_00810C88 (item 0x24) is
  non-zero, else the refusal script with line 0x80000016. D_00810C88 = 0 on
  this route. Item 0x24 is given only by AREA19 sub-state 1's [53]
  (0x827550, C), present only while flag 0x25 is set (AREA07 / AREA08).
- **Door [27] and [7]** (C, decomp docs/AREA19_OVERLAY.md): door [27]
  (0x823580) takes its open path only with bit (+0x34 = 2) of D_00810854;
  without it its Use plays script 0x82AD50 and gives CC3 0x0A once. The
  only writer of bit 2 is [7]'s 0x825930 (counter 0x1E = 0xFF at the end
  of script 0x82BD90), which runs only after 0x825AB0 has set D_008107F6 =
  1, and 0x825AB0 starts its first script only when D_00810702 == 0xA
  (code). From another area, 00193EB0 (NM) sends the player to AREA19
  entry 0xA from AREA13 entry 5 / 7 (AREA13's hatch [63]) (code).
  **Inferred, not read:** that this arrival is the only way D_00810702
  becomes 0xA. D_00810702 is a room tracker that also changes inside an
  area: 001AD010's room-move path stores D_008106B7 into it when
  D_008106B8 == 2, measured 9 -> 8 in the duct (a19_00 f1453). AREA19's
  door table (`tools/area_overview.py --area 19`) has no room move to
  entry 10 (its room moves give entries 0..8); the other in-area room
  moves (duct exit squares and similar, whatever sets D_008106B7 in
  AREA19) were not enumerated. [6]'s trigger areas (0x82BD00: x
  1055..1085, z 975..990, y >= 210; 0x82BD40: x 885..933, z 912..959, y
  190..200) are in those parts too.
- **The ladder up** (00196970, BM): its five circles of radius 8 are the
  ladders at AREA19 entries 9, 11, 12, 10 and 13. At entry 9's foot, Cross
  facing +z: action 0x15, then 0x17 (a13b_00 f6, f65); the stick up
  climbs; at y >= 284.5 inside the circle (710, 1266.1) the request 0D 00
  06 01 (f362): AREA13 entry 6 (720, 143.1, 1259.6), the shaft under the
  open hatch [62] (D_00810700..703 = 0D 00 06 0D at f433). The player then
  climbs out on his own: action 0x43 (3B8D = 4, f728), 0x18 (f730),
  control at (720, 160, 1252.3) facing -z (f824).
- **Door [17] from inside** (0x823580, room move id 3, record 09 04): the
  grating step east (ledge climb facing +x, action 8, a13b_01 f88..f153),
  Use facing +x at (772.5, 1263): taken f337, entry 9 (785, 160, 1262)
  f499. Flag 0x1C stayed 0, so it was not refused.
- **Door [14]'s button [15]** (001BD9F0 model 3, latch 1): Use facing +z
  at (665.7, 1151.5): taken f379 (a13b_02), room move id 2 to entry 3
  (688, 160, 1174.1) f711.
- **Door [8]** (001BC350, room move id 1) from its south side facing +z:
  entry 1 (664.7, 160, 1221.1), the lift lobby (a13b_03 f258).
- **The lift [10]** (001BD560 model 0x0B, door id 0x80: AREA04 entry 7)
  and its buttons (001BC960, id 0xFF: no lock bit; decomp WORLD_GRAPH.md
  section 3). At this load the lift's +0x0B is 0. The outer [12] (model 0,
  at (647.7, 175, 1238.9), yaw +pi/2) took Use facing -x at (652.7,
  1238.6) (a13b_04 f213); after its script the lift's +0x0B went 0 -> 2
  (f424) and, the car having come, 3 (f536). The car's opening is on its
  east side at z about 1258; the inner [13] (model 1, at (644.7, 175,
  1276.1), yaw -pi/2) took Use facing +x (a13b_05 f435): `a13_use` aims
  at (639.7, 1276.1), the walk stopped at (639.5, 1271.6) (f425..f434),
  and in the frame the Use was taken the player's position became (639.7,
  160, 1276.4); the lift's script ran (f754..f833), the request 04 FF 07 01
  (f895: AREA04, sub from D_00810730[4], entry 7) and D_0081083E 1 -> 0;
  AREA04 entry 7 loaded (f966), a script ran at the arrival (3B8D = 2,
  action 0x41, f1279; at AREA04 entry 7 the inner button 001BC960 starts
  one, decomp WORLD_GRAPH.md section 3), control at (570.7, 54.9, 244.6) facing -x
  (f1538).
- **The bite** (a13b_05 f365): inside the car, action 0x3E (A04_GRABS) and
  the infection 80 -> 90; `a13_use`'s shake loop freed the player.

## 3. Route table

Frames `f` are trace frames; `c` is the main-loop counter 0x70003B64.

| Beat (folder) | Source | Frames | Counters | Presses | What happens |
|---|---|---|---|---|---|
| `a19_00_duct` | a13_05 | 1623 | 111049..112672 | Cross f169 | west to the duct square, crawl (0x2C f172, 0x2D f318) south and east out (0x2E f1518): the room behind door [32], D_00810702 = 8 (f1453) |
| `a19_01_pickup_g2` | a19_00 | 343 | 112673..113016 | Cross f134; Triangle f287 | the pickup g[2]: D_00810CA8 0 -> 12 and its page f194 |
| `a19_02_duct_back` | a19_01 | 1650 | 113017..114667 | Cross f114 | into the duct from its exit square facing -x, west, north and out into the ladder room (D_00810702 = 9, f1390); the ladder's foot |
| `a13b_00_ladder_up` | a19_02 | 893 | 114668..115561 | Cross f3; stick up | the ladder (0x15 f6, 0x17 f65), request 0D 00 06 01 f362, **AREA13 entry 6** f426, out of the hatch [62] (0x43 f728, 0x18 f730), control f824 |
| `a13b_01_door17` | a13b_00 | 549 | 115564..116113 | Cross f85, f334 | ledge climb onto the grating step (action 8 f88), door [17] facing +x: taken f337, **entry 9** (outside) f499 |
| `a13b_02_button15` | a13b_01 | 761 | 116114..116875 | Cross f376 | outside to door [14], its button [15] facing +z: taken f379, **entry 3** f711 |
| `a13b_03_door8` | a13b_02 | 308 | 116876..117184 | Cross f113 | door [8] facing +z: taken f116, **entry 1** (the lobby) f258 |
| `a13b_04_lift_call` | a13b_03 | 546 | 117185..117731 | Cross f210 | the outer button [12] facing -x: taken f213, the lift's +0x0B 0 -> 2 f424 -> 3 f536 |
| `a13b_05_lift_ride` | a13b_04 | 1607 | 117751..119358 | Cross f432 | into the car (a bite f365: infection 80 -> 90), the inner button [13] facing +x: taken f435, request 04 FF 07 01 f895, **AREA04 entry 7** f959, its arrival script f1279, control f1538 |
| `a13b_s0_roof_ladder` (side) | a13b_01 | 889 | 116115..117004 | Cross f280; stick up | outside to the building's south-east corner, the roof ladder (0x15 f283, 0x17 f342, 0x18 f601) to the roof (y 215) f695; east on the roof to the railing at (773.2, 215, 1226.4) |

Main line a13_05 -> a19_00 .. a19_02 -> a13b_00 .. a13b_05: 8,280 frames,
within counters 111049..119358. Every snapshot was reloaded after its
capture and resumed on the first check (no tail idle). The counter gap
between a beat's last row and the next beat's first row is the snapshot
reload: 1 (no counter missing) for a13_05 -> a19_00 -> a19_01 -> a19_02
-> a13b_00 and a13b_01 -> a13b_02 -> a13b_03 -> a13b_04; 3 for a13b_00 ->
a13b_01 (counters 115562..115563 not recorded); 20 for a13b_04 -> a13b_05
(117732..117750 not recorded, the largest gap of any route group so far;
earlier groups mostly 1 to 3, at most 12); 2 for the side beat a13b_01 ->
a13b_s0 (116114 not recorded in that branch). So the main line records
8,280 + 9 = 8,289 of the 8,310 counters 111049..119358; 21 counters were
never recorded (all of them inside the a13b group; the a19 group is
contiguous). Why the reload before a13b_05 took 20 counters is not
measured.

## 4. Story bytes, health and items (measured)

| Byte | Before | After | Where |
|---|---|---|---|
| D_00810700..703 | 13 00 09 13 | 04 00 07 04 | 13 00 08 13 a19_00 f1453, 13 00 09 13 a19_02 f1390, 0D 00 06 13 a13b_00 f426, 0D 00 06 0D f433, 0D 00 09 0D a13b_01 f499, 0D 00 03 0D a13b_02 f711, 0D 00 01 0D a13b_03 f258, 04 00 07 0D a13b_05 f959, 04 00 07 04 f970 |
| D_00810CA8 (halfword) | 0 | 12 | a19_01 f194 (the pickup g[2]) |
| D_0081083E (counter 0x66, read by 001BC960) | 1 | 0 | a13b_05 f895 (the lift's request) |
| D_00810854 (AREA19 lock byte) | 0 | 0 | |
| D_00810C88 (item 0x24) | 0 | 0 | the reader [21]'s key, never held |
| flags 0x1D..0x21, counters 0x1D..0x21 (D_00810775..79, D_008107F5..F9) | 0 | 0 | no AREA19 story event ran |
| player health (+0x220) | 44 | 44 | |
| infection (+0x228) | 80 | 90 | a13b_05 f365 (a bite in the lift car) |

**The cold.** With item 0x1A held (D_00810C7E = 1 since a13_02) the
outdoor health drain stops: 0015D100 (C, `src/func_0015D100.c`) returns
before its 360-frame counter while D_00810C7E != 0. Measured: the health
stayed 44 through 2,089 outdoor frames (exploration) and through
a13b_01..a13b_02 and a13b_s0. NINTH_LEVEL_ROUTE.md section 7 item 3 is
answered.

## 5. Census: what the tenth level executes beyond every earlier level

**Tool** (`../Extermination/tools/route_census.py`, additive):
`run --segments a19 --pass A19` and `run --segments a13b,a13b_s0_roof_ladder
--pass A13B` replay each beat from its recorded source snapshot with
route_capture's own beat function and one-shot breakpoints on every boot
function and on the group's overlay (`TENTH_GROUPS`: a19 arms AREA19,
overlay id 16; a13b arms AREA13, id 10). `tenth-delta` runs `chain_delta`
(SEVENTH_LEVEL_ROUTE.md section 9) against the first-level census, beat
15, the A01, A00, A01R, A02, A04, A22, A01U and A06 passes, the four
eighth-level groups, the ninth-level group A13 and, for a13b, a19. Output:
`../Extermination/build/s87/census/a19_delta.json`, `a13b_delta.json` (and
`runs/A19/`, `runs/A13B/`). a13b_05 is a change beat: its hits from
001AD010's frame on (census f1009) are its `a13b_exit` segment (the AREA04
load at entry 7).

```sh
# decomp repo, .venv python, repo root (hidden PCSX2)
.venv/bin/python tools/route_census.py run --segments a19 --pass A19
.venv/bin/python tools/route_census.py run --segments a13b,a13b_s0_roof_ladder --pass A13B
.venv/bin/python tools/route_census.py tenth-delta
```

**Replay fidelity.** All ten replays completed their beat's own checks
(`beats_incomplete` empty): a19_00 1,594 frames against 1,623 (3 of 1,595
rows identical), a19_01 336 / 343 (0), a19_02 1,650 / 1,650 (0 of 1,651),
a13b_00 893 / 893 (0), a13b_01 549 / 549 (0), a13b_02 761 / 761 (762 of 762
identical), a13b_03 308 / 308 (309 of 309), a13b_04 546 / 546 (547 of 547),
a13b_05 1,657 against 1,607 (0), a13b_s0 889 / 889 (890 of 890). As for the
earlier levels, the sets are those of the same route shape, not of the
exact frames.

**Totals.**

| Group | Executed | New | Bytes | Boot | Overlay | Status of the new (census) |
|---|---:|---:|---:|---:|---:|---|
| a19 | 741 (727 boot, 14 AREA19) | 13 | 7,000 | 1 | 12 | boot NM 1; overlay C 12 |
| a13b (main) | 968 in all (948 boot, 20 AREA13) | 4 | 368 | 3 | 1 | boot BM 3; overlay C 1 |
| a13b_exit (the AREA04 load at entry 7) | | 1 | 1,272 | 1 | 0 | NM 1 |
| a13b_s0 (side) | | 0 | 0 | 0 | 0 | |

Tenth level in all: **at least** 18 new functions, 8,640 bytes (a lower
bound; section 5.1 lists the frames and functions this census could not
see). No unattributed hit. 23 hits in a13b came from other overlays at
addresses armed for AREA13: 13 from AREA19 code (overlay id 16) at
a13b_00 f1..f2 and 10 from AREA04 code (overlay id 5) at a13b_05 f1328..f1329
(a13b_exit); they are not counted, and AREA04's own functions are not in
this census. Two AREA19 functions (0x824690, [11]'s behaviour, and 0x827DD0, the
AREA01-twin group object) count as already run in the ninth level: a13_05's replay hit
AREA19 code at its first frames through addresses armed for AREA13
(NINTH_LEVEL_ROUTE.md section 5).

### 5.1 Measured gaps (why the count is a lower bound)

The census arms one overlay per group and re-arms its one-shot breakpoints
only at a label; every beat here is one label (a change beat adds a second
label from 001AD010's frame on). So:

1. **AREA19 code in a13b_00, f1..f425.** AREA19 stays resident until the
   AREA13 entry-6 load (the area field changes at f426), but a13b_00 is not
   in `A13B_CHANGE_BEATS` (only a13b_05 is) and group a13b arms AREA13's
   addresses only. AREA19 code reached 13 of those addresses at f1..f2
   (`a13b_delta.json`, `overlay_hits_other_overlay`, id 16); mapped by range
   they lie inside AREA19 functions that the a19 replays ran anyway
   (0x823580, 0x824690, 0x827DD0, 0x829370, 0x8298D0). Any AREA19 function
   that ran only during the climb and 00196970's request (f3..f425) at an
   address that is not an armed AREA13 address was **not measured**.
2. **AREA19 code in a13_05** (the ninth level's pass A13, segment a13_exit
   from census f176 on: the AREA19 load and the climb down the entry-9
   ladder) armed AREA13 only (NINTH_LEVEL_ROUTE.md section 5). The init
   0x8250C0, which boot 001E7780 calls at the AREA19 load (decomp
   docs/AREA19_OVERLAY.md), falls there; whether it and any other AREA19
   function that ran only in that arrival ran is **not measured**. That is
   why 0x8250C0 appears in the not-hit list below.
3. **Seven AREA13 functions in a13b_00, f426..f893.** The 13 AREA13
   addresses spent by AREA19 code at f1..f2 were not armed again after the
   AREA13 load in that beat. Behind them are seven AREA13 functions that
   ran in no earlier level and in none of a13b_01..a13b_05 / a13b_s0:
   0x8246D0 (224 bytes), 0x8247B0 (268), 0x8248C0 (148), 0x824960 (136;
   pieces 0x824960 / 0x8249A0), 0x8249F0 (132; pieces 0x8249F0 / 0x824A30),
   0x827DD0 (44) and 0x827E00 (288), 1,240 bytes in all. Whether they ran
   in a13b_00's AREA13 frames is **not measured**; they are in
   `overlay_AREA13_not_run`. (The other four spent addresses, 0x823580,
   0x824A80, 0x8293A0 and 0x8299E0, are AREA13 functions that ran in
   earlier levels.)

Closing the gaps needs a replay of a13b_00 (and of a13_05) with AREA19
armed up to the load and AREA13 re-armed after it, for example by listing
a13b_00 as a change beat with AREA19 armed in its first label; not done in
this lane.

**Overlay statuses** are read from the working tree at the time of the run
(decomp b81e50b plus uncommitted work of other lanes); AREA19's statuses
match decomp docs/AREA19_OVERLAY.md (36 C, 6 NEARMISS, 1 asm pad).

New functions (census frames):

| Function | Bytes | Status | Subsystem | First |
|---|---:|---|---|---|
| overlay 0x823580, 0x8250F0, 0x825240, 0x8255D0, 0x8257C0, 0x825AB0, 0x825C70, 0x825EE0, 0x826100, 0x827790, 0x829370, 0x8298D0 | 492, 336, 468, 492, 356, 440, 624, 540, 868, 592, 1,232, 408 | C (byte-identical at link) | overlay_AREA19 | a19_00 f1 (the first replayed frame: door [27], [6] and its area test, [43], [7] (state 1 sub-state 0, entry test), [9], [47], [18], [10], the AREA01-twin group objects) |
| 00118790 | 152 | NM | lowmem | a19_00 f1389 (inside the duct) |
| 001885F0 | 28 | BM | actor_anim | a13b_00 f364 (the ladder, at the request) |
| overlay 0x8236E0 (AREA13) | 32 | C | overlay_AREA13 | a13b_00 f727 (the AREA13 entry-6 arrival) |
| 001833F0 | 72 | BM | actor_anim | a13b_00 f729 (climbing out of the hatch) |
| 001E6F60 | 236 | BM | weapon_equip | a13b_02 f419 (door [14]'s button [15]) |
| 001305B0 | 1,272 | NM | entity_logic | a13b_05 f1615 (a13b_exit: AREA04's first frames) |

The captions in the First column name the beat's event at that frame, not
the function's role. To check a port translation of any of these, grep the
port (`grep -rlI -i <address> src tools`): 001305B0, 001833F0, 001885F0 and
001E6F60 are referenced there (em_level8_port_creature.c,
em_player_stage_live.c, em_player_ladder_climb.c, em_level9_port_fx.c); the
AREA19 overlay functions and 00118790 are not (the overlay addresses that
grep finds belong to other areas' overlays).

AREA19 overlay functions not hit in the a19 replays (29): 0x823540 (no
code), 0x823770, 0x823780, 0x823CA0, 0x823D10, 0x824A90, 0x824BE0,
0x824EF0, 0x824F70, 0x825030, 0x8250C0 (the init, before the load), 0x825420,
0x825930, 0x826470, 0x826500, 0x826540, 0x826570, 0x826840, 0x826B30,
0x826C10, 0x827430, 0x827540, 0x827550, 0x8279E0, 0x827B10, 0x827B20,
0x827B60, 0x829840 and 0x829A70 (sub-state 1's behaviours and the scripts'
callbacks of events that did not run). "Not hit" means not hit in the a19
replays; for 0x8250C0 and any function that runs only while climbing the
entry-9 ladder see section 5.1.

## 6. Tried and not kept (exploration runs, this session)

Scratch snapshots under `../Extermination/build/a19cap/probe/` (deleted at
the session's end); leads, not ground truth.

- **AREA13's east side (door [20], hatch [63]).** From the outdoor field
  (door [14] / door [17] side) walks toward door [20]'s north side
  stopped: at (795.4, 1013.7) and (749.5, 1022.2) against the pipes south
  of the field (cell-world walls 40 units high, y 160..200, along the
  pipes from (652, 1088) to (761, 1008) and beyond); at (826.3, 1020.4)
  and (797.7, 1103.7) against walls east of the field; at (796.4, 1247.1)
  and (784.3, 1269.3) east and north of door [17]'s porch. Standing on a
  pipe (y 168), walks south slid along it. Cross facing a pipe started no
  climb.
- **The roof.** The roof ladder (a13b_s0) leads to the roof at y 215; a
  walk east stopped at a railing (an exploration run stopped at (773.7,
  215, 1224.6); the kept beat a13b_s0 ends at (773.2, 215, 1226.4), its
  measured end snapshot); Cross there
  examined something (a status page, request 00 38) and started no climb.
- **[44]** (overlay 0x823E90, AREA13 (798.4, 215, 1149.5)): its step 1
  (0x824180, C) has two Use paths: without the node's +0xB bit 0 an
  examine script (0x82B090) whose end stores D_008106B0 = 1 and
  D_008106B1 = 0x84 (+0x34 + 0x80; the meaning of those bytes is not
  read); with it script 0x82B2D0 and flag 0x1C = 1 (its event). Not
  tried.
- **A collision reachability scan** (scratch, over the captured grid and
  cell worlds; a lead, not evidence): it joins door [20]'s side to the
  field only downhill, by a drop off the slope wall near (830, 1160), and
  the field to the roof only by the roof ladder.
- **The entry-2 room** (inside, entries 2 / 3): Cross at (698.9, 1183.6)
  examined [22] or [61] there (a status page, request 03 3D).
- **Host load.** Twice a session lost the frame boundary after a status
  page (DebugServer status calls timed out under a load average above
  150); the wrapper in section 1 avoids it.

## 7. Open

1. How the story reaches AREA13's door [20] / hatch [63] (AREA19 entry 10),
   which [7]'s sequence and door [27] wait for. Leads: [44]'s second Use
   path (+0xB bit 0) and its event, a drop from a higher part of the
   field, or a way from the roof not found.
2. The reader [21] / door [22] refusal was not played: no walk from the
   room behind door [32] toward the platform at y 220 is kept, and the
   review's reachability scan (a lead) says the platform is reachable from
   that room. Its gate is read from 001576E0 (BM). Door [32] (no lock,
   model 0x03) was not opened either.
3. AREA19 past its entry-9 rooms (decomp WORLD_GRAPH.md section 7 step 5,
   revised in section 8c) is not recorded.
4. The next steps from AREA04 entry 7 with this state (bit 2 of
   D_00810845 still 0, no item 0x29) are not derived.
