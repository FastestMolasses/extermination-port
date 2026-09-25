# Player FLOOR-closure states 0xE, 0x13, 0x14 and 0x18

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "player-closure-0e-18". This document covers four player states of the
FLOOR closure (FIRST_CONTROL.md "FLOOR state closure", `kFloorStates` in
`em_player.c`) and the private callees they reach:

| +4 | +5 | routine | entered by |
|---|---|---|---|
| 1 | 0xE | 00168050 | the hang 001647D0 sub-state 0x23 with +1F1 = 6 (0016575C: +1F0 0x1D, +D 2) |
| 1 | 0x13 | 0016B790 | 001696A0 at 001696D4 (its only caller is 00169730; it tests pad bits D_00810E70 & spad 0x70003B7C / 0x70003B7E) |
| 1 | 0x14 | 0016B8A0 | 0016B790 at 0016B87C; 00223C70 at 00223F48 / 002240D4 |
| 1 | 0x18 | 0016D130 | the hang 001647D0 sub-state 0x23 with +1F1 = 5 (001657E8: +1F0 0x2C, +D 2); also the ladder 001662D0 at 001679E8 (+1F0 0x2C), which `kFloorStates` does not cite |

It records what each routine does, the translation
`src/game/em_player_closure_0e_18.c/.h`, the evidence and the binding. The
module is **live in AREA11** (bound by em_player_closure_live.c): section 5 lists what the
coordinator binds. None of the 15 route beats reaches these states (section
4.3), so the unit oracle is the only evidence.

## 1. What the original does

Every routine works on the 0x320-byte player record by original offsets.
"The row clip" is 001749A0(p, 00188550(p), 0, 16.0). "node0" and "node1" are
`*(D_00275B40)` and `*(D_00275B40 + 4)`, the words at the record's own +40 /
+44 (PLAYER_HANG.md section 1). "The Use test" is `D_00810E74 & spad
0x70003B76`. "Clip end" is +200 & 0x1000; "blend over" is +200 & 0x8000
clear. Sound calls are 001FBD50(p, id, 0, 300.0).

### 00168050: state 0xE (dispatch on +6)

