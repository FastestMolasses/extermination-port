# SDK float math: the boot ELF's sin/cos/tan/atan/atan2/sqrt

Status: 2026-09-23 (landed with WP-5). A translation with an
original-instruction oracle. One site is live: the status background
0020A7A0 draws its sine through `em_sdk_math_original_float_0011E2A8`
(`em_status_background_draw.c`), over the tables of the user's ELF
exported by `tools/export_sdk_math_tables.py` to `assets/sdk_math_tables.emsm`
(docs/STARTUP.md). The collision world (`em_collision_world.c`) holds the one
shared SDK context, with the soft-float workers of SDK_SOFT_FLOAT.md bound
since 2026-09-24, so 0011E620 atan2f and 0011E748 sqrtf are complete; its
column and the gated FLOOR use it. The other sites are not wired; section 7
is the binding recipe for them. The section 7 gate on the atan2f/sqrtf sites
is lifted (option 1, section 7).

Files:
- `src/game/em_sdk_math_original.{h,c}`: the translation.
- `tools/test_sdk_math_original_reference.py`: the oracle test (`make test-sdk-math-original-reference`).
- `tests/sdk_math_original_test.c`: the native contract test under ASan/UBSan (`make test-sdk-math-original`).

## 1. What was translated

Each function was translated from its instructions in
`../Extermination/build/asm/matchings/main/code/func_*.s`, not from the decomp C.
Several of those C files are NEARMISS (0011C7B0, 0011DBB8, 0011E748), and others are
asm or `.word` bodies. The library is the EE toolchain's fdlibm-derived float library.
The fdlibm names below only help with recognition. What the code does comes from
the instructions.

| Address | Role | Calls |
|---|---|---|
| 0011E2A8 | sinf | 0011D770, 0011CCC8, 0011C7B0 |
| 0011DE90 | cosf | 0011CCC8, 0011D770, 0011C7B0 |
| 0011E398 | **tanf** (not cosine, see 6.3) | 0011D878, 0011C7B0 |
| 0011C7B0 | reduction by pi/2, all branches: \|x\| ≤ pi/4, n = ±1, medium with the 2nd/3rd iterations, Inf/NaN, and large | 0011DF78, 0011CE20 |
| 0011CE20 | large-argument reduction (kernel_rem_pio2f), prec 0..3 (the game uses prec 2; see section 3 for how prec 3 was exercised) | 0011E148, 0011DF98 |
| 0011D770 / 0011CCC8 / 0011D878 | sine / cosine / tangent kernels | 0011DF78 (tan) |
| 0011DBB8 | atanf | 0011DF78 |
| 0011C4C8 | atan2f kernel | 0011DBB8, 0011DF78 |
| 0011E620 | atan2f wrapper | 0011C4C8, 0011E080, **workers** 00128350, 0011DB90, 0011FD78, 00127758 |
| 0011CB90 | sqrtf kernel (integer digit loop) | none |
| 0011E748 | sqrtf wrapper | 0011CB90, 0011E080, **workers** as for 0011E620 |
| 0011DF78, 0011E080, 0011DE60, 0011DF98, 0011E148 | fabsf, isnanf, copysignf, floorf, scalbnf | 0011DE60 (scalbnf) |

**Arithmetic.** Every COP1 instruction is a call into the shared EE model
`src/game/em_ee_float.h` (EE_FLOAT_MODEL.md section 6), on raw binary32 words:
`em_ee_add_bits`, `_sub_bits`, `_mul_bits`, `_div_bits`, `_neg_bits`,
`_cvt_w_s_bits`, `_cvt_s_w_bits` and `_c_eq/_c_lt/_c_le_bits`. The module has no
float arithmetic of its own, and the host FPU never computes a value. A fix to
the model therefore reaches this module.

An earlier version carried a private copy of the model. When it was replaced, the
old and the new module were compared as two libraries: 4,000,000 random words
(and pairs) through every leaf, kernel, reduction and wrapper entry point, 56,000,000
results, 0 differences. The quick and full oracle runs and the sanitizer test were
then repeated (section 3).

