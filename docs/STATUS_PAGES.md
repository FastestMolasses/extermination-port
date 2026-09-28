# Status pages (lane status_pages)

Date: 2026-09-27. A standalone translation lane for the first level
(AREA11). It covers the status pages the player can open there that had no
native translation, and it removes the page fail-stops from the port's side:
- the status hub's DATABASE, SPR4 and MAP pages;
- the ITEM root's EQUIPMENT, EVENT and HEALING children;
- the pages the non-battery item takes open (types 0x1E/0x1F, 0x10, the key
  0x32 and the map 0x08).

The lane (phase B15) wired nothing; chain C8b (FAILSTOPS, then its MAP
fix round) bound the pages live: section 7 is the current state (DATABASE,
SPR4 with its part pages, MAP with its model nodes and the ITEM children run
live; no page the first level reaches is left unbound).

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
| 1 MAP | 0020F950 | hub hover 3; the map take (B0 = 2) | `em_area01_ui_0020F950` (AREA01 lane), its helpers here; its nodes 002101C0 in the UI pool (section 7) |
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

**Default run** (342 cases; up to four worker processes: 13.9 s CPU, 4.9 s wall on the M1, 2026-09-27; `EM_TEST_JOBS=1` serial: 12.7 s):
- It keeps the greedy outcome cover (`QUICK_PINS`, 300 items from the
  `cover` command), the cases the first mutation check needed
  (`EXTRA_PINS`) and the review's survivor pins (`SURVIVOR_PINS`, 11
  cases, below), so it also reaches all 826 outcomes.
