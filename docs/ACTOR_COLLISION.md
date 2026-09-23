# Actor collision cells: publication and query (lane "actor-collision")

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Module: `src/game/em_actor_collision.{h,c}`, with query support added to
`src/game/em_collision.{h,c}`. Oracle: `tools/test_actor_collision_reference.py`.
Unit fixture: `tests/actor_collision_test.c`. Nothing here is wired into the live
game yet. Section 7 says how the coordinator binds it.

## 1. What the original does

**Cells.** An area's collision cells live in one directory. The pointer is at
spad `0x70003250` and the count at `0x7000324C`. The layout is:

- a count word at `+0`;
- one offset word per uid at `+4 + 4*uid`;
- the hulls. Each hull is a min/max AABB (`+0..+0x17`), a prim count (s16,
  `+0x18`) and then the prims from `+0x1C`.

There are four prim types, selected by the header's `0xF000` nibble. Bit `0x800`
marks the extended ("re-transformable") layout, which keeps a local copy.

| Type | Size | What it is | Vertical test | Column test |
|---|---|---|---|---|
| `0x8000` | 0x14 / 0x24 | sphere (half height = radius) | 001A44B0 (walker pass 2 only) | none (hit = 0) |
| `0x4000` | 0x18 / 0x2C | vertical cylinder (radius `+0x10`, half height `+0x14`) | 001A44B0 | 001A56A0 |
| `0x2000` | 0x1C | one axis face (face byte `+2`, origin, signed extent) | 001A4650 (faces 3/4) | 001A5760 |
| `0x1000` | 0x14+0x18n / 0x24+0x30n | convex n-gon (normal, d, n points, n edge normals) | 001A4030 | 001A58B0 |

Word bit 31 marks a static cell, which walker pass 1 reads. Bit 30 means skip.
Pass 1 stops at the first word without bit 31.

**AREA11 has no static cell.** Word 0 is `0x70`, so pass 1 stops at once. Every
AREA11 cell is reached through the owner that publishes it. The on-disc
directory is `extract/chunk15/f12_id44.bin +0x39800`: 27 uids, 22 hulls,
0x4ED8 bytes. The captured beats show which owner holds each uid:

| uid | Prims | Owner (captured) |
|---|---|---|
| 0 | 5 × 0x1000 (not extended) | fence-door box (x 414..423, z 286..291); not in any captured list |
| 1, 2 | 1 × 0x2000 | faces at z 164.3 / 156.3; not in any captured list |
| 4 | 13 × 0x1800 | elevator `0x827B10` (`0x7AA880`) |
| 5, 6 | 1 × 0x4000 (not extended) | drums 00156620 (`0x7A99D0`, `0x7A9CC0`) |
| 7..10 | 6 × 0x2000 | crates 001551B0 |
| 14 | 12 × 0x1800 | truck `0x823FF0` (`0x7A9FB0`) |
| 15 | 6 × 0x1800 | `0x825940` (`0x7A6AD0`, model 26) |
| 17, 18 | 5 × 0x2000 | class-4 owners `0x7AAB70` and `0x7AA590` (uid 18 is the panel cell of PLAYER_CLIMB_SLIDE.md) |
| 19..26 | 6 × 0x1800 | pickup boxes 00219550. 19 is the battery (freed in beat 01; its hull keeps the last transform). 20 and 26 were never placed. |

Owners with uid 3 or 16 have offset word 0, and the walkers skip them.

**Publication.**

