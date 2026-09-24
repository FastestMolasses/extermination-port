# Horizontal move and sweep walkers (lane "collision-move-walkers")

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Module: `src/game/em_coll_move_original.{h,c}`. Oracle:
`tools/test_coll_move_reference.py`. Nothing here is wired into the live game
yet: census L05 (2026-09-24) found it **blocked** on two untranslated workers,
0019CB60 and 001A6440 (section 4 item 2). The census rows of both were
corrected from verified-unbound to missing: the oracle runs them as original
instructions on both sides (a worker hook), which verifies the walkers around
them but is not a translation. Section 4 says how the coordinator binds it.

## 1. What the original does

The walkers share one scratchpad block. `EmCollMoveScratch` mirrors it field
for field:

| Scratchpad | Field | Meaning |
|---|---|---|
| `0x70003190..9C` | `start` | segment start (lane 3 zeroed by the walkers) |
| `0x700031A0..AC` | `end` | segment end, clamped by every hit |
| `0x700031B0..BC` | `point` | the last hit point |
| `0x700031C0..CC` | `delta` | point − goal, written when a pass hit |
| `0x700031D0` | `record` | 0, `D_700030B0` (a cell prim hit), or a grid node |
| `0x700031D4` | `entity` | the owner hit (or the locked entity) |
| `0x700031D8` | `mode` | the return value |
| `0x700030CA` | `cell_class` | `D_700030B0` +0x1A: class (high byte) and kind (low byte) |
| `0x700030D4..DC` | `cell_normal` | `D_700030B0` +0x24 |
| `0x7000324E` | `query_class` | the query actor's `+2 & 0x1F` |
| `0x70003254` | `self` | the query actor's `+0x14` |
| `0x70003B88` | `kind` | the kind byte being gated |
| `0x70003680..8C` | `work` | 001A4D10 intermediates; 001A4030 stores its ratio at +0 |

**`0019AD00(actor, target, flags)`: the move probe.** The `.s` is the
authority (the readable C is NEARMISS and calls it a "camera track update",
which it is not).

1. The segment runs from (actor +0xB0, target.y, actor +0xB8) to the target.
   The goal is copied to a stack snapshot, and `0x700031AC`, `0x7000319C`
   and `0x700031D4` are cleared (0x0019AD60..0x0019ADB0).
2. The end is pushed 1% of a unit further (0x0019ADAC..0x0019ADE8):
   `dir = 00103230(00102760(end − start), 0.01)`, `end += dir`. Every one
   of these is a VU0 macro routine: a four-lane difference; a normalise
   (the x/y/z squares summed, a VU square root, the reciprocal 1/length
   from the VU divider, the xyz lanes scaled by it); a scale of the xyz
   lanes by the x lane of the factor; and a four-lane sum.
3. **Flag bit 0, when actor +0 bit 0 is set** (0x0019ADF0..0x0019AEC4):
   - a class-0 actor (+2 & 0x1F == 0) calls `001A6440(0x40)`. A nonzero
     result is vetoed when the entity it left in `0x700031D4` has +0x52
     bit 1;
   - any other class calls `001A7280()`. A nonzero result is vetoed unless
     the actor's own +0x52 has bit 1;
   - a surviving result copies `point` to `end` and sets mode 1, unless the
     actor's +0x52 has bit 0.
   (The C's `f1 ? 0x40 : 0` is always 0x40 on both paths.)
4. `0x7000324E` = actor +2 & 0x1F (always).
5. **Flag bit 1**: `0x70003254` = actor +0x14, then `0019FE50`; 0 means a
   hit, and the mode becomes 2.
6. **Flag bit 2**: `0019CB60` (the grid pass); 0 means a hit, and the mode
   becomes 4. A later pass overrides an earlier mode.
7. `end −= dir` (0x0019AF2C).
8. With a mode: `end` = the goal snapshot, `delta = point − end`. **Flag
   bit 31 then adds `delta.x` / `delta.z` to actor +0xB0 / +0xB8**, the
   actor's current position, not the goal (0x0019AF84..0x0019AFAC). With no
   mode, `record` = 0. `mode` is stored and returned.

**`0019AFE0(actor, from, to, flags)`: the sweep.** It has the same body
(the two `.s` differ only at the entry and one call). The segment runs from
(from.x, to.y, from.z) to `to`, and step 7 is `start += dir` (the 1% step is
added to the START; the end is left pushed out).

**`0019FE50()`: the horizontal cell walker.** It first stores
`record = D_700030B0` (0x0019FE94), even when nothing is hit. It keeps the
x and z extents of the segment (min, max). A hit moves only the extent on
the end side to the clamped end (0x001A0240, 0x001A05E8). The readable C
names the two extents the other way round (its `lox` holds the maximum).

- **Pass 1, the static cells** (directory word bit 31; stop at the first
  word without it; skip bit 30):
  - `kind` = byte +8 of record i (stride 0x28) of
    `D_0024D7C0[D_00810700][D_00810701]`;
  - the gate: kind ≥ 0x5A skips; 0x51 needs query class 0; 0x52 needs query
    class 2; 0x53 skips query class −1;
  - the hull AABB: x and z extents against the hull's, and the start y
    inside its y range;
  - the prims, **stopping at the first hit**. The hit sets end.x/z = point,
    `entity` = 0 and `cell_class` = (class & 0xFF00) | kind.
- **Pass 2, the published class-4 owners** (`D_00275B7C` / `D_00275B84`).
  An owner is skipped when:
  - its +0 is 0;
  - its class is not 4;
  - it is `0x70003254`;
  - its uid (+0xE >> 8) is 0xFF;
  - its word is 0. The word is read **before** the uid < count check;
  - the kind gate above fails, with `kind` = its +0x54 byte;
  - its uid is ≥ count.

  The hull AABB is then tested and **every** prim is walked. Each hit sets
  end.x/z, `entity` = the owner and `cell_class`. The hit flag is the last
  prim call's return, not reset per prim, so an unknown prim type (no
  call, no advance) re-applies a previous hit.
