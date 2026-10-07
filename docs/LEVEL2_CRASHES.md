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

## Canonical player and target fields

AREA01 exposed three shared-player omissions. Use predicates read retained
feet at player `+A0`, but the live stage had only refreshed `+B0`. The stage
now follows `0015BCF0`: retain the placement (including preceding carry) at
`+A0`, initialize `+B0`, and publish final feet after footsteps. Original
instruction comparison covers 224 cases over all 16 AREA01 captures,
including carried placement, locked states and callback order.

The rebuilt player also lacked the status-1 publication in `0015C420`.
Without it, the original flame callback requested a reaction but its parent
skipped damage. The exact spawn store is restored; 384 original instruction
cases cover six spawn kinds and four initial status bytes in every capture.

The Use chain's `001AA4E0` can now inspect class-2 targets through canonical
pool fields and the existing `001AA410` / `001AA2A0` translations, using the
original square-root owner. Its 8,654 cases pass, and the running-jump
reference retains 7,706 cases / 26,423 calls / 1,090 fault cuts. Aim target
`00185A10` can read the same canonical `+34` low halfword, while `+36`
keeps its independent owner: 256 original cases / 7,936 ordered events
and five crossing-span refusals pass. These changes add no actor shadows.

Shared Makefile edit: build `em_player_target_live.c` and the existing
`em_level14_port_boot.c`; add `test-player-target-live-reference`.

The player/target changes also require an AREA01 hull provider. The old
provider belongs to AREA11 Roger and refuses AREA01 records, both in aim/
melee and the newly reached Use-distance scan. The new per-world provider
borrows each canonical actor's delivered `+58` chain and current model-slot
bone matrices. It preserves the existing hull walkers and filters, clears
on unload, survives shared-owner rebinding, and never falls back after a
refusal. The original oracle passes 116 queries / 57 hits over 16 captures,
with four refusal cases and eight provider-lifetime contracts. Existing
hull and close-out regressions pass 1,238 cases and 40 runs respectively.
The shared scene file adds only its AREA01 registration, and the Makefile
builds `em_area01_hull_live.c`.

The complete isolated player/target/hull stage passes a zero-warning
`make -B all`, startup displacement `9.599849`, default smoke and the
AREA01 status probe (open, close, resumed input; no fault). The initially
separate player stage exposed the hull dependency at `001764E0`; the
complete stage includes its provider. Touched references pass, including
the repaired ASan/UBSan stage-host harness, which explicitly proves an
absent water owner faults. Receipts: `index-player-{build,startup,smoke,host,hull}.log`,
`index-player-references{,-tail}.log` and
`index-player-hull-exploration/summary.json` under `build/level2-crashes/`.

## Fire contact composition

The AREA01 close-out now calls its existing `001E3D20` contact worker and
`0021BB00` player gate, preserving canonical collision/player views across
nested effects. With the original spawn-status publication, the recorded
`a01_s3` path applies the expected five HP damage and reaction action `3E`.
The untouched full checker passes all 21 live phases through this route,
262 captured rows and the whole camera endpoint. The existing AREA01 RNG
check proves caller/LCG continuity; it does not possess an original per-call
trace, and reports its differing endpoint seeds without installing one.
Receipt: `after-fire-status/strict-check-correct-binary.log`.

The new original-reference suite passes 93 contact cases, 247 ordered
boundaries, 32 effect spawns and three refusals over 16 captures. Existing
effect-chain and AREA01 effect-service suites pass. Temporary diagnostic
traces are removed. The shared scene change is one AREA01 contact callback
registration; the Makefile adds its reference target. The opt-in fixture
now reports the unchanged row checker's exact prefix and observed targets,
including the east room's recorded entry 8.

The isolated staged fire tree passes `make -B all` with zero warnings,
startup displacement `9.599849`, default smoke, its reference suites and
the exploration harness. Fresh `a01_s3` exploration reaches the damage
reaction without a fault and matches all 262 captured rows. Receipts:
`index-fire-{build,startup,smoke,references,harness}.log` and
`index-fire-exploration/summary.json` under `build/level2-crashes/`.

## Aim target model ownership

After hull and health-field binding, aiming reaches `00183C40`, which reads
the target's bone-slot words at `+110` and their current matrices. Those
words already belong to the AREA01 actor/model views. A read-only provider
now borrows that storage for the current target, with the existing shared
slot arena, and exposes the exact regions to the existing aim world owner.
No target record is synthesized; absent, unheld or incompatible regions
still fault. The shared scene file adds only the forwarding accessor.

The original oracle compares 832 cases over 16 captures and eight model
identities, with 2,784 ordered stores/calls and 22 provider refusals. It
executes the actual target-region composition and compares whole RAM.
All 20 original model branches, active/inactive views and private/shared
projections are covered. Existing actor-view references pass; the aim-world
harness passes 100 ASan/UBSan checks, including missing-provider refusal.

The custom input driver now keeps adjacent hold steps continuous. Its
previous extra neutral frame released R1 immediately before Circle, so
completed aim input did not actually fire. A sanitizer-backed harness
checks the exact nine-frame R1 / R1+Circle / R1 sequence. This corrects
test input only; the game's original trigger mapping is unchanged.

The isolated staged aim tree passes a zero-warning build, startup at
`9.599849`, default smoke and all touched references. Its corrected native
probe completes aiming and two trigger presses with both aim and firing
observed, without a fault. This proves the actual probe, not every impact
surface or enemy-damage branch. Receipts:
`index-aim-{build,startup,smoke,references,harness}.log` and
`index-aim-exploration/summary.json` under `build/level2-crashes/`.

