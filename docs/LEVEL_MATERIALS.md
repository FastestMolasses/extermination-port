# AREA11 level materials: GS draw state (R10, R25, R07)

FIRST_LEVEL_AUDIT WP-13 listed three render approximations:

- **R10.** Every level and actor draw used an invented state: discard below
  texture alpha 0.5, then standard alpha blending.
- **R25.** Bilinear/REPEAT sampling and perspective-correct level colour
  were assumed, not decoded.
- **R07.** The menu actor's infection tint.

This document records what the original does and what the port now does.

## Where the original sets the state

A level record carries only its TEX0 qword (`export_level.py`, record
format). The rest of a level triangle's GS state comes from two places.
Neither depends on the texture.

1. **PRIM** comes from the level kernel's GIF-tag template.
   - The level kernel is the VU1 code that the CALL `0x00237180` packet
     uploads.
   - At `0x002373F0` it loads the qword at VU1 dmem 1020 (0x3FC). At
     `0x002373F8` it stores that qword at the head of every output buffer
     (132 qwords past the buffer base), before the buffer is kicked to the
     GIF.
   - The clip kernel `0x00239C90` loads dmem 1017/1018 (0x3F9/0x3FA) at
     `0x0023A0B0`/`0x0023A090`.
   - The level chain uploads those qwords from channel 0, `D_00816440`.
     `skin_arena_init` copies channel 0 from `D_002514D0`. Qwords 0..4 of
     `D_002514D0` (the VIF code and dmem 0x3F9..0x3FC) are unchanged in the
     captures. Only the constants at 0x3FD..0x3FF are rewritten at runtime.
   - The dmem 0x3FC tag is PACKED with PRE=1, NREG 4 and REGS 0x4126
     (TEX0, ST, RGBAQ, XYZF2). **PRIM 0x03C** decodes as tristrip, IIP 1,
     TME 1, FGE 1, **ABE 0**. The clip kernel's triangle tag has the same
     IIP, TME, FGE and ABE bits.
2. **TEST_1, ALPHA_1, TEX1_1 and CLAMP_1** come from an env packet that the
   level chain REFs before every unit.
   - The packet is the 9-qw `D_00815360`. `001D0F20` builds it.
   - `001D0F20` writes four sets of ten classes. Each packet is at
     `*(gp-0x7CFC) + 0xBA0 + 0x5A0*set + 0x90*class`, and
     `*(gp-0x7CFC)` = `0x00814220` in every capture.
   - `D_00815360` is set 1, class 0. It holds seven A+D writes:
     - PRIM 0x100: `0x001D175C`. PRE tags override it.
     - TEX1 0x60: `0x001D1764`.
     - TEST: `(ZTST << 17) | 0x1000D`, built at `0x001D16D4..E0` and
       stored by class 0 at `0x001D1804`. Jump table `0x0026E410`
       entry 0 = `0x001D17F8`.
     - ZBUF (ZMSK from the set).
     - ALPHA 0x80000000A8: `0x001D17F8..0x001D1810`.
     - CLAMP 0: `0x001D17B4`.
     - COLCLAMP 1.
   - The builder writes the same TEX1 and CLAMP constants for every class.

The decoded level class, confirmed by the captures:

| Register | Value | Meaning |
| --- | --- | --- |
| PRIM (template) | 0x03C | tristrip, Gouraud, textured, fogged, **no blending** |
| TEST_1 | 0x5000D | ATE 1, **ATST GREATER, AREF 0**, AFAIL KEEP; ZTE 1, ZTST GEQUAL |
| ALPHA_1 | 0x80000000A8 | A=Cs B=0 C=FIX D=0, FIX 0x80 (unused while ABE is 0) |
| TEX1_1 | 0x60 | LCM 0, MXL 0, **MMAG and MMIN LINEAR**, L 0, K 0 |
| CLAMP_1 | 0x0 | **REPEAT** in S and T |
| ZBUF_1 | ZMSK 0 | depth written |
| PRMODECONT / FBA / PABE / COLCLAMP | 1 / 0 / 0 / 1 | PRIM attributes used; no alpha correction; clamp |