- The memo below lives in each process, so worker processes re-run the
  heavy callees: the parallel default run costs about 10 % more CPU than a
  serial one, and fits the ~10 s wall budget (chain C8b MAP; the serial run
  took 13 s).
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
- the MAP node model bank in this test: the AREA01 lane's stand-in-bank
  caveat holds here; the live binding uses the disc's bank (section 7, `make
  test-status-map-reference`).

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
| 0020D930 | `em_menu_hover_0020D930` (all three tables; chain C8b) | the one owner: the hub, the ITEM root and 002125B0 call it |
| 001281C0 (float_to_int) | `em_ee_cvt_w_s_bits` semantics | |
| 001026A0, 001029C0, 00102BB0 | the SDK vector helpers (`em_sdk_vu0` / player record helpers) | |
| 00185420 | the host's `page_find_device` (`em_item_device_find`: the battery branch of 00184D20) | the other kinds are unreachable in AREA11 (section 7); a device other than the panel faults |
| 001AFF10 / 001B0000 / 001AFF90, 001C6120 / 001CA5E0 / 001C6150 / 001AF7C0 / 001CB5B0 / 001C62C0 / 001C6380, 001CB480 | `em_status_models_call` (MAP, section 7) | the UI pool and the MAP bank |
| 00182B30 | faults (unreachable in AREA11, section 7) | |
| 0015C700 | `em_player_0015C700` (with 0015C7C0; chain C8b) through `player_pose_0015C700` | |
| 0015C750 | faults (unreachable in AREA11, section 7) | HEALING kind 2 device hand-off |
| 001C47E0 | `em_pickup_items_001C47E0` (chain C8b) | the item-use consume call of HEALING |

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
  hub and route images (this lane) and the live pages replayed over the
  status-hub capture (section 7), verified against original instructions. A
  PCSX2 capture of each page (hub → page) would add real call shapes and
  pixels; none was taken (the lanes do not launch PCSX2).
- **The MAP models' pixels** are the port renderer's (section 7): no
  capture shows the page, and the draw 001CB480 runs on the renderer's
  skinned path with the original's light matrices.
- **0015C750 is a boundary** in this lane's test (its entry is checked, its
  effect is not run); it is unreachable in AREA11 (section 7).
- **Quick-mode boundaries.** 0020A7A0 and 0020AC70 are not run in the
  default run (see section 3). The full run runs them.
- The texture atlases, the renderer, the callee gaps 0020D930 mode 2,
  001C47E0 and 0015C700, the dispatcher glue and the MAP page are done
  (section 7).

## 7. The live binding (chain C8b FAILSTOPS, 2026-09-27)

**Live:** the status hub's DATABASE (00214020, the AREA01 lane's
translation), SPR4 (00211970 with its selector and the five part pages) and
MAP (0020F950, the AREA01 lane's translation, with its nodes 002101C0; below,
"MAP"), the ITEM children EQUIPMENT 00214570, EVENT 00215870 and HEALING
002160B0, and the takes that open them (0x1E / 0x1F HEALING, the key 0x32
DATABASE, the magazine 0x10 SPR4, the map 0x08 MAP). No page the first
level reaches is unbound; the fail-stops left guard the branches listed
under "Unreachable in AREA11".

**Page core** (`em_status_page.c`, 0020CDC0):
- the cold entry is case 0's whole request map (section 1's table, in its
  test order); it faults before any side effect only for request 6
  (00225A00) and, without a request, a nonzero D_008106C5 (the passcode
  pages); `tools/test_status_page_reference.py` runs every request 1..5, 7
  and 0xFF over the B1 boundaries (every B1 with `EM_TEST_FULL=1`) against
  the original, with t[0x15] starting at a sentinel;
- phase 3 sub-state 1 loads module 0x1F / 0x1E / 0x2C / 0x24 for page 0..3
  and only advances t[3] for any other page id; sub-state 2 ticks page 0
  (the ITEM root) and pages 1..3 (`EM_STATUS_PAGE_PAGE_TICK`), returns to
  the hub for any other id, and faults for the passcode pages 4 / 5 and
  page 8 (phase 2), which AREA11 cannot reach.

**Runtime** (`em_status_runtime.c`): `em_status_runtime_bind_pages` binds
`em_status_pages_live`; the ITEM root's child states 4 / 6 / 7 and the page
ticks 1..3 call it over the canonical bytes (the page core's views of the
status block, the request bytes and the message words are stored before
the call and loaded after it, as for BATTERY). The page modules (0x1E..0x24,
0x2C..0x31) complete at host speed, as 0x1F / 0x21 do, and apply their GS
blocks; `EM_STATUS_PAGE_PLAYER_TEXTURE` (00200970(1)) applies the restore.

**`em_status_pages_live.c`** runs the translations over views of the port's
storage (the list is its header's; every other address faults) and
dispatches every callee by original address:

| Callee | Binding |
|---|---|
| 00207D00 / 00207E40 / 00207D90 | `em_page_draw` (blend, sprite, SCISSOR_1: the A+D register 0x40 of the packet em_area01_ui_00207D90 builds) |
| 0020A7A0 | `em_page_draw` background (em_status_background, stepped at the draw) |
| 0020AE40 / 0020B210 / 0020B0D0 / 0020BEF0 / 0020CD40 / 60 / A0 | `em_sul_*` over the views; 00208AD0 (flag 8) is `em_status_health_draw` with the hub's records |
| 0020CD80 / 0020CCB0 / 001FCF10 | `em_spr_0020CD80`, `em_cs_0020CCB0`, `em_rvr_001FCF10` (the host's 001FCB90) |
| 001C5FB0 / 00123168 / 001CBA50 | `em_status_draw_001C5FB0` into its buffer D_008111D0, a copy over the views, `em_page_draw` text |
| 001FCF60 / 001FE070 | `em_message_live_record_draw` / `em_message_live_fe070` (the record container *D_0028A49C, module 0 slot 3, is a view at its original address 0x00B0B800) |
| 0020D930 / 0020E020 / 0020AC70 | `em_menu_hover_0020D930` with 001B62C0's record at 0x700038A0, the shared trail reset, `em_item_trail_step` (a2 != 0 zeroes the new slot; the base floats get 1792 / 1824 added) |
| 001FF080 | the runtime's module worker |
| 00185420 | the host's device lookup (the battery branch; a device other than the panel faults) |
| 001C47E0 / 0015C700 | `em_pickup_items_001C47E0` over the item block; `player_pose_0015C700` (`em_player_0015C700`) after the vitals are stored |
| 00211240 / 00211310 / 002117D0 / 00213A00 / 00213C50 / 00213CC0 / 001FCF30 | this lane's translations, when an AREA01 page calls them |
| 001AFF10 / 001B0000 / 001AFF90 and MAP's model callees | `em_status_models_call` (below, "MAP") |
| 00208040 | `em_page_draw_flat` (00210F30's marker: its corners' XYZF2 X / Y from float_to_int of x and z, as the packet builds them) |
| 001029C0 / 00102BB0 / 001026A0 / 001026D0 / 001031E0 / 001B1470 | the SDK leaves over the views (`em_owner_services` identity / rotate-y, `em_effect_original_001026A0`, `em_sdk_vu0_001026D0`, the xyz copy, `em_player_001B1470`) |
| 00182B30 / 0015C750 | fault: unreachable in AREA11 (below) |

The vitals D_00810858 / 5C are g.status (the port's one copy); the
player's +0x220 (D_008104D0) is the same value there, so a +0x220 store
that D_00810858 does not hold faults. D_00810C61 / C62 / CB4 are
em_weapon's (a fire mode em_weapon refuses faults). 002160B0 state 4's
probe byte 0x010202B0 holds 0x2B, the byte every route image and the
status-hub image hold there (section 2).

**Textures** (`em_gs_texture.c`, `tools/export_status_pages.py` ->
`assets/status_pages/status_pages.emsp`): the first level's GS memory after
a New Game (DISC_TEXTURES.md's disc replay), the blocks each page module
load and the 00200970(1) restore write, and the .data windows the pages
read (their tables, the clip pairs 0015C7C0 reads, and the record
container). Every window equals the status-hub capture's RAM byte for
byte. `em_page_draw` decodes each TEX0 from the GS memory the original
would hold at that moment (PSMT8 / PSMT4 with a PSMCT32 CSM1 CLUT; a texel
or CLUT in a block no upload wrote refuses the frame) into an atlas.
`tools/test_gs_texture_reference.py` compares the decode with the decomp's
`decode_token_lm` for every TEX0 word of the windows and the translations'
constants, over the world and every module state (full run).

**Unreachable in AREA11** (the fail-stops kept):
- 0015C750 and page 8 (HEALING kinds 2..4, items 0x20..0x22, and request 4
  from 00157F30): AREA11's seven item owners are types 0x1B, 0x1E (two),
  0x1F, 0x32, 0x10 and 0x08 (the interaction scene's pickups); 001C40B0's
  default case raises D_00810C64 + type, so only kinds 0 / 1 (C82 / C83)
  can be owned; C84..C86 are 0 in every route capture 00..15.
- 00185420's non-battery kinds and 00182B30 (EVENT's Cross): EVENT's list
  holds C87 / C88 and the event items 0x25..0x3B (0x810C89..); all are 0
  in every route capture and no AREA11 take writes them (the key 0x32
  raises D_00810CC3 + 0x32, the map D_00810CB8 + 8). Cross on an empty list
  does nothing.
- The part pages' commits other than SELECTOR's: each list holds only the
  equipped part in AREA11 (C64, C69, C6B are 1; C65..C68, C6A, C6C, C6D and
  MULTI's C6E..C72 are 0 in every route capture, and no AREA11 take gives
  a part), so Cross is refused; MULTI's D_0081070B (not a canonical byte
  yet) would fault if reached. SELECTOR's entries 0 and 1 are always
  listed: its Yes sets the fire mode (em_weapon).
- The passcode pages, request 6 and request 5 (section 1).

**Verification:**
- `make test-status-page-reference`, `test-status-pages-reference` (the
  lane's), `test-menu-hover-reference` (the one 0020D930 against every
  original table), `test-pickup-items-reference` (001C47E0 added),
  `test-player-heal-reference` (0015C700 / 0015C7C0),
  `test-gs-texture-reference`, `test-status-map-reference` (below, "MAP"),
  `test-area11-interaction-host` (the takes of 0x1E, 0x32, 0x10 and the map
  0x08 open their pages; HEALING's notice lasts 240 calls; START closes; hub
  hover 3 opens MAP and Circle returns).
- The level smoke's designed side run `status_pages`
  (`EM_LEVEL_SMOKE_UNTIL=status_pages`, part of `make
  test-level-smoke-side`): from first control it opens DATABASE, SPR4 (the
  LOWER U.R.S. and SELECTOR SWITCH part pages, whose Yes changes the fire
  mode), MAP, EQUIPMENT, EVENT and HEALING from the hub and backs out of
  each, then takes 0x1E and 0x1F (each used from its notice through the
  count-up and 0015C700, from health 30), the key 0x32, the magazine 0x10
  and the map 0x08 (the take is the use claim the scan makes, taken
  directly: test input). Every page call is written to
  `EM_STATUS_PAGES_TRACE`, and `tools/test_status_pages_live.py` replays
  each through the original instructions over the status-hub capture with
  the port's views written over it: 6,643 calls (MAP's nodes included),
  19,660 callee entries (address, stack pointer, argument register images,
  float arguments) and every view byte after the call equal the original's
  (before MAP was bound: 1,143 calls, 12,636 entries). This is the dispatcher glue section 5 asked
  for: the 64-bit register images, the v0 results, and s0 as 002160B0's
  third argument.

**Not verified:** pixels (no capture shows a page open;
`EM_LEVEL_SMOKE_PAGES_CAPTURE=<dir>` writes one frame of each page for a
look); the stack's register save slots (the replay compares the stack only
through the callees' arguments).

**MAP** (chain C8b's fix round, 2026-09-27). 0020F950 runs as the other
pages do, with these additions:
- **The UI pool.** `em_status_models`' pool D_0028B020 is the one storage
  of the 24 records; `EmStatusSceneActor` is now the record's byte layout
  (every field at its original offset, 0x2F0 bytes, static asserts), so the
  pool is a view at its original address. 001AFF10 / 001B0000 / 001AFF90 go
  to `em_status_models_call`; the walk runs each node's +0x10 through the
  node hook, where `em_status_pages_live` runs the AREA01 translation
  002101C0 over the same views (its stack pointer 0x30 below the 001B0000
  dispatch's, 001B0000's frame).
- **The bank.** 002101C0 binds 001C6120(D_0028A570, the table 0x2658C0's
  code) through 001CA5E0(p, model, 7). D_0028A570 is D_0028A490[0x38], the
  word 001FF830 state 7 relocates when module 0x1E loads (0020CDC0 phase 3
  loads 0x1E for page 1): its destination D_0028A748 (default case of state
  0; 0x19A3F40 in every AREA11 capture) plus slot 0x38's descriptor offset
  (0). `tools/export_status_pages.py` writes that relocation into the EMSP
  (version 2); `em_gs_texture` sets the word when it applies module 0x1E's
  step (0 before, as in every capture). The bank itself (22 single-node
  models, codes 0..0x15; map i's nodes use codes i and 0xB + i) is read from
  the disc by `tools/export_status_map.py` into `assets/status_map/`, the
  texels from the GS memory with module 0x1E's upload applied.
  `em_status_models_call` serves 001C6120 (the bank address + the model's
  directory offset), 001CA5E0 (+0x44, then 001CA5F0's +0x4C: kind 7 is
  001CB480), 001C6150 (the node count), 001AF7C0 / 001CB5B0 / 001C62C0 /
  001C6380 (the hub's workers) and the draw 001CB480.
- **001CB480** (the kind-7 draw method): 001D2910(0) kept, 001D8C20(2),
  001D2830(0, 0), 001C7420(p, 0x3F5, 1), 001D3BA0(1, +0x44), 001D2830(0,
  kept). Mode 2 is not one of the modes 001D89D0 hands to 001D8C30 (1,
  3..6), so the light is the room rig: `em_owner_draw_live_light` runs
  001D8C20(2) and 001D89D0 with the same bindings as the world owners'
  001CAA00 (the rig table, the point lights, the rig record D_00817BC0), and
  002101C0's +0x02 bit 0x40 adds the glow (B's ambient row += 64 x the +0x80
  colour: (0, 0, 0.5) for a map not selected, (0, 0.15 or 0.55, same) for
  the current map, (2, 2, 2) for the selected map's second node). The draw
  is queued with the node matrices as the palette and the rig the renderer
  lights vertices with: A's three slot directions (its columns), B's three
  colour rows and B's ambient row less the 8388608 bias (em_gfx.h
  EmGfxCharRig, the formula the object kernel applies to the colour matrix
  001C7420 uploads). The channel-1 GS state it draws in is the page's: TEST_1
  0x5000D in blend mode 3 (the 00207D00 block in the status-hub capture:
  alpha test greater than 0, depth test GEQUAL), the kernel template's PRIM
  0x03C (no blending), fog off (001D2830(0, 0)), as the renderer's skinned
  path draws.
- **Order and scissor.** 001B0000 puts the models on the page's own GS list
  (channel 1): `em_page_draw_models` marks the place in the stream; at render
  the 2D layer so far is drawn (`em_gfx_overlay_decor_flush`), then the
  host's `models_draw` renders the queued draws inside the SCISSOR_1 window
  (`em_gfx_draw_scissor`; mode 2's window 0x12..0x1EF x 9..0x96), and the
  later 2D calls over them. 0020F950 and 00210F30 reach 00207D90 / 00208040
  through the dispatcher (`EmArea01Ui.leaf_calls`) so the scissor and the
  marker triangle land in the same stream.
- **Views** (the header lists them): the area bytes D_00810700..702, the
  pool, D_00810610 (the models' view), D_0028A570, D_00275BCC (the port's
  free bone slots: the original's free count is the cap 002101C0 compares
  with, the port's slots stand for its stack), the player record's
  +0xA0..+0xA8 and +0xC4 (g.pos, g.yaw) and the render context word
  D_00275670 and its +0x2468 (`em_rcl_bytes`). The .data windows and the
  other read-only views are compared after every call: a store into one
  faults (the review's point on the untraced windows).
- **The marker gates.** 00211400 reads the event bytes D_0081077F, 782,
  784, 789 and 78C (and the item bytes C8D / C8E): they are migrated
  progress bytes now (`em_scene_state.h`): 0 in every route capture 00..15
  and in the status-hub capture, and only 001AF2C0's New Game clear writes
  them in the first level. 0x81076D and 0x810770 (read only when C8D / C8E
  is set, which no AREA11 take does) stay unmigrated: reading them faults.

**MAP verification:**
- The `status_pages` run opens MAP from the hub with no map (the list, then
  Circle), takes the map 0x08 (MAP zoomed on map 8 with both its nodes'
  models, R1 then R2 zoom, the D-pad pans, Circle to the list, Cross zooms
  again with the player's marker, Circle twice to the hub) and opens MAP
  again with map 8 owned. Every call is traced: a node's 002101C0 is its own
  record (page 0x002101C0, a0 = the record, its pool view the record alone).
  `tools/test_status_pages_live.py` replays 6,643 calls (242 MAP, 5,258
  node, the rest the other pages): 19,660 callee entries and every view byte
  equal the original's; `tools/test_level_smoke.py` also requires the MAP
  node binds (4) and 001CB480 draws (390). A single mutant (the selected
  current map's colour 0x3F0CCCCD -> 0x3F0CCCCE in 002101C0) fails the
  replay (195 view differences).
- `make test-status-map-reference`: D_0028A570's relocation (the EMSP
  record, D_0028A748 in six AREA11 captures), the original 001C6120 over
  the disc bank at that address for 66 codes (the table's, the directory's
  and masked ones) and 001C6150, the radius and the skeleton records of all
  22 models.
- `make test-area11-interaction-host`: the map take opens MAP (22 nodes;
  map 8's two bind D_0028A570 + the bank's offsets with +0x4C = 001CB480)
  and START closes; hub hover 3 opens MAP (22 nodes, no model, the 2D layer
  flushed before the pool's models each list frame) and Circle returns.

**Not verified (MAP):** the models' pixels (the renderer's skinned path;
its normal normalisation is the host's, the object kernel's clip pass
001D3BA0 is not run, GS rasterisation is Metal's), and D_00275BCC's value
(the port's free slot count; the branch it gates needs more than 250 slots
per node).