## Room interactions and resource boundaries

Control-room and east-door paths now compose existing placement, fade,
sound, room-script and flag-80-list owners (`00182F90`, `001B0C00`,
`001BBD60`, `001BB400 / 001BB7C0 / 001BB7F0`, `001B1DE0`). The terminal's
alignment uses existing `001B6F00`. Original transition proof passes 304
calls / 400 ordered boundaries across 16 captures and four refusals.
The list publication retains the existing allocator/order/refusal rules.

AREA01 music and message voices are added to the local stream-sector
export. Its 23,725 sectors compare byte-for-byte with the user disc; all
12,008 prior sectors remain identical, and the source tables match all
16 captures. Stream forwarding reuses the current lane owners and drive
policy. Re-export with `python3 tools/export_streams.py` in an isolated
asset directory. This worktree's `assets/streams` is a real directory.

The successful spawn now publishes all four original rotation words,
not only the yaw. Actual native publication compares with `001B07C0` in
320 cases over 16 captures. The later camera seed still loses its fourth
lane: original `0018CBD0` writes all four words, but protected
`em_camera.c`'s seed API and `em_camera_live.c`'s publication copy only XYZ.
Four original comparisons prove the stale `camera+3C` word. Repair requires
those protected owners; no compensating camera write is added here.

With PS2 drive timing selected, the control-room conversation matches all
1,435 recorded rows; its whole-camera tail still exposes that protected
fourth-lane defect. Default host-speed audio finishes the line six frames
earlier. Both native runs complete without a fault. Control-room pickups
also complete with captured positions, item fields and progress; request/
message timing differs while uncaptured module reads answer at host speed.
These are scoped observations, not full-checker parity passes.

The terminal's two pose quadwords `00810710..0081072F` now use the existing
canonical progress owner and its original reset. A 192-case original
`00159B90` prefix proof includes 288 actual SDK copies and 16 missing-window
refusals; ASan/UBSan checks aliasing, bounds, lifetime and reset. This does
not enable save serialization or load-game state.

The original `00157F60` request callback now shares one scalar owner with
the existing AREA11 panel wrapper and posts its fields through canonical
views. Full reference coverage is 1,548 cases plus 33 access-failure cuts;
existing panel tests pass. The native east-room route reaches entry 8 and
677 exact captured rows, posting request `6/80` and the terminal owner.
The next status boundary `0020E060` still accepts only the AREA11 panel;
request-6 status composition remains separate work. No decline, return or
accepted save is claimed from this intermediate receipt.

A closed-loop input probe reaches the actual duct trigger before calling
Use. It exposed `00188610`; the row selector now reuses the existing ROOM
owner and an original four-byte table added to the local boot-script
export. The leaf passes 273 existing original cases plus 160 real-runtime
cases over all 16 captures. Regenerate with
`python3 tools/export_area01_boot_scripts.py`; this worktree's
`assets/area01_boot_scripts` is a real directory. The subsequent crawl
path still requires independent proof.

Shared edits for this group: the Makefile builds
`em_area01_transition_services.c` and adds its reference target;
`em_scene_bindings.c` publishes spawn rotation, adds/clears/registers the
row-selector callback, and corrects its comment about terminal pose storage.

The exact staged room tree passes its zero-warning rebuild, startup at
`9.599849`, default smoke, all touched reference suites and the exploration
harness. Its control-room replay completes with all 1,435 recorded row
fields exact under PS2 drive timing; whole-camera parity retains the
protected limitation above. Receipts: `index-rooms-{build,startup,smoke,
references,references-tail}.log` and `index-rooms-exploration/summary.json`
under `build/level2-crashes/`.

## Terminal confirmation and decline

The next status boundary is now bound. Request 6 borrows the actual live
type-38 terminal fields, runs existing `00225A00` through the original
memset boundary on scene-owned `00810040..00810113`, and enters the existing
BATTERY confirmation via the original `0020CDC0` cold branch. Its default
No selection and return use the existing page owner. Accepting still reaches
unbound phase 6 / `00225AC0`; no card I/O or load-game state is provided.

The canonical reset compares 48 original cases over 16 captures. ASan/UBSan
checks exact aliases, bounds, lifetime, 256 actor types, signed costs and
free/reuse. The status-page full reference passes 3,576 cases plus nine
refusal branches; BATTERY covers 438 directed and 147 composed frames,
including terminal decline. Existing status-runtime checks pass. The native
PS2-timed replay matches all 1,225 captured rows, opens the prompt and
returns through entry 9 without a fault. Whole-checker results are recorded
separately from this row comparison.

Shared edit: `em_scene_bindings.c` adds only the terminal owner-read/reset
forwarders and their header include; there is no Makefile change. The
exploration fixture also retains a short, input-only vent entry/crawl probe
and recognizes the primary “no translation” fault before its later generic
coordinator message.

The untouched full smoke checker passes 21 live phases through the terminal
route, including all 1,225 rows and the whole ending camera. Native RNG
continuity passes; unavailable original per-call AREA01 RNG and differing
endpoint seeds retain the existing diagnostic-only status. Receipt:
`after-terminal-decline-ps2/a01_s4/check.log`. The exact staged tree also
passes a zero-warning rebuild, startup displacement `9.599849`, default
smoke, all touched references, and a fresh complete 1,225-row terminal
replay. Receipts: `index-terminal-{build,startup,smoke,references}.log` and
`index-terminal-exploration/summary.json` under `build/level2-crashes/`.
