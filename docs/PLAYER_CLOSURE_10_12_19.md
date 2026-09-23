# Player +4 = 1 states 0x10, 0x12, 0x19 and 0x1A of the FLOOR closure

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "player-closure-10-12-19", 2026-09-23. This document covers four
0015B130 state routines that the FLOOR state closure lists
(FIRST_CONTROL.md, "FLOOR state closure"), and their callees that no other
routine calls. It records what each original does, the translation, the
binding the coordinator makes, the evidence and the limits. Nothing here is
wired into the live player yet.

Code: `src/game/em_player_closure_10_12_19.c/.h`. Oracle:
`tools/test_player_closure_10_12_19_reference.py` (`make
test-player-closure-10-12-19-reference` once the lead adds the target,
section 4).

The decomp's NEARMISS comments call 00169730 and 0016AE40 a
"boss/large-enemy behaviour state machine". That label is wrong. Both are
entries of 0015B130's per-state table (+5 0x10 and 0x12), and they run on
the player record.

## 1. What the originals do

All four switch on the sub-state byte +6. Several sub-states also switch on
the phase byte +7. "The guard" below is 00181D70 (for 0x10 / 0x12) or
001823E0 (for 0x19): when it returns nonzero it has already entered +4 = 2
(PLAYER_MAJOR2.md), and the routine returns. "Use" means
`D_00810E74 & 0x70003B76`, the use button pressed this frame.

### 00169730: +5 0x10, the ledge hang and shimmy (entered by 001662D0 at 00167C38)

- **0**: +6 = 1, +7 = 0, +1F0 = 0x21, +1F1 = +25C = +2F1 = 0, clip
  001885B0(p) at blend 16.0, then +290..+298 = +B0..+B8 (001031E0).
- **1**: the guard; 001696A0 (below); use → +6 = 0xA, +1F0 = 0x23.
  Otherwise 001751A0 (the stick quadrant into +24C), then by +24C:
  - 0: 001814E0(p, 0) → +6 = 0x14, +7 = 0, +1F1 = 1. Otherwise 001818D0
    (a wall 20 ahead) keeps the state; with no wall → +6 = 0x50, +7 = 0,
    +1F0 = 0x28;
  - 1: +6 = 0x1E, +1F0 = 0x24;
  - 2: +38 = -0.08, then 00181730(p, 0) → +6 = 0x28, else 0x3C (+7 = 0);
  - 3: +38 = 0.08, then 00181730(p, 1) → +6 = 0x32, else 0x46.
- **0xA / 0xB** (the drop): clip 0xD6, +2F4 = +B4, +2EC = 0. Then each
  frame: on +200 & 0x1000, +5 = 7, +6 = 0, +1F0 = 0xD. 00179880, then
  00175900(p, 1).
- **0x14** (the climb up), after the guard, by +7:
  - 0: +2F1 = 1, clip 0xBC at 8.0, +25C = 2, +26C = 1.0, +38 = +21C = 0,
    then 00181B80 (+2F4/+2F8 = +B0/+B8);
  - 1: +7 + 1 once +200 & 0x8000 clears;
  - 2: on 0x1000 the step lands:
    - sound 0x124 + 00179B90();
    - 00181BA0 (the step offsets, below), 00181B80, build_trs_matrix;
    - 001696A0, then 001751A0;
    - with +24C == 0, 001814E0(p, 1) returning 1 or 2 starts the next
      step. For 2 with +23F == 3, +23F = 2. Then 001811F0 and
      +25C = +23F, +38 = +21C = 0;
    - anything else is +7 + 1.

    Without 0x1000 it runs the root motion: +204 = +26C,
    +38 = node0[8] - +21C, +21C = node0[8], then 00178B90(p, 0);
  - 3: 00181430, +38 = +21C = 0;
  - 4: on 0x1000, the sound, 00181BA0 and +6 = 0. Otherwise the root
    motion, without +204.
- **0x1E / 0x1F** (the turn back): clip 0xD7. On 0x1000:
  +C4 = wrap(pi + +C4), clip 001885B0(p) at 0.0, +6 = 0.
- **0x28 / 0x32** (the shimmy, sides 0 / 1), after the guard:
  - phase 0: clip 0xCA / 0xCB;
  - phase 1: 001751A0. While +24C is still 2 (side 0) or 3 (side 1),
    00181730 confirms the ledge. Then +B0 += +38 · cos(+C4) and
    +B8 -= +38 · sin(+C4). If it does not, +6 = 0x3C / 0x46, +7 = 0. When
    +24C changes, +7 + 1 and clip 001885B0(p) at 8.0;
  - phase 2: +6 = 0 once 0x8000 clears.
