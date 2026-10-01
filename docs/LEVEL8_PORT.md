# The eighth level: the new functions of the a06b / a01v / a04b route (lane E8T)

Lane E8T, 2026-09-30 (level side track). It covers the 53 functions the
eighth-level route census found new (decomp `build/s87/census/`
`a06b_delta.json`, `a01v_delta.json`, `a04b_delta.json`, `new_functions`:
5, 3 and 45 rows; `a22b_delta.json` lists none; EIGHTH_LEVEL_ROUTE.md
section 5). 51 had no verified port translation and are translated here;
two (AREA04 0x824830 and 00215870) have verified translations in other
modules and are reused, and this lane re-runs them against the original over
the eighth-level captures (section 3.4). The task also names the lift button
001BC960 and the lift 001BD560: they are not rows of these deltas (both ran on
earlier levels) and have verified translations in `em_area02_math.c`; section
3.4 re-runs those over the a04b captures and the AREA13 arrival too.

Every translation is compared with the original instructions by
`tools/test_level8_port_reference.py`. What that comparison covers is exactly
what section 3 states: the comparisons the test makes, on the cases it runs.
Nothing is bound: no port code calls these entries yet.

| File | What |
|---|---|
| `src/game/em_level8_port.h` | public API: 51 entries, the hook table (108 hooks and the +0x4C method), fault codes |
| `src/game/em_level8_port_internal.h` | memory view, fault latch, EE float helpers, one typed wrapper per hook (generated from one table), the internal entry points |
| `src/game/em_level8_port_lift.c` | 001BBD20, 001BC560, 001BC6D0, 001BC740, 001BC860, 001BD180, 001BD270, 001BD9F0, 001BDE60 |
| `src/game/em_level8_port_gap.c` | 001284E0, 0012B850, 001BE6C0, 001BEAC0, 001BF5B0, 001BF6B0, 001BFF90 |
| `src/game/em_level8_port_creature.c` | 0011E0A8, 0012E3A0, 0012E560, 0012E840, 0012EB60, 0012F100, 0012FC10, 00131ED0, 00131F90, 00132490, 001328D0, 00132FB0, 001333F0, 00133640, 00133A20, 00133DB0, 001B4810, 0021BE40 |
| `src/game/em_level8_port_probe.c` | 001A7B80, 001A7BA0, 001B1560, 001B2B10, 001B2BF0, 001B30E0, 001B32F0, 001B3440, 001B3580, 001B55E0 |
| `src/game/em_level8_port_fx.c` | 001EAD70, 001ED100, 001F91C0 |
| `src/game/em_level8_port_overlay.c` | AREA04 0x823920, 0x8239A0, 0x823A90, 0x8245F0 |
| `tools/test_level8_port_reference.py` | original-instruction oracle and comparison; the reuse checks |

Addresses are runtime addresses. The AREA04 overlay is linked 0x40 below
where it runs (0x823920 is `func_overlay_AREA04_008238E0`).

## 0. Scope: the census rows, and what already existed

A grep of the port (`src/`, `tools/`, `docs/`) for each address before this
lane found these; "hook only" means a worker slot, a call site or a
behaviour address, not a translation.

