# SDK soft float: the four workers of the atan2f/sqrtf error path

Status: translated 2026-09-23 (lane "sdk-soft-float"), with an
original-instruction oracle. **Bound 2026-09-24** into the collision world's
SDK context (section 4). Its data comes from the user's export
`assets/sdk_soft_float.emsf`. It completes the worker set that
SDK_MATH_ORIGINAL.md section 5 left open, and the gate in that doc's
section 7 is lifted by this binding (option 1). Section 5 below shows that
option 2 (a route measurement proving the path is never reached) is
**refuted**: the AREA11 route reaches a domain-error tail of 0011E620 or
0011E748 in beat 03 (and again in beat 05).

The bound context serves the column's sqrt and the gated FLOOR's
0011E620 / 0011E748. No live caller reaches the error tails yet:
- the column's argument is never negative;
- FLOOR is not engaged.

So the route's EDOM is not reproduced live yet. The call site that raises it is
not identified (section 5).

Files:
- `src/game/em_sdk_soft_float.{h,c}`: the translation.
- `tools/test_sdk_soft_float_reference.py`: the oracle test (`make test-sdk-soft-float-reference`, section 6).
- `tests/sdk_soft_float_test.c`: the contract test under ASan/UBSan (`make test-sdk-soft-float`).
- `tools/export_sdk_math_tables.py`: also writes the data export `assets/sdk_soft_float.emsf` (section 4).
- `src/game/em_collision_world.c`: the binding.

## 1. What was translated

Every function was translated from its instructions in
`../Extermination/build/asm/matchings/main/code/func_*.s`. Most of the decomp C
files here are `.word` bodies; the readable ones (00127728, 0011DB90, 001274B0,
0011FD78, 00128320) are ee-gcc code that the decomp byte-matches, and they agree
with the listing. The library is the EE toolchain's soft-float support (the
fp-bit layout of an unpacked number). The name only helps with recognition.
What the code does comes from the instructions.

