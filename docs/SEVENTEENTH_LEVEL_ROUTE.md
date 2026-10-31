# AREA07 finished, by a branch from AREA03: seventeenth-level route, original ground truth

Lane L1 (capture), 2026-10-31. Original executable SHA-256
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

`SIXTEENTH_LEVEL_ROUTE.md` stopped in AREA07's chamber C at health 17, infection 100, and
exploration died on the walk back. This document records the branch that arrives with more health
and finishes AREA07: subs 0, 2 and 3, the script of the 0x823990 actor (flag 0x25), and the lift out.
Beat groups `a03d`, `a04d`, `a07b` of `../Extermination/tools/route_capture.py`. Hidden PCSX2 fork,
exact one-frame steps, pad input only, no teleports and no memory writes (the recorder rejects a
write), one trace and one resumable snapshot per beat. Nothing here is port behaviour.

In-game text is described, never quoted. Positions are world units; yaw is the player's +0xC4
(X = sin, Z = cos). Frames `f` are trace frames, `c` the main-loop counter 0x70003B64. Actor indices
`[n]` are the live table of the area and sub named (they are not the indices of the code graph, and
the table is rebuilt on each sub load).

**Status.**

- **The set is new**: `../Extermination/build/fork_refs/levels-recordings-20261031-1/`
  (`s87/route_a03d/`, `route_a04d/`, `route_a07b/`, each with `manifest.json`; the run log per beat
  and `progress.json` at the top). `levels-recordings-20261030-1` was not touched: lanes N1 and P1
  go on porting `a03c_02`..`a07_00` from it. The first set's a03c_00 and a03c_01 are this branch's
  common stem; `a03d_00` resumes from `a03c_01`'s snapshot.
- **The branch point is a03c_01.** Health is 90 and infection 85 there. The first set took a second
  attack at the creature by the seal (infection 100 from a03c_02 f452), and with it the drain of
  section 4. The branch passes that creature at once; infection stays 85 to the end of the set.
- **AREA07 is recorded to its end**: sub 0 (switch [28]), sub 2 (the side crawl, g[0]), sub 3 (door
  [11], [17]'s script, g[0]), and back to the lift [21] to AREA04 entry 10 (a07b_00..a07b_07).
- **Flag 0x25 = 0xFF, flag 0x15 = 0xFF, counter 0x15 = 0xFF, CC3 0x11 and CC3 0x14 are measured**
  (a07b_04 f4293). They are the writes read in 00823C20's C (`src/overlays/AREA07`).
- **Not entered**: AREA14, 16, 17, 18, 20, 21, and AREA19 sub 1's [53]. The set ends where the lift
  lands in AREA04 sub 1 at health 55, infection 85. This is a stop by instruction, not a block.

## 1. Tool and conventions

```sh
# macOS arm64 host (the fork runs x86_64 under Rosetta); from the decomp repo root, decomp .venv.
.venv/bin/python ../port-route16/tools/record_level_routes.py \
    --out-root build/fork_refs/levels-recordings-20261031-1 --append \
    --source-root build/fork_refs/levels-recordings-20261030-1 \
    --source-root build/fork_refs/levels-recordings-20261018-26 --beats a07b_01_sub2
```

The port's `tools/mid_route_beats.py` and `tools/build_start_saves.py` name every beat here (cards
`a03d_00` .. `a07b_07`, chains `a03d`, `a04d`, `a07b`). `python3 tools/build_start_saves.py` was run
for `a03d_00`, `a04d_00`, `a07b_00`, `a07b_04` and `a07b_07`: each passed the save and read-back
checks. The other cards were not built.

## 2. What decides each step

**a03d_00 (AREA03 sub 1, the seal).** As a03c_02, but the player waits at (412, 735) for the leap
(-20; the creature lands by (404, 736)) and leaves as soon as the reaction ends, by the south side
at z 742.5: (405, 742.5), (390, 742), (381, 733), (376, 716). No second attack. Health 70, infection
85, against the first set's 58 and 100.

**a04d_01 (AREA04 sub 1).** As a04c_01, with item 0x1E kept in the bag and no HEALING page: the
menu's frames were the ones in which the creatures closed in (first version: health 40).

