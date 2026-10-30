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
The later asset and registry work is recorded in LEVEL_ASSET_DELIVERY.md;
missing fork recordings and AREA00 live phases remain subsequent packages.

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

## Ownership of 00185420 and 00184D20 (review 2026-10-19)

Main's live `em_item_device_find` (`src/game/em_item_device.c`,
[ITEM_DEVICE_LOOKUP.md](ITEM_DEVICE_LOOKUP.md), `make test-item-device-reference`)
already implements the battery-item branch of both originals over a frozen
published list. `tools/level_readiness.py` records it in `ALIASES` as a
partial owner of `boot:00185420` and `boot:00184D20`, so the readiness map now
lists it beside the level-15 translations. Rule for wiring level 15: the
full-scope translations `em_level15_support_00185420` and
`em_level15_perception_00184D20` become the single owner of both originals;
`em_item_device_find` then delegates to them instead of keeping a second
implementation (a main-owned change that must keep the first-level smoke and
`test-item-device-reference` identical). Until then neither level-15 module
is wired, so the live path has one owner.

The same review scanned every unverified/missing row for a definition whose
directly preceding comment names that original while the function name has
no address suffix. Besides `em_item_device_find`, every hit is either an
already-listed owner's file, a documented native boundary whose substitute
module the row already names (EE sound library, C runtime string/memset,
2D GS layer, movie glue, module loader), or a caller/hook wrapper.
`0020F2A0` stays missing (assembly-only definition; the live ITEM root uses
exported artwork, not a translation).

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

## Level-15 movement dispatcher and motion

The unwired `em_level15_motion` adds `0013D600`, `00140C30` and
`00140F80`. The dispatcher calls the two translated motion routines directly;
the remaining callees are explicit fail-stop worker contracts. The three C
bodies and the readable `0011E0A8` helper definition were inspected. An
assembly-only helper was accidentally printed during a source read; it was
not copied into the native sources, staged or committed. Subsequent source
reads reject assembly bodies before displaying C.

The full suite passes **237 cases and 670/670 reachable words**, with all
original comparisons enabled and exhaustive fail-stop checks. Cases cover
all dispatcher states, movement states, animation tables and wrap boundaries,
facing probes, water effects, non-finite phases and workers that change fields
read later. Captured boot text and dispatcher tables are checked against the
ELF; the calls themselves are designed inputs over retained AREA03 snapshots.

Original execution establishes the two local float slots at entry SP minus
8 and 4, including the dispatcher's nested frame adjustment. The native C was
corrected to retain the vertical arithmetic result and read acceleration before
speed. Adversarial worker-write cases also pass. Local-float-slot, probe-angle
and state-dispatch mutations are detected
(`build/levels/level15-motion-mutations-1/results.json`). The full receipt is
`build/levels/oracle-level15-motion-full-1/`. The staged-index build passes
without warnings (`build/levels/level15-motion-index-build.log`), with clean
staged leak/provenance checks. No live source or source list changed.

## Level-15 perception predicate

The unwired `em_level15_perception` adds `00184D20` from its byte-matched C.
The full original-instruction sweep passes **1,058 cases and all 323 reachable
words**. The five other words in the static graph belong only to shape 1's
class-10 branch: the retained class has already passed a gate accepting only
4 or 6. That exclusion is explicit and checked, with every class byte and
worker writes that change the stored class covered. No oracle comparison is
disabled. Cases also cover event/subtype gates, all shapes, height/radius/angle
boundaries, non-finite inputs, actual original math helpers, poisoned inputs
and every failure site. Calls are designed inputs over retained captures.

The oracle corrected parameter-pointer reuse and memory-read ordering for
shapes 1 and 2: one pointer serves both horizontal components, and later
radius/height/heading reads retain the pointer until the next worker call.
Event-gate, fixed-radius and retained-parameters mutations are detected
(`build/levels/level15-perception-mutations-1/results.json`). The full receipt
is `build/levels/oracle-level15-perception-full-1/`; the staged-index build
passes without warnings (`build/levels/level15-perception-index-build.log`).
Staged leak/provenance checks pass. No live source or source list changed.

