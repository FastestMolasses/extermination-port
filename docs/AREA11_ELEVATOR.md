# Original elevator interaction

`em_elevator.c` translates active owner00827B10. Since WP-4 it is live
through the AREA11 interaction host at pool node #27 (AREA11_INTERACTION_HOST.md);
the old `em_examine` terminal and the legacy ride (`elevator_tick`) are
retired, and the level smoke matches the refusal and the ride with route
beats 02 and 04 row for row. Its state 0 (0x827B54..0x827BF0) is bound
too: the floor byte D_0081083A selects the actor's +0xB4 (190 or 230) and
the script heights, and 001C6380 builds its matrix before the child spawn;
the host's `em_area11_interaction_host_elevator_state0` places the port's
elevator mesh there, so an AREA11 rebuild after the ride draws it at the
lower floor. Model creation and
indicator allocation remain scene responsibilities; this core does not
manufacture missing actors or report a missing script as completed.

The owner chooses powered script82A750 or refusal82A990 on arm bit4. It
starts that script on one callback and first ticks it on the next callback.
Both scripts acquire the scripted frame, align the player and set facing.
The powered path sets a camera, starts/waits for clip47, calls movement
handler828050, sets the exit camera and releases the frame. The refusal path
uses camera handlerD/sub5 and global message1A before releasing the frame.
`tools/export_elevator.py` exports all15 unchanged records as local EMSC data.

**Owner+2A is not a300-frame refusal cooldown.** The powered path seeds0;
each script callback increments it until120, when sound19A plays. The
refusal seeds300, which bypasses that sound counter. Script completion clears
phase/arm, immediately restoring eligibility. The legacy level exporter now
stops assigning a fabricated300-frame delay to this interaction.

On completion, the owner checks the current area power bit. If set it toggles
81083A, snaps its own Y to190/230, and updates the move-to/description height
plus both camera-record heights. The indicator then copies its new node0
matrix. Its signed brightness level approaches128/0 by8 per active callback.
The150 movement ticks belong to828050, separate from owner script completion;
the old host elevator helper still conflates those events until this owner is
bound. The real script must own when that final snap/toggle occurs.

`make test-elevator-reference` runs2,520 original instruction cases from the
user's AREA11 overlay, including start/active/unknown phases, arm-bit values,
both positions, power/completion combinations, signed counter boundaries and
indicator levels. It compares owner fields, three patched script heights and
sound/script/pose/indicator/actor callback order. Model allocation, script
handlers, collision and final rendering are explicit external boundaries.
The same target checks240 original00828050 carry cases and20 boot-handler
command cases. Motion initialization chooses sound452/453 and waits one call;
then each of150 calls independently truncates the platform+B4, player origin+A4
(global00810354) and camera target Y additions before rebuilding the platform.
It does not toggle the owner's lower/upper state. The command oracle executes
original001B94F0/001B9C10/001B8FC0/001B9A00 instructions, comparing direct fields,
phase and alignment/publication call counts. Full player placement00182F90 and
camera publication001DD980 are intercepted boundaries.

`em_elevator_program` now reads these actual records and routes their commands
to required typed host bindings. Opcode1/sub1 places the player immediately;
it is not a timed walk. Opcode4/sub8 sets yaw immediately. Zero-duration
opcode0/sub0 copies and publishes the camera on one call, yields, then publishes
and completes on the next. The clip47 request uses rate1/blend1; opcodeA/sub3
waits for the real animation end flag. The refusal waits on actual message1A.
Its frame sub2/sub4, chase camera and message worker remain host responsibilities.

`em_elevator_runtime` now connects the owner and exported program to the shared
interaction runtime. Winning the use scan claims one stable owner and sets
armed bit4; a competing interaction cannot replace it. Player and pooled-owner
callbacks remain separate to preserve the original task order. Status frames
freeze both. A failed camera/message/animation binding retains ownership and
does not toggle the elevator or masquerade as script completion.