**a07b_01 (AREA07 sub 0, chamber C to sub 2).** The walk east along z 541 stops at (291, 542.4)
(section 6); the diagonal from (288, 542) to (340, 628) passes it. Door [35] from the west at
(352.3, 645.5) facing +x. The lift room, north by (375, 580), west by (345, 578): the junction
creature (0012E3A0, 80, at (346.1, 574.5); [11] in this sub load's table, [10] in a07b_06's) hit once
on this line (-10). The slider [26] (needs D_00810848 bit 1: a07b_00) from (335, 535.5) facing -z.

**a07b_02 (sub 2, the side crawl).** Four flames [19..22] (001E3D90) and the player-only band walls
35 and 36 (attribute 0x51, z 181..208) close the corridor. The attribute-0x37 square x 358..365, z
220..227 facing +x: the hang (action 0x2C), stick up into the crawl, east to the junction at x 386.5,
north. g[0] (0015AFA0, item 0x1E) lies in the crawl at (386.2, 220.4, 186.1): Cross at z 187.7 takes
it, Triangle closes the page. On to the junction at z 155.5, west, out at the second square (x
358..365, z 152..159).

**a07b_03 (sub 2, the north arm).** West on the z 157..165 line to the slider [25] (001BB860, id
0x88) from (209.9, 165) facing -x: sub 3, entry 0. The hazard [13] by x 330 costs 5 on this line,
and on z 153 too (a07b_06 f144).

**a07b_04 (sub 3).** AREA08 sub 3's plan fits the room. Round the pit by (188, 140), (150, 135),
(146, 120); the stair (attribute 0x35, y 220 -> 250, +x); door [11] (001BC350) from (188.5, 94.5)
facing -x; [17] (0x823990, at (161.5, 257, 74)) from (161.5, 81) facing -z. Creature [9] (0012E3A0,
80) stayed by (135, 141) on this line, there and back: no hit. The script runs 2,997 frames with no
press needed: flag 0x25 = 1 at f1296, then the two grants and the bytes at f4293.

**a07b_05 (sub 3, g[0]).** g[0] (00219550, item 0x62) at (175.2, 250.1, 72.4), Use from (174, 80.2):
the walk stops there, about 4.5 short of the player-only wall 670 (z 75.8). Door [11] from the
west at (173.8, 95.3) facing +x, the stair, the slider [10] from (197.5, 165) facing +x.

**a07b_06 (sub 2, back).** The flames and the band stay after flag 0x15 (section 6). The crawl again
from the north square to the south one, item 0x1E at the south mouth (+30), the slider [23] from
(351, 330) facing +z: sub 0, entry 5, (335, 220, 541).

**a07b_07 (sub 0 to the lift).** AREA08's plan: the outer button [23] from about (395, 674.5) facing
+z, the inner button [24] from (367.5, 705) facing -x, the lift [21]: AREA04 entry 10, sub 1. A hit at
(350.4, 580.7) on the way (-15, f90), 7.7 units from the junction creature, the nearest 0012E3A0.

## 3. Route table

