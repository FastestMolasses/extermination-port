# First-level fidelity audit (AREA11)

Date: 2026-09-22. Sources: a read-only audit of the live path in eight slices (orchestration, player, camera, UI, world, render, audio-media, startup-input, original-inventory); an adversarial check of every high-severity finding that was not already verified; and the results of three work-in-progress lanes (face-host, roger-media, status-hub-ui).

"First level" means: New Game, the AREA11 opening cinematic, first control, everything inside AREA11, and the transition out of it.

Rule used throughout: **a label is not evidence.** A module counts as *verified* only when an oracle that executes original ELF or overlay instructions, or a capture comparison, exercises it. *Live* means it is reached from `main` with no `EM_*` environment variable set. This file contains addresses and individual constants only. It contains no original data or disassembly.

---

## 1. Honest status

Most of the AREA11 live path is original-faithful and verified, **up to first control**:

- the boot/title state machine (decoded but not oracle-tested)
- the fade and letterbox machines (`em_fade`)
- the script interpreter (`em_script`)
- the opening cinematic: script 0x828FC0 actors, face and camera track; since WP-8 its lines run on the message service
- the snow weather and particles
- the AREA11 flame-effect visuals
- the point-light pool and the character lighting VU arithmetic
- the scalar player motor, heading and walk entry/stop source logic
- the collision box faces
- the pickup and prop indicator children

After first control, almost everything the player can interact with runs on **legacy stand-ins**, and many of them are fabricated:

- **Use handling and pickups:** three independent proximity scans (door, then pickup, then examine) and a 2-frame synchronous take. Since WP-5 the take posts the original status request (the battery pops up the ITEM/BATTERY page); the invented "Found: <NAME>" line is deleted.
- **Doors:** the legacy `em_door`, which walks the player at an invented 15 u/s and re-places the player at a synthesized point.
- **Status screen:** since WP-5 every AREA11 status screen runs the original page core: the cold entry, the original hub (`em_status_hub` with its 00209DF0 layout), the ITEM/BATTERY pages, the 0020E0C0 exit and the 0020A7A0 background with the original sinf. The music stops and resumes on the original's schedule, but the stream-channel volume 0x1999 the original sets is only reported (the port plays at full scale). The hub draws the original menu player and equipment models over the background and under its 2D layer (H6). The module-0x21 load runs the loader's own steps since chain C8b LOADER (10 dispatches at host speed, the original's 24 with the PS2 disc-drive timing switch; H7). The menu sounds are silent (WP-14).
- **Director beats:** a hand-written keyframe player that plays the wrong sounds (0x97/0x99).
- **Truck:** an invented truck fall.
- **Fan pair (records 1/2):** a constant-spin "decor prop".
- **Continue:** restores invented demo stats.

The oracle-verified replacements exist but **nothing in `src/` calls them**:

- AREA11 interaction host (panel, battery page, elevator, face, status runtime)
- original door runtime, program and transit
- original pickup owners
- Roger runtime and media
- cinematic playback
- status hub, draw and pages
- the shared Use arbiter

The consequences are serious:

- The level **cannot be progressed**. Nothing live sets terminal power, so the elevator always refuses.
- The **Roger encounter never happens** after the opening.
- There is **no exit from AREA11**.
- The original player pose source is **permanently invalidated** after the first aim, door, examine or hit.

Presentation also has confirmed global errors:

- no AREA11 world fog (record near -209, far 304, RGB 48)
- many actor models baked with an invented stand-in light
- every legacy SFX about 7.4 semitones sharp

Summary: the *pieces* are largely verified; the *wiring* and the *live scene coordinator* are the missing work, and several live fabrications must be removed.

**Status update (2026-09-22, after WP-0..WP-2 landed):** the truck fall, the fan spin, the director sounds, the Continue demo stats, the missing fog and the permanent pose invalidation above are fixed (see the WP-0..WP-2 status lines in §4). The invented fan spin is removed, so the fan pair is drawn static. The 00827630 init rot.z (±π/4 by record +0x03) is implemented in em_pickup, but it is applied only once the manifest pickup lines carry `owner 0x827630 <flags2>`. That is PENDING: the local manifest (assets/scene_snow/scene.txt) has not been updated, so the running port still draws both fans with placement yaw only. The wiring gaps (no terminal power, no Roger encounter, no exit, legacy Use/door/status paths) are still open.

**Status update (2026-09-23, WP-4):** the AREA11 interaction host is live: the panel sets the power bit through its original script and BATTERY page, the terminal refuses without it and rides the elevator down with it, and the level smoke matches the refusal and the ride row for row against route captures 02 and 04, and the panel against 03 in two windows (the prompt window, between the request and the Yes press, is not compared, and the player/camera Y is checked as retained/offset; WP-5 and the floor lanes). The §2 graph below predates it: em_area11_interaction_host is now called (w_001B6990, pool nodes #26/#27, the Use and stage hooks, step F), and the examine terminal and elevator_tick are gone.

**Status update (2026-09-23, WP-5):** every AREA11 status screen runs the original page core (em_status_page in the host's em_status_runtime) with its 0020E0C0 exit; the START/TRIANGLE hub is the original `em_status_hub` + `em_status_hub_ui` with the translated 0020A7A0 (its sine the original 0011E2A8) and the translated status models (`em_status_models`: the menu player and the equipment letter models, drawn between the background and the 2D layer; the pool and all 27 node matrices bit-exact against the status-hub capture); the battery pickup pops up the ITEM/BATTERY acquisition notice on its 001C47A0 request; the music stops and resumes on the original's schedule (its 0x1999 stream volume is only reported, H22); the invented status pages, the Found line and the hover cue are deleted. Open: the module-load wait (H7).

**Status update (2026-09-23, WP-8, PARTIAL):** one message service is live at main-loop step F (001FCA10, `em_message_live` over `em_message_service`): the opening's line 0x66, the panel's 0x80000018 and the terminal's 0x8000001A run on it, with the original glyph layout and passes (001FD950 draw prefix, 001FE070, 001FC7B0, 001CC1E0, 001CBE10, 001CC3B0; drawn through the port's glyph atlas) in place of the `em_hud_subtitle` approximation; the opening's stream hold `D_008106F4` follows the original protocol (001FD4C0 sets 2, the lane stand-in's prefill 1, the stream row's release 0), and the opening actors' talk comes from the activity bytes 001BA580 consumes. `em_panel_message`, the opening's own dialogue clock and the `.emod` assets are deleted. Open: the stream lanes (`em_stream_lanes_original`) are not live (their initial state 001F9820, the IOP status words and an 001157F0 sink are missing; docs/STREAM_LANES.md), so voiced lines fault; the mode-3/4 presenters are untranslated; the director (WP-10), Roger (WP-9) and the legacy door (WP-7) do not post into it yet.

**Status update (2026-09-23, census L01, PARTIAL):** the original player stage is live from first control: every stage runs 0015BA50 (begin, the switch with the display's 001C64F0, end), 0015B130 (0021C440, the port's idle/walk callbacks as its state[0]/[1], the +20E countdown, 0015D100, 0015D000) and 0015BCF0's tail (+BC, the -200 check into 0015D460 with the live 001AEDE0, the +31B loop-sound stop), bound by `em_player_stage_live.c` with D_00248C98 from the local export. em_player_damage.c's copies of 0021C440 / 0015D100, its +20E countdown and its kill plane are deleted (the legacy bug-latch struggle remains). The player's vitals are a per-stage view of the port's storage. D_0081083C is canonical (D2). The newgame-control run (9.599989), its frame trace and the level smoke's tick log are unchanged. Open: the takeover is still the interaction runtime's (its acquire / tick / release stand in for 00174A50 + 00182D70, the +4 = 4 commit and 00182DF0); on the port's idle/walk under 0x70003B8D without that owner the prelude does not run (00174A50's 0017B490, L12); the hit, infection and low-health paths reach fail-stop workers (rumble 001B61C0, the effect manager, 0015C9D0, the +20 / +1C objects) and unbound +4 = 2 states; 0015CF90 has no oracle; the canonical B3 byte is still the stand-in expression. Census: 10 of the lane's 15 rows are live (0015BCF0 tail only); 00182B30, 00182D70, 0015B530 and 001837A0 are bound but unreached while the takeover is the interaction runtime's, so they stay verified-unbound. Outside AREA11 a port enemy hit now faults in the unbound +4 = 2 states where the legacy flinch played (off the route; L02 restores it).

**Status update (2026-09-24, census L05..L08, PARTIAL; L05 BLOCKED):** AREA11 has one original collision world (`em_collision_world.c`), built at every area build (w_001AFCA0): the cell directory (`area11_cells.bin`, now a required export), the installed flags-7 EMCL (node class and rank section; the port's own collision reads the same file and its runs are unchanged), the walkers' one scratchpad state and the SDK context. The panel, terminal and item owners publish their pool records through the one 001B17A0 (`em_owner_services_001B17A0`; the host's duplicate offer is deleted) and 001B1B70 into the world's class lists; the terminal and the items re-transform their cells with 001A2370 at the original positions (the terminal's completion hook 0x827E54 is new in em_elevator.c and oracle-checked), and the live directory equals route captures 00 and 04 byte for byte (`make test-collision-world-capture`). 001AAD00 runs its nine list passes and its list block; its interactive list is the Use scan's one store. The scripted camera retarget's 0019A910 / 0019B7D0 and the item ray run the translated walkers (the refusal camera fixture's overhead point is now exact). The FLOOR mechanism has its collision workers bound (0019AB20, 0019B6C0, 0019B8C0, 00175640, 0019BC40) but stays gated. Open: the player's movement probes keep em_collision.c because 0019CB60 and 001A6440 have no translation (the census had them as verified-unbound: the oracle hooks the originals); the crates, drums, truck, prop and 0x825940 do not publish their cells (L25/L23/L35/L24), so the original walkers do not see them yet (the port's own walker never saw the crates either); the follow camera (L13) keeps em_collision.c; the list passes have no original-layout record memory (fail-stop; the port's class-1/2/0xD lists are empty); em_actor_collision.c's query half (0019AB20, 0019BC40) still uses the truncating float helpers and a second copy of the prim tests of em_coll_probe_original (whose 001A4030 runs live), so it stays out of the live path (001764E0's 001760C0 column keeps em_collision.c) until it is harmonized with its oracle (EE_FLOAT_MODEL.md 5c). Newgame-control (9.599989) and its frame trace, and the level smoke's tick log, are byte-identical to the pre-step build.

**Status update (2026-09-24, census L02 + L25, BLOCKED):** FLOOR (the floor service 00175900, the fall check 001796C0 and their state closure) and the original crate/drum owners are still not live; nothing changed on the live path. The blockers, each a missing worker or datum rather than a stand-in to add: (1) met since the display step (2026-09-24): the player's pose has one owner, its own record worked by em_pose_host_workers over the whole clip bank (chains and hold frames included), and the display draws that record for every stage a translated routine owns (`player_states_bind_display(1)`; status update below); (2) met since the soft-float step (2026-09-24): SDK atan2f / sqrtf in the floor service are bound over the collision world's SDK context, which now carries the soft-float workers (em_sdk_soft_float over D_0024295C and the errno word from the user's export `assets/sdk_soft_float.emsf`; the SDK_MATH_ORIGINAL.md 7 gate is lifted); (3) the closure's callbacks (em_player_fall, _hang, _recovery, _ladder_climb, _closure_0e_18, _closure_10_12_19, _weapon_states_a/_b, _major2, the reaction and slide adapters) are unbound (since the one-owner step, 2026-09-24, every original has one translation: 0021D250 / 0021D2E0 / 00179880 in em_player_fall, 00180420 / 00174AB0 / 0017FC80 in em_player_ladder_climb, 00180300 in em_player_ladder_entry, the others reaching them through bridges; the record-level 00174AC0 em_player_heading_record is every module's `heading` worker, proven bound in the locomotion display and fall oracles; the live turn stays on mirrors until 001612D0 runs over the record); (4) the crates and drums need an AREA11 world model bank for 001B0EA0, a decision on the 001CAA00 draw, the harmonized 0019AB20 query half (ACTOR_COLLISION.md 7 item 4), the legacy group split with L24 and exports of D_002468B0 / D_00246A00 / D_00246A10 (CRATES_DRUMS_ORIGINAL.md "Status"). Done in the step: 00175CF0's tanf 0011E398 and atanf 0011DBB8 are bound into the gated FLOOR over the collision world's SDK context, with its fault word as a new `EmPlayerStatesBinding.sdk_fault` latch that the floor service and the fall check test; the floor slot labelled `cosine` is `tangent` (0011E398 is tanf), and the floor oracle's hook of it is host tanf on both sides. Newgame-control (9.599989) and the level smoke are unchanged.

**Status update (2026-09-24, display step):** the player's clip clock, node channels and skeleton live in the player record (`em_player_record_pose` over `em_pose_host_workers`, PLAYER_CLIPS.md section 6): the raw bank of all 459 clips at +40 (0xD689C0), 21 node records at the +110 words, D_00248C90's +0 column, and 001749A0 / 001749F0 / 001C61D0 / 001C64F0 (its chain step included) / 001C63E0 / 001C6DA0 / 001C68C0 on those bytes. `em_player_pose_host.c` keeps its API over the record; `em_player_pose` is no longer the player's (it still poses Roger, the status models and the unreached cinematic bank). 0015BCF0's animate step runs after every stage; takeover and translated-state stages display the record's node matrices; a non-idle/walk stage advances the record by +34; the stage's clip workers are the record's; `player_states_bind_display(1)` is declared, so FLOOR now lacks only its state callbacks (the closure binder). Evidence: `test_player_record_pose_reference.py` (the live module against the original over the captured records: every first-level clip's first frames, both chains into 0x5F / 0x72, the captured skeletons re-evaluated byte for byte), the first-control pose trace (56 callbacks exact), newgame-control 9.599989, the level smoke's six live phases. The port's idle/walk callbacks keep their legacy baked display (L12); the smoke's tick log moves by float ulps only in the battery walk, from one foot-stop begin whose feet are now EE-exact.

**Status update (2026-09-24, census L02 second attempt, BLOCKED):** FLOOR stays gated and nothing changed on the live path. The closure binder was not written: the closure calls the move walkers 0019AD00 / 0019AFE0 directly, and on ordinary AREA11 play (0017D080 from the fall's edge test on every walk-off below running speed, 0016C570 / 001791D0 on the hill slide of route 06) with masks that reach their untranslated grid pass 0019CB60 and hull lock 001A6440 (census L05); the slide also calls 001EFD90, which has no live effect owner (L26). A stand-in there is not allowed and fail-stop workers would quit where the legacy fall and collide-and-slide play on. FIRST_CONTROL.md "Missing today" holds the worker-slot inventory (001755B0 is the only other untranslated worker ordinary play reaches; the rest need off-route states first). Fixed: EmPlayerLandWorkers carried 00128350's double as an int, dropping its high word before 001000E0; the slots are 64-bit now and test_player_fall_reference compares the whole register.

**Status update (2026-09-24, Boxes: FLOOR, crates / drums, Use chain; live):**
- **FLOOR** (00175900, 001796C0 and the whole state closure) and the **Use
  chain** (00160220, 001798D0, 0017C440, 0015DF10, the climb / vault /
  ladder / running-jump states) run on the live player record in AREA11
  (em_player_closure_live.c; FIRST_CONTROL.md "Engaged").
- The **crates and drums** run their original owners (em_area11_boxes.c;
  CRATES_DRUMS_ORIGINAL.md "Binding"). Their legacy em_enemy copies are
  not placed in AREA11, and the drum kind is deleted.
- The collision query half, the floor module and the box owners are on the
  measured EE float model with their oracles.
- The level smoke passes seven live phases:
  - `boxes` reproduces route 05's two ledge climbs row for row (+5, +1F0,
    clip, clock, ground, heading, Y);
  - the players' post-release Y equals the captures (the port re-grounds as
    the original does).
- The six box records equal route 04 byte for byte in their modelled
  spans.
- **Open:**
  - the boxes' damage paths and effects are fail-stop, because no live code
    writes +0x36 (L26 for the effects);
  - the port's own idle / walk callbacks remain until L12;
  - the scripted takeover is still the interaction runtime's stand-in;
  - the box draw is the legacy EMDL at the original matrix (the object
    kernel is RENDER's).

**Status update (2026-09-24, Effects step: packet chain, fog; effects BLOCKED):**
- **Live:** the Metal world fog's coefficients come from the one
  translation of 0021B920 (`em_fog_gs_coefficients` calls
  `em_packet_chain_0021B920`; the host-binary32 copy is gone). The module
  and em_status_ui_leftovers are in COMMON. test_area11_fog_reference
  checks the helper against the EE model (2,006 pairs) and every in-scope
  route beat. The frame order and the newgame-control run are unchanged.
- **Translated:** the puff handlers' 001CFB50 and 001D0540
  (em_effect_kinds). The captured transform block of beats 05, 08 and 12
  is reproduced byte for byte (EFFECT_KINDS.md 2.1a).
- **Blocked:** the effect manager, spawn chain, driver, handlers, head
  sprite and equipment sprite stay unwired. Every one of their draws reads
  the render-context views: the +0x2240 / +0x22C0 clip matrices, the
  0x70003AC0 / 0x70003A40 matrices, the +0xA0 fog block and the packet
  cursor. The port has no canonical render-context block, and no live code
  writes them (EFFECT_MANAGER.md 5.0).
  - The footstep, climb and slide puffs therefore stay the counted gap
    `player_effect_gap`.
  - L32 / L30 must come first: one render-context owner and 001D2960 over
    the live view. That owner also replaces em_snow_projection's private
    001D2960 copy.

**Status update (2026-09-24, census L03: the hill slide; live):**
- The **slope slide** 0016C6A0 (with 0016C520, 0016C570, 0016CD70,
  0017F5F0, 00174FD0, 001791D0 and 00224B80) runs on the live record in
  AREA11: 00175CF0 meets the hill's authored class-0x1000 nodes, 001796C0
  enters state 0x1C (em_player_closure_live.c; PLAYER_CLIMB_SLIDE.md
  section 6). Its last fail-stop on the route, 00182430 as a standalone
  worker, is bound to the footstep's one translation
  (em_player_step_sounds); em_player.c's own copy of that mapper is
  retired.
- The hand-back from a translated state to the port's idle keeps the
  record's +1F1 (00161020 case 0 and 0017C030 do not write it); route 06
  f181.. shows +1F1 = 1 on idle.
- The level smoke passes eight live phases: `slide` reproduces route 06
  from the entry (f72) through the landing (f138), the skid-out, the
  hand-back (f181) and 12 idle rows (+5, +1F0, +1F1, clip, clock, ground
  exact; the heading's authored values in order, crossings within one row;
  per-row motion within 0.0025; LEVEL_SMOKE.md "slide").
- **Open:** the slide's puffs are the counted effect gap (L26); its loop
  sound 0x12E and the step/landing ids are not in the exported registry
  (WP-14), so +31B stays -1 and 0016CD70 re-requests 0x12E each tick; the
  entry follows the port's legacy walk (L12), steered since census L13..L16
  against the live camera's forward: it enters 0.86 from the original's
  entry, and check_slide aligns on the landing and allows two rows for the
  crossings and the landing, scaled to the entry offset (one row up to 0.6,
  two up to 0.9; LEVEL_SMOKE.md "slide"; pending lead review, one row
  returns with L12).

**Status update (2026-09-24, census L23 / L19: the truck set piece live; L21
blocked):**
- The **truck 00823FF0** and its **camera trigger 008251E0** run on their
  pool nodes (em_area11_boxes; TRUCK_ORIGINAL.md "Binding"): the model bank,
  bone slot, placement, hull cell (uid 14, published class 4, so the floor
  service stands the player on the truck record), the draw, the rumble
  (001B1E20 on the new pad block D_00810E40, em_pad_actuator) and the
  counted effect gap. The legacy static `em_truck.c` is deleted (H16).
- The **AREA11 script host** is live (em_area11_script_host; AREA_SCRIPT.md
  6.1): em_area_script runs 0x8292C0 over the canonical storage, with the
  interaction host's frame-event bindings, the pose host's 00182F90 and the
  one bound SDK sine. The scripted takeover writes 0015B130's admission
  (+5 = 0, +1F0 = 0x41) and 00182DF0's release tail on the record.
- **Main-loop step I** (001B5B70) runs every frame (ORCH-22).
- The level smoke passes ten live phases: `truck_preview` equals route 07
  row for row from the script's frame through the release and 25 rows
  (camera shots, placement, heading, bars, D_00810792, the player record);
  `truck_crossing` equals route 08's truck record from the arm through the
  rest (LEVEL_SMOKE.md).
- **Since WP-8b (2026-09-25):** the director 008253F0 (L21, WP-10) runs
  on its original owner and scripts, with Roger's alternate 0x828990 and the
  voiced lines on the stream lanes (DIRECTOR_ORIGINAL.md section 6).
- **Open:** the truck's effects (L26) and sounds 0x454 / 0x455 (WP-14).

**Status update (2026-09-25, census L12 + L33: the idle / walk states live):**
- 0015B130's state[0] / state[1] are the original **00161020 / 001612D0**
  over the player record in AREA11 (em_player_closure_live.c `bind_loco`;
  LOCOMOTION_DISPLAY.md section 4), with the gait display 0017C030 /
  0017B660 (001C9D50 per node) / 0017B5C0 / 00179D20 / 00179FF0, the
  record-level **0017BC40** and **0017B910** (new; their oracle
  test_player_loco_workers_reference), and **00187350** on the record after
  every player stage. The display is the record's evaluated pose.
- **Against the original:** `make test-first-control-reference` (new): 56
  first-control callbacks of the record equal the original's actor bytes;
  newgame-control travels 9.599849 and the stop fixture 18.649738, both the
  original's; the level smoke's 15 live phases PASS with the translated walk
  navigating every beat; frame order unchanged.
- **Retired:** em_player_reversal and its glue, host test and oracle (a
  second, partial translation; H11 is now this lane's), the mirror 00187350
  dispatcher; the legacy idle cycle, eight-tick entry, gait blend, stop /
  re-entry metadata and step clock no longer run in AREA11 (they stay for the
  scenes without an original world).
- **Open:** the port's stand-ins still pre-empt the idle/walk states with the
  legacy callbacks and display: the door sequence (WP-7 / L18), the examine
  lock (the director's lock is gone since WP-8b), the armed stances, R2 and melee (P24..P28,
  L28). The skid's and the footstep's effects are the counted gap (L26).

**Status update (2026-09-25, census L26 / L27 / L28 / L39: effects,
equipment and head sprite live):**
- With the render context live, `em_effects_live` binds the effect
  translations over it (EFFECT_MANAGER.md section 8): the barrel 001F0360
  (both world variants), 001F0310 at the area build, the spawns 001EF9D0 /
  001EFD90 / 001EFD20 / 001F0460 (every player-side spawn with the record's
  whole +0xC0 quadword, the truck's 32, the crates', drums' and weather
  node's), the driver 001EA240 and the handlers 001EC1F0 / 001EC3F0 /
  001EC470 / 001EBF10 on pool nodes, 001CFBE0 into em_packet_chain_original,
  the head sprites 001F0120 / 001E2560 (the player's 0x3B, Roger's 0x47),
  the pickup glint 001F0A60 and the glow markers' 001CD520. `em_equipment_live`
  binds the seven 0018A6B0 nodes (0018A8D0 and the flavour bodies,
  PLAYER_EQUIPMENT.md section 8) and the equipment change's 0015C310(p, 1).
  The snow's and the AREA11 effect's fog come from 0021B9A0 on the context.
- **Against the original:** the level smoke's check_effects (new) compares
  the effect, head-sprite and equipment nodes and the barrel's lane packets
  with routes 08, 10, 11, 12, 13 and 14 (route 08's eight truck puffs bit
  for bit, route 12's four player footstep puffs; packet 4 and route 10's
  glow-marker primitives where the camera equals the capture's); every
  barrel frame emits 11 markers and 6 lanes and the route counts no effect
  gap; newgame-control 9.599849 and the frame order unchanged.
- **Retired:** em_player.c `player_effect_gap`, the truck's effect gap, the
  bindings' own 001EF9D0 copy, the no-op aura draw, the snow's and the AREA11
  effect's host fog coefficients.
- **Open:** no renderer stage draws the effect chains (done by WP-13,
  2026-09-26: the chain page is drawn, docs/CHAIN_PAGE.md); the
  equipment's own 001CAA00 draw (done by the player step, 2026-09-26: the
  nodes draw their original units); the skid's two
  untranslated handlers 001EAD70 / 001EC270 (reachable off the route,
  checked to write nothing but their packets) are the binder's counted gap;
  every other untranslated handler and 001EFE00 fault (none is reachable in
  AREA11 in the port; EFFECT_MANAGER.md 8.2).

**Status update (2026-09-26, full-route smoke and census re-classification):**
- At port HEAD 097fbd9 the level smoke plays route beats 01..14 on the main
  line (18 phases) and the side beats 00 and 09 in their own runs, and every
  phase reproduces its capture. `make test-level-smoke-full` now requires
  that: the checker's `--require-through` fails the target when a phase the
  run had to play is NOT-LIVE, driven or not reached, instead of passing
  with a shorter route (LEVEL_SMOKE.md "Running it").
- The checks that are still relaxed are listed in LEVEL_SMOKE.md "What the
  full route does not yet compare". Each has a named cause and remover: the
  module-0x21 load (H7), beat 0's voiced-line teardown (2 rows: the music
  refill's phase, navigation; since the measured drive model of 2026-09-27
  below, the drive's latency is no longer a cause), Roger's
  idle phase before f358 (navigation), the slide / step-off stance
  (navigation), the terminal's 0x827E6C copy, lane 3's parameters, and the
  opening's post-step. (The point-light sway is compared since the rand()
  order audit of 2026-09-27, below.)
- Census (FIRST_LEVEL_CENSUS.md 1.22): liveness was measured again over the
  whole route. Live 640 of 733 non-boundary functions (90.8% by
  instructions), verified-unbound 87, unverified 5, missing 1, stand-in 0.
  Six rows were upgraded to live: 00102948, 0011D878, 001AF890, 001B1380,
  001B6F80 and 001C6150. 00187DC0 moved to unverified: its live handler is
  a copy that no oracle executes.
- Frame order: the reported idle04 / walk04 failure at HEAD is an alignment
  artefact of running the comparator without `--native-index`. On a
  post-control window every frame passes (LEVEL_SMOKE.md "Frame order").
- The stream / voice capture this update asked for was recorded (the
  decomp's CAPTURES_C7.md section 1) and is used by the drive model below.

**Status update (2026-09-26, the player step: the player and its equipment
on the object-unit draw; OWNER_DRAW.md section 10, census 1.23):**
- The player's +0x4C (0015C160, after its shadow) and the seven equipment
  nodes' +0x4C (0018A6B0, in the walk) run the original 001CAA00 through
  `em_owner_draw_live`; the renderer draws the units' triangles as for the
  crates. The player's model is exported (`tools/export_player_model.py`,
  0x00D1C1C0, checked against RAM in 16 captures) and the object textures
  now include the player's and the equipment's (291 TEX0, resident in all
  15 route captures). The record's draw fields come from their original
  writers, now live: 0015C1F0's model bind (+0x44, +0x4C, +0x0C) and
  001AF5C0's wipe (+0x02 bit 0x20, +0x80.., +0x94 = -1).
- The legacy player mesh no longer draws as the player's +0x4C from the
  hand-off on, and no longer carries the equipment there; it remains for
  the opening's 1,302 reported post-steps (design risk 2) and the status
  screen's menu player. em_weapon reads the hand node from the record; the
  gfx bone publish (`em_gfx_last_skinned_bone`) is retired.
- Evidence: every triangle of the 15 captured player units and 105
  equipment units equals the original microcode's
  (test_object_unit_reference); the native 001CAA00 chain, with the native
  001D89D0's camera fill, equals the original unit byte for byte over the
  same 120 owner-frames (test_actor_light_001d89d0_reference C); live, all
  16 owners of the camera-exact snapshots 10 and 14, the player and its
  equipment included, equal the original's unit bytes, clip pass, position
  rows and colour / rig lanes (check_owner_units). newgame-control
  9.599849, compare_frame_order idle04 / walk04 / st03 / cut02 / cut15 PASS.
- Still open: the opening's player on the record pose (design risk 2);
  Roger's face units (001CB3C0); the legacy-drawn owners; the other player
  model kinds (0x3D..0x40) and equipment 0x36's TEX0 0, which fault if
  reached (not on the route; variant 4 is not reachable in AREA11 at all,
  PLAYER_EQUIPMENT.md section 7). The smoke now fails if the player's or an
  equipment node's live unit differs in the camera-exact snapshots 10 / 14,
  and if the walk's and the post-step's player draw gate disagree in any
  tick (LEVEL_SMOKE.md).

**Status update (2026-09-26, WP-13: the chain page D_007635C0 drawn;
docs/CHAIN_PAGE.md, census 1.24):**
- One consumer, `em_chain_page_live`, walks the page 001CB800 splices at
  every frame close exactly as the DMA sends it (CNT / NEXT / REF / CALL /
  RET, tags not transferred), runs its VIF1 codes, the two VU1 programs it
  CALLs (translated from their microcode: the lane program of D_00233290 and
  the sprite program of table 0x231770, `em_vu1_page_programs.h`) and its
  GIF packets, and `em_gfx_gs_prims` draws every primitive in GS order with
  the GS pixel path of the states the pages hold. Live: the effect puffs and
  the head sprites' breath (sprites), the pickup glint (line strips), the
  glow markers and equipment sprites, the ring lanes (no route slot is
  active: nothing drawn, as in the captures) and the 0015BF90 decal. The
  blend presets the page REFs come from 001D0F20's bank, translated
  (`em_gs_blocks_original`). Retired: the dedicated decal entry
  (`em_gfx_shadow_decal_fan` / `_texture`, `em_shadow_live_flush_decal`),
  `export_shadow_decal_texture.py` (replaced by `export_page_textures.py`)
  and the decal test's Metal part (moved to `test_chain_page_gpu.py`).
- Evidence: every MSCAL of the 15 captured pages and 600 + 600 synthetic
  batches equal the ORIGINAL microcode (registers, data memory, kicked
  packets), the captured pages' 386 primitives equal the original walk's,
  and the timing premise of the translation is asserted on every run
  (test_chain_page_reference); the GS pixel path against a GS pixel model
  (test_chain_page_gpu); live, 40 sampled pages of the full route re-walked
  with the original microcode over the port's own bytes draw the port's
  primitives, and in the camera-exact snapshot 10 the five glow markers
  drawn equal the capture page's (check_chain_page). newgame-control
  9.599849, compare_frame_order idle04 / walk04 / st03 / cut02 / cut15 PASS.
- Still open: 001DDE10's four-sprite frame-copy pass is walked over, not
  drawn (it samples the frame buffer); the first sprite of a page whose
  RGBAQ precedes its ST takes the frame's GS Q, drawn as 1.0 (about 1.5
  vertices a page); the AREA11 flame and the snow still draw outside the
  page (008235F0's 001D04B0 and the weather's 001E0D70 kick are not bound;
  the flame's `em_effect_sprite_project` remains a second, partial
  translation of the sprite program until that owner is bound); Metal's
  rasterization stands for the GS DDA.

**Status update (2026-09-26, the owners step: the remaining world owners on
their records; OWNER_DRAW.md section 10, census 1.25):**
- The terminal / elevator 00827B10, the panel 00159210, the placed prop
  001C4820 (bound live: em_sul_001C4820 at its node), the six items 00219550
  and the map item 0015AFA0, and the opening controller 00823E80's parachute
  canopy bind their models (001B0FD0 / 001B1020 over the world bank or the
  D_0028A56C library), place their nodes (001C6380 into their records' bone
  slots: the terminal's state 0, every carry tick and the completion) and
  draw through their +0x4C, 001CAA00 over their own records
  (`em_area11_boxes_owner_*`, the object-unit renderer). The terminal's
  per-frame copy of its node 0 into its indicator child's slot (0x827E6C) is
  bound. The canopy's state 1 publishes its record (001B1B70) as the
  original does, and the prop's 001B17A0 publishes its cell: the published
  class-4 list at beat 04 now equals the original's exactly.
- Retired: the legacy elevator platform mesh (`g.elev_*`, `elevator_pose`;
  the manifest's `elevator` line is no longer read), the legacy panel mesh
  (the `grate` line installs only cell 18), the bound items' legacy instance
  draw, and the canopy / record-20 prop instances (retired at their owners'
  placement when the owners bind). The indicator children's stand-in draw now
  sits at the child's own node (its slot), not the parent's legacy palette.
  `tools/export_object_textures.py` adds the items' model 0x72 (303 TEX0,
  resident in all 15 route captures): users re-run it.
- Evidence: check_owner_units compares the new owners' units with the
  snapshots (in the camera-exact beats 10 and 14 every owner that ran its
  +0x4C, the two visible items included, equals the original's unit bytes,
  clip pass and position rows); check_indicator_children holds the terminal
  child's slot equal to the terminal's node on every tick and the terminal
  record equal to the captures before and after the ride (the relaxed
  "0x827E6C" row of LEVEL_SMOKE.md is gone); the elevator phase compares the
  terminal record's +0x04 and +0xB0..+0xB8 row for row over route 04;
  test_collision_world_capture's published list gained uids 17 and 3;
  test_pickup_owner_reference now executes the items' state 0 (0015AC00 and
  00219550's), which moved 0015AC00 to live (census 644 / 84 / 4 / 0 / 1)
  and found the decomp's NEARMISS C of 00219550 inverting a test.
  newgame-control 9.599849, compare_frame_order idle04 / walk04 / st03 /
  cut02 / cut15 PASS.
- Still open: the fan pair and the security gun with its cable (census L24,
  then called "the husks": their owners were not bound, so their legacy
  meshes drew; done 2026-09-28, status update below); the indicator children's own draw 001CABA0 (channel 3, the depth
  sort into the chain page and the class-3 GS state: still the additive mesh
  stand-in); Roger's face units (001CB3C0).

**Status update (2026-09-26, chain C7: the scripted takeover on the player
stage; PLAYER_STAGE_WORKERS.md 2.1, census 1.26):**
- A script owner's frame (the truck trigger, the fence door, the director,
  Roger) is the player stage's own takeover, as in the original: 0015B130's
  prelude admits the player (00182B30, +4 = 4, 00174A50(8.0), 00182D70),
  each stage runs 0015BA50's +4 = 4 path (00183090 with the face's 001D0C70,
  the +1F4 advance) and 0015B530 (001837A0), and 0015B530's 00182DF0
  releases the player. The interaction host keeps only a staged token for
  these owners. 00182DF0 is one translation (em_player_stage_00182DF0), also
  used by the runtime's release of the panel, the terminal and the items; the
  pose host's approximated releases and player_pose_commit_tick are gone.
  The pool free's 001AF800 pushes its slots in its own loop
  (em_roger_actor_001AF800) for every AREA11 binder instead of calling
  001AF890 per slot. The floor service's first contact runs
  em_player_first_contact (00187DC0 / 00187EA0), executed by the floor oracle.
  00182D40 and 00174AB0 each have one translation (em_player_00182D40,
  em_player_ladder_climb's).
- Evidence: the full route and both side runs pass with --require-through;
  the new check_stage_takeover holds +4 = 4 from the admission to the
  release in routes 07, 09, 10, 11, 13 and 14 while the phases' row-for-row
  comparisons are unchanged; the tick log equals the previous build's on all
  13,017 ticks apart from +4 during the takeovers.
  test_player_stage_workers_reference (00182DF0), test_player_cinematic_reference
  (the stage composition and 00182DF0 over bank 0x96),
  test_roger_actor_original_reference (001AF800) and test_player_floor_reference
  (00187DC0) execute the originals. newgame-control 9.599849. Census: live
  648, verified-unbound 81, unverified 3, missing 1 (91.3% by instructions).
- Not done (blocked): the player's 001CA770 (001B82D0 sub 4) and 001CA700
  (001B81D0) on the record's +0x90. The port's player face is the face
  host's state, not a pool slot; putting it in a slot at +0x90 makes the
  player's 001CAA00 reach the attachment draw 001CB3C0, which is not
  translated (em_owner_draw_live faults on it). 001CA770 therefore stays the
  face host's detach and 001AF890 is verified-unbound (reached in the port
  only through Roger's 001CA770, not on the route). Needs: Roger's face units
  (001CB3C0 with 001D3F50 / 001D3E40, 001C7900, 001CB2C0), then the face
  state in the slot.
- Still the interaction runtime's: the panel, the terminal and the items'
  takeovers (their scripts request clips through em_interaction_animation,
  not +1F2 / 00183090); +4 stays 1 there.

**Status update (2026-09-27, chain C7: main-loop steps V / W, 001D1EF0, the
(3, 1) registrations; RENDER_CONTEXT.md section 9, census 1.27):**
- Main-loop step V 001D2300 runs in every iteration on the render context
  (em_frame_kick; em_frame_step calls it before presenting): the frame's main
  list with the slot's half-pixel offsets, the clear it selects (Z only, or
  black under render flag 3, which it clears), 001E0DF0 and the NEXT chain;
  its hardware kick is the renderer's presentation. Step W 001D2580 stores
  the field; the port's field model is one field per iteration in the
  route's phase (field = D_00810E80 at step V). 0x70003B70 / 72 are 001AB370's
  0x800 / 0x800.
- With flag 3 cleared by step V, the status frame's, the task chain's and
  the load veil's 001D2830(3, 1) run on the context (UM_001D2830 removed) and
  001D1EF0 is bound from the area build on (states 0 and 5, 001ADF00,
  001AD4E0; its kick's page drawn). Before the area bind (the New Game
  bring-up) 001D1EF0 stays reported.
- One storage each: the point-light pool is the context's +0x210..+0x221F
  (`g.point_lights` removed; the owner draw views +0x220 directly), and the
  status pages write 0020DFA0's UI view into the camera pool's D_00810610
  (em_status_models' copy removed), so the status frames' 001D1C50 projects
  the UI view and state 5's 0018C0D0 rebuilds the world view, as in the
  original. The renderer's background gate reads step V's own gate code.
- Evidence: test_render_context_live_reference (steps V / W, a status frame
  with 001D2830(3, 1), a tear-down frame, both gate branches, against the
  original instructions; the list address of the kick); check_render_context
  over the full route and both side runs (the field phase on every tick,
  5,368 world lists equal the route snapshots', 412 status frames equal the
  two status captures in flag words 0x0B / 0x03, fog, save slot, main list
  with the black clear, and the UI view). compare_frame_order idle04 /
  walk04 / st03 (--native-index 1330), cut02 and cut15 PASS. newgame-control
  9.599849. Census: live 659, verified-unbound 78, unverified 3, missing 1,
  boundary 443 (91.5% by instructions).
- Limits (RENDER_CONTEXT.md 9.4): the draw environments' bodies (boot bank
  A) are not modelled, so step V's XYOFFSET in the live port is computed from
  a zero SCISSOR (unconsumed DMA bytes); the +0x1D8 channel-3 list is not
  built (001C1D00), so a world list has no CALL there; the renderer still
  clears the colour at every frame begin (the original keeps it in a world
  frame without the background, which the route never shows); the movie
  frame's flag 4 (001D1C10, step N) is not set.

**Status update (2026-09-27, chain C8: duplicate translations reduced to
one bound owner; docs/SDK_VU0.md, census 1.28):**
- **SDK VU0 leaves.** 001026D0, 00102900 and 00102948 each have one
  translation, the header-only em_sdk_vu0.h, checked by the new
  test_sdk_vu0_reference against the original instructions. Every module
  that carried a copy calls it:
  - the shadow route, the locomotion display, the equipment, the effect
    manager, the actor light, the camera leftovers, the status models, the
    frame render heads and em_shadow_original;
  - em_shadow_original's product now runs on the measured VU0 model instead
    of a host-double truncation. The full shadow sweep and check_shadow pass.
- **0019F330 (census live).** It runs its one translation
  (em_coll_list_passes_walkers) as 0019BC40 pass 2's worker, over the
  collision world's scratchpad state. em_collision.c's column_node is gone.
- **One owner each for the rest:**
  - 001C6150: the status pages' and indicator children's byte reads call
    em_owner_services_001C6150;
  - 001BC240 / 001BC290: em_door_original's phases 4 / 5 (em_sdf_ copies
    removed, their cases moved to test_door_original_reference);
  - 001D4B50 / 001DA1E0 / 001DA290: the shadow passes (em_rvr_ packet
    builders removed).
- **Evidence.** All make test-* pass. test-level-smoke-full passes with
  --require-through. newgame-control 9.599849. Census: live 660,
  verified-unbound 77, unverified 3, missing 1, boundary 443 (91.8% by
  instructions).
- **Open (SDK_VU0.md "Not reduced"):**
  - em_crate_original's 001026D0 and em_snow's inline 00102900 keep their
    own arithmetic, because their oracles are not on the measured model.
    The crate oracle's SDK semantics give a denormal where the VU0 model
    gives 0, a fidelity question for the live crates.
  - Roger's 001C6150 read stays on his resource bytes.
  - 001026A0 / 00103230 still have several copies.

**Status update (2026-09-27, chain C8: EE-float harmonization of the older
oracles; docs/EE_FLOAT_MODEL.md section 5, census 1.29):**
- **Every oracle on the measured model.** The shared roots
  (test_point_light_reference.Oracle, test_player_slide_reference.EE,
  test_player_reentry_reference.Original) and seven closure interpreters
  now run COP1 through the new `tools/ee_cop1.py` and VU0 through
  `ee_float_model.vu_lane`; the per-oracle host-float intercepts are gone.
  A probe over every interpreter class confirms all 116 agree with the model
  (EE_FLOAT_MODEL.md section 5a lists each oracle and its model).
- **Translations that then differed, fixed through em_ee_float.h:** the
  point light (tick and fold), the camera retarget, the weather (the
  strength quotient), the snow tiles, the item trail (the fan's MSUB sign
  was fitted to the old product - ACC oracle), the item SDK math and the
  interaction scan's atan, the face kernel 001D0720 (host round-to-nearest
  before), the cinematic camera and playback, the pickup motion, the item
  geometry, the foot stop, em_collision's compact faces, the crates' and
  drums' SDK calls, and the COP1
  sites of the candidates, the load veil, the alignment, the projection, the
  AREA11 effect, the pickup owner and the panel program.
- **One owner each, reduced on the way:** 00102738 (em_sdk_vu0.h, ten
  private copies), the crates' SDK block and em_camera_rotation (owner
  services, em_sdk_vu0.h, em_effect_original), the point light's flicker
  matrix, em_snow's tile colour (em_sdk_vu0_00102900). This closes the two
  items the previous update left open for the crate and the snow.
