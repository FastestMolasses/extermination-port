# Player shadow while standing on an actor (original 0015BF90)

When the player stands on an actor (the elevator car in route beats 02 and
04, a crate in 05, the truck in 08), the player word `+0x214` names that
actor. In that state the player post-step 0015C160 does not call the
projected shadow 001DA6A0 (docs/SHADOW_ORIGINAL.md). It calls 0015BF90
instead. Until now `em_shadow_original_route_0015C160` latched
`EM_SHADOW_FAULT_UNTRANSLATED` on that route.

This document covers:
- what 0015BF90, 001F9100 and 001F8D30 do;
- the native translation (`src/game/em_shadow_actor_route.{h,c}`);
- how the coordinator binds it;
- how it was verified, and its limits.

Addresses are boot-ELF addresses. RAM and scratchpad values come from the
route captures (`../Extermination/build/s87/route/`, FIRST_LEVEL_ROUTE.md)
and `../Extermination/build/startup-reference/`.

## 1. What the original does

The route draws a small textured quad (a decal) on the surface under the
player. It does not draw a silhouette.

### 0015C160: the route choice (byte-matched, `src/func_0015C160.c`)

If `D_008102B1 != 0`, 0015C160 calls `001CB590(player, 0x320, player[9])`.
Then, unless `D_00810771 == 1`:
- if `player+0x214 == 0`, it calls `001DA6A0(D_00275B44)`;
- otherwise it calls `0015BF90(player)`.

The `+0x4C` draw method follows in both cases. The oracle runs 0015C160
itself over the captured RAM of beats 02 and 04, where `+0x214` =
0x7AA880. The call order is `001CB590(player)`, `0015BF90(player)`,
`+0x4C(player)`, and 001DA6A0 is not called.

### 0015BF90(p) (NEARMISS; read from the .s)

1. If `p+0x1F0 == 0x19`, it returns and draws nothing (0015BFA8).
2. `0x700038A0` = the quadword `p+0xB0` (00102948 at 0015BFB8).
3. It reads the floats at `+0xC4` of the two node records named by the
   words `p+0x154` and `p+0x158`. These are slots 17 and 18 of the node
   table at `p+0x110`. `c.lt.s a, b` keeps `a` when `a < b` and `b`
   otherwise (0015BFD0). The result is stored to `0x70003A20` and to
   `0x700038A4` (the point's y). The start point is therefore (hip x,
   lower foot y, hip z).
4. **Scripted path.** This path is taken when `0x70003B8D != 0` and
   `p+0x1F0 == 0x41` (0015C004 / 0015C014).
   - `0x700038B0` = (0, 1.0, 0, 1.0).
   - `0x700038A4` -= 1.0.
   - It calls `001F9100(p+0xB0, 0x700038A0, 0x700038B0, 4.2)` (0015C074).
     The decal sits one unit below the lower foot, with normal +y. No query
     is made.
5. **Normal path.**
   - `0x700038B0` = `0x700038A0`, then `0x700038B4` -= 100.0.
   - It calls `0019A570(0x700038A0, 0x700038B0, 6, 0)` (0015C0C8). This is
     the segment query with mask 6: cells and grid, no hull lock.
   - If there is no hit (v0 == 0), it returns.
   - Otherwise `0x700038A0` = the quadword at `0x700031B0` (the hit point,
     00102948 at 0015C0E4).
   - `0x700038B0..B8` = the words `+0x24..+0x2C` of the record named by
     `*0x700031D0` (the hit normal), and `0x700038BC` = 1.0.
   - It calls `001F9100(p+0xB0, 0x700038A0, 0x700038B0, 4.2)` (0015C140).
     Its `$a3` is left holding the record pointer, but 001F9100 overwrites
     `$a3` before any use. The readable C's fifth argument is dead.

### 001F9100(owner, point, normal, size) (byte-matched)

