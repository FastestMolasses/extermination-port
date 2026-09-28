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
  has no debugger register. It was therefore set by an accumulator product
  with 1.0 at 0x154650 and read back by an accumulator subtraction of +0 times
  +0 at 0x169524. The EE instances were:
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

## 5. Oracles and translations on the model (harmonized 2026-09-27)

Every original-instruction oracle now executes COP1 and VU0 macro
arithmetic on this model, and every native translation that differed from
its oracle after the move was fixed through `em_ee_float.h` (chain C8,
"EE-float harmonization of older oracles"). "EE" below means the original
instruction is COP1; "VU" means a VU0 macro (COP2) instruction.

**Why the old host formulas were wrong.** A differential run over 20,000
random pairs (`../Extermination/tools/ee_float/audit.py`) measured:
- truncated add/sub vs EE: 27% wrong (5,486 and 5,342 of 20,000);
- RN add/sub/mul vs EE: 22% / 21% / 52% wrong;
- truncated div vs EE div.s: 50% wrong.

A truncated host double, `trunc((double)a op b)`, is exact for VU mul, div
and sqrt on finite values. For VU add/sub it is exact only when the signs
agree or the exponent distance is at most 29: with a tiny opposite-sign
addend further below, the double sum rounds back to the larger operand
before the truncation, one ULP too large in magnitude. The old shared VU0
interpreter (`Oracle.macro`) differed from `vu_lane` in 55 VMADDbc lanes of a
3,000-trial-per-form sweep over ordinary magnitudes (exponents -20..20),
all of this kind, and raised on overflow instead of saturating.

**How the inventory was taken.** A probe (session scratch, not committed)
imported every `tools/*.py`, instantiated each interpreter class and ran
real COP1 words (ADD/SUB/MUL/DIV.S, ADDA/SUBA/MULA.S read back through a
MADD of +0 x +0, MADD/MSUB.S, CVT.S.W / CVT.W.S, C.EQ/LT/LE.S) through the
class's own dispatcher over 200 operand pairs chosen to exercise the
pre-trim, and compared every result with `ee_float_model.py`. Before the
step 67 classes disagreed; after it all 116 agree. The closure interpreters
(functions, not classes) were found by grepping for COP1 decoding and read.

### 5a. Every oracle interpreter and its float model