- **0x3C / 0x46** (the ledge end, sides 0 / 1), after the guard:
  - clips 0xCC / 0xCD at 4.0, then on 0x1000 0xCE / 0xCF;
  - phase 2 with use: 001787B0(p, side) finds a 0x32 ledge. Then +6 = 0x5A,
    +1F0 = 0x12 and clip 0xD0 / 0xD1;
  - without use: 001751A0. When +24C leaves 2 / 3, clip 0xD2 / 0xD3 and
    +7 + 1;
  - phase 3: +6 = 0 on 0x1000.
- **0x50**: 0016A4B0 (the push, below).
- **0x5A..0x5D** (the climb over):
  - 0x5A: on 0x1000, +6 + 1;
  - 0x5B: 0x70003A20 = 12.0, +6 + 1, clip 0xE5 at 0x70003A20, sound 0x187.
    Then +2F4/+2F8/+258 = +2E0/+2E8/+2E4 (the target), and
    +28 = float_to_int(0x70003A20). The velocities are
    +2E0/+2E8/+2E4 = (target − position) / 0x70003A20 for x/z/y.
    0x70003A24 = wrap(+218 − +C4), and +26C = |0x70003A24| / 8.
    0x70003A20 is reloaded after every call, as the original does;
  - 0x5C: while +28 != 0, the position moves by the velocities,
    +C4 = 001B12B0(+218, +C4, +26C) and +28 − 1. At 0: +6 + 1, 00182A70,
    the position is the target and +C4 = +218;
  - 0x5D: on 0x1000, 00182A70. Then +5 = 0xC, +6 = 0, +1F0 = 0x17,
    +D = +2F1 = 0 and 0017FC80(p, 16.0).

### 0016AE40: +5 0x12, the ladder / pole hang (entered by 002230A0)

- **0**: as 00169730 0 with +1F0 = 0x22, and the quadword copy
  +290..+29C = +B0..+BC (00102948).
- **1**: the guard; 0016ADE0; use → +6 = 0xA, +1F0 = 0x23. Then 00175390
  (the pad stick, below). With a stick:
  - 00181E20(p, 0, 4.5) (a 0x1E node 5.5 ahead along +218) → +6 = 0x14,
    +7 = 0, +1F1 = 1;
  - otherwise 00181F60 (a 0x36 edge 4 ahead) → +6 = 0x1E, +7 = 0.
- **0xA / 0xB**: as 00169730.
- **0x14** (climbing), after the guard, calls 00175390, then by +7:
  - 0: 0x70003A20 = wrap(0x700031E4 − +C4). Its sign picks +2F1 = 1 with
    clip 0xBC, or +2F1 = 2 with 0xC3 (8.0). Then +26C = 1.0, +25C = 2 and
    +38 = +21C = 0;
  - 1: +7 + 1 once 0x8000 clears;
  - 2: 00182090 (turn +C4 toward +218), then build_trs_matrix. On 0x1000:
    - sound 0x112 + 00179B90();
    - 0016ADE0 returns;
    - with +23F != 0, 00181E20(p, 1, 2.25 + 00182100()) starts the next
      step: 001811F0, +25C = +23F, +38 = +21C = 0;
    - otherwise +7 + 1.

    Without 0x1000: +204 = +26C and the root motion. Then
    00181E20(p, 1, +38 + 00182100()) decides whether 00178B90(p, 0) runs;
  - 3: 00181430, +38 = +21C = 0;
  - 4: on 0x1000, the sound and +6 = 0. Otherwise the root motion and
    00181E20.

  After the phase (unless 0016ADE0 returned), 00181950 (the forward
  probes) and +290..+29C = +B0..+BC.
- **0x1E** (the turn onto the pole):
  - +C4 = 001B12B0(+218, +C4, 0.06981317) first;
  - phase 0: +2F1 = 1, clip 0xBC at 8.0, +26C = 1.0, +25C = 2, +38 = +21C = 0;
  - phase 1: the 0x8000 wait;
  - phase 2: on 0x1000, the sound, +7 + 1, 00181430 and +38 = +21C = 0.
    Otherwise +204 = +26C, the root motion and 00178B90;
  - phase 3: on 0x1000, the sound, +6 = 0x28, +7 = 0 and +B0/+B8 =
    +290/+298. Otherwise the root motion and 00178B90.
- **0x28**: the guard, 001751A0. +24C 0 → +6 + 1, +7 = 0, +1F0 = 0x28;
  +24C 1 → +6 + 2, +7 = 0.
