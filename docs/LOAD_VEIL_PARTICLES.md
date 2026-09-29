# Area-load veil particles (the 001ADF50 load veil's draw)

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

While 001ADF50 loads an area (New Game into AREA11, any later area change),
its veil state machine 0021B550 (translated in `src/game/em_load_veil.c`,
docs: SCENE_COORDINATOR_DESIGN.md S12a) calls two routines on every tick of
state 1 and of state 2 sub-state 0:

- **0021B1B0**, the veil draw: it builds the display-list packets of one
  frame of the veil;
- **0021B500**, the phase step.

Both are translated, with every packet builder 0021B1B0 reaches, in
`src/game/em_load_veil_particles.c/.h` (sections 1 and 2), and **both are
live** since chain C8 (LOADVEIL, 2026-09-27): em_scene_bindings binds them at
their original caller 0021B550, the packets land at the render context's
channel-0 cursor, and main-loop step V's own list, which sends them to the
GS, is drawn whole by the GS frame stage (section 3). The GS state the
packets REF comes from the boot builder's GS blocks, now translated
(em_gs_blocks_original, RENDER_CONTEXT.md 8.3).

**What a player sees today** (since chain step H7, 2026-09-29). The area
load 001FF080(1, 0) runs the loader task 001FF0D0's own steps (the area
streamer 001FFCD0 with its sound-bank upload 001FB370; MODULE_LOADER.md
1.9), so D_00275BD8 stays 1 for as many ticks as those steps take and
0021B550 ramps the veil through its state 1, then decays it in state 2.
At host speed (the Original profile) the New Game load draws **55 veil
frames**: the ramp over the load's 20 dispatches reaches levels 0.2 /
0.16 / 0.12, then the decay. With the PS2 disc-drive timing switch the
loader takes the recorded New Game reads and the veil runs **257 frames**
(the captures' phase says 258: section 5).

## 1. What the original does

The veil block is *D_00275888 (0x8214C0 in every capture). The routines
read or write only these bytes of it:

| offset | field | who |
|---|---|---|
| +0x04 | phase (float) | 0021B1B0 reads it; 0021B500 advances it; 0021B180 clears it |
| +0x08 | level 0 (float) | 0021B550 ramps it 0 to 1 (+0.01 per tick) and decays it; 0021B1B0 reads it as the line brightness |
| +0x14 | seed (word) | 0021B1B0 only: set to 0x07234567, then stepped |
| +0x18 | base Y (word) | 0021B180 sets 0x8000; 0021B1B0 reads it |

Packets go to display-list channels. The render context is *D_00275670
(0x811CC0 in the captures). Channel `c`'s write cursor is the word at
context + 0x10 + 4c. Each builder writes at the cursor and advances it.
Context + 0x9C is the current frame-buffer index (0 or 1).

### 0021B500: the phase step (byte-matched C)

phase = phase + 0.007. If the result is not less than 1.0, phase = phase − 1.0.

### 0021B1B0: the veil draw (asm-word; translated from its instructions)

1. Seed = 0x07234567. The previous vertex starts as colour (0, 0, 0, 0x80)
   and position (0, base Y, 0, 0).
2. 001D1F80(0, 0, 7): a REF tag to a static GS block (section 5).
3. **512 line segments**, i = 0..511. Each segment works as follows:
   - **Noise.** Let r = i mod 5 and next = seed × 5 + 1. Then
     n = ((5 − r)·(seed >> 16) + r·(next >> 16)) / 5, an unsigned 32-bit
     division. It blends two 16-bit outputs of the generator. The noise is
     n / 65536 − 0.5, with n converted as a signed word. The original's
     unsigned-conversion path for a negative n can never be taken, because
     n < 2^30.
   - **Brightness.** x = i / 512 and d = x − phase. If d < 0, it uses
     (d + 1)² instead of d². That value is squared again. Below 0.1 it is
     replaced by 0. Brightness c = float_to_int(255 × (d⁴ × level 0)). The
     line is brightest just behind the phase point and fades further back.
   - **Envelope.** e = ((0.5 − fabsf(0.5 − x)) / 0.5)^16. On every 64th
     segment (i & 0x3F == 0), e = e + 0.2.
   - **The new vertex.** Position X = (i + 0x700) << 4 (12.4 fixed point,
     GS X 0x700 + i). Y = float_to_int(float(base Y) + noise × 1280 × e).
     Z = 0xFFFFFF, F = 0. Colour = ((2c) >> 3, (3c) >> 3, c, alpha c), with
     arithmetic shifts.
   - 001D63B0(0, previous position, previous colour, new position, new
     colour) writes one Gouraud line.
   - When r = 4, seed = next. The new vertex becomes the previous one
     (00102948 copies each quadword).
4. The lens passes run after the lines:
   - 001D1F80(0, 0, 7), then 001DFA40(0, 0, 0x80808080, −0.45);
   - 001D1F80(0, 0, 2), then 001DFA40(0, 0x40, 0x40404040, −0.45).

   The original also loads 5.5 and 2.75 into f13 before the two calls.
   001DFA40 never reads f13.

Channel 0 grows by exactly 0x162B0 bytes per call:

- three REF tags (0x30 bytes);
- 512 line packets (0xE000 bytes);
- two lens passes of 0x4140 bytes each.

The worker calls per call are:

- 512 × (float_to_int, fabsf, float_to_int) for the lines;
- per lens pass, 16 + 256 fabsf and 15 × 16 × 2 float_to_int.

### The packet builders

Every builder first writes the DMA tag at the cursor:

- the halfword quadword count at +0;
- the id byte at +3 (0x10 CNT, or 0x30 REF);
- the word at +4 (0, or the REF address).

Byte +2 and bytes +8..+0xF of the tag are never written. They keep whatever
the channel memory held.

- **001D1F80(c, a, b)**: a REF to *D_00275674 + 0xBA0 + 1440a + 144b, 9
  quadwords.
- **001D1FF0(c, a)**: a REF to *D_00275674 + 0x4A0 + 64a, 4 quadwords.
- **001D2040(c, a)**: a REF to *D_00275674 + 0x5A0 + 64a, 4 quadwords.
- **001D1F20(c)**: a REF to *D_00275674 + 0x20 + 400 × (context + 0x9C),
  0x19 quadwords. This is the draw environment of the current frame buffer.
- **001D63B0(c, p0, c0, p1, c1)**, 7 quadwords. It returns cursor + 0x10.
  - A CNT tag, then a DIRECT word 0x50000005 at +0x1C.
  - A PACKED GIF tag: one loop, EOP, PRIM 0x49 (line, Gouraud, alpha
    blend), registers RGBAQ, XYZF2, RGBAQ, XYZF2.
  - Then c0 (4 words), p0 (3 words and a 0), c1 (4 words) and p1 (3 words
    and a 0).
- **001D7080(c, rgba, q)**, 4 quadwords. It is an A+D RGBAQ write: the data
  word `rgba`, then Q = `q`.
- **001D6BA0(c, a1, a2, a3, t0, t1)**, 5 quadwords. It returns cursor + 0x10.
  It is an A+D block of TEXFLUSH, then TEX0_1 with these fields:
  - TBP0 = a1 >> 8;
  - TBW = (1 << a2) >> 6;
  - TW = a2 and TH = a3;
  - TCC = t1 and TFX = t0.

  Each field is the sign-extended register shifted as a doubleword.
- **001D6E60(c, a1, a2, a3)**: 11 quadwords. It returns cursor + 0x10.
  - A CNT tag with the words 0, 0, 0x11000000 and DIRECT 0x50000009.
  - A PACKED A+D GIF tag of 8 registers.
  - Then 001006D8(+0x30, psm 0, 1 << a2, 1 << a3, 0, 2).
  - FRAME's FBP is then replaced by (a1 >> 13) & 0x1FF.
- **001006D8(env, psm, w, h, zte, zpsm)**, the SDK draw-environment fill.
  Every argument is a halfword. It writes eight register pairs:
  - FRAME_1: FBW = (w + 63) / 64 and PSM = psm.
  - ZBUF_1: ZBP = 00100610(psm, w, h) and ZPSM = zpsm. ZMSK is set when
    zte = 0.
  - XYOFFSET_1 = (0x800 − w/2, 0x800 − h/2) in 12.4.
  - SCISSOR_1 = (0, w − 1, 0, h − 1).
  - PRMODECONT and COLCLAMP: each is read back and only bit 0 is set.
  - DTHE: read back; bit 0 = psm & 2.
  - TEST_1 = (zte & 3) << 17 | 0x10000 when zte ≠ 0, else 0x30000.

  00100610 computes ceil(w / 64) × ceil(h / 64) when psm & 2 is set, else
  ceil(w / 64) × ceil(h / 32) (signed halfword arithmetic). It returns that
  product, doubled unless the SDK mode dword (D_00241010 & 0xFFFF0000FFFF)
  is 1, as a sign-extended halfword.
- **001D6930(c, a1, a2, a3, src)**, the frame copy. It returns the entry
  cursor.
  - First 001D6E60(c, a1, a2, a3), 001D2040(c, 0) and 001D1FF0(c, 2).
  - Then 12 quadwords: an A+D block with these registers:
    - TEXFLUSH;
    - TEX0_1 = 0xA_24020000, with TBP0 = 0x700 added when context + 0x9C
      is 0: TBW 8, PSMCT32, TW 9, TH 8, TCC 0, TFX 1 (DECAL);
    - TEXA 0x20.
  - Then a PACKED sprite: PRIM 0x116 (sprite, textured, UV).
    - The colour is the 16 bytes at `src`.
    - UV runs (8, 8) to (0x1FF8, 0xDF8).
    - XY is the 1 << a2 by 1 << a3 rectangle centred on GS 0x800.
  - It downsamples the displayed 512 × 224 frame into a texture.
  - The 8 bytes after each of its two UV words (+0x88 and +0xA8) are not
    written.
- **001D6B60**: 001D6930, then 001D1F20. It returns 001D6930's value.
- **001DFA40(c, a1, rgba, k)**, the lens pass. It returns the entry cursor.
  1. 001D6B60(c, *D_0027568C, 8, 8, &D_0026E880) copies the frame into the
     256 × 256 texture at GS byte address *D_0027568C (0x258000).
  2. 001D6BA0(c, *D_0027568C, 8, 8, 0, 0) selects it as TEX0.
  3. 001D1FF0(c, 3), 001D2040(c, 0) and 001D7080(c, rgba, 1.0) follow.
  4. **The table.** It fills a 16 × 16 table in its stack frame at
     sp + 0xD0. For row i and column j, let u = j/15, v = i/15, U = |2u − 1|
     and V = |2v − 1| (fabsf). Then m = 0.5 / (1 + (U² + V²) × k). The entry
     is (0.5 + (u − 0.5)·m, 0.5 + (v − 0.5)·m, 1.0). Lane 3 of the entry
     is never written.
  5. **15 strips**, i = 0..14, each 0x43 quadwords:
     - A CNT tag and DIRECT 0x50000041.
     - A PACKED GIF tag: 16 loops, EOP, PRIM a1 | 0x14 (triangle strip,
       textured; a1 = 0x40 adds alpha blend), registers ST, XYZF2, ST,
       XYZF2.
     - For each column j, rows i and i + 1:
       - ST is the whole 16-byte table entry, lane 3 included;
       - X starts at 0x7000 and steps by float_to_int(float(X) + 8192/15);
       - Y = ((row × 0xE0) / 15 + 0x790) << 4;
       - Z = 0xFFFFFF.
  6. 001D1F20(c) and 001D1FF0(c, 1) end the pass.

  The screen is covered by a 15 × 15 grid of textured quads. Its texture
  coordinates bend outward from the centre: m = 0.5 in the middle and grows
  toward the corners when k < 0. The first pass is opaque with colour 0x80;
  the second pass alpha-blends colour 0x40.

## 2. The translation (`src/game/em_load_veil_particles.c/.h`)

- **Public functions.** There is one per original:
  `em_load_veil_particles_0021B1B0`, `_0021B500`, `_001D1F80`, `_001D1FF0`,
  `_001D2040`, `_001D1F20`, `_001D63B0`, `_001D7080`, `_001D6BA0`,
  `_00100610`, `_001006D8`, `_001D6E60`, `_001D6930`, `_001D6B60` and
  `_001DFA40`. Register arguments are the original's (a0 is the channel).
  Return values come back through `*result`, as original addresses.
- **Packet memory.** `world.packet` is a byte window that stands for the
  original addresses [packet_address, packet_address + packet_size). Cursors
  are original addresses. The bytes the original leaves unwritten keep their
  value. The three dwords 001006D8 reads back are modified in place.
- **Views, not copies.** The module reads these through views:
  - the veil block (`EmLoadVeilParticlesBlock`: +0x04, +0x08, +0x14,
    +0x18), reloaded wherever the original reloads it;
  - the cursors;
  - context + 0x9C;
  - D_00275674, D_0027568C, D_0026E880 (16 bytes) and D_00241010 (8 bytes).
- **The table.** 001DFA40's table is `world.table`, 0x1000 bytes. Lanes 0..2
  are written on every call. Lane 3 is left as the buffer holds it, because
  the original copies whole entries into the strips (section 5).
- **Workers.** Only two: `w_0011DF78` (fabsf) and `w_001281C0`
  (float_to_int). Everything else these routines call is translated here,
  and 00102948 is a 16-byte copy.
- **Arithmetic.** Every COP1 operation goes through `em_ee_float.h` on raw
  binary32 bits. The module performs no host float operation. There is no
  VU0 code.
- **Fail-stop.** The fault is latched in `s->fault` as
  {address, EM_LVP_FAULT_*}. Once it is set, every later call returns −1.
  - 0021B1B0 checks everything before it writes anything:
    - every view it reaches;
    - both workers;
    - the channel index;
    - the cursor's qword alignment;
    - that the whole 0x162B0-byte run fits the window.
  - Each builder checks its own run the same way.
  - A worker returning a negative value stops the run where it failed. The
    original has no failure path, so this state exists only in the port.

## 3. Binding (live)

### 3.1 The two routines

- **0021B500** (`veil_0021B500`, em_scene_bindings.c): the translation over
  the veil block's +0x04 (`s_veil.w04`). Only the phase is read or written.
- **0021B1B0** (`veil_0021B1B0`): `em_rcl_0021B1B0` runs the translation on
  the render context's one EmLoadVeilParticles (the instance whose REF tags
  and frame-copy packets the frame heads and 001DDE10 already use), with
  the views `s_veil.w04`, `level[0]`, `w14` (the seed, a new field of
  EmLoadVeil at the original +0x14) and `w18`. Its world:
  - the channel cursors are the context's +0x10.. words, the packet window
    is the arena D_0028F700.., context +0x9C is the buffer index;
  - D_00275674 and D_0027568C are the exported D_00275670 block's words;
  - D_0026E880 (the frame-copy sprite colour, four words of 0x80) is a new
    block of `assets/render_context.emrc` (tools/export_render_context.py;
    STARTUP.md row 49: re-export once);
  - 001DFA40's table is a persistent 0x1000-byte buffer (lane 3, section 5);
  - the workers are the context's own: 0011DF78 (em_sdk_math_original) and
    001281C0 (em_player_float_to_int).

  A fault latches in the render context (em_rcl_fault) and faults the scene.
  The run the call wrote (its channel-0 start and end) and the buffer index
  are kept for step V (`em_rcl_veil_span`); the frame head 001D1AE0 forgets
  them.

