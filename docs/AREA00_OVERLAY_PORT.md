# AREA00 overlay owners (level-3 lane A00OVL)

Lane A00OVL of the level-3/4 side track, 2026-09-28. It covers the 13 AREA00
overlay functions that the AREA00 route census ran (decomp
`build/s87/census/a00_delta.json`, `new_functions`, region `overlay:AREA00`).
All 13 are translated and compared with the original instructions by
`tools/test_area00_overlay_reference.py`. What that comparison covers is
exactly what section 3 states: the comparisons the test makes, on the cases
it runs. Nothing is bound: no port code calls these entries yet.

| File | What |
|---|---|
| `src/game/em_area00_overlay.h` | public API: 13 entries, the hook table, fault codes |
| `src/game/em_area00_overlay_internal.h` | memory view, fault latch, one typed wrapper per hook |
| `src/game/em_area00_overlay.c` | the 13 translations |
| `tools/test_area00_overlay_reference.py` | original-instruction oracle and comparison |

Addresses are runtime addresses. The overlay is linked 0x40 below where it
runs, so each decomp/splat name is 0x40 lower (0x825170 is
`func_overlay_AREA00_00825130`). Data names in the decomp C
(`D_overlay_AREA00_<addr>`) are runtime addresses (the overlay link resolves
them from their names, decomp docs/OVERLAYS.md); the test confirmed each one,
because every script, table and record address reaches a hook argument or a
store that is compared with the original's.

## Scope: what ran, and what these functions reach

- The census lists 13 AREA00 overlay functions that ran; its 9 "overlay hits
  other overlay" rows are AREA01 code executing at the same addresses after
  the a00_10 / a00_s0 area change (overlay id 2 resident), not AREA00 code.
- None of the 13 calls another AREA00 overlay function: every callee is a boot
  function or the actor's +0x4C callback. So no other AREA00 overlay function
  is reached through them. The AREA00 functions the census did not run
  (`overlay_not_run`: 0x823C50, 0x823CF0, 0x823E10, 0x823EB0, 0x824130,
  0x8241B0, 0x8247C0, 0x8247D0, 0x824BB0, 0x824E00, 0x824E40, 0x825D70,
  0x825E80, 0x825FC0, 0x8260B0, 0x8260F0, 0x826790, 0x826BE0, 0x826CB0,
  0x826CC0) are not translated here.
