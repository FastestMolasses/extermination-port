# Collision walkers and actor list passes (census lane L08)

Date: 2026-09-23. Lane "coll-list-passes". Scope: census lane
**L08-coll-missing-and-list-passes** of `docs/FIRST_LEVEL_CENSUS.md` (14
functions, 2,081 instructions: 7 stand-in, 7 missing), plus the two small
queries that are the only callers of three of them (0019B7D0 and 0019BA80).

Files (new, not yet in the build):

| File | Contents |
|---|---|
| `src/game/em_coll_list_passes_walkers.h/.c` | 0019B7D0, 0019E280, 0019BA80, 001A3980, 0019E930, 0019F330, the camera-ground adapter |
| `src/game/em_coll_list_passes.h/.c` | 001A9D20, 001A8DA0, 001A9F60, 001AA140, 001A7870, 001A8BE0, 001A8660, 001A9000, 001A97B0, 001A9B10, and 001AAD00's nine-hook sequence |
| `tools/test_coll_list_passes_reference.py` | the original-instruction oracle for all of the above |

No original code, data or disassembly is in these files. Tables the routines
read (the rank tables, the cell directory, D_0024A740, the SDK math tables)
come from the user's ELF and captured RAM at run time.

## 1. Status per function

"Before" is the census row; "after" is what this lane delivers. Nothing here
is bound into the live game (see section 4), so every row becomes
**verified-unbound**, not live.

