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
| First contact with the tunnel's surface `5B` | `00175900 → 00187DE0` | Bound to the existing math owner; captured-RAM original comparison passes. Continuous entry still blocked earlier by protected drawing. |
| Moving while player water depth `+23C` is nonzero | `00187350 → 001E8B90` | Bound to the existing render owner over the area's canonical grid; original comparison passes. Continuous entry not yet observed. |
| Ordinary skids and landings | `001EA240 → 001EC270` | Existing silent omission removed; live work fields and both packet chains match original instructions. |
| Shooting water, and an ordinary impact-marker variant | `001EB7F0`, `001ECB00` | Water handler translated; ordinary handler reuses its existing HUD owner. Live packet comparisons pass; full play requires the separate aim-hull fix. |
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

## Water and effect binding

The known water failure has two layers. The floor service calls
`00187DE0` on first contact, before footsteps can reach `001E8B90`.
The new floor bridge publishes its just-computed depth to the actual player
record, calls the existing `em_area01_math_00187DE0`, then refreshes the
typed floor view. Water workers borrow the in-stage record read-only; they
do not project post-stage position or animation state over it.

The ripple adapter calls the sole `em_area01_render_001E8B90`, using the
loader-owned grid initialized by `001E7780` and the original conversion
owner `001281C0`. There is no new persistent grid, injected capture state,
or new boundary clamp. Contact uses the original splash effect `80000016`
and sound `CA`/`DB`; wading's `8000001D` handler was already live.

`001EC270` now uses the same work-block/packet bridge as the splash
handlers. Its two descriptors were already exported. The old counted-gap
path is removed: every missing effect handler faults. Static follow-up
found water bullet effect `80000026 → 001EB7F0` and ordinary impact effect
`80000019 → 001ECB00`. The first is a short translation of the original
two random rounds; the second calls the existing `em_area00_hud` owner,
including sprite colour, three packet submissions and work-step easing.

Run `python3 tools/export_area01_water_effects.py` to add the two descriptor
windows needed by those impact handlers to the existing local EMET export.
The separate exporter preserves the protected base tool, verifies the
pinned boot ELF and compares every new descriptor byte against all 16
AREA01 captures. It replaces this worktree's asset symlink before writing.
No exported bytes are committed.

`test_area01_water_reference.py` compares whole RAM/scratch and ordered
contact boundaries against original instructions. Quick mode checks 8
contact chains, 48 ripple calls and 6 complete native effect packet chains;
full mode checks 30, 180 and 24 respectively. It exercises both depth sounds
and refuses a missing grid. The existing floor, footstep, player-view,
runtime, flame-service, effect API and render references also pass. These
are worker/composition proofs, not a claim that a continuous route has
reached the water. Receipts are `water-reference*.log`, `water-export.log`,
`references-water-existing.log` and `render-reference.log` under
`build/level2-crashes/`.

Shared water edits: `em_scene_bindings.c` registers/clears one water
callback and constructs its original argument lanes. The Makefile adds
`test-area01-water-reference`; gameplay bindings reuse already-built owners.

The isolated staged water tree passed `make -B all` with zero warnings,
New Game control (30 ticks, displacement `9.599849`), default smoke and
all listed reference suites, including 956 render cases / 4,744 entries.
Its exploration still stops at the independent flame close-out
`001A8734` before water; this is recorded as a failure, not a water pass.
Receipts: `build/level2-crashes/index-water-{build,startup,smoke,references,render}.log`
and `index-water-exploration/summary.json`. Independent adapter review
found no concrete defect; continuous water play remains unverified.
