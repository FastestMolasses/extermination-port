# Actor lighting driver 001D89D0: bit-exact translation

Date: 2026-09-24. Lane `light-001D89D0`. **Bound since 2026-09-25** as the
`w_001D89D0` of the live owner draw (`em_owner_draw_live`, OWNER_DRAW.md
section 10): the crates, drums, truck and fence door. Section 6 records how
the recipe was followed.

`em_owner_services_001C7420` asks its `w_001D89D0` worker for two matrices:
the light-direction matrix A (SPR 0x70003400) and the colour matrix B (SPR
0x70003440). Until now nothing native produced them bit for bit.
test_owner_draw_reference lane D borrowed the original's A and B.
ACTOR_LIGHTING.md recomposes the rig for the renderer (em_lighting). That
recomposition is proven equal on 14 actors under its caller contract, but it
is not a translation of this entry point. This lane is that translation. It
is proven over every captured owner draw and over synthetic states.

This document holds addresses, field offsets and behaviour only. It holds no
original code, data or disassembly.

## 1. Delivered

| File | What |
|---|---|
| `src/game/em_actor_light_001D89D0.{h,c}` | 001D89D0 and everything it reaches: 001D8C30, 001D8130, 001D7B30, 001D2910(8) = 001D2710(8), 001D8340, 001D8270, 001D8690, and the SDK VU0 routines 001026A0, 00102738, 00102760, 00102798, 001028B8, 001028D0, 00102900, 00102948 / 00102958. It reuses em_owner_services' verified 001029C0, 00102A60, 00102B08 and 00102BB0. The binding adapter `em_actor_light_w_001D89D0` has the `EmOwnerServicesWorkers.w_001D89D0` signature. |
| `tools/test_actor_light_001d89d0_reference.py` | The original-instruction oracle (section 5). |

Arithmetic: every COP1 and VU0 macro instruction goes through em_ee_float.h
under its real form. No host float operation is performed. All of this code
runs on the EE (COP1 and VU0 macro mode), whose rules EE_FLOAT_MODEL.md
measured. **No VU1 code is involved**, so the model's unmeasured VU1
assumption is not exercised here. Every form used is in the measured table:
MULAbc x, MADDAbc y/z and MADDbc w over xyzw, MUL xyz, ADDbc x (y, z), ADDq x,
VDIV (3,0), VSQRT, SUB xyzw, MULq xyz, ADD xyzw, and MULbc xyzw with x.

## 2. What the original does

The routine is called as **001D89D0(owner, A, B, arg3)**. 001C7420 passes
A = SPR 0x70003400, B = SPR 0x70003440 and arg3 = owner + 0x80.

**Mode dispatch.** The routine reads the lighting mode at context +0x246C
(written by 001D8C20). For modes 1, 3, 4, 5 and 6 it calls **001D8C30(mode,
A, B, arg3)** and returns. The decomp's NEARMISS C shows `func_001D8C30(mode)`
only; the .s leaves a1..a3 untouched, so A, B and arg3 pass through. Every
other mode (0, 2, 7 and up, negative) takes the rig path. 001CA990 always
sets mode 0 first, and every captured context holds 0.

**001D8C30(mode, A, B, in)**:

- t = in.w − 1.0, set to +0 when t ≤ 0 (EE compare).
- The mode selects a case through a seven-entry jump table as an unsigned
  compare: 0 and 1 share the first case, and every value ≥ 7 (negatives
  included) also takes the first case.
- Case 0/1/other: A = 0 and B rows 0..2 = 0. B row 3 = 8388608 + (128 + in.xyz)
  with the inner add first, and B.w = 8388608 + 64t.
- Case 2: the same clears, then B row 3 = 8388608 + in.xyz and B.w = 8388608 + 64t.
- Case 3: B row 0 = 8388608 + 128·in.xyz and B[3] = 8388608 + 64t. A is not written.
- Cases 4, 5 and 6: A = the 16 words at context +0x2380, and B row 0 xyz =
  8388608 + 128·in.xyz. B[3] depends on the case:
  - case 4: the word 0x4B000040 (8388672.0);
  - case 5: 0.2 · (128 · in.w);
  - case 6: 8388608 + 128 · in.w.

**The rig path** (the rig record R is D_00817BC0, and D_00275688 = R):

