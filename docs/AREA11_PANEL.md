# AREA11 power panel: original interaction evidence

## Shared runtime adapter

`em_panel_runtime` now connects the original owner and exported programs to
one `EmInteractionRuntime`. The owner token survives the BATTERY menu, the
post-menu script and the following player-release callback. Status frames
freeze both the owner and its script. Failed alignment, camera, message,
status, sound, power or indicator bindings stop the adapter and retain its
owner token; they never count as successful completion.

`make test-panel-runtime` combines the actual script and message assets with
the original frame handshake, raw player pose channels and battery-discharge
core under ASan/UBSan. Six full sequences cover idle and walking acquisition,
no battery, cancellation and successful discharge. The global message is
visible149 times; the empty terminal record is not a visible message frame.
The menu request occurs at ordinary callback154. After the explicit status
exit boundary, clip15C commits at157, sound3EF occurs at169, power at282,
frame exit at283 and player release at284. Paused menu time is excluded from
these ordinary-callback numbers. The post-menu offsets remain14/127/128 for
sound/power/frame exit. Release from15C resets the raw idle cursor to0.

The fixture also freezes150 callbacks without changing any owner, script,
message or pose state, and checks eight distinct failed host operations.
Geometry, camera/audio side effects and the status dispatcher's eventual
exit are explicit fixture boundaries. Cancellation returns to BATTERY
browse; this test supplies a later status exit and does not implement Back
as closing the page. Scene wiring and full menu/camera/render comparisons
remain required; this adapter test is not an end-to-end fidelity claim.

The fixture begins at an explicit shared-owner claim. The later Use-caller
audit in `INTERACTION_SCAN.md` additionally recovers001798D0: a winning
ordinary Use scan requests the default clip with blend0 before setting
action25. The following player callback then performs selector takeover.
Thus the fixture's direct acquisition from walking tests a supported raw
source transition, not the usual complete walk-to-panel Use path. The live
host must preserve that earlier default-clip request and callback boundary.

The normal scene's static model04 panel is original placement18 at
`(240,245,232.800003)`, yaw `-pi`, behavior `00159210`, class84/subtype24.
Its owner is the power switch; the former Roger-at331.7 battery-console
binding was incorrect. `em_panel.c` is a readable host core; since WP-4
(2026-09-23) it is live through the AREA11 interaction host at pool node #26
(AREA11_INTERACTION_HOST.md). The scene still never offers a substitute
confirmation or switches power on without the original script: the power bit
is the canonical D_0081084C, set only by 001580C0.

## Interaction and owner state

`00184BA0` scans one whole interactive list on the use-button edge, only
when its global selector/lock gates permit it. It selects the nearest
accepted candidate with a strict less-than comparison, then writes
owner+B=4 and selector3B8D=3. The panel's `00183EF0` class4/mode0/type24
gate uses descriptor275480: planar distance at most9.5, absolute Y
difference at most20, and `abs(wrap(pi+playerYaw-ownerYaw)) <= pi/4`.
There is no additional raycast for this subtype. `em_panel_candidate`
implements only that per-object gate; the host must retain the global
scan gates and single-winner arbitration before arming it.

`00157860` immediately calls `001B6F00`: player yaw becomes
`wrap(ownerYaw+pi)`, and the owner world matrix transforms local
`(.3,0,9,1)` while retaining the player's ground Y. `00182F90` shifts the
player's position mirrors consistently. For the panel placement this is
approximately `(239.7,currentY,223.8)`, yaw0; the matrix is authoritative.

If item1B count (`00810C7F`) is zero, it starts script246F20 and sets
message token80000018. Otherwise an ordinary use starts script2477A0.
Both first acquire the scripted frame, retarget the camera with opcodeD
sub3, and show global message-bank line18. That line identifies the
emergency battery terminal; its existing exported text must be used.
The latter script ends by calling00157F60, which clears owner+A/B,
restores status1, and posts the ordinary BATTERY page request **B0=1,
B1=82**, with the owner reference. Request6 belongs to subtype38, not
this panel.

The script's entry record carries120.0, but opcode7/sub2 waits on the
actual frame/letterbox handshake. It is not a hardcoded120-frame delay.
The host hooks preserve00159210's distinction between starting a script
with an immediate interpreter tick and starting one for the next tick.

## Battery confirmation and discharge

