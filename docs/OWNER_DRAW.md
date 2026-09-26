# World-owner draw (001CAA00): the original packets and the native path

Date: 2026-09-24; binding 2026-09-25 (step "GS-exact actor draw"). **Live**
for the crates (001551B0), drums (00156620), truck (00823FF0) and fence
door (001BC350): their +0x4C builds the original unit through the
translations (section 10) and the renderer draws the triangles the
original VU1 programs kick (section 7). The other owners this document
names still draw through their legacy stand-ins (section 11).

This document answers three questions for the AREA11 world owners: the crates
(001551B0), drums (00156620), fan (00827630), truck (00823FF0), elevator
(00827B10) and husks (00825940, 00827490).

1. What does the original draw for one owner? What goes to VU1 and the GS?
2. How does the port reproduce it, and with what evidence?
3. Which owners run the exact path, and what the others wait on.

It contains addresses, field names and packet codes only. It holds no
original code, data or disassembly.


## 1. Delivered

| File | What |
|---|---|
| `src/game/em_owner_draw_original.{h,c}` | Translations of 001CA7B0 (cull/clip flags) and 001CA940 (kernel choice). 001CA940 reaches 001D3C30, 001D38F0, 001D3BA0, 001D38A0, 001D37D0 (NEARMISS), 001D3AD0, vif_append_ref_tag, and 001D2910(0) = 001D2710(0). The file also holds the AREA11 world model bank: `em_world_models_parse`, and 001C6120 over the bank. |
| `tools/export_world_models.py` | Writes `assets/scene_snow/world_models.emwm` and `.json` (ignored). These hold the table at `*D_0028A59C` and all 21 models it indexes. Each is checked byte for byte against 16 AREA11 captures. |
| `tools/test_owner_draw_reference.py` | The original-instruction oracle. It also proves the native chain against the captured draw packets (section 5). |
| `tests/owner_draw_test.c` | ASan/UBSan fixture. It pins the fail-stop contract, the packet layout, the bank refusals and the 001C6120 masking. |
| `src/game/em_object_unit.{h,c}` | P1/P2 on the CPU (section 7): `em_object_unit_parse` reads a unit's DMA tags into the VU1 uploads (`EmGfxObjectUnit`, em_gfx.h), `em_object_unit_run` runs the object kernel (em_vu1_object_kernel.h) over every model block and, for a clip unit, the clip program (em_vu1_object_clip.h) after it, and returns every drawn triangle in GS terms and GS order. A face unit (001CB3C0's, CALL 0x0023C480) runs the face morph program (em_vu1_face_morph.h) the same way. |
| `src/em_gfx.h`, `src/gfx/metal/em_gfx_metal.m` | `em_gfx_object_unit` / `em_gfx_object_texture`: the GS class-0 pixel path over those triangles (section 7). The D3D12 / Vulkan stubs return -1. |
| `tools/export_object_textures.py` | Writes `assets/scene_snow/object_textures.emot` / `.json` (ignored): the 137 TEX0 values of the bank's model blocks, decoded from the GS memory of every route capture and identical in all 15 (section 7.3). |
| `src/game/em_owner_draw_live.{h,c}` | The binding (section 10): 001CAA00 with every worker bound to its translation over canonical storage, the unit parsed at once and drawn at the frame's end. |
| `src/game/em_skin_arena_init.h` | skin_arena_init (001D2E20), the skin records' templates; run by the render context at every area load (section 10). |
| `tools/test_object_unit_reference.py` | The object-unit path against the original VU1 microcode over every captured owner unit and every captured face unit (section 8). |
| `tests/object_unit_test.c` | ASan/UBSan fixture: the parser's refusals, synthetic plain, clip and face units, the run's refusals. |
| `tests/object_unit_gpu_test.c`, `tools/test_object_unit_gpu.py` | The Metal pixel path over captured units against a model of the documented GS pixel path (section 8.1). |

With the modules below, every draw worker of 001CAA00 has a native
translation except **001CB3C0** (the +0x90 attachment, a face unit):

- em_owner_services (001CAA00, 001CA990, 001C7420);
- em_load_veil_particles (001D1F80);
- em_actor_light_001D89D0 (001D89D0 and its callees; ACTOR_LIGHT_001D89D0.md);
- em_owner_draw (this lane).

None of the bound world owners has an attachment (+0x90 = 0 in every
capture). Roger's is his face (section 11).


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

