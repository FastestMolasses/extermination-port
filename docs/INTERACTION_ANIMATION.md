# Scripted player animation

The panel, elevator, pickups and door share the original request/commit/advance contract.
The legacy `sa_*` player path clears a finished clip automatically. Original
`00183090`, `001C67E0` and `001C64F0` instead hold these terminal clips at their
last sample and publish player+200 bit1000 until another request replaces them.
Re-requesting the current clip does not restart its cursor.

`EmInteractionAnimation` implements the independently verified current-bank
clips 40..43, 45, 47 and 15C at rate 1, with command blend 0 or 1. Their original headers have
next=-2 and no event table. The native exporters verify those fields and
durations. Captured palettes separately cover panel, lever and idle; the
door/pickup exports validate their raw keys without claiming a live pose capture.
Missing clips and unsupported rates,
blends or clip families return failure rather than a fabricated end flag.

On a changed request, 00183090 initializes the clip and returns 0, suppressing
the ordinary player-frame advance. Blend 0 initialization resolves its internal
one-tick transition immediately and publishes sample 0. Blend 1 initialization
retains the prior pose for the commit callback; the next callback resolves the
transition and publishes sample 0. Only later callbacks advance the source
cursor. Actor+3C is remaining time, not an elapsed frame.

| Original command | Commit callback 0 | First sample 1 | First end flag |
|---|---|---|---|
| Panel 15C, blend 0 | Sample 0 | Callback 1 | Callback 121 |
| Elevator 47, blend 1 | Prior pose | Callback 2 | Callback 201 |
| Door 43/45, blend 1 | Prior pose | Callback 2 | Callback 151 |

`make test-interaction-animation-reference` executes original 00183090 / clip-init /
clock instructions from the owner's ELF against the native C and real player
asset. 1,582 callback comparisons cover all seven clips and both blends, repeated requests while
running and after completion, high-bit transition state, remaining time,
source sample cursor and end flags. Channel sampling calls are explicit ordered
boundaries in this clock oracle. Separate exporter checks compare all 21 captured
world matrices for panel, lever and idle within 0.0000610352.

Live, 00183090 is `em_player_stage_commit` (em_player_stage_workers) on a
script owner's takeover (the director, Roger, the truck trigger, the fence
door) since chain C7; see PLAYER_STAGE_WORKERS.md "The takeover". This module
is the commit and advance of the interaction runtime's takeovers that remain
(the panel, the terminal and the items), ticked only when the original player
task runs; the census row 00183090 names both, and the record's
anim_advance_time checks this module's clock every tick. Its raw pose worker
also receives blend 1 commits that preserve the displayed palette, so later
source-channel transitions remain coherent. Acquisition uses the recovered
idle blend 8 through quaternion/translation/scale channels.

`em_interaction_frame` also implements door preparation sub 0. Like pickup
sub 13 it sets camera_top 2 and retains activity/letterbox state; its selector
is 2 instead of 1. Both wait for actual player readiness. Original script phase
is a byte write, preserving the upper three bytes of that word. The expanded
frame oracle passes 11,520 original state and ordered-call cases, retaining
all previous sub 2 / sub 4 / sub 13 cases.