- **0x29**: 0016A4B0. **0x2A / 0x2B**: clip 0xD7. On 0x1000:
  +C4 = wrap(pi + +C4), clip 001885B0(p) at 0.0, +6 = 0x32, +7 = 0.
- **0x32**: as 0x1E without the turn, with blend 0.0. Phase 3 on 0x1000:
  the sound, +6 = 0, +1F0 = 0x22.

### 0016DE40: +5 0x19, the crawl space (entered by 0016D130 at 0016D544)

It starts with +1 = 0. (0015B130 calls it only outside the 0x70003B8D
takeover; its prelude sets 0x70003B8F = 1 for +5 0x19.)

- **0**: D_008106BE = 2, +302 = 0.
  - In area 0 with !(+B8 <= -1470): +D = 2, +5 = 0x1A, +6 = 0,
    +1F0 = 0x2E, +B0 = 185.8, +B8 = -1450, +B4 += -0.2,
    D_00810702 = 5, then 001B0460(1).
  - Otherwise +6 + 1, +7 = 0, +38 = 0, +1F1 = 0 and clip 00188610(p) at
    0.0. Then +B4 -= 1.0 until 00179010 (below) finds the ground.
- **1**: +6 = 0xA once D_0028A9A0 == 0.
- **0xA**: the 001823E0 guard. With use, 00184BA0 returning nonzero also
  stops the frame. Then clip 00188610(p) at 1.0 and 00174FD0 (the steer
  input). By +24C: 0 → +6 = 0x14; 1 → 0x1E; 2 or 3 → 00176F90 returning
  0x1F takes 00199DB0 into +290 and +B0/+B8 = +290/+298, then +6 = 0x28.
- **0x14**: the guard. +6 + 1, +2F4 = 0.5 / 0.7 / 1.0 for +23F 1 / 2 /
  else, clip 0x14B, 001FB9F0(0x13F + 00179B90(), 0x1000, 0x1000, 0x1000),
  then +38 = +21C = 0.
- **0x15** (the crawl step), after the guard:
  - on 0x1000, +6 = 0xA. Otherwise +204 = +2F4, the root motion on
    node0[0], 00179150, +B4 += -0.2, 00179010, +302 = 0;
  - with no ground (+A == 0):
    - in area 3, three boxes set D_00810702 to 7, 4 or 2; outside all
      three it is 0. Each also sets +302 = 1;
    - in area 8 sub 3, one box snaps +B0/+B8 to (123.5, 156.4) with
      D_00810702 = 0; another sets 2 and +302 = 1;
    - in area 0x13 sub 0, one box sets 8 and +302 = 1.
    Then 00179450 (the floor query). When it answers, +258 decides:
    below -24.0 is +D = 2; above -4.01 is +D = 0, and the drop to the
    ground; otherwise +D = 1. Then +6 + 1;
  - with ground whose attribute +23B is 0x37: +D = 0, +6 + 1. Then area
    0x13 has a box for D_00810702 = 9, area 3 sets 0, and area 0 sets 8
    (+B4 <= -85) or 9 (+B4 <= -65), each with +302 = 1;
  - finally 00179910 (the area-2 exit, below) → +6 = 0x63 and
    001AEDE0(4, 0).
- **0x16**: +6 + 1, 001AEDE0(4, 0). **0x17**: once D_0028A9A0 == 2:
  +5 = 0x1A, +6 = 0, +1F0 = 0x2E, 001AEE10(4, 0), and 001B0460(1) when
  +302 != 0.
- **0x1E / 0x1F**, **0x28 / 0x29** (the turns): the guard. +26C =
  wrap(pi + +C4), or wrap(+C4 ∓ pi/2) by +24C == 2. Then +C4 steps toward
  +26C by 0.06981317 (001B12B0); on arrival +6 = 0x32. 0x28 falls through
  into 0x29, and the guard runs again.
- **0x32 / 0x33**: +28 = 0x10 and fall through. Each frame, behind the
  guard, +28 − 1; when the value before the decrement was 0, +6 = 0xA
  (17 frames from 0x10).
- **0x63**: nothing (the fade runs).

### 0016EBA0: +5 0x1A, the exit from the crawl space (entered by 0016D130 at 0016D6D0 and by 0016DE40)

