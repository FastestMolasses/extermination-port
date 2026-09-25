# Level smoke: the first level, live, phase by phase

Step S13 of SCENE_COORDINATOR_DESIGN.md (2026-09-23), extended by WP-4 (the
elevator refusal, the panel and the elevator ride), census L25 (the
boxes: the Use chain's ledge climbs onto the crates' original owners) and
census L03 (the hill slide). The smoke plays the
port's first level headless from New Game along the original route and checks
each phase twice:

- in process, against original facts it can observe;
- against the original captures, by replaying the run's tick log.

A phase whose original owners are not live in the port yet reports
**NOT-LIVE**. The report names the step it waits on and the binding the port
runs for that owner today. A NOT-LIVE line is not a pass: nothing past it has
been verified.

A NOT-LIVE phase that a later live phase needs the state of is **driven**:
its runner plays it through the owner's current port binding, the run reports
`NOT-LIVE driven`, and the capture checker skips it. No phase is driven since
WP-6 (the battery pickup is live).

## Running it

```sh
make test-level-smoke                  # the whole route (about 11 s today)
EM_LEVEL_SMOKE_UNTIL=first_control make test-level-smoke
EM_LEVEL_SMOKE_UNTIL=status make test-level-smoke
EM_LEVEL_SMOKE_UNTIL=elevator_refusal make test-level-smoke
```

The make target does the following:

1. It runs `EM_UNCAPPED=1 EM_STARTUP_TEST=newgame-level EM_AREA_CHANGE_LOG=build/level_smoke/ticks.jsonl build/extermination`.
   - The app is headless because an `EM_*TEST` variable is set.
   - The frontend drives the title menu and the movie skip, as it does for
     `newgame-control`.
2. It prints the run's `level smoke:` lines.
3. It runs `tools/test_level_smoke.py` over the run log and the tick log.

`EM_LEVEL_SMOKE_UNTIL` takes a phase name from the table below. The default
is the last phase. An unknown name fails and lists the phases. The run stops
at the first NOT-LIVE phase, because every later beat starts from the state
the earlier beats leave. After the last phase it runs one more frame, with no
input, before it quits: the route captures sample after the original frame,
and the original ticks the 0x28A9A0 fade after the slot-0 task, so the port's
post-frame fade is the next tick's start sample in the tick log. The capture
check of the last phase needs that tick (for example the first-control fade
block when `EM_LEVEL_SMOKE_UNTIL=first_control`).

Files:
- `src/game/em_level_smoke_test.{h,c}`: the in-process driver. Its phase
  table is `k_phases`. It is hooked beside em_opening_control_test: begin in
  `em_game_install_new`, and after-frame at the end of `frame_close_out`,
  which is the 001D1EA0 position of every world and status frame. A latched
  scene fault stops the game task before `frame_close_out`, so the task's
  fail-stop return in `em_scene_task_001ACEC0` calls
  `em_level_smoke_test_scene_stopped` (and
  `em_opening_control_test_scene_stopped` for newgame-control) instead. The
  run then ends with `FAIL ... the scene coordinator faulted at <address>` and
  a nonzero exit rather than waiting for its next phase (for example when
  `assets/scene_snow/interaction.emis` or `assets/spawn/spawn_table.emsp` is
  missing). Nothing is called while no fault is latched.
- `tools/test_level_smoke.py`: the capture checks. Its phase list is
  `PHASES`.
- `em_scene_bindings_pool_binding()`: the current binding of an owner, for
  the NOT-LIVE lines.

## Phases

The phases follow the main line of FIRST_LEVEL_ROUTE.md section 3. Side beats
00 and 09 are not phases: beat 09's room move has its own capture test,
`make test-room-move-reference`.