## 6. The native draw path

Per owner, in the owner walk's order (ORIGINAL_FRAME_ORDER.md):

1. The behaviour's +0x4C worker runs `em_owner_draw_live_001CAA00`
   (section 10): em_owner_services_001CAA00 with 001CA7B0, 001D8C20,
   001D89D0, 001C7420's packets, 001D1F80 and 001CA940 all translated. The
   unit is written at the render context's channel-0 cursor in its packet
   arena, exactly where the original writes it, and the cursor advances.
   A culled owner (001CA7B0 -1) writes nothing.
2. The unit is parsed at once (`em_object_unit_parse`; REF targets from
   the render context's storage and the bank) and its pieces are kept for
   the frame.
3. At the frame's end (`frame_close_out`, after the level and the legacy
   actor chain, under the frame's fog), `em_owner_draw_live_flush` hands
   each unit, in build order, to `em_gfx_object_unit`.
4. The backend runs the kernels on the CPU and rasterizes the triangles they
   kick (section 7). A -1 anywhere latches a scene fault at 001CAA00; no
   stand-in draws instead.

| Unit part | Reproduced by | Exact? |
|---|---|---|
| colour matrix B, per-node position and lighting rows | the translated 001C7420 / 001D89D0 | yes: the unit bytes (section 5 D, ACTOR_LIGHT_001D89D0.md); live against the captures (section 9) |
| the cull (-1: no unit) | 001CA7B0 | yes (section 5 A); live: the owners that drew in the camera-exact route beat equal the capture's (section 9) |
| the kicked XYZF2, RGBAQ, ST/Q, TEX0 of every vertex, the ADC drop | em_vu1_object_kernel.h through em_object_unit_run | yes: every triangle of every captured unit equals the original microcode's (section 8) |
| the flags & 1 clip pass (0x002354A0) | em_vu1_object_clip.h through em_object_unit_run | yes, same test (14 clip units) |
| a face unit (0x0023C480) | em_vu1_face_morph.h through em_object_unit_run | yes, same test (60 face units); no live owner builds one yet (section 11) |
| the fog-off REF 2 | the parser replaces dmem 1021 with the REF's row | parsed and fixture-tested; no AREA11 capture carries it |
| GS class 0 (TEST, TEX1, CLAMP, COLCLAMP, ZBUF, no blending) | the Metal pixel path | the Metal output equals a model of the formulas of section 7.2 on 99.7 % of the interior pixels (section 8.1); the formulas are the GS's, not compared with a GS framebuffer (section 12) |
| texture: the vertex's TEX0 | object_textures.emot, decoded from GS memory | the decoded texels are identical in all 15 captures (section 7.3) |
| rasterization (coverage, the per-pixel interpolation) | Metal | no: GPU float interpolation at the output resolution (section 12) |

## 7. The object-unit path (P1/P2/P3 as built)

### 7.1 The contract (em_gfx.h "Object units")

`EmGfxObjectUnit` holds what the unit uploads: the program (the object
kernel or the face program), the colour CNT's 16 words (dmem 1013..1016),
the node CNTs' 32 words per node (dmem 0..8n-1), a face unit's weights
(dmem 1011..1012), skin record 0's 28 words (dmem 1017..1023, with dmem
1021 replaced when the fog-off REF 2 was emitted: P3), skin record 1's for
a clip unit, the model REF's blocks and the clip flag.
`em_object_unit_parse_one` parses the first unit of a sequence (001CAA00
appends 001CB3C0's face unit after the owner's own). `em_gfx_object_unit` returns 0, or -1 when
it cannot draw exactly what the original draws; the reason is printed once.
`em_gfx_object_texture` registers one TEX0's texels (CLD ignored).

### 7.2 What the backend does (em_object_unit_run + Metal)

- **VU1.** One data-memory image per program built from the pieces (every
  other qword zero: the kernel and the clip program read nothing the unit
  does not upload, VU1_OBJECT_KERNEL.md 5 C and VU1_OBJECT_CLIP.md 5 A).
  Block k is copied to `em_vu1_object_kernel_top(k)` and run with MSCAL
  (k = 0) or MSCNT; the kicked packet must be the PRIM 0x03C
  TEX0/ST/RGBAQ/XYZF2 form, and its strip triangles (vertex i >= 2 without
  ADC) are kept with vertex i's TEX0. A clip unit then runs the clip
  program over the same blocks (`em_vu1_object_clip_image` / `_batch` /
  `_run` / `_triangles`; PRIM 0x03B). Order: the object pass block by
  block, then the clip pass block by block, as the GS receives them. A face
  unit's blocks (0x163 qwords: UNPACK 256 and 96 to TOPS, then MSCAL /
  MSCNT) run on `em_vu1_face_morph_mscal` / `_mscnt` at
  `em_vu1_face_morph_top(k)`; its kicks have the object kernel's layout.