The reported UM_0021B1B0 / UM_0021B500 are removed.

### 3.2 The GS state the veil's REFs send (em_gs_blocks_original)

The builders' REFs point into the boot builder sub_EXTERMINATION's GS
blocks at D_00275674 (0x814220). Since this step all of them are translated
(001D0F20's bank loops, 00101898, 00101630, 001008C0; 001006D8 and 00100610
are this module's, one owner) and built at `em_rcl_init`:

| REF | Block | What the veil gets |
|---|---|---|
| 001D1F80(0, 0, 7) | bank G, pass 0, preset 7 (+0xBA0 + 0x90 * 7) | PRIM 0x100, TEX1 0x60, TEST 0x30003 (alpha ALWAYS, Z ALWAYS), ZBUF with ZMSK, ALPHA 0x80000000A8 (Cs * 0x80 >> 7), CLAMP REPEAT, COLCLAMP |
| 001D1F80(0, 0, 2) | bank G, pass 0, preset 2 | TEST 0x33001, ALPHA 0x8000000068 (Cs * 0x80 >> 7 + Cd: additive) |
| 001D1F20(c) | bank A of context +0x9C (+0x20 + 0x190 * slot) | the frame buffer's draw environment: FRAME_1 FBP 0x38 (slot 0) or 0 (slot 1), FBW 8, PSMCT32; XYOFFSET (with step V's half line); SCISSOR 512 x 224; TEST 0x50000; the tail CLAMP / COLCLAMP / FBA / PABE / SCANMSK / TEX1 / TEXA |
| 001D2040(c, 0) | bank E, 0 (+0x5A0) | TEST 0x3000D (alpha GREATER than 0, Z ALWAYS), ZBUF with ZMSK |
| 001D1FF0(c, 1 / 2 / 3) | bank D (+0x4A0 + 0x40 * a) | PRIM 0, then CLAMP_1 REPEAT / REGION_CLAMP 0..511 x 0..223 / REGION_CLAMP 0..255 x 0..255 |
| step V's clear under flag 3 | bank C, 1 (+0x420) | TEST 0x30000, a sprite over 512 x 224 of RGBA (0, 0, 0, 0x80) |