| Row | Bytes | Decomp | First (census) | Port before this lane | Here |
|---|---:|---|---|---|---|
| 001EAD70 | 388 | BM | a06b_00 f577 | a counted gap in `em_effects_live.c` | translated |
| 001BE6C0 | 1,012 | NM | a06b_01 f612 (the AREA01 load) | none | translated |
| 001BEAC0 | 168 | BM | a06b_01 f612 | none | translated |
| 001BF6B0 | 2,268 | BM | a06b_01 f612 | hook only (the behaviour 001C02E0 stores, `em_area01_math_owner.c`) | translated |
| 001BFF90 | 64 | BM | a06b_01 f613 | hook only (`em_area01_math_owner.c`) | translated |
| 001BF5B0 | 128 | BM | a01v_01 f62 | none | translated |
| 001284E0 | 276 | BM | a01v_01 f83 | none | translated |
| 0012B850 | 280 | AW | a01v_01 f85 | hook only (`em_area01_exita.c`) | translated |
| 00215870 | 1,896 | BM | a04b_01 f8546 (the item page) | `em_status_pages_00215870` (`em_status_pages_item.c`, STATUS_PAGES.md, oracle-verified) | **reused**, re-checked |
| AREA04 0x8245F0 | 188 | C | a04b_01 f8615 | none | translated |
| AREA04 0x824830 | 252 | C | a04b_03 f627 | `em_area04_port_00824830` (AREA04_PORT.md, oracle-verified) | **reused**, re-checked |
| AREA04 0x823920, 0x823A90 | 124, 128 | C | a04b_03 f696 | none | translated |
| 001F91C0 | 1,172 | NM | a04b_03 f696 | hook only (`em_area02_misc.c`) | translated |
| 001BBD20 | 56 | CL | a04b_03 f1215 | hook only (`em_area02_math.c`) | translated |
| 001BD180 | 232 | BM | a04b_03 f1216 | hook only (`em_area02_math.c`) | translated |
| AREA04 0x8239A0 | 240 | C | a04b_03 f1225 | none | translated |
| 0012E3A0, 0012E560, 0012E840, 0012EB60, 0012FC10 | 440, 732, 792, 1,436, 2,464 | AW, NM, BM, BM, NM | a04b_03 f1949..f1951 | none | translated |
| 001BC740 | 284 | BM | a04b_03 f1949 | hook only (`em_area02_math.c`) | translated |
| 00131ED0, 00132490, 001328D0, 001333F0, 00133640, 00133A20, 00133DB0 | 72, 1,084, 1,752, 580, 916, 676, 120 | BM, NM, NM, BM, NM, BM, AW | a04b_03 f1950 | none | translated |
| 001B1560, 001B2B10, 001B32F0, 001B3440, 001B4810 | 108, 112, 160, 164, 1,240 | BM, BM, NM, NM, BM | a04b_03 f1950..f1951 | none | translated |
| 0021BE40 | 144 | AI | a04b_03 f1950 | none | translated |
| 001A7B80, 001A7BA0 | 20, 2,748 | BM, NM | a04b_03 f1977 | none | translated |
| 001B55E0, 001ED100 | 424, 840 | NM, NM | a04b_03 f2000 | none | translated |
| 0011E0A8 | 156 | AW | a04b_03 f1954 | none | translated |
| 0012F100 | 1,468 | NM | a04b_03 f2109 | none | translated |
| 00131F90, 00132FB0, 001B2BF0, 001B30E0 | 1,280, 1,076, 260, 360 | NM, NM, AW, NM | a04b_03 f2119 | none | translated |
| 001B3580 | 228 | BM | a04b_04 f474 | none | translated |
| 001BC560 | 364 | NM | a04b_04 f526 | none | translated |
| 001BD270 | 248 | BM | a04b_04 f732 | hook only (`em_area02_math.c`) | translated |
| 001BD9F0, 001BDE60 | 684, 344 | NM, AW | a04b_04 f1713 (a04b_exit) | none | translated |
| 001BC860 | 244 | BM | a04b_04 f1714 (a04b_exit) | none | translated |
| 001BC6D0 | 112 | BM | a04b_04 f1860 (a04b_exit) | none | translated |

Status abbreviations as in SECOND_LEVEL_ROUTE.md section 6 (BM byte-matched
C, NM NEARMISS, AW word assembly, AI inline asm, CL C linked from assembly,
C byte-identical overlay C).

Ground truth. Every function was translated from the original instructions
(read locally; nothing reproduced), with the decomp's C as a guide where it
is byte-identical; the order of loads and stores between calls, the float
operand order and every branch follow the instructions. Where the decomp's
text and the instructions disagree the instructions won:

- **001BBD20** (CL): the tail call is 001FBD50(self, sound, **0**, 300.0);
  the C text passes the caller's a2 through.
- **001BD9F0** (NEARMISS): 00158590 gets (self, 1, 1), or (self, 0, 1) when
  the lift's +0x2E is 1 and D_00810774 is 1; 001AFC10 and 001B0FD0 get
  self. The text's (0, 1, 3) / (1, 1, 3) / (2, 1, 3) arguments are not the
  instructions'.
- **00131E80** (a hook of 0012E3A0) reads self only; the call site also sets
  a1 (the +0x1F0 block), which the callee ignores.
- **001B4810** (BM) sets neither its bone index (s0) nor its radius (f20) for
  the types 0, 2, 8 and 12 and up: there the original uses its caller's
  callee-saved registers. **001B55E0** (NM) likewise leaves the class (s0)
  unset for a kind other than 0, 1 or 2. The translations fault
  (section 1) instead of inventing a value.
- **001A7BA0** (NEARMISS): the box of the second object is kept from its own
  points, but each of its min / max tests compares against the first object's
  transformed point of the same index (0x70003400 + 16 k) and then takes the
  second's value. The translation keeps that order; the two "corner" tests
  compare a register with itself (always true on the EE), so the replaced
  corner is always 0.

## 1. Interface (the AREA06_PORT / AREA22_PORT design)

