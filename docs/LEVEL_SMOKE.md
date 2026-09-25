# Level smoke: the first level, live, phase by phase

Step S13 of SCENE_COORDINATOR_DESIGN.md (2026-09-23), extended by WP-4 (the
elevator refusal, the panel and the elevator ride), census L25 (the
boxes: the Use chain's ledge climbs onto the crates' original owners),
census L03 (the hill slide), census L23 / L19 (the truck preview and
crossing on the truck's original owners and the AREA11 script host) and
census L09 / L10 / L11 (the cage ladders, the tank and pipe-end climbs, the
crevice jump and the east tower climb, with the director's beats driven
through its legacy stand-in) and census L22 (Roger's encounter on his
original owner and scripts). The
smoke plays the
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
`NOT-LIVE driven`, and the capture checker skips it. Since census L09..L11
three phases are driven: `cage_roof`, `crevice_prompt` and `east_tower`, the
director 008253F0's beats 0, 1 and 2. The director is not bound yet
(census L21; Roger, which it waited on, is live since census L22:
DIRECTOR_ORIGINAL.md section 6), so node #21 still runs the legacy
em_director.c, and the later climbs and the jump start from where its
beats leave the player (none of the three original scripts moves the
player).

## Running it

```sh
make test-level-smoke                  # through truck_crossing (about 11 s)
make test-level-smoke-full             # the whole live route, through east_tower (about 19 s)
EM_TEST_FULL=1 make test-level-smoke   # the same as test-level-smoke-full
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

`EM_LEVEL_SMOKE_UNTIL` takes a phase name from the table below. The binary's
default is the last phase; the make target's default is `truck_crossing`
(rule 4 of "Adding a phase"), and `EM_TEST_FULL=1` or
`test-level-smoke-full` lifts it. An unknown name fails and lists the phases. The run stops
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

### Frame captures

`EM_LEVEL_SMOKE_PHASE_CAPTURE=<phase>:<file.bmp>` saves the frame that ends
`<phase>` (a verification aid in `next_phase`; the headless renderer draws
it as usual). Compare it by eye with the route beat's `original.png`; it is
not a pixel test. For example, the elevator phase's end frame against
`04_elevator_ride/original.png` shows the powered terminal's green arrow
(docs/CENSUS_UNVERIFIED.md, the indicator children). Keep such files under
an ignored `build/<task>/` folder.

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
| truck_preview | 07 | trigger 0x8251E0, camera script 0x8292C0 | yes (census L23, L19) | — |
| truck_crossing | 08 | truck 0x823FF0 | yes (census L23) | — |
| cage_ladders | 10 (f0..f1090) | Use 00160220 -> 0015D4C0 case 0x32, ladder entry 00165B60 (state 0xB), climb 001662D0 (state 0xC); the walk's step-off fall 00162DB0 / landing 00163B40 | yes (census L09, L10) | — |
| cage_roof | 10 (f1091..) | director 0x8253F0 beat 0 script 0x8294C0, Roger 0x8237E0 script 0x828990 | no: driven through em_director.c | WP-10 (L21; Roger is live since L22) |
| crevice_climbs | 11 (f0..f706) | ledge climbs 0015DF10 onto the tank and the pipe end; the pipes walk's step-off | yes (census L04, L02) | — |
| crevice_prompt | 11 (f707..) | director beat 1, script 0x829A40 (line 0x97) | no: driven through em_director.c | WP-10 (L21), WP-8 |
| crevice_jump | 12 | running jump 0015EC50 / 001634A0 (+1F0 0x0C), landing 8 / 0xF; the approach's step-off | yes (census L11) | — |
| east_tower_climb | 13 (f0..f531) | high ledge climb 0015DF10 onto the east tower top | yes (census L04) | — |
| east_tower | 13 (f532..) | director beat 2, script 0x829CC0 (line 0x99) | no: driven through em_director.c | WP-10 (L21), WP-8 |
| roger | 14 | Roger 0x8237E0 quad 0x82AB80, script 0x8283D0 (bank 0x96: 0022EEF0 camera, the player's clip 1 through 00183090), equipment 001C5C90 | yes (census L22) | — |

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
- **The live camera** (census L13..L16, docs/CAMERA_LIVE.md;
  `check_first_control_camera`), against the original's per-frame samples
  of the camera block D_008101E0 (0xD0 bytes; the tick log's `camblk`
  carries the port's block, the forward D_00810600 and D_00810690..A3):
  - `newgame_samples.jsonl`: the port's first two ticks with a seated block
    equal frame 2639 (the 001B0460 seat) and frame 2640 (0018B9C0's state
    0) byte for byte, except at 2640 the state-0 ceiling (+0x60..+0x63 and
    +0x5A bit 0x80: the port evaluates no player pose before the opening
    releases the player, CAMERA_LIVE.md section 5);
  - `postcinema_samples.jsonl` (save state 03 on): the mode-8 settle
    (001914A0) clears the action +6 on the port's first-control tick as on
    frame 4027; the 24 frames before it equal byte for byte (the opening
    timeline words +0x6C..+0x7B and the ceiling excepted), and the 40
    sampled frames before those differ only in the eye / target heights
    and the forward +0xB0 that the settle is still chasing, with the eye
    height difference never growing. A frame's last sample may predate its
    camera stage, so it must equal the port's block at the end of that
    frame or of the one before.

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
block, the power byte and D_008106B0/B1 (0000). The post itself comes on
the original's row (64 rows after the scan): the op00 sub8 settle is as long
as the distance from where the camera target starts, and since the live
camera (census L13..L16) the port's target starts from the original's
follow camera at the pad-navigated stance (0.037 off the capture's,
converging during the settle). Until then the port posted 3 rows early
from the legacy follow camera's target (about one unit lower); the check
now requires the original's row, and in both runs the post two rows after
the settle's last target change (the settled record, the animation-end
wait, then op09) with the settle ending on the item's X/Z (to 1e-3). Aligned on the post,
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
- *The camera after a release* is compared since the live camera (census
  L13..L16, `check_follow_after_release`): from the release to the end of
  the capture, the eye / target D_008105D0 / E0, the block's desired
  eye / target +0x10 / +0x20 (to the capture's five decimals) and the bytes
  +4..+7. The refusal and the elevator equal the capture row for row. The
  panel's camera leaves the script with the retained approach Y (0.003
  off); the difference never grows and the camera equals the capture from
  f679, 24 rows after the release.
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
and 12 idle rows are compared. The entry must lie within 0.9
(`SLIDE_ENTRY_XZ`) of the original's in X/Z; its offset sets the allowed
rows (`SLIDE_ENTRY_ROWS`: one row up to 0.6, two up to 0.9). The landing
(+1F0 0x30 -> 0) must come within the allowed rows of the original's; the
rows before it are aligned on the entry, the rows from it on the landing:
- +5, +1F0, +1F1, the clip, the clock (from the row after the entry; the
  entry row's clock is the walk clip's) and the ground owner, exactly, row
  for row before the earlier of the two landings and after the landing;
  between the two landings (the slide still running in one run) every
  field but +1F0 equal; on the landing row every field but the slide
  clip's clock, which ran on for the same number of rows;
- the heading from the row after 0016C6A0's turn onto +218: it takes the
  original's authored downhill values in the original's order (0.4113,
  0.404, 0.33419, 0.29289), each change within the allowed rows of the
  original's; exact from the landing on (aligned on it);
- the per-row motion (each row's step from the previous one) within 0.0025
  in X, Y and Z, except on the node-crossing rows (and their neighbours)
  and the landing rows; after the landing (aligned on it) within 0.0025
  too; the landed Y within 0.02 of the original's.

Why not the absolute positions: the port's walk down to the hill is
navigation (the smoke's own stick input, steered against the camera's
forward; the idle / walk states are the original's since census L12), so the slide starts away from the original's entry in X/Z: 0.58
under the legacy follow camera, 0.86 since the live camera (census
L13..L16). Where the slide crosses from one authored node to the next
(new +218 and slope) then follows that entry point: a crossing earlier or
later changes that row's speed gain (0.01 sin(slope)), and the constant
step residual after it (measured at most 0.0018) is that gain. From the
0.86 entry the second and third crossings move by two rows and the landing
by one (f139 in the port); until the live camera the crossings moved by at
most one row and the landing not at all, and the check required exactly
that (one row, no landing alignment). The row tolerance now scales with the
measured entry offset (`SLIDE_ENTRY_ROWS`): one row for an entry within 0.6
of the original's in X/Z (the HEAD run's 0.575 still passes with one row),
two rows up to `SLIDE_ENTRY_XZ` = 0.9, and an entry further off fails. The
relaxation follows the navigation input, not a slide or camera routine, and
is **pending lead review** (CAMERA_LIVE.md section 4). Tightening the
approach instead was tried: a run-up and a release lead that bring the
player within 0.28 of route 06's stance still enter the slide 0.86 off,
because the smoke's stick input and the live camera's state at the stance
(it follows the port's own walk history; its eye is 3 units from the
capture's there) set the path. With the idle / walk states the original's
(census L12; their first-control record equals the original's) the entry
is unchanged at 0.863, so the walk translation is not the cause; one row
needs the route's own input replayed from the capture's state. The
landing row's Y is the floor under the port's own X/Z (measured 0.0133 above
the original's); every step after the landing is equal within 0.00002.
Measured: every other compared field equal. A mutation (the entry speed 0.2
to 0.21 in em_player_slide.c) still fails the check (at f82: the
per-row step, 0.0040 off in X; measured 2026-09-25).

What the smoke does not compare: the slide's sounds and effects (the loop
0x12E and the skid/landing ids are not in the exported sfx registry, WP-14,
and the 001EFD90 spawns go to the counted effect gap, L26); the camera
rows (the live camera follows the port's own entry point; route 06 has no
release to align it on).

### truck_preview

Route beat 07, live since census L23 / L19: the trigger 008251E0 runs
em_truck_trigger_tick on its record and its camera script 0x8292C0 runs on
the AREA11 script host (em_area11_script_host; TRUCK_ORIGINAL.md and
AREA_SCRIPT.md section 6.1 "Binding").

**Runner.** route_capture.py's beat_truck_preview: the stick walks the
waypoints (280, 392), (300, 400), (328, 412) at full deflection (each until
within 2.0, a blocked waypoint skipped after 45 frames) and is released on
the first frame with 3B8D != 0 (the trigger has started the script); then
neutral until D_00810792 = 1 and control, and 30 frames of settle.
**In process:** the script's frame (3B8D = 2), the bars and camera byte 1
were seen; the player ends at the script's placement (327.4, 396.7) with
the heading 1.97222; D_00810792 = 1; the trigger node freed itself.

**Against the capture** (`check_truck_preview`, route 07). Aligned on the
first tick with 3B8D != 0 after the slide and route 07's first such row
(f164), every row through the release (f527) and 25 rows after it, as for
the terminal scripts (`compare_window`): the spad bytes, the camera byte,
the letterbox, the message block, the power byte, the placement (01/1, from
f167), the heading (04/8, from f168), the camera eye/target of every shot
(from f169) until the release, and the re-grounded Y after it; plus
D_00810792 (1 from the release row) and, from the row after the first, the
player record's +5, +1F0, +1F1, clip and ground (the admission's +5 = 0 /
+1F0 = 0x41 at f165, 00182DF0's tail at f527). Measured: every compared
field equal. A mutation (the placement Y + 0.001 in the script host's
00182F90 worker) fails at f167.

The follow camera from the release to the end of the capture (census
L13..L16, `check_follow_after_release` 'converge-open'): the solver flags
+7 of the last walking frame before the script were 0 in the port and 0x40
in the original (the approach is navigation), so the eye / target leave
the release 0.14 off; the difference must never grow and is 0.00001 at
f557, the capture's last row.

### truck_crossing

Route beat 08, live since census L23: the truck 00823FF0 runs
em_truck_original_tick on its record (em_area11_boxes), publishing its hull
cell (uid 14) into the collision world, so the player's floor service
stands on the truck record.

**Runner.** route_capture.py's beat_truck_crossing: the stick walks (345,
390), then (365, 368) (walk_path as above), is released, and the run waits
for D_00810792 = 0xFF and settles. **In process:** the player stood on the
truck record (its +0x214), the truck armed (+0x2EC), fell (state 1) and
rests in state 2 with D_00810792 = 0xFF; the set piece spawned its 32
effects (TRUCK_ORIGINAL.md; counted at the effect gap, census L26); the
pad block's actuator ran (001B61C0) and step I's countdown stopped it
again (+0x16 and +0x28 are 0, as in route 08's end snapshot); the player
is on the low ground north of the pit.

**Against the capture** (`check_truck_crossing`, route 08). Aligned on the
arm row (the first with +0x2EC = 1: f43, the player's ground the truck
record 0x7A9FB0 in both), every row through the fall's end (f209) and 10
rows at rest: the truck record's +0x00..+0x0F, +0xB0 (to the capture's 5
decimals) and +0x2DC..+0x2EF, and D_00810792. Measured: equal. The tick
log gained `story792` and `truck` (the record address, +0x00..+0x0F, +0xB0
and +0x2DC..+0x2EF). A tampered truck Y in a copy of the log fails at f89.

What the smoke does not compare in beat 08: the player's own walk across
and off the truck (navigation: the original idle / walk states since census
L12, steered by the smoke's stick against the live camera's forward; the original's walk got blocked on the truck
for 40 frames), the rumbles' timing (not in the capture rows), the truck's
sounds 0x454 / 0x455 (not in the exported sfx registry, WP-14) and its
effects (L26).

### cage_ladders

Route beat 10 up to the roof, live since census L09 / L10: the Use chain's
0015D4C0 finds the column's authored attribute-0x32 grid node and runs case
0x32 (00177030 mode 4 over the node's axis +0x34..+0x3F, which the EMCL
axis section carries since this step; STARTUP.md step 13), 00165B60 runs
the entry (state 0xB) and 001662D0 the climb and the dismount (state 0xC),
all over the live record (em_player_closure_live.c;
PLAYER_LADDER_ENTRY.md and PLAYER_LADDER_CLIMB.md "Binding").

**Runner.** route_capture.py's beat_cage_roof_roger up to the roof: the
stick walks (360, 320), (360, 296) (walk_path, tolerance 1.0), settles 5,
goes to (360, 293.5) at 0.4 stick, settles 5, faces pi (and settles 10, as
face() does) and presses Cross for 2 frames. Once the record shows +1F0 =
0x17 the stick is held up until +1F0 leaves 0x15 / 0x17 / 0x18, as the
route's ladder() does, two frames later than the row that showed 0x17: the
capture's pad reached the game two frames after it was set (route 10 keeps
clip 0xE6 through f330), the overlay's reaches it on the next frame. Then
(359.8, 262) at 0.5 stick, the same face, press and climb to the roof.
**In process:** both presses enter state 0xB with +1F0 0x15, both climbs
run 0x17 and 0x18, ladder A ends on the cage floor (y 225.374) and ladder
B on the roof (y 264.912), each within 0.001.

**Against the capture** (`check_cage_ladders`, route 10):
- the walk's one step-off (route f88; `check_fall`, below);
- ladder A from its entry (f268) through the hand-back into the walk state
  (f581), ladder B from f780 through f1090 (at f1091 director beat 0 takes
  the player): +5, +1F0, +1F1, clip, clock (from the row after the entry),
  ground and heading exactly, X/Z exactly (the entry places +B0/+B8 on the
  node's centre, so the stance does not carry), Y as the lift from the
  entry row while on the ladder (the entry keeps the stance's floor Y, 1e-5
  apart here) and exactly after the dismount.

Measured: every compared field equal; the lift within 7.2e-6. A mutation
(the climb's 3.0 step in em_player_ladder_climb.c to 3.0156) passes in
process and fails the capture check at f356.

### cage_roof, crevice_prompt, east_tower (driven)

The director's beats (FIRST_LEVEL_ROUTE.md section 5). The runner holds
the pad neutral until the legacy em_director.c has run its beat, stored its
step byte D_00810813 (0x10, 0x20, 0xFF) and control is back, then settles
30 frames. Reported `NOT-LIVE driven`; nothing is compared. The original's
beat 0 also runs Roger's conversation 0x828990 and leaves D_00810813 = 0x11
(Roger's ordinary branch); the stand-in leaves 0x10, which its own beat-1
test accepts. Beats 1 and 2 show the lines 0x97 and 0x99 in the original;
the stand-in shows none (DIRECTOR_ORIGINAL.md section 6).

### crevice_climbs

Route beat 11 up to director beat 1: the tank climb and the pipe-end climb
(the ledge climb 0015DF10 / state 2, live since the Boxes step) and, between
them, the pipes walk's step-off onto the 270 plateau (the fall 00162DB0
and landing 00163B40, bound since the Boxes step).

**Runner.** route_capture.py's beat_crevice_prompt: the stick walks
(385, 238), (407, 240) (tolerance 1.0) and settles; then the route's stance
before the tank press, (405.283, 240.018), at 0.4 stick (within 0.1),
settle 20, face 1.5637 (the route's heading at the press), settle 30,
Cross; the climb hands back to control. Then the pipe waypoints (420, 262)
.. (470, 292) (tolerance 1.5), with one extra waypoint (470, 300) before
the last so the port's final leg runs straight down -z as the original's
did; the walk's stop is the pipe-end stance (as in route_capture.py; a
second approach there slides along the pipe's end face), face -3.0327,
settle 30, Cross; that climb ends when +5 leaves 2 (route f706: director
beat 1 claims the player on the landing row). **In process:** both presses
enter the ledge climb and end on y 286.09 and 279.9 (within 0.001).

**Against the capture** (`check_crevice_climbs`, route 11):
- the tank climb f172..f276 and the pipe-end climb f642..f706: +5, +1F0,
  +1F1, clip, clock, ground, heading and Y row for row; X/Z as the
  displacement from the entry within the distance between the two stances
  plus 0.001 (0015DF10 carries the stance along the wall and places it at
  the wall's distance, so the stance offset bounds the residual);
- the step-off between them (route f498; `check_fall`).

Measured: the stances are 0.138 and 0.407 from the original's, the X/Z
residuals 0.1385 and 0.1097; every other field equal.

### crevice_jump

Route beat 12, live since census L11: 00160220's running-jump probe
0015EC50 enters state 6 and 001634A0 (with the recovery lane's 001751A0,
00178EC0, 0017C860, 002243F0) carries the player across the crevice; the
landing is the fall family's state 8 (em_player_closure_live.c;
PLAYER_RUNNING_JUMP.md "Binding").

**Runner.** route_capture.py's beat_crevice_jump: the stick walks
(485, 275), (477, 262) (tolerance 1.0; the drop off the pipe on the way),
settles 5, faces pi, settles 10, then points at (477, 150) at full
deflection until z <= 249.5; Cross is held 2 frames with the stick kept,
and the stick stays on until +1F0 leaves 0x0C / 0x0F; then neutral and a
settle. **In process:** the jump was entered, landed (+1F0 0x0F) and the
player settled on the north block (z below 210, y 269.84).

**Against the capture** (`check_crevice_jump`, route 12):
- f230..f304: +5, +1F0, +1F1, clip, clock, ground and Y row for row (the
  jump 6 / 0x0C with clips 0x69 / 0x6B, the landing 8 / 0xF at f277 with
  clip 0x6E, the recovery 8 / 1 and the hand-back into state 1);
- the heading is 0015EC50's take-off heading, the stick's direction at the
  press (navigation): constant through the window in both runs;
- the per-row horizontal step length equals the original's within 1e-4 on
  every row where both runs step along their take-off headings (free
  flight: 37 rows); the rows where the arc slides along the north block's
  edge depend on the lateral offset the heading makes;
- the approach's step-off off the pipe (route f42; `check_fall`).

Measured: take-off heading -2.99699 against the original's -2.97704; every
compared field equal, the step lengths within 2e-5. A mutation (tier 3's
launch speed in em_player_running_jump.c, 1.8 to 1.8005) fails at f240.

### east_tower_climb

Route beat 13 up to director beat 2: the high ledge climb (0015DF10 /
state 2) onto the east tower top.

**Runner.** route_capture.py's goto(445, 178) at 0.6 stick, settle 5, then
the route's stance (444.246, 179.776) at 0.4 stick, settle 20, face
-1.6104, settle 30, Cross; the climb ends when +5 leaves 2 (route f531:
director beat 2 claims the player on the landing row). **In process:** the
climb ends on y 289.75.

**Against the capture** (`check_east_tower_climb`, route 13): f438..f531
as for the crevice climbs. Measured: stance 0.004 from the original's, X/Z
residual 0.0031, every other field equal.

### roger

Route beat 14: the running jump across the east tower's gap into Roger's
quad and his encounter (census L22; ROGER_ACTOR_ORIGINAL.md section 4,
AREA_SCRIPT.md 6.1, ROGER_CINEMATIC.md).

**Runner.** route_capture.py's beat_roger_encounter: goto(436, 190) at 0.6
stick (tolerance 0.8), settle 5, face -pi/2, settle 10, then run toward
(300, 190) at full stick until x <= 411.5 (f238), Cross for two frames with
the stick kept; the stick stays on until +1F0 = 0x0C (the running jump,
f241) and until Roger's script block names 0x8283D0 (his 008237E0 ordinary
branch: the player crossed into the quad 0x82AB80 in mid-air, f283), then
neutral until the scripted frame opens (3B8D != 0) and until control is
back (3B8D = 0, +1F0 = 0; f1758), then settle 60. **In process:** the
script ran, D_008107D8 holds bit 0 and the player stands at the script's
01/9 placement (338, 289.75, 192), heading -2.531.

**Against the capture** (`check_roger`, route 14):
- the script start: the port's first tick with Roger's block at pc
  0x8283D0 against f283 in Roger's +0x00..+0x0F, +0xB0 and block, and every
  tick until the port's frame opens with the block held at the op16
  (00182BF0 waits for the landing: 4 ticks in the port, 5 rows in the
  original; the jump itself is navigation);
- from the first tick with 3B8D != 0 (f288) every row to the end of the
  capture (f1818, 1531 rows): the spad bytes, the camera byte, the
  letterbox, the message block, the fade block (the next tick's start
  sample), Roger's +0x00..+0x0F, +0xB0 and script block, the equipment
  node's +0x00..+0x0F and, from Roger's clip init at f358, its +0xB0
  (before it his idle clip's phase is the time since the area load, which
  the capture's save state and the port's walk do not share), D_008107D8
  and D_00810813, the player record's +5, +1F0, +1F1, clip, clock and
  +0x2F3, the camera eye / target while the camera byte is 3 (the bank
  0x96 timeline, 0022EEF0, from f358) or near the release, and the
  player's position and heading from the 01/9 placement (f1756) on;
  the script block's halfword +0x0E (Roger's +0x1FE, the animation flags
  his clip advance returns) only from his clip init at f358, like the
  equipment's +0xB0: before it they are his idle clip's, whose loop wrap
  (0x3000 for one frame) falls on the time since the area load. Both are
  **navigation-induced exemptions**, to lift once the smoke's walk timing
  matches the capture's; +0x0E was added with the live camera (census
  L13..L16: the walk steered against the live forward takes a different
  time) and is pending lead review;
- from the release to the end of the capture, the live follow camera row
  for row (`check_follow_after_release`, census L13..L16).

Measured: every compared field equal on every row.

### The step-offs (`check_fall`)

The walks of beats 10, 11 and 12 each step off one edge. From the port's
fall entry (+5 = 5, +1F0 0xB) and the route's: +5, +1F0, +1F1, clip and
clock (once the fall's clip 0x73 has replaced the walk's) and the Y as the
drop from the entry, row for row through the landing; then, aligned on the
first row with +5 = 8 in each run, the landing 8 / 0xF (clip 0x6E), the
recovery 8 / 1 and the hand-back into state 1 plus 2 rows. The landing must
fall on the original's row when both falls start at the same height (to
0.001); the edge point is the port's own walk (navigation), and a different
start height may move it by one row (beat 10: 191.44 against 191.624, one
row earlier). Measured: beats 11 and 12 land on the original's rows; beat 10
one row earlier; every compared field equal.

What these phases do not compare: the walks between the climbs (the
original idle / walk states since census L12, steered by the smoke's stick
against the live camera's forward), the sounds (the ladder's 0x107 / 0x10E / 0x10F and
the landing ids are not in the exported sfx registry, WP-14; they reach
em_sfx_play silently and are reported once), the effects (0017DEB0's and
00187EE0's 001EFD90 spawns reach the counted effect gap, L26) and the
director's beats (driven, above).

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
