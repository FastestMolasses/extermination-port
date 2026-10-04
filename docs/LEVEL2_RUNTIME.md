# AREA01 runtime, views and lifecycle

The AREA01 composition and its canonical views of existing owners: the runtime dispatcher and live probe, actor/player/memory/math views, overlay state, the owners added for the arrival (bone init, talk owner, flicker light), crate registry, collision close-out, camera workers, matrix scratch, the room sampler and the shared predicates.

Contents:

- [AREA01 composition and live binding probe](#area01-composition-and-live-binding-probe)
- [AREA01 canonical actor view](#area01-canonical-actor-view)
- [AREA01 external player record segments](#area01-external-player-record-segments)
- [AREA01 canonical memory callbacks](#area01-canonical-memory-callbacks)
- [AREA01 math workers over canonical memory views](#area01-math-workers-over-canonical-memory-views)
- [AREA01 canonical overlay state and initialization](#area01-canonical-overlay-state-and-initialization)
- [AREA01 bone-slot initialization helpers](#area01-bone-slot-initialization-helpers)
- [AREA01 placement [38]: talk owner 0x825740](#area01-placement-38-talk-owner-0x825740)
- [AREA01 flicker-light owner](#area01-flicker-light-owner)
- [AREA01 crate registry binding](#area01-crate-registry-binding)
- [AREA01 collision close-out binding](#area01-collision-close-out-binding)
- [AREA01 camera worker binding](#area01-camera-worker-binding)
- [AREA01 actor matrix scratch and floor configuration](#area01-actor-matrix-scratch-and-floor-configuration)
- [AREA01 room sampler binding](#area01-room-sampler-binding)
- [AREA01 shared worker binding](#area01-shared-worker-binding)

## AREA01 composition and live binding probe

Status (2026-10-04): verified dispatcher; the live adapters are committed
and run only under the probe (LEVEL2_BINDING.md "State"). This is
not route completion. Normal play still fails at the AREA01 world-frame
guard. No AREA01 world frame has passed the recording comparisons.

### One dispatcher for existing translations

`em_area01_runtime` composes the existing AREA01 overlay, actor/owner math,
SYS, EXITA, EXITB and light-owner translations. Original argument lanes,
signed integer returns and binary32 bits pass through `EmArea01Call`.
Every memory request goes to the host's canonical owner. There is no
full-RAM allocation, capture loading or missing-worker fallback.

The shared implementations of `001AA000` (collision list passes),
`001C69A0` (existing status/pose code) and `001CB360` (the new adapter to
existing owner draw) remain host workers. The dispatcher does not call the
older AREA01 copies in place of those owners. The player math module is
also excluded: the live first-level player already owns those originals.

The cross-module oracle executes four original roots: `001BB860`,
`001BB520`, `00128C10`, and `00128AB0`. It checks the full RAM/scratch
result and worker argument order through math-to-EXITB and SYS-to-EXITA
calls. Identity and copy execute their existing original-code translations;
model allocation and pose initialization are explicitly scripted boundaries
in this test. Their separate adapter proof is in LEVEL2_RENDER.md ("AREA01 generic model and animation workers").

- Quick: **32 cases, 63 worker boundaries, 3 fault cases**.
- Full: **320 cases, 602 worker boundaries, 3 fault cases**.
- Missing entry, incomplete arguments and missing memory latch a fault.
- Receipts: `build/level2/runtime/quick.json` and `full.json`.

SYS and EXITA/EXITB pass exact relative stack offsets to nested calls.
Math's existing API has no entry stack parameter. The tested math roots
have no callee that reads their stack locals, so this test deliberately
does not claim equivalence of their physical EE stack pointers. Native
call locals are separate from persistent game memory.

### Live composition under development

`em_area01_live` connects the actor, player, scene, model, render, loader
and overlay providers. It uses the existing SDK memory adapter for vector
and matrix leaves. Actor and player transactions publish before native
workers and refresh afterward. A model worker brackets actor records
itself; its head/shadow callback runs after both records are published.

The Box model's animation-bank word at +40 is a writable view of the
existing model owner's `anim` field. Model and method words +44/+4C and
slot-address projections remain protected. The generic model owns its
record bytes through the actor view; it allocates no second bone arena.
Player-reserved slots resolve through the player's existing pose owner
before the shared arena.

Original scratch addresses still require review across the legacy typed
providers. In particular, the pose and renderer's 3400/3440 views and the
pose/aim/camera 3600 and 38B0 views must retain the last original writer.
Do not claim live canonical-memory equivalence from the standalone module
oracles alone. Unmapped spans continue to fail.

`EM_LEVEL2_BINDING_PROBE=1` is effective only with
`EM_STARTUP_TEST=newgame-level`. It enables the development callback
composition in the existing arrival smoke. Missing callbacks/workers still
fail at their original address. It is diagnostic instrumentation, not a
playable option; it does not relax the default frame guard or count a
partial frame as an AREA01 smoke pass. The first connected probe reached
`00219550` on node `007A5640`, the first AREA01 pickup, and failed there at
arrival frame 0 (rebuild counter 15007). It completed no AREA01 world frame.
Receipt: `build/level2/connected-probe/run.log`; tested binary SHA-256
`1ca59dbb7d35c001aa8f05d8629d69cacbe76682d01eb51a972bb50cc37f53a9`.
This binary predates the collision, shadow and script composition below.

Further diagnostic probes still completed **zero AREA01 world frames**:

| Probe | Result | Binary SHA-256 |
| --- | --- | --- |
| `connected-probe-02` | First `00219550` failed; actor publication obscured the inner failure. | SHA-256 `91b13a22d332b285ed030534997da9f02cf0b99c9d57f5a2af1c30b3f8f9fd02` |
| `connected-probe-03` | Primary model `001B1020` failure; the inactive actor segment produced a secondary boundary failure. | SHA-256 `25cd193a58e02020d4bd3ed0b1a2e9fe8d7abd6cf9e2b8d0e3d9586e14a4504e` |
| `connected-probe-04` | Model lookup requested `000001CC`: loader table row 0x37 was zero. Actor, player and collision transactions remained healthy. | SHA-256 `bbe4cb57ba7c364213992fc0259041214535ffef1ad4a1e715baa3720dddcddf` |
| `connected-probe-05` | First pickup initialized; `0015AFA0` on node `007A5C20` reached library model 0x4D at `00C799C0`, absent from the existing sparse global export. | SHA-256 `b0cc6691d15c653ffb585b8170d855b6aa0500b3b5c59552b61e4fdbee196900` |
| `connected-probe-06` | Pickup initialization completed; `00826D40` on node `007A70B0` reached the unbound story-flag query `001BA1C0`. All actor, player, collision, model, pickup and door adapters remained healthy. | SHA-256 `d6582c35002bc05b46b0e794a03f5004533dc702a29deb94b02c7cfaedb88705` |
| `connected-probe-07` | The story query and mechanism setup progressed to allocating its companion. Publication refused companion `007B58D0` because callback `001C5680` was not registered. | SHA-256 `bfc14ccc42a27d100491234417f9083caa7f6d72826dfc20f5c4b426d459b9b3` |
| `connected-probe-08` | Mechanism setup and companion publication completed. Its paired `00828850` on `007A73A0` reached missing query `001B11E0`. | SHA-256 `1146285e8badac4a4eef103fdd425767632598bb90d7fc946c2b58222f71e378` |
| `connected-probe-09` | Shared query completed. `001C02E0` on node `007A93F0` reached unmapped progress byte `00810845`; all canonical adapters remained healthy. | SHA-256 `dfcf4cad55636ec499e0b6234a9eeab25bc55df54cddad35ff86009a9643d29f` |
| `connected-probe-10` | `001C02E0` allocated its companion and initialized its model and pose, then its SDK vector subtraction `001028D0` refused a reached boundary. | SHA-256 `2f7b407f561b70096c3b8bf598dda4051c91e917c4c8702e97b0bd68f3da25ac` |
| `connected-probe-11` | Companion initialization completed; first flame owner `001E3D90` at `007A96E0` reached its second matrix copy and refused unmapped scratch `700036E0`. | SHA-256 `dd6f72f311fc24e600aba5154b2bb6319965cf5258f129c63eb621eb21cf10e0` |
| `connected-probe-12` | Flame initialization completed; existing crate owner `001551B0` at uid `1900` refused its ground query because AREA01 static placement kinds were unbound. | SHA-256 `d915142c78d724f133ca6d5f68d5e4d9c618fbf866bb1fdd5f75b36a12456afb` |
| `connected-probe-13` | Crates completed; shaft-door `00823580` at `007ABD10` reached `001C63E0` with its retained bootstrap animation bank unbound. | SHA-256 `3751185b684f449baab69fb118676d8a51b3a1f69b37cd86d53b4589e4bea57a` |
| 2026-10-04 (merge into main) | The rebuild (frame 0) completes; in the first world frame (frame 1) class-2 owner `00128C10` on node `007A8540` reaches `00102798` (SDK 4x4 transpose), which `em_area01_runtime` does not dispatch; every adapter transaction stays healthy. About 2 minutes. | SHA-256 `577a87e935f7067ee40674953f2e63216644972f1513a48122e1625674a07a8e` |
| 2026-10-04 (tail bindings) | After the bindings in LEVEL2_BINDING.md "State" (00102798, scratch 34C0, 001B1B30 / 001B1630, 001F4A10's lanes, AREA01 head owners, the close-out's file read, library textures) frame 1's pool walk, player stage, camera and `001AAD00` complete; `001D1EA0` stops at the unregistered character-bank TEX0 of the unit at `012C2200` and the chain page's unknown packet `002345E0`. Receipt `build/level2/tail-probe/run.log`. | SHA-256 `2750526cf44acdc7f830b50ef4a4353e7efdd39f20cac21478eeb7da90ab19ca` |
| 2026-10-04 (step FRAMES) | After the character-bank textures, the floor-field / ripple VU1 programs and the bindings of 0012D580, 001F9100, 001C9D50, 001B12B0 and D_00242F20 (LEVEL2_BINDING.md "State") the presentation `001D1EA0` completes and, with `EM_A01_ARRIVAL_TICKS=600`, the run reaches 600 idle world frames after the rebuild (counter 15607) with no fault. A private diagnostic build (missing workers logged, not committed) found the four workers in one run. Receipt `build/level2/frames/probe600.log`. | SHA-256 `e1aff83a23be45634a30e1dc00362a7fc660a305779f2aa2eaefdfd6b4262400` |

Probe 04 took **130.945 s**, returned 1 and failed at arrival frame 0,
counter 15007. Its receipt is `build/level2/connected-probe-04/receipt.json`.
The original `001B1020 -> 001B0DC0 -> 001C6120` reads the retained global
library `D_0028A56C`. The area loader never initializes that bootstrap row;
the existing global library owner in `em_area11_roger` already owns it.
The new adapter borrows that word and only its recognized global-library
spans, excluding AREA11 animation and face banks. It does not substitute
a default inside model lookup or copy the relocation table. Separate model
oracle cases set the area-table row to zero while retaining the original
global-library owner, including model 0x72.
Probe 05 took **133.052 s** and still failed at arrival frame 0. Its
preceding owner calls completed, but no complete AREA01 frame is a pass.
Probe 06 took **134.995 s** and still completed zero AREA01 frames. The
query now calls the existing `em_security_gun` translation of `001BA1C0`
against the canonical scene progress byte, after validating its address.
The existing function body is unchanged; it was exposed for reuse.
Probe 07 took **131.046 s** and still completed zero AREA01 frames. The
companion is now registered with the generic actor adapter; its indicator
adapter reuses the existing indicator and model translations.
Probe 08 took **128.282 s** and still completed zero AREA01 frames. All
canonical adapter transactions remained healthy at the missing query.
Probe 09 took **129.636 s** and still completed zero AREA01 frames.
`D_00810845` is now migrated to the existing scene progress region, with
`D_00810766` (the same owner's bypass store) and `D_00810842` (AREA01's
door-lock flags). No port mirror existed. The existing New Game reset
owns their initial values; no arrival seed or capture state is imported.
`test-continue-reset-reference` passes **14 gameplay/progress fields and
73 inventory fields** against executed `001AF2C0`, including dirty values
for all three added bytes. Six wider accesses crossing their reserved
neighbors are refused. Receipt: `build/level2/progress-reset.log`.
Probe 10 took **132.187 s**, still at arrival frame 0. The next change
preserves the exact failed address from SDK memory requests, so a refused
span is not obscured by the enclosing vector-worker address.

The next bounded progress migration exposes exactly six more bytes from the
same `EmProgress` owner. The placement table was reread from the original
AREA01 overlay through `D_0024D7C0[1] -> 002758D0 -> 0082BD50`; the exported
roster carries the same 54 records. Original `001B6990` copies record +2 to
actor +3 and record +4 to actor +0D, so the subtype gates below come from
loader-delivered placement data rather than capture initialization.

| Byte | Concrete first-visit access and provenance |
| --- | --- |
| `0081075A` | Placement 36 (`0082C2F0`, callback `00825350`) reads event 2 before model setup. Placement 38 (`0082C340`, callback `00825740`) reads it after setup and enters teardown when zero. |
| `0081075D` | Placement 40 (`0082C390`, callback `001C50B0`) has actor +3 = 7. Its `001C4FA0` predicate reads event 5, returns true when zero and selects teardown. No active-light branch is forced. |
| `00810760` | Placements 41/42 (`0082C3B8` / `0082C3E0`, callback `008261A0`, +0D = 2/3) dispatch `00826200` / `00826440`; both read event 8 during setup. The latter also calls the existing `001BA1C0(8)` query while idle. |
| `008107D9` | Counter 1 is read by the placed NPC in state 1 and exit door 12 (`0082BF30`, callback `00823580`). The door stores `0x80`; `00825590` stores `0x81` when the first-visit NPC script ends. |
| `008107E0` | With event 8 = 0, bridge `00826200` reads counter 8 in state 1/phase 0, even when that counter remains zero. The companion's later sequence writes 1 / `0xFF` and reads 2 / `0xE0`; those conditional branches are not promoted to recorded first-visit execution. |
| `00810759` | Original byte-matched `00825590` stores `0xFF` when script `00829FA0` ends, immediately before `001C4760(2,1)` and the `7D9 = 0x81` store. This is a recorded first-visit route write, not an arrival read. |

The existing `001AF2C0` zero-fill owns all six initial values. There is no
new area-load reset, persistent copy or capture seed. The original reset's
`00121A28(D_00810700, 0, 0x640)` clears them; none of its subsequent stores
changes them. Original indexed script writers `001BA080` and `001B82D0`
still reach the same event/counter storage. Sixteen AREA01 end captures
confirm the arrival bytes are zero, `759` changes to `0xFF` and `7D9` to
`0x81` at NPC completion, and the other four remain zero. The route's
frame trace places that completion at `a01_05` frame 3929 (see
SECOND_LEVEL_ROUTE); these observations verify reach and do not supply
runtime values.

The ownership inventory explicitly identifies the effects selector's
existing read-only `75D` views. `em_effects_live_room_lights` refreshes a
local view from canonical progress. The separate `001F6BB0` manager still
refuses keys 0 / `0x1301` before reading its unbound latch fields; AREA01
key `0x0100` does not use them. Migrating the byte does not silently enable
those unproved manager branches.

Reserved bytes remain unavailable: `76D` / `770` belong to other
`001C4FA0` subtypes, `7DF` is the revisit counter behind deferred-group
event 6 = `0xFF`, and water-owner `7F5` / `7F6` are read only in AREA19.
First arrival has event 6 = 0, so the `00823CD0` and `00825950` revisit
owners are not allocated. Placement 20's `00159B90` later interaction can
write saved vectors `710` / `720`; those load-game pose spans still lack
a canonical live owner and are not part of this six-byte migration.

Validation after the six-byte change: **20 gameplay/progress fields and
73 inventory fields** match executed `001AF2C0`, with all six new bytes
dirtied before both resets. The same suite retains **90 executed
`001C4760` cases** and the opening completion slice. The native probe
accepts three merged canonical spans and rejects **15 accesses crossing
reserved bytes**, including the gap between `75A` and `75D`. The no-shadow
check passes over **720 source files**, tracking **18 progress/request
addresses, 6 writer sets, 9 view loads and 25 declared view exceptions**.
Commands: `python3 tools/test_continue_reset_reference.py` and
`python3 tools/test_scene_no_shadow.py`. These are reset/ownership proofs;
no complete AREA01 world-frame comparison is claimed.

The subsequent class-2 audit migrates exactly one additional progress byte,
`0081080F` (`D_008107D8[0x37]`). All six unchanged class-2 records in the
recorded AREA01 arrival execute `00128C10` → `00128B80`, with actor `+36`
zero, and therefore read this reaction gate. Their original callbacks were
executed to completion without substituted workers; the ignored trace is
`build/level2/first-frame-audit/class2_calls.json`. Original `001AF2C0`
clears the byte through its `00810700/0x640` memset. The existing native
enemy code discusses the address but owns no persistent copy; the AREA01
SYS translation now reads the canonical scene byte through its existing
view. Original later-area writes, including `00137C80` and AREA21
`00827000`, do not establish a first-visit writer. Nothing seeds this byte
from a capture or migrates its reserved neighbors.

With `0081080F` dirtied to `FF` on both sides, the reset oracle passes
**21 gameplay/progress fields, 73 inventory fields and 90 executed
`001C4760` cases**, retaining the opening completion slice. The native probe
also refuses both new two-byte accesses crossing `0081080E` or `00810810`
(**17 reserved-boundary refusals** total). The no-shadow check passes over
**726 source files**, with **19 progress/request addresses, 6 writer sets,
9 view loads and 25 declared view exceptions**. Commands remain the two
reset/ownership checks above; the reset log is
`build/level2/class2-reset.log`. This is an additional narrow ownership
checkpoint, not evidence of a complete native AREA01 frame.

The next callback audit adds only `00810767`, event 15. Placed owner 45
(`008267C0`, captured node `007B1E00`) calls `001BA1C0(self, 15)` first in
state 1, then queries event 7 (`0081075F`, already canonical) when event 15
is not `FF`. Original `001AF2C0` clears event 15 through the same memset;
there was no native persistent mirror. Later script writes continue through
the existing event interpreter. The migrated span `766..767` is now
contiguous, while requests crossing `765` or `768` still refuse. The
no-shadow inventory recognizes the exact indexed call instead of requiring
an invented direct-address read in the overlay translation.

With event 15 dirtied to `FF`, the reset oracle passes **22 gameplay/progress
fields, 73 inventory fields and 90 `001C4760` cases**. The probe accepts
four adjacent migrated spans and retains **17 reserved-boundary refusals**.
The no-shadow check passes **726 files / 20 progress/request addresses / 6
writer sets / 9 view loads / 25 declaration exceptions**. Reset log:
`build/level2/event15-reset.log`. The expanded read-only original audit runs
the original `001CB590` selection before each captured callback and applies
the pool walk's `+1` clear and walk index. **90 of 91** callbacks complete
without stubs; the existing title callback stops at an unsupported oracle
MMI instruction. Its log is `build/level2/first-frame-audit/all_calls.json`.
Each callback starts from the recorded arrival, so this is a dependency
audit, not a sequential native frame comparison or a 60-frame simulation.

The active composition now also connects the existing fade, render, sound,
camera-publication and shared interaction services. The shared status host
loads global UI resources without an AREA11 owner roster. AREA01 player
banks come from the live loader, and the script host retains its one
interpreter and player-takeover owner. These are integration changes;
route fidelity still requires the live comparisons.

Generic `001AFC10` keeps its original ordering: clear +14, then run
`001AF800`, then unlink/free. Before publication the adapter prepares a
call-local view of private +110 words. The existing pool's bone callback
then executes the sole `em_roger_actor_001AF800` while the actor transaction
is inactive. The original specialized Box/equipment/effect release paths
remain selected for their owners. Model verification includes actual pool
free and free/reuse chains, plus stale-lifetime refusal.

The pool walk now offers a bound selection callback at its original
`001CB590` site, after publishing `pool.current` and before clearing +1 or
calling the behavior. AREA01 uses it to publish `D_00275B44/48` and call
the existing `em_anim_rest_001CB5B0` with the canonical `D_00275B40` word.
The player/camera `001CB590` calls use the same service. This covers native
and generic pool callbacks and prevents a callback from seeing the preceding
actor's bone-array address. The old walk API delegates to the same body
without a selection callback; the first-level binding keeps that behavior.

The pool's original-instruction proof now also compares `D_00275B40`:
quick **72 cases, 96 walks, 1,030 visits, 24 fault cases**; full **246 cases,
328 walks, 5,014 visits, 80 fault cases**, plus **1,136 reserve/exhaustion
allocations**. ASan/UBSan unit checks cover selection order, all walk modes
and callback failure before drawn/behavior changes. Receipts:
`build/level2/pool-select-{unit,quick,full}.log`.

### Shared edits (committed 2026-10-04)

- `em_scene_bindings.c`: test-only composition hook and callback selection;
  normal guard remains. Texture catalog selection at completed area load.
- `Makefile`: compile the new adapters and their existing dependencies.
- `em_area11_interaction_host.c`: the shared `001B17A0` publication uses
  local call context instead of depending on the AREA11-only interaction
  host being loaded. Its original visibility and class-list workers stay.

The live spine (committed 2026-10-04, still behind the probe) is not
claimed verified: route comparison remains open (LEVEL2_BINDING.md "State").

Probe 11 took **135.910 s**, at arrival frame 0. The composition now owns
exactly `700036E0..7000375F`, the two additional flame matrices. No prior
live module supplies those spans. The original `001E3D90` writes each
complete matrix through `00102958` before its first reader; no captured
matrix or synthetic identity is installed. The existing aim storage still
supplies `700036A0`. Out-of-range and inactive requests fail. The probe
retained all 19 in-process AREA11 phase checkpoints, but failed before an
AREA01 world frame; the full capture checker did not run.


Probe 12 took **130.339 s** and still completed zero AREA01 world frames.
The existing ground walker checks the static placement kind before its hull
test. The scene now gives the collision world a read projection of the
immutable roster's `+8` bytes (stride `0x28`), as original `0019F730` reads
through `D_0024D7C0`. AREA11 keeps its existing empty static prefix.
The projection is cleared on collision-world unload; missing or oversized
input is refused. This binding alone does not establish an AREA01 route pass.

Probe 13 took **141.308 s** and still completed zero AREA01 world frames.
The door-bank correction borrows the existing bootstrap resource owner; see
LEVEL2_RENDER.md ("Shared library models for AREA01 pickups"). Its AREA11 and AREA01 bytes are checked against all
32 relevant captures. The original door/pose calculation is unchanged.

## AREA01 canonical actor view

`em_area01_actor_view.c/.h` gives original-address AREA01 translations a
coherent record view while retaining the native pool and model services as
the owners of their existing fields. It does not allocate actors, models,
or bone slots and does not initialize runtime state from captured RAM.
The adapter is standalone until the AREA01 worker binder uses its boundaries.

### Ownership

The adapter maintains one original-layout image per pool slot. Its canonical
storage consists only of bytes `EmActor` and active shared services do not
represent: owner-private words, timers, handles, matrices before they have
a shared owner, and other untouched record bytes. Represented fields are
transient projections of `EmActor`, refreshed on first access in each
execution segment and committed explicitly before a worker sees the actor.

The represented ranges are `00..1F`, `2E..37`, `52..9A`, `9C..9E`,
`B0..CF`, and `1F0..2EF`. These are exactly the fields serialized by
`em_actor_pool_record_image`. The overlapping word `+34` and halfword `+36`
are both handled: its low half is committed to `w34`, and its upper half to
`h36`. The older Roger bridge omits `+34..35`; that omission is not copied.
Float fields move as bytes, preserving signaling/exponent-255 bit patterns.

Pool links `+14`, `+18`, and `+1C` are readable but may not change through
this view. Store-aware views reject such writes immediately; the untyped
overlay view detects mutations at commit. Callback `+10` may change only
through the supplied registered-behavior rebind hook. The hook must install
the requested original address and a non-null native behavior, preserve
the actor's lifetime and represented fields, and leave pool links alone.
The adapter never copies a callback address into an unbound native actor.

The shared projection callback returns non-overlapping original-layout
spans, outside the ranges above, with one explicit exception: effect owners
may project their canonical work fields over subranges of `+1F0..+2EF`.
Those bytes are neither stale-checked against nor committed into EmActor's
scratch; the effect owner is their sole mutable source. Unprojected scratch
still belongs to EmActor. Read-only spans can project model handles,
animation words, draw methods, and original slot addresses from the existing
owner. Writable spans point to existing canonical storage, such as a world
matrix. Their values are serialized into the segment's coherent record
image; commit copies changed writable fields back into their real owner.
Read-only changes fail. Source storage remains valid until the next
projection callback and must not be callback-stack locals.

Existing boxes APIs provide `owner_fields` for `+40/+44/+4C`, `owner_world`
for `+D0`, `owner_slot` for slot addresses/typed bones, and `slot_world` for
the shared original-layout bone arena and stack. They perform no allocation
when queried. The worker binder supplies projections only for fields those
services actually own. Generic animated records which have no Box owner
keep their metadata and slot-address words in this adapter; their slots
still come from the existing shared stack. Creating a second Box, model
allocator, slot arena, or independently mutable model record is prohibited
by this ownership contract.

A request spanning private and projected fields reads one coherent image.
For example, `+CC..D3` can include an EmActor rotation word and the beginning
of its canonical shared world matrix. Commit updates the corresponding
owners separately. Spans crossing actor boundaries, conflicting shared
projections, missing shared bytes, and stale projections fail closed.

### Required lifetime and worker boundaries

1. Immediately after the real `em_actor_pool_reset_001AF8E0`, call
   `em_area01_actor_view_reset` with that pool and the shared-projection and
   callback-binding hooks. This is the only adapter operation that clears
   private bytes. Do not call it on an ordinary actor allocation or free.
2. Begin before executing a translated owner. The sparse math view passes
   its read/write flag to `em_area01_actor_view_bytes`; the overlay's untyped
   mapping passes zero and relies on commit validation as well.
3. Before every native worker, touch each live actor argument through
   `em_area01_actor_view_touch(view, actor_address)`, then commit. Touching is
   required even if the owner kept its actor address in a local without
   rereading the record. It captures the latest shared values before a
   worker frees an owner or changes its model lifetime.
4. Run the existing native worker; begin a new segment afterward. A later
   view refreshes all represented and shared fields that worker changed.
   Commit once more when the translated owner returns. Never retain record
   pointers across a worker boundary. Recursive translated workers sharing
   this transaction need the binder to establish the same boundaries;
   nested `begin` is explicitly refused.
5. A freeing worker ends the actor's lifetime. Do not touch it afterward;
   the next segment may be empty and commit successfully. A new actor in
   the same slot has the pool's new generation. Views and commits from the
   old generation refuse, including a free-and-reallocate between them.

Original `001AFC10` clears the header, selected represented fields, and
scratch; it leaves most owner-private bytes untouched. `001AFA90` likewise
does not clear those bytes. The adapter retains them across free/reuse,
including passive snapshots of formerly shared fields. Only `001AF8E0`
clears the whole original record. Generation changes are therefore a stale
transaction check, not a reason to erase unknown fields.

Commit also detects an unbracketed native modification of represented or
shared fields, preventing a stale image from silently overwriting the
native worker's result. The first fault remains latched until the actual
pool-reset boundary reinitializes the adapter. Writes committed before a
fault are not rolled back. Unknown addresses outside this pool return no
view without a fault, allowing the composite binder to try another owner.

No scene-binding, Makefile, boxes, or allocator edits are part of this
adapter. Integration adds the source to the build, creates one static view,
resets it at the pool-reset site, and brackets worker dispatch as above.

### Evidence

`tools/test_area01_actor_view_reference.py` builds the actual pool, adapter,
math core, and AREA01 actor helper translation. The user's pinned boot ELF
supplies the original instructions. No capture or runtime byte seed is
compiled into the new code.

- Quick runs **60** original pool mutation/lifetime operations; full runs
  **600**, including a sweep of every writable record byte. Original
  `001AF8E0`, `001AFA90/001AFA50`, and `001AFC10/001AFBC0` run in the existing
  pool oracle. After every operation all **256 x 752 record bytes** are
  compared, including unknown bytes surviving reuse and their eventual
  reset. Bone counts are left zero in these pool cases; bone allocation is
  the existing service's responsibility, not a claim of this test.
- Sparse math aliases `20000000` and `30000000` reach the same actor bytes;
  the `+34/+36` overlap is checked through the native pool image.
- Original `001C39F0` runs its straight-movement branch against the existing
  translation through the sparse actor view, with all pool bytes compared.
  Its one `001026A0` call is an explicitly scripted test boundary returning
  the same vector on both sides. The test brackets that worker and does not
  claim to verify the matrix-vector helper here.
- Synthetic shared-service fixtures verify coherent spans crossing private
  and model fields, deferred commit to canonical matrix storage, retained
  metadata after owner generation ends, and **16 fail-stop cases** covering
  pool-link writes, stale generations, unbracketed native changes, unknown
  callback bindings, overlapping/invalid projections, read-only mutations,
  invalid spans, and fault latching. These fixtures do not simulate a live
  model allocator or establish that the AREA01 model binder is complete.

Quick and full pass. Receipts are `build/level2/actor-view/quick.log` and
`full.log`. The AREA01 world guard remains a separate integration gate.

### Effect write-first fields and observation

A projection with `writable=2` is an uninitialized canonical aligned word.
It requires the view's `written` completion hook. Reads, partial first writes,
and wider first writes crossing that word fail; an exact four-byte writable
access grants reads within that segment. At successful commit the canonical
word is written and the hook certifies that store. The access contract is an
actual store: acquiring a writable pointer without executing its store is not
a supported operation. A raw untyped mutation bypassing the declared first
store fails commit, including an attempt to retain a full-record pointer.
`touch()` captures lifetime without claiming every byte is readable. See
LEVEL2_SERVICES.md ("AREA01 effects service boundary") for effect `+24` and its existing `node_written`
owner contract.

`em_area01_actor_view_snapshot(view,address,size,out)` is observation between
transactions. It composes current EmActor/shared fields and retained private
bytes directly into the caller's output, without changing gameplay storage,
transaction state, or fault state. Invalid spans/projections and uninitialized
requested fields return -1. The project callback must itself be read-only
apart from its serialization scratch. Smoke can request the header, position,
16 bytes at `+1F0`, and 20 bytes at `+2DC` independently; it must check every
return rather than interpret failure as zero data.

## AREA01 external player record segments

`EmArea01PlayerView` lends the existing `EmPlayerLiveActor.bytes` to AREA01
owners between player stages. It owns no second readable record. Its two
snapshots are validation/restoration data and are never returned as memory.
This is necessary because the native player keeps its in-stage layout at +B0,
whereas the original 0015BCF0 tail copies feet to +A0 and published bone-1 hip
to +B0. Existing camera and close-out adapters document the same distinction
(`em_camera_live.c`, `em_area11_bindings.c:player_closeout`).

The host binding `player_states_external_view_init(view,pool)` uses the actual
player, g.pos, g.yaw, published pose hip, and canonical vitals. Vitals publish
through the existing `vitals_store`, including its infected health-limit
side effect. Native player reset increments a generation. Native +214/+308
links are validated against the real pool and serialized as original addresses;
unknown/freed host links refuse. No captured bytes enter the runtime.

At begin, only +A0..+BF, +C4, +20E, +214, +220..+22F, +234/235 bit 0 and +308
are projected. The hip uses the existing camera/close-out fallback to placement
while no published pose is available; the validity flag participates in stale
state detection. At commit, canonical generation, placement, hip, heading,
vitals and native link metadata must still match the entry snapshot. Link
owner generations are checked too. Then legitimate position (+A0), heading and
vitals writes publish to their owners. The internal in-stage representation is
restored, while ordinary record writes and +235's upper bits remain in the
single record.

Hip/homogeneous words, callback/self/link words, model metadata, model/bone
storage and bone counts are protected. Their native owner must perform any
legitimate mutation between segments. A write-aware access refuses them early;
an untyped pointer mutation is caught at commit. The adapter does not silently
rebind unknown callbacks, model addresses or collision links. A caller needing
one of those operations must bind its actual worker. Failures latch and do not
publish external-owner changes; the game must stop. This is a transaction
boundary, not an automatic rollback of earlier original stores.

### Integration

After the real state-0 player/pool rebuild, initialize one adapter. Begin just
before an external translated owner. Route original addresses 008102B0..5CF
through `em_area01_player_view_bytes`. Commit before **every** native worker,
including workers that merely read the player, begin again after it, and commit
on owner return. The player stage itself must run outside this projected view.
Pointers expire at commit. Do not initialize over an active borrow, and do not
fall back to raw player bytes when a protected or invalid span refuses. The
adapter observes shared-owner changes, but a native routine writing ordinary
raw record bytes cannot be distinguished from a translated store; respecting
all worker boundaries is mandatory.

For the interaction callback reached *inside* a native player stage,
`em_area01_player_view_borrow_begin` instead starts a read-only borrow of the
actual intermediate record. It calls no external-owner snapshot or publish
function, projects no placement/hip/vitals, and restores no saved bytes.
`bytes` retains its strict span checks and rejects every write; `commit`
validates the whole record and native link metadata stayed unchanged. The
host must choose this begin mode at every resume while its explicit
in-player-stage scope is set, including collision-query resumes. Native work
still ends the borrow first and starts a fresh borrow afterward. This does
not authorize other translated player writes inside the stage; such a write
faults. Scope must end before the regular post-stage projected path resumes.

New source files: `em_area01_player_view.c/.h`, `em_area01_scene_view.c/.h`.
Shared host edits: `em_player.c/.h` (binding and reset generation). Root owns
Makefile and scene integration.

### Evidence and tests

`tools/test_area01_player_view_reference.py` executes the actual instructions of
001A8840, fabs 0011DF78 and sound-byte helper 00187EC0 over all eight main AREA01
route snapshot fixtures (including the route's final exit state). It compares
the complete projected player and contacted-owner records plus the scratch
halfword; each native worker boundary commits and begins the borrowed player
again. Quick: 8 contact plus sound cases. Full: 128 contact plus sound cases,
including accept and reject paths. Both pass; receipts are
`build/level2/player-view/{quick,full}.log`.

47 refusal/lifetime cases cover write-aware and raw protected mutations,
reset/position/hip/link staleness, invalid spans and nesting. Positive checks
cover mixed ordinary/canonical spans, pointer identity with the actual record,
canonical vitals publication, saved-layout restoration, and +235 upper-bit
preservation. Scene checks prove native-field aliasing, request-block aliases
and refusal across separate owners/reserved bytes. The fixture supplies
synthetic external owners to test the adapter; it is not a claim that the
entire live player host or every player worker has now been integrated.
`em_player.c` and both new providers pass native syntax checks.
In-stage borrow checks intentionally make external position/hip owners
disagree with the native record, prove exact raw-record identity without any
owner change, and observe a native position update between borrows. They also
cover writes through both the checked API and a retained read pointer, native
link mutation, and nesting with the projected mode. These are storage-boundary
tests; the interaction worker has its own original-instruction oracle.

## AREA01 canonical memory callbacks

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

### Verification

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


### Overlay direction and later reload lifetime

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
in LEVEL2_AUDIO.md ("AREA01 positional audio and shared scratch"). This is a source-order audit; a same-AREA01 door
reload still needs full smoke verification.

## AREA01 math workers over canonical memory views

The optional `EmA01Math.view` callback lets the existing AREA01 math and
light translations access their native memory owners directly. It does
not allocate an EE RAM image, copy captured state, bind a worker, or enable
AREA01 world frames. Existing reference tests retain the linear RAM and
scratchpad mode by leaving the callback null.

The core removes the two existing RAM mirror prefixes before asking the
host for a span. Scratchpad addresses keep their original prefix. The host
must return the entire span in its canonical owner, or refuse it. Reads
and stores carry distinct access flags so immutable assets can reject
stores. A refused mapping latches the original requested address, including
its mirror prefix. No linear-memory fallback follows a refused callback.
The existing store count and ordered store trace remain active.

This is a storage adapter, not a translation of another original function.
The first intended callers are `001C02E0`, `001D0C80`, `001D0D40` and
`001C50B0` through their existing math core. Their actor, slot, loader,
scene and scratchpad owners must supply the eventual spans. Returning a
mutable pointer is not permission to duplicate any of those owners or to
retain a loader pointer across a module load.

`make test-area01-math-views` passes with ASan, UBSan and warnings as errors.
Its synthetic fixture runs the real `001D0D40`, checks direct alias identity,
both RAM mirrors, scratchpad boundaries, canonical worker writes becoming
visible immediately, read-only resource refusal, wrapping/out-of-range
requests, trace addresses and refusal of all later work after a fault.
The default linear mapping is checked in the same executable.

The complete existing original-instruction math suite was rerun:

- Quick: passed in **5.02 seconds**.
- Full: passed in **287.86 seconds**, including **48,851 direct bone-helper
  cases**, **1,606 cases / 7,656 boundaries** for their caller, and all
  existing full math cases. The two previously recorded unrelated branch
  gaps in section "AREA01 bone-slot initialization helpers" are unchanged.
- The light oracle also passes: quick **161 cases / 216 boundaries**;
  full **2,309 cases / 2,536 boundaries**, plus its six fail-stop contracts.

Receipts: `build/level2/oracles/math_views_summary.json` and the four logs
named there. The original-instruction sweeps exercise the existing linear
oracle storage; the sanitizer fixture proves the new direct-view contract.
Neither result certifies a future live actor adapter.

Shared edits: `em_area01_math_core.c/.h` (optional direct view and bounded
scratchpad span test), `tools/test_area01_math_reference.py` (matching
ctypes layout), and `Makefile` (math-view and light-oracle targets). New
file: `tests/area01_math_views_test.c`.

## AREA01 canonical overlay state and initialization

`em_area01_state.c/.h` binds the delivered AREA01 overlay data and BSS and
translates runtime `00823A50`, together with the AREA01 paths through its
caller `001E7780`. It owns only the six boot globals at `00275C18..00275C2F`.
There is no second overlay arena, captured-state initializer, replacement
effect-grid owner, or new BSS clear.

The existing `em_area01_sys_001E7D20` remains the grid owner's translation.
Existing AREA01 exit/render translations also use these global pointers and
records. No existing port translation of AREA01's `00823A50` or the shared
`001E7780` was found. The AREA11 initializer `em_slg_008237C0` and other
areas' initializers are different functions and remain unchanged.

### Original load and initialization evidence

The user's `extract/OVERLAY/AREA01.BIN` has MWo3 id 2, loaded at `00823500`.
Its header specifies text size `54C0`, data size `4300`, BSS size `14AC80`,
and BSS boundary `0082CD00`. Header plus text plus data is the actual file
size `9800`. Therefore the loaded data spans `00828A00..0082CCFF` and BSS
spans `0082CD00..0097797F` (**1,354,880 bytes**). The existing
`assets/area01/overlay_data.emsc` data is byte-identical to the file's data
section; this provider uses the same bytes already delivered by the loader.

Original `001FFCD0` state 1, after a successful overlay read, calls
`002009E0(load base, file size)`. That function reads header word `+14`,
calls `FlushCache(2)`, and clears that many bytes beginning after the file
through `00121A28`. Existing `em_module_loader.c:w_overlay` implements this
clear in the loader's allocation. Full BSS zeroing is therefore evidenced by
the original loader, not inferred from omitted export bytes. The new module
neither repeats it at scene rebuild nor manufactures a zero-filled fallback.

Original `001D0660` calls `001F0310` and then `001E7780`. For AREA01, the
latter performs these stores in order:

1. Clear `00275C24`, `00275C28`, `00275C2C`, `00275C18`, `00275C1C`,
   and `00275C20`.
2. For keys `0100` and `0101`, call runtime `00823A50` (decomp source
   `func_overlay_AREA01_00823A10.c`). Its ordered stores are `C2C = 1`,
   `C28 = 20`, `C20 = 0082CD00`, `C24 = 0`, `C18 = 0`,
   and `C1C = 00836D80`.
3. The closing record loop clears `0082CD00 + 54` and `+58`. No other
   BSS byte is cleared by this dispatcher.

Other AREA01 sub bytes have no dispatch target: the six globals remain
zero. This scoped wrapper rejects other area bytes before writing; it does
not implement the other areas' dispatch handlers.

### Ownership and integration

The only shared loader edits are the declaration in `em_module_loader.h`
and implementation in `em_module_loader.c` of
`em_module_loader_memory_mutable`. It returns the same bounded, already
allocated span as the existing read-only accessor. It never allocates,
clears, loads, or copies memory. Existing loader behavior is unchanged.

`EmArea01State` stores the six globals and a memory-provider binding. Its
`bytes` API exposes exactly three regions: those globals, overlay data, and
overlay BSS. Views of data/BSS alias the loader. The overlay data includes
script records that original script commands modify; future AREA01 script
hosts must resolve those records through this canonical view rather than
load another independently mutable copy of `scripts.emsc`.

Every query reacquires the loader span and checks the loaded overlay header.
Missing spans, a replaced overlay, empty/wrapping/out-of-region queries,
and latched faults produce no view. Bind/init failures latch their first
original address; subsequent execution refuses until the host explicitly
detaches the binding. Unknown-range queries alone do not latch a fault, so
a composite resolver may consult other canonical owners. A failed store
keeps the original stores that preceded it.

Exact scene integration for the parent binding change:

1. Keep one zero-initialized `EmArea01State` in the scene binding, and a
   memory callback that returns
   `em_module_loader_memory_mutable(ctx, address, size)`.
2. In `w_001AFCA0`, after `em_area11_bindings_effects_attach` (the existing
   `001F0310` binding), replace the AREA01 `001E7780` boundary comment with
   `em_area01_state_bind` over the live module loader followed by
   `em_area01_state_001E7780` using canonical area/sub bytes. Report either
   failure through `em_scene_fault` with the state's fault address. Do not
   clear BSS or reload an EMSC there.
3. This completes before `w_001AFCA0` returns, hence before the frame
   machine calls `001AFCF0`, player spawn `001B07C0`, placement spawn
   `001B6990`, and the later `001C5C50` spawn in that rebuild. The pool reset
   still precedes the effects and overlay initialization.
4. The future SYS/render/overlay binders must use the same global/data/BSS
   views. Reacquire any region arrays before execution after a module load;
   do not retain raw pointers while the loader can replace allocations.
   Detach before closing the loader or discarding this scene binding. A
   same-overlay scene rebuild may rebind without resetting its memory.
5. Keep the AREA01 world-frame guard until every reached owner and worker
   is bound. This initialization does not claim a playable arrival.

No `em_scene_bindings.c` or `Makefile` edits are part of this module. Add
`src/game/em_area01_state.c` to the native build and a reference target
running `python3 tools/test_area01_state_reference.py` during integration.

### Verification

`tools/test_area01_state_reference.py` builds the actual state provider and
actual loader with a test-only allocation bridge. The oracle executes the
original `001E7780` and `00823A50` instructions with no external worker
stubs on those paths. The user's pinned boot ELF and extracted overlay are
checked against the code in each capture before execution. The sources are
the arrival beat `build/s87/route/15_level_exit` and all 15 AREA01-resident
`build/s87/route_a01` end snapshots; `a01_07` is excluded because its
snapshot has already entered AREA00.

- Quick: **16 captures, 160 initialization cases**, direct init and
  dispatcher sub bytes 0, 1, 2, 255, each with recorded and poisoned
  BSS/global initial values. **219,528,960 state bytes** compared.
- Full: the same cases plus all 256 sub-byte values, **416 cases** and
  **570,775,296 state bytes** compared.
- Every case compares the ordered original stores, all 24 global bytes,
  the whole overlay data and BSS, the unchanged loaded file, and boundary
  guard bytes. Poisoning exposes omitted stores and excessive clears.
- A separate original `002009E0` execution runs its real `00121A28` memset;
  only `FlushCache` is a host boundary. The existing native loader clears
  exactly the same **1,354,880 bytes**, preserving the file and guard.
- API checks cover exact loader-pointer alias identity, moving a loader
  allocation, retained script/BSS modifications across rebuilds, malformed
  headers, missing/truncated spans, query bounds, unsupported area, partial
  stores before a missing-BSS fault, and refusal after a latched fault.

Both modes pass. Receipts are `build/level2/area-state/quick.log` and
`full.log`. The existing module-loader quick oracle also passes (8.3 s;
`build/level2/area-state/module-loader-quick.log`), including whole loads
and New Game at both drive settings. The targeted no-disassembly scan,
Python compilation, warning-clean bridge build, and diff check pass.
These are initializer/storage checks; the grid owner's existing
oracle proves its separate update logic. No original bytes are embedded in
the new source or tests.

## AREA01 bone-slot initialization helpers

2026-10-03. The two arrival-only helpers 001D0C80 and 001D0D40 are now
translated in the existing `em_area01_math_owner.c/.h`. They remain unbound
to a live AREA01 world. Their original caller, 001C02E0 state 0, now calls
these implementations directly within the standalone module.

Before adding them, all native `src/` references were checked. The only
references were worker declarations/call-outs in AREA01, AREA04 and level 8;
there was no native definition. The existing bone allocator in
`em_owner_services_original.c` belongs to 001B0EA0/001B0DC0 and tests capacity
against the bone count, while 001D0C80 reserves one additional animation
record. It is not the same original function and was not substituted.

The original sources are the byte-matched
`../Extermination/src/func_001D0C80.c` and `func_001D0D40.c`. The caller is
`src/func_001C02E0.c` in that repository; its NEARMISS label is not evidence,
so the changed native caller is compared with its original instructions.
No decomp source changes were needed.

### Behavior and dependencies

001D0C80 binds the model using 001CA5E0, reads its count through 001C6150,
and stores the count as a byte in the node. If the signed halfword free-slot
count is less than the bone count plus one, it sets lifecycle state 3 and
returns 1. Otherwise it allocates the node's bone slots through 001AF780,
reads the node's count again after every allocation, stores the held count,
calls 001CB5B0, and allocates the extra animation record. It returns 0.

001D0D40 follows the node's animation-record pointer once. It stores the
table address, converts the signed frame count to an EE float, clears the
time word and stores the low-byte loop flag. Conversion uses
`em_ee_cvt_s_w_bits`; every read and write uses the existing `EmA01Math`
view and store tracing. No new global memory or allocator was introduced.

| Original worker | Required binding |
|---|---|
| 001CA5E0 | Existing model binder over the node's canonical record |
| 001C6150 | Existing model count service (`em_owner_services_001C6150`) |
| 001AF780 | The existing canonical slot stack; no AREA01-private stack |
| 001CB5B0 | Existing bone-source setup on the same node and stack |

The live adapter must supply these services and map D_00275BCC to the same
stack's free count. An absent worker or bad address keeps the math core's
latched fail-stop behavior. In 001C02E0, the original continues after
001D0C80 even when its game result is 1; the native caller preserves that
control flow rather than inventing an early return. A port fault still
prevents all later stores/calls through the shared core.

### Verification

`tools/test_area01_math_reference.py` now includes both original ranges and
executes them inline when testing 001C02E0. It compares memory at every
external worker entry, argument registers, worker ordering, final memory
and integer results against the original instructions. The normal
fail-stop contract also exercises both new entries after a latched fault.

The new direct cases use AREA01 captured node records. Designed worker
boundaries exercise signed capacity (including negative and exact-limit
cases), count-byte narrowing, neighboring-byte preservation, an allocator
that changes the count during the loop, and animation-record aliasing with
the node's own pointer field. Conversion cases include zero, negative
counts, both signed integer extremes and values above float's exact-integer
range. Full mode expands to every count byte and additional deterministic
integer patterns. These synthetic cases are not claimed as recorded
gameplay.

Quick run on native arm64 macOS, `EM_TEST_JOBS=4`, passed in **8.1 seconds**:

- 001D0C80: **40 cases, 1,443 worker boundaries, all 4 branch outcomes**.
- 001D0D40: **21 cases**, no calls or conditional branches.
- The changed caller 001C02E0: **57 cases, 363 worker boundaries, all 24
  branch outcomes**, with both new helpers executed inline.
- The complete existing math quick suite also passed, including the
  other actor, owner, script, player and table cases.

The quick receipt is `build/level2/oracles/area01_math_bone.log`. Full mode
is run as `EM_TEST_FULL=1 EM_TEST_JOBS=4 python3
tools/test_area01_math_reference.py`; its receipt is
`build/level2/oracles/area01_math_bone_full.log`.

The complete full suite passed in **458.7 seconds**, using all twelve
original math-lane captures (the direct bone grid uses the eleven AREA01
captures, excluding the AREA00 exit snapshot):

- 001D0C80: **14,135 cases, 760,551 worker boundaries, all 4 branch
  outcomes**.
- 001D0D40: **34,716 cases**; together the new helpers have **48,851**
  direct cases.
- 001C02E0 with both helpers inline: **1,606 cases, 7,656 worker
  boundaries, all 24 branch outcomes**.
- The existing full cases also pass, including all 75,297 actor-table
  cases. Two unrelated pre-existing branch outcomes remain uncovered
  (001BBAE0: 9/10; 001C2770: 119/120); this result does not claim complete
  coverage of every routine in the module.

This proves the standalone functions and changed standalone caller on
these inputs. It does not prove the future AREA01 adapter, a connected
route, live slot ownership or rendering. No emulator was launched.

Shared-file edits in this step: `src/game/em_area01_math_owner.c`,
`src/game/em_area01_math_owner.h`, and
`tools/test_area01_math_reference.py`. The changes add two public entries,
replace the caller's two worker calls with direct same-module calls, and
extend the existing oracle. No Makefile or first-level status file was
changed by this step.

## AREA01 placement [38]: talk owner 0x825740

2026-10-03. Standalone translation added to the existing
`em_area01_overlay.c` / `.h`; this does not bind another live owner or remove
the AREA01 fail-stop gate.

The original placement table at 0x82BD50 uses 0x28-byte records. Record [38]
is at 0x82C340, and its callback at +0x24 is 0x825740, checked directly in
`../Extermination/build/s87/route/15_level_exit/eeMemory.bin`. The matched
decomp source is `func_overlay_AREA01_00825700.c` (link address is runtime
minus 0x40), 464 bytes. The existing overview records the same owner and
placement in `AREA01_OVERVIEW.md` sections 7.1 and 8.

This owner is absent from the kept first-visit end pools. Its initial
execution is not established by that absence: the original setup runs its
model and animation workers before testing D_0081075A. When that byte is
zero, it sets state 3; otherwise it asks 001BA1C0(self, 6), keeping state 1
only when that result is zero. States 2 and 3 both call 001BA540 and
001AFC10. This is why the callback needs a translation even when it is not a
persistent first-visit NPC.

In state 1, step 0 starts script 0x82A7B0 when Use bit 4 is set. Step 1 ticks
the script; a nonzero result clears the step and Use byte and selects clip
1 with blend 30. The animation, visibility and actor draw callback run
after either step. Setup copies the current yaw into the talk block and
D_0028A5C4 into the owner; all original loads, stores and calls retain their
order. The translation uses the existing hooks and fault record.

Verification, native arm64 macOS, from the port root:

```sh
python3 tools/test_area01_overlay_reference.py
EM_TEST_FULL=1 python3 tools/test_area01_overlay_reference.py
```

Both passed on 2026-10-03. The original instructions are read from the
user's captured AREA01 RAM after validating its overlay text against the
extracted overlay and its boot text against the pinned ELF. Placement [38]
uses designed cases over the captured control-room NPC record, explicitly
calling 0x825740; these are not represented as captured executions of [38].

- Quick: 184 cases for this owner; 2,192 cases for the complete overlay
  suite, 3,524 executions including poisoned initial bytes.
- Full: 1,131 cases for this owner, including all 256 values of state, step,
  Use and D_0081075A; 14,034 cases for the complete suite and 21,199
  executions including poisoned initial bytes.
- Both execute all 79 reachable non-branch instruction words of 0x825740
  and all 2,072 such words across the 15 entries. Callee arguments, memory
  at every call entry, ordered memory accesses and final memory agree. The
  full run compares all 32 MiB and the scratchpad at every call entry.
- Setup gates and script completion cover zero, positive and negative
  return bit patterns. Scribbling hooks check the re-read of +0x0D after
  calls and setup's yaw, global word and story byte. The shared fail-stop
  checks cover the new entry alongside the others: latched fault, missing
  hook table or fault pointer, and refused memory/call operations.
- The suite's hook-contract pass completed 641 native runs on 26 cases,
  covering 53 hooks, the actor callback and all 15 entry points.

Receipts are ignored local files:
`build/area01-smoke-scaffold/overlay-quick.log` and
`build/area01-smoke-scaffold/overlay-full.log`. This evidence proves the
standalone owner on the tested cases. It does not certify its future live
worker bindings, the conversation's presentation or a route containing it.

## AREA01 flicker-light owner

2026-10-03: original standalone owner proof. The later canonical worker
adapter and its integration boundary are documented in
LEVEL2_RENDER.md ("AREA01 light worker binding"); the owner remains the single translation.

`em_area01_light_owner.c/.h` adds the missing owners of boot functions
001C4FA0 and 001C50B0. Both original C bodies are byte-matched. The original
instruction body additionally fixes the draw-vector stack address: its
0x40-byte frame places that vector at caller sp minus 0x10.

001C4FA0 dispatches on actor +3 and reads the corresponding canonical story
or inventory byte. 001C50B0 uses that predicate before setup and before each
active frame. Setup calls the existing model worker, stores its byte result,
chooses the original color and point-light amplitude from area/sub/parameter,
optionally calls the point-light worker, and scales the color vector. Active
frames consume exactly one random value, compute the original EE float
jitter, clamp the three draw components to the original [0,255] interval and
call the draw worker with a stack vector. Teardown releases a light handle
unless it is -1, then frees the actor. These clamps are in the original;
they are not native safety substitutions.

The module uses the existing `EmA01Math` address and worker contract. It
owns no global/actor memory. The caller provides the original actor address
and entry stack address, and maps the canonical data. External worker calls
remain mandatory and fail-stop through the math core; the predicate is
called directly because this file is its sole translation owner. Float
operations use `em_ee_float.h` throughout. No script, light, model, random
or draw behavior is substituted here.

All external workers are entered with original stack pointer `sp - 0x40`.
The math dispatcher carries no separate stack argument, so its host context
must supply this value when calling stack-aware existing workers such as
001C5050 and 001F5F60. Their own frame allocation then remains theirs.

### Binding dependencies

| Worker | Existing translation / service |
|---|---|
| 001F5490, 001F5F60 | `em_area00_fx_exit.c` |
| 001C5050 | `em_area00_world_001C5050` in `em_area00_world.c`; forwards the original 001D7FA0 result |
| 001D7FA0 | `em_point_light_register` over `em_rcl_point_lights()`; `em_effects_live.c:w_room_001D7FA0` preserves the returned handle |
| 001D80B0 | `em_rcl_001D80B0` / `em_frh_001D80B0` over the same canonical pool |
| 00122BB8 | shared canonical random generator |
| 001AFC10 | `em_actor_pool_free_001AFC10` |
| 001028B8, 001028D0, 00102900 | existing SDK vector owners through the shared dispatcher |

001C5050 was checked before implementing: it already exists and was not
duplicated. Its decomp C declares void, but the original caller consumes the
forwarded result; the existing AREA00 translation explicitly preserves that
result. The effect-only `w_001D7FA0` discards the handle and is insufficient
for this caller. No decomp source was edited.

The live adapter must supply these original-layout fields from canonical
owners, with store-back before external workers and refresh after them:

| Original bytes | Native source / binding requirement |
|---|---|
| actor +3, +4, +0D | `EmActor.model`, `u04[0]`, `param` |
| actor +20 | persistent point-light handle; `EmActor` has no direct field, so retain it in the actor's canonical controller |
| actor +44 | model entry supplied by 001F5490; retain it in the canonical model/controller owner |
| actor +80..8F, +B0..CF | `EmActor.f80`, `pos`, `rot`; preserve float bits |
| 00810700..701 | canonical scene area/sub bytes |
| 00810C87, 0081075D, 0081076D, 00810770 | canonical story/inventory bytes used by the predicate |
| caller sp-10..sp-1 | temporary vector read by 001F5F60 during the call |

The native pool's record-image helper does not supply +20 or +44; a zeroed
image would lose the light handle and model. The standalone oracle's full
RAM image is a test fixture, not a proposed live second actor arena.

### Verification and limits

Run `python3 tools/test_area01_light_owner_reference.py`, or prefix it with
`EM_TEST_FULL=1`. The native library is compiled with C11, warnings as
errors and contraction disabled. The test executes the original ELF
instructions over the AREA01 arrival capture
`Extermination/build/c10/exit/exit_01_movie_arrival/`: the callback word
remains at 0x7B0F50, but the record has already been freed (status byte 0
and cleared header). This corrects the initial description of a live
state-0 actor. The oracle deliberately constructs each lifecycle state;
the preserved callback identifies the fixture, not route reachability.

The oracle checks the two original code ranges against the pinned ELF.
It runs the real 001C4FA0 inside 001C50B0 and checks every external worker's
call order and consumed arguments, the full actor and relevant globals at
each worker boundary, the original worker-entry stack, the draw vector,
every final original/native write
and the predicate result. Synthetic cases cover setup acceptance/refusal,
all predicate arms, teardown handle boundaries, parameter/mode branches,
callee writes that force the owner to re-read state, signed random results,
and EE float edge values. Full mode expands the predicate type-byte space
and randomized float cases. The test's worker stubs are explicitly test
boundaries; they do not verify the workers themselves.

Quick: **161 cases / 216 worker boundaries**. Full: **2,309 cases / 2,536
worker boundaries**. Both also check six fail-stop contracts: null owner
context, null predicate context, null result, an already-latched fault,
unmapped input and an unbound reached worker. Timing and final results are
recorded in the parent binding report.

This proves a standalone translation on the tested inputs. It does not
prove the future live binder, light allocation/drawing, pixels, arbitrary
aliasing of an actor with global bytes, or every possible worker failure.

## AREA01 crate registry binding

The first-visit placement table at `0082BD50` contains six `001551B0`
crates (indices 6..11, model 6). Placement 6 (`0082BE40`, placement bits
`1900`, link -1) does not read a nest group during initialization.
Placement 7 (`0082BE68`, placement bits `1A01`, link 0; actor `007AAE60`)
does: it is the only one of those six with bit 0 set. The remaining four
have bits `1B00`..`1E00` and link -1. This is unchanged in the fifteen
AREA01 route/side RAM fixtures; the AREA00 exit capture is excluded.

The original `001551B0` instruction path at `001553E4` reads
`D_0024A850[area]` (zero becomes one), then
`D_0024D820[area][first + link]`. AREA01's selector is 2, the pointer table
is `00829848`, and group 2 is `00829360`. Its four `0x2C` records have
persistent IDs `70`..`73`, followed by a -1 halfword terminator at
`00829410`. The existing crate translation still performs all four
`001B11E0` queries. Any untaken entry arms the original rattle counters;
when all four are taken, it clears placement bit 0. The group is resolved
only after successful model initialization and only on the reached
original branch. Allocation-busy, bit-clear and negative-link paths do
not ask the provider for any registry bytes.

`em_area11_boxes_bind_registry(view, context)` installs a borrowed
`(context, original_address, size, write)` callback. Bind it after
`em_area11_boxes_reset()` and before the first crate tick. The scene host
can serve initialized overlay spans from its existing module loader and
the two boot selector spans from `em_effects_live_window`; all requests
have `write = 0`. No actor transaction is necessary, no runtime capture
is loaded, and no resource pointer survives a call. Reset or binding NULL
clears the callback and first missing-address fault.

The existing `EmCrateRegistry` has an optional authoritative resolver;
its legacy pointer-array mode remains the default. The box resolver
validates the record headers through the sentinel and asks for that one
contiguous canonical span. It returns the provider's pointer itself,
without allocating/copying registry or model data. Header validation is
read-only prevalidation at the original group-access boundary; the
translated owner retains the actual taken queries and record operations.
A missing header or missing whole span refuses the owner without a
fallback. This is not a claim that provider validation requests reproduce
every individual original load in exact order.

The existing roster `em_actor_roster_001B11E0` reads the box scene's
canonical progress view. It supplies `h_taken` directly; there is no
second persistent-bit owner. AREA01's taken words lie at
`00810880..0081089F`. These four IDs use bits 16..19 of `0081088C`.

Only `0024A852/2` and `0024D824/4` were added to the read-only boot
resource export. The pointer table and records already belong to the
loader's `00828A00..0082CCFF` initialized AREA01 data region. The resulting
export at this change was 38 windows / 64,002 payload bytes and verified
1,088,034 bytes against seventeen first-level/arrival captures. The
existing effect-table metadata capacity is 64 with explicit overflow
refusal. Subsequent independent resource additions may raise the count.

`tools/test_area01_crate_registry_reference.py` compiles the actual boxes
binding and the existing crate/roster translations. It executes original
`001551B0`, its SDK leaves, and actual original `001B11E0` against every
AREA01 route/side fixture. Model, collision, sound, effect, allocation and
draw workers remain explicit scripted boundaries, not claims about those
native owners. Every modeled crate field, ordered worker call and allowed
write set is compared. Quick mode runs 105 owner ticks / 705 worker calls
across fifteen captures; full mode passes 285 ticks / 2,325 worker calls
with all sixteen taken-bit combinations. The legacy original crate suite
passes 5,324 ticks (536 captured, 1,500 synthetic sequences), 16,953 worker
calls and 3,600 SDK cases after the appended optional resolver.
The separate boundary cases check six missing-span refusals, canonical
pointer identity, changed bytes, provider replacement, zero selector
fallback, detach and actual boxes reset.

The state's later break path also reads this group when link is
nonnegative. Its child allocator/copy hook remains an explicit fail-stop
in the live boxes binding; the arrival package does not silently skip it
or claim crate-breaking completion. The original crate suite continues
to verify that branch through its explicit worker contract.

## AREA01 collision close-out binding

This connects two existing original owners to the shared `001AAD00` list
passes. It does not establish a native AREA01 route pass. The ordinary
first-level guard remains in place.

### Original evidence and missing binding

The original gameplay frame `001AE5E0` runs the player, render preparation,
actor walk, player post-step, effects, camera, then `001AAD00` and `001D1EA0`.
Its scripted sibling `001AE6B0` reaches the same tail after its split actor
walk. These callers are byte-matched C in the sibling decomp tree.

The nine `001AAD00` passes were executed as original instructions over all
16 AREA01 snapshots, staging their captured published lists as live lists
in the same way as the existing first-level list-pass oracle. This is an
end-of-capture dependency audit, not a replay of every gameplay frame.

* `001A8BE0` decrements its `0x70003B86` loop counter and dispatches an
  active class-D type-3 entry to `001A8840` at call site `001A8C94`.
  This is caught in `a01_00_train_room` at node `007B2FA0`, and in the
  tunnel, locked shaft and side captures s1/s3/s4. The entry's extents
  pointer is one of `00248120`, `00248134`, `00248184`, already supplied
  by the immutable floor-field export.
* `001A9F60` admits active class-2 type-0 entries when the script and
  transition gates are clear, and calls `001A9E00(player, entry)` at
  `001A9FD8`. Side capture s6 reaches this with `007A9100`.
* The arrival snapshot itself reaches neither missing pair. No currently
  unbound camera worker was reached by executing original `0018B9C0`
  from each of the same 16 snapshots. This does not claim all camera
  states on the subsequent route are covered.

The byte-matched original files `src/func_001A8840.c` and
`src/func_001A9E00.c` identify the complete memory and worker dependencies.
Their sole translations already exist in `em_area01_sys.c`, with runtime
dispatch in `em_area01_runtime_dispatch.inc`; neither body is duplicated.
`001A8840` uses fabs and `00187EC0`, may write the player mailbox/state,
and clears `3B86` on contact. `001A9E00` uses the shared SDK sqrt/atan2/
sin/cos and `001B1470`, then may push the other record and set its `+0B`.
Its player radius/height at `00275490` belongs to the collision world's
existing immutable eight-byte export.

### Binding and ownership

`em_collision_world_bind_area_passes` accepts an optional canonical byte
provider and a pair dispatcher. List addresses continue to resolve in
the collision world's existing lists; the existing contact export also
keeps its owner. For other addresses the supplied provider is authoritative,
so a missing AREA01 record cannot fall through to an obsolete projection.
The two worker slots dispatch only `001A8840` and `001A9E00`. Unbound slots
retain their previous failure behavior. Incomplete bindings are rejected;
world unload clears the binding.

Before each pair call, the current pass counters are published into the
existing probe state's `span_lo`/`span_hi`. They are read back afterward,
including when the callback fails. This preserves `001A8840`'s early
termination of the class-D loop. `em_collision_world_contact_bytes` exposes
the existing immutable radius/height storage with exact bounds checking.

The root integration keeps actor/player views active throughout the nine
passes. Its collision view is committed before the native close-out,
begun/committed around each raw pair call, and begun again after native
list publication before the outer suspend. This avoids importing an old
collision snapshot over the pass's counter writes or final list swap.
The pair dispatch uses the existing active runtime entry, preserving the
ordinary nested native worker boundaries.

Shared edits by purpose:

* `em_collision_world.c/.h`: optional pair/record binding, pass-counter
  publication, unload cleanup, bounded contact-export access. Existing
  static placement kinds and class-7 list base are retained.
* Root integration in `em_scene_bindings.c`: AREA01 provider/dispatcher
  registration and whole-pass transaction boundaries.
* Root integration in `em_area01_live.c/.h`: active runtime entry and the
  existing collision contact bytes in the read-only resource resolver.
* New test bridge and reference script: no game-state owner, original
  bytes, or assets added to source control.

### Verification

`tools/test_area01_closeout_reference.py` loads the actual collision world
and runs its public `em_collision_world_close_out_001AAD00` entry. The
bridge uses the existing SYS pair translations, native SDK and `00187EC0`
owners, and the actual collision-view begin/commit sequence described above.
The expected side executes original instructions, including every nested
callee of these pairs. It compares all RAM, pair dispatch order, scratch
counter outputs and final native list publication. Original code bytes
are checked against the pinned boot ELF.

Default quick: 40 whole-pass runs, 30 `001A8840` and 10 `001A9E00` calls,
3 contact writes and 7 pushes. Full (`EM_TEST_FULL=1`): 240 whole-pass runs,
135 `001A8840` and 103 `001A9E00` calls, 24 contact writes and 55 pushes,
all passing in 8.382 seconds. Both modes use all 16 snapshots and 32
binding/resource/failure checks. Cases cover a contact terminating a list
with a remaining entry, absent contact, the sound modes, busy/height/range
push gates and the player flag that suppresses the other record's `+0B`.

Side s6 subsequently reaches `001AA140 -> 001AA000` at `001AA23C`, which
remains guarded by this change. Its focused pair case retains its first
actual class-2 owner and clears other lists, explicitly isolating the
caught `001A9F60 -> 001A9E00` path. The other 15 snapshots run their
captured lists. `001AA000` already has an existing SYS owner and is a
separate later binding task, not a reason to substitute a result here.

Ignored receipts are under `build/level2/closeout/` and
`build/level2/frame-tail-audit/`. The concurrent complete first-level
aggregate uses a separately frozen build12 binary, so its outcome does
not claim to exercise these subsequent AREA01-only adapter changes.

## AREA01 camera worker binding

The camera remains the existing `em_camera_live` owner. Its optional
`EmCameraLiveHost.area_worker(ctx,function,a0,a1)` forwards exactly three
original calls to the composite runtime:

| Entry | Arguments | Existing implementation |
|---|---|---|
| `001B0300` | none (`a0=a1=0` in the host callback) | `em_area01_sys_001B0300` |
| `00198D90` | camera `008101E0`, player `008102B0` | `em_area01_room_00198D90` |
| `001D2830` | 3, 1 | canonical render-context owner |

The host must call `em_area01_live_call` from an inactive composite, with
`na=0` for `001B0300` and `na=2` for the other entries. It retains the
AREA01/probe eligibility guard and returns failure on unsupported context.
The camera publishes its current `g.cam` view before the host call and
reloads it afterward. The composite's usual suspend must publish camera
stores on return, including when reporting a fault. Aim release's
`001B0300` boundary also publishes/reloads its temporary specials scratch
view. No camera/player record, render owner, or persistent scratch is copied
into a second owner. A missing callback remains a fault.

`em_area01_runtime` now binds a room-module view and dispatches `00198D90`.
The room module's appended optional `view(ctx,address,size,write)` is
authoritative: a failed view never falls back to the region array. All loads
and stores retain their original widths, order, and worker calls. The module
still supports its previous region-array test/API mode when no view is set.

`em_area01_camera_services_call(host,call)` binds the duct camera's
`0018C4B0` (one argument, two float lanes) and `0018C6A0` (two arguments,
one float lane) directly to `em_camera_follow_original`'s existing XYZ
workers. Call it while the byte views are active. It calls no other native
owner and needs no suspend/resume. It requests exact, aligned 12-byte spans,
checks write permission before calling, and preserves exact and partial
operand aliases by using the actual canonical pointers. It returns 0
handled, 1 unknown entry, or -1 refused/faulted.

Root integration adds `em_area01_room.c` and
`em_area01_camera_services.c` to COMMON and binds the optional camera host
callback. Camera vector services run through the composite host before its
unbound-worker fallback. The module owns no sound or area-transition worker.

### Reachability evidence

The original sources `func_00195130.c`, `func_00193EB0.c`, and
`func_0018BC20.c` are readable NEARMISS decompilations; their executable body
comes from the original assembly. The instruction oracle, rather than their
match labels, proves the port's behavior. The original area/state guards
exclude these existing NULL callbacks from AREA01 (area byte 1):

| Callback | Original call guard |
|---|---|
| `001944B0` | areas 6, 8, or `0x13` |
| `00194DB0` | area 0, sub 2, `00810803 == 3` |
| `00230230` | area `0x0E` |
| `00823FE0` | area `0x0D`, entry at least 8 |
| `001AEDE0` in camera specials | camera state 4, entered by the area-`0x0D` arm |
| `001B0C60` in camera | areas `0x0D/0x13` in the event router; `0x0E/0x12` in the transition trigger |

The AREA00 readiness check previously required `00194DB0` for every area-0
state. It now uses the original sub-2/story-3 guard. AREA01's exit goes to
AREA00 sub 0, where that worker is not called. The worker itself remains
unbound and still refuses where genuinely needed.

Recorded census files in
`../Extermination/build/s87/census/runs/A01/` establish the actual additional
AREA01 calls: `001B0300` first appears in `a01_04_return_north` f1810,
also in a01_05 f3931, s0 f297, s2 f346, s4 f398, s5 f136, and s7 f950.
`00198D90` first appears in `a01_s5_duct` f282. The per-frame route traces
show camera mode 1 in the control room and action `0x0A` in the duct.
Mode-1 actions 9/11 take the existing locomotion dispatch; they do not call
mode-0's `00198CE0/00198F10`. The census's `001AEDE0` hits are not evidence
of a camera callback: doors and scripts also call it. Original camera branch
guards identify which callsite can reach it.

### Verification

- Canonical room view, original instructions and all prior comparisons:
  quick 180 routine cases/60 callee-effect cases; full 1,202/1,166,
  6,714 compared calls, all 19 branch sites both ways. The suite also keeps
  its existing room/player world comparisons. Receipts:
  `build/level2/room-view-{quick,full}.log`.
- Runtime composition: quick/full 32/320 existing chains plus six duct
  camera states. The new six execute every SDK/vector callee from original
  instructions on both sides, compare all RAM and scratch, and check the
  room worker's exact caller stack. Receipts:
  `build/level2/runtime/camera-room-{quick,full}.log`.
- XYZ service binding: quick/full 160/1,200 original calls, exact and partial
  aliases, all 64 fixture bytes and return values; six missing-write,
  misalignment and bounds refusals. Receipts:
  `build/level2/camera-services-{quick,full}.log`.
- ASan/UBSan camera host-boundary test checks exact function/arguments,
  current-view publication, return-view reload, specials scratch reload,
  identity and missing/failing-callback refusal. A callback that writes
  canonical camera fields then fails before publishing leaves those fields
  intact; failure does not reload stale `g.cam` over preceding stores. It uses explicit host
  boundary effects, not a claim that the full composite camera frame ran.
  Receipt: `build/level2/scratch-alias.log`.
- Memory-view contract checks room read/write flags, refused-provider
  authority, no region fallback, and sticky fault ordering. Receipt:
  `build/level2/memory-view/room-contract.log`.
- Camera specials suite retains all existing instruction comparisons and
  adds six original area-0 sub/story cases with `00194DB0` absent: only
  sub2/story3 refuses. Receipts:
  `build/level2/camera-specials-area00-{quick,full}.log`.

These are worker/storage proofs. A successful complete AREA01 smoke route
is still required to establish the integrated frame order and presentation.

## AREA01 actor matrix scratch and floor configuration

The recorded arrival contains six `00128C10` class-2 owners. Their ordinary
state-1 path reaches `001B2140`, then the SDK matrix operations on
`70003000..7000303F`, before `001B17A0`. The live byte provider previously
had no owner for those 64 bytes. The original `001C3BE0` and `001C3D60`
helpers also use this matrix.

`EmArea01Live.scratch_3000` is now the sole persistent native owner of that
exact span. The source audit found standalone address-based consumers but
no preexisting host allocation for it. Existing pose/draw matrices begin
at `70003400`; the collision and camera vector aliases remain separate.
`em_area01_live_matrix_3000` borrows the canonical bytes only while the live
binding is bound, active and unfaulted. The production byte provider checks
overlap before fallback, so a crossing request cannot obtain a partial
matrix or unrelated storage. It accepts reads and writes to the same bytes.
The live binding initializes its storage, and the reached original SDK
identity operation fully writes the matrix before its original readers.
No captured matrix is loaded into the game.

The same audit found eight placed `0015A2C0` floor fields, roster entries
46..53. The original placement walk gives configuration row IDs
`1, 1, 0, 5, 1, 1, 1, 1`; all eight link fields are zero. Their state-0
initialization reads the original 20-byte rows 0, 1 and 5. The existing
immutable effect-table exporter now includes `00248120/0x28` and
`00248184/0x14`, retaining the original addresses and exporting only those
three records. Conditional linked-child paths are not established as
first-visit initialization dependencies by these placements.

The private `assets/effect_tables.emet` now has 40 blocks and 64,062 payload
bytes (64,848 file bytes). All previous 38 blocks / 64,002 payload bytes are
unchanged. The 60 added bytes match the pinned original ELF and all 16
AREA01 captures, for 960 compared bytes. The exporter's unchanged default
first-level verification also passed 1,089,054 bytes across 17 captures.
This does not claim every preexisting effect-table block equals AREA01
runtime RAM: older mutable effect data at `0025D3E4`, for example, differs
there. The prior asset and the preservation receipt are ignored files under
`build/level2/matrix-scratch/`.

### Original instruction proof

`tools/test_area01_matrix_scratch_reference.py` compiles the production
live matrix accessor with warnings treated as errors, the existing
AREA01 math/SYS translations and the existing SDK-memory worker owner.
The test linker retains only the required live entry points. The oracle
executes the pinned original instructions using actual captured class-2
records. It compares the complete 32 MiB RAM and 16 KiB scratchpad after
each complete `001C3BE0`/`001C3D60` call and each `00128C10` prefix that
reaches `001B17A0`. The latter stops immediately before that worker;
prefix cases do not claim to verify the remainder of `00128C10`.

The native canonical matrix starts poisoned with `A5`, while a deliberately
different test-only scratch window stays poisoned with `C7`. The proof
checks that all matrix writes reach the canonical allocation and leave the
other window untouched. Thirteen contracts cover two exact aliases, six
extent/size refusals, three lifecycle refusals and two bidirectional writes.
The test also checks that the production byte resolver routes overlapping
requests to that accessor before fallback, and derives the configuration
rows from the original roster rather than saved native results.

| Run | AREA01 captures | Original cases | Class-2 prefixes | Alias contracts | Config bytes compared | Seconds |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| quick | 1 | 3 | 1 | 13 | 60 | 1.87 |
| full | 16 | 252 | 60 | 13 | 960 | 3.77 |

Run `python3 tools/test_area01_matrix_scratch_reference.py`; set
`EM_TEST_FULL=1` for the full captured-record matrix. Suggested Make target:
`test-area01-matrix-scratch`. Reports and logs are ignored under
`build/level2/matrix-scratch/` and
`build/level2/matrix-scratch-{quick,full}.log`.

### Retained door-bank integration

The same byte-provider edit routes the exact readonly `D_0028A574` word
through `em_area11_roger_door_bank_word()`. Resource and resource-rest
lookups try the delivered loader owner, retained global-library owner,
then `em_area11_roger_door_bank_rest()`, with each containing owner's
extent enforced. This uses the existing Roger retained-resource owner;
it creates no second bank or table cache. The door-bank export and its
capture comparisons are a separate root-owned proof. This matrix oracle
compiles the combined provider but does not execute the full door route.

These checks establish exact matrix-worker behavior and immutable source
coverage. They do not establish a complete native arrival or rendered-frame
match and do not change census status.

## AREA01 room sampler binding

The runtime now dispatches `001D0D60` to the existing
`em_area01_room_001D0D60` owner. It requires one integer and one floating
argument, passes the float's bits unchanged, and sign-extends the returned
integer. The existing runtime boundary supplies the room's stack, memory
view, nested calls and sticky fault propagation. There is no new sampler
implementation or storage owner.

### Original route and storage

`001C02E0` state 0 calls the existing bone initializer and `001D0D40` to
attach an extra shared slot at actor `+90`. The sampler record starts with
table `0024FD50`, length 91, time zero and loop flag one. State 1 calls
`001BF630` for the player-distance gate. When it succeeds, actor `+28` is
one and `001D0D60(slot, 1.0)` advances the sampler; its result is stored
into the actor tail. The caller also reaches the already-bound sound,
placement, interaction publication and draw workers.

The slot remains in the existing shared 0xD0-byte slot arena. D0D60 reads
its table, length, time and loop flag, then writes time at `+8` and seven
interpolated floats at `+40..58`. Its only nested worker is `001281C0`:
the existing stream-lanes float-to-int owner. The original 0x60-byte frame
is preserved at that boundary. The routine wraps or clamps exactly as the
existing owner does; this binding adds no iteration limit or altered
length handling.

The original `func_001D0D60.c` is marked byte-matched (October 2, 2026).
The older `AREA01_ROOM.md` assembly-status note predates that match. The
runtime proof executes the original ELF instructions, including its
callees, without oracle hooks.

Two exact immutable boot windows were added to the existing effect-table
exporter:

| Address | Bytes | Original reader |
| --- | ---: | --- |
| `0024FD50` | `0x9F4` | 91 rows of seven floats, `001D0D60` |
| `00275638` | `0x14` | C02E0 interaction descriptor and `001BF630` limit at `00275648` |

All 16 AREA01 captures have this actor's selector zero and class two, so
the interaction reader uses three descriptor floats. Both complete
windows equal the user's pinned boot ELF in every capture: 41,088 bytes
compared. The original C02E0 sampler is at time zero in 15 captures and
time 51 in the duct side-beat capture. These are test fixtures, never live
initialization inputs.

The live byte provider already exposes immutable effect-table windows.
The export is the ignored `assets/effect_tables.emet`. The existing exporter also compared all 34 blocks (62,620 bytes) with 17
first-level captures, totaling 1,064,540 equal bytes.

### Verification and scope

`tools/test_area01_runtime_reference.py` now exercises runtime to ROOM to
the actual native stream conversion worker. Every sampler case compares
all RAM, scratchpad and the return value against the unhooked original,
and checks each worker's function, argument counts and stack offset.

- Quick: PASS in 3.189 s; 34 sampler cases, 106 stream-worker calls,
  including all 16 captured records. Existing 32 composition and six
  camera-room cases also pass, with five sticky-fault contracts.
- Full: PASS in 5.517 s; 151 sampler cases and 487 stream-worker calls,
  including loop flags 0/1/0x80, boundary and repeated wraps, clamping,
  fractional times and steps. Existing 320 composition and six
  camera-room cases also pass, with the same five fault contracts.

Shared edits are the runtime dispatch, its existing bridge/reference
test, and two exporter spans. The bridge calls the existing native
stream-lanes owner; it does not reproduce the conversion. Ignored proof
receipts and generated data are under `build/level2/runtime/`. This proof
does not establish completion of a native AREA01 route.

## AREA01 shared worker binding

`em_area01_shared_services_call(host,sdk,scene,call)` adds no persistent
state. In an active canonical actor/player/collision segment it dispatches:

- `001B11E0`: existing actor-roster bit test over the actual scene progress
  bytes. `828850` reaches it in state 0 after `001B0FD0` succeeds, using the
  unsigned `+9A` byte. The original low-byte-zero early return is retained.
- `00182BF0`: existing script-host predicate over the canonical player and
  three scene flags. The existing worker now has an optional store callback;
  every reached store to `008106BC` and player `+22C/+224/+0` uses canonical
  write permission in original order, including stores of unchanged values.
  Reads borrow the canonical record; there is no player copy or duplicate
  branch implementation. AREA01's controller reaches the predicate only
  after its original room, story, position, busy and scratch gates.
- `001C4760`: existing director key-count worker over canonical scene bytes.
  The caller's index and increment are unchanged; the original extra request
  stores for index >=32 stay in that existing owner.
- `001B1EA0`: existing director winding worker and existing SDK atan2 context.
  Original polygon resources and player points are read through host views.
  The same owner now handles XY and YZ as well as XZ. The YZ cross-product
  reverses the operands, exactly as the original instruction sequence.
  `8267C0` state1/sub0 reaches mode2 only after story tests 0xF/7 and the
  preceding mode0 test succeeds. Counts<3 and unsupported modes return the
  original zero without requiring point/polygon data. Non-finite arithmetic
  remains outside the pre-existing translated domain and refuses.

`em_area01_shared_services_publish(pool,call)` is the separate `001B1B70`
native boundary. The composite first touches the actor, suspends its active
views, invokes publication, and resumes all views. It resolves only a live,
self-linked pool record and invokes the existing collision world's class-list
owner. It allocates no list or actor and performs no independent publication
logic. `826200` reaches it after the original two region tests and substate
writes. Ordinary class-list saturation remains the original silent full-list
behavior.

### Evidence

`tools/test_area01_shared_services_reference.py` executes the pinned original
boot instructions against sparse canonical scene/player/polygon/list fixtures.
Quick/full pass 956/6,280 cases, including 320/2,304 bit queries, original
predicate results and full record/flag mutations, counter requests, all three
polygon planes with actual SDK callees, and class-list entries/counters at
saturation. Its world-publication getter is an explicit fixture boundary
which calls the actual existing class-list owner; no result is stubbed.
Four additional predicate-store cases include a refused unchanged store and
verify that only preceding original stores survive. Runtime never reads these
fixture buffers or captured RAM.

The existing script-host quick/full oracle also passes 1,156/20,256
predicates, 1,610/42,410 math sets, all 18 captures and 2,419 Roger-route frames. Existing director quick/full
passes 600/2,318 polygon cases, 882/22,526 owner ticks, 726/18,366 atan cases,
and all 12,424 first-level route frames. No first-level guard was weakened.

Receipts: `build/level2/shared-services-{quick,full}.log`,
`script-host-predicate-{quick,full}.log`, and `director-planes-{quick,full}.log`.
These proofs do not establish a complete AREA01 frame or route pass.

The progress audit for boot owner `001C02E0` found only its direct
`00810845` bit-5 read and bypass-only `00810766=FF` store. Its child
`001BFFD0`, distance predicate `001BF630`, and clip helper `001BFF90`
introduce no additional progress bytes. The immutable bypass table at
`00829110` has condition-1 ids `79/7A/7B`, which query already-canonical
AREA01 taken word `0081088C`, bits 25/26/27. `00810842` is the independent
AREA01 door row in `D_00810841[area]`. All three bytes start at zero from
the original `001AF2C0` memset of `00810700..00810D3F`; normal area rebuilds
do not repeat that new-game reset. Root migrated their whitelist entries
and original-reset comparison.

That audit also found the scene spawner's canonical preflight checked
conditions 4/5's secondary flags at an address `58` bytes too high. The
original `001B6660` and existing roster owner use `008107D8+index`;
condition 4 sign-extends its index, while condition 5 uses the unsigned
high byte. Only those two expressions changed. The actor census proof
now compiles the actual scene preflight into its test shim and checks
all 2,048 condition/high-byte combinations. Existing quick/full original
spawner proof passes 100/400 synthetic tables and all nine captures:
`build/level2/progress-roster-{quick,full}.log`.
