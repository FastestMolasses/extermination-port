# Segment and camera queries (0019A570, 0019A910) and their walkers

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

`src/game/em_coll_segment_walkers.c` translates the two segment queries of
the collision library and everything they call that no other module
translates:

| Original | What it does |
|---|---|
| 0019A570(from, to, mask, id) | The segment query. Mask bit 0 runs 001A6440 (a hull lock, a worker), bit 1 runs 001A0B10 (cells), bit 2 runs 0019D330 (grid). The callers: the climb depth test 00177F40 (mask 6), the drum 00156620 (mask 4), the ledge catch 0017D080 (mask 6), the shadow 0015BF90 (mask 6), the enemies and weapons. |
| 0019A910(from, to, mask) | The camera query. Bit 0 runs 001A6AD0(0x40) (a worker), bit 1 runs 001A1390, bit 2 runs 0019D770. The callers: the camera 0018D330 / 0018DD20 / 0018F870 / 00198240 / 00197490 (masks 6 and 7; 0018D7B0 passes 7 only for camera mode 2) and the pickup / interaction scan 00183EF0 (masks 4 and 6). |
| 001A0B10 / 001A1390 | The cell walkers. Pass 1 covers the static cells of *0x70003250; pass 2 the published class-4 owners (D_00275B7C). |
| 0019D330 / 0019D770 | The grid walkers: a span of one rank table picked over all six directions, each node's six rank bounds, an attribute gate, then 0019ED80. |
| 001A50A0(prim) | The segment against one face of a 0x2000 box prim. |
| 001A5C30(prim) | The segment against a 0x4000 round prim, a vertical cylinder. |

001A50A0 and 001A5C30 are also the two workers the probe lane left open in
the surface walker 001A2AE0's pass 2 (docs/COLL_PROBES.md):
`em_coll_segment_face_worker` and `em_coll_segment_round_worker` fill
`EmCollProbeWorkers`.

Reused, not re-translated: 001A4030, 0019F1A0 and 0019ED80
(`em_coll_probe_original.c`), 0011DF78 fabsf and 0011E748 sqrtf
(`em_sdk_math_original.c`). Every routine here is a NEARMISS file in the
decomp; the translation follows the .s and cites the address of each branch
and store. All EE COP1 and VU0 macro arithmetic goes through `em_ee_float.h`.

## 1. What the originals do

**0019A570 and 0019A910 (the queries).**
- Copy `from` to 0x70003190 and `to` to 0x700031A0 and to a stack copy; set
  both w lanes (0x7000319C, 0x700031AC) to 1.0; clear 0x700031D4.
- Mask bit 0: 0019A570 calls 001A6440(id & 0xFFFF); 0019A910 calls
  001A6AD0(0x40). Neither lock reads a second argument. A nonzero return
  copies 0x700031B0 to 0x700031A0, result 1.
- 0019A570 only: 0x7000324E = -1 (after the lock, before the walkers). So
  its walkers always see query class -1: kinds and attributes 0x51, 0x52
  and 0x53 never pass through 0019A570.
- Bit 1: 0x70003254 = 0 (no self), then the cell walker; a hit gives 2.
- Bit 2: the grid walker; a hit gives 4 (grid wins).
- On a hit the stack copy of `to` goes back to 0x700031A0; with no hit
  0x700031D0 = 0. The result is stored at 0x700031D8 and returned.

**The cell walkers 001A0B10 (segment) and 001A1390 (camera).**
- 0x700031D0 = D_700030B0 (the cell record). The query box is the segment's
  bounding box on all three axes.
- **Pass 1** stops at the first directory word without bit 31. Then:
  - 001A0B10 skips 0x40000000 words. It stages the static kind
    D_0024D7C0[area][sub][i].+8 in 0x70003B88 and gates it: below 0x50
    passes; 0x50 and 0x5A or more are skipped; 0x51 needs 0x7000324E == 0,
    0x52 needs 2, 0x53 is skipped when it is -1; 0x54..0x59 pass. The hull is
    the word's low 30 bits (0x20000000 words go through the EE's uncached
    RAM mirror, the same bytes).
  - 001A1390 skips words with 0x40000000 or 0x20000000 and kinds of 0x51
    or more. It does not touch 0x70003B88.
