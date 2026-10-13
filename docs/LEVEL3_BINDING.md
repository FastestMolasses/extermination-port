# AREA00 first-visit binding

2026-10-13, branch `level3`, based on port `d86f73c`. Worktree:
`../extermination-port-level3`. This is a **building prerequisite checkpoint,
not playable level 3**. AREA00 gameplay remains fail-stop at the existing
completed-load boundary; no owner, camera or drawing phase is certified.
No protected performance file, decomp source, README or user test runner
was edited. Nothing was merged or pushed, and no emulator was launched.

## Current state

The existing first-level route and AREA01 behavior are retained. The fresh
headless first-control regression passes: 1,301 locked ticks, no locked
motion, 30 movement ticks, displacement 9.599849. AREA01's source still
refuses AREA00 in `area_read`; supplying generated files alone does not
open that gate. New AREA00 phases are opt-in and must pass the independent
checker before any gameplay claim.

Completed here:

- Disc-upload texture catalogs for both recorded subareas. Each has 579
  keys; every exported texel equals each selected original GS freeze
  (sub 0: nine captures, sub 1: two). These are exports, not native draws.
- Shadow receivers from the delivered disc banks, using the existing EMSR
  writer: 1,100 objects / 1,802 blocks for sub 0; 1,099 / 1,783 for sub 1.
  The independent RAM checks cover 34,553,376 and 7,599,360 bytes. Both
  receiver files load in the native parser; a truncated file is refused.
- `em_area00_state_00824E00`, verified against original instructions. It
  borrows the existing six boot globals and loader data/BSS; it introduces
  no second global image and does not duplicate common `001E7780`.
- Canonical access for events `0081075B/5C` and counters `008107DA/DB/DC`
  in the existing `EmProgress`. Their original reset/lifetime is retained.
- A strict smoke driver/checker covering `a00_arrival`, `a00_00..a00_10`
  and side return `a00_s0`, with fork references by default and v2.6.3
  selected through `EM_REFERENCE=v263`.
- [THIRD_LEVEL_CENSUS.md](THIRD_LEVEL_CENSUS.md): all 141 delta entries,
  19,012 instructions, with actual translation definitions and historical
  oracle evidence. No AREA00-live promotion.

## Binding ledger

| Boundary | Existing owner / required binding | State |
|---|---|---|
| AREA01 shaft exit / module read | `em_scene_task`, `em_module_loader`, AREA00 sub-0 pack entries | Already present; next native resource selection still refuses area 0 |
| Texture delivery | `em_world_textures_live`, the single graphics registry | Catalogs verified; 579 exceeds protected capacity 512; selectors still admit only AREA11/AREA01 |
| Shadow receivers | `em_shadow_live`, `em_shadow_original` | Both catalogs verified/loadable; area-0 selection not wired |
| Collision and static bank | `em_collision_world`, loader resource `0028A5A0`, `em_render_context_live` | Existing AREA00 EMCL/cells/static exports; scene paths still AREA01-specific |
| Overlay initialization | existing `001E7780` owner plus new `em_area00_state_00824E00` | New initializer oracle passes, not wired |
| Overlay data/BSS | loader-owned AREA00 MWo3 id 1, data `826F80/6500`, BSS `82D480/295900` | New checked borrowed view; existing live composition remains AREA01 id 2 |
| Progress | `EmProgress`, `em_scene_progress_at` | Five missing bytes admitted with original writer evidence; no new storage |
| Placed roster / actors | `em_actor_roster_spawn_001B6990`, existing canonical actor/model/collision adapters | Must select AREA00's 69 placements and 61 deferred records; no duplicate pool/owner |
| Overlay behavior | `em_area00_overlay` (13 tested entries) | Still unbound; same addresses can mean different AREA01 functions |
| Scripts | `em_area_script`, existing script host and live shared services | AREA00 window `8284E0..82BB50`, data `826F80..82D480`, op09 `8253E0` need selection; AREA01 window/allowlist cannot be reused unchanged |
| Messages | `em_message_live` | Current loader already accepts area 0; select AREA00 EMMD at rebuild, retain canonical request/presenter state |
| Sound | existing stream and SFX owners | Select AREA00 scope and both sub registries; keep unsupported workers faulting |
| Ferry / cab | overlay `825600/8263C0`, shared `001A2370`, `001F5940` | Translations exist; canonical hull, point/effect and script adapter composition unverified |
| Switch / door / crates | `825920`, `825170`, shared door/crate/shot owners | Translations exist, AREA00 use/target/script paths unverified |
| Player and camera | existing player closures and camera owners | AREA00 dispatch and borrowed-memory paths unverified; no replay pass |
| Return / bridge | AREA01 overlay plus revisit translations | Main return and plain side return have separate checks; no native return reached |