## Level-15 particle emitter

The unwired `em_level15_particles` adds `0015A750` from its near-match C.
The full suite passes **258 cases and 214/214 reachable words**, including
fourteen captured actor records, every valid kind, RNG/phase boundaries,
out-of-range kinds, original matrix workers, worker-written fields and the
full fail-stop sweep. The original descriptor is at entry SP minus `0x60`.

For out-of-range kinds the original retains the caller's incoming count in
`s1`; the native API exposes that count explicitly. The oracle seeds only that
entry context, then executes the original function unchanged. Cases verify
negative, zero, one and multiple-emission counts. Valid kinds replace the
incoming value. Any future binding must supply the original caller context;
this is not a live binding or an invented default count.

The callee definition exposed a fifth argument omitted from the near-match
call declaration. Original execution confirms submission copy mode `1`.
The oracle also corrected frame-delta-before-RNG reads and the phase reload
when wrapping after the final increment. Copy-mode, incoming-count and
phase-reload mutations are detected
(`build/levels/level15-particles-mutations-1/results.json`). The full receipt
is `build/levels/oracle-level15-particles-full-1/`; the staged-index build
passes without warnings (`build/levels/level15-particles-index-build.log`).
Staged leak/provenance checks pass. No live source or source list changed.

## Shared entry proof and AREA19 reuse

Fresh quick runs of the existing `aim_fire_control`, `aim_fire_lamp`,
`aim_fire_target` and `status_pages` suites pass. Their original assertions and
source files are unchanged; the private receipts are
`build/levels/oracle-shared-<suite>-1/`. The source-fingerprinted receipts are
now included in this audit, so their explicit entry tables can support the
readiness map. Custom or implicit test entries still are not inferred from
filenames, hook tables or labels.

The new `area19_partner` suite executes AREA19's own `008298D0` text against
the existing `em_area01_ovl_00828850` implementation. The AREA19 C definition
was read and each retained capture's loaded overlay checked against AREA19.BIN.
The full suite passes **378 cases and 72/72 reachable words**, including all
state bytes, signed timer boundaries, unsigned identifiers, aliases, worker
writes and exhaustive fail-stop checks. Timer-boundary, partner-delay and
identifier-sign mutations in private copies of the native source are detected
(`build/levels/area19-partner-mutations-1/results.json`). No shared native
source changed. The full receipt is `build/levels/oracle-area19-partner-full-1/`.
The opt-in target is `make test-area19-partner-reference`.

This closes the owned standalone-translation portion of step 2: sixty missing
route routines were added with full original-instruction proof, and the
existing later-level suites have current receipts. Four decomp source blockers
and the separately listed shared live-code evidence gaps remain explicit.
The staged-index build passes without warnings
(`build/levels/shared-proof-index-build.log`), as do staged leak/provenance
checks. No live binding or default test group changed.

## Remaining step-2 work

The map currently retains **4 missing non-boundary entries**, after adding the
sixty return/arrival/continuation/support routines and correcting the existing `001FC520` ownership to
`em_sfx_service_release`. The latter has an existing original-instruction test
inside `test_area11_sfx_reference.py`, but its custom entry path is not yet a
fresh receipt in this audit. It remains unverified here; no duplicate translation
was added.

The missing group is four level-15 boot routines whose definitions contain
assembly rather than usable C bodies. All missing route entries with usable C
now have unwired translations and fresh full oracle receipts. The four source
blockers still need C recovery and oracle cases, not a status promotion. The complete addresses, membership and earliest
recorded beat are retained in the readiness report and its ignored JSON output.

The 82 remaining unverified entries also include shared first-level definitions
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
make test-level15-motion-reference
make test-level15-perception-reference
make test-level15-particles-reference
make test-area19-partner-reference
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

