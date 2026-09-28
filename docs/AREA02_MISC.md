# AREA02 lane L4MISC: the remaining new boot functions of the revisit and AREA02

Level 4 (AREA01 revisit and AREA02) side track, lane L4MISC, 2026-09-28 (session s88). New files
only: `src/game/em_area02_misc.{c,h}` (prefix `em_area02_misc_`) and
`tools/test_area02_misc_reference.py`. The module is built and tested but not wired: nothing in
the port calls it and the Makefile does not list it (the test builds it privately). Section 7 says
where each routine would bind.

## 1. Scope

The rows of `../Extermination/build/s87/census/a02_delta.json` (`new_functions`) in the boot
region whose census subsystem is weapon_equip, hud_objects, lowmem, unknown_01, unknown_03,
render_vif, input_io, frame_update, init_io, fx_render, frame_main, audio or area_state: 33
functions, 11,420 bytes. The subsystem names are census labels, not evidence; the routines are
named by address. First run = the census's first beat and frame (FOURTH_LEVEL_ROUTE.md section 9).

Before translating, the port was searched for every address. Five of the 33 already had a verified
translation (an original-instruction oracle over the same function), which this lane reuses instead
of translating again (section 7); their tests were run again this session and pass. 0015C7C0 is
verified inside `em_player_0015C700` only on the path through 0015C700, so it is also translated
here as a standalone routine (0015C750 calls it directly). The other 28 (10,216 bytes, 0015C7C0
included) are translated here.

| Function | Bytes | Census subsystem | Decomp source | First run | Translation |
|---|---:|---|---|---|---|
| 0011C128 | 924 | lowmem | word assembly | a01r_00 f379 | this module |
| 0011E520 | 252 | lowmem | NEARMISS C (ee-gcc) | a01r_00 f379 | this module |
| 0015C700 | 72 | frame_update | byte-matched C | a02_s0 f861 | `em_player_0015C700` (em_player_stage_workers.c) |
| 0015C750 | 84 | frame_update | byte-matched C | a02_s0 f619 | this module |
| 0015C7C0 | 516 | frame_update | byte-matched C | a02_s0 f619 | this module (standalone); also inside `em_player_0015C700` |
| 00199FA0 | 476 | init_io | NEARMISS C | a02_02 f380 | `em_player_ladder_00199FA0` (em_player_ladder_entry.c) |
| 001A8970 | 620 | unknown_01 | byte-matched C | a02_02 f369 | this module |
| 001A8E80 | 188 | unknown_01 | asm body | a02_02 f369 | this module |
| 001A9360 | 276 | unknown_01 | NEARMISS C | a02_02 f369 | this module |
| 001AA640 | 184 | frame_main | asm body | a02_01 f612 | this module |
| 001AA700 | 152 | frame_main | byte-matched C | a02_01 f612 | this module |
| 001B62C0 | 552 | input_io | NEARMISS C | a02_s0 f622 | `em_item_stick_sample` (em_item_trail.c) |
| 001B6AE0 | 260 | input_io | byte-matched C | a01r_01 f1074 | this module |
| 001D3A30 | 144 | render_vif | byte-matched C | a01r_03 f735 | this module |
| 001D3AC0 | 12 | render_vif | byte-matched C | a01r_03 f735 | this module |
| 001D3C40 | 152 | render_vif | byte-matched C | a02_00 f59 | this module |
| 001D3CE0 | 12 | render_vif | byte-matched C | a02_00 f59 | this module |
| 001D66A0 | 652 | render_vif | NEARMISS C | a01r_s0 f287 | this module |
| 001E49F0 | 8 | weapon_equip | byte-matched C | a02_02 f465 | this module |
| 001E4A00 | 728 | weapon_equip | NEARMISS C | a02_02 f124 | this module |
| 001E4CE0 | 2320 | weapon_equip | NEARMISS C (two jump tables) | a02_02 f124 | this module |
| 001EBD20 | 228 | hud_objects | NEARMISS C | a02_02 f46 | this module |
| 001EC5F0 | 556 | hud_objects | NEARMISS C | a02_03 f357 | this module |
| 001EC9A0 | 124 | hud_objects | NEARMISS C | a02_01 f615 | this module |
| 001ECEF0 | 180 | hud_objects | byte-matched C | a02_03 f658 | this module |
| 001EDAF0 | 844 | hud_objects | NEARMISS C | a01r_01 f1682 | this module |
| 001F4010 | 168 | fx_render | NEARMISS C | a01r_01 f1631 | this module |
| 001F4E40 | 244 | fx_render | asm body | a02_01 f487 | this module |
| 001F6B30 | 36 | fx_render | byte-matched C | a02_02 f405 | this module |
| 001F9660 | 276 | unknown_03 | NEARMISS C | a01r_01 f1139 | this module |
| 001FB0B0 | 16 | audio | byte-matched C | a02_05 f1633 | `em_stream_lanes_001FB0B0` (em_stream_lanes_original.c) |
| 001FC520 | 88 | audio | byte-matched C | a01r_02 f1012 | `em_sfx_service_release` (em_sfx_bank.c) |
| 0021BD10 | 76 | area_state | C linked from asm | a02_02 f465 | this module |

