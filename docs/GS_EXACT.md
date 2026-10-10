# GS-exact rendering: the CPU GS model

Job B16, lane RASTER, 2026-09-27 (fix round the same day: probe 8,
section 3.7 rewritten, points dithered, PACKED Q, binary32 divide, strict
span faults). This is the Original profile's pixel
path (PORT_PROFILES.md, "Colours: GS-exact"). The port already computes the
exact primitives: the VU1 kernel translations, the chain page and the frame
lists. Metal draws them differently from the GS at the pixel level.
`src/gs/em_gs_raster.{h,c}` is a CPU model of the GS drawing path instead;
since chain step GSFRAME (2026-10-03) it draws the Original profile's world
frames (section 9), and Metal is the Enhanced profile's GPU path:

- **Input:** GS register writes (A+D form) or whole GIF packets (PACKED,
  REGLIST, IMAGE), exactly what the GIF hands the GS.
- **Output:** the GS local memory (4 MiB, swizzled as the GS stores it).
  That covers colour buffers, Z buffers, textures and CLUTs.

**Reference.** The reference is PCSX2's software renderer, measured through
the decomp's conformance harness (decomp `docs/GS_CONFORMANCE.md`). It is
PCSX2's model of the GS, not real hardware. Section 3.7 is a clear case of
reference-specific behaviour. Bit equality with a real GS is not
established by anything here.

**Which build** (2026-10-09; FIRST_LEVEL_AUDIT.md 1b F1). The conformance
numbers in sections 1..8 were measured on the legacy build (v2.6.3); the
project's PCSX2 fork (based on v2.9.114) gives identical memory on all 906
tests, so they hold for both. Route and frame comparisons (section 10) use
the **fork** as the pixel reference from now on; the fb2 numbers below
stay relative to v2.6.3 until those frames are regenerated on the fork.
The one renderer difference that reaches this game: v2.6.3 throws away a
line segment starting a quarter row below the scissor's last row, the fork
draws its first pixel; the rules of 3.6 predict the fork's pixel (not yet
confirmed by a test).

**Clean room.** No emulator source was opened, read or searched. That
includes the local `pcsx2/` tree and PCSX2 / GSdx / Play! / DobieStation
code online. Every rule below rests on one of two things:
- public GS documentation (register and field layouts, GIF tag and PACKED
  formats, the page / block / column tables, the documented texture-function,
  fog, alpha-test, blend and dither formulas);
- a measurement: designed primitives drawn by the reference and compared
  with the model pixel for pixel.

One number predates this lane: the 425-byte header before local memory in
a snapshot's `gs.bin` (`FREEZE_VRAM`, from the decomp's `tools/gs_vram.py`
and `tools/c7cap_partb.py`). Per the B16 review, an earlier session
(workflow wf_b95432db, 2026-09-26) found it by reading PCSX2's save-state
code. It is a container offset, not GS behaviour, and B16 re-validates it
by measurement: every upload and fence page decodes exactly at that
offset.

Each rule names its evidence. "Doc" means documentation only (not
measured). Strict mode refuses those features (section 6). Counts of the
form "N values off" are colour or Z words of the named tests that differ
from the reference.

## 1. Status in one table

The model gets the **same packet bytes** the reference got and starts from
zeroed local memory (section 7). There are **906 conformance tests** in 30
batches: 8,056,832 colour values and 1,884,160 Z values. (The first version
of this document said "6,848,512 buffer values" for its 759 tests; that
number counted colour values only.)

| capture set (decomp `build/b16/`) | tests | bit-exact | colour values off | Z values off |
|---|---|---|---|---|
| gscap (GSCAP lane: layout, raster, shade, texture, pixel, frame, probe2) | 252 | 228 | 18 | 3,309 |
| gscap3 (p3_start, p3_z, p3_stq) | 102 | 77 | 21 | 1,662 |
| gscap4 (p4_rcp, p4_z, p4_misc) | 48 | 45 | 3 | 22 |
| gscap5 (p5_s, p5_q) | 78 | 41 | 1,734 | 0 |
| gscap6 (p6_cov, p6_tfx, p6_z) | 61 | 45 | 0 | 220 |
| gscap7 (p7_lvl, p7_wrap, p7_z, p7_zc, p7_scope, p7_flush) | 218 | 124 | 83 | 11,356 |
| gscap8 (p8_span, p8_class, p8_misc, p8_gif, p8_more, p8_gif2) | 147 | 143 | 15 | 0 |
| **all** | **906** | **703** | **1,874** | **16,569** |

Every capture packet also draws in strict mode with no refusal and no span
fault (section 6).

**Exact:**
- the local-memory layout of every format tested, and all transfers;
- the GIF paths: every PACKED descriptor used in context 1 (with the Q a
  PACKED RGBAQ takes), REGLIST and IMAGE (2.1);
- triangle, sprite, line and point coverage, except 3 pixels (8.4);
- flat and Gouraud colour and fog weight inside triangles, except 6 values
  (8.3);
- the texture functions and fog at the interpolators' precision;
- the perspective divide for constant S, T, Q, and sprites with any Q;
- the STQ vertex grid and the span it depends on (3.7, 4.5): every span
  boundary the model uses is measured, or strict mode faults where an
  unmeasured one could change a pixel (section 6);
- nearest and bilinear sampling, wrap modes, CLUT layouts and the CLD 0..5
  loads;
- textured points (UV and STQ);
- alpha test, destination alpha test, Z test, all blend equations,
  COLCLAMP, PABE, FBA, FBMSK, CT16 conversion and dithering (triangles,
  lines and points; not sprites);
- the row coordinate of UV sprites, accumulated in binary32 (3.4, since
  2026-10-10), and with it 001DDE10's depth-of-field pass: bit-exact on all
  7 captured frames of the original (all of local memory; section 7,
  `test_dof_pass_reference`).

**Not exact:**
- Z: 16,569 of the 1,884,160 Z values (0.9 %). 14,694 of them are one LSB
  off; GSCAP `z_interp_z32` has 1,874 off by 2..120 (Z32 values above
  2^24) and `z_range16` one off by 16,383 (8.1). Most of gscap7's Z count
  is the level-class Z repeated across its variants.
- Texture coordinates of perspective triangles whose span has one Z (the
  vertex-grid path): up to 2.0 % of pixels, 1/16 texel (8.2).
- UV (FST 1) triangles: 24 values in 5 tests, one 1/16 texel or texel
  (section 8.7).
- A few isolated pixels of Gouraud lines and of edges through pixel
  centres.

Section 8 has the exact counts and what is known about each. The game's
**level class** is the most important case for the Original profile: a
fogged, Gouraud, perspective T8 + CLUT tristrip with MODULATE, alpha test
and Z. Five original tests cover it
(gscap `level_class`, p3_stq `level_0..3`): its colour is exact
except 3 values in 36,864. Its Z is off by one LSB on 0.8-4.8 % of pixels
(8.1).

## 2. Local memory, GIF and transfers

### 2.0 Layout (`em_gs_addr32/16/8/4`)

- **PSMCT32 / PSMCT24.**
  - A page is 64x32 pixels and holds 32 blocks of 8x8.
  - The block order, row by row, is 0 1 4 5 16 17 20 21 / 2 3 6 7 18 19 22 23
    / 8 9 12 13 24 25 28 29 / 10 11 14 15 26 27 30 31.
  - A block holds four columns of 8x2 pixels. In a column, pixel row 0 holds
    words 0 1 4 5 8 9 12 13 and row 1 holds 2 3 6 7 10 11 14 15.
  - PSMZ32 / PSMZ24 use the same block order XOR 24.
  - Evidence: Doc. The measured maps (decomp `layout` batch, `maps.npz`) are
    equal to these tables.
- **PSMCT16 / PSMCT16S / PSMZ16 / PSMZ16S.**
  - A page is 64x64 pixels and holds blocks of 16x8.
  - The four documented block orders are in `em_gs_raster.c`.
  - A column is 16x2 pixels.
  - Evidence: CT16 and Z16 are measured. The test decodes every CT16 and Z16
    buffer through the measured maps. CT16S and Z16S are Doc.
- **PSMT8.**
  - A page is 128x64 pixels and holds blocks of 16x16 in the PSMCT32 block
    order.
  - A column is 16x4 pixels. Byte = column * 64 + (j & 1) * 4 + (j >> 1) * 16
    + ((x >> 3) & 1) * 2 + (r & 1) * 8 + (r >> 1). Here r = y & 3, and
    j = (x & 7) XOR 4 when (r >> 1) XOR (column parity) is 1.
  - Evidence: measured from the texture batch's identity and random T8
    uploads read back from `gs.bin` (all bytes). Every T8 upload of the
    later batches is also compared (section 7 B).
- **PSMT4.**
  - A page is 128x128 pixels and holds blocks of 32x16 in the PSMCT16 block
    order.
  - Nibble = column * 128 + (j & 1) * 8 + (j >> 1) * 32 + ((x >> 3) & 3) * 2
    + (r & 1) * 16 + (r >> 1), with j as for PSMT8.
  - Evidence: measured (the T4 uploads, all nibbles).
- **T8H / T4HL / T4HH.** Stored in bits 24..31, 24..27 and 28..31 of a CT32
  word. Doc.

### 2.1 GIF packets (`em_gs_gif`)

The GIF tag fields are NLOOP, EOP, PRE, PRIM, FLG and NREG / REGS.
- **PACKED descriptors.** 0 PRIM, 1 RGBAQ, 2 ST, 3 UV, 4 XYZF2 (ADC selects
  XYZF3), 5 XYZ2 (ADC selects XYZ3), 6..9 TEX0 / CLAMP (the low 64 bits as
  the register value), A FOG, C / D XYZF3 / XYZ3 (the same PACKED layout as
  4 / 5, without a drawing kick), E A+D, F NOP. Only the documented bits of
  each field are used. In REGLIST, E and F skip their word.
- **The Q of a PACKED RGBAQ** is 1.0 at the start of every GIF tag and then
  the Q of the last PACKED ST in that tag. A+D writes of RGBAQ or ST do not
  change it.
- **REGLIST:** two registers per quadword (an odd count pads the last
  one). **IMAGE:** transfer data.
- Evidence: the A+D and IMAGE paths are exercised by every capture packet.
  p8_gif (decomp `build/b16/gscap8`) measures the rest:
  - `pk_st_rgbaq_xyz2`: PRE with a PRIM, descriptors 2, 1, 5, 256 sprites
    with their own Q: exact.
  - `pk_q_after_packed_st`, `pk_q_after_ad_rgbaq`, `pk_q_after_ad_st`: a
    tag of [RGBAQ, XYZ2] after a tag with a PACKED ST (Q 0.25..2.0), after
    an A+D RGBAQ with Q 3, or after an A+D ST. All exact with Q 1.0; the
    earlier model, which kept the Q across tags, left 1,024 values off in
    each.
  - `pk_uv_xyzf2_fog`: descriptors 3, 1, 4 (fogged Gouraud UV triangles,
    nearest) and A, 1, 5 (FOG then XYZ2 sprites): exact except 1 value, a
    UV triangle texel (section 8.7).
  - `pk_junk_adc_ad_nop`: descriptors 1, E, F, 5 with ones in every unused
    bit of RGBAQ and XYZ2, two vertices with ADC: exact except 1 Gouraud
    colour value (section 8.3).
  - `reglist`: [PRIM, RGBAQ, XYZ2, XYZ2] and an odd count [RGBAQ, XYZ2,
    XYZ2]: exact.
  - p8_gif2 (third run): `pk_desc0_prim` (descriptor 0 with ones above bit
    10), `pk_desc6_8_tex` (descriptors 6 and 8 per sprite, junk in the
    high 64 bits) and `reglist_e_f` (REGLIST E and F words holding FRAME
    and SCISSOR values that would move the drawing if written) are exact.
    `pk_descCD_xyz3` sends strip vertices through descriptors D and C in
    the 64-bit register layout; the reference reads them in the PACKED
    XYZ layout and does not draw at them. That reading is exact; reading
    the register layout (the first model) leaves 4,320 values off,
    ignoring the descriptors 1,913, and the PACKED layout with a drawing
    kick 1,117.
  - Not measured: descriptors 7 and 9 (context 2, refused in strict
    mode).

### 2.2 Transfers

- **HOST -> LOCAL.** Pixels arrive in raster order over the TRXREG rectangle
  from (DSAX, DSAY): 4, 3, 2 or 1 bytes per pixel, or a nibble for T4 (low
  nibble first).
- Evidence: measured. Every upload rectangle of the captures equals the
  reference through the model's own addressing: CT32 textures and CLUTs, T8,
  T4, the address textures and the blend destinations.
- **LOCAL -> LOCAL** is Doc. Strict mode refuses it.

## 3. Primitives

### 3.0 The vertex queue

- **Kicks.** XYZ2 / XYZF2 kick and draw; XYZ3 / XYZF3 kick without drawing.
  A PRIM write empties the queue.
- **Assembly.** Lists draw every 1 / 2 / 3 / 2 vertices. Strips use the last
  three vertices and fans the first and the last two. A line strip draws
  from the last two.
- **Attributes.** With PRMODECONT.AC 1 they come from PRIM, otherwise from
  PRMODE.
- Evidence: Doc, and the capture packets, including a tristrip, trifans,
  line strips and the level strips.
- **Flat colour.** A triangle takes its third vertex. A line takes its
  second vertex. A sprite takes its second vertex (IIP 0 or 1). A point
  takes its own. Sprite Z and F come from the second vertex.
