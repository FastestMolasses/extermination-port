# Level 2–15 standalone oracle audit

2026-10-18, branch `levels`. **Step 2 is in progress.** All 31 pre-existing
standalone suites in the audit inventory pass their current quick modes. Four
missing routines on the AREA00 return now have a translation and a new original
instruction suite, which passes quick and full mode. Five further missing owners
in AREA04/06 now pass their expanded full suites. No new routine is wired.
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

## Remaining step-2 work

The map currently retains **55 missing non-boundary entries**, after adding the
nine return/arrival routines and correcting the existing `001FC520` ownership to
`em_sfx_service_release`. The latter has an existing original-instruction test
inside `test_area11_sfx_reference.py`, but its custom entry path is not yet a
fresh receipt in this audit. It remains unverified here; no duplicate translation
was added.

The missing groups include AREA13/19 functions missed by earlier deltas, the AREA15 continuation and AREA03 arrival, plus the
level-15 boot routines. Some decomp definitions still contain assembly rather
than a usable C body. They need an explicit source/translation review and oracle
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

None from this checkpoint. No live or protected source changed. The previously
documented AREA01 voice-timing discrepancy and AREA00 texture-capacity stop
remain live-phase prerequisites, not waived assertions.
