# Status pages in the GS field (side lane "status", 2026-10-20)

Goal (FIRST_LEVEL_AUDIT.md 1b item 2; decomp docs/VIDEO_COMPARE.md, the
demo_level_1019 comparison: segments 5, 8 and 14, MAD 5.0 / 2.2 / about 7):
the Original profile draws the status screen's frames (the MAIN / hub
page, the ITEM page and its children, the take pages) as the original's GS
draws them, so its presented frames match the original's pixel for pixel
there. The non-page overlay pass (message glyphs over world frames, the
letterbox bars, the fade) stays as it is.

## 1. What the port draws today

Since step 3d (section 3d) the Original profile draws the BATTERY page's
frames as the original's GS field. The rest of this section is the state
before it, which still holds for the hub, the ITEM root and the other
pages. Those status frames have no GS field in the port: they are drawn
whole by the GPU renderer
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

Strict mode (the Original profile's) still refused part of these pages
after this step (12 sprites on four frames, 9 on 03 f500): the probe left
19 of its columns open. Step 1 of section 5 settled that (section 3c):
strict mode now draws all five pages exactly. The live page frames met
the row rule's open variants too; step 3d settled those (section 3d).

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
every width 1..512 (checked; checked again for the rule of section 3c, at
every start column mod 4), so it stays matching.

## 3c. Step 1: the column rule settled, strict mode draws the pages (2026-10-20)

Four designed probes in the fork (decomp `build/status_field_capture`:
`probe_cols.py` p10_cols, and this step's `probe_open.py` p11_open,
`probe_open2.py` p12_open and `probe_holdout.py` p13_holdout, hidden, under
the run lock, about 1 s each; 2,048 UV sprites of height 1, about 360,000
decoded columns) measured the UV sprite column rule (GS_EXACT.md 3.4): 16.16
fixed point set up in binary32 from the left corner, for bilinear sampling
the filter's coordinate U - 0.5 texel truncated toward zero, then four
columns at a time in groups aligned to window x mod 4, each lane its own
truncated binary32 offset and each group a truncated four-column step. The
first three probes found it (one parameter a test: the old rule's open
columns were a start offset per x mod 4 and per sub-pixel start); the
fourth was captured after it was written down and every one of its columns
is predicted (random sprites of every kind, starts whose binary32
association matters, XYOFFSET X at 1025 and 1024.5 pixels). em_gs_raster
draws by it (`uv_cols_init` / `uv_cols_u16`); strict mode then refused
only where the start's sum order, which no probe told apart, would change
a drawn column. Step 3d's held-out probe settled that order (section 3d).

Proof:
- `make test-status-field-reference` (extended): the five captured BATTERY
  page lists bit-exact through the model in both modes, strict mode
  refusing nothing (it refused 12 / 12 / 12 / 12 / 9 sprites before), and
  all four probes exact in both modes. Mutants of the rule, 9 of 9 killed
  (GS_EXACT.md section 7).
- `make test-gs-raster-reference`: 703 of 906 bit-exact as before (931 of
  1,158 with EM_TEST_FULL=1), every open count unchanged; its strict cases
  now 18 (the old open class's sprite a control, also under XYOFFSET X 1
  and 4 pixels off; the sum-order case refused).
- Unchanged elsewhere: a scratch build comparing every drawn UV sprite
  column with the rule before over the level smoke through the AREA01
  arrival (one GS worker): 105,808 UV sprites, 40,594,432 columns, 0
  changed, nothing refused, the run passing through a01_arrival; the
  depth-of-field captures (`make test-dof-pass-reference`, EM_TEST_FULL=1),
  `make test-gs-world`, `make test-ui-packet-reference` (EM_TEST_FULL=1),
  `make test-status-gs-page`, `make test-level-smoke` and the fb2 harness's
  quick point pass.

## 3a. Step 2: the page packets built natively, byte for byte (2026-10-20)

`src/game/em_ui_packet_original.{h,c}` (prefix `em_uip_`) translates the
leaf builders a status frame's slot-1 chain is made of: 00207D00 (the blend
preset REF of GS block bank F), 00207D90 (SCISSOR_1), 00207E40 (the UV
sprite), 00207F80 (the flat rectangle), 00208040 (the flat triangle, its
conversions through a 001281C0 worker), 001CC8A0 with 001CCE80 (the small
and tall glyph uploads to GS block 0x1B00), 001CBC20 (the small-font strip
flush) and 001CC3B0 (the tall-font flush; its register values are
em_message_glyph_cc3b0's). Each appends the original's packet at the slot
cursor (render context + 0x10 + 4 * slot) through a memory callback by
original address, writing exactly the bytes the original writes: a byte the
original leaves alone keeps what the buffer held. Its REF targets are the
original addresses (the GS blocks, the ELF's glyph packets D_002511C0 /
D_002510C0 / D_00251140 / D_00250FC0). Bound since step 3 (section 3b) for
the BATTERY page and the glyph runs.

Proof, `make test-ui-packet-reference` (tools/test_ui_packet_reference.py,
quick about 1 s, `EM_TEST_FULL=1` about 1.5 s):

- **The original instructions** (the shared EE interpreter over the 01
  f300 image, its routines checked equal to the pinned ELF): 1,703 argument
  sets in full mode (quick: every edge case and a sample, 600), every byte
  of a filled scratch area and the cursor equal, the original storing
  nothing else. Fail-stop: 28 native refusals (no cursor word, a packet past
  memory or over its cursor, a glyph outside the font, no font or nibble
  table, a failing worker, the latch), each writing nothing. Mutations
  killed: 15 of 16 (zero / sign extension of three colour words, the lane
  order of SCISSOR_1, the blend mode map, the tall upload's texel offsets,
  the transfer's RRH and the small upload's QWC, the alpha clamp's bound, a
  written tag byte +2, the triangle's trailing zero, the flush's VIF code and
  strip size, the sprite's UV height shift, the small flush's V shift and
  its half height's arithmetic shift); the
  survivor is equivalent (00208040's two conversions in the other order:
  the worker is pure).
- **The captured chains.** Each of the five frames' slot-1 chain (01 f230 /
  f300 / f400: 599 calls; 03 f440: 627; 03 f500: 551; the background, the
  page sprites, the small and tall uploads and flushes, the marker
  rectangle) decodes completely into these builders, and the native
  builders, run twice over two fills from the captured cursor, write the
  captured bytes at every byte they write (65,337 to 82,933 a frame), each
  cursor landing on the next captured packet. The bytes neither run writes
  are exactly three classes the original leaves alone too: the DMA tags'
  unsent words (+2 and +8..+F), the REGLIST pads of 001CC3B0's passes and
  the tall uploads' unfilled texels (001CC8A0 fills 12 x 20 of the 32 x 32
  it sends). The chain's open and close (001D1AE0's three REFs, step V's
  REF of GS block +0 and 001D1F80(1, 0, 7); em_load_veil_particles and
  em_frame_kick, tested by their own oracles) match their formulas.
