# AREA00 lane A00HUD: the hud_objects and weapon_equip delta

Level 3 (AREA00) side track, lane A00HUD, 2026-09-28 (session s88). New files only:
`src/game/em_area00_hud.{c,h}` (prefix `em_area00_hud_`) and
`tools/test_area00_hud_reference.py`. The module is built and tested but not wired:
nothing in the port calls it, the Makefile does not build it, and wiring waits for the
AREA00 load (section 7).

## 1. Scope

The rows of `../Extermination/build/s87/census/a00_delta.json` (`new_functions`) in the
census subsystems hud_objects and weapon_equip: 21 functions, 11,496 bytes, all boot. The
subsystem names are census labels, not evidence; the functions are named by address and
the table says what the code does.

| Function | Bytes | Decomp | Census subsystem | First beat | What the original does | Here |
|---|---:|---|---|---|---|---|
| 001DEDB0 | 40 | asm (words) | weapon_equip | a00_09 | render context + 0x2490 (a0 == 9) or + 0x2470 | translated |
| 001DEE80 | 52 | asm (inline) | weapon_equip | a00_09 | 001DEDB0(a0), three words a1 -> record +0x10.. | translated |
| 001DEEC0 | 32 | asm (inline) | weapon_equip | a00_09 | 001DEDB0(a0), record +4 = a1 | translated |
| 001DF020 | 228 | byte-matched | weapon_equip | a00_09 | stack templates, slot word, seven slot-reset calls | translated |
| 001DF110 | 100 | byte-matched | weapon_equip | a00_09 | 001DF020(3, a0), a header at the cursor, 001CB760 | translated |
| 001DF180 | 1052 | NEARMISS | weapon_equip | a00_09 | 16 x 16 jittered grid, 15 packet rows | translated |
| 001DF5A0 | 96 | C linked from asm | weapon_equip | a00_09 | 001DF180(3; f12 passed through), header, 001CB760 | translated |
| 001E2800 | 928 | NEARMISS | weapon_equip | a00_10 exit | VU0 projection of two points, depth fade, one packet | translated |
| 001E2BA0 | 728 | NEARMISS | weapon_equip | a00_10 exit | 32-step shaded line through 001E2800, VU0 clip per step | translated |
| 001E2E80 | 1944 | byte-matched | weapon_equip | a00_02 | pool owner: moving hazard, probes, damage table | translated |
| 001E8E80 | 1012 | NEARMISS | hud_objects | arrival | 8 x 8 lattice record set-up | **reused**: em_area01_exitb.c |
| 001E9280 | 756 | NEARMISS | hud_objects | arrival | packet build from that record | **reused**: em_area01_exitb.c |
| 001EAB50 | 412 | byte-matched | hud_objects | a00_03 | effect subtype 0 handler | translated |
| 001EB980 | 156 | byte-matched | hud_objects | a00_00 | effect subtype 0x27 handler | translated |
| 001EBBB0 | 124 | NEARMISS | hud_objects | a00_10 exit | effect subtype 7 handler | translated |
| 001EBC30 | 228 | NEARMISS | hud_objects | a00_09 | effect subtype 8 handler | translated |
| 001ECB00 | 872 | NEARMISS | hud_objects | a00_03 | effect subtype 0xF handler | translated |
| 001ECFB0 | 332 | NEARMISS | hud_objects | a00_09 | effect subtype 3 handler | translated |
| 001ED7A0 | 844 | NEARMISS | hud_objects | a00_09 | effect subtype 4 handler | translated |
| 001EEBA0 | 780 | NEARMISS | hud_objects | a00_02 | effect subtype 0x19 handler | translated |
| 001EEEB0 | 780 | NEARMISS | hud_objects | a00_03 | effect subtype 0x1A handler | translated |

19 functions (9,728 bytes) are translated here.

**Existing port code (grep of `src/game`, `tools`, `docs` for every address before
translating):**
- **001E8E80, 001E9280: reused, not translated again.** Lane EXITB's `em_area01_exitb.c`
  translates both (entries `em_area01_exitb_001E8E80` / `_001E9280`), verified by its oracle
  over the a01_07 capture, which is the AREA00 arrival state (AREA01_EXITB.md). This lane
  adds a reuse check (section 4): lane EXITB's own oracle cases for 001E9280 run over the
  AREA00 captures a00_02, a00_06 and a00_09 (the 0015AB00 records AREA00 keeps); 24 cases,
  all equal.