The design is `em_area06_port.h`'s (docs/AREA06_PORT.md section 1) with:

- **Entries.** 51, named `em_level8_port_<address>`, with the original's
  argument registers in order; entries with a result write the original's v0
  (or f0 for 0011E0A8 and 001B3580) to `*result`.
- **Stack frames.** 0011E0A8 writes through its pointer only. The entries
  whose original (or a callee's) frame holds locals take `sp`, the original
  stack pointer at entry, and address the frame at the original's offsets:
  0012E3A0, 0012E840, 0012EB60, 0012F100, 0012FC10, 00131F90 (frame +0x5C),
  00132490 (+0x58, +0x5C), 001328D0, 00132FB0 (+0x40..+0x4C), 001A7B80,
  001A7BA0 (+0xD0..+0x18C and the difference array +0x190), 001B2BF0
  (+0x5C), 001B32F0 / 001B3440 (+0x4C), 001F91C0 (+0x90..+0xCC). A nested
  translation gets the original's inner stack pointer (the caller's sp minus
  its frame). Two frame slots hold callee-saved registers (001A7BA0 saves s1
  at +0x13C, 00131F90 reloads f22 from its +0x5C): those reloads are kept in
  C, as the harness files such accesses with the register saves.
- **Hooks.** 108 boot-function hooks named by original address (generated
  from one table: the header's struct, the wrappers and the test's list),
  plus `w_callback(ctx, function, actor)` for the actor's +0x4C method
  (0012E3A0, 0012E840, 001BD9F0, 001BDE60, 001BE6C0, 001BF6B0). 001000E0
  and 00128350 carry 64-bit registers (a double in a register pair).
- **Calls between translations** are direct (every one is also an entry):
  0012E3A0 -> 0012E560 / 0012E840; 0012E840 -> 0012EB60 / 0012F100 /
  0012FC10 / 001328D0 / 00133A20 / 00133DB0 / 00131ED0 / 001B4810; 0012EB60
  -> 001333F0 / 00132490 / 00133640; 0012F100 -> 001333F0 / 00132FB0 /
  00133640 / 00131F90 / 00132490 / 001B1560; 0012FC10 -> 001A7B80 /
  0021BE40 / 001B55E0 / 001B2B10 / 00132490; 001A7B80 -> 001A7BA0; 00131F90 /
  00132490 -> 0011E0A8; 00132490 / 00133640 -> 001B1560; 001328D0 -> 001B2B10
  / 001B32F0 / 001B3440; 00132FB0 -> 001B2B10 / 001B2BF0 / 001B3580;
  001B2BF0 -> 001B30E0; 00133640 -> 0021BE40; 001BF6B0 -> 001BEAC0 /
  001BF5B0 / 001284E0; 001BD9F0 -> 001BC860; 001BDE60 -> 001BBD20.
- **Quadwords, floats.** As AREA06: EE arithmetic on bit patterns through
  `em_ee_float.h` (add / sub / mul / div, MADD with the accumulator, CVT.S.W,
  NEG, the compare keys); an arithmetic shift of a register is `l8_sra`.
- **Fail-stop.** As AREA06: fault 5 (unmapped or misaligned address, or
  `bytes` NULL), 1 (reached NULL hook or NULL `w_callback`), 2 (a hook
  returned < 0), and **7** (`EM_LEVEL8_PORT_FAULT_REGISTER`): the original
  would use a register its caller left (001B4810 types 0, 2, 8, 12 and up:
  faulted right after the type is read, nothing called or written;
  001B55E0 kinds other than 0, 1, 2: faulted where the original makes the
  call that takes the register, after every earlier call and access). After
  a fault no hook runs, `bytes` is not called, writes are dropped, the first
  fault is kept, no result is written and the entry returns -1; a fault
  latched on entry, a NULL hook table, a NULL fault pointer and (entries with
  a result) a NULL result pointer return -1 at once.

## 2. What each function does (behaviour, from the code)

player = the player block 0x8102B0; the target point D_00810360; the
creature's block ent = self +0x1F0. Floats are carried as the original's
bits. The header comment of each function in the sources gives the full
field-level behaviour; in short:

**The lift and the area-change segment (AREA04 [51] / [53] / [54], AREA13's
arrival).**