| +6 | what it does |
|---|---|
| 0 | +6 = 1 and +290 = +B0 (001031E0), then falls into 1. |
| 1 | 00181180(p, 0) != 0 returns (the reaction entry +4 2 / +5 5). Else 00174FD0, then by +24C: **0** 001806E0 (0 gives +6 0xA, 2 gives 0x1E); **1** 00180790 (0 gives 0x14, 2 gives 0x28); **2** 00180850(p, 0) (0 gives 0x32, else 0x46); **3** 00180850(p, 1) (0 gives 0x3C, else 0x50). Always then +7 = 0, +21C = +38 = +2E4 = 0. |
| 0xA / 0x14 | 00181180(p, 1) for 0xA, (p, 0) for 0x14; nonzero returns. **+7 0:** +7 = 1 and 00180000 / 00180040 at 4.0. **+7 1:** blend over gives +7 = 2. **+7 2:** 00174FD0. Without the clip end: root motion, +2E4 = node0 +4 − +21C, +21C = node0 +4, +B4 += +2E4, and when +24C is still 0 / 1, +204 = 2.0 / 1.5 / 1.0 for gait (+23F) 3 / 2 / other. At the clip end: 00182AB0, +2F1 = 1 − +2F1, then with +24C == 0 / 1 the reach test again (001806E0 / 00180790): a free reach replays the clip at 1.0 and clears +21C; 001806E0's 2 (00180790's other value) sets +6 0x1E (0x28), +7 0, +1F0 0x18; otherwise, or with another +24C, +6 = 0 and 0017FF80(p, 8.0). Every clip-end path ends with +290 = +B0. |
| 0x1E / 0x28 | +6 + 1, 001749A0(p, 0xA5 / 0xA6, 0, 4.0). |
| 0x1F / 0x29 | Blend over gives +6 + 1. |
| 0x20 | By +7, at +3C ≤ 78 / 64 / 42 / 25 / 14: +7 + 1 (the last: +6 + 1), with 00182AB0 for the first two and 00182430(p, 2) for the rest. |
| 0x21 | At the clip end: 001C68C0, +B0..+BC = node1 +C0..+CC (00102948), +B4 −= 10.5 then += −0.2, 00175900(p, 1), 00182430(p, 2), 00187EE0(p, p+B0, p+D0), 00174AB0, 00174A50(p, 18.0); then +5 = 0, +6 = 0, +1F0 = 0. |
| 0x2A | At +3C ≤ 22: +6 + 1, +290 = +B0 (quadword), then as 0x21 with 11.5 up to 00187EE0, and +B0 = +290 again (the position is restored). |
| 0x2B | At the clip end: as 0x21 with 11.5, without 00182430 / 00187EE0. |
| 0x32 / 0x3C | 00181180(p, 0). **+7 0:** +7 = 1, 00180080 / 001800C0 at 8.0. **+7 1:** blend over gives +7 2. **+7 2:** 00174FD0; with +24C not 2 / 3: +6 = 0, 0017FF80(p, 8.0), 00182AB0, return. Else 00180850(p, 0 / 1): 0 steps sideways, +38 = −D_00248610[+23F] (0x3C: +D_00248610[+23F]), +204 = D_00248620[+23F], +B0 += +38·cos(+C4), +B8 −= +38·sin(+C4); nonzero sets +6 0x46 / 0x50, +7 0. Then a clip end plays 00182AB0. |
| 0x46 / 0x50 | 00181180(p, 0). Side s = 0 / 1. **+7 0:** +7 1, 00180100(p, s, 4.0). **+7 1:** blend over: +7 2, sound 0x120 / 0x121. **+7 2:** clip end: +7 3, 00180180(p, s, 1.0). **+7 3:** 00174FD0; a Use press runs 001809B0(p, s) and on nonzero sets +6 0x5A, +7 0, +1F0 0x1F and 00180280(p, s, 1.0); without Use, +24C other than 2 / 3 gives +7 + 1 and 00180200(p, s, 1.0). **+7 4:** clip end: +6 = 0, 0017FF80(p, 16.0). |
| 0x5A | At +3C ≤ 6.0: +6 + 1, sound 0x187, the eight-frame move: +2F4/+2F8/+258 = +2E0/+2E8/+2E4 (targets), +28 = 8, steps +2E0/+2E8/+2E4 = (target − position)/8, t = 001B1470(+218 − +C4) (also at 0x70003A20), +26C = \|t\|/8; clip 0x7A at 8.0. |
| 0x5B | +28 != 0: steps +B0/+B8/+B4, +C4 = 001B12B0(+218, +C4, +26C), +28 − 1. +28 == 0: +6 + 1, the targets placed, +C4 = +218, sound 0xFE. |
| 0x5C | Clip end: +5 = 9, +6 = 0, +1F0 = 0x10, +D = 0, the row clip (back to the hang). |

The reach tests (all use 00180420 first: +290 = (0, 0, −3, 1) × M(+D0)):

- **00180300(p, v, kind)**: spad 0x70003600 = (0, 0, 10, 0); 0x70003610 =
  (0x70003600 × M) + v; 0019AFE0(p, v, 0x70003610, 6). 0 returns 2. Else
  +23B = the hit's surface byte (`*(*(0x700031D0) + 0x1A)`), and kind 0 / 1 / 2
  returns 0 on surface 0x32 / 0x3B / 0x33, else 1; any other kind returns 1.
- **001806E0**: the point (+290 x, 18 + +B4, +298 z); 00180300 with kind +D;
  2 returns 2; else y = 6 + node1 +C4 and 00180300 again.
- **00180790**: the point +290 with y − 4.5; 0019AB20(p, point, p+280, 6)
  nonzero returns 2; else y = node1 +C4 − 6 and 00180300 != 0 returns 1.
- **00180850(p, flag)**: the offset (±4.5, 0, 0, 0) × M (+ for flag != 0);
  for y = node1 +C4 + 2.0 and − 2.0 (D_002754B0): 00180300 on (+290 x, y,
  +298 z, 1) + offset; both 0 returns 0, else 1.