- The return is 1 when nothing was hit and 0 otherwise.

**`001A4830`: the 0x8000 / 0x4000 prims** (asm-word in the decomp; from the
`.s`).

- The end height must lie in [cy − half, cy + half]. Half is the radius
  for 0x8000 and +0x14 for 0x4000.
- The x/z line's closest point is computed with `001028E8` (a four-lane VU0 product),
  then EE add, neg, div and mul.
- It misses when d² > r², and when the start is strictly inside the circle.
- The half chord and the segment length come from two SDK `0011E748` sqrt
  calls.
- Candidate 1 = closest − u·hc, then candidate 2 = closest + u·hc
  (through the EE float accumulator: an accumulate step, then a multiply-add).
  A candidate is accepted when its x lies strictly between the
  segment's x ends in the direction of travel, **or** its z does.
- A hit writes `point` = (x, end.y, z), `cell_normal` = ((x − cx)/r, 0,
  (z − cz)/r) and `cell_class` = 0x2000. There is no facing test.

**`001A4D10`: the 0x2000 face prims** (asm-word; from the `.s`).

- Faces 3/4 (y) never hit.
- The end height must lie in the face's y extent.
- A walk toward +x skips face 1, toward −x face 2; toward +z face 5, toward
  −z face 6.
- **x faces** (byte < 3): x0 must lie strictly inside the segment's x
  extent. hz = sz + (ez − sz)(x0 − sx)/(ex − sx), strictly inside the
  face's z extent. The normal x is +1 for face 1, else −1.
- **z faces** (byte ≥ 5): the same with the axes swapped. The normal z is
  +1 for face 5, else −1.
- `work` receives ex − sx, ez − sz, the plane offset and the crossing,
  also on a rejected crossing.

**`001A4030`: the 0x1000 n-gon prims** (byte-matched C). The facing test is
dir·n ≤ −1e-5. The per-axis interval test and the edge test ≤ 1e-5 follow.
A hit writes `point`, the ratio ny²/(nx²+nz²) to `work[0]` (0x70003680),
the class (0x2000 / 0x1000 / 0x4000, or 0x2000 / 0x0800 / 0x8000 for ny < 0)
and the prim normal.

## 2. Translation

| Routine | Native |
|---|---|
| 0019AD00 | `em_coll_move_0019AD00` |
| 0019AFE0 | `em_coll_move_sweep_0019AFE0` |
| 0019FE50 | `em_coll_move_walk_0019FE50` |
| 001A4830 / 001A4D10 / 001A4030 | `em_coll_move_prim_001A4830` / `_001A4D10` / `_001A4030` |
| 001028D0, 001028B8, 001028E8, 00102738, 00103230, 00102760 | static VU0 helpers in the module |

- **Floats.** Every EE COP1 op and VU0 macro op goes through
  `src/game/em_ee_float.h` (docs/EE_FLOAT_MODEL.md), with the exact forms
  listed above. A refused form is a fault. EE compares use `em_ee_c_lt` /
  `em_ee_c_le` (DAZ and saturation included).