- No existing port translation covers any of the 13: a grep of the port for
  each address finds only the AREA11/AREA01 functions that share the address in
  their own overlays (em_area01_overlay.c's 0x823580 is AREA01's shaft door,
  em_director_original's 0x825600 is AREA11 code) and the level docs. The
  AREA01 EXITA/EXITB/SIDE/UI modules cover boot functions; here those appear
  only as hooks.
- Ground truth: the decomp's C for all 13 is byte-identical and links from C
  (decomp docs/AREA00_OVERLAY.md, including the two jump-table dispatchers
  0x823580 and 0x825170). The translation follows that C; the order of loads
  and stores between calls was read from the original instructions (local
  reading only; nothing reproduced) and is checked by the test.

## 1. Interface (the AREA01 overlay design)

The design is `em_area01_overlay.h`'s (docs/AREA01_OVERLAY.md section 1),
unchanged:

- **Memory.** The module keeps no state. Every read or write of original
  memory goes through `bytes(ctx, address, size)`. Pointers in records stay
  original 32-bit addresses and are resolved again wherever the original
  dereferences them; fields are re-read where the original re-reads them.
- **Callees.** 51 boot-function hooks named by original address, plus the
  +0x4C callback (`w_callback(ctx, function, actor)`, the function read from
  +0x4C). Pointer arguments are original addresses. A hook returns >= 0 on
  success and writes the original result to `*result` when the original has
  one. 001CD520 takes its GIF tag as a 64-bit argument (the original passes a
  64-bit register).
- **Stack locals (new here).** 0x823820 and 0x825600 pass the address of a
  local of their own frame to callees: 0x823820 a 0x60-byte packet block at
  sp - 0x60 (to 001CFA60 and 001CFBE0), 0x825600 a 16-byte vector at
  sp - 0x10 (to 001026A0 and 001F5940). Both entries take `sp`, the original
  stack pointer at entry, and pass those addresses (the frame layout is the
  original's). The module never reads or writes the local itself; only the
  callees do. A binding must therefore give the callees a mapped scratch
  frame at those addresses.
- **Fail-stop.** As AREA01: fault 5 (unmapped address, or `bytes` NULL), 1
  (reached NULL hook, or NULL callback: the function read from +0x4C), 2 (a
  hook returned < 0). After a fault no hook runs, `bytes` is not called again,
  writes are dropped, the first fault is kept, the op09 entry does not write
  its result, and the entry returns -1. A fault latched on entry, a NULL hook
  table, a NULL fault pointer (and, for the op09 entry, a NULL result pointer)
  return -1 at once.
- **EE arithmetic.** Every float operation is `em_ee_float.h` on bit
  patterns (add/sub with the operand pre-trim, truncating mul, div, CVT.S.W,
  the compare-key c.eq/c.lt/c.le), with the original's operand order.

## 2. What each function does (behaviour, from the code)

Node roles are the ones the route capture measured (THIRD_LEVEL_ROUTE.md
sections 1 and 2.4); the placement indices come from there. Node addresses
are those of the a00_04 image.

| Entry | Node(s) | Behaviour |
|---|---|---|
| 0x823580 | shaft door [52], 0x7B7C00 | **+0x04** 0: 001BBDA0; while D_0081075A != 0xFF: +0x00 = 2, +0x05 = 5, script 0x8284E0; else +0x00 = 1. 1: step +0x05 through the 8-entry table 0x82D400 (values >= 8 run only the tail). Step 0: D_0081075E == 0xFF → +0x05 = 1, 0019C6F0(2, 0), 0019C6F0(0, 1); else D_0081075D == 0xFF → +0x05 = 6, 0019C6F0(2, 0); else 001BBE40(self, block, 0) non-zero → 2. 1: 001BBE40 → +1. 2: 001BC0E0 → +1. 3: 001BC240, +1. 4: 001BC290 → 0. 5: 001BA1F0 non-zero → +0x00 = 1, +0x0B = 0, +0x05 = 0, 001C67E0(self, 0, 0, 0). 6: +0x0B bit 2 → +0x05 = (the value read at the dispatch) + 1, script 0x8286E0. 7: 001BA1F0 non-zero → 001B0C60(1, 0xFF, 0), +1. Then 001BC300. 2/3: 001AFC10. Other: nothing. |
| 0x823820 | the node spawned in a00_08 (0x7BDCF0, +0x0D = 0x63) | Eight puffs in block +0x1F0 (+0x08 timers, +0x28 four floats a/b/c/d each). **+0x04** 0: per puff timer = rand % 120, a = b = 0, c and d = rand / 2^31; +0x1F0 = -1, +0x1F4 = 0, +0x0C = +0x09 = 0, +0x04 = 1, +0x05 = 0; then falls into 1. 1: identity matrix at 0x700036A0, 0x700036D0 = +0xB0, h = 001CCF70(0x700036D0); per puff: timer - 1 (stored and re-read); while it is >= 0 nothing more. Otherwise, when a < 0.3: t = (0.3 - a) / 0.3; 0x70003600 = f2i(255 t), 0x70003604 = f2i(192 t) << 8, 0x70003608 = f2i(192 t) << 16, then a sprite 001CD520(0, 2, 0x700036D0, tag, 0x80000000 \| the three words, w, w, 2.0) with w = 1 + 3a / 0.3. Then two packets (001CFA60 + 001CFBE0 with tables 0x8287E0 and 0x828870); a += 0.09 clamped to 2.0; b += 0.01; past 1.5 the puff is re-armed (timer = rand % 60 + 40, a = b = 0, new c, d). Last: sound 001FC3C0(self, block, 0x41C, 200, 4096). 2/3 free. |
| 0x824EA0 | the arrival owner (its freed record 0x7A8250 is in the a01_07 image) | **+0x04** 0, +0x05 0: to state 3 when 001BA1C0(self, 2) or D_00810701 is non-zero; else once 001B10B0(self, 0xF, 0x11) is 0: 001C63E0(self, 1), sixteen block floats, identity at +0xD0, +0x05 + 1. +0x05 1: script 0x828D60 once 00129780(self, block, 3) is non-zero. 1: identity at 0x70003000; +0x05 0 → 1 and the halfword 0x70003B84 = 0; then 001C2770(self, block, 0) (0 → 001C3D60), 001C64F0(self, block +0xEC), 00102958(0x70003400, 0x70003000), 001C69A0, the callback, +0x05 + 1 when 0x70003B84 >= 0x14A, and 001BA1F0 non-zero → +0x04 + 1, +0x05 = 0. +0x05 2: the same end test, then 001B1E20(6, 0) when 0x70003B8D != 0 and 0x70003B84 == 0x4B0. 2: 001C4760(3, 1), +0x04 + 1. 3: free. |
| 0x825170 | door [51], 0x7B7910 | **+0x04** 0: 001BBDA0, script 0x8294E0 unless D_0081075B == 0xFF, +0x00 = 1. 1: step +0x05 through the 7-entry table 0x82D420. Step 0: D_00810702 == 5 → D_0081075B = 0xFF, then 001BBE40 non-zero → D_008107DB = 0xFF, +0x05 = 3; else D_0081075B != 0xFF → +0x0B bit 2 → 6; else 001BBE40 non-zero → 3. 1: 001BC0E0 → script 0x8299A0, +1. 2: 001BC0E0 → +0x0B = 0, +0x05 = 0. 3: 001BC0E0 → +1. 4: 001BC240, +1. 5: 001BC290 → 0. 6: 001BA1F0 non-zero → if D_0081075B == 0: D_0081075B = 1 and 001C4760(4, 1); then +0x05 = 0, +0x0B = 0, script 0x8294E0. Then 001BC300. 2/3 free. |
| 0x8253E0 | op09 callback, record 0x829B60 of the terminal's script 0x8299E0 | Steps the block in a1: +0x04 0 → 1 and +0x05 = 0 (then as 1); 1: +0x05 + 1, sound 001FB9F0(0x19A, 0x1000, 0x1000, 0x1000) when (+0x05 & 0xF) == 0, returns 1 when +0x05 > 0x30. Other +0x04: returns 0. |
| 0x825480 | terminal [40], 0x7B58C0 | **+0x04** 0: once 001B0FD0 is 0: +0x00 = +0x08 = 1, +0x30 = 0x82A520, 001C6380, 0x700038A0 = (0, 1, 0, 1), 001C5570(self, 0x700038A0, 9, 0). 1: +0x05 0 and +0x0B bit 2 → +0x05 = 1, script 0x8299E0; +0x05 1 and 001BA1F0 non-zero → D_008107DC \|= 1, +0x0B = +0x05 = 0; then 001B17A0 and the callback. 2/3 free. |
| 0x825600 | ferry [48] (kind 2), [49] (kind 0x15), [50] (kind 7) | Kind = +0x0D. **+0x04** 0, once 001B0FD0 is 0: kind 2 copies +0xB0 to +0xA0 and, when D_008107DC bit 1 is set, places itself at (155, -48, -1590, 1); kind 0x15 stores +0xA0 = +0xB0 - (node at +0x18)+0xA0 (001028D0); then 001C6380, 001A2370(self, +0xD0). 1, kind 2: +0x05 0 with D_008107DC bit 0 → +0x05 = 1 and script 0x82A0E0 (bit 1 set) or 0x829BE0; +0x05 1: 001BA1F0 non-zero → 0, then 001A2370; then 001C6380, +0x01 = 1, 001B1B70, the callback, and four points 001F5940(3, M · 0x82D440[i], 0). Kind 7: d = (node two +0x18 links up)+0xB4 - 25; when d != 85 it stores 85 + d at +0x80 of the object whose address is at (D_00275B40)+4; then 001C6380, +0x01 = 1, 001B1B70, 001A2370, callback. Other kinds: +0xB0 = (node at +0x18)+0xB0 + +0xA0 (001028B8), +0xBC = 1, 001C6380, and when that node's +0x01 is set 001A2370, +0x01 = 1, 001B1B70; then the callback. 2/3 free. |
| 0x825920 | [43] 0x7B6190 (+0x02 = 0x88) and the group nodes 0x7B5BB0, 0x7B5EA0 (+0x02 = 0x08) | **+0x04** 0: D_0081075D == 0xFF → 001B0F60(self, 6) == 0 → +0x05 = 2, and for +0x02 bit 7 with D_0081075E != 0xFF the effect 001EFD20(0, (-7.55, -5.69, -1391.65, 1)). Else +0x02 bit 7 → 001B0F60(self, 7) == 0 → script 0x82A540, +0x30 = 0x82A700, +0x00 = 1. Else 001B0F60(self, 6) == 0 → +0x05 = 2. 1: +0x05 0: +0x00 = 2 if D_008104E6 else 1, +0x0B bit 2 → +0x05 = 1. +0x05 1: 001BA1F0 non-zero → 001C64F0(self, 1.0), +0x05 = 2, the player placed by 00182F90(0x8102B0, (+0xB0, D_00810354, +0xB8 - 5, 1)) and the same effect call. Then, while D_00810702 is 5 or 6: 001C68C0, 001B17A0, callback, and for +0x02 bit 7 0x700038A0 = M · 0x82A710 and, while D_0081075D != 0xFF, 001F4E20(0x700038A0, (0, 0x80, 0, 0x80), 2.0). 2: nothing. 3: free. |
| 0x825C80 / 0x8261E0 / 0x8262D0 | 0x7B6480 / 0x7B6770 / 0x7B6A60 | One code, three data sets. **+0x04** 0: +0x30 = 0x2758A8 / 0x2758B0 / 0x2758B8, +0x08 = 3, +0x00 = 1, +0x04 = 1. 1: +0x0B bit 2 → script 0x82A720 / 0x82B790 / 0x82B990, then +0x05 = 1; +0x05 1: 001BA1F0 non-zero → +0x0B = +0x05 = 0; then 001B17A0. 2: nothing. 3: free. |
| 0x8263C0 | ferry cab [47], 0x7B6D50 | **+0x04** 0, once 001B0FD0 is 0: e = 001B6660(0x827A30); if non-zero: e+0xA0 = e+0xB0 - +0xB0 (001028D0), e+0x20 = +0x14; +0x00 = +0x08 = 1, +0x30 = 0x82CCE0, 001C6380. 1: +0x05 0 with +0x0B bit 2: D_00246FB4 = 0x8000000A, boot script 0x246F20, +0x05 + 1; +0x05 1: 001BA1F0 non-zero → +0x05 = +0x0B = 0. Every frame: 0x82CCE0 = +0xB0 + (-16.099998, 12, -40.400024); when +0xB0 != (node at +0x1C)+0xB0 or +0xB4 != that node's +0xB4 + 1, it copies that x and y + 1 and calls 001C6380, 001A2370; then 001B1B70, +0x01 = 1, callback, and two points 001F5940(9, +0xB0 + (-11.017, 26.99, -41 / -36.954), 0). 2/3 free. |
| 0x8266A0 | [60], 0x7B9380 (+0x0D = 0x19) | **+0x04** 0: 001B0FD0, 001C6380. 1: when D_0081075E == 0xFF and +0x0D == 0x19: +0x0D = 0x1A, 001AF800, 001CB5B0(+0x09, read after 001AF800), 001B0FD0, 001C6380, +0x04 = 1; then 001B17A0 and the callback. Any other value: free. |

Measured in the images (not new roles, only the links the code follows):
the ferry group's +0x18 / +0x1C are its neighbours in pool order ([49]+0x18
= [48], [50]+0x18 = [49], [47]+0x1C = [48]), so kind 7 reads the ferry's
+0xB4 and the cab follows the ferry. The capture cases run with
D_00275B40 = node + 0x110, the value the pool loop publishes before each
behaviour (docs/AREA01_OVERLAY.md "Facts this lane measured").

## 3. Verification

`python3 tools/test_area00_overlay_reference.py` (port root, macOS arm64 or
Linux). It compiles the module into `build/area00/ovl/area00_overlay.dylib`
with `-std=c11 -Wall -Wextra -Werror -Wpedantic -ffp-contract=off`. At most 4
worker processes (EM_TEST_JOBS overrides).

- **Oracle and harness.** The AREA01 overlay harness
  (docs/AREA01_OVERLAY.md section 3), reused: FallEE runs the original overlay
  code resident in the recorded RAM of the 11 images with AREA00 resident
  (decomp `build/s87/route_a00/a00_00..a00_09` and
  `build/s87/route_a01/a01_07_level_exit`). Before any case the test asserts
  that each image's overlay text (0x823540..0x826F80, size from the file
  header) equals the user's `extract/OVERLAY/AREA00.BIN`, that both jump
  tables equal the file and that the boot text equals the pinned ELF.