- **Capture evidence kept:** the captured player point colour and flicker
  matrix, the panel camera, the opening camera's eye / target bytes, the
  Roger cinematic's up / zoom bytes, the 216 captured snow tiles and the
  crates' route records all still equal. em_item_sdk_sine now equals the
  original on all 360 level-script sine values (was 257 misses); the
  area-script test asserts it.
- **Evidence.** All make test-* pass. test-level-smoke-full passes with
  --require-through. newgame-control 9.599849. Census unchanged: live 660,
  verified-unbound 77, unverified 3, missing 1, boundary 443.
- **Open (EE_FLOAT_MODEL.md section 5c):** VU0 per-lane helpers on a
  truncated host double (001028B8 / 001028D0 / 001026A0 / 00102850 copies;
  exact except a tiny opposite-sign addend), the duplicate SDK math in
  em_item_sdk_math / em_interaction_scan, em_snow's host sinf wave,
  em_lighting (L40), em_status_draw's battery ramp.

**Status update (2026-09-27, the stream drive's timing from the
C7 capture; IOP_STREAM.md "Drive model", census 1.30):**
- **The drive model is measured, not zero-latency.** The C7 stream capture
  (decomp CAPTURES_C7.md section 1) holds 209 00112610 reads with the
  drive's status and position registers.
  - One read at a time. 00113280 answers 6 until the read in flight is done,
    also one the EE abandoned.
  - The position is the sector after the last read.
  - A seek of 0, 2 or 6 fields, by the distance class, then the read within
    one field.
  - Of the 205 reads with a previous read, 186 equal the model and 19 are
    one field off, which is the unrecorded sub-field poll phase.
  - The opening's cue 0x3F prefill and all three voiced lines' first voice
    reads took exactly 6 seek fields.
  - Distances outside the measured ranges (Roger's cue 29 read and its
    resume) take the nearest measured class and are reported by the level
    smoke.