- **001A4030** is translated again here rather than reused from
  `em_actor_collision.c`, whose copy (`prim_ngon`) still uses the older
  truncating model: no add/sub pre-trim and a truncated DIV.S. That copy
  also keeps the 0x70003680 ratio in a local. Section 6 has the details.
- **The directory, lists and owners** are the `EmActorCollisionWorld` of
  docs/ACTOR_COLLISION.md: `EmActorCellTable`, `EmActorClassLists` and the
  pool's `EmActor`s. The walker reads `status`, `cls`, `uid`, `kind` (low
  byte) and `h52`.
- **Workers** (`EmCollMoveWorkers`): `lock_6440` (001A6440), `lock_7280`
  (001A7280), `grid` (0019CB60) and `sqrt` (0011E748). Each gets the
  scratch and returns the original's v0 through `*result`.
- **Faults** (−1):
  - a call whose flags can reach a missing worker. This is checked before
    anything is written: bit 0 with actor +0 bit 0 needs the lock for its
    class, bit 1 the directory and `sqrt`, bit 2 `grid`;
  - a static cell reached without `static_kind`;
  - a published owner whose word has bit 31 (pass 2 adds the raw word);
  - a word read outside the image;
  - a lock that succeeds without an entity (the original would read
    address 0x52);
  - a refused VU form;
  - the adapters' extra checks (section 4).

## 3. Verification

`python3 tools/test_coll_move_reference.py`: the quick run takes about 6 s
on 8 workers. The 06 route slice is the longest item.
`EM_TEST_FULL=1` runs the exhaustive sweep and the whole route. See
"Route" below for its runtime.

- **Interpreter.** The EE of `test_player_slide_reference.py`, subclassed
  (`FloatEE`) so that COP1 and VU0 macro go through `tools/ee_float_model.py`.
  The shared file is not edited. Code identity: the static call graph of
  every routine executed (the walkers, the prims, the workers and the SDK
  sqrt) is compared with the ELF in each world's RAM.
- **What every case compares:**
  - the return;
  - every `EmCollMoveScratch` field;
  - the **whole 16 KB scratchpad**. The original's final scratchpad must
    equal the initial one plus the stores the worker executions made plus
    the native fields;
  - the query actor's +0xB0..+0xB8;
  - that the original writes no other RAM;
  - the worker call sequence: routine, argument and scratch at entry. On
    the native side each worker runs the **original** routine as
    instructions over the same RAM, with the native scratch loaded, so the
    comparison is end to end.
- **Worlds:** the RAM and scratchpad snapshots `04_elevator_ride` (the
  05_boxes start), `05_boxes` (the 06_hill_slide start) and
  `08_truck_crossing` (truck, uid 15, pickups).

| Case class | Quick | Full | What |
|---|---|---|---|
| prim | 2,400 | 15,723 | Every prim of every captured hull (24 segments each). Synthetic prims AREA11 lacks: spheres, extended 0x4000, every face byte 0..7 with every extent sign, n-gons from ny 0.9 to −0.9. Exact-threshold cases: integer circles (on-circle starts and ends, tangents, band edges), the n-gon facing and inside epsilons (±2 ulps of ±1e-5), the interval ends, and the class ratios exactly 3.0 and 0.49029058 (normals found by searching the measured model), both ny signs, nx ± 1 ulp. |
| walk | 360 | 1,572 | 0019FE50 near every owner hull, with query classes 0, 2, 4 and −1 and self ∈ {player, owner, 0}. Long segments across several crates. Every owner re-kinded 0x50..0x53, 0x59, 0x5A against every query class. Injected drum owners (0x4000 cells). An unknown prim type inside owner hulls. A static-directory variant (uids 0..2 with bit 31, kinds from `D_0024D7C0` in RAM). |
| move/sweep | 220 | 480 | 0019AD00 / 0019AFE0 with flags 0..7 and bit 31. Actors: the player (class 0, lock 001A6440), real owners (self skip, 001A7280) and synthetic status/class/+0x52 combinations. Also segments at small coordinates, where the 1% step shows in the end bits. |
| scripted-lock | 60 | 120 | The lock handling with a scripted 001A6440 / 001A7280, applied identically on both sides. AREA11's own lists never make them succeed: 0 of 68 / 26 real calls in the quick run. Results 0/1/2, the entity's and the actor's +0x52 bits, bit 31. |
| adapter | 21 | 36 | All seven adapters against the original, including the `EmPlayerProbeHit` / `EmPlayerClimbHit` view from the original's final state. |
| fail-stop | 30 | 30 | Native only: each missing worker faults before any write. So do bit 31 on the const-position adapters and a bit-31 owner word. |

