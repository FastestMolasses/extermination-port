# The ninth level: the new functions of the a13 route (lane L9T)

Lane L9T, 2026-10-01 (level side track). It covers the 44 functions the
ninth-level route census found new (decomp `build/s87/census/a13_delta.json`,
`new_functions`: 44 rows, 34,692 bytes; NINTH_LEVEL_ROUTE.md section 5): the
lift's arrival in AREA13, the lobby, door [14]'s button, the outdoor load,
door [17] and [4]'s scene, the pickup, the hatch [62] and its ladder, and the
AREA19 load at entry 9 (`a13_exit`). 42 had no verified port translation and
are translated here; two (001E7C60 and 00214570) have verified translations
in other modules and are reused, and this lane re-runs them against the
original over the ninth-level captures (section 3.4).

Every translation is compared with the original instructions by
`tools/test_level9_port_reference.py`. What that comparison covers is exactly
what section 3 states: the comparisons the test makes, on the cases it runs.
Nothing is bound: no port code calls these entries yet.

| File | What |
|---|---|
| `src/game/em_level9_port.h` | public API: 42 entries, the hook table (137 hooks and the indirect-call callback), fault codes |
| `src/game/em_level9_port_internal.h` | memory view, fault latch, EE float helpers, one typed wrapper per hook (written from the test's HOOKS table; see section 1, Hooks), the internal entry points |
| `src/game/em_level9_port_area13.c` | AREA13 0x823700, 0x823940, 0x823A10, 0x823BC0, 0x823C10, 0x823D50, 0x823E90, 0x823FE0, 0x824180, 0x8266A0, 0x826850, 0x826FB0, 0x826FC0, 0x826FF0, 0x827150, 0x8292A0, 0x8293A0, 0x8299E0, 0x829AA0 |
| `src/game/em_level9_port_turret.c` | AREA13 0x824BB0 (the outdoor watchers) and its probe 0x826140 |
| `src/game/em_level9_port_exit.c` | 001383C0, 00138540, 001386E0, 00154460, 001546C0, 00154740, 001549C0, 0015A200, 00183440, 001838B0, 00196970, 00196CE0 |
| `src/game/em_level9_port_anim.c` | 001BA7F0, 001BDCA0, 001BDD70, 001C06E0, 001C1030, 001C4BA0 |
| `src/game/em_level9_port_fx.c` | 001DE920, 001E5AC0, 001E7050 |
| `tools/test_level9_port_reference.py` | original-instruction oracle and comparison; the reuse checks |

Addresses are runtime addresses. The AREA13 overlay is linked 0x40 below
where it runs (0x823700 is `func_overlay_AREA13_008236C0`). Names such as
`bone_root_pulse` (001C06E0) or `bone_wobble_decay_1` (001BDCA0) are decomp
labels, not evidence; the behaviour below is read from the code.

## 0. Scope: the census rows, and what already existed

A grep of the port (`src/`, `tools/`, `docs/`) for each address before this
lane found these; "hook only" means a worker slot, a call site or a stub, not
a translation; "legacy" means old port code that names the address in
comments (pre-2026-09-23 claims, never compared with the original).

| Row | Bytes | Decomp | First (census) | Port before this lane | Here |
|---|---:|---|---|---|---|
| AREA13 0x823700, 0x823BC0, 0x823C10, 0x823D50, 0x823E90, 0x824180, 0x824BB0, 0x8266A0, 0x826850, 0x826FF0, 0x827150, 0x8292A0, 0x8293A0, 0x8299E0 | 300, 76, 312, 312, 328, 520, 5,520, 428, 1,888, 344, 2,780, 248, 1,596, 184 | C (byte-identical since 83580f9) | a13_00 f1 | none | translated |
| AREA13 0x829AA0 | 504 | NEARMISS | a13_00 f561 | none | translated (instructions) |
| 001BA7F0 | 236 | BM | a13_00 f572 | hook only (`em_roger_actor_original.c` w_001BA7F0) | translated |
| AREA13 0x823940 | 208 | C | a13_00 f6996 | none | translated |
| 001BDCA0, 001BDD70 | 208, 228 | BM | a13_01 f398, f524 | hook only (`em_level8_port_lift.c`, 001BDE60's hooks) | translated |
| 001DE920 | 1,160 | NM | a13_01 f521 | hook only (`em_render_context.c` w_001DE920) | translated (instructions) |
| 001E5AC0 | 3,328 | NM | a13_01 f521 | hook only (`em_weather.h` draw kind 2) | translated (instructions) |
| 001E7050 | 692 | NM | a13_01 f521 | none (a comment in `em_area01_render_gs.h`) | translated (instructions) |
| AREA13 0x823FE0 | 124 | C | a13_01 f521 | hook only (`em_camera_area11_specials.c` w_00823FE0, the call from 00195130's area-0xD case; CAMERA_AREA11_SPECIALS.md) | translated |
| AREA13 0x826140 | 1,232 | C | a13_01 f521 | none | translated |
| AREA13 0x823A10 | 36 | C | a13_02 f927 | none | translated |
| 00214570 | 1,144 | NM | a13_02 f956 | `em_status_pages_00214570` (`em_status_pages_item.c`, STATUS_PAGES.md, oracle-verified) | **reused**, re-checked |
| AREA13 0x826FC0, 0x826FB0 | 44, 16 | C | a13_04 f602, f761 | none | translated |
| 001838B0 | 92 | BM | a13_05 f114 | stub (`em_player_stage_live.c` stub_001838B0, unbound) | translated |
| 001383C0 | 376 | NM | a13_05 f484 (a13_exit) | none | translated (instructions) |
| 00138540, 001386E0 | 408, 532 | BM | a13_05 f484 | none (a comment in `em_weapon.h`) | translated |
| 00154460, 001546C0, 00154740, 001549C0 | 324, 124, 628, 1,344 | BM | a13_05 f484 | legacy (`em_enemy.c` "tendril field": unverified) | translated |
| 0015A200 | 184 | BM | a13_05 f484 | hook only (`em_area01_sys.c` CALL3) and legacy (`em_enemy.c`) | translated |
| 00183440 | 148 | AI | a13_05 f485 | stub (`em_player_stage_live.c` stub_00183440, unbound) | translated (instructions) |
| 00196970 | 880 | BM | a13_05 f485 | none | translated |
| 00196CE0 | 1,712 | NM | a13_05 f485 | hook only (`em_camera_leftovers.c` w_00196CE0) | translated (instructions) |
| 001C06E0, 001C1030 | 2,380, 1,232 | NM | a13_05 f484 | none | translated (instructions) |
| 001C4BA0 | 264 | AW | a13_05 f484 | none | translated (instructions) |
| 001E7C60 | 68 | BM | a13_05 f484 | `em_area01_sys_001E7C60` (`em_area01_sys.c`, AREA01_SYS.md, oracle-verified) | **reused**, re-checked |

Status abbreviations as in SECOND_LEVEL_ROUTE.md section 6 (BM byte-matched
C, NM NEARMISS, AW word assembly, AI inline asm, C byte-identical overlay C).
The census lists 0x829AA0 as NEARMISS and the other 20 AREA13 rows as overlay
C; the decomp's AREA13 C is byte-identical except 0x827F90 (not a census row)
and 0x829AA0.

Ground truth. Every function was translated from the original instructions
(read locally; nothing reproduced), with the decomp's C as a guide where it
is byte-identical; the order of loads and stores between calls, the float
operand order (mula / madd, add / mul, the operand of every compare) and
every branch follow the instructions. Where the decomp's text and the
instructions disagree the instructions win:

- **00196CE0** (NEARMISS): 001916C0 gets (self, other, 0): a0 is the camera
  record unchanged from the entry and the 0 is in a2 (the text passes 0 as
  the first argument); 001916C0's own code takes (camera, player, mode). Its
  "outside the second window and region 5" test is !(d < 225) and its state-2
  step-back test !(y < height) (NaN-exact forms of the text's >= tests).
- **001C1030** (NEARMISS): state 2 sub-state 0 calls 001C67E0(self, 0, 0,
  0): a0 is the actor (never changed on that path) and a1 is 0 (the text
  writes `anim_clip_init(0, 2, ...)`).
- **001C06E0** (NEARMISS): after the hit-class logic the halfword +0x36 is
  read again on every path before its low 12 bits are subtracted.
- **00183440** (inline asm: the decomp's asm body uses mnemonics, with
  `.word` for its branches; translated from the instructions): +0x23F = 2 and +0x24C = 1
  are stored before the call to 001662D0; +0x268 + 1.0 is stored whether or
  not it reaches 4.0.
- **001C4BA0** (word asm): the state byte +4 is read first, then the parent
  (+0x18); on the +3 == 4 path no 001C6380 is called.
- **0x829AA0** (NEARMISS): see section 2; the instructions' order was taken
  for the packet stores.

## 1. Interface (the LEVEL8_PORT / AREA06_PORT design)

The design is `em_level8_port.h`'s (docs/LEVEL8_PORT.md section 1) with:

- **Entries.** 42, named `em_level9_port_<address>`, with the original's
  argument registers in order; entries with a result write the original's v0
  to `*result` (0x823A10, 0x823FE0, 0x826140, 0x826FB0, 0x826FC0, 00154460,
  0015A200, 001BDCA0, 001BDD70). 001E5AC0's fourth argument is its f12 (the
  frame delta) as a float.
- **Stack frames.** Entries whose original (or a direct callee's) frame
  holds locals take `sp`, the original stack pointer at entry, and address
  the frame at the original's offsets: 0x824BB0 (it passes sp - 0x60 to
  0x826140), 0x826140 (dir at sp - 0x20, tint at sp - 0x10), 001DE920 (the
  frame sp - 0x140: 001D6C90's seven stack arguments as doublewords at
  +0x00..+0x30, the saved packet start at +0xF0, the vector at +0x100, the
  basis copy at +0x110), 001E5AC0 (the strip descriptor at sp - 0x60),
  001E7050 (the descriptor at sp - 0x60).
- **Hooks.** 137 callee hooks named by original address (boot functions and
  the AREA13 overlay's own 0x823830, 0x824160, 0x824390, 0x824520, 0x824960
  and 0x826610 at their runtime addresses). The header's struct (between
  its GENERATED HOOKS markers) and the wrappers in the internal header
  (GENERATED WRAPPERS) were written from the test's HOOKS list by a lane
  script that is not committed; the test's `header_checks` compares them
  with HOOKS on every run (names and order, argument and result types, one
  wrapper per hook that calls it and latches its address), so an edit of
  one side alone fails the test. Plus
  `w_callback(ctx, function, a0)` for every indirect call through a function
  word read from memory: the actor's +0x4C method and 0x827150's step table
  0x82D190. Float hook arguments and results cross as floats whose bits are
  the original's; inside the module floats are carried as bit patterns.
- **Calls between translations** are direct: 0x823700 -> 0x823940;
  0x823BC0 -> 0x823C10 / 0x823D50; 0x823E90 -> 0x824180; 0x824BB0 ->
  0x826140; 001383C0 -> 00138540 / 001386E0; 001546C0 -> 00154740 /
  001549C0; 001549C0 -> 00154460; 00196CE0 -> 00196970.
- **Floats.** EE arithmetic on bit patterns through `em_ee_float.h` (add /
  sub / mul / div, MULA and MADD with the accumulator, CVT.S.W, NEG, the
  compare keys); arithmetic shifts are `l9_sra`, the EE's div `l9_div` /
  `l9_rem`.
- **Fail-stop.** As LEVEL8: fault 5 (unmapped or misaligned address, or
  `bytes` NULL), 1 (reached NULL hook or NULL `w_callback`), 2 (a hook
  returned < 0), and **7** (`EM_LEVEL9_PORT_FAULT_REGISTER`): the original
  would use a value its own code leaves undefined. The one such path is
  001E7050 dividing by its source's bone count +0x0C when that is 0 (the EE's
  div leaves HI as it was and the original stores HI as the slot's bone
  index): the translation faults right after reading the count (section
  3.1). After a fault no hook runs, `bytes` is not called, writes are
  dropped, the first fault is kept, no result is written and the entry
  returns -1; a fault latched on entry, a NULL hook table, a NULL fault
  pointer and (entries with a result) a NULL result pointer return -1 at
  once.

## 2. What each function does (behaviour, from the code)

player = the player block 0x8102B0; CAM = the camera record 0x8101E0;
blk = self +0x1F0. The header comment of each function in the sources gives
the field-level behaviour; in short:

**AREA13's placements (overlay id 10).**

| Entry | Behaviour |
|---|---|
| 0x823700 ([3], the lobby's scene owner) | init (001B10B0 model 0x62, 001C63E0, 001CA6F0, 001BA8E0, descriptor 0x82A760, +0x58 = D_0028A61C); state 1: flag 0x1A set (001BA1C0) -> free; else by counter 0x19 (D_008107F1): 0 -> 0x823830 (its scene, a hook), 1 -> 0x823940 |
| 0x823940 | the talk: Use (+0x0B bit 2) starts script 0x82A620; when it ends +0x0B = +5 = 0 and the clip 20; every frame 001BA580, 001C64F0, 001B17A0, 001C68C0, the +0x4C method |
| 0x823A10 | an op09 record of [4]'s script 0x82A770: 001C47A0(0x1A, 1) (item 0x1A); returns 1 |
| 0x823BC0 / 0x823C10 / 0x823D50 (the holes [5] / [6]) | flag 0x1B set -> free; descriptor 0x82B070 / 0x82B080; once item 0x27 (D_00810C8B) is held -> free; else the Use starts the examine script 0x82AB70 / 0x82ADF0, its end 001AEE10(4, 0) |
| 0x823E90 ([44], the machine) | flag 0x1C set -> free; init (001B0FD0, descriptor 0x2759B8, +0x34 = 4); state 1 by D_008107F4 & 0xF: 0x824160 / 0x824180 / 0x824390 / 0x824520 / 0x824960 (all but 0x824180 are hooks) |
| 0x824180 ([44]'s step 1) | the seven sub-states of the table 0x82E200 (scripts 0x82B090 / 0x82B2D0, D_00810774 = 1, the area request D_008106B0 / B1 / D0, D_008107F4 += 1); every frame the light point (-1.333, 1.9, -2.6) through +0xD0 to 001F5940(4, ..), 001C6380, 001B17A0, the +0x4C method |
| 0x823FE0 (called by boot 00195130 at 0x195D18 in its area-0xD case while D_00810702 >= 8; it takes no argument) | 1 when flag 0x1C != 0xFF, 001B1EA0(0, player position, 0x82E1C0, 4) and the player's y above 210 |
| 0x824BB0 / 0x826140 (the four outdoor watchers, deferred group 0x829D00) | the AREA01 0x826D40 / 0x8282F0 twins with the differences at the top of `em_level9_port_turret.c` (no state 0x64; states 4 and 1 idle while D_00810702 < 8; state 2 untested; the miss effect 0x8000002C when the hit polygon's +0x1A was 5) |
| 0x8266A0 (the watchers' partners) | the AREA01 0x828850 twin plus the D_00810702 < 8 wait: a hit (+0x36) kills the partner (0x80000045, sounds 0x426 / 0x427 at the 10-frame count) |
| 0x826850 (the hatches [62] / [63]) | offered for Use (+2 = 0x84) while item 0x27 is held; north (z > 1000) / south descriptors 0x82CDD0 / 0x82CDF0 and counter 0x61 bits 0 / 1; already open: model 0x0D and state 2; the Use writes the ladder points 0x82CAB0 / C0 / 0x82CB00, script 0x82CA50, the record 0x82CA00 at count 4, and at its end the open model (001C6120(D_0028A59C, 0x0D)), counter 0x61 \|= bit; four corner lights (001F4BF0) each frame; state 2: visibility 001B1630, the +0x4C method |
| 0x826FB0 / 0x826FC0 | op09 records of the hatch's script: a0 +0x2E = 0xFF / 001FBD50(self, 0x40D, 0, 300); return 1 |
| 0x826FF0 ([58] / [59]) | init (descriptor 0x2759C0); the Use starts script 0x82CE10 (player x < 800) or 0x82CFD0 |
| 0x827150 ([45] / [46], the machine's panel) | D_00275CA8 = blk each frame; init (model 0x12 from the placement record when D_00810774 == 0xFF); +0x0D 0x11: the five sub-states (the 0x82D190 step table through `w_callback`), the markers (model 0x3F5, sprites 001F4E20, the blinking first marker with sound 0x8D2 once) and while the step is below 4 the sparks 001EFD20(5, ..) every 16 frames and twelve sprites; +0x0D 0x12: drawn while D_00810774 == 0xFF |
| 0x8292A0 ([49]..[54]) | free once D_008107F4 bit 5; state 0x64 draws, counts +0x28 down while bit 5 is set |
| 0x8293A0 ([47] / [48]) | the placement record's second pose while D_008107F4 bit 6 is clear (0019C6F0(0x1F / 0x20)); after 0x6E0 frames with bit 6 the first pose (001AF800, 001CB5B0) |
| 0x8299E0 ([11]) | 0015AC00 init; 0015AE20 while D_00810774 == 0 |
| 0x829AA0 (no jal caller; its address is a behaviour pointer in a boot .data table of 0x30-byte records, the word at 0x25973C; ran live at a13_00 f561 per the census) | a 24-frame fading full-screen GS packet (001CB5F0(0x7635C0, 0xFFE000, 6)), colour 001281C0(224 n / 24) x3 and 001281C0(128) |

**The outdoor load and the lift door.** 001DE920: the packet builder of the
outdoor weather (two batches of 16 groups of 6 particles, VU0 program and GS
state through 001D6B10 / 001D2040 / 001D6BA0 / 001D1FF0 / 001D6C90 /
001D1F20, closed by 001CB760). 001E5AC0: two banks of three billboard strips
around the camera (texture offsets from the camera point, 001CFAE0 /
001CFFE0, the 37 n + 11 seed), and the fade-in quad 001E6F60 once the timer
+0x220 is above 75. 001E7050: an emitter on a source actor (+0x24; the
player in a13_01): eight bone slots, 001CD070 / 001CD2B0 / 001CFAE0 /
001CFBE0, the slot phase stepping 0.2 past 1.4. 001BDCA0 / 001BDD70: the
lift door's wobble open / close over the 16 bone records of D_00275B40.
001BA7F0: a probe from the actor's +0x114 body 20 units down, 001F9100 on a
hit.

**The AREA19 load (a13_exit).** 001383C0 / 00138540 / 001386E0: AREA19's
creature (three at the arrival): the mode gate 0x70003B8D, the spawn (models
0x7B / 0x79 by bit 7 of +0x0D, the 1.3 scale, hit points by D_0081070A), the
live frame (the six sub-states of 0x26D1C0 are hooks, the timers, 001B4810
on ent +0x84, the hit, the step sound 0x821 + n % 5). 00154460 / 001546C0 /
00154740 / 001549C0 / 0015A200: the kind-0xE field (six at the arrival):
the trigger box (3 x the pad footprint, 3 + its height), the room tint from
D_00246800, the 12 tendrils (SCAN, DEPLOY, HOLD, RETRACT; 001545B0, sound
0x42D), the spawn from a generator pad. 00183440 / 001838B0: two player
stage workers (00183440 counts +0x268 to 4 then releases the mode byte and
sets the stage +4 = 1, +5 = 0x0C; 001838B0 starts the 8.0 blend). 00196970:
AREA19's five ladder circles (region index, ceiling, the climb request
001B0C60). 00196CE0: the ladder camera (states 0..4, the two windows, the
orbit by 001B12B0, 00192010 / 0018D7B0 / 0018C6A0 / 0018C4B0, the reset by
+0x230). 001C06E0: an AREA19 creature on a root bone (spawn; chase with
001BE5F0, the aim jitter, the 2-degree ease; the probe 0019A570; the attack
script over +0x2A with the strike 0x8000005A; the hit pool; dying). 001C1030:
an actor knocked back when hit (the reach probes into +0x60 / +0x64, the
20-frame sway, the 0x442 sound and the free). 001C4BA0: a part following its
parent's matrix.

## 3. Verification

`python3 tools/test_level9_port_reference.py` (port root, macOS arm64 or
Linux; no make target: the Makefile belongs to another chain while this side
track runs). It compiles the five sources into
`build/level9/port/level9_port.dylib` with `-std=c11 -Wall -Wextra -Werror
-Wpedantic -ffp-contract=off` (the reuse checks build into
`build/level9/port/reuse`; all of `build/` is ignored, and `build/level9`
can be removed after a run). At most 4 worker processes (EM_TEST_JOBS
overrides). `EM_LEVEL9_PORT_ONLY=<label prefix>` runs a subset (no coverage,
fail-stop, contract, leftover or reuse checks); `EM_LEVEL9_PORT_MISSING=1`
lists unexecuted words; `EM_LEVEL9_PORT_SOURCE=<dir>` tests other copies of
the sources (the mutation sweep); `EM_LEVEL9_PORT_NOCONTRACT=1` skips the
hook contract (debugging only).

### 3.1 Oracle and harness

The LEVEL8 harness (docs/LEVEL8_PORT.md section 3.1, itself AREA06's /
AREA22's / AREA04's), copied and owned here, with:

- **Images.** The 7 recorded ninth-level RAM images: a04b_04 (the lift's
  arrival in AREA13) and the ends of a13_00..05. AREA13 (id 10) is resident
  in a04b_04 and a13_00..04, AREA19 (16) in a13_05. Before any case the test
  asserts that each image's overlay text equals the user's
  `extract/OVERLAY/<AREA>.BIN`, that the boot text equals the pinned ELF,
  that the boot jump tables (001386E0's 0x26D1C0, 001C06E0's 0x26E280)
  equal the ELF and AREA13's 0x82E200 (0x824180) its file, and that the
  designed-record area 0x1C00000 is zero; the table loads are the only
  accesses left out of the comparison (the switches encode them).
- **D_00275B40.** In the game the frame loop points D_00275B40 at the
  running actor's bone array (actor +0x110) before its behaviour runs; the
  cases of the entries that read it (0x824BB0, 0x826140, 001BDCA0, 001BDD70)
  set it so (the captured value is whatever the last actor left).
- **Stack.** The window is [STACK_TOP - 0x800, STACK_TOP) (001DE920's
  frame, 0x140, is the deepest).
- **Callees run as original code** (RUN): 001026A0, 00102738, 00102760,
  00102798, 001028B8, 001028D0, 00102900, 00102918, 00102948, 00102958,
  001029C0, 00102A60, 00102B08, 00102BB0, 001031E0, 00103230, 0011DBB8,
  0011DE90, 0011DF78, 0011E2A8, 0011E520, 0011E748, 001281C0, 00128250,
  001B1240, 001B12B0, 001B1470, 001C6160 (each first rehearsed with the
  argument registers its hook does not pass poisoned). The oracle core gained
  pextlw / pextuw (00102798's transpose; test_area01_math_reference's
  model). Every other callee is stubbed with scripted results (a Scribble
  writes what the callee would, e.g. 0019A570's hit point, polygon, object
  and kind at 0x700031B0..D8).
- **Compared, per case**: as LEVEL8 (the calls and their arguments; memory at
  every call entry; the memory accesses between calls one for one, in order,
  by address, size and changed-or-not; all memory after the last store; the
  return value; the store-log self-check; stops at unmapped or misaligned
  original accesses; every case again from a poisoned start image; coverage
  of every reachable original word of all 42 entries; the table's ctx at
  every call).
- **Header**: `header_checks` (every run, also with
  `EM_LEVEL9_PORT_ONLY`): the C hook struct and the wrappers against HOOKS
  (section 1, Hooks).
- **Fail-stop**: `fault_checks` (0x823940 on [3] with the Use bit: NULL
  hook, failing hook, an unmapped address after one call; 001BDCA0 for an
  unmapped address before any call; a latched fault; every entry with a
  latched fault, a NULL hook table, a NULL fault pointer and, for the
  entries with a result, a NULL result pointer), `hook_contract_site` (on
  passing cases that together reach all 137 hooks, the callback and all 42
  entries: calls failing, accesses refused, each hook NULL / INT32_MIN / 1 /
  INT32_MAX, `bytes` NULL; EM_TEST_FULL=1 every call and every access of
  each case, the default run an even sample of at most 8 calls and 16
  accesses per case) and `leftover_checks` (001E7050's zero divisor in
  states 0 and 1: fault 7 at 0x1E7050 after exactly the original's calls and
  accesses up to the count's load, nothing after).
- **Dead words.** 37 reachable words no input can reach (`DEAD_WORDS`, with
  the reason in the test): the unsigned-int-to-float fix-up after a bltz on
  a byte loaded zero-extended (00154740's four tint bytes, 001DE920's
  D_008106BF, 0x829AA0's +5: 36 words), and 0x824BB0's elevation step's
  "store the target angle" branch (it needs the angle both <= cur - k and >=
  cur + k, k = 0.011635528; with the EE's truncating add / sub cur - k < cur
  + k for every finite cur, and the angle is an atan result: 1 word).

### 3.2 Cases

- **Capture**: every entry on the captured state of every image it applies
  to (the AREA13 owners in a04b_04 and a13_00..04: [3], the holes, [44], the
  watchers and partners, the hatches, [58] / [59], the panel, [47], [49]..,
  [11]; the creature, the fields, 001C06E0 / 001C1030 / 001C4BA0 and the
  generator pads in a13_05; the emitter and the weather in a13_01; the
  player workers and the camera in every image): 284 cases.
- **Designed**: every state and sub-state of every function with each
  callee result on both sides of its test; the AREA13 flags and items
  (D_00810774, D_008107F1, D_008107F4 bits and step, D_00810702, D_00810839,
  D_00810C8B); the float bounds of each compare (210, 800, 1000, the
  regions' radius 8 and heights, the windows 225 / 241 / 294 / 229 / 266,
  the watchers' 1.134464 and -0.5235988 limits, 25 / 2.5 k, 4.0, 1.4, 75);
  the watchers' tracked record placed around record A at designed offsets
  (and with A's +0xB0 / +0xB8 zeroed so the heading comes from the atan),
  their probe's three outcomes and both draws, the period effect's hit kinds
  and attributes; the panel's step / count / markers / blink / latch
  states; the creature's mode, state, timers, hit and sound draws; the
  field's player offsets on each axis; the camera's regions, windows and
  +0x230 codes; 001C06E0's chase, ease, probe, attack steps, hit classes and
  pool; 001E5AC0's flags, seeds, frame deltas, timers and camera points;
  001E7050's source state, handle, count and phases: 2,345 designed cases
  (with the survivor cases).
- **The default run's share.** Every capture case, the designed cases a
  coverage pass picked (`DEFAULT_KEEP`: with the capture cases they reach
  every word the whole set reaches, greedy by cost) and the survivor cases.
  EM_TEST_FULL=1 runs everything.
- **Survivor cases** (`survivor_cases`, default run), added because the
  mutation sweep (section 3.5) showed the set above missed them: 0x823700's
  +0x0D above 0x7F (zero-extended into 001B10B0 / 001BA8E0); 001386E0's
  sound index with draws that tell (r >> 17) from (r >> 18); 00138540 with
  bit 7 of +0x0D alone deciding; 0x8292A0 with D_008107F4 bits other than
  bit 5 set; 0x826850 with z exactly 1000 (the south hatch) in its three
  paths; 001C06E0's +0x2A landing exactly on 0x1000 (attack step 5, dying);
  00196CE0 state 2 with the player exactly at the region's height; 0x824BB0
  with record A's yaw exactly at 1.134464 (sweep, track, idle). Fix round
  (the review's four named survivors, each pinned): 0x824BB0's sweep with
  record A's yaw exactly at the lower limit -1.134464 (bits 0xBF91361E;
  states 4 and 1: the strict test leaves +0x210 at +0.0); 0x826140 with the
  hit-object kind 0x10 and 0x13 (the ends of the 0x10..0x13 range that
  counts as hit 1); 001549C0 state 2 with the player 3.0 above and below
  the field (inside the vertical limit 3 + h, outside 3 - h); 001E7050
  state 1 with every slot phase 0x3F99999A, which the 0.2 step takes
  exactly to 1.4 (0x3FB33333) under the EE model, not wrapped by the
  original's c.le.s.

### 3.3 Measured

2026-10-01 (fix round), M1, host shared with other lanes. Default run: 585
cases (284 capture + 301 designed: the coverage picks and the survivor
cases), 953 runs (368 poisoned), 8,119 call entries compared, 3,649 helper
register rehearsals, coverage 6,983 / 6,983 reachable words (the 37 dead
words of section 3.1 excluded), the header check, the fail-stop checks, the
hook contract (1,767 native runs on 107 cases), the undefined-value checks
and the reuse smoke samples passing; 21.0 s CPU, 17.1 s wall with 4
workers (load average about 9). `EM_TEST_FULL=1`: 2,629 cases (284 capture
+ 2,345 designed), 4,744 runs (2,115 poisoned), 119,310 call entries
compared over all memory, 82,271 rehearsals, the hook contract on 234 cases
(18,377 native runs, every call and every access), every reuse check in
full (001E7C60 12 cases, 00214570 164 cases); 2,767 s CPU, 2,421 s wall
(one worker runs the contract of the longest sites, 001DE920's and
001E5AC0's, for most of it). All pass.

### 3.4 Reuse checks (the existing translations over the ninth-level captures)

Each check runs the existing translation against the original with that
module's own harness (imported, not modified; libraries built into
`build/level9/port/reuse` where the harness takes an output folder).
EM_TEST_FULL=1 runs them in full; the default run keeps a smoke sample.

- **001E7C60** (`em_area01_sys_001E7C60`, the harness of
  `tools/test_area01_sys_reference.py`, `run_case`, its route folder pointed
  at route_a13): the level fill of the three AREA19 grids that 001E7D20 owns
  at the a13_05 arrival (s = D_00275C20 + byte +0x0D * 0xA060) with the
  heights 132, 160, 131.9 and -0: 12 cases (default: 1).
- **00214570** (`em_status_pages_00214570`, the harness of
  `tools/test_status_pages_reference.py`, `item_case`): the harness's own
  EQUIPMENT page inputs over the a13_02 image (the item 0x1A page of [4]'s
  scene) and the a13_03 image (after item 0x27): 164 cases (default: 21).

### 3.5 Mutation sweep (bounded, one round)

Single-edit mutants of the five sources (comparison operators, the float
compare macros, add / sub, signed / unsigned loads, a halfword store made a
byte store, hexadecimal constants +1 and ^4, small integers +1, dropped
stores), a seeded sample (seed 0xA9, 150 mutants, plus one trial mutant) run
through the default test (contract skipped) with `EM_LEVEL9_PORT_SOURCE`.
The sweep script is lane scratch (not committed); it is rerunnable from this
description.

- 151 mutants: 127 killed, 8 rejected by the compiler (a dropped store left
  a variable unused under `-Werror`, or a signed load compared with an
  unsigned constant), 16 survived.
- **Real gaps** (10): 0x823700's +0x0D signedness, 001386E0's sound shift,
  00138540's bit-7 mask, 0x8292A0's bit-5 mask, 0x826850's z == 1000 record
  test, 001C06E0's two `< 0x1000` clamps, 00196CE0's state-2 height compare,
  0x824BB0's two yaw clamps (sweep and track). The survivor cases (section
  3.2) kill all ten (each mutant rerun on the extended default set).
- **Equivalent survivors** (6, argued from the code): a signed instead of an
  unsigned halfword load where only the low 16 bits of the result are stored
  (0x824BB0's +0x2A - 4, 0x8292A0's +0x28 - 1); a signed byte load of
  D_008107F4 whose only use is the byte store of f | 0x40 (0x827150); a
  signed byte switch whose case labels are all below 0x80 (0x824BB0's hit
  attribute, 0x826850's +5: a byte >= 0x80 takes the default either way);
  and a mutated digit inside a comment (001E5AC0).

- **Review sweep** (one bounded round by the reviewer, seed 0x9E, 44
  mutants stratified by operator, default set, contract and reuse
  skipped): 26 killed, 3 rejected by the compiler, 15 survived. Four
  changed behaviour (0x824BB0's lower yaw clamp `<` made `<=`; 001549C0's
  vertical limit 3 + h made 3 - h; 0x826140's kind >= 0x10 made > 0x10;
  001E7050's phase wrap !(ph <= 1.4) made !(ph < 1.4), the last killed by
  EM_TEST_FULL=1 but not by the default set). The fix round pins each with
  a survivor case (section 3.2) and reran the reviewer's four mutant
  sources over the survivor cases: each now fails on its pinned case(s)
  (RAM at a call entry differs), and the lane's sources pass. The other 11
  are equivalent (the reviewer's written argument; not re-derived in the
  fix round): low-byte or halfword
  stores of a signed instead of an unsigned load (six), a nonzero test
  (two), a switch or compare whose labels are all below 0x80 (two), and an
  operand that only ever takes the values -1, 3 or 4 (one).

## Binding

Nothing calls these entries. To run them live a host must:

- supply `bytes` over the scene's original-byte storage (the actor pool,
  the player and camera blocks, the AREA13 overlay's data (the scripts, the
  descriptors, the hatches' ladder points 0x82CAB0.., the panel's tables
  0x82D190..), the placement tables D_0024D7C0, the bone arrays, the probe's
  hit lists in the scratchpad, the packet context D_00275670 and its buffer,
  the data tables named in section 2, the stack frame below `sp`), and point
  D_00275B40 at the running actor's bone array as the frame loop does;
- run the AREA13 owners as node behaviours (+0x10): 0x823700, 0x823BC0,
  0x823E90, 0x824BB0, 0x8266A0, 0x826850, 0x826FF0, 0x827150, 0x8292A0,
  0x8293A0, 0x8299E0; 0x829AA0 from the path that reads the boot .data
  table holding its address (the 0x30-byte records around 0x25973C, which
  also hold AREA13's 0x828500, 0x828C60, 0x828E10 and 0x828F40; no jal
  reaches it; the code that reads that table is not identified yet);
  0x823FE0 from `em_camera_area11_specials.c`'s w_00823FE0 (00195130's
  area-0xD case, D_00810702 >= 8; that hook passes the camera record, which
  0x823FE0 does not read: the entry takes no argument);
  0x823A10 / 0x826FB0 / 0x826FC0 from the script host's op09 records
  (scripts 0x82A770 and 0x82CA50); 001383C0, 001546C0, 001C06E0, 001C1030,
  001C4BA0 and 001E7050 as node behaviours; 0015A200 from 0015A2C0
  (`em_area01_sys.c` CALL3 0x0015A200); 001BDCA0 / 001BDD70 from
  `em_level8_port_001BDE60`'s hooks w_001BDCA0 / w_001BDD70; 001BA7F0 from
  `em_roger_actor_original.c`'s w_001BA7F0 (001BA580 category 6 in area
  0xD); 00196CE0 from `em_camera_leftovers.c`'s w_00196CE0 (0018BC20
  action 13); 001DE920 from `em_render_context.c`'s w_001DE920 (001DDAA0's
  area-key dispatch); 001E5AC0 from 001E55F0 (`em_weather.h` draw kind 2);
  00183440 / 001838B0 in place of `em_player_stage_live.c`'s stub_00183440 /
  stub_001838B0 (0015B610 +5 = 3, 0015B530 +5 = 0xC); the reused 001E7C60
  and 00214570 are already bound where their modules are;
- bind each hook to a verified port translation or a fail-stop stand-in;
  the AREA13 hooks 0x823830 ([3]'s scene), 0x824160 / 0x824390 / 0x824520 /
  0x824960 ([44]'s other steps), 0x826610 and the step functions behind
  0x82D190 have no translation yet (the census did not see them run on this
  route; NINTH_LEVEL_ROUTE.md section 5 lists them); the stubbed callees
  (every hook not in the RUN list above) are not verified here, and the
  helpers run as original code need port translations paired by address.

## Known gaps

- **Live evidence.** The captures are end-of-beat states. Most paths are
  designed or random cases on the captured RAM with state bytes patched:
  the hatches' Use and opening, [44]'s steps, the panel's sub-states 1..4,
  the watchers' tracking and period effect, the creature's and field's live
  sub-states, 001C06E0's attack and death, 001C1030's knock-back, 001E5AC0
  and 001DE920 beyond their captured inputs.
- **0x829AA0** has no live node in the end-of-beat images (a free record
  was used) and no jal caller; it is referenced from a boot .data table
  (section 2) and ran live at a13_00 f561 (census). Its cases are designed
  on that free record. **0x823FE0** is called by 00195130 (section 2); its
  capture cases run it on each image's own flags and player position, the
  designed cases on both sides of each test.
- **Stubbed callees' inputs.** Only the RUN helpers are proven not to read a
  register their hook does not pass; the other hooks' argument lists were
  read from the callees' code and the call sites, and their results are
  scripted beyond their real ranges.
- **Undefined value.** 001E7050 with a source whose bone count is 0 faults 7
  where the original stores a leftover HI; whether any live source reaches
  it is not established (the player has a nonzero count in a13_01).
- **What the harness cannot see** (as LEVEL8): whether an access that
  changed nothing is a load or a store of the same value; hardware behaviour
  past a stop; the frame bookkeeping.
- **Float proofs rest on the EE model** (`em_ee_float.h`).
- **Default run time.** 21 s CPU, 17 s wall with 4 workers in the fix
  round (load average about 9; the reviewer measured 44.6 s CPU at load
  17-27; the lane's first run 17.7 s), above
  the ~10 s CPU guideline; the default contract samples calls and accesses
  per case (the full sweep, every call and every access, runs with
  EM_TEST_FULL=1), and the case share is already the minimal coverage set.
- **Mutation sweep** is a bounded sample (the lane's one round, 151
  mutants, plus the review's 44), not exhaustive.
- **Reuse checks** use the other harnesses' own models and case lists.