- 001DEDB0 / 001DEE80 / 001DEEC0 are written **inline** inside em_render_context.c's
  001DEDE0 translation (for flag 2 then 9, verified as part of 001DEDE0 in
  test_render_context_reference.py). That is not a callable translation of the three
  entries with arbitrary arguments (001E7310 / 001E7440 / 001E7570 call them directly), so
  they are translated here as entries; the inline form is not changed.
- 001DF110 is a worker (`w_001DF110`) of em_render_context.c (bound to a fault /
  unreached stub today); 001E2BA0 is a worker of em_security_gun_rest.c and is described in
  comments of em_weapon.h / em_gfx.h (claims); 001EB980 / 001EBBB0 / 001EBC30 / 001ED7A0
  are listed in EFFECT_KINDS.md / EFFECT_MANAGER.md as untranslated handlers that fault.
  None of these was a translation.
- The parallel AREA00 lanes call some of these through workers: lane A00LOW's
  `em_area00_low.c` (001E7310) calls 001DEE80, 001DEEC0 and 001DF5A0 (with f12); lane A00FX's
  `em_area00_fx_spawn.c` (001EF510) calls 001EEEB0. Neither lane's files are edited here;
  their workers can bind to the entries here (section 7).

## 2. Model

- **Memory.** Original EE memory through caller-supplied regions keyed by original
  address: RAM, the scratchpad and a stack region. The stack locals whose address the
  original hands to a callee live in the original frame layout below the entry `sp`:
  - 001DF020 (frame 0x80): the two 0x20-byte templates at +0x40 and +0x60 (001D7A80 reads
    them);
  - 001DF180 (frame 0x10F0): the 16 x 16 grid at +0xD0 and the two ST quadwords at
    +0x10D0 / +0x10E0 (00102948 copies them into the packets; float_to_int's inputs are
    re-read from them);
  - 001E2BA0 (frame 0xA0): previous RGBA +0x50, RGBA +0x60, current point +0x70, step +0x80,
    next point +0x90.
  Every other local is a C local. **Limit (as lane SYS):** the original keeps saved
  registers in its frames and the module keeps none, so an input whose records lie in the
  stack below the entry `sp` is not claimed. **The grid's w words:** 001DF180 writes x, y, z
  of each grid quadword and never w; the quadword copies carry w (whatever the stack held)
  into the packets. The module reproduces this exactly because it copies from the same
  stack region (both sides start from the same stack bytes in the test); in the GIF PACKED
  ST form that word is not used.
- **Quadword accesses** (the templates, 001E2800's colours and packet, VU0 loads / stores)
  use the address with its low four bits cleared, as the hardware does (case
  `1E2800 unaligned`).
- **VU0.** 001E2800 and 001E2BA0 work on the VU0 state in the context (`vu`: vf1..vf31,
  ACC, Q, CLIP; vf0 is the constant), writing exactly the registers the original writes:
  001E2BA0 loads vf24..vf27 (the clip matrix 001CD370(2)), vf28..vf31 (0x70003AC0) and vf23
  (context +0xA0), which 001E2800 then reads as its caller left them. VU0 registers are not
  callee-saved: the worker receives the state (`call->vu`) and a callee that uses VU0 reads
  and writes it there. **vf3.z / w:** the original moves the whole 128-bit t0 into vf3 after
  setting only its low 64 bits (1.0, 0); the upper half is the caller's, which the port does
  not model; the module writes vf3.x / y only, and only vf3.x is read (by 001E2800).
- **The clip test** (the vclipw of 001E2BA0): x, y, z against |w| on DAZ'd magnitudes, one
  flag pair per lane, shifted into the 24-bit CLIP register, as em_render_context.c. A lane
  with exponent 255 faults UNMEASURED; it cannot occur here, because every compared lane is
  the result of the clamped VMADDbc form (all operands saturated; a VU sum of finite values
  is at most +-MAX).
- **Calls.** Every call leaving the module goes through the one worker `call` with the
  callee address, the stack pointer at the call (entry `sp` less each frame: 0x10, 0x80,
  0x10F0, 0x40, 0xA0, 0x60, 0x50 / 0x20 / 0x30 for the handlers), the argument registers the
  original sets that the callee reads (a0..t3 as 64-bit images, f12..f19 as raw bits; `na` /
  `nf`), and the VU0 state. Translated callees are called directly: 001DEDB0 (from 001DEE80
  / 001DEEC0), 001DF020 (from 001DF110), 001DF180 (from 001DF5A0), 001E2800 (from 001E2BA0).
- **Arithmetic.** Every COP1 operation through `em_ee_float.h` in the original operand
  order (add, sub, mul, div, the ADDA / MADD pair of 001E2BA0's squared length, cvt, c.lt /
  c.le / c.eq); every VU0 macro instruction through its measured form (VMULAbc, VMADDAbc,
  VMADDbc, VMULQ, VSUBbc, VDIV, VMINIbc, VMAXbc, VFTOI4).
- **Integer details kept.** 001DF180's row division by 15 is the signed multiply-high
  sequence the original computes; 001E2800's fade products are 32-bit (low word of the
  product, then an arithmetic shift by 8); stub results are 64-bit register images
  (001E2E80 ORs 0019A570's / 0019AA80's whole v0 into its hit word).
