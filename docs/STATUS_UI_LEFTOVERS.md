# Status UI leftovers (census lane L35) and the BATTERY page draw

Date: 2026-09-23. Lane `status-ui-leftovers` (build lane `b7-status-ui-leftovers`).

Scope: every function of census lane **L35-status-ui-leftovers**
(`docs/FIRST_LEVEL_CENSUS.md` section 5), plus the three BATTERY pop-up
functions the critic notes (section 7.2) reclassified: **0020B210** and
**0020B0D0** (stand-in) and **0020AE40** (clock verified, draw unverified).
The user named the BATTERY pop-up: today `em_battery_ui.c` draws it with
hand-placed sprites.

Files (all new; nothing existing was edited):

| File | What |
|---|---|
| `src/game/em_status_ui_leftovers.h/.c` | the translations (prefix `em_sul_`), with workers and fail-stop |
| `tools/test_status_ui_leftovers_reference.py` | the original-instruction oracle (`make test-status-ui-leftovers-reference`, hunk below) |
| `tests/status_ui_leftovers_test.c` | ASan/UBSan fixture for the fail-stop edges (built and run by the oracle) |

The module is **built and tested but not bound** (CLAUDE.md fidelity rules:
an unfinished original module stays unwired until its workers are bound).
The binding notes below say exactly what each translation replaces.

## 1. Result

`python3 tools/test_status_ui_leftovers_reference.py`, quick mode, about 3 s:
PASS. `EM_TEST_FULL=1`, about 40 s: PASS.

| Check | Quick | Full |
|---|---|---|
| 0020AE40: 9 page tables x flag words | 150 of 18,432 cases (1,681 calls) | 18,432 cases (207,360 calls) |
| 0020B0D0: 9 tables x 8 held words | 30 of 72 | 72 |
| 0020BEF0 ring index | 216 | 216 |
| 0020CD40 / 0020CD60 / 0020CDA0 | 3 | 3 |
| 0020B210 list: random pages, every caller flag word, edges | 754 of 12,054 cases (13,678 calls) | 12,054 cases (220,034 calls) |
| 001C5860 band | 321 values | 25,021 values |
| 001C5930 area title, synthetic nodes | 500 of 6,000 | 6,000 |
| 001C4820, synthetic states 0..5 x bind result | 18 | 18 |
| 0021B8E0 / 0021B900 / 0021BAC0 / 0021BA70 / 0021BAB0 | 13 synthetic + 13 per capture | same |
| 0022EBE0 | 2,539 of 65,536 (E4, mode) pairs | 65,536 |
| 001B0BA0 (support, see 2.6) | ELF counts + 20 random tables | + 400 |
| BATTERY packet bytes vs the capture | 2 frames, 5,104 bytes each | same |
| Captures (opening, playable, route beats 00..14) | 17 captures, 4 title frames each | 17 captures, 310 title frames each |
| Fail-stop (missing worker / region / worker fault) | 30 + the C fixture | same |

Also asserted every run: every jal/j target of the translated routines is
hooked or translated (20 targets), and **both outcomes of all 94 conditional
branches** in the translated routines were executed. A mutation check (edit
the translation, rerun) killed every non-equivalent mutation tried (row
colour compare, the band-5 style, the number row y, the ring wrap, the
0x20-exact plate, the bind result test, the quiet-mode set, the 0xD..0xE
promotion range, the 0x564 window base, the 0xC0 save offset, the predicate).

How the comparison works: the oracle executes the ORIGINAL instructions from
the user's ELF (COP1 through `tools/ee_float_model.py`) and hooks every
callee that is not translated. Each hook records its arguments. The native
module gets the same values through its workers. The test compares the
ordered call lists, every byte of the page / node record / render context,
the written globals and the return values. float_to_int (001281C0) and
block_copy (00121870) execute original instructions on both sides; on the
captures the string width 001CC170 does too. The SDK string routines that
00209280 reaches use MMI byte ops the shared interpreter lacks; the test adds
them in its own subclass (`StringEE`, architectural definitions).