- **0**: 001B0B50 (below), +38 = 0. Then by +D:
  - 0: +6 = 0xA, clip 0x153, anim_eval_skeleton. Then +B0..+BC =
    (0, 0, 5, 1) through the matrix at +D0 (001026A0).
  - 1: the same point without the skeleton, build_trs_matrix, clip 0x72,
    +6 = 0x14, +2EC = 0, +28 = 8.
  - else: the point (0, -3, -5, 1) through +D0 goes to 0019AD00(p, point, 7).
    On a hit, +B0/+B8 = hit point + 1.5 · the node normal x/z, and
    +B4 -= 20.5. 0x70003A20 = atan2(-normal.z, normal.x), and
    +C4 = wrap(4.712389 + 0x70003A20). Then +6 = 0x1E and clip 00188550(p)
    at 0.0.
- **0xA and 0x16**: once D_0028A9A0 == 0 and +200 & 0x1000:
  +5 = +6 = +1F0 = 0 (back to idle).
- **0x14**: the wait set by +D 1: +28 − 1 each frame; when the value
  before the decrement was 0, +6 + 1.
- **0x15**: 00179880, then 00175900(p, 1). On a floor: +6 + 1, 00182870(p, 1)
  and clip 0x6D at 8.0.
- **0x1E**: once D_0028A9A0 == 0: +5 = 0x18, +6 = 0, +1F0 = 0x2C,
  +1F1 = 0, +D = 2.

### The callees translated here

- **001696A0 / 0016ADE0**: with `D_00810E70 & (0x70003B7C | 0x70003B7E)`
  (tested one mask after the other), +5 = 0x13, +6 = 0, +1F0 = 0x25 and
  D_00275B14 = 0x34 / 0x1E. 001696A0 also saves +2E0 → D_00275B10,
  +2E8 → D_00275B00[3] and +C4 → D_00281B64[0], and restores +B0/+B8 from
  +290/+298. Both return 1, else 0.
- **0016A4B0** (the push, by +7):
  - 0: clip 0xD4 at 4.0, +290.. = +B0.. (quadword), +294 += 21;
  - 1: on 0x1000, clip 001885B0(p) at 8.0, then +2E = 0, +38 = 0,
    +28 = 0, +25C = 0;
  - 2: 001751A0, then 0016A8B0. Use → +7 + 1. When +25C == 0 it hands
    back to the owner state:
    - +5 == 0x10 → +6 = 0;
    - +24C == 1 → +6 + 1;
    - else +6 = 0x28, +1F0 = 0x22.

    Then clip 001885B0(p) at 8.0, +C0 = 0, +B0.. = +290..
    and +B4 -= 21;
  - 3: at the end of the swing (+2E == 3 and +28 >= 0x18):
    - +7 + 1, +38 = D_00248630[+25C];
    - +2E0 = -+38 / 120, +26C = -+C0 / 110, +2EC = 0.4;
    - clip 0xD5, then +2F4 = +B4.

    Otherwise 001751A0 and 0016A8B0;
  - 4 (clip 0x72 on 0x1000) and 5, the release:
    - +C0 += +26C. While +38 != 0, +38 += +2E0 (at <= 0, +38 = +C0 = 0)
      and 00178B90(p, 0);
    - 00181950 hitting ahead → +38 = 0;
    - 00181A70 (a 0x32 ledge) → +C0 = 0, +6 = 0x5B, return;
    - 00179880 and 00175900(p, 1): on a floor 0017C580. With no floor
      and +38 <= 0 and +C0 == 0: +5 = 7, +6 = 0, +1F0 = 0xD;
    - +23A == 0x5D → 0021D250(p, 0);
  - 0x63: 0021D2E0(p, 0x78, 0).
- **0016A8B0** (the swing; also 0015B130 state[0x15], which no store in
  the closure enters):
  - while +28 == 0, +25C steps toward +23F (+24C == 0) or down by one;
  - +C0 = wrap(+C0 + D_00248640[+25C] · cos(+38)) and
    +38 = wrap(+38 + 0.06283186). Each sum is stored before its wrap call;
  - +B0..+BC = (0, -21, 0, 1) through identity · rotate_x(+C0) ·
    rotate_y(+C4) · translate(+290);
  - by +2E (0..3): anim_clip_arbiter(p, 0xD8 / 0xDA (0, 1) or 0xD9 / 0xDB
    (2, 3) by +25C == 3, 1.0, (float)+28). Then +28 counts up to 0x18
    (+2E 0, 2) or down to 0 (1, 3), and +2E advances; 3 wraps to 0.
- **00181950**: three 0019AD00(p, point, 0x80000007) probes, at (0, 0, 4.5)
  on the yaw +C4, +C4 + pi/4 and +C4 − pi/4 (D_00248950). Each yaw is
  wrapped, through identity · rotate_y · translate(+B0). Returns the first
  probe's bit.
