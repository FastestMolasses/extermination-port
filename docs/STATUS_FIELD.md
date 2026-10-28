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
frames as the original's GS field, since step 3e (section 3e) the ITEM
root's, since step 3f (section 3f) the ITEM children EQUIPMENT's and
EVENT's and since step 3g (section 3g) the hub's DATABASE page's. The rest
of this section is the state before them, which still holds for the hub
and the other pages. Those status frames have no GS field in the port: they are drawn
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

## 3e. Step 3, third part: the ITEM root in the GS field (2026-10-20)

The Original profile now draws every ITEM root frame (0020EE50 state 1:
0020F170 and 0020F2A0, the root of ITEM with its five regions, the analog
glow and, while a region is lit, its help line) as the original's GS draws
it, and presents that field.

**What the original's list is (measured).** Hidden fork captures of the
root (section 4: the fork state slot12_status_root, as it is and with the
port's neutral stick 0x80 held) show a DIRECT-only list (no VU1 code: the
root draws no model; section 4's earlier note said otherwise without a
capture): 86,432 GIF bytes, 1,553 primitives into the field: the clear,
0020A7A0's background, 0020F170's and 0020F2A0's UV sprites and 0020AC70's
sixteen fans. Each fan is 001D66A0 (through the forwarder 00208AB0): a CNT
packet of 0x86 quadwords, PRIM 0x4C (an additive Gouraud triangle strip),
33 centre / rim vertex pairs (64 triangles, 32 of them not degenerate).
The CPU GS model draws all four captured lists bit-exact in both modes, all
of local memory (`make test-status-field-reference`), so the Gouraud
triangles needed no new rule.