| Entry | Behaviour |
|---|---|
| 001BBD20 (self, n) | the sound of the lift's model row (high byte of +0x56) and index n through 001FBD50 (0, 300); returns 001FBD50's result |
| 001BC560 () | the lift ride's camera step: the angle D_00810214 -= 1.1693707 (normalized), D_00810204 += 13.8, the point (0, 0, -20) turned by D_00810210 into D_008101F0, three accumulations, 0018D7B0(D_008101E0, 1); returns 1 |
| 001BC6D0 (self, st) | a script step on the record three links down: st +4 0 arms its +0x0B = 2 once; 1 returns whether it reached 3 |
| 001BC740 / 001BC860 (self, blk) | the button's Use offer: with +0x0B bit 2 the player turned to the button's yaw + pi and placed at (0.3, y, 5) through its matrix, the button's script (0x24E3A0 / 0x24E560 by +3; 001BC860 0x24E7E0), 001BA1F0, +0 = 2, returns 1 |
| 001BD180 / 001BD270 (rec) | the doors opening / closing: the two counters +0x10 / +0x14 by 0.2 with their clamps (6 / 14 / 16; 8 / 0), the pose block's four +0x80 floats; 001BD180 returns 1 when open, 001BD270 when +0x10 reached 0 |
| 001BD9F0 (self) | an area-change door (AREA13's room moves): init; sub-states 0 (the Use offer 001BC860), 1 (the camera target D_008105E0 at the link, 60 frames), 2 (the countdown, 001BC150 the area request), 3 (001AEBA0(4)), 4 (back to 0 when D_008106B8 clears); 001B17A0 and the +0x4C method |
| 001BDE60 (self) | the AREA13 arrival's lift door: init; sub-states 0 (+0x0B -> 001BBD20(self, 0)), 1 (001BDCA0 open -> 001BBD20(self, 1)), 2 (001BDD70 closed -> 0); 001C6380, +1 = 1, 001B1D20, the +0x4C method |

**AREA01's gap node after the keypad (a06b_01's load at entry 7, a01v).**

| Entry | Behaviour |
|---|---|
| 001BF6B0 (self) | g[36] after 001C02E0's bit-5 branch: state 0 spawns the companion (001BEAC0) and a 001BFFD0 mate, binds the model, faces (the two turned vectors, their dot at sub +8); state 1 (in the room, 001B2140) the six sub-states of the table 0x26E260 (track 001BF630, the 0x1000 animation flag, the aim and the bug spawn 001284E0 with sound 0x445, the cooldown), the hit and hit points (+0x36 / +0x34, death sounds 0x15D / 0x448, 001C1500); state 2 the fade; state 3 the free |
| 001BE6C0 (self) | the companion: follows the parent's states; passes a hit to the parent (spark 0x80000076 along the eye-to-self line, sound 0x15D); pose from the parent's matrix (+0x0D 1); the light 001F4A00 |
| 001BEAC0 (self, pos, a2, a3) | the companion's spawn (001AFA90(2)); returns the node |
| 001BF5B0 (a0, p, mode) | on the 0x1000 flag the notifier 001D0D40 (0x24FD50 / 0x5B or 0x250750 / 0x41) when p[1] differs from mode |
| 001BFF90 (obj, p, clip) | the clip set once (001C67E0(obj, clip, 4, 0)) |
| 001284E0 (owner, pos, kind, dir) | a bug spawn (behaviour 0012A5D0) while the live count 0x700031F4 is 10 or less; returns 1 on a spawn |
| 0012B850 (self, sub) | a bug's grab: 001C2770 hold test, the 120-frame hold (001287F0), the release (00128640, +5 = 1), 001C3D60 when not held |

**The creature at the lift (0012E3A0, AREA04 a04b_03) and its helpers.**

