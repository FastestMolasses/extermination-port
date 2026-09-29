# The AREA11 snow: weather actor 001E55F0, tiles 001E67C0, the snow program D_00233800

The snow is drawn from its original packets on the chain page (chain C8b
step FLAMESNOW, 2026-09-28; CHAIN_PAGE.md section 6.1). Every frame the
weather actor builds its channel-3 list of 108 tile requests, the frame
close's 001E0D70 CALLs that list into page D_007635C0, and the page consumer
runs the snow program's translation on every tile. No emulator
implementation source was used.

The owner's SCUS_971.12 SHA-256 is
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

| Original | Port | Role |
| --- | --- | --- |
| 001E55F0 | `em_weather.c`, `em_snow_runtime.c` | the weather actor: intensity controller; state 1 reads the channel-3 cursor, runs 001E67C0, closes the list (RET) and stores it at context +0x2520 (001D2DE0(0, start)) |
| 001E67C0 | `em_snow.c` | the AREA11 tile emitter: 6 rows x 6 depth steps x 3 vertical steps = 108 tiles; its fog programmer calls 0021B9A0(2), (3, 0, 300), (1) run on the render context |
| 001CFAE0 | `em_weather_packets.c` | a tile's 0x58-byte draw state (byte-matched C) |
| 001CFFE0 | `em_weather_packets.c` | a tile's five DMA packets at the channel cursor (NEARMISS C; the .s followed) |
| 001E0D70 | `em_render_context.c` (live since the render context step) | the frame close's CALL of +0x2520 into the page, slot 0xFFB |
| D_00233800 | `em_vu1_page_programs.h` (`em_vu1_snow_program_mscal`) | the snow program: generation, trajectory, projection, colour (CHAIN_PAGE.md section 3) |
| 0011E2A8 | `em_sdk_math_original.c` | the SDK sinf of each row's drift wave |

## The snow program (D_00233800)

The program packet uploads 256 + 81 instructions (ELF 0x00233828 to micro 0,
0x00234030 to micro 0x100), the 80-scalar lookup (ELF 0x002342BC, VU qwords
0..0x4F, all four lanes) and 17 constant rows (0x6E..0x7E). A tile's packets
upload the projection rows (0x6E..0x7C), the descriptor D_00255170 (0x50..0x58)
and VU59 (phase, colour multiplier 1, fade-in interval 0.000001, seed
fraction) with the tile matrix (0x5A..0x5D), then MSCAL 0.

Its instructions are the sprite program's (table 0x231770, the AREA11 flame's
and the effects') except five in the emission and the branch offsets that
follow from them; `em_vu1_page_programs.h` translates both as one function
with a `snow` switch. What the program does, as its instructions show:

- **Generation.** Four random advances per source particle, even when the
  age gate suppresses it: two pairs select lookup angles, and each pair XORs
  its product's mantissa into the 23-bit random state (RINIT from VU59's
  seed; local to the tile). The source fraction advances by repeated binary32
  additions. The snow flags keep the descriptor's Z direction and randomize
  X / Y. The lookup values are the original's constants, not host
  trigonometry.
- **Trajectory.** The particle's position through the tile matrix with
  gravity, its colour and size blends and the fade.
