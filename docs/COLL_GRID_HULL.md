# The move walkers' grid pass and the hull locks (0019CB60, 001A6440, 001A6AD0, 001A7280)

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "coll-grid-hull". Module: `src/game/em_coll_grid_hull.{h,c}`. Oracle:
`tools/test_coll_grid_hull_reference.py` (units), with the move and segment
oracles (`tools/test_coll_move_reference.py`,
`tools/test_coll_segment_walkers_reference.py`) now comparing their queries
end to end with these translations bound.

Census L05 stopped on two untranslated workers, 0019CB60 and 001A6440
(COLL_MOVE.md section 4 item 2). Both are translated here, with the two
other hull locks, 001A6AD0 and 001A7280. They are bound directly inside the
collision modules, which no longer have a worker slot for any original
callee: 0019AD00, 0019AFE0, 0019A570 and 0019A910 run completely native.

## 1. What the originals do

All four are NEARMISS files in the decomp; the translation follows the .s.

**0019CB60 (the grid pass of mask bit 2).** Called by 0019AD00 / 0019AFE0
after the cell walker, over the scratchpad segment 0x70003190 -> 0x700031A0.
- The x and z direction masks: the end's (bits 0/1 for x, 0x10/0x20 for z)
  and the start's, chosen by `start <= end` on each axis. The y pair is never
  set.
- 0019F1A0 ranks the start and the end twice with swapped masks. The first
  pair's ranks (all six words of 0x70003240 are copied, only x and z are
  used) index the span helper tables *(0x70003228 + 4i); the second pair
  gives the ranks the walk tests.
- The span pick runs over directions 0, 1, 4 and 5 (2 and 3 are skipped).
  For an odd direction 0x70003B86 = rank, 0x70003B88 = helper[first rank];
  for an even one 0x70003B86 = helper[first rank], 0x70003B88 = rank + 1.
  The narrowest span under the node count (0x7000320C) wins; with none, the
  span registers are uninitialized (the native code faults).
- Each node of the span (node address *0x70003208 + 0x40 index) must pass
  its four x/z rank bounds: rank 0 < +0x0C skips, +0x0E < rank 1 skips,
  rank 4 < +0x14 skips, +0x16 < rank 5 skips.
- The node attribute (+0x1A) is stored in 0x70003B88 and gated like the
  static kinds of 0019FE50: 0x5A and above never pass; 0x51 needs query
  class 0 (0x7000324E), 0x52 query class 2, and 0x53 is refused to query
  class -1.
- 0019ED80 tests the node. Every accepted crossing moves the end's x and z
  (only) to 0x700031B0 and is remembered with its node.
- With a hit, 0x700031D0 = the last accepted node and 0x700031B0 = its
  crossing; the return is 0. Without one, 0x700031D0 is left as it was and
  the return is 1.

**The hull locks** walk the +0x58 geometry chain of an entity. A chain is a
count word, then records: three mask bytes (x, y, z), a bone slot byte m, a
u16 stride, an s16 vertex count n, the normal at +8, and from +0x18 n
vertices then n edge normals (the plane point is vertex 0). A first record
whose count is -2 is skipped and the count reduced by one. Slot m's matrix is
the 0x40 bytes at *(entity +0x110 + 4m) + 0x90, cached until m changes.
Per record:
- The mask: (arg & 0x10) with record byte 0 & entity +0x5C gives bit 0,
  (arg & 0x20) with byte 1 & +0x5D bit 1, (arg & 0x40) with byte 2 & +0x5E
  bit 2. A negative count clears it; a zero mask skips the record.
- The normal (w = 0) and vertex 0 (w = 1) go through the slot's matrix
  (001026A0). t = normal . point, d = dir . normal.
- The facing test (001A6440 only when mask bit 2 is set, 001A6AD0 always,
  001A7280 never): d must be <= -1e-5, else the record is skipped.
- hit = qa + dir * ((t - normal . qa) / d).
- The hit must lie STRICTLY between qa and qb on at least one axis (a strict
  crossing on x goes straight to the edge test, otherwise y, then z; none
  rejects the record).
- Every edge: (hit - transformed vertex k) . transformed edge normal k must
  be <= 1e-5.