It calls 001F8D30 with:
- the facing: a stack copy of `D_0025DAF0`;
- the colour: `D_0025DAE0` itself (`$t0`);
- `half_w = half_h = size`;
- a fade range of 30.0.

In the pinned ELF, and in every capture, `D_0025DAE0` = (8, 8, 8, 255)
and `D_0025DAF0` = (0, 0, 1, 0). Only 001F9100 and its siblings
001F9140 / 001F9180 / 001F91C0 reference them, and nothing writes them.
The native side loads both from the user's ELF.

### 001F8D30(owner, point, normal, facing, colour, half_w, half_h, range) (NEARMISS; read from the .s)

1. **Decal matrix** (stack 0x70):
   - identity (001029C0);
   - turned about z by `atan2(facing.z, facing.x)` (0011E620 with
     `$f12` = facing +8 and `$f13` = facing +0, then 00102A60);
   - times the look-at rows of `normal` (001CD390 into stack 0xB0, then
     `001026D0(M, look, M)`, so each row of M goes through the look-at
     rows);
   - moved by `point` (00102918, xyz only).
2. **Fade.**
   - If `range == 0` (c.eq.s at 001F8DD0), fade = 1.
   - Otherwise fade = 1 - fabsf(owner.y - point.y) / range (0011DF78),
     clamped with `c.le.s` to at most 1 and with `c.lt.s` to at least 0.1.
     The EE has no NaN, and its compare order is total, so these two tests
     are exactly "> 1" and "< 0.1".
3. **Colour.** It is scaled by the fade (00102900: one `vmulx`). Each lane
   is converted by 00128250 (float to unsigned) and packed as
   `r | g << 8 | b << 16 | a << 24`, with no masking.
4. **Corners.** The local corners are (-hw, -hh), (hw, -hh), (-hw, hh) and
   (hw, hh), each with z = 0. Each goes through the decal rows vf20..vf23
   with `vf0.w` as the last weight. The results are written back in place
   (stack 0xF0..0x12F).
5. **Projection.** The first three corners are projected through the
   camera rows at `0x70003AC0` (vf28..vf31):
   - Q = 1 / clip.w (vdiv);
   - x, y, z *= Q (vmulq.xyz);
   - depth = max(min(vf23.z + vf23.w * clip.w, vf23.x), 0);
   - all four lanes go through vftoi4 into `0x70003600`, `0x70003610` and
     `0x70003620`.

   **vf23 is still the decal matrix's row 3.** This routine copied the
   projection block of 001CE300 but never loads the fog vector. So the
   depth lanes (0x7000360C / 1C / 2C) are computed from the decal
   position, not from fog. Nothing reads them before 001CE300 overwrites
   0x70003600..0x7000360F, but 0x7000361C and 0x7000362C survive. The
   translation reproduces this.
6. **Winding.** The cross product is
   `(P1.x - P0.x) * (P2.y - P1.y) - (P1.y - P0.y) * (P2.x - P1.x)`, in
   32-bit wrapping arithmetic (mult / mult1 keep the low word). If it is
   negative (bltz at 001F90A8), nothing is drawn.
7. **Submit.** It calls `001CE300(1, corners, 0x2004290511322469, rgba)`
   (001F90CC). The corners are the four transformed ones. The TEX0 word
   decodes to:
   - TBP0 0x2469, TBW 8, PSMT8, 16x16 texels;
   - TCC 1, TFX modulate;
   - CLUT at CBP 0x2148, CLD 1.

   001CE300 clips two triangles against the view (001CF470). It builds a
   PACKED GIF fan of ST / RGBAQ / XYZF2 in display-list page D_007635C0
   (001CB5F0), then adds the TEX0 A+D (001CB950) and closes the page
   (001CB900).

On the elevator (beats 02 and 04), the query hits a cell record (kind 2)
10.9 units below `+0xB4`. The normal is (0, 1, 0), the fade is about 0.64,
and rgba = 0xA2050505. The quad is 8.4 x 8.4 units, centred under the
player.

