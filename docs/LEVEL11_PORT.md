# The eleventh level: the new functions of the a13c route (lane L11T)

Lane L11T, 2026-10-01 (level side track). It covers the 43 functions the
eleventh-level route census found new (decomp `build/s87/census/a13c_delta.json`,
`new_functions`: 43 rows, 27,492 bytes; ELEVENTH_LEVEL_ROUTE.md section 5):
AREA13's recharger [60], the roof walkway, the battery machine [44] and
[45]'s sequence (the blast), the cure g[3], the fallen boom and [7]'s area
(the group 0x82A230 and its two 0x141D20 actors), and the region south of
the pipe fence. 42 had no verified port translation and are translated here;
one (0020D930) has a verified translation, `em_menu_hover_0020D930`, and is
reused: this lane re-runs it against the original over the eleventh-level
captures (section 3.4).

Every translation is compared with the original instructions by
`tools/test_level11_port_reference.py`. What that comparison covers is
exactly what section 3 states: the comparisons the test makes, on the cases
it runs. Nothing is bound: no port code calls these entries yet.

| File | What |
|---|---|
| `src/game/em_level11_port.h` | public API: 42 entries, the hook table (103 hooks and the indirect-call callback), fault codes |
| `src/game/em_level11_port_internal.h` | memory view, fault latch, EE float helpers, one typed wrapper per hook (written from the test's HOOKS table; see section 1, Hooks), the internal entry points |
| `src/game/em_level11_port_area13.c` | AREA13 0x8240E0, 0x824390, 0x824520, 0x8246D0, 0x8248C0, 0x824960, 0x8249F0, 0x826610, 0x827C30, 0x827DD0, 0x827E00, 0x827F20, 0x827F90, 0x8284E0, 0x828500, 0x828C60, 0x828E10, 0x828F40 |
| `src/game/em_level11_port_creature.c` | the 0x141D20 actor family: 00141D20, 00141F00, 00142070, 00142330, 001424C0, 001429D0, 00145880, 001459A0, 00146110, 001464B0, 001469B0, 00146CE0, 001471E0 |
| `src/game/em_level11_port_boot.c` | 00100130, 00118418, 0019A6F0, 001B2E50, 001B3F10, 001CDDC0, 001CE660, 001E4610, 001F4190, 001F4840, 001F9140 |
| `tools/test_level11_port_reference.py` | original-instruction oracle and comparison; the reuse check |

Addresses are runtime addresses. The AREA13 overlay is linked 0x40 below
where it runs (0x824390 is `func_overlay_AREA13_00824350`). Bracketed numbers
([44], [45], [7] ...) are the placement records of ELEVENTH_LEVEL_ROUTE.md
section 1.1; they, the decomp's role comments and names such as
`anim_clip_init` (001C67E0) are labels, not evidence. The behaviour below is
read from the code.

## 0. Scope: the census rows, and what already existed

A grep of the port (`src/`, `tools/`, `docs/`) for each address before this
lane found these; "hook only" means a worker slot, a call site or a stub,
not a translation. Several AREA13 addresses are found elsewhere for other
areas' overlays (the same runtime addresses hold other code there).

| Row | Bytes | Decomp | First (census) | Port before this lane | Here |
|---|---:|---|---|---|---|
| AREA13 0x824390 | 396 | C | a13c_02 f1149 | hook only (`em_level9_port.h` w_00824390: [44]'s step 2, called from 0x823E90's step switch in `em_level9_port_area13.c`) | translated |
| AREA13 0x827C30 | 412 | C | a13c_02 f1149 | reached through `em_level9_port`'s w_callback (0x827150's step table 0x82D190) | translated |
| AREA13 0x828E10, 0x828F40 | 284, 856 | C | a13c_03 f451 | none (0x828E10 in `em_area02_overlay.c` is AREA02's) | translated |
| AREA13 0x8240E0, 0x8249F0 | 124, 132 | C | a13c_03 f462 | none | translated |
| AREA13 0x8246D0 | 224 | C | a13c_03 f470 | none | translated |
| AREA13 0x8248C0 | 148 | C | a13c_03 f526 | none | translated |
| AREA13 0x824520 | 428 | C | a13c_03 f697 | hook only (`em_level9_port.h` w_00824520, step 3; the hits in `em_truck_original.c` / `em_area06_port_overlay.c` are other areas' 0x824520) | translated |
| AREA13 0x827DD0, 0x827E00 | 44, 288 | C | a13c_03 f752, f753 | step table via w_callback (0x827DD0 in `em_level10_port_area19.c` is AREA19's) | translated |
| AREA13 0x827F20 | 112 | C | a13c_03 f1053 | step table via w_callback | translated |
| AREA13 0x824960 | 136 | C | a13c_03 f1284 | hook only (`em_level9_port.h` w_00824960, step 4) | translated |
| AREA13 0x827F90 | 1,352 | NM | a13c_03 f1413 | step table via w_callback | translated (instructions) |
| AREA13 0x828500 | 1,880 | C | a13c_03 f1413 | none | translated |
| AREA13 0x8284E0, 0x828C60 | 32, 428 | C | a13c_03 f1530, f1711 | none (0x8284E0 in `em_area00_overlay.c` is AREA00's) | translated |
| AREA13 0x826610 | 132 | C | a13c_05 f62 | hook only (`em_level9_port_turret.c` w_00826610, from 0x824BB0) | translated |
| 00118418 | 660 | NM | a13c_02 f61 | none | translated (instructions) |
| 001CDDC0, 001CE660 | 1,344, 504 | NM | a13c_03 f464 | none | translated (instructions) |
| 001E4610 | 980 | NM | a13c_03 f464 | none | translated (instructions) |
| 001F4840 | 440 | BM | a13c_03 f753 | none | translated |
| 001F4190 | 1,704 | NM | a13c_03 f754 | none | translated (instructions) |
| 001F9140 | 56 | BM | a13c_03 f1413 | none (a comment in `em_shadow_actor_route.h`) | translated |
| 00100130 | 36 | BM | a13c_04 f2032 | none (comments in `em_hud.c`, `em_menu_hover.h`: `em_menu_hover` folds the soft-double compare in) | translated |
| 0020D930 | 1,640 | NM | a13c_04 f2032 | `em_menu_hover_0020D930` (`em_menu_hover.c`, verified against the original by `tools/test_menu_hover_source_reference.py`) | **reused**, re-checked |
| 00141D20, 00141F00, 00142070, 00142330, 00145880, 001459A0, 00146CE0, 001471E0, 001469B0, 001424C0, 00146110, 001429D0 | 472, 360, 704, 388, 276, 1,904, 660, 428, 320, 1,296, 916, 2,796 | NM, BM, BM, AW, BM, NM, BM, BM, NM, NM, NM, NM | a13c_05 f1814..f1819 | none | translated |
| 001464B0 | 648 | BM | a13c_06 f6 | none | translated |
| 001B2E50 | 288 | NM | a13c_06 f6 | hook only (`em_level8_port_probe.c` w_001B2E50) | translated (instructions) |
| 001B3F10 | 732 | NM | a13c_06 f152 | hook only (`em_level8_port_creature.c` w_001B3F10) | translated (instructions) |
| 0019A6F0 | 532 | NM | a13c_06 f332 | none | translated (instructions) |

Status abbreviations as in SECOND_LEVEL_ROUTE.md section 6 (BM byte-matched
C, NM NEARMISS, AW word assembly, C byte-identical overlay C).

**Ground truth.** Every function was written from the decomp's C
(byte-identical for the overlay rows except 0x827F90; byte-matched or
NEARMISS for the boot rows; for 00142330 the decoded words of its decomp
file, the only source) and then corrected until the original-instruction
oracle (section 3) found no difference: the
order of every load and store between calls, every call and argument, the
float operation order and every branch outcome on the cases run. The
oracle's per-case reports (which access, call or byte differs first) and
coverage of every reachable original word drove the corrections; where a
choice had no observable effect on any case (for example which of two
equal register values a store uses) it is not a claim. Where the decomp's
text and the original disagree the original wins; the differences found:

- **00118418** (NEARMISS): returns the new stream cursor (a0 +8 after the
  += 6 or += 3; the text returns 0); the cursor is read once at entry and,
  on the note-on path, again after each matched channel's calls; the
  broadcast path compares a0 fields after the channel fields' loads in the
  order (+0x22 then a0 +0x24; base, +0x3E, q[4], +2, q[5], +6, a0 +0x18).
- **001E4610** (NEARMISS): the size switch reads the actor's own +0xD (the
  text reads the block's +0xD) and has three outcomes: 0 -> s +0x14 = 50, 1
  -> 30, each with s +0x28 = 5, +0x44 = +0x50 = 0; any other kind keeps
  those words (the text has two outcomes). The ring call passes the
  block's doubleword +0x20 and word +0x1C as 001CE660's fifth and sixth
  arguments (the text calls it with four).
- **001424C0** (NEARMISS): the alerted branch turns toward the player when
  the distance d +0x5C is **beyond** 20 (the text: within 20); the turn
  test compares with the double of the float pi/8 (0x3FD921FB60000000); the
  distance d +0x5C is read once or twice by d +0x71 as the original does.
- **001459A0** (NEARMISS): bit 0 of d +0x74 is set at each hit site (the
  text sets it at the tail); the second segment pair is tried whenever d
  +0x74 is still zero after the first pair (an `if`, not an `else if`).
- **00146110** (NEARMISS): the vertical probe copies e +0xB0.. to
  0x700038A0 / 0x700038B0 word by word (no 00102948 calls) and the floor
  0x700038D0 is read right after the hit polygon's first word.
- **0019A6F0** (NEARMISS): with (a0 +2 & 0x1F) nonzero and (t0 & 0xFFFF) !=
  0x40 the call 001A7280 is skipped and the result register keeps a value
  the function set earlier: measured nonzero for every input tried (every
  other register filled with four patterns at entry; 81 combinations of
  segment values, segment pointers, flags and t0), so the
  translation takes it as nonzero (the text leaves it uninitialized). Every
  caller in the census passes t0 = 0x40.
- **001B2E50** (NEARMISS): a negative count skips the walk (last = -1: the
  flags just below the table are read).
- **001CDDC0 / 001CE660** (NEARMISS 0% / 40%): every transform is the VU0
  multiply-accumulate of the rows by x, y and z broadcast, then the
  multiply-add of row 3 by w (all four lanes); Q = 1 / w is the VU0
  division of the constant register's w by w; the projection multiplies
  by Q (x, y, z for corner 0; x, y, then z by a second Q = 1 / (w - 1) per
  clipped vertex, w - 1 being a broadcast subtract on the w lane); the
  fog lane is fog.z + w * fog.w (accumulate from the constant register,
  then multiply-add on the w lane), clamped by the minimum with fog.x and
  then the maximum with 0, and the whole vector goes through the
  fixed-point conversion with four fraction bits. Corner 0's camera vector is
  spilled to the frame (sp - 0x20), each clipped vertex's to sp - 0x10 and
  1 / w for S T Q is an EE division of the spilled w. The fog and camera
  rows are read again for each triangle. The block header's first
  quadword is one quadword store of zero; modes 2..4 store 0xFF0 at
  0x7000360C before the colour words overwrite it. 001CE660 reads its band
  as start, end, count, count, and reloads the sine / cosine it just
  stored.
- **001F4190** (NEARMISS): the drift section reads the time, cfg +0x48 and
  the velocity's y, then the position's x, stores the new y velocity, and
  reads the velocity again (x, y, z) before the position stores.
- **0x827F90** (overlay NEARMISS): no difference in behaviour; the work
  block pointer D_00275CA8 is read once for the phase and the count.
- **00142330** (word assembly): translated from the decoded words of its
  decomp file (the only source); section 2 gives the behaviour.

## 1. Interface (the LEVEL10_PORT / LEVEL9_PORT design)

The design is `em_level10_port.h`'s (docs/LEVEL10_PORT.md section 1) with:

- **Entries.** 42, named `em_level11_port_<address>`, with the original's
  argument registers in order; entries with a result write the original's
  v0 to `*result` (0x8240E0, 0x8246D0, 0x8248C0, 0x8249F0, 00100130,
  00118418, 0019A6F0, 001B2E50, 001B3F10, 00146110, 001464B0, 00146CE0).
  64-bit register arguments are `uint64_t` (00100130's two soft doubles,
  001CDDC0's a3 and 001CE660's t0); 0019A6F0 takes its fifth argument (t0)
  as `t0`; float arguments are `float` with the original's bits (001F9140's
  f12, 001B3F10's f12 / f13).
- **Stack frames.** Entries whose original (or a nested callee's) frame
  holds locals take `sp`, the original stack pointer at entry, and address
  the frames at the original's offsets: 0x826610 (the colour at sp -
  0x10), 0x827C30 (the point at sp - 0x10), 0x828500 (the colour at sp -
  0x10; 001F9140's frame at sp - 0x50), 0x828C60 / 0x828F40 (the packet at
  sp - 0x60), 001F9140 (sp - 0x10), 0019A6F0 (sp - 0x10), 001B3F10 (0019A6F0
  at sp - 0x30), 00146110 (sp - 0x10), 001469B0 (001B3F10 at sp - 0x30),
  00142330 (001469B0 at sp - 0x30), 001424C0 (00146110 / 001469B0 at sp -
  0x40), 00142070 (the behaviours at sp - 0x30), 00141D20 (00142070 at sp -
  0x30), 001CDDC0 (sp - 0x10 / - 0x20), 001CE660 (001CDDC0 at sp - 0x90),
  001E4610 (001CE660 at sp - 0x40). The offsets are the ones the oracle's
  stack window shows (section 3.1).
- **Hooks.** 103 callee hooks named by original address (boot functions
  and the AREA13 overlay's own 0x824060). The header's struct (between its
  GENERATED HOOKS markers) and the wrappers in the internal header
  (GENERATED WRAPPERS) were written from the test's HOOKS list by a lane
  script that is not committed; the test's `header_checks` compares them
  with HOOKS on every run (names and order, argument and result types, one
  wrapper per hook that calls it and latches its address), so an edit of
  one side alone fails the test. Plus `w_callback(ctx, function, a0)` for
  every indirect call (the actor's +0x4C method). Float hook arguments and
  results cross as floats whose bits are the original's; inside the module
  floats are carried as bit patterns.
- **Calls between translations** are direct: 0x824390 -> 0x8249F0 /
  0x8240E0; 0x828500 -> 001F9140; 001B3F10 -> 0019A6F0; 001E4610 ->
  001CE660 -> 001CDDC0; 001F4840 -> 001F4190; 00141D20 -> 00141F00 /
  00142070; 00142070 -> 001471E0, 00142330, 001424C0, 001429D0, 00146CE0,
  00145880, 001459A0; 00142330 / 001424C0 -> 001469B0 -> 001B3F10;
  001424C0 -> 00146110; 001429D0 -> 001464B0.
- **Floats.** EE arithmetic on bit patterns through `em_ee_float.h` (add,
  sub, mul, div, cvt.s.w and the compare keys); VU0 lanes through
  `em_vu_vec_bits` / `em_vu_div_bits` / `em_vu_min_bits` / `em_vu_max_bits` /
  `em_vu_ftoi4_bits` with the forms of section 0 (each one is in the float
  model's table of forms the boot ELF executes). Quadword loads and stores
  use the EE's rule (the low four address bits are ignored); a VU0
  register load or store is four word accesses, as the oracle models
  lqc2 / sqc2.
- **Access order.** No C expression in the three sources reads memory twice
  where C leaves the order unspecified: every read the original orders is
  its own statement, the only read of its expression, or nested as an
  address (a lane scan of the sources found none left; section 3.1).
- **Fail-stop.** As LEVEL10: fault 5 (unmapped or misaligned address, or
  `bytes` NULL), 1 (reached NULL hook or NULL `w_callback`), 2 (a hook
  returned < 0), and 7 (`EM_LEVEL11_PORT_FAULT_REGISTER`) for a VU0 form the
  float model does not define (never expected: every form used is in its
  table). After a fault no hook runs, `bytes` is not called, writes are
  dropped, the first fault is kept, no result is written and the entry
  returns -1; a fault latched on entry, a NULL hook table, a NULL fault
  pointer and (entries with a result) a NULL result pointer return -1 at
  once.

## 2. What each function does (behaviour, from the code)

self = the pool node; talk = self + 0x1F0; the player block 0x8102B0
(position 0x810350..58, a second copy 0x810360..68). The header comment of
each function in the sources gives the field-level behaviour; in short:

**AREA13 (overlay id 10).**

| Entry | Behaviour |
|---|---|
| 0x8240E0 | the walkway test: 1 when flag 0x1C (D_00810774) is not 0xFF, 001B1EA0(0, the player, the area 0x82E140, 4) is set and the player's y is above 210 |
| 0x8249F0 (self) | up to 12 objects of the +0x18 chain: +0x36 = 1 where +2 & 0x1F is 4, +3 is 0xA and +0xB4 is not below 215; returns 1 |
| 0x824390 ([44] step 2) | +5 0: +5 = 1, the count +0x2A = 490; +5 1: count down; at 0 0x8249F0, then script 0x82B3D0 (0x8240E0) or 0x82B810 (0x824060) with +5 = 2, +6 = 1, else script 0x82BC90, D_008107F4 += 1, the parent's +0x2A += 1; +5 2: at the script's end the parent's +0x2A += 1, +5 = 0, D_008107F4 += 1; then 001C6380, 001B17A0, the +0x4C method |
| 0x824520 ([44] step 3) | scripts 0x82BD10 / 0x82BDD0 / 0x82BE90 released by the parent's +0x28 reaching 2 / 3 / 4; +5 3: at the end 001FABB0(), 001FA790(0, 0x12), D_008107F4 += 1; then 001C6380, 001B17A0, the method |
| 0x824960 ([44] step 4) | +5 0: once D_00810833 is set, script 0x82C110; +5 1: at its end 001FAE70(0) and +4 = 3 |
| 0x8246D0 / 0x8248C0 (a0, self, blk) | the callbacks of scripts 0x82B3D0 / 0x82B810: a 50- (45-) step count in blk +0x10; per step the player's x -1 and y + 0.2 * 0011DE90(0.0349 * count) (z -0.7); 1 when the count passes the bound |
| 0x826610 (src) | 001AFA90(0xC); a new object gets its position (src +0x30), its matrix (src), its +0x100 = the matrix times the colour 0x82C9E0, and the behaviour 0x1F5040 |
| 0x827C30 ([45] step 0) | at count 0x1DF two effects at 0x82D1B0[0], self +0x28 += 1, markers -1, the sound 0x8D3, 001B1E20(8, 100); at 0x1F3 D_008107F4 \|= 0x20, the effect 0x80000041 and the sound 0x44D at (743, 230, 1265); the marker count by the count (1 / 100 / 200 / 300 / 400 -> 5..1); the limit 0x30C |
| 0x827DD0 ([45] step 1) | the limit = 0 when self +0x2A is set, else 0xFFFF |
| 0x827E00 ([45] step 2) | at counts 0 / 0x5A / 0x96 the effects 0x8000004A / 0x8000002F at 0x82D1B0[1..3] with the angle (-pi/2, 0, 0, 1), +0x28 += 1, markers -1, the sound 0x44B; limit 0x12C |
| 0x827F20 ([45] step 3) | 001C64F0(self, 1) above count 0xB4, the sound 0x8D4 at count 0; limit 0x168 |
| 0x827F90 ([45] step 4) | phase 0: every 45 counts a random point up the bone matrix with effects 0..2, 2, 2, 0x8000004A, 0x8000002F (and 3 at count 0) and the sound 0x8D6; every 20 counts a second point; with self +0x3C <= 10 and the player in the area 0x82D330 below y 210 the player's pending damage = its health (D_008104D4 = D_008104D0, D_008102BF = 0xB, D_008102B0 \|= 2); at +0x3C <= 4 the next phase, the sound 0x8D5, D_00810833 = 0xFF. Phase 1: five effects 4 stepped from (795, 180, 1130) toward (680, 170, 1015) at the first count; after 30 self +5 += 1. Always 001C64F0(self, 1), limit 0xFFFF |
| 0x8284E0 (self, other) | +4 = 2 unless bit 1 of other +0 is set |
| 0x828500 (the thrown piece) | state 0: model by +0xD (7..10), target (the player, a random point at radius 50 around the player, or (735, 225, 1250)), random spin, life 1200, the collision method 0x8284E0; state 1: ends on D_008106B8 2 with the halfword 0x28A9A0 2, on D_00810833 0xFF (kinds 7 / 8) or at the end of its life; the arc start -> target (height 100 sin(pi t)), the landing probe after t 0.5, the marker 0x3F5 and the ground mark (001F9140 with the colour 0x82D3B0); state 2: the impact effect and sound 0x449 |
| 0x828C60 / 0x828E10 / 0x828F40 | effects: the matrix from +0xC0 / +0xB0, then packets 0x82D3C0 (001CFB50 / 001CFBE0), 0x82D450 (001D04B0) or eight packets 0x82D4E0 on a circle of radius 10 (001CFA60 / 001CFBE0) with an easing step, ending past 1.8 / 1.5 / 1.5 |

**Boot.**

| Entry | Behaviour |
|---|---|
| 00100130 (two soft doubles) | 1 when 001274B0 (the soft-double compare) returns >= 0 |
| 00118418 (the sequencer event) | type 1: re-arms the playing channels of the event (the envelope times (q[2] * 4) * rate / 60); else the note-on of the matching idle channel (00117BA0's pitch, 001179E0, 001157F0); advances the cursor by 6 / 3 and returns it |
| 0019A6F0 (a0, a1, a2, flags, t0) | the segment probe: the segment into 0x70003190.., then by flags the grid hull (001A6440 / 001A7280), 001A0B10 and 0019D330; the winning mode (1 / 2 / 4) at 0x700031D8 and returned |
| 001B2E50 (a0, out) | the sorted threshold table 0x700030F0 at a0 +4 (section header in the source) |
| 001B3F10 (self, bearing, height) | the line-of-fire test: the yaw error selects a radius table by self +3; the player within it (001B13F0), within the height, and 0019A6F0 (mode 7) not blocked by an object with bit 0x2000 -> 1 |
| 001CE660 / 001CDDC0 | the textured ring of band +0 samples (section 0) and the textured quad with fog |
| 001E4610 (the ring emitter) | grows the ring (s +0x18) to 2 with an alpha fade past 1, builds the colour bytes (00128250) and draws the ring (001CE660) |
| 001F4840 / 001F4190 | the spawned effect: the lifetime by kind, then one burst per frame of cfg +0x4C pieces from the table 0x25A350 + kind * 0x60 (an inline random sequence rnd * 37 + 11) |
| 001F9140 | the ground mark: 001F8D30 with the colour D_0025DB00 copied to the frame |
| 00141D20 (e) | the behaviour of the 0x141D20 actors: the pause byte 0x70003B8D, then by e +4: spawn 00141F00, live 00142070, 001450B0, 00145850; the countdowns; 001B5360, 001B0D80 |
| 00141F00 | the spawn step: health 500 / 300 or 350 / 200 by e +0xD bit 7 and D_0081070A, the scale, the pose 0x7E / 0x7D |
| 00142070 | the live frame: the flat distance to the player, the zone 001471E0, the behaviour e +5 (0 00142330, 1 001424C0, 2 001429D0, 3..8 hooks), the class byte, the voice cues of clips 9 / 8, 00146CE0, 00145880, 001459A0, the clip time 001C64F0 and the services |
| 00142330 / 001424C0 / 001429D0 | behaviours 0 (idle), 1 (wander) and 2 (chase): section header of each in the source |
| 00145880 / 001459A0 / 00146110 / 001464B0 | the fall and run, the collision probes, the step probe and the forward probe |
| 001469B0 | the alert counter (001B3F10 toward the player's +0x14C actor) |
| 00146CE0 | the hit counter (damage, stagger, death) |
| 001471E0 | the zone by the player's z / the areas 0x245A60 / 0x245AA0 |

## 3. Verification

`python3 tools/test_level11_port_reference.py` (port root, macOS arm64 or
Linux; no make target: the Makefile belongs to another chain while this side
track runs). It compiles the three sources into
`build/level11/port/level11_port.dylib` with `-std=c11 -Wall -Wextra
-Werror -Wpedantic -ffp-contract=off` (the reuse check builds
`em_menu_hover.c`, unmodified, into `build/level11/port/reuse`; all of
`build/` is ignored, and `build/level11` can be removed after a run). At
most 4 worker processes (EM_TEST_JOBS overrides).
`EM_LEVEL11_PORT_ONLY=<label prefix>` runs a subset (no coverage, fail-stop,
contract or reuse checks); `EM_LEVEL11_PORT_ALL=1` runs every designed case
without the full-mode comparisons; `EM_LEVEL11_PORT_MISSING=1` lists
unexecuted words; `EM_LEVEL11_PORT_SOURCE=<dir>` tests other copies of the
sources (the mutation sweep); `EM_LEVEL11_PORT_NOCONTRACT=1` /
`EM_LEVEL11_PORT_NOREUSE=1` skip the hook contract / the reuse check
(debugging only); `EM_LEVEL11_PORT_KEEP=1` prints the coverage pass behind
DEFAULT_KEEP.

### 3.1 Oracle and harness

The LEVEL10 harness (docs/LEVEL10_PORT.md section 3.1, itself LEVEL9's /
LEVEL8's / AREA06's / AREA22's / AREA04's), copied and owned here, with:

- **Images.** The 7 recorded eleventh-level RAM images (the ends of
  a13c_00..06, all with AREA13, id 10, resident). Before any case the test
  asserts that each image's overlay text equals the user's
  `extract/OVERLAY/AREA13.BIN`, that the boot text equals the pinned ELF,
  that the boot jump tables 0x26EA00 (001F4190), 0x26EA40 (001F4840),
  0x26D270 (00142070) and 0x26D2A0 (001429D0) equal their ELF bytes and that
  the designed-record area 0x1C00000 is zero; the table loads are the only
  accesses left out of the comparison (the switch encodes them).
- **Callees.** A static check (`callee_checks`, every run) walks every
  entry's reachable words: every call target is a hook or another entry,
  so no original function runs unlisted at the top level of the oracle.
  Callees run as original code (RUN, each first rehearsed with the argument
  registers its hook does not pass poisoned): 001000E0, 001026A0,
  00102850, 001028B8, 001028D0, 00102900, 00102918, 00102948, 00102958,
  001029C0, 00102BB0, 00102C58, 0011DE90, 0011DF78, 0011E2A8, 0011E620,
  001274B0, 00128250, 00128350, 001B1240, 001B12B0, 001B13F0, 001B1470,
  001B15D0. Every other callee is stubbed with scripted results (a
  Scribble writes what the callee would, e.g. 001B2BF0's floor at
  0x700038D0 and hit object at 0x700031D0, 0019A570's hit point, 001B2F70's
  ground height, 001A6440's hit object at 0x700031D4).
- **D_00275B40** is pointed at the node's bone array (node +0x110) in the
  cases of the AREA13 owners and the actors, as the frame loop does.
- **Compared, per case**: as LEVEL10 (the calls and their arguments; memory
  at every call entry; the memory accesses between calls one for one, in
  order, by address, size and changed-or-not; all memory after the last
  store; the return value; the store-log self-check; stops at unmapped or
  misaligned original accesses; every case again from a poisoned start
  image; coverage of every reachable original non-branch word of all 42
  entries; the table's ctx at every call).
- **Header**: `header_checks` (every run): the C hook struct and the
  wrappers against HOOKS.
- **Fail-stop**: `fault_checks` (0x824390 on [44] in a13c_02 with +5 3:
  NULL hook, failing hook, an unmapped address after two calls; 0x8249F0
  for an unmapped address before any call; a latched fault; every entry
  with a latched fault, a NULL hook table, a NULL fault pointer and, for
  the entries with a result, a NULL result pointer) and
  `hook_contract_site` (on passing cases that together reach every hook
  but one (below), the callback and all 42 entries: calls failing, accesses
  refused, each hook NULL / INT32_MIN / 1 / INT32_MAX, `bytes` NULL;
  EM_TEST_FULL=1 every call and every access of each case, the default run
  an even sample of at most 8 calls and 16 accesses per case).
- **Dead words** (`DEAD_WORDS`, each word's instruction class checked, its
  text not reproduced): 00118418's two divide-by-zero traps after the
  divisions by the constant 60; 001F4190's remainder sign fix-up for a
  negative piece index (the index counts up from 0); 00146CE0's second test
  of d +0x7B (ten words: the byte was tested zero just before and nothing
  in between writes it; e +0 aliases d +0x7B only for an odd e, whose
  halfword e +0x34 already stops the original), whose call 0021C040 is
  therefore the one hook the contract does not reach
  (`CONTRACT_UNREACHABLE`); 001CDDC0's fog clamp to 0 (two words: VMAX
  with 0 runs before VFTOI4, and a case with fog.x = -10 confirms the order)
  and one compare of its (u, v) switch that no corner index 0..3 reaches.
- **Access-order audit.** A lane scan (not committed) of the three sources
  for statements reading memory twice in an order C leaves unspecified; the
  ones it found were rewritten with sequenced reads and the test rerun.

### 3.2 Cases

- **Capture** (151): 0x8240E0 on each image; [44]'s steps and 0x8249F0 on
  [44] (a13c_00..02); 0x826610 on the four watchers' matrices (each
  image); [45]'s five step functions on [45] and its block (each image);
  every function of the actor family on both 0x141D20 actors of a13c_05 and
  a13c_06; 00118418 on every live sequencer track of every image.
- **Designed** (1,345, the bound and survivor cases included): every state and sub-state of every function
  with each callee result on both sides of its test, on captured nodes with
  the state bytes patched (a13c_02's [44] and [45], a13c_05's actor) or on
  designed nodes in the free area 0x1C00000. Among them: the y bound 210
  exactly and one float either side; chains of 0, 1, 11, 12 and 13 objects
  with each tested field on both sides; the counts at their bounds (490,
  0x1DF / 0x1F3, 0x5A / 0x96, 0xB4, 1200, 0x7FFF and negative halfwords,
  the threshold 59 / 60, the counter 299 / 300); [45]'s phases with the
  random draws 0, 1, 2, 5, -1; the thrown piece over the kinds 6..11, its
  quit conditions and both probes; 00100130 with the soft compare run and
  stubbed (64-bit results with the high word set); 00118418's broadcast
  and note-on paths with each channel test failing in turn; 0019A6F0 over
  the flags 0..7, the object bits, t0 0x40 / 0x41 / 0x10040 and every
  callee result; 001B2E50 over the counts 0, 1, 2, 3 and -1 and the flag
  patterns; 001B3F10 over the five kinds and six bearings; 001CE660 over
  0..20 samples and two matrices; 001CDDC0 over modes 0..5, two colours,
  six fog vectors (fog.x 300 and -10 among them) and the clipper's n 0 /
  3 / 4; 001E4610 over its kinds 0 / 1 / 2 / 0x80; 001F4840 / 001F4190 over
  every kind 10..27 with the real per-kind records; the actor family over
  the pause byte, the states, every behaviour 0..9 and 0xFF, the
  countdowns at 0 / 1 / 0x8000, the hit counter's bits and health bounds,
  the zones, the probes' results (steered through 001B2BF0's flags and
  floor), the exact-equal facing test, the frame counter's mask and the
  nested frames through 00141D20 -> 00142070 -> 001424C0 -> 00146110 and ->
  00142330 -> 001469B0 -> 001B3F10 -> 0019A6F0.
- **Bound cases** (`bound_cases`, 104, in the designed set): each
  float compare against a constant in the actor family, 001B3F10, 001E4610
  and 0x827F90 with the compared value exactly at the constant and one
  float either side (for the growth to 2 in 001E4610 the three starting
  values that land there were found with the EE float model).
- **The default run's share.** Every capture case, the designed cases a
  coverage pass picked (`DEFAULT_KEEP`: with the capture cases they reach
  every word the whole set reaches, greedy by cost) and the survivor cases.
  EM_TEST_FULL=1 (or EM_LEVEL11_PORT_ALL=1) runs everything. The default
  run therefore proves word coverage (every reachable non-branch word
  executed and compared), not both outcomes of every branch; the bound
  cases other than the survivors run only with EM_TEST_FULL=1.
- **Survivor cases** (`survivor_cases`, always run): section 3.5.

### 3.3 Measured

2026-10-01, M1, host shared with other lanes (load average 12..21 during the
sweep). Default run: 395 cases (151 capture + 244 designed: 235 coverage
picks and the 9 survivor cases), 732 runs (337 poisoned), 5,697 call
entries compared, 2,564 helper register rehearsals, coverage 5,325 / 5,325
reachable non-branch words (DEAD_WORDS left out), the header check, the
callee check, the fail-stop checks, the hook contract (2,371 native runs on
126 cases) and the reuse smoke sample (432 cases) passing; 6.6 s user +
0.6 s system CPU, 3.1 s wall with 4 workers. `EM_TEST_FULL=1`: 1,496 cases
(151 capture + 1,345 designed), 2,823 runs (1,327 poisoned), 36,698 call
entries compared over all memory, 18,459 rehearsals, the hook contract on
442 cases (19,534 native runs, every call and every access), the reuse
check in full (8,736 cases, 333 / 333 reachable non-branch words of
0020D930); 229.7 s user + 1.2 s system CPU, 63.0 s wall. All pass.

### 3.4 Reuse check (the existing translation over the eleventh-level captures)

**0020D930** (`em_menu_hover_0020D930`, `em_menu_hover.c` built unmodified):
the original 0020D930 runs on a13c_04's image (the HEALING page's commit,
the census's first frame f2032) with 001B62C0 stubbed to the designed stick
(magnitude at 0x700038A8, angle at 0x700038AC), the soft-double gate
00128350 / 00100130 / 001274B0 run as original code and 001FB9F0 logged;
`em_menu_hover_0020D930(hover, mode, magnitude, angle)` must give the same
hover byte (t +0x11) and play the cue exactly when the original calls
001FB9F0(5, 0x1000, 0x1000, 0x1000); the original must write nothing but
t +0x11. EM_TEST_FULL=1: 8,736 cases (modes 0, 1, 2, -1, 3, 255; previous
hover 0..6; magnitudes 0, the float below 0.8, 0.8, 1; every table bound
and the floats either side, plus 0, +-pi, +-1), all 333 reachable
non-branch words of 0020D930; the default run a smoke sample of 432.

### 3.5 Mutation sweep (bounded, one round)

Single-edit mutants of the three sources (the float compare macros swapped,
add / sub, signed / unsigned loads, a halfword store made a byte store and
a word store a halfword store, `&&` / `||`, `==` / `!=`, the integer
comparison operators, hexadecimal constants +1 and ^4, small integers +1,
dropped stores), a seeded sample (seed 0xB11, 150 mutants) run through
every designed case (EM_LEVEL11_PORT_ALL=1, contract and reuse skipped)
with `EM_LEVEL11_PORT_SOURCE`. The sweep script is lane scratch (not
committed); it is rerunnable from this description. Before the bound and
survivor cases existed.

- 150 mutants: 128 killed (one of them, a `||` made `&&` in an entry's
  guard, crashed a worker and hung the process pool; it was stopped by
  hand, counted killed), 6 rejected by the compiler under `-Werror` (three
  duplicate case labels, a dropped store that left a variable unused, two
  `||` made `&&` between mutually exclusive equality tests), 16 survived.
- **Real gaps** (8): 001F4190's drift (`+` made `-`: the designed records
  had a zero basis, so the velocity was zero); the bounds 15 and 32 of
  001429D0, 4 of 0x827F90 and 2 of 001E4610 moved by one or four units in
  the last place; 001B3F10's y + 15 and 001464B0's y - 20 with the constant
  one unit higher (at the captured heights the EE's truncating subtraction
  hides that unit); 00146110's floor test made non-strict. The survivor
  cases (`survivor_cases`, run by default) kill all eight (each mutant was
  rebuilt and rerun on them: every one fails, the real translation
  passes): a basis at +0xD0 for the drift; the next float above 15, 32 and
  4; a starting growth (0x3FFFDDE1, found with the EE float model) that
  lands one float above 2, and one (0x3FFFDDDF) that lands exactly on it;
  the heights 0 and 30 for the two constants; the floor exactly at y + 15.
  The bound cases (section 3.2) were added at the same time for every
  other float compare against a constant in the same functions.
- **Equivalent survivors** (8, argued from the code): three array sizes in
  parameter or local declarations made one larger (the extra element is
  never read); bit 0 of a byte read signed instead of unsigned; a word read
  signed or unsigned where it is only compared with 0 / 1 or switched on
  0 / 1 (two mutants); bit 0x2000 of a halfword read signed instead of
  unsigned; 001F4190's death tick read unsigned before the subtraction into
  a signed value (the same bits).

## Binding

Nothing calls these entries. To run them live a host must:

- supply `bytes` over the scene's original-byte storage (the actor pool,
  the player block, the AREA13 overlay's data (the scripts, the areas
  0x82E140 / 0x82D330, the points 0x82D1B0, the colours 0x82C9E0 / 0x82D3B0,
  the packets 0x82D3C0 / 0x82D450 / 0x82D4E0, the step table 0x82D190),
  [45]'s work block behind D_00275CA8, the story bytes, the bone arrays, the
  scratchpad (0x70003190.. probe records, 0x700036A0.. matrices,
  0x700038A0.. vectors, 0x70003AC0 camera rows, 0x70003B68 / 0x70003B8D),
  the boot tables (0x245960 / 0x2459B0 joints, 0x245A60 / 0x245AA0 areas,
  0x245AE0 zone records, 0x24D720.. radii, 0x25A350 effect records,
  0x2753F0 thresholds, 0x242630 pitches), the sequencer channels 0x27CCC0
  and stream behind 0x281AC0, the clipper buffers 0x8112C0 / 0x8117C0, the
  renderer context behind D_00275670 and the stack frame below `sp`), and
  point D_00275B40 at the running actor's bone array as the frame loop
  does;
- **bind the ninth level's hook slots** (`em_level9_port.h`, LEVEL9_PORT.md
  "Binding"; this lane does not edit em_level9_* files): its w_00824390,
  w_00824520 and w_00824960 (0x823E90's step switch for D_008107F4 & 0xF
  2 / 3 / 4) to `em_level11_port_00824390` / `00824520` / `00824960` with
  the same self; its w_00826610 (0x824BB0's call with the matrix rec_b +
  0x90) to `em_level11_port_00826610` with the original sp at that call;
  its w_callback, where 0x827150 calls the word of the step table 0x82D190
  indexed by the block's step (0x827C30, 0x827DD0, 0x827E00, 0x827F20,
  0x827F90), to the matching entry with the same self (0x827C30 also needs
  sp); the script interpreter's op-9 callbacks (records 0x82B550 /
  0x82B690 / 0x82BB10) to 0x8246D0 / 0x8248C0 with (a0, self, blk);
  0x828500's collision method (+0x34 = 0x8284E0) to 0x8284E0;
- bind the eighth level's hooks w_001B2E50 (`em_level8_port_probe.c`) and
  w_001B3F10 (`em_level8_port_creature.c`) to `em_level11_port_001B2E50` /
  `001B3F10` (the latter with the original sp);
- run the 0x141D20 actors (spawned by the group 0x82A230 at [7]'s end) with
  00141D20 as their node behaviour (+0x10); the spawned effects with
  001F4840 / 001E4610 as theirs; the thrown piece and the effects 0x828500
  / 0x828C60 / 0x828E10 / 0x828F40 as theirs (their spawner was not traced:
  AREA13_OVERLAY.md, "Unreached code"); 00118418 from the sequencer's event
  loop; 00100130 where the soft-double compares call it; 0020D930 through
  `em_menu_hover_0020D930` with the stick sampled by 001B62C0's
  translation and the cue 001FB9F0 played on its result 1;
- bind each hook to a verified port translation or a fail-stop stand-in.
  Port entries already exist by address for many hooks (for example
  `em_level8_port_00131ED0` / `001B1560` / `001B2B10` / `001B2BF0` /
  `001B32F0` / `001B3440` / `001B3580` / `001B4810`, `em_pose_host_001C63E0`
  / `001C67E0` / `001C68C0` / `001CA1C0`, `em_coll_move_0019AD00`,
  `em_coll_segment_0019A570` / `0019D330` / `001A0B10`,
  `em_coll_grid_hull_001A6440` / `001A7280`, `em_shadow_actor_route_001F8D30`,
  `em_shadow_decal_001CB950` / `001CF470`, `em_packet_chain_001CB5F0` /
  `001CB900`, `em_sdk_soft_float_001274B0` / `00128350`); their
  verification status is their own lanes', not re-checked here. The stubbed
  callees (every hook not in the RUN list above) are not verified here,
  and the helpers run as original code need port translations paired by
  address. The AREA13 hook 0x824060 (the roof area test) has no
  translation (no census row of any level: ELEVENTH_LEVEL_ROUTE.md section
  7 item 4); 001434C0, 00143610, 001437E0, 00143AF0, 00144040, 00144C20
  (behaviours 3..8), 001450B0 and 00145850 (states 2 / 3) ran in no
  census and have no translation.

## Known gaps

- **Live evidence.** The captures are end-of-beat states: most paths are
  designed cases on the captured RAM with state bytes patched. [44] is
  freed after a13c_02 and [45] sits in its last phase from a13c_03 on, so
  their earlier steps, the thrown piece and the effects run only on
  designed states; the actor family's behaviours 1 and 2 are captured
  (a13c_05), 0 in a13c_06.
- **Census lower bound.** The census sets are those of the route shape, not
  of the exact frames (ELEVENTH_LEVEL_ROUTE.md section 5: a13c_05's replay
  walked a different path); functions run only off this route are not
  rows here.
- **Stubbed callees' inputs.** Only the RUN helpers are proven not to read
  a register their hook does not pass; the other hooks' argument lists were
  taken from the decomp's C of the callees and the call sites (and earlier
  lanes' HOOKS tables), and their results are scripted beyond their real
  ranges.
- **0019A6F0's skipped call** (section 0): the register's value on that
  path is measured nonzero, not proven nonzero for every input.
- **Dead words** are argued from the code (section 3.1), not executed.
- **What the harness cannot see** (as LEVEL10): whether an access that
  changed nothing is a load or a store of the same value; hardware
  behaviour past a stop; the frame bookkeeping.
- **Float proofs rest on the EE / VU0 model** (`em_ee_float.h`).
- **Mutation sweep** is a bounded sample, not exhaustive.
