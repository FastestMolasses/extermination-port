# Area-load veil particles (the 001ADF50 load veil's draw)

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "load-veil-particles". While 001ADF50 loads an area (New Game into
AREA11), its veil state machine 0021B550 (translated in
`src/game/em_load_veil.c`, docs: SCENE_COORDINATOR_DESIGN.md S12a) calls two
routines on every tick of state 1 and of state 2 sub-state 0:

- **0021B1B0**, the veil draw: it builds the display-list packets of one
  frame of the veil;
- **0021B500**, the phase step.

The port binds both as reported no-port-code calls
(`UM_0021B1B0` / `UM_0021B500` in `em_scene_bindings.c`), so the veil shows
black and the block's +0x04 phase never moves. This lane translates both and
every packet builder 0021B1B0 reaches, in `src/game/em_load_veil_particles.c/.h`.
The module is **built and tested but not wired**. Section 3 lists what the
coordinator binds. The GS side (drawing the packets) is a boundary; section 5
says what it needs.

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

## 3. Binding (coordinator)

Nothing is wired. The seam is `em_scene_bindings.c`:

- `veil_0021B1B0` and `veil_0021B500` are the `EmLoadVeilWorkers.w_0021B1B0`
  and `.w_0021B500` of `k_veil_workers`. They report `UM_0021B1B0` and
  `UM_0021B500` today.
- The block is `s_veil` (`EmLoadVeil`, `em_load_veil.h`).

### 0021B500 (can bind now; pure state)

```c
static int veil_0021B500(void *ctx, EmLoadVeil *veil)
{
    EmLoadVeilParticlesBlock b = {&veil->w04, NULL, NULL, NULL};
    (void)ctx;
    return em_load_veil_particles_0021B500(&b);
}
```

Only `phase` is used. After the New Game load, the captured block's phase
is exactly 258 executed steps from 0. The port's count will equal that only
when its load lasts as many veil ticks as the original's (see section 5).

### 0021B1B0

**1. Storage.** `EmLoadVeil` has no +0x14. The owner of `em_load_veil.h`
must add

```c
    uint32_t w14;    /* +0x14: 0021B1B0's noise seed */
```

between `level[3]` and `w18`. The port's struct is not laid out at the
original offsets, so this adds a field and moves nothing that is read by
offset. `tools/test_area_load_reference.py` then can drop its
`VEIL_MODELLED` exclusion of +0x14..+0x17. The `em_scene_bindings.c`
veil log (`log_veil`) writes a zero-filled gap there and would write the
field.

**2. Adapter.** With one `EmLoadVeilParticles lvp` owned by the coordinator:

```c
static int veil_0021B1B0(void *ctx, EmLoadVeil *veil)
{
    EmLoadVeilParticlesBlock b = {&veil->w04, &veil->level[0], &veil->w14, &veil->w18};
    (void)ctx;
    return em_load_veil_particles_0021B1B0(&lvp, &b);
}
```

A negative return must go to the scene fault latch, with `lvp.fault`
naming the address.

**3. `lvp.world`.**

- `cursor` / `cursor_count` are the channel cursor words of the render
  context (context + 0x10, one word per channel). Only channel 0 is written.
  If the port keeps host-pointer channels (like
  `EmOwnerServicesChannel`), give the module
  `packet = channel base`, `packet_address = the original address that
  base stands for`, `packet_size = its capacity`, and a cursor word
  `packet_address + (cursor − base)`. Convert back after the call.
- `ctx_9C` is the frame-buffer index word.
- `d00275674` is the static GS block base. Only the REF addresses depend on
  it. The blocks' contents matter to the GS side only.
- `d0027568C` is the capture texture's GS byte address: 0x258000 in every
  capture.
- `d0026E880` is 16 bytes from the ELF's data: the sprite colour, four
  words of 0x80.
- `d00241010` is the first 8 bytes of the SDK GS parameter block.
- `table` is a persistent 0x1000-byte buffer (section 5 on lane 3).

**4. Workers.** Bind existing translations:

- `w_0011DF78`: an adapter over `em_sdk_math_original_0011DF78`
  (`em_sdk_math_original.c`, already linked) that passes raw bits through
  memcpy;
