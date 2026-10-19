# AREA00 first-visit binding

2026-10-13, written on branch `level3` (based on port `d86f73c`, worked in
the `../extermination-port-level3` worktree, merged into main 2026-10-14). AREA00 now loads through the shaft exit,
but **arrival and first world frames do not pass**. That checkpoint stopped at
arrival frame 0 because its 579-key texture catalog exceeded the
then-protected 512-slot graphics registry. The 2026-10-18 capacity follow-up
is below; historical smoke rows have not been relabelled as passing. On the
2026-10-13 `level3` branch no protected file, decomp source, README or user
test runner was edited, nothing was merged or pushed, and no emulator was
launched. On the 2026-10-18 `levels` branch, protected-file edits stayed
within the authorized object-texture registry exception, and fork
recordings were made through the decomp's hidden, run-locked `route_capture`.

## Current state

2026-10-18 asset follow-up: original page replay found an additional
`001E9E60` surface texture. Both canonical AREA00 catalogs now hold 580 fixed
keys, checked against every selected GS freeze and uploaded by the native
resource-loader fixture. The explicitly authorized graphics change raises
the registry to 1024 with a hash index. All four default/GPU smokes pass
with byte-identical ticks, RNG and presented-frame logs; control is 9.599849.
Metal-drawn (`gpu`) frames carry no pixel hash, so they are compared by
tick, RNG and frame order; the registry's pixel behaviour is proven by the
Metal fixture `tests/level_texture_registry_test.m` (LEVEL_ASSET_DELIVERY.md). This does not certify an AREA00 frame. The subsequent
surface and retained-library delivery work is described below. Full asset receipts and retained runtime-material limits are in
[LEVEL_ASSET_DELIVERY.md](LEVEL_ASSET_DELIVERY.md).

The stage-5 continuation adds the 40-byte `D_00248290` read view through
the existing effect pack, preserving its 50 earlier windows. The full
`0015AB00` oracle passes 160 cases and 30,000 worker calls. The unchanged
production read provider passes 820 exact subrange checks and five bounds
refusals under ASan/UBSan. All 13 retained fork snapshots match the table.

The next native fault was model `0x4F` at `00C7CBC0`, called by
`0015AFA0` through `001B1020`. AREA00's roster requires five library
models, of which `4F/59` were missing from the retained export. Extending
two existing read-only spans supplies both while preserving all earlier
bytes, flags and the 16-region limit. All five models match 13 snapshots;
55 original-instruction binding cases pass over the 11 AREA00-resident
snapshots. No native game source changed for either addition.
See [AREA00_ASSETS.md](AREA00_ASSETS.md) for reproduction and exact scope.

The following native attempt passed those resource reads and stopped at
the crate registry's AREA00 selector `0024A850` inside `001551B0`. Adding
that halfword and `0024D820`'s pointer to the same export supplies the
existing lazy resolver; group data remains loader-owned. The full crate
oracle passes 86 ticks and 442 worker calls, with both lazy access and
sticky missing-root faults checked. These additions still change no
native game source and do not promote an AREA00 phase by themselves.

With all three additions, ordinary New Game reaches arrival alignment at
counter 26925 with the drive-timing switch (`EM_PS2_DISC_DRIVE_TIMING=1`,
the documented command) or 26634 at host speed
(`build/levels/area00-arrival-hostspeed-review-1/`), then fails at arrival frame 0 in drum callback `00156620`:
the protected `em_area11_boxes.c` owner table has 32 slots. The original
arrival has 26 crates and 13 drums, so those callbacks alone require 39
views. This is the current stop; the 512-texture refusal below is historical.
The explicit ownership rule prohibits changing this AREA11 file. The
proposed change and its required validation are in **Requests for main**.