### 1.1 The BATTERY page against the original's own packets

`../Extermination/build/startup-reference/panel/` is the original BATTERY
confirmation page (002149F0 state 4). Its RAM still holds the two slot-1
packet buffers the original built for its last two frames. For each buffer:

1. The original 0020AE40(page, D_00265C50, 2), 0020B210(page, D_00265CD0,
   0x20042D05A1322000, 0x402) and 0020B0D0 run unhooked over the capture,
   with the slot cursor set to the buffer start. The 5,104 bytes they build
   equal the captured bytes (the fixture reproduces the frame).
2. The **native** em_sul_0020AE40 / em_sul_0020B210 / em_sul_0020B0D0 run with
   workers that pass every call to the ORIGINAL 00207D00, 00207E40, 00209280
   and 001281C0 in a fresh copy of the capture. The bytes built equal the
   captured bytes, byte for byte (25 calls, 16 sprites; 00209280's own cell
   packets included), and the page bytes and D_002821B4 / D_002821B8 /
   D_00282240 equal the original's.

So the draw stream this module produces is exactly the one that built the
original frame. (The older `test_battery_ui_reference.py` only checks that
each stand-in quad appears somewhere in the buffers, as an unordered set.)

## 2. Per function

Census status before -> after. "verified-unbound" = an original-instruction
oracle executes the function and the translation matches; not wired live.

### 2.1 0020AE40: status sub-page frame (byte-matched C)

- **Before -> after:** live (critic: clock verified, draw unverified) -> **verified-unbound**.
- **What it does:** 00207D00(1, 0); four 256x128 frame sprites (TEX0 words
  +0, +0x10, +8, +0x18 of the page table, colour 0x40808080); flag 0x40 draws
  one wide bottom sprite (+0x40) instead of two (+0x20, +0x28); flag 2 adds
  the gauge backdrop (+0x78, 0x80808080); flag 8 calls 00208AD0(page, 0x1B6,
  0x6E), else flag 2 calls 00209280(page, 0x96, 0xB4, TEX0 +0x70, 1);
  00207D00(1, 3); the corner sprites +0x38 (and +0x30 unless flag 0x40); the
  title plate +0x68 at x 0x71D0 when the flag word is exactly 0x20, else 0x7100.
- **Workers:** blend 00207D00, sprite 00207E40, health 00208AD0, battery 00209280.
- **Data:** the page's TEX0 table (BATTERY: D_00265C50, 0x80 bytes of ELF data).
- **Verification:** 18,432 table x flag cases (full), packet bytes (1.1).

### 2.2 0020B0D0: page arrows (byte-matched C)

- **Before -> after:** stand-in (critic) -> **verified-unbound**.
- **What it does:** 00207D00(1, 3), then two 32x32 sprites at (0x7800, 0x7B30)
  and (0x7800, 0x8240). Held word D_00810E70 bit 0x1000 picks +0x58 (up lit)
  with +0x50; else bit 0x4000 picks +0x48 with +0x60 (down lit); else +0x48
  with +0x50. The stand-in lights both arrows when both bits are held; the
  original lights only the up arrow.
- **Data:** D_00810E70 (the held pad word) and the page table.

### 2.3 0020B210: page list (NEARMISS; followed against the .s)