The 82 shared live-code entries still marked unverified in the readiness map
need evidence at their exact original entry or binding boundary before a
status promotion. They include platform/audio/stream and frame owners, shared
player/state/render helpers and custom test paths outside this lane's owned
standalone modules. Existing tests are listed where their entry can be proven;
this lane proposes no unmeasured changes to those shared or protected owners.
There is no live diff requiring approval in this checkpoint.

## Texture-capacity dependency refresh (2026-10-18)

The stage-3 registry change updates `em_gfx.h`, which is in the conservative
transitive dependency closure of all 48 suites. Their old receipts correctly
become stale even though the standalone translation bodies are unchanged.
All 48 fresh quick suites pass under `build/levels/oracle-capacity-refresh-1/`,
and the metadata receipt is published. Every existing assertion and full-sweep
test is preserved; those exhaustive sweeps were not rerun. The regenerated
readiness map again uses the source-matching receipts.


## Part B: AREA06 dependency refresh (2026-10-20)

The Part B sections (2026-10-20/21) were written in a separate review lane
and squash-merged into main as `6c04534` (2026-10-28); that lane's worktree
is retired. The `build/level-oracle-audit/` receipts they name were ignored
files in it and no longer exist: re-run the named make targets and tools
before relying on the numbers. Part B touched no SPU2 material.

This follow-up audits the checked-in readiness table without regenerating it.
Its level 5–15 U rows collapse to 81 distinct boot entries; none is an overlay
entry. The per-level totals repeat shared functions. Original-entry evidence,
partial live bindings, and intentional hardware adapters are kept separate in
the follow-up sections below. Hooking a named entry does not verify its body.

A private fresh inventory also found 29 AREA06-suite entries whose old receipt
became stale through the conservative shared-header/source dependency closure.
The complete unchanged `test_area06_port_reference.py` full sweep passes:
3,649 cases, 6,227 original/native runs, 5,091/5,091 reachable words, 45,504 call
entries, 19,659 helper rehearsals, and 2,958 fail-stop runs across 109 cases.
All 30 suite entries retain their existing assertions, including the shared
callee reuse checks. No translation defect was found in this refresh.

Receipt: `build/level-oracle-audit/area06-evidence.json`; log:
`build/level-oracle-audit/area06-full/area06_port.log`. The receipt's dependency
SHA-256 is `8163b0a8d2445bd65243ac02c8b2ff7f89aeade060d46843940a4f28ce392db0`.
It is private metadata, not a replacement for the lead's readiness publication.

```sh
python3 tools/level_oracle_audit.py --run build/level-oracle-audit/new-area06-run \
  --full --suite area06_port --evidence build/level-oracle-audit/new-area06-evidence.json
```

Requests for main: use this fresh receipt when reconciling the readiness map.
No AREA06 production diff or live wiring change is needed from this rerun.


### Part B native regression receipt

The unchanged application passes `newgame-control` at **9.599849**,
`make test-level-smoke` (three phases), and
`make test-level-smoke-ps2-drive` (20 phases through AREA01 arrival).
Both tick logs are byte-identical to the preserved `6753747` baseline:
65,543,433 host-timing bytes and 458,392,518 PS2-drive-timing bytes. Status
presentation checks pass as well. Runs were headless. Receipt and logs:
`build/level-oracle-audit/native-gates/`; baseline:
`build/level-oracle-audit/baseline-6753747/`.

The staged AREA06 documentation checkpoint also builds in isolation without
warnings (`build/level-oracle-audit/index-area06-build.log`). Later follow-up
translations remain outside the application source list; their tests are
opt-in. No protected production file, default audio behavior, or smoke
assertion changes in this Part B work.


## Part B: level-5 shared initialization and audio (2026-10-20)