So in a veil frame the GS sees: the slot's draw environment, the black
clear, the 512 lines written as their own colour (the blend Cs * 0x80 >> 7
leaves it as it is; the alpha written is the line's), then per lens pass the frame copied (TBP0 0x700 or
0, the buffer being drawn, TBW 8, bilinear, REGION_CLAMP to the 512 x 224
frame, DECAL, TCC 0) into the 256 x 256 surface at 0x258000 and drawn back
over the whole frame as 15 x 15 textured quads (MODULATE, REGION_CLAMP
0..255): opaque with colour 0x80, then added with colour 0x40. Every
primitive has the Z test ALWAYS and ZMSK, so no pixel depends on Z. The
lens reads the frame buffer the same frame drew: each veil frame is the
lines, bent outward from the centre and glowing, over black.

### 3.3 Drawing it (em_load_veil_live, the GS frame stage)

- **Where.** Main-loop step V (`rcl_step_v`, main.c): 001D2300 builds and
  kicks the frame's list; then `em_load_veil_live_draw` runs. In a frame
  whose 0021B1B0 ran (`em_rcl_veil_span`), it walks the kicked list
  (`em_rcl_kick`) exactly as the DMA sends it: em_chain_page's list mode
  (CHAIN_PAGE.md section 11) over the render context's storage, from the
  list's first tag to the END tag at the GS block + 0x10. The walk hands
  every primitive with the context-1 environment in force (FRAME_1, ZBUF_1,
  XYOFFSET_1, SCISSOR_1, PRMODECONT, DTHE, FBA_1, PABE, TEXA, SCANMSK).
  Frames without the veil are untouched.
