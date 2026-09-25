# BATTERY page record (002149F0) and the binding of its page draws

Date: 2026-09-25. Lane `battery-page-record`.

Scope: the record-level translation of **002149F0**, the ITEM > BATTERY
sub-page (ITEM child, module 0x21), that the census and the render step
asked for (FIRST_LEVEL_AUDIT "Render + UI step": "the BATTERY page draw
(needs a record-level 002149F0)"). With it, the four page draws
0020AE40 / 0020B0D0 / 0020B210 (em_status_ui_leftovers, lane L35) and
0020CCB0 (em_census_standins) have a caller that holds the original page
record. Section 4 gives the binding notes that retire the hand-placed
BATTERY stand-in in `em_battery_ui.c`. Also translated: **0020CD80**, the
no-target cue thunk (001FB9F0(2, 0x1000, 0x1000, 0x1000)). STATUS_UI_LEFTOVERS.md
section 4 listed it as untranslated.

Files (all new; nothing existing was edited):

| File | What |
|---|---|
| `src/game/em_status_page_record.h/.c` | `em_spr_002149F0`, `em_spr_0020CD80` (prefix `em_spr_`), with workers and fail-stop |
| `tools/test_status_page_record_reference.py` | the original-instruction oracle (`make test-status-page-record-reference`, hunk in section 5) |

The module is **built and tested but not bound**. The CLAUDE.md fidelity rule
applies: an unfinished original module stays unwired until its workers are
bound. A private build of HEAD with the module appended has zero warnings.

## 1. Result

`python3 tools/test_status_page_record_reference.py`:
- **Quick mode:** about 3 s, PASS.
- **`EM_TEST_FULL=1`:** about 31 s, PASS.

| Check | Quick | Full |
|---|---|---|
| A. synthetic records over the panel capture (1 to 3 lockstep frames each) | 1,200 of 30,000 cases (2,058 frames) | 30,000 cases (52,480 frames) |
| A. directed sequences (every path, played out over many frames) | 434 frames | 2,271 frames |
| A. write order: the k-th worker call fails, the state must equal the original's at that call | 2,696 | 116,896 |
| B. captures: panel, panel/root, status-hub, route beats 00..15 (input scripts; route 01 also replays the take request) | 2,123 frames, 19 captures | 10,313 frames |
| C. composition with the verified draw translations | 144 frames (3,712 leaf calls) | 616 frames (15,983 leaf calls) |
| C. the original 0020B210 over a one-row BATTERY list never raises the wrap event | 180 runs | 180 |
| D. fail-stop (every NULL worker or record, short page, each worker failing, ring index outside the page, zero owner) and 0020CD80 against the original | 53 + 2 | same |

Asserted on every run:
- Every jal target of 002149F0 is hooked (15 callees).
- **Both outcomes of all 62 conditional branches** of 002149F0 were executed.
- Every byte the original writes lies in a compared region. The compared
  regions are:
  - the whole game block 0x810000..0x811000, which holds the page record
    D_00810130, the request block D_008106B0.., the counts, D_00810CB2 /
    D_00810CB7 and the button words;
  - D_002821B0 / B4 / B8 and D_00282240;
  - the scratchpad byte 0x70003B8D;
  - 0x40 bytes of every owner record.

A mutation check (edit the translation, rerun) killed every non-equivalent
mutation tried:
- the timers 0x1E and 0x14;
- the cost line 16 -> 0xE and the capacity 0x24;
- `+0x13 + 1`;
- the 0x5060 mask;
- the halfword against low-byte charge read;
- the highest-kind order;
- `D_002821B4 = 2`;
- the unit step `- 2`;
- the request-6 D_008106C5 write;
- the jump-table range;
- five write-before-call orderings (D_008106C5 / D_00810CB2 / page +5 /
  D_002821B8 / D_002821B4 against the cue and message workers).

Two mutations survived, and both are equivalent:
- 0x830 against 0x870 in state 4 differs only in bit 0x40, which that
  branch has already excluded;
- the unit timer's signed or unsigned 16-bit view is only compared with 0.

How the comparison works:
- **The oracle.** It executes the ORIGINAL 002149F0 from the user's ELF (the
  shared EE core, `FallEE`) over captured RAM, with every callee hooked and
  recorded.
