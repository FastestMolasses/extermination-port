# Status pages (lane status_pages)

Date: 2026-09-27. A standalone translation lane for the first level
(AREA11). It covers the status pages the player can open there that had no
native translation, and it removes the page fail-stops from the port's side:
- the status hub's DATABASE, SPR4 and MAP pages;
- the ITEM root's EQUIPMENT, EVENT and HEALING children;
- the pages the non-battery item takes open (types 0x1E/0x1F, 0x10, the key
  0x32 and the map 0x08).

Nothing is wired and no tracked file was edited. The binding is the chain's
job (section 5).

Files (all new):

| File | What |
|---|---|
| `src/game/em_status_pages.h` | the public entries (prefix `em_status_pages_`) |
| `src/game/em_status_pages_internal.h` | shared helpers (on top of `em_area01_ui_internal.h`) |
| `src/game/em_status_pages_helpers.c` | MAP helpers 00211240 / 00211310 / 002117D0; DATABASE helpers 00213A00 / 00213C50 / 00213CC0 / 001FCF30 |
| `src/game/em_status_pages_item.c` | ITEM children 00214570 / 00215870 / 002160B0, 00215FE0, list helpers 0020BBE0 / 0020BC50 |
| `src/game/em_status_pages_spr4.c` | SPR4 page 00211970 and 002121A0 / 002125B0 / 00212B60 / 00212F30 / 0020BF20 |
| `src/game/em_status_pages_parts.c` | the five SPR4 part pages 00218D90 / 00217090 / 00218640 / 002177B0 / 00217FA0 |
| `tools/test_status_pages_reference.py` | the original-instruction oracle (section 3) |

It builds privately under `build/b15/status_pages/` (the test compiles its
own dylib with `-Wall -Wextra -Werror -Wpedantic -ffp-contract=off`).

## 1. Which pages the first level can reach

This section is read from the instructions: the dispatch code of 0020CDC0,
0020EE50 and 00211970 (no labels were used). A take posts B0 = D_008106B0
and B1 = D_008106B1, and 0020CDC0 case 0 maps them to a page id t[0x10],
where t is the status block D_00810130. On the hub, Cross enters a page by
the hover t[0x11]: 1 goes to page 3, 2 to page 2, 3 to page 1 and 4 to
page 0. Phase 3 then loads the page module (0x1F, 0x1E, 0x2C, 0x24, 0x25 or
0x26 for pages 0..5) and calls the page every frame.

**0020CDC0 case 0, the cold entry, in the order the instructions test.**
Before the tests it clears t[2..5]; t[0x15] keeps whatever it held unless
a branch below stores it. Every branch with a request sets t[1] = 3.

| Test | Page t[0x10] | t[2] / t[4] | t[0x15] |
|---|---|---|---|
| B0 = 6 (00225A00 is called first) | 0 ITEM | 2 / 2 | 3 (BATTERY) |
| B0 = 5 | 2 SPR4 | 0 / 0 | unchanged (00211970 state 0 reads request 5) |
| B0 = 4 | 0 ITEM | 2 / 2 | 5 (HEALING) |
| B0 = 1, B1 & 0xC0 nonzero (tested first) | 0 ITEM | 2 / 2 | 3 (BATTERY) |
| B0 = 1, B1 0..4 | 2 SPR4 | 2 / 2 | 6 (LOWER U.R.S.) |
| B0 = 1, B1 5..6 | 2 SPR4 | 2 / 2 | 2 (UPPER U.R.S.) |
| B0 = 1, B1 7..9 | 2 SPR4 | 2 / 2 | 5 (SCOPE MOUNT) |
| B0 = 1, B1 0xA..0xE | 2 SPR4 | 2 / 2 | 3 (MULTIPLE ATTACHMENT) |
| B0 = 1, B1 0xF | 2 SPR4 | 2 / 2 | 4 (SELECTOR SWITCH) |
| B0 = 1, B1 0x10..0x16 | 2 SPR4 | 0 / 0 | unchanged (state 0 takes it to the notice, state 9) |
| B0 = 1, B1 0x17..0x1A | 0 ITEM | 2 / 2 | 1 (EQUIPMENT) |
| B0 = 1, B1 0x1B..0x1D | 0 ITEM | 2 / 2 | 3 (BATTERY) |
| B0 = 1, B1 0x1E..0x22 | 0 ITEM | 2 / 2 | 5 (HEALING) |
| B0 = 1, B1 0x23..0x3F | 0 ITEM | 2 / 2 | 4 (EVENT) |
| B0 = 2 | 1 MAP | 0 / 0 | unchanged |
| B0 = 3, or any other nonzero B0 | 3 DATABASE | 0 / 0 | unchanged |
| B0 = 0, C5 = D_008106C5 = 2 | 5 passcode | 0 / 0 | unchanged |
| B0 = 0, C5 nonzero and not 2 (0xFF included) | 4 passcode | 0 / 0 | unchanged |
| B0 = 0, C5 = 0 | none: t[1] = 1, the hub | 0 / 0 | unchanged |

