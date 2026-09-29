# AREA22 new functions (level-3/4 side track lane A22T)

Lane A22T, 2026-09-28. It covers the 11 boot functions the AREA22 route
census found new (decomp `build/s87/census/a22_delta.json`, `new_functions`;
main line 6, side beats 5; SIXTH_LEVEL_ROUTE.md section 9) and the AREA22
area init at runtime 0x823580. Eight of the 11 had no port translation and
are translated here, with the init; the other three have verified
translations in other modules and are reused (section 0). All nine
translations are compared with the original instructions by
`tools/test_area22_port_reference.py`, which also runs the three reused
translations against the original on the AREA22 drum prims. What that
comparison covers is exactly what section 3 states: the comparisons the
test makes, on the cases it runs. Nothing is bound: no port code calls
these entries yet.

| File | What |
|---|---|
| `src/game/em_area22_port.h` | public API: 9 entries, the hook table, fault codes |
| `src/game/em_area22_port_internal.h` | memory view, fault latch, one typed wrapper per hook |
| `src/game/em_area22_port.c` | the 9 translations |
| `tools/test_area22_port_reference.py` | original-instruction oracle and comparison; the reuse checks |

## 0. Scope: the census rows, and what already existed

A grep of the port (`src/`, `tools/`) for each address, before this lane:

| Row | Bytes | Decomp | Beat (census) | Port before this lane | Here |
|---|---:|---|---|---|---|
| 00100110 | 32 | C, byte-identical | a22_00 f160 | inlined as `above_1e5` in `em_coll_segment_walkers.c` (a static helper of 001A5C30, not an entry) | translated (entry) |
| 001028E8 | 20 | inline asm | a22_00 f83 | inlined as `vmul4` / `vu_mul` in `em_coll_move_original.c` / `em_coll_segment_walkers.c` | translated (entry) |
| 001A44B0 | 412 | word asm | a22_00 f83 | `em_coll_probe_001A44B0` (`em_coll_probe_original.c`, docs/COLL_PROBES.md, oracle-verified over AREA11 RAM and synthetic prims) | **reused**, re-checked on the AREA22 prims |
| 001A4830 | 1,248 | word asm | a22_00 f83 | `em_coll_move_prim_001A4830` (`em_coll_move_original.c`, docs/COLL_MOVE.md, oracle-verified) | **reused**, re-checked |
| 001A5C30 | 2,056 | NEARMISS | a22_00 f108 | `em_coll_segment_001A5C30` (`em_coll_segment_walkers.c`, docs/COLL_SEGMENT_WALKERS.md, oracle-verified) | **reused**, re-checked |
| 00183010 | 116 | C, byte-identical | a22_01 f272 (also a22_s1) | only a hook: `w_00183010` of `em_area04_port.h` (001C1A80's push) | translated |
| 0012D940 | 1,064 | C, byte-identical | a22_s1 f89 | only a call site: `em_area01_exita.c` (0012A5D0's +5 = 10 / 11 step) | translated |
| 001963A0 | 1,476 | NEARMISS | a22_s1 f427 | only a worker slot: `w_001963A0` of `em_camera_leftovers.h` (NULL: faults) | translated |
| 0018C850 | 204 | word asm | a22_s1 f428 | none | translated |
| 0018C920 | 360 | C, byte-identical | a22_s1 f428 | none | translated |
| 001BB310 | 232 | C, byte-identical | a22_s2 f266 | none | translated |
| 0x823580 (init) | 32 | C, byte-identical (`overlay_AREA22_func_00823540.c`) | runs at the AREA22 load, a04_05 exit phase (not a census hit, SIXTH_LEVEL_ROUTE.md section 9) | none | translated |

Ground truth. The byte-identical C was followed, and the load / store order
and the float operand order of every function were read from the original
instructions (locally; nothing is reproduced). 001028E8 (inline asm) and
0018C850 (word asm) were translated from the instructions. 001963A0's
NEARMISS C was checked against the instructions and agrees with them in
behaviour; the translation follows the instructions for the order of
accesses (for example, state 0 re-reads +1 after clearing the halfword +8,
and the "raise" computes `cur + ((42 + y) - cur)` in that order, which is
not `42 + y` in general under the EE model).

## 1. Interface (the AREA04 design)

The design is `em_area04_port.h`'s (docs/AREA04_PORT.md section 1), with
these differences:

- **Entries.** Five without a result: 001028E8 `(out, a, b)`, 0012D940
  `(self, sub)`, 00183010 `(actor, delta)`, 001963A0 `(cam, player)`,
  0x823580 `()`. Four with a result: 00100110 `(a0, a1, a2, a3)` (whole
  64-bit registers, passed on to 001274B0), 0018C850 `(vec, target, step)`,
  0018C920 `(goal, vec, step)`, 001BB310 `(actor)` (always 1). Float
  arguments are `float` and are carried as their bits.
- **No callback.** None of these functions reaches a +0x4C callback.
- **Hooks.** 19 boot-function hooks named by original address. 001274B0's
  result is the whole 64-bit register (00100110 tests it as a signed 64-bit
  value). Float results come back as the float whose bits are the
  original's.
- **Calls between translations.** 001963A0 calls 0018C920 and 0018C850
  directly; both are also entries.
- **Fail-stop.** As AREA04: fault 5 (unmapped or misaligned address, or
  `bytes` NULL), 1 (reached NULL hook), 2 (a hook returned < 0), plus 6
  (em_ee_float.h refused the VU0 form of 001028E8; the one form used,
  vmul.xyzw, is a measured form, so it does not happen). After a fault no
  hook runs, `bytes` is not called again, writes are dropped, the first
  fault is kept, no result is written, and the entry returns -1. A fault
  latched on entry, a NULL hook table, a NULL fault pointer and (entries
  with a result) a NULL result pointer return -1 at once.
- **Quadwords.** 001028E8 aligns its three addresses down to 16, as the EE
  does for its quadword loads and stores; it reads the four words of a,
  then the four of b, then writes the four of out, each in lane order.
- **EE arithmetic.** `em_ee_float.h` on bit patterns (add/sub/mul/div,
  CVT.S.W, NEG, the compare keys c.lt / c.le, and the VU0 multiply
  `em_vu_vec_bits`).

## 2. What each function does (behaviour, from the code)

cam = the camera block 0x8101E0, e / player = the player block 0x8102B0,
eye = D_008105D0, target = D_008105E0 (vec4 each).

| Entry | Called by (route) | Behaviour |
|---|---|---|
| 00100110 | 001A5C30 (the round-prim segment test: "dy above 1e-5") | 1 when 001274B0(a0, a1, a2, a3) as a signed 64-bit value is > 0, else 0. The registers are passed on unchanged. |
| 001028E8 | 001A4830, 001A5C30 (stack vectors) | out = a * b, lane by lane (VU0 multiply), quadword addresses aligned down to 16. |
| 0012D940 | 0012A5D0 (the corridor bugs) with +5 = 10 / 11 | v = 001C2770(self, sub, 6), then by +6: **0** heading +0xC4 = D_00810374 (+ pi when sub halfword +0xF6 & 7 >= 4), + pi ((rand & 0x1F) - 16) / 180, wrapped (001B1470); 0012E070(sub); sub +0xD8 = 0.5; clip 0x1B (001287F0 with 0.0); sub +0xE4 = 0x400; +6 + 1; sub +0xF0 / +0x70 / +0x74 / +0x80 / +0x88 = 0, +0x78 = +0x84 = 1.0; +0 = 1. **1** +0xB0 += +0x38 sin(+0xC4), +0xB8 += +0x38 cos(+0xC4) (0011E2A8 / 0011DE90), 001B5360(self); sub +0xE4 == 0x100: +0xC0 = 0, sub +0xD8 = 0, sub halfword +0xF4 = 0, then by (rand & 0xC0) >> 6: clip 0x11 and +6 + 1 / clip 0x1E and +6 = 3 / clip 0x1A, +0 = +4 = 2, +5 = 4, +6 = 0 / clip 0x12 and +6 = 3; else sub +0xE4 & 0xF and 00128640(self) == 0: +5 = 1, +6 = +7 = 0. **2** with sub halfword +0xF4 bit 0x1000: 00128830(self, 0, 0, 1.0), clips 0 (0.0) and 1 (6.0), +0xC4 = 001B1470(pi + +0xC4), then 00128640 == 0 resets as in 1. **3** as 2 without the turn. At the end, v == 0: 001C3D60(self, sub). |
| 00183010 | 001C1A80 (the two model-0x52 owners' push, a22_01 / a22_s1) | actor +0xA0 += d, +0xB0 += d, 0x70003B40 += d (001028B8 in place), then 0x70003B50 = actor +0xC0 (00102948). No access of its own. |
| 0018C850 | 001963A0; also called by 00198050 and 00198440 (not on this route) | d = target - vec +4, a = fabs(d) (0011DF78). a <= step: vec +4 = target, returns 4. Else v = a (0.5 step), v = step unless step < v, negated when d < 0; vec +4 (read again) += v; returns 0. |
| 0018C920 | 001963A0; also called by 00198050 and 00198440 (not on this route) | The same step for x (+0) then z (+8) of vec toward goal; a snap reads goal's axis again and stores it; returns bit 0 (x snapped) / bit 1 (z snapped). |
| 001963A0 | the camera action dispatch 0018BC20, action 12 (the ladder; a22_s1) | **By cam +1**: 0 halfword +8 = 0, +1 (read) + 1, +2 = 0, then pose B (+0x10..+0x1C = 114, 173.7, 193.4, 1) and +1 = 1 when e +0xA4 <= 176.9, else pose A (115, 266, 230.3, 1) and +1 = 3. 1 0018C920(+0x10, eye, 0.2), 0018C850(eye, +0x14, 0.2); above 176.9: pose A, the raise, eye = +0x10, +1 = 4. 2 eye = +0x10; above 176.9 as 1. 3 the raise, the two chases; at or below 176.9 pose B, +1 = 2. 4 the raise, 0018C850; at or below 176.9 pose B, +1 = 2. (Raise: +0x14 = cur + ((42 + e +0xA4) - cur), 266 when not <= 266.) **By e +0x230**: 8 0018C920(e +0xA0, +0x20, 0.1); 6 / 7 0018C6A0(e +0xB0, +0x20, 0.4); 9 above 176.9: D_00810702 = 3, +0x20..+0x28 = (115, 254, 246.3), 0018C6A0(+0x20, target, 0.3), 0018C4B0(target, +0x24, 0.3), 0018D7B0(cam, 5), return; else D_00810702 = 5 and 0018C6A0(e +0xB0, +0x20, 0.4); other: +5 = 1 and halfword +0xA0 = 0x28 above 176.9, else +5 = 0; +6 = +1 = 0; 0018C920(e +0xA0, +0x20, 0.1). Then 0018C4B0(+0x20, e +0xB4 + +0x8C, 0.3), 0018C0C0(cam), 0018D7B0(cam, 5). |
| 001BB310 | op09 record 0x24DA80 of the door program 0x24DA40 (door [10]'s locked branch, a22_s2) | p = actor +0x1C; 0x700038A0 = (p +0xB0, +0xB4, +0xB8, 1.0) and target = it (00102948); 0x700038A0 = (player x - 15 sin(yaw), 5 + player y, player z - 15 cos(yaw)) and eye = it. Returns 1. For door [10] 0x7AAB70, +0x1C is 0x7AAE60, the lamp [11] above the door. |
| 0x823580 | 001E7780, area key 0x1600 (AREA22 sub 0) | D_00275C28 = 0x20, D_00275C2C = 0, D_00275C24 = 0, D_00275C1C = 0x823E00 (the overlay's .bss base), in that order; the two arguments are not read. |

**Leads for SIXTH_LEVEL_ROUTE.md open item 2 (from the code; not traced):**
001963A0 writes D_00810702 = 3 when e +0x230 == 9 and e +0xA4 > 176.9
(and 5 at or below), and it is the ladder camera (first run at the ladder
grab, a22_s1 f427); the capture recorded D_00810702 = 3 at the ladder
top-out (a22_s1 f935, y 221). 0012D940's state 0 calls 0012E070, which
clears bit (sub +0xF6 & 7) of D_0081083C when sub +0xF6 & 0x80; 0012D940
first ran at a22_s1 f89, the frame D_0081083C returned to 0 after the bug's
grab. Neither is established as the writer by a trace.

## 3. Verification

`python3 tools/test_area22_port_reference.py` (port root, macOS arm64 or
Linux; no make target: the Makefile belongs to another chain while this
side track runs). It compiles the module into
`build/area22/port/area22_port.dylib` with `-std=c11 -Wall -Wextra -Werror
-Wpedantic -ffp-contract=off`. At most 4 worker processes (EM_TEST_JOBS
overrides). `EM_AREA22_PORT_ONLY=<label prefix>` runs a subset (no
coverage, fail-stop, contract or reuse checks) for debugging.

- **Oracle and harness.** The AREA04 harness (docs/AREA04_PORT.md section
  3), reused: FallEE runs the original code resident in the recorded RAM
  of 8 images: the a04_05 end (the AREA22 arrival, decomp
  `build/s87/route_a04/a04_05_progression_exit`) and the ends of a22_00,
  a22_01, a22_s0, a22_s1, a22_s2 (AREA22 resident), and a22_02 / a22_s3
  (AREA01 / AREA04 resident; boot entries only). Before any case the test
  asserts that each AREA22 image's overlay text (0x823540..0x823600, size
  from the file header) equals the user's `extract/OVERLAY/AREA22.BIN` and
  that every image's boot text equals the pinned ELF. No function here has
  a jump table. 001963A0's calls to 0018C920 / 0018C850 run as original
  code at the top level of the oracle.
- **Callees run as original code**: 0011DE90, 0011DF78, 0011E2A8,
  001028B8, 00102948, 00122BB8, 0012E070, 001274B0, 00128830, 0018C0C0,
  0018C4B0, 0018C6A0, 001B1470 (each first rehearsed with the argument
  registers its hook does not pass poisoned). Stubbed with scripted
  results: 00128640, 001287F0, 0018D7B0, 001B5360, 001C2770, 001C3D60.
- **Compared, per case**: the calls and their arguments; memory at every
  call entry before the callee's writes are replayed; the memory accesses
  between calls one for one, in order, by address, size and
  changed-or-not; all memory after the last store (32 MiB, scratchpad,
  window); the return value; the store-log self-check; stops at unmapped
  or misaligned original accesses; every case again from a poisoned start
  image; coverage of every reachable original word (one word excluded:
  the delay slot of the default of 0012D940's `(rand & 0xC0) >> 6`
  switch, which no value reaches); the table's ctx at every call.
- **Fail-stop.** `fault_checks` (001BB310 on door [10]: NULL hook, failing
  hook, an unmapped address after three calls and before any call, a
  latched fault, NULL hooks; every entry with a latched fault, a NULL hook
  table, a NULL fault pointer and, for the four entries with a result, a
  NULL result pointer) and `hook_contract_site` (the AREA04 contract
  sweep: on passing cases that together reach all 19 hooks and all 9
  entries, every call failing, every memory access refused, each hook NULL
  / INT32_MIN / 1 / INT32_MAX, `bytes` NULL).
- **Reuse checks** (the three reused translations). For each AREA22
  image, the round prims of the captured cell directory (three: the drums
  [14]..[16], 0x4000 prims, radius 4, half height 6) are run through the
  ORIGINAL 001A44B0, 001A5C30 and 001A4830 (with everything they call as
  original code) and through the existing translations
  (`em_coll_probe_001A44B0`, `em_coll_segment_001A5C30`,
  `em_coll_move_prim_001A4830`, through a generated bridge that loads their
  state from the same scratchpad and writes it back): the captured segment
  of each image, and on a22_00 designed segments (horizontal crossings at
  seven heights across the drum's span and caps, eight directions, five
  offsets across the rim, long and one-frame short; vertical segments at
  five radii through both caps with horizontal drift 0, 1e-6, 1e-5,
  2e-5, both directions; 40 random segments per prim). Compared: the
  return value, the whole scratchpad, and that the original wrote no RAM.

**Cases** (923 in the default run: 168 capture and 755 designed cases):

- 168 capture cases, on the state each image ends in: 0x823580 on the six
  AREA22 images; 0012D940 on every live 0012A5D0 node of all eight images
  (self, self + 0x1F0; states 1..5 at +6 as captured); 001963A0 on the
  camera block and the player; 0018C850 (eye, cam +0x14, 0.2) and
  0018C920 (cam +0x10, eye, 0.2) and (player +0xA0, cam +0x20, 0.1), the
  calls 001963A0 makes; 00183010 (player, 0x700038A0); 001BB310 on every
  live door (001BB860 / 001BC350) node; 001028E8 on (0x700038A0, eye,
  target).
- 755 designed cases: every state and step of every function; for
  0012D940 all states 0..5 and 0xFF with 001C2770 results 0 / 1 / -1 /
  0x80000000, +0xF6 low bits both sides of 4, random draws across both
  masks, sub +0xE4 = 0x100 and its neighbours with each (rand & 0xC0) >> 6
  pick, +0xF4 bit 0x1000 set and clear, 00128640 results; for 001963A0 every
  eye state 0..5 / 0xFF against e +0xA4 = 176.9, its two neighbours, NaN,
  -inf, +inf and far values, and action words 1, 6, 7, 8, 9, 12, 0x109,
  0xFFFFFFFF; the raise's clamp at 266 and its rounding (cur 0, 266, the
  float below, +-1e30, 3e38 against y around 224), 42 + y exact (y 0, -42,
  1), the eye's x / z chase at distances around its 0.2 step, the 0.1
  chase at |d| exactly 0.1, the tail sum; for the chases 0018C850 /
  0018C920 d equal to the step, just above and below, both signs, zero
  and negative steps, d = +-0 with a negative step, a step whose compare
  key is 0 (1, 0x80000001, -0), NaN / inf / FLT_MAX in the target, the
  vector and the step, and a stubbed fabs at the step, its neighbours,
  NaN, -0, +inf; 001BB310 over yaws (0, pi, -pi/2, 1e20, -0, NaN, a
  signalling NaN, +inf, FLT_MAX), saturating player positions, another
  linked record, an unmapped and a misaligned +0x1C; 001028E8 over lanes
  of normal, overflowing, underflowing, denormal, signed-zero, inf, NaN
  and FLT_MAX values, aliasing out / a / b, unaligned addresses (aligned
  down), RAM and scratchpad, unmapped a / b / out; 00100110 over doubles
  around 1e-5 in both orders (0, -0, 1e-5 and its float neighbours, +-1,
  +-3.4e38, 1e-40, +-inf, NaN, a signalling NaN) with poisoned a2 / a3,
  and a stubbed 001274B0 returning 64-bit values whose low and high halves
  disagree in sign; 00183010 over deltas, another actor and a delta inside
  the actor; the init over filled words; scribbling callees for every
  field the original re-reads after a call (0012D940 +6, +0xC4, +0x38,
  +0xB0, +0xB8, sub +0xE4, +0xF4; 001963A0 e +0xA4, +0xB4, +0x230, cam
  +0x14, +0x24, +0x8C; 001BB310 D_00810360..74; the chases' goal and
  vector); unmapped pointers (the camera block, the player, sub, self).
  The original's angle wrap 001B1470 loops for ever on 1e20 or a NaN
  pattern; those two headings stub it.
- `EM_TEST_FULL=1` adds 80 perturbed cases (40 seeded rounds of 001963A0
  over eye states, action words, heights and the eye / camera vectors, and
  of 0012D940 over states, +0xE4, +0xF4, +0xF6, heading, speed and callee
  results), runs every reuse segment, and compares all memory at every
  call entry.

**Measured** (2026-09-28, M1, host shared with other lanes): default run
923 cases, 1,640 runs (717 poisoned), 7,190 call entries compared, 4,963
helper register rehearsals, coverage 734 / 734 reachable words, hook
contract 201 native runs on 12 cases, reuse checks 900 of 6,174 segment
runs (001A44B0 304 runs / 18 hits, 001A5C30 294 / 50, 001A4830 302 / 82;
hit classes 0x4000 / 0x8000 caps and 0x2000 sides), ~9 s CPU (~5 s wall
with 4 workers); `EM_TEST_FULL=1`: 1,003 cases, 1,780 runs, 7,918 call
entries compared over all memory, all 6,174 reuse runs (2,058 per
function; 103 / 482 / 584 hits), ~39 s CPU (~12 s wall). All pass.

**Mutation sweep** (one bounded sweep, `build/area22/port/sweep.py`, not
committed; results `build/area22/port/sweep_results.json`). 989
single-operation mutants of `em_area22_port.c`: every float constant with
its lowest bit flipped, every address macro + 4, operator swaps (`==` /
`!=`, `<` / `<=`, `>` / `>=`, `&&` / `||`, add / sub, mul / div, lt / le,
access widths 8 / 16 / 32 both ways, `!` removed, `&` as `|`, `>> 6` as
`>> 5`), constant swaps (1.0 / 0, the 0.2 / 0.3 / 0.4 steps, the two poses,
eye / target), every integer literal +-1, the operands of every two-operand
EE op swapped, every store, call and fault test dropped, every
`if (hook) return;` turned into a plain call, and every pair of adjacent
simple statements swapped. Each mutant ran in a forked child against the
full default case list (the mutated function's cases first) until the
first failing case, then `fault_checks`, then the hook contract on the 12
contract sites. 69 do not compile. Of the 920 that run:
- **838 killed**: 801 by the cases, 18 by `fault_checks`, 12 by the hook
  contract (an entry returning 0 or -2 after a failed hook, or writing its
  result after one), 7 by a crash of the mutated module (a wider access
  into a narrower local).
- **82 equivalent, none unexplained**:
  - 37 are inert after a fault: 28 turn `if (hook) return;` into a plain
    call and 9 drop an `if (a22_failed(o)) return;`, in places where every
    later hook is then skipped, `bytes` is not called, writes are dropped
    and the entry returns -1 (in 00100110 and 00183010, which return
    straight after their calls, the hook contract killed the variants where
    this does not hold);
  - 11 change a fault-path return value that no caller distinguishes
    (`a22_begin` -2 / 0 and `a22_end`-style -2 / 0: the entries map any
    failure to -1; the chase step's -2 / 0 / missing test after a failed
    fabs: the entry still ends in -1);
  - 13 change a value that is always overwritten or unused before it is
    read: the initial values of hook result variables (4; every hook
    writes *result on success), the sizes and initializers of 001028E8's
    lane arrays (7), the Q argument of the multiply (1), and the latch of
    a refused VU0 form (1; vmul.xyzw is a measured form, never refused);
  - 8 swap the operands of an EE add or mul (7 add, 1 mul): the model's
    add and mul are symmetric (checked on 100,000,196 operand pairs,
    random and special values, scratch `build/area22/port/sym.c`: 0
    asymmetric);
  - 7 keep the value a test sees: `& 6` for `& 7` before `>= 4` (bit 2),
    `& 0xC1` for `& 0xC0` before `>> 6`, flags 2 for 1 where only truth is
    tested (3), a loop bound 9 for 8 with a step of 8, and `!(step <= v)`
    for `!(step < v)` in the chase (they differ only when step and v have
    equal compare keys; v is a product, never inf or a denormal, so that
    means v and step both zero, or step a denormal and v zero, and the add
    that follows treats those alike: the designed tie cases with steps 1,
    0x80000001 and -0 pass and the mutant passes them);
  - 6 swap statements that do not move a memory access relative to
    another: address alignment, local declarations, the pure computation
    before a load, and a flag update against a store.
- The sweep is one bounded sweep; it covers these mutants only. It ran
  against the case list before the 18 tie cases (steps 1, 0x80000001, -0) were added. An
  earlier pass of the same sweep (before the last review cases) left three
  non-equivalent survivors that the case list then gained cases for: the
  0.2 step of the eye's x / z chase in 001963A0 states 1 / 3 (the
  captures and the earlier designed states all held the eye equal to
  +0x10, so the chase always snapped), and `d < 0` against `d <= 0` in the
  chase (d = +-0 with a negative step); the 0.1 step and the 42 constant
  (both killed by the added `|d| = 0.1` and `42 + y` exact cases) were
  found by a 40-mutant trial before that.

## Binding

Nothing calls these entries. To run them live a host must:

- supply `bytes` over the scene's original-byte storage (actor pool, the
  player block 0x8102B0, the camera block 0x8101E0 and the eye / target
  0x8105D0 / 0x8105E0, D_00810374, D_00810702, D_00275C18..2C, the
  scratchpad);
- call 0x823580 where 001E7780 handles area key 0x1600 (001E7780 itself is
  not translated here; it zeroes D_00275C18..2C first);
- call 001963A0 from the camera dispatch (`em_camera_leftovers.c`'s
  `w_001963A0` slot, action 12) with (0x8101E0, 0x8102B0);
- call 0012D940 from 0012A5D0's +5 = 10 / 11 branch
  (`em_area01_exita.c`'s call site) with (self, self + 0x1F0);
- bind `em_area04_port.h`'s `w_00183010` hook (001C1A80's push) to
  00183010;
- call 001BB310 from the script host's op09 for record 0x24DA80 (the
  script's owner as the argument);
- 00100110 and 001028E8 are called by the collision prim tests, which
  already carry their own inlined equivalents (above);
- bind each hook to a verified port translation or a fail-stop stand-in.
  Stubbed callees are not verified here: 00128640, 001287F0, 0018D7B0,
  001B5360, 001C2770, 001C3D60. The helpers run as original code in the
  test still need port translations paired by address. Port files that
  name them exist (0018C4B0 / 0018C6A0: `em_camera_follow_original.c`,
  docs/CAMERA_LEFTOVERS.md section 4; 0011DF78 / 0011DE90 / 0011E2A8:
  `em_sdk_math_original.c`; 001274B0: `em_sdk_soft_float.c`; 00128830 /
  0012E070: `em_area00_low.c`); whether each is a verified translation of
  the same entry is for the binding lane to check, pairing by address.

## Known gaps

- **Live evidence.** The captures are end-of-beat states. The camera
  capture cases run 001963A0 in state 1 with the player's action word 1
  (the end state); the ladder frames that run it with action 9 / eye
  states 3 / 4 are covered by designed cases, not by a captured image of
  those frames (a22_s1's end holds pose A's values, 115 / 266 / 230.3, in
  cam +0x10.. and in the eye, which are 001963A0's pose A).
  The bug captures hold states 1..5 at +6 but no node with +5 = 10 / 11;
  0012D940 is run on them directly. 00100110 / 001028E8 have no captured
  register state; their cases are designed.
- **Stubbed callees' inputs.** Only the RUN helpers are proven not to read
  a register their hook does not pass.
- **Callee result ranges.** Results are scripted beyond the real ranges.
- **What the harness cannot see** (as AREA04): whether an access that
  changed nothing is a load or a store of the same value; hardware
  behaviour past a stop; the frame bookkeeping.
- **Float proofs rest on the EE model** (`em_ee_float.h`), in the oracle
  and in the module alike.
- **Infinite loops.** The original's angle wrap 001B1470 never returns for
  1e20 or a NaN pattern (the EE compares it as a large number); cases with
  such headings stub it.
- **Reuse checks** cover only the three drum prims (all 0x4000) and the
  segments listed; the 0x8000 branch of the three reused functions is
  checked by their own lanes, not here.
