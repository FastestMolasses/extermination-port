# The chain page D_007635C0 drawn: DMA, VIF1, the two VU1 programs, GIF and the GS (WP-13)

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

Files:
- `src/game/em_vu1_page_programs.h`: the lane program (DMA packet
  D_00233290) and the sprite program (table 0x231770), translated from their
  VU1 microcode (section 3).
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
001CB8A0): 386 primitives: 369 sprites, 5 triangles of fans, 12 lines of line
strips; 86 XGKICKs and 208 DIRECT packets. By producer:

| Producer | Shape |
|---|---|
| 001F0720 (six lanes a frame) | blend REF (mode 1), CALL D_00233290, three UNPACK packets and an MSCAL: the lane program. No route slot is active (the countdown +0x58 is 0 or below), so the program draws nothing on the route |
| 001CFBE0 (head sprites, puffs; and in the captures the AREA11 flame 001D04B0) | blend REF, CALL 0x231770, the UNPACKs of the projection rows, the source block (a REF of 9 qwords) and the parameters, MSCAL: the sprite program's sprites (the PRIM of the tag row 001CFBE0 uploads: 0x56 sprite, textured, blended, or 0x76 with fog) |
| 001CD520 (glow markers, equipment sprites) | blend REF, a REF of D_00251220 (the DIRECT code and GIF tag) and 6 qwords of data: one sprite (PRIM 0x76: textured, fogged, blended) |
| 001F0A60 (the pickup glint) | DIRECT 13: two line strips of three vertices (PRIM 0x6A: Gouraud, fogged, blended, untextured); 001CB900 appends the blend REF after the packet, so the DMA sends the REF first (slot lists run newest first) |
| 001CE300 (the decal) | the blend REF, the TEX0 packet 001CB950 writes, the fans (PRIM 0x7D) |
| page CALLs into the packet arena | the object units 001CAAC0 depth-sorts, 001DDE10's four-sprite frame-copy pass (slot 0xFFF), the weather's 001E0D70 kick (slot 0xFFB): see section 6 |

## 3. The two VU1 programs (em_vu1_page_programs.h)

Micro addresses are instruction indices of the uploaded program. Both are
entered by MSCAL 0.

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
REPEAT, TFX MODULATE with TCC 1; fog (C * F + FOGCOL * (255 - F)) >> 8 with
the frame's FOGCOL; the blend ((A - B) * C >> 7) + D with COLCLAMP on the
frame pixel (framebuffer fetch), RGB only. The GS sprite takes Z, F and RGBA
from its second vertex and S / Q, T / Q at each corner, affine across the
rectangle. Every other state is refused (-1, the scene faults): PRIM with AA1,
FST, CTXT or FIX; an untextured sprite or triangle, a textured line; ABE 0;
TEST other than 0x53001; COLCLAMP other than 1; TEX1 other than 0x60; CLAMP
other than 0; a TEX0 not PSMT4 / PSMT8 through a CT32 CLUT with TCC 1 and
MODULATE, or not registered; a state the page did not set; FGE without the
frame's fog.

**Two premises**, stated because no capture shows them:
- **The frame's Q.** 001CD520's and the sprite program's packets write RGBAQ
  before ST, so their first sprite takes the Q the GS held from the frame's
  earlier draws. The port does not model the frame's GS traffic; such a
  vertex (q_known 0) is drawn with Q = 1.0, the Q every later sprite of those
  packets carries (their second ST). Counted: 42 vertices over the 15
  captured pages, 19,276 over the full route (about 1.5 per page).
- **The inherited VIF cycle.** The lane program's constant UNPACK (14 rows to
  row 0) comes before the page's first STCYCL, because its packet's STCYCL
  sits in its DMA tag (section 2). The cycle is then the frame's, taken as
  CL == WL: the only setting under which the program finds its rows 0..13 in
  one block. Counted (15 UNPACKs over the captured pages).

## 6. What the page walks over, or never holds, in the port

- **001DDE10's four-sprite pass (slot 0xFFF).** Every world frame (flag 1)
  001DDE10 CALLs a channel-3 packet: GS environment REFs of the other banks
  (FRAME / ZBUF / TEST / CLAMP), 001D6C90's texture-from-frame packets and
  four sprites that sample the frame. Its look is not reproduced (it needs
  the frame buffer as a texture). The consumer walks over that one CALL,
  whose address the render context records at 001CB760(0xFFF000)
  (`em_rcl_page`), and counts it; the smoke asserts it is the only CALL
  walked over (12,573 over the full route). It was not drawn before either.
- **Never in the port's page:** the weather's pending kick (001E0D70 reads
  context +0x2520, which the port's weather 001E55F0 does not write: the snow
  draws through em_snow_runtime), the object units 001CAAC0 depth-sorts (not
  bound), and the AREA11 flame (owner 008235F0's 001D04B0 is not bound: the
  flame draws through em_area11_effect_runtime, whose `em_effect_sprite_project`
  and `em_snow_particles_generate` are a second, partial translation of the
  sprite program; reducing that duplicate needs the flame owner on its
  original record with 001D04B0 bound, after which the page draws the flame
  and those two helpers retire). Any of them reaching the page would fault
  (an unknown CALL into the arena).
