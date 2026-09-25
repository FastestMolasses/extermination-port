# The drop-shadow decal packet: 001CE300, 001CF470, 001CF870, 001CF970, 001CB950

Lane "shadow-decal", 2026-09-25. Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

When the player stands on an actor (player `+0x214 != 0`: the elevator car,
a crate, the truck), the post-step 0015C160 draws the shadow through
0015BF90 → 001F9100 → 001F8D30 (docs/SHADOW_ACTOR_ROUTE.md), not through the
projected shadow 001DA6A0. 001F8D30 ends in `001CE300(1, corners, TEX0,
rgba)`. SHADOW_ACTOR_ROUTE.md limit L1 left 001CE300 untranslated, so the
route stopped there. This lane translates it, with everything it calls
except the page builders that em_packet_chain_original already has.

Files:
- `src/game/em_shadow_decal_original.{h,c}`: the translation.
- `tools/test_shadow_decal_reference.py`: the original-instruction oracle
  (section 3).

Nothing is wired. Section 5 has the binding notes and the Makefile hunk.

## 1. Census rows of this lane

| Function | Decomp | Census now | After this lane | Evidence |
|---|---|---|---|---|
| 001CE300 decal packet | NM (.s read) | verified-unbound, but CENSUS_UNVERIFIED.md says it is really **missing** | verified-unbound | em_shadow_decal_001CE300; RAM + scratchpad byte-exact over 31 route cases and the unit sweep |
| 001CF470 frustum clipper | hand-written asm | unverified ("em_shadow_actor_route", module not linked; CENSUS_UNVERIFIED.md: **missing**) | verified-unbound | em_shadow_decal_001CF470; census generator cases equal, digest = the census pin |
| 001CF870 edge outcode | hand-written asm | boundary, in "resource / display-object registry" (a mislabel: it is a clipper leaf) | verified-unbound | em_shadow_decal_001CF870; leaf sweep + inside every clip |
| 001CF970 plane crossing | hand-written asm | boundary, same group (same mislabel) | verified-unbound | em_shadow_decal_001CF970; leaf sweep + inside every clip |
| 001CB950 TEX0 A+D packet | nonmatching .s (the C is readable) | boundary, "GS/VIF packet build (sprites, flush)" | verified-unbound | em_shadow_decal_001CB950; inside every 001CE300 case |

Reused, not translated again: 0011DF78 (`em_sdk_math_original_0011DF78`),
001CB5F0 / 001CB900 (`em_packet_chain_w_001CB5F0` / `_w_001CB900`, through
the workers), and 00121870 block_copy, which here always copies one whole
0x50-byte record. 001CD370 is a worker. Its packet-chain adapter reads
`D_00275670 + a0 * 0x40 + 0x2240` through the chain's regions.

## 2. What the original does (read from the original instructions)

### 001CE300(tag, quad, tex0, rgba)

1. The four colour bytes are written as words to 0x70003600..0x7000360C
   (r, g, b, a, each 0..255).
2. There are two passes. Pass p draws the triangle of corners
   {p, p+1, p+2}. For each of its three corners j:
   - clipper input record j (D_008112C0 + 0x50·j) gets the corner quadword
     at +0x00;
   - it gets the texture coordinate at +0x30 / +0x34. That is (0,0) for
     corner 0, (0,1) for corner 1, (1,0) for corner 2 and (1,1) for
     corner 3.
   The other words of the record keep whatever they held.
3. `n = 001CF470(D_008112C0, 001CD370(0))`. 001CD370(0) is the render
   context's +0x2240 clip matrix. If n = 0, the pass draws nothing.
4. Otherwise it opens a packet of `3n + 2` quadwords on page D_007635C0 with
   depth key 0 (001CB5F0). The packet holds:
   - qw 0: zero, with the word +0x0C = `0x50000000 | (3n + 1)` (DIRECT);
   - qw 1: a GIF tag with NLOOP = n (sign-extended), EOP and PRE set. PRIM
     is 0x7D: triangle fan, Gouraud, textured, fogged, alpha-blended, STQ.
     The mode is PACKED with three registers (REGS 0x412: ST, RGBAQ,
     XYZF2);
   - n vertices of three quadwords each, from the fan records
     D_008117C0 + 0x50·k.
