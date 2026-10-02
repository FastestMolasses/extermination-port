# The chain page D_007635C0 drawn: DMA, VIF1, the three VU1 programs, GIF and the GS (WP-13)

Step "FXDRAW" of chain C7, 2026-09-26. Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

The effect barrel's ring lanes (001F0720), the head sprites and every puff
handler (001CFBE0), the pickup glint (001F0A60), the glow markers and
equipment sprites (001CD520) and the 0015BF90 drop-shadow decal (001CE300)
build their packets into the render context's chain table, page D_007635C0.
Since the effects and shadow steps they ran live and built those packets byte
for byte (EFFECT_MANAGER.md section 8, SHADOW_DECAL.md), but nothing drew them
except the decal, through a dedicated renderer entry. This step draws the
page as the original's DMA sends it to the GS: one generic consumer walks the
chain 001CB800 splices at the frame close, runs its VIF codes, the two VU1
programs it CALLs and its GIF packets, and hands the renderer every primitive
the GS would draw, in GS order.

Since step FLAMESNOW of chain C8b (2026-09-28) the page also carries the
AREA11 flame (owner 008235F0's 001D04B0) and the weather's channel-3 list
(001E55F0 / 001E67C0's 001CFFE0 tiles, CALLed by 001E0D70), and the consumer
runs a third program, the snow program D_00233800 (section 6.1).

Files:
- `src/game/em_vu1_page_programs.h`: the lane program (DMA packet
  D_00233290), the sprite program (table 0x231770) and the snow program
  (D_00233800), translated from their VU1 microcode (section 3).
- `src/game/em_chain_page.{h,c}`: the DMA / VIF1 / GIF / GS walk (section 2).
- `src/game/em_gs_blocks_original.{h,c}`: 001D0F20's blend-preset bank, the GS
  state the page REFs (section 4).
- `src/game/em_chain_page_live.{h,c}`: the live binding (section 7).
- `src/em_gfx.h` `em_gfx_gs_prims` / `em_gfx_gs_texture`, Metal in
  `src/gfx/metal/em_gfx_metal.m`: the GS pixel path (section 5).
- `tools/chain_page_model.py`: the original's side (DMA, VIF1, the ORIGINAL
  microcode on a VU1 machine, GIF, the GS vertex queue).
- `tools/test_chain_page_reference.py`, `tools/test_chain_page_gpu.py`,
  `tests/chain_page_test.c`, `tools/level_smoke_chain_page.py` (section 8).
- `tools/export_page_textures.py`; the program packets join
  `tools/export_effect_tables.py` (section 7).

## 1. What changed

| Before | Now |
|---|---|
| The page's packets reached no renderer (FXLIVE, SHADOW limitations); the decal's fans were read back by em_shadow_live and drawn by `em_gfx_shadow_decal_fan` | `em_chain_page_live_draw` walks the spliced page at 001D1EA0's kick position and `em_gfx_gs_prims` draws every primitive in GS order |
| The GS blend presets the page REFs were not in the port's memory (001D0F20 untranslated); the decal renderer hard-coded the mode-1 writes | 001D0F20's preset bank (`em_gs_blocks_001D0F20_presets`) runs at the render context's load, and the page's REFs read it |
| `tools/export_shadow_decal_texture.py` -> `shadow_decal.emdt` | `tools/export_page_textures.py` -> `page_textures.emot` (the decal's texture and the five others the page can draw) |

Retired (the original replacement is live in this step): `em_gfx_shadow_decal_fan`,
`em_gfx_shadow_decal_texture`, `EmGfxDecalVertex`, `em_shadow_live_flush_decal`
(em_shadow_live now checks the decal's packets and cross-checks the page's
decal triangles through `em_shadow_live_page_drew`), `tools/export_shadow_decal_texture.py`
and the Metal part of `tools/test_shadow_decal_reference.py` (its five pixel
cases and refusals moved to `tools/test_chain_page_gpu.py`, rule 4).

Census: no row changes status. The consumer's DMA / VIF / GIF / GS walk and
the VU1 microcode are the renderer boundary (no EE function of the census),
001D0F20 runs at boot before the first captured label, and every producer on
the page was already live; section 1.24 of FIRST_LEVEL_CENSUS.md records the
notes.

## 2. What the DMA sends (read from the original's data and code)

**The walk.** 001CB800 (PACKET_CHAIN.md section 2) writes a NEXT tag at
base = D_0028F700 + index * 0x70000 + 0x1F3EC0 + (a1 << 6) that links the
newest block of the first non-empty slot, each slot's head block links the
next slot's newest, and the last links base + 0x20; its a2 is the context
block, so base is stored at the context's +0. The DMAC walks that chain in
source-chain mode: CNT (data after the tag, then the next tag), NEXT (data
after the tag, then the address), REF (the data at the address, then the next
tag), CALL (data after the tag, then the address, the return pushed on a
two-level stack) and RET. The page ends where the chain reaches base + 0x20 at
the top level (what follows is the frame's tail, not the page).

**Tags are not transferred.** The builders never write a tag's upper 64 bits
(001CB5F0, 001CB6B0, 001CB760 write +0x00, +0x04, +0x10, +0x14 only), and the
captured pages hold stale bytes there that read as VIF codes (an STCYCL and
an UNPACK of 9 qwords, for one, in a blend REF's tag): transferred, they would
derail VIF1. Every VIF code the page's producers write sits in the first
qword of their data instead (001F0720's packets, 001CE300's DIRECT codes, the
presets' FLUSH / DIRECT). So only data reaches VIF1. One consequence: the two
program packets carry a FLUSHA and an STCYCL in their own DMA tag, which are
therefore never sent; the lane program's constant UNPACK can come before the
page's first STCYCL (section 5).

**VIF1.** The captured pages use NOP, STCYCL, BASE, OFFSET, STMASK, STMOD 0,
FLUSH / FLUSHE / FLUSHA, MPG, UNPACK V4-32 and V1-32 (no mask, FLG or USN),
MSCAL 0 and DIRECT. MPG uploads come only from the two program packets: the
lane program's 138 instructions from 0x002332B8 to micro 0, the sprite
program's 256 from 0x00231798 to micro 0 and 79 from 0x00231FA0 to micro
0x100.

**GIF.** DIRECT is PATH2, each XGKICK of the programs PATH1. Every packet is
PACKED (the drawing ones with PRE); its registers are RGBAQ, ST, XYZF2, XYZ2, TEX0_1, NOP and A+D
writes of PRIM, TEX0_1, CLAMP_1, TEX1_1, ALPHA_1, COLCLAMP, TEST_1 and
TEXFLUSH. PACKED ST holds its Q for the next PACKED RGBAQ, which carries it
into the vertex (the GS's internal Q).

**What the captured pages draw** (the latest page of routes 00..14, its start
at the context's +0; the route snapshots are taken before the next frame's
001CB8A0), with the weather's CALL walked over: 386 primitives: 369 sprites,
5 triangles of fans, 12 lines of line strips; 86 XGKICKs and 208 DIRECT
packets. Every captured page holds one weather CALL (108 snow MSCALs; route
10's draws 385 sprites) and the flame's REF of D_00828340. By producer:

| Producer | Shape |
|---|---|
| 001F0720 (six lanes a frame) | blend REF (mode 1), CALL D_00233290, three UNPACK packets and an MSCAL: the lane program. No route slot is active (the countdown +0x58 is 0 or below), so the program draws nothing on the route |
| 001CFBE0 (head sprites, puffs, and the AREA11 flame 001D04B0) | blend REF, CALL 0x231770, the UNPACKs of the projection rows, the source block (a REF of 9 qwords; the flame's is its overlay descriptor D_00828340) and the parameters, MSCAL: the sprite program's sprites (the PRIM of the tag row 001CFBE0 uploads: 0x56 sprite, textured, blended, or 0x76 with fog) |
| 001CD520 (glow markers, equipment sprites) | blend REF, a REF of D_00251220 (the DIRECT code and GIF tag) and 6 qwords of data: one sprite (PRIM 0x76: textured, fogged, blended) |
| 001F0A60 (the pickup glint) | DIRECT 13: two line strips of three vertices (PRIM 0x6A: Gouraud, fogged, blended, untextured); 001CB900 appends the blend REF after the packet, so the DMA sends the REF first (slot lists run newest first) |
| 001CE300 (the decal) | the blend REF, the TEX0 packet 001CB950 writes, the fans (PRIM 0x7D) |
| 001E0D70 (slot 0xFFB: its id 0xFFC000 is capped) | a CALL of the weather's channel-3 list (context +0x2520): per snow tile 001CFFE0's REF of the mode-2 preset, CALL D_00233800, the UNPACKs of the projection rows (15 to 0x6E), the descriptor (9 to 0x50) and the parameters with the tile matrix (5 to 0x59), MSCAL; then 001E55F0's RET. 108 tiles per list on the route (section 6.1) |
| page CALLs into the packet arena | the object units 001CAAC0 depth-sorts, 001DDE10's four-sprite frame-copy pass (slot 0xFFF): see section 6 |

## 3. The VU1 programs (em_vu1_page_programs.h)

Micro addresses are instruction indices of the uploaded program. All five
are entered by MSCAL 0. The lane, sprite and snow programs are the route's;
the streak and kind-2 programs first draw in the AIM side runs (chain step
AIMLIVE's fix round, 2026-10-02; AIM_FIRE.md section 10).

**The lane program** (138 instructions). Rows 0..8 are the clip projection
001CD370(2), K (0x70003AC0) and the fog row (packet 4 of 001F0720), rows 9..13
the program packet's GIF tag and four ST rows, rows 0x0E..0x11 the preset's
four corner rows, and from 0x20 the lane's 32 slots of 6 rows (a 4-row
matrix, the colour, and a TEX0 row whose z word is the countdown). For each
slot whose countdown (the low halfword of that word, signed) is above 0, it
writes the kick packet: the tag of row 9 (PRE, PRIM 0x07C tristrip Gouraud
textured fogged blended, 13 PACKED registers: TEX0 then four times ST, RGBAQ,
XYZF2), the TEX0 row, and four vertices: the corner through the slot matrix,
then K for the screen position and the clip projection for the clip test;
Q = 1 / K's w; S, T = the ST row times Q; the colour = the slot colour / 256,
its alpha times the fog weight clamp(fog.z + fog.w * w, 0, fog.x); Z + 1024;
F = 255, plus 2048 (the ADC bit) once any vertex of the slot so far has a clip
judgement among the last three. It kicks per slot, alternating the output
between 0x320 and 0x390 for every slot, active or not.

**The sprite program** (335 instructions). It works in batches of up to 28
source particles until the descriptor's count (row 88 x) is used up. Per
source particle (micro 0x053, or 0x08F when the descriptor flags have 0x10)
it draws two random lookup angles (the VU random unit: RINIT from the
parameters' seed, RNEXT and RXOR as docs/SNOW_PARTICLES.md describes), steps
the fraction, and rejects the particle when its elapsed time or its remaining
life is negative (MAC sign flags); otherwise (micro 0x0C3) it computes the
velocity and offset from the lookups and the flags, the position through the
tile matrix with gravity, and the colour and size blends and the fade. Each
batch then emits (micro 0x110): the screen position through K, the extent
through P, the fog weight, the clip test; a visible particle writes one
sprite (its colour times the fog weight, the two corners position -/+ extent,
ST (0, 0) and (1, 1)), a clipped one lowers the count; the batch is kicked
with the tag of row 124 (its NLOOP the visible count). Output alternates
between 0x100 and 0x280 per batch.

**The snow program** (D_00233800: 256 + 81 instructions; the same lookup
and constant rows as the sprite program's). Its instructions are the sprite
program's except in the emission: it copies the particle's K-projected
position into VF08 (micro 0x118), scales its w by 0.02 (I loaded at 0x11D,
the multiply at 0x120) and clamps it to 1 (0x127, the branch's delay slot,
both paths); a visible particle's colour, after the fog weight, is
multiplied by that near weight (0x13E) before its FTOI0 (0x142) and store
(0x146). The rest differs only in branch offsets that follow from its two
extra instructions (0x0F7's exit, 0x147, 0x14B). The translation is the
sprite program's with a `snow` switch (`em_vu1_snow_program_mscal`); the
reference test runs both against their own microcode.

**The streak program** (table 0x230800: 001CFBE0 kinds 0 and 4; 256 + 126
instructions; the impact effect 0x80000060's handler 001EACF0). Micro
0x000..0x0C2 are the sprite program's; its particle record (0x0C3..0x0F3)
adds a trailing point (the same position for the age row 87 w); the
emission (0x0F9..0x17B) draws each particle as a textured quad of 13 rows
(TEX0, then ST, RGBAQ, XYZF2 four times): the head and the tail through K,
their 2D difference turned a quarter, scaled by the size and normalised.
It uses the EFU: ERCPR (1 / the head's and the tail's w) and ERLENG (the
normaliser), read back by MFP (after WAITP for the ERLENG).

**The kind-2 program** (table 0x232540: 001CFBE0 kind 2; 256 + 66
instructions; the cable's hit effect node 0021AAC0). Micro 0x000..0x0F8
are the streak program's; the emission (0x0F9..0x13D) draws each particle
as a line of four rows (VF00, the head's XYZ, the colour, the tail's XYZ)
after the tag row 124: the head through rows 118..121 with P = ERCPR of its
w, the tail through the same rows with Q = 1 / its w and through the clip
rows 114..117, the colour / 256 times the tail's fog weight clamped to
[0, row 122 x], the tail's w the fog weight or (clipped) 1 + row 122 y.

**The EFU** (streak and kind 2). No capture holds an EFU result, so its
arithmetic is the model the background renderer already uses for ERLENG
(em_background_gs.h): ERLENG = 1 / sqrt(x^2 + y^2 + z^2) and ERCPR = 1 / x
(VDIV's quotient), each evaluated exactly enough (double) and truncated to
binary32 with denormals flushed; a zero operand is not established and
faults. P is written 12 (ERCPR) or 24 (ERLENG) cycles after issue; WAITP
and a following EFU op stall for it, MFP does not (each program leaves
exactly the latency before its MFP). Bit-for-bit equality with the
hardware is unproven (AIM_FIRE.md section 10.7 names the capture).

**The execution model** is the reference test's VU1 machine
(tools/chain_page_model.py VuOracle: the shadow test's machine with
ee_float_model's VU0 lane rules), whose instructions are the ELF's own:
- a pair's lower op reads its registers before the upper op writes them, and
  a VF operand still in the pipeline stalls issue, so every VF read sees the
  program-order value;
- Q (7 cycles after a DIV), the MAC flags and the clip flags (4 cycles) are
  not interlocked. On every path of both programs each read sees one
  producer, the same on every run; the test logs the producer of every read
  over the captured pages and the synthetic cases and asserts it:

| Program | Read (micro) | Producer (micro) |
|---|---|---|
| lane | Q at 0x067, 0x068 | DIV 0x060 |
| lane | clip test 0x069 | CLIP 0x064 (and the slot's two earlier vertices) |
| sprite | Q at 0x00F / 0x0D5, 0x0D6 / 0x0DF / 0x11E / 0x125 | DIV 0x008 / 0x0CE / 0x0D6 / 0x117 / 0x11E |
| sprite | MAC test 0x06A / 0x071 (0x09E / 0x0A5 on the flags-0x10 path) | 0x066 / 0x06D (0x09A / 0x0A1) |
| sprite | clip test 0x123 | CLIP 0x11F |
| snow | the sprite program's reads, the same producers | (its extra ops read no Q and set no flag a later read sees first) |
| streak | Q at 0x00F / 0x0D6, 0x0D7 / 0x0E4 / 0x12C..0x12E / 0x161 | DIV 0x008 / 0x0CF / 0x0DB / 0x125 / 0x15A |
| streak | clip test 0x12F; P at 0x12D / 0x147 / 0x162 | CLIP 0x129; ERCPR 0x121 / ERLENG 0x139 / ERCPR 0x156 |
| kind 2 | Q at 0x00F / 0x0D6, 0x0D7 / 0x0E4 / 0x125 | DIV 0x008 / 0x0CF / 0x0DB / 0x11E |
| kind 2 | clip test 0x127; P at 0x126 | CLIP 0x122; ERCPR 0x11A |

  A DIV in the same pair as a Q read (0x0D6, 0x11E) starts after that read.
- the arithmetic: DAZ operands, truncated results, FTZ, finite overflow to
  +-MAX, the multiply-add a truncated product added to ACC; VDIV truncated
  with +-MAX for a zero divisor; saturating VFTOI; raw VMAX / VMINI; an
  exponent-255 word on a live lane faults (not established; no capture has
  one). The MAC flags the sprite program tests are the sign bits.

The register file lives across MSCALs; each program reads only registers it
wrote in the same MSCAL, except the sprite program's VF11.w, which it stores
into ST's unused w lane. The consumer resets VU1 state at every page: the
test walks every captured page again from random VU1 memory and registers
and gets the same primitives.

## 4. The blend presets (001D0F20's bank)

001CB900(table, id, mode) REFs 001CB9B0(mode) = D_00275674 + 0x6A0 + 0x80 *
mode. The boot builder sub_EXTERMINATION (001D0F20, NEARMISS; the .s
followed) fills that bank once: ten presets of 0x80 bytes, each a FLUSH /
DIRECT 7 qword, an A+D GIF tag (NLOOP 6) and the writes PRIM 0x17E, TEX1_1
0x60, TEST_1, ALPHA_1, CLAMP_1 0, COLCLAMP 1, with (TEST_1, ALPHA_1) per preset
(em_gs_blocks_original.h lists them). The route uses modes 1 (TEST 0x53001:
alpha NEVER with AFAIL RGB_ONLY, Z GEQUAL; ALPHA 0x44) and 2 (the same TEST;
ALPHA 0x8000000068: Cs * 0x80 >> 7 + Cd). `em_rcl_init` runs the bank into the
render context's GS-block storage; the rest of 001D0F20 is not translated.

## 5. The GS path (em_gfx_gs_prims)

**The vertex queue.** Writing PRIM (PRE or A+D) empties the queue; XYZF2 /
XYZ2 queue a vertex and, unless its ADC bit is set, draw what the type
completes: lists (point, line, triangle, sprite) every 1 / 2 / 3 / 2
vertices, strips and line strips from the last vertices, fans from the first
and the last two. Each drawn primitive keeps the PRIM register, the state the
page set before it (TEX0, CLAMP, TEX1, ALPHA, TEST, COLCLAMP; a flag word says
which) and its vertex words.

**The pixel path** (em_gfx.h "The chain page"): the X / Y 12.4 coordinates
through the object units' GS-to-NDC mapping, Z through their depth mapping
(depth GEQUAL, no Z write: AFAIL RGB_ONLY), colour and F screen-linear
(Gouraud), S, T, Q screen-linear with the per-pixel divide (STQ); the texture
through its CT32 CLUT, bilinear with the GS 4-bit weights at U - 0.5 and
REPEAT, TFX MODULATE with TCC 1; fog FOGCOL + ((C - FOGCOL) * F7 >> 15)
with the 8.7 weight F7 = floor(128 * F + 0.01) of the screen-linear F
(floor shift; em_fog_gs_blend7 / em_fog_gs_weight7, the rule measured in
PCSX2's software GS, GS_EXACT.md 3.2 / 5.2 and
tools/test_gs_fog_conformance.py; for a constant F it is FOGCOL + ((C -
FOGCOL) * F >> 8), and the (C * F + FOGCOL * (255 - F)) >> 8 this section
gave until 2026-09-28 matched 1,040 of 4,096 pixels) with the frame's FOGCOL; the blend ((A - B) * C >> 7) + D with COLCLAMP on the
frame pixel (framebuffer fetch), RGB only. The GS sprite takes Z, F and RGBA
from its second vertex and S / Q, T / Q at each corner, affine across the
rectangle. Every other state is refused (-1, the scene faults): PRIM with AA1,
FST, CTXT or FIX; an untextured sprite or triangle, a textured line (a LINE
list of two vertices, PRIM type 1, draws like a two-vertex line strip since
chain step AIMCAM: the aim beam 001E2BA0's PRIM 0x69, AIM_FIRE.md section 7.
Evidence: the GS assembles a list every two vertices and a strip from its
last two (GS_EXACT.md 3.0), so either packet of two vertices is one segment;
the measured line rule (decomp GS_CONFORMANCE.md 5.2: 136 lines, 1,931
pixels, 0 mismatches) was measured on PRIM LINE packets, tools/
gs_conformance_suite.py `line_frac_*`, `line_axis`, `line_diamond*`, and
the line strips of `flat_select` / `gouraud_line` follow the same rule and
vertex choice (5.3). This page rasterizes either with Metal's line, as
every line it draws (section 9)); ABE 0;
TEST other than 0x53001; COLCLAMP other than 1; TEX1 other than 0x60; CLAMP
other than 0; a TEX0 not PSMT4 / PSMT8 through a CT32 CLUT with TCC 1 and
MODULATE, or not registered; a state the page did not set; FGE without the
frame's fog.

**The Q of a PACKED RGBAQ** (since the fb2 step, 2026-09-28). The GS's
internal Q is 1.0 at the start of every GIF tag and then the Q of the last
PACKED ST in that tag (measured in PCSX2's software GS: p8_gif
`pk_q_after_packed_st`, `pk_q_after_ad_rgbaq`, `pk_q_after_ad_st`,
GS_EXACT.md 2.1). `em_chain_page`'s GIF walk and the reference model
(tools/chain_page_model.py `Gs.tag`) set it at every tag. The packets'
register order (read from the captured pages): 001CD520's DIRECT packet
(D_00251220's tag) and the sprite program's kicks are TEX0, RGBAQ, ST,
XYZF2, ST, XYZF2 per sprite, and the glint's lines RGBAQ, XYZF2 three times,
so a sprite's RGBAQ takes the Q before its own STs: the previous sprite's
second ST in the same tag, or 1.0 for the tag's first. Before this step the
walk carried the Q across tags and drew a vertex whose RGBAQ preceded every
ST of the page with Q = 1.0 as a premise (42 vertices over the 15 captured
pages, 19,276 over the full route); under the measured rule every one of
those is 1.0, and no other vertex of the captured pages changes Q (the
walk of all 15 pages with both rules), so no drawn pixel changed. Every
vertex now has its tag's Q (`stale_q` is 0, asserted by the reference test
and the level smoke). A sprite's texture coordinates use the second
vertex's Q at both corners in the GS (GS_EXACT.md 4.5); in these packets
both vertices carry the same RGBAQ Q, so the port's per-corner divide
gives the same values.

**One premise**, stated because no capture shows it:
- **The inherited VIF cycle.** The lane program's constant UNPACK (14 rows to
  row 0) comes before the page's first STCYCL, because its packet's STCYCL
  sits in its DMA tag (section 2). The cycle is then the frame's, taken as
  CL == WL: the only setting under which the program finds its rows 0..13 in
  one block. Counted (15 UNPACKs over the captured pages).

## 6. What the page walks over, or never holds, in the port

### 6.1 The flame and the snow (step FLAMESNOW, 2026-09-28)

- **The flame.** Owner 008235F0 (em_area11_effect_runtime) calls
  001D04B0(+0xD0, 1, D_00828340, phase, seed) at its DRAW (0x8236AC); the
  port's 001D04B0 (`em_effects_live_001D04B0`; the decomp holds it as an asm function, its three calls
  read from the .s) runs 001CCF70(+0xD0 + 0x30), 001CFA60 and 001CFBE0 on the
  translations every other effect uses. The descriptor is overlay data: its
  0x90 bytes (the EMEF export's, equal to the captures') stay readable by
  address through `em_effects_live_window`, so the page's REF of D_00828340
  reads them. Retired: `em_area11_effect_runtime_draw`,
  `em_effect_sprite_project`, the flame's `em_snow_particles_generate` call,
  the EMTX texture slot 1 and `em_gfx_particles_draw_slot`.
- **The snow.** The weather actor 001E55F0 (em_snow_runtime) reads the
  channel-3 cursor (context +0x1C), runs 001E67C0 (em_snow's tiles, with its
  0021B9A0 calls on the render context) and for every tile 001CFAE0 and
  001CFFE0(3, 3, D_00255170, state) (`em_weather_packets`), then writes the
  RET tag and 001D2DE0(0, start): context +0x2520. The frame close's
  001E0D70 (live since the render context step) CALLs it at slot 0xFFB, and
  the consumer walks it: 108 tiles, each a CALL of D_00233800 and an MSCAL
  of the snow program. Retired: `em_snow_runtime_draw`,
  `em_snow_particles_generate` / `em_snow_particles_color` /
  `em_snow_project` (em_snow_particles.c, em_snow_projection.c), the EMTX
  texture slot 0 and `em_gfx_particles_draw` / `em_gfx_particle_texture_set`.
  The snow's TEX0 (the descriptor's +0x70) is a page texture
  (tools/export_page_textures.py, export_disc_textures.py).
- **One owner.** The particle generation, the projection and the colour of
  both now run only in `em_vu1_page_programs.h` (the sprite and snow
  programs share one translation). The only CPU-side remains are the owners'
  own translations: 008235F0's controller (em_area11_effect.c) and 001E67C0's
  tiles (em_snow.c).
- **001E0DF0.** Step V's 001E0DF0 would send a pending list 001E0D70 left
  (with render flag 4, a movie frame, or when flags & 0x0E000000); in the
  first level 001E0D70 always runs first (flag 4 is never set by the port,
  AREA11's flags are 0x10) and clears it, as in every capture (+0x2520 = 0
  at every route snapshot).

- **001DDE10's four-sprite pass (slot 0xFFF).** Every world frame (flag 1)
  001DDE10 CALLs a channel-3 packet: GS environment REFs of the other banks
  (FRAME / ZBUF / TEST / CLAMP), 001D6C90's texture-from-frame packets and
  four sprites that sample the frame. What the sprites are (the decomp's
  func_001DDE10, read 2026-09-28 for the fb2 step): per slot k = 0..3, after
  001D6B10(3, D_0027568C, 8, 8), 001D6BA0, 001D1FF0(3, 3) and 001D6C90 set
  the texture state, one PACKED sprite (tag with PRE, registers RGBAQ, UV,
  XYZF2, UV, XYZF2) covering the whole field, window corners (0x7000,
  0x7900) and (0x9000, 0x8700), UV (8, 8) to (0x1008, 0x1008) in 1/16
  texel, RGB 0x80 and alpha the slot's eased value at +0x2500 + 4k (in
  AREA11, when neither 001D2910(7) nor 0022EBE0 is set, the targets 0x18 /
  0x28 / 0x38 / 0x48), Z the slot's eased depth
  (+0x24F0 + 4k, from the tracked point's depth through the slot's gain).
  So each sprite blends a copy of the frame, read through the texture state
  001D6C90 sets, over the pixels its Z test passes. Its look is not
  reproduced: it needs the frame as GS memory (the displayed buffer read
  back through that texture state) and the GS Z buffer for the test, which
  only the GS model's binding provides (GS_EXACT.md section 9); the port's
  Metal frame has neither. The fb2 harness (tools/test_fb2_pixels.py) shows
  no region-wide difference at the camera-exact points 10 and 14 that this
  pass would explain, but it cannot isolate the pass either. The consumer walks over that one CALL,
  whose address the render context records at 001CB760(0xFFF000)
  (`em_rcl_page`), and counts it; the smoke asserts it is the only CALL
  walked over (12,573 over the full route). It was not drawn before either.
- **The object units 001CAAC0 depth-sorts (chain step AIMLIVE; behind the
  aim/fire gate until its fix round, in ordinary play since 2026-10-02).** 001CABA0 (the muzzle node's +0x4C draw, AIM_FIRE.md
  section 9.1) builds a class-2 object unit on channel 3 and 001CAAC0 /
  001CB760 CALL it from the page at its depth. em_chain_page keeps each such
  CALL as a unit marker (`EmChainPageUnit` callback, at most 32 a page:
  `EM_CHAIN_PAGE_UNITS_MAX`); em_chain_page_live's callback runs
  em_object_unit over the unit (class 2: the GS state 001D1F80(3, 2, 2)
  writes at 0x815A20, TEST 0x53001, ZBUF with ZMSK 1, ALPHA
  0x8000000068; the skin records 0x816B40 / +0x80, the clip records
  0x816E40 / +0x80; PRIM 0x07C, the clip pass 0x07B), and its triangles
  join the page's primitives at the marker, leaving the unit's end state
  (TEX0, PRIM) for what follows. The page counts them (`units`, per unit
  the primitives, strip triangles, TEX0 and PRIM; the tick log's `page`).
  The Metal page shader draws them with the TFX HIGHLIGHT and textured
  type-3 triangles. The page re-walk (tools/chain_page_model.py,
  `units=`) takes a unit's end state from the port and compares the rest
  of the page (`digest_without_units`); the units' triangles are proven by
  test-object-unit-reference part K. Outside the gate no unit reaches the
  page; an object unit of another class still refuses.

## 7. Binding (live)

- **001D1EA0's kick.** em_render_context_live wraps the frame close's 001CB800
  (the start = 001CB800's base, D_00810E80 read as it does) and 001DDE10's
  001CB760 (the slot-0xFFF target); `em_rcl_page` hands both out once per kick.
  001E0D70's 001CB760 (id 0xFFC000, the weather's list) is noted for the
  smoke (`em_rcl_page_weather`); the consumer walks it like any CALL.
- **The producers bound for this page since FLAMESNOW** (section 6.1): the
  flame 008235F0's DRAW -> `em_effects_live_001D04B0` (tick_effect in
  em_area11_bindings.c); the weather 001E55F0 -> `em_snow_runtime_tick_actor`
  (tick_weather), its tiles through `em_weather_packets` into channel 3 and
  +0x2520 through `em_rcl_001D2DE0`.
- **em_chain_page_live_draw**, in frame_close_out at the page's position: in a
  world frame after the level, the walked and post-step units and the
  shadow's passes, before the fog is switched off (the snow and the flame are
  inside the page since FLAMESNOW); in a
  status frame after the status page (001D1EA0(0) kicks the page too; the
  port's status pages are empty: 418 over the default run); and, since
  2026-09-27, in a tear-down frame (001D1EF0's 001D1EA0(0): the area build,
  the status close) through em_render_001D1EF0 (RENDER_CONTEXT.md section
  9). It walks the page
  over the render context's storage (`em_rcl_bytes`: the arena, the chain
  table, the context, the GS blocks, the .data D_00250F30..) and the
  effect-table export's ELF blocks (`em_effects_live_window`: the five
  program packets, D_00253670, D_002565E0.., and the overlay source blocks
  001D04B0 was handed: the flame's D_00828340), then tells em_shadow_live how many decal
  triangles it drew (`em_shadow_live_page_drew`: they must be exactly the
  frame's 0015BF90 fans'), checks every vertex has its GIF tag's Q
  (section 5) and draws.
- **Assets:** `python3 tools/export_effect_tables.py` again (the three program
  packets, 0x00231770 + 0xDD0, 0x00233290 + 0x570 and, since FLAMESNOW,
  0x00233800 + 0xDE0, joined its blocks; each block is checked equal in every
  capture; em_effects_live refuses an export without the three) and `python3
  tools/export_page_textures.py` (or `export_disc_textures.py`; since
  FLAMESNOW the set holds the weather descriptor D_00255170's TEX0; since
  chain step AIMCAM the laser dot's 0x20045BA5154222DC (001854E0 /
  00185760's code immediate), 8 textures; since chain step AIMLIVE's fix
  round (2026-10-02) the streak program's packet 0x00230800 + 0xF70 and the
  kind-2 program's 0x00232540 + 0xD50 (em_effects_live refuses an export
  without all five), and in the page textures the impact sources' TEX0
  rows, the ring decals', the lamp flare's, the cable hit sprite's and the
  cable strand's code words, 16 textures) (STARTUP.md rows 50 and 52).
- **What the page counts for the smoke** (the tick log's `page`): besides
  the MSCALs per program, the streak and kind-2 programs' primitives
  (`streak_prims`, `kind2_prims`), the lanes' strip triangles
  (`lane_strips`: an active ring-decal slot) and the DIRECT packets' strip
  triangles (`direct_strips`: 0021A500's parted cable strand), so
  check_chain_page can tell each strip triangle's source; the first 12
  pages of each kind are sampled for the re-walk.
- Fail-stop: a page fault, a refused primitive or a decal count mismatch
  latches `em_chain_page_live_fault` and faults the scene.

## 8. Verification

- **`make test-chain-page-reference`** (tools/test_chain_page_reference.py;
  quick ~11 s, `EM_TEST_FULL=1` ~140 s):
  - the latest page of every route capture 00..14: every MSCAL (90 lane, 59
    sprite, and the weather's 108 snow MSCALs on one page, 1,620 on all 15 in
    full) run by the translation and by the ORIGINAL microcode from the same
    data memory and registers: all 16 KiB of data memory, every register and
    every XGKICK's packet equal; the whole page's primitives (386 with the
    weather's CALL walked over; with it walked, beat 10 alone adds 385) equal
    the model's, with the same DMA, DIRECT, MSCAL, XGKICK and skip counts;
    three pages (all 15 in full) walked again from random VU1 contents draw
    the same;
  - synthetic batches: 40 (600) lane batches with active slots placed around
    the captures' cameras (clipped, ADC and wrapping countdowns), 40 (600)
    sprite and 20 (300) snow batches (counts 1..90, every flags value, the
    0x10 path, clipped particles); every conditional branch of the three
    programs both ways; the timing table of section 3 asserted on every run;
  - since 2026-10-02 also 40 (600) streak and 40 (600) kind-2 batches from
    a captured sprite MSCAL's memory with each packet's own uploads and the
    kind's GIF tag row (D_00251260 entries 0 / 1 and 4 / 5): every branch
    both ways, the timing table asserted, and an ERCPR of zero faulting on
    both sides (no capture holds either program's page);
  - faults: an exponent-255 word on a live lane (every program); nine
    malformed pages (END, an unknown MPG, an MSCAL before any program, ITOP,
    REGLIST, an A+D FRAME write, an unmapped REF, no room) and one clean fan;
  - the preset bank equal to every capture's bytes, and in full mode to the
    bank the ORIGINAL 001D0F20 writes when executed.
  Full sweep 2026-09-26: 690 lane and 660 sprite MSCALs, 15,944 XGKICKs,
  3,660,384 packet bytes, all equal. Full sweep 2026-09-28 (FLAMESNOW):
  690 lane, 660 sprite and 1,920 snow MSCALs (1,620 of them the captured
  pages' weather tiles), 17,835 XGKICKs, 4,193,296 packet bytes, all equal;
  the 15 pages with the weather walked draw 5,219 primitives, equal to the
  model's.
- **`make test-chain-page-gpu`** (tools/test_chain_page_gpu.py, ~4 s): the Metal
  pixel at the frame centre equals the GS pixel model for five decal fans
  (the retired decal entry's cases), four sprites (additive and 0x44, with
  and without fog) and two glint lines (the one pixel the line lights in the
  centre column); six refusals draw nothing.
- **`make test-chain-page`** (tests/chain_page_test.c, ASan / UBSan): a clean
  page, the argument refusals, a CALL walked over, and 20,000 corrupted pages
  walked without a memory error.
- **The level smoke** (`check_chain_page`, LEVEL_SMOKE.md): full route 13,013
  pages drawn (12,573 world frames, the rest empty status and tear-down
  frames; 4,122,371 sprites, 1,314 triangles, 1,256 lines), the only CALL
  walked over each frame's 001DDE10 one, the lane program run six times in
  exactly the barrel's frames without a lane drawn; every world page holds
  the weather's CALL at the list its frame closed (108 snow MSCALs) and
  reads the flame's descriptor once; 40 sampled pages re-walked with the
  original microcode over the port's own page bytes (the weather's CALL
  walked: the ORIGINAL snow program on every tile) draw exactly the port's
  primitives, the blend presets, the three program packets and the flame's
  descriptor they read equal to the captures' (444 reads); in the
  camera-exact snapshots the glow markers drawn equal the capture's own
  page's (10: five, 14: none), colour masked (rand()), and so do the
  weather's tile packet 3 and the flame's 001CFBE0 packets 4 and 1 (phase
  and seed words masked).
- **By eye** (build/captures/chain_page/): the cage roof (route 10's end):
  the glow marker at the pillar edge against 10's original.png
  (`port_crop.png` / `orig_crop.png`).

## 9. Limits

- Rasterization is Metal's (float interpolation, the line rule), not the GS
  DDA; no GS dump of a drawn frame exists to compare pixels with.
- The inherited VIF cycle is a premise (section 5); the Q is the measured
  per-tag rule since the fb2 step.
- 001DDE10's four-sprite pass is walked over, not drawn (section 6).
- The flame's and the snow's sprites follow their owners' phase and seed
  (the flame's age since its spawn, rand(): 008235F0 state 0 and 001E55F0
  state 0), which the port's stream does not hold at a capture's position
  (RAND_ORDER.md): their primitives are compared with the captures only
  through their packets' camera and fog rows (section 8), and with the
  original microcode over the port's own packets on the sampled pages.
- The lane program's drawing path: no route slot is active; the AIM side
  runs' shots activate slots through 001F0460 (the ring decals), whose
  lanes then draw. The level smoke counts them (`lane_strips`), allows them
  only from a side run's first tick and re-walks the first 12 such pages
  with the original microcode; their positions follow where the run stands
  and are not compared with a capture.
- The streak and kind-2 programs are proven against their own microcode
  (synthetic batches and the side runs' sampled pages), not against a
  capture: no capture holds a page that runs them, and their EFU results
  are a model (section 3).
- The sprites' positions and colours follow the producers' draws, and the
  port's stream is never at a capture's position (RAND_ORDER.md), so the
  smoke compares the drawn primitives with the captures only for the glow
  markers (their colour through check_marker_colour, with each side's own
  draws), and the flame's and the snow's packets as above.
- VU1 arithmetic is the VU0 model assumed for VU1, as for the object kernel
  (VU1_OBJECT_KERNEL.md).

## 10. Makefile (applied)

```make
test-chain-page-reference:   python3 tools/test_chain_page_reference.py
test-chain-page-gpu:         python3 tools/test_chain_page_gpu.py
test-chain-page:             the ASan / UBSan fixture tests/chain_page_test.c
```

`src/game/em_chain_page.c`, `src/game/em_chain_page_live.c` and
`src/game/em_gs_blocks_original.c` are in COMMON.

## 11. List mode: a whole frame list (the load veil, 2026-09-27)

`em_chain_page_run_list` walks a frame's whole list (main-loop step V's, as
001D21E0 kicks it) with the same DMA / VIF1 / GIF / GS machinery, for the
load veil's frames (LOAD_VEIL_PARTICLES.md section 3.3):

- **DMA.** The walk ends at the top-level END tag (step V's list closes with
  a NEXT to the GS block + 0x10, which holds an END); its data is
  transferred. Page mode still faults on END.
- **GIF.** A DIRECT may hold several GIF packets: after an EOP, PATH2 reads
  the next tag until the DIRECT's data is used up (001D6930 sends its TEX0
  A+D packet and its sprite in one DIRECT 10). Page mode keeps one packet per
  DIRECT.
- **Registers.** Besides the page's, list mode takes the A+D writes of the
  context-1 environment (FRAME_1, ZBUF_1, XYOFFSET_1, SCISSOR_1,
  PRMODECONT, DTHE, FBA_1, PABE, TEXA, SCANMSK), recorded for every
  primitive after them in `prim_env` (EmGfxGsEnv); the context-2 set of the
  draw environments and FOGCOL (accepted; no context-1 primitive reads them);
  and the vertex registers by A+D: RGBAQ with its Q, ST, UV, XYZF2 and XYZ2
  (the clear sprite of bank C). The page never holds any of them, so page
  mode still faults on them.
- **Verification.** `make test-chain-page` walks a frame list (two GIF
  packets in one DIRECT, the environment, an A+D sprite, the END; page mode
  refuses the same bytes) and 5,000 corrupted lists under ASan / UBSan;
  `make test-load-veil-gpu` walks the veil's lists and draws them; the level
  smoke's check_load_veil walks the live one (LEVEL_SMOKE.md "The load
  veil").