- Evidence: measured (GSCAP `flat_select`, `z_small`).

### 3.1 Triangle coverage

- **Window coordinates** are vertex (12.4) minus XYOFFSET. Pixel (x, y) is
  sampled at the integer point (x, y).
- **Rule.** A pixel is covered when its sample point is strictly inside all
  three edges. A sample exactly on an edge counts when that edge is a left
  edge or a horizontal top edge (top-left rule).
- **Scissor.** The bounds are inclusive.
- **Span.** Each row's span is solved exactly from the edge equations in
  64-bit integers.
- Evidence:
  - GSCAP: 500 triangles and 38,809 pixels with 0 mismatches, including
    the XYOFFSET, half-pixel and scissor tests. The other 23 candidate
    rules mismatch 564..4,959 pixels (decomp GS_CONFORMANCE.md 5.2).
  - p6_cov: 288 triangles (12 tests x 24), each with an edge through pixel
    centres. The edges cover 24 slopes, both sides, both directions and
    three vertex slots. All exact.
- Exception: section 8.4.

### 3.2 Colour and fog weight in triangles

This is the arithmetic that reproduces every Gouraud colour and fog-weight
value of the captures (except 8.3).

1. **First drawn pixel.** For each row, `xd` is the first covered pixel, or
   the scissor's left bound when that is further right.
2. **Blocks.** Pixels are grouped in 4-pixel blocks aligned to x ≡ 0 mod 4.
   The start block holds `xd`; `jd = xd mod 4` is its lane.
3. **Values.** With E the exact plane value of the attribute (vertex values
   0..255, barycentric at the integer sample) and g = dC/dx:
   - start `V7 = floor(128 * E(xd, y))` (8.7 fixed point);
   - block step `s = trunc(512 * g)` (toward zero; in 1/128 units per 4
     pixels);
   - lane offset `L(d) = trunc(128 * d * g)` with d = lane − jd (−3..3);
   - pixel x, in block k after the start block at lane j, holds
     `V7 + k * s + L(j − jd)`.
4. **Use.** The 8-bit colour is that value >> 7. The texture function and
   fog use the 8.7 value itself (section 5.1).

Evidence:
- **p3_start** (38 tests). Triangles have dC/dx = k/512 exactly over
  512-pixel rows, so every row shows its start to 1/512. The geometry
  covered vertical, sloped and off-screen left edges and three inner
  scissors.
  - The 23 colour tests (376,832 values) and 8 fog tests (131,072) are all
    exact.
  - This fixed the 1/128 start, the start at the scissor-clipped first
    pixel, the dependence on `xd mod 4` and the relative lanes.
- **Earlier models.** floor(exact) matched 41,607 of 42,072 values. The
  best previous model (row start plus a 2^-9 step) matched 291,373 of
  291,716 (decomp GS_CONFORMANCE.md 5.3).
- **This model** reproduces all Gouraud, ramp and field tests of GSCAP and
  all dithered Gouraud frames, except the 5 values of 8.3.

### 3.3 Z in triangles

- **Row start.** Taken from the **top vertex** (least Y, then least X) with
  the gradients' exact numerators times the **binary32 reciprocal of the
  area**:
  `Zs = z_top + (xd − x_top) * Nx * f32(1/A) + (y − y_top) * Ny * f32(1/A)`,
  in double.
- **In the row:** `z(x) = floor(Zs + (x − xd) * dZ/dx)` with the exact
  gradient.
- **Clamp.** Values beyond the format clamp to its maximum. Z24 keeps the
  stored top byte.
- Evidence:
  - **p4_z** (24 tests, dZ/dx = 1/512 exactly, so each row shows its start
    to 1/512). With the f32(1/A) factor 22 values in 1 test are off. With
    the exact reciprocal, 27,239 values in 15 tests are off.
  - The top-vertex origin makes the model independent of vertex order, as
    the reference is (probe2 `gouraud_order_*`, p6_z's four orders per
    triangle give equal counts).
  - Z24 / Z16 clamping: measured (GSCAP `z_range`).
- Not exact: section 8.1.

### 3.4 Sprites

- **Coverage:** x0 <= x < x1 and y0 <= y < y1 on the sorted corners.
- **Colour, Z and F** come from the second vertex.
- **Texture coordinates:**
  - UV, the row coordinate V (measured 2026-10-10, below): accumulated in
    binary32, one step a row, from the sprite's first covered row. In
    texel units (V / 16 and Y / 16, both exact): step = (v1 - v0) / (y1 -
    y0), rounded once; at the first covered row v = v0 + (row - y0) *
    step, the product and the sum each rounded; at every next row v +=
    step, rounded; the coordinate is floor(16 v) in 1/16 texel. Scaling by
    powers of two is exact, so the same holds in 1/16 units. Where the
    gradient is dyadic every value is exact and this is the floor of the
    exact value; where it is not, the accumulated rounding leaves some rows
    one 1/16 texel below an exact integer value.
  - UV, the column coordinate U: the floor of the exact value (affine
    between the corners as given), in 1/16 texel. No capture tells it from
    the row rule applied to the columns (every measured column gradient is
    dyadic or meets no integer value where the two differ); strict mode
    refuses a sprite where they differ (section 6).
  - STQ: S and T affine between the corners. Both corners use the second
    vertex's Q, and the divide is that of section 4.5.
- Evidence:
  - Coverage: 93 sprites, 5,704 pixels.
  - The second vertex's Q: p3_stq `sprite_q_bil` / `sprite_q_near` are
    exact. The alternative (each corner's own S/Q, interpolated) leaves
    5,580 and 4,066 values off there, and 2,520 in p4_rcp.
  - The row rule: 001DDE10's depth-of-field pass (CHAIN_PAGE.md section
    6.2), captured in the PCSX2 fork on both sides of the pass (decomp
    `build/dof_capture`: 7 frames and a repeat; section 7,
    `test_dof_pass_reference`). Its blend sprite stretches 256 texel rows
    over the 224 rows of a half-line field (V 0.5..256.5 over Y
    -0.5..223.5: 8/7 texel a row), so every seventh row (y = 3 mod 7) has
    an integer exact value. With the floor of the exact value the model
    left field / copy pixels off in every frame a blend reaches (open_w0170
    2,695 / 1,349, open_w0300 2,209 / 1,039, open_w0880 639 / 373,
    play_first_control 183 / 126, play_hill_slide 251 / 160). Solving each
    integer row from the pixels only the last blend reaches (its source is
    the captured copy itself) gives one 1/16 texel below the exact value at
    rows 24, 31, 38, 45, 52, 59, 157 and 164, and the exact value at rows 3,
    10, 17 and 66 .. 150 (15 rows); the floor of the exact value fits every
    other row. The binary32 accumulation reproduces all 23 rows and every
    other one. A product from the start in binary32 (the step per 1/16
    pixel, per pixel or in normalised units, also through a reciprocal or
    a fused multiply-add), a product from the first row and an
    accumulation in double precision each miss the 8 rows below the exact
    value; blocks of four rows miss 14. With the rule all 7 frames are
    bit-exact (all 4 MiB of local memory). On every UV sprite of the
    conformance packets (29,272 row and column positions, among them
    GSCAP `mag_a_bil_frac`'s row of exact value 65 at the non-dyadic
    gradient 8/21, where the reference reads 65) the accumulation gives the
    floor of the exact value, so the 906 tests are unchanged.
  - Not settled by these captures: the accumulation's start when the
    sprite's first covered row lies above the scissor (from that row or from
    the scissor's), its start as one fused multiply-add, and corners given
    bottom to top; the columns (above). Strict mode refuses a sprite where
    any of these would change a coordinate it draws (section 6). None
    occurs in the first level: in an instrumented run of the level smoke
    through the AREA01 arrival (a scratch build counting every UV sprite
    drawn, 2026-10-10) the only UV sprites with a non-dyadic gradient were
    the depth-of-field blends (52,764 of 105,808 UV sprites, all on the row
    axis, none starting above the scissor or upward, no column gradient
    non-dyadic), and the level smoke with the check in place refuses
    nothing.

### 3.5 Points

- The point lights pixel (floor(x + 1/2), floor(y + 1/2)). Its own
  attributes are used. Points are textured like any primitive (UV, or STQ
  with the grid of 4.5) and dithered (5.3).
- Evidence: 24 points (GSCAP raster). p8_misc `tex_points_{uv,stq}_{near,
  bil}` (1,024 textured points each) and `dither_points_*` (4,096 points
  each) are exact.

### 3.6 Lines

- **Major axis.** It is the axis of the larger extent; a 45-degree line is
  x-major.
- **Pixels lit.** For every centre line on the major axis from the start
  (inclusive) to the end (exclusive), the pixel at the minor coordinate
  rounded half up is lit. It is not lit when the end point lies inside that
  pixel's diamond (|dx| + |dy| < 1/2). A point exactly on the boundary
  counts as inside when its minor offset is negative.
- **Before the start.** The pixel just before the start is lit only when
  the start lies inside its diamond.
- **Attributes:** colour and F at `floor(128 * exact)` along the major axis.
  Z is the floor of exact.
- Evidence:
  - Coverage: 136 lines and 1,931 pixels, 0 mismatches (decomp
    GS_CONFORMANCE.md 5.2).
  - Attributes: p4_misc `line_exact_0..2` and `line_ymajor` (gradients of
    k/512 and k/16 exactly) are exact.
- Residuals: section 8.5.

### 3.7 The span: queued primitives and the constant-Z decision

The reference decides one thing over a **span** of primitives rather than
per primitive: whether the STQ vertex grid of 4.5 applies.
- **Triangles and points:** the grid applies exactly when **every vertex
  of every primitive in the span has the same Z** (Z 0 or any other
  value). Whether Z is written or tested plays no part, and neither does
  any other state. A triangle list whose triangles each have one Z, but
  not the same one, gets no grid (p8_more `trilist_flat`), and neither do
  points of different Z (p8_class `c*_rev_tri_point`).
- **Sprites:** a span of sprites always uses the grid, whatever the Z of
  either corner (p8_class `sprite_corner_*`, `c*_sprite_tri`). The model
  leaves sprite vertices out of the Z comparison; since a class change ends
  the span, a sprite span holds only sprites.

This is almost certainly an artefact of the reference (a real GS has no
notion of a span). It is modelled because it decides 1/16-texel
coordinates, and the Original profile measures against this reference.

**Implementation.** The model queues primitives with their state (a copy
of the Draw state and the context) and draws the queue in order when the
span ends (`span_end`). Each boundary is either **measured** (a capture
shows that the reference's span ends there, or does not) or **unmeasured**.
The model ends the span at an unmeasured boundary too, but records it: see
"Unmeasured boundaries" below.

**Measured to end the span:**
- a primitive whose class (point, line, triangle, sprite) differs:
  triangle to sprite and back, triangle to point and back (p8_class
  `c*_tri_sprite`, `c*_sprite_tri`, `c*_rev_*`, `c*_tri_point`);
- a primitive whose attribute bits differ: IIP (p7_flush `prim_iip`,
  p8_span `rev_prim_iip`), FGE and ABE (p8_span `prim_fge`, `prim_abe`),
  TME and FST (p8_span `rev_prim_tme`, `rev_prim_fst`). AA1, FIX and CTXT
  1 are refused in strict mode (section 6);
- a TEX0 write that changes TBP0, TCC or TFX (p7_flush `tex0_tfx`, p8_more
  `tex0_tbp`, `tex0_tcc`), with TME 1;
- a change of TEX1 (a K change with MXL 0, p8_span `tex1_k`), CLAMP
  (p7_flush), TEXA (a CT32 texture, which TEXA does not affect: p8_span
  `texa`), all with TME 1;
- ALPHA with ABE 1 (p8_span `alpha_abe1`), PABE with ABE 1 (`pabe_abe1`),
  FOGCOL with FGE 1 (`fogcol_fge1`), DIMX with DTHE 1 (`dimx_dthe1`, on a
  CT32 frame, where DIMX changes no pixel);
- a change of TEST, COLCLAMP, DTHE (p7_flush), XYOFFSET, SCISSOR, FRAME
  (FBMSK), ZBUF (PSM, with ZMSK 1) and FBA (p8_span `xyoffset_move`,
  `scissor_shrink`, `frame_fbmsk`, `zbuf_other`, `fba`);
- a CLUT load (4.6) that changes the CLUT buffer (p8_more
  `t8_clut_reload_change`; the queued primitives still read the old CLUT,
  p8_misc `clut_cld1_same_value`), or that loads from another CBP even
  with the same contents (p8_more `t8_tex0_cbp_reload`,
  `t8_tex2_cbp_reload`), while the span is textured;
- a HOST -> LOCAL transfer into the span's texture, frame or Z memory, even
  with the bytes already there (p8_span `trx_tex_same`, `trx_frame`,
  `trx_z`). The model tests an overlap of whole pages, which these tests
  hit; how the reference decides an overlap near a page edge is not
  measured;
- the caller's `em_gs_flush` (reads of local memory must call it first;
  `em_gs_read_frame_rgba` does).

**Measured not to end the span:**
- the same value written again (TEX0, TEX1, XYOFFSET, SCISSOR in
  p7_flush; TEST, CLAMP, COLCLAMP, DTHE, FRAME, ALPHA, FOGCOL and a TEX2
  giving the same TEX0 in p8_span);