- **Drawn.** The captured list with the rebuilt chain in place, its
  unwritten bytes left as the fill, through the CPU GS model (as section
  3): every frame's drawn field equals the displayed field, and local
  memory differs from the original's only inside the glyph strip's pages
  (GS block 0x1B00: 656 / 656 / 656 / 496 / 0 words), the unfilled texels
  the nearest-sampled passes never read. So the port need not reproduce
  the stale slot memory to draw the field exactly.

## 3b. Step 3, first part: the live game builds the packets (2026-10-20)

`src/game/em_status_gs_page.{h,c}` (prefix `em_sgp_`) binds the callers to
the builders over the one render context (em_render_context_live): the slot
cursor D_00275670 + 0x14 and the frame's slot-1 buffer (D_0028F700 + 0xC9000
+ index * 0x95760, the frame head's), the only bytes its memory callback
hands out (a packet past the buffer is a fault, not a write into slot 2's
buffer). It is installed in `main.c` after the message service, in both
profiles (the packets are the original's memory; the Enhanced profile runs
the same logic):

- **The BATTERY page** (002149F0 through em_battery_page_live): em_battery_ui
  has a sink (`em_battery_ui_set_sink`), so every leaf call the list accepts
  (00207D00, 00207E40, 00207F80, 00209280's text) builds its packet at its
  call time, in the call order. 00209280's text is 001CBA50 (the small
  font; new `em_uip_001CBA50`, NEARMISS C, the .s followed) or 001CC1E0 (the
  tall font; em_message_glyph's run with this module's own workers) over the
  style record's bytes.