- **Emission.** A particle passes the clip test `-|W| <= X, Y, Z <= |W|` of
  the guard-band projection 001CD370(0) (a larger volume than the visible
  raster; the GS scissor handles the rest). Its GS colour keeps the
  original's operation order:

  ```
  fog_weight  = clamp(fog.z + fog.w * w, 0, fog.x)
  near_weight = min(w * 0.02, 1)             (the snow program only)
  gs_color    = FTOI0(((colour * (1/256)) * fog_weight) * near_weight)
  ```

  with w the particle's K-projected w. Each visible particle writes one
  sprite: TEX0 (the descriptor's +0x70 row), RGBAQ, ST (0, 0), XYZF2 of the
  plus corner, ST (1, 1), XYZF2 of the minus corner (FTOI4, each corner
  rounded on its own), batched by 28 and kicked.

The page's GS path draws the sprites (CHAIN_PAGE.md section 5): additive
(blend preset 2: the descriptor's kind 2), depth test GEQUAL without a depth
write, the snow's PSMT8 texture through its CT32 CLUT (a page texture:
tools/export_page_textures.py / export_disc_textures.py; the shared PSMT8
palette decode is the decomp's `docs/CLUT_LAYOUT.md`).

## The channel-3 list

`em_snow_runtime_tick_actor` (the weather node's behaviour, tick_weather in
em_area11_bindings.c) runs, for a state-1 call with intensity above 0:

1. the channel-3 cursor (context +0x1C) as the list's start;
2. 001E67C0: 0021B9A0(2, 0, 0) and (3, 0, 300.0) on the render context,
   em_snow_tiles (the 108 tiles), then for every tile 001CFAE0(state, 0,
   0x700036A0, phase, seed + 0.0001, 1.0, 0.000001) and 001CFFE0(3, 3,
   D_00255170, state) (`em_weather_packets_tile`), then 0021B9A0(1, 0, 0);
3. 001E55F0's close: a RET tag at the cursor (+3 = 0x60) and, when the start
   was not 0, 001D2DE0(0, start) (`em_rcl_001D2DE0`).

001CFFE0's packets for (kind 2, variant 3): a REF of the mode-2 blend preset
(001CB9B0(2), 8 qwords); a CALL of D_00233800; a CNT of 16 qwords (STCYCL 1/1
and an UNPACK of 15 rows to 0x6E: P from 0x70003A40, the clip projection at
the state's +0x40, K from 0x70003AC0, the context's +0xA0 fog as 001E67C0's
fog calls left it, (0, 0, the state's +0x54, 0) and the GIF tag row
D_00251260[3]); a CNT of 10 (UNPACK 9 rows to 0x50: the descriptor); a CNT
of 7 (UNPACK 5 rows to 0x59: the state's +0x44, +0x48, +0x50, +0x4C and the
tile matrix; MSCAL 0; FLUSH). Each tag writes only its QWC halfword, ID byte
and address word. 0x260 bytes per tile.

The scene manifest's `weather <flags> <config>` line loads the tables
(`python3 tools/export_snow.py --reference-ee <an AREA11 EE dump>` or `--gs
<a .p2s>`: the descriptor, the lookup and the six row records from the ELF;
the EE state only confirms the snow branch). A texture token of an older
manifest is not read.

## CPU tile emitter (001E67C0)

`em_snow.c` translates the AREA11 branch of original `001E67C0`. It emits six
rows, each containing six depth steps and three vertical steps: 108 tiles.
It advances each row's phase and drift once, and keeps the tile seed sequence
local to the call. The node passes the camera eye from before that frame's
camera update (the cutscene block) or the live eye (gameplay).

The previous readable decompilation labeled this function as an explosion and
sound spawner. Its `0021B9A0` calls configure fog, and its arguments to
`001CFAE0` were wrong: the submission is phase, random fraction plus 0.0001,
colour multiplier 1 and fade interval 0.000001, which 001CFAE0 packs into
VU59's order above.

The emitter's own arithmetic (001E67C0's sums, products, quotients and
conversions) runs on the measured EE model through em_ee_float.h
(docs/EE_FLOAT_MODEL.md section 5b), the tile colour is the SDK 00102900
(em_sdk_vu0.h), the tile rotation the recovered 001029E8 polynomial and
square root, and each row's drift wave the SDK sinf 0011E2A8
(em_sdk_math_original over the one SDK context, since FLAMESNOW; it was the
host `sinf` before). Its divisions are the snapshot-confirmed rounded
binary32 EE divisions: truncating the random fraction division would change
49 of the latest 108 seed mantissas.

## Verification

- **`make test-snow-tiles-reference`** (tools/test_snow_tiles_reference.py,
  quick ~3 s): executes the original 001E67C0 (and the SDK sinf 0011E2A8 it
  calls) and compares em_snow_tiles (with em_sdk_math_original's 0011E2A8)
  on 24 (48 in full) varied inputs: every tile record (matrix, VU59,
  descriptor) and the controller state, exact. On 4 (48) of them the
  original 001E67C0 runs again with its draw requests executed (001CFAE0,
  001CD370, 001CFFE0, 001CB9B0) over a render context seeded from route
  capture 10; `em_weather_packets_tile` writes the same 108 x 0x260
  channel-3 bytes and leaves the same cursor. With
  `--reference-ee ../Extermination/build/startup-reference/opening_ee.bin
  --reference-tiles ../Extermination/build/weather_reference/original_tiles.json`
  it also reproduces the opening snapshot's two captured frames: 216
  parameter qwords, 216 colours, the six phases, and every tile matrix
  exactly (camera sample 134.0 for the latest tiles).
- **`make test-weather-reference`**: 001E55F0's controller (em_weather.c).
- **`make test-chain-page-reference`**: the snow program's translation against
  the ORIGINAL microcode on every captured page's 108 snow MSCALs (quick: one
  page; full: all 15), synthetic snow batches, every branch both ways
  (CHAIN_PAGE.md section 8).
- **The level smoke** (`check_chain_page`): every world page holds the
  weather's CALL at the list its frame closed and runs its 108 snow MSCALs;
  the sampled pages re-walked with the ORIGINAL microcode (the snow program
  on every tile) draw the port's primitives; in the camera-exact snapshots
  10 and 14 the tile packet 3 (P, the clip projection, K, the fog, the GIF
  tag row) equals the capture's weather list's.

## Limits

- The snow's particles follow the weather's state-0 seed and phases (rand())
  and its intensity walk, which the port's stream does not hold at a
  capture's position (RAND_ORDER.md): the tiles' VU59, colour rows and
  matrices are compared with the captures only in the opening snapshot's
  reconstructed frames, and the camera rows in the camera-exact beats.
- The other weather branches (001E5AC0, AREA21's cases) are not connected: a
  weather line whose flags are not the snow branch does not load.
- 001E67C0's writes of the scratchpad 0x700036A0.. and 0x700038A0.. (its
  working matrix and vector) are not modelled; no first-level reader of them
  after the weather's call is known.
- 001E55F0's D_008106BF store (00128250 of the intensity) is not part of
  em_weather's frame: its one reader, 001DE920, is not reached on the first
  level (em_render_context_live binds it to a fault).
- Exceptional VU arithmetic (an exponent-255 operand) faults instead of
  being emulated (CHAIN_PAGE.md section 3).

Original images, instruction dumps, binary tables and snapshot data remain
local ignored build / assets files; none are embedded in the source or tests.
