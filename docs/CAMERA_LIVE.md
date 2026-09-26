# The live walking camera (census L13..L16)

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Chain step "Census L13..L16: walking camera live" (2026-09-25). In AREA11
the camera the player sees is the original camera, translated routine by
routine and bound over one storage of the original bytes. The legacy
follow camera of `em_camera.c` (`camera_update`, its prestep, dispatch and
solver copies) no longer runs in the first level. It is kept only for a
scene without an original collision world, which is outside the first
level.

## 1. What runs, and where

| Original | What it is | Translation | Live call site |
|---|---|---|---|
| 0018B9C0 | the camera frame | em_camera_leftovers `em_camleft_0018B9C0` | `w_0018B9C0` → `em_camera_0018B9C0` / `_opening` → `em_camera_live_frame` (both world-frame variants) |
| 0018BC20, 00190F20, 0018C0C0 | action dispatch, area trigger, target copy | em_camera_leftovers | from 0018B9C0 |
| 001914A0, 00191580, 0018C5A0 | camera action 8 (the mode-8 settle) | em_camera_leftovers | action 8 |
| 00195130, 00193EB0, 001936E0, 00191210 | camera action 0, the event router, the lock-on swing, the area-0x10 clamp | em_camera_area11_specials | actions 0 and 3 |
| 001916C0, 00191000, 00193D90 | target placement, L1 orient-behind, idle auto orbit | em_camera_leftovers | specials workers |
| 001921D0, 0018D7B0, 0018D330, 00191390 | follow update, solve dispatch, prepass, per-state heights | em_camera_follow_original | follow workers; 0018D7B0 also from the scripted retarget and the frame machine's state 4 |
| 0018C6A0, 0018C4B0, 00191D40, 00192010, 00191120 | the chases and yaw step | em_camera_follow_original | direct calls |
| 0018DD20, 0018F870, 0018D910, 0018CE60 | the solvers and bounds | em_camera_leftovers_solver | follow workers |
| 00230000, 0022FCA0, 00194D10 | the locomotion tether (player codes 2/4/0xF) | em_camera_leftovers | follow / specials workers |
| **0018C0D0** | the commit | **em_camera_commit_original** (new) | 0018B9C0's commit, `camera_commit_original` (the interaction and script hosts, the opening, the frame machine's state 4 and 5), 001B0460 |
| **00102798** | 4x4 transpose (D_00810650) | em_camera_commit_original (new) | inside 0018C0D0 |
| **00193660** | 001936E0's grab test | em_camera_commit_original (new) | specials worker |
| 00102CD0 | the look-at | em_census_standins `em_cs_00102CD0` | inside 0018C0D0; `em_cs_view_to_native` gives the renderer's view |
| 001B0460 | the camera re-seat from the room's camera record | em_script_host_workers `em_script_host_001B0460` (reads the spawn table window) | 001B07C0's last call (area load and room move), `spawn_w_001B0460` |
| 001B0080 | the room-entry seat | em_script_door_fan `em_sdf_001B0080` | 001B0460's worker |
| 001B0B50 | D_008106BE from D_008106C8 | em_player_closure_10_12_19 | 001B0460's worker |
| 0015CBA0 | +1F0 → the action code +230 | em_camera_leftovers `em_camleft_0015CBA0` | the player stage after 0015BCF0's tail (em_player.c) |
| 0019A910 / 0019B7D0 | the camera's segment / ground queries | em_coll_segment_walkers / em_coll_list_passes_walkers | over the collision world's one probe state |

The frame machine's state 4 (the room move) calls 0018D7B0(cam, 1) and
0018C0D0(cam, 1) through the live camera too (`w_0018D7B0`,
`w_0018C0D0`); the reported no-effect bindings UM_0018D7B0 and
UM_0018C0D0_STATE4 are gone, and so is UM_001B0460.

## 2. The new translations (`src/game/em_camera_commit_original.c`)

