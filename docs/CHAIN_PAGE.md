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

Since step DOF (2026-10-09) the Original profile draws the page's last
CALL, 001DDE10's depth-of-field pass at slot 0xFFF, through the CPU GS model
(section 6.2); the GPU renderer still walks over it.

Files:
- `src/game/em_vu1_page_programs.h`: the lane program (DMA packet
  D_00233290), the sprite program (table 0x231770) and the snow program
  (D_00233800), translated from their VU1 microcode (section 3).
- `src/game/em_chain_page.{h,c}`: the DMA / VIF1 / GIF / GS walk (section 2)
  and its pass mode for 001DDE10's depth-of-field pass (section 6.2).
- `src/game/em_gs_blocks_original.{h,c}`: 001D0F20's blend-preset bank, the GS
  state the page REFs (section 4).
- `src/game/em_chain_page_live.{h,c}`: the live binding (section 7); the
  pass reaches the GS model through `em_gfx_gs_page_pass` (em_gfx.h) and
  `em_gs_world_field_declare` (GS_EXACT.md section 9).
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
packets; with 001DDE10's pass walked too (pass mode, section 6.2), 120 more
sprites and 480 more DIRECT packets (8 and 32 a page). Every captured page
holds one weather CALL (108 snow MSCALs; route 10's draws 385 sprites), the
flame's REF of D_00828340 and the pass's CALL, its last. By producer:

| Producer | Shape |
|---|---|
| 001F0720 (six lanes a frame) | blend REF (mode 1), CALL D_00233290, three UNPACK packets and an MSCAL: the lane program. No route slot is active (the countdown +0x58 is 0 or below), so the program draws nothing on the route |
| 001CFBE0 (head sprites, puffs, and the AREA11 flame 001D04B0) | blend REF, CALL 0x231770, the UNPACKs of the projection rows, the source block (a REF of 9 qwords; the flame's is its overlay descriptor D_00828340) and the parameters, MSCAL: the sprite program's sprites (the PRIM of the tag row 001CFBE0 uploads: 0x56 sprite, textured, blended, or 0x76 with fog) |
| 001CD520 (glow markers, equipment sprites) | blend REF, a REF of D_00251220 (the DIRECT code and GIF tag) and 6 qwords of data: one sprite (PRIM 0x76: textured, fogged, blended) |
| 001F0A60 (the pickup glint) | DIRECT 13: two line strips of three vertices (PRIM 0x6A: Gouraud, fogged, blended, untextured); 001CB900 appends the blend REF after the packet, so the DMA sends the REF first (slot lists run newest first) |
| 001CE300 (the decal) | the blend REF, the TEX0 packet 001CB950 writes, the fans (PRIM 0x7D) |
| 001E0D70 (slot 0xFFB: its id 0xFFC000 is capped) | a CALL of the weather's channel-3 list (context +0x2520): per snow tile 001CFFE0's REF of the mode-2 preset, CALL D_00233800, the UNPACKs of the projection rows (15 to 0x6E), the descriptor (9 to 0x50) and the parameters with the tile matrix (5 to 0x59), MSCAL; then 001E55F0's RET. 108 tiles per list on the route (section 6.1) |
| page CALLs into the packet arena | the object units 001CAAC0 depth-sorts (section 6); 001DDE10's depth-of-field pass (slot 0xFFF, the page's last CALL): section 6.2 |

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

**The kind-6 program** (D_0023D930: 001CFBE0 kind 6, AREA01's fire owner
001E3D90's near-fire layer; 256 + 130 instructions, packet 0xF70 bytes with
its RET). Micro 0x000..0x10A are the sprite program's except the batch size
(one source particle per batch, the immediates at 0x028 / 0x035 / 0x03A)
and no I / VF11 set-up at 0x0F5 / 0x0F6; the emission (0x10B..0x17C) takes
the batch's one particle as a screen-space square: its centre through K,
its half size through the clip rows (its w), the colour times 1/256 and the
fog weight. A square the clip judgement rejects in z (the test masks z
only) or that lies wholly outside the GS window (rows 125 / 126, the
packet's own upload: min and max x, y) is replaced by an empty tag (NLOOP
0, EOP) at row 101's base, which is kicked; otherwise the square is cut to
the window (its ST cut by the same fraction: 0.5 / half size per unit) and
drawn as 5 x 5 SPRITE tiles of six rows each (row 100, the colour, the far
corner's ST and XYZ, the near corner's ST and XYZ; ST from 1 at the near
corner to 0 at the far one) after the tag row 124 with NLOOP 25 at row 101's
base - 1. Like the sprite program it stores VF11.w (ST's unused w lane)
without writing it. Translation: `em_vu1_kind6_program_mscal` (the sprite
program's flow with a variant switch, emvup_kind6_particle); its own test is
`tools/test_level2_kind6_vu_reference.py` (LEVEL2_RENDER.md "Kind-6
near-fire program").

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
| kind 6 | the sprite program's reads through 0x0EF; Q at 0x11E / 0x125 / 0x140 / 0x14B | its producers; DIV 0x117 / 0x11E / 0x139 / 0x140 |
| kind 6 | clip test 0x123; MAC test 0x143 / 0x145 / 0x149, 0x14A / 0x14B, 0x14C | CLIP 0x11F; 0x13F / 0x141 / 0x145 / 0x147 (Sx 0x80, Sy 0x40 of the written x, y) |

  A DIV in the same pair as a Q read (0x0D6, 0x11E; kind 6 also 0x140)
  starts after that read.
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
REPEAT, TFX MODULATE with TCC 1 (and, since step FRAMES, MODULATE with TCC
0: Af = Av, the RGB textures of AREA01's floor fields and ripple surface,
LEVEL2_RENDER.md "Floor-field and ripple programs"; `make
test-chain-page-gpu` checks it); fog FOGCOL + ((C - FOGCOL) * F7 >> 15)
with the 8.7 weight F7 = floor(128 * F + 0.01) of the screen-linear F
(floor shift; em_fog_gs_blend7 / em_fog_gs_weight7, the rule measured in
PCSX2's software GS, GS_EXACT.md 3.2 / 5.2 and
tools/test_gs_fog_conformance.py; for a constant F it is FOGCOL + ((C -
FOGCOL) * F >> 8), and the (C * F + FOGCOL * (255 - F)) >> 8 this section
gave until 2026-09-28 matched 1,040 of 4,096 pixels) with the frame's FOGCOL; the blend ((A - B) * C >> 7) + D with COLCLAMP on the
frame pixel (framebuffer fetch), RGB only. The GS sprite takes Z, F and RGBA
from its second vertex and S / Q, T / Q at each corner, affine across the
rectangle. Every other state is refused (-1, the scene faults): PRIM with AA1,
FST, CTXT or FIX; an untextured sprite, a flat-shaded triangle (an
untextured Gouraud triangle draws on the flat path, Cf = Cv, Af = Av, since
the AIM fix round: the knife trail 001F15F0's strip, PRIM 0x4C, AIM_FIRE.md
section 11.3), a textured line (a LINE
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
MODULATE / HIGHLIGHT or TCC 0 and MODULATE, or not registered; a state the page did not set; FGE without the
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

### 6.2 001DDE10's depth-of-field pass (step DOF, 2026-10-09)

**What the original does** (the decomp's func_001DDE10, byte-matched, whose
comment "radar/altimeter HUD bar builder" is wrong; RENDER_CONTEXT.md 2.3;
the packets its callees write, read in every route capture). While render
flag 1 is set, every world frame's close runs 001D1EA0 -> 001DDA00 ->
001DDAA0 -> 001DDE10 (001DE920 instead only for the area keys 0xB00..0xE00
with D_008106C8 & 0x60, which AREA11 never sets). 001DDE10 builds one
channel-3 list and hands it to 001CB760 at slot 0xFFF, the page's last slot,
so the page ends with that CALL. Four times, k = 0..3:

1. **The copy** (001D6B10 -> 001D6930, then 001D1F20). 001D6E60 switches
   drawing to the 256x256 PSMCT32 buffer at D_0027568C (GS byte 0x258000:
   FBP 0x12C, FBW 4) with XYOFFSET (0x7800, 0x7800), SCISSOR 0..255 x
   0..255, PRMODECONT and COLCLAMP bit 0 set, DTHE bit 0 clear (001006D8
   keeps the other bits of those three words as the packet memory held
   them: stale, unread by the GS), TEST 0x30000; the REF of bank E 0
   (001D2040(3, 0)) makes TEST 0x3000D and ZBUF the field's Z buffer with
   ZMSK; the REF of bank D 2 (001D1FF0(3, 2)) the REGION_CLAMP 0..511 x
   0..223; then one DIRECT of two GIF packets: TEXFLUSH, TEX0 = the field
   this frame draws (TBP 0x700 when context +0x9C is 0, else 0: bank A's
   FRAME of that slot; 512 wide, 512x256, PSMCT32, DECAL, TCC 0), TEXA 0x20,
   and the sprite PRIM 0x116 (UV, no blending) with D_0026E510's RGBA (0x80
   x 4), UV (0.5, 0.5)..(511.5, 223.5) over the window (0, 0)..(256, 256).
   TEX1 is whatever was last set (bank A's 0x60 for k > 0; the page's last
   for k = 0: 0x60 in every capture). Then the REF of the frame's draw
   environment, bank A of the kicked slot (001D1F20: back to the field).
2. **The blend** (001D6BA0, 001D1FF0(3, 3), 001D6C90, the packet). TEX0 =
   the copy (TBP 0x2580, TBW 4, 256x256, PSMCT32, MODULATE, TCC 0), the
   REGION_CLAMP 0..255 of bank D 3, TEXA 0, TEST 0x51001 (alpha NEVER with
   AFAIL FB_ONLY: colour and alpha written, never Z; ZTE, ZTST GEQUAL), ALPHA
   0x44 ((Cs - Cd) * As >> 7 + Cd); one sprite PRIM 0x156 (UV, blended) over
   the whole field, (0x7000, 0x7900)..(0x9000, 0x8700), UV (0.5,
   0.5)..(256.5, 256.5), RGBA (0x80, 0x80, 0x80, rr), XYZF2 Z = cc >> 4 (the
   PACKED Z field is the word's bits 4..27, GS_EXACT.md 2.1). cc and rr are
   the slot's eased depth and alpha at context +0x24F0 / +0x2500 + 4k
   (RENDER_CONTEXT.md 2.3: cutscenes, 0022EBE0 != 0, focus on the camera's
   look-at point, gains 4 / 2 / 1 / 0.5, alpha 0x3E; AREA11 play, focus on
   the player, gains 8 / 7 / 6 / 5, alphas easing to 0x18 / 0x28 / 0x38 /
   0x48). Since the scene's Z test is GEQUAL (larger Z is nearer), only the
   field pixels at or behind the slot's depth take the blend, and each pass
   copies the already blended field.

After the four passes: the draw environment once more (001DE898's
001D1F20) and the list's RET. No primitive reads the buffer it draws into:
the copy reads the field and draws the copy, the blend reads the copy and
draws the field (FIRST_LEVEL_AUDIT.md lead F2's premise does not hold).
The copy buffer is the drop shadow's 128x128 target memory (TEX0
0x5DC00A580, FBW 2) and the load veil's lens buffer (D_0027568C): the GS
memory is one.

**One owner, the lens's path.** The copy is 001D6930's packet, the one the
load veil's lens sends too (001DFA40 -> 001D6B60 -> 001D6930,
LOAD_VEIL_PARTICLES.md): both are built by the one translation
`em_load_veil_particles_001D6930` (with 001D6E60, 001D2040, 001D1FF0,
001D1F20 and 001D6BA0 beside it), walked by the same list-mode register
handling of em_chain_page, and drawn through the same em_gs_world_prims
path with environments as the veil's list frames. 001DDE10 and 001D6C90
are em_render_context's translations.

**The walk (pass mode, em_chain_page.h).** With `pass_call` the walker
follows that top-level CALL instead of walking over it; inside it the GIF
takes list mode's registers (the context-1 environment and the A+D vertex
registers: section 11), a DIRECT may hold several GIF packets (001D6930's),
and each primitive gets the environment the pass set before it
(prim_env). The REF of the kicked slot's draw environment (bank A,
`draw_envs` + 0x190 * `draw_env_slot`, 0x19 qwords) is not transferred: the
DMA reads it after step V's 001D2300 has written the field's half-line
XYOFFSET into it, after the walk at the frame close, so the walk records an
environment-again mark (again[]: the primitive it precedes) and forgets
the registers that block writes (the environment, CLAMP_1, TEX1_1, TEST_1,
COLCLAMP: the frame's from there on). Any other REF into bank A in the pass,
a context-2 or FOGCOL write in it, a second pass CALL or more than 16 marks
fault. Counts: `passes`, `pass_prims`, `pass_direct`, `again`. On every
captured page the pass draws 8 sprites (copy, blend, four times) with marks
before primitives 1, 3, 5, 7 and after 8 (relative to the pass), from 32
DIRECT packets.

**The draw (em_chain_page_live, em_gfx_gs_page_pass, em_gs_world).** In a
world frame the GS model records (em_gfx_gs_world_recording) the consumer
walks the pass with the bank and slot em_rcl_draw_env gives (D_00275674 +
0x20, context +0x9C), hands the page before it to em_gfx_gs_prims, then the
pass to em_gfx_gs_page_pass with the field (that block's FRAME_1 and
SCISSOR_1, constant per slot): the backend declares the field to the model
(em_gs_world_field_declare: a buffer this frame draws, for the texture
residency check and the workers' ordering; the kick faults when its head
names another field), records each run of primitives with its environment
(em_gs_world_prims, the load veil's path: FRAME_1 / ZBUF_1 / XYOFFSET_1 /
SCISSOR_1 / PRMODECONT / DTHE / TEXA, then TEX0 / CLAMP / TEST / ALPHA /
COLCLAMP, PRIM and the vertex registers) and sends the kick's environment
packet at each mark (em_gs_world_env_again, which makes the field the
drawing target again). The model draws everything in strict mode: the
bilinear 2:1 shrink and the stretch back (TEX1 0x60, REGION_CLAMP), DECAL
and MODULATE with TCC 0, the alpha test NEVER with FB_ONLY, the Z test
GEQUAL against the field's Z24 buffer with no Z write, the blend; anything
it cannot do exactly faults the scene. The workers synchronise where a
band would read rows another band draws: before the first copy (the
field's last rows), between each copy and its blend, and before each next
copy (8 barriers a frame). With the GS frame on, a 001DDE10 CALL outside a
recorded world frame faults (001D1EA0(0) frames never hold one). With the
GPU renderer (EM_GPU_RENDERER=1) the walker still walks over the CALL
(`skip_calls`, counted) and nothing of the pass is drawn: Metal has neither
the field as GS memory nor its Z buffer.

**Measured (2026-10-10): bit-exact against the original.** The decomp's
fork capture (build/dof_capture: 5 frames of the opening cutscene, 2 of
AREA11 play, one repeated; the DMA cut at the pass's two boundaries, so
the GS memory before and after the pass and its GIF bytes are the
original's own) is the test `make test-dof-pass-reference`
(tools/test_dof_pass_reference.py, GS_EXACT.md section 7). In every frame
all 4 MiB of local memory after the pass are equal three ways: the
captured GIF bytes through the model; the port's own packets
(em_render_context_001DDE10 over the captured EE state with the live
binding's workers: every EE byte it writes and the 4,544 GIF bytes of the
pass equal to the original's); and the Original profile's path (the pass
walk, em_gs_world_page_pass, the kick, 1 to 8 workers). The field, the
copy and the Z buffer (untouched) are compared apart: the 2:1 bilinear
shrink of the copy, the 256 -> 224-row stretch of the blend, MODULATE with
TCC 0, the GEQUAL plane at Z = the PACKED word >> 4 and the (Cs - Cd) As
>> 7 + Cd lerp are exact. One model rule changed to get there (GS_EXACT.md
3.4): a UV sprite's row coordinate is accumulated in binary32 one row at a
time, so the blend's 8/7-texel row step falls one 1/16 texel below its
exact value on some of the rows where that value is an integer; with the
exact value the model was off on 183 to 2,695 field pixels of each frame a
blend reaches. In two frames no blend passes its Z test (the first wide
shot and a close-up: every field pixel is nearer than the blends' planes),
so the copy is tested alone there, in a whole-line field (FBP 0) and a
half-line one. Not captured: a blend that passes its Z test in a
whole-line field (GS_EXACT.md 8.8). The fb2 points (GS_EXACT.md 10.1)
include the original's pass.

## 7. Binding (live)

- **001D1EA0's kick.** em_render_context_live wraps the frame close's 001CB800
  (the start = 001CB800's base, D_00810E80 read as it does) and 001DDE10's
  001CB760 (the slot-0xFFF target: the depth-of-field pass, section 6.2);
  `em_rcl_page` hands both out once per kick.
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
  cable strand's code words, 16 textures) (STARTUP.md rows 50 and 52);
  since step DRAWN (2026-10-04) also AREA01's kind-6 packet D_0023D930 +
  0xF70 (an export without it makes the first near-fire page fault: the
  chain page's unmapped-address refusal).
- **What the page counts for the smoke** (the tick log's `page`): besides
  the MSCALs per program, the streak and kind-2 programs' primitives
  (`streak_prims`, `kind2_prims`; since the DRAW merge, 2026-10-07, also
  the kind-6 program's `mscal_kind6` / `kind6_prims`, last in the record,
  which `check_kind6_area01` reads over the AREA01 ticks), the lanes' strip triangles
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
    every XGKICK's packet equal; the whole page's primitives (since step DOF
    with 001DDE10's pass walked in pass mode on both sides: 506 with the
    weather's CALL walked over, the pass's 120 among them; with it walked,
    beat 10 alone adds 385) equal the model's, with the same DMA, DIRECT,
    MSCAL, XGKICK and skip counts, and the pass's environments, marks and
    counts equal (part G: section 6.2);
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
  model's. Full sweep 2026-10-09 (step DOF, the pass walked in pass mode on
  both sides): 690 lane, 660 sprite, 1,920 snow, 600 streak and 600 kind-2
  MSCALs, 18,765 XGKICKs, 6,466,096 packet bytes, all equal; the 15 pages
  with the weather and the pass walked draw 5,339 primitives (the pass's 120
  among them), equal to the model's, and the same again from random VU1
  contents.
- **`make test-chain-page-gpu`** (tools/test_chain_page_gpu.py, ~4 s): the Metal
  pixel at the frame centre equals the GS pixel model for five decal fans
  (the retired decal entry's cases), four sprites (additive and 0x44, with
  and without fog) and two glint lines (the one pixel the line lights in the
  centre column); six refusals draw nothing.
- **`make test-chain-page`** (tests/chain_page_test.c, ASan / UBSan): a clean
  page, the argument refusals, a CALL walked over, and 20,000 corrupted pages
  walked without a memory error.
- **The pass** (step DOF): `make test-chain-page-reference` walks every
  captured page with the pass in pass mode on both sides (the native walk
  and tools/chain_page_model.py's pass mode): the pass's primitives, the
  environment of each and its marks equal; part G checks them against the
  capture's own bytes: the copy's environment, TEX0 (the field bank A's
  slot block draws into), CLAMP (bank D 2), TEST and ZBUF (bank E 0) and
  geometry, the blend's TEX0 (the copy), CLAMP (bank D 3), TEST 0x51001,
  ALPHA 0x44, geometry, and its Z and alpha from the context's +0x24F0 /
  +0x2500 (15 pages, slot 0 on 9 and slot 1 on 6; the pass is the page's
  last); `make test-chain-page` the pass mode's semantics and refusals (11)
  and 5,000 corrupted pass pages; `make test-gs-world` the pass through the
  model with 1, 2, 3 and 8 workers (the same bytes; a world frame where
  nothing reads the copy's memory before the pass, so only the field's
  declaration orders the first copy: without it the thread sanitizer
  reports the race) and the field declaration's refusals.
- **The pass's pixels** (2026-10-10): `make test-dof-pass-reference`
  (tools/test_dof_pass_reference.py with tests/dof_pass_reference_bridge.c;
  quick 3 captures, `EM_TEST_FULL=1` all 8): against the original's GS
  memory on both sides of the pass, captured in the PCSX2 fork (decomp
  build/dof_capture). A: the captured GIF bytes through the model (and 3 /
  8 row bands in lockstep); B: the port's em_render_context_001DDE10 over
  the captured EE state (every byte it writes and the walked GIF bytes equal
  to the original's); C: the pass walk and em_gs_world (1, 2, 3, 8
  workers; also with every block counted as an upload). All bit-exact:
  section 6.2.
- **The level smoke** (`check_chain_page`, LEVEL_SMOKE.md): full route 13,013
  pages drawn (12,573 world frames, the rest empty status and tear-down
  frames; 4,122,371 sprites, 1,314 triangles, 1,256 lines; at step DOF,
  2026-10-09, through a01_arrival: 13,571 pages, 13,131 of them world
  frames each with the pass, whose 105,048 sprites are counted apart from
  the 4,261,106 others), each frame's 001DDE10 CALL drawn as the pass in the
  Original profile (8 sprites, marks 1, 3, 5, 7, 8, 32 DIRECT packets, the
  page's last; re-walked in the model's pass mode on the sampled pages: 37
  on the route) or, with the GPU renderer, the only CALL walked over, the
  lane program run six times in
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
- 001DDE10's depth-of-field pass is drawn by the GS model only (the
  Original profile); the GPU renderer walks over it. Its pixels are
  bit-exact against 7 captured frames of the original (section 6.2); a
  blend reaching pixels of a whole-line field is not among them
  (GS_EXACT.md 8.8).
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
test-dof-pass-reference:     python3 tools/test_dof_pass_reference.py (section 6.2)
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

## 12. Call mode: a channel list a frame list CALLs (the background, 2026-10-02)

`em_chain_page_run_call` walks a channel list the frame list CALLs (render
channel 3's at context +0x1D8, 001E1E60's) with list mode's machinery and
registers, and ends at the list's own top-level RET (transferred), where
the DMA would return to the caller; an END faults. Only call mode accepts
the grid program: the MPG of 79 instructions from ELF 0x0023C9B8 (inside
the packet 0x0023C990 the list CALLs) loads it, and MSCAL 0 runs
`em_vu1_grid_program_mscal` (`counts.mscal_grid`). The level background
(em_background_live, BACKGROUND.md "Port") is its user; the reader serves
the packet from the effect-table export. Verification:
test-background-reference (the program's translation against the original
microcode) and the level smoke's check_background (the walk's triangles
against the original program over the port's upload).
