# AREA11 opening scenery evidence

These changes use the original SCUS-97112 overlay, boot ELF, model library,
and a cold-boot PCSX2 reference. The old port and older forced-state scene
exports are comparisons, not authorities. Generated assets remain ignored.

## Foreground parachute canopy

The large foreground object is the deployed canopy. Its existing native
mesh was correct; the scene manifest placed it at an unrelated captured pose.

| Original source | Value |
| --- | --- |
| AREA11 placement record | `0082A550`, record 10 in table `0082A3C0` |
| Owner behavior | `00823E80`, the opening controller |
| Model resolution | Per-area model `11`, through `001B0FD0` |
| Model source | `chunk15/f06_id98.bin + 0x9180` |
| Position | `(235.10000610351562, 230, 185.60000610351562)` |
| Yaw | `-1.9495327472686768` |
| Geometry | 209 vertices, 157 triangles |

Original playable actor `007A8E10` has model pointer `013AA8C0`. Its actor
world matrix and node world matrix agree exactly. The overlay's position/yaw
reconstruct that matrix within `1.1e-7` for sine/cosine components; all
translation floats agree exactly. Its native vertex-position set agrees
with the original model. The opening controller continues drawing this
owner after its script ends; its child-stop field does not despawn the canopy.

Regenerate the placement with:

```sh
python3 tools/export_opening_scenery.py
```

## Pickup indicator meshes

The six AREA11 boxes with behavior `00219550` allocate separate class-12
children through `001C5570(parent, color, 0x73, 1)`. Their original base color
is `(0,1,0,0.25)`. The last component controls random brightness. The child
behavior is `001C5680`; it binds the global model library through `001C2360`
and draws through `001CABA0`. They are actual small mesh strips, not light
cones, screen rectangles, or billboards.

`tools/export_pickup_lights.py` reads the original area's deferred-spawn
registry through `0024D820`, selects these owners, exports global library
model `73`, and writes explicit `pickup_light` manifest records for UIDs
`0B01`, `0B04`, `0B05`, `0B06`, `0B07`, and `0B08`. It checks the corresponding
native pickup transforms against the original records before binding them.

The model begins at `chunk27/f01_id37.bin + 0x167400` and contains 8 vertices,
4 triangles, and one 16×16 texture. All referenced vertices use bone 0.
Original actor `007ACBC0` and its three nodes all use the parent box's world
matrix. The generated model preserves the original positions, UVs, winding,
and texture; the unlit effect path ignores its normal attributes.

Regenerate with the decomp environment's existing Python dependencies:

```sh
../Extermination/.venv/bin/python tools/export_pickup_lights.py
```

The default texture source is the local cold-boot `opening_gs.bin` freeze
component; `--gs` accepts another original GS freeze capture. This input is
local evidence and is not committed.

## Original color and draw state

For each active child frame, `001F54E0` computes:

```text
random = original_rng() / 2^31
brightness = 127 + amplitude * (254 * random - 127)
actorRGB = clamp(baseRGB * brightness - 127, -127, 127)
```

Each original floating-point operation truncates to binary32. Four of the
six captured child colors differ under ordinary native rounding. The
implementation explicitly preserves this rounding. Reversing the saved
original SDK RNG state recovers six consecutive inputs, and the test checks
all six original actor color floats exactly. Mode 1 of `001D8C30` zeroes the
normal lighting matrix and encodes `128 + actorRGB` as integer GS color.

The original effect packet selected by `001D3900(3,model)` has:

| Register | Meaning |
| --- | --- |
| `ALPHA = 0x8000000068` | Source RGB plus destination RGB |
| `ZBUF` with `ZMSK = 1` | No depth writes |
| `TEST = 0x53001` | Depth GEQUAL; failed alpha test writes RGB only |
| `PRIM` fog flag clear | No world fog on this effect |

The native `em_gfx_draw_skinned_additive` keeps the mesh placement, ignores
normals, uses this additive blend, and preserves destination depth/alpha.
The effect consumes the shared original RNG once per active frame. Whole-game
RNG call order still depends on other untranslated actors.

The reference's three visible child colors produce integer GS RGB values
`(1,128,1)`, `(1,101,1)`, and `(1,132,1)`. The corresponding encoded rows occur
in original DMA memory at `006738F0`, `00673B50`, and `00673DB0`.

## Validation and remaining work

`make test-pickup-lights` checks captured color floats, endpoint color values,
the initialization frame without a draw, parent transform, RNG consumption,
and effect cleanup when its owner is taken or its scene clears. It runs with
AddressSanitizer and UndefinedBehaviorSanitizer. Metal syntax validation passes.

The canopy correction is visible in the matched opening reference comparison.
The new pickup light pass still needs its native screenshot comparison.
The following prop audit replaces those additional incorrect bindings.
This work does not establish that all static materials, actor lighting,
or pickup interactions in the existing port are faithful.

## Switch panel, elevator, and misidentified Roger