`em_level5_state.{c,h}` adds complete **unwired** translations of the readable
C definitions for `001D0660` and `001E7780`. The former always runs effects
initialization before the area dispatcher. The latter preserves all 40 exact
area/sub-area keys, the key-zero argument remap to 1, the six global resets,
the signed record count, and the wrapping `0xA060` record stride. Its sixteen
overlay workers and effects worker remain explicit boundaries. The protected
AREA00/AREA01 owner and live scene adapter are unchanged.

The unchanged level-14 harness compares both original entries and the nested
original dispatcher with the new C: calls and arguments, every memory access
in order, whole memory at calls and return, poisoned starts, worker mutations,
unmapped/misaligned stops, and first-fault behavior. **65,745 full cases pass**,
including all 65,536 selectors; **192/192 reachable words** execute. Five
intentional mistakes (zero-key argument, stride, signed count, read order,
and omitted effects call) are rejected. Logs are
`build/level-oracle-audit/level5-state-chain-full.log` and
`build/level-oracle-audit/level5-state-mutations-final.log`.

Cause caught while translating: the original reads the sub-area byte between
the third and fourth global reset, earlier than the C statement order suggests.
The first candidate failed the ordered-access assertion; the final candidate
preserves that order. This was fixed in the new unwired file. The current live
owners' narrower scene/area scope is a binding limitation, not silently
certified by the new candidate.

`test_level5_shared_audio_reference.py` adds an exact-entry audit for all 25
shared audio U entries. Sixteen have passing original/native comparisons:

| Group | Original entries |
|---|---|
| Queue / sound library | `001157F0`, `001193A8`, `00119400`, `00119450`, `001194B8`, `00119528`, `001199F0` |
| Stream lanes / allocator | `00119828`, `0011A2B0`, `0011A4E8`, `0011A608`, `0011A658`, `001F9820`, `001FB0B0` |
| Actual scene stop / service release | `0011A070`, `001FC520` |

Full receipts cover 680 sound-library cases, the 8/9-call bank chains and two
rejected timing mutations, 315,956 stream cases, 18,900 lockstep frames across
27 captures, 400 captured stream fields, 1,000 stop cases, six release cases,
and 333 EE allocator-suite cases (315 hit the selected allocator entry).
The stop test compiles the actual protected scene and SFX forwarding bodies
read-only into an ignored fixture, then executes the real driver routine.
No existing assertion is changed and no production defect was found.

Nine U entries remain **intentional hardware adapters**, despite direct
original-wrapper probes: `0010BAA0`, `0010BBC0`, `0010BBE0`, `0010BC00`,
`0010F8F8`, `0010F968`, `00112610`, `00112D18`, `00113280`. Their lower
syscall/RPC results are scripted; this does not prove host heap, DMA, or CD
hardware equivalent to the original SDK. The complete quick run asserts that
all 25 target entries actually execute; a hooked name cannot satisfy it.
Per-entry scopes, counts, source digests and receipt digests are in
`build/later-oracle-audio/handoff.json`; component full reports and the final
`all-quick-report.json` are in that same private folder.

```sh
EM_TEST_FULL=1 EM_TEST_JOBS=2 make test-level5-state-reference
EM_TEST_FULL=1 EM_TEST_JOBS=2 make test-level5-shared-audio-reference
```

Requests for main: retain the nine adapters' boundary classification. Review
the new complete initialization functions for future later-area binding once
the AREA00/revisit work is free; provide canonical memory and overlay workers
rather than broadening the current partial owner by inference. Nothing here
wires those functions or changes shared audio behavior. The native regression
receipt above applies to the unchanged application.


## Part B: level-15 shared services and source blockers (2026-10-20)

`test_level15_shared_services_reference.py` keeps all existing comparator
assertions and adds exact native-body fixtures where older tests only hooked
the wrapper. All eleven full lanes pass. Twenty entries have original/native
body comparisons:

| Lane | Entries | Full result |
|---|---|---|
| GS clear / copy | `001008C0`, `00121870` | 200 clear cases; 12 copy cases, whole RAM and scratchpad |
| Status / loader | `001AFF90`, `001FF590`, `001FF830`, `001FFCD0`, `001FF080`, `001FF0D0`, `00200730`, `00200830`, `00200890`, `00200970` | 175 pool cases; 161 dispatch cases / 159 whole loads; 124 area dispatches / 7 loads; 25 poll, 12 DMA, 184 packet/restore cases; complete New Game loader states |
| Aim / pickup | `001B1510`, `001C47E0` | 7,360 aim-leaf cases; 240 inventory-consume cases |
| Messages | `001FD0E0`, `001FDDB0` | 52,227 line-walker calls, both outcomes of all 14 branches; 4,656 cue calls, with the two existing structurally one-sided branches documented |
| Draw kernels | `001D3900`, `001D3CF0` | 218 indicator cases, including 110 clipped cases |
| Collision | `001A5C30`, `001A56A0` | existing segment suite; new 4,288-case column oracle, both return outcomes and all six output words bit-identical, no callee hooks |

The two actual native draw forwarding bodies `001D3990` / `001D3D90` also
match their original entry's lower-call arguments: channel 3 and the resolved
model address. Their 36 delegations and six native refusals pass. Model
resolution and host channel persistence remain separate boundaries. Three
private deliberate native mutations are rejected. No shared production
mistranslation was found in these compared entries.

The actual pad bodies are tested but **not promoted to complete original
translations**. `00110B80` has 1,024 original status combinations and the
native stable-port case plus four refusals; the native deliberately represents
only connected, stable port 0/slot 0. `00111018` has 1,024 original actuator
payload/native motor pairs plus four refusals; the RPC lifecycle is a hardware
boundary. The existing live scene adapter `001D0660` has 32 branch/fault
contract cases, but remains conditional on host scene predicates; this is
separate from the complete unwired candidate proved above.

Per-entry native symbols, limits, counts and source hashes are in
`build/later-oracle-services/handoff.json`; eleven full lane logs/JSON receipts,
`summary.json`, and `mutation-controls.json` are retained beside it.

```sh
EM_TEST_FULL=1 EM_TEST_JOBS=2 make test-level15-shared-services-reference
```

### Requests for main: four ownerless source blockers

Rechecking the actual definitions confirms that `0013D2D0`, `0013D850`,
`00141C90`, and `0020F2A0` still have assembly-only bodies. These are the four
missing non-boundary game entries in the later-route inventory, all on level
15; the other missing rows are native/SDK boundaries. No usable decomp C was
found, no behavior was inferred from names, and no original instruction
listing was displayed or copied. Definition hashes and marker counts are in
`build/level-oracle-audit/source-definitions.json`.

The user explicitly selected “Record source blockers in Requests for main,
then continue A2” on 2026-10-20. Recover readable C for these four definitions
in the decomp, then translate and run their original-entry oracles. They
remain missing. Keep the two pad adapters classified as hardware boundaries;
do not infer complete later-scene readiness from the existing scene adapter's
contract tests. No protected source or readiness document was changed.


## Part B: level-14 shared player functions (2026-10-20)

All 30 shared player U entries now have an original-entry audit. Twenty-nine
pass the unchanged existing comparators, with an observation-only entry
counter proving that the original body executes rather than being hooked:

| Suite | Selected entries | Full result |
|---|---|---|
| Closure | `001696A0`, `00169730`, `0016A4B0`, `0016A8B0`, `0016ADE0`, `0016AE40`, `00175390`, `001787B0`, `001811F0`, `00181430`, `001814E0`, `00181730`, `001818D0`, `00181950`, `00181A70`, `00181B80`, `00181BA0`, `00181E20`, `00181F60`, `00182090`, `00182100` | 200,000 state cases; 719/719 feasible conditional outcomes over 32 routines; 791 worker faults; 200 missing-worker/storage refusals |
| Ladder | `0017FD80`, `0017FE00`, `0017FF00`, `00180600`, `001809B0` | 400,000 state and 100,000 helper cases; 378/378 outcomes; 1,316 worker faults; 41 missing-worker refusals |
| Major2 | `00181D70`, `002230A0` | 5,670 fixed and 30,000 random cases; 143 path classes; 28 dispatcher slots; 168 fail-stop checks |
| Healing | `0015C700` | 2,205 cases; 540 identical animation requests |