| Address | Decomp | Before | After | What it does |
|---|---|---|---|---|
| 0019E280 | BM | stand-in (em_camera.c `interaction_camera_query` ground branch) | verified-unbound | The camera's grid ground walk: all six rank directions (0019F1A0 on both segment ends, twice), the narrowest rank-table span, each node's six rank bounds, attr **0x78** only, 0019ED80. On a hit the segment end moves to the crossing and the last accepted node is the record. |
| 0019E930 | NM (.s) | stand-in (em_collision.c) | verified-unbound | The attribute grid walk of 0019BA80: the y-ordered 0019F1A0 pairs (masks 8/4, then 0x37/8, or the mirror), a span pick in which directions 2 and 3 use the first pair's y ranks as helper indices, the six rank bounds (+0x0C..+0x16), attr **0x1E..0x59**, 0019ED80; each hit moves the segment end's y to the crossing's y. Returns 0 on a hit. |
| 0019F330 | AW (.s) | stand-in (em_collision.c `column_node`) | verified-unbound | 0019BC40 pass 2's plane crossing: the line a -> b against the node plane (no front-face test), the ring edge test (<= 1e-5), q[0..2] the crossing, 0x70003680 = sqrt(nx^2 + nz^2) (0011E748), 0x70003684 = \|ny\| / that (or 0x7F7FC99E below 1e-4), q[3] = +-(pi/2 - atan(0x70003684)) by the sign of ny. |
| 001A3980 | NM (.s) | stand-in (em_collision.c) | verified-unbound | The attribute cell walk of 0019BA80: pass 1 the static cells (the leading directory words with 0x80000000, stopping at the first without it; 0x40000000 skips a word, 0x20000000 skips it while the query class 0x7000324E is 0) whose D_0024D7C0 kind byte is **0x1E..0x59**, the hull at tbl + (word & 0x3FFFFFFF); pass 2 every published class-4 owner **with no kind gate** (unlike its sibling 001A32C0); a hull is tested when the segment's y range overlaps its AABB's and the segment start's x/z lies inside it; prim tests 001A4030 / 001A4650 / 001A44B0 (0x8000 prims only in pass 2; an unknown type nibble is neither tested nor stepped over); each hit clamps the segment's y, narrows the y range to [start, hit] and stores the kind in 0x700030CA's low byte. Returns 0 on a hit. |
| 001A7870 | NM (.s) | stand-in (census: em_collision.c; in fact nothing runs, 001AAD00 is reported unmirrored) | verified-unbound | Class-2 capsule push-apart. Pass 1 writes +0x50 = 1 for an active entry whose +0x58 record is set, has a nonzero first word, -2 at +0xA and shares a bit of its +6 byte with the entry's +0x5E, else 0. Pass 2: for each marked outer entry and each marked entry after it, the capsules (the +0x58 offsets on the +0x110[slot] node, +0xC4 is the long axis) overlap on the long axis and within the radii in x/z; unless the inner entry's +0x52 bit 0 is set, the inner entry's +0xB0/+0xB8 are pushed out (only +0xB0 += overlap when 001000C0(00128350(len), 0.001) says len < 0.001). |
| 001A8660 | BM | missing | verified-unbound | The player (a0) against one class-0xD type-1 entry: x/z circle test (0011E748) and the height test (half heights from the +0x30 records); on overlap the entry's +0x34 behaviour(entry, player, player + 0xB0); if the player's state byte is 1: 0021BD10 for +0xD = 0xB (player +0xF = 2), the knock-back speed from D_0024A740/D_0024A780 by D_0081070A into +0x22C (+0xD 3/4) or +0x224, state = 3, the direction normalize(player +0xA0 - entry +0xB0, w = 1) into +0x70; then 0x70003B86 = 0 (ends 001A8BE0's walk). |
| 001A8BE0 | BM | stand-in (em_enemy.c legacy pair/contact pass) | verified-unbound | Unless D_0028A9A0 or 0x70003B8D is set: walks the class-0xD live list with the counter in 0x70003B86; active entries by type: 1 -> 001A8660, 3 -> 001A8840, 5 -> 001A8970. |
| 001A8DA0 | BM | missing | verified-unbound | Class-1 x class-0xD: for each active outer entry, the inner walk (0x70003B86) calls 001A8CE0(outer, inner) for active type-3 entries with +0xD 0. |
| 001A9000 | BM | stand-in (em_enemy.c legacy contact pass) | verified-unbound | Class-0xD type-5 entries (+0xD != 0xB, status 1) against the class-4 list (0x70003B88): types 0xA/0xC/0x18/0x2A -> 001A8F40; 6/0x1E -> 001A8E80; 0x1C/0x50/0x1F -> 001A8E80 unless D_00810700 == 0 and D_00810702 == 5. |
| 001A97B0 | BM | missing | verified-unbound | Class-0xD entries (status 1; type 3 with +0xD 0 and +0x56 != 0, type 5, or type 6 with +0xD 2) against the class-2 list (status 1, class byte not 0xA): inner types 0, 2..7, 9..11, 16..19, and 1 unless (+0xD 3 and (+5 == 9 or D_00810354 < 50.0)); handler by the outer type: 5 -> 001A9360, 3 -> 001A96F0, else 001A9480. |
| 001A9B10 | BM | missing | verified-unbound | Class-2 type-0 entries whose +0x2D4 word >> 8 is 1..3 against the class-4 list: type 7 with a nonzero +0x38 float -> 001A99E0(outer, inner). |
| 001A9D20 | BM | missing | verified-unbound | Class-1 x class-2 (counts in registers, re-read from the globals per outer entry): active inner types 0, 1, 4..7 -> 001A9C40(outer, inner). |
| 001A9F60 | BM | missing | verified-unbound | Unless 0x70003B8D or D_0028A9A0: class-2 entries with class byte 2, active, type 0 -> 001A9E00(player, entry). |
| 001AA140 | BM | missing | verified-unbound | Class-2 unordered pairs (outer counter 0x70003B88, inner 0x70003B86 seeded from it), both passing class byte 2, type 0, +0x2D4 & 0xF == 0 and status != 2 -> 001AA000(a, b, a + 0x1F0, b + 0x1F0). |
| 0019B7D0 | BM | (census: verified-unbound, but only hooked as a worker) | verified-unbound | Stages start = from, end = to, both w = 1.0, 0x700031D4 = 0; 0019E280; 4 on a hit (end restored), else 0x700031D0 = 0; 0x700031D8 = result. |
| 0019BA80 | NM (.s) | (census: verified-unbound, but only hooked as a worker) | verified-unbound | The twin of 0019B8C0 over 001A3980 / 0019E930: start = end = point, start.y -= box.y, nudge +-0.001 by the sign of box.y, both w = 0, 0x7000324E = actor +2 & 0x1F, 0x70003254 = actor +0x14 (mask bit 1), results 2 / 4, then the delta 0x700031C0 = point - end. |

### Findings against the decomp C

- **Hidden second arguments (byte-matched files).** 001A9F60's C calls
  `func_001A9E00(a0)` and 001A9B10's calls `func_001A99E0(e)`; at both call
  sites the entry is still in the second argument register (001A9F60 loads
  it at 0x1A9FA0, 001A9B10 reads +0x38 through it at 0x1A9BE4) and both
  callees read that register as their second argument. The translations pass
  (player, entry) and (outer, inner); the oracle compares the registers the
  original passes.
- **0019E930's readable C is wrong in two places:** the -1 test on the rank
  at 0x70003244 is a halfword (0x19EB94), not a word, and the rank-table
  walk steps one entry at a time (0x19EBF8), not two.
- **0019E930's -1 test cannot fire.** Every mask it passes to 0019F1A0
  writes rank 2 with a value >= 0 before the test. It is translated as
  written.
- **001A3980 differs from its sibling 001A32C0** (em_coll_probe_original):
  the pass-1 kind window is 0x1E..0x59 instead of < 0x1E, and pass 2 has no
  kind gate at all (the +0x54 byte is read only after a hit).
- **001A3980's 0x20000000 words on the route.** 0019BA80 takes the query
  class from the actor's class byte & 0x1F, and the player's class byte is
  0x20 in every captured beat. So the player's ladder probes run at query
  class 0 and skip any 0x20000000 static word. The captured AREA11 directory
  has no flagged words at all. A flagged word's hull address lies 0x20000000
  above the table, in the EE's uncached mirror of main RAM.
- **001A3980's NEARMISS C differs from the .s in two more places.** Pass 1
  leaves its loop at the first directory word without 0x80000000
  (0x1A3A18); the C continues to the next word. And the hull gate is an
  overlap test (the hull is skipped when the segment's highest y is below
  the AABB's minimum y, or its lowest y above the AABB's maximum y); the C
  reads it as a containment test with the two bounds exchanged. The
  translation follows the .s, and the oracle stages both (a flagged uid 1
  behind an unflagged uid 0, and segments that overlap a hull without
  lying inside it).
- **Captured 0x4000 prims.** Every beat's directory holds two 0x4000
  prims without 0x800: the only prim of uid 5's hull and of uid 6's. On
  beats 00, 03 and 13 a published class-4 owner is bound to each (records
  0x7A99D0 and 0x7A9CC0, status 1), so pass 2 tests them on captured data:
  a vertical segment onto either hull top hits the prim (0x700030CA =
  0x4046). No captured hull holds a 0x8000 prim, and no captured round
  prim has 0x800 set (0x800 n-gons are common: 79 of the 122 prims).
- **001A3980's uid 0xFF skip is redundant below 256 cells.** Pass 2 rejects
  uid 0xFF (0x1A3D70) and later any uid >= the count (0x1A3DA0). With
  AREA11's 27 cells, only the order of the reads differs. The skip changes a
  result only when the count is at least 256.
- **001AA140 decrements its outer counter after the inner walk**, so a
  callee that zeroed 0x70003B88 would send the original far past the list.
  The oracle's scripts never do that inside 001AA000.
- **001A7870's long axis is +0xC4 (y).** The NEARMISS C's names suggest x;
  the push is in x/z (+0xB0/+0xB8).
- **0019E280's rank bounds at equality.** 0019F1A0's rank is the last table
  index whose coordinate is strictly below the query (c.lt.s). So on the
  three minimum tables (+0x0C, +0x10, +0x14: the gate is bound <= rank)
  equality means "the node is the last one starting below the segment's
  top", an ordinary case the oracle now reaches. On the maximum tables
  (+0x0E, +0x12, +0x16: bound >= rank) equality means the node's maximum
  lies strictly below the segment's minimum on that axis. For y (+0x12)
  that is still reachable: 0019ED80's range test compares the rounded
  crossing, and its ring test ignores y, so a segment whose bottom is one
  ulp above a flat node's vertex can be accepted. For x and z (+0x0E,
  +0x16) the crossing lies at least one ulp beyond the node's maximum, and
  0019ED80 compares the largest edge dot of that point with 1e-5. Over
  AREA11's seven attr-0x78 nodes, the smallest dot such a point can have is
  1.074e-5: the in-plane distance times cos of half the exterior angle,
  minimised over every vertex region and edge. That is above 1e-5, so the
  strictness of those two tests cannot change a result on this grid. The
  test recomputes this margin from the captured nodes on every run and
  fails if it drops to 1e-5 or below.