- ALPHA and PABE with ABE 0, FOGCOL with FGE 0, DIMX with DTHE 0
  (p7_flush, p8_span `pabe_abe0`, `dimx_dthe0`);
- TEXFLUSH, a PRIM rewrite, a switch from strip to triangle list, a
  transfer into unused memory (p7_flush, p7_scope);
- MIPTBP1 with MXL 0 and TEXCLUT with a CT32 texture (p8_span `miptbp1`,
  `texclut`);
- TEX0 or TEX2 writes that change only the CLUT fields (CBP, CPSM, CSM,
  CSA, CLD), with any texture format (p8_span `tex2_cbp`; p8_more
  `tex0_cbp_ct32`, `tex0_cld_ct32`, `tex2_cld_ct32`, `tex2_csa_ct32`,
  `t8_tex0_cbp_noload`, `t8_tex2_cbp_noload`). A CLUT load decides for
  itself (above);
- a reload of the same CBP with the same contents (p8_more
  `t8_tex0_same_reload`), and a transfer into CLUT memory without a reload,
  with the same or other bytes (`t8_clut_mem_same`, `t8_clut_mem_other`).

**Unmeasured boundaries.** The model ends the span at:
- a TEX0 / TEX2 write that changes only TBW, PSM, TW or TH;
- a CLUT load from the same CBP with the same contents but another CPSM,
  CSM or CSA, and the first CLUT load the model sees while a textured span
  is queued (the reference's previous CBP is unknown);
- SCANMSK changes;
- LOCAL -> HOST and LOCAL -> LOCAL transfers (both refused in strict mode).

A span that ends at one of these starts or extends a **chain**. When a
later span ends, the model checks the chain: if one of its spans has one Z
and uses STQ, and the chain's Z are not all equal, merging the spans would
change that span's grid decision. The result then depends on an unmeasured
rule: `span_faults` counts it, and strict mode refuses it
(`EM_GS_REFUSE_UNMEASURED`, "a span boundary the grid decision depends
on"). The pixels are drawn under the model's rule, but the Original
profile faults the scene. A measured boundary, or a primitive of other
attributes or class, closes the chain.

**Why the reference's rule looks field-based.** A TEX0 or TEX2 write with
CLUT fields that differ does not end the span, but a CLUT load from another
CBP does, even with the same bytes. A TEXA change ends it although a CT32
texture does not use TEXA, while MIPTBP1 and TEXCLUT do not. So the rule is
not "the pixels would change"; the model lists the measured cases instead
of guessing a principle.

Evidence (decomp `build/b16/gscap7`, `gscap8`; probes
`tools/gs_conformance_probe7.py`, `tools/gs_conformance_probe8.py`):
- **p7_lvl.** The four level strips of p3_stq, each with one feature
  changed: no fog, flat, DECAL, CT32 texture, Z ALWAYS, a triangle list,
  S / 4, and all of them at once. None of them brings the grid back: with
  it 35..73 values per test are off, without it 0 or 1.
- **p7_wrap.** p3_stq's twelve perspective triangles and the four strips
  over the address texture, under CLAMP and REPEAT. The grid applies in all
  32 drawing tests; wrapping plays no part.
- **p7_z.** Each geometry drawn in five ways:
  - Z written with varying vertex Z (ALWAYS or GEQUAL): no grid;
  - varying vertex Z without a Z write: no grid;
  - Z written with all vertex Z 0: grid;
  - alpha test only: grid.
- **p7_zc.** Constant vertex Z (0x123456 or 1) gives the grid. One strip
  vertex with Z 1, or Z planes in x only or y only, give no grid. With the
  one differing vertex, a per-triangle rule would still leave 22..49
  values off per strip, because the later triangles have equal Z.
- **p7_scope.** In all 16 tests, one varying-Z triangle anywhere removes
  the grid from the whole span, across PRIM rewrites, a strip / list
  switch, a same-value TEX1 and a FOGCOL write, in either order.
- **p7_flush, p8_span, p8_more.** A varying-Z half strip, one write, a
  constant-Z half strip (or the reverse for TME and FST). The writes change
  no pixel except through the grid; the model, run with the boundary
  forced to end and forced not to end, predicts 19..49 values that differ
  per p8_span / p8_more test (`gs_conformance_probe8.py discriminate`;
  for `t8_clut_reload_change` the no-end prediction is the grid switched
  off, 42 / 49 values). The reference matches one of the two predictions
  exactly in every test, apart from the UV half strips of
  `rev_prim_fst` (8 and 5 values, all in the UV half, section 8.7).
- **p8_class.** Strips, 1,024 2x2 constant-S sprites (p4_rcp's
  construction) and points over a 1024 x 1 address texture, one set after
  the other with nothing between. With the class rule the tests are exact;
  without it 151..2,116 values per test are off.

All p7 and p8 tests are exact apart from the 8.2 residue of 1-2 values in
the perspective triangles, the level Z of 8.1, the UV half strips of
p8_span `rev_prim_fst` (section 8.7) and the two p8_gif values (2.1).

## 4. Texturing

### 4.1 Nearest

- The texel is (floor(u), floor(v)) of the texel coordinate at the integer
  sample.
- Evidence: measured (GSCAP), including fractional UV and minification.

### 4.2 Bilinear

1. U = floor((u − 1/2) * 16), and V likewise. The index is U >> 4 and the
   weight is U & 15.
2. Horizontal first: `a = t00 + ((t10 − t00) * fu >> 4)`.
3. Then vertical: `a + ((b − a) * fv >> 4)`. Every shift is a floor.

- Evidence: 14,651 of 14,651 pixels (GSCAP).
- **Filter choice.** The model uses MMAG. With MXL 0 and MMAG equal to MMIN
  (the level class, TEX1 0x60) the LOD does not matter. MMAG ≠ MMIN and the
  mipmap filters are Doc, and strict mode refuses them.

### 4.3 Wrap modes

- They apply to the integer texel index:
  - REPEAT: i & (w − 1);
  - CLAMP: 0..w − 1;
  - REGION_CLAMP: MINU..MAXU;
  - REGION_REPEAT: (i & MINU) | MAXU.
- Evidence: 8 of 8 tests.

### 4.4 Sprite texture coordinates

See 3.4.

### 4.5 STQ: the vertex grid and the divide

- **The vertex grid.** When the span uses the grid (3.7: one Z, or a
  sprite span), every vertex's S and T are floored to 2^−(14 − E) and its
  Q to 16 significant bits (2^−(15 − E)). E is the binary exponent of that
  vertex's Q; a sprite uses the second vertex's Q for both corners.
  Otherwise S, T, Q are used as given.
- **In a triangle**, the values are interpolated to the pixel
  screen-linearly and rounded to binary32.
- **The texel coordinate** is `floor(f32(S / Q) * size * 16)` in 1/16
  texel: the quotient is rounded to binary32 before the scale. Nearest uses
  >> 4; bilinear uses − 8 as in 4.2. Coordinates beyond 2^40 are clamped
  (not measured).
- Evidence (every test named here has vertex Z 0, so its span has one Z):
  - **p4_rcp:** 2,944 constant-STQ sprites and 128 constant-STQ triangles
    over a 1024-texel address texture. All exact with the grid.
    - Without it: 3,976 values off in 4 of the 6 tests.
    - A fixed 2^-14 / 2^-15 grid without the exponent: 420 off.
  - **p5_s** (39 tests, Q = 1, S planes): 191 values off. Without the grid
    44,297. Without the binary32 rounding of the pixel values 263.
  - **p5_q** (39 tests, Q planes): 1,597 off, 138,213 without the grid.
    Taking E from one vertex for all three (the first, the last, the
    largest or the smallest Q) is worse in every case: 17,936..23,726
    colour values off against 2,019 (the 541 tests of gscap..gscap6,
    measured before the span rule, which does not change them).
- **The binary32 quotient** (fix round): in p8_class, three sprites have
  an exact S/Q * 16384 within 0.0005 below an integer (for example
  10,863.9996), and the reference reads the next 1/16 step, which binary32
  rounding of the quotient gives. With it p8_class is exact; without it 22
  values in 7 tests are off. p5_q drops from 1,597 to 1,543 values off (20
  tests better, 4 worse: `sq_gen_0`, `_2`, `_3`, `_7`); GSCAP `persp_1`
  and `level_class` drop by one value each and `persp_2` rises by one. A
  binary32 reciprocal times S is worse (p4_rcp 16 values off).
- Residual noise: section 8.2.

### 4.6 CLUT

- **Loading** happens on a TEX0 write when CLD says so (CLD 0..5 with the
  CBP0 / CBP1 compare), CSM1.
- **CT32 entries.** The low halfword is kept in buffer slot i and the high
  halfword in i + 256.
- **PSMT8.** Index i reads the 16x16 CLUT image at i with bits 3 and 4
  swapped.
- **PSMT4.** It reads the 8x2 image row-major at CSA * 16.
- **Filtering.** Bilinear filters the looked-up colours.
- **CLD.** 0 no load; 1 load; 2 load and CBP0 = CBP; 3 load and CBP1 =
  CBP; 4 load only when CBP differs from CBP0, then CBP0 = CBP; 5 the same
  with CBP1. CBP0 and CBP1 are independent. A load reads the CLUT memory
  at that moment: rewriting the memory without a load leaves the buffer
  as it was.
- Evidence: GSCAP texture batch, T8 / T4, both filters, CSA 1 (4 of 4
  tests), and the level class. p8_misc `clut_cld4`, `clut_cld5`,
  `clut_cld45_cross`, `clut_cld0_cld1`, `clut_cld1_same_value`,
  `clut_cld2_3` (T8 sprites drawn after each TEX0, the CLUT memory
  rewritten between loads) are exact; loading on every CLD 4 / 5 leaves
  256 values off in `clut_cld4` / `clut_cld5`.
- The span effects of a load: 3.7.
- CSM2, CT16 CLUTs and TEXA expansion (CT24 / CT16 textures) are Doc.

### 4.7 UV triangles (FST 1)

- U and V are interpolated exactly and floored in 1/16 texel.
- Evidence: p4_misc `uv_exact_*` (slow exact U gradients), all 6 exact.
- Not exact in general: UV triangles leave 1/16-texel residues (section
  8.7).

## 5. Pixel pipeline

### 5.1 Texture function at the interpolators' precision

C7 and A7 are the 8.7 vertex colour and alpha from section 3.2. For
sprites and flat shading they are the value << 7.
- **MODULATE:** C = min(255, Ct * C7 >> 14); A = TCC ? At * A7 >> 14 :
  A7 >> 7.
- **DECAL:** C = Ct; A = TCC ? At : A7 >> 7.
- **HIGHLIGHT:** C = min(255, (Ct * C7 >> 14) + (A7 >> 7)); A = TCC ?
  min(255, At + (A7 >> 7)) : A7 >> 7.
- **HIGHLIGHT2:** C as HIGHLIGHT; A = TCC ? At : A7 >> 7.
- **Untextured:** C = C7 >> 7.

Evidence:
- **p3_start `mod_*`** (MODULATE over constant 0xFF texels with Gouraud
  colour, 65,536 values): exact. With the 8-bit colour (Ct * C >> 7),
  62,248 values are off.
- **p6_tfx** (25 tests, 395,264 values): all four functions x TCC 0 / 1,
  over two textures, with and without fog, on exact colour planes. All
  exact. With the 8-bit colour and alpha, 258,183 values are off.

### 5.2 Fog

- C' = FOGCOL + ((C − FOGCOL) * F7 >> 15), with F7 the 8.7 fog weight of
  section 3.2. It is applied after the texture function.
- Evidence:
  - p3_start `fog_*` (8 tests, 131,072 values): exact. With the 8-bit
    weight, 78,223 values are off (44,588 in p6_tfx).
  - GSCAP `fog_cols` and `fog_tex` (constant F): the same formula with
    F7 = F << 7.
- CHAIN_PAGE.md section 5 and OWNER_DRAW.md section 7.2 stated
  (F * C + (255 − F) * FOGCOL) >> 8. That form is wrong: it matches only
  1,040 and 640 of 4,096 pixels (decomp GS_CONFORMANCE.md 5.5).
- **In the Metal path (2026-09-28).** Every fog site (the object units and
  the static world, the chain page's primitives, the shadow receivers, and
  the skinned path's draws) uses this rule with the 8.7 weight, through one
  shader copy (EM_FOG_GS_MSL) of the CPU mirror `em_fog_gs_blend7`
  (src/gfx/metal/em_fog_gs.h; `em_fog_gs_blend` is its constant-F form,
  F7 = F << 7). The weight of Metal's screen-linear F is
  `em_fog_gs_weight7`: F7 = floor(128 * F + 0.01), the epsilon absorbing
  float noise at vertex and constant values. On the skinned path the colour
  entering the fog is the texture function's 8-bit value floor(Ct * Cv /
  128) (float noise absorbed by 0.001), and the fogged result is exact
  8-bit, as the GS writes it. `tools/test_gs_fog_conformance.py`
  (`make test-gs-fog-conformance`) requires all 8,192 pixels of GSCAP
  `fog_cols` and `fog_tex` equal through the CPU and the Metal shader, and,
  for a Gouraud F (part C, the eight p3_start `fog_*` tests at the exact
  plane value), 342,349 of 342,831 channel values through
  `em_fog_gs_weight7` + `em_fog_gs_blend7`, where the 8-bit weight the
  shaders used before (floor(F + 0.001) with >> 8) gives 186,635; the other
  482 follow the DDA stepping of 3.2. Still not the GS: the per-pixel F is
  Metal's float interpolation at its own sample points (no DDA), and the
  colour entering the fog is the 8-bit texture function (5.1 uses the 8.7
  colour).

### 5.3 Alpha test, DATE, Z test, blend, dither, CT16, FBA, FBMSK, COLCLAMP

These are the GSCAP rules (decomp GS_CONFORMANCE.md 5.5) and the p8_misc
point cases, all exact:
- **Alpha test.** The eight ATST modes compare the post-function alpha with
  AREF. AFAIL:
  - KEEP writes nothing;
  - FB_ONLY writes RGBA and no Z;
  - ZB_ONLY writes Z only;
  - RGB_ONLY writes RGB, keeps the destination alpha and writes no Z.
- **DATE.** A pixel is written only where the destination alpha bit equals
  DATM.
- **Z test.** NEVER, ALWAYS, GEQUAL and GREATER are unsigned compares
  against the stored Z. ZMSK keeps Z.
- **Blend.** ((A − B) * C >> 7) + D per channel (floor); As and Ad are
  unclamped. PABE blends only where As bit 7 is set.
- **Dither.** Only on 16-bit frames with DTHE, for triangles, lines and
  points; sprites are not dithered. The value is (C + DIMX[y & 3][x & 3]),
  clamped or wrapped by COLCLAMP, then >> 3. Evidence for the primitive
  kinds: probe2 `dither_kinds_ct16` (flat and Gouraud triangles and a
  Gouraud line dithered, IIP 1 and DECAL sprites not) and p8_misc
  `dither_points_ct16_d1_cc1` / `_cc0` (4,096 points each: exact dithered;
  undithered, 2,164 and 2,166 values off). The CT32 and DTHE 0 controls
  are exact. The first version of this document said points were not
  dithered; no capture had drawn one, and the capture shows they are.
- **CT16 alpha** is A bit 7.
- **FBA** ORs 0x80 into the written alpha.
- **FBMSK** keeps the masked destination bits.
- **COLCLAMP 0** keeps the low 8 bits.

## 6. Strict mode and refusals

`EmGs.strict = 1` refuses every primitive that needs a feature no capture
has settled:
- AA1, PRIM FIX, context 2, PRMODECONT.AC 0 (attributes from PRMODE);
- MXL > 0, MMAG ≠ MMIN, mipmap MMIN;
- T8H / T4HL / T4HH;
- CT24 / CT16 textures, CT16 CLUTs;
- textured lines;
- SCANMSK, CT16S / CT24 frames, Z16S;
- DATE on a 16-bit frame, ZTE 0;
- LOCAL -> LOCAL and LOCAL -> HOST transfers;
- a UV sprite whose drawn texel coordinates depend on what section 3.4
  leaves unsettled (since 2026-10-10): its rows under the accumulation
  started at the scissor's first row (when the sprite starts above it),
  started as one fused multiply-add, or started from the top corner's value
  (corners given bottom to top); its columns, whose exact value the model
  uses, under the same accumulation and those variants. The check runs at
  the sprite's kick (`sprite_uv_settled`) and refuses only where one of
  them would read another 1/16 texel at a pixel the sprite draws.

A refused primitive draws nothing. It is counted (`refused_prims`),
`refusals` gets a reason bit and `reason` names the first refusal.

**Span faults.** When spans joined by unmeasured boundaries (3.7) would
change a grid decision if merged, `span_faults` counts it in any mode, and
strict mode also sets `EM_GS_REFUSE_UNMEASURED` ("a span boundary the grid
decision depends on"). Those pixels are already drawn under the model's
rule; the fault says they are not established.

The Original profile runs strict and faults the scene on any refusal or
span fault, as the port's fail-stop rule requires.

## 7. Verification

**`tools/test_gs_raster_reference.py`** (port; `python3`; about 1.4 s,
`EM_TEST_FULL=1` adds the repeat capture, about 1.9 s):
- **A.** For every test of the 30 batches, `packet.bin` goes through the C
  model **in strict mode** from zeroed memory; the span is flushed at the
  end. Every colour and Z buffer is compared through the measured maps. A
  refusal or a span fault in any capture packet fails the test.
- **B.** Every uploaded rectangle and the fence page are compared.
- **C.** (EM_TEST_FULL=1) the repeat capture of the first suite.
- **D.** The binding helper (section 9) replays 300 random primitives with
  their environment from `EmGfxGsPrim` / `EmGfxGsEnv` records. Memory must
  be byte-identical to the same writes sent as an A+D packet, and
  `em_gs_read_frame_rgba` must equal the measured decode.
- **E.** Strict mode itself: fourteen synthetic packets. AA1, MXL > 0,
  PRMODECONT.AC 0, a textured line, LOCAL -> LOCAL, LOCAL -> HOST, a
  constant-Z / varying-Z split at an unmeasured boundary (a TEX0 TW change)
  and three UV sprites of section 3.4's unsettled cases (rows starting above
  the scissor, rows given bottom to top, columns whose exact value differs
  from the row rule) must each be refused in strict mode with that reason
  and draw in the default mode. Four controls must draw with no refusal in
  both modes: the same split with one Z, the split at a measured boundary
  (TFX), a dithered point, and 001DDE10's blend sprite (the measured row
  rule).
- The open items are recorded with their exact counts (`OPEN`, 203 tests),
  so any change of a count fails.

Result on 2026-09-27 (fix round):
- 703 of 906 tests bit-exact. The other 203 are all recorded as open, with
  1,874 colour and 16,569 Z values off.
- All uploads and all fences equal. No refusal and no span fault in any
  capture packet in strict mode. D passes; E: 10 of 10. 0 failures.
- `EM_TEST_FULL=1`: 931 of 1,158 bit-exact, 0 failures.

Re-run on 2026-10-10 with the row rule of section 3.4: the same 703 of
906 (931 of 1,158 in full mode), every count of `OPEN` unchanged; E: 14 of
14. With the UV check switched off (a scratch copy) the three UV cases fail.

**`tools/test_dof_pass_reference.py`** (`make test-dof-pass-reference`,
2026-10-10; quick about 0.5 s with the library built, 5 s with a rebuild;
`EM_TEST_FULL=1` about 1.5 s): 001DDE10's depth-of-field pass against the
original's local memory on both sides of it. The decomp's fork capture
(`build/dof_capture`, its `manifest.json`: 5 frames of the opening cutscene,
2 of AREA11 play and a repeat of one, the DMA cut at the pass's two
boundaries) holds the memory before and after the pass, the registers at
its start (the last value of each that frame's stream wrote), the pass's
GIF bytes (from the EE chain and from the GS dump) and the EE RAM after
001DDE10 ran. The C side is `tests/dof_pass_reference_bridge.c`.
- **A.** The captured GIF bytes through the model (strict) from the memory
  and registers before the pass: the field (512 x 224), the copy buffer
  (256 x 256 at 0x258000), the Z buffer (untouched: equal before and
  after) and every other word must equal the memory after it; again through
  3 (full: also 8) row-band EmGs in lockstep. Reported per blend: its Z,
  alpha and the field pixels its Z test passes, and the field's pixels by
  how many blends reach them.
- **B.** The port's packets: `em_render_context_001DDE10` over the captured
  EE image with the live binding's workers, from the state before it ran
  (the channel-3 cursor back at the pass's CALL target; a cutscene frame
  snaps its pairs from the captured point; every eased value is put back
  as it was before the run's one step of it, found by inverting that step
  with the EE float model and checked by running the translation: the four
  depths of each play frame and D_00275690 in the first wide shot had
  moved, the others had converged). Every EE and scratchpad byte the run leaves must equal the
  capture's, its 001CB760 must be the slot block's CALL, and the DMA walk
  of what it built must give the captured 4,544 GIF bytes.
- **C.** The Original profile's path over the port-built image:
  `em_chain_page`'s pass mode (8 sprites, 32 DIRECT packets, marks 1, 3, 5,
  7, 8), then `em_gs_world_page_pass` and the kick (bank A's block as the
  head) with 1 and 3 workers (full: 1, 2, 3, 8), from the memory before the
  pass restored with the drawn buffers as drawn (`em_gs_world_memory_restore`),
  and with 3 (full: also 8) workers with every block counted as an upload:
  every word must equal the memory after the pass.
- **D.** (full) the repeat run equal to its first run.

Result on 2026-10-10: every capture bit-exact in A, B and C (field,
copy, Z and the rest of local memory). Mutants, each killed: the row rule
reverted to the floor of the exact value (A and C), the environment-again
mark of the field removed from the recorder (C, 3 workers), one packet
constant of 001DDE10 (B and C), the marks shifted by one primitive (C).
The first version of C restored the memory as one upload (all blocks
resident) and drew wrong bytes with 3 to 8 workers, varying from run to
run: the recorder left uploaded blocks out of its ordering, so the copy
read field rows a band was still blending. The ordering now covers drawn
blocks whatever their residency (section 9); with the old rule
`make test-gs-world`'s new every-block-uploaded frames report the data
race under the thread sanitizer.

**Mutation check** (decomp `build/b16/gscap8/model/mut2.py`, run on a
scratch copy of the model): 28 single-change mutants, all killed by the
test.
They include the review's survivors: points not dithered
(`dither_points`: 2,164 / 2,166 values off in p8_misc), no span end at a
class change (`span_no_class`: 8 tests), no span end at a CLUT change
(`clut_no_change_flush`: 6 tests), CLD 4 / 5 ignoring the CBP compare
(256 values each), the PACKED Q set to 0 or kept across tags (1,024 values
in each `pk_q_*`), and strict mode switched off (the E cases). Also:
sprite Z counted, point Z ignored, CLUT fields ending the span, a TCC
change unmeasured, MIPTBP1 / TEXCLUT ending it, TEXA / FBA / ZBUF not
ending it, DIMX / PABE always ending it, transfers never ending it, the
double-precision quotient, the span-fault check off, AC 0 and LOCAL ->
HOST allowed, and PACKED XYZ3 read in the register layout.

Also run:
- ASan / UBSan (`-fsanitize=address,undefined`, no recovery) over the 30
  capture packets, each in the default mode, in strict mode and with bits
  flipped in 2 % of its bytes: no report. (The first version found an
  out-of-range float conversion for absurd STQ values, since clamped,
  4.5.)
- `cc -std=c11 -Wall -Wextra -Wpedantic -Werror`: clean.

The capture sets are regenerated with the decomp tools (decomp
`docs/GS_CONFORMANCE.md` sections 3, 8 and 9). Their inputs are designed
primitives only, nothing disc-derived.

## 8. Open items (exact counts, 2026-09-27)

### 8.1 Z starts (16,569 values in 101 tests)

The top-vertex / f32(1/A) start is exact on p4_z except one triangle, and
makes the model independent of vertex order. 14,694 of the values are one
LSB off:
- where the triangle's apex is at the top: p6_z has 220 values off in 16
  of 24 tests; p4_z `z24_7ff000_u2` has 22;
- where gradients are very large: GSCAP `z_interp_z24` 390, `z_interp_z32`
  14; p3_z `z32_c1` 253;
- in general triangles: probe2 `gouraud_order_*` 61 or 90 each.
- **The level class:**
  - p3_stq `level_0..3` have 390, 253, 134 and 254 values off of 8,192;
  - GSCAP `level_class` has 34 of 4,096;
  - p7_lvl repeats these for each of its nine variants.

Larger differences: GSCAP `z_interp_z32` has 1,874 more values off by
2..120 (Z32 values above 2^24, where binary32 has no integer resolution),
and `z_range16` one value off by 16,383 (8.3).

Measured facts:
- the needed start offsets stay within ±0.05 LSB of the exact value;
- the in-row step is the exact gradient;
- the exact reciprocal fits the apex-top and general cases better
  (p6_z 112, probe2 552, `z_interp_z24` 202, the p3_stq levels 759) but
  breaks the flat-top ones (p4_z 27,239, p3_z 2,717). So the reference's
  start is neither form;
- no float32 setup variant tried explains both (sorted or unsorted
  vertices, binary32 products and differences, binary32 gradients).
- A lead from the review: taking the start from vertex 0 instead of the top
  vertex lowers the total to 15,784 (`level_class` 34 -> 22,
  `gouraud_order_0_2` 90 -> 53) but loses two bit-exact tests and makes
  the result depend on vertex order, which the reference's does not. The
  reference's origin or tie-break is probably neither.

Next capture: apex-top and general triangles with dZ/dx = k/512 exactly,
each vertex's Z varied alone, to separate the vertex, gradient and edge
terms.

### 8.2 Perspective STQ residue (colour: p5 1,734; p3_stq 5; GSCAP texture 9; p7 1-2 per perspective test)

- **Grid path** (span with one Z). After the grid, the reference's
  per-pixel S/Q differs from the model by a relative noise of about
  2^-19, at most about 2^-17 (measured on p5, before the binary32
  quotient of 4.5).
  - **Structure.** None: not by lane, block or row parity.
  - **What did not explain it:** binary32 plane evaluation, per-pixel and
    block accumulation in binary32, reciprocal-estimate divides, and
    per-pixel requantisation at 1..8 extra bits.
  - **Effect.** At most one 1/16 texel of bilinear weight, on 0.2-2.0 %
    of the pixels of the varying-Q triangles of p5 (`sq_gen_*`: 30..326
    of 16,384).
- **Varying-Z path** (the level class). 0-2 values per test, and the
  same 1-2 in p3_stq's perspective triangles when they are drawn with
  varying Z. This path is nearly exact already.

### 8.3 Colour starts at edge-exact rows (5 values), and one more

These are GSCAP `gouraud_strip` (4) and `gouraud_rand_3` (1), at rows
where the first pixel lies exactly on a non-dyadic left edge. The start is
one 1/128 lower than floor(128 * exact). The same edge excludes an on-edge
pixel (8.4). The exact 1/128 planes of p4_misc `col128_*` are exact, so it
is not a general float effect.

p8_gif `pk_junk_adc_ad_nop` has one Gouraud colour value off by one
(blue); it has not been analysed.

GSCAP `z_range16` (1 colour, 1 Z) is a CT16 clamp-edge case not
investigated further.

### 8.4 Coverage below the middle vertex (3 pixels)

In p4_misc `cov_nd_0` and `cov_nd_2`, two triangle pairs share a
non-dyadic edge. Below the middle vertex's row they are not watertight:
two pixels are covered twice and one by neither. GSCAP `gouraud_strip`
(3, 9) is a similar case. The 288 single triangles of p6_cov with the same
slopes are exact.

### 8.5 Gouraud lines (9 values)

These are p3_start `lines_x` (3) and `lines_y` (4) and GSCAP
`gouraud_line` (2). They are values within 0.002 of an integer, off in
both directions. Lines with exact k/512 gradients are exact.

### 8.6 Untested features

These are refused in strict mode (section 6). Also not measured:
- the unmeasured span boundaries of 3.7 (strict mode faults where they
  could change a pixel);
- PACKED descriptors 7 and 9 (context 2) (2.1);
- the model's page-level test of a transfer's overlap with the span's
  memory near a page edge (3.7).

### 8.7 UV triangle texels (24 values in 5 tests)

UV (FST 1) triangles read a neighbouring 1/16 texel (bilinear) or texel
(nearest) at isolated pixels: p3_stq `uv_tri_bil` 9, GSCAP `tri_uv` 1,
p8_span `s0_rev_prim_fst` 8 and `s1_rev_prim_fst` 5 (all in the UV half),
p8_gif `pk_uv_xyzf2_fog` 1. The slow exact gradients of p4_misc
`uv_exact_*` are exact. Rounding the interpolated U, V to binary32 (from
the exact value, or from binary64 barycentric weights) changes none of
these counts. A lead not yet tried (2026-10-10): a UV sprite's row
coordinate is accumulated in binary32 one row at a time (3.4); the same
kind of accumulation along a triangle's edges or across its spans.

### 8.8 UV sprites: the row rule's variants and the columns (unmeasured)

Section 3.4's row rule is measured on 001DDE10's blend sprite: corners
given top to bottom, the first covered row drawn (not above the scissor),
a start offset of half a row, the half-line field. Not measured, and
refused in strict mode where they would change a drawn coordinate
(section 6): the start when the sprite begins above the scissor, a fused
start, corners given bottom to top, and the columns (the model keeps the
exact value; no capture has a non-dyadic column gradient meeting an
integer value). None of these occurs in the first level (3.4). Also not
captured: a blend that passes its Z test in a whole-line field (OFY
1936.0; open_wide_t0720 is one, but no blend reaches a pixel there). The
row rule predicts it (a start offset of 0: every row y = 0 mod 7, y > 0,
can fall one 1/16 texel below its exact value); a capture of such a frame
(a cutscene frame of the other field parity) would confirm it.

## 9. Binding: how the Original profile renders through the model

Bound in chain step GSFRAME (2026-10-03, audit 1b item 2, second half).
The Original profile's world frame is the field the model draws; the GPU
renderer (Metal) is the Enhanced profile's path (`EmSettings.gpu_renderer`,
`EM_GPU_RENDERER=1`; LAUNCHER_OPTIONS.md "Resolution").