- **Pass 2** walks the published class-4 list and skips status 0, class not
  4, the self owner (0x70003254), uid 0xFF, word 0 and uid >= count. After
  the box test the owner kind (+0x54) must be below 0x50 (001A0B10) or 0x51
  (001A1390).
- **Box test** in the order x, z, y. **Prims:** 0x1000 -> 001A4030, 0x2000
  -> 001A50A0, 0x4000 -> 001A5C30, 0x8000 advance without a call, any other
  type neither calls nor advances.
- **Pass 1 hit** (the first prim that hits ends the hull): 0x700031A0 =
  0x700031B0 (all three lanes), 0x700031D4 = 0, result 1, the kind in the
  low byte of 0x700030CA, then the box bound on the end's side moves.
- **Pass 2 hit** is handled inside the prim loop, which runs on: the same
  stores with 0x700031D4 = the owner and its kind byte. The walker's hit
  register is left at 3 by the copy loop, so a following 0x8000 prim (no
  call) repeats the same, idempotent, stores. The box moves once after the
  hull.
- Returns 1 when either pass hit, else 0.

**The grid walkers 0019D330 (segment) and 0019D770 (camera).**
- Direction masks from the segment's sign on all three axes; 0019F1A0 ranks
  the start and end twice with swapped masks (the first pair's ranks are
  kept for the span helpers).
- **Span pick** over all six directions (0019DF10 skips the y pair; these
  do not). With no direction under the node count the span registers are
  uninitialized: the native code faults.
- **Walk.** Each node passes its six rank bounds (+0x0C..+0x16), then its
  attribute (+0x1A, staged in 0x70003B88):
  - 0019D330 applies 001A0B10's static kind gate to it;
  - 0019D770 skips only 0x51..0x53 (everything from 0x54 up passes, the
    surface attributes included).
- Each accepted 0019ED80 moves the end to the hit. On a hit the last
  accepted point and node are published (0x700031B0, 0x700031D0); result 1.

**001A50A0 (box face).** Face code prim +2; origin +4..+0xC; extents
+0x10..+0x18 of either sign.
- Per axis: delta = end - start (0x70003620); a negative delta rejects the
  "+" codes 2/4/6 of that axis, a non-negative one rejects 1/3/5; then min
  and max (0x70003600 / 0x70003610, origin + extent on the extent's side)
  and origin - start (0x70003630).
- The jump table D_0026DA80 sends code 0 to the miss return; codes of 7 or
  more miss.
- The tested axis needs rel * (min - end) < 0. t = rel / delta
  (0x70003680); the two other coordinates (0x70003684/88) must lie strictly
  inside their min/max.
- Hit: the point takes the box ORIGIN on the tested axis (not the min).
  X and Z faces give class 0x2000, Y faces 0x4000 (code 3) or 0x8000
  (code 4); the normal is +-1 on the tested axis (+ for odd codes).

**001A5C30 (round prim).** Centre +4..+0xC, radius +0x10, half height +0x14.
- The closest approach of the segment's line to the centre in x/z (one VU0
  multiply of {dx, dz, rx, rz} by {dx, dz, dx, dz}); beyond the radius it
  misses. Otherwise the two chord points (0011E748 twice).
- **Vertical** when fabsf(dx) and fabsf(dz) compare below the double 1e-5
  (0011DF78, 00128350, 001000C0); it then needs fabsf(dy) above 1e-5
  (00100110) and tests the cap the segment crosses: point (start.x, cap y,
  start.z), class 0x8000 / normal y -1 rising, 0x4000 / +1 falling.
- Otherwise the line is parameterized by z (dz > dx) or x. A chord point
  whose height lies strictly inside the slab and whose parameter lies
  strictly between start and end is a side hit: class 0x2000, normal
  ((x - cx) / r, 0, (z - cz) / r). Else the cap between the two chord
  heights, interpolated along the chord. Note this cap test uses the whole
  line, not the segment.
- The 1e-5 compare is exact: the double lies between the floats 0x3727C5AC
  and 0x3727C5AD, so "below" is |x| <= 0x3727C5AC and "above" is
  |x| >= 0x3727C5AD. A NaN pattern is not below and is above (001274B0
  returns 1 for NaN), which on these inputs agrees with the magnitude rule.

