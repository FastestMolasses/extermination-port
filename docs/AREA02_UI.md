# AREA02 lane UI (L4UI)

Lane L4UI of the level-4 side track (AREA01 revisit and AREA02), 2026-09-28.
The census delta (`../Extermination/build/s87/census/a02_delta.json`,
FOURTH_LEVEL_ROUTE.md section 9) lists 18 new boot functions whose
subsystem labels are ui_screens, ui_menu_lib and draw2d (18,240 bytes). The
labels are only labels; the roles below come from the code and the
captures. All 18 run only in the two side beats that open the status
screen: a01r_s0 (the event's pickup, whose take opens a page of the status
screen) and a02_s0 (the bed, where the healing page's use-item prompt
spends item 0x20).

| addr | bytes | decomp source | first beat (census frame) | port translation |
|---|---:|---|---|---|
| 00208750 | 860 | NEARMISS | a02_s0 (f622) | **this lane**, `em_area02_ui_00208750` |
| 00208AB0 | 24 | inline asm | a01r_s0 (f287) | **this lane**, `em_area02_ui_00208AB0` |
| 00209860 | 1412 | NEARMISS | a02_s0 (f622) | reused: `em_status_draw.c` (`em_status_ammo_draw`) |
| 00209DF0 | 2480 | byte-matched C | a02_s0 (f622) | **this lane**, `em_area02_ui_00209DF0` |
| 0020AC70 | 456 | byte-matched C | a01r_s0 (f287) | reused: `em_item_trail.c` |
| 0020BF20 | 3468 | byte-matched C | a01r_s0 (f287) | reused: `em_status_pages_spr4.c` |
| 0020E1E0 | 112 | byte-matched C | a02_s0 (f621) | reused: `em_status_scene_original.c` |
| 0020E250 | 328 | byte-matched C | a02_s0 (f621) | reused: `em_status_scene_original.c` |
| 0020E3A0 | 188 | byte-matched C | a02_s0 (f621) | reused: `em_status_scene_original.c` |
| 0020E460 | 652 | NEARMISS | a02_s0 (f622) | reused: `em_status_scene_original.c` |
| 0020E6F0 | 1420 | byte-matched C | a02_s0 (f622) | reused: `em_status_scene_original.c` |
| 0020EC80 | 464 | byte-matched C | a02_s0 (f623) | reused: `em_status_scene_original.c` |
| 00211970 | 2088 | byte-matched C | a01r_s0 (f286) | reused: `em_status_pages_spr4.c` |
| 002121A0 | 1028 | byte-matched C | a01r_s0 (f287) | reused: `em_status_pages_spr4.c` |
| 002125B0 | 1444 | byte-matched C | a01r_s0 (f287) | reused: `em_status_pages_spr4.c` |
| 00212B60 | 968 | byte-matched C | a01r_s0 (f287) | reused: `em_status_pages_spr4.c` |
| 00212F30 | 640 | byte-matched C | a01r_s0 (f287) | reused: `em_status_pages_spr4.c` |
| 00215FE0 | 208 | byte-matched C | a02_s0 (f619) | reused: `em_status_pages_item.c` |

New files: `src/game/em_area02_ui.h`, `src/game/em_area02_ui.c`,
`tools/test_area02_ui_reference.py` and this document. Nothing is wired and
no tracked file was edited.

**Why these are "new" at level 4.** The first-level route census never
opens the status hub or the SPR4 / healing pages on the route (the port's
status work was verified on the separate status-hub and route captures,
STATUS_HUB.md, STATUS_PAGES.md, STATUS_SCENE.md), so the census counts them
first here. They are the same boot code the first level's status screen
runs.

**Overlap with existing port files** (grep of src/ and tools/ for every
address, before translating):
- 15 of the 18 have verified translations with original-instruction tests;
  section 2 lists them and what this lane re-checked over the level-4
  captures.