- **Fail-stop.** An address outside every region, a NULL worker, a negative worker result,
  a NULL region list, or an unmeasured float form: the first fault is latched (code, entry,
  address or callee), the call returns -1, and every later call returns -1 before any work
  until `em_area00_hud_clear_fault`. A NULL context returns -1 without latching. No routine
  here reads a value it never set on any input other than the grid w words above (no
  UNDEFINED fault).
- **Store trace (test builds only).** `EM_AREA00_HUD_STORE_TRACE`; `wr` is the only store
  path.

## 3. What each routine does

Full statements are in the comment above each translation; short forms:

- **001DEDB0(a0)**: D_00275670 + 0x2490 when a0 == 9, else + 0x2470. **001DEE80(a0, a1)**:
  the three words at a1 copied one by one (load, store) to that record's +0x10..+0x18.
  **001DEEC0(a0, a1)**: record +4 = a1. Both leave the record in v0.
- **001DF020(a0, a1)**: templates D_002534C0 / D_002534E0 to frame +0x40 / +0x60; result the
  word D_00275670 + a0 * 4 + 0x10; 001D1FF0(a0, 2), 001D7510(a0, 0, 0), 001D1F80(a0, 0, 1),
  001D7000(a0, 0), 001D6DD0(a0, 0x7000, 0x7900), 001D7A80(a0, 0x40, sp+0x40, sp+0x60, a1),
  001D1F20(a0).
- **001DF110(a0)** / **001DF5A0(f12)**: 001DF020(3, a0) / 001DF180(3; the caller's f12);
  then at the context's cursor c = +0x1C (re-read before each access): byte c+3 = 0x60, word
  c+4 = 0, halfword c+0 = 0, cursor += 0x10; 001CB760(D_007635C0, 0xFFF000, the call's
  result, the first c).
- **001DF180(a0; h)**: result the slot word; 001D6B60(a0, D_0027568C, 8, 8, 0x26E860),
  001D6BA0(a0, D_0027568C, 8, 8, 0, 0), 001D1FF0(a0, 3), 001D2040(a0, 0), 001D7080(a0,
  0x80808080; 1.0). Grid point (i, j): u = i/15, v = j/15, s(t) = 0011DF78(2t - 1),
  k = (s(v)^2 + s(u)^2) * h, two LCG draws r1, r2 scaled by 2**-31; x = 2**-9 + (v + k(2 r1 -
  1)), y = 0x3B924925 + (u + k(2 r2 - 1)), z = 1. 15 rows of 0x430 bytes at the slot cursor:
  header (byte +3 = 0x10, word +4 = 0, halfword +0 = 0x42, quadword +0x10 = 0, +0x1C =
  0x50000041, +0x20 = 0x400A400000008010, +0x28 = 0x4242), then 16 x 4 quadwords: grid[i][j],
  the row's ST (0x7000 + 546.13336 per column through float_to_int, ((0xE0 i)/15 + 0x790)
  << 4, 0xFFFFFF, 0), grid[i+1][j], the next row's ST. Then 001D1F20(a0), 001D1FF0(a0, 1).
- **001E2800(mode, pa, ca, pb, cb)**: pa / pb through vf28..vf31, perspective divide, w - 1,
  z divide, the depth ramp min(vf23.z + vf23.w * w, vf23.x) then max with 0, 28.4 fixed
  point, to 0x70003600 / 0x70003610; colours to 0x70003620 / 0x70003630; for mode != 0 each
  depth word >>= 4, clamped to 0xFF, then mode 1 scales the alpha, modes 2 / 3 the three
  colour words ((c * d) >> 8), and the depth word becomes 0xFF0; packet d = 001CB5F0(chain,
  z, 4) with the four quadwords, 001CB6B0(chain, z, 2, D_00253720), 001CB900(chain, z, mode)
  (z = the word 0x70003608, re-read for each call).