The state initializer's exact stores come from byte-identical decomp
`func_overlay_AREA00_00824DC0.c` (runtime `00824E00`). Common `001E7780`
must first clear its six globals, call the area initializer, then clear
`+54/+58` of **both** records, as original `func_001E7780.c` does. The new
adapter deliberately owns only the area-specific stores. The existing
`s_area01_state.globals` is the storage to borrow when generalizing the
composition; an AREA00 copy would break the ownership rule.

Current preparation documents contain stale claims. `001A2370`,
`0019C6F0`, `001C5570` and many AREA00 boot routines already have native
owners or shared call routes. The message loader no longer rejects area 0.
See the census for definition-backed mappings rather than repeating those
old gaps. None of that proves a live AREA00 binding.

## Phases and comparison results

| Requested phase | New smoke scope | Native comparison result |
|---|---|---|
| 1. Arrival / load / bindings | AREA01 exit boundary, then `a00_arrival` | Not passed: AREA00 load completion is still refused; resource exports and initializer pass separately |
| 2. First world and neutral frames | Fork a01_07 f539..4555, including final 60 neutral rows | Not reached; checker covers 4,017 rows, no native parity |
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

The future scene logger must supply `a00` observations from canonical
owners: `hp` bits; progress windows `8107D8/64`, `810758/8`, `810860/64`,
`810D00/32`; locks `810840/8`; weapon bytes `810C60/8`; and the ten
owner records named in `level_smoke_area00.OWNERS` (header, position,
`+1F0`, `+2DC`, callback). The new checker rejects absent observations.
The arrival recording has only AREA01 owner samples, so AREA00 owner
comparison is explicitly absent during `a00_arrival`; gameplay phases
require all ten samples. Existing per-row and whole endpoint camera
observations are reused. An eventual row pass still does not certify
rendered pixels, sound, unrecorded owner fields or off-route behavior.

AREA01 currently has a known prerequisite difference at a01_03 f957
(voice ends one tick early with drive timing; LEVEL_SMOKE.md). The new
runner does not waive it. Even after opening AREA00, the complete
New-Game-to-AREA00 comparison must fix this difference at its original
owner or continue to report failure.

## Requests for main

**Texture registry capacity, confirmed by the production catalog parser.**
`src/em_gfx.h` defines `EM_GFX_OBJECT_TEX_MAX=512`; the protected Metal
registry enforces that limit. Each verified AREA00 export contains 579
distinct keys, following AREA01's eager whole-bank/shared-resource method.
`em_world_textures_live.c::catalog` rejects the header before uploading
anything. `test_area00_resources_reference.py` reproduces that refusal
using the production parser and records zero registry mutations.

Requested change: raise `EM_GFX_OBJECT_TEX_MAX` to **1024**, audit every
array/count consumer (including Metal), then run the existing registry,
GS and pixel regressions and this resource fixture. No change to original
game behavior, sampled texels or assertions is requested. This branch
does not edit the protected header or any renderer file.

This is a capacity requirement of the existing **eager export policy**,
not a proof that 579 textures are simultaneously needed by gameplay.
Removing only world models absent from the endpoint snapshots still
leaves 527 keys; equipment/page sources contain additional eager keys.
A smaller catalog would require a complete binding/reachability proof
(including intermediate frames and side actions), not silent truncation
to fit 512. No such proof is claimed here.