**The pieces.**
- `src/gs/em_gs_world.{h,c}` (platform-independent C, no GPU API): the
  recording, the residency check, the workers and the field.
- `src/gfx/metal/em_gfx_metal.m` (the `gsw_*` functions): with the GS frame
  enabled, a world frame's GS draws go to the model instead of the GPU; the
  backend presents the field and draws the 2D overlay pass over it.
- `src/game/em_gs_frame_live.{h,c}`: enables the GS frame at start-up
  (unless the GPU renderer is chosen), installs the GS memory image, and at
  step V hands the kicked list's head to the model.
- `src/game/em_render_context_live.c` `em_rcl_kick_head`: the head of the
  list 001D2300 kicked.
- `tools/export_gs_memory.py`: the GS memory image (below).

**What is drawn, in what order.** A world frame is the frame close's world
flush (em_render_frame.c `frame_close_out`, 001D1EA0(1)), which declares it
(`em_gfx_gs_world_frame`). From there to the kick the backend records, in
the order the port issues them (the order its GPU path drew them, which the
live modules keep as the original's list order: background, channel 0, the
page):
1. the background's channel-3 triangles with the environment its list set
   (`em_gfx_background_prims_env`: the list's own ZBUF_1 ZMSK 1, TEST, TEX0
   and RGBAQ);
