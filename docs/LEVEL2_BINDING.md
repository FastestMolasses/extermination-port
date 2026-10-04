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
Receipt: `build/level2/arrival/run.log`, `ticks.jsonl.gz` and `rand.trace`.
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

The message resource adapter now runs during `001AFCA0`'s native area
rebuild. It selects the delivered area's EMMD view for `001FD790` and
`001FD950`, preserving the request block, draw/glyph state, styles, streams
and presenters. `001FC9B0` keeps its original reset sites. The original
service caller is `001FCA10 -> 001FDB80 -> 001FD790`; AREA01 dialogue itself
is still unbound. See LEVEL2_MESSAGES.md: **4 state-preserving selections,
11,956 capture-equal bank bytes, 54 quick / 3,330 full original service
ticks**, plus missing-bank and persistent-fault checks. The fixture's
synthetic draw/glyph state is distinguished from its recorded inputs.

The math storage contract now accepts direct views of canonical owners,
with no full-RAM arena or fallback for missing spans. Its sanitizer alias,
bounds and fail-stop checks pass; the complete math and light original-code
oracles pass again in quick and full modes. LEVEL2_MATH_VIEWS.md separates
the new adapter checks from the existing linear-memory oracle evidence.
The future actor binder still has to supply those views.

The resource checkpoint connects three adapters during the live state-0
rebuild, while retaining the world-frame guard:

- After pool/model-owner reset, select `D_0028A59C` from the loader's slot
  0x43 through the existing world-model owner. The EMWM table must match
  the delivered address. LEVEL2_MODEL_BANK.md proves **1,664 lookups,
  832 owner initializations and 1,362 bone records** in full mode. Direct
  AREA01-to-AREA11 without module-3 reload relocates that bank in both the
  original and native loader; the fixed-address export correctly refuses
  it. That additional transition is not claimed supported.
- Bind RCL for both world areas. AREA01 keeps its delivered static bank
  and borrows dynamic table slot 0x45; it does not reload AREA11's static
  export. The existing `001D5370` dispatch reaches the prepared dynamic
  packet workers and existing depth/page owners. Full standalone
  composition: **16 snapshots, 276 entries, 2,278 tracked original calls**.
  First-level RCL regression: **15 beats / 1,335 entries**. See
  LEVEL2_RENDER_PACKETS.md for the remaining VU/pixel limits.
- After `001F0310`, run AREA01's `001E7780 -> 00823A50` initialization
  before the spawn calls. Six globals are owned by `EmArea01State`; data
  and BSS alias the loader. Full initializer oracle: **416 cases,
  570,775,296 state bytes**, plus the original BSS-clear proof. See
  LEVEL2_AREA_STATE.md. Rebuilds do not clear the whole overlay again.

The loader-to-model test exposed an existing `region_for` bug: a shorter
same-base read retained the old allocation extent, so a later overlapping
read could discard still-resident AREA01 top resources. The replacement
preserves untouched prefixes/suffixes and gives each read its exact
extent. Full loader regression passes **5,544 read cases and 45 additional
whole modules**, plus its existing page, GS and sanitizer checks.

The live resource probe still reaches the rebuild at counter **15007** and
then the intentional `001AE040` world-frame fault. The new first-control
probe passes **1,301 locked ticks, zero motion, 30 move ticks, 9.599849**.
Receipts: `build/level2/resource-arrival/` and
`build/level2/resource-newgame-control.log`. No AREA01 actor frame or
dialogue is claimed by these resource checks.

## Verification

- Initial `make all`: passed, zero compiler warnings.
- `EM_STARTUP_TEST=newgame-control`: passed; 1,301 locked ticks, zero locked
  motion, 30 move ticks, displacement **9.599849**, census 49.
- Resource-checkpoint first-level main smoke: **19 live phases through exit**,
  capture checker passed with the matching indexed binary. Measured-drive
  smoke also passed through Roger. The panel/no-battery, status pages,
  fence-side-1, all **11 aim** and all **3 damage** side runs passed. The
  branch runs remain pending. Recursive make initially tried to rebuild
  from concurrent unstaged work; that link failure was isolated from the
  completed gameplay runs. Receipts: `build/level2/resources/main-check.log`,
  `ps2-drive-smoke.log`, `side-smoke.log`, and `remaining-smoke.log`.
- Ten AREA01 quick oracle suites passed. Exact counts and receipts are in
  SECOND_LEVEL_CENSUS. The existing-render suite initially failed to link
  the point-light module's shared matrix workers; adding its existing
  `em_owner_services_original.c` dependency fixed the harness.
- The 277 baseline `make test-*` targets have per-target results in
  `build/level2/verification/results.json`. Sandboxed native
  GPU tests cannot create Metal devices; these require a headless run with
  normal host access. That infrastructure failure is not a game pass.