- **The native side.** `em_spr_002149F0` gets the same worker results: the
  list and scroll returns, and 00185420's owner. On the captures,
  00185420 is not scripted. The ORIGINAL 00185420 runs nested over the
  capture with no hooks, and both sides get its value. On the panel
  captures it returns the panel 0x7AA590; on route 01 and 03 it returns 0.
- **Lockstep.** Multi-frame sequences run in lockstep. Each side keeps its
  own state, and only the button word is set per frame.

### 1.1 Composition (the binding, executed)

Part C binds the native page's workers to the verified translations:
- `frame` -> `em_sul_0020AE40`;
- `list` -> `em_sul_0020B210`, with D_002821B4 / B8 / D_00282240 loaded
  into `EmSulListGlobals` and stored back;
- `arrows` -> `em_sul_0020B0D0`;
- `marker` -> `em_cs_0020CCB0`;
- `message_line` -> `em_rvr_001FCF10`;
- the cues -> `em_sul_0020CD40` / `60` / `A0` and `em_spr_0020CD80`.

`em_sul` reads through one live region over the native game block.

The original 002149F0 runs with all of those routines **unhooked**, as
original instructions. Both sides record the same leaves:
- 00207D00, 00207E40, 00207F80, 00208AD0, 00209280, 001FCB90 and 001FB9F0,
  and the text blitters;
- 001281C0, which executes original instructions on both sides.

The leaf streams, the page and the message words are equal on these runs:
- **panel:** the captured confirmation, then cursor, Yes and the whole
  discharge; also No, then the list, then 00185420 finds the panel, then the
  confirmation;
- **status-hub and panel/root:** the list;
- **route 01:** the list's accept finds no device, so 0020CD80 and state 8
  run; also the take request replayed, which shows the acquisition notice;
- **route 03:** the discharge state to its completion.

On the panel frame the stream is:
- 0020A7A0;
- 16 sprites: the frame, the list row, the highlight, the marker row and the
  arrows;
- the 00209280 gauge;
- 001FCB90(0x10E, 0xCC, 5, 0);
- 00207D00(1, 3);
- the two 001281C0 conversions and the 0020CCB0 rectangle.

STATUS_UI_LEFTOVERS.md 1.1 already showed that these leaf arguments rebuild
the captured GS packets byte for byte.

## 2. The translation

`em_spr_002149F0(workers, records, fault)`. The records are pointers into
the caller's single storage of the original bytes, with multi-byte fields in
EE little-endian order:

| Record | Original | Use |
|---|---|---|
| `page` (>= 0xA0 bytes) | D_00810130 (UI block) | +1..+6 page / ITEM root / child state, +0x12 charge at entry, +0x13 message line, +0x17 cursor, +0x18 rows, +0x19, +0x1A, +0x1B acquired kind, +0x1C, +0x1E (= 0x1B), +0x30 owner address, +0x3C unit timer, ring +0x50.. |
| `d8106B0` / `d8106B1` / `d8106C5` / `d8106D0` | request block | pending request (read, cleared, set 1), kind / item, status exit 0xFF, owner record address |
| `d810C7F` | D_00810C7F..81 | owned counts of items 0x1B / 0x1C / 0x1D |
| `d810CB2` / `d810CB7` | charge (half-units, halfword; also read as its low byte) / capacity | |
| `d810E74` | this frame's button / event bits | |
| `d2821B0` / `B4` / `B8`, `d282240` | message request block kind / phase / line, message group | |
| `spad3B8D` | 0x70003B8D | 3 after a completed discharge or recharge |

The owner record is reached through `owner_read` (+3 type byte, +0x34 signed
cost) and `owner_write` (+0xA = 1, +0xB = 5), so a binder can map it onto a
native struct.

### 2.1 What each state does