2. FOGCOL, from the render context's +0xB0 (`em_gfx_fog_coefficients`);
3. the static world's run (`em_gfx_gs_opaque`) and every object unit
   (`em_gfx_object_unit`: the object kernel, its clip pass and the face
   program, the same CPU translations), each after the class-0 state block
   their REF names, D_00815360 (001D1F80(0, 1, 0)), read from the boot
   builder's GS blocks; the object triangles keep the kernel's template PRIM
   (the clip pass's PRIM is one lower);
4. the drop shadow's passes in 001DA6A0's order: state block (2,9)
   D_00815E10 and 001DA1E0's DIRECT strip (its four rows D_002531D0 from
   the effect tables export); the boxes (00237180's triangles, then
   00239C90's, PRIM 0x044 / 0x043, the call's RGBAQ); the target packet
   D_00817E20 (the 14 registers em_shadow_gs.h pins, checked in order
   against the captured packet by test_shadow_original_reference); set
   D_00814DC0, RGBAQ 0xFFFFFF80 and the silhouette triangles (PRIM 0x004);
   the frame's draw environment again, set (2,6) D_00815C60, CLAMP block
   D_008146C0 and the target's TEX0 0x5DC00A580; the receivers (0023C200,
   PRIM 0x07C, then 0023E8A0's for class 2, PRIM 0x07B); CLAMP block 1 at
   the end (001D1FF0(0, 1));
5. the chain page's primitives with the state they carry, and, as its
   last CALL (slot 0xFFF), 001DDE10's depth-of-field pass since step DOF
   (2026-10-09; CHAIN_PAGE.md section 6.2): the field declared to the
   model (em_gs_world_field_declare: bank A's FRAME_1 / SCISSOR_1 of the
   kicked slot, checked against the head at the kick), then four times the
   copy of the field into the 256x256 buffer at D_0027568C with its
   environment (FRAME_1 / ZBUF_1 / XYOFFSET_1 / SCISSOR_1 / PRMODECONT /
   DTHE / TEXA), the draw environment again (the kick's packet, OP_ENV:
   001D1F20's REF), the blend back over the field under its Z test, and
   the draw environment again at the end.
A GPU draw in a world frame (`em_gfx_draw_skinned`) is a fault: nothing
in the first level's world frames takes that path (the level smoke through
Roger passes with the GS frame on).

Each primitive becomes its register writes (state, PRIM, then per vertex
RGBAQ with Q, ST, UV for FST 1, XYZF2 or XYZ2). A state register is
recorded only when it changes (a same-value write never ends a span,
section 3.7, and reloads no different CLUT, 4.6); a state block or the
environment again resets that cache. Strips are recorded triangle by
triangle (PRIM before each), which draws the same triangles with the same
span (3.7: the span ends on attribute or class changes, not on a
same-value PRIM). No transfer is recorded: a transfer register or an IMAGE
packet in a recorded frame faults.

**The kick.** At step V, after 001D2300, `em_rcl_kick_head` reads the
kicked list's first two tags, REFs of the slot's draw environment (GS block
+ 0x20 + 0x190 x slot, 0x19 qwords) and of the clear (+0x3A0, Z only; +0x420
with render flag 3), checks each REF's address and size against the slot
(context +0x9C, whose list D_0028F700 + (slot << 14) is the one kicked) and
fails with the named reason otherwise, and passes their GIF data (the VIF FLUSH / NOP codes
dropped, DIRECT data kept). The model runs that head (FRAME_1 with the
field's buffer, ZBUF_1, XYOFFSET_1 with 001D2300's half line, SCISSOR_1,
PRMODECONT, COLCLAMP, DTHE, TEST, the Z clear), then the recorded body.
The field is the buffer the head's FRAME_1 names, SCISSOR_1's 224 rows.

**The movie frame.** When the blocking movie 00203350 takes the main
iteration (001AAE40 with D_00821058 == 1: the departure movie E001.PSS at
the level exit), the world list the game task built before it is never
kicked: after the movie returns, 001D1C10 runs 001D1AE0, which restarts the
frame's list, and sets render flag 4 before 001D2300 kicks it. em_frame
therefore drops the recorded world draws when the movie suspends the
iteration (`em_gfx_gs_world_drop`); the movie's presentation frames and the
kick after it show no field (as on the GPU path, which draws no world
there). The movie's own GS memory writes are not modelled (the port
presents its frames with the GPU); the next world frame's head clears and
redraws the field.

**Workers and order.** The model runs on worker threads, each an EmGs with
its own registers and span over the one local memory, each drawing the
rows (y >> 2) % n == k of whatever buffer it draws (EmGs.band_*; triangles
and sprites skip other rows, lines and points do not plot them). Every
worker takes every write, so their registers, spans, CLUTs and counters
stay equal; the bands partition the pixels, so they write exactly what one
EmGs writes (tools/test_gs_raster_reference.py part F: every capture packet
in lockstep bands; `make test-gs-world`: 1, 2, 3 and 8 workers draw the
same fields and memory, under the thread sanitizer). Where a primitive
reads a buffer drawn earlier in the frame, or a draw goes into a buffer read
earlier in the frame (the shadow's target, between its passes), the
recording inserts a barrier (a buffer is marked as drawn once per stretch
between barriers: marking it again changes nothing, so a list whose every
primitive carries FRAME_1 and SCISSOR_1, the load veil's, costs one mark): every worker reaches it before any goes on;
it must fall where the span has already ended (a worker with queued
primitives there faults, so a barrier never moves a span boundary). The
worker count is EM_GS_THREADS, else the host's processors less three, at
most 8: a host property, the bytes are the same for any count. A texture's
reads are ordered by the drawn blocks it covers whatever their residency
(since 2026-10-10: `texture_entry` lists every drawn block, and a buffer
drawn over uploaded blocks for the first time makes the texture checks
run again, `mark_drawn`). Before, it listed only blocks no upload wrote,
which is enough while no upload writes a buffer the frames draw: in the
first level none does (the library image, AREA11's area-load uploads and
the player texture packet, 5,504 blocks, hold no block of either field,
the Z buffer or the 256 x 256 copy and shadow buffer), but a frame drawn
over uploaded memory raced (found by `test_dof_pass_reference` part C;
`make test-gs-world` now also draws its frames with every block uploaded).

**Pipelining.** As the GS draws a kicked list while the EE builds the next
one, the kick hands the frame to the workers and returns; the next kick
(or the next frame that shows no field) waits for it. The backend ends the
frame's GPU command buffer with the field's quad and the overlay pass but
commits it only then, after putting the field into the frame's texture
(three textures rotate, each reused only after the GPU finished with it).
A frame is therefore shown one tick after its kick, which is when the
PS2's display shows the buffer drawn from the list kicked one tick before
(decomp CAPTURES_C7.md 5b: the list built in s0 -> s1 is drawn in s1 -> s2
and displayed at s2); the game logic does not see the difference. A fault
of the frame (a refused primitive, a span fault, a malformed packet)
surfaces at that wait and stops the game (fail-stop).

**Strict mode, residency.** The workers run strict (section 6): a refusal
or a span fault faults the frame. Every textured primitive's TEX0 (the
texels CLAMP_1 lets it reach, and its CLUT) must lie in blocks an upload
wrote (the GS memory image) or in a buffer the model drew (the fields, the
shadow target, the pass's copy buffer; the field the frame draws once it
is declared); anything else faults. The recorder must know that TEX0 (of
the primitive's context, PRIM CTXT): written in the frame by a recorded
write or by a recorded state block's A+D / PACKED data (decoded, as are its
CLAMP and PRIM; a state block's own textured vertex kicks are checked the
same way). At the frame's start, after the environment again and after a
TEX2 write it is unknown, and a textured primitive then faults; an unknown
CLAMP is taken as REPEAT, the widest read.

**GS memory: the uploads.** The model samples textures where the
original's uploads put them (DISC_TEXTURES.md section 2):
- the area load's uploads reach it as the original sends them: the
  loader's area consumer (em_scene_bindings.c `loader_area_chain`), which
  accepts exactly 001FF590(0xAB, 1)'s A entry at 001FFCD0 state 4,
  00200890's player texture packet at state 7 and (since the merge,
  2026-10-08) 001FF590(0xAC, 1)'s A entry at state 8 (AREA01 sub 0's room
  texture upload at the level exit), hands the delivered buffer to
  `em_gfx_gs_upload` -> `em_gs_world_upload_chain`: the VIF1 source chain
  (CNT tags to a RET or END), its DIRECT GIF data (A+D writes of the
  transfer registers and host-to-local PSMCT32 IMAGE data; anything else
  faults, the rule of tools/export_disc_textures_gs.py) run through a model
  of their own over the local memory, after the workers have drawn the
  frame before; the blocks they write become resident;