| Phase | Route beat | Original owners | Live | Waits on |
|---|---|---|---|---|
| first_control | 01 row f0 (slot 04) | 0x1AE040 state 1 / 001AE5E0, 49 pool nodes | yes (S12a) | — |
| status | 01 status exit; frame_trace2 `status_04.json` | 001AE7E0 r==2 → state 3 → 5 → 1 | yes (S11b; the original page core, hub, models and 0020E0C0 exit since WP-5) | — |
| battery | 01 | pickup 00219550 g0.0, take script 0x266620, ITEM page | yes (WP-6; the status pop-up since WP-5) | — |
| elevator_refusal | 02 | terminal 0x827B10, script 0x82A990, message 0x8000001A | yes (WP-4) | — |
| panel | 03 | panel 00159210, scripts 0x2477A0/0x247BE0, 00157F60 BATTERY page, power 0x80 | yes (WP-4) | — |
| elevator | 04 | terminal 0x827B10, script 0x82A750, carry 0x828050 | yes (WP-4) | — |
| boxes | 05 | Use dispatcher 00160220, ledge climb 0015DF10 / state 2 onto crates r4/r3 (001551B0) | yes (census L25) | — |
| slide | 06 | floor class 0x1000 -> 001796C0, slope slide 0016C6A0 (state 0x1C, +1F0 0x30) | yes (census L03) | — |
| truck_preview | 07 | trigger 0x8251E0, camera script 0x8292C0 | no | WP-12, WP-10 |
| truck_crossing | 08 | truck 0x823FF0 | no | WP-12 |
| cage_roof | 10 | ladder, director 0x8253F0 beat 0, Roger 0x8237E0 script 0x828990 | no | WP-15, WP-10, WP-9 |
| crevice_prompt | 11 | director beat 1, script 0x829A40 | no | WP-10, WP-8, WP-15 |
| crevice_jump | 12 | running jump (+1F0 0x0C) | no | WP-15 (no module) |
| east_tower | 13 | director beat 2, script 0x829CC0 | no | WP-10, WP-15 |
| roger | 14 | Roger 0x8237E0 quad 0x82AB80, script 0x8283D0 | no | WP-9, WP-15 |

## What the live phases check

### first_control

This is the end of the first world frame after the opening runtime finishes
with the fade clear (the newgame-control handoff).

**In process:**
- task +8/+9/+B/+C = 3/1/1/0;
- selector 3B8D = 0;
- pool census 49 (ORIGINAL_FRAME_ORDER.md section 4);
- area 0x0B room 0;
- no scene fault.

**Against the captures:** the first-control tick is found in the tick log by
the D_00810750 value the run prints.
- Against `status_04.json` frame 0 (the original's idle gameplay frame from
  slot 04): the task bytes, the spad bytes 0x70003B8C..93, D_008106B0/C5/CE,
  and one 001AE5E0 in the tick.
- Against route 01 row f0: the spad bytes, D_008106B0..B9, the area bytes,
  and the eight bytes of the 0x28A9A0 fade block (step 4, as the opening
  leaves it), read from the start sample of the next tick.

### status

START is pressed at the end of the first-control frame. After 30 status
frames TRIANGLE is pressed. The hold length is test input, not game
behaviour.

**In process:** every status frame is state 3 sub-step 1, with the world
frozen. That means:
- no D_00810750 increment;
- player position and camera eye unchanged;
- 3B8D stays 0 (Q7: a status screen opened from gameplay keeps it).

After the close:
- one gameplay variant per frame in state 1;
- B0 and C5 clear;
- the phase passes when the fade is idle again.

**Against the captures:**
- The ticks around the open (+B becomes 3) and the close (+B becomes 5), two
  before to three after, must equal `status_04.json` in:
  - task bytes;
  - spad bytes;
  - B0/C5/CE;
  - whether 001AE5E0 ran.

  This covers the r==2 tick without a frame, state 3 sub-step 0, the
  state-5 tick without a variant, and the first 001AE5E0.
- Every tick between is state 3 sub-step 1 without a variant.
- The exit fade, from the first substate-2 row until the fade is idle, must
  equal route 01's status exit row for row (f484..f495): 001AEDB0 at the
  close, 001AEE40(0x20) in state 5, then the 0x20-per-frame ramp.