This is the .s, which the NEARMISS C matches. The C's one listed difference,
a register swap in state 6, is not semantic, and no other difference was
found.

**State 0 (entry).**
- Clears +0x17..+0x1A, then sets D_002821B0 = 4, B4 = 0, D_00282240 = 3 and
  +0x1E = 0x1B.
- Lists **only the highest owned kind** (2, 1 or 0) at ring +0x50. The ring
  never has more than one row.
- Then it branches on D_008106B0:
  - **6:** +0x12 = charge low byte, +0x13 = 8, +0x30 = D_008106D0. Then
    state 5 (timer 0xF0) when charge < 2 * cost, else state 4 (+6 = 1, No).
    D_008106B0 is not cleared.
  - **B1 & 0x80:** the cost confirmation. D_008106B0 = 0, state 4 (No),
    +0x12 = charge low byte. The line +0x13 comes from the cost: 4 -> 0xA,
    6 -> 0xC, 16 -> 0xE, 24 -> 0x10, anything else -> 8.
  - **B1 & 0x40:** the recharge path, line 6. State 5 (0x78) when charge ==
    capacity, else state 4.
  - **Otherwise:** the acquisition of item B1. +0x1B = B1 - 0x1B. The ring
    scan sets the cursor and D_00282240 = 4 when the acquired kind is listed.
    Charge and capacity are both set to 0xC / 0x24 / 0x30 (kind 0 / 1 /
    other). D_008106B0 = 0, then state 3 (notice) with 0xF0.
  - **No request:** +5 += 1, then state 1 in the same call.

**State 1 (the list).**
- 0x20: cue 0020CD60, D_002821B4 = 2, +1 = 3, +2..+5 = 0. This is the
  return to the ITEM page; nothing is drawn.
- Otherwise it draws 0020A7A0 / 0020AE40(2) / 0020B210(2):
  - a nonzero list event gives +0x1C = 0, +5 += 1 and 0020BBE0(+0x1A);
  - otherwise, with rows and 0x40, +0x30 = 00185420(ring[cursor] + 0x1B):
    - a nonzero owner: cue 0020CD40. Type byte 0x2C takes the recharge
      path, as state 0 does. Any other type sets state 4 with the cost line.
    - no owner: 0020CD80, D_002821B4 = 0, state 8 (0xF0).
- Then 0020B0D0.

**State 2.** The 0020BC50 scroll. A nonzero return steps +5 back.

**State 8.** The no-target line 0x19 (D_002821B4 = 1, D_00282240 = 5,
D_002821B8 = 0x19, set before the draws, list flags 0x602). It runs 240
frames, or until 0x60, which plays cue 0020CD60. Then D_002821B4 = 0,
D_008106B0 = 0, D_00282240 = 3 and state 1.

**State 3.** The acquisition notice, list flags 0x402. It runs 240 frames or
until 0x5060, which plays cue 0020CD60. Then D_008106B0 = 0, state 1 and
D_00282240 = 3.

**State 4 (the Yes/No confirmation).**
- The draws first, flags 0x402. Then D_002821B4 = 1, D_00282240 = 5,
  D_002821B8 = +0x13, 001FCF10 and 00207D00(1, 3).
- 0x8000 moves to Yes and 0x2000 moves to No, each with cue 0020CDA0. Then
  0020CCB0.
- 0x40 on No: D_002821B4 = 0, D_00282240 = 3, state 1, cue 0020CD60, and
  D_008106C5 = 0xFF under request 6.
- 0x40 on Yes:
  - line 6: +0x12 = charge, state 7, timer 3;
  - otherwise, charge < 2 * cost gives state 5 (0xF0) and cue 0020CD60;
    enough charge gives state 6, timer 1.
  - Then D_008106B0 = 1, except under request 6. There the page **leaves**:
    D_002821B4 = 0, +1 = 6, +2..+5 = 0, and no discharge runs on the page.
    Then cue 0020CD40.
- Under request 6, 0x830 gives D_002821B4 = 0, cue 0020CD60 and
  D_008106C5 = 0xFF.