- **00181A70**: the point (0, 10, 5.5) on +C4. A 0019AD00 hit with
  attribute 0x32 that 001782A0 accepts → +2E4 -= 6, 1.
- **001814E0(p, arg)**: the point (0, 0, 4.4) (arg 0), 11.15 (+23F 3) or
  6.65 through +D0. 0019A570(point, point + 25 up, 4, 0) → +D = the
  attribute; 0x34 / 0x1E returns 1. **A miss, or a hit with any other
  attribute**, retries at 6.65 when +23F == 3; a 0x34 / 0x1E there
  returns 2.
- **00181730(p, side)**: two segment probes, 3 to the side (+38 − 3 /
  3 + +38), at z −1 and +1 (D_002754B8), over the column 15.5..25.5 above.
  A 0x34 hit returns 1.
- **001818D0**: 0019AD00(p, point 20 ahead, 7) != 0.
- **00181B80**: +2F4/+2F8 = +B0/+B8. **00181BA0**: by the clip +20C, the
  step lengths 0x70003A20 / 24 (0xBC/0xC3 2.25/4.5, 0xC1/0xC8 2.25/0,
  0xC2/0xC9 4.5/0, 0xBE/0xC4 4.5/4.5, 0xC0/0xC6 6.75/9, 0xBD/0xC5 6.75/4.5,
  0xBF/0xC7 9/9; other clips keep the words). Then +B0/+B8 = +2F4/+2F8 +
  (0, 0, 3A20, 0) through +D0, and +290/+298 += (0, 0, 3A24, 0) through +D0.
- **001787B0(p, side)**: 0019AFE0 from the point (∓18, 18, 0) to it plus
  (0, 0, 14.5) (001028B8, w = 1.0). With result & 6, attribute 0x32 and
  001782A0 → 1.
- **00181E20(p, frame, reach)**: the point (0, 0, 1 + reach) on the +218
  yaw frame (frame 0) or +D0, then 0019A570 up 25. A 0x1E hit returns 1.
- **00181F60**: the point 4 ahead. A 0x36 hit → 00199DB0 into +290 and
  +218 = wrap(pi/2 + atan2(−axis.z, axis.x)), via 0x70003A20. Returns 1.
- **00182090**: with +23F != 0, +C4 = 001B12B0(+218, +C4, 0.06981317).
  Returns 1 on arrival.
- **00182100**: by +2F1 (1: the node *(p+160), 2: *(p+15C), else 0.0),
  sqrt(dx² + dz²) from +B0/+B8. dx is 0x70003A20 and dz 0x70003A24; the
  sum is EE mula/madd.
- **00175390**: +23F = D_00810E57. At 0: +24C = 0, +218 = +C4, return 0.
  Otherwise:
  - +244 = cos(pi · E64/256) and +248 = cos(pi · E65/256);
  - +24C = atan2(−+248, +244) (a float);
  - +218 = wrap((pi + +24C) + D_008106A0);
  - return +23F.
- **001811F0**: +2F1 alternates 1 ↔ 2. The clip comes from (+2F1 was 1?)
  × (+25C == 3?) × (+23F 1 or 2?): 0xC5/0xC7, 0xC4/0xC6, 0xBD/0xBF or
  0xBE/0xC0. It is requested at 1.0; then +26C = 0.7 for +23F 1, else 1.0.
  **00181430**: clip 0xC8/0xC9 (+2F1 1) or 0xC1/0xC2 by +25C == 3.
- **00179010**: 0019AB20(p, +B0, (0, −6, 0, 1), 0x80000006). On a hit,
  +A = 1, +23B = the attribute and +9C = 0019A310 of the hit. On a miss,
  +23B = 0. Returns +A.
- **00179910**: area 2 only. The record *(D_0024D650[2][sub]) at +120
  (sub 0/2) or +F0 (sub 1) is the exit point. 0x70003A20/24/28 hold
  |point − +B0..+B8| (0011DF78). All below 8 sets D_008106B8 = 1,
  D_008106B5 = area and D_008106B7 = 6 / 5. D_008106B6 is 1 (sub 0/2),
  or 2 / 0 by D_00810730[2] & 0x80. Returns 1.
- **001B0B50**: D_008106BE = 1 (D_008106C8 & 1), 0x81 (& 2) or 0.

### Where the readable C differs from the instructions

The oracle found or confirmed these. The translation follows the
instructions.

- **001814E0** (NEARMISS): the C returns 0 after a first hit whose
  attribute is not 0x34 / 0x1E. The instructions (00181634) take the
  +23F == 3 retry there too. The oracle's first run caught this.
