# Native interaction services

`em_area11_interaction_host` assembles the panel, elevator, shared player,
timed messages and persistent BATTERY/ITEM status adapter using the actual
local assets. Passing null math/status hooks selects the original SDK math
and native inventory, camera, sound, music, fade and UI services. Alternate
hooks remain available for explicit host boundaries. Missing required work
faults; it never silently completes a script or grants panel power.

## Live binding (WP-4, 2026-09-23)

The host is live in AREA11 (SCENE_COORDINATOR_DESIGN.md section 6, "WP-4
landed"):

- **Lifecycle.** The scene bindings load it at the state-0 rebuild, after
  001B6990 has placed the roster (w_001B6990), and install the Use hook
  (`em_area11_interaction_host_use`), the player-stage hook
  (`em_area11_interaction_host_player`) and, since WP-8, the host hooks of
  the live step-F message service (`em_area11_interaction_host_message_host`:
  the 001D06E0 face talk and the gate that holds step F while the status
  page layer runs; docs/MESSAGE_SERVICE.md). w_001AFCA0 and game shutdown
  detach and clear it.
- **Owners.** Pool node #26 (00159210, area11[18]) runs
  `em_area11_interaction_host_panel_tick` from its second call (state 1),
  #27 (00827B10, area11[19]) `em_area11_interaction_host_elevator_tick`; the
  first call is each owner's state 0 (its child). The terminal's state 0
  also places the actor (0x827B54..0x827BF0: D_0081083A selects 190/230 for
  +0xB4 and the script heights, then 001C6380 builds the matrix):
  `em_area11_interaction_host_elevator_state0` re-derives the owner from
  the floor byte, sets the Use descriptor height and rebuilds the elevator
  pose at that height, before the child spawn (0x827C18). The manifest
  placement is always the upper floor, so an AREA11 rebuild after the ride
  would otherwise draw the elevator at 230 under an owner at 190. Each state-1 tick ends
  with the owner's 001B17A0 publication (the 001B1630 cone/range gate, then
  001B1B70), and 001AAD00 swaps the list in (`_publish`).
- **Items (WP-6).** The load binds the seven item owners (00219550 x6,
  0015AFA0) to `em_pickup_original` (taken ones are skipped); their pool
  nodes #0..#6 call `_pickup_state0` on the first call and
  `_pickup_tick` after it, each owner at its own position, publishing
  through the translated 001B17A0 (docs/PICKUP_OWNERS.md). The host
  supplies their turn, camera-settle, status-request, aura (001F1180) and
  take-sound hooks; the take posts its original B0/B1 request.