- Otherwise 0x20 returns to the list, with cue 0020CD60.

**State 5 (the result line +0x13 + 1).**
- Under request 6 it ends on 0x870 (with cue 0020CD60) or on the timer, and
  sets D_008106C5 = 0xFF.
- Otherwise it ends on 0x60 (with cue 0020CD60) or on the timer, and returns
  to the list.

**State 6 (discharge).**
- Every 30 frames: 2 half-units off the charge and sound 001FB9F0(6, ...).
- It stops when the charge reaches +0x12 - 2 * cost. 0x870 finishes at once:
  the charge is set to that value, then cue 0020CD40.
- The finish writes D_008106C5 = 0xFF, owner +0xA = 1, +0xB = 5 and
  0x70003B8D = 3.

**State 7 (recharge).** The mirror of state 6. It counts up to the capacity
(timer 0x14) and ends with the charge = the capacity.

**State 9 and up.** Nothing happens (the jump table's range check).

### 2.2 Fail-stop

- **Checked before anything is written or called.** A NULL worker or record
  pointer returns -1 with EmSprFault code 1. A page shorter than 0xA0 bytes
  returns -1 with code 3.
- **Worker faults.** A worker's negative return gives -1, code 2, with the
  worker's original address.
- **Bad input.** Each gives -1 with code 3:
  - a ring index outside the page (the original would read the
    neighbouring game block);
  - a zero owner address that the original would dereference (request 6
    with D_008106D0 = 0).
- **Writes before a fault stay.** For the last two cases, and for any
  worker fault, the writes and calls made before the fault stay, in the
  original order. The write-order sweep proves that the native state at
  every worker call equals the original's.

## 3. Captured states

| Capture | Page +5 | Records |
|---|---|---|
| startup-reference/panel | 4 (confirmation, No) | D_008106D0 = the panel 0x7AA590 (+3 = 0x24, cost 2), charge 12 / 12, line 8 |
| panel/root | 0 | same records, UI +4 = 1 |
| status-hub | 1 | same records |
| route 01, 02 | 1 (list) | one row, B1 = 0x1B; 00185420 finds nothing, so a CROSS here is state 8 |
| route 03..14 | 6 (discharge), timer 30, charge 8 | the next call that reaches the timer completes the power-up (verified: +0xA = 1, +0xB = 5, 0x70003B8D = 3) |
| route 00, 15 | 0 (page cleared) | route 00 owns no battery, so there are no rows |

## 4. Binding notes (for the coordinator)

Nothing here is wired. The live path today is:
- `em_status_runtime.c` `battery_tick`: `EM_ITEM_CHILD_PAGE` argument 5,
  reached from `em_status_runtime_page_tick`;
- `em_battery_ui.c`: its tick, and its hand-placed render in `DRAW_BATTERY`;
- `em_panel.c` `em_panel_battery_begin/step`: the type-24 branch of states
  4/5/6;
- the host's `battery_finished` hook, which writes 3B8D = 3.

`em_spr_002149F0` replaces all four.

**Call site.** In `battery_tick` (the ITEM root's child page 5), call
`em_spr_002149F0(&workers, &records, &fault)` once per frame, where the
original calls 002149F0. A -1 goes to `fail()` with `fault.address`.

**Records.**
- **`page`.** Give the runtime one persistent 0xA0-byte store for
  D_00810130. The 0020E060 memset (`EM_STATUS_RESET_UI`) clears it.
  `EmStatusPage` (UI+0..+3) and `EmItemRoot` (UI+4..+6, +0x10, +0x11, +0x15,
  +0x16) become views of it: load before the call and store after, or better,
  accessors. The page itself writes +1..+6 on its exits:
  - +1 = 3 with +2..+5 = 0 is the ITEM return, today's `BACK_TO_STATUS`;
  - +1 = 6 is the request-6 exit.

  The other fields (+0x12.., +0x30, +0x3C, the ring) exist only in this
  store.
- **Request block.** `d8106B0` / `d8106B1` / `d8106C5` are the pointers
  `em_status_runtime_page_tick` already receives (`&scene->req[...]`).
  `d8106D0` is `em_scene_req_at(scene, 0x008106D0)`; the page tick must
  pass it through.
- **`d810C7F` (3 bytes), `d810CB2` (2), `d810CB7` (1).** Today em_pickup
  holds them (the `read_inventory` / `write_charge` /
  `write_battery_capacity` hooks). There are two ways to bind them:
  - migrate them to `em_scene_progress_at(scene, 0x810C7F, 3)` / `(0x810CB2, 2)`
    / `(0x810CB7, 1)`, which is one canonical copy and preferred;
  - or use a per-call view: load from `read_inventory`, and after the call
    store D_00810CB2 / D_00810CB7 through `write_charge` /
    `write_battery_capacity` when they changed. Only these two are written.
- **`d810E74`.** `(uint8_t *)&scene->d810E74` (the hosts are little-endian),
  or a 2-byte view of `input->pressed`. It is read only.
- **Message words.** `d2821B0` / `d2821B4` / `d2821B8` =
  `&block.mode` / `&block.phase` / `(int32_t *)&block.line` of the live
  message service's `EmMessageBlock`, as `em_area11_script_host.c` binds
  them.
  - **D_00282240** has no canonical port storage today. `EmItemRoot.message_group`
    plays that role, and it needs one single home that both this page and
    `em_sul_0020B210` (its 4 -> 3 drop) use.
  - Once bound, the page layer's own mode-4 copy of the request block goes
    (CENSUS_STANDINS "delete the stand-ins").
- **`spad3B8D`.** `&scene->spad3B8D`. The page writes 3 itself, so the
  `battery_finished` hook's 3B8D write is retired.
  D_008106C5 = 0xFF reaches 0020CDC0 through `c5` as today.

**Owner workers.**
- `owner_read` / `owner_write` accept only the address D_008106D0 holds. In
  AREA11 that is the bound panel's +0x14 value, 0x7AA590 in the captures.
  Any other address faults, because only the panel is published (W22).
- The fields map as follows:

  | Original offset | Native value |
  |---|---|
  | +3 | the panel's type byte, 0x24 in the capture (the class byte the host already holds for the panel record) |
  | +0x34 | `EmPanel.cost` |
  | +0xA | `EmPanel.charged` |
  | +0xB | `EmPanel.armed` |

- `find_device` = `em_item_device_find` (00185420, verified by
  test_item_device_reference) over the published list. It maps the returned
  owner to its record address (the panel, or 0).

**The four page draws (and the other draw workers).** The original builds
its GS packets inside 002149F0. So during the tick the draw workers must
record the leaf commands (00207D00 blend, 00207E40 sprite, 00207F80
rectangle, 00209280 gauge, the text calls). `em_status_runtime_render`'s
`DRAW_BATTERY` then submits them in that order. That replaces
`em_battery_ui_render`'s block from "Original0020AE40 flags2" through the
arrows, the `battery()` helper and the No/Yes marker sprite.

| Worker | Bind to | Notes |
|---|---|---|
| `background` (0020A7A0) | `em_status_background_render` with the page tile | The TEX0 0x20043C859D422150 is EMBA sprite 26, as em_battery_ui draws it today. |
| `frame` (0020AE40) | `em_sul_0020AE40(sulw, mem, EM_SPR_PAGE_ADDRESS, table, flags)` | Its `battery` worker is `em_status_battery_draw` (00209280, x 0x96, y 0xB4, compact 1). |
| `list` (0020B210) | `em_sul_0020B210(sulw, mem, r->page, r->page_size, table, glyph, flags, &g, result)` | `g` is loaded from and stored to `*d2821B4` / `*d2821B8` / `*d282240` around the call. |
| `arrows` (0020B0D0) | `em_sul_0020B0D0(sulw, mem, table)` | It reads D_00810E70, the held word `scene->d810E70`. |
| `marker` (0020CCB0) | `em_cs_0020CCB0(&rect, r->page, r->page_size)` | `float_to_int` = `em_player_float_to_int`. `rectangle` = the 00207F80 draw that the status hub renders (x/16 - 1792, (y/16 - 1936) * 2). |
| `message_line` (001FCF10) | `em_rvr_001FCF10` with `w_001FCB90` = `em_cs_001FCB90` on the service's `EmCsPresenters` (CENSUS_STANDINS binding) | This replaces the `text(ui, gfx, 0, 270, 408)` Yes/No line. The other `text(..., 138, 336)` lines are the D_002821B8 lines that step F presents from the request the page writes (WP-8b). |
| `cue_accept` / `cue_back` / `cue_cursor` | `em_sul_0020CD40` / `60` / `A0` | |
| `cue_refuse` | `em_spr_0020CD80` | |
| `sound` | the port's 001FB9F0 submit | UI cues 0/1/2/4 stay silent until WP-14 exports them. Unit sound 6 is the same path. |
| `blend` | the ordered overlay blend | |
| `list_refill` (0020BBE0), `list_scroll` (0020BC50) | a faulting worker ("unreachable on the BATTERY page") | Section 2 and the test's single-row check: the list has at most one row, and the original 0020B210 then never raises the wrap event. So state 2 cannot be reached from this page. Neither routine is translated. |

The `mem` of the `em_sul_*` calls:
- the ELF data window 0x265C50..0x266000 (the two tables; an exporter must
  write them, since the port reads no ELF at run time);
- the game-block words D_00810E70, D_00810E78, D_00810C70..72 and
  D_00810C64.. (read only with flag 8), as regions over their canonical
  storage.

The sprites map TEX0 to the EMBA atlas records. `tools/export_panel.py`
writes each record's TEX0 at +24; the current loader ignores it.

**Retire when bound.** Retirement rule 4 applies: a stricter test
supersedes these.
- `em_battery_ui.c`'s tick, begin, phase and render. Keep only the EMBA
  atlas load that the sprite lookup needs, or move it.
- `em_panel_battery_begin` / `em_panel_battery_step`.
- `battery_tick`'s synthesized message fields and capacities.
- `tests/battery_ui_test.c`.
- `tools/test_battery_reference.py`: it ignores the draws and checks a
  reimplementation.
- `tools/test_battery_pickup_reference.py`: it hooks the draws.
- `tools/test_battery_ui_reference.py`: an unordered sprite set.
- The Makefile targets of those three tests.

When those go, the census rows change:
- 002149F0 -> live;
- 0020AE40 / 0020B0D0 / 0020B210 / 0020CCB0 / 0020CD40..A0 / 001FCF10 ->
  live;
- a new row for 0020CD80.

The level smoke's battery phase (the notice in route 01 f220..f459, the
panel confirmation of route 03) then exercises the page.

## 5. Makefile hunk (test target; the lead applies it)

```
.PHONY: test-status-page-record-reference
test-status-page-record-reference:
	python3 tools/test_status_page_record_reference.py
```

When bound, add `src/game/em_status_page_record.c` to `COMMON`.
`em_status_ui_leftovers.c`, `em_census_standins.c` and
`em_render_verify_rest.c` are already there.

## 6. Limits

- **Beat-end captures.** The route captures are beat-end states:
  - states 3, 5, 7 and 8 are not captured;
  - states 3 and 8 are replayed over route 01's records: the take request
    B0 = 1 / B1 = 0x1B, and the list's CROSS with the original 00185420;
  - 5 and 7 are synthetic.
- **Recharge owners.** No AREA11 owner has type 0x2C, so the recharge path
  runs over synthetic records only.
- **00185420 is a boundary.** It is scripted in the synthetic cases and
  executed as original instructions on the captures.
- **Not compared here:** the leaves past their arguments (the packets are
  STATUS_UI_LEFTOVERS 1.1's check) and glyph pixels.
- **Byte pointers into native fields.** The binding notes' byte pointers into
  native `uint16_t` fields (D_00810E74) assume a little-endian host. Every
  port target is little-endian.