- **Position.** X, Y (12.4) map to NDC as the background does
  (`em_background_gs_ndc`: field window 1792..2304 x 1936..2160, the
  convention of the port's world projection). Z (24 bits) becomes the
  port's depth for the same view depth: the engine's P gives
  Z = bz + az / w (em_math.h) and em_mat4_perspective_gs gives
  d = F / (F - N) - N F / ((F - N) w), so d = F / (F - N) (1 - (Z - bz) /
  (2^24 - 1)). Vertices carry w = 1: every attribute is screen-linear.
- **Pixel.** GS class 0 as the unit's GS state REF names it (TEST 0x5000D,
  TEX1 0x60, CLAMP 0, COLCLAMP 1, ZBUF ZMSK 0, PRIM ABE 0):
  u = S / Q, v = T / Q per pixel; bilinear with 4-bit weights at the sample
  point U - 0.5 and REPEAT wrap (as the shadow receiver); TFX HIGHLIGHT
  with TCC 1: Cv = min((Ct Cf >> 7) + Af, 255), Av = min(At + Af, 255);
  alpha test Av > 0 (fail KEEP); fog Cv = (F Cv + (255 - F) FOGCOL) >> 8
  with the frame's FOGCOL; depth less-equal with write (the GS GEQUAL on
  its reversed Z); no culling, no blending.

### 7.3 Textures (tools/export_object_textures.py)

Every model block vertex's qword 0 is a TEX0 value. The bank's 21 models
carry 137 distinct values (CLD ignored): 136 PSMT4 and one PSMT8, all
CPSM PSMCT32, CSM1, CSA 0, TCC 1, TFX 2 (the exporter refuses any other
form). Each is decoded from the GS local memory of every route capture
00..14 with the decomp's GS memory readers, and the exporter fails unless
all 15 decodes are identical: the textures are resident for the whole
level. The texels are the CLUT entries' bytes with the raw GS alpha. The
export is ignored (`assets/`), like every disc- or capture-derived asset.

## 8. Proof of the CPU path (tools/test_object_unit_reference.py)

For every owner with draw method 001CAA00 and a bank model in route beats
00..14 (section 5 D's 255 owner-frames):

1. The ORIGINAL 001CAA00 runs over the captured RAM and builds its unit.
2. The unit's tags go through DMAC and VIF1 onto one VU1 data memory (CNT
   inline, REF targets from RAM, the CALLed kernel packets' MPG blocks from
   the ELF, STCYCL, BASE / OFFSET / TOPS, MSCAL / MSCNT). The object
   kernel's batches run on VU1_OBJECT_KERNEL.md's interpreter (VU0 lane
   rules), the clip program's on VU1_OBJECT_CLIP.md's; both execute the
   ORIGINAL instructions. Every XGKICK is decoded independently in Python.
3. `em_object_unit_parse` + `em_object_unit_run` over the same unit bytes.
4. The triangle lists must be equal: count, order, pass, block, the whole
   TEX0 and per vertex X, Y, Z, F, R, G, B, A, S, T, Q. The parsed pieces
   must equal the unit's uploads (colour, nodes, both skin records, the
   model REF).

Also: the GS state REF's bytes (D_00815360) are the class-0 set, the arena
qword is NOP NOP NOP FLUSH and D_00275674 = 0x00814220 in all 15 captures;
13 refusals each break one rule: the colour UNPACK, node order, the GS
state REF's class, the arena REF, the skin record's codes, the CALL target,
a truncated unit, a foreign tag, a model REF qwc, and four GS-state byte
forms (TEST, ZMSK, the GIF tag, the DIRECT code).

**Face units (F).** Every distinct intact face unit in the display lists
of route beats 00..14 (60: Roger's and Dennis's) runs the same way, the
ORIGINAL face program on VU1_FACE_MORPH.md's interpreter against the native
path, with the pieces checked against the unit (colour, node, skin record,
face REF).