- **The named `.sqrt` worker.** Every 0011E748 argument the cases produce
  is also given to `em_item_sdk_sqrt`. The agreement is printed, not
  asserted, because the comparison above ran the original sqrt: 175 of 175
  in the quick run.

- **Route** (the original player stage, `SR.Stage` / `SR.RouteReplay`, under
  `FloatEE`). Every 0019AD00 / 0019AFE0 call the ORIGINAL stage makes is
  intercepted. The native walker and the original run on the live state and
  are compared as above, then the original runs for real and the stage
  continues.
  - Quick run: the first 05_boxes climb (press 6428) for 2 frames (the
    0015DF10 probe frame: 7 calls, modes 0 and 2), and the first 3 frames of
    the 06_hill_slide replay (51 calls).
  - `EM_TEST_ROUTE=1` (or `EM_TEST_FULL=1`): both 05_boxes climbs to idle
    and the whole 06_hill_slide beat (walk, slide, skid out, idle). The
    full run (17,931 cases plus the route) takes 254 s on 8 workers. Result (2026-09-23): 3,899 original calls, all
    identical:
    - 05_boxes press 6428: 80 frames, 58 calls (modes 0 ×56, 2 ×2);
    - 05_boxes press 6646: 80 frames, 58 calls (modes 0 ×56, 2 ×2);
    - 06_hill_slide: 212 frames through player states 0, 1 and 0x1C, with
      3,783 calls (modes 0 ×3,710, 2 ×11, 4 ×62).

    Every one of these went through the original 0019CB60 / 001A6440 as
    native workers.