2026-10-18 standalone follow-up on branch `levels`: four missing AREA01 routines
on the AREA00 return (`00823A90`, `00823CD0`, `008240E0`, `00826010`) now have an
unwired translation in `em_area00_return.c`. Its original-instruction test
passes 521 cases and all 534 reachable non-branch words, including 30 captured
actor cases and the full fail-stop sweep. This is no AREA00 phase promotion.
See [LEVEL_ORACLE_AUDIT.md](LEVEL_ORACLE_AUDIT.md) for exact scope and receipts.

The historical capacity diagnostic's native scene selection reaches AREA00 after ordinary New Game, AREA11
and AREA01. The original load completes and selects AREA00 collision and
scene resources. At counter 26922 the existing shaft phase observes
AREA00 state 0, before rebuild. The following page-render request faults
at `001CB800` because the catalog cannot fit. This replaces the previous
`001FF080` “area 00 room 0 is not exported” stop; it is not a gameplay pass.

The current composition selects AREA00's original initializer, borrowed
loader data/BSS, roster, callbacks, scripts, messages, collision, static
bank and per-subarea resources. AREA01 wrappers keep their existing area-1
behavior. The shared owner remains authoritative for each global, actor,
player, weapon, model and collision span. Unknown workers still fault.

Completed prerequisites now include both verified 580-key texture catalogs,
shadow banks (sub 0: 1,100 objects / 1,802 blocks; sub 1: 1,099 / 1,783),
the full module deliveries and canonical SFX scopes for subareas 0/1.
The complete sound registry preserves the earlier entries and samples;
unsupported original sound cases remain explicitly unsupported.
The scene logger now emits the strict AREA00 observations from the
canonical owners. No captured state is installed in a native run.

[THIRD_LEVEL_CENSUS.md](THIRD_LEVEL_CENSUS.md) retains all 141 delta entries
and 19,012 instructions. None is promoted to AREA00-route live: the
required native row comparisons have not passed.

## Binding ledger

| Boundary | Existing owner / required binding | State |
|---|---|---|
| AREA01 shaft exit / module read | `em_scene_task`, `em_module_loader`, full AREA00 sub-0/1 pack entries | Native sub-0 delivery and scene selection reached; sub-1 sectors independently checked |
| Texture delivery | `em_world_textures_live`, the single graphics registry | Both selectors wired; corrected 580-key catalogs pass resource upload at capacity 1024; required default/GPU registry regressions pass |
| Shadow receivers | `em_shadow_live`, `em_shadow_original` | Both selectors wired and checked, with transactional failure cases |
| Collision and static bank | `em_collision_world`, loader resource `0028A5A0`, `em_render_context_live` | AREA00 paths selected; static bank remains loader-owned |
| Overlay initialization | existing `001E7780` plus `em_area00_state_00824E00` | Selected initializer, six common resets and both record clears verified against original instructions |
| Overlay data/BSS | AREA00 MWo3 id 1, data `826F80/6500`, BSS `82D480/295900` | Checked borrowed loader views; the existing six boot globals remain the sole storage |
| Progress | `EmProgress`, `em_scene_progress_at` | `75B/75C`, `7DA/7DB/7DC` and AREA00 lock byte `841` admitted with original writer evidence; no new storage/reset |
| Placed roster / actors | `em_actor_roster_spawn_001B6990`, canonical actor/model/collision adapters | Per-subarea placements/deferred records selected; no separate pool |
| Overlay behavior | `em_area00_overlay` | All 13 prepared entry adapters selected before AREA01 address dispatch; unknown overlay entries fault |
| Scripts | `em_area_script`, existing script host | AREA00 window `8284E0..82BB50`, mutable loader data and area-specific callbacks selected; arrival `824E40` checked against original instructions |
| Messages | `em_message_live` | AREA00 EMMD selected during rebuild; canonical request/presenter state retained |
| Sound | existing stream/SFX owners | Complete AREA00 sub-0/1 registry scopes; 3,278 combined entries / 234 samples; no unsupported-case bypass |
| Surface initialization | existing effect-table provider and `em_area01_exitb` | `D_00248290` added from pinned ELF; full initialization/packet-state oracle and production read bounds pass |
| Pickup models | existing retained library provider and shared model adapter | Five roster models delivered exactly within 16 regions; production provider and 55 original binding cases pass |
| Crate registry | existing box owner, effect windows and module loader | Six-byte AREA00 roots exported exactly; production lazy resolver passes 86 original crate ticks |
| Ferry / cab / shaft workers | existing roster, WORLD, FX and effect services | Direct `001B6660`, `0019C6F0`, `001B0C60`, marker/depth/transform adapters installed; standalone comparisons pass; native phases remain uncertified |
| Player and camera | existing player closures and camera owners | Canonical selected-area views installed; no AREA00 native row pass |
| Return / bridge | AREA01 overlay plus revisit translations | Main and plain side returns remain separate strict phases; neither reached |