- **Checks before drawing.** Every qword of the veil's channel-0 run was
  transferred by the walk (the list really sends the run), and the first
  primitive draws into the slot's frame buffer (bank A's FRAME_1).
- **The GS frame stage** (`em_gfx_gs_frame`, em_gfx.h "The GS frame",
  Metal): GS memory as surfaces keyed by (FBP, FBW), PSMCT32; the frame
  buffers 512 x 224 and the capture surface 256 x 256; a TEX0 reads the
  surface at the same byte address and buffer width. Each primitive is drawn
  at the GS's own resolution with the GS pixel path (TFX / TCC, the eight
  alpha tests and four AFAIL modes, the blend with COLCLAMP, FBA, FBMSK,
  REGION_CLAMP and the other wrap modes, bilinear with the GS weights) by
  framebuffer fetch; then the slot's frame buffer is shown over the whole
  4:3 game rectangle, nearest-neighbour. Anything outside what it
  implements is refused (points, fog, a Z test other than ALWAYS, DATE,
  dithering, PABE, a non-CT32 frame or texture, a texture read of the
  surface being drawn, CTXT, AA1, mipmaps, a texture whose reachable texels
  lie outside its surface).
- **Fail-stop.** A walk fault, a refused primitive, or a run the list did
  not send latches `em_load_veil_live_fault`, and step V stops the loop.