- 00209DF0 had none. The live hub draws it through `em_status_hub_ui.c`,
  which replays records that `tools/export_status_hub.py` exported by
  *executing* the original 00209DF0 over the status-hub capture (for each
  hover and the terminal-infection branch), with the health, battery,
  ammunition, infection figure and trail parts rebuilt at prepare time
  (STATUS_HUB.md). That is an export, not a translation of the code; the
  translation here is.
- 00208750 had none: the same exporter runs the original into a scratch
  packet context and stores the resulting nine line strips per marker.
- 00208AB0 had none; `em_item_trail.c` covers the fan packets of 001D66A0
  that 0020AC70 reaches through it, and lane misc
  (`em_area02_misc.c`, `em_area02_misc_001D66A0`) translated 001D66A0
  itself.

## 1. The three translations

Contract: the AREA01 UI lane's (AREA01_UI.md section 1, `em_area01_ui.h`,
reused unchanged): the state is an `EmArea01Ui`; every original byte is
reached by its original address through `core.world` (EE RAM, the
scratchpad at 0x70000000, the EE stack); every callee outside this lane
goes through `call` by original address with the integer argument
registers as 64-bit images and the float argument registers as binary32
bits; each routine carves its original frame below `sp` (00209DF0 0xE0,
00208750 0x150, 00208AB0 none), so the stack address 00209DF0 hands
00208750 is the original's; the fail-stop latch is sticky (code 1 `call`
NULL, 2 a callee failed, 4 an address no view covers, 6 below); every COP1
operation goes through `em_ee_float.h`. The header `em_area01_ui_internal.h`
supplies the memory and call helpers.

### 00209DF0(ui), the status hub's 2D layer

`ui` is the status block D_00810130; its byte +0x11 is the hub's hover.
In order:

1. Blend mode 0 (`00207D00(1, 0)`).
2. Four marker glows. For marker k = 0..3 the routine writes three float
   (x, y) rows (the third and fourth word of each row zero) at sp + 0xA0 and
   calls 00208750(16, sp + 0xA0, colours), with the selected colour rows
   D_00265570 when the hover byte (read again for each marker) is k + 1,
   else D_00265540. Markers 0 and 2 are vertical (fixed x, three y), 1 and
   3 horizontal.
3. The analog trail: the scratchpad words 0x700038A0 / A4 = 432.0 / 272.0,
   then 0020AC70(ui, 0x700038A0, 0), then blend mode 0.
4. The wheels: the descriptors D_002651B0 and D_00265210 get the centre and
   the start / end angles -44 / 44; four iterations each draw both
   (002082B0(1, descriptor)), add 90 to the four angles (load, EE add,
   store, one at a time), write the (x, y) of the three descriptors
   D_00265270 / D_002652D0 / D_00265330 from the iteration's extents
   (16 * (w + 0x700), 16 * ((h >> 1) + 0x790); the extents per iteration are
   the switch's four pairs), set eight colour words of the third
   descriptor D_00265330 (0x60 bytes, 0x265330..0x26538F: +0x24 / +0x28,
   +0x34 / +0x38, +0x44 / +0x48, +0x54 / +0x58, stored as the addresses
   0x265354..0x265388) to the highlighted values when the hover is nonzero
   and equals the iteration + 1, else to the normal ones, and draw the
   three.
5. The readouts: 00208AD0(ui, 0xD0, 0xC4) (health), 00209280(ui, 0x10,
   0x76, texture word, 0) (battery), 00209860(ui, 0x10, 0xBE) (ammunition).
6. The infection figure: n = float_to_int(D_0081085C) (the 64-bit result
   image compared with 100). At 100 one label call 001CC1E0 with the
   string D_00267290 and the style D_00265520; otherwise the label
   D_00267294 without a style, then 001C5FB0(n, 3, 1), 00123168 into the
   scratch string D_002862C0, 00122EF0 appending D_00273570, and 001CBA50
   draws it.
7. Blend 3, four 32x32 sprites; blend 0, two larger sprites; four text
   calls (001CBA50) with the string pointers D_002672A4..B0.
8. Three dots: four float_to_int conversions each, in the order x0, y0,
   x1, y1 (their results passed to 00207F80 as the EE register images),
   then blend 0 and one full rectangle.

**Code 6, the inherited texture word.** 00209860 with D_00810CA4 != 2 and
D_00810CA6 > 4 draws its secondary row with its caller's saved register s0
as the sprite's texture word (the decomp's 00209860 source notes this; the
status draw lane rejects those selectors, STATUS_HUB.md). Called from
00209DF0 that register is the wheel loop's counter, 4: the test executes
the original 00209DF0 with CA4 = 0, CA6 = 5 over the a01r_03 image and
records the texture word of the last 00207E40 call made from inside
00209860: it is 4 (a TEX0 of 4, which is no asset). The call interface does
not carry s0, so the translation latches code 6 (detail 0x209860) at that
call instead of calling 00209860; the calls before it are made. No level-4
capture holds such selectors (every a01r / a02 image holds CA4 = 0xFF,
CA6 = 0).