- **Re-transform, `001A2370(actor, matrix)`.** It re-transforms an owner's
  extended hull: each local lane goes through `001026A0` (w 1 for points and
  centres, 0 for normals). It rebuilds the hull AABB and sets n-gon `d` = axis
  · first point.
  - It does nothing for uid `0xFF`, for word 0, for uid ≥ count, and when the
    first prim is not extended. So the drums' 0x4000 cells never move.
  - The truck, the elevator and the pickups pass `+0xD0`.
  - `0x825940` passes `*(D_00275B40 + 0xC) + 0x90`, its bone-3 matrix. Runtime
    `0x825A54..0x825A64` has the first of its four call sites.
    `D_00275B40` is the current owner's `+0x110` bone-slot array: the pool
    walk `001AFD70` calls `001CB590(cur, 0x2F0, +9)` before each behaviour,
    which stores `D_00275B48 = cur` and calls `anim_bone_array_setup`,
    `D_00275B40 = D_00275B48 + 0x110` (both byte-matched,
    `src/func_001CB590.c`, `src/anim_bone_array_setup.c`; em_actor_pool.c
    models the same step). So `*(D_00275B40 + 0xC)` is `*(owner + 0x11C)`,
    which is how the test reads the matrix from a captured owner.
- **Class lists.** `001B1B70(actor)` pushes the owner's `+0x14` onto the class
  lists:
  - class 4 goes through `001B1D20` onto `D_00275B80`, cap `0x80`;
  - classes 1, 2/0xA, 7 and 0xD, and flag `0x80`, go through `001B1C60`,
    `001B1CA0`, `001B1D60`, `001B1DA0` and `001B1DE0`.

  The lists grow downward from static bases. `001AAD00` publishes each list,
  copying the cursor and count into `D_00275B7C/B84` etc., then resets the
  cursor to the base.

  Consequences:
  - Published entry j is the (count−1−j)-th push, so the newest push comes
    first.
  - A frame's pushes overwrite the published entries from the end while
    readers still walk them.
  - `001AF8E0` resets every list.
- **Who publishes when.**
  - Crates (REST) and the truck call `001B1B70` every tick.
  - The drums call `001B1D20` within 50 units of the player; otherwise they go
    through `001B17A0`.
  - The elevator and the pickups go through `001B17A0`. It publishes only when
    `001B1630` finds the owner visible.

  So those cells exist for collision only while they are on screen.

**Queries.**

- **`0019AB20(actor, position, probe, mask)`: the vertical probe.**
  - The segment runs from `(x, y − probe.y ± 0.001, z)` (+0.001 when
    probe.y < 0) to the position.
  - `0x7000324E` = actor `+2 & 0x1F`.
  - With mask bit 1, `0x70003254` = actor `+0x14` and `0019F730` runs. A hit
    gives 2.
  - With mask bit 2, `0019C830` runs the grid over the segment the cells
    clamped. A hit gives 4.
  - On a hit, delta = point − position. Mask bit 31 adds delta.y to `+0xB4`.
    With no hit, `0x700031D0` = 0.
  - `0x700031D4` (the hit owner) keeps the last cell owner hit even when the
    grid then wins.
- **`0019F730`, pass 2.** It walks the published class-4 list and skips an
  owner when:
  - its `+0` is 0;
  - its class is not 4;
  - it is the query actor itself;
  - its uid is `0xFF`, its word is 0, or its uid is ≥ count;
  - its `+0x54` is ≥ 0x51.

  After that it applies the hull AABB (x/z point, y interval) and walks every
  prim. On each hit it clamps the segment end y, records the owner in
  `0x700031D4` and ORs `+0x54` into the low byte of `0x700030CA`. The record
  `0x700031D0` = `D_700030B0`: its `+0x1A` is the class | kind, and `+0x24`
  (`0x700030D4`) is the normal.
- **`0019BC40(pos)`: the column table.** Pass 1 walks the same owners, without
  the self and `+0x54` gates, and only while n < 20.
- **`D_008104C4` (player `+0x214`).**
  - `00175900` probes `0019AB20(p, +0xB0, +0x280, 6)`. The captured `+0x280`
    is (0, −13.8, 0). It passes the probe's return value to
    `00175CF0(p, kind, index)` as `arg1`; it is the only caller.
  - `00175CF0` stores `0x700031D4` in `+0x214` when kind & 2 and it is
    nonzero (`.s` 0x00175DD0..0x00175DE0). It then reads `+0x214` twice in
    the same call: `00175640(*(+0x214))` for a 0x1000 surface (the jal at
    0x00175E00 loads it in its delay slot), and the `+0xA |= 0x80` test
    (0x00175F84). `00175900` probes again later (the 0x5B depth probe),
    which overwrites `0x700031D4` but not `+0x214`.
  - `0015BA50` moves `+0x214` to `+0x308` and clears it each frame.
  - The truck arms on `*(+0x214) + 0x0D == 9`.