- **Before -> after:** stand-in (critic) -> **verified-unbound**.
- **What it does**, in order, on the page record (the 0xA0-byte UI block at
  D_00810130 for the status pages):
  1. +0x1A = 0. With a nonzero count +0x18 and no 0x400 lock, D_00810E78 bit
     0x1000 moves the cursor +0x17 up (at row 0 it raises the wrap event 1
     instead), bit 0x4000 moves it down (clamped at count - 1 below four
     rows; at row 3 with four or more rows it raises event 2). A move plays
     0020CDA0; a wrap plays it only with five or more rows.
  2. Refills up to five visible rows +0x90.. from the ring +0x50.. starting at
     the head +0x19, wrapping at the count.
  3. 00207D00(1, 0), then up to four rows (k steps by 0x30, so rows lie 0x180 GS units apart), each two
     128x64 sprites (row record +0 at x 0x77F0, +8 at 0x7FF0; y =
     float_to_int(16.0f * (((k + 0x6A) >> 1) + 0x790))). Compare modes (flags
     0x1F0) colour the row by cursor and by the committed value +0x12 - +0x1E
     (0xFF stays 0xFF): 0x80208080 / 0x80808080 on the cursor row, 0x40208080
     / 0x40404040 elsewhere; mode 0x20 with committed value 2 lights every row
     at or above 2. Plain mode uses 0x80808080 / 0x40404040, and with flag 8
     prints the row number (the byte at D_00810700 + row + (short)+0x1E +
     0x564, through 001C5FB0(n, 2, 0), 00123168 and 001CBA50 at x 0x7AF with
     style D_00265510 on the cursor row, D_00265518 elsewhere).
  4. With rows: unless 0x200, D_002821B4 = 1 and D_002821B8 = the selection
     (mode 0x40: base + 2 for value 2, else 0x3D for 0 and 0x3E otherwise;
     mode 0x20: base + value, promoted to 0x3C when it is 0xC..0xE and
     D_00810C70 + C71 + C72 == 3; otherwise base + value). Then 00207D00(1,
     0), the cursor highlight (glyph argument, 256x64, 0x20808080, at the
     cursor row), the selected-row marker (row record +0x10 of ring entry
     0020BEF0(page), 64x64 at (0x89F0, 0x83E0), 0x40808080; not in mode 0x40,
     and in mode 0x20 only when the sum above is 0 or 3), and D_00282240 4 ->
     3 when +0x1B differs from that ring entry. With no rows D_002821B4 = 0.
  5. Returns the wrap event with five or more rows, else 0.
- **Workers:** blend, sprite, float_to_int 001281C0, sound 001FB9F0 (via the
  translated 0020CDA0), and for flag 8 format 001C5FB0, copy 00123168,
  text_fixed 001CBA50.
- **Globals:** D_002821B4, D_002821B8, D_00282240 (EmSulListGlobals, read and
  written); D_00810E78, D_00810C70..72 and the D_00810700 + 0x564 window
  through the memory regions.
- **Limit:** the page record is caller storage of at most 0x400 bytes; an
  index outside it faults (the original would read neighbouring memory).
  The number string copy faults when it does not fit the 16-byte stack
  buffer (the original would overrun its frame).

### 2.4 0020BEF0: ring index (byte-matched C)

- **Before -> after:** missing -> **verified-unbound**.
- **What it does:** v = +0x17 + +0x19; when v is not below +0x18, v -= +0x18 once.

### 2.5 0020CD40 / 0020CD60 / 0020CDA0: UI cues (byte-matched C)

- **Before -> after:** stand-in (em_hud.c / em_battery_ui.c cue requests,
  silent, WP-14) -> **verified-unbound**.
- **What they do:** one tail call 001FB9F0(id, 0x1000, 0x1000, 0x1000) with
  id 0 (accept), 1 (back) and 4 (cursor). The result is passed back.
- **Worker:** sound 001FB9F0.

### 2.6 001C5930 / 001C5860: area title (NEARMISS 69 % / byte-matched), and 001B0BA0

- **Before -> after:** 001C5930 stand-in (em_hud.c legacy card; lifecycle
  only in em_area11_bindings.c `tick_area_title`) -> **verified-unbound**;
  001C5860 stand-in -> **verified-unbound**; 001B0BA0 (boot, before the
  census window; no row) -> verified-unbound.