| Entry | Behaviour |
|---|---|
| 0012E3A0 (self, sp) | the behaviour: gated by the byte 0x70003B8D (modes 0..4), then by +4: spawn 0012E560, live 0012E840, 2 00131650, 3 00131E80; the countdowns, 001B5360, 001B0D80 |
| 0012E560 (self, ent) | the spawn: flags, positions, the model by +0x0D (001B10B0), the hit points by kind and D_0081070A, the 1.3 scale of the 0x80 kinds, the +0x56 clip |
| 0012E840 (self, ent, sp) | the live frame: 001B2140, the distance ent +0x44, the behaviour table 0x26D0D0 (0 idle 0012EB60, 1 hunt 0012F100, 5 bite 0012FC10, others hooks), the pause reset, 001B4810, 00133A20, 00133DB0, the hit flags, +0, the countdowns, 001328D0, the clip 001C64F0 and its step sound 0x7D3, 00131ED0, 001C68C0, 001B17A0, the +0x4C method |
| 0012EB60 (self, ent, sp) | idle, the eight sub-states of 0x26D100 (picks from the tables 0x244F10 / 0x244E90, the Use-range test 001B13F0 70, the idle clips and sounds 0x7DD / 0x7DF), then 00132490 and 00133640 |
| 0012F100 (self, ent, sp) | hunt: the random heading, the timer (0x244F30), the step sounds 0x7DE, the path test 00132FB0, the chase (001B13F0 15 / 40, 001B1560 pi/90, the lunge point 00131F20 / 001B3250 -> +5 = 2) or the 64-frame wander, the timer end, 00133640, 00131F90, the breath spark 0x8000001D, 00132490 |
| 0012FC10 (self, ent, sp) | bite: wind-up clip (0x244F70), turn and trail 0x8000000D (001EFF10), the bite (001A7B80 overlap -> push D_00810320, 001B55E0(self, 1), damage D_008104D4 by kind / type / difficulty, player bit 1) or the spit (kind 2 at frame 24, 001EFFD0), the recover clip; 00132490 |
| 00131F90 (self, ent, sp) | the turn toward ent +0x30: the quick turn (001B12B0) or the clip-7 side step by the frames left (00128250 ranges, the tables 0x242F50 / 0x243180) |
| 00132490 (self, ent, sp) | gravity ent +0x38 into y; the bite range (as a double <= 11.5) with facing -> +0x52 bit 0; else the step along the yaw by the clip's speed table (22 clips) frame by frame |
| 001328D0 (self, ent, sp) | the probes: the wall ahead (001B32F0), the four corners (001B3390), the floor rows (001B3440), the fall start (001B2F70, 001339E0), the ledge (0019B6C0, attribute 0x5B), the ceiling (0019AB20) |
| 00132FB0 (self, ent, sp) | the path ahead (001B2BF0): the turn away (001B3580), the wander (001B37D0), the climb (+5 = 3, returns 2); returns 0 or 1 otherwise |
| 001333F0 (self, ent) | the approach test (see the source) |
| 00133640 (self, ent) | the lunge test: range, height, facing, the rolls of 0x245120 / 0x245130 -> +5 = 5 / 6 / 7 |
| 00133A20 (self, ent) | the hit: the damage (x5 with bit 0x8000) against +0x34, death (001B4CF0) or the stagger |
| 00133DB0 (self, ent) | the knock-back start (+5 = 8) |
| 00131ED0 (self) | the pose block's first record cleared unless +5 is 0x63 |
| 0011E0A8 (ptr, x) | modff |
| 0021BE40 (self) | 0 when the player can be bitten (mode 0, alive, states 1 / 1, 0021BB00 zero, +0x20E zero), else 1 |
| 001B4810 (self) | the bite light by type (the point, bone, colour, 001F4E20) |

**The probes.**

| Entry | Behaviour |
|---|---|
| 001A7B80 (self, sp) | 001A7BA0(self, player, 0x10, 0x20) |
| 001A7BA0 (a, b, m1, m2, sp) | the bone-box overlap and simplex test of two objects (section 0); 1 on an overlap |
| 001B1560 (self, pos, limit) | 1 when the turn to pos is within limit |
| 001B2B10 (self, dst, src) | src turned by the yaw |
| 001B2BF0 (self, pos, out, limit, sp) | the ground probe: 0019AD00 / 001B30E0 / 0019A310 / 001B2E50 -> the floor bits |
| 001B30E0 (pos, out) | the floor from the probe's hit list |
| 001B32F0 / 001B3440 | the wall probes with the 0x700031C0 shove |
| 001B3580 (self, pos) | the heading to pos with a random spread of +-pi/3 |
| 001B55E0 (self, kind) | the bite spark 0x80000006 / 0x22 / 0x23 two ahead of the player |

**Packets and effects.** 001EAD70 / 001ED100: 001CFB50 / 001CFBE0 pairs on
D_0081F8F0 with the view's random seed (001ED100 also fills three GIF
register tables and eases the view's +8 toward 0.02). 001F91C0: a creature's
per-part effect segments by type (the part tables 0x25DB20 / 0x25DC00 /
0x25DC90 / 0x25DCF0, 001F8D30).

**AREA04 overlay.** 0x823920 / 0x823A90: in state 1 every 40th / 50th frame
the effect 001EFEB0(0, +0xD0) / 001EFD20(0x8000001B, +0x100). 0x8239A0: the
sparks' spin (001D04B0 with 0x8274A0, t += 0.01 to 2). 0x8245F0: the NPC's
second sub-state (script 0x8280D0 on the Use, the end clip, 001BA580,
001C64F0).

## 3. Verification

