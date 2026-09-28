# AREA01 revisit owners (level-3/4 lane A01RV)

Lane A01RV of the level-3/4 side track, 2026-09-28. It covers the 17 AREA01
overlay functions that the AREA01 revisit ran and that neither the first
level, the first AREA01 visit nor AREA00 ran (decomp
`build/s87/census/a02_delta.json`, `new_functions`, region
`overlay:AREA01`; FOURTH_LEVEL_ROUTE.md section 9). All 17 are translated
and compared with the original instructions by
`tools/test_area01_revisit_reference.py`. What that comparison covers is
exactly what section 3 states: the comparisons the test makes, on the cases
it runs. Nothing is bound: no port code calls these entries yet.

| File | What |
|---|---|
| `src/game/em_area01_revisit.h` | public API: 17 entries, the hook table, fault codes |
| `src/game/em_area01_revisit_internal.h` | memory view, fault latch, one typed wrapper per hook |
| `src/game/em_area01_revisit.c` | the 17 translations |
| `tools/test_area01_revisit_reference.py` | original-instruction oracle and comparison |

Addresses are runtime addresses. The overlay is linked 0x40 below where it
runs, so each decomp/splat name is 0x40 lower (0x825950 is
`func_overlay_AREA01_00825910`). Data names in the decomp C
(`D_overlay_AREA01_<addr>`) are runtime addresses; every script, table,
group and record address the translation names reaches a hook argument or a
compared access, so the test confirms each one.

## Scope: what ran, and what these functions reach

- The 17: 0x8237D0, 0x823900, 0x8239C0, 0x824340, 0x824770, 0x824D50,
  0x824F70, 0x824FE0, 0x825040, 0x825910, 0x825950, 0x825BE0, 0x825D30,
  0x825EA0, 0x825F00, 0x825FC0, 0x8282F0 (group `a01r`, all first run in
  a01r_00 or a01r_01).
- They call each other: 0x825950 dispatches 0x825BE0 / 0x825D30 / 0x825EA0
  / 0x825F00 / 0x825FC0; 0x824340 calls 0x824F70, 0x824FE0 and 0x825040.
  Those calls are direct calls of the translations (in the test they run as
  original code inside the oracle, so a case that enters 0x825950 compares
  its callees too). Every other callee is a hook: 52 boot functions, the
  AREA01 overlay function 0x826010 (not in this set: it ran in a00_10's exit
  phase, census `already_ran = area00_exit`, and has no port translation) and
  the actor's +0x4C callback.
- Their callers, read from the decomp C: 0x823CD0 (the 0x828A00[0..4]
  creatures, `already_ran = area00_exit`, not translated in the port) calls
  0x824340 for +0x05 2..5, 0x824770 for 6 and 0x824D50 for 7, each with
  (self, self + 0x1F0); 0x826D40 (em_area01_overlay_826d40.c, lane OVL)
  calls 0x8282F0 through its hook `w_008282F0`; 0x825910 is named by the
  record 0x82ACD0 of script 0x82AC10 (its +0x04 word, 0x82ACD4, in every
  image); 0x825950 is the behaviour word of the 0x828A00 records [5] and [7]
  (0x828B04, 0x828B5C); 0x8237D0 / 0x823900 / 0x8239C0 are the behaviour
  words at 0x82AA04 / 0x82AA30 / 0x82AA5C, inside the group 0x82A900 that
  FOURTH_LEVEL_ROUTE.md section 2.1 says script 0x82AD90's op14 spawns.
- No existing port translation covers any of the 17: a grep of the port for
  each runtime and link address finds only the level docs, em_examine's
  comments on other overlays' functions at 0x824340 / 0x824FA0 (AREA06 and
  AREA02 code), em_truck_original's 0x825000 (AREA11) and the
  `w_008282F0` hook of em_area01_overlay (lane OVL), which this lane's
  0x8282F0 can serve.