Original placement record18 (`0082A690`) allocates class `84`, behavior
`00159210`, subtype `24`, per-area model `04`, completed-bit index7. The
subtype is not a second model ID. Original actor `007AA590` resolves model
pointer `01350640`; its local bounds are approximately `6.1 × 14 × 2.11`.
The old port misidentified this as a wide grate, invented a `23.42 × 8 × 6.4`
blocking box, and moved it 12 units along world X when power changed.
Those behaviors have been removed. Its model stays at the original placement.

`00159210` creates a global model75 child with `(1,0,0,1)` base color unless
the area's completed bit was already set at initialization. After this
actor's own successful interaction script finishes, it sets child state3
and clears the child pointer. A global power-bit change by itself does
not run that state transition. `em_props_panel_complete()` exposes the
actual completion boundary; it must not be called by an unrelated flag setter.

The original successful script at `00247BA0`/`00247BE0` runs a 120-tick
entry wait, camera command3, player clip `15C`, wait10, callback `001575B0`
(sound `3EF`), wait110, and callback `001580C0` (set area bit7, sound `3EE`).
Its selection and charging UI through `00157860` remain to be translated.
The panel is no longer replaced by a fabricated console interaction at
Roger's location.

Placement record19 (`0082A6B8`) is the elevator owner `00827B10`, per-area
model `0F`, at `(224,230,250.7)`. Its original body has 566 triangles and
approximately `33.7 × 21.2 × 33.7` bounds. The source crosses an extracted
file boundary: `chunk15/f05_id97.bin + 0x66B00` continues into `f06_id98.bin`.
The old model0C placeholder has been replaced with this body.

The elevator's model10 child has four vertices and two triangles. Its
unpowered base color is `(1,0,0,.25)`. The parent's signed level at `+28`
approaches128 or0 by8 per active tick. While positive it supplies
`(0,level/128,0,.25)`; at zero it supplies red again. Its node0 world matrix
copies the elevator parent's current node0 matrix. It now uses the original
unlit additive path instead of the old ordinary opaque pickup draw.

Script callback `00828050` first chooses sound `452`/`453` and rate
`+.26666668`/`-.26666668` from `81083A`, returning without movement on its
initialization tick. It then adds the rate independently to parent Y,
player Y, and camera target Y for150 ticks. Parent `00827B10` toggles its
pose flag when the script finishes and snaps its own Y to190 or230. Native
motion now uses that exact rate and EE truncation, and permits a reverse
ride. The original full terminal script sequence is still a separate
interaction integration task.

Record8 (`0082A500`), at `(331.7,290,192.5)`, is the ordinary Roger actor
`008237E0`, resource47. Its original model `01877740` matches all328704 bytes
of `chunk15/f18_id94.bin + 0x35000`. The old `area_battery_terminal` pickup
and matching battery-insert examine line were misidentifications and are
removed from the generated normal scene. Roger's ordinary behavior must
be installed as an actor, not as a static console prop.

Regenerate these scene corrections with:

```sh
../Extermination/.venv/bin/python tools/export_area11_props.py
```

The exporter preserves exact original normal attributes instead of baking
a guessed directional light. This yields344 vertices/232 triangles for
model04 and1116 vertices/566 triangles for model0F (the earlier 333/1098
counts merged some distinct normal attributes). The two indicator meshes
each have4 vertices/2 triangles. Original model tables, not filenames or
captured guessed placements, select every exported model.

`tests/props_indicator_test.c` checks the two original captured red-channel
floats, indicator initialization and color ramps, independent panel
completion, fixed panel placement, exact elevator rate/150-tick duration,
reverse ride, and resource cleanup under ASan/UBSan. Combined with the
pickup fixtures, eight original child colors match bit for bit. The native
opening-to-control capture now includes the corrected elevator body and
red indicator. Remaining geometry differences are still under audit.

The panel interaction's original script, BATTERY confirmation and inventory
semantics are documented in [AREA11_PANEL.md](AREA11_PANEL.md). Its tested
host core stays unbound until the real UI and script handlers are ready.

## Pickup body skeleton correction

The missing green strips and flattened box were not evidence for an
invented light offset. Original global model72 has three authored nodes,
parents `[-1,0,1]`, with all geometry in slots1 and2. Node1 translates
by `(0,.880000114,0)`; node2 adds `(0,.999999523,2.21170902)`. The old
`export_pickup_items` path discarded those slots and wrote every vertex
against an identity palette. The lid and base consequently overlapped.

`export_pickup_lights.py` now also exports the original model72 body,
retaining node-local positions, normal attributes, node indices and its
rest palette. The native pickup pose builder already composes this palette
with the owner's placement. The new body has 372 vertices /220 triangles;
the EMDL has three original nodes plus its ordinary fallback identity slot.
Its entire 27,392-byte source model matches the immutable original RAM
capture. `tools/test_pickup_model_reference.py` compares all six original
owners' node worlds and confirms the exported slots and hierarchy. Small
remaining matrix differences reflect host versus EE composition rounding;
this validation does not claim bit-identical matrix arithmetic.

The apparent lower duplicate of the power panel also appears in the
original playable capture and belongs to model04's own geometry. Comparing
its placed triangles with all six static scene meshes finds no duplicates.
