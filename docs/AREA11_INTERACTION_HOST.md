# Native interaction services

`em_area11_interaction_host` assembles the panel, elevator, shared player,
timed messages and persistent BATTERY/ITEM status adapter using the actual
local assets. Passing null math/status hooks selects the original SDK math
and native inventory, camera, sound, music, fade and UI services. Alternate
hooks remain available for explicit host boundaries. Missing required work
faults; it never silently completes a script or grants panel power.

The module is built but is not yet installed in the live scene loop. The
complete 11-owner publication/Use walker, original Roger/door runtimes and
normal status hub are subsequent integration requirements. Loading this
module does not create another independent proximity scan.

The actual-asset sanitizer fixture links the native player pose host, raw
channels, model, collision, camera, scripts, inventory and status modules.
It checks panel refusal (156 ordinary callbacks), the first battery with
actual turn/camera settling (118), default-No/reselection/cancellation (170),
and successful discharge followed by panel power and release (285). The
discharge occupies61 status callbacks. These are fixture callback counts,
not claimed unassisted original playthrough timings. Direct owner claims,
the previous published list, ordinary camera evolution, GPU submission and
audio devices are explicit fixture boundaries.

Loading now requires the verified AREA11 sound bank and selects its area
remap only after all other host resources are bound. Teardown clears that
selection. Cue `3EE` is accepted as original silence; `3EF` uses the original
sample, pitch and voice gains. A missing cue fails the script callback.
The fixture checks selection, missing-bank cleanup and reload, while its
audio device remains a boundary. The separate actual-mixer and original
driver proofs are described in `AREA11_PANEL_SFX.md`; hardware interpolation,
reverb and scheduling remain unfinished.

Across status callbacks, raw pose channels, displayed palettes and panel/
elevator script state stay frozen. The final status frame remains consumed;
the shared recovery byte receives70 before ordinary camera updates resume.
Successful discharge updates charge, the panel's original controller bytes,
the persistent power flag and indicator lifetime. Native status exit clears
its separate draw pool after restoring projection; that clear must remain
valid after the UI camera context ends.

Panel alignment and facing are checked against original saved data. Pickup
camera sub8 updates only the actual target and then publishes DD980 depth
context; it leaves camera desired vectors unchanged. Elevator sub0 writes
both actual and desired vectors. After a pickup owner changes shared camera
fields, the scene must call `em_area11_interaction_host_camera_fields` before
a later panel/elevator callback imports camera fields from the native camera.

Whole-world teardown must detach player stage/Use hooks and unload/reset the
player pose before freeing owner tokens with host_clear. Ordinary completion
instead releases ownership in the next shared player callback. The fixture
also checks teardown during an acquired interaction followed by clean reload.

## Dennis cinematic face

The host owns an `EmPlayerFaceHost` prepared at load from the actual player
EMDL and `opening/player_face.*` (resource preparation only; the face stays
detached and consumes no RNG). `em_area11_interaction_host_face_attach` is the
`001B81D0` service: it attaches the face and writes player_ready2. It requires
the shared owner and ready1/2 and faults otherwise. The original's immediate
(`ev+0x14`) `001B82D0` branch can call `001B81D0` while `70003B8F` is still 0;
that pre-acquisition route is not modelled and fails loudly.
`em_area11_interaction_host_face_talk` is the direct `001FD950` ->
`001D06E0` event and is accepted only at ready2; it never touches the
activity mailbox.

With ready2 the shared runtime calls the host's cinematic player worker.
Like `00183090`, it ticks the face (`001D0C70` -> `001D0720`) before any body
work, then advances the deferred foreign bank, an active shared animation or
the ordinary source. The frame core's close (`001B82D0` op4/6) emits
RELEASE_SKELETON only at ready2; the host detaches the face (`001CA770`) and
the core writes ready1. `em_area11_interaction_host_player_record` returns the
alternate face mesh with the current player palette only while ready2.

The fixture's `cinematic_face` scenarios attach, talk, request the foreign
bank on the next callback (source0.5, 690.5 remaining), pause everything
through a status page, close with frame4, and release to default idle0 at 80.
A rejected face GPU update retains ownership and faults before the body is
bound. The status pause uses the actual pickup request, so it runs after
`first_battery` and keeps that acquired battery: the original pickup program
adds the item (`001C40B0`) before requesting status, and the real battery
page rejects a request with an empty inventory. Roger's live script, camera
and media are not driven by this fixture.

Run `make test-area11-interaction-host` for the AddressSanitizer and
UndefinedBehaviorSanitizer fixture. Existing original-instruction oracles
remain the evidence for individual numerical routines and controller rules;
this fixture checks that their native services work together.