- Ground truth: the decomp's C for all 17 is byte-identical and links from C
  (decomp docs/AREA01_OVERLAY_C.md: 40 of 41 AREA01 functions linked C,
  `link_overlay.py AREA01` byte-identical; 0x824340's jump table is pinned).
  The translation follows that C; the order of loads and stores between
  calls was read from the original instructions (local reading only; nothing
  reproduced) and is checked by the test.

## 1. Interface (the AREA01 / AREA00 overlay design, plus stack locals)

The design is `em_area01_overlay.h`'s and `em_area00_overlay.h`'s
(docs/AREA01_OVERLAY.md section 1, docs/AREA00_OVERLAY_PORT.md section 1):

- **Memory.** The module keeps no state. Every read or write of original
  memory goes through `bytes(ctx, address, size)`. Pointers in records stay
  original 32-bit addresses and are resolved again wherever the original
  dereferences them; fields are re-read where the original re-reads them.
  A quadword copy (the original's quadword load and store) is two
  doubleword reads followed by two doubleword writes, lower half first, as
  the EE model performs them.
- **Stack locals (new in this lane: the module writes them).** 0x8237D0
  (vectors a = sp - 0x20, b = sp - 0x10, copies of 0x829DA0 / 0x829DB0, and a
  matrix m = sp - 0x60), 0x823900 (v = sp - 0x10), 0x825BE0 (quad = sp -
  0x40, a copy of the four points at 0x82B090) and 0x8282F0 (dir = sp -
  0x20, tint = sp - 0x10, a copy of 0x82CB20) write locals of their own
  frame and pass their addresses to callees. Those entries, and 0x825950
  (which runs 0x825BE0 with sp - 0x20, its own frame being 0x20 bytes), take
  `sp`, the original stack pointer at entry; the offsets are the original
  frame layout. The module writes the locals through `bytes` like any other
  memory and never reads them back (only callees do). A binding must map
  that frame region for the module and the callees.
- **Callees.** 53 hooks named by original address plus the +0x4C callback
  (`w_callback(ctx, function, actor)`, the function read from +0x4C just
  before the call). Pointer arguments are original addresses. A hook
  returns >= 0 on success and writes the original result to `*result` when
  the original has one (float results as float bits). 001CD520 takes its
  GIF tag as a 64-bit argument. 0x825D30 leaves 3 in a2 when it calls
  001BA1A0 (the decomp C passes it as a third argument); 001BA1A0 reads only
  a0 and a1 (decomp C, and the test's register rehearsal), so its hook takes
  two.
- **Fail-stop.** As AREA01 / AREA00: fault 5 (unmapped address, or `bytes`
  NULL), 1 (reached NULL hook, or NULL callback: the function read from
  +0x4C), 2 (a hook returned < 0). After a fault no hook runs, `bytes` is not
  called again, writes are dropped, the first fault is kept, an entry with a
  result does not write it, and the entry returns -1. A fault latched on
  entry, a NULL hook table, a NULL fault pointer and (entries with a result)
  a NULL result pointer return -1 at once.
- **EE arithmetic.** Every float operation is `em_ee_float.h` on bit
  patterns (add/sub with the operand pre-trim, truncating mul, the
  MULA/MADD sum of 0x824F70, div, NEG, CVT.S.W, the compare-key
  c.eq/c.lt/c.le), with the original's operand order.

## 2. What each function does (behaviour, from the code)

Node roles are the ones the route capture measured (FOURTH_LEVEL_ROUTE.md
sections 1.1 and 2.1). "pl" is the word D_008106C0 (the 0x825950 node with
+0x0D 0x47 stores itself there while it runs); "target" is the record at
pl +0x118. Headers of each translation in `em_area01_revisit.c` give the
same descriptions with every store and call in order.

| Entry | Called by | Behaviour |
|---|---|---|
| 0x825950 | behaviour of 0x828A00[5] (+0x0D 0x47, the event owner) and [7] (0x4B) | **+0x04** 0: 0x47: pl = self, 001B10B0(self, +0x0D, 0x4A), +0x04 = 1, +0x38 = 1.0, +0x00 = 1, +0x28 = +0x2A = 0, +0x58 = D_0028A5C4, 0019C6F0(0x29, 0), +0x240 = 0; then (+0x0D read again) 0x4B: D_00810845 bit 5 set -> +0x04 = 3; else 001B10B0(self, 0x4B, 0x4C), 001C63E0(self, 0), +0x00 = 1, 001CA6F0(self, 0), +0x58 = 0, 0019C6F0(0x29, 1), +0x04 = 1. 1: 0x47: pl = self and D_008107DF selects 0/1 -> 0x825BE0, 2/0x10 -> 0x825D30, 0x40 -> 0x825EA0, 0x80 -> 0x825F00 (0xFF and other values: nothing); then (+0x0D and D_008107DF read again) 0x4B at 0xFF -> 0x825FC0. 2/3: 0x47 clears pl; 001AFC10(self). Other states: nothing. |
| 0x825BE0 | 0x825950 (stage 0/1) | Copies the quad 0x82B090 to its frame on every call. **+0x05** 0: 001C63E0(self, 7), +0x05 = 1. 1: 001B1EA0(0, D_00810350, quad, 4) non-zero -> +0x05 = 2, D_008107DF = 1, 001BA1A0(self + 0x1F0, script 0x82AA90). 2: 001BA1F0(self) non-zero -> +0x05 = 0, D_008107DF = 2, 00102948(D_008105E0, (+0x18) + 0xB0). Then 0x826010(self), 001C64F0(self, 1.0), 001C68C0(self), +0x01 = 1, callback. |
| 0x825D30 | 0x825950 (stage 2/0x10) | **+0x05** 0: 00102948(D_008105E0, (+0x18) + 0xB0), 0x826010, 001C64F0(self, 1.0); at D_008107DF == 0x10: 001BA1A0(block, script 0x82AC10), 001CA6F0(self, 2), +0x05 += 1, +0xB0..+0xCC = 0. 1: 001BA1F0 non-zero -> +0x05 = 0, D_008107DF = 0x40, byte +0x04 of the node at +0x1C = 3, 001BA1A0(block, script 0x82AD10); then block+0x0E = 001C64F0(self, 1.0) (halfword), +0x28 += 1, at >= 0x26C block+0x0E = 0x1000. Then 001C68C0, +0x01 = 1, callback. |
| 0x825EA0 | 0x825950 (stage 0x40) | 001BA1F0 non-zero -> +0x05 = 0, D_008107DF = 0x80; 001C68C0, +0x01 = 1, callback. |
| 0x825F00 | 0x825950 (stage 0x80) | **+0x05** 0: 001BA1A0(self + 0x1F0, script 0x82AD90), +0x05 = 1, +0x28 = 0. 1: 001BA1F0 non-zero -> +0x2E = 0xFFFF, D_008107DF = 0xFF, 001C47A0(0x20, 1), 001C4760(5, 1), 001B6660(group 0x829220), 001B6660(group 0x8291C0), 001FAE70(0), +0x04 = 3. |
| 0x825FC0 | 0x825950 (+0x0D 0x4B at 0xFF) | 001C64F0(self, 1.0), 001C68C0, 001B17A0, callback. |
| 0x825910 | script 0x82AC10, record 0x82ACD0 (op09) | Returns 1 while a0 +0x3C <= 505.0, else 0. |
| 0x824340 | 0x823CD0 (+0x05 2..5) | held = 001C2770(self, block, 2), then the step +0x06 (0..8; others run only the tail). 0: block+0xD8 = 0, +0x07 = 0, held == 0 -> +0x06 = 1. 1: 0012D580(self, block, held). 2: 00128830(self, 0, 0, -2.5), 001287F0(self, block, 0x13, 0), +0x06 += 1, +0x07 = 0, block+0xD8 = 0. 3: block+0xF4 bit 0x1000 -> +0x06 = step + 1, +0x07 = 0, 00128830(self, 0, 2.5, 0), 001287F0(.., 0x14, 0); else 0x825040. 4: block+0xF4 & 0x5000 -> +0x06 = step + 1, 00128830(self, 0, 1.5, 3), 001287F0(.., 0x15, 0), block+0xD0 = 0x78, block+0xE4 = 0, block+0xF0 = 0.8, +0xC0 = -pi/4, block+0xD8 = 0.6. 5: 0x824FE0; block+0xE4 & 0xF -> +0x06 = +0x07 = 0; else +0xC0 = pi * (56.25 * -block+0xF0) / 180, block+0xF0 -= 0.04 (below 0: +0xC0 = 0, +0x06 += 1, block+0xD0 = 0), +0xB4 += block+0xF0, 0x824F70 non-zero -> +0xC0 = 0, +0x05 = 6, +0x06 = 0. 6: 0x824FE0; +0xB4 += 0.1 * 0011E2A8(001B1470((float) block+0xD0)); block+0xD0 += 0x18; block+0xE4 & 0xF -> +0x06 = +0x07 = 0; else 0x824F70 -> as in 5; else block+0xD0 > 0x870 -> block+0xE4 = 0x400, +0x06 += 1. 7: block+0xE4 == 0x100 -> 001287F0(.., 0x11, 0), +0xC0 = 0, +0x06 += 1, block+0xD8 = 0, block+0xF4 = 0; else its & 0xF -> +0x06 = +0x07 = 0. 8: block+0xF4 bit 0x1000 -> 00128830(self, 0, 0, 1), 001287F0(.., 0, 0), 001287F0(.., 1, 6), +0xC4 = 001B1470(pi + +0xC4), +0x06 = +0x07 = 0. Tail: held == 0 -> 001C3D60(self, block). |
| 0x824F70 | 0x824340 | 1 when 0011E748((+0xB0 - target+0xC0)^2 + (+0xB8 - target+0xC8)^2) <= 8.0, else 0. |
| 0x824FE0 | 0x824340 | +0xC4 = 001B12B0(001B1240(+0xB0, target+0xC0, target+0xC8), +0xC4, 0.04363323). |
| 0x825040 | 0x824340 (step 3) | Reads +0x07 and pl. 0: block+0xE8 = 001B1240(+0xB0, target+0xC0, target+0xC8), +0x07 += 1, block+0xD2 = 0x78, returns 0. 1: +0xC4 = 001B12B0(block+0xE8, +0xC4, 0.13962634); block+0xD2 -= 1; returns 1 at 0 or when +0xC4 == block+0xE8 (read again), else 0. Other: 0. |
| 0x824770 | 0x823CD0 (+0x05 6) | Spot = 0x829DE0 + 0x18 * ev+0xE2 (three position, three angle words). 0: +0x06 += 1, ev+0x30.. = spot position, ev+0x40.. = spot angles, matrix 0x700036A0 from the angles and ev+0x30 on pl's bone (the word at pl + 0x110 + 4 * 0x829DC0[index], then 001026D0 with bone + 0x90), ev+0x10.. = its row - +0xB0.., ev+0x1C = 1, ev+0xE8 = pl+0xC4, ev+0xD4 = 1; then as 1. 1: +0xC4 turns toward pi + pl+0xC4 and +0xC0 toward -pi/2 (001B12B0, 4 degrees), ev+0xD4 -= 0.02 scales ev+0x10 (00103230), that offset turned by the change of pl's heading since step 0 (001B1470, 00102BB0, 001026A0 into 0x700038A0) is added to the spot matrix row: +0xB0 = row.x + off.x, +0xB4 = row.y - off.y, +0xB8 = row.z + off.z; actor matrix 0x70003000 from +0xC0.. copied to +0xD0, row 0x70003030 = +0xB0. At ev+0xD4 <= 0.2: ev+0x30.. = spot position, +0xC0.. = spot angles (from ev+0x40..), 001287F0(.., 0x16, 0), ev+0xD0 = 0x1C2, and ev+0xE2 (read again) 0 -> D_008107DF = 0x10, +0x04 = 4, +0x05 = +0x06 = 0; else +0x06 += 1, +0x07 = 0. 2: matrix 0x70003000 from +0xC0.. and ev+0x30 on pl's bone, +0xB0.. = its row; ev+0xD0 -= 1, at 0: +0x05 = 7, +0x06 = +0x07 = 0. |
| 0x824D50 | 0x823CD0 (+0x05 7) | 0: +0x06 = 1, 001287F0(.., 0x19, 0), ev+0xD0 = 0xF0, ev+0xD4 = 1. 1: matrix 0x70003000 from +0xC0.. and ev+0x30 on pl's bone, +0xB0.. = its row, 00102958(0x70003400, 0x70003000), 001C69A0(self), 0012DE90(ev); unless ev+0xD4 < 2.0: 0x700038A0 = (0, 1, 1, 1), 0x700038B0 = (0, 0, 0, 1), 001026A0(0x700038A0, ((D_00275B40)+0xC)+0x90, 0x700038A0), 001FB9F0(0x1B2, 0x1000, 0x1000, 0x1000), 001EFD90(0x80000009, 0x700038A0, 0x700038B0), +0x04 = 3. |
| 0x8237D0 | group 0x82A900 behaviour | **+0x04** 0: +0x28 = 0. 1: +0x28 += 1; at a multiple of 10: o = 001AFA90(0xC); if o: 00102948(o+0xB0, +0x100), 00102958(o+0xD0, +0xD0), 001026A0(o+0x100, o+0xD0, a), o+0x10 = 0x1F5040; then (o zero or not) 00102958(m, +0xD0), 001026A0(m+0x30, o+0xD0, b), 001F4010(3, m). |
| 0x823900 | group 0x82A900 behaviour | **+0x04** 1: v = (0, 0, 1, 1); 001026A0(v, +0xD0, v); 001028D0(v, v, +0x100); 00102760(v, v); 001EFD90(0x80000003, +0x100, v); 001EFD20(0x80000024, +0x100). |
| 0x8239C0 | group 0x82A900 behaviour | **+0x04** 1: 0x700038B0.. = 0x30, 0x80, 0x30, 0x80; 001F4E20(+0x100, 0x700038B0, 5.0). |
| 0x8282F0 | 0x826D40 (record B's matrix) | 0x700038A0 = (60, 0, 0, 0) and dir = (3, -2, 0, 1) both by the matrix (001026A0), 38A0 += dir (001028B8). hit = 2 when 0019AA80(dir, 38A0, 0x20) (then 38A0 = 0x700031B0), else 0. The words 31D8, 31D4, 31D0 are kept; 0019A570(dir, 38A0, 7, 0x20) non-zero: 38A0 = 31B0 - D_00810360, d = the xyz dot product (00102738), 0x70003A20 = d; hit = 1 when d > 10000 or when 31D8 == 1 and byte +3 of the record at 31D4 is 0x10..0x13, else 2; zero: the three words are put back. hit 2: +0x204 = 31D4 when +0x204 is 0 and 31D8 == 1; +0x04 == 4 -> a sprite 001CD520 at 31B0 whose red is ((random >> 16) * 0xFFFF >> 30 & 0x1F) + 0x40, then 001E2BA0(dir, 38A0, colour 0.8, 100); else +0x200 >= 13 -> 001E2BA0 from 31B0 to the tint 0x82CB20 by record B's matrix. hit 1: 001E2BA0(dir, 31B0, colour, 100). Returns 0, or for hit 2: 2 when +0x204 == 31D4, else 1. |

### Facts this lane measured (in the six revisit images)

- D_008106C0 is 0x7A64F0 (0x828A00[5], +0x0D 0x47) in a01r_00 and a00_10,
  and 0 in a01r_01, a01r_02, a01r_s0 and a01r_s1 (the node was freed at the
  event's end through state 3, which clears it). The node at its +0x118 is
  0x7DA7E0 in both images where it is set.
- 0x828A00[7] (0x7A6AD0, +0x0D 0x4B) is in state 1 in all six images.
- The five 0x823CD0 creatures (0x7A5640 .. 0x7A6200, spot indices ev+0xE2 =
  4, 3, 2, 1, 0) are at +0x05 = 1 in a01r_00 and a00_10 (the step in which
  0x823CD0 runs 0x8240E0 and, once D_008107DF == 2, counts ev+0xE3 down to
  +0x05 = 5, where 0x824340 takes over) and gone after the event.
- The three 0x826D40 nodes 0x7A8830, 0x7A8E10, 0x7A93F0 are in state 4 with
  +0x208 = 0, +0x204 = 0 and +0x200 0 or 1 in all six images; their state-4 path
  calls 0x8282F0 every frame while +0x208 <= 0 (docs/AREA01_OVERLAY.md
  section 2), which agrees with the census (first hit a01r_00 f1).
- No image holds a group-0x82A900 node (0x8237D0 / 0x823900 / 0x8239C0):
  the group is spawned at a01r_01 f1139 and gone by that beat's end.

## 3. Verification

`python3 tools/test_area01_revisit_reference.py` (port root, macOS arm64 or
Linux). It compiles the module into
`build/area01_revisit/area01_revisit.dylib` with `-std=c11 -Wall -Wextra
-Werror -Wpedantic -ffp-contract=off`. At most 4 worker processes
(EM_TEST_JOBS overrides).

- **Oracle and harness.** The AREA00 / AREA01 overlay harness
  (docs/AREA00_OVERLAY_PORT.md section 3, docs/AREA01_OVERLAY.md section 3),
  reused: FallEE runs the original overlay code resident in the recorded RAM
  of the six revisit images with AREA01 resident (decomp
  `build/s87/route_a01r/a01r_00`, `a01r_01`, `a01r_02`, `a01r_s0`,
  `a01r_s1` and `build/s87/route_a00/a00_10_progression_exit`; a01r_03 ends
  in AREA02). Before any case the test asserts that each image's overlay text
  (size from the file header) equals the user's `extract/OVERLAY/AREA01.BIN`,
  that 0x824340's jump table equals the file and that the boot text equals
  the pinned ELF.
- **Callees run as original code**: 001026A0, 001026D0, 001028B8, 001028D0,
  00102738, 00102760, 00102948, 00102958, 001029C0, 00102A60, 00102B08,
  00102BB0, 001031E0, 00103230, 0011E2A8, 0011E748, 00122BB8, 001B1240,
  001B12B0, 001B1470, 001BA1A0 (each first rehearsed with its unused
  argument registers poisoned). All others are stubbed with scripted
  results; some stubs scribble memory. The five event stages and the three
  0x824340 helpers run as original code when a case enters their caller.
- **Stack model (new).** The oracle enters with sp = STACK_TOP (0x7F0F0000,
  the harness's private stack); the native entry gets the same sp. When the
  oracle executes the first instruction of a stack-local function at top
  level it registers that frame's local range (the bytes from sp - size to
  sp: 0x60, 0x10, 0x40 and 0x20 for 0x8237D0, 0x823900, 0x825BE0, 0x8282F0);
  a range ends when sp returns to its entry value (a later function may
  reuse the bytes). The function's own loads and stores in a live range are
  logged and compared like any other access, one for one; its register saves
  (outside the ranges) are not. Writes that callees running as original code
  make on the stack are logged and replayed. The native `bytes` serves the
  same region from a private 1 MiB buffer. Both stacks start from one fill
  byte (0xA5; 0x3C in the poisoned run), so a dropped or misplaced local
  store shows at the next call entry. The store-log self-check covers the
  logged stack bytes, except bytes a later function's register saves
  overwrote.
- **Compared, per case**: the calls and their arguments (a 64-bit register
  for 001CD520's tag); memory at every call entry before the callee's writes
  are replayed (dirty set; `EM_TEST_FULL=1`: all 32 MiB + scratchpad +
  stack); the memory accesses between calls one for one, in order, by
  address, size and changed-or-not (the only original loads left out are
  0x824340's jump-table loads, which the C switch encodes); all memory after
  the last store; the return value of the four entries with one; the
  store-log self-check; stops at unmapped or misaligned original accesses;
  every case again from a poisoned start image; coverage of every reachable
  original word; the table's ctx at every call.
- **Fail-stop.** `fault_checks` (0x825D30 with +0x05 = 1: NULL hook,
  failing hook, unmapped address before any call, inside an address
  argument (+0x1C) and at the callback read, latched fault, NULL hooks;
  every entry with a latched fault, a NULL hook table, a NULL fault pointer
  and, for the four entries with a result, a NULL result pointer) and
  `hook_contract_site` (on 21 passing cases that together reach all 53
  hooks, the callback and all 17 entries: every call failing, every memory
  access refused, each hook NULL / INT32_MIN / 1 / INT32_MAX, `bytes` NULL;
  571 native runs).

**Cases** (1,047 in the default run, all capture and designed cases):

- 134 capture cases: in each of the six images every 0x825950 node as
  captured and each of the five event stages on it, 0x825910 on it (a0 the
  node, a2 the record 0x82ACD0), every 0x823CD0 creature with 0x824340,
  0x824770, 0x824D50, 0x825040, 0x824F70 and 0x824FE0, and every 0x826D40
  node with 0x8282F0 on its record B's matrix; D_00275B40 = node + 0x110 as
  the pool loop publishes it (docs/AREA01_OVERLAY.md "Facts this lane
  measured").
- 913 designed cases on the a01r_00 image: every state and step of every
  function (0x825950 states 0..4 and 0xFF with +0x0D 0x47 / 0x4B / others,
  every D_008107DF stage and D_00810845 bit 5 on both sides; 0x824340 steps
  0..9 and 0xFF with each held value); callee results 0, 1, 2, -1 and 0x100,
  0x10000, 0x80000000, 0x7FFFFFFF at every test of a result; scribbling
  callees for every field read again after a call (+0x0D and D_008107DF
  after a dispatched stage, +0x18 / +0x1C / +0x05 / +0x28 / +0x4C after the
  stage calls, block+0xD0 / +0xE4 / +0xF0 in 0x824340's steps, ev+0xE2 and
  ev+0xD4 in 0x824770, ev+0xD4 in 0x824D50, pl +0xC4 in 0x824770, the
  0x70003 1D0..1D8 words and D_00275B40 in 0x8282F0); float boundaries of
  every compare (505.0, 8.0 by exact distances, 0.2 after the 0.02 step, 2.0,
  10000 by exact points, 0.04 at the F0 step, +0x28 0x26B / 0x26C, block+0xD0
  0x870 / 0x871, -0 / +0, denormals, FLT_MAX and exponent-255 patterns);
  +0x28 modulo 10 with negative and wrapping counters; spawned pointers
  zero, low-zero-bit and real; the random word of 0x8282F0 across its sign
  and halves; changed overlay data (the quad, the vectors 0x829DA0/B0 and
  0x82CB20, a 0x829DC0 bone slot) to prove each is read, not assumed; and,
  added in review, the F0 values of the 0.04 decay at which the operand
  order of pi * (56.25 * -F0) shows, and 0.1 * wave landing in +0xB4 = 0.
- `EM_TEST_FULL=1` adds 1,000 perturbed cases (40 seeded rounds over every
  live 0x825950, 0x823CD0 and 0x826D40 node: states, steps, stages, flags,
  fields, callee results) and compares all memory at every call entry.

**Measured** (2026-09-28, M1): default run 1,047 cases, 1,803 runs (756
poisoned), 10,414 call entries compared, 6,322 helper register rehearsals,
coverage 1,455 / 1,455 reachable words, ~10.5 s CPU (~3.6 s wall with 4
workers); `EM_TEST_FULL=1`: 2,047 cases, 3,294 runs (1,247 poisoned), 16,183
call entries compared over all memory, 10,390 helper register rehearsals,
~66 s CPU. All pass.

**Mutation sweep** (one bounded sweep, `build/area01_revisit/sweep.py`, not
committed; mutant list `build/area01_revisit/muts.json`, results
`results.json`). 2,151 single-operation mutants of `em_area01_revisit.c`:
every float constant with its lowest bit flipped (29), every address macro
+ 4 (48), operator swaps (`==`/`!=`, `<`/`<=`, `>=`/`>`, `&&`/`||`,
add/sub, mul/div, lt/le/eq, madd/msub, access widths and signedness, the
shift, mask and modulo constants: 424), every integer literal +-1 (1,026),
every store, call, callback and helper call dropped (319), every pair of
adjacent statements swapped (305). Each mutant ran in its own process
against the case list (cases of the mutated function first), then
`fault_checks`. 110 do not compile. Of the 2,041 that run:
- **1,996 killed**: 1,973 by a case, `fault_checks` or an exception in the
  harness, 9 by crashing the test process (a NULL dereference the mutant
  introduces into the fail-stop paths, which fails the run), and 14 of the
  59 first survivors on the full test after review: the post-fault
  `return -1` of the four entries with a result (8 literals and 4 swaps,
  killed by the hook contract), F_0_1 with its low bit flipped and the swap
  of the two multiplies of step 5 (killed by the cases added in review).
- **45 equivalent, none unexplained**:
  - 3 change `rv_begin`'s value: the entries test it only for non-zero, and
    with a fault latched on entry a body that runs anyway is inert (no hook,
    no `bytes`, no store) and returns -1 without a result.
  - 5 load a halfword zero-extended instead of sign-extended where the value
    only feeds a 16-bit store or a test of its low 16 bits.
  - 15 change the initial value of a result variable (`r`, `held`, `obj`,
    `node`) before a hook that writes it on success, or whose result is
    unused; when the hook is skipped or fails, a fault is latched and nothing
    observable follows.
  - 6 change the shift of 0x700038B4 or 0x700038B8 in 0x8282F0's colour
    word: both are stored as 0 just before they are read back, with no call
    between, so any shift of them is 0.
  - 2 change a bound that does not change the iterations or the code
    (`q[8]` sized 9, the loop's `<= 0xCC` as `<= 0xCD`).
  - 14 swap a declaration, a `(void)` cast or an assignment to a local with
    a neighbouring statement that neither reads nor writes it, or move
    `r = 0` after a hook whose result is not used.
- The sweep did not iterate: it is one bounded sweep, and it covers these
  mutants only.

## Binding

Nothing calls these entries. To run them live a host must:

- supply `bytes` over the scene's original-byte storage (actor pool, the
  0x810000 story block including D_008106C0 / D_008107DF / D_00810845, the
  overlay's data at 0x823500.., boot data such as D_0028A5C4 and
  D_00275B40, the scratchpad) and a mapped frame below each `sp` it passes
  (sp - 0x60 .. sp for 0x8237D0, sp - 0x10 .. sp for 0x823900, sp - 0x40
  .. sp for 0x825BE0, sp - 0x20 .. sp for 0x8282F0, and sp - 0x60 .. sp -
  0x20 for 0x825950, whose 0x825BE0 runs at sp - 0x20); the callees read
  and write those locals;
- dispatch 0x825950 (records 0x828A00[5] and [7]) and 0x8237D0 / 0x823900 /
  0x8239C0 (the group 0x82A900 behaviours) from the pool loop by the +0x10
  behaviour word (after publishing D_00275B40 = node + 0x110), 0x824340 /
  0x824770 / 0x824D50 from a translation of 0x823CD0 (not in the port yet),
  0x8282F0 from em_area01_overlay's `w_008282F0` hook (which must add the
  frame), and 0x825910 from the script interpreter's op09;
- bind each hook to a verified port translation or a fail-stop stand-in. The
  callees are not verified here: the animation and draw calls (001C63E0,
  001C64F0, 001C68C0, 001C69A0, 001CA6F0, 001B17A0, 001287F0, 00128830,
  0012D580, 0012DE90, 001C2770, 001C3D60), the script tick 001BA1F0, the
  quad test 001B1EA0, the collision probes 0019AA80 / 0019A570, spawning and
  freeing (001AFA90, 001B6660, 001AFC10), 001B10B0, 0019C6F0, 001C4760,
  001C47A0, 001FAE70, effects, sprites and sound (001EFD20, 001EFD90,
  001F4010, 001F4E20, 001CD520, 001E2BA0, 001FB9F0), the overlay function
  0x826010, and the +0x4C callbacks (0x1CAA00 at every node in the images).
  The helpers the test runs as original code are verified only as far as
  they are compared here (their writes and results are the original's; a
  binding still needs a port translation of each). Several have port
  translations in other lanes; a binding lane must pair them by address and
  check each pairing.
- the op09 entry's a1 and a2 are unused by the original; pass what the
  interpreter passes.

## Known gaps

- **Live evidence.** The end snapshots show D_008107DF at 0 or 0xFF only, so
  the event's middle stages (1, 2, 0x10, 0x40, 0x80), the creatures' steps 2
  to 7 and the group-0x82A900 behaviours are covered by designed cases, not
  by a captured image of those frames. A capture inside a01r_01 (for
  example at f215, f567, f1065, f1139) would give them live images.
- **Stubbed callees' inputs.** Where a hook does not take a register the
  original happens to leave set, only the helpers run as original code are
  proven not to read it (register rehearsal). For stubbed callees that rests
  on their definitions (the lane translating each callee must confirm).
- **Callee result ranges.** Results are scripted beyond the real ranges;
  what the real callees return is theirs to prove.
- **Inputs the original helpers do not finish.** 001B12B0 / 001B1470 with
  an angle near FLT_MAX run the original's wrap loop past the harness's step
  limit (found while designing 0x825040's equality cases); those inputs are
  left out.
- **What the harness cannot see** (as AREA01 / AREA00): whether an access
  that changed nothing is a load or a store of the same value; hardware
  behaviour past a stop; the jump-table loads; the functions' register saves
  in their frames.
- **Float proofs rest on the EE model** (`em_ee_float.h`).
- **Not covered**: a hook that re-enters an entry with the same fault
  record; other status values than those listed above.