## 2. Route evidence

The census route beats are snapshots taken after 001AAD00's swap, so each
beat's published lists are the lists the passes walked on the frame before.
Read from the captures:

- The class-1 list is empty on every beat. The class-2 list is empty on
  beats 00..12 and holds one entry (class byte 0xAA, type 1) on 13 and 14,
  so 001A7870 and 001AA140 stop at their count test and 001A9F60 skips the
  entry (class 0xA). The class-0xD list holds one type-1 entry (0x7A8540,
  +0x34 = 0x00823580, an AREA11 overlay routine) on beats 10 and 11: that is
  001A8BE0 -> 001A8660, which the census saw from beat 07 on. The player
  was not within its circle at either snapshot, so the behaviour did not run
  there.
- None of the thirteen worker callees (001A8840 .. 001AA000, 0021BD10) and
  neither 001000C0 nor the behaviour 0x00823580 ran anywhere on the census
  route.

## 3. Verification

`python3 tools/test_coll_list_passes_reference.py` (default about 6 s on 8
workers; `EM_TEST_FULL=1` about 2.5 min; `EM_TEST_JOBS=1` serial).

The interpreter is the fall lane's FallEE (COP1 and VU0 macro through
`tools/ee_float_model.py`). The original instructions are read from the
captured RAM and checked against the pinned ELF (the code range
0x100000..0x230000, the two jump tables, D_0024A740 and the atan tables).