The clip requests: 00180000 (its body is split out as 00180004), 00180040,
00180080 and 001800C0 request (0x99/0x9B), (0x9C/0x9A), (0x9D/0x9F),
(0x9E/0xA0) by +2F1 == 0 / != 0 with force 1; 00180100, 00180180, 00180200
and 00180280 pick by (sel, +2F1) from (0xA7, 0xAD, 0xAA, 0xB0), (0xA8,
0xAE, 0xAB, 0xB1), (0xA9, 0xAF, 0xAC, 0xB2), (0xA1, 0xA3, 0xA2, 0xA4) with
force 0. Each passes the caller's $f12 as the blend. 00182AB0 is sound
0x11B + 00179B90(); 00174AB0 is 001749A0(p, 0, 1, 0.0).

### 0016B790: state 0x13

| +6 | what it does |
|---|---|
| 0 | +6 = 1, +7 = 0, 001749A0(p, 0x182, 0, 8.0), then D_00275B00 + 8 = 0. |
| 1 | Blend over: +6 = 2, 0016BAE0(p, 0). |
| 2 | Clip end: +6 = 3, +1F0 = 0x27, 001749A0(p, 0x180, 0, 1.0). |
| 3 | 001607D0(p) == 0 gives +4 = 1, +5 = 0x14, +6 = 0, +1F0 = 0x26. |

**0016BAE0(p, arg)**: n = 001AFA90(8); when a node is allocated: n+3 = 5,
n+D = arg, n+B0..+B8 = the record's, n+BC = 1.0, n+C0..+C8 = 0, n+CC = 1.0,
n+60..+6C = 1.0, and n+10 = 00188340 (its update routine).

### 0016B8A0: state 0x14

| +6 | what it does |
|---|---|
| 0 | +6 = 1, 001749A0(p, 0x180, 0, 1.0), then +C0 = 0. |
| 1 | By D_00275B14: **0x1E** gives +6 0xA. **0x34** gives +6 2; d = 001B1470(D_00281B64 − +C4) (also at 0x70003A20); +218 = D_00281B64 when \|d\| (0011DF78) ≤ π/2, else 001B1470(π + D_00281B64); +2E0 = D_00275B10, +2E8 = D_00275B0C. Always +7 = 0. |
| 2 | +C4 = 001B12B0(+218, +C4, 0.13962634); equal to +218 (C.EQ.S) gives +6 0xA. |
| 0xA | +6 = 0xB, 001749A0(p, 0x183, 0, 8.0). |
| 0xB | Blend over: +6 = 0xC, D_00275B00 + 8 = 1. |
| 0xC | Clip end: +23B 0x1E gives +5 0x12 / +1F0 0x22, else +5 0x10 / +1F0 0x21; +6 = 0, +2F1 = 0, then 001749A0(p, 001885B0(p), 0, 16.0). |

The globals come from 001696A0 (decomp C, lane player-closure-10-12-19): it
enters +5 0x13 and stores D_00275B14 = 0x34, D_00275B10 = +2E0, D_00275B0C =
+2E8 and D_00281B64 = +C4. 00181D70 (em_player_major2) also stores
D_00275B14 (0x34, 0x36 or 0x1E). The decomp's comment on 0016B8A0 calls
D_00275B14 a "level id"; that label is wrong (a label is not evidence).

### 0016D130: state 0x18