- Accepted: 0x700031B0 = hit, D_700030B0 +0x24 (0x700030D4) = the
  transformed normal; 001A6440 / 001A6AD0 also store (record byte 1 &
  +0x5D & 0xFE) in 0x700030CC when mask bit 1 is set, and the record's
  whole first word in 0x700030D0. The segment end becomes the hit (later
  records and entities test the shortened segment).
- The result is 1 when any record was accepted.

The three locks differ in which entities they walk and what they store at
entry:
- **001A6440(arg)** (0019AD00 / 0019AFE0 for a class-0 query actor with
  mask bit 0 and +0 bit 0, arg 0x40; 0019A570 mask bit 0, arg id & 0xFFFF).
  Entry: 0x700030CA = 0, 0x700030CC = 0, 0x700030D0 = 0, 0x700031D0 =
  D_700030B0. It walks the published class-2 list D_00275B8C / D_00275B94
  (EM_ACTOR_LIST_CLASS2), entities whose +0x00 has bit 0 and whose +0x58
  is set. An entity with an accepted record becomes 0x700031D4.
- **001A6AD0(arg)** (0019A910 mask bit 0, arg 0x40): the same, skipping
  entities of class (+2 & 0x1F) 0 and 2, with the facing test always on.
  After an entity with an accepted record, 0x700030CA is classified from the
  normal: ratio = ny^2 / (nx^2 + nz^2); below 0.49029058 gives 0x2000; up to
  3.0 gives 0x1000 (0x800 when ny < 0); above, 0x4000 (0x8000 when ny < 0).
- **001A7280(arg)** (0019AD00 / 0019AFE0 for a query actor of nonzero class
  with mask bit 0 and +0 bit 0, arg 0x40). Entry: 0x700030CA = 0,
  0x700031D0 = D_700030B0 (0x700030CC / D0 untouched). It walks the player
  record D_008102B0 only: it returns 0 when its +0x58 is 0 or its +0x00 is
  0 (any bit). No facing test, no 0x700030CC / D0 stores. Once a record is
  accepted, 0x700031D4 = D_008102B0.

**Corrections to the readable C (NEARMISS files).**
- 0019CB60: the C walks its local rank copy, the rank words, the helper
  pointers and the span table with doubled strides (two halfwords, four
  words); the .s steps one element at a time.
- 001A6440: the C returns 0 always; the .s returns 1 when any record was
  accepted. The C accepts a hit inside the segment on all three axes; the
  .s needs a strict crossing on one. The C rejects a record when an edge dot
  is <= 1e-5; the .s rejects when one is above. The C stores the record's
  first byte in 0x700030D0; the .s stores its whole first word.
- 001A7280: the C comment's "within [qa, qb] on all 3 axes" is the same
  misreading of the interval test.

## 2. Translation

| Original | Native |
|---|---|
| 0019CB60 | `em_coll_grid_hull_0019CB60(grid, EmCollProbeState *)` |
| 001A6440 | `em_coll_grid_hull_001A6440(lists, hulls, EmCollHullScratch *, arg)` |
| 001A6AD0 | `em_coll_grid_hull_001A6AD0(lists, hulls, EmCollHullScratch *, arg)` |
| 001A7280 | `em_coll_grid_hull_001A7280(hulls, EmCollHullScratch *, arg)` |
| 001026A0, 001028B8, 001028D0, 00102738, 00103230, copy_qw4 | static VU0 helpers (the instruction forms of em_ee_float.h) |

- **Reused:** 0019F1A0 and 0019ED80 (em_coll_probe_original.c).
- **The grid view** is the EMCL rank section (`EmCollProbeGrid`, EMCL flags
  7; COLL_PROBES.md section 3), the one the collision world already loads.
  `em_coll_grid_hull_node_record` gives a node's identity for a record slot
  and `em_coll_grid_hull_node_index` maps it back.
- **The chains** come from `EmCollHullWorld`: `chain(context, entity, out)`
  supplies the bytes +0x58 names (original layout) and the slot matrices;
  `player` is the view of D_008102B0 (+0x00, +0x09, +0x58, +0x5C..+0x5E) for
  001A7280. The locks read the entity's +0x00, +0x02, +0x58 and +0x5C..+0x5E
  from its `EmActor` (status, cls, w58, w5C) and ask for the chain only
  after those gates, where the original first reads it.