- **Walkers**: every case compares the whole scratchpad state the routines
  use (the EmCollProbeState words, 0x70003684, 0019F330's four outputs) and
  the return value, and asserts the original wrote nothing else. Cases: the
  ground query under the hip and the ladder probe at the feet for the route
  rows of every beat's trace; points on the seven attr-0x78 nodes and on the
  0x1E..0x59 nodes; ladder probes on owner cells; the three walkers alone on
  vertical, oblique and normal-directed segments with randomized ranks;
  0019F330 on 0019BC40's line (pos -> pos + (0, 1, 0)) and oblique lines;
  static cells made from the owner-free leading uids (every kind gate, and
  words carrying 0x40000000 or 0x20000000), including segments through each
  static prim with every static cell at one kind: 0x1E, 0x59, and 0x1D or
  0x5A alternating by beat (all of 0x1D, 0x1E, 0x32, 0x59 and 0x5A in the
  full run). Further sweeps set every static word to 0x40000000 (skipped),
  0x20000000 (below) or 0x60000000 at query class 1 (the 0x40000000 test
  comes first, so the run asserts these segments hit exactly as often as in
  the 0x40000000 sweep). Every other prim segment, half the direct
  001A3980 cases and every staged case below start from a stale nonzero
  0x700031D4, so pass 1's store of 0 on a hit (0x1A3CAC) is compared. Also
  covered: the camera-ground adapter, and
  0019F330 without its SDK context (it faults and leaves the state
  untouched).
- **0x20000000 static words** (001A3980 pass 1, 0x1A3A48 and
  0x1A3AA4/0x1A3AAC). The captured AREA11 directory has no word with
  0x80000000, 0x40000000 or 0x20000000, so the test makes them: the random
  static cases pick 0x20000000 for a third of the words, and the direct
  walker runs with the query class 0x7000324E at 0, 1, 0x100 or 0x8000. In
  every run, a sweep sets every static word to 0x80000000 | 0x20000000 at
  kind 0x32 and runs each prim segment twice, with the query class at 0 and
  at 1 or 0x100. For each static hull, 0019BA80 (mask 2) also probes down
  onto the hull top twice: from the player and from a class-4 owner. 0019BA80
  stores the actor's class byte & 0x1F as the query class. The player's class
  byte is 0x20 in every captured beat, so the player's queries run at class 0
  and skip these words, while an owner's (4 or 0x84) run at class 4 and test
  them. A flagged word's hull is read at tbl + (word & 0x3FFFFFFF), which is
  0x20000000 above the table. That is the EE's uncached mirror of main RAM,
  the same bytes as tbl + (word & 0x1FFFFFFF). The interpreter maps addresses
  below 0x40000000 onto main RAM modulo 32 MB, and the native reads
  tbl + (word & 0x1FFFFFFF). The run asserts that flagged hulls were hit with
  a nonzero class, that the class-0 result differed on those same segments,
  and that the owner's ladder probe hit where the player's did not. The
  same probes also run from the player with its class byte patched to 0x30
  and 0x14 (query classes 0x10 and 0x14; bit 4 is inside 0019BA80's 0x1F
  mask), and the run asserts that they hit exactly where the owner does.
- **Staged prim lists** (001A3980's prim walk in both passes). AREA11's
  hulls hold 0x4000 prims without 0x800 (uids 5 and 6, tested by pass 2 on
  captured data, see section 1) but no 0x8000 prim and no round prim with
  0x800. So in every world the test rewrites the prims of static uid 0's
  hull (pass 1) and of the hit owner's hull (pass 2), on both sides, and
  runs a falling and a rising vertical segment through the hull centre:
  - a 0x8000 prim (with and without 0x800) on the segment: pass 2 hits it
    (0x1A3E94), pass 1 steps over it (0x1A3B90) and hits nothing;
  - a 0x4000 prim (with and without 0x800) on the segment: both passes hit;
  - each of the four round-prim headers 100 to the side (missed), followed
    by a 0x4000 prim F on the segment: F is hit, which needs that header's
    size step (0x1A3B90, 0x1A3BC0, 0x1A3EA4, 0x1A3ED4). The 0x800 tail is
    zeros (001A44B0 reads only the first 0x18 bytes), so a wrong size lands
    on a zero header;
  - an unknown type nibble (type 3 in 4 or 0x14 bytes, type 0 in 0x14
    bytes) ahead of F: the walk neither tests nor steps over it (0x1A3B84,
    0x1A3E88), reads the same header again and never reaches F. A step of
    4 or 0x14 bytes would reach F.
  - oblique segments for the hull gate's x / z tests, which read the
    segment start (as does 001A44B0's circle test): start inside the AABB
    and end 5 beyond it in x or z (hit), and start 5 beyond it with the end
    at the centre (gated out).
  The run asserts every expected outcome (hit y and entity), and that each
  of the 30 case / pass combinations ran somewhere.
- **Pass 1's y re-split** (0x1A3CF0 / 0x1A3CF4). Two hulls A and B are
  stacked in y at B's x/z (patched AABBs 2 high, one 0x4000 prim each) and
  a vertical segment 20 long crosses both. A is static uid 0 and is hit
  first; B is static uid 1 (pass 1 again) or the hit owner's hull (pass
  2). Falling, A lies below B; rising, above. After the hit on A the range
  is [start, A's face], which still contains B, and B's face is the final
  hit; with the two range ends exchanged, B would be gated out. A second
  variant keeps B's prim between the start and A but moves B's AABB past
  A's hit: the narrowed range gates B out and A stays the final hit, where
  a range left at the whole segment would test B's prim. The run asserts
  the final hit y and entity for all four (both directions, both
  variants), for B static and B owner.
- **Pass 1 stops at the first unflagged word** (0x1A3A18). Static uid 1
  with a 0x4000 prim on the segment is hit behind a static uid 0 and never
  tested behind an unflagged uid 0 (asserted).
- **Stale query state.** The probe lane's Case stages 0x700031D0 = 0 and
  0x700031D4 = 0, so a store of 0 there is invisible. Every other route row,
  a third of the node-point ground cases, half the random ladder probes, two
  of the three adapter calls and all the boundary cases below start instead
  from a stale pair: 0x700031D0 names a grid node record (the node's
  address) or the cell record D_700030B0, and 0x700031D4 holds a nonzero
  entity (an owner record). Every third route row probes with mask 4 alone,
  so on a miss 0019BA80's 0x19BC08 clear is the only record store.
- **Boundary cases** (in the default run):
  - 0019E280's rank bounds at equality: vertical segments through each
    attr-0x78 ring vertex, and one ulp beside it in x or z, whose top or
    bottom is one ulp above the vertex. The default run spreads the 28
    vertices over the 15 beats, which all share the same grid.
  - 0019BA80's nudge at box.y = +0.0 and -0.0: the point is 0.0003 below a
    mid-attribute floor node, so the other nudge sign would turn the rising
    segment into a falling one through the plane.
  - 0019E930's rank bounds at equality, as for 0019E280: the walker alone
    on vertical segments through the ring vertices of 0x1E..0x59 floor
    nodes (two per beat; every tenth in the full run), one ulp beside them
    in x or z, top or bottom one ulp above the vertex.
  - 0019E930's attribute window: a floor node's +0x1A byte is patched to
    0x1D, 0x1E, 0x59 and 0x5A on both sides (the RAM byte and the native
    grid's attr), then the walker runs alone on a vertical segment through
    it, followed by the ladder probe. The same node at 0x77, 0x78 and 0x79
    takes the ground query (0019E280 accepts 0x78 only; AREA11's grid has
    no attribute above 0x78), and the run asserts only 0x78 hits.
  - 001A3980 pass 2's owner gates. The first class-4 owner whose hull top
    a vertical segment hits is used. It is patched on both sides, and then
    the walker runs alone, followed by the player's ladder probe (mask 2).
    The patches: status 0, 1 and 2 (0x1A3D34 tests only 0; 1 and 2 both
    occur in captured owners, and the run asserts both hit like the
    captured status); class byte 0x14 or 0x24 (& 0x1F,
    0x1A3D48); a uid whose directory word is 0 (0x1A3D8C); a uid equal to
    the count (0x1A3DA0); and a +0xE high byte of 0xFF (0x1A3D70). The last
    runs once with the captured count and once with the count at 256
    (0x7000324C and the directory's count word). Six more vertical segments
    start or end exactly on that hull's top or bottom y: the hull gate
    (0x1A3DB4 onward) and the y re-split after a hit (0x1A3FD8) at
    equality. With the captured count
    (27), the later uid < count test (0x1A3DA0) would also reject uid 255.
    So there the skip is executed and compared, but removing it changes
    nothing. At 256 the skip is the only guard: directory word 255 (table
    +0x400) lies inside another hull's data and is not a hull offset. The
    run asserts that the status, class, uid-count and 0xFF patches each
    change the compared state in every world.
  - 0019F330's flat test at h = 1e-4 exactly (0x19F594: h < 1e-4 takes
    the flat ratio 0x7F7FC99E). A floor node's plane is patched on both
    sides to n = (hx, 1, 0), with d set so the plane passes through the
    node's centre. hx takes the seven floats from 3 ulps below 1e-4 to 3
    ulps above it. The line is 0019BC40's, rising through that centre. The
    run asserts that at least one hx gives 0x70003680 = 1e-4 exactly (one
    per world does).

  The EMCL (with the node class and the rank section) is exported by the
  decomp exporter and verified byte for byte against five captured RAM
  images on every run.
- **Passes**: every case compares every byte of the pool arena, the player
  record, the list arrays, the synthetic records and the globals, the
  scratchpad words 0x70003B86/88 and 0x700038A0..AC, the worker call log
  (ids and argument registers) and the return value, and asserts the
  original wrote nothing else; each world ends with a whole-RAM comparison.
  Cases: each route beat's published lists as the live lists (the nine hooks
  in order, each hook alone, 001A8660 on every class-0xD type-1 entry);
  synthetic lists over the beat's pool records with randomized gating
  bytes, capsules and radii, worker scripts that zero the walk counters (as
  001A9360 and 001A99E0 do). Staged cases run in every world:
  - the 0021BD10 gate and both speed tables of 001A8660, and 001A9000's
    001A8F40 branch and its two status tests (outer and inner entry must
    be 1: outer / inner statuses 1/1, 1/2, 1/0, 1/3, 2/1 and 0/1; the run
    asserts only 1/1 calls the worker);
  - 001A8660's circle test (dist == ra + rb, 0x1A86AC) and height test
    (|gap| == reach from both sides, 0x1A8718) at equality with exact
    values, and just beyond each; the run asserts the behaviour call happens
    exactly at equality;
  - 001A97B0's 50.0 gate, its inner +5 == 9 exclusion (with +0xD 3 and
    without), every inner type 0..21 and its three handlers;
  - 001AA140's status test, and each +0x2D4 bit (0x1, 0x2, 0x4, 0x8, 0x10,
    0x100, 0x108);
  - 001A8BE0's walk ending after 001A8660 clears 0x70003B86: an overlapping
    type-1 entry followed by an active type-1, type-3 or type-5 entry;
  - 001A7870 at an overlap of exactly 0 on an inner +0xB0 of -0.0, on both
    push paths;
  - 001A7870's capsule y-extent tests at equality. The outer capsule spans
    y -0.5..2.5. The inner capsule has the same half length, and its bottom
    equals the outer top (0x1A7A34) or its top equals the outer bottom
    (0x1A7A44), with every sum exact. The inner capsule is 0.5 away in x,
    so the capsules overlap. The run also stages the inner capsule one ulp
    further out on each side. It asserts that both equality cases push and
    that neither of the one-ulp cases does. On the pushing pair it then
    stages pass 1's tag test (a halfword -2: tags 0x00FE and 0x01FE on the
    inner record, 0x00FE on the outer, mark nothing) and the inner +0x52
    test (bit 0 alone: 2 pushes, 1 and 3 do not), all asserted;
  - 001AAD00's hook order: an entry in the class-2 list (pushed +1.5 in x
    by 001A7870) and in the class-0xD list (type 1), 6 from the player
    before the push and 4.5 after, with radii summing to 5. The whole hook
    sequence runs, and the run asserts the behaviour call happens, which
    needs 001A7870 to run before 001A8BE0;
  - 001A9B10's +0x2D4 >> 8 window;
  - the class-byte masks of 001A9F60, 001AA140 and 001A97B0 (class bytes
    0x12, 0x1A, 0x22, 0x2A, 0x42, 0x4A and 0xE2).

  Fail-stop checks: an unbound worker faults before any write. An
  unported binding (`em_coll_list_passes_unported` and its 001AA000,
  0021BD10 and behaviour variants) faults with the call site's address:
  0x1A8C94, 0x1AA23C, 0x1A875C and 0x1A8734.
  The run asserts that all fourteen worker kinds were reached, that
  001A8660 overlapped and knocked back, and that both 001A7870 push paths
  ran.