| +6 | what it does |
|---|---|
| 0 | +38 = 0, then by +D: **0** +6 3, clip 0x152 at 8.0, +28 = 0x50, sound 0x122. **1** +6 + 1, clip 0x70 at 8.0. **2** 0017F240(p, 0) != 0 returns (without +7 = 0); else +1F1 = 0; D_00810700 == 0 gives +6 0x14; else 00174FD0 and by +24C: 0 gives +6 0xA and +1F1 1; 1 with a Use press gives +6 0x14; 2 / 3 give +2F1 0 / 1 and +6 0x28. **3** +6 0x1E, clip 0x70 at 8.0. Then +7 = 0. |
| 1 | Clip end: +6 = 2, 00182870(p, 0); f = 001C61D0(+40, 0x79) as a float (0x70003A20); anim_clip_arbiter(p, 0x79, 2.0, f − 53); +258 = +254 − (+B4 + 17.0) (D_002488AC); +2E4 = 0.6; +28 = float_to_int(+258 / +2E4) (the quotient also at 0x70003A20); +254 −= 17.0; sound 0x12C. |
| 2 | +28 − 1; on the frame it read 0: +6 + 1, +B4 = +254, +28 = 0x18; else +B4 += +2E4. |
| 3 | +28 − 1; on the frame it read 0: +6 + 1, sound 0x13F + 00179B90(), 001AEDE0(4, 0). |
| 4 | D_0028A9A0 != 2 gives +204 = 0. At 2: +5 = 0x19, +6 = 0, +1F0 = 0x2D; +D == 1: +B4 = node1 +C4, +B0 += 7.5·sin(+C4), +B8 += 7.5·cos(+C4), build_trs_matrix, 00179150; else +B0 / +B8 = node1 +C0 / +C8. Then +1 = 0, 001AEE10(4, 0). |
| 0xA | +6 = 0xB, clip 0x7C at 5.0. |
| 0xB | Blend over: +6 = 0xC, sound 0x12C. |
| 0xC | +3C ≤ 18: +6 = 0xD, 001AEDE0(4, 0). |
| 0xD | D_0028A9A0 != 2 gives +204 = 0. At 2: with D_00810700 == 0: +5 0x1A, +6 0, +1F0 0x2E, +D 0, the record placed at (186, −19.5, −1463.5) facing π, D_00810702 = 0xA, 001B0460(1). Otherwise +5 0x19, +6 0, +1F0 0x2D, +1 0, +B4 += 20.5, build_trs_matrix, +B0 = (0, 1, 5, 1) × M (001026A0 into +B0), 00179150. Then 001AEE10(4, 0). |
| 0x14 | +6 = 0x15, clip 0x80 at 4.0, +21C = +38 = +2E4 = 0, +2F4 = +B4. |
| 0x15 | Clip end: +5 = 7, +6 = 0, +1F0 = 0xD (the drop 001639E0). While the blend is over: +38 = node0 +8 − +21C, +21C = node0 +8, 00178B90(p, 1), +2EC = node0 +4 − +2E4, +2E4 = node0 +4, +B4 += +2EC. |
| 0x1E | Clip end: +6 = 0x1F, clip 0x71 at 1.0, +25F = 1, 00182870(p, 0), +254 −= 20.5, +2E4 = 0.6, +28 = float_to_int((+254 − +B4) / +2E4). |
| 0x1F | +28 − 1; on the frame it read 0: +6 + 1, +B4 = +254, clip 0x7A at 1.0; else +B4 += +2E4. |
| 0x20 | Clip end: +6 = 0, +1F0 = 0x2C, +1F1 = 0, +D = 2, the row clip. |
| 0x28 | 0017F240 == 0: +6 + 1, 0017DFB0(p, +2F1, 8.0). |
| 0x29 | 0017F240 == 0 at the clip end: +6 + 1, 0017E0D0(p, +2F1, 1.0). |
| 0x2A | 0017F240 == 0: 00174FD0; +24C != D_00275498[+2F1] gives +6 0x31 and 0017E150(p, +2F1, 1.0). Else a Use press runs 00178620(p, +2F1) into +1F1; 1 or 3 gives +6 + 1, +D = 4, 0017E1D0(p, +2F1, 1.0), then +1F0 = 0x12. |
| 0x2B | As 00168050's 0x5A, with clip 0xE5 for +1F1 == 1, else 0x7A. |
| 0x2C | As 0x5B; on arrival 00182A70 for +1F1 == 1, else sound 0xFF. |
| 0x2D | Clip end, by +1F1: **1** 00182A70, +5 0xC, +6 0, +1F0 0x17, +D 1, +2F1 0, 0017FC80(p, 16.0) (the ladder 001662D0). **3** +5 9, +6 0, +1F0 0x10, +D 1, the row clip (the hang). |
| 0x31 | 0017F240 == 0 at the clip end: +6 = 0, the row clip. |

