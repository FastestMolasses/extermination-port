# Level smoke: the first level, live, phase by phase

The route ends in AREA01 since step GUARD (2026-10-04): its last phase,
`a01_arrival`, is the arrival's rebuild and 60 neutral world ticks,
compared with route 15 f741–801 by `tools/level_smoke_area01.py`
("a01_arrival" below). The 0x1AE040 guard that stopped AREA01's world
frames is gone; an AREA01 original without an owner still faults where it
is reached. `make test-level-smoke-full` and `--require-through last` end
at `a01_arrival`. The harness also names the AREA01 main beats
`a01_00`..`a01_07` and the side beats `a01_s0`..`a01_s7` (opt-in). The
first of them, `a01_00` (the train room), runs live and is compared row
for row ("a01_00" below); since step DRAWN (2026-10-04) all 781 rows
pass, and since the level-2 check (2026-10-08) a01_01, a01_02, a01_s0,
a01_s3, a01_s4 and a01_s6 pass too ("a01_01..a01_07 and the side beats"
below). It is opt-in (about 5 minutes with its checks), not part of `make
test-level-smoke-full`. Runs that reach AREA01 also check its drawn world
after the phases ("The AREA01 world" below).
`make test-area01-smoke-harness` checks the harness itself against the
recorded inputs, not a native run. See
[LEVEL2_BINDING.md](LEVEL2_BINDING.md) for the AREA01 state and the work
left.

Step S13 of SCENE_COORDINATOR_DESIGN.md (2026-09-23), extended by WP-4 (the
elevator refusal, the panel and the elevator ride), census L25 (the
boxes: the Use chain's ledge climbs onto the crates' original owners),
census L03 (the hill slide), census L23 / L19 (the truck preview and
crossing on the truck's original owners and the AREA11 script host) and
census L09 / L10 / L11 (the cage ladders, the tank and pipe-end climbs, the
crevice jump and the east tower climb) and census L22 (Roger's encounter on
his original owner and scripts), by the full-route step of 2026-09-25 (side
beat 00 live in its own run), by WP-8b (the stream lanes live; the director
008253F0's three beats and Roger's voiced conversation on their original
scripts) and by census L18 (side beat 09: the fence door on its original
owner, 2026-09-25) and by census L29 (the player's drop shadow, checked
after the phases: "The drop shadow" below, 2026-09-26). The
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
`NOT-LIVE driven`, and the capture checker skips it. No phase is driven since
WP-8b: the director's beats `cage_roof`, `crevice_prompt` and `east_tower`
run on the original owner (census L21, DIRECTOR_ORIGINAL.md section 6) with
their voiced lines on the stream lanes (STREAM_LANES.md "Live binding").

**Route coverage.** The checker ends with one `level smoke: route beats:`
line naming every route beat 00..15 and the state of each of its phases
(live, NOT-LIVE driven, NOT-LIVE, or not reached; a side beat the run did
not play reads "not played in this run"). As of 2026-10-02: beats
01..15 live on the main line (19 phases; beat 15, the level exit, since
chain C11 EXIT), the side beats 00 and 09 each in
its own run, and `make test-level-smoke-full` requires all of them
(`--require-through`, "Running it"). Since 2026-09-27 the side-9 run goes on
to the fence door's side 1 (`fence_door_side1`, the decomp's C7 DOOR1
capture, not a route beat), which the same targets require.

## Running it

```sh
make test-level-smoke                  # first_control, status, battery (about 15 s; the shortest supported run)
make test-level-smoke-full             # the whole route through the exit and the AREA01 arrival idle (a01_arrival), --require-through last (about 150 s), then the side runs below (about 70 s)
EM_TEST_FULL=1 make test-level-smoke   # the same as test-level-smoke-full
make test-level-smoke-side             # side beat 00 (about 14 s), the designed status_pages run (about 47 s), then side beat 09 with side 1 (about 56 s), each its own run with its own rand() trace, then test-level-smoke-aim, test-level-smoke-damage and test-level-smoke-branch
make test-level-smoke-damage           # the DAMAGE side runs side by side: dmg_flame, dmg_crevice_fall, dmg_pit_fall (EM_DAMAGE_SIDES=a,b runs only those)
make test-level-smoke-branch           # the BRANCH side runs side by side: br_ledge_ammo, br_map_item, br_elevator_up, br_panel_decline, br_crate_stack, br_west_ledge, br_yard_ammo, br_cage_key, br_plateau, br_roger_talk (EM_BRANCH_SIDES=a,b runs only those; 7 min 5 s for the ten with their checks, measured 2026-10-03)
make test-level-smoke-aim              # the aim/fire side runs side by side: aim_r1_hold, aim_r2_hold, aim_fire, aim_melee, aim_light, aim_world, aim_cable, aim_burst (8 min 57 s for the first seven with their checks, measured 2026-10-02 under a load average near 50); EM_TEST_FULL=1 adds aim_both, aim_reload, aim_reload_empty (the eleven about 25 min under a load average near 140)
EM_LEVEL_SMOKE_UNTIL=status_pages make test-level-smoke       # the designed status_pages run alone (with its page-trace replay)
EM_LEVEL_SMOKE_UNTIL=fence_door_side1 make test-level-smoke   # through truck_crossing, then side beat 09 and the fence door's side 1 (about 56 s)
EM_LEVEL_SMOKE_UNTIL=elevator_refusal make test-level-smoke   # any later main-line phase, e.g. panel, boxes, roger
EM_LEVEL_SMOKE_UNTIL=a01_arrival make test-level-smoke        # the main line through the AREA01 arrival idle alone (about 2 min)
EM_LEVEL_SMOKE_UNTIL=panel_no_battery make test-level-smoke   # side beat 00 alone
EM_LEVEL_SMOKE_UNTIL=fence_door make test-level-smoke         # side beat 09 without side 1
make test-level-smoke-ps2-drive        # the main route through roger with the PS2 disc-drive timing switch on (about 110 s)
EM_PS2_DISC_DRIVE_TIMING=1 make test-level-smoke-side          # any target with the switch on (the side runs, about 70 s)
```

Every target runs with the switches in its environment (`src/em_settings.h`);
without any, that is the Original profile, whose disc answers at host speed.
See "The stream drive's two modes".

**Supported end phases.** A run may end at `battery` or at any later phase
of the table below, main line or side. Each of them passes the make target
(checked one by one on 2026-09-27). A run may not end at `first_control` or
`status`. Those two runs play and pass in process, but the checker then
fails `check_render_context` with "too little exercised": its gameplay
checks need at least 100 gameplay ticks after first control, and those runs
have 2 and 13. The minimum keeps the check from passing on almost no data,
so it stays. Every supported run plays `first_control` and `status` on its
way and checks both against their captures. The side run
`panel_no_battery` checks `first_control` and not the status screen.

**Times** (measured 2026-09-27 on the M1):
- The default target ends at `battery` (lead decision, 2026-09-27, rule 4
  of "Adding a phase") and takes about 15 s: about 12.4 s for the run and
  1.5 s for its checker. It is the shortest supported run, so the default
  cannot come nearer to 10 s by moving its end phase. Most of the run is
  the New Game path to first control (1,393 of its 1,919 logged ticks),
  which every run plays.
