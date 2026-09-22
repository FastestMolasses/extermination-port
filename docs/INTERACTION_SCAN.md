# Original AREA11 interaction selection

`em_interaction_scan.c` translates the original single-winner use scan,
elevator predicate, pickup predicate and camera publication gate. The local
EMIS exporter/loader keeps all eleven initial interactive owners identifiable,
including the distant door and Roger. Live game bindings remain a separate
integration step; these helpers do not make the legacy pickup collection or
generic examine controllers faithful.

## Evidence and verification

The tests execute instructions directly from the user's original
SCUS-97112 ELF, pinned to SHA256
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.
They do not use the readable decomp C as their oracle. AREA11 controller
tests use its original overlay at runtime base `00823500`, retaining the
overlay header in address calculations.

- `make test-interaction-scan-reference`: 1,671 scan/state/call-order cases;
  1,960 elevator eligibility/score cases; 200 panel score timing cases;
  41 canonical reverse-publication cases; 20 Use-edge/takeover cases;
  2,580 pickup eligibility/score/LOS cases; 548 camera publication cases;
  14 original door/Roger publication and script selection cases.
- `make test-interaction-scan`: ASan/UBSan host bounds, shared-score,
  canonical armed-byte, list cap, LOS owner exception and missing-service
  contracts.
- `make test-interaction-scene`: ASan/UBSan actual local eleven-owner
  metadata, live owner refresh after publication, missing visible bindings,
  every truncated input length, malformed metadata, duplicate identity,
  trailing bytes and re-entry reset.
- `make test-panel-interaction`: existing panel/controller/menu regression.

The opening and playable RAM captures both reproduce their published
interactive lists exactly when the original camera gate is applied to all
eleven owners and accepted owners are reversed. Reports and original input
hashes are local in `build/interaction_scan_reference/`.

This is a native semantic translation, not a newly compiled PS2 byte match.
Finite EE sums/products use the project's truncating arithmetic model;
EE division and the original SDK square root round to nearest. VU sqrt and
division truncate. The tests execute original SDK math bodies, but do not
constitute a new physical-EE rounding proof or model exceptional NaN,
overflow and denormal behavior. The public SDK atan2 helper reproduces the
finite numerical path; the zero-vector wrapper's error callback/errno are
outside its interface.

## Exact scan and insertion order

`00184BA0` does not inspect the controller button. Its callers check the
configured Use press edge, `00810E74 & 70003B76`; they do not use a hardcoded
CROSS constant. There are five direct calls, in `00160220`, `001612D0`, and
three branches of `0016DE40`.

The scan gates on frame selector `70003B8D`, signed fade wait `0028A9A0`,
and use inhibit `008106EF`. A gated call leaves the score untouched.
Otherwise it resets `70003B98` to zero, even for an empty list. It reads
the previously published list at `00275B5C`, count `00275B64`.

Each canonical owner must have status bit0 set, class flag bit7 set and
armed byte `+B` equal to zero. Candidate result2 wins immediately, even if
an earlier object was closer. Other nonzero results compete using a
strictly smaller shared float score, initialized to10000. Equal scores keep
the first entry. Ordinary scores are planar distances, not squared
distances. The winner's armed byte is overwritten with4 and selector becomes3.

The scratch score is a sequential side effect. Predicates may write it
before ultimately rejecting; selector5 can accept without writing it.
The panel's native helper now writes its radius result before height and
facing rejection, matching `00183EF0`. Do not replace this with independent
category minima or a list of precomputed successful distances.

For initial idle/walk control, the exact sequence is:

1. The player update reaches `00160220` through its ordinary idle/run
   action handler, after the earlier `001607D0` action priority gate.
   On a Use edge, the interaction scan precedes ledge/environment fallback.
2. A successful scan invokes `001798D0`: clears player words `+38/+21C`,
   byte `+25C`, requests transition0 through `00174A50`, clears `+5/+6/+1F0`.
   The caller then sets action `+5=25`, phase `+6=0` and returns success.
   The idle handler still runs its common tail; the running handler returns
   before its ordinary animation/translation work.
3. Ordinary frame `001AE5E0` continues to `001AFD70(0)`, updating pooled
   owners. The winner's real owner controller can consume its armed byte
   during this same frame.
4. The camera update follows pooled owners. Frame close-out `001AAD00`
   publishes the collected list for the next player update.

`001B1DE0` collects the canonical pointer at walked actor `+14`, not the
temporary walked proxy. It accepts at most32 publications, pushes backwards,
and close-out publishes that reversed order. `EmInteractionList` preserves
these semantics with bounded native storage.

## Owner data and publication

`tools/export_interaction_scan.py` reads the original deferred registry,
AREA11 placement records, descriptors and SDK coefficient table, then
validates associations against local original RAM. Native identity is the
original **source record address**. A captured actor allocation address is
only evidence; it is not a permanent native identity.

The initial forward walker order is seven pickups (PUID1,4,5,6,7,8,9),
the distant door, Roger, panel, elevator, with other noninteractive owners
between them. Preserve the exported publication rank. Use current owner
positions and controller state when publishing, rather than freezing either
snapshot's subset.