- **The teardown exemption is removed.** check_director_beat no longer
  allows a free early teardown. Its new check_voice_drive compares the first
  voice lane with the capture: the start row, the read's 7 fields, the
  2-field hold, and the sequencer's wait up to the fields spent on lane 0's
  refill first. The teardown must lie exactly at the key-on's shift.
  - 0x97 and 0x99: on the capture's rows.
  - 0x7F: 2 rows early. The original served a lane-0 music refill first
    (f1164), because its music was keyed on 3583 fields before the voice
    where the port's was 3449: navigation since route 03's status close.
  - Mutation controls: a zero-latency drive and a 7-field full seek each
    fail at cage_roof.
- **The opening is not yet the original's (blocked).** The stream request
  reaches its key-on in 12 fields, which was 6. The original takes 27,
  because it first waits 15 fields for the area music's read. Two things are
  missing:
  - 0x1AE040's area-entry 001FAE70(1), which is unbound (bound since the
    rand() order audit below: the key-on now takes 17 fields);
  - a mechanism for that read's 16-field seek from the intro movie's
    position (+131414). The capture cannot separate the distance from the
    drive's state after the movie's stream reads, and the port has no movie
    reads.
- **Evidence.**
  - test-iop-stream: the new drive-model check against every captured
    read, and the contract test.
  - The co-simulation: cursor 46/46, which was 45/46.
  - test-level-smoke-full with --require-through.
  - newgame-control 9.599849. First control is 6 frames later (locked_ticks
    1307).
  - compare_frame_order: idle04 / walk04 / st03 at native index 1336, cut02
    and cut15 PASS, as at HEAD.
  - All make test-* pass.
  - Census unchanged: live 660, verified-unbound 77, unverified 3,
    missing 1, boundary 443.

**Status update (2026-09-27, the fence door's side 1: the arrival
walk-out on its originals; DOOR_ORIGINAL.md "Side 1", census 1.31):**
- **Side 1 is live and compared.** The decomp's C7 DOOR1 capture presses
  Cross at the door from behind the fence. The level smoke's
  `fence_door_side1` phase plays it from `fence_door`'s end and equals the
  capture row for row from the scan (f228) to f544. This covers the side-1
  alignment, clip 0x43, B7 = 1, the re-place at entry 1, the walk-out, the
  door record and the follow camera.
- **The walk-out is the player's own stage.**
  - 001B07C0(1)'s +4 / +5 / +6 = 5 / 1 / 0 go into the live record.
  - The stage's +4 = 5 handler is the translated 0015B610.
  - Its +5 = 1 routine is the translated 00183250, bound by the closure
    binder over the record's 0017B490, 001749A0, 00178B90, 00174A50 and
    00175900.
  - Its +5 = 2..4 routines (001833F0, 00183440, 001834E0) fault: no
    first-level record writes them.
  - The hold of the door script's takeover ends at the re-place without
    00182DF0 (`player_pose_takeover_restated`), because the original never
    runs 00182DF0 for it.
- **Retired:** the legacy walk-out's AREA11 hook
  (`em_door_legacy_walkout_tick`), `em_door_room_move_arrival` and
  `em_door_movement_stage_release`. The legacy walk-out stays only for the
  legacy doors of the scenes without an original roster.
- **Renderer fix found on the way.** Before this step the port quit at the
  side-1 press stance. There the camera looks down from above the door, and
  a receiver vertex of the shadow's clip kernel at w 0.24 could not be
  unprojected to a binary32 point within 1/16 pixel. The unprojection now
  searches the rounded point's binary32 neighbours. The 1/16-pixel check
  itself is unchanged (SHADOW_ORIGINAL.md).
- **Evidence.**
  - test-player-floor-reference: the executed 0015B610 and 00183250, plus
    the whole walk-out on one record (114 frames, 60 moves, the first stop
    request on frame 109).
  - test-level-smoke-full with --require-through (the side-9 run now plays
    and requires fence_door_side1 too).
  - A mutation run (standing timer 49) fails at f422.
  - newgame-control 9.599849.
  - compare_frame_order PASS.
  - All make test-* pass.
  - Census unchanged: live 660, verified-unbound 77, unverified 3,
    missing 1, boundary 443.

**Status update (2026-09-27, the rand() order audit; RAND_ORDER.md, census
1.32):**
- **The order is audited against the C7 per-call capture.** The port's
  `EM_RAND_TRACE` is resolved to the original callers
  (`tools/rand_order.py`) and compared with the decomp's newgame, r01 and
  r10 traces.
  - From the area entry the port equals the original call for call for 4
    calls. The first difference was the security gun 00825940's
    lifecycle-0 draw at AE+1, which the port missed (the owner was not
    bound, census L24; bound 2026-09-28, status update below).
  - Every frame's fixed-schedule callers (the sway, the indicators, the
    glow markers, the music, the item and effect-owner first ticks) equal
    the original's over the opening (AE+1..AE+1312), the 30 frames after
    first control, and the smoke's aligned route windows 01 (66 frames) and
    10 (311 frames).
  - The opening's faces are not the original's: em_opening_actor ticks
    both inside the opening controller from AE+16, where the original
    ticks Roger's in his owner from AE+2 and the player's in the player
    stage after the barrel from AE+5 (design risk 2).
- **Bound:** 0x1AE040's area-entry 001FAE70(1) (the first draw from state
  1, and cue 25's read) and the room move's 001FAE70(0). The opening's
  prefill now waits 5 fields for the area music's read (the original: 15,
  its 16-field seek from the movie's position, disc timing), so first
  control comes 4 frames later (locked_ticks 1311) and the frame-order
  window moves to native index 1340.
- **Comparable in the smoke** (check_owner_units, check_sway,
  check_marker_colour, check_head_sprites):
  - the whole lighting rows, over the port's own point-light pool and
    view;
  - the sway, as the original 001D7C30 over the port's pool and draws;
  - the glow markers' colour, as the original 001F4D40 over each side's
    own draws;
  - the head sprites' sub-state, wait, ramp and scalar, as 001E2560's
    transitions over the port's draws.
- **Evidence.**
  - make test-rand-order.
  - test-level-smoke-full with --require-through.
  - Mutation runs on the logged data: each check fails on a dropped,
    missing or changed draw, a changed marker rgb, head-sprite scalar or
    lighting digest.
  - newgame-control 9.599849.
  - compare_frame_order PASS: idle04 / walk04 / st03 at native index 1340,
    cut02 / cut15.
  - All make test-* pass.
  - Census unchanged: live 660, verified-unbound 77, unverified 3,
    missing 1, boundary 443.

**Status update (2026-09-27, full-route smoke to Roger, census re-measured,
the remaining gaps; census 1.33):**
- **The whole route passes with no new divergence.** At port HEAD 6da4eb5,
  `make test-level-smoke-full` passes with `--require-through`:
  - route beats 01..14 on the main line (18 phases);
  - side beat 00 in its own run;
  - side beat 09 with the fence door's side 1.

  Every relaxed check has a named cause and remover in LEVEL_SMOKE.md "What
  the full route does not yet compare". None could be removed faithfully in
  this step: each needs other work or other data (navigation, the security
  gun's owner (since bound, census L24), the opening's records, the loader, a renderer stage). Nothing in
  the port was changed.
- **Supported invocations.** Each end phase was run through the make
  target. Every phase from `battery` on passes, main line and side alike.
  `first_control` and `status` fail check_render_context's 100-gameplay-tick
  minimum; the check was left as it is. LEVEL_SMOKE.md now lists only the
  supported invocations ("Supported end phases"), and STATUS_HUB.md's hub
  capture uses `battery`.
- **Times.** The smoke targets take:
  - default target: about 15 s, ending at `battery` (lead decision,
    2026-09-27: the shortest supported end phase; most of it is the New
    Game path to first control, which every run plays);
  - full main line: about 120 s;
  - side runs: about 70 s.

  The whole route (`--require-through last`) and both side runs run only
  under `make test-level-smoke-full` (or `EM_TEST_FULL=1`); no check moved
  or changed (LEVEL_SMOKE.md "Running it").
- **Census 1.33.** Liveness was measured again with the edge recorder over
  the full route, both side runs, newgame-control and the area change.
  - All 660 live rows are confirmed. No non-live row's translation runs
    live.
  - Thirteen boundary functions (the IOP stream driver, the stream command
    packers, 001F9820, the loader's 001FF080 / 00200890, the pad
    actuator) run translations but stay boundaries (lane L36).
  - Totals are unchanged: live 660, verified-unbound 77, unverified 3,
    stand-in 0, missing 1, boundary 443 (91.8% of the non-boundary
    instructions).
- **What remains** is section 1b below: 20 items, prioritized, across logic,
  look, sound and feel.
- **Evidence.**
  - all 237 make test-* targets pass;
  - make test-level-smoke-full passes with --require-through;
  - newgame-control gives 9.599849;
  - make -B all builds with no warnings.

  Frame order is unaffected: no code changed.

**Status update (2026-09-27, the PS2 disc-drive timing behind a launcher
switch, off by default; LAUNCHER_OPTIONS.md BUILT):**
- **The switch.** `src/em_settings.{h,c}` is the one settings struct the
  profiles use (PORT_PROFILES.md "Profile switch plumbing"): one field per
  switch, `em_settings_original` with the Original values, chosen at launch
  (`em_settings_from_env` until the launcher exists). Its first switch is
  `ps2_disc_drive_timing` (`EM_PS2_DISC_DRIVE_TIMING=1`), Original value 0.
  `em_stream_live_boot` applies it to the IOP stream backend.
- **Host speed (the default).** A stream read is done at the first query
  after its issue (IOP_STREAM.md "Host speed and the PS2 disc-drive timing
  switch"). The read sequencer, its ready query and the key-on hold are
  the translated code in both modes. The drive model is unchanged behind
  the switch.
- **Effect.** First control comes 10 frames earlier than with the switch
  (newgame-control locked_ticks 1301 against 1311; 9.599849 in both), 21
  frames before the original's, exactly the capture's drive wait in the
  opening's stream request. Each voiced line's read takes 1 field against
  the recording's 7, so 0x97 / 0x99 tear down 6 rows early and 0x7F 8 (with
  its 2 rows of navigation). The frame-order post-control window is native
  index 1330 (1340 with the switch).