Byte-matched C was translated from the C and the oracle decides. NEARMISS C, word assembly, the
asm bodies and the C linked from asm were read against the original instructions
(`../Extermination/build/.asmnorm/`, local only); where they differ from the C, the instructions
win (section 6).

## 2. Model

The lane-A00LOW design (AREA00_LOW.md section 2, itself lane EXITA's and lane SYS's), with these
additions.

- **Memory.** Regions keyed by original address: RAM, the scratchpad and a stack region. Three
  routines hand a local's address to a callee, so that local lives in the stack region at the
  original's offset below the entry stack pointer: 0011E520's exception record (sp - 0x60, 0x24
  bytes: +0 type, +4 name, +8 and +0x10 the argument as a double, +0x18 the return value, +0x20 the
  error code), 001E4CE0's packet descriptor (sp - 0x60) and 001F9660's 0x2F0-byte copy
  (sp - 0x2F0). Every other local is a C local. 001D66A0's quadword clear ignores the low four
  address bits, as the EE does.
- **Calls.** One worker, `call`, for every call leaving the module (callee address, a0..t3 as
  64-bit register images, f12..f19 as raw bits, the counts `na` / `nf`, the stack pointer at the
  call); the worker returns all 64 bits of v0 and f0. The module tests the whole register wherever
  the original does (0011E080's and 0011DB90's results, 0021BB00's result, 001CD070's handle
  against 0xFFFFFF), keeps it whole where the original passes it on (001CD070's handle to
  001CFBE0, 001F6760's result to 001F6640, 001F9660's key register), and rebuilds the original's
  64-bit register values where it combines them (001E4A00's colour words from four 00128250
  results, each shifted word sign-extended; 001F4E40's colour word). 001A8970's handler is the word
  at a1 +0x34. The tail jumps of 001D3AC0 / 001D3CE0 are direct calls here.
- **Arithmetic.** `em_ee_float.h`. 001A8E80's squared distance is the first product into the
  accumulator plus the second (the accumulate op); 001D66A0's two rotation seeds are an
  accumulated product plus / minus a second product (the multiply-subtract is ACC - fs * ft,
  EE_FLOAT_MODEL.md section 2); 001E4A00's colour ramps are VU0 (vector subtract, a broadcast
  multiply by u, vector add, then max with 0.0 and min with 255.0 per lane) through
  `em_vu_vec_bits` / `em_vu_max_bits` / `em_vu_min_bits`; everything else is COP1.
