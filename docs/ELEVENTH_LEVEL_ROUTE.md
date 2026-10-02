# AREA13's battery machine [44] and the boom: eleventh-level route, original ground truth

Lane NEXTCAP (capture), 2026-10-01 (session s91). Original executable
SHA-256 `ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

`TENTH_LEVEL_ROUTE.md` section 7 left the story at AREA13's door [20] /
hatch [63] side, which no walk from AREA13's field reached, and the last
kept snapshot (a13b_05) at AREA04 entry 7. This document derives the next
step from the code (section 2: AREA13's [44] is a battery-powered machine
whose event changes AREA13's collision) and records it as one opt-in beat
group of `../Extermination/tools/route_capture.py`, `a13c` (7 beats),
from a13b_01's end (outside door [17], entry 9). Hidden PCSX2, exact
one-frame steps, pad input only, no teleports and no memory writes, one
trace and one resumable snapshot per beat. Nothing here is port behaviour.

**Status.**

- **No new area was reached.** The group stays in this AREA13 load. The
  hatch [63] / door [20] side (AREA19 entry 10, decomp WORLD_GRAPH.md
  section 7 step 5) is still not reached (section 6, section 7).
- **Played (main line a13c_00 .. a13c_06):** the recharger [60] (battery
  charge 0 -> 12); the roof and its walkway to [44]; [44] with the battery
  (charge 12 -> 4, flag 0x1C = 1); the blast and [45]'s sequence (flag 0x1C
  = 0xFF); the roof's north part (now open) and the stair to the ground
  north of the building, where the pickup g[3] (item 0x22) cures the
  infection (health 100, infection 0); back up, down the fallen boom from
  [44]'s platform to [7]'s area (counter / flag 0x42 = 0xFF, two actors
  spawned) and on into the region south of the pipe fence, which no earlier
  level reached.
- **Why the group starts at a13b_01, not at a13b_05.** The a13b trip down
  the lift to AREA04 entry 7 (a13b_02 .. a13b_05) is a detour on this
  route: played on from a13b_05 (exploration, section 6) a second bite at
  the AREA04 lift raised the infection 90 -> 100, and with the infection at
  100 the health fell from 44 to 9 in about 6,200 frames; the only cure
  found, item 0x22, lies behind [44]'s event, about 10,000 frames of play
  away from there (this group's beats plus the lift ride and the walk from
  the lobby). a13b_01 is the last kept snapshot with infection 80 (no
  drain).
  The code gives nothing new to do at AREA04 entry 7 (bit 2 of D_00810845
  still 0, no item 0x29; decomp WORLD_GRAPH.md section 7): the lift back up
  is the only new move there (section 6).
- **The player did not die on the kept route.** Health 44 at the start, 29
  after the blast (two hits of 10 and 5 in the field, source not read), 100
  after item 0x22, 100 at the end.
- Group: 7 beats, 10,186 frames, counters 116115..126314 (counter 116114,
  between a13b_01 and a13c_00, and counters 124121..124127, between
  a13c_04 and a13c_05, are not recorded).
- **Census** (section 5): 43 functions (27,492 bytes) that no earlier level
  ran: 25 boot, 18 AREA13 overlay.

In-game text (messages, prompts, item names) is described, never quoted.
Positions are world units; yaw is the player's +0xC4 (X = sin, Z = cos).

## 1. Tool, outputs and conventions

```sh
# decomp repo, .venv python, repo root (hidden PCSX2; hold build/.pcsx2.lock)
.venv/bin/python tools/route_capture.py run --beats a13c
.venv/bin/python tools/route_capture.py events --beats a13c
.venv/bin/python tools/route_capture.py verify --beats a13c
```

`--beats all` and every earlier group are unchanged. a13c_00 starts from
`build/s87/route_a13b/a13b_01_door17/state.p2s`; each later beat from the
previous beat's snapshot. Outputs: `../Extermination/build/s87/route_a13c/<beat>/`
(ignored): `trace.json`, `state.p2s`, `eeMemory.bin`, `gs.bin`,
`scratchpad.bin`, `original.png`, `snapshot.json`. Every kept snapshot was
reloaded after its capture (`run_beat`'s check) and resumed. Six resumed on
the first check (`tail_idle_frames` 0). **a13c_04 was captured twice**: its
first snapshot failed the resume check, and `run_beat` captured the beat
again with 23 idle frames before the snapshot (`tail_idle_frames` = 23 in
its trace.json; its 2,182 frames include them), and that snapshot resumed.
The kept log folders show it (`route_a13c/logs/`): a13c_04's check folder
was first created at 18:30, the second capture ran 18:31..18:32 and its
check passed at 18:32. The kept beats all come from one run of the group
(18:24..18:36). An earlier run of the group (log folders from 17:55)
reached a13c_04 and stopped during its capture, before any check. The kept
logs do not record why (each attempt overwrites them). Its a13c_00..a13c_03
outputs were replaced by the second run. The capture and census runs were started through a
scratch wrapper of this lane that raises the DebugServer / Pine socket
timeouts to 60 s in its own process (as TENTH_LEVEL_ROUTE.md section 1)
while the lane held the shared PCSX2 lock.

- **Rows**: the a13 decoder (NINTH_LEVEL_ROUTE.md) with this group's
  owners. `s770` [8:10] is flag 0x1C (D_00810774), `s7f0` [8:10] counter
  0x1C (D_008107F4), `s798` [4:6] flag 0x42 (D_0081079A), `s818` [4:6]
  counter 0x42 (D_0081081A), `s830` [6:8] D_00810833, `c86` item 0x22
  (D_00810C86), `charge` D_00810CB2 (half units), `hp` D_008104D0, `inf`
  the player's +0x228.
- **Helpers**: the a13 / a19 helpers (`a13_settle`, `a13_face`, `a13_use`,
  `a13_control`, `a19_press_until`), `next_prompt_yes`, and this group's
  `a13c_walk` (a04_go point by point with every stop checked against a
  slack of 4 units, tolerance 1.0 on the stair landing), `a13c_take` (walk,
  face, Cross retried at once: on the ground north of the building the
  player is hit about every 110 frames by an attacker that was not
  identified, section 6), `a13c_roof_ladder` and
  `a13c_control` (control kept for 30 frames with 3B8D = 0).

### 1.1 Owners sampled

Measured with `tools/area_overview.py --area 13 --ram
build/s87/route_a13b/a13b_01_door17/eeMemory.bin` (placement table 0x82D570
[n], deferred group 0x829D00 g[n]). The whole group stays in this load, so
the nodes hold; [44] and [7] free themselves (a13c_03 f2158, a13c_05
f1815) and their slots are reused by later spawns, so their rows are
meaningless after that. The two 0x824BB0 rows carry the keys `sentry_g19` /
`sentry_g21`, the keys of the kept traces and census replays (the tool's
`A13C_OWNERS` uses them again, so `events` and `verify` read those rows).
The key name is only a key: whether these nodes attack the player is not
established (section 6).

| Row key | Node | Behaviour | Record |
|---|---|---|---|
| `pick_g3` | 0x7A5F10 | 00219550 | g[3] (703.6, 158.9, 1334.8): item 0x22 |
| `sentry_g19` / `sentry_g21` | 0x7A8B20 / 0x7A9100 | overlay 0x824BB0 model 0x1A | g[19] (759.7, 205, 974.9) / g[21] (724.9, 205, 1280) |
| `r7_824A80` | 0x7AB440 | overlay 0x824A80 | [7] (688, 163, 1041) |
| `door17_r17` / `door20_r20` | 0x7AD1A0 / 0x7ADA70 | overlay 0x823580 / 001BC350 | [17] / [20] |
| `r44_823E90` | 0x7B20F0 | overlay 0x823E90 model 0x25 | [44] (798.4, 215, 1149.5) |
| `r45_827150` | 0x7B23E0 | overlay 0x827150 model 0x28 | [45] (827, 156.9, 1130.4) |
| `r47_8293A0` | 0x7B26D0 | overlay 0x8293A0 | [47] (706.2, 160, 1047.7) |
| `r49_8292A0` / `r51_8292A0` | 0x7B29C0 / 0x7B2FA0 | overlay 0x8292A0 | [49] (766.5, 235, 1267.9) / [51] (718, 215, 1246.9) |
| `recharger_r60` | 0x7B4A10 | 00159210 model 0x2C | [60] (774.8, 160, 1237.9) |
| `hatch_r63` | 0x7B52E0 | overlay 0x826850 | [63] (1081, 160, 845) |

## 2. What decides each step (code, then capture)

Evidence marks as in decomp docs/WORLD_GRAPH.md: BM byte-matched C, NM
NEARMISS C, C overlay C linked byte-identical.

- **[44] is a battery machine.** 00185420 (BM) returns the first published
  owner that 00184D20 (BM) accepts for the item in use; 00184D20 accepts a
  class-4 owner of model 0x25 for the battery items 0x1B, 0x1C and 0x1D
  only. [44] is placement class 0x84, model 0x25. Its step 1 (0x824180, C):
  Use (+0xB bit 2) without +0xB bit 0 starts script 0x82B090; at its end
  D_008106B1 = +0x34 + 0x80 = 0x84 (+0x34 is 4, set at [44]'s state 0,
  0x823E90 C) and D_008106B0 = 1, D_008106D0 = [44]'s +0x14. The battery
  page 002149F0 (NM) takes D_008106B1 bit 7 as the confirmation for owner
  D_008106D0 with the cost +0x34: Yes discharges 2 * 4 = 8 half units and
  marks the owner +0xA = 1, +0xB = 5. Back in step 1 the Use is then taken
  with +0xB bit 0: script 0x82B2D0, flag 0x1C (D_00810774) = 1, and
  counter 0x1C (D_008107F4) += 1 (step 2).
- **The charge.** The route reaches [44] with D_00810CB2 = 0 (spent at
  AREA02's panel, FOURTH_LEVEL_ROUTE.md) and capacity D_00810CB7 = 12. The
  recharger [60] (00159210, BM, model 0x2C) runs 00157860 (BM): with the
  charge below the capacity its model-0x2C path plays script 0x247420; the
  page's recharge path (D_008106B1 bit 6) refills up to D_00810CB7.
- **Step 2 (0x824390, C)**: 490 frames after step 2 starts, 0x8249F0 sets
  +0x36 = 1 on the chained class-4 model-0x0A owners at y >= 215 (the
  roof's [34], [36], [40]), then: the player above y 210 inside the area
  0x82E140 (the walkway, x 765..815, z 1156..1221) -> script 0x82B3D0,
  whose callback 0x8246D0 moves D_00810350 by -1 per frame and the height
  on a sine arc for 50 frames; inside 0x82E180 (the roof, x 705..777, z
  1207..1278) -> script 0x82B810 (callback 0x8247B0); elsewhere script
  0x82BC90. Both areas also need flag 0x1C != 0xFF.
- **[45] (0x827150, C)**: waits for D_008107F4 bit 1, ORs 0x10, then 0x40
  every frame and runs its step table 0x82D190: step 0 (0x827C30) puts an
  effect at 0x82D1B0[0] (815.6, 242.8, 1145.4) at count 0x1DF and ORs 0x20
  into D_008107F4 at 0x1F3 (with an effect at (743, 230, 1265), the roof);
  step 4 (0x827F90, NM) sets D_00810833 = 0xFF when its +0x3C falls to 4,
  and while +0x3C <= 10 a player inside the area 0x82D330 below y 210 takes
  pending damage equal to the whole health. [44] step 3 releases scripts
  0x82BD10 / 0x82BDD0 / 0x82BE90 by [45]'s step count; step 4 waits for
  D_00810833 and runs script 0x82C110, which ends with flag 0x1C = 0xFF.
- **What the event opens.** [49]..[54] (0x8292A0, C) end once D_008107F4
  bit 5 is set; the walks north on the roof that stopped at their
  positions before the event pass after it (section 6). [47] (0x8293A0, C), loaded with bit 6 clear, takes its second
  pose and calls 0019C6F0(0x1F, 1) / (0x20, 0); with bit 6 set it counts
  0x6E0 frames and then 0019C6F0(0x1F, 0) / (0x20, 1). 0019C6F0 (NM) finds
  the class-0x0B placement with that key ([1] key 0x1F, index 1; [0] key
  0x20, index 0) and clears (1) or sets (0) bit 0x40000000 of that entry's
  word in the cell-list directory. The scratchpad word 0x70003250 holds the
  directory's address (0x018321C0 in every a13c snapshot), and entry n's
  word is at that address + 4 + 4n (the C indexes `base[idx + 1]`), so
  entry 0's word is at EE RAM 0x018321C4 and entry 1's at 0x018321C8.
  Measured in the kept snapshots (`eeMemory.bin` / `scratchpad.bin`):
  a13b_01 and a13c_00 .. a13c_02 hold entry 0 = 0xC000015C and entry 1 =
  0x8000025C; a13c_03 .. a13c_06 hold 0x8000015C and 0xC000025C (the
  exploration snapshots showed the same flip). Entry 1 holds two walls
  (x 800..825, z 1085..1168, y 148..208) that closed the north-east end of
  the fallen boom (the cell-44 walkable top from (816.7, 1117.2) at y 205.7
  to (633, 977.3) at y 179.3) where it meets [44]'s platform; entry 0 holds
  one wall (x 829..849, z 1114..1148, y 200..223) on the platform.
- **The infection.** 002160B0 (BM), the HEALING page: kind 4 (item 0x22)
  sets the health to the cap and spends all of D_0081085C (the infection);
  kind 3 (0x21) spends 30; kind 2 (0x20) needs a target owner (00185420
  with 0x20: class 4, model 0x36). With +0x228 at 100 the INFECTED latch
  (player +0x234) is set (decomp FINDINGS.md, player damage pipeline); the
  measured drain is in section 6.
- **[7] (0x824A80, C)**: with flag 0x42 clear, the player inside the area
  0x82E220 ((690, 1045), (718, 1020), (704, 1001), (677, 1033), tested in x
  / z) starts script 0x82C510 and stores counter 0x42 (D_0081081A) = 0xFF;
  at the script's end it calls 001B6660(0x82A230) (the group's two 0x141D20
  records, (723, 158.8, 723.1) and (765.3, 163, 789.1); the census sees
  0x141D20 run from that frame on) and [7] ends. The area lies under the
  boom (its top is at y ~190 there).
- **[58] / [59] (0x826FF0, C)**: Use starts script 0x82CE10 (x < 800) or
  0x82CFD0; at the end only +0xB / +5 are cleared. Played (exploration) at
  [58]: a 250-frame script, nothing moved.

## 3. Route table

Frames `f` are trace frames; `c` is the main-loop counter 0x70003B64.

| Beat (folder) | Source | Frames | Counters | Presses | What happens |
|---|---|---|---|---|---|
| `a13c_00_recharger` | a13b_01 | 994 | 116115..117109 | Cross f127, f504; Left f683; Cross f705; Triangle f901 | door [17] from outside facing -x: taken f130, **entry 4** f272; the recharger [60] facing +x at (765.8, 1238.2): script 0x247420 f507, request 01 40 f623, the prompt f653, Yes: the charge 0 -> 12 at 2 per 20 frames f711..f811 |
| `a13c_01_to_machine` | a13c_00 | 1459 | 117110..118569 | Cross f165, f662 | door [17] from inside: **entry 9** f330; the roof ladder (Cross f662); the roof east, the walkway to [44]'s Use point (805.3, 214.4, 1153.2) facing -z |
| `a13c_02_battery` | a13c_01 | 1177 | 118570..119747 | Cross f3; Left f864; Cross f886 | [44] takes Use f6: script 0x82B090; request 01 84 f803 (the battery page's confirmation); Yes: the charge 12 -> 4 at 2 per 30 frames f890..f980; script 0x82B2D0 and **flag 0x1C = 1** f1015; D_008107F4 0x01 -> 0x12 f1149 ([44] step 2, [45] sees bit 1) -> 0x52 f1150 |
| `a13c_03_blast` | a13c_02 | 2189 | 119748..121937 | none | back north to (778.8, 214.9, 1206.9) on the walkway (inside 0x82E140); f462 [44]'s 490-frame check: script 0x82B3D0, 3B8D = 2 f464; f471 D_008107F4 = 0x72 ([49] ends f471..f473); the scene puts the player at (750, 161.5, 1147.8) (f600) and runs to f1283 (D_008107F4 = 0x73 f696, 0x74 f1283); the walk to (719.4, 1178.5); hits f1531 (health 44 -> 34) and f1643 (-> 29); D_00810833 = 0xFF f1710; script 0x82C110 f1711; **flag 0x1C = 0xFF** f2157 with the player at (696, 159.7, 1102); [44] ends |
| `a13c_04_cure` | a13c_03 | 2182 | 121938..124120 | Cross f250, f1239, f1635; Left f1648; Cross f1670; Triangle f2035 | the roof ladder; the roof's north part, a ramp down west to the y-210 part, the switchback stair north of the lobby (upper flight y 210 -> 184, the landing at z 1306.5..1306.8, lower flight y 184 -> 158) to the ground north of the building; the pickup g[3] facing +x: taken f1242, **item 0x22** (D_00810C86 = 1) with its HEALING page f1305; the item used: count 1 -> 0 f1673, **health 29 -> 100, infection 80 -> 0** f2035 |
| `a13c_05_boom` | a13c_04 | 1843 | 124128..125971 | none | the stair up, the ramp onto the roof, the walkway, [44]'s platform; a drop onto the boom's north-east end (809.5, 208.6, 1130.0) f914; down the boom south-west; [7]'s area at (703.5, 189.7, 1031.7): **counter 0x42 = 0xFF** f1138, script 0x82C510 (3B8D = 2 f1140..f1813); **flag 0x42 = 0xFF** f1814 and the group 0x82A230 spawned (census: 0x141D20 from f1814); [7] ends f1815 |
| `a13c_06_south` | a13c_05 | 342 | 125972..126314 | none | on down the boom to its south-west end (645.2, 181.1, 985.2), off its south side, down the slope south (y 181 -> 153) into the region south of the pipe fence; control at (637.5, 154.5, 890.5) |

Main line a13b_01 -> a13c_00 .. a13c_06: 10,186 frames, counters
116115..126314; the counter gap is 1 at every beat boundary (no counter
missing) except a13c_04 -> a13c_05 (8: counters 124121..124127 not
recorded) and a13b_01 -> a13c_00 (2: counter 116114 not recorded).

## 4. Story bytes, health and items (measured)

| Byte | Before | After | Where |
|---|---|---|---|
| D_00810CB2 (battery charge, half units) | 0 | 4 | 12 at a13c_00 f811; 4 at a13c_02 f980 |
| D_00810774 (flag 0x1C) | 0 | 0xFF | 1 at a13c_02 f1015; 0xFF at a13c_03 f2157 |
| D_008107F4 (counter 0x1C; [44]'s step in bits 0..3) | 0x01 | 0x74 | 0x12 / 0x52 a13c_02 f1149 / f1150; 0x72 a13c_03 f471; 0x73 f696; 0x74 f1283 |
| D_00810833 (counter 0x5B) | 0 | 0xFF | a13c_03 f1710 |
| D_0081081A (counter 0x42) | 0 | 0xFF | a13c_05 f1138 |
| D_0081079A (flag 0x42) | 0 | 0xFF | a13c_05 f1814 |
| D_00810C86 (item 0x22) | 0 | 0 | 1 at a13c_04 f1305, used f1673 |
| player health (D_008104D0) | 44 | 100 | 34 / 29 a13c_03 f1531 / f1643; 100 a13c_04 f2035 |
| infection (+0x228) | 80 | 0 | a13c_04 f2035 |
| D_00810700..703 | 0D 00 09 0D | 0D 00 09 0D | 0D 00 04 0D a13c_00 f272 .. a13c_01 f330 |

AREA13's lock byte D_0081084E, D_00810839 (the hatches) and AREA19's
D_00810854 did not change.

## 5. Census: what the eleventh level executes beyond every earlier level

**Tool** (`../Extermination/tools/route_census.py`, additive):
`run --segments a13c --pass A13C` replays each beat from its recorded
source snapshot with route_capture's own beat function and one-shot
breakpoints on every boot function and on AREA13's overlay (overlay id 10;
`ELEVENTH_GROUPS`). `eleventh-delta` runs `chain_delta` against the
first-level census, beat 15, the A01, A00, A01R, A02, A04, A22, A01U and A06
passes, the four eighth-level groups, A13, A19 and A13B. Output:
`../Extermination/build/s87/census/a13c_delta.json` (and `runs/A13C/`).

```sh
# decomp repo, .venv python, repo root (hidden PCSX2; hold build/.pcsx2.lock for `run`)
.venv/bin/python tools/route_census.py run --segments a13c --pass A13C
.venv/bin/python tools/route_census.py eleventh-delta
```

**Replay fidelity.** All seven replays completed their beat's own checks
(`beats_incomplete` empty). Rows are compared by the main-loop counter
(`trace_vs_recorded_by_counter` in `runs/A13C/<beat>.json`: rows paired by
counter, identical / paired). The start offset is `counter_offset_row0`
(the replay's first counter minus the recording's); the frame-aligned
`trace_vs_recorded` matters only for a13c_00, where it differs:

| Beat | Frames replay / recorded | Start offset | Rows identical by counter | End digests (player / globals / owners) |
|---|---|---:|---|---|
| a13c_00 | 995 / 994 | -1 | 995 / 995 (exact; frame-aligned 0 / 995 only because of the offset) | equal / 1 word (0x810D98) / equal |
| a13c_01 | 1,460 / 1,459 | 0 | 405 / 1,460 (first difference at counter 117515: camera, position, yaw) | equal / 3 words / 35 words |
| a13c_02 | 1,176 / 1,177 | 0 | 991 / 1,177 | equal / 1 word (0x810D98) / equal |
| a13c_03 | 2,189 / 2,189 | 0 | 2,033 / 2,190 (only `msgrec` / `msg` differ, counters 120859..121015) | equal / 1 word (0x810D98) / equal |
| a13c_04 | 2,270 / 2,182 | 0 | 1,752 / 2,183 | equal / 1 word (0x810D98) / equal |
| a13c_05 | 1,843 / 1,843 | -7 | 0 / 1,837 | 17 / 33 / 44 words differ |
| a13c_06 | 342 / 342 | 0 | 343 / 343 (exact) | equal / equal / equal |

a13c_05's replay is a different walk. The capture's a13c_05 started at
counter 124128: the 7 counters 124121..124127 after a13c_04's snapshot ran
before the capture session's first row (section 3, "not recorded"). The
census replay of the same snapshot started at counter 124121, 7 counters
earlier. The beat's walk is closed-loop (a04_go steers by the sampled
position), and the replay's pad input is not the recording's
(`recorded_inputs_equal_by_counter` false); no row matches by
counter, and the end digests differ for player, globals and owners. It
still passed the beat's own checks (counter 0x42 and then flag 0x42 at
0xFF, i.e. [7]'s area was entered and its script ended), so its
function set is that of the same route shape, not of the recorded frames.
As for the earlier levels, the census sets are those of the route shape,
not of the exact frames. No hit came from another overlay and none is
unattributed.

**Totals.** 922 functions executed (890 boot, 32 of AREA13's 49); **43
new, 27,492 bytes**: 25 boot (byte-matched C 9, NEARMISS 15, asm word 1)
and 18 AREA13 overlay (C 17, NEARMISS 1).

New functions (census frames; the captions name the beat's event at that
frame, not the function's role):

| Function | Bytes | Status | Subsystem | First |
|---|---:|---|---|---|
| 00118418 | 660 | NM | lowmem | a13c_02 f61 (the examine script of [44]) |
| overlay 0x824390, 0x827C30 | 396, 412 | C | overlay_AREA13 | a13c_02 f1149 ([44] step 2, [45] step 0) |
| overlay 0x828E10, 0x828F40 | 284, 856 | C | overlay_AREA13 | a13c_03 f451 |
| overlay 0x8240E0, 0x8249F0 | 124, 132 | C | overlay_AREA13 | a13c_03 f462 ([44]'s 490-frame check) |
| 001CDDC0, 001CE660, 001E4610 | 1,344, 504, 980 | NM | gs_upload, gs_upload, weapon_equip | a13c_03 f464 |
| overlay 0x8246D0 | 224 | C | overlay_AREA13 | a13c_03 f470 (script 0x82B3D0's callback) |
| overlay 0x8248C0 | 148 | C | overlay_AREA13 | a13c_03 f526 |
| overlay 0x824520 | 428 | C | overlay_AREA13 | a13c_03 f697 ([44] step 3) |
| overlay 0x827DD0, 0x827E00 | 44, 288 | C | overlay_AREA13 | a13c_03 f752, f753 |
| 001F4840 / 001F4190 | 440 / 1,704 | BM / NM | fx_render | a13c_03 f753 / f754 |
| overlay 0x827F20 | 112 | C | overlay_AREA13 | a13c_03 f1053 |
| overlay 0x824960 | 136 | C | overlay_AREA13 | a13c_03 f1284 ([44] step 4) |
| 001F9140 | 56 | BM | unknown_03 | a13c_03 f1413 |
| overlay 0x827F90 / 0x828500 | 1,352 / 1,880 | NM / C | overlay_AREA13 | a13c_03 f1413 ([45] step 4) |
| overlay 0x8284E0, 0x828C60 | 32, 428 | C | overlay_AREA13 | a13c_03 f1530, f1711 |
| 00100130 / 0020D930 | 36 / 1,640 | BM / NM | lowmem / ui_screens | a13c_04 f2032 (the HEALING page's commit) |
| overlay 0x826610 | 132 | C | overlay_AREA13 | a13c_05 f62 |
| 00141D20, 00141F00, 00142070, 00142330, 00145880, 001459A0, 00146CE0, 001471E0, 001469B0, 001424C0, 00146110, 001429D0 | 472, 360, 704, 388, 276, 1,904, 660, 428, 320, 1,296, 916, 2,796 | NM, BM, BM, asm word, BM, NM, BM, BM, NM, NM, NM, NM | entity_logic | a13c_05 f1814..f1819 (the group 0x82A230 spawned at [7]'s end) |
| 001464B0 / 001B2E50 / 001B3F10 / 0019A6F0 | 648 / 288 / 732 / 532 | BM / NM / NM / NM | entity_logic / math_vector / math_vector / level_world | a13c_06 f6, f6, f152, f332 |

0x828500, 0x828C60, 0x828E10 and 0x828F40 have no static reference in the
overlay (decomp docs/AREA13_OVERLAY.md, "Unreached code"); this census sees
them run during [45]'s sequence (a13c_03).

To check a port translation, grep the port (`grep -rlI -i <address> src
tools`): 001F9140, 00100130, 0020D930, 001B2E50 and 001B3F10 are
referenced there (em_shadow_actor_route.h; em_hud.c / em_menu_hover.h;
em_status_runtime.c / em_status_pages_live.h; em_level8_port_*); none of the
other boot functions is.

The port does have an AREA13 module: the ninth level's
`src/game/em_level9_port_area13.c` and `em_level9_port_turret.c` (API
`em_level9_port.h`, doc `LEVEL9_PORT.md`, commit 9f40164). It translates
[44]'s owner 0x823E90 and its step 1 0x824180, [45]'s 0x827150, [49]..[54]'s
0x8292A0, [47]'s 0x8293A0, the 0x824BB0 nodes and others (LEVEL9_PORT.md
section 0). **None of the 18 new overlay functions above has a translation
there.** Four of them are already hook slots of that module, called by
address but untranslated: 0x824390, 0x824520 and 0x824960 (`w_00824390`,
`w_00824520`, `w_00824960` in `em_level9_port.h`: [44]'s steps 2..4, called
from 0x823E90's step switch in `em_level9_port_area13.c`) and 0x826610
(`w_00826610`, called from `em_level9_port_turret.c`). [45]'s step
functions 0x827C30, 0x827DD0, 0x827E00, 0x827F20 and 0x827F90 (the step
table 0x82D190, decomp AREA13_OVERLAY.md) are reached through that
module's `w_callback`; LEVEL9_PORT.md's Binding section lists the step
functions behind 0x82D190 as untranslated. A translation lane for these
functions extends that module rather than starting a new one. The other
port hits for these addresses are not these functions: 0x827DD0 in
`em_level10_port_area19.c` / `export_area13_level.py` is AREA19's group
owner at the same address, 0x828E10 in `em_area02_overlay.c` is AREA02's,
and 0x8284E0 in `em_area00_overlay.c` is AREA00's.

AREA13 overlay functions this group did not run (17, from
`overlay_AREA13_not_run`): 0x823700, 0x823830, 0x823940, 0x823A10,
0x823A40, 0x823BC0, 0x823C10, 0x823D50, 0x826FB0, 0x826FC0, 0x829AA0 and
the init 0x8236E0, which ran in earlier levels (the census's
`already_ran`); and, run in no level so far (`already_ran` null), the
entry pad 0x823540 (a 4-byte asm word), 0x824060 (the roof area test: the
walkway test 0x8240E0 answered first), 0x8247B0 (script 0x82B810's
callback: the roof branch), 0x824160 ([44] step 0) and 0x828F30.

## 6. Tried and not kept (exploration runs, this session)

Scratch snapshots and tools under `../Extermination/build/nextcap/`
(probe/, tools/; deleted at the session's end); leads, not ground truth.

- **AREA04 entry 7 and the lift back (from a13b_05).** Cross at once at the
  arrival spot (570.7, 244.6) was not taken by the inner button [54]
  (distance 9.5); a bite at f92 (action 0x3E, infection 90 -> 100; the
  group-0x826790 node 0x7AA2A0 (0012E3A0), the creature the eighth level
  left alive, stood at (548.2, 54.9, 261.3) in a13b_05's snapshot);
  after the bite a walk to (566.5, 244.3) and Cross was taken (f253) and
  the lift ran to AREA13 entry 0 (control f1412). A first attempt that
  waited longer at the button took a second bite (health 44 -> 19) and bug
  grabs and died (health 0, action 0x3F, f2104). With the infection at 100
  the health fell 44 -> 39 -> 34 -> 30 over the 2,003 frames f1412..f3415
  (lobby, door [8], button [16], door [17]) and on to 9 by f7590; the
  player then died at [44]'s Use point (health 9 -> 0 in one frame; the
  cause was not read).
- **The roof's north part before the event.** Walks north on the roof
  stopped at (719.4, 215, 1232.5) and (734.8, 215, 1253.5); after the event
  the same walks pass (section 3, a13c_04).
- **Standing still at [44]'s Use point** (from an exploration state equal to
  a13c_02's end): the player
  died there (health 44 -> 0 in one frame); the session's frame counter
  then restarted (the game left the gameplay loop) and the run was
  abandoned. **Unverified:** the run's notes put the death in the frame
  bit 5 of D_008107F4 was set, 780 frames after it became 0x52. In the kept
  route bit 5 came 499 counters after 0x52 (counter 119720 -> 120219,
  [45] step 0's fixed count 0x1F3), so the 780 frames fit only if [45]'s
  count pauses during script 0x82BC90 (the branch for a player outside both
  areas); that was not read, and the scratch evidence was deleted at the
  session's end, so neither the frame count nor the cause is established. In the kept beat the
  player stands inside 0x82E140 at the 490-frame check instead.
- **Staying at (750, 161.5, 1147.8) after the blast scene** gave the same
  two hits (10 and 5) as the kept walk to (719.4, 1178.5).
- **The fallen boom from the field** (its south-west part near (702, 1057))
  is out of reach from below (the walk stops against the fence at (701.5,
  162.6, 1058.5)); walked from the platform, the boom's north side drops the
  player back into the field at (702, 168, 1057.4) (a walk 18 units north
  of the boom's centre line). [44]'s platform south of the walkway is
  reached only by the drop at (809.5, 208.6, 1130.0) (no climb back up was
  found).
- **The region south of the pipe fence.** It holds g[0], g[1] and g[7]
  (item 0x20), [58] (examine only) and the two 0x141D20 actors, which hit
  the player (health 95 -> 70 within about 1,300 frames near (740, 820)).
  Walks east stopped at (739.7, 162.4, 822.8) and (795.5, 158.1, 790.8);
  door [20]'s north side (entry 10, (1064, 160, 889)) was not reached.
- **Hits of 5 about every 110 frames** (action 0x3E): on the ground east
  of the boom's end (health 29 -> 0 in about 600 frames, exploration) and
  at the pickup g[3] (29 -> 4 in a debug replay with a13_use's slower
  approach). The nearest owners are the 0x824BB0 nodes g[19] (759.7, 205,
  974.9) and g[21] (724.9, 205, 1280), the AREA01 0x826D40 twins (whose
  state-4 path aims and fires through 0x8282F0, port AREA01_REVISIT.md);
  the attacker was not identified.
- **The lobby's pickup g[8]** (item 0x1E, (655.5, 160.2, 1283.1)): a walk
  from entry 1 stopped at (654.7, 160, 1270.7) and Use there was not taken.
- **A collision reachability scan** (scratch, the captured grid plus the
  cell lists honouring the directory's disable bit; a lead): the region of
  door [20]'s north side (x 990..1087, z 836..983) is entered only from
  above, from a strip at y 186..211 (x 831..1106, z 836..1038) that the scan
  joins to neither [44]'s platform nor the south region (x 552..820). Along
  x 826 a walk from the platform's south end reaches (826, 186.6, 1014)
  (exploration); the grid wall at x 831 (z 980..1095, y 150..238) has the
  surface attribute 0x51, which collides only for query id 0 (decomp
  FINDINGS.md, surface attributes).
- **East of the x-831 wall from the platform.** The plateau south of [44]'s
  platform and east of that wall (y 199..213, x 831..890, z 1030..1100,
  with the strip's north end at (884.5, 211.5, 1038) in the scan) is
  entered past the wall's north end (z 1094.5): a walk from the platform's
  south end reached (837.0, 202.8, 1108.4). Walks on stopped at (834.9,
  207, 1102.6) going south and (844.2, 206.7, 1109.5) going east, against a
  diagonal pair of grid walls (y 186..220, from about (832, 1100) to (880,
  1035)) and a post (x 849..854, z 1106..1111); Cross there facing 2.0,
  2.4, 2.55 and 3.0 started no climb.

## 7. Open

1. How the story reaches door [20]'s north side (entry 10) / the hatch
   [63] (AREA19 entry 10). Leads: the strip at y 186..211 east of the
   x-831 wall, whose north end touches the plateau reached at (837.0,
   202.8, 1108.4) (section 6: the diagonal walls between them were not
   passed); the big building between the south region and the strip
   (x 750..900, z 830..1000, roof y 240..315) with g[16] (00219870 model
   0x30, (858.5, 250.8, 835)) and [64] (001C4820, (937.3, 240, 835)) on
   top; [59] (0x826FF0, (991, 160.2, 924.4)) inside the region.
2. The two 0x141D20 actors were not fought; the 0x824BB0 nodes were not
   shot at; who hits the player for 5 is not identified.
3. Where the two hits of 10 and 5 in a13c_03 (f1531, f1643) come from is
   not read.
4. Census gaps: 0x8247B0 and 0x824060 (the roof branch of [44]'s step 2)
   need a variant of a13c_03 with the player on the roof at the check;
   0x824160 ([44] step 0; counter 0x1C was already 1 at the ninth level's
   first AREA13 load, decomp WORLD_GRAPH.md section 8b) was hit by no
   census replay.