Results (2026-09-23, third fix round):

- Default (about 5.5 to 7 s wall on 8 workers): 30 route rows, 4,685 walker cases and 1,902
  list-pass runs, PASS. The walker cases include 168 0019E280 and 702
  0019E930 rank-edge segments, 45 attribute 0x77 / 0x78 / 0x79 ground
  queries (hits 0 / 15 / 0), 60 box.y = +-0.0 probes and 120 attr-edge
  cases. 001A3980: 60 hits through 0x20000000 words at a nonzero query
  class, all 60 gated at class 0; 15 owner and 30 patched-class player
  ladder hits through them; the owner-gate patches changed the state in all
  15 + 45 cases; 30 AABB-edge hits; 846 staged prim-list cases, all with
  the expected outcome (0 pass-1 hits on an on-segment 0x8000 prim, 30 on
  a 0x4000 prim, 30 pass-2 hits on a 0x8000 prim); 180 stacked-hull
  re-split cases (60 of them owner / owner); 30 pass-1 stop cases; one
  exact h = 1e-4 plane per world.
- `EM_TEST_FULL=1` (2 min 17 s): all 12,439 route rows, 47,456 walker cases
  and 4,142 list-pass runs, PASS. Walker results:
  - ground hits/misses 3,757/12,522;
  - ladder results 0: 16,664, 2: 42, 4: 1,673;
  - 0019F330 crossings/rejects 511/389;
  - 3,292 static-cell cases, 365 hits;
  - 180 hits through 0x20000000 words, all gated at class 0; 90 patched
    player-class hits;
  - 0019E280 rank-edge segments hits/misses 855/1,665; 0019E930 525/1,095;
  - attribute 0x77 / 0x78 / 0x79 ground hits 0 / 630 / 0;
  - 2,520 box.y = +-0.0 probes;
  - attr-edge hits/misses 3,360/1,680.

  The list passes made 5,429 worker calls, with 268 001A8660 overlaps and
  151 knock-backs. 001A7870 pushed 322 times, 142 of them on the
  |len| < 0.001 path. The x / z max-bound margin was 1.074e-5, above 1e-5.