### Where the readable C is wrong

- **001F8D30.** The C projects with a "global" vf23 fog vector (`VF23_x/z/w`)
  and divides by w. The instructions use vf23 = the decal matrix's row 3,
  and multiply by the VU reciprocal Q. That is two roundings, not one.
- **0015BF90.** The C passes a fifth argument `p` to 001F9100. It is dead:
  001F9100 overwrites `$a3`.

## 2. The translation (`src/game/em_shadow_actor_route.c/.h`)

- `em_shadow_actor_route_0015BF90(route, player)` takes the raw 0x320-byte
  `EmPlayerLiveActor`. It reads +0xB0..+0xBF, +0x154, +0x158 and +0x1F0,
  and writes no player byte.
- `em_shadow_actor_route_001F9100(route, owner, point, normal, f12)` and
  `em_shadow_actor_route_001F8D30(route, owner, point, normal, facing,
  colour, f12, f13, f14)` take raw words.
- `em_shadow_actor_route_load_tables(elf, size, &tables)` loads
  `D_0025DAE0` / `D_0025DAF0` from the user's ELF. Nothing disc-derived is
  in the source.

Every COP1 and VU0 operation goes through `em_ee_float.h`. The VU0 chains
use `em_vu_vec_bits` in the exact original forms:
- the four-step row multiply-accumulate on all four lanes;
- the Q multiply of x/y/z and the two w-lane depth steps;
- the reciprocal division (lanes w, w), the w-lane min / max and the 12.4
  conversion.

The following are reused, not re-translated:
- `em_owner_services_identity_001029C0`, `_rotate_z_00102A60` and
  `_translate_00102918`;
- `em_sdk_math_original_0011DF78`;
- `em_stream_lanes_00128250`.

Two SDK leaves are translated here, because no module exports them in the
EE float model: 001026D0 (4x4 product) and 00102900 (scale). The
quadword copy 00102948 is a memcpy.

**Workers** (`EmShadowActorRouteWorkers`). Each returns 0, or a negative
value on a fault:

| worker | original | what it must return |
|---|---|---|
| `node_c4(slot, word, &v)` | the RAM read `*(word + 0xC4)` | the float bits at +0xC4 of the node the player word at `slot` (0x154 / 0x158) names |
| `segment(from, to, 6, 0, &result, point, normal)` | 0019A570 | its v0. On a hit, also the four words at 0x700031B0 and the record normal (+0x24..+0x2C of `*0x700031D0`) |
| `atan2(y, x, &r)` | 0011E620 | atan2f, raw bits |
| `look_at(out, v)` | 001CD390 | the 4x4 look-at rows |
| `submit(tag, corners, tex0, rgba)` | 001CE300 | draws the quad |

**Scratch** (`EmShadowActorRouteScratch`) holds pointers into the one
scratchpad image:
- 0x700038A0 and 0x700038B0 (4 words each);
- 0x70003A20;
- 0x70003600 (12 words: P0..P2);
- 0x70003B8D (read);
- 0x70003AC0 (16 words, read).

**Fail-stop.** Each entry point checks every worker, table and scratch
pointer it can reach, and the record. If anything is missing, it latches
`EM_SHADOW_ACTOR_ROUTE_FAULT_UNBOUND` at its own address and returns -1
before any write. When a worker returns a negative value
(`..._FAULT_WORKER`, at the worker's original address, or 0015BFC8 /
0015BFCC for the node reads), or the float model refuses a form
(`..._FAULT_FLOAT`), it stops at once. The writes made before that point
stay, as the original order leaves them. A latched fault refuses every
later call.

## 3. Verification

`make test-shadow-actor-route-reference` (the target is in section 4; run
directly with `python3 tools/test_shadow_actor_route_reference.py`).