- the library module 0x1B, which the boot's 001FF1E0(0x1B) and the New
  Game's 001AD1A0 00200830(D_0028A564) upload and the port does not run
  (MODULE_LOADER.md section 5), comes from `assets/gs_library.emgm`
  (EMGM v1, the runs of uploaded 256-byte blocks; `tools/export_gs_memory.py`:
  FirstLevel.library() then library_slot() of the disc model, 1,920
  blocks), installed at start-up.
`make test-gs-memory-reference` (tools/test_gs_memory_reference.py): the
image equals the disc model's library, and AREA11's two area-load buffers
through the C model leave all 5,504 blocks the disc model's uploads write
(library, area, player packet) byte-identical and exactly those resident;
the C walk's refusals. Live, the first-control field drawn this way equals
the one drawn from a whole-world image byte for byte. The status pages'
uploads go to the status runtime's GS data, not to this memory: they draw
with the GPU, and the original restores the regions they overwrite
(00200970) before the next world frame.

**Presentation.** The platform layer only shows the field:
`em_gfx_field_presentation` (em_gfx.h) is the hook, and its one mode is the
user's choice of 2026-10-09, EM_GFX_FIELD_INTERLACED (each field
line-doubled at its interlaced height, the picture placed by the display
registers of main-loop step U): section 11. The 2D overlay pass (message
glyphs, the letterbox bars, the screen fade, the status overlay) is drawn
by the GPU over the presented field, moved with the placed picture; it is
not part of the GS frame (its draws are not GS packets in the port).

**Backends.** Only the Metal backend presents the field
(`em_gfx_gs_world_enable`); the d3d12 and Vulkan backends refuse it, so on
them the game does not start in the Original profile (the default) unless
EM_GPU_RENDERER=1 selects the GPU renderer. The Original profile currently
needs the Metal backend (macOS); the field presentation on the other
backends is not built.

**The depth-of-field pass and the workers.** The pass reads the field the
body is drawing (each copy) and the buffer it has just drawn (each blend),
and draws the field again after reading it. The recorder orders the
workers by the buffers it knows are drawn and read: the declared field
counts as drawn from its declaration (the body draws it throughout) and
again at each environment again, the copy buffer from its FRAME_1, so a
barrier falls before the first copy (every band of the field drawn), at
the first environment again after each copy (every band of the copy drawn
before any blend reads it) and at each next copy's FRAME_1 (every band's
blend done before the copy buffer is drawn again): 8 barriers a frame, each
after a FRAME_1 change has ended the span. The copy buffer is the drop
shadow's 128x128 target memory too, so in a frame with the shadow the
first copy's barrier also waits for the receivers' reads of it. `make
test-gs-world` draws the pass with 1, 2, 3 and 8 workers to the same bytes;
without the declaration's barrier the thread sanitizer reports the race.
The backend hands the walked pass to `em_gs_world_page_pass` (the field
declared, each primitive with its environment, the kick's environment at
each mark), which `test_dof_pass_reference` part C drives with the
original's captured memory: 1, 2, 3 and 8 workers leave every word equal to
the original's after the pass.

**Not part of the GS frame.** The status frames (the hub, the pages; their
models use the GPU's skinned path) and the tear-down frames draw with the
GPU as before. (001DDE10's depth-of-field pass, walked over until step DOF,
is drawn since: above.)

**Frame cost.** Section 10.2.

## 10. The fb2 reference frames and the pixel harness

The decomp's 19 software-renderer frame points (`build/s87/c7cap/fb2/<point>/`,
CAPTURES_C7.md 5b: the 16 route snapshots `00_panel_no_battery` ..
`15_level_exit`, `first_control`, `route03_end`, `route07_end`) each hold
`displayed.bin`, `draw.bin` and `z.bin` two frames after the recorded
state. `tools/test_fb2_pixels.py` (`make test-fb2-pixels`, 2026-09-28)
compares the port's Original-profile frame with `displayed.bin` per pixel.
Since chain step GSFRAME (2026-10-03) that frame is the GS model's field
(10.1); the description below of the Metal frame and its sampling is the
GPU renderer's (`EM_GPU_RENDERER=1`), which the harness still measures.

**Like with like** (the tool's docstring has the full statement):
- **Which original field.** At a point the outputs come from loop top s2;
  the displayed buffer is the one FRAME named at loop top s1 (checked from
  meta.json at every point), drawn with s1's XYOFFSET (OFY 1936.0 or
  1936.5), and it shows the game state of route row s1.
- **Which port frame.** The level smoke's own alignment
  (tools/test_level_smoke.py) gives the port tick whose post-task state is
  the snapshot's row (`rec`); the frame compared is that of tick
  i + (s1 − rec) (for first control, idx + (s1 − 4085), route 01 f0 = slot
  04). The port writes it through `EM_FB_CAPTURE_TICKS` or
  `EM_LEVEL_SMOKE_FB_CAPTURE` (em_scene_bindings.h); the tool checks the
  captured tick is the aligned one, and reports whether the camera eye /
  target, the player's position / heading and the task bytes equal row s1
  bit for bit ("camera exact").
- **What the port frame is.** Metal at the headless target (the 960x720
  window times the backing scale: 1920x1440 here), the 4:3 game rect, one
  frame per scene tick after the task. The projection maps the rect's width
  to the field's 512 pixels and its height to 224 lines at OFY 1936.0.
- **Sampling.** GS pixel (x, y) is the point (x, y + OFY − 1936) in field
  units (the GS samples at the integer point, 3.1); the tool takes the
  Metal pixel whose centre is nearest (column floor(x * 1920 / 512), row
  floor(y' * 1440 / 224)). No filter, no search, no tolerance. The sampled
  Metal pixel was itself rasterized up to 0.13 pixel / 0.08 line from the
  GS sample point; that is the sampling's own error and is not
  compensated.
- **RGB only** (PMODE shows circuit 2; no alpha reaches the display).
- **Metrics.** Exact-match fraction (all three channels equal), mean
  absolute channel error, maximum channel error, percentiles of the
  per-pixel maximum error; the sampled port field, the original field and a
  difference image go to build/fb2_pixels/<point>/.

**Which points are compared.** The quick run (about 17 s) compares
`first_control` from a smoke run to first control; `EM_TEST_FULL=1` (about
6 min) adds two route runs through the AREA01 arrival (pass 1 aligns, pass
2 captures and is re-aligned on its own log; the aligned ticks and their
post-task state must be identical, so a non-deterministic run fails) and
compares every snapshot the smoke aligns: 08, 10, 11, 12, 13, 14 and, since
step DRAWN (2026-10-04), 15 (the AREA01 arrival: route 15's last row f801
is the end of the smoke's a01_arrival phase; the capture session then ran
neutral frames, as the port's idle does, lengthened by two ticks with
`EM_A01_ARRIVAL_TICKS`). `--point 15_level_exit` (`make
test-fb2-pixels-area01`, about 2.5 min) compares that point alone. Where
route 15's trace has no row at s1 (11 and 15) the camera / player / task
comparison uses the capture session's own row (`capture.json`). The camera
is exact at 10, 14 (the smoke's VIEW_EXACT) and 15; elsewhere it follows
the navigation (08, 11, 12, 13) or the opening's earlier end at host speed
(first_control: the eye still rising, census L33). Not compared, with the
reason the tool prints: 00 and 09 (side beats, no snapshot alignment),
01..07 (the smoke aligns their next beats on scans and windows, not on the
snapshots), route03_end and route07_end (repeats). Nothing of the snow or the flame can match: their
sprites follow the port's rand() stream (RAND_ORDER.md).

**Numbers** (exact pixels of 114,688; mean absolute channel error; maximum
channel error). "Before" is port HEAD e654b42 plus the capture hooks;
"after" is this step (the 8.7 fog weight on every integer fog site, the
skinned path's fog on the 8-bit colour, nearest glyph sampling, the per-tag
Q). Claims are relative to PCSX2's software GS.

| point | camera | before | after |
|---|---|---|---|
| 10_cage_roof_roger | exact | 32,834 (28.63 %); 1.39; 213 | 35,430 (30.89 %); 1.39; 213 |
| 14_roger_encounter | exact | 28,532 (24.88 %); 1.81; 98 | 33,022 (28.79 %); 1.78; 99 |
| 13_east_tower | not exact (eye, target, position) | 33,956 (29.61 %); 2.92; 80 | 40,544 (35.35 %); 2.92; 80 |
| 11_crevice_prompt | no row at s1 | 7,811 (6.81 %); 5.76; 143 | 9,173 (8.00 %); 5.76; 143 |
| 08_truck_crossing | not exact | 3,087 (2.69 %); 7.78; 103 | 3,201 (2.79 %); 7.78; 103 |
| 12_crevice_jump | not exact | 2,754 (2.40 %); 6.76; 124 | 2,852 (2.49 %); 6.74; 124 |
| first_control | not exact (eye) | 1,032 (0.90 %); 18.90; 227 | 985 (0.86 %); 18.94; 227 |

15_level_exit (AREA01's arrival, step DRAWN 2026-10-04, port with the
same renderer): camera exact; 54,378 (47.41 %); 0.67; 240; per-pixel
maximum error p50 / p90 / p99 1 / 1 / 11. Its difference image is the same
±1 floor over every surface (no snow or flame in this frame).

At 10 and 14 the per-pixel maximum error is 1 at the median, 3 / 4 at the
90th percentile and 23 / 29 at the 99th (after). The difference images show
the geometry in place; the large errors are the snow and the flame (rand()),
the fans' phase at 14 (FIRST_LEVEL_AUDIT.md 1b), edges (Metal's coverage),
and the rest is a ±1 floor spread over every surface: each channel alone
is exact on about half of the pixels. The fog step moved only part of that
floor. A measurement made for the next step (not applied, not in the
contract): the texture function at the 8.7 colour of 5.1 in the object /
static-world shader alone takes 10 to 41,507 (36.19 %) and 14 to 42,703
(37.23 %). The rest of the floor is Metal's float interpolation at its own
sample points instead of the DDA (3.2), and Metal's rasterization.

The floors the tool asserts (FLOORS) are the "after" fractions rounded down
to 0.1 percentage point; a renderer change that gains pixels raises them in
the same commit. They are measured on this Mac's GPU; another GPU may
interpolate differently.

**001DDE10's depth-of-field pass** (CHAIN_PAGE.md section 6.2) is drawn
by the model since step DOF (2026-10-09; 10.1). The GPU renderer walks over
it (it has neither the field as GS memory nor its Z buffer), so the Metal
numbers above are without it.

### 10.1 The GS field (chain step GSFRAME, 2026-10-03)

With the GS frame (section 9) a capture also writes the field itself
(`<capture>.gsfield`, 512 x 224 x 4 bytes) and the run log names its FRAME_1
and XYOFFSET_1. The original's displayed buffer holds the whole list, its
letterbox bars, transition fade and message glyphs included; the port
draws those as the GPU's 2D overlay pass over the presented field. So the
number compared is the **presented frame** read back at the centre of each
field pixel's place in the picture (the presentation of section 11 shows
field pixel (x, y) over the lines 2y + line .. 2y + line + 1 of the 448,
line 1 for a half-line OFY, moved by the screen position; the harness reads
the centre of that span from the capture's `<capture>.present`; with no
overlay the presented and the bare field are equal word for word, as at
first control), and the field alone is reported beside it. No sampling, no
tolerance.

**The frame loop's phase.** At 5 of the 7 points the port drew the field
in the other buffer with the other half line: first control, 08, 10, 11
and 12 (port FBP 0x38 / 0 with OFY 1936.5 / 1936.0 where the original's is
the opposite; the field is drawn half a line off). At 13 and 14 it is the
original's. The port's D_00810E80 has the other parity there; the cause is
not traced (the loads taking another number of main-loop iterations at host
speed is a candidate: the New Game's veil runs 55 ticks against the PS2's
258). It is reported per point by the harness.
The original's own phase is not a fact of the game either (fork-side
measurement, 2026-10-09, FIRST_LEVEL_AUDIT.md 1b F1): in a capture run it
follows the vsync the tool's START lands on and the AREA11 load's
iteration count, which differs between PCSX2 versions, so these points
are re-measured against fork captures of the same field phase before the
port's parity counts as a defect.