- **001E2BA0(start, end, colour)**: statement in section 2 and the module comment: 32 steps
  of end - start over 32, a shade 0011DF78(0011E2A8(phase)) per step (phase from one LCG
  draw, step 0.1 * length / 4, wrapped at 2 pi), RGBA = float_to_int(255 * colour * shade),
  a segment drawn by 001E2800(2, ..) only when both of its ends pass the clip test.
- **001E2E80(e)**: states 2 / 3: 001AFC10(e); 0: set-up then 1; 1: move by b = e+0x1F0, the
  kind 3 / 4 slow-downs, the 150-frame life, the two probes 0019A570 / 0019AA80 with the
  +0x1A == 0x32 exception, the damage (kind 3: halfword +0x94 in 10..18 through the two
  9-entry tables by D_0081070A; kind 4: 0x13 / 0x14), the hit effects, the one-shot turn
  effect, 001D04B0 draws for kinds 3 / 4, and the +0x20 phase (0.05 per frame, minus 1 above
  2).
- **The nine handlers** (a0 = node + 0xD0, a1 = depth key; D_00275C34 = the work block,
  re-read wherever the original re-reads it): 001EB980 / 001EBBB0: one 001CFB50 (work +0x54,
  +0x5C, 1.0, 1e-6, 3.0) and two / one 001CFBE0. 001EBC30: the node's 0x40 bytes to
  0x70003400, float +0x34 there += 2.5, 001CFB50 on it, one 001CFBE0 whose table depends on
  D_00275C30's +0x38. 001EAB50 / 001ECB00: below 0.5 / 1.0 of work +0x54 a coloured sprite
  (001CD520, packed 0x20045B2599421E98 / 0x20045BA5154222DC, size 1 + 8x / 6 + 6x, last
  float 2.0 / 6.0), then one plain draw / three LCG draws (f16 = 6.0) and an ease of work +8
  to 0.05. 001ECFB0: three LCG draws (f16 = 3.0) over D_002571B0 + 0x90 k, ease to 0.02.
  001ED7A0 / 001EEBA0 / 001EEEB0: two or three 8-word constant blocks, three LCG draws
  (f16 = 10.0, copy flag 1), ease to 0.02 by / 10, / 10, / 8. The LCG draw: n = work +4;
  f13 = ((n >> 16) & 0xFFFF) / 65535 + 0.0001; work +4 = 37 n + 11.

## 4. How it was verified

`tools/test_area00_hud_reference.py`. Default run: about 7.6 s of CPU (user + sys) and
5.5 s wall with the default 2 worker processes. `EM_TEST_FULL=1 EM_TEST_JOBS=4`: about 74 s
of CPU, 26 s wall.

