# AREA02 overlay owners (level-3/4 lane A02OVL)

Lane A02OVL of the level-3/4 side track, 2026-09-28. It covers the 15 AREA02
overlay functions that the AREA02 route census ran (decomp
`build/s87/census/a02_delta.json`, `new_functions`, region `overlay:AREA02`),
plus 0x825520, the one overlay function they reach that the route did not
run. All 16 are translated and compared with the original instructions by
`tools/test_area02_overlay_reference.py`. What that comparison covers is
exactly what section 3 states: the comparisons the test makes, on the cases
it runs. Nothing is bound: no port code calls these entries yet.

| File | What |
|---|---|
| `src/game/em_area02_overlay.h` | public API: 16 entries, the hook table, fault codes |
| `src/game/em_area02_overlay_internal.h` | memory view, fault latch, one typed wrapper per hook |
| `src/game/em_area02_overlay.c` | the 16 translations |
| `tools/test_area02_overlay_reference.py` | original-instruction oracle and comparison |

Addresses are runtime addresses. The overlay is linked 0x40 below where it
runs, so each decomp/splat name is 0x40 lower (0x8242F0 is
`func_overlay_AREA02_008242B0`). Data names in the decomp C
(`D_overlay_AREA02_<addr>`) are runtime addresses; the test confirmed each
one, because every script, table, record and data address reaches a hook
argument, a compared access or a compared store (the area init's two
stored addresses included).

## Scope: what ran, and what these functions reach

- The census's 15 AREA02 rows: 0x823580, 0x823900 (the area init), 0x823930,
  0x823980, 0x823D70, 0x824020, 0x8242F0, 0x824800, 0x824910, 0x824AC0,
  0x824C40, 0x824CD0, 0x824D50, 0x824FA0, 0x825100. The two AREA02
  functions the census did not see are the entry pad 0x823540 (an asm nop,
  nothing to translate) and 0x825520.
- Unlike AREA00's, these functions call each other: 0x823930 dispatches to
  0x823980 / 0x824020; 0x823980 calls 0x824D50; 0x824020 calls 0x824800 and
  0x8242F0; 0x8242F0 calls 0x824CD0, 0x824910, 0x824AC0; 0x824CD0 calls
  0x824C40; 0x825100 calls **0x825520** when D_0081083F is 2 (the turning
  object, never turned on the route). 0x825520 is translated here because
  0x825100's translation must reach it; it is compared with the original
  like the others (designed cases only, no capture reaches it).
- No existing port translation covers any of the 16: a grep of the port for
  each address finds only AREA11/AREA01/AREA00 functions that share an
  address in their own overlays (0x823580 in em_area01_overlay.c and
  em_area00_overlay.c) and `em_examine.c`/`.h`, which cite AREA02 0x824FA0
  as the "shape" of a legacy examine behaviour. That legacy module is not
  an original translation (its header says the message presentation was
  read off a capture); it is neither reused nor changed here.
- Ground truth: the decomp's C for all 16 is byte-identical
  (decomp docs/AREA02_OVERLAY.md; 15 link from C, 008254E0 = runtime
  0x825520 is marked NEARMISS only because the overlay fill tool cannot yet
  absorb the last function's text-end pad; its object is identical). The
  translation follows that C; the order of loads and stores between calls
  and the float operations (operand order, the multiply-add of 0x824D50,
  the negate of 0x825520) were read from the original instructions (local
  reading only; nothing reproduced) and are checked by the test.

## 1. Interface (the AREA00 / AREA01 overlay design)

The design is `em_area00_overlay.h`'s (docs/AREA00_OVERLAY_PORT.md section
1), with these differences:

- **Memory.** Every read or write of original memory goes through
  `bytes(ctx, address, size)`. Pointers in records stay original 32-bit
  addresses and are resolved again wherever the original dereferences them;
  fields are re-read where the original re-reads them. The two 16-byte
  copies of 0x824CD0 are two 8-byte accesses each (low half first).
- **Callees.** 39 boot-function hooks named by original address, plus the
  +0x4C callback. A hook returns >= 0 on success and writes the original
  result to `*result` where the original uses one. Float results (cos
  0011DE90, sin 0011E2A8, the angle wrap 001B1470, the turn step 001B12B0)
  come back as the float whose bits are the original's. 001CD520 takes its
  GIF tag as a 64-bit argument.
