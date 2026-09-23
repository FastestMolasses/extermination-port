# Floor service surface and object probes (0019B6C0, 0019B8C0)

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

`src/game/em_coll_probe_original.c` translates the two probes the floor
service 00175900 calls after its ground probe. It also translates everything
they call:

| Original | What it does |
|---|---|
| 0019B6C0(top, bottom) | The surface record. 00175900 runs it from 18 above the feet (D_700038A0) down to the feet. It calls 001A2AE0 (cells), then 0019DF10 (grid). |
| 0019B8C0(actor, at, probe, mask) | The object probe: 00175900 runs it in contact without a surface record, with mask 7. It builds a vertical segment and calls 001A32C0 (cells, mask 2), then 0019E640 (grid, mask 4). |
| 001A2AE0 / 001A32C0 | The cell walkers. Pass 1 covers the static cells of *0x70003250; pass 2 covers the published class-4 owners (D_00275B7C). |
| 0019DF10 / 0019E640 | The grid walkers. Each picks a span of one rank table, tests each node's rank bounds, then calls 0019ED80. |
| 0019F1A0(point, mask) | A point's rank in each of the six sorted node tables. |
| 0019ED80(segment, node) | The grid node segment test. |
| 001A4030, 001A4650, 001A44B0 | The cell prim tests the walkers call: the n-gon, the 0x2000 face, and the 0x4000/0x8000 round prim. |

Every routine is a NEARMISS or an asm-word file in the decomp, except
001A4030, whose C is byte-matched. The translation follows the .s, and each
line cites the address it translates.

The two surface-walker pass-2 tests, 001A50A0 and 001A5C30, are workers. No
AREA11 owner reaches them.

All EE COP1 and VU0 macro arithmetic goes through `em_ee_float.h`.

## 1. What the originals do

**0019B6C0 (surface record).**
- Stores `top` in 0x70003190 and `bottom` in 0x700031A0, with w = 1.0 in both.
- Clears 0x70003254 (no self) and 0x700031D4.
- Runs 001A2AE0, then 0019DF10, which is nearer because each hit clamps the
  segment end. The result is 2 for a cell hit and 4 for a grid hit.
- On a hit, restores the end and leaves 0x700031C0 unwritten. With no hit,
  sets 0x700031D0 = 0.
- 00175900 reads three things from it:
  - the return value (+B);
  - `*(0x700031D0) + 0x1A` as +23A;
  - 0x700031B4 as +250.

**0019B8C0 (object probe).**
- The segment runs from (at.x, at.y − probe.y ± 0.001, at.z) to `at`. The
  nudge is +0.001 when probe.y < 0 (0x19B93C).
- Sets w = 0 and 0x7000324E = actor +2 & 0x1F.
- With mask 2, sets 0x70003254 = actor +0x14 and runs 001A32C0. With mask 4,
  runs 0019E640.
- Both walkers return 0 on a hit. The result is 2 for a cell hit and 4 for a
  grid hit, which wins.
- Afterwards it takes the nudge back off start.y. On a hit, it restores the
  end and writes 0x700031C0 = point − at.

**The cell walkers.**
- 001A2AE0 clamps the query box on all three axes. 001A32C0 clamps y only
  and tests x/z at the start point.
- **Pass 1** stops at the first directory word without bit 31. It then
  skips:
  - words with 0x40000000;
  - words with 0x20000000 while 0x7000324E == 0;
  - entries whose kind byte D_0024D7C0[area][sub][i].+8 fails the gate.
    001A2AE0 keeps kinds of 0x5A and above; 001A32C0 keeps kinds below 0x1E.
- **Pass 2** walks the published class-4 list. It skips:
  - status 0;
  - class ≠ 4;
  - the self owner;
  - uid 0xFF;
  - word 0;
  - uid ≥ count;
  - the kind (+0x54) gate. 001A2AE0 tests it before the box, 001A32C0
    after.