5. Each vertex is computed like this. `c` = the camera rows 0x70003AC0 times
   (x, y, z, 1): the record's w is never read, because the transform's last
   weight is vf0.w.
   - **XYZF2**: x and y times the VU reciprocal Q = 1 / c.w. The w lane
     becomes c.w - 1.0. z times a second VU reciprocal 1 / (c.w - 1). The
     fog lane is `max(min(fog.z + fog.w · (c.w - 1), fog.x), 0)`, where fog
     is the render context's +0xA0 quadword (read once per pass, after the
     packet is opened). All four lanes are converted to 12.4 fixed point.
   - **RGBAQ**: the four words at 0x70003600, re-read for every vertex.
   - **ST**: `S = s · invw`, `T = t · invw`, then the word invw, then 1.0.
     Here `invw = 1.0 / c.w` is a COP1 division (rounded differently from
     the VU reciprocal of the x/y lanes).
6. After both passes it calls `001CB950(D_007635C0, 0, tex0)`, then
   `001CB900(D_007635C0, 0, tag)`.

The readable NEARMISS C is wrong on the z lane: it divides where the
original multiplies by a reciprocal, so there are two roundings, not one.
It is also wrong on the fog lane: it uses a separate compare formula, not
the w-lane min / max. It also presents S/T/Q as a plain division. The
translation follows the instructions.

### 001CF470(tri, m): the clipper

1. The three input records get `+0x40 = m · (x, y, z, 1)`.
2. Then three passes run, on axes z, y and x in that order. Each pass reads
   one buffer and writes the other: input D_008112C0 and output
   D_008117C0 first, then back, then forward again. So the result ends in
   D_008117C0.
3. For every edge `i → (i + 1) mod n`, 001CF870 gives an outcode:
   - bit 0x01: the start's lane is above |w| (the "≤ |w|" test fails);
   - bit 0x02: the start's lane is below -|w|;
   - 0x10 / 0x20: the same two tests for the end.
4. What the pass emits for each code:

| Code | Emits |
|---|---|
| 0x00 | a copy of the start record |
| 0x01 | the +w crossing |
| 0x02 | the -w crossing |
| 0x10 | the start, then the +w crossing |
| 0x20 | the start, then the -w crossing |
| 0x21 | the +w crossing, then the -w crossing |
| 0x12 | the -w crossing, then the +w crossing |
| 0x11, 0x22 | nothing |
| anything else | nothing (it cannot occur) |

5. If a pass emits nothing, the result is 0. Otherwise it is the final
   count.

The classification uses |w| but the crossing uses sign · w. Behind the eye
(w < 0) this is not a true frustum clip. The translation keeps that, and
the unit sweep includes such quads.

### 001CF870(a, b, axis) and 001CF970(out, a, b, axis, sign)

001CF870 gives the outcode above, from two COP1 compares per end against
`0011DF78(w)` and its negation.

001CF970 loads both records whole, then computes:
- `da = |a.clip - sign·a.w|` and `db = |b.clip - sign·b.w|` on all lanes.
  The low three words of both 128-bit copies are then rotated `axis`
  times, so lane x holds the lane of the axis;
- t = |da.x / (db.x + da.x)|, through Q;
- every quadword of the record (position, +0x10, +0x20, the texture
  quadword, the clip position) = `a + (b - a) · t`.

The clip position is stored last. Nothing is read after a store, so `out`
may alias an input.

### 001CB950(table, id, tex0)

It opens 3 quadwords (001CB5F0):
- qw 0: zero, with +0x0C = 0x50000002 (DIRECT 2);
- qw 1: the A+D tag (NLOOP 1, EOP, NREG 1);
- qw 2: `tex0` for register 0x06 (TEX0_1).

It returns the packet address. 001CE300 ignores it.

### The order the GS sees

The page's slot lists run newest to oldest (PACKET_CHAIN.md). Slot 0 of
D_007635C0 therefore holds, in execution order:
1. the blend-state block of mode `tag` (001CB900 appends it last);
2. the TEX0 packet;
3. the fan of pass 1 (corners 1, 2, 3);
4. the fan of pass 0 (corners 0, 1, 2).

Anything else the frame puts in slot 0 is interleaved by append time.

## 3. Verification

`python3 tools/test_shadow_decal_reference.py`. The Makefile target is in
section 5. The default run takes about 5 s. `EM_TEST_FULL=1` takes the time
in section 3.1.