- **Calls inside the overlay** are direct calls between the translations
  (no hook), and every function is also a public entry.
- **Stack locals (new here).** 0x824910 keeps its two spawn points in its
  own frame (sp - 0x20, sp - 0x10; frame 0x60) and 0x824CD0 its two copied
  points and two transformed points (sp - 0x40 .. sp - 0x10; frame 0x60).
  Unlike AREA00's, the module itself writes these locals (through `bytes`)
  and passes their addresses to callees. The entries that reach them take
  `sp`, the original stack pointer at entry: 0x823930, 0x824020, 0x8242F0,
  0x824910, 0x824CD0. Each nested call gets sp minus the caller's original
  frame size (0x823930 0x10, 0x824020 0x20, 0x8242F0 0x40), so every local
  has its original address. A binding must map those frames for the module
  (and the callees' own frames below).
- **Entries with a result** (0x8242F0, 0x824800, 0x824D50) take an
  `int32_t *result`, written only on success.
- **Fail-stop.** As AREA00: fault 5 (unmapped or misaligned address, or
  `bytes` NULL), 1 (reached NULL hook, or NULL callback: the function read
  from +0x4C), 2 (a hook returned < 0). After a fault no hook runs, `bytes`
  is not called again, writes are dropped, the first fault is kept, no
  result is written, and the entry returns -1. A fault latched on entry, a
  NULL hook table, a NULL fault pointer and (entries with a result) a NULL
  result pointer return -1 at once.
- **EE arithmetic.** Every float operation is `em_ee_float.h` on bit
  patterns (add/sub with the operand pre-trim, truncating mul, div,
  CVT.S.W, MULA + MADD, NEG, the compare key c.lt / c.le), with the
  original's operand order.

## 2. What each function does (behaviour, from the code)

Node roles are those the route capture measured (FOURTH_LEVEL_ROUTE.md
sections 1.1 and 2.4); pool addresses are those of the a02_00..a02_04
images.

| Entry | Node(s) | Behaviour |
|---|---|---|
| 0x823580 | a spawned node: first run on the frame the car passed x 140 (census a02_02 f405), where 0x8242F0 calls 001EFD20(0, +0xB0); which call gives it its behaviour word was not traced (freed record 0x7C69F0 in a02_02..a02_04) | **+0x04** 0: counter +0x200 = 0, +0x04 = 1, falls into 1. 1: counter 0 and 10: 001EFD20(0x80000041, 0x700038A0) with the point (175, 10, 0, 1) / (175, 20, 10, 1); 60, 65, 70, 75: 001EFD90(0x80000074, 0x700038A0, 0x700038B0) with the points x = 170 / 140 / 110 / 80, y 20, z 10 and the vector (0, -pi/2, 0, 1), results to +0x1F0 / +0x1F4 / +0x1F8 / +0x1FC; at 70 byte +4 of the first result = 3 when that result is not 0. Then counter + 1, and past 0x50 +0x04 = 3. 2/3: 001AFC10. Other: nothing. |
| 0x823900 | area init | Five boot words: 0x275C28 = 0x275C24 = 0x20, 0x275C1C = 0x829280, 0x275C2C = 0, 0x275C18 = 0x969E80. |
| 0x823930 | [31] 0x7AF4E0 (+0x02 0x89), [32]..[35] 0x7AF7D0 .. 0x7B00A0 (+0x02 0x04) | +0x02 & 0x1F: 9 -> 0x823980, 4 -> 0x824020, other: nothing. |
| 0x823980 | the switch [31] | **+0x04** 0: D_00810761 set -> D_00810761 = D_008107E1 = 0xFF, +0x04 = 3, 0019C6F0(0x1D, 1), (0x1E, 1); else +0x30 = 0x827340, +0x04 = +0x00 = 1, +0x28 = +0x2A = 0. 1: by D_008107E1: 0 -> 001F4BF0(0x700038A0 = +0xB0 + (1, 15, -3.5), w 1; 0x700038B0 = words (0, 0x80, 0, 0x80)), and on +0x0B bit 2 D_00810761 = D_008107E1 = 1 and script 0x826780; 1 -> when 001BA1F0 is non-zero: 001FABB0, 001FA790(0, 0x13), D_008107E1 \|= 2; 0xFF -> when +0x06 == 0 and D_00282154 == 0: +0x06 = 1, 001FAE70(0). Then 001B17A0, and while D_008107E1 != 0, +0x05: 0 -> 0x824D50: 1 -> +0x05 = 1, 2 -> +0x05 = 8; 1 -> script 0x826F40, 2; 2 -> 3 at the script's end, then D_00810350 <= -25 -> -25 and then D_00810358 <= -66 -> -66; 8 -> script 0x826B40, 9; 9 -> 10 at the script's end, the same x clamp only. 2/3: 001AFC10. |
| 0x823D70 | the gates [36] (kind 16), [37] (15), [38] (14, x 185) and 0x7B0C60 (14, x -280) | **+0x04** 0: D_00810761 set -> kind 14 frees itself (+0x04 = 3), the others +0x05 = 1; then 001B0FD0, +0x04 = +0x00 = 1. 1: kind 14 and x < -270: D_008107E1 bit 2 -> +0x04 = 3; 001B1B70, 001C6380, callback. Kind 14 and x > 170: +0x05 0: bit 3 -> +0x05 = 1, +0x28 = 5; 001B1B70, 001C6380, callback. +0x05 1: +0x28 - 1, at 0 +0x04 = 3, 0019C6F0(0x1D, 1), (0x1E, 1); 001B1B70. Kind 15: +0x05 0 waits for bit 3, 1 animates. Kind 16: the same with bit 2. 2/3: 001AFC10. |
| 0x824020 | [32] the car (kind 7), [33] (8), [34] (11), [35] (10) | **+0x04** 0: D_00810761 set -> kinds 10/11: +0x05 = 1, 001B0FD0, 0019C6F0(1, 1); others +0x04 = 3. Else kinds 10/11: 001B0FD0; others: 0x824800 == 0 -> +0x04 = 3. Then 0x40 words at +0x1F0 = 0, 00121A28(+0x2B0, 0, 0x40), +0x04 = +0x00 = 1, +0x28 = +0x2A = 0. 1: kinds 7/8: +0x05 0 waits for D_008107E1 bit 1; 1: 0x8242F0 non-zero -> D_00810761 = D_008107E1 = 0xFF. Kind 10 (+0x0D re-read): +0x05 0 waits for bit 7 (001B6660(0x825C00), +0x05 = 1), 1 animates (001B1B70, 001C6380, callback). Kind 11 (re-read): waits for bit 6, then animates. 2/3: 001AFC10. |
| 0x8242F0 | kinds 7 (the car) and 8 | +0x28 + 1. Kinds 7/8: 001C64F0(self, 1.0), 001C68C0, callback; kind 7 (re-read) also 001B1B70 and 001A2370(self, (+0x110)[0] + 0x90). Kind 7 by +0x06: 0 script 0x826980, +0x06 = 1, +0x2E0 = 0, returns 0 at once; 1 -> 2 at the script's end (script 0x8269C0), turn to 0.314; 2, 3, 4 turn (0.436, 0.349, 0.209; rate 0.00297, the 4th 0.00593) then step at the script's end (0x826A00, 0x826A40, 0x826A80); 5 turn to -0.319 (rate 0.0089), -> 6 at the end; 6 D_008107E1 \|= 0x80, +0x06 = 7; 7 +0x04 = 3, returns 1. Then +0x07: 0 when !(x <= -280): D_008107E1 \|= 4, +0x07 = 1, 001F02C0(+0xB0, 0x8B3, 900), (0x8B4, 900); 1 when !(x <= 140): \|= 8, +0x07 = 2, 001F6B30, 001F02C0(+0xB0, 0x8B5, 300), 001EFD20(0, +0xB0); 2 +0x2E0 + 1. Then 001AA700, 0x824CD0, 0x824910, 0x824AC0, +0x2A + 1, returns 0. Kind 8 by +0x06: 0 script 0x826AC0, +0x06 = 1, +0x2E0 = 0; 1 +0x2E0 + 1, -> 2 at the script's end, turn to 0.534; 2 D_008107E1 \|= 0x40, 3; 3 0019C6F0(1, 1), +0x04 = 3; then +0x2A + 1, 0. Other kinds: 0. A turn is +0xC4 = 001B12B0(goal, +0xC4, rate). |
| 0x824800 | kinds 7/8 at their state 0 | 001CA6E0(self, 001C6120(D_0028A59C, +0x0D)), +0x40 = D_0028A6E8, +0x0C = 001C6150(+0x44) (a byte); when the halfword D_00275BCC < +0x0C: +0x04 = 3, returns 0. Else one 001AF780 result per bone to +0x110 + 4i (the count re-read each pass), +0x09 = the count, 001CB5B0(+0x0C), kind 7: 001C63E0(self, 0), kind 8: (self, 1); returns 1. |
| 0x824910 | the car | The 18 records at 0x827350 (id, done, point at +0x10): for each not done whose point x is below the car's x: id 0x80000001 spawns 0x80000013 at the point + (0, 10, 0) (w 1), id 0x80000002 spawns 0x8000002F there (the points are stack locals); then 001EFD20(id, point), done = 1, and 001F02C0(point, 0x449 / 0x44A / 0x44B, 200) by rand % 3 (negative remainders: none). |
| 0x824AC0 | the car | When the word 0x70003B68 % 8 == 0: four 001EFD20(0x80000030, 0x700038A0); each non-null e gets (10, 0, -20 + rand % 80, 1) at +0xB0, transformed in place by (+0x110)[0] + 0x90 (001026A0), x + 40, +0xC0 = pi (rand % 150 + 15) / 180, +0xC4 = pi (rand % 360) / 180, +0xC8 = 0. |
| 0x824C40 | a point | 001CD520(0, 2, point, tag 0x20045B0599421EF0, 14, 14, 4, k \| k << 8 \| k << 16) with k = 255 - (rand & 15). |
| 0x824CD0 | the car | Copies the points 0x827590 and 0x8275A0 to its frame, transforms each by (+0x110)[0] + 0x90 (001026A0, the bone read before each) and draws 0x824C40 at both results. |
| 0x824D50 | the switch | Owner = +0x1C. The points 0x827630.. through the owner's (+0x110)[0] + 0x90 into +0x2B0..+0x2E0 (001026A0, the bone read before each). When (ox - D_00810360)^2 + (oz - D_00810368)^2 < 4900 (MULA + MADD): 2 when D_00810350 < p1.x and !(D_00810350 <= p0.x), 001B1EA0(0, 0x810350, 0x8275B0, 4) == 1, -pi/2 < D_00810374 < 0 and D_0081050C == 3; else (D_00810350 re-read) 1 for p2 / p3, quad 0x8275F0 and -pi < yaw <= -pi/2; else 0. |
| 0x824FA0 | 0x7B0F50 (examine owner) | **+0x04** 0: when 001B0FD0 is 0: 001C6380, +0x08 = 3, +0x30 = 0x2758E0, +0x04 = 1. 1: +0x00 = 2 when 001BA1C0(self, 9) is non-zero else 1; +0x05 0 on +0x0B bit 2: +0x05 = (value read) + 1, script 0x827670; 1: at the script's end +0x0B = +0x05 = 0; then 001B17A0, callback. 2/3: 001AFC10. |
| 0x825100 | [27] 0x7AE920 (+0x03 4), [28] 0x7AEC10 (+0x03 7, the turntable) | **+0x04** 0: 001B0FD0, +0x30 = 0x829190, +0x00 = 1, +0x38 = 0, +0x04 = 1, D_0081083F = 0. 1, +0x03 7: +0x05 0: D_0081083F != 0 -> +0x05 = 1, script 0x828E10. +0x05 1, by D_0081083F: 2 -> 0x825520 then as 1; 1 -> +0x38 = 0.00374 if D_0081083F (re-read) is 2 else 0; at the script's end D_0081083F = +0x05 = +0x38 = 0; 001C6380, 001A2370(self, +0xD0). Then 001C6380 and 001B18F0(self, 0x700038A0, 0x700038B0) with (58, 0, 0, 1) / (0, 0, 58, 1), if 0 again with (40, 0, 40, 1) / (-40, 0, 40, 1); callback when either is non-zero. 1, +0x03 4: +0x05 0 on +0x0B bit 2: 001B6F00(self, (0, 0, 5, 1), pi), script 0x828C50, +0x05 = 1; 1: at the script's end D_0081083F = 1, +0x05 = 2; 2: D_0081083F == 0 -> D_0081083F = +0x0B = +0x05 = 0. Then while D_00810761 == 0: 001F4BF0 with +0xB0 + (1, 15, 0) (w 1) and the words (0, 0x80, 0, 0x80); 001C6380, 001B17A0, callback. 2/3: 001AFC10. |
| 0x825520 | (from 0x825100) | When D_008104C4 != 0, D_008102BA != 0 and that object's +0x0D == 9: 0x700031F0 = 1; dx = D_00810350 - ox, dz = D_00810358 - oz; x = ox + (dx cos + dz sin), z = oz + (dx (-sin) + dz cos) with cos / sin of 0.00374 (0011DE90 / 0011E2A8, called cos, sin, sin, cos); yaw D_00810374 = 001B1470(0.00374 + yaw). |

## 3. Verification

`python3 tools/test_area02_overlay_reference.py` (port root, macOS arm64 or
Linux). It compiles the module into `build/area02/ovl/area02_overlay.dylib`
with `-std=c11 -Wall -Wextra -Werror -Wpedantic -ffp-contract=off`. At most 4
worker processes (EM_TEST_JOBS overrides).

- **Oracle and harness.** The AREA00 overlay harness (docs/AREA00_OVERLAY_PORT.md
  section 3), reused: FallEE runs the original overlay code resident in the
  recorded RAM of the 7 images with AREA02 resident (decomp
  `build/s87/route_a01r/a01r_03_door16`, `build/s87/route_a02/a02_00..a02_04`
  and `a02_s0`). Before any case the test asserts that each image's overlay
  text (0x823540..0x825680, size from the file header) equals the user's
  `extract/OVERLAY/AREA02.BIN`, that the jump table (0x829200, 8 entries)
  equals the file and that the boot text equals the pinned ELF. Nested
  overlay calls run as original code at the top level of the oracle.
- **Stack window (new).** The oracle enters with sp = STACK_TOP (0x7F0F0000);
  the 0x400 bytes below it (the frames of the overlay's call chain; nested
  helpers run 0x400 lower) are compared memory. The function's own loads and
  stores there are logged and compared one for one like RAM, except the
  frame bookkeeping (a save or restore of $s0..$s7, $fp, $ra or $f20..$f31
  at an $sp offset), which has no counterpart in the translation. Writes of
  helpers run as original code into the window (001026A0's transformed
  points) are replayed. The window starts zeroed on both sides (plus the
  poisoned run's bytes) and is compared at every call entry (dirty set, or
  all of it with EM_TEST_FULL=1) and after the run.
- **Callees run as original code**: 0011DE90, 0011E2A8, 001026A0, 00122BB8,
  001B12B0, 001B1470, 001BA1A0 (each first rehearsed with its unused
  argument registers poisoned). All others are stubbed with scripted
  results; some stubs scribble memory. 00121A28 (the memset) is a stub: the
  test's EE core does not implement one of its MMI instructions.
- **Compared, per case**: the calls and their arguments (a 64-bit register
  for 001CD520's tag); memory at every call entry before the callee's writes
  are replayed; the memory accesses between calls one for one, in order, by
  address, size and changed-or-not (the only original loads left out are
  0x8242F0's jump-table loads); all memory after the last store (32 MiB,
  scratchpad, window); the result of 0x8242F0 / 0x824800 / 0x824D50; the
  store-log self-check; stops at unmapped or misaligned original accesses;
  every case again from a poisoned start image; coverage of every reachable
  original word; the table's ctx at every call.
- **Fail-stop.** `fault_checks` (the examine owner, state 1 step 1: NULL
  hook, failing hook, unmapped address before any call and inside the
  callback read, latched fault, NULL hooks; every entry with a latched
  fault, a NULL hook table, a NULL fault pointer and, for the three entries
  with a result, a NULL result pointer) and `hook_contract_site` (the AREA00
  contract sweep, its sites now chosen by targets per native run: on 30
  passing cases that together reach all 39 hooks, the callback and all 16
  entries, every call failing, every memory access refused, each hook NULL
  / INT32_MIN / 1 / INT32_MAX, `bytes` NULL; 679 native runs).

**Cases** (1,411 in the default run, all capture and designed cases):

- 115 capture cases: every owner node of every image as captured, through
  its behaviour word (0x823930, 0x823D70, 0x824FA0, 0x825100) and through
  each internal entry that takes it (0x823980 / 0x824D50 on the switch;
  0x824020 / 0x824800 on the kind-4 nodes; 0x8242F0 on kinds 7 / 8;
  0x824910 / 0x824AC0 / 0x824CD0 / 0x824C40 on the car while it is live), the
  freed 0x823580 record, and the area init and 0x825520 on all 7 images.
- 1,296 designed cases: every state (0..4 and 0xFF) and step of every
  function; callee results 0, 1, 2, -1 and 0x100, 0x10000, 0x80000000,
  0x7FFFFFFF at the tests of a result; every flag and story byte on both
  sides of every test (D_00810761, D_008107E1 bits 1/2/3/6/7 and their
  complements, and each bit the code ORs in already set and clear, D_0081083F, D_00282154, D_0081050C, +0x02 masks, +0x0B bit 2);
  the counter values of 0x823580 around every case value and past 0x50; the
  bone-count cap of 0x824800 (signed halfword, byte counts above 0xFF); the
  zone test of 0x824D50 with the four points scribbled by a stubbed 001026A0
  (every compare boundary of x and yaw, the squared distance at 4900, quad
  results); the triggers of 0x824910 with every id kind and scripted random
  words (negative remainders, INT32_MIN / MAX); the frame counter of
  0x824AC0 (negative remainders); float boundaries of every compare (-270,
  170, -280, 140, -25, -66 and their neighbours, -0 / +0, denormals, FLT_MAX);
  small positions (below the EE operand pre-trim); scribbling callees for
  every field the original re-reads after a call (+0x0D after the callback
  and after 001B6660 / 001AA700, +0x0C in the bone loop, D_008107E1, the
  player position and yaw after 001B1EA0, +0x4C before the callback, the
  bone pointer, the trigger records, D_0081083F); unmapped pointers (the
  first spawn of 0x823580, a spawned record, the bone of 0x824CD0, the
  object of 0x825520).
- `EM_TEST_FULL=1` adds 612 perturbed cases (60 seeded rounds over the live
  owner nodes of a02_00..a02_02: states, steps, flags, story bytes, x, callee
  results) and compares all memory at every call entry.

Angles of a magnitude where 2 pi is below one ulp are not used for
0x8242F0's turns: the original's wrap 001B1470 then never ends (the oracle
stops on its step limit), as it would on the PS2.

**Measured** (2026-09-28, M1, host shared with other lanes): default run
1,411 cases, 2,130 runs (719 poisoned), 26,274 call entries compared,
3,223 helper register rehearsals, coverage 1,618 / 1,618 reachable words,
~9 s CPU (~4 s wall with 4 workers); `EM_TEST_FULL=1`: 2,023 cases, 27,135
call entries compared over all memory, ~70 s CPU (~29 s wall). All pass.

**Mutation sweep** (one bounded sweep, `build/area02/ovl/sweep.py` and
`contract_survivors.py`, not committed; mutant list
`build/area02/ovl/muts.json`). 2,335 single-operation mutants of
`em_area02_overlay.c`: every float constant with its lowest bit flipped,
every address macro + 4, operator swaps (`==`/`!=`, `<`/`<=`, `>`/`>=`,
`&&`/`||`, add/sub, mul/div, lt/le, madd/msub, mula/mul, dropped negate,
access widths, masks and shifts, `%` / `/`, `r != 0` as `== 1` / `> 0`),
every integer literal +-1, every store, call, callback and helper call
dropped, and every pair of adjacent statements swapped (pure declarations
without a call left out). Each mutant ran in a forked child against the
case list (the mutated function's cases first) until the first failing
case, then `fault_checks`; the survivors then ran the hook contract. 139
do not compile. Of the 2,196 that run:
- **2,111 killed**: 2,099 by the cases or `fault_checks`, 12 more by the
  hook contract (an entry with a result returning -2 or 0 after a fault,
  or writing its result before the fault test). The review before this
  sweep (a first, aborted pass) added the cases for D_008107E1 bits already
  set (OR against XOR, OR 0x81 against OR 0x80) and for 001BA1F0 results 2 / -1
  / 0x80000000 in 0x823980's steps 2 and 9.
- **85 equivalent, none unexplained**:
  - 30 change the initial value of a result variable (`r`, `e`, `model`,
    `count`, `slot`) before a hook: every hook writes `*result` on success,
    and when it is skipped or fails a fault is latched, after which nothing
    observable happens and the entry returns -1.
  - 29 move such an initializer (or `float out`) across a statement that
    does not use it.
  - 11 swap a pure local computation or declaration (an address sum, a
    float operation on values already loaded) with an adjacent statement:
    the order of the memory accesses is unchanged.
  - 4 move an `a02_failed` test across the call before it: after a fault
    everything that follows is inert (no hook, no `bytes`, writes dropped).
  - 3 reorder independent statements: two ORs into the colour word of
    0x824C40, the two `dt = 1.0` assignments of 0x8242F0, and 0x823980's
    `zone == 2` / `zone == 1` tests (disjoint).
  - 1 calls `a02_open` before the NULL test of the hook table (it does not
    dereference either pointer).
  - 3 change only the value of a flag passed where the code tests truth
    (`a02_clamp_x(o, 2)`, `a02_trig(o, 2)` twice).
  - 1 replaces MULA by MUL: `em_ee_mula_bits` is defined as the MUL result.
  - 3 in `a02_begin`: returning -2 instead of -1 for a NULL table or fault
    pointer or a latched fault (the caller tests truth and returns -1), or
    0 with a fault latched on entry (the body then runs inert and `a02_end`
    returns -1).
- The sweep did not iterate beyond that review: it is one bounded sweep,
  and it covers these mutants only.

## Binding

Nothing calls these entries. To run them live a host must:

- supply `bytes` over the scene's original-byte storage (actor pool, the
  0x810000 story block, boot data such as 0x275BCC / 0x275C18.. / 0x282154 /
  0x28A59C, the overlay's data at 0x823500.., the scratchpad) and map the
  stack frames below `sp` for the five `sp` entries;
- call 0x823900 where the original runs the area init, and dispatch the
  owners from the pool loop by the +0x10 behaviour word (0x823930,
  0x823D70, 0x824FA0, 0x825100, and 0x823580 for the spawned node that
  carries it; which spawn sets that behaviour word was not traced);
- bind each hook to a verified port translation or a fail-stop stand-in.
  The callees are not verified here: 001EFD20 / 001EFD90 (spawns),
  001F02C0 / 001F4BF0 / 001F6B30 / 001FA790 / 001FABB0 / 001FAE70 (sound
  and markers), 001BA1C0 / 001BA1F0 (scripts), 001B0FD0, 001B17A0,
  001B18F0, 001B1B70, 001B1EA0, 001B6660, 001B6F00, 001A2370, 001AA700,
  001AF780, 001AFC10, 0019C6F0, 001C6120, 001C6150, 001C6380, 001C63E0,
  001C64F0, 001C68C0, 001CA6E0, 001CB5B0, 001CD520, 00121A28, and the +0x4C
  callbacks (0x1CAA00 at every node in the images that calls it; 0 at the
  switch, which never calls it). The helpers run as original code in the
  test (cos, sin, the wrap, the turn step, matrix x vector, rand, the script
  start) still need port translations of their own. Several callees have
  translations in other lanes; a binding lane must pair them by address and
  check each pairing.

## Known gaps

- **Stubbed callees' inputs.** Where a hook does not take a register the
  original happens to leave set (0x823930's a1 / a2 for its callees, which
  the decomp notes the callees do not read), only the RUN helpers are proven
  not to read it (register rehearsal). For stubbed callees that rests on
  their definitions.
- **Callee result ranges.** Results are scripted beyond the real ranges;
  what the real callees return is theirs to prove.
- **00121A28** is stubbed, so its clearing of +0x2B0.. is not in the
  compared memory of either side (the call and its arguments are).
- **What the harness cannot see** (as AREA00): whether an access that
  changed nothing is a load or a store of the same value; hardware
  behaviour past a stop; the jump-table loads; the frame bookkeeping.
- **Live evidence.** The captures show the owners at the end of each beat
  only (the car's run of a02_02, the switch's Use of a02_01 and the gates'
  countdown are covered by designed cases, not by a captured image of those
  frames); 0x825520 and the turntable's turning path have no capture.
- **Float proofs rest on the EE model** (`em_ee_float.h`).
- **Not covered**: a hook that re-enters an entry with the same fault record.