## 2. Corrections to the readable C (NEARMISS files)

- **001A1390.** The C continues where the .s leaves pass 1 at the first word
  without bit 31; it sends 0x8000 and unknown prims to a default that
  advances (the .s advances 0x8000 only); on a pass-1 hit it sets the box's
  y from 0x700031B4 instead of copying 0x700031B0 to 0x700031A0 and
  re-clamping.
- **001A5C30.** The header comment calls 001028E8 a 2D normalize; it is a
  4-lane VU0 multiply.
- **0019A570 / 0019A910.** The C passes a second argument to 001A6440 /
  001A6AD0; the .s passes one (id & 0xFFFF, or 0x40), and neither lock reads
  a1..a3.
- 001A0B10, 001A50A0, 0019D330, 0019D770: the C agrees with the .s (names
  aside: its lo/hi box names are swapped).

## 3. Binding (coordinator)

**Live since census L06b (2026-09-24):** the collision world
(`src/game/em_collision_world.{h,c}`) holds the one `EmCollProbeState`, the
`EmCollSegmentFaceScratch`, the SDK context (the user's export; D_0026C5D0 from
its window) and the `EmCollSegment` below, with no lock workers. Bound
consumers of 0019A910 (mask 6): the 0018D330 prepass and 0018D910's AREA11
bounds (the live camera's segment workers in em_camera_live.c since census
L13..L16, docs/CAMERA_LIVE.md; the ground branch is 0019B7D0,
docs/COLL_LIST_PASSES.md) and 00183EF0's
item ray (the interaction host's `pickup_ray`, the `EmInteractionRaycast`
slot: `hit` = result != 0, `flags` = the record's +0x1A halfword, `kind` =
the result, `owner` = the hit owner's record, which the item's identity
matches when the ray ends in its own published cell). The installed EMCL
carries the rank section (flags 7). Not bound yet: 0019A570 and its walkers
(the climb, the ledge catch, the drum and the shadow are not bound), the
port's follow camera (L13; it still queries em_collision.c) and the lock
workers 001A6440 / 001A6AD0 (untranslated). Evidence for the live binding:
`tools/test_camera_interaction_fixture.py` runs the retarget over the
captured scene's own cell directory and published class-4 list and matches
the panel and refusal captures (the refusal's overhead point is now exact),
and the level smoke's tick log is unchanged.

**State.** One `EmCollProbeState` per scene (zeroed at area load) is the
scratchpad: pass the SAME instance to the floor probes (docs/COLL_PROBES.md)
and to every query here. Add one `EmCollSegmentFaceScratch` (0x70003600..,
zeroed). Then:

```
EmSdkMathContext math;            /* em_sdk_math_original: tables from the ELF,
                                     world.d26C5D0 -> the library mode word (1) */
EmCollSegmentWorkers locks = { ctx, lock_6440, lock_6AD0 };   /* or NULL */
EmCollSegment seg = { &probe_world, &math, &locks, &state, &face };
```

`probe_world` is the `EmCollProbeWorld` of docs/COLL_PROBES.md section 4
(cells: the directory, class lists and static kind view; grid: the rank
view of an EMCL with flags 7, installed since census L07).

**Slots.**
- `EmCollProbeWorkers` (the floor probes' surface walker, pass 2):
  `{ &seg, em_coll_segment_face_worker, em_coll_segment_round_worker }` as
  `EmCollProbePlayer.workers`. The probe passes its own state; it must be
  `seg.state`.
- `EmPlayerClimbWorkers.segment` (00177F40, mask 6): a shim on the climb
  context calling `em_coll_segment_query(&seg, from, to, mask, id)`.
- `EmPlayerRecoveryWorkers.segment` (0017D080, mask 6):
  `em_coll_segment_query_result(&seg, ...)`.
- `EmDrumOriginalHooks.segment` (00156620, mask 4):
  `em_coll_segment_query_i32(&seg, ...)`.
- `EmInteractionRaycast` (00183EF0, the 0019A910 family, mode 6 or 4):
  `em_coll_segment_0019A910(&seg, from, to, mode)`, then
  `em_coll_segment_hit`: `hit` = result != 0, `flags` = `record_node` (the
  record +0x1A halfword the scan tests with 0x2000 / 0x2800), `kind` = the
  result (0x700031D8), `owner` = `entity`.
- The live camera (em_camera_live.c `fw_segment` / `lw_segment`, 0019A910
  mask 6; em_camera_probe.h is retired): the result and
  `em_coll_segment_hit` give the point, the record +0x1A halfword and +0x24
  normal the camera reads.
- The shadow's 0015BF90 route (mode 6, then the point and record +0x24) is
  still untranslated in em_shadow_original; `em_coll_segment_hit` supplies
  what it reads.

**Workers still open.** 001A6440 and 001A6AD0 (the hull locks, mask bit 0):
with `workers` NULL a query with bit 0 faults. The camera's mode-2 queries
(mask 7) and any mask-7 0019A570 need them. Everything with masks 2, 4 and 6
is complete.

**Shared scratch.** 00183EF0 stages its ray endpoints at 0x70003600 /
0x70003610 before calling 0019A910, which 001A50A0 overwrites (x/y/z lanes).
The query copies them first, and 00183EF0 does not read them afterwards, so
a native caller may keep its endpoints elsewhere.

**Faults** (-1; the queries leave `state` and `face` unchanged): a missing
lock worker when bit 0 is set; a static cell without its kind view; a pass-2
directory word with bit 31; a prim or hull outside the image; a grid index
outside its table; the grid walkers' uninitialized span; no `math` when a
round prim is reached (0011E748).

## 4. Verification

`tools/test_coll_segment_walkers_reference.py`
(`make test-coll-segment-walkers-reference`).

**The oracle.** The probe lane's EE (`test_coll_probe_reference.ProbeEE`:
every COP1 and VU0 macro instruction through `tools/ee_float_model.py`)
executes the original instructions from captured AREA11 RAM: the queries,
walkers and prim tests above, 001A4030, 0019F1A0, 0019ED80, the SDK vector
routines, 0011E748 with its kernel, 0011DF78, the soft-float conversion and
compares (00128350, 001278C0, 00127728, 00126AB8, 001000C0, 00100110,
001274B0, 00126BE8, 00127398) and, for the binding check, 0019B6C0 with
001A2AE0 and 0019DF10. Every code range and the jump table D_0026DA80 are
checked against the pinned ELF, and D_0026C5D0 must be 1. 001A6440 and
001A6AD0 are hooked on both sides: a scripted result is recorded and
compared call by call. No shared file is edited.

**What each case compares.** The return value and the whole scratchpad
state: 0x70003190..0x700031D8, the cell record +0x1A/+0x24, 0x70003680,
0x7000324E, 0x70003254, the ranks 0x70003240..0x7000324A, 0x70003B86/88 and
001A50A0's 0x70003600..0x70003638 and 0x70003684/88. Any other RAM or
scratchpad write by the original fails the case.

**Cases (quick run 6-9 s on 8 workers).**
- **Route.** 53 of 12,439 rows across the 15 beats (one per player state
  per beat), seven queries each: the shadow's form (0019A570 from the hip,
  100 down, mask 6), the camera's line of sight (target to eye) and ceiling
  and floor forms (eye +-200) with 0019A910 mask 6, both queries with mask 7
  and the scripted lock (hit on odd rows), and a short vertical 0019A570 at
  the feet. The hit view (`em_coll_segment_hit`) is compared with the bytes a
  caller reads through the original's record pointer.