- The full main-line run plays 13,039 ticks to Roger in about 108 s; the exit
  phase adds about 700 ticks (and the departure movie's 2 s to its test skip),
  about 130 s in all under load (measured 2026-10-02). Its checker takes
  about 10 s more.
- A run that ends at `fence_door_side1` takes about 56 s.

The default run checks `first_control`, `status` and `battery` against
their captures, and runs the whole-run checks (render context, indicator
children, player draw gate, rand order, sway, marker colour, head sprites,
shadow, chain page, load veil, face attachments, the opening's actors; and
the stream-drive report) over its ticks. Every
later phase, both side runs and the whole route's `--require-through last`
run only under `make test-level-smoke-full` (or `EM_TEST_FULL=1`); run it
before any commit that touches a phase past `battery`. The full target runs
the same whole-run checks over every run it makes: the side runs write and
check their own rand() trace, so rand order, sway, marker colour and head
sprites also cover the ticks of `panel_no_battery`, `fence_door` and
`fence_door_side1`.

The make target does the following:

1. It runs `EM_UNCAPPED=1 EM_STARTUP_TEST=newgame-level EM_AREA_CHANGE_LOG=build/level_smoke/ticks.jsonl EM_RAND_TRACE=build/level_smoke/rand.trace build/extermination`.
   - The app is headless because an `EM_*TEST` variable is set.
   - The frontend drives the title menu and the movie skip, as it does for
     `newgame-control`.
2. It prints the run's `level smoke:` lines.
3. It runs `tools/test_level_smoke.py` over the run log, the tick log and
   the rand() trace (`--rand-trace`; docs/RAND_ORDER.md),
   with `--require-through <phase>`: the phase the run was asked to reach
   (`last` for the whole main line). The checker then fails unless every
   phase the run had to play was checked live against its capture: the main
   line up to that phase, and the side phase itself for a side run. A phase
   that stopped the run as NOT-LIVE, was driven, or was never reached fails
   the target instead of passing with a shorter route (full-route step,
   2026-09-26). The side runs pass their own phase.

`EM_LEVEL_SMOKE_UNTIL` takes a phase name from the table below. The binary's
default is the last phase; the make target's default is `battery`
(rule 4 of "Adding a phase"), and `EM_TEST_FULL=1` or
`test-level-smoke-full` lifts it. An unknown name fails and lists the phases. The run stops
at the first NOT-LIVE phase, because every later beat starts from the state
the earlier beats leave. After the last phase it runs one more frame, with no
input, before it quits: the route captures sample after the original frame,
and the original ticks the 0x28A9A0 fade after the slot-0 task, so the port's
post-frame fade is the next tick's start sample in the tick log. The capture
check of the last phase needs that tick (for example the fade block after
the last phase's release).

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

### The stream drive's two modes

The PS2 disc-drive timing switch (`LAUNCHER_OPTIONS.md`, built 2026-09-27;
`EM_PS2_DISC_DRIVE_TIMING=1`) chooses the IOP stream backend's drive: off
(the default, the Original profile) the disc answers at host speed; on, the
drive model measured in the original runs (IOP_STREAM.md "Host speed and
the PS2 disc-drive timing switch", "Drive model"). The run prints its mode
on the `stream drive:` line (`stream drive: host speed: ...` or `stream
drive: PS2 disc-drive timing on: ...`), and the checker reads it there
(tools/rand_order.py `drive_mode`; a run log without the line fails). Two
checks depend on it; both compare the same code-determined events against
the same captures in either mode:

| Check | Switch on (`make test-level-smoke-ps2-drive`) | Host speed (the default, `make test-level-smoke-full`) |
|---|---|---|
| `check_voice_drive` (cage_roof, crevice_prompt, east_tower) | the voice read takes the capture's fields (7); the lane's start, the hold to the key-on and the sequencer's wait (less lane 0's read) equal the capture's | the voice read takes the host-speed read's 1 field; every ready query and lane-0 read before it takes the host-speed rows (1 row each); the lane's start, the hold to the key-on and the sequencer's wait (less lane 0's read) equal the capture's |
| the teardown in `check_director_beat` | exactly the key-on's shift from the capture's row | the same rule; the shift is now the drive's 6 fields plus lane 0's difference |
| the opening's end in `check_rand_order` and `make test-rand-order` | first control at most 16 frames earlier than the original's (the area music's 16-field seek from the intro movie's position is outside the model) | the port's stream request reads at host speed (1 row of ready query, 1 row of read), its hold to the key-on equals the capture's (4 rows), and first control comes exactly the capture's drive wait earlier: 21 frames, the C7 stream capture's opening request (15 extra rows of ready query while the area music's read finished, 6 extra rows of read) |

The opening's exact rule rests on one thing no single capture shows: the
C7 newgame rand() capture (first control) and the C7 stream capture (the
opening's drive wait) are two runs of the same opening. The stream capture's
fade-in frame equals newgame_samples' (CAPTURES_C7.md section 1), and the
switch-on run obeys the same equation (11 = 21 - the model's 10), so the
two runs' drive waits agree.

The same switch selects the screen-module loader's drive (MODULE_LOADER.md
1.7): the module-0x21 loads of the battery pop-up and the panel prompt take
the loader's 10 host-speed dispatches, or with the switch the captured 24
frames. `check_module_load` (below, "battery" and "panel") compares the
loader rows by the run's mode; the run's `module loader:` line (printed
after the `stream drive:` line) gives the mode and the counters.

Measured 2026-09-27 (full route, both modes; newgame-control):

| | Host speed | Switch on |
|---|---|---|
| first control (newgame-control locked_ticks) | 1301 | 1311 |
| first control against the original's AE+1324 | AE+1303, 21 frames earlier | AE+1313, 11 frames earlier |
| 0x7F key-on and teardown (s) | 8 rows early (6 drive + 2 lane 0) | 2 rows early (lane 0) |
| 0x97, 0x99 key-on and teardown (s) | 6 rows early | on the capture's rows |
| stream reads over the full route | 420, all at host speed | 420 (328 contiguous, 19 fast, 73 full seeks) |
| the frame-order post-control window | native index 1330 (1393 since chain step H7: the New Game's loads and veil take ticks) | native index 1340 (1665 since chain step H7) |

The 30-tick displacement is 9.599849 in both modes. The side runs pass in
both modes too.

### Frame captures

`EM_LEVEL_SMOKE_PHASE_CAPTURE=<phase>:<file.bmp>` saves the frame that ends
`<phase>` (a verification aid in `next_phase`; the headless renderer draws
it as usual). Compare it by eye with the route beat's `original.png`; it is
not a pixel test. For example, the elevator phase's end frame against
`04_elevator_ride/original.png` shows the powered terminal's green arrow
(docs/CENSUS_UNVERIFIED.md, the indicator children). Keep such files under
an ignored `build/<task>/` folder.

`EM_LEVEL_SMOKE_FB_CAPTURE=<phase>+<k>:<file.bmp>` saves the frame of the
k-th scene tick after the one that ended `<phase>` (k >= 1) and prints
`level smoke: fb capture: <phase>+<k> = tick <n>`; `EM_FB_CAPTURE_TICKS=
<tick>:<file.bmp>[;...]` saves the frames of those scene-tick-log ticks
(the log's `tick` numbers; it needs `EM_AREA_CHANGE_LOG`) and prints `fb
capture: tick <n> -> <file>`. Both serve the fb2 pixel harness
(`tools/test_fb2_pixels.py`, `make test-fb2-pixels`, GS_EXACT.md section
10), which checks each captured tick against this checker's own route
alignment before it compares a pixel.

`EM_LEVEL_SMOKE_PAGE_CAPTURE=<state>:<file.bmp>` saves the second status
frame in a row whose ITEM > BATTERY page (002149F0, UI+4 = 5) is at state
<state> (UI+5): 3 the acquisition notice (route 01), 4 the panel's
confirmation (compare with `startup-reference/panel/confirm.png`). The page
and its lines are the originals since the status UI step
(docs/STATUS_PAGE_RECORD.md section 7).

The terminal's arrow is red during the refusal (route 02's capture: level
+0x28 = 0) and green after the power (route 04). The elevator/refusal
fixture's screenshot (used by tools/test_message_capture.py) shows it green
because that fixture was seeded from a powered state (CENSUS_UNVERIFIED.md,
"The terminal-screen colour").

## Phases

The phases follow the main line of FIRST_LEVEL_ROUTE.md section 3. The side
beats 00 and 09 are side phases (`Phase.side`): the main line names them
(`side beat, not on the main line`) and does not run them, because each
starts from its own snapshot in the route. `EM_LEVEL_SMOKE_UNTIL=<side
phase>` runs the main line up to it and then only it: `panel_no_battery`
(beat 00, from first control) and `fence_door` (beat 09, from the truck
crossing's end) are live, each in its own run (`make test-level-smoke-side`
runs both). `fence_door_side1` starts from `fence_door`'s end
(`Phase.from_side`): `EM_LEVEL_SMOKE_UNTIL=fence_door_side1` runs the main
line, `fence_door`, then it (the side-9 run of `make test-level-smoke-side`,
which `make test-level-smoke-full` runs), and `--require-through
fence_door_side1` requires both side phases.

| Phase | Route beat | Original owners | Live | Waits on |
|---|---|---|---|---|
| first_control | 01 row f0 (slot 04) | 0x1AE040 state 1 / 001AE5E0, 49 pool nodes | yes (S12a) | — |
| panel_no_battery (side) | 00 | panel 00159210 without item 0x1B, script 0x246F20, message 0x80000018 | yes, its own run (WP-4; compared since 2026-09-25) | — |
| status | 01 status exit; frame_trace2 `status_04.json` | 001AE7E0 r==2 → state 3 → 5 → 1 | yes (S11b; the original page core, hub, models and 0020E0C0 exit since WP-5) | — |
| status_pages (side, designed) | none: no capture shows a page open | 0020CDC0 phase 3: DATABASE 00214020, SPR4 00211970 and its part pages, MAP 0020F950 and its nodes 002101C0, EQUIPMENT / EVENT / HEALING, the takes of 0x1E / 0x1F / 0x32 / 0x10 / 0x08 | yes, its own run (chain C8b FAILSTOPS and its MAP fix round; every page and node call replayed through the original instructions; since chain step PAGELOADS every page module load's loader rows) | pixels (no capture); the page loads' drive time (no capture) |
| battery | 01 | pickup 00219550 g0.0, take script 0x266620, ITEM page | yes (WP-6; the status pop-up since WP-5) | — |
| elevator_refusal | 02 | terminal 0x827B10, script 0x82A990, message 0x8000001A | yes (WP-4) | — |
| panel | 03 | panel 00159210, scripts 0x2477A0/0x247BE0, 00157F60 BATTERY page, power 0x80 | yes (WP-4) | — |
| elevator | 04 | terminal 0x827B10, script 0x82A750, carry 0x828050 | yes (WP-4) | — |
| boxes | 05 | Use dispatcher 00160220, ledge climb 0015DF10 / state 2 onto crates r4/r3 (001551B0) | yes (census L25) | — |
| slide | 06 | floor class 0x1000 -> 001796C0, slope slide 0016C6A0 (state 0x1C, +1F0 0x30) | yes (census L03) | — |
| truck_preview | 07 | trigger 0x8251E0, camera script 0x8292C0 | yes (census L23, L19) | — |
| truck_crossing | 08 | truck 0x823FF0 | yes (census L23) | — |
| fence_door (side) | 09 | door 001BC350 (001BBE40, the ELF program 0x24DE40 on the AREA11 script host, 001BC150), room move to entry 2 (0x1AE040 state 4) | yes, its own run (census L18) | — |
| fence_door_side1 (side, from fence_door) | C7 DOOR1 (not a route beat) | the same door from behind the fence (clip 0x43, B7 = 1), room move to entry 1, 001B07C0(1)'s walk-out 5 / 1 / 0: the player's 0015B610 / 00183250 | yes, after fence_door in its run (2026-09-27) | — |
| aim_r1_hold / aim_r2_hold (side, from 08) | AIM aim_00 / aim_01 (decomp CAPTURES_C10.md, not route beats) | R1 0016FCF0 / R2 001703E0 stances, camera actions 1 / 2 (00197D20 / 00198650), the release 00197490, then action 0 | yes, each its own run (chain step AIMCAM's fix round, 2026-10-01; no gate since 2026-10-02) | — |
| aim_fire / aim_both / aim_reload / aim_reload_empty / aim_melee (side, from 08) | AIM aim_03 / aim_02 / aim_06 / aim_07 / aim_09 | the fire machines, the round 001861C0 and its marker 0018ABA0, the muzzle node 001F5040, the casing 001F4010, the reloads 0017B300 / 0016F600, melee 001735C0 / 00173E60 with the knife's 001AA840 / 0019B2C0 and its trail 001F18C0 | yes, each its own run (chain step AIMLIVE, 2026-10-02) | — |
| aim_light (side, from 08) | AIM aim_08 | the gun lamp 00187780 / 00187690 (Square: the flare; the cone 001D9530 is skipped in AREA11) | yes, its own run (2026-10-02) | — |
| aim_world (side, from 08) | AIM aim_04 | the walk and the stick aim, rounds into the ground, the pillar, past the fence and a miss: the impact 0x80000060 (001EACF0, the streak program 0x230800), the ring decal 001F0460 / 001EBBB0 and its lanes, 001A9C40 in the close-out | yes, its own run (2026-10-02) | — |
| aim_burst (side, from 08) | AIM aim_05 | START: the status hub 00209DF0, the SPR4 page 00211970 and its SELECTOR part page 00217FA0 pick the 3-round burst (D_00810C61 = 1); R1: burst fire, 00170A60 states 0x14..0x17 | yes, its own run (chain step AIMCAP, 2026-10-02) | the page modules' drive time (host speed; the capture's loads are longer) |
| aim_cable (side, from 08) | AIM aim_10, then aim_11 | rounds at the security gun's cable, then the knife at its foot: 0018A180 (001B61C0), the reaction 001EFE00 / 001EFEB0 / 0021AAC0 (the kind-2 program 0x232540) / 0021A500 (its strand strips), 001EAB50, the gun's lifecycle 2, the cable freed | yes, its own run (2026-10-02) | — |
| cage_ladders | 10 (f0..f1090) | Use 00160220 -> 0015D4C0 case 0x32, ladder entry 00165B60 (state 0xB), climb 001662D0 (state 0xC); the walk's step-off fall 00162DB0 / landing 00163B40 | yes (census L09, L10) | — |
| cage_roof | 10 (f1090..) | director 0x8253F0 beat 0 script 0x8294C0, Roger 0x8237E0 script 0x828990 (voiced line 0x7F) | yes (census L21 with WP-8b) | — |
| crevice_climbs | 11 (f0..f706) | ledge climbs 0015DF10 onto the tank and the pipe end; the pipes walk's step-off | yes (census L04, L02) | — |
| crevice_prompt | 11 (f706..) | director beat 1, script 0x829A40 (voiced line 0x97) | yes (census L21 with WP-8b) | — |
| dmg_flame (side, from crevice_prompt) | DAMAGE dmg_00..dmg_04 (decomp CAPTURES_C10.md "DAMAGE", not route beats) | the flame's contact 001A8660 / 0x823580 (001EFE00: the burn node 0022BBC0), the hit 0021C440 / 0021D800 and the rumble 001B61C0, the heartbeat 0015D000, the death 0021D2E0 and its decal 001F77B0, the game over 001AD140 / 001AD4E0 (screen module 0x27, 001ABF90) / 001ADF00, the title after a death 001AC070 / 001AC480, the New Game to first control | yes, its own run (chain step DAMAGE, 2026-10-02) | — |
| dmg_crevice_fall (side, from crevice_prompt) | DAMAGE dmg_06 | the walking jump short of the north block, the landing hit 0017C580 / 00163E90 | yes, its own run (chain step DAMAGE) | — |
| dmg_pit_fall (side, from truck_preview) | DAMAGE dmg_07 | the truck's fall, the walk off its roof, the 0x5D floor's death 0021D250 / 0021D2E0, the game over | yes, its own run (chain step DAMAGE) | — |
| br_panel_decline (side, from elevator_refusal) | BRANCH br_03 (decomp CAPTURES_C10.md "BRANCH", not a route beat) | the panel 00159210 with the battery: script 0x2477A0, the BATTERY page's two-unit prompt, No, Triangle: the cancel script 0x247DA0; the power stays off | yes, its own run (chain step BRANCHES, 2026-10-03) | — |
| br_elevator_up (side, from elevator) | BRANCH br_02 | the terminal 0x827B10 on the lower floor: 0x82A750 and the carry 0x828050 back up, D_0081083A 1 -> 0 | yes, its own run (BRANCHES) | — |
| br_crate_stack (side, from elevator) | BRANCH br_04 | the light melee on box r5 (001551B0's damage break: the husk rebind, 001FC580's cue, the debris 0x8000000A (001F2BA0) and 0x80000015 (001EA240 subtype 0x0D, 001EBD20)), the raised r3 woken, its fall and break | yes, its own run (BRANCHES) | — |
| br_ledge_ammo (side, from boxes) | BRANCH br_00 | pickup 00219550 g0.3 (item 0x1E): the take, its ITEM page, the taken bit | yes, its own run (BRANCHES) | — |
| br_map_item (side, from slide) | BRANCH br_01 | the map item 0015AFA0 g0.6 (item 0x08): the grab clip 0x40, the MAP take page, the taken bit | yes, its own run (BRANCHES) | — |
| br_west_ledge (side, from fence_door) | BRANCH br_05 .. br_08 | the corridor box's ledge climb and step-off (the skid 0x80000033: 001EAD70), the west-yard ladder up, box r6 broken, pickup g0.5 (item 0x10), the ladder down (the grab from above, 0x16) | yes, its own run (BRANCHES) | — |
| br_yard_ammo (side, from fence_door) | BRANCH br_10 | pickup g0.1 (item 0x1E) on the yard floor | yes, its own run (BRANCHES) | — |
| br_cage_key (side, from truck_crossing) | BRANCH br_09 | ladder A, pickup g0.4 (item 0x32): the take and its page (00214020's) | yes, its own run (BRANCHES) | — |
| br_plateau (side, from crevice_prompt) | BRANCH br_11 .. br_13 | the raised pipe's ledge climb and step-off, the plateau ladder up, pickup g0.2 (item 0x1F), the ladder down | yes, its own run (BRANCHES) | — |
| br_roger_talk (side, from roger) | BRANCH br_14 | Roger 0x8237E0's third branch 0x823B70: the use scan marks him (+0x0B = 4), the talk script 0x828810 (line 0x13, VOICE.DAT cue 1) | yes, its own run (BRANCHES) | the voice read's drive time (host speed; the recording's with the PS2 disc-drive timing switch) |
| crevice_jump | 12 | running jump 0015EC50 / 001634A0 (+1F0 0x0C), landing 8 / 0xF; the approach's step-off | yes (census L11) | — |
| east_tower_climb | 13 (f0..f531) | high ledge climb 0015DF10 onto the east tower top | yes (census L04) | — |
| east_tower | 13 (f531..) | director beat 2, script 0x829CC0 (voiced line 0x99) | yes (census L21 with WP-8b) | — |
| roger | 14 | Roger 0x8237E0 quad 0x82AB80, script 0x8283D0 (bank 0x96: 0022EEF0 camera, the player's clip 1 through 00183090), equipment 001C5C90 | yes (census L22) | — |
| exit | 15 (EXIT capture: exit_00 f31.., exit_01 ..f306) | fan r2 00827630's exit box, Roger's departure 0x828A10 (op01 kind 3, op0F: the movie E001.PSS), 001B0C60(1, 0, 4), 001AD010 / 001ADF50, 001FF080(1, 0) (AREA01 sub 0), the AREA01 arrival 0x1AE040 state 0 | yes (chain C11 EXIT, 2026-10-02) | — |
| a01_arrival | 15 (route 15 f741..f801) | the AREA01 rebuild, then 0x1AE040 state 1 / 001AE5E0 over AREA01's 54 placements and spawned owners (LEVEL2_BINDING.md), the camera's one-shot seat 0018B9C0 state 0 (mode 8), the close-out's 001AA140 / 001AA000 | yes (step GUARD, 2026-10-04) | the AREA01 owners' records (not in route 15) |
| a01_00 (opt-in, from a01_arrival) | a01_00_train_room (780 frames) | the recorded pad: the walk round the crates and through the floor fields (001A8840's contact 00187EC0, the splash 001EAF00, the wet-feet decal 001F0460), the Use scan 00184BA0 and ledge grab (f304), the hang 001647D0 with 00182250's alignment on the crate cell (f357), the pull-up (f360..), the fall and landing (f491..f520), the walk to the tunnel mouth | yes (step DRAWN, 2026-10-04: all 781 rows, the kind-6 near-fire program D_0023D930 drawn from f405) | — |

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
initialises them). `EM_LEVEL_SMOKE_HUB_CAPTURE=<file.bmp>` (in any run
that plays the status phase, for example `EM_LEVEL_SMOKE_UNTIL=battery`)
writes the hub frame whose walk equals the
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
row for row again to the page's module load (f189..f192). Then the ITEM
root's module-0x21 load (`check_module_load`, MODULE_LOADER.md section 4.1):
the tick log's `loader_pre` bytes (slot 2's record and D_00275BD8 after each
frame) from the request row f194 to the load's completion equal the
load-wait probe's rows f194..f217, without their 14 busy polls at host
speed (10 rows), all 24 with the PS2 disc-drive timing switch. The turn
(op0E) is not compared either: its step count depends on the stance.
Negative controls: a changed spad or request byte in the window fails.

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
panel is compared from the scan through the status open (plus B0/B1 from the
request row); then the ITEM root's module-0x21 load (`check_module_load`,
MODULE_LOADER.md section 4.1: the `loader_pre` rows from the request f391 to
the load's completion equal h7's f391..f414, without the 14 busy polls at
host speed, all 24 with the PS2 disc-drive timing switch); from the load's
completion to the Yes press, row for row at the drive's shift (14 at host
speed, 0 with the switch), with the prompt taking the request exactly that
shift earlier than the original's 30 ticks (16 at host speed); and from the
Yes confirmation (B0 0 -> 1) through the discharge, the exit, script
0x247BE0, the power bit and the release (plus B0/B1); its player Y is
checked as retained while the
script owns the player (001B6F00 keeps the ground Y of the approach, which is
navigation input), and equal to the capture after the release. The elevator
window also compares the terminal record 00827B10 with the route rows'
`elevator_r19` row for row: its +0x04 and +0xB0..+0xB8 (the carry
00828050's +0xB4 steps, 230 down to 190; the tick log's `terminal`, since the
owners step: the record's own model bind, 001C6380 and +0x4C,
OWNER_DRAW.md section 10). The tick log gained
the fields these need (em_scene_bindings.c): `screen8`, `msg_pre`, `cam4`,
`power`, `floor`, `pos_post`, `yaw_post`, `eye_post`, `tgt_post`, and for
the boxes `player`; since chain C8b LOADER also `loader_pre` (slot 2's
record +0, +8..+0x1F, D_00275BD8 and D_00282157 after the previous frame's
dispatch, MODULE_LOADER.md section 4 item 9). Negative
controls (one tampered letterbox byte, carried Y, camera eye, power byte,
message phase, message kind or message token in a copy of the log) each fail
the check.

*What the message block measures.* Since WP-8 the log samples the live
message service's block itself (D_002821B0 mode, B4 phase, B8 line; the one
storage, `em_message_live`): the line the running script posted (0x80000018
panel, 0x8000001A refusal) through 001B7D60 case 0, the phase through the
service's ticks and its 001FC9B0 teardown.

**Known divergences, reported, not compared:**
- *The status page's module load at host speed.* The ITEM root's
  module-0x21 load runs the loader's own steps (MODULE_LOADER.md): at host
  speed it takes 10 dispatches where the original's disc takes 24, so from
  the load on the port runs 14 frames ahead of the capture (the rows are
  compared at that shift). Across it, the frame counters and the stream
  lanes' refill phase run 14 behind and the port's rand() sequence is 28
  draws behind (the wait's 001D7C30 draws twice per frame); rand() values
  are not compared (check_rand_order compares the aligned windows'
  callers). With the PS2 disc-drive timing switch the shift is 0.
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
  offset, also in the prompt window between the load and the Yes press.
- *D_00282157 and the voice lanes D_00282155/156* are the stream lanes'
  (em_stream_live, WP-8b): D_00282157 is the phase of 001FA0D0's disc read,
  read by 0x1AE040 state 3 and passed to the status page input (which does
  not read it). No voice cue is pushed on the route before the director's
  beat 0; the panel's and the terminal's messages are text-only.

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
and the 001EFD90 spawns are the live effect binder's, compared at the
effect beats by check_effects, L26); the camera
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
effects (TRUCK_ORIGINAL.md; live effect nodes, check_effects, census L26); the
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

### cage_roof, crevice_prompt, east_tower

The director 008253F0's beats 0, 1 and 2 (FIRST_LEVEL_ROUTE.md section 5) on
the original owner (em_director_original over em_area11_script_host, live
since WP-8b): scripts 0x8294C0 (with Roger's 0x828990 and his voiced line
0x7F, VOICE.DAT cues 143..148), 0x829A40 (line 0x97, cue 150) and 0x829CC0
(line 0x99, cue 149).

**Runner.** Each beat opens on its own when the previous phase leaves the
player inside its quad. The pad stays neutral until the beat has stored its
step byte D_00810813 (beat 0: the director's 0x10, then Roger's ordinary
branch's 0x11 on the next frame; beats 1 and 2: 0x20 and 0xFF) and control
is back, then settles 90 frames (each capture ends 60 rows after its
release; a port whose line tears down early releases early). **In
process:** the step byte and control.

**Against the capture** (`check_director_beat`, routes 10, 11, 13).
Aligned on the director's frame (the first tick whose 3B8D leaves 0: route
10 f1090, 11 f706, 13 f531), every row to the end of the capture: spad,
camera byte, letterbox, power, the message block, the player's position
(relative to the stance, which is navigation: 0 after the ladder in beat 0,
0.27 / 0.05 and 0 / 0.003 in beats 1 and 2) and heading, the player record
(+5, +1F0, +1F1, ground) and clip, D_008107D8 / D_00810793, D_00810813,
the camera (the scripts' shots exactly; the follow camera before the
frame's first shot is the walk's, not compared; from the release's restore
row relative to the stance) and, in beat 0, Roger's record and script block
and the follow camera after the release row for row.

**The voiced line's teardown (check_voice_drive).** A voiced line holds its
teardown until the last voice lane's timer ends (D_00282155/156). The timer
runs from the lane's key-on, and the key-on waits for the lane's first disc
read. The port's drive answers at host speed by default, or runs the model
measured in the original with the PS2 disc-drive timing switch on
(IOP_STREAM.md; "The stream drive's two modes" above).

The checker compares the line's first voice lane with the C7 stream capture
of the same beat (the decomp's `build/s87/c7cap/stream/r10|r11|r13`,
CAPTURES_C7.md section 1). It reads the tick log's `stream` row: D_00810E90,
D_00282157 / 58, the active bytes and each lane's +0x03. It requires:
- the lane's start on the capture's row;
- the read, from issue to done, taking the capture's fields (7) with the
  switch on, or the host-speed read's 1 field at host speed, where every
  ready query and lane-0 read before it must also take the host-speed rows;
- the key-on following the capture's 2 fields later;
- the sequencer's wait before the read differing only by the fields 001FA0D0
  spent on lane 0's music refill first;
- the teardown lying exactly the key-on's shift s from the capture's row.

The fields that follow the teardown are then compared on its clock: the
message block, and in beat 0 Roger's record, his D_00810813 = 1 and the
player's clip back to idle. Every other field keeps the capture's rows,
including beat 0's camera shots and its release at f3508.

Measured 2026-09-27, switch on:
- **0x97 and 0x99:** s = 0, every change on the capture's rows (beat 1 298,
  beat 2 138).
- **0x7F:** s = 2 (f3380 against f3382), 822 changes on the capture's rows
  and 8 on the teardown's. The original's sequencer served a lane-0 refill
  for 2 fields first, its read issued in f1164. The port's music was then in
  another phase: keyed on 3449 fields before the voice, against the
  original's 3583 (vsync 15472, after route 03's status close). That
  difference is navigation.

Measured 2026-09-27, host speed (the default):
- **0x97 and 0x99:** s = 6, the drive's read time (beat 1: 170 changes on
  the capture's rows and 128 on the teardown's; beat 2: 70 and 68).
- **0x7F:** s = 8 (f3374 against f3382): the same 6 plus lane 0's 2; 822
  changes on the capture's rows and 8 on the teardown's.

Mutation controls: with the switch on, a zero-latency drive and a 7-field
full seek each fail check_voice_drive at cage_roof. At host speed, a read
one field slower fails it (and the opening's exact end).

### panel_no_battery (side beat 00)

Route beat 00 from first control (`make test-level-smoke-side`). **Runner:**
the stick walks to the route's press stance (242.605, 226.742; f71) and
faces 0.69894, then Cross. The panel 00159210 without item 0x1B starts
0x246F20 in the scan's frame (3B8D 0 -> 2, f75), message 0x80000018
(f79..f229), the bars, the release at f230. **In process:** the scan, the
message, the letterbox and camera byte 1 were seen, no item and no power,
control returns. **Against the capture (check_panel_no_battery):** aligned on
the scan, f75 through the release and 25 rows after: spad, camera byte,
letterbox, message, power (compare_window, the camera's Y with the
approach's retained ground offset, as the powered panel's), the placement
(X/Z) and heading row for row from the scan row (the script places the
player in the scan's frame), the re-grounded Y after the release, the player
record (+5, +1F0, +1F1, clip, ground) from the row after the scan; the
follow camera converges to the capture exactly from f244 (at most 0.00023
off at the release, the retained ground Y). Measured 2026-09-25: port ticks
1478..1657 equal route 00 f75..f254.

The follow-camera convergence check allows one unit of the capture's
five-decimal rounding per row (1e-5 plus the binary representation of the
rounded decimals); before, 0.00023 after 0.00022 failed on the float
representation of 1e-5.

### status_pages (designed side run, chain C8b)

No route capture shows a status page open, so this run is designed input
(`make test-level-smoke-side`; STATUS_PAGES.md section 7). **Runner**
(`status_pages_frame`, a script of pad steps): from first control, after the
status phase and 120 idle gameplay frames, START opens the hub; the stick
selects hub hover 1 (DATABASE: the category list, D-pad right, Cross),
hover 2 (SPR4: the selector's hover 6, LOWER U.R.S., Cross on its equipped
entry; hover 4, SELECTOR SWITCH, down, Cross, left to Yes, Cross: the fire
mode changes), hover 3 (MAP with no map owned: the list, D-pad down) and
hover 4 (ITEM: its hovers 1 EQUIPMENT, 4 EVENT and 5 HEALING), leaving each
page with Circle (the pages' back, pad bit 0x20;
Triangle 0x10 and START 0x800 close the whole screen, as 0020CDC0's 0x810
test and the hub's 0x830 say). Then START closes, and the takes run: the use
claim of the pickup owner (the claim the scan makes, taken directly: test
input) for 0x1E and 0x1F, each used from its HEALING notice (Cross, Cross,
left to Yes, Cross) from health 30 (test input) through 002160B0's
count-up and 0015C700 (60, then 100), the key 0x32 (DATABASE on its record),
the magazine 0x10 (SPR4's notice) and the map 0x08 (MAP zoomed on map 8:
R1 for 8 frames, the D-pad right and up for 6 each, Circle to the list,
Cross zooms again with the player's marker, R2 for 4 frames, Circle twice
to the hub; then MAP from the hub again with map 8 owned: Cross, Circle,
Circle), each closed with START. **In process:** every step's page-core
state is reached in time, MAP, SPR4, DATABASE, EQUIPMENT, EVENT and HEALING
were shown, the fire mode changed and
the health ends at 60 and then 100. **Against the original
(check_status_pages):** the run writes every page call to
`EM_STATUS_PAGES_TRACE` (em_status_pages_live: the views before, each
callee's entry and the bytes it wrote, the views after), and
`tools/test_status_pages_live.py` runs each call's original page routine in
the EE interpreter over the status-hub capture with the port's views
written over it, hooking each callee the port dispatched: every callee
entry (address, stack pointer, 64-bit argument registers, float arguments)
and every view byte after the call must equal the original's (the stack's
register save slots excepted). A MAP node's 002101C0 call (from the pool
walk 001B0000) is its own traced call, with its pool record as the view.
The checker also requires MAP's model path: its nodes bound map models
(001CA5E0) and drew them (001CB480). **The page modules' loads
(check_page_module_loads, chain step PAGELOADS):** every page module the run
loads runs the screen-module loader's steps; from the tick log's
`loader_pre` bytes each load's rows (the slot-2 record and D_00275BD8
after each dispatch, from its first to the idle row) must equal route 03's
captured module-0x21 rows without the 14 busy polls (h7 f391..f414: the
same one-chunk header shape; MODULE_LOADER.md finding 8) with the record's
module byte +0xE the page's, and the run must load exactly 0x1E, 0x1F,
0x20, 0x22, 0x23, 0x24, 0x2C, 0x2D and 0x31. Measured 2026-09-30 (chain
step PAGELOADS): 6,634 page and node calls (MAP 242, its nodes 5,258, SPR4
322, DATABASE 152, EQUIPMENT 55, EVENT 45, HEALING 560), 19,192 callee
entries, 4 model binds, 390 001CB480 draws and 20 page module loads of 10
dispatches each (before the step the loads were instant: 6,643 calls,
19,660 entries). The whole-run checks (render context, rand() order, sway, marker
colour, head sprites, shadow, chain page, load veil) run over this run too.
`EM_LEVEL_SMOKE_PAGES_CAPTURE=<dir>` writes one frame of each page for a
look (no capture to compare with; MAP writes one per mode: map_<t[3],
+ 4 once map 8 is owned>.bmp).

### fence_door (side beat 09, census L18)

The runner (`fence_door_frame`) starts at the truck crossing's end: the
stick walks the route's path to the fence door ((386, 348), (395, 340),
(402, 317), (413, 296); tolerance 1.5; the fence stops the walk at
(414.9, 292.8) as at route f135), then to the press stance (417.786,
293.837; f255) at 0.4 stick within 0.1, faces 2.4073 and presses Cross (f306),
navigation input only. It then waits, with the pad neutral, for the door's
phases 3, 4 and 5, B8 = 2, the door back in phase 0, D_00810702 = 2 and
control, and settles 70 frames (the capture ends 60 rows after the
re-place).

**In process:** the scan (3B8D != 0), the door's phases 3 / 4 / 5 and 0 with
its armed byte clear, B8 = 2 seen, the player re-placed in room 2.

**Against the capture** (`check_fence_door`), aligned on the scan (the
first tick with 3B8D != 0, route f309, where 00184BA0 armed the door and
its 001BBE40 ran in the same frame), row for row to the capture's end (f532,
224 rows):
- spad, the camera byte, the letterbox, the message block, power and
  B0 / B1 (`compare_window`), the script's camera eye / target while the
  frame is open;
- the player's position (X / Z exact, Y within 1e-5) and heading on every
  row: 001BBE40's 00182F90 alignment and +0xC4 at f309 (bit-exact only on the
  EE float model, DOOR_ORIGINAL.md), then 001B07C0(1)'s entry 2 at f472;
- the player record's +5, +1F0, +1F1 and clip on every row, its clock from
  the program's clip 0x45 on (f313; before it the idle clock counts from the
  stance the navigation reached);
- the door record's +0x00..+0x0F and script block +0x1F0..+0x1FF (the route
  rows' door_r0 "h" and "s1F0"): phases 3 (f309), 4 (f406), 5 (f407) and 0
  (f472), the program's pc / phase / skip byte and the advance flags
  +0x1FE (clip 2's end bit from f464), +0x09 / +0x0C (001B0EA0's slots);
- the room move through `tools/test_room_move_reference.py`'s checks over
  the phase's tick log: B5..B9 and the fade block from the B8 = 2 row for 70
  rows, the re-place row's position and heading, 001AD010 against the
  executed original, the state-4 tick's nine calls then 001AE7E0 /
  001AE5E0, one weather and one title node before and after;
- the follow camera from the re-place to f532, exact (eye, target, the
  block's +0x10 / +0x20 and +4..+7).

### fence_door_side1 (the C7 DOOR1 capture, from fence_door's end)

The capture is the decomp's `build/s87/c7cap/door1/c7_door1_fence_door_side1`
(CAPTURES_C7.md section 4). It starts from route 09's end snapshot: entry 2,
behind the fence, heading pi. The capture's own walk to the door (f0..f224)
is its tool's navigation, not a route, and is not compared.

The runner (`fence_door_side1_frame`) starts where `fence_door` ends. It:
- turns toward the door (two `nav_face` calls, since the turn from pi takes
  longer than one);
- walks to (422.8, 282.0) at 0.6 stick, within 1.0;
- walks to the capture's press stance (422.757, 284.633; f225) at 0.4
  stick, within 0.1;
- faces the capture's heading -0.12148 and presses Cross for two frames.

It then waits, with the pad neutral, for the door's phases 3, 4 and 5, the
player record at +4 = 5 / +5 = 1, the door back in phase 0, D_00810702 = 1
and the record back at +4 = 1, +5 = 0, +1F0 = 0 with control. It settles 61
frames (the capture ends 60 rows after control returns).

**In process:** the scan, the door's phases 3 / 4 / 5 and 0 with its armed
byte clear, B8 = 2 seen, the walk-out's state 5 / 1 seen, the player
re-placed in room 1 and returned to +4 = 1.

**Against the capture** (`check_fence_door_side1`), aligned on the scan
(f228), row for row to the capture's end (f544, 317 rows), with the same
fields as `check_fence_door`:
- spad, the camera byte, the letterbox, the message block, power and
  B0 / B1, and the script's camera while the frame is open;
- the player's position (X / Z exact, Y within 1e-5) and heading on every
  row: 001BBE40's side-1 alignment at f228, 001B07C0(1)'s entry 1 at f371,
  then 00183250's walk-out (Z +0.3 per frame from f423, the hand-over frame
  f453, the slowing steps to Z 312.51059 at f483);
- the record's +5, +1F0, +1F1 and clip on every row, and its clock from the
  program's clip 0x43 on (f232). This covers clip 2 from f371 with its clock
  (1.0, then 45 counting down) and clip 0 from f480;
- the door record's +0x00..+0x0F and +0x1F0..+0x1FF;
- the room move through `tools/test_room_move_reference.py`'s checks (B7 =
  1 this time);
- the follow camera from the re-place (f371) to f544, exact;
- the record's +4 (the tick log's `player` 8th value), which the capture does
  not sample. It is 1 before 0015B130's admission, 4 from the admission to
  the tick before the re-place, and 5 from the state-4 tick (001B07C0(1),
  with +5 = 1, +1F0 still 0x41 and the selector 0). It is 1 again from
  00183250's exit (f484), with +5 = 0 and +1F0 = 0.

A mutation run (00183250's standing timer 49 instead of 50) fails at f422
on the player's Z.

### aim_r1_hold, aim_r2_hold (the AIM captures aim_00 / aim_01, from 08's end)

The captures are the decomp's `build/aimfire/capture/aim_00_r1_hold` and
`aim_01_r2_hold` (docs/CAPTURES_C10.md "AIM"): from route 08's end snapshot
(the player idle at (371.50317, 184.84026, 361.34225), heading 2.38104),
R1 (R2) pressed at f10 and released at f89; the stance at f13, +6 = 2 at
f29, 0x63 at f92, the holster, idle at f116 (f124), the capture's end at
f146 (f154). The stances, their camera and the gun run their original
bodies (the only path since 2026-10-02; until then the side runs set the
aim/fire gate, AIM_FIRE.md section 1).

The runner (`aim_hold_frame`) starts where truck_crossing ends. It walks to
a point 6 behind the start along the heading, then straight in with the
stick on a far point of that line (0.6: 0.1 per tick) and releases it 1.8
short (the walk-stop slides on): the run stands 0.65 from the capture's
place with a heading 0.015 off (the stick's resolution). It settles, waits
for the idle clip's +3C to count down to 13.0 (the capture's row f12, so
the draw starts from the same idle frame), presses R1 (R2) and holds it 79
ticks from the stance, then runs to the capture's last row. In process:
the stance and its action code on the first held tick, +6 = 2 sixteen
ticks later, 0x63 on the release tick, idle at the end, no fault.

`check_aim_hold` compares every row from the stance on: the player's +5,
+6, +7, +1F0, +1F1, clip, clock and action code +230 and the camera bytes
D_008101E4..E7 exactly; in the player's frame the eye D_008105D0, the
target D_008105E0, the camera block's eye +0x10 and target +0x20 and the
player's +A0 / +B0 (the tick log's `aim` field: +6, +7, +230 and the
camera's view of +A0..+A8 / +B0..+B8) within 0.002, apart from two
components that depend on where the run stands: the eye's lateral offset
starts from the follow camera's rest (0.346 in the capture, 0 after the
straight walk-in), may never exceed that start difference and agrees within
0.002 from f69 on; D_008105E0's x / z chase each world axis by at most 1.0
per tick in aim_00's release (f94..f99), so they depend on the heading: on
those rows the port's world steps equal the capture's within 1e-4, and
elsewhere the local difference stays within 0.2 and is within 0.002 on the
settled hold and the last row. At the last row the render context's
D_00275690 / D_00275694 and +0x245C..+0x2467 equal the capture's end
snapshot; check_render_context skips its fixed-point check of those bytes
from the stance on (001DDE10 eases them toward the aim mode's targets and
back). Measured: both pass (aim_00: the target's x 0.125 / z 0.056 in the
release, the start's lateral 0.346; everything else within 0.0013).

A run that stood 3 units off the captures' place found the follow camera's
eye climbing six ticks late after the release: there the prepass
0018D330's ground test under the hip (+0x6D) misses, and the per-state
height 00191390 waits for it. The place matters; the binding does not
differ.

### The AIM replays (aim_fire, aim_both, aim_reload, aim_reload_empty, aim_melee, aim_light, aim_world, aim_cable, aim_burst)

Side phases from route 08's end, like aim_r1_hold (AIM_FIRE.md sections 9
and 10). Each replays one AIM capture (decomp build/aimfire/capture,
CAPTURES_C10.md "AIM"): aim_fire = aim_03_single_fire, aim_both =
aim_02_r1_r2_both, aim_reload = aim_06_reload_partial, aim_reload_empty =
aim_07_reload_empty, aim_melee = aim_09_melee, aim_light =
aim_08_light_holster, aim_world = aim_04_world_hit, aim_cable =
aim_10_cable_shots and then aim_11_cable_melee (recorded from aim_10's end;
its frame f is the replay's frame 1448 + f: aim_10's last frame 1441 plus
the seven idle frames between the recordings, by their frame counters),
aim_burst = aim_05_burst_fire (chain step AIMCAP: its pad script from the
file, its first input frame 10, compared from f13; its status screen runs
main-loop iterations that close out neither a world nor a status frame,
so the phase's frame function runs on every iteration, Phase.every_tick:
without it the script fell two frames behind at START).

The runner (`aim_replay_frame`, table `k_aim_replays`) walks in as
aim_hold_frame does and waits for the idle clip's +3C to reach the
capture's value at its row first + 2, where `first` is the capture's first
input frame (10 for the button replays: 12.0, 12.0, 8.0, 13.0, 13.0, 13.0
at f12; 5 for the stick replays: 10.0 and 18.0 at f7). It then feeds the
capture's own pad script frame for frame: the button replays from their
tables (the trace.json input words), the stick replays aim_world /
aim_cable with both stick bytes from the file `EM_AIM_PAD_SCRIPT`, which
tools/test_level_smoke_aim.py writes from the captures' trace.json
(`pad_script`). At the aligned tick the run log gets "level smoke: <phase>:
aligned counter=N"; the checker takes the replay's first compared tick as
the next one (for the button replays it also requires the first stance
tick to be that tick).

`check_aim_replay` (tools/test_level_smoke.py) compares every row from the
row after the alignment (f13, or f8 for the stick replays) to the last:
exactly the player's +5, +6, +7, +1F0, +1F1, clip, clock, the action code
+230 and +0x274..+0x27F, the fire mode D_00810C61, the magazine D_00810C62,
the reserve D_00810CB4, the light D_00810D3C and the gun node's +0x2E event;
per row the set of fire-path records (the impact markers 0018ABA0, the
muzzle nodes 001F5040, the impact effects of subtypes 0x1B / 0x23 / 7, the
knife's trail nodes 001F18C0, and the cable reaction's nodes 0021AAC0 /
0021A500 with its effect of subtype 0; the capture's pool_delta against
the tick log's `fire` list) with +4, +0x0C, +0x0D and +0x28 equal (+0x28
not for the effects, the trail and the cable nodes); each muzzle node's
+0xB0 in the player's frame within 0.002. **The places the rounds struck
(each marker's and impact effect's +0xB0) are compared by direction only:**
their bearing from the player within the start heading's difference +
0.002, and, while a muzzle node lives, their elevation seen from the muzzle
point within 0.002. Where along the struck surface they lie is not
compared: the run stands about 0.65 from the capture's start (and after
aim_cable's walk about 1.0), so the range differs. The trail's +0xB0
equal as stored; the cable nodes' +0xB0 equal in the world; the cable
hit's effect by its height and the pillar face's z. aim_cable also
compares, on every row, the security gun's and the cable's +0, +4, +5, +9
and the cable's +0x36 with the capture's, and check_gun_fan's static gun /
cable comparison stops at its first tick.

Measured (2026-10-02): aim_fire 343 records (muzzle 0.0000), aim_both 0,
aim_reload 343, aim_reload_empty 2940 (f13..f1231), aim_melee 40 (the
trail on all of the capture's rows that hold it: f26..f29, f87..f90,
f102..f108, f119..f129, f193..f206), aim_light 0 (304 rows, the lamp on
and off, holstered and redrawn), aim_world 319 over 1397 rows (124 struck
points also by elevation), aim_cable 1002 over 2020 rows (204 by
elevation; the cable hit 0.0015, the cable nodes 0.0000). The captures'
four kind-0x20 effect nodes at f18 (state 3) are in every AIM capture and
are not the fire path. Whole-run checks added for the side runs: the chain
page's lane strips (the shots' ring decals), DIRECT strips (the parted
strand), streak and kind-2 pages, each re-walked with the original
microcode on its first 12 pages (CHAIN_PAGE.md section 7).

### The AIM side runs' whole records (chain step AIMCAP, 2026-10-02)

Every AIM side run (the holds and the replays) also compares whole records
row for row (tools/test_level_smoke.py `check_aim_records`), from the tick
log's `aimrec` key (EM_LOG_AIM_RECORDS=1, which tools/test_level_smoke_aim.py
sets): the live player record +0x000..+0x31F, the gun node (player +0x20)
and the knife node (+0x18) at +0x00..+0x3F, +0xA0..+0xCF and
+0x1F0..+0x21F (the bytes the nodes model; em_equipment_live_field), the
camera bytes D_008101E4..E7 and the status block D_00810130..+0x5F (the
rows' `ui_rec`; em_status_runtime_ui_block). Rules:

- The player record: every 4-byte word equal, except: the place (+0xA0..
  +0xBF: the image keeps the placement's words; the hip +0xB0, the pose
  host's bone 1 as the camera's view holds it, in the player's frame
  within AIM_EXACT); the heading +0xC4 (its change from the first row; in
  the stick replays within the first row's difference + AIM_EXACT), the
  record's matrix +0xD0..+0x10F and the hand matrix +0x2A0..+0x2CF in the
  player's frame within AIM_EXACT, the aim point +0x2D0 as a point there,
  +0x218 equal or equal relative to the heading; the start words (+0x28 /
  +0x2A, +0x208, +0x248, +0x258, +0x260, +0x264, +0x294, +0x2E0, +0x2F8:
  what the two runs' histories leave before the capture's start) change on
  the capture's rows, to its value or by its step (button replays); the
  melee's sound handle +0x302 is 0xFF in both or a track in both, the
  track not compared: the port's track choice is not deterministic (its
  tracks are freed on the host audio thread's clock, not the game's tick;
  a known port defect, AIM_FIRE.md section 11.4, audit 1b item 1).
  The stick replays (aim_world, aim_cable) leave out the walk's foot and
  contact words (+0x09C, +0x104, +0x238, +0x250, +0x314) and the start
  words, and compare the hand matrix and the aim point on the aim-stance
  rows only.
- The gun and the knife: every modelled byte equal, except the gun's world
  vectors (+0xA0, +0xB0, +0x1F0 points, +0xC0 the barrel's direction) in
  the player's frame within AIM_EXACT (stick replays: stance rows only) and
  the laser's hit: the dot +0x200 by its bearing from the player (button
  replays), its range weight +0x214 and hit flag +0x210 not compared.
- The camera bytes: D_008101E4..E7 equal (the stick replays leave out
  +E7, the eye's collision bits). In the button replays the camera too
  (`check_aim_camera`, check_aim_hold's rules): the eyes in height and
  depth within AIM_EXACT and laterally within the first row's offset +
  AIM_EXACT, the camera block's target within AIM_EXACT, D_008105E0's
  height within AIM_EXACT and its x / z within AIM_TGT_LATERAL.
- The status block: equal on every row, except across aim_burst's page
  module loads (the disc answers at host speed, so the port's page runs,
  with the same pad script, frames the original spent loading): from a
  capture row that waits on a module (phase 3 sub-state 1, or the SPR4
  page's state 3 sub-step 1) it is not compared until the two agree again,
  and after the last load, which Triangle closes before the capture's page
  ran, every byte but the SPR4 state +0x04 and hover +0x11 is compared.

Measured (2026-10-02, the eleven runs): every rule holds on every row; the
largest frame differences are 0.0006 (aim_world's hip and the record's
matrix row 3, the walk); the sound handle is another track than the
capture's on a number of rows that changes from run to run (aim_melee: 17,
222, 66, 66 and 17 rows in five runs on 2026-10-02; AIM_FIRE.md section
11.4);
aim_burst compares the status block exactly on 124
rows, skips 41 across the loads and compares 294 without +0x04 / +0x11.
The page-module waits measured in aim_05 (the original's rows that wait on
a module): SPR4's module 0x2C 28 rows (f50..f77), the SELECTOR's 0x31 18
rows (f82..f99) and the SPR4 reload 23 rows (f154..f176); the port's at
host speed 11, 10 and 11 rows (LAUNCHER_OPTIONS.md, the drive switch).

### The DAMAGE side runs (dmg_flame, dmg_crevice_fall, dmg_pit_fall)

Side phases (DAMAGE.md section 8): dmg_flame and dmg_crevice_fall after
crevice_prompt, dmg_pit_fall after truck_preview. Each plays the capture
lane DAMAGE's own closed-loop policies (decomp route_capture.py
`dmg_beat_*`) as a program of steps (em_level_smoke_test.c "damage":
idle, hits to a health, retreat, the death to the game over, the title,
Up, Cross, the New Game to first control, the walks, the walking jump, the
truck's descent and the step off its roof), driven by the port's own state.
Each beat starts after 35 neutral ticks, the capture's pin. In process: the
steps' predicates and no fault; the run log prints the counters the checker
aligns on ("done ... at tick N counter C", "press X at counter", "title
menu takes input at counter", "first control again at tick T counter C").

The tick log adds `dmg` (health, pending, infection, the pending infection's
bits, +0x00, +0x0F, the protection, +0x235, +0x234, +4..+7, +0x210 of the
flame, +0x0D, the pad block's +0x16 / +0x18 / +0x19 / +0x28, and the burn and
decal nodes) and `pad_pre` (the pad block before the frame).
`tools/level_smoke_damage.py` compares each window, aligned on its event,
tick by tick with the recording (its docstring lists every field): the hits
with the same flinch clip and low-health latch to the hand-back (the
knock-back's per-tick step within 0.01, measured 0.0082; the other fields
equal), the heartbeats, the death
to the load request, the game over aligned on the end of screen module
0x27's load (host speed: 10 ticks against the disc's 23), the title (equal
counters to 001AC070's install; the menu takes input no later than
recorded), the New Game (equal counters from Cross to the game task; first
control the same tick count after it as the run's own boot New Game, at the
recorded place, heading and health), the landing hit and the pit fall with
their height paths. dmg_flame's second New Game is cut from the whole-run
checks (`second_game`; its own check compared it): check_fade_weights
expects 001D19D0 once per New Game, check_render_context accepts the area
build's re-seat (state 0) that a context still bound from the death shows,
the sway check skips the game-over ticks (no world frame), and
check_overlay11 leaves the flame's cooldown and +0x00 = 2 to this check from
the first damage window on.

### The BRANCH side runs (br_ledge_ammo, br_map_item, br_elevator_up, br_panel_decline, br_crate_stack, br_west_ledge, br_yard_ammo, br_cage_key, br_plateau, br_roger_talk)

Side phases (chain step BRANCHES, audit 1b item 16): the decomp capture
lane BRANCH recorded the AREA11 branches the main route skips (decomp
CAPTURES_C10.md "BRANCH": fifteen beats br_00..br_14). Each side phase plays
the lane's own closed-loop policies (route_capture.py `br_beat_*`) as a
program of steps (em_level_smoke_test.c "branches": the walks, the gotos,
the face taps, the takes with their status page and Triangle, the ladders
up and down, the ledge climbs and run-offs, the light melee on a box, the
terminal, the panel's No, Roger's talk), driven by the port's own state,
from the end of the main-line phase (or side phase) the recording starts
from; br_west_ledge plays br_05..br_08 and br_plateau br_11..br_13 in a row,
as the recordings chain. Each beat starts after 35 neutral ticks (the
capture's pin) and prints `beat <name> at tick N counter C`, the checker's
slice. The policies' predicates are the capture tool's (`in_control`: the
selector 0, +1F0 0, the status closed; `settle`: then the record's clip
+0x20C 0), and every pad the program sets reaches the overlay two ticks
later: the recordings' pad took effect three frames after the row it was
set on, the overlay's on the next frame (measured on the ladders' climb
clip: four rows after the first 0x17 row in the recordings and route 10).

The tick log adds `br` (the taken bits of area 11, the counts of items
0x08, 0x10, 0x1B, 0x1E, 0x1F and 0x32, and the records of the items
g0.1..g0.6 and the boxes r3..r6 and drums r14 / r15, an item owner's own
+0x00 / +0x02 / +0x04 / +0x05 / +0x0B laid over its record), and the runs
set EM_LOG_AIM_RECORDS=1 for the player record and the status block.
`tools/level_smoke_branch.py` compares each beat window by window, each
aligned on its event and then row for row with the recording (its
docstring lists every field): the takes (the scan; the take clip after the
turn toward the item; the request, or route 01's camera-settle rule; the
page row for row around its module load, which at host speed is shorter;
the close, within the Triangle's one-row pad timing; the taken bit, the
item counts and the item's record to control), the ladders (grab to
hand-back; a recording whose pad lost the held stick for one row, br_11 at
f1031, is aligned again on the dismount), the ledge climbs and the
step-offs (the climb's Y as the lift, the landing to the row after the
hand-back), the box breaks (the swing, then the four boxes over 200 rows),
the ride up (as check_elevator), the panel's decline (as check_panel to
the prompt, then the list and the cancel script) and Roger's talk (row for
row; the voice read at host speed, and the teardown and everything after it
exactly that many rows earlier, as check_director_beat). Navigation input
is named where it is left out: the stance's +1F1 (the face taps), the turn
clip's side, the MAP page's player marker (UI+0x40 / +0x48), the follow
camera after a release.

Whole-run checks over these runs: a BRANCH run's side phase bounds the
main line's ladder count (`side_start`); after br_elevator_up's window the
terminal is compared with routes 00..03 again (check_indicator_children);
a box break's swing opens the knife's aim-run rules (`aim_from`); the box's
rand() caller is mapped (rand_order.py: 001551B0).

Measured 2026-10-03 (all ten PASS, `make test-level-smoke-branch`): the
page loads take 9 or 10 rows at host speed against the recordings' 24..26;
Roger's voice read 1 row against 7 (the teardown 6 rows earlier).

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

### exit

Route beat 15, re-recorded with whole rows by the EXIT capture lane (decomp
CAPTURES_C10.md "EXIT": `build/c10/exit/exit_00_departure`,
`exit_01_movie_arrival`; FIRST_LEVEL_EXIT.md section 7): the fan crossing,
Roger's departure, the movie, the area change, the AREA01 load and the
arrival, the last phase of the main line (chain C11 EXIT, 2026-10-02).

**Runner** (em_level_smoke_test.c `exit_frame`). From roger's end (route
beat 14's last row, where exit_00 starts), a neutral pad until fan r2
(0x7A7690) leaves phase 2 and enters it again: that tick is exit_00's row
31 (the fan's cycle runs from the area load, whose length is the drive's).
From there it replays exit_00's 50 pad entries (stick only): the
original's state first shows an entry of frame f in its row f + 3, so the
after-frame hook of the tick aligned to row c sets the entry of row c - 2.
At row 344 D_008107D8 must be 0x81 (the crossing). From there no input: the
departure, the movie (the frontend's test hold of START from 2 s, as for
the intro movie: the game sees one frame either way), the area change and
the load. The frames without a world frame (the load, its veil, the
rebuild) are watched from the slot-0 task's end
(`em_level_smoke_test_task_end`); the phase ends on the frame before the
AREA01 rebuild (area 1, slot 0 +9 = 1, +B = 0), and the finish's one more
frame is the rebuild, exit_01's row 306, the first frame of control in
AREA01. The run quits there: every later AREA01 frame faults (level 2).
That last tick has no next tick to carry its post-frame values, so the
smoke asks the scene tick log for a **tail** line (`{"tail": 1, ...}`:
the fade block, the message block, the stream lanes, the ambient loop and
the pool's records), written after the main loop ends.

**Against the capture** (`check_exit`):
- exit_00 f31..f434 and exit_01 f0..f10 (counter 16207: the frame before
  the load), every row, aligned by the main-loop counter: the player's
  position, heading, +5, +1F0, +1F1, clip, +0x2F3 and clock (the clock
  from the walk's clip on, f42: before it the idle clip's phase is the time
  since the area load), the spad bytes, B0..B9, the area bytes, slot 0's
  +8..+B, D_008107D8, fan r2's phase and timer, Roger's +0x00..+0x0F and
  script block (freed: none), his equipment node, the camera eye / target
  and byte, the letterbox, the message block and the fade block; the run
  log's one movie line is selector 1, E001.PSS;
- the load: the distinct states of slot 0, the slot-2 loader (+8..+B) and
  D_00275BD8 equal the capture's 18 in order; a state the capture holds
  one frame holds one tick; the others (the drive's polls, 001FB370's
  upload, the veil's post-load wait) are not longer than the capture's;
  every tick's B0..B9, area bytes, spad, letterbox, camera, message and
  fade equal the capture's row of the same state; and the chain and the
  veil over the exit's ticks replay through the original instructions
  (tools/test_area_load_reference.py replay_chain / replay_veil: 94 chain
  ticks and 001AD010);
- the arrival, f304..f306 aligned on the rebuild: the fields above (Roger,
  the equipment and fan r2 are left to the pool at f306, whose addresses
  AREA01's records reuse; the player's clock is exempt at f306: the port's
  pose attach runs in the rebuild where the original's 0015C420 runs in the
  first stage) and from the tail: the fade block (the fade-in), the message
  block, D_00282157 = 1 (the music's read), D_00282160 = 0x44E (the end
  snapshot's) and the pool: every live record's +0x00..+0x17 equal to the
  capture's f306 (row 0's pool with f306's changes: 78 records) and their
  +0x18..+0x3F / +0xA0..+0xDF.

Measured (2026-10-02): every compared field equal on every row; the load
at host speed takes 94 ticks against the capture's 295 frames (the drive's
polls, 001FB370's 22 dispatches against 26, the veil's 55 frames against
80).

The whole-run checks (render context, effects, shadow, rand order, ...)
cover AREA11's ticks: the checker leaves out the arrival's rebuild tick.
The render context's view check accepts the rebuild's re-seat (001B0460
in state 0, projected by its 001D1EF0), as it accepts state 4's.

### a01_arrival

`tools/level_smoke_area01.py check_arrival`: the run continues from
`exit` with a neutral pad; the rebuild (port counter 15007) is route 15
row 741 and the next 60 ticks are f742..f801, compared row for row with
no exemption except the rebuild's clock (the same one-frame pose attach
difference `exit` documents): spad, screen, message, power, player
position and heading, camera eye and target, the camera flag word
D_008101E4..E7 (+6 = 8 from f742: 0018B9C0's one-shot seat after
001AF690's reset), the camera block's eye +0x10 and target +0x20 and the
forward D_00810600 (since step CAMERA), the request, area and task bytes, slot 0's +8..+C,
the player's +5 / +1F0 / +1F1 / clip / +2F3, ground and clock, the next
tick's fade, and the story bytes D_008107D8 / D_00810758 / D_00810792.
Route 15 records no AREA01 owner, so the owners' records are not compared
here; the AREA01 rand() draws go through `check_rand_order`'s caller
audit (no AREA01 per-call capture exists). The exit's pool witness at the
rebuild is taken by this run too. `EM_A01_ARRIVAL_TICKS=N` (60..600)
lengthens the idle as a diagnostic; frames past f801 are not recorded.
At f801, route 15's last row, the whole camera is compared byte for byte
with the recording's saved RAM (`check_camera_end`: eeMemory.bin, whose
snapshot names the same main-loop counter and the RAM's SHA-256): the
camera block D_008101E0..+0xCF, the forward D_00810600..0F and
D_00810690..D_008106A3.

Measured (2026-10-04): every compared field equal on all 61 rows; since
step CAMERA also the block's eye / target / forward on every row and the
whole camera block at f801.

### a01_00

`python3 tools/test_level_smoke_area01.py --until a01_00` (about 5
minutes with the checks): New Game, the whole first level, a01_arrival, then route beat
a01_00_train_room from its recorded pad (each command submitted two rows
later, the BRANCH rule; the source gap from route 15's last counter is 1,
so row 0 is port counter 15068). `level_smoke_area01.check_route` compares
each of the 781 rows with `compare_route_row`: spad, screen, message,
power, the player's position, heading, +5 / +1F0 / +1F1 / clip / +2F3,
ground and clock, camera eye, target and flag word, the camera block's
eye +0x10 / target +0x20 and the forward D_00810600 (since step CAMERA),
the request, area and task bytes, slot 0's +8..+C, the next tick's fade, health, the progress
windows D_008107D8..+0x3F, D_00810758..+7, the taken bits and the
documents (the tick log composes them byte by byte from their owners), and
all 11 recorded owner records (head, position, +0x1F0 block, +0x2DC timer,
callback). No exemption. Every route phase (check_route, all but a01_07)
ends with `check_camera_end` at its last row: the whole camera block,
forward and D_00810690.. against the beat's saved RAM, byte for byte.
`--verify-harness` checks that these camera checks accept the recordings
(the per-row fields against each beat's last-row RAM in all 15 beats) and
reject a corrupt block eye/target and a corrupt last-row block byte.

Measured (step DRAWN, 2026-10-04): **a01_00 PASS, all 781 rows**, the
whole camera block at f780 included: the near-fire layer's kind-6 program
(LEVEL2_RENDER.md "Kind-6 near-fire program") draws from f405, the pull-up,
the walk off the crates, the fall (f491), the landing (f519) and the walk
to the tunnel mouth follow the recording. Receipts (ignored):
`build/level2/kind6/a01_00d.log`.

Measured before (step MOVE, 2026-10-04): rows f0..f404 equal in every field
(the tool prints "a01_00: NOT PASSED; 405 of 781 recorded rows exact
before the stop"); the run then stops at the chain page's refusal of
D_0023D930. A private diagnostic build that skipped only the kind-6
draw request (deleted, never committed) matched all 781 rows and the
checker passed a01_00 together with the first level's phases and
a01_arrival. Receipts (ignored): `build/level2/move/receipts/`.

Step CAMERA (2026-10-04), with the camera block checks above: the release
build again stops at f405 with rows f0..f404 equal (now including the
block eye / target / forward). The same kind-6-skipping diagnostic build
(deleted, never committed) passed a01_arrival and a01_00 through the
extended checker, the whole camera block at a01_00's last row included.
It then ran a01_01: rows f0..f2 equal, f3 differs (the player's clip turns
1 two rows before the recording's), because the recording's first pad
command, set right after its source state was loaded, reached the
original's player two frames later than every mid-beat command does. With
only that first command submitted two rows later, all 306 a01_01 rows and
its last-row camera block matched; with the whole a01_01 script delayed,
rows f0..f24 matched. a01_02's rows f0..f38 matched; at f39 the run stops
in the floor service 00175900 (player closure 001612D0's fault), the
frame on which the census first records 00187DE0, the surface-0x5B first
contact that em_player_first_contact does not bind yet.
Since the level-2 check (2026-10-08) the exported pad applies that reading
per beat ("a01_01..a01_07 and the side beats" below). Receipts (ignored):
`build/level2/camera/`.

### a01_01..a01_07 and the side beats (level-2 check, 2026-10-08)

**The first command's latency is read from each recording.** Every beat
was recorded from its source's loaded save state, the recorder setting the
beat's frame-0 pad command right after the load. A mid-beat command shows
in the player three rows after it (the driver's two-row submission plus
the tick); the first one sometimes took longer. `level_smoke_area01.
first_command_extra_rows` reads it from the recording itself: the first
row whose player state (+5, +1F0, clip) leaves row 0's idle, minus three
(a01_01 row 5: 2; a01_02..a01_07, a01_s1, a01_s2, a01_s7 row 3: 0; the
arrival beats a01_00, a01_s0, a01_s3, a01_s4, a01_s6 row 6: 3, where the
arrival's own input lock decides that row natively whatever the command's
row is; a01_s5 row 7: 4). The exported pad file places the frame-0
command that many rows later (`command_rows`; the manifest records it).
This is a reading of the recorder, not a property of the game; every
later row is compared strictly. `--verify-harness` checks the driver
against the shifted commands.

Measured (receipts in ignored `build/l2check/smoke2`, `smoke3`, `smoke4`;
`EM_PS2_DISC_DRIVE_TIMING=1` where stated):
- **a01_00 PASS (781 rows), a01_01 PASS (306 rows), a01_02 PASS (591
  rows)**, each with its ending camera block: the tunnel, the water at
  the shaft (00187DE0's first contact, its ripple and splash), the lower
  tunnel and the shaft-landing stairs (AREA01's one surface-0x35
  polygon).
- **a01_03: rows f0..f956 of 991 exact** with the drive-timing switch (the
  locked shaft door's Use, its door program and message, the conversation
  script 0x8298E0); at f957 the voice line ends one frame before the
  recording's (seven frames at host speed). The later rows of that beat
  and the next beats inherit the shift: a diagnostic that tolerates only
  the message words (private, not committed; not a checker) finds a01_04's
  camera and walk drifting from row 0, a01_05's 3,835 conversation rows
  exact up to its own voice-line end, and a01_06's player path equal with
  its overlay owner 0x826CF0's record differing.
- a01_07: the replay plays the door's opening and the area change; the
  load of AREA00 (level 3) then stops at its completion, where the port's
  own AREA00 assets (world texture catalog, shadow receivers) do not exist
  yet (`em_scene: 001FF080(1, 0): area 00 room 0 is not exported`). The
  module pack holds AREA00 sub 0's files since this check, so the stop is
  no longer a missing disc read.
- **a01_s0 PASS (1,435 rows and the whole ending camera block)** with the
  drive-timing switch, since the camera seed copies its fourth lane (the
  player's +0xCC through 0x70003B5C, 0018CBD0's quadword copy).
- a01_s3 PASS (262 rows), a01_s4 PASS (1,225 rows, drive timing), a01_s6
  PASS (229 rows).
- a01_s1 rows f0..f313 (the DATA BASE pickup's page request is consumed
  earlier: its page module answers at host speed even with the switch,
  which has no recorded time for AREA01's page reads); a01_s2 rows
  f0..f257 of the control-room pickups (the same); a01_s5 rows f0..f1622
  of the duct (the crawl, then the pickup's page request, the same); each
  after a01_s0's full pass.
- a01_s7 inherits a01_03's voice-line row and stops there.

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
00187EE0's 001EFD90 spawns run the live effect binder, L26; its nodes are
compared at the effect beats only).

### The script owners' takeover (`check_stage_takeover`, chain C7)

A script owner's frame (the truck trigger in route 07, the fence door in 09,
the director in 10 / 11 / 13, Roger in 14) is the player stage's own
takeover, as in the original (PLAYER_STAGE_WORKERS.md section 2.1):
0015B130's prelude admits the player (+4 = 4), 0015BA50's +4 = 4 path and
0015B530 run each stage, and 0015B530's 00182DF0 releases it. The tick log's
`player` list carries the record's +4 as its eighth value (the route rows do
not sample +4). Inside each of these phases' windows the checker requires:
the first tick with +4 = 4 is the admission (+5 = 0, +1F0 = 0x41, 3B8F and
the selector nonzero); every tick after it holds +4 = 4 until the release
tick, where 00182DF0 has left +4 = 1, +5 = 0, +1F0 = 0, 3B8F = 0 with the
selector 0. The phases' own row-for-row comparisons (+5, +1F0, +1F1, the
clip, the clock, +0x2F3, 3B8F in the spad bytes) cover the values the route
rows sample. Measured on the full route: route 07 from port tick 4444 to
4806, 09 (its own run) 5345 to 5507, 10 6143 to 8560, 11 9462 to 9949, 13
10920 to 11131, 14 11485 to 12954; the admission rows are the captures' first
3B8F = 1 rows and the release rows their first rows with 3B8F = 0 again (6
rows early in 11 and 13 before the drive model of 2026-09-27, whose whole
frame follows the voiced line's teardown; on the capture's rows since with
the PS2 disc-drive timing switch on; at host speed, the default, they lie
exactly on the teardown's clock, 6 rows early). The
tick log is otherwise equal to the stand-in's (the interaction runtime's
takeover before C7) on every tick.

### The render context (`check_render_context`, census L32 / L30)

Not a phase: after the phases, over every tick from first control to the
end of the run (the full route, or side beat 00 in its own run). The tick
log's `rctx` carries, at each tick's end, the render context's flag words
+0x0C / +0x174, the fog block +0xA0..+0xFF, the zoom +0x2468, V +0x2380, K
+0x23C0, the 001CD370(0) projection +0x2240, the eased pairs
+0x24F0..+0x2513, the +0x2450 block, D_00275690 / D_00275694, the camera
pool's D_00810610 (docs/RENDER_CONTEXT.md section 8), and since chain C7's
step V (RENDER_CONTEXT.md section 9) the list cursor +0x08, +0x98 / +0x9C
and the first 0x70 bytes of the other slot's main list (the one the
previous iteration's step V 001D2300 built). It checks:
- every gameplay tick (001AE5E0 ran, 3B8D = 0, D_008101E4 != 3) holds the
  route snapshots' values, which all 15 snapshots share: flags 0x43 / 3, the
  whole fog block with presets and latches, D_00275690 / 94 at their fixed
  point, the widths 24 / 40 / 56 / 72 with +0x2510 = 0, and the +0x245C..
  +0x2467 tail 001DDE10's player path leaves;
- every tick whose frame head ran (V changed) projected the D_00810610 of the
  END of the previous tick: the original's one-frame view lag (+0x2380's
  only writer is 001D2960, called only by 001D1C50, which runs before the
  camera stage; the snapshots of the beats whose camera moved hold
  +0x2380 != D_00810610);
- every tick after the first status screen's close (+B = 5) holds routes
  01..14's fog record save slot 0 +0x120..+0x13F, which 0020DFA0's
  0021BAC0(0) wrote at that screen's CONFIGURE (the status UI step; route
  00's snapshot, taken before any status screen, holds an older save; a
  build without the save fails at the close tick);
- on every 200th gameplay tick (at most 40), the ORIGINAL 001D2960,
  executed over the 05_boxes snapshot with the tick's V and zoom
  (test_frame_render_heads_reference's interpreter, its sqrtf the original
  0011E748), writes the logged K and +0x2240 bit for bit;
- on every tick, step W's field +0x98 = 1 - +0x9C (step B's slot): the
  phase every capture holds (the port's field model);
- every world gameplay tick after a world frame holds the route snapshots'
  main list for the other slot (seven tags: the draw environment, the Z-only
  clear +0x3A0, the CALL of the +0x1D8 channel-3 list, channel 0, the page,
  channel 1, the end; the CALL is there since the static-world step binds
  001C1D00) and the cursor at its end;
  the tags' written bytes are compared (the count halfword, the id byte, the
  address), not byte +2 and the upper eight, which keep the arena's earlier
  contents;
- every status frame (+B = 3 with its 001D2830(3, 1)) after a status frame
  holds the two status captures' (startup-reference status-hub and panel,
  taken mid-iteration after that call) flag words 0x0B / 0x03 (flag 3 set,
  flag 6 clear), fog block, save slot and main list's six tags (the black clear +0x420
  that flag 3 selects, channel 1 before channel 0 as D_008106C4 != 0
  orders), and, once the previous tick's D_00810610 is the UI view, a frame
  head that projected it: the captures' +0x2380 is 0020DFA0's UI view
  (identity with -1 at +0x14), which the status pages now write into the
  camera pool's D_00810610.

The one exception: 0x1AE040 state 4 (the room move) re-seats the camera
with 0018D7B0 / 0018C0D0, which builds D_00810610, and falls into state 1 in
the same tick, so that tick's frame head projects the re-seated view (the
check counts it separately).

The check reads D_008101E4 as the camera block's +0x04 (the live camera's
one storage of it); before census L18 the tick log and the render context
read a scene-state copy that nothing but state 4 wrote.

Measured (full route): 5,378 gameplay ticks, 6,888 frame heads (6,836 with
the camera moving that frame), 27 sampled ticks; 11,634 field ticks, 5,368
world lists, 412 status frames (all 412 on the UI view). Side beat 00: 128,
124 and 1 (284 field ticks, 127 world lists, no status screen). Side beat 09
(its run): 2,370 gameplay ticks, 2,713 frame heads, one state-4 re-seat,
4,197 field ticks, 2,363 world lists, 412 status frames.

### The AREA01 world (`check_area01`, `check_shadow_area01`; step DRAWN)

The whole-run checks below cover the first game's AREA11 ticks. A run that
reaches AREA01 (a01_arrival and the AREA01 route phases) also checks, over
the ticks after the exit's rebuild:
- `level_smoke_static_world.check_area01`: every AREA01 001C1D00 call drawn
  in its tick; the sampled calls (one in 400; quick: the first; full: all)
  re-executed by the ORIGINAL 001C1D00 with its dynamic table pass
  001D5BD0 over route 15's AREA01 capture with the port's inputs: the
  capture's dynamic table must be the port's (address, extent, FNV-1a),
  every byte the original writes must be in the port's logged output and
  equal to it, then the channel-0 triangles through the original
  microcode. The AREA01 sample carries the dynamic table's identity, the
  chain table and D_00250F30.. before the call and the call's whole
  output (EmRclStaticSample.dyn; LEVEL2_RENDER.md "AREA01 world in the
  level smoke and its pixels").
- `level_smoke_shadow.check_shadow_area01`: the shadow samples restart
  when the AREA01 composition binds at the rebuild; 0015C160's routes and flushes over the AREA01 ticks, and the
  sampled player shadows, decals and owner-walk actor shadows replayed with
  the original over route 15's AREA01 capture.
- `level_smoke_chain_page.check_kind6_area01`: the tick log's page record
  carries the kind-6 program's MSCALs and primitives (D_0023D930, 001CFBE0
  kind 6, asked for by 001E3D90's near-fire layer); its primitives appear
  only on pages that ran its MSCAL, and a run that played a01_00 must have
  at least one such page (the program itself is compared with the original
  microcode by `make test-level2-kind6-vu-reference`).
Measured on the a01_00 run (quick / full): static world 842 calls, 1 / 2
samples equal; shadows 822 player calls, 4 / 9 sampled plans and 4 / 9
actor plans equal; kind 6 (integration, 2026-10-07): 374 pages, 99,000
primitives, the first at a01_00 row f405 (the row where the release build
stopped before step DRAWN). Receipts (ignored):
`build/integrate_draw/a01_00_full.log` (EM_TEST_FULL=1, 2026-10-07) and
`build/integrate_draw/a01_00_quick.log` (with the kind-6 check). The a01_arrival run (`EM_LEVEL_SMOKE_UNTIL=a01_arrival
make test-level-smoke`): 61 static-world calls, 61 player shadows, one
sampled plan equal. A run that ends at a01_arrival has no static-world
sample in AREA01 (one in 400 calls) and says so.

The AREA01 pixels are compared by the fb2 harness (`make
test-fb2-pixels-area01`; "Frame captures" above and GS_EXACT.md section 10).

### The static world (`check_static_world`; tools/level_smoke_static_world.py)

Not a phase: after the phases, over the whole run (STATIC_WORLD.md section
8). The tick log's `static` carries the static world's live draw (the last
run drawn: its start and end, the FNV-1a of its bytes and of its triangles,
its level and clip batches, triangles and culled vertices) and the number of
001C1D00 calls; `static_sample` carries every 400th call's inputs (the
render context, the scratchpad 0x70003A40..0x70003B3F, D_00810610,
D_00810700..702, D_008101D0..DF, D_00253560, D_00817240 and the skin records
before the call) and output (the channel-0 run, the channel-3 list,
D_00253560 after). It checks:
- every tick that ran 001C1D00 drew its run in the same tick (the draw
  count follows the call count one for one) and every drawn run drew
  triangles;
- the sampled calls (default: the first two; `EM_TEST_FULL=1`: all): the
  ORIGINAL 001C1D00, executed over the 05_boxes snapshot with the port's
  inputs laid over it (test_static_world_reference's interpreter), writes
  the port's channel-0 run (every byte it writes; a tag's +2 and +8..+15 are
  never written and keep the arena's earlier bytes) with the cursor after
  it, the port's channel-3 list and D_00253560..EF; the run, replayed with
  the ORIGINAL VU1 microcode of the level and clip kernels
  (test_static_world_draw_reference's walk), draws exactly the triangles the
  port drew (their FNV-1a and count);
- the aligned route snapshots whose frame view (+0x2380) and zoom equal the
  port tick's: the port's run has the capture's own run's length and draws
  the triangles the capture's own run draws through the original
  microcode. A main-line run that checked `roger` must have compared 10
  and 14 (`VIEW_EXACT_MAIN_LINE`); a side-beat run may compare none.

Measured (full route to Roger): 12,572 runs, 25,651,789 triangles; 32
sampled calls re-executed; view-exact at snapshots 10 (2,457 triangles) and
14 (847). Side beat 00: 1,649 runs, 5 samples; status_pages: 3,793 runs,
10 samples; fence_door_side1: 5,704 runs, 15 samples.

### The indicator children (`check_indicator_children`, CENSUS_UNVERIFIED.md)

Not a phase: after the phases, over every tick from first control on. The
tick log's `children` lists every indicator child (001C5680 / 001C5760)
whose bind ran (em_indicator_bind_live): its record address, +0x10, +0x04,
+0x09, +0x0C, +0x0D, +0x44, +0x4C, the slot words and the first slot's
+0x90 matrix. It checks:
- each child's record fields equal the route snapshots' child at the same
  record address (the snapshots agree), the model handles included;
- its first slot's matrix (001C6380's placement) equals theirs bit for bit;
- the terminal's 001C5760 child: 00827B10 copies its own node 0 matrix into
  the child's slot on every phase-1 call (0x827E6C), and the child's own
  state-0 placement equals the terminal's node in every capture, so on
  every tick the child's slot equals the terminal's node 0 of the tick log's
  `terminal` record (its address, +0x04, +0x09, +0x0C, +0x44, +0x4C, node 0's
  +0x90 and +0xB0) bit for bit; that record equals routes 00..03' before the
  elevator phase's window (state['ride_scan']) and routes 04..14' after it
  (state['ride_end']; the elevator phase compares the window's rows);
- the slot words are nonzero exactly up to +0x09 (their addresses are not
  compared: the stack's history is not yet the original's);
- at the aligned snapshot ticks (the effects' `snapshots`) the set of bound
  children equals the snapshot's, and the terminal record equals the
  snapshot's.

Measured (full route): 9 children over 13,000 ticks (the five lights, the
battery's light until its take, the security gun's 0x7A lamp, the panel's child
until the power, the terminal's: the terminal's node on all 13,000 ticks,
the record equal to routes 00..03 on 3,062 ticks before the ride and to
routes 04..14 on 9,550 after it) and the same set and terminal as the
snapshot at all 6 aligned snapshots (scratch mutations of the log: one bit
of the child's slot, of the terminal's node after the ride, or of its +0xB4
in the carry each fail the check); the save slot of check_render_context
over 11,599 ticks. Side beat 00 (no status screen in its run): 1,650 ticks;
side beat 09: 5,563 ticks, 1 snapshot.

### The indicator units (`check_indicator_units`, OWNER_DRAW.md section 11)

Not a phase: after the phases, over every tick from first control on. The
tick log's `page_units` lists the frame's 001CABA0 calls (em_owner_draw_live:
the record, its +0x80 words, the channel-3 bytes, the clip pass and the
digests of the unit's colour matrix B, lighting rows and position rows). It
checks:
- on every world-frame tick (001AE5E0 / 001AE6B0 in the tick's trace; a
  status frame draws none), every bound child that starts the tick in its
  draw state (+0x04 == 1 in the tick before's `children`) and keeps it made
  exactly one call, and no other record did;
- at every aligned route snapshot, for every child of the snapshot (+0x4C
  001CACB0, +0x44 set, +0x04 == 1): the set equals the port's calls, and the
  ORIGINAL 001CACB0 runs over the snapshot's RAM with the port's +0x80 words
  (001F54E0's rand() pulse), the port's point-light pool and the view
  D_00810610 the draws read: its channel-3 byte count (0: culled by
  001CA7B0, on both sides) and clip pass equal the port's, so do the colour
  matrix B and the lighting rows, and in the camera-exact beats 10 and 14
  the position rows; the security gun's lamp 0x7A is counted.

### The area title (`check_area_title`, STATUS_UI_LEFTOVERS.md 2.6)

Not a phase. The tick log's `title_nodes` lists the 001C5930 nodes ([record,
+0x04, +0x05, +0x06, +0x28, +0x2A, +0x1F0, +0x1F4]) and the cumulative count
of their 001CC1E0 lines. It checks: at most one node; in every tick whose
title phase 0 stepped its timer the lines grow by exactly what the
scratchpad mode byte 0x70003B8D at the tick's end says (the tick log's
spad selector): by 1 when it is not 1..3, by 0 when it is (the opening's
2); a tick whose byte changes follows its end value too, and those ticks
and the node's first state-1 calls are printed (on the main route the
first call ends with the byte 0 and draws one line, the opening stores 2
the tick after); by none once the timer ran out (band 0); at every aligned
snapshot the node equals the snapshot's at the same record; at the port tick
the fence_door phase aligned with route 09's last row (its snapshot) the
room move's fresh node holds the snapshot's fields (240 ticks left), at
another record: 001AFA90 hands the port a different free record there
(0x7AEF00 against the original's 0x7B0390; the pool's free list is not the
original's history at that point).

### The background (`check_background`, BACKGROUND.md)

Not a phase. The tick log's `background` holds the frame's channel-3 walk
(em_background_live): drawn, the list's start, its triangles, their vertex
digest and the dmem upload the grid program read. It checks: every drawing
frame draws 1,922 triangles; on sampled ticks (the first draw, every 200th,
every aligned snapshot tick) the ORIGINAL grid program (decoded from the ELF
and executed by tools/test_background_reference.py) over that upload kicks
exactly the drawn triangles and the upload's template and constants are the
ELF's; at the camera-exact snapshots 10 and 14 the original program over the
capture's own channel-3 list kicks them too.

### The security gun, its cable and the fan pair (`check_gun_fan`, census L24)

Not a phase: after check_indicator_children, over every tick of the run.
The tick log's `gun_fan` carries the security gun 00825940, its cable
00827490 and both fan records 00827630 (em_area11_bindings_gun_fan_log:
record address, +0x10, +0x00, +0x04, +0x05, +0x09, +0x28, +0x34, +0x36,
+0x38, +0xC0..+0xCC, the gun's bone 3 +0x78, +0x220 and its lamp's +0xA0).
It checks against every route snapshot 00..14 (SECURITY_GUN.md 5.4):
- the gun and the cable are the same in all 15 snapshots, and from their
  first call on the port's records at the same addresses equal them field
  for field on every tick (the gun dormant in 0x64 with +0x28 = 584 from
  its one lifecycle-0 rand() draw, bone 3 +0x78 = -1.1344, the lamp at
  +0x220 dark; the cable in lifecycle 1, +0x34 = 1, not hit); before their
  first call both are in lifecycle 0;
- every snapshot's fan state (+0x04, +0x05, +0x28, +0x38, +0xC8, both
  records) is one the port's fans run through on some tick (the fans' phase
  at a snapshot follows the recording's timing, so it is not compared at
  the aligned tick; check_owner_units draws them over the port's +0xC8);
- the aligned snapshot ticks hold the same records.

Measured (full route, 2026-09-28): the gun and cable equal on 13,018 ticks
after the gun's setup (1 before it); the 30 captured fan states among the
1,091 distinct states the port's fans ran; 6 aligned snapshots.

### The AREA11 overlay owners (`check_overlay11`, chain step A11FIX; tools/level_smoke_overlay11.py)

The tick log's `overlay11` rows (the flame 008235F0, the flag-0x30 manager
00823CE0 and the opening controller 00823E80: +0x00, +0x04, +0x05, +0x2E,
+0x30, +0x34, the flame's +0x1F0 block and +0x210, and whether the record is
on the published class-0xD list) and `sfx413` (the tracks whose requested id
is 0x413) are compared with the route snapshots 00..14:

- from its first call on, on every tick, the flame's record equals every
  snapshot's (+0x04 1, +0x00 1, +0x30 its own +0x1F0, +0x34 0x823580, the
  half extents 7 / 15 / 7) and its contact cooldown is 0 (no route touches
  it); the flag-0x30 manager's equals every snapshot's (lifecycle 1,
  waiting); after the opening the controller's equals every snapshot's
  (+0x05 2, +0x2E 0xFFFF);
- at the camera-exact aligned snapshots 10 and 14 the flame is on the
  published class-0xD list exactly as the capture's list block holds it (on
  at 10, off at 14), so the contact pass 001A8BE0 -> 001A8660 runs live; the
  other aligned snapshots depend on the view cone and are only counted;
- no track requests the flame's loop 0x413 before first control; when the
  run reaches crevice_prompt, some tick requests it (the decomp's audio
  capture holds it in every frame of route 11).

### The effects (`check_effects`, census L26 / L27 / L28 / L39)

Not a phase: after the phases, when a phase check aligned a port tick with
the last row of a route snapshot (truck_crossing with 08, cage_roof /
crevice_prompt / east_tower with 10 / 11 / 13, crevice_jump with 12 (its
entry tick plus the rows from route 12's entry row to its end), roger with
14). The tick
log's `effects` carries the effect binder's counters, its pool nodes, the
equipment nodes and the last barrel's lane-packet and glow-marker digests
(em_effects_live / em_equipment_live; docs/EFFECT_MANAGER.md section 8).
It checks:
- over the whole run: every barrel frame (001F0360) emitted the 11 glow
  markers and drew the six ring lanes, and no counted effect gap (the
  packet-only handler 001EC270; the skid's 001EAD70 runs its translation
  since chain step BRANCHES; EFFECT_MANAGER.md 8.2) was reached;
- at each aligned tick, against the snapshot's pool list (D_00275BC0): the
  equipment nodes' +0x00..+0x0F, +0x44 and +0x4C as a set, each drawn this
  tick at the player's node its mesh draws it at; the player's links to
  them, +0x18 (the knife, 0015C420) and +0x20 (the gun, 0015C310), as
  original record addresses (the tick log's `links`; since chain step
  AIM, AIM_FIRE.md section 4); the head sprites'
  lifecycle, key, owner, bone and offset (the sub-state +0x05, wait, ramp
  and scalar follow the draws: check_head_sprites); the effect nodes' state, subtype, step, limit and
  accumulator (route 08: with the truck puffs' +0xB0 and +0x100 rows, bit
  for bit; route 12: the player's four footstep puffs);
- the barrel's 001F0720 packets against the original's own in the
  snapshot's DMA buffer (the chain before the context's +0x18 cursor):
  packets 1..3 of all six lanes (lane 3's parameter quadwords excepted:
  identical in every capture from opening_ee.bin to route 14, so no routine
  of the level writes them; their earlier writer is open,
  EFFECT_MANAGER.md 8.4), and packet 4 and
  the visible glow markers' primitives (their rand() colour masked here;
  check_marker_colour compares it) where
  the port's camera equals the capture's (10 and 14).

Measured (full route): 12,573 barrel frames, 12,841 effect chains, none
skipped; 08: 7 equipment nodes, 2 head sprites, 8 truck puffs; 10: packet
4 and 5 glow-marker primitives; 14: packet 4.

### The owner units (`check_owner_units`, OWNER_DRAW.md sections 9 and 10)

Not a phase: at the same aligned snapshot ticks as check_effects. The tick
log's `owner_units` carries, per 001CAA00 call of the last drawn frame (the
crates, drums, truck and fence door, the terminal, the panel, the prop
001C4820, the items 00219550 / 0015AFA0, the canopy 00823E80, since census
L24 the security gun 00825940, its cable 00827490 and the fan pair
00827630, since chain C8b's FACE step Roger 008237E0 and his equipment
node 001C5C90, the seven player equipment nodes and the player on
em_owner_draw_live), the owner's record address, the unit's
byte count (0: culled), the clip pass, digests of the colour matrix B, the
lighting rows, the position rows, the point-light slots and the lighting
rows' lanes y and z, the point 001CAA00 culled and lit the owner at (three
float bit patterns), a digest of its nodes' +0x90 matrices, and 001CB3C0's
face unit (its bytes and digest; the unit bytes above are the owner's
unit alone). Roger calls his +0x4C only with his +0x01 set, his equipment
only once Roger holds his nodes and has +0x01 set. For every
such owner the ORIGINAL 001CAA00 runs over the snapshot (the owner-draw
oracle; the player after the walk, as 0015C160 calls its +0x4C; an item
only where the snapshot's +0x01 is set: the items call their +0x4C only
when their 001B17A0 found them visible). The
equipment nodes are matched by their flavour +0x03 and variant +0x0D (the
port's pool places the respawned flavour-2 nodes at other records;
check_effects compares their bytes), the other owners by their record.
Then:
- wherever both drew: B and the lighting rows' lanes y and z (the room
  rig's slots 1 and 2) are equal; for the player and the equipment (the
  movers) only where the port's point equals the snapshot's (both depend
  on it through the point-light fold), and a mover at the snapshot's point
  must also hold the snapshot's pose (its nodes' +0x90 digest);
- the whole lighting rows over the port's own point-light pool: the
  slots' sway follows the draws of 001D7C30, which differ from the
  capture's, so the ORIGINAL 001CAA00 runs a second time over the snapshot
  with the port's pool (the tick log's `lights`: the pool at context
  +0x210..+0x221F after the frame's 001D7C30, rebuilt byte for byte; its
  slot digest must equal the one the port drew with) and the port's view
  D_00810610 as the draws read it (`view610` of the tick before: the camera
  stage commits the next view after the draws). Its rows must equal the
  port's for every owner compared above, including the player and the
  equipment in 10 and 14. The check reports how many of the tick's 16 view
  words differ from the snapshot's (0 in 10 and 14; check_sway proves the
  slots follow 001D7C30 over the port's own draws: on 40 sampled ticks the
  ORIGINAL 001D7C30 over the port's previous pool and draws writes the
  port's pool; the one exemption is the status screen's entry, see
  check_sway below);
- in the camera-exact snapshots (10, 14): the owners that ran, and every
  owner's byte count, clip pass and position rows (node x VP). There every
  mover's point AND pose must equal the snapshot's (the port's player
  stands where the capture's does), so the player's and the equipment's
  units are compared in full, never skipped: a wrong owner view, node
  record or +0x110 mapping in em_player_draw_live / em_equipment_live
  fails the smoke;
- a run with a camera-exact snapshot must have compared the player and all
  seven equipment nodes in full (with a unit) in at least one of them;
- Roger and his equipment (since chain C8b's FACE step) are movers too:
  their clip phase follows the time since the area load, so they are
  compared in full only where the port's point AND pose equal the
  snapshot's, and counted otherwise, also in the camera-exact snapshots;
  wherever both ran 001CAA00, 001CB3C0's face unit has the original's length
  (full route, 2026-09-28: every aligned snapshot 08, 10..14; Roger's two
  owners at the snapshot's pose in 14, so compared in full there);
- the fan pair (since census L24): its +0xC8 follows its spin cycle, whose
  phase at a snapshot follows the recording's timing (check_gun_fan), so
  where the port's +0xC8 at the aligned tick differs from the snapshot's,
  the ORIGINAL 001C6380 first places the fan at the port's +0xC8 over the
  snapshot (+0xC0 / +0xC4 must equal it) and both original draws run over
  that pose (full route, 2026-09-28: 12 fan units drawn so; 10: 26 owners
  and 14: 24 owners compared in full, the gun, the cable and the fans
  included).

The player draw gate (`check_player_draw_gate`, every tick whose post-step
0015C160 ran in the logged frame): em_scene_bindings_player_record_drawn()
is read by the equipment nodes' +0x4C in the walk and again by the
post-step, so the two reads must agree: a reported post-step (route -1)
builds no player and no equipment unit, a live post-step that reaches its
+0x4C builds the player's unit, and a player unit comes only from such a
post-step. Ticks without a post-step in their frame (the status screen's,
where the scene does not step and the log holds the last flushed frame's
units) are counted, not judged. Full route: 11,271 live post-steps, 1,302
reported (the opening, with the seven equipment nodes listed in every one),
428 ticks not judged.

Measured (full route, 2026-09-26, with the terminal, the panel, the prop,
the items and the canopy on the path): 08: 14 units drawn in both, B and the
rig lanes compared for the 6 placed world owners (the player stands
elsewhere at that tick); 10: 12 drawn, all compared, camera exact with all
22 owners (the door, the truck, the two visible items, the player and its
seven equipment nodes drawn; the other world owners culled as in the
capture) equal in bytes, clip and position rows; 11: 10 (2 compared); 12: 9
(1); 13: 22 (14, the world owners); 14: 11, all compared, camera exact with
all 20 owners equal. In 10
and 14 the player and all seven equipment nodes stand at the snapshot's
point with its pose. A scratch mutation of the tick log (the player's pose,
point or rows, and one equipment node's pose, in 10 and 14; an equipment
unit in a reported tick; a live post-step without the player's unit) fails
each of these assertions.

### The room lights and the fade weights (`check_room_lights`, `check_fade_weights`; audit 1b item 4)

Run whenever first control passes (every run that reaches it).

- **check_room_lights.** The area entry's 001D19E0 -> 001D7BB0 resets the
  render context's point-light pool and registers the room lists at run
  time (001F68B0 / 001F6E40 -> 001F6640 -> 001D7FA0,
  em_effects_live_room_lights; AREA11_POINT_LIGHT.md). At first control
  (against the first-control capture `playable_ee.bin`) and at every aligned
  route snapshot (beats 00..14), the port's pool from the tick log's
  `lights` holds the capture's id counter (+0x210) and staged count
  (+0x214), the same active slots (weight +0x2C > 0), and in each active
  slot the capture's multiplier, adder, type, handle, position and colour
  words. The angles and the flicker matrices follow the port's rand()
  stream (check_sway below runs the original over them).
- **check_fade_weights.** The New Game's 001AD1A0 runs 001D19D0 -> 001D9070
  once over the global library's model 0x16 (the gun lamp's third cone
  shell), which the port holds as the disc has it (the Roger export's
  writable region). The run log's line `fade weights: ... fnv1a` gives the
  FNV-1a of the model's bytes after the call; it must equal the
  first-control capture's and every AREA11 route snapshot's at the same
  address (in AREA11 nothing draws the cone: D_008106C8 has 0x20000000).

### The rand() order (`check_rand_order`, `check_sway`, `check_marker_colour`, `check_head_sprites`; docs/RAND_ORDER.md)

Not phases: after the phases, over the run's `EM_RAND_TRACE`
(`build/level_smoke/rand.trace`; each side run of `make
test-level-smoke-side` writes and checks its own,
`build/level_smoke_side/rand.trace`, so these checks also cover the side
beats' ticks), which tools/rand_order.py resolves to
(original caller, state) per call. The tick log's `counter` (the main-loop
counter at the tick's start) ties the trace's lines to the ticks. An
unknown rand() caller fails the check.
- **check_rand_order.** The opening against the decomp's C7 newgame
  capture, aligned on the area entry (0x1AE040 state 0's 001FAE70(1), the
  first draw from the unseeded state 1):
  - the area-entry frame equal, and every call equal in caller and state
    up to the opening's actors' spawn, which must be the first difference:
    the script's op14 runs when the stream request's wait ends, which the
    drive makes shorter than the original's (at host speed by the capture's
    wait; the switch, by the model's), so in that frame the port's opening
    body draws its face's first values where the still-waiting original
    draws its glow markers, from the same state. The spawn frame plus the
    opening's end difference must be the original's own spawn frame. The
    security gun's AE+1 draw (census L24), Roger's owner's face at AE+2 and
    the player's face in the player stage after the barrel from AE+5 are
    among the equal calls (the chain's OPENING step);
  - from the spawn, every frame's callers equal the original's frame the
    same shift later, caller for caller, until the first difference, which
    must involve a value-driven caller (a timer drawn from a state the wait
    moved);
  - every frame's deterministic callers (the sway, the indicators, the
    glow markers, the music, the item, effect-owner and security-gun first
    ticks) equal
    frame for frame to the port's first control, and the 30 frames after
    it;
  - the opening's end by the run's drive mode ("The stream drive's two
    modes"): at host speed first control exactly the capture's drive wait
    (21 frames) earlier, with the stream request read at host speed and
    its hold equal to the capture's; with the switch on, at most 16 frames
    earlier (the area music's read: disc timing).

  Then every frame of the phase windows aligned row for row with a stretch
  the capture holds (route 01's battery take, route 10's director beat) has
  the capture's deterministic callers. The value-driven callers' totals and
  the faces' stage positions are printed.
- **check_sway.** On sampled ticks (the first, every 250th, at most 40, and
  the snapshot ticks) the ORIGINAL 001D7C30 over the port's pool of the
  tick before and the port's own two draws of the frame writes exactly the
  port's pool. A sampled tick where the original would draw and the port
  made no draw fails, with one exemption tied to the tick log: the status
  screen's entry (`status_entry_tick`). There the frame machine's task is at
  +B = 3 with +C 0, then 1 (the first ticks of the status run, as status_04
  shows around the open), the tick's trace holds no world frame (neither
  001AE5E0 nor 001AE6B0, so no 001D1C50 -> 001D7C30), and the tick lies
  within the original's entry gap: route 01's rand() capture has 001D7C30
  draws in every frame but f190..f191, the two frames after the battery
  take's request posts at f189 (`sway_entry_gap` derives the 2 from the
  capture and checks the request). Such a tick must leave the pool as it
  was; it is replaced by the next drawing tick, which must be exactly the
  entry's first tick + 2 (the port skips as many frames as the original).
  Chain step H7's longer New Game load first put such a tick among the
  samples, with the PS2 disc-drive timing switch (tick 3001, the battery
  take's entry).
- **check_marker_colour.** On sampled barrel frames each of the eleven
  001F4D40 calls drew its value of the trace, and the ORIGINAL 001F4D40
  over its colour words and that value hands 001CD520 the port's rgb (the
  tick log's `markers`). In the camera-exact snapshots the capture's own
  draws (the frame's last eleven calls, stepped back through the LCG from
  the snapshot's state word) explain each captured marker's colour with
  the port's depth fade.
- **check_head_sprites.** Every tick's head-sprite sub-state, wait, ramp and
  scalar follow 001E2560's transitions over the port's 001E2560 draws in
  pool order, and every draw is used.

Measured (2026-09-28, chain C8b OPENING): 227 calls equal up to the
actors' spawn at AE+10 (the original's AE+31; 447 calls up to AE+20 with
the switch on, 11 frames before), then 32 frames caller for caller at the
shift (126 calls up to the player face's then-missing AE+5 draw before the
step; 4 before census L24); the skeleton equal over AE+1..AE+1302 and 30
frames after control; first control 21 frames earlier, the capture's drive
wait (AE+1312 and 11 frames with the switch on); the windows 01 (66
frames) and 10 (311 frames) equal; the sway on 46 sampled ticks (90 draws);
the markers in 506 calls of 46 barrel frames, and snapshot 10's 5 captured
markers; the head sprites: 2 new, 149 flips, 149 ramp ends, 13,369 wait
ticks and 12,375 ramp ticks.

### The drop shadow (`check_shadow`, census L29 / L29b; tools/level_smoke_shadow.py)

Not a phase: after the phases, whenever first control was checked (every
run). The tick log's `shadow` carries, per tick, the post-step
(`w_0015C160`: whether it ran this tick, D_008102B1, D_00810771, the record
+0x214 names and the route: 0 none, 1 001DA6A0, 2 0015BF90, -1 reported
while the player record does not hold the displayed pose), the last shadow
call (em_shadow_live_log: its route, 001DA6A0's result, the kind, the
receivers and class-2 receivers, the decal's fans and vertices, whether the
passes were flushed) and, on sampled calls (the first, then every 100th
001DA6A0 and every 20th 0015BF90 call, at most 40 each), the call's inputs
and outputs in hex. It checks:
- over the whole run: every post-step's route is 0015C160's own for its
  gate bytes; every 001DA6A0 that drew was flushed, every decal with fans
  was flushed, nothing else was drawn; the first-control tick draws the
  shadow (the original's playable capture holds the chain);
- sampled 001DA6A0 calls (quick: the first, the last and two between;
  `EM_TEST_FULL=1`: all): the ORIGINAL 001CB590 + 001DA6A0, executed over
  route 01's RAM with the port's inputs patched in (the player record, its
  21 node records, ctx+0x2240 / +0x2340 / +0x2380 / +0x2468, 0x70003AC0,
  D_00810610, the area bytes, D_00817FF0), write the port's light globals
  D_00817F20..D_00817FF0, ctx+0x24B0, the silhouette VP, both box uploads,
  the UV upload and the receiver sequence with its classes; the sample's
  views equal the tick's render context (V, K, the +0x2240 projection) and
  its zoom +0x2468 the render context's at the end of that tick or of the
  one before (the camera stage's 001D25F0 rewrites it after 0015C160, as
  the opening's timeline does every frame since its post-steps are
  computed, chain C8b OPENING);
- sampled 0015BF90 calls (quick: three; full: all): the ORIGINAL 0015BF90 ->
  001F9100 -> 001F8D30, executed over route 04's RAM with the port's record,
  node records, 0x70003B8D, camera, fog and clip matrix and its 0019A570
  answered with the port's hit, submits the port's quad (tag, corners, TEX0,
  colour), and the ORIGINAL 001CE300 then writes the port's packets byte for
  byte into a cleared page slot 0, behind the mode-1 blend reference;
- the aligned route snapshots: the gate bytes and the route equal the
  snapshot's (and the kind for route 1); every route capture's mode-1
  blend block holds the writes the decal renderer implements.

Roger's shadow (since chain C8b's FACE step; check_actor, SHADOW_ORIGINAL.md
"Roger"): the tick log's `shadow_actor` carries his 001BA580 -> 001DA6A0
call of the walk (the result, kind, receivers, whether the walk flush drew
it, cumulative calls and draws) and, on sampled calls (the first, then every
100th, at most 40), its inputs. Every call is kind 0x29 and every drawn call
is flushed; on sampled calls (quick: four; full: all) the ORIGINAL 001CB590 +
001DA6A0 over route 14's RAM with his record, its 21 node records and the
views patched in writes the port's plan (as for the player's) and REFs the
kind-0x29 proxy; at the aligned snapshots of routes 13 and 14 the port's
call draws, as the captures' do.

Measured (the run through the fence door, `EM_LEVEL_SMOKE_UNTIL=fence_door_side1`): 3,194 001DA6A0 calls, all
drawn and flushed; 640 0015BF90 calls, 628 decals drawn; 1,302 reported
post-steps (the opening, before chain C8b OPENING; none since: 5,065
calls through the fence door, 11,933 on the full route); samples 4 of 32
and 3 of 32 re-executed. Full
route: 10,631 001DA6A0 calls (9,364 drawn), 640 0015BF90 calls; all 40 and
32 samples re-executed, all equal. Mutations of a node, the camera, the area
byte, a packet byte and the segment answer each fail it. Roger (full route
to roger, 2026-09-28): 11,277 calls, 5,116 drawn and flushed; all 40 samples
re-executed, all equal; drawn at 13 and 14.

### Face attachments (`check_face`, chain C8b FACE; tools/level_smoke_face.py)

Not a phase: after the phases, whenever first control was checked. The tick
log's `owner_units` carries each 001CAA00 call's face unit (bytes, digest)
and `face_units`, per attached call of the last drawn frame (the calls
given the attachment's regions: Roger, and the player while a script holds
its face slot), [record, frame, face bytes, sample]; on sampled calls (per
record the first, then every 200th, at most 80) the sample holds the call's
inputs (the record's bytes, the node records' +0x90 matrices, the face
slot, D_00810610, context +0x2410..+0x244F, the view-projection 0x70003AC0,
context +0x0C / +0x9C, the rig record D_00817BC0 and D_00275688, the area
bytes) and every byte the call appended. It checks:
- every attached call appended exactly one face unit (0x190 bytes, or
  0x1A0 with the fog-off REF 2) with its digest in the owner log; Roger
  drew one;
- sampled calls (quick: the first, the last and two between per record;
  `EM_TEST_FULL=1`: all): the ORIGINAL 001CAA00 (the body unit, then
  001CB3C0 and its callees) over route 14's RAM with those inputs and the
  tick's point-light pool patched in; every byte it writes (the bytes two
  runs agree on over a display-list window filled with a pattern and with
  its complement) equals the port's appended byte, the byte counts and the face unit's length are
  equal.

Measured (full route to roger, 2026-09-28): 15,328 face units over as many
attached calls (Roger 10,812, the player 4,516); 78 samples (Roger 55, the
player 23; bodies culled and drawn), all equal in full mode.

### The opening's actors (`check_opening_actors`, chain C8b OPENING; tools/level_smoke_opening.py)

Not a phase: after the phases, whenever first control was checked. The
original is the decomp's opening capture (build/startup-reference/
opening_ee.bin). The run is aligned on the one port tick whose camera
block holds the capture's timeline words +0x6C..+0x7B (scene 0x22, bank
0x98's clip-0 track, the cursor and the head: 001B8FC0 kind 6 started the
timeline and the cursor counts the camera stage's samples). At that tick:
- the capture's two 001BB0E0 records (001BAC00's) and the head-sprite node
  after them are live in the port at the same record addresses;
- the units of the opening body, its class-8 node and the player
  (the tick log's `owner_units`) hold the capture's pose digest (every
  node record's +0x90..+0xCF matrix) and lighting point bit for bit;
- the player's route-row bytes (+5, +1F0, +1F1, +20C, +3C, +214, +2F3, +4)
  equal the capture's.
Over the run, the body draws with its face unit, and its node with it, in
every frame whose walk ran from its spawn to the done mask; neither draws
in the frames after, and the last is before first control.

Measured (2026-09-28): aligned at port tick 295 (cursor 135 of 646); the
records 0x7A96E0 (21 nodes), 0x7AE920 (1 node) and the player (21 nodes)
equal; the body drew on 1,293 ticks.

### The chain page (`check_chain_page`, WP-13; tools/level_smoke_chain_page.py)

Not a phase: after the phases, whenever first control was checked (every
run). The tick log's `page` carries the chain page em_chain_page_live drew at
the tick's frame close (docs/CHAIN_PAGE.md): whether it was drawn this tick,
the cumulative page count, its start tag, the 001DDE10 CALL it walked over,
em_chain_page's counts (DMA tags, qwords, DIRECT packets, lane and sprite
MSCALs, XGKICKs, primitives by PRIM type, CALLs walked over, vertices
without their GIF tag's Q (0 since the fb2 step, asserted), UNPACKs before the page's first STCYCL), the decal triangles,
the digest of the primitives handed to the renderer, the glow markers'
primitives and, on sampled pages (the first, then every 250th, at most 40),
every (address, bytes) the walk read (each range once); then (since chain
C8b FLAMESNOW) the snow program's MSCALs, the weather list 001E0D70 CALLed
and the reads of the flame's descriptor. The tick's `snow` record is the
weather's last closed channel-3 list (em_snow_runtime: its frame, start,
tiles, the tile-0 packet-3 digest, whether every tile's packet 3 is the
same) and `flame` the flame's last 001D04B0 (em_effects_live: its frame,
key, the digests of its 001CFBE0 packets 1 (phase and seed masked) and 4).
It checks:
- every drawn page: the only CALL walked over is that frame's 001DDE10
  four-sprite CALL; the lane program ran 0 or 6 times, 6 in exactly as many
  pages as the barrel (001F0360) ran frames, and no lane drew;
- the weather: a page CALLs the weather's list exactly when the weather
  closed one in its frame, at that list's start, and runs its 108 snow
  MSCALs (none otherwise); the flame: a page reads the flame's descriptor
  exactly once when the flame's 001D04B0 ran in its frame, never otherwise;
- sampled pages: the ORIGINAL VU1 microcode with the DMA / VIF / GIF walk and
  the GS vertex queue (tools/chain_page_model.py), over the port's own page
  bytes (the weather's CALL walked: the snow program on every tile; the
  re-walks run in forked workers), draw exactly the port's primitives (the
  digest) with the same counts, and the blend presets, the three program
  packets and the flame's descriptor the page read hold the route captures'
  bytes;
- the camera-exact snapshots (10, 14): the glow markers the port drew equal
  the ones the capture's own latest page draws (the original microcode over
  the capture), vertex for vertex, the colour masked (it follows the draw
  of 001F4D40; check_marker_colour compares it at the EE primitive with
  each side's own draw) and a Q taken from the frame on either side not
  compared; the weather's tile packet 3 (P, the clip projection, K, the
  fog, the GIF tag row) and the flame's 001CFBE0 packets 4 and 1 (phase and
  seed words masked) equal the capture's.
Every decal the page draws is also counted back to em_shadow_live
(`em_shadow_live_page_drew`), so check_shadow's "flushed" now means drawn by
the page.

Measured (full route, 2026-09-28, chain C8b FLAMESNOW): 13,013 pages drawn
(12,573 with the six lane MSCALs, one per barrel frame, each with the
weather's 108 snow tiles and the flame; the rest are the status and
tear-down frames', empty): 4,122,371 sprites, 1,314 triangles, 1,256 lines;
12,573 001DDE10 CALLs walked over; every vertex with its GIF tag's Q (the
25,628 the page premise drew as 1.0 before the fb2 step are 1.0 by the
measured per-tag rule); 40
sampled pages re-walked equal (444 reads of the presets, program packets and
flame descriptor equal to the captures'); aligned 10 (5 glow markers, the
snow packet 3 and the flame packets 1 / 4) and 14 (0 glow markers, the same
packets).

### The load veil (`check_load_veil`; tools/level_smoke_load_veil.py)

Not a phase: after the phases, whenever first control was checked (every
run plays the New Game load). The tick log's `veil_draw` carries, on the line
after each frame whose 0021B1B0 drew and whose step-V list the GS frame stage
drew (em_load_veil_live; docs/LOAD_VEIL_PARTICLES.md section 3.3): the frame
counter, the channel-0 run and its bytes, the buffer index, the kicked list,
the displayed FRAME_1, the walk's counts, the digest and the brightest line
colour byte. For each such frame, with the veil block of the tick whose
counter it is (its pre phase and base Y, its post levels), it checks:
- the ORIGINAL 0021B1B0, executed over the opening capture with that block,
  context +0x9C = the slot and the channel-0 cursor = the run's start, writes
  the port's run byte for byte, except the fourth word of each of the 30
  strips' ST quadwords (001DFA40's table lane 3, stale stack words the GS
  ignores); the seed it leaves is the port's; the ORIGINAL 0021B500 steps the
  phase to the port's;
- the list drew the clear, 512 lines, two copy sprites and 900 strip
  triangles into the slot's frame buffer (FRAME_1 0x80038 or 0x80000);
- a level-0 veil lit no line (all black);
- the seed the load leaves equals every route capture's.
It prints the phase the load left beside the captures'.

Measured (2026-09-29, chain step H7: the New Game's module 3 and area
load run the loader task's own steps, MODULE_LOADER.md 1.9): at host
speed **55 veil frames**, none at level 0 (the ramp over the area load's 20
dispatches, then the decay), the phase the load leaves 0.385; with the PS2
disc-drive timing switch (the recorded New Game reads) **257 frames**,
phase 0.799, where the captures, after the PS2 disc load, hold 0.806 (258
steps: the one more is the PS2's ninth sound-bank call, IOP_STREAM.md "The
sound-bank transfer"). Every one of those frames passes the checks above.
The run log's "module loader:" line gives the dispatches and the area's
uploads (1 A entry, 1 player packet) and 001FB370's calls (8) and
command 0x20s (1).

## What the full route does not yet compare (2026-09-28)

`make test-level-smoke-full` plays route beats 01..14 on the main line and 00
and 09 in their own runs, and every phase reproduces its capture. These are
the places where a check is still relaxed. Each is reported by its phase,
never silently skipped. Chain C8b ROUTE re-ran the whole route at port
HEAD 6594182 (2026-09-28: `make test-level-smoke-full`, NOT-LIVE: none, and
`make test-level-smoke-ps2-drive`) and re-read every row against the run's
report: each row below still holds as written, and none could be removed
faithfully in that step (each waits on navigation timing, the rand()
stream's position, a renderer stage, the area load's sound-bank chain or a
new capture). Since chain C7's table (2026-09-27) chain C8b removed the
panel's prompt-window row (the module loader, LOADER) and check_shadow's
reported opening post-steps (the opening's records, OPENING), and moved
check_rand_order's row from AE+1 to the actors' spawn (the security gun's
owner, census L24; OPENING); it added the rows of the checks it created
(Roger's units and faces, the fans' phase, the flame's and the snow's
sprites, the load veil). What removes each:

| Where | What is relaxed | Why | What removes it |
|---|---|---|---|
| cage_roof (10) | the voiced line 0x7F's teardown and what follows it land 2 rows earlier than the drive mode alone explains (8 at host speed, 2 with the switch on); check_voice_drive allows exactly the key-on's shift, which it proves is the drive's difference plus the fields the original's read sequencer spent on a lane-0 music refill first | the music's refill phase at the line's start is the time since the music's last start (3583 fields in the original, 3449 in the port): navigation | walk timing equal to the capture's since route 03's status close (navigation) |
| status_pages (designed) | no capture: the page calls are replayed through the original instructions over the status-hub capture, not compared with a recording; pixels are not compared; the stack's register save slots are not compared; the takes are the scan's claim made directly, not walked to | no route capture shows a status page open | a PCSX2 capture of each page (hub → page) and of each take |
| roger (14) | Roger's +0x1FE flags and the equipment's +0xB0 before his clip init at f358 | his idle clip's phase is the time since the area load, which the smoke's walk does not share with the capture | walk timing equal to the capture's (navigation) |
| slide (06), cage_ladders (10) | the landing row within one row, the heading crossings within two rows | the stance the stick reaches differs from the original's by up to 0.86 | navigation only; the slide's motion after the landing is exact |
| check_owner_units | the player's and the equipment's B, rig lanes and rows at snapshots 08, 11, 12 and 13 (compared at 10 and 14) | the player's placement at the aligned tick follows the navigation's timing (the phases compare it on their own windows) | navigation that reaches each snapshot's placement |
| check_owner_units, check_face | Roger's and his equipment's units at snapshots 08, 10..13 (only the face unit's length; compared in full at 14), and the face units' content against the captures (the sampled re-execution over the port's own inputs proves it) | Roger's clip phase follows the time since the area load; his face weights follow the port's rand() stream | walk timing equal to the capture's (navigation); a stream at the capture's position |
| check_effects | lane 3's parameter quadwords | no routine of the level writes them (identical from the opening on) | their earlier writer (EFFECT_MANAGER.md 8.4) |
| check_chain_page | the page's sprites other than the glow markers (head sprites, puffs, equipment sprites), the glint and the decal against the captures' pages | their inputs follow the draws (the head sprite's phase: check_head_sprites proves its transitions over the port's own draws; the puffs' seeds) or the navigation's timing; the sampled re-walks prove the drawing of the port's own pages | navigation to each snapshot's placement; a stream at the capture's position (docs/RAND_ORDER.md section 6) |
| check_rand_order | the opening's values from the actors' spawn on (compared caller for caller at the drive's shift until a value-driven timer differs), and (switch on only) its end | the stream request's wait: at host speed the drive answers at once (the Original profile's policy), so the spawn and the end come the capture's wait earlier and the values drawn after differ; with the PS2 disc-drive timing switch on, the area music's 16-field seek is outside the drive model (at host speed the end is exact) | none at host speed (policy); with the switch, a drive model of the seek from the movie's position |
| check_gun_fan | the fans' phase at the aligned snapshot ticks (each snapshot's state is only required to be on the port's cycle) | the fans' cycle counts the owner's calls from the area entry, whose number at a snapshot follows the recording's timing (the opening's drive wait, navigation) | walk timing equal to the capture's (navigation) and the drive-timing switch |
| check_chain_page | 001DDE10's four-sprite pass (slot 0xFFF) is walked over, not drawn | it samples the frame buffer as a texture; its look is not reproduced (CHAIN_PAGE.md section 6) | a renderer stage for the frame-copy sprites |
| check_chain_page | the flame's and the snow's sprites (only their packets' camera, fog and matrix rows are compared in the camera-exact beats) | their positions and colours follow the owners' seeds and phases (rand() at 008235F0 / 001E55F0 state 0, the flame's age), which the port's stream does not hold at a capture's position (RAND_ORDER.md) | the rand() stream at the capture's position |
| check_load_veil | the veil's pixels, and how many ticks it runs | no capture holds a load's frame (every capture is taken after the load); the veil runs as long as the loader's steps take (chain step H7): 55 frames at host speed (the user's policy: the disc answers at host speed), 257 with the PS2 disc-drive timing switch where the PS2 drew 258 (its ninth sound-bank call, most likely SIF DMA time, which no mode reproduces) | a capture of a frame mid-load (the decomp's fb2 method) for the pixels |

Not compared at all: the sounds (WP-14), the pixels (the smoke itself
compares none; the fb2 pixel harness `make test-fb2-pixels` measures the
frame at first control and, with `EM_TEST_FULL=1`, at every fb2 point the
smoke aligns, against the software-renderer fields, GS_EXACT.md section 10;
by eye against each beat's original.png: `EM_LEVEL_SMOKE_PHASE_CAPTURE`;
the chain page's GS pixel path is checked against a GS pixel model by
`make test-chain-page-gpu`, the load veil's GS frame by `make
test-load-veil-gpu`, the static world's triangles by `make
test-static-world-gpu`),
the walks between the scripted and climbing windows (navigation).

**Frame order.** `tools/compare_frame_order.py` must be given a
post-control window of a newgame-control trace (`--native-index`), as
LOCOMOTION_DISPLAY.md does. Without it the comparator aligns idle04 / walk04
/ st03 on the first native window with the same machine state, which is the
three selector-0 ticks at the opening's end, where record 13 (008257A0)
still ticks. The report then shows "drum area11[14] against 008257A0
area11[13]". That is an alignment artefact, not a node-order divergence:
from native index 1330 (counter 2587, first control + 11) idle04 PASSes
event for event, and cut15 on its own window (host speed, the default).
walk04 is compared in a walking window: from native index 1392, the tick
the port's walk spawns its first footstep effect node (00187EE0 ->
001EFD90 -> 001EF9D0, callback 001EA240, census L26), it PASSes event for
event; an idle window (1330) lacks that node, which the original's walking
frame holds. Since chain C8b OPENING (2026-09-28) the opening's two
001BB0E0 records and their head sprite are in the walk, as in the
original: cut02 PASSes from native index 26 (the actors' spawn, AE+10;
before it the walk lacks them) and st03 at native index 1321 (the records'
last walk after the controller's done mask, the original's frame 3965).
**Current windows (measured 2026-10-02, chain step A11FIX, identical at the
step's base):** since the area load runs the loader's steps (chain step
AREALOAD) the opening and first control come 64 ticks later in a
newgame-control trace (first control at native index 1383, counter 2640), so
the windows above are idle04 1394, walk04 1456, cut02 90 and st03 1384; all
four and cut15 PASS there.
`tools/frame_order_allow.json` has no entry since chain C8b ROUTE
(2026-09-28): its last entry, the walking footstep node, was retired
because the port spawns the node through the original chain and walk04
passes without it (a stricter comparison). With the PS2 disc-drive timing
switch on (`EM_PS2_DISC_DRIVE_TIMING=1`) first control comes 10 frames
later and the windows are native index 1340 (counter 2597) for idle04,
1402 for walk04, 36 for cut02 (the spawn at AE+20) and 1331 for st03,
where the same five PASS (2026-09-28). cut07 (selector 3) and st14 (Roger)
have no matching window in a newgame-control trace.

**Since chain step H7 (2026-09-29)** the New Game's module-3 and area
loads take the loader task's steps and the veil's ramp and decay, so the
area entry moves 63 ticks later at host speed (trace index 79, counter
1336) and 325 with the switch (index 341, counter 1598); every window moves
with it: at host speed idle04 at native index 1393 (counter 2650), walk04
1455, st03 1384, cut02 89; with the switch 1665, 1727, 1656 and 361. All
five PASS in both modes with the empty allow list (cut15 on its own
window); `--self-test` passes.

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
