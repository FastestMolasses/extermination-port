# AREA11 actor lighting (H18 / WP-13)

The AREA11 actor models were shipped with an invented light. The exporters
(`export_props.py attr_color`, `export_level.py attr_to_color`) replaced
every authored normal with `0.30 + 0.70*max(N.L, 0)` for a made-up
direction and flagged the mesh as baked vertex colour. The renderer also
kept a rig-less stand-in with the same formula for normal meshes. Nothing
in the original computes that value.

## What the original does

Every listed actor has draw method 001CAA00 at actor+0x4C. That method runs
001CA990, which sets lighting mode 0 (001D8C20(0)) and calls 001C7420. It
then submits the model after DMA CALL 0023C750, the object kernel.

- 001C7420 calls 001D89D0(actor, 70003400, 70003440, actor+0x80). In mode 0
  that routine runs these steps:
  - 001D8130 loads the room rig. 001D7B30 looks the key up in D_00251C50.
    A missing key falls back to entry 0, so the lookup never fails.
  - 001D8340 composes three slots. Slot 0 is cleared unless actor+2 bit 0x20
    (camera fill) is set. When 001D8270 passes, slot 0 is then replaced by
    the point-light fold at the light point. The light point is node
    `actor+0x98` at +0xC0, or actor+0xB0 when that byte is 0xFF.
  - 001D8690 multiplies every colour row and the ambient row by the actor
    RGB at actor+0x80..0x88, using a separate `mul.s` then `add.s` of
    8388608.
- 001D8270 decides whether the fold runs. It returns 0 for type bytes
  (+3) 03, 08, 09, 0B, 0D, 15, 16, 17, 3D and 3E. For any other type it
  returns 1 only when the model radius at `[actor+0x44]+0x20` compares
  below 30.0.
- The kernel's lighting slice is 0023C878..0023C928. It multiplies the
  record's attr xyz by the node's normal matrix, then clamps, applies the
  colour matrix and applies the 255 clamp. `em_lighting_vertex` already
  reproduces this slice (OPENING_LIGHTING.md).

The capture confirms this path. In `playable_ee.bin`, every model record of
parachute, truck, door, husk pair, crate, egg, fan and item 0B carries a
unit normal with w = 0. That is 4,064 records in total.

## What changed

- **Exporter** (`../Extermination/tools/export_props.py`):
  - `attr_color` is replaced by `attr_normal`. It returns the authored attr
    xyz unchanged. It faults when a baked transform would have to rotate
    the normal.
  - The exporter modes now write flags 0 (`NORMAL_FLAGS`).
  - The placed-props scene bake is retired, because it would bake an actor
    into a rig-less scene part.
  - New mode: `--relight-area11 ASSETS`. It rebuilds each shipped file from
    its source blob and checks that the triangle list is bit-identical
    (positions, UVs, texture slot, palette slot, in order). It then
    rewrites only the normal slot and the flags. Parents, palette frames,
    clips, texture entries and texels are copied byte-for-byte. The
    previous file is kept as `*.standin.bak`.
  - Sources: the chunk15 concat view @0x123000 for AREA11 entries 06, 08,
    09, 0B, 0D, 0E, 11, 13 and 14; the chunk27 library for the gib set; the
    office n0 table for the global crate.
- **New crate asset.** The shipped global `enemy_crate.emdl` is the office
  n0 crate (12 triangles). The four captured AREA11 crates (001551B0, type
  06) bind per-area entry 0x0D (90 triangles) instead. That entry is now
  `scene_snow/props/enemy_crate.emdl`, with texels from the opening GS
  freeze. `em_enemy.c` loads the scene-local crate first.
- **`export_level.py attr_to_color`** no longer has a stand-in. A
  normal-carrying level record without a room rig now faults. The AREA11
  snow export passes its rig and is not affected. The office n1 level
  export (`--level extract/chunk06.n1/f03_id43.bin`) now faults on its
  first normal record until its rig is supplied. The drawbridge zone
  export has no normal records; its eleven zone EMDLs are byte-identical
  to the shipped ones.
