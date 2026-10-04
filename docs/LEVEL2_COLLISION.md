# AREA01 collision

The collision prerequisite audit, the borrowed collision boundary and the static placement kinds.

Contents:

- [AREA01 collision prerequisite audit](#area01-collision-prerequisite-audit)
- [AREA01 borrowed collision boundary](#area01-borrowed-collision-boundary)
- [AREA01 static collision kinds](#area01-static-collision-kinds)

## AREA01 collision prerequisite audit

2026-10-03. No emulator was launched. This audit reads the
original C, the locally generated original instructions, the existing native
owners and the recorded route. No gameplay return value was changed.

### Cell directory

The reported uid-0 bit-29 rejection is already fixed. The single owner is
`em_actor_cells_hull_offset` in `src/game/em_actor_collision.c`: it removes the
directory flag bits, then maps the uncached main-RAM mirror to its physical
offset. `em_actor_cells_init`, the hull readers and the transform use that
owner. AREA01's arrival already loads this directory through
`w_001AFCA0` in `em_scene_bindings.c`.

### 0019D770 no-span path

The discrepancy is real, but replacing the port's fault with a miss would be
wrong. `src/game/em_coll_segment_walkers.c:grid_walk` returns -1 when none of
six spans is shorter than the node count. Original `func_0019D770.c` is
NEARMISS; the original instruction body in the decomp's ignored
`build/asm/matchings/main/code/func_0019D770.s` confirms that its start, end and
column registers are assigned only when a strictly shorter span wins. The
choice finishes at 0019D978. The only direct caller is 0019A910, at 0019AA04.
Its incoming values are the mode byte, a stack address and an inherited
register. These are not a defined empty span. The existing native API has no
representation of those caller registers.

The existing oracle now checks a narrower, useful fact about the recorded
AREA01 route. It also constructs a whole-grid segment and executes the
original selector to demonstrate that the undefined arm really exists for
this data. Thus the data alone does not prove general unreachability.

#### Bound and evidence

The original rank search 0019F1A0 returns ranks in [0, N-1]. The recorded
AREA01 sub-0 grid has N=854; all six helper columns lie in [0, N]. Therefore
a failure to choose any span requires every span to have length N:

- For each even column, the live upper rank must be N-1 and its helper 0.
- For each odd column, the live lower rank must be 0 and its helper N.
- Since the coordinate columns are sorted, the Z endpoints must then cover
  at least the interval from the second odd-column coordinate to the last
  even-column coordinate: a Z separation of **1087.16015625** or greater.

`tools/test_coll_segment_walkers_reference.py:area01_span_domain` checks the
entire rank/helper data and all coordinate columns in **15 AREA01 snapshots**
against this grid. It checks **19,798 recorded AREA01 boundary rows**, including
main and side beats and excluding AREA00 rows after the exit, using the same
four camera-query shapes as the existing camera reference tests: camera
target to eye, eye to target, and eye to 200 units above/below. All **79,192
query shapes** have Z separation at most **53.519050**, far below the bound.
A 0.01 allowance covers decimal rounding in the trace; it is a test allowance,
not a gameplay clamp.

The original 0019D770 instructions, including all four original rank-search
calls, are executed through 0019D978 for the widest recorded query and the
whole-grid witness. The former chooses a span; the latter retains three
distinct seeded caller-register values. The witness stops before the unsafe
walk, and is not presented as a native/original outcome comparison.

This proves the no-span arm unreachable for those recorded boundary query
shapes. It does **not** prove the same of every mid-frame camera call, a later
area, arbitrary endpoints, malformed rank tables or camera paths added by
future binding work. The native fail-stop remains. Full live AREA01 smoke
must exercise the actual query calls before this is called a live guarantee.

#### Verification

The domain check runs within the existing
`make test-coll-segment-walkers-reference` target; it adds no new target and
does not change gameplay code. If the AREA01 captures are absent it reports
that portion as skipped explicitly. The regular first-level comparisons stay
unchanged. Quick/full results are recorded by the parent binding task.

The quick run passed in 13.8 s. The full run passed in 1,373.7 s: all
12,439 first-level rows (seven queries each), 15 captured re-runs, 3,000
cases for each of three unit families and 600 synthetic worlds. It checked
25,598 original hull-lock calls and 12,497 hit views, plus the AREA01 domain
and original selector witnesses described above.

### Remaining collision binding work

AREA01 owners first reach 0019B4C0 -> 001A06A0 / 0019CF50. Their standalone
owners are in `em_area01_sys.c`; they need the canonical collision
scratchpad, static grid, current class lists and actor record resolver.
`001AA000` already has a live owner in `em_coll_list_passes.c`; use it rather
than the standalone duplicate in `em_area01_sys.c`. The shared player,
camera, segment and list-pass modules remain the owners of functions already
bound for AREA11. Arrival already provides AREA01's EMCL and cell directory.

## AREA01 borrowed collision boundary

`em_area01_collision_view.c/.h` lets the AREA01 byte-view translations use the
already loaded collision world. It does not allocate cells, a grid, actors,
models, a second collision world, or a RAM image. Captures are used only by the
reference test. The runtime receives the loader's actual resource bytes.

### Canonical storage and boundaries

The host supplies `em_collision_world_segment()` and
`em_collision_world_move_scratch()`, plus the existing
`em_aim_fire_runtime_world_state()` instance. The latter is a new narrow accessor
for `R.world`; it must not be replaced with a newly initialized
`EmAimFireWorldLive`. In particular, `move_last` and `word_31BC` already have an
owner. The adapter keeps only address encodings, immutable initialization
metadata, read-only list serialization, and transaction/lifetime observations.

| Original address | Existing owner exposed |
| --- | --- |
| `70003190..31AC` | probe start/end, four lanes each |
| `700031B0..31BC` | existing `R.world.point16` projection of probe point XYZ and existing `R.world.word_31BC` |
| `700031C0..31C8` | probe delta XYZ |
| `700031CC` | existing move scratch's fourth delta lane |
| `700031D0`, `31D4`, `3254` | original-address encodings of native record/entity/self identities |
| `700031D8`, `3240..324A`, `324E`, `3B86`, `3B88` | probe kind, ranks, query class, span fields |
| `700030CA`, `30CC`, `30D0`, `30D4..30DC` | canonical cell result class, hull words and normal |
| `70003600..3608`, `3610..3618`, `3620..3628`, `3630..3638`, `3680..3688` | existing face/probe scratch |
| `7000368C` | existing move scratch's fourth work lane |
| `700031F8..323C`, `324C`, `3250` | read-only `00199C50` metadata and canonical cell count/address |
| class-list cursors/counts and occupied slots | canonical published/live lists; pointer words serialized read-only |
| cell directory original address and its `+20000000` EE mirror | the same mutable `EmActorCellTable.bytes`, including re-transformed hulls |
| grid resource interval | borrowed immutable loader bytes |

A begin imports the previous move writer into the probe/face representation if
`R.world.move_last` is set. It transfers every overlapping represented field
before clearing that flag. It leaves probe-only face scratch alone. Move record
pointers are decoded through the existing grid owner. The record normal/axis
inside `EmCollMoveScratch` describe an immutable grid record, not additional
scratchpad words, and are not copied as scratch state.

Every subspan of the point quadword aliases the same temporary serialization.
Commit validates the generation, canonical pointer fields, and original point
before publishing its XYZ/fourth word and decoding the pointer words. Unknown,
misaligned, or non-round-tripping identities fail. There is no fabricated
identity for an unknown native pointer. A grid record word must be exactly one
of the actual 64-byte node starts. A cell record word is `700030B0`.

`bytes` refuses empty/wrapping accesses, unsupported mixed-owner spans, writes to
immutable metadata/resources/lists, and accesses outside its owned spans. A
read spanning mutable cells and an enclosing loader delivery also refuses: the
loader's input bytes must never replace the updated directory. `owns` lets the
composite make this refusal final instead of falling through to a generic
loader or scratch mapping. No field's omitted fourth lane is invented. The
supported point quadword is explicitly composed; other incompatible spans
must remain failures until given a verified canonical composition.

### Identity and lifetime binding

Set `host.pool` to the existing actor pool and `host.player` to
`player_states_actor()`. With `address` and `identity` callbacks left NULL, the
adapter maps only exact pool record addresses and the canonical live player to
`008102B0`. This is the same player pointer already passed to
`em_collision_world_bind_player` by `em_player_stage_live.c`.

A physical pool slot retains its EE identity while free; resolving an address
does not allocate it or make its fields available in another provider. Each
borrow snapshots all 256 pool generation/allocated pairs and refuses a commit
after allocation, free, reset, or reuse. These are observations, not a second
pool. Optional identity callbacks exist for independent fixture/owner
compositions and must round-trip. The host's generation callback must change
when resource/scene storage is rebuilt. The generation cannot change during a
binding: rebind against the newly loaded owners afterwards. The player view
independently guards the player's own reset and pose lifetime.

### Worker ownership

`em_area01_collision_view_call` reuses these existing translations:

- `0019A570`, `0019A910`: segment/camera queries.
- `0019B6C0`: surface query, with the existing face/round workers.
- `0019F1A0`, `0019ED80`: rank and grid-node leaves.
- `001A4030`, `001A50A0`, `001A5C30`: cell prim leaves. The new bounded
  `em_actor_cells_prim` accessor reuses the owner's existing `prim_size` logic
  and refuses a prim extending beyond the current mutable directory.
- `0019AB20`: the existing actor-collision ground query, extended with an
  optional shared-state output.

`0019B4C0`, `001A06A0`, and `0019CF50` remain owned by `em_area01_sys.c`; no
worker body is copied into this adapter. The test runs that SYS chain over the
borrowed views with the real native SDK memory adapter and collision leaves.
No collision worker is a return-value stub.

The ground extension is necessary because the existing hit-only API discarded
scratch state. `em_actor_collision_ground_state_0019AB20` runs the same body,
starting from the supplied state so untouched fields survive. Original
`0019ABF0/ABF8/AC00` clear start/end W and entity. Original `0019F76C` stages the
cell-record pointer; the static-cell pass publishes its kind in `70003B88`.
On a hit, `0019AC78..ACA0` restore end XYZ and store delta; on a miss `0019ACD0`
clears the record; `0019ACD8` stores kind. The old hit-only entry remains a
wrapper using local state, with unchanged returned hit fields. The original
NEARMISS C was checked against the original instruction listing; the oracle
executes the pinned instructions rather than treating the readable C as proof.

### Composite integration

After the actual collision load and `00199C50` initialization:

1. Bind the host's shared/segment/move/pool/player pointers as above. `file`
   is an `EmSlgCollFile` over the loader's original `D_0028A598` grid resource;
   `d28A5A8` is the real relocated directory address, with the canonical cell
   table count. Resource extents must include all node/rank/vertex spans. The
   adapter calls the existing `em_slg_00199C50` to derive pointer metadata; it
   does not reconstruct it from a capture.
2. Begin collision alongside actor/player views on resume. Commit it alongside
   those views on suspend. Route `owns(address,size)` exclusively to this
   provider before a containing loader resource or broad scratch view.
3. For collision dispatch, first publish actor/player projections so native
   class-list actors are current. Reopen their views for operand access; commit
   the collision view, call `em_area01_collision_view_call`, then begin the
   collision view. The call requires its collision view inactive, and uses
   `host.bytes` for non-collision operands. Ground mask bit 31 writes the
   actor's projected `+B4`, to be published by its normal commit. A post-stage
   player `+B4` is a protected hip projection and correctly refuses that store;
   the arrival math's mask 7 does not request it.
4. The direct SDK memory workers operate while all byte views are active;
   they do not access native actors. Internal calls from SYS therefore retain
   the original stores and call order. Do not reset collision state per worker
   or replay a call after failure.

Add only `src/game/em_area01_collision_view.c` to the build's game sources;
existing dependencies are already runtime owners. No scene/Makefile/index
changes were made by this adapter task.

Shared-file changes are `em_aim_fire_runtime.c/.h` (borrow accessor),
`em_actor_collision.c/.h` (bounded prim accessor and optional ground state),
`em_coll_probe_original.h` (tag the existing state for its forward declaration),
and one `em_collision_world.c` constant. The last fixes class 7's list base
from `0028AD30` to `0028AC30`: original `001AF8E0` writes AC30, as the existing
actor-collision header and instruction oracle already recorded. AD30 would
overlap the class-4 list interval. The new test executes the original reset and
compares all six published/live list cursors and counts.

### Evidence and limits

Run `python3 tools/test_area01_collision_view_reference.py`; use
`EM_TEST_FULL=1` for the full sweep. It composes the existing segment oracle
fixture with `tests/area01_collision_view_bridge.inc`, keeping captured RAM only
inside the test harness. The original executable is SHA-256 pinned. Relevant
captured code ranges must match that executable before executing them.

The 15 end snapshots still in AREA01 cover the seven main-route positions and
eight side routes. `a01_07_level_exit` ends in area 00 and is explicitly excluded
from pairing with the AREA01 grid. Quick runs 1,095 original calls; full runs
37,365. They compare return values and every represented scratch field, including
pointer encodings, retained point W and face work. Both execute positive and
negative direct node/prim tests, ground grid hits, and the SYS call chain. The
4,560 additional boundary cases cover mutable/mirrored cells, enclosing delivery
refusal, original initializer metadata, all six original reset list bases and the
non-collision fade word outside their backing interval,
point subspan aliasing, pointer commit/refusal, stale state, all pool identities,
and actual pool allocation/free/reuse while borrowed. Only the original reset's
`00121A28` memset is an argument-checked test boundary; it is not collision logic.

Receipts are `build/level2/collision-view-quick.log`,
`collision-view-full.log`, and `collision-view-actor-regression.log`. The
unchanged hit-only API's existing actor-collision quick regression passed 1,840
ground cases, 588 columns, 243 re-transforms, 400 list operations and 14 truck
rows. This proves the storage/worker boundary for the exercised shapes, not a
complete live AREA01 frame or every possible operand alias. Unmapped spans,
unknown identities, missing workers and original undefined rank-span paths
remain fail-stop; no frame gate is bypassed here.

## AREA01 static collision kinds

The native crate initializer failed because the collision world had no
placement-kind view. Original `0019F730` reads that byte before testing a
static hull. The existing walker correctly refused the missing data.

The original C and matching-assembly source of `0019F730` were inspected.
Its first pass stops on a directory entry without bit 31, skips bit 30,
then reads the unsigned byte at `placement[i * 0x28 + 8]` through
`D_0024D7C0[area][sub]`. It stages that byte in `70003B88` and applies the
kind gates before the hull bounds. Kind `0x52` requires query class two.

Every one of the 16 AREA01 captures has 38 cell entries and resolves the
placement pointer to delivered overlay address `0082BD50`. Entry zero is
`A000009C`, kind `0x52`; entry one is `C0000448`, kind `0x51`, skipped by
bit 30. Entry two ends the static prefix. The complete 38 placement
records equal the original overlay in every capture: 24,320 bytes checked.
The existing cell owner already handles entry zero's uncached RAM mirror.

Crate node `007AAB70`, uid `1900`, calls `0019AB20` from its `001551B0`
initializer. Its query class is four, so the original skips entry zero
after reading kind `0x52`. A null native kind view instead fails before
that gate, regardless of the crate's position.

The integrating lane added
`em_collision_world_bind_static_kinds(placements, count)`. It derives the
bounded byte projection required by the existing walkers from the roster's
immutable records, inside the existing world owner. Unload clears it.
The AREA01 scene binds it from `s_roster01`; no actor or collision world is
duplicated. AREA11 continues without a kind view because its static pass
stops at entry zero.

`tools/test_area01_static_ground_reference.py` and
`tests/area01_static_ground_bridge.c` exercise this actual public setter
and the canonical live collision world. The fixture loads the real AREA01
EMCL and directory, then supplies captured hull transforms and published
actors. It executes the original crate initializer with only its state
byte reset to zero, catches its first query, and compares that query with
all original ground-query callees running. This is an initializer
perturbation, not an unmodified route replay. Probe X/Z scratch values are
preserved from the executed caller.

- Quick: PASS, 144 ground cases in 3.857 s.
- Full: PASS, 928 ground cases in 7.978 s.
- Both include all 16 captures, the missing-kind failure, all projected
  bytes, class 0/2/4 gates, upward/downward and inside/outside queries,
  real grid results, rejected bindings preserving the valid view, unload,
  and a later AREA11 load. There are 22 failure/cleanup contracts.
- Every ground result field is compared bit for bit using the existing
  actor-collision oracle. Observed return kinds are clear/grid (0/4);
  this test does not claim a static-cell contact. The original static-hull
  paths execute for admitted class-two queries.

Strict C compilation, Python compilation, scoped whitespace and
no-disassembly checks pass. Receipts are in ignored
`build/level2/static-ground/` (the first quick console log is under
`build/level2/runtime/`). No capture, original bytes or assets were added
to source. This focused proof does not establish a native AREA01 route
milestone; that requires the continuing live probe.

The checkpoint includes the public collision-kind API and focused proof;
the scene call described above belongs to the larger guarded integration.