- **Captures.** The last collision query of each snapshot frame is the
  camera's ceiling test (0018DD20: 0019A910(D_700038F0, 200 up, 6)). Re-run
  from the captured segment, the original and the native reproduce the
  captured start, end, point, record, owner, result, ranks, cell record,
  0x70003680, 0x7000324E/54 and 001A50A0's words on all 15 beats (11 hits).
  On the 10 hits whose record carries 0x8800, 0x70003A3C holds the hit y - 1,
  as 0018DD20 stores it. 0x70003B86/88 are not compared: code after the
  query reuses them (the captures hold 0 or 1 there).
- **Units.** 180 each of 001A50A0 (the directory's own faces and synthetic
  boxes, codes 0..8, both extent signs), 001A5C30 (vertical and near-vertical
  segments with bit-exact threshold steps, side and cap crossings) and the
  four walkers on staged segments under random persistent state.
- **Synthetic worlds.** 48 cases on RAM patched on both sides: static cells
  with 0x40000000 / 0x20000000, static and owner kinds around every gate,
  box / round / filler / unknown prims written into hulls, grid attributes
  around both grid gates, the self skip. Each runs the four walkers, both
  queries, and the probe lane's 0019B6C0 with this module bound as its
  pass-2 workers (owners of kind 0x5A+ reach 001A50A0 / 001A5C30; the
  counted worker calls must be nonzero).