- **0018C0D0(cam, mode)** is byte-matched C in the decomp; the
  translation follows its instructions: the w lanes of the actual target and
  eye set to 1.0; 0x700038A0 = target − eye and its +0x0C = 1.0; the
  actual horizontal distance D_0081069C = sqrtf(x·x + z·z) (the multiply
  and the multiply-add into the argument), forced to 0.001 below 0.001 with
  0x700038A8 = 0.001 and the eye's z nudged by 0.001; D_00810698 = eye y −
  the player's +A4; the desired horizontal distance D_00810690 from cam+20 −
  cam+10 the same way (cam+18 nudged); D_00810694 = |0x700038B4|;
  0x700038A0 normalised; the view position 0x700038C0 = the eye plus 4.0 or
  −1.0 (mode ≠ 0: −1.0 for action 0xA, else 4.0; mode 0: 4.0 for actions 1
  and 2 unless +4 is 3, else the eye itself), each lane a multiply then an
  add; the view D_00810610 = 00102CD0(0x700038C0, 0x700038A0,
  D_008105F0); cam+B0 = the forward; D_00810650 = the transpose;
  D_008106A0 = atan2f(−z, x) of the forward; D_00810600 = the forward;
  cam+9C = 001B1240(eye, target x, target z).
- **00102798** moves the words of a 4x4 matrix into its transpose (four
  quadword loads, the word interleaves, four stores).
- **00193660** is 0x700038A0 = D_008105E0 − D_008105D0, 0x70003A20 =
  sqrtf of its xyz dot, and 1 when that is below 5.5.

Workers: sqrtf 0011E748 and atan2f 0011E620 (em_sdk_math_original over the
collision world's SDK context), 001B1240 (em_script_host_workers) and the
look-at. A missing worker faults before the first write.

## 3. Storage, views and inputs (`src/game/em_camera_live.c`)

**One storage** of the original bytes: the camera block
0x008101E0..0x008102AF, the vector pool D_008105D0..D_008106A3 (eye,
target, up, forward, the view and its transpose, D_00810690..D_008106A0),
the camera's scratchpad words 0x700038A0..0x70003A3F, 0x70003400 (matrix),
0x70003600, 0x70003630 and 0x700031B0 (the segment query's point, with the
hit record's +0x1A halfword and +0x24 normal). The render-context words
001DD980 publishes (+0x2450..+0x2467) are the render context's since
2026-09-25 (em_render_context_live, RENDER_CONTEXT.md section 8): the camera's
001DD980 calls run the distance math (em_interaction_projection_001DD980)
and then 001DD950 on that one block. `em_camera_live_bytes(address, size)`
reads or writes any of the first two by original address; the render
context reads D_00810610 and D_008105E0 through it, and the player record
through `em_camera_live_player_bytes` (the camera's view, section 5).
`em_camera_live_adopt_view` loads the g.cam view into the bytes for a
g.cam writer that calls 001DD980 before the next camera entry.

**The module views.** The follow module (`EmCameraFollowScratch`) and the
specials module (`EmCamSpecialsScratch`) each keep a struct view of these
words; em_camera_leftovers works on the canonical window directly. Every
worker adapter that crosses from one module to another stores the caller's
view to the canonical words, loads the callee's view, and does the reverse
on return, so every routine sees the words the original would.

**The g.cam view.** The port's legacy camera struct `g.cam` is a view of
the same bytes for the modules that still use it (the interaction and
script hosts, the opening runtime, the director and door stand-ins, the
renderer). Every live entry point loads the g.cam fields into the
original bytes first and stores them back after:

| g.cam | original | g.cam | original |
|---|---|---|---|
| state, sub_state, swing, top_mode, table_sel, mode, hit | +0, +1, +3, +4, +5, +6, +7 | yaw, orbit_tgt, orbit_rad | +44, +48, +4C |
| timer | +8 (halfword) | y_lo, y_hi, var_5c, overhead_y | +50, +54, +5C, +60 |
| eye_des, tgt_des, seed_euler | +10, +20, +30 (x/y/z) | hit_attr, probe_flags | +58, +5A (halfwords) |
| ground_attr78, cine_scene | +6D, +6E | cine_track, cine_time, cine_head | +70, +74, +78 |
| aim_h, wall_yaw, tgt_soft | +8C, +90, +A0 | eye, tgt, up | D_008105D0 / E0 / F0 (x/y/z) |

The commit's outputs go out only: the forward D_00810600, D_00810690
(`horiz_dist`) and the native view (`em_cs_view_to_native` of D_00810610)
with `g.viewproj` from the render context's zoom +0x2468 (em_rcl_zoom; the
`g.cam.zoom` copy is gone). The first level's world frames draw with the
frame head's view instead (the D_00810610 of the previous camera stage,
RENDER_CONTEXT.md section 8.2). The camera distance +0C, the preset +64, the w lanes, +2,
+8B, +94..+9C, +B0 and the timeline's +6C have no g.cam field: the block
keeps them.

**The binder's inputs** (`EmCameraLiveHost`, set by `em_scene_bindings.c`
at the area build; a fixture supplies its own):
- the player record D_008102B0: the live record (`player_states_actor`),
  with +A0 = g.pos (the port's canonical placement), +B0 = the pose host's
  bone-1 position (`player_pose_hip`; 0015BCF0's tail stores node 1's
  +C0 there), +C4 = g.yaw, and 0x70003B50 = the pose host's published
  Euler (`player_pose_script_euler`), as the script host's view does.
  While the pose host has none, +B0 and 0x70003B50 are substitutions
  (section 5);
- the pad assignment block 0x70003B74.. (its word 6 is 0x70003B80,
  00191000's and 001936E0's L1 mask) from the player closure;
- 0x700031F0, the AREA11 boxes' word (the truck's carry sets it, 0015BCF0
  now clears it at its start, 0018B9C0 ORs its low byte into +8B);