- **Use.** On D_00810E74 & 0x40 the hook runs 00184BA0 over the published
  list (the panel's type-24 predicate, the elevator's selector-1 predicate,
  the items' selector-3 predicate `em_interaction_pickup_candidate` with
  0019A910 mode 6 as the port's camera segment query), arms the winner,
  claims the shared owner (3B8D = 3) and runs 001798D0: one pass, lowest
  score wins, a result of 2 commits at once. It returns 0 without a winner,
  so the player's own Use actions follow. The door and Roger are not in the
  list yet (WP-7/WP-9).
- **Status screens (WP-4 requests, WP-5 all).** The scene core's
  0020E060/0020CDC0 run `_status_open`/`_status_page`, the original page
  layer over the canonical request bytes, for every status screen, drawn by
  `_status_render` at 001D1EA0(0): the panel's 00157F60 request (B0 = 1 /
  B1 = 0x82 / D_008106D0 = the panel's record address; 002149F0's exit
  writes 3B8D = 3), a battery pickup's 001C47A0 request (B0 = 1 / B1 =
  0x1B..0x1D: the ITEM page's BATTERY acquisition notice) and START/TRIANGLE
  (B0 == 0: the hub phase). A request whose page is not translated (the
  other item takes: MAP, DATABASE, SPR4, the ITEM child) faults in 0020CDC0
  with a report naming the page. The panel owner is bound only for B0 != 0 with
  B1 & 0x80 (a stale B1 stays after a request). The hub phase is the
  original `em_status_hub` inside the runtime (WP-5;
  `em_status_runtime_bind_hub` with `panel/status_hub.emhs` and
  `panel/status_hub_atlas.emha`). The host supplies its 00209DF0 inputs
  (`hub_display`: D_00810858/5C, D_008104E4 = g.pd_infected, C7F, CB2/CB7,
  CA4/CA6, CB4; CA8..CB0 fault) and its status models: `em_status_models`
  (loaded from `assets/status_models`, tools/export_status_models.py) runs
  the hub's model workers (`hub_models`: 001AFF10 + 0020E6F0, 0020E250 and
  001B0000), the page events CONFIGURE (0020DFA0's pool clear and UI view),
  CLEAR_DRAW (001AFEB0) and RESET_DRAW (001AFE60), and draws the queued
  models (`hub_models_draw`) between the background and the 2D layer
  (STATUS_SCENE.md section 7). It also loads the SDK tables of 0020A7A0's
  sine (`assets/sdk_math_tables.emsm`). ITEM children other than BATTERY
  fault (no other_page hooks). The fixture's `status_hub_route` scenario
  checks the hub's draw cadence (nothing at sub-state 0, one 0020A7A0 step,
  the backdrop flushed before the seven model draws, and one 00209DF0 per
  sub-state-1 frame, UI+0x20), hover 4 + X into ITEM and Back to the hub
  with the texture slot re-uploaded, the 0020E0C0 exit's two extra ticks
  and the fault on X at hover 3 (MAP).
- **Canonical storage.** The shared EmInteractionFrame is a per-call view:
  every entry point loads it from EmSceneState (3B8D, 3B8F, 3B92, 3B84,
  D_008106D4..DF, D_008106EF, D_008106F3), the port camera (D_008101E1/E3/E4/
  E6, D_008105F0) and the live message block's phase D_002821B4, and stores
  it back. The panel and terminal scripts' message command is 001B7D60 case
  0 on the live service (`em_message_live_post`: D_002821B0 = 2, B4 = 1,
  B8 = the line, BC = the delay); the tick log samples that block. The panel's
  power bit is D_0081084C and the elevator's floor D_0081083A, both canonical
  D2 progress bytes.

The runtime's own status frame machine (`em_status_runtime_tick`,
`_battery_open`, `_pickup_request`) is no longer on the live path; the
fixture below still drives it as its stand-in for 0x1AE040 states 3/5. It
goes when the fixtures drive the page route (WP-5 remainder).

The fixture's `elevator_state0_floor` scenario loads the host with
D_0081083A = 1 and = 0 over the manifest's 230 placement and asserts that
00827B10's state 0 leaves the drawn elevator Y (g.elev_pos[1], the input of
elevator_pose), the owner height, the three script heights and the Use
descriptor's height at 190/205/245 (lower) or 230/245/205 (upper), and that
a second state 0 on an owner that already armed faults.

## Fixture

The actual-asset sanitizer fixture links the native player pose host, raw
channels, model, collision, camera, scripts, inventory and status modules.
It checks panel refusal (156 ordinary callbacks), the first battery with
actual turn/camera settling (118), default-No/reselection/cancellation (170),
and successful discharge followed by panel power and release (285). Since
WP-6 the items are the host's own bindings: the fixture places the seven
instances before the load, runs each node's state 0 and the host's item
ticks every ordinary callback, and checks that the desired camera vectors
stay put during the battery's op00 sub8 settle; `other_take` runs the takes
of 0x0B04 (0x1E), 0x0B07 (key 0x32), 0x0B08 (0x10: the magazine bytes
C62/CB4 written directly) and 0x0B09 (the map, class 7 with its aura) to
their original requests and checks that the page each opens faults. The
discharge occupies61 status callbacks. These are fixture callback counts,
not claimed unassisted original playthrough timings. Direct owner claims,
the previous published list, ordinary camera evolution, GPU submission and
audio devices are explicit fixture boundaries.

