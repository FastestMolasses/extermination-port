# AREA01 scripts, interaction and effects

Scripts and the camera timeline, messages, placed doors, pickups, Use and status, effects, indicator children, flames, the gun auxiliary workers and the equipment child.

Contents:

- [AREA01 live script binding](#area01-live-script-binding)
- [AREA01 message-bank binding](#area01-message-bank-binding)
- [AREA01 placed doors](#area01-placed-doors)
- [AREA01 canonical pickup owners](#area01-canonical-pickup-owners)
- [AREA01 canonical Use and shared status services](#area01-canonical-use-and-shared-status-services)
- [AREA01 effects service boundary](#area01-effects-service-boundary)
- [AREA01 canonical indicator children](#area01-canonical-indicator-children)
- [AREA01 flame and shared render services](#area01-flame-and-shared-render-services)
- [AREA01 gun auxiliary workers](#area01-gun-auxiliary-workers)
- [AREA01 equipment child](#area01-equipment-child)

## AREA01 live script binding

The live host remains `em_area11_script_host.c`, and the sole interpreter and
opcode implementations remain `em_area_script.c` / `em_script.c`. The optional
area binding supplies resources and original worker boundaries. AREA11 retains
its old resource selection and rejects extended handlers without this binding.

`em_area01_script_live.c` borrows the delivered AREA01 overlay data at
0x828A00..0x82CD00. Each resolution checks the delivered MWo3 header's id 2 and
load address 0x823500 and reacquires the loader pointer. No exported overlay
copy is executed. An AREA11 script address cannot silently choose its old image
while the AREA01 binding is installed.

Four private EMSC files under `assets/area01_boot_scripts` hold 7,568 unmodified
ELF bytes: panels 0x246C20..0x247E20, doors/data 0x24D8F0..0x24DF80, pickups
0x2482C0..0x248540, and take scripts 0x266620..0x2668A0. The door program accessor,
door kickoff patcher, and interpreter observe the same mutable boot descriptor.
The walk table and sound descriptors reject writes. Door destinations are the
existing `assets/area01/door_destinations.emsp`; their two read-only ranges stay
owned by this binding. `tools/export_area01_boot_scripts.py` reads the pinned
boot ELF, walks 77 reachable records, and records differences in all 16 original
captures without using those runtime mutations as initialization. It refuses
output paths containing symlinks. Install with
`python3 tools/export_area01_boot_scripts.py --out assets/area01_boot_scripts`
(the default `--out` is a build directory); the files are ignored and must
not be committed.

### Canonical state and calls

Reset the shared host with the real pool/scene, then bind `EmArea01Script`.
Start and tick use the existing shared host APIs with native views published.
A typed script-block view is loaded/stored from the actor's real scratch bytes.
Owner slots now cover the actual pool capacity, so multiple placed doors have
independent script state. Inactive actors return 1 as original 001BA1F0 does.

The native script host publishes its vectors and active script block before
an area callback and reloads them afterward. The `owner_bank` get/set callback
stages only +0x40; changed values publish before the animation/record worker
and at tick end. Root installs the callback via `em_area01_script_bank`: reads
use the authoritative actor snapshot, writes bracket a live transaction and
store only +0x40. There is no pointer into an inactive serialized actor view.

Root's script worker resumes the canonical transaction, calls the runtime,
then suspends it. Nested calls need a stack span below the incoming script
worker SP, not the outermost logical top: original 001BA1F0 reserves 0x40 and
op09 tail-calls its callback. The new 826950 callback reserves 0x50. Private
locals must not overwrite an outer translated callback's live stack span.

AREA01 op09 callbacks are selected by overlay identity: 825130/825240 (the
existing overlay owner), 825910 (existing revisit owner), and 826950 (new walk
callback). Boot callback addresses are explicitly admitted; unknown functions
fail. `em_area01_script_workers.c` exposes `handles`/`call` for 1B7670 (existing
AREA00 low), 1B6AE0 (existing AREA02 misc), 825910 (existing revisit), and 826950.
The reused modules borrow exact canonical spans without a new arena or byte
copy. The missing 826950 implementation was absent under both its runtime and
shifted link name; it follows byte-matched `func_overlay_AREA01_00826910.c`,
including sound calls at elapsed 20, 40, 55 and the original worker arguments.

The optional interpreter handler field forwards op05, 0E, 12, 13, 17 and 1A
to existing original owners. op0A sub8 calls the shared player reset 001798D0.
op00 kind8 reuses `em_pickup_camera_settle`; op0B sub1 tests script +0x0E bit
0x1000. op01 kind8 is now the missing original walk branch in the sole op01
implementation, using the shared SDK sine/cosine workers. Existing branches
are unchanged. The appended worker fields are reflected in the ctypes layout
check of the existing script oracle.

### Verification

- `tools/test_area01_script_host_reference.py`: quick 54 variants / 9,840 exact
  ticks; full 81 variants / 22,052 ticks. All normal and skip variants compare
  original interpreter/implemented handler instructions, ordered worker calls,
  script blocks, modeled globals/player/camera, message bytes, mutated records,
  and reject stray writes. Extended handler owners, callback bodies, timeline,
  pose completion and placement are explicit boundaries in this composition
  test, with identical external-service responses on both sides.
- The same command runs `tests/area01_script_binding_test.c`: boot aliasing,
  read-only bounds, actual replacement-delivery pointers, wrong overlay id,
  AREA11 address rejection, callback arguments, model-bank reads/writes,
  missing callback and rejected-worker paths.
- `tools/test_area01_script_workers_reference.py`: quick 81 cases / 81 worker
  boundaries; full 1,136 cases / 848 boundaries across all 16 captures. Original
  callback instructions run; every direct worker's function, argument lanes,
  SP and game-state bytes match, followed by final state and return comparisons.
  Math/quad/placement leaves execute the original instructions as the oracle;
  audio-service calls are explicit boundaries.
- Existing `tools/test_area_script_reference.py` quick regression: 56 variants,
  1,382 lockstep ticks; 11 AREA11 route beats, 6,031 frames, zero differences.
  Logs: `build/level2/scripts-{quick,full}.log`,
  `script-workers-{quick,full}.log`, `scripts-area11-regression.log`.

### Integration and remaining boundaries

Root owns the Makefile, runtime/scene dispatch, native transaction callbacks
and progress documentation. Add both new script C files to the build. Route
1BA1A0/1BA1F0 with the existing shared host, canonical boot reads/writes through
`em_area01_script_bytes`, and the four helper functions through
`em_area01_script_worker_call` while original-address views are active. Reuse
root runtime's existing math/SYS owners for other extended handlers. Release
the resource binding only after resetting the shared host's borrowed pointers.

`em_area01_script_player_banks(s,map,ctx)` registers the canonical delivered
regions for special-animation bank rows 0x96, 0x97, and 0x98. It merges overlapping
tails of the same loader read before calling the existing player pose mapping
API, whose region contract rejects partial overlaps. There is no extra bank
copy or pose host. The shared interaction host's map-banks callback uses this
helper; reset the pose host before replacement loader delivery ends the borrowed
resource lifetime. The binding test covers unsorted overlaps and missing banks.

### Camera timelines

The new `em_area01_timeline_call` handles 0022EC30/0022EEF0 while canonical views
are active. The actual AREA01 records select
scene 2, bank 0x96 clip 2 (record 0x82AED0), and scene 35, bank 0x98 clip 0
(record 0x82BB90). Scene 2 binds original fade/flag event tracks 0x26AC80 and
0x26AAE0; scene 35 emits cue 6 at t=50. The implementation was checked against
the matched setup C and original instructions, and the NEARMISS 0022EEF0 C plus
original assembly, including post-worker state reload and cursor update order.

The raw camera samples are borrowed directly from their canonical loader bank.
`em_cinematic_camera_sample` remains the single sampler. The existing playback
tick's post-sample work is factored into `em_cinematic_playback_sampled_tick`;
both the AREA11 wrapper and AREA01 use that body. AREA01 publishes the original
eight sampled floats, cut state, scratch matrix/vector, camera output and zoom
in the original order. Unknown scene IDs, audio tracks, foreign event cursors,
missing resources/workers, and inconsistent track length fail rather than
silently succeed. The normal op00 kind6 setup supplies matching track lengths.

The shared script host owns D275BFC, D275C98 and D8234C0 and exposes those exact
bytes through `em_area11_script_host_timeline_bytes`; the AREA01 script provider
forwards them. These globals survive an area resource reset, as the original
does: every recorded AREA01 arrival retains cut counter 32 from AREA11. The
AREA11 path publishes its sampler output into the same global storage; the
counter is carried from actual playback, never initialized from a capture.
`timeline.emsp` adds three sparse read-only ELF windows: two
32-byte scene-2 event tables and the existing 52-byte SDK tangent coefficient
table. The boot exporter generates it locally in the same private directory.
There are no original data bytes in source and no second runtime camera-track
resource allocation.

`tools/test_area01_timeline_reference.py` executes the original setup and tick,
including the full original sampler/tangent/rotation, over the two delivered
camera tracks. Quick: 630 ticks / 1,299 service boundaries in three captures.
Full: 11,698 ticks / 23,600 boundaries in all 16 captures, including every half
frame of both complete timelines in the first capture. It compares each
boundary's function, arguments, SP and canonical state, followed by final bytes
for camera/cursors, sample output, cut/start globals and both scratch outputs.
Six negative checks cover unsupported scenes, missing service, audio state,
foreign cursors and inconsistent length. Published camera, cue/fade and render
services are explicit boundaries in this test, not evidence of their adapter.

Root integration adds `em_area01_timeline.c` and forwards the two originals to
the adapter. The host must provide 21BAB0, AEDE0/AEE10, B1E20, B0250, 21B9A0,
D2830, DD980 and D25F0 through their existing owners. 21BAB0 preserves its
64-bit return. The original setup/tick stack reservations are 0x20/0x30.
The unchanged AREA11 camera regression passes 480 original driver cases and
424 wait cases in quick mode; full passes 1,526 driver cases, 1,470 wait cases,
malformed projections and sanitizer checks, with exact state and its captured
t=25 result. Logs are
`build/level2/timeline-{quick,full}.log`, `timeline-area11-regression.log`, and
`timeline-area11-full.log`, plus `scripts-timeline-binding.log` (54 variants /
9,840 unchanged script ticks).

## AREA01 message-bank binding

The AREA01 rebuild now selects `assets/area01/message_data.emmd` through
`em_message_live_select_area`; AREA11 selects its existing message export.
This changes the resource and record-table views without reinstalling the
frame service. The request block, text style, draw buffers, glyph state,
host, streams and presenters keep their existing storage and values.
Missing or mismatched data latches a fault. Message reset remains at the
original `001FC9B0` call sites.

Original callers: `001FCA10 -> 001FDB80 -> 001FD790` fetches records from
`D_00264DD0[D_00810700+1]` for an area line. `001FD950` draws that line
through the loaded bank at `D_0028A594`; global lines keep `D_0028A4E8`.
The original instruction oracle checks these service paths. The native
resource selection is in `w_001AFCA0`, after the area's collision load;
it is not an invented original function call. It runs before any AREA01
owner can post a message. It does not enable the still-unbound world frame.

The EMMD loader previously accepted area 23 into an array of length 23
and rejected area 0. The range now accepts indices 0–22, matching the
allocated views, and rejects any index outside them. The first-visit
binding uses only 1 and 11; no additional area is claimed live.

### Verification

`make test-message-area-reference` builds the actual live service with
test-only frame, font-upload and renderer boundaries. Its bridge inspects
the service without adding a production introspection API. The streams,
face and presenter boundaries record calls, not replacement game logic.

The test switches AREA11 -> AREA01 -> AREA11 -> AREA01 and reselects AREA01,
checking that each selection preserves the complete dynamic service/draw/
glyph state and all bindings, without reinstalling step F. AREA01's global
and area bank bytes (**11,956 bytes**) equal the original NPC-conversation
capture at its actual resource pointers. It checks a missing-bank failure
and the persistent fault latch.

The preservation fixture combines a recorded busy request prefix (a01_05
f300) with the end snapshot's remaining request bytes, synthetic nonzero
draw-buffer sentinels, and glyph records produced by a test string through
the real glyph owner. It deliberately makes accidental resets visible;
it is not presented as one captured original whole-frame state.

The original-instruction service comparison uses
`build/s87/route_a01/a01_05_npc_bridge_talk` in the decomp:

- Quick: **18 cases / 54 ticks**, recorded lines 0x0A and 0x40 and the
  global shaft-door refusal 0x80000008, both game modes and three voice modes.
- `EM_TEST_FULL=1`: **1,110 cases / 3,330 ticks**, every area-table starting
  record plus the refusal, with the same mode combinations.
- Every case compares all 156 request-block bytes, all 14 shared mode/
  mailbox bytes, and every non-draw worker call on each tick. The native
  glyph/layout routines run; the service oracle's draw boundary is stubbed,
  so this test does not claim a pixel or glyph-packet comparison.

Both runs pass. This is service/bank integration evidence, not the main
AREA01 route smoke: the frame-machine guard is still present.

Shared edits: `em_message_live.c/.h` (preserving resource switch),
`em_scene_bindings.c` (area-aware resource selection), `Makefile` (quick
oracle target). New files: `tests/message_area_bridge.c`,
`tools/test_message_area_reference.py`, this document. No launcher option
was added and no disc-derived bytes are embedded in source.

## AREA01 placed doors

2026-10-04. `em_area01_door_live.c` binds the shared door originals to
canonical records, delivered data and named workers. It owns no placement
singleton, pose/model bank, actor array or script copy. Each invocation
projects the actor's door fields, publishes them before a worker, and
refreshes them after the worker. This permits all five AREA01 doors to
coexist without sharing phase, side, animation flags or script cursors.

### Original owners and shared implementation

Original C was read for 001BBDA0, 001B0F60, 001BBE40, 001BC0E0,
001BC150, 001BC240, 001BC290, 001BC300 and 001BC350. These are matched
functions; the existing original-instruction oracle remains the semantic
check. AREA01's shaft controller 00823580 is already translated in
`em_area01_overlay.c`. It keeps its seven phases, story-byte tests and
linked-node publication, calling the same boot door functions as before.

`em_door_original.c` now exposes its existing initialization, script-pump,
commit, close and publication leaves by original address. Its existing
001BC350 controller uses those same leaves. The adapter forwards all model,
animation, script, collision, draw and fade work through `EmArea01Call`.
The separate AREA11 resource opener and pose runtime retain their existing
fence binding and behavior; AREA01 uses the canonical model/pose owner.

The adapter's 001B0F60 uses `em_slg_001B0F60`, forwarding 001B0EA0 and
001C63E0. The allocation's original +0x40 bank store occurs between those
workers. That field must be a writable canonical projection of the Box
owner's `anim` member. The door adapter does not modify a second copy of
the model owner's state. Initialization publishes D_002755F0 at +0x30;
the shared original leaf captures the door ID at +0x34, clears +0x2E and
sets the original +0x80 color/scale words from the link flags.

`em_door_transit.c` remains the sole owner of 001BBE40 geometry and
001BC150 destination selection. Its private preparation steps were
factored so kickoff patches the script after selecting/storing the side,
then computes/stores player yaw, then constructs the alignment point.
This corrects the former placement of all geometry calculations before
the patch/face callbacks while preserving `prepare`'s complete output.
The sound comes from `em_sdf_001BBD60`, reading only the selected halfword.
`em_door_program_patch` now supports the original locked arm's two clip
stores as well as the existing ordinary arm. Other program bytes survive.

### Binding contract

`em_area01_door_bind` takes the same bytes/worker host contract as the
AREA01 runtime. The caller keeps its canonical actor segment active;
every native worker commits and starts a fresh segment. The adapter
borrows pointers only within such a segment. Errors latch the reached
function or missing data address until a new binding.

`em_area01_door_handles` recognizes 001B0F60, 001BBDA0, 001BBE40,
001BC0E0, 001BC150, 001BC240, 001BC290, 001BC300 and 001BC350.
`em_area01_door_call` accepts an `EmArea01Call`. The shaft's 00823580
entry stays in `em_area01_runtime`; its shared boot calls dispatch here.
No callback or source placement address is substituted with AREA11's.

Required canonical data:

- Actor bytes, including +0x34 ID, +0x2E side, +0x56 link flags and the
  actual script block argument. Direct phase workers also support a block
  other than actor +0x1F0, matching their original arguments.
- D_0028A574, the delivered library animation bank; model allocation and
  pose workers use the existing shared slots and source resources.
- D_0024DB80 sound rows, D_0024E140's area pointers and destination rows,
  and mutable boot script bytes 0x24DBC0 through 0x24DF80.
- Player D_008102B0, request bytes D_008106B5 through B8, area D_00810700,
  door persistence D_00810841[area], and scratch point 0x700038A0.
- Delivered AREA01 overlay script bytes. The shaft's locked follow-ups
  use 0x829860 and 0x8298E0; the existing overlay selects them and the
  shared script host must resolve them from that delivered overlay.

Reached worker families are allocation 001B0EA0; pose 001C63E0,
001C64F0, 001C67E0 and 001C68C0; script 001BA1A0 / 001BA1F0; player
alignment 00182F90; math 001B1240, 001B1470, 0011E2A8 and 0011DE90;
publication 001B1B30; the actor's actual +0x4C callback; free 001AFC10;
and fade 001AEDE0 / 001B0C00. Missing workers fail. The layer performs
no room-load completion, unlock, script completion or animation fallback.

The recorded AREA01 set contains one shaft door and four boot doors.
Their IDs are 0x80, 0x81, 2, 0x83 and 0x86; subtypes are 3 and 0x15.
All read the delivered bank at 0xD191C0 in these captures. These addresses
and IDs are evidence only; production selection reads each live record.

### Verification

`tools/test_area01_door_live_reference.py` executes the original boot door
and shaft overlay code, checking original text against the pinned ELF and
delivered overlay. The native composition runs over copies of the same
capture data. Math and script-start workers execute original instructions;
model/animation, script pump, collision publication, draw and fades are
explicit boundaries whose implementations are verified by their owners.

At every non-math worker boundary it compares all five door records, the
shared script range, player/request bytes and alignment scratch, together
with function/argument order. Final comparisons cover the data ranges and
return values of non-void entries. Stack frames are call-private storage
and excluded. Pure math results are computed independently from original
instructions; scratch writes internal to the point calculation are
compared at the externally observable alignment boundary. No capture
pixels, instruction words or original data are committed.

| Check | Result |
|---|---|
| Door quick | 3 captures, 60 cases, 172 exact worker boundaries; 4.1 s |
| Door full (`EM_TEST_FULL=1`) | 16 captures, 640 cases, 1,943 exact worker boundaries; 16.9 s |
| Fail-stop | Unknown function, missing record, missing required argument, rejected place worker; each fault remains latched |
| Existing door controller | 5,788 instruction/state/order cases and 66 AREA11 fence-route frames pass |
| Existing transit quick/full | Original kickoff geometry, script patches, side selection and 112 destination cases pass |
| Existing AREA11 pose runtime | Source channels, owner matrix and palette compare exactly to first-control capture |

The full sweep covers lifecycle/phase dispatch, armed/unarmed, both sides,
ordinary/locked arms, shaft story gates, allocation refusal, room and
whole-area destinations, and independent script-block arguments. It does
not claim a live AREA01 door traversal or rendering screenshot: integration
still requires every reached shared worker and source range to be bound.
The root owns source-list and scene integration; no Makefile, scene binding
or index edits were made by this door task.

## AREA01 canonical pickup owners

`em_area01_pickup_live` adapts `00219550`, `0015AFA0`, `0015AC00`,
`0015AE20`, `001B6EA0`, `001F1110` and `001F1180` to the composite's canonical
actor, script, player and scene views. It retains no pickup roster or actor
state. `em_pickup_owner` and `em_pickup_items_original` remain the original
translation owners. The direct `0015AE20` entry now shares the existing
phase body without applying its caller's lifecycle dispatch.

Each reached worker receives stores made before its original call; the
adapter reloads its call-local layout view afterward. It writes the original
`+0x30` descriptor and `+0x2EC` child pointer, patches shared long-script clip
words and writes child shutdown to the child's own record. A freeing worker
is the last access to that record in the call. `001B6EA0` preserves inventory,
map/key and status-request aliases through the original byte addresses.

Library bind `001B1020` and placement `001C6380` use the model adapter's
existing owner-services translations and shared slot stack. Short and long
programs run in the shared script host. Publication, collision, child spawn,
positional sound, persistence and inventory are explicit composite workers;
an absent worker latches a failure. The aura's `+0x2D0..+0x2DF` is in the
same record. Its RNG uses the host's `00122BB8`; its draw-block callback is
bound with `em_area01_pickup_set_aura_draw`, and must bracket native views
before invoking the existing effects aura draw. No separate aura timer or
sprite state is stored by this adapter.

### Original callsite corrections

The byte-matched boot ELF instructions, not the readable NEARMISS C, govern
`00219550`. The initial descriptor is the **address** `0x00275878`, or
`0x00275880` when area is `0x10`. This was already recorded in the decomp's
`docs/NEARMISS.md` row for `func_00219550` and `docs/FINDINGS.md` (“00219550's
+0x30 is an address”, 2026-10-02); readable C remains uncorrected.

The call at `0x002196A8` is `001C5570(owner, 0x700038A0, 0x73, 1)`.
The preceding scratch vector contains `(0, 1, 0, 0.25)`. The returned child is
stored at owner `+0x2EC`. Readable C gives a reordered prototype/call; no
219550-specific correction for that order was found in the decomp's
FINDINGS or NEARMISS documentation. This adapter's oracle compares all four
arguments and the complete scratch vector at that boundary. The correction was recorded in decomp commit `f5a27bc` (only
`docs/FINDINGS.md` and `docs/NEARMISS.md`; no source change). The older standalone oracle also establishes child shutdown
as byte value **3**, and the second height threshold as another addition of
**6** for this owner, despite the readable C's differing expressions.

### Evidence

`tools/test_area01_pickup_live_reference.py` executes original instructions
with model/script/render/sound/inventory boundaries explicitly supplied on
both sides. At every boundary it compares the actor, child, shared script,
player/scene and scratch bytes, then compares all game-data spans and the
entire scratchpad after return. Original `001BA1A0` executes within the
script-start boundary. Test data comes from the existing AREA01 captures;
production has no capture reader.

| Run | Captures | Cases | Exact worker boundaries | Fault cases | Seconds |
| --- | ---: | ---: | ---: | ---: | ---: |
| quick | 1 | 44 | 109 | 4 | 5.42 |
| full | 16 | 311 | 683 | 4 | 35.19 |

The four fault cases cover unsupported entry, missing argument, missing
record, and failed reached worker; each latches and prevents later calls.
The shared pickup-owner regression also passes its full **6,216 lifecycle /
order / height cases**, **272 state-0 cases** and **144 consume cases**.

`tools/test_area01_pickup_aura_reference.py` executes original `001F1110` and
`001F1180`, including their SDK vector helpers. It compares the complete
actor at RNG and draw boundaries, the chosen sprite record and derived draw
arguments, and the entire actor after return. Native draw is an explicit
recorded boundary here; this is not pixel evidence.

| Run | Aura cases | RNG/draw boundaries | Draw boundaries | Seconds |
| --- | ---: | ---: | ---: | ---: |
| quick | 40 | 23 | 10 | 0.62 |
| full | 1,805 | 953 | 420 | 0.75 |

These are standalone adapter results. The live composition, Use selection,
takeover, status flow and sound require separate evidence; no live census
row follows from these tests alone. Logs are under ignored
`build/level2/oracles/pickup_*.log`.

## AREA01 canonical Use and shared status services

This is standalone original-instruction and native-host evidence. It does not
claim an ungated AREA01 playthrough or a live census promotion.

### Ownership and binding

`em_area01_interaction_live` adapts the original `00183EF0` candidate and
`00184BA0` scan to the composite canonical byte resolver. It reads the actual
published list (`D_00275B5C` / `D_00275B64`), actor/player fields, descriptors,
scene admission bytes and shared score. It reuses `em_interaction_scan`,
`em_interaction_pickup_candidate_views`, `em_door_candidate`,
`em_roger_candidate` and the existing descriptor-point gate. It owns no actor
roster, candidate metadata cache, player token, camera or animation state.
Candidates and their armed bytes are temporary call projections. A failed
predicate is a host failure and cannot become a successful scan candidate.

The winning actor's `+0B` is written before the claim callback. That callback
commits the canonical views, passes the native actor identity to
`em_area11_interaction_host_claim_scan`, and resumes the views. The shared host
uses its existing `EmInteractionRuntime` claim/stage-owner functions; the
original selector becomes 3. Existing script start/tick and player-stage hooks
then use that same owner identity. Bank registration is supplied by a callback
before the player takeover; AREA01's script adapter maps loader rows
`0x96..0x98` as merged borrowed canonical spans.

`em_area11_interaction_host_load_shared(directory, math, map_banks, ctx)` loads
only common status resources and initializes the existing frame, staged token,
pose and cinematic worker hooks. It does not load AREA11 placement metadata,
its seven-pickup roster, panel/elevator controllers or its SFX area selection.
The common status loader is factored out of the existing AREA11 entry, so both
entries use the same runtime/hub/model/page implementations.
`em_area11_interaction_host_math()` exposes the loaded host's immutable math
view. Root supplies the existing common status asset directory
`assets/scene_snow`; that path names shared assets, not AREA11 scene placement.

The shared host owns the exact previously unowned scratch spans
`70003640..365F` and `70003690..3697`, and exposes its existing score
`70003B98..3B9B` through `em_area11_interaction_host_scan_memory`.
`70003660..366F` aliases the existing effects `spad3660` vector through
`em_effects_live_scratch_3660`; `3600..363F` belongs to the collision/camera
alias. No broad scratch arena is added. The pickup candidate's optional views
publish its normalized vectors, raw displacement and dot/length results;
existing callers without those views retain their original API.

Six immutable boot descriptor windows are added to the existing
`effect_tables.emet` export, served by `em_effects_live_window` at their original
addresses: `275460` (16 bytes), `275470` (8), `275488` (8), `2755E8` (16),
`275878` (16), and `2758A8` (24). The total is **88 bytes**. They come from the
pinned local ELF, never a runtime snapshot, and introduce no mutable copy.
The complete export has **32 blocks / 60,052 source bytes**. Its existing
AREA11 check compared **1,020,884 bytes across 17 captures**. The six new
windows separately compared **1,408 bytes across all 16 AREA01 captures**.
Other effect-table bytes can be modified by AREA01, so this second check is
explicitly limited to the immutable descriptors.

### Verification

`tools/test_area01_interaction_live_reference.py` executes original `00183EF0`
and `00184BA0` over captured resource/actor layouts and moved player positions.
It compares the full **32 MiB RAM** and **16 KiB scratchpad**, return bits,
every ray-call argument and score at that boundary, plus winner/claim order.
The original SDK arithmetic executes in the oracle. `0019A910` is an explicit
controlled boundary here; this test does not establish collision geometry.
The fixture's claim verifies the actor is armed while the selector is still
zero, then publishes selector 3. Shared native claim behavior is checked by
the separate host test below.

The quick run passes **32 cases**, **10 owner forms**, **16 ray boundaries**
and **8 claim boundaries**, over one capture, in **0.82 seconds**. The full
run passes **850 cases**, **21 owner forms**, **315 ray boundaries** and
**230 claim boundaries**, over **16 captures**, in **7.50 seconds**. Exact
receipts are `build/level2/interaction/report.json` and
`build/level2/oracles/interaction_full.log`. Cases include ordinary and
special-action pickup eligibility, blockers and self hits, doors, descriptor
points, NPC facing, all captured published owner forms, forward/reversed
multi-owner lists, empty lists and each scan admission gate.
Special-action cases use a non-unit player vector W to verify the initial
whole-vector copy preserves that lane before the SDK normalization writes.

`tools/test_area11_interaction_host.py` passes with address/undefined-behavior
sanitizers after the common loader refactor. Its **16 existing scenarios**
still pass. Two additional shared-only scenarios pass: the full status
hub → ITEM → hub → MAP → hub → close route, and generic owner claim with
exactly one bank-registration callback, selector 3, canonical math identity
and teardown. The status route checks frame/draw order, module loads and
original closure latency using the same native services as before. These
are actual-asset native integration assertions, not a newly independent
original-instruction proof of the entire status runtime. Log:
`build/level2/oracles/shared_host.log`.

### Current limits

The adapter supports selector 0/default, 1, 3 and 4 and the action-`2D`
class-7 override. All 21 published owner forms in the 16 AREA01 captures fall
within these paths. Selector 2, selector 5 and selector-0 class-4 subtype
`2C` fail explicitly; they are not silently approximated. Missing descriptors,
workers or canonical spans fail. Structural validation may precede original
writes for invalid mappings; the instruction oracle establishes valid mapped
behavior, not identical exceptions for invalid original pointers.

The shared status panel-discharge route still requires a real bound panel;
shared-only initialization supplies no AREA01 panel owner. The root binder
must route the generic Use hook and all worker boundaries through the canonical
views and provide bank mappings before a claim. Live acceptance remains a
separate guarded native probe result.

## AREA01 effects service boundary

`em_area01_effects_services.c/.h` adapts three original calls to the existing
`em_effects_live` singleton. It adds no effect translation, node allocator,
model, sound binding, or mutable game-global owner. `001EFD20_result` is a
result-preserving entry to the existing EFD20 body; the old discard-result
entry remains a wrapper around it.

### Original call evidence

The pinned boot ELF is the instruction authority (SHA-256
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`). The
decomp sources are `func_001EFD90.c`, `func_001EFD20.c`, `func_001EF9D0.c`,
and `func_001EFE00.c`. Their incomplete C prototypes do not remove implicit
argument-register forwarding. The original-instruction oracle executes the
actual calls with the following arguments/results:

| Entry | Arguments | Result and original behavior |
|---|---|---|
| `001EF9D0` | a0 id, a1 nullable position, f12 raw scalar | Original pool address or zero; existing effect owner selects its table, allocates, sets callback/header/live38, and performs its existing light/sound branches. |
| `001EFD90` | a0 id, a1 position, a2 rotation | Rotation word +C supplies EF9D0's f12. Successful result gets both QWs and position W=1. |
| `001EFD20` | a0 id, a1 position | EF9D0 f12=1; successful result gets the position QW, zero rotation, position W=1. |

AREA01's original `func_overlay_AREA01_00826D00.c` (runtime owner 00826D40)
passes aligned scratch vectors 700038A0/700038B0 for ids 80000006/07/03 and
the randomized 26/2C/67 variants. The revisit translation's 00823900 path
passes owner+100 to EFD90/EFD20, and math/player passes 700038B0 with player+C0.
The original 001EFE00 caller makes a stack position, calls EF9D0, then on a
nonzero result stores source+14 at result+24 before copying source position
and rotation. This is the concrete first-write requirement for effect +24.

The adapter validates na/nf, copies raw vector bytes, preserves f12 bits and
the original returned pool address, and permits EF9D0's null position. It
requires non-null 16-byte-aligned vectors for the QW wrappers and rejects
unmapped/misaligned operands before allocation. It does not approximate
unaligned original SDK copies. Failure is surfaced to the caller, not converted
into the original successful zero-allocation result.

### Canonical fields

`em_effects_live_node_identity` validates native pool membership, allocated
state, self identity, slot kind, and generation. Projection then borrows the
same fields returned by existing `node_field`; it never creates another slot:

- Every effect: canonical `+38` live word and `+D0` matrix.
- Driver: its work `+1F0..+1FF`, `+244`, and `+24C`.
- Head sprite: `+24` owner, `+28` bone, `+A0` local vector, `+1F0` timer,
  `+244` ramp, and `+24C` scalar.
- Other callbacks: scratch remains EmActor-owned, with no extra projection.
- Non-head `+24`: the existing `parent24` word. Until the existing owner
  marks it initialized, it is a write-first projection. After its exact
  four-byte store commits, the completion hook calls `node_written`.

EmActor remains authoritative for header, position, rotation, and its other
represented fields. The actor adapter permits only scratch subranges to be
superseded by an effect projection; header/links/pose projections still fail.
An original-address request crossing legitimate owners uses the one transient
record image. An uninitialized +24 read, partial first store, or cross-field
first store fails; it never falls back to private bytes. Native effect ticks
must run with the transaction suspended, as do allocations and frees. Slot
reuse refuses the previous generation. This adapter does not revise the
existing effects owner's attach/allocation initialization policy.

### Integration

1. Add `em_area01_effects_services.c` to the existing game build. Keep the
   existing effects attach/load and pool behavior binder as their owners.
2. In the actor project callback, ask `services_project` first. A positive
   result supplies all its spans; do not overlay a model projection over the
   same effect fields. A zero result means no active effects owner.
3. After actor-view reset, set `view.written=em_area01_effects_services_written`.
   Use `actor_view_touch` for lifetime-only access. A full readable record is
   intentionally unavailable while +24 has not been initialized.
4. For these three calls, run `services_prepare(host,call,&spawn)` while views
   are active. It copies arguments into a call-local `EmArea01EffectsSpawn`.
   Suspend/commit all native-facing transactions, run `services_invoke`, then
   resume. No actor-view pointer survives this boundary. `services_call` is a
   convenience only for operands whose provider is valid with views inactive.
5. Native effects callbacks also require this commit/run/resume boundary.
   Missing handlers and the existing owner faults remain fail-stop.

`actor_view_snapshot` supports read-only logging between transactions. It
does not latch faults even on malformed projections or unreadable spans.
Snapshot failure must be reported as unavailable, not zeroed state.

No scene, Makefile, or index changes are included in this adapter patch;
the composite AREA01 binder owns those integration changes.

### Verification and limits

`tools/test_area01_effects_services_reference.py` runs nine full original
reset/spawn/allocation chains: each of the three services with empty entry,
other callback, and driver callback. Original 001AF8E0, 001AFA90/001AFA50,
EF9D0, EFD90/EFD20, and SDK QW copies execute; only reset's memset is an
explicit checked boundary. Returned address and all 752 bytes of the first
pool record are compared. The synthetic no-light/no-sound table isolates
this adapter. It is not a capture or a claim about live sound output.

The same test runs ASan/UBSan contracts for all three effect kinds, deferred
canonical writes, untouched EmActor scratch under shared fields, preserved
unshared scratch, first-store access, read-before-write refusal, stale native
writes, free/reuse, inaccessible source/alignment refusal, null position,
empty entry, and read-only instrumentation (including failure without a fault
latch). Quick/full intentionally run the same finite boundary suite.

Existing actor oracle regression: quick 60/full 600 original pool mutation
operations, all 256 record images after each operation, sparse original math
movement, and 16 existing refusal cases. Existing effect oracle regression:
quick 180/full 545 spawn cases; full 5,463 synthetic driver ticks plus 13
captured nodes/389 lockstep ticks. Both quick/full pass. Receipts:
`build/level2/effects-services-{quick,full}.log`,
`build/level2/effects-services-owner-{quick,full}.log`, and
`build/level2/actor-view-effects-{quick,full}.log`.

These are service, storage, and existing-owner proofs. They do not establish
a complete AREA01 frame or new sound accuracy. Existing em_effects_live
lighting/sound implementation and unsupported-handler faults are unchanged.

## AREA01 canonical indicator children

`em_area01_indicator_live` binds `001C5680` and `001C5760` through the
existing `em_indicator_child_step` translation. Its call-local record reads
the canonical actor `+04`, `+0A`, `+80`, and `+A0` fields, publishes writes
before each worker, and reloads after the worker. It never keeps a separate
colour record. `001F54E0` is the existing `em_effect_kinds` translation;
its RNG and indirect draw enter the same host worker boundary.

The generic model adapter now binds `001C2360` and `001C22A0` through the
existing `em_rvr` owners. They use the retained library word or delivered
world bank, the existing model setter and resource lookup, the sole
`boxes_slot_world` stack, and the existing default-bone translation. Model
words `+44/+4C`, matrix `+D0`, and slot addresses `+110` stay in the actor
view's private canonical bytes. Slots stay in the shared original-layout
arena. No typed indicator `Child` or AREA11 `Node.a0` state is created for
these generic children.

This is required by the first-visit `00826D40` state-0 manual spawn. That
owner allocates a class-C child, writes callback `001C5680` and model `7A`,
and subsequently writes its colour and copies matrices through `+11C`.
The generic callback registration must preserve that canonical path.
`001C5760` uses the same adapter, including its `+0A` placement branch.
Freeing enters the existing pool free and prepared generic `001AF800` path;
the adapter never refreshes a freed record.

### Verification

`tools/test_area01_indicator_live_reference.py` runs the pinned original
boot instructions over captured AREA01 resources, alongside the production
actor and model adapters. It compares the entire 32 MiB RAM image and 16 KiB
scratchpad after every child callback. The interpreter's private ABI stack
is outside those compared images. Original model binding, slot allocation,
default bones, placement, colour arithmetic, pool free, and bone release
execute in full. RNG and final `001CACB0` draw are explicit boundaries;
the draw boundary compares canonical colour and the first slot matrix and
writes the native drawn byte to verify refresh. The separate model-draw
oracle proves the actual draw translation.

| Run | Captures | Bind scenarios | Callback steps | Draw / RNG boundaries | Parent colour / matrix stores | Fault cases | Seconds |
|---|---:|---:|---:|---:|---:|---:|---:|
| Quick | 1 | 14 | 42 | 21 / 21 | 42 | 3 | 3.26 |
| Full (`EM_TEST_FULL=1`) | 15 | 315 | 735 | 315 / 315 | 630 | 3 | 8.48 |

Scenarios cover library models `73/74/75/7A`, world models `0/4/21`,
successful and refused binds (including a negative signed cap in full),
three RNG values, parent writes between callbacks, optional repeated
placement, and actual native pool/bone free. Worker failures latch, preserve
earlier writes, and prevent further callbacks. Logs are ignored outputs at
`build/level2/oracles/indicator_quick.log` and `indicator_full.log`.

The existing generic model quick regression also passed: 49 main cases,
2 retained-library cases, 2 pool release/reuse cases, and 4 failed-worker
transaction cases in 3.70 seconds. These are standalone proofs; live census
promotion still requires the guarded native route evidence.

## AREA01 flame and shared render services

`em_area01_flame_services.{c,h}` binds existing translations. It contains no
actor, model, packet allocator, renderer state or persistent scratch storage.
This is a prerequisite for the AREA01 arrival probe, not evidence that the
complete arrival or its framebuffer matches the original.

### Reached callers and ownership

The first AREA01 flame is node `007A96E0`, callback `001E3D90`, variant 2.
The main-route endpoint images contain 17 flames: five variant 0, seven
variant 1 and five variant 2. Its state 0 initializes the actor matrix,
phases and seed, then executes state 1 in the same call. For AREA01 key
`0100`, the fog-mode changes used by other areas are not reached. The
camera-mode-3 and z-below-minus-700 gate remains in the original SYS owner.

The reached render calls are:

| Entry | Existing owner | Canonical inputs and outputs |
|---|---|---|
| `001CD070` | `em_area01_render_gs` | Clip/context matrices from RCL; point `70003750`; writes shared `70003600` and effects owner's `00275C04` |
| `001CD2B0` | `em_area01_render_gs` | Reads the preceding projection; writes shared `70003610`, collision scratch `70003680`, and live owner's `00275C00` |
| `001CFAE0` | `em_weather_packets` | Writes the caller's original stack transform; reads one of the three canonical flame matrices and RCL's context word |
| `001CFBE0` | `em_effects_live` → `em_head_sprite_original` | Reads the transform and immutable descriptor; emits through the existing RCL packet and chain owners |
| `001F4A10` | `em_area01_render_gs` | Existing placement renderer; forwards RNG, `001C7900`, `001C6120`, `001D3990`, `001CB760` |
| `001E9E60` | `em_area01_render_hud` | Existing grid renderer; forwards `001CB5F0`, `001CB950`, `001CB6B0`, `001CB760` |

The SYS owner writes all 64 bytes at `700036E0` and `70003720` with
`00102958` before reading either matrix. Its first matrix remains the
existing `700036A0` owner. The proof starts the two new spans with poison,
then executes the native SDK copies and the complete original/native
state-0 body; no captured matrix seed is needed by the runtime. The
projection result `00275C00` is likewise written by `001CD2B0` before the
flame reads it. `00275C04` must borrow `em_effects_live_d275C04()`.

The original `001CFAE0` is byte-matched; the projection and SYS readable
decompilations are NEARMISS, so their existing translations and this proof
follow the pinned original instructions. The weather owner now offers a
strict byte-view entry. Its old typed entry delegates to that same body.
It performs the five scalar tail stores, the context-word load and matrix
pointer store, then four quadword copies in original order. Same-value
stores request write permission; failed accesses retain the exact earlier
stores. The bound interface requires aligned source/destination quadwords
and refuses invalid wrap or missing spans. Destination bytes `+58..+5F`
remain untouched. Overlapping source/destination cases are tested.

### Host integration

`em_area01_flame_services_handles()` and `..._call(host,call,fault)` run
inside an active canonical byte transaction. The host supplies the sole
memory provider; refusals never fall back to private storage. The returned
fault address identifies the failed memory access or worker.

For `001CFBE0`, call `em_area01_flame_packet_prepare()` while views are
active, suspend native transactions, call `..._invoke()`, then resume even
if the native packet owner failed. The temporary packet argument structure
copies only the immutable descriptor and the caller's `0x58` initialized
transform bytes. The final eight transform bytes are never read. Actual
packet writes and failure state stay in `em_effects_live` and RCL.

`001F4A10` forwards the original nested stack `entry_sp - 0x90`. Its
`001C7900` vector is written at `entry_sp - 0x40`, with arguments
`(matrix, vector, 0x3F5, 3)`. The host needs the existing channel-3
`001C7900` owner and `001C6120` lookup. `001E9E60` forwards at
`entry_sp - 0xC0`; the RCL worker adapter owns its packet callbacks.
These argument lanes, stack addresses and vector bytes are checked at
every forwarded boundary. No Scene or live-dispatch edits are part of
this adapter's implementation.

Build dependencies added by the composing host are the new service source
and existing `em_area01_render_gs.c` / `em_area01_render_hud.c`;
`em_weather_packets.c`, `em_effects_live.c` and `em_stream_lanes_original.c`
are existing common owners.

### Readonly resources and loader limit

`export_effect_tables.py` adds two disjoint boot windows:

| Window | Reason |
|---|---|
| `00253CA0..002541EF` | Flame descriptors, three extent pairs and nine `0x90` source rows |
| `0026E9B0..0026E9BF` | `001E9E60` blend colour |

Both windows equal the boot ELF in all 16 AREA01 snapshots (22,016 bytes
compared). The complete 36-block export contains 63,996 payload bytes;
its default 17 AREA11 comparisons cover 1,087,932 bytes. Existing mutable
light-list windows are not claimed to remain ELF-equal after AREA01 runs.

The live effects loader previously copied every file block into its sparse
ELF image but silently registered only the first 32 readable windows.
The exporter already had 34 blocks, so its latest mechanism descriptors
were invisible to `em_effects_live_window()`. The metadata capacity is now
64 and an over-capacity manifest is rejected before loading its blocks.
The test loads the actual local asset and checks every one of the 36
windows against the ELF, then checks a separate oversized temporary
manifest. It never replaces the live asset for a negative test.

### Water first-visit audit

The recorded water node is `007B00A0`, callback `001E7D20`, variant 0.
AREA01 uses its default grid initialization branch: the grid is the
loader-owned `00275C20` target, not a new allocation. State 0 initializes
all 32×32 points, UVs, heights and velocities and returns. It does not call
the packet workers or area-13 splash path in that tick.

State 1 relaxes the grid and applies an RNG impulse. AREA01's `001E7CB0`
gate permits rendering. It then needs the native SDK copies and RCL
`001CB5F0`, `001CB950`, `001CB6B0`, `001CB760`, `001D2E00`, `001D2DE0`.
The six packet/slot workers are provided by `em_area01_rcl_workers`.
The point/UV/grid state remains in `em_area01_state`'s borrowed loader BSS;
the camera, RCL context, GS bank and scratch matrices remain their existing
owners. Area-13 `00275C10/14` animation globals and splash calls are not
required by AREA01's reached branch.

### Validation

Commands:

```text
python3 tools/test_area01_flame_services_reference.py
EM_TEST_FULL=1 python3 tools/test_area01_flame_services_reference.py
python3 tools/test_snow_tiles_reference.py
EM_TEST_FULL=1 python3 tools/test_snow_tiles_reference.py
```

Quick adapter proof: 261 original projection/transform/packet calls,
34 complete state-0 flame bodies, eight shared GS/HUD bodies, nine
original-code refusal prefixes, all 36 resource windows and the manifest
capacity refusal. Full: 1,376 calls, 262 state-0 flame bodies and 144
GS/HUD bodies, with the same failure and resource checks. Every complete
body compares whole RAM and scratch; packet emission uses the actual
native effects and packet-chain owners. RNG executes original code on
both sides; sound and final `001B17A0` publication are explicit external
boundaries with independent owner proofs. The GS/HUD forwarder cases
execute their nested workers' original bodies on the native fixture; they
prove the adapter's boundaries, not those workers' native implementation.

The weather regression protects the changed shared `001CFAE0` owner:
quick compares 2,592 tiles and 262,656 packet bytes; full compares 5,184
tiles and 3,151,872 packet bytes. All pass. Receipts are
`build/level2/flame-services-{quick,full}.log`,
`flame-weather-regression-{quick,full}.log`,
`flame-resource-export.log` and `flame-resource-area01.log`.

### Reached idle glow through the existing sprite owner

The unchanged arrival capture's `00159B90` node `007AD490` is in state 1,
phase 0. Its two `00158590` calls reach `001F4CC0(700038A0,700038B0)`;
the first boundary has entry SP `7F0EFF80`. The original caller produces
the point and green channel words before that call. This reach also
occurs in all fifteen AREA01 route/side snapshots; the AREA00 exit
snapshot is excluded. This binding is therefore a live idle prerequisite.

The same flame-services dispatcher now handles `001F4CC0`, `001F4BF0`
and `001CD520`. `F4CC0` delegates to the existing `em_area01_render_gs`
body, forwarding `0021B9A0` with `(2,0,100000)`, `(3,0,1000000)`, then
`F4BF0`, then `(1,0,0)`. Its forwarded stack is entry SP minus `0x30`.
`F4BF0` delegates to the sole `em_status_scene_glow_001F4BF0` body and
forwards RNG and `CD520` at entry SP minus `0x40`. Its call-local colour
input is borrowed from the active canonical view. No persistent colour,
fog, RNG, packet, model or actor owner is added.

`em_aim_fire_runtime_sprite_borrow` accepts only a correctly shaped
`001CD520` call and a call-local external map. It requires the existing
aim runtime to have been attached. It reuses `em_aim_fire_render_live`
and `em_player_equipment_001CD520`, the existing RCL context/packet chain,
and the aim runtime's actual VU/fog-validity fields. Only external point
and scratch lookup uses the borrowed map. The usual ambient-fog validity
is invalidated before the sprite; an emitted sprite publishes its actual
fog load. On failure, canonical writes and the original render owner's
first fault are retained. Later sprite calls refuse before mapping.

Keep actor/player/collision views active for the sprite entry. The root
may continue to publish/reload around its existing native RNG/fog workers.
There is no new suspension requirement, resource export or allocator.
The collision view now permits bounded joined requests over its existing
contiguous `3600..363F` backing, with compile-time member-offset checks.
This includes the sprite's exact writable `3600/32` request, while all
previous single-vector pointers remain identical and crossing an outer
boundary still refuses. The early alias installation/detach lifetime is
unchanged (LEVEL2_AUDIO.md ("AREA01 positional audio and shared scratch")).

`tools/test_area01_glow_services_reference.py` first executes original
`159B90/158590` to the actual reached boundary in the unchanged arrival
image and all fifteen AREA01 fixtures. It then compares complete original
`F4CC0/F4BF0/CD520` bodies against the actual native fog, sprite and packet
owners. RNG is the only original-code boundary on the native side.
Positive controls force both clip outcomes and arbitrary channel words;
all RAM, scratch, nested argument lanes/SPs and fog validity match.
Quick: 28 complete bodies, 168 ordered calls, seven emitted chains. Full:
96 bodies, 576 calls, 41 emitted chains. Both include three denied original
read/store prefixes, retained fog/RNG writes, and repeated-sprite fault
refusal. This proves the composition at the reached boundary, not the
complete native `159B90` owner.

The shared collision regression passes 1,095 original calls in quick mode
and 37,365 in full mode over fifteen snapshots, with 5,145 alias/metadata/
pointer/refusal checks in both. Receipts:
`build/level2/glow-services-{quick,full}.log`,
`collision-sprite-alias-{quick,full}.log` and
`flame-glow-quick.log`. The latter reruns the existing flame adapter proof
with the expanded dispatcher and all forty current resource windows.

## AREA01 gun auxiliary workers

`em_area01_gun_aux_call` binds the existing `em_area01_revisit_008282F0`
probe and `em_area02_misc_0011E520` asin wrapper. The latter uses a new
call-local sparse-memory entry into the same implementation; the original
region-backed API and context layout are unchanged. Its mode word is
borrowed from the existing SDK context, and fabs/sqrt/isnan use their
existing SDK translations. Exception services remain explicit host workers
and fail if unavailable. No replacement SDK mode, errno, or scratch owner
is created.

`008287C0` previously had only an unbound hook. Its new narrow translation
follows the byte-matched decomp source
`Extermination/src/overlays/AREA01/func_overlay_AREA01_00828780.c` (runtime
address is link address plus `40`). It allocates class C, copies the
canonical `0082CB30` vector into its stack local, copies the input matrix
and position to the child, transforms the vector into child `+100`, and
publishes callback `001F5040` last. A refused allocation accesses no child
or vector. Copy and transform operations use existing shared workers. The
native callback registry must connect the existing muzzle owner when this
spawn is reached.

The original stack frames are retained: `8282F0` calls workers at entry SP
minus `80`, with vectors at SP minus `20/10`; `8287C0` calls at SP minus
`40`, with its vector at SP minus `10`. Native math/render/collision worker
boundaries therefore cannot overwrite the caller's stack locals.

`826D40` runs on first arrival, but its probe and muzzle-spawn branches did
not run in the recorded first-visit route: its owners stayed in state `64`
while flag 6 was clear. `8282F0` belongs to the existing revisit census.
The new adapter makes no claim that these branches ran in the guarded
native first visit.

### Verification

`tools/test_area01_gun_aux_reference.py` checks each input overlay's text
against the original extracted AREA01 image and executes the pinned
original boot/overlay instructions. It compares all 32 MiB game RAM,
16 KiB scratchpad, named stack locals, results, and service arguments plus
relevant memory at every worker boundary. Matrix and scalar SDK leaves
execute original instructions; the native SDK path is separately compared
with those instructions. Collision, sprite/beam submission, allocation,
RNG, and SDK exception services are explicit paired boundaries. This is a
composition proof, not a rendered gun-shot or collision-world proof.

| Run | Captures | Probe cases | Spawn cases | Asin cases | Worker boundaries | Fault checks | Seconds |
|---|---:|---:|---:|---:|---:|---:|---:|
| Quick | 1 | 21 | 2 | 22 | 202 | 4 | 2.35 |
| Full (`EM_TEST_FULL=1`) | 16 | 1,536 | 32 | 904 | 14,678 | 4 | 93.27 |

The full sweep covers both ray results, hit kinds and distances, owner
states and beam timer thresholds, successful/refused allocations, SDK
modes -1/0/1/2, valid asin endpoints, and invalid-domain exception paths.
The four refusal cases check unknown entry, missing mode word, and missing
allocation/math workers; each latches without further calls.

The existing region-backed `test_area02_misc_reference.py` regression also
passed: 266 selected main cases, 50 side-effect cases, 30 store-site
variants, 2,512 compared worker calls, 47 callee policies, and 9 API failure
checks, in 1.6 seconds. Logs are ignored files under
`build/level2/oracles/gun_aux_quick.log`, `gun_aux_full.log`, and
`area02_misc_view_regression.log`.

## AREA01 equipment child

The generic model adapter now calls the sole
`em_roger_actor_001C5C90` translation for the AREA01 equipment child.
`00826CF0` dispatches it when byte `+03` is 1. The captured first-visit pair
is body `007B0390` and equipment `007B0680`, with model kind `6B`.

The parent comes from the actual canonical `+18` record pointer. A
call-local typed projection supplies its fields; no Roger pair index,
extra parent registry, or model/bone arena is used. The destination slot
comes from the actual canonical `D_00275B40` word. State 0 calls the
existing library bind, state 1 copies/transforms through the shared raw
slots, and draw publishes the record before the existing generic draw
boundary. Native workers' last writes are refreshed before returning.

Free prepares the same generic `001AF800` release token used by the pool
gateway, commits the actor view, temporarily installs that release hook,
and asks the host to run the actual native pool free. The previous hook is
restored on every return. No actor refresh or typed store follows a
successful free. The host's `model_external` handles `001CAA00` through the
existing draw adapter and `001AFC10` through native pool free, without
opening another view transaction inside that callback.

### Shared slot publication correction

`001B0FD0` / `001B0EA0` export their freshly bound typed Box slots to the
shared raw arena once. Generic workers subsequently read the raw arena and
adopt results into the typed Box view. Publishing the older typed view at
every generic worker entry would overwrite direct overlay stores, such as
`826D40`'s write to a bone's `+78` before `001C6380`. That publication was
removed. A new composed proof binds the actual gun model, writes two
different parent angles through raw slot `+78`, then places it; all RAM and
scratch match original instructions.

The equipment proof also attaches the captured body parent's existing
slots to the Box projection in the test fixture, takes its typed snapshot,
and changes the second raw bone's world-matrix translation before calling
`001C5C90`. It allocates no extra slot. Two different raw values must reach
the equipment child, and the entire RAM/scratch comparison would detect a
stale parent typed-to-raw export. This stresses the shared projection
boundary; the live captured body still uses its generic model owner.

`setup()` now calls the existing `em_anim_rest_001CB5B0` over borrowed
`D_00275B48` / `D_00275B40` pointers. The pool walker must publish the
current actor's bone-array word before each callback, independently of
model binding.

### Verification

`tools/test_area01_equipment_live_reference.py` runs the original boot
owner and its model, math, allocation, placement, and free callees over
captured AREA01 resources. The only draw boundary is `001CAA00`, already
covered by the separate model-draw oracle. Every case compares all 32 MiB
RAM and 16 KiB scratchpad. It covers all lifecycle values 0..4, parent
states 0..2, parent readiness and visibility, all ten matrix-copy kinds,
two non-copy kinds, and an intentionally different canonical destination
array to pin `D_00275B40` semantics.

| Run | Captures | State cases | Raw parent matrix cases | Draw boundaries | Actual native pool frees | Seconds |
|---|---:|---:|---:|---:|---:|---:|
| Quick | 1 | 25 | 2 | 15 | 5 | 3.27 |
| Full (`EM_TEST_FULL=1`) | 16 | 1,168 | 32 | 272 | 384 | 14.46 |

After the slot-publication and setup-owner changes, the full existing
generic model proof passed 735 main cases, 2 retained-library cases,
2 pool release/reuse cases, 4 failure-transaction cases, and the 2 new raw
parent-store cases, with 30 external boundaries and 1 RNG boundary over
15 captures in 18.18 seconds. Indicator quick regression passed 14
scenarios / 42 callbacks / 21 draw and RNG boundaries / 42 parent writes
and 3 fault checks in 3.55 seconds.

Logs are ignored outputs under `build/level2/oracles/equipment_parent_quick.log`,
`equipment_parent_full.log`, `model_equipment_regression_full.log`, and
`indicator_regression_quick.log`. These are standalone composition proofs;
the guarded native route determines live integration status.