### 00208750(n, xy, rgb), the marker glow

Frame 0x150. The 0x48-byte block D_00265160 is copied once to sp + 0x100
(four quadwords loaded before the first store, then the last doubleword).
It holds nine (dx, dy) integer pairs: eight copies displaced horizontally
or vertically, in pairs at decreasing distances, and the undisplaced copy
last. For each pair, in order, the routine builds one packet on slot 1 of
the render context D_00275670 (the cursor word at +0x14, loaded again
before each header store): byte +3 = 0x10, word +4 = 0, halfword +0 =
2n + 2, cursor += (2n + 3) * 16; then quadword +0x10 = 0, word +0x1C =
(2n + 1) | 0x50000000, doubleword +0x20 = GIF tag (0x20254000 in the upper
word, 0x8000 | n below, n sign-extended), +0x28 = 0x41, then n vertices
of 32 bytes.

t starts at 0 **for every copy** and steps by 1 / (n - 1) (EE div,
computed for every copy, also for n <= 1). With u = 1 - t the weights are
u*u, 2*(u*t) and t*t (a quadratic Bezier). Each vertex:
- words 0..3: float_to_int of the weighted sum of the three **integer**
  colour rows at rgb, rgb + 0x10, rgb + 0x20 (lane k, converted with
  cvt.s.w); all four halved by an arithmetic shift (read back from the
  packet) when dx or dy is nonzero, so the eight displaced copies are half
  bright;
- words 4 and 5: float_to_int(d + the weighted sum of the three **float**
  rows at xy, xy + 0x10, xy + 0x20, lane 0 (x) or 1 (y)), d = dx or dy
  converted to float and added first;
- word 6 = 0xFFFFFF, word 7 = 0.

Each weighted sum is the EE's ADDA of the first two products and then MADD
with the third. With n <= 0 the headers are still written and the cursor
still advances; no vertex is made and nothing is called (a NULL `call` is
then not a fault; with n > 0 it faults before any store).

**The decomp's NEARMISS C of 00208750 differs from the instructions** (the
decomp's comment and C say otherwise; this lane does not edit the decomp):
three points: t is reset to 0 for each of the nine copies (the C sets it
once before the copy loop and never resets it); the weights are built from
u = 1 - t, not from (vertex index - t); and the xy rows are read as floats
(the C converts them from integers). The C already computes the step
1 / (n - 1) inside the copy loop, as the instructions do; that part is
right. The C's comment also calls the four colour words "XYZ"; they are
the colour lanes of the integer rows rgb + 0 / + 0x10 / + 0x20. The
instructions were read and the test confirms each of the three points
(section 3: mutants 1-3).

### 00208AB0(a0, a1, a2) with f12

A tail jump to 001D66A0(1, a0, a1, a2) with f12 unchanged: the three
argument registers move up one place as full register images and a0
becomes 1; v0 is 001D66A0's. 0020AC70 calls it once per trail record with
(position vector, colour words, D_00273580, angle).

## 2. The reused translations, re-checked over the level-4 captures

The level-4 images (`../Extermination/build/s87/`, eeMemory.bin +
scratchpad.bin at the end of each beat):