- Mutation check (independent pass, this round). The set was written anew
  for this round: 117 one-line mutants over the two translation files,
  covering every gate, store, size step and loop of the fourteen routines,
  and including every mutant named in the last review. Each was applied
  alone to a scratch copy of src/ and tools/ and run through the default
  mode.
  - The first pass (113 mutants, 3 of which did not compile under -Werror
    and were rewritten) left 17 survivors. Six were real gaps and got
    staged cases: 0019E280's attribute test (the 0x79 node), 0019E930's
    +0x0C rank bound at equality (its rank-edge segments), the hull gate's
    z test on the segment start (the oblique segments), the y re-split
    removed in pass 1 and in pass 2 (the 'beyond' variant, with an owner /
    owner pair for pass 2), and 001AAD00's hook order.
  - The second pass (117 mutants; 0019E930's +0x10 / +0x14 bounds and the
    hull gate's x test added) left 14. Two of them (the x / z tests read
    from the segment end) survived because the oblique segments only left
    the AABB past its maximum; they now leave it on both sides, and both
    are caught. So is the pass-2 re-split removal (the owner / owner pair).
  - That leaves 11 survivors of 117 in the default run. All 11 also survive
    `EM_TEST_FULL=1`:
    - 0019E280's +0x0E and +0x16 bounds made strict (w7 / w11). These
      cannot change a result on this grid: the margin argument in
      section 1, which the test asserts.
    - 0019E280's initial best span (the node count) lowered by one. This
      matters only when the narrowest of the six direction spans holds at
      least count - 1 of AREA11's 3,099 nodes, which no query reaches
      (not exercised).
    - 0019E280's span choice on a tie (< made <=). Two equally narrow
      direction spans then pick different candidate lists; the result
      changes only if the two lists order two accepted nodes differently.
      Not exercised.
    - 0019E280's final point restore removed. Equivalent: 0019ED80 writes
      0x700031B0 only when it accepts a node, so the point already holds
      the last accepted crossing.
    - 0019E930's y mask 0x37 widened to 0x3F. Equivalent: the extra bit
      recomputes rank 3 from the start, which the next call (mask 8 on the
      end) overwrites.
    - 0019E930's -1 test removed. It cannot fire.
    - 001A3980 pass 2's zero-word test (0x1A3D8C) removed. This is
      equivalent on AREA11's directory. A zero word makes the hull the
      directory header itself. Its "AABB" is the count and the first
      offsets, read as denormal floats, and its "prim count" (table +0x18)
      is 3,728. The first "prim" header (table +0x1C, uid 6's offset 0xEC4)
      has type nibble 0, so it is neither tested nor stepped over, and
      nothing can hit.
    - 001A3980 pass 1's re-split test at start.y == end.y (<= made <).
      Equivalent as far as the prim tests go: a horizontal segment's
      crossing has the segment's own y, so both branches store the value
      that both range ends already hold.
    - 0019F330's ring-edge test at dot == 1e-5 exactly (0x19F510). This is
      not exercised: the ring's edge normals and vertices are not patched.
      Unlike the flat limit, no single scalar moves the dot onto 1e-5.
    - 001A7870's tiny test as <= 0 instead of < 0 (0x1A7AC0). This is
      equivalent: len is a float widened exactly to double, and 0.001 as a
      double (0x3F50624DD2F1A9FC) is not a float value, so the soft-float
      compare never returns 0.

  Every mutant the last review named is caught by the default run: pass
  2's 0x8000 call removed, the 0x4000 and 0x8000 sizes fixed at either
  value, pass 1's re-split swapped and removed, pass 2's re-split removed,
  owner status != 1, 001A9000's inner status == 0, 001A7870's low-byte tag
  and +0x52 & 3, the 0x40000000 / 0x20000000 gate order, an unknown type
  nibble stepped over (by 4 or 0x14), pass 1's 0x700031D4 store removed,
  and 0019BA80's class mask narrowed to 0x0F. So are the two NEARMISS-C
  readings (pass 1 continuing past an unflagged word, a containment hull
  gate). The previous round's '7 of 62' reflected only that round's own
  set; the review's independent set found more, and this round's set
  found five further gaps outside the review's list (0019E280's attribute
  test, 0019E930's +0x0C bound, the gate's x and z start reads, the hook
  order). The re-split removals are not equivalent in general: the range
  only prefilters hulls by their AABBs, so removing it changes a result
  when a hull's prim lies outside its own AABB (as the 'beyond' variant
  stages). Whether AREA11's captured hulls ever do that was not checked.