No additional protected VU program gap was established. Current page
support includes the known sprite/streak/kind-2/kind-6/floor/ripple paths.
Later channel-9 workers reached in `a00_09` remain an integration audit
item, not a substantiated request to edit a protected file.

## Verification and reproduction

Run on native arm64 macOS in `extermination-port-level3`. The final builds
and reported verification jobs were serialized at nice priority 15. The
initial baseline attempt printed a sandbox `setpriority` warning and
continued; the final source and index builds were repeated through the
approved host path (`build-baseline.log` retains that initial warning).
If a sandbox denies setting
priority, use host approval for the same command; do not accept `nice`'s
warning and continue at normal priority.

```sh
cd /Users/abe/Documents/Extermination.nosync/extermination-port-level3
nice -n 15 make -j4
nice -n 15 python3 tools/export_area00_world_textures.py
nice -n 15 python3 tools/export_area00_shadow_receivers.py
nice -n 15 python3 tools/test_area00_resources_reference.py
nice -n 15 env EM_TEST_FULL=1 python3 tools/test_area00_resources_reference.py
nice -n 15 python3 tools/test_area00_state_reference.py
nice -n 15 env EM_TEST_FULL=1 python3 tools/test_area00_state_reference.py
nice -n 15 python3 tools/test_scene_no_shadow.py
nice -n 15 python3 tools/test_level_smoke_area00.py --verify-harness --out build/level3/verify-fork
nice -n 15 env EM_REFERENCE=v263 python3 tools/test_level_smoke_area00.py --verify-harness --out build/level3/verify-v263
nice -n 15 python3 tools/test_level_smoke_area01.py --verify-harness --out build/level3/verify-area01
nice -n 15 env EM_HEADLESS=1 EM_STARTUP_TEST=newgame-control build/extermination
# Long diagnostic: expected to FAIL until the live bindings are complete.
nice -n 15 env EM_PS2_DISC_DRIVE_TIMING=1 python3 tools/test_level_smoke_area00.py --run --until a00_arrival --out build/level3/native
# Future complete route and independent side return:
nice -n 15 env EM_PS2_DISC_DRIVE_TIMING=1 python3 tools/test_level_smoke_area00.py --run --until a00_10 --out build/level3/progression
nice -n 15 env EM_PS2_DISC_DRIVE_TIMING=1 python3 tools/test_level_smoke_area00.py --run --side a00_s0 --out build/level3/side-return
python3 tools/check_no_disassembly.py --staged
# Required isolated build, after staging the source/test changes:
git checkout-index -a --prefix="$PWD/build/level3/index/"
(cd build/level3/index && nice -n 15 make -j4)
```

Fresh standalone results (ignored receipts under `build/level3/`):

| Check | Result | Receipt |
|---|---|---|
| AREA00 initializer, quick | PASS: 4 cases, 10,942,560 compared bytes | `state-test.log` |
| AREA00 initializer, full | PASS: all 10 captures, 20 cases, 54,712,800 compared bytes; ordered stores, aliases, lifetime, bounds and faults | `state-full.log` |
| Resources, quick / full | PASS export/capture equality and native shadow parsing/refusal; both texture catalogs explicitly BLOCKED at 579 > 512 before registry mutation | `resources-quick.log`, `resources-full.log`, `resources/report.json` |
| Canonical-state audit G6 | PASS: 755 files, 25 migrated progress/request bytes | `scene-no-shadow.log` |
| AREA00 fork harness | PASS: 13 C branch paths, 2,731 commands, 18,331 driver frames, 16,504 checker rows, 179 corruption rejections | `verify-fork/` |
| AREA00 v2.6.3 harness | PASS: 13 paths, 2,731 commands, 18,303 frames, 16,490 rows, 179 rejections | `verify-v263/` |
| AREA01 regression harness | PASS: 16 paths, 4,447 commands, 20,110 frames, 19,565 rows, 150 rejections | `verify-area01/` |
| Native arm64 source build | PASS, zero compiler warnings | `build.log` |
| Isolated staged-index build | PASS, zero compiler warnings; same staged source set, private scratch checkout | `index-build.log` |
| Native arrival diagnostic | FAIL as expected at `001FF080(1, 0)`: area 00 room 0 is not exported; loader `001FFCD0`, code 2; no AREA00 alignment | `native/run.log`, `native/receipt.json`, `native-diagnostic.log` |