- **Mutations** (quick run; the script lives only in the lane's scratch). Each
  of these is caught:
  - every comparison direction in the prims (strictness, bands, tangents,
    the inside start, the facing, inside and interval epsilons, both class
    thresholds);
  - the direction rejects and the face normal signs;
  - the candidate order;
  - the hull y gate;
  - swapping the reclamp sides;
  - the pass-1 break;
  - the self skip;
  - the pass-2 kind gate and the 0x52 query-class gate;
  - the lock vetoes;
  - the delta order and bit 31's z lane;
  - the record clear;
  - the end restore and the sweep's start step;
  - the 1% step constant (±1 ulp);
  - VU vs EE division in 00102760;
  - the `work` stores.

  These change nothing observable, and no test catches them:
  - the hull x/z AABB gate's `<` vs `<=`: a face or circle crossing must be
    strictly inside the segment, so a hull touching the extent cannot hit;
  - removing the reclamp after a hit: the prims test against the clamped
    end, so a hull beyond the hit cannot hit;
  - `0 + Q` vs `Q` in 00102760.

## 4. Binding (for the coordinator)

1. **World.** One `EmCollMoveWorld` per area: `.cells` = the
   `EmActorCollisionWorld` of docs/ACTOR_COLLISION.md §7 (the same table and
   `EmActorClassLists`; AREA11 needs no static kinds). One
   `EmCollMoveScratch` per world, zeroed at area load and shared by every
   call, as the scratchpad is. Its fields persist between calls, and
   0019FE50's `record` store and the mode-1 path rely on that.
2. **Workers.**
   - `.sqrt` = a wrapper returning `em_item_sdk_sqrt(x)` (0011E748's
     nonnegative path). Both 001A4830 arguments are ≥ 0.
   - `.grid` = **0019CB60, untranslated.** It needs the grid rank tables and
     spans (0019F1A0, `0x70003210` / `0x70003228` / `0x70003240`) that the EMCL
     lacks: docs/ACTOR_COLLISION.md "KNOWN INEXACT".
   - `.lock_6440` / `.lock_7280` = **001A6440 / 001A7280, untranslated.** The
     player's mask-7 probes (player +0 bit 0 set, class 0) call 001A6440
     every time.

   Until these three exist, every player mask (6, 7, 0x80000006) faults, so
   nothing may be bound live. Census L05 stopped here (2026-09-24): the world
   the adapters need is live (`src/game/em_collision_world.c`: the cell
   directory, the class lists the live owners publish into, the flags-7 grid),
   so the remaining work is the translation of 0019CB60 (a rank-span grid
   walk over 0019F1A0 / 0019ED80, like 0019D330, into this module's scratch)
   and of 001A6440 (the class-2 list's +0x58 geometry chains; the port's
   class-2 list is empty in AREA11 until Roger, L22, publishes), each with its
   oracle extended from this test's worker harness, then the bindings of item
   3. The port's player keeps `em_collision_move_probe` (em_player.c
   `probe_move` / `probe_sweep`) until then; the fence door's hull (mask bit 0)
   is the port's `em_door_probe` there.
3. **Player stage (w_0015BCF0), context `EmCollMovePlayer`** = { world,
   scratch, `player_states_actor()` (the live record: +0x00, +0x02, +0x52),
   self = that same live pointer }.

   | Slot | Adapter |
   |---|---|
   | `EmPlayerProbeWorkers.move` | `em_coll_move_player_move` |
   | `EmPlayerProbeWorkers.sweep` | `em_coll_move_player_sweep` |
   | `EmPlayerClimbWorkers.move` | `em_coll_move_climb_move` |
   | `EmPlayerClimbWorkers.sweep` | `em_coll_move_climb_sweep` (`pickup_box` = entity +0x10 == 00219550) |
   | `EmPlayerSlideWorkers.move` | `em_coll_move_slide_move` (bit 31 moves `position` x/z) |
   | `EmPlayerSlideWorkers.sweep` | `em_coll_move_slide_sweep` |

   `EmPlayerProbeWorkers` serves 001764E0, 00176C80 and 001756E0.

   The adapters fault in these cases:
   - bit 31 on a const-position slot;
   - a cell record whose kind byte is 0x35 (its +0x34 axis is not carried).

   A worker record (grid node) must come with `record_node` / `_normal` /
   `_axis` filled by the grid worker.
4. **Owners.** `EmDrumOriginalHooks.sweep` = `em_coll_move_owner_move`, with
   `EmCollMoveOwner` = { world, scratch, the drum's `EmActor`,
   `&EmDrumOriginal.position` }. It serves the flight arm's
   0019AD00(self, point, 0x80000007). That arm belongs to models 0xA/0xC,
   which AREA11 never places.
5. **What this replaces (do not rewire yet).** The legacy
   `em_collision_move_probe` (em_player.c `probe_move` / `probe_sweep`,
   em_game.c:479). It differs from the original in several ways:
   - it walks the EMCL set-2 static n-gons and the `EmCollCell` faces, not
     the owners' current cells;
   - it has no lock pass;
   - it uses host sqrt and division for the 1% step;
   - **its bit 31 sets pos = target + delta (the hit point), and pos = target
     with no hit. The original adds delta to the actor's own +0xB0/+0xB8 and
     leaves them alone with no hit.**

   Retire it when these adapters are bound.
6. **Makefile (not edited; hunks in the lane report).**
   - `test-coll-move-reference`;
   - `src/game/em_coll_move_original.c` added to `COMMON` after
     `src/game/em_collision.c` when it is bound. It needs only the
     `em_actor_collision.h` types, not its object.

## 5. Limits

- 0019CB60, 001A6440 and 001A7280 are workers (section 4). The test runs
  them as original instructions, so the walkers are verified, but the native
  port has no implementation of them.
- The route mode replays only the player stage (no camera stage, owners or
  scripts), as `RouteReplay` documents. Its purpose here is to produce the
  original's own walker calls on a real world.
- The walkers' scratch is separate from `em_actor_collision.c`'s private
  0019AB20 segment. In the original the two share the scratchpad. Every
  adapter copies its result at return, and no AREA11 consumer is known to
  read a walker field after a later 0019AB20. That has not been proven for
  the whole level.

## 6. Findings for other lanes

- **`em_actor_collision.c` float model.** Its 001A44B0/001A4650/001A4030/
  001A56A0/001A58B0 translations still use truncated add/sub/div. That is
  EE_FLOAT_MODEL.md §5c's harmonization item for that file. The measured
  001A4030 in this module passes the exact-threshold cases. The actor
  collision oracle shares the known-wrong COP1 of the shared interpreter,
  so the pair must be harmonized together. The `FloatEE` class here is a
  ready template.
- **`0x70003680`** is written by 001A4030 (the ratio) and by 001A4D10 (the
  x/z intermediates). This module publishes both into `work`.
- **The legacy move-probe bit-31 semantics** (section 4 item 5) affect
  every current user of `em_collision_move_probe` with `EM_COLL_SLIDE`.