- `python3 tools/check_no_disassembly.py` on every new file: clean.
- The five new sources, with em_coll_probe_original.c, em_actor_collision.c,
  em_sdk_soft_float.c and em_effect_original.c added, build into the game
  binary with the game's flags without a warning (private lane build).

## 4. Binding notes (for the coordinator)

Nothing is bound yet. What each translation replaces and what it needs:

1. **0019B7D0 / 0019E280** replace the `ground_only` branch of
   `interaction_camera_query` in `em_camera.c`: it tests every attr-0x78 grid
   poly without the rank gates and returns the first hit, while the original
   takes the last accepted node of one rank span. Bind
   `em_coll_list_passes_camera_ground` (context `EmCollListPassesGround`: the
   world's `EmCollProbeGrid` and its one `EmCollProbeState`) as
   `EmCameraFollowWorkers.ground` (em_camera_follow_original, same
   signature, exercised by the oracle) and, for the legacy camera, call
   `em_coll_list_passes_0019B7D0` there. Needs em_coll_probe_original.c.
2. **0019BA80 / 001A3980 / 0019E930**: the only caller is 00176F90 (the
   ladder attribute refresh, `em_player_ladder_entry`, not live). Its worker
   `EmPlayerLadderWorkers.probe_0019BA80` needs an adapter that runs
   `em_coll_list_passes_0019BA80(world, state, actor +0x14, actor +2, point,
   box, mask)` and fills `EmPlayerLadderScratch`: s31B0 = state.point, record
   = CELL / OTHER / NONE from state.record, s31D8 = the result, entity and
   entity_0E from state.entity. The scratch's `record_bytes` (the whole
   64-byte record 0x700031D0 names) cannot be filled from EmCollProbeState and
   EmCollProbeGrid alone (the grid node's +0x18..+0x23 and +0x2C..+0x3F and
   the cell record's other words are not carried), so that adapter must fault
   on a read of a missing field rather than invent one. The live Use/ladder
   code today uses `em_collision_segment_query`, which knows no 0x1E..0x59
   attribute walk.
3. **0019F330** replaces `column_node` in `em_collision.c` (called by
   `em_collision_column_finish`, which `em_actor_collision_column_0019BC40`
   uses): call `em_coll_list_passes_0019F330(grid, math, state,
   &face.cross[0], pos, pos + (0, 1, 0) (EE add), node, q)`. It needs the
   `EmSdkMathContext` (0011E748, 0011DBB8) and the one scratchpad state
   (0x70003680 is `state->ratio`, 0x70003684 is
   `EmCollSegmentFaceScratch.cross[0]`). The pass-2 node walk around it
   (0019BC40's rank span and gates) is still the inexact walk of
   em_collision.c; that belongs to the 0019BC40 row, not this lane.
4. **The nine hooks of 001AAD00**: `em_scene_bindings.c` reports 001AAD00 as
   `UM_001AAD00` (unmirrored) and `em_render_frame.c` runs nothing. Bind
   `em_coll_list_passes_001AAD00_hooks(passes, 0x008102B0)` at the start of
   the close-out (`w_001AAD00` in em_scene_bindings.c), followed by the list
   swap `em_actor_class_lists_swap_001AAD00`. This replaces the em_enemy.c
   legacy pair/contact passes the census names for 001A8BE0 and 001A9000.
   It needs:
   - `EmCollListMemory.bytes` over the original-layout bytes of the pool
     records, the player record and the records their +0x30 / +0x58 /
     +0x110 words point to. The live pool keeps native `EmActor` structs
     that do not carry +0x30, +0x34, +0x38, +0x50, +0x52, +0x58, +0x5E,
     +0x110.., +0x224, +0x22C or +0x2D4, so the binder must supply byte
     images for them; a range it cannot give faults.
   - `EmCollListGlobals` with the live list cursors/counts. The live lists
     (`EmActorClassLists`) push slot i at base - 4(i + 1), so the cursor is
     base - 4 * live and entry j is slot[live - 1 - j]; the list arrays must
     be readable through `bytes` at those addresses.
   - `EmCollListData.d24A740`: 0x440 bytes from D_0024A740 of the user's ELF.
   - `EmCollListPasses.math`: an `EmSdkMathContext` (em_sdk_math_original).
   - Workers: `normalize = em_coll_list_passes_normalize`
     (em_effect_original_00102760); the behaviour call at the entry's +0x34
     (for AREA11's 0x00823580, an overlay routine with no translation yet,
     so `em_coll_list_passes_unported_behaviour` until it has one);
     `em_coll_list_passes_unported` for the pair workers 001A8840,
     001A8970, 001A8CE0, 001A8E80, 001A8F40, 001A9360, 001A96F0, 001A9480,
     001A99E0, 001A9C40 and 001A9E00; `em_coll_list_passes_unported_001AA000`
     and `em_coll_list_passes_unported_0021BD10` for those two. Keep each
     binding until its callee is translated. None of these callees ran on
     the census route, so the route never reaches the fault. A reached one
     returns -1, and `passes->fault` holds the original address of the call
     site.
5. **One storage for 0x70003B86 / 0x70003B88.** `EmCollProbeState.span_lo`
   / `span_hi` (the walkers) and `EmCollListGlobals.s3B86` / `s3B88` (the
   list passes) are the same two scratchpad words. In the original, the
   values one routine leaves there are what the next reader finds. Within
   this lane, keeping them separate changes no result: every list pass
   seeds its counters before reading them, and 0019E280 / 0019E930 write
   both words before reading them. The words are still observable state,
   because the last writer's values survive into later code and into any
   comparison against a capture. So the coordinator must back both with
   one storage: copy the frame's one state's span_lo / span_hi into
   `globals.s3B86` / `s3B88` before `em_coll_list_passes_001AAD00_hooks`
   and copy them back after it (or keep the words in one place both views
   read). The oracle already models one storage: the globals start from
   the scratchpad words and are compared against them after every run.

## 5. Limits

- Nothing in this lane is live. Items 2 and 4 above need data the live port
  does not keep yet (grid record bytes; original-layout actor records), and
  item 4 needs the behaviour 0x00823580.
- Thirteen callees of the passes and the +0x34 behaviour are not translated.
  They are hooked in the oracle and bound as fail-stop workers; the census
  shows none of them on the route.
- The route evidence for the passes is the snapshot lists, not the
  frame-by-frame lists. The census shows every pass running every frame from
  S2 on, but it does not record which branches ran.
- 0019F330 is verified alone. Its caller's node walk (0019BC40 pass 2) is not
  part of this lane.
- 0019E280's x and z maximum rank bounds (+0x0E, +0x16) at equality are
  not exercised because no input can reach them on AREA11's grid (see
  "0019E280's rank bounds at equality"). A strict-inequality mutant of
  either test survives both modes. The test asserts the margin that makes
  this so.
- 0019E930's -1 test on rank 2 (0x19EB94) cannot fire. It is translated as
  written, and its removal survives both modes.
- Several 001A3980 paths run only on synthetic data. The census route
  never reaches them; the test makes each by patching RAM on both sides, and
  the oracle compares them:
  - static cells at all (AREA11's captured directory has no word with
    0x80000000), so every pass-1 path, including 0x40000000 / 0x20000000 /
    0x60000000 words, the stop at an unflagged word and pass 1's y re-split;
  - 0x8000 prims, and round prims with 0x800 (no captured hull holds
    either), in both passes, and unknown type nibbles;
  - owner patches: status 0, uid 0xFF (with the count at 27 and 256), a
    zero-word uid and uid == count;
  - hulls stacked in y, or with a prim outside their AABB (the re-split
    cases).
  Captured data does reach pass 2's 0x4000 prims (uids 5 and 6, beats 00,
  03 and 13; section 1). On the route, the player's 0019BA80 probes run at
  query class 0.
- Eleven mutants survive both modes (the mutation check in section 3):
  - equivalent here or in general: 0019E280's final point restore, 0019E930's
    mask 0x3F, 001A3980's zero-word test on this directory, its pass-1
    re-split test at start.y == end.y, and 001A7870's soft-float tiny test
    at equality;
  - unreachable on this grid or dead: 0019E280's +0x0E / +0x16 bounds made
    strict (the asserted margin) and 0019E930's -1 test;
  - not exercised: 0019E280's initial best span and its tie-break between
    equally narrow spans, and 0019F330's ring-edge test at dot == 1e-5.