- camera action 0's legacy pre-emption and the +4 == 3 timeline (section 6).

**The ELF tables.** 00190F20 (area 0xE) and 00194D10 (the tether's region
test) read 001B1EA0 quads at D_0024A4B0 and D_0024A5F0 + 0x40·i:
`tools/export_camera_tables.py` writes the span 0x24A4B0..0x24A6F0 from the
user's ELF into `assets/camera_tables.emrg` (STARTUP.md step 48; required:
the camera does not bind without it).

**0x70003B40..0x70003B5C is not camera storage.** The camera only reads
it; no camera routine stores it. The port holds two sources, each the
latest original writer at the moment its one reader reads:
- the scene state's `spad3B40[0..7]` is 001B07C0's write (the spawn's
  +B0 / +C0 quads). Its only camera reader is 001B0080 (`rw_001B0080`),
  which runs inside 001B07C0 -> 001B0460 (001B07C0 calls 001B0460 at its
  end, after the store; em_scene_bindings `spawn_w_001B0460` is the port's
  only caller of the live seat), so it always reads that write. The
  seat's frame 2639 matches byte for byte;
- the camera frame's `C.s3B50` is a load, at every camera entry, of
  0015BCF0's tail publication (+C0 -> 0x70003B50, the pose host's saved
  Euler). In both of the original's frame paths 0015BCF0 runs before
  0018B9C0 (ORIGINAL_FRAME_ORDER.md), so the tail's copy is the latest
  write the camera frame reads.

