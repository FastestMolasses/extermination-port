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

Audio asset coverage remains incomplete: the current native SFX manifest
has no entries for panel cues3EE/3EF, and its existing sound API silently
ignores unmapped IDs. The fixture verifies the requested cues and ordering,
not audible panel effects. Those original sound-bank bindings must be
recovered before claiming the live interaction is complete.

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

Run `make test-area11-interaction-host` for the AddressSanitizer and
UndefinedBehaviorSanitizer fixture. Existing original-instruction oracles
remain the evidence for individual numerical routines and controller rules;
this fixture checks that their native services work together.
