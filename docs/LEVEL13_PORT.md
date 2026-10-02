# The thirteenth level: the new functions of the a19c route (lane L13T)

Lane L13T, 2026-10-02 (level side track). It covers the 36 functions the
thirteenth-level route census found new (decomp
`build/s87/census/a19c_delta.json`, `new_functions`: 36 rows, 23,628 bytes;
THIRTEENTH_LEVEL_ROUTE.md section 5.2): the bar swing and its release
(player states), the AREA19 creatures' behaviours 2 / 3 / 4, the tendril
pieces, the bar-pad test, the probe chain, a draw method, [7]'s effect, and
AREA19 sub 1's placements ([34]..[40], [46], [53], the 0x827B60 objects) with
their script callbacks. Five of the 36 already had a verified translation and
are reused (section 0); the other 31 are translated here.

Every translation is compared with the original instructions by
`tools/test_level13_port_reference.py`. What that comparison covers is
exactly what section 3 states: the comparisons the test makes, on the cases
it runs. Nothing is bound: no port code calls these entries yet.

| File | What |
|---|---|
| `src/game/em_level13_port.h` | public API: 31 entries, the hook table (88 hooks and the indirect-call callback), fault codes |
| `src/game/em_level13_port_internal.h` | memory view, fault latch, EE float helpers (with the MULA / MADD pair), one typed wrapper per hook (written from the test's HOOKS table; section 1, Hooks), the internal entry points |
| `src/game/em_level13_port_area19.c` | AREA19 0x823780, 0x823D10, 0x824A90, 0x826570, 0x826840, 0x826C10, 0x827430, 0x827550, 0x8279E0, 0x827B10, 0x827B20, 0x827B60, 0x829840, 0x829A70 |
| `src/game/em_level13_port_creature.c` | 001386E0's behaviours 00139240, 00139E00, 0013A3B0; the tendrils 001545B0, 00154F00 |
| `src/game/em_level13_port_boot.c` | 00194240, 00197390, 001B2B80, 001B3250, 001B34F0, 001B37D0, 001CB140, 001CB1F0, 001D42E0, 001D4430, 001F6AC0, 001F6B90 |
| `tools/test_level13_port_reference.py` | original-instruction oracle and comparison |

Addresses are runtime addresses. The AREA19 overlay is linked 0x40 below
where it runs (0x823780 is `func_overlay_AREA19_00823740`); its data
references are relocated to the runtime addresses the code names here
(0x82AF50, 0x82B000 ... read from the resident image). Bracketed numbers
([34], [40] ...) are the placement records of THIRTEENTH_LEVEL_ROUTE.md;
they, the decomp's role comments and names such as `anim_clip_init`
(001C67E0) are labels, not evidence. The behaviour below is read from the
code.

## 0. Scope: the census rows, and what already existed

A grep of the port (`src/`, `tools/`, `docs/`) for each address before this
lane found these; "hook slot" means a worker slot, a call site or a stub, not
a translation. Status as in SECOND_LEVEL_ROUTE.md section 6 (BM byte-matched
C, NM NEARMISS, AW word assembly, C byte-identical overlay C).

| Row | Bytes | Decomp | First (census) | Port before this lane | Here |
|---|---:|---|---|---|---|
| 00194240 | 620 | BM | a19c_00 f295 | hook slot (`em_area06_port.h` w_00194240: 00169250's state 0) | translated |
| 001818D0, 0016A4B0, 0016A8B0, 00181950, 00181A70 | 128, 1,020, 916, 280, 268 | BM, NM, NM, BM, BM | a19c_00 f647..f774 | **verified translations** in `em_player_closure_10_12_19.c` (static functions there except 0016A8B0; PLAYER_CLOSURE_10_12_19.md: original-instruction oracle, 719/719 branch outcomes, 200,000 cases in its full run) | **reused**, not re-translated (section 3.5) |
| overlay 0x829840 | 132 | C | a19c_01 f8 | none (AREA19 has no port module at this address) | translated |
| 00139240 | 1,960 | NM | a19c_02 f58 | hook slot (`em_level9_port.h` w_00139240: 001386E0's behaviour 2) | translated |
| overlay 0x824A90 | 328 | C | a19c_02 f405 | none | translated |
| 00139E00 | 1,444 | NM | a19c_03 f207 | hook slot (`em_level9_port.h` w_00139E00: behaviour 3) | translated |
| 001545B0, 00154F00 | 264, 676 | BM, AW | a19c_03 f913 | hook slots (`em_level9_port.h` w_001545B0, w_00154F00: 001549C0's tendrils) | translated |
| 0013A3B0 | 3,992 | BM | a19c_04 f1 | hook slot (`em_level9_port.h` w_0013A3B0: behaviour 4) | translated |
| 001CB140, 001CB1F0, 001D42E0, 001D4430 | 164, 8, 332, 16 | BM, BM, NM, BM | a19c_04 f9 | 001CB1F0 named in `em_status_models.c`'s method table (0x26E310 kind 10), no translation | translated |
| 001B34F0 | 140 | BM | a19c_04 f113 | none | translated |
| 001B2B80, 001B3250, 001B37D0 | 100, 156, 536 | BM, AW, NM | a19c_05 f1144 | hook slots: w_001B3250 (`em_level8_port.h`, `em_level10_port.h`), w_001B37D0 (`em_level8_port.h`, `em_level11_port.h`) | translated |
| 001F6AC0 / 00197390 | 16 / 244 | BM / NM | a19c_06 f1721 / f1722 | 001F6AC0: the inline word compare of `em_effect_manager.c` (its `w_word`, not a translation of the function); 00197390: hook slot (`em_camera_leftovers.h` w_00197390, NULL) | translated |
| overlay 0x823780, 0x823D10, 0x826570, 0x826840, 0x826C10, 0x827430, 0x827550, 0x8279E0, 0x827B60, 0x829A70 | 1,312, 2,428, 716, 748, 2,072, 260, 564, 292, 612, 800 | NM, NM, C, C, NM, NM, C, C, C, NM | a19c_06 f1721 | none (port hits at these addresses are other areas' functions) | translated |
| overlay 0x827B10 | 16 | C | a19c_07 f376 | none | translated |
| 001F6B90 / overlay 0x827B20 | 12 / 56 | BM / C | a19c_07 f501 | none (0x827B20 a fixture name in test_area19_assets_reference.py) | translated |

**Ground truth.** Every function was written from the original instructions
(read privately from the user's ELF / the resident AREA19 image; nothing of
them is reproduced here), with the decomp's C as the reading guide, and run
against the original-instruction oracle (section 3) until no difference
remained: the order of every load and store between calls, every call and
argument, the float operation order and every branch outcome on the cases
run. Where a choice had no observable effect on any case it is not a claim.
Where the decomp's text and the original disagree the original wins; the
differences found:

- **00139E00** (NEARMISS): in state 3 the speed clamp is inverted in the
  text: the original sets +0x44 = 0.4 and +0x48 = 0 when +0x44 is **not**
  below 0.4; the text does it when +0x44 is below 0.4.
- **00139240** (NEARMISS): when the wander heading +0x5C is negative and
  the floor probe 001B2F70 misses, the original falls into the
  positive-heading test (+0x5C read again; above 0 with y above 15 + the
  player's y it is negated); the text does nothing on the miss. (With the
  probe leaving +0x5C alone the re-read value is still negative, so only the
  extra read shows.)
- **001B3250** and **00154F00** (word assembly) were translated from the
  instructions (the only source); section 2 gives the behaviour.
- The other NEARMISS rows (0x823780, 0x823D10, 0x826C10, 0x827430,
  0x829A70, 001D42E0, 00197390, 001B37D0) showed no behaviour difference
  from their text on the cases run.

Both differences are recorded as logic-note rows in the decomp's
`docs/NEARMISS.md`.

## 1. Interface (the LEVEL12_PORT / LEVEL11_PORT design)

The design is `em_level12_port.h`'s (docs/LEVEL12_PORT.md section 1) with:

- **Entries.** 31, named `em_level13_port_<address>`, with the original's
  argument registers in order; entries with a result write the original's
  v0 (or f0) to `*result`: 0x827B10, 0x827B20, 001545B0, 00194240,
  001B2B80, 001B3250, 001B34F0, 001F6AC0 (`int32_t`); 001B37D0 (`float`
  with the original's bits). Float arguments are `float` with the
  original's bits (001545B0's x / z, 001B3250's limit, 001B37D0's distance
  and limit).
- **Stack frames.** Entries whose original (or a nested translated callee's)
  frame holds locals take `sp`, the original stack pointer at entry, and
  address the frames at the original's offsets: 0x823780, 0x823D10,
  0x824A90 (the effect packet at sp - 0x60, handed to the packet hooks),
  0x829840 (the colour quadword at sp - 0x10), 00154F00 (the saved position
  at sp - 0x10), 001B3250 (the probe's height word at sp - 0x10, written by
  001B2D00 / 001B2F70), 001B37D0 (the probe vector at sp - 0x10; its nested
  001B3250 at sp - 0x60, so that word at sp - 0x70).
- **Hooks.** 88 callee hooks named by original address (boot functions; no
  AREA19 function is a callee here). The header's struct (between its
  GENERATED HOOKS markers) and the wrappers in the internal header
  (GENERATED WRAPPERS) were written from the test's HOOKS list by a lane
  script that is not committed; the test's `header_checks` compares them
  with HOOKS on every run (names and order, argument and result types, one
  wrapper per hook that calls it and latches its address), so an edit of
  one side alone fails the test. Plus `w_callback(ctx, function, a0)` for
  the indirect call (the actors' +0x4C method). Float hook arguments and
  results cross as floats whose bits are the original's; 001D6F60's second
  argument is the 64-bit register value. The twelfth level's translations
  0013C8C0, 0013BBB0, 0013BA20 and 001B2F70 are hooks here (bound to the
  `em_level12_port_*` entries, section "Binding").
- **Calls between translations** are direct: 001B37D0 -> 001B3250 ->
  001B2B80; 0013A3B0 -> 001B34F0; 001CB1F0 -> 001CB140 -> 001D4430 ->
  001D42E0; 0x827B20 -> 001F6B90.
- **Floats.** EE arithmetic on bit patterns through `em_ee_float.h` (add,
  sub, mul, div, cvt.s.w, the compare keys and the MULA / MADD pair of
  001545B0, 00194240 and 001B34F0). Quadword loads and stores (0x829840's
  colour) use the EE's rule (two doubleword accesses). No VU0 form is used.
- **Access order.** No C expression in the three sources reads memory
  twice where C leaves the order unspecified: every read the original
  orders is its own statement, the only read of its expression, or nested
  as an address.
- **Fail-stop.** As LEVEL12: fault 5 (unmapped or misaligned address, or
  `bytes` NULL), 1 (reached NULL hook or NULL `w_callback`), 2 (a hook
  returned < 0). After a fault no hook runs, `bytes` is not called, writes
  are dropped, the first fault is kept, no result is written and the entry
  returns -1; a fault latched on entry, a NULL hook table, a NULL fault
  pointer and (entries with a result) a NULL result pointer return -1 at
  once.

## 2. What each function does (behaviour, from the code)

self = the pool node; blk / ent = self + 0x1F0; the player block 0x8102B0
(its position +0xB0 = 0x810360). The header comment of each function in the
sources gives the field-level behaviour; in short:

**AREA19 (overlay id 16).**

| Entry | Behaviour |
|---|---|
| 0x823780 ([40], two flame columns) | init: random phases / seeds, the position (870, 370, 910), +0x30 = 0x82AFE0, +0x34 = 0x823770; with D_0081077B and D_00810778 both 0xFF 001B6660(0x82AB00) and state 3; D_008107F8 set -> +5 = 1. Nothing while D_00810702 is 8 / 5 / 4 / 2 (001FC520 of the sound handle). +5 0 waits for D_008107F8 (then sound 0x8E8); +5 1: per column four packets 0x82AF50 (scaled by blk +4) at (870, 448, 938 / 880) with phase and seed, the phase advancing by blk +0 (wrapped above 2), the sound 0x8E9 kept (001FC3C0), 001B17A0 |
| 0x823D10 ([39], the fire) | init: the position (848, 377, 920), +0x30 = the parent's +0x1F0, +0x34 = 0x823CA0, a seed, the size blk +0x24 = 1; state 3 at once with D_008107FB 0xFF or D_008107F8 set. Then (not while D_00810702 is 5 / 4 / 2): state 2 once D_008107F8 is set; three flame packets 0x82B120 (z 940 / 920 / 900); with the size set and D_008107F9 bit 7 clear three smoke columns (0x82B000 sized 4 j and by the size, 0x82B090); 0021B9A0 between; in state 2 after 60 frames the size shrinks by 1/240 a frame and blk +0x28 grows by 0.01, past 5 state 3; the sound 0x413 in state 1; blk +0..+8 = (18, 45 size, 35); 001B1B70 |
| 0x824A90 (an effect of [7]'s script) | init: the matrix from +0xC0 / +0xB0, a random blk +4; each frame the packet 0x82B2D0, blk +0 += 0.02, past 1.5 state 3 |
| 0x826570 ([38], the valve) | init (001B0FD0 zero): state 2 when story bit 0x20 or 0x23 is set (001BA1C0), else 0019C6F0(7, 1), +0x30 = 0x82CC60, state 1. Use (+0xB bit 2): script 0x82CA20, 001B6F00 to (0, 0, 5) facing pi; the sound 0x19A at frame 120; at the script's end 001F6BA0, 0019C6F0(7, 0), D_00810778 = D_008107F8 = 0xFF (the fires end), 001B6660(0x82AB00) with D_0081077B 0xFF, state 2. The light (0x80, 0, 0x80) at (890.4, 384.9, 836.37) |
| 0x826840 ([37], the lift valve) | init: state 2 with D_00810779 0xFF, else +8 = 1, +0x30 = 0x82D270, state 1. Use: script 0x82CFF0 (D_008107F9 low nibble set) or 0x82CC70 with D_008107F9 bit 7; 001B6F00 to (0, 0, 6.2); the lift's (+0x18) +0x2EC = 0x8A; at the end +5 = +0xB = 0, D_008107F9 bit 7 cleared. The light at (819.4, 384.9, 836.35) |
| 0x826C10 ([36], the lift) | init: rest height +0x2E8 = y; state 2 with D_00810779 0xFF, else state 4 (y +15 when D_008107F9's low nibble is set), 0019C6F0(0x15, 1). State 4: the countdown +0x2EC (set by [37]) starts a 180-frame move of +-1/12 a frame (sounds 0x8EB / 0x8EA) that toggles the nibble; the player in 850 < x < 859.6, 850.5 < z < 855 starts script 0x82D290 (state 1). State 1: at the script's end D_00810779 = 0xFF, state 2; once +0x28 is set (the script callback 0x827540) a 70-frame wait (sound 0x8EC, 001B1E20(5, 120)), then the drop (+0.2 a frame accelerating) to the rest height and 001B1E20(8, 20) once. Four corner lights 001F5940(3, ..) while above rest |
| 0x827430 ([35]) | follows the linked object's (+0x1C) height: y = its y + the constant 0x40FCCCC0 (about 7.899994, not 7.9f = 0x40FCCCCD; the translation uses the original's bits) when it changed. At the captured heights near 380 the two sums round alike, so no case pins this constant to its last unit |
| 0x827550 ([53], item 0x24's giver) | init: state 3 with story bit 0x26 set or 0x25 clear; else 001B10B0 / 001C63E0 / +0x58 = D_0028A704 / 001F1110. +5 0: for 445 <= y <= 460 001F1180 each frame and, in the area 0x82F820, 001FB0B0(0) and script 0x82D590; +5 1: 001F1180 for 300 frames, 001C47A0(0x24, 1) at the script's end |
| 0x8279E0 ([34]) | story bit 0x46 set: 0019C6F0(8, 1), 001B6660(0x82A9C0), state 3; else for D_00810702 == 1 and 0x70003B8D != 4 script 0x82E090; at its end 001C4760(0xB, 1) (item 0x0B), D_0081081E = 0xFF |
| 0x827B10 / 0x827B20 | script callbacks: D_0081081E = 1; 001B6660(0x82A9C0), 001F6B90, 0019C6F0(8, 1); both return 1 |
| 0x827B60 (the 11 objects) | state 3 with D_0081079E 0xFF; init copies the pose to blk +0x10 / +0x20; D_0081081E == 1 loads a countdown from +0x9A; at 0: state 2 and a 0019A570 probe 4 above / below; on a hit 001F0460(4, ..) at the pose turned by pi/2, 0.2 up. State 2: +0x10 = 0x156620 |
| 0x829840 (callback of 0x827DD0) | spawns 001AFA90(0xC) at the source's pose with the colour 0x82F680, behaviour 0x1F5040 |
| 0x829A70 ([46]) | init: with D_00810838 set the turns (0019C6F0(5, 0); the +0x74 of *(D_00275B40 + 8 / + 0xC) = -pi/2 / pi/2), state 2; else +8 = 1, +0x2EC = +0xE, +0xE \|= 0xFF00, +0x30 = 0x82F790, a child 001AFA90(0xC) (model 0x18, behaviour 0x1C5760). Use: with +0xB bit 0 D_00810838 = 1, 001C47E0(0x25, 1), +0xE restored, the turns, the child's state 3, 0x70003B8D / B91 / B92 = 0, 001CA770 when 0x70003B8F is 2; without bit 0 script 0x82F690 |

**Boot: 001386E0's behaviours (the AREA19 creatures; ent = self + 0x1F0).**

| Entry | Behaviour |
|---|---|
| 00139240 (behaviour 2) | 0013C8C0 first; +6 0 rolls the side's row of D_002451A0 (end / speed 0.4 / 0.8); +6 1 near the player (within 50, 0021BE40 clear, facing within 0.1745) rolls D_002451C0 (5: within 20 ends, else behaviour 5; others behaviour 4), far (beyond 100) counts to 241, between 50 and 100 with +0xA bit 7 goes to behaviour 3; the speed ease, the re-aim count, the wander heading (mirrored by the floor probe 001B2F70 or the height), the 301-frame life count back to behaviour 1, the yaw and turn eases, 0013BBB0, 0013BA20 |
| 00139E00 (behaviour 3) | the 300-frame charge with the sound 0x817: speed to 0.8, the pitch toward the player's head height (001B1270), the grab when 0021BE40 is clear and 001A7B80 set (damage D_008104D4 25 / 22 / 20 / 18, D_008102B0 \|= 2, the knock direction D_00810320, clip 5, the hit effect 0x80000006); state 2 waits for the clip end (0x1000) and backs off (-0.4); state 3 counts 80; the end returns to behaviour 1 |
| 0013A3B0 (behaviour 4, the grab) | a 10-way switch: settle (clip 6) until the pose word is 7 (clip 8, sound 0x818); the circle within the 1.4835 cone and the lunge box (001B34F0 over (4, 9)) to the grab (0021BF90, D_00810374, the creature 3.7 behind the player, clip 9); the hold 8 above the player, every 60 frames the sound 0x819 and the health drain toward a floor (15 / 12 / 10 by +0xD bit 7 and D_0081070A); the throw (sound 0x81A, effect 0x80000029, D_008104DC 60 / 48 / 40, 0021C040); recover (clip 0xD), cooldown, retreat (state 9, pushed by 0019AD00); D_008106BD cancels the hold; the reset; the eases of +0x50, +0xC8, the yaw |
| 001545B0 | the tendril reach: inside the ellipse of D_00248120[+0xD] (semi-axes 0.92 rec +8 / +0) in the bearing to (x, z) |
| 00154F00 | the 12 tendril pieces: position / velocity halfwords (gravity 8 or 1 by the target's +0x80 above 0.5, the bounce off 100 with a random velocity, the damping of 0x80-range pieces), each drawn by moving the actor to the piece (+0x60 / +0x64 / +0x68 / +0x8C scales, 001C6380, the +0x4C method), the position restored |

**Boot: the others.**

| Entry | Behaviour |
|---|---|
| 00194240 | the AREA19 bar pads: the player within 15 of (1017.9, 1030.9) at y 223 (+-4) -> D_008106F2 = 6; (997.2, 929.8) y 198 -> 1; (896.5, 930.2) y 197 -> 2; (760.7, 883.1) y 268 -> 3 |
| 00197390 | the camera set-up after a sub change: 001916C0(cam, player, 0), the point = row D_00810702 of D_0024A6C8, 0018D7B0(cam, 5), D_008105D0 = it; unless the player's +0x230 is 9 / 8 / 0x2C, cam +6 / +1 / +2 = 0 |
| 001B2B80 / 001B3250 | the probe: 0019AD00(a0, point, 7) set -> 001B2D00 \| 4, else 001B2F70; 001B3250: no bit 0 -> 2; bit 2 with the hit object's +0x1A bit 0x2000 -> 1; else 0 when y - lim is below the probed height, 2 otherwise |
| 001B37D0 | the heading search: pi + yaw, then +-k pi/8 (k 1..7), the first whose probe (0, 3, dist) returns 0 |
| 001B34F0 | the box test (horizontal radius a1 +0, height a1 +4) |
| 001CB1F0 / 001CB140 / 001D4430 / 001D42E0 | a draw method (kind 10 of 0x26E310): 001D8C20(5), 001C7420(obj, 0x3F5, 3), the DMA reference tags on channel 3 (the context's 0x817140 row, 0x2514B0 when 001D2910(0) is 0, the model's packet), an end tag on the context's +0x1C list, 001CAAC0(D_00275B44 + 0xB0, the list start), 001D8C20(0) |
| 001F6AC0 / 001F6B90 | the light record ready test (+0x24 != -1); 001F6640(0x25D2C0) |

## 3. Verification

`python3 tools/test_level13_port_reference.py` (port root, macOS arm64 or
Linux; no make target: the Makefile belongs to another chain while this side
track runs). It compiles the three sources into
`build/level13/port/level13_port.dylib` with `-std=c11 -Wall -Wextra
-Werror -Wpedantic -ffp-contract=off` (all of `build/` is ignored;
`build/level13` can be removed after a run). At most 4 worker processes
(EM_TEST_JOBS overrides). `EM_LEVEL13_PORT_ONLY=<label prefix>` runs a
subset (no coverage, fail-stop or contract checks); `EM_LEVEL13_PORT_ALL=1`
runs every designed case without the full-mode comparisons;
`EM_LEVEL13_PORT_MISSING=1` lists unexecuted words;
`EM_LEVEL13_PORT_SOURCE=<dir>` tests other copies of the sources;
`EM_LEVEL13_PORT_NOCONTRACT=1` skips the hook contract (debugging only);
`EM_LEVEL13_PORT_KEEP=1` prints the coverage pass behind DEFAULT_KEEP.

### 3.1 Oracle and harness

The LEVEL12 harness (docs/LEVEL12_PORT.md section 3.1), copied and owned
here, with:

- **Images.** The 8 recorded thirteenth-level RAM images (the ends of
  a19c_00..07; AREA19 resident in all, sub 0 in a19c_00..05, sub 1 in
  a19c_06 / 07). Before any case the test asserts that each image's overlay
  text equals the user's `extract/OVERLAY/AREA19.BIN`, that the boot text
  equals the pinned ELF and that the designed-record area 0x1C00000 is
  zero.
- **Jump table.** 0013A3B0 dispatches its state byte through the 10-entry
  table 0x26D1E0 (the translation is a switch); the table's loads are left
  out of the access comparison, every other access is compared.
- **Callees.** A static check (`callee_checks`, every run) walks every
  entry's reachable words: every call (jal and tail j) target is a hook or
  another entry. Callees run as original code (RUN, each first rehearsed
  with the argument registers its hook does not pass poisoned): 001026A0,
  00102760, 001028B8, 00102918, 00102948, 00102958, 001029C0, 00102B08,
  00102BB0, 00102C58, 00103230, 0011DE90, 0011DF78, 0011E2A8, 001281C0,
  001B1240, 001B1270, 001B12B0, 001B1470, 001B1560, 001B15D0 (some cases
  stub 001B15D0 / 001B1560 to set a distance or a facing result). Every
  other callee is stubbed with scripted results (a Scribble writes what the
  callee would: 001B2F70's probed height into 0x700038A0 or the frame word,
  001C6380 setting D_00810838 for 0x829A70's second test).
- **The +0x4C method** is stubbed as the callback (the tendrils' method is
  0x1CB1F0, itself an entry here; in 00154F00's cases it is the callback,
  as in the original's indirect call).
- **Frame bookkeeping.** As LEVEL12, stores of callee-saved registers at sp
  offsets are frame bookkeeping, with one listed exception: 001B37D0 keeps
  its distance (in $f22) in the frame vector (`LOCAL_STORES`), which is
  compared.
- **Compared, per case**: as LEVEL12 (the calls and their arguments; memory
  at every call entry; the memory accesses between calls one for one, in
  order, by address, size and changed-or-not; all memory after the last
  store; the return value; the store-log self-check; stops at unmapped or
  misaligned original accesses; every case again from a poisoned start
  image; coverage of every reachable original non-branch word of all 31
  entries; the table's ctx at every call).
- **Header**: `header_checks` (every run).
- **Fail-stop**: `fault_checks` (0x826570 on [38] in a19c_07 with +4 1 / +5
  2, which calls 001B17A0, the +0x4C method and 001F4BF0: NULL hook,
  failing hook, an unmapped +0x4C after the first call; 001F6AC0 for an
  unmapped address before any call; a latched fault; every entry with a
  latched fault, a NULL hook table, a NULL fault pointer and, for the
  entries with a result, a NULL result pointer) and `hook_contract_site`
  (on passing cases that together reach every hook, the callback and all
  31 entries: calls failing, accesses refused, each hook NULL / INT32_MIN /
  1 / INT32_MAX, `bytes` NULL; EM_TEST_FULL=1 every call and every access of
  each case, the default run an even sample of at most 8 calls and 16
  accesses per case plus the first call of each callee; the sites are
  picked so that every (entry, hook) pair seen on a passing case fails at
  least once inside that entry).
- **Dead words** (`DEAD_WORDS`, each word's instruction class checked, its
  text not reproduced): 0x823780's column-switch exit for k & 1 outside 0 /
  1 (one delay slot) and its store of 1.0 to blk +4 after that word was just
  stored as 1.0 (two words); 0x823D10's two column-switch exits for an index
  outside 0..2 (two delay slots).

### 3.2 Cases

- **Capture** (298): on each sub-0 image (a19c_00..05) 00139240, 00139E00
  and 0013A3B0 on the three live creatures (with their captured state
  bytes), 001B2B80 / 001B3250 / 001B37D0 / 001B34F0 / 001CB1F0 on them,
  00154F00 and 001545B0 on the six tendril actors, 00194240 on the player,
  00197390 on the camera and player; on every image 001F6AC0 on the two
  light records, 001F6B90, 0x827B10, 0x827B20; on the sub-1 images every
  live sub-1 placement with its own entry (0x823780, 0x823D10, 0x826570,
  0x826840, 0x826C10, 0x827430, 0x829A70 in both; [34] 0x8279E0 and the 11
  0x827B60 objects in a19c_06).
- **Designed** (533, the bound and survivor cases included): every state and sub-state
  of every function with each callee result on both sides of its test, on
  the captured nodes with the state bytes patched or on designed records in
  the free area 0x1C00000: the story bytes (D_0081077B / 778 / 779 / 7F8 /
  7F9 / 7FB / 81E / 79E / 838, the area byte D_00810702, 0x70003B8D /
  B8F), the counters one below and at their bounds (+0x28 at 0x77 / 0x78,
  0xB3 / 0xB4, 0x45 / 0x46, 0x12B / 0x12C; the countdowns at 1 / 2), the
  lift's player box inside and on each edge, the tables D_002451A0 /
  D_002451C0 rolled to each value, the grab states 0..9, 10 and 0xFF with
  every floor / variant / cancel, the tendril pieces in every position band
  with and without the fast flag, the four bar pads inside and outside in
  both axes, the probe results for every 001B3250 outcome and a 001B37D0
  search ending at the first, a middle, the last and no heading.
- **Bound cases** (`bound_cases`, in the designed set, EM_TEST_FULL=1
  only beyond DEFAULT_KEEP): these float compares, and only these, with the
  compared value exactly at the bound and one float either side: 0x823780's
  and 0x823D10's phase wraps at 2, 0x823D10's size floor and its end at 5,
  0x824A90's end at 1.5, the lift box 850 / 859.6 (x) and 850.5 / 855 (z)
  and its rest height 370, 0x827430's height equality, 0x827550's 445 /
  460, 00139240's distances 50 / 100 / 20 / 15, its speed 0.4 and its two
  height tests, 00139E00's state-1 speeds 0.8 / 1.0 and state-3 0.4,
  0013A3B0's state-2 0.8, state-5 17, state-7 0.4 and the state-3 / state-9
  heights, 00194240's radius 15 for pad 0 along x and its height 4 for pad
  0, 001B3250's height word, 001B34F0's radius and height, and 00154F00's
  0.5. The review's survivor cases (section 3.4) add four more bounds:
  0013A3B0 state 2's +0x50 < 0 and state 7's +0x44 < 0 at exactly 0,
  00194240 pad 3's radius along z at exactly 15, and the unsigned row byte
  D_00810702 at 0x80. Every other float compare (other compares against 0,
  the bar-pad radius of pads 1 and 2 and pad 0 along z, the other pads'
  heights) has no case exactly at its bound. Both outcomes are run: the
  review's branch record over the EM_TEST_FULL=1 set (827 cases, before the
  four review cases) saw 307 conditional branches executed, all both ways
  except six whose other outcome no input reaches (the review names the
  DEAD_WORDS exits, 0x823C00's test of blk +4 against the 1.0 just stored
  there, and 0x139F18's v < 1.0 right after v <= 0.8 held).
- **The default run's share.** Every capture case, the designed cases a
  coverage pass picked (`DEFAULT_KEEP`, 165: with the capture cases they
  reach every word the whole set reaches), one case of every entry and the
  survivor cases (`survivor_cases`, 13, section 3.4). The
  default run therefore proves word coverage, not both outcomes of every
  branch; the remaining designed and bound cases run with EM_TEST_FULL=1.

### 3.3 Measured

2026-10-02, M1, host shared with other lanes (rerun after the review
round). Default run: 479 cases (298 capture + 181 designed: the 165
DEFAULT_KEEP picks, one case of each entry the picks miss and the 13
survivor cases), 770 runs (291 poisoned), 8,025 call entries compared,
4,394 helper register rehearsals, coverage 4,366 / 4,366 reachable
non-branch words (DEAD_WORDS left out), the header check, the callee check,
the fail-stop checks and the hook contract (4,145 native runs on 228 cases)
passing; 10.7 s user + 1.0 s system CPU, 4.1 s wall with 4 workers.
`EM_TEST_FULL=1`: 831 cases (298 capture + 533 designed), 1,388 runs (557
poisoned), 14,565 call entries compared over all memory, 6,998 rehearsals,
the hook contract on 287 cases (7,847 native runs, every call and every
access); 602 s user + 5 s system CPU, 2 min 41 s wall. All pass.

### 3.4 Mutation sweep (bounded, one round)

Single-edit mutants of the three sources (the float compare macros
swapped, add / sub, `==` / `!=`, `&&` / `||`, the integer comparison
operators, unsigned / signed byte and halfword loads, a halfword store made
a byte store and a word store a halfword store, hexadecimal constants +1 and
^4, small integers +1, dropped stores), a seeded sample (seed 0xB13, 150 of
the mutants), each built separately and run in its own process through every
case of the entries of its source file (EM_LEVEL13_PORT_ALL=1, contract
skipped; the boot file's mutants also through 0013A3B0's and 0x827B20's
cases, its callers). The sweep script is lane scratch (not committed); it is
rerunnable from this description. A first attempt (stopped at mutant 92 when
another process removed its scratch directory) had shown two real gaps,
whose survivor cases were added before the round below: one unit in the
last place of 00139E00's pitch constants 7 / 5 (invisible at the captured
heights; a zero height and z show it) and 00154F00's 0.5 one unit higher
(the bound cases had idle pieces).

- 150 mutants: 139 killed (one of them by a crash: a mutant that skipped
  `l13_begin` faulted the worker), 2 rejected by the compiler under
  `-Werror` (a signed / unsigned comparison, an `&&` inside `||` without
  parentheses), 9 survived.
- **Real gap** (1): 0x827B60's lower probe point y - 4 with the 4 one unit
  higher (hidden at the captured heights). The survivor case (an object at
  height 0) kills it (rerun: killed, the real translation passes).
  Proactively, survivor cases with D_008107F9's low nibble 4 were added for
  every nibble test of the lift and its valve.
- **Contract gap** (1): 0x829A70's `return -1` after a failed 001AFA90 made
  `-2`. The sweep skips the contract, but the default contract also passed
  it: it failed each hook in one entry only. The contract now also fails
  the first call of every callee of each site and picks its sites so that
  every (entry, hook) pair seen on a passing case is covered; rerun with the
  mutant, the default run fails at that site.
- **Equivalent survivors** (7, argued from the code): an internal helper's
  `return -1` made `-2` twice (0013A3B0's state switch and 00139E00's grab,
  whose value only signals a fault); 001B37D0's internal result after a
  fault (discarded); a halfword read signed instead of unsigned where only
  the stored low half (ent +0x2C + 1) or bit 13 (001B3250's +0x1A) is used;
  a byte read signed where only bit 7 is used (D_008107F9 in 0x823D10); the
  lift's nibble mask 0xF made 0xB right after the nibble was stored as 0 or
  1.

**Review round** (the reviewer's own bounded sweep, seed 0x13E7, 40
stratified mutants: 33 killed, 7 survived; no further sweep was run). Four
survivors were not equivalent; each now has a pinned survivor case
(`survivor_cases`, the last four), and each mutant was rebuilt and run
through the lane test: it fails exactly its new case (1 of 479), and the
real translation passes all 479.

- 0013A3B0 state 2, the +0x4C pick: the original tests +0x50 < 0 (0.65
  when below, else 0.4); the tracking cases only used +0x50 = +-0.5. Case
  `survivor 13A3B0 s2 +50 zero` (the a19c_02 creature, player at (1000,
  200, 950), the creature 3.5 behind it, +0x50 = 0): the original stores
  0.4; `<` made `<=` stores 0.65.
- 0013A3B0 state 7, the push: the original pushes only when +0x44 < 0; the
  cases used +-0.5, 0.6, +-0.2. Case `survivor 13A3B0 s7 +44 zero`: the
  original makes no push; `<=` calls 001B2B10 / 001028B8 / 0019AD00.
- 00194240, pad 3's z (bits 0x445CC666, about 883.1): the only radius bound
  case was pad 0 along x. Case `survivor 194240 pad 3 z edge` (the player
  at pad 3's x, y 268, z = that value + 15, exact in single precision):
  225 is not below 225, so the original misses; the constant one unit
  higher hits (D_008106F2 = 3, result 1).
- 00197390, the row byte D_00810702: the original's load is unsigned; every
  case used a row below 0x80. Case `survivor 197390 row 0x80`: a signed
  read indexes D_0024A6C8 at a different address (the access compare
  fails). Low practical severity: the area byte is small in the game.

The other three are equivalent, argued from the code:

- 0x829A70's sub-state byte +5 read signed: it is only compared with 0 and
  1, and a sign-extended byte is 0 or 1 exactly when the unsigned one is.
- 0x823780's D_008107F8 read signed: it is only tested for != 0, which sign
  extension does not change.
- 0x826840's D_008107F9 read signed before `& 0xF`: sign extension changes
  only bits 8 and up, which the mask clears.

In each the loaded address and size are the same, and the harness records a
load as its address and size (the bytes it reads are the memory already
compared), not its signedness, so nothing observable differs.

### 3.5 The reused translations

0016A4B0 (the push / swing state), 0016A8B0 (the swing), 001818D0,
00181950 and 00181A70 are translated in `em_player_closure_10_12_19.c`.
Only 0016A8B0 has a public entry (`em_player_closure1019_0016A8B0`); the
other four are static functions of that file (`e0016A4B0`, `e001818D0`,
`e00181950`, `e00181A70`), reached only through the public state entries
`em_player_closure1019_00169730` (state 0x10: 001818D0 and 0016A4B0) and
`em_player_closure1019_0016AE40` (state 0x12: 00181950 and 0016A4B0), with
0016A4B0 calling 0016A8B0, 00181950 and 00181A70. A host cannot call the
four directly; it binds the state entries (see Binding). They are verified by
`tools/test_player_closure_10_12_19_reference.py`: an original-instruction
oracle over the captured AREA11 RAM with every conditional-branch outcome of
those routines executed (PLAYER_CLOSURE_10_12_19.md section 5). They are
not re-translated or re-tested here. Their behaviour does not depend on the
area beyond the player record and the probe results their workers return,
which that oracle scripts. No a19c end-of-beat image holds the player in
these states (the swing ran inside a19c_00 and a19c_03, the images are taken
after the landing and the slide), so a capture case here would only patch
the same record fields that oracle already sweeps. Their census ordering
(the swing at a19c_00 f644..f828: 001818D0 for the wall ahead, 0016A4B0 /
0016A8B0 for the swing and release, 00181950 / 00181A70 for the release's
probes) matches the closure document's call structure.

## Binding

Nothing calls these entries. To run them live a host must:

- supply `bytes` over the scene's original-byte storage (the actor pool,
  the player block, the story bytes D_00810700.. / D_00810775.. /
  D_008107F5.. / D_00810838 / D_00810854, the AREA19 overlay's data (the
  packet models 0x82AF50 / 0x82B000 / 0x82B090 / 0x82B120 / 0x82B2D0, the
  colour 0x82F680, the scripts and areas the entries name), the scratchpad
  (0x700036A0.. matrices, 0x700038A0.. vectors, 0x70003400 / 0x70003600,
  0x70003A20, 0x700031D0, 0x70003B8D..B92), the boot tables (D_002451A0 /
  D_002451C0, D_00248120, D_0024A6C8, D_0028A704, 0x26D1E0 is not read by
  the translation), the renderer context behind D_00275670 / D_00275B44 and
  its DMA lists, the light records 0x25D270 / 0x25D2C0, and the stack frame
  below `sp`), and point D_00275B40 at the running actor's bone array as the
  frame loop does;
- **bind the ninth level's hook slots** (`em_level9_port.h`,
  LEVEL9_PORT.md "Binding"; this lane does not edit em_level9_* files):
  001386E0's dispatch by +5 w_00139240 / w_00139E00 / w_0013A3B0 to
  `em_level13_port_00139240` / `00139E00` / `0013A3B0` (the same self and
  ent), and 001549C0's w_001545B0 / w_00154F00 to `em_level13_port_001545B0`
  / `00154F00` (the latter with the original sp at that call);
- **bind the eighth / tenth / eleventh levels' slots**: w_001B3250
  (`em_level8_port.h`, `em_level10_port.h`) to `em_level13_port_001B3250`
  and w_001B37D0 (`em_level8_port.h`, `em_level11_port.h`) to
  `em_level13_port_001B37D0`, each with the original sp at the call;
- bind `em_area06_port.h`'s w_00194240 (00169250's state 0) to
  `em_level13_port_00194240` (its result unused there) and
  `em_camera_leftovers.h`'s w_00197390 (mode 0's state 15, today NULL) to
  `em_level13_port_00197390` with the camera record's and the player's
  original addresses;
- **bind this module's hooks to the twelfth level**: w_0013C8C0,
  w_0013BBB0, w_0013BA20 and w_001B2F70 to `em_level12_port_0013C8C0` /
  `0013BBB0` / `0013BA20` / `001B2F70` (the first two with the original sp:
  each is entered with the caller's sp minus its own frame);
- run 001CB1F0 as the +0x4C method of the actors whose kind selects it
  (0x26E310 entry 10; `em_status_models.c` names it there), the AREA19
  entries as the node behaviours of their placements (sub 1: [34] 0x8279E0,
  [35] 0x827430, [36] 0x826C10, [37] 0x826840, [38] 0x826570, [39]
  0x823D10, [40] 0x823780, [46] 0x829A70, [53] 0x827550, the 0x827B60
  objects; 0x824A90 as the effect [7]'s script spawns), 0x827B10 /
  0x827B20 / 0x829840 as the script callbacks that call them, 001F6AC0 /
  001F6B90 where 001F6BB0 / the scripts reach them;
- the reused five (section 3.5) are bound through the player state table,
  not here: `em_player_closure1019_00169730` (state 0x10) and
  `em_player_closure1019_0016AE40` (state 0x12) reach the static
  001818D0 / 00181950 / 00181A70 / 0016A4B0 and the public
  `em_player_closure1019_0016A8B0`; PLAYER_CLOSURE_10_12_19.md owns that
  binding;
- bind each hook to a verified port translation or a fail-stop stand-in.
  Port entries already exist by address for several hooks (for example
  `em_pose_host_001C67E0` / `001C68C0`, the sdk math and the twelfth
  level's entries above); their verification status is their own lanes',
  not re-checked here. The stubbed callees (every hook not in the RUN list)
  are not verified here, and the helpers run as original code need port
  translations paired by address.

## Known gaps

- **Live evidence.** The captures are end-of-beat states: the creatures sit
  in behaviour 1 or 5, the tendril pieces are idle in a19c_00..03 (two are
  active in a19c_04 / 05),
  the lift in state 4, the valves and [46] in state 1; behaviours 2 / 3 / 4,
  the moving pieces, the lift's scripts, [53], the effect 0x824A90 and the
  spawner 0x829840 run only on designed states.
- **Census lower bound.** The census sets are those of the route shape, not
  of the exact frames (THIRTEENTH_LEVEL_ROUTE.md section 5.2: six of eight
  replays walked different paths); functions run only off this route
  (0x823770, 0x823CA0, the lift's callback 0x827540, AREA15) are not rows
  here.
- **Stubbed callees' inputs.** Only the RUN helpers are proven not to read
  a register their hook does not pass; the other hooks' argument lists were
  taken from a static read of each callee's entry and earlier lanes' HOOKS
  tables, and their results are scripted beyond their real ranges.
- **Dead words** are argued from the code (section 3.1), not executed.
- **What the harness cannot see** (as LEVEL12): whether an access that
  changed nothing is a load or a store of the same value; a load's
  signedness (only the value it produces downstream); hardware
  behaviour past a stop; the frame bookkeeping.
- **Float proofs rest on the EE model** (`em_ee_float.h`), including the
  MULA / MADD pair.
- **Mutation sweep** is a bounded sample, not exhaustive.
- **The reused five** were not re-run over the a19c images (section 3.5).