- **0016EBA0** (NEARMISS): case 0 calls 001B0B50 with no argument (the C
  passes the state byte), and case 0x15 passes the record in $a0 and p+2EC
  in $a1 to 00179880 (the C passes only p+2EC).
- **00169730 / 0016AE40** (NEARMISS): the calls the C writes without
  arguments (001751A0, 00175390, 00182100) receive the record. $a0 still
  holds it after 001696A0, 0016ADE0 and 00181D70, which never write $a0.

## 2. Where they sit in the closure

| +5 | Routine | Entered by | Exits |
|---|---|---|---|
| 0x10 | 00169730 | 001662D0 at 00167C38 | 1/7 (0xB), 1/0xC (0x5D), 1/0x13 (001696A0), 2/7 (00181D70), 1/7 via 0016A4B0 (and 0017C580 → 1/8, 0021D250 → 2/0x16) |
| 0x12 | 0016AE40 | 002230A0 | 1/7, 1/0x13 (0016ADE0), 2/7, and 0016A4B0's exits |
| 0x19 | 0016DE40 | 0016D130 at 0016D544 | 1/0x1A (cases 0 and 0x17), 2/0x19 (001823E0), 00184BA0's use chain |
| 0x1A | 0016EBA0 | 0016D130 at 0016D6D0; 0016DE40 | 1/0 (idle), 1/0x18 (0016D130) |

Every exit is already in the closure table. 0016A8B0 is also state[0x15],
but nothing in the closure stores +5 = 0x15.

## 3. Translation

- One routine per original. Each works on the raw record by original
  offsets and never touches the +214/+308 pointer words. Every address in
  a comment is the original instruction translated there.
- **Faults.** A state entry point checks, before its first write, that
  the context, every worker, the scene, the scratch and the major2 scene
  are bound (`em_player_closure1019_bound`). Otherwise it returns -1. A
  worker returning < 0 stops the routine at once. The writes made before
  the call stay, as the original order leaves them. Reading past a
  four-entry table (D_00248630 or D_00248640 with +25C > 3) faults at the
  read.
- **Arithmetic.** Every COP1 operation is `em_ee_float.h` on raw bits:
  add/sub/mul/div, neg, madd/mula (00182100), cvt.s.w (00175390, 0016A8B0),
  and c.eq/c.lt/c.le. fabs (0011DF78) is the sign clear. The copies
  00102948 / 001031E0 are inline.
- **Scratch.** The routines store the words the original stores:
  0x70003A20/24/28, the matrix 0x700036A0, and the vectors 0x700038A0..D8.
  They reload 0x70003A20 after every worker call, where the original
  reloads it.
- **Reused translations**, called directly (pure over the record):
  - `em_player_fall_drop` (00179880, em_player_fall.c);
  - `em_player_major2_00181D70` and `em_player_major2_001823E0`
    (em_player_major2.c).

  The oracle executes the originals of all three against them.
- **Names.** The types are `EmPlayerClosure1019*` and the functions
  `em_player_closure1019_*`. Lane player-closure-0e-18 already uses
  `EmPlayerClosureWorkers` / `em_player_closure_*`, and both headers
  compile together.

## 4. Binding (for the coordinator)

**Stage slots** (`EmPlayerStageWorkers`, em_player_floor.h), each with an
`EmPlayerClosure1019` context:

| Slot | Callback |
|---|---|
| `state[0x10]` | `em_player_closure1019_00169730` |
| `state[0x12]` | `em_player_closure1019_0016AE40` |
| `state[0x19]` | `em_player_closure1019_0016DE40` (0015B130 already skips it under the takeover) |
| `state[0x1A]` | `em_player_closure1019_0016EBA0` |
| `state[0x15]` (optional) | `em_player_closure1019_0016A8B0` (not entered by the closure) |

`EmPlayerClosure1019` = { workers, scene, scratch, major2 }. **major2 must
be the same `EmPlayerMajor2Scene` the +4 = 2 states use**: D_00275B14 is
written by 00181D70, 001696A0 and 0016ADE0, and read by 002230A0 (and
0016B8A0).

**Scene** (`EmPlayerClosure1019Scene`), filled from the canonical state
before the call and written back after:

- pads: D_00810E70 / D_00810E74 and the scratchpad masks
  0x70003B76 / 7C / 7E;
- D_0028A9A0 (the fade word), D_00810700 / 01 / 02 (area, sub-area,
  zone);
