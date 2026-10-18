# Level 2–15 standalone oracle audit

2026-10-18, branch `levels`. **Step 2 is in progress.** All 31 pre-existing
standalone suites in the audit inventory pass their current quick modes. Four
missing routines on the AREA00 return now have a translation and a new original
instruction suite, which passes quick and full mode. Five further missing owners
in AREA04/06 now pass their expanded full suites. Ten more missing AREA13/15
routines pass two new full suites. The remaining AREA19 flame/turret owners
also pass a new full suite. All fifteen AREA03 route entries now pass two further full
suites. Thirteen level-15 boot helpers also pass a new full suite.
Four further creature entries and two rising-hazard entries pass two more
full suites. No new routine is wired.
Assets, missing fork recordings, the registry exception and AREA00 live phases
remain later work packages, in the requested order.

## What the fresh receipts establish

`tools/level_oracle_audit.py` runs the suites serially with test-specific
environment overrides removed, `EM_HEADLESS=1`, `EM_REFERENCE=fork` and two oracle
workers. Existing suites that explicitly declare a v2.6.3 fixture exception keep
that exception; the audit does not relabel these inputs as fork captures.

[LEVEL_ORACLE_EVIDENCE.json](LEVEL_ORACLE_EVIDENCE.json) contains the result,
mode, UTC completion, elapsed time, private log path and a SHA-256 fingerprint
of each suite's local Python imports, named native sources and transitive local
headers. It contains no game bytes. A changed dependency invalidates the receipt.
The tool checks the fingerprint before and after each run and retains failed or
interrupted results privately. Only a wholly passing requested run publishes
the metadata receipt. `--check` validates receipts; it does not run game code.

The readiness map now recognizes native `ENTRIES` tables as well as `FUNCS`.
`SIZES` alone, hooks, callee policies and function labels grant no proof. An
explicit entry gains a current-pass annotation only if its identified native
definition is in the passing receipt's dependency closure. Older census ledger
evidence remains separately identified as historical. A definition supersedes
an old SDK boundary classification instead of being counted as both.

The audited groups are AREA01 overlay/math/render/existing-render/sys/side/UI/
exit-A/exit-B/room/revisit; AREA00 overlay/low/world/HUD/FX/existing-FX; AREA02
overlay/math/misc/UI; AREA04, AREA22 and AREA06 port groups; and level 8–14 port
groups. No assertion or default test selection in these existing suites changed.
Their quick passes do not claim that their exhaustive sweeps were rerun.

## AREA00 return: four missing AREA01 entries

The full census records these functions in `a01r_00_to_train_room` and
`a01r_01_event`. They had run during earlier transition work, so delta-only
inventories did not supply their missing translations. The new
`em_area00_return.c` owns no memory: original addresses use an explicit byte view,
external calls require typed workers and the first fault stops later observable
accesses. It is built by its opt-in oracle target and has no live caller or
application source-list entry.

| AREA01 runtime entry | Behavior recovered from the decomp C | Reachable non-branch words exercised |
|---|---|---:|
| `00823A90` | Range gate, scratchpad look-at setup and resulting yaw | 130/130 |
| `00823CD0` | O3 owner setup, state dispatch, animation tail and effect exit | 201/201 |
| `008240E0` | Turn/wait state machine, random delay and range check | 118/118 |
| `00826010` | Sixth-tick probe, random skip, target test and hit effect | 85/85 |

The new suite reuses the level-14 comparator's checks without changing them:
original text identity against the user's local ELF/overlay, calls and arguments,
ordered loads/stores between calls, memory at call boundaries, final whole-memory
equality, poisoned starts, float bits and fail-stop behavior. The test executes
521 cases, including 30 captured actor records from the fork's AREA00 return and
AREA01 revisit. Designed cases cover state dispatch, gate boundaries, signed
halfword wrap, random/result lanes and callees that modify subsequently read
fields. The full run fails every reached call and memory access at its selected
contract sites. All 534 reachable words pass; there are no dead-word exclusions.

The first translation attempts failed the oracle on these concrete differences;
the fixes are in the C, with the comparison unchanged:

| Cause | Corrected behavior |
|---|---|
| Treating the scratchpad vector copy as three independent assignments | Load all three source components before the three destination stores |
| Evaluating the yaw arguments in source-expression order | Read current yaw before the goal yaw, as the original does |
| Reading the probe target before its timer, then rereading the timer | Read timer then target, store the increment, and reuse the original's subsequent timer load |
| Assuming a larger local stack slot | Place the probe's local rotation vector at entry SP minus `0x10`, preserving its untouched fourth component |

Four deliberate mutations were detected: range return value, turn timer branch,
probe cadence and owner animation scale. The guard checks also reject a NULL
table, NULL fault/result pointer and a fault latched on entry. Header checks
compare the typed hook order and signatures against the test table. The new
full-pass receipt is under `build/levels/oracle-return-full-1/`; the aggregate
receipts are under `build/levels/oracle-audit-1/`. These are private, ignored logs.

## AREA04 and AREA06 full-route extensions

The AREA04 module adds `00824320` and `00825510`; the AREA06 module adds
`00823B50`, `008240E0` and `00824340`. Their own docs describe the recovered
behavior and concrete translation corrections. The existing modules and tests
were extended additively, with every old comparison and coverage requirement
retained. Each new function also runs as an independent oracle entry.

| Module | Full cases | Reachable words | Native failure runs | Receipt |
|---|---:|---:|---:|---|
| AREA04 | 2,881 | 1,864/1,864 | 623 | `build/levels/oracle-area04-full-1/` |
| AREA06 | 3,649 | 5,091/5,091 | 2,958 | `build/levels/oracle-area06-full-1/` |

AREA06's existing reuse checks also pass, including 455 strip-node ticks,
6,000 overhead-bar state cases and status/message checks. Level 8 is rerun
because its local test dependencies include these modules; its quick rerun passes.
The expanded quick modes take 4.8 seconds (AREA04) and 7.9 seconds (AREA06),
with two workers on this Mac. Subset audits now
retain older stale receipts explicitly until their sources are retested.
The isolated staged-index build of this extension passes with zero warnings
(`build/levels/arrival-index-build.log`); the staged leak/provenance checks pass.

## AREA13 and AREA15 full-route extensions

The full census also finds four AREA13 and six AREA15 routines omitted by the
old delta inventories. New standalone `em_area13_route` and `em_area15_route`
modules implement their matching decomp C. They keep explicit workers and the
same borrowed-memory, typed-call and fail-stop contracts. They are built only
by opt-in test targets and have no live caller. The existing level-9/14 suites
and their worker boundaries are unchanged.

| Area | Runtime entries | Cases | Reachable words | Full receipt |
|---|---|---:|---:|---|
| AREA13 | `00823580`, `00823830`, `00823A40`, `00824A80` | 546 | 225/225 | `build/levels/oracle-area13-full-1/` |
| AREA15 | `00823B40`, `00823C80`, `00823F60`, `00824240`, `00824350`, `008243E0` | 562 | 326/326 | `build/levels/oracle-area15-full-1/` |

Both suites configure the existing level-14 comparator without modifying it.
They compare original text identity, ordered accesses, worker arguments,
call-boundary and final whole memory, poisoned starts, and every failed access
or call at the selected full-contract sites. Cases include actual captured
actors, all state/substate branches, signed countdown boundaries, flag/result
lanes, floating-point boundaries, and worker mutations of later-read fields.
All reachable words are exercised with no exclusions. Original captures are
explicitly selected from the retained v2.6.3 routes; these passes do not establish
fork recording coverage.

The AREA13 door first reads its state, then its owner pointer. The initial
translation reversed those reads; the oracle rejected it. AREA15's two
countdowns reload the signed halfword after decrement/store; retaining only the
local result omitted an observable read. Both translations were corrected,
with all comparisons retained. Four deliberate mutations were detected: the
door's owner flag, the cutscene completion flag, the camera callback's return
value, and the countdown boundary (`build/levels/route-mutations-1/results.json`).
The quick suites take about one second each with two workers on this Mac.
The isolated staged-index application build passes with zero warnings
(`build/levels/continuation-index-build.log`); the staged leak checks pass.

