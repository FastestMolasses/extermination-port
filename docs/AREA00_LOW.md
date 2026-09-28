# AREA00 lane A00LOW: the new lowmem / io / audio / movie boot functions

Level 3 (AREA00) side track, lane A00LOW, 2026-09-28 (session s88). New files only:
`src/game/em_area00_low.{c,h}` (prefix `em_area00_low_`) and
`tools/test_area00_low_reference.py`. The module is built and tested but not wired: nothing in
the port calls it and the Makefile does not list it (the test builds it privately). Section 7
says where each routine would bind.

## 1. Scope

The rows of `../Extermination/build/s87/census/a00_delta.json` (`new_functions`) whose census
subsystem is lowmem, unknown_07, unknown_02, init_io, input_io, stream_archive, audio or movie:
39 boot functions, 24,244 bytes. The subsystem names are census labels, not evidence; the
routines are named by address. First run = the census's first beat and frame (the arrival rows
belong to beat a01_07, whose second half is the AREA00 arrival).

Eight already had a verified translation, which this lane reuses instead of translating again
(section 7): seven in `em_area01_exita.c` (docs/AREA01_EXITA.md; its test still passes) and
00193D90 in `em_camera_leftovers.c` (docs/CAMERA_LEFTOVERS.md, `em_camleft_00193D90`; its
test covers it as the `orbit` class and still passes). The other 31 (17,272 bytes) are
translated here.

| Function | Bytes | Census subsystem | Decomp source | First run | Translation |
|---|---:|---|---|---|---|
| 001000C0 | 32 | lowmem | byte-matched C | a00_03 f101 | this module |
| 00102870 | 32 | lowmem | asm body (the instructions) | a00_10 f974 | this module |
| 00102990 | 16 | lowmem | asm body (the instructions) | a00_03 f101 | this module |
| 00113478 | 180 | lowmem | NEARMISS C (ee-gcc) | a01_07 f3468 | em_area01_exita |
| 001181B0 | 612 | lowmem | NEARMISS C (ee-gcc) | a00_04 f1269 | this module |
| 00119080 | 52 | lowmem | byte-matched C (ee-gcc) | a00_04 f1269 | this module |
| 00128600 | 64 | lowmem | byte-matched C | a01_07 f4487 | em_area01_exita |
| 00128640 | 420 | lowmem | byte-matched C | a01_07 f4486 | em_area01_exita |
| 00128830 | 152 | lowmem | byte-matched C | a00_02 f486 | this module |
| 001288D0 | 228 | lowmem | byte-matched C | a00_02 f485 | this module |
| 00129F00 | 184 | lowmem | byte-matched C | a00_09 f483 | this module |
| 00129FC0 | 1540 | lowmem | byte-matched C (jump table) | a00_09 f398 | this module |
| 0012A5D0 | 2032 | lowmem | byte-matched C | a01_07 f531 | em_area01_exita |
| 0012ADC0 | 508 | lowmem | NEARMISS C | a01_07 f4488 | em_area01_exita |
| 0012AFC0 | 1092 | lowmem | byte-matched C | a01_07 f4487 | em_area01_exita |
| 0012B410 | 1076 | lowmem | NEARMISS C | a00_01 f176 | this module |
| 0012B970 | 1188 | lowmem | byte-matched C (jump table) | a00_05 f1 | this module |
| 0012BE20 | 1644 | lowmem | byte-matched C (jump table) | a00_02 f485 | this module |
| 0012C490 | 1540 | lowmem | byte-matched C (jump table) | a00_03 f72 | this module |
| 0012CAA0 | 1952 | lowmem | byte-matched C | a00_09 f424 | this module |
| 0012D240 | 828 | lowmem | NEARMISS C | a00_09 f705 | this module |
| 0012D580 | 708 | lowmem | byte-matched C | a00_03 f73 | this module |
| 0012D850 | 232 | lowmem | word assembly | a00_09 f193 | this module |
| 0012DE90 | 480 | lowmem | NEARMISS C | a00_09 f705 | this module |
| 0012E070 | 60 | lowmem | C linked from asm | a00_09 f397 | this module |
| 0012E0B0 | 420 | lowmem | byte-matched C | a00_04 f104 | this module |
| 0012E260 | 84 | lowmem | byte-matched C | a00_04 f104 | this module |
| 0012E2C0 | 224 | lowmem | byte-matched C | a00_02 f487 | this module |
| 00193D90 | 280 | init_io | word assembly | a00_09 f905 | em_camera_leftovers |
| 00198CE0 | 164 | init_io | byte-matched C | a00_07 f889 | this module |
| 00198F10 | 776 | init_io | NEARMISS C | a00_07 f1036 | this module |
| 001B5360 | 640 | input_io | byte-matched C (jump table) | a00_02 f577 | this module |
| 001B7670 | 96 | unknown_02 | asm body (the instructions) | a00_04 f992 | this module |
| 001B7700 | 312 | unknown_02 | NEARMISS C | a00_08 f360 | this module |
| 001B8AB0 | 1284 | unknown_02 | NEARMISS C | a00_04 f1542 | this module |
| 001E7310 | 296 | stream_archive | byte-matched C | a00_09 f756 | this module |
| 001FC580 | 348 | audio | byte-matched C | a00_02 f576 | this module |
| 001FF030 | 72 | movie | byte-matched C | a00_08 f363 | this module |
| 0022DCD0 | 2396 | unknown_07 | NEARMISS C | a01_07 f531 | em_area01_exita |