- **0020A7A0 at its call time.** The page's background now steps the shared
  D_002655A0 inside 002149F0, where the original runs it
  (`em_status_background_live_step`: its 00207D00 and 00207E40 calls become
  packets with the call's TEX0), and the GPU render draws that step's
  sprites (`em_status_background_render_step`) instead of stepping at
  render time. The hub, the ITEM root and the other pages still step at
  render time (the next part).
- **The glyph runs** (em_message_live's glyph sink): every 001CC8A0 upload
  and 001CC3B0 flush of the message service (the take notice, the area
  title, the dialogue) builds its packet at its call time, in every frame.
- **The fonts** are the resources' bytes rebuilt from the user's
  `assets/font.emfn` (`src/game/em_ui_font_raw.{h,c}`, prefix `em_ufr_`: the
  export holds every glyph slot as one texel per bit, so the inversion is
  exact); D_0026E350 is `em_ufr_nibbles` (bit i of n -> nibble i = 0xF).
  Without the export the game does not start (fail-stop, as without
  `assets/gs_library.emgm`).

The GPU renderer still drew the page after this part; section 3d presents
the BATTERY page's packets as the field. EM_STATUS_GS_DUMP=<file> records each frame's slot-1 bytes
between the frame head and step V (em_status_gs_page.h).

Proof:

- `make test-ui-packet-reference` (extended): 001CBA50 against the original
  instructions (with 001232E0, 001CCB00, 001CC8A0, 001CCE80 and 001CBC20
  executed: 36 quick / 81 full argument sets, every byte of a 0x6000-byte
  area and both cursors equal; empty, '$', control and high bytes, 31 to 70
  bytes across the 32-cell strip flush, slots 0 / 1 / 2, edge advances and
  heights), 6 new fail-stop checks (no NUL, a run past memory whose first
  upload fits: nothing written, no fonts, no table, no text, no flush
  cursor), and the fonts: both resources rebuilt from the export equal the
  capture's every byte (12,270 and 8,192), `em_ufr_nibbles` equals the ELF's
  D_0026E350. Mutations killed: 9 of 9 (the '$' glyph, the control bound,
  the flush column, the restart at x + width, the last flush's U, the
  advance, a tall font byte's nibble order, the nibble table, the
  terminator check).
- `make test-status-gs-page` (new, `tools/test_status_gs_page.py`, about
  16 s with the game run; EM_TEST_FULL=1 about 25 s): the headless game to
  the battery take with the dump.
  All 1,122 dumped frames (261 BATTERY frames, 861 glyph-run frames) decode
  completely into the builders' packets (816 skewed glyph strips by shape).
  Route 01's f300 and f400 are built **byte for byte** by the live page
  (every call, the background included; the 76,521 written bytes equal the
  native builders' and the capture's) at the frames the original's own
  counts give: the port's first BATTERY frame (route 01's state 5 step 3,
  f219) + (T - 219) - the 29 hub frames of the smoke's START visit (each one
  0020A7A0 on D_002655A0, which no screen resets; route 01's capture session
  had no such visit). So the port's background has stepped exactly as often
  as the original's. f230's frame by that rule precedes the port's page; a
  port frame equals it but for the background. The captured list with the
  port's page bytes in place (their unwritten bytes as the port's arena
  held them) through the CPU GS model draws the displayed field exactly
  (f300; EM_TEST_FULL=1 f400 too), local memory off only inside the glyph
  strip pages. EM_TEST_FULL=1 runs on to route 03's panel BATTERY page
  (1,528 frames, 369 BATTERY): its f440 and f500 equal port frames in every
  call after the background (the take prompt, the marker rectangle
  00207F80, the gauge; 21,837 and 4,241 written bytes equal), and each
  captured background equals exactly one port frame's (3188 and 4045), 60
  page frames apart as f440..f500: the shared D_002655A0 at the same step
  count, reached through another history (the capture session's status
  visits are not the smoke's). Mutations of the live binding: 3 of 4 killed (a sprite's
  TEX0, the glyph flushes not built, the background's 00207D00); the
  survivor (the style record's glyph byte read from its flag byte) is
  equivalent on this route: the white record's colour word already has
  0x80 in its top byte, which the glyph byte is ORed into.
- **Unchanged elsewhere.** `make test-level-smoke` passes (rand order, the
  render context's 307 status frames and their step V lists, the audio
  rows); its per-tick log is identical to a build of the branch head
  without this step on all 1,981 ticks, every field. The GPU renderer's
  presented frames at three BATTERY ticks (the page's first frame, the
  notice at port frame 2982, the list) are byte-identical to that build's.
  `make all` 0 warnings.

## 3d. Step 3, second part: the BATTERY page in the GS field (2026-10-20)

The Original profile now draws every BATTERY page frame (the battery take's
ITEM / BATTERY page with its notice, the panel's BATTERY page) as the
original's GS draws it, and presents that field. Four pieces:

- **The frame** (`em_status_gs_page`): em_status_runtime_render's BATTERY
  draw asks `em_sgp_field_claim`; with the GS frame enabled the GPU draws
  nothing for the page (em_battery_ui_render is not called) and the message
  service's GPU glyph strips are not drawn over it (em_message_live_render:
  their packets are in the list). At step V, after 001D2300's kick and the
  load veil's frame, `em_sgp_kick` walks the kicked list (em_rcl_kick; DMA
  tags CNT, NEXT, REF, REFS, CALL, RET, END, TTE off; the data at original
  addresses in the render context's storage) into its VIF stream, keeps the
  DIRECT data (any other VIF code faults: a status page's list has no VU1
  code), and hands the GIF data, the head included, to
  `em_gfx_gs_gif_frame` with the slot's draw environment's FRAME_1 /
  SCISSOR_1 (em_rcl_draw_env). A BATTERY draw whose page built no packets,
  a list that CALLs the background, or any refusal faults.
- **The model** (`em_gs_world_gif_frame`, src/gs/em_gs_world.h): the
  recorder's checks over the stream (every textured kick's TEX0 known and
  reading resident or drawn memory; a host-to-local transfer's blocks
  resident from the transfer on: the glyph uploads to block 0x1B00 are in
  the list), then the stream whole through the model that holds the GS
  registers (the parser, or the direct worker), strict mode, on the game's
  thread while the workers wait (the parser with a decoded texture cache of
  its own for that frame: p95 8.6 ms a page frame on this Mac; without it
  about 20 ms), presented as a list frame (its XYOFFSET_1, the half line,
  is the one the stream leaves). The original's lists set every state
  register their predecessor set (the five captures run from a reset model
  give the same field), so the frame does not depend on the world frames
  before it.
- **The memory the list reads.** The page module's chunk (module 0x21, the
  BATTERY atlas, and every page module: one VIF1 chain of whole-page
  PSMCT32 transfers, MODULE_LOADER.md) reaches the model as the area
  load's uploads do (em_status_runtime's loader_chain, em_gfx_gs_upload);
  00200970(1)'s restore (library slot 0x35 and the player texture packet)
  does too, as em_gs_texture's restore step blocks (em_gs_texture_step_chain:
  one 8 x 8 PSMCT32 transfer per block, the same bytes); the boot's
  001CCB10 (decomp func_001CCB10.c / func_001CCBD0.c, both byte-matched)
  clears blocks 0x1B00..0x1B80 (the glyph strip page) and uploads the grey
  ramp CLUT to 0x1B67 (em_uip_001CCB10_chain, its 001CCE80 header the
  oracle-tested transfer_header; em_sgp_install sends it). Every block the
  five captured lists read comes from module 0x21's step, the list's own
  glyph transfers, or 001CCB10.