- **The tick log** (EM_AREA_CHANGE_LOG) gains `veil_draw` on the line after
  a veil frame: its frame counter, the run, slot, list, displayed FRAME_1,
  the walk's counts, the digest, the brightest line colour byte and the
  run's bytes (LEVEL_SMOKE.md "The load veil").

## 4. Verification

- **`make test-load-veil-particles-reference`**
  (tools/test_load_veil_particles_reference.py) builds the module as a
  shared library.
  - **The original side.** The oracle (`VeilEE`: the fall lane's `FallEE`,
    imported and not edited; COP1 through `tools/ee_float_model.py`)
    executes every routine in section 1 unmodified over a copy of the
    captured RAM `build/startup-reference/opening_ee.bin`. 0011DF78 and
    001281C0 also run their original code, in place at the caller's stack
    pointer; each call is logged with its argument and result. The native
    workers are bound to `em_sdk_math_original_0011DF78` and
    `em_player_float_to_int`.
  - **Callee set.** The jal targets of the translated routines are exactly
    these routines plus the two workers.
  - **What is compared.** The native side runs over another copy of the
    same RAM with a window over all 32 MB. Per case: all 32 MB byte for
    byte, the return value, the worker call log, and 001DFA40's table (all
    16 bytes of each entry, the native buffer seeded with the stack words
    the original frame held at 001DFA40's entry).
  - **Cases.** 0021B1B0 over the captured block with context + 0x9C = 0
    and 1 and varied phase, level, base Y, seed and cursor; 001DFA40 with
    0021B1B0's two argument sets and random ones; 0021B500 on edge and
    random phases; every builder with random arguments (halfword edges,
    shift counts past 31, negative values, SDK mode dwords 1 and others).
    Both outcomes of every conditional branch (16 sites; the one
    unreachable outcome is listed and asserted never taken).
  - **Capture evidence.** The veil block (state 3, finished) is identical in
    18 captures. Its seed 0xA6D22D01 is exactly what one executed 0021B1B0
    leaves, and its phase 0x3F4E55C8 is reached from 0 by 258 executed
    0021B500 steps (the native module agrees at every step).
  - Default 408 cases (about 2 s); `EM_TEST_FULL=1` 3453 (about 8 s).
    Mutations (a TEST_1 register number, the 64th-segment test, the table's
    Q lane, the seed step, a 12-byte ST copy) each fail it.
