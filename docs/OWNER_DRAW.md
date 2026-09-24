# World-owner draw (001CAA00): the original packets and the native path

Date: 2026-09-24. Lane `owner-draw`. **Nothing here is bound into the frame.**
The binding chain owns em_owner_services, the scene coordinator, em_gfx.h and
the Metal backend; section 7 lists the exact changes proposed to it.

This document answers two questions for the AREA11 world owners: the crates
(001551B0), drums (00156620), fan (00827630), truck (00823FF0), elevator
(00827B10) and husks (00825940, 00827490).

1. What does the original draw for one owner? What goes to VU1 and the GS?
2. How must the port reproduce it? What is already native and verified, and
   what is still missing?

It contains addresses, field names and packet codes only. It holds no
original code, data or disassembly.

## 1. Delivered

| File | What |
|---|---|
| `src/game/em_owner_draw_original.{h,c}` | Translations of 001CA7B0 (cull/clip flags) and 001CA940 (kernel choice). 001CA940 reaches 001D3C30, 001D38F0, 001D3BA0, 001D38A0, 001D37D0 (NEARMISS), 001D3AD0, vif_append_ref_tag, and 001D2910(0) = 001D2710(0). The file also holds the AREA11 world model bank: `em_world_models_parse`, and 001C6120 over the bank. |
| `tools/export_world_models.py` | Writes `assets/scene_snow/world_models.emwm` and `.json` (ignored). These hold the table at `*D_0028A59C` and all 21 models it indexes. Each is checked byte for byte against 16 AREA11 captures. |
| `tools/test_owner_draw_reference.py` | The original-instruction oracle. It also proves the native chain against the captured draw packets (section 5). |
| `tests/owner_draw_test.c` | ASan/UBSan fixture. It pins the fail-stop contract, the packet layout, the bank refusals and the 001C6120 masking. |

With the three earlier modules, every draw worker of 001CAA00 now has a
native translation except **001D89D0** (lighting) and **001CB3C0** (the
+0x90 attachment). The earlier modules are:

- em_owner_services (001CAA00, 001CA990, 001C7420);
- em_load_veil_particles (001D1F80);
- em_owner_draw (this lane).

None of the world owners has an attachment. Section 6 covers 001D89D0.

## 2. The original call chain for one owner

**Who calls it.** 001AFD70 walks the owner list (head D_00275BC0, link +0x1C).
Before it calls each owner's +0x10 behaviour, it runs 001CB590(owner). That
call sets D_00275B48 = D_00275B44 = owner and D_00275B40 = owner + 0x110.
The behaviour ends with the call to its +0x4C draw method, and for all the
owners above that method is 001CAA00. The draw therefore always sees its own
node slots through D_00275B40. That matters for owners with +0x98 != 0xFF:
each crate has +0x98 = 0, so its position is its own node 0 +0xC0.

**001CAA00(owner)** (em_owner_services):

- radius = model +0x20. If radius < 20.0 (EE compare), radius = radius × 1.2
  (EE multiply). With no model, radius = 20.0.
- position = owner +0xB0 when +0x98 == 0xFF, else D_00275B40[+0x98] + 0xC0.
- It calls 001CA990(owner, position) with f12 = radius, then 001CB3C0 when
  +0x90 != 0.

**001CA990** (em_owner_services):

- flags = 001CA7B0(position, radius).
- flags < 0: nothing is emitted.
- Otherwise, in order: 001D8C20(0) (context +0x246C = 0, lighting mode 0),
  001C7420(owner, 0x3F5, channel 0), 001D1F80(0, 1, 0) and
  001CA940(flags, owner +0x44).

**001CA7B0(position, f12 = r)** (this lane):

1. b = the quadword at `position` with w = 1.0.
2. a = b × D_00810610, the view matrix (the SDK row transform 001026A0,
   VU0 forms MULA/MADDA/MADD).
3. a.w = 0.
4. The view z a.z is compared first:
   - a.z < -r (EE compare after an EE negate): the result is **-1** (culled);
   - a.z < r: bit 1.
5. For k = 0..3: d = (p.x · a.x + p.y · a.y) + p.z · a.z, where p is plane
   k at context +0x2410 + 0x10·k (00102738: one VU multiply over xyz, then
   two broadcast adds into x, in that order). Then:
   - d < -r: **-1**;
   - d < r: bit 2 << k.

   The planes are the four side planes that 001D2960 builds (see
   FRAME_RENDER_HEADS.md).