- **The row rule.** In 19 + 26 of the port's 369 page frames the
  background's wide layer (corners bottom to top, from above the field)
  drew a row that the row rule's open variants read differently, and strict
  mode refused it. Two designed fork probes (decomp
  `build/status_field_capture/probe_rows.py` p14_rows and the held-out
  `probe_rows2.py` p15_rows_holdout, hidden, under the run lock, about 2 s
  each; GS_EXACT.md 3.4) measured the rule: binary32 accumulation from the
  TOP corner at the first row DRAWN of V - h (h = 0.5 bilinear), the start
  (vT - h) + p d, and the same sum order for the columns. Every pixel of the
  832 probe sprites fits; the rule before fits 226 of p14's 448, and the
  other sum orders leave 512 / 72 pixels of p15 off. Strict mode no longer
  refuses UV sprites (GS_EXACT.md section 6).

Proof:

- `make test-status-gs-page` (extended; about 17 s, EM_TEST_FULL=1 about
  76 s): with EM_PRESENT_LOG, all 261 BATTERY frames to the battery take
  (369 through the panel, EM_TEST_FULL=1) are presented as GS fields; the
  fields of port frames 2983 and 3083 (route 01's f300 and f400 by the
  counts of section 3b) hash equal to the fields the original displayed;
  001CCB10's CLUT kick equals GS block 0x1B67 in every capture (3; 5 with
  EM_TEST_FULL=1). EM_TEST_FULL=1 runs the game again with frame captures at
  those two frames: the presented picture, the overlay pass included, at
  every field pixel's place (test_fb2_pixels.shown_field) is the displayed
  field, 0 of 114,688 pixels off at both. Route 03's f440 / f500 are not
  compared by pixels: their backgrounds are the shared D_002655A0 at
  another step count (section 3b).