- The initial 277-target sweep completed: 255 passed and 22 failed in
  the sandbox. Fifteen failed targets then passed with normal headless
  host access, including all GPU pixel checks, cutscene skip, first
  control and message capture. **270/277 distinct baseline targets have
  passed**; the seven smoke targets are being completed separately.
  The full main/panel/status/fence/11-aim components passed before a
  concurrent source addition exposed a missing application link entry at
  the next rebuild. The source list is repaired; this interrupted full
  invocation is not counted as a full-suite pass.
- Missing-worker staged-index build: `make -B all`, zero warnings,
  receipt `build/level2/prerequisites-index-build.json`.

The resource staged-index build passed `make -B all` with **zero warnings**
(`build/level2/resources/index-build.json`). Completed large trace receipts
are preserved as gzip files; `build/level2/compressed-receipts.json` lists
the paths. The resource checkpoint retains the AREA01 world-frame guard.

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

Message-resource checkpoint:

- `em_message_live.c/.h`: area selection preserves dynamic service state;
  the EMMD area index is checked against the actual 23-element array.
- `em_scene_bindings.c`: select the AREA01 or AREA11 message resource in
  the area rebuild, before owners can request a message.
- `Makefile`: `test-message-area-reference` target. FIDELITY_FEATURES
  describes the arrival-only state; no launcher option was added.
- New `tests/message_area_bridge.c`, `tools/test_message_area_reference.py`
  and LEVEL2_MESSAGES.md.

Math-view checkpoint:

- `em_area01_math_core.c/.h`: optional direct memory resolver, preserving
  the existing linear oracle mode and fault/store-trace contracts.
- `tools/test_area01_math_reference.py`: appended ctypes view field.
- `Makefile`: native math-view contract and light-owner oracle targets.
- New `tests/area01_math_views_test.c` and LEVEL2_MATH_VIEWS.md.

Area-resource checkpoint:

- `em_scene_bindings.c`: bind AREA01 render resources; select the world
  model bank after owner reset; initialize and detach canonical AREA01
  overlay state at the original lifecycle points.
- `em_area11_boxes.c/.h`: transactional world-bank selection and
  nonallocating owner metadata projection, preserving the shared stack.
- `em_module_loader.c/.h`: bounded mutable view and interval-preserving
  read allocations; no replacement loader state machine.
- `em_render_context_live.c/.h`: dynamic table views and dispatch through
  existing packet/depth/page translations.
- `tools/test_render_context_live_reference.py`: existing dependency
  sources required by the composed packet adapter.
- `tools/test_level_smoke.py`: random-call symbolication now honors
  `EM_LEVEL_SMOKE_BIN`, like the other smoke scripts, so an isolated binary
  is checked against its own symbols.
- `Makefile`: compile existing AREA01 VIF and the new state provider;
  add state, bank and composed-render oracle targets.
- New AREA01 state source/header, three reference suites, two test bridges,
  and LEVEL2_AREA_STATE, LEVEL2_MODEL_BANK, LEVEL2_RENDER_PACKETS.
- LEVEL2_COLLISION records the completed full sweep; no collision game
  code changed.

## Known gaps

All main beats `a01_00..a01_07` and side beats remain unplayed by the native
port. AREA00 arrival is the intended stopping boundary. The message bank
now switches during rebuild; model/render resources and overlay init are
connected there. Live AREA01 dialogue, rendering presentation, canonical actor records,
scripts, interactions, doors and pickups still need their AREA01 adapters.
The extraction resident-offset label shift is not fixed; the decomp's
`tools/extract_data.py` is outside the allowed decomp edit scope. Any
source correction there must be reported in permitted docs, not applied.
No decomp corrections have been committed by this branch.

## Canonical callback dependency checkpoint

SYS, EXITA, EXITB, ROOM, SIDE, overlay and shared render/UI/FX accessors now
accept authoritative byte providers with the original read/write direction.
A refused provider never falls back to an array. The scene view exposes only
already canonical named fields and migrated progress ranges. No normal
AREA01 frame gate is opened by this dependency checkpoint.

Shared edits: the named AREA01 contexts/helpers and their ctypes oracle
layouts; AREA00 FX helpers; the two SIDE initializers in aim/fire; designated
render-world initializers in status pages and render context; the new
`test-area01-memory-view-reference` Makefile target. Exact files and full
oracle counts are documented in `LEVEL2_MEMORY_VIEWS.md`. Composite live
binding and scratch lifetime work remain in progress outside this checkpoint.

The isolated staged-source build passed `make -B all` with **zero warnings**
in **40.352 s**. The same export passed **89 canonical boundary checks**.
Binary SHA-256:
`28fb6c803248043709ee3bf4e653dcac0dd1988b067ad8fe938ddf1d4399222e`.
Receipts: `build/level2/canonical-callbacks/index-build.json`,
`index-build.log`, and `index-contract.log`. Full module oracle evidence
is listed in the callback document; AREA01 route completion remains unproven.

## Native state dependency checkpoint

