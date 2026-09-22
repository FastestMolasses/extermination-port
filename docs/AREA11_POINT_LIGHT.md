# Original AREA11 auxiliary point light

The previous port invented a faint steam light at `(452,279,278)` with
intensity 14 and tuned warm RGB. The original light is an independent room
resource. `001F6D60` selects the auxiliary placement list `0025D5A0` for
AREA11/sub0; `001F6E40` releases old handles and registers that list through
`001F6640` and `001D7FA0`. The earlier exporter only inspected the primary
selector `001F6760`, so its claim that the snow area had no room lights was
incomplete. The nearby overlay actor `008235F0` does not register this light.

The single record uses color preset 2 at `0026EB90`. It places a type 1 light
at original binary32 coordinates `(451.600006,279,277.200012)`. Registration
uses multiplier 1 and adder 0. Adoption scales the complete color vector by 128,
giving RGB `(1658.879883,414.719971,103.679993)` and intensity 414.719971.
These position/color bytes equal the immutable original opening snapshot.
There is no authored radius; `001D8340` uses inverse squared distance, with
the denominator clamped to at least 1.

`em_point_light.c` preserves the original 32-slot active/staging lifecycle:

- `001D7BB0` clears active type/handle/position/color fields and the counters.
  It preserves the other slot bytes. Registration writes staging slots.
- `001D7C30` first updates active slots. Intensity becomes
  `max(0,adder + intensity*multiplier)`; RGB is multiplied by the multiplier.
- Type 1, multiplier 1 lights outside key 0F00 consume two original RNG outputs.
  Each angle adds `-.001 + .002*(float(random)*2^-31)` and clamps to
  approximately ±.03141593 radians. The original SDK polynomial and matrix
  multiplication build the X/Y rotation; host sine/cosine are not substituted.
- Other active types/multipliers and key 0F00 use identity rotation. Pending
  lights are adopted afterward into the first inactive slots. Adoption zeros
  angleXYZ while preserving angleW and the slot's prior matrix. New lights
  therefore begin their random update on a later frame.

The body light fold follows original instructions: seed the direction with
weighted camera fill, add each rotated offset scaled by10k, add light color
scaled by2k, then normalize the final direction. Here
`k = .1*intensity/max(distance_squared,1)`. Face lighting continues to bypass
this fold through its separate original rig.

Native integration loads an exported `point_lights.emlp` resource and ticks
the original pool at the `001D1C50` phase: after ordinary player update and
before pooled actors, or before actor updates during the opening. The shared
`em_random_next` supplies its RNG calls. The guessed steam light registration
is removed. Legacy steam audio and billboard FX remain separate, unaudited
approximations.

## Validation

`tools/test_point_light_reference.py` executes the original EE instructions,
including both SDK VU0 rotation helpers, directly from the owner's ELF. It
compares 256 complete 32-slot updates, 780 RNG calls, 256 dynamic folds and 40
registrations, including capacity and allocator wrap. The original room
reset and both table selectors also execute, producing the same auxiliary
registration. All active/staging slot bytes are compared.

The saved original angles reconstruct all 64 bytes of the captured flicker
matrix. Using the captured light and Dennis's actual bone 1 anchor reproduces
the unique current body DMA color row at `002FE060`, all 12 RGB bytes. This
row distinguishes round-to-nearest EE DIV.S from truncation; the latter does
not match. Other finite products/sums and VU reciprocal operations retain
the independently tested truncating path. This is not a claim about every
exceptional EE/VU floating-point input.

```
python3 tools/export_point_lights.py \
  --reference-ee ../Extermination/build/startup-reference/opening_ee.bin
make test-point-light test-point-light-reference
```

The exporter only reads the owner's original executable; optional snapshot
validation checks placement and color. Its generated assets, provenance JSON
and reports remain ignored. ASan/UBSan tests cover staging/adoption, RNG gates,
zero-distance normalization, capacity, and malformed/missing resources.

A frozen native build loaded the original registration and captured opening
frame 280 successfully, exiting with code 0. Its half-tick 269, camera sample
134.5, script PC and all camera values equal the previous lighting capture.
`build/point_light_reference/visual_validation.json` records the witness and
binary/image hashes. Actor/runtime tests also pass through normal and skipped
opening teardown. This smoke check verifies integration, not full GS raster
or whole-game RNG equality.

## Remaining scope

Exact whole-game RNG call ordering and earlier frame counts remain unverified.
Native starts the original initializer and consumes the shared RNG; it does
not replay an arbitrary captured angle as a universal initial state. The
original angle-update formula and supplied-state output are verified even
when the native global stream differs. Camera-fill composition and native
bone arithmetic retain their separate rounding dependencies. Opening Dennis,
Roger and equipment pass the original body-light eligibility gate; eligibility
for other native actor classes remains unaudited. Legacy primary lamps in
other scenes retain their previous path. Texture/fog/raster behavior is
separate from this light input correction.
