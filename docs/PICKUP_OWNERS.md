# Original AREA11 pickup owners

The canonical adapter in `em_pickup_original.h` replaces the independent
nearest-item scan and two-frame take for bound AREA11 pickups (the legacy
`Found` line is deleted since WP-5; the legacy take posts the battery's
original 001C47A0 request instead, and every other take reports on stderr
the 001B6EA0 request it withholds and the page 0020CDC0 would open).
It consumes the previous-frame winner selected by `em_interaction_scene`,
runs the original exported programs, and queues the real status request.
The game host owns Use acceptance, shared player takeover, status pages,
and camera publication. The adapter does not silently complete a missing
worker. Other unbound scenes retain the explicitly legacy path.

## Original evidence

Tests read the user's pinned `SCUS_971.12` locally, SHA256
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.
No original instructions, disassembly, scripts, meshes, or textures are
checked into this change. `tools/export_pickup_programs.py` reproduces two
660-byte EMSC files under ignored `assets/scene_snow`, from original
`002482C0..0024853F` and `00266620..0026689F`.

The callbacks differ in ways the former port and readable decompilation
missed:

| Behavior | `0015AFA0` / `0015AE20` | `00219550` |
|---|---|---|
| Initial class | 7 | 4 |
| On armed bit4 | phase0→1 | phase0→1, class flags→87 |
| Grab-height thresholds | player Y+6, then+7 | player Y+6, then+6 |
| Short program | `00248480` | `002667E0` |
| Grab program | `002482C0` | `00266620` |
| Script completion | lifecycle1→2 | cue194, persist, lifecycle→3, child lifecycle→3, class flags→4 |
| Following owner tick | persist then free | free |

Both thresholds use two separately rounded original `ADD.S` operations.
Both start functions receive actor+1F0 and do not tick the script on the
arming frame. Neither clears the armed byte before freeing. Class7 still
runs its aura boundary when scratch3B92 is zero, then publication/drawing;
class4 publishes/draws even on its completion frame. The native core begins
after model initialization; it does not claim to recover every initializer.

The original class4 C candidate repeats the first height comparison,
misstates the script-start pointer, and writes child state4. Raw execution
proves the second threshold is+12, the pointer is actor+1F0, and the child
state is3. These decompilation source corrections remain a separate PS2
compile/gate task; this native change does not promote those candidates.

## Programs, inventory, and status ordering

The short program is op07/sub13 enter, op09 consume, terminal op07/sub4.
The grab program additionally turns toward the owner, requests clip40..42,
settles the camera target, and waits for the real animation-end flag before
consuming. Every ordinary command here returns advance1 and no original
record sets force-continue. Consuming therefore yields before terminal
release: the next outer frame can enter status while the actor and its
script still exist. Status pauses both player/script workers. After status
returns, release and the owner cleanup continue in their original order.

Op07/sub13 sets selector1/camera-top2, waits for the actual player-ready
signal, then sets scratch3B92 and script skip-phase1. It does not add the
sub2 letterbox entry or clear the activity array. The shared frame command
now implements both paths separately.

`001B6EA0` dispatches on the owner subtype:

- 0: call `001C40B0(type,1)`, then write request B0=1 and B1=type.
- 1: increment the separate map byte at CB8[type] with byte wrap, then
  write B0=2 and B1=type.
- Other values: increment the separate key byte at CC3[type] with byte
  wrap; only types>=20 write B0=3/B1=type. Lower types preserve an existing
  request.

`em_pickup_equipment_read/write` exposes persistent C60/CA4/CA6 state. Its
new-game values are0/FF/0 from `001AF2C0`. These are independent state bytes,
not values inferred from inventory counts. Maps and keys also remain
separate arrays. The existing item mutation worker still owns special
weapon/magazine/meter behavior; this batch proves family dispatch and the
status ordering, not every `001C40B0` case or every new-game inventory seed.

## Facing and camera

The exported op0E record contains step `3E32B8C3` (0.174532935 radians).
`em_pickup_turn` executes the recovered bearing and bounded angle-step
arithmetic; it writes the actual player yaw. `em_pickup_camera_settle`
implements op00/sub8: first call seeds phase1 without moving the target;
later calls update X/Z through `0018C6A0` and Y through `0018C4B0`.
For each axis, absolute difference<=1 uses difference/4 and reports that
axis settled; larger differences use absolute difference/6 for X/Z or/8
for Y, capped at0.4 or0.3 respectively, with the original sign. The script
advances when all three settled bits are set. The host must publish eye
and target through its `001DD980` boundary on every call, including entry.

Broader facing tests exposed a one-ULP bug in the shared SDK atan helper:
the original76-byte coefficient block at26C5D8 stores four high parts,
then four low parts, then eleven polynomial coefficients. Old decompilation
comments reversed the high/low names. The EMIS bytes were already correct;
the corrected C struct names restore the original cancellation order.
The public binary layout remains unchanged. The SDK zero-vector errno/error
hook is outside this numerical helper; no replacement `atan2f` is used.

## Host contract

Load canonical metadata and render instances, then bind every remaining
pickup with `em_pickup_original_bind`. A persisted absent owner returns-2;
missing assets, workers, duplicate binding, or mismatched owner metadata
fail. Use `em_pickup_original_owner(uid)` as the stable token passed to
shared runtime claim and as the live status/class/armed storage. The game
performs original Use-success pose reset before the later takeover worker.

At the original pooled-actor stage, call `em_pickup_original_tick` once.
It visits the seven pickup owners by original publication rank. Its
publication hook must evaluate the current original culling point and
publish the canonical owner where eligible. The adapter marks its deferred
draw record visible only when that worker reports visibility. The host
retains ordinary update/render order for child indicators and other actors.

After binding, `em_pickup_update` cannot independently arm an item. Bound
owners cannot reach its countdown, synchronous grab, fake battery-key
shortcut, or request post. Real status presentation and cue playback stay
explicit required callbacks. The class7 aura rendering worker is a visible
remaining boundary; this change does not fabricate an aura.

## Validation

- Original owner instructions:6,216 lifecycle, event-order, and height cases.
- Original consume helpers:144 item/map/key/status and byte-wrap cases.
- Original facing helpers/SDK:1,104 cases, including the one-ULP regression.
- Original camera-settle handler and workers:1,188 cases.
- Expanded shared original82D0 oracle:5,184 sub2/sub4/sub13 cases.
- Both pre-existing scanner oracles pass after the SDK correction, including
  the two captured original publication lists and pickup mode6 LOS contract.
- ASan/UBSan actual-program fixture covers short item/map/key paths, grab
  item/map paths, status pause, delayed cleanup, persistence/reentry,
  independent-scan suppression,660 truncated files and12 malformed records.
- Existing pickup-light and panel interaction tests pass.

Run `make test-pickup-owner-reference test-pickup-original
test-interaction-frame-reference test-interaction-scan-reference
test-pickup-lights test-panel-interaction`.

The raw oracles compare original instruction flow under the project's
finite EE arithmetic model (truncated sums/products, rounded scalar
division). They are not a physical-EE rounding proof or a new live pickup
matrix/visual capture. The player lane separately exports and validates
45-frame terminal clips40..42; this batch does not claim the whole scene
is integrated or that every status page is complete.
