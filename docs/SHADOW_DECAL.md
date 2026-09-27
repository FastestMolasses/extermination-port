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

Live since census L29 (2026-09-26): section 5. Drawn from the chain page
since WP-13 (2026-09-26, docs/CHAIN_PAGE.md).

## 1. Census rows of this lane

| Function | Decomp | Census now | After this lane | Evidence |
|---|---|---|---|---|
| 001CE300 decal packet | NM (.s read) | verified-unbound, but CENSUS_UNVERIFIED.md says it is really **missing** | verified-unbound (live since census L29) | em_shadow_decal_001CE300; RAM + scratchpad byte-exact over 31 route cases and the unit sweep |
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

`python3 tools/test_shadow_decal_reference.py` (`make
test-shadow-decal-reference`). The default run takes about 5 s on an idle
machine (plus the Metal part's one-time library build, cached in
build/shadow_decal_reference/). `EM_TEST_FULL=1` takes the time
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
- **The pixels** moved with the decal's draw to the chain page (WP-13,
  docs/CHAIN_PAGE.md): `tools/test_chain_page_gpu.py` checks the same five
  fan pixels and the refusals on `em_gfx_gs_prims`, the generic GS path that
  replaced the dedicated `em_gfx_shadow_decal_fan`.
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

## 5. Binding (live since 2026-09-26, census L29)

`src/game/em_shadow_decal_original.c` is in COMMON. em_shadow_live
(src/game/em_shadow_live.c; SHADOW_ACTOR_ROUTE.md section 4) binds it as
the route's `submit` (`em_shadow_decal_w_001CE300`, context = its
`EmShadowDecal`):

- `stage`: the module's 640 words for D_008112C0..D_00811CBF, zero at start.
- `scratch.s3600`: the module's one 0x70003600..0x7000363F block, the same
  words as the route's `s3600` and its 001CD390's globals; `scratch.s3AC0`:
  the render context's scratchpad camera.
- `workers`: `em_shadow_decal_bind_packet_chain` over the render context's
  packet chain (`em_rcl_packet_chain()`: the context, the packet arena, the
  chain table D_007635C0), wrapped so that 001CB5F0 records each packet it
  opens.

**The draw (since WP-13, docs/CHAIN_PAGE.md).** After 001CE300 returns, the
recorded packets are checked (the fans of pass 0 and pass 1, then the TEX0
packet): the TEX0 packet must be the TEX0_1 A+D write of
`EM_SHADOW_DECAL_TEX0`, each fan the DIRECT code and GIF tag 001CE300 writes
(NLOOP n, EOP, PRE, PRIM 0x7D, PACKED, REGS ST / RGBAQ / XYZF2) with no ADC
vertex, and the tag must be 1; anything else faults (at 001CE300 /
001CB950). The packets stay in slot 0 of the page, and the chain page's
consumer (em_chain_page_live) draws them with everything else on the page, in
the order the DMA sends it (slot 0 first; within it newest first: the mode-1
blend preset, the TEX0 packet, the fans of pass 1, then pass 0), through the
generic GS path `em_gfx_gs_prims`: the preset's A+D writes are read from the
port's own copy of 001D0F20's bank (em_gs_blocks_original), no longer assumed
by the renderer. The consumer reports the fan triangles it drew with the
decal's TEX0 back to `em_shadow_live_page_drew`, which requires exactly this
frame's fans' (the sum of n - 2) and then marks the decal flushed.

**The texture** is one of the chain page's
(`assets/scene_snow/page_textures.emot`, `tools/export_page_textures.py`,
STARTUP.md row 52, which replaced `export_shadow_decal_texture.py` and
`shadow_decal.emdt`): the 16 x 16 texels through the CLUT (raw GS alpha),
decoded from the route captures' GS memory and required to be identical in
every capture. Its uploader is still not identified (section 4).

**Evidence.** The level smoke's check_shadow (C): the original 001CE300,
run over the port's sampled inputs, writes the port's packets byte for byte
into a cleared slot 0, with the mode-1 blend reference; check_chain_page:
sampled pages re-walked with the original's DMA / VIF / GIF rules draw the
port's primitives, the decal's among them. The GS pixels:
tools/test_chain_page_gpu.py. Live frames by eye (route 04 end,
build/captures/shadow_live/): the dark blob under the player on the elevator
floor, as in the capture.

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
- **S3: the texture uploader** (section 4). The live texture is exported
  from the user's own route captures' GS memory; a machine without those
  captures cannot produce it, and the first page that draws the decal then
  faults (fail-stop, no stand-in blob).
- **S5: the page's other producers** are drawn with the decal since WP-13
  (docs/CHAIN_PAGE.md), in slot order.
- **S6: rasterization.** Metal's float interpolation, floored with a 0.001
  epsilon, stands for the GS DDA (as the object units and the receivers);
  no GS dump of a drawn decal exists to compare pixels with.
- **S4: VU0 registers.** As in SHADOW_ACTOR_ROUTE.md L4, the VU0 registers
  these routines leave behind (vf1..vf28, ACC, Q) are not modelled. Every
  later user on this path reloads them.
- **Census.** The five rows of section 1 are live since census L29
  (FIRST_LEVEL_CENSUS.md section 1.20); `test_census_unverified_reference.py`'s
  `001CF470/missing` key was retired by the render context step, and its
  CF470 reference generator still passes. This test proves the translation
  reproduces the pinned digest.

## 7. Results log

- 2026-09-25, quick: PASS. The counts are the ones section 3 gives: 1,500
  leaf, 40 clip (digest = the census pin), 240 unit, 31 route (31 calls,
  byte-exact), 25 branches both ways, 19 fault cases, and the texture
  identical in 4 beats.
- 2026-09-25, `EM_TEST_FULL=1`: PASS (section 3.1).