**Oracle.** FallEE (the shared interpreter with the measured COP1 / VU0 model), extended in
the test by VMAXbc / VMINIbc (the model's vu_max / vu_min), VFTOI4, the clip test and the
CLIP register read, runs the ORIGINAL instructions over the end-of-beat captures
`../Extermination/build/s87/route_a00/{a00_02_south_route, a00_06_cab_roof,
a00_09_ne_room_out, a00_10_progression_exit}/`. The native module runs over a copy of the
same bytes and a stack region with the same starting bytes (0xA5). An address outside the
32 MiB stops the oracle.

**Callees.** Run as original on both sides (the native side in a second interpreter over the
native memory and the native VU0 state): 00102948, 00102958, 001026D0, 00102760, 00102870,
001028B8, 001028D0, 00102900, 00102918, 001029C0, 00102BB0, 001031E0; 0011DF78, 0011E2A8,
0011E748, the LCG 00122BB8, float_to_int 001281C0; 001CD370, 001CD390; the chain
001CB5F0, 001CB6B0, 001CB760, 001CB900. Stubbed (recorded, scripted result): 0019A570,
0019AA80, 001AFC10, 001CD520, 001CFB50, 001CFBE0, 001D04B0, 001D1F20, 001D1F80, 001D1FF0,
001D2040, 001D6B60, 001D6BA0, 001D6DD0, 001D7000, 001D7080, 001D7510, 001D7A80, 001EFD20,
001EFEB0, 001F02C0. The test asserts every direct call target has a policy. In the oracle
the run callees keep their VU0 writes (VU0 is not callee-saved); the other registers
except v0 / v1 / f0 are restored.

**Register reads.** RegScan (imported from `test_area01_sys_reference.py`) measures what
each callee reads before writing; the test fails if a callee reads an argument register
its policy does not compare. Measured: 001D6BA0 reads a0..t1 (six), 001D6B60 / 001D7A80 /
001CFBE0 / 001CD520 a0..t0 (five), 001CD520 f12..f14, 001CFB50 f12..f16, 001D04B0 f12 /
f13, 001D7080 / 001F02C0 f12; so the decomp C's shorter prototypes of 001D6B60 / 001D6BA0
(section 6) would drop inputs.

**Lockstep.** As lanes SYS / EXITB, plus two checks: at every call leaving the module the
native side must match the callee, `sp`, every compared argument register and every
register the native says it sets, all of RAM and the scratchpad (the lines either side
stored to since the previous check), **the VU0 state** (vf1..vf31 except vf3.z / w, ACC, Q,
CLIP) and **the bytes behind every argument that points into the stack** (16 bytes, 32 for
001D7A80's two templates). After the last store: memory again, then all 32 MiB, the
scratchpad, the final VU0 state, the call log and the result.

**Cases** (329):
- context routines: 001DEDB0 with a0 = 9 and six others (8, 10, 0x109, -7..); 001DEE80 with a
  plain source and three sources overlapping the destination words (the per-word load /
  store order); 001DEEC0; 001DF020 with four slots and three a1 values; 001DF110; 001DF180
  with four slots and amplitudes 0.05, 0, 1, -0.3, 3e10; 001DF5A0;
- 001E2800: designed projections (w = z + 3) with four depth ramps (a ceiling of 1000 for
  the 0xFF clamp, a negative ceiling), all modes 0..4, random colours including 0x7FFFFFFF
  (the 32-bit product), unaligned pointers, and the captured a00_10 camera (0x70003AC0) and
  ramp (context +0xA0);
- 001E2BA0: designed clip / projection matrices (identity and perturbed) with short and
  long lines (inside, crossing and outside the clip volume; lengths that wrap the phase),
  and three over the captured matrices;
- 001E2E80: the captured records of a00_02 and a00_06, every state (0..4, 0xFF), set-up for
  kinds 3 / 4 / 5, the damage table for both D_0081070A values, kind 3 h = 9..19, kind 4 h =
  0x12..0x15, h = -1 / 0x800A / 0x7FFF, targets with byte 0 = 0 / 2 / 3, the first probe with
  +0x1A = 0x31 / 0x32 / 0x33 and results 1, -1, 0x100000000, the timers across their
  thresholds (+0x34 = 24..26, +0x38 = 0 / 1 / 150, +0x20 around 1.95 and 2), e+5 = 0 / 1;
- the nine handlers over the a00_09 work block: work +0x54 at 0, 0.3, 0.49999997, 0.5, 0.9,
  0.99999994, 1.0, 1.5, -0.2 (both sprite thresholds), work +8 at 0, 0.02, 0.05, 0.3, -1 and
  just below 0.02, seeds 0x80000000 / 0xFFFFFFFF / 0x7FFF0000 / 0xFFFF (the arithmetic
  shift), D_00275C30 +0x38 = 0 / 1;
- *sweep 1* (`cases_sweep1`, 20 cases added for the first sweep's survivors, section 5): a
  fade depth of exactly 0x100 and one above 0xFFFF; clip lanes equal to |w| and one ulp
  above it (odd mantissas); a line outside only in z-minus that then enters the clip
  volume; a lane with exponent 254; the phase reaching exactly 2 pi (the LCG seed
  0xFC77A683, whose draw is 0, and a step of 0x437B53D2, whose phase increment is exactly
  2 pi in EE arithmetic; found by running the original LCG and square root in the
  oracle); the 001DF110 cursor crossing a 64 KiB boundary; D_0027568C with bit 31 set; an
  exact 2.5 sum in 001EBC30; an LCG fraction of 0; 001E2E80's phase reaching exactly 2.0,
  its kind-4 halfword with a nonzero high byte (0x0113, 0x0114, 0x8013) and its +0x34
  counter carrying into the upper half.

**Variants** (1,873; lane EXITB's): fx after / fx before at every stub, poison, prestore,
edge-byte, edge-top. Seven variants send the original outside RAM (a complemented cursor
or pointer word) and are counted as not comparable.

**Branches.** Both outcomes of 57 of the 59 conditional branches are taken (EM_TEST_FULL
asserts it; so does the default run). The other two are `UNREACHABLE` with proof: 001E2800's
two "depth below 0" tests (the depth word is the 28.4 conversion of a lane just maximised
with +0, and the VU order puts every negative pattern below +0, so it is never negative).

**Static checks.** 001E2E80's two jump tables (D_0026E8C0 / D_0026E890) in every capture
equal the ELF and point at the nine targets in order (the translation's amount tables);
the scratch area 0x01E00000 is zero in every capture used.

**API.** NULL context, NULL regions, NULL worker, a failing worker (fault 2 naming the
callee, refusal while latched), clear_fault (also NULL), an unmapped access reported at its
address (001E2E80's state byte; 001DEDB0's first read is D_00275670) with no output
written.

**Reuse check.** Lane EXITB's oracle (`test_area01_exitb_reference.py`, imported; its build
redirected to `build/area00/hud/exitb/`) runs its 001E9280 case on each 0015AB00 record of
the a00_02, a00_06 and a00_09 captures: 24 cases, all equal.

**The default run** is the pinned base cases (each routine's representative and boundary
cases, the captured records, the sweep-1 cases) plus `QUICK_PINNED`, the 28 cases or
variants that first killed a mutant the pinned cases left alive (section 5): 146 of 329
cases and 10 of 1,873 variants, 17,341 calls compared.

**Result.** Every case and variant matches in both modes. The full run compares 124,886
calls and 4,820 stub-side field changes.

## 5. Single-operation mutants

One bounded sweep, then one fix round (this track does not loop sweeps until dry). A
scratch driver (not committed; `build/area00/hud/mutate.py`) generated every
single-operation edit of the translation bodies and constants of `em_area00_hud.c` (from
the float constants to the last handler; the plumbing and the entry wrappers excluded):
each hex / decimal literal (hex xor 1, decimal + 1), each relational operator (== / != /
< / <= / > / >=), `&&` / `||`, the EE operation (add / sub, mul / div, c.lt / c.le,
c.eq / c.lt, madd / msub, adda / suba, VMINI / VMAX, VFTOI4 / VFTOI0, VMULAbc / VMADDAbc),
the access width (u8 / s16, st8 / st16 / st32 / st64), the register image (`reg` /
zero extension), the deletion of each statement and the swap of each statement with the
next. Each mutant was built through `EM_AREA00_HUD_SOURCE` (without -Werror) and run
fail-fast on the default run, restricted by `EM_AREA00_HUD_ONLY` to the entries that reach
the edited line; four mutants at a time.

**1,678 mutants: 1,343 killed by the default run, 320 survive and are equivalent (below),
15 are not C.** Round 1 (the default run as first written): 1,282 killed, 381 survived.
The survivors that were not equivalent led to `cases_sweep1` (19 more killed); the
remaining 362 were rerun in `EM_TEST_FULL=1`, which killed 40 more, and the case or
variant that first killed each became `QUICK_PINNED`; two more cases (the kind-4 high
byte, the counter carry) killed the last two. The final default run kills all 1,343
(checked by rerunning every round-1 survivor against it). Killed by kind: literal 714 of
834, operator 54 of 61, deletion 305 of 324 (+ 14 not C), EE operation / width / register
image 159 of 246, swap 111 of 213 (+ 1 not C).

**The 320 survivors, by the rule that makes each equivalent** (list with mutant numbers:
`build/area00/hud/survivors_classified.json`, regenerated by the driver):

| Rule | Count | Why no input can tell them apart |
|---|---:|---|
| independent statements swapped | 87 | two computations on locals, two fields of the call record, two stores to distinct fields or regions (rodata, stack, packet, scratchpad), a local computation moved across a call that cannot read it, or two loads (their order matters only for which unmapped address faults first) |
| sign vs zero extension of an address below 2**31 | 74 | `reg(x)` against `(uint64_t)x` for an argument that is a mapped address (RAM, scratchpad 0x70000000.., stack 0x7F000000..): equal below 2**31. The two returned slot words (001DF020 / 001DF180) are the cursor word that 001DF110 / 001DF5A0 dereference right after, and the direct entries return 32 bits |
| quadword address low bits | 30 | a literal +1 in an address that a quadword access uses with its low four bits cleared (every such base is 16-aligned) |
| register the callee does not read | 29 | an argument slot past `na` (the LCG takes none, 001D6B60 / 001D7A80 / 001CFBE0 read five), or a register count raised to include a0 / a3 where the original happens to hold 0 there (RegScan: the callee does not read it) |
| array size in a declaration | 27 | the declared size of a local or table array |
| aliasing only | 12 | a store and a load of different fields of records reached through pointers (the header cursor, the work block, 0x70003620 against a colour pointer, the ease's pointer re-read): they differ only when the records overlap; no case aliases them |
| Q argument of a form that does not read Q | 11 | the `q` parameter of em_vu_vec_bits for VMULAbc / VMADDAbc / VMADDbc / VSUBbc |
| store of the value already there, or overwritten before any read | 11 | e.g. a halfword store at +3 whose second byte (+4) is zeroed by the next word store; the fade's `< 0` / `<= 0` (stores 0 over 0); the fade word stored as a halfword (either < 0x100 with a zero upper half or immediately overwritten by 0xFF); the target byte `| 2` / `| 3` of a byte known to be 1; the ease's `<` / `<=` (equality stores the target itself) |
| undefined C | 8 | a loop or index one past a four-lane array (VF0[4], vf[2][4], out[4]) |
| fixed values | 5 | the two ST first words run through the same input-independent sequence (0x7000, then float_to_int(x + 546.13336) 16 times, reset each row): the 546.13336 constant's last bit, a halfword store of a value below 0x10000, and exchanging or dropping the second word's recomputation change no value |
| unreachable fault path | 5 | the UNMEASURED fault address and the exponent-255 clip check (every form used is measured; every clip lane is a clamped VMADDbc result, so exponent 255 cannot occur) and the fade's `< 0` branch (UNREACHABLE, section 4) |
| distinct fixed addresses | 5 | 001ED7A0 / 001EEBA0 / 001EEEB0 store the last block word before or after reading D_00275C34, and re-read it at k = 0; 001EBC30's pointer read against the 0x70003434 store: fixed, distinct addresses |
| denormal compares as zero, dead initialisation | 4 | an amount of 1 (a denormal) compares equal to 0 in the EE compare; the amount's initial 0 is always overwritten |
| vf0 lanes never read | 3 | VF0.y / z are never read; VF0.x = 1 (a denormal) in the depth maximum still converts to 0 |
| colour OR | 3 | the colour register is ORed with 0xFFFFFFFF80000000, which sets every upper bit whatever the extension of the three words |
| a lane compared with its own magnitude | 2 | extending the clip loop to w compares |w| > |w| |
| call field already zero | 2 | `mk` zeroes the call record, so storing 0 into a[0] / a[1] or not is the same |
| bits the shift already cleared | 1 | `(s << 6) & 0xFC1`: the low six bits are already zero |
| equivalent control flow | 1 | deleting the `return` after 001AFC10 reaches the "state not 0 or 1" return |

## 6. Decomp C that differs from the original (for the lead's FINDINGS)

The translations follow the original instructions (`build/.asmnorm` listings of the pinned
ELF), checked by the oracle. Where the committed C says otherwise:

- **001ECB00** (NEARMISS): the C passes 0.0 as the fifth float (f16) of all three 001CFB50
  calls; the original passes **6.0** each time.
- **001DF180** (NEARMISS), four differences:
  1. `func_001D6B60(D_0027568C, 8, 8)`: the original's call is (a0, D_0027568C, 8, 8, t0 =
     0x26E860), and 001D6B60 reads all five registers (RegScan);
  2. `func_001D6BA0(arg0, D_0027568C, 8, 8)`: the original also sets t0 = 0 and t1 = 0,
     which 001D6BA0 reads;
  3. the grid x and y lack the original's final offsets: x = 2**-9 + (v + ...), y =
     0x3B924925 (about 1/224) + (u + ...);
  4. the header writes use one cursor read; the original re-reads the cursor word before
     each of its first four accesses (differs only when a store aliases the cursor).
- **001DF5A0** (C linked from asm): `func_001DF180(3)` is declared without the float; the
  original passes its caller's f12 through as 001DF180's amplitude (lane A00LOW's 001E7310
  calls it with 0.2 * p+0x60). The compiled C happens to leave f12 untouched too.
- **001E2BA0** (NEARMISS): the C's comments call 0011DF78 fabsf and 00102870 a divide by a
  scalar; labels only (both run as original here). The C's `len_hint` float argument is
  never read by the original (it reads no f12 before setting it).
- **001E2800** (NEARMISS): the C does not show the two stores of the projected (not yet
  divided) vector to the frame; nothing reads them. The C's `vu0_project` compares depth
  with float `>` / `<`; the original uses VMINI / VMAX, whose order differs from a float
  compare only for -0 and exponent-255 patterns.
- 001DEE80 / 001DEEC0 declare 001DEDB0 with four int arguments; it reads a0 only.
- Census labels: "hud_objects" / "weapon_equip" name render-context set-up (001DEDB0..
  001DF5A0), a line draw (001E2800 / 001E2BA0), a pool owner (001E2E80) and effect-kind
  handlers; none is HUD code as far as the instructions show.

## 7. Binding

Nothing is wired. What each entry needs when the lead binds it:

- **Effect-kind handlers.** The effect driver 001EA240 calls D_00255434[subtype * 2] with
  (node + 0xD0, depth, work = node + 0x1F0, stored in D_00275C34 first). Subtypes (read from
  the ELF's table D_00255430): 0 -> 001EAB50, 3 -> 001ECFB0, 4 -> 001ED7A0, 7 -> 001EBBB0,
  8 -> 001EBC30, 0xF -> 001ECB00, 0x19 -> 001EEBA0, 0x1A -> 001EEEB0, 0x27 -> 001EB980.
  Today em_effect_kinds_handler faults on these (EFFECT_MANAGER.md 8.2); the binder can call
  `em_area00_hud_<addr>(s, node + 0xD0, depth)` with the work block already in D_00275C34.
  Workers: 001CFB50 -> em_effect_kinds_001CFB50, 001CFBE0 -> em_head_sprite_original_001CFBE0,
  001CD520 -> em_player_equipment_001CD520, 001281C0 -> em_effect_original_float_to_int,
  00102958 -> the four-quadword copy (em_sdk_vu0 style). 001EEEB0 is also called from
  001EF510 (lane A00FX, `FX_001EEEB0`).
- **001E2E80** is a pool behaviour (entity tables at 0x257E1C and 0x258D7C hold it). Workers:
  00122BB8 -> em_random_next, the vector leaves -> em_sdk_vu0 / the math leaves, 001CD390
  (em_effect_original's context), 0019A570 / 0019AA80 (collision; lane A00LOW / A00WORLD
  name them), 001AFC10 -> em_actor_pool, 001EFD20 / 001EFEB0 / 001F02C0 (effect spawns),
  001D04B0 (the effect owner's draw). The scratchpad words 0x700031B0 / 0x700031D0 /
  0x700031D4 are the probes' outputs and must be the canonical ones.
- **001DEE80, 001DEEC0, 001DF5A0**: lane A00LOW's 001E7310 workers (`CALL2(0x001DEE80u,
  ..)`, `CALL2(0x001DEEC0u, ..)`, `FCALL1(0x001DF5A0u, f12)`). **001DF110**:
  em_render_context.c's `w_001DF110` (001DEEE0's call with record + 0x10). The 001D1F20 ..
  001D7A80 workers are render-context state setters (em_render_context / em_rcl);
  001CB5F0 / 001CB6B0 / 001CB760 / 001CB900 -> em_packet_chain_original on the context's
  chain.
- **001E2BA0**: em_security_gun_rest.c's `w_001E2BA0` (0x827274 / 0x82732C / 0x8273A0) and
  00185760. It needs the context's VU0 state; the caller's vf registers are not read, since
  001E2BA0 loads everything it uses. 001CD370 -> the context's +0x2240 + 2 * 0x40.
- The VU0 state (`s->vu`) is per context; a binder that also runs other VU0 translations
  between calls must keep one state, or accept that only vf23..vf31 carry between these two
  routines (001E2800 is only called from 001E2BA0 in the ELF).

## 8. Known gaps

- Inputs are end-of-beat captures plus edits; no AREA00 frame was replayed through the
  module. The effect handlers' work blocks and 001E2BA0's lines are constructed (no live
  node of these subtypes and no line draw is in the captures); 001E2E80 has two captured
  records.
- The stubbed callees' behaviour is outside this lane (section 4's list).
- Reads that a run callee's stores could change (001DF180's per-row re-read of D_00275670
  and of the slot cursor across the 00102948 copies, 001E2800's re-read of 0x70003608) are
  seen only where memory aliases; no aliasing case was built for them.
- vf3.z / w are not modelled (section 2). The CLIP register's upper bits (shifted in by the
  caller's earlier clip tests) are carried but only the low 12 bits are used.
- Not added to the Makefile, the suite or README (lane isolation); the lead adds a make
  target (`python3 tools/test_area00_hud_reference.py`).
