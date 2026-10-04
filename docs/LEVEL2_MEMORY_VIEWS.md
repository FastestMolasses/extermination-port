# AREA01 canonical memory callbacks

The callback APIs form a verified dependency checkpoint. Composite runtime
and shared-scratch installation described below remain work in progress;
they are not enabled for normal AREA01 gameplay.

The existing SYS, EXITA, EXITB and render translations now accept a canonical
byte callback. No translated game logic, RAM arena, initialization values or
worker substitutes were added. SYS/EXITA/EXITB append `view` to their contexts
and share the existing worker `ctx`. `EmArea01RenderWorld` appends `view` and
`view_ctx`. The callback receives `(ctx, original_address, size, write)`;
`write` is exactly 0 for loads and 1 for stores. Addresses are unchanged (the
math module's pre-existing callback separately normalizes RAM mirrors).

A non-null callback is authoritative: a refusal faults at the accessed
address even if a valid region/view array also covers it. Callback mode works
with null arrays. Empty/wrapped callback spans refuse. Null callbacks retain
the previous array path. Every scalar and quadword store goes through a
write-aware access; quadword alignment is unchanged. Render's shared UI and
AREA00 FX helpers carry the same flag. The status-pages positional world
initializer was changed to designated fields; the render-context initializer
was changed by the render lane. Existing structs must be zero initialized or
have the new fields explicitly initialized. Python ctypes layouts were updated.

`em_area01_scene_view(scene,address,size)` exposes only direct canonical
request/progress bytes and named `EmSceneState` fields. It owns no bytes.
It refuses reserved progress bytes, gaps, empty/wrapped spans and spans crossing
separate host fields. In particular camera-owned D_008101E4 and the unmigrated
D_00810811 are absent. Native scalar spans require a little-endian host.

## Verification

`EM_AREA01_CANONICAL_VIEW=1` runs each existing original-instruction suite with
its translated-side arrays disabled. The test-only callback borrows the very
same native oracle buffers; it neither copies bytes nor changes expected
comparisons. It is not runtime data or a proposed runtime RAM model. Existing
API fault checks continue to test the legacy array path.

Quick/full receipts are in `build/level2/memory-view/`:

| Suite | Full evidence |
|---|---|
| SYS | 2,108 cases, 3,048 side-effect cases, 867 single-load variants, 7,421 variants; 2,644,698 worker entries; 282 original runs left mapped test memory and remain explicitly not comparable |
| EXITA | 836 cases, 1,410 side-effect cases, 269 store variants; 31,062 worker entries |
| EXITB | 467 cases, 2,499 variants; 38,941 calls; 84 original variants left RAM and remain explicitly not comparable |
| Render | 21,306 cases; 91,911 worker entries; all 16 capture beats |

All passed. `tools/test_area01_memory_view_reference.py` passes 65 additional
native boundary checks: every render/UI/FX access width and direction,
quadword alignment, callback context, strict refusal despite usable fallback
storage, callback-only contexts, latching and clear. Default UI and AREA00 FX
quick oracles passed (266/176 cases, 4,228/4,843 worker entries), checking the
shared struct/helper extension with existing users. SYS's default array quick
suite also passed. No game frame gate is opened by this change.

The SIDE context now has the same optional authoritative callback. Existing
aim/fire region-array callers explicitly leave it null. The runtime binds
`001EFE00`, called by AREA01 `00828850` and the shared actor hit path, to the
existing SIDE translation. Its nested effect allocation publishes native
owners and refreshes the byte views before the original parent/position
stores. The original caller's stack offset is retained.

Canonical SIDE full proof: **775 routine cases, 1,110 effect cases, 2,869
compared calls, 1,555 effect field changes**, all ten branch sites both ways,
plus **2,295 existing-translation checks**. Receipt:
`build/level2/side-view-full.log`. The canonical boundary suite now passes
**89 checks**, including SIDE read/write width, strict refusal, no fallback,
sticky failure and callback-only storage (`side-contract.log`). These are
storage and worker proofs, not AREA01 route parity.


## Overlay direction and later reload lifetime

`EmArea01OvlHooks` also appends the strict `view(ctx,address,size,write)`
callback. Its load/store helpers preserve every original access width and
mark unchanged stores as writes. The composite runtime binds this callback,
not the old undirected `bytes` path. A refused callback never falls back to
`bytes`. This makes effect write-first scratch legal on its first full-word
store and prevents stores through read-only projections or immutable data.
The old callback remains supported for existing zero-initialized callers.

The overlay instruction suite's canonical mode compares load/store direction
as well as address, width, changed bytes and order, including stores whose
value was already present. Quick/full passed 2,192/14,034 cases, all 2,072
reachable non-branch words, and 18,439/75,221 worker entries. Every-call and
every-access refusal checks still pass. Receipts are
`build/level2/memory-view/overlay-{quick,full}.log`.
The strict native boundary contract now passes 86 checks; the overlay cases
include unchanged scalar stores, write then read, overflow/zero span refusal,
and no fallback after refusal (`overlay-contract.log`). Runtime composition
quick remains 32 chains plus six room states (`runtime/overlay-view-quick.log`).

Scene shared-scratch detachment must happen before `em_game_legacy_area_load`
unloads the old pose host. `area_read` now invokes the idempotent detach before
that load; state-0 `w_001AFCA0` also invokes it before collision reset for reset
without a new area load. The exact alias owners and preserved bytes are listed
in `LEVEL2_AUDIO_SERVICES.md`. This is a source-order audit; a same-AREA01 door
reload still needs full smoke verification.
