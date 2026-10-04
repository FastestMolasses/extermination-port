# AREA01 first-visit binding

Worktree: `../extermination-port-level2`, branch `level2`, based on main
`3d482f6` (2026-10-03). Main and the first-level worktrees are untouched.
`assets/` and `data/` are real directories with links to the main checkout's
entries. Before any export into a linked directory, replace that directory
link with a private copy; a writer opening a file through a directory link
would otherwise change main's assets. No emulator was launched.

## State

Phase 1: arrival failure reproduced and census mapped. The five missing
arrival workers now have standalone original-instruction verification.
Broad regression verification is in progress. Phases 2–4 are not complete:
no AREA01 world frame plays yet.
The frame-machine guard remains; missing workers have not been defaulted.

The headless `a01_arrival` probe reaches the state-0 rebuild at native
counter **15007** (route 15 row 741, also EXIT `exit_01` row 306). Its first
world frame reports `0x1AE040 state 1 in AREA01`, then
`FAULT at 001AE040 (code 1)`; the smoke fails at `a01_arrival frame=0`.
Receipt: `build/level2/arrival/run.log`, `ticks.jsonl` and `rand.trace`.
Reproduce: `EM_LEVEL_SMOKE_UNTIL=a01_arrival make test-level-smoke`.

The opt-in phase asks for 60 neutral world frames after the rebuild. Its
checker compares the existing tick-log fields with route 15 rows 741–801;
it does not claim to compare owner bytes absent from that recording. The
default full first-level route still ends at `exit`. Longer runs keep the
arrival pool witness and the exit check; they do not turn an unverified
phase into a pass.

## Census and dependencies

See [SECOND_LEVEL_CENSUS.md](SECOND_LEVEL_CENSUS.md): the current 174-row
`a01_delta.json` and the 62 post-arrival rows in FIRST_LEVEL_CENSUS section
3.26 have **179** distinct region-qualified entries, **29,547** instructions.
Overlay identity matters: AREA01 `00823580` is not AREA11 `00823580`.

At the mapping checkpoint: 0 AREA01-live, 172 verified-unbound, 5 missing,
2 boundary. After the prerequisite translations: **0 live, 177
verified-unbound, 0 missing, 2 boundary**. First-level shared-live evidence
is recorded separately; it does not prove an AREA01 adapter is bound.
The five initially omitted arrival routines were `001C4FA0`, `001C50B0`,
`001D0C80`, `001D0D40`, AREA01 `00825740`.

[LEVEL2_DEPENDENCIES.md](LEVEL2_DEPENDENCIES.md) records original callers,
existing owners and the canonical-state mapping work still required.
[LEVEL2_COLLISION.md](LEVEL2_COLLISION.md) records why the segment walker's
no-span refusal must not be replaced by a made-up return value. Actor-cell
bit 29 is already accepted by the baseline.

## Binding ledger

No new AREA01 behaviour has been bound at the phase-1 checkpoint. Existing
arrival bindings are documented in FIRST_LEVEL_EXIT section 7. The call
chain after arrival is `001ACEC0 -> 001AD250 -> 001AD4D0 -> 001AE040`, then
the classifier and `001AE5E0` / `001AE6B0`. The guard prevents reaching
unbound actors, rendering and interaction services.

The first phase-2 prerequisite checkpoint supplies the missing translations
without changing that guard:

- `001C50B0`, placed by AREA01 roster record 40, calls its predicate
  `001C4FA0`; new `em_area01_light_owner` uses the existing math address
  contract and canonical workers. Quick **161 cases / 216 worker
  boundaries**; full **2,309 / 2,536**, plus six fail-stop contracts.
- `001C02E0` state 0 now calls its same-module `001D0C80` and `001D0D40`.
  Quick **40 / 21 helper cases**, caller **57 cases / 363 boundaries**;
  full **48,851 helper cases**, caller **1,606 / 7,656**. The entire math
  suite passes; two pre-existing unrelated branch outcomes remain
  uncovered. See LEVEL2_BONE_INIT.md.
- AREA01 roster record 38's `00825740` runs setup before its first-visit
  teardown gate. Its standalone owner has **184 quick / 1,131 full
  cases**; full overlay suite **14,034 cases / 21,199 executions**, all
  15 entries. See LEVEL2_TALK_OWNER.md.

These are translation results, not AREA01 route or live-worker evidence.

## Verification

- Initial `make all`: passed, zero compiler warnings.
- `EM_STARTUP_TEST=newgame-control`: passed; 1,301 locked ticks, zero locked
  motion, 30 move ticks, displacement **9.599849**, census 49.
- First-level main smoke: **19 live phases through exit**, capture checker
  passed. The complete side-run suite is still in progress.
- Ten AREA01 quick oracle suites passed. Exact counts and receipts are in
  SECOND_LEVEL_CENSUS. The existing-render suite initially failed to link
  the point-light module's shared matrix workers; adding its existing
  `em_owner_services_original.c` dependency fixed the harness.
- The 277 baseline `make test-*` targets are running, with per-target
  results in `build/level2/verification/results.json`. Sandboxed native
  GPU tests cannot create Metal devices; these require a headless run with
  normal host access. That infrastructure failure is not a game pass.

## Shared-file edits

Phase 1:

- `src/game/em_level_smoke_test.c`: opt-in arrival idle phase; explicit
  first-level default endpoint remains `exit`.
- `tools/test_level_smoke.py`: dispatch the AREA01 checker, preserve exit
  checks on longer logs, and keep `last` meaning the first-level endpoint.
- `tools/test_area01_render_existing_reference.py`: link the matrix-worker
  dependency already used by the primary effect oracle.
- `tools/test_coll_segment_walkers_reference.py`: AREA01 route bounds and
  original-selector evidence, without changing the game walker.

New phase-1 files: this document, SECOND_LEVEL_CENSUS, LEVEL2_DEPENDENCIES,
LEVEL2_COLLISION and `tools/level_smoke_area01.py`.

Phase-2 missing-worker checkpoint:

- `em_area01_math_owner.c/.h` and its existing oracle: two missing helpers
  and direct calls from their existing owner; no live host added.
- `em_area01_overlay.c/.h` and its existing oracle: roster record 38's
  missing owner; no live hook table added.
- New `em_area01_light_owner.c/.h`, its oracle and the three evidence
  documents LEVEL2_LIGHT_OWNER, LEVEL2_BONE_INIT and LEVEL2_TALK_OWNER.
- SECOND_LEVEL_CENSUS and LEVEL2_DEPENDENCIES record availability; neither
  first-level census nor first-level audit was edited.

## Known gaps

All main beats `a01_00..a01_07` and side beats remain unplayed by the native
port. AREA00 arrival is the intended stopping boundary. The message bank,
static and dynamic rendering, overlay init, canonical actor records,
scripts, interactions, doors and pickups still need their AREA01 adapters.
The extraction resident-offset label shift is not fixed; the decomp's
`tools/extract_data.py` is outside the allowed decomp edit scope. Any
source correction there must be reported in permitted docs, not applied.
No decomp corrections have been committed by this branch.