- **Registers the original reads without setting.** 001A8970 on one path (state 1 and opponent
  reaction 6 or 7) compares its caller's f20 with 0.8 times itself: the value comes from
  `entry_f20` (the caller's f20 bits at entry) when the binder knows it; a NULL `entry_f20` on that
  path is the new fault **UNDEFINED** (address 0x001A8AC4, the instruction that reads it). 001E4CE0
  in state 1 with a variant its second jump table does not set (0, 1, 11, 15 and up) uses its
  caller's s0 as the mesh-set pointer and s5 as the frame limit: the module does everything the
  original does before the first such read and then faults UNDEFINED with that read's address
  (0x001E537C or 0x001E53EC for s0, 0x001E555C for s5). With a handle of 0xFFFFFF and an age that
  reaches 1.8 in that call neither register is read, and the call completes as the original.
- **Fail-stop.** Unmapped address, NULL worker, negative worker result, NULL output, UNDEFINED, and
  FLOAT (an unmeasured VU0 form; the three forms used are measured, so it never fires). The latch,
  clear_fault and NULL-context rules are EXITA's.
- **Store trace.** `EM_AREA02_MISC_STORE_TRACE` (test builds only); `wr` is the only store path.

## 3. What each routine does

Full descriptions are in the comment above each translation. Short forms (c = the effect state
record D_00275C34, packet = 001CFB50(0x0081F8F0, 0, a2; f12, f13, 1.0, 1e-6, f16), submit =
001CFBE0(a0, a1, table, descriptor, t0), draw = float((seed >> 16) & 0xFFFF) / 65535 + 1e-4 and
seed' = 37 seed + 11):

- **0011C128(x) -> f0.** By |x| (the sign bit cleared, compared as a signed word): exactly 1.0:
  x * pio2_hi + x * pio2_lo; above 1.0: (x - x) / (x - x); up to 0x3EFFFFFF: x itself below
  0x32000000, else x + x * p(t) / q(t) with t = x * x; otherwise with t = (1 - 0011DF78(x)) * 0.5
  and s = 0011CB90(t): above 0x3F799999 pio2_hi - (2 (s + s p/q) - pio2_lo), else the split form
  with s truncated to its top 12 fraction bits; negated unless x is positive. p and q are the
  degree-6 / degree-4 polynomials of the listing's constants, one EE product and one EE sum per
  step. (The shape is that of a single-precision arcsine; the name is only a recognition aid.)
- **0011E520(x) -> f0.** The kernel's result unless D_0026C5D0 != -1, 0011E080(x) == 0 and
  1.0 < 0011DF78(x): then the exception record is filled, errno (the cell 0011FD78 returns) = 0x21
  when the mode is 2 or 0011DB90(record) returns 0, errno = the record's error code when that is
  non-zero, and the result is 00127758(the record's return value).
- **0015C750(a0).** +0x234 = 0, D_00810707 = 0, 0015C1F0(a0); link +0x1C non-zero: link +4 = 3,
  +0x1C = 0; 0015C7C0(a0).
- **0015C7C0(e).** Halfword e +0x20C against the nine hurt clips (each loaded when tested); the
  first match restarts its normal clip through 001749A0(e, clip, 0; 1.0), the first pair's by
  001B0070() bit 2.
- **001A8970(self, other).** The three proximity gates (|dx| <= other's extent x, |dz| <= its
  extent z, |self +0xA4 + h - other +0xB4| <= h + its extent y with h = self's extent y / 2), the
  handler other +0x34 (other, self, self +0xB0), then by the reaction byte other +0xD: 5 -> self
  +0xF = 0xA, +0x224 = +0x220, +0 = 3 and the unit direction (001028D0 / 00102760 through
  0x700038A0, 0x700038AC = 1.0); state 1 -> the f20 test for reactions 6 / 7, 0xE with 0021BD10()
  -> +0xF = 2, the blend from D_0024A7C0 or D_0024A800 (by D_0081070A) into +0x224 (and +0x22C for
  9, 0xC, 0xD), +0 = 3 and the direction; the halfword 0x70003B86 = 0 except when a gate or the f20
  test fails.
- **001A8E80(a0, a1).** Planar distance (0011E748 of the accumulated squares) <= 7.0 + a0's extent
  x and |6.0 + a1 +0xB4 - a0 +0xB4| <= 8.0: a1 +0x36 = 0x14, 0x70003B88 = 0.
- **001A9360(a0, a1).** 00183C40(a1, 0x700038B0), 001028D0, 0x700038AC = 0, 0x70003A20 =
  00102738 of 0x700038A0 with itself, 0x70003A24 = v then v * v (v = the two extents x); when
  0x70003A20 <= v * v: 001A91C0(a0, a1; 0011E748(0x70003A20)), the direction into a1 +0x70,
  0x70003B88 = 0.
- **001AA640(a0, a1).** |dx| <= 50, |dz| <= 46, |dy| <= 40 (0011DF78 each): a1 +0x36 = 1.
- **001AA700(a0).** The D_00275B84 entries of D_00275B7C with class 4, type 6 and state 1:
  001AA640(a0, entry).
- **001B6AE0(-, a1, a2) -> v0.** Area-script command 0x1A (entry 26 of ftab_0024D880): state a1 +4
  0: 001FD4C0(a2 +0x18), then a2 +8 == 0 -> +4 += 1, 00119828(0, 0, 0), 00119828(1, 0, 0),
  a2 +0x10 = 0, else +4 = 2; 1: a2 +0x10 += 1.0, past 60.0 -> +4 += 1; 2: v0 = (D_008106F4 == 1).
- **001D3A30 / 001D3C40(a0, a1), 001D3AC0 / 001D3CE0(a0).** 001D1F80(a0, 2, 1), then the 0x10-byte
  list record at the slot word D_00275670 +0x10 + 4 a0 (reloaded for each store): +3 = 0x30, +4 =
  bank + (D_00275670 +0x9C << 7) with bank 0x00816D40 / 0x00816E40, halfword +0 = 8, slot word +=
  0x10; then 001D37D0 / 001D3AD0(a0, a1, 0x30, the slot address). 001D3C40 first calls 001D3A30.
  The two thunks call them with a0 = 3.
- **001D66A0(slot, xyzw, colour a, colour b; angle) -> v0.** The 33-pair Gouraud fan packet (0x870
  bytes) at the slot word, which it advances; returns the packet + 0x10.
- **001E49F0.** Returns at once (the think pointer 001E4CE0 installs).
- **001E4A00(a0; age, size).** Only while age < 1.8: the two ramp colours and two 001CD520 sprites
  at a0 +0x30 (frame 001281C0(10 t) of D_00253C50, then the fixed descriptor; widths w and 2 w).
- **001E4CE0(a).** The burst emitter owner (header comment in the .c for all of it): state 0 picks
  the parameter table (first jump table), the 0x8000006E sound for variants 9 / 12 / 13, a +0 = 2
  or 1, the matrix setup, the block (+0 seed from 00122BB8, +8 = +0xC = 0.1), a +0x34 = 001E49F0,
  state 1 at once; state 1 sets the variant's scratch words and limit (second jump table), draws
  when 001CD070's handle is not 0xFFFFFF (+0x40000 inside the area 0x10 sub 1 box for variants 6 /
  7), eases the step, ages, and re-registers (001B1B70 or 001B17A0) or ends (a +4 = 3); states 2
  and 3 free (001AFC10).
- **001EBD20 / 001EC5F0 / 001EC9A0 / 001ECEF0 / 001EDAF0(a0, a1).** One or three packets and
  submits: 001EBD20 copies a0 to 0x70003400, adds 5.0 to 0x70003434, packet (c +0x54, c +0x5C,
  6.0), submit kind 1 with 0x002563A0 / 0x00256430 by D_00275C30 +0x38; 001EC5F0 three seeded
  passes (draw, c +4 = seed', packet (c +0x54, draw, 5.0), submit kind 0 with 0x00256A60 /
  0x00256AF0 / 0x00256B80); 001EC9A0 packet (30.0), submit kind 2 with 0x00256D30; 001ECEF0 packet
  (3.0), submit kind 1 with 0x00257090 / 0x00257120; 001EDAF0 writes the 24 fields of the records
  0x00257510 / 0x002575A0 / 0x00257630, three seeded passes (10.0) submitted with those records,
  kind 1 and t0 = 1, then c +8 eases toward 0.02 with a floor of 0.02.
- **001F4010(i, a1).** 001F2F90(a1, slot, e, i) and 001F3340(slot, e, i) with slot = 0x007709C0 +
  0x90 D_00275C40 and e = 0x0025A350 + 0x60 i; D_00275C40 += 1, wrapping to 0 at 0x80 (signed).
- **001F4E40(a0, a1, a2; f12).** The pulsing sprite: m = |((word 0x70003B68 * a2) & 0x1FF) -
  0x100|, s = (a1 +0xC * m) >> 8, a draw of 00122BB8 (bits 23..30), s = (s + ((s * draw) >> 8)) >>
  1, colour = the three channel words of a1 scaled by s / 128; 001CD520(0, 2, a0, a fixed
  descriptor, colour; f12, f12, 1.5).
- **001F6B30().** 001F6640(001F6760()).
- **001F9660(src, key).** block_copy of 0x2F0 bytes into the stack copy; with area (D_00810700 << 8)
  + D_00810701 == 0x1500: key 0xA0 -> copy +3 = 0xB, 0x9D -> 0xA; other areas: 0x72 -> 0xC, 0x6E ->
  1, 0x9D -> 9, 0x9C -> 2; a match calls 001F91C0(copy), no match returns.
- **0021BD10() -> v0.** 1 when D_008102B0 == 1 and 0021BB00(0x008102B0) returns 0, else 0.

## 4. How it was verified

`python3 tools/test_area02_misc_reference.py` (EM_TEST_FULL=1 for the sweep).

**Oracle.** Lane SYS's recording interpreter SysEE (FallEE: the measured COP1 / VU0 model) runs
the original code of each routine over captured RAM and scratchpad (`../Extermination/build/s87/
route_a02/<beat>/`, default a02_02; the a01r beats for captured records); the native module runs
over a copy of the same bytes. FallEE has no VMAXbc / VMINIbc; the test adds them through the
measured `ee_float_model.vu_max` / `vu_min` (lane HUD's mixin; 001E4A00 uses them).

**Callees.** 47 policies; the test asserts one for every direct call target of the 28 routines and
checks each against the registers the callee reads on all its paths (lane SYS RegScan).
- **Run as original** on both sides (22): 0011DF78, 0011CB90, 0011E080, 00128350, 0011FD78,
  00127758, 001B0070, 001028D0, 00102760, 0011E748, 00102738, 0011E2A8, 0011DE90, 001281C0,
  00102948, 00128250, 001029C0, 00102C58, 00102918, 00122BB8 (the LCG), 00102958, 00121870.
- **Stub** (scripted v0 / f0 / side effects, 25): the rest, including 0011DB90 (the matherr-shaped
  worker, stubbed so that both of its results and its writes to the record's return value and
  error code are reachable; its whole record is compared at the call) and 001F91C0 (its whole
  0x2F0-byte copy compared at the call). The indirect handler of 001A8970 is a stub.

**Lockstep.** A00LOW's: at every call the callee, stack pointer, compared argument registers,
float arguments, every register the native says it sets, stub pointer snapshots, and every line
either side stored to before any stub side effect. After the last store the same check, the result
(v0, or the f0 bits for 0011C128 / 0011E520), the call log, all 32 MiB and the scratchpad.

**UNDEFINED cases.** The oracle runs with the case's f20 (the fill pattern otherwise) and its
callee-saved registers at the fill pattern; for an `undef` case the test requires that the oracle
executed the named instruction, that every call before the native module's fault matched in
lockstep, and that the native module faults UNDEFINED at that address. 43 such cases (001A8970 with
a NULL `entry_f20` on the f20 path; 001E4CE0's variants 0, 1, 11 and 15 on each read path, and the
stretch path with a non-zero 0x70003A24 left in the scratchpad).

**Cases** (974 enumerated and captured; 160 random more in full mode).
- `cap`: 001AA700 with the player over all 13 a02 / a01r beats (the captured D_00275B7C tables
  hold 0 to 21 matching records), 001EBD20 / 001EC9A0 / 001EDAF0 over each beat's D_00275C34 /
  D_00275C30 records, 0015C750 / 0015C7C0 on the captured player, 001D3CE0 on three beats.
- `t`, enumerated from the code: 0011C128 over 32 edge words (zero, denormals, every range
  boundary 0x31FFFFFF..0x3F800001 both signs, 2.0, MAX, Inf, NaN) and 60 random words;
  0011E520 over mode -1 / 0 / 1 / 2, the NaN and Inf words, |x| on both sides of 1 and the stubbed
  0011DB90 returning 0, 1 or 2^32 with error codes 0, 0x22, -1 and a return value; every clip of
  0015C7C0 with the table's own values and with unique values written into the tables (an
  off-by-one table read shows), both 001B0070 bit-2 outcomes, 0015C750 with and without the link;
  001A8970's three gates each failing and passing, then every reaction (5, 6, 7, 9, 0xC, 0xD, 0xE
  and others) by state 0 / 1 / 3, both tables, f20 of -1, 1, 0, +-0.5 and the special words, the
  handler changing the reaction byte, 0021BD10 true / false; 001A8E80 / 001A9360 / 001AA640 at and
  around their limits; 001AA700 with synthetic tables (count 0, 1, 2, 6; every rejecting byte);
  001B6AE0 in every state with the timer around 60; the list writers for slots 0..3 and bank
  shifts; 001D66A0 for three slots, five angles, two geometries and a slot word not 16-aligned;
  001E4A00 at ages around 0.2 t and 1.8 and ramps outside 0..255 (both clamps); 001E4CE0 in every
  state and every variant (0..15) with both handle outcomes (including one with only the upper
  half set), ages both sides of 0.2, the D_00275C00 0x101 edge, the area box of variants 6 / 7
  (every edge and one inside / outside value per side), the frame limits 30 / 999, the ease floor
  and seeds with every high-half shape; the packet writers with both D_00275C30 flags, three seeds
  and the ease around 0.02; 001F4010 at ring 0 / 5 / 0x7E / 0x7F / 0x80 / -1; 001F4E40 over frame
  counters, three multipliers, large colour words and solved LCG draws of 0 and 0xFF; 001F6B30 with
  v0 only in the upper half; 001F9660 over both key sets and a non-matching / upper-half key.
- Exact thresholds with one ulp either side (the `ulp` cases, all in the default run): 001A8970's
  3.0 / 4.0 gates, 001A8E80's 8.0 limits, 001AA640's 50 / 46 / 40, 001B6AE0's 60.0, 001E4A00's
  1.8 and t = 0.2, 001E4CE0's 1.8 age, 0.01 step and 0.2 age, 001EDAF0's 0.02.
- `r` (full mode): 160 random perturbations of the emitter, the contact records and 0011E520.

**Stub side effects (fx).** For every case of 20 routines an `fx` and an `fx before` variant (lane
SYS rounds 1 and 5). Full: 1,678; default: a fixed sample of 50. A variant (fx or store-site) that
turns an input into an UNDEFINED one (an 001E4CE0 variant byte perturbed to 11, with the oracle shown
to execute the read) or a pointer into an unmapped address is not comparable: it is counted, not
compared (fx: none; store-site: 4 of 100 in full mode, 1 of 30 in the default run).

**Store-site variants.** A00LOW's over the store sites the run's cases reach. Full: 100 variants
(170 store sites, 98 covered by the cases); default: a sample of 30.

**Branch coverage.** Full mode asserts both outcomes of every conditional branch of the 28 except
two, UNREACHABLE with the proof in the test: 0011C128's `1.0 < x + 1e30` test for |x| below
0x32000000 (always true), and 001AA700's second zero test of the count (a repeat of the first on
the same register). The default run keeps a pinned greedy cover (`QUICK_COVER`, computed once from
the full case set with `EM_AREA02_MISC_COVER=1`) and reaches the same coverage.

**API checks (9).** NULL context (two entries), NULL output, the latch, clear_fault (also NULL), an
unmapped record (UNMAPPED with the address), a NULL worker, a worker returning -1, and the
UNDEFINED fault of 001E4CE0 through the API.

**Reused translations.** Their own tests were run this session and pass:
`test_player_heal_reference` (0015C700 / 0015C7C0), `test_player_ladder_entry_reference`
(00199FA0, its `corners` class), `test_item_trail_reference` (001B62C0 through the stick
samples), `test_stream_lanes_reference` (001FB0B0), `test_area11_sfx_reference` (001FC520).

**Timings (M1, 4 workers, other lanes running; two measurements each).** Default: 266 of 974
cases, 5.2 to 5.7 s user, 5.8 to 6.0 s wall. Full: 1,134 cases, 45 to 49 s user, 21 to 26 s wall.

## 5. Mutants

One bounded sweep (a scratch generator, not kept; each mutant compiled from its own copy of the
source with `EM_AREA02_MISC_SOURCE`, the copies deleted afterwards): single-operation mutants of
the routine bodies drawn from `==` / `!=`, EE lt / le, add / sub, mul / div, store widths, byte /
halfword loads widened to words, `+ 1` -> `+ 2`, `<` -> `<=`, a dropped store or call, and hex
constants with the low bit flipped.

| Round | Mutants | Killed (default) | Survived | Did not compile |
|---|---:|---:|---:|---:|
| seed 7, first run | 48 | 41 | 6 | 1 |
| seed 7, after the new cases | 48 | 46 | 1 | 1 |
| seed 2026 | 48 | 41 | 3 | 4 |
| seed 2026 survivor m43, after its new case | 1 | 1 | 0 | 0 |

What closed the seed-7 survivors: m13 (001A8970's 0.8 * f20 made a division) needed f20 = +-0.5
(the f20 values -1, 1 and 0 give the same outcome both ways); m19 (the table path's state store
widened) and m35 (D_0081070A read one byte higher) needed non-zero neighbours (self +1 and
D_0081070B); m32 (001E4CE0's second variant read widened) needed a non-zero emitter +0xE; m25 (the
parameter table entry of variant 6) was killed only by the full run and is now pinned (every
state-0 case of 001E4CE0 is in the default run). Seed 2026: m43 (001A8E80's dz as a sum) needed a
self position off the origin (`t 1A8E80 self at`, pinned).

Equivalent (3, by the EE sum's operand trim, which clears the low d - 1 bits of the operand with the
smaller exponent before adding; a 40,000-input search over 0011C128's whole finite domain between
0x32000000 and 1.0, both signs, found no input that separates any of them):
- seed 7 m05: pio2_lo's last bit in the split form. The term pio2_lo - 2c is always added to 2 s p/q,
  whose exponent is far above pio2_lo's, so that bit is trimmed before it can reach the result.
- seed 2026 m27 / m35: the last bits of the coefficients pS5 and pS3. Each is scaled by t <= 0.25
  and added to a larger coefficient before it reaches the result, and the trim clears it.

With the pins every non-equivalent mutant of both rounds is killed by the default run.

## 6. Findings (decomp side; no decomp file was edited)

- **0011E520, NEARMISS C: the error path returns the wrong value.** When the record is filled, the
  original returns 00127758(record +0x18) (the float of the return value the matherr-shaped worker
  may have written), not the kernel's result: after the errno stores it branches to the epilogue
  with f0 from 00127758 untouched. The C returns `saved`. The oracle shows it (`t 11E520 x3f800001
  m1 mv0x1 e34 r3ff0000000000000`: the original returns 1.0, the kernel's result is 0x7F7FFFFF, the
  EE quotient of 0 / 0; with the record's return value left at 0 the original returns 0.0).
- **001EDAF0, NEARMISS C.** The three 001CFBE0 calls pass t0 = 1 (the fifth argument is a copy of
  a1), not 0.
- **001D66A0, NEARMISS C.** The second rotation seed is z sn sin(step) - cos(step) (w cs) (the
  accumulator minus the product); the C has the opposite sign. The result is the packet start + 0x10,
  not the end of the vertex list. The header stores reload the slot word before each store, and
  +0x10..+0x1F is one quadword clear with the low address bits ignored (the C writes three words at
  +0x10). The C's comment that the raw packet geometry is checked by `test_item_trail_reference`
  is true only for the call pattern that test drives (w = 28, the stick glow).
- **001A9360, NEARMISS C.** 0011E748 is called with f12 = the value 0x70003A20 holds (read back just
  before the test), and 001A91C0 receives its result in f12; the C declares both without that
  argument.
- **001E4CE0, NEARMISS C.** 001CD2B0 takes only f12..f15 (the C passes the variant as a first
  argument, which the callee never reads). The C's `kind` variable is overwritten by the area key
  for variants 6 / 7; no effect on the original's calls. The uninitialised `set` / `limit` for
  variants 0, 1, 11 and 15 and up are the original's (section 2), not a decompilation gap.
- **001E4A00, NEARMISS C.** The ramps are VU0 vector operations with VU clamping semantics; the C
  writes them as COP1 scalar arithmetic with C comparisons (same shape, different arithmetic).
  The colour words are 64-bit register values (each shifted channel sign-extended), not 32-bit
  unsigned ORs; the difference reaches 001CD520 as t0's upper half when the alpha channel is
  0x80 or more.
- **0021BD10, C linked from asm.** The C agrees with the instructions (no difference found).
- **Callers and tables** (from the ELF, for the binding): 001B6AE0 is entry 0x1A of the area-script
  table ftab_0024D880 (word at 0x0024D8E8); 001E4CE0 is referenced from six table words
  (0x0025802C, 0x0025856C, 0x002588CC, 0x00258C5C, 0x0025904C, 0x0025907C); 001EBD20 / 001EC5F0 /
  001EC9A0 / 001ECEF0 / 001EDAF0 from the table words 0x0025549C / 0x0025555C / 0x00255494 /
  0x002554BC / 0x002554E4; 0011E520, 001AA700 and 001F6B30 have no caller in the boot ELF (the
  census saw them run from overlay code; the port's AREA01 overlay translation calls 0011E520 from
  `em_area01_overlay_826d40.c`).

## 7. Binding, overlaps and reuse

Nothing is wired. Where each routine would bind (all through an adapter from the caller's worker
ABI to this module's region model, as EXITA's):

- **Collision passes.** `em_coll_list_passes.c` calls 001A8970 (001A8BE0 type 5), 001A8E80
  (001A9000), 001A9360 (001A97B0) and 0021BD10 (001A8660) through its workers, bound today to
  `em_coll_list_passes_unported*` in `em_collision_world.c`. These translations are those workers.
  001A8970's `entry_f20` must come from the translated caller chain (001A8BE0 and its callers keep
  f20 unchanged or not: not measured here); until then leave it NULL, which faults UNDEFINED only
  on the reaction-6 / 7 path.
- **AREA01 overlay.** `em_area01_overlay.h`'s `w_0011E520` (and `em_security_gun_rest.c`'s worker
  of the same address) can be backed by `em_area02_misc_0011E520`. It needs the four soft-float
  workers (`em_sdk_soft_float.c`: 00128350, 0011DB90, 00127758; 0011FD78) and 0011E080 / 0011DF78
  / 0011CB90 from the SDK math modules.
- **Status pages.** `em_status_pages_live.c` refuses 0015C750 ("not bound"); this is it. Its
  0015C7C0 is the standalone translation here; `em_player_0015C700` keeps its own inline copy for
  the heal path.
- **Area script.** Command 0x1A (001B6AE0) is not in `em_area_script.c`'s command set and faults
  today; this is its handler.
- **Equipment.** `em_equipment_live.c`'s `x_001F4010` returns -1 (fail-stop); this translation can
  back `w_001F4010` of `em_player_equipment.c`.
- **Script actors.** `em_area11_roger.c` / `em_startup_load_gaps.c` leave 001BB0E0's command 6
  (001F9660) unbound; this is it.
- **Render / effects.** 001D3AC0 / 001D3CE0 are called by 001CAE40 (three call sites), 001D66A0 by 00208AB0 (the item
  page), 001E4A00 by 001E4CE0, 001F4E40 by 001C4CB0 (three call sites); the effect owners
  and hud objects are table-dispatched (section 6). `em_item_trail.c`'s fan is a specialised model
  of 001D66A0 for the stick glow (w = 28, triangles instead of packet bytes); this module has the
  general routine.
- **Reused here:** `em_player_0015C700`, `em_player_ladder_00199FA0`, `em_item_stick_sample`
  (001B62C0), `em_stream_lanes_001FB0B0`, `em_sfx_service_release` (001FC520).

## 8. Known gaps

- **Inputs.** Captured records exist only for the contact table (001AA700), the effect state
  records and the player; the captures are end-of-beat images, and no capture holds a live burst
  emitter, a live 001A8970 contact pair or a 0011E520 error-path call. Those cases are synthetic
  records written into zeroed RAM of the a02_02 image.
- **Callee side.** Every stub (001CFB50 / 001CFBE0, 001CD520 / 001CD070 / 001CD2B0, 001D1F80 /
  001D37D0 / 001D3AD0, 001EFD20, 001B1B70 / 001B17A0 / 001AFC10, 0015C1F0, 001749A0, 0021BB00,
  00183C40, 001A91C0, 001FD4C0, 00119828, 001F2F90 / 001F3340, 001F6760 / 001F6640, 001F91C0,
  0011DB90, the 001A8970 handler) is verified at its call only; its effects on later loads are
  covered by the fx side effects.
- **Aliasing.** Records overlapping the scratch blocks the routines write (0x700038A0..0x700038BF,
  0x70003A20..0x70003A37, 0x70003400..0x7000343F, 0x70003B86 / 0x70003B88) or each other are not
  tested. The module keeps the original's load / store order.
- **Alignment.** The EE raises an address error on a misaligned halfword / word / doubleword
  access; the module does not model it (001D66A0's doubleword stores at the packet +0x20 / +0x28,
  001E4CE0's at set +0x70, and the table loads assume the original's aligned addresses).
- **Registers.** 001A8970's f20 source for the port is open (section 7). 001E4CE0's UNDEFINED
  variants are refused, not reproduced.
- **Pointers above 0x7FFFFFFF.** As EXITA: outside the memory model; the module faults UNMAPPED.

## 9. Reproduce

macOS arm64, port repo root:

```
python3 tools/test_area02_misc_reference.py                     # default (~6 s CPU)
EM_TEST_FULL=1 python3 tools/test_area02_misc_reference.py      # full sweep, coverage asserted
EM_AREA02_MISC_GAPS=1 python3 tools/test_area02_misc_reference.py     # list missed outcomes
EM_TEST_FULL=1 EM_AREA02_MISC_COVER=1 python3 tools/test_area02_misc_reference.py   # recompute QUICK_COVER
python3 tools/check_no_disassembly.py src/game/em_area02_misc.c src/game/em_area02_misc.h \
    tools/test_area02_misc_reference.py docs/AREA02_MISC.md
```
