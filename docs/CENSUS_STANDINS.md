# Census stand-ins: translations of the originals behind them

Lane "stand-in-translations" (2026-09-24). Scope: the census rows
(docs/FIRST_LEVEL_CENSUS.md, recount 2026-09-24) whose live path runs a
stand-in and that had **no translation**: 00102CD0, 001FCB90, 0020CCB0 and
0021BAE0, plus the other mode-3/4 presenters named with 001FCB90 in
MESSAGE_SERVICE.md and FIRST_LEVEL_AUDIT WP-8 decision (b): 001FD0E0,
001FCF90 (with its callee 001FE660) and 001FCF60.

- Module: `src/game/em_census_standins.c/.h` (prefix `em_cs_`).
- Oracle: `tools/test_census_standins_reference.py`. Quick mode takes about
  4 s and `EM_TEST_FULL=1` about 15 s. Both PASS.
- **00102CD0 is bound** (census L13..L16). The other translations are not:
  their stand-ins named below are still live, and the binding notes in
  section 3 say exactly what replaces what.

## 1. Results

| Original | Decomp | Translation | Evidence (oracle) | Live stand-in it replaces |
|---|---|---|---|---|
| 00102CD0 look-at | BM (ee-gcc) | `em_cs_00102CD0` (bit patterns), reusing the verified leaves 001029C0 / 00102918 (em_owner_services_original), 00102718 / 00102760 (em_effect_original) and 001027E0 (em_render_verify_rest) | The original executed with its leaves: every output word on 4,021 random and special vectors (full mode; 171 in quick mode), and on the camera inputs of all 15 route beats. In **every beat** the result equals the matrix the original left in `D_00810610`. `em_cs_view_to_native` of every result is the exact sign flip of the y and z lanes. | none: bound live (the commit 0018C0D0, census L13..L16) |
| 001FCB90 help presenter | BM | `em_cs_001FCB90` | Executed over the captured help container `*D_0028A498`: all 9 groups, every line plus -1 / count, at (0x8A,0xA8) and (0x10E,0xCC). 578 calls in full mode. Every glyph advance, glyph-run draw (x, y, bytes, config words, style bytes) and result is compared, along with the style, measure and line buffers. | The status page layer presents its mode-4 lines from its own copy of the request block (`em_area11_interaction_host.c`, WP-5). The step-F gate `message_gate` holds the service while a page runs. `em_hud.c` has a legacy message lookup. The BATTERY page draws its help text itself (`em_battery_ui.c text(..., 138, 336)`). |
| 001FCF60 record title | BM | `em_cs_001FCF60` | Sub-bank 1 of `*D_0028A49C`: every line plus -1 / count, at (0xA8,0xBE) and (0x64,0x47). 224 calls in full mode. | The same gate, with modes 3/4 faulting in `em_message_live` |
| 001FCF90 record list | NM (.s followed) | `em_cs_001FCF90` | Sub-bank 2 of `*D_0028A49C`: every line at pages 0, 1, n/10, n/10+1 and -1. 446 calls in full mode. | The same |
| 001FE660 segment count | BM | `em_cs_001FE660` | Every line of sub-bank 2 on its own, and inside every 001FCF90 call | (callee of 001FCF90) |
| 001FD0E0 mode-3 cue presenter | NM (.s followed) | `em_cs_001FD0E0` | Two sets of cases. (a) The captured cue bank `*D_0028A4EC`, all 8 lines. (b) A synthetic bank of the test's own ASCII covering 0x0A / 0x0C / 0, record triggers, and a 70-glyph line that overflows into the next line's buffer. Cases cover states 0/1/2/3/-1, the stale-id path, and seven 001FDDB0 result scripts. 001FC9B0 executes original instructions on both sides. Every 001FDDB0 call is compared (slot table, count, glyph line, block bytes), along with the block, D_00820EC0..CC, style and buffers. Both outcomes of every conditional branch are reached, except the two that are one-sided by construction (0x1FD300, 0x1FD33C). | modes 3/4 fault in `em_message_live.c`; the step-F gate |
| 0020CCB0 BATTERY marker | BM | `em_cs_0020CCB0` | All 256 values of page byte +6. float_to_int executes original instructions and the 00207F80 arguments are compared. **Capture evidence:** the native call's arguments were replayed through the ORIGINAL 00207F80. The resulting packet is present in the original's packet buffers: the `panel` capture has the "byte +6 set" packet (x 0x84F0..0x8530), and route beats 03..14 have the "clear" packet (x 0x7FD0..0x80B0). | `em_battery_ui.c` hand-placed `sprite(ui, gfx, 27, no_selected ? 335 : 253, 412, 12, 12, 0x80CE6000)` |
| 0021BAE0 END_PROJECTION | BM | `em_cs_0021BAE0` | block_copy 00121870 executes original instructions on both sides. Every context byte is compared on random 0x400-byte blocks (slots 0..22, and 0x7FFFFFFF and -9, which wrap through the 32-bit shift to offsets 0x100 and 0) and on the real context of all 18 captures at slot 0. Refusals: -10, 23, 0x7FFFFFF0, and no worker. | `status_page_event` `EM_STATUS_PAGE_END_PROJECTION` only clears `world.status_ui_context` |