Numbers, exact pixels of 114,688 (mean absolute channel error, per-pixel
p50 / p90 / p99). "Before" is the GPU renderer at the same port HEAD
(`EM_GPU_RENDERER=1`, Metal at 1920x1440 sampled at the original's OFY);
"after" is the presented GS field; "field alone" leaves out the overlay
pass. Claims are relative to PCSX2's software GS.

| point | camera | phase | before (Metal) | after (GS field, presented) | field alone |
|---|---|---|---|---|---|
| 14_roger_encounter | exact | same | 33,022 (28.79 %; 1.78; 1/4/29) | **99,985 (87.18 %; 0.97; 0/1/28)** | 46 (0.04 %) |
| 13_east_tower | not exact | same | 40,544 (35.35 %; 2.92; 1/8/19) | **64,137 (55.92 %; 2.60; 0/8/16)** | the same |
| 10_cage_roof_roger | exact | other | 35,430 (30.89 %; 1.39; 1/3/23) | 18,303 (15.96 %; 3.33; 2/10/32) | 12,642 (11.02 %) |
| 11_crevice_prompt | not exact (the capture's row) | other | 9,173 (8.00 %; 5.76) | 6,281 (5.48 %; 6.66) | the same |
| 08_truck_crossing | not exact | other | 3,201 (2.79 %; 7.78) | 3,602 (3.14 %; 7.31) | the same |
| 12_crevice_jump | not exact | other | 2,852 (2.49 %; 6.74) | 2,827 (2.46 %; 6.84) | the same |
| first_control | not exact (eye) | other | 985 (0.86 %; 18.94) | 1,125 (0.98 %; 18.84) | the same |
| 15_level_exit (merge, 2026-10-08) | exact | same | 54,378 (47.41 %; 0.67) | **113,053 (98.57 %; 0.95; 0/0/9)** | 0 (0.00 %; 15.92) |

At 14 (camera exact, same phase) the field alone is +3 on 62 % of its
pixels and darker at the top and bottom 32 lines: the original's buffer
holds the transition fade and the letterbox bars, which the presented
frame then applies too. What remains at 14 (14,703 pixels): the fan
blades (7,727 pixels in their box: the fans' phase follows the
recording's timing, FIRST_LEVEL_AUDIT.md 1b), the snow (the port's rand()
stream, RAND_ORDER.md), a few edges, and the sky grid's region in the top
left: 6,358 of its 11,200 pixels differ by 1..3 (some up to 41 where snow
falls), mean +0.9, ending exactly at the hill's silhouette, so in the
channel-3 background's triangles; not traced then (the background's STQ on
the vertex grid, open item 8.2, measured far smaller residues on designed
primitives; a cause in the grid's ST at the GS's precision is not
excluded). **Traced at step DOF (2026-10-09): it was 001DDE10's
depth-of-field pass**, which the model did not draw then (below). At 10
the half-line phase moves every edge and gradient by half a line. The
other points differ in their camera, their phase or both.
15_level_exit (the AREA01 arrival, measured at the GSFRAME merge with the
AREA01 steps on main): the original's buffer there holds the transition
fade over AREA01's world, which the port draws as the overlay pass over a
field that already holds the world, so the field alone matches nowhere
while the presented frame matches on 98.57 % of the pixels. That AREA01's
world itself draws in the field is shown by the level-2 phases (a01_00 ..
a01_02, a01_s0 / s3 / s4 / s6 PASS with the GS frame; a field captured at
a01_arrival + 420 shows the tunnel, the crates and the fire). The AREA11
numbers above were re-measured at the merge and are unchanged.
FLOORS in the tool are these "after" fractions rounded down to 0.1
percentage point (FLOORS_GPU keeps the Metal ones).

**Step DOF (2026-10-09): 001DDE10's depth-of-field pass drawn** (CHAIN_PAGE.md
section 6.2; section 9). Full mode, the same port runs and alignment as
above, the presented field (exact pixels of 114,688; mean channel error;
per-pixel p50 / p90 / p99); "before" is the port at the merge base
a345d9a, "after" this step; relative to v2.6.3's software GS (the fb2
frames hold the original's own pass):

| point | camera | phase | before | after | pixels the pass changed | of them now exact / no longer exact |
|---|---|---|---|---|---|---|
| 14_roger_encounter | exact | same | 99,985 (87.18 %; 0.97; 0/1/28) | **105,836 (92.28 %; 0.89; 0/0/28)** | 6,686 | 5,954 / 103 |
| 13_east_tower | not exact | same | 64,137 (55.92 %; 2.60; 0/8/16) | **71,609 (62.44 %; 2.35; 0/7/14)** | 18,772 | 7,707 / 235 |
| 10_cage_roof_roger | exact | other | 18,303 (15.96 %; 3.33; 2/10/32) | 21,490 (18.74 %; 3.18; 1/9/32) | 20,990 | 4,225 / 1,038 |
| 11_crevice_prompt | not exact | other | 6,281 (5.48 %; 6.66) | 6,330 (5.52 %; 6.59) | | |
| 08_truck_crossing | not exact | other | 3,602 (3.14 %; 7.31) | 3,605 (3.14 %; 7.31) | | |
| 12_crevice_jump | not exact | other | 2,827 (2.46 %; 6.84) | 2,827 (2.46 %; 6.84) | 0 | |
| first_control | not exact (eye) | other | 1,125 (0.98 %; 18.84) | 1,118 (0.97 %; 18.77) | | |
| 15_level_exit | exact | same | 113,053 (98.57 %; 0.95) | 113,053 (98.57 %; 0.95) | | |

At 14 the top-left region the GSFRAME text above left untraced (160 x 70
pixels, the sky above the hill): exact 5,347 -> 10,472, off by 1 2,897 ->
307, by 2 1,671 -> 78, by 3 794 -> 40, by more (most of it the snow,
rand()) 491 -> 303; its mean signed error +0.80 -> +0.05. 5,670 of the
5,954 pixels that became exact at 14 lie there: the soft far background
was the pass. At 12 the pass changed no pixel at that tick (the camera
looks down onto the snow field, no sky in view). These numbers judge the
whole field; FLOORS rose to the "after" fractions rounded down (10 0.187,
11 0.055, 13 0.624, 14 0.922). The pass's own pixels were compared with
the fork capture of it the next day (section 7, `test_dof_pass_reference`).

**The pass's conformance step (2026-10-10).** The row rule of 3.4 (a UV
sprite's row coordinate accumulated in binary32), which makes the pass
bit-exact on the 7 captured frames, changes these points as follows (full
mode, the same alignment; exact pixels of 114,688): 14 105,836 -> 105,973
(92.40 %; mean channel error 0.89; all 137 in the sky region above the
hill: 10,472 -> 10,609 of its 11,200 pixels exact, off by 1 307 -> 183, by
2 78 -> 65, by 3 40 -> 40, by more, the snow, 303 -> 303); 13 71,609 ->
71,794 (62.60 %); 10 21,490 -> 21,696 (18.92 %); 11 6,330 -> 6,325 (5.51 %);
first control 1,118 -> 1,117 (0.97 %); 08 3,605, 12 2,827 and 15 113,053
unchanged. At 10, 11 and first control the port draws the other field
phase and at 11 and first control the camera differs, so a pixel there can
move either way. FLOORS rose to 10 0.189, 13 0.625, 14 0.924.

### 10.2 Frame cost

The GS frame runs on worker threads while the main thread builds the next
tick (section 9, "Pipelining"). Measured on this M1 Pro (8 performance and
2 efficiency cores, 7 workers) on a quiet machine (load average 4.6 to 6.0),
EM_FRAME_TIMING over newgame-control, two runs at the final code and one
with EM_GPU_RENDERER=1 for comparison (2026-10-03, after the review fix):
- **The load veil's 54 list frames** (the New Game's veil, section 9): 0.20
  to 4.17 ms of wall time per tick (mean 1.95), main thread the same; the
  busiest worker 3.4 ms of CPU per frame (max 4.0). The GPU renderer: 0.76
  to 0.84 ms. Before the fix the recorder marked the veil's buffer once per
  primitive (each mark 1,792 address computations, then a scan of all
  16,384 blocks), which took 20.4 to 21.7 ms of main-thread CPU per veil
  tick, every one over the period (the veil ran at about 41 Hz); the first
  GSFRAME measurement below left the veil ticks out.
- **The in-level ticks before first control** (1,395 after the first): wall
  5.3 to 13.0 ms (mean 8.0 / 8.1, p99 11.8 / 12.1), none over the period;
  the busiest worker 6.4 / 6.6 ms of CPU per frame (p95 7.8 / 8.8).
- **Ticks over the period, both renderers:** the start-up step (110 to 123
  ms), one host step of the title / menu path (17 to 33 ms of wall time at
  under 6 ms of CPU), the step of the veil that runs the area load (75 ms;
  GPU 87 ms; the loader at host speed) and the first two in-level ticks
  (GS 25.3 / 25.9 and 17.4 / 18.8 ms; GPU 23.6 and 26.5 ms: the level's
  first ticks' tasks, which vary from run to run on both renderers; the
  first GS frame's worker time is 16.9 to 17.1 ms, its texture residency
  checks about 2.6 ms of main-thread time). None of these comes from the
  GS frame alone; every other tick is under the 16.68 ms period.
- **Re-measured at the merge onto main (2026-10-08, load average 5 to 7,
  the same method):** two runs, then one with EM_GPU_RENDERER=1. The veil
  ticks 0.18 to 6.16 ms (mean 1.0 / 1.2); the last 1,300 ticks (in level)
  wall mean 8.14 / 8.15 ms, p99 12.08, max 12.90 (GPU 8.21, p99 11.60); the
  busiest worker mean 6.4 ms (p95 8.0). Over the period: the start-up, the
  area-load step (112 / 115 ms; GPU 109) and the first two or three
  in-level ticks (17.5 to 19.4 ms; GPU 26.9 / 17.3), plus once a title
  host step (16.8 ms); 4 to 6 of 2,997 steps, as on the GPU (5 of 2,996).
- **The wall time depends on free cores for the workers** (review 2):
  under heavier load (load average about 10, newgame-control) 78 of 2,992
  steps were over the period on wall time while the main thread used 7.5
  to 12 ms of CPU on those in-level ticks and the busiest worker about 7.4
  ms on average (22.6 ms at most); the main-thread CPU was over on only 3
  steps (the start-up, the area-load step at 71.6 ms, the first in-level
  tick at 18.7 ms). The quiet-machine numbers above are the ones the
  period is judged by.