The default resource check took about 10 seconds; AREA00 harness checks
took about 6 seconds. The full modes and native route are explicit opt-ins.
Harness counts certify the driver/checker contracts, not native gameplay.
The native diagnostic ran for 275.769 seconds, returned 1, and recorded
`native_parity=false`. Its in-process completions through `a01_06` are
not independent capture checks. The executable SHA-256 is
`a47ce92f6f3f085de21ddfb29e87ae0b29116e03a9cde11b2c049a6924877d89`.

Generated
assets, captures, pad files, logs and native objects stay in ignored
`assets/` and `build/level3/`. The real asset directory contains links to
the original checkout's existing assets; the four new AREA00 exports are
private files. `data/memcard/slot1`, `slot2` and `data/save` are private.

## Remaining differences and next binding step

AREA00 is not playable. The exact current stop is the missing area-0 scene
resource selection. Behind it are the catalog limit above, selective
overlay/state/script descriptors, canonical progress and scratch views,
roster callback dispatch, resource selection for sub 1, and the actual
actor/player/camera/effect/sound composition. Those are binding tasks over
existing translations, not permission to invent replacements. Sub-1
module-pack residency must also be checked before the switch's reload.

Concrete entry map (not a complete transitive worker audit):

| Existing entry | Required binding |
|---|---|
| `em_scene_bindings.c::area_read`, `world_scene` | Select AREA00 sub 0/1 resources after original loader delivery, preserving resource epochs and detach/rebind order |
| `state0_001AF8E0`, `state0_001D0660`, `w_001AFCA0` | Select AREA00 models/cells/messages/SFX and the area-specific initializer within the existing common owner; two record clears use stride `A060` |
| `area01_bind_live`, `em_area01_live_bind`, `em_area01_live_bytes`, `em_area01_runtime.c::dispatch` | Select the borrowed state provider and overlay callback table; overlapping `00823580` must never dispatch as AREA01 |
| `em_area01_script_live.c::em_area01_script_image`, `record`, `em_area01_script_bind` | Select module ID 1, original data/script windows and callback whitelist; keep mutable loader bytes |
| `w_001B6990`, `spawn_area01`, `area01_bind_roster`, `area01_rebind` | Bind original placements/deferred conditions; first arrival reaches `824EA0`, `825480`, `825600`, `825920`, then ferry cab `8263C0` |
| `em_world_textures_live.c::supported/ensure`, `em_shadow_live_select_area` | Select both verified subarea exports once the catalog policy can fit the registry |

The shared hull/spawn services `001A2370` / `001C5570` and door adapter
`001B0F60` already exist. `001F5940` has an effect translation but still
needs its live service adapter. First rebuild/arrival verification must
precede expansion of the remaining LOW/WORLD/HUD/FX dispatch surface.

After the main registry change, extend the existing composition with
area-specific descriptors, preserving its canonical actor/player/model/
collision transactions. Dispatch AREA00 overlays before the shared boot
dispatcher; do not let their addresses fall into AREA01's overlay table.
Bind one required worker at a time under the new smoke, retaining the
first fault until the complete original call path is present. Then run
the seven phase groups above, add original packet/field comparisons for
drawing, and promote only the census rows actually exercised and compared.

## Topic commits

- `d8b1916`: borrowed AREA00 state initializer, original-instruction test,
  canonical progress access and ownership audit.
- `b3f3b16`: both resource exporters, fork default selection, scoped shared
  texture discovery and production parser verification.
- `c654a3b`: strict AREA00 C replay phases, capture checker and quick
  positive/negative harness; existing AREA01 regression retained.
- The following documentation commit contains this ledger, the full
  census and the LEVEL_SMOKE phase contract.