| Address | Size (instr.) | Role | Calls |
|---|---|---|---|
| 00128350 | 16 | float → double (the wrappers' worker `w_00128350`) | 001278C0, 00127728 |
| 001278C0 | 36 | unpack a float into the 0x10-byte record | none |
| 00127728 | 11 | store (class, sign, exp, 64-bit fraction) as a 0x18-byte record, pack it | 00126AB8 |
| 00126AB8 | 75 | pack a double record | none |
| 0011DB90 | 9 | matherr (worker `w_0011DB90`) | 001274B0 |
| 001274B0 | 19 | the double compare | 00126BE8 ×2, 00127398 |
| 00126BE8 | 39 | unpack a double into the 0x18-byte record | none |
| 00127398 | 69 | the three-way compare of two records | none |
| 0011FD78 | 3 | errno cell (worker `w_0011FD78`) | none |
| 00127758 | 21 | double → float (worker `w_00127758`) | 00126BE8, 00128320 |
| 00128320 | 11 | store (class, sign, exp, 32-bit fraction) as a record, pack it | 001277B0 |
| 001277B0 | 67 | pack a float record | none |

376 instructions in all. None of them is COP1 arithmetic. The float argument
of 00128350 is stored from f12 and the result of 001277B0 is moved into f0; the
module does both moves with `em_ee_bits` / `em_ee_float` from `em_ee_float.h`.

**The records.** +0 class, +4 sign, +8 unbiased exponent, then the fraction:
32 bits at +0xC for a float, 64 bits at +0x10 for a double. The class values are
0 = signalling NaN, 1 = quiet NaN, 2 = zero, 3 = number, 4 = infinity.

**What each function does.**
- **001278C0.** It stores the sign first, so every class has one.
  - Exponent field 0 is class 2 (zero). **A denormal is zero.**
  - Field 0xFF with fraction 0 is class 4 (infinity).
  - Other 0xFF values are NaNs. Fraction bit 20 (0x100000) picks quiet (1) over
    signalling (0), and the raw 23 fraction bits are stored.
  - A number stores fraction = (m << 7) | 0x40000000 and exp = field − 127.
  - Zero and infinity leave the exponent and fraction words alone. NaN leaves the exponent alone.
- **00128350.** The fraction word moves up by 30 into the 64-bit fraction (0x128370 / 0x128380),
  and 00127728 packs the record.
- **00126AB8.**
  - A NaN (class below 2) ORs bit 51 into the fraction with **no guard shift**, and the exponent becomes 0x7FF.
  - Infinity becomes 0x7FF with fraction 0. Zero, or a fraction of 0, gives exponent 0 and fraction 0.
  - An exponent below −1022 shifts right by (−1022 − exp) when that is below 57 (else 0), then by 8.
    It does not round, and the exponent field is 0.
  - An exponent of 1024 or more becomes infinity.
  - Otherwise the fraction rounds to nearest even on its low 8 bits:
    - add 0x7F unless the low byte is 0x80;
    - at 0x80, add 0x80 only when bit 8 is set;
    - a carry past 0x1FFFFFFFFFFFFFFF shifts it right by one and bumps the exponent;
    - then it shifts right by 8.
  - Class 3, and every class value other than 0, 1, 2 and 4, takes the number path.
- **00126BE8.** It is the double version of 001278C0:
  - exponent field 0 is zero (a denormal is zero);
  - a NaN's class comes from bit 51, and its 52-bit fraction is stored **unshifted**;
  - a number stores (m << 8) | 1 << 60 and field − 1023.
- **00127758.**
  - The 32-bit fraction is bits 30..61 of the 64-bit one, with bit 0 set when any of the low 30 bits is set
    (a sticky bit, 0x12778C..0x12779C).
  - 001277B0 packs it like 00126AB8, with float limits. Below −126 it shifts right by (−126 − exp) when that is
    below 26 (else 0), then by 7, with no rounding. At 128 or more it becomes infinity.
  - The round-to-nearest-even uses the low 7 bits (0x3F / 0x40 / bit 7). The carry test is the 32-bit sign.
  - A NaN ORs 0x100000 into the fraction with no guard shift.
- **00127398.**
  - It returns 1 when either class is a NaN.
  - **Infinity:**
    - both infinite: b.sign − a.sign, so **+Inf against −Inf gives 1**;
    - only a infinite: a.sign ? −1 : 1;
    - only b infinite: b.sign ? 1 : −1.
  - **Zero:**
    - both zero: 0, whatever the signs;
    - only a zero: b.sign ? 1 : −1;
    - only b zero: a.sign ? −1 : 1.
  - **Two numbers:**
    - different signs: a.sign ? −1 : 1;
    - then the signed exponents, then the unsigned 64-bit fractions, each ordered by a's sign;
    - 0 when all are equal.
- **001274B0** returns 00127398 of the two unpacked doubles.
- **0011DB90.** It loads the record's arg1 (+8), calls 001274B0(arg1, arg1), **discards the result** and
  returns 0. It writes nothing outside its own stack. The exception record is unchanged, so a wrapper's
  `err` stays 0.
- **0011FD78** returns the word at D_0024295C: the errno cell pointer, 0x00242670 in the ELF and in every capture.

**Unwritten stack words.** 00128350 and 00127758 hand the whole record to the
packer. For zero and infinity (and for a NaN's exponent) those words were never
written, so the original reads stale stack there. Both packers also merge their
stale incoming a2 register into the result before masking it off
(0x126B9C → 0x126BC4 / 0x126BD8; 0x127884 → 0x127890 / 0x1278A8). None of those
bits reaches the result:
- the zero, infinity and NaN paths never read the stale fraction or exponent;
- the masks clear every a2 bit.

The oracle runs every conversion over a random stack fill and a random a2, and a
quarter of the cases a second time over a different fill and the complemented a2.
It asserts the same result every time (section 6). The native records start zeroed
only so that C never reads an indeterminate value.

## 2. Values (oracle results unless marked)

| Call | Result |
|---|---|
| 00128350(1.0f = 0x3F800000) | 0x3FF0000000000000 |
| 00128350(denormal 0x00000001 / 0x80000001) | +0 / −0 (0x8000000000000000) |
| 00128350(0x7F800001, a signalling NaN) | 0x7FF8000040000000 |
| 00127758(0) | 0 (the atan2f zero-vector result) |
| 00127758(0x7FF8000000000000, D_0026C650) | **0x7FB00000** (the sqrtf domain-error result) |
| 00127758(0x47EFFFFFF0000000) | 0x7F800000 (a tie that rounds up into infinity) |
| 00127758(0x36A0000000000000 = 2^−149) | 0x00000001 (a float denormal, truncated) |
| 001274B0(NaN, x) / (0, −0) / (−1, 0) / (+Inf, −Inf) | 1 / 0 / −1 / 1 |
| 0011DB90(any record) | 0, record unchanged |
| 00127758(00128350(x)) for a normal float x | x (the contract test's native sweep, 64 random fractions per exponent; each function is oracle-verified on its own) |

With this module bound, `em_sdk_math_original` reproduces SDK_MATH_ORIGINAL.md section 2 exactly
(D_0026C5D0 = 1). Every case below sets errno (*0x242670) to 0x21:
- atan2f(±0 or denormal, ±0 or denormal) = +0;
- sqrtf(negative normal) = 0x7FB00000.

sqrtf(negative denormal) = 0x7F7FFFFF with errno unchanged. With mode 2, 0011DB90 is not called (errno = 0x21
directly). With mode 0, sqrtf's retval is 0, so the result is +0.

## 3. Fail-stop

The four adapters have the `EmSdkMathWorkers` slot types. Each returns 0, or −1 and records the first fault
address in `EmSdkSoftFloatContext.fault`. `em_sdk_math_original` then faults at the same worker address.

| Condition | Address |
|---|---|
| NULL result (w_00128350) | 0x00128350 |
| NULL record or result (w_0011DB90) | 0x0011DB90 |
| NULL context, NULL out-pointer, no D_0024295C storage, no errno cell, or D_0024295C holding an address other than `errno_address` (w_0011FD78) | 0x0011FD78 |
| NULL result (w_00127758) | 0x00127758 |

The functions themselves are total. `em_sdk_soft_float_load_d24295C` returns −1 in these cases:
- NULL pointers;
- a size other than 1,532,624;
- a bad ELF magic.

## 4. Binding (live since 2026-09-24)

The module is the `.workers` of the one shared `EmSdkMathContext` (SDK_MATH_ORIGINAL.md section 7): `w.math` in
`em_collision_world.c`.

```c
static struct { uint32_t d24295C; int32_t errno_word; EmSdkSoftFloatContext context; int loaded; } s_soft;
/* first world load: em_sdk_soft_float_load_export(EM_COLLISION_WORLD_SOFT_FLOAT_PATH, &d24295C, &errno_word)
 * context = { &d24295C, d24295C, &errno_word, 0 }; every world load: */
em_sdk_soft_float_bind(&w.math.workers, &s_soft.context);
```

- **Data export.** The runtime reads exports, not the ELF.
  - `tools/export_sdk_math_tables.py` (STARTUP.md step 33) writes `assets/sdk_soft_float.emsf` next to the
    tables. Its layout is 'EMSF', u32 version 1, then two (address, word) pairs: (0x0024295C, D_0024295C's
    initial word) and (that word, the initial word of the cell it names).
  - The tool reads both from the user's ELF and requires them inside the LOAD segment's file image.
  - `em_sdk_soft_float_load_export` accepts only that layout: magic, version, first address 0x0024295C,
    the second address equal to the first word, and exactly 24 bytes.
  - The ELF gives 0x00242670 and 0. `test_sdk_soft_float_reference` part 9 asserts the file against the
    ELF and against `playable_ee.bin`. Part 7 asserts D_0024295C = 0x00242670 in every route snapshot.
  - `em_sdk_soft_float_load_d24295C` (the ELF reader) is kept for the oracle and contract tests.
- **Canonical storage.** D_0024295C and the errno word are .data.
  - The original initialises them only in the ELF image, so the storage is loaded once, by the first world
    load. An area build does not reset it (`em_collision_world_unload` leaves it alone).
  - 0011FD78 reads D_0024295C on every call.
  - `errno_address` equals the loaded D_0024295C. Any other value faults at 0x0011FD78 instead of writing
    somewhere else.
  - The errno word has no reader on the first-level path that this lane knows of. It is kept because it is
    the state the route snapshots show (section 5).
- **Fail-stop.** A missing or malformed export fails `em_collision_world_load`: its stderr line names the
  file, and the AREA11 build faults at 0x001AFCA0.
- **Slots.**
  - The four `EmSdkMathWorkers` slots are the only binding. They are used by
    `em_sdk_math_original_0011E620` / `_0011E748` and by their float adapters `_float_0011E620` /
    `_float_0011E748`.
  - The floor's `.atan2` / `.sqrt` (`em_collision_world_bind_player`) and the column's `.sqrt` run over this
    context.
  - The other atan2/sqrt rows of SDK_MATH_ORIGINAL.md section 7 are not bound yet: slide, climb, the
    `em_interaction_sdk_atan2` sites and the director. When each one is bound, its reference test must
    switch its SDK stand-in in the same commit (EE_FLOAT_MODEL.md, "Binding note for the lead").

## 5. Route measurement: the domain-error path is reached in beat 03

Each route snapshot `../Extermination/build/s87/route/<beat>/eeMemory.bin` is the whole EE RAM at the end of the beat.
In every one of them, D_0024295C = 0x00242670 and D_0026C5D0 = 1. The errno word at 0x242670 reads:

| Beats | errno word |
|---|---|
| first control (`playable_ee.bin`), 00, 01, 02 | 0 |
| **03_panel_power** (continuous from 02: counter 4989 → 4990), and 04 … 14 | **0x21** (EDOM) |

- **Who stores EDOM.** 0x21 is stored into the errno cell only by the domain-error tails of four wrappers: 0x11E4CC
  (0011E420), 0x11E5CC (0011E520), 0x11E6F4 (0011E620 atan2f) and 0x11E808 (0011E748 sqrtf).
  - The other callers of 0011FD78 store 5 or 0xC.
  - The functions that read D_0024295C directly contain no 0x21 constant.
  - 0011E420 and 0011E520 have the same shape as the two wrappers here: a kernel (0011BCF8 / 0011C128), fabsf, and
    then the same four workers. They are most likely acosf/asinf with an |x| > 1 domain error. Nothing in the port
    translates them yet.
- **Conclusion.** During beat 03 (Cross at the power panel with the battery, the BATTERY prompt, Yes, power on), the
  original ran one of those four error tails at least once. The tail runs the four workers of this module.
  **Option 2 of the SDK_MATH_ORIGINAL gate is refuted.** These workers are required, not optional.
- **Narrowed by the route census** (decomp `tools/route_census.py`; `../Extermination/build/s87/census/per_beat.json`).
  It armed a one-shot breakpoint on every boot function, per label.
  - 0011E420 and 0011E520 were armed and **never hit** on the route. So the tail was 0011E620's or 0011E748's,
    the two wrappers this module completes.
  - In the boot ELF, only the four wrappers call 0011DB90 and 00127758, and only on their error paths.
    Both ran, with 0011FD78, in beats **03 and 05**. Beat 05's tail left errno at 0x21.
  - The test asserts these facts when the census file is present.
- **Not identified.** Which of the two wrappers it was, and which call site, is not known. A PCSX2 replay of
  beats 03 and 05 with breakpoints on 0x11E6F4 and 0x11E808 would name the frame and the caller. This step
  does not run PCSX2.
  The route snapshots do not record errno per frame, and neither does `trace.json`.
- **Not covered.** Overlay code was not searched. It can reach the cell only through 0011FD78 or D_0024295C.
- **Asserted.** The test asserts the snapshot data, that the errno word is 0 or 0x21 in every beat, and that it
  first reads 0x21 at the end of beat 03 and stays there. It then replays the zero-vector and negative-root cases
  of part 6 over the beat 03 RAM (every beat in full mode), with the original wrappers and callees executing
  against that snapshot's .data.

## 6. Verification

`python3 tools/test_sdk_soft_float_reference.py`.

**The oracle.** It is the EE interpreter of `tools/test_sdk_math_original_reference.py`, imported and not edited.
The test subclasses it to record every store outside the running frames.
- It runs the user's ELF code over `playable_ee.bin`.
- Each entered function is checked byte for byte against `SCUS_971.12`.
- Every call must write only its own stack frame and the output it is allowed to write: the record for the
  unpackers, the errno word for the wrappers.

| Part | Quick (default) | Full (`EM_TEST_FULL=1`) |
|---|---|---|
| 00128350: 31 specials, every exponent × {0, 1, 0x100000, 0x400000, 0x7FFFFF} × sign, random words | 7,591 | 1,002,591 |
| 00127758: 33 specials, the narrowing edges (float-denormal/overflow exponents × lsb/round/tie/sticky/carry fraction patterns), in-range and random words | 6,991 | 1,206,177 |
| same, second stack fill + complemented a2 (asserted equal) | 3,655 | 552,198 |
| 001274B0 pairs (special × special, equal, ±1 ulp, sign flip, random) | 4,444 | 401,444 |
| leaves on arbitrary inputs (7 each): 001278C0 and 00126BE8 (all record bytes from a random fill), 00126AB8 / 001277B0 (arbitrary class, sign word, exponent incl. INT32_MIN/MAX, fraction; random a2), 00127398, 00127728, 00128320 | 3,200 | 320,000 |
| 0011DB90 records (returns 0, record unchanged, no foreign store) | 183 | 20,033 |
| 0011FD78 (D_0024295C as captured and 4 other values) | 5 | 5 |
| wrappers 0011E620/0011E748 executed whole vs `em_sdk_math_original` bound to this module (D_0026C5D0 −1, 0, 1, 2, 5) | 1,085 | 3,025 |
| the same cases over route RAM (zero vectors / negative roots, mode 1) | 71 (beat 03) | 7,344 (16 beats, 00..15) |
| fail-stop / loader / bind (in the shim) | 26 | 26 |
| the data export against the ELF and `playable_ee.bin`, its loader, and the route-RAM zero-vector / negative-root cases over a context built from the loaded export (part 9) | 73 | 461 |
| **total equal to the original** | **27,324** | **3,513,304** (2026-09-24, 27 s wall under load) |

The quick run takes about 1 s wall on the M1 (1.2 s and 0.95 s measured) and the full run 43 to 49 s (8 workers, under load). These are
timings, not assertions; only the counts are fixed.

**Negative controls.** Twenty-one single defects were injected through `EM_SDK_SOFT_FLOAT_SOURCE`. The quick run caught
all 17 that change behaviour:
- the float NaN-class bit;
- the double and float tie-to-even tests;
- the sticky bit;
- the float denormal limit 26 → 24 and the double limit 57 → 55;
- the float overflow limit;
- a guard shift added to the double NaN;
- the inf/inf compare order;
- `<` → `<=` on the exponent compare;
- a denormal float treated as a number;
- the float carry;
- the widening shift 30 → 29;
- the NaN fraction shifted in 00126BE8;
- 0011DB90 returning the compare;
- the double sign bit;
- the zero class.

The other four are equivalent, and the reasoning shows why:
- the float limit 26 → 25 and 26 → 27, and the double 57 → 56: a shift of 25/26 (56) followed by 7 (8) clears a
  32-bit (64-bit) fraction either way;
- the double carry test `<` → `<=`: no rounded fraction can equal 0x1FFFFFFFFFFFFFFF exactly.

**Contract test.** `tests/sdk_soft_float_test.c` checks:
- the section 2 values;
- the float round trip for 64 random fractions of every exponent;
- the loader;
- the wrapper protocol with this module bound: zero vector, denormal pair, negative normal and denormal square
  roots, mode −1, and a missing errno cell faulting at 0x0011FD78;
- the export loader: the well-formed layout, each defective header word, a short file, a trailing byte and
  NULL arguments;
- a 200,000-iteration sweep of every entry point on random records.

It is built with `-fsanitize=address,undefined -fno-sanitize-recover=undefined` and runs in under a second.

**Cross-check (reported, not asserted).** `em_player_recovery_below_0_6pi(x)` is the closed form that
em_player_recovery uses for 00128350 + 001000C0 (001274B0(d, 0x3FFE28C740000000) < 0). It equals this module's
chain on **all 2^32 words** (0 differences; a one-off native sweep, 23 s).

## 7. Makefile (for the lead)

Test targets, following the sdk-math-original pair:

```make
.PHONY: test-sdk-soft-float-reference test-sdk-soft-float
test-sdk-soft-float-reference:
	python3 tools/test_sdk_soft_float_reference.py

test-sdk-soft-float:
	@mkdir -p build/sdk_soft_float
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=undefined -Isrc tests/sdk_soft_float_test.c src/game/em_sdk_soft_float.c src/game/em_sdk_math_original.c -lm -o build/sdk_soft_float/sdk_soft_float_test
	build/sdk_soft_float/sdk_soft_float_test ../Extermination/config/SCUS_971.12
```

`src/game/em_sdk_soft_float.c` is in the game sources, and both targets are in the Makefile.

## 8. Findings for other lanes

1. **`EmPlayerLandWorkers.convert_00128350` / `.test_001000E0` (em_player_fall.h) lose the double's upper word.**
   - At 0x17C5CC, 0017C580 copies the whole 64-bit v0 of 00128350 into a0 for 001000E0. 001000E0 returns
     001274B0(a0, a1) ≤ 0.
   - The slots carry an `int`, and the fall oracle's harness truncates the same way (`s32` of v0 in
     `w_convert_00128350`). Only the low word, (m << 29) truncated, reaches the compare.
   - Measured on the original instructions: for +220 = 1.0f, 5.0f or 0x3F800001, 001000E0 of the real double
     returns **0**, and of the low word it returns **1**.
   - With +234 == 1 that selects `reset_to_reaction` where the original does not.
   - The fix: the slots should pass the `uint64_t` double. `em_sdk_soft_float_00128350` is the translation, and
     001000E0 is `em_sdk_soft_float_001274B0(d, 0) <= 0`. The fall oracle must change in the same commit.
2. **Other callers of 00128350** (float → double for a double compare), not assessed here: 0010D2C8, 00132490,
   00147B50, 001424C0, 0017C580, 0017D080, 00189EC0, 001A5C30, 001A7870, 0020D930.
   - em_player_recovery (0017D080) passes the cross-check in section 6.
   - em_coll_segment_walkers (001A5C30) uses static closed forms (`below_1e5` / `above_1e5`) that were not checked.
   - All of these can call `em_sdk_soft_float_00128350` and `_001274B0` directly.
3. **0011E420 / 0011E520** (the acos/asin-shaped wrappers) are untranslated, and one of the four EDOM sources in
   beat 03 (section 5). They use the same four workers.

## 9. Limits

- Beat 03's domain error is attributed to one of four wrappers, not to a call site (section 5).
- 001000C0, 001000E0 and 00100110 (the double-compare shims over 001274B0) are not part of this lane.
- The oracle's random sweeps are samples. The 00128350 domain is covered by every exponent with the edge fractions
  plus random words, not all 2^32 words through the interpreter. The 2^32 native sweep in section 6 is a
  cross-check of the recovery closed form, not an oracle run.