| name | beat | why |
|---|---|---|
| r1 | route_a01r/a01r_01_event | the source of a01r_s0; item 0x20 held; B1 = 0x20 (the item page the event opened) |
| r1s0 | route_a01r/a01r_s0_pickup | the pickup's end: status block t[4] = 9 (the SPR4 page's take notice), B1 = 0x10, D_00810CB4 = 90 |
| r3 | route_a01r/a01r_03_door16 | the AREA02 arrival, the source of a02_s0: health 90, infection 40, item 0x20 = 1 |
| bed | route_a02/a02_s0_mts_bed | after the bed: D_008106D0 = the bed's record 0x7A5F10, item 0x20 used |
| a4 | route_a02/a02_04_panel | battery charge 0, infection 60 |

What the reused modules were re-run on:

- **em_status_pages** (00211970 with 002121A0 / 002125B0 / 00212B60 /
  00212F30 / 0020BF20; 002160B0 with 00215FE0), through the status pages
  lane's harness with the same oracle method: its 583 designed cases for
  these routines, retargeted from the status-hub image to the five level-4
  images in turn, plus the real level-4 call shapes: the pickup's take
  (B0 = 1, B1 = 0x10..0x16) opening 00211970 on r1, 00211970 in its
  captured state 9 on r1s0 with three pad values, HEALING opened on item
  0x20 (B0 = 1, B1 = 0x20) with the captured inventory on r3 and r1, and the
  bed's use-item request (B0 = 4) with the **real bed record** (D_008106D0
  as captured on the bed image) and the counts as they were before the use
  (item 0x20 = 1, infection 40, health 90), plus nine pad frames of the
  page with the item-0x20 entry. 605 cases in all (one retargeted case is
  skipped because the original itself cannot complete it on that image,
  section 3).
- **em_status_draw** (00208AD0 health, 00209280 battery, 00209860
  ammunition): each image's own health, warning byte, clock values, battery
  charge / capacity / equipped byte (both layouts) and ammunition inventory
  against the original over that image (the status draw test's structures
  and command capture): 40 cases, 454 ordered commands, all equal. The
  level-4 values stay in that module's supported domain (CA4 = 0xFF, CA6 =
  0: the secondary row is skipped).
- **em_status_scene_original** (0020E250, 0020E3A0, 0020E1E0, 0020E460,
  0020E6F0, 0020EC80): its inputs are the equipment bytes D_00810CA4..A7
  (the letters), D_008104E4 and D_00810C60 (variant and costume). In all
  five level-4 images they equal the status-hub capture's (FF 05 00 07, 0,
  0; the test checks the 15 values), so the level-4 hub builds the same six
  glyphs and the same menu player as the first level, which
  `make test-status-scene-reference` executes against the original (and
  the exported glyph and player models cover). Not re-executed over the
  level-4 images.
- **em_item_trail** (0020AC70): its inputs are the analog stick bytes and
  the shared ring; `make test-item-trail-reference` covers them and
  `make test-status-hub-ui-reference` the hub base (432, 272). In this
  lane's 00209DF0 cases 0020AC70 is a callee: the full run executes it as
  original code on both sides, the default run stubs it (entry-checked),
  as the status pages lane does.

## 3. Verification

