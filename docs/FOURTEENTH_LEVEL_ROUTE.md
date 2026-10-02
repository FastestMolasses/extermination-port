# AREA19's hall, sub 0's west part, the stair tower and AREA15: fourteenth-level route, original ground truth

Lane STORY (capture), 2026-10-02 (session s94). Original executable
SHA-256 `ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

`THIRTEENTH_LEVEL_ROUTE.md` left the story in AREA19 sub 1 at entry 1 (the
hall, after [34]'s scene), with AREA15 not reached: the doors to it ([50] /
[51]) open from the landings of a stair tower whose door from the hall, [49],
is sealed from the tower side. This document derives the way into the tower
from the code and the grids (section 2) and records it as two opt-in beat
groups of `../Extermination/tools/route_capture.py`: `a19d` (21 beats,
AREA19 sub 1 -> sub 0 -> sub 1's tower -> AREA15 sub 0, from a19c_07's end)
and `a15` (2 beats: AREA15 sub 0's exit back to the tower, and the tower's
other door into AREA15 sub 1). Hidden PCSX2, exact one-frame steps, pad input
only, no teleports and no memory writes, one trace and one resumable
snapshot per beat. Nothing here is port behaviour.

**Status.**

- **AREA15 was reached and recorded to its exit.** The way: the two hall
  creatures shot; the valve [38] (the fire); the hand-over-hand ceiling west;
  the valve [37] (the lift rises 15); under the raised lift into the cage
  round sub 1's south-west ladder (the lift's scene, flag 0x21); that ladder
  down to sub 0 entry 0xD; sub 0's west part (the deck, the stair, a run off
  a landing's end, the west deck); the bar 858, the post's ladder 859 and the
  bar 1195 by the hang's sideways Use; [7]'s room's roof; the ladders 1016
  and 694 (sub 1 entry 8, inside the stair tower); the seal [48] broken from
  inside (door [49]'s lock bit 3); the flights to the y-450 landing; door
  [50] (AREA15 sub 0, [0]'s long scene: flag 0x22). Then AREA15 sub 0's
  slider [13] and door [14] back to the tower's y-450 landing (AREA19 sub 1
  entry 4), the flights to the y-500 landing and door [51]: AREA15 sub 1
  entry 0.
- **Not played: AREA15 sub 1's event** ([4] / [6] 0x823850: flag 0x23,
  counter 0x23, the forced return to AREA15 sub 0 that rewrites five areas'
  subs). Leads in section 7.
- **Health:** 72 at the start, 44 at the end (bites and grabs, section 4);
  the pickup g[3] (item 0x1E) under the lift was taken and used (+30).
  Infection 0 -> 70 (bug bites in the alcove and the cage). Rounds 60 -> 2
  (the two creatures: 30 + 28 presses with the lock held).
- Groups: a19d 21 beats, 26,633 frames, counters 151818..178484; a15 2 beats,
  2,281 frames, counters 178485..180767. 28,914 frames in all.
- **Census** (section 5): `fourteenth-delta` reports 90 functions (55,288
  bytes) outside its baseline: 67 boot, 2 AREA19 overlay (0x826B30,
  0x827540: the lift's scene) and all 21 AREA15 overlay functions that ran.
  Its baseline leaves out the first level's C10 capture passes; 31 of the 67
  boot functions (27,720 bytes) already ran there (30 in AIM, 0x163E90 in
  DMG / DMGB). **59 functions (27,568 bytes) ran in no earlier census pass
  at all**: 36 boot (16,560 bytes), the 2 AREA19 and the 21 AREA15 overlay
  functions.
- **00193EB0's area-0x13 arm** (section 2): the decomp's NEARMISS C inverts
  two branches against the original instructions. This is not new: the
  port's verified translation (`src/game/em_camera_area11_specials.c`,
  commit c9af471, 2026-09-23, oracle `tools/test_camera_area11_specials_reference.py`;
  `docs/CAMERA_AREA11_SPECIALS.md` sections 1 and "Where the readable C
  differs from the instructions") already follows the instructions. What
  this level adds is the measurement (the cage ladder's descent, 13 00 0D)
  and the correction of the thirteenth level's reading, which came from the
  C. The decomp's `docs/WORLD_GRAPH.md` now cites the instructions; this
  lane does not edit `src/` (a correction of `src/func_00193EB0.c` is in a
  separate task's worktree, not in the main tree).
- **Web leads:** the public walkthroughs (GameFAQs guides by MACisBack,
  VicViper_Mk2 and _PSXTREME_; Cheat Code Central; SuperCheats; Neoseeker;
  ChapterCheats) answered this session's fetches with HTTP 403, an
  unreachable host or no walkthrough text; nothing here comes from them.

In-game text (messages, captions, prompts) is described, never quoted.
Positions are world units; yaw is the player's +0xC4 (X = sin, Z = cos).

## 1. Tool, outputs and conventions

```sh
# decomp repo, .venv python, repo root (hidden PCSX2; hold build/.pcsx2.lock)
.venv/bin/python tools/route_capture.py run --beats a19d,a15
.venv/bin/python tools/route_capture.py events --beats a19d,a15
.venv/bin/python tools/route_capture.py verify --beats a19d,a15
```

`--beats all` and every earlier group are unchanged. a19d_00 starts from
`build/s87/route_a19c/a19c_07_door52/state.p2s`; each later beat from the
previous beat's snapshot. Outputs: `../Extermination/build/s87/route_a19d/<beat>/`
and `route_a15/<beat>/` (ignored): `trace.json`, `state.p2s`, `eeMemory.bin`,
`gs.bin`, `scratchpad.bin`, `original.png`, `snapshot.json`. Every kept
snapshot was reloaded after its capture (`run_beat`'s check) and resumed on
the first check (`tail_idle_frames` 0). The groups were captured through a
scratch wrapper of this lane (deleted at the session's end) that raises the
DebugServer / Pine socket timeouts to 60 s and the save-state wait to 120 s
in its own process (a save
on these frames took longer than `pcsx2_session.snapshot`'s 10 s once: the
emulator was stopped mid-save and left a partial slot-16 file, removed).

- **Determinism.** The beats replay the inputs of the exploration runs that
  found the route (section 6): the emulator is deterministic from a snapshot
  (an exploration run repeated with the same inputs from the same snapshot
  ended on the same frame, counter and creature state). A resume from a
  beat's own snapshot can start 1..9 main-loop counters off the recording, so
  the kept group drifted from the exploration from a19d_02 on (health ended
  higher: no bug grab in the cage). Two beats differ from the exploration's
  inputs: a19d_11 stops the bar at x 799.3 and a19d_12 transfers from there
  (section 6); a19d_14 checks that the hang is over the roof before the drop.
- **Rows**: `decode_a19d` (route_capture.py): the a19 row plus `s77c` (flags
  0x24..0x27), `s7fc` (counters 0x24..0x27), `lock` (the three R1 locks
  D_008106E0..E8), `mag` (D_00810C62), `rounds` (D_00810CB4, the rounds held,
  the magazine included), `inv64` (D_00810C64..A3) and, per owner, `k` (its
  +0x30..+0x3F: +0x34 hit points, +0x36 damage). `A19D_OWNERS` are a19c_07's
  sub-1 load: [34] .. [40], [46], the seal [48], door [49], the creatures A /
  B (0012E3A0) and three bugs (0012A5D0). From a19d_07 (sub 0) and a19d_16
  (the tower's fresh sub-1 load) on, the owner rows read other nodes. The a15
  rows carry no owners.
- **Helpers** (route_capture.py): `a19d_rock` (the stick rocked out of a
  grab: a bug on the back 0x3B, a flyer's hold 0x3C, 0x3D, a bite 0x3E),
  `a19d_go_rock`, `a19d_walk13` (a13d_go per point), `a19d_trav` (a hang
  move toward a point), `a19d_ceiling_drop`, `a19d_try_climb`,
  `a19d_side_try` (the stick held sideways, then Cross: the hang's or the
  ladder's transfer).

## 2. What decides each step (code, then the grids, then the capture)

Evidence marks as in decomp docs/WORLD_GRAPH.md: BM byte-matched C, NM
NEARMISS C, C overlay C linked byte-identical, ins read from the original
instructions. Grids: AREA19 sub 0 and sub 1 read from the captured RAM (the
world-section header at *0x0028A598, the 64-byte nodes; node numbers below
are that RAM's), the cell directory at *0x70003250 (one word per class-0x0B
placement after a header word; bit 30 = disabled).

- **The way down from sub 1 is a ladder descent (00193EB0, ins).** For the
  player's event codes 6..9 / 0x2C / 0x2D in area 0x13 the original tests
  D_00810701 (the sub) first: in sub 0 it only stores +6 = 0xD; in sub 1 it
  requests sub 0 (001B0C60(0x13, 0, n)) at x < 872 and y <= 356 (n = 0xD),
  at x >= 872, z <= 900, y <= 365 (n = 0xC) or at x >= 872, z > 900, y <= 356
  (n = 0xB). Sub 0's spawn entries 0xB (907, 309.1, 960.2), 0xC (925.1,
  309.8, 859.7) and 0xD (855.1, 309.4, 851.3) are the tops of the ladders 694
  (z 960), 959 (z 859.5) and 776 (z 851.5). **The NEARMISS C
  (src/func_00193EB0.c) inverts both tests** (it runs the bands for
  D_00810701 == 0 and swaps the two z bands); the thirteenth level's reading
  (THIRTEENTH_LEVEL_ROUTE.md section 7: the bands act only with D_00810701
  == 0, i.e. in sub 0) came from it. The port's committed translation
  already follows the instructions (`em_camera_area11_specials.c`,
  CAMERA_AREA11_SPECIALS.md: room 0 stores +6 = 0xD [00194038]; the z <= 900
  / z > 900 split to events 0xC / 0xB [001940D8]); reuse it.
- **[34]'s scene closed the platform's west part.** The cell directory word
  of sub 1's placement [1] (class 0x0B, key 8, kind 0x51: player-only, at
  (942, 380, 848.8)) is disabled in a19c_06's end snapshot and enabled in
  a19c_07's (measured); [34] (0x8279E0, C) also enables key 8 at a later load
  when flag 0x46 is set. An exploration walk west on the y-380 platform from
  door [52]'s north side stopped at x 951: the ladder at z 859.5 is closed
  after the arrival scene.
- **The ladder at x 848..862 (sub 1 nodes 140 / 141, attribute 0x32, y 370 ->
  330)** lies in a fenced cage (x 846..863.5, z 835..861; attribute-0x50
  fences on the east and on the north above y 387, a lintel at y 384.8) south
  of the lift [36]. Placement [0] (class 0x0B, key 0x15, kind 0x51) is a
  player-only cell over its top, enabled at load by the lift (0x826C10, NM:
  0019C6F0(0x15, 1) while flag 0x21 != 0xFF) and disabled at the end of the
  lift's scene (measured, directory bit 30).
- **The lift [36] (0x826C10, NM) and the valve [37] (0x826840, C).** [36] at
  (835.5, 370, 878.5) carries the truck [35] (0x827430, NM: +7.9 above the
  lift); its column (x 800.7..868.8, z 861.1..896) has attribute-0x50 rails
  from y 388 and attribute-0x52 walls (query class 2: creatures) from 369.8.
  [37]'s Use (+0xB bit 2, D_008107F9 low nibble 0) starts script 0x82CC70
  (D_008107F9 |= 0x80) and sets [36]'s +0x2EC = 0x8A; [36] counts it down,
  then moves +1/12 a frame for 180 frames (+15) and sets the low nibble
  (measured a19d_05: D_008107F9 0x00 -> 0x80 f249, 0x81 f567, 0x01 f869; the
  lift's +0xB4 370 -> 385). The caption during the script: the player says
  he can now reach the manhole. With the lift up the player walks under it
  from its north side (event code 4 under it) into the cage; the box 850 < x
  < 859.6, 850.5 < z < 855 starts script 0x82D290 (state 1); the script's
  callback 0x827540 lets the lift drop back to its rest height; at the
  script's end D_00810779 (flag 0x21) = 0xFF and key 0x15 is off (measured
  a19d_07 f798..f1013).
- **The two creatures (0012E3A0, +0x34 = 100)** stand in the hall's west part
  (sub 1 deferred group entries g[12] (824.2, 372, 909.2) and g[13] (809.8,
  370.8, 843), the valve [37]'s alcove). With R1 held the lock D_008106E0
  takes a creature in line of sight; Circle fires one round (5 hit points a
  hit; rounds D_00810CB4 fall by one a round, the magazine D_00810C62 reloads
  from them at 0). The fire between the hall's parts ([39] 0x823D10, [40]
  0x823780; their cell, key 7, kind 0x51) did not stop the creature A in one
  exploration run (section 6). The valve [38] (0x826570, C) ends the fire:
  script 0x82CA20, counter 0x20 1, flag / counter 0x20 0xFF, key 7 off.
- **The hand-over-hand ceiling.** 0015D4C0 (NM) case 0x3A probes 40 up from
  an attribute-0x3A floor: on attribute 0x1E it enters the ceiling hang (+5
  0x11, action 0x20, +0x254 = the hit height - 20.5); the hang then moves with
  the stick (+5 0x12). Sub 1's ceiling nodes 147..154 (attribute 0x1E, y
  446.5) run from x 940 west along z 920..934, south along x 880..900 to z
  838, west along z 900..920 to x 850, north along x 850..867.5 to z 981.5
  and west along z 962..981.5 to x 809.5 (attribute-0x36 marks at both ends,
  nodes 638 / 632). Its pad (node 234, y 424) is on a box (node 231) on the
  y-410 ledge (node 213, x 894..990, z 920..939), reached by the ladder at
  (892, 929.5) (nodes 113..115). Cross while hanging drops (+5 leaves 0x11 /
  0x12).
- **The pickup g[3]** (0015AFA0, item 0x1E, a healing item: STATUS_PAGES.md
  HEALING kinds 0..4 = items 0x1E..0x22) lies at (822.2, 370.4, 878.8) under
  the lift; the take opens the HEALING page; Cross, Cross, Left (Yes), Cross
  uses it (+30 health, 001C47E0 takes the item).
- **Sub 0's [10] (0x827790, C).** With D_0081081D (counter 0x45) 0 and spawn
  entry 0xD it plays script 0x82D990, after 366 frames script 0x82DA10, then
  gives CC3 item 0x0C and sets counter 0x45 = 0x80 (measured a19d_07 f2644);
  with 0x80 it plays script 0x82DD10 while the player is in the area
  0x82E050 (x 745..766, z 892..900, y 265), re-armed 600 frames after the
  player leaves it (measured a19d_10: flag 0x45 = 0xFF f1055).
- **Sub 0's west part.** From the ladder 776's foot (the deck nodes 263 /
  1272, y 265) west: the slope 262 (class 0x10: the slide) down to the floor
  723 (y 210.9); the stair 686 / 687 (attribute 0x35) north to the landing
  1214 (y 229, z 888..898); a gap to the next landing 1200 / 1212 (y 229, z
  911.2..921); the stair 700 / 702 north up to the west deck 1350 / 719 (y
  265). The deck part east of x 882.8 is behind a player-only wall (node
  1176). Player-only walls 1216 (x 790.5, y 222..254) and 1164..1168 and the
  stair's rails 732 / 733 (attribute 0x50, x 789.6) keep the player off the
  pipe 1452 and its underside bar 1195 from the landings and the stair.
- **The bars and the post.** The bar 858 (attribute 0x34, y 291.5, x
  750.2..817.7, z 878..888) has its attribute-0x3A pad 919 on the box 874 (y
  268) on the west deck. The post (x 809.5..811.3, z 898.6..906.5, y
  215..320) carries the ladder 859 (attribute 0x32, face x 809.5, normal -x).
  The bar 1195 (attribute 0x34, y 261.5, x 790.7..894.2, z 917..927) is the
  pipe 1452's underside. While hanging, 00169730 (NM) runs a sideways stick
  (+0x24C 2 / 3) as cases 60 / 70: with Use held in its phase 2, 001787B0
  (BM) sweeps 14.5 forward from a point 18 to the side and 18 up and, on an
  attribute-0x32 face, starts the transfer (+6 0x5A, action 0x12) to the
  ladder. Measured (a19d_12): hanging at x 800.2 facing +x, the stick held
  toward +z (the hang moved to x 804.2, z 885), the third Cross (f134) took
  the transfer (action 0x12 f137, the ladder's 0x17 at a19d_13 f48); in an
  earlier capture attempt that started the same moves from x 812 the
  transfer was not taken (section 6). On the post's ladder at y 242 the
  stick toward +z and Cross (the second press, f418) put the player on the
  bar 1195 (action 0x21 f452; the ladder-side function was not read). The player-only wall 1201 (z 890.3, x 769.8..803.8, y
  260..303.8) lies between the bar 858 and the post.
- **The roof, the deck and the tower.** The bar 1195's east end is over [7]'s
  room's roof (node 192, y 230, railed by attribute-0x50 rails 314..321); the
  roof holds the foot of the ladder 1016 (z 939, up to the deck node 222 at y
  265); the deck's ladder 694 (z 960) is 00196970's circle for sub 1 entry 8:
  the request 13 01 08 01 at y 314.1 (a19d_16 f652..f716) and sub 1 entry 8
  (907, 354, 960.6) on the tower's ladder; control at (907.0, 370, 953.3)
  (f1111; the beat ends at (906.4, 370, 953.3)) in
  a pocket fenced by player-only walls (sub 1 nodes 314..316, 616..618).
- **The seal [48]** (001581A0, NM; (980.7, 377.7, 940.6), yaw 0: it faces the
  tower's inside) breaks when its +0x36 is non-zero. Kept capture: from
  (980.9, 946.2) facing -z (xz distance 5.6 from the seal) the first Circle
  (f140) started no melee; the second (f202) started the light melee
  (action 0x36 f205), which broke the seal (a19d_18 f219: D_00810854 0x04 ->
  0x0C, door [49]'s bit 3). In a19d_17, from (984.4, 946.5) (xz distance
  7.0), six Circle presses (f265, f312, f359, f406, f453, f500) started
  three light melees (f268, f362, f456; the other presses fell inside a
  running melee) and the seal held. In a19d_17's census replay (the same
  beat function, a closed-loop walk to the same target) one Circle (f311)
  broke it (section 5); where the player stood there is not in the run
  record. So no distance rule follows from these runs: the hit rule was not
  read.
- **The tower's flights** (sub 1 nodes 80 / 39, 30 / 41, 28 / 38, 29 / 40,
  27 / 37; landings 602 / 605 / 606 / 608 at y 395.5, 43 at 421, 45 / 48 at
  450, 22 at 475.5, 24 at 500.5) lead to door [50] (001BC350, BM, id 0x86) at
  (902, 450, 941) and door [51] (id 0x87) at (902, 500, 941), both used facing
  -z.
- **AREA15 sub 0's [0]** (0x8235A0 / 0x8236B0, C: the overlay's NPC at
  (831.6, 240, 888.8)) plays script 0x826E70 at once while counter 0x22
  (D_008107FA) is 0: D_00810702 = 1 (the area bytes 0F 00 00 0F -> 0F 00 01
  0F at a19d_20 f523), at its end CC3 item 0x0D, counter 0x22 = 1, flag 0x22
  (D_0081077A) = 0xFF (f9891). AREA15 sub 0's slider [13] (001BB860, NM,
  model 0x08, room move id 1) takes the player from entry 1's side to entry 2
  (916.3, 240, 928.4); door [14] (001BC350, id 0x80) to AREA19 entry 4 sub 1
  (907, 450, 956: the tower's y-450 landing).

## 3. Route table

Frames `f` are trace frames; `c` is the main-loop counter 0x70003B64.

| Beat (folder) | Source | Frames | Counters | Presses | What happens |
|---|---|---|---|---|---|
| `a19d_00_beastA` | a19c_07 | 641 | 151818..152459 | R1 held from f161, Circle x30 (f181..) | east of the fire to (920, 900); a bite (action 0x3E) f50 (72 -> 67); the lock on creature A (node 0x7A7980) f181; its +0x34 100 -> 0 f592; rounds 60 -> 30 |
| `a19d_01_ceiling` | a19d_00 | 2607 | 152461..155068 | Cross f257, f1263, f1674, f1865 | the valve [38] facing -z (3B8D 2 f261; counter 0x20 1 f477; flag / counter 0x20 0xFF f752); hits 67 -> 52 (f856..f1065); the ladder at (892, 929.5) (action 0x15 f1266) to the y-410 ledge (f1573); the ledge climb onto the box (action 0x08 f1688, y 424 f1755); Cross on the pad: the ceiling hang (action 0x20, +5 0x11 f1868); hand over hand west / south / west to (853.8, 426, 905.9) |
| `a19d_02_lockB` | a19d_01 | 510 | 155069..155579 | Cross f5, R1 held from f420 | the drop at (852.8, 370, 905.8); a hit f62 (52 -> 47); north round the barrel [18] to (788.2, 370.2, 901.6) facing -z; the lock on creature B (0x7A7C70) f440 |
| `a19d_03_beastB` | a19d_02 | 457 | 155580..156037 | R1 held, Circle x28 | creature B's +0x34 100 -> 0 f418 (it stayed at (804, 858)); rounds 30 -> 2 |
| `a19d_04_alcove` | a19d_03 | 1189 | 156038..157227 | Cross f236, f331, f426 (facing +x at (795.4, 878.6)), f714, f809, f904 (at (795.5, 868)) | no climb onto the lift; south into the alcove (818.4, 842.7); infection 0 -> 50 (bug bites) |
| `a19d_05_valve37` | a19d_04 | 869 | 157228..158097 | Cross f246 | the valve [37] facing -z: 3B8D 2 f250, D_008107F9 0x80 f249; the lift rises (D_008107F9 0x81 f567); 0x01 and control f869 (lift +0xB4 385); infection 60 |
| `a19d_06_g3` | a19d_05 | 440 | 158099..158539 | Cross f371 | north round the lift's west end, under the raised lift to (825.9, 879.8); g[3] taken (3B8D 3 f374): the HEALING page with item 0x1E |
| `a19d_07_cage` | a19d_06 | 2703 | 158548..161251 | Cross f30, f152, Left f165, Cross f187, Triangle f481, Cross f1161 | item 0x1E used (47 -> 77 f281); under the lift into the cage: script 0x82D290 (3B8D 2 f799); **flag 0x21 = 0xFF** f1013; the ladder (Cross f1161): request 13 00 0D, area bytes 13 00 0D 13 f1327; [10]'s scripts (3B8D 2 f1633..f2644); **counter 0x45 = 0x80** f2644; control on the deck (855, 265, 844.5) |
| `a19d_08_west` | a19d_07 | 642 | 161252..161894 | Cross f600 | the deck west; the slope's slide (action 0x30) to the y-211 floor; the stair north; running north off the landing's end (Cross at z 896 starts no jump), over the gap: landing at (784.2, 229, 909.5) |
| `a19d_09_westdeck` | a19d_08 | 352 | 161895..162247 | - | the stair north to the west deck (760.6, 265, 960.1) |
| `a19d_10_area82E050` | a19d_09 | 1114 | 162248..163362 | - | a hit f135 (77 -> 72); [10]'s area: script 0x82DD10 (3B8D 2 f228); **flag 0x45 = 0xFF** f1055 |
| `a19d_11_bar858` | a19d_10 | 445 | 163363..163808 | Cross f203 | onto the pad (761.5, 268, 884); Cross facing +x: the hang on the bar 858; east to (800.1, 271, 883) |
| `a19d_12_post` | a19d_11 | 202 | 163809..164011 | stick toward +z, Cross f50, f92, f134 | the sideways Use: action 0x12 f137, onto the post's ladder 859 (809.5, 275, 902.5) |
| `a19d_13_bar1195` | a19d_12 | 525 | 164012..164537 | stick down; stick toward +z, Cross f376, f418 | the ladder (0x17 f48) down to y 242; onto the bar 1195 (action 0x21 f452) at (804.2, 241, 922) |
| `a19d_14_roof` | a19d_13 | 638 | 164538..165176 | Cross f535, f583 | a hit f150 (72 -> 54, the source not read); east along the bar to x 894.2; the drop (action 0x23 f586) onto the roof (894.2, 230, 924) f610 |
| `a19d_15_deckB` | a19d_14 | 445 | 165177..165622 | Cross f142 | the ladder 1016 facing +z (action 0x15 f145) up to the deck (915, 265, 950.7) |
| `a19d_16_ladder694` | a19d_15 | 1170 | 165623..166793 | Cross f261 | round the slab to (906.7, 964.5); the ladder 694 facing -z; request 13 01 08 at y 314.1 (f652), area bytes 13 01 08 13 f716; control in the tower (907.0, 370, 953.3) f1111; end (906.4, 370, 953.3) |
| `a19d_17_seal_far` | a19d_16 | 557 | 166795..167352 | Circle x6 (f265..f500) | to (984.4, 946.5) facing -z: three light melees (action 0x36 f268, f362, f456), the seal intact |
| `a19d_18_seal` | a19d_17 | 274 | 167353..167627 | Circle f140, f202 | to (980.9, 946.2): the first press starts no melee; the second starts the only light melee (f205), which breaks the seal: **D_00810854 0x04 -> 0x0C** f219 |
| `a19d_19_flights` | a19d_18 | 903 | 167629..168532 | - | the flights to the y-450 landing (902.8, 450, 946.3); a hit on the first flight f227 (54 -> 44) |
| `a19d_20_door50` | a19d_19 | 9950 | 168534..178484 | Cross f22 | door [50] facing -z (3B8D 2 f25); area bytes 0F 00 00 13 f167, 0F 00 00 0F f178; AREA15 sub 0 entry 0 (907, 240, 928) f521; D_00810702 = 1 f523 ([0]'s script 0x826E70, 3B8D 2 f524..f9891); **flag 0x22 = 0xFF, counter 0x22 = 1** f9891; control (943.5, 240, 913.1) |
| `a15_00_door14` | a19d_20 | 1127 | 178485..179612 | Cross f195, f605 | the slider [13] facing -0.66 (3B8D 2 f198): entry 2 (0F 00 02 0F f350); door [14] facing +z (f608): 13 01 04 0F f770, 13 01 04 13 f781; control on the y-450 landing (907, 450, 956) f1068 |
| `a15_01_door51` | a15_00 | 1154 | 179613..180767 | Cross f598 | the flights to the y-500 landing; door [51] facing -z (3B8D 2 f601): 0F 01 00 13 f743, 0F 01 00 0F f754; AREA15 sub 1 entry 0, control (906, 290.1, 925.3) f1095 |

Main line a19c_07 -> a19d_00 .. a19d_20 -> a15_00 -> a15_01: 28,914 frames,
counters 151818..180767 (each beat starts 1..9 counters after the previous
beat's last: the resume, section 1).

## 4. Story bytes, health and items (measured)

| Byte | Before | After | Where |
|---|---|---|---|
| D_00810778 (flag 0x20) | 0 | 0xFF | a19d_01 f752 |
| D_008107F8 (counter 0x20) | 0 | 0xFF | 1 a19d_01 f477; 0xFF f752 |
| D_008107F9 ([36] / [37]: bit 7, low nibble) | 0 | 0x01 | 0x80 a19d_05 f249; 0x81 f567; 0x01 f869 |
| D_00810779 (flag 0x21) | 0 | 0xFF | a19d_07 f1013 |
| sub 1 cell directory [0] (key 0x15) / [2] (key 7) | enabled | disabled | after the lift's scene / after [38] (read in the exploration's snapshots) |
| D_0081081D (counter 0x45) | 0 | 0x80 | a19d_07 f2644 |
| D_0081079D (flag 0x45) | 0 | 0xFF | a19d_10 f1055 |
| D_00810854 (AREA19 lock byte) | 0x04 | 0x0C | a19d_18 f219 |
| D_0081077A (flag 0x22) | 0 | 0xFF | a19d_20 f9891 |
| D_008107FA (counter 0x22) | 0 | 1 | a19d_20 f9891 |
| D_00810700..703 | 13 01 01 13 | 0F 01 00 0F | 13 00 0D 13 a19d_07 f1327; 13 01 08 13 a19d_16 f716; 0F 00 00 13 / 0F 00 00 0F a19d_20 f167 / f178; 0F 00 01 0F f523; 0F 00 02 0F a15_00 f350; 13 01 04 0F / 13 01 04 13 f770 / f781; 0F 01 00 13 / 0F 01 00 0F a15_01 f743 / f754 |
| D_00810C82 (item 0x1E) | 0 | 0 | 1 a19d_06 (taken); 0 a19d_07 (used) |
| D_00810CB4 (rounds) | 60 | 2 | 30 a19d_00; 2 a19d_03 |
| player health (D_008104D0) | 72 | 44 | 67 a19d_00 f50; 62 / 57 / 52 a19d_01; 47 a19d_02 f62; 77 a19d_07 f281; 72 a19d_10 f135; 54 a19d_14 f150; 44 a19d_19 f227 |
| infection (player +0x228) | 0 | 70 | 50 a19d_04; 60 a19d_05; 70 a19d_07 |

Items by code (read, not sampled: CC3 lies outside the rows): CC3 0x0C
([10], a19d_07), CC3 0x0D (AREA15 [0], a19d_20).

The scenes' captions and dialogue: [37]'s Use: the player remarks that the
manhole is now within reach; the lift's scene shows the lift falling back;
[10]'s two arrival scripts and its west-deck script are scenes with captions
(not read); AREA15's [0] is a long conversation with a character in a
medical room; at its end the status overlay shows the infection level.

## 5. Census

**Tool** (`../Extermination/tools/route_census.py`, additive):
`run --segments a19d --pass A19D` replays each a19d beat from its recorded
source snapshot with route_capture's own beat function and one-shot
breakpoints on every boot function and on the AREA19 overlay (id 16; the
sub changes keep the overlay); a19d_20 (AREA19 -> AREA15) runs under the
`OverlayCensusSession` (boot + AREA19 armed, AREA19's remaining breakpoints
swapped for AREA15's, id 12, at the first frame boundary where the resident
id changes). `run --segments a15 --pass A15` replays a15_00 (AREA15 ->
AREA19, AREA15 armed first) and a15_01 (AREA19 -> AREA15) the same way.
`fourteenth-delta` runs `chain_delta` over both groups against the
first-level census, beat 15, the A01, A00, A01R, A02, A04, A22, A01U and A06
passes, the eighth- to eleventh-level groups, A13D, a19b (A19B, A19BX) and
A19C, once for the AREA19 overlay and once for AREA15. Output:
`../Extermination/build/s87/census/a19d_delta.json`, `a15_delta.json` (and
`runs/A19D/`, `runs/A15/`). **The baseline leaves out the first level's C10
capture passes** (AIM, BR / BRB, DMG / DMGB, OPT / OPTB, EXIT / EXITB under
`census/runs/`); the overlap with them was measured afterwards by reading
every `runs/*/*.json` hit list except A19D / A15 (below).

```sh
# decomp repo, .venv python, repo root (hidden PCSX2; hold build/.pcsx2.lock for `run`)
.venv/bin/python tools/route_census.py run --segments a19d --pass A19D
.venv/bin/python tools/route_census.py run --segments a15 --pass A15
.venv/bin/python tools/route_census.py fourteenth-delta
```

**Replay fidelity.** All 23 replays completed their beat's own checks
(`beats_incomplete` empty; a15_01's first attempt stopped at its start with
a refused DebugServer connection and the retry is the kept run). Swaps:
a19d_20 after frame 174 (counter 168716; 43 AREA19 breakpoints removed, 45
AREA15 armed); the a15 swaps likewise at the door's area change. Rows are
compared by the main-loop counter; the start offset is the replay's first
counter minus the recording's.

| Beat | Frames replay / recorded | Start offset | Rows identical by counter | End words differing (player / globals / owners) |
|---|---|---:|---|---|
| a19d_00 | 641 / 641 | 0 | 642 / 642 (exact) | 0 / 1 / 0 |
| a19d_01 | 2,611 / 2,607 | -1 | 1,041 / 2,608 | 20 / 28 / 158 |
| a19d_02 | 510 / 510 | 0 | 511 / 511 (exact) | 0 / 1 / 0 |
| a19d_03 | 457 / 457 | 0 | 458 / 458 (exact) | 0 / 1 / 0 |
| a19d_04 | 1,189 / 1,189 | 0 | 1,190 / 1,190 (exact) | 0 / 1 / 0 |
| a19d_05 | 870 / 869 | 0 | 127 / 870 | 0 / 2 / 38 |
| a19d_06 | 444 / 440 | -1 | 7 / 441 | 12 / 19 / 112 |
| a19d_07 | 2,764 / 2,703 | 4 | 340 / 2,700 | 0 / 2 / 149 |
| a19d_08 | 642 / 642 | 0 | 643 / 643 (exact) | 0 / 0 / 0 |
| a19d_09 | 353 / 352 | 0 | 245 / 353 | 14 / 31 / 181 |
| a19d_10 | 1,111 / 1,114 | 1 | 7 / 1,112 | 14 / 30 / 243 |
| a19d_11 | 441 / 445 | 0 | 2 / 442 | 0 / 2 / 297 |
| a19d_12 | 202 / 202 | 0 | 203 / 203 (exact) | 0 / 0 / 0 |
| a19d_13 | 525 / 525 | 0 | 472 / 526 | 0 / 1 / 15 |
| a19d_14 | 638 / 638 | 0 | 639 / 639 (exact) | 0 / 1 / 0 |
| a19d_15 | 445 / 445 | 0 | 446 / 446 (exact) | 0 / 0 / 0 |
| a19d_16 | 1,170 / 1,170 | 0 | 1,171 / 1,171 (exact) | 0 / 1 / 0 |
| a19d_17 | 368 / 557 | -1 | 5 / 368 | 19 / 33 / 32 |
| a19d_18 | 274 / 274 | 0 | 275 / 275 (exact) | 0 / 0 / 0 |
| a19d_19 | 895 / 903 | -1 | 5 / 895 | 18 / 32 / 88 |
| a19d_20 | 9,950 / 9,950 | 8 | 289 / 9,943 | 0 / 3 / 51 |
| a15_00 | 1,127 / 1,127 | 6 | 241 / 1,122 | 0 / 2 / 14 |
| a15_01 | 1,155 / 1,154 | 0 | 574 / 1,155 | 0 / 3 / 49 |

The beats are closed-loop, so the other replays are different walks of the
same route shape. a19d_17's replay pressed Circle once (f311) and its loop
(which stops when D_00810854 changes) ended at 368 of 557 frames: the
`l854` column differs from the recording in the replay's last 41 rows, so
in that replay the seal broke on the first press. The replay's position is
not in the run record (its positions differ from the recording in 362 of
368 rows). As for the earlier levels, the census sets are those of the
route shape, not of the exact frames.

**Totals.** a19d_delta: 1,288 functions executed (1,261 boot, 27 of
AREA19's 43); a15_delta: 1,282 (1,261 boot, 21 of AREA15's 29).
`fourteenth-delta` reports **90 functions, 55,288 bytes** outside its
baseline: 67 boot (44,280 bytes; byte-matched C 39, NEARMISS 19, asm word
7, C linked from asm 1, inline asm 1), 2 AREA19 overlay (236 bytes, C) and
21 AREA15 overlay (10,772 bytes; C 17, NEARMISS 3, asm word 1). No hit is
unattributed. Other-overlay hits: the AREA19 delta has 29 AREA15 hits, all
in the three door beats (a19d_20 16, a15_01 8, a15_00 5); the AREA15 delta
has 238 AREA19 hits from every a19d beat and both a15 beats (it reads both
passes, e.g. a19d_16 24, a19d_07 23).

**Overlap with the first level's C10 passes** (measured from the run
files): 31 of the 67 boot functions (27,720 bytes; byte-matched C 19,
NEARMISS 11, inline asm 1) already ran there: 0x163E90 in DMG and DMGB, and
in AIM (the AREA11 R1 hold / fire / reload beats) 0011E860, 0016F530,
0016F5D0, 0016F600, 0016FCF0, 00170A60, 00179BC0, 00179CA0, 0017A0B0,
0017A130, 0017A8B0, 0017ABA0, 0017B300, 00183AC0, 001854E0, 00185760,
00185A10, 001860A0, 001861C0, 00187CC0, 0018ABA0, 0018F870, 00197490,
00197740, 00197870, 00197D20, 001A6AD0, 001A9C40, 001B1C60, 001B5DC0. No
earlier pass armed AREA15, and the AREA19 hits of earlier passes are all in
the baseline (A13, A13B, A19, A19B, A19BX, A19C). **New in any census pass:
59 functions, 27,568 bytes**: 36 boot (16,560 bytes; byte-matched C 20,
NEARMISS 8, asm word 7, C linked from asm 1), 2 AREA19 and 21 AREA15
overlay.

Functions outside the baseline (census frames; the captions name the
beat's event at that frame, not the function's role; **AIM** / **DMG** mark
the 31 that the first level's C10 passes already ran):

| Functions | Status | Subsystem | First |
|---|---|---|---|
| 0011E860, 0016F530, 0016FCF0, 00170A60, 00179BC0, 00179CA0, 0017A0B0, 0017A130, 0017A800, 0017A8B0, 0017ABA0, 0017AF70, 0017B300, 00183AC0, 001854E0, 00185760, 00185A10, 00185E30, 001860A0, 001861C0, 00187CC0, 0018ABA0, 0018F870, 00197740, 00197870, 00197D20, 001A6AD0, 001A9C40, 001B1510, 001B1C60, 001B5DC0 | BM 18, NM 11, asm word 1, inline asm 1 | lowmem, entity_update, actor_anim, init_io, level_world, unknown_01, math_vector, input_io | a19d_00 f165..f186 (R1 held, the lock, the first rounds); **AIM**: all but 0017A800, 0017AF70, 00185E30, 001B1510 |
| 00131650, 00131740, 001339E0, 0016F600, 00131F20, 001B2D00, 001B4CF0, 001EDE40 | asm word 1, NM 3, BM 4 | entity_logic, entity_update, math_vector, hud_objects | a19d_00 f243..f604 (the creature hit; down at f592); **AIM**: 0016F600 |
| 00131B10, 0016F5D0, 00197490, 00131E80, 001F6BA0 | BM 4, NM 1 | entity_logic, entity_update, init_io, fx_render | a19d_01 f2..f754 (the valve [38]'s script); **AIM**: 0016F5D0, 00197490 |
| 0016AC50, 0016ADE0, 0016AE40, 00175390, 00181E20, 00182090, 00182100 | BM 3, C from asm 1, NM 2, asm word 1 | entity_update, actor_anim | a19d_01 f1873..f1928 (the ceiling hang) |
| 00163E90 | BM | frame_update | a19d_02 f63 (the drop); **DMG** / DMGB |
| overlay AREA19 0x827540, 0x826B30 | C | overlay_AREA19 | a19d_07 f801 / f883 (the lift's scene) |
| 001AA2A0, 001AA410 | asm word | frame_main | a19d_08 f603 (the run off the landing's end) |
| 00181730, 001782A0, 001787B0 | NM, BM, BM | actor_anim | a19d_12 f38 / f137 (the sideways Use, the transfer to the ladder) |
| 0017FD80, 0017FE00, 0017FF00, 00180600, 001809B0 | BM 4, NM 1 | actor_anim | a19d_13 f329..f379 (the post's ladder, the transfer to the bar 1195) |
| 002230A0 | BM | area_logic | a19d_14 f150 (the hit on the bar) |
| 001A56A0 | asm word | level_world | a19d_16 f155 |
| overlay AREA15: 0x823540, 0x8235A0, 0x8236B0, 0x824070, 0x824510, 0x824560, 0x8247B0, 0x824990, 0x824E00, 0x824E50, 0x825030, 0x825320, 0x825430, 0x825D10, then 0x823780 | C 11, NM 3, asm word 1 (0x823540) | overlay_AREA15 | a19d_20 f521..f523 (AREA15 sub 0's load and [0]'s script), 0x823780 f9892 (after it) |
| 00153950, 00153A10, 00153A90; overlay AREA15 0x823850, 0x8239F0, 0x823E40, 0x8252D0, 0x826600, 0x826850 | asm word 1, BM 2; C 6 | entity_logic, overlay_AREA15 | a15_01 f1097..f1098 (AREA15 sub 1's load: [4] / [6], [8], [1]..[3]) |

To check a port translation, grep the port (`src` and `tools`,
case-insensitive, `grep -rlI -i`) for the address string: boot functions
as 8 hex digits (e.g. `0017FD80`), overlay functions as 6 (e.g. `827540`).
Measured 2026-10-02 with that rule: **70 of the 90** addresses appear (66
if the overlay addresses are also searched as 8 digits, `00827540`); of the
59 new to any census pass, 39 (29 boot, 10 overlay). A match is only the
address string: whether it is a translation or a hook slot is not judged
here; among the boot matches are the aim / fire, weapon-state, camera,
ladder-climb and player-closure modules. **No overlay match is an AREA15
translation**: 0x823540 (em_area00_overlay.c, em_area02_overlay.c,
em_area22_port.h and others), 0x823780 (AREA19's [40] in
em_level13_port_area19.c), 0x8236B0 (an AREA22 data group in
export_area22_tables.py), 0x824560 (AREA06), 0x825030 (AREA19,
em_level12_port_area19.c), 0x825D10 (an AREA04 site in
test_area04_assets_reference.py), 0x826600 (an AREA04 data group) and
0x826850 (AREA13, em_level9_port_area13.c) are other areas' addresses. The
two AREA19 functions are named, not translated: 0x826B30 in a callback
table of test_area13_assets_reference.py, 0x827540 in a comment of
em_level13_port_area19.c. Not referenced: 00131740, 00131B10, 00153950,
00153A10, 00153A90, 0016AC50, 001EDE40 and the AREA15 overlay 0x8235A0,
0x823850, 0x8239F0, 0x823E40, 0x824070, 0x824510, 0x8247B0, 0x824990,
0x824E00, 0x824E50, 0x8252D0, 0x825320, 0x825430.

## 6. Tried and not kept (exploration runs, this session)

Scratch snapshots under `../Extermination/build/fourteenthcap/probe/`
(deleted at the session's end); leads, not ground truth.

- **The heal detour.** Going back through door [52] to the y-380 platform
  and west to the ladder at z 859.5 (for sub 0's pickup g[14], item 0x1E, on
  the y-210 floor): the walk stopped at x 951 ([34]'s key-8 cell, section 2).
- **The fire as a barrier.** From (920, 900) with the fire burning, one
  closed-loop run took the creature A down without a hit (30 presses); a
  differently timed run (presses only while locked) let it cross east at
  frame 414 and bite (health 72 -> 37, 32 rounds). The kept beat uses the
  first run's inputs. After the valve [38] with the creature A alive it
  crossed and took the player from 67 to 7.
- **Other places to shoot from.** From the y-410 ledge (898.5, 410, 930) no
  lock formed on the creature A 50 below; from the ceiling's west-end
  platform (y 421) and the y-403 platform inside the diamond (attribute-0x52
  walls) no lock on the creature B behind the lift (87 units). From (790,
  908) the lock held (kept).
- **Climbing the lift, the truck or the box.** Cross facing the lift at
  rest from the east (873.3, 877.9), the north (843.1, 900.4; 853.2, 900.5)
  and, raised, the west (796.2, 876.2) and north (814.6, 900.4) started no
  climb; Cross facing the box (x 828..846, top y 410, attribute 0x46) from
  the alcove started none. With the lift at rest a walk into its column
  stopped 4.5 short of it on every side.
- **The flyers** (sub 0, 001383C0, +0x34 = 40) grab (action 0x3C, the player
  lifted) and bite (18); the light / heavy melee missed them (they hover at
  y 280..305); the stick rocked frees a hold. In the exploration they cost
  up to 36 at the pad; the kept a19d_14 took one hit of 18 on the bar 1195
  (its source not read).
- **The landing's gap.** Cross at the landing's end (z 896) while running
  started no running jump; the run carried the player over the gap onto the
  far landing (kept a19d_08). An earlier try from a standing start fell into
  the gap (health 37 -> 19 from a bite on the stair, then a fall toward the
  pit; not kept).
- **The bar transfer.** The first capture attempts of a19d_12 started from x
  812 (the exploration's stop) and moved west then east along the bar with
  the stick aimed through the camera: once the hang settled at x 804.2 and
  the sideways Use was not taken in three presses, once the aim went the
  wrong way to the bar's east end and into the swing (+6 0x50). The kept
  a19d_11 stops at x 799.3 instead.
- **The seal from (984.4, 946.5)** (a19d_17, kept; xz distance 7.0): six
  Circle presses, three light melees, no break. This holds for the kept
  capture only: the census replay of the same beat broke the seal on its
  first press (section 5). The beat's own label in route_capture.py was
  corrected to these counts; the kept trace.json and the census run file
  carry the older label ("six light melees from 6.6 units").
- **AREA15 sub 1** (from a15_01's end): a walk west along z 933 stopped at x
  864.5 (the player crouched under a low ceiling; an organic mass in the
  way; grid nodes 1409 / 1410: a block x 780..860, z 870..950, top y 305.1).

## 7. Open

1. **AREA15 sub 1's event.** [4] (0x823850, +0xD 0x54, at (804.5, 325, 938))
   waits (D_008107FB 0) until the player's y >= 310 inside the area 0x827C80
   (an L: x 750..890 for z 906..995, x 750..793 for z 825..906, read from the
   overlay image); then script 0x827400 (flag 0x23 = 1), D_008107FB = 1 and a
   900-frame count; D_008107FB = 2 when the player's y < 291, or x > 893, y >
   329, z > 940, or the count ends; then 0x823C80: script 0x8277C0, items C64
   0x06 and CC3 0x0E, D_00810730[0, 1, 2, 4, 6] = 0x82, 0x81, 0x82, 0x81,
   0x81, the request AREA15 sub 0 entry 0. The raised parts of sub 1 (the
   stairs 792 / 793 and 795 to the y-339.6 and y-330.1 platforms, the
   climbable object [8] 0x826600 at (798, 314.8, 931.9)) are leads, not
   played.
2. **AREA15 sub 0 after the event**: [1] / [2] (0x824070: flag 0x23 set,
   flags 0x28 / 0x29 clear) give item 0x25 (C64), the key of AREA19 sub 1's
   gate [46] (0x829A70: D_00810838 = 1, cell key 5 off); [53] (0x827550, item
   0x24) waits for flag 0x25 (AREA07 / AREA08).
3. **Ammunition**: 2 rounds held at the end; the only magazine pickup in
   AREA19 (sub 0 g[0], item 0x10) lies at y 130 at the bottom of the room
   behind door [27].
4. The ladder-side transfer to the bar 1195 (which function), the seal
   [48]'s hit rule (why a19d_17's three melees missed and its replay's one
   hit), and the flyers' and bugs' hit rules were not read.
5. Memory cards: as THIRTEENTH_LEVEL_ROUTE.md section 7 item 4. After this
   lane's last session the shared install's `inis/PCSX2.ini` and
   `portable-data/memcards/Mcd001.ps2` / `Mcd002.ps2` carry mtimes
   2026-10-02 03:36 (PCSX2's shutdown); no in-game save was made, but that
   the cards' content is unchanged is not proven.