6. The result is 0..0x1F. **Only bit 0 (the sphere crosses the view-z
   plane) changes the draw.** Bits 1..4 are computed and not used by this
   path.

**001CA940(flags, model)** (this lane):

- flags != 0 and flags & 1: 001D3C30 → 001D3BA0(channel 0, model).
- Otherwise: 001D38F0 → 001D38A0(channel 0, model).

Each builder writes DMA tags at the channel cursor `*(D_00275670 + 0x10)`.
Section 3 lists them.

## 3. The DMA unit and what VU1 and the GS receive

One owner with n bones and no clip gives **0x100 + 0x80·(n-1) + 0x50 bytes**.
That is 0x150 for the crate, fan, truck and elevator, 0x1D0 for the drum and
the husk partner, and 0x2D0 for the husk creature. The clip variant adds 0x40.
The units are written at the channel 0 cursor, in this order:

| # | Tag | Written by | Content |
|---|---|---|---|
| 1 | CNT, qwc 5 | 001C7420 | VIF NOP, FLUSH, STCYCL 1,1, UNPACK V4-32 4 qw to **VU1 0x3F5**. Payload: B, the 001D89D0 colour matrix (three light colours and the ambient row, times the owner RGB). |
| 2 | CNT, qwc 1 + 8n | 001C7420 | VIF STCYCL 1,1, UNPACK V4-32 8n qw to **VU1 0**. Per node (8 qw): **node +0x90 × VP** (SPR 0x70003AC0), then **C × A**. C is node rows 0..2, each normalised (VU square root and divide), plus row 3 raw. A is the 001D89D0 light-direction matrix. A collapsed bone (+0x94) sends the cleared B, with the node's row 3, times VP and times A instead (OWNER_SERVICES.md). Chunks hold at most 248 qw. |
| 3 | REF 9 qw → 0x00815360 | 001D1F80(0, 1, 0) | GS state set 1, class 0 (LEVEL_MATERIALS.md). One DIRECT GIF packet of seven A+D writes: PRIM, TEX1 0x60, TEST 0x5000D, ZBUF, ALPHA 0x80000000A8, CLAMP 0 and COLCLAMP 1. |
| 4 | REF 8 qw → 0x00816440 + 0x80·(context +0x9C) | 001D38A0 | Skin record 0. STCYCL 4,4 and UNPACK V4-32 7 qw to **VU1 0x3F9..0x3FF** (listed below the table). |
| 5 | REF 1 qw → *D_00275674 (0x00814220) | vif_append_ref_tag | The arena's first qword. A VIF FLUSH. |
| 6 | CALL qwc 0 → 0x0023C750 | vif_append_ref_tag | The object kernel's chain in the ELF. It uploads the microprogram (MPG) and returns. Context +0x50 = 0x0023C750. |
| (7) | REF 2 qw → 0x002514B0 | 001D37D0, only while context +0x0C bit 0 is **clear** | UNPACK 1 qw to VU1 0x3FD = (255.0, 2048.0, 255.0, 0). This makes the kernel's fog value F = 255 + 0·w for every vertex: no fog. In all 16 AREA11 captures (and beat 15) +0x0C = 0x43, so the bit is set and this tag never appears there. |
| 8 | REF (model +0x04 low halfword) qw → model + 0x40 | 001D37D0 | The model's blocks (below). Each block ends in MSCAL 0 (the first) or MSCNT (the rest). |
| clip | REF 8 qw → 0x00816540 + 0x80·slot, REF 1 → arena, CALL → **0x002354A0**, (REF 2), REF model again | 001D3BA0 → 001D3AD0 | Only when flags & 1. Skin record 1 differs from record 0 in dmem 0x3F9..0x3FB, the clip GIF tags. 0x002354A0 is a second, larger microprogram chain. The whole model is sent a second time. Context +0x50 = 0x002354A0. |

DMA tag bytes +2 and +8..+0xF are never written. The captured lists carry
stale bytes there.

**VU1 data memory for one unit:**

- **0 .. 8n-1:** per node, the position matrix (node × VP) and the lighting
  matrix (C × A).
- **0x3F5..0x3F8:** the colour matrix B.
- **0x3F9..0x3FB:** GIF tags used by the kernel. Record 1 carries different
  tags for the clip program.