Byte-matched C was translated from the C and the oracle decides. NEARMISS C, word assembly,
asm bodies and the C linked from asm were read against the original instructions
(`../Extermination/build/.asmnorm/`, local only); where they differ from the C, the
instructions win (section 6).

## 2. Model

The lane-EXITA design (AREA01_EXITA.md section 2, itself lane SYS's), unchanged except where
noted.

- **Memory.** Regions keyed by original address: RAM, the scratchpad and a stack region.
  001FC580's two stack words (`sp - 0x40 + 0x38` / `+ 0x3C`) are the only stack items: their
  addresses go to 001FBF50, which fills them, and 001FC580 reads them back. Every other local is
  a C local. 00102870 and 00102990 move whole quadwords and, like the EE, ignore the low four
  address bits of both pointers.
- **The canonical grab-slot byte.** D_0081083C (the player's grab-slot bits, census L01, a
  migrated progress byte of EmProgress) is never addressed through the regions: 0012E070 and
  0012E0B0 reach it through `EmArea00Low.grab_bits`, a view the binder points at the canonical
  byte (SCENE_COORDINATOR_DESIGN.md 3.2). This keeps `tools/test_scene_no_shadow.py` (G6)
  passing without a REACHERS entry. A NULL view that a routine reaches is a NULL fault with
  address 0.
- **Calls.** One worker, `call`, for every call leaving the 31: callee address (for 00129FC0's
  indirect call, the word it loads from +0x4C), a0..t3 as 64-bit register images, f12..f19 as
  raw bits, the counts `na` / `nf`, and the stack pointer at the call. The worker returns all 64
  bits of v0; the module tests the whole register wherever the original does (every zero /
  non-zero test of a callee result, the `busy` / `mode` results of 001C2770 that 00129FC0,
  0012B410, 0012B970, 0012BE20, 0012C490 and 0012D850 keep across the routine and hand whole to
  0012D580, and 001274B0's sign in 001000C0). 001FF030 leaves through a tail jump to 001FF080:
  that is a call with 001FF030's own entry stack pointer. Calls among the 31 are direct.
- **Arithmetic.** `em_ee_float.h`. 00198F10's distance is the first product into the
  accumulator plus the second (the accumulate op), not two adds; 0012BE20 / 0012C490's tilt is
  pi * (75.0 * -x) / 180.0 in that order; 0012B410's heading is 2pi * float(draw & 0xF0) / 256;
  00102870 is the VU0 divide of 1.0 by f12 into Q and a Q multiply of x, y, z; 00102990 is
  VFTOI0 per lane.
- **Fail-stop.** Unmapped address, NULL worker, negative worker result, NULL output, NULL
  grab_bits view reached, and a new code **TRAP**: 00119080 divides by (c & 0xFF), and the
  original takes its divide-by-zero break when that is zero; the module latches
  EM_AREA00_LOW_FAULT_TRAP with the routine's address. The latch, clear_fault and NULL-context
  rules are EXITA's.
- **Store trace.** `EM_AREA00_LOW_STORE_TRACE` (test builds only); `wr` is the only store path
  to the regions. The grab_bits view is not traced by address; the test compares its line at
  every check instead.

## 3. What each routine does

Full descriptions are in the comment above each translation. Short forms (P = the record
0x008102B0, S(x) = scratchpad word 0x7000xxxx):

- **001000C0(a0..a3) -> v0.** 001274B0 with a0..a3 as received; 1 when its whole v0 is
  negative.
- **00102870(a0, a1; f12).** a0.xyz = a1.xyz * (1 / f12) through Q, a0.w = a1.w.
- **00102990(a0, a1).** a0 = VFTOI0(a1), four lanes.
- **001181B0(ev).** Halfword ev +0x34 == 1: for the 48 records at 0x0027CCC0 with +0 == 1,
  +0x1A == 2, +0x22 == ev +0x24 and three more fields equal to the event data bytes / ev +0x18
  (the zero-extended halfword against the sign-extended word): +0x46 = 1, +0x50, +0x52 and two
  tempo-scaled lengths (byte * 4 * halfword 0x0027F77A / 60). ev +8 += 6 (from the value read
  once at the start). Otherwise: the event byte into (word 0x00281ACC)[3], then for the records
  with +1A == 1 and five more matches, +0x34 = that byte, k = 001179E0(i, ev), 001157F0(1, i,
  k >> 16, k & 0xFFFF); ev +8 (read again) += 3.
- **00119080(a, b, c, d) -> v0.** Low bytes: (a + (b - a) * d / c) & 0xFF; c zero traps.
- **00128830(a0; x, y, z).** a0 +0xB0.. += (x, y, z) turned by the matrix at a0 +0xD0.
- **001288D0(a0, a1).** S38A0 = (0, -1.5, 0, 1) turned by (word (word D_00275B40) +0x14) +
  0x90; S38B0 = two integer quads by a1 +0xE1; 001F4A00(S38A0, S38B0).
- **00129F00(a0, a1).** a1 +0xF0 += 0.06 (4.0 cap), a0 +0xB4 -= it, the (0, -5, 0) probe
  0019AB20; a hit resets a1 +0xF0 = 0.06.
- **00129FC0(a0, a1).** mode = 001C2770(a0, a1, 0); states 0..7 of a0 +5 (damage entry,
  knock-down or flinch, the shared damage step of states 1 and 5 with its 60-frame voice
  cooldown, 0012D580, the fall, the 10.0 height test, 00129F00); tail 001C3D60 when mode is
  zero, +0xF4 = 001C64F0(a0; 1.0), the 0x70003000 -> 0x70003400 copy, 001C69A0, and when
  001B17A0 is non-zero 001288D0 (signed +0xFA non-zero) and the handler at +0x4C.
- **0012B410 / 0012B970 / 0012BE20 / 0012C490 / 0012D850(a0, a1).** Sub-state machines at
  a0 +6 (0012D580 at +7) of the 0012A5D0 record family: the settle probe (0, -1.4, -5) through
  001C25E0 and 001C2540 / 001C24D0, 00128600 / 00128640 choices, the 00128830 moves with
  001287F0 animations, the rise (tilt from +0xF0, 0.04 per frame), 0012E2C0 turns, the spray
  effects 001EFFD0 (0.838, 0.42) at 70.0 / at the -10 degree window within 50.0 of D_00810350,
  0012E0B0 claims, the 0x2D0 counter, the landing.
- **0012CAA0 / 0012D240(a0, a1).** The attachment to one of P's bone matrices (row +0xF6 & 7
  of the table 0x00242DD0, the pointer P +0x110 + 4 * row, + 0x90), its approach (+0xD4 from
  1.0 down by 0.02 to 0.2), the hold, the throw-off with P +0x224 / +0x22C speeds by D_0081070A
  and a1 +0xE1, and the 0x70003B8D / D_008106BC reset to +5 = 0xA.
- **0012DE90(a1).** +0xD4 += 0.02 (cap 2.0); scales the two matrices behind D_00275B40 +0xC /
  +0x14 by it, keeping their +0xC0..+0xC8 words.
- **0012E070(a0).** Bit 7 of +0xF6: grab bits &= ~(1 << (+0xF6 & 7)). Then +0xF6 = 0 always.
- **0012E0B0(a0, a1) -> v0.** Gates, then by the heading difference to P the first free grab
  bit of 0..3 or 4..7: claimed, a1 +0xF6 = bit + 0x80, a0 +0 = 2, P +0 |= 2, result 1.
- **0012E260 / 0012E2C0.** Heading steps toward D_00810350 / D_00810358 (the second with its
  0x78-frame timer and equality test).
- **00198CE0 / 00198F10(e, a1).** Camera record steps (the second builds its eye from a1 +0xC0
  and a +-35 offset by a1 +0x0D, and has the fixed home pose for area 8 sub 3 within 8.0 of
  (123.5, 156.4)); tail e +0x44 = 001B1240(D_008105D0; D_008105E0, D_008105E8).
- **001B5360(p).** The down probe 0019A570 (200 below in mode 4, else 30) and the correction
  001F9100 / 001F9180 with the weight of p +3.
- **001B7670 / 001B7700 / 001B8AB0(a0, a1, a2) -> v0.** Area-script command handlers (the
  table ftab_0024D880 holds them at indexes 0x13, 0x11 and 3): the S3B91 latch by the command's
  mode word; the D_008106CE / D_008106CF request with the D_00275BD8 gate; the eye / target
  shake with three random formulas and the +0x0C duration.
- **001E7310(p).** Channel-9 owner: 001D2830 / 001DEE80 / 001DEEC0 / 001DF5A0 with a decaying
  amplitude and intensity, then 001AFC10.
- **001FC580(a0, a1).** The per-sound slot of 0x00281F30 / F40 / F50 / F60 for the listed
  sound ids, from 001FBF50's two outputs.
- **001FF030(a0).** D_00810730[D_00810700] = a0 when bit 7, D_00810701 = a0 & 0x7F,
  D_00275BD8 = 1, tail 001FF080(0, 0x1D).

## 4. How it was verified

`python3 tools/test_area00_low_reference.py` (EM_TEST_FULL=1 for the sweep).

**Oracle.** Lane SYS's recording interpreter SysEE (FallEE: the measured COP1 / VU0 model)
runs the original code of each routine over captured AREA00 RAM and scratchpad
(`../Extermination/build/s87/route_a00/<beat>/`, default a00_09); the native module runs over a
copy of the same bytes. FallEE has no VFTOI; the test adds VFTOI0 / VFTOI4 through the measured
`ee_float_model.vu_ftoi` (a subclass in the test, used by 00102990 only).

**Callees.** 59 policies; the test asserts one for every direct call target of the 31 (and
the tail target 001FF080) and checks each against the registers the callee reads on all its
paths (lane SYS RegScan).
- **Run as original** on both sides: 001026A0, 001026D0, 00102760, 001028B8, 00102948,
  00102958, 001029C0, 00102A60, 00102B08, 00102BB0, 00102C58, 001031E0, 00103230, 0011DE90,
  0011DF78, 0011E2A8, 00122BB8 (the LCG), 001274B0 (the soft-float compare), 001281C0,
  001B1240, 001B12B0, 001B13F0, 001B1470.
- **Stub** (scripted v0 / f0): the rest, 36, including the EXITA-translated 00128390,
  00128600, 00128640 and 0012ADC0. 0012B410 stores the a1 register as it is after 00128390;
  that leaf never writes a1 (read from its instructions), so the value is the 1 it set.
- **001FBF50's outputs.** 001FBF50 stores 0 into both stack words at its entry before it reads
  anything, so what they held is not an input and is not compared; the stub always writes them
  (zeros unless a case scripts values), on both sides.

**Lockstep.** EXITA's: at every call the callee, stack pointer, compared argument registers,
float arguments, every register the native says it sets, stub pointer snapshots, and every
line either side stored to (plus the grab_bits line) before any stub side effect. After the
last store the same check, the result, the call log, all 32 MiB and the scratchpad.

**Cases** (1,981 enumerated, captured and review-pinned; 160 random more in full mode).
- `cap`: every captured 0012A5D0 record (0x7AD490, 0x7ADD60) in beats a00_00..a00_09 through
  the arm its state selects (+4 = 2: 00129FC0; +4 = 1: +5 2..9 -> 0012B410, 0012B970,
  0012BE20, 0012C490, 0012CAA0, 0012D240, 0012D580, 0012D850), and 001B5360, 0012E260,
  0012E2C0 on each; D_00275B40 is pointed at a synthetic two-matrix block (at the end of those
  beats it points at 0x008102F0, whose +0x14 word is 0).
- `t`, enumerated from the code: every state / sub-state of every machine with 001C2770
  results 0 / 1 / 2^32, both outcomes of every flag and counter test (+0xF4 0x1000 / 0x4000 / 0x5000,
  +0xE4 0 / 0x100 / 0x101 / 0x300 / 0x400 / 0x500 / 0x600, +0xD0 at 0 / 1 / 2 and 0x2B8 / 0x2B9,
  +0xF6 rows 0 / 3 / 4 / 7 / 0x85, both D_0081070A and +0xE1), stub results non-zero only in
  the upper half, LCG draws of both parities, the grab bits full / empty / half, every 001B5360
  mode 0..13 and 0xFF, every 001FC580 sound id and the slot compare, the three script handlers
  for modes 0..3 and -1 with every state and gate, 001181B0 events where each record fails one
  test and some pass all (both event kinds, ev +0x18 with bit 31), 00119080 including both
  divide-by-zero traps (the original's break is required; the native must fault TRAP),
  001000C0 on doubles either side, 00102870 / 00102990 with zero, denormal, huge and
  unaligned inputs.
- Exact thresholds through the EE sums, and one ulp either side, solved with
  `ee_float_model`: 00129F00's 4.0, 0012DE90 / 0012D240's 2.0, 0012CAA0's 0.2, 0012BE20's
  -10 degrees, 0012B970's 70.0 equality, 00129FC0's 10.0.
  Both outcomes of a test is not every boundary value: a counter landing exactly on its limit
  is covered only where listed here or in `k`.
- `k` (25, all pinned in the default run): the round-3 review killers (section 5).
  0012E2C0 state 1 with b +0xE8 and b +0xEC different (1.0 / 0.5 and 0.5 / 1.0; the captured
  +0xEC is 1.0); 0012B410 state 1 with player +0x230 = 8 and 1 and word 0x70003B68 = 1, 2, 4,
  8, 0x10, 0x11, 0x18, 0x20, 0x21 (each single bit of the 0x1F / 0xF frame masks and the next
  bit up); 00198F10 state 1 with e +2 = 5; 00129FC0 states 1 and 5 with the damage step
  leaving +0x34 exactly 0 (hp 2, damage 2) and 1 (hp 3).
- `r` (full mode): 160 random perturbations of the arm records and the globals they read.

**Stub side effects (fx).** For every case of 17 routines an `fx` and an `fx before` variant
(EXITA / lane SYS rounds 1 and 5). Full: 3,868; default: a fixed sample of 60.

**Store-site variants.** EXITA's round-4 variants over the store sites the run's cases reach.
Full: 491 variants (549 store sites reached, 285 covered by the cases); default: a sample of
40 (its smaller case set reaches 548 store sites, 255 covered).

**Branch coverage.** Full mode asserts both outcomes of every conditional branch of the 31
except two, UNREACHABLE with the proof in the test: 001181B0's two divide-by-zero guards
compare the constant divisor 60 with zero. The default run keeps a pinned greedy cover (the
list in the test, computed once from the full case set) plus the mutant killers, and reaches
the same coverage. Jump-table arms have no conditional branch of their own; every arm is a
case.

**API checks (10).** NULL context, NULL output, the latch, clear_fault (also NULL), the TRAP
fault, an unmapped record (UNMAPPED with the address), the NULL grab_bits view (reached and not
reached), a NULL worker and a worker returning -1.

**Timings (M1, default worker count, cached library, other lanes running; measured after the
round-3 cases).** Default: 341 of 1,981 cases, 6.2 s user, 3.5 s wall. Full: 2,141 cases,
70.9 s user, 27.4 s wall. (The round-3 reviewer measured the earlier default at 4.4 s user,
2.5 s wall; timings vary with load.)

## 5. Mutants

One bounded sweep (a scratch generator, not kept; each mutant compiled from its own copy of
the source with `EM_AREA00_LOW_SOURCE`, the copies deleted afterwards): 48 single-operation mutants
of the routine bodies, seed 7, drawn from `==` / `!=`, EE lt / le, add / sub, mul / div,
store widths, signed / unsigned loads, `+ 1` -> `+ 2`, masks, a dropped store or call, and hex
constants with the low bit flipped.

| Round | Killed (default) | Survived | Did not compile |
|---|---:|---:|---:|
| on the first final C (before the grab view) | 39 | 8 | 1 |
| same seed on the final C (different draw: the file changed) | 43 | 5 | 0 |

Survivors and what closed them:
- Killed by the full run, not by the default sample: the 0012E070 +0xF6 store made a byte
  store, dropped stores in 00129FC0 state 3 and 0012B970 state 2, 0012D580's +0xE4 = 0x500
  and 0012BE20 / 0012C490's +0xD8 = 0 made halfword stores, a dropped store in 00129FC0 state 5.
  Their killing cases are pinned (QUICK_KILLERS), with new cases where the full run's killer
  was a store-site variant (`e4 upper`, `st2 d8 set`: the old value's upper half non-zero).
- Killed by no case: 00129F00's 4.0 cap moved by one ulp. The exact-threshold cases above were
  added and kill it (also pinned).
- Equivalent (5): a signed / unsigned exchange where only equality with 8 or 0 is tested
  (D_00810700, D_00275BD8), where only the zero test and bit 14 are used (00129FC0's +0x36),
  where only the low three bits are used (0012CAA0's +0xF6 row) and where the value is
  truncated to 16 bits before the zero test (0012D850's counter).

With the pins every non-equivalent mutant of these two rounds (seed 7) is killed by the default
run. Across both rounds: 13 survivors, 5 of them equivalent (the second round alone: 5
survivors, 2 of them equivalent).

**Round-3 review sweep (reviewer, seed 2026, 44 mutants; not rerun here).** Default killed
32, full 35; 9 survived both modes. Five are equivalent, for these reasons:
- m03, the 0012D580 +7 switch on a signed byte: the cases are 0..3 and default, so
  the sign does not matter.
- m20, a signed / unsigned +0x36 before `& 0x2000`: the mask discards the sign bits.
- m31: the +5 = 0xA halfword store is followed at once by +6 = 0.
- m41: the difference is truncated to 16 bits either way.
- m43: D_008106BC is compared only with 0.

The four that are not equivalent were closed by the `k` cases, each pinned in the default
run:

| Mutant | Change | Killing case (default run) |
|---|---|---|
| m00 | 0012E2C0 state 1 reads b +0xEC for +0xE8 | `k 12E2C0 s7 1 d2 2 e8 1.0 ec 0.5` (result 1 vs 0) |
| m15 | 0012B410 state 1 frame mask 0xF -> 0xE | `k 12B410 st1 p230 1 m0x1` (extra 0012ADC0 call) |
| m27 | 00198F10 e +1 = 0 as a halfword store | `k 198F10 st1 m0x11 e2 5` (0x8101E2 differs at 001B1240) |
| m39 | 00129FC0 damage step `<= 0` -> `< 0` | `k 129FC0 st1 hp 2 dmg 2` (+0 state byte differs) |

The same edits were also compiled with `EM_AREA00_LOW_SOURCE`, together with the frame-mask
neighbours the review named. That is 11 more single-constant edits, not a new sweep:
- 0x1F -> 0x1E / 0x1D / 0x1B / 0x17 / 0x0F / 0x3F;
- 0xF -> 0xE / 0xD / 0xB / 0x7 / 0x1F.

The default run killed all 15. Each mask edit was killed by the `k 12B410` value that
isolates its bit, except 0x1F -> 0x0F, which the existing pinned `t 12B410 st1 p230 8 m0x10
d0 1` kills. The unmodified module passes every `k` case.

## 6. Findings (decomp side; no decomp file was edited)

- **0012E070, C linked from asm: the C is wrong.** The halfword +0xF6 = 0 store sits in the
  return's delay slot, so the original clears +0xF6 on both paths; `src/func_0012E070.c` clears
  it only when bit 7 was set. The oracle shows it (`t 12E070 f6 0x7f ...`). The file links from
  its assembly, so the ELF is unaffected; the C is not ground truth.
- **0012B410, NEARMISS C.** `func_001C2770(0)` passes one argument; the original calls it with
  its own a0 and a1 unchanged and a2 = 0. State 4 sub 2 stores byte +0 from the a1 register
  after 00128390, not a literal 1 (the same value, since 00128390 does not write a1).
- **0012D240, NEARMISS C.** `func_001287F0(0x19, 0.0f)`: the original passes its a0 and a1
  unchanged and a2 = 0x19.
- **001B8AB0, NEARMISS C.** The end test is "not (+0x10 < +0x0C)" (a less-than compare and
  its not-taken path), where the C writes `>=`; mode 2 reads the word +0x14 once before each
  draw (the multiplier) and again after it (the halved subtrahend), where the C reads both
  after the draw (different only if the draw's seed store reached +0x14).
- **001181B0 (ee-gcc NEARMISS C).** The event branch reads ev +8 once and stores base + 6; the
  other branch reads it again at the end. The C's `ev->f08 = ev->f08 + 6` reads it again in
  both (different only if a record store of the loop reached ev +8).
- **0012DE90, NEARMISS C.** The first six copies use D_00275B40 read once; every later use
  reloads it and the +0xC / +0x14 pointer, as the C does. No semantic difference found.
- **Script table.** 001B8AB0, 001B7700 and 001B7670 are entries 3, 0x11 and 0x13 of
  ftab_0024D880 (the area-script commands, called with (owner, script block, command record)).
  The census labels (unknown_02) say nothing about that.

## 7. Binding, overlaps and reuse

Nothing is wired. Where each routine would bind (all through an adapter from the caller's
worker ABI to this module's region model, as EXITA's):

- **The 0012A5D0 family.** `em_area01_exita.c`'s 0012A5D0 calls 0012B410, 0012B970,
  0012BE20, 0012C490, 0012CAA0, 0012D240, 0012D580, 0012D850, 00129FC0, 001288D0 and
  0012E070 through its worker, and 00129780 calls 00128830; `em_area01_sys.c`'s 00128C10 calls
  0012D580, 00129FC0 and 001288D0, and 00128B80 calls 0012E070. These translations are those
  workers. `em_enemy.c` describes several of them (0012D580, 00129FC0, 001B5360, 00128830) in
  comments of an unverified legacy model; it translates none.
- **Camera.** `em_camera_leftovers.c`'s 0018BC20 dispatch has the worker slots `w_00198CE0` and
  `w_00198F10`; these translations can back them. 00193D90 is already `em_camleft_00193D90`.
- **Scene.** `em_scene_frame.c` calls `w_001FF030` in 0x1AE040 state 6; this is it.
- **Sounds.** `em_crate_original.h` / `em_drum_original.h` take 001FC580 as their `sound`
  worker and `em_area11_boxes.c` leaves it unbound (fail-stop); this is the translation for it.
- **Area script.** `em_area_script.c` handles the AREA11 command set; commands 3, 0x11 and 0x13
  (001B8AB0, 001B7700, 001B7670) are not in it and fault today. 001B7670 writes S3B91, a
  canonical scratchpad byte: its binding must write the canonical storage and join G6's WRITERS
  for `spad3B91` as the counterpart of that original writer.
- **Canonical bytes.** 0012E070 / 0012E0B0's `grab_bits` view goes to EmProgress's D_0081083C
  (em_scene_state.h). 0012CAA0, 0012D240 and 0012E0B0 read S3B8D, and the binding must map the
  scratchpad region's byte onto the canonical one (as for EXITA's 00128640).
- **Others.** 001181B0 is called by 001152D8 and 00119080 by 00116598 (ee-gcc code around the
  0x0027CCC0 records); 001000C0 by 00189EC0, 0017D080, 001A5C30 and 001A7870 (the collision
  modules `em_coll_list_passes.c` / `em_coll_segment_walkers.c` / `em_player_recovery.c`
  compute its compare inline inside their own verified translations); 00102870 by 001F1F60 /
  001E2BA0; 00102990 by 001CEFD0 / 001F15F0; 001E7310 is referenced only from the table word at
  0x00258ECC, whose dispatcher was not identified here.
- `em_iop_stream_00113478` remains a drive model, not the EE routine (AREA01_EXITA.md 7).

## 8. Known gaps

- **Aliasing.** As EXITA: records overlapping the scratch blocks the routines write (at
  0x70003000, 0x70003400, 0x70003600..0x7000362C, 0x700036A0..0x700036DC and
  0x700038A0..0x700038BC) or each other are not tested. The module
  keeps the original's load / store order.
- **Inputs.** The captured arm cases come from end-of-beat images; the routines' first runs
  (the census frames) are mid-beat and no capture holds them. D_00275B40's two matrices are
  synthetic in every case (the captured images do not hold a live pair).
- **Callee side.** Every stub (001C2770, 001C25E0, 001C2540, 001C24D0, 001C3D60, 001C3DB0,
  001287F0, the effect / sound / probe services, 001FBF50, 0019A570, 0019AB20, 001F9100 /
  001F9180, 001F4A00, 0018C4B0 / 0018C6A0, 00191530, 0021BD60, the channel services 001D2830 /
  001DEE80 / 001DEEC0 / 001DF5A0, 001AFC10, 001FF080, 001179E0 / 001157F0) is verified at its
  call only; its effects on later loads are covered by the fx side effects.
- **Pointers above 0x7FFFFFFF.** As EXITA: outside the memory model; the module faults
  UNMAPPED.
- **001E7310's caller** (the table at 0x00258ECC) was not identified.

## 9. Reproduce

macOS arm64, port repo root:

```
python3 tools/test_area00_low_reference.py                   # default (~6 s CPU)
EM_TEST_FULL=1 python3 tools/test_area00_low_reference.py    # full sweep, coverage asserted
EM_AREA00_LOW_GAPS=1 python3 tools/test_area00_low_reference.py   # list missed outcomes
python3 tools/check_no_disassembly.py src/game/em_area00_low.c src/game/em_area00_low.h \
    tools/test_area00_low_reference.py docs/AREA00_LOW.md
python3 tools/test_scene_no_shadow.py
```