1. **001D8130** stores D_00275688 = R, then copies one room rig into R. The
   rig is entry 001D7B30() of D_00251C50: 45 entries of 0x78 bytes, the
   first whose word 0 equals the key, or entry 0 when none matches. The key
   is 0x0F00 when context +0x0C bit 8 is set (001D2910(8) → 001D2710(8)).
   Otherwise it is D_00810700 · 256 + D_00810701. The copy writes:
   - R+0xB0 = 0 and R+0xB4 = entry+0x1C;
   - the slot angles x and y: R+0x80/+0x84, +0x90/+0x94 and +0xA0/+0xA4;
   - the slot colours and weights: R+0xF0..+0xFC, +0x100..+0x10C and
     +0x110..+0x11C;
   - the ambient R+0x120..+0x128;
   - R+0x12C = 128.0.

   **The z angles R+0x88, +0x98 and +0xA8 are never loaded.** They keep
   whatever the record holds. They are 0 in all 15 route captures.
2. **001D8340(owner, flag = owner+0x02 & 0x20, point)**. The point is
   owner +0xB0 when +0x98 is 0xFF; otherwise it is node(+0x110 + 4·(+0x98))
   + 0xC0, all four lanes.
   - **flag clear:** R+0xF0..+0xFC (slot 0 colour and weight) and
     R+0xC0..+0xCC are zeroed.
   - **Slot directions.** For slot i = 0..2: rot = identity, rotated about X
     by angle +0, then Z by +8, then Y by +4. (The decomp comment on 001D8340
     says "Y from +0, Z from +4, X from +8". The matched C calls X, Z, Y with
     +0, +8, +4. The comment is wrong; the C is right.) Then q = (0, 1, 0, 1).
   - **Slot 0, flag set (camera fill):** q = rot × q (the row transform);
     q.w = 0; R+0xC0 = transpose(D_00810610) × q; R+0xCC = 1.0.
   - **Slot 0, flag clear:** R+0xC0..+0xCC = 0, and the slot's angles
     R+0x80..+0x8C = 0. **So a fill draw after a non-fill draw reads angle z
     = 0.**
   - **Slots 1 and 2:** R+0xC0+16i = rot × q, all four lanes.
   - **The fold,** only when 001D8270(owner) passes. The type bytes 03, 08,
     09, 0B, 0D, 15, 16, 17, 3D and 3E never pass; for other types, the model
     radius [+0x44]+0x20 must be < 30.0. The fold computes:
     - the direction accumulator acc = D_00253170 + R.C0 · R.FC;
     - the colour accumulator wacc = R.F0. The second seed D_00253180 is
       stored and immediately overwritten by this copy, so it is dead.
     - For each of the 32 point lights (context +0x220 + 0x80·j) whose
       weight at +0x2C is not ≤ 0:
       - probe = light+0x10 − point (four lanes);
       - d = probe·probe (xyz; **the squared distance**, since 00102738 is a
         dot product), set to 1.0 when d < 1.0;
       - f = (0.1 · weight) / d;
       - acc += M × (probe · 10f), where M is the 4×4 matrix at light+0x40;
       - wacc += (light+0x20) · 2f.
     - Then R+0xC0 = normalise(acc) (xyz / |xyz| through VSQRT and VDIV;
       w = 0) and R+0xF0 = wacc.
3. **001D8690(A, B, arg3)**, where c = max(arg3.w − 1.0, +0):
   - A columns 0..2 = the slot directions R+0xC0, +0xD0 and +0xE0 (xyz). The
     w lanes of rows 0..2 = slot 0's colour R+0xF0..+0xF8. Row 3 = 0.
   - B row r (0..2) = slot r's colour xyz × arg3.xyz, with w = slot weight ×
     c. Each product is the rig value times the argument, in that order.
   - B row 3 = 8388608 + ambient × arg3.xyz, and B.w = 8388608 + 64c.
4. **Glow** (owner +0x02 bit 0x40): B row 3 xyz += 64 · owner+0x80..+0x88,
   and B.w += 64 · max(owner+0x8C − 1.0, +0). **This reads the owner's own
   +0x80 words, not arg3.** The difference matters only for a caller that
   passes another pointer.

001D89D0 writes nothing else: A, B, R+0x80..+0x12F and D_00275688. The oracle
asserts this on every case.

## 3. The native API

- `EmActorLightWorld`: the views, each named by its original address:
  - D_00275688 and the rig record D_00817BC0 (76 words);
  - context +0x246C, +0x0C, +0x220 (32 × 32 words) and +0x2380;
  - D_00810700/701, D_00251C50 (45 × 30 words), D_00253170 and D_00810610.

  The module keeps no copies. **The rig record must be the one persistent
  canonical record**, not a per-call temporary: the z angles and slot 0
  carry over between calls (section 2).