Corrections to the readable C (the `.s` is the authority):

- `001A4030`: the edge normals are at `p + 0x14 + 12n`, not 16 bytes further.
  The normal is staged at `0x700030D4`, not `0x700030B0`. The hit is stored at
  `0x700031B0`.
- `001A2370`'s C passes `(in, mtx, out)` to `001026A0`. The call is
  `(out, mtx, in)` on the same buffer.
- `001A58B0` has no facing test.
  - A vertical n-gon (ny 0) divides by zero to ±`0x7F7FFFFF`. It never
    survives the edge test in AREA11.
  - For ny ≤ 0 it records the lower crossing and writes `0x700031AC`.
- Unknown prim types:
  - The walkers leave the pointer in place.
  - Pass 2 of `0019F730` keeps the previous prim's hit flag.
  - `0019BC40` reads a stale register there, so the native faults.

## 2. Translations

| File | Original routines |
|---|---|
| `em_actor_collision.c` | 001A2370 (+001026A0, 00102738), 001B1B70, 001B1D20 (and the other pushes), 001AAD00 list block, 001AF8E0 list half, 0019AB20, 0019F730 (both passes), 001A44B0, 001A4650, 001A4030, 0019BC40 pass 1, 001A56A0, 001A58B0 |
| `em_collision.c` | `em_collision_grid_vertical` (0019C830 gate + 0019ED80), `em_collision_grid_vertical_nodes` (same over a given node order), `em_collision_column_finish` (0019BC40 pass 2 + sort + cull, split out of `em_collision_column_table`, which is unchanged in behaviour), `em_collision_column_box_face` (001A5760) |

- The directory is kept in its original byte layout. The owners are the pool's
  `EmActor` records, and the lists hold `EmActor.self`.
- Arithmetic follows the EE model the oracles share: each operation truncates,
  overflow clamps, denormals flush, and mula/madd are separate truncated
  operations.
- Faults (−1), where the original would read memory it does not own:
  - a malformed directory;
  - a static cell reached without its `D_0024D7C0` kind view;
  - a published owner whose word has bit 31 (pass 2 adds the raw word);
  - an unknown prim type in `0019BC40`;
  - NULL arguments, including a NULL or incomplete `EmCollColumnMath` for
    `0019BC40` (no host sqrt/atan is substituted).

## 3. The AREA11 directory asset

`python3 tools/test_actor_collision_reference.py --export [PATH]` writes the
user's own directory bytes, by default to `assets/scene_snow/area11_cells.bin`
(ignored). `em_actor_cells_load` reads that file. This is a stand-in until a
dedicated exporter exists (section 7).

## 4. Verification

`python3 tools/test_actor_collision_reference.py` takes about 8–9 s.
`EM_TEST_FULL=1` takes about 45–70 s. Both run over four PCSX2 route beats (04
elevator ride, 05 boxes, 07 truck preview, 08 truck crossing), and the code
executed from RAM is checked against the ELF.

- **Publication against real captures.** Across all four beats, 32 captured
  hulls equal `001A2370`-native applied to the disc hull with the owner's own
  matrix, byte for byte:
  - truck, elevator, pickups 21..25 and uid 15 (bone 3);
  - outside those hulls, every captured directory equals the disc bytes.
- **Original `001A2370` against native.**
  - 183 cases (391 in full) over the captured hulls, with the owners' matrices
    and random rotations.
  - 60 cases (400 in full) over synthetic directories. These cover the
    extended 0x8000/0x4000 prims, unknown types, empty hulls and non-extended
    first prims, which AREA11 data never reaches.
