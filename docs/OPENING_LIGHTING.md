# Original opening vertex lighting

The native character renderer previously interpolated normals, normalized the
result, and evaluated lighting in its fragment shader. The original VU face
and body programs evaluate each authored vertex, submit integer RGBAQ colors,
and let GS interpolate those colors. Matching the light equation alone did
not make the former shader faithful.

`em_lighting.c` now performs the original finite arithmetic on the CPU. Metal
uploads the resulting per-vertex integer RGB values and interpolates them in
screen space with `center_no_perspective`. Textures still use their existing
perspective interpolation. No light intensities or directions were tuned to
screenshots.

The relevant original face instructions are `0023C5D8..0023C690`; the body
counterpart is `0023C878..0023C928`. Both perform these steps:

1. Multiply the authored normal by the bone-local light matrix. Do not
   normalize that normal.
2. Clamp each directional intensity to zero from below.
3. Multiply the three intensities by their color rows, then add the ambient
   row carrying the `8388608` float bias.
4. Clamp the biased values to float bits `4B0000FF`. PACKED RGBAQ consumes
   the low eight bits of each output word as an integer channel.

The original matrix uploaders `001C7420` and `001C7900` normalize each bone
basis column before composing its light matrix. The new helper preserves
that operation order and separately truncates bounded binary32 products and
sums. It does not replace the original operation sequence with a normalized
interpolated normal. The captured face GIF tag has PRIM `3C`: triangle strip,
Gouraud shading, texture and fog enabled. GS DDA interpolation occurs in the
primitive coordinate system; see the
[Sony GS User's Manual](https://www.scribd.com/document/784545197/GS-Users-Manual),
sections 3.4.9–3.4.10 and the PRIM register.

The face and body require different rig inputs. `001D88B0`, called for the
face, enables camera fill and passes a null owner to `001D8340`, bypassing
dynamic point-light folding. The body uses `001D89D0` with the actual actor.
The native opening mesh retains its existing combined geometry, but face
vertices now carry explicit metadata selecting the separately published face
rig. The game builds that rig with camera fill enabled and point-light folding
disabled. Setting a body rig clears the previous face override.

Original actor fields also select light-reference bones 1, 2 and 0 for
Dennis, Roger and the attached equipment respectively. Both humans enable
camera fill; the equipment does not. The opening draw now supplies those
verified inputs rather than using bone 1 for all three records.

The opening's scene manifest previously rounded directions to four decimal
places. The companion `export_level.py` now reads the original SDK polynomial
coefficients at `00241100`, follows the `001029E8` finite rotation sequence,
and writes nine significant digits. Both AREA11 world directions round-trip
to the exact captured binary32 values. Model and animation asset data is
unchanged.

Validation uses the owner's original ELF and immutable opening EE snapshot:

- Every one of the 3,360 original face records passes both original face and
  body instruction slices: 6,720 comparisons against the C kernel. Another
  1,000 varied normal/light/color cases cover clamps and non-unit normals.
- The matrix builder reproduces all 48 consumed normal-matrix bytes and all
  64 color-matrix bytes for both captured faces: 224 bytes total. The original
  rig export independently matches 24 captured directional-light bytes.
- The actor test checks all 850 Dennis and 701 Roger face vertices select the
  separate rig. Focused ASan/UBSan checks cover basis scaling, the original
  integer conversion, clamping, collapsed bases and invalid matrices.

The audit selects the current face DMA upload by matching its morph weights
against live actor state, rather than selecting an arbitrary stale packet in
EE memory. Dennis resolves to REF `002FED50` and Roger to `002FA780`.
Using those captured matrices, the old fragment approach differs at Dennis
triangle centroids by up to 25.23 GS color units, with mean difference 1.95;
521 of 1,124 centroids differ by more than one unit. This isolates lighting
interpolation and normalization. It does not include occlusion, textures or
fog and is not a count of differing screenshot pixels.

```
make test-lighting test-opening-actor test-opening-runtime
make test-lighting-reference
python3 tools/audit_opening_lighting.py \
  --report build/opening_lighting/audit.json
```

The reference audit requires the original opening snapshot and extracted face
assets. Its report and rendered captures remain in ignored build directories.

The Metal validation preserves a frozen binary built from baseline `c3f1e8f`
and a frozen binary containing this correction. Both loaded the same current
scene assets, including the precise authored rig directions. Each reached
native frame 280, half-tick 269, camera sample 134.5 and script PC `00829140`
with identical camera witnesses, captured successfully and exited normally.
The original reference image is the isolated opening capture at the same
camera sample. These images check shader compilation and submission; they do
not establish full original raster equality. `build/opening_lighting/`
contains `before_frame280.png`, `final_frame280.png`, the logs, frozen
binaries and a SHA-256 receipt in `visual_validation.json`.

## Remaining scope

The original AREA11 point-light pool is now recovered in
[AREA11_POINT_LIGHT.md](AREA11_POINT_LIGHT.md). Its auxiliary placement table,
color/intensity, random angle update and dynamic fold replace the former
guessed steam light. The face rig bypasses it as before. Whole-game RNG call
ordering remains a fidelity dependency, so native flicker angles are not
claimed to equal an arbitrary original captured frame.

The ordinary native rig currently supplies identity actor RGB to the matrix
builder. `em_lighting_actor_rgb` now reproduces the 001D8690 multiply and
`em_lighting_fold_gate` reproduces 001D8270. Both are verified against the
AREA11 actors in [ACTOR_LIGHTING.md](ACTOR_LIGHTING.md), but the caller does
not use them yet. Existing post-draw tint handling is retained; self-glow
(actor+2 bit 0x40) before the color clamp is not claimed complete. The
rig-less shader stand-in is deleted: a normal mesh drawn without a rig is
rejected. Existing texture
alpha/blend handling and fog remain separate from this RGB correction. Native
bone/camera arithmetic, quantized GS raster positions, GS interpolation
precision and occlusion against unquantized native geometry also remain
explicit fidelity dependencies. Vulkan and D3D12 still have their existing
unimplemented rendering backends.
