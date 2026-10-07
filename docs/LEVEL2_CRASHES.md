# AREA01 first-visit crash audit

Worktree `extermination-port-crash`, branch `level2-crash`, starting at
`8ce3067`. Original instructions and local recordings are the behavioral
authority. This ledger distinguishes worker proof, recorded route comparison,
and exploratory input. It does not certify unrestricted play.

The release still has a rendering stop at `0023D930`: the fire owner's third
layer requests kind 6 near the crate stack (recorded `a01_00` row 405).
Completing that path requires the protected `em_chain_page.c` and
`em_vu1_page_programs.h`, plus its locally exported packet. This branch does
not bypass the draw or remove its fault. Runs stopped there cannot prove
the later portions of their routes.

## Reachable faults and evidence

| Trigger | Original | Status |
|---|---|---|
| First contact with the tunnel's surface `5B` | `00175900 → 00187DE0` | Baseline missing live contact binding; existing math translation found. |
| Moving while player water depth `+23C` is nonzero | `00187350 → 001E8B90` | Baseline explicit missing-worker fault; existing render translation found. |
| Ordinary skids and landings | `001EA240 → 001EC270` | Baseline silently omitted packets; existing render translation and source windows found. |
| Use scan near class-2 owners, including the control-room approach | `00160220 → 001AA4E0` | Baseline target projection refuses class 2 before the original eligibility predicate. |
| Approaching fire closely, including the crate pull-up | `001E3D90 → 001CFBE0(kind 6)` | Still faulting; protected rendering files are required. |

## Verification receipts

Local output belongs under ignored `build/level2-crashes/` and the individual
reference suites' usual ignored directories. No captured bytes or exported
assets belong in this commit.

Baseline `make -B all` passed with zero warnings. Baseline headless
`EM_STARTUP_TEST=newgame-control` passed: 1,301 locked ticks, zero locked
motion, 30 movement ticks, displacement **9.599849**. The sandbox cannot
create a Metal device; native test processes therefore require host Metal
access while retaining `EM_HEADLESS=1` (muted output, controller ignored).

## Shared-file edits

`em_scene_bindings.c` adds read-only floor observations to the tick log:
surface, depth, puddle, marsh and surface-height bits. The Makefile adds two
opt-in exploration targets. `em_level_smoke_test.c` includes the new driver
and calls its phase-entry/input hooks only when explicitly selected.

## Census promotions

No promotion is justified by a standalone oracle alone. Promote only after a
native reached worker and its compared AREA01 recording window are identified.

## Exploration fixture

`make test-area01-exploration AREA01_EXPLORE_ARGS='--case water'` starts an
ordinary New Game and uses the existing level-smoke approach to AREA01.
`--list` describes each custom input probe and each retained route replay;
`--all` opts into the entire set. `--bin` can select a frozen build.
The new C hook is active only when both exploration environment variables
select the current phase. Movement targets change pad input, never player,
collision, scene, or camera state.

Each run records the binary hash, phase prerequisites, first fault, final
player position/state, health, water fields, room entry and observed actions.
Completed tick logs are compressed. Input completion and observed coverage
are distinct: a script that misses its intended room is
`TARGET-NOT-OBSERVED`, and a navigation target blocked by geometry is
`ROUTE-BLOCKED`. Neither is a gameplay pass. Original parity remains a
separate check by the existing smoke comparator.

The initial staged fixture was built with `make -B all` (zero warnings),
passed New Game control at **9.599849**, and passed `make test-level-smoke`.
Its harness passes ASan/UBSan, malformed-script and bounded-navigation
refusals; the existing smoke harness/checker passes 16 paths, 20,101 pad
frames, 19,565 capture-shaped rows and 150 rejected corruptions. Receipts:
`build/level2-crashes/index-fixture-{build,startup,smoke,references}.log`.

The staged baseline fixture observed the status screen open and close.
Its east water approach reproduced collision close-out `001AAD00` failing
at `001A8734` before reaching water. This is a separate defect: the shared
collision world still routes AREA01 flame callback `001E3D20` through its
AREA11-only handler. The worker already exists in `em_area01_side.c`.
The fixture correctly returns a nonzero result and reports water as
unobserved. Receipt: `build/level2-crashes/index-fixture-exploration/summary.json`.