| Run | Owner-frames | Units | Clip units | Triangles | Face units | Face triangles | Time (M1) |
|---|---|---|---|---|---|---|---|
| default | 40 of 255 (every behaviour and class, all clip units) | 26 | 14 | 5,911 | 2 of 60 (a drawing Roger, a Dennis) | 2,172 | ~5 s |
| `EM_TEST_FULL=1` | 255 | 119 | 14 | 28,585 | 60 | 46,296 | ~43 s |

Defect injection (2026-09-25): R and G swapped in the kicked colour, the
clip pass skipped and the strip end index shifted are each caught; a TOP
fixed at 0x1B0 for every block survives and is equivalent (each block's
image is rebuilt, so the double-buffer TOP only moves where the same bytes
are computed).

### 8.1 The Metal pixel path (tools/test_object_unit_gpu.py)

The headless fixture `tests/object_unit_gpu_test.c` draws every intact
bank-model unit of a route capture's current display list (beat 03: 15
units, the crates, drums, truck, panel, elevator and pickup; beat 07: the
truck) through `em_object_unit_parse` and `em_gfx_object_unit`, with the
capture's fog, and captures the frame. The test rasterizes the same units'
triangles (em_object_unit_run) at the capture's pixel centres with the
documented pixel path in Python and compares, away from triangle edges
(1.5 output pixels) and depth ties:

| Beat | Units | Triangles | Pixels compared | Exact | Within 2 |
|---|---|---|---|---|---|
| 03 (default run) | 15 | 3,724 | 153,163 | 99.73 % | 99.99 % |
| 07 (`EM_TEST_FULL=1`) | 4 | 2,066 | 301,801 | 99.95 % | 100 % |

Defects injected into the shader (2026-09-25): the bilinear sample point
without its -0.5 texel offset (32 % exact) and the fog weight 256 - F (60 %
exact) are both caught. The model is of the port's pixel path, not of the
GS: it proves the shader implements section 7.2, not that the GS
rasterizes the same.

## 9. Proof live (the level smoke)

The tick log's `owner_units` field records, per 001CAA00 call of the last
drawn frame, the owner's record address, the unit's byte count, the clip
pass and digests of B, of the lighting rows, of the position rows, of the
point-light slots and of the lighting rows' lanes y and z.
`check_owner_units` (tools/test_level_smoke.py) runs the ORIGINAL 001CAA00
over each route snapshot a phase aligned (08, 10, 11, 12, 13, 14) and
compares:

- wherever both drew: B, and the lighting rows' lanes y and z (A's columns
  1 and 2, the room rig's slots 1 and 2) — equal for all 14 such units;
- the whole lighting rows only where the port's point-light slots equal the
  snapshot's: in no snapshot so far. The slots' sway angle and matrix
  follow rand() in 001D7C30, and the port's rand() order differs from the
  original's (the known RNG audit, FIRST_LEVEL_CENSUS.md); position, colour
  and weight of the one AREA11 light are equal;
- in the camera-exact beats (10, 14): the set of owners that ran 001CAA00,
  their byte counts and clip passes, and the position rows (node x VP) —
  equal (the door and the truck in beat 10; all culled in 14).

The smoke's phases all pass with the owners on this path (18 live phases
through Roger, side beats 00 and 09).

## 10. Binding (live)