The B1 compares are signed byte compares after a zero-extending load, and
B1 below 0x40 is the only range left once bit 0xC0 is tested. The SPR4
t[0x15] values are the part-page hovers 00211970 state 2 maps to modules
(6 -> 0x2D, 2 -> 0x2E, 5 -> 0x2F, 3 -> 0x30, 4 -> 0x31).

| Page | Function | How AREA11 reaches it | Status |
|---|---|---|---|
| 0 ITEM | 0020EE50 | hub hover 4; B0 = 1 with B1 & 0xC0 or B1 0x17..0x3F; B0 = 4; B0 = 6 | `em_item_root` (existing) |
| ITEM > EQUIPMENT (0x20) | 00214570 | ITEM hover 1; takes 0x17..0x1A | **translated here** |
| ITEM > BATTERY (0x21) | 002149F0 | ITEM hover 3; takes 0x1B..0x1D or with B1 & 0xC0; B0 = 6; the panel | `em_spr_002149F0` (existing) |
| ITEM > EVENT (0x22) | 00215870 | ITEM hover 4; takes 0x23..0x3F | **translated here** |
| ITEM > HEALING (0x23) | 002160B0 | ITEM hover 5; takes 0x1E..0x22 (AREA11 has 0x1E/0x1F) and B0 = 4; both set t[0x15] = 5 | **translated here** |
| 1 MAP | 0020F950 | hub hover 3; the map take (B0 = 2) | `em_area01_ui_0020F950` (AREA01 lane), its helpers here |
| 2 SPR4 | 00211970 | hub hover 2; B0 = 1 with B1 < 0x17 and no bit 0xC0 (0x10..0x16 open state 9's notice; B1 < 0x10 opens a part page directly through t[0x15]); B0 = 5 (the refill) | **translated here** |
| SPR4 part pages | 00218D90, 00217090, 00218640, 002177B0, 00217FA0 | SPR4 hovers 6, 2, 5, 3 and 4 (modules 0x2D, 0x2E, 0x2F, 0x30, 0x31) | **translated here** |
| 3 DATABASE | 00214020 | hub hover 1; the key take (B0 = 3; any B0 other than 0, 1, 2, 4, 5, 6 too) | `em_area01_ui_00214020` (AREA01 lane), its helpers here |
| 4/5 passcode | 002072C0 | B0 = 0 and D_008106C5 nonzero: 2 gives page 5, any other value (0xFF included) page 4 | not reachable in AREA11 (see below) |

**Not reached in AREA11, and not translated:**
- The passcode pages. The boot ELF's byte stores to D_008106C5 are:
  - 0xFF in the SPR4, BATTERY and EVENT pages (00211970, 002149F0,
    00215870). It is the close signal: a later 0020CDC0 frame that sees
    C5 = 0xFF moves the screen to its closing phase (t[1] = 5);
  - zero in 0020E080. 0020CDC0's closing phase 5 runs the close routine
    0020E0C0 every frame, and its last step calls 0020E080 (phase 6 also
    calls 0020E080 directly). 0020E080 clears C5 and B0 together, so a
    0xFF left by a page does not reach the next cold entry;
  - the keypad callback 00207350's own store. Only the passcode page
    002072C0 itself refers to 00207350.
  So in the boot ELF nothing makes C5 nonzero at a cold entry; only overlay
  code could. The AREA11 overlay has no store to 0x8106C5: the whole
  overlay range was scanned in the route 03 image for a byte store with
  offset 0x06C5.
- Request 6, the 00225A00 / 00225AC0 path. It is posted only by 00157F60
  for an owner of type 0x38.

**Translated, although no AREA11 owner posts their requests:**
- SPR4 states 10..12, the magazine refill. It is request 5, posted by
  00157F60 for type 0x37.
- HEALING's request-4 branch, posted by 00157F30.

The interaction lists D_00275B5C of route beats 00, 03, 08, 13 and 14 hold
only class 4/5/7/0xA owners of subtypes 0, 1, 2, 3 and 0x24.

The 0020CDC0 phase-2 path, the health count-up, is entered when HEALING
sets t[0x10] = 8, and only for kinds 2..4 (items 0x20..0x22). The AREA11
takes are kinds 0 and 1, which count up inside HEALING state 5. The
captured inventory holds no kind 2..4, so phase 2 stays unreached in the
first level, as `em_status_runtime.c` says.

## 2. What each function does

Every function's header comment in the source states its behaviour in
full. They are summarised here, with every point where the decomp's C
differs from the instructions. The translations follow the instructions.

**MAP helpers.**
- **00211240(k)** draws one list-mode marker at the halfword pair
  D_00265920 + 4k.
- **00211310(v)** draws one map-view marker from the vector v.
- **002117D0(a0, v, map, floor)** moves v into map-view space. It
  subtracts the floor row, turns v through 001029C0 / 00102BB0 / 001026A0
  on the scratchpad, then applies the zoom and pans (the status block's
  t+0x24 / 0x28 / 0x2C, read as D_00810154 / 158 / 15C). The NEARMISS C
  passes (0x70003400, row) to 001029C0, but the instructions set only a0.

**DATABASE helpers.**
- **00213A00(t, a1)** runs the category list:
  - the cursor t[0x17] moves on the repeat pad; the wrap flag goes to
    t[0x1A] and is also the result;
  - the 9-entry window t+0x90 is refilled;
  - 8 help lines are drawn through 001FCF60;
  - two cursor bars follow.
- **00213C50(t, sel)** refills the window, starting one entry earlier when
  sel is 1.
- **00213CC0(t)** is one step of the scroll animation: 9 lines offset by
  t+0x1C, which steps by ±1. It returns 1 at ±12, where t[0x19] advances.
  00213CC0 is an all-`.word` asm stub in the decomp; this translation was
  written from the instructions alone.
- **001FCF30(a0, a1, a2)** is a tail call of 001FE070(bank, a0, a1, a2).
  The bank address comes from the pointer D_0028A49C.

**ITEM children.**
- **00214570 EQUIPMENT** (entries 0x17..; list from D_00810C7B..7E) is a
  display-only list. Cross buzzes, with a debounce of 15. A take's request
  opens the notice, state 3. For item 0x19 it also moves D_00810C60 from
  1 to 2.
  - *NEARMISS C difference:* the C calls 0020B0D0(0, t). The instructions
    pass (t, D_00265B80), like the other pages.
- **00215870 EVENT** lists entries 0..1 and 0x25..0x3B. Cross looks up a
  device (00185420(id + 0x23)) and asks 00182B30 whether the player can
  act. On Yes it sets the device's +0xB = 5 and the scratchpad mode byte
  0x70003B8D = 3, and closes the status screen (D_008106C5 = 0xFF).
- **002160B0 HEALING** (kinds 0..4 = items 0x1E..0x22) handles:
  - the take notice (state 3) and the refusals (state 6: line 0x1A full
    health, 0x19 no device, 1 or 0x1B at the 60/100 cap);
  - the Yes/No prompt (state 4);
  - the use itself: the targets t+0x34 / t+0x38, the device hand-off
    0015C750 for kind 2, the page-8 exit for kinds 2..4, then 001C47E0,
    00215FE0 and the cursor clamp;
  - the health count-up (state 5: +1.0 every 3 frames, sound 0xE every
    tenth main-loop frame).
  - **The s0 read.** State 4 picks its prompt line from the byte at
    t + 0x50 + s0, and never sets s0 on that path. s0 is the caller's s0.
    0020EE50 holds t there, so the byte is RAM[0x10202B0]. It is 0x2B in
    all 16 route images and the status-hub image, so the prompt line is
    0x18 throughout the first level. The translation takes the caller's s0
    as a second argument, so the binding passes t.
- **00215FE0** builds HEALING's list from D_00810C82..86.
- **0020BBE0(t, n)** refills the 5-row window.
- **0020BC50(t, rows, tex, flags)** is one frame of the scroll animation:
  the row offset is ±4 and it wraps at ±0x30. `flags` is not used.

**SPR4.**
- **00211970**, the 13-state page:
  - state 0: entry, the take notice and the refill request;
  - state 1: the part selector (help lines by hover);
  - states 2 and 3: the part-page module load;
  - states 4..8: the part pages;
  - state 9: the take notice;
  - states 10..12: the refill prompt, the count and the done notice.
- **002121A0** draws the frame and the ammunition readouts (D_00810CB4 and
  D_00810C63 × 30).
- **002125B0** is the selector: 0020D930(t, 2), then the 0020AC70 trail at
  (212, 170) and six labels highlighted by hover.
- **00212B60** draws the weapon picture from the equipment bytes
  D_00810CA4..CA7.
- **00212F30** draws one of ten part markers.
- **0020BF20** is the four-row counter panel:
  - rows: primary D_00810CA8, secondary CAA, fuel CAE + 100 × CAC, CB0;
  - a compact or full layout, with a row highlight;
  - blinking icons in mode 2;
  - the numbers go through 001C5FB0, 00123168 and 001CBA50.

**The part pages** share one list skeleton (the `part_tick` comment). They
differ in their list, their Cross check and their Yes commit:

| Page | List | Cross accepts when | Yes writes |
|---|---|---|---|
| 00218D90 LOWER | 0x810C64..68 | the entry (0020BEF0) is not CA6 | CA6; on CA4 = 2, also CA7 = 7, CA5 = 5, CA4 = 0xFF |
| 00217090 UPPER | C69, C6A | entry + 5 is not CA5 | CA5, plus the CA4 clean-up |
| 00218640 SCOPE | C6B..C6D | entry + 7 is not CA7 | CA7, plus the CA4 clean-up |
| 002177B0 MULTI | 0x810C6E..72 | see below | CA4..CA7 and D_008106E0..E8 |
| 00217FA0 SELECTOR | 0, 1 (+2 with C73) | the entry is not C61 | C61 |

**002177B0's Cross check** differs from its NEARMISS C in two places. The
instructions do this:
- With c = entry - 2 and c < 3, D_0081070B = c. The wanted id is 0xC only
  when D_00810C70, C71 and C72 are **all** set; otherwise it is t[0x12].
  The C also yields 0xC when none of the three is set.
- For any other entry the wanted id is **entry + 0xA**. The C uses t[0x12].

## 3. Verification

`python3 tools/test_status_pages_reference.py` runs the default sample,
serially. `EM_TEST_FULL=1` runs every case with up to 4 worker processes;
`EM_TEST_JOBS=n` overrides the worker count of either run. The test
compiles the translations once and reuses the library while the digest of
the flags, the sources and every header under `src/` is unchanged. Other
commands:
- `debug <group>:<i,...>` runs single cases.
- `cover` prints the greedy outcome cover.
- `EM_SP_GROUPS=<names>` runs only some groups.

**The machinery is the AREA01 UI lane's**, imported from
`tools/test_area01_ui_reference.py` and `test_area01_render_reference.py`:
- the RouteEE interpreter with every COP1 op from `ee_float_model.py`;
- the EE access rules and TLB map;
- the store journal;
- the dirty-page tracker;
- the entry check at every callee.

It is pointed at this lane's routines by setting that module's globals.
Three things were added:
1. **Per-case hooks.** The routines of the case's own lane run inside it on
   both sides; every other routine is a logged callee. This includes the
   AREA01 routines this lane calls (00207D90) and this lane's routines that
   the AREA01 routines call.
2. **Registers other than a0..** For 002160B0 the caller's s0 is set on the
   oracle side.
3. **A scripted boundary for 0015C750.** HEALING kind 2's player hand-off
   touches the DMA controller at 0x10009000, which lies outside the
   interpreter's model. Its entry (arguments, all RAM, scratchpad) is
   checked and logged on both sides, but it is not run.

**Captured images** (every instruction and table comes from them):
- `../Extermination/build/startup-reference/status-hub` (the hub open);
- route beats `01_battery`, `03_panel_power` and `14_roger_encounter`.

No capture shows any of these pages open, so every case is a designed
input over those images. The chosen fields are:
- the state bytes;
- the pads D_00810E70 / 74 / 78 and the stick D_00810E64 / 65;
- the owned-item flags and the equipment bytes;
- the list rings;
- health and infection;
- the requests;
- the scratchpad counter 0x70003B64.

Where a device is needed, the case builds a one-entry interaction list
D_00275B5C / 64 whose shape-5 record sits at the player's position. The
original 00185420 / 00184D20 then accept it; a second version 100 units
away makes them refuse. D_008106F1 = 1 makes 00182B30 report the player
busy. The MAP / DATABASE reuse cases on the hub image first run the
original 001AFEB0 / 001AFE60, as 0020CDC0 phase 3 sub-state 0 does before
a page's first call; without them the UI pool is full and 001AFF10
returns 0.

Every case compares:
- every callee entry, in order (address, stack pointer, 64-bit argument
  registers, float argument registers);
- all 32 MiB of RAM and the 16 KiB scratchpad at every callee entry;
- at the end, the results, RAM and scratchpad.

Full run, 2026-09-27 (`EM_TEST_FULL=1`, 4 workers, 686 s wall, 2,160 s CPU):

| Group | Cases | What |
|---|---:|---|
| MAP / DATABASE helpers | 1,078 | every helper over 4 images: marker indices, vectors with NaN / ±1e9 / −0, maps 0..10 × floors, zoom / pan values, rings of 0 / 1 / 9 / 20 with every start, pad repeats at the cursor ends, scroll offsets ±10..±13 |
| ITEM children and list helpers | 1,063 | HEALING (flags, takes, request 4, every kind × mode × health / infection gate, the prompt with s0 = t and other values, the count-up at every boundary), EQUIPMENT, EVENT (every device class, refused, busy player), list wraps, 0020BBE0 / 0020BC50 at ±0x2F / 0x30 / 0x31 |
| SPR4 page, helpers and part pages | 1,209 | every 00211970 state (0..13), take notices 0x10 / 0x11, the refill, a 13-direction stick sweep, every hover × equipment byte, the panel in every mode × sel with and without the blink bit, the five part pages in every state with requests, full / empty ownership, scrolls, every kind in states 3 / 4, and the MULTI gates |
| reuse: AREA01 MAP / DATABASE on AREA11 | 516 | `em_area01_ui_0020F950` / `_00214020` over the hub and route-03 images, their callees (this lane's helpers included) as original code |

Results:
- 3,866 cases pass (3,859 generated plus the 7 survivor pins the
  generators did not already hold), with 47,634 callee entries equal to
  the original's (all RAM, scratchpad and arguments).
- Branch coverage: this lane's 422 conditional branches have 844
  outcomes. 826 are reached; the other 18 are unreachable by construction
  and are listed in `UNREACHED` with reasons:
  - the sign test and remainder fix-up of `i % 4` on loop indices that
    start at 0 (5 × 3);
  - HEALING kind 1's compares of a just-stored 100.0 (2);
  - the SPR4 hover ≥ 7, which 0020D930 mode 2 never leaves after a sweep
    over every stick direction and the dead zone (1).
- The full run requires every reachable outcome.
- Faults: 96 native-only cases:
  - a NULL call gives code 1 with nothing written;
  - a latched fault means no call and no write;
  - no views give code 4 (00212F30 reads no memory and completes);
  - a failing first callee gives code 2 and stops there.

**Default run** (342 cases, serial: 10.4 to 11.8 s CPU and wall on the M1, 2026-09-27):
- It keeps the greedy outcome cover (`QUICK_PINS`, 300 items from the
  `cover` command), the cases the first mutation check needed
  (`EXTRA_PINS`) and the review's survivor pins (`SURVIVOR_PINS`, 11
  cases, below), so it also reaches all 826 outcomes.
- It is serial: the memo below lives in each process, and worker
  processes re-run the heavy callees (measured before the pins: 4 workers
  cost 13.3 s CPU for 4.6 s wall, serial 10.0 s CPU).
- The oracle memoises the heavy callees (0020A7A0, 0020AC70, 00213A00,
  001FCF30, 001FCF60, 001FE070, 0020B210, 0020AE40, 001CBA50, 0020B0D0,
  001C5FB0, 001FCF10), keyed on the full entry, and replays a recording
  only when every value it read is unchanged. The native side replays
  their stores after the entry check.
- 0020A7A0 (the background) and 0020AC70 (the trail) are entry-checked
  boundaries in the default run only. They are verified by their own
  tests, their results are unused, and no page reads what they write.
  `EM_TEST_FULL=1` runs them as original code on both sides.

**Mutation check.** The `mutants` command builds 21 single-operation
mutants and runs the default sample on each:
- the lane's first 10: bounds, a float constant, the scroll step, the s0
  probe, a cap, a store target, a row formula, a hover table, and the two
  MULTI gates;
- the 11 the review's 40-mutant sweep left alive in the default run (5 of
  them in the full run too). Each was an input gap, not wrong code; a
  `SURVIVOR_PINS` case now holds the missing data condition:

| Mutant | Change | Pinned case |
|---|---|---|
| 3A00_7 | 00213A00 down-move bound 7 -> 6 | cursor 6 with the 0x4000 repeat |
| CF30_10 | 001FCF30 bank word +0x10 -> +0x14 | help-bank header word +0x14 = 0x40 (both words are 0 in every image, so the change was invisible on captured data) |
| panel4 | 00218D90 panel, entry 4 -> selection 2 | LOWER state 1 on entry 4 with counter rows 1..3 nonzero |
| panel2 | 002177B0 panel bound 2 -> 3 | MULTI state 1 on entry 2 with D_00810CB0 nonzero |
| multilocate | 002177B0 take locates by index, not page / row | take of B1 0xE with all five owned: entry 4 at list index 4 |
| 3CC0_m11 | 00213CC0 bound -11 -> -12 | d = -1 from t+0x1C = -11 |
| 60B0_tenth | 002160B0 state 5 `% 10` -> `% 9` | main-loop counter 10 |
| 60B0_clamp | 002160B0 clamp n < 4 -> n < 5 | Yes consuming the last kind 4 with cursor 4: the rebuilt list has 4 entries |
| BF20_13 | 0020BF20 secondary-row blink range 2 -> 1 | mode 2, sel 0x14, blink bit clear |
| 1970_mod | 00211970 module of hover 3: 0x30 -> 0x31 | state 2 with t[0x15] = 3 |
| multi3 | 002177B0 gate c < 3 -> c < 2 | Cross on entry 4 with C70..C72 set |

