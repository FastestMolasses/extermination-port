# Message glyph runs (WP-8)

Status: 2026-09-23. Live: the message service's text draws through it on every
present tick (`em_message_live`, docs/MESSAGE_SERVICE.md "Binding").

Files:
- `src/game/em_message_glyph_original.{h,c}`: the native translation.
- `tools/test_message_glyph_reference.py`: the original-instruction oracle
  (`make test-message-glyph-reference`).
- `tests/message_glyph_test.c`: the native fail-stop contract, ASan/UBSan
  (`make test-message-glyph`).
- `em_hud_glyph_strip` / `em_hud_tall_glyph_cell` (`src/game/em_hud.c`): the
  draw boundary, through the tall-font atlas.

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

Not modelled at the boundary (renderer work, WP for the Original profile's GS
path): the prebuilt packets' TEX0/CLUT (the port's atlas is white with alpha
coverage), their TEX1 = 0 (nearest sampling; the overlay glyph sampler is
bilinear), TEST and ALPHA registers, and the GS rasterisation of a sprite
whose texel span (u_end = width + 1) is one texel wider than its pixel span.

## Capture compare

`python3 tools/test_message_capture.py` (`make test-message-capture`) runs the
level smoke to its refusal phase with `EM_LEVEL_SMOKE_MESSAGE_CAPTURE` and
compares the frame whose step F presents 0x8000001A for the fifth time with
the elevator/refusal capture's screenshot (its block holds +0x68 = 5): the
same two text lines, each line's box within one pixel on every edge at
640x480. Last run (2026-09-23): port (268, 419)..(329, 433) and
(268, 445)..(370, 458) against the original (268, 419)..(330, 432) and
(268, 445)..(370, 457). The check is coarse by design (one pixel at 640x480
is less than one GS pixel horizontally); the exact layout is the oracle's.
