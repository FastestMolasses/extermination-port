# Level smoke: the first level, live, phase by phase

Step S13 of SCENE_COORDINATOR_DESIGN.md (2026-09-23). The smoke plays the
port's first level headless from New Game along the original route and checks
each phase twice:

- in process, against original facts it can observe;
- against the original captures, by replaying the run's tick log.

A phase whose original owners are not live in the port yet reports
**NOT-LIVE**. The report names the step it waits on and the binding the port
runs for that owner today. A NOT-LIVE line is not a pass: nothing past it has
been verified.

## Running it

```sh
make test-level-smoke                  # the whole route (about 7 s today)
EM_LEVEL_SMOKE_UNTIL=first_control make test-level-smoke
EM_LEVEL_SMOKE_UNTIL=status make test-level-smoke
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
  which is the 001D1EA0 position of every world and status frame.
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
| status | 01 status exit; frame_trace2 `status_04.json` | 001AE7E0 r==2 → state 3 → 5 → 1 | yes (S11b) | — |
| battery | 01 | pickup 00219550 g0.0, take script 0x266620, ITEM page | no | WP-6, WP-5 |
| elevator_refusal | 02 | terminal 0x827B10, script 0x82A990, message 0x8000001A | no | WP-4, WP-8 |
| panel | 03 | panel 00159210, scripts 0x2477A0/0x247BE0, 00157F60 BATTERY page, power 0x80 | no | WP-4, WP-5 |
| elevator | 04 | terminal 0x827B10, script 0x82A750, carry 0x828050 | no | WP-4 |
| boxes | 05 | ledge climb onto crates r4/r3 (001551B0) | no | climb wiring (WP-15), WP-18 |
| slide | 06 | slope slide 0016C6A0 | no | slide wiring (WP-15) |
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

**Known divergence (WP-5, H6):** the interim em_hud hub closes on the first
state-3 frame after the TRIANGLE edge arrives, so the port takes one tick
from the press to +B = 5, the same as from START to r == 2. In
`status_04.json` (opened and closed with TRIANGLE) the original took two
frames longer for the close than for the open: the press at f200 gave
+B = 5 at f204, the press at f10 gave r == 2 at f12. The inputs differ:
the capture script (`frame_trace2/exp_status.py`) held TRIANGLE for four
frames each time (f10..f13 and f200..f203, so f204 is the first frame after
the release), while the smoke taps START and TRIANGLE for one frame. The
decomp points to an edge-triggered close whose extra latency comes from the
exit, not from the hold: 0020CDC0 sets its phase 5 on the 0x830 (sub-state
0) or 0x810 (phase 2) edge, and phase 5 returns 0020E0C0 each frame until
that exit sequence reports done (NEARMISS C, `src/func_0020CDC0.c`). WP-5
must assert the press-to-close delay from 0020E0C0 itself, and should use a
one-frame tap capture if the hold length is in doubt. The smoke therefore
aligns the close on the +B = 5 tick, not on the press, until 0020CDC0 is
translated (WP-5).

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