- D_008106BE, D_008106C8, D_008106A0 (the camera yaw);
- D_00810E57 / E64 / E65 (the gait byte and the raw stick);
- D_00810730[2], D_008106B5..B8 and 0x700031E4 (the ledge heading, as
  em_player_recovery.h's `EmPlayerRecoveryLedge.heading`);
- D_00275B10, D_00275B00[3] and D_00281B64[0].

The routines write only zone, d8106BE, d8106B5..B8 and the three saved
words.

**Scratch**: one `EmPlayerClosure1019Scratch` per player. Any worker whose
original writes 0x70003A20 must write `scratch->s3A20` (the fall and
recovery modules keep their own copies of that word; see their headers).

**Workers** (`EmPlayerClosure1019Workers`). Where a translation exists,
bind it through a small adapter:

| Worker | Original | Existing translation (adapter needed) |
|---|---|---|
| request | 001749A0 | the pose host's clip request (as for the fall / major2 workers) |
| arbiter | anim_clip_arbiter 001749F0 | as `EmPlayerLandWorkers.arbiter` |
| clip_885B0 / clip_88610 / clip_88550 | 001885B0 / 00188610 / 00188550 | untranslated (major2 has a 001885B0 worker, the others 00188550) |
| stick | 001751A0 | `em_player_recovery_stick_quadrant_worker` (same signature) |
| steer | 00174FD0 | the hang/slide steer_input worker (untranslated) |
| floor | 00175900 | `player_states_floor_service` (same signature, no adapter) |
| translate | 00178B90 | `em_player_recovery_translate_worker` (same signature) |
| random5 | 00179B90 | the footstep `random5` worker |
| sound | 001FBD50 | the positional sound worker |
| sound_1FB9F0 | 001FB9F0 | `EmSceneWorkers.w_001FB9F0` (same signature) |
| wrap | 001B1470 | `em_player_recovery_wrap` / `em_player_001B1470` |
| approach | 001B12B0 | `em_script_host_001B12B0` (lane script-host) |
| cosine / sine / atan2 / sqrt | 0011DE90 / 0011E2A8 / 0011E620 / 0011E748 | `em_sdk_math_original_float_*` |
| to_int | float_to_int 001281C0 | `em_player_recovery_float_to_int` |
| trs | build_trs_matrix 001C94B0 | `em_owner_services_build_trs_matrix` |
| apply | 001026A0 | `em_effect_original_001026A0` |
| vadd | 001028B8 | `em_player_hang_vadd` |
| identity / rotate_x / rotate_y / translate_m | 001029C0 / 00102B08 / 00102BB0 / 00102918 | `em_owner_services_identity_001029C0` / `_rotate_x_00102B08` / `_rotate_y_00102BB0` / `_translate_00102918` |
| segment | 0019A570 | `em_coll_segment_0019A570` (lane coll-segment) |
| move / sweep | 0019AD00 / 0019AFE0 | `em_coll_move_0019AD00` / `em_coll_move_sweep_0019AFE0` |
| ground | 0019AB20 | `em_actor_collision_ground_0019AB20` |
| slope | 0019A310 | `em_player_slope_angle` |
| floor_query | 00179450 | the fall module's `floor_query` binding (em_player_floor_query) |
| w00179150 | 00179150 | `em_player_closure_00179150` (lane player-closure-0e-18) |
| midpoint | 00199DB0 | `em_player_ladder_00199DB0` (lane ladder-entry) |
| ledge_top | 001782A0 | untranslated |
| area_point | the D_0024D650 record words | a read of the area table (area 2 only) |
| skeleton | anim_eval_skeleton 001C6DA0 | the pose host |
| script_1B0460 | 001B0460 | `EmAreaScriptWorkers.w_001B0460` (same signature) |
| fade / fade_1AEE10 | 001AEDE0 / 001AEE10 | `EmSceneWorkers.w_001AEDE0` / `w_001AEE10` |
| use | 00184BA0 | the coordinator's use hook (the USE mechanism) |
| use_probe | 00176F90 | `em_player_ladder_00176F90` (lane ladder-entry) |
| land / surface5d / teleport | 0017C580 / 0021D250 / 0021D2E0 | `em_player_fall_land` / `_surface5d` / `_teleport` |
| land_sound | 00182870 | `em_player_reaction_00182870` |
| sound_182A70 / clip_FC80 | 00182A70 / 0017FC80 | `em_player_ladder_00182A70` / `em_player_ladder_0017FC80` |
| root_node | *(*D_00275B40) + 0 / 8 | the major2 `root_node` binding |
| bone | *(p + 15C / 160) + C0 / C8 | the skeleton's node 71 / 72 world translation |

The probe workers fill an `EmPlayerProbeHit`: `node` (the low byte is the
+1A attribute), `point` (0x700031B0), `normal` (node +24) and `axis`
(node +34). They receive the mask exactly as the original passes it,
0x80000000 bit included.

**Build.** When bound, add `src/game/em_player_closure_10_12_19.c`,
`src/game/em_player_fall.c` and `src/game/em_player_major2.c` to `COMMON`.
The last two are not in it today. The lane build linked with all three
added, with zero warnings.

## 5. Verification

`tools/test_player_closure_10_12_19_reference.py`:

- **What runs.** The original instructions of the 32 routines above run
  unhooked:
  - the four states and every private callee;
  - 0016A8B0, 001B0B50;
  - 00179880, 00181D70, 001823E0 (the reused translations);
  - 00102948, 001031E0, 0011DF78.

  They run over the captured AREA11 RAM (`playable_ee.bin`, state 04,
  player record at 0x8102B0). Their bytes and the tables D_002488B0,
  D_00248630, D_00248640, D_00248950, D_002754B8 and D_0024D650[2] are
  checked against the pinned ELF first.
- **Guard rails.** Any other executed address fails the case. COP1 goes
  through `tools/ee_float_model.py`; VU0 is refused.
- **Workers.** Every other callee is hooked and scripted per case. The
  script sets return values, record writes, node words, 0x70003A20
  scribbles and probe hits.
- **Compared.** All 0x320 record bytes, the 25 scene words and the 35
  scratch words, at exit and at entry to every worker call. Also the
  worker call sequence with every argument (floats as bits, matrices and
  vectors as words).
- **Faults.** Injected on the native side, they must stop at the
  original's image at that call.
- **Coverage.** Every conditional-branch outcome of the 32 routines:
  719/719, plus 3 listed impossible outcomes. Two are the bltz on a
  zero-extended byte in 00175390. The third is 00169730's +23F test after
  001814E0 returned 2 (2 implies +23F == 3). All 47 handled (+5, +6)
  pairs are run.
- **Refusals.** 200 checks: each worker and each storage pointer missing
  → -1 and nothing written. +25C = 4 at 0016A4B0 +7 3 faults after +7
  advances.
- **Route census.** The 15 route beats (FIRST_LEVEL_ROUTE.md, 12,439
  frames) show +5 only in {0, 1, 2, 5, 6, 8, 0xB, 0xC, 0x1C, 0x25}. None
  of these states is reached, so no route replay exists. The test fails
  if a recapture reaches one. Beat 10 twice runs +5 0xB then 0xC with
  +1F0 0x17, the ladder climb 001662D0 whose 00167C38 store enters 0x10.
  The trace does not record +4.
- **Runs.**
  - Default (quick): 16,000 cases, about 8 s on the loaded lane machine.
  - `EM_TEST_FULL=1`: 200,000 cases, PASS, 719/719 branch outcomes
    (2 min 42 s under the same load).
- **Mutants.** Each is killed:
  - 001814E0 without the retry after an other-attribute hit (the NEARMISS
    C shape);
  - `<=` for the -24.0 test;
  - 0016EBA0 without 001B0B50;
  - 0x70003A20 not reloaded before float_to_int;
  - the pad x/y swap in 00175390;
  - +302 = 1 in area 8's first box;
  - 00181950 returning any hit;
  - the +2EC store moved after its clip request;
  - 0016A8B0 without the pre-wrap store of +C0;
  - `<` for the -85 test.

  One mutant is equivalent: mul + add in place of madd in 00182100. The
  EE madd differs only on an overflowing product, and a sum of squares
  saturates the same either way.

## 6. Limits

- **Reachability.** No capture reaches these states; the evidence is the
  unit oracle alone. The closure lists them because 001662D0, 002230A0
  and 0016D130 can enter them. The area-specific boxes of 0016DE40
  (areas 0, 3, 8, 0x13) and 00179910 (area 2) are not AREA11 (area 0xB).
  They are translated because the routines contain them.
- **Untranslated workers** that the live path needs: 001749A0 / the clip
  choosers 001885B0 / 00188610 / 00188550, 00174FD0, 001782A0,
  anim_eval_skeleton and 00184BA0's use chain. Until each is bound, the
  entry points refuse (-1), which the stage reports as a fault.
- **The node reads** (root_node, bone, area_point) are bound reads of
  world memory, not calls. The oracle checks them by writing the node
  words at the captured node records and scribbling them from hooked
  calls.
- **Infinite loop.** 0016DE40's drop to the ground (+B4 -= 1.0 until
  00179010 hits) is as unbounded as the original's.
- The UBSan (trap) build of the module ran the quick sweep clean. No
  ASan fixture exists; the oracle drives the module through ctypes.