| Beat | Source | Frames | Counters | End |
|---|---|---:|---|---|
| `a03d_00_seal` | a03c_01_duct (first set) | 815 | 245751..246565 | D_00810844 = 0x81, D_00810845 = 0xAC; (235.1, 55, 633.1); health 70, infection 85 |
| `a03d_01_door24` | a03d_00 | 692 | 246565..247256 | **AREA04** sub 1 entry 2 (332, 54.9, 365.5) |
| `a04d_00_event` | a03d_01 | 8,509 | 247256..255764 | flag 0x28 = 0xFF, C64 0x29, CC3 0x10; (335.8, 14.9, 372); health 65 |
| `a04d_01_door73` | a04d_00 | 1,042 | 255764..256805 | item 0x1E held; north room entry 9 (440.5, 14.9, 97); health 60 |
| `a04d_02_lift` | a04d_01 | 2,264 | 256805..259068 | **AREA07** entry 0 (380, 220, 705); health 60 |
| `a07b_00_switch` | a04d_02 | 1,583 | 259068..260650 | D_00810848 = 2; chamber C (206.1, 220, 587.8), entry 0xB |
| `a07b_01_sub2` | a07b_00 | 2,053 | 260650..262702 | sub 2 entry 0 (350, 220, 313.4); health 50 |
| `a07b_02_duct` | a07b_01 | 1,389 | 262702..264090 | C64 0x1E = 2; (359.9, 220.1, 155.5), north mouth |
| `a07b_03_sub3` | a07b_02 | 748 | 264090..264837 | sub 3 entry 0 (191.5, 220, 165); health 45 |
| `a07b_04_terminal` | a07b_03 | 4,299 | 264837..269135 | flag 0x25 = flag 0x15 = 0xFF, CC3 0x11, CC3 0x14; (161.5, 250, 82), entry 2 |
| `a07b_05_back` | a07b_04 | 1,507 | 269135..270641 | CC3 0x62; sub 2 entry 1 (228.1, 220, 165.5) |
| `a07b_06_duct` | a07b_05 | 2,419 | 270641..273059 | item 0x1E used; sub 0 entry 5 (335, 220, 541); health 70 |
| `a07b_07_lift` | a07b_06 | 1,974 | 273059..275032 | **AREA04** sub 1 entry 10 (430.7, 14.9, 7.1); health 55, infection 85 |

Line a03c_01 -> a03d_00..01 -> a04d_00..02 -> a07b_00..07: 29,294 trace frames (one row of overlap per
beat), counters 245751..275032, each beat starting on the previous beat's last counter. Every beat
passed the recorder's resume check.

## 4. Story bytes, health and items (measured)

| Byte | Before | After | Where |
|---|---|---|---|
| D_00810844 (AREA03 locks) | 0x80 | 0x81 | a03d_00 f784 |
| D_00810845 (AREA04 locks) | 0xA8 | 0xAD | bit 2 a03d_00 f784; bit 0 a04d_00 f8413 |
| D_00810780 (flag 0x28) | 0 | 0xFF | 1 a04d_00 f258; 0xFF f8413 |
| D_00810800 (counter 0x28) | 0 | 1 | a04d_00 f8413 |
| D_00810836 | 0 | 0 | 1 a04d_02 f1701; 0 a07b_07 f1291 (the lift's two ends; its reader was not looked up) |
| D_00810848 (AREA07 locks) | 0 | 2 | a07b_00 f1519 |
| D_0081077D (flag 0x25) | 0 | 0xFF | 1 a07b_04 f1296; 0xFF f4293 |
| D_0081076D (flag 0x15) | 0 | 0xFF | a07b_04 f4293 |
| D_008107ED (counter 0x15) | 0 | 0xFF | a07b_04 f4293 |
| items C64 | | | 0x29 +1 a04d_00 f8413; 0x1E +1 a04d_01 f501, +1 a07b_02 f794, -1 a07b_06 f1534 (one held at the end) |
| items CC3 | | | 0x10 +1 a04d_00 f8413; 0x11 and 0x14 +1 a07b_04 f4293; 0x62 = 1 by the end of a07b_05 |
| health | 90 | 55 | 70 a03d_00 f225; 65 a04d_00 f177; 60 a04d_01 f85; 50 a07b_01 f1215; 45 a07b_03 f51; 40 a07b_06 f144; 70 f1625; 55 a07b_07 f90 |
| infection | 85 | 85 | no change in 29,294 frames |
| rounds | 51 | 51 | the magazine stays 21: no shot fired in the kept beats |

With infection 85 throughout, health fell only on hits; the 2-point ticks of the first set never
appear. That fits, but does not prove, the first set's reading that they belong to infection 100.

## 5. Census

Not run. `route_census.py` knows only the fifteen earlier groups.

## 6. Tried and not kept

Exploration runs from scratch snapshots (deleted); leads, not ground truth.

- **The walk east on z 541 in AREA07 sub 0**: stopped at (291, 542.4), clip 3, on both tries. Going
  west the same stretch passed at z 536. The grid read shows no low wall there and the table no
  actor; what stops it was not identified.
- **Shooting the junction creature from (365, 572)**: with R1 and Circle, health of the creature 80 -> 40 for 8 rounds
  (mag 21 -> 13). A melee took infection 85 -> 100 at no health. The creature then left its normal
  state and its hit points read 0 by f1890 with no further round fired, while the player lost 10 and
  then 5. Sub 2 was reached at health 42, infection 100. Not kept: 42 with the drain is worse than 50
  without it. What brings the creature from 40 to 0 was not read.
- **Running past the junction creature on an earlier timing**: the same path, no shot, health 60 ->
  20. The kept beat took one hit. Timing and random draws decide this, not the path alone.
- **The flames after flag 0x15**: north arm to the corridor, (350, 175), then south. The band stops
  the player at z 189.6 and health falls by 5 about every 90 frames, 35 -> 0 by f2159. The band is
  still solid and the flames still burn with D_0081076D = 0xFF. Nothing in the beats turns them off.
- **A waypoint at (250, 165) in sub 2**: a full-stick orbit, and a bite (infection +10).
  A too-tight waypoint in a corridor with bugs costs more than it saves.
- **go_rock to (395, 674.5) with tolerance 0.6 at the outer button**: 480 frames of orbit, a bite
  (infection 85 -> 95). Tolerance 2.0 keeps the beat at infection 85.
- **a04d_01 with the HEALING page**: health 40 at the slider. Replaced by the no-menu version.
- **a07_00's walk unchanged in this branch**: stopped at (225, 542) by a hit; a07b_00 ends the walk
  softly.

## 7. Open

1. **Sub 3's cabinet pickups** g[1] (item 0x20) and g[2] (item 0x1E), (167.9, 232.3, 158.3) and (165.4,
   232.4, 158.2), lie in the box x 160..172, z 150..160, inside the player-only walls at x 172.4, z
   144.4 and z 160.4 (attribute 0x51), with a bug in it. Read from the grid, not tried. How the player
   reaches them is unknown. With item 0x20 out of reach the bed [42] in chamber A was not played.