The closure suite retains its three documented impossible outcomes. These are
shared-body proofs, not new later-level route replays: the original first-level
16-beat census did not reach those closure states. Per-entry execution counts,
source hashes and scopes are in
`build/later-oracle-player/player-oracle-handoff.json` and the four
`{closure,ladder,major2,heal}-full.json` receipts beside it.

```sh
EM_TEST_FULL=1 EM_TEST_JOBS=2 make test-level14-shared-player-reference
# Expected-failure check: passes while the strict oracle rejects the unfixed
# live owner with exactly the pinned failure signature; fails on any change.
make test-level14-player-frame-reference
# Strict regression reproducer: exits nonzero on the live owner until main
# applies the proposal below. Opt-in; no aggregate target runs it.
make test-level14-player-frame-strict
```

The full `0015BCF0` frame oracle is deliberately a separate opt-in target
(`test-level14-player-frame-strict`). `test-level14-player-frame-reference`
wraps it as an expected-failure check
(`tools/test_level14_player_frame_expected_failure.py`): it runs the unchanged
strict oracle and passes only when the comparison completes (a harness error
is not accepted as the expected failure), all 1,384 cases run with 44/44
outcomes, no case matches, and the mismatch signature is exactly the pinned
one. The pin has two layers, both measured from the oracle's own report
(call addresses and record offsets only): tables of the distinct mismatch key
sets (710 / 425 / 249 cases), call orders (6), final-record offset sets (5),
publication slot sets (`[5,7]` x1334, `[5,7,9]` x50), boundary record offsets
(14) and boundary argument mismatches (`0011A070` x425), each with its case
count; and a SHA-256 over every case's index, inputs and mismatches. Any
added, removed or moved difference, so a new owner regression as well as a
partial fix, fails the check and prints the changed signatures. Two
sensitivity controls run after it: a private include root
(`build/later-oracle-player/regression-control`, removed afterwards) with a
production `em_player_frame.c` copy that also flips record byte 0x10 must be
rejected as a signature change, and the private proposal tree
(`build/later-oracle-player/proposed-include`, skipped when absent) passes the
strict oracle and must be rejected as "now matches". When main applies the
proposal, retire the wrapper and point the target back at the strict oracle.
The strict oracle compiles the actual frame,
stage glue, pose selector/publication and stage-tail sources, and runs the
complete pinned original wrapper. Stage, skeleton, vitals, camera, footstep
and stop are symmetric scripted callee boundaries; the original copy helper
executes. It compares call order, every actor byte at each boundary and return,
carry/hip/Euler publication, and the canonical busy byte. Unknown native host
branches abort. Scope is the bound translated-stage path (damage-state zero,
locomotion/display enabled); it does not certify the legacy path or the
scripted callee bodies.

The current production owner is **FAIL** and remains U. Confirmed causes:
camera precedes animation; kill-plane/loop-stop runs before its original
place; vitals runs after footstep; final actor `+B0` retains feet instead of
bone-1 position; published Euler X/Z are zeroed; and the original stage busy
state is replaced by legacy host locks. These independent differences are
not normalized away. The exact protected-owner proposal and its final private
validation are recorded below; no shared production file is edited here.

### Requests for main: protected player wrapper

The current production wrapper fails all **1,384 cases**; the private proposal
passes all 1,384 with **44/44 conditional outcomes**. Four cases explicitly
change the stage child busy output in both directions. Before canonical B3
publication was added, those four cases failed while the other 1,380 passed
(`frame-busy-before-fix.json`). Merely preserving the previous busy byte is
insufficient: the completed stage view must publish its result.