- **00178620(p, side)**: the points (∓9, 19.5, −6, 1) and (∓9, 19.5, 4, 1)
  (−9 for side 0) × M into 0x700038C0 / 0x700038D0; 0019AFE0(p, near, far, 7).
  With (result & 6): surface 0x3D and 00178390 != 0 return 3; surface 0x3B
  and 001782A0 != 0 set +D = 1 and return 1. Every other path returns 0.
- **00179150**: +B0 += sin(+C4)·(+38·cos(+9C)); +B8 += cos(+C4)·(+38·cos(+9C))
  (+9C and +38 re-read for the second), then 001790B0.
- **001790B0**: +314 = 0; for each of the seven D_00248970 points: 0x700038A0
  = point × M, and 0019AD00(p, 0x700038A0, 0x80000007) nonzero sets +314 bit i.

### Exits

00168050 leaves to +5 0 (0x21, 0x2B), +5 9 (0x5C), +4 2 / +5 5 (00181180).
0016B790 to +5 0x14 (and whatever 001607D0 enters). 0016B8A0 to +5 0x10 /
0x12. 0016D130 to +5 7, 9, 0xC, 0x19, 0x1A and +4 2 / +5 6 (0017F240). All
are in the FLOOR closure table.

## 2. Reading: the instructions settle

The translation was read from `build/asm` (instructions), with the decomp C
alongside. The C of the four states and of the callees agrees with the
instructions on every point checked. The instructions settle:

- **Delay-slot stores come before the call:** +6 before 001031E0 (00168158),
  00102948 (001688C0), 0017FF80 (0016838C and six more), 00179B90 (0016D4FC)
  and 00188550; +7 before 001749A0 (0016B7F0); +21C before 00178B90
  (0016D858); +B4 before 00175900 (00168808), build_trs_matrix (0016D5C0,
  0016D760) and 001749A0 (0016D958); +254 before sound 0x12C (0016D490);
  0x70003A20 before float_to_int; +2F1 before 0017FC80 and 001885B0; +D
  before 00188550; D_00810702 before 001B0460.
- **Stores after a call** (in a `b` delay slot): +21C = 0 after 00180000 /
  00180040 (00168354), +C0 = 0 after the request (0016B91C), D_00275B00 + 8 =
  0 after the request (0016B7F8), +1F0 = 0x12 after 0017E1D0 (0016DB20), +28
  after float_to_int.
