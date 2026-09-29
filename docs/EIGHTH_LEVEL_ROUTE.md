# After the keypad: eighth-level route, original ground truth

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
- **Not recorded: AREA13 itself** (its rooms, and its exits to AREA19),
  nor the second half of the order.

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
- **The creature at the lift** (0012E3A0, node 0x7BF180): its halfword
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

1. AREA13 (entry 0 recorded at a04b_04's end): its rooms, the room moves
   [15] / [16] / [17] (001BD9F0, flag 0x1C), the holes [5] / [6] (item
   0x27) and the falls into AREA19 (00193EB0), recorded to its exit; an
   AREA13 census group (the owners and the overlay armed).
2. [3]'s second stage (0x824930, flag 0x12 = 0xFF): who writes counter
   0x12 = 1 at the lift (the only direct store is 00131B10's, which ran in
   no replay), whether beating the creature matters, and what flag 0x12 =
   0xFF gates later (by the C, [3] despawns at the next AREA04 load only
   when it is 0xFF; with counter 0x12 = 0x10 its first stage respawns the
   creature and the bugs, inferred from 0x8246B0 / 0x824830, not played).
3. Whether the beam's collapse is ever meant to happen before AREA15 (the
   world graph finds no way out of the pit in sub 0; section 6).
4. Healing: the player holds no medical item on this route (item 0x20,
   the M.T.S. bed's, is held once); health is 45 and infection 80 at
   AREA13's arrival.
5. AREA22's reader [9] (item 0x23 now held) and the room behind door
   [10], and AREA04's lift [56] (item 0x29, sub 1 only): not played.
6. a04b_00's census replay never gets door [45] to take the Use (above).