Loading now requires the verified AREA11 sound bank and selects its area
remap only after all other host resources are bound. Teardown clears that
selection. Cue `3EE` is accepted as original silence; `3EF` uses the original
sample, pitch and voice gains. A missing cue fails the script callback.
The fixture checks selection, missing-bank cleanup and reload, while its
audio device remains a boundary. The separate actual-mixer and original
driver proofs are described in `AREA11_PANEL_SFX.md`; hardware interpolation,
reverb and scheduling remain unfinished.

Across status callbacks, raw pose channels, displayed palettes and panel/
elevator script state stay frozen. The final status frame remains consumed;
the shared recovery byte receives70 before ordinary camera updates resume.
Successful discharge updates charge, the panel's original controller bytes,
the persistent power flag and indicator lifetime. Native status exit clears
its separate draw pool after restoring projection; that clear must remain
valid after the UI camera context ends.

Panel alignment and facing are checked against original saved data. Pickup
camera sub8 updates only the actual target and then publishes DD980 depth
context; it leaves camera desired vectors unchanged. Elevator sub0 writes
both actual and desired vectors. After a pickup owner changes shared camera
fields, the scene must call `em_area11_interaction_host_camera_fields` before
a later panel/elevator callback imports camera fields from the native camera.

Whole-world teardown must detach player stage/Use hooks and unload/reset the
player pose before freeing owner tokens with host_clear. Ordinary completion
instead releases ownership in the next shared player callback. The fixture
also checks teardown during an acquired interaction followed by clean reload.

## Dennis cinematic face

The host owns an `EmPlayerFaceHost` prepared at load from the actual player
EMDL and `opening/player_face.*` (resource preparation only; the face stays
detached and consumes no RNG). `em_area11_interaction_host_face_attach` is the
`001B81D0` service: it attaches the face and writes player_ready2. It requires
the shared owner and ready1/2 and faults otherwise. The original's immediate
(`ev+0x14`) `001B82D0` branch can call `001B81D0` while `70003B8F` is still 0;
that pre-acquisition route is not modelled and fails loudly.
`em_area11_interaction_host_face_talk` is the direct `001FD950` ->
`001D06E0` event and is accepted only at ready2; it never touches the
activity mailbox.

With ready2 the shared runtime calls the host's cinematic player worker.
Like `00183090`, it ticks the face (`001D0C70` -> `001D0720`) before any body
work, then advances the deferred foreign bank, an active shared animation or
the ordinary source. The frame core's close (`001B82D0` op4/6) emits
RELEASE_SKELETON only at ready2; the host detaches the face (`001CA770`) and
the core writes ready1. `em_area11_interaction_host_player_record` returns the
alternate face mesh with the current player palette only while ready2.

The fixture's `cinematic_face` scenarios attach, talk, request the foreign
bank on the next callback (source0.5, 690.5 remaining), pause everything
through a status page, close with frame4, and release to default idle0 at 80.
A rejected face GPU update retains ownership and faults before the body is
bound. The status pause uses the actual pickup request, so it runs after
`first_battery` and keeps that acquired battery: the original pickup program
adds the item (`001C40B0`) before requesting status, and the real battery
page rejects a request with an empty inventory. Roger's live script, camera
and media are not driven by this fixture.

Since WP-4 the fixture keeps the host's canonical bytes in its own
EmSceneState: it stores the view after its direct owner claims, decays
D_008106EF at its camera stage and hands the panel's canonical request to the
runtime's own frame machine (`bridge_status_request`). The callback counts
are unchanged (156, 118, 170, 285). The live route is checked end to end by
`make test-level-smoke` against the route captures (LEVEL_SMOKE.md).

Run `make test-area11-interaction-host` for the AddressSanitizer and
UndefinedBehaviorSanitizer fixture. Existing original-instruction oracles
remain the evidence for individual numerical routines and controller rules;
this fixture checks that their native services work together.