The interpreter is the shadow-route EE: every COP1 and VU0 operation goes
through `tools/ee_float_model.py`. This test adds the MMI three-word rotate,
and records branch outcomes at any call depth. No shared file is edited.

- **Route (31 cases).** 0015BF90 runs as original over every in-scope beat
  (00..14) and the opening capture, as captured and with the scripted 0x41
  path. Everything runs as original: 0019A570 over the captured collision
  world, 0011E620, 001CD390 and the rest. Its 001CE300 call is caught with
  the full state at entry. The original 001CE300 then runs, with all its
  callees. The native one runs, bound through
  `em_shadow_decal_bind_packet_chain` to `em_packet_chain_original`, over a
  copy of the same 32 MiB and the same scratchpad.
  **All of RAM and all of the scratchpad must be equal afterwards.** That
  covers the packets, the page slot and head words, the context cursor, the
  clipper buffers and 0x70003600.
  Every beat submits exactly one quad with tag 1 and the documented TEX0.
  The fan sizes are 3/3 for most beats, 4/3 in beat 02 and 3/4 in beat 09;
  the opening culls one triangle. rgba is 0xA2050505 in most beats,
  0xA4050505 in 05, 0xA1050505 in 12 and 0xD7060606 in the opening.
  Beats 04, 05 and 08 are the elevator ride, the crates and the truck, and
  all three are byte-exact.
- **Unit (240 of 20,000 cases).** 001CE300 is called directly over the RAM
  of beats 02, 04, 05 and 08. The quads are:
  - decal squares from 0.2 to 3000 units, in random orientations near the
    player;
  - free corners at radii 4 to 40,000, which cross every plane and w = 0.

  The cases also vary: the tag (0..5 and -1), the TEX0, the colour, junk in
  the clipper records (NaN, Inf and denormal patterns), other beats'
  cameras, and other fog coefficients (a negative fog maximum included).
  The same whole-RAM / scratchpad comparison applies. The fan sizes seen
  run from 3 to 7.
- **Clip (40 cases; 400 full).** 001CF470 runs alone on the exact generator
  and seed of `test_census_unverified_reference.part_1cf470`. For every
  case the native count and all 0xA00 bytes of both buffers must equal the
  original's. The digest of the original's fans must equal the census's
  pinned `CF470_REFERENCE`, in both modes.
- **Leaf (1,500 of 50,000 cases).** 001CF870 and 001CF970 run alone on
  random records. The records include signed zeros, denormals, NaN / Inf /
  MAX patterns, degenerate edges, lanes exactly on the plane, w = ±0, axis
  3 for the outcode, both signs and odd sign words. Some cases alias the
  output with an input.
- **Branches.** All 25 conditional branches of the five routines are taken
  both ways, except 2 outcomes that cannot occur:
  - 001CE300's last corner test failing (it needs a corner outside 0..3);
  - 001CF470's last outcode test failing (it needs a lane above |w| and
    below -|w| at once).

  The test asserts that neither outcome is ever seen.
- **Callee set.** The jal targets of the five routines are exactly 001CD370,
  001CF470, 001CB5F0, 001CB950, 001CB900, 0011DF78, 00121870, 001CF870 and
  001CF970.
- **Faults (19 cases).**
  - Each worker, scratch pointer, stage, the worker table or the quad
    missing gives UNBOUND at 0x001CE300, with nothing written.
  - Each of the 8 worker calls failing in turn gives WORKER at that
    worker's address (the fog read at 0x001CE50C), and the latched fault
    refuses the next call with nothing written.
  - The leaves refuse axis -1 / 4.
  - The `em_shadow_decal_w_001CE300` adapter equals the entry point.
- **Mutations (manual, this session).** Each of these makes the test fail:
  - z using the first reciprocal;
  - the 0x21 signs swapped;
  - corner 1 / 2 texture coordinates swapped;
  - the rotate direction reversed;
  - the fog clamp order;
  - the outcode's ≤ made <;
  - invw taken from the VU reciprocal;
  - the axis order;
  - the record's w used as the transform weight;
  - the 0x10 case copying the end;
  - the TEX0 register number.

  These survive, because each is equivalent on the EE:
  - the operand order of `db + da` (a NaN sum only feeds the VU divide,
    which returns +MAX for any NaN);
  - the |t| (t is never negative);
  - NLOOP zero-extended (the count is never negative).

### 3.1 Full sweep