Mutation check (run once, then reverted): each of the following makes the
oracle fail:
- 001FD0E0 continuing the line after 0x0A (the decomp C's reading);
- the 0020CCB0 x choice swapped;
- the cross-product operands swapped in 00102CD0;
- the 001FCF90 row step changed.

## 2. Findings

1. **001FD0E0 (NEARMISS): the C and the .s disagree.** In the .s, a 0x0A byte
   advances the offset, decrements the stop counter and **ends the current
   line**. The next glyph goes to the next of the four lines. The decomp C
   keeps filling the same line after a 0x0A. The translation follows the .s,
   and the oracle confirms it: the C's reading fails at the first real cue
   line. The stop counter is only a stop flag: 0x0C and 0 zero it, so the
   remaining lines stay empty.
2. **001FD0E0 does not consume records.** Record slots are indexed by the
   line's glyph count at the time of the match. After the four lines, +0x44
   is reduced by the number of matches, so the next call finds the same
   records again. Block +0x2C holds a **stack address**: the original's frame
   line buffer, `sp + 0xC0 + 0x40 * line`. The translation stores
   `frame_address + 0x40 * line`, where `frame_address` is a token. State 0
   saves +0..+0xC through D_00820EC0..CC around 001FC9B0. 001FC9B0 clears the
   absolute block D_002821B0, which is also the block the one caller passes.
   D_00282228 (set to 0 on a stale id) is that same block's +0x78.
3. **Container layout.** `*D_0028A498` and `*D_0028A49C` are containers:
   - header word 0 is the data offset, word 1 the entry count, word 2 the
     data size;
   - 16-byte entries follow at +0x10: {offset, offset/16, size, padded size};
   - each entry is an ordinary message bank (001FE460/480/4B0/4D0 layout).

   The containers hold 9 help groups and 3 record banks. 001FCB90 uses entry
   `group`, 001FCF60 entry 1 and 001FCF90 entry 2. All three banks, the
   configs D_00264CF0 / D_00264C90 and D_00264DB0 (all zero) are
   byte-identical in all 18 captures.
4. **0021BAE0 is not a projection event.** It restores the 32-byte
   render-context record at +0xA0, the one the fog programmer 0021B9A0 works
   on, from slot `slot` at +0x120. The matching save is 0021BAC0(0) in
   0020DFA0, just before 0020DFA0 reprograms that record through
   0021B9A0(5, 0.0, 1000000.0). The port's event name
   `EM_STATUS_PAGE_END_PROJECTION` does not describe this.
5. **The look-at stand-in was the original up to rounding.** The original
   view matrix (row vectors, Y down, +Z into the screen) and the retired
   `em_mat4_lookat_gs` differed exactly by negating lanes y and z of every
   row, with a largest difference of 5.1e-7 relative to the position scale
   (measured before its removal). That remap is `em_cs_view_to_native`, a
   sign-bit flip, which the oracle now checks on every result.

## 3. Binding notes (exact replacements)

**00102CD0: bound (census L13..L16, docs/CAMERA_LIVE.md).**
- The translated commit 0018C0D0 (em_camera_commit_original.c) calls
  `em_cs_00102CD0` for D_00810610 on the canonical camera words; the
  renderer's view is `em_cs_view_to_native` of it. `em_mat4_lookat_gs` is
  deleted: a scene without the live camera, the status UI scene and the
  EM_PROJ_TEST self-check also take the look-at through this pair
  (`camera_view_00102CD0`, em_camera.c).
- `em_census_standins.c` and `em_render_verify_rest.c` are in COMMON.
- test_camera_commit_reference.py (which hooked 0x102CD0) is retired:
  tools/test_camera_live_reference.py executes the whole commit, the
  look-at and its leaves included.

**001FCB90 / 001FCF60 / 001FCF90 / 001FD0E0 → `em_message_live.c`.**
- Build one `EmCsPresenters` bound to the service's own `EmMessageDraw`
  (the `draw_line` worker's context):
  `em_cs_presenters_init(&pres, &draw, &cs_data, &mode3, (int32_t *)&service.block.presenter_78)`.
- Set the service's workers `mode3_present`, `help_draw`, `record_setup` and
  `record_draw` to `em_cs_worker_mode3_present / _help_draw / _record_setup /
  _record_draw` with context `&pres`.
- Mode-3 workers:
  - `mode3.reset`: `em_message_reset(&service)`, which returns 1.
  - `mode3.line_draw` stays NULL. 001FDDB0 has no translation, so a mode-3
    request still faults, as today. The census shows no 001FD0E0 row on the
    route.
- Data, from the user's own files (`tools/export_message_data.py`, a
  version bump of `.emmd`):
  - the two containers `*D_0028A498` and `*D_0028A49C`, and the cue bank
    `*D_0028A4EC`. Their disc source files are not identified yet: the
    captures hold them at 0xB05000, 0xB0B800 and 0x11739C0. The exporter
    must find their chunk/file the way it found the global and AREA11 banks.
  - the ELF's D_00264CF0 and D_00264C90 configs (words 0..4) and D_00264DB0.
  - the style blocks the configs' +0x14 point at (D_00275830 and D_00275820
    in the captures): an `EmMessageTextStyle` each, owned next to D_00275C50.
