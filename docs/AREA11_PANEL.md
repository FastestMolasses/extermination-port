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
Original input bits8000/2000 select Yes/No, with Up taking precedence;
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
under ASan/UBSan. Menu logic is source-derived and has not yet been compared
with a captured original menu session. `test-pickup-lights` also checks
the actual item1B take and charge/capacity persistence/refill/count wrapping.

Remaining binding work: the original BATTERY page rendering/input flow,
global scan arbitration, consistent player alignment, camera retargeting,
animation15C assets, the original script records and handler adapters,
and the global power-bit callback. No end-to-end panel fidelity is claimed
until those bindings and an original session comparison are complete.