Common `001E7780` clears its six globals, calls the selected initializer,
then clears `+54/+58` for each initialized record with stride `A060`.
The AREA00 initializer borrows those existing globals and the loader's
memory. It never clears a replacement BSS image or owns a duplicate reset.
The optional AREA00 read/write view preserves canonical permissions;
a refused write cannot fall back to a mutable legacy byte view.

## Phases and comparison results

| Requested phase | New smoke scope | Native comparison result |
|---|---|---|
| 1. Arrival / load / bindings | AREA01 exit boundary, then `a00_arrival` | Not passed: alignment at counter 26925 (drive timing) / 26634 (host speed), then frame-0 `00156620` fault when the 32 shared owner views are exhausted |
| 2. First world and neutral frames | Fork a01_07 f539..4555, including final 60 neutral rows | 0/4,017 rows certified; alignment reached, but the first owner walk still fails |
| 3. Movement / collision | `a00_00..a00_10` | Not reached |
| 4. Camera | Per-row eye/target/forward/flags plus whole endpoint camera RAM | Checker contract passes; native AREA00 camera not reached |
| 5. Drawing | Future original packet and displayed-field comparisons | Not implemented/certified; exact exported texels are not a pixel comparison |
| 6. Interactions | `a00_01` locked door, `03/04` padlock/terminal, `05` ferry, `06` cab, `07` stack/crate tops/duct, `08` switch, `09` barricades/door | Not reached |
| 7. Return to AREA01 | `a00_10` progression return; `a00_s0` plain side return | Not reached; separate loader/state/progress checks prepared |

Original route correction: `a00_s0` branches from the initial AREA00
arrival and returns without the progression flag. It is not a continuation
after `a00_10`. The main progression return is `a00_10`, with
`D_0081075E=FF`; `a00_s0` ends with that flag 0. The original endpoint
snapshot proves the lowered bridge after the main return; a full native
bridge/revisit sequence remains separate verification work.

## Smoke contracts and limits

The default `test_level_smoke_area00.py` is a short **harness contract
test**, not a game run. `--run` explicitly requests the long replay from
ordinary New Game. Pads contain only input and reference timing metadata;
no captured game state is installed into the native run. `--prepare`
exports pads and does not certify anything.

The C phases reuse the existing strict EMA1 reader. AREA01 still ends at
its existing boundary when selected alone. A level-3 continuation keeps
that exact boundary check, then checks the following actual scene tick.
Return phases detect actual AREA01 state 0. Only repeated loader waits
may be shortened; no gameplay tick may be dropped or realigned. The
existing original loader/veil oracle is reused on return runs. Recorded
pad corrections preserve the late first commands and the one-frame
dropout in `a00_04`; their source metadata is checked.

