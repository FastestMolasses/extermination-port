# Message presenters: the rest (001FDDB0 and the mode-3/4 data)

Lane "presenter-helpers" (2026-09-25). Scope: what the mode-3 / mode-4
presenters of docs/CENSUS_STANDINS.md (001FCB90, 001FCF60, 001FCF90,
001FD0E0) still lacked before the chain can bind them and delete the
step-F gate stand-in (FIRST_LEVEL_AUDIT WP-8 decision (b)):

- the cue line walker **001FDDB0**, which had no translation, so
  `mode3.line_draw` stayed NULL and a mode-3 request faulted;
- their **data**, which no exporter wrote.

- Module: `src/game/em_message_presenter_rest.c/.h` (prefix `em_mpr_`).
- Exporter: `tools/export_message_data.py` also writes
  `assets/message/message_presenters.emmp` (ignored). `message_data.emmd`
  stays byte-identical (checked against the existing asset: same SHA-256
  `111c69ef…688c`).
- Oracle: `tools/test_message_presenter_rest_reference.py`. Quick mode
  takes about 3 s, `EM_TEST_FULL=1` about 12 s. Both PASS.
- **Not bound.** The binding notes are in section 3.

## 1. Results

| Original | Decomp | Translation | Evidence (oracle) |
|---|---|---|---|
| 001FDDB0 cue line walker | NM (.s followed) | `em_mpr_001FDDB0`, adapter `em_mpr_worker_line_draw` (an `EmCsMode3Workers.line_draw`) | **B.** 29 hand-made cases and a random sweep (120 quick, 2,000 full; 52,227 compared calls in full mode) over records of the test's own. They cover tags 0/1/2/3/4/7/0x20, gate modes 0/1/2/-1, zero and wrapping counts, unsigned edges of the gate compare, counts -1..0x46 (the 0x40 cap), NUL / low / high glyph bytes, config 0 (template) and pen extremes. 00121A28, 001FC770 and 001CC170 execute original instructions. 001CBE10 and 001FC7B0 are hooked as in the message-draw oracle, and 001FB9F0 is hooked. Compared: every draw (x, y, text, config words, style bytes), every advance and sound call, the result, every block byte, D_00275C50..57 and D_00820ED0..D_00821010. Every store of the original lands on a modelled byte. **C.** 001FD0E0 runs with 001FDDB0 **executing** on the original side, and the native side runs `em_cs_001FD0E0` bound to `em_mpr_worker_line_draw` over the data parsed from the exported `.emmp`. The test runs every real cue line (0, 2, 4, 6 of `*D_0028A4EC`) frame by frame from state 0 until the page is marked done: 260 / 192 / 164 / 296 frames, all compared. Quick mode runs line 4 to the end and 12 frames of the others. Over B and C, both outcomes of all 14 conditional branches of 001FDDB0 are reached. The callees are asserted to be exactly {00121A28, 001FC770, 001CC170, 001FB9F0}. |
| mode-3/4 data | — | `em_mpr_data_parse` / `em_mpr_data_load` → `EmMprData.cs` (an `EmCsMessageData`) | **A.** Every field of the exported file equals the bytes at the original's address in all 18 captures: the help container, the record container, the cue bank, both configs, both style blocks and D_00264DB0. Both configs' +0x14 point at D_00275830 / D_00275820 in every capture. The native parse is checked field by field. Five malformed images are refused (magic, version, one byte short, one byte long, header only). |