`EM_TEST_FULL=1` (2026-09-25, about 5 minutes on 8 workers): PASS.
- 50,000 leaf cases.
- 400 clip cases. The fans are {0: 214, 3: 88, 4: 71, 5: 21, 6: 6}, and
  the digest equals the census pin.
- 20,000 unit cases. The fan sizes are {3: 9193, 4: 5822, 5: 2373, 6: 402,
  7: 28}.
- 31 route cases, byte-exact.
- The texture is identical in all 15 in-scope beats.

## 4. The decal texture: a boundary for the renderer

The TEX0 word 001F8D30 passes is 0x2004290511322469. It decodes to:
- TBP0 0x2469 (byte address 0x246900), TBW 8;
- PSMT8, 16 × 16 texels;
- TCC 1 (RGBA), TFX 0 (MODULATE);
- CBP 0x2148, CPSM PSMCT32, CSM1, CSA 0, CLD 1 (the CLUT is loaded with
  this TEX0).

The test decodes it from the constant and asserts each field.

The texture and its CLUT are **resident in GS memory in every captured
beat** (route beats 00..15). Their digests are identical
in every beat, and the texels use only indices 0..15. The 16 CLUT entries used
are grey (128, 128, 128) with a rising alpha, and the texel index is 0 at
the corner and 15 at the centre, so the decal is a radial blob carried in
the alpha channel. The test checks residency and equality of the digests across
beats (4 beats by default, all in full mode). Nothing disc-derived is
printed or stored.

**The uploader was not identified.** No boot-ELF instruction builds 0x2469
except 001F8D30's TEX0. Beat 04's captured RAM holds no BITBLTBUF aimed at it.
The texture is data-driven, probably part of a resource the area load
uploads.

Until the uploader is translated, the renderer has two options. It can take
the texture from an export of a captured GS image into the ignored
`assets/`. Or it can fault. It must not substitute a generated blob.

The draw state the renderer must reproduce is the blend-state block of
mode 1: 001CB900(page, 0, 1) → 001CB9B0(1) = D_00275674 + 0x720 = 0x814940
on the route. It is the same in the captured beats and holds these A+D
writes:

| Register | Value | Meaning |
|---|---|---|
| PRIM | 0x17E | sprite, IIP, TME, FGE, ABE, FST (a state write; the fan's own PRE PRIM follows) |
| TEX1_1 | 0x60 | MMAG and MMIN linear (bilinear, no mipmaps) |
| TEST_1 | 0x53001 | alpha test on, ATST NEVER, AFAIL RGB_ONLY (RGB written, no alpha and no Z write); Z test on, GEQUAL |
| ALPHA_1 | 0x44 | (Cs - Cd) · As >> 7 + Cd |
| CLAMP_1 | 0 | REPEAT |
| COLCLAMP | 1 | clamp |

With TFX MODULATE and TCC 1, the source colour is the vertex RGBA times the
CLUT texel. The vertex colour is the (8, 8, 8, 255) colour scaled by the
height fade in 001F8D30. The texel alpha times the vertex A, then
`>> 7`, darkens the frame under the blob. The fog lane per vertex comes from
the +0xA0 fog block, and FOGCOL is whatever the frame set.

## 5. Binding notes

Nothing is wired. The module is built only by its test.

**Makefile (lead-owned).**

```make
.PHONY: test-shadow-decal-reference
test-shadow-decal-reference:
	python3 tools/test_shadow_decal_reference.py
```

When bound, add `src/game/em_shadow_decal_original.c` to COMMON. Its
dependencies `em_packet_chain_original.c`, `em_status_ui_leftovers.c` and
`em_sdk_math_original.c` are already there.

**em_shadow_actor_route (the caller).** Its `submit` worker has exactly the
signature of `em_shadow_decal_w_001CE300`:

```c
workers.submit  = em_shadow_decal_w_001CE300;
workers.context = &b->decal;          /* an EmShadowDecal */
```

Tag 1, the corners, TEX0 and rgba pass through unchanged. The route test
already verifies them. `EmShadowDecal` needs:
- `stage`: 640 words the coordinator owns for D_008112C0..D_00811CBF.
  Zero-initialise them once. The packet never depends on the stale words,
  only the buffer bytes do. The route's other clip user, 001CDDC0, is not
  on the first-level route.
- `scratch.s3600`: the same pointer as `EmShadowActorRouteScratch.s3600` and
  `EmEffectOriginalGlobals.spad3600`, the one scratchpad image. 001F8D30
  writes P0 there, and 001CE300 overwrites the first 4 words right after.
- `scratch.s3AC0`: the frame's camera rows (the same as the route's `s3AC0`).
- `workers`: `em_shadow_decal_bind_packet_chain(&w, &chain)`. `chain` is
  the frame's `EmPacketChain`. Its regions must map:
  - the render context: +0x00..+0xFF for the cursor and the fog, and
    +0x2240..+0x227F for the clip matrix;
  - the packet buffer the +0x18 cursor walks;
  - the page table D_007635C0 (0x8000 bytes).