The records add TEX0 fields. Every AREA11 level record has TCC 1 (RGBA),
CPSM CT32 and PSM PSMT4 or PSMT8. Most use TFX 0 (MODULATE). The
normal-carrying records of `f17_id93.bin` (05_movables) use TFX 2
(HIGHLIGHT).

**Consequence: the alpha test.** The texel alpha that reaches the alpha
test is As = At.

- The level kernel sends RGBAQ A = the low byte of float(65536 + w), where
  w is the record's colour.w: I = 65536.0 at `0x00237218`, vf9.y = I at
  `0x00237220`, and colour + vf9.y (broadcast to all lanes) at `0x002373B0`. Colour records have w = 1.0, so A = 128 and MODULATE gives
  As = At*128>>7 = At.
- Normal records have w = 0, so A = 0 and HIGHLIGHT gives As = At + 0. Its
  RGB, Ct*Cf>>7 + 0, is the same as MODULATE.
- Captured actor draws are always TFX 2 with the colour-matrix ambient w at
  8388608.0, so A = 0 there too.

A level or actor pixel is therefore dropped only when its **filtered**
texel alpha is 0, and it neither writes colour nor depth (AFAIL KEEP).
Every other pixel is written **opaque**, because blending is off. The
cutout textures hold GS alpha 0 or 0x7F only (exported as 0 and 254). The
GS bilinear filter then keeps any pixel with some weight on an opaque texel.
The grey 0x50 or 0x14 RGB of the transparent texels mixes into it. This is
where the original's thick, dark grate and railing bars come from. The old
port path discarded below 0.5 and blended the remainder, which drew thinner
bars with bright fringes.

**Consequence: sampling (R25).** The port's sampler was already correct:
linear min/mag filters, no mipmaps, REPEAT on textures tiled to their
native period. It is now decoded rather than assumed. Level colour is
RGBAQ, and the GS Gouraud-interpolates it linearly in screen space (IIP 1).
The port's level colour varying is now `center_no_perspective`, the same as
the actor light colours (OPENING_LIGHTING.md). Only texture coordinates
(STQ) stay perspective-correct.

The crisp stair-step edges in `original_area11_playable.png` are polygon
edges. The screenshot is an upscale of a 512x448 frame. They are not
nearest-neighbour texture sampling: TEX1 is linear in both directions.

## Captured evidence

`tools/test_level_material_reference.py` walks both display lists
(`0x0028F700`, `0x00293700`) of six AREA11 EE captures. The captures are
playable, opening, handoff, roger-encounter, elevator clip47 and elevator
completed. It walks them as the DMAC (source chain, TTE off), VIF1 and GIF
would. At every MSCAL/MSCNT kick it snapshots the GS registers, the address
that last wrote each register, and the uploaded dmem 0x3F5..0x3FF qwords.

- **4,300 textured level-kernel kicks** (`0x00237180` and clip `0x00239C90`)
  carry one state, the table above. TEX1, TEST, ALPHA and CLAMP are always
  written from `D_00815360` + 0x30/0x40/0x60/0x70. The untextured
  `0x00237180` kicks are the shadow-volume boxes (SHADOW_ORIGINAL.md).
  They are excluded by their template's TME bit.
- **5,840 textured opaque object-kernel kicks** (`0x0023C750`,
  `0x0023C480`) carry the same TEST, ALPHA, TEX1 and CLAMP and template
  PRIM. Every record they draw is TCC 1, TFX 2 with A = 0, so As = At holds
  for actors too. This is the evidence for giving unflagged (actor) meshes
  the same class-0 state by default.
- **81 distinct TEX0 keys** are drawn by level kicks in the captures. They
  cover 190 of the 290 exported zone textures (keys repeat across zones).
  The other 100 textures get the same state because the state is
  texture-independent: every unit re-sends the same static packet and
  template.

The captures also show classes that this work does not port:

- blended effects: ALPHA 0x68 / TEST 0x53001;
- the shadow receiver pass: kernels `0x0023C200`/`0x0023E8A0`, ALPHA 0x44,
  TEST 0x5C00D with DATE;
- 21 untextured object-kernel kicks per frame with TEST 0x3000D (depth
  ALWAYS) and ZMSK 1. Their source blob is at `0x012D0000`, not yet
  identified.

## What changed

- **Exporter**:
  `../Extermination/tools/export_level.py --gs-materials assets/scene_snow`.
  Run it from the decomp root.
  - For each AREA11 zone EMDL, it rebuilds the geometry from its chunk15
    source and requires exact equality of positions, UVs, texture slots and
    indices.
  - It then writes one code per texture into the EMDL tex entry's
    `reserved` word and sets header flag bit 3 (`EM_MODEL_FLAG_GSMAT`).
  - It also writes `<zone>.gsmat.json` with the raw registers, TEX0, TCC,
    TFX and the RGBAQ A values of every vertex of that texture's triangles.
  - It reads the template PRIM from the pinned ELF and the env constants
    above.
  - It faults on any texture outside the set the backend implements:
    TCC 0, TFX 1/3, or TFX 0 with A ≠ 128 and TFX 2 with A ≠ 0.
  - No other byte of the file changes.
  - The flag is bit 3 rather than bit 1. Older binaries OR the mesh flags
    into their shader mode word, where bits 1 and 2 already have meanings.
    An old binary renders the flagged assets pixel-identically; this was
    checked.
- **`em_gfx.h`**: `EM_GFX_MESH_GSMAT` (8) and the code layout macros.
  - bits 0..13 TEST
  - bit 14 PRIM.ABE
  - bits 15..22 ALPHA A/B/C/D
  - bit 23 TCC
  - bits 24..25 TFX
  - bit 26 MMAG
  - bits 27..29 MMIN
  - bits 30..31 wrap
- **`em_model.h/.c`**: `EM_MODEL_FLAG_GSMAT`. A flagged file without
  textures is rejected.