2. **Item 0x62** (g[0] in sub 3) was taken; what it does or which page it opens was not read. CC3 0x63
   is AREA08's door [6] script (SIXTEENTH section 7).
3. **What ends the flames [19..22]**, if anything. [30] (001C1A80, type 0x52) stands by them at (350.5,
   220, 203.9); not read.
4. **The turrets [1..8] in sub 2** (0x823F80 / 0x8259D0, y 265) never fired at the player in the kept
   beats; whether they need a condition was not read.
5. **The junction creature's drop from 40 to 0** (section 6).
6. **The hazard [13]** (001BECC0 with 001BE6C0 and 001BF3C0 children at (330.1, 220.8, 165.8)): -5 per
   pass at z 153 and z 157; its range was not read.

## 8. Where the recording stops, and what the next run needs

a07b_07 ends in AREA04 sub 1 at (430.7, 14.9, 7.1), entry 10, health 55, infection 85, 21 in the gun and
51 held, one item 0x1E in the bag. It stops there by the user's instruction. The way on from the graph,
not played: AREA19 sub 1's [53].

**What the port lanes need.** The recordings above; cards from `tools/build_start_saves.py`; chains in
`tools/mid_route_beats.py`. rand() replay tables were not recorded (`tools/record_rand_replay.py <beats>`
needs the set as source). New asset exports: AREA07 subs 2 and 3 (AREA07 sub 0 and AREA04 sub 1 come
from the first set's list). New overlay code on the live path: 0x823990 and 00823C20, script 0x826930,
0x823F80, 0x8259D0, all in decomp `src/overlays/AREA07`. New boot behaviours in the actor tables: 001E3D90
flames, 001BECC0 / 001BE6C0 / 001BF3C0 hazard, 001C1A80, 0015AFA0 pickups in a crawl, 00219550 item
0x62, 001546C0, 00156620 drums, 001551B0 crates, 0022DCD0, 001C50B0, 001C5930. No census pass was run.

A port lane porting a07 from the first set stops at chamber C at health 17. To go on it must take this
branch from a03c_01 instead: the first set's later beats and this set's beats are separate lines with
their own random draws, and neither continues the other.