- **Prim dispatch.**

  | Walker, pass | 0x1000 | 0x2000 | 0x4000 | 0x8000 |
  |---|---|---|---|---|
  | 001A2AE0 pass 1 | 001A4030 | 001A4650 | 001A44B0 | no call |
  | 001A2AE0 pass 2 | 001A4030 | 001A50A0 (worker) | 001A5C30 (worker) | no call |
  | 001A32C0 pass 1 | 001A4030 | 001A4650 | 001A44B0 | no call |
  | 001A32C0 pass 2 | 001A4030 | 001A4650 | 001A44B0 | 001A44B0 |

  An unknown type nibble neither calls nor advances.
- **On a hit.** The walker clamps the end (001A2AE0 pass 2 clamps all three
  axes; the others clamp y only). It stores the owner, or 0, in 0x700031D4,
  and ORs the kind into the low byte of 0x700030CA. The high byte is the
  class the prim test wrote.

**The grid walkers.**
- **0019DF10** ranks the start and the end twice, with swapped direction
  masks. The first pass indexes the span helper tables *(0x70003228 + 4i); the
  second gives the ranks.
- **0019E640** ranks the higher end once, with mask 0x33. It returns
  "clear" when 0x70003244 is −1.
- **Span pick.** Over directions 0, 1, 4 and 5, both walkers pick the
  narrowest span. They leave 0x70003B86/88 behind.
- **Walk.** Each node of the span passes when its rank bounds (+0x0C, +0x0E,
  +0x14, +0x16) admit the point ranks. Then the attribute gate applies:
  - 0019DF10 keeps 0x5A..0x77, the surface nodes;
  - 0019E640 keeps attributes below 0x1E. It leaves the attribute in
    0x70003B88.
- **0019ED80** clamps the whole end (0019DF10) or y only (0019E640), and
  names the node in 0x700031D0.

## 2. Corrections to the readable C (NEARMISS files)

- **001A2AE0.** The C has both kind gates inverted: the original keeps
  kinds of 0x5A and above. The C also sends 0x8000 prims to 001A44B0 in
  pass 1, where the original makes no call. Pass 2 calls 001A50A0/001A5C30
  for 0x2000/0x4000, and never calls anything for 0x8000.
- **0019DF10.** The C's first 0019F1A0 pair has its masks swapped. The .s
  ranks the start with s6 and the end with s5 (0x19DFA4/0x19DFB4). The
  reference test's mutant with the C's masks fails.
- **0019DF10 and 0019E640.** When no direction beats the node count, the
  span registers are uninitialized. The native code faults there. In
  0019E640 the fault comes only after the 0x70003244 check.

## 3. Grid data: the EMCL node class and rank section

The grid walkers need data that the EMCL poly records did not carry. The
decomp exporter `../Extermination/tools/export_collision.py` writes it when
given `--node-class` (off by default, so a default re-export stays
byte-identical to the installed asset until the coordinator binds the
original floor service). The change is limited to this data:

- **Node class (header flag bit 1, `EM_COLL_FLAG_NODE_CLASS`).** Each grid
  poly's pad byte is node +0x1B. `em_collision.c` already reports it as the
  class of a grid hit.
- **Rank section (header flag bit 2, `EM_COLL_PROBE_FLAG_RANKS`).** It is
  appended after the edge normals, and every field keeps the level's own RAM
  layout:
  - "EMRK", version 1;
  - N, the index of the first grid poly (grid node i is poly first + i),
    and V;
  - the raw grid vertex pool (V × 12 bytes). The EMCL pool is de-duplicated,
    so 0019F1A0 cannot use it;
  - node +0x00..+0x17 (12 halfwords per node): the six boundary vertex
    indices and the six rank bounds;
  - the 12 tables at header +0x18:
    - 0..5 are *(0x70003210 + 4k), each a permutation of the nodes;
    - 6..11 are *(0x70003228 + 4k), holding table positions 0..N.

`em_collision_load` ignores the trailing section. `em_coll_probe_grid_load`
reads it and validates every index against its table.

**RAM verification.** `--verify-ram EEMEMORY` compares the export byte for
byte with captured RAM:
- D_0028A598 entry 0 names the same grid header;
- node bytes +0x00..+0x33 of all 3,099 nodes;
- the 3,503 grid vertices;
- the 10,812 edge normals and the 10,812 indices;
- all 12 × 3,099 table entries;
- with the beat's scratchpad.bin, the staged pointers 0x700031FC..0x7000323C
  and 0x7000320C.