- `w_001281C0`: an adapter over `em_player_float_to_int`
  (`em_player_stage_workers.c`, not yet in the Makefile's `COMMON`) or
  `em_effect_original_float_to_int`. The oracle binds the first one.

**5. Makefile.** Add `src/game/em_load_veil_particles.c` to `COMMON`, plus
`src/game/em_player_stage_workers.c` if nothing else has linked it yet.

**6. Drawing.** Until the GS side exists (section 5), binding 0021B1B0 only
produces packets that nothing draws. The veil stays black and the only
state that changes is +0x14. Bind it together with the channel-0 packet
consumer.

## 4. Verification

`python3 tools/test_load_veil_particles_reference.py` (proposed
`make test-load-veil-particles-reference`) builds the module as a shared
library.

- **The original side.** The oracle (`VeilEE`: the fall lane's `FallEE`,
  imported and not edited; COP1 through `tools/ee_float_model.py`) executes
  every routine in section 1 unmodified. It runs over a copy of the
  captured RAM `build/startup-reference/opening_ee.bin`.
- **The worker leaves.** 0011DF78 and 001281C0 also run their original code,
  in place at the caller's stack pointer. Each call is logged with its
  argument and result. The native workers are bound to
  `em_sdk_math_original_0011DF78` and `em_player_float_to_int`.
- **Callee set.** The test asserts that the jal targets of the translated
  routines are exactly these routines plus the two workers.
- **What is compared.** The native side runs over another copy of the same
  RAM, with a window that covers all 32 MB. Per case, the test compares:
  - **all 32 MB** byte for byte;
  - the return value;
  - the worker call log (order, argument, result);
  - 001DFA40's table, all 16 bytes of each entry, against the oracle's
    stack frame. The native buffer is seeded with the stack words the
    original frame held at 001DFA40's entry.
- **Cases.**
  - 0021B1B0 over the captured block, with context + 0x9C = 0 and 1, and
    with a varied phase (0 … 0.9999), level (0 … 2.5), base Y, seed and
    channel-0 cursor.
  - 001DFA40 with the two 0021B1B0 argument sets, then random channels,
    a1, rgba, k and 9C.
  - 0021B500 on edge and random phases.
  - Every builder with random arguments: halfword edges for the SDK pair,
    shift counts past 31, negative values, and SDK mode dwords 1 and others.
- **Branch coverage.** Both outcomes of every conditional branch in the
  translated routines are asserted: 16 sites. The one unreachable outcome is
  listed and asserted never taken.
- **Capture evidence.** The veil block (state 3, finished) is identical in
  18 captures: opening, playable, handoff and route beats 00–14. Its seed
  0xA6D22D01 is exactly what one executed 0021B1B0 leaves. Its phase
  0x3F4E55C8 is reached from 0 by 258 executed 0021B500 steps, and the
  native module agrees at every step. The route beats start at first
  control, after the veil has finished, so no route beat runs these
  routines.
- **Results** (2026-09-23):
  - default: 408 cases (4 × 0021B1B0, 4 × 001DFA40, 40 × 0021B500 and 360
    builder calls), 15168 worker calls, about 2 s;
  - `EM_TEST_FULL=1`: 3453 cases (24 / 20 / 409 / 3000), 88000 worker
    calls, about 8 s.
- **Mutations.** Each of these made the test fail: a wrong TEST_1 register
  number, the 64th-segment test, the table's Q lane, the seed step, and a
  12-byte ST copy.

`tests/load_veil_particles_test.c` (proposed `make test-load-veil-particles`,
ASan/UBSan, synthetic world, no original data) pins the fail-stop contract:

- a full run's cursor advance, seed and worker count;
- every refusal before a write: NULL views and workers, window one qword
  short, misaligned cursor, cursor below the window, channel outside the
  view;
- the latch;
- a mid-run worker failure;
- 0021B500's refusal and step.

## 5. Limits and open items

- **The GS side.** The port has no consumer for these packets. Drawing the
  veil needs:
  - Gouraud alpha-blended lines (PRIM 0x49);
  - the sprite copy of the displayed frame into the 256 × 256 texture at
    0x258000 (PRIM 0x116, with 001D6E60's FRAME/ZBUF/XYOFFSET/SCISSOR/TEST
    environment);
  - textured triangle strips from that texture, opaque and then
    alpha-blended (PRIM 0x14 / 0x54).
  - the contents of the static GS blocks the REF tags point into:
    - *D_00275674 + 0x20 + 400n: the per-frame-buffer environment;
    - +0x4A0 + 64n and +0x5A0 + 64n;
    - +0xBA0 + 1440a + 144b.

    These are data at *D_00275674 (0x814220 in the captures). This lane did
    not identify the routine that builds them, and reads nothing from them.
    Their register contents (alpha, test and clamp modes) decide how the
    lines and strips blend.
- **Lane 3 of the table.** The original ships stale stack words in the unused
  fourth word of every strip ST quadword. The GS ignores that word in a
  PACKED ST. The test reproduces it exactly by seeding the buffer from the
  oracle's stack. In the port, the buffer's lane 3 is whatever it holds, so
  those words differ from the hardware's (GS-invisible). Modelling them
  would need the full stack history of the frame.
- **Unreachable outcome.** 0021B1B0's unsigned-to-float conversion branch
  for a negative noise word is never taken, because the word is below 2^30.
  The translation keeps the path, and no case can reach it.
- **Load duration.** The final phase depends on how many ticks the veil runs
  in states 1 and 2 sub 0: 258 in the capture. The original's load waits on
  disc reads (test_area_load_reference.py "Capture"). The port's tick count
  is the load chain's business, not this module's.
- **Channel count.** `cursor_count` is a native bound. The captures show
  four plausible cursor words at context + 0x10. The veil uses channel 0
  only.