All 21 are killed by the default run (2026-09-27, 166 s for the 21).

Not verified:
- final pixels: the 2D workers are callees here;
- the page-module textures, which are not resident in these images (they
  come from each page's own module);
- the MAP node model bank: the AREA01 lane's stand-in-bank caveat still
  holds.

`python3 tools/check_no_disassembly.py` passes on every file of the lane.

## 4. Reuse of the AREA01 translations

MAP 0020F950 (with 002101C0, 00210030, 00210A00, 00210C00, 00210F30,
00211400) and DATABASE 00214020 (with 002131B0, 002134C0, 00213F30,
00207D90, 00208040) are the committed `em_area01_ui_*` functions. They
are the same functions, not copies: one translation per address. They
use the same `EmArea01Ui` contract as this lane, so both lanes share one
state and one dispatcher:
- An AREA01 routine's `call` of 00211240 / 00211310 / 002117D0 / 00213A00
  / 00213C50 / 00213CC0 / 001FCF30 goes to `em_status_pages_*`.
- This lane's `call` of 00207D90 goes to `em_area01_ui_00207D90`.

The `reuse` group verifies the AREA01 page functions over the AREA11
images.

## 5. Binding

**State.** One `EmArea01Ui` serves every page. Its views must hold the
original bytes the pages read and write at their original addresses:
- the status block D_00810130 (0xA0 bytes);
- the message words D_002821B0 / B4 / B8, D_00282240 / 44;
- the request bytes D_008106B0 / B1 / C5 / CD / D0;
- the game block bytes D_00810700.. (the area, the owned tables at
  0x810C61..0x810CCA, the equipment bytes CA4..CB7, health / infection
  D_00810858 / 5C, D_008104D0, D_008106E0..E8, D_008106F1, D_0081070B);
- the pads D_00810E64 / 65 / 70 / 74 / 78;
- the scratchpad 0x70003400..0x70003B8D (3B64 counter, 3B8D mode byte,
  38A0 / 3400 / 3600 work);
- D_00275BD8 (the module busy byte), D_00275B5C / 64 (the interaction
  list), D_0028A49C (the help bank);
- the `.data` tables D_00265510..D_002665A0, D_002659C0, D_00265890 /
  920 / 980;
- the string buffer D_002862C0 and the EE stack (for the callee frames);
- the byte at 0x10202B0, which HEALING state 4 reads (it can be a
  one-byte view holding the captured 0x2B).

Today `em_status_runtime` copies the block into an `EmStatusPage` struct
(`ui_load`). The chain should make the block bytes themselves the storage
and give the core a view of them.

**Page core (`em_status_page.c`, 0020CDC0).**
1. The cold entry must follow 0020CDC0 case 0 exactly, in its test order.
   The full table is in section 1 ("0020CDC0 case 0, the cold entry");
   in short:
   - B0 = 6: 00225A00, page 0, t[2] = t[4] = 2, t[0x15] = 3. Unreached in
     AREA11 (section 1), so the binding may keep a fault for it;
   - B0 = 5: page 2 (t[2] = t[4] = 0; the SPR4 refill request);
   - B0 = 4: page 0, t[2] = t[4] = 2, t[0x15] = 5 (HEALING);
   - B0 = 1: B1 & 0xC0 is tested **first** and gives page 0 with t[0x15] =
     3 (BATTERY). Otherwise:
     - B1 0..0xF gives page 2 with t[2] = t[4] = 2 and t[0x15] = the part
       page's SPR4 hover: 0..4 -> 6 (LOWER), 5..6 -> 2 (UPPER), 7..9 -> 5
       (SCOPE), 0xA..0xE -> 3 (MULTI), 0xF -> 4 (SELECTOR);
     - B1 0x10..0x16 gives page 2 with t[2] = t[4] = 0 (00211970 state 0
       then shows the notice);
     - B1 0x17..0x1A / 0x1B..0x1D / 0x1E..0x22 / 0x23..0x3F give page 0
       with t[2] = t[4] = 2 and t[0x15] = 1 / 3 / 5 / 4 (EQUIPMENT /
       BATTERY / HEALING / EVENT);
   - B0 = 2: page 1 (MAP);
   - B0 = 3, and any other nonzero B0: page 3 (DATABASE);
   - B0 = 0: D_008106C5 = 2 gives page 5, any other nonzero C5 (0xFF
     included) page 4, both unreached in AREA11 (section 1); C5 = 0 opens
     the hub (t[1] = 1).
   Every request branch sets t[1] = 3; branches that do not store t[0x15]
   leave its old value.
2. Phase 3 sub-state 1 must load modules 0x1E / 0x2C / 0x24.
3. Sub-state 2 must dispatch t[0x10] 1 / 2 / 3 to:
   - `em_area01_ui_0020F950(s, t)`;
   - `em_status_pages_00211970(s, t)`;
   - `em_area01_ui_00214020(s, t)`.
4. Page id 8 goes to phase 2. It is unreached in AREA11 (section 1), so
   it keeps its fault.

**ITEM root (`em_item_root.c`, 0020EE50).** The `EM_ITEM_CHILD_PAGE` worker
(today `other_tick` / `hooks.other_page_tick` in `em_status_runtime.c`)
calls these for states 4 / 6 / 7:
- `em_status_pages_00214570(s, t)`;
- `em_status_pages_00215870(s, t)`;
- `em_status_pages_002160B0(s, t, t)`: the third argument is the caller's
  s0.

State 5 stays `em_spr_002149F0`. The load worker must accept modules 0x20
/ 0x22 / 0x23 (EQUIPMENT / EVENT / HEALING).

**SPR4 part pages.** 00211970 calls them directly. Its state 3 loads
modules 0x2D..0x31 through 001FF080, the same load worker.

**The dispatcher** (`EmArea01Ui.call`, by original address; the port
native or the gap):

| Callee | Native | Notes |
|---|---|---|
| 00207D90 | `em_area01_ui_00207D90` | |
| 00207E40, 00207D00 | the 2D layer (`em_status_background_draw`, `em_battery_page_live`) | packet builders; the pages hand over their exact arguments |
| 0020A7A0 | `em_status_background` | the one D_002655A0 state |
| 0020AE40, 0020B0D0, 0020B210, 0020BEF0 | `em_sul_*` (`em_status_ui_leftovers`) | |
| 0020CCB0 | `em_cs_0020CCB0` | |
| 0020CD40 / CD60 / CDA0, 0020CD80 | `em_sul_*`, `em_spr_0020CD80` | the sound cues |
| 001FCF10 | `em_rvr_001FCF10` | |
| 001FCF60 | `em_cs_001FCF60` | |
| 001FE070 | message service (`em_census_standins` / `em_message_*`) | help-bank line |
| 001C5FB0, 00123168, 001CBA50 | `em_status_hub_ui` / `em_status_draw` inline number formatting, the font path | need an address-level entry |
| 001FB9F0 | `em_sfx` | |
| 001FF080 | the module loader (`em_status_scene_loader_request_001FF080`) | modules 0x1E, 0x20, 0x22, 0x23, 0x24, 0x2C..0x31 |
| 0020E020 | `em_item_trail_reset` | |
| 0020AC70 | `em_item_trail` | base (212, 170) for SPR4 |
| 0020D930 | **gap**: mode 2 (the SPR4 six-way table) | `em_status_hub` has table 0, `em_item_root_hover` table 1 |
| 001281C0 (float_to_int) | `em_ee_cvt_w_s_bits` semantics | |
| 001026A0, 001029C0, 00102BB0 | the SDK vector helpers (`em_sdk_vu0` / player record helpers) | |
| 00185420 | **partial**: `em_item_device_find` covers only the battery branch of 00184D20 | kinds 0x20, 0x23..0x27 need the rest of 00184D20 |
| 00182B30 | `em_player_stage_workers` | |
| 0015C700 | the player frame / pose host | |
| 0015C750 | **gap** | HEALING kind 2 device hand-off |
| 001C47E0 | **gap** | the item-use consume call of HEALING |

**Dispatcher glue the chain must test.** The `reuse` group runs the
AREA01 page functions with this lane's helpers as original-code callees,
and each lane routine is verified on its own at the callee boundary. The
glue that maps a callee's register image to a native argument list is
therefore not exercised end to end here, and the chain's level test has to
cover it:
- 002117D0's a0 is the full 64-bit register image (`uint64_t`);
- 00213A00, 00213CC0 and 001FCF30 return their result in v0 (the
  `uint32_t *v0` out-parameter);
- 001FCF30's three arguments are passed on as full register images;
- 002160B0's third argument is the caller's s0, not a2.

**What to retire once bound:**
- the page report in `em_area11_interaction_host_status_page` (the
  "B0 = …, B1 = … opens …, which is not translated" fault);
- the `other_page_tick` / `other_page_render` hooks and `DRAW_OTHER` in
  `em_status_runtime.c`, which this binding replaces;
- the `em_hud.c` legacy page screens (scenes without the AREA11 host);
- in the docs:
  - the player-reachable fail-stop entries in `FIDELITY_FEATURES.md` (lines
    on DATABASE / SPR4 / MAP and the non-battery takes);
  - `FIRST_LEVEL_AUDIT.md` H7 item (2), H9 and C.16;
  - the census row 0x0020CDC0 limitation.

**Tests.**
- Keep `tools/test_status_pages_reference.py` as this lane's reference
  test. The chain adds its make target, since the Makefile is not edited
  here.
- The level smoke then needs a status route that opens each page (the
  hub hovers 1..3 and the ITEM hovers 1 / 4 / 5 by stick input).

## 6. Known gaps

- **No capture of any page open.** All evidence is designed input over the
  hub and route images, verified against original instructions. A PCSX2
  capture of each page (hub → page) would add real call shapes; none was
  taken, because this lane does not launch PCSX2.
- **Textures and render.** The pages submit 2D draws through callees.
  Neither of these exists yet:
  - the page-module texture atlases (modules 0x1E / 0x20 / 0x22 / 0x23 /
    0x24 / 0x2C..0x31), with their exporter;
  - a renderer consumer for these packets.
- **Callee gaps:** see the dispatcher table (0020D930 mode 2, 00185420
  non-battery kinds, 0015C750, 001C47E0), plus the loader's module
  contents.
- **The MAP page's model bank** (asset slot D_0028A570[0]) is resident in
  no image. 002101C0's model paths were verified on stand-in banks by the
  AREA01 lane, and the reuse cases here do not add model setups.
- **0015C750 is a boundary** in the test: its entry is checked, but its
  effect is not run.
- **Quick-mode boundaries.** 0020A7A0 and 0020AC70 are not run in the
  default run (see section 3). The full run runs them.
- **Dispatcher glue.** The native composition of the AREA01 page functions
  with this lane's helpers through the chain's dispatcher is not run here
  (section 5, "Dispatcher glue the chain must test").