- **Lists.** Original `001AF8E0`/`001B1B70`/`001B1D20`/`001AAD00`, with its
  nine hooks recorded, against native: 400 operations (3000 in full) over all
  class bytes. The whole list block and its storage are compared.
- **`0019AB20`.** Every result field: kind, point, delta, `0x700031D4`, the
  record, its `+0x1A` and `+0x24`, and `+0xB4`.
  - Cases: random probes over every owner hull, plus prim edges built from the
    captured cells:
    - face bounds and planes at the segment ends (zero probes included);
    - exact-circle radii and caps;
    - n-gon vertices, edge midpoints and planes;
    - facing products near −1e−5;
    - inside-test dots just in and just out of (0, 1e−5].
  - Variants: injected drum owners (0x4000); `+0x54` 0x50/0x51; a
    static-directory variant (pass 1 over uids 0..2 with the RAM
    `D_0024D7C0` kinds, which covers the non-extended n-gon); a crowded list.
  - Sizes: quick 1,840 cases (1,540 of 5,251 edges); full 10,051 (all edges).
  - Every `0019C830` pass is also replayed natively over the original's own
    node order (`em_collision_grid_vertical_nodes`). All 3,746 grid passes of
    the full run are bit-exact.
  - On every case that reaches `0019C830`, the native grid pass must start
    from the original's entry state: the segment `0x70003190`/`0x700031A0`
    after `0019F730` (`EmActorCollisionHit.grid_start`/`grid_end`) and
    `0x700031D4`, bit for bit. A differing case is a mismatch.
- **`0019BC40` with every owner prim type** against native, which covers the
  n-gon owner cells the climb lane could not bind: 588 columns (4,805 in
  full), including 2,005 prim-edge columns and the 20-candidate cap. Every
  entry (flags, height, aux, owner, node byte) is identical.
  - **SDK calls.** Nothing is hooked on the original side: `001A58B0` and
    `0019F330` call the original `0011E748` (sqrt, via `0011CB90`) and
    `0011DBB8` (atan), executed as instructions (their code, with their
    whole static call graph, is checked against the ELF). The native
    column's `EmCollColumnMath` workers call back into the same original
    executions, so the aux values are compared against original
    instructions end to end.
  - **Named workers.** Separately, each distinct SDK argument is given to
    the workers section 7 names for the live binding, and the agreement is
    printed (not asserted): `em_item_sdk_sqrt` equals `0011E748` on every
    argument (46 of 46 quick, 56 of 56 full); `em_director_original_0011DBB8`
    is one ulp high on 18 of 49 quick (24 of 58 full). Section 5 has the
    cause.
- **The truck crossing (route beat 08).** Beat 08 resumes from the 07 snapshot
  with the truck at rest.
  - Frames 30..43: the floor probe runs at each row's x/z, from 13.8 above the
    traced feet down to them.
  - The original and native agree on every row.
  - The truck (`0x7A9FB0`) is the hit owner exactly from f43. That is the frame
    the trace's `+0x214` becomes the truck.
- **Mutations.** Each of these fails the quick run:
  - every comparison direction in `001A44B0`/`001A4650`/`001A4030`/`001A56A0`;
  - the facing and inside-test epsilons;
  - the `+0x54` gate, the self skip and the nudge sign;
  - the pass-2 y re-clamp;
  - the `001A2370` bound lanes and w lanes;
  - the list cap and the 0xA class;
  - the `001A58B0` sign.

  These mutations change nothing observable, and no test catches them:
  - the class byte's low bits (the kind overwrites them);
  - ny == 0 in the classification;
  - `break` → `continue` past the 20-candidate cap.
- `tests/actor_collision_test.c` (ASan/UBSan) covers:
  - directory validation;
  - the list storage model (newest first, overwrite from the end, cap);
  - the result record, the gates and the faults;
  - the column entries;
  - every worker adapter.