- **`export_level.py office0_bake_placed`** (the office/drawbridge
  model-table carve) called the deleted `attr_color`. It now carries
  `attr_normal`, welds on the exact normal, and faults on a rotated
  placement. Its three callers:
  - `--office0-doors` carves the two doors model-local and writes them
    with `NORMAL_FLAGS`. The door behaviour 001BC350 is the one captured
    in AREA11 with draw method 001CAA00. The east door 001BB860 has no
    capture. Against the pre-change exporter the triangle lists, texture
    entries and texels are equal (door_m15 exactly; door_m17 differs only
    by a sub-1e-5 position on one triangle, because the old colour weld
    merged two records with different normals).
  - `--office0-placed` still prints its placement census, then exits with
    "office0 placed-object scene bake retired (H18)". It baked 21 actor
    fixtures into one static scene EMDL at world matrices.
  - `--drawbridge` still writes the zones and the manifest lines (spawn,
    doorsfx, enemies). It then exits non-zero because `12_placed.emdl` is
    retired: 7 placed actor models. The 001C4820 fixture among them is
    captured in AREA11 with draw method 001CAA00. The placements are
    world-rotated (yaw-pi bridge leaves), so a baked normal would not
    stay node-local either.
- **`em_lighting`**:
  - `em_lighting_actor_rgb` implements the 001D8690 multiply.
  - `em_lighting_fold_gate` implements 001D8270.
- **Metal backend**:
  - The rig-less stand-in and the camera-fill spot exception on normal
    meshes are deleted.
  - A normal-carrying opaque draw without a rig is rejected with a
    diagnostic printed once per mesh. It used to be printed once per
    session, so the first offender hid every later one. Glow and
    additive sets still draw.
  - The level colour clamp is widened from 1.0 to 255/128 (see R11).

## Verification

`python3 tools/test_actor_lighting_reference.py` runs from the port
directory. It writes its report to
`build/actor_lighting_reference/report.json` and reads the pinned ELF, the
playable capture and the generated assets.

- It executes 001D89D0 as original instructions over the capture for all
  14 bound actors. For the 10 actors drawn in the capture, it locates the
  DMA unit. The executed colour matrix equals all 64 captured bytes, and
  `em_lighting_matrices` over the node matrix equals the captured
  normal-matrix xyz lanes. Totals: 640 colour bytes and 468 normal-lane
  bytes. The second display list is one frame older, and its point-light
  flicker directions differ.
- It recomposes the rig as the port must feed it (the contract below).
  Directions, all 64 colour bytes and the normal lanes equal the executed
  original for all 14 actors.
- It runs the original kernel slice on every drawn record of every bound
  actor: 4,527 record evaluations across the 14 actors. The exported EMDL
  carries each record's exact normal. `em_lighting_vertex`
  with the recomposed rig produces identical RGB words: 13,581 words in
  total.
- It runs 001D8270 over 256 types with 9 radii (2,304 cases), and 001D8690
  over 300 random cases.
- Removed error: over 3,807 records, the old stand-in colours differ from
  the original by a mean of 29.2 and a maximum of 176.3 GS units. Item 0B
  has the maximum because its actor RGB is (4,4,4).

GPU capture: `EM_STARTUP_TEST=newgame-control EM_CAPTURE_FRAME=1400` on the
private build. The parachute canopy fills the lower left of the
first-control view, as in `original_area11_playable.png`. The port's camera
is 9.6 units further along, because the test walks after control.

| Canopy interior (RGB mean / median) | value |
| --- | --- |
| original screenshot | 60.5 / 61 |
| port before (stand-in bake) | 87.9 / 90 |
| port after (authored normals, rig) | 61.8 / 62 |

The patches are canopy interior: x 40..560 and y 1130..1420 of the
original (scaled to 1920x1440), and the changed canopy pixels with
x < 560 and y > 1250 in the port. The captures (ignored build directory)
are `build/r1-actor-lighting/first_control_before_standin.png`,
`first_control_after_relit.png` and `parachute_original_before_after.png`.

205,165 pixels change: 198,820 in the lower-left canopy region (including
its cords) and 6,345 behind the elevator grille and railing, where actors
are partly hidden. The level,
the player and the panel/elevator (already flags 0) are byte-identical.

## R11: level vertex colour scale