Mutation check (run once, then reverted). Each of these makes the oracle
fail:
- the pen advanced before the draw (the decomp C's reading);
- tags 2/3/4 skipping glyph i (the C's reading);
- a passed gate skipping glyph i (the C's reading);
- a skipped glyph not advancing the run index;
- a signed gate compare;
- drawing the tail run at return 1.

## 2. Findings

1. **001FDDB0 (NEARMISS): the .s and the decomp C disagree in two ways.**
   - The run is drawn at the **current** pen, and the pen then advances by
     the run's width. The C advances the pen first.
   - After a record with tag 2, 3 or 4, after tag 0 or any other tag, and
     after a **passed** gate (block +0x40 > record +8, unsigned), glyph i is
     **emitted**. The C skips it for tags 2/3/4 and for a passed gate. Only
     the gate's two completion paths (a zero count, or a countdown reaching
     0) skip glyph i. 0x20 and a waiting gate return 0.
2. **A skipped glyph still advances the run index.** The run buffer keeps
   a 0 at that index, so on the frame a gate opens, the glyphs after it up
   to the next record are drawn as an **empty** run. The pen still moves by
   their width. The oracle saw this in the case 'gate finishes', where the
   draws are "" at x 55, "AB" at 55 and "" at 73. On the next frame the gate
   is passed and the whole run draws. This is original behaviour: it lasts
   one frame per gate.
3. **The run after the last record is not drawn** (return 1 without a
   flush). The loop runs through `i == count`, so a record matched at a
   row's terminator is stored at index `count` and flushes everything. In
   the real cue bank this is measured on the first frame of every page:
   - The last row of each page ends with such a record, so it draws in
     full.
   - Every other row's last record sits on its final glyph. That glyph is
     one byte below 0x20, and each of the 18 0x0A bytes in the bank
     follows one. It is left in the run and never drawn. It would not draw
     anyway: 001CC170 gives it no width. Nothing visible is lost.
4. **The real cue bank uses only tag 1 (gates).** It has 200 records on
   lines 0/2/4/6, and every count is non-zero. The gate sound is
   001FB9F0(0x8C9, 0x1000, 0x1000, 0x1000). Tags 2/3/4/0x20 and the zero
   count path are reached only by the synthetic cases.
5. **D_00820F50 is `EmMessageDraw.measure + 0x80`.** The draw module's
   measure buffer covers D_00820ED0..D_00820F90, so the walker's run buffer
   is part of the storage the message service already owns. It is not a
   second copy.
6. **The cue bank is 0xF5C bytes**: header, entries and records up to 0xD10,
   then the string section. docs/CENSUS_STANDINS.md 3 says "0xD10 of
   0x1000", but 0xD10 is where the string section header starts. The
   census oracle already measured 0xF5C (md.bank_extent). Both containers
   are exactly their header's word 0 + word 2: 0x62A0 and 0xF500. Every
   extracted file matches the captured RAM through its last byte,
   including the sector padding.
7. **The original's cue address is 0x011739C0 in all 18 captures.** It is
   a heap load address, so neither the ELF nor the disc holds it. The
   loader takes it as a parameter. With that value, the words 001FD0E0
   stores (block +0x18 / +0x1C and the slot tables) equal the original's.

## 3. Binding notes (for the chain)

In `em_message_live.c`, next to the `EmCsPresenters` of
docs/CENSUS_STANDINS.md 3:

- Data: `em_mpr_data_load(&mpr, "assets/message/message_presenters.emmp",
  0x011739C0)`. Pass `&mpr.cs` as the `data` of `em_cs_presenters_init`.
  `mpr` must not move afterwards, because its configs point at its own
  styles. Free it with `em_mpr_data_free`.
- Line walker: `em_mpr_line_draw_init(&walker, &draw, &mpr.cs, ctx, sound)`
  on the **same** `EmMessageDraw` as the presenters, which the service's
  `draw_line` worker also uses. `sound` is the 001FB9F0 submit with the
  em_sul signature (negative return = fault). The status UI leftovers'
  `EmSulWorkers.sound` adapter fits as is.
- `EmCsMode3Workers mode3 = { &walker, <reset = em_message_reset>,
  em_mpr_worker_line_draw }`. Nothing is left NULL.
- Then the stand-ins listed in docs/CENSUS_STANDINS.md 3 go, with the step-F
  gate `message_gate` first (WP-8 decision (b)).

Makefile hunks (the lead applies them):

```make
# source list, after src/game/em_message_live.c \
           src/game/em_message_presenter_rest.c \

.PHONY: test-message-presenter-rest-reference
test-message-presenter-rest-reference:
	python3 tools/test_message_presenter_rest_reference.py
```

## 4. Conventions and limits

- **Fail-stop.** Each of the following faults (-1, latched in
  `EmMprLineDraw.fault`): a NULL sound worker when a gate arms (after the
  gate's stores, as in the original order), a slot outside the cue bank or
  unaligned, a colour index outside D_0026EC10, and a block +0x20 that is
  neither 0 nor the supplied `config_264C90_address`. The oracle checks each
  one, plus the latched fault refusing the next call.
- **The line pointer.** The walker reads glyph i from the `line` argument,
  which is the buffer block +0x2C names. `em_cs_001FD0E0` passes exactly
  that buffer. i < 0x40 keeps every read inside it.
- **No floats.** 001FDDB0 has no float arithmetic.
- **Not compared:** glyph pixels (the 001FC7B0 boundary) and the sound
  engine behind 001FB9F0.