`make test-elevator-runtime` runs the actual elevator program and player model
under ASan/UBSan, using the verified frame/animation clocks and motion helper.
Relative to use tick0, clip47 commits at8, reports its real end flag at209,
enters carry at210, completes the owner at363, and releases the player at364.
Both ride directions pass;150 paused calls change no clocks. A401-poll refusal
fixture verifies that only host message completion releases the script.
Acquisition/release pose channels, the world camera/presenter, and the use scan
remain explicit host boundaries; this is not yet a live scene integration.

**Correction (WP-4, 2026-09-23).** The carry's three add.s are the EE's
single-guard-bit add (em_pose_math.h `pose_add`, the model the fan, truck,
director and pose captures settled), not a plain truncation. The played ride
(route 04_elevator_ride) holds all 150 carried player Y values, f394..f543,
ending at 190.00061; the truncating model this section used to claim
(189.99832153320312 down, 229.9993896484375 up) matches none of them after
the first step. `em_elevator.c` and the oracle's add.s now use the guard-bit
model, and `make test-elevator-reference` replays the capture's 150 values
through both. Only the owner's completion snaps its own Y to 190/230.

The refusal's D/sub5 camera now has a complete first-level binding through
`camera_interaction_retarget_distance_area11`. The argument is−20; current
camera+C and preset camera+64 both remain−46.8 in the captured reference.
`em_camera_rotation` follows original001029C0/00102C58/001026A0, including
Z/Y/X order, the SDK polynomial, zero-angle no-op, homogeneous components
and each VU operation's rounding.972 original-instruction matrix/offset
cases match all77,760 output bytes in the bounded arithmetic model.

A fresh original refusal capture in slot13 starts from immutable post-panel
slot06, clears only the area power bit and arms the actual elevator owner.
It does not write player pose or camera fields. The source archive hash is
unchanged and the isolated emulator exits0. At yaw−1.3037610054016113, native
eye/target coordinates, camera bounds and hit/probe/ground flags match the
capture exactly. The native overhead collision result differs by one float
ULP,0.000030517578125; that limitation is recorded under the existing camera
collision tolerance. The panel fixture remains unchanged. Reproduce with
`make test-camera-rotation-reference test-camera-interaction-fixture`.
This is not a claim of complete camera-solver or PS2 hardware equivalence.

The refusal's global message8000001A uses the same original presenter as
the panel message80000018: since WP-8 both run on the live message service
(docs/MESSAGE_SERVICE.md; `EmPanelMessage` and `elevator_refusal.emod` are
deleted). `test_panel_message_reference.py` runs the live service against
the original FCA10/FDB80/FD790/FD950 chain for both lines at delays 0/1/30
(1,052 ticks; the busy flags are covered by the service's own oracle). Each
line has 149 visible draws; input does not dismiss it. Completion remains
visible to the following script-worker callback.

`make test-elevator-program` uses the exported scripts under ASan/UBSan with
explicit frame/player/camera/message boundaries. It checks refusal, both ride
directions, both camera waits,150 carry callbacks, the final3 script ticks
before owner completion, and missing/failed binding paths. A negative host
script result cannot masquerade as completion or toggle the elevator.
The live legacy examine/elevator path is still unbound to this adapter;
these tests do not establish end-to-end elevator fidelity.

`tools/export_elevator_clip.py` replaces only the existing clip47 palettes
with the original stateful channel sampler and unnormalized quaternion blend.
It validates the complete animation bank and skeleton against a new original
EE capture, then checks all21 world bone matrices at source frame39. Maximum
matrix error is0.0000610352, versus0.00436401 for the old baked clip. All200
sampled frames have zero root-channel translation; actor+A0 stays the world
origin. The reference actor+3C value161 is remaining time, not source frame161.
The exporter preserves all56 other clips, mesh, textures, tables and file size;
a second run changes no bytes. Native transition blending remains separate.

The isolated original fixture starts from the saved post-panel state06 and
writes only arm bit4 to the actual elevator owner at7AA880. It does not seed
player, camera or animation state. New slot11 captures the resulting clip47;
the starting archive remains unchanged. Local capture/report files live under
decomp `build/startup-reference/elevator/` and port `build/elevator_clip_export/`.