- **The smoke in both modes** (LEVEL_SMOKE.md "The stream drive's two
  modes"). The checker reads the run's mode. With the switch on
  (`make test-level-smoke-ps2-drive`) check_voice_drive and the opening's
  end compare as before. At host speed they compare the same events and
  require the timing difference to be exactly the host-speed reads':
  every read before the key-on takes the host-speed rows, the holds equal
  the capture's, and the opening ends exactly 21 frames early. No
  assertion was loosened; mutations (a host read one field slower) fail
  both checks.
- **Evidence.** 237 of the 238 make test-* targets pass (with the new one); the other,
  test-scene-no-shadow, fails only on the B15 status-pages file
  em_status_pages_item.c (commit 8655157), outside this step;
  `make test-level-smoke-full` (host speed) and `make
  test-level-smoke-ps2-drive` pass with `--require-through`, and the side
  runs pass in both modes; `make test-rand-order` passes in both modes;
  newgame-control 9.599849 in both; compare_frame_order idle04 / walk04 /
  st03 PASS at native index 1330 (host) and 1340 (switch on), cut02 /
  cut15 PASS in both; `make all` with no warnings. Census unchanged
  (section 1.34).

**Status update (2026-09-27, chain C8: the load veil live; LOAD_VEIL_PARTICLES.md
sections 3-5, census 1.35):**
- **Bound.** The veil draw 0021B1B0 and phase step 0021B500 run at their
  original caller 0021B550 (em_scene_bindings; em_rcl_0021B1B0 on the render
  context's one veil module, whose table and D_0026E880 are new storage).
  UM_0021B1B0 / UM_0021B500 are removed. EmLoadVeil has the seed at +0x14.
- **Its GS state.** The boot builder's GS blocks are translated whole
  (em_gs_blocks_original: 001D0F20's banks A..G and header, 00101898,
  00101630, 001008C0) and built at em_rcl_init; step V's XYOFFSET is now
  computed from the real draw environments, as in the captures.
- **Drawn.** After step V, a frame whose 0021B1B0 ran has its kicked list
  walked as the DMA sends it (em_chain_page's new list mode: END, several GIF
  packets per DIRECT, the environment registers and A+D vertices) and drawn
  by the new GS frame stage (em_gfx_gs_frame: GS-memory surfaces at the GS's
  resolution, the frame copied to a texture and drawn back by the lens
  strips, the GS texture / alpha-test / blend rules), then shown in the game
  rectangle (em_load_veil_live).
- **What a player sees.** The area read completes inside 001FF080(1, 0), so
  a load spans no tick at host speed: 0021B550 never enters state 1, the
  level stays 0 and the veil draws one frame, black, as the original's code
  does for a load that ends at once (the PS2 drew 258 frames at New Game).
  Item 11 below (the loader task's own steps) is what makes it visible.
- **Evidence.** The level smoke's `check_load_veil` (every run): the live
  run is byte-equal to the ORIGINAL 0021B1B0 executed at the same call, its
  seed equals the port's and all 15 route captures', the original 0021B500
  steps the phase to the port's. New tests: `make test-gs-blocks-reference`
  (the executed 001D0F20 with its SDK callees, 18 captures), `make
  test-load-veil-gpu` (the GS frame stage against a GS pixel model, a
  visible veil). `make test-area-load-reference` now executes 0021B500 and
  0021B1B0 in its chain replay; `make test-chain-page` covers list mode.
  newgame-control 9.599849 (unchanged). Census: six rows to live (live 666,
  verified-unbound 71; 92.4%).
- **No capture shows a veil frame** (every capture is taken after a load):
  its pixels are proven against the GS model only. A PCSX2 software-renderer
  frame taken mid-load (the fb2 method) would settle them.

**Status update (2026-09-27, chain C8b FAILSTOPS and its fix round: the
status pages live, MAP included; STATUS_PAGES.md section 7, census 1.36 and
1.37):**
- **Live.** DATABASE 00214020, SPR4 00211970 (its selector and five part
  pages) and the ITEM children EQUIPMENT 00214570, EVENT 00215870 and
  HEALING 002160B0 run through `em_status_pages_live` over the port's
  storage of the bytes they address, every callee dispatched by original
  address. The takes of 0x1E / 0x1F, the key 0x32 and the magazine 0x10 open
  their pages. The page core follows 0020CDC0 case 0's whole request map.
- **Textures.** `em_gs_texture` holds the first level's GS memory from the
  user's disc and applies each page module load and the 00200970(1)
  restore; `em_page_draw` decodes each TEX0 from it (checked against the
  decomp's decoder).
- **Callee gaps.** 0020D930 is one owner (`em_menu_hover`, all three tables:
  the hub, the ITEM root and SPR4's selector); 001C47E0 and 0015C700 /
  0015C7C0 are translated with oracles; 0015C750, 00185420's non-battery
  kinds and 00182B30 are unreachable in AREA11 and keep their fail-stops.
- **Evidence.** The level smoke's designed side run `status_pages` (part of
  make test-level-smoke-side): 1,143 page calls replayed through the
  original instructions over the status-hub capture, 12,636 callee entries
  and every view byte equal.
- **MAP (the step's fix round).** The MAP page 0020F950 runs live too: its
  22 UI-pool nodes run 002101C0 in `em_status_models`' pool (the record is
  the original's byte layout, a view at D_0028B020), the map models come
  from the disc's module 0x1E bank D_0028A570 (`tools/export_status_map.py`;
  the relocated word from the EMSP, 0 until module 0x1E loads), 001CB480
  lights them in mode 2 through 001D89D0 (`em_owner_draw_live_light`) and
  they draw between the page's 2D layers inside its SCISSOR_1 window;
  00210F30's marker (00208040) and 00211400's markers draw on the map (its
  gate bytes D_0081077F / 782 / 784 / 789 / 78C are migrated). The
  `status_pages` run opens MAP from the hub (no map; then map 8 owned) and
  takes the map 0x08 (zoom, pans, list, zoom again): 6,643 page and node
  calls replayed through the original instructions, 19,660 callee entries
  and every view byte equal; `make test-status-map-reference` runs the
  original 001C6120 / 001C6150 over the disc bank. No player-reachable
  status page fail-stop is left in the first level.
- **Not compared:** page pixels (no capture shows a page open); the MAP
  models are drawn by the renderer's skinned path with the original's light
  matrices.

**Status update (2026-09-28, GS fog arithmetic: the measured rule in the
Metal path; GS_EXACT.md 5.2):**
- Every integer fog site of the Metal path (object units, chain-page
  primitives, shadow receivers) uses FOGCOL + ((C - FOGCOL) * F >> 8)
  through one shader copy of `em_fog_gs_blend`; the skinned float path uses
  the same weights (F / 256) without the floor. The old
  (F * C + (255 - F) * FOGCOL) >> 8 was not original.
- Evidence: `make test-gs-fog-conformance` (8,192 of 8,192 GSCAP fog pixels
  through the C function and the Metal shader). The GPU fixtures' models
  (test_chain_page_gpu, test_object_unit_gpu) asserted the old form and now
  assert the measured one.
- Open: Gouraud F reaches the fog at 8-bit precision (the GS uses 8.7).

**Status update (2026-09-28, chain C8b LOADER: the module loader live for
module 0x21; MODULE_LOADER.md, census 1.38):**
- **Live.** The screen-module loader runs its own steps: `em_scene_bindings`
  boots one `em_module_loader` at start-up over the user's exported sectors
  (`tools/export_module_loader.py`, a required asset) and binds its slot-2
  task; the ITEM root's 001FF080(0, 0x21) registers it and 001FF0D0 /
  001FF830 / 001FF3F0 with 00200780 / 00200730 / 00200830 run once per
  frame after the game task, their 0x63 step clearing D_00275BD8 and
  stopping the slot (001AB7D0). The instant module-0x21 path in
  `begin_module` is deleted. A loader fault stops the frame loop after the
  dispatch (`em_frame_set_task_check`).
- **One D_00275BD8.** The scene state's byte is the one storage; the page
  core's busy field is a per-call view of it (as the request bytes are) and
  the loader reads and writes it through its view.
- **One slot table.** D_0028A490 is modelled whole (0xB0 words up to the
  task table) with the loader's cursors as its slots (0x44, 0xA9..0xAE):
  every self-naming disc header under 6 MiB now loads through (45 of 45;
  14 used to stop at the old 68-word model).
- **Drive.** Host speed by default: the load takes 10 dispatches. The PS2
  disc-drive timing switch selects the recorded drive time: 24.
- **One 001FEF70.** The page core's exit uses em_status_scene_bank_001FEF70
  (its own copy is deleted).
- **Evidence.** The level smoke's `check_module_load` (routes 01 and 03):
  the loader rows after every frame equal the captured rows without the 14
  busy polls at host speed (10 rows), all 24 with the switch
  (`make test-level-smoke-ps2-drive`); the panel's prompt window from the
  load's completion to the Yes press equals route 03 at the shift, and the
  prompt takes the request 16 ticks after the post (the original's 30 =
  16 + 14; 30 with the switch). `make test-module-loader-reference` (new
  target; the whole loads against the original instructions over captured
  RAM, now over the whole slot table), `make test-status-scene-reference`
  (001FFCD0 and 001FF590 against the original instructions: AREA11's
  header and synthetic headers through every state). newgame-control
  9.599849 (unchanged).
- **Not done: the area load** (1b item 11). 001FFCD0 / 001FF590 are
  translated and verified but not bound: the sound-bank step 001FB370 they
  wait on runs the EE sound library's queue, the SIF DMA and the driver's
  command 0x20, none of which is live. The load veil still draws one black
  frame per load.

**Status update (2026-09-28, census L24: the security gun, its cable and the
fan pair on their original owners; SECURITY_GUN.md, FAN_ORIGINAL.md, census
1.39):**
- **Identity and labels.** The "husk creature" 00825940 is a fixed security
  gun and the "husk partner" 00827490 its power cable (decomp
  verify-area11-husks). The labels are renamed everywhere in the port:
  em_script_door_fan_husk → `em_security_gun`, em_husk_fan →
  `em_security_gun_rest`, HUSK_FAN.md → SECURITY_GUN.md, the tests and the
  census rows.
- **Live.** `tick_gun` / `tick_gun_cable` / `tick_fan` in
  em_area11_bindings.c run `em_gun_tick`, `em_gun_cable_tick` and
  `em_fan_original_tick` over their records, in every walk mode, and all
  four draw their 001CAA00 units through em_owner_draw_live. The gun's
  lifecycle 0 runs 001B0FD0, its one rand() draw, bone 3 +0x78, 001C6380,
  001A2370 (its plate, uid 15) and allocates its own lamp (001AFA90); then
  the dormant 0x64 every tick. The cable runs its lifecycle 0 (001B11E0 over
  the taken bit 0x50, now exported by em_actor_roster) and lifecycle 1
  every tick; its hit and lifecycle 2 (001B1190 through
  `em_gun_rest_001B1190`, cues 0x426 / 0x427) are bound up to 001EFE00,
  which faults (unreached). The fans run their spin cycle and their tail
  (the exit bit D_008107D8 |= 0x80, 001B0C60, the player hit).
- **Retired.** em_enemy's gun and cable kinds, their meshes and their
  fabricated shot burst (with the orphaned egg_explode / flash-gib stand-in),
  the enemies group in em_area11_bindings, the interim lamp spawn, and
  em_pickup's static fan draw (a bound fan retires its prop instance). No
  test was retired.
- **Kept fail-stop:** the gun's lifecycles 4 and 1 (return visit; the
  verified `em_gun_rest_tick` is not bound), its lifecycle-2 swing
  (D_00810788 == 0xFF only), the cable's 001EFE00 node chain (0021AAC0 /
  0021A500 / 001CE860).
- **Evidence.** The level smoke's new check_gun_fan (the gun and the cable
  equal all 15 route snapshots on every tick after their setup; every
  captured fan state is on the port's cycle) and check_owner_units (the
  gun, cable and fans as owner units against the ORIGINAL 001CAA00; the fans
  over the port's +0xC8 through the ORIGINAL 001C6380; camera-exact beats
  10 and 14 compare 26 and 24 owners in full); `make test-rand-order` and
  check_rand_order (126 calls equal from the area entry, was 4; the AE+1
  divergence is gone, the first difference is the player face's AE+5 draw);
  `make test-collision-world-capture` (uid 15 equals the original in
  captures 00 and 04; its "not published" exception is removed);
  compare_frame_order idle04 / walk04 / st03 (--native-index 1330), cut02
  and cut15 PASS; newgame-control 9.599849 (unchanged).
- **Not on the smoke's route:** the fan's exit box (after Roger it now
  starts Roger's departure 0x828A10, whose op0F handshake is still a
  fail-stop) and its hit box (the chain to 0021C440 / 0021E9C0 is proven by
  the oracle, not live).

**Status update (2026-09-28, chain C8b FACE: the face attachments live,
Roger and his equipment on the object-unit path, the player's face slot,
Roger's shadow; FACE_ATTACH.md, SHADOW_ORIGINAL.md "Roger", census 1.40):**
- **001CB3C0 live.** em_owner_draw_live's `w_001CB3C0` runs em_face_attach
  (001CB3C0 -> 001C7900, 001CB2C0, 001D1F80, 001D3F50 -> 001D3E40) over
  its views; the callers with an attachment pass the regions it reads by
  address (`em_owner_draw_live_001CAA00_attached`). The owner's unit and the
  face unit are parsed one by one (the face alone when 001CA7B0 culled the
  body) and drawn in build order by the face-morph program. The inline tag
  writer / vif_append_ref_tag / 001D2910 copies of em_face_attach are gone
  (em_owner_draw_original's and the render context's are the one owners).
- **Roger and his equipment** draw their original units (models 0x47 and
  library 0x6B from the export, Roger's face unit). Retired: roger.emdl, the
  em_face_model morph of his face, opening/equipment_6b.emdl's draw and the
  draw list.
- **The player's face slot** (em_face_slot): 001B81D0's 001CA700 /
  001D06D0, 001B82D0 sub 4's 001CA770 (with 001AF890), 00183090's 001D0C70
  and 001FD950's 001D06E0 run on the player record's +0x90 over the one
  001AF710 stack; the player's 001CAA00 collapses node 7 and appends
  Dennis's face unit while a script holds it. Retired: em_player_face_host,
  em_face_model, the RELEASE_SKELETON stand-in, the row-0x18 / player-only
  refusals, test_player_face_host (superseded by test_face_slot_reference,
  rule 4).
- **Roger's shadow** (001BA580 -> 001DA6A0, kind 0x29) is drawn after the
  walk's owner units (em_shadow_live's actor call, proxy
  assets/roger_shadow.emdl); UM_001DA6A0 is gone. The original draws it in
  routes 13 and 14 (their captures REF his proxy).
- **Assets.** Re-run `tools/export_object_textures.py` (346 textures: Roger's
  body, equipment and face TEX0s), `tools/export_roger_banks.py` (Dennis's
  face resource) and the decomp's `tools/export_shadow_proxy.py --kind 0x29`
  (STARTUP.md).
- **Evidence.** The level smoke's check_face (15,328 face units over the
  full route; sampled calls re-executed by the ORIGINAL 001CAA00 +
  001CB3C0 over route 14's RAM with the port's inputs, every written byte
  equal), check_owner_units (Roger and his equipment against the original
  at every aligned snapshot; in full at route 14), check_shadow's
  check_actor (Roger's 11,277 calls, sampled re-execution, drawn at routes 13
  and 14 as the captures); test-face-attach-reference, test-face-slot-
  reference (new targets).
- **Found, not fixed:** the player's 21 node records are not popped from the
  shared 001AF710 stack (0015C420's pops), so every later slot address is 21
  slots below the original's and the first 21 pops alias the player's node
  addresses (FACE_ATTACH.md section 5).

### 1b. What still separates the port from the original first level (prioritized, 2026-09-27)

This list covers what is left between the port and the original AREA11, up to
Roger's encounter. It was made after the full-route smoke and the census
re-measurement above. Each item is here because a census row, a relaxed smoke
check or a module doc names it. The list itself changes nothing.

Each item gives:
- its area: logic, look, sound or feel;
- where it shows;
- the census rows or lane;
- what would remove it.

"Capture" marks items that need a new original recording. A chain step must
not launch PCSX2, so those items go to the lead.

The items are ordered in four groups:
- A: what differs on the route in every run;
- B: what differs on the route now and then;
- C: what a player reaches off the recorded route;
- D: what may already be original but is not yet proven (evidence gaps).

**A. On the route, every run**

1. **Done (census L24, 2026-09-28): the security gun, its cable and the fan
   pair are on their original owners** (SECURITY_GUN.md section 5,
   FAN_ORIGINAL.md). The gun's lifecycle-0 rand() draw (0x8259F0) is now the
   original's AE+1 call; the opening's first rand() difference is the
   player face's AE+5 draw (item 2). What remains of it: the gun's
   return-visit lifecycles 4 / 1 fault by design; the cable's hit faults at
   001EFE00 (unreached: no live +0x36 writer); the flag-0x30 manager
   00823CE0 is still a no-code node; the gun's lamp does not draw (item on
   the indicator children's 001CABA0 path).
2. **Look / logic: during the opening, the player and the faces come from the
   opening runtime, not from the records** (design risk 2).
   - During the opening the displayed player is not the record's pose. The
     shadow's post-step is reported, not drawn, on 1,312 ticks.
   - em_opening_actor ticks both faces inside the opening controller from
     AE+16. The original ticks Roger's face in his owner from AE+2 and the
     player's face in the player stage from AE+5. The opening's rand()
     values differ from these calls on.
   - The opening runtime also stands in for four verified-unbound script
     ops: 001BAC00 (op 0x14 is a no-op there), 001BB0E0 (em_opening_actor),
     001BAD40 and 001BA510.
   - What removes it: the opening's actors on their records. The player's
     0015C160 post-step is then computed.
3. **Look: the static world does not use the original's static-object path.**
   Rows 001C1D00, 001D5370, 001D52E0, 001E0CF0 and 001D21B0;
   RENDER_CONTEXT.md 8.4; lane L31.
   - The port's level renderer draws the scenery. Its GS state is decoded
     (LEVEL_MATERIALS.md). Its culling and packet order are not the
     original's: they do not come from 001D5370's walk over the
     static-object bank.
   - Step V's list has no +0x1D8 CALL. The background gate reads flag 0x21
     in place of the context's +0x1D8 word.
   - What removes it, three things:
     - an export of the static-object bank *D_0028A5A0 (in the chunk15
       concatenation at 0x304000);
     - a renderer boundary decision for 001D4FB0 / 001D4B20 / 001D4DA0 /
       001D5BD0;
     - the background channel 001E1E60 / 001E1AD0.
4. **Look: the lighting stand-ins.** Lanes L40 and L33.
   - The actor fold gate 001D8270 is verified but not called, and the
     renderer's post-draw tint (em_render_frame char_rig_build) stands in for
     it and for 001D8690's actor RGB on the legacy meshes that still draw
     (001D8690 itself runs live inside 001CB3C0's 001D88B0 since chain C8b's
     FACE step).
   - These are not bound: the UI lighting mode 001D8C30 and the fade
     weights 001D8060 / 001D80B0 / 001D9070 (the face lighting 001D88B0 and
     001D8690 run inside 001CB3C0 since chain C8b's FACE step).
   - The room point-light lists are resolved offline by
     tools/export_point_lights.py. The original resolves them at run time
     in 001F6640 / 001F66F0 / 001F6760 / 001F6D60 / 001F6E40.
5. **Done (chain C8b FACE, 2026-09-28): Roger's face and the face units.**
   001CB3C0 is live for Roger and the player (FACE_ATTACH.md); Roger and his
   equipment draw their original units; the player's 001CA700 / 001CA770
   run on its +0x90 slot; Roger's shadow draws. What remains of it: the
   opening's actors (item 2), the shared stack's missing player pops
   (FACE_ATTACH.md section 5), and no pixel comparison of a face frame.
6. **Sound: the SPU2 voice model.** Section-4 boundary: 41 EE sound-library
   functions have no original comparison. WP-14 AM-03 / 04 / 26 / 27.
   - The mixer is dry: no ADSR, no reverb bus, no Gaussian interpolation.
   - The positional gain 001FBF50 (AM-19) has no oracle.
   - The looped positional voices 001FC3C0 are verified but not bound (the
     flame's 0x413).
   - Capture: nothing records SPU2 output today, and the smoke compares no
     sound.
7. **Policy: the disc-drive timing switch. Resolved 2026-09-27.** The drive
   model measured from the recordings is behind the PS2 disc-drive timing
   switch (LAUNCHER_OPTIONS.md BUILT), off by default: the Original profile's
   streams read at host speed. check_voice_drive, the opening's end and the
   frame-order window (native index 1330 at host speed, 1340 with the
   switch) follow the run's mode (LEVEL_SMOKE.md "The stream drive's two
   modes"). Since chain C8b LOADER the same switch selects the module
   loader's recorded drive time for module 0x21's two reads (H7; 24 frames
   instead of the 10 host-speed dispatches).

**B. On the route, now and then**

8. **Logic: the panel's, the terminal's and the items' takeovers.** They run
   the interaction runtime's acquire and per-stage tick over their scripts'
   animation core. The script owners' takeovers are the stage's own since
   chain C7 (0015B130's prelude, 0015B530, 00182DF0). These three share
   only the release, 00182DF0, with them.
   - What removes it: their scripts request clips through +1F2 / 00183090
     on the stage.
9. **Look: draws not yet on their original units.**
   - The indicator children's +0x4C is 001CABA0. The port still draws each
     child's model mesh additively at the child's own node.
   - Roger's projected shadow 001DA6A0 is reported (UM_001DA6A0), because
     his kind 0x29 proxy is not exported.
   - The snow and the AREA11 flame draw before the chain page, not inside
     it (CHAIN_PAGE.md section 6):
     - the port's weather does not write the context's +0x2520, which
       001E0D70's kick reads; em_snow_runtime draws the snow;
     - 008235F0's 001D04B0 is not bound; em_area11_effect_runtime's
       second, partial translation of the sprite program draws the flame.
   - 001DDE10's frame-copy four-sprite pass is walked over, not drawn.
   - If a vertex's RGBAQ comes before any ST on its page, the chain page
     draws it with Q = 1.0.
   - The area title card 001C5860 / 001C5930 is em_hud's legacy card.
   - The load veil is drawn from its own packets since chain C8, but it
     draws one black frame per load: the area read ends inside one call
     (item 11: the area streamer is translated but blocked on the sound
     bank upload; LOAD_VEIL_PARTICLES.md section 5).
10. **Sound: silent or partly bound cues.**
    - The UI cues 0 / 1 / 2 / 5 / 0xB / 0xD and the unit sound are silent
      (AM-07).
    - These are not bound: the sound-bank loader chain 001FB370 /
      001FB3E0 / 001FB910 / 001FC6E0, and the rest of 001FB100 (the
      output-mode commit and the D_00281B70 copy). 001FB370 is also what
      the area load waits on (item 11).
    - 001FBC50's live part is em_sfx_stop_all, which no oracle checks.
    - 001FBDB0 is verified but not bound.
    - 001FC280's body is unverified: its D_00282160 cache is not modelled.
11. **Logic / feel: the area load's dispatches (H7).** The module-0x21
    load (the BATTERY prompt and pop-up) runs the loader's own steps since
    chain C8b LOADER: the prompt takes the request 16 ticks after the post
    at host speed (the original's 30 less the drive's 14 busy polls) and 30
    with the PS2 disc-drive timing switch. What is left is the area read
    001FF080(1, 0):
    - the port's read (em_game_legacy_area_load) finishes inside the call,
      so 001ADF50's load and the load veil span no tick (the PS2: 258
      frames at New Game);
    - the area streamer 001FFCD0 and 001FF590 are translated and verified
      (MODULE_LOADER.md section 1.9), but their bank step hands the area's
      sound bank to 001FB370, whose steps run the EE sound library's handle
      table and command queue (00119528, 00119400, 00119450, 001199F0,
      001195A8), the SIF DMA to IOP memory and the sound driver's command
      0x20 with its acknowledgement: none of that is live, and a port
      answer would decide the veil's length by invention;
    - what removes it: em_iop_stream's driver translation extended with
      command 0x20 and the SIF DMA, the EE sound library's queue bound over
      it, 001FB370 bound, then 001FFCD0 bound with the New Game module-3
      load (001AD1A0) and the area read routed through the loader
      (MODULE_LOADER.md section 5 lists the exports it then needs).
12. **Logic: startup, input and frame glue that is still the port's own.**
    All of these rows are verified-unbound. The captures prove only their
    observable results.
    - The pad read 001B57E0 / 001B5F40 is em_frame's frame_input_read.
      001B5940's block itself is live.
    - The area build's re-arm 001AF690 / 001AFCA0 is w_001AFCA0. Only its
      001AF5C0 is original.
    - The overlay init 008237C0 is the roster spawn.
    - 001AC070 / 001AB790 are the legacy task installs for Continue and New
      Game.
    - 001AB4E0 is not bound.
    - Camera state 0's 00199C50 is a reported no-effect binding.
13. **Logic: the three unverified rows.** Each needs an oracle that executes
    it:
    - 0015CF90: the D_00810707 / B9 progress bytes;
    - 001B1190: the pickups' persistence event;
    - 001FC280.

**C. Off the recorded route, but reachable by a player in AREA11**

14. **Feel / logic: damage and death.**
    - The flame's contact damage 00823580 is not modelled (INV-17). Its
      class-0xD collision push 001B1DA0 / 001A8660 is not bound.
    - The stage's hit, infection and low-health paths reach fail-stop
      workers: 0x80000023 / 001ED450, 001EFE00, the rumble 001B61C0 and the
      unbound +4 = 2 states.
    - Nothing exercises the truck-pit fall.
15. **Feel: weapons and the aiming camera.** Lane L28.
    - The aim, R1, R2 and melee states (P24..P28) run em_weapon's
      stand-ins.
    - Camera actions 1 / 2 (aim), 5 and 9..15 fault.
    - The aim release 00197490 (CAM-16) is untranslated.
16. **Logic: the status screen's other pages.**
    - Since chain C8b the DATABASE and SPR4 pages (with SPR4's part pages)
      and the ITEM children EQUIPMENT, EVENT and HEALING run live, verified
      by the level smoke's designed `status_pages` run replayed through the
      original instructions (STATUS_PAGES.md section 7).
    - The MAP page (hub hover 3, the map take 0x08) runs live since chain
      C8b's fix round (STATUS_PAGES.md section 7, "MAP"); its models'
      pixels are the renderer's (no capture shows the page).
    - Nothing exercises the options or save paths.
17. **Content: the unplayed branches.**
    - The west-yard and plateau ladders (census 7.1) have no capture.
    - The census records one hit per label, so jump-table cases the route
      did not take were never recorded.
    - Capture: a census pass over these branches.

**D. Evidence gaps (the behaviour may already be original)**

18. **Look: no pixel comparison.** Each beat's renderer output is compared
    with its original.png by eye only.
    - The capture lane's software-renderer framebuffers now make a pixel
      metric possible (CAPTURES_C7.md 5b): the displayed 512x224 field and
      Z at the 16 route snapshots, plus three extra points.
    - A metric needs the port at a snapshot's exact state. The camera-exact
      beats 10 and 14 are the candidates.
    - Rasterization is Metal's float interpolation, not the GS's DDA.
    - How the 512x224 fields are presented is a deferred platform choice
      (CLAUDE.md, PORT_PROFILES.md).
19. **Feel: the walks between the scripted windows.** They are navigation,
    and the smoke does not compare them (LEVEL_SMOKE.md "What the full route
    does not yet compare"):
    - the slide and step-off stance;
    - Roger's idle phase before f358;
    - beat 10's music-refill phase;
    - the player's units at snapshots 08, 11, 12 and 13.

    The walk code itself is live and original:
    - over the first 30 ticks of control the displacement equals the
      original's (9.599849);
    - from native index 1340 the frame order passes event for event.
20. **Beyond Roger.** The level exit (beat 15) is in neither the census nor
    the smoke. It is outside the current goal ("up to Roger") and waits on
    item 1's fan tail.

---

## 2. Live call graph (normal run)

```
main.c:265 main → em_frame_init, em_random_seed(0x45) [SI-01: seed attribution wrong], em_frontend_install (:323), em_frame_run
em_frame_step (em_frame.c:274): gfx_begin (invented clear colour) → gamepad/frame_input_read → screen_fade_tick (001AEBE0)
    → em_task_dispatch → 001FCA10 (step F) → transition_fade_tick (G) → 001FB100's 001F9CF0 (step H, since WP-8b) → 001B5B70 (I) → movie pump
    [since WP-8b every step starts with the field: D_00810E90 and the IOP stream backend's field (em_stream_live)]
slot0 startup_task: logos → E900 → title → New Game → em_game_install_new → em_scene_task_001ACEC0 (+8 = 0)
    [since S12a: 001AD1A0 → 001AD230 (001AF2C0) → 001AD360 (E900 at step 1) → 001ADF50: 001FF080(1,0) =
     em_game_legacy_area_load (formerly game_load_task): player EMDL, scene_manifest_load(assets/scene_snow/scene.txt),
     collision, em_sfx_init → state 0 (001B07C0(0) from the spawn table)]
       manifest installs: legacy door (no goto), elevator mesh, truck, panel-as-static-prop (grate), 1 examine "terminal",
       7 legacy pickups + 6 pickup_lights + 2 prop indicators, 2 type-0x13 "display props" (really fan pair 00827630),
       4 crates, 2 drums, the security gun and its cable (then "husk pair"), weather/snow, point lights, AREA11 effect, light rig (no fog line)
    → game_task → ingame_frame_machine (:5219): case 0 (init, falls through the same tick) → case 1 selects:
       [since S8: em_scene_task_001ACEC0 → cores 001ACEC0/001AD250/0x1AE040; state 0 → em_game_legacy_state0, and since S9 the tick ends there (no world frame, as the original);
        since S10a state 1 runs the cores em_sf_001AE5E0/em_sf_001AE6B0 in the original stage order, and the two
        lists below survive only as the 001AFD70 legacy blocks em_game_legacy_pool_gameplay/_cutscene; truck,
        director and grate now run after the player stage]
       cutscene_frame (:5192, selector≠0): point_light, em_opening_runtime_tick (script 0x828FC0), effect, grate,
                       snow, pickups render-only, render_chain_build, opening camera, frame_close_out
       gameplay_frame (:4679): [hud-open / game-over early-outs, retired by S11b: frame-machine states 3/5 and
                       the 001AD140 → 001AD4E0 → 001ADF00 chain replace them] → truck → [director: the original 008253F0 since WP-8b] → grate
                       → actor_update (pose_stage → player_move → pose finish → legacy matrix display) → point_light
                       → opening tail → effect → render_chain_build → em_door_update (legacy) → goto/warp
                       → snow → em_pickup_update (legacy) → indicators → Found line → em_examine_update (legacy terminal)
                       → elevator_tick (legacy) → em_enemy_update → damage/vitals → weapon → camera_update → sfx listener
                       → frame_close_out (world draw, effects, weapon spot/cone, em_hud status, Found, area title,
                         game over/continue; the director's letterbox bars were deleted in WP-8b)
NEVER CALLED (compiled, oracle-tested, dead): em_area11_interaction_host (→ panel/elevator/status runtimes, item/battery UI,
    player face host, panel message, interaction scene/runtime), em_roger_runtime/em_roger (em_roger live and em_roger_runtime deleted since census L22), em_door_original_runtime /
    em_door_program / em_door_transit / em_door_candidate, em_status_hub (+ em_status_draw), em_pickup_original_bind/tick,
    em_interaction_scan, em_cinematic_playback (not even in the Makefile app list; live since census L22), em_status_hub_ui (new, not in Makefile).
```

Original AREA11 inventory, for reference. Placement table 0x82A3C0 (21 records) and deferred registry D_0024D820[11]:

| Record(s) | Owner | Port handling today |
|---|---|---|
| r0 | room-move door 001BC350 | the original owner since census L18 (em_area11_door: 001BC350, 001BBE40, the ELF program on the AREA11 script host, 001BC150, state 4's re-place) |
| r1/r2 | fan pair 00827630 (hazard, AREA11 exit) | fabricated spin |
| r3–6 | crates 001551B0 | since census L25: the original owner (em_area11_boxes.c over em_crate_original; records equal route 04); drawn by their own 001CAA00 units since the object-unit step (OWNER_DRAW.md) |
| r7 | flame 008235F0 | visuals verified |
| r8/r9 | Roger 008237E0 + equipment 001C5C90 | unwired |
| r10 | opening controller 00823E80 | live |
| r11 | manager 00823CE0 | dormant in the first visit |
| r12 | manager 008253F0 (3 beats) | since WP-8b: the original owner (em_director_original over em_area11_script_host) |
| r13 | manager 008257A0 | dormant in the first visit |
| r14/15 | drums 00156620 | since census L25: the original owner (em_area11_boxes.c over em_drum_original; records equal route 04) |
| r16/17 | truck 00823FF0 + trigger 008251E0 | fabricated |
| r18 | battery panel 00159210 | since WP-4: the original owner in the AREA11 interaction host (Use, scripts, BATTERY page, power) |
| r19 | elevator/terminal 00827B10 | since WP-4: the original owner in the AREA11 interaction host (refusal, powered ride, carry) |
| r20 | prop 001C4820 | render-only |
| deferred | 7 pickups (00219550 ×6, 0015AFA0 ×1), the security gun 00825940 and its cable 00827490 | since census L24: their original owners (SECURITY_GUN.md) |

---

## 3. Confirmed high-severity problems

All rows below were adversarially CONFIRMED. Where the verifier corrected a finding, the table shows the corrected version. Findings that duplicate each other across slices are merged.

| # | Class | Port file:line | What the original does | Fix |
|---|---|---|---|---|
| H1 (ORCH-01, W01, W22, INV-03, P11) | FIXED (WP-4, 2026-09-23): the host loads at the state-0 rebuild, the panel and terminal nodes run it, Use is polled through player_use_poll (00160220/00184BA0 over the published list) and the stage hook runs the shared player; the refusal and the ride match routes 02/04 row for row in the level smoke; the panel matches route 03 in two windows (scan to status open, Yes to release + 25), with its prompt window not compared (the module-0x21 load wait, WP-5) and its player Y compared as retained and the camera Y with that offset (SCENE_COORDINATOR_DESIGN.md section 6, WP-4). Open: W22 (the door and Roger are not in the published list yet, WP-7/9; the items are since WP-6) | Now: `em_scene_bindings.c:1058-1063` (w_001B6990 load and hooks), `em_area11_bindings.c:391/423` (nodes #26/#27), `em_area11_interaction_host.c:787/812/849` (Use, panel, terminal). Found at (retired in WP-4): `em_area11_interaction_host.c:457` (no caller), `em_props.c:56` elevator_tick, `em_examine.c:315`; `em_props.c` grate keeps only the panel mesh and cell-18 collision | Panel 00159210 is a Use-armed actor. 00157860 aligns the player and runs its script. Callback 00157F60 posts the battery page (D_008106B0=1, B1=0x80+cost). Callback 001580C0 sets the power bit D_00810841[area] bit 7 and plays 0x3EE. Use is polled inside the player callbacks 00161020/001612D0 via 00160220, before movement. | Load the host at AREA11 scene load and tick it from the coordinator (WP-3/WP-4). Install `player_use_set_hook`/`player_pose_set_stage_hook`. Then retire grate interaction, `elevator_tick` and the examine terminal. |
| H2 (ORCH-02, W02, UI-05, INV-03) | FIXED (WP-4): the terminal picks 0x82A750 or 0x82A990 from the canonical D_0081084C, which the panel's 001580C0 sets; the examine terminal is retired. Its state 0 places the actor at the D_0081083A floor (190/230, 0x827B54..0x827BF0) through `em_area11_interaction_host_elevator_state0` | Now: `em_area11_interaction_host.c:228` (powered, D_0081084C bit 7), `:836` (state 0), `:849` (state 1). Found at (retired in WP-4): `em_examine.c:315-334`; `em_game.c:5799` | Terminal 00827B10 picks between the powered script 0x82A750 and the refusal script 0x82A990 from the power bit. The panel script sets that bit. | Same as H1. Do **not** add a `battery_terminal` manifest line (that path is fabricated, see H20). |
| H3 (ORCH-04, W06, R20, INV-04, AM-23) | FIXED (WP-9, census L22, 2026-09-24): `em_area11_roger` binds Roger and the equipment node on their original owners and draws them every frame (the level smoke's `roger` phase, route 14 row for row); `em_roger_runtime.c` is deleted | Roger 008237E0 at (331.7,290,192.5) is live and drawn at first control (captured RAM, owner 0x7A8830, story byte 0x8107D8=0). It dispatches on 0x8107D8: automatic polygon 0x82AB80 → encounter script 0x8283D0 (bank 96, stream stop 001FABB0, cue 29, resume 001FAE70(0)); armed talk 0x828810; departure 0x828A10 → 001B0C60(1,0,4); alternate 0x828990. | Bind the Roger owner (EM_INTERACTION_ROGER) in the host. `em_roger_runtime_load` needs message, scene, camera, player and frame workers, otherwise it fails explicitly (WP-8, after WP-7). |
| H4 (CAM-04) | FIXED for Roger (census L22): em_cinematic_playback is in COMMON; the script host's op00 kind 6 starts it (0022EC30) and em_camera's top mode 3 ticks it (0022EEF0); the opening track stays on em_opening_runtime.c | 0022EEF0 drives the Roger scene-1 camera, zoom and roll (roger-encounter capture, camera+0x74=25). | Add to the build and use it as the single camera-op binding for the opening, Roger and event scripts. |
| H5 (CAM-05) | FIXED for the scripts (WP-4): the host's retarget and chase hooks run it; the level smoke matches the scripts' camera eye/target with routes 02/04 row for row and with route 03 in the panel's two windows (camera Y with the retained-Y offset). The follow camera after a release: FIXED 2026-09-25 (census L13..L16, CAMERA_LIVE.md): the live camera equals routes 02 / 04 / 14 row for row after the release, route 03 from f679, route 07 converging | Now: `src/game/em_camera.c:1868`, called by `em_area11_interaction_host.c:145` (retarget) and `:470` (chase). Found at: `em_camera.c:1865` (callers only in the then-unwired host) | 001B7B30 cases 2–5: 0018CBD0 seed → 0018D7B0 styles 5 and 1 → cam+0xA0=0x78. Used by the refusal, panel and elevator scenes. | Wired as part of H1. |
| H6 (ORCH-07, UI-01) | FIXED in AREA11 (WP-5, 2026-09-23): both positions run the original page core in the interaction host's status runtime for every status screen: 0020E060 (the page reset), 0020CDC0 case 0 (001AED80(0), cue 0xB, 0020DFA0, the request map), the hub, the ITEM root and BATTERY page, and the 0020E0C0 exit (D_008106CC = 1, 0020E080 clears B0/C5), so the close takes the original two extra ticks after the edge (status_04: f200 -> f204 against f10 -> f12; `make test-level-smoke` asserts it). The START/TRIANGLE hub (0020CDC0 phase 1) is the original `em_status_hub` + `em_status_hub_ui` + 0020A7A0 in the runtime (the UI+0x20 clock is the runtime's, zeroed by 0020E060; sub-state 0 draws nothing; one 0020A7A0 step and one 00209DF0 per sub-state-1 frame). Its status models run through `em_status_models` (0020DFA0's pool clear and UI view, 001AFEB0/001AFE60, 001AFF10 + 0020E6F0/0020EC80, 0020E250/0020E1E0/0020E3A0/0020E460, the 001B0000 walk; exported models from tools/export_status_models.py) and draw after the background (flushed first through em_gfx's ordered 2D layer) and before 00209DF0's layer; `make test-status-models` matches the status-hub capture's pool and all 27 node world matrices bit for bit, and the level smoke asserts the capture's seven records and their draws. Limits: a glyph, variant or costume the capture does not show is not exported and faults, as does the D_008104E4 == 1 glow sprite 001CD520; every first-level route capture (00..14) holds the captured inputs (CA4..CA7 = FF 05 00 07, D_008104E4 = 0, D_00810C60 = 0). Scenes without the AREA11 host keep the legacy screen. The frame machine opens and closes the screen as the original does (001AE7E0 r == 2 -> state 3 -> 0020CDC0 until nonzero -> state 5 -> state 1, world frozen; the st14 frame order matches) | `em_status_runtime.c` hub_tick/hub_worker/render; `em_status_models.c`; `em_area11_interaction_host.c` hub_display/hub_models/hub_models_draw and status_open/status_page; `em_scene_bindings.c` w_0020E060/w_0020CDC0 | 001AE7E0 returns 2 on Triangle/Start (D_00810E74 & 0x810) or on a pending B0/C5 request, and anim_frame_top_b enters state 3: 001D1C50, 001D2830(3,1), 0020CDC0 until it returns nonzero, then state 5. | Done (WP-5).
| H7 (UI-02, W12, R23) | PARTIAL (WP-5/WP-6, 2026-09-23): the battery take is the original owner 00219550 (WP-6): its program consumes the item (001B6EA0 -> 001C47A0: 001C40B0, then B0 = 1, B1 = 0x1B) and the status screen pops up on it through the original page core: 0020CDC0 case 0 maps it to the ITEM page (screen 0, state 2, selection 3), the ITEM root loads module 0x21 and 002149F0 shows the acquisition notice (charge and capacity 12; the level smoke asserts its 239 frames, route 01 f220..f459, and compares the take row for row with route 01), then the list; TRIANGLE exits. The Found line is deleted. The other AREA11 takes (types 0x1E/0x1F, 0x10, key 0x32, map 0x08) post their original requests (B0 = 1/1/3/2); since chain C8b (FAILSTOPS and its MAP fix round) the pages they select run live (the ITEM child 002160B0, SPR4 00211970, DATABASE 00214020, MAP 0020F950: `em_status_pages_live`, STATUS_PAGES.md section 7; fixture `other_take`; the level smoke's `status_pages` run takes 0x1E, 0x1F, 0x32, 0x10 and the map 0x08). Since chain C8b LOADER the module-0x21 load runs the loader's own steps (`em_module_loader`, MODULE_LOADER.md): 10 dispatches at host speed, the captured 24 with the PS2 disc-drive timing switch (STATUS_LOAD_WAIT_PROBE.md's busy counts); the level smoke compares the loader rows of routes 01 and 03 and the prompt window at the drive's shift. Open: (1) module 0x1F (and the other page modules) still load at once: each needs its upload proven equal to the port's atlas (MODULE_LOADER.md Binding item 8); (2) closed by chain C8b's fix round: MAP is bound | `em_area11_interaction_host.c` pickup_status (the request) and the 0020CDC0 report; `em_level_smoke_test.c` battery phase (live) | The take path 001B6EA0 → 001C47A0/4720/4760 posts B0=1/2/3 with B1=type. 001AE7E0 then auto-opens the status screen, and 0020CDC0 case 0 maps B0/B1 to a page and message (battery 0x1B–0x1D → ITEM, message 3). There is no in-world toast. | Delete `em_hud_found_show/render`. Post the B0/B1 request into `em_status_runtime` (WP-5/WP-6). |
| H8 (UI-03, ORCH-20, SI-15) | FABRICATED | `src/game/em_game.c:5505` (Continue literal 75/60/4/120/4-6, copied from the demo fixture at :5695) | Continue: 001AC070 → 001ACEC0 route 1 → 001AD230 → 001AF2C0, which clears the whole 0x640-byte block at D_00810700 (also progress flags such as D_00810811/D_00810841), copies the restart-area record, and sets health 100, mag 30, reserve 60, battery 0. | Apply the same 001AF2C0 reset `em_game_install_new` uses, clear the progress flags, and reload the restart area (WP-1). |
| H9 (UI-04) | FIXED in AREA11 (WP-5, 2026-09-23): the invented page views (ui_pageN.emui sheets, "CONTENT TBD"/"PARTIAL" strips, the assumed ITEM rows with "x01") and the hover cue 4 are deleted, and the AREA11 hub is the original em_status_hub (its 00209DF0 layout, 0020D930 hover and help lines): X on hover 4 enters ITEM through the page core, X on hovers 1, 2 and 3 enters DATABASE, SPR4 and MAP (live since chain C8b and its fix round, STATUS_PAGES.md section 7), X with no hover buzzes (cue 2). The legacy em_hud screen serves only scenes without the AREA11 host | `src/game/em_status_hub.c`, `src/game/em_status_page.c` (the dispatch) | 0020CDC0 phase 3 dispatches to real pages: 0020EE50 ITEM, 0020F950 MAP, 00211970 SPR4, 00214020 DATABASE. | Route through `em_status_page` + `em_item_root/ui`. Leave pages with no recovered implementation unreachable rather than showing invented content (WP-5). |
| H10 (ORCH-10, CAM-17, AM-05/ORCH-11, INV-06/07) | RESOLVED (WP-8b, 2026-09-25): node #21 runs em_director_original over em_area11_script_host; `em_director.c` (kCineBeats, CINE_BAR_FADE, the chase re-seat) and `em_area11_flow.c` are deleted; the lines 0x97 / 0x99 are the message service's with their voices on the stream lanes (DIRECTOR_ORIGINAL.md section 6). Formerly: APPROXIMATION | formerly `src/game/em_director.c:28` (kCineBeats), `:175-182` (em_sfx_play 0x97/0x99) | Manager 008253F0 starts scripts 0x8294C0 / 0x829A40 / 0x829CC0 via 001BA1A0 and polls 001BA1F0. These scripts contain op07 sub8 enter (D_008101E4=1, 3B8D=2, skeleton bind 001B81D0, zoom 0); op06 on flag 0x3B; 0x16/0x18/0D sub2; sine-eased op00 kind-1 blends; and an op07 sub4/5 teardown (sub5 sets D_00810758[0x3B]=0xFF). Op0C sub0 is the **message** op 001B7D60: lines 0x97/0x99 show a text line (118/198 frames) and push VOICE.DAT cues 150/149 through 001FD580 → 001FA5A0. Beat 0 completion calls 001C4760(1,1). | Run the three scripts through `em_script` with shared op bindings (op00 kind 1 sine ease, op07 sub8, op0C → message service with voice push). Delete kCineBeats, the em_sfx_play call and CINE_BAR_FADE. Implement 001C4760 (WP-10). |
| H11 (P09) | MISSING | `src/game/em_player.c:870` | 00174AC0 (byte-matched): walking with speed > 0.5, gait ≥ 2 and \|wrap Δ\| > 2.3561945 → +0x1F0=7. 0017C030 case 7: turn clip variant 2/4, blend 4, SFX 0x137. Case 6: follow-up clip, zero speed, yaw += π. 001612D0 case 2: surface effect every 8 ticks, resume. | Implement mode 7/6 and the 001612D0 resume. Export the clips. Add an oracle (WP-15). |
| H12 (P10) | NOT_WIRED (port-only invalidation) | `src/game/em_player_pose_host.c:83`; callers `em_player.c:268,307,334,355,391,509,612,664,830` | 00182DF0 always re-seeds the default channel state on release. The original has no permanently invalid pose. Once invalid in the port, foot-stop, fidget and entry-return stop working, and `player_pose_acquire` returns -1, which **faults the interaction runtime**. | Re-seed at the default clip on every legacy release, as 00182DF0 does. This is a prerequisite for H1 (WP-2). |
| H13 (P20) | FIXED (census L18, 2026-09-25): the legacy walk-to no longer runs in AREA11; 001BBE40 aligns the player (route 09 f309 bit for bit) | FABRICATED before (corrected: the walk target is the near-side staging point) | `src/game/em_player.c:265`; `em_game_internal.h:119` WALK_SPEED 15 | 001BBE40 (byte-matched) snaps yaw and translates the player instantly with 00182F90 to door ±5 − 5·(sin,cos)(yaw), then starts the script (player clips 0x45/0x43, waits 90/70). | Replace with `em_door_transit` + `em_door_program` (WP-7). Interim fix: snap via 00182F90 semantics. |
| H14 (W07) | FIXED (census L18, 2026-09-25): `em_area11_door` binds the runtime, transit, the program (on the AREA11 script host) and `door_original/model.emdl` | NOT_WIRED before | `src/game/em_door.c:1` (legacy live); `em_door_transit_active` is defined in legacy `em_door.c:1850` | 001BC350 lifecycle → 001BBE40 kickoff → 001BC0E0 pump → 001BC240/001BC150 commit → 001BC290 close. | Bind `em_door_original_runtime` + transit + program, and use `door_original/model.emdl` (WP-7). |
| H15 (W10, INV-12, ORCH-08) | FIXED (WP-6, 2026-09-23): the host binds the seven owners at load, each pool node #0..#6 runs its owner (state 0, then `em_area11_interaction_host_pickup_tick`), 00184BA0 arms them from the published list, and `pickup_trigger_scan`, the countdown and the flat inventory add are deleted | Now: `em_area11_interaction_host.c` (bind_pickups, pickup hooks, `_pickup_state0/_tick`), `em_area11_bindings.c` tick_pickup. Found at (deleted in WP-6): `em_pickup.c` pickup_trigger_scan, pickup_take | 0015AFA0/0015AE20 and 00219550: wait for the armed bit 4, start take script 0x2482C0 / 0x248480 (0x266620 / 0x2667E0 for 00219550), wait for 001BA1F0; op-9 take; 00219550 completion plays cue 0x194 and sets taken-bit persistence. | Bind the pickups into the host's interaction scene, tick them from the coordinator, and remove `pickup_trigger_scan` and the countdown (WP-6). |
| H16 (W16, INV-10) | FIXED (census L23, 2026-09-24: em_truck.c deleted; the original owners are live, TRUCK_ORIGINAL.md) — was FABRICATED | `src/game/em_truck.c:263` (AABB trigger, 65-frame fall, -0.9 tumble) | The trigger 008251E0 **only starts camera script 0x8292C0** (gate D_00810792==0, a two-band X/Z union, D_008102B5<2) and then sets D_00810792=1. The truck 00823FF0 arms when the player **stands on it** (the D_008104C4 actor kind 9), shakes for 47 frames, then falls with beats up to 119 frames and X+Y velocity; sound 0x454 plays at frame 8 and 0x455 at frame 110; 24 FX spawns; 3 rumbles; the end state is D_00810792=0xFF. | Freeze the truck static (drop the invented trigger and fall) until 00823FF0/008251E0 are translated with an overlay oracle (WP-1 now, WP-12 later). |
| H17 (R01) | NOT_WIRED | `src/game/em_scene.c:167`; `em_game.c:1889-1892`; the live `scene_snow/scene.txt` has no fog line | 001D8FD0 (byte-matched) loads the rig record key 0x0B00 (near -209, far 304, RGB 48,48,48). 001D1C50 restores fog every frame. The face PRIM has FGE=1. The snow test's fog constants agree. | Export fog from the record (not the -208 from the old backup manifest). Check fog_apply against GS F=255·(far−z)/(far−near) (WP-1 quick fix, WP-13 verification). |
| H18 (R02) | APPROXIMATION of invented lighting (corrected; medium-high) | `../Extermination/tools/export_props.py:404` `attr_color`, `export_level.py` `attr_to_color` | Actors use the default mode 0 of 001D89D0: per-vertex rig from 001D8130/001D8340 plus the point-light fold. No original path computes 0.30+0.70·max(N·L,0). | Re-export parachute, truck, door_m03, the security gun and its cable, crate, egg, item_13, item_0b and gibs with real normals and flags=0, and remove the stand-in branch (WP-13). |
| H19 (AM-01) | INACCURATE pitch model (corrected from FABRICATED) | `src/game/em_sfx.c:195`; WAV rates from `audio_export.py:462` `tone_rate` | For A0 events, 00115850 stores bend 0x40 before 00117918. The table anchor is D_00241D70[0xD0]=4096. The legacy rates are ×1.531 (+118 steps), about 7.4 semitones sharp and 35% shorter. Cue 0x3EF (oracle): 10101.56 Hz, not 15480. | Re-export every registry id through the verified pitch path used by `export_startup_audio.py`/`export_area11_sfx.py`, storing an integer SPU pitch. Retire `tone_rate` (WP-14). |
| H20 (INV-01) | FABRICATED | `src/game/em_pickup.c:58` (constant 1°/frame spin) | 00827630 is a timed spin cycle: 60-tick wait, ramp to 0.349 rad/f, hold, ramp down. Record 1 plays 0x451 unless D_00810788==1. Record 2 player box X(318,340) Y(280,320): hit (+0x224=5.0, byte0=3, +0x0F=6); at Z<156, 001B0C60(1,1,4) if D_00810758==0xFF, else D_008107D8 \|= 0x80 (Roger departure trigger). | Translate 00827630 with an overlay oracle (WP-11). Interim: stop the invented spin. |
| H21 (INV-02) | MISSING | `src/game/em_game.c:5529` (level-exit arms "pending") | There are two AREA11 exits: fan 001B0C60(1,1,4), and Roger departure 0x828A10 → 001B0C60(1,0,4). The area-change request is D_008106B5..B8 → 001AD010 → sub-state 5 → frame case 0. | Implement the area-change consumer (ORCH-06) and targets for AREA01 sub 1 (not exported) and sub 0 (scene_drawbridge) at entry 4 (WP-11). **S12a: the consumer is live** (001AD010 → 001ADF50 native area read → state-0 rebuild → 001B07C0(0) from the exported D_0024D650; `em_scene_request_area_change_001B0C60` is the translated request, exercised by EM_AREA_CHANGE_TEST with AREA11 0x0B/0/0). Still missing: the fan/Roger requests (WP-11/WP-9) and the AREA01 targets (the area read faults for any area but 0x0B/0). |
| H22 (AM-06) | RESOLVED for the streams (WP-8b, 2026-09-25): 001FABB0, 00119828, 001FAE70 and the lane fades run on the stream lanes (em_stream_live); 00119828 is the IOP command 0x16, the driver's effect-return volume (IOP_STREAM.md: not a stream channel gain), kept by the backend and inaudible without the SPU2 reverb the port does not model; the resumed cue 25 plays at the lanes' own volume (001FA330). Formerly PARTIAL (WP-5, 2026-09-23): the stop/resume schedule is the original's, the volume is not. At the open, 001FBC50 -> `em_sfx_stop_all` and 001FABB0 -> the port's stream-release stand-in, not a translation (`em_bgm_stop(0)`, `em_opening_media_stop()`, D_008106F4/F5 = 0; also at 001AD360 step 0; no 001FA570 ring reset, no per-lane 001FAAC0 key-off, no D_00282157 store: the stream lanes are not live, WP-8); 00119828(0/1, 0x3FFF, 0x3FFF) is the full scale the port's streams always play at. At the close, the state-5 001FAE70(1) is translated (001FC280's area loop: -1 for every AREA11 spawn record; cue (D_008106C8 >> 8) & 0x7F = 25 with D_00810D38 = 0; fade 270 + ((rand() >> 16) & 0x7F); cue 25 is `em_opening_media_resume_music`, any other cue faults; the infected override, D_008104E4 = g.pd_infected == 1, faults since cue 0x18 has no stream). Open: 001FBC50 and 001FC280 set stream channels 0/1 to 0x1999 (spawn record +0x20 low half in AREA11); those calls reach w_00119828, which reports them (UM_00119828) because the port's streams have no per-channel gain, so the resumed cue 25 plays at full scale. The state-0 and state-4 001FAE70 calls stay reported (STARTUP.md) | `src/game/em_scene_bindings.c` (w_001FABB0, w_00119828, w_001FAE70, s_00810D38) | anim_frame_top_b state 1, r==2: 001FBC50 stop-all SFX, 001FABB0 stop streams, 00119828 ×2. On exit, 001FAE70(1) restarts cue 25 with a 270+rand fade. | Comes free with WP-5. `em_status_frame` emits these. Update the stale note at `em_sfx.c:477`. |

Confirmed problems in non-high findings that other rows depend on:

- The port's pose invalidation (H12) makes host `acquire()` fault. **Fix H12 before H1.**
- The host binds the PANEL, the ELEVATOR and (since WP-6) the seven items. **Do not delete the legacy door or examine scans until the DOOR and ROGER owners are bound to the shared scene** (W22 correction).

---

## 4. Roadmap to a faithful first level

The order follows dependencies and impact. "Removes fabrication" marks packages that delete live invented behavior.

### WP-0 Lead housekeeping for finished lanes (no code risk)
- **Scope:**
  - Add `src/game/em_status_hub_ui.c` to COMMON.
  - Add Makefile targets `test-status-hub-ui-reference` (`python3 tools/test_status_hub_ui_reference.py`, about 84 s) and `test-roger-media-reference` (needs `tools/export_roger_media.py` assets).
  - Re-run `tools/test_area11_interaction_host.py`, because `EmOpeningDialogue` gained `loaded` (roger-media lane). The face-host lane's PASS may predate that change.
  - Commit the three lanes together with the uncommitted host, test and tool diffs.
- **Lane state:**
  - face-host: DONE. Fixture corrected so `cinematic_face` runs after `first_battery`. B81D0/D0C70/CA770/FD950 wiring rechecked. 9/9 scenarios pass.
  - roger-media: DONE. Clock off-by-one fixed per 001FD790/001FD950/001FDB80. 4,143 ticks match.
  - status-hub-ui: DONE. v2 EMHS exporter. 167 streams / 19,968 commands against executed 00209DF0. Clock and trail are now caller-owned.
- **Removes fabrication:** no.
- **Status: DONE.** The three lanes are committed (face-host 62c58ed, roger-media 8efed7d, status-hub-ui e1d88a4). `em_status_hub_ui.c` is in COMMON and both Makefile targets exist. `tools/test_area11_interaction_host.py` was re-run on 2026-09-22 after those commits: all three scenarios PASS.

### WP-1 Neutralize live fabrications that need no new infrastructure
- **Scope. Each item is independently committable:**
  - **H8:** Continue applies the 001AF2C0 reset (the same values as New Game) and clears the D_00810700 block progress flags. **DONE** (9d4a631). The 001AF2C0 inventory seeds (item counts 0/5/7/0x17 = 1, 0x10 = 2, magazine packs 2) are now applied by `em_pickup_reset` and checked against the executed original (cleanup-game lane, uncommitted at the time of writing).
  - **H10 partial:** delete `em_sfx_play(0x97/0x99)` in `em_director.c:175-182`. **DONE** (da41660).
  - **H16:** truck static (remove the AABB trigger and fall), leaving a TODO. **DONE** (da41660).
  - **H20:** stop the constant spin on type-0x13 props. **DONE** (da41660). The static pose now also applies 00827630's init rot.z (±π/4 by record +0x03) when the manifest line carries `owner 0x827630 <flags2>` (cleanup-game lane). PENDING: the local manifest lines do not carry the suffix yet, so the live fans are still yaw-only.
  - **H17:** add the fog record (−209, 304, 48,48,48) to the scene_snow manifest via the exporter. **DONE** (port da41660, exporter decomp 7ae3b07; `test-area11-fog-reference`).
  - **AM-22/INV-23:** do not auto-start manifest BGM on the New Game path. **DONE** (9d4a631).
  - **SI-02/AM-17:** route `wpn_rand` and `footstep_rand5` through `em_random_next()` with the original bit extraction (00179B90: rand()&7 folded). **DONE** (7095fd6, `test-player-random-reference`).
  - **R03/R04: REFUTED as stated — the original has a flashlight cone-shell draw (area-gated).** The original's path: 0017A970 sets the draw enable D_008106C7 with D_00810D3C, 00188ED0 calls 00187780 while it is set, and 00187780 calls 001D9530, the cone-shell draw (unless area flag 001B0070() & 0x20000000). AREA11's D_008106C8 (what 001B0070 returns) is 0x20081910 in save-state captures 02–15 (ORIGINAL_FRAME_ORDER.md P31) and 0x20089910 in the 09_fence_door route capture (RENDER_CONTEXT.md, PLAYER_REACTION.md); bit 0x20000000 is set in both, so 001D9530's cone shells are skipped in the first level: only 00187780's draw request runs (the decomp's CURIOSITIES.md entry 1). Recorded in 7095fd6. The port's per-pixel spot term is still a stand-in (`em_gfx.h`).
  - **R09/ORCH-27:** the original frame clear. **DONE (render + UI step, 2026-09-25):** the original clears Z only and draws the channel-3 background grid (001E1E60, kernel 0x0023C990) first in every world frame (docs/BACKGROUND.md); the port draws it from the disc-replayed asset, gated as 001D2300 gates its CALL. First control's sky region is (48, 48, 48) with no black pixel, as the original.
  - **ORCH-03, W24, INV-14, INV-15:** delete the dead `battery_terminal` path and the type-0x11 hook, and rename `have_battery` to the opening-complete byte D_00810811. **DONE:** `battery_terminal` and the rename in 9d4a631; the type-0x11 take hook and `em_game_set_battery` / `em_game_has_battery` removed by the cleanup-game lane.
- **Originals:** 001AF2C0, 001B7D60, 001D8FD0, 00122BB8, 00179B90.
- **Verification:**
  - The existing suites must stay green.
  - Fog: capture pixel/fog-coefficient check against the opening GS dump (fog constants 255/2048/151.11/−0.497 already appear in `test_snow_particles_reference.py`).
  - Continue: add a decomp-C/instruction check of 001AF2C0 field writes.
- **Depends on:** nothing.
- **Removes fabrication:** YES.

### WP-2 Pose-source lifetime (H12)
- **Scope:** replace permanent `player_pose_invalidate` with a re-seed at the default clip when each legacy path releases (aim, R2, melee, door, examine, interact, hit, low health), mirroring 00182DF0. In the long term, give aim, melee and door their own raw-channel workers.
- **Originals:** 00182DF0, 00182D70, 00161020, 001612D0, 0017B910.
- **Verification:** `test_player_pose_host_reference.py`, `test_player_pose_live_reference.py`, `test_player_foot_stop_reference.py`. Add a new case: aim, release, then run stop, and assert the foot-stop still fires.
- **Depends on:** nothing. **Blocks:** WP-4 (acquire faults on an invalid pose).
- **Removes fabrication:** yes (port-only state).
- **Status: DONE** (7095fd6). Legacy releases re-seed through 00182DF0's path; `test_player_pose_host_reference.py` executes 0x182DF0 and includes the aim-release-then-stop case.

### WP-3 Live scene coordinator (backbone)
- **Scope:**
  - Port anim_frame_top_b (0x001AE040) states 0–6 with the byte-matched 001AE7E0 classifier as the single dispatch point. Case 0 returns without a world frame. Case 1 checks D_008106B8/B9 → 001AD010/001AD140.
  - Build a native pooled-actor list walked at the 001AFD70 positions:
    - gameplay 001AE5E0 order: player 0015BCF0 → 001D1C50 → 001C1D00 → 001AFD70(0) → 0015C160 → 001F0360 → 0018B9C0
    - cutscene 001AE6B0 order: 001AFD70(1) → player → 001AFD70(2)
  - Move truck, director, grate, enemies and effects into owners on the list.
  - Move the em_hud and game-over early-outs out of `gameplay_frame` (done in S11b).
  - Wire the letterbox gate `em_frame_screen_fade_gate` (SI-17).
- **Originals:** 0x001AE040, 001AE7E0, 001AE5E0, 001AE6B0, 001AFD70, 001AD010, 001ADF50.
- **Verification:** 001AE7E0 is byte-matched, so an ELF-instruction oracle over all inputs is cheap. `test_status_frame_reference.py` already executes 0x1AE040..0x1AE5E0 for the status branches; extend it to states 0/1/2/6. The frame order has no oracle today; compare the ordered call trace against the NEARMISS C bodies.
- **Depends on:** nothing. **Blocks:** WP-4…WP-12.
- **Removes fabrication:** removes the port-ordered update list (ORCH-14/15/16).
- **Status: IN PROGRESS.** Phase 1 cores landed (f519488, ac74c14, 61796a0). S8 (legacy-mode wiring) landed: the slot-0 task runs the translated 001ACEC0/001AD250/0x1AE040 cores and the letterbox gate is fed 3B90/C4 (SI-17 wired; C4 is still always 0). S9 landed: the state-0 tick returns without a world frame (the port's same-tick fall-through is removed; no frame-index constant needed re-baselining, since they all count world frames). S10a landed: both world-frame variants run as the translated 001AE5E0/001AE6B0 cores with their stages in the original order; the port-ordered monoliths gameplay_frame/cutscene_frame are deleted (ORCH-14/15/16 closed for the stage order). S10b landed: the native actor pool is live. State 0 spawns the AREA11 roster (deferred group, placements, weather and title nodes), the first player stage spawns the player's attachment and effect children, and every 001AFD70 position walks the pool node by node in the original list order (frame-order traces match the original node for node, callback and record, except the allow-listed record 13, footstep and opening-actor nodes). Each owner node runs the port's legacy code for it or is an explicit no-port-code node; scenes without an original roster keep the old block as one legacy_world node. The canonical D2 progress region exists (taken bits and D_00810CA4..CA7 migrated from em_pickup). S11a and S11b landed together (lead decision D4): spad 3B8D/3B91/3B92 have one storage (g.frame_selector, the opening's private skip promotion and `s.cinematic_ready` are deleted; 001AE6B0 promotes 3B91), the input words D_00810E74/E70/E50 are written every tick in the original layout, and the frame machine acts on the classifier: START/TRIANGLE opens the status screen through states 3/5 with the world frozen (legacy em_hud as the interim 0020E060/0020CDC0; H6 PARTIAL), and death writes B9 at the player stage (0015CF90), which leads at fade 2 to 001AD140 → the byte-matched 001AD4E0 → 001ADF00 → the interim 001AC070 continue task. The em_hud self-toggle, its menu-inhibit copy (B3 is canonical, written at the player stage by an interim stand-in for 0015BA50's tail) and the status/game-over early-outs are deleted; the unported classifier arms fault. S12a landed: New Game (and Continue) register the task with a cleared record and run the original load arms 001AD1A0 → 001AD230 (001AF2C0) → 001AD360 (the intro movie at step 1) → 001ADF50 (the native area read and the load veil) → the state-0 rebuild, whose 001B07C0(0) places the player from the exported D_0024D650 (byte-matched translation, oracle-tested; the manifest spawn is gone for AREA11); game_load_task is retired; the area-change consumer 001AD010 → +9 = 5 is live; the weather node reads the canonical D_008106C8 (001B0250); record 13's manager 008257A0 is translated and frees itself on the second world frame, so the New Game census at first control is the original 49. S12b landed: the AREA11 door's commit is the original room move (001BC150: 001AEDE0(4, 0), B8 = 2, B7 = the destination row's side byte, from the exported door descriptor), 001AD010 sets 0x810702 at fade 2 and 0x1AE040 state 4 re-places the player with the byte-matched 001B07C0(1) at spawn entry 2 (side 0) or 1 (side 1) and falls into state 1 in the same tick; the weather and area-title nodes leave and are respawned as in the original; the tick sequence matches the original route capture 09_fence_door row for row (`make test-room-move-reference`). S13 landed: the live level smoke `make test-level-smoke` (EM_STARTUP_TEST=newgame-level, docs/LEVEL_SMOKE.md) walks the route of FIRST_LEVEL_ROUTE.md phase by phase; first control and the status open/close pass in process and against the original captures (status_04.json, route 01_battery), and the 13 later phases report NOT-LIVE with the step each waits on. See SCENE_COORDINATOR_DESIGN.md section 6 "Phase 2 status". HK (housekeeping, 2026-09-23) extended D2: D_00810707 (with its 0015CF90 store at the player stage, read by 001B07C0), D_00810792/793 and D_00810813 are canonical progress bytes; `g.cine_step` (and its non-original per-area-build reset) and `g.opening_key_item_zero` are deleted; 001C4760 runs its one translation over the canonical key bytes for the opening and the legacy director; D_008106F1/D_00810707 are single storage for the stage workers, Major2 and recovery lanes (pointers); the G6 no-shadow grep passes on the committed tree; STARTUP.md lists every local export step. Nothing in the tick log of the level smoke or the newgame-control frame trace changed (SCENE_COORDINATOR_DESIGN.md section 6, "HK landed").

### WP-4 Install the AREA11 interaction host (panel, battery, power, elevator, face)
- **Scope:**
  - Call `em_area11_interaction_host_load` from the AREA11 scene arm.
  - Tick the panel and elevator owners from the coordinator.
  - Install the Use and stage hooks (`player_use_set_hook`, `player_pose_set_stage_hook`) so Use is polled inside the player callbacks (00160220).
  - Render the panel message and face host.
  - Retire `grate` interaction, `elevator_tick`/`elevator_descent_begin` and the `em_examine` terminal for scene_snow. Keep the panel mesh, cell18 collision and indicators, which are verified.
- **Originals:** 00159210, 00157860, 00157F60, 001580C0, 00827B10, 00828050, scripts 0x82A750/0x82A990, 001B7B30, 0018CBD0, 0018D330, 0018D910, 001B81D0, 001D0C70, 001FD950.
- **Verification:** `test_area11_interaction_host.py`, `test_panel_reference.py`, `test_panel_message_reference.py`, `test_elevator_reference.py` (2,520 cases), `test_elevator_commands_reference.py`, `test_camera_interaction_fixture.py` (panel/animation_ee.bin and elevator/refusal captures), `test_status_page_record_reference.py` and `make test-status-runtime` (the bound 002149F0; they superseded the retired `test_battery_ui_reference.py`), `test_face_allocation_reference`. New work: the live-path smoke exists since S13 (`make test-level-smoke`, docs/LEVEL_SMOKE.md); WP-4 adds the runners and capture checks of its phases `elevator_refusal`, `panel` and `elevator` (route beats 02, 03, 04: the power bit 0x80 and the descent to y 190), which report NOT-LIVE until then.
- **Depends on:** WP-2 and WP-3. Battery-page UI needs WP-5 (or the host's battery_open route, which does not need the other_page hooks).
- **Removes fabrication:** yes (examine terminal pivot W04, grate stand-in, legacy elevator conflation W05). **Unblocks level progression (H2).**
- **Status: DONE (2026-09-23).** See SCENE_COORDINATOR_DESIGN.md section 6, "WP-4 landed": the host is loaded at w_001B6990 and bound at nodes #26/#27, Use and the shared player run inside the player stage (also in 001AE6B0 once the opening is done), 001AAD00 publishes, request-opened status screens (B0 != 0) run the host's original page layer, the step-F message service shows 0x80000018/0x8000001A, D_008106EF decays at 0018B9C0, D_0081084C and D_0081083A are canonical D2 bytes, and the carry 00828050 uses the EE guard-bit add (route 04's 150 values). The level smoke runs the phases elevator_refusal, panel and elevator live against routes 02/03/04 (battery is driven through the legacy pickup, NOT-LIVE until WP-6). Retired: the em_examine terminal, the legacy ride and its lock state. Remaining: the status page's module load is instant (route 03 waits 25 frames; WP-5), the player is not re-grounded on the elevator actor after a release (the collision/floor lanes), the follow camera after a release is the port's (WP-16), the Use list holds only the panel and the elevator (W22), the status page's system cues are silent (WP-14).

### WP-5 Status stack live (frame-machine state 3)
- **Scope:**
  - Replace the interim 0020E060/0020CDC0 bindings (`em_hud_status_open`/`em_hud_status_tick`, S11b) and `em_hud_render` with `em_status_runtime`.
  - The runtime owns the UI+20 clock and zeroes it in 0020E060; it resets the shared trail on 0020E020.
  - Call `em_status_hub_ui_prepare/render` at EM_STATUS_HUB_DRAW.
  - Bind the `other_page_tick/render` hooks so open is not refused.
  - Wire `em_status_page`, `em_item_root/ui` and `em_battery_ui`.
  - Deactivate the hub/ITEM texture slot on each switch.
  - Handle pickup B0/B1 auto-open.
  - Delete the invented `em_hud` pages, Found line and hover cue 4.
  - Keep 0020A7A0 and the model workers 0020E250/E3A0/E1E0/E6F0 as explicit required workers; do not substitute `em_hud_background_sprite`.
- **Originals:** 001AE7E0, 0020CDC0, 0020D930, 00209DF0, 00208AD0, 00209280, 00209860, 0020E060, 0020E020, 0020E0C0, 0020EE50, 002149F0, 0020A7A0, 0011E2A8, 0020DFA0, 001AFF10, 001AFF90, 001AF800, 001AFEB0, 001AFE60, 001B0000, 0020E250, 0020E1E0, 0020E3A0, 0020E460, 0020E6F0, 0020EC80, 001F4BF0.
- **Verification:** `test_status_frame_reference`, `test_status_page_reference`, `test_status_hub_reference` (17,520), `test_status_draw_reference`, `test_status_hub_ui_reference`, `test_item_root/ui/trail/geometry_reference`, `test_battery_ui/pickup_reference`, `test_status_background_reference` (0020A7A0 with the executed original sine, and the status-hub and panel RAM captures' D_002655A0 blocks), `test_sdk_math_original(-reference)`, the host fixture's hub scenario and the level smoke's hub-draw count, `test_status_scene_reference` and `test_status_scene_original` (the status-model owners and the loader), `test_status_models` (the live binding over the status-hub capture: pool and all 27 node world matrices bit-exact), the host fixture's hub scenario (the backdrop flushed before the model draws), the level smoke's hub-draw and model-record checks, and the status-hub image compare (STATUS_SCENE.md section 7; the background orientation, STATUS_HUB.md).
- **Depends on:** WP-3.
- **Removes fabrication:** YES (H7, H9, UI-06/07/09, R05 orbit if the menu scene is redone). Also fixes the H22 audio.
- **Status: PARTIAL (2026-09-23).** See SCENE_COORDINATOR_DESIGN.md section 6, "WP-5 landed". Done: every AREA11 status screen runs the original page core (cold entry, the original hub, ITEM root, BATTERY page, the 0020E0C0 exit with its two-tick close latency); the hub is `em_status_hub` + `em_status_hub_ui` with the runtime-owned UI+0x20 clock (zeroed by 0020E060), the texture slot released on each page switch, hover 4 entering ITEM and hovers 1-3 faulting; the translated 0020A7A0 (sine: the original 0011E2A8, `em_sdk_math_original`) is the one background of the hub, ITEM and BATTERY pages; the status models are translated and drawn (`em_status_models` over `em_status_scene_original`, with the exported models and em_gfx's ordered 2D layer; H6); the battery pickup pops up the acquisition notice (H7); the invented pages, the Found line and the hover cue 4 are deleted (H9). The module-0x21 load runs the translated loader since chain C8b LOADER (H7; module 0x1F still loads at once); the 0x1999 stream volume (H22); the non-battery takes open their pages since chain C8b (the map take's MAP since its fix round); the runtime's own frame/queued path serves only the sanitizer fixtures.

### WP-6 Pickups and the single Use arbiter
- **Scope:**
  - Bind all 7 AREA11 pickups via `em_pickup_original_bind/tick` into the host's interaction scene.
  - Publish the owners and resolve Use through `em_interaction_scene_scan_checked`: one pass over the published list, lowest score wins, result 2 commits immediately.
  - Remove `pickup_trigger_scan`, the 2-frame take and the flat `inventory_add`, so the subtype families (maps, keys) are correct (W11).
  - Apply the 001C40B0 case-0x10 magazine write directly (W13).
- **Originals:** 00184BA0, 00183EF0, 0015AFA0, 0015AE20, 00219550, 001B6EA0, 001C47A0/4720/4760, 001C40B0.
- **Verification:** `test_interaction_scan_reference`, `test_interaction_pickup_reference`, `test_pickup_owner_reference`, `test_pickup_motion_reference`, `pickup_original_test`.
- **Depends on:** WP-4 (host) and WP-5 (status requests).
- **Removes fabrication:** yes (legacy take, Found, W11, W13, W14).
- **Status: DONE (2026-09-23).** See SCENE_COORDINATOR_DESIGN.md section 6, "WP-6 landed": the seven owners are bound in the host and run at their own nodes (00219550's light child frees itself at the take's completion, the owner node at its free), published through the translated 001B17A0 and armed by the host's one 00184BA0 pass; the item block D_00810C60.. is canonical D2 progress; 001C40B0 (all cases, W13 direct) and the class-7 aura's 001F1110/001F1180 are translated with an oracle (`test-pickup-items-reference`); every take posts its original request; the legacy scan, the countdown, the flat add, the ammo queue and the interact-clip lock are deleted. The level smoke's battery phase is live and compared with route 01. Open: the aura sprite's draw (001F0A60) and cue 0x194 (no exported sample, WP-14) are reported, not drawn/played; the take pages other than BATTERY are untranslated (they fault); D_008104A0/D_008104E6 are passed as 0 (no port writer). Since census L07 the items' collision cells (001A2370 / 001B1D20) are in the collision world and the mode-6 LOS test is the translated 0019A910 over it; the crates', drums' and truck's cells are not published yet (L25/L23).

### WP-7 Original door (room move)
- **Scope:**
  - Bind `em_door_original_runtime` + `em_door_program` + `em_door_transit` + `em_door_candidate` for the AREA11 door, publishing it in the shared scene.
  - Draw `door_original/model.emdl` from the runtime palette.
  - Commit through 001BC150. For this door the id bit 7 is clear, so the commit is a B8=2 room move to spawn entry 2 (side 0) or entry 1 (side 1) from D_0024D650[11], writes the entry byte 0x810702, and keeps actors, overlay and audio.
  - Camera: `camera_interaction_retarget_distance_area11(-20)` (CAM-06), and re-seat via 001B0080 (camdist −46.8) instead of CAM_DIST 33 (CAM-07).
  - Delete the legacy MOVE-TO walk, walk-out constants and `door_m03` for AREA11.
- **Originals:** 001BC350, 001BBE40, 00182F90, 001BC150, 001BC290, 001AEDE0, 001B0460, 001B0080, 00183250.
- **Verification:** `test_door_original_reference` (5,662), `test_door_original_runtime.py`, `test_door_transit_reference` (on the EE float model since census L18), `test_door_candidate_reference`, `test_area_script_reference` (the program 0x24DE40 over route 09), `test_script_door_fan_reference`, and the level smoke's `fence_door` phase (route 09 row for row). `test_door_program_reference/runtime` were retired with census L18 (their handler copies are no longer bound; the script host's handlers are the bound translation).
- **Removes fabrication:** YES (H13 walk, synthesized re-place, CAM_DIST).
- **Status: DONE for side 0 (census L18, 2026-09-25; DOOR_ORIGINAL.md "Binding").** `em_area11_door` runs the fence door's node on `em_door_original_tick` over its pool record: 001BBDA0's 001B0F60 (001B0EA0's bone slots through em_area11_boxes), the Use scan's class-5 candidate and arm (the interaction host publishes the door through 001B1B30), 001BBE40 (`em_door_transit_kickoff` on the EE float model: the f309 alignment bit for bit; the patch with 001BBD60's word), the ELF program 0x24DE40 on the AREA11 script host (op07 frame, op0D sub 5 camera, op0A clip 0x45, op0B sub 6 door clip 2 with cue 0x401, op02 wait 90), 001BC240/001BC150 (fade, B8 = 2, B7 = 2), 001BC290 and 001BC300 (the runtime's pose and palette drawn through the actor draw chain). 0x1AE040 state 4's D_008101E4 = 0 now reaches the camera byte (its one storage); the shared runtime keeps a held player across 001AFCF0's 3B8F clear. Retired: the manifest door in AREA11, em_door.c's S12b adapter and `EM_ROOM_MOVE_TEST`, the AREA11 camera's door cinematic, state 4's `g.doorcam = 3`. Evidence: the level smoke's `fence_door` equals route 09 f309..f532 row for row (spad, camera byte and camera, letterbox, message, B0/B1, the player's placement, heading and record, the door record's header and script block, the room move, the follow camera). Open: side 1 (no capture; its arrival's walk-out is the legacy walk-out), the locked program (subtype 0x15), the door id's bit 7.

### WP-8 Single message service (001FCA10)
- **Scope:**
  - One native message machine over the D_002821B0/B4/B8/BC state, ticked at main-loop step F.
  - Route the opening, director, examine refusal, panel and Roger requests into it. Handle modes 2/3/4, voice pushes (001FD580 → 001FA5A0), the stream table (001FD4C0) and `D_008106F5` modes.
  - Draw its lines through the translated glyph chain (001FE070 → 001FC7B0 → 001CC1E0 → 001CC3B0) instead of `em_hud_subtitle`.
  - Bind the stream/voice lanes (`em_stream_lanes_original`) in place of the no-effect stream workers.
- **Originals:** 001FCA10, 001B7D60, 001FDB80, 001FD790, 001FD950, 001FD580, 001FD6A0, 001FD4C0, 001FA5A0.
- **Verification:** `test_roger_media_reference` (4,143 ticks), `test_panel_message_reference` (3,156 callbacks). Add mode-1/2 gates and a voice-cue case.
- **Depends on:** WP-3. **Blocks:** WP-9 and WP-10.
- **Removes fabrication:** partly (UI-15 plain-text refusal).
- **Status: DONE for the service, glyph draw and routing (lead decision 2026-09-23: WP-8 is split; the stream lanes and the IOP stream backend are WP-8b below).** Live: `em_message_live` installs the service at step F from `main.c` on data exported by `tools/export_message_data.py` (the ELF's `D_00264DD0` global and area-11 tables, `D_0026EC60`, `D_0026EC10`, `D_00264CD0`/`D_00264BF0`; the disc's global and AREA11 banks). Routed into it: the opening (op0C on the live block, the op12 stream request 001FD4C0(0x66) with its `D_008106F4 == 1` wait, the op-4/001B6BF0 `D_002821B4 = 2` stores, the actors' talk from `D_008106D4[0/1]` as 001BA580 consumes it), the AREA11 host's panel/terminal lines (001B7D60 case 0, the delay word passed through as the request holds it; the host's frame view reads and writes `D_002821B4` in the live block) and 001FC9B0 (`w_001FC9B0`). Its draw is the translated glyph chain (docs/MESSAGE_GLYPH.md) through the port's atlas. Workers: 001D06E0 → the host's face talk; 001FD470 → `w_001FBC50` (em_sfx_stop_all, then its two 00119828 calls) and `w_001FABB0` (the port's stream-release stand-in, not a translation: it stops em_bgm and the opening stream and clears `D_008106F4`/`D_008106F5`, with no 001FA570 ring reset, no per-lane 001FAAC0 key-off and no `D_00282157` store); 001FA790 lane 0 with the opening row's cue → `em_opening_media` (the lane-0 stand-in: `D_008106F4` 2 → 1 on its first step H, an instant prefill); 001FAAC0 on the idle voice lanes → nothing. The opening's 001B82D0 phase 0 now also makes its two 00119828(0/1, 0, 0) calls after 001FD4C0 (reported no-effect bindings: no 001157F0 sink). Deleted: `em_panel_message`, the `em_opening_dialogue_*` clock, `em_hud_subtitle`, the `.emod` exports (`tools/export_interaction_message.py`, and the dialogue parts of the opening, panel, elevator and Roger exporters). Verified: `test-message-glyph(-reference)` (the glyph chain against the executed original packets), `test-message-service(-reference)`, `test-message-draw(-reference)`, `test_panel_message_reference.py` (the live service against the original for 0x80000018/0x8000001A), `test_roger_media_reference.py` (4,143 encounter ticks on the live service, face talk 1,0,1,0), `test-opening-runtime` (the opening's glyphs and the `D_008106F4` 2 → 1 → 0 protocol), `test-message-capture` (the refusal frame against the original screenshot: the same two text lines within 1 px), the level smoke (the block row for row against routes 02/03/04) and newgame-control (9.599989). Open: (1) **Moved to WP-8b: the stream lanes are not bound.** Their initial state 001F9820 is now translated and oracle-checked (`em_stream_lanes_001F9820`, unbound; 315,956 full-sweep cases, and the captured lane voices/masks/buffers of all 26 images reproduced). Binding the lanes as the single owner still needs a work package of its own (docs/STREAM_LANES.md "Still missing" 1..5): an IOP stream driver in the audio backend at the 001157F0 boundary (commands 0x3C/0x3E/0x40/0x41/0x42/0x43/0x16, SPU ADPCM playback from the lane buffers, and the `D_00281880` block-cursor words 0011A730 returns; the captures fix their values, not their timing), its IOP buffer allocator (`D_00275B20/24/28`) and 0011A2B0, the stream-file exporter and sector reader plus `sub_O_STREAM_MUSIC_DAT_1`, 001FB100 at step H, and the replacement of every legacy stream path at once (`em_opening_media`, em_bgm's resume, `w_001FABB0`, `w_00119828`, the 001B0C00 fades, the game-over cue). Until then `D_00282155/156` read 0 and a voiced line faults at 001FA5A0. **Lead decision (2026-09-23): split** — this is WP-8b "stream lanes + IOP stream backend"; WP-9 and WP-10 depend on it for voiced lines; (2) **done 2026-09-26 (status UI step):** the mode-3/4 presenters run at step F and the gate below is deleted; before that, the mode-3/4 presenters (001FD0E0, 001FCB90/001FCF90/001FCF60) were untranslated: the status page layer kept its own mode-4 copy and the host's gate held step F while it ran. The gate is a port stand-in (the original 001FCA10 has none; a mode-2 line's delay and timer freeze while a page is open); **lead decision (2026-09-23): keep it, labelled, until those presenters are translated** (then delete it); (3) the director (WP-10), Roger (WP-9) and the door (WP-7) still have to post into it; (4) the glyph draw's GS state (CLUT, nearest sampling, blending) is the atlas boundary.

### WP-8b Stream lanes + IOP stream backend (split from WP-8, 2026-09-23)
- **Scope:** bind `em_stream_lanes_original` (001FAE70/001FABB0/001FA790/001F9CF0/001FA0D0/001FA330/001FA5F0/001FAAC0/001FAB50/001FABF0/001FC280/001FD470 and the initial state 001F9820, all oracle-verified) as the single owner of the music/voice streams, replacing every legacy stream path at once (`em_opening_media`, em_bgm's resume, `w_001FABB0`, `w_00119828`, the 001B0C00 fades, the game-over cue). The port's audio backend implements the IOP side behind the 001157F0 boundary: the stream commands the lanes send (their IOP meaning is unverified until the driver behaviour is characterized — docs/STREAM_LANES.md), SPU ADPCM playback from the lane buffers, and the `D_00281880` block-cursor words 0011A730 returns (values fixed by the captures, timing a stated model); its buffer allocator (`D_00275B20/24/28`) and 0011A2B0; a local STREAM file exporter and sector reader plus `sub_O_STREAM_MUSIC_DAT_1`; 001FB100 at step H. Seed the lanes with `em_stream_lanes_001F9820` at 001AAE40's start-up.
- **Why it matters:** voiced lines (director WP-10, Roger WP-9) and faithful music fades/volumes (H22's 0x1999 channel gain, the opening's 00119828(0/1, 0, 0)). **Since 2026-09-25 it is the only blocker of WP-10:** the director's prepared binding reproduces route 10 up to f1162 and stops at Roger's voiced line 0x7F (001FA5A0 unbound); a voiced line needs the ring push 001FA5A0, the lane service 001F9CF0 (D_008106F5 2 -> 1 when the voice starts) and the lanes' busy bytes D_00282155 / 156 (its end), i.e. items 1..6 of STREAM_LANES.md "Still missing".
- **Status (2026-09-25): DONE (live).** `em_stream_live` is the one owner of `em_stream_lanes_original` and
  `em_iop_stream` (STREAM_LANES.md "Live binding", IOP_STREAM.md "Binding notes"): boot at 001AAE40's start-up
  (IRX buffers, `sub_O_STREAM_MUSIC_DAT_1`, 001F9820 with the one-storage check of the four stream voices against
  the SFX driver's), the field at the top of every frame (`D_00810E90`, the IOP's RPC and driver ticks, 59.94 Hz
  pacing), step H's 001F9CF0 (skipped while `D_00821058 == 1`), the mixer in em_bgm's callback; 001FA5A0 / 001FAAC0 /
  the busy bytes bound in the message service; every legacy stream path replaced in the same change
  (em_opening_media's lane 0, em_bgm's tracks and resume, `w_001FABB0`, `w_00119828`, the 001B0C00 fades, the game
  over's 001FA790 / 001FAB50, the opening runtime's and the frontend's stops, the interaction host's status and
  abort events). The drive model gained one stated rule: a read the EE abandons without the break lands by the
  drive's next ready query (IOP_STREAM.md). Verified: the full level smoke (every live phase, the director's three
  beats and Roger's voiced conversation compared with routes 10, 11 and 13), `test-opening-runtime` over the real
  lanes, `test-iop-stream`, `test-stream-lanes`, newgame-control (9.599849, as HEAD), all `make test-*` targets.
  **Open:** (1) the drive's read time: by default the disc answers at host speed; with the PS2 disc-drive timing
  switch on (2026-09-27) the drive runs the model measured in the C7 stream capture (IOP_STREAM.md "Host speed and
  the PS2 disc-drive timing switch", "Drive model"). At host speed the voiced lines tear down exactly the drive's
  6 rows early (0x7F 8, with 2 rows of navigation) and the opening's stream request keys on 7 fields after the
  request frame. With the switch on, 0x97 / 0x99 tear down on the capture's rows and 0x7F 2 rows early (the
  original's sequencer served a lane-0 music refill first, a navigation-dependent phase), and the opening's request
  keys on after 17 fields, 5 of them waiting for the area music's read that the area-entry 001FAE70(1) issues. The
  original takes 27: it waits 15 fields, because that read's 16-field seek from the movie's position is outside
  the model. (2)
  0x1AE040's state 2 r == 1 and state 6 stay reported (UM_001FAE70); the state-0 area-entry 001FAE70(1) and the
  state-4 room move's 001FAE70(0) are bound since the rand() order audit (RAND_ORDER.md). (3) The rest of 001FB100 (the output-mode commit, the
  `D_00281B70` copy, 001FC6E0) is unbound; the mode bytes are 0 in every capture. (4) 001FC280's `D_00282160` cache
  is not modelled (an ambient loop other than -1 faults).
- **Verification:** the lanes oracle, plus a stream-state compare against the captures' lane blocks along the route.

### WP-9 Roger encounter live
- **Status (2026-09-24): LIVE for route 14 (census L22, FIRST_LEVEL_CENSUS.md section 1.10).** `em_area11_roger` binds 008237E0 / em_roger_tick and the equipment 001C5C90 over their original record bytes; the four programs run on `em_area11_script_host`; 0022EEF0 / 0022EC30 (em_cinematic_playback, now in COMMON) drive the bank 0x96 camera in top mode 3; the player's takeover runs 00183090 / 00182DF0 on the record (the special bank 0x96); the encounter stream is cue 29 on the stream lanes (the lane-0 stand-in until WP-8b) and 001FAE70(0) resumes the music there; Roger and the equipment draw through the rig path. The level smoke's `roger` phase equals route 14 f288..f1818 row for row. Beat 10's alternate 0x828990 and its voiced line 0x7F are live since WP-8b (with WP-10; the cage_roof phase equals route 10). Open: the armed talk 0x828810 (bound, no capture on the route), the departure 0x828A10 (not in the first visit; its op0F handshake is a fail-stop NULL), Roger's drop shadow (001DA6A0 reported). `em_roger_runtime` / `em_roger_assets` and `test_roger_encounter_reference` are deleted.
- **Scope:**
  - Bind the Roger owner (callback 0x8237E0, kind 10) in the host.
  - Supply the message (WP-8), camera (`em_cinematic_playback`; add it to the Makefile), player/face (`em_player_face_host` via the host) and frame workers.
  - Install the media, the clock and the resume stream (cue 29 during the encounter, 001FAE70(0) resume).
  - Draw Roger and the equipment child 001C5C90 through the rig path.
- **Originals:** 008237E0, 00823910/00823B70/00823C40, scripts 0x8283D0/0x828810/0x828A10/0x828990, polygon 0x82AB80, 0022EEF0, 001B7B30, 001FABB0, 001FAE70.
- **Verification:** `test_roger_reference` (6,912 controller cases), `test_roger_pose_reference`, `test_roger_cinematic_reference`, `test_cinematic_playback_reference`, `test_roger_media_reference`, the face host tests, plus a capture compare against build/startup-reference/roger-encounter.
- **Depends on:** WP-3, WP-4, WP-8.
- **Removes fabrication:** no. Adds a missing encounter.

### WP-10 Director beats as scripts
- **Status (2026-09-25): DONE (live with WP-8b).** Node #21 runs `tick_director_original` (em_director_original over em_area11_script_host) on the live path; `em_director.c`, `em_area11_flow.c`, their test, exporter and the `g.cine_*` state are deleted. The level smoke's cage_roof, crevice_prompt and east_tower phases are live and compared with routes 10, 11 and 13 to the end of each capture (Roger's 0x828990 and the voiced lines included); the only difference is the voiced lines' teardown, 8 / 6 / 6 rows early (the drive's read time, WP-8b open 1). The director verification run and `make test-level-smoke-director` are retired (superseded by the live phases' checks).
- **Status (2026-09-24): UNBLOCKED (WP-9 live, census L22).** The script host is live (L19) and Roger is bound: beat 0's 06/2 waits for D_00810813 = 1, which Roger's alternate script 0x828990 writes (route 10 f3460) once the director raises D_00810793; the director still runs on `em_director.c` (DIRECTOR_ORIGINAL.md section 6).
- **Scope:**
  - Run manager 008253F0's three scripts through `em_script`, using the same op bindings as WP-9: op00 kinds 0/1/5, op07 sub8/sub4/sub5, op06, 0x16, 0x18, 0D sub2, op0C via WP-8.
  - Implement 001C4760(1,1).
  - Letterbox via `em_frame_screen_fade_start`.
  - Delete kCineBeats, CINE_BAR_FADE and the chase re-seat.
  - Selector≠0 during beats blocks the menu (ORCH-19).
- **Originals:** 008253F0, 00825500/00825600/008256D0, 001B8FC0, 001B82D0, 001BA080, 001C4760, 001AEBE0.
- **Verification:** `test_script_reference`, `test_cinematic_playback_reference`. New work: an overlay-instruction oracle for the manager gates and a camera track compare (ORCH-13).
- **Depends on:** WP-3, WP-8, WP-9 (shared op07 bindings).
- **Removes fabrication:** YES.

### WP-11 Fan pair 00827630 and the AREA11 exit
- **Status (2026-09-28, census L24): the fan pair is DONE.** Both records run `em_fan_original_tick` on their nodes and draw their 001CAA00 units (FAN_ORIGINAL.md "Binding", SECURITY_GUN.md 5.3 / 5.4); the static em_pickup draw is retired; its `w_001B0C60` is `em_scene_request_area_change_001B0C60`. Open: the AREA01 targets; Roger's departure 0x828A10 (the exit bit's consumer) still faults at its op0F handshake (WP-9); the exit and hit boxes are not on the level smoke's route.
- **Scope:**
  - Translate 00827630: spin cycle, 0x451, hit box, the Roger 0x80 bit and the exit request.
  - Port the area-change consumer: D_008106B5..B8 → 001AD010 → 001ADF50 load wait → frame case 0 → 001AFCA0 → 001B07C0 spawn placement (ORCH-06). **Done in WP-3 S12a** (bind the fan's `w_001B0C60` to `em_scene_request_area_change_001B0C60`); the AREA01 targets remain.
  - Export the targets AREA01 sub 1 and sub 0 at entry 4.
- **Originals:** 00827630, 001B0C60, 001AD010, 001ADF50, 001AFCA0, 001B07C0, 001E7780.
- **Verification:** new overlay-instruction oracle for 00827630 (pattern: `test_roger_reference` overlay loading); RAM captures for spawn placement.
- **Depends on:** WP-3; the Roger departure exit also needs WP-9.
- **Removes fabrication:** YES (H20). Adds the exit (H21).

### WP-12 Truck set piece
- **Status: DONE (census L23, 2026-09-24).** Both owners are live on their nodes with the AREA11 script host (L19); the level smoke's `truck_preview` / `truck_crossing` equal routes 07 / 08. Open: the effects (L26) and the sounds 0x454 / 0x455 (WP-14).
- **Scope:** translate 008251E0 (camera script 0x8292C0) and 00823FF0 (stand-on arm, shake, fall beats, Z-only carry, sounds 0x454/0x455, FX, rumble, persisted 0xFF), and publish its hull instead of the port's AABB carry (P19).
- **Verification:** new overlay oracle.
- **Depends on:** WP-3, WP-10 (script host).
- **Removes fabrication:** YES (finishes H16).

### WP-13 Render fidelity
- **Scope:**
  - Fog verification (H17).
  - Actor re-exports with real normals (H18).
  - `door_original` model (W09/R21).
  - Decode the level alpha/test/blend state (R10), the level vertex-colour scale (R11), and TEX1/CLAMP (R25).
  - The 001D8270 fold gate and +0x98 light node (R13).
  - Menu identity camera and yaw π+t (R05/R06).
  - Additive actor RGB (R07).
- **Verification:** `audit_opening_lighting.py`, `test_point_light_reference`, plus GS-dump comparisons from opening_gs.bin.
- **Depends on:** WP-3.
- **Removes fabrication:** YES (the stand-in light and menu orbit).
- **Render + UI step (2026-09-25):** the level background is live (R09 above);
  the indicator children run per node (docs/CENSUS_UNVERIFIED.md): the
  terminal arrow turns green once powered, as in route 04, and every child
  draws its 001F54E0 in walk order. Still open in this area: the object-unit
  draw P1/P2 for the owners not on it yet (docs/OWNER_DRAW.md section 11;
  the crates, drums, truck and fence door are on it since the object-unit
  step below), the player drop shadow (live since the shadow step
  below), the
  canonical render context (L32 / L30), the BATTERY page draw (needs a
  record-level 002149F0), the mode-3/4 presenters (their data containers'
  disc files are identified, docs/CENSUS_STANDINS.md: chunk00/f02_id02.bin,
  chunk00/f03_id03.bin, chunk03/f15_id17.bin; the exporter extension and the
  binding remain), 0021BAE0, and WP-8b.
- **Render context step (2026-09-25, census L32 + L30; docs/RENDER_CONTEXT.md
  section 8):** the one canonical render context runs live
  (`em_render_context_live`): main-loop step B 001D1AE0, the frame head
  001D1C50 (P / V / K, the four 001D2D20 projections, the planes, the P / K
  scratchpad copies, 001D30A0's skin-record fills, the fog programmer's mode
  0), the frame close 001D1EA0 (001E0D70, 001DDA00 with 001DDE10's four-sprite
  packets, the kick 001CB800 that splices and clears the chain table), the area
  render init 001C1DC0 (flags, the area fog 001D8FD0, 001C1F50), the zoom
  writers 001D25F0 / 001D2610 and 001DD980's store 001DD950, all on one block
  whose .data comes from the user's ELF (tools/export_render_context.py). The
  world pass draws with the frame head's view and zoom (the original's
  one-frame view lag, measured in the captures and checked every tick), the
  Metal fog reads the context's coefficients and FOGCOL, and the snow and the
  AREA11 effect take P / the 001CD370(0) projection / K from it. Removed:
  `g.cam.zoom`, `em_camera_scope_zoom`, `em_snow_projection_matrices`, the
  camera's EmInteractionProjection copy and the interaction host's hard-coded
  zoom. Not bound: 001C1D00 (the static-object bank export and the static draw
  boundary), 001D19E0, 001D1EF0 (flag 3 needs step V 001D2300), 001D2580,
  the status page's fog save / restore; RENDER_CONTEXT.md 8.4. The effect
  binding (L26 / L27 / L39) is no longer blocked on the context.
- **Effects step (2026-09-25, census L26 / L27 / L28 / L39):** the effect,
  head-sprite and equipment originals run live over the context and build
  their packets byte for byte into its chain table (status update in
  section 1). Still open here: a renderer stage that draws the effect
  chains (the VU1 programs of table 0x231770 / 0x233290) and the equipment
  nodes' own 001CAA00 draw.
- **Object-unit step (2026-09-25, OWNER_DRAW.md P1/P2; docs/OWNER_DRAW.md
  sections 6..12):** the crates, drums, truck and fence door draw the
  original's own units. Their +0x4C runs 001CAA00 live
  (`em_owner_draw_live`: 001CA7B0, 001D8C20, 001C7420 with the bit-exact
  001D89D0, 001D1F80 and 001CA940 over the AREA11 model bank, into the
  render context's packet arena), and `em_gfx_object_unit` runs the object
  kernel 0023C750 and its clip pass 002354A0 on the CPU
  (`em_object_unit_run`) and rasterizes exactly the triangles they kick in
  GS class 0 (HIGHLIGHT, bilinear, REPEAT, alpha test, fog), with the
  textures decoded from GS memory (`tools/export_object_textures.py`: 137
  TEX0, identical in all 15 route captures). Evidence: every triangle of
  the 119 captured owner units and of the 60 captured face units equals the
  original VU1 microcode's (tools/test_object_unit_reference.py); the Metal
  output equals a model of the documented GS pixel path on 99.7 % of the
  interior pixels of beat 03's 15 units (tools/test_object_unit_gpu.py); live, the
  units' colour matrix and rig lighting lanes equal the route snapshots',
  and in the camera-exact beat 10 so do the drawn set and the position rows
  (LEVEL_SMOKE.md check_owner_units). skin_arena_init (001D2E20) runs at the
  area load (the skin records' templates). Retired: the crate / drum /
  truck EMDL meshes and the fence door's mesh upload. Still open: the
  player (done by the player step, 2026-09-26: its model, owner view and
  equipment on this path; em_weapon's bone lookup on the record),
  Roger (001CB3C0's face builders; the face program itself runs),
  the legacy-drawn owners (elevator, panel, pickups, fan, the security gun
  and its cable, parachute,
  001C4820, the indicator children) until their owners are live, and
  Metal's rasterization (not the GS DDA; no GS framebuffer exists in the
  captures to compare with). OWNER_DRAW.md sections 11 and 12.
- **Shadow step (2026-09-26, census L29 + L29b; docs/SHADOW_ORIGINAL.md
  "Binding", SHADOW_ACTOR_ROUTE.md section 4, SHADOW_DECAL.md section 5):**
  the player post-step 0015C160 runs at its two variant positions
  (`w_0015C160`: the D_008102B1 gate, 001CB590, D_00810771 and the +0x214
  route), and the player's drop shadow is live (`em_shadow_live`): with
  +0x214 == 0 the projected shadow 001DA6A0 over the player record, its node
  records and the render context, drawn by the Metal passes (alpha clear,
  the two boxes, the 128 x 128 silhouette, the receivers with the 0023E8A0
  re-pass) after the level and the walked actors; on an actor (the
  elevator, a crate, the truck) the 0015BF90 decal (001F9100, 001F8D30,
  001CE300 into page D_007635C0) drawn by `em_gfx_shadow_decal_fan` at the
  page splice (since WP-13 by the chain page's consumer with the rest of
  the page, docs/CHAIN_PAGE.md). The player's own draw moved behind the shadow (0015C160's
  +0x4C). Evidence: the level smoke's check_shadow (every post-step's route,
  every draw flushed, the first-control frame drawn; the original 001DA6A0
  and 0015BF90 + 001CE300 re-executed over the port's sampled inputs give
  the port's plan and packets), test_shadow_decal_reference's Metal pixel
  check, newgame-control 9.599849 and the frame trace unchanged. Still open:
  the post-step during the opening (reported: the opening runtime owns the
  displayed player, design risk 2), Roger's 001DA6A0 (kind 0x29 proxy not
  exported), the decal texture's uploader (exported from the route
  captures' GS memory), and Metal's rasterization of the passes (SHADOW_ORIGINAL.md
  "GS side").

- **Status UI step (2026-09-26, census section 1.21; docs/STATUS_PAGE_RECORD.md
  section 7, CENSUS_STANDINS.md 3, MESSAGE_PRESENTER_REST.md 3,
  CENSUS_UNVERIFIED.md, BACKGROUND.md "Wiring"):** the ITEM > BATTERY page
  is the original 002149F0 (`em_battery_page_live` over the one UI block
  D_00810130), drawn by its own routines 0020AE40 / 0020B210 / 0020B0D0 /
  0020CCB0 with 00209280, their leaves submitted from the EMBA atlas by TEX0
  (`em_battery_ui`, now the leaf list); the pop-up notice and the
  confirmation lines come from the original presenters. The mode-3 / mode-4
  presenters (001FD0E0 with 001FDDB0, 001FCB90, 001FCF90, 001FCF60) run at
  step F (`em_message_presenters_live`); the status pages' message words
  are a view of the live block, so the step-F gate stand-in and the page
  layer's copy are deleted (WP-8 decision (b) done). 0020DFA0's fog save
  0021BAC0 / program 0021B9A0(5) and 0020E080's restore 0021BAE0 run on the
  live render context (the fog record's owner); the CONFIGURE path runs
  0020DFA0's callees in its order (001AFE60, 0020E020, then the rest). The
  level background draws on the context's render flag 0x20 as 001D2300
  gates it, and flag 0x21 for 001E0DF0's +0x1D8 list (built only under
  0x20 / 0x21 by 001C1D00's 001E0CF0, which is not bound; BACKGROUND.md),
  and its TEX0 / RGBAQ (the manifest line only names the asset). The indicator children bind their model and bone slots
  (001C2360 / 001C22A0) and place them (001C6380) through
  `em_indicator_bind_live` (models 0x73..0x75 / 0x7A now in the Roger
  export). The terminal-screen colour needs no change: red in the refusal
  as route 02, the green in the elevator/refusal fixture is its powered
  seeding. Evidence: the level smoke's battery and panel phases row for row
  on the original page; check_render_context's save-slot rows;
  check_indicator_children (records and placements bit for bit; the
  terminal's child before the elevator scan only); tests/status_runtime_test.c
  on the bound page (Yes / discharge, default No, Back / ITEM / reselect,
  the module 0x32 gate, the charge and capacity write faults, the pickup);
  the render conversion against the captured confirmation packets
  (STATUS_UI_LEFTOVERS.md 1.1 step 3); newgame-control 9.599849 (as HEAD);
  compare_frame_order verdicts equal to HEAD's. Retired (rule 4): em_battery_ui.c's page, em_panel_battery_begin /
  _step, tests/battery_ui_test.c, test_battery_reference.py,
  test_battery_pickup_reference.py, test_battery_ui_reference.py. Still
  open: the indicator children's own draw (OWNER_DRAW.md P1) and the
  terminal's copy of its node matrix into its child's slot; the hub's /
  ITEM page's help-line stand-ins remain only for the fixtures.

### WP-14 Audio
- **Scope:**
  - Re-export the pitch (H19) and Q14 gains (AM-02).
  - General A0 sequencer with loop-aware VAG voices, SPU2 ADSR and Gaussian interpolation (AM-03/26/27).
  - Area-scoped banks from D_00264A70/D_00264AD0 with absent entries (AM-15). This removes the office 0x7D8 stand-in (AM-14).
  - UI cues 0/1/2/5/0xB/0xD (AM-07).
  - 00117428 allocator (AM-18).
  - 001FC3C0 looped positional voices for the flame 0x413 (AM-12).
  - Full 001FAE70 branch selection (AM-21).
  - Reverb bus (AM-04).
- **Verification:** `test_area11_sfx_reference.py` pattern, extended to every exported id. New: 001FBF50 gain oracle (AM-19).
- **Depends on:** partly WP-4/5/6 for call sites.
- **Removes fabrication:** YES.

### WP-15 Player locomotion completeness
- **Scope:**
  - Reversal skid (H11).
  - Ordinary display from `EmPlayerPose` + 0017B660 instead of matrix lerps (P12/P13/P27/P29).
  - Footsteps from the remaining clock and 00187350 (P14/P15).
  - 001764E0 walk-gated probes and 001760C0 (P16).
  - 00175900 floor service and 001796C0 fall state (P17/P18).
  - Aim release states (P24), R2 stance via 001703E0 and R1 via 0016FCF0 (P25/P26).
  - Single-tick melee chain (P28).
- **Verification:** extend the `test_player_*_reference` oracles. New oracles for 0017ABA0, 00187350, 001764E0, 00175900.
- **Depends on:** WP-2.
- **Removes fabrication:** yes (P14 timing, P26 attribution).
- **Status (2026-09-24, Boxes step):** P16/P17/P18 are live in AREA11. The
  floor service, the fall check and 001764E0's original workers run over
  the collision world (FIRST_CONTROL.md "Engaged"). The Use chain's ledge
  climb reproduces route 05. P12/P13 (the ordinary idle/walk display) stay
  the legacy baked display until L12.
- **Status (2026-09-24, census L09..L11):** the cage ladders (0015D4C0
  case 0x32, 00165B60, 001662D0), the tank, pipe-end and east tower ledge
  climbs, the crevice running jump (0015EC50, 001634A0 with the recovery
  lane) and the walks' falls run live on the record and equal routes 10..13
  row for row (LEVEL_SMOKE.md: cage_ladders, crevice_climbs, crevice_jump,
  east_tower_climb; `make test-level-smoke-full`). The data they needed,
  the grid nodes' +0x34..+0x3F axis, is in the EMCL since this step
  (STARTUP.md step 13). Beat 14's tower jump waits on the roger phase
  (WP-9).
- **Status (2026-09-25, census L12):** H11 (the reversal skid), P12/P13 (the
  ordinary idle/walk display: 0017C030 / 0017B660 over the record) and
  P14/P15 (00187350 from the record's clip clock) are live in AREA11; the
  first-control record equals the original's on 56 callbacks
  (test-first-control-reference). Open: P24..P28 (the aim, R2, R1 and melee
  states stay the port's stand-ins, L28).

### WP-16 Camera completeness
- **Scope:**
  - Run the 0018D330 prepass for every style and consume the 0x5A/0x6D/0x60 bits (CAM-10).
  - 001921D0 tail for codes 1/3 (CAM-08).
  - 00195130 case 0xB AREA11 specials (CAM-09).
  - L1 via 00193EB0 gating plus 001936E0 (CAM-11).
  - Mode-8 settle and the 001B0460 entry seat, deleting opencam (CAM-18/19).
  - 00197490 aim release (CAM-16).
- **Verification:** `test_camera_live_reference`, `test_camera_interaction_fixture`, the level smoke's camera rows (`check_first_control_camera`, `check_follow_after_release`).
- **Depends on:** WP-3.
- **Removes fabrication:** yes (CAM-06/07/08 stand-ins).
- **Status: LIVE in AREA11 (2026-09-25, census L13..L16; CAMERA_LIVE.md,
  FIRST_LEVEL_CENSUS.md section 1.11).** `em_camera_live.c` binds
  em_camera_follow_original, em_camera_leftovers(_solver),
  em_camera_area11_specials, the new 0018C0D0 / 00102798 / 00193660
  translations and the look-at 00102CD0 (em_cs_00102CD0) on the canonical
  camera block and pool: the 0018D330 prepass for every style (CAM-10),
  001921D0 (CAM-08), 00195130 whole (CAM-09), 00193EB0 / 001936E0
  (CAM-11), the mode-8 settle and the 001B0460 seat (CAM-18/19). Evidence:
  the area load's seat and state-0 frame and the hand-off settle byte for
  byte against the per-frame samples; the follow camera after every
  release (LEVEL_SMOKE.md). Retired: em_camera_probe, the host-math commit,
  em_mat4_lookat_gs, test_camera_probe_reference,
  test_camera_commit_reference. Open: the aim release 00197490 (CAM-16) and
  camera actions 1/2 (aim), 5, 9..15 fault with their addresses (not on the
  route; the port's own aim stays a named stand-in, L28); the director,
  examine and door-cinematic owners pre-empt action 0 through named
  stand-ins (L21 / L18); scenes after the level exit keep the legacy
  camera. The playable_ee.bin fixture was not needed: the per-frame
  samples and the route traces cover the gameplay frames.

### WP-17 Input and startup
- **Scope:**
  - 001B5940 analog block in `frame_input_read`: D-pad → stick with gait 3 (SI-03/P22), stick → D-pad bits (SI-04), quantize (SI-05), repeat word 0x810E78 (SI-06), raw axes (SI-08).
  - New Game movie skip on START only (SI-09). Since S12a the movie is requested by the game task itself (001AD360 step 1: D_00275C78 = 0, D_00821058 = 1 → `em_frontend_movie_request`), and the skip mask is 002036E0's with spad 3B90 = 2.
  - Black plus the 001ADF50 loading shimmer instead of fade-in 4 (SI-10). **S12a: PARTIAL.** 001ADF50 runs natively (001AED80 clear, the veil state machine 0021B180/0021B550/0021B840, 001AEDB0 full black) and the state-0 rebuild runs 001AEE40(4), as captured; the veil's particles (0021B1B0/0021B500) are not drawn, so the load shows black.
  - Attract completion (SI-11).
  - RNG seed: cold boot is 1, not 0x45 (SI-01).
- **Verification:** new oracles for 001B5940 and 001AC480.
- **Depends on:** nothing.
- **Removes fabrication:** YES (SI-01/08/09, P22 aim-only d-pad).

### WP-18 Enemies and hazards verification
- **Scope:**
  - Oracles for crates 001551B0 (alert, leap, husk rebind 0x22/0x29; drop the gibs; W19/W21/INV-19) and drums 00156620 (FX, no debris; W20).
  - Gun cable taken-bit persistence and 0x426/0x427 (W17 residue; bound in census L24: SECURITY_GUN.md 5.2).
  - AREA11 crawler mesh param (W18).
  - Flame contact damage 00823580 through the +0x224/+0x0F/+0x00 contract, not the 0x4000 mailbox (INV-17/INV-28). Since census L01 the stage's 0021C440 consumes that contract; its hit paths still reach fail-stop workers and unbound +4 = 2 states (PLAYER_STAGE_WORKERS.md section 2.1).
- **Depends on:** WP-3.
- **Removes fabrication:** yes (gibs, the 0x7D8 stand-in).
- **Status (2026-09-24, census L25):** the crates and drums are live on their original owners (CRATES_DRUMS_ORIGINAL.md "Binding"). The legacy gibs and the 0x7D8 stand-in for them are retired in AREA11. Their break is inert: no live code writes +0x36, and its effects need L26. The flame's contact damage remains; the security gun and its cable (L24, then "the husk pair") are live on their owners since 2026-09-28.

---

## 5. Refuted and uncertain items

### Refuted (do not re-report)
- **ORCH-05, "the AREA11 door is the level exit; route it through an area transition": REFUTED.** 001BC150 with door id bit 7 clear (captured RAM, all AREA11 captures) is a **same-area room move** (B8=2) to spawn entry 2 or 1. It is never an area change. The surviving defect is medium: the port re-places at a computed point about 10 u off with the wrong yaw and never writes 0x810702 (tracked in WP-7). **Fixed by S12b (2026-09-23):** the port now re-places through B8 = 2, 001AD010 (0x810702) and 0x1AE040 state 4's 001B07C0(1), matching the route capture's re-place (f472).
- **W17, "the door husk pair should be an active set piece in the first level": REFUTED.** The pair is a security gun (00825940) and its power cable (00827490), not creatures (decomp verify-area11-husks, 2026-09-27; SECURITY_GUN.md). The gun stays dormant in state 0x64 while event flag 0x30 (D_00810788) is clear. Every AREA11 capture shows the flag at 0, the gun in state 0x64, and the cable in state 1. The dormant gun is faithful for the first visit. The residue it listed (the cable's taken-bit persistence, its shot reaction, the lamp child 0x7A) is bound since census L24 (2026-09-28) except the hit's 001EFE00 node chain (fail-stop, unreached: no live +0x36 writer) and the lamp's own draw.
- **R03/R04, "the boot ELF draws nothing for the flashlight; gate the spot and cone off": REFUTED.** 0017A970 sets D_008106C7 with D_00810D3C; 00188ED0 calls 00187780 while D_008106C7 is set; 00187780 calls 001D9530, which draws the cone shell (chunk27 meshes 0x10/0x11/0x16) under the gun light matrix, skipped only when 001B0070() & 0x20000000. In AREA11 that bit is set (D_008106C8 = 0x20081910 in save-state captures 02–15, ORIGINAL_FRAME_ORDER.md P31; 0x20089910 in the 09_fence_door route capture, RENDER_CONTEXT.md and PLAYER_REACTION.md), so the first level skips the cone shells; AREA01 (0x8D00 / 0x8D01) draws them. The original cone is not translated; the port's spot term is a stand-in.
- **INV-08, "manager 1 (00823CE0) second cinematic reached in the first level": REFUTED.** It only waits on flag 0x30. Within AREA11, only its own script sets that flag, after the cinematic has already started. The only op06 sub0 record that sets it to 1 is in AREA17.BIN. This is revisit content, low priority. INV-09 (manager 3, 008257A0) has the same D_00810788 gate and is therefore also revisit content (not adversarially checked, but it goes dormant when D_00810788==0 per its C).
- **Partial corrections to confirmed items. Do not repeat the original wording:**
  - ORCH-10/CAM-17: kCineBeats values *are* original record data.
  - ORCH-07: the `em_status_hub_ui.h` declarations are now defined.
  - UI-05: the request global is B1=0x80+cost via 00157F60, not C5.
  - AM-01: the problem is a wrong derivation, not an invention.
  - R02: an approximation, not a fabricated feature.
  - P20: the staging point is on the near side.
  - W10: bind is test-only.
  - W22: fades do happen live via the legacy door. The host binds only panel and elevator.
  - AM-06: medium.
  - INV-02: there are two exits, not one.

### Uncertain or unverified. Evidence that would settle each
| Item | Open question | Settling evidence |
|---|---|---|
| face-host limitation | B82D0 op7/8/11/12 immediate route calls B81D0 while 3B8F==0; does the AREA11 Roger encounter take it? | Trace ev+0x14 in scripts 0x8283D0/0x828810 in the oracle; roger-encounter capture of 70003B8F. |
| face-host limitation | Priority order in cinematic_player (deferred foreign bank vs shared animation vs idle) | Show from 0015B130/0015BA50 whether a shared animation and a foreign-bank request can co-occur. |
| roger-media boundary | D_008106F5 modes 1/2 and voice lookups 001FD580/001FD6A0 are never exercised (voice −1) | Oracle case with a non-negative voice record (e.g. director lines 0x97/0x99, voice cues 150/149). |
| status-hub-ui boundary | Unsupported ammo selectors (primary≠2, secondary>4) use an inherited register as TEX0 | Capture with those selectors, or proof they are unreachable in AREA11. |
| R01 | Is the status-menu actor draw fogged? | GS dump of the status-hub capture (FGE bit on the menu actor PRIM). |
| R11 | Level vertex colour 1.0 → GS 128 or 255? | Compare RGBAQ in opening_gs.bin against record colours. |
| CAM-09 | Is arm 1 of the 00195130 case 0xB (y<185, z<220, 359<x<394.8) floor reachable? | Collision query at that XZ against AREA11 EMCL. |
| P31 | Does AREA11 enable the passive hazard drain (D_008106C8 & 0x60)? | Read D_008106C8 in playable_ee.bin. |
| SI-27/AM-21/INV-24 | Which 001FAE70 branch AREA11 takes (D_008104E4, weapon id, D_008106C8 track) | Read those globals in the handoff and playable captures; oracle 001FAE70. |
| W18 | AREA11 crawler model param → mesh | Per-area model table (chunk15/f05_id97 +0x5000) against the crate record param; capture model pointer. |
| INV-27 | Record 20 (001C4820) mesh binding | Same capture method as the canopy (OPENING_SCENERY.md). |
| SI-22 | Does op 0x14 spawn (001BAC00) frame coincide with op10 sub1 visibility? | CONTINUE flags in the 0x828FC0 records; opening capture frame index. |
| ORCH-22/SI-28 | Main-loop phase order has no oracle; step I (001B5B70 rumble countdown) runs since census L23 (after step G; step H 001FB100 is still missing) | Instruction trace of 0x001AAE40 over a captured frame. |
| INV-02 | AREA01 sub 1 scene is not exported | Exporter run for area 1 sub 1 from the user's disc. |

### Label and documentation corrections found (status as of 2026-09-22)
- Port:
  - CLAUDE.md gait-hold paragraph (P30: gait 1 translates at 0.1). CORRECTED (9f00668).
  - CLAUDE.md "DECODED + LIVE-VERIFIED" door camera (CAM-06). CORRECTED (9f00668); the style 5/1 wording was made exact by the cleanup-game lane.
  - Stale "eye +9 / target +8" (CAM-08). CORRECTED in CLAUDE.md (9f00668, walking camera heights).
  - `em_camera.c:366` calls 0022EEF0 "scope/sniper" (it is the cutscene timeline). CORRECTED (9f00668).
  - `render_chain_build` is labelled 001D1C50 (that function is the per-frame fog/GS setup). CORRECTED: `em_game.c` now calls it a port-only collector that runs at the 001D1C50 slot.
  - `em_hud.c` says 0020CDC0 is undecompiled. CORRECTED (9f00668); the same claim in `em_hud.h` and `em_game_internal.h` was corrected by the cleanup-game lane.
  - `em_sfx.c:477` "NOT YET CALLED" is stale. CORRECTED (9f00668).
  - `em_random.c` / `main.c` 0x45 seed attribution (SI-01). CORRECTED (f338b46: the cold-boot seed is 1).
  - `em_opening_runtime.c` op 0x14/op6 handler addresses (SI-22). CORRECTED: the handlers now cite ftab_0024D880[6] = 001BA080 and [0x14] = 001BAC00, which match the ELF table. The open SI-22 timing question in the table above remains.
  - Also corrected by the cleanup-game lane: `em_game.h` "0x1AE040 still undecompiled" (it is NEARMISS `anim_frame_top_b.c`), the D_00810811 "battery" docs, the director's op0C "music" names (now message lines), the fog comments (FOGCOL 0..255, near -209, F per vertex) and the "boot ELF draws NOTHING" flashlight comment.
- Decomp: the four function headers are CORRECTED in decomp 7ae3b07; the FINDINGS item carries a CORRECTION paragraph in "ENGINE FRAME ANATOMY".
  - 001FAE70 "reticle selector": it is the stream-resume cue selector.
  - 0020CDC0 / 002149F0 / anim_frame_top_b "save/load / title-attract": they are the status/battery/in-game frame machines.
  - 001FBC50 "subsystem init": it is stop-all SFX.
  - 001735C0 "boss machine": it is the player light melee.
  - FINDINGS "ENGINE FRAME ANATOMY" lists steps N/O as unconditional; they are gated on D_00821058==1. CORRECTED.