These functions contain no MADD, MSUB or VU0 instructions.

**Data.** `em_sdk_math_original_load_tables` reads every table from the user's ELF.
The ELF must be exactly 1,532,624 bytes and start with the ELF magic. It reads:
- D_0026C170 (−0.0f);
- D_0026C178, the 198-entry ipio2 table;
- D_0026C490, npio2_hw;
- D_0026C538, init_jk;
- D_0026C548, PIo2;
- D_0026C598, the tangent T[13];
- D_0026C5D8 / D_0026C5E8 / D_0026C5F8, the atan hi/lo/aT;
- D_0026C650, the double retval of 0011E748.

The oracle asserts that the captured RAM holds the same bytes. The code contains
instruction immediates (lui/ori constants), and each one names the address that
builds it. No table values are in the source.

## 2. Behaviour that differs from a host libm (all oracle-verified)

- **Denormals.**
  - An operand exponent field of 0 is read as zero: ADD, MUL, DIV and the compares all apply DAZ.
  - A result below 2^-126 flushes to a signed zero.
  - Consequences:
    - `atan2(denormal, denormal)` divides 0 by 0, which gives +MAX, so atanf(MAX) returns pi/2.
    - A tiny ratio gives exactly ±0.
    - `scalbnf(denormal, n)` cannot rescale: the MUL by 2^25 at 0x11E19C reads the operand as zero.
- **Inf/NaN.**
  - `sinf`/`cosf`/`tanf` of Inf or NaN return `x - x`, which saturates to +0x7F7FFFFF.
  - The kernel `0011CB90(-x)` returns `(x-x)/(x-x)`: 0/0 on the EE is **+0x7F7FFFFF**.