The scene logger supplies `a00` observations from canonical
owners: `hp` bits; progress windows `8107D8/64`, `810758/8`, `810860/64`,
`810D00/32`; locks `810840/8`; weapon bytes `810C60/8`; and the ten
owner records named in `level_smoke_area00.OWNERS` (header, position,
`+1F0`, `+2DC`, callback). `C61/C62` come from `em_weapon`, not the
progress region, which correctly refuses those separately owned bytes. The new checker rejects absent observations.
The arrival recording has only AREA01 owner samples, so AREA00 owner
comparison is explicitly absent during `a00_arrival`; gameplay phases
require all ten samples. Existing per-row and whole endpoint camera
observations are reused. An eventual row pass still does not certify
rendered pixels, sound, unrecorded owner fields or off-route behavior.

AREA01 currently has a known prerequisite difference at a01_03 f957
(voice ends one tick early with drive timing; LEVEL_SMOKE.md). The new
runner does not waive it. Even after opening AREA00, the complete
New-Game-to-AREA00 comparison must fix this difference at its original
owner or continue to report failure. The continuation's saved replay
confirms it with the existing strict prefix diagnostic: 957 of 992 rows
match, then message state is `(2, 2, 64)` instead of `(2, 1, 64)` at row
957 (`build/level3/area01-prefix-live.log`). This diagnostic is not a
complete phase pass.

## Historical capacity request and remaining main prerequisites

The following records the 2026-10-13 refusal. The user authorized this
capacity/index exception on 2026-10-18; its implementation and passing
regression proof are described above and in LEVEL_ASSET_DELIVERY.md.

**Historical texture registry refusal, confirmed by the production catalog parser.**
`src/em_gfx.h` then defined `EM_GFX_OBJECT_TEX_MAX=512`; the protected Metal
registry enforced that limit. Each AREA00 export then contained 579
distinct keys, following AREA01's eager whole-bank/shared-resource method.
`em_world_textures_live.c::catalog` rejects the header before uploading
anything. `test_area00_resources_reference.py` reproduces that refusal
using the production parser and records zero registry mutations. The
actual input-only replay now reaches the same refusal: after AREA00
selection, `chain page` reports no page textures and the scene faults at
`001CB800`, arrival frame 0. See `build/level3/live-arrival-1/run.log` and
its binary-hash receipt; no arrival alignment is reached.

Requested change: raise `EM_GFX_OBJECT_TEX_MAX` to **1024**, audit every
array/count consumer (including Metal), then run the existing registry,
GS and pixel regressions and this resource fixture. No change to original
game behavior, sampled texels or assertions is requested. The earlier `level3` branch
did not edit that header or any renderer file; the present `levels` exception
is limited to capacity and lookup.

This is a capacity requirement of the existing **eager export policy**,
not a proof that 579 textures are simultaneously needed by gameplay.
Removing only world models absent from the endpoint snapshots still
leaves 527 keys; equipment/page sources contain additional eager keys.
A smaller catalog would require a complete binding/reachability proof
(including intermediate frames and side actions), not silent truncation
to fit 512. The source audit found 505 non-equipment keys; valid equipment
variants can add nine or twelve more, and queued draws retain their own
model references. Thus choosing the actor's current equipment alone is
neither a complete capacity solution nor a proven lifetime policy. No
catalog pruning or residency redesign is claimed here.

No additional protected VU program gap was established. Current page
support includes the known sprite/streak/kind-2/kind-6/floor/ripple paths.
Later channel-9 workers reached in `a00_09` remain an integration audit
item, not a substantiated request to edit a protected file.

## Requests for main

**Raise the protected shared owner-view capacity.** In
`src/game/em_area11_boxes.c`, `BOX_MAX=32` limits both the `Box` views and
draw-order indices. `box_for` refuses the 33rd simultaneously held view.
This is host bookkeeping; the original actor pool still has 256 slots.
The AREA00 arrival and nine subsequent resident captures contain 26 crates
and 13 drums (39 views before counting doors and other world owners).
The last resident capture retains 32 crates/drums. The live failure is
`more AREA11 boxes than slots`, from `00156620`, at arrival frame 0.

Proposed diff for main to review and apply; it is **not applied** here:

```diff
--- a/src/game/em_area11_boxes.c
+++ b/src/game/em_area11_boxes.c
@@
-    /* four crates, two drums, the truck and the door, and the other world
-     * owners bound through em_area11_boxes_owner_* (the terminal, the panel,
-     * the prop 001C4820, the item owners; room for the rest) */
-    BOX_MAX = 32,
+    /* At most one host view per live canonical actor slot. Generation and
+     * freed-slot checks retain the existing ownership and reuse rules. */
+    BOX_MAX = EM_ACTOR_POOL_CAPACITY,
```

`tools/test_area00_owner_capacity.py` compiles the unchanged protected
provider under ASan/UBSan. It confirms 32 successful views, refusal of the
33rd, and correct freed/generation reuse. Its independent capture census
confirms the 39-view minimum. `--require-supported` correctly fails with
the present capacity; its default diagnostic mode reports the mismatch.
The fixture exercises the configured bound and does not lock in 32 as
correct game behavior. Main should require acceptance through the
canonical pool capacity, then exercise full/reused draw-order
storage, default/GPU AREA11 and AREA01 `--until a01_02` exact logs and pixels,
control 9.599849, and this same AREA00 arrival replay. Neither increasing
the actor/bone pool nor reassigning or omitting owners is requested.
Private evidence: `build/levels/area00-owner-capacity/receipt.json` and
`build/levels/area00-arrival-crate-1/`.

**Keep the AREA01 voice prerequisite explicit.** The existing a01_03
row-957 discrepancy remains a strict failure. Its audio/movie/frame owner
is outside this worktree's authorization. No checker assertion or replay
row has been dropped or realigned to bypass it.

## Verification and reproduction

Run on native arm64 macOS in the port checkout (the receipts below were made
in the `level3` worktree and stayed there). Heavy jobs are
serialized at nice priority 15 using the approved host path. Two initial
quick state/runtime attempts printed a sandbox `setpriority` warning and
continued; the reported full tests below supersede them at actual nice 15.
Do not treat `nice`'s warning as successful priority control.

```sh
# in the port checkout
nice -n 15 make -j4
# Generated outputs must be private, never written through checkout symlinks.
nice -n 15 python3 tools/export_sfx_registry.py
nice -n 15 python3 tools/export_module_loader.py
# Full table export goes to private scratch; installed root/table delivery is tested below.
nice -n 15 python3 tools/export_area00_tables.py --out build/level3/area00-tables
nice -n 15 python3 tools/test_area00_delivery_reference.py
nice -n 15 python3 tools/test_area00_assets_reference.py
nice -n 15 python3 tools/test_area00_resources_reference.py
nice -n 15 env EM_TEST_FULL=1 python3 tools/test_area00_state_reference.py
nice -n 15 python3 tools/test_area00_runtime_reference.py
nice -n 15 python3 tools/test_area00_effects_binding_reference.py
nice -n 15 python3 tools/test_area00_script_binding_reference.py
nice -n 15 python3 tools/test_area00_spawn_binding_reference.py
nice -n 15 python3 tools/test_area01_state_reference.py
nice -n 15 python3 tools/test_area01_runtime_reference.py
nice -n 15 python3 tools/test_area01_effects_services_reference.py
nice -n 15 env EM_TEST_FULL=1 python3 tools/test_area00_world_reference.py
nice -n 15 python3 tools/test_area01_door_live_reference.py
nice -n 15 python3 tools/test_area01_sfx_registry.py
nice -n 15 python3 tools/test_world_textures_reference.py
nice -n 15 python3 tools/test_scene_no_shadow.py
nice -n 15 env EM_HEADLESS=1 EM_STARTUP_TEST=newgame-control build/extermination
# Long diagnostic: currently expected to fail at 00156620 (the 32-view owner table).
nice -n 15 env EM_PS2_DISC_DRIVE_TIMING=1 python3 tools/test_level_smoke_area00.py --run --until a00_arrival --out build/level3/live-arrival-1
# Later full-route and independent side-return checks:
nice -n 15 env EM_PS2_DISC_DRIVE_TIMING=1 python3 tools/test_level_smoke_area00.py --run --until a00_10 --out build/level3/progression
nice -n 15 env EM_PS2_DISC_DRIVE_TIMING=1 python3 tools/test_level_smoke_area00.py --run --side a00_s0 --out build/level3/side-return
python3 tools/check_no_disassembly.py --staged
# Build each staged topic in isolation before committing; remove scratch afterward.
git checkout-index -a --prefix="$PWD/build/level3/index/"
(cd build/level3/index && nice -n 15 make -j4)
```