- **Reads around calls:** node1 is read after 001C68C0; node0 +8 before and
  node0 +4 after 00178B90 (0016D130 0x15); +38 before sin/cos in 00179150
  (its product sits in the call's delay slot); +7 re-read after 00174FD0
  (00168E24); +2F1 re-read before 0017E150 / 0017E1D0.
- **00180000 is split** by splat into 00180000 (the frame setup) and
  00180004 (the body). Callers call 00180000.
- **The clip requests take the blend in $f12** that their C prototypes omit
  (00180040..00180280, 0017FF80). 00168050's C declares 00182430 without
  arguments; the instructions pass (p, 2).
- **00178390 and 001790B0 are asm-word files**; 001790B0 was read from them.
- **Default:** 0016D130 +6 = 0 with +D outside 0..3 only sets +38 = 0 and +7 = 0.

## 3. The translation (`src/game/em_player_closure_0e_18.c/.h`)

- **The record.** Every routine works on `EmPlayerLiveActor` by original
  offsets. Arithmetic is `em_ee_float.h` on raw bits (ADD.S/SUB.S with the
  pre-trim, DIV.S round-to-nearest, MUL.S, NEG.S, CVT.S.W, the DAZ compares).
- **Entry points.** `em_player_closure_state0E/13/14/18` are
  `EmPlayerStateCallback`s whose context is a `const EmPlayerClosureWorkers *`.
  Every private callee is exported as `em_player_closure_<address>` for the
  other lanes that call it (section 5.4).
- **Reused translations** (called, not re-translated): 00181180
  `em_player_major2_00181180`, 0017F240 `em_player_hang_0017F240`, 0011DF78
  `em_sdk_math_original_0011DF78`, and since 2026-09-24 (section 5.4, "One
  owner") 00180420 and 00174AB0 (`em_player_ladder_climb_00180420` /
  `_00174AB0`) and 00180300 (`em_player_ladder_probe_00180300`). The copies
  00102948 / 001031E0 are written out as moves.
- **Workers.** Every other callee is one of 41 workers in
  `EmPlayerClosureWorkers`; five more are reads and stores outside the record
  (`scene`, `node`, `hit_surface`, `set_275B08`, `set_810702`), made at the
  point of the original access. The scratchpad words (0x70003600..1F,
  0x700038A0..DF, 0x70003A20) live in one binder-owned
  `EmPlayerClosureScratch`.
- **Fail-stop.** Every entry point refuses (−1, nothing written) unless every
  worker and the scratch are bound. A worker returning < 0 stops the routine
  there, with the writes before it kept.
- **The small tables** are embedded: D_00248610, D_00248620 (4 words each),
  D_002754B0 (2), D_002488AC (1), D_00275498 (2) and D_00248970 (28). The
  oracle checks the ELF bytes and that every entry is read.
- **Two additions** to the original, both faults instead of reading outside
  a table: +23F > 3 in 00168050's 0x32 / 0x3C, and +2F1 > 1 in 0016D130's
  0x2A. The original's +23F is 0..3 and its +2F1 is 0 / 1 there.
- **The spawn.** 001AFA90's node is handed back as a byte view by original
  offsets; +10 holds the original update address 0x00188340
  (`EM_PLAYER_CLOSURE_SPAWN_UPDATE`), which the binder maps to its own
  00188340 update.

## 4. Verification

### 4.1 Unit oracle (`tools/test_player_closure_0e_18_reference.py`)

`ClosureEE` is the hang lane's `HangEE` (the shared interpreter with every
COP1 op through `tools/ee_float_model.py`), subclassed to record this lane's
branches. The shared files are unchanged. The original instructions run
from the captured RAM (state 04), whose code and table bytes are checked
against the pinned ELF first. The player record sits at its captured
address 0x8102B0, with the captured node pointers at +40 / +44.

- **Executed unmodified:** the four states and the 19 private callees, plus
  00181180, 0017F240, 0011DF78, 00102948 and 001031E0 inside them. The
  native side of 00180420, 00180300 and 00174AB0 is the ladder lanes'
  translation reached through this lane's bridges, so every case that runs
  them checks the bridge and the owner together (the build links
  em_player_ladder_climb.c and em_player_ladder_entry.c).
- **Hooked:** the 41 worker callees, scripted per case (return values;
  record, node and hit-surface writes; vector outputs), the same script
  replayed by the native workers. The hooked set is asserted equal to the
  jal/j targets of the executed code.
- **Compared** after the call and at every worker call: all 0x320 record
  bytes, the 25 scratchpad words, D_00275B08, D_00810702 and the 0x100
  spawned-node bytes; the full worker call sequence with every argument
  (floats as bits, vectors as the words the callee receives); the return
  value of each exported callee.
- **Coverage asserted:** all 470 conditional-branch outcomes of the 23
  translated routines; each of the 50 handled (state, +6) pairs; each of the
  19 exported callees run directly with random arguments; every embedded
  table entry read.
- **Faults:** a random worker fails in about 1 case in 400; the native must
  return −1 with exactly the calls up to it and the original's state at that
  call. **Refusals:** each of the 47 bindings removed in turn on the four
  states and on 00180850 (235 checks), plus the +23F = 4 and +2F1 = 2 faults.

Results (2026-09-23): quick (default) 16,000 of 400,000 cases, about 6 to
10 s; full (`EM_TEST_FULL=1`) all 400,000 cases (1,114 injected faults),
2 min 28 s. Both PASS.

### 4.2 Mutants

Eighteen hand mutants of the C were run against the quick oracle; 17 were
killed: `≤ 6.0` as `<`, a wrong gait-2 speed, the 0x70003A20 store dropped,
kind 2's surface 0x33, +21C stored after 00178B90, +38 read after the sine
call, the spawn's +D, +21C cleared before 00180000, +7 cleared on 0017F240's
exit, the 00180850 mask test, the shuffle sign, six D_00248970 probes, a host
DIV.S for the +26C step, the D_0028A9A0 fade test as `< 2`, one D_002488AC bit,
one D_00248970 bit and the +7 re-read. Three of them (the +38 read order,
the host DIV.S and the fade test) first survived and were killed after the
scripts were widened: sin/cos now also write record words, wrap returns
denormals, and the fade word takes values above 2. The survivor (0x2A
passing the first +2F1 read to 0017E150) is equivalent: no call sits
between the two reads.

