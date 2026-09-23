# Actor pool and AREA11 static roster

Modules: `src/game/em_actor_pool.{h,c}` (WP-3 step S4) and
`src/game/em_actor_roster.{h,c}` (WP-3 step S7). The design context is in
SCENE_COORDINATOR_DESIGN.md sections 2.5 and 4.1 to 4.4.

## What is verified

- **Pool.** 001AF8E0 (pool half), 001AFA90 alloc, 001AFA50 link, 001AFBC0
  unlink, 001AFC10 free and 001AFD70 walk. `tools/test_actor_pool_reference.py`
  runs the original instructions (with 001AF800, 001CB590 and 001CB5B0) next to
  the native pool over random alloc/free/walk scenarios. It scribbles every
  byte the native record holds between operations, so any byte that alloc or
  free clears, rewrites or keeps is checked against the original.
  `tests/actor_pool_test.c` covers the fail-stop paths.
- **Roster.** 001B6990, 001B6910, 001B65C0, 001B64F0, 001B6660, 001B11E0 and
  001C5C50. `tools/test_actor_census_reference.py` runs them over the exported
  AREA11 tables and 400 synthetic tables, then compares the whole 0x100 x 0x2F0
  arena, the pool globals, the progress bytes and the list order. It also walks
  D_00275BC0 in the captured original RAM (the census).

## Record field +0x2E (`EmActor.flags2`)

- 001B6660 stores `(rec[+6] (s16) >> 8) & 0xFF` there with `sh`, and 001B6990
  stores `(rec[+2] (s16) >> 8) & 0xFF`. Both functions are byte-matched.
- 001AFA90, 001AFC10 and 001AF800 never write +0x2E, because no store in their
  .s uses offset 0x2E. The halfword therefore survives a free and a later
  alloc. Only the 001AF8E0 memset clears it, and 001C5C50 leaves a reused
  record's old value in place.
- `em_actor_pool_record_image` writes it at +0x2E. The census compares that
  field directly; the old log-based patch is gone. After each synthetic spawn,
  the census frees some spawned records and lets 001C5C50 reuse them. Oracle
  and native then have to agree on the kept halfword. `synthetic_recycled_flags2`
  counts the nonzero cases, and the test asserts that this count is above 0.
- `EmActorRosterSpawned.flags2` is only a copy of `actor->flags2`, taken after
  the spawner runs, for the spawn report.

## Boundaries (deliberate, fail-stop)

- See the header comments. A freed `next` during the walk, a NULL behaviour, a
  bad free handle and bones without the 001AF800 worker all fault.
- The native-only fields (`behavior`, `release`, `owner`, `source_id`,
  `generation`, `allocated`) have no original counterpart.
- The pool does not cover the per-class list block D_00275B54..D_00275BB8.
  That block belongs to the 001AAD00 class-list owner.

## Binding

The coordinator chain owns the pool and calls
`em_actor_pool_walk_001AFD70(pool, scene, mode, world, trace, ctx)` once per
tick. A node's native behaviour is attached through the roster's
`EmActorRosterBindFn`. A node left unbound faults at its original callback
address when the walk reaches it.
