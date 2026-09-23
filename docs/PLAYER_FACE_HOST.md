# Dennis cinematic face host

`em_player_face_host` prepares a reusable, independent render model for
Dennis's cinematic face. It preserves the ordinary player model, including
its original head, materials, textures and baked animation arrays. This is
required because `em_face_model_attach` removes the body's wholly bone-7
triangles when attaching the separately lit morph face.

The module currently supports the initial Dennis model3B face selection.
It does not implement the other model selections in `001B81D0`. Inputs are
the actual ordinary player EMDL and `opening/player_face.emdl/.emfm`, exported
from the original assets. Model and morph data remain ignored local files.

## Lifetime and API

Start with a zeroed `EmPlayerFaceHost`. `em_player_face_host_load` accepts
the graphics context, a const ordinary model, scene directory, and the
caller's shared original RNG service. It validates the source geometry and
face resource, copies the render arrays, attaches the face to that copy,
and creates an independent GPU mesh. The copy owns its parents, vertices,
indices, materials and texels. It has no baked clips/palettes: the caller
must use the current original player pose, not a separate animation clock.
Resource preparation consumes no RNG and leaves the face detached.

`em_player_face_host_attach` corresponds to `001B81D0`. Fresh attachment
starts from cleared state and sets speed 1. Repeating attachment while the
face is present uses `001D0690`'s partial reset: current weights and waits
survive; talking, state machines, mouth fields and targets reset; speed
becomes 1. The current morph positions are uploaded before the next draw.

`em_player_face_host_detach` models the cleared state after `001CA770` and
`001AF890`. It disables the alternate draw and clears face state. It retains
the prepared CPU/GPU resources so later attachment requires no destructive
edit of the ordinary mesh. This resource caching is a host adaptation;
fresh face state still follows the original allocator's zeroing behavior.
`em_player_face_host_free` deletes the GPU mesh before freeing CPU storage.

`em_player_face_host_record` returns 1 for the active mesh/model, 0 when
detached, and -1 if unavailable or failed. Draw it using the caller's current
22-slot world palette. A required GPU position update failure is sticky;
the record accessor does not silently select the ordinary head afterward.
The caller must treat that failure as an unavailable required scene worker.

## Original events and ordering

Dennis does not use Roger's activity consumption path. `001FD950` directly
calls `001D06E0(player, 1)` when speaker 0 starts while `70003B8F == 2`; it
calls `001D06E0(player, 0)` when completion mask bit 0 clears. The activity
mailbox is separately written by the presenter. Call
`em_player_face_host_talk` at those direct events. The face host accepts no
activity pointer and cannot consume Roger's or Dennis's mailbox byte.

`00183090` calls `001D0C70` before inspecting the foreign-bank animation
mode or initializing an ordinary body transition, when the ready byte is
2. Call `em_player_face_host_tick_before_body` once at that location.
This API advances once per invocation and does not invent a frame number,
deduplication rule, private RNG stream or status/menu ticking policy.
The shared host remains responsible for original actor/message scheduling.

The appended vertices retain `EM_GFX_VERT_FACE_LIGHT`. The drawing host must
publish the separate face rig as it does for the opening human actors:
camera fill enabled, dynamic light folding disabled (`char_rig_build` with
NULL owner, camera-fill 1, dynamic-fold 0, followed by
`em_gfx_char_face_rig`). The module preserves this tag but does not own scene
lights or invoke lighting callbacks itself.

## Validation and remaining limits

Run `python3 tools/test_player_face_host.py`. The focused ASan/UBSan test
loads actual player/face assets, verifies independent ownership of all
render arrays, and checks that ordinary geometry/materials/baked palettes
remain byte-unchanged. Seven malformed assets, an invalid body index, a
mixed head/body triangle, GPU creation failure and GPU update failure are
handled explicitly. The test also frees the original model while the
alternate copy is alive, checking that later updates have no borrowed
storage dependency.

The original-instruction portion checks 400 callbacks through allocation,
talk, update, repeated attach, detach and fresh reattach, comparing all 88
modeled face-state bytes and 147 controlled RNG calls. Another 48 cases
check `00183090` face-before-body order and unchanged activity0; 72 cases
check the direct `001FD950` Dennis talk events. Glyph layout/render workers
are intercepted in those event-only cases.

The callback state comparison uses host IEEE float32 arithmetic, matching
the pre-existing face branch oracle. A truncating arithmetic model differs
by one ULP at the first mouth update; physical EE/VU arithmetic and live
RNG call order remain outside this proof. The RNG fixture uses the original
31-bit return domain. It does not seed the live game or claim a matching
whole-game random stream.

`tools/test_face_allocation_reference.py` separately verifies the full
original allocator/initializer/free path against two immutable captures:
the new face block was zero and free clears all 208 bytes. Its 256 reused
face cases prove the partial reset. Thus the earlier uncertainty about
fresh Dennis allocation contents is resolved for these captures; arbitrary
captured face weights are not used as universal initial state.

`em_area11_interaction_host` now owns one instance: it loads it with the
host, attaches at the B81D0 service, forwards the direct FD950 talk events,
ticks it first in the ready2 player worker and detaches at the frame close
(see `AREA11_INTERACTION_HOST.md`). The live Roger script/dialogue binding
and a paired original cinematic rendering comparison remain open.