Continuation results of the 2026-10-13 `level3` branch (historical; ignored receipts under `build/level3/`):

| Check | Result | Receipt |
|---|---|---|
| AREA00 selected initializer/common dispatcher, full | PASS: 10 captures, 120 cases, 328,276,800 compared bytes; ordered stores, aliases, lifetime, bounds/faults | `state-dispatch-full.log` |
| AREA00 runtime overlay selection | PASS: 16 original paths, 13 worker boundaries, 5 canonical writes; overlap refusal, permissions and fault latch | `runtime-full.log` |
| AREA00 shared runtime/effects adapters | PASS: 19 original directory paths and marker tail call; 13 marker/depth/transform cases, 31 compared workers; canonical publish/partial-store/fault contracts under ASan/UBSan | `shared-runtime-full.log`, `effects-binding-full.log` |
| AREA00 script binding | PASS: 36 original worker cases, four exact clip boundaries, three failure cases; both AREA01/AREA00 resource fixtures under ASan/UBSan | `script-binding-live.log` |
| AREA00 direct deferred spawn | PASS: 28 original cases, 12 spawned records, four original allocation failures; whole pool/progress/list/return equality and live actor-view fixture | `spawn-binding.log` |
| Final AREA00 assets/root lookup | PASS: 15 loaders, 70 canary sections, 99 controls; immutable root/table comparison over all 11 retained captures | `placement-root-export.log`, `placement-root-assets-test.log` |
| AREA00 overlay view regression | PASS: 1,090 cases, 1,728 runs, 5,905 calls, all 1,298 reachable words | `overlay-view-regression.log` |
| AREA01 state/runtime/door regressions | PASS: 160 state cases / 219,528,960 bytes; runtime 32 paths + 6 camera + 34 sampler cases / 209 boundaries; door 60 cases / 172 workers / 4 fault cases | `area01-state-regression.log`, `area01-runtime-regression.log`, `area01-door-regression.log` |
| Texture/shadow selectors | PASS: real AREA00 paths and transactional receiver checks; both texture catalogs explicitly BLOCKED before registry mutation | `resources-live-test.log` |
| Shared texture regression | PASS: AREA11 487 keys, AREA01 460, six changed reused keys and eleven rejection cases | `world-textures-regression.log` |
| SFX/module delivery | PASS: 3,278 entries / 234 samples, all 1,278 prior entries / 188 samples unchanged; complete six disc reads per AREA00 subarea | `delivery-test.log` |
| AREA01 SFX regression | PASS: 16 bank captures, 1,001 lookups, 64 original dispatches, 6 sequencer cases / 10 key-ons; 4,096 accepted / 4,097 refused and malformed cases | `area01-sfx-regression.log` |
| AREA00 WORLD full regression | PASS: 1,033 cases, 4,005 variants, 14,679 compared workers, 3,203 field changes, all 64 branches; 74 existing variants incomparable because original execution left RAM | `shared-world-full.log` |
| Final AREA01 shared regressions | PASS: runtime 32 paths + 6 camera + 34 sampler cases; effects nine original spawn/reset/allocator chains and sanitized contracts | `shared-area01-runtime-regression.log`, `shared-area01-effects-regression.log` |
| Canonical-state audit G6 | PASS: 755 files, 26 migrated progress/request bytes | `scene-no-shadow-final.log` |
| Final native source build | PASS, zero compiler warnings | `live-build-final.log` |
| Final first-control regression | PASS: 1,301 locked ticks, zero motion, 30 movement ticks, displacement 9.599849 | `newgame-control-live.log` |
| Resource isolated staged-index build | PASS, zero compiler warnings | `index-resources-build.log` |
| Binding isolated staged-index build | PASS, zero compiler warnings | `index-bindings-build.log` |
| Integration build before native diagnostic | PASS, zero compiler warnings | `live-build-3.log` |
| Native arrival diagnostic | FAIL at `001CB800`, protected texture capacity; area-0 load completes, no arrival alignment/rows | `live-arrival-1.log`, `live-arrival-1/run.log`, `live-arrival-1/receipt.json` |