- **Floats:** every COP1 and VU0 macro operation goes through
  em_ee_float.h; the class ratio is the MULA / MADD / MUL / DIV sequence of
  the .s.
- **Faults (-1):** a missing grid; a table index outside the tables; the
  uninitialized span; a class-2 entry that names no actor; an entity whose
  chain the world cannot supply; a read outside the chain, or a halfword /
  word read at an odd / unaligned offset (an EE address error); a bone slot
  outside the supplied matrices; a refused VU form.

### Bound in the collision modules (the worker slots are gone)

- **em_coll_move_original** (0019AD00, 0019AFE0). `EmCollMoveWorkers` is
  removed. `EmCollMoveWorld` is now { `cells` (directory, class-4 and
  class-2 lists, static kinds), `grid` (EmCollProbeGrid), `hulls`
  (EmCollHullWorld), `math` (EmSdkMathContext for 0011E748, the
  em_sdk_math_original translation) }. `EmCollMoveScratch` gained the words
  the translations write: `cell_word_1c` / `cell_word_20` (0x700030CC /
  D0), `rank[6]` (0x70003240..4A) and `span_lo` (0x70003B86); `kind` is
  0x70003B88 (the span word and the attribute). A grid hit sets `record` to
  the node identity with `record_node` (attr | class << 8) and
  `record_normal` (node +0x24, the EMCL plane). `record_axis` (node +0x34)
  is zero: the EMCL does not carry it, so the adapters fault on a hit whose
  surface byte is 0x35 (00175CF0 reads that axis), for a grid node as for a
  cell record. 0019AD00 / 0019AFE0 now run on copies of the scratch and the
  actor and commit only on success, so every fault leaves both untouched.