Verified images: route beats 05, 06, 08 and 14, and playable_ee.bin. The
exporter also checks that the EMCL ring vertices are bit-identical to the
grid pool (the pool de-duplication could otherwise merge −0 and +0).

The regenerated file differs from the current asset in three places:
- the flags word (1 → 7);
- the 3,099 grid pad bytes;
- a 190,808-byte tail.

Every other byte is identical.

**Not installed.** assets/scene_snow/snow.emcl still has flags 1. The authored
class differs from the normal-derived class on 317 of the 3,099 grid nodes:

| Derived class | Authored class | Nodes |
|---|---|---|
| slope | wall 0x2000 | 87 |
| slope | floor 0x4000 | 155 |
| floor | wall | 9 |
| wall | floor | 33 |
| wall | slope | 10 |
| wall | ceiling | 2 |
| down-slope | ceiling | 21 |

The legacy port paths that read `EmCollHit.surf_class` from grid hits (the
camera, em_game.c, em_player.c's FLOOR/SLOPE tests) will change behaviour.
Installing is therefore the lead's step:

```
cd ../Extermination && python3 tools/export_collision.py \
  extract/chunk15/f07_id52.bin extract/chunk15/f08_id4d.bin extract/chunk15/f09_id53.bin \
  extract/chunk15/f10_id5b.bin extract/chunk15/f11_id4a.bin extract/chunk15/f12_id44.bin \
  -o ../extermination-port/assets/scene_snow/snow.emcl --at 218.592,201.789 \
  --node-class --verify-ram build/s87/route/06_hill_slide/eeMemory.bin
```

After installing, re-run:
- `EM_STARTUP_TEST=newgame-control` (displacement 9.599989);
- the level smoke;
- the camera tests;
- test_actor_collision_reference.py, which compares classes when the flag is
  set.

The reference test then requires the installed file to be byte-identical to
its own fresh export.

## 4. Binding (coordinator)

The world and the persistent state:

- `EmCollProbeGrid grid`: `em_coll_probe_grid_load(&grid, emcl, path)`, over
  the EMCL the actor-collision world's `grid` points at. Pass the same path.
- `EmCollProbeWorld world = { &acw, &grid }`, where `acw` is the
  `EmActorCollisionWorld` the ground worker uses (docs/ACTOR_COLLISION.md
  section 7 item 4).
- `EmCollProbeState state`: one state, zeroed at area load and passed to
  every probe, as the original keeps one scratchpad.
  - Two persistent words can change a result: 0x70003244 (`rank[2]`),
    for which 0019E640 returns "clear" when it is −1; and 0x7000324E
    (`query_class`), which 001A2AE0 pass 1 reads at 0x1A2C18 for
    0x20000000-flagged static cells. 0019B6C0 never writes 0x7000324E; in
    the original it holds the class of the last 0019AB20/0019B8C0 caller of
    any actor, in the native only the last native 0019B8C0's (or 0).
  - 0019F1A0 is its only writer and never stores a negative value. Every
    captured AREA11 scratchpad holds 2947..3098 there.
  - So zero, which is not −1, gives the original's behaviour.

The floor service's two workers (`EmPlayerStatesBinding`, em_player.h):

- `b.head = em_coll_probe_player_head` (0019B6C0).
- `b.object = em_coll_probe_player_object` (0019B8C0).
- `b.probe_context = &probe`, where `EmCollProbePlayer probe` is:

  ```
  { &world, &workers or NULL, &state,
    { self = the player's identity (never a class-4 owner), cls = player +2 byte } }
  ```

  In AREA11 the player's +2 byte is 0x20, so cls & 0x1F = 0.
- The adapters fill `EmPlayerProbeHit`:
  - `kind`;
  - `node`: the record's +0x1A halfword. For a cell hit that is 0x700030CA;
    for a grid node it is attr | class << 8. 00175900 reads its low byte;
  - `point`: 0x700031B0;
  - `normal`: record +0x24;
  - `owner`, `entity`, `entity_flags` and `entity_type`: 0x700031D4 and its
    +2/+3;
  - `delta` (object probe only): 0x700031C0.

  0019B6C0 does not write 0x700031C0, so the head adapter leaves `delta`
  zero. 00175900 does not read it after that probe. `axis` stays zero,
  because grid node +0x34 is not in the EMCL. Only 00175CF0 reads the axis,
  and only for ground hits on surface 0x35.
- **Workers.** `EmCollProbeWorkers` takes 001A50A0 (`face_segment`) and
  001A5C30 (`round_segment`). With NULL workers, a probe that reaches one of
  them faults with −1 and leaves the state untouched. No AREA11 owner has a
  +0x54 kind of 0x5A or above, so the live AREA11 world never reaches them.
- This clears `EM_PLAYER_NEED_HEAD` and `EM_PLAYER_NEED_OBJECT`.
  `EM_PLAYER_NEED_NODE_CLASS` clears once the installed EMCL carries flag 2.
- The binding still needs these corrections, which are not this lane's
  files:
  - FIRST_CONTROL.md "Missing today" still lists 0019B6C0/0019B8C0 and the
    EMCL node class as missing;
  - PLAYER_FLOOR.md and ACTOR_COLLISION.md (section 5, "The EMCL has no node
    class") say the same.

**Faults.** Each of these makes a probe fault with −1 and leave the caller's
state as it was:
- a missing worker;
- a static cell without the D_0024D7C0 kind view (`acw.static_kind`);
- a pass-2 owner whose directory word has bit 31. The original adds that
  word unmasked, which names no image byte;
- an index outside a table;
- the uninitialized span above.

Pass 1's 0x20000000 words are read through the EE's uncached main-RAM
mirror, so they give the same bytes as the masked offset. The test's
static-cell cases exercise this path.

## 5. Verification

`tools/test_coll_probe_reference.py` (`make test-coll-probe-reference`).

**The oracle.**
- The shared EE core (`test_player_slide_reference.EE`) is wrapped so that
  every COP1 and VU0 macro instruction goes through `tools/ee_float_model.py`.
  The shared file is not edited.
- It executes the original instructions from captured AREA11 RAM: every
  routine above, the SDK vector routines 00102738, 001028B8, 001028D0 and
  00103230, and, for one check, 0019AB20.
- Every code range is checked against the pinned ELF.
- 001A50A0 and 001A5C30 are hooked on both sides: a scripted result is
  recorded and compared call by call.

**What each case compares.**
- The return value.
- The whole scratchpad state:
  - 0x70003190..0x700031D8;
  - the cell record D_700030B0 +0x1A and +0x24;
  - 0x70003680, 0x7000324E and 0x70003254;
  - the six ranks at 0x70003240;
  - 0x70003B86/88.
- Every byte the original stores must lie in that set. Any other RAM or
  scratchpad write fails the case.

**Cases (quick run about 5-8 s on 8 workers).**
- **Exporter.** Re-run with `--verify-ram` on 5 RAM images.
- **Route.** 64 of 12,439 rows across the 15 beats, one per distinct
  player-state value per beat. Both probes run at each row's feet (+A0),
  with the floor service's arguments (+18; feet − 0.2, probe (0, −13.8, 0),
  mask 7). `EM_TEST_FULL=1` runs all 12,439 rows.
- **Capture.** For each of the 15 beat snapshots (the last stage was idle),
  the original probes at the snapshot feet (+A0) reproduce the captured
  player bytes:
  - +250 (the surface record's point y, or the feet y on a miss) agrees on
    all 15;
  - +23A (the object probe's byte in contact; no snapshot stands in a
    surface record) agrees on the 13 beats in contact. Beats 09 and 14 end
    without contact (+A = 0), so their +23A is not checkable.

- **Adapters.** The `EmPlayerProbeHit` fields equal the bytes 00175900 reads
  through the original's own record pointer.
- **Surface and low-attr nodes.** 120 points on 0x5A..0x77 and < 0x1E nodes,
  under random persistent state. They include 11 cases with 0x70003244 = −1.
- **Units.** 180 each of 0019F1A0 (random and exact-vertex keys), 0019ED80
  (oblique segments), the prim tests and the four walkers, on staged
  segments:
  - the prim tests run over the directory's own 0x2000 and 0x1800 prims,
    plus synthetic narrow and wide 0x4000/0x8000 prims;
  - every prim type is hit.
- **Synthetic worlds.** 80 cases on RAM patched on both sides:
  - static cells, some 0x40000000-disabled and some 0x20000000-gated;
  - kind views and owner kinds around both gates;
  - round prims written into owner hulls;
  - grid attributes under the point;
  - scripted 001A50A0/001A5C30 hits and misses;
  - the self skip.
- **Gate boundaries** (deterministic):
  - grid attributes 0x1D, 0x1E, 0x59, 0x5A, 0x77 and 0x78;
  - static flags 0x80000000, 0xA0000000 and 0xC0000000 × query class 0/2 ×
    kinds 0x1D, 0x1E, 0x59 and 0x5A;
  - owner kinds 0x04..0x78.
- **Fail-stop.** A missing 001A50A0 worker, and a static cell without its
  kind view: −1, with the state unchanged.
- **Beat 06, slide class.** On all 99 sliding rows the original 0019AB20
  hits a grid node whose RAM class byte equals the EMCL's. 56 of them are on
  0x10 (0x1000) nodes. The slide-entry row (counter 7008) stands on a 0x10
  node.

**Mutants.** Each of the 16 mutants below fails the quick run:
- the readable C's 0019DF10 masks;
- the readable C's inverted 001A2AE0 gate;
- each attribute and kind gate off by one (0x1E, 0x5A, 0x78, static and
  owner);
- dropping the 0x70003244 sentinel;
- one ulp in 0019ED80's t;
- dropping 0019B8C0's start.y restore;
- swapping the span pick's odd/even rule;
- 001A2AE0 pass 2 clamping only y;
- ignoring the 0x20000000 gate;
- dropping 001A32C0's pass-2 0x8000 call;
- the 001A4650 face-3 class.

Two candidate mutants were found to be equivalent to the original, not test
gaps:
- `<=` versus `<` in 0019F1A0's final compare: the search keeps
  v(lo) < key, or lo = 0, and both give rank 0;
- host division versus em_ee_div: not distinguished by reachable inputs
  (both round to nearest, and no captured world gives a denormal or
  overflowing operand, where the EE model's DAZ and saturation differ).
  This is not equivalence in general; the code uses em_ee_div.

`EM_TEST_FULL=1` runs:
- every route row;
- 800 node points;
- 3,000 × 4 unit cases;
- 800 synthetic cases.

It passed on 2026-09-23 (171 s on a loaded machine). All 12,439 route rows
were identical on both probes:

| Probe | Miss | Cell hit | Grid hit |
|---|---|---|---|
| Surface record | 12,158 | 0 | 281 |
| Object probe | 1,761 | 724 | 9,954 |

The object probe's cell hits were on crates 0x7A7980 and 0x7A7C70 and on
the elevator 0x7AA880 (uid 4, kind 3, beats 02 and 04). The run also
included 1,744 0019ED80 accepts, 182 round-prim hulls and 273 scripted
worker calls.

A one-off UBSan build of the bridge (`-fno-sanitize-recover=all`) passes the
quick run.

## 6. Limits

- **Not in AREA11, so reached only through synthetic worlds.** Static cells
  (the AREA11 directory's word 0 has no bit 31) and round prims. Owner kinds
  of 0x5A and above never occur. The prim tests and walkers for these paths
  are still translated and verified on synthetic RAM.
- **Surface-walker pass-2 tests.** 001A50A0 and 001A5C30 are not
  translated. They are workers, faulting when unbound.
- **The -1 sentinel.** The native code cannot know the original's stale
  0x70003244 across other walkers' queries (0019C830 and others also rank
  direction 2). Its only effect is the −1 test, and no writer stores −1.
- **0x7000324E across actors.** The native state cannot track the class
  another actor's 0019AB20/0019B8C0 left there. It matters only for static
  0x20000000 cells, which AREA11 lacks.
- **Grid node +0x34..+0x3F.** These bytes are not exported. No routine here
  reads them.
- **Other grid queries** in em_collision.c and em_actor_collision.c
  (0019C830, 0019BC40 pass 2) still brute-force the node list. They can use
  `em_coll_probe_0019F1A0` and the rank view to become exact. That is their
  owners' change.