### 4.3 Route captures

The 15 route beats (`../Extermination/build/s87/route/*/trace.json`, field
`p5`) never enter +5 0xE, 0x13, 0x14 or 0x18: their sequences use only 0, 1,
2, 5, 6, 8, 0xB, 0xC, 0x1C and 0x25. There is no route replay for these
states; the unit oracle is the only evidence. AREA11 reachability is
therefore observed, not proven (as for the rest of the closure).

## 5. Binding (coordinator)

**Bound live in AREA11 since the Boxes step (2026-09-24):** `em_player_closure_live.c` binds this module over the live player record (FIRST_CONTROL.md "Engaged"). Workers with no translation are fail-stop workers that name their original. The notes below are the binding it follows.

Nothing is wired.

### 5.1 Stage slots

`b.stage.state[0xE] = em_player_closure_state0E`, `state[0x13] =
em_player_closure_state13`, `state[0x14] = em_player_closure_state14`,
`state[0x18] = em_player_closure_state18`, each `state_context[...] =
&closure_workers` (one `EmPlayerClosureWorkers`). This fills the four
`kFloorStates` entries 0x0E, 0x13, 0x14, 0x18.

### 5.2 Workers

| worker | original | binds to |
|---|---|---|
| `scene` | D_00810E74, spad 0x70003B76, D_00810700, D_0028A9A0, D_00275B14, D_00275B0C, D_00275B10, D_00281B64 | the canonical owners: the pad block, the area byte, the fade block (em_scene_state), and the D_00275B14 / D_00275B10 / D_00275B0C / D_00281B64 words shared with `EmPlayerMajor2Scene.d275B14` (00181D70) and the 10_12_19 lane's 001696A0 |
| `node` | node0 / node1 words | the skeleton records at the record's +40 / +44 |
| `hit_surface` | `*(*(0x700031D0) + 0x1A)` | the hit the bound `sweep` left (shared state, as in PLAYER_HANG.md) |
| `set_275B08`, `set_810702` | D_00275B00 + 8, D_00810702 | the canonical words (em_actor_pool also reads D_00810702) |
| `request` | 001749A0 | the same request worker the hang / fall lanes bind |
| `sound` | 001FBD50 | the player sound worker |
| `steer` | 00174FD0 | untranslated here; the hang lane's `steer_input` binding |
| `ledge_move` | 001809B0 | `em_player_ladder_climb_001809B0` (lane ladder-climb) through an adapter |
| `clip_FF80`, `clip_FC80` | 0017FF80, 0017FC80 | 0017FC80: `em_player_ladder_climb_0017FC80`; 0017FF80 untranslated |
| `clip_DFB0`, `clip_E0D0`, `clip_E150`, `clip_E1D0` | 0017DFB0.. | untranslated (the hang lane has the same workers) |
| `surface_sound` | 00182430 | `em_player_step_sounds` (em_player_floor.h; checked by the footstep oracle), bound live over the record's +23A / +23C by em_player_closure_live.c x_surface_sound since census L03 |
| `sound_109` | 00182A70 | untranslated |
| `land_sound` | 00182870 | `em_player_reaction_00182870` |
| `floor` | 00175900 | `player_states_floor_service` |
| `place` | 00187EE0 | as `surface_sound`: inside the footstep module, not exported |
| `pose_reset` | 00174A50 | `em_player_stage_row_request` (em_player_stage_workers.h) |
| `skeleton`, `translate`, `arbiter`, `clip_frames` | 001C68C0, 00178B90, anim_clip_arbiter, 001C61D0 | the animation workers the hang / fall lanes bind |
| `use_test` | 001607D0 | `em_player_weapon_001607D0` (em_player_weapon_states_a.h; same signature, context an `EmPlayerWeaponStates`) |
| `clip_row`, `clip_row_B` | 00188550, 001885B0 | the row-clip workers (em_player_major2 has both as workers) |
| `ledge_3D`, `ledge_3B` | 00178390, 001782A0 | untranslated |
| `sweep`, `point_test` | 0019AFE0, 0019AD00 | `em_coll_move_sweep_0019AFE0` and `em_coll_move_0019AD00` through adapters |
| `ground` | 0019AB20 | `em_actor_collision_player_ground` (writes +280) |
| `random` | 00179B90 | untranslated (it is 00122BB8() & 7, less 5 when it is 5 or more) |
| `to_int` | float_to_int (001281C0) | untranslated |
| `fade`, `fade_end`, `camera` | 001AEDE0, 001AEE10, 001B0460 | the scene coordinator's fade and 001B0460 |
| `alloc` | 001AFA90 | `em_actor_pool_alloc_001AFA90` through a byte-view adapter |
| `trs`, `transform`, `vadd` | build_trs_matrix, 001026A0, 001028B8 | the SDK matrix workers; `em_player_hang_vadd` fits `vadd` through a bits adapter |
| `sine`, `cosine` | 0011E2A8, 0011DE90 | `em_sdk_math_original_w_0011E2A8` / `_w_0011DE90` (exact fit) |
| `wrap`, `approach` | 001B1470, 001B12B0 | `em_player_001B1470` (bits, through an adapter); `em_script_host_approach` (exact fit) |

