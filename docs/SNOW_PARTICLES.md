# Original snow particle generator

`src/game/em_snow_particles.c` translates the particle generation and trajectory
instructions in the original VU program submitted by `001E67C0`. The controller,
tile emitter, textures, projection matrices, and graphics submission remain
separate. No emulator implementation source was used.

The owner's SCUS_971.12 SHA-256 is
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.
Relevant original addresses are:

| Address | Role |
| --- | --- |
| `00233828` | Color scale, particle fraction step, random seed initialization |
| `00233AC0` | Random direction and offset; flags; age rejection |
| `00233E40` | World transform, gravity, color/size interpolation |
| `00234110..00234240` | Depth/fog attenuation and integer GS color conversion |
| `002342BC` | 80 scalar lookup values, uploaded to VU qwords `00..4F` |
| `00255170` | Nine-qword snow descriptor uploaded to VU `50..58` |

The lookup values are original six-decimal constants, not host trigonometric
evaluations. The caller supplies the extracted table and descriptor. VU `59`
contains **phase, color multiplier, fade-in interval, seed fraction**; VU
`5A..5D` contains the tile transform. These are not the argument ordering
previously described in the old readable emitter C.

The generator preserves four random advances per source particle, even when
the age gate suppresses that particle. Two pairs select lookup angles; each
pair also XORs its binary32 product mantissa into the 23-bit random state.
This state is local to the tile. The source fraction advances through repeated
binary32 additions, rather than computing `index/count` independently. The
snow flags preserve the descriptor's Z direction while randomizing X/Y.

The final GS color helper preserves the original operation order:

```
fog_weight = clamp(fog.z + fog.w * clip_w, 0, fog.x)
near_weight = min(clip_w * 0.02, 1)
gs_color = truncate(((color * (1/256)) * fog_weight) * near_weight)
```

Its output is an integer GS channel, not a normalized host shader color. It
expects a particle that passed the original clip test, with positive clip W.
The renderer must retain the original additive blend and depth behavior.

## Verification

`make test-snow-particles-reference` executes only the relevant original
instructions from the locally owned ELF and compares the readable C output.
The independent VM decodes the instruction words, including delayed MAC flags
and division; it does not translate disassembler strings or call the C helpers.
It checks 1,280 combinations of all 32 flag values, five particle counts, and
eight phases across age boundaries. All 8,480 emitted position/color/size
records match byte for byte. Another 1,000 cases compare the projected color
helper against the original arithmetic instructions.

An optional immutable opening snapshot supplies an independent runtime check:

```
python3 tools/test_snow_particles_reference.py \
  --reference-vu ../Extermination/build/weather_reference/vu1Memory.bin \
  --reference-micro ../Extermination/build/weather_reference/vu1MicroMem.bin
```

The captured VU's last effect has 80 particles, rather than the snow descriptor's
20. Its first 1,912 microprogram bytes are identical to the snow generator;
the check verifies this before comparing data. All 24 final-batch particles
match the original captured positions, colors, and sizes: **1,152 identical
bytes**, across both readable C and the independent instruction VM. All 80
lookup values also match the original VU memory's scalar components exactly.

`make test-snow-particles` runs 8,960 bounds/finite-output cases with AddressSanitizer
and UndefinedBehaviorSanitizer. The module also compiles with warnings as errors.

Arithmetic follows the operation order and finite binary32 truncation described
in the [Sony VU User's Manual, version 6.0](https://studylib.net/doc/25815876/vuusersmanual.158394566),
sections 1.1.2, 2.2, and the random instruction references. The captured runtime
comparison provides direct evidence for these inputs. Exceptional VU arithmetic
and every possible hardware rounding edge case are not emulated, and this
generator's checks do not establish full-renderer pixel equivalence.

Original images, instruction dumps, binary tables, and snapshot data remain
local ignored build/assets files; none are embedded in the source or tests.

## Native scene integration

`export_snow.py` exports the descriptor, lookup, six row records and resident
texture from the user's original ELF/AREA11 state. The scene's `weather`
record loads them through `em_snow_runtime`. Weather advances before the
camera update, matching the previous-camera sample used by original tile
submission. Scene unload/reload clears both state and texture. Other weather
branches are rejected until their original behavior is recovered.

The Metal path draws the recovered particles as textured additive sprites,
with depth testing and no depth writes. Projection retains clip W for size
and VU color attenuation. Texture sampling spans the original whole sprite.
GS subpixel raster quantization and full pixel equivalence remain unverified.
Global random-consumer ordering is still incomplete, so particle locations
are not claimed to match an arbitrary captured frame.

The first visual smoke found a shared PSMT8 palette decoder bug: it omitted
the physical PSMCT32 address decode before the CSM1 index permutation. The
corrected exporter produces the original soft solid flake rather than the
erroneous ring. See the decomp repository's `docs/CLUT_LAYOUT.md`.

`make test-snow-runtime` exercises240 ticks using the real exported assets
under ASan/UBSan, checks11,479 visible particle submissions for bounded finite
data, and verifies clear/reload, missing assets, unsupported weather and GPU
texture-allocation failure. These are integration checks, separate from the
original-instruction generator/emitter comparisons above.

## CPU tile emitter

`em_snow.c` translates the AREA11 branch of original `001E67C0`. It emits six
rows, each containing six depth steps and three vertical steps: 108 tiles.
It advances each row's phase and drift once, and keeps the tile seed sequence
local to the call. The renderer supplies the original scene tables and the
camera eye from before that frame's camera update.

The previous readable decompilation labeled this function as an explosion and
sound spawner. Its `0021B9A0` calls actually configure fog, and its arguments to
`001CFAE0` were wrong. The actual submission is phase, random fraction plus
0.0001, color multiplier 1, and fade interval 0.000001; that helper packs them
into the VU parameter order documented above.

The native emitter uses snapshot-confirmed rounded binary32 EE divisions.
Truncating the random fraction division changes 49 of the latest 108 seed
mantissas, altering the entire VU random sequence for those tiles. Both
captured buffers now match all 216 complete parameter qwords and color qwords,
plus both sets of six resulting phase values (48 bytes). This evidence does
**not** distinguish the controller's intensity/127 division mode: the latest
intensity produces the same quotient either way; both quotient modes for the
previous intensity also produce the same observed colors and phase advances.

The tile rotation uses the recovered `001029E8` polynomial and square root,
rather than host sine/cosine for the matrix. The drift waveform still uses
host `sinf`; this is an explicit remaining fidelity limitation.

`make test-snow-tiles-reference` executes original `001E67C0` instructions and
compares 48 varied inputs, 5,184 full tile records, and final controller state.
The flow comparison shares host `sinf` as an intercepted dependency. A separate
pass executes the original SDK sine instruction tree: 4,842 of 5,184 resulting
matrices match exactly; the maximum remaining component difference is
0.00006103515625 world units. These are different claims and are reported
separately.

The optional DMA comparison reads the immutable opening EE snapshot and
`original_tiles.json`. The original camera track's sample134.5 matches the
saved camera globals byte for byte; latest tile matrices instead correspond
to sample134.0. Using that original previous sample reduces the largest
translation discrepancy from 0.003875732421875 to 0.00006103515625. Remaining
basis discrepancy is at most 0.00000025331974029541016. Reconstructing prior
drift from a truncating addition is not generally unique, so these matrix
errors are measured, not treated as proof of an exact pre-render state.

```
python3 tools/test_snow_tiles_reference.py \
  --reference-ee ../Extermination/build/startup-reference/opening_ee.bin \
  --reference-tiles ../Extermination/build/weather_reference/original_tiles.json
```
