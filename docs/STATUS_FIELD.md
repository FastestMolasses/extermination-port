# Status pages in the GS field (side lane "status", 2026-10-20)

Goal (FIRST_LEVEL_AUDIT.md 1b item 2; decomp docs/VIDEO_COMPARE.md, the
demo_level_1019 comparison: segments 5, 8 and 14, MAD 5.0 / 2.2 / about 7):
the Original profile draws the status screen's frames (the MAIN / hub
page, the ITEM page and its children, the take pages) as the original's GS
draws them, so its presented frames match the original's pixel for pixel
there. The non-page overlay pass (message glyphs over world frames, the
letterbox bars, the fade) stays as it is.

## 1. What the port draws today

A status frame has no GS field in the port: the hub, the ITEM / BATTERY
page and the other pages are drawn whole by the GPU renderer
(em_status_runtime_render: em_battery_ui, em_item_ui, em_status_hub_ui,
em_page_draw, em_status_models), at host resolution, from the leaf calls
the translated page code records (00207D00 / 00207E40 / 00207F80, 0020A7A0,
the 001CBA50 / 001CC1E0 text, 001B0000's models). GS_EXACT.md section 9
("Not part of the GS frame"). The decomp's video comparison confirms it:
in demo_level_1019 every captured frame of segment 8 (67) has no field
(`native/frames.json`: `field` false), and segments 5 and 14 have 8 and 29
such frames. So the differences there are the whole page, not only its
text: the page textures (sampled bilinear by the GPU at host resolution),
the text (the port's own font atlas through `em_hud_text_color`, not the
glyph uploads and sprite passes of 001CC8A0 / 001CC3B0) and the background
layers.

## 2. What the original's list is (measured)

The decomp's fork captures (`build/status_field_capture`, section 4) hold
the original's whole list for five page frames: route beat 01's ITEM /
BATTERY page with the take notice ("Found: BATTERY PACK - 6 GAUGE") at
beat frames 230, 300 and 400, and beat 03's panel BATTERY page at 440 and
500. Beat 01 f300's list (81,552 GIF bytes, all VIF DIRECT: no VU1 code)
draws 558 primitives into the 512 x 224 field (FRAME_1 FBP 0 or 0x38, the
field's draw offset 1936.0 or 1936.5): one untextured sprite (the clear),
542 bilinear UV sprites (TEX1 0x60: 0020A7A0's background, 513 of them on
one texture, and the page sprites, CLAMP 5) and 15 nearest UV sprites
(TEX1 0: the message glyph strips' five passes for three text runs), over
19 textures, with the glyph and CLUT uploads in the list itself. The page
has no 3D model (the item picture is a sprite); the hub has (001B0000's
units through the VU1, below).

## 3. Result of this step: the CPU GS model draws the original's lists exactly

`tools/test_status_field_reference.py` (`make test-status-field-reference`;
GS_EXACT.md section 7) runs each captured list through the CPU GS model
(em_gs_raster) from the GS memory it starts from. Before this step the
model left 7, 222, 709, 54 and 0 words of the field off (beat 01 f230 /
f300 / f400, beat 03 f440 / f500), every one in the background's wide
layer (128 texels over 561 or 684 pixels): the UV sprite column rule was
the floor of the exact value, which no capture had tested at a non-dyadic
column gradient. The captures and a designed probe measured the rule
(GS_EXACT.md 3.4): the column coordinate is fixed point, a step truncated
toward zero to 2^-14 of a 1/16 texel, accumulated from the first drawn
column. Now all five lists draw every word of local memory exactly, and the
drawn field equals the field the display showed two loop tops later.

Strict mode (the Original profile's) still refuses part of these pages:
the probe leaves 19 of its 85,314 columns open (GS_EXACT.md 8.8), and the
check that refuses that class refuses 12 sprites on four frames (a row of
bilinear 32-texel sprites 12 pixels wide) and 9 on 03 f500 (eight of them
and the background's wide layer). The rule draws those exactly in every
captured frame; settling the open class is the next measurement.

Unchanged elsewhere: no UV sprite of the first level's world frames has a
non-dyadic column step (a scratch build counting them over the level smoke
through the AREA01 arrival: 105,808 UV sprites, 0 with an inexact step, 0
with a column that changed, 0 refused), so every world field is the same;
the 906 conformance tests are unchanged (703 bit-exact), the
depth-of-field captures stay bit-exact, `make test-level-smoke` and the fb2
harness's quick point pass. The Metal glyph pass (em_gfx_overlay_glyph_gs,
MESSAGE_GLYPH.md) keeps the floor of the exact value for its columns; for
the glyph strips' shape (u_end = width + 1 texels over width pixels from a
whole pixel, nearest) both rules read the same texel at every column of
every width 1..512 (checked), so it stays matching.

## 4. The captures (decomp, ignored)

- `build/status_field_capture/capture.py <beat> <T> ...` (and
  `state:<fork state> <T>`): replays the canonical route beat
  (`build/fork_refs/s87/route/<beat>/trace.json`) open loop in the fork from
  its source state to beat frame T, then at 001D21E0 of iterations T and
  T + 1 reads the EE RAM, the GS memory after list T and the privileged
  registers, at loop top T + 2 the displayed field, and in a second run
  with list T's first tag made END the GS memory list T starts from. Hidden,
  under the run lock, software renderer; about 75 MB a frame.
- `build/status_field_capture/probe_cols.py list | capture | decode`: the
  column probe (decomp tools/gs_conformance.py's harness; 8 tests of 64
  sprites; GS_EXACT.md 3.4).
- The status hub (`state:slot14_status_hub`) and the ITEM root
  (`state:slot12_status_root`) lists hold VU1 code (the models): their GS
  stream is the fork's GS dump, which `capture.py` records in its third
  pass (not yet run to completion: the hub run's hidden launch timed out
  once).

These two scripts are this lane's own code in an ignored folder; copies
for the decomp's `tools/` are in the lane's diff folder (the port lane does
not commit to the decomp).

## 5. What remains (the next steps of this lane)

1. Settle the column rule's open class (GS_EXACT.md 8.8) with a probe aimed
   at it, so strict mode draws the captured pages.
2. Build the pages' GS list in the port from the recorded leaf calls, as
   the original's packet builders do (00207D00's blend block REFs,
   00207E40's sprite packet, 00207F80, 0020A7A0's background sprites, the
   001CC8A0 / 001CC3B0 glyph uploads and passes and 001CCB10's CLUT,
   00209280's number cells), and compare it byte for byte with the
   captured lists (STATUS_UI_LEFTOVERS.md 1.1 does this for the panel
   capture's packets).
3. Draw a status frame in the Original profile as a GS frame through
   em_gs_world (a list frame with the frame's own head), with the page
   textures in the model's local memory (the uploads the page modules send:
   em_gs_texture's steps), presented like a world field; then compare the
   presented frames with the fork's fields and the video comparison's
   segments 8 and 14.
4. The hub and the pages with models (001B0000's units through the object
   kernel's GS output, as the world frames draw them), with GS-dump
   captures; segment 5.
