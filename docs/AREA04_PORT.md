# AREA04 new functions (level-3/4 side track lane A04T)

Lane A04T, 2026-09-28. It covers the 16 functions the AREA04 route census
found new (decomp `build/s87/census/a04_delta.json`, `new_functions`): the 14
AREA04 overlay functions that ran on the main line and the two boot
functions 001BE5F0 and 001C1A80, which first ran right after the AREA22
arrival (census group `a04_exit`, a04_05 f515 / f516). It adds the three
overlay functions they call that have no port translation: 0x823580
(0x823700's talk turn) and 0x824830 / 0x824930 (0x8246B0's sub-states). All
19 are translated and compared with the original instructions by
`tools/test_area04_port_reference.py`. What that comparison covers is
exactly what section 3 states: the comparisons the test makes, on the
cases it runs. Nothing is bound: no port code calls these entries yet.

| File | What |
|---|---|
| `src/game/em_area04_port.h` | public API: 19 entries, the hook table, fault codes |
| `src/game/em_area04_port_internal.h` | memory view, fault latch, one typed wrapper per hook |
| `src/game/em_area04_port.c` | the 19 translations |
| `tools/test_area04_port_reference.py` | original-instruction oracle and comparison |

Addresses are runtime addresses. The overlay is linked 0x40 below where it
runs, so each decomp/splat name is 0x40 lower (0x823B90 is
`func_overlay_AREA04_00823B50`). Data names in the decomp C
(`D_overlay_AREA04_<addr>`) are runtime addresses; every script, table,
record and data address the translations use reaches a hook argument, a
compared access or a compared store, so the test confirms each one.

## Scope: what ran, and what these functions reach

- The census's 16 rows: overlay 0x823700, 0x823B40, 0x823B90, 0x823EE0,
  0x824100, 0x8241F0, 0x824490, 0x8246B0, 0x824DC0, 0x825880, 0x825B00,
  0x825D60, 0x825DF0, 0x8260C0; boot 001BE5F0 (census status: inline asm)
  and 001C1A80 (NEARMISS).
- Reached callees added here: 0x823580 (0x823700 calls it when the lock
  bit is clear), 0x824830 (0x8246B0 with D_008107EA 0 or 0x10) and 0x824930
  (1, 2, 0x20 or 0xFF). 0x823580 ran in a02_05's exit phase (census:
  "already ran"), 0x824830 / 0x824930 did not run on the route; none had a
  port translation. 001C1A80 calls 001BE5F0, which is also its own entry.
