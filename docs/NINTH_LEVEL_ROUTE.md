# AREA13 to AREA19: ninth-level route, original ground truth

Lane A13CAP (capture), 2026-09-30 (session s89). Original executable
SHA-256 `ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

`EIGHTH_LEVEL_ROUTE.md` ends with the lift [51] leaving AREA04 and the
player in control at **AREA13 entry 0** (a04b_04_lift). The decomp's
`docs/WORLD_GRAPH.md` section 7 step 4 says AREA13's only exits are the lift
back and the falls into AREA19 (00193EB0), reached through the room moves
and the holes [5] / [6], which need item 0x27. This document records the
original from a04b_04's end along that order as the opt-in beat group `a13`
of `../Extermination/tools/route_capture.py`. Hidden PCSX2, exact one-frame
steps, pad input only, no teleports and no memory writes, one trace and one
resumable snapshot per beat. Nothing here is port behaviour.

**Status.**

- **AREA13 played from its arrival to hole [5].** Out of the lift car, the
  lobby, door [8] (entry 2) with [3]'s scene (a13_00); the button [16] of
  door [14] to the outside (entry 8, a13_01); outdoors round the building to
  door [17] from the east (entry 4) with [4]'s scene and item 0x1A (a13_02);
  the pickup g[5], item 0x27 (a13_03); over the grating step to the hatch
  [62], which opens with the item (a13_04); its ladder down to y 143.1, where
  00193EB0 requests **AREA19 sub 0 entry 9**; AREA19's ladder down to its
  foot and control at (710, 240, 1256.8) (a13_05).
- **The player did not die.** Health 45 -> 44 (one point lost outdoors to
  the cold, a13_02), infection 80 throughout, no bite. The item 0x20
  (a healing item) is held with count 1 at the arrival (section 4); it was
  not used.
- **Not played:** hole [6] / hatch [63] (AREA19 entry 10), door [20], [44]'s
  event (flag 0x1C), [7] (counter 0x42), the lift [10]'s return, and AREA19
  past its entry-9 ladder.
- The a13 group: 6 beats, 11,018 frames, counters 100025..111048.

In-game text (messages, prompts, item names) is described, never quoted.
Positions are world units; yaw is the player's +0xC4 (X = sin, Z = cos).

## 1. Tool, outputs and conventions

```sh
# decomp repo, .venv python, repo root
.venv/bin/python tools/route_capture.py run --beats a13      # the AREA13 beats, in order
.venv/bin/python tools/route_capture.py run --beats a13_04   # one beat (from its source snapshot)
.venv/bin/python tools/route_capture.py events --beats a13   # change log of the traces
.venv/bin/python tools/route_capture.py verify --beats a13
```

`--beats all` and every earlier group are unchanged. a13_00 starts from
`build/s87/route_a04b/a04b_04_lift/state.p2s`; each later beat from the
previous beat's snapshot. Outputs: `../Extermination/build/s87/route_a13/<beat>/`
(ignored): `trace.json`, `state.p2s`, `eeMemory.bin`, `gs.bin`,
`scratchpad.bin`, `original.png`, `snapshot.json`. Every snapshot was
reloaded after its capture (`run_beat`'s check); no beat needed a tail.

- **Rows**: the EIGHTH_LEVEL_ROUTE.md row (decode_a04b) with the AREA13
  owners (`A13_OWNERS`) and `s770` (D_00810770..77, flags 0x18..0x1F),
  `s7f0` (D_008107F0..F7, counters 0x18..0x1F), `s798` / `s818` (flag /
  counter 0x42), `l84e` (D_0081084E, AREA13's lock byte), `c8b` (item 0x27),
  `c82` (item 0x1E), `c7e` (item 0x1A), `c86` (item 0x22), `cc0`
  (D_00810CC0..CF), `c61` (D_00810839, counter 0x61) and `event230` (the
  player's +0x230, the event code 00193EB0 dispatches on).
- **Helpers** (`a13_settle`, `a13_face`, `a13_use`, `a13_control`): the
  a06 / a04b helpers with one change: outdoors the idle player plays the
  cold clips (20 and 349) instead of clip 0, so settling waits for action 0
  with 3B8D == 0 and ignores the clip. `a13_use` idles 3 frames before each
  press and retries (a press right after the player settles is not always
  taken: an exploration press at the hatch's Use point was ignored and the
  same press 3 frames later was taken).
- **Item pages**: 90 frames, then Triangle (as a04b_control).

### 1.1 Owners sampled

Pool nodes measured with `tools/area_overview.py --area 13 --ram
build/s87/route_a04b/a04b_04_lift/eeMemory.bin` (placement table 0x82D570
[n], deferred group 0x829D00 g[n]).

| Row key | Node | Behaviour | Record |
|---|---|---|---|
| `r3_823700` | 0x7AAB70 | overlay 0x823700 (class 0xAA) at (677.3, 160, 1202) | [3]: flag 0x19 |
| `r4_823A40` | 0x7AAE60 | overlay 0x823A40 at (760.5, 174, 1277) | [4]: flag 0x1A, item 0x1A |
| `hole_r5` / `hole_r6` | 0x7AB150 / 0x7AB440 | overlay 0x823BC0 (+0x0D 0 / 1) | [5] (720.1, 160, 1262) / [6] (1081, 160, 845) |
| `r7_824A80` | 0x7AB730 | overlay 0x824A80 at (688, 163, 1041) | [7]: counter 0x42 |
| `door8_r8` | 0x7ABA20 | 001BC350 model 0x03, room move id 1 (entries 2 / 1) | [8] |
| `lift_r10`, `button_r13` | 0x7AC000, 0x7AC8D0 | 001BD560 model 0x0B (door id 0x80: AREA04 entry 7), 001BC960 model 1 | [10], [13] |
| `door14_r14` | 0x7ACBC0 | 001BDE60 model 5 at (688.7, 160, 1161.1) | [14] |
| `button_r15` / `button_r16` | 0x7ACEB0 / 0x7AD1A0 | 001BD9F0 models 3 / 4 at (665.7, 175, 1157.6 / 1164.6) | [15] / [16] |
| `door17_r17` | 0x7AD490 | overlay 0x823580 model 0x03, room move id 3 (entries 9 / 4) | [17] |
| `door20_r20` | 0x7ADD60 | 001BC350 model 0x03, room move id 4 (entries 5 / 10) | [20] |
| `r44_823E90` | 0x7B23E0 | overlay 0x823E90 at (798.4, 215, 1149.5) | [44]: counter / flag 0x1C |
| `hatch_r62` / `hatch_r63` | 0x7B52E0 / 0x7B55D0 | overlay 0x826850 model 0x35 | [62] at hole [5], [63] at hole [6] |
| `pick_g5` / `pick_g8` | 0x7A64F0 / 0x7A6DC0 | 00219550 | g[5] item 0x27 (773.6, 160.2, 1274.3) / g[8] item 0x1E (655.5, 160.2, 1283.1) |
| `bug_g25` .. `bug_g28` | 0x7A9FB0, 0x7AA2A0, 0x7AA590, 0x7AA880 | 0012A5D0 (g[25], g[26]), 00128C10 (g[27], g[28]) | the lobby and east-room bugs |
| `c_g21` | 0x7A93F0 | overlay 0x824BB0 at (724.9, 205, 1280) | g[21] |

## 2. What decides each step (code, then capture)

- **The lift car** (001BD560 model 0x0B, NM): at the arrival the lift is in
  its state 1 sub-state 2 with +0x0B = 3 (its doors open, waiting for the
  inner button); the car's opening is on its east wall around z 1258. A
  walk east at z 1273 stops at x 640.2 against the car's wall (exploration);
  a walk at z 1258 leaves the car.
- **[3]** (overlay 0x823700, AU; its call 0x823830 per the scan): at the
  entry-2 arrival from door [8] it starts at once: flag 0x19 = 1 (a13_00
  f562), script 0x82A360 (a camera scene of 6,331 frames with the player
  locked), then flag 0x19 = 0xFF, counter 0x19 = 1, D_00810CCC (CC3 0x09) =
  1 and D_00810D12 (CC3 0x4F) = 1 with a document page (status request
  03 4F), closed with Triangle.
- **Door [14]'s buttons** (001BD9F0, NM; decomp docs/WORLD_GRAPH.md
  section 3): [16] (model 4, north side, latch 0) offers Use while the
  player faces -z; after its script the room move id 2 (record 08 03) with
  latch 0 goes to entry 8 (688, 160, 1150.6). [15] (model 3, latch 1,
  entry 3) and door [17] (0x823580) refuse Use only while flag 0x1C
  (D_00810774) == 1; it stayed 0.
- **Outside** the idle player plays clips 20 / 349 and the health falls by
  1 every 360 frames (exploration, standing at entry 8: 45 -> 41 at
  frames 921, 1281, 1641 and 2001 after the arrival at 561; a13_02: 45 -> 44 at f310). Whether item 0x1A stops it was not
  measured (the route does not go outside after it).
- **[4]** (overlay 0x823A40, AU): at the entry-4 arrival from door [17]
  it starts its scene; script 0x82A770's op09 record gives item 0x1A
  (D_00810C7E = 1 at a13_02 f927; the item page is an equipment page for a
  cold-weather coat), and at the script's end flag 0x1A = 0xFF and counter
  0x1A = 0xFF (f1028); [4] frees itself.
- **The east room** (x 708..780, z 1213..1277, measured from the
  collision grid of the a13 snapshots): a box (x 713..751.3, z 1213..1238,
  top y 187.5) and a grating step (x 742.4..751, z 1237.6..1277, top y
  168.4) separate door [17]'s side from the hatch [62]. Walking west stops
  at x 755.5 (exploration, at z 1231 and at z 1270); a ledge climb (action
  8) facing -x takes the player onto the step, and walking off its west
  side drops to y 160 (a13_04 f111..f176).
- **The holes [5] / [6]** (overlay 0x823BC0 -> 0x823C10 / 0x823D50, AU,
  read from the instructions): state 0 ends the node (state 3) when flag
  0x1B is 0xFF; state 1 ends it when D_00810C8B (item 0x27) is non-zero;
  otherwise its Use starts script 0x82AB70 / 0x82ADF0 (an examine message).
  With item 0x27 taken (a13_03 f413) both nodes ended and their pool slots
  were reused.
- **The hatches [62] / [63]** (overlay 0x826850, AU, read from the
  instructions): the class byte is 0x84 (offered for Use) while item 0x27
  is held, else 0x04; the Use descriptor is 0x82CDD0 for [62] (z > 1000):
  point (720, 160.5, 1253.7), radius 10, yaw -3.072 (the player faces
  +z). Its Use starts script 0x82CA50; at the script's end the hatch goes
  to state 2 and ORs counter 0x61 (D_00810839) with 1 ([63]: 2). Measured:
  Use at (720, 1253.7) facing +z (a13_04 f382), state 2 and counter 0x61 =
  1 at f762.
- **The ladder and the fall** (00193EB0, NM; decomp WORLD_GRAPH.md section
  5): Use facing +z inside the open hatch: action 0x16 (event code 7) at
  a13_05 f13, then at f112 the player at (720, 143.1, 1259.6) (entry 6's
  point) with action 0x17 and event code 8; with D_00810702 = 4 (the
  door-[17] arrival) and y <= 159, 00193EB0 stores the request 13 00 09 01
  (D_008106B5..B8) at f112: AREA19 sub 0 entry 9 (D_00810700..703 =
  13 00 09 0D at f176, 13 00 09 13 at f187).
- **AREA19 entry 9**: the player appears at (710, 281.4, 1266.1) on a
  ladder (f483), action 0x44 (3B8D = 4, event code 0x2D) descends 12 units
  on its own, then action 0x17 waits (f598); the stick held down climbs
  (about 1 unit per 7 frames) to the ladder's foot, action 0x18 at y 242.4
  (f796), control at (710, 240, 1256.8) (f844).

## 3. Route table

Frames `f` are trace frames; `c` is the main-loop counter 0x70003B64.

| Beat (folder) | Source | Frames | Counters | Presses | What happens |
|---|---|---|---|---|---|
| `a13_00_door8` | a04b_04_lift | 7062 | 100025..107087 | Cross f222 (not taken), f396; Triangle f6988 | out of the car at z 1258, the lobby, door [8] facing -z: 3B8D = 2 f399, **entry 2** f561; [3]: flag 0x19 = 1 f562, its scene, flag 0x19 = 0xFF, counter 0x19 = 1 and CC3 0x09 / 0x4F f6895, the document page f6898..f6993 |
| `a13_01_door14` | a13_00 | 573 | 107088..107661 | Cross f188 | the button [16] facing -z: taken f191, its sub-states 1..4 f191..f460, **entry 8** f523 (outside, (688, 160, 1150.6)) |
| `a13_02_door17` | a13_01 | 1097 | 107662..108759 | Cross f353; Triangle f1020 | east along z 1150, north past the building to (787, 1262), health 45 -> 44 f310 (the cold), door [17] facing -x: taken f356, **entry 4** f498; [4]'s scene: item 0x1A f927, the equipment page f930..f1025, flag and counter 0x1A = 0xFF f1028 |
| `a13_03_item27` | a13_02 | 562 | 108760..109322 | Cross f353; Triangle f506 | the pickup g[5] from (765.5, 1272.5) facing +x: **item 0x27** (D_00810C8B = 1) f413, its page f416..f511; holes [5] / [6] end |
| `a13_04_hatch` | a13_03 | 811 | 109323..110134 | Cross f108, f379 | ledge climb onto the grating step facing -x (action 8 f111..f176), off its west side, the hatch [62] at (720, 1253.7) facing +z: taken f382, script 0x82CA50, **state 2, counter 0x61 = 1** f762 |
| `a13_05_shaft` | a13_04 | 913 | 110135..111048 | Cross f10; stick down from f598 | the hatch's ladder (action 0x16 f13), y 143.1 f112: request 13 00 09 01, **AREA19 sub 0 entry 9** (D_00810700 = 0x13 f176); the AREA19 ladder: action 0x44 f484, 0x17 f598, 0x18 f796, control at (710, 240, 1256.8) f844 |

Main line a04b_04 -> a13_00 .. a13_05: 11,018 frames, counters
100025..111048.

## 4. Story bytes, health and items (measured)

| Byte | Before | After | Where |
|---|---|---|---|
| D_008107F4 (counter 0x1C) | 0 | 1 | at the AREA13 load (0 in the a04b_03 end snapshot, 1 in a04b_04's): [44] 0x823E90's stage 0 is 0x824160 (C), `D_008107F4 += 1` |
| D_00810774 (flag 0x1C) | 0 | 0 | never set on this route ([44]'s stage 1 was not played) |
| D_00810771 (flag 0x19) | 0 | 1, then 0xFF | a13_00 f562, f6895 |
| D_008107F1 (counter 0x19) | 0 | 1 | a13_00 f6895 |
| D_00810CCC (CC3 0x09), D_00810D12 (CC3 0x4F) | 0 | 1 | a13_00 f6895 |
| D_00810C7E (item 0x1A) | 0 | 1 | a13_02 f927 |
| D_00810772 / D_008107F2 (flag / counter 0x1A) | 0 | 0xFF | a13_02 f1028 |
| D_00810C8B (item 0x27) | 0 | 1 | a13_03 f413 |
| D_00810839 (counter 0x61) | 0 | 1 | a13_04 f762 |
| D_00810700..703 | 0D 00 00 0D | 13 00 09 13 | 0D 00 02 0D a13_00 f561, 0D 00 08 0D a13_01 f523, 0D 00 04 0D a13_02 f498, 13 00 09 0D a13_05 f176, 13 00 09 13 f187 |
| player health (+0x220) | 45 | 44 | a13_02 f310 (outside) |
| infection (+0x228) | 80 | 80 | no bite on this route |

**Healing.** At the AREA13 arrival (a04b_04 end snapshot) the healing-item
counts D_00810C82..86 (items 0x1E..0x22, the HEALING page's range,
STATUS_PAGES.md) are 0, 0, 1, 0, 0: item 0x20 is held once (the item the
fourth level's bed used in its side beat a02_s0; the main line never used
it). It was not needed and not used. The graph's AREA13 pickups include
healing items: item 0x1E at g[8] (655.5, 160.2, 1283.1, behind the lobby's
crate row; a Square at the crate [26] in front of it changed the crate's
state byte from 4 to 2 and a bug took the player's back, exploration, not
kept), item 0x20 at g[7] (655.9, 162.1, 773.7, the south field) and item
0x22 at g[3] (703.6, 158.9, 1334.8).

## 5. Census: what the ninth level executes beyond every earlier level

**Tool** (`../Extermination/tools/route_census.py`, additive):
`run --segments a13 --pass A13` replays each a13 beat from its recorded
source snapshot with route_capture's own beat function and one-shot
breakpoints on every boot function and on AREA13's overlay functions
(`NINTH_GROUPS`: overlay id 10). `ninth-delta` runs `chain_delta`
(SEVENTH_LEVEL_ROUTE.md section 9) against the first-level census, beat 15,
the A01, A00, A01R, A02, A04, A22, A01U and A06 passes and all four
eighth-level groups (A06B, A01V, A22B, A04B). Output:
`../Extermination/build/s87/census/a13_delta.json` (and `runs/A13/`).
a13_05 is a change beat: its hits from 001AD010's frame on (census f176)
are its `a13_exit` segment (the AREA19 load at entry 9).

```sh
# decomp repo, .venv python, repo root (hidden PCSX2)
.venv/bin/python tools/route_census.py run --segments a13 --pass A13
.venv/bin/python tools/route_census.py ninth-delta
```

**Replay fidelity.** All six replays completed their beat's own checks
(`beats_incomplete` empty): a13_00 7,063 frames against 7,062 (2 rows
identical), a13_01 570 against 573 (59 of 571), a13_02 1,097 / 1,097
(1,098 of 1,098 identical), a13_03 560 against 562 (15 of 561), a13_04 815
against 811 (0 of 812), a13_05 913 / 913 (914 of 914 identical). As for the
earlier levels, the sets are those of the same route shape, not of the
exact frames.

**Totals.**

| Group | Executed | New | Bytes | Boot | Overlay | Status of the new (census) |
|---|---:|---:|---:|---:|---:|---|
| a13 (main) | 996 in all (971 boot, 25 AREA13) | 29 | 24,088 | 8 | 21 | boot: BM 4, NM 4; overlay: see below |
| a13_exit (the AREA19 load at entry 9) | | 15 | 10,604 | 15 | 0 | BM 9, NM 4, AI 1, AW 1 |

a13 in all: 44 new functions, 34,692 bytes. No unattributed hit. Eleven
hits in a13_exit (census f484) came from AREA19 code (overlay id 16) at
addresses armed for AREA13; they are not counted, and AREA19's own
functions are not in this census. Four AREA13 functions (0x823580,
0x823830, 0x823A40, 0x824A80) count as already run in the eighth level:
a04b_04's census replay hit them at AREA13's first frames through
addresses armed for AREA04 (EIGHTH_LEVEL_ROUTE.md section 5).

**Overlay statuses.** `route_census` reads the overlay statuses from the
working tree at the time of the run; during this run another lane's
uncommitted AREA13 C files were in `src/overlays/AREA13` (the census lists
20 of the 21 new overlay functions as overlay C and 0x829AA0 as NEARMISS).
The statuses at the decomp commit 43bcf60 (`area_overview.py --area 13`)
are: 0x826FB0 C; 0x823A10, 0x823BC0, 0x826140, 0x826FC0, 0x8299E0 AI; the
other 15 AU (assembly). Neither is a claim about byte identity made here.

New functions (census frames; boot status as SEVENTH_LEVEL_ROUTE.md
section 9; overlay status at 43bcf60):

| Function | Bytes | Status | Subsystem | First |
|---|---:|---|---|---|
| overlay 0x823700, 0x823BC0, 0x823C10, 0x823D50, 0x823E90, 0x824180, 0x824BB0, 0x8266A0, 0x826850, 0x826FF0, 0x827150, 0x8292A0, 0x8293A0, 0x8299E0 | 300, 76, 312, 312, 328, 520, 5,520, 428, 1,888, 344, 2,780, 248, 1,596, 184 | AU / AI (above) | overlay_AREA13 | a13_00 f1 (the first replayed frame: [3], the holes, [44], the outdoor creatures, the hatches, [58] / [59], [45], [49]..[54], [47], g[11]) |
| overlay 0x829AA0 | 504 | AU | overlay_AREA13 | a13_00 f561 (the entry-2 arrival) |
| 001BA7F0 | 236 | BM | math_vector | a13_00 f572 |
| overlay 0x823940 | 208 | AU | overlay_AREA13 | a13_00 f6996 ([3]'s scene's end) |
| bone_wobble_decay_1 (001BDCA0), bone_wobble_decay_0 (001BDD70) | 208, 228 | BM | anim_runtime | a13_01 f398, f524 (door [14]) |
| 001DE920, 001E5AC0, 001E7050 | 1,160, 3,328, 692 | NM | weapon_equip / stream_archive | a13_01 f521 (the entry-8 load, outside) |
| overlay 0x823FE0, 0x826140 | 124, 1,232 | AU, AI | overlay_AREA13 | a13_01 f521 |
| overlay 0x823A10 | 36 | AI | overlay_AREA13 | a13_02 f927 (script 0x82A770's op09: item 0x1A) |
| 00214570 | 1,144 | NM | ui_screens | a13_02 f956 (the equipment page) |
| overlay 0x826FC0, 0x826FB0 | 44, 16 | AI, C | overlay_AREA13 | a13_04 f602, f761 (script 0x82CA50's op09 records: the hatch) |
| 001838B0 | 92 | BM | actor_anim | a13_05 f114 (the ladder) |
| 001383C0, 00138540, 001386E0, 00154460, 001546C0, 00154740, 001549C0, 0015A200 | 376, 408, 532, 324, 124, 628, 1,344, 184 | NM, BM, BM, BM, BM, BM, BM, BM | entity_logic | a13_05 f484..f485 (a13_exit: AREA19's first frames) |
| 00183440 | 148 | AI | actor_anim | a13_05 f485 (a13_exit) |
| 00196970, 00196CE0 | 880, 1,712 | BM, NM | init_io | a13_05 f485 (a13_exit: 00196970 is the AREA19 -> AREA13 climb check, WORLD_GRAPH.md section 5) |
| bone_root_pulse (001C06E0), 001C1030, 001C4BA0, 001E7C60 | 2,380, 1,232, 264, 68 | NM, NM, AW, BM | anim_runtime / math_vector / stream_archive | a13_05 f484 (a13_exit) |

The captions in the First column name the beat's event at that frame, not
the function's role; names such as `bone_wobble_decay_1` are labels, not
evidence. To check a port translation of any of these, grep the port
(`grep -rlI -i <address> src tools`).

AREA13 overlay functions not hit in the a13 replays: 0x823540 (no code),
0x8236E0, 0x824060, 0x8240E0, 0x824160 ([44]'s stage 0: it ran at the load,
in a04b_04, where AREA13 was not armed), 0x824390, 0x824520, 0x824960
([44]'s later stages), 0x8246D0, 0x8247B0, 0x8248C0, 0x8249F0 (their
scripts' op09 records), 0x826610, 0x827C30, 0x827DD0, 0x827E00, 0x827F20,
0x827F90, 0x8284E0, 0x828500, 0x828C60, 0x828E10, 0x828F30 and 0x828F40.

## 6. Tried and not kept (exploration runs, this session)

Scratch snapshots under `../Extermination/build/s89_a13/probe/` (deleted
at the session's end); leads, not ground truth.

- Leaving the car at z 1273 (blocked at x 640.2, above).
- A Square at the lobby's crate [26] (above).
- Door [17] and the outdoor walk with `a06_settle`: it waits for clip 0,
  which the cold idle never plays; the helpers above replace it.
- Walking west in the east room at z 1231 and 1270 (blocked at x 755.5).
- Use at the hatch from its east side (729.6, 1261), its north side
  (721.5, 1271.3), and at its Use point facing -z: not taken; at its Use
  point facing +z the first press was not taken and a press 3 frames later
  was (the published use list then held the hatch and the pickups g[12]
  (0x7A7980) and g[3] (0x7A5F10)).
- Walking north into the open hatch: stopped at z 1253.7; the ladder is
  started with Use, not by walking.
- One exploration snapshot (taken right after the hatch opened) did not
  resume (the frame boundary was never reached); `run_beat` verifies every
  kept snapshot.

## 7. Open

1. AREA13's hole [6] / hatch [63] (spawn entries 5 / 7, AREA19 entry 10),
   door [20], [44]'s event (the machine at y 215; its stage 1 sets flag
   0x1C = 1, which locks [15] and [17], and script 0x82C110 sets 0xFF),
   [7] 0x824A80 (counter 0x42), the lift [10]'s return to AREA04 entry 7,
   and the outdoor creatures (0x824BB0 / 0x8266A0, four each) were not
   played. None was needed for hole [5].
2. AREA19 past its entry-9 ladder (decomp WORLD_GRAPH.md section 7 step 5:
   AREA03 through the panel [24], or sub 1 to AREA15) is not recorded; an
   AREA19 group would start from a13_05's end. (Continued in
   `TENTH_LEVEL_ROUTE.md`: groups `a19` / `a13b` from a13_05's end.)
3. Whether item 0x1A stops the outdoor health loss. (Answered in
   `TENTH_LEVEL_ROUTE.md` section 4: it does.)
4. The use of item 0x20 from the HEALING page (the prompt) is not played.