The two must become one storage (the pose host publishing into the scene
state's 0x70003B40..5C) before another caller of 001B0460 is bound
(00157360, 0016D130, 0016DE40, 001B7B30 in the original): there the latest
writer is 0015BCF0, not 001B07C0.

**The progress bytes** D_0081078B (event 0x33, 00191210's gate) and
D_00810803 (counter 0x2B, 00195130's area-0 gate) are canonical in the D2
region since this step; nothing writes them in AREA11.

## 4. Verification

- **`make test-camera-live-reference`**
  (`tools/test_camera_live_reference.py`, ~3 s; `EM_TEST_FULL=1` ~20 s).
  - The original 0018C0D0 executes whole, every callee included, on the EE
    model (RvrEE: COP1, VU0 macro and MMI through tools/ee_float_model.py):
    001028D0, 0011E748, 0011DF78, 00102760, 001031E0, 00102CD0 with its
    leaves, 00102948, 00102798, 0011E620 and 001B1240.
  - The cases: 400 random states (6,000 full), with both arguments, the top
    mode and action arms, both 0.001 floors and rolled up vectors; plus the
    camera state of the 14 in-scope route captures and three reference
    captures, both arguments.
  - Compared word for word: the camera block, the pool and the scratch
    window.
  - Also 00193660 on 120 cases (2,000 full) around 5.5, and 00102798 on 20
    matrices (200 full), also in place.
  - Mutants: five of six killed; the survivor (the multiply-add written as a
    multiply plus an add) is equivalent for finite squares.
- **`make test-camera-interaction-fixture`**: the panel and refusal
  retargets on the live camera over the captured scenes' own collision
  world. Every camera word the retarget writes and the actual eye / target
  now equal the captures byte for byte; the bounds tolerance of the legacy
  path (6.1e-5) is gone.
- **`make test-area11-interaction-host`**: the host fixture binds the live
  camera over the captured block (its panel scenes run the retarget and
  the commit on it).
- **The level smoke** (LEVEL_SMOKE.md, `make test-level-smoke-full`):
  - **Area load** (`newgame_samples.jsonl`, the original's camera block per
    frame from New Game): the 001B0460 seat (frame 2639) byte for byte, and
    0018B9C0's state-0 frame (2640) byte for byte except the state-0
    ceiling (section 5).
  - **Hand-off to first control** (`postcinema_samples.jsonl`): the settle
    ends on the port's first-control tick as it does on frame 4027. The 24
    frames before are byte for byte (except the section 5 fields), and the
    40 sampled frames before those differ only in the eye / target heights
    and forward the settle is still chasing, the eye height difference never
    growing.
  - **After each script's release** (the rows to the end of each capture):
    - the refusal (route 02), the elevator (04) and Roger (14): the follow
      camera equals the capture row for row: the eye, target, desired
      eye / target and the camera bytes +4..+7;
    - the panel (03): exact from f679, 24 rows after the release (the
      retained approach Y, 0.003, decays);
    - the truck preview (07) converges from 0.14 to 1e-5 by the capture's
      end (the solver flags +7 of the last walking frame before the script
      were 0 in the port and 0x40 in the original: the approach is
      navigation).
  - **The battery** (route 01): the op00 sub8 settle now starts from the
    original's follow camera (0.037 off at the pad-navigated stance), and
    the post comes on the original's row (64 rows after the scan; 61 under
    the legacy camera). `check_battery` now requires that row.
  - **Every walking phase** runs on the live camera (the stick is steered
    against the live forward) and still equals its route rows: boxes,
    ladders, climbs and the jump unchanged. The slide's entry point moved
    (navigation: 0.88 from the original's in X/Z, 0.58 under the legacy
    camera), which moves its second and third heading crossings by two rows
    and its landing by one; `check_slide` now aligns the rows after the
    landing on the landing and scales the allowed rows to the measured
    entry offset (`SLIDE_ENTRY_ROWS`: one row up to 0.6, two up to 0.9, an
    entry further off fails; LEVEL_SMOKE.md). Every field of every other
    row is still compared exactly, and the entry-speed mutant (0.2 -> 0.21
    in em_player_slide.c) still fails at f82. **Pending lead review**: the
    relaxation follows from the smoke's navigation input, not from a camera
    or slide routine. A tighter approach was tried and does not help: from
    within 0.28 of route 06's stance the entry is still 0.86 off, since the
    port's walk heading from the stick (L12) and the live camera's state at
    the stance (3 units from the capture's eye, it follows the port's own
    walk history) set the path. With L12 live (2026-09-25; the first-control
    record equals the original's, LOCOMOTION_DISPLAY.md section 4) the entry
    is unchanged at 0.863: the smoke's own stick input and the camera state
    it produces set it, not the walk translation. One row needs the route's
    own input replayed from the capture's state.
  - **Roger** (route 14): his script block's halfword +0x0E (Roger's
    +0x1FE, the animation flags of his idle clip) is compared from his clip
    initialization (f358) on, like the equipment's +0xB0: before it the
    idle clip's loop wrap (0x3000 for one frame) falls on the time since the
    area load, which the port's walk (now steered against the live camera)
    no longer shares with the capture's save state. Recorded next to the
    +0xB0 exemption as navigation-induced, to lift once the smoke's walk
    timing matches the capture's; pending lead review.
- `EM_STARTUP_TEST=newgame-control`: 30 ticks travel 9.599989 (unchanged at
  this step; 9.599849, the original's, since census L12).
- The translated modules keep their own oracles, unchanged:
  test-camera-follow-original(-reference), test-camera-leftovers(-reference),
  test-camera-area11-specials-reference, test-census-standins-reference.

## 5. Limits and known differences

- **Substitutions in the camera's player view** (`player_refresh`,
  em_camera_live.c). The original's 0015BCF0 evaluates the pose and
  publishes it every frame; the port evaluates none before first control,
  so while the pose host has no evaluated pose:
  - +B0 (the bone-1 position 0015BCF0's tail copies from node 1's +C0)
    reads the placement g.pos;
  - 0x70003B50 (the tail's copy of +C0..+CC) reads the record's +C0, g.yaw
    and +C8.
  What replaces both: the player's pose evaluated from the area load
  (0015BCF0's animate step and tail publication running before first
  control), after which the pose host's hip and Euler are the tail's
  publication on every frame. Their visible effect is the state-0 ceiling
  below; the smoke's exemption for it goes with them.
- **The state-0 ceiling.** At the area load the port evaluates no player pose
  before the opening releases the player, so 0018D330's ceiling probe
  starts from the placed position instead of the hip (the +B0 substitution
  above). The original finds the
  0x8800 ceiling at y 440 (+5A bit 0x80, +60); the port does not. No camera
  routine reads bit 0x80 or +60, and the first walking prepass rewrites
  +5A.
- **The opening's timeline words** +6C..+7B (001B8FC0 kind 6 / 0022EEF0 on
  bank 0x98) stay the opening runtime's. It plays its track itself
  (em_opening_runtime, the opening's timeline stand-in, census L33) and keeps
  its own cursor. It also releases the camera one settle frame earlier than
  the original, which the hand-off check shows converging.
- **Faults where an original has no translation**:
  - camera actions 1/2 (aim: 00197D20, 00198650), 5, 9..15, and mode 1's
    001B0300;
  - 001B0C60 in areas 0x12 / 0xE;
  - the specials' other-area arms (001944B0, 00194DB0, 00230230,
    0x823FE0, 001AEDE0).

  None is reached on the route (the census saw only actions 0 and 8). The
  port's own aim (R2 / R1 stance) never sets the aim codes (+1F0 49..53), so
  its camera stays the stand-in of section 6.
- **0x70003A28 across frames.** 0022FCA0's orbit (the tether, codes 2/4/0xF)
  reads 0x70003A28 as the previous frame left it. In the original the player
  routines also write that word between camera frames, and the port's player
  modules keep their own copies. The camera's copy carries only the camera's
  writes. The route never takes the tether.
- **Legacy scenes** (no original roster) keep `camera_update` and
  `camera_commit_view`, whose look-at is now the translated 00102CD0.

## 6. Stand-ins that still pre-empt camera action 0

`camera_area11_standins` (em_camera.c) runs in 00195130's place while one
of these owns the camera. The frame, the dispatch and the commit around it
stay the original's:

| Stand-in | Owner it stands for | Lane |
|---|---|---|
| `em_examine_camera` | an examine cue's op00 shot | (no AREA11 route beat) |
| `camera_mode1_aim` + 0018D7B0(0) | the aim camera 00197D20 / 00197870 | L28 |

The +4 == 3 timeline is the opening runtime's track while it owns the
camera, and otherwise the AREA11 script host's 0022EEF0 (census L22).

Since census L18 the fence door's camera is its program's op0D sub 5 on the
AREA11 script host (0018CBD0 with -20, 0018D7B0(5) and (1)) and 0x1AE040
state 4's re-seat (D_008101E4 = 0 stored to this block's +0x04 at its
0018D7B0 call, then 0018D7B0(1) / 0018C0D0(1)); `camera_door_cinematic` no
longer stands in for AREA11 (it stays in `camera_update` for the scenes
without the live camera). Route 09's scripted camera and the follow camera
from the re-place equal the capture row for row (LEVEL_SMOKE.md
"fence_door").

## 7. Retired in this step

- `src/game/em_camera_probe.{c,h}`, the prepass / AREA11-bounds duplicate,
  and its test `tools/test_camera_probe_reference.py`
  (`make test-camera-probe-reference`). Superseded by the translated
  0018D330 / 0018D910 and the exact camera interaction fixture.
- `tools/test_camera_commit_reference.py`
  (`make test-camera-commit-reference`), the legacy commit's offset gate
  with its leaves hooked. Superseded by test-camera-live-reference, which
  executes the whole commit.
- `em_mat4_lookat_gs` (em_math.h). Every view is now the translated
  look-at. The two fixtures that built a test view carry their own input
  helper.
- In AREA11: the legacy follow camera, the D_008106EF countdown helper (the
  frame does it), `em_game_legacy_camera_rearm` (001B0460 is translated) and
  the closure's own D_008106A0 computation. D_008106A0 and cam+9C are read
  from the commit's words; the recovery scene's D_0081027C is now cam+9C, as
  in the original, not the forward heading.
- The opening's fabricated heading write at 001B8FC0 kind 0 (the placement
  now copies the record's quads, w lanes included, into the camera).
- The reported no-effect bindings UM_001B0460, UM_0018D7B0 and
  UM_0018C0D0_STATE4 (em_scene_bindings.c): the calls are bound.

Fixtures adapted, no assertion changed: the opening runtime
fixture's 0018B9C0 worker commits after the split sample
(tests/opening_runtime_test.c), and the player host fixtures stub the
stage's two camera-side stores (tests/player_*_host_test.c).