- The close latency: counted from the tick whose D_00810E74 holds the
  START/TRIANGLE edge (& 0x810), the close must take as many ticks more
  than the open as in `status_04.json`. There the presses at f10 and f200
  (`frame_trace2/exp_status.py`) gave r == 2 at f12 and +B = 5 at f204:
  two ticks more. Since WP-5 the port's close runs the original exit: the
  hub's edge frame enters phase 5, then 0020E0C0 runs case 0 and returns
  nonzero from case 2 (NEARMISS C `src/func_0020CDC0.c`, byte-matched
  `src/func_0020E0C0.c`), so the port also takes two. (The capture held
  TRIANGLE four frames, the smoke taps it for one; the close is
  edge-triggered, so the hold does not matter.)

The hub shown in between is the original em_status_hub (WP-5). The phase
asserts its draw cadence: a frame whose tick began at sub-state 1 steps
0020A7A0 once (`em_status_background_live_steps`) and draws 00209DF0 once
(the UI+0x20 clock equals that count); the cold entry, sub-state 0 and the
0020E0C0 exit frames draw nothing (29 hub draws in the 30-frame hold). It
also asserts the status models (`em_status_models`): the pool holds the
status-hub capture's seven records (the menu player 0020E6F0, then the
letters 0020E460 with the glyphs '/', '@', '0', '1', '2', '8'), and every
hub frame after the first draws each record once (the first walk only
initialises them). `EM_LEVEL_SMOKE_HUB_CAPTURE=<file.bmp>` (with
`EM_LEVEL_SMOKE_UNTIL=status`) writes the hub frame whose walk equals the
capture's (walk 10) for an image compare with
../Extermination/build/startup-reference/status-hub/hub.png
(STATUS_SCENE.md section 7). `EM_LEVEL_SMOKE_MESSAGE_CAPTURE=<file.bmp>`
(with `EM_LEVEL_SMOKE_UNTIL=elevator_refusal`) writes the frame whose step F
presents the refusal line 0x8000001A for the fifth time, the state of the
elevator/refusal capture; `make test-message-capture` compares its text
lines with that capture's screenshot (MESSAGE_GLYPH.md).

### Navigation (the route phases)

The runners drive the analog stick and the buttons through the gamepad
overlay (`em_input_set_gamepad`, the pad path a DualShock takes), with the
closed loop of route_capture.py: the stick points at a world (x, z) target
relative to the camera forward D_00810600 (stick up = forward, right =
(-fz, fx)); `nav_goto`, `nav_face`, `nav_settle` and `nav_press` mirror its
goto, face, settle and press. A pad attached to the machine replaces the
overlay (em_gamepad) and would disturb a run. The targets are route_capture's
(the terminal: (229, 250.4), then (223.5, 250.4) at half stick, face
-1.3037), except the battery's and the panel's. The battery's aims at route
01's stance before its press (218.212, 222.373; f118..f124, facing -0.9588)
at full stick to 1.0, then 0.4 stick to 0.1 (it may stop short), and faces
-0.9588. The panel's: the
original's walk carried the player past route_capture's (239.7, 222) to
(241.4, 225.3) before the press (beat 03 f228), the port's stops shorter,
so the runner aims at (240.5, 225) (00183EF0's panel radius is 9.5 around
(240, 232.8)).

### battery

Walk to route 01's stance, face -0.9588 and press Cross. 00184BA0 arms the
original owner 00219550 g0.0 from the published list (3B8D = 3); its take
program 0x266620 turns the player (op0E), plays the grab clip 0x42, settles
the camera target on the item (op00 sub8), consumes the item (001B6EA0 ->
001C47A0: 001C40B0(0x1B, 1), then B0 = 1, B1 = 0x1B) and the status screen
pops up on it. **In process:** the scan wins (the tick is printed as
`scan_d810750`); the screen opens (+B = 3) with item 0x1B taken and
B1 = 0x1B; the page is the ITEM root (screen 0) in its BATTERY child (item
state 5, after module 0x21) showing 002149F0's acquisition notice
(sub-state 3) with charge and capacity 12 and B0 consumed; the notice hands
over to the list (sub-state 1) after exactly 239 frames (route 01:
sub-state 3 from f219, the list at f459); TRIANGLE 20 frames later closes
the screen; control returns, the taken bit of uid 0x0B01 is set and the
count of 0x1B is 1.