- **em_coll_segment_walkers** (0019A570, 0019A910). `EmCollSegmentWorkers`
  is removed; `EmCollSegment`'s third member is now `const EmCollHullWorld
  *hulls` (NULL: no chains). Mask bit 0 runs 001A6440(id & 0xFFFF) /
  001A6AD0(0x40) over `world->cells->lists`' published class-2 list.
  `EmCollSegmentFaceScratch` gained `hull_word_1c` / `hull_word_20`
  (0x700030CC / D0).

### What the coordinator binds (em_collision_world.c is not this lane's)

- `w.seg = (EmCollSegment){ &w.probe, &w.math, NULL, &w.state, &w.face }`
  still compiles and is exact today: the port's class-2 list is empty in
  AREA11 (nothing publishes class 2 before Roger, census L22), so the locks
  walk nothing and return 0, as the original does with an empty list. When
  Roger (or any class-2 entity with +0x00 bit 0 and a +0x58 chain)
  publishes, pass an `EmCollHullWorld` whose `chain` supplies his chain
  bytes and his bone matrices (the record's +0x110 slots, +0x90); without
  it the mask-bit-0 queries fault instead of missing him.
- The move world, when L05 binds the player's move/sweep slots: `{ &acw,
  &grid, &hulls, &math }` over the collision world's `EmActorCollisionWorld`,
  `EmCollProbeGrid` and SDK context, one `EmCollMoveScratch` zeroed at area
  load. `hulls.player` must be an `EmActor` view of the player record
  (+0x00 status, +0x09 bones, +0x58, +0x5C) and `hulls.chain` must supply
  its chain (the player's own +0x58 chain: 8 records over bone slots 2 and
  3 in every capture); only a nonzero-class query actor reaches 001A7280,
  and none does on the route.
- Stale text outside this lane: em_collision_world.c (line ~174) still
  says the hull locks are not translated and that a mask-bit-0 query
  faults. Both are now wrong: the locks run natively, and the NULL third
  member of `w.seg` means "no hull world" (an empty class-2 list walks
  nothing; a published entity without a chain view faults).
- Build lists (not edited by this lane; hunks in the lane report): the
  Makefile's COMMON, `tools/test_camera_interaction_fixture.py` and
  `tools/test_area11_interaction_host.py` compile em_coll_segment_walkers.c
  and need `src/game/em_coll_grid_hull.c` beside it.

## 3. The fail-stop workers the walkers could reach

Every worker slot of the move, segment, probe and list walkers, with the
census evidence (`../Extermination/build/s87/census/route_functions.json`:
one-shot breakpoints on all 2,991 candidate entries over S0..S3 and beats
00..14):

| Walker module | Slot | Original | On the census route | Now |
|---|---|---|---|---|
| move | `lock_6440` | 001A6440 | every beat S2..14 | translated, called directly |
| move | `lock_7280` | 001A7280 | never | translated, called directly |
| move | `grid` | 0019CB60 | every beat S2..14 | translated, called directly |
| move | `sqrt` | 0011E748 (001A4830's) | 001A4830 never; 0011E748 every beat | em_sdk_math_original's translation, called directly |
| segment | `lock_6440` | 001A6440 | (as above) | translated, called directly |
| segment | `lock_6AD0` | 001A6AD0 | never | translated, called directly |
| probe | `face_segment` / `round_segment` | 001A50A0 / 001A5C30 | 001A50A0 beats 02..06; 001A5C30 never | already bound to em_coll_segment_walkers (em_collision_world.c) |
| list walkers | none | | | 0019E280, 0019E930, 001A3980, 0019F330 have no worker slot |

So no fail-stop worker slot remains in these walkers. What remains is data:
the chains of class-2 entities (Roger) and the player view for 001A7280.
The 001AAD00 list passes (em_coll_list_passes.c, `w_001A8840` ..
`w_0021BD10`) are the list PASSES, not walkers, and stay fail-stop
(COLL_LIST_PASSES.md).

## 4. Verification

`python3 tools/test_coll_grid_hull_reference.py` (about 5-8 s on 8
workers; `EM_TEST_FULL=1` runs every case). The original instructions run
over captured RAM in the move lane's FloatEE (the measured float model); the
code of 0019CB60, 0019F1A0, 0019ED80, the locks and their SDK leaves is
checked against the pinned ELF. Every case compares the return value and
the whole 16 KB scratchpad (the original's final scratchpad must equal the
initial one with the native words written in), and the original may write
no RAM.

| Cases | Quick | Full | What |
|---|---|---|---|
| 0019CB60 | 330 | 1,040 | Beats 05, 06, 10, 12, 13: level (and 15% sloped) segments at the player's recorded positions in every direction, axis-parallel ones, captured or randomized persistent words (ranks, span words, point, record), query classes 0/2/4/-1/1; segments ending on, and starting one ulp beyond, a node's extreme vertex (the rank-bound equalities) |
| gate sweeps | 10 | 60 | On nodes the segments really cross: attributes 0x00, 0x50..0x54, 0x59, 0x5A, 0x78 x query classes 0/2/-1/1 (RAM and EMCL patched) |
| real chains | 90 | 300 | Beats 13 and 14: 001A6440 / 001A6AD0 over Roger's real chain and bone matrices (the published class-2 list), args 0x40, 0x10..0x70 and random ids; 001A7280 over the player's own chain in beats 06, 10, 14 |
| synthetic chains | 120 | 400 | 1..3 entities with rotated/translated bone matrices, 1..6 records, slot switches, -2 first records, negative counts, every mask byte and arg bit, status / class filters (0, 2, 0xA, 4, 0x22, 0xB), back faces, missing chains |
| exact | 225 | 225 | Identity matrices: the facing epsilon (d exactly -1e-5 +- 2 ulps, with and without a strict crossing), the edge epsilon (1e-5 +- 2 ulps outside the y and z edges), strict interval ends moving -x, and moving +x (a plane facing -x with arg 0x40, and the +x plane crossed from behind with arg 0x30, where only 001A6AD0 still tests facing; ends and starts at +-0, 1 denormal ulp either side of the plane and +-1e-3), zero-length segments, and the 001A6AD0 class ratios exactly 3.0 and 0.49029058 (+- 1 ulp of nx, both signs of ny) |
| y / z interval, vertex counts | 333 | 333 | y- and z-plane records (identity, and frames spun about the plane's axis and translated): segments along y alone and along z alone, both directions, normal facing and back, args 0x40 (facing test on) and 0x30; segments ending or starting exactly on the plane, 1 denormal ulp either side, +-1e-3 and one ulp short of 1; x-plane chains with vertex count 0 beside count -1 (crossings outside the square: only count 0 accepts), for all three locks |
| real chains, y / z | 60 | 310 | Segments along y alone or z alone through Roger's records (001A6440 / 001A6AD0, beats 13, 14) and the player's (001A7280, beats 06, 10, 14): record centres, vertices and edge midpoints, mostly from the facing side, some ending short |
| synthetic, y / z | 70 | 240 | synthetic_entities worlds (rotated frames, several records) crossed along y alone, along z alone, or along y with z drifting |
| fail-stop | 8 | 8 | Native only: no chain resolver, a slot outside the matrices, an odd stride, a chain cut short, 001A7280 without the player view |

Result (2026-09-24, second fix round, quick, 5.9 s on 8 workers): 1,238
cases identical; `EM_TEST_FULL=1`: 2,908 cases identical, 13.8 s (while
another oracle ran); in full mode every real (lock, y/z, direction)
combination has accepts (2..26).

| Routine (quick) | Hit / cases |
|---|---|
| 0019CB60 | 123 / 330 hits (it returns 0 on a hit; plus 5 gate sweeps of 36 patched runs each) |
| 001A6440 on Roger's chain | 17 / 67 |
| 001A6440 synthetic and exact | 129 / 252 |
| 001A6AD0 on Roger's chain | 9 / 22 |
| 001A6AD0 synthetic and exact | 89 / 215 |
| 001A7280 on the player's chain | 39 / 61 |
| 001A7280 synthetic and exact | 114 / 203 |

001A6AD0's classes on its hits: 0x2000 x68, 0x4000 x11, 0x8000 x10,
0x800 x5, 0x1000 x4.

**Which clause decides.** A segment that moves along one axis alone
keeps the hit's other coordinates exactly equal to qa's, so only that
axis's clauses of the interval ladder can accept it, and its direction
picks the clause (qa above the hit: the first, below: the second). The
test counts the accepted cases of that kind and fails unless every lock
has at least one for each of x-down, x-up, y-down, y-up, z-down and z-up,
and unless the real chains supply some for 001A6440 and 001A7280 on both
y and z. Quick run: the 18 (lock, axis, direction) combinations have 3..38
synthetic and exact accepts (y and z: 9..19). The real chains add, per
lock: 001A6440 y down 3, y up 1, z down 2 (no z-up accept on Roger's
chain); 001A6AD0 y down 2, y up 1, z up 1 (no z-down); 001A7280 y down 1,
y up 8, z down 3, z up 5. The missing real combinations are covered by the
synthetic and exact cases only.

Each of the 12 comparisons of the ladder (per axis: clause 1's start
`qa > hit` and end `qb < hit`, clause 2's start `qa < hit` and end
`qb > hit`, all strict) has cases that tell it from its non-strict form:
the segment starts or ends exactly on the record's plane, so the hit
equals qa or qb on the deciding axis. On x the -x cases pin clause 1 and
the +x cases pin clause 2.

**End to end.**
- `tools/test_coll_move_reference.py`: every 0019AD00 / 0019AFE0 /
  0019FE50 / prim / adapter case now runs the native walker with nothing
  hooked; the original's 0019CB60 / 001A6440 / 001A7280 / 0011E748 run in
  place and are only counted. New: 80 (quick) of 180 hull-lock move/sweep
  cases (the player through Roger's real chain with his +0x52 veto bits,
  synthetic class-2 worlds for the player and class-0 actors, a synthetic
  player chain for nonzero-class actors), and route replays of beats 10,
  11, 12, 13 and 14 (quick: 2 frames each; whole: 400 frames each). Quick
  run: 3,097 cases and 228 route calls identical, about 5 s. The 16
  lock-argument cases (every mode) put one record with x and y mask bytes
  but no z byte beside one with all three, on the player's chain
  (001A7280) and on a class-2 entity (001A6440); the run asserts that the
  original's lock rejects the first and accepts the second, which pins the
  0x40 argument. `EM_TEST_FULL=1` (2026-09-24, fix round, 535 s on 8
  workers): all 18,007 cases (hull-lock and lock-argument moves: 001A6440
  accepted 98 of 277 calls, 001A7280 19 of 67) and the whole route below
  identical. `EM_TEST_ROUTE=1` alone takes
  about 12 min. The route: 38,915 original
  walker calls identical, every one through the native grid pass and lock:

  | Replay | Frames | Calls | Modes | 0019CB60 hits | 001A6440 calls |
  |---|---|---|---|---|---|
  | 05_boxes press 6428 | 80 | 58 | 0 x56, 2 x2 | 0 | 58 |
  | 05_boxes press 6646 | 80 | 58 | 0 x56, 2 x2 | 0 | 58 |
  | 06_hill_slide | 212 | 3,783 | 0 x3,710, 2 x11, 4 x62 | 62 | 3,471 |
  | 10_cage_roof_roger | 400 | 5,440 | 0 x5,103, 4 x337 | 337 | 4,726 |
  | 11_crevice_prompt | 400 | 6,537 | 0 x6,065, 4 x472 | 472 | 5,277 |
  | 12_crevice_jump | 345 (trace end) | 6,689 | 0 x6,336, 4 x353 | 353 | 5,796 |
  | 13_east_tower | 400 | 8,699 | 0 x8,268, 4 x431 | 431 | 6,804 |
  | 14_roger_encounter | 400 | 7,651 | 0 x7,650, 4 x1 | 1 | 6,748 (over Roger's chain) |

  No route call's lock accepted a record (the player never walks into
  Roger's hull there); the accepting paths are proven by the aimed and
  synthetic cases above.
- `tools/test_coll_segment_walkers_reference.py`: the mask-7 route-row
  queries of both 0019A570 and 0019A910 run the native locks over each
  beat's own class-2 list, and beats 13 and 14 add both queries through
  Roger's chain (8 aimed segments each in the quick run, 60 in the full).
  Each aimed segment is re-run with stale words the lock replaces (a
  nonzero 0x700030CC / 0x700030D0 and a grid-node record in 0x700031D0),
  masks 1 and 3, both queries; the run asserts that some of those locks
  accept. Quick run (fix round): 202 lock calls, 18 accepted plus 36 over
  stale words, all identical; about 5-12 s depending on load.
  `EM_TEST_FULL=1` (2026-09-24, fix round, 661 s): all 12,439 route rows
  of beats 00..14 plus 60 aimed segments on each Roger beat; 25,598
  original lock calls (109 accepting on the plain runs, 218 more over
  stale words), all identical, with every other case of that oracle
  unchanged and passing.
  The lock-argument cases (every mode, in the gate-boundary item on beat
  05) give one class-2 entity an x-plane record with x and y mask bytes
  but no z byte, then the same record with all three, crossed +x with
  mask 1 (the lock alone). 0019A910 (001A6AD0, argument 0x40) must reject
  the first and accept the second; 0019A570 (001A6440, id & 0xFFFF) runs
  with ids 0x40 (as 0019A910), 0x70 and 0x30 (both accept) and 0xFF80
  (none of the lock's bits: both reject). The run asserts the original's
  lock results and the 10 case count. Quick run (second fix round): PASS,
  8.5 s. `EM_TEST_FULL=1`: PASS, 751 s (10 lock-argument cases
  included; every other count as in the fix round).

**Mutants** (applied one at a time to em_coll_grid_hull.c; each caught by
the quick run): the readable C's all-axes interval test; the edge and
facing epsilons' `<=` as `<`; the facing test always on in 001A6440 or on
in 001A7280; 0x700030CC without the 0xFE mask; 0x700030D0 as a byte;
001A7280's status as bit 0; 001A6AD0 without its class-2 skip; each class
threshold's direction and the ny < 0 swap; no segment shortening; the -2
record count; negative counts kept; the slot cache never reloaded; vertices
with w = 0, normals or edge normals with w = 1; the entity kept at the
first hit; the x mask against +0x5D; any status bit in 001A6440; the entry
clears of 0x700030CA / CC / D0 (both locks) ; the 0x700030CC / D0 stores in
001A7280; 001A7280 without its +0x58 check; in 0019CB60 the y clamp, each of
the four rank-bound comparisons as `<=`, the 0x53 and 0x5A gates, swapped
first-pair masks, the y span directions, the odd/even span rule and the
returns. Added with the y / z cases (fix round, each caught by the quick
run): the interval test on x only; the y axis skipped; each of the two y
clauses and each of the two z clauses removed alone; on y and z, the end
comparisons made non-strict (`qb <= hit`, `qb >= hit`) and the start
comparisons made non-strict (each of the four alone); a vertex count of 0
masking the record like a negative one. Second fix round (2026-09-24, all
re-run on a scratch copy, one at a time, each caught by the quick run):
each of the 12 ladder comparisons flipped between strict and non-strict
on its own axis (x clause 2's end, `qb > hit` made `qb >= hit`, is caught
by the new +x interval-end cases alone: 20 cases differ, all of them
'interval end +x' at +-0 and +-1 denormal ulp); each of the six clauses
removed alone; the ladder on x only (200 cases differ).

In em_coll_move_original.c, each of these is caught by the move oracle's
quick run: the lock result not naming the cell record in 0x700031D0; the
grid pass not writing back the ranks, 0x70003B86 or 0x70003B88; the
0x700030CC word not written back (the hull cases stage stale 0x700030CA..D0
words); the locked entity's +0x52 veto and the nonzero-class actor's own
+0x52 requirement removed; 001A7280 called with 0x70 instead of 0x40 (the
lock-argument cases: a player-chain record with x and y mask bytes but no
z byte, which only another argument would test).

In em_coll_segment_walkers.c, each of these is caught by the segment
oracle's quick run: the 0x700030CC or the 0x700030D0 write-back removed,
and the cell-record store into 0x700031D0 after the lock removed (the
Roger-chain cases re-run with stale nonzero 0x700030CC / D0 and a grid-node
record in 0x700031D0, masks 1 and 3, both queries); 0019A910 passing 0x70
instead of 0x40 to 001A6AD0 (the lock-argument cases below: the original
rejects the no-z record, the mutant accepts it).

Equivalent (no reachable input tells them apart; not test gaps):
- skipping the -2 first record or not: that record's negative count masks
  it, and both walk the same records;
- restoring 0x700031B0 after 0019CB60's walk: 0019ED80 writes it only on an
  accepted crossing, which is the one saved;
- the operand order of the commutative VU products (dot operands, the hit
  sum): the forms clamp both operands alike for finite inputs.

## 5. Limits

- **Node +0x34 (the axis)** is not in the EMCL rank section, so a grid-node
  record carries no axis. The oracle does not compare `record_axis` for a
  grid node (every other field it does). Two consumers read the axis:
  - on the move walkers' path, only surface 0x35 (00175CF0;
    em_player_floor_apply reads `hit->axis` only for mode 0x35), so the
    move adapters fault on a 0x35 hit;
  - on the segment path, 00181F60 (em_player_closure_10_12_19.c, about
    line 522) reads `hit.axis[0]` and `hit.axis[2]` after a mask-4
    0019A570 query whose hit has attribute 0x36.
  AREA11's grid has no node of attribute 0x35 or 0x36 (its 3,099 nodes
  carry 0x00, 0x03..0x05, 0x32, 0x3C, 0x46, 0x50, 0x51, 0x5A, 0x5D and
  0x78; checked on beats 05 and 14), so neither consumer can meet a
  grid-node axis in the first level. A future EMCL revision could carry
  +0x34..+0x3F (decomp exporter lane).
- **The chain view is test-backed only.** No live module supplies a chain
  yet: the port's class-2 list is empty in AREA11 and the player is not an
  `EmActor`. The first class-2 publisher (Roger, L22) must come with its
  chain view, or mask-bit-0 queries fault from then on.
- **Route replays drift.** Only the player stage runs in the replays of
  beats 10..14; every walker call is still compared on the live state in
  which it is made, but the later frames are no longer the capture's.
- The copy-and-commit wrapper of 0019AD00 / 0019AFE0 changes nothing the
  original can observe; it only makes every fault leave the caller's scratch
  as it was.