`src/game/em_owner_draw_live.{h,c}`; callers: `em_area11_boxes.c` (h_draw,
the crates, drums and truck; `em_area11_boxes_door_draw` for the door,
whose runtime palette is the nodes' +0x90, DOOR_ORIGINAL.md).

| Original | Binding |
|---|---|
| 001CAA00, 001CA990, 001C7420 | em_owner_services over the owner's view (+0x02, +0x03, +0x44, +0x80, +0x90, +0x94, +0x98, +0xB0 from the pool record, the +0x110 slots' +0x90) |
| 001CA7B0 | em_owner_draw_001CA7B0: D_00810610 (the live camera's pool, the render context's external view) and context +0x2410 |
| 001D8C20(0) | context +0x246C = 0 |
| 001D89D0 | em_actor_light_w_001D89D0: D_00251C50 and D_00253170 (the render context's ELF .data, `render_context.emrc`; the block now runs through D_00253170), the rig record D_00817BC0 (this module's storage) and D_00275688, context +0x0C / +0x2380 / +0x246C, D_00810700, D_00810610, the point-light slots (em_point_light's pool, copied per draw), the owner's +0x80 words |
| 001C7420's packets | the render context's channel-0 cursor (context +0x10) in its packet arena; 0x70003AC0 from its scratchpad copy |
| 001D1F80(0, 1, 0) | `em_rcl_001D1F80` (the render context's veil module; the cursor word is handed over and taken back) |
| 001CA940 | em_owner_draw_001CA940 over the AREA11 bank |
| 001CB3C0 | not bound: an owner with +0x90 != 0 faults (Roger's face, section 11) |
| skin_arena_init (001D2E20) | `em_rcl_skin_arena_init`, run by the frame machine's 001D19E0 binding at every area load (the rest of 001D19E0 stays unmirrored, RENDER_CONTEXT.md 8.4) |

**The GS state and arena REFs are checked by address.** 001D0F20 builds
the GS state packets (and the arena qword) at boot and is not translated,
so the port's copy of those bytes is not built. The parser requires the
REF 9 to name D_00815360 (set 1, class 0) and the REF 1 to name
0x00814220; the reference test proves their bytes are the class-0 set and
NOP NOP NOP FLUSH in every capture.

**Assets.** `python3 tools/export_world_models.py`,
`python3 tools/export_object_textures.py` and (block size changed)
`python3 tools/export_render_context.py` (STARTUP.md). A missing texture
export faults at the first drawn unit.

**Retired stand-ins.** The legacy crate / drum / truck EMDL meshes
(`props/enemy_crate.emdl`, `enemy_egg.emdl`, `props/area_truck.emdl`) are
no longer loaded by em_area11_boxes, and the fence door's runtime model is
no longer uploaded as a mesh: their +0x4C is the unit above.

## 11. Owners not on this path yet

| Owner | Draws today | Waits on |
|---|---|---|
| player (0x8102B0; +0x4C = 001CAA00, 21 nodes, model at 0x00D1C1C0, 149 blocks) | the legacy player EMDL through em_gfx_draw_skinned | an export of the player's model bytes (verified against RAM like the bank); an owner view over the record pose's node records; the seven equipment nodes (0018A6B0) on the same path, since the legacy player mesh carries their models; em_weapon's bone lookup (em_gfx_last_skinned_bone reads the player's draw) moved to the record |
| Roger (008237E0; +0x90 = his face) | roger.emdl with the opening face through em_gfx_draw_skinned | the face unit's EE builders: 001CB3C0 (and its 001026D0 / 001029C0), 001C7900 and 001CB2C0 (verified-unbound in em_anim_runtime_rest), 001D3F50 -> 001D3E40 (untranslated; the decomp's C is a NEARMISS), and the face state's weights from their original updater. The renderer side is ready: em_gfx_object_unit runs a face unit (section 8 F). Roger's body model (0x47) and the unit resolver for the Roger export's models are the binder's |
| Roger's equipment 001C5C90 | opening/equipment_6b.emdl | a model resolver for the D_0028A56C library (the Roger export holds model 0x6B) |
| elevator, panel, pickups, fan, husks, parachute, 001C4820, 001C5760, 001C5680 | their legacy meshes | each owner live on its original record with bone slots (their own census lanes) |

## 12. Limits

- **Rasterization is Metal's.** The kicked values are exact; coverage and
  the per-pixel interpolation of RGBA, F, S, T, Q and depth are GPU float
  at the output resolution, floored with a 0.001 epsilon, not the GS's DDA
  (the shader is checked against a model of these formulas, section 8.1).
  The captures' GS freezes hold no rendered frame (PCSX2's hardware
  renderer keeps it on the GPU), so no framebuffer comparison is possible;
  a screenshot comparison (the units of beats 03 and 07 through a Python
  model of this pixel path, composited over `original.png`) was inspected
  by eye: positions, shapes and textures agree where the owners are
  visible. It is not a test.
- **VU1 arithmetic** is the VU0 rule set, assumed (VU1_OBJECT_KERNEL.md 4).
- **The lighting rows** are compared live only in lanes y and z (section
  9) until the port's rand() order is the original's.
- **The fog-off REF 2** has no AREA11 capture (context +0x0C bit 0 is set
  in all of them).
- **The model placement** (0x01335F40) is a RAM placement checked in every
  capture (section 4).
- **Beat 15** (the level exit) is out of scope.
