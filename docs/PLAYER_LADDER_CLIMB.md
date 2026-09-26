# Player ladder climb: state +5 = 0xC (001662D0 and its helpers)

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "player-ladder-climb". This document covers the original ladder state
001662D0 (0015B130's table entry 0xC) and the helpers it calls: the clip
pickers 0017FC80, 0017FD00, 0017FD40, 0017FD80, 0017FE00, 0017FE80 and
0017FF00, and the probes 00180420, 00180460, 00180530, 00180600 and 001809B0.
It records what they do, how they were read, the native translation
(`src/game/em_player_ladder_climb.c/.h`), how to bind it and the evidence.
The translation is bound live (section 4) and, since census L10, the level
smoke's `cage_ladders` phase runs both cage climbs of route 10 through it
(entry 0xB, climb 0x17 with clips 0xE8 / 0xEA, dismount 0x18 with clip
0xF0) and equals the capture row for row (LEVEL_SMOKE.md).

## 1. What the original does

### Entry

- **From the attribute-0x32 grab.** A Use press on an attribute-0x32 grid
  node reaches 0015D4C0 case 0x32, which enters +5 = 0xB (00165B60,
  PLAYER_CLIMB_SLIDE.md section 7). 00165B60 hands on to +5 = 0xC. This is
  the path of the two cage climbs in route beat 10 (section 5).
- **From the hang.** 001647D0 sub-state 0x23 with +1F1 = 1 stores +5 = 0xC,
  +6 = 0, +1F0 = 0x17 and +2F1 = 0, then calls 0017FC80(p, 16.0)
  (PLAYER_HANG.md).
- **From the scripted takeover.** 00182DF0 enters +4 = 1, +5 = 0xC at
  00182F24 when +1F0 is 0x17 (docs/FIRST_CONTROL.md). 001838B0, the +4 = 4
  handler's routine for +5 = 0xC, calls 001662D0 itself. If +4 is still 1
  afterwards, it sets +4 = 4, +5 = 0 and +6 = 0 (its byte-matched C).

It is in the FLOOR state closure (docs/FIRST_CONTROL.md, "FLOOR state
closure": +4 1, +5 0xC, 001662D0).

### Sub-states (+6)

"React" means `00181110(p, 0)`: when +224 or +22C is nonzero, or +F bit 1 is
set, it stores +4 = 2, +5 = 4, +6 = 0 and +302 = its argument, and returns
1. The state then returns at once and skips the tail. "bone0" and "bone1"
mean `*(D_00275B40)` and `*(D_00275B40 + 4)`, the player's own bone records
(PLAYER_HANG.md). "The row clip" means 001749A0(p, 00188550(p), 0, 16.0).

| +6 | What it does |
|---|---|
| 0 | +6 = 1, +7 = 0 (before the call), 0017FC80(p, 16.0), +290..+298 = +B0..+B8. Falls into 1. |
| 1 | React. 00174FD0. Then by +24C. **0:** 00180460. Result 0 gives +6 += 1 (re-read). Result 2 with +D == 0 gives +6 = 0x14. **1:** 00180530. Result 0 gives +6 = 0xA, 1 gives 0x28, anything else 0x1E. **2 / 3:** 00180600(p, 0 / 1, 9.0, 10.0, -2.0). Result 0 gives +6 = 0x32 / 0x3C. Finally +21C = 0 and +38 = 0. |
| 2 / 0xA | React. +6 += 1 (before the call), then 0017FD00 / 0017FD40 (p, 4.0). |
| 3 / 0xB | React. When the blend is over (+200 & 0x8000 clear): +6 += 1. |
| 4 / 0xC | 00174FD0. **At the clip end (+200 & 0x1000):** +B4 = 3.0 + +294 (4) or +294 - 3.0 (0xC), stored before 00182A70. Then 001FB9F0(0x107, 0x1000, 0x1000, 0x1000) and +2F1 = 1 - +2F1. If +24C is 0 (4) or 1 (0xC), it probes again. **4:** 00180460. Result 0 gives 0017FD00(p, 1.0), then +21C = 0. Result 2 with +D == 0 gives +6 = 0x14. Anything else gives +6 = 1 and 0017FC80(p, 16.0). **0xC:** 00180530. Result 0 gives 0017FD40(p, 0.0), then +21C = 0. 1 gives +6 = 0x28, anything else 0x1E. If +24C is some other value: +6 = 1 and 0017FC80(p, 16.0). Every clip-end path ends with +290..+298 = +B0..+B8. **Before the end:** +38 = bone0+4 - +21C, +21C = bone0+4, +B4 += +38. When +24C matches, +204 = 2.0 / 1.5 / 1.0 for +23F 3 / 2 / other. **Last:** 00181110(p, 1). When it reacts, the state returns without the tail. |
| 0x14 | +6 = 0x15, +7 = 0, +1F0 = 0x18, +28 = 0, then 001749A0(p, 0xF0, 1, 4.0). |
| 0x15 | **At the clip end:** 001C68C0(p). Then +B0..+BC = the 16 bytes at bone1+C0 (00102948). +B4 = +B4 - 11.5, then +B4 += -0.4. 00175900(p, 1). A nonzero result calls 00182430(p, 2), then 00187EE0(p, p+B0, p+D0). Then 00174AB0 (001749A0(p, 0, 1, 0.0)) and 00174AC0(p, 0). With +23F >= 2: +6 += 1, then 0017C440(p, 1). Otherwise +25C = 0, then 0017C540. **While blending:** nothing. **Otherwise, by +7.** **0:** spad A0 = +B0..+B8 with y + 10.0, then 00180300(p, A0, 0). If that is 0 and 00199FA0(a, b) is nonzero: 3A20 = fabs(b.y - +B4) and then 3A20 - 16.8. +2E4 = 3A20 / (float)001C61D0(+40, (short)+20C), and +7 += 1. **1:** +3C <= 1.0 gives +7 = 2. Then +B4 += +2E4. Then, for every +7, one step of the +28 countdown: when +28 is k = 0, 1, 2 or 3 and +3C <= 78, 64, 48 or 30 (the k-th limit), +28 = k + 1 (before the call), then 00182A70. |
| 0x16 | 00174AC0(p, 0), 00178B90(p, 1). When the blend is over: 0017C540. |
| 0x1E | +6 = 0x1F, +1F0 = 0x18, +28 = 0, then 001749A0(p, 0xF1, 1, 8.0). |
| 0x1F | When the blend is over: +6 = 0x20. |
| 0x20 | **At the clip end:** 001C68C0(p). +B0 = bone1+C0, +B4 = bone1+C4 - 11.5, +B8 = bone1+C8, then +B4 += -0.2. 00175900(p, 1), whose result is not used. 00174AB0. Then +4 = 1, +5 = 0, +6 = 0, +1F0 = 0. **Then**, with +28 == 0 and +3C <= 22.0: +28 = 1. +290 = +B0. 001C68C0(p). The same bone1 placement with -0.4. 00175900(p, 1), and on a nonzero result 00182430(p, 2) and 00187EE0. Last, +B0..+B8 = +290..+298. |
| 0x28 | +6 = 0x29, +1F0 = 0x1A, then 001749A0(p, 0xFF, 1, 4.0). |
| 0x29 | When the blend is over: +6 = 0x2A (before the call), then 00182A70. |
| 0x2A | At the clip end: +6 = 0x2B. |
| 0x2B | **+224 != 0:** 001B61C0(0, 0xC0, 5, 1), 001FBD50(p, 0x152, 0, 300.0), 0021C350(p), then +24C = 1. **Else +22C != 0:** the same with 0x153 and 0021C270. **Else:** 00174FD0. **Then +24C 0:** +6 += 1, +28 = 0, 001749A0(p, 0xFE, 1, 1.0). **+24C 1:** +6 += 2. +B0 = bone1+C0, +B8 = bone1+C8, +B4 -= 7.2, then 001749A0(p, 0x80, 1, 0.0). Then +21C = +38 = +2E4 = 0 and +2F4 = +B4. |
| 0x2C | **At the clip end:** +6 = 1, +B4 = bone1+C4 - 10.5, +2F1 = 0. Then 001749A0(p, D_002754D0[0] = 0xE6, 0, 0.0) and 0017FC80(p, 16.0), then +1F0 = 0x17. **Then** +28 0 / 1 with +3C <= 20.0 / 2.0 gives 00182A70, then +28 += 1 (re-read). |
| 0x2D | **At the clip end:** +5 = 7, +6 = 0, +1F0 = 0xD. **Otherwise** root motion: +38 = bone0+8 - +21C, +21C = bone0+8 (before 00178B90(p, 1)), +2EC = bone0+4 - +2E4, +2E4 = bone0+4, +B4 += +2EC. |
| 0x32 / 0x3C | React. +6 += 1. 001FB9F0(0x10E / 0x10F, 0x1000, 0x1000, 0x1000). 0017FD80(p, 0 / 1, 4.0). |
| 0x33 / 0x3D | React. **At the clip end:** +6 += 1, then 0017FE00(p, side, 1.0). **Otherwise:** +38 = bone0+0 - +21C, +21C = bone0+0. spad A0 = (+38, 0, 0, 0) and B0 = A0 through the +D0 matrix (001026A0). +B0 += B0.x and +B8 += B0.z. |
| 0x34 / 0x3E | React. **Use press** (D_00810E74 & spad 0x70003B76): +1F1 = 001809B0(p, side). With 7: +6 = 0x50, +1F0 = 0x19, 0017FF00(p, side, 1.0), then D_008106F2 = 5 / 4. Any other nonzero: +6 = 0x46, +1F0 = 0x19, 0017FF00(p, side, 1.0). Zero: nothing. **No press:** 00174FD0. If +24C != 2 / 3: +6 += 1, then 0017FE80(p, side, 1.0). |
| 0x35 / 0x3F | React. At the clip end: +6 = 1, then 0017FC80(p, 16.0). |
| 0x46 | When +3C <= 6.0: +6 = 0x47. 001FBD50(p, 0x187, 0, 300.0). The targets +2F4 = +2E0, +2F8 = +2E8, +258 = +2E4, and +28 = 8. The steps +2E0/+2E8/+2E4 = (target - position) / 8. t = 001B1470(+218 - +C4), stored at 3A20. +26C = \|t\| / 8 (neg.s on t < 0). Then 001749A0(p, 0xE5 if +1F1 == 1 else 0x7A, 0, 8.0). |
| 0x47 | **+28 == 0:** +6 = 0x48. +B0/+B8/+B4/+C4 = +2F4/+2F8/+258/+218. Then 00182A70 when +1F1 == 1, else 001FBD50(p, 0xFF, 0, 300.0). **Otherwise:** step the position, +C4 = 001B12B0(+218, +C4, +26C), +28 -= 1. |
| 0x48 | At the clip end, by +1F1. **1:** 00182A70, +6 = 0, +1F0 = 0x17, +2F1 = 0, then 0017FC80(p, 16.0). **5:** +5 = 0x18, +6 = 0, +1F0 = 0x2C, +1F1 = 0, +D = 2. **3:** +5 = 9, +6 = 0, +1F0 = 0x10, +D = 1. **Other:** the same with +D = 0. The last three end with the row clip. |
| 0x50 | At the clip end: +6 = 0x51, then 001FBD50(p, 0x187, 0, 300.0). 3A20 = 12.0, then 001749A0(p, 0xBA, 0, 12.0). +28 = (short)float_to_int(3A20). +260/+264/+258 = (+290/+298/+294 - position) / 3A20. t = 001B1470(+218 - +C4), stored at 3A24. +26C = \|t\| / 3A20. |
| 0x51 | **+28 == 0:** +6 = 0x52, 001FBD50(p, 0x123, 0, 300.0), position = +290..+298 and +C4 = +218. **Otherwise:** step by +260/+264/+258, turn +C4 as in 0x47, +28 -= 1. |
| 0x52 | At the clip end: +5 = 0x10, +6 = 0, +1F0 = 0x21, +2F1 = 0. |
| other | Nothing. |

**The tail (00167C4C).** Every path except the React returns and the
00181110(p, 1) return of 4/0xC ends here. When D_00810700 == 2, it calls
00176DC0(p).

**Exits written by the state itself:**
- +4 1 / +5 0 (0x20);
- +5 7 (0x2D);
- +5 9 / 0x18 (0x48);
- +5 0x10 (0x52);
- +4 2 / +5 4 through 00181110.

Its workers can exit too: 0017C440 and 0017C540 return to +4 = 1. All of
these states are already in the FLOOR closure table.

### The helpers

- **0017FC80(p, blend).** It requests D_002754D0[+235 & 1] (0xE6 / 0x100)
  when +2F1 == 0, else D_002754D4[+235 & 1] (0xE7 / 0x101), with flags 0
  and the blend. 001885D0 / 001885F0 do the lookup.
- **0017FD00 / 0017FD40(p, blend).** They request 0xE8 / 0xE9 when +2F1 ==
  0, else 0xEA / 0xEB (flags 0, the caller's $f12).
- **0017FD80 / 0017FE00 / 0017FE80 / 0017FF00(p, side, blend).** The side
  (0 or not 0) picks a pair and +2F1 picks within it:
  - 0017FD80: 0xF2/0xF8 or 0xF5/0xFB;
  - 0017FE00: 0xF3/0xF9 or 0xF6/0xFC;
  - 0017FE80: 0xF4/0xFA or 0xF7/0xFD;
  - 0017FF00: 0xEC/0xEE or 0xED/0xEF.
- **00180420(p).** spad A0 = (0, 0, -3.0, 1.0), then the tail call
  001026A0(p+290, p+D0, A0).
- **00180460(p).** spad A0 = +B0..+B8 with y + 4.0, then
  001760C0(p, A0, 1, 18.0). A nonzero result returns 3. Otherwise it runs
  00180420, sets A0 = (+290, 18.0 + +B4, +298) and returns
  00180300(p, A0, +D). A0.w is the 1.0 that 00180420 left, and 00180300's
  001028B8 adds that lane too.
- **00180530(p).** 00180420. A0 = +290 with y - 3.0 and B0 = +290 with
  y - 4.5, then 0019AB20(p, B0, p+280, 6). A nonzero result returns 2.
  Otherwise it returns 00180300(p, A0, +D) != 0.
- **00180600(p, side, reach, height, depth).** A0 = (0, height, depth, 1)
  and B0 = (side == 0 ? -reach : reach, height, depth, 1). C0 and D0 are
  those through the +D0 matrix. It then calls 0019AFE0(p, C0, D0, 7). The
  routine has no return statement, and its $v0 is 0019AFE0's result, which
  both callers test.
- **001809B0(p, side).** Returns:
  1. **0** when 00180600(p, side, 11.5, 10.0, -1.5) is nonzero.
  2. **3** when the box sweep (±ydist, 16 or 19, -6 / 4) hits kind 0x3D
     (`*(0x700031D0) + 0x1A`) and 00178390 agrees. ydist is 12 or 10 for
     +D == 2 (by +B4 < 560), else 9. The 16 / 19 is for +5 == 0xC / other.
  3. **7** when the lunge (A0 = (2·±ydist, 12, -2, 1) through the matrix,
     C0 = that + 10 in y, 0019A570(B0, C0, 4, 0)) hits kind 0x34. It first
     saves +B0/+B8, then calls 00177030(p, 2) and 00199DB0(A0). It then
     computes d = sqrt(dx² + dz²) (mula/madd; 3A20/3A28/3A2C) and
     n = float_to_int(d / 4.5) - 1 (side == 1) or + 1 (any other side).
     With step = 4.5 · (0.5 + n), it sets +290 = A0.x ± step·sin(+C4) and
     +298 = A0.z ± step·cos(+C4). Last it restores +B0/+B8 and sets
     +294 = spad 0x700031B4 - 20.5.
  4. Otherwise a third sweep (±ydist, 9, -5 / 5):
     - no hit gives 0;
     - kind 0x32 / 0x3B with 001782A0 gives 1 and +D = 0 / 1;
     - 0x3D gives 0;
     - any other kind needs 00178080. In area (8, 3) (D_00810700 /
       D_00810701) it gives 5, with +2E8 = 156.4, when 120 < +2E0 < 130 and
       250 < +2E4 < 260; otherwise 2. In other areas it gives 2 when
       0017E250(p, (+2E0, +2E4, +2E8, 1)) is 0, else 0.

## 2. Reading: instructions, not the NEARMISS C

`src/func_001662D0.c` (99.83 %) and `src/func_001809B0.c` (83.35 %) are
NEARMISS. The translation follows `build/asm/matchings/main/code/func_*.s`,
read branch by branch. Every other routine in the table is byte-matched C,
which was checked against its instructions. The instructions settle these
points, where the readable C is wrong or silent:

- **001C68C0's argument is the actor.** The NEARMISS C passes `flg`
  (sub-state 0x15) and `0x20` (sub-state 0x20). $a0 is never changed from
  the entry value, so the call is 001C68C0(p) (00166A08, 00166DB8).
- **001809B0's first call is 00180600(p, side, 11.5, 10.0, -1.5).** The
  NEARMISS C drops p and side. $a0/$a1 are the incoming arguments at
  001809F0.
- **00180600's result.** Its C is `void`, but its $v0 after the final
  `jal 0019AFE0` is untouched, and both callers test it.
- **001809B0's window.** It is c.le.s / c.lt.s. +2E0 <= 120, +2E0 >= 130,
  +2E4 <= 250 and +2E4 >= 260 each give 2 (00181028..00181088). On the
  0017E250 path the result is 0 when 0017E250 returns nonzero, and 2 when
  it returns 0.
- **001809B0's dash sign.** It tests `side == 1` (00180D2C). The box signs
  test `side != 0`.
- **00187EE0 gets p, p+B0, p+D0.** Its C shows one parameter, but it passes
  $a1 on to 001031E0.
- **Delay-slot stores come before the call:**
  - +7 = 0 before 0017FC80 (00166490);
  - +6 before 0017FD00 / 0017FD40 / 0017FE00 / 0017FE80 and 00182A70 (0x29);
  - +B4 before 00182A70 (4 / 0xC);
  - +B4 before 00175900 (0x15 / 0x20);
  - +25C before 0017C540 and +6 before 0017C440 (0x15);
  - +28 before 001749A0 (0x14, 0x1E, 0x2B) and before 001031E0 (0x20);
  - +2F1 = 0 before 0017FC80 (0x48);
  - +21C before 00178B90 (0x2D);
  - spad 3A20 = 12.0 before 001749A0 (0x50).

  +21C = 0 after 0017FD00 / 0017FD40 (4 / 0xC) is in the delay slot of a
  `b`, so it runs after the call. D_008106F2 after 0017FF00 and +24C = 1
  after 0021C350 / 0021C270 are placed the same way.
- **Re-reads after calls:**
  - +6 after 00181110, 00180460, 00174AC0 and 00174FD0;
  - +28 after 00182A70 (0x2C) and after the 0x20 placement;
  - +7 after 001C61D0;
  - +B4 / +B0 / +B8 after every worker;
  - spad 3A20 after 001749A0 and float_to_int (0x50);
  - A0 after 00199DB0 and the sine call (001809B0);
  - the hit kind after each sweep;
  - +C4 before each of sin and cos.

## 3. Translation

`em_player_ladder_climb.c` works on the live actor (EmPlayerLiveActor, the
raw 0x320-byte record) by original offsets. Every line cites the
instruction it translates.

- **Arithmetic.** EE COP1 arithmetic and compares use the `_bits` forms of
  `em_ee_float.h`, in the original's operand order. cvt.s.w uses
  `em_ee_cvt_s_w_bits`. fabs (0011DF78) clears bit 31. Moves copy bits.
- **Scratchpad.** `EmPlayerLadderClimbScene.spad38A0` / `spad3A20` hold spad
  0x700038A0..DF and 0x70003A20..2F. The routines store to them exactly as
  the original stores the scratchpad. Vector workers get pointers into them
  where the original passes a scratchpad address. The actor's +D0 matrix,
  the +290 output of 00180420 and the 001809B0 / 00199FA0 stack vectors are
  passed as copies. The +290 result is stored back right after the call.
- **Tables.** D_002754D0 / D_002754D4 (two halfwords each) are embedded. The
  oracle checks the captured RAM's copy against the ELF, and cases read
  every entry.
- **Reuse.** 00181110 is `em_player_major2_00181110` (em_player_major2.c,
  verified by its own oracle and executed as original code here).
- **Exports:**
  - `em_player_ladder_climb_state(void *context, EmPlayerLiveActor *)`:
    001662D0. Its context is an `EmPlayerLadderClimb` { workers, scene },
    with workers an `EmPlayerLadderClimbWorkers` and scene an
    `EmPlayerLadderClimbScene`. Every type carries the `EmPlayerLadderClimb`
    prefix so the header can be included next to em_player_ladder_entry.h
    (state 0xB), whose `EmPlayerLadderWorkers` is a different struct.
  - `em_player_ladder_climb_0017FC80` / `_0017FD00` / `_0017FD40` /
    `_0017FD80` / `_0017FE00` / `_0017FE80` / `_0017FF00` / `_00174AB0` /
    `_00180420` / `_00180460` / `_00180530` / `_00180600` / `_001809B0`:
    the helpers, for their other callers (0017FC80 is also called by
    001647D0, 0016D130, 00169730, 00165B60 and 00221FC0; 001809B0 by
    00168050; 00180420 by 00180790, 001806E0 and 00180850; 00174AB0 by
    00168050 and 0016D130).
- **One owner (2026-09-24).** 0017FC80 (with 001885D0 / 001885F0), 00180420
  and 00174AB0 are translated only here. The ladder entry
  (em_player_ladder_entry.c) runs `_0017FC80` over its own `request`, and
  the closure states (em_player_closure_0e_18.c) run `_00180420` and
  `_00174AB0` over their own `transform` / `request` and scratch, through
  a one-routine `EmPlayerLadderClimb` (PLAYER_CLOSURE_0E_18.md 5.4). Their
  oracles execute the originals, so they check these helpers once more
  from those callers. 00180300 is the ladder entry's
  (`em_player_ladder_probe_00180300`).
- **Fail-stop:**
  - If any worker or the scene is NULL, the state returns -1 and writes
    nothing. Each helper checks only the workers it can reach.
  - If a worker returns a negative value, the routine returns -1 at once.
    Earlier writes stay, as the original order leaves them.

## 4. Binding (coordinator)

**Bound live in AREA11 since the Boxes step (2026-09-24):** `em_player_closure_live.c` binds this module over the live player record (FIRST_CONTROL.md "Engaged"). Workers with no translation are fail-stop workers that name their original. The notes below are the binding it follows.

- **Stage slot.** `EmPlayerStatesBinding.stage.state[0xC] =
  em_player_ladder_climb_state` and `stage.state_context[0xC] = &ladder`
  (an `EmPlayerLadderClimb`). 0015B130 dispatches +4 = 1, +5 = 0xC there.
  0015B530's `routine[EM_PLAYER_MAJOR4_001838B0]` (001838B0, not translated
  here) runs the same callback first.
- **Scene.** The binder owns one `EmPlayerLadderClimbScene` and keeps it current:
  - `area` / `area_sub` = D_00810700 / D_00810701;
  - `pad` = D_00810E74[0], the pad edge halfword;
  - `use_mask` = spad 0x70003B76 (0x40 in the captured config).

  `d8106F2` is D_008106F2[0], which the state writes: mirror it back to the
  global's owner. The spad arrays are the scratchpad words. Any worker that
  reads or writes spad 0x700038A0..DF or 0x70003A20..2F in the original
  (00174FD0 writes 0x70003A20, 00180420-style callees write A0) must use
  these arrays.
- **Bones.** `node(ctx, n, off, &bits)` returns the word at
  `*(player +0x40 + 4n) + off`: bone 0 (+0/+4/+8) or bone 1 (+C0..+CC). It
  is called at the original's read time, so a 001C68C0 worker that
  re-evaluates the skeleton must update what `node` returns.
- **Hit record.** `hit_kind` / `hit_y` return the byte at
  `*(0x700031D0) + 0x1A` and the word at 0x700031B4, as the last sweep
  (0019AFE0 / 0019A570, or a callee that sweeps) left them. Bind them over
  the same context as the sweep workers.
- **Workers.** A candidate counts only once it passes its own oracle and a
  live-actor adapter exists. Several candidates below are concurrent lane
  work that is not yet committed (marked *lane*); they are pointers subject
  to their own review, not evidence for this module. "Direct" means the
  function already has this slot's exact C signature.

  | Field | Original | Native candidate / note |
  |---|---|---|
  | node | data read | `em_pose_view_node_bits` (em_pose_host_workers.h, *lane*; direct, context = an `EmPoseActorView`) |
  | request | 001749A0 | `em_pose_host_request` (em_pose_host_workers.h, *lane*; direct) |
  | sound | 001FBD50(p, id, 0, 300.0) | `em_player_misc_w_sound` (em_player_misc_workers.h, *lane*; direct) over `em_player_misc_001FBD50` |
  | sfx | 001FB9F0(id, 0x1000, 0x1000, 0x1000) | `em_sfx_play(id)`, the live binding of 001FB9F0 (em_player_closure_live.c x_sound_1FB9F0; other request words fault; 0x107 / 0x10E / 0x10F are not in the exported registry, WP-14) |
  | cue | 001B61C0(0, 0xC0, 5, 1) | `em_player_rumble_worker` (em_player_ladder_entry.h, *lane*; direct, context = an `EmPlayerRumble`) over `em_player_rumble_001B61C0` |
  | sound_109 | 00182A70 | `em_player_ladder_00182A70` (em_player_ladder_entry.h, *lane*; adapter over that lane's `EmPlayerLadderWorkers`) |
  | steer_input | 00174FD0 | `em_player_slide_steer_input` (slide mirror; needs a live adapter) |
  | heading | 00174AC0 | `em_player_heading_record_worker` (em_player_heading_record.h; direct, context an `EmPlayerHeadingRecord`); its `world.spad3A20` must be the one shared 0x70003A20 word this scene's `spad3A20[0]` stands for |
  | skeleton | 001C68C0 | `em_pose_host_skeleton` (em_pose_host_workers.h, *lane*; direct) over `em_pose_host_001C68C0` |
  | floor | 00175900 | `player_states_floor_service` |
  | footstep | 00182430(p, 2) | `em_player_step_sounds` (em_player_floor.h), bound live by em_player_closure_live.c x_surface_sound since census L03 |
  | ground_effect | 00187EE0(p, p+B0, p+D0) | `em_player_ground_effect_00187EE0` (em_player_floor.h, the footstep's one translation) over the record, foot = +B0 (x_place); its 001EFD90 spawns run the live effect binder (em_effects_live, L26) |
  | translate | 00178B90 | em_player_recovery.h |
  | reentry | 0017C440 | untranslated (the motor module) |
  | handoff | 0017C540 | `em_pose_host_handoff` (em_pose_host_workers.h, *lane*; direct), or `em_player_reaction_0017C540` through an adapter |
  | w0021C270 / w0021C350 | 0021C270 / 0021C350 | `em_player_0021C270` / `em_player_0021C350` (em_player_stage_workers.h; host adapter) |
  | camera | 00176DC0 | `em_player_ladder_00176DC0` (em_player_ladder_entry.h, *lane*; adapter) |
  | clip_row | 00188550 | D_002754C0[+235 & 1] = 0x7B / 0x8E (the climb module has it internally) |
  | clip_frames | 001C61D0 | `em_pose_host_clip_frames` (em_pose_host_workers.h, *lane*; direct) over `em_pose_host_001C61D0` |
  | probe | 00180300(p, A0, kind) | `em_player_ladder_probe_00180300` (em_player_ladder_entry.h), the one translation; an adapter hands it the A0 words as bits and the ladder entry's workers over this context's sweep |
  | column | 001760C0(p, A0, 1, 18.0) | the floor module's column probe |
  | wall | 0019AB20(p, B0, p+280, 6) | `em_actor_collision_ground_0019AB20` (adapter) |
  | sweep | 0019AFE0(p, C0, D0, 7) | `em_coll_move_sweep_0019AFE0` (em_coll_move_original.h; adapter over its world / scratch / actor) |
  | sweep_box | 0019A570(B0, C0, 4, 0) | `em_coll_segment_0019A570` (em_coll_segment_walkers.h, *lane*; adapter) |
  | hit_probe | 00199FA0(a, b) | `em_player_ladder_00199FA0` (em_player_ladder_entry.h, *lane*; adapter). It must write b[1] whenever it returns nonzero. |
  | hit_point | 00199DB0(A0) | `em_player_ladder_00199DB0` (em_player_ladder_entry.h, *lane*; adapter) |
  | transform | 001026A0 | `em_player_sdk_apply`, with an adapter |
  | ledge_ahead | 0017E250(p, v) | `em_player_misc_w_ledge_ahead` (em_player_misc_workers.h, *lane*; direct) over `em_player_misc_0017E250` |
  | dash | 00177030(p, arg) | `em_player_ladder_00177030` (em_player_ladder_entry.h, *lane*; adapter that drops its result, which 001662D0 does not read) |
  | grab_check / grab / hit_react | 00178390 / 001782A0 / 00178080 | untranslated |
  | sqrt / sine / cosine | 0011E748 / 0011E2A8 / 0011DE90 | `em_sdk_math_original_float_*` |
  | wrap | 001B1470 | `em_player_sdk_wrap` or `em_player_001B1470` |
  | approach | 001B12B0 | `em_player_slide_approach` |
  | to_int | 001281C0 | `em_player_float_to_int` (em_player_stage_workers.h) |

  The hit_kind / hit_y data reads have no candidate yet; they belong to
  whichever context owns the sweep workers' scratch.
- **Clips.** Before the state can be live, the display must export and draw
  these clips:
  - 0xE6/0xE7/0x100/0x101 and 0xE8..0xEF;
  - 0xF0..0xFF, 0x80, 0xBA, 0xE5 and 0x7A;
  - the row clips 0x7B/0x8E.
- **Also usable by others.** `em_player_ladder_climb_0017FC80` fits the
  hang's `clip_FC80` and major2's `w0017FC80` through a one-line adapter.
- **Makefile.** When bound, add `src/game/em_player_ladder_climb.c` (and
  `src/game/em_player_major2.c`, if not already there) to COMMON. The test
  target is `test-player-ladder-climb-reference`.

## 5. Verification

`python3 tools/test_player_ladder_climb_reference.py` (make
`test-player-ladder-climb-reference`).

- **Setup.** The original 001662D0 and every routine it reaches that is
  translated here run from the captured AREA11 RAM (`playable_ee.bin`). That
  covers 0017FC80, the clip pickers, 00180420/460/530/600, 001809B0,
  001885D0/F0, 00174AB0, 001031E0, 00102948, 0011DF78 and 00181110: 20
  routines. The test first asserts that the executed code and D_002754D0/D4
  in that RAM equal the pinned ELF. The player record is at its captured
  address 0x8102B0, with its captured bone pointers. The captured record
  seeds 30 % of the cases.
- **Interpreter.** The shared EE is subclassed (LadderEE). Every COP1 op
  goes through `tools/ee_float_model.py`, and any VU0 op raises.
- **Hooks.** Every other callee is hooked and scripted. The same return
  value is applied on both sides, along with the same actor, bone, spad and
  hit-record writes. Every non-float callee may write the spad words, so a
  cached spad read fails.
- **Checks:**
  - All 0x320 actor bytes, spad 0x700038A0..DF and 0x70003A20..2F, and
    D_008106F2 after the call.
  - The same state at every callee entry, which checks every store-before-
    call and every re-read.
  - The callee sequence with every argument: floats as bits, and vectors
    as the words the callee receives and whether they live in the
    scratchpad (with the offset) or in a copy.
  - Coverage: every conditional branch of the 20 executed routines seen
    taken and not taken (378/378). All 34 handled sub-states run, all four
    table entries are read, and injected worker faults stop at exactly the
    original's state.
  - 41 missing-worker refusals, plus a missing scene and a NULL context.
- **Helper cases.** Each exported helper runs alone against the original
  with sides 0/1/2/-1 and special float arguments (00180600), and the
  return value is compared. 001809B0 covers every result (0, 1, 2, 3, 5,
  7). One case in three of 001809B0 is driven into the area (8, 3) window,
  at its boundaries.
- **Runs.** The default run is 14,000 state cases and 5,000 helper cases:
  8–11 s parallel on a heavily loaded machine (about 16 s of CPU in total).
  `EM_TEST_FULL=1` runs 400,000 and 100,000 cases: PASS, 262 s wall on the
  same loaded machine, 1,316 injected faults and 378/378 branch outcomes.
- **Mutants killed.** A mutation run over the native file is listed in
  section 7.
- **Route (beat 10).** The route captures reach this code only in beat 10
  (`10_cage_roof_roger`): the two cage climbs, +1F0 0x17 / 0x18 with clips
  0xE6, 0xE8/0xEA and 0xF0. No other beat has a frame with +5 = 0xC. The
  test replays each climb through the native state:
  - sub-state 2 once;
  - sub-state 4 at every captured clip change, with 00180300 returning 0
    while the trace keeps climbing and 2 on the last cycle, and +D == 0;
  - sub-state 0x14 once.

  It asserts that the requested clips and blends equal the captured +20C
  and +3C on the first row of each new clip, and that +1F0 is 0x17 during
  the cycles and 0x18 at 0xF0:
  - (0xE8, 4.0), then (0xEA, 1.0) and (0xE8, 1.0) alternating, then
    (0xF0, 4.0);
  - both climbs, 7 cycle clips each.

  This shows that the +2F1 alternation, the blends of sub-states 2, 4 and
  0x14, and the +1F0 at the top match the original run. It does not show:
  - the probe results (taken from the trace);
  - the in-cycle root motion;
  - +B4 (the captured +B0 is also moved by per-frame code outside this
    state; the row at the clip change is not the state's own store).

## 6. Limits

- **Scripted hooks.** Behaviour of the hooked callees is not claimed. Those
  callees need their own translations and oracles (section 4).
- **Float workers.** They cannot fault. They follow the house signature of
  the other state modules.
- **No live evidence.** No whole-world run or PCSX2 capture exercises
  sub-states 0xA..0xC, 0x1E..0x2D, 0x32..0x3F or 0x46..0x52. The route only
  climbs up and dismounts at the top. Their evidence is the instruction
  oracle.
- **Hidden spad users.** A worker bound here that touches spad outside the
  two published blocks is the binder's concern. The oracle covers only the
  two blocks.

## 7. Mutation run

A mutation run over `em_player_ladder_climb.c` (one change at a time, the
default test run each time) killed all 24 mutants:
- +2F1 toggled after the 4 / 0xC probe;
- 0x15's `+23F >= 2` as `> 2`;
- the 0x15 countdown limits 78 / 64 swapped;
- 0x2B's `+6 += 2` as `+= 1`;
- the D_008106F2 values 5 / 4 swapped;
- 001809B0's `+B4 < 560` as `<=`;
- 001809B0's dash `side == 1` as `side != 0`;
- 00180600's `side == 0` negation as `side != 1`;
- 0x50 dividing by 12.0 instead of re-reading spad 3A20;
- 00199FA0's a.y read instead of b.y;
- 0017FC80's row bit `+235 & 1` as bit 1;
- 0x2C's +1F0 stored before its first 001749A0;
- the +204 rates of +23F 3 / 2 swapped;
- 0x33's +B8 step from B0.y instead of B0.z;
- 4's 00181110(p, 1) return not skipping the tail;
- 0x20's first lift -0.2 as -0.4;
- 0x15's fabs dropped;
- 001809B0's `+2E0 < 130` as `<=`;
- 0x2D's +21C stored after 00178B90;
- sub-state 1's `+6 += 1` as a constant 2;
- 00180530's result not normalized to 0 / 1;
- 001809B0's hit kind not re-read after 0019A570;
- 0x47's 001B12B0 arguments swapped;
- 001809B0's +B0 / +B8 saved after 00177030.