Original002149F0 uses existing message-bank group5 lines8/9 for the
two-unit confirmation and insufficient-charge result. It defaults to No.
Original input bits8000/2000 select Yes/No, with Left taking precedence;
40 confirms and20 cancels back to the battery list. The ordinary request
opens confirmation even when charge is low: selecting Yes tests charge
and then shows the insufficient result for240 ticks or until40/20.

After Yes with enough charge, state6 starts a timer at1. On expiration it
resets to30 and subtracts two half-units unless charge already equals the
target. Type24 costs2 full units /4 half-units: discharge ticks1 and31
subtract the two units, and tick61 completes. Original input mask870
can finish this animation early. Completion marks owner+A=1, owner+B=5,
and resumes scripted mode. `em_panel_battery_step` exposes this exact
logical sequence and sound/selection events; it does not render a menu,
invent text, or close the actual page on the host's behalf.

The owner resumes script247BE0 after menu discharge. It retargets the
camera, starts player animation15C, waits with original opcode2 for10,
calls001575B0 (sound3EF), waits110, and calls001580C0. That callback sets
bit7 in the area's00810841 byte and plays3EE. Opcode7/sub4 releases the
scripted frame. Only after the interpreter finishes does the owner mark
its indicator child state3 and clear that pointer. Cancellation uses
script247DA0 and restores scan eligibility. No guessed panel slide or
collision hull participates in either path.

Opcode2 is not an elapsed-time shortcut: its first call seeds the timer,
later calls subtract1, and another call observes zero and completes. A
record seeded with10 therefore takes12 handler calls. The original script
sequencer's yield/continue result then determines when the next record runs.

## Inventory and validation

Original001C40B0 cases1B/1C/1D add12/36/48 internal half-units, raise
capacity to at least that pack's value, and cap charge to capacity. The
item count is an unclamped byte store. Smaller packs refill charge without
shrinking capacity. `em_pickup` now preserves these semantics; HUD display
units are charge/capacity shifted right one. The pending pickup event
order is unchanged, and scene clears preserve the inventory.

`tools/test_panel_reference.py` executes unmodified original00159210 and
00157860 instructions from the local ELF and compares448 state/call-order
cases against the host core. Script handlers, alignment and graphics are
explicit external boundaries in that oracle. `tests/panel_interaction_test.c`
checks the eligibility boundaries, refusal/cancellation/success paths,
default No, insufficient-charge duration,61-tick discharge and early finish
under ASan/UBSan. `tools/test_battery_reference.py` additionally executes
original002149F0 over936 confirmation/discharge cases and compares phase,
cursor, timers, charge, owner flags, completion and sound-call order.
`test-pickup-lights` also checks
the actual item1B take and charge/capacity persistence/refill/count wrapping.

## Captured original page and exported program

The isolated original session used the pristine first-control state04,
then explicitly seeded a small battery and the use-scan result in a
disposable session. This is a controlled interaction fixture, not evidence
that the player has naturally collected the item or walked to the panel.
The original game performed alignment, message/status transitions, battery
discharge, animation and the final power callback. New isolated slots08,
07 and06 preserve confirmation, animation and completion; state04 was not
overwritten. The emulator exited after capture.

Ignored `Extermination/build/startup-reference/panel/` holds `confirm.png`,
the matching `eeMemory.bin`/`gs.bin`, `panel_animation.png`,
`animation_ee.bin`/`animation_scratchpad.bin`, `completed.png` and sampled
interaction/completion traces. The final original flag at0081084C is80.
The trace field named `position` reads player+B0, the animated hip. It
must not be confused with player+A0 or actor matrix+D0's translation:
those remain `(239.699997,229.890442,223.800003)` throughout this fixture.

`tools/export_panel.py` exports the local ELF's script arena246F20..247E20
as EMSC,27 original BATTERY TEX0 references,8 original message strings
with their tag2 color spans, the original background-state record and the
unmodified clip15C channel bytes. The extra white atlas texel implements
the original untextured No-cursor rectangle inside the ordered native
decor queue. It is not replacement game artwork. Source addresses/hashes
remain in the ignored asset report.

The standalone `em_battery_ui` module draws the original frame, labels,
pack icon, arrows, half-unit meter and Yes/No cursor; uses actual text
and color spans; and preserves No, Back, unavailable-device and discharge
states. `tools/test_battery_ui_reference.py` verifies every one of its29
confirmation decor/cursor submissions against the original EE GIF packet
buffers: TEX0, RGBA and both XY endpoints match exactly. Its loader and
flow test run with ASan/UBSan. Original text metrics264CD8+264CE0 resolve
to24 canvas pixels per line in the captured page. Text glyph rasterization
and shared background float/GS-color quantization are outside this packet
comparison; no native framebuffer identity is claimed.