The existing actor-pool walk accepts an optional `001CB590` selection hook,
after publishing its current actor and before clearing drawn or invoking the
behavior. The old walk entry delegates to the same body with no hook.
AREA01's pending composition uses it to publish the existing current-bone
selector; this commit adds no second pool or bone storage. Original-code
full proof passes **246 cases, 328 walks, 5,014 visits, 80 freed-next cases,
1,136 reserve allocations and 89 refused allocations**. It compares the
original `001CB590 -> 001CB5B0` result after every operation. Sanitizer tests
cover selection order in all three modes and failure before the behavior.
Receipts: `build/level2/pool-select-{unit,quick,full}.log`.

Three exact bytes become canonical in the existing scene progress region:
`00810766` (`001C02E0` bypass store), `00810842` (AREA01 door flags), and
`00810845` (`001C02E0` mechanism gate). Their initialization is the existing
`001AF2C0` reset; no per-arrival reset or copied capture is introduced.
The reset oracle now compares **14 gameplay/progress fields and 73 inventory
fields**, including dirty values for these bytes. Six neighboring reserved
spans are refused. The no-shadow audit names their actual readers/writers.

Shared files: `em_actor_pool.c/.h`, `em_scene_state.h`, the terminal-power
comment/log wording in `em_game.c`, actor-pool unit/oracle tests,
`continue_reset_probe.c`, its oracle, and `test_scene_no_shadow.py`.
No normal AREA01 frame gate changes. The isolated staged-source
`make -B all` passed with **zero warnings in 39.860 s**; actor-pool,
reset and no-shadow targets passed in that export. Receipts are in
`build/level2/native-state/`; binary SHA-256
`52664816ec7e71c4a8d777a7ca05ce2dd65ac60f5a06ecd17ad227810581489f`.

## AREA01 sound-resource checkpoint

The existing shared SFX exporter now includes all 1,000 AREA01 area-paged
IDs and global failed-grab cue `01AC`. The private native registry contains
**1,278 entries and 188 samples**; all previous **278 entries and 143
samples** retain identical serialized values and PCM. The finite table
coverage includes explicit absent and unsupported entries; it is not a
claim that every ID is requested on the first visit. The existing EMSR
loader's entry cap increases to **2,048**, with larger/malformed inputs
still refused. No audio algorithm changes in this checkpoint.

Full original-code verification passes **16,016 dispatches across 16
captures, 404 sequencer cases and 839 key-ons**. Quick verifies **1,001
native lookups**, 64 original dispatches, six sequencer cases and four
loader boundaries. AREA11 original and sanitizer audio regressions pass.
`LEVEL2_SFX.md` gives exact scopes, conditional-call evidence and remaining
modulation/bank refusals. Main's **15 SFX files / 4,668,860 bytes** retain
identical hashes; only the level2 worktree's private registry was replaced.

Shared edits: `tools/export_sfx_registry.py`, the EMSR entry cap in
`em_sfx_bank.c`, four area-parameter/lookup helpers in
`test_area11_sfx_reference.py`, and the `test-area01-sfx-registry` Makefile
target. New files are `test_area01_sfx_registry.py` and `LEVEL2_SFX.md`.
The exact staged-source `make -B all` passed with **zero warnings in
39.923 s**, and its AREA01 registry test passed. Receipts:
`build/level2/sfx-checkpoint/index-{build.json,build.log,tests.log}`.
Binary SHA-256:
`987f34a7fd5c2f4b1050f3903c8b64179e6a3ddc91942607c3534ad2c49a9535`.
Normal AREA01 world frames remain gated and route verification is pending.

## Shared C69A0 pose checkpoint

The status renderer now uses the root and post-blend matrix stages of the
existing complete `em_area01_math_001C69A0` translation. Its former separate
calculation and duplicate root scaling are removed. The status channel
evaluator supplies its already blended quaternion; the adapter invents no
keys or blend fraction. Live status preserves the original root, animation,
rest, quaternion and product scratch writes and previous slot matrices.
The public helper retains its existing immutable-input/output contract.

Original-code checks pass **145 direct status cases, 4 compatibility cases
and 3 refusal contracts**; the complete math sweep passes, including
**756 C69A0 cases, 32,940 boundary calls and all 4 branch outcomes** over
12 beats. The existing status sanitizer fixture retains **27/27 bit-exact
matrices and 63 lighting/reset pairs**. The existing census fixture retains
**4,451 equal comparisons** and its documented previous divergences.

Shared edits are `em_area01_math_actor.c/.h`, `em_status_models.c/.h`, the
census fixture's isolated link, and the main/status-test Makefile links.
New files are the direct status-pose bridge and reference test. Their
source contains no captured data. The AREA01 model adapter and live
composition remain separate integration work; normal AREA01 gameplay is
still guarded. No census row is promoted by this checkpoint.

The exact staged-source `make -B all` passed with **zero warnings in 38.793 s**; all three focused status/census targets passed. Receipts: `build/level2/pose69-checkpoint/index-{build.json,build.log,tests.log}`. Binary SHA-256: `e64a260d3f8ae534afc11e4cf42658cb0a3daa1dbdedb9a722369d20fb0a09b8`.