**The builders.**
- `em_uip_001D66A0` (em_ui_packet_original; decomp src/func_001D66A0.c is
  NEARMISS, the .s followed for every operation and operand order): the
  fan's EE FPU arithmetic on em_ee_float (MULA / MADD / MSUB included);
  the four sin / cos results (0011E2A8 / 0011DE90 of the angle and of
  pi / 32) come from the caller's workers, float_to_int (66 calls) from
  the memory callbacks; every value is computed before the first store, a
  cursor not quadword aligned is refused (the original's quadword store).
- `em_item_root_draw_0020F170` / `em_item_root_draw_0020F2A0`
  (em_item_root; decomp src/func_0020F170.c and src/func_0020F2A0.c, both
  byte-matched C): the leaf calls, in order, with the original's
  arguments; 0020F2A0 is entered after 0020D930 (the caller's hover
  update) and calls 0020AC70 with 248.0 / 208.0 (D_700038A0) and reset 0.

**The binding.** em_status_runtime's ITEM worker calls them at their call
time through a sink (`em_status_runtime_set_item_sink`, bound by
em_status_gs_page in both profiles): 0020F170's 0020A7A0 steps the shared
D_002655A0 there (em_status_background_live_step; em_item_ui draws that
step's sprites in the GPU renderer, `em_item_ui_background_stepped`,
instead of stepping again), and 0020AC70 becomes 00207D00(1, 1) and the
sixteen fans: em_item_trail_step_fans hands each 00208AB0 call (its
vector, its colour int, its angle and the four worker results the fan's
triangles use) to the sink, which builds 001D66A0(1, pos, {c, c, c, 0},
D_00273580, angle). The colour quadword's fourth word is a stack word
0020AC70 never writes (its frame's top word). Measured: every captured
fan holds 0 there (test_ui_packet_reference's decode asserts the colour
quadword is (16, 16, 16, 0)), so the sink passes 0. Why the word is 0 is
a hypothesis, not measured: it may be the top word of the 128-bit $ra
that 0020D930, called just before at the same depth, saved. The word is
the centre vertex's alpha. Blend mode 1 (additive, FIX 0x80) does not use
it for the colour, but the Gouraud-interpolated alpha is written to the
framebuffer's alpha bits, so a nonzero value would change GS local memory
(the all-local-memory checks pass because it is 0 in every capture).
D_00273580, the rim colour, is sixteen zero bytes of the ELF's data that
nothing writes. em_status_runtime_render's DRAW_ITEM claims the
field as DRAW_BATTERY does (em_sgp_field_claim); the help line's glyph
runs are the message service's packets (section 3b). The dump's records
carry flag 2 for an ITEM root frame and, with flag 4, the 96 bytes of
D_002655A0 before the frame's 0020A7A0 (em_status_gs_page.h).

Proof:

- `make test-item-trail-reference` (extended, about 10 s): on 43 ticks of
  stick history the sixteen fans built natively (em_item_trail_step_fans'
  sink into em_uip_001D66A0, the colour words as the binding passes them)
  equal the packets the original instructions of 0020AC70 / 001D66A0
  write, every byte of a filled area (1,486,080 bytes) and the cursor; and
  001D66A0 alone over 400 cases (random positions, colour words, slots,
  cursor offsets and worker results: the sin / cos hooks return the same
  random bits to both): every byte of a filled area (1,068,800), the
  cursor, the four worker calls' arguments and order, no store of the
  original outside its packet, its cursor and its stack; fail-stop: an
  unaligned cursor and a failing float_to_int write nothing.
- `make test-item-root-reference` (extended): 0020F170 and 0020F2A0
  executed (tools/export_item_root.py's executor) for the hover byte 0..7
  and 0xFF: their 185 leaf calls, in order, equal the native draws'; a
  refusing worker at any of them stops the draw.
- `make test-ui-packet-reference` (extended): the four captured ITEM root
  chains decode into the builders (a fan at rest: every ring slot of zero
  magnitude, its angle from the capture's ring) and the native builders
  rebuild them byte for byte (97,325 written bytes each; the fans' sin /
  cos by the live workers em_item_sdk_math, the 0x7F stick's angle
  -3 pi / 4 among them); the rebuilt list drawn: the displayed field, no
  word of local memory off.
- `make test-status-gs-page EM_TEST_FULL=1` (section 6 of the test; `--item`
  alone about 96 s, the whole EM_TEST_FULL=1 run about 5.5 min): the level smoke's status_pages side run with the dump.
  All 1,650 dumped frames decode; its 54 ITEM root frames are each the
  background, 0020F170's and 0020F2A0's packets (and the help line's glyph
  runs) and are all presented as GS fields; the 45 at rest with hover 0
  equal the capture slot12_status_root_pad8080 f0040 in every call after
  the background. The original was then given the port's D_002655A0 of
  port frame 3467 at its loop top 39 (capture.py CAPTURE_WRITE, the only
  write; captures slot12_status_root_pad8080_bg3467 f0040 / f0041): port
  frames 3468 and 3469 equal them in every call (549, the background
  included; 97,283 written bytes equal the builders' and the capture's),
  their presented fields hash equal to the displayed fields, and the
  presented pictures, the overlay pass included, are 0 of 114,688 pixels
  off at both.
- Mutations: of em_uip_001D66A0, 6 of 7 killed (MSUB as MADD, the rim's
  0.5 as 0.8, the quadword count, an unwritten rim word, the update order,
  the alignment check); the survivor (MADD as MUL then ADD) equals the
  original unless a product overflows binary32, which no trail input
  reaches (|x|, |y| < 4096, radius at most 44, |sin|, |cos| at most 1).
  Of the draws, 4 of 4 (a region's colour, 0020F170's blend dropped,
  0020AC70's y, the lit region); the trail sink's vector, 1 of 1; the live
  binding (test_status_gs_page --item), 3 of 3 (the fan colour's fourth word as
  the colour, 0020AC70's 00207D00(1, 1) dropped, 0020F170 not called).
- **Unchanged elsewhere.** Against a build of the branch head (e47a495):
  the main route through the AREA01 arrival, its 13,813-tick log identical
  and all 15,070 committed frames of the same kind and field hash (no
  ITEM root frame on it); the status_pages side run, its 5,674-tick log
  identical and of its 6,932 committed frames exactly the 54 ITEM root
  frames differ (GPU frames before, GS fields now). The GPU renderer
  (EM_GPU_RENDERER=1, the Enhanced profile's path): the side run's tick log
  identical and the presented frames at four ITEM root ticks (one lit, with
  its help line) byte-identical; that comparison was one run against a
  build of the branch head, not a committed test.
- **The GPU renderer's half of the binding.** `make test-item-ui-reference`
  (its fixture, tests/item_ui_test.c): after em_item_ui_background_stepped
  the next render draws that step (its token, the ITEM tile's rectangle
  the render's own step is given) and does not step D_002655A0 itself; the
  token is consumed, so the render after it steps on its own; a refused
  draw fails the render and is consumed too. Mutations 3 of 3 killed (the
  token not consumed, the stepped draw skipped, the wrong token). Not
  covered by a committed test: em_status_runtime's item_background handing
  the sink's token to em_item_ui_background_stepped (its fixture,
  `make test-status-runtime`, does not link, below), and a live
  once-per-ITEM-root-frame count of em_status_background_live_steps in the
  GPU renderer's run (only the one-off comparison above).
- `make test-item-ui-reference`, `make
  test-item-sdk-math-reference`, `make test-status-field-reference`
  (EM_TEST_FULL=1), `make test-level-smoke` pass; `make all` 0 warnings.
  `make test-status-runtime` does not link at the branch head already
  (step 3d's em_sgp_* and em_gfx_gs_* calls have no stubs in its
  fixture); its fixture gained this step's stub only.

Not compared by pixels: the lit frames and those of a moving stick (their
fans hold the stick's history, which no capture shares yet), and the page
transitions into and out of the root (section 5).

## 3f. Step 3, fourth part: EQUIPMENT and EVENT in the GS field (2026-10-21)

The Original profile now draws every frame of the ITEM children EQUIPMENT
(00214570) and EVENT (00215870) as the original's GS draws it, and
presents that field. HEALING (002160B0) is not bound yet: its 0020AE40
call (flags 8) draws 00208AD0's health gauge, whose arcs go through
002082B0, a builder not translated yet.

**What the original's list is (measured).** Hidden fork captures from the
state slot12_status_root (section 4: the stick to the region, Cross, the
page open; EQUIPMENT opens at loop top 31, EVENT at 33, after 0020EE50
state 3's module load 0x20 / 0x22) show DIRECT-only lists (no VU1 code):
EQUIPMENT 108,400 GIF bytes, 549 primitives (the clear, 0020A7A0's
background over TEX0 0x200425859D422100, 0020AE40's plates, 0020B210's
rows, 0020B0D0's arrows and the description's four tall glyph runs);
EVENT 52,000 bytes, 525 primitives (an empty list: no event item is
owned). The page's own text in these frames is the message service's
(001FE070 / 001FCF10: its glyph uploads and flushes, bound since 3b); the
pages call 001CBA50 / 001CC1E0 and 00207F80 directly in no captured frame.
The frame whose Circle leaves EQUIPMENT (state 1's 0x20 test returns
before any leaf) builds no slot-1 packet at all: its list is the head, one
primitive (the clear), captured at loop top 72. The CPU GS model draws all
seven captured lists bit-exact in both modes, all of local memory
(`make test-status-field-reference`).

**The binding.** `em_status_pages_live` has a packet sink
(`em_status_pages_live_set_sink`, bound by em_status_gs_page in both
profiles). On a tick of a bound page every leaf reaches it at its call
time, in the call order, before em_page_draw records it for the GPU
renderer: 0020A7A0 steps the shared D_002655A0 there
(em_status_background_live_step; em_page_draw draws that step,
`em_page_draw_background_step`, instead of stepping again at the
render), 00207D00 and 00207E40 become their packets as the BATTERY page's
do, 00207F80 and the text runs go to the BATTERY page's glue (s_rectangle,
s_text; a text run without a style record or with a slot other than 1
faults). A leaf with no builder (00207D90, 00208040, 00208AD0's gauge,
0020AC70's trail, 001B0000's models) faults on a bound page.
em_status_runtime_render's DRAW_PAGE claims the field for a bound page's
frame through `em_sgp_field_claim_page`, which, unlike em_sgp_field_claim,
accepts a frame whose page built no packets (the Circle frame: the
original kicks the head alone then). The dump marks a bound page's frame
with flag 8.

**Field parity.** The captured page frames T = 60 / 62 draw into FBP 0x38
with the half-line offset (XYOFFSET_1 Y 1936.5), T + 1 into FBP 0 (1936.0).
A first pair of written-background captures was taken against port frames
of the other parity (FBP 0 first): every call equal, but every field pixel
a few levels off (the background's bilinear sprites sampled half a line
apart); replaying the captured list over the port's own GS memory (a
scratch dump) drew the displayed field exactly, so the memory was right
and the parity the difference. Those captures were deleted; the pairs
below are against port frames of the captures' parity.

Proof:

- `make test-status-gs-page EM_TEST_FULL=1` (section 7 of the test, the
  same status_pages run as section 6; `--item` alone runs both, about
  90 s): all 1,696 dumped frames decode; its 100 bound page frames are
  all presented as GS fields: 54 equal the capture
  slot12_status_root_equipment f0060 and 44 slot12_status_root_event
  f0062 in every call after the background, and 2 are empty (port frames
  3450 and 3534, the Circle frames), each presenting exactly the displayed
  field of slot12_status_root_equipment_circle f0072. The original was
  given the port's D_002655A0 of port frame 3421 (EQUIPMENT) and 3511
  (EVENT) at loop top T - 1 (capture.py CAPTURE_WRITE, the only write):
  port frames 3422 / 3423 equal slot12_status_root_equipment_bg3421 f0060
  / f0061 and 3512 / 3513 equal slot12_status_root_event_bg3511 f0062 /
  f0063 in every call (627 and 529, the background included; 85,413 and
  62,391 written bytes equal the builders' and the capture's), their
  presented fields hash equal to the displayed fields, and the presented
  pictures, the overlay pass included, are 0 of 114,688 pixels off at all
  four and at the Circle frame 3450.
- `make test-ui-packet-reference EM_TEST_FULL=1`: the seven new captured
  chains decode into the builders and are rebuilt byte for byte (16
  chains now), each drawn to the displayed field.
- Mutations of the live binding (test_status_gs_page --item): 3 of 5
  killed (EVENT not bound, the background stepped at the render, an empty
  frame claimed through em_sgp_field_claim). The survivors (the tall text
  run's style dropped, a text run handed to the small font) are
  equivalent on this route: no bound frame calls 001CBA50 / 001CC1E0; the
  text path was then reduced to the BATTERY page's verified s_text.
- **Unchanged elsewhere.** Against a build of the branch head with round
  3e's change (e47a495 + that patch): the main route through the AREA01
  arrival, its 13,813-tick log identical and all 15,070 committed frames
  of the same kind and field hash; the status_pages side run, its
  5,674-tick log identical and of its 6,932 committed frames exactly the
  100 EQUIPMENT / EVENT frames differ (GPU frames before, GS fields now).
  The GPU renderer (EM_GPU_RENDERER=1): the side run's tick log identical
  and the presented frames at four bound-page ticks (two EQUIPMENT, one
  EVENT, the Circle frame) byte-identical (one run against that build,
  not a committed test). `make test-status-runtime` links and passes again
  (its fixture stubs the GS frame off: em_frame_gfx, em_gfx_gs_upload,
  em_gfx_gs_world_enabled, em_sgp_field_claim, em_sgp_field_claim_page);
  `make test-status-gs-page`, `test-status-pages-reference`,
  `test-item-ui-reference`, `test-status-field-reference` (EM_TEST_FULL=1)
  and `test-level-smoke` pass; `make all` 0 warnings.

Not compared by pixels: EQUIPMENT with an equipment entry selected or a
scrolled list, EVENT with an event item (its 0020CCB0 marker), and the
transitions into and out of the pages (0020EE50 state 3's module-load
frames, the frames after the Circle frame): section 5.

## 3g. Step 3, fifth part: DATABASE in the GS field (2026-10-21)

The Original profile now draws every frame of the hub's DATABASE page
(00214020, page 3 of the hub) as the original's GS draws it and presents
that field: the category list, a record, the category step, the list's
scroll (state 2) and a blank entry (state 3), the frame Circle leaves on,
and the key take's record and list.

**What the original's list is (measured).** Hidden fork runs from the
state slot14_status_hub (section 4: the stick down, Cross; Cross on the
first entry, id 0, the one record owned there; Cross; right; up with the
cursor at the top; Cross on a blank entry; Circle) show DIRECT-only lists
(no VU1 code). The page opens at loop top 33 (state 0 runs into state 1
in one iteration; loop tops 30..32, the last frames of the page module's
load, page core phase 3 step 1 from loop top 12, build no slot-1
packet); its list is 0020A7A0's background, 002131B0's frame
(blend 0, four sprites, blend 3, two or three), 002134C0's arrows or (the
record) its page numbers, 001CBA50(1, 0x870 / 0x898, 0x7AC, 0x10, 0x10,
the number, D_00275870), and the message service's glyph runs (the
category title, the entries' names, the record's text): 571 calls the
list (536 primitives), 665 the record (548), 547 the next category and
state 3 (531), 549 the scroll: 00213CC0's two 00207D90 packets, the list
window (70, 70)-(490, 165) and then the whole field (0, 0)-(512, 224),
with nothing between them on these frames (the nine help lines are blank
entries, which draw no glyph). Circle (state 1's 0x20 test) returns before
any leaf: the head alone, one primitive, at loop top 182. Every list of
loop tops 30..186 was captured (light: the list's slot and its chain,
`capture_lists.py`, section 4): the visit shows 8 distinct pages after the
background (the list, the record, the frame Cross leaves the record on,
right held, the next category and state 3, up held, the first scroll
frame, the scroll). The CPU GS model draws all fourteen full captures
bit-exact in both modes, all of local memory (`make
test-status-field-reference`), and the builders rebuild their chains byte
for byte (`make test-ui-packet-reference`, whose decoder now reads
00207D90's packet).

**The background's length.** 0020A7A0 draws 513 calls, or 514 while
layer 2's burst runs (its timer negative: em_status_background.h, the
original-instruction oracle `test-status-background-reference`). Every
earlier compared frame had the burst; the side run's last DATABASE visit
has frames without it (port frames 7060..7157), so test_status_gs_page
now finds the background's length frame by frame (background_calls) where
it compares DATABASE pages.

**The binding.** em_status_pages_live binds 00214020 to its sink
(bound_page) and the sink gains 00207D90 (`scissor`; em_status_gs_page's
s_scissor, em_uip_00207D90): on a bound page 00207D90 builds its packet
at its call time, as the other leaves do, before em_page_draw records it
for the GPU renderer (a page not bound records it only, as before).
002134C0's page numbers are 001CBA50 runs with a style record (D_00275870)
and go to the BATTERY page's verified s_text through p_text. 00213A00's
and 00213CC0's help lines (001FCF60) are the message service's glyph runs
(bound since 3b).

**The side run.** The status_pages side run's first DATABASE visit (the
list, right, Cross on a blank entry) is kept, and a second visit with the
original's visit's steps is added at the end of the script (START, the
list, Cross on id 0, Cross, right, up, Cross, Circle twice, START), so
every frame before it keeps its place and the written-background pairs of
3e and 3f stay valid.

Proof:

- `make test-status-gs-page EM_TEST_FULL=1` (section 8 of the test, the
  same status_pages run as sections 6 and 7; `--item` alone runs the
  three, about 2 min): every one of the run's 329 DATABASE frames (its
  three visits: 2894..2981, the key take's 5648..5711, 6982..7158) is
  presented as a GS field, and every one that built packets opens with
  the background and 002131B0's packets. The first hub visit's 3 distinct pages after the
  background are pages of the original's visit; the last visit's 8 are
  exactly the original's 8 (both ways). The empty frames (the Circle
  frames 2981 and 7158, and the key take's first frame 5648, whose state
  0 with the request returns before any leaf) present the displayed field
  of the original's Circle frame f0182. The original was given the
  port's D_002655A0 of port frames 6989 (the list), 7019 (the record),
  7095 (the scroll; no burst) and 7109 (state 3) at loop top T - 1
  (capture.py CAPTURE_WRITE, the only write): port frames N + 1 / N + 2
  equal the captures T / T + 1 (44 / 45, 70 / 71, 128 / 129, 156 / 157)
  in every call (571, 665, 548, 546), the written bytes equal the
  builders' and the capture's, their presented fields hash equal to the
  displayed fields, and the presented pictures, the overlay pass included,
  are 0 of 114,688 pixels off at all eight and at the Circle frame 7158.
- `make test-level-smoke-side` (check_status_pages): the side run's 329
  DATABASE page calls, the new visit's record, scroll and blank entry
  included, replayed through the original instructions, every callee
  entry and view byte equal.
- `make test-status-field-reference EM_TEST_FULL=1`: 30 lists bit-exact
  (14 of them DATABASE's); `make test-ui-packet-reference EM_TEST_FULL=1`:
  30 captured chains rebuilt byte for byte and drawn to the displayed
  field.
- Mutations of the live binding (test_status_gs_page --item):
  5 of 5 killed (DATABASE not bound, 00207D90 not sunk, its corners
  transposed, the text runs not sunk, the text run's style record
  dropped; the last two kill the record frames, whose page numbers are
  001CBA50 runs).
- **Unchanged elsewhere.** Against a build of the same tree with only
  DATABASE unbound (the side run's new script included): the main route
  through the AREA01 arrival, its 13,813-tick log identical and all
  15,070 committed frames of the same kind and field hash; the
  status_pages side run, its 5,922-tick log identical and of its
  7,180 committed frames exactly the 329 DATABASE frames differ (GPU
  frames before, GS fields now). The GPU renderer (EM_GPU_RENDERER=1): the
  side run's tick log and present log identical and the presented frames
  at seven DATABASE ticks (the first visit's list; the last visit's list,
  record, scroll, state 3 and Circle frame; the key take's record)
  byte-identical (one run against that build, not a
  committed test). `make test-status-runtime`, `test-status-gs-page`,
  `test-status-pages-reference`, `test-item-ui-reference`,
  `test-level-smoke` and `test-level-smoke-side` pass; `make all` 0
  warnings.

Not compared by pixels: the key 0x32 take's record and list (no capture:
the original's take of the key is not captured; checked by structure),
the take's first frame (it builds no slot-1 packet in the port; the
original's is not captured),
DATABASE's entry with more than one record page (its arrows and the
page turn), and the transitions into and out of the page (loop tops
30..32 build no slot-1 packet in the original; the port's frames there
are not bound pages): section 5.

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
- The ITEM root (section 3e): `capture.py state:slot12_status_root 30`
  (the state's own stick 0x7F), `CAPTURE_PAD=0x80,0x80 capture.py
  state:slot12_status_root 40` (the port's neutral stick held from frame 0;
  folder `slot12_status_root_pad8080`) and, with `CAPTURE_WRITE=
  0x2655A0:<hex> CAPTURE_TAG=bg3467` (the bytes written to EE memory at
  loop top T - 1 of the first T, before its checkpoint: a designed probe,
  the only write; the port's D_002655A0 of its frame 3467), frames 40 and
  41 (`slot12_status_root_pad8080_bg3467`). The root's list is DIRECT
  only: no VU1 code.
- EQUIPMENT and EVENT (section 3f): `CAPTURE_INPUTS='<json pad
  schedule>' CAPTURE_INPUTS_TAG=<name> capture.py state:slot12_status_root
  <T>` (this step's option: the schedule is set before frame f's step).
  EQUIPMENT: stick (0x80, 0xFF) from frame 0, Cross with it at frames 8-9,
  neutral 0x80 from 10 (folder `slot12_status_root_equipment`, f0060);
  EVENT: stick (0x00, 0x66) (`slot12_status_root_event`, f0062); with
  CAPTURE_WRITE of the port's D_002655A0 of frame 3421 / 3511 at loop top
  59 / 61, frames 60, 61 / 62, 63 (`..._equipment_bg3421`,
  `..._event_bg3511`); and EQUIPMENT with Circle (0x2000) at frames 70-71
  (`slot12_status_root_equipment_circle`, f0072: the Circle frame).
  `build/status_field_capture/pages/explore.py` (the page core's bytes
  each frame of a schedule) found the frames.
- DATABASE (section 3g): from `state:slot14_status_hub`, the schedule
  `build/status_field_capture/database/schedule_rest.json` (stick (0x80,
  0xFF) from frame 0, Cross 0x4000 at 8-9, neutral from 10; Cross at 50,
  80; right 0x20 at 100; up 0x10 at 120; Cross at 150; Circle 0x2000 at
  180; each two frames) with `CAPTURE_INPUTS_TAG=database`: frames 45 (the
  list), 70 (the record), 115 (the next category), 128 (the scroll), 156
  (state 3), 182 (the Circle frame) in `slot14_status_hub_database`; with
  CAPTURE_WRITE of the port's D_002655A0 of frames 6989 / 7019 / 7095 /
  7109, frames 44, 45 / 70, 71 / 128, 129 / 156, 157
  (`..._database_bg6989` and so on). `database/capture_lists.py <state>
  <schedule> <a> <b> <out>` (light: per loop top a checkpoint, the list's
  16 KB slot and its slot-1 chain at step V of the next iteration,
  `lists/fTTTT.bin`; about 17 seconds a frame, 17 MB for 157 frames): every
  list of loop tops 30..186 in `slot14_status_hub_database/lists`.
  `database/explore.py` (the page core's bytes each frame of a schedule)
  found the frames.
- The status hub (`state:slot14_status_hub`) draws 001B0000's models
  through the VU1: its GS stream is the fork's GS dump, which `capture.py`
  records in its third pass (not yet run to completion: the hub run's
  hidden launch timed out once).

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
   uploads, the restore and 001CCB10 in the model's memory; (done,
   section 3e) the ITEM root (0020F170 / 0020F2A0 and 0020AC70's fans,
   001D66A0) drawn and presented as the GS field; (done, section 3f)
   em_status_pages_live's sink and EQUIPMENT / EVENT drawn and presented
   as the GS field, their Circle frame included; (done, section 3g)
   DATABASE (00214020) with the sink's 00207D90. Still to bind: HEALING
   (00208AD0's health gauge: its 002082B0 arcs need a translated builder
   first), SPR4 (00211970: its 0020AC70 fans go
   through em_uip_001D66A0, its 00207D90 SCISSOR_1 through the sink's
   scissor, bound in 3g), MAP (00208040's flat triangle and 001B0000's models:
   VU1, item 4), the hub's 2D layer (em_status_hub_ui; with its models,
   item 4) and DRAW_OTHER, each with its 0020A7A0 at call time and each
   list checked for VU1 code with a capture first; the page transitions
   (0020EE50 state 3's module-load frames, the frames after a Circle);
   then the video comparison's segments 8 and 14 are compared again. The ITEM root's lit frames by pixels: a designed capture
   that also writes the port's trail ring (D_00821300, D_00275C90) and
   hover byte. DATABASE's key take (the record 0x32 and its list) and an
   entry with two record pages by pixels: a capture of the original's take
   or a designed write of the entry's ownership byte.
4. The hub and the pages with models (001B0000's units through the object
   kernel's GS output, as the world frames draw them), with GS-dump
   captures; segment 5.