- **0x3FC:** the template GIF tag. PRE 1 with PRIM 0x03C (triangle strip,
  Gouraud, textured, fogged, no blending), NREG 4, REGS 0x4126 (TEX0, ST,
  RGBAQ, XYZF2) and NLOOP 32. LEVEL_MATERIALS.md finds the same template PRIM
  on the 5,840 captured object-kernel kicks.
- **0x3FD:** the fog row (255.0, 2048.0, A, B). 001D30A0 copies A and B from
  context +0xA0. The optional REF 2 replaces the row with (255.0, 2048.0,
  255.0, 0).
- **0x3FE / 0x3FF:** the guard-band rows. 001D30A0 copies them from
  context +0x2220 / +0x2230.

**Model block** (export_world_models.py checks every block). Each block is
one STCYCL 4,4, then UNPACK V4-32 128 qw to TOPS + 0 (the FLG bit set, so
the VU1 double buffer and not the node area), then 32
vertices of 4 qw each, then MSCAL/MSCNT. Each vertex holds:

- **qw0:** a 64-bit TEX0 register value (the texture of this vertex's
  strip);
- **qw1:** (s, t, 1.0, 0);
- **qw2:** the authored normal (x, y, z, 0);
- **qw3:** position x, y, z and a data word. In the data word, (word & 0x3FF)
  >> 3 is the node slot, and bit 15 set means "no kick" (ADC). Its float value
  is the strip winding sign (em_gfx.h shadow section).

**Model header:**

| Offset | Field |
|---|---|
| +0x00 | blocks |
| +0x04 | qwc = blocks × 0x82 |
| +0x08 | bones |
| +0x0C | skeleton offset = 0x40 + 16·qwc |
| +0x14..+0x1C | minimum corner |
| +0x20 | radius |
| +0x24..+0x2C | extents |
| skeleton offset | bones × 0x50-byte records: parent at +0x04, bind matrix at +0x10 |

**What the object kernel does with it.** This is known from earlier work;
the evidence is named in section 6:

- position: c = p × (node × VP); XYZ = ftoi4(c.xyz / c.w) on the GS 12.4
  grid.
- ADC is set when the data word's bit 15 is set, or when any of the last
  three vertices lies outside the guard band. **0023C750 never draws a
  triangle with a vertex outside the guard band.** Only the clip program
  0x002354A0, which runs when flags & 1, draws such triangles, clipped.
- fog: F = clamp(A + B·c.w, 0, 255).
- colour: the normal times the node's lighting matrix gives three light
  intensities. They are clamped at 0, multiplied by B, clamped at 255, and
  written as RGBAQ.
- texture: ST × Q, with the vertex's TEX0.

## 4. The world model bank (the exporter)

`*D_0028A59C` is the AREA11 world model table, at 0x01335F40 in every AREA11
capture. 001B0EA0 looks models up in it by owner +0x0D. The lookup is
001C6120(bank, id) = bank + (word[1 + (id & 0x7FFF)] >> 2 << 2), and
001CA6E0 stores the result at +0x44.

- **Source.** The chunk15 files concatenated in index order hold the table at
  0x123000. That is f05_id97 + 0x5000; from there the RAM view is contiguous
  through f06, f07 and on. The span is the table plus 21 models, 544,928
  bytes. Every model has the header and block codes above, and no two models
  overlap.
- **Byte check.** The whole span equals RAM from `D_0028A59C` in
  `playable_ee.bin` and in route beats 00..14 (16 images). The exporter checks
  every image on each run.
- **Bindings.** Each run also checks every owner bound to a bank model, 288
  owner bindings in total:
  - +0x44 == 001C6120(`D_0028A59C`, +0x0D);
  - +0x0C == the model's bone count;
  - every slot below that count is set.
- **Spawn records.** The overlay placement table is at 0x0082A3C0: 40-byte
  records, halfword +0x04 = model id, word +0x24 = behaviour. Each placed
  owner's record id equals its captured +0x0D.

| Id | Blocks | Bones | Owners (placement record / captured) |
|---|---|---|---|
| 0x04 | 13 | 1 | panel 00159210, 001C4820 |
| 0x06 | 4 | 2 | husk partner 00827490 (spawned at run time) |
| 0x08 | 16 | 4 | husk creature 00825940 (spawned at run time) |
| 0x09 | 76 | 1 | truck 00823FF0 |
| 0x0B | 1 | 1 | pickup 0015AFA0 (captured only) |
| 0x0D | 6 | 1 | crates 001551B0 |
| 0x0E | 3 | 2 | drums 00156620 |
| 0x0F | 36 | 1 | elevator 00827B10 |
| 0x10 | 1 | 1 | 001C5760 (captured only; draw method 001CACB0, not this path) |
| 0x11 | 9 | 1 | parachute 00823E80 |
| 0x13 | 2 | 1 | fan 00827630 |
| 0x14 | 10 | 2 | door 001BC350 |

