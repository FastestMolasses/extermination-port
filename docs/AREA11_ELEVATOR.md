# Original elevator interaction

`em_elevator.c` translates active owner00827B10 independently of the old
`em_examine` shortcut. It is currently unbound while the original script
commands, camera and player alignment are connected. Model creation and
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
No end-to-end elevator fidelity is claimed from this core test.