- **What 001C5930 does** on the node (+0x04 state, +0x05 title phase, +0x06
  band phase, +0x28 title timer, +0x2A title string index, +0x1F0 band,
  +0x1F4 band timer):
  - state 0: timer 300; index = D_00289B40[D_00810700] (low half) +
    D_00810701; state 1; band = 001C5860(); band timer 300.
  - state 1: the scratchpad mode byte 0x70003B8D equal to 1, 2 or 3 makes the
    node **quiet** (no drawing, and the band part is skipped).
    Title phase 0: unless quiet, the title string D_002671C0[index] centred
    at x 0x800 - width / 2 (001CC170), row 0x7A2, 001CC1E0(1, x, 0x7A2, 0xA,
    0x14, text, 0); the timer counts down and at 0 the phase becomes 1.
    In title phase 0 or 1, a nonzero D_008106B8 sets state 3.
    Unless quiet: band phase 1 resets to 0 when the band changes. Band phase
    0: band 0 just restarts the band timer and moves to phase 1; otherwise,
    once the title phase is 1, the band line D_0026726C[band] is drawn centred
    at 0x896 on the same row, **with the style record D_00265520 for band 5
    and none otherwise**; its timer counts down (at 0: restart, phase 1), and
    a band change restarts it.
  - state 2 or 3: 001AFC10(node).
- **Correction to the decomp C:** the NEARMISS C passes style 0 for band 5
  too. The .s passes D_00265520, and the oracle confirms it (a mutation to 0
  fails). The em_hud.c comment about the gate is also wrong: the mode byte
  values 1..3 silence the node (they do not enable the draw).
- **001C5860:** f = 100.0f - D_008104D8 (EE subtract); the first of 80, 50,
  30, 10, 0 that f is not at or below gives 0..4, else 5.
- **001B0BA0:** builds D_00289B40 (23 entries of {base, count}) from the 23
  halfword counts at D_0024A850; the base advances by the count, or by 1
  when the count is 0. The test checks that every capture holds exactly the
  table 001B0BA0 builds from the ELF.
- **Workers:** text_width 001CC170, text_proportional 001CC1E0, free_actor
  001AFC10. **Data:** D_00810700, D_00810701, D_008106B8, D_008104D8,
  D_00289B40, D_002671C0 and D_0026726C with the strings they point at
  (ELF data), the mode byte 0x70003B8D.
- **Captured states:** every capture holds one live title node (0x7AB150;
  0x7B0390 in 09 after the room move). The opening capture (mode byte 2, so
  quiet) runs the last title frame and the phase change without drawing;
  09_fence_door (mode byte 0) runs a freshly spawned node (timer 240) that
  draws its title every frame and, in full mode, reaches the phase change.

### 2.7 001C4820: placed prop / pickup node (byte-matched C)

- **Before -> after:** missing ("render-only; drawn from the scene props") -> **verified-unbound**.
- **What it does:** state 0: 001B0FD0(node), and 001C6380(node) when it
  returned 0; state 1: 001B17A0(node), then the node's +0x4C method (on
  every capture 001CAA00, the default draw); states 2 and 3: 001AFC10(node).
- **Captured states:** the pickup node at 0x7AAB70 (state 1) in all 17 captures.

### 2.8 Render-context record saves (byte-matched C)

- **Before -> after:** 0021B8E0, 0021B900, 0021BA70, 0021BAB0, 0021BAC0
  missing -> **verified-unbound**.
- **What they do** on the block *(D_00275670): 0021B8E0 copies its 32-byte
  record +0xA0 to +0xE0; 0021B900 to +0xC0; 0021BAC0(slot) to +0x120 + 32 *
  slot; 0021BA70(v) stores the 64-bit v at +0xB0, then 0021B900; 0021BAB0
  returns the 64-bit word +0xB0. The copies go through block_copy 00121870.
- **Captured states:** each capture's own context (0x811CC0 on the route).

### 2.9 0022EBE0: cinematic predicate (C linked from asm; the .s was read)

- **Before -> after:** missing -> **verified-unbound**.
- **What it does:** 1 when D_008101E4 == 3; otherwise 0 when the mode byte
  0x70003B8D is 0 or 4, else 1. Exhaustive over both bytes.