- **`make test-load-veil-particles`** (tests/load_veil_particles_test.c,
  ASan / UBSan) pins the fail-stop contract.
- **`make test-gs-blocks-reference`** (tools/test_gs_blocks_reference.py,
  about 7 s): the ORIGINAL 001D0F20 executed with 00101898 and every callee
  it reaches running writes the native 0x2220 bytes (run twice over two
  stack fills: exactly the 12 read-back dwords' upper bits follow the
  stack); the native blocks equal all 18 captures outside the words the
  frame rewrites (XYOFFSET in its boot or step V form, FOGCOL) and those
  stale bits; 001008C0 and 00101630 executed against the native functions.
- **`make test-load-veil-gpu`** (tools/test_load_veil_gpu.py, about 6 s):
  the veil of the translation over the translated GS blocks, walked in list
  mode and drawn by `em_gfx_gs_frame` in a headless Metal window, against a
  model of the GS pixel path, for a visible veil (level 1.0) in both slots,
  cut after the lines, the first copy, the first lens pass and the whole
  run: every lit line pixel lies on its segment with a colour between its
  ends; both copies equal the model on every one of the 65,536 pixels; the
  lens passes equal the model on every pixel whose sample is unambiguous
  (439,792 compared; the 9,480 within 1e-4 of a triangle edge or 1e-3 of a
  sub-texel step are Metal's float interpolation standing for the GS DDA).
  A level-0 veil draws an all-black frame; a GEQUAL Z test, a texture read
  of its own surface and a PSMCT16 frame are refused.
- **`make test-chain-page`** covers list mode (CHAIN_PAGE.md section 11).
- **`make test-area-load-reference`**: the chain replay now executes the
  ORIGINAL 0021B500 inline and, at each 0021B1B0, the ORIGINAL veil draw
  over the opening capture on the block as it stands; the logged block
  (all 0x1C bytes, the seed included) must equal it after every tick.
- **The level smoke** (`check_load_veil`, tools/level_smoke_load_veil.py;
  LEVEL_SMOKE.md "The load veil"): every veil frame of the run (the New Game
  load: 55 at host speed, 257 with the switch) had its run re-made by the ORIGINAL 0021B1B0 at that call
  (the tick's block, slot and cursor) byte for byte, except the strips'
  ST lane 3; its seed and the original 0021B500's phase equal the port's;
  the list drew the clear, 512 lines, two copies and 900 triangles into the
  slot's frame buffer; a level-0 veil lit no line; the seed the load leaves
  equals every route capture's. It prints the phase the load left against
  the captures' (0.385 after the host-speed load's 55 frames, 0.799 after
  the switch's 257; 0.806 after the PS2's 258).
- **`make test-area-load-reference`** replays every veil step of the
  logged loads (two in its run: New Game and the area-change reload) through
  the executed 0021B550 / 0021B180 / 0021B840; each 0021B1B0 on the way is
  the ORIGINAL veil draw (the first 3 of a run in quick mode, all with
  `EM_TEST_FULL=1`: it writes only the seed, which it restarts at every
  call).

## 5. What no capture shows, and the limits

- **No capture of a veil frame.** Every capture that holds the veil block
  was taken after the load (state 3, finished): the opening, the playable
  and handoff images and route beats 00..14 hold the New Game load's end
  state, route 15 the AREA01 load's (phase 0.995, about 142 steps). No GS
  frame of a load was ever dumped. So the veil's pixels are proven against
  the GS model of the GPU test, not against an original frame; the packets
  are proven against the executed original and the end state against the
  captures. A PCSX2 software-renderer frame (the decomp's C7 fb2 method)
  taken mid-load would allow a pixel comparison; that is a new capture for
  the lead.
- **How long the veil runs.** The load spans the loader task's steps
  (chain step H7): at host speed the New Game load's 20 dispatches give 55
  veil frames (the phase the load leaves: 0.385); on the PS2 the New Game
  load drew 258 (every route capture's phase 0.806: 258 executed 0021B500
  steps) and the AREA01 load about 142. With the PS2 disc-drive timing
  switch the New Game's reads take their recorded fields
  (MODULE_LOADER.md 1.7) and the veil runs 257 frames (phase 0.799): the
  one frame short is the sound-bank upload's ninth call on the PS2, most
  likely its SIF DMA's hardware time, which no mode reproduces
  (IOP_STREAM.md "The sound-bank transfer"). The level smoke checks every
  one of those frames against the executed original (section 4).
- **Rasterization.** Metal rasterizes at the GS's resolution with the GS
  sample points, but its triangle and line rules and its float
  interpolation are not the GS DDA. A line pixel Metal places past an end
  takes no colour beyond the ends'. A sprite's texel coordinates are
  evaluated exactly at the sample point from its corners.
- **Z is not stored.** Every veil primitive tests Z ALWAYS; the stage
  refuses any other Z test.
- **Lane 3 of the table.** The original ships stale stack words in the
  unused fourth word of every strip ST quadword; the GS ignores that word
  of a PACKED ST. The reference test reproduces it by seeding the buffer
  from the oracle's stack; live, the buffer's lane 3 is whatever it holds
  (the smoke's comparison skips exactly those words).
- **Stale stack bits in bank A.** The draw environments' read-back dwords
  (PRMODECONT, COLCLAMP, DTHE) keep the boot stack's upper bits in the
  original; the port's are zero. The GS reads only bit 0 of each.
- **Presentation.** The shown frame buffer is 512 x 224, spread over the
  game rectangle's height as every GS-mapped draw of the port; how the
  Original profile presents fields is the open user decision
  (LAUNCHER_OPTIONS.md). The field's half-line offset is in the drawn
  surface (step V's XYOFFSET).
- **Unreachable outcome.** 0021B1B0's unsigned-to-float conversion branch
  for a negative noise word is never taken (the word is below 2^30).
- **Channel count.** `cursor_count` is a native bound of four; the veil
  uses channel 0 only.