- Existing translations: a grep of the port for each of the 19 addresses
  finds no translation. The hits are route docs (FIFTH_LEVEL_ROUTE.md, the
  AREA00/AREA01 overviews listing 0x1C1A80 placements and 1BE5F0 in their
  census lists, AREA01_ROOM.md's note that 001D0D60 also serves 001C1A80)
  and `em_script_host_workers.c`, whose 0x825880 is an AREA11 script beat
  address, not this function. Nothing is reused; nothing existing is
  changed.
- Ground truth. Overlay: the decomp's C under `src/overlays/AREA04/`,
  byte-identical for all 17 (decomp docs/AREA04_OVERLAY.md; none of the 17
  is in the NEARMISS two-function slot 00825240, which none of them
  calls). Boot: `src/func_001BE5F0.c` is inline asm and
  `src/func_001C1A80.c` is NEARMISS C, so both translations were written
  from the original instructions (read locally; nothing reproduced). The
  NEARMISS text passes a second argument to 001D0D60 (0x30000000 in state
  0, `self` in state 1); in the original that register is not an argument:
  001D0D60 takes a0 and f12 (docs/AREA01_ROOM.md), and the test's register
  rehearsal of 001D0D60, run as original code with a1 poisoned, proves the
  result and writes do not depend on it. For every function the order of
  loads and stores between calls and the float operations (operand order,
  the MUL/ADDA/MADD distance of 0x823700, the MULA/MADD distance of
  001BE5F0, the reel's `!(x <= limit)` band tests) were read from the
  original instructions. What the test pins of them: the load and store
  order (every access compared one for one); the order of 0x823700's
  three-term sum (dx*dx + dy*dy, then dz*dz) by four designed positions
  with three non-zero axes, each of which gives a different last bit under
  both other orders (the capture and earlier designed positions all had an
  axis at 0, so the review found a reordering surviving); the band tests
  at their limits. Not pinned, because it cannot be told apart under the
  em_ee model: which operand pair of 001BE5F0's two-term sum goes to MULA
  and which to MADD (the reviewer measured 0 differences over 20M random
  bit patterns), and whether 0x823700 folds dy*dy through ADDA of the
  rounded product or through MADD of the raw operands (likewise 0 in
  20M). The translation follows the original there; the test does not
  prove that choice.

## 1. Interface (the AREA02 overlay design)

The design is `em_area02_overlay.h`'s (docs/AREA02_OVERLAY_PORT.md section
1), with these differences:

- **Entries.** 16 owner entries `(hooks, self, fault)`; 0x824490 also takes
  `sp`; three entries return a result: 0x823580 `(self, who)` (0 / 1),
  0x823B40 `()` (always 1) and 001BE5F0 `(player, actor, block)` (0 / 1).
  0x823700 passes `who` = self + 0x1F0; 001C1A80 passes (0x8102B0, self,
  self + 0x1F0).
- **Stack locals.** Only 0x824490 has them: it copies the 64 bytes at
  0x828220 (four 16-byte loads, then four 16-byte stores, each as two
  8-byte accesses, low half first) to sp - 0x40 .. sp - 0x01 and passes
  sp - 0x40 to 001B1EA0. `sp` must be 16-byte aligned (the EE's quadword
  accesses ignore the low four bits; the translation does not).
- **Hooks.** 57 boot-function hooks named by original address, plus the
  +0x4C callback. Float results (cos, sin, fabs, sqrt, 001B1240, 001B1470)
  come back as the float whose bits are the original's. 001B17A0,
  001C64F0 and 001B6660 carry their results (the original uses 001B17A0's
  in 0x824100 / 001C1A80, stores 001C64F0's low half in 0x824490 and
  001B6660's at +0x240 in 0x824830).
- **Fail-stop.** As AREA02: fault 5 (unmapped or misaligned address, or
  `bytes` NULL), 1 (reached NULL hook), 2 (a hook returned < 0). After a
  fault no hook runs, `bytes` is not called again, writes are dropped, the
  first fault is kept, no result is written, and the entry returns -1. A
  fault latched on entry, a NULL hook table, a NULL fault pointer and
  (entries with a result) a NULL result pointer return -1 at once.
- **EE arithmetic.** Every float operation is `em_ee_float.h` on bit
  patterns (add/sub, mul, CVT.S.W, MUL + ADDA + MADD, MULA + MADD, the
  compare key c.lt / c.le), with the original's operand order.

## 2. What each function does (behaviour, from the code)

Node roles are those the route capture measured (FIFTH_LEVEL_ROUTE.md
sections 1.1, 2.3 and 2.4); pool addresses are those of the a04 images.
"Callback" is the function at +0x4C, called with the node.

| Entry | Node | Behaviour |
|---|---|---|
| 0x823580 | (from 0x823700; door [45] 0x7B20F0) | Only with +0x0B bit 2, else 0. a = 001B1470(001B1240(+0xB0, player x, player z) - +0xC4); when fabs(a) <= pi/2: player yaw (0x810374) = 001B1470(pi + +0xC4), +0x2E = 0; else = 001B1470(+0xC4), +0x2E = 1; with +0x03 == 0x16 +0x2E = 1 - +0x2E. The point (x - 6 sin(yaw), player y, z - 6 cos(yaw), 1) at 0x700038A0 goes to 00182F90(0x8102B0, point); 001BA1A0(who, 0x8272A0), 001BA1F0(self); returns 1. |
| 0x823700 | door [45] 0x7B20F0 | **+0x04** 0: 001BB520(self, +0x1F0), +0 = 1. 1: **+0x05** 0: when D_00810841[D_00810700] & (1 << (+0x34 & 31)): 001BB560(self, blk, 0) != 0 -> 2; else 0x823580 != 0 -> +5 + 1. 1: 001BB7C0 != 0 -> +0x0B = +5 = 0. 2: 001BB7C0 != 0 -> +5 + 1. 3: 001BC150, +5 + 1. 4: 001BB7F0 != 0 -> +5 = 0. Then 001C6380, callback, and when sqrt((px-x)^2 + (py-y)^2 + (pz-z)^2) <= 20 (MUL, MUL, ADDA, MADD): +1 = 1 and, with +0x02 bit 7, 001B1DE0. 2 / 3: 001AFC10. |
| 0x823B40 | (op09 callback of records 0x827B50 and 0x827C10) | D_00810845 bit 3 clear: set it, 001FB9F0(0x3EE, 0x1000, 0x1000, 0x1000). Returns 1. |
| 0x823B90 | the director [1] 0x7A9FB0 | **+0x04** 0: with D_00810845 bit 5 or 001BA1C0(self, 0xC) != 0: D_00810764 = 0xFF, state 3; else script 0x8275A0 + 001FABB0 (D_008107E4 == 0) or 0x8278D0, +4 + 1. 1, by D_008107E4: 0: when 0x70003B8D != 0, (signed) +0x1FC == 1 and 0x70003B84 == 0xAA: 001B1E20(2, 0xF0); at the script's end +0x2E = 0xFFFF, player placed at (440.1, 14.9, 356.4, 1) facing pi (001B6F80), script 0x8278D0, 001B6660(0x8268D0), 001FB0B0(0xF). 1 / 2, **+0x05** 0: bit 5 -> +0 = 2, D_00810764 = 0xFF, state 3; else player y (0x810354) <= 16 and 001B1EA0(0, 0x810360, 0x827CD0, 4) != 0 -> +5 + 1, D_008107E4 = 2, 001FABB0, 0x70003B84 = 0. 1: at the script's end the point (440.4, 14.9, 114.4, 1), 001B6F80(point, 0), +0x2E = 0xFFFF, state 3, 001C4760(6, 1), 001FAE70(0). 3: 001AFC10. |
| 0x823EE0 | [47] 0x7B26D0 | Reads +0x18, +0x04 and (+0x18)->+0x18 (the link) at entry. **+0x04** 0: D_00810764 != 0xFF -> +0x0D = 0x827530[8 D_00810701]; 001B0FD0; 001CA5E0(self, +0x44, 1); D_00810764 == 0xFF -> state 2. 1: **+0x05** 0: D_00810764 == 0xFF -> state 2, else D_008107E4 == 2 -> +5 = 1. 1: when 0x70003B84 >= 0x55D, or D_00810764 == 0xFF and +0x0D == 0x827530[8 D_00810701]: state 2, +0x0D = 0x827534[8 D_00810701], 001CA6E0(self, 001C6120(D_0028A59C, +0x0D)), 001C62C0, 001CA5E0(self, +0x44, 1). Then 001C6380, callback. 2: (*D_00275B40)+0x7C = ((link +0x110) +0x7C); 001C6380, callback. 3: 001AFC10. |
| 0x824100 | group 0x8268D0's node (0x7BBCA0 in a02_05) | **+0x04** 0: 001B10B0(self, 0x4E, 0x52) == 0 -> 001C63E0(self, 0), +4 + 1. 1: 001C64F0(self, 1.0), 001C68C0, callback when 001B17A0 != 0; D_008107E4 == 2 -> state 3. 2 / 3: 001AFC10. |
| 0x8241F0 | [61] 0x7B4FF0 | **+0x04** 0: 001BA1C0(self, 0xD) or (self, 0x65) != 0 -> state 3; else +0 = 1, state 1. 1, while D_0081083D: **+0x05** 0: script 0x827D10, +5 = 1; 1: at the script's end 001B6660(0x826600), D_008107E5 = 0xFF, state 3. 2 / 3: 001AFC10. |
| 0x824490 | NPC [2] 0x7AA2A0 (from 0x824320) | Copies 0x828220 (64 bytes) to sp - 0x40. **+0x05** 0: 001B1EA0(0, 0x810350, sp - 0x40, 4) == 1 -> script 0x827D90, +5 = 1. 1: at the script's end +0x2E = 0xFFFF, +5 = 0, +0x40 = D_0028A5D8, 001C67E0(self, 0, 0, 0), 001C47A0(0x23, 1), 001C4760(8, 1), D_008107E9 = 1, 001FAE70(0); then 001BA580(self, +0x0D) and +0x1FE = 001C64F0(self, 0.5). Other steps: 001BA580(self, +0x0D), 001C64F0(self, 1.0). |
| 0x8246B0 | [3] 0x7AA590 | **+0x04** 0: 001BA1C0(self, 0x12) != 0 -> state 3; else 001B10B0(self, +0x0D, 0x6D), 001C63E0(self, 0), +0x58 = D_0028A6F8, state 1, +0 = 1. 1, while D_0081076A: D_008107EA 0 / 0x10 -> 0x824830, 1 / 2 / 0x20 / 0xFF -> 0x824930; then with D_008107EA (re-read) != 0: 001C64F0(self, 1.0), 001B17A0, 001C68C0, callback. 2 / 3: 001AFC10. |
| 0x824830 | [3] | **+0x05** 0: D_008107EA == 0 -> +5 = 1, script 0x828450; else +5 = 2, +0x240 = 001B6660(0x826790), 001B6660(0x8267F0). 1: at the script's end +0x2E = 0xFFFF, the same two 001B6660 calls, D_008107EA = 0x10, 001FAE70(0), +5 = 2. |
| 0x824930 | [3] | **+0x06** 0: with an object at +0x240: 00121870(self + 0x2E0, object + 0xB0, 16), script 0x8286D0, +6 = 1; without: 001B6660(0x827230), 001EFD20(0x8000005B, 0x828B90), D_0081076A = D_008107EA = 0xFF, +6 = 2. 1: at the script's end +6 = 2. 2: D_008107EA = 0xFF, +6 = 3. |
| 0x824DC0 | [4] 0x7AA880 | **+0x04** 0: 001BA1C0(self, 0x14) != 0 -> state 3; else +0 = 1, state 1. 1, while D_0081076C == 1: **+0x05** 0: script 0x828BE0, +5 = 1; 1: at the script's end 0x70003B8D = 3, D_0081076C = 0xFF, state 3. 2 / 3: 001AFC10. |
| 0x825880 | [66] 0x7B5EA0 | **+0x04** 0: 001B0FD0 == 0 -> state 1, +0 = 1, +0x30 = 0x275928, 001C6380. 1: 001B17A0; with +0x0B bit 2: script 0x82C1F0, state 4, 001BA1F0, (+0x18)+0x28 = 120 (halfword), +0x28 = 0; callback; the marker. 4: +0x28 < 120 -> + 1, and at 120 001FBD50(self, 0x19A, 0, 300); at the script's end state 1, +0x0B = 0; the marker; callback. Other states: 001AFC10. The marker: 0x700038A0 = (+0xB0, +0xB4 - 0.04, 0.34 + +0xB8, 1), 0x700038B0 = words (0, 0x80, 0, 0x80), 001F4BF0(0x700038A0, 0x700038B0). |
| 0x825B00 | [65] 0x7B5BB0 | Words +0x1F0 (moving), +0x1F4 (direction), floats +0x1F8 (base), +0x1FC (offset). **+0x04** 0: 001B0FD0 == 0 -> +0x28 = 0, +0x1F0 = 0, base = +0xB4; with D_00810834: direction 1, offset 10, +0xB4 += offset; else 0 / 0; 001C6380, 001A2370(self, self + 0xD0), state 1, +0 = 1. 1: +0x28 counting down to 0 sets moving and plays 001FBD50(self, 0x451 (direction 0) / 0x452, 0, 300). While moving: direction 0: offset + 0.1923077, past 10 (not <=) -> 10, stop, direction 1, D_00810834 = 1; direction 1: offset - 0.1923077, below 0 -> 0, stop, direction 0, D_00810834 = 0; +0xB4 = base + offset; then (any direction) 001C6380, 001A2370. Then 001B1B70, callback. Other states: 001AFC10. |
| 0x825D60 | [68] 0x7B6480 | **+0x04** 0: 001B0FD0 == 0 -> 001C6380, state 1, +0 = 1. 1: callback. Other states: 001AFC10. |
| 0x825DF0 | the console [69] 0x7B6770 | **+0x04** 0: 001B0FD0 == 0 -> with D_0081083B state 4, +0x2A = +0x28 = 0, else state 1; +0 = 2, +0x30 = 0x82C630, 001C6380; +0x2EC = 001C5570(self, (0, 1, 0, 0.25) at 0x700038A0, 0xC, 0); that object's +0xB0 += 0.7, +0xB4 -= 0.1 (+0x2EC re-read each time). 1: +0 = 1 when D_00810702 == 4 else 2; 001B17A0; with +0x0B bit 2: script 0x82C3B0, state 4, 001BA1F0, (+0x18)+0x28 = 120, +0x28 = 120, +0x2A = 60; callback. 4: with +0x0B != 0 and the script's end: +0x0B = 0, +0x28 = 1, +0x2A = 0, (+0x1C)+4 = 0x64, D_0081083B = 0xFF. Then +0x28 != 0: - 1, at 0 with an object at +0x2EC: 001FBD50(self, 0x19A, 0, 300), object +4 = 3, +0x2EC = 0; else +0x2A != 0: - 1, at 0: 001FBD50(self, 0x455, 0, 300), D_0081083B = 0xFF, (+0x1C)+4 = 1. Callback. Other states: 001AFC10. |
| 0x8260C0 | the reel [70] 0x7B6A60 | **+0x04** 0: 001B0FD0 == 0 -> with D_0081083B at (380, 14.9) with +0xC8 = 0, else +0x30 = 0x82C640; 001C6380, 001A2370, state 4, +5 = 0, +0 = 1. 4: **+0x05** 0: +2 = 4 (D_0081083B) or 0x84; with +0x0B bit 2: the word 0x82C724 = 0xF3, script 0x82C650, +5 + 1. 1: at the script's end +5 = +0x0B = 0. Then 001B1B70, callback. 1: **+0x05** 0: +5 = 1, +0x28 = 700. 1: +0x28 - 1; at <= 0 state 0x64; else by +0xB0 (each test `!(x <= limit)`): > 499: x - 0.2; > 497: x - 0.2, y - 0.08944256, +0xC8 + 0.023182336; > 495: x - 0.17888552, y - 0.0894, +0xC8 + 0.0232; > 428: x - 0.1789, y - 0.0894; > 426: x - 0.1789, y - 0.0894, +0xC8 - 0.0232; > 424: x - 0.2, +0xC8 - 0.0232; > 395: x - 0.2; else (380, 14.9), +0xC8 = 0; then 001C6380, 001A2370. Then 001B17A0, callback. 0x64: (380, 14.9), +0xC8 = 0, state 4, +5 = 0, 001C6380, 001A2370, 001B17A0, callback. Other states: 001AFC10. |
| 001BE5F0 | (from 001C1A80) | With the byte 0x70003B8D != 0: 0. Else 1 when sqrt((player +0xA0 - actor +0xB0)^2 + (player +0xA8 - actor +0xB8)^2) (MULA, MADD) <= record[0] and fabs(player +0xA4 - actor +0xB4) <= record[1], record = block +0x18 (read twice); else 0. |
| 001C1A80 | two AREA22 owners after the a04_05 arrival (0x7AC5E0, 0x7AC8D0; +0x208 = 0x275660: 6.3, 2.0) | **+0x04** 0: 001D0C80(self, D_0028A508), 001D0D40(self, 0x24F8F0, 0x28, 1), 001C62C0(self), +0x30 = +0x208 = 0x275660, state 1, +0x0A = 0, +0x28 = 240 + ((rand >> 16) 240 >> 15), 001D0D60(+0x90, 1 + 19 (2^-31 CVT(rand))). 1: 001B17A0 == 0 -> +0x0A = 0; else +0x28 != 0 -> - 1, else 001FBD50(self, 0x441, 0, 300) and +0x28 = 300 + ((rand >> 16) 180 >> 15); then 001BE5F0(0x8102B0, self, self + 0x1F0): 0 -> +0x0A = 0; 1 -> 00187EC0(8, 0), and with +0x0A == 0: +0x0A = 1, 00102948(self + 0xA0, 0x810350); else +0x0A = 1, 001028D0(0x700038A0, 0x810350, self + 0xA0), 00102900(0x700038A0, 0x700038A0, -0.7), 00183010(0x8102B0, 0x700038A0), 00102948(self + 0xA0, 0x810350). Then 001C6380, 001D0D60(+0x90, 1.0), callback. 2 / 3: 001AF890(+0x90), 001AFC10. |

## 3. Verification

`python3 tools/test_area04_port_reference.py` (port root, macOS arm64 or
Linux; no make target yet: the Makefile belongs to another chain while this
side track runs). It compiles the module into `build/area04/port/area04_port.dylib`
with `-std=c11 -Wall -Wextra -Werror -Wpedantic -ffp-contract=off`. At most 4
worker processes (EM_TEST_JOBS overrides). `EM_AREA04_PORT_ONLY=<label
prefix>` runs a subset (no coverage, fail-stop or contract checks) for
debugging.

- **Oracle and harness.** The AREA02 overlay harness
  (docs/AREA02_OVERLAY_PORT.md section 3), reused: FallEE runs the original
  code resident in the recorded RAM of 11 images: the a02_05 end (the
  AREA04 arrival, decomp `build/s87/route_a02/a02_05_progression_exit`),
  the ends of a04_00..a04_04 and a04_s0..a04_s3 (`build/s87/route_a04/`),
  all with AREA04 resident, and the a04_05 end (AREA22 resident, the two
  001C1A80 owners) for the two boot entries only. Before any case the test
  asserts that each AREA04 image's overlay text (0x823540..0x826600, size
  from the file header) equals the user's `extract/OVERLAY/AREA04.BIN` and
  that every image's boot text equals the pinned ELF. AREA04 has no jump
  tables, so no original load is left out of the access comparison.
  Nested calls between the translated functions run as original code at
  the top level of the oracle.
- **Stack window.** As AREA02: the oracle enters with sp = STACK_TOP; the
  0x400 bytes below are compared memory, the frame bookkeeping excepted;
  0x824490's copy of the area is compared store for store.
- **Callees run as original code**: 0011DE90, 0011DF78, 0011E2A8,
  0011E748, 001028D0, 00102900, 00102948, 00121870, 00122BB8, 001B1240,
  001B1470, 001BA1A0, 001D0D60 (each first rehearsed with its unused
  argument registers poisoned: this is what proves that 001D0D60 does not
  read a1). All others are stubbed with scripted results; some stubs
  scribble memory.
- **Compared, per case**: the calls and their arguments; memory at every
  call entry before the callee's writes are replayed; the memory accesses
  between calls one for one, in order, by address, size and
  changed-or-not; all memory after the last store (32 MiB, scratchpad,
  window); the result of 0x823580 / 0x823B40 / 001BE5F0; the store-log
  self-check; stops at unmapped or misaligned original accesses; every case
  again from a poisoned start image; coverage of every reachable original
  word; the table's ctx at every call.
- **Fail-stop.** `fault_checks` ([66] 0x825880, state 1 with +0x0B bit 2:
  NULL hook, failing hook, unmapped address before any call and inside the
  callback read, latched fault, NULL hooks; every entry with a latched
  fault, a NULL hook table, a NULL fault pointer and, for the three
  entries with a result, a NULL result pointer) and `hook_contract_site`
  (the AREA02 contract sweep: on the passing cases that together reach all
  57 hooks, the callback and all 19 entries, every call failing, every
  memory access refused, each hook NULL / INT32_MIN / 1 / INT32_MAX,
  `bytes` NULL).

**Cases** (1,405 in the default run, all capture and designed cases):

- 164 capture cases: every owner node of every AREA04 image as captured,
  through each entry that takes it (the door through 0x823700 and
  0x823580, [3] through 0x8246B0, 0x824830 and 0x824930, the NPC [2]
  through 0x824490, the director both live (a02_05) and freed), 0x823B40
  on every AREA04 image, and 001C1A80 / 001BE5F0 on both AREA22-side
  owners.
- 1,241 designed cases: every state (including 2, 3, 4, 5, 0x63..0x65 and
  0xFF where the code has a default) and step of every function; callee
  results 0, 1, 2, -1 and 0x100, 0x10000, 0x80000000, 0x7FFFFFFF at the
  tests of a result; every flag and story byte on both sides of every test
  (D_00810845 bits 3 and 5, D_00810764, D_008107E4 0..3 / 0xFF,
  D_00810701 indices, D_0081076A, D_008107EA (all eight dispatch values and
  two others), D_0081076C, D_0081083B, D_0081083D, D_00810834, D_00810702,
  0x70003B8D, 0x70003B84 at 0xAA / 0x55C / 0x55D, +0x0B bit 2, +0x02 bit 7,
  +0x03 0x16); door [45]'s lock test over lock indices 0 / 4 / 0xFF, shifts
  0, 5, 31, 32, 37, -1, 0x7FFF, 16, 21, 48 and lock bytes with the bit set
  and clear, plus shifts 16 and 21 with D_00810845 = 0x01 (review round:
  `shift & 31` in 16..23 is above the 8-bit lock byte and never unlocks,
  which kills a wrong `& 15` mask); four player positions with three
  non-zero axes (bit patterns in the test) that pin the order of the
  20-unit distance sum;
  the talk turn's geometry (eight directions x four yaws x both +0x03
  kinds) and its pi/2 boundary (stubbed fabs: pi/2, its neighbours, NaN,
  -0, -inf); the 20-unit distance (stubbed sqrt at 20 and its neighbours,
  NaN, -0, 0) and real positions; the director's y <= 16 test (16, its
  upper neighbour, NaN, -inf, +-FLT_MAX, denormals); the counters of
  0x825880 (0x76..0x79, 0x7FFF, 0x8000, 0xFFFF), 0x825B00, 0x825DF0 and
  the reel (1, 2, 0, 0x8000, 0x8001, 0xFFFF); the lift's two limits (10 -
  0.1923077 and 0.1923077 with their neighbours, 10, 0, -0, NaN,
  denormals); every band limit of the reel (499, 497, 495, 428, 426, 424,
  395 and their neighbours, NaN, -inf, FLT_MAX, 380, -0, denormals) with
  small and extreme y / +0xC8; 001BE5F0's radius and height limits (real
  positions and stubbed sqrt / fabs at the limits, their neighbours and
  NaN); 001C1A80's random draws (0, +-extremes, -1, 0x10000 / 0xFFFF,
  0x7FFF0000 / -0x10000) and first / repeat contact; scribbling callees for
  every field the original re-reads after a call (+0x05 / +0x04 in the
  increments, +0x0D, +0x18, +0x1C, +0x44, +0x4C, +0x90, +0x1F0 / +0x1F4 /
  +0x1F8 / +0x1FC, +0x208, +0x28, +0x2EC, D_00810764, D_008107E4,
  D_008107EA, the player block); unmapped pointers (+0x18, the link chain,
  the bone, D_00275B40 and the object it names, a misaligned link, the
  spawned object of 0x825DF0, 001C1A80's +0x208 record).
- `EM_TEST_FULL=1` adds perturbed cases (60 seeded rounds over the live
  owner nodes of a02_05, a04_00, a04_01 and a04_02: states, steps, flags,
  story bytes, positions, counters and callee results) and compares all
  memory at every call entry.

**Measured** (2026-09-28 after the review round, M1, host shared with
other lanes): default run 1,405 cases, 2,435 runs (1,030 poisoned), 9,892
call entries compared, 3,221 helper register rehearsals, coverage 1,637 /
1,637 reachable words, hook contract 589 native runs on 38 cases, ~10 s
CPU (~3 s wall with 4 workers); `EM_TEST_FULL=1`: 2,097 cases, 3,383
runs, 11,840 call entries compared over all memory, ~49 s CPU (~13 s
wall). All pass.

**Mutation sweep** (one bounded sweep, `build/area04/port/sweep.py` and
`contract_survivors.py`, not committed; mutant list
`build/area04/port/muts.json`). 2,764 single-operation mutants of
`em_area04_port.c`: every float constant with its lowest bit flipped, every
address macro + 4, operator swaps (`==`/`!=`, `<`/`<=`, `>`/`>=`,
`&&`/`||`, add/sub, mul/div, lt/le, madd/msub, mula/mul, access widths,
masks and shifts, `r != 0` as `== 1` / `> 0`), every integer literal +-1,
every store, call, callback and helper call dropped, every `if (hook)
return;` turned into a plain call (the failure ignored), and every pair of
adjacent statements swapped. Each mutant ran in a forked child against the
case list (the mutated function's cases first) until the first failing
case, then `fault_checks`; the survivors then ran the hook contract. 193 do
not compile. Of the 2,571 that run:
- **2,350 killed**: 2,342 by the cases or `fault_checks`, 8 more by the hook
  contract (an entry with a result returning -2 or 0 after a fault, or
  writing its result before the fault test).
- **221 equivalent, none unexplained**:
  - 124 turn `if (a04_c_X(...)) return;` into a call that ignores its
    failure: after a fault every hook is skipped, `bytes` is not called,
    writes are dropped and the entry returns -1, so continuing is inert.
  - 22 change the initial value of a result variable before a hook (every
    hook writes `*result` on success; a skipped or failed hook latches a
    fault first).
  - 12 return 1 instead of 0 after a failed hook inside 0x823580 or
    001BE5F0: their entries return -1 without writing the result, and
    their callers (0x823700, 001C1A80) test the fault before the value.
  - 31 swap a declaration or a pure local computation (a sub or product of
    values already loaded, an address sum) with an adjacent statement: the
    order of the memory accesses is unchanged.
  - 9 move an `a04_failed` test across the statement before or after it
    (inert after a fault, as above); 1 calls `a04_open` before the NULL
    test of the hook table (it does not dereference either pointer).
  - 11 change only a value tested for truth or sign: `go = 2` (twice), the
    reel band's drop / roll flags as 2 / -2 (eight), `roll >= 0` where roll
    0 never reaches the test; 2 grow the local arrays of 0x824490.
  - 3 in `a04_begin`: -2 instead of -1 for a NULL table or fault pointer or
    a fault latched on entry (callers test truth), or 0 with a fault latched
    on entry (the body then runs inert and `a04_end` returns -1).
  - 2 flip the lowest bit of 0.2 and of 0.17888552, which are subtracted
    only from the reel's x when !(x <= 395): an exhaustive check over every
    such x bit pattern (0x43C58001..0x7FFFFFFF, `em_ee_sub_bits` with each
    constant and its flip; scratch `build/area04/port/flip_proof.c`) finds
    no x whose result differs.
  - 1 each: `r != 0` as `r == 1` and as `r > 0` after 0x823580 (it returns
    0 or 1); `v | 8` as `v ^ 8` in 0x823B40 (only reached with bit 3 clear);
    MULA as MUL (`em_ee_mula_bits` is defined as the MUL result).
- The sweep did not iterate: it is one bounded sweep, and it covers these
  mutants only. Its equivalence list was not exhaustive about the test:
  the review's own 44-mutant sweep found two non-equivalent survivors in
  0x823700 that this sweep's operator set did not generate (a `& 15` lock
  mask and a reordered three-term distance sum). Both are killed by the
  review-round cases above (checked against the two mutants: 4 and 4
  failing cases; the third order, dy*dy + dz*dz then dx*dx, fails 3). No
  new sweep was run.

## Binding

Nothing calls these entries. To run them live a host must:

- supply `bytes` over the scene's original-byte storage (actor pool, the
  0x810000 story block and the player block 0x8102B0, boot data such as
  0x24F8F0 / 0x275660 / 0x275928 / 0x275B40 / 0x28A508 / 0x28A59C /
  0x28A5D8 / 0x28A6F8, the overlay's data at 0x823500.., the scratchpad)
  and map [sp - 0x40, sp) for 0x824490;
- dispatch the owners from the pool loop by the +0x10 behaviour word
  (0x823700, 0x823B90, 0x823EE0, 0x824100, 0x8241F0, 0x8246B0, 0x824DC0,
  0x825880, 0x825B00, 0x825D60, 0x825DF0, 0x8260C0, and 001C1A80 in
  AREA22); 0x824490 is reached only through 0x824320 (NPC [2]), which is
  not translated (below); 0x823B40 is the callback of two op09 records,
  0x827B50 (in script 0x8278D0) and 0x827C10 (the overlay's data holds its
  address at 0x827B54 and at 0x827C14; the review infers 0x827C10 is also
  inside script 0x8278D0, not established here), and the script host must
  call it for both;
- bind each hook to a verified port translation or a fail-stop stand-in.
  The stubbed callees are not verified here: 00182F90, 00183010, 00187EC0,
  001A2370, 001AF890, 001AFC10, 001B0FD0, 001B10B0, 001B17A0, 001B1B70,
  001B1DE0, 001B1E20, 001B1EA0, 001B6660, 001B6F80, 001BA1C0, 001BA1F0,
  001BA580, 001BB520, 001BB560, 001BB7C0, 001BB7F0, 001BC150, 001C4760,
  001C47A0, 001C5570, 001C6120, 001C62C0, 001C6380, 001C63E0, 001C64F0,
  001C67E0, 001C68C0, 001CA5E0, 001CA6E0, 001D0C80, 001D0D40, 001EFD20,
  001F4BF0, 001FABB0, 001FAE70, 001FB0B0, 001FB9F0, 001FBD50, and the +0x4C
  callbacks (0x1CAA00 / 0x1CAF60 at the AREA04 nodes, 0x1CB360 at the
  001C1A80 owners). The helpers run as original code in the test still
  need port translations of their own (several exist in other lanes, e.g.
  001D0D60 in `em_area01_room.c`); a binding lane must pair them by
  address and check each pairing.

## Known gaps

- **Not translated**: 0x824320 (NPC [2]'s behaviour, the only caller of
  0x824490; census: ran in a02_05's exit phase) with its second sub-state
  0x8245F0, and 0x825510 (the behaviour of lift [62] 0x7B52E0 and of node 0x7B55D0;
  also ran in a02_05's exit phase). They are outside this lane's 16 and are the next
  AREA04 translation items; the rest of the overlay (0x823920, 0x8239A0,
  0x823A90, 0x823B10, 0x824A40, 0x824A90, 0x824EF0, 0x825040, 0x8251C0,
  0x825280 / 0x825310) did not run on the route.
- **Stubbed callees' inputs.** Only the RUN helpers are proven not to read
  a register their hook does not pass; for stubbed callees the argument
  lists rest on the callers' instructions (which registers are set) and
  the callees' definitions.
- **Callee result ranges.** Results are scripted beyond the real ranges;
  what the real callees return is theirs to prove.
- **What the harness cannot see** (as AREA02): whether an access that
  changed nothing is a load or a store of the same value; hardware
  behaviour past a stop; the frame bookkeeping; an unaligned `sp` for
  0x824490 (the EE would align the quadword accesses).
- **Live evidence.** The captures show the owners at the end of each beat
  only: the console's Use and countdowns, the reel's roll, the director's
  event, door [45]'s talk turn, [3]'s and [4]'s scripts and the lift's
  motion are covered by designed cases, not by a captured image of those
  frames; 0x824830 / 0x824930 and 0x824490's step 1 have no capture in
  the state that runs them.
- **Float proofs rest on the EE model** (`em_ee_float.h`).
- **Not covered**: a hook that re-enters an entry with the same fault record.