| family (root) | COP1 | VU0 macro | users |
|---|---|---|---|
| `test_point_light_reference.Oracle` | `tools/ee_cop1.py` (moved in C8; `truncate_ee_division` stays as the negative control that shows the captured player colour at 0x2FE060 needs round-to-nearest DIV.S) | `vu_lane` / `vu_div` / `vu_sqrt`, bit-pattern ACC and Q (moved in C8) | player_callback_oracle (floor, footstep, probe, stage workers), interaction_scan's ScanOracle, item_sdk_math's Original (roger, face allocation, player face host, director, crate and through it drum and head sprite, door candidate, cinematic playback, item device / geometry), pickup_owner's OwnerOracle (door, fan, message service, truck, area script, pickup motion), actor_lighting's Ram (shadow_original's ShadowRam, whose VOPMULA/VOPMSUB also use `vu_lane`), actor_pool, actor_census, area_load, room_move, spawn_place, manager_8257a0, camera_rotation, item_trail, scene task / frame / classify, area11 fog / sfx, continue_reset, input_block, roger_media, export_status_hub |
| `test_player_slide_reference.EE` | `tools/ee_cop1.py` (moved in C8) | `vu_lane` / `vu_div` / `vu_sqrt`, bit-pattern ACC and Q (FallEE's measured macro, moved into the base in C8; FallEE is now the same class by name) | 32 classes: FallEE and its subclasses (slide, climb, fall, ladder entry, camera follow / leftovers / specials, render context, render verify, status pages, census oracles, the AREA01 lanes' oracles), main_loop_and_gap, message_draw, startup_load_gaps, script_door_fan |
| `test_player_reentry_reference.Original` | `tools/ee_cop1.py` (moved in C8) | none (no VU0 op in its routines) | interaction_animation, pose_transition (player_pose, pose_bank, roger pose / cinematic, door runtime), status_frame, panel_message (item_root, status_page, export_item_root), collision_faces |
| own interpreters, already on the model | `ee_float_model` | `vu_lane` (or no VU0) | coll_move FloatEE (anim_runtime_rest, locomotion_display, pose_host_workers, sdk_vu0), coll_probe ProbeEE (segment walkers), effect_original EE (effect kinds / manager), frame_render_heads FrhEE (render_context_live), shadow_actor_route RouteEE (shadow decal), script_host_workers ScriptEE (heading record), recovery ModelEE (running jump), hang, ladder climb, reaction, closures 0E/18 and 10/12/19, major2, stage_workers, owner_services, pickup_items, player_equipment, status_scene, status_background, sdk_math_original, stream_lanes, player_motor, chain_page_model |
| closures moved in C8 | `tools/ee_cop1.py` | none | camera_retarget, elevator, elevator_commands, opening_face (its 001D0720 part), player_heading, snow_tiles, weather |

Not on the model, by design:
- **VU1 microcode** (VU1 was not measured, section 1): test_snow_particles_reference,
  the morph oracle in test_opening_face_reference, test_shadow_original_reference's
  MiniVU / VU1, the VU1 kernel oracles.
- **Hooks that stand in for SDK VU0 routines inside closure oracles**: test_snow_tiles_reference
  (001026A0, 001028B8, 00102900 and the 001029E8 polynomial, per-lane
  truncation; its 00102B08 quarter turn is COP1 and its VSQRT is `vu_sqrt`),
  test_camera_retarget_reference (001028D0). test_player_heading_reference keeps
  host cosf / atan2f and a host 001B1470 wrap and compares within 3e-6.

`ee_cop1_plain` (test_player_floor_reference) is removed: the base Oracle
does the same. The crate oracle's range gate (`EE_RANGES`) is removed: the
SDK routines the owners call now run on the model as well.

### 5b. Native translations moved in C8

Each was compared again with its (now measured) oracle; the evidence column
names the oracle and any capture it checks.

| module | original | change | evidence |
|---|---|---|---|
| em_point_light.c | 001D7C30 tick, 001D8534 fold | COP1 sites (001D7C80, 001D7D8C/90, 001D7E1C/20, 001D85AC..001D85FC; traced from the oracle) through em_ee_*; the flicker matrix is em_owner_services' 001029C0 / 00102B08 / 00102BB0 (its private copy removed); the tick returns the SDK status | test_point_light_reference (266 update, 256 fold cases, the captured player colour at 0x2FE060 and the captured flicker matrix) |
| em_camera_retarget.c | 0018CBD0 | every scalar through em_ee_* (eye adds, MULA/MADD square, ADDA/MADD target height); the 001028D0 delta stays VU | test_camera_retarget_reference (948 cases, the captured panel camera) |
| em_camera_rotation.c | 001029C0 + 00102C58 + 001026A0 | now calls em_owner_services' 001029C0 / 00102C58 and em_effect_original's 001026A0 (its private rotation removed) | test_camera_rotation_reference (972 cases byte-exact); its callers' oracles |
| em_weather.c | 001E55F0 | all COP1; the strength quotient rounds to nearest | test_weather_reference (12,000 state comparisons) |
| em_snow.c | 001E67C0 | own sums, products, quotients and conversions through em_ee_*; the tile colour is em_sdk_vu0_00102900; the matrix transform stays VU; returns the SDK status | test_snow_tiles_reference including the captured tiles (`--reference-ee/--reference-tiles`: 216 params, colours and phases equal, matrix error 0) |
| em_item_trail.c | 001B62C0, 0020AC70, 001D66A0 | all COP1; **MSUB is ACC - fs*ft**: the fan's d term had the opposite sign (fitted to the old product - ACC oracle) | test_item_trail_reference (22,016 fixed-point triangles) |
| em_item_sdk_math.c | 0011C7B0, 0011CB90, 0011CCC8, 0011D770 | add/sub/mul through em_ee_* (these SDK bodies are COP1) | test_item_sdk_math_reference |
| em_interaction_scan.c | 00183EF0, 001B1470, 001B1630, 0011C4C8 / 0011DBB8 | COP1 sites through em_ee_*; the 001028D0 / 00102738 / 00102760 vector calls stay VU; 001B1630's distance is ADDA/MADD | test_interaction_scan_reference, test_item_sdk_math_reference |
| em_opening_face.c | 001D0720 | all COP1 (it was host round-to-nearest) | test_opening_face_reference, test_face_slot_reference |
| em_cinematic_camera.c, em_cinematic_playback.c | 0022EEF0, 0011E398 / 0011D878 | interpolation, zoom, roll and time through em_ee_*; the up vector stays VU | test_cinematic_playback_reference (the capture's up and zoom bytes), test_camera_reference over opening_ee.bin (eye / target bytes equal) |
| em_pickup_motion.c | 001B7F90 (001B1240, 001B12B0, 001B1470), 001B8FC0 | all COP1 | test_pickup_motion_reference |
| em_crate_original.c (SDK block), em_drum_original.c | 001029C0, 00102A60 / 00102B08 / 00102BB0, 00102C58, 00102918, 001026D0, 001026A0, 00102738, 001B1470, 001281C0 | the SDK calls go to the one bound translation of each (owner services, em_sdk_vu0.h, em_effect_original); the private host-float copies are removed and their status is a fault | test_crate_original_reference (route captures), test_drum_original_reference, test_head_sprite_reference |
| em_sdk_vu0.h | 00102738 | new leaf; the private copies in em_coll_probe_original, em_coll_list_passes_walkers, em_coll_grid_hull, em_coll_move_original, em_pickup_items_original, em_owner_draw_original, em_actor_light_001D89D0, em_camera_follow_original, em_camera_area11_specials and em_camera_leftovers now call it | test_sdk_vu0_reference executes the original; each caller's oracle |
| em_pose_math.h | (the pose, script and owner helpers) | pose_add / sub / mul / div are em_ee_*; pose_madd / pose_msub stay MUL then ADD / SUB; the director's accumulator sites call em_ee_mula / madd / msub | test_director_original_reference, test_pose_transition_reference, test_area_script_reference |
| em_load_veil.c | 0021B550 | add.s / mul.s through em_ee_* | test_area_load_reference |
| em_collision.c (`em_collision_box_face`) | 001A4D10 / 001A50A0 | all COP1 through em_ee_* (it differed from the measured oracle; its quotients round to nearest) | test_collision_faces_reference (4,800 cases, 721 exact hits) |
| em_item_geometry.c | 002082B0 | scalar sites through em_ee_* (it differed from the measured oracle); the colour vectors stay VU | test_item_geometry_reference (5,562 exact vertices) |
| em_item_device.c, em_door_candidate.c, em_roger.c (candidate), em_panel.c (candidate), em_panel_program.c (001B9BA0), em_pickup_owner.c (0015AE20), em_player.c (001760C0 / 0019AB20 probe glue), em_player_foot_stop.c (0017B910 begin, 0017C030 tick), em_status_draw.c (00208AD0 health arc), em_area11_effect.c (008235F0), em_interaction_alignment.c (001B6F00 yaw), em_interaction_projection.c (001DD980) | as listed | their COP1 sites through em_ee_* | each module's reference test |

### 5c. What is left

- **VU0 per-lane helpers on a truncated host double**, `em_effect_float32((double)a ± b)`:
  em_truck_original, em_door_original_runtime, em_area_script's 001028D0,
  em_interaction_alignment's 001026A0, em_player_floor's 001026A0,
  em_player_pose_host's 00182F90 alignment, em_point_light's fold sums,
  em_snow's matrix transform, em_camera_retarget's delta, em_cinematic_playback's
  up vector, em_item_geometry's colours and em_interaction_scan's
  00102760. They equal the model except for the opposite-sign edge above.
  Their oracles pass; the fix is to reduce them to header-only owners of
  001028B8, 001028D0, 001026A0 and 00102850 in em_sdk_vu0.h.
- **Duplicate SDK math**: em_item_sdk_math.c (0011C7B0, 0011CB90, 0011CCC8,
  0011D770) and em_interaction_scan.c (0011DBB8, 0011C4C8) duplicate
  em_sdk_math_original.c. Both are on the model now; the reduction needs the
  SDK tables in every caller's build.
- **em_collision.c's `em_collision_box_face`** is a second translation of
  001A4D10 / 001A50A0 beside em_coll_move_original and
  em_coll_segment_walkers (the census's live owners); both are on the model.
- **em_snow.c's wave** uses host sinf where the original calls the SDK
  0011E2A8 (test_snow_tiles_reference reports it as the "original SDK matrix"
  comparison, 5,022 of 5,184 exact in the full sweep).
- **em_lighting.c** is checked only under its port contract (census L40; the
  bit-exact em_actor_light_001D89D0 is unbound).
- **em_status_draw.c's battery colour ramp**: the original routine of its
  sums was not established; left as it was.
- **em_pose_transition.c's quaternion blend** uses pose_madd (MUL then ADD)
  where the original has MULA / MADD / MSUB; the two differ only when a
  product overflows, which unit quaternions cannot.

## 6. Native header

`src/game/em_ee_float.h` is the C form of this model. **Every new native
translation of original COP1 or VU0-macro arithmetic uses it**, not local
float helpers. Section 5 lists the modules on it and what is left; a
change to a translation and its oracle twin lands in one commit.

- **Exactness.** All arithmetic is on integers (significands in `uint64_t`),
  and no host float operation is performed. The host rounding mode, FTZ/DAZ
  state and FMA contraction therefore cannot change a result. The test
  disassembles the compiled shim and fails on any host FP instruction that
  rounds. Compares are allowed: clang turns the raw-bit NaN tests into an
  unordered `fcmp`, which is exact in every mode. The header also compiles
  clean under `-std=c11 -Wall -Wextra -Wpedantic -Wconversion
  -Wsign-conversion -Wshadow -Werror` with clang and GCC 14.
- **Speed (2026-09-28).** The bit length is one count-leading-zeros (GCC /
  clang; the shift chain elsewhere), `em_eei_exact_product` uses the
  product's known 47- or 48-bit width, and `em_vu_form_lookup` binary-searches
  the form table (kept sorted by op, dest, bc). The old and new header
  agree on 50 million random and edge operand sets for every op and on
  every (op, dest, bc) form (a scratch differential, C8 static-world fix
  round), and this header test passes quick and full.
- **Host lanes (`em_vu_host_lanes.h`).** For the VU1 kernels on the live
  draw only (the level kernel's walk, the object and face kernels in
  em_object_unit.c) a multiply, add, subtract or VDIV (3,3) reciprocal on
  finite operands is computed by the host FPU in round-toward-zero,
  flush-to-zero, denormals-are-zero, which is exactly this section's VU
  rule (IEEE rounds the exact result once; toward zero that is the
  truncation, and overflow gives the largest finite value); a zero divisor
  is handled as the model. `test-vu-host-lanes` proves every lane equal to
  this header on boundary classes and random operands. The model stays
  the reference: every test oracle and every other translation uses this
  header.
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