## AREA19 flame and turret owners

`em_area19_route_flame.c` implements `00824690`; `em_area19_route_turret.c`
implements `00827DD0`. Both are unwired, with explicit typed workers and an
opt-in target. The turret adapts this project's existing AREA01 translation
after a full reading of the matching AREA19 C: AREA19 has no flag-6 gate or
state-100 wait, and a failed collision probe chooses its effect from the hit
kind saved before the call. This is independently tested against AREA19's own
original instructions; the AREA01 source and its tests were not modified.

The new suite passes **714 cases, 1,395/1,395 reachable words** (217 flame,
1,178 turret), with every access/call failure at the selected full-contract
sites. The full receipt is `build/levels/oracle-area19-full-1/`. The original
flame packet and spark-vector addresses are entry SP minus `0x70` and `0x10`;
the first native attempt used offsets one slot too low and failed the call
comparison. Correcting the translation made the comparison pass.

Tests include real AREA19 actor records from the explicitly retained original
v2.6.3 route, designed flame state/flag/random/phase boundaries, and callee
writes to packet, phase and vector bytes. The existing adversarial AREA01
turret cases are also reused as **designed fixtures**: their local input data
is paired with the original AREA19 text, whose identity is checked. These
hybrid test images are never described as AREA19 captures. They preserve the
existing pointer-aliasing, floating-point and worker-mutation cases. Four new
missed-probe cases ensure the effect uses the saved hit kind even when the
worker replaces that byte. No code/data bytes are written into the repository.

All three deliberate mutations were detected: wrong missed-hit effect,
adding a wind-down gate, and changing flame phase speed
(`build/levels/area19-mutations-2/results.json`). No coverage exclusion or
comparison relaxation was introduced. The staged-index application build
passes with zero warnings (`build/levels/area19-index-build.log`), and the
staged leak/provenance checks pass.

## AREA03 initialization, callbacks and turret pair

The new `em_area03_route` module verifies eight entries: init `00823580`
(the census names the enclosing pad `00823540`), turret `00823930`, partner
`00825290`, lift-offset callback `00826270`, script-parameter callback
`00826330`, toggle `00826860`, and sound callbacks `00826880` / `008268B0`.
They are unwired. The matching AREA03 C was read, including its differences
from the AREA19 turret: AREA03 uses different effect scratch vectors and
always selects the generic effect for non-actor hits, without a second probe.

The suite passes **878 cases and 1,249/1,249 reachable words**, including the
full fail-stop sweep (`build/levels/oracle-area03-full-1/`). It retains every
comparison and uses actual original AREA03 actors plus explicitly designed
AREA01 twin fixtures carrying original AREA03 text. The callback cases cover
both toggle branches, non-boolean flags, signed/float bit boundaries and return
values. All inputs remain local; no binary data is committed.

Two observable ordering mistakes in the first translations were fixed in C:
the initializer publishes its table pointer after the other three stores; the
lift callback reads player/camera heights before publishing the spawn entry,
then stores those heights before reading/updating the target height. The
original's order is now checked one access at a time. The old harness's chosen
scratch range is occupied in AREA03, so this suite selects a different common
256 KiB zero window and retains the complete zero-memory assertion. It does
not clear captured data to satisfy that assertion.

Lift-height and turret-effect mutations were detected
(`build/levels/area03-mutations-1/results.json`). The staged-index native build
passes without warnings (`build/levels/area03-index-build.log`), and the staged
leak/provenance scan passes. This checkpoint established the first eight
AREA03 entries; the seven remaining entries are covered below.

## AREA03 gates, lift controls and turret probe

A separate `em_area03_arrival` module covers the remaining seven overlay
entries without changing the established turret suite's probe boundary:
`00824D30`, `00825430`, `008256E0`, `00825980`, `00825DF0`, `00826340`
and `008265D0`. All seven matching C definitions were read. The new module
remains unwired, with explicit typed workers and an opt-in test target.