The workers marked untranslated keep the states gated: binding them is the
remaining work.

### 5.3 The scratch

`scratch` is one binder-owned `EmPlayerClosureScratch`. The routines write
it and hand its words by value to `transform`, `vadd`, `sweep`, `ground` and
`point_test`. 00178390 and 001782A0 read the hit that `sweep` left (spad
0x700031B0 / 0x700031D0): the binder shares that state between `sweep`,
`hit_surface`, `ledge_3D` and `ledge_3B`.

### 5.4 Exported callees and duplicates

- `em_player_closure_00179150` fills the 10_12_19 lane's `w00179150` worker
  (0016DE40).
- `em_player_closure_0016BAE0` serves 0016BC40 and 001834E0.
- **One owner (2026-09-24).** 00180420, 00174AB0 and 00180300 are no longer
  translated here. 00180420 and 00174AB0 run from em_player_ladder_climb.c
  (the census owner of 00180420 and 0017FC80; 00174AB0 moved with them, so
  one module holds the three leaves the climb, the closure and the ladder
  entry share), 00180300 from em_player_ladder_entry.c (its translation reads
  the hit record's surface byte from the probe state and faults on a missing
  record; its world mode ran it 196 times over route beat 10). The exports
  `em_player_closure_00180420` / `_00180300` / `_00174AB0` remain as this
  lane's entry points and run the owners through bridges in
  em_player_closure_0e_18.c:
  - 00180420: an `EmPlayerLadderClimb` whose `transform` is this lane's
    001026A0 worker and whose scene view of 0x700038A0..DF is a copy of
    `scratch->s38A0`;
  - 00180300: an `EmPlayerLadderWorkers` with `apply` / `vadd` /
    `sweep_0019AFE0` over this lane's `transform` / `vadd` / `sweep`, a
    probe-state view of 0x70003600..1F, and, when the sweep hits, the hit
    record's surface byte from `hit_surface` (the owner reads it next, with
    nothing between);
  - 00174AB0: an `EmPlayerLadderClimb` whose `request` is this lane's.

  Each bridge copies the scratchpad view back into `scratch` before every
  worker call and after the routine, so the workers and the per-call
  snapshots see the words as the original leaves them.

## 6. Limits

- **No route evidence** (section 4.3).
- **Untranslated workers:** 00174FD0, 0017FF80, 0017DFB0, 0017E0D0,
  0017E150, 0017E1D0, 00182A70, 00178390, 001782A0, 00179B90, float_to_int,
  and 00187EE0 as a standalone routine, besides the animation,
  sound and fade workers listed above.
- **Table faults** (+23F > 3, +2F1 > 1) replace reads of neighbouring data.