`python3 tools/test_level8_port_reference.py` (port root, macOS arm64 or
Linux; no make target: the Makefile belongs to another chain while this side
track runs). It compiles the six sources into
`build/level8/port/level8_port.dylib` with `-std=c11 -Wall -Wextra -Werror
-Wpedantic -ffp-contract=off`. At most 4 worker processes (EM_TEST_JOBS
overrides). `EM_LEVEL8_PORT_ONLY=<label prefix>` runs a subset (no coverage,
fail-stop, contract, leftover or reuse checks); `EM_LEVEL8_PORT_MISSING=1`
lists unexecuted words; `EM_LEVEL8_PORT_SOURCE=<dir>` tests other copies of
the sources (a mutation sweep).

### 3.1 Oracle and harness

The AREA06 harness (docs/AREA06_PORT.md section 3.1, itself AREA22's /
AREA04's), copied and owned here, with:

- **Images.** The 12 recorded eighth-level RAM images: the ends of
  a06b_00..01, a01v_00..02, a22b_00..01 and a04b_00..04. AREA06 (id 6) is
  resident in a06b_00, AREA01 (2) in a06b_01 / a01v_00 / a01v_01, AREA22
  (19) in a01v_02 / a22b_00, AREA04 (5) in a22b_01 / a04b_00..03, AREA13
  (10) in a04b_04. Before any case the test asserts that each image's
  overlay text equals the user's `extract/OVERLAY/<AREA>.BIN`, that the boot
  text equals the pinned ELF and that the four boot jump tables (001BF6B0's
  0x26E260, 0012E840's 0x26D0D0, 0012EB60's 0x26D100, 001B4810's 0x26DE40)
  equal the ELF; their loads are the only accesses left out of the
  comparison (the switches encode them).
- **Stack.** The window is [STACK_TOP - 0x800, STACK_TOP) (the deepest frame
  chain, 0012E3A0 -> 0012E840 -> 0012FC10 -> 001A7BA0, reaches 0x320).
- **Callees run as original code** (RUN): 001000E0, 001026A0, 001026D0,
  00102718, 00102738, 00102760, 001028B8, 001028D0, 00102948, 00102958,
  001029C0, 00102B08, 00102BB0, 00102C58, 0011DE90, 0011DF78, 0011E2A8,
  0011E620, 0011E748, 001281C0, 00128250, 00128350, 001B1240, 001B12B0,
  001B13F0, 001B1470, 001B15D0, 001C6160 (each first rehearsed with the
  argument registers its hook does not pass poisoned). Every other callee is
  stubbed with scripted results (a Scribble writes what the callee would,
  e.g. 0019A310's distance or 001B2F70's floor).
- **Compared, per case**: as AREA06 (the calls and their arguments; memory at
  every call entry; the memory accesses between calls one for one, in order,
  by address, size and changed-or-not; all memory after the last store; the
  return value (f0 for 0011E0A8 and 001B3580); the store-log self-check;
  stops at unmapped or misaligned original accesses; every case again from a
  poisoned start image; coverage of every reachable original word of all 51
  entries; the table's ctx at every call).
- **Fail-stop**: `fault_checks` (001BC740 on button [54] with the Use bit:
  NULL hook, failing hook, an unmapped address after two calls; 001BD180 for
  an unmapped address before any call; a latched fault; every entry with a
  latched fault, a NULL hook table, a NULL fault pointer and, for the entries
  with a result, a NULL result pointer), `hook_contract_site` (every call
  failing, every access refused, each hook NULL / INT32_MIN / 1 / INT32_MAX,
  `bytes` NULL, on passing cases that together reach all 108 hooks, the
  +0x4C method and all 51 entries) and `leftover_checks` (fault 7, section 1).
- **Dead words.** 19 reachable words no input can reach (`DEAD_WORDS`, with
  the reason in the test): 001A7BA0's two self-compares (the EE's compare is
  total) and its d4 negation (the dot of a vector with its own normal:
  every lane product has a clear sign bit).

### 3.2 Cases

- **Capture**: every entry on the captured state of the images it applies to
  (the AREA13 room moves and the arrival door in a04b_04; the gap node, its
  companion and the bugs in a06b_01 / a01v; the lift, its buttons and the
  NPC in the AREA04 images; the creature and its sparks in a04b_03).
- **Designed**: every state and sub-state of every function with each callee
  result on both sides of its test; the float bounds of each compare (the
  door counters' 6 / 14 / 16 / 8 / 0, the gap node's alpha, the reach 15 /
  30 / 40 / 50 / 70, the heights 13 / 10 / -0.5, 0.8 dot, 24 / 46 / 22 /
  95 / 43 frames); scripted rand values chosen from the capture's own tables
  (0x244F10, 0x245120, 0x245130) at run time; the creature placed around the
  player at designed distances and bearings; the player given the creature's
  boxes (001A7BA0 finds an overlap: the bite lands) and bones shifted
  against the creature's (overlap without enclosure); designed probe hit
  lists in the scratchpad; the modff exponent classes; seeded random
  creature states (a random state of every byte and float the creature code
  tests, positions around the player, scripted callee results) for each of
  its 14 state functions: 3,900 in all.
- **The default run's share.** Every capture case and every designed case
  outside the bulk grids runs by default. Of the bulk grids (`GRID` in the
  test: the combination sweeps of the gap node, the lift's room moves, the
  overlay periods, the creature's targeted states, the probes) and of the
  random creature states, the default run keeps the cases a coverage pass
  picked (`GRID_KEEP`, `PINNED`): the pass runs every case's original and
  keeps a greedy set that, with the other default cases, reaches every word
  the whole set reaches. EM_TEST_FULL=1 runs everything.
- **Survivor cases** (`survivor_cases` in the test, default run), added
  because the mutation sweep (section 3.5) showed the set above missed
  them: 00131F90's side step with the frames left at the edges of each
  range; 00132FB0's three random fields with rand words that tell their
  shifts apart; 001ED100's level at the floats next to its 0.02 floor;
  001A7BA0 on two designed box objects touching on each face of each axis
  and with 4, 5 and 8 points a group (the clamp to four); 0012FC10's spit
  speed with high rand bytes and its stage 3 with only bit 0 of +0x58 set;
  001328D0's stage 3 against 2, 4 and 5; 001B30E0's slope at pi/3 and the
  floats beside it, both signs.

### 3.3 Measured

2026-09-30, M1, host shared with other lanes. Default run: 866 cases (124
capture + 742 designed), 1,520 runs (654 poisoned), 17,286 call entries
compared, 13,886 helper register rehearsals, coverage 6,300 / 6,300
reachable words (the 19 dead words of section 3.1 excluded), the fail-stop
checks, the hook contract (4,943 native runs on 110 cases), the leftover
checks and the reuse smoke samples passing; 19.1 s CPU, 8.5 s wall with 4
workers. `EM_TEST_FULL=1`: 6,826 cases (124 capture + 6,702 designed), 12,092
runs (5,266 poisoned), 94,568 call entries compared over all memory, 63,624
rehearsals, the hook contract on 765 cases (12,071 runs), every reuse check
in full; 443 s CPU, 158 s wall. All pass.

### 3.4 Reuse checks (the existing translations over the eighth-level captures)

Each check runs the existing translation against the original with that
module's own harness (imported; libraries built into
`build/level8/port/reuse`). EM_TEST_FULL=1 runs them in full; the default
run keeps one smoke sample of each.

- **0x824830** (`em_area04_port_00824830`, the harness of
  `tools/test_area04_port_reference.py`, `run_case`): [3] (0x7AA590) as
  captured in a22b_01 and a04b_00..03, and its sub-states 0 (D_008107EA 0 /
  0x10 / 0xFF with 001B6660 results), 1 (001BA1F0 0 / 1) and 2: 50 cases.
- **00215870** (`em_status_pages_00215870`, the harness of
  `tools/test_status_pages_reference.py`, `item_case`): the harness's own
  EVENT page inputs over the a04b_01 image (the NPC's item page frame): 69
  cases.
- **001BC960 / 001BD560** (`em_area02_math_*`, the harness of
  `tools/test_area02_math_reference.py`, `run_case`): the lift [51] / [56]
  and the buttons [53] / [54] as captured in a04b_00 and a04b_03, the Use at
  button [54] taken and not, and the lift / buttons at a04b_04's AREA13
  arrival: 17 cases.

### 3.5 Mutation sweep (bounded, two rounds)

Single-edit mutants of the six sources (operators, comparisons, float
compare macros, signed / unsigned loads, store widths, constants +1 and
^4, integers +1, dropped stores and calls), a seeded sample run through the
default test with `EM_LEVEL8_PORT_SOURCE`. The sweep script is lane scratch
(not committed); it is rerunnable from this description.

- **Round 1** (150 mutants; the generator then also mangled digits inside
  identifiers, so 71 did not compile and are not counted): 70 killed, 9
  survived. Two were real gaps, killed by the first survivor cases
  (00131F90's 12-frame edge, 00132FB0's wander shift); seven are
  equivalent (below).
- **Round 2** (seed 0xB2, 200 mutants, generator fixed): 176 killed, 1 hung
  (a loop bound `<` made `<=` never ends; counted as detected), 6 rejected
  by `-Werror` (logical-op / parentheses / unused-variable warnings), 17
  survived. Seven were real gaps (001ED100's floor constant, 001A7BA0's
  touching-face compare and its 4-point clamp, the spit speed constant,
  0012FC10's stage-3 mask, 001328D0's stage-3 test, 001B30E0's pi/3
  constant); the round-2 survivor cases kill all seven (each mutant rerun
  on them). Ten are equivalent.
- **Equivalent survivors** (argued from the code, not assumed): a different
  return value on a path that has already latched a fault (the entry
  returns -1 and the value is discarded; 001B1560's and 001A7BA0's callee
  failures); a signed instead of an unsigned byte load in a truthiness
  test, a test against 0 or 1, or a test of bit 0..6 (0012FC10's damage
  table pick, 00132FB0's climb tests, 0012EB60, 001BE6C0's parent byte); a
  signed halfword load where only == 3
  / == 4 follow (00133640's clip) or the value is masked to 16 bits
  (0012EB60's counter); 0x81 for 0x80 on a branch where bit 0 is known
  clear (0012E560); initial values of locals that the hook always writes on
  success (0012E840, 0012E560, 001333F0); 2 for 1
  as a truth flag (001B32F0's wall mode); 0011E0A8's exponent test `< 23` made
  `<= 23` (at exponent 23 the fraction mask is 0 and the integral branch gives
  the same result and store); and 001A7BA0's d4 negation, which is a dead
  path (section 3.1).

## Binding

Nothing calls these entries. To run them live a host must:

- supply `bytes` over the scene's original-byte storage (the actor pool, the
  player and camera blocks, the pose block *D_00275B40, the model / box
  lists the creature and the player point to, the probe's hit lists in the
  scratchpad, the data tables named in section 2, the stack frame below
  `sp`);
- run 0012E3A0, 001BD9F0, 001BDE60, 001BF6B0, 001BE6C0 and the AREA04
  0x823920 / 0x8239A0 / 0x823A90 as node behaviours (+0x10), 0012B850 from
  the bug behaviour 0012A5D0 (`em_area01_exita.c` CALL2 0x0012B850), 0x8245F0
  from 0x824320, 001BC740 / 001BD180 / 001BD270 / 001BBD20 from
  `em_area02_math.c`'s hooks (FN_1BC740 / FN_1BD180 / FN_1BD270 /
  FN_1BBD20), 001BF6B0 / 001BFF90 from `em_area01_math_owner.c`, 001F91C0
  from `em_area02_misc.c`, 001EAD70 in place of `em_effects_live.c`'s
  counted gap, 001BC6D0 and 001BC560 from the script host (op09 records),
  and bind the +0x4C method `w_callback` to the owner draw;
- bind each hook to a verified port translation or a fail-stop stand-in.
  The stubbed callees (every hook not in the RUN list above) are not
  verified here; the helpers run as original code need port translations
  paired by address.

## Known gaps

- **Live evidence.** The captures are end-of-beat states. The creature is
  captured once (a04b_03, bite state +5 = 5, sub-state 2); its idle, hunt,
  lunge, turn and fall paths, the gap node's states other than 1, the lift's
  opening and ride (001BD180 / 001BC560), the AREA13 room moves' sub-states
  1..4 and the NPC's second sub-state are designed or random cases on the
  captured RAM with the state bytes patched.
- **Stubbed callees' inputs.** Only the RUN helpers are proven not to read
  a register their hook does not pass; the other hooks' argument lists were
  read from the callees' code (and the call sites), and their results are
  scripted beyond their real ranges.
- **Register leftovers.** 001B4810 (types 0, 2, 8, 12 and up) and 001B55E0
  (kinds other than 0, 1, 2) fault 7 where the original uses its caller's
  register; whether any live caller reaches them is not established (the
  creature is type 1 and 0012FC10 passes kind 1).
- **What the harness cannot see** (as AREA06): whether an access that changed
  nothing is a load or a store of the same value; hardware behaviour past a
  stop; the frame bookkeeping (and the two callee-saved frame slots of
  section 1).
- **Float proofs rest on the EE model** (`em_ee_float.h`).
- **Default run time.** The default run takes about 19 s CPU (8.5 s wall
  with 4 workers), above the ~10 s CPU guideline; the coverage-picked share
  is already minimal for full word coverage, and the oracle's per-access
  replay is the cost.
- **Mutation sweep** is a bounded sample (two rounds, 350 mutants of about
  7,000), not exhaustive.
- **Reuse checks** use the other harnesses' own models and case lists.