### 2.10 The verified-unbound rows of L35 (binding notes only)

001B0DC0, 001B0EA0, 001B0FD0, 001B1630, 001B17A0, 001B1E20, 001B5B70,
001CA7B0, 001CA940, 001CA990, 001CAA00 and 001CB3C0 are translated in
`em_owner_services_original.c` and verified by
`tools/test_owner_services_reference.py` (see OWNER_SERVICES.md). Here they
are the workers of 001C4820: `model_bind` = `em_owner_services_001B0FD0`,
`place` = `em_owner_services_001C6380`, `publish` =
`em_owner_services_001B17A0`, and `method` dispatches the +0x4C word, which is
001CAA00 (`em_owner_services_001CAA00`) for the AREA11 pickup. 001B5B70 is the
frame step I rumble countdown (ORCH-22) and 001B1E20 the rumble dispatch;
neither is reached from this module.

## 3. Binding (for the coordinator)

Nothing here is wired. To bind, the lead adds `src/game/em_status_ui_leftovers.c`
to COMMON and replaces the stand-ins as follows.

**BATTERY page (`em_battery_ui.c` `em_battery_ui_render`, the block from the
comment "Original0020AE40 flags2 in call order" through the two arrow
sprites, and the `battery()` helper).** Replace with, per frame of 002149F0
states 1, 3, 4 and 8:
`em_sul_0020AE40(w, mem, 0x810130, 0x265C50, 2)`,
`em_sul_0020B210(w, mem, page, 0xA0, 0x265CD0, 0x20042D05A1322000, flags, &globals, &event)` and
`em_sul_0020B0D0(w, mem, 0x265C50)`, with the flag word of the state (state 1:
2, whose return value is the list event; states 3 and 4: 0x402; state 8:
0x602; state 2 draws its list through 0020BC50, not translated here).
- `page`: the original 0xA0-byte page record (D_00810130). Its list fields
  (+0x12, +0x17..+0x1B, +0x1E, the ring +0x50 and rows +0x90) must come from
  the live 002149F0 state 0 translation; the stand-in's `EmPanelBatteryMenu`
  does not hold them.
- `globals`: the live single storage of D_002821B4 / D_002821B8 / D_00282240
  (the message presenter state; `em_interaction_frame.h` names D2821B4).
- `mem`: the ELF data window 0x265C50..0x266000 (the two tables) and the game
  block words D_00810E70, D_00810E78, D_00810C70..72 and D_00810C64.. (only
  read with flag 8).
- `blend`: the ordered overlay blend (`EmGfxOverlayBlend`, modes 0..3).
- `sprite`: look up the TEX0 in the EMBA records. `tools/export_panel.py`
  already writes each record's TEX0 at record +24, but the `em_battery_ui.c`
  loader ignores it. Then draw with the existing mapping: canvas x = x / 16 -
  1792, y = (y / 16 - 1936) * 2, w x h, colour / 128. The stand-in's sprite
  ids are D_00265C50 words 0..15 and the D_00265CD0 row triples 16..24.
- `battery`: `em_status_battery_draw` (em_status_draw.c, 00209280, verified by
  `test_status_draw_reference.py`) with x 0x96, y 0xB4, the TEX0 argument and
  compact 1. This replaces the stand-in's hand-built `battery()` grid.
- `float_to_int`: `em_player_float_to_int` (em_player_stage_workers.c, 001281C0, verified).
- `sound`: the port's 001FB9F0 submit (the em_sfx path).
The stand-in's text lines (notice / error / confirm) and the No/Yes cursor
are 002149F0's own draws after these three calls, not part of this lane.

**UI cues.** Replace the silent `em_hud.c` / `em_battery_ui.c` cue requests
(`HUD_SFX_*` for 0020CD40/60/A0; `EM_ITEM_SOUND_*`, `EM_STATUS_PAGE_BACK_SOUND`)
at their call sites with `em_sul_0020CD40/60/A0(w)`, `sound` bound as above.

