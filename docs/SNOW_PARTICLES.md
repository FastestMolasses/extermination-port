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
with depth testing and no depth writes. The verified projection helper now
uses the live camera, preserves the original guard-band test and independently
quantizes both GS corners. The backend retains the original reversed ST
orientation and converts quantized GS depth to the existing native depth
convention. Full raster/pixel equivalence remains unverified.
Global random-consumer ordering is still incomplete, so particle locations
are not claimed to match an arbitrary captured frame.

The first visual smoke found a shared PSMT8 palette decoder bug: it omitted
the physical PSMCT32 address decode before the CSM1 index permutation. The
corrected exporter produces the original soft solid flake rather than the
erroneous ring. See the decomp repository's `docs/CLUT_LAYOUT.md`.

`make test-snow-runtime` exercises240 ticks using the real exported assets
under ASan/UBSan, checks27,476 quantized particle submissions, including
15,993 centers outside the viewport but inside the original guard band, and verifies clear/reload, missing assets, unsupported weather and GPU
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

## Projection, clipping and GIF submission

`em_snow_projection.c` independently translates original VU instructions
`00233FC0..00234270`. Its input is the actual VU6E..7C projection upload:
three matrices, fog, depth bias and GIF tag. The immutable opening EE dump
contains this upload immediately before each snow descriptor packet. It is
not inferred from the last effect left in VU memory, which is another kind.

`em_snow_projection_matrices` accepts the original Y-down, +Z-forward view
matrix and the current zoom. A native Y-up, -Z-forward view converts by
negating its Y and Z rows. Original `001D2960` builds the extent projection
with `P00=0.8f*zoom`, `P11=0.5f*zoom`, center `(2048,2048)`, depth coefficients
`0x3F664CB3` and `0x49CCCCCC`, and W=forward depth. `001CD370(0)` selects the
1280 by 560 guard-band projection produced by `001D2D20`, with divisors640
and280 and fixed near0.1/far16711680. The native builder preserves the
observed depth pair1/-0.2. It is not a general emulator of EE scalar addition:
naively truncating far-minus-near after full-precision host subtraction gives
a different result because original operand alignment matters.

At the captured zoom1011.6609497070312, actual P00/P11 are809.3287353515625
and505.8304748535156. P00 uses the original binary32 literal0.8f and truncated
multiplication. Zoom divisions use rounded binary32 results; matrix products
and additions use the separately tested finite VU arithmetic. Constructing
all three matrices from the captured original view and zoom reproduces all
192 bytes. This does not establish that the port's live view/zoom construction
is itself identical to the original SDK.

The VU center test is `-abs(W) <= X,Y,Z <= abs(W)`, including equality. It
uses a larger volume than the visible raster: native visible NDC corresponds
to twice original clip X and minus1.25 times original clip Y. In the216
captured snow tiles, the original submits261 of4,320 particles;146 submitted
centers are outside the visible viewport but inside this guard band. The GS
scissor handles their eventual raster coverage.

The helper returns the raw FTOI4 XYZF2 qwords, after independent rounding of
the plus and minus corners. PACKED XYZF2 extracts X/Y from low16 bits,
Z from bits4..27 of the third word, and F from bits4..11 of the fourth word.
The final decoded GS Z is therefore approximately `trunc(B+A/W+biasZ)`;
the integer is not sixteen times that depth. The six-register GIF tag
supplies TEX0, RGBAQ, ST, XYZF2, ST, XYZF2. ST `(0,0)` belongs to the
**plus** corner and `(1,1)` to the **minus** corner. Both axes consequently
run opposite to a conventional top-left `(0,0)` sprite. RGBAQ latches Q=1
before the ST pair; the intermediate ST Q operand does not divide texture
coordinates by zero. These packing rules follow the
[Sony EE User's Manual, GIF PACKED formats](https://manuals.plus/m/55bdc71d3aebc33752a7dad6526b78248b6c258294fcf8a3ebed1f010da22365.pdf),
section3.4.1, pages153–154.

For native submission, decode each corner independently:

```
gsX = (rawX & 0xffff) / 16.0
gsY = (rawY & 0xffff) / 16.0
gsZ = (rawZ >> 4) & 0xffffff
ndcX = (gsX - 2048) / 256
ndcY = -(gsY - 2048) / 112
```

These GS half extents account for the512 by224 interlaced field raster
presented at4:3. Preserve each corner's ST association when constructing
native triangles; deriving one unquantized center and width loses the
independent1/16-coordinate rounding.

The native scene geometry retains its0..1 depth convention. If its projection
coefficients are `p10` and `p14`, an affine conversion compatible with that
convention is `nativeZ = -p10 + p14 * (gsZ - B - biasZ) / A`, where
`A=1677721.5` and `B=0.8996078372001648`. Use the actual native coefficients
rather than a newly rounded near/far reconstruction. This keeps the original
snow depth quantization, but the rest of the geometry still uses native
unquantized depth. The oracle establishes the original packed outputs, not
identical raster/depth comparisons against every native surface.

`make test-snow-projection-reference` runs1,055 synthetic boundary and random
cases against the original instructions. The optional immutable snapshot
adds all4,320 original snow particles, independently generated by those
instructions, for5,375 total cases. Clip coordinates, both corner qwords,
color and ST compare byte for byte (151,664 bytes in the snapshot run).
The extended bounded VM also passes the existing1,280-case generator,
1,000-case color and24-record captured-output regressions.

```
python3 tools/test_snow_projection_reference.py \
  --reference-ee ../Extermination/build/startup-reference/opening_ee.bin \
  --report build/weather_reference/projection_validation.json
```

The projection oracle models finite binary32 inputs and valid nonzero
reciprocal denominators. Exceptional VU DIV/FTOI behavior, complete GS raster
coverage and the live native camera's upstream arithmetic remain separate
fidelity work.