- `EmActorLightOwner`: owner +0x02, +0x03, +0x98, +0x80..+0x8F, +0xB0..+0xBF,
  the model radius word, and the +0x110 slots' +0xC0 rows.
- `em_actor_light_001D89D0(s, owner, a, b, arg3)`. A and B are
  read-modify-written in place, because cases 3..6 leave rows untouched. The
  callees are public for tests.
- **Fail-stop.** The following latch `EmOwnerServicesFault` codes
  (EM_OWNER_FAULT_*):
  - a reached NULL view: NULL_WORKER, at the address of the routine that
    reads it (0x001D89D0, 0x001D2710, 0x001D7B30, 0x001D8270, 0x001D8340 or
    0x001D8C30);
  - a node index at or past the slots: BAD_INDEX at 0x001D89D0;
  - a refused float form: UNMEASURED_FORM, unreachable with today's table.

  Every view a call will reach is checked before its first store, so a view
  fault leaves A, B, the rig record and D_00275688 untouched. Modes 1 and
  3..6 never read the owner, and mode 3 reads neither the template nor the
  rig. An excluded type reads neither the model nor the fold views. A node
  pointer of 0 faults only if the fold reads it.
- **Binding.** `em_actor_light_w_001D89D0(binding, EmOwnerServicesOwner*, a,
  b)` with an `EmActorLightBinding` as the worker ctx:
  - The fields come from the owner view: cls, kind, pose_bone, pos, the
    model radius, and bone[k]->world row 3 as node +0xC0.
  - `w_owner_rgb` supplies +0x80..+0x8F, which EmOwnerServicesOwner does not
    carry.
  - The adapter runs 001D89D0(owner, A, B, owner + 0x80). A fault returns -1,
    and owner services then latches WORKER_FAILED at 0x001D89D0.

## 4. Coverage in the captures

The 255 owner-frames are every owner with draw method 001CAA00 and a bank
model, in route beats 00..14.

| Property | Owner-frames |
|---|---|
| light point is a node (+0x98 ≠ 0xFF) | 255 |
| fold gate passes | 195 |
| fold with at least one point light of weight > 0 | 195 |
| camera fill (+0x02 bit 0x20) | 0 |
| glow (+0x02 bit 0x40) | 0 |
| context +0x0C bit 8 (key 0x0F00) | 0 |
| D_00253170 nonzero | 0 |

The captures therefore exercise the room rig, the slot rotations, the
non-fill slot 0, the fold with live point lights, 001D8690 and the node
point. The following are proven by the synthetic sweep only:
- the camera fill (and 00102798);
- the glow;
- the 0x0F00 key;
- modes other than 0;
- the +0xB0 point.

## 5. Verification

`python3 tools/test_actor_light_001d89d0_reference.py` (M1, 8 workers):
the default run takes 3.1 s and `EM_TEST_FULL=1` takes 10.0 s.

The oracle is test_owner_services_reference's EE interpreter, with float
arithmetic from ee_float_model. This test adds the four MMI lane interleaves
of 00102798. Before use, the test checks that the extension turns a known
matrix into its transpose. The route captures' code ranges (001D89D0..
001D8FD0, 001D8130.., 001D7B30, 001D2710..001D2960, the SDK routines and
the jump table) are asserted equal to the pinned ELF.

| Part | Default | Full |
|---|---|---|
| A. synthetic 001D89D0 states: all modes (0..8, 100, -1, INT_MIN), fill, glow, fold, node/+0xB0, key hit / double hit / miss / 0x0F00, arg3 ≠ owner+0x80, wide magnitudes, specials (NaN, Inf, ±MAX, denormals, -0) | 440 | 6,000 |
| A. boundary: fold weight +0, -0, ±denormal, smallest normal and 1.0 at d² = 1, 0.25 and 4, with a -0 accumulator | 18 | 18 |
| A. direct 001D8C30: modes 0..8, 100, -1, INT_MIN, with exponent-edge inputs | 96 | 720 |
| A. binding adapter over an EmOwnerServicesOwner | 120 | 1,200 |
| B. captured 001D89D0 after 001D8C20(0), all owner-frames | 255 | 255 |
| C. owner draw chain with the adapter bound, all owner-frames | 255 (119 drawn) | 255 (119 drawn) |
| D. fail-stop checks | 21 | 21 |

**A and B.** In every case A, B (both prefilled with a pattern), the whole
rig record and D_00275688 equal the original's. The original writes nothing
outside them and its stack.

**C. The owner draw chain.** Each case runs em_owner_services_001CAA00 with
this module's C entry point as `w_001D89D0`. The Python callback is only the
+0x80 supplier. The other workers are em_owner_draw, em_load_veil_particles
and a recorded 001D8C20 that writes the module's mode word. The captured
mode word starts in place, so the chain must write 0.

