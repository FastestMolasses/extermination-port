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

**Bound live since the status UI step (2026-09-26):** section 7 says how.
Sections 1..6 are the translation and its oracle; section 4's notes are the
plan the binding followed.

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

## 4. Binding details (beyond section 7)

Section 7 is the live binding. The pre-binding brief's details that section 7
does not restate:

- **Call site.** `em_spr_002149F0(&workers, &records, &fault)` runs once per
  frame from the ITEM root's child page 5, where the original calls
  002149F0; a -1 goes to the runtime's `fail()` with `fault.address`.
- **Page exits.** The page itself writes UI +1..+6 on its exits: +1 = 3
  with +2..+5 = 0 is the ITEM return; +1 = 6 is the request-6 exit. The
  page writes 0x70003B8D = 3 itself.
- **Inventory bytes.** D_00810C7F / CB2 / CB7 are a per-call view of
  em_pickup's `read_inventory` / `write_charge` / `write_battery_capacity`
  (only CB2 and CB7 are written). One canonical copy through
  `em_scene_progress_at(scene, 0x810C7F, 3)` / `(0x810CB2, 2)` /
  `(0x810CB7, 1)` would be the cleaner home.
- **Owner.** `owner_read` / `owner_write` accept only the address D_008106D0
  holds: in AREA11 the bound panel's +0x14 value (0x7AA590 in the
  captures); any other address faults.
- **Worker arguments.** `frame` (0020AE40) draws its gauge through
  `em_status_battery_draw` (00209280, x 0x96, y 0xB4, compact 1); `arrows`
  (0020B0D0) reads D_00810E70, the held word; `marker` (0020CCB0) is
  `em_cs_0020CCB0` with `float_to_int` = `em_player_float_to_int` and its
  rectangle the 00207F80 draw the status hub renders
  (x/16 - 1792, (y/16 - 1936) * 2); the background (0020A7A0) tile's TEX0
  0x20043C859D422150 is EMBA sprite 26. `list_refill` (0020BBE0) and
  `list_scroll` (0020BC50) fault: the list has at most one row, so the
  original 0020B210 never raises the wrap event and state 2 cannot be
  reached from this page (section 2). Neither routine is translated.
- **em_sul memory.** The ELF data window 0x265C50..0x266000 (the two tables,
  exported) and the game-block words D_00810E70, D_00810E78,
  D_00810C70..72 and D_00810C64.. (read only with flag 8), as regions over
  their canonical storage.
- **Census.** 002149F0, 0020AE40 / 0020B0D0 / 0020B210 / 0020CCB0 /
  0020CD40..A0 / 001FCF10 are live (FIRST_LEVEL_CENSUS.md section 1.21);
  0020CD80 is bound but the route never reaches it (no census row).

## 5. Makefile

`src/game/em_status_page_record.c` is in `COMMON`; the target is
`test-status-page-record-reference`.

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

## 7. Live binding (status UI step, 2026-09-26)

The ITEM root's child page 5 (`em_status_runtime.c` battery_tick) runs
002149F0 through the runtime's `battery_page` hook, which the AREA11 host
binds to `em_battery_page_live_tick` (src/game/em_battery_page_live.c).

- **Records.** The runtime owns the one UI block D_00810130 (0xA0 bytes;
  0020E060's RESET_UI clears it). EmStatusPage / EmItemRoot are views of its
  +0..+6, +8, +0xC, +0x10, +0x11, +0x15, +0x16, loaded before the call and
  stored after. D_008106B0 / B1 / C5 are the page's request bytes (the
  canonical ones around the live page tick). D_00810C7F / CB2 / CB7 are a
  per-call view of read_inventory; a changed charge or capacity is stored
  through write_charge / write_battery_capacity (the acquisition writes
  both). D_00810E74 is this frame's pressed word; D_00810E70 / D_00810E78
  (the held and repeat words, `EmStatusInput.held` / `.repeat`) are the em_sul
  memory regions. D_002821B0 / B4 / B8 / D_00282240 are the page's message
  words, themselves the runtime's view of the live message block
  (`message_words`; docs/MESSAGE_SERVICE.md "Binding"). D_008106D0
  and 0x70003B8D come from the scene state in the host.
- **Draws.** 0020A7A0 / 0020AE40 / 0020B210 / 0020B0D0 / 0020CCB0 and their
  leaves (00207D00, 00207E40, 00207F80, 00209280's em_status_battery_draw
  with the hub's resident text) record the frame's leaf calls in
  `em_battery_ui` (the EMBA atlas and the ordered leaf list); the status
  render submits them once, each sprite's TEX0 looked up in the atlas (a TEX0
  it lacks fails the render). The tables D_00265C50 / D_00265CD0 are the
  atlas records' TEX0 words in the ELF order (the exporter reads them from
  the ELF). 001FCF10 presents through the live presenters
  (`em_message_live_help_draw`); its glyph passes draw at the frame's step-F
  render, before step F's own.
- **Workers.** The cues 0020CD40 / 60 / A0 / 0020CD80 and the unit sound 6 go
  to the status cue binding (silent until WP-14 exports the system set);
  00185420 is the host's device lookup over the published list (only the
  panel is a device owner: another device faults); the owner record is the
  bound panel (+3 its record's type 0x24, +0x34 EmPanel.cost, +0xA / +0xB
  EmPanel.charged / .armed). 0020BBE0 / 0020BC50 fault (unreachable here).
- **Retired** (rule 4, superseded by this oracle and the level smoke):
  em_battery_ui.c's page (tick, begin, phase, render, texts), the runtime's
  synthesized message fields, `em_panel_battery_begin` / `_step` (a second
  copy of states 4/5/6), the host's `battery_finished` / `owner_available`
  hooks, tests/battery_ui_test.c, tools/test_battery_reference.py,
  tools/test_battery_pickup_reference.py, tools/test_battery_ui_reference.py
  and their targets. tests/status_runtime_test.c binds the page as the
  AREA11 host does (em_battery_page_live_tick with fixture owner, device
  lookup, cue and presenter workers, the message words and the hub's
  gauge records) and runs the runtime-level scenarios on it: Yes with the
  discharge (units at 1 and 31, charge 10 then 8, owner +0xA / +0xB, mode
  byte 3) and the release; default No; Back into the ITEM root (its
  512-triangle trail) and BATTERY again; the exit's module 0x32 reload
  gate; a failing charge write during the discharge and a failing
  capacity write on the pickup (both -1 with ownership and inventory
  kept); the pickup's notice, the empty device lookup (0020CD80's cue 2)
  and the outer exit; an unbound page faults. tests/panel_runtime_test.c
  drives the translation for its discharge.
- **Evidence.** The level smoke's battery phase (the 239-frame notice, then
  the list) and panel phase (the confirmation, Yes, the discharge 12 -> 8
  and the completion row for row against route 03), both on this page.
  `EM_LEVEL_SMOKE_PAGE_CAPTURE=4:<file.bmp>` saves the confirmation frame:
  it shows the original's layout of `startup-reference/panel/confirm.png`
  (title, frame, gauge 06/06, the three-line confirmation and Yes / No with
  the marker by No; ignored `build/captures/battery_page/after.png`).