`001B1630` subtracts camera anchor `008105D0`, rejects distance above350,
normalizes with the original VU path, and compares to `00810600`:
distance below35 passes; below45 requires dot>=0; otherwise dot>=0.7.
This runs before that frame's camera update. Door `001BC300` passes its
origin plus10Y. Roger's initial `00823910` can force render byte `+1=1`
**after** this publication gate; the forced render flag is not a valid
replacement for the gate. This distinction explains the playable capture's
visible Roger being absent from the use list.

`em_interaction_scene_offer` must run only where the actual controller
calls its publication helper. It reports a visible owner lacking a live
binding as an error. `em_interaction_scene_scan` refreshes status and class
flags from live canonical pointers after publication; state changes must
not be hidden by copied metadata. Keep those pointers valid until scene
teardown or the owner has left the published list.

## Candidate behavior

The elevator is class4/selector1. It uses a six-float point/radius/height/yaw
descriptor distinct from its model origin. Its initializer changes the
descriptor's Y with the190/230 floor selection. Planar radius and height are
inclusive; facing is `abs(wrap(pi + playerYaw - descriptorYaw)) <= pi/4`.
Power availability does not gate selection: it chooses the owner's later
powered or refusal script. Player action2D rejects this class.

Initial pickups use selector3. Six are callback `00219550`, class4; PUID9
uses `0015AFA0`, class7/subtype1. Both descriptor families are exactly10
radius and3.5 height, verified from original data. Height allows the player
3.5 above or20.5 below the item. Ordinary subtype0/default facing allows a
seven-unit automatic pass, otherwise a bearing within pi/2. Subtype1
always tests opposite owner yaw within pi/2, including inside seven units.
Subtype2 uses the wall-mount branch only when the class low nibble is7;
PUID7's subtype2/class4 therefore uses the ordinary bearing branch.

The original SDK atan2/atanf coefficient table is supplied by EMIS; the
native predicate does not substitute platform atan2. Selector4 and the
class7 subtype2 pitch/roll condition are also translated and tested.

Both initial pickup families issue query mode6 from player position+16Y
to the owner origin. A hit with material flags `2800` rejects unless the
hit kind is2 and the hit owner is this canonical candidate. The ray worker
is injected, so this test does not claim the host collision implementation
or dynamic owner identity mapping is complete. Missing required workers
return -1; the host adapter must raise its concrete failure and never pass
that -1 into the general scan as a successful predicate.

Action2D accepts only class7 and uses a separate mode6 ray from player+1Y,
material flag2000, normalized camera-target/item directions and original
distance/dot thresholds. It returns2 and leaves the planar score unchanged.

## Door and Roger follow-up boundaries

Both can become competitors when approached. Neither is silently omitted
from the metadata. Their complete game controllers still need bindings.

The first door is class5/subtype3/selector0. Its candidate point is shifted
by the authored door angle, and its two-sided facing gate is distinct from
the panel/elevator gates. A winner must enter `001BC350`/`001BBE40` and the
real staging, player/door clips, scripts and transit sequence. This initial
subtype3 uses the ordinary `0024DE40` branch; subtype15's lock selection is
not a reason to invent a lock for this owner.

Roger `008237E0` dispatches by story byte `008107D8`: zero to `00823910`,
nonzero with bit7 clear to armed-talk `00823B70`, bit7 set to departure
`00823C40`. The initial path can auto-start script `008283D0` through the
original polygon test against `0082AB80`, with a separate story branch
using `00828990`. The armed-talk path starts `00828810`, waits for real
script completion, then clears armed/phase. Departure starts `00828A10`
and eventually frees the actor. `00810791==1` suppresses initial publication.
Raw controller tests verify near/far publication and these script choices
with animation/script workers as explicit boundaries. Roger's use predicate
is class10/selector0, radius10, height20 and bearing tolerance pi/4.

## EMIS format and host API

Version1 is local little-endian data:20-byte header `EMIS`, version1,
area/sub key `0B00`, owner count11, record stride80. It is followed by19
float SDK coefficients (`atan_low[4]`, `atan_high[4]`, `atan_coefficients[11]`).
Each80-byte record has a32-byte metadata prefix followed by12 floats:

| Offset | Field |
| --- | --- |
| 0,4,8,12,16 | u32 source ID, role, publication rank, callback, item type |
| 20 | u16 area/PUID |
| 22..25 | u8 initial status, class flags, subtype, selector |
| 26,28 | reserved u16/u32, zero |
| 32..55 | six-float descriptor, unused fields zero |
| 56..67 | source position XYZ |
| 68..79 | source rotation XYZ |

Roles are pickup0, panel1, elevator2, door3, Roger4. The loader validates the
complete role set, unique identities/UIDs, sorted publication ranks, exact
file size, finite geometry and role/callback consistency. Re-entry clears
all previous owner bindings and lists. Lookup is by stable source ID,
pickup UID, or unique non-pickup role. The host binds real controller bytes,
offers owners in their original update order, commits once at frame close,
then performs one scan at the original player Use point.