For all **119 drawn units**, every byte of the native display-list window
equals the original 001CAA00 run. It follows that:
- **B** equals the colour CNT payload for VU1 0x3F5;
- **A** equals SPR 0x70003400 after the original run;
- SPR 0x70003440, the rig record and D_00275688 are equal as well.

116 of the 119 units are found byte-exact in the captured display lists. The
3 misses are the pickup frames that lane D reports: the pickup was not drawn
in those frames. The 136 culled owner-frames must not reach the lighting
worker. This replaces lane D's borrowed A and B with a native, proven
producer.

**Defect injection** (2026-09-24). 31 native defects were injected, and the
default run catches 29. The two it misses are equivalent mutants:
- the d² clamp written as ≤ instead of <: it assigns 1.0 to a value that
  already equals 1.0;
- mode 4's B[3] computed as 8388608 + 64 instead of stored as the word: the
  two are bit-identical.

The caught defects:
- rotation order;
- the gate compare and the gate type list;
- the weight compare, caught by the -0 boundary cases;
- the 10f / 2f swap;
- the probe order;
- the missing transpose;
- q.w not cleared, and the fill w;
- the slot 0 angle clear, and the F0 clear;
- the A w lanes;
- the B.w clamp, the B row w product and the ambient bias;
- the glow reading arg3;
- 001D8C30's inner add order, and its mode 5 multiply order, caught by the
  exponent-edge inputs;
- the first-match key, and the 0x0F00 bit;
- R+0x12C;
- mode 2 sent to 001D8C30;
- the dot-product sum order;
- the normalise w lane;
- the seed ignored;
- the row-transform broadcast lane;
- in the adapter: the node row, the +0xB0 position, caught by the adapter
  cases, and the radius.

## 6. Binding (live: em_owner_draw_live)

1. Canonical storage for the views in section 3. The mode word must be the
   same storage the bound `w_001D8C20` writes, because 001CA990 sets mode 0
   just before 001C7420. The rig record and D_00275688 must persist across
   draws and frames.
2. Point the views at the canonical storage. They must hold the values the
   original has at draw time:
   - D_00251C50 and D_00253170, from the user's ELF or RAM;
   - the point-light table at context +0x220, kept by em_point_light's
     owner;
   - D_00810700/701;
   - the view D_00810610.
3. Bind `workers.w_001D89D0 = em_actor_light_w_001D89D0` and
   `workers.ctx = &binding` (the binding shares its ctx with the other
   workers only through `binding.ctx`). Set `binding.w_owner_rgb` to read the
   owner's +0x80..+0x8F words from canonical owner storage.
4. Add `src/game/em_actor_light_001D89D0.c` to COMMON. It needs
   em_owner_services_original.c, which is already in COMMON, and
   em_ee_float.h.

As bound: the mode word is context +0x246C of the render context (the
storage its `w_001D8C20` writes); D_00275688 is the render context's
D_00275670 block; D_00817BC0 is em_owner_draw_live's own storage; D_00251C50
and D_00253170 come from `render_context.emrc` (its D_00250F30 block now
ends at 0x00253180); the point-light slots are em_point_light's pool,
copied per draw; D_00810700/701 and D_00810610 are the render context's
external views; `w_owner_rgb` returns the pool record's +0x80 words. The
level smoke compares B and the lighting rows' rig lanes with the route
snapshots (OWNER_DRAW.md section 9).

## 7. Makefile targets

```make
.PHONY: test-actor-light-001d89d0-reference
test-actor-light-001d89d0-reference:
	python3 tools/test_actor_light_001d89d0_reference.py
```

When the worker is bound, add to COMMON next to
`src/game/em_owner_services_original.c`:

```make
           src/game/em_actor_light_001D89D0.c \
```

## 8. Limits

- **Aliasing.** The original re-reads memory between stores. The
  translation assumes that A, B, arg3, the owner, the rig record and the
  tables do not overlap. The original callers pass the scratchpad, the
  owner and D_00817BC0, which do not overlap.
- **The captures do not reach the fill or the glow.** The camera fill, the
  glow, the 0x0F00 key and modes ≠ 0 are proven against the original
  instructions on synthetic states only (section 4).
- **em_lighting is unchanged.** Its recomposition (ACTOR_LIGHTING.md) stays
  the renderer's contract. This module produces what 001C7420 packs. It does
  not feed `EmGfxCharRig`.
- **Scope.** Beat 15 (the level exit, area 1) is out of scope, as in lane D.