The oracle executes the original 0015BF90, 001F9100 and 001F8D30 from the
user's ELF. Their SDK leaves also run as original:
- 00102948, 001029C0;
- 00102A60 with 001029E8 / 00102A90;
- 001026D0, 00102918, 0011DF78, 00102900;
- 00128250 with 001278C0.

COP1 and VU0 run through `tools/ee_float_model.py` (the test's own `RouteEE`
subclass; no shared file is edited). The four workers are hooked. 0019A570
(route cases), 0011E620 and 001CD390 run nested as original code, and
their outputs go to the native workers. 001CE300 is recorded.

Every case compares:
- the return value;
- the worker calls in order, with every argument (segment from/to/mask/id,
  atan2 y/x, the look-at vector, and all four corners, TEX0 and rgba of
  the submit);
- the scratchpad words 0x700038A0..BF, 0x70003A20 and 0x70003600..2F.

It also asserts that the original wrote no other RAM or scratchpad byte
(stack excepted). The test checks that the callee set of the three
routines is exactly workers + leaves + themselves.

- **Default run (about 2 s; about 36 s with `EM_TEST_FULL=1`, measured 2026-09-23):**
  - 600 0015BF90 unit cases with synthetic players (the +0x1F0 / 3B8D
    gates, equal / signed-zero / exponent-255 foot heights, scripted hits
    of every kind, several normals, some scripted yaw values);
  - 500 direct 001F8D30 / 001F9100 cases (ranges 0, -0, negative, a
    denormal; heights around the clamp edges; colours outside 0..255);
  - all 9 conditional branches of the three routines, taken both ways
    (asserted);
  - all 15 route beats plus the opening capture, over the captured RAM and
    scratchpad, in three variants: as captured (0019A570 runs as original
    over the captured collision world: 15 hits), the 0x41 path and the
    0x19 exit;
  - 0015C160's dispatch in beats 02 and 04;
  - 12 native fault cases (each worker missing, each worker failing at its
    position, a failing node read, missing tables and scratch, the latched
    fault);
  - the binding checks below.
- **`EM_TEST_FULL=1`:** 20,000 cases of each unit kind.

**Binding checks.** Every 001CD390 call the original computed is replayed
through `em_effect_original_001CD390`: the matrix and all 16 words it leaves
at 0x70003600. Every 0011E620 call is replayed through
`em_sdk_math_original_0011E620` (tables from the ELF, D_0026C5D0 = 1). All
calls match. The coordinator can therefore bind these two workers to those
translations.

Mutation check (manual, this session): each of these makes the test fail:
- the vf23 depth lane changed;
- the atan2 argument order swapped;
- the winding sign test changed;
- the min changed to `<=`;
- the 001026D0 w weight changed.

The `c.le.s` versus "> 1" rewrite survives. It is equivalent on the EE
(total compare order), as section 1 says.

## 4. Binding (for the scene coordinator)

Nothing is wired. The module is built only by its test.

**Makefile (lead-owned).** The test target:

```make
.PHONY: test-shadow-actor-route-reference
test-shadow-actor-route-reference:
	python3 tools/test_shadow_actor_route_reference.py
```

When the route is bound, add `src/game/em_shadow_actor_route.c` to the game
sources. It calls into `em_owner_services_original.c`,
`em_sdk_math_original.c` and `em_stream_lanes_original.c`. On 2026-09-23 the
game sources list only `em_sdk_math_original.c`; add the other two unless
another binding has added them already.

**Route.** `em_shadow_original_route_0015C160` must stop latching
`EM_SHADOW_FAULT_UNTRANSLATED` for `player_214 != 0`. Requested change
(em_shadow_original.c, lead-owned): return a distinct value, e.g. 2, for
that route. Then, in the coordinator's `w_0015C160` (SHADOW_ORIGINAL.md
"Binding"):