The other nine entries (0x00..0x03, 0x05, 0x07, 0x0A, 0x0C, 0x12) are
exported and structurally checked, but no captured owner binds them.

`world_models.emwm` layout:

| Offset | Content |
|---|---|
| 0x00 | 'EMWM' |
| 0x04 | version 1 |
| 0x08 | table address |
| 0x0C | span size S |
| 0x10 | model count (= table word 0) |
| 0x14..0x1F | 0 |
| 0x20 | S original bytes |

`em_world_models_parse` keeps views into that buffer. For each model it fills
an `EmOwnerModel` (bone count, radius bits, skeleton records), so
em_owner_services reads the model exactly as the original does.

## 5. Proof against the captured packets

`python3 tools/test_owner_draw_reference.py`. The default run takes about
1.7 s; `EM_TEST_FULL=1` takes 3.7 s (M1).

**A. 001CA7B0.** 20,000 cases in the full run (600 in quick), plus 100
boundary cases. There are three input families:

- a translated identity view with the captured plane shape;
- captured views and planes, around captured owner positions;
- random lanes with special bit patterns: NaN, Inf, denormals, ±0, ±MAX.

The boundaries cover z = ±r and one ulp either side, a negative radius, and
the (x + y) + z summation order (2^24 − 2^24 + 1). Every outcome -1 and 0..31
occurs. The original writes nothing outside its stack.

**B. 001CA940.** 960 cases (160 quick). They cross:

- flags 0, 1, 2, 3, 0x10, 0x11, 0x1E, 0x1F, -1, -2, INT_MAX and
  INT_MIN + 1;
- context +0x0C values 0x43, 0x42, 0, ~0 and ~1;
- skin slots 0, 1, 0x1FFFFFF and ~0;
- four model/+0x04 pairs, including +0x04 > 0xFFFF.

The window is prefilled with a pattern. Every window byte, the cursor and
context +0x50 must be equal, and the byte count must equal
`em_owner_draw_001CA940_bytes`.

**C. Bank.** For all 21 models, every view field and skeleton record is
checked against the bytes. Native 001C6120 is compared with the original
001C6120, run over captured RAM, for 84 ids: each id, id | 0x8000,
id | 0x10000 and id | 0xFFFF8000.

**D. Captured draws.** Every owner in beats 00..14 with draw method 001CAA00
and a bank model: 255 owner-frames. For each one:

1. The **original** 001CAA00 runs over the captured RAM and scratchpad, with
   the draw loop's D_00275B48/44/40 set.
2. Its unit is located in the captured display list, on every byte the
   original writes. For crates, drums, fan, truck, elevator and husks, a
   drawn unit that is missing from the list fails the test.
3. The **native** chain runs: em_owner_services_001CAA00 with
   - `w_001CA7B0` = em_owner_draw_001CA7B0;
   - `w_001CA940` = em_owner_draw_001CA940, with the model bound from the
     bank through native 001C6120, whose handle is asserted equal to the
     captured +0x44;
   - `w_001D1F80` = em_load_veil_particles_001D1F80, over the same window;
   - `w_001D89D0` answered with the original 001D89D0's A and B.
4. Every byte of the native window, the cursor, context +0x50 and the
   lighting mode must equal the original run. The 001CA7B0 class must agree
   with the kernels in the unit.

Full run results:

| Behaviour | Owner-frames | Drawn plain | Drawn clip | Culled | Unit found in captured list |
|---|---|---|---|---|---|
| crates 001551B0 | 60 | 25 | 1 | 34 | 26 of 26 |
| drums 00156620 | 30 | 9 | 0 | 21 | 9 of 9 |
| fan 00827630 | 30 | 12 | 0 | 18 | 12 of 12 |
| truck 00823FF0 | 15 | 8 | 1 | 6 | 9 of 9 |
| elevator 00827B10 | 15 | 6 | 2 | 7 | 8 of 8 |
| husk creature 00825940 | 15 | 7 | 0 | 8 | 7 of 7 |
| husk partner 00827490 | 15 | 6 | 2 | 7 | 8 of 8 |
| parachute 00823E80 | 15 | 6 | 5 | 4 | 11 of 11 |
| door 001BC350 | 15 | 7 | 1 | 7 | 8 of 8 |
| panel 00159210 | 15 | 6 | 1 | 8 | 7 of 7 |
| 001C4820 | 15 | 6 | 1 | 8 | 7 of 7 |
| pickup 0015AFA0 | 15 | 7 | 0 | 8 | 4 of 7 |