`python3 tools/test_area02_ui_reference.py` (no make target: the Makefile is
not this lane's file; binding adds one).

Method (the status pages lane's harness, imported and extended; that is
the AREA01 UI lane's): the oracle runs the original routine over a level-4
image, every callee outside the lane as original code, nested, its entry
logged (address, sp, the 64-bit integer argument registers it takes, the
float argument registers); the native translation runs over its own copy,
each callee it reaches through `call` running the same original code in a
second interpreter sharing the native memory with all other registers
poisoned. At every callee entry all 32 MiB of RAM and the 16 KiB
scratchpad equal the oracle's; at the end the call log, RAM and
scratchpad are identical (and v0 for 00208AB0).

Cases:
- **00209DF0** (160): every hover 0..5 and 0xFF, twelve infection values
  (captured, 0, 40, 60, the last float below 100, 100, just above 100,
  99.5, -1, 1000, -0, 100.9) and eight selector pairs (captured, primary 2
  over an invalid secondary, the valid secondaries, and two invalid pairs
  that must fault code 6 after exactly the oracle's calls before 00209860)
  on every image, plus 40 random combinations with health values.
- **00208750** (188): n = 16, 1, 2, 3, 0, -1, -3, 5 with the four markers'
  rows and both colour tables on two images, and 60 random cases: float
  rows with specials (infinities, NaN, the largest float, denormal, -0,
  2^31 edges), integer colour rows with sign and range edges, the offset
  table perturbed (zero and nonzero pairs), rows on the scratchpad, the
  packet cursor moved to a free area.
- **00208AB0** (22): trail-shaped position / colour / angle arguments and
  upper register halves.
- **Faults** (native only): NULL `call` (code 1, nothing written), a
  latched fault (-1, nothing written, no call), no views (code 4; 00208AB0
  reads no memory itself and makes its call), a failing callee at the
  first, second and last call position (full run: a spread of every
  position) (code 2, no call after it, the fault names the callee), and
  00208750 with n = 0 and a NULL `call` (no fault).

Every conditional branch of the three routines is taken both ways except
one: 00209DF0's wheel switch tests i == 0, 2, 1, then 3, and i is the loop
counter 0..3, so the last test never fails (0x20A1D4 not taken).

**Default run** (2026-09-28, M1, 4 workers): about 10 s wall, 22 s CPU
(`EM_TEST_JOBS=1`: about 23 s): 13
of 160 00209DF0 cases, 31 of 188 00208750, 6 of 22 00208AB0, 40 of 605
reuse em_status_pages, 18 fault cases, 40 em_status_draw cases, 15 scene
input checks, the s0 measurement; 35 of 36 outcomes (the 36th is the
unreachable one). Quick mode memoises / replays the heavy callees as the
status pages lane does, and additionally float_to_int (keyed on f12 and
sp, its only argument register), 002082B0, the three readouts and the
sprite / rectangle / label / string callees; `EM_TEST_FULL=1` runs every
case with every callee as original code on both sides.

**Full run** (`EM_TEST_FULL=1`, 2026-09-28, M1, 4 workers: 547 s wall,
1,118 s CPU): PASS. 160 00209DF0, 188 00208750 and 22 00208AB0 cases, 605
reuse em_status_pages cases (604 run, 1 skipped, below): 974 oracle cases,
612,700 callee calls, every one with RAM, scratchpad and arguments equal to
the original's at entry; 64 fault cases; 40 em_status_draw cases; 15 scene
input checks; 35 of 36 branch outcomes (the 36th unreachable).

One retargeted reuse case is skipped: a HEALING state-4 case designed on
the status-hub image (002160B0 with t[5] = 4 and Cross) follows the
interaction pointer, which is 0 on the r3 image, so the **original**
reads address 0xA, which no mapping covers; the test skips such a case
only when it is a retargeted one and the original cannot complete it (the
level-4 call shapes must all complete, and do).

**Mutation sweep** (`EM_TEST_JOBS=1 python3 tools/test_area02_ui_reference.py
mutants`, the review's bounded sweep over the default lane cases, about
2.5 min). Each mutant is written to its own source file and built into its
own library (`build/area02_ui/area02_ui_mut_<nn>_<name>.dylib`): macOS
dlopen returns the image already loaded from a path, so the first version
of this sweep, which reused one path, ran mutant 1 for every later mutant
and its "22 of 22" was not evidence (review finding). The sweep now runs
an unmutated control first (it must survive, and does) and asserts that no
library path is loaded twice.

Result: 22 single-operation mutants of `em_area02_ui.c`, all killed: t not
reset per copy, u = t, xy rows converted as integers, halving on dx and dy
both nonzero, logical instead of arithmetic halving, the offset subtracted
instead of added, the middle weight u*t not doubled, the halfword count,
the GIF tag's n zero-extended, the step from n instead of n - 1, vertex
word 6, the marker hover compare off by one, the wheel highlight compared
with the hover instead of hover - 1, one wheel extent, the infection
compare, the formatter width, the code-6 gate, the dots' conversion order,
the trail base, the angle step, 00208AB0's slot argument and its dropped
f12.

**Equivalent mutants** (they survive every case because they cannot change
behaviour; three were in the invalid first sweep and were replaced by the
offset-subtracted, undoubled-weight and wheel-off-by-one mutants above,
three came from the review's sweep):

- *Offset added last* (`ui_fadd(sum, off)` for `ui_fadd(off, sum)`): the
  EE single add is commutative (the same rounded sum of the same two
  operands, including the EE's clamp of overflow and its flush of
  denormal inputs, applied to each operand alike).
- *ADDA operand swap* in the colour or xy blend: the same argument as the
  add; the review found 0 asymmetries of `em_ee_add_bits` over 50 million
  random and special-value pairs.
- *MADD operand swap* (`em_ee_madd_bits(acc, w2, c2)`): the product term
  is a single commutative multiply before the accumulate.
- *MADD as a separate multiply and add* on the integer colour path: MADD
  (`em_ee_madd_bits`) accumulates the raw product, while MUL then ADD
  first saturates an overflowing product to +-max and flushes a denormal
  one. Neither case is reachable here: the colour lane is an integer
  converted to float (0 or |c| >= 1, |c| <= 2^31) and w2 = t * t is 0
  (t = 0 at the first vertex) or at least (1 / (n - 1))^2 >= 2^-62 and at
  most about 1 (t steps from 0 by 1 / (n - 1), n - 1 steps per copy; with
  n = 1 the one vertex has t = 0), so every product is 0 or a normal
  number far from overflow, and the two paths give the same bits.
- *Wheel highlight without the nonzero test* (`i == hover - 1u` alone):
  with hover = 0, hover - 1u is 0xFFFFFFFF, which never equals the
  iteration i in 0..3, so the nonzero test changes nothing.
- *00208750 reading (dx, dy) from D_00265160 instead of its stack copy*:
  nothing writes the table between the block copy at entry and the reads
  (the only stores in between are the packet header / vertex stores into
  the render context's packet arena and float_to_int's own frame), so the
  table and the copy hold the same words at every read.

## 4. Binding

Nothing is wired. What a binder needs:

- **00209DF0** replaces the export replay of `em_status_hub_ui.c` as the
  hub's DRAW once its callees are bound: 00207D00 / 00207E40 / 00207F80 /
  001CBA50 / 001CC1E0 to the 2D layer (`em_page_draw` style, the calls
  the AREA01 UI lane's `leaf_calls` mode routes), 002082B0 to
  `em_item_geometry_arc`, 00208AD0 / 00209280 / 00209860 to
  `em_status_draw` (their argument registers carry the same values that
  module takes, read from the same globals), 0020AC70 to `em_item_trail`,
  float_to_int 001281C0 and the string workers 001C5FB0 / 00123168 /
  00122EF0 to their translations. 00208750 is called directly (this lane);
  its packets land on slot 1 of the render context, so a binder that draws
  from the render context's packet arena (as the original) needs nothing
  more, and one that consumes calls must read the nine strips back (the
  exporter's layout).
- **00208AB0**'s one callee 001D66A0: `em_area02_misc_001D66A0` (lane misc)
  or the fan path of `em_item_trail`.
- The code-6 selectors never occur on the recorded routes; a live binding
  keeps the fault (fail-stop), not a stand-in texture.

## 5. Known gaps

- The level-4 captures are end-of-beat images: no capture shows the hub,
  the SPR4 page or the healing page open (the pages' states were rebuilt on
  the images by designed inputs, as the status pages lane does with the
  AREA11 images). The bed's page ran with the real bed record and the
  pre-use counts restored by the case, not from a mid-page capture.
- The status scene translations were not re-executed over the level-4
  images; their inputs were checked equal to the first-level ones.
- The code-6 path (invalid ammunition selectors) is refused, not
  reproduced: the original's result there is a sprite with texture word 4.
- The final GS pixels of the glows, arcs and text are the renderer's
  boundary, as for the rest of the status UI (STATUS_HUB.md).