`em_panel_program` uses the shared original script sequencer and typed
handlers for timer2, frame7, callback9, playerA, messageC and cameraD.
It patches the original message operands and requires real host operations.
Its post-menu script requests15C at tick1, sound3EF at tick14, power bit80
then sound3EE at tick127, and frame release at tick128. These sound/power
ticks agree with original sampled retarget frame8087, sound8101 and
power8214. Missing bindings cause a fault; they never advance as success.

`tools/export_panel_clip.py` resolves direct clip15C from the runtime
player bankD689C0, whole-byte identical to chunk28/f01_id3c.bin, at1C2E50.
It bakes121 frames with the recovered stateful channels at rate1.0 and
unnormalized quaternion blend. The original zero-translation root channel
requires no extra actor movement; node1 contains the animated hip.
Frame30's21 world matrices match the captured original with maximum
absolute error0.00006103515625, explicitly a host/EE rounding tolerance.
The exporter appends this clip while preserving every existing mesh,
texture and clip byte; it refuses to overwrite a differing existing15C.

Remaining live bindings: global use arbitration and player alignment,
the original shared frame/status entry and exit, the actual message-worker
completion signal, and the player takeover/release pose transitions. The camera
command calls0018CBD0 using the current distance and scratchpad rotation,
then0018D7B0 modes5/1; a fixed authored camera or the old door-camera helper
is not an equivalent substitute. (Historical: these bindings landed with
WP-4. The level smoke now plays the panel with the battery end to end and
matches route beat 03 row for row outside the status page's module load; see
LEVEL_SMOKE.md.)

The isolated `em_camera_retarget_seed` now implements CBD0's scalar tail
at an explicit transformed-offset boundary. Its948 original-instruction
cases match float bits under the bounded EE arithmetic model, and the
original panel camera matches exactly. Rotation and SDK square root are
helper boundaries. The readable decomp was
corrected from an erroneous unconditional falloff and inverted clamp;
its measured object similarity improved91.78% to94.04%, while it remains
assembly-backed. The full PS2 six-stage gate passes.

The panel program's message completion hook uses -1 for failure,0 for
waiting and1 for completion. Negative results fault the script and cannot
run its later battery callback; the sanitizer-backed program test covers
that failure path as well as missing camera bindings and callback timing.

## Shared camera and frame worker boundaries

The panel's scripted retarget runs on the live camera since census
L13..L16 (docs/CAMERA_LIVE.md): the translated 0018D330 prepass and the
whole 0018D910 (em_camera_follow_original / em_camera_leftovers_solver)
replace the retired `em_camera_probe` partial copy.
`tools/test_camera_interaction_fixture.py` checks the retarget's camera
words byte for byte against this capture.

`camera_interaction_retarget_area11` connects that prepass/bounds work,
CBD0 and the existing DD20 solver with mask6. Its captured panel fixture
matches eye, target, hit/ground flags and overhead exactly; the maximum
bounds error is0.000030517578125 against an explicit0.00006103515625
host/EE tolerance. The verified fixture has zero seed Euler angles. The
original fixture established the initial zero-rotation path. The subsequent
`em_camera_rotation` implementation now covers finite normalized Euler angles:
972 original SDK matrix/offset cases pass byte-for-byte, and a nonzero-yaw
elevator refusal capture matches all six camera coordinates. See
`AREA11_ELEVATOR.md` for the separate one-ULP overhead collision limitation.
The actual panel body belongs to cell-world set2 and therefore remains
included by mask6; it is not a guessed movable hull.

`em_interaction_frame` recovers001B82D0 sub2/sub4 state writes and ordered
letterbox/projection/audio calls. Its2592 original-instruction cases include
existing readiness, pending player takeover, release, skip and camera-mode
branches. It deliberately does not produce player readiness itself.