A deliberate change to the exported Euler-X result buffer is rejected in all
1,384 cases, solely at that output. This is comparator-sensitivity evidence,
not a production-source mutation. Independent review also removed an unrelated
`player_pose_align()` edit; the proposal generator now asserts the entire
suffix following `player_pose_finish_palette()` remains unchanged.

Final receipts are `build/later-oracle-player/frame-production.json`,
`frame-proposal.json`, and `frame-mutant.json`; their source hashes and the
private proposal are indexed by `player-oracle-handoff.json`. Proposal SHA-256:
`7a537098195528269490483f673950f966757f9f4d9fd95f45bf58083bd6bd85`.
The exact unapplied diff follows. Main owns these files; apply only after its
current work is free, then rerun the shared player/scene/pose regressions and
all native gates. The unchanged-app smoke receipts do **not** validate this
proposal in a live route. The production entry remains U.

```diff
--- a/src/game/em_player.c
+++ b/src/game/em_player.c
@@ -18,6 +18,7 @@
 #include "game/em_player.h"
 #include "game/em_ee_float.h"
 #include "game/em_effect_color.h"
+#include "game/em_glue_original.h"
 #include "game/em_player_closure_live.h"
 #include "game/em_player_floor.h"
 #include "game/em_player_heading.h"
@@ -1219,37 +1220,40 @@
         live_fault("0015BA50 D_008106F1/D_00810CB6 not bound");
         return;
     }
+    /* Publish the completed 0015BA50 busy result before the wrapper tail. */
+    em_scene_state()->req[EM_SCENE_REQ_B3] = live.scene.busy;
     /* 0015B130 / 00182D70 write 0x70003B8F through the stage's view. */
     if (live.scene.spad3B8F != live.loaded3B8F) em_scene_state()->spad3B8F = live.scene.spad3B8F;
     /* 00183250's exit (+4 = 5's walk-out) clears 0x70003B8D through the view. */
     if (live.scene.spad3B8D != live.loaded3B8D) em_scene_state()->spad3B8D = live.scene.spad3B8D;
-    /* 0015BCF0 after 0015BA50: +BC, the -200 check and the loop-sound stop. */
+    /* 0015BCF0: establish position W, then evaluate before vitals,
+     * camera and footstep. The kill-plane and loop-stop tail follows them. */
     for (unsigned axis = 0; axis < 3; ++axis) em_live_set_f32(&live.a, 0xB0 + 4 * axis, g.pos[axis]);
-    if (em_player_stage_tail(&live.a, &live.b.stage) < 0) {
-        live_fault("0011A070 worker fault");
-        return;
-    }
-    /* 0015BCF0's 0015CBA0: the state byte +1F0 -> the action code +230 the
-     * camera dispatches on (em_camera_leftovers' translation; it reads +1F0,
-     * +1F1, +236 and +0D, none of which the tail writes). */
-    if (em_camleft_0015CBA0(&live.a) < 0) {
-        live_fault("0015CBA0 fault");
-        return;
-    }
-    /* 0015BCF0's animate step (after +BC = 1.0, which the tail writes; the
-     * -200 check and the loop-sound stop touch none of its inputs): the
-     * record's skeleton at its +B0 / +C4. */
+    em_live_set_u32(&live.a, 0xBC, 0x3F800000u);
     em_live_set_f32(&live.a, 0xC4, g.yaw);
     if (player_pose_animate() < 0) {
         live_fault("0015BCF0 skeleton evaluation fault");
         return;
     }
-    /* 0015BCF0's 00187350 after the evaluation (its 0015CF90 runs in
-     * em_player_0015BCF0; neither reads what the other writes): the step
-     * sounds and surface effects from the record's clip clock, +1F0's
-     * mailbox and nodes 17 / 18 (census L12). */
+    EmSceneState *scene = em_scene_state();
+    const EmGlueVitals vitals = {em_scene_progress_at(scene, 0x00810706u, 1),
+                                 em_scene_progress_at(scene, 0x00810707u, 1),
+                                 (uint8_t *)&g.status.health, (uint8_t *)&g.status.infection,
+                                 &scene->req[EM_SCENE_REQ_B9]};
+    if (em_glue_0015CF90(live.a.bytes, &vitals) < 0) {
+        live_fault("0015CF90 fault");
+        return;
+    }
+    if (em_camleft_0015CBA0(&live.a) < 0) {
+        live_fault("0015CBA0 fault");
+        return;
+    }
     if (loco_live() && em_player_closure_live_footstep(&live.a) < 0) {
         live_fault("00187350 worker fault");
+        return;
+    }
+    if (em_player_stage_tail(&live.a, &live.b.stage) < 0) {
+        live_fault("0011A070 worker fault");
         return;
     }
     stage_position_publish();
--- a/src/game/em_player_frame.c
+++ b/src/game/em_player_frame.c
@@ -514,6 +514,8 @@
     actor_update();          /* func_0015BCF0 — player actor update   */
     player_pose_finish_palette(); /* original hip/Euler scratch publication */
     player_struggle_tick();  /* legacy bug-latch struggle (mash CROSS) */
+    if (g.pd_state != 2 && player_states_stage_live())
+        return 0; /* Preserve the original stage busy byte and its vitals call order. */
     EmSceneState *scene = em_scene_state();
     scene->req[EM_SCENE_REQ_B3] = (uint8_t)(player_damage_locked() ||
                                             em_examine_input_locked() ||
@@ -522,8 +524,7 @@
     const EmPlayerLiveActor *record = player_states_actor();
     if (!record)
         return -1;
-    if (g.pd_state == 2 || !player_states_stage_live())
-        player_states_vitals_publish();
+    player_states_vitals_publish();
     const EmGlueVitals vitals = {em_scene_progress_at(scene, 0x00810706u, 1),
                                  em_scene_progress_at(scene, 0x00810707u, 1),
                                  (uint8_t *)&g.status.health, (uint8_t *)&g.status.infection,
--- a/src/game/em_player_pose_host.c
+++ b/src/game/em_player_pose_host.c
@@ -787,8 +787,8 @@
      * scratch vectors after the player callback, including script-owned ticks. */
     memcpy(source.hip, g.player_palette + 16 + 12, sizeof source.hip);
     source.hip_valid = 1;
-    source.saved_euler[0] = source.saved_euler[2] = 0;
-    source.saved_euler[1] = g.yaw;
+    memcpy(source.record.actor->bytes + 0xB0, g.player_palette + 16 + 12, 16);
+    memcpy(source.saved_euler, source.record.actor->bytes + 0xC0, sizeof source.saved_euler);
     source.saved_euler_w = em_live_f32(source.record.actor, 0xCC);
     source.saved_euler_valid = 1;
 }

```

### Part B rollup

The checked-in levels 5–15 inventory contains **81 unique U entries**:
67 existing native bodies/wrappers now have passing original-entry evidence
within their stated boundaries, two complete new unwired initialization
candidates pass, eleven intentional hardware adapters retain their model
classification, and the protected player wrapper remains a strict failing
oracle. Counts and limits are reflected in all eleven route documents.
The four ownerless level-15 game definitions remain source blockers, as the
user directed. No readiness regeneration, route wiring, shared production
repair, or audio integration is included.

The new initialization candidate fixes original memory-read ordering, preserving
observable reset callbacks; five deliberate source mutations are rejected.
The other confirmed translation defects are in the protected player wrapper
and are handed to main above. Existing native regressions remain New Game
**9.599849**, host smoke **PASS**, and PS2-drive smoke **PASS**, with both tick
logs byte-identical to the pre-B baseline (hashes recorded above).
