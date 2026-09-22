# Scripted player animation

The panel, elevator, pickups and door share the original request/commit/advance contract.
The legacy `sa_*` player path clears a finished clip automatically. Original
`00183090`, `001C67E0` and `001C64F0` instead hold these terminal clips at their
last sample and publish player+200 bit1000 until another request replaces them.
Re-requesting the current clip does not restart its cursor.

`EmInteractionAnimation` implements the independently verified current-bank
clips40..43,45,47 and15C at rate1, with command blend0 or1. Their original headers have
next=-2 and no event table. The native exporters verify those fields and
durations. Captured palettes separately cover panel, lever and idle; the
door/pickup exports validate their raw keys without claiming a live pose capture.
Missing clips and unsupported rates,
blends or clip families return failure rather than a fabricated end flag.

On a changed request,00183090 initializes the clip and returns0, suppressing
the ordinary player-frame advance. Blend0 initialization resolves its internal
one-tick transition immediately and publishes sample0. Blend1 initialization
retains the prior pose for the commit callback; the next callback resolves the
transition and publishes sample0. Only later callbacks advance the source
cursor. Actor+3C is remaining time, not an elapsed frame.

| Original command | Commit callback0 | First sample1 | First end flag |
|---|---|---|---|
| Panel15C, blend0 | Sample0 | Callback1 | Callback121 |
| Elevator47, blend1 | Prior pose | Callback2 | Callback201 |
| Door43/45, blend1 | Prior pose | Callback2 | Callback151 |

`make test-interaction-animation-reference` executes original83090/clip-init/
clock instructions from the owner's ELF against the native C and real player
asset.1,582 callback comparisons cover all seven clips and both blends, repeated requests while
running and after completion, high-bit transition state, remaining time,
source sample cursor and end flags. Channel sampling calls are explicit ordered
boundaries in this clock oracle. Separate exporter checks compare all21 captured
world matrices for panel, lever and idle within0.0000610352.

The shared owner runtime ticks this worker only when the original player task
runs. Its raw pose worker also receives blend1 commits that preserve the displayed
palette, so later source-channel transitions remain coherent. Acquisition uses
the recovered idle blend8 through quaternion/translation/scale channels. Live
scene activation and its world-loading boundaries remain separate from these
component checks.

`em_interaction_frame` also implements door preparation sub0. Like pickup
sub13 it sets camera_top2 and retains activity/letterbox state; its selector
is2 instead of1. Both wait for actual player readiness. Original script phase
is a byte write, preserving the upper three bytes of that word. The expanded
frame oracle passes11,520 original state and ordered-call cases, retaining
all previous sub2/sub4/sub13 cases.