`em_interaction_runtime` gives panel/elevator adapters one shared owner
token and connects the verified frame/animation cores at explicit host
worker boundaries. Successful actual takeover publishes182D70 readiness
immediately. A blocked takeover remains blocked; absent hooks fault.
The original001AE040 status branch does not run the ordinary player task,
so the runtime consumes no animation or acquisition callbacks there.
After selector release,0015BA50 advances the player before0015B530 calls
182DF0; the runtime preserves that order and clears ownership only after
the real release hook succeeds. A finished clip holds its terminal pose.
The sanitizer test covers competing owners, paused pending commits,
endpoint hold, blend1 pose preservation and failed release. This is an
integration contract test, not a second original-instruction oracle.

`tools/export_interaction_idle.py` replaces only clip0 with80 frames from
the original player bank. Confirmation remaining75 corresponds to cursor5,
not elapsed75; its21 matrices agree with the original capture within
0.00006103515625. Existing nonzero clips, geometry and textures are
preserved. Acquisition's default-clip request has flags0: an already-active
idle0 keeps its cursor, while a different source clip requires the original
per-node blend8. Neither case adds a readiness delay. Release from47/15C
forces idle0 with flags1/blend0; its subsequent default/blend16 request
then does nothing because idle0 is already active. A matrix blend is not
provided as a fallback for a required source-channel transition.

`em_status_frame` implements original 001AE040's status entry and phases3/5.
Its432 original-instruction comparisons match state bytes and ordered
external calls. Entry resets the UI/sound/stream workers and changes the
control mode. Phase3 waits for the real stream gate and calls the status
page worker; only that worker's actual completion permits phase5. Exit
publishes the camera, restores ordinary control and issues the original
music/fade requests. Negative or missing host workers fault. Page drawing,
stream completion and sound/render side effects remain explicit host
boundaries, not a claim of complete native status integration.

The panel's message token80000018 uses the global timing table at272DF0.
Record18 has148 ticks and record19 is its zero-duration terminal; neither
has a voice cue or actor-talk slot. Since WP-8 the line runs on the live
message service (docs/MESSAGE_SERVICE.md; `em_panel_message` and its
`terminal.emod` are deleted); the notes below describe the timing it keeps.
It respects the actual request delay and stream flags 155/156; it has no
input dismissal or typewriter effect. The live service agrees with the
original chain FCA10/FDB80/FD790/FD950 at delays 0/1/30
(`test_panel_message_reference.py`). The full line appears 149 times because
the zero-timer completion call also draws it. The phase-2 completion signal
remains visible to the next script tick and is cleared by the following
message-service tick. Its glyphs are the translated chain of
docs/MESSAGE_GLYPH.md.

## ITEM root and status lifecycle

The original confirmation's No/Circle route first returns to BATTERY
browsing. Circle there loads module1F and returns to the radial ITEM menu.
Circle on that root sets screen63, which returns to the broader status
hub. It does not close the panel interaction. Triangle/Start can close
the status dispatcher only after its pending request, stream gate and
status request permit the original810 mask check.

An isolated capture from the pristine confirmation fixture preserves the
original root in local state12 and ignored `panel/root/` EE/GS/screenshot
artifacts. Only two actual Circle presses were used; no menu state bytes
were patched. The source confirmation fixture was not overwritten.

`em_item_root` implements0020EE50 at explicit draw, sound, module-loading
and child-page boundaries. Its3,024 original-instruction cases compare
all represented state bytes and ordered calls. Another540 cases execute
the original D930 ITEM angle table, including values immediately around
every threshold. The magnitude gate converts binary32 to a soft double
and compares against double0.8. The old readable decomp discarded both
comparison arguments; restoring them improves its measured object
similarity66.34% to67.98%, while it remains assembly-backed.

`em_status_page` implements the panel cold request, ITEM branch and exit
of0020CDC0, plus0020E0C0 and the001FEF70 inventory-module lookup. Its1,208
original-instruction cases cover the real asset-busy gates, successful
completion, input exit, changed-module reload, restore callbacks and
return through screen63. No timer fabricates an asynchronous completion.
The broader status hub and other child pages remain required workers;
missing or failed workers return a fault and retain status ownership.

`export_item_root.py` executes the original0020F170/0020F2A0 drawers for
all six hover states and exports their ordered commands,17 original GS
textures and five group1 help strings into ignored EMIRv2 data. The
native `em_item_ui` loader/renderer matches all107 original commands,
including blend changes and the analog trail's position within the
queue. Its sanitizer fixture also verifies shared-atlas invalidation.
The trail remains a mandatory worker: the original0020AC70 calls an
additive Gouraud fan, not a cursor image. That geometry worker and raw
stick normalization remain to be bound before a complete root-page
pixel comparison or live panel integration is claimed.