- **Metal backend**:
  - Opaque skinned draws (tint alpha ≥ 1, not additive) use a new
    no-blend pipeline. They run the per-slice TEST_1 alpha test: all eight
    ATST compares against AREF on the GS-scale alpha, and AFAIL KEEP
    discards.
  - Mesh creation rejects a code it does not implement: AFAIL ≠ KEEP,
    ABE 1, TCC 0, TFX 1/3, a filter other than MMAG/MMIN LINEAR, or a
    non-REPEAT wrap.
  - Unflagged meshes get the class-0 code.
  - The translucent tint path (rgba[3] < 1, the port's gib fade) is not
    decoded. It keeps standard blending and the 0.5 cutout.
  - The level colour varying is `center_no_perspective`.
  - Glow and additive draws are unchanged.

## Verification

- `python3 tools/test_level_material_reference.py` passes in about 1.4 s.
  Quick and full mode run the same cases, because there is no sampled
  sweep. It also:
  - checks the kernel instruction fields;
  - checks the arena arithmetic;
  - rebuilds each zone's texture order from its source independently of
    the exporter;
  - requires every JSON record and every packed code to equal the captured
    registers exactly;
  - compiles the header macros and compares their decode with the test's.
  - Mutations are caught: flipping one ATST bit in one EMDL code, or
    editing one JSON `test_1`, fails the test.
  - The report is written to `build/level_material_reference/report.json`.
- These tests still pass: `make test-overlay-blend`,
  `make test-area11-fog-reference`, `make test-roger-assets` and
  `python3 tools/test_actor_lighting_reference.py`.
- The headless `EM_STARTUP_TEST=newgame-control` run passes (displacement
  9.599989) with the same position before and after the change. The
  capture is deterministic: a rerun of the same binary is pixel-identical.
- Capture comparison: `build/captures/level-materials/before.png` and
  `after.png`, from the same source snapshot plus only this change.
  - 829,776 pixels change, and 42,263 of them by more than 24 summed RGB.
    The large changes are on the railing and elevator grate cutouts. The
    small changes across level surfaces come from the screen-linear colour.
  - The port camera differs from the original screenshot's, because the
    test walks 30 ticks. The metric therefore compares matched content
    regions, not pixels. The regions are the railing band polygon and the
    grate panel box, chosen per image. Luminance is Rec.601. The EMD is
    over 32-bin histograms.

| Region | Metric | original | before | after |
| --- | --- | --- | --- | --- |
| railing | mean luminance | 43.2 | 48.0 | 45.9 |
| railing | dark pixels (L < 70) | 92.9 % | 87.4 % | 90.7 % |
| railing | histogram EMD to original | 0 | 4.9 | 3.0 |
| railing | bright 1-px ridges (fringe) | 0.04 % | 4.42 % | 2.78 % |
| grate | mean luminance | 68.5 | 71.9 | 69.7 |
| grate | dark pixels (L < 70) | 48.6 % | 40.8 % | 43.9 % |
| grate | histogram EMD to original | 0 | 5.4 | 5.8 |
| grate | bright 1-px ridges | 0.92 % | 3.03 % | 3.01 % |

Every railing metric moves toward the original. Grate coverage and mean
luminance move toward the original, but the grate EMD grows slightly, by
0.4. The remaining gap is dominated by resolution: the original rasterises
at 512x448, so sub-pixel bars become whole pixels, while the port renders
at 1920x1440. The metric script is `build/captures/level-materials/metric.py`.

## Limits and open items

- **Alpha threshold.** The GS truncates a bilinear result with 4-bit
  fractional weights. Metal filters with its own weight precision, and the
  exported alpha stores 0x80 as 255. The shader compares
  floor(a·127.5 + 0.001), or 128 when a = 1, against AREF. This is exact
  for unfiltered texels and approximate within one GS alpha unit on
  filtered edges.
- **05_movables TFX-2 textures.** Their normal-carrying records were not
  seen drawn in any capture. Only its TFX-0 colour records were: the
  roger-encounter level kicks, unmodified in RAM. Their code relies on the
  level and object kernels sharing class 0, which every capture confirms.
  Their RGB in the port is the H18 rig bake, not the level kernel's
  treatment of a normal. That is a lighting question for the owner of
  `export_level.py attr_to_color`.
- **R07 is not implemented here**, because it needs `em_game.c`, which this
  lane does not own. The audit's claim is confirmed in `001D89D0`:
  - After `001D8690` (`0x001D8B28`), if `actor+2 & 0x40`, the routine adds
    64.0 × actor+0x80/84/88 into colour-matrix row 3 x/y/z (arg2+0x30/34/38)
    at `0x001D8B40..0x001D8B8C`.
  - It adds 64.0 × max(actor+0x8C − 1, 0) into row 3 w (arg2+0x3C) at
    `0x001D8B90..0x001D8BC8`.
  - These are additive terms before the kernel's 255 clamp, not a
    post-multiply. The w term also raises the vertex alpha A that the alpha
    test sees.
  - Needed change (render coordinator): when the menu actor's +2 bit 0x40
    is set, add 64·actorRGB to the rig ambient passed to `em_gfx_char_rig`,
    and drop the `(128+delta)/128` post-tint at `em_game.c` (the audit's
    line 1716).
  - `em_lighting_matrices` currently hard-codes ambient w = 0. The w term
    needs an ambient.w input there, in `em_lighting.c`, which is outside
    this lane.
  - Infection is 0 at New Game, so the first-control view is unaffected.
- **Not decoded:** the state of the translucent tint path, and the classes
  listed at the end of "Captured evidence".

## Reproduce

```sh
# decomp root (macOS arm64, decomp .venv): tag the zone EMDLs
.venv/bin/python tools/export_level.py --gs-materials ../extermination-port/assets/scene_snow
# port root
python3 tools/test_level_material_reference.py
# private lane build (CLAUDE.md), then:
EM_STARTUP_TEST=newgame-control EM_CAPTURE=build/r2-level-materials/after.bmp build/r2-level-materials/extermination
```