KNOWN INEXACT (grid only, shared with every EMCL grid walk): `0019C830` visits
only the nodes one rank-table span admits, in that span's order, and every hit
clamps the segment end. The EMCL carries neither the spans nor the rank bounds,
so the native visits every node in EMCL order. This can move the final height
by an ulp, or pick another node at an equal height. A differing `0019AB20`
case is classed KNOWN INEXACT only when all of these hold; anything else fails
the run:

- the native grid pass started from the original's exact entry state
  (segment and `0x700031D4`, above);
- the original's grid arithmetic over its own node order is reproduced
  exactly (the ordered replay);
- both sides ended on a grid record, with the same node or at an equal
  height.

The full run found 3 such cases in 10,051, each with the same node and a
one-ulp y. They are printed, not hidden. Exactness needs the spans and rank
bounds in the EMCL. The 3,099 EMCL grid planes equal the RAM nodes bit for bit.

## 5. Findings for other lanes

- **Stale static n-gons in the EMCL.** The EMCL's 84 set-2 polys are the
  n-gon owner cells at their disc positions: door 5, elevator 13, truck 12,
  uid 15 6, pickups 48. `em_collision_segment_query`/`move_probe` walk them as
  static walls, but in the original they exist only while their owner
  publishes, and at its current transform. They must leave the static walk
  when `0019FE50`/`001A0B10` pass 2 is bound (section 7).
- **The EMCL has no node class.** The EMCL lacks `EM_COLL_FLAG_NODE_CLASS`
  (flags = 1), so a grid hit's class byte is unknown. The player adapter
  faults on it instead of guessing (PLAYER_CLIMB_SLIDE.md §6 item 2).
- **Stale doc.** TRUCK_ORIGINAL.md lists `001A2370` as not translated. It is
  translated now: `em_actor_collision_owner_hull`.
- **Decomp defect: the NEARMISS C of `001A4030` (Extermination
  `src/func_001A4030.c`) misplaces two addresses.** Checked against
  `func_001A4030.s`:
  - the edge normals: the C computes `plane + 0x10 + (count * 3 + 4) * 4`
    (line 88), which is `p + 0x24 + 12n`. The `.s` adds `(3n + 4) * 4` to
    `s2 = p + 4`, which is `p + 0x14 + 12n`;
  - the staged normal: the C's final loop writes `0x700030B0 + 4i`. The `.s`
    stores at `0x24(D_700030B0)`, which is `0x700030D4 + 4i`, the record's
    `+0x24`.

  The native follows the `.s`. **Fixed in the decomp (2026-09-23,
  Extermination 6454325):** `src/func_001A4030.c` now byte-matches (100%,
  linked from the compiled C), so it is ground truth. It also shows a side
  effect the native does not model yet: 001A4030 stores the ratio
  ny^2/(nx^2+nz^2) to scratchpad 0x70003680 (0x001A4358), which
  em_actor_collision.c keeps in a local. The same address is written or
  named by 001A2370, 001A50A0, 001A7BA0, 001B41F0, 001B55E0, 001CD2B0 and
  001CE660; if a reader of 0x70003680 is on the AREA11 path, the native
  must publish it.