- **AREA01 (level 2), measured at the merge:** the level smoke's binary
  run to a01_00 without its logs (EM_STARTUP_TEST=newgame-level,
  EM_LEVEL_SMOKE_UNTIL=a01_00, the prepared pads), the last 840 ticks: the
  main thread takes 13.6 ms of CPU per tick on both renderers (AREA01's
  own tick, not the GS frame); with the GS frame the wall mean is 14.23 ms
  (p99 19.2, max 20.9) and 65 of the 840 ticks are over the period, the
  busiest worker 10.2 ms on average (17.7 at most); with the GPU renderer
  13.58 ms and none over. AREA01's main-thread cost leaves the workers
  less room; it is level-2 work (not in this step's first-level contract).
- Slower hosts are not measured.
- **Step DOF (2026-10-09): 001DDE10's depth-of-field pass drawn.** The pass
  is four copies of the field into 256x256 (65,536 pixels each) and four
  full-field blends (114,688 each): 720,896 bilinear pixel evaluations a
  frame, and 8 worker barriers. Offline (one EmGs, the pass's register
  writes over a random field and Z buffer, best of 30): 24.5 ms a pass,
  about 34 ns a pixel. A scratch variant of the model with the sprite's
  per-column U precomputed draws the same bytes in 22.9 ms; on that
  variant nearest sampling takes 13.9 ms and an untextured pass 10.6 ms,
  so the bilinear sampling (four texel address computations and fetches)
  is about 54 % of the cost and the rest of the pixel path (the frame and
  Z addresses and reads, the tests, the blend) 46 %. Live,
  EM_FRAME_TIMING over newgame-control, two runs of the merge
  base (a345d9a) and two of this step interleaved, the last 1,300 ticks
  (in level), on a **heavily loaded** machine (load average 10 to 16: a
  PCSX2 capture and other jobs ran beside it; no quiet machine was
  available in this step): the busiest worker's CPU per frame 7.59 / 8.02
  ms -> 11.47 / 11.43 ms (+3.9 ms, +50 %; p95 9.83 / 10.52 -> 13.98 /
  14.33); the GS frame's wall time 16.78 / 19.25 -> 19.35 / 22.24 ms; the
  tick's wall time 8.90 / 13.24 -> 16.91 / 18.07 ms, with 56 / 340 -> 740 /
  652 of the 1,300 ticks over the period; the main thread's CPU unchanged
  (8.4 to 8.7 ms). Under that load the frame no longer fits the period;
  from the quiet-machine figures above (the busiest worker 6.4 ms) and the
  +3.9 ms the pass adds, a quiet M1 Pro should stay under it (about 10 to
  11 ms), but that is an estimate, to be re-measured on a quiet machine.
  The GPU renderer (EM_GPU_RENDERER=1, which walks over the pass) is
  unchanged. A cheaper exact path for large UV sprites (the texel, frame
  and Z addresses split into per-column and per-row parts; the page / block
  / column tables of PSMCT32 and PSMZ32 are separable) is the obvious next
  saving; precomputing the sprite's per-column U alone saves 6 % of the
  pass (above). Neither is applied in this step.

The first GSFRAME measurement (under the other tracks' load, 44 to 57)
gave a tick wall time of 14.32 to 23.28 ms on average over the in-level
ticks with the workers short of cores; the main thread's CPU per in-level
tick was 8.4 to 8.6 ms (chain C8b measured 7.6 ms for the game on a quiet
machine). Offline (the first-control frame replayed, thread CPU time):
one worker 30.1 ms; with 8 bands the busiest band 6.19 ms (the sum 42.6:
each band takes every write and sets up the triangles its rows meet).

## 11. Presentation: the field and the picture position (2026-10-09)

The user's decisions of 2026-10-09 (LAUNCHER_OPTIONS.md, both (a)): each
512x224 field is line-doubled and placed at its interlaced height, and the
options screen's SCREEN ADJUST moves the 4:3 picture inside the window the
way the original's code sends it to the display. Both are platform-layer
presentation after the GS field: the game code and the field's bytes are
the same with any choice. Nearest neighbour only; no smoothing, no CRT or
scanline simulation; 4:3.

**What the original's code does (the inputs).**
- *The field's half line.* Step S hands 001015A8 / 00101810 the half
  offset 1 - D_00810E88, so every other field is drawn with OFY 1936.5
  instead of 1936.0 (the D_00810E88 = 0 field, FBP 0x38; measured in the
  port's run for all 96 ticks of the presentation preview, and in the
  original at 52 of 57 loop tops, decomp CAPTURES_C7.md 5b). Its rows
  sample the scene half a field row lower.
- *The display registers.* Step R: 001AB4E0(0x70003B94, 0x70003B96) builds
  both display environments D_00810EA0 / D_00810EC8 with the SDK's
  001002E0(env, psm 0, 512, 224, x, 2y) and sets their FBP (0 / 0x38).
  001002E0 makes PMODE 0x66 (circuit 2 only), SMODE2 3 (interlaced, field
  mode), DISPFB, DISPLAY = DX 636 + 5x, DY 50 + 2y, MAGH 4, DW 2559, DH 447,
  and BGCOLOR 0. Step U: 00100550 puts the environment D_00810E80 selects
  into the GS privileged registers (circuit 2's, since the GS revision
  halfword D_00241016 is not 1). At offset 0, 0 these are exactly the
  registers measured in PCSX2 at all 19 points (CAPTURES_C7.md 5b), and the
  route captures' D_00810EA0..EF equal the native step R byte for byte.
- *Register meaning.* DX counts MAGH + 1 = 5 clocks per framebuffer pixel,
  DY counts lines of the 448 (the SDK uses 50 for interlaced against 25
  for non-interlaced at the same spot); DX / DY say where the picture
  starts. So the offset x moves the picture x pixels right, y moves it 2y
  lines (y field rows) down. **This direction is inferred from the code and
  the register semantics; no recording shows a picture at a nonzero offset,
  so it was not observed on a screen.** The OPTIONS recording shows Left
  raising x and Up raising y (OPTIONS.md), so Left moves the picture right
  and Up moves it down. Outside the picture the display shows BGCOLOR
  (0 at all 19 points).

**The port.** `src/game/em_sdk_display_original.{h,c}` translates 001002E0
and 00100550 (the latter byte-matched C, the former a NEARMISS read with
its listing); `src/game/em_display_env_live.{h,c}` owns D_00810EA0..EF and
binds steps R and U in the frame loop (em_frame.c, after P and before V:
the existing em_slg_001AB4E0 with em_sdk_001002E0 as its worker over the
render context's D_00241010, then em_sdk_00100550 whose stores go to
`em_gfx_gs_display_store`). The render context's D_00241010 is the ELF's
.data (GS revision 3 where the boot stored 0x1B); 001002E0 does not read the
revision and 00100550 only tests it against 1, so the stores are the same.
`src/gs/em_gs_display.{h,c}` is the mapping (pure C), the Metal presenter
(`f_gsfield`, shared by macOS and iOS) computes it per pixel:

- the field's line: 0 when the XYOFFSET_1 that drew it has a whole OFY, 1
  when a half (any other fraction faults). A world frame's is step V's
  draw environment; a list frame's (the load veil) is that of the last
  primitive drawn into the displayed buffer (a list that draws nothing into
  it faults). Decided per field from that state, never from a counter. The
  presenter reads it when the frame is handed to the GS model
  (`em_gs_world_handed_xyoffset`, the kick's or the list's job, known
  before the model draws it), so the field and its overlay pass are placed
  in the same frame; when the field has been drawn, `gsw_complete` checks
  that its XYOFFSET_1 is the one it was placed with (a mismatch faults);
- the picture point (px, py) in pixels of the 512 and lines of the 448 of
  the game rectangle; x = px - (DX - 636) / (MAGH + 1), y = py - (DY - 50)
  - line; BGCOLOR where (x, y) is outside 512 x 448, else field texel
  (floor(x), floor(y) / 2). So a half-line field shows one line lower (its
  first line BGCOLOR, its last row's second line cropped), the picture
  moves by the offset, the uncovered edge is BGCOLOR and the far edge is
  cropped by the game rectangle;
- the registers must be the measured configuration (PMODE circuit 2 alone,
  SMODE2 interlaced field mode, MAGH 4, MAGV 0, DW 2559, DH 447); anything
  else faults (fail-stop), as does presenting a field before the first
  step U.

**Frames the GS model does not draw.** The 2D overlay pass (glyphs,
letterbox bars, fades) is drawn by the GPU after the field. On a field
frame its viewport is the game rectangle moved by the same shift as the
field, the field's line included (`em_gs_display_viewport`; the scissor
stays the rectangle): its 448 canvas lines fall on the picture's lines as
that field's rows are shown, canvas lines 2r and 2r + 1 on field row r in
both parities. The original's GS draws the bars and text into the field
through the same XYOFFSET_1, in whole field rows, so on screen they move
with the field; the port's bars (001AE900's first and last 32 of 224
rows, canvas 0..64 and 384..448) cover exactly those 64 field rows on
both parities. Until the overlay-line fix (2026-10-09) the pass took the
shift alone: on a half-line field it sat one display line above the
picture, covering 65 rows with rows 31 and 191 half band, half picture
(found by the decomp's video comparison, its VIDEO_COMPARE.md). The pass
is still the GPU's (host-resolution glyphs, not the GS's pixels): drawing
it through the GS model is FIRST_LEVEL_AUDIT.md 1b item 2. The
frames the GPU draws whole (the status pages, the options screen) take the
placement at begin_frame from the registers the previous iteration's step U
stored, with BGCOLOR over the rectangle first: in the one frame after the
offset changes they show the old position (a field frame shows the new one
at once). While the blocking movie 00203350 holds the iteration (a boundary
the port replaces with its own movie presentation) steps R and U do not
run and the original's movie driver owns the display. From the decomp C
(00203350 byte-matched; 00205050 a NEARMISS whose two display calls and
their constant arguments were checked against its original instructions):
00203350 calls 00205050 on the movie's environment D_007A55C0, which builds
it with 001002E0(env, psm 0, 512, 224, x 0, y 0), sets DISPFB's DBX / DBY to
0 and FBW to 8, and stores it with 00100550. Its offset is 0, not the scene
state's 0x70003B94 / 0x70003B96, so during the movie the original shows the
picture at the default DX 636 / DY 50 (MAGH 4, DW 2559, DH 447 in the same
SDK mode) with BGCOLOR 0 (001002E0 writes 0 to the BGCOLOR word), whatever
SCREEN ADJUST holds. The movie's per-field pump 00206030 then only flips
DISPFB's FBP between the environment's two pages (00205700, which stores the
same environment again), so DISPLAY and BGCOLOR stay as 00205050 set them.
The port matches this: it drops the stored registers
(`em_gfx_gs_display_release`), so the movie frames are drawn over the game
rectangle unplaced, which is the default position (shift 0), until step U
stores again. Measured on captures (`tools/check_present_capture.py`,
below).

**What it looks like** (the presentation preview of 2026-10-09, port
`build/presentation_preview/README.txt`): the picture as a whole stays put
and motion is smooth; each field carries every other line's detail, so a
horizontal edge still flickers by one line each field, centred on where it
is. Only the whole-picture bob of plain line-doubling is gone. Nothing here
was compared with real hardware.

**Verification.**
- `make test-gs-display` (tests/gs_display_test.c, ASan / UBSan): the
  registers from the SDK translations at the offsets 0 and +-20 per axis,
  both field parities (the shifted line, the background line), the shift
  on each axis (uncovered edge, crop), the 1920 x 1440 pixel-centre sampling
  (every field row and column in order), BGCOLOR's bytes and the refusals;
  the overlay viewport over both parities at offsets (0, 0), (0, 20) and
  (-20, -20) in a 1920 x 1440 and an offset 1013 x 759.75 game rectangle:
  the letterbox bands and a text strip, rasterized at pixel centres on the
  rasterizer's 1/256-pixel grid, cover exactly their whole field rows (32 +
  32 for the bands where the rectangle shows them), and the viewport
  without the line splits rows 31 and 191 of a half-line field.
- `make test-display-env-reference` (tools/test_display_env_reference.py):
  the original 001002E0 (900 quick cases over 13 SDK mode sets: written,
  zero-width traps, message-printer exits) and 00100550 (both circuits)
  against the translations, every conditional branch both ways; the
  original 001AB4E0 + 001002E0 + 00100550 against the live chain at the
  offsets 0, +-20, (2, 3) and random halfwords over a route capture (its
  captured environments equal the native ones), with the ELF's D_00241010
  too; `EM_TEST_FULL=1` (2026-10-09): 7,306 001002E0 cases, 1,000
  00100550 cases, 47 offsets over all 16 route captures, each capture's
  environments equal to the native ones. `make test-startup-load-gaps-reference` still runs 001AB4E0 itself.
- `tools/check_present_capture.py [--overlay-ok] [--bands=N] <capture.bmp>`:
  a captured field frame against its own field under the placement, every
  pixel (the capture's `<capture>.present` holds the field's XYOFFSET_1,
  the shader's constants and, since the overlay-line fix, the overlay
  pass's viewport, which must carry the field's line); per field row how
  much of each footprint the overlay drew (band rows, split rows);
  `--bands=N` requires N band rows at the top and at the bottom and no
  split row at the band edges. 2026-10-09, after the fix (ignored
  `build/overlay_line/`): the opening's letterbox frames at level smoke
  ticks 406 (OFY 1936.0) / 429 (1936.5) and subtitle frames 544 / 567 all
  pass `--overlay-ok --bands=32` (32 + 32 band rows, no split row; the
  overlay viewport 0 / 3.2143 drawable rows down at 1920 x 1440), the
  subtitle on field rows 194..216 on both parities. 2026-10-09 (ignored `build/present_check/`): ticks 1535 / 1536
  of the level smoke (a still moment; OFY 1936.5 / 1936.0), all 2,764,800
  pixels exact, the half-line field's first three rows BGCOLOR; ticks 400 /
  401 (the opening, letterbox bars) exact outside the bars' rows, the bars
  in place; with a scratch-only forced offset of +-20, +-20 (not in the
  source) the field and the bars moved 75 columns and 129 rows, the
  uncovered edges BGCOLOR. The OPTIONS side run opt_03 (the options screen,
  GPU-drawn) shows its picture 19 rows / 7 columns lower / right at the
  offset (2, 3) the recording kept.
- `make test-fb2-pixels` reads the presented frame at each field pixel's
  place (section 10.1). `EM_TEST_FULL=1` on 2026-10-09: all 8 compared
  points exactly the numbers of chain step ROUTE (first control 1,125
  exact, 14 87.18 %, 13 55.92 %, 15 98.57 %, ...), the half-line fields
  (first control, 14) included.

## 12. Files

- `src/gs/em_gs_raster.h`, `src/gs/em_gs_raster.c`: the model (no global
  state; one `EmGs` per context; row bands EmGs.band_*; the lockstep test
  hook EmGs.tee).
- `src/gs/em_gs_frame.h`, `src/gs/em_gs_frame.c`: the replay and read-back
  helpers.
- `src/gs/em_gs_world.h`, `src/gs/em_gs_world.c`: the Original profile's
  world frame (section 9): recording, residency, uploads, workers, field
  (and its declaration for 001DDE10's depth-of-field pass, which the chain
  page hands over through em_gfx_gs_page_pass -> em_gs_world_page_pass:
  CHAIN_PAGE.md section 6.2; the test hook em_gs_world_memory_restore).
- `src/game/em_gs_frame_live.{h,c}`: its game side (start-up, the kick).
- `src/gs/em_gs_display.{h,c}`: the presentation's mapping (section 11);
  `src/game/em_sdk_display_original.{h,c}` and
  `src/game/em_display_env_live.{h,c}`: steps R and U.
- `tests/gs_display_test.c` (`make test-gs-display`),
  `tools/test_display_env_reference.py` (`make test-display-env-reference`)
  and `tools/check_present_capture.py`: section 11's verification.
- `tools/export_gs_memory.py`: the library image `assets/gs_library.emgm`.
- `tools/test_gs_raster_reference.py` (`make test-gs-raster-reference`):
  the verification (section 7, part F the row bands).
- `tests/gs_world_test.c` (`make test-gs-world`): the workers draw what one
  worker draws (thread sanitizer), the depth-of-field pass included, also
  with every block of local memory uploaded.
- `tools/test_dof_pass_reference.py` with `tests/dof_pass_reference_bridge.c`
  (`make test-dof-pass-reference`): 001DDE10's depth-of-field pass against
  the original's captured GS memory (section 7).
- `tools/test_gs_memory_reference.py` (`make test-gs-memory-reference`):
  the GS memory against the disc model of the uploads.
- `tools/test_fb2_pixels.py` (`make test-fb2-pixels`): the fb2 pixel
  harness (section 10).
- Decomp capture tools (new, lane B16): `tools/gs_conformance_probe3.py`
  .. `probe8.py`. Their batches and rules are in decomp
  `docs/GS_CONFORMANCE.md` sections 8 and 9.