The diagnostic ran for 277.709 seconds, returned 1 and recorded
`native_parity=false`. The executable SHA-256 was
`5ddc7487154695900b96ba480c82dd42e551b709abc59128da175e23632be23f`.
Its in-process AREA01 phase completions are not independent capture
passes. The subsequent direct-spawn and shared WORLD/FX bindings are
standalone-tested additions beyond this first capacity stop; that
recording does not exercise them.

The earlier harness receipts remain valid because the driver and checker
assertions have not changed: fork AREA00 has 13 paths, 2,731 input commands,
18,331 driver frames, 16,504 capture-shaped rows and 179 corruption
rejections (`verify-fork/`); v2.6.3 has 18,303 frames and 16,490 rows with
the same paths/commands/rejections (`verify-v263/`). AREA01's harness has
16 paths, 4,447 commands, 20,110 frames, 19,565 rows and 150 rejections
(`verify-area01/`). These are harness contracts, not native gameplay.
The old load-refusal diagnostic remains historical in `native/`.

Generated assets and receipts stay ignored. `assets/` is a real directory;
changed SFX/module packages and AREA00 delivery files are private. The
remaining existing assets are linked to the primary checkout and are not
written through. `data/memcard/slot1`, `slot2` and `data/save` are private.

## Remaining differences and next step

The current reproducible stop is `00156620` at arrival frame 0, caused by
the protected 32-view owner table. Surface dimensions, retained pickup
models and crate roots are now supplied exactly, with no native game-code
changes. Arrival aligns, but still has zero certified rows. Movement,
camera, interactions, drawing and both returns remain uncertified.
Standalone tests do not establish live call order or eliminate later
missing workers. The inherited AREA01 voice discrepancy also remains.

After main resolves the owner-view capacity, rerun arrival before
expanding downstream phases. Preserve the first fault and bind remaining
workers to their existing owners. Then compare all
4,017 arrival/neutral rows and the ending camera memory, followed by each
route phase and the independent side return. Drawing still requires
original packet and displayed-field comparisons. Promote census rows
only after their native AREA00 behavior is exercised and compared.