**Against the capture** (`check_battery`): aligned on the scan tick and
route 01's first row with 3B8D != 0 (f125). From the scan to the post, row
for row: the spad bytes, the camera byte, the letterbox block, the message
block, the power byte and D_008106B0/B1 (0000). The post itself comes 61
rows after the scan in the port and 64 in the original: the op00 sub8 settle
is as long as the distance from where the camera target starts, and the
port's target starts from its own follow camera (WP-16: about one unit lower
than the original's D_008105E0) at the pad-navigated stance, so the check
requires instead that in both runs the post is two rows after the settle's
last target change (the settled record, the animation-end wait, then op09)
and that the settle ends on the item's X/Z (to 1e-3). Aligned on the post,
row for row again to the page's module load (f189..f192; the load wait is
WP-5's). The turn (op0E) is not compared either: its step count depends on
the stance. Negative controls: a changed spad or request byte in the
window fails.

### elevator_refusal, panel, elevator

**In process:** the Use scan wins (3B8D leaves 0 within 60 frames of the
press); the refusal shows message 0x8000001A, the letterbox and camera byte
1, keeps the power bit clear and releases the player; the panel opens the
BATTERY page on the 00157F60 request (B0 = 1, B1 = 0x82), the runner selects
Yes as route_capture does (30 frames, LEFT, 10 frames, Cross), the discharge
leaves item 0x1B with charge 8 and the power bit 0x80 is set before the
release; the ride leaves the player at y 190 (to 0.01) with the floor byte
D_0081083A = 1.

**Against the captures** (routes 02, 03, 04; `tools/test_level_smoke.py`):
aligned on the scan tick (the first tick with 3B8D != 0 after the press, the
tick whose D_00810750 the run prints) and the beat's first row with 3B8D != 0,
row for row through the release and 25 rows after it: the spad bytes
0x70003B8C..93, the cinematic camera byte D_008101E4, the letterbox block
D_0028A8D0, the message block D_002821B0 (kind, phase, token; sampled after
step F), the power byte D_0081084C, the player X/Z from the script's
placement on, the player Y while the script owns the player (the ride's 150
carried values included), the heading from the script's facing on, and the
camera eye/target D_008105D0/E0 from the script's first shot until the
release (at the panel with the retained-Y offset). The
panel is compared in two windows: from the scan through the status open (plus
B0/B1 from the request row), and from the Yes confirmation (B0 0 -> 1)
through the discharge, the exit, script 0x247BE0, the power bit and the
release (plus B0/B1); its player Y is checked as retained while the
script owns the player (001B6F00 keeps the ground Y of the approach, which is
navigation input), and equal to the capture after the release. The tick log gained
the fields these need (em_scene_bindings.c): `screen8`, `msg_pre`, `cam4`,
`power`, `floor`, `pos_post`, `yaw_post`, `eye_post`, `tgt_post`, and for
the boxes `player`. Negative
controls (one tampered letterbox byte, carried Y, camera eye, power byte,
message phase, message kind or message token in a copy of the log) each fail
the check.

*What the message block measures.* Since WP-8 the log samples the live
message service's block itself (D_002821B0 mode, B4 phase, B8 line; the one
storage, `em_message_live`): the line the running script posted (0x80000018
panel, 0x8000001A refusal) through 001B7D60 case 0, the phase through the
service's ticks and its 001FC9B0 teardown.

**Known divergences, reported, not compared:**
- *The status page's module load.* The original's ITEM root waits 24
  loader dispatches on its load of module 0x21, the BATTERY page (item
  state 3, route 03 f390..f414, and route 01 f193..f217), before the
  prompt; the port's status modules are resident, so its prompt consumes
  the request 7 ticks after the post against the original's 30. Everything
  from the Yes confirmation on is tick-exact. The wait is the disc read of
  001FF080 (D_00275BD8 clears when it completes): the translated loader
  needs 10 dispatches, the other 14 are I/O time whose split the captures
  do not record (STATUS_SCENE.md section 3; open, H7).