**The blocker is the same as the effects'.** No live code produces the
render-context views: the +0x2240 clip matrix, the +0xA0 fog, the packet
cursor and the page table, nor a consumer of page D_007635C0
(EFFECT_MANAGER.md 5.0, census lanes L32 / L30). Until the canonical
render-context block and its page consumer exist:
- keep `submit` **unbound**. The route then faults UNBOUND at 0x0015BF90
  before any write (SHADOW_ACTOR_ROUTE.md section 4);
- or bind it and let the missing regions fault at 0x001CD370.

Never draw a stand-in blob.

**The Metal shadow passes.** `em_gfx_shadow_*` (em_gfx.h, em_shadow_gs.h)
are the GS side of 001DA6A0: the silhouette target and the receivers. They
are not this route. The decal needs one of two things:
- **a generic consumer of page D_007635C0** that plays the GS packets it
  holds in slot order (A+D state blocks, TEX0 with CLUT load, PACKED
  ST / RGBAQ / XYZF2 fans with PRE PRIM). The effects and the head sprite
  need the same consumer, so this is the preferred shape;
- or a dedicated `em_gfx_decal_fan(gfx, xyzf[], stq[], rgbaq[], n, tex0,
  blend-state)` that takes the words this module writes, verbatim.

Either way the renderer's inputs are exactly the packet words verified
here. The renderer must:
- rasterise the 12.4 XY and the integer Z with ZTST GEQUAL and no Z write;
- sample the 16 × 16 PSMT8 texture bilinearly with REPEAT through the CLUT;
- modulate, fog with F, and blend (Cs - Cd) · As >> 7 + Cd into RGB only.

In the original frame the decal belongs where page D_007635C0's slot 0
is spliced (001CB800), not in the 001DA6A0 pass order.

## 6. Limits and open items

- **S1: route coverage.** Only the end snapshots of beats 02 and 04 have
  `+0x214` set. At the end of beats 05 and 08 the player is off the crate
  and the truck. The route cases still run the decal over those beats'
  captured players and worlds, and every beat is byte-exact. 0015BF90
  reads no `+0x214`; that is the only difference on the actor. A mid-beat
  frame on a crate or on the truck is not captured (SHADOW_ACTOR_ROUTE.md
  L3).
- **S2: the RANGE guard is unexercised.** A clipper record index outside
  the 32 records would make the original read or write the render context
  at 0x00811CC0. The translation faults instead. For a triangle, the
  |w| / sign · w mismatch allows at most 5 → 9 → 17 outputs in theory, so
  only a pathological quad could reach it. No generated case did.
- **S3: the texture uploader** (section 4).
- **S4: VU0 registers.** As in SHADOW_ACTOR_ROUTE.md L4, the VU0 registers
  these routines leave behind (vf1..vf28, ACC, Q) are not modelled. Every
  later user on this path reloads them.
- **Census.** Five rows change (section 1). `test_census_unverified_reference.py`
  pins the divergence `001CF470/missing` and fails when a pinned divergence
  disappears. Its function probe now finds `em_shadow_decal_001CF470`, so
  that key must be removed from its `EXPECTED`, and CENSUS_UNVERIFIED.md's
  001CF470 section updated (lead-owned files). Its CF470 reference
  generator itself still passes. This test proves the translation
  reproduces the pinned digest.

## 7. Results log

- 2026-09-25, quick: PASS. The counts are the ones section 3 gives: 1,500
  leaf, 40 clip (digest = the census pin), 240 unit, 31 route (31 calls,
  byte-exact), 25 branches both ways, 19 fault cases, and the texture
  identical in 4 beats.
- 2026-09-25, `EM_TEST_FULL=1`: PASS (section 3.1).