- **Totals.** 119 units are drawn and 116 are found byte-exact in the
  captured lists. In every one of the 119, the native chain equals the
  original.
- **Pickup misses.** The three pickup units not found are 01_battery,
  04_elevator_ride and 13_east_tower. Their behaviour does not reach its draw
  in every state, so the check is reported for pickups but not asserted. The
  captures show only that the pickup was not drawn in those frames.
- **Clip packets.** The captured clip units include the second skin record,
  the CALL to 0x002354A0 and the repeated model REF. There are 14 of them:
  - the parachute in beats 00..04;
  - the panel and the elevator in 02;
  - the truck in 04;
  - the elevator and 001C4820 in 05;
  - a crate in 08;
  - the husk partner and the door in 09;
  - the husk partner in 11.

**Defect injection** (2026-09-24). 24 native defects were injected, and the
quick run catches all 24:

- **001CA7B0:** the multiply operand order (the fs clamp), the broadcast-add
  order, less-or-equal in each of the three compare sites, the negate
  replaced by setting the sign bit, the w lane taken from the position, the
  last multiply-add lane, a wrong accumulate lane, the view lane, the plane
  bit order.
- **001CA940 chain:** the REF 2 polarity, the model data offset, the slot
  shift, context +0x50 not stored, the CALL tag id, the clip skin record, the
  clip kernel, the flags bit tested, the arena REF qwc, the skin REF qwc, the
  REF 2 qwc.
- **Bank:** the 001C6120 mask, the skeleton parent offset.

Two of the 24 first survived: the broadcast-add order and the negate. The
2^24 order cases and the negative-radius cases were added to catch them.

`tests/owner_draw_test.c` (`make test-owner-draw`, 0.1 s) pins:

- the NULL-view faults and their addresses;
- the latch;
- the window check before any write, for both builders;
- the tag layout: the plain unit with slot 1, and the clip unit with REF 2;
- the untouched tag bytes;
- the byte counts;
- nine bank refusals, each leaving the bank zeroed;
- 001C6120 masking.

When `assets/scene_snow/world_models.emwm` exists, the fixture also checks
the owner ids and bone counts in it.

## 6. The native draw path, and what is still missing

**Already native and verified (the packet side):**

| Step | Native | Evidence |
|---|---|---|
| bank, 001C6120, model view | em_owner_draw (`em_world_models_*`) | this lane (C, D) |
| 001B0EA0/001B0FD0 allocation, bone init, placement 001C6380 | em_owner_services | OWNER_SERVICES.md; 20/20 crates, 10 drums, 10 fans reproduced from captures |
| 001CAA00, 001CA990, 001C7420 | em_owner_services | OWNER_SERVICES.md; this lane D (whole unit) |
| 001CA7B0, 001CA940 | em_owner_draw | this lane A, B, D |
| 001D1F80 | em_load_veil_particles | LOAD_VEIL_PARTICLES.md; this lane D |
| 001D8C20 | context +0x246C = 0 (one store) | this lane D |

**Missing on the EE side:**

1. **001D89D0, bit-exact.** ACTOR_LIGHTING.md shows that the port's
   recomposed rig equals the executed original for 14 actors on every colour
   byte and normal lane, under its caller contract. That recomposition is not
   a translation of the entry point, and no native routine writes A and B the
   way 001C7420 consumes them. Lane D here borrows the original's A and B.
   Before binding, a translation of 001D89D0 (001D8130, 001D7B30, 001D8340,
   001D8270, 001D8690 and the VU0 helpers) must pass an oracle like lane D's.
2. **The model binding.** The worker slots and what they need:
   - `w_001C6120` → em_world_models_001C6120.
   - `w_001CA6E0` → owner +0x44 = the bank view, and +0x4C = 001CAA00. That
     is 001CA5F0 kind 0, already in em_roger_actor.
   - `w_001AF780`: a node arena and the D_00275BD0 stack. em_roger_actor
     models it for Roger only.
   - `w_anim_bone_array_setup` → the side table's +0x110 slots.

   These are the coordinator's (OWNER_SERVICES.md "Binding"). The bank
   supplies every model byte they read.