- **The wrappers with D_0026C5D0 = 1.** The ELF image and every capture hold 1. The results were
  measured with every callee running as original instructions, including the soft-float library, 0011DB90 and 0011FD78:

  | Call | Result | errno (*D_0024295C = 0x242670) |
  |---|---|---|
  | atan2f(±0, ±0), and also a **denormal pair**, because c.eq.s reads denormals as 0 | +0 (00127758(0.0)) | 0x21 |
  | sqrtf(negative normal) | **0x7FB00000** (00127758 of the double NaN D_0026C650) | 0x21 |
  | sqrtf(negative denormal): c.lt.s(x, 0) is false under DAZ | 0x7F7FFFFF (the kernel's 0/0) | unchanged |

  With D_0026C5D0 = −1 the kernels' values come back and errno is not touched. With mode 2, 0011DB90 is
  not called. With mode 0, sqrtf's retval is 0. 0011E748 reads D_0026C5D0 a second time at 0x11E7E0.

## 3. Verification

`python3 tools/test_sdk_math_original_reference.py`. The oracle is a bounded EE interpreter written for this
test:
- It executes the user's own ELF code over `playable_ee.bin`.
- Before a function runs, its range (from `docs/FUNCTIONS.csv`) is checked byte for byte against
  `SCUS_971.12`.
- COP1 is `tools/ee_float_model.py`.

Results are compared bit for bit: the result, y[0]/y[1]/n of the reductions, and y[0..2]/n of 0011CE20.

| Part | Quick (default: 4.3-5.8 s wall, 8 workers) | Full (`EM_TEST_FULL=1`, 2 m 19 s idle, 2 m 54 s at load average ~16) |
|---|---|---|
| sinf / cosf / tanf / 0011C7B0 | 2,758 args each | 202,891 each |
| atanf | 1,106 | 100,081 |
| 0011C4C8 pairs | 1,575 | 90,425 |
| kernels 0011D770 / 0011CCC8 / 0011D878 | 2,230 | 61,480 |
| sqrt kernel / floorf / scalbnf+copysignf / fabsf / isnanf | 1,411 / 1,206 / 1,326 / 232 / 46 | 130,123 / 60,289 / 21,026 / 232 / 46 |
| 0011CE20 direct, prec 0..3 (of which: prec 3 with init_jk[3] patched / jv clamped / stale reads refused) | 616 (120 / 123 / 90) | 15,426 (3,090 / 1,065 / 2,826) |
| wrappers, callees recorded (modes −1, 0, 1, 2, 5; six callee scripts, two of which rewrite D_0026C5D0 inside 00128350) | 4,110 | 4,110 |
| wrappers, callees executed as original | 7 | 7 |
| fail-stop / adapters | 38 / 800 | 38 / 16,000 |
| **total equal to the original** | **26,095** | **1,311,207** |

The times in the header row are wall-clock measurements from particular runs on the M1, not guarantees: they
depend on machine load (other lanes' builds and tests share the cores; the full run took 2 m 19 s idle and
2 m 54 s at a load average of ~16) and on `EM_TEST_JOBS` (the worker count). Only the case counts are fixed.

The 0011CE20 count includes the stale cases, where the check is that the native
refuses (fault 0x0011CE20) exactly the cases whose original result depends on
unwritten stack.

**Argument classes:**
- every branch edge ±2 ulps;
- signed zeros, denormals, ±MAX, ±Inf and NaNs;
- words at and next to k·pi/2 for k < 130, plus random large k (the npio2_hw and 2nd/3rd-iteration paths);
- the script ease arguments pi·t − pi/2;
- uniform random words;
- random finite values at several scales.

**Wrappers, recorded mode.** The following are compared on both sides:
- the ordered callee calls and their arguments;
- the 0x24-byte exception record that 0011DB90 receives, including a matherr that writes err/retval;
- the errno stores;
- the result.

**Wrappers, original-callee mode.** The native workers are bound to original executions of 00128350,
0011DB90, 0011FD78 and 00127758, and the native result equals the whole original.

**0011CE20 paths the game never takes.** The only caller, 0011C7B0, passes prec 2
and e0 ≥ 0. The test still drives the other paths on the original instructions:
- **jv < 0.** e0 runs from −12, so for e0 ≤ −5 the clamp after 0x11CE64 sets jv = 0.
- **prec 3 with jz ≥ 2.** The ELF's init_jk[3] (D_0026C538 + 12) is **0**, so with
  the real table prec 3 starts at jz = 0 and reaches at most jz = 1 (the single
  jz + 1 at 0x11D380). The second compensation loop (0x11D680) and the tail sum
  (0x11D6D8 / 0x11D6EC) then never run. The patched cases store init_jk[3] = 2, 5 or 9
  in both the oracle RAM and the native tables for one case, compare y[0..2] and n
  bit for bit, and restore 0 (asserted before and after). All 120 quick / 3,090 full
  patched cases were compared, none were stale.

Mutating either loop bound (0x11D680 `i > 1` → `i > 2`; tail `i >= 2` → `i >= 3`)
or deleting the jv clamp is caught by the quick run (86, 70 and 43 mismatches).

**0011CE20 with prec 3 and jk = 0.** The original then reads stack words it never wrote:
- iq[jk−k] below its frame, at 0x11D21C;
- fq[1], at 0x11D700, when jz = 0.

The test runs each case over two different stack fills. When the original's result changes with the fill,
the native must fault at 0x0011CE20, and it does. Only 0011C7B0 calls 0011CE20, always with prec 2, so the
game never takes this path.

**Route (asserted).** The AREA11 script host (`test_area_script_reference.capture_case`) replays route beats
07 (truck preview, 364 frames) and 10 (Roger + director, 2,419 frames). Its sine worker is bound to
`em_sdk_math_original_w_0011E2A8`. The results:
- 0 differences;
- 11,088 camera eye/target comparisons;
- 800 sine calls, each also equal to the oracle.

**Negative controls.** Fourteen defects were injected into the native module one at a time, and the quick
run caught every one (the three 0011CE20 mutants above, and these eleven):
- truncating DIV;
- ADD without the pre-trim;
- the 0x3E999999 edge of the cosine kernel;
- the third reduction step's 26;
- the sqrt kernel's final rounding;
- D_0026C170 replaced by +0;
- 0011CE20's carry subtraction;
- 0011E748's second read of D_0026C5D0;
- the tangent's sign;
- floorf's negative step;
- scalbnf's overflow multiply.

One more mutant replaced scalbnf's `tiny*tiny` with a signed zero. It is equivalent under FTZ, and the run found
no difference.

**Contract test.** `tests/sdk_math_original_test.c` covers:
- the loader rejections;
- values that follow from the model alone;
- odd/even symmetry;
- every fail-stop address;
- the wrapper protocol (call order, errno cell, exception fields, the denormal pair);
- the adapters;
- a 200,000-word undefined-behaviour sweep over every entry point.

It must be built with `-fno-sanitize-recover=undefined` so that a UBSan report
fails the run (the requested Makefile target does).

## 4. Fail-stop

Each fault returns −1 and sets `*fault` to one of these addresses:

| Condition | Address |
|---|---|
| tables NULL | the reading instruction: 0x0011C98C (npio2_hw), 0x0011CEA0 (init_jk), 0x0011D974 (T), 0x0011DD78 / 0x0011DC14 (atan tables), 0x0011C684 (D_0026C170), 0x0011E7D8 (D_0026C650) |
| world NULL or no D_0026C5D0 cell | 0x0011E648 (atan2f), 0x0011E76C (sqrtf) |
| worker missing or returning −1 | 0x00128350, 0x0011DB90, 0x0011FD78, 0x00127758 |
| NULL output | the function's entry |
| 0011CE20 index outside its 20-entry stack arrays or the 198-entry D_0026C178, prec outside 0..3, or a read of an unwritten fq[1] | 0x0011CE20 |

Paths that read no table need none. For example, sinf with \|x\| ≤ 0x4016CBE3 runs without tables. The wrappers
call their workers only on the error path.

## 5. Not translated here (workers)

The error path of the two wrappers calls four functions:
- 00128350 (float → double; below it 001278C0, 00127728, 00126AB8);
- 0011DB90 (matherr: it calls 001274B0 → 00126BE8 ×2 and 00127398, and returns 0);
- 0011FD78 (returns the pointer D_0024295C);
- 00127758 (double → float; below it 00126BE8, 00128320, 001277B0).

About 440 integer instructions sit below them. They are worker slots, and the
oracle verifies them only as original executions.

**Done.** `em_sdk_soft_float` translates all four and everything below them (SDK_SOFT_FLOAT.md,
oracle `test_sdk_soft_float_reference`), with the section 2 values. It is bound into the collision
world's SDK context (section 7). A context whose `.workers` are left empty still **faults** at
0x00128350 on an EE-zero atan2 vector or a negative normal sqrt argument. It does not guess.

## 6. Findings for other lanes

1. **ACTOR_COLLISION section 5 (the 0011DBB8 question) is settled.**
   - Under the EE model, `em_director_original_0011DBB8` equals the original 0011DBB8 on every finite
     argument: 0 of 1,093 quick and 0 of 99,847 full differ.
   - The director's pre-trim/nearest-division model is correct.
   - The actor-collision oracle, which truncates every add and division, is the wrong side.
   - `em_director_original_0011C4C8` and `_0011E620` differ only where DAZ/FTZ matters (the test asserts
     0 other cases): 923 and 930 of 89,981 full. These are denormal operands, tiny quotients, and a denormal
     pair that the original routes to the zero-vector path.
2. **`em_item_sdk_sine`/`_cosine` are not the original.** This is the crate/drum and door candidate/transit
   binding.
   - They differ on 67,390 and 66,955 of 119,490 arguments (|x| ≤ 4pi, full), all of them normal.
   - The cause is plain-truncation add, the model section 5c of EE_FLOAT_MODEL.md names.
   - `em_item_sdk_sqrt` equals the kernel on every nonnegative finite argument: 50,032 of 50,032.
3. **The floor's 0011E398 slot is tanf.** It is the `__kernel_tan` path with iy = ±1, as in 00175CF0:
   `t = func_0011E398(+0x9C)`, then `delta / t`.
   - Fixed 2026-09-24 (census L02 step): the slot is `EmPlayerFloorWorkers.tangent` /
     `EmPlayerStatesBinding.tangent` (it was `cosine`).
   - The floor oracle does not execute 0x11E398: it hooks it as a boundary with the same host model on
     both sides. That model was host cosf (after the label); it is host tanf now. The live binding is
     `_float_0011E398`.
4. **`em_interaction_sdk_atan2`** (Roger trigger/candidate, door candidate/transit, item device, pickup motion,
   interaction scan) differs from the original on 37,246 of 89,981 pairs (full). Of the 37,246 full differences, 36,193
   have normal operands and a normal host result (531 of 586 in quick mode).
5. `em_area_script_sin_0011E2A8` equals the original on its whole domain: 68,792 of 68,792 (full, asserted). It
   refuses |x| > 0x4016CBE3, where this module has the whole reduction.

## 7. Binding (for the coordinator chain)

**One shared context.** It is `w.math` in `em_collision_world.c`:

- `.tables` and `.world.d26C5D0` come from `assets/sdk_math_tables.emsm`
  (`em_sdk_math_original_load_export`, at every area build; D_0026C5D0 = 1).
- `.workers` are `em_sdk_soft_float_bind(&w.math.workers, &soft)`. `soft` holds
  D_0024295C and the errno word at the address it names (0x00242670, initial 0). Both
  come from `assets/sdk_soft_float.emsf` (`tools/export_sdk_math_tables.py`,
  `em_sdk_soft_float_load_export`; SDK_SOFT_FLOAT.md section 4). They are loaded
  once, by the first world load, and an area build does not reset them.
- A missing or malformed export fails the world load, so the AREA11 build faults
  at 0x001AFCA0.

The float-returning adapters have the player and collision worker shape. On a fault they return +0 and set
`sdk.fault`. The caller must test `sdk.fault` after the owner's tick and fail the frame at that address; no value
is substituted.

**The atan2f and sqrtf gate is lifted (2026-09-24, option 1).** The rule was that no
0011E620 or 0011E748 site may be bound until either (1) the four soft-float
workers were translated and bound, or (2) a route measurement showed that their
domain-error paths are never reached. Without the workers, `_float_0011E620` faults
at 0x00128350 on every EE-zero vector while D_0026C5D0 != -1: y and x both ±0
**or denormal**, because c.eq.s at 0x11E67C/0x11E68C reads denormals as 0.
`_float_0011E748` faults there on a negative normal argument.

The evidence for lifting it:
- **Option 2 is refuted.** The errno word at 0x242670 first reads 0x21 at the end of beat 03 and
  stays 0x21. The route census hit 0011DB90, 0011FD78 and 00127758 in beats 03 and 05. It never
  hit 0011E420 or 0011E520, so each of those tails was 0011E620's or 0011E748's.
  `test_sdk_soft_float_reference` asserts all of this (SDK_SOFT_FLOAT.md section 5).
- **Option 1 holds.**
  - The four workers are translated. Their oracle (27,324 quick cases) includes the whole wrappers
    executed as original instructions against this module with the workers bound, for modes −1, 0,
    1, 2 and 5.
  - They are bound into the one context above, over the data export.
  - The export equals the ELF and the captured first-control RAM (D_0024295C = 0x00242670, errno
    0). Every route snapshot holds the same pointer.
  - The zero-vector and negative-root cases run over a context built from the loaded export and
    equal the original, errno included (test part 9).

So every atan2/sqrt row of the table below may now be bound. Each rebinding still
follows the harmonization rule at the end of this section.

The sin/cos/tan/atan entry points (`_float_0011E2A8`, `_float_0011DE90`,
`_float_0011E398`, `_float_0011DBB8`, `em_sdk_math_original_w_0011E2A8`,
`em_sdk_math_original_0011E2A8` / `_0011DE90`) call no worker and cannot fault once
the tables are loaded.

**Bound today** (`em_collision_world_bind_player`, over the collision world's context, with that
context's fault word as `EmPlayerStatesBinding.sdk_fault`):
- the floor's `.tangent` (= tanf; the slot was named `.cosine` until 2026-09-24) and `.atan`,
  since the census L02 step;
- the floor's `.atan2` and `.sqrt`, since the soft-float step (2026-09-24);
- the column's `.sqrt` / `.atan` (`w.column_math`).

The floor rows run only once FLOOR engages. The column's sqrt argument is nx²+nz² ≥ 0, so it never
reaches the domain-error tail. The other rows below are not bound yet. The floor's oracle needed no switch for
`.atan2` / `.sqrt`: its unit section hooks all four SDK slots as a boundary with the same host model
on both sides, and its real-world section (`test_player_floor_reference.real_world_section`) already
hands the native slots the results of the original 0011E620 / 0011E398 / 0011DBB8 / 0011E748
executed over the route RAM.

| Original caller (worker slot) | Bind to | Replaces |
|---|---|---|
| 00174FD0 / 0017F5F0 slide: `EmPlayerSlideWorkers.sine`, `.cosine`, `.atan2` | `em_sdk_math_original_float_0011E2A8`, `_float_0011DE90`, `_float_0011E620` (context `&sdk`) | host models |
| climb: `EmPlayerClimbWorkers.atan2`, `.sqrt` (em_player_climb.h) | `_float_0011E620`, `_float_0011E748` | host models |
| 00175CF0 floor: `EmPlayerFloorWorkers.atan2`, **`.tangent` (= 0011E398)**, `.atan`, `.sqrt`; em_player.h's live binding of the same four plus `sdk_context` / `sdk_fault` | `_float_0011E620`, **`_float_0011E398`**, `_float_0011DBB8`, `_float_0011E748` (all four bound in em_collision_world.c; FLOOR itself is gated) | host models |
| 0019BC40 / 0019F330 column: `EmCollColumnMath.sqrt`, `.atan` (em_collision.h) | `_float_0011E748`, `_float_0011DBB8` | em_item_sdk_sqrt / director atan wrappers (equal on that domain; the argument is nx²+nz² ≥ 0) |
| script host `w_0011E2A8` (em_area_script.h) | `em_sdk_math_original_w_0011E2A8` | em_area_script_w_0011E2A8 (equal; adds \|x\| > 0x4016CBE3) |
| crate/drum 0011E2A8 / 0011DE90 (em_crate_original.c, em_drum_original.c) | `em_sdk_math_original_0011E2A8` / `_0011DE90` | em_item_sdk_sine/cosine (**differ**, 6.2) |
| every 0011E620 site that uses `em_interaction_sdk_atan2` (em_roger.c trigger/candidate, em_door_candidate.c, em_door_transit.c, em_item_device.c, em_pickup_motion.c, em_interaction_scan.c) | `_float_0011E620`. 0011C4C8 has one caller, 0011E620, so every game site is the wrapper. | em_interaction_sdk_atan2 (**differs**, 6.4) |
| director 001B1EA0 (em_director_original) | `em_sdk_math_original_0011E620(&sdk_tables, &sdk.world, &sdk.workers, …)` | its own copy (differs only under DAZ/FTZ) |

**Data.**
- D_0026C5D0 is .data. Give it canonical storage (value 1) rather than a constant, because 0011E748 reads it
  twice.
- The errno cell is the int at *D_0024295C (0x242670). `w_0011FD78` returns the collision world's canonical
  storage for it (`s_soft.errno_word`), and faults if D_0024295C names any other address.

**Harmonize each pair in one commit.** Rebinding a module changes its oracle's expectations. Each owner's
reference test must switch its SDK stand-in to the original execution, or to this module, in the same commit
(EE_FLOAT_MODEL.md, "Binding note for the lead").