Unsampled main-line ticks (review 2026-10-19). Four ticks of the fork
AREA00 main line have no row: counters 35978-35980 (a00_04 ends at 35977,
a00_05's four-frame lead-in starts its rows at 35981) and 38516 (a00_06
ends at 38515, a00_07's two-frame lead-in starts at 38517). Before a00_05
and a00_07 are certified, record them on the fork, hidden and under the
run lock:

```sh
# native arm64 macOS; the decomp's .venv supplies zstandard
nice -n 10 ../Extermination/.venv/bin/python tools/record_level_boundaries.py \
  --route-set build/fork_refs/s87/route_a00 --beats a00_05_ferry_deck,a00_07_duct_to_ne_room \
  --out-root ../Extermination/build/fork_refs/levels-boundaries-<new>
```

`--route-set` reads the set's `manifest.json` instead of the supplemental
index and applies the same replay and full endpoint check. Status:
**blocked**. The 2026-10-19 attempt
(`Extermination/build/fork_refs/levels-boundaries-20261019-2/`) resolved
a00_05's gap (4, three missing counters) and its source/target hashes, then
waited in the run lock: `build/.pcsx2.lock` is a 0-byte regular file dated
2026-10-18 13:33 with no `owner.json` and no open handle, not the lock
directory the session takes. The waiting run (no emulator started) was
stopped by its own process id. Only the lock's holder or the user may
remove that file.

Stage-5 native evidence, all on the unchanged executable built from
`11d2f10` (same native source as `ab1cd94`), with input-only replays:

| Private receipt under `build/levels/` | Result |
|---|---|
| `area00-arrival-before-1/` | `00248290` read refusal, arrival frame 0 |
| `area00-arrival-surface-1/run.log`, `receipt.json` | Surface read passes; `001B1020` reports missing retained model |
| `area00-arrival-model-diagnostic-1/run.log`, `area00-readview-preflight-1/diagnostic-parity.json` | Private logging build identifies caller `0015AFA0`, missing model address `00C7CBC0`; ticks and RNG counter/state match the preceding run |
| `area00-arrival-library-1/run.log`, `receipt.json` | Model construction passes; crate selector `0024A850` is missing |
| `area00-arrival-crate-1/` | Arrival aligns at 26925 (drive timing; 26634 at host speed in `area00-arrival-hostspeed-review-1/`, the same 291-tick offset as `a01_arrival`'s 15299 / 15008); 32-view capacity fails at `00156620`, frame 0; 254.403 seconds, native parity false |

The final resource pack has 53 effect windows and 16 retained model
regions. Generated assets are private regular files in this worktree.
No protected AREA11 or rendering source was changed during stage 5.
Intermediate native tick/RNG files are superseded by the final replay;
their hashes and the diagnostic comparison are retained in the private
preflight receipts. Original captures and the before/final native pair remain.

Final stage-5 regressions pass with the final private assets installed:
AREA11 through `a01_arrival` and AREA01 through `a01_02`, each with default
and GPU rendering. All twelve tick, RNG and presented-frame files are
byte-identical to the stage-3 after-capacity baseline (`gpu` frames compared
by tick, RNG and frame order, as above). Asset hashes stayed
unchanged throughout all four final runs. New Game control is 9.599849.
Combined receipt: `build/levels/area00-readview-preflight-1/final-regressions.json`;
default traces: `build/levels/area00-final-defaults-2/`; GPU traces:
`build/levels/area00-final-regressions-1/gpu1-a01_arrival/` and `gpu1-a01_02/`.
Those runs' `ticks.jsonl` files were byte-identical to
`capacity-before-smokes-2/`'s and were deleted by name; their hashes stay in
the combined receipt and in LEVEL_ASSET_DELIVERY.md. Since the 2026-10-19
review, every `build/levels/` receipt lives in the port checkout's ignored
`build/levels/` (moved out of the `levels` worktree).
The isolated staged-index native build has zero warnings
(`build/levels/area00-index-build-1.log`). Eighteen readiness tests and the
unchanged AREA00 harness contract also pass. These regressions do not
certify an AREA00 gameplay frame.

## Topic commits

Earlier prerequisite checkpoint:

- `d8b1916`: borrowed AREA00 initializer and canonical progress access.
- `b3f3b16`: verified texture/shadow exports and production parser checks.
- `c654a3b`: strict replay phases, checker and positive/negative harness.
- `595cea7`: binding ledger, census and smoke documentation.

Continuation:

- `74c2844`: AREA00 texture/shadow selectors, sound scopes, module deliveries,
  immutable placement lookup root, strict delivery/parser regressions.

- `6e43d7f`: canonical AREA00 scene/state/runtime/script bindings, shared
  spawn/world/effect adapters, logger and original-reference tests.

This ledger and the smoke/census updates form the following documentation
commit. Both isolated source checkouts and private fixture binaries were
removed after verification; cited logs, reports and native-run receipts
remain. No completed playable level is claimed.