- **Callees run as original code**: 001026A0, 001028B8, 001028D0,
  00102948, 00102958, 001029C0, 00122BB8, 001281C0, 001BA1A0, 001CFA60
  (each first rehearsed with its unused argument registers poisoned). All
  others are stubbed with scripted results; some stubs scribble memory.
- **Compared, per case**: the calls and their arguments (a 64-bit register
  for 001CD520's tag); memory at every call entry before the callee's writes
  are replayed (dirty set; `EM_TEST_FULL=1`: all 32 MiB + scratchpad); the
  memory accesses between calls one for one, in order, by address, size and
  changed-or-not (the only original loads left out are the two jump tables,
  which the C switches encode); all memory after the last store; the op09
  return value; the store-log self-check; stops at unmapped or misaligned
  original accesses; every case again from a poisoned start image; coverage
  of every reachable original word; the table's ctx at every call.
- **Stack locals.** The oracle enters each function with sp = STACK_TOP
  (0x7F0F0000, the harness's private stack); the native entry gets the same
  sp, and the local addresses (sp - 0x60, sp - 0x10) are compared as hook
  arguments. The locals themselves live in the private stack, which is
  outside the compared memory (as in the AREA01 harness): 001CFA60 and
  001026A0 run as original code and write them, 001CFBE0 and 001F5940 are
  stubs. The module never touches that memory, so nothing of it is left
  uncompared on the native side.
- **Fail-stop.** `fault_checks` (terminal, state 1 step 1: NULL hook, failing
  hook, unmapped address before any call and inside the callback read,
  latched fault, NULL hooks; every entry with a latched fault, a NULL hook
  table and a NULL fault pointer; the op09 entry with a NULL result pointer)
  and `hook_contract_site` (the AREA01 contract sweep: on 25 passing cases
  that together reach all 51 hooks, the callback and all 13 entries, every
  call failing, every memory access refused, each hook NULL / INT32_MIN / 1 /
  INT32_MAX, `bytes` NULL; 692 native runs).

**Cases** (1,090 in the default run, all capture and designed cases):

- 168 capture cases: every owner node of the 11 images as captured (the
  a01_07 arrival record included, although its header is 0), plus the op09
  callback on the terminal's script block with record 0x829B60 in each image.
- 922 designed cases: every state (0..4 and 0xFF) and every step of every
  function; callee results 0, 1, 2, -1 and 0x100, 0x10000, 0x80000000,
  0x7FFFFFFF at every test of a result (so the form of each test is pinned);
  every story byte and flag on both sides of every test (D_0081075A/B/D/E,
  D_00810701/02, D_008104E6, D_008107DC bits, +0x02 bit 7, +0x0B bit 2 and
  its complement); scribbling callees for every field re-read after a call
  (+0x05 after the step calls, D_0081075B after 001BA1F0, +0x09 after
  001AF800, +0x4C after the calls before the callback, the cab's position
  after 001C6380, the puff floats after 001CD520 / 001CFBE0, the three colour
  words after each 001281C0, 0x70003B84 / 0x70003B8D after the arrival's
  calls); the float boundaries of every compare and clamp (a at 0.3 and
  a + 0.09 at 2.0, b + 0.01 at 1.5, kind 7's d == 85, the cab's x/y equality
  with -0 / +0, denormals, FLT_MAX, and the exponent-255 patterns); scripted
  random words (negative, INT32_MIN, INT32_MAX) for both remainders; spawned
  pointers with zero low bits; +0x02 with only low bits set; small positions
  and heights (below the magnitude where the EE operand pre-trim drops the
  low bits of an added constant, found while reviewing the sweep).
- `EM_TEST_FULL=1` adds 848 perturbed cases (60 seeded rounds over every
  live owner node: states, steps, flags, story bytes, callee results) and
  compares all memory at every call entry.

**Measured** (2026-09-28, M1): default run 1,090 cases, 1,728 runs (638
poisoned), 5,905 call entries compared, coverage 1,298 / 1,298 reachable
words, ~8 s CPU (~5 s wall with 4 workers); `EM_TEST_FULL=1`: 1,938 cases,
8,236 call entries compared over all memory, ~69 s CPU. All pass.

**Mutation sweep** (one bounded sweep, `build/area00/ovl/sweep.py`, not
committed; mutant list `build/area00/ovl/muts.json`). 2,320 single-operation
mutants of `em_area00_overlay.c`: every float constant with its lowest bit
flipped (30), every address macro + 4 (54), operator swaps (`==`/`!=`, `<` /
`<=`, `>=`/`>`, `&&`/`||`, add/sub, mul/div, lt/le, access widths, masks,
the remainder and shift constants, `r != 0` as `== 1` / `> 0`: 504), every
integer literal +-1 (1,218), every store, call and callback dropped (275),
every pair of adjacent statements swapped (239). 42 do not compile. Of the
2,278 that run:
- **2,122 killed**: 2,114 by the case list as the sweep began (first failing
  case per mutant, cases of the mutated function first, then
  `fault_checks`), 8 more on the full test (hook contract included) after
  the cases added in review: F_25, F_MINUS_40_4, F_MINUS_36_954 (the small
  heights and positions), the three `& 0x81` masks of 0x825920 (+0x02 with
  low bits only), and the op09 entry returning 0 or -2 instead of -1 after
  a fault (hook contract).
- **156 equivalent, none unexplained**:
  - 18 compile to the same machine code as the translation (comments).
  - 104 change the initial value of a result variable (`r`, `handle`, `e`)
    before a hook: every hook writes `*result` on success, and when it is
    skipped or fails a fault is latched, after which no hook, no `bytes`
    call and no store happens and the entry returns -1, so the value is
    never observable.
  - 23 move an `r = 0;` across a statement that neither reads nor writes
    `r`, or across a hook whose result the original ignores (0019C6F0,
    001C4760, 001C64F0, 001B17A0, 001B0FD0 in 0x8266A0), whose result is
    then overwritten before any read.
  - 10 swap a pure local computation (an address sum, a float operation on
    values already loaded) with an adjacent load: the pure value does not
    depend on memory, and the order of the memory accesses is unchanged.
  - 1 makes `a00_begin` return 0 with a fault latched on entry: the body
    then runs inert (as above) and `a00_end` returns -1.
- The sweep did not iterate: it is one bounded sweep, and it covers these
  mutants only.

## Binding

Nothing calls these entries. To run them live a host must:

- supply `bytes` over the scene's original-byte storage (actor pool,
  0x810000 story block, the overlay's data at 0x823500.., boot data such as
  0x246F20 / 0x246FB4 / 0x2758A8.., and the scratchpad), and map a scratch
  frame for the two `sp` entries (the callees read and write sp - 0x60 ..
  sp - 0x01 and sp - 0x10 .. sp - 0x01);
- dispatch the 12 owners from the pool loop by the +0x10 behaviour word
  (after publishing D_00275B40 = node + 0x110, as the original pool loop
  does), and 0x8253E0 from the script interpreter's op09;
- bind each hook to a verified port translation or a fail-stop stand-in.
  The callees are not verified here: 001BBDA0 / 001BBE40 / 001BC0E0 /
  001BC240 / 001BC290 / 001BC300 (the door program), 001BA1F0 (script tick),
  0019C6F0, 001B0C60 (area change), 001C4760, the animation and draw calls
  (001C6380, 001C64F0, 001C67E0, 001C68C0, 001C69A0, 001B17A0, 001B1B70,
  001A2370, 001AF800, 001CB5B0), the render packets (001CCF70, 001CD520,
  001CFBE0), effects and sound (001EFD20, 001F4E20, 001F5940, 001FB9F0,
  001FC3C0), 00182F90, 001B0F60, 001B0FD0, 001B10B0, 001B1E20, 001B6660,
  001BA1C0, 001C2770, 001C3D60, 001C5570, 001C63E0, 00129780, and the
  +0x4C callbacks (0x1CAA00 at every node in the images whose function calls
  it; 0 at the examine owners and the puffs, which never call it). Several of these have port translations in other lanes; a
  binding lane must pair them by address and check each pairing.
- the op09 entry's a0 and a2 are unused by the original; pass what the
  interpreter passes.

## Known gaps

- **Stubbed callees' inputs.** Where the hook does not take a register the
  original happens to leave set, only the RUN helpers are proven not to read
  it (register rehearsal). For stubbed callees that rests on their
  definitions (the lane translating each callee must confirm).
- **Callee result ranges.** Results are scripted beyond the real ranges; what
  the real callees return is theirs to prove.
- **What the harness cannot see** (as AREA01): whether an access that changed
  nothing is a load or a store of the same value; hardware behaviour past a
  stop; the jump-table loads.
- **Live evidence.** The captures show the owners at the end of each beat
  only. States reached on the route between end snapshots (for example the
  terminal's +0x05 = 1 during its script, the shaft door's steps 6 and 7 in
  a00_10, the ferry's script start) are covered by designed cases, not by a
  captured image of that frame. 0x823820 is captured only in state 1 (a00_08
  and a00_09); 0x824EA0 only as its freed record.
- **Float proofs rest on the EE model** (`em_ee_float.h`); any equivalence
  argued in the sweep below would need re-checking if the model changed.
- **Not covered**: a hook that re-enters an entry with the same fault record;
  other status values than those listed in section 3.