The full suite passes **965 cases and 1,279/1,279 reachable words**, with no
coverage exclusions and the existing ordered-access, call-boundary memory,
whole-memory, poisoned-start and fail-stop comparisons retained. It includes
actual AREA03 actors from both explicitly retained v2.6.3 arrival captures,
all gate/lift state branches, phase/timer and collision boundaries, worker
writes to later-read fields, and an alias case in which the first gate bone
store changes the phase used by the second bone. Two six-entry dispatch tables
are verified against the local overlay before substituting native C dispatch.
The original probe's loaded tint stays borrowed data; no tint bytes are embedded.

The oracle rejected two gate translation errors: the work flag was read twice
instead of retained once, and the first bone update reread phase unnecessarily
while the second loaded the pointer and phase in the wrong order. The C now
retains phase for the first bone, then reads the bone-table pointer, reloads
phase and reads the second bone pointer. No comparison changed.

The full receipt is `build/levels/oracle-area03-arrival-full-1/`. Deliberate
mutations of the gate reread, lift timer and probe kind boundary were all
rejected (`build/levels/area03-arrival-mutations-1/results.json`). The staged
index application build passes without warnings
(`build/levels/area03-arrival-index-build.log`); leak/provenance checks pass.

## Level-15 boot support

The new unwired `em_level15_support` implements thirteen missing entries:
`001418F0`, `00153EA0`, `00185420`, `001A91C0`, `001BEB70`, `001BEC40`,
`001C2690`, `001CB200`, `001CB2B0`, `001D4440`, `001D4640`, `001D6580`
and `0020F170`. These cover the creature timer, release notifier, active-object
search, duration selector, child spawning, state notification, collision
transform and render-command construction.

The five translated render helpers call each other internally, and the oracle
executes those same original callees. They are also separate test entries.
This tests the entire `001CB2B0` → `001CB200` → `001D4640` → `001D4440` →
`001D6580` chain without substituting a hook for any translated member.
External workers retain typed, fail-stop contracts. The C declarations and
bodies of the external workers were checked where a near-match caller's
prototype was incomplete: `001D2090` needs the slot and reference pointer;
`001D2910` takes only its ID. The native calls use those actual contracts.

The suite passes **739 designed cases and 575/575 reachable words**, including
the full failure sweep, with no coverage exclusions. Inputs include actual
loaded boot tables from explicitly retained original captures; these are not
claimed as live recorded invocations. Cases cover signed timer wrap, all
selector branches, non-boolean flags, negative/unmapped searches, all child
allocation outcomes, a parent/child alias, float bit boundaries, 64-bit packet
arguments and workers that replace the render context or later-read fields.
The duration dispatch table is checked against the local ELF before native
C dispatch. The comparator is unchanged.

Two translation ordering errors were corrected: the timer/collision helpers
read Z before X when preparing the angle call, and the render chain reads its
context pointer before the mesh count. Deliberate mutations of timer boundary,
duration flag and saved command cursor are detected
(`build/levels/level15-support-mutations-1/results.json`). The full receipt is
`build/levels/oracle-level15-support-full-2/`. The staged-index build passes
without warnings (`build/levels/level15-support-index-build.log`); the staged
leak/provenance checks pass. No live source list or renderer file changed.

## Level-15 creature attack, initialization and damage

The unwired `em_level15_creatures` adds `0012F6C0` (attack/animation state),
`0013D490` (creature initialization), `00141A30` (damage and state selection)
and `00153B50` (hurt/death and knock-back movement). All four decomp C
bodies were read. External calls and the draw callback are explicit workers.

The full original-instruction suite passes **847 cases and 509/509 reachable
words**, including all fail-stop sites, with no coverage exclusions. Designed
cases cover state/clip/status combinations, setup results, signed counter
wrap, all damage flag branches, floating-point boundaries, initialization with
overlapping source/destination records, and workers that change later-read
state, positions, damage or movement speed. Captured boot text is validated
against the ELF; these designed calls are not claimed as recorded invocations.

