# EE float model: what the original computes

Settled 2026-09-23 by the `ee-float-model` lane. This is the reference for
every oracle and native helper that reproduces EE FPU (COP1) or VU0 macro
(COP2) arithmetic. Executable form: `tools/ee_float_model.py`. Test:
`tools/test_ee_float_model.py`. Native form: `src/game/em_ee_float.h` (§6).

## 1. How it was measured

- **Emulator and settings.** The user's PCSX2 (`../Extermination/build/startup-reference/PCSX2.app`)
  launched hidden by `../Extermination/tools/pcsx2_session.py` with save state 04. It
  loads `build/startup-reference/inis/PCSX2.ini`. Every EE/VU round, DAZ, clamp,
  recompiler and speedhack key is identical to `portable-data/inis/PCSX2.ini`; the
  two files differ only in UI, OSD and PINE settings. The GameDB
  entry for SCUS-97112 sets no round or clamp override.

  | INI key | value | governs |
  |---|---|---|
  | `FPU.Roundmode` | 3 (chop) | EE add/sub/mul/madd/msub/adda/suba/mula, cvt.s.w |
  | `FPUDiv.Roundmode` | 0 (nearest) | EE div.s |
  | `FPU.DenormalsAreZero`, `FPUDiv.DenormalsAreZero` | true | denormal operands read as 0; tiny results flush to a signed 0 |
  | `fpuOverflow` = true, `fpuExtraOverflow` = false, `fpuFullMode` = false | "normal" clamp, recompiler | EE results saturate to ±0x7F7FFFFF. Operands are not clamped. |
  | `VU0.Roundmode` | 3 (chop) | every VU0 macro op, including VDIV/VSQRT |
  | `VU0.DenormalsAreZero` | true | VU0 DAZ/FTZ |
  | `vu0Overflow` = true, `vu0ExtraOverflow`/`vu0SignOverflow`/`vu0Underflow` = false | "normal" clamp | operand clamps that depend on the instruction form (§4) |
  | `FpuMulHack`, `VuAddSubHack` | false | no special cases |

- **Instruction instances.** For each op I used one real instance in the original
  boot ELF. The instance sits inside a function listed in `../Extermination/docs/FUNCTIONS.csv`,
  and only its address was used. At a frame boundary the DebugServer did
  `write_register` on the operand FPRs/VFs/ACC/Q, `set_pc` to the instance, `step`,
  then `read_registers`. Every step was checked to land on pc+4. The EE ACC
  has no debugger register. It was therefore set with `MULA.S acc, 1.0` (0x154650)
  and read back with `MSUB.S +0, +0` (0x169524). The EE instances were:
  ADD.S 0x102A7C, SUB.S 0x102A88, MUL.S 0x102EC4, DIV.S 0x102F14, MADD.S 0x154658,
  MSUB.S 0x169524, ADDA.S 0x1286D0, SUBA.S 0x169520, MULA.S 0x154650,
  CVT.W.S 0x11C94C, CVT.S.W 0x11C958, NEG.S 0x102EAC, MOV.S 0x102DA8,
  C.EQ.S 0x10D72C, C.LT.S 0x102A64, C.LE.S 0x11D0E0.
- **VU0 forms.** In the recompiler, VU0 clamping depends on the destination
  mask and the broadcast lane. I therefore recorded **every** (op, dest, bc)
  form that occurs in the ELF's code: 78 forms, one real instance each.
  `../Extermination/tools/ee_float/vusig.py` records the list with the addresses.
- **Block execution cross-check.** Single-stepping compiles one-instruction
  blocks. To rule out block-level differences, two original routines were run
  free, with the breakpoint only at the exit, over random and special inputs:
  - `001026A0`, the 227-caller matrix×vector (VMULAx, VMADDAy, VMADDAz, VMADDw), 400 runs;
  - the NEG/ADD/MUL/DIV prefix of `00102EA8` up to `00102F24`, 400 runs.

  All 800 runs match the model applied instruction by instruction.
