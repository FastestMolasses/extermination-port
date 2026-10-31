# After the keypad: eighth-level route, original ground truth

The route tables below retain the original legacy recording. The 2026-10-18
fork continuation is tracked separately at the end of this document; its
different inputs and ancestry do not replace the legacy reference path.

Lane "NEXT" (world graph, then capture), 2026-09-28 (session s88). Original
executable SHA-256
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

SEVENTH_LEVEL_ROUTE.md ends in AREA06 with the keypad's bit set
(D_00810845 bit 5, beat a06_02) and then plays the beam's collapse into a
pit whose only door needs a bit that AREA15 writes. The decomp's
`docs/WORLD_GRAPH.md` (this lane's part A, from the code and data only)
derives the order from there: the keypad's bit opens AREA04's door [45],
behind which the NPC [2] gives item 0x23; the reader [50] beside AREA04's
lift [51] takes that item and sets the lock bit 7; the lift's inner call
button [54] (001BC960) then needs flag 0x12 (D_0081076A) at 0xFF or
counter 0x12 (D_008107EA) at 0x10 (with conditions on D_008106C0), and the
lift goes to AREA13 (entry 0), the next area not yet recorded. AREA13
leads on to AREA19 and, through it, AREA15.

This document records the original from a06_03 (outside the keypad room,
the bit set) along that order, as opt-in beat groups of
`../Extermination/tools/route_capture.py`: `a06b` (back east over the beam
without its collapse), `a01v` (AREA01's upper floor to door [18]), `a22b`
(AREA22 to door [6]) and `a04b` (AREA04: door [45], the NPC, the reader,
the lift to AREA13).
Hidden PCSX2, exact one-frame steps, pad input only, no teleports and no
memory writes, one trace and one resumable snapshot per beat. Nothing here
is port behaviour.

**Status.**

- **AREA06 left after the keypad, with the beam intact.** A running jump
  from the west pad's south part carries the player over the beam's
  trigger quad onto its east part (a06b_00): the beam's collapse test
  requires the player's +5 below 2 or in 29..34 and the jump is +5 = 6, so
  D_00810768 stays 0 and the beam stays in state 1. Door [1] then leads to
  AREA01 entry 7 (a06b_01).
- **Back to AREA04** over AREA01's gap (a running jump north, a01v_01),
  door [18] to AREA22 entry 5, AREA22's corridor and door [7] from the
  south, door [6] to AREA04 entry 1 (a22b_01). No lock and no story byte on
  the way.
- **AREA04: done up to the reader.** Door [45] opens with the bit
  (a04b_00, entry 9); the NPC [2]'s quad starts its script, a long
  cinematic (8,353 frames of script), and at its end item 0x23
  (D_00810C87 = 1) with counter 0x11 = 1 and the item page (a04b_01); back
  through door [45] (a04b_02); up the conveyor to the reader [50]: its
  script sets D_00810845 bit 7 (0x28 -> 0xA8) and flag 0x12 = 1, and the
  event [3] 0x8246B0 runs its first script, 0x828450, and spawns a creature
  (behaviour 0012E3A0, group 0x826790) and four bugs at the lift
  (a04b_03).
- **AREA13 reached by the lift [51] (a04b_04).** From a04b_03's end
  (counter 0x12 = 0x10, flag 0x12 = 1, D_008106C0 = 0) into the car to the
  inner call button [54] at (561.2, 69.9, 242), yaw +pi/2: Use with the
  player facing -x (the button's yaw + pi) is taken at once. The button's
  script, then the lift's +0x0B 3 -> 4, flag 0x14 (D_0081076C) 0 -> 1 and
  [4] 0x824DC0's script 0x828BE0, flag 0x14 = 0xFF, the request 0D FF 00 01
  and **AREA13 entry 0** at (634.5, 159.8, 1273.0) at f1711 (y 160.0
  from f1713). [3]'s second stage
  (0x824930) did not run: the creature (0012E3A0) at the lift was not
  beaten and is not needed for the lift; its hits took the player's health
  from 90 to 45 on the way into the car. The a04b group: 5 beats, 15,952
  frames.
- **Not recorded here: AREA13 itself** (its rooms, and its exits to
  AREA19), nor the second half of the order. AREA13 from this arrival to
  AREA19 entry 9 is recorded in `NINTH_LEVEL_ROUTE.md` (group `a13`).

**Native status (lane E1, 2026-10-30).** From New Game the port plays this
route row for row: canonically a06b_00 780 rows, a06b_01 822, a01v_00 555,
a01v_01 177, a01v_02 771, a22b_00 1,099, a22b_01 886 (`--until a22b_01`, 73
live phases), and on the bed branch through a04b_00 1,242, a04b_01 8,690,
a04b_02 714 and a04b_03 2,414 (`--until a04b_03`, 78 live phases): the reader,
event [3], and the creature's record equal to the end RAM at the last row, with
each beat's progress block equal, health and infection included
(LEVEL3_BINDING.md "2026-10-30, lane E1"). On from a04b_03's end, with no anchor
after the area entries: the pistol recording a04b_s5 1,937 rows, a04b_s6 288,
a04b_s7 1,764 of 2,410 (the creature and its four bugs in every row, `--until
a04b_s7`), and the route's a04b_04 1,437 of 2,083 (`--until a04b_04`), both to
the row before AREA13's load. The table below is the earlier start-card status.

**Start cards (lane D1, 2026-10-30).** The a04b beats play from start
cards (`python3 tools/mid_route_beats.py a04b`, card a04b_00; the fork
continuation levels-recordings-20261018-10). A loaded game is not the
route's machine state, so these are not whole-beat row comparisons:

| Beat | From its card | Status |
|---|---|---|
| a04b_00 | door [45] taken, entry 9 | end block equal but for the infection word (the loaded bugs did not bite on the way) |
| a04b_01 | the NPC's script 0x827D90, item 0x23, counter 0x11 | end block equal, the same one word |
| a04b_02 | door [45] out, entry 8 | end block equal, the same one word |
| a04b_03 | reader [50], lock bit 7, flag 0x12, script 0x828450, counter 0x12 = 0x10, the creature | end block equal, the same one word. Aligned on the lock byte, the card run equals the recording row for row from f1092 to the beat's end f2412 (1,321 rows): player, camera, locks and the owners [3], [4], [50], [51], [53], [54]. The creature spawns at (545, 55, 260) with counter 0x12 = 0x10 (f2344); 69 ticks later, the recording's last row, its record equals the end RAM: the first 16 bytes (state 1 sub 5 / 2), +0xB0 = (545, 54.9, 260.26), +0x1F0 |
| a04b_04 | the creature and the bugs fight; the Use at [54] | health falls by the creature's bites (action 0x3E, 20 or 25 a hit; the recordings show the same two amounts) and the bugs' grabs, at other rows than the recording's. From the card the recorded pad leaves the player facing away from [54]; with a scratch pad that turns and presses (never committed), the Use is taken and the ride plays: the lift's sub-states 3, 7, 5 / 6 at the recording's spacing (111 and 499 ticks), flag 0x14 = 0xFF, the player at (575, 55, 256), the fade, the request to AREA13. There the run stops: AREA13's module is not in the loader's table (`the screen-module loader faulted at 00112440`) and AREA13 has no host yet |

The creature's rows cannot be row-exact from a card: its choices draw
rand() and the loaded game's bugs and position differ.

In-game text (messages, prompts, item names) is described, never quoted.
Positions are world units; yaw is the player's +0xC4 (X = sin, Z = cos).

## 1. Tool, outputs and conventions

```sh
# decomp repo, .venv python, repo root
.venv/bin/python tools/route_capture.py run --beats a06b     # the AREA06 beats, in order
.venv/bin/python tools/route_capture.py run --beats a01v     # the AREA01 upper-floor beats
.venv/bin/python tools/route_capture.py run --beats a22b     # the AREA22 beats
.venv/bin/python tools/route_capture.py run --beats a04b     # the AREA04 beats
.venv/bin/python tools/route_capture.py events --beats a04b  # change log of the traces
.venv/bin/python tools/route_capture.py verify --beats a06b,a01v,a22b,a04b
```

`--beats all` and every earlier group are unchanged. a06b_00 starts from
`build/s87/route_a06/a06_03_room_out/state.p2s`; each later beat from the
previous beat's snapshot. Outputs: `../Extermination/build/s87/route_<group>/<beat>/`
(ignored): `trace.json`, `state.p2s`, `eeMemory.bin`, `gs.bin`,
`scratchpad.bin`, `original.png`, `snapshot.json`.

- **Walking, bugs, Use**: AREA04's `a04_go` and the a06 helpers
  (`a06_settle`, `a06_face`, `a06_approach`, `a06_use`), which shake off a
  bug's grab (FIFTH / SEVENTH_LEVEL_ROUTE.md section 1).
- **The running jumps** (`a06b_running_jump`, and the same steps in
  a01v_01): the stick toward the target, Cross once the lip is passed, the
  stick held through the jump (action 0x0C) and released at the landing
  (action 0x0F): on the beam, a stick still held after the landing walks
  the player off its edge (an exploration run did).
- **Item pages** (`a04b_control`): when an item take opens the status page
  (ui byte 1 = 3), 90 frames, then Triangle, as a01u_s0.
- **Rows**: the SEVENTH_LEVEL_ROUTE.md row (via decode_a01u) with each
  group's owners (`A01V_OWNERS`, `A22B_OWNERS`, `A04B_OWNERS`; a06b uses
  `A06_OWNERS`). The a04b rows add `s76c` (D_0081076C..6F: flag 0x14, the
  lift key) and `l84c` (D_0081084C).

### 1.1 Owners sampled

Pool nodes measured with `tools/area_overview.py --area N --ram <snapshot>`
on the arrival snapshot of each load.

| Group | Row key | Node | Behaviour | Record |
|---|---|---|---|---|
| a01v | `door18_r18` / `door19_r19` | 0x7AEC10 / 0x7AEF00 | 001BB860 / 001BC350 | [18] / [19] |
| a01v | `gap_g36_1BF6B0` | 0x7AAB70 | 001BF6B0 | g[36], after 001C02E0's bit-5 branch (section 2) |
| a01v | `n_1BFFD0`, `n_1BE6C0` | 0x7A6200, 0x7A6AD0 | 001BFFD0, 001BE6C0 | spawned at the gap |
| a01v | `bug_a` .. `bug_c` | 0x7B7EF0, 0x7B81E0, 0x7B84D0 | 0012A5D0 | group 0x829110, at the gap and on the platform |
| a22b | `door6_r6` .. `door10_r10`, `reader_r9` | 0x7A9CC0, 0x7A9FB0, 0x7AA2A0, 0x7AA880, 0x7AA590 | as in SIXTH_LEVEL_ROUTE.md | this load's addresses |
| a04b | `npc_r2`, `r3_8246B0`, `r4_824DC0` | 0x7AA2A0, 0x7AA590, 0x7AA880 | overlay 0x824320, 0x8246B0, 0x824DC0 | [2], [3], [4] |
| a04b | `door45_r45`, `reader_r50` | 0x7B20F0, 0x7B2FA0 | overlay 0x823700, 00158810 model 0x2F | [45], [50] |
| a04b | `lift_r51`, `button_r53`, `button_r54` | 0x7B3290, 0x7B3870, 0x7B3B60 | 001BD560 model 0x0B, 001BC960 models 0 / 1 | [51], [53], [54] |
| a04b | `lift_r56`, `socket_r55`, `button_r58` | 0x7B4140, 0x7B3E50, 0x7B4720 | 001BD560 model 0x0D, 00159E70, 001BDFC0 | [56], [55], [58] |

The director [1] (0x823B90) is not in this load's pool: with D_00810845
bit 5 set its state 0 ends at once (FIFTH_LEVEL_ROUTE.md section 2.3).

## 2. What decides each step (code, then capture)

- **The beam's test** (overlay 0x824560, C, state 1 sub-state 0): the
  collapse script starts only with the player at y >= 55 inside the quad
  0x827680 (x -273..-269, z -600..-570), D_008106B9 = 0 and D_008102B5
  below 2 or in 29..34. Walking is +5 = 1 (a06_04, measured); the running
  jump is +5 = 6. 0015EC50 needs a drop of more than 4.01 ahead: straight
  east from the pad's lip the beam itself is under the lip, and Cross is
  not taken (exploration); from the pad's south part (z -592) the drop is
  the pit, and the jump is taken.
- **AREA01's gap node with the bit set** (001C02E0, g[36]; NM): its state 0
  found D_00810845 bit 5 set at the load, took its other branch (behaviour
  001BF6B0 at the node, D_00810766 = 0xFF) and spawned group 0x829110
  (three 0012A5D0 bugs, 001BE6C0 and 001BFFD0 at the gap). Measured in the
  a06b_01 end snapshot. The jump north was not hindered.
- **Door [45]** (overlay 0x823700, C): with bit 5 set, 001BB560 with 0 and
  the room move (door program 0x24D900; request B7..B8 = 09 02).
- **The NPC [2]** (0x824320 / 0x824490, C): the quad 0x828220 (x 390..420,
  z 78..105, read from the overlay's data) starts script 0x827D90; its
  op0D sub 0 record waits for the camera timeline (the camera block's
  +0x74 counts 0.5 a frame up to +0x78 = 4141.0, measured); at the
  script's end 001C47A0(0x23, 1), 001C4760(8, 1) and counter 0x11 = 1.
- **The reader [50]** (00158810 model 0x2F; 001576E0 BM): with D_00810C87
  != 0, script 0x246C20, whose op09 001580C0 sets bit 7 (`1 << +0x2E`,
  +0x2E = 7); 00158810 stores D_0081076A = 1.
- **The event [3]** (0x8246B0, C): while D_0081076A is set, counter 0x12
  0: script 0x828450, then counter 0x12 = 0x10 and groups 0x826790 (the
  creature 0012E3A0 at (545, 55, 260)) and 0x8267F0 (four 0012A5D0 bugs)
  (0x824830). Its second stage 0x824930 runs only for counter 0x12 = 1,
  2, 0x20 or 0xFF. Its sub-state 0 tests the pointer at +0x240, which
  0x824830 stored from 001B6660 (group 0x826790) and which nothing in the
  event's code clears: not null, it copies that object's +0xB0 and starts
  script 0x8286D0 (counter 0x12 = 2, flag 0x12 = 0xFF by the scan of its
  records); null, group 0x827230, 001EFD20 and
  flag 0x12 = counter 0x12 = 0xFF at once. The one direct store of
  counter 0x12 = 1 the scan finds is in 00131B10 (BM: its state 0, when
  the actor's +0x0D bit 1 is set); 00131B10 ran in no a04b replay, so
  which actor it serves here is not established. Nothing read shows the
  creature's death writing the counter or clearing the pointer. The
  second stage is not needed for the lift (below) and did not run on this
  route.
- **The lift [51]** (001BD560 model 0x0B, NM): measured after the reader,
  the button [53] went to its state 1 sub-state 5 and the lift to state 1
  sub-state 2 (doors open; a04b_03 f1213..f1325). Sub-state 2 waits for
  +0x0B = 4 from the inner button [54]. 001BC960 (BM: byte-matched and
  linked from its compiled C; its state 2 sub-state 1 also read from the
  original instructions, decomp docs/WORLD_GRAPH.md section 3):
  with the lock bit set, flag 0x12 == 0xFF advances it; otherwise counter
  0x12 == 0x10 enables it: with D_008106C0 null, Use is offered every
  frame (001BC740: node +0x0B bit 2, the player turned to the button's
  yaw + pi); with it not null, it advances when that record's +4 >= 2, or
  offers Use while its +0xB0 < 554.0. At a04b_03's end the button is in
  state 2 sub-state 1 with +0 = 1 (only the offering branch sets it). Use
  (a04b_04 f521, taken f524): state 1 sub-state 1 (script 0x24E560),
  sub-state 6 at f730, the lift's +0x0B 3 -> 4 at f731; the lift's
  sub-state 3 (f732), then its keyed first ride: flag 0x14 = 1 and
  sub-state 7 (f843), [4]'s script until flag 0x14 = 0xFF, sub-states 5 /
  6 (f1342 / f1343) and the request 0D FF 00 01 (f1342). The exploration
  that failed (section 6) pressed Cross facing +x, -z and +z, never -x.

## 3. Route table

Frames `f` are trace frames; `c` is the main-loop counter 0x70003B64.

| Beat (folder) | Source | Frames | Counters | Presses | What happens |
|---|---|---|---|---|---|
| `a06b_00_trigger_jump` | a06_03_room_out | 779 | 78996..79775 | Cross f384 | east to the west pad's south part, running jump over the beam's quad (action 0x0C f387, landing 0x0F f436 at (-217.3, 56.9, -581.3)), east pad, east floor; D_00810768 stays 0 |
| `a06b_01_door1` | a06b_00 | 814 | 79776..80590 | Cross f82 (not taken), f169 | door [1]: **AREA01 entry 7** (D_00810700..703 = 01 00 07 06 at f314) |
| `a01v_00_catwalk_east` | a06b_01 | 554 | 80591..81145 | — | east along the catwalk, north along x 130 to (130.1, 60, -460.8) |
| `a01v_01_gap_jump` | a01v_00 | 177 | 81146..81323 | Cross f94 | running jump north over the gap (0x0C f97, landing f144), (130.9, 60, -379.5) |
| `a01v_02_door18` | a01v_01 | 764 | 81324..82088 | Cross f129 (not taken), f216 | door [18]: **AREA22 entry 5** (request 16 FF 05 01 f307; area bytes f371) |
| `a22b_00_to_door7` | a01v_02 | 1098 | 82089..83187 | Cross f670 (not taken), f757 | the south arm, the corridor, door [7] from the south: entry 2 (f912) |
| `a22b_01_door6` | a22b_00 | 879 | 83188..84067 | Cross f216 | door [6]: **AREA04 entry 1** (request 04 FF 01 01 f297; area bytes f361) |
| `a04b_00_door45` | a22b_01 | 1264 | 84068..85332 | Cross f763, f932 (not taken), f1059 | balcony, conveyor down, the hall (a bite at f289: infection 60 -> 70), door [45] with bit 5: **entry 9** (f1214) |
| `a04b_01_npc` | a04b_00 | 8689 | 85333..94022 | Triangle f8615 | the NPC [2]'s quad at (405.1, 79.1): script 0x827D90 f169..f8522, **D_00810C87 = 1** and counter 0x11 = 1 at f8522, the item page f8525..f8620 |
| `a04b_02_door45_out` | a04b_01 | 1941 | 94023..95964 | Cross f390, f700, f1009, f1318 (not taken), f1623 | door [45] from the room: entry 8 (f1778) |
| `a04b_03_reader` | a04b_02 | 2017 | 95965..97982 | Cross f622 | conveyor up, the reader [50]: flag 0x12 = 1 (f625), script 0x246C20, **D_00810845 = 0xA8** (f813); [3]'s script 0x828450 (f626..f1947), the lift's doors open (f1213..f1325), counter 0x12 = 0x10 (f1948) |
| `a04b_04_lift` | a04b_03 | 2041 | 97983..100024 | Cross f521 | into the car past the creature (health 90 -> 70 f169, -> 45 f371), button [54] facing -x: taken f524; lift +0x0B 3 -> 4 f731, flag 0x14 = 1 f843, 0xFF f1342, request 0D FF 00 01 f1342, D_00810700..703 = 0D 00 00 04 f1406 then 0D 00 00 0D f1417, **AREA13 entry 0** at (634.5, 159.8, 1273.0) f1711 (y 160.0 from f1713), in control f1972 |

Main line a06_03 -> a06b_00 .. a04b_04: 21,017 frames (12 beats; a04b
15,952), counters 78996..100024. Every snapshot was reloaded after its capture; a22b_00 was
captured twice (its first snapshot did not resume) and kept with 23 tail
idle frames.

## 4. Story bytes (measured)

| Byte | Before | After | Where |
|---|---|---|---|
| D_00810768 (flag 0x10, the beam) | 0 | 0 | never set on this route (the beam kept its state 1) |
| D_00810766 | 0 | 0xFF | AREA01's gap node at the a06b_01 arrival (001C02E0, bit-5 branch) |
| D_00810C87 (item 0x23) | 0 | 1 | a04b_01 f8522 |
| D_008107E9 (counter 0x11) | 0 | 1 | a04b_01 f8522 |
| D_00810845 | 0x28 | 0xA8 | a04b_03 f813 |
| D_0081076A (flag 0x12) | 0 | 1 | a04b_03 f625 |
| D_008107EA (counter 0x12) | 0 | 0x10 | a04b_03 f1948 (still 0x10 at AREA13's arrival) |
| D_0081076C (flag 0x14, the lift key) | 0 | 1, then 0xFF | a04b_04 f843, f1342 |
| D_00810700..703 | 04 00 08 04 | 0D 00 00 0D | a04b_04 f1417 (0D 00 00 04 at f1406) |
| infection (+0x228) | 60 | 70, then 80 | a04b_00 f289, a04b_04 f6 (bites) |
| player health | 90 | 45 | a04b_04 f169 (70), f371 (45): the creature's hits |

## 5. Census: what the eighth level executes beyond every earlier level

**Tool** (`../Extermination/tools/route_census.py`, additive):
`run --segments <group> --pass <PASS>` replays each beat of an eighth-level
group from its recorded source snapshot with route_capture's own beat
function and the one-shot breakpoints on every boot function and on the
group's own area overlay (a06b: AREA06, a01v: AREA01, a22b: AREA22, a04b:
AREA04). `eighth-delta` runs `chain_delta` (SEVENTH_LEVEL_ROUTE.md section
9) for each group against the first-level census, beat 15, the A01, A00,
A01R, A02, A04, A22, A01U and A06 passes and the eighth-level groups
before it (in story order). Outputs:
`../Extermination/build/s87/census/<group>_delta.json`; each summary also
lists `beats_incomplete` (replays that did not complete their beat's own
checks; their hits cover only the frames before the failure) and
`replay_runs` (frames and completion per beat). a04b_04 is a change beat:
its hits from 001AD010's frame on (census f1405) are its `a04b_exit`
segment.

```sh
# decomp repo, .venv python, repo root (hidden PCSX2)
.venv/bin/python tools/route_census.py run --segments a06b --pass A06B
.venv/bin/python tools/route_census.py run --segments a01v --pass A01V
.venv/bin/python tools/route_census.py run --segments a22b --pass A22B
.venv/bin/python tools/route_census.py run --segments a04b --pass A04B
.venv/bin/python tools/route_census.py eighth-delta
```

**Replay fidelity.** Every replay but one completed its beat's own
checks: a06b_00 778 frames against 779 (52 rows identical), a06b_01
815/815, a01v_00 555/555, a01v_01 178/178, a01v_02 765/765, a22b_00 835 of
1,099 rows, a22b_01 808 of 880, a04b_01 8,682 frames against 8,689 (it
started one counter off, 0 rows identical), a04b_02 1,940 against 1,941,
a04b_03 2,153 against 2,017 (468 rows identical), a04b_04 2,042 rows
against 2,042 (it started one counter off: 0 rows identical, 594
identical ignoring the counter, the clock and r9; the end digests of the
player, the globals and the owners equal the recording's). **a04b_00 did
not complete** in three attempts (and in three more with ten Use tries,
not kept): door [45]'s Use was not taken at (440.3, 14.9, 114.5); its pass
holds the walk to the door (602 functions, 2,200 frames), not the door's
opening, and `a04b_delta.json` lists it under `beats_incomplete`. The
door's program ran in a04b_02 (from the room), so the a04b function set
misses only what differs between the two sides. As for the earlier levels,
the sets are those of the same route shape, not of the exact frames.

**Totals.**

| Group | Executed | New | Bytes | Boot | Overlay | Status of the new |
|---|---:|---:|---:|---:|---:|---|
| a06b (main) | 843 (838 boot, 5 AREA06) | 1 | 388 | 1 | 0 | BM 1 |
| a06b_exit (the AREA01 load at entry 7) | | 4 | 3,512 | 4 | 0 | BM 3, NM 1 |
| a01v | 809 (799 boot, 10 AREA01) | 3 | 684 | 3 | 0 | BM 2, AW 1 |
| a01v_exit (the AREA22 load at entry 5) | | 0 | 0 | 0 | 0 | — |
| a22b (and its exit, the AREA04 load at entry 1) | 767 (all boot) | 0 | 0 | 0 | 0 | — |
| a04b (main) | 985 (967 boot, 18 AREA04) | 41 | 27,036 | 36 | 5 | BM 14, NM 16, AW 4, AI 1, CL 1, C 5 |
| a04b_exit (the AREA13 load at entry 0) | | 4 | 1,384 | 4 | 0 | BM 2, NM 1, AW 1 |

a04b in all: 45 new functions, 28,420 bytes. No unattributed hit. Seven
hits in a04b_exit (census f1713..f1714) came from AREA13 code (overlay id
10) at addresses armed for AREA04 (0x823580, 0x8235C0, 0x823920,
0x823A90, 0x823B10, 0x823B90, 0x824A90); they are not counted, and
AREA13's own functions are not in this census (no AREA13 group). No other
group had a hit with another overlay resident.

New functions (census frames; status as SEVENTH_LEVEL_ROUTE.md section 9):

| Function | Bytes | Status | Subsystem | First |
|---|---:|---|---|---|
| 001EAD70 | 388 | BM | hud_objects | a06b_00 f577 |
| 001BE6C0 | 1,012 | NM | math_vector | a06b_01 f612 (AREA01 load: the gap node's bit-5 branch) |
| 001BEAC0 | 168 | BM | math_vector | a06b_01 f612 |
| 001BF6B0 | 2,268 | BM | math_vector | a06b_01 f612 |
| 001BFF90 | 64 | BM | math_vector | a06b_01 f613 |
| 001284E0 | 276 | BM | lowmem | a01v_01 f83 |
| 0012B850 | 280 | AW | lowmem | a01v_01 f85 |
| 001BF5B0 | 128 | BM | math_vector | a01v_01 f62 |
| 00215870 | 1,896 | BM | ui_screens | a04b_01 f8546 (the item page) |
| overlay 0x8245F0 | 188 | C | overlay_AREA04 | a04b_01 f8615 (the NPC's second sub-state, D_008107E9 = 1) |
| overlay 0x824830 | 252 | C | overlay_AREA04 | a04b_03 f627 ([3]'s first stage) |
| overlay 0x823920, 0x823A90 | 124, 128 | C | overlay_AREA04 | a04b_03 f696 |
| 001F91C0 | 1,172 | NM | unknown_03 | a04b_03 f696 |
| 001BBD20 | 56 | CL | math_vector | a04b_03 f1215 (the lift's doors) |
| 001BD180 | 232 | BM | math_vector | a04b_03 f1216 (the keyed lift's mover) |
| overlay 0x8239A0 | 240 | C | overlay_AREA04 | a04b_03 f1225 |
| 0012E3A0, 0012E560, 0012E840, 0012EB60, 0012FC10 | 440, 732, 792, 1,436, 2,464 | AW, NM, BM, BM, NM | lowmem | a04b_03 f1949..f1951 (the creature) |
| 001BC740 | 284 | BM | math_vector | a04b_03 f1949 (001BC960's Use offer) |
| 00131ED0, 00132490, 001328D0, 001333F0, 00133640, 00133A20, 00133DB0 | 72, 1,084, 1,752, 580, 916, 676, 120 | BM, NM, NM, BM, NM, BM, AW | entity_logic | a04b_03 f1950 |
| 001B1560, 001B2B10, 001B32F0, 001B3440, 001B4810 | 108, 112, 160, 164, 1,240 | BM, BM, NM, NM, BM | math_vector | a04b_03 f1950..f1951 |
| 0021BE40 | 144 | AI | area_state | a04b_03 f1950 |
| 001A7B80, 001A7BA0 | 20, 2,748 | BM, NM | level_world | a04b_03 f1977 |
| 001B55E0 | 424 | NM | input_io | a04b_03 f2000 |
| 001ED100 | 840 | NM | hud_objects | a04b_03 f2000 |
| 0011E0A8 | 156 | AW | lowmem | a04b_03 f1954 |
| 0012F100 | 1,468 | NM | lowmem | a04b_03 f2109 |
| 00131F90, 00132FB0, 001B2BF0, 001B30E0 | 1,280, 1,076, 260, 360 | NM, NM, AW, NM | entity_logic / math_vector | a04b_03 f2119 |
| 001B3580 | 228 | BM | math_vector | a04b_04 f474 |
| 001BC560 | 364 | NM | math_vector | a04b_04 f526 (the Use at button [54]) |
| 001BD270 | 248 | BM | math_vector | a04b_04 f732 (the lift's sub-state 3) |
| 001BD9F0, 001BDE60 | 684, 344 | NM, AW | math_vector | a04b_04 f1713 (a04b_exit: AREA13's first frame) |
| 001BC860 | 244 | BM | math_vector | a04b_04 f1714 (a04b_exit) |
| 001BC6D0 | 112 | BM | math_vector | a04b_04 f1860 (a04b_exit) |

a04b_03's census replay ran 136 frames longer than the recording (the
creature's fight had begun); the functions first hit after f2017 (0012F100,
00131F90, 00132FB0, 001B2BF0, 001B30E0) belong to that fight, not to the
recorded frames. The captions in the First column name the beat's event at
that frame, not the function's role. To check a port translation of any of
these, grep the port (`grep -rlI -i <address> src tools`); a name is not
evidence.

AREA04 overlay functions not hit in the a04b replays: 0x823540 (the pad),
0x823580 (door [45]'s locked-branch turn), 0x823B10, 0x823B40, 0x823B90
(the director, freed at the load), 0x824100, 0x824930 ([3]'s second
stage), 0x824A40, 0x824A90, 0x824EF0, 0x825040, 0x8251C0 and 0x825280.

## 6. Tried and not kept (exploration runs, this session)

Scratch snapshots under `../Extermination/build/s88_next/` and the fix
round's probes under `build/s88_fix/` (both deleted at the session's
end); leads, not ground truth.

- **The collapse's pit** (from a06_04's end): walking east along the
  fallen beam descends (y 38.8 at x -268 to 34.5 at x -252) and drops to
  the pit floor at x -243; the pit's east slope (attribute 0x04, 45
  degrees) stops the walk at x -221.4. The pit's collision (AREA06 sub 0)
  holds no ladder, ledge or hang attribute (only the slide 0x35 into it
  and the bar 0x34 over it).
- **The jump straight east** from the pad's lip at z -584 was not taken
  (the beam under the lip).
- **The creature at the lift** (0012E3A0; its node is the pool's next free
  record: 0x7C2660 on the pistol branch, 0x7C3510 with the shotgun, 0x7BF470 in
  the port's loaded game): its halfword
  +0x34 went 200 -> 195 -> 190 on successive pistol hits (R1 held, Circle
  pressed; D_00810C62, the rounds in the pistol, went down by one a shot)
  and 185 -> 170 -> 155 on Square melee hits. Three shooting runs: (1)
  from the balcony, 32 shots, +0x34 down to 110, the player's health 90 ->
  0 (hits of 20 to 25 from the creature, bites raising infection to 100);
  (2) retreating along the balcony, the same end; (3) down the conveyor
  into the hall: the creature did not follow (it stayed at the lift,
  distance 151.9), and with infection at 100 the player's health fell by 3
  about every 170 frames to 0. The pad bits: 3B74 = 0x80 (Square), 3B76 =
  0x40 (Cross), 3B78 = 0x20 (Circle), 3B7C = 0x08 (R1), 3B7E = 0x02 (R2)
  in the raw pad byte order; R1 held and Circle pressed fired
  (measured), which matches PLAYER_WEAPON_STATES_A.md's armed forwarding.
  The fight is not needed for the lift (a04b_04).
- **The lift car** after the reader, first session: walking in to (585.8,
  54.9, 260.9) and pressing Cross facing +x, -z and +z for 1,700 frames
  changed neither the lift (state 1, sub-state 2) nor the buttons: the
  player was never at button [54] (561.2, 242) facing -x. The fix round's
  probe (the same approach as a04b_04) took the Use at the first press.

## 7. Open

1. (Recorded in `NINTH_LEVEL_ROUTE.md`, group `a13`, up to hole [5] and
   AREA19 entry 9.) AREA13 (entry 0 recorded at a04b_04's end): its rooms, the room moves
   [15] / [16] / [17] (001BD9F0, flag 0x1C), the holes [5] / [6] (item
   0x27) and the falls into AREA19 (00193EB0), recorded to its exit; an
   AREA13 census group (the owners and the overlay armed).
2. Answered by the fight's recording: section 8.
3. Whether the beam's collapse is ever meant to happen before AREA15 (the
   world graph finds no way out of the pit in sub 0; section 6).
4. Healing: the player holds no medical item on this route (item 0x20,
   the M.T.S. bed's, is held once); health is 45 and infection 80 at
   AREA13's arrival. See `NINTH_LEVEL_ROUTE.md` section 4 for the item
   counts measured at that arrival.
5. AREA22's reader [9] (item 0x23 now held) and the room behind door
   [10], and AREA04's lift [56] (item 0x29, sub 1 only): not played.
6. a04b_00's census replay never gets door [45] to take the Use (above).


## 8. The first boss fight (lane F1, 2026-10-30)

Recorded on the PCSX2 fork with a scripted pad and no memory writes
(`tools/capture_boss_fight.py`; `../Extermination/build/fork_refs/boss_fight_20261030/`:
beats, whole-run stores `store` / `g_store`, per-tick tables, every rand() call).
The replays run open loop: every row of both chains repeats exactly.

| Beat | From | Frames | What |
|---|---|---|---|
| `a04b_g0_shotgun` | a04b_01 end | 356 | The take, the fit |
| `a04b_g1_door45` | g0 | 756 | Door [45] out |
| `a04b_g2_reader` | g1 | 2413 | The reader, the creature spawns |
| `a04b_g3_fight` | g2 | 1647 | Six shells, dead at f887 (health 100, infection 50), control back |
| `a04b_g4_drop` | g3 | 287 | The drop, its page |
| `a04b_g5_lift` | g4 | 1973 | Button [54], the keyed ride, AREA13 entry 0 |
| `a04b_s5_fight` | a04b_03 end | 1936 | Pistol: dead at f1166 (health 55, infection 40, 19 rounds left) |
| `a04b_s6_drop` | s5 | 287 | The drop |
| `a04b_s7_lift` | s6 | 2409 | The ride |

**The SHOTGUN UNIT.** LOWER U.R.S. entry 1: item type 1, D_00810C65, placement
g[4] of AREA04 (node 0x7A6200, behaviour 00219550) at **(416.1, 15, 102.2)**, in
the room behind door [45], on the way to the NPC [2]. The take gives 6 shells
(D_00810CA8). It opens the V.U.R.S. page on it (t[5] = 3): Cross, Cross, Left,
Cross fits it (D_00810CA6 = 1), Triangle leaves. The ZOOM scope (g[2], D_00810C6C,
index 8) lies at (434.1, 15, 79.8). **The recorded route took neither:** a04b_02's
Cross presses are not these pickups (the recorded a04b_02 / a04b_03 ends hold C65 =
0, CA8 = 0); the guide's room is right. In the R1 stance Square fires one shell
(0017A970 -> 0017A8B0(p, 3), +275 = 3, 00171B00: sound 0x5DC), Cross toggles +275.
Leaving the part page reloads 001FEF70's bank, module 0x32, into sound group 3:
that bank holds the attached weapon's cues (R1 stances 0x5DC, R2 stances 0x5DD,
holster 0x5DE..0x5E0), which are absent in every area without an attachment. R2 is
the first-person stance (action 0x32; THIRD_LEVEL_ROUTE.md), not L1 as the guide's
walkthrough says; its control list is right.

**The creature.** +0xD = 3, +0x34 = 200. It arms with +0x56 = 600 only while the
player stands at y >= 47 inside the 16-vertex polygon D_00245020 (001333F0, +0xD
bit 1). Otherwise it stands in state 9 (00131510: clips 0x30..0x32), which sets
+0x6A = 100 every frame, and 001B41F0 refuses hits in state 9 or from a player
below y 47, so it cannot be shot from the conveyor; falling below y 50 puts it
back at (542.3, 55, 276.4). Within 50 units planar, a 45 degree cone and sight
(00133640) it attacks: within 24 state 5 kinds 0 / 1 (bites of 20 or 25,
D_008104D4), beyond that state 6 or state 5 kind 2, a spit (hazard 001E2E80,
+0x22C = 10 infection). States 3 and 7 are out of reach for it: their guards test
+0xD bit 1. State 8 needs +0x54.

**Damage.** Pistol 5 a hit (60 rounds in all: 30 + 30), melee 15 with +0x6A = 30.
The shotgun's 00186A60 sends 10 pellets, each 42 under 20 units, 35 under 50, else
25; every pellet stores the creature's +0x36, so a shell does **one** pellet's
damage: 200 -> 158 -> 116 -> 74 -> 32 -> 0 at under 20 units (ticks 3681, 3730,
3867, 3998, 4416), five shells of six.

**The green core.** The creature's hull (+0x58) holds 19 records. Two of them, on
bone 4, about 10 units above its feet, carry 0x80 in their second mask byte, and
its +0x5D is 0x81. 001A6440 stores that byte's upper bits (D_700030CC = record[1]
& +0x5D & 0xFE), 00186A60 passes it to 001B41F0 as the flag: a hit there gives
effect 0x80000076, sound 0x15D, +0x36 | 0x8000, and 00133A20 multiplies by 5 with a
stagger. The pistol run's first hit is one: 25 (a04b_s5, tick 3096). The shotgun
run never hit it; by the code a shell there does 42 x 5 = 210, a kill at once
(inferred, not recorded). That the two records are the visible green core is
inferred from where they lie.

**Death to control (answers section 7's item 2).** 00133A20: +4 = 2, 001B4CF0
(effects 0x8000006F / 70 -> node 001F4840, pieces models 0x2A..0x2E, handler
001EDE40). **00131B10 state 0 writes counter 0x12 = 1** (+0xD bit 1; ticks after
the death: 62 pistol, 52 shotgun). [3]'s 0x824930 then starts script 0x8286D0
(+0x240 stays the node, 3B8D = 3, then 2), whose op 06 sets the counter to 2
(001BA120), 00131B10 state 3 -> 1. The creature is freed about 330 ticks after
it died. 0x824A90 moves the camera; the player is placed at (539.4, 55, 269), yaw
-1.88. 0x824A40 spawns group 0x827230 (the drop) and two effects: 0x8000005B, the
node 001F7E40, a grey veil of 32 screen sprites a frame, and 0x80000004, the
node 001F77B0, the **purple pool** (position the frame slot sp - 0x10, which
0x824A40 never writes: the stack's leftover, in both recordings (544.9, 54.3,
255.7), the drop's place). Script op 07 sets flag 0x12 = 0xFF (001B87F0); 0x824930
sub 2 sets the counter to 0xFF (pc 0x824A20). The spores 001F88C0 run on the
creature from its state 2 (00131650), rand()'s by the fourth frame.

**The drop.** Behaviour 0015AFA0, model 0x57, at (544.9, 55, 255.7), at the same
place in both runs: in the pool at the foot of the lift, not where it died. Use
gives D_00810C91, item 0x2D, line 45 of the catalog: **DOGTAG 1, Private First
Class Hans Grant** (CURIOSITIES 9), with its item page. The world graph lists no
gate on item 0x2D. The conveyor tops out near x 505 on the same line (z about
259), so the tag lies about 40 units past its end, on the flat before the lift;
the cheat list's "treadmill" is at best this end of the room.

**Does beating it matter?** Not for the lift: a04b_04 rides past it alive. Flag
0x12 = 0xFF changes the lift's button text line (001BC960) and, by the code, [3]
despawns at the next AREA04 load; with the flag the lift still leads to AREA13
entry 0 (g5, s7). What it gives is the dog tag.

**Native status.** `make test-boss-fight` (tools/test_boss_fight.py) plays both
chains from their cards: every row from the reader's lock byte through AREA13's
load is identical: position, heading, action, health, infection, creature, spawned
bugs. Shotgun 4,444 rows (card `boss`, a04b_g2 f1209 .. a04b_g5 f1302); pistol
5,194 rows (card `a04b_03`, to a04b_s7 f1763); the hall's bugs too, compared whole
from the store. `tools/mid_route_beats.py bosst`: the take and the fit from card
a04b_02 (a04b_g0, a04b_g1), end block equal. These are anchored: a load puts every
bug at its placement (one beside the reader, which bites a walking player and dies,
a bug the recording still has) and restarts the glow and face timers and the clock
0x70003B68, and the steered walk to the reader is not row-exact, so the anchors
(EM_START_AT_RESTORE at the script's start and at the lock, EM_START_AT_RANDS and
EM_START_AT_SYNC at the lock; docs/STARTUP.md) supply those bytes and nothing is
written after the lock. A normal `EM_START_AT=boss` run plays the same code on
other draws. AREA13 is not loaded: the pack does not hold it and the run fail-stops
at that request, the end of this scope. Not compared: pixels (the frames at g3's
end match the recording's by eye, pool included; the tag shows at g4) and sound.
Free pad runs (scripted random stick, fire, melee; shotgun and pistol cards; no
memory written after the lock): standing still, shooting, melee and wandering down
the conveyor (state 9) all end with the creature or the infection killing the
player, game over and the title, with no fail-stop. The Start menu's hub opens and
closes with the unit fitted; SPR4 reached through Start is not compared with the
original (the take's own page is, by its end block). Model 0x35 (D_00810CA6 = 3)
has nodes with binds of their own and is not exported: fitting it fail-stops at
the hub, named.

## Fork continuation work (2026-10-18)

The missing fork chain was reproducibly stopped at `a04b_00_door45`.
The isolated retry in
`../Extermination/build/fork_refs/levels-recordings-20261018-1/`
matched the retained failed trace's rows and input timeline exactly. The
read-only per-frame probe in `levels-recordings-20261018-5/` found that
all five Use edges failed one of two requirements: the player was still
in the idle animation blend-out substates, or the door was out of range.
The door was published in the interaction list, with its lock bit set;
the global interaction gates were clear.

The source distinction is `00161020`: its idle cases 1 and 2 call the
Use dispatcher `00160220`; its blend-out cases do not. A zero animation
clip does not establish that the player is in a polling state. Waiting
with neutral input for idle case 1 fixed the door. The two-frame press
then completed `a04b_00_door45` in 1,444 sampled rows, and the saved state
resumed in a separate hidden session (`levels-recordings-20261018-6/`).
No game-memory edit or teleport was used.

`levels-recordings-20261018-7/` continued through the NPC and the return
through door 45, both resume-checked. Its reader attempt exposed the
other problem with waiting for clip zero: the original idle clip is
selected through `00174A50` / `0017B490`, so nonzero clips can also occur
while the player has control. Waiting for the actual idle polling state
instead completed the reader in `levels-recordings-20261018-8/`.

That branch reached the lift with too little health and died before
using its button. A separate east-side approach in
`levels-recordings-20261018-9/` also died. These failures are retained as
private diagnostics, not successful route receipts. The carried healing
item requires a medical station (`002160B0`, kind 2); the successful
original fork side beat `a02_s0_mts_bed` already records that recovery.
`levels-recordings-20261018-10/` rebuilt the route from that
checkpoint, starting `a02_00_duct` from the recovered state and replaying
all 29 intervening main-line beats. It then passed all five AREA04 return
beats, all six first-visit AREA13 beats, the three AREA19 beats and the
return ladder. This is an explicit fork branch with
its own source-state provenance, not an adjustment to a captured health
field. The continuation now reaches the AREA03 elevator, including all
retained side beats; the final treatment prerequisite is documented in
FIFTEENTH_LEVEL_ROUTE.md.

The return to AREA13 exposed a second recorder defect: its 12 hardcoded
owner addresses all referred to different slots on the recovered branch.
`a13b_02_button15` completed its room transition, but the helper watched
the wrong button and timed out. The sampler identities for all ten
earlier groups matched their original anchor snapshots; the mismatch
starts with the AREA13 reload (`levels-recordings-20261018-10/owner-audit.json`).
The wrapper's `--owner-binding` compares class/model, param/UID,
kind/link, table index and callback against the original anchor snapshot.
Every rebound owner must have exactly one match in the live actor list.
These are the placement fields copied by `001B6990`, not moving position
or animation fields. Only sampler read addresses change.

`levels-recordings-20261018-11/` re-recorded door 17 with the corrected
sampler, then passed the other four AREA13 return beats and all seven
battery-machine beats, including the cure and blast. The eight rooftop
beats passed in `levels-recordings-20261018-12/`. For their sampler,
13 older watched slots retain their validated earlier bindings and five
new owners match the later anchor. The recorded ancestry between those
anchors stays in the same area/sub-area. Freed slots can be reused; as
in the original fixed-slot sampler, a retained key is not proof that
its former actor is still alive. Each trace records the bindings and
anchor hashes. The original route predicates are unchanged.

The reusable wrapper is `tools/record_level_routes.py`, run with the
decomp's `.venv/bin/python` on macOS arm64 (the fork runs under Rosetta).
It imports the decomp capture tools, uses their hidden owned sessions
and shared `build/.pcsx2.lock`, stops on failure, and leaves detailed
logs and all game data inside ignored `build/fork_refs`. Its
`--idle-state-wait` policy changes neutral controller timing only;
`--source-beat a02_00_duct=a02_s0_mts_bed` records the branch explicitly.
The wrapper rejects game-memory writes during route driving.

`LEVEL_ROUTE_EVIDENCE.json` indexes accepted supplemental trace hashes
and their completed saved-state resume checks. `level_readiness.py`
reads that index without replacing any canonical capture. The boundary
recorder (`tools/record_level_boundaries.py`) has captured 88 missing
neutral ticks across 28 boundaries in `levels-boundaries-20261018-1/`
through `-8/`. Every final row matches every sampled field of the next
beat's first row. The readiness tool rechecks the consecutive counters,
source-state and trace hashes, complete endpoint rows and passing
receipts. No between-beat counters remain unrecorded on the indexed
supplemental branch. All 164 retained route beats now have fork captures;
115 have supplemental recording/resume receipts. No new census, native
pixel result or legacy-parity result is inferred from these captures.

Validation: 18 readiness/receipt checks and 10 sampler/helper checks pass.
The stage-4 isolated staged-index build receipt is
`build/levels/recordings-index-build-1.log` in the port worktree.
The hidden-session registry is empty and the shared run lock is free.

## Fork recording supplement (2026-10-18)

These supplemental recordings use the original game on the project fork. They are separate from the legacy tables above. The recovered route starts from the already-recorded AREA02 medical-station branch, then replays the intervening route; each trace records its actual source state. No game-memory edits or teleports are used. The input wait changes and recovery decision are described in EIGHTH_LEVEL_ROUTE.md.

12 beats in this level have passed a separate saved-state resume check. Trace hashes, capture-driver hashes and completed run-status receipts are indexed in LEVEL_ROUTE_EVIDENCE.json. All game data stays local under the paths below.

| Beat | Source beat | Sampled rows | Private receipt folder in decomp |
|---|---|---:|---|
| `a01v_00_catwalk_east` | `a06b_01_door1` | 555 | `build/fork_refs/levels-recordings-20261018-10/s87/route_a01v/a01v_00_catwalk_east` |
| `a01v_01_gap_jump` | `a01v_00_catwalk_east` | 177 | `build/fork_refs/levels-recordings-20261018-10/s87/route_a01v/a01v_01_gap_jump` |
| `a01v_02_door18` | `a01v_01_gap_jump` | 690 | `build/fork_refs/levels-recordings-20261018-10/s87/route_a01v/a01v_02_door18` |
| `a04b_00_door45` | `a22b_01_door6` | 1242 | `build/fork_refs/levels-recordings-20261018-10/s87/route_a04b/a04b_00_door45` |
| `a04b_01_npc` | `a04b_00_door45` | 8690 | `build/fork_refs/levels-recordings-20261018-10/s87/route_a04b/a04b_01_npc` |
| `a04b_02_door45_out` | `a04b_01_npc` | 714 | `build/fork_refs/levels-recordings-20261018-10/s87/route_a04b/a04b_02_door45_out` |
| `a04b_03_reader` | `a04b_02_door45_out` | 2414 | `build/fork_refs/levels-recordings-20261018-10/s87/route_a04b/a04b_03_reader` |
| `a04b_04_lift` | `a04b_03_reader` | 2083 | `build/fork_refs/levels-recordings-20261018-10/s87/route_a04b/a04b_04_lift` |
| `a06b_00_trigger_jump` | `a06_03_room_out` | 780 | `build/fork_refs/levels-recordings-20261018-10/s87/route_a06b/a06b_00_trigger_jump` |
| `a06b_01_door1` | `a06b_00_trigger_jump` | 740 | `build/fork_refs/levels-recordings-20261018-10/s87/route_a06b/a06b_01_door1` |
| `a22b_00_to_door7` | `a01v_02_door18` | 1018 | `build/fork_refs/levels-recordings-20261018-10/s87/route_a22b/a22b_00_to_door7` |
| `a22b_01_door6` | `a22b_00_to_door7` | 886 | `build/fork_refs/levels-recordings-20261018-10/s87/route_a22b/a22b_01_door6` |

These are fork recording and resume results. They do not certify native gameplay, replace the legacy reference path, or constitute a new instruction census. 0 indexed boundary receipts capture 0 otherwise missing ticks and match every endpoint field. 0 ticks between these supplemental beats still lack boundary receipts. Missing legacy counters remain labelled as such.


## Shared-entry audit status (2026-10-20)

All 40 U rows for this level in the checked-in readiness map were audited:
28 have passing original/native function tests at their stated boundaries,
11 remain intentional hardware adapters, and `0015BCF0` is a confirmed
protected-owner mismatch with a strict failing oracle. Where relevant, passing counts include the new unwired initialization
candidates; their existing live bindings retain partial scope. No assertion was weakened and `LEVEL_READINESS.md` was not regenerated.
See `LEVEL_ORACLE_AUDIT.md` for the per-entry evidence, defect causes, exact
proposal for main, and the four level-15 source blockers. The native application
is unchanged: New Game PASS 9.599849 and both smoke tick logs byte-identical.
This does not certify a native replay of this route.
