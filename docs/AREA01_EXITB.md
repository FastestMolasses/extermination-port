# AREA01 lane EXITB: the exit-only boot functions (six subsystems)

Level 2 (AREA01) side track, lane EXITB, 2026-09-26 (session s87). New files only:
`src/game/em_area01_exitb.{c,h}` (prefix `em_area01_exitb_`) and
`tools/test_area01_exitb_reference.py`. The module is built and tested but not wired:
nothing in the port calls it, the Makefile does not build it, and wiring waits for the
AREA01 load and the AREA00 arrival (AREA01_OVERVIEW.md section 11).

## 1. Scope

The rows of `../Extermination/build/s87/census/a01_delta.json` (`new_functions`) with
`exit_change_or_area00_only` set, region boot, in the census subsystems entity_logic,
math_vector, hud_objects, frame_update, anim_runtime and obj_registry: 16 functions,
8,920 bytes. All first ran in beat a01_07 after the area change to AREA00 (census frames
f531/f532, 001C6160 at f755), that is in the AREA00 load and arrival, not in AREA01 play.
The subsystem names are census labels, not evidence; the functions are named by address.

| Function | Bytes | Decomp | Census subsystem | First (a01_07) | Captured records at the end of a01_07 |
|---|---:|---|---|---|---|
| 00156F30 | 1072 | NEARMISS | entity_logic | f531 | 3 pool nodes with callback 00156F30 (state 4) |
| 001576E0 | 384 | byte-matched | entity_logic | f532 | (callee of 00158810 and others) |
| 001581A0 | 316 | NEARMISS | entity_logic | f531 | 1 node (state 1) |
| 00158810 | 948 | NEARMISS | entity_logic | f531 | 1 node (state 1, +0x20 set) |
| 00158BD0 | 340 | NEARMISS | entity_logic | f531 | 1 node (state 1) |
| 0015AB00 | 244 | byte-matched | entity_logic | f531 | 8 nodes (state 1, halfword +0x0E = 0..7) |
| 0015B030 | 248 | asm (words) | frame_update | f531 | 1 node (state 1, +0x20 set) |
| 001BB520 | 56 | byte-matched | math_vector | f531 | - |
| 001C2430 | 148 | byte-matched | math_vector | f532 | - |
| 001C2540 | 152 | asm (words) | math_vector | f532 | - |
| 001C3DB0 | 764 | NEARMISS | math_vector | f532 | - |
| 001C6160 | 44 | byte-matched | anim_runtime | f755 | (records with a +0x40 table: the 00128C10 / 0012A5D0 nodes) |
| 001D0400 | 172 | asm (inline) | obj_registry | f531 | - |
| 001E8E80 | 1012 | NEARMISS | hud_objects | f531 | (callee of 0015AB00 state 0) |
| 001E9280 | 756 | NEARMISS | hud_objects | f532 | (callee of 0015AB00 state 1) |
| 001E9580 | 2264 | byte-matched | hud_objects | f531 | - |

**Overlaps with existing port code** (grep of `src/game` and `tools` for every address;
no verified translation of any of the 16 existed, so all are translated here):
- 00156F30 is named in `em_player_floor.h` only as a behaviour value its floor test
  compares with (0x00156F30 / 0x00827880 / 0x00828700); not a translation.
- 00158BD0 is named in a comment of `em_area01_render_gs.h` (a caller of 001F4A10).
- 0015B030 is compared as a callback value in `em_interaction_scene.c` (line 58).
- 001BB520 is called through a worker by `em_area01_math_owner.c` (call1, state 0 of its
  owner) and described in a comment of `em_door.c`.