- **Two EE float models disagree on `0011DBB8`** (settled since: docs/EE_FLOAT_MODEL.md;
  the decomp's `0011DBB8` C also had atanhi/atanlo swapped in the id>=0 return, fixed
  in 6454325 — recheck any translation that copied it against 0x11DE10..0x11DE44). This oracle (the shared
  interpreter of `tools/test_player_slide_reference.py`) truncates every
  add, sub, mul and div. `em_pose_math.h`, which
  `em_director_original_0011DBB8` uses, pre-trims add/sub operands to one
  guard bit and rounds division to nearest ("the saved PCSX2
  configuration"). The structure of the director translation is right:
  rebuilt over a scratch copy of `em_pose_math.h` with plain truncating
  add and div, it equals this oracle's execution of `0011DBB8` on 606 of
  606 arguments. Rebuilt with only the division changed, it equals 417;
  with only the add changed, 587. As shipped, it equals 411. Which model
  PCSX2 implements decides whether the column aux values (and every
  collision oracle that shares this interpreter) are exact. That is a lead
  decision, not this lane's.

## 6. Not translated (fail-stop or unbound)

- **The horizontal and segment walkers' pass 2.** Each is listed with the prim
  tests it calls:

  | Walker | Round prims | Face prims | N-gons |
  |---|---|---|---|
  | `0019FE50` (`0019AD00` move probes) | `001A4830` | `001A4D10` | `001A4030` |
  | `001A0B10` (`0019A570` segments) | `001A5C30` | `001A50A0` | `001A4030` |
  | `001A1390` (camera) | | | |

  Until those are bound, wall probes see only the EmCollCell faces that
  `em_collision_cell_bind` binds, plus the stale set-2 polys above.
- `001B17A0` / `001B1630` (render visibility), which gates the elevator,
  pickup and drum publication.
- The `D_00275B54/B58` list: nothing pushes to it in the translated code.
- The player's `+0x34` axis (surface 0x35): the adapter faults on it.

## 7. Binding (for the coordinator)

1. **Storage.** One `EmActorCellTable`, loaded by `em_actor_cells_load` at area
   load (w_001AFCA0 / the area load that installs `0x70003250`). One
   `EmActorClassLists`, reset where w_001AFCA0 runs `001AF8E0`.
   `EmActorCollisionWorld` = { table, lists, &the EMCL, NULL, 0 }. AREA11 needs
   no static kinds.
2. **Stage `w_001AAD00`.** Call `em_actor_class_lists_swap_001AAD00` after the
   nine close-out hooks.
   - Its FLAG80 list duplicates the interaction list that
     `em_interaction_scene_publish` keeps. Use this module for class 4 and pick
     one store for the rest.
3. **Owner pool nodes.** Give each owner an `EmActorCollisionOwner` = { world,
   lists, its EmActor, pool }.
   - **Truck #24:** `EmTruckHooks.hull` = `em_actor_collision_owner_hull`,
     `.hull_bounds` = `em_actor_collision_owner_hull_bounds`, `.publish` =
     `em_actor_collision_owner_publish`.
   - **Crates #12..15:** `.publish` = `em_actor_collision_owner_publish`,
     `.probe` = `em_actor_collision_owner_probe`.
   - **Drums #22..23:** `.contact` = `em_actor_collision_owner_contact`,
     `.hull` = `em_actor_collision_owner_hull`, `.probe` =
     `em_actor_collision_owner_probe`. `.visibility` still needs `001B17A0`.
   - **Elevator (`0x827B10`), pickups (`00219550`), `0x825940`:** their modules
     must call `em_actor_cells_retransform_001A2370`:
     - the elevator and pickups with `+0xD0`, after `001C6380`;
     - `0x825940` with its bone-3 matrix.

     They then publish through `001B17A0` → `em_actor_class_publish_001B1B70`.
     No such worker exists in em_elevator_* / em_pickup_* yet.
4. **Player stage `w_0015BCF0`.** Bind `EmPlayerFloorWorkers.ground` and
   `EmPlayerFallWorkers.ground` to `em_actor_collision_player_ground`, with
   context `EmActorCollisionPlayer` = { world, { player +0x14, player +0x02,
   NULL }, NULL }.
   - **Column worker.** `em_actor_collision_column_0019BC40` serves 00179450
     and the climb's 0015DF10. Its `EmCollColumnMath` is required (NULL or a
     missing worker faults) and is bound to:
     - `.sqrt` (0011E748): a wrapper returning `em_item_sdk_sqrt(x)`, the
       nonnegative 0011CB90 path (the argument is nx·nx + nz·nz);
     - `.atan` (0011DBB8): a wrapper around `em_director_original_0011DBB8`
       with the tables from `em_director_original_load_atan_tables` over the
       user's ELF. The argument is always finite here (at most
       `0x7F7FC99E`). The worker returns a float, so on a negative return the
       wrapper records the fault in its context, and the caller fails the
       frame after the call returns; no value is substituted.
     - Section 5 has the one-ulp open question on 0011DBB8's arithmetic
       model.
   - **`D_008104C4`: done (lane "player-states-live").** `00175CF0` stores
     `0x700031D4` in `+0x214` and reads `+0x214` again in the same call
     (section 1), inside `em_player_floor_apply`:
     1. `EmPlayerProbeHit.owner` carries each probe's `0x700031D4` value,
        which `em_actor_collision_player_ground` sets.
     2. `EmPlayerFloorActor.link_owner` is `+0x214` (it replaced the old
        `link` flag); `link_flags`/`link_type` cache the owner's +2/+3.
     3. The store runs `if ((hit->kind & 2) && hit->owner)`, after
        `position += delta` and before the push test (`.s`
        0x00175DD0..0x00175DE0). A kind-4 result keeps `0x700031D4` but does
        not store it.
     4. `link_test(context, a->link_owner, &linked)` is `00175640(*(+214))`.
        `em_actor_collision_player_link` translates 00175640 (byte-matched)
        over the owner's `model` (+3) and `callback` (+0x10);
        `em_player_link_00175640` is the shared rule.
     5. `contact |= 0x80` tests the stored `link_owner`.

     Evidence (`tools/test_player_floor_reference.py`):
     - 00175640 executed for all 256 type bytes with its three behaviour
       addresses (1,537 cases);
     - 96 targeted store cases, plus the owner compared by address in all
       5,000 floor-service cases (the old test compared only
       `bool(+214)`);
     - the real worlds: on route beat 05's crate rows and beat 08's f43,
       the original 00175900 and `em_player_floor_service` over
       `em_actor_collision_player_ground` store the same owner, and it is
       the trace's (9 rows quick: 8 crate rows and f43; 294 full).

     Setting `owner` to NULL in the adapter fails the real-world rows.
     `EmActorCollisionPlayer.entity` remains the last probe's (the 0x5B
     depth probe's); binders read `+0x214` from the floor actor
     (`player_states_actor()->link_owner`), never from `entity`.
   - **Player adapters** (player query side). The first two are named
     above; the other two are new:
     - `em_actor_collision_player_ground`, for 0019AB20;
     - `em_actor_collision_player_link`, for 00175640;
     - `em_actor_collision_player_column`, for 00179450's 0019BC40. Its
       context `EmActorCollisionPlayerColumn` = { world, math }. It faults
       without math, and above 16 survivors, where the original's result
       arrays alias;
     - `em_actor_collision_player_link_kind`, the climb's +308 kind (2 for
       behaviour 00828700/00827880, 1 other, 0 none).

     FIRST_CONTROL.md "Live player states" has the binding, and the gates
     that keep it off until 0019B6C0/0019B8C0 and the EMCL node class exist.
   - `0015BA50` copies `+0x214` to `+0x308` and clears it, at its original
     position in each player stage.
   - `EmTruckWorld.ground_kind` = `&((EmActor *)D_008104C4)->param` (+0x0D),
     or NULL when it is 0, with `D_008104C4` =
     `player_states_actor()->link_owner`. The truck arms on 9.
5. **Makefile (not edited; hunk for the lead).**
   - Add `src/game/em_actor_collision.c` to the game sources after
     `src/game/em_collision.c`.
   - Add `test-actor-collision-reference: python3
     tools/test_actor_collision_reference.py`.
   - Add a `test-actor-collision` target building `tests/actor_collision_test.c`
     with `src/game/em_actor_collision.c src/game/em_collision.c
     src/game/em_actor_pool.c -lm` and the sanitizer flags.
6. **Retire after binding.** The legacy `em_collision_moving_*` /
   `em_collision_blocker_*` registries and the EMCL set-2 static walk
   (em_collision.h marks them).
