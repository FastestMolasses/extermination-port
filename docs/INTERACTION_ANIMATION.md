# Scripted player animation

The panel and elevator need the original request/commit/advance contract.
The legacy `sa_*` player path clears a finished clip automatically. Original
`00183090`, `001C67E0` and `001C64F0` instead hold these terminal clips at their
last sample and publish player+200 bit1000 until another request replaces them.
Re-requesting the current clip does not restart its cursor.

`EmInteractionAnimation` implements the independently verified current-bank
clips47 and15C at rate1, with command blend0 or1. Their original headers have
next=-2 and no event table. The native exporters verify those fields as well
as duration and captured bone palettes. Missing clips and unsupported rates,
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

`make test-interaction-animation-reference` executes original83090/clip-init/
clock instructions from the owner's ELF against the native C and real player
asset.662 callback comparisons cover both clips/blends, repeated requests while
running and after completion, high-bit transition state, remaining time,
source sample cursor and end flags. Channel sampling calls are explicit ordered
boundaries in this clock oracle. Separate exporter checks compare all21 captured
world matrices for panel, lever and idle within0.0000610352.

This is a component, not yet the live interaction path. The shared owner runtime
must tick it only when the original player task runs, publish placement/hip
mirrors, and preserve status-page pause behavior. Acquisition requires the
original idle blend8 through per-node quaternion/translation/scale state;
ordinary matrix interpolation cannot establish that transition's fidelity.
The acquisition/restore pose bridge and live panel/elevator binding remain work.