- 001C2540, 001C3DB0 and 001C6160 are called through workers by `em_area01_math_actor.c`
  (001C2540's result is used, which is why this module returns it); the parallel lane
  EXITA's `em_area01_exita.c` calls 001C2430 (testing its v0), 001C2540 and 001C3DB0
  and 001D0400 (with a0 = 0x823430, a1 = 0x26A900) through workers. Neither lane edits the
  other's files; those workers can bind to the entries here.
- 001E9580 is called through a worker by `em_area01_sys.c` (0015A2C0 / 00159B90) and is
  described in comments of `em_enemy.{c,h}` (its colour set is quoted there as claims).
When the lead wires these, those workers can bind to the entries here.

## 2. Model

- **Memory.** Original EE memory (RAM and the scratchpad) through caller-supplied regions
  keyed by original address. None of the 16 routines hands the address of a stack local to
  a callee (every work area is in the scratchpad), so the module never addresses the stack.
- **Calls.** Every call leaving the module goes through the one worker `call` with the
  callee's address (for the +0x4C handler, the pointer the original loads, after the call
  before it), the stack pointer at the call (entry `sp` less each original frame: 0x40,
  0x30, 0x20, 0x20, 0x20, 0x20, 0x40, 0x20, 0x20, 0x40, 0x60, 0x10, 0x30, 0xA0, 0xA0, 0x90
  in table order), and the argument registers the callee reads (`na` / `nf`; 64-bit
  images, 32-bit values sign-extended as the original's register operations leave them).
  The worker returns v0 (compared as the full register where the original tests the full
  register) and f0. Translated callees are called directly: 001576E0 from 00158810,
  001E8E80 and 001E9280 from 0015AB00.
- **Arithmetic.** Every float operation is the EE operation the original executes, through
  `em_ee_float.h`: in particular the multiply-add pairs of 001C3DB0 (dot product and squared
  length) and of the lattice distance in 001E8E80 / 001E9580 are `em_ee_mula/adda` +
  `em_ee_madd`, as in the original. Under the port's model (em_ee_float.h) ADDA / MULA give
  exactly the ADD / MUL result, and MADD adds the product truncated but not saturated; the
  truncation is the same as MUL's, so MADD differs from MUL then ADD only when one of its
  two multiplied operands has exponent 255 (an Inf or NaN pattern: the raw product passes
  it to the sum, MUL would saturate it to +-MAX first). Where that cannot happen the two
  forms are bit-identical (section 5.1).
- **Results.** 001576E0 returns 0/1/2 (int32), 001C6160 the halfword (uint32).
  001BB520, 001C2430 and 001C2540 leave the last callee's v0 (001B0FD0's / 0019AB20's /
  0019B4C0's) untouched in v0, all 64 bits; their optional output is a `uint64_t` holding
  that full register, because a caller may test the whole register (lane EXITA's owner
  tests 001C2430's v0 against 0). 001BB520 and 001C2430 are void in the decomp C; the
  original's v0 is still that callee's.
- **Fail-stop.** An address outside every region, a NULL worker, a negative worker result,
  a NULL region list: the first fault is latched (code, entry, address or callee), the call
  returns -1, and every later call returns -1 before any work until
  `em_area01_exitb_clear_fault`. A NULL context returns -1 without latching. Bytes written
  before the faulting access stay written. No routine here reads a value it never set on
  any input (no UNDEFINED path was found), so there is no UNDEFINED fault.
- **Store trace (test builds only).** `EM_AREA01_EXITB_STORE_TRACE` names a function that
  receives every store (address, size); `wr` / `st64` are the only store paths.

## 3. What each routine does

Full statements are in the comment above each translation; short forms:

- **00156F30(p)**, by state byte +4. 0: when 001B0FD0(p) returns 0: byte +0 = 1,
  001C6380(p), the four quadwords at +0x90 of the records D_00275B40[0] and [1] (the
  pointer word re-read for each) copied to p+0x1F0 and p+0x230, state 4. 4: when halfword
  +0x36 is set, the swing restarts; then 001B17A0(p) and the +0x4C handler. 1: angle
  +0x2E4 += +0x2E0, wrapped to -pi when not <= pi, else to pi when < -pi;
  0x70003A20 = +0x2E8 * 0011E2A8(angle); 0x700036A0 = identity rotated by 00102A60 with it;
  001C6380(p); both records' +0x90 matrices multiplied by it (001026D0, in place);
  (4 * 0011E398(0x70003A20), 0, 0, 0) through record [1]'s matrix into 0x700038B0, whose
  x and z are subtracted from record [1]'s +0xC0 / +0xC8; +0x2E8 -= 0.001 and state 4 below
  0; restart when +0x36 is set; then 001B17A0(p) and the handler. 3: 001AFC10(p). Others:
  nothing. The restart: state 1, +0x2E8 = 0.2, (+0x2E4 = 0 from state 4 only), +0x36 = 0,
  0x700038A0 = (p+0xB0) - D_00810350 (001028D0 computes its second argument minus its
  third, and the call passes p+0xB0 then 0x810350), rotated by the identity turned with
  -(+0xC4) (00102BB0), step +0x2E0 = 0.08 when the rotated x is below 0, else -0.08.
- **001576E0(a0, a1)**: 0 unless bit 2 of byte +0x0B. Scratch 0x700038A0 = (0.3, 0, 5, 1),
  001B6F00(a0, 0x700038A0; pi), byte +0 = 2. Flag = D_00810C87 for type +3 in {0x12, 0x2F},
  else D_00810C88. Set: D_00246CB4 = 0x154, 001BA1A0(a1, 0x246C20), 001BA1F0(a0), result 1.
  Clear: D_00246FB4 = 0x80000014 (0x12/0x2F) or 0x80000016, 001BA1A0(a1, 0x246F20),
  001BA1F0(a0), result 2.
- **001581A0(p)**. 0: when the bit (1 << (halfword +0x2E & 31)) of D_00810841[D_00810700] is
  set (low 8 bits), state 3; else 001B0FD0(p), 001C6380(p), byte +0 = 1, 001F1110(p, 2).
  1: when +0x36 is set: bytes +0, +4 = 2, the bit (byte +0x2E) set in that byte,
  001FB9F0(0x3F1, 0x1000, 0x1000, 0x1000); then 001F1180(p), 001B17A0(p), handler.
  2, 3: 001AFC10(p). Others: nothing.
- **00158810(p)**: statement in the module comment (states 0..3, sub-state +5, 001576E0 with
  p+0x1F0, the scratch timer halfword 0x70003B84 and byte 0x70003B91, D_0081076A,
  001C5570's result kept at +0x20).
- **00158BD0(p)**. 0: 001B0FD0(p). 1: 001C6380(p); the handler when 001B17A0(p) is nonzero;
  0x700038B0 = (0, 0x80, 0, 0x80) when the area bit (halfword +0x2E) is set, else
  (0x80, 0, 0, 0x80); 0x700038C0 = (1, 1, 1, 1); 001F4A10(p+0xD0, 0x700038B0, 0x700038C0).
  2, 3: 001AFC10(p).
- **0015AB00(p)**. 0: row = 0x248290 + 20 * (signed halfword +0x54); +0x30 = row, +0x34 =
  0x15AAF0, bytes +0x0C, +9 = 0, +0, +4 = 1; 0x700038A0 = 2 * row[0], 0x700038A8 =
  2 * row[2]; 001E8E80(p, halfword +0x0E, 0x700038A0). 1: 001E9280(p), 001B17A0(p).
  2, 3: 001AFC10(p).
- **0015B030(p)**: q = word +0x20 read once at entry. 0: when 0015AC00(p) is 0 and byte q+4
  >= 2, state 3. 1: byte q+4 < 2: p+0xB0 = q+0xB0 + p+0xA0 (001028B8), +0xBC = 1.0,
  001C6380(p), 0015AE20(p, p+0x1F0); else state 3. 2, 3 and 4..255: 001B1190(byte +0x9A),
  001AFC10(p).
- **001BB520(a0)**: 001B0FD0(a0); +0x30 = 0x2755E8; halfword +0x34 = byte +0x2E; halfword
  +0x2E = 0.
- **001C2430(a0, a1, a2)**: 0x700038C0 = a2 * a1 (001026A0) + (a0+0xB0) (001028B8);
  0x700038D0 = (0, -10, 0, 0); 0019AB20(a0, 0x700038C0, 0x700038D0, 0x80000007).
- **001C2540(a0, a1, a2, a3)**: 0x700038C0 = a3 * a1 + (a0+0xB0); 0x700038D0 = a3 * a2;
  result 0019B4C0(a0, 0x700038C0, 0x700038D0, 0x80000006).
- **001C3DB0(a0, a1, a2, a3)**: rotates a2 into a3 about a0 x a1 by the angle
  0011E620(sqrt(|a0 x a1|^2), a0 . a1) through a basis built in 0x70003400.. (early out:
  a3 = a2 when every component of a0 x a1 compares equal to 0); a3 then normalised. The
  rotation 0x70003480 is the identity with rows 1 and 2 = (0, cos, sin) and (0, -sin, cos)
  of the angle (stores: +0x14 and +0x28 = 0011DE90(ang), the cosine; +0x18 = 0011E2A8(ang),
  the sine; +0x24 = its negation).
- **001C6160(a0)**: D_00275BF8 = 001C6120(word +0x40, signed halfword +0x2C); result the
  halfword at D_00275BF8 + 2 (re-read after the store).
- **001D0400(a0, a1; k)**: 0x90 bytes a1 -> a0 (00121870); a0+0x00 / +0x10 scaled by k
  (00103230), a0+0x40 / +0x50 (00102900), floats +0x78, +0x7C, +0x84 *= k, word +0x80 =
  001281C0(float(word +0x80) * k).
- **001E8E80(a0, a1, a2)** and **001E9580(a0, a1, a2)**: the 8 x 8 lattice set-ups of an
  0xA060-byte record at D_00275C18 (resp. D_00275C1C) + a1 * 0xA060, statements in the
  module comment. 001E9580 selects one of 17 rows by (D_00810700 << 8) + D_00810701
  (the byte-matched C's switch; unlisted keys take the default row).
- **001E9280(a0)**: packet build from the D_00275C18 record selected by halfword +0x0E: six
  0x1A-quadword blocks (three lattice rows each), a 5-quadword block from 0x70003AC0, a
  9-quadword block, then 001CB950 / 001CB6B0 / 001CB760 (statement in the module comment).

## 4. How it was verified

`tools/test_area01_exitb_reference.py`. Default run (section 5 says what it holds):
about 6 s of CPU (user + sys) and 4 s wall with the default 2 worker processes.
`EM_TEST_FULL=1 EM_TEST_JOBS=4`: about 65 s of CPU, 17 to 25 s wall.

**Oracle.** FallEE (the shared interpreter with the measured COP1 / VU0 model), extended in
the test by the two MMI word interleaves 00102798 needs (the shared core lacks them; a
transpose property check guards the extension), runs the ORIGINAL instructions over the
end-of-beat capture `../Extermination/build/s87/route_a01/a01_07_level_exit/` (AREA00
resident, the state these functions run in). The native module runs over a copy of the
same bytes. An address outside the 32 MiB stops the oracle (the shared core would fold it
onto RAM).

**Callees.** Run as original on both sides (the native side in a second interpreter over
the native memory): the vector / matrix leaves 00102948, 00102958, 001026A0, 001026D0,
00102718, 00102760, 00102798, 001028B8, 001028D0, 00102900, 001029C0, 00102A60, 00102BB0,
00103230, 00121870; the math leaves 0011DE90, 0011E2A8, 0011E398, 0011E620, 0011E748; the
LCG 00122BB8; 001281C0; 001C6120; the packet allocators 001CB5F0, 001CB950, 001CB6B0,
001CB760. Stubbed (recorded, scripted result): 001B0FD0, 001C6380, 001B17A0, 001AFC10,
001B6F00, 001BA1A0, 001BA1F0, 001F1110, 001F1180, 001FB9F0, 001C5570, 001F4A10, 0015AC00,
0015AE20, 001B1190, 0019AB20, 0019B4C0 and the +0x4C handler. The test asserts every
direct call target of the 16 routines has a policy.

**Register reads.** RegScan (imported unchanged from `test_area01_sys_reference.py`)
measures what each callee reads before writing; the test fails if any callee reads an
argument register its policy does not compare. Measured: every stub reads only a0..a(n-1)
of its policy (001F4A10 reads a0, a1 of three compared; 001B0FD0, 001AFC10, 001BA1F0,
001C6380, 001B17A0, 001B1190, 0015AC00 read a0 only). Two limits: RegScan treats a0..a3 as
clobbered after a call, so it reports no integer reads for 00102A60 / 00102BB0 (whose
body reads a0 / a1 after calling 001029E8); the policy compares both anyway. And
00102A60 / 00102BB0 read vf4.yzw inside 001029E8 before writing them, and 00102718 reads
vf6.w: in 001029E8 those lanes are multiplied by 1 and then overwritten, and 00102718's
w lane is subtracted from itself, so the results do not depend on them; both sides start
each run callee with the same VU0 state.

**Lockstep.** As lane SYS: at every call leaving the module the native side must match the
callee, `sp`, every compared argument register (and every register the native says it
sets must hold the original's value), and all of RAM and the scratchpad (the lines either
side stored to since the previous check). The same check after the last store, then all
32 MiB, the scratchpad, the call log and the result.

**Cases** (467 ordinary; the default run takes 147, section 5):
- captured: every captured owner record as is (3 x 00156F30, 00158810, 001581A0, 00158BD0,
  8 x 0015AB00, 0015B030), records with an animation table for 001C6160;
- every state value (including unused ones: 2, 4, 5, 0xFF), every sub-state 0..5 of
  00158810 in states 1 and 2, both sides of every test: area index and bit numbers
  (0, 2, 7, 8, 9, 10, 17, 33, 0x107: the five-bit shift and the byte truncation), type bytes
  0x12 / 0x13 / 0x2F / other, the flags D_00810C87 / D_00810C88 / D_0081076A, stub results
  0 / 1 / -1, the timer halfword at 0x64 / 0x77 / 0x78 and the byte 0x70003B91 at 0..3,
  angles at +-pi (both wraps), the 0.001 timer through 0, the restart trigger, 0x700038A0's
  sign;
- vectors: 001C3DB0 with perpendicular, parallel, antiparallel, zero and signed-zero
  pairs plus random pairs, a z component with exponent 255 (the multiply-add edge), and
  three in the call shape lane EXITA's owner uses (a1 =
  0x70003610, a3 = 0x70003620, next to the routine's own scratch vectors); 001C2430 /
  001C2540 random matrices, and stub results with the upper 32 bits of v0 set
  (0x100000000, 0xFFFFFFFF00000000) for 001BB520 / 001C2430 / 001C2540; 001D0400 with k = 1, 0.5,
  -2, 0, 1e-30, 3.3e38, 1.25 and integer words 0 / 7 / -12 / 100000 / 0x7FFFFFFF;
- 001E9580 with all 21 table keys plus unlisted ones (0xB00, 0x301, 0x1302, 0xFFFF), LCG
  seeds random, record index 0..7; 001E8E80 / 001E9280 directly and through 0015AB00.

**Variants** (2,499; the default run takes 52):
- *fx after / fx before* (lane SYS's two kinds) for every case whose routine makes a stubbed
  call: at each stub the fields the translated routines load after it (resp. loaded since
  the previous stub) change, identically on both sides;
- *poison*: every byte the routines store before they load it starts as the complement of
  its final value, so a missing store, or one that stored a value that was already there
  (the capture's records are already initialised), differs;
- *prestore*: at each stub, the bytes the routines store after it (before the next stub)
  are set to the complement of what they store, so a store left out after a stubbed call
  differs;
- *edge-byte*: the byte after every byte / halfword the routines load or store (when they
  touch it in no other way) is 0xA5, so a wider access differs; *edge-top*: the top bit
  of every byte / halfword they load is flipped, so a sign change differs;
- *sweep 1* cases (`cases_sweep1`): the inputs added for the survivors of the first
  mutation round (section 5); *review 2* cases (`cases_review2`): the inputs added in the
  fix round (section 5.1);
- *handler*: 001B17A0 moves the +0x4C handler to a second stubbed address, so a translation
  that loads the handler before that call calls the wrong one (00156F30 states 1 and 4,
  001581A0, 00158810 states 1 and 2, 00158BD0).

**Branches.** Both outcomes of all 108 conditional branches in the 16 routines are taken in
the default run and asserted in `EM_TEST_FULL=1` (no unreachable outcome).

**Result.** Every case and variant matches in both modes. The full run compares 38,941
calls and 6,314 stub-side field changes; 84 variants (a flipped pointer sending the
original outside RAM) are counted as not comparable. Results are compared as the entry
returns them: 001576E0 as int32, 001C6160 as uint32, and 001BB520 / 001C2430 / 001C2540
as the original's full 64-bit v0.

**API.** NULL context, NULL regions, NULL worker, a failing worker (fault 2 naming the
callee, no further call, refusal while latched), clear_fault (also NULL), and unmapped
accesses reported at their address with the entry, before any call or output: 0015B030 at
+0x20 of a record at the region's end (0x2010), and 001C6160 twice, once where its first
load (+0x2C) is outside the region (0x201C) and once where only its second (+0x40) is
(0x2010), which also pins the order of the two loads.

## 5. Single-operation mutants

One bounded sweep (this track does not loop sweeps until dry). A scratch driver (not
committed; `build/area01/exitb/mutate.py`) generated every single-operation edit of the
translation bodies of `em_area01_exitb.c` of these kinds: each hex / decimal literal
(hex value xor 1, decimal + 1), each relational operator (== / != / < / <= / > / >=),
`&&` / `||`, the EE operation (add / sub, mul / div, c.lt / c.le, madd / msub,
mula / mul, adda / add, a removed negation), the access width or sign (u8 / u16 / s16,
st8 / st16), the deletion of each store or call statement, and the swap of each
statement with the next. Each mutant was built through the test's
`EM_AREA01_EXITB_SOURCE` hook (without -Werror) and run fail-fast on the cases of the
entries that reach the edited line; survivors were rerun in `EM_TEST_FULL=1`; the
survivors of the first round led to the targeted cases of `cases_sweep1` (and to the
edge / top / prestore variants and the refined poison variant), and the whole set was
then run again against the final default run.

**1,612 mutants: 1,373 killed, 231 survive, 8 are not C** (deleting the single statement
of an unbraced `if` / `else` arm). Killed by kind: literal 833 of 889, operator 193 of
227, deletion 272 of 282 (+8 not C), swap 75 of 214.

**The default run** is exactly the case or variant that killed each of the 1,373 mutants
(the first difference in the fail-fast order; `QUICK_PINNED`, 160 names), the targeted
cases (handler, sweep 1), the first captured record of each routine, and 13 cases that
take the remaining branch outcomes (`QUICK_BRANCH`, a greedy cover), plus one 001C3DB0 case
in EXITA's call shape (added after the sweep) and the 14 review-2 cases (section 5.1):
147 of 467 cases and 52 of 2,499 variants, about 6 s of CPU and 4 s wall. The final sweep
ran against the default run as it stood before those two later additions and killed the
same 1,373 (1,344 with -Werror builds plus the 29 that only build without it); the later
cases only add to it.

**The 231 survivors, by the rule that makes each equivalent** (the list with mutant
numbers is `build/area01/exitb/survivors_classified.json`, regenerated by the driver):

| Rule | Count | Why no input can tell them apart |
|---|---:|---|
| identical definition | 5 | `em_ee_adda_bits` / `em_ee_mula_bits` are defined as add / mul; `sizeof k_9580[1]` = `sizeof k_9580[0]` |
| register slot the contract leaves undefined | 17 | a changed a/f register beyond `na` / `nf` (the worker reads only the first `na` / `nf`) |
| truthiness or unused field | 18 | the 001E9580 row flag 1 -> 2 (still true), the default row's key (never compared), `swing_restart`'s flag 1 -> 2 |
| width or sign the use masks away | 16 | a 16-bit read whose value is then masked to a byte (`& 0xFF`, `& 4`, a byte store of `+ 1` or `|`), a shift count `& 31`, a sign change compared only for != 0 or == a small value or `>= 0x78u` (the signed value converts to a huge unsigned exactly when the unsigned one is >= 0x8000) |
| store of the value the byte already holds | 12 | 00158810 sub-state 2 storing 2 into +5 (the branch tested +5 == 2), a halfword store at +4 writing 0 into +5 where +5 == 0 by the branch, and 001C3DB0's five stores into 0x70003480.. of exactly the values 001029C0 (the original identity leaf, run as original) wrote there just before |
| dead store | 5 | 001E8E80's first +0x38 / +0x3C stores, overwritten before any read (no call or load between) |
| dead store unless records alias | 11 | 001E9580's default +0x40 / +0x48 / +0x4C / +0x50, overwritten by the row stores; between them only a0+0xB0.., a2 and D_00810700/701 are read |
| threshold between fixed values | 6 | the lattice height t depends only on (i, j): its 64 values are (3 - sqrt(d)) / 4 for d in {0, 1, 2, 4, 5, 8, 9, 10, 13, 16, 17, 18, 20, 25, 26, 29, 32}; none lies within 0.04 of 0.1, 0.15 or 0.35, and at t = 0 (d = 9) both sides of the zero test give +0 |
| independent statements swapped | 96 | two stores to different fields with no load, call or aliasing between them, or two loads |
| read moved across a store to another field | 37 | differs only when the loaded and the stored field overlap (records aliasing); the cases have no such aliasing (lane SYS's round-6 aliasing variants were not built here: section 7) |
| reorder across a run callee | 2 | 00156F30 reads D_00275B40 before / after 0011E398 (writes nothing in RAM) and 001026A0 (writes only 0x700038B0) |
| only for records outside RAM | 2 | the record index as a signed halfword: differs only for +0x0E >= 0x8000, where both record addresses are outside the 32 MiB |
| undefined C or the test hook | 4 | `st64` writing a ninth byte (a shift by 64, undefined in C), its span / trace sizes (only the test's fault range and dirty-line list) |

### 5.1 Fix round after the second review

The second review ran its own bounded sweep of 46 single-operation mutants over all 16
routines: 40 killed by the default run, one (m08) only by `EM_TEST_FULL=1`, five surviving
both. No new sweep was run in the fix round; the scratch driver
`build/area01/exitb/fix/fixmut.py` (not committed) ran only the mutants below, each one
exact replacement in a copy of the module, fail-fast on the default run
(`EM_AREA01_EXITB_ONLY` = the entries that reach the edit; results in `fixmut.json`).

| Mutant | Edit | Outcome |
|---|---|---|
| m26 (review) | 001C3DB0 dot product as MUL then ADD instead of ADDA + MADD | **killed** by the pinned `1C3DB0 dot inf z`: at call 8 (0011E620) the original's f13 is 0x7F7FFFFF, the mutant's 0 |
| m26len (sibling site) | 001C3DB0 squared length as MUL then ADD | survives; **equivalent**, proof below |
| m33 (review) | 001E8E80 lattice distance as MUL then ADD | survives; **equivalent**, proof below |
| m08 (review) | area bit shifted by `b & 7` instead of `b & 31` | **killed by the default run** now: `1581A0 s0 shift-mask b9` (also the 158BD0 / 158810 shift-mask cases) |
| m09, m46 (review) | the bit number read as a halfword instead of the byte (m46 also without `& 0xFF`) | survive; **equivalent**: the shift uses only the low five bits, which the byte and the halfword share (little-endian), and `area_bit_set` returns a byte, so `& 0xFF` is redundant (rule "width or sign the use masks away") |
| m40 (review) | 001E9580 height limit 0.15 -> 0.1 | survives; **equivalent** (rule "threshold between fixed values"): a lattice t in (0.1, 0.15] needs sqrt(d) in [2.4, 2.6), d in [5.76, 6.76), and d is a sum of two integer squares (5 and 8 bracket that interval) |
| v0 width x 3 | 001BB520 / 001C2430 / 001C2540 returning v0 cut to 32 bits | **killed** (`1BB520 0 0`, `1C2430 0`, `1C2540 0`: the negative stub results' sign extension; the `v0 hi` cases also differ) |
| 001C6160 load order | +0x40 loaded before +0x2C | **killed** by the API check (fault at 0x2030 instead of 0x201C) |

Why the first sweep missed m26: its EE-operation edits swapped madd / msub, mula / mul and
adda / add, but never replaced a MADD with a separate MUL and ADD.

**The multiply-add rule** (from `em_ee_float.h`): `em_ee_adda_bits` / `em_ee_mula_bits` are
the ADD / MUL results. MADD is `ee_sum(acc, raw product)` and MUL is `saturate(raw
product)`. For finite operands the raw product is the exact product packed with
truncation, which is +-MAX on overflow and a signed zero on underflow: never exponent 255
and never a denormal, so saturate leaves it unchanged and MADD equals ADD(acc, MUL)
bit for bit. They can differ only when a multiplied operand has exponent 255.
- m33: the lattice operands are `cvt.s.w` of i - 3 and j - 3 (|value| <= 4), never exponent
  255, so MADD = ADD(acc, MUL) always. The same holds for 001E9580's lattice distance.
- m26len: acc = ADDA(sx * sx, sy * sy) is an ADD result (never exponent 255) and >= +0 (a
  square's sign is positive). For finite sz the rule above gives equality. For sz with
  exponent 255, sz * sz is +Inf or a NaN, so MADD gives +MAX (ee_sum with an Inf / NaN
  operand); MUL gives +MAX, and ADD(acc, +MAX) with acc >= +0 is +MAX (it overflows or is
  truncated back to +MAX). Equal on every input.
- m26: acc = ADDA(ax * bx, ay * by) can be any sign, so with az of exponent 255 and bz
  finite nonzero, MADD gives +-MAX by the sign of the Inf product, while ADD(acc, MUL)
  gives acc +- MAX, e.g. 0 for acc = -MAX: the pinned case above.

## 6. Decomp NEARMISS C that differs from the original (for the lead's FINDINGS)

The translations follow the original instructions (the `build/.asmnorm` listings of the
pinned ELF), checked by the oracle above. Where the committed NEARMISS C says otherwise:

- **00158810** (`src/func_00158810.c`), five differences:
  1. state 0 stores `&D_700038A0` at +0x20; the original stores 001C5570's return value;
  2. state 1 sub-states 1 and 3 and state 3 pass `arg0 + 0x1F0` to 001BA1F0 / 001AFC10;
     the original passes the record itself (a0 unchanged; RegScan: both read a0 only);
  3. state 2 dispatches sub-states 4 / 1 / 2; the original tests 2, 1 and 0, and it is
     sub-state **0** that calls 001576E0 (sub-state 4 does nothing there);
  4. state 2 sub-state 2 ORs the bit into `D_00810841` itself; the original ORs it into
     `D_00810841[D_00810700]`, and takes the bit number from the **byte** +0x2E (state 0
     tests the halfword);
  5. 001B0FD0 is declared (a1, 1, 2); the original's a0 is the record, which is all
     001B0FD0 reads.
- **00156F30** (`src/func_00156F30.c`): state 0 calls `func_001B0FD0(1)` and state 3
  `func_001AFC10(3)`; the original passes the record in a0 to both.
- **001581A0 / 00158BD0**: `func_001B0FD0()` and (00158BD0) `func_001C6380()` are declared
  without arguments; the original's a0 is the record and both callees read it.
- **001C3DB0** (NEARMISS; a note for port translators, not a correction of the C): the C
  writes the dot product as `ax * bx + ay * by + az * bz`; the original instructions
  evaluate it as ADDA of the first two products, then MADD of the third. The rounding is
  the same (both truncate); the two differ only when a0.z or a1.z has exponent 255, where
  MADD passes the raw Inf / NaN product to the sum and a separate MUL saturates it first.
  Pinned case `1C3DB0 dot inf z` (a0 = (-2e19, 0, bits 0x7F800000), a1 = (2e19, 0, 1)):
  the original's dot, passed to 0011E620 in f13, is 0x7F7FFFFF (+MAX); MUL then ADD gives 0
  (-MAX + MAX). A port translation of that expression therefore needs `em_ee_madd`. The
  squared length in the same routine and the lattice distances of 001E8E80 / 001E9580 are
  bit-identical either way (section 5.1); the earlier claim that the multiply-add rounds
  differently from a separate multiply and add was wrong and is withdrawn.
- **001576E0** (byte-matched): its comment says "bit 4"; the code (and the original) test
  bit 2 (mask 4). Comment only.
- 001E9280's corrected body (commit 837d548) agrees with the original on every case.

## 7. Known gaps

- Inputs are the one exit capture (plus edits); no AREA00 frame was replayed. The records
  these functions run on in AREA00 are the ones captured at a01_07's end (f4546); the
  states 0 / 2 / 3 paths are reached by edits, not by captured frames.
- Stubbed callees' behaviour is outside this lane (001B0FD0, 001C6380, 001B17A0,
  001AFC10, 001B6F00, 001BA1A0, 001BA1F0, 001F1110, 001F1180, 001FB9F0, 001C5570, 001F4A10,
  0015AC00, 0015AE20, 001B1190, 0019AB20, 0019B4C0, and the handler 001CAA00).
- A read hoisted across one of the module's own stores or a run-policy callee's stores is
  seen only where records alias in the cases (no aliasing variants, unlike lane SYS round 6).
- 00158810 reads the scratchpad byte 0x70003B91, which the port keeps canonical in
  EmSceneState (`spad3B91`, test_scene_no_shadow.py): when wired, the region for
  0x70003B91 must be the canonical byte. The module only reads it; no REACHERS / WRITERS
  entry is needed (the scene no-shadow test passes with these files).
- Stubbed results were 32-bit sign-extended values except in the `v0 hi` cases; the full
  64-bit v0 of 001BB520 / 001C2430 / 001C2540 is now returned and compared, so a caller
  that tests the whole register (lane EXITA's owner on 001C2430) can bind to it directly.
- Not added to the Makefile, the suite or README (lane isolation); the lead adds a make
  target (`python3 tools/test_area01_exitb_reference.py`).