The shared overlay regression now covers ten pixel samples. Both
original subtract and opaque modes use TEST alpha>0; transparent source
fragments leave the destination untouched. Additive mode ignores alpha.
The Metal path preserves mixed-mode submission order and its intervening
clamps. This GPU fixture is synthetic and contains no original assets.

The subsequent `em_item_trail` core implements1B62C0 normalization,
20AC70's16-slot ring and1D66A0's GS triangle packet. Its169 stick cases
and22,016 fixed16 triangles over43 moving/decaying callbacks agree with
the original instructions and ring bytes. SDK sine/cosine/atan2/square
root results and integer conversion are explicit oracle boundaries;
that test does not claim whole-SDK errno or transcendental equivalence.
The original PRIM4C is an untextured Gouraud strip whose alternating
center/outer vertices produce32 visible fan triangles per slot. Its RGB
uses intensity/255, unlike the128-based modulation of textured sprites.

The ordered overlay triangle API passes a synthetic interpolated-color
pixel fixture alongside the ten existing blend tests. The three-frame
original-artwork screenshot is `build/item_ui_visual/root.png`, generated
by `tests/item_ui_visual.c`. Static icon, connector and help-box placement
align with the saved original root. The center glow now uses the original
intensity scale. This fixture starts the shared backdrop animation fresh;
background phase and GS/Metal raster filtering remain explicit differences.

The readable1D66A0 source also had an incorrect context expression. Its
cursor is at `*(D275670) + 0x10 + 4*slot`. Correcting the pointer and slot
scaling improves the measured C similarity67.06% to68.56%. It remains
assembly-backed and the full original-build verification gate passes.


`em_status_runtime` now owns the persistent outer frame/page lifecycle,
BATTERY page, ITEM page and cached trail submissions. It loads the actual
exported artwork and text, accepts a panel request without consuming the
rest of that ordinary callback, and consumes every later status frame,
including the final phase5 release frame. Repeated rendering does not tick
state or generate another trail sample.

`make test-status-runtime` passes actual-asset ASan/UBSan cases for61 discharge
callbacks, default No, Back to ITEM, battery re-selection, a50-callback wait
for an actual module-ready signal, final-frame consumption, and retained
faults when an inventory write or an unimplemented hub worker fails. The
required world services and broader status pages remain explicit boundaries.
Modules32..35 are actual DATA chunks50..53 and include audio-bank work;
a timer or an unrelated texture's presence cannot stand in for their readiness.

Battery pickup status now follows original request1/index1B..1D, independently
of the panel request82. After the pickup inventory mutation, its opcode09
yields; the next outer frame acquires status before the pickup script's
terminal opcode07/sub4 can release its owner. The adapter preserves that
pending request and leaves the final status release frame consumed.

Original149F0 selects the highest owned battery row, sets charge and capacity
to the acquired pack's12/36/48 half-units, clears the pending request, and
enters state3 with counter240. The displayed acquisition text comes from
original message group4, lines27..29; if the acquired pack is not the selected
highest pack, the original retains ordinary group3 help. EMBA version2 adds
these three strings without changing the existing sprites or text records.
The loader continues accepting version1 for the older panel path.

State3 draws the BATTERY page, decrements its counter, and returns to browsing
when it expires or a button in mask5060 is pressed. A button dismissal plays
the original Back cue. It does not confirm power or close status. The outer
Triangle/Start exit still goes through CDC0/E0C0. Browsing Cross calls the
actual185420 device lookup; an empty result produces original group5 line25
and its error banner. A different eligible owner requires a real binding,
and is a fault rather than an invented empty lookup or confirmation.

Validation now includes1,352 CDC0/exit cases and84 original149F0 acquisition
and input cases covering11,904 notice callbacks. The actual-asset sanitizer
fixture checks pickup entry, notice dismissal, empty-device browsing, outer
exit, final-frame consumption and retained inventory-write failure. The
original29-quad panel packet comparison still passes after the EMBA extension.
The shared list/draw workers and final pickup pixels remain separate checks;
no original pickup-menu screenshot comparison is claimed by these tests.

The full original SDK numerical cursor path is now implemented and tested in
[ITEM_SDK_MATH.md](ITEM_SDK_MATH.md), removing the host-libm boundary from the
new end-to-end stick and triangle oracle.
