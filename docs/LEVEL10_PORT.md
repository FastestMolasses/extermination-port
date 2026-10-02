# The tenth level: the new functions of the a19 / a13b route (lane L10T)

Lane L10T, 2026-10-01 (level side track). It covers the 18 functions the
tenth-level route census found new (decomp `build/s87/census/a19_delta.json`
and `a13b_delta.json`, `new_functions`: 13 + 5 rows, 8,640 bytes;
TENTH_LEVEL_ROUTE.md section 5): AREA19's entry-9 rooms (the a19 group: the
duct, the pickup g[2], the duct back), the climb back up into AREA13, door
[17], button [15], door [8], the lift, and the AREA04 load at entry 7
(`a13b_exit`). 17 had no verified port translation and are translated here;
one (AREA19 0x8298D0) has the same instructions as AREA01 0x828850, whose
translation `em_area01_ovl_00828850` is verified, and is reused: this lane
re-runs it against the AREA19 original over the tenth-level captures
(section 3.4).

Every translation is compared with the original instructions by
`tools/test_level10_port_reference.py`. What that comparison covers is
exactly what section 3 states: the comparisons the test makes, on the cases
it runs. Nothing is bound: no port code calls these entries yet.

| File | What |
|---|---|
| `src/game/em_level10_port.h` | public API: 17 entries, the hook table (62 hooks and the +0x4C callback), fault codes |
| `src/game/em_level10_port_internal.h` | memory view, fault latch, EE float helpers, one typed wrapper per hook (written from the test's HOOKS table; see section 1, Hooks), the internal entry points |
| `src/game/em_level10_port_area19.c` | AREA19 0x823580, 0x8250F0, 0x825240, 0x8255D0, 0x8257C0, 0x825AB0, 0x825C70, 0x825EE0, 0x826100, 0x827790, 0x829370 |
| `src/game/em_level10_port_boot.c` | 00118790, 001305B0, 001833F0, 001885F0, 001E6F60 and AREA13 0x8236E0 |
| `tools/test_level10_port_reference.py` | original-instruction oracle and comparison; the reuse check |

Addresses are runtime addresses. The overlays are linked 0x40 below where
they run (AREA19 0x823580 is `overlay_AREA19_func_00823540`, AREA13 0x8236E0
is `func_overlay_AREA13_008236A0`). Bracketed numbers ([27], [6] ...) are
the placement records of TENTH_LEVEL_ROUTE.md section 1.1; they and the
decomp's role comments are labels, not evidence. The behaviour below is read
from the code.

## 0. Scope: the census rows, and what already existed

A grep of the port (`src/`, `tools/`, `docs/`) for each address before this
lane found these; "hook only" means a worker slot, a call site or a stub,
not a translation. The AREA19 addresses that grep finds elsewhere belong to
other areas' overlays (the same runtime addresses hold other code there).

| Row | Bytes | Decomp | First (census) | Port before this lane | Here |
|---|---:|---|---|---|---|
| AREA19 0x823580, 0x8250F0, 0x825240, 0x8255D0, 0x8257C0, 0x825AB0, 0x825C70, 0x825EE0, 0x826100, 0x827790 | 492, 336, 468, 492, 356, 440, 624, 540, 868, 592 | C (byte-identical at link; 0x823580's jump table pinned) | a19_00 f1 | none | translated |
| AREA19 0x829370 | 1,232 | C | a19_00 f1 | none for AREA19; its AREA13 twin 0x826140 is `em_level9_port_00826140` (a separate entry with AREA13's tint record 0x82C9D0) | translated (from the AREA19 instructions; the same shape as the AREA13 translation) |
| AREA19 0x8298D0 | 408 | C | a19_00 f1 | `em_area01_ovl_00828850` (`em_area01_overlay.c`, AREA01_OVERLAY.md, oracle-verified): AREA01 0x828850 has the same 102 words | **reused**, re-checked |
| 00118790 | 152 | NM | a19_00 f1389 | none | translated (instructions) |
| 001885F0 | 28 | BM | a13b_00 f364 | folded into `em_player_ladder_climb.c` (its 001885D0 / 001885F0 lookup inline, no entry) | translated (the function on its own) |
| AREA13 0x8236E0 | 32 | C | a13b_00 f727 | none (docs only) | translated |
| 001833F0 | 72 | BM | a13b_00 f729 | stub (`em_player_stage_live.c` stub_001833F0, unbound); worker slot in `em_area01_render_frame.c` | translated |
| 001E6F60 | 236 | BM | a13b_02 f419 | hook only (`em_level9_port_fx.c`, 001E5AC0's w_001E6F60) | translated |
| 001305B0 | 1,272 | NM | a13b_05 f1615 (a13b_exit) | hook only (`em_level8_port_creature.c`, 0012E840's behaviour 6, w_001305B0) | translated (instructions) |

Status abbreviations as in SECOND_LEVEL_ROUTE.md section 6 (BM byte-matched
C, NM NEARMISS, C byte-identical overlay C).

Ground truth. Every function was translated from the original instructions
(read locally; nothing reproduced), with the decomp's C as a guide where it
is byte-identical; the order of loads and stores between calls, the float
operand order and every branch follow the instructions. Where the decomp's
text and the instructions disagree the instructions win:

- **00118790** (NEARMISS): on the path where op[4] is nonzero and differs
  from +0x36, the byte base[v] is read before +0x36 = k + 1 is stored (the
  text stores +0x36 first).
- **001305B0** (NEARMISS): state 1 reads self +0xC4 before ent +0x30 for
  001B12B0 (the text names +0x30 first); 0021BE40 is passed the player
  block only (its code reads a0 alone; the text passes two more values).

## 1. Interface (the LEVEL9_PORT / LEVEL8_PORT design)

The design is `em_level9_port.h`'s (docs/LEVEL9_PORT.md section 1) with:

- **Entries.** 17, named `em_level10_port_<address>`, with the original's
  argument registers in order; entries with a result write the original's
  v0 to `*result` (0x829370, 00118790, 001885F0). 001E6F60 takes its seven
  argument registers (a0..a3, t0..t2) as `n0..n6`; AREA13 0x8236E0 takes
  none.
- **Stack frames.** Entries whose original (or a direct callee's) frame
  holds locals take `sp`, the original stack pointer at entry, and address
  the frame at the original's offsets: 0x825240 (the 64-byte area record
  copied from 0x82BD00 to sp - 0x40 at every entry), 0x8250F0 (it passes sp
  - 0x20 to 0x825240), 0x827790 (the area 0x82E050 copied to sp - 0x40),
  0x829370 (dir at sp - 0x20, tint at sp - 0x10).
- **Hooks.** 62 callee hooks named by original address (boot functions and
  the AREA19 overlay's own 0x825420 and 0x825930 at their runtime
  addresses). The header's struct (between its GENERATED HOOKS markers) and
  the wrappers in the internal header (GENERATED WRAPPERS) were written from
  the test's HOOKS list by a lane script that is not committed; the test's
  `header_checks` compares them with HOOKS on every run (names and order,
  argument and result types, one wrapper per hook that calls it and latches
  its address), so an edit of one side alone fails the test. Plus
  `w_callback(ctx, function, a0)` for the actor's +0x4C method. Float hook
  arguments and results cross as floats whose bits are the original's;
  inside the module floats are carried as bit patterns.
- **Calls between translations** are direct: 0x8250F0 -> 0x825240; 0x8257C0
  -> 0x825AB0.
- **Floats.** EE arithmetic on bit patterns through `em_ee_float.h` (add,
  sub, mul and the compare keys); arithmetic shifts are `l10_sra`.
  Quadword loads and stores (the area copies, the tint, 001E6F60's zero
  quadword) use the EE's rule: the low four address bits are ignored.
- **Fail-stop.** As LEVEL9: fault 5 (unmapped or misaligned address, or
  `bytes` NULL), 1 (reached NULL hook or NULL `w_callback`), 2 (a hook
  returned < 0). No path here leaves a value undefined (LEVEL9's fault 7 is
  not used). After a fault no hook runs, `bytes` is not called, writes are
  dropped, the first fault is kept, no result is written and the entry
  returns -1; a fault latched on entry, a NULL hook table, a NULL fault
  pointer and (entries with a result) a NULL result pointer return -1 at
  once.

## 2. What each function does (behaviour, from the code)

self = the pool node; talk = self + 0x1F0; player = 0x8102B0 (position
0x810350..58). The header comment of each function in the sources gives
the field-level behaviour; in short:

**AREA19 (overlay id 16).**

| Entry | Behaviour |
|---|---|
| 0x823580 ([27], the door) | +4 0: 001BBDA0, +0 = 1. 1: the six steps of the table 0x82F800 by +5, then 001BC300 (also for +5 above 5): step 0 tests bit (+0x34 & 31) of the lock byte D_00810841[D_00810700] and asks 001BBE40(self, talk, 0) (bit set; +5 = 3) or (.., 1) (clear; +5 += 1); 1: 001BC0E0 then script 0x82AD50; 2: 001BC0E0 then 001C4760(0xA, 1) unless D_00810CCD, +0xB = +5 = 0; 3 / 4 / 5: 001BC0E0 / 001BC240 / 001BC290 steps. 2 / 3: 001AFC10 |
| 0x8250F0 ([6]) | +4 0: 001BA1C0(self, 0x1D) set frees (state 3); else 001B10B0(self, +0xD, 0x67), 001C63E0(self, 2 or 0 by D_00810775), 001CA6F0(self, 2). 1 by D_008107F5: bit 0 clear -> 0x825240, bit 2 clear -> 0x825420 (a hook), else the idle update (001C64F0(self, 1), 001B17A0, 001C68C0, the +0x4C method) |
| 0x825240 | the area 0x82BD00 copied to the frame; +5 0: the player inside it (001B1EA0 == 1) and not below y 210 starts script 0x82B6E0 (+5 = 1, +0x28 = 0, D_008106C0 = 0); outside: the idle update. +5 1: at count 0x424 001EFE00(0x8000002B, D_008106C0) when set; the count +1; the script's end (001BA1F0): 001AEE10(4, 0), +0x2E = 0xFFFF, D_008107F5 \|= 1, \|= 2, 001C67E0(self, 0, 0, 0), +5 = 0, 001E8B40(0), D_008106C0 = 001B6660(0x82A590), 001FAE70(0) |
| 0x8255D0 ([43]) | +4 0: with D_008107F5 bit 0 +0xD = 0x25 and (001B0FD0 zero) state 2; else (001B0FD0 zero) state 1 and the child 001C5570(self, (0, 1, 0, 0.25), 0x1B, 0) in +0x2EC, moved +0.5 in x and -0.5 in z. 1: once bit 0 is set the model 001C6120(D_0028A59C, 0x25), state 2, the child's +4 = 3 and +0x2EC = 0; 1 / 2: the +0x4C method when 001B17A0 is set |
| 0x8257C0 ([7]) | +4 0: 001B0F60(self, 0xB) zero -> state 1, +8 = 3, +0x30 = 0x82C6D0, +0 = 2 / 1 by 001BA1C0(self, 0x1E). 1: 0x825AB0 while D_008107F6 is 0, else 0x825930 (a hook); 001F5940(7, position, 0) while D_008107F6 < 3; 001C68C0, 001B17A0, the method |
| 0x825AB0 | +5 0: script 0x82C410 when D_00810702 is 0xA; 1: at the script's end +0x28 = 0, 001831F0(0), script 0x82C4D0, +7 = D_008102B5, D_008102B5 = 0; 001831F0(2) every frame of 1; 2: +5 = 3 at the end, the count +0x28 and from 500 001831F0(2) (D_008102B5 = +7 at exactly 500); 3: script 0x82C690 once D_008102B5 is 0, else 001831F0(2); 4: D_008107F6 = 1 at the end |
| 0x825C70 ([9]) | +4 0: (001B0FD0 zero) +0x1F8 = 0.057692308, state 2 and 0019C6F0(0x21, 0) when D_00810776 is 0xFF, else state 1 and 0019C6F0(0x21, 1). 1: +5 0 waits for D_00810776 == 1; +5 1 counts +0x28 to 300, then for 260 frames (001EFD90(2, (1012, 135, 917.6), (0, pi, 0, 1)) at 300) adds +0x1F8 to the +0x80 float of the part at D_00275B40 +4 and twice it to the one at +8; after that, at D_00810776 0xFF, zeroes both, state 2, 0019C6F0(0x21, 0). The method in states 1 and 2 |
| 0x825EE0 ([47]) | the child 001C5570(self, (1, 0, 0, 0.25), 0x23 / 0x24, 0) in +0x1F0; in state 1, once D_00810776 is 0xFF or D_008107F6 > 5, the model 001C6120(D_0028A59C, 0x1F), state 2 and the child replaced by a 0x24 one; the method when 001B17A0 is set |
| 0x826100 ([18]) | +4 0: state 3 (D_00810777 0xFF) or 4. 4: the player in x (962, 968), y > 293, z (923.6, 929.6) with D_008104A0 == 0x2A starts script 0x82C6E0 (state 1). 1: the player's z steps down by 1 a frame after a 31-frame count until 796.589, y by 0.1597114 between z 810.023 and 916.321; below z 808 once: 001FC580(self, 0x19D), 001B1E20(7, 40), three 001EFD90 effects at the node, D_00810777 = 0xFF; state 3 at the script's end |
| 0x827790 ([10]) | the area 0x82E050 copied to the frame. By D_0081081D: 0 -> script 0x82D990 at spawn entry 0xD, 001831F0(2) each frame and script 0x82DA10 from count 366, at its end 001C4760(0xC, 1) and D_0081081D = 0x80; 0x80 -> script 0x82DD10 when the player is in the area, re-armed 601 frames after leaving it |
| 0x829370 (the probe of the group owner 0x827DD0) | AREA01 0x8282F0's instructions with AREA19's tint record 0x82F670: the two probes 0019AA80 / 0019A570 along the matrix's (3, -2, 0) from (60, 0, 0); hit 1 (far, beyond 100 units, or an object of kind 0x10..0x13), 2 or 0; the glint (state 4: 001CD520 with a random red 0x40..0x5F, 001E2BA0) or the tinted line (+0x200 >= 13); the result 0 / 1 / 2 (2 when +0x204 is the hit object) |
| 0x8298D0 (reused) | AREA01 0x828850 (AREA01_OVERLAY.md): the partner of 0x827DD0 |

**AREA13 0x8236E0** (AREA13's load-time function): D_00275C28 = 0x20,
D_00275C2C = 0, D_00275C24 = 0, D_00275C1C = 0x82E280.

**Boot.**

| Entry | Behaviour |
|---|---|
| 00118790 (p; 001152D8's controller 0x60) | +0x3C = +0x38 = 1; op = D_00281AD4 + p +8: op[4] nonzero and equal to +0x36 -> +0x38 = +0x36 = +0x3C = 0; nonzero and different -> +0x14 = (op[3] << 8) + op[2], +0x36 += 1, +0x3A = base[+0x14]; zero -> +0x14 = op[3] << 8 then \| op[2], +0x3A = base[+0x14]. p +8 += 5, returned |
| 001305B0 (self, ent; 0012E840's behaviour 6, AREA04's creature) | by +6: 0 sets ent's timers and flags, clip 0x1E, then as 1; 1: 001FBD50(self, 0x7E1, 0, 300) once, when ent +0x54 is set and self +0x3C <= 136; with ent +0x58 bit 0x1000 clip 0x1F and on, else +0xC4 = 001B12B0(001B1240(self +0xB0, D_00810360, D_00810368), +0xC4, 0.0174533); 2: on bit 0x1000 clip 0x20; 3: 0021BE40(player) zero and 001A7B80(self) set -> the commit (D_008102BF = 2, D_00810320.. from 001B2B10, D_008104D4 = 20 / 25 / 28 / 30 by +0xD and D_0081070A, D_008102B0 \|= 2, 001B55E0(self, 1), clip 0x21), else the steer (00131F20, 001028B8, 001B3250 -> clip 0xA, else 001B1560 zero -> clip 0x21); 4: on bit 0x1000 back to +5 = +6 = 0. From +6 >= 2 with ent +0x69 bit 0 the halfword ent +0x50 counts down; at 0 it is reloaded with ((rand >> 9) & 7) + 5 and 001EFD90(0x8000001D, (self position, y = ent +0x40), self +0xC0) runs. Last 00132490(self, ent) |
| 001833F0 (self; 0015B610 +5 = 2) | +0x23F = 2, +0x24C = 0, 001662D0(self); +4 == 1 clears the byte 0x70003B8D |
| 001885F0 (self) | the signed halfword at 0x2754D4 + 2 * (+0x235 & 1) |
| 001E6F60 (n0..n6; called by 001E5AC0) | a 0x70-byte GS packet record at the cursor of pool slot n0 (D_00275670 + 4 n0, +0x10, advanced by 0x70): the header (+3 = 0x10, +4 = 0, +0 = 6), a zero quadword at +0x10, 0x50000005 at +0x1C and ten doublewords (seven constants, n6, and (n5 << 32) \| sext(n1 \| n2 << 16), (n5 << 32) \| sext(n3 \| n4 << 16)) |

## 3. Verification

`python3 tools/test_level10_port_reference.py` (port root, macOS arm64 or
Linux; no make target: the Makefile belongs to another chain while this side
track runs). It compiles the two sources into
`build/level10/port/level10_port.dylib` with `-std=c11 -Wall -Wextra
-Werror -Wpedantic -ffp-contract=off` (the reuse check builds into
`build/level10/port/reuse`; all of `build/` is ignored, and
`build/level10` can be removed after a run). At most 4 worker processes
(EM_TEST_JOBS overrides). `EM_LEVEL10_PORT_ONLY=<label prefix>` runs a
subset (no coverage, fail-stop, contract or reuse checks);
`EM_LEVEL10_PORT_ALL=1` runs every designed case without the full-mode
comparisons; `EM_LEVEL10_PORT_MISSING=1` lists unexecuted words;
`EM_LEVEL10_PORT_SOURCE=<dir>` tests other copies of the sources (the
mutation sweep); `EM_LEVEL10_PORT_NOCONTRACT=1` / `EM_LEVEL10_PORT_NOREUSE=1`
skip the hook contract / the reuse check (debugging only);
`EM_LEVEL10_PORT_KEEP=1` prints the coverage pass behind DEFAULT_KEEP.

### 3.1 Oracle and harness

The LEVEL9 harness (docs/LEVEL9_PORT.md section 3.1, itself LEVEL8's /
AREA06's / AREA22's / AREA04's), copied and owned here, with:

- **Images.** The 11 recorded tenth-level RAM images: a13_05 (the ninth
  level's last beat: the AREA19 arrival at entry 9), the ends of a19_00..02
  (AREA19, id 16), a13b_00..04 and a13b_s0 (AREA13, id 10) and a13b_05
  (AREA04, id 5, after the lift). Before any case the test asserts that
  each image's overlay text equals the user's `extract/OVERLAY/<AREA>.BIN`,
  that the boot text equals the pinned ELF, that the AREA19 images' table
  0x82F800 (0x823580's steps) equals its file and that the designed-record
  area 0x1C00000 is zero; the table loads are the only accesses left out of
  the comparison (the switch encodes them).
- **D_00275B40.** The cases of the AREA19 owners and of 001305B0 set it to
  the node's bone array (node +0x110), as the frame loop does before a
  behaviour runs (0x825C70 and 0x829370 read it).
- **Stack.** The window is [STACK_TOP - 0x800, STACK_TOP).
- **Callees run as original code** (RUN): 001026A0, 00102738, 001028B8,
  001028D0, 00102948, 001031E0, 001B1240, 001B12B0 (each first rehearsed
  with the argument registers its hook does not pass poisoned). Every other
  callee is stubbed with scripted results (a Scribble writes what the
  callee would, e.g. 0019A570's hit point, polygon, object and kind at
  0x700031B0..D8, 001B2B10's vector at 0x700038A0, or 001C5570's child
  stored into the node).
- **Compared, per case**: as LEVEL9 (the calls and their arguments; memory
  at every call entry; the memory accesses between calls one for one, in
  order, by address, size and changed-or-not; all memory after the last
  store; the return value; the store-log self-check; stops at unmapped or
  misaligned original accesses; every case again from a poisoned start
  image; coverage of every reachable original non-branch word of all 17
  entries (branch words are not counted; see section 3.2 for what that
  does and does not prove); the
  table's ctx at every call).
- **Header**: `header_checks` (every run, also with `EM_LEVEL10_PORT_ONLY`):
  the C hook struct and the wrappers against HOOKS (section 1, Hooks).
- **Fail-stop**: `fault_checks` (0x8250F0 on [6] in its idle path: NULL
  hook, failing hook, an unmapped address after three calls; 001885F0 for
  an unmapped address before any call; a latched fault; every entry with a
  latched fault, a NULL hook table, a NULL fault pointer and, for the
  entries with a result, a NULL result pointer) and `hook_contract_site`
  (on passing cases that together reach all 62 hooks, the callback and all
  17 entries: calls failing, accesses refused, each hook NULL / INT32_MIN /
  1 / INT32_MAX, `bytes` NULL; EM_TEST_FULL=1 every call and every access of
  each case, the default run an even sample of at most 8 calls and 16
  accesses per case).
- **Dead words.** None: every reachable non-branch word of the 17 entries
  is executed.

### 3.2 Cases

- **Capture**: every entry on the captured state of every image it applies
  to (the AREA19 owners, 0x825240 / 0x825AB0 on [6] / [7] and 0x829370 on
  the two group owners in a13_05 and a19_00..02; 0x8236E0 on the six
  AREA13 images; 001305B0 on the AREA04 creature of a13b_05; 001833F0,
  001885F0, 001E6F60 (with 001E5AC0's arguments) and 00118790 on every
  live sequencer track in every image): 130 cases.
- **Designed**: every state and sub-state of every function with each
  callee result on both sides of its test, on a19_00's nodes with the state
  bytes patched: the door's lock bits (bit index above 31 and negative), the
  story bytes D_008107F5 / F6 / D_00810775 / 76 / 77 / D_0081081D /
  D_00810702 / D_008102B5 / D_008104A0 / D_00810CCD; every count bound (the
  0x424 effect, 500, 300 / 560, 31, 366, 601, signed wrap at 0x7FFF); the
  float compares at their bounds, as follows (not "the next float on each
  side" for every bound): 0x825240's y 210 exactly, the next float above
  and below, and 209.9 / 300 / -210; 0x826100 state 4's x 962 / 968, y 293
  and z 923.6 / 929.6 exactly and the next float inside each (up(962),
  down(968), up(293), up(923.6), down(929.6)), plus values outside;
  0x826100 state 1: its 796.589 compare comes before the +0x28 count and
  is run exactly (z = 796.589 with +0x28 = 31, in the default set) and as
  the next float above; the 916.321 / 810.023 and 808 compares come after
  the count (with +0x28 = 31 z is first decremented by 1, so those cases
  compare z - 1): 808 exactly and the next float below with +0x28 = 0;
  916.321 and 810.023 exactly with +0x28 = 0 (the review-round survivor
  cases, below) and the next float inside each (down(916.321),
  up(810.023)) with +0x28 = 0; 001305B0's 136 as 100 / 136 / 137
  (no next-float case); children present or absent and a +0x2EC rewritten
  by the spawn; the probe's two outcomes, distances 100 exactly and the
  next float above, object kinds 0x0F / 0x11 / 0x14 (and 0x10, a survivor
  case) and +0x200 / +0x204 at their tests, random draws for the glint colour;
  00118790 on designed event bytes (op[4] zero, equal and different, the
  halfword +0x36 above 0xFF); 001305B0's five states, the commit's +0xD /
  D_0081070A branches, the steer's two probes and the countdown's random
  draws; 001E6F60 on a designed pool with an aligned record and one whose
  quadword address has bit 3 set, and sign-extended argument words: 507
  designed cases, plus the 8 survivor cases (515).
- **The default run's share.** Every capture case, the designed cases a
  coverage pass picked (`DEFAULT_KEEP`: with the capture cases they reach
  every word the whole set reaches, greedy by cost) and the survivor cases.
  EM_TEST_FULL=1 runs everything. The default run therefore proves word
  coverage (every reachable non-branch word executed and compared), not
  both outcomes of every branch: boundary cases picked for a branch's other
  outcome (D_008107F6 == 3 or 6, t == 0x424 / 0x425, 001B1EA0 returning 2,
  x == 968, y == 293, counts 0x16E / 0x259, the door's bit -1, 001E6F60's
  negative n5 / n6, 001885F0 at +0x235 = 2 / 3, 001833F0 at +4 = 2 / 0x81,
  and others) run only with EM_TEST_FULL=1. The review round measured the
  gap: of 48 targeted mutants, the default run killed 25 and the full run
  41 (section 3.6).
- **Survivor cases** (`survivor_cases`, default run), added because the
  mutation sweep (section 3.5) showed the default share missed them:
  0x827790 with +0x2A at 0x7FFF in D_0081081D 0x80's step 3 and +0x28 at
  0x7FFF in 0's step 1 (both counts are signed halfwords: 0x7FFF + 1 is
  below the bound); 001885F0 at the odd slot holding 0x8001 (a signed
  halfword); 0x825240 with the player exactly at y 210 (not below it: the
  script starts). Added in the review round (section 3.6): 0x826100 state
  1 with +0x28 = 0 and z exactly 916.321f (0x4465148B, not below the bound:
  y is left alone) and exactly 810.023f (0x444A8179, not above it: y is
  left alone), each with y 250; 0x826100 state 1 with +0x28 = 0, z 900 and
  y 0, where one ulp of the y step constant 0x3E238B63 (0.1597114f) shows
  in the result; 0x829370 on the owner 0x7A96E0 with hit-object kind
  exactly 0x10 (the lower bound of 0x10..0x13; the first probe misses, the
  second hits at distance 0: the original takes the 001031E0 / 001E2BA0
  line).

### 3.3 Measured

2026-10-01, M1, host shared with other lanes, after the review round.
Default run: 241 cases (130 capture + 111 designed: the coverage picks and
the 8 survivor cases), 417 runs (176 poisoned), 752 call entries compared,
128 helper register rehearsals, coverage 1,601 / 1,601 reachable non-branch
words, the header check, the fail-stop checks, the hook contract (1,755
native runs on 100 cases) and the reuse smoke sample (19 cases) passing;
3.8 s user + 1.0 s system CPU, 3.4 s wall with 4 workers. `EM_TEST_FULL=1`:
645 cases (130 capture + 515 designed), 1,144 runs (499 poisoned), 3,032
call entries compared over all memory, 624 rehearsals, the hook contract on
158 cases (4,382 native runs, every call and every access), the reuse check
in full (33 cases, 72 / 72 reachable non-branch words); 63.2 s user + 1.5 s
system CPU, 21.9 s wall. All pass.

### 3.4 Reuse check (the existing translation over the tenth-level captures)

**0x8298D0** (`em_area01_ovl_00828850`, the harness of
`tools/test_area01_overlay_reference.py`, `run_case`, imported and not
modified): the test first asserts that AREA19's 0x8298D0 words equal AREA01's
0x828850 words in the user's two overlay files, then runs the AREA01
translation as the entry at 0x8298D0 on the AREA19 images: the two
partners' captured states in a13_05 and a19_00..02 and every state with
each callee result on both sides (001B0FD0 / 001B11E0 in state 0,
001EFE00 with +0x36 0 / 1 / 0x8000 in state 1, +0x28 around 10 in state
2): 33 cases with EM_TEST_FULL=1 (all 72 reachable non-branch words of
the function's 102; the other 30 are branch words or unreachable), a smoke sample
of 19 by default.

### 3.5 Mutation sweep (bounded, one round)

Single-edit mutants of the two sources (comparison operators, the float
compare macros, add / sub, signed / unsigned halfword loads, a halfword
store made a byte store, `&&` / `||`, hexadecimal constants +1 and ^4,
small integers +1, dropped stores), a seeded sample (seed 0xAA, 150
mutants) run through the default test (contract and reuse skipped) with
`EM_LEVEL10_PORT_SOURCE`. The sweep script is lane scratch (not
committed); it is rerunnable from this description.

- 150 mutants: 128 killed (one of them, the area copy's loop bound 4 made
  5, writes past its local array: its run hung and was stopped by hand,
  counted killed), 14 rejected by the compiler (a dropped store left a
  variable unused under `-Werror`, a mutated constant became an unsigned
  literal compared with a signed value, or a case label became a
  duplicate), 8 survived.
- **Real gaps** (3): 0x827790's +0x2A signedness, 001885F0's halfword
  signedness, 0x825240's y < 210 test made <=. The survivor cases
  (section 3.2) kill all three (each mutant rerun on them), and a fourth
  case pins 0x827790's +0x28 signedness, the same class.
- **Equivalent survivors** (5, argued from the code): four initial values
  of a hook-result variable (`ignored` in the door, `aim` in 001305B0,
  `child` in 0x8255D0, `r` in `call_1B0FD0`) that the hook always writes
  before the value is read (after a failed hook nothing observable
  follows), and 001305B0's `ent +0x54 != 0` test with a signed instead of
  an unsigned halfword load (a nonzero test).

### 3.6 Review round (independent targeted mutants)

The lane review ran one bounded round of 49 targeted single-edit mutants of
the two sources (one rejected for a pattern that did not apply), on the
default run first and the default-run survivors again with
`EM_TEST_FULL=1`: default 25 killed / 23 survived, full mode killing 16 of
those 23. Of the 7 full-mode survivors:

- **4 non-equivalent, now killed by pinned cases** (section 3.2, survivor
  cases; each mutant was rebuilt and rerun on its case and fails it, the
  real translation passes): 0x826100 state 1's `z < 916.321` made `<=`
  (first difference at 0x810354 at the entry of 001BA1F0) and its
  `!(z <= 810.023)` made `!(z < 810.023)` (same place); the y step
  constant 0x3E238B63 made 0x3E238B64 (0x810354 0x63 against 0x64 there);
  0x829370's `kind >= 0x10` made `> 0x10` (memory differs at the entry of
  the probe's call 7, 001031E0). The cause was the same in each: the
  designed set never put the compared value exactly on that bound.
- **3 equivalent** (argued from the code): 0x825240 state 1 stores
  D_008107F5 | 1, re-reads the byte with no call in between and stores
  that | 2, so | 3 in the second store writes the same byte (a refused
  access stops the function before either difference could show);
  00118790's halfword +0x36 read signed instead of unsigned: it is only
  compared for equality with a byte 1..0xFF (true for both loads exactly
  when the halfword is that byte) and otherwise stored back + 1 as a
  halfword (same low 16 bits); 0x829370's random draw >> 16 done as a
  logical instead of an arithmetic shift differs only for a negative draw,
  and 00122BB8 (byte-matched in the decomp, `src/func_00122BB8.c`) returns
  its state masked with 0x7FFFFFFF, never negative. (The harness's designed
  negative draw -0x10000 does not separate the two shifts in the stored
  colour; no real 00122BB8 result is negative.)

## Binding

Nothing calls these entries. To run them live a host must:

- supply `bytes` over the scene's original-byte storage (the actor pool,
  the player block, the AREA19 overlay's data (the scripts, the area
  records 0x82BD00 / 0x82E050, the tint record 0x82F670, the table
  0x82F800), the story bytes, the bone arrays, the probe's hit records in
  the scratchpad, the sequencer tracks 0x27E0C0.. and the sequence data
  behind D_00281AD4, the packet pools behind D_00275670, the stack frame
  below `sp`), and point D_00275B40 at the running actor's bone array as the
  frame loop does;
- run the AREA19 owners as node behaviours (+0x10): 0x823580, 0x8250F0,
  0x8255D0, 0x8257C0, 0x825C70, 0x825EE0, 0x826100, 0x827790, and
  0x8298D0 through the reused `em_area01_ovl_00828850`; 0x829370 from the
  group owner 0x827DD0 (AREA19's twin of AREA01 0x826D40, not translated
  here: it ran in the ninth level's census, NINTH_LEVEL_ROUTE.md section 5,
  and is not a census row of either level); AREA13 0x8236E0 from the boot area
  dispatcher 001E7780 (its static reference, decomp docs/AREA13_OVERLAY.md); 00118790 from the sequencer's
  event loop 001152D8 (controller 0x60); 001305B0 in place of
  `em_level8_port_creature.c`'s w_001305B0 (0012E840's behaviour 6);
  001833F0 in place of `em_player_stage_live.c`'s stub_001833F0 (0015B610
  +5 = 2); 001E6F60 in place of `em_level9_port_fx.c`'s w_001E6F60
  (001E5AC0); 001885F0 where 0017FC80 calls it (the ladder-climb
  translation folds the lookup in already);
- bind each hook to a verified port translation or a fail-stop stand-in;
  the AREA19 hooks 0x825420 ([6]'s second sub-state) and 0x825930 ([7]'s
  sequence after D_008107F6 is set) have no translation yet (the census did
  not see them run on this route; TENTH_LEVEL_ROUTE.md section 5 lists
  them); the stubbed callees (every hook not in the RUN list above) are not
  verified here, and the helpers run as original code need port
  translations paired by address.

## Known gaps

- **Live evidence.** The captures are end-of-beat states. Most paths are
  designed cases on the captured RAM with state bytes patched: [6]'s and
  [7]'s scripts (D_008107F5 / F6 never changed on this route,
  TENTH_LEVEL_ROUTE.md section 4), [9]'s 260-frame sequence, [18]'s slide,
  [10]'s scripts at entries 0xD and the area, the door's open steps, the
  probe's hits, 001305B0's states (the capture has state 1), 00118790 on
  designed bytes.
- **Census lower bound.** TENTH_LEVEL_ROUTE.md section 5.1: AREA19 code that
  ran only in a13b_00's first 425 frames or in a13_05's AREA19 frames, and
  seven AREA13 functions in a13b_00's AREA13 frames, were not measured;
  they are not rows here.
- **Stubbed callees' inputs.** Only the RUN helpers are proven not to read a
  register their hook does not pass; the other hooks' argument lists were
  read from the callees' code and the call sites, and their results are
  scripted beyond their real ranges.
- **What the harness cannot see** (as LEVEL9): whether an access that
  changed nothing is a load or a store of the same value; hardware
  behaviour past a stop; the frame bookkeeping.
- **Float proofs rest on the EE model** (`em_ee_float.h`).
- **Mutation sweep** is a bounded sample, not exhaustive.
- **Reuse check** uses the AREA01 harness's own model.
