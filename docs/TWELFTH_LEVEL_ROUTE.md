# AREA13's south roof, door [20] and the hatch [63] into AREA19 entry 10: twelfth-level route, original ground truth

Lane STORY (capture), 2026-10-01 (session s92). Original executable
SHA-256 `ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

`ELEVENTH_LEVEL_ROUTE.md` section 7 left the story in the region south of
AREA13's pipe fence (a13c_06), with door [20]'s north side (AREA13 entry
10) and the hatch [63] (the fall to AREA19 entry 10) not reached. This
document derives the way there from the code and the level's collision
(section 2) and records it as two opt-in beat groups of
`../Extermination/tools/route_capture.py`: `a13d` (8 beats, AREA13) and
`a19b` (3 beats, AREA19 from entry 10), from a13c_06's end. Hidden PCSX2,
exact one-frame steps, pad input only, no teleports and no memory writes,
one trace and one resumable snapshot per beat. Nothing here is port
behaviour.

**Status.**

- **A new part of AREA19 was reached: entry 10** (the hatch [63]'s ladder,
  the region east of door [27]), which no earlier level reached (the tenth
  level played only the entry-9 rooms). The way: east through the south
  region to a switchback stair south of the big building, a ladder to the
  roof corridor (y 240), a ladder onto the first roof block (y 280), a
  running jump onto the second block, its ladder down, the corridor's east
  ladder down to door [20]'s side, door [20] from the north (entry 5), the
  hatch [63] with item 0x27, its ladder: 00193EB0's request 13 00 0A 01.
- **Story bytes that moved:** counter 0x61 (D_00810839) 1 -> 3 (the hatch
  [63]); in AREA19 [7]'s first sequence (0x825AB0, which waits for spawn
  entry 0xA) ran to its end: counter 0x1E (D_008107F6) 0 -> 1; [6]'s first
  stage (0x825240) ran when the player reached the top of a ladder at y
  220: flag 0x1D (D_00810775) 0 -> 1, counter 0x1D (D_008107F5) 0 -> 3, and
  the 0x82A590 group's 0x12E3A0 creature spawned (D_008106C0 = its node).
- **Not reached:** AREA19's exits. Door [27] (lock bit 2) waits for [7]'s
  second sequence (Use on [7], script 0x82BD90, counter 0x1E -> 0xFF); [7]
  was not reached (section 6 has the derived way, section 7 the open
  items). The panel [24] / door [25] (AREA03) and the sub-1 ladders
  (AREA15) were not approached.
- **Health:** 100 at the start, 90 after one hit on the roof corridor
  (a13d_02 f112), 90 at the end. The infection stayed 0.
- Groups: a13d 4,523 frames, counters 126315..130849; a19b 4,554 frames,
  counters 130850..135406. The counter gap is 1 at every beat boundary
  except a13d_03 -> a13d_04 (5: counters 128602..128605 not recorded).
- **Census** (section 5): at least 39 functions (18,004 bytes) that no
  earlier level ran: a13d 9 boot; a19b 23 boot and 7 AREA19 overlay.
  "At least": a19b_00's AREA13 frames were not measured for AREA13 code,
  and four AREA19 addresses were not measured in a19b_00's AREA19 frames
  (section 5, measured gaps).
- **Web leads:** the public walkthroughs found (GameFAQs guides by
  MACisBack, VicViper_Mk2 and _PSXTREME_, the Neoseeker and SuperCheats
  pages, a YouTube 100% walkthrough playlist) could not be read from this
  session (bot checks or empty pages). Nothing here comes from them.

In-game text (messages, prompts, item names) is described, never quoted.
Positions are world units; yaw is the player's +0xC4 (X = sin, Z = cos).

## 1. Tool, outputs and conventions

```sh
# decomp repo, .venv python, repo root (hidden PCSX2; hold build/.pcsx2.lock)
.venv/bin/python tools/route_capture.py run --beats a13d,a19b
.venv/bin/python tools/route_capture.py events --beats a13d,a19b
.venv/bin/python tools/route_capture.py verify --beats a13d,a19b
```

`--beats all` and every earlier group are unchanged. a13d_00 starts from
`build/s87/route_a13c/a13c_06_south/state.p2s`; each later beat from the
previous beat's snapshot (a19b_00 from a13d_07). Outputs:
`../Extermination/build/s87/route_a13d/<beat>/` and `route_a19b/<beat>/`
(ignored): `trace.json`, `state.p2s`, `eeMemory.bin`, `gs.bin`,
`scratchpad.bin`, `original.png`, `snapshot.json`. Every kept snapshot was
reloaded after its capture (`run_beat`'s check) and resumed on the first
check (`tail_idle_frames` 0). a19b_00's first capture attempt stopped
because the emulator exited during its first frame (its log ends right
after the state load); the second attempt is the kept one. The beats were
captured one per lock hold, through a scratch wrapper of this lane that
raises the DebugServer / Pine socket timeouts to 60 s in its own process
(as TENTH_LEVEL_ROUTE.md section 1).

- **Rows**: the a13 decoder (NINTH_LEVEL_ROUTE.md) for a13d, the a19
  decoder (TENTH_LEVEL_ROUTE.md) for a19b, with this lane's owners. `s770`
  [10:12] is flag 0x1D (D_00810775), `s7f0` [10:12] counter 0x1D
  (D_008107F5), `s7f0` [12:14] counter 0x1E (D_008107F6), `c61` counter
  0x61 (D_00810839), `hp` D_008104D0, `area4` D_00810700..703.
- **Helpers**: the a13 / a19 helpers (`a13_settle`, `a13_face`, `a13_use`,
  `a13_control`, `a19_press_until`, `use_press`) and this group's `a13d_go`
  (the stick magnitude eased to min(1, max(0.4, d / 12)) near a waypoint:
  at full magnitude a walk circled round a waypoint near the stair's
  railings), `a13d_walk` (every stop checked against a slack of 4 units),
  `a13d_ladder_up` / `a13d_ladder_down` (Cross facing the ladder, the stick
  held until control at the other end).

### 1.1 Owners sampled

AREA13 (`A13D_OWNERS`): the a13c rows (ELEVENTH_LEVEL_ROUTE.md section 1.1;
the same AREA13 load) plus:

| Row key | Node | Behaviour | Record |
|---|---|---|---|
| `beast_7AAB70` / `beast_7B2CB0` | 0x7AAB70 / 0x7B2CB0 | 00141D20 | group 0x82A230 [0] / [1] (spawned at [7]'s end, a13c_05) |
| `roof_g16` | 0x7A8250 | 00219870 model 0x30 | g[16] (858.5, 250.8, 835) |
| `watch_g17` / `watch_g23` | 0x7A8540 / 0x7A96E0 | overlay 0x824BB0 | g[17] (1050, 201.1, 877.3) / g[23] (990, 205, 944.1) |

AREA19 (`A19B_OWNERS`), measured on an exploration snapshot at the foot of
entry 10's ladder (`tools/area_overview.py --area 19 --ram`): this load
puts the records at other nodes than the a13_05 arrival through entry 9
(for example [7] at 0x7AD490 here, 0x7AD780 there), so the a19 table does
not apply.

| Row key | Node | Behaviour | Record |
|---|---|---|---|
| `r6_8250F0` | 0x7AD1A0 | overlay 0x8250F0 | [6] (956, 210, 1006.5): flag / counter 0x1D |
| `r7_8257C0` | 0x7AD490 | overlay 0x8257C0 | [7] (913.7, 198.2, 919.6): entry 0xA, counter 0x1E |
| `r9_825C70` | 0x7ADA70 | overlay 0x825C70 | [9] (1012, 130, 916.9): waits for flag 0x1E |
| `r10_827790` | 0x7ADD60 | overlay 0x827790 | [10] (class 9) |
| `r18_826100` | 0x7AF4E0 | overlay 0x826100 | [18] (964.9, 280.6, 804.8) |
| `reader_r21` / `door22_r22` | 0x7AFDB0 / 0x7B00A0 | 00158810 / 001BB860 | the reader [21] and door [22] |
| `panel_r24` / `door25_r25` | 0x7B0680 / 0x7B0970 | 00158EC0 / 001BB860 | the panel [24], door [25] (AREA03) |
| `door27_r27` | 0x7B0F50 | overlay 0x823580 | door [27] (lock bit 2) |
| `door31_r31` | 0x7B1B10 | 001BC350 | door [31] |
| `r43_8255D0` / `r47_825EE0` | 0x7B3E50 / 0x7B4A10 | overlay 0x8255D0 / 0x825EE0 | [43] / [47] |
| `g18_219870` / `g20_219870` | 0x7A8830 / 0x7A8E10 | 00219870 model 0x30 | g[18] / g[20] (1036, 160, 920 / 940) |
| `beast_7BDCF0` | 0x7BDCF0 | 0012E3A0 | group 0x82A590 [0] (from a19b_02 f1710 on) |

## 2. What decides each step (code, then collision, then capture)

Evidence marks as in decomp docs/WORLD_GRAPH.md: BM byte-matched C, NM
NEARMISS C, C overlay C linked byte-identical.

- **Why door [20]'s north side.** [7] in AREA19 (0x8257C0, C) runs its
  first sequence 0x825AB0 (C) only while D_00810702 == 0xA, and door [27]
  opens only with AREA19's lock bit 2, which [7]'s second sequence 0x825930
  (C) sets (TENTH_LEVEL_ROUTE.md section 2). 00193EB0 (NM) requests AREA19
  entry 0xA from AREA13 when the spawn entry is 5 or 7 and the player is at
  y <= 159. AREA13 entry 5 is the inside of door [20] (001BC350, BM, model
  0x03, no lock test, room move id 4: entry 5 / 10), the room of the hatch
  [63]; so the story needs door [20] from its north side (entry 10, (1064,
  160, 889)).
- **How the north side is reached: the level's collision.** The grid
  collision of AREA13's level file (exported locally by the port's
  `tools/export_area13_level.py`; node bytes verified against the
  captured RAM there) carries the surface attribute that 0015D4C0 (NM)
  switches on for Use (port PLAYER_LADDER_ENTRY.md): attribute 0x32 is a
  ladder. AREA13 has five ladders besides the roof ladder and the hatch
  shafts: at x 798.5 (z 815.6..823, y 203.5 -> 240), on the first roof
  block's west face (x 888.5, y 240 -> 280) and east face (x 921.4), on the
  second block's east face (x 991.4), and at z 836.4 (x 1018.2..1025.6, y
  240 -> 176.5, foot floor x 1016.2..1027.6, z 836.4..845.4) beside door
  [20]'s region. The ladder at x 798.5 is reached from the south region by
  a switchback stair (attribute 0x35, the stair class of the stair north
  of the lobby: lower flight x 753.8..783.2, z 825..835, y 160 -> 179.5
  westward, landing x 744..754, upper flight z 813.7..823.3, y 179.5 ->
  203.5 eastward). The roof corridor (floor node at y 240 over x 800..1095,
  z 645..835; the corridor is its part between a wall at z 805, x
  830..1095, and the railing at z 834) is crossed by two blocks (x 890..920 and 960..990, top y 280) that fill its width. These
  are leads read from the data; each step below was then played.
- **The second block's missing ladder.** The second block's west face has
  the ladder's rails and top railing in the collision (walls at x
  958.9..961.6, z 827.3..834.7; railing nodes of attribute 0x51 at x 960, y
  280..290) but no attribute-0x32 face and no attribute-0x32 foot or top
  floor (the first block's west ladder has all three). Measured
  (exploration from a13d_02's state): four Cross presses facing +x at
  (954.4, 240, 830.6) started nothing (action 0 throughout). The route
  crosses that block by the running jump from the first block's top
  (0015EC50, NM: none of its three AREA13 boxes covers the roof; the
  second box, x 720..800, z 800..840, y 150..210, covers the stair). From
  the east the block can be left by a drop, so the corridor is two-way
  only with the jump.
- **The hatch [63]** (0x826850, C): with z <= 1000 its descriptor is
  0x82CDF0 (1071.5, 160.5, 844.9, radius 10, yaw -1.606, read from RAM)
  and its open state is D_00810839 bit 1; item 0x27 (held since the ninth
  level) makes it class 0x84; Use runs script 0x82CA50 and ORs the bit.
  Its shaft ladder (grid nodes at x 1077.5, y 121.5 -> 160) is entered with
  Use facing +x, as hatch [62]'s with Use facing +z.
- **AREA19 entry 10.** [7] (0x8257C0) state 1 with D_008107F6 == 0 runs
  0x825AB0: script 0x82C410 at once (D_00810702 is 0xA), then 0x82C4D0
  (with 001831F0 and D_008102B5 handling), then 0x82C690; at the end
  D_008107F6 = 1 (TENTH_LEVEL_ROUTE.md section 2; LEVEL10_PORT.md has the
  port's translation, 0x825AB0 included). Measured: the three scripts ran
  with 3B8D = 2 from a19b_00 f486 to f1734 (1,249 frames; 3B8D is 0 again
  at f1735, the frame counter 0x1E becomes 1) and the player was taken
  down entry 10's ladder to (1086.5, 185, 845). Before that, 3B8D was 3
  from f112 (the request) to f482 and 4 at f484..f485.
- **[6]'s first stage** (0x8250F0 -> 0x825240, C): with the player inside
  the area 0x82BD00 and not below y 210, script 0x82B6E0 (flag 0x1D = 1 by
  its op06), D_008106C0 = 0; at the script's end D_008107F5 |= 1 and 2,
  001E8B40(0), D_008106C0 = 001B6660(0x82A590) (the group's one record:
  0012E3A0, model 1, at (917, 210, 993)) and 001FAE70(0). Measured: the
  top of the ladder at z 974 (x 1066.5..1074.5, y 190 -> 220) is inside
  that area. Its second stage (0x825420, C) needs the player in the area
  0x82BD40 with 190 <= y <= 200 (script 0x82BA00, then D_008107F5 = 0xFF);
  not played.
- **The east ledge** (AREA19 grid node of attribute 0x39, x 1095..1100,
  z 880..950, y 170): walking onto it turned the walk into the wall-side
  shuffle (action 0x2F, a19b_01 f166..f764); 0x39 is not one of 0015D4C0's
  Use cases, so the action comes from the walk itself (which routine
  enters it was not read).

## 3. Route table

Frames `f` are trace frames; `c` is the main-loop counter 0x70003B64.

| Beat (folder) | Source | Frames | Counters | Presses | What happens |
|---|---|---|---|---|---|
| `a13d_00_stair` | a13c_06 | 905 | 126315..127220 | none | east through the south region (past the box of cell entry 78, x 739..745, z 797..805, y 166..171), north under the upper flight to the lower flight's east end, up it west to the landing (y 179.5), up the upper flight east to (790.7, 203.5, 818.8) |
| `a13d_01_ladder405` | a13d_00 | 457 | 127221..127678 | Cross f133 | the ladder at x 798.5 facing +x: actions 0x15 f136, 0x17 f195, 0x18 f328; y 240 (the roof corridor) from f422 at (806.1, 240, 819.3); control at (809.4, 240, 819.3) f457 |
| `a13d_02_block890` | a13d_01 | 636 | 127679..128315 | Cross f291 | east along the corridor; a hit at (862.8, 240, 828.1) f112 (health 100 -> 90, action 0x3E); the first block's west ladder facing +x: 0x15 f294; y 280 (its top) from f601 at (896.1, 280, 830.6); control at (899.4, 280, 830.6) f636 |
| `a13d_03_jump` | a13d_02 | 285 | 128316..128601 | Cross f212 (stick held east) | on the top to (900, 820), facing +x, the run east: action 0x0C (state 6) f215 at (914.1, 280, 821.8); the landing (action 0x0F) f262 at (974.1, 280, 826.9) on the second block |
| `a13d_04_ladder426` | a13d_03 | 394 | 128606..129000 | Cross f101 | the second block's east ladder from its top facing +x: action 0x16 f104; the corridor (1000.7, 240, 810) f384 |
| `a13d_05_ladder432` | a13d_04 | 760 | 129001..129761 | Cross f246, f316 | east to the ladder at z 836.4, from its top facing +z: action 0x16 f319; near the foot actions 0x1A f656, 0x0D f701, 0x0F f702; (1021.9, 176.5, 840.9) |
| `a13d_06_door20` | a13d_05 | 543 | 129762..130305 | Cross f328 | down the slope north and east to door [20]'s north side (1064, 160.2, 889.8); Use facing -z: 3B8D = 2 f331; **entry 5** f493; control at (1064, 160, 872) f494 |
| `a13d_07_hatch63` | a13d_06 | 543 | 130306..130849 | Cross f111 | the hatch [63] at (1071.5, 160.5, 844.9) facing +x: 3B8D = 3 f114, script 0x82CA50 f115; **counter 0x61 1 -> 3** f494 |
| `a19b_00_arrival` | a13d_07 | 1794 | 130850..132644 | Cross f10 | the hatch's ladder (action 0x16); at y 143.1 the request **13 00 0A 01** f112; AREA19 entry 10 f176..f187 (area bytes 13 00 0A 0D -> 13 00 0A 13); on the ladder at (1077.2, 250.1, 845) f483; [7]'s scripts (3B8D = 2) f486..f1734; **counter 0x1E 0 -> 1** f1735 (3B8D 0 again); control at (1086.5, 185, 845) |
| `a19b_01_ledge` | a19b_00 | 991 | 132645..133636 | Cross f886 | off the platform (y 185) to the floor at y 170, east to the wall, north along the attribute-0x39 ledge (action 0x2F f166..f764) to the step at z 950..960 (y 170); a ledge climb facing +z (action 8 f889) onto the platform at y 190, (1090.1, 190, 962.1) |
| `a19b_02_ladder1023` | a19b_01 | 1769 | 133637..135406 | Cross f205 | the ladder at z 974 facing +z (action 0x15 f208); y 220 (its top) from f431 at (1070.5, 220, 981.6), (1070.5, 220, 981.9) from f432; [6]'s script 0x82B6E0 (3B8D = 3 f432, 2 f433..f1709): **flag 0x1D = 1** f500, a message that closes by itself; **counter 0x1D 0 -> 3** f1710 (both bits in one sampled frame; 3B8D 0 again) and control |

Main line a13c_06 -> a13d_00 .. a13d_07 -> a19b_00 .. a19b_02: 9,077
frames, counters 126315..135406.

## 4. Story bytes, health and items (measured)

| Byte | Before | After | Where |
|---|---|---|---|
| D_00810700..703 | 0D 00 09 0D | 13 00 0A 13 | 0D 00 05 0D a13d_06 f493; 13 00 0A 0D a19b_00 f176, 13 00 0A 13 f187 |
| D_00810839 (counter 0x61) | 1 | 3 | a13d_07 f494 |
| D_008107F6 (counter 0x1E) | 0 | 1 | a19b_00 f1735 |
| D_00810775 (flag 0x1D) | 0 | 1 | a19b_02 f500 |
| D_008107F5 (counter 0x1D) | 0 | 3 | a19b_02 f1710 (bits 0 and 1 together in the sampled rows) |
| D_008106C0 | 0 | 0x7BDCF0 | the 0012E3A0 node spawned at the end of script 0x82B6E0 (end snapshot) |
| player health (D_008104D0) | 100 | 90 | a13d_02 f112 |
| D_00810854 (AREA19 lock byte) | 0 | 0 | |

## 5. Census: what the twelfth level executes beyond every earlier level

**Tool** (`../Extermination/tools/route_census.py`, additive):
`run --segments a13d --pass A13D` and `run --segments a19b --pass A19B`
replay each beat from its recorded source snapshot with route_capture's
own beat function and one-shot breakpoints on every boot function and on
one overlay (AREA13, overlay id 10, for a13d; AREA19, id 16, for a19b;
`TWELFTH_GROUPS`). `twelfth-delta` runs `chain_delta` against the
first-level census, beat 15, the A01, A00, A01R, A02, A04, A22, A01U and
A06 passes, the four eighth-level groups, A13, A19, A13B, A13C and, for
a19b, A13D. Output: `../Extermination/build/s87/census/a13d_delta.json`,
`a19b_delta.json` (and `runs/A13D/`, `runs/A19B/`).

```sh
# decomp repo, .venv python, repo root (hidden PCSX2; hold build/.pcsx2.lock for `run`)
.venv/bin/python tools/route_census.py run --segments a13d --pass A13D
.venv/bin/python tools/route_census.py run --segments a19b --pass A19B
.venv/bin/python tools/route_census.py twelfth-delta
```

**Replay fidelity.** All eleven replays completed their beat's own checks
(`beats_incomplete` empty). Rows are compared by the main-loop counter
(`trace_vs_recorded_by_counter`); the start offset is the replay's first
counter minus the recording's.

| Beat | Frames replay / recorded | Start offset | Rows identical by counter | End words differing (player / globals / owners) |
|---|---|---:|---|---|
| a13d_00 | 918 / 905 | 0 | 41 / 906 (first difference at counter 126356) | 16 / 37 / 101 |
| a13d_01 | 457 / 457 | 0 | 458 / 458 (exact) | 0 / 1 (0x810D98) / 0 |
| a13d_02 | 636 / 636 | 0 | 637 / 637 (exact) | 0 / 0 / 0 |
| a13d_03 | 288 / 285 | 0 | 139 / 286 (first difference at counter 128455) | 14 / 30 / 34 |
| a13d_04 | 394 / 394 | -2 | 1 / 393 | 0 / 2 / 26 |
| a13d_05 | 760 / 760 | 0 | 761 / 761 (exact) | 0 / 1 (0x810D98) / 0 |
| a13d_06 | 543 / 543 | 0 | 544 / 544 (exact) | 0 / 1 (0x810D98) / 0 |
| a13d_07 | 543 / 543 | 0 | 544 / 544 (exact) | 0 / 1 (0x810D98) / 0 |
| a19b_00 | 1,794 / 1,794 | 2 | 0 / 1,793 | 0 / 2 / 171 |
| a19b_01 | 991 / 991 | 0 | 992 / 992 (exact) | 0 / 1 (0x810D98) / 0 |
| a19b_02 | 1,770 / 1,769 | 0 | 79 / 1,770 (first difference at counter 133716) | 3 / 2 / 236 |

a13d_00, a13d_03 and a19b_02 are different walks of the same route shape:
the beats are closed-loop (the stick steers by the sampled position), the
replay's pad input is not the recording's (`recorded_inputs_equal_by_counter`
false), and the rows part at the counters given. a13d_04 and a19b_00 start
2 counters off the recording (-2 and +2; why was not read), so few rows
pair. As for the
earlier levels, the census sets are those of the route shape, not of the
exact frames.

**Measured gaps.** Group a19b arms only AREA19 (plain `CensusSession`,
not the EXIT group's overlay swap), and its breakpoints are one-shot.

- a19b_00's AREA13 frames (f1..f175: the hatch's ladder and 00193EB0's
  request) were not measured for AREA13 overlay code.
- The AREA19 breakpoints were armed while AREA13 was still resident.
  AREA13 code hit four of them at a19b_00 f1 (0x823580, 0x824BE0,
  0x826840, 0x8293B0; overlay id 10 in `overlay_hits_other_overlay`, not
  counted), which removed them, so **a19b_00's AREA19 frames (f176..f1794)
  did not measure those four addresses**. a19b_01 and a19b_02 are
  separate replays that re-arm them. Evidence: 0x823580 (door [27]'s
  behaviour) and 0x829370 are both hit at f1 of a19b_01 and of a19b_02;
  in a19b_00, 0x829370 is hit at f485 (AREA19 resident), while 0x823580,
  removed at f1, has no AREA19 hit. Per function:
  - 0x823580 (door [27], 492 bytes): ran in the tenth level; not new
    either way.
  - 0x829370 (pieces 0x829370 and 0x8293B0): its entry stayed armed and
    first hit a19b_00 f485; unaffected.
  - 0x824BE0 (784 bytes, NEARMISS, link name
    func_overlay_AREA19_00824BA0): new either way; **a19b_02 f1204 is its
    first measured run**, and it may have run earlier in a19b_00's AREA19
    frames.
  - 0x826840 (748 bytes, C, link name func_overlay_AREA19_00826800): not
    run in a19b_01, a19b_02 or any earlier level; **whether it ran in
    a19b_00's AREA19 frames is not measured**.

  So the a19b counts below are lower bounds: AREA19 functions executed 21
  or 22, new 30 or 31 (14,944 or 15,692 bytes). The fix is a re-run of
  a19b_00 with an overlay-swap session (boot + AREA13, swapped to AREA19
  at the overlay change, as the EXIT group does); not done in this lane.
- No hit is unattributed.

**Totals.** a13d: 757 functions executed (745 boot, 12 of AREA13's 49); **9
new, 3,060 bytes, all boot** (byte-matched C 7, NEARMISS 2); no AREA13
overlay function is new (every one it ran ran in the ninth to eleventh
levels). a19b: 1,248 executed (1,227 boot, 21 of AREA19's 43 measured);
**30 new, 14,944 bytes** (lower bound, see the gaps above): 23 boot
(byte-matched C 12, NEARMISS 6, asm word 2, asm inline 3) and 7 AREA19
overlay (C 6, NEARMISS 1); 3 of them (0x8250C0, 00138900, 001831F0; 912
bytes) ran only in a19b_00 after the area change (001AD010 at f176).
Together **at least 39 functions, 18,004 bytes** that no earlier level
ran.

New functions (census frames; the captions name the beat's event at that
frame, not the function's role):

| Function | Bytes | Status | Subsystem | First |
|---|---:|---|---|---|
| 001437E0 | 772 | BM | entity_logic | a13d_00 f276 |
| 001CAFA0, 001CB060, 001D3F60, 001D40D0 | 188, 8, 356, 12 | BM, BM, NM, BM | anim_runtime, anim_runtime, render_vif, render_vif | a13d_00 f455 |
| 00146740, 001B39F0 | 620, 564 | BM, BM | entity_logic, math_vector | a13d_00 f515 |
| 001B2F70, 001B3390 | 368, 172 | NM, BM | math_vector | a13d_00 f519, f520 |
| overlay 0x8250C0 | 48 | C | overlay_AREA19 | a19b_00 f483 (AREA19's init, the area-change phase) |
| 00138900 / 001831F0 | 788 / 76 | BM / BM | entity_logic / actor_anim | a19b_00 f485 / f486 (the area-change phase; [7]'s scripts start) |
| 0013BA20, 0013BE60, 0013BF20, 001A96F0, 001C6910, 001C9570 | 396, 192, 716, 192, 68, 152 | BM, asm inline, NM, asm inline, BM, asm inline | entity_logic (3), unknown_01, anim_runtime (2) | a19b_00 f485 |
| overlay 0x825930 | 372 | C | overlay_AREA19 | a19b_00 f1736 ([7]'s second sequence, waiting for Use, from counter 0x1E = 1 on) |
| 00138C20, 0013BBB0, 0013C8C0, 0013CD50, 0013D220 | 1,560, 688, 1,160, 1,224, 164 | BM, asm word, NM, NM, BM | entity_logic | a19b_00 f1767, f1768 |
| 0013C4C0 | 1,016 | BM | entity_logic | a19b_01 f120 |
| 0011BCF8, 0011E420, 0013C1F0 | 1,072, 252, 720 | asm word, NM, BM | lowmem, lowmem, entity_logic | a19b_01 f165 |
| 0017F9E0, 0016EF50, 001B1270 | 420, 1,492, 60 | NM, NM, BM | actor_anim, entity_update, math_vector | a19b_01 f166, f167, f170 (the attribute-0x39 shuffle starts at f166) |
| 001821E0, 00179560 | 104, 280 | BM, BM | actor_anim | a19b_01 f206, f207 |
| overlay 0x824EF0, 0x824F70, 0x825030 | 116, 192, 140 | C | overlay_AREA19 | a19b_02 f503 (behaviours of group 0x82B5A0, the group of script 0x82B6E0, tools/area_overview.py) |
| overlay 0x824BE0 | 784 | NM | overlay_AREA19 | a19b_02 f1204 (first measured run; a19b_00's AREA19 frames did not measure it) |
| 001E8B40 / overlay 0x825420 | 72 / 428 | BM / C | hud_objects / overlay_AREA19 | a19b_02 f1711 / f1712 (the end of script 0x82B6E0; [6]'s second stage) |

To check a port translation, grep the port (`grep -rlI -i <address> src
tools`). Referenced there (whether as a translation or as a hook slot is
not judged here): 001437E0, 00146740, 001B39F0 (em_level11_port_*);
001B2F70, 001B3390 (em_level8_port_creature.c, em_level11_port_internal.h);
00138900, 0013BE60, 0013BF20, 001C6910, 00138C20 (em_level9_port_exit.c);
001A96F0 (em_coll_list_passes.c); 001831F0, 001E8B40 and the overlay
0x825420 / 0x825930 (em_level10_port_area19.c, LEVEL10_PORT.md); 001CB060
(em_status_models.c); 0017F9E0 (em_player.c); 0011E420 (only a test).
The other port hits for these addresses are not these functions: 0x824F70
in `em_area01_revisit.c` is AREA01's, 0x8250C0 in `em_area02_overlay.c` is
AREA02's. None of the others (001CAFA0, 001D3F60, 001D40D0, 0013BA20,
001C9570, 0013BBB0, 0013C8C0, 0013CD50, 0013D220, 0013C4C0, 0011BCF8,
0013C1F0, 0016EF50, 001B1270, 001821E0, 00179560, overlay 0x824BE0,
0x824EF0, 0x825030) is referenced.

AREA19 overlay functions run in no level so far (`already_ran` null in
`overlay_AREA19_not_run`): 0x823540 (the 4-byte entry pad), 0x823770,
0x823780, 0x823CA0, 0x823D10, 0x824A90, 0x826470, 0x826500, 0x826540,
0x826570, 0x826B30, 0x826C10, 0x827430, 0x827540, 0x827550, 0x8279E0,
0x827B10, 0x827B20, 0x827B60, 0x829840, 0x829A70; and 0x826840, which
is in that list only because a19b_00's AREA19 frames did not measure it
(gaps above): it did not run in a19b_01, a19b_02 or any earlier level.

## 6. Tried and not kept (exploration runs, this session)

Scratch snapshots and tools under `../Extermination/build/twelfthcap/`
(probe/, tools/; deleted at the session's end); leads, not ground truth.

- **Collision maps.** A scratch renderer drew top-down maps of the grid
  nodes (export_area13_level.py's `area13.emcl` / `area19.emcl`) and the
  cell-list prims read from the captured RAM (the directory at the
  scratchpad word 0x70003250, entries with bit 0x40000000 skipped); the
  ladders, the stair, the corridor and the blocks of section 2 were found
  that way.
- **The surface attributes 0x51 / 0x52 near the stair.** The player's move
  probes use query id 0 (the actor's world slot; port em_collision.h), so
  attribute-0x52 walls (the box x 737..819, z 807..843 round the stair)
  do not stop the player; a first walk stopped at (735.6, 807) against the
  small box of cell entry 78 instead.
- **Block 960's west face** (section 2): no climb.
- **AREA19 basement** (y 150, x 1036..1100, z 820..1024, reached by the
  stair at x 1036..1056, z 867..887): green spikes on its floor (two
  00219870 records, g[18] / g[20], at (1036, 160, 920 / 940)), two thin
  beams across it in the snapshots' screenshots, and repeated knock-backs (action 0x3E) near (1050..1080,
  905..935), one with health 85 -> 75 (exploration from an earlier
  arrival). Four Cross presses facing +x at (1080.5, 150, 905), with
  knock-backs between them, started no climb. The kept route (a19b_01) uses the ledge along the east wall
  instead.
- **Past [6]'s first stage** (from a19b_02's end, exploration): the walkway
  (y 220, x 970..1100, z 1025..1045) west to the ladder at z 1024 (x
  997.5..1005.5, y 220 -> 184), down it, the stair (attribute 0x35, x
  989..1033, z 990..1010, y 184 -> 172) to the floor at y 172 (x 998..1033,
  z 968..1025). South of that floor, between it and the platform at y 195
  (grid nodes 144 / 145, x 990..1033, z 915..945), lies a 45-degree slope
  (nodes 153 / 158, x 990..1033, z 945..968, y 172 -> 195, normal (0,
  0.707, 0.707), attribute 0) whose authored class byte is 0x10, that is
  class 0x1000. By 00175CF0 / 001796C0 (port PLAYER_CLIMB_SLIDE.md) a floor
  contact of class 0x1000 whose attribute is not 0x39 starts the slide
  (state 0x1C) along the downhill heading; in AREA11 that class is on
  exactly the slide hill's 18 nodes (em_collision.h). Here downhill is +z,
  toward the y-172 floor, so the slope reads as a slide down from the
  platform, not a way up (read from the code and the data, not played).
  The exploration walks toward it stopped on the floor at z 972.1..972.4,
  about 4 units short of its foot (z 968), with health 90 -> 85 and action
  0x3E there; what stopped them was not read (the exploration traces are
  deleted). The platform at y 195 holds
  the east end of the beam to [7] (attribute 0x3A at x 991.7..1004.1, z
  924.6..935.6, y 198, under an attribute-0x34 bar at y 223.2 from x 890.7
  to 1003.2; 0015D4C0's case 0x3A with 0x34 above is the pole entry, as
  AREA06's bar, SEVENTH_LEVEL_ROUTE.md), whose west end (attribute 0x3A at
  x 888.7..902.7, y 197) lies in [7]'s room (floor y 195.1, x 885..935, z
  910..960). How the y-195 platform is reached was not found.

## 7. Open

1. [7]'s Use (0x825930, C: +0xB bit 2 -> script 0x82BD90; counter 0x1E 3,
   4 (001E8B40(1), sound 0x8DF), 0xFF -> D_00810854 |= 4, door [27]). The
   way into [7]'s room is not found. Leads from AREA19's grid (the port's
   local `assets/area19/sub0/area19.emcl`, export_area19_level.py; read,
   not played):
   - **The y-195 platform** under the bar over the beam to [7] (section
     6). Its slope from the y-172 floor is slide class 0x1000 (section 6),
     so the platform is more likely reached from above or from [7]'s side.
     Other leads near it: the pipe ends at x 1003.9..1020, y 203.6..206.2,
     z 915..924; the bar at y 253.4 (node 840, attribute 0x34) over x
     1012.9..1022.9, z 972.4..1039.9, with its attribute-0x3A start on the
     walkway at x 1012.4..1023.4, z 1026.3..1039 (its south end, z 972.4,
     is over the y-172 floor, not over the platform).
   - **Ladders (attribute 0x32) at that spot:**
     - nodes 884..886: face at x 1006.4 (normal +x), z 921.6..929.5, y
       265 -> 290, directly above the y-195 platform's x / z and 70 units
       up. Foot floor 886 at y 265 (x 1006.4..1016.9) on the y-265 deck
       (node 244); top floor 884 at y 290 (x 996.4..1006.4) at node 384
       (attribute 0x04, x 950..1005, z 910..940).
     - nodes 1016..1018: face at z 939 (normal -z), x 911..919, y 230 ->
       265, inside the x / z of [7]'s room. Top floor 1017 at y 265 (z
       939..949) on the deck over the room (nodes 222 / 227); foot floor
       1018 at y 230 (z 928.5..939) on node 192 (attribute 3), a y-230
       floor over x 885..935, z 910..960, the same x / z as the room's
       floor (node 197, y 195.1). Whether that y-230 floor leads down to
       [7] is not read.
     - nodes 959 / 960: face at z 859.5 (normal +z), x 921.1..929.1, from
       the y-210 floor (nodes 299 / 300, x 900..970, z 820..880) up to y
       357; no grid floor at its top (y 330..365) in this export.
     - nodes 694 / 695: face at z 960 (normal +z), x 902.9..911, from the
       y-265 deck (node 227) up to y 349.
   - **The y-265 deck** is one level of floors: nodes 222 / 227 (x
     895..935, z 940..980), 252 (x 935..1005, z 955..980), 244 / 248 / 247
     (x 1005..1030, z 845..1100) and 258 (x 994.9..1030, z 820..845). How
     the deck itself is reached was not read.
2. [6]'s second stage (0x825420: area 0x82BD40, 190 <= y <= 200, script
   0x82BA00, counter 0x1D = 0xFF) and the 0012E3A0 creature spawned at its
   first stage (not met on the kept route).
3. [9] (0x825C70, C) waits for flag 0x1E = 1 (script 0x82BD90's op06) and
   then moves two objects for 260 frames and toggles cell entry 0x21
   (0019C6F0); not reached.
4. AREA19's exits from this side: door [27] (bit 2), the panel [24] (item
   0x1B) and door [25] to AREA03, the sub-1 ladders (00196970) to AREA15.
5. The roof hit (a13d_02 f112, health 100 -> 90, action 0x3E). The
   00219870 node g[16] at (858.5, 250.8, 835) changed its header bytes at
   f100 (+0 01 -> 02, +5 00 -> 01), f107 (+4 01 -> 02), f108 (+1 01 -> 00)
   and f116 (+4 02 -> 03), and its header was cleared at f117. At the hit
   frame f112 the pool node sampled as `r44_823E90` (0x7B20F0) holds a new
   record with behaviour 0x1E4CE0 at (858.5, 250.8, 829.0). The node
   sampled as `r51_8292A0` (0x7B2FA0), which held a 0x1EA240 record at
   (842.2, 239.6, 822.4), is cleared at f112 and holds a second 0x1E4CE0
   record at (858.5, 250.8, 823.0) from f113. Both new records have
   g[16]'s x and y. The row keys keep a13c's names; in this beat those
   nodes hold short-lived records, not [44] / [51]. Which record dealt the
   hit is not read.
6. Census gaps (section 5): a19b_00's AREA13 frames (f1..f175) were not
   measured for AREA13 overlay code, and a19b_00's AREA19 frames did not
   measure 0x823580, 0x824BE0, 0x826840 and 0x8293B0 (AREA13 code hit and
   removed those one-shot breakpoints at f1). 0x826840 may therefore have
   run unseen, and 0x824BE0's first run may be earlier than a19b_02 f1204.
   Both close with one re-run of a19b_00 under an overlay-swap session
   (boot + AREA13, swapped to AREA19 at the overlay change).
7. Memory cards: the shared install's `inis/PCSX2.ini`,
   `inis/playtime.dat` and `portable-data/memcards/Mcd001.ps2` /
   `Mcd002.ps2` (under `build/startup-reference/`) carry mtimes
   2026-10-01 21:07:07..08, the end of this lane's last session (the A19B
   census); PCSX2's shutdown appears to rewrite them. The route makes no
   in-game save, but that
   the cards' content is unchanged is not proven; a tooling-level check
   (hash the cards before and after every session) is a shared-tooling
   item for all capture lanes, outside this lane.