- **Hygiene.** Nothing was saved. The emulator was terminated after each
  battery, and `pcsx2_session` verified the source state's SHA-256 was unchanged.
  No emulator is left running.
- **Recordings.** They are machine-derived and live in ignored files under
  `../Extermination/build/startup-reference/ee_float/` (oracle input; never delete):
  - `vectors/*.jsonl`: 33,800 recorded results. SHA-256 prefixes: fpu_arith
    c9d17a40, fpu_acc b0f58b16, fpu_misc 05edd831, vu 1429aad5,
    vu_signatures 37170114.
  - `vectors/instances.json`
  - `blockcheck.json`: e6cb6b2c.

  The recorders are `battery.py`, `vusig.py` and `blockcheck.py` in
  `../Extermination/tools/ee_float/` (with `harness.py`, `scan.py`, the VU form
  fitters under `fit/`, and the differential `audit.py`).
  Re-record with the project venv: `../../../.venv/bin/python battery.py; …/python vusig.py; …/python blockcheck.py 400`.

The model reproduces **all 33,800 recorded results and all 800 block runs,
with 0 mismatches.** The test's default run takes 0.2 s.

## 2. EE FPU (COP1) rules

Values are binary32 bit patterns. "Saturate" means NaN → +0x7F7FFFFF, whatever
its sign, and ±Inf → ±0x7F7FFFFF.

