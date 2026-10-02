# The fourteenth level: the new functions of the a19d / a15 route (lane L14T)

Lane L14T, 2026-10-02 (level side track). It covers the 59 functions the
fourteenth-level route census found new to any census pass (decomp
`build/s87/census/a19d_delta.json` and `a15_delta.json`, `new_functions`,
less the 31 rows the first level's C10 passes already ran;
FOURTEENTH_LEVEL_ROUTE.md section 5: 36 boot, 2 AREA19, 21 AREA15; 27,568
bytes): the creatures of 0012E3A0's family in their state 2 / 3 (the hall
creatures shot in a19d_00 / 03), the hit effects, the player's ceiling hang
(state 0x11 into 0x12), the ledge-top test, the running jump's target
helpers, the AREA19 lift's script callbacks, the AREA15 enemy 00153950, and
the AREA15 overlay: its init, sub 0's [0] and its swinging gates, the
placements that end at once on this route, and sub 1's placements. Nineteen
of the 59 already had a verified port translation and are reused (section
0); the other 40 are translated here.

Every translation is compared with the original instructions by
`tools/test_level14_port_reference.py`. What that comparison covers is
exactly what section 3 states: the comparisons the test makes, on the cases
it runs. Nothing is bound: no port code calls these entries yet.

| File | What |
|---|---|
| `src/game/em_level14_port.h` | public API: 40 entries, the hook table (72 hooks and the indirect-call callback), fault codes |
| `src/game/em_level14_port_internal.h` | memory view, fault latch, EE float helpers, one typed wrapper per hook (written from the test's HOOKS table; section 1, Hooks), the internal entry points |
| `src/game/em_level14_port_area15.c` | AREA15 0x823580, 0x8235A0, 0x8236B0, 0x823780, 0x823850, 0x8239F0, 0x823E40, 0x824070, 0x824510, 0x824560, 0x8247B0, 0x824990, 0x824E00, 0x824E50, 0x825030, 0x8252D0, 0x825320, 0x825430, 0x825D10, 0x826600, 0x826850 |
| `src/game/em_level14_port_area19.c` | AREA19 0x826B30, 0x827540 |
| `src/game/em_level14_port_creature.c` | 00131650, 00131740, 00131B10, 00131E80, 00131F20, 001339E0; 00153950, 00153A10, 00153A90 |
| `src/game/em_level14_port_boot.c` | 0016AC50, 001782A0, 001AA2A0, 001AA410, 001B2D00, 001B4CF0, 001EDE40, 001F6BA0 |
| `tools/test_level14_port_reference.py` | original-instruction oracle and comparison |

Addresses are runtime addresses. Both overlays are linked 0x40 below where
they run (0x8235A0 is `func_overlay_AREA15_00823560`, 0x826B30 is
`func_overlay_AREA19_00826AF0`); their data references are the runtime
addresses the resident code names (0x8272B0, 0x826E70 ... read from the
resident image). Bracketed numbers ([0], [36] ...) are the placement records
of the route documents; they, the decomp's role comments and names such as
`anim_clip_init` (001C67E0) are labels, not evidence. The behaviour below is
read from the code.

## 0. Scope: the census rows, and what already existed

A grep of the port (`src/`, `tools/`, `docs/`) for each address before this
lane found these; "hook slot" means a worker slot, a call site or a stub, not
a translation. Status as in SECOND_LEVEL_ROUTE.md section 6 (BM byte-matched
C, NM NEARMISS, AW word assembly, CA C linked from assembly, C byte-identical
overlay C).

| Row | Bytes | Decomp | First (census) | Port before this lane | Here |
|---|---:|---|---|---|---|
| 0017A800, 0017AF70 | 168, 904 | AW, NM | a19d_00 f181 | **verified translations** in `em_aim_fire_control.c` (`pitch_to_target`, `track_target`; AIM_FIRE.md, test-aim-fire-control-reference) | **reused** |
| 001B1510 | 76 | BM | a19d_00 f181 | **verified**: `em_aim_fire_001B1510` (`em_aim_fire_leaves.c`; test-aim-fire-leaves-reference) | **reused** |
| 00185E30 | 620 | NM | a19d_00 f182 | **verified**: `em_aim_fire_target_00185E30` (`em_aim_fire_target.c`; test-aim-fire-target-reference); hook slot w00185E30 in `em_player_weapon_states_a.h` | **reused** |
| 00131650, 00131740, 001339E0 | 236, 500, 52 | AW, NM, BM | a19d_00 f243 | hook slots w_00131650, w_001339E0 (`em_level8_port.h`: 0012E3A0's state 2, 00130xxx's fall) | translated |
| 00131F20, 001B2D00 | 112, 324 | BM, BM | a19d_00 f566 | hook slots w_00131F20 (`em_level8_port.h`, `em_level10_port.h`), w_001B2D00 (`em_level13_port.h`) | translated |
| 001B4CF0, 001EDE40 | 1,640, 844 | BM, NM | a19d_00 f592 | hook slot w_001B4CF0 (`em_level8_port.h`, `em_level11_port.h`); 001EDE40 none | translated |
| 00131B10 | 872 | BM | a19d_01 f2 | none | translated |
| 001F6BA0, 00131E80 | 12, 68 | BM, BM | a19d_01 f753, f754 | hook slots w_001F6BA0 (`em_level13_port.h`), w_00131E80 (`em_level8_port.h`, 0012E3A0's state 3) | translated |
| 0016AC50 | 388 | BM | a19d_01 f1873 | none (named in SFX_REGISTRY_FIRST_LEVEL.md and a callee list of `test_player_floor_reference.py`; no slot for +5 0x11 in the port's floor-state table) | translated |
| 0016AE40, 0016ADE0, 00175390, 00181E20, 00182090, 00182100 | 2,376, 96, 328, 320, 100, 212 | NM, CA, BM, NM, BM, AW | a19d_01 f1916..f1928 | **verified** in `em_player_closure_10_12_19.c`: the public state entry `em_player_closure1019_0016AE40` (+5 0x12) and the static `e0016ADE0`, `e00175390`, `e00181E20`, `e00182090`, `e00182100` (PLAYER_CLOSURE_10_12_19.md) | **reused** |
| overlay AREA19 0x827540, 0x826B30 | 12, 224 | C, C | a19d_07 f801, f883 | none (0x826B30 named in a callback table of `test_area13_assets_reference.py`, 0x827540 in a comment of `em_level13_port_area19.c`) | translated |
| 001AA2A0, 001AA410 | 356, 200 | AW, AW | a19d_08 f603 | hook slots `target_sight` / `target_radius` (`em_player_running_jump.h`; unbound stubs in `em_player_closure_live.c`) | translated |
| 00181730, 001782A0, 001787B0 | 416, 240, 340 | NM, BM, BM | a19d_12 f38, f137 | 00181730 / 001787B0: **verified** static `e00181730` / `e001787B0` in `em_player_closure_10_12_19.c`; 001782A0: hook slots (`grab` in `em_player_ladder_climb.h` and `em_player_misc_workers.h`, `ledge_3B` in `em_player_closure_0e_18.h`, the closure 10/12/19 worker; stub `x_001782A0` in `em_player_closure_live.c`) | 00181730 / 001787B0 **reused**; 001782A0 translated |
| 0017FD80, 0017FE00, 0017FF00, 00180600, 001809B0 | 120, 120, 120, 220, 1,888 | BM, BM, BM, BM, NM | a19d_13 f329..f379 | **verified**: `em_player_ladder_climb_0017FD80` .. `_001809B0` (`em_player_ladder_climb.c`; PLAYER_LADDER_CLIMB.md) | **reused** |
| 002230A0 | 1,616 | BM | a19d_14 f150 | **verified**: `em_player_major2_002230A0` (`em_player_major2.c`; PLAYER_MAJOR2.md) | **reused** |
| 001A56A0 | 180 | AW | a19d_16 f155 | **verified** inside `em_actor_collision.c` (the static `column_round`, 0019BC40's pass 1; ACTOR_COLLISION.md) | **reused** |
| overlay AREA15 0x823540 (the init 0x823580), 0x8235A0, 0x8236B0, 0x824070, 0x824510, 0x824560, 0x8247B0, 0x824990, 0x824E00, 0x824E50, 0x825030, 0x825320, 0x825430, 0x825D10; 0x823780 | 96, 272, 200, 464, 76, 580, 468, 432, 76, 480, 660, 272, 2,260, 2,276; 196 | AW (the init: C), C, C, NM, C, C, C, C, C, C, C, C, NM, NM; C | a19d_20 f521..f523; 0x823780 f9892 | none (port hits at these addresses are other areas' functions) | translated |
| 00153950, 00153A10, 00153A90; overlay AREA15 0x823850, 0x8239F0, 0x823E40, 0x8252D0, 0x826600, 0x826850 | 192, 116, 188; 416, 324, 288, 68, 580, 288 | AW, BM, BM; C | a15_01 f1097, f1098 | none | translated |

**The census row 0x823540.** The census attributes the hit at 0x823580 to
the 0x60-byte row starting at the overlay's entry pad (0x823540, word
assembly); the pad's words are not executed. The function that ran is the
overlay's init at 0x823580 (decomp C `overlay_AREA15_func_00823540`, linked
0x40 lower), and that is the entry translated here (`em_level14_port_00823580`).

**Ground truth.** Every function was written against the original-instruction
oracle (section 3) until no difference remained: the order of every load
and store between calls, every call and argument, the float operation order
and every branch outcome on the cases run. The sources of the text differ by
row:

- **Byte-matched and byte-identical overlay C** (24 rows): the decomp's C
  was the reading guide; where the compiled code orders accesses
  differently from the C statement order (0x823E40 reads +0x2A before
  +0x1C; 001B4CF0's kind 1 reads +0xD once for both bit tests; 001782A0
  reads the player's y before the frame vector's y), the oracle's access
  comparison fixed the order.
- **NEARMISS** (00131740, 001EDE40, 0x824070, 0x825430, 0x825D10): the
  decomp's C with the oracle deciding every access and call. One behaviour
  difference from the text was found: **001EDE40** passes 1, not 0, as
  001CFBE0's fifth argument (the register t0 holds 1 at all three calls;
  the test poisons the argument registers an entry does not take, so this
  is the original's value). It is recorded as a logic-note row in the
  decomp's `docs/NEARMISS.md`. The swinging gates' text re-reads the
  scratch target 0x70003A2C where the original does (its NEARMISS note);
  the translation follows the original's reads.
- **Word assembly** (00131650, 00153950, 001AA2A0, 001AA410): this lane did
  not read their instruction text. Each translation was derived from the
  original's runs over designed inputs (the test's `EM_LEVEL14_PORT_TRACE`
  mode prints the original's calls with their arguments and results, and
  its loads and stores with addresses and values, per case) and then
  verified by the same comparison as every other entry: all of their
  reachable words executed and every conditional branch both ways
  (section 3.3). 001AA410's per-kind constants, 001AA2A0's bands and the
  dispatch of the other two are the original's outputs on every input
  class run, including the bound cases; inputs outside the cases' classes
  are covered only by that branch argument.

## 1. Interface (the LEVEL13_PORT / LEVEL12_PORT design)

The design is `em_level13_port.h`'s (docs/LEVEL13_PORT.md section 1) with:

- **Entries.** 40, named `em_level14_port_<address>`, with the original's
  argument registers in order; entries with a result write the original's
  v0 (or f0) to `*result`: 0x826B30, 0x827540, 001782A0, 001AA2A0, 001B2D00
  (`int32_t`); 001AA410 (`float` with the original's bits). 001AA2A0's
  radius is a `float` with the original's bits. 00153A90's second argument
  is the address its 001C64F0 result is stored to (00153950 passes self +
  0x1F0).
- **Stack frames.** Only 001782A0's frame holds locals: it takes `sp`, the
  original stack pointer at entry; 00199FA0 fills the two frame vectors at
  sp - 0x20 and sp - 0x10, and the first one's y is read back.
- **Hooks.** 72 callee hooks named by original address: 65 boot functions
  and seven AREA15 functions this route did not run (0x823B40, 0x823C80,
  0x824240, 0x824350, 0x8243E0, 0x824B40, 0x824C90). The header's struct
  (between its GENERATED HOOKS markers) and the wrappers in the internal
  header (GENERATED WRAPPERS) were written from the test's HOOKS list by a
  lane script that is not committed; the test's `header_checks` compares
  them with HOOKS on every run. Plus `w_callback(ctx, function, a0)` for the
  indirect call (the actors' +0x4C method). Float hook arguments and results
  cross as floats whose bits are the original's.
- **Calls between translations** are direct: 0x8235A0 -> 0x8236B0 /
  0x823780; 0x823850 -> 0x8239F0 -> 0x823E40; 0x824510 -> 0x824560 /
  0x8247B0; 0x824E00 -> 0x824E50 / 0x825030; 00131650 -> 00131740 /
  00131B10 / 001339E0; 00153950 -> 00153A10 / 00153A90.
- **Floats.** EE arithmetic on bit patterns through `em_ee_float.h` (add,
  sub, mul, div, neg, cvt.s.w and the compare keys). No VU0 form is used.
- **Access order.** No C expression in the four sources reads memory twice
  where C leaves the order unspecified: every read the original orders is
  its own statement, the only read of its expression, or nested as an
  address.
- **Fail-stop.** As LEVEL13: fault 5 (unmapped or misaligned address, or
  `bytes` NULL), 1 (reached NULL hook or NULL `w_callback`), 2 (a hook
  returned < 0). After a fault no hook runs, `bytes` is not called, writes
  are dropped, the first fault is kept, no result is written and the entry
  returns -1; a fault latched on entry, a NULL hook table, a NULL fault
  pointer and (entries with a result) a NULL result pointer return -1 at
  once.

## 2. What each function does (behaviour, from the code)

self = the pool node; ent = self + 0x1F0; the player block 0x8102B0 (its
position +0xA0 = 0x810350). The header comment of each function in the
sources gives the field-level behaviour; in short:

**AREA15 (overlay id 12).** "The tail" is 001C64F0(self, 1.0), 001B17A0,
001C68C0 and the +0x4C method; "bit n" is 001BA1C0(self, n), a story bit.

| Entry | Behaviour |
|---|---|
| 0x823580 | the overlay's init: D_00275C28 = 0x20, D_00275C2C = D_00275C24 = 0, D_00275C1C = 0x829F00 |
| 0x8235A0 (sub 0's [0]) | state 0: bit 0x23 -> state 3; else 001B10B0(self, +0xD, 0x65), 001C63E0(self, 0), 001CA6F0(self, 2), +0 = 1, +0x30 = 0x8272B0, +0x58 = D_0028A6E8, state 1. State 1: D_008107FA 0 -> 0x8236B0, 1 -> 0x823780. States 2 / 3: 001AFC10 |
| 0x8236B0 | [0]'s scene: +5 0 script 0x826E70, +5 = 1, D_00810702 = 1; +5 1 at the script's end 001C4760(0xD, 1), +0x2E = 0xFFFF, D_008107FA = 1, +5 = 0, 001FAE70(0); the tail |
| 0x823780 | after the scene: +5 0 with +0xB bit 2 script 0x827130, +5 = 1; +5 1 at the end +0xB = +5 = 0, 001C67E0(self, 0, 20, 0); the tail |
| 0x825320 (sub 0's three spinning objects) | state 0: 001B0FD0, 001C6380, +0x1F0 = 0.01 + (-0.002 + 0.004 * 2^-31 * rand); state 1: +0xC4 = 001B1470(+0xC4 + +0x1F0), 001C6380, 001A2370(self, self + 0xD0), 001B17A0, +0x4C |
| 0x825430 / 0x825D10 (sub 0's swinging gates; the second mirrored) | state 0 init (+0x200 = 0.08). State 1: the player's offsets x - 869 / z - 951.5 (mirrored: -(x - 869) / 966.5 - z) in 0x70003A20 / A24. Inside \|x\| < 7.7, -2 < z < 9.5: with z < 0 the angle (bone 0 +0x74) pinned to +-1.3788102 by the side +0x1F0 and \|x\| < 2, phase pi/2, amplitude = angle; else the push (side 0 takes the sign of the player's +0xC4 (0x810374) and negates the amplitude; sound 0x404 once when \|angle\| > 0.5235988; phase += step wrapped at +-pi; the target 2.0799727 * (9.5 - z) * (x + 4 clamped at 0, or x - 4 clamped by the side) clamped to 79 degrees, in radians, in 0x70003A2C; beyond the amplitude it follows amplitude * sin(phase) when that is further, else eases by 0.01; within it the amplitude becomes the target, phase pi/2; the angle = the target). Outside: while the amplitude is above 0.02 the free swing (sound 0x403 at 300 * amplitude on each zero crossing and at the +-pi wrap; angle = amplitude * sin(phase); amplitude *= 0.985), else angle = amplitude = 0. Then 001C6380, 001A2370(self, bone 0 + 0x90), +1 = 001B1630(+0xB0, +0xB4, +0xB8), with +1 set and \|angle\| > 1.3613569 001B1B70, +0x4C |
| 0x824070 | state 0: state 3 unless bit 0x23 set and 0x28, 0x29 clear; +0xD 0x50 / 0x64 model set-ups; 001BA8E0(self, +0xD), state 1, +0 = 1, +0x30 = 0x8288F0. State 1 by D_008107FC: 0 -> 0x824240 (only +0xD 0x64, else return), 1 -> 0x824350 (+0xD 0x50), other -> 0x8243E0; then 001C68C0, +1 = 1, +0x4C. States 2 / 3: 001BA540, 001AFC10 |
| 0x824510 -> 0x824560 (+0xD 0x64) / 0x8247B0 (+0xD 0x50) | state 0: bit 0x25 set, or 0x28 and 0x29 clear -> state 3; else the model set-up (0x65 / 0x52, 001C63E0 3 / 4, 001CA6F0 2, 001BA8E0, +0x58, +0x30 = 0x828BA0 / 0x828BB0). State 1: +5 0 with +0xB bit 2 the script 0x828900 / 0x828A40; +5 1 at its end 001C67E0(self, 3 / 4, 20, 0), +0xB = +5 = 0 (0x824560 also counts +0x28 and at 0x208 copies 0x828B80 / 0x828B90 into D_008105D0 / 5E0 and D_008101F0 / 200); then 001BA580(self, +0xD) and the tail. States 2 / 3: 001BA540, 001AFC10 |
| 0x824990 | state 0: bit 0x30 clear -> state 3; +0xD 0x50 / 0x5A model set-ups (+0x30 = 0x829260 / 0x829270); 001BA8E0, state 1, +0 = 1. State 1: bit 0x2A -> state 3; D_008107FF 0 -> 0x824B40, 1 -> 0x824C90. States 2 / 3: 001BA540, 001AFC10 |
| 0x824E00 -> 0x824E50 (+0xD 0x59) / 0x825030 (+0xD 0x50) | state 0: bit 0x2C set or 0x26 clear -> state 3; else the model set-up. 0x824E50: +5 0 with D_00810702 1 and D_00810784 0 script 0x829330; +5 1 at its end 001C4760(0x15, 1), 001AEE10(4, 0), D_00810804 = 1, +0x2E = 0xFFFF, +5 = 2, 001FAE70(0); the tail (with +1 = 1) while D_00810804 is set. 0x825030: +5 0 with D_00810804 set places it at (975.5, 240, 902.5), +0xC4 = 0, 001C67E0(self, 7, 0, 0), +5 = 1, the tail while D_00810784 is 0; +5 1 with +0xB bit 2 script 0x8296F0, +5 = 2; +5 2 at its end 001C67E0(self, 7, 20, 0), +0xB = 0, +5 = 1; the tail |
| 0x823850 (sub 1, +0xD 0x54 / 0x5D) | state 0: bit 0x23 set or 0x22 clear -> state 3; 0x54: 001B10B0(.., 0x56), 001C63E0(self, 8), D_008106C0 = 001B6660(0x826CD0); 0x5D: 001B10B0(.., 0x5F), 001C63E0(self, 0), +0x58 = D_0028A610; +0x28 = +0x2A = 0, state 1, +0 = 1. State 1 by D_008107FB: 0 -> 0x8239F0, 1 -> 0x823B40, 2..4 -> 0x823C80. States 2 / 3: 001AFC10 |
| 0x8239F0 | (+0xD 0x54) +5 0: the player's y not below 310 and 001B1EA0(0, 0x810350, 0x827C80, 6) == 1 -> +5 = 1, D_0081077B = 1, script 0x827400; +5 1 at its end 001FAE70(0), D_008107FB = 1, +0x2E = 0xFFFF, +5 = 0, +0x28 = 900. While D_70003B92 is 0 the tail and 0x823E40 |
| 0x823E40 | +0x2A + 1; on multiples of 8: past 240 +0x240 = 2 + ((rand >> 16) * 4 >> 15), +0x2A = 0; then +0x240 counts down, or at 0 the point 300 along the +0x1C object's angles (00103230) plus its position into 0x700038A0, 001FBD50(self, 0x164, 0, 800), 001B0CD0(object, 0) |
| 0x8252D0 | +3 == 1 -> 001C5C90(self), else 001C4820(self) |
| 0x826600 | state 0: +0x1F0 = -1, 001B0FD0, with bit 0x22 the +0x120 object's +0x84 -= 20, 0019C6F0(0x22, 1), 001C6380. State 1: bit 0x23 -> state 3; with D_70003B92 0: bit 0x22 turns the +0x114 object's +0x74 by 0.6108653 and the +0x118 object's +0x70 by 0.69813174 (001B1470) and 001FC3C0(self, self + 0x1F0, 0x44F, 600, 4096); the point (-2.575, 45.472, -102.748, 1) through self + 0xD0 (001026A0) to 001F5940(2, .., 0); 001C6380, 001A2370, 001B1B70, +0x4C; with D_70003B92 set 001FC520(self + 0x1F0). States 2 / 3: 0019C6F0(0x22, 0), 001FC520, 001AFC10 |
| 0x826850 | state 0: 001B0FD0, 001CA5F0(self, 0xC) for +0xD 0xD, 001C6380. State 1: +0xD 0xC: D_008107FB >= 3 -> state 3, else 001C6380, 001B17A0, +0x4C; other +0xD with D_008107FB >= 3: 001C6380, 001B1B70, +0x4C. States 2 / 3: 001AFC10 |

**AREA19 (overlay id 16).**

| Entry | Behaviour |
|---|---|
| 0x826B30 (the lift's script callback) | three 001EFD90(0x80000015, 0x700038A0, self + 0xC0) at (855.9, 370, 863), (850.9, 370, 856), (863.9, 370, 876.3); returns 1 |
| 0x827540 (callback) | a0 +0x28 (halfword) = 1 (the lift's drop flag read by 0x826C10); returns 1 |

**Boot: the creatures of 0012E3A0 (ent = self + 0x1F0) and the AREA15 enemy.**

| Entry | Behaviour |
|---|---|
| 00131650 (state 2) | by +5: 0 00131740, 1 00131940, 2 00131B10, others nothing; the +0x20 object (if any) +4 = 3, +0x20 = 0; 001339E0; 00132490, 001328D0; ent +0x58 = 001C64F0(self, ent +0x34); 00131ED0, 001C68C0, 001B17A0, +0x4C |
| 00131740 | +6 0: +6 + 1, ent +0x63 = 0, ent +0x3C = 0, ent +0x34 = 1.0, sound 0x7D4; with ent +0x61 set, the +0x2C byte 4 and (float)001C6160 - +0x3C >= 65: ent +0x61 = 0; ent +0x61 still set -> clip 0x27; else +0 = 1 when +0x34 is nonzero, clip 0x28 and sound 0x7D6 with +0x36 bit 15, else the clip D_00275390[(rand >> 11) % 3]; +0x36 = 0. +6 1: ent +0x58 bit 0x1000 with +0x34 nonzero -> the reset (+0 = 1, +0x36 = 0, +4 = 1, +5 = +6 = 0, ent +0x62 = 0, ent +0x6A = 0x3C); with +0x34 zero +5 = 2, +6 = 0; without the bit and ent +0x61 0: 00133A20 |
| 00131B10 | +6 0: +6 + 1, 001B1190(+0x9A), sound 0x7D8; +0xD bit 1: +6 = 3, ent +0x34 = 1.0, D_008107EA = 1, clip 4 / 5 by ent +0x61; else ent +0x34 = 1.0, clip 0x29 with ent +0x61, else 0x2A / 0x2B by rand bit 9 (all at 5.0). +6 1: the clip (+0x2C without bit 15) 0x2A at +0x3C == 34 or 0x2B at 21 -> sound 0x7D5; on ent +0x58 bit 0x1000: +6 + 1 and, without +0xD bit 1, ent +0x6D = 0, sound 0x7D7, 001EFE00(0x8000001E, self) zero -> +4 = 3. +6 2: with +0xD bit 1, D_0028A9A2 set and D_0028A9A0 == 2: +4 = 3 and the +0x24 object's +4 = 3, +0x24 = 0. +6 3: with D_008107EA == 2: +6 = 1, ent +0x34 = 0.35, +0x24 = 001EFE00(0x80000046, self), clip 0x2A / 0x2B |
| 001339E0 (a0, ent) | with ent +0x65 set: ent +0x65 = 0, 0021C040(0x8102B0, a0) |
| 00131E80 (state 3) | the +0x20 object (if any) +4 = 3, +0x20 = 0; 001B1190(+0x9A), 001AFC10 |
| 00131F20 (self, a1, a2) | a1 through the rotation of self's +0xC0 angles (001029C0 / 00102C58 into 0x700036A0) into a2 (001026A0) |
| 00153950 (the enemy) | by +4: 0 00153A10, 1 00153A90, 2 00153B50, 3 00153EA0 (and nothing more); then with ent +0x10 set 001B5360 |
| 00153A10 | +4 + 1, +0 = 1, ent +0x10 = 1, +0x34 = 0x3C, +0x30 = 0x275440; 001B10B0(self, 0x72, 0x71) zero -> 001C63E0(self, 0x33), +0x58 = D_0028A65C |
| 00153A90 | the mailbox +0x36: set with D_0081077B 0 -> +0 = 1, +0x36 = 0; set otherwise -> +4 = 2, +5 = 0; then unless D_0081077B is 1 with D_008107FB 0: *out = 001C64F0(self, 1.0), 00131ED0, 001C68C0, 001B17A0, +0x4C |

**Boot: the others.**

| Entry | Behaviour |
|---|---|
| 0016AC50 (player +5 0x11, the ceiling hang's start) | +6 0: +6 = 1, +7 = 0, +0x28 = 8, +0x2E4 = (+0x254 - +0xB4) / 8, clip 0x70; 1: on +0x200 bit 0x1000 clip 0xBA, +0x25F = 3, +6 = 2; 2: +0x28 zero -> +0xB4 = +0x254, sound 0x110, +6 = 3; else +0xB4 += +0x2E4, +0x28 - 1; 3: on bit 0x1000 +5 = 0x12, +6 = 0, +0x1F0 = 0x22, +0x1F1 = +0x25D = +0x2F1 = 0, clip 001885B0(p) (001749A0, all at 1.0) |
| 001782A0 (the ledge top) | 00199FA0(sp - 0x20, sp - 0x10) zero -> 0; t = (3 + +0xB4 - y) / 3 into 0x70003A20 (y the first vector's y); t below 0 -> 0; else 0x70003A20 += 0.6, +0x2E4 = y + 3 * (float)001281C0(that), 00199DB0(0x700038A0), +0x2E0 / +0x2E8 = its x / z, +0x218 = +0xC4; 1 |
| 001AA2A0 (p, object, radius) | d = 0011E748(dx^2 + dz^2) (object minus p, x and z) into 0x70003A20; d above radius -> 0; dy = object y - p y: kinds 4 and 8 accept -8 <= dy <= 30, the others -13 <= dy <= 10 |
| 001AA410 (object) | by the kind +3: 1, 2, 4, 5, 7, 0xC -> 20; 6 -> 35; 8 -> 30; others 0 |
| 001B2D00 (a0, a1) | 0019BC40(a0); over the column table (n = *0x700031E0, heights 0x700030F0, flags 0x70003170): the first height not below a0 +4: flag bit 0 -> *a1 = it, 1, else 0; none: the last one's flag -> 0; else (not the first) the one before with its flag (0x7000316E) -> *a1 = its height (0x700030EC), 1; else 0 |
| 001B4CF0 (p, the hit effects) | by the kind +3 (1, 3, 4, 5, 6, 7, 9, 10, 11; others nothing): an offset (by +0xD bits 0 / 7) into 0x70003600, a bone index and an effect id; the angles +0xC0 into 0x70003610 (kinds 6 / 9 / 10 turn y by pi / pi/2 / pi through 001B1470); the offset through bone idx's matrix (001026A0), 001EFD90(0x80000036 / id / 0x80000025), sounds 0x15C / 0x15D |
| 001EDE40 (a0, a1) | the parameter slots of the effect records 0x257510 / 0x2575A0 / 0x257630; per record the generator *D_00275C34: phase = (float)(+4 >> 16) / 65535 + 1e-4, +4 = +4 * 37 + 11, 001CFB50(0x81F8F0, 0, a0, +0x54, phase, 1.0, 1e-6, 10.0), 001CFBE0(a1, 1, record, 0x81F8F0, 1); then +8 eases toward 0.02 by a tenth, floored at 0.02 |
| 001F6BA0 | 001F66F0(0x25D270) |

## 3. Verification

`python3 tools/test_level14_port_reference.py` (port root, macOS arm64 or
Linux; no make target: the Makefile belongs to another chain while this side
track runs). It compiles the four sources into
`build/level14/port/level14_port.dylib` with `-std=c11 -Wall -Wextra
-Werror -Wpedantic -ffp-contract=off` (all of `build/` is ignored;
`build/level14` can be removed after a run). At most 4 worker processes
(EM_TEST_JOBS overrides). `EM_LEVEL14_PORT_ONLY=<label prefix>` runs a
subset (no coverage, fail-stop or contract checks); `EM_LEVEL14_PORT_ALL=1`
runs every designed case without the full-mode comparisons;
`EM_LEVEL14_PORT_MISSING=1` lists unexecuted words;
`EM_LEVEL14_PORT_TRACE=<label prefix>` prints the original's behaviour trace
of the matching cases (calls, loads and stores with values; no instruction
text) and stops; `EM_LEVEL14_PORT_PCS=<hex,..>` lists which cases executed
the given original words; `EM_LEVEL14_PORT_SOURCE=<dir>` tests other copies
of the sources; `EM_LEVEL14_PORT_NOCONTRACT=1` skips the hook contract
(debugging only); `EM_LEVEL14_PORT_KEEP=1` prints the coverage pass behind
DEFAULT_KEEP.

### 3.1 Oracle and harness

The LEVEL13 harness (docs/LEVEL13_PORT.md section 3.1), copied and owned
here, with:

- **Images.** Nine recorded fourteenth-level RAM images: the ends of
  a19d_00, 01, 03, 06, 07, 08, 12 (AREA19 resident; the 0012E3A0 creatures,
  the lift, the player in the hang, the run, the post), a19d_20 (AREA15
  sub 0) and a15_01 (AREA15 sub 1). Before any case the test asserts that
  each image's overlay text equals the user's `extract/OVERLAY/AREA19.BIN` or
  `AREA15.BIN`, that the boot text equals the pinned ELF and that the
  designed-record area 0x1E40000 is zero (the AREA15 images use the area
  0x1C00000 the earlier lanes took).
- **Argument registers.** The argument registers an entry does not take
  (a0..t3, f12..f19) are poisoned at its start, so an entry that read one
  would differ from the native side.
- **Jump table.** 001B4CF0 dispatches its kind byte through the 12-entry
  table 0x26DE70 (the translation is a switch); the table's loads are left
  out of the access comparison, every other access is compared.
- **Callees.** A static check (`callee_checks`, every run) walks every
  entry's reachable words: every call target is a hook or another entry.
  Callees run as original code (RUN, each first rehearsed with the argument
  registers its hook does not pass poisoned): 001026A0, 00102948, 001029C0,
  00102C58, 00103230, 0011DF78, 0011E2A8, 0011E748, 001281C0, 001B1470
  (some cases stub 001281C0). Every other callee is stubbed with scripted
  results (a Scribble writes what the callee would: 00199FA0's frame
  vectors, 00199DB0's point).
- **The +0x4C method** is stubbed as the callback.
- **Frame bookkeeping.** As LEVEL13, with one listed exception: 001782A0
  loads the first frame vector's y into $f20 (`LOCAL_STORES`), which is
  compared.
- **Compared, per case**: as LEVEL13 (the calls and their arguments; memory
  at every call entry; the memory accesses between calls one for one, in
  order, by address, size and changed-or-not; all memory after the last
  store; the return value; the store-log self-check; stops at unmapped or
  misaligned original accesses; every case again from a poisoned start
  image; coverage of every reachable original non-branch word of all 40
  entries; the table's ctx at every call). New here: the **branch report**
  records the outcome of every conditional branch the entries execute at
  their own level and prints how many went both ways.
- **Header**: `header_checks` (every run).
- **Fail-stop**: `fault_checks` (0x826850 on a15_01's +0xD 0xC object in
  state 1, which calls 001C6380, 001B17A0 and the +0x4C method: NULL hook,
  failing hook, an unmapped +0x4C after the second call; 0x827540 for an
  unmapped address before any call; a latched fault; every entry with a
  latched fault, a NULL hook table, a NULL fault pointer and, for the
  entries with a result, a NULL result pointer) and `hook_contract_site`
  (as LEVEL13: every (entry, hook) pair seen on a passing case fails at
  least once inside that entry; EM_TEST_FULL=1 every call and every access).
- **Dead words** (`DEAD_WORDS`, 8): in 0x825430 / 0x825D10, the two
  not-taken words of the clamps after the target's product with 9.5 - z
  (positive branch: the product is never below 0, the factor was clamped to
  >= +0 and 9.5 - z > 0 inside the box; negative branch: never above 0).
  They were located by the cases that run the neighbouring words (the
  79-degree clamp before, the join after), not by reading the words.

### 3.2 Cases

- **Capture** (92): every live AREA15 node of a19d_20 and a15_01 with its
  own entry; the init on both; 0x826B30 / 0x827540 on the lift node (a19d_00,
  01, 03, 06); 00131650, 00131740, 00131B10, 001339E0, 00131E80, 00131F20,
  001B4CF0 and 001B2D00 on the 0012E3A0 creatures of a19d_00 / 01 / 03 / 07
  / 08 / 12; 0016AC50, 001782A0 and 001F6BA0 on the player (a19d_01 / 08 /
  12); 001EDE40 on a19d_00 / 07.
- **Designed** (1,412 in EM_TEST_FULL=1, the bound and survivor cases included): every
  state and sub-state of every function with each callee result on both
  sides of its test, on the captured nodes with the state bytes patched or
  on designed nodes (a copy of a19d_20's [0] at 0x1E41000 with the entry's
  +0xD and behaviour word) for the AREA15 placements that end at once on
  the route: the story bits (001BA1C0 scripted in every order the gates
  read them), the story bytes (D_008107FA / FB / FC / FF, D_00810702 /
  784 / 804, D_0081077B, D_008107EA, D_70003B92, D_0028A9A0 / A2), the
  counters at and around their tests (0x823E40's +0x2A on and off multiples
  of 8, negative, past 240; 0x824560's +0x28 at 0x207 / 0x208), the swinging
  gates' player offsets inside and outside the box, every pin, push and
  free-swing path, and a seeded sweep of 240 random gate states (EM_TEST_FULL
  only); 001B4CF0 every kind 0..12 and 0xFF with both bits; 001AA410 every
  kind 0..15 and three above; 001AA2A0's distance against the radius and
  dy across both bands for kinds 0..15 and five above; 001B2D00's column
  tables (found / not found, first / middle / last, the flags of both
  tables, n = 0 and negative); 001EDE40's generator states.
- **Bound cases** (`bound_cases`, 158, all in the default run): the compared
  value exactly at the bound and one float either side where an input
  reaches the bound exactly: 0x8239F0's y 310; the gates' z bounds -2 / 0 /
  9.5, the pin's x bounds -2 / 2, the angle 0.5235988, the phase wraps at
  +-pi (push and free swing), the free swing's amplitude 0.02 and zero
  crossings, the tail's 1.3613569; 00131740's 65; 00131B10's 34 / 21
  equalities; 001782A0's t = 0; 001AA2A0's radius and its four band ends;
  001B2D00's height equal to the probe's y; 001EDE40's floor 0.02. The
  gates' \|x\| < 7.7 cannot be reached exactly from a player position (the
  offset is a multiple of 2^-14), so it is straddled by the nearest player
  x either side. The other float compares (the 79-degree clamps, the
  target against the amplitude and against amplitude * sin(phase)) have no
  case exactly at the bound; both outcomes are run.
- **Survivor cases** (`survivor_cases`, 4, section 3.4).
- **The default run's share.** Every capture case, the designed cases a
  coverage pass picked (`DEFAULT_KEEP`, 260: with the capture cases they
  reach every word and every branch outcome the whole set reaches), all
  bound and survivor cases, and one case of every entry. The default run
  therefore proves word coverage and both outcomes of every live branch.

### 3.3 Measured

2026-10-02, M1, host shared with other lanes. Default run: 514 cases (92
capture + 422 designed: the 260 DEFAULT_KEEP picks, the 158 bound cases and
the 4 survivor cases), 868 runs (354 poisoned), 3,561 call entries compared,
847 helper register rehearsals, coverage 3,419 / 3,419 reachable non-branch
words (DEAD_WORDS left out), 307 conditional branches executed, 303 of them
both ways (the other four are the dead clamps' branches, always taken), the
header check, the callee check, the fail-stop checks and the hook contract
(1,860 native runs on 156 cases) passing; 5.1 s user + 0.7 s system CPU,
3.1 s wall with 4 workers. `EM_TEST_FULL=1`: 1,504 cases (92 capture +
1,412 designed), 2,616 runs (1,112 poisoned), 11,488 call entries compared
over all memory, 3,232 rehearsals, the same coverage and branch record, the
hook contract on 327 cases (4,161 native runs, every call and every
access); 44.3 s user + 1.4 s system CPU, 29 s wall. All pass.

### 3.4 Mutation sweep (bounded, one round)

Single-edit mutants of the four sources (the float compare macros
LT / LE swapped, add / sub, `==` / `!=`, `&&` / `||`, the integer comparison
operators, unsigned / signed byte and halfword loads, a halfword store made
a byte store and a word store a halfword store, hexadecimal constants +1 and
^4, single-digit integers +1, dropped stores), a seeded sample (seed 0x14E,
150 of the mutants), each built separately and run in its own process
through every case (EM_LEVEL14_PORT_ALL=1, contract skipped). The sweep
script is lane scratch (not committed); it is rerunnable from this
description.

- 150 mutants: 132 killed, 5 rejected by the compiler under `-Werror`, 13
  survived. Each survivor was rebuilt and run again through the final test,
  once with EM_LEVEL14_PORT_ALL=1 and once as the default run with the hook
  contract.
- **Real gaps** (2), each now pinned by a case: the free swing's
  zero-crossing test of the new phase (`0 < phase` made `<=`: no case had a
  phase of exactly 0; the bound cases `bound 825430 / 825D10 free zero`
  now start or end the step at 0), and the push's `b < a` made `<=` (the
  target exactly equal to amplitude * sin(phase); the survivor cases
  `survivor 825430 / 825D10 a == t`, whose amplitude 0x3F320F30 was found
  with the EE model so that the product is the target's bits exactly).
  Rerun: both mutants fail their new cases; the real translation passes.
- **Contract kills** (6): a `return -1` made `-2` on a fault path (0x8235A0
  once, 0x824070 three times, 0x824990, 001B4CF0). The sweep skips the
  contract; the default run's contract fails each of them.
- **Equivalent survivors** (5, argued from the code): two initial values of
  a result variable that the hook always overwrites before it is read
  (`int32_t r = 0` in 0x8247B0, `uint32_t p = 0` in 00131B10); 001EDE40's
  floor test with the constant one unit higher (only v = 0.02 itself
  changes branch, and both branches then store the same 0.02); the gates'
  `abs < 7.7` made `<=` (the offset is a multiple of 2^-14 and cannot equal
  7.7f's bits, section 3.2); 00131740's state byte +6 read signed (it is
  compared only with 0 and 1, and written back as +1 only when it is 0).

### 3.5 The reused translations

The 19 reused rows are verified by their own lanes' oracles; they are not
re-translated or re-tested here, and their status is those lanes' (section
0 names the module, the document and the test of each). How they are
reached:

- `em_player_closure_10_12_19.c`: 0016AE40 is the public state entry
  `em_player_closure1019_0016AE40` (+5 0x12); 0016ADE0, 00175390, 00181730,
  00181E20, 00182090, 00182100 and 001787B0 are static functions of that
  file, reached through `em_player_closure1019_00169730` (+5 0x10) and
  `em_player_closure1019_0016AE40`. A host binds the state entries, not
  the statics.
- `em_player_ladder_climb.c`: `em_player_ladder_climb_0017FD80`, `_0017FE00`,
  `_0017FF00`, `_00180600`, `_001809B0` (public).
- `em_aim_fire_control.c`: 0017A800 and 0017AF70 are its `pitch_to_target`
  and `track_target` (dispatched by address in that module);
  `em_aim_fire_target.c`: `em_aim_fire_target_00185E30`;
  `em_aim_fire_leaves.c`: `em_aim_fire_001B1510`.
- `em_player_major2.c`: `em_player_major2_002230A0` (+4 2 state 7).
- `em_actor_collision.c`: 001A56A0 is the static `column_round` of
  0019BC40's pass 1 (no entry of its own).

Their census ordering (the ceiling hang in a19d_01 f1873..f1928: 0016AC50,
then 0016AE40 with 0016ADE0 / 00175390 / 00181E20 / 00182090 / 00182100;
the post's ladder and the bar in a19d_12 / 13: 00181730, 001782A0 /
001787B0, then the ladder-climb rows; 002230A0 at the roof's hit) matches
the call structure those documents give. No a19d image holds the player
inside these states at its end (the images are taken after each beat's
last action), so a capture case here would only patch the record fields
their oracles already sweep.

## Binding

Nothing calls these entries. To run them live a host must:

- supply `bytes` over the scene's original-byte storage (the actor pool,
  the player block, the story bytes D_008106C0 / D_00810702.. /
  D_0081077B.. / D_008107EA.. / D_00810804, the AREA15 overlay's data (the
  scripts 0x826E70, 0x827130, 0x827400, 0x828900, 0x828A40, 0x8296F0,
  0x829330, the areas 0x826CD0 / 0x827C80, the vectors 0x828B80 /
  0x828B90, the words 0x8272B0 .. 0x8297F0 its placements store), the
  scratchpad (0x700036A0 matrix, 0x700038A0.. vectors, 0x70003A20..A3C,
  0x70003600..0x7000361C, the column table 0x700030EC..0x700031E0,
  0x70003B92), the boot tables (D_00275390, D_0028A5DC / 600 / 610 / 65C /
  6E8, D_0028A9A0 / A2, the effect records 0x257510 / 0x2575A0 / 0x257630
  and D_00275C34's generator, 0x26DE70 is not read by the translation) and
  the stack frame below `sp` for 001782A0), and point D_00275B40 at the
  running actor's bone array as the frame loop does;
- **bind the eighth / tenth / eleventh / thirteenth levels' hook slots**
  (this lane does not edit those files): `em_level8_port.h`'s w_00131650 /
  w_00131E80 / w_00131F20 / w_001339E0 / w_001B4CF0 to
  `em_level14_port_00131650` (self, ent) / `00131E80` / `00131F20` /
  `001339E0` / `001B4CF0`; `em_level10_port.h`'s w_00131F20 and
  `em_level11_port.h`'s w_001B4CF0 likewise; `em_level13_port.h`'s
  w_001B2D00 and w_001F6BA0 to `em_level14_port_001B2D00` /
  `em_level14_port_001F6BA0`;
- **bind the player modules' slots**: 001782A0 (the `grab` slots of
  `em_player_ladder_climb.h` and `em_player_misc_workers.h`, `ledge_3B` of
  `em_player_closure_0e_18.h`, the closure-10/12/19 worker, today the
  fail-stop stub `x_001782A0` of `em_player_closure_live.c`) to
  `em_level14_port_001782A0` with the original sp at the call;
  `em_player_running_jump.h`'s `target_radius` / `target_sight` to
  `em_level14_port_001AA410` / `001AA2A0` (001AA4E0 reads 0x70003A20 back
  after a nonzero 001AA2A0, which this entry writes through `bytes`);
  0016AC50 as the floor-state table's +4 1 / +5 0x11 entry (the
  `kFloorStates` / `set1` lists of `em_player.c` / `em_player_closure_live.c`
  have no 0x11 row; it hands off to 0x12, `em_player_closure1019_0016AE40`);
- **bind this module's hooks** to verified port translations where they
  exist, each by its own lane's status (not re-checked here): 00131ED0 /
  00132490 / 001328D0 / 00133A20 to `em_level8_port_*`; 001C63E0 /
  001C67E0 / 001C68C0 / 001749A0 to `em_pose_host_*`; 0011DF78 / 0011E2A8 /
  0011E748 to `em_sdk_math_original_*`; 00199DB0 / 00199FA0 to
  `em_player_ladder_*`; 0019BC40 to `em_actor_collision_column_0019BC40`;
  001AFC10 to `em_actor_pool_free_001AFC10`; 001885B0 to
  `em_area06_port_001885B0`; the others (001B10B0, 001BA1C0, 001BA540,
  001BA580, 001BA8E0, 001CA6F0, 001C5C90, 001B1EA0, 001C4760, 001EFD90,
  001EFE00, 001FBD50, 001F5940, 001F66F0, 001CFB50, 001CFBE0 ...) to the
  modules that name them by address (`em_roger_actor`, `em_director_original`,
  `em_effect_original`, `em_effects_live`, `em_effect_kinds`,
  `em_player_misc`, ...), or to fail-stop stand-ins; the seven AREA15
  hooks (0x823B40 .. 0x824C90) have no translation (this route did not run
  them);
- run the AREA15 entries as the node behaviours of their placements (sub 0:
  0x8235A0 [0], 0x825320, 0x825430, 0x825D10, and the placements that end at
  once: 0x824070, 0x824510, 0x824990, 0x824E00; sub 1: 0x823850, 0x8252D0,
  0x826600, 0x826850, and 00153950, which the overlay installs), 0x823580
  as the overlay's init, 0x826B30 / 0x827540 as the callbacks of the lift's
  script (0x826C10 starts it; `em_level13_port_00826C10`), 001EDE40 where
  its table word at 0x2554EC dispatches it.

## Known gaps

- **Live evidence.** The captures are end-of-beat states: the creatures sit
  in state 1 or 2 with the sub-states they had at the beat's end, [0], the
  gates and sub 1's placements in state 1 as captured. The scenes, the placements that
  end at once (they ran only at a19d_20 f522, in state 0), the hit effects,
  the running jump's targets and the lift's callbacks run only on designed
  states.
- **Word-assembly rows.** 00131650, 00153950, 001AA2A0 and 001AA410 were
  translated from the original's runs, not from their instruction text
  (section 0); their proof is the comparison's (every reachable word, every
  branch both ways, the bound cases), not a reading of the code.
- **Census lower bound.** The census sets are those of the route shape, not
  of the exact frames (FOURTEENTH_LEVEL_ROUTE.md section 5: most replays
  walked different paths); functions run only off this route (the seven
  AREA15 hooks, 00153B50 / 00153EA0, the other 0x823xxx sub-handlers) are
  not rows here.
- **Stubbed callees' inputs.** Only the RUN helpers are proven not to read a
  register their hook does not pass; the other hooks' argument lists were
  taken from earlier lanes' HOOKS tables and the decomp's prototypes, and
  their results are scripted beyond their real ranges.
- **Dead words** are argued from the code and located by neighbouring
  coverage (section 3.1), not executed.
- **What the harness cannot see** (as LEVEL13): whether an access that
  changed nothing is a load or a store of the same value; a load's
  signedness (only the value it produces downstream); hardware behaviour
  past a stop; the frame bookkeeping.
- **Float proofs rest on the EE model** (`em_ee_float.h`).
- **Mutation sweep** is a bounded sample, not exhaustive.
- **The reused 19** were not re-run over the a19d images (section 3.5).