```c
    if (r == 2 &&
        em_shadow_actor_route_0015BF90(&b->actor_route, b->player_live) < 0)
        return -1;                    /* 0015BF90, after 001CB590 */
```

`player_live` is the same `EmPlayerLiveActor` the stage uses (the record
at 0x8102B0).

**Tables.** Call `em_shadow_actor_route_load_tables(elf, size,
&b->actor_route_tables)` once, where the other ELF tables are loaded.

**Scratch.** Point every field at the coordinator's one scratchpad image:
- `s38A0` / `s3A20`: the same words as `EmPlayerLandScratch.s38A0` /
  `.s3A20` and `EmPlayerStageGlobals.spad3A20`;
- `s3600`: `EmEffectOriginalGlobals.spad3600` (001CD390 writes the same
  block just before);
- `s3B8D`: `EmSceneState.spad3B8D`;
- `s3AC0`: the camera rows `EmShadowOriginalScene.camera_3AC0` of the frame.

**Workers:**
- `node_c4`: the `+0xC4` float of the node record
  `*(player + 0x110 + 4 * 17)` / `... 18` (the same node array
  `player_nodes` the 001DA6A0 binding uses). Check that `word` is that slot's
  pointer, and fault otherwise.
- `segment`: `em_coll_segment_0019A570(&seg, from, to, 6, 0)` with the
  frame's `EmCollSegment`. On a result other than 0, fill `point[0..2]`
  and `normal` from `em_coll_segment_hit`. Fill `point[3]` from the
  scratchpad word 0x700031BC. See limit L2.
- `atan2`: `em_sdk_math_original_0011E620(tables, world, workers, y, x,
  &r, &fault)`. This is verified above.
- `look_at`: `em_effect_original_001CD390(&effect, out, v)` on the effect
  module's context, so its spad3600 is the shared block. This is verified
  above.
- `submit`: **001CE300 is not translated** (limit L1). Until it is, leave
  it unbound: the route then faults at 0x001CE300 (UNBOUND at 0x0015BF90,
  before any write) instead of at 0x0015BF90 UNTRANSLATED.

**Order in the frame.** The call replaces 001DA6A0 at the same place:
after 001CB590 and before the `+0x4C` draw.

## 5. Limits and open items

- **L1: 001CE300 is not translated.** It covers the frustum clip 001CF470,
  the GIF fan and the display-list page calls 001CB5F0 / 001CB950 /
  001CB900. The decal is not drawn until that lane exists. The texture at
  TBP0 0x2469 (PSMT8, CLUT 0x2148) must be resident, which is also part of
  that work. What this lane hands 001CE300 is verified exactly: tag,
  corners, TEX0 and rgba.
- **L2: the fourth word of 0x700031B0.** 0015BF90 copies the whole
  quadword at 0x700031B0 into 0x700038A0. The segment walkers write only
  x/y/z (em_coll_segment_walkers.c copies three words), and
  `EmCollProbeState` does not keep the fourth word. It is 0 in every
  capture, and nothing in these routines reads it (00102918 adds xyz only,
  and 001F8D30 reads point.y). It only reaches 0x700038AC. The binder
  must supply it from the scratchpad image. If that image is not kept,
  export the word from `EmCollProbeState` (a coll-segment-walkers change
  for the lead).
- **L3: route coverage.** Only the end snapshots of beats 02 and 04 have
  `+0x214` set. Beats 05 and 08 end with the player off the crate and the
  truck. The oracle still runs 0015BF90 over their captured players,
  because 0015BF90 does not read +0x214, but a mid-beat frame on the crate
  or the truck is not captured. The 0x41 path is forced (+0x1F0 and 3B8D
  patched) and appears in no capture.
- **L4: VU0 register state.** The native side does not model the VU0
  registers that 001F8D30 leaves behind (vf1, vf2, vf20..vf23, vf28..vf31,
  ACC, Q). 001CE300, the only later consumer on this path, reloads vf23 and
  vf28..vf31 before use.