- **Gate boundaries** (deterministic): static kinds 0x4F..0x5A x query class
  -1/0/1/2 x flags 0x80000000/0xA0000000/0xC0000000 on both pass 1s; owner
  kinds 0x4F..0x52 on both pass 2s; grid attributes 0x4F..0x5A x the four
  query classes on both grid walkers; the pass-2 repeat after a hit; the
  1e-5 thresholds of 001A5C30 on dx, dz and dy (the floats either side of
  the double, both signs; each positive pair must change the outcome); the
  fail-stops (missing lock worker for each query, missing kind view for each
  query, missing math). Also: 0x8000 prims carrying bytes a round-prim test
  would hit, before and after a hit, in both passes; a bit-31 word after the
  stop word; the box re-clamp across two hulls moved out of the level (pass 1
  and pass 2); a segment starting or ending exactly on a box face's plane.

**Mutants.** Each of these 23 mutants of the native file fails the quick
run: either 1e-5 threshold moved by one ulp; the 0x53 gate dropped; the
0x51 gate inverted; the camera's static gate at 0x50; the camera's owner
gate at 0x50; the camera's grid gate at 0x55; the grid span pick skipping
the y pair; the grid gate without its y bounds; 0019A570 not setting
0x7000324E; 0019A910 setting it; 001A0B10 ignoring 0x40000000; 001A1390
admitting 0x20000000 words; the face hit taking the box min; the face test
admitting c = 0; the cap parameter from y2; the side normal's z from x; no
re-clamp in pass 1; no re-clamp in pass 2; pass 1 continuing past the stop
word; 0x8000 prims tested as round prims; the VU0 multiply done on the host;
pass 2 stopping at its first hit. By inspection one candidate is equivalent,
not a test gap, and was not run: resetting the pass-2 hit register after a
hit (the repeated stores write the same values).

**Full sweep.** `EM_TEST_FULL=1` runs every route row, 3,000 x 3 unit cases
and 600 synthetic cases. It passed on 2026-09-23 (1,095 s on a machine at
load 30-50). All 12,439 route rows were identical on every query:

| Query | None | Lock | Cells | Grid |
|---|---|---|---|---|
| 0019A570 (3 per row) | 3,994 | 2,628 | 2,465 | 28,230 |
| 0019A910 (4 per row) | 20,388 | 2,448 | 372 | 26,548 |

The owners hit on the route were the crates 0x7A7980 and 0x7A7C70 and the
elevator 0x7AA880; 12,497 hit views matched. The synthetic sweep made 449
001A50A0 and 91 001A5C30 calls through the probe lane's bound workers.

`EM_TEST_UBSAN=1` builds the bridge with `-fsanitize=undefined
-fno-sanitize-recover=all`; the quick run passes with it.

## 5. Limits

- **Hull locks 001A6440 / 001A6AD0** are workers (section 3). The route
  cases script them identically on both sides; their own behaviour is not
  verified here.
- **Not in AREA11, so reached only through synthetic worlds:** static cells
  (the AREA11 directory's word 0 has no bit 31), round prims, and owner
  kinds of 0x50 and above.
- **0x7000324E across callers.** 0019A570 always leaves -1; 0019A910 leaves
  whatever the last writer stored. The native state carries it, but only
  native writers update it.
- **The pass-2 repeat** after a hit (the hit register left at 3) stores the
  same values again; it cannot change a result, and it is kept as the
  original does it.
- **Grid data** is the EMCL rank section (flags 7), installed since census L07
  (docs/COLL_PROBES.md section 3).