**Area title.** In `em_area11_bindings.c`, `tick_area_title` becomes
`em_sul_001C5930(w, mem, raw node, 0x2F0)` on the 001C5930 node, which
replaces the lifecycle-only switch and `em_hud_area_title_stop()`. The legacy
card (`em_hud_area_title` armed from `em_scene.c` and `em_scene_bindings.c`,
and `em_hud_area_title_render`) is then removed. That card uses an OBSERVED
string, a fixed 36-pixel row, white, and no mode gate, and it has no band
line.
- `free_actor`: the existing `free_self_001AFC10` (em_actor_pool_free_001AFC10).
- `text_width`: `em_message_draw_cc170` (001CC170, verified-unbound in
  em_message_draw_original.c).
- `text_proportional`: the 001CC1E0 text boundary the status hub adapter
  already renders (`em_status_hub_ui.c` text commands, proportional 10x20).
- `mem`: D_00810700/701 and D_008106B8 from the scene state
  (`s_scene->req[EM_SCENE_REQ_B8]`); D_008104D8 (the infection float); the
  0x5C-byte D_00289B40 table from `em_sul_001B0BA0` over the 46 bytes at
  D_0024A850; the pointer tables D_002671C0 / D_0026726C and their strings,
  plus the style record D_00265520 (ELF data: an exporter must write them,
  since the port reads no ELF at run time); the scratchpad mode byte
  0x70003B8D.

**Pickup node.** The `em_area11_bindings.c` entry for 001C4820 ("prop:
render-only") gets `em_sul_001C4820` with the owner-services workers in
2.10. The pickup's draw then goes through 001CAA00 instead of the scene-props
draw in `em_render_frame.c`.

**Render context.** `em_render_context.c` (lane L30) declares the worker
`w_0022EBE0`; bind it to `em_sul_0022EBE0(D_008101E4, mode byte)`. The context
saves bind where their callers are translated (001D8FD0 -> 0021B8E0; 0020DFA0
-> 0021BAC0(0); 0022EEF0 -> 0021BA70; 0022EC30 -> 0021BAB0; 0021B970 /
0021B9A0 -> 0021B900) on the port's single mirror of the *(D_00275670)
block, with `block_copy` a 32-byte move inside it.

### Makefile hunk (test target; the lead applies it)

```
.PHONY: test-status-ui-leftovers-reference
test-status-ui-leftovers-reference:
	python3 tools/test_status_ui_leftovers_reference.py
```

When bound, add `src/game/em_status_ui_leftovers.c` to `COMMON`. A private
full build with it appended (lane `b7-status-ui-leftovers`) has zero warnings.

## 4. Limits

- **Route captures are beat-end states.** On every route snapshot the status
  pages are closed (the UI block is idle), so the BATTERY draw is checked
  on the panel capture (the BATTERY confirmation page, the same page as beat
  03) and on synthetic pages, not on a route beat. The title, pickup node,
  context saves and predicate run on all 17 captures.
- **Boundaries:** the 2D GS draw layer (00207D00, 00207E40) and the text
  blitters (001CBA50, 001CC1E0) are compared by their arguments. Section 1.1
  shows that the arguments rebuild the original packets. Glyph pixels and the
  final framebuffer are not compared. 00208AD0, 00209280, 001C5FB0, 00123168,
  001FB9F0, 001AFC10, 001B0FD0, 001C6380 and 001B17A0 are workers here (all
  translated elsewhere, except 001FB9F0's downstream and 001C5FB0, which is
  only inline in the hub).
- **Synthetic widths:** synthetic title nodes use a scripted 001CC170 width.
  The captures use the original's executed width.
- **Per-case coverage:** flag words the first level does not use (for
  example 0x8, 0x20 and 0x40 pages) are verified here but unreached on the route.
- **Not translated:** 0020CD80 (the cue 2 thunk; 002149F0's no-target path,
  not on the census route) and 0020BC50 (the list-close animation).
