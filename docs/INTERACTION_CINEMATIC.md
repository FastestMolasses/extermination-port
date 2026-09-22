# Original cinematic entry

`em_interaction_cinematic_command` implements `001B82D0` subcommands9–12
against the same `EmInteractionFrame` used by the panel, pickups and elevator.
The Roger encounter uses sub12. This helper is built but still requires its
live Roger/player/media bindings.

Phase0 publishes selector2/camera-top1, clears the original activity and
skip/counter bytes, starts fade-out4, requests the record's stream, increments
the script phase, then mutes channels0 and1. Phase1 waits for fade phase2 and
enters the bars immediately. Phase2 waits for the actual player-ready signal
unless the record requests an immediate entry; sub11/12 attach the player's
face before configuring scope0. The immediate branch increments phase before
the scope call, while the player-ready branch does so afterwards. Phase3
requires stream-ready exactly1, starts fade-in16 and publishes ready/skip
state. The original phase4 fade-complete branch is also preserved.

Fade and stream readiness come from live services. Missing side-effect
workers fail explicitly. No local timer substitutes for either handshake.
`make test-interaction-cinematic-reference` executes the original command
instructions for 6,184 cases and compares full shared state, script state,
arguments, service order and state visible at each service call. Eight
additional cases check immediate failure at each required native boundary.
The fade, stream, face and projection workers are intercepted in this proof;
their implementation and live presentation are separate checks.

Terminology correction: `001B81D0` uses `001CA700` to attach the face object at
player+90, with suppressed bone index7 at+94. `001D06D0` selects its speed1.
The resulting player-ready2 byte indicates that this face is attached; it
does not itself replace the body skeleton. `00183090` ticks the face before
its body-animation request logic. A separate op0A command changes the body
bank/mode. The native runtime's current ready2 rejection remains necessary
until both workers are bound; calling it an alternate body skeleton was
inaccurate.
