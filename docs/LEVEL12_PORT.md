# The twelfth level: the new functions of the a13d / a19b route (lane L12T)

Lane L12T, 2026-10-01 (level side track). It covers the 39 functions the
twelfth-level route census found new (decomp `build/s87/census/a13d_delta.json`
and `a19b_delta.json`, `new_functions`: 9 + 30 rows, 18,004 bytes;
TWELFTH_LEVEL_ROUTE.md section 5): AREA13's south roof (the 0x141D20 actors'
behaviour 5 and side probe, a render-list helper chain), door [20] and the
hatch [63] into AREA19 entry 10 ([7]'s first sequence and its second
sequence's start, the AREA19 init), the AREA19 creatures of 001386E0's family
(behaviours 0 / 1 and their steering), the player's attribute-0x39 east
ledge, and [6]'s first stage (the group 0x82B5A0, the effect 0x824BE0, the
second stage 0x825420). None had a verified port translation; all 39 are
translated here.

Every translation is compared with the original instructions by
`tools/test_level12_port_reference.py`. What that comparison covers is
exactly what section 3 states: the comparisons the test makes, on the cases
it runs. Nothing is bound: no port code calls these entries yet.

| File | What |
|---|---|
| `src/game/em_level12_port.h` | public API: 39 entries, the hook table (85 hooks and the indirect-call callback), fault codes |
| `src/game/em_level12_port_internal.h` | memory view, fault latch, EE float helpers, one typed wrapper per hook (written from the test's HOOKS table; section 1, Hooks), the internal entry points |
| `src/game/em_level12_port_area19.c` | AREA19 0x824BE0, 0x824EF0, 0x824F70, 0x825030, 0x8250C0, 0x825420, 0x825930 |
| `src/game/em_level12_port_creature.c` | 001386E0's family: 00138900, 00138C20, 0013BA20, 0013BBB0, 0013BE60, 0013BF20, 0013C1F0, 0013C4C0, 0013C8C0, 0013CD50, 0013D220 |
| `src/game/em_level12_port_boot.c` | 0011BCF8, 0011E420, 001437E0, 00146740, 0016EF50, 00179560, 0017F9E0, 001821E0, 001831F0, 001A96F0, 001B1270, 001B2F70, 001B3390, 001B39F0, 001C6910, 001C9570, 001CAFA0, 001CB060, 001D3F60, 001D40D0, 001E8B40 |
| `tools/test_level12_port_reference.py` | original-instruction oracle and comparison |

Addresses are runtime addresses. The AREA19 overlay is linked 0x40 below
where it runs (0x825420 is `func_overlay_AREA19_008253E0`). Bracketed numbers
([6], [7] ...) are the placement records of TWELFTH_LEVEL_ROUTE.md section
1.1; they, the decomp's role comments and names such as `anim_clip_init`
(001C67E0) are labels, not evidence. The behaviour below is read from the
code.

## 0. Scope: the census rows, and what already existed

A grep of the port (`src/`, `tools/`, `docs/`) for each address before this
lane found these; "hook slot" means a worker slot, a call site or a stub, not
a translation. Status as in SECOND_LEVEL_ROUTE.md section 6 (BM byte-matched
C, NM NEARMISS, AW word assembly, AI inline assembly, C byte-identical overlay
C).

| Row | Bytes | Decomp | First (census) | Port before this lane | Here |
|---|---:|---|---|---|---|
| 001437E0 | 772 | BM | a13d_00 f276 | hook slot (`em_level11_port.h` w_001437E0: 00142070's behaviour 5) | translated |
| 00146740, 001B39F0 | 620, 564 | BM, BM | a13d_00 f515 | hook slots (`em_level11_port.h` w_00146740, w_001B39F0) | translated |
| 001B2F70, 001B3390 | 368, 172 | NM, BM | a13d_00 f519, f520 | hook slots (`em_level8_port.h`, `em_level11_port.h`) | translated |
| 001CAFA0, 001CB060, 001D3F60, 001D40D0 | 188, 8, 356, 12 | BM, BM, NM, BM | a13d_00 f455 | 001CB060 named in `em_status_models.c`'s method table (0x26E310 kind 8), no translation | translated |
| overlay 0x8250C0 | 48 | C | a19b_00 f483 | none (0x8250C0 in `em_area02_overlay.c` is AREA02's) | translated |
| 00138900 | 788 | BM | a19b_00 f485 | hook slot (`em_level9_port.h` w_00138900: 001386E0's behaviour 0) | translated |
| 0013BA20, 0013BE60, 0013BF20, 001A96F0, 001C6910, 001C9570 | 396, 192, 716, 192, 68, 152 | BM, AI, NM, AI, BM, AI | a19b_00 f485 | hook slots: w_0013BE60, w_0013BF20, w_001C6910 (`em_level9_port.h`, 001386E0's tail); w_001A96F0 (`em_coll_list_passes.h`, bound to `em_coll_list_passes_unported`) | translated |
| 001831F0 | 76 | BM | a19b_00 f486 | hook slot (`em_level10_port.h` w_001831F0: [7]'s first sequence) | translated |
| overlay 0x825930 | 372 | C | a19b_00 f1736 | hook slot (`em_level10_port.h` w_00825930: [7] 0x8257C0) | translated |
| 00138C20, 0013BBB0, 0013C8C0, 0013CD50, 0013D220 | 1,560, 688, 1,160, 1,224, 164 | BM, AW, NM, NM, BM | a19b_00 f1767, f1768 | hook slot w_00138C20 (`em_level9_port.h`, behaviour 1); 0013CD50 mentioned in POSE_HOST_WORKERS.md | translated |
| 0013C4C0 | 1,016 | BM | a19b_01 f120 | none | translated |
| 0011BCF8, 0011E420, 0013C1F0 | 1,072, 252, 720 | AW, NM, BM | a19b_01 f165 | 0011BCF8 / 0011E420 named in SDK_SOFT_FLOAT.md as untranslated | translated |
| 0017F9E0, 0016EF50, 001B1270 | 420, 1,492, 60 | NM, NM, BM | a19b_01 f166, f167, f170 | 0017F9E0: the `surface39` slot of `em_player_floor.h` (handler 0), untranslated (`em_player.c`) | translated |
| 001821E0, 00179560 | 104, 280 | BM, BM | a19b_01 f206, f207 | none | translated |
| overlay 0x824EF0, 0x824F70, 0x825030 | 116, 192, 140 | C | a19b_02 f503 | none (0x824F70 in `em_area01_revisit.c` is AREA01's) | translated |
| overlay 0x824BE0 | 784 | NM | a19b_02 f1204 | none | translated |
| 001E8B40, overlay 0x825420 | 72, 428 | BM, C | a19b_02 f1711, f1712 | hook slots (`em_level10_port.h` w_001E8B40, w_00825420: [6] 0x8250F0) | translated |

**Ground truth.** Every function was written from the original instructions
(read privately from the user's ELF / AREA19.BIN; nothing of them is
reproduced here), with the decomp's C as the reading guide, and run against
the original-instruction oracle (section 3) until no difference remained:
the order of every load and store between calls, every call and argument,
the float operation order and every branch outcome on the cases run. Where
a choice had no observable effect on any case (for example which of two
equal register values a store uses) it is not a claim. Where the decomp's
text and the original disagree the original wins; the differences found:

- **0016EF50** (NEARMISS): (a) in states 0xA / 0xB with sub-state 0, a
  word +0x24C other than 0, 2 and 3 still clears +0x38 and +0x21C (the text
  skips both); (b) the table word 0x248670[+0x23F] is read once for +0x26C
  and +0x204 (the text reads it twice); (c) state 0x15 calls 001764E0 a
  second time after the drift, whether or not the first ran (the text calls
  it once); (d) state 0x16's done path (bit 0x1000) calls 001764E0 and then
  runs the common tail (+0xB4 += -0.2, 00175900, 001796C0); the text
  returns directly; (e) its non-done path also calls 001764E0 before the
  tail (the text does not); (f) each drift reads the step (+0x2E0 / +0x2E8)
  before the position (the text's own note).
- **0013C8C0** (NEARMISS): the first 001B2B10 call gets the actor as its
  first argument (the register still holds it); the text passes the
  scratch vector three times.
- **0011E420** (NEARMISS): on the error path (mode not -1, not a NaN, |x| >
  1) the result is 00127758's (the exception record's value converted),
  not the kernel's value; the text returns the kernel's value on every
  path.
- **001B2F70** (NEARMISS): 0019BC40 receives the caller's position (the
  first argument register is untouched before the call); the text's call
  passes nothing.
- **0013BA20** (BM, a behaviour note, not a text error): the pose test
  masks the sign-extended halfword +0x2C with 0xFFFF7FFF, so a halfword
  with bit 15 set is never 0 or 4 (0x8000 does not clear to 0).
- **00138900** (BM): state 3 sets ent +0x5C = -pi/2 while self +0x3C <=
  25 (the decomp's role comment says "past frame 25"; its C is right).
- **0011BCF8** (word assembly) and **0013BBB0** (word assembly) were
  translated from the instructions (the only source); section 2 gives the
  behaviour. 0013BE60, 001A96F0 and 001C9570 (inline assembly) likewise.

## 1. Interface (the LEVEL11_PORT / LEVEL10_PORT design)

The design is `em_level11_port.h`'s (docs/LEVEL11_PORT.md section 1) with:

- **Entries.** 39, named `em_level12_port_<address>`, with the original's
  argument registers in order; entries with a result write the original's
  v0 (or f0) to `*result`: 0013C1F0, 0013C4C0, 0013CD50, 0013D220,
  00146740, 001821E0, 001B2F70, 001B3390, 001B39F0 (`int32_t`); 0011BCF8,
  0011E420, 001B1270 (`float` with the original's bits). Float arguments
  are `float` with the original's bits (0011BCF8's / 0011E420's x,
  001B1270's px / py, 001B3390's f12).
- **Stack frames.** Entries whose original (or a nested translated
  callee's) frame holds locals take `sp`, the original stack pointer at
  entry, and address the frames at the original's offsets: 0x824BE0 (the
  packet at sp - 0x60), 0x825420 (the area copy at sp - 0x40), 0011E420
  (the exception record at sp - 0x60), 0013BBB0 (the integral-part word at
  sp - 4), 001B3390 (the queried value at sp - 4), 0013C8C0 (0011E420 at
  sp - 0x30), 00146740 (0011E420 at sp - 0x30), 00138C20 (0013C8C0 and
  0013BBB0 at sp - 0x30). The offsets are the ones the oracle's stack
  window shows (section 3.1).
- **Hooks.** 85 callee hooks named by original address (boot functions;
  AREA19 has no overlay callee here). The header's struct (between its
  GENERATED HOOKS markers) and the wrappers in the internal header
  (GENERATED WRAPPERS) were written from the test's HOOKS list by a lane
  script that is not committed; the test's `header_checks` compares them
  with HOOKS on every run (names and order, argument and result types, one
  wrapper per hook that calls it and latches its address), so an edit of
  one side alone fails the test. Plus `w_callback(ctx, function, a0)` for
  the indirect call (0x825420's +0x4C method). Float hook arguments and
  results cross as floats whose bits are the original's; inside the module
  floats are carried as bit patterns.
- **Calls between translations** are direct: 00138C20 -> 0013C8C0,
  001B2F70, 001B1270, 0013BBB0, 0013BA20; 00138900 -> 0013BA20; 0013C8C0
  -> 0013CD50, 0013D220, 0011E420, 001B39F0, 0013C4C0, 0013C1F0; 0013C4C0
  -> 001B39F0, 0013D220; 00146740 -> 0011E420, 001B39F0; 0011E420 ->
  0011BCF8; 0016EF50 -> 001821E0, 00179560; 001C6910 -> 001C9570;
  001CB060 -> 001CAFA0 -> 001D40D0 -> 001D3F60; 0x825930 -> 001E8B40.
- **Floats.** EE arithmetic on bit patterns through `em_ee_float.h` (add,
  sub, mul, div, neg, the multiply-add pair ADDA / MADD of 001B39F0,
  cvt.s.w and the compare keys); 001C9570's lane scaling through
  `em_vu_vec_bits` (VMULbc, dest xyz, broadcast x / y / z: a form in the
  float model's table). Quadword loads and stores (0x825420's area copy)
  use the EE's rule (the low four address bits are ignored, two doubleword
  accesses); a VU0 register load or store is four word accesses, as the
  oracle models lqc2 / sqc2.
- **Access order.** No C expression in the three sources reads memory
  twice where C leaves the order unspecified: every read the original
  orders is its own statement, the only read of its expression, or nested
  as an address.
- **Fail-stop.** As LEVEL11: fault 5 (unmapped or misaligned address, or
  `bytes` NULL), 1 (reached NULL hook or NULL `w_callback`), 2 (a hook
  returned < 0), and 7 (`EM_LEVEL12_PORT_FAULT_REGISTER`) for a VU0 form the
  float model does not define (never expected). After a fault no hook runs,
  `bytes` is not called, writes are dropped, the first fault is kept, no
  result is written and the entry returns -1; a fault latched on entry, a
  NULL hook table, a NULL fault pointer and (entries with a result) a NULL
  result pointer return -1 at once.

## 2. What each function does (behaviour, from the code)

self = the pool node; ent = self + 0x1F0 (the behaviour block); the player
block 0x8102B0 (its position +0xB0 = 0x810360). The header comment of each
function in the sources gives the field-level behaviour; in short:

**AREA19 (overlay id 16).**

| Entry | Behaviour |
|---|---|
| 0x8250C0 | the area init: D_00275C2C = 3, C28 = 0x20, C20 = 0x82F880, C24 = 0, C1C = 0x84D9C0 |
| 0x825420 ([6], the 0x8250F0 sub-state with D_008107F5 bit 2 clear) | the area 0x82BD40 copied to the frame; position 0x82BD80, yaw -0.83775806; +5 0: in the area (001B1EA0 == 1) with 190 <= y <= 200 -> script 0x82BA00, +0x2E = 0, +5 = 1; +5 1: at the script's end +5 = 2, +0x2E = 0xFFFF, D_008107F5 = 0xFF, 001C67E0(self, 1, 0, 0), 001FAE70(0); with 0x70003B92 zero the clip step, 001B17A0, 001C68C0 and the +0x4C method |
| 0x825930 ([7], the 0x8257C0 sub-state with D_008107F6 != 0) | +5 0 on bit 2 of +0xB: script 0x82BD90, 001B6F00(self, (0, 0, 5.5), pi), +5 = 1; +5 1: the clip step; D_008107F6 4 -> 001E8B40(1), the sound 0x8DF (1000), D_008107F6 += 1; 0xFF -> D_00810854 \|= 4 (door [27]'s lock bit 2); +5 = 2 at the script's end |
| 0x824EF0 / 0x824F70 / 0x825030 (group 0x82B5A0) | in state 1: 001EFEB0(0x8000004B) and (4) at the matrix; the matrix negated row by row into 0x700036A0 and 001EFEB0(5); a 001F4E20 sprite (colour 0x30 / 0x80, size 5) |
| 0x824BE0 | the effect: +0xD 0 / 1 picks the phase start (0.3) and two packet models (0x82B480 / 0x82B510 or 0x82B360 / 0x82B3F0); each frame two 001CFA60 / 001CFBE0 pairs with a random fraction from the seed (s * 37 + 11), phase += 0.03, past 1.5 state 3; states 2 / 3: 001AFC10 |

**Boot: 001386E0's family (the AREA19 creatures; ent = self + 0x1F0).**

| Entry | Behaviour |
|---|---|
| 00138900 (behaviour 0) | the sub-state machine 0..4: the ent +0x20 countdown, y - 2.5 on bit 2 of ent +0x81, the count while D_008106C7 is set and the player is within 150, -pi/2 while self +0x3C <= 25, then behaviour 2 on bit 0x1000 of ent +0x70; then the pitch ease (pi/120) and 0013BA20 |
| 00138C20 (behaviour 1) | 0013C8C0; the speed cap 0.4; state 0 wanders (every 64th frame the target ent +0 beyond 70 starts state 1; random yaw / pitch targets on the 0x20 / 0x22 countdowns; a downward pitch made positive over a floor (001B2F70) within 20); state 1 approaches (within 30 back to 0; yaw 001B1240, pitch -001B1270); the stun count (0x97), the switch to behaviour 2 (bit 0 of +0xA, or 0x78 frames of the player within 150 with 0021BE40 and 0019AFE0 zero) with the sound 0x816, the sound 0x826 on the +0x86 countdown; the yaw / pitch eases; 0013BBB0, 0013BA20 |
| 0013BA20 | the bank: ent +0x60 follows the yaw while the pose is 0 or 4; the yaw change, clamped to 0.0349, drives self +0xC8 by 001B12B0 |
| 0013BBB0 | the pose k (1, 0 or 4) by ent +0x5C against +0x50 and pi/4; when it differs from self +0x2C & 0x7FFF, 001C67E0(self, k, ..) with the timing by the current pose and the clip time (001C6160, 0011E0A8, 001281C0) |
| 0013BE60 | the drift: speed += accel; y by the pitch, x / z by the yaw (cos / sin) |
| 0013BF20 | the ground probes into ent +0x81: ahead (0019AD00), sideways by the frame parity, and up / down by the pitch (0019AB20) |
| 0013C8C0 / 0013CD50 / 0013C4C0 / 0013C1F0 / 0013D220 | the steering: the obstacle class 25 ahead (0013CD50: 0019AFE0, the column table 0019BC40, the heights below / above, the class 1 / 2 / 3), the side probes at +-pi/8, the turn toward or away from the hit object (0011E420 of the dot product, 001B1380 / 001B39F0), the side step (0013C4C0) and the turn (0013C1F0); 0013D220 tests the floor object kind 0x5B 10 below |

**Boot: the others.**

| Entry | Behaviour |
|---|---|
| 0011BCF8 / 0011E420 | the float arc cosine: the kernel's three ranges (pi/2 - (x - (lo - x r)), pi - 2 (s + r s - lo), 2 (df + r s + c)) with the EE's operations; the wrapper's domain error (mode 0x26C5D0, 0011E080, \|x\| > 1): the exception record, 0011DB90, errno 0x21, the converted result |
| 001437E0 | the 0x141D20 actor's behaviour 5: the swell (scale +0x8C by +-0.08 between 1 and 4, the model swap 0x28A490[0x7D / 0x7E] with the cue 0x83D / 0x83C once), then back to behaviour 0 with a cooldown 300..1199 |
| 00146740 / 001B39F0 | the side probe toward the hit object (within 23.5: turn by 001B1380 unless the angle is beyond 2.356, else 3) or half way (001B39F0: two 200-unit probes, the nearer side or a random one within 1.5; 3 when that side is closer than 10) |
| 001B2F70 | the column scan: the nearest flagged gentle (<= pi/3) height into *out, 1 when the position is between two |
| 001B3390 | the pair probe: 0019B2C0(a1, a2, 6), 0019A310; beyond f12 the shift by 0x700031C0 |
| 001B1270 | the pitch to a point: 001B1470(0011E620(-(px - y), py - z)) |
| 0016EF50 / 00179560 / 0017F9E0 / 001821E0 | the player-block functions first run in the census frames of the attribute-0x39 shuffle (a19b_01 f166..f207): 0017F9E0 (the record at 0x700031D0: the yaw from its +0x3C / +0x34, the side +0x2F1 by the sign of the turn, the corner points 0019F680 0 / 1 or 2 / 3 averaged into x / z, +5 = 0x1B, +0x1F0 = 0x2F, 00174A50); 0016EF50 (the states 0 / 1 / 0xA / 0xB / 0x14 / 0x15 / 0x16 of section 0's list and the source header); 00179560 (the move by +0x38 along the yaw, the matrix 001C94B0, the probe 4.5 to the side, 0019AD00); 001821E0 (+0x224 or +0x22C nonzero or bit 1 of +0xF -> +4 = 2, +5 = 9, +6 = 0, 1) |
| 001831F0 | the player block's +0x23F / +0x24C by the mode 0 / 1 / other |
| 001A96F0 | the class-0xD x class-2 pair box test (001A97B0's outer type 3): inside the box +0x54 = 1, 0x70003B86 = 0 |
| 001C6910 / 001C9570 | the node matrix: 001029C0, the rotation 00102C58, the scale per row (VU0), the translation 00102918; then 001C9940 |
| 001CB060 / 001CAFA0 / 001D40D0 / 001D3F60 | a draw method (kind 8 of 0x26E310): the DMA ref tags on channel 3 (the context's 0x816F40 row, 0x2514B0, the model's packet) between the render-state calls |
| 001E8B40 | sets byte +5 of the record behind D_00275C20 +0x58 (1) or +0xA0B8 (0) |

## 3. Verification

`python3 tools/test_level12_port_reference.py` (port root, macOS arm64 or
Linux; no make target: the Makefile belongs to another chain while this side
track runs). It compiles the three sources into
`build/level12/port/level12_port.dylib` with `-std=c11 -Wall -Wextra
-Werror -Wpedantic -ffp-contract=off` (all of `build/` is ignored;
`build/level12` can be removed after a run). At most 4 worker processes
(EM_TEST_JOBS overrides). `EM_LEVEL12_PORT_ONLY=<label prefix>` runs a
subset (no coverage, fail-stop or contract checks); `EM_LEVEL12_PORT_ALL=1`
runs every designed case without the full-mode comparisons;
`EM_LEVEL12_PORT_MISSING=1` lists unexecuted words;
`EM_LEVEL12_PORT_SOURCE=<dir>` tests other copies of the sources (the
mutation sweep); `EM_LEVEL12_PORT_NOCONTRACT=1` skips the hook contract
(debugging only); `EM_LEVEL12_PORT_KEEP=1` prints the coverage pass behind
DEFAULT_KEEP.

### 3.1 Oracle and harness

The LEVEL11 harness (docs/LEVEL11_PORT.md section 3.1), copied and owned
here, with:

- **Images.** The 11 recorded twelfth-level RAM images (the ends of
  a13d_00..07, AREA13 resident, and a19b_00..02, AREA19 resident). Before
  any case the test asserts that each image's overlay text equals the
  user's `extract/OVERLAY/AREA13.BIN` or `AREA19.BIN`, that the boot text
  equals the pinned ELF and that the designed-record area 0x1C00000 is
  zero. No entry here dispatches through a jump table.
- **Callees.** A static check (`callee_checks`, every run) walks every
  entry's reachable words: every call target is a hook or another entry.
  Callees run as original code (RUN, each first rehearsed with the argument
  registers its hook does not pass poisoned): 001026A0, 00102738,
  00102760, 001028B8, 001028D0, 00102918, 00102948, 00102958, 001029C0,
  00102B08, 00102BB0, 00102C58, 00103230, 0011CB90, 0011DE90, 0011DF78,
  0011E080, 0011E0A8, 0011E2A8, 0011E620, 0011E748, 001281C0, 00128250,
  00128350, 001B1240, 001B12B0, 001B1380, 001B13F0, 001B1470, 001B15D0,
  001C6160 (some cases stub one of them to steer a bound). Every other
  callee is stubbed with scripted results (a Scribble writes what the
  callee would: 0019BC40's column table, 0019A310's value in the frame,
  0011DB90's errno word, 0019B6C0's hit object).
- **D_00275B40** is pointed at the node's bone array (node +0x110) in the
  cases of the AREA19 creatures, [6] / [7] and the 0x141D20 actors.
- **Compared, per case**: as LEVEL11 (the calls and their arguments; memory
  at every call entry; the memory accesses between calls one for one, in
  order, by address, size and changed-or-not; all memory after the last
  store; the return value; the store-log self-check; stops at unmapped or
  misaligned original accesses; every case again from a poisoned start
  image; coverage of every reachable original non-branch word of all 39
  entries; the table's ctx at every call).
- **Header**: `header_checks` (every run): the C hook struct and the
  wrappers against HOOKS.
- **Fail-stop**: `fault_checks` (0x825420 on [6] in a19b_02 with +5 2:
  NULL hook, failing hook, an unmapped address after four calls; 0013D220
  for an unmapped address before any call; a latched fault; every entry
  with a latched fault, a NULL hook table, a NULL fault pointer and, for
  the entries with a result, a NULL result pointer) and
  `hook_contract_site` (on passing cases that together reach every hook,
  the callback and all 39 entries: calls failing, accesses refused, each
  hook NULL / INT32_MIN / 1 / INT32_MAX, `bytes` NULL; EM_TEST_FULL=1 every
  call and every access of each case, the default run an even sample of at
  most 8 calls and 16 accesses per case).
- **Dead words** (`DEAD_WORDS`, each word's instruction class checked, its
  text not reproduced): 0013C8C0's slot of the branch after the two coin
  compares (the coin is 0 or 1) and 0013CD50's switch default (the mask
  holds bits 0 and 1 only).

### 3.2 Cases

- **Capture** (222): on each a19b image every family entry on each of the
  three 0x1383C0 creatures (0013CD50 with the scratch segment 0x700038A0),
  0x825420 on [6], 0x825930 on [7], 0x8250C0, 001E8B40(0 / 1 / 2),
  001B2F70 and the player functions 0016EF50 / 00179560 / 001821E0 /
  001C6910 on the player block; on each a13d image 001437E0, 00146740,
  001B39F0 and 001A96F0 on both 0x141D20 actors, and 001B2F70.
- **Designed** (961, the bound, extra and survivor cases included): every state and
  sub-state of every function with each callee result on both sides of its
  test, on captured nodes with the state bytes patched (a19b_02's creature
  0x7A9FB0, [6], [7]; a13d_00's actor 0x7AAB70; a19b_01's player block) or
  on designed records in the free area 0x1C00000. Among them: the pose word
  0, 1, 4, 0x8000, 0x8004; the creature's countdowns at 0 / 1, the stun and
  alert counts one below their bounds, the frame tick on and off, the
  player near and far; the steering's 0x80 / 0x81 bit patterns, the probe
  results in every order, column tables for the obstacle classes 0..3 and
  the four masks, the hit object ahead, behind and to the side; 0013BBB0's
  seven lean pairs by four poses and the clip times either side of 0x2C /
  0x16 and of the 23 / 45 caps; the ledge states 0 .. 0x17 with each flag
  and +0x24C 0 / 2 / 3 / 5 / -1; 0x825420's y 189 / 190 / 200 / 201 and the
  script results; 0x825930 by D_008107F6 0..5 and 0xFF; 0011BCF8 over the
  float classes (+-1, the words next to 1, 0.5 and 2**-57, NaN, +-Inf,
  denormal, +-0) and 0011E420 over the error modes -1 / 0 / 1 / 2 with the
  matherr result and errno word either way.
- **Bound cases** (`bound_cases`, 164, in the designed set): each float
  compare against a constant or between two values with the compared value
  exactly at the bound and one float either side (the distances 5 / 23.5 /
  150, the angles 0.0349 / pi/4 / pi/3 / 2.356, the heights against y and lo
  - 5 / hi + 5, the scales 1 / 4, the phase 1.5, the y 190 / 200, the zero
  tests with +-0 and the smallest denormals).
- **The default run's share.** Every capture case, the designed cases a
  coverage pass picked (`DEFAULT_KEEP`: with the capture cases they reach
  every word the whole set reaches, greedy by cost), one case of every entry
  (the contract's `bytes` target) and the survivor cases. EM_TEST_FULL=1
  (or EM_LEVEL12_PORT_ALL=1) runs everything. The default run therefore
  proves word coverage (every reachable non-branch word executed and
  compared), not both outcomes of every branch; the bound cases run only
  with EM_TEST_FULL=1.
- **Survivor cases** (`survivor_cases`, 12, always run): section 3.4.

### 3.3 Measured

2026-10-01, M1, host shared with other lanes. Default run: 366 cases (222
capture + 144 designed: the DEFAULT_KEEP picks, one case per entry and the
12 survivor cases), 642 runs (276 poisoned), 4,202 call entries compared,
2,428 helper register rehearsals, coverage 3,742 / 3,742 reachable
non-branch words (DEAD_WORDS left out), the header check, the callee
check, the fail-stop checks and the hook contract (5,358 native runs on 167
cases) passing; 8.0 s user + 0.9 s system CPU, 3.4 s wall with 4 workers.
`EM_TEST_FULL=1`: 1,183 cases (222 capture + 961 designed), 2,087 runs (904
poisoned), 15,589 call entries compared over all memory, 10,292
rehearsals, the hook contract on 520 cases (26,440 native runs, every call
and every access); 539 s user + 5 s system CPU, 2 min 34 s wall. All pass.

A sanity mutant (one rate constant of 0013BA20 one unit higher) fails 118
cases, the first at the call where the rate is passed.

### 3.4 Mutation sweep (bounded, one round)

Single-edit mutants of the three sources (the float compare macros
swapped, add / sub, `==` / `!=`, `&&` / `||`, the integer comparison
operators, unsigned / signed byte and halfword loads, a halfword store made
a byte store and a word store a halfword store, hexadecimal constants +1 and
^4, small integers +1, dropped stores), a seeded sample (seed 0xB12, 150 of
the mutants) run through every designed case (EM_LEVEL12_PORT_ALL=1,
contract skipped) with `EM_LEVEL12_PORT_SOURCE`. The sweep script is lane
scratch (not committed); it is rerunnable from this description. Before the
survivor cases existed.

- 150 mutants: 137 killed, 4 rejected by the compiler under `-Werror` (two
  dropped stores that left a variable unused, one dropped store of a
  computed byte, a duplicate case label), 9 survived.
- **Real gaps** (5): bit 0x1000 of ent +0x70 in 00138900 tested only with 0
  and 0x1000 (a mask 0x1001 survived); 0011BCF8's third q constant one unit
  higher (no case input showed it); 0013CD50's `<= 25` made `< 25` in mask
  1 and in mask 0's last fallback; 0013C8C0's angle bound 2.3561945 one
  unit higher. The survivor cases (`survivor_cases`, run by default) kill
  all five (each mutant was rebuilt and rerun on them: every one fails, the
  real translation passes): ent +0x70 = 0xEFFF and 0x1001; x = the word
  0xBEDEA086 (an |x| < 0.5 input found with the module and a mutant build
  over random words); column tables with |hi - y| and |y - lo| exactly 25
  and one float either side; the dot -0.7071068 with the square root
  0011CB90 scripted to the three results that put 0011E420's value exactly
  on 0x4016CBE4 and one float either side (found with the module).
- **Contract-only** (1): an entry's `return -1` after a fault made `-2`
  (00146740's wrapper); the sweep skips the hook contract, which kills it
  (rerun: the default run fails at its first failing-call site).
- **Equivalent survivors** (3, argued from the code): a flag variable set to
  2 instead of 1 where only its truth is used; an array declared one row
  larger (the extra row is never read); 0x70003B92 read signed instead of
  unsigned where it is only compared with 0.

## Binding

Nothing calls these entries. To run them live a host must:

- supply `bytes` over the scene's original-byte storage (the actor pool,
  the player block, the AREA19 overlay's data (the areas 0x82BD40 /
  0x82BD80, the scripts 0x82BA00 / 0x82BD90, the packet models 0x82B360 ..
  0x82B510, the table 0x82F880 / 0x84D9C0 words), the story bytes, the bone
  arrays, the scratchpad (0x70003170 / 0x700030F0 / 0x700031E0 the column
  table, 0x700031B0.. probe records, 0x700036A0.. matrices, 0x700038A0..
  vectors, 0x70003600, 0x70003A20, 0x70003B68 / 0x70003B8A / 0x70003B86 /
  0x70003B92), the boot tables (0x282250 slopes, 0x248670 durations,
  0x28A490 models, 0x26C5D0 / 0x26C630 the math error mode and name), the
  renderer context behind D_00275670 / D_00275678 and its DMA buffers, the
  table behind D_00275C20 and the stack frame below `sp`), and point
  D_00275B40 at the running actor's bone array as the frame loop does;
- **bind the ninth level's hook slots** (`em_level9_port.h`, LEVEL9_PORT.md
  "Binding"; this lane does not edit em_level9_* files): 001386E0's
  dispatch by +5 w_00138900 / w_00138C20 to `em_level12_port_00138900` /
  `00138C20` (the latter with the original sp at that call) and its tail
  w_0013BF20 / w_0013BE60 / w_001C6910 to `em_level12_port_0013BF20` /
  `0013BE60` / `001C6910`, the same self and ent;
- **bind the tenth level's hook slots** (`em_level10_port.h`, LEVEL10_PORT.md
  "Binding"): w_001831F0 ([7]'s first sequence 0x825AB0) and w_001E8B40
  ([6]'s first stage 0x825240) to `em_level12_port_001831F0` / `001E8B40`;
  w_00825420 (0x8250F0's sub-state) to `em_level12_port_00825420` with the
  original sp; w_00825930 (0x8257C0's sub-state) to
  `em_level12_port_00825930`;
- **bind the eleventh level's hook slots** (`em_level11_port.h`,
  LEVEL11_PORT.md "Binding"): w_001437E0 (00142070's behaviour 5) to
  `em_level12_port_001437E0`; w_00146740 (001424C0) to
  `em_level12_port_00146740` with the original sp; w_001B39F0, w_001B2F70
  and w_001B3390 to `em_level12_port_001B39F0` / `001B2F70` / `001B3390`
  (the last with the original sp); the eighth level's w_001B2F70 /
  w_001B3390 (`em_level8_port.h`) the same way;
- bind `em_coll_list_passes.h`'s w_001A96F0 (001A97B0, outer type 3; today
  `em_coll_list_passes_unported` in `em_collision_world.c`) to
  `em_level12_port_001A96F0`, and `em_player_floor.h`'s `surface39`
  handler 0 to `em_level12_port_0017F9E0` (handler 1, 0017FB90, ran in no
  census and has no translation);
- run 0016EF50 as the player's state behaviour for its ledge states (its
  caller, the player state dispatch, is not translated here), 001CB060 as
  the +0x4C method of the actors whose kind selects it (0x26E310 entry 8),
  0x824BE0 / 0x824EF0 / 0x824F70 / 0x825030 as the node behaviours of the
  effect and of the group 0x82B5A0 (spawned by script 0x82B6E0), 0x8250C0
  from the area load;
- bind each hook to a verified port translation or a fail-stop stand-in.
  Port entries already exist by address for several hooks (for example
  `em_level11_port_0019A6F0`, `em_coll_segment_0019A570`-family probes,
  `em_pose_host_001C67E0` / `001C68C0`, `em_sdk_soft_float_001274B0` /
  `00128350`); their verification status is their own lanes', not
  re-checked here. The stubbed callees (every hook not in the RUN list) are
  not verified here, and the helpers run as original code need port
  translations paired by address. 0017FB90 (the other ledge handler),
  0011C128 / 0011E520 (the arc sine pair) and 001386E0's other behaviours
  (00139240, 001399F0, 00139E00, 0013A3B0) ran in no census of this level.

## Known gaps

- **Live evidence.** The captures are end-of-beat states: most paths are
  designed cases on the captured RAM with state bytes patched. The AREA19
  creatures sit in behaviour 1 in all three a19b images; behaviour 0, the
  effect 0x824BE0 and the group 0x82B5A0's members (freed by the a19b_02
  end) run only on designed states; the player's ledge states run on the
  a19b_01 player block with the state bytes patched.
- **Census lower bound.** The census sets are those of the route shape, not
  of the exact frames (TWELFTH_LEVEL_ROUTE.md section 5: a13d_00, a13d_03
  and a19b_02's replays walked different paths; a19b_00's AREA19 frames did
  not measure 0x824BE0 / 0x826840); functions run only off this route are
  not rows here.
- **Stubbed callees' inputs.** Only the RUN helpers are proven not to read
  a register their hook does not pass; the other hooks' argument lists were
  taken from a static read of each callee's entry (the registers it reads
  before writing them) and earlier lanes' HOOKS tables, and their results
  are scripted beyond their real ranges.
- **Dead words** are argued from the code (section 3.1), not executed.
- **What the harness cannot see** (as LEVEL11): whether an access that
  changed nothing is a load or a store of the same value; hardware
  behaviour past a stop; the frame bookkeeping.
- **Float proofs rest on the EE / VU0 model** (`em_ee_float.h`).
- **Mutation sweep** is a bounded sample, not exhaustive.
