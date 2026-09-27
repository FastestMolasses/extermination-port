# Original cinematic entry

`em_interaction_cinematic_command` implements `001B82D0` subcommands 9-12
against the same `EmInteractionFrame` used by the panel, pickups and elevator.
The Roger encounter uses sub 12. It is live: `em_area_script`'s op07
(001B82D0) calls it for subcommands 9-12 through `em_area11_script_host`
(the truck and Roger's scripts, census L19 / L22; AREA_SCRIPT.md).

Phase 0 publishes selector 2 / camera-top 1, clears the original activity and
skip/counter bytes, starts fade-out 4, requests the record's stream, increments
the script phase, then mutes channels 0 and 1. Phase 1 waits for fade phase 2 and
enters the bars immediately. Phase 2 waits for the actual player-ready signal
unless the record requests an immediate entry; sub 11/12 attach the player's
face before configuring scope 0. The immediate branch increments phase before
the scope call, while the player-ready branch does so afterwards. Phase 3
requires stream-ready exactly 1, starts fade-in 16 and publishes ready/skip
state. The original phase 4 fade-complete branch is also preserved.

Fade and stream readiness come from live services. Missing side-effect
workers fail explicitly. No local timer substitutes for either handshake.
`make test-interaction-cinematic-reference` executes the original command
instructions for 6,184 cases and compares full shared state, script state,
arguments, service order and state visible at each service call. Eight
additional cases check immediate failure at each required native boundary.
The fade, stream, face and projection workers are intercepted in this proof;
their implementation and live presentation are separate checks.

Terminology correction: `001B81D0` uses `001CA700` to attach the face object at
player+90, with suppressed bone index 7 at +94. `001D06D0` selects its speed 1.
The resulting player-ready 2 byte indicates that this face is attached; it
does not itself replace the body skeleton. `00183090` ticks the face before
its body-animation request logic. A separate op0A command changes the body
bank/mode; calling ready 2 an alternate body skeleton was inaccurate. Live, a
script owner's takeover runs 00183090 as `em_player_stage_commit` (chain C7,
PLAYER_STAGE_WORKERS.md), with 001D0C70 = the interaction host's face tick
when 3B8F == 2. The interaction runtime no longer rejects ready 2: it runs its
cinematic player worker and faults without one.
