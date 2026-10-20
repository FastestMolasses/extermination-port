# Message glyph runs (WP-8)

Status: 2026-09-23. Live: the message service's text draws through it on every
present tick (`em_message_live`, docs/MESSAGE_SERVICE.md "Binding").
2026-10-28: the small font (001CBA50 / 001CBC20, section "The small font")
is translated here too and live for the options screen and the status pages
(`em_page_draw_cba50`).

Files:
- `src/game/em_message_glyph_original.{h,c}`: the native translation.
- `tools/test_message_glyph_reference.py`: the original-instruction oracle
  (`make test-message-glyph-reference`).
- `tests/message_glyph_test.c`: the native fail-stop contract, ASan/UBSan
  (`make test-message-glyph`).
- `em_hud_glyph_strip` / `em_hud_tall_glyph_cell` (`src/game/em_hud.c`): the
  draw boundary, through the tall-font atlas; `em_hud_small_glyph_strip` /
  `em_hud_small_glyph_cell` the small font's.

This module is the message draw module's two workers (docs/MESSAGE_DRAW.md):
`glyph_advance` is 001CBE10 and `draw_text` is 001FC7B0.

## What it translates

| Original | Native | Source read | What it does |
|---|---|---|---|
| 001FC7B0 | `em_message_glyph_fc7b0` | NEARMISS C and the .s | Walks the text. A byte 0x0A recurses on the rest of the text one line lower (y + (cfg word 4 + cfg word 2) >> 1, arithmetic shift) and returns. Other control bytes, 0x80, 0xA0..0xDF and 0xF0..0xFF are skipped. Any other byte starts a run at x + index * cfg word 1 (the multiply's low word): the 0x80-byte run buffer is cleared, then bytes are copied while they are at least 0x20 and the text lasts; a 0x81 byte and the byte after it become one 0x20. A non-empty run goes to 001CC1E0(1, base + 0x700, y + 0x790, 0xA, 0x14, run, cfg style pointer). |
| 001CC1E0 | `em_message_glyph_cc1e0` | asm-only in the decomp; the .s | Its fourth argument is never read. For every byte of the run: a byte of at least 0x20 has the advance 001CBE10(byte) and the glyph index byte - 0x20, or 0x89 for '$'; a smaller byte has glyph 0 and advance 0. Every byte uploads its glyph (001CC8A0(1, strip x, strip y, glyph * 30)), then the strip x and the run width grow by the advance. When the strip x reaches 0x200 the strip is flushed (001CC3B0), x moves on by the run width and the strip restarts at 0. The end flushes the rest. A flush passes (slot, x, y, strip x + 1, strip y + 0x14, run width, the accumulated height, the style). |
| 001CBE10 | `em_message_glyph_advance` | byte-matched C | The tall-font advance of a byte value: a switch whose default is 9. |
| 001CC3B0 | `em_message_glyph_cc3b0` | NEARMISS C and the .s | The colours: with a style, alpha = style byte +4 in both, the outline RGB is the constant 0x100505 and the fill ORs in style word 0; without a style, 0x80100505 and 0x80808080. Style byte +5 (the flag) picks the primitive. The five passes, in order: (x, y - 1), (x + 1, y), (x, y + 1), (x - 1, y) in the outline colour, then (x, y) in the fill. Flag 0: the prebuilt sprite packet D_002510C0 and two vertices, UV (0, 0) at (x, y) and (u_end, v_end) at (x + width, y + height / 2); style byte +7, clamped to 0xF, is ORed into both XYZ2 words at bit 16. Flag non-zero: the prebuilt triangle-strip packet D_00251140 and four vertices, the top edge shifted right by the flag. The NEARMISS C gets the pass order and the strip's vertex coordinates wrong; the translation follows the .s. |
| 001CCB00 | none | byte-matched C | An empty function; the strip reset is 001CC1E0's own zeroing. |

The flush worker receives every GS register value 001CC3B0 packs: RGBAQ, and
UV plus XYZ2 per vertex, as 64-bit words (the sign extensions and the XYZ2 Z
field included), the kind (sprite or strip) and the strip's uploads since the
last reset.

## Verified (last run, 2026-09-23)

`python3 tools/test_message_glyph_reference.py`, about 11 s quick; `EM_TEST_FULL=1`
runs every combination (last full run: 2,580 bank-string cases with 46,008
uploads and 1,944 flushes, 450 synthetic runs with 16,002 uploads and 756
flushes, 640 direct 001CC3B0 cases, 261 advance values; quick: 220, 70, 98). Nothing is hooked away: 001CC8A0 and 001CC3B0 are
observed (their arguments recorded) and then executed, so the original writes
its whole packet.
- Before anything runs, the bytes of every executed routine in the capture
  (roger-encounter) are checked equal to the pinned ELF, and so are D_00264CD0,
  D_00264BF0 and the three prebuilt packets.
- Compared on every call: each 001CC8A0 call's four arguments against the
  native upload (mode 1, strip x, strip y, glyph byte offset), in order; each
  001CC3B0 call's eight arguments against the native flush, and the packet bytes
  it appends at the slot-1 cursor (`*D_00275670 + 0x14`) against an image built
  from the native register values: both DMA refs, the cnt tag, the VIF DIRECT
  code, and every pass's GIF tag, RGBAQ, UV and XYZ2. Bytes the original does
  not write keep the fill pattern on both sides. The strip contents handed to
  each flush equal the uploads since the previous flush. The original may store
  only into the packet area and the cursor word.
- Cases: 001CBE10 for every byte value and five other integers; 001CC3B0
  directly over an argument grid (seven styles: none, the captured default,
  the record flag 8, flag 1, flag 0xFF with byte +7 = 0x10, a sprite style
  with +7 = 0x10, another with +7 = 0x0F; wrapping and negative coordinates;
  negative heights; 400 random argument sets in full mode); every string of
  the global and area-11 banks (215) at the message position with the
  captured style, and in full mode each also with the flag-8 style, a third
  style and the D_00264BF0 template (no style) at three positions; synthetic
  strings of this file's own making: every byte value, the 0x81 expansion
  (also trailing), newline recursion, control bytes inside and at the start
  of runs, skipped run starts (0x80, 0xA0..0xDF, 0xF0..0xFF), a strip that
  reaches exactly 0x200 texels and runs that wrap it more than once, and
  wrapping positions.
- Mutation check: 12 of 13 targeted mistranslations fail it (the pass order,
  the 0xF clamp's bound, the '$' remap, the 0x200 wrap bound, the u_end + 1,
  the 0x81 expansion value, the run base stride, the newline step's config
  word, the 0xF0 skip, the fill's alpha, a changed advance, an added advance).
  The survivor is equivalent: the strip's half height uses an arithmetic
  shift, but a logical one differs only in the top bit, which the 20-bit
  shift into the XYZ2 word discards.

`tests/message_glyph_test.c` (32 checks): missing or failing workers latch a
fault and later calls refuse; text without a terminator inside the supplied
bytes, a run that would fill the 0x80-byte buffer (the original would lose
its terminator), and a missing config fault.

## The packets (since 2026-10-20)

Every 001CC8A0 upload and 001CC3B0 flush the live service's runs make also
builds the original's packet in the render context's slot-1 arena at its
call time, through em_message_live's glyph sink (`em_message_live_set_glyph_sink`,
bound by `em_status_gs_page`; the builders em_uip_001CC8A0 / em_uip_001CC3B0,
docs/STATUS_FIELD.md sections 3a and 3b). The draw below is unchanged; the
packets are what the status pages' GS frame will draw.

## The draw boundary

`em_hud_glyph_strip` draws a flush through the tall-font atlas
(`assets/font.emfn`, the decomp's `tools/export_font.py` from the user's own
RAM dump; its cells are the original 1-bit glyphs expanded as 001CC8A0 does).
Each strip column shows the latest glyph uploaded over it: a tall upload
writes a 32-texel-wide window whose first 12 columns are the glyph, so a later
glyph covers the earlier one from its own x on. Columns 12..31 of a window are
packet memory the original never writes; they are drawn as empty. The passes'
XYZ2/UV values are mapped with the UI canvas offsets 0x700/0x790 (field lines
doubled, 512x448) and drawn with `em_gfx_overlay_glyph` (sprite) or
`em_gfx_overlay_glyph_skew` (strip), modulated by RGBAQ / 0x80.

**Sampling (2026-09-28, the fb2 step).** Both prebuilt packets write
TEX1_1 = 0 before the passes: in D_002510C0 (sprite) and D_00251140 (strip)
the A+D GIF tag's fourth register (qword 5 of each block) is TEX1_1 with
the value 0, so MMAG and MMIN are nearest (read from the user's ELF; the
reference test checks both packets equal to the pinned ELF). The glyph
strips now sample the font atlas nearest (`em_gfx_overlay_glyph_nearest`
around `em_hud_glyph_strip`'s passes); the port's other font quads (the
legacy HUD text, not drawn from these packets) keep the bilinear sampler.
The status pages' blend blocks set TEX1_1 = 0x60 (bilinear) for the page
sprites (FIDELITY_FEATURES.md, the status-screen entry); the glyph packet
sets its own TEX1 after them, so the two do not conflict.

**Drawn as the GS draws it (2026-10-16, GS_EXACT.md 10.4).** Each sprite
pass of a strip (the non-skewed kind) now goes to `em_gfx_overlay_glyph_gs`
(em_gfx.h) with its packet's XYZ2 and UV as given, per run of strip columns
that one upload owns: the Metal overlay pass draws every field pixel from
the GS's own sample, the integer point of its row under the field's draw
offset (OFY 1936.5 on a half-line field), with the sprite's coverage, U
and V as the floor of their exact values in 1/16 texel (the rows' 2 texels
per row are dyadic; the columns' u_end = width + 1 span, 228 over 227 in
the AREA01 title, is not; the GS model's column rule, GS_EXACT.md 3.4,
reads the same texel as the floor of the exact value at every column of
that shape, widths 1 to 512 from any start column), and the CLUT's ink colour: 001CC8A0 expands an ink bit
to texel index 15, and the strip's TEX0 reads the CLUT 001CCB10 uploads
(byte-matched; CBP 0x1B67), a grey ramp whose entry 15 is grey 240 at the
GS's full alpha (`GLYPH_INK` in em_hud.c; the atlas keeps the coverage
only). The AREA01 title at fb2 point 15 is exact since (it was a row off
on either half line and 240 against 225 under the fade before). The skewed
kind (markup tag 3, a triangle strip) keeps the host quad.

Not modelled at the boundary (renderer work, WP for the Original profile's GS
path): the TEST and ALPHA registers, and the skewed kind's GS rasterisation.
The fb2 set shows message text at 09_fence_door and 15_level_exit (the area
title); 15 is compared (tools/test_fb2_pixels.py).

## The small font (001CBA50 / 001CBC20, 2026-10-28)

| Original | Native | Source read | What it does |
|---|---|---|---|
| 001CBA50 | `em_message_glyph_cba50` | byte-matched C | Arguments (slot, x, y, advance, height, text, style). Every byte of the text is one strip cell: glyph byte - 0x20, 0x87 for '$', 0 for a byte below 0x20, uploaded with 001CC8A0(0, column, 0, glyph * 32); the column grows by 16 and the drawn width by `advance`. When the column reaches 0x200 the accumulated height grows by `height`, the strip is flushed with 001CBC20(slot, x, y, width, height, column, 0x10, style), x moves on by the width and the strip restarts. The end adds `height` and flushes the rest (an empty text flushes once with width 0). 001CCB00, the strip reset it calls, is empty. |
| 001CBC20 | `em_message_glyph_cbc20` | NEARMISS C (register colouring only) | References the prebuilt packets D_002511C0 (3 quadwords) and D_00250FC0 (8 quadwords) on both arms of the style test, then one sprite: UV (0, 0) at XYZF2 (x, y) and (u_end, v_end) at (x + width, y + height >> 1, arithmetic), Z 0xFFFFFF. With a style the RGBAQ is style word 0 with byte +4 ORed in at bit 24, and byte +7, clamped to 0xF, is ORed into both XYZF2 words at bit 16 (the Y coordinate's fraction); without one (a null pointer) 0x80808080 and 0. |

The flush worker gets the packed words (RGBAQ, both UV and XYZF2, 64-bit as
the original stores them), the two packet references (address and
quadword count) and the strip's uploads since the reset. The prebuilt
packets set, by their A+D writes (read from the user's ELF by the reference
test, `small_state`): ALPHA_1 = 0x44 (D_002511C0); TEXFLUSH, PRIM 0x156
(sprite, textured, alpha-blended, UV), TEX0_1 the PSMT4 strip at block
0x1B00 (512 wide, TCC, modulate, the CLUT at 0x1B67 as for the tall
strip), TEX1_1 = 0x60 (bilinear) and TEST_1 (D_00250FC0). The tall
sprite packet D_002510C0 is the same block with TEX1_1 = 0.

**Callers.** `em_page_draw_cba50` runs the translation for the options
screen (`em_options_live`, 001CBA50 dispatched by address; a style pointer
of 0 is the original's null) and the status pages (`em_status_pages_live`:
0020B210's 001CBA50 calls and 00208AD0's through `em_status_draw`). Each
flush joins the page's draw stream in order; the D_002511C0 write leaves
the stream's blend mode at 0 (ALPHA_1 = 0x44, 00207D00's mode 0) until
the next 00207D00. The upload worker refuses a glyph the small-font atlas
lacks, the flush worker a slot other than 1 (the page stream's): the
frame is refused and the caller faults. The status pages' tall-font
001CC1E0 calls go to `em_message_live_cc1e0`, as the options screen's
do. This retired the page stream's stand-in text (`em_page_draw_text`,
which drew the glyphs straight from the atlas with alpha 1.0, ignored the
style's +4 and +7 bytes and the packets, and faulted on a null style).
`em_hud_text_color`'s other users (the hub, the BATTERY and ITEM pages)
are unchanged.

**Draw boundary.** `em_hud_small_glyph_strip` maps the two XYZF2 corners
and the UV extent with the UI canvas offsets 0x700 / 0x790 (field lines
doubled, the Y fraction kept) and draws each uploaded 16 x 16 cell's part
of the sprite from the small-font atlas with `em_gfx_overlay_glyph`,
bilinear, modulated by RGBAQ / 0x80 times the ink colour (`GLYPH_INK`,
as above: the same nibble table and CLUT), the RGBAQ alpha, standard alpha
blend, cut to the SCISSOR_1 window. Not GS-exact: the renderer's bilinear
weights, and at a cell edge it blends the atlas neighbour where the GS
blends the strip's next cell (or, at the strip's edge, whatever the wrap
mode reaches); the atlas's white coverage texels stand for the CLUT's
ink / transparent pair. TEST_1 (alpha test never, framebuffer only) is not
modelled. No capture of these pages' text has been compared.

**Verified (2026-10-28).** `tools/test_message_glyph_reference.py` runs
the original 001CBA50 and 001CBC20 (and 001CC8A0's mode 0, 001CCE80,
001CCB00, 001232E0) over the same capture: each 001CC8A0 call's four
arguments (mode 0) against the native upload in order, its 0x100-byte
packet size, each 001CBC20 call's eight arguments against the native
flush, and the 0x80 bytes it appends at the slot-1 cursor against an
image built from the native record (both packet references, the cnt tag,
the VIF DIRECT code, the GIF tag and register list, the UV / RGBAQ / XYZF2
dwords); the original may store only into the packet area and the cursor
word. Cases: 001CBC20 directly over a grid (six styles: none, the
default, +7 = 7, all bytes 0xFF, +7 = 0x10, +7 = 0x0F; wrapping
coordinates and extents; 400 random sets in full mode; quick 96 of 616);
001CBA50 over synthetic strings (every byte value, '$', control bytes,
exactly 32 cells, 33, 64 and 127 cells), seven advance / height pairs
(including 0, negative and wrapping), four positions (including wrapping)
and the six styles, plus the distinct bank strings (quick 50). Mutation
spot checks: the clamp bound 0xF -> 0xE and the '$' glyph 0x87 -> 0x89
both fail it. `tests/message_glyph_test.c` adds the small font's
fail-stop contract (51 checks).

## Capture compare

`python3 tools/test_message_capture.py` (`make test-message-capture`) runs the
level smoke to its refusal phase with `EM_LEVEL_SMOKE_MESSAGE_CAPTURE` and
compares the frame whose step F presents 0x8000001A for the fifth time with
the original's frame of the same moment (its message block holds +0x68 = 5):
the same two text lines, each line's box within one pixel on every edge.

Since 2026-10-10 the original is the PCSX2 fork's capture (decomp
`build/fork_refs/pixels/messages/elevator_refusal`, the canonical route beat
02 replayed to the vsync wait where the block equals v2.6.3's in all 0x70
bytes), compared field to field: its 512x224 displayed field against the
port's presented frame read at each field pixel's place (the glyphs are the
overlay pass over the GS field; `test_fb2_pixels.shown_field`). Last run
(2026-10-10): port (214, 195)..(263, 202) and (214, 208)..(296, 214) against
the fork's (214, 195)..(264, 201) and (214, 207)..(297, 213). The check is
coarse by design; the exact layout is the oracle's.

Before (to 2026-10-09) it compared the v2.6.3 elevator/refusal fixture's
640x480 host screenshot. Run of
2026-09-23: port (268, 419)..(329, 433) and (268, 445)..(370, 458) against
(268, 419)..(330, 432) and (268, 445)..(370, 457); by 2026-10-10, with the
GS field's presentation, the port's first line ends at 434 and that mode
fails. The screenshot is not the field (the host presentation of a
different, hand-played session), so the fork's field replaced it.
`EM_REFERENCE=v263` refuses since 2026-10-10 (exit status 2, a REFUSED
line naming the reason): that fixture has no GS field and no save state
to take one from, and the retired 2.6.3 app cannot record one, so there is
no like-for-like v2.6.3 reference for a field compare.