- *The camera after a release.* The original's follow keeps the script's
  eye X/Z and eases only its height; the port's follow camera (em_camera
  mode 0) moves the eye behind the player, and the camera block's +7 (the
  solver's hit byte) differs after the ride. Only the cinematic byte +4 is
  compared after the release; the follow camera is WP-16's.
- *The messages of the status page* (mode 4) run inside the port's page, not
  in the logged block; mode-4 rows are skipped.
- *The panel's player and camera Y.* While the script owns the player the
  panel windows check the player Y as retained (equal to the port's own
  approach Y, not the capture's) and the scripted camera Y with that same
  offset; the prompt window between the
  request and the Yes press is not compared (the module load above).
- *D_00282157 and the voice lanes D_00282155/156* read 0. D_00282157 is the
  phase of 001FA0D0's asynchronous disc read (em_scene_bindings.c
  r_00282157; the port's reads complete within their call), passed through
  that reader to 0x1AE040 state 3 and to the status page input (which does
  not read it). The voice lanes are not live (STREAM_LANES.md); no voice cue
  is pushed on the route before Roger and both messages are text-only.

*The ground after the release* is no longer a divergence: with FLOOR
engaged (00175900 over the original collision world) the port re-grounds on
its first ordinary callback as the original does, and the post-release rows
compare the player Y exactly (229.88731 at the panel, 189.99998 after the
ride).

### boxes

Route beat 05, live since census L25: the crates run their original owner
001551B0 (em_area11_boxes.c, CRATES_DRUMS_ORIGINAL.md "Binding"), and a
Use press runs the dispatcher 00160220 over the live player record
(em_player_closure_live; PLAYER_USE_DISPATCH.md section 4).

**Runner.** From the elevator's release the player walks to the route's
stance before crate r4 (228.787, 281.266, within 0.1), faces 0.0265 and
settles 30 frames, then presses Cross for 2 frames. The same happens on the
crate top before r3: (226.237, 288.309), facing -1.5708. After each climb
hands back to control, the pad stays neutral through the idle return (12
frames, route f254..f265). Last, the player walks north onto the upper
ledge. **In process:**
- each Cross enters the ledge climb (+5 = 2);
- the first climb ends on r4 at y 203.776 and the second on r3 at y
  217.786 (within 0.001);
- the player then stands on the ledge above y 219.

**Against the capture** (`check_boxes`, route 05). The tick log's `player`
field holds the record's +5, +1F0, +1F1, clip +20C, clock +3C and ground
owner +214 (as its original record address). Each climb is aligned on its
first row with +5 = 2 (f175, f393). The 91 rows through the landing and the
idle return are compared row for row:
- +5, +1F0, +1F1, the clip, the clock (from the row after the entry; the
  entry row's clock is the idle loop's at the press), the ground owner and
  the heading, exactly;
- Y exactly from the hang on (f190 / f408) and on the crate tops, and as the
  lift relative to the stance before it;
- X/Z as the displacement from the entry row, within 0.01. The stance is
  navigation input within 0.1 of the route's, and the climb's end placement
  follows the wall distance. The measured bound is 0.0062 and 0.0051.

Both climbs land on the original's ground records (0x7A7C70, then 0x7A7980).

### slide

Route beat 06, live since census L03: the floor service's apply 00175CF0
meets the hill's authored class-0x1000 grid nodes, 001796C0 enters state
0x1C and 0016C6A0 runs the slide on the live record
(em_player_closure_live.c binds em_player_slide_live_state as 0015B130's
state[0x1C]; PLAYER_CLIMB_SLIDE.md section 6).

**Runner.** From the boxes' end the player walks to route 06's start stance
(219.594, 305.214; 0.4 stick to within 0.1), settles, faces -0.04141 and
settles again. Then it plays route_capture.py's beat_hill_slide: the stick
at full deflection toward (240, 312) until within 1.5, then toward
(262, 356) without a stop until +1F0 = 0x30, and on until +1F0 leaves 0x30
(the landing). The stick is then released and held neutral through the
skid-out, the hand-back to state 0 and 60 frames of settle.
**In process:**
- walking down the hill enters the slide (+5 = 0x1C, +1F0 = 0x30);
- the slide action ends in state 0x1C on the low ground (y below 186; route
  f138 y 185.0);
- control returns there (route f181: y 185.28).

**Against the capture** (`check_slide`, route 06). The run's one slide entry
(its first tick with +5 = 0x1C) is aligned on route 06's (f72). The
122 rows through the landing (f138), the skid-out, the hand-back (f181)
and 12 idle rows are compared:
- +5, +1F0, +1F1, the clip, the clock (from the row after the entry; the
  entry row's clock is the walk clip's) and the ground owner, exactly;
- the heading from the row after 0016C6A0's turn onto +218: it takes the
  original's authored downhill values in the original's order (0.4113,
  0.404, 0.33419, 0.29289), each change within one row of the original's;
  exact from the landing on;
- the per-row motion (each row's step from the previous one) within 0.0025
  in X, Y and Z, except on the node-crossing rows (and their neighbours)
  and the landing row; the landed Y within 0.02 of the original's.

Why not the absolute positions: the port's walk down to the hill is
navigation (its own locomotion, WP-15/L12, steered against the legacy
follow camera, WP-16), so the slide starts about 0.58 from the original's
entry in X/Z. Where the slide crosses from one authored node to the next
(new +218 and slope) then follows that entry point: a crossing one row
earlier or later changes that row's speed gain (0.01 sin(slope)), and the
constant step residual after it (measured at most 0.00184) is that one
row's gain. The landing row's Y is the floor under the port's own X/Z
(measured 0.0125 above the original's); every step after the landing is
equal within 0.00004. Measured: every other compared field equal. A
mutation (the entry speed 0.2 to 0.21 in em_player_slide.c) fails at f137
(the landing one row early).

What the smoke does not compare: the slide's sounds and effects (the loop
0x12E and the skid/landing ids are not in the exported sfx registry, WP-14,
and the 001EFD90 spawns go to the counted effect gap, L26); the camera
(the legacy follow camera, WP-16).

## Adding a phase (the contract for WP-4 onward)

In the commit that makes a phase's original owners live:

1. **Runner.** In `em_level_smoke_test.c`, give the phase's `k_phases` row a
   `begin` and a `frame` function. The runner may drive **pad input only**
   (`pad_key`, and the stick when navigation is added), never positions or
   state.
   - Navigation should copy route_capture.py's closed loop (FIRST_LEVEL_ROUTE.md
     section 1): the stick points at a world target relative to the camera
     forward 0x810600.
   - Use is Cross, the status screen closes with Triangle, and LEFT moves the
     BATTERY prompt's cursor to Yes.
   - In-process assertions must cite original evidence (addresses,
     ORIGINAL_FRAME_ORDER, or the route beat).
2. **Capture check.** In `tools/test_level_smoke.py`, add the phase's check
   to `PHASES`. The script refuses a phase that passes in process without a
   capture check.
   - Compare the phase's tick window with the beat's `trace.json` rows (field
     list in FIRST_LEVEL_ROUTE.md section 1).
   - Align on state events (a script start, a B0/B8 request, a fade substate),
     not on frame numbers. Re-captures shift beat frame numbers by a few
     frames, and the port's navigation path is its own.
   - Scripted segments keep their lengths (FIRST_LEVEL_ROUTE.md section 7) and
     can be compared row for row.
3. **Tick log.** If the phase needs a field the log lacks (for example power
   0x81084C, the letterbox block 0x28A8D0 or the message block 0x2821B0),
   extend the log in em_scene_bindings.c together with the owner's
   canonical storage. Never log a port-private mirror as if it were the
   original byte.
4. **Speed.** The default `make test-level-smoke` should stay near 10 s. When
   the live route grows past that, keep the default target at a quick
   `EM_LEVEL_SMOKE_UNTIL` and add a separate full-route target.
5. **Docs.** Update this table, the design doc's step line, and the
   FIRST_LEVEL_AUDIT WP row.