**The render side.** Today em_enemy draws the crates with an EMDL mesh through
`em_gfx_draw_skinned`. The EMDL comes from export_props over the same blocks,
with texels from the GS freeze. The other owners have their own legacy
draws. Measured against the unit above, `em_gfx_draw_skinned` is exact in
some parts and not in others:

| Unit part | em_gfx today | Exact? |
|---|---|---|
| per-vertex colour (lighting matrix, B) | em_gfx_metal.m computes it on the CPU with em_lighting_matrices + em_lighting_vertex from the palette and `EmGfxCharRig` | yes, under the ACTOR_LIGHTING contract (verified per record for 14 actors) |
| GS state class 0 (TEST, ALPHA, TEX1, CLAMP, template PRIM) | the opaque skinned state | yes (LEVEL_MATERIALS.md: 5,840 object-kernel kicks) |
| fog row | `em_gfx_fog` | yes for the fog-on case. The fog-off REF 2 (context +0x0C bit 0 clear) has no em_gfx equivalent. |
| positions: node × VP on the VU, ftoi4 | GPU float, palette × viewproj | **no**. Not bit-exact on the 12.4 grid. The silhouette path already does this step on the CPU bit for bit (`em_shadow_gs_bone` + `em_shadow_gs_object_batch`). |
| guard-band ADC: 0023C750 never draws a triangle with a vertex outside the guard band | Metal clips such triangles and draws them | **no** |
| the flags & 1 clip pass (0x002354A0) | none | **no**. The microprogram is not translated. em_vu1_shadow_clip.h translates the level clip kernels 00239C90 and 0023E8A0 only. |
| the cull (-1: no unit at all) | em_enemy draws without 001CA7B0 | **no**, until the owners are bound to em_owner_services |
| texture: the per-vertex TEX0 and ST·Q | the EMDL texture table (TEX0 → embedded RGBA8), bilinear | not verified per vertex. No test runs the object kernel's ST/Q path. The level path's texture set is verified (LEVEL_MATERIALS.md). |

**The draw path the port needs, step by step.** For each owner, in the
owner walk's order, which is also the original list order (ORIGINAL_FRAME_ORDER.md):

1. The coordinator runs the behaviour. The behaviour's draw worker runs
   em_owner_services_001CAA00 with the workers above. The result is the
   original's unit bytes in a native display-list buffer, plus the flags.
2. The renderer consumes the unit, not a stand-in:
   - the colour matrix (tag 1);
   - per node, the position matrix and the lighting matrix (tag 2);
   - the fog row of the skin record, or the REF 2 row;
   - the model's blocks, resolved from their REF address through the bank
     (`em_world_models_at(address - 0x40)`).
3. **Per vertex, on the CPU, as the silhouette path does:**
   - position: c = p × M[slot], then XYZ = ftoi4(c.xyz / c.w);
   - ADC: the data word's bit 15, or a guard-band clip flag on vertex
     i-2..i;
   - fog: F from the fog row;
   - colour: em_lighting_vertex from the lighting matrix and B;
   - texture: ST·Q and TEX0.
4. Rasterize the kicked triangles (strip order, no culling) in GS state class
   0 with the vertex's texture.
5. When flags & 1, run the 0x002354A0 program's triangles after them.
6. Show nothing for flags -1.

## 7. Proposals for the chain (em_gfx.h / Metal; not edited here)

**P1. A native object-unit draw that consumes the unit's contents.** Proposed
text for em_gfx.h:

```c
/* The object kernel 0023C750 (and, when `clip`, the 0x002354A0 program) over
 * one 001CA990 unit, computed on the CPU bit for bit as the silhouette path
 * does (em_shadow_gs_bone / em_shadow_gs_object_batch for XYZ and ADC,
 * em_lighting_vertex for RGBAQ, the fog row for F), then rasterized in GS
 * class 0 (TEST 0x5000D, ALPHA off, TEX1 bilinear, CLAMP repeat) with no
 * face culling. Triangles with ADC set are not drawn. Returns 0, or -1 when
 * it cannot draw exactly what the original draws (a missing input, a TEX0
 * without a resolved texture, clip set while the 0x002354A0 program is
 * untranslated); the reason is printed once per session. */
typedef struct {
    const uint32_t *color;       /* 16 words: VU1 0x3F5..0x3F8 (unit tag 1 payload) */
    const uint32_t *nodes;       /* 32 words per node: node x VP, then C x A (tag 2 payload) */
    uint32_t node_count;
    const uint32_t *constants;   /* 28 words: VU1 0x3F9..0x3FF (skin record 0 payload,
                                    with 0x3FD replaced when the REF 2 was emitted) */
    const uint32_t *clip_constants; /* skin record 1 payload, or NULL when !clip */
    const uint8_t *blocks;       /* model + 0x40: block_count x 0x82 qwords */
    uint32_t block_count;
    uint32_t clip;               /* 001CA7B0 flags & 1 */
} EmGfxObjectUnit;
int em_gfx_object_unit(EmGfx *gfx, const EmGfxObjectUnit *unit);

/* The TEX0 -> texture binding for object units: every TEX0 value a bank
 * model's vertices carry, mapped to an uploaded RGBA8 texture decoded from
 * GS memory (the same source the level's gsmat textures use). */
int em_gfx_object_texture(EmGfx *gfx, uint64_t tex0, const uint8_t *rgba,
                          uint32_t width, uint32_t height);
```

**Metal (em_gfx_metal.m).** The new function shares the silhouette code path:

- CPU vertex transform with `em_shadow_gs_bone` / `em_shadow_gs_object_batch`
  (the rows come from the unit, so no product is recomputed);
- GS-window-to-NDC mapping for the 512x448 frame, as the receiver pass does;
- per-vertex RGBAQ from em_lighting_vertex over the unit's lighting matrix;
- F from the constants row.

It draws into the frame's colour and depth with the class-0 pipeline that
`em_gfx_draw_skinned` already uses, with the texture chosen by the vertex's
TEX0.

**P2. The clip program 0x002354A0.** Translate it as
`src/game/em_vu1_object_clip.h` (new lane), on the pattern of
em_vu1_shadow_clip.h (00239C90 / 0023E8A0). Its oracle runs the uploaded
microprogram over the 14 captured clip units in section 5. Until it
exists, `em_gfx_object_unit` returns -1 when `clip` is set. Under the
fidelity rules, the owners stay unwired, because a near-plane crate would
otherwise vanish.

**P3. Fog-off.** In `EmGfxObjectUnit.constants`, the 0x3FD row after the REF 2
(255, 2048, 255, 0) gives F = 255. No separate em_gfx_fog state is needed per
draw.

**P4. Until P1/P2 land.** Mapping owners onto `em_gfx_draw_skinned` has two
known inexact parts: the GPU position rounding, and guard-band triangles
drawn instead of dropped. The clip pass is missing as well. The palette is
node +0x90, whose 16 floats in memory are already the column-major layout
em_gfx wants. The viewproj is the port's P·V, not the unit's VP rows. That is
not the Original profile, so this lane does not recommend it as the live path.

## 8. Makefile hunks (for the chain; the Makefile is not edited here)

```make
.PHONY: test-owner-draw
test-owner-draw:
	mkdir -p build/owner_draw && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffp-contract=off -Isrc tests/owner_draw_test.c src/game/em_owner_draw_original.c -o build/owner_draw/owner_draw_test && ./build/owner_draw/owner_draw_test

.PHONY: test-owner-draw-reference
test-owner-draw-reference:
	python3 tools/test_owner_draw_reference.py

.PHONY: export-world-models
export-world-models:
	python3 tools/export_world_models.py
```

When the owners are bound, add `src/game/em_owner_draw_original.c` to COMMON
next to `src/game/em_owner_services_original.c`. The module needs nothing
else: it includes only em_owner_services_original.h and em_ee_float.h.

## 9. Limits

- **The unit, not the pixels.** Section 5 proves the EE output: the DMA unit,
  byte for byte. No test here runs the object kernel's texture path. No GS
  framebuffer comparison is made.
- **001D89D0 is borrowed** in lane D. Section 6 item 1 names its lane.
- **Placement address.** The table address 0x01335F40 is a RAM placement,
  not a disc value. The exporter checks it in every capture. REF targets and
  001C6120 handles are original addresses, which the renderer resolves
  through the bank. Nothing dereferences them.
- **Other captures.** Beat 15 (the level exit) is area 1, with its own table
  at another address, and is out of scope.