- Then delete the stand-ins:
  - the host's `message_gate`. Its hook becomes NULL, which is the
    original's no-gate behaviour.
  - the status page layer's own mode-4 copy of the request block
    (em_area11_interaction_host.c, WP-5).
  - `em_status_hub_ui_render`'s separate `help_line` presenter call.
  - the `em_hud.c` legacy lookup.
  - the BATTERY page's `text(..., 138, 336)` and `text(ui, gfx, 0, 270, 408)`
    calls. Those positions are 001FCB90's (0x8A, 0xA8) and 001FCF10's
    (0x10E, 0xCC) in canvas coordinates. The page logic writes the request,
    and step F presents it.
- This is WP-8 decision (b): the gate goes when these presenters are bound.

**0020CCB0 → the BATTERY page draw.**
- Replace `em_battery_ui.c`'s marker call,
  `sprite(ui, gfx, 27, ui->menu.no_selected ? 335 : 253, 412, 12, 12, 0x80CE6000)`,
  with `em_cs_0020CCB0(&rect_workers, page, page_size)`.
- `page` is the original page record whose byte +6 is the selection; the
  stand-in's `no_selected` plays that role. Its callers are the page
  routines 002149F0 / 002160B0 / 00211970 / 00215870 / 00217090 / 002177B0 /
  00217FA0 / 00218640 / 00218D90. In the first level that is 002149F0, the
  BATTERY page.
- Workers:
  - `float_to_int` = the live 001281C0 translation
    (`em_player_stage_workers em_player_float_to_int`);
  - `rectangle` = the 00207F80 draw the status hub already renders
    (`em_status_hub_ui.c` `EM_STATUS_HUB_UI_RECTANGLE`: x/16 - 1792,
    (y/16 - 1936) * 2 on the 512x448 canvas). This is an untextured
    rectangle, not atlas sprite 27.
- Bind it together with the em_status_ui_leftovers BATTERY page draw
  (0020AE40 / 0020B0D0 / 0020B210, lane L35). The rest of `em_battery_ui.c`
  is the same kind of stand-in.

**0021BAE0 → `status_page_event` `EM_STATUS_PAGE_END_PROJECTION`.**
- Call `em_cs_0021BAE0(&sul_workers, context, context_size, 0)` on the bytes
  of the render context D_00275670 points at, with the same `block_copy`
  worker as `em_sul_0021BAC0`.
- It only restores what was saved, so bind it together with the
  `EM_STATUS_PAGE_CONFIGURE` side: 0020DFA0's 0021BAC0(0)
  (`em_sul_0021BAC0`) and 0021B9A0(5, 0.0, 1000000.0). 0021B9A0 is missing
  (census L-row 0021B9A0), and 0020DFA0 is unverified.
- Until the render context +0xA0 record has an owner that the renderer's fog
  reads, the flag clear stays. Binding the copy alone would move bytes
  nothing reads.

## 4. Conventions and limits

- **Fail-stop:**
  - A NULL worker, a read outside a supplied bank or container, or a store
    outside 001FD0E0's modelled frame (sp+0xC0..sp+0x858) faults (-1). The
    presenters latch the fault in `EmCsPresenters.fault`.
  - Where the original would read unrelated memory (an out-of-range group,
    a record past a bank), the translation faults instead.
  - 0020CCB0 and 0021BAE0 check their workers and ranges before any call.
- **Floats:**
  - 0020CCB0 uses em_ee_float.h (`cvt.s.w`, `mul.s`).
  - 00102CD0 inherits the float models of the reused leaves.
    em_effect_original's 00102718/00102760 have no refusal path. The oracle
    never refused an input in the sweep, and the zero-forward, denormal and
    huge cases included.
- **Not compared:** glyph pixels (the 001FC7B0 boundary) and the 00207F80
  packet beyond its arguments. The packet is checked against the captures
  instead.