- **The draw order against those two:** the port draws the snow and the flame
  before the page; the original sends them inside it (the weather's kick at
  slot 0xFFB, the flame at its depth slot). All three blend additively or
  with 0x44, so only overlapping pixels can differ.

## 7. Binding (live)

- **001D1EA0's kick.** em_render_context_live wraps the frame close's 001CB800
  (the start = 001CB800's base, D_00810E80 read as it does) and 001DDE10's
  001CB760 (the slot-0xFFF target); `em_rcl_page` hands both out once per kick.
- **em_chain_page_live_draw**, in frame_close_out at the page's position: in a
  world frame after the level, the walked and post-step units, the shadow's
  passes, the snow and the AREA11 effect, before the fog is switched off; in a
  status frame after the status page (001D1EA0(0) kicks the page too; the
  port's status pages are empty: 418 over the default run). It walks the page
  over the render context's storage (`em_rcl_bytes`: the arena, the chain
  table, the context, the GS blocks, the .data D_00250F30..) and the
  effect-table export's ELF blocks (`em_effects_live_window`: the two program
  packets, D_00253670, D_002565E0..), then tells em_shadow_live how many decal
  triangles it drew (`em_shadow_live_page_drew`: they must be exactly the
  frame's 0015BF90 fans'), replaces the frame's Q (section 5) and draws.
- **Assets:** `python3 tools/export_effect_tables.py` again (the two program
  packets, 0x00231770 + 0xDD0 and 0x00233290 + 0x570, joined its blocks; each
  block is checked equal in every capture) and `python3
  tools/export_page_textures.py` (STARTUP.md rows 50 and 52).
- Fail-stop: a page fault, a refused primitive or a decal count mismatch
  latches `em_chain_page_live_fault` and faults the scene.

## 8. Verification

- **`make test-chain-page-reference`** (tools/test_chain_page_reference.py;
  quick ~8 s, `EM_TEST_FULL=1` ~50 s):
  - the latest page of every route capture 00..14: every MSCAL (90 lane, 59
    sprite) run by the translation and by the ORIGINAL microcode from the same
    data memory and registers: all 16 KiB of data memory, every register and
    every XGKICK's packet equal; the whole page's primitives (386) equal the
    model's, with the same DMA, DIRECT, MSCAL, XGKICK and skip counts; three
    pages (all 15 in full) walked again from random VU1 contents draw the
    same;
  - synthetic batches: 40 (600) lane batches with active slots placed around
    the captures' cameras (clipped, ADC and wrapping countdowns) and 40 (600)
    sprite batches (counts 1..90, every flags value, the 0x10 path, clipped
    particles); every conditional branch of both programs both ways; the
    timing table of section 3 asserted on every run;
  - faults: an exponent-255 word on a live lane (both programs); nine
    malformed pages (END, an unknown MPG, an MSCAL before any program, ITOP,
    REGLIST, an A+D FRAME write, an unmapped REF, no room) and one clean fan;
  - the preset bank equal to every capture's bytes, and in full mode to the
    bank the ORIGINAL 001D0F20 writes when executed.
  Full sweep 2026-09-26: 690 lane and 660 sprite MSCALs, 15,944 XGKICKs,
  3,660,384 packet bytes, all equal.
- **`make test-chain-page-gpu`** (tools/test_chain_page_gpu.py, ~4 s): the Metal
  pixel at the frame centre equals the GS pixel model for five decal fans
  (the retired decal entry's cases), four sprites (additive and 0x44, with
  and without fog) and two glint lines (the one pixel the line lights in the
  centre column); six refusals draw nothing.
- **`make test-chain-page`** (tests/chain_page_test.c, ASan / UBSan): a clean
  page, the argument refusals, a CALL walked over, and 20,000 corrupted pages
  walked without a memory error.
- **The level smoke** (`check_chain_page`, LEVEL_SMOKE.md): full route 12,991
  pages drawn (12,573 world frames, 418 empty status frames; 159,801 sprites,
  1,314 triangles, 1,328 lines), the only CALL walked over each frame's
  001DDE10 one, the lane program run six times in exactly the barrel's
  frames without a lane drawn; 40 sampled pages re-walked with the
  original microcode over the port's own page bytes draw exactly the port's
  primitives, their blend presets equal to the captures'; in the camera-exact
  snapshots the glow markers drawn equal the capture's own page's (10: five,
  14: none), colour masked (rand()).
- **By eye** (build/captures/chain_page/): the cage roof (route 10's end):
  the glow marker at the pillar edge against 10's original.png
  (`port_crop.png` / `orig_crop.png`).

## 9. Limits

- Rasterization is Metal's (float interpolation, the line rule), not the GS
  DDA; no GS dump of a drawn frame exists to compare pixels with.
- The frame's Q and the inherited VIF cycle are premises (section 5).
- 001DDE10's four-sprite pass is walked over, not drawn (section 6).
- The flame's duplicate translation and the snow / flame draw order (section 6).
- The lane program's drawing path is proven by synthetic batches only: no
  route slot is active, and 001F0460, the only lane-slot writer on the path,
  faults before it (EFFECT_MANAGER.md 8.2).
- The sprites' positions and colours follow the producers' rand() order,
  which is not yet the original's, so the smoke compares the drawn
  primitives with the captures only for the glow markers.
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