The first knock-back translation read acceleration/velocity and speed/position
in the wrong order. It now reads acceleration before velocity, reloads velocity
before position, and reads speed before each horizontal position after the
trigonometry worker returns. The oracle checks each access; no comparison was
changed. Charge-scale, fall-clamp and attack-timer mutations all fail
(`build/levels/level15-creatures-mutations-1/results.json`). The full receipt is
`build/levels/oracle-level15-creatures-full-1/`. The staged-index build passes
without warnings (`build/levels/level15-creatures-index-build.log`) and staged
leak/provenance checks pass. Live source lists and protected files are unchanged.

## Level-15 rising hazard and children

The standalone `em_level15_fountain` adds `001BECC0` and `001BF3C0`, with
explicit workers and no live caller. Both near-match decomp C bodies were
read. Their call declarations were checked against the callees and then
against original execution: `001BEAC0` receives the owner and scratch vector,
`001C1570` receives the owner, and `001B1020` receives the child, model, clip
and frame. The near-match callers had omitted or misidentified these arguments.

The full suite passes **1,046 cases and 439/439 reachable words**, including
eight actual captured actor records from the retained AREA03 arrival inputs.
The other cases cover every parent/child state, phase/fade boundaries, damage
and cooldown flags, signed timers, non-boolean modes and workers that change
later-read fields or replace the child's parent pointer. The comparator is
unchanged, all reachable words are covered, and full fail-stop checks pass.

The oracle corrected redundant reads of phase/fade arithmetic results, the
cooldown and initial damage mask. It also established that the child retains
its parent state for the two comparisons and reads its mode before parent
height. The parent pointer captured before the bind worker remains in use
after that worker returns, even if the worker changes the object's pointer.
Rise-speed, child-cap and retained-parent mutations are detected
(`build/levels/level15-fountain-mutations-1/results.json`). The full receipt is
`build/levels/oracle-level15-fountain-full-1/`. The staged-index build passes
without warnings (`build/levels/level15-fountain-index-build.log`), with clean
staged leak/provenance checks. No live code or source list changed.

## Remaining step-2 work

The map currently retains **9 missing non-boundary entries**, after adding the
fifty-five return/arrival/continuation/support routines and correcting the existing `001FC520` ownership to
`em_sfx_service_release`. The latter has an existing original-instruction test
inside `test_area11_sfx_reference.py`, but its custom entry path is not yet a
fresh receipt in this audit. It remains unverified here; no duplicate translation
was added.

The missing group is nine level-15 boot routines: five have readable C,
and four still contain assembly rather than a usable C body. They need an explicit source/translation review and oracle
cases, not a status promotion. The complete addresses, membership and earliest
recorded beat are retained in the readiness report and its ignored JSON output.

The 92 remaining unverified entries also include shared first-level definitions
whose custom tests or live bindings are outside these standalone groups. This
checkpoint does not certify them from names or ask to change protected owners.

## Reproduce

Native arm64 macOS, with the user's local decomp inputs:

```sh
make test-level-readiness
make test-area00-return-reference
make test-area13-route-reference
make test-area15-route-reference
make test-area19-route-reference
make test-area03-route-reference
make test-area03-arrival-reference
make test-level15-support-reference
make test-level15-creatures-reference
make test-level15-fountain-reference
EM_TEST_FULL=1 make test-area00-return-reference
python3 tools/level_oracle_audit.py --check
# OUT must be a new directory; this explicitly runs the multi-minute aggregate.
make verify-level-oracles OUT=build/levels/new-audit
python3 tools/level_readiness.py --write --json build/levels/readiness.json
python3 tools/level_readiness.py --check
```

The application source list and default test groups are unchanged. No asset
export, emulator session or native gameplay smoke is part of this checkpoint.
The isolated staged-index native build passes with zero compiler warnings
(`build/levels/oracle-index-build.log`). The staged disassembly scan and the
explicit source/document/metadata provenance check pass; no disc-derived file
is staged.

## Requests for main

No live or protected source changed. Source work is needed in the read-only
decomp for four remaining boot entries: `0013D2D0`, `0013D850`, `00141C90`
and `0020F2A0`. Their current definitions have assembly bodies rather than
usable C. They remain missing; this lane has no proposed port diff for them
and does not infer their behavior from labels. The previously documented
AREA01 voice-timing discrepancy and AREA00 texture-capacity stop remain
live-phase prerequisites, not waived assertions.