R11 asked whether level colour 1.0 maps to GS 128 or 255. It maps to GS
128. The level kernel CALL 00237180 loads 65536.0 (LOI at 00237218,
ADDI.y at 00237220). It adds that to the record colour (ADDy at 002373B0)
and stores the result into the RGBAQ slot of the GIF tag, whose REGS are
0x4126 (TEX0, ST, RGBAQ, XYZF2) at VU1 dmem 1020. PACKED RGBAQ keeps the
low byte, which is floor(128*c). The test runs that slice on all 9,720
distinct AREA11 level colours, and every result matches floor(128*c).
`opening_gs.bin` is a GS freeze; it holds only the last RGBAQ,
(96,96,96,128), which cannot be tied to a level record. The port's scale
(1.0 = identity) is therefore correct. The port does not quantize colours
at the vertex; the difference is below 1 GS unit. The clamp now matches
the GS range for c < 2 (255/128). It changes no current AREA11 asset: the
zone meshes peak at exactly 1.0 and `05_movables` at 0.8409. The RoomLight
shade is bounded by 32 + |74*d1 + 38*d2|, about 108, which is below 128.

## Caller contract (`em_gfx.h`, owned by the render coordinator)

This contract is proven equal to the original above but not yet wired:

1. Slot 0 is zero unless actor+2 bit 0x20 is set.
2. The fold runs only when `em_lighting_fold_gate(type, radius)` passes.
   `char_rig_build` currently folds for every actor.
3. The light point is the actor+0x98 node.
4. The colour rows and ambient pass through `em_lighting_actor_rgb`. The
   rig currently uses identity RGB.
5. Each vertex is lit with its own node's matrix.

With today's wiring, the report measures these maximum differences: item
0B 162, husk partner 17, door 14, truck 5 and parachute 2 GS units.

## Limits

- **Husk creature.** Captured node 3 carries a 65 degree rotation that
  node 0 does not. The shipped static mesh bakes rest poses under one palette. On
  176 records the result differs by up to 58 GS units, and the geometry
  differs too. It needs a per-node palette from its owner.
- **Gibs and the global office crate.** They were relit with verified
  geometry, but they are not present in any capture.
- **Rig-less scenes.** `assets/scene` and `scene_office0` have no rig
  lines, so their actor draws are now rejected. That includes the player
  and the status-menu turntable, whose fallback fill depended on the
  deleted exception. `export_level.py --lightrig` for (2,1) and (2,0)
  restores the original rigs, but it also emits their fog records.
- **Status-menu backplate, in every scene including AREA11.**
  `em_render_frame.c` draws the black backplate (a flags-0 mesh) through
  `em_gfx_draw_skinned_tinted` before any `em_gfx_char_rig` call, and the
  rig is cleared at frame start. Every opening of the status menu
  therefore rejects it and prints the diagnostic for that mesh. Nothing
  changes on screen, because the clear colour is black. The
  newgame-control run shows no diagnostic only because it never opens the
  menu. The fix belongs to the render coordinator: create the backplate
  with `EM_GFX_MESH_VCOLOR` or draw it as an overlay.
- **Other shipped stand-in bakes.** A scan of every shipped EMDL finds
  flags-1 actor meshes whose colours are uniform grey. Most bottom out at
  exactly 0.300, the floor of the retired 0.30+0.70*max(N.L,0) formula.
  The single-face `area_item_0d` (0.615) and `area_internal_terminal`
  (0.741) are constant instead:
  - Referenced and still drawn: `tendril.emdl` and
    `enemy_crate_cardboard_n1.emdl` (em_enemy), `fx/light_cone.emdl`
    (em_weapon), and the drawbridge scene's doors (m03, m09, m15), props
    (item 4d, 58, 6c, 72, area item 0d) and `12_placed.emdl`.
  - In `scene_snow/props` but referenced by no manifest or source file:
    `area_battery_terminal`, `area_internal_terminal`, `area_item_11`,
    `area_item_battery` and `item_4d/4f/57/58/6c`. The AREA11 pickups
    draw `item_72` and `area_item_0b`, and the panel indicator draws
    `item_75`; all three are flags 0. `item_73` is the
    additive pickup light (00219550 child, 001C5680, draw 001CACB0),
    whose constant 1.0 is not a stand-in.

  These keep their stand-in colours until their owners re-export them with
  the updated modes. The placed bakes can no longer be regenerated. They
  need per-actor, model-local exports and a port owner that places and
  lights them. The snow `05_movables` level mesh is not a stand-in: it
  bakes the real AREA11 rig statically (RoomLight). That still omits the
  per-draw point-light fold.
- **Self-glow.** Actor+2 bit 0x40 is not representable through
  `EmGfxCharRig`. The test rejects such actors; none of the AREA11 ones has
  it.