- `make test-status-field-reference`: the five captured lists and all six
  probes (p10 .. p15) bit-exact in both modes, nothing refused.
- `make test-gs-raster-reference`: 703 of 906 bit-exact as before (931 of
  1,158 with EM_TEST_FULL=1), its output line for line the same as the
  branch head's but for the timings; its three UV strict cases are now
  controls.
- `make test-dof-pass-reference` (EM_TEST_FULL=1): the 8 captures bit-exact.
- `make test-gs-world` (thread sanitizer): new GIF frames (a transfer and
  the sprites reading it in the stream) draw the field one EmGs draws from
  the same memory, with the same fields and memory under 1, 2, 3 and 8
  workers; a read of unwritten memory and a GIF frame inside a recording
  fault.
- **Unchanged elsewhere.** `make test-level-smoke EM_TEST_FULL=1` passes
  whole (the main route through the AREA01 arrival, present-again with its
  3 status entries, and the side, aim, damage, branch, options and audio
  runs; the status_pages side run's page module loads now reach the model
  too). Against a build of the branch head over the same main-route run:
  the 13,813-tick log is identical line for line, and of the 15,466
  committed frames (EM_PRESENT_LOG) exactly the 369 BATTERY page frames
  differ (GPU frames before, GS fields now); every other frame's kind and
  field hash is the same, so the world fields, and with them the fb2
  harness's points, are unchanged (its quick point passes as before).
  `make test-ui-packet-reference` passes; `make all` 0 warnings.
- The GPU renderer (EM_GPU_RENDERER=1, the Enhanced profile's path) is
  unchanged: no field is claimed, em_battery_ui_render and the GPU glyph
  strips draw the page as before (the d3d12 / Vulkan backends refuse a
  GIF frame and never get one: their GS frame is off).

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
- `build/status_field_capture/probe_open.py`, `probe_open2.py` and
  `probe_holdout.py` (`list | capture | decode`, same harness): p11_open,
  p12_open and p13_holdout under `build/status_field_capture/probe_open`
  (section 3c).
- `build/status_field_capture/probe_rows.py` and `probe_rows2.py` (`list |
  capture | decode`, same harness): p14_rows (its live set from the port's
  dump of the BATTERY frames, written by the lane's scratch script) and
  p15_rows_holdout under `build/status_field_capture/probe_open` (section
  3d).
- The status hub (`state:slot14_status_hub`) and the ITEM root
  (`state:slot12_status_root`) lists hold VU1 code (the models): their GS
  stream is the fork's GS dump, which `capture.py` records in its third
  pass (not yet run to completion: the hub run's hidden launch timed out
  once).

These scripts are this lane's own code in an ignored folder; copies for
the decomp's `tools/` are in the lane's diff folders (the port lane does
not commit to the decomp).

## 5. What remains (the next steps of this lane)

1. (done, section 3c) Settle the column rule's open class (GS_EXACT.md
   8.8), so strict mode draws the captured pages.
2. (done, section 3a) Build the pages' packets natively, byte for byte.
   001CCB10's CLUT is not a slot packet (a boot-time kick on its own DMA
   channel); its 64 entries are the grey ramp em_hud's GLYPH_INK reads.
3. Bind the page code's leaf workers to these builders over
   em_render_context_live's arena: (done, section 3b) the BATTERY page and
   the message service's glyph runs; (done, section 3d) the BATTERY page's
   frames drawn and presented as the GS field, with the page module
   uploads, the restore and 001CCB10 in the model's memory. Still to bind:
   em_status_pages_live (its 00207D00 / 00207E40 / 00207D90 / 00208040 /
   001CBA50 / 0020A7A0 dispatch cases, em_page_draw's recorder), the ITEM
   root (em_item_ui) and the hub's 2D layer (em_status_hub_ui), each with
   its 0020A7A0 at call time; then em_status_runtime_render's DRAW_ITEM /
   DRAW_PAGE / DRAW_OTHER cases claim the field as DRAW_BATTERY does (their
   lists are DIRECT only too: the ITEM root's picture is a sprite; check
   with a capture), and the video comparison's segments 8 and 14 are
   compared again.
4. The hub and the pages with models (001B0000's units through the object
   kernel's GS output, as the world frames draw them), with GS-dump
   captures; segment 5.