| op | rule | discriminating evidence (all agree with the rule) |
|---|---|---|
| ADD.S / SUB.S | Apply DAZ to both operands, then pre-trim the operand with the smaller exponent field. At distance d = 1..24 its low d−1 bits are cleared (one guard bit is kept below the larger operand's last bit). At d ≥ 25 it becomes a signed 0. Then compute the exact sum, truncate toward 0, flush below the smallest normal to a signed 0, and saturate. With an Inf/NaN operand the result is the saturated IEEE result: max + (−Inf) = −MAX, not 0. | 2,241 + 2,241 vectors. Plain truncation of the exact sum disagrees on 603 + 640 of them, and all 1,243 follow the pre-trim. |
| MUL.S | DAZ; exact product, truncated; FTZ; saturate. 0×Inf and NaN give +MAX. | 2,329 vectors. Nearest rounding disagrees on 533. There are 162 flushed results, each with the sign of the operand-sign XOR. There are 44 denormal-operand vectors where DAZ matters. |
| DIV.S | DAZ; **round to nearest-even**; FTZ; saturate. A zero divisor gives ±MAX from the XOR of the signs, checked before NaN: NaN/0 = ±MAX and 0/0 = ±MAX. Inf/Inf and NaN give +MAX. x/Inf gives a signed 0. | 2,116 vectors. Truncation disagrees on 569. |
| MADD.S | ACC + fs×ft. The product is truncated but **not** saturated, so Inf/NaN reach the add. The sum then follows the ADD.S rule, pre-trim included. | 1,400 vectors. The pre-trim alternative disagrees on 620, a saturated product on 148, and a fused (single-rounding) madd on 656. |
| MSUB.S | **ACC − fs×ft**, with the accumulator as minuend and the same product rule. | 1,400 vectors. "product − ACC" disagrees on 1,281 and all of them are ACC − product. |
| ADDA.S / SUBA.S / MULA.S | ACC ← the ADD/SUB/MUL result, saturated. The MADD chain proved that MULA stores the saturated value. ADDA/SUBA saturation cannot be seen through the MSUB readback. | 646 / 646 / 777 |
| CVT.W.S | Truncate toward 0. If \|x\| ≥ 2³¹, or x is Inf or NaN, the result is 0x7FFFFFFF when the sign bit is clear and 0x80000000 when it is set. Denormals give 0. | 371 |
| CVT.S.W | int → float, **truncated**. | 260. Nearest rounding disagrees on 19. |
| NEG.S | Exponent-255 inputs are saturated keeping their sign, then the sign flips. Denormals are negated raw. | 47 |
| MOV.S | raw copy | 47 |
| C.EQ/LT/LE.S | DAZ, then sign-keeping saturation (NaN/Inf → ±MAX by the sign bit), then compare. As a result −0 == +0 == denormal. | 849 each |

Not present in the boot ELF or in any of the 19 overlays, so neither measured
nor modelled: SQRT.S, RSQRT.S, ABS.S, MAX.S, MIN.S, MADDA.S, MSUBA.S. The
oracles' handlers for these are unreachable. The AREA11 overlay uses only ADD,
SUB, MUL, DIV, NEG, MOV, MULA, MADD, CVT.S.W and the compares.

FCR31 flag bits (O/U/I/D), VU MAC/status flags and Q-pipeline timing were
not modelled. Only the C bit of the compares was recorded.

## 3. VU0 macro (COP2) lane rules

Lane arithmetic is truncation with DAZ and FTZ. A finite overflow gives
±MAX. There is **no pre-trim**: an exact sum truncated. With the EE pre-trim
applied instead, 591 VU add/sub lanes would disagree. There is **no result
saturation**. An unclamped Inf/NaN operand propagates:
- a NaN is quieted (bit 22 set), and the first operand's NaN wins;
- 0×Inf and Inf−Inf give 0xFFC00000.

Operand clamping, when present, maps NaN → +MAX and ±Inf → ±MAX. Which
operands are clamped depends on the form (§4).

| op | rule | evidence |
|---|---|---|
| VADD/VADDbc/VADDq | no clamps | 775 + 1,586 + 813 |
| VSUB/VSUBbc | fs and ft clamped when dest = xyzw, and in the fs==ft forms dest=w and dest=zw. Other partial masks: no clamp. | 1,095 + 818 |
| VMUL/VMULbc/VMULq | fs clamped. ft/bc/Q also clamped when dest = xyzw. | 797 + 1,465 + 1,069 |
| VMULAbc (x, y, z) | fs clamped only | 862 |
| VMADDAbc (y, z, w) | fs clamped only; ACC NaN wins over the product's | 862 |
| VMADDbc | bc = w: fs, ft and ACC clamped. bc = x, z: fs only, and the product's NaN wins over ACC's. | 990 |
| VOPMULA / VOPMSUB | no clamps; fs.yzx × ft.zxy, and VOPMSUB is ACC − product | 430 + 430 |
| VDIV | Truncated. A zero divisor gives ±MAX from the XOR of the signs. In the general form (0,0) at 0x1CFA04, a NaN operand or Inf/Inf gives +MAX. In the reciprocal forms (3,0) and (3,3), 51 instances with fs = vf0.w, a NaN divisor comes back quieted. Inf/x gives ±MAX; x/Inf gives a signed 0. | 2,318. Nearest rounding disagrees on 344. |
| VSQRT | sqrt(\|x\|), truncated. Exponent-255 inputs read as MAX. | 807. Nearest rounding disagrees on 370. |
| VMAX/VMINI | Raw sign-magnitude order with −0 below +0. No DAZ and no clamp: the operand's raw bits come back. | 488 + 616 |
| VABS | clear the sign bit, raw | 115 |
| VFTOI0/4 | truncate x·2ⁿ; saturate by sign; denormal → 0 | 115 + 115 |
| VITOF0/4 | int·2⁻ⁿ to float, truncated | 83 + 83 |

Absent from the ELF's code: the VMSUB/VMSUBbc/VMSUBA family, VMAX and VMINI
(non-bc), VADDA/VSUBA/VMULA/VMADDA (non-bc), the q forms other than VMULq and
VADDq, the I-register forms, and VRSQRT.

## 4. VU0 form table

`ee_float_model.VU_FORMS` maps (op, dest mask with x=8 y=4 z=2 w=1,
broadcast lane) to (clamp fs, clamp ft/bc/Q, clamp ACC, NaN order). There is
one entry per form found in the original. Asking for any other form raises
`UnmeasuredCase` (fail-stop). Where a recorded instance reads vf0 or the same
register twice, one flag was not separable. Every other original instance of
that form has the same register pattern, so the choice cannot change an
original result. The entries are marked "free" in the source.

## 5. Oracles and native helpers that deviate (harmonization list)

Nothing below was changed in this round, because the shared files belong to
other owners. "EE" means the translated original instruction is COP1; "VU"
means COP2. A truncated host-double formula, `trunc((double)a op b)`, is exact for
VU add/sub/mul/div/sqrt on finite values when the exponent distance is ≤ 53.
Except for FTZ, it is therefore correct for VU and wrong for EE add/sub.
A differential run over 20,000 random pairs (`../Extermination/tools/ee_float/audit.py`)
measured:
- truncated add/sub vs EE: 27% wrong (5,486 and 5,342 of 20,000);
- RN add/sub/mul vs EE: 22% / 21% / 52% wrong;
- truncated div vs EE div.s: 50% wrong;
- `em_pose_math.h`: exact for finite normal operands and results.

### 5a. Shared oracle infrastructure (report only; the lead harmonizes)

Line numbers in this section refer to the files at commit 5c6a6d7-era HEAD
(2026-09-23); anchor on the named function when a file has moved.

1. **`tools/test_point_light_reference.py` `Oracle.plain`**, COP1 lines 165–168.
   - `add.s`/`sub.s` = `fp(x±y)` (no pre-trim). Change it to `ee_float_model.ee_add/ee_sub`,
     or the identical `test_pose_transition_reference.add`.
   - `mul.s` ok.
   - `div.s` default `x/y` (RN) is right for finite values. Keep the
     `truncate_ee_division=True` run: it is a negative control. The test asserts that
     only the round-to-nearest colour equals the captured original RAM at 0x2fe060
     (`captured_player_color_distinguishes_ee_division_rounding`), which is
     independent capture evidence for the DIV.S rule.
   - Add zero-divisor ±MAX and saturation.

   The VU lines 90–118 are correct for finite values. Two fixes: VDIV's zero divisor at
   line 111 must be ±MAX from the sign XOR (currently always +MAX), and
   Inf/NaN handling must follow `vu_lane`/`VU_FORMS`. Every subclass inherits
   this, including ScanOracle, OwnerOracle, DoorOracle, SdkOriginal, TaskOracle
   and actor_pool. The exceptions are the classes that override add/sub:
   `TruckOracle`, `FanOracle`, `DirectorOracle` and `test_player_random.SdkOriginal`,
   which already use the pre-trim.
2. **`tools/test_interaction_scan_reference.py` `ScanOracle.plain`**, lines 37–43.
   - ADDA.S (fn 24) = `fp(x+y)` has no pre-trim: use `ee_adda`.
   - MADD.S (fn 28) = `fp(acc + fp(x*y))` has no pre-trim, and an overflowed product
     raises (struct.pack) instead of passing Inf to a saturated sum: use `ee_madd`.
   - MULA ok.
3. **`tools/test_item_sdk_math_reference.py` `Original.plain`**.
   - Line 145–148: MSUB.S = `product − ACC` is **wrong**: use `ee_msub(acc, fs, ft)`,
     which is ACC − fs·ft.
   - Line 141–144: `int(value)` cvt.w.s neither saturates nor handles Inf/NaN:
     use `ee_cvt_w_s`. Accepting fn 13 (TRUNC.W.S, not an EE op) should be dropped.
4. **`tools/test_player_slide_reference.py` `EE.cop1`**, lines 384–404. It is the root
   of the reversal, floor, probe and footstep oracles.
   - `add.s`/`sub.s` have no pre-trim (lines 384–385).
   - `div.s` is truncated (line 387): it must be RN.
   - MADD/MSUB/ADDA/SUBA have no pre-trim (lines 394–401). MSUB orientation is already right.
   - `flt()` already reads denormals as signed zeros, but it reads exponent-255
     patterns as finite values near 2^128 instead of sign-keeping ±MAX, so
     C.EQ(0x7F800000, 0x7F7FFFFF) gives 0 where the EE gives 1 (and arithmetic on
     such operands differs too): use `ee_c_*` and `ee_*`.
   - The `fp()` NaN assertion is fine as fail-stop.

### 5b. Other oracles (their own interpreters)

| file:line | deviation | harmonization |
|---|---|---|
| test_camera_probe_reference.py:83–91, test_camera_retarget_reference.py:66–74 | add/sub no pre-trim; **div truncated**; MADD `truncate(acc+x*y)` skips the product truncation and the pre-trim | ee_add/ee_sub/ee_div/ee_madd/ee_adda |
| test_weather_reference.py:148–155 | add/sub no pre-trim; **div truncated**; cvt.w.s `int(x)` unsaturated | ee_* ; ee_cvt_w_s |
| test_snow_tiles_reference.py:119–126 | add/sub no pre-trim; cvt.w.s unsaturated (div RN ok) | ee_add/ee_sub; ee_cvt_w_s |
| test_player_motor_reference.py:76–79, test_player_heading_reference.py:74–76 | add/sub no pre-trim; **div truncated** | ee_add/ee_sub/ee_div |
| test_player_reentry_reference.py:68–71 | add/sub no pre-trim; **div truncated** (root of interaction_animation, pose_transition bases) | ee_* |
| test_opening_face_reference.py:86–89 (COP1 part) | **round-to-nearest** add/sub/mul | ee_add/ee_sub/ee_mul (div RN ok) |
| test_player_face_host.py:25 | **round-to-nearest** add/sub/mul | ee_add/ee_sub/ee_mul |
| test_item_trail_reference.py:72–74 | MSUB = **product − ACC**; MADD no pre-trim | ee_msub / ee_madd |
| test_shadow_original_reference.py:213–216 | ADDA/MADD/MSUB no pre-trim (orientation ok) | ee_adda/ee_madd/ee_msub |
| test_player_floor_reference.py:161–167 | MADD no pre-trim | ee_madd |
| test_elevator_reference.py:103 | cvt.s.w `float(int)` is **nearest** (EE truncates; differs above 2²⁴) | ee_cvt_s_w |
| test_area_script_reference.py:118–121 | cvt.w.s ok; fp() adds at lines 370/482 have no pre-trim if they stand for add.s | ee_add |
| test_pose_transition_reference.py `add`/`rounded` | matches the model for finite normal values (0/20,000 differences); lacks DAZ/FTZ/saturation; `bits(a/b)` div ok | optional: route to ee_float_model |

### 5c. Native helpers (src/)

The count of EE add.s/sub.s comes from the original functions each module
cites. A module whose cited functions contain COP1 add.s/sub.s needs the
pre-trim wherever its `add`/`sub` translates those instructions.

| helper (file:line) | formula | deviation | harmonization |
|---|---|---|---|
| `em_pose_math.h` pose_add/sub/mul/madd/msub (shared) | pre-trim + truncation | exact for finite normals. It lacks DAZ, FTZ (random wide-range mul: 629/20,000 denormal results kept) and Inf/NaN saturation. | add DAZ on inputs; flush tiny results to a signed 0; saturate |
| `em_pose_math.h` pose_div (shared) | `(float)(a/b)` | RN ok; b = 0 gives ±Inf where the original gives ±MAX; overflow gives Inf instead of MAX | zero divisor → ±MAX by sign XOR; saturate |
| `em_effect_color.h` em_effect_float32 (shared) | truncated double | correct for mul; for the EE add.s in 001F54E0/001D8C30 (−127+254r, 127+c·x, x−127, 128+d) it has no pre-trim | use a pose_add-style pre-trim add for those four sums |
| `em_item_sdk_math.c`:21–34 (shared) add/subtract | truncated double | its cited SDK bodies are EE (add.s 18, sub.s 37): no pre-trim | pre-trim add/sub |
| `em_player_slide.c`:13–16, `em_player_climb.c`:14–17 | f32_add/sub truncated; **f32_div truncated** | EE sites (add.s 42/61, div.s 10/21) | pre-trim add/sub; **RN div** |
| `em_actor_collision.c`:19–38 | ee_add/sub truncated, **ee_div truncated**, tiny → +0 (unsigned) | EE add.s 32 / sub.s 29 / div.s 8 in the cited bodies. Its `vu_dot` (00102738) is VU, so the no-trim formula is right there. | split: EE pre-trim add/sub, RN div, signed FTZ; keep the VU dot on plain truncation |
| `em_collision.c`:42, 931–952 face_float/grid_f/grid_div | truncated; grid_div truncated | EE add.s 7 / sub.s 6 / div.s 3 | pre-trim add/sub; RN div |
| `em_player_floor.c`:252–254 | truncated add/sub | EE add.s 40 / sub.s 19 (and VU ops) | pre-trim for the add.s/sub.s sites only |
| `em_interaction_scan.c`:82–85 | truncated add/sub; divide RN ok | EE add/sub sites (atan reduction) | pre-trim add/sub |
| `em_crate_original.c`:11–13 (+ `em_drum_original.c`:11–13) | truncated | EE add.s/sub.s 3+3 besides the VU work | pre-trim at the add.s/sub.s sites |
| `em_load_veil.c`:36–37 | `pose_scalar(a+b)` (no pre-trim) | EE add.s 8, sub.s 5 | pose_add |
| `em_snow_projection.c`:8–16, `em_point_light.c`:39–47, `em_snow.c`:7–24 | truncated add | mostly VU (correct); the few EE add.s sites (2 / 7 / 1) need the pre-trim | per-site split |
| `em_roger.c`:117, `em_door_candidate.c`:5–7, `em_door_transit.c`:7–20, `em_pickup_motion.c`:5–7, `em_cinematic_playback.c`:8–12, `em_item_device.c`, `em_item_geometry.c`, `em_item_trail.c`, `em_lighting.c`, `em_status_draw.c`, `em_player_motor.c`:7 | truncated add/sub (pickup_motion/cinematic divide RN ok) | EE vs VU not established (no cited function found) | check each site's original instruction; EE add/sub → pre-trim |
| `em_truck_original.c`:22–28, `em_director_original.c`, `em_fan_original.c`, `em_area_script.c` (pose_*) | pose_* for EE, truncation for VU | consistent with the model for finite values | none beyond the em_pose_math.h items |
| `em_snow_particles.c`:24–40 (VU1 microcode) | truncation | VU1 was **not** measured (the DebugServer drives only the EE). VU1 has the same INI settings as VU0 (Roundmode 3, DAZ, overflow clamp). | none until VU1 can be measured |

**Binding note for the lead.** Each harmonization changes an oracle and
its native twin together. A test that passes today may be passing because
both sides share the same wrong model, so harmonize the pair in one commit
and re-run the affected reference test plus the route captures. The single
shared fix with the widest reach is `Oracle.plain` in
test_point_light_reference.py (pre-trim for COP1 add/sub).

## 6. Native header

`src/game/em_ee_float.h` is the C form of this model. **Every new native
translation of original COP1 or VU0-macro arithmetic uses it**, not local
float helpers. The §5c harmonizations should move modules onto it, each
together with its oracle twin (see the binding note above).

- **Exactness.** All arithmetic is on integers (significands in `uint64_t`),
  and no host float operation is performed. The host rounding mode, FTZ/DAZ
  state and FMA contraction therefore cannot change a result. The test
  disassembles the compiled shim and fails on any host FP instruction that
  rounds. Compares are allowed: clang turns the raw-bit NaN tests into an
  unordered `fcmp`, which is exact in every mode. The header also compiles
  clean under `-std=c11 -Wall -Wextra -Wpedantic -Wconversion
  -Wsign-conversion -Wshadow -Werror` with clang and GCC 14.
- **Where it leaves the model's big integers.** Each change is exact for the
  reason given:
  - *Sum.* The smaller operand is aligned exactly up to an exponent distance
    of 39. Beyond that it becomes a sticky 1. At any distance of 25 or more
    it cannot carry into the kept bits, and it only borrows one unit from
    them.
  - *Quotient.* It is 39 bits wide, and the remainder becomes the sticky bit,
    which decides nearest-even exactly as the 50-bit quotient does.
  - *VSQRT.* It takes the root of m·2³⁸, which has at least 31 bits, so
    truncating it to 24 bits equals truncating the exact root.
- **API.** A name ending in `_bits` works on raw `uint32_t` binary32
  patterns. The same name without the suffix is the float form, which
  converts with `memcpy`. Integer words are `uint32_t` in the `_bits` form and
  `int32_t` in the float form.

  | model | header |
  |---|---|
  | `ee_add/sub/mul/div` | `em_ee_add/sub/mul/div[_bits](fs, ft)` |
  | `ee_madd/ee_msub(acc, fs, ft)` | `em_ee_madd/msub[_bits](acc, fs, ft)`. MSUB is ACC − fs·ft. |
  | `ee_adda/suba/mula` | `em_ee_adda/suba/mula[_bits](fs, ft)`, which return the new ACC |
  | `ee_neg/mov/cvt_w_s/cvt_s_w`, `ee_c_eq/lt/le` | the same names with `em_` (the compares return 0 or 1) |
  | `VU_FORMS` + `vu_lane` | `em_vu_form_lookup` then `em_vu_form_lane_bits`; `em_vu_lane[_bits]`; `em_vu_vec[_bits]` for a whole register (broadcast, Q, the VOPMULA/VOPMSUB swizzle, and the dest mask, where unwritten lanes keep their value) |
  | `vu_div` | `em_vu_div[_bits](fs.fsf, ft.ftf, fsf, ftf, &q)` |
  | `vu_sqrt`, `vu_ftoi(·, 0/4)`, `vu_itof(·, 0/4)`, `vu_max/min`, `vu_abs` | `em_vu_sqrt`, `em_vu_ftoi0/4`, `em_vu_itof0/4`, `em_vu_max/min`, `em_vu_abs` |

  The ops are enumerated as `em_vu_op` (`EM_VU_ADD` … `EM_VU_OPMSUB`), and a
  non-broadcast form passes `EM_VU_NO_BC` as its bc.
- **Unmeasured forms fail.** The header's counterpart of `UnmeasuredCase` is a
  status code. A form the original never executes returns
  `EM_EE_FLOAT_UNMEASURED` (−1) and writes nothing; this applies to the
  (op, dest, bc) forms and to the VDIV (fsf, ftf) forms. `em_vu_vec` returns
  `EM_EE_FLOAT_NO_OPERAND` (−2) when it is given a NULL array that the op
  reads. A caller treats any nonzero status as a fault (fail-stop).
- **Not provided,** because nothing was measured (§2, §3):
  - EE: SQRT.S, RSQRT.S, ABS.S, MAX.S, MIN.S, MADDA.S and MSUBA.S;
  - VU: VRSQRT and the other VU forms absent from the ELF, plus VFTOI12/15
    and VITOF12/15;
  - FCR31 and MAC flags, and Q timing.
- **Float return values** keep every bit on x86-64 and arm64. On a 32-bit x87
  host, use `_bits` wherever a signalling NaN may pass through a return value.
- **Test.** `python3 tools/test_ee_float_header.py` (`make
  test-ee-float-header`) generates a ctypes shim under `build/ee_float_header/`
  and builds it with UBSan in trap mode. It then checks:
  - all 33,800 recordings, through both the `_bits` and the float API;
  - the 800 free-running block runs;
  - the form table against `VU_FORMS` / `VU_DIV_FORMS` for every
    (op, dest, bc) and (fsf, ftf) pair;
  - that refused calls leave their outputs untouched;
  - a seeded random differential against `ee_float_model.py`, which includes
    constructed near-midpoint quotients, near-square roots, signed-zero
    pairs, and FTZ and saturation partners.

  It reports 105,000 random cases in about 1 s by default, and 4.2 M in about
  4 s with `EM_TEST_FULL=1`. Missing recordings are a hard failure.
