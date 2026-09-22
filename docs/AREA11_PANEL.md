# AREA11 power panel: original interaction evidence

The normal scene's static model04 panel is original placement18 at
`(240,245,232.800003)`, yaw `-pi`, behavior `00159210`, class84/subtype24.
Its owner is the power switch; the former Roger-at331.7 battery-console
binding was incorrect. `em_panel.c` is a readable host core, currently
**unbound**: the native scene does not offer a substitute confirmation or
silently switch power on when the required UI/script bindings are absent.

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
is not an equivalent substitute. The native panel stays unbound until
these host adapters are ready. No end-to-end panel fidelity is claimed.

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

`em_camera_probe` now implements the original0018D330 prepass and the
AREA11 branch of0018D910. The original-instruction comparison covers216
prepass and432 bounds cases, including query endpoints, class filters,
ground78, overhead flags and the crossed-bound correction. Query results
and vector normalization are explicit boundaries; no general VU or
collision-engine equivalence is claimed.

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
has a voice cue or actor-talk slot. `export_panel.py` now writes both records
and their original strings to ignored `terminal.emod`. `em_panel_message`
reuses the opening's FD790/FD950 dialogue clock and subtitle renderer.
It respects the actual request delay and stream flags155/156; it has no
input dismissal or typewriter effect. The original chain FCA10/FDB80/
FD790/FD950 agrees across1578 worker callbacks, including delays0/1/30
and either stream busy flag. The full line appears149 times because the
zero-timer completion call also draws it. The phase2 completion signal
remains visible to the next script tick and is cleared by the following
message-service tick. Glyph drawing is an explicit oracle boundary.
