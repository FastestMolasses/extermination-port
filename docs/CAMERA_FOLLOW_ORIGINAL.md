# Follow camera (001921D0 with 0018D7B0, 0018D330 and 00191390)

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "camera-follow-original" (roadmap WP-16: CAM-08 the 001921D0 tail
for player codes 1/3, CAM-10 the 0018D330 prepass for every style).

This is the camera the player sees whenever no script and no area special
holds it: in AREA11 every frame between the scripted scenes (after the
elevator refusal, panel and ride releases, on the boxes, the hill slide,
the crevice, the tower). The camera frame 0018B9C0, with the camera
block's +4 == 0, runs:

1. 00191390(cam, player): the per-state heights;
2. 0018BC20 → 00195130 (action 0, em_camera_area11_specials.c): the
   area arm. AREA11 (area 0xB) has two fixed-eye boxes; outside them
   00195130 calls 001921D0(cam, player, 0) (camera state +1 = 0/1), or
   001921D0(cam, player, 1) for +1 = 3;
3. the commit 0018C0D0.

The module is **built and tested but not wired** (section 4). It
translates:

| original | what it is | source form read |
|---|---|---|
| 001921D0 | the follow update | NEARMISS C + the instructions (the instructions win) |
| 0018D7B0 | the solve dispatch | byte-matched C |
| 0018D330 | the prepass (CAM-10) | byte-matched C |
| 00191390 | the per-state heights | asm-word file (the instructions) |
| 0018C6A0, 0018C4B0 | the actual-eye chases | asm-word files |
| 00191D40, 00192010 | the eye-height chases | NEARMISS C / asm-word file |
| 00191120 | the bounded yaw step | asm-word file |
| 001028D0, 001028B8, 00102760, 00103230, 00102738, 001026A0, 00102948, 001031E0 | the SDK vector leaves (VU0 macro code) | asm-word files |
| 0011DF78 | fabsf | reused: em_sdk_math_original.c |

Offsets below are on the 0xD0-byte camera block at 0x008101E0 ("cam+X")
and the 0x320-byte player record at 0x008102B0 ("p+X"). Floats are
compared with the EE compare rules (em_ee_float.h); "≤" is C.LE.S, and so
on.

## 1. What the original does

### 00191390(cam, p): the per-state heights

It clears cam+94 and cam+98, then sets the eye height offset cam+8C and
the height parameter cam+5C by the player state word p+230:

| p+230 | cam+8C | cam+5C |
|---|---|---|
| 0x13 | 11.0 | 2.0 |
| 6, 7, 8, 9, 0x2C, 0x2D | 0.0 | 2.0 |
| 2, 4, 0xF | −3.0 | 1.0 |
| anything else (1, 3, ...) with cam+64 == 0xC1F99999 | 2.0 | 6.0 |
| anything else otherwise | 6.0 | 2.0 |

Finally cam+98 = 23.0 when the ground byte cam+6D is nonzero. The cam+64
constant is the pattern the instructions build, 0xC1F99999, one ulp
below the nearest float to −31.2 (0xC1F9999A); the oracle feeds both.

### 0018D330(cam, p, style, mask): the prepass (CAM-10)

It fills three camera fields from collision queries (0x700038A0 and
0x700038B0 are its scratch vectors):

1. **Ground under the hip (cam+6D).** 0019B7D0 from (p+B0, p+B4 + 4,
   p+B8, 1) to (p+B0, p+A4 − 2, p+B8, 1). cam+6D = 1 on a nonzero result,
   else 0.
2. **Ceiling over the hip (cam+5A bit 0x80, cam+60).** 0019A910(p+B0,
   (p+B0, p+B4 + 200, p+B8, 1), mask). On a hit whose record halfword
   (*0x700031D0)+0x1A has 0x8800, the flags start at 0x80 and cam+60 = the
   hit point's y (0x700031B4). Otherwise the flags start at 0 and cam+60
   keeps its value.
3. **The ray from the player toward the eye.** From
   (p+A0, p+A4 + 11, p+A8, 1) toward (cam+10, p+A4 + 11, cam+18, 1),
   normalised:
   - **style ≠ 2:** 20 units long (the 20-unit offset is kept at
     0x700038C0).
     On a hit, record bit 0x2000 sets flag 1, else 0x8800 sets flag 8.
   - **style 2 (aim):** 9 units long (the offset kept at 0x70003910),
     the same flags; then a second 9-unit ray from 6 units over the feet, where a
     hit with 0x2000 sets flag 0x10.
4. cam+5A = the flags (a halfword).

### 0018D7B0(cam, style): the solve dispatch

mask = 7 for style 2, else 6. It runs 0018D330(cam, D_008102B0, style,
mask), then the solver the style picks:

- styles 2 and 6: 0018F870(cam, player, style, mask);
- style 5: 0018D910(cam, player, mask), result 0;
- every other style: 0018DD20(cam, player, style, mask).

cam+7 = the result (a byte). Then style 1 copies cam+20 to the actual
target D_008105E0 and cam+10 to the actual eye D_008105D0 (16 bytes
each); style 0 chases the actual eye toward cam+10 with 0018C6A0 and
0018C4B0 at 4.0. It returns the result.

### 001921D0(cam, other, freelook, unused): the follow update

`other` is the a1 00195130 passes (the player). The dispatch is on
other+230; `sign` starts at 0.0.

**Posing states.** Most end in 0018D7B0 ("solve N") and a chase of the
actual eye D_008105D0 toward cam+10. "approach" is 001B12B0, "wrap" is
001B1470, and "boom" means cam+10 = D_008105E0.x + L·sin(yaw) and
cam+18 = D_008105E8 + L·cos(cam+44).

| other+230 | what it does |
|---|---|
| 2, 4, 0xF | the locomotion tether 00230000(cam, other); nothing else |
| 6 | cam+44 = approach(other+C4, cam+44, 1°); cam+6C = 0; boom L = cam+C; solve 3; chase x/z and y at 0.8 |
| 7 | returns at once in area 0x13 room 1; else as 6 toward π + other+C4 at 2° |
| 0x18 | solve 4; chase x/z at 0.8 |
| 9 | 00191D40(cam, cam+8C + cam+5C + other+B4, 1.0); solve 3; chase x/z and y at 0.8 |
| 0x14 | yaw toward wrap(π + other+218) at 1°; boom L = cam+C + cam+94; 00191D40(cam, 15 + cam+8C + 11 + cam+5C + other+A4, 1.0); solve 4; chase at 0.8 |
| 0x15 | other+38 == 0: yaw = 00191120(π + other+C4, cam+44, 2°, π/4), and only when it changed is cam+44 stored and the boom (L = cam+C + cam+94) rebuilt; other+38 ≥ 0 (nonzero): approach wrap(π + other+C4 − π/4) at 0.3°; < 0: wrap(π/4 + π + other+C4); the height and close as 0x14 |
| 0xA, 0x19 | other+38 == 0: yaw = 00191120(other+C4, cam+44, 2°, π/4), stored with the boom (L = cam+C + cam+94) only when it changed; other+38 ≥ 0 (nonzero): approach wrap(π/4 + other+C4) at 0.3°; < 0: wrap(other+C4 − π/4) (the opposite sides to 0x15); then, outside area 4, 00192010(cam, cam+8C + cam+5C + other+B4, 15, 10); solve 3; chase at 0.8 |
| 8 | returns at once in area 0x13 room 1; else as 0x2C |
| 0x2C, 0x2D | cam+6C == 0: approach other+C4 at 2°, cam+6C = 1 once the yaw equals other+C4, boom L = cam+C, solve 3. Otherwise approach at 0.2°, boom, 00192010(cam, cam+8C + cam+5C + other+B4, 25, 20), solve 3. Both chase at 0.8 |
| 0x13 | approach other+C4 at 1°; cam+10/18 = D_008105E0.x/z − 40·sin/cos; 00192010(cam, 17 + cam+8C + cam+5C + other+A4, 25, 0.0); solve 4; chase x/z at 1.8 and y at 1.0 |
| 0x2F | cam+30 = 0x70003B50; the matrix 0x70003400 = identity rotated by cam+30 (001029C0, 00102C58), its translation = other+A0; cam+20 = M·(0, 15, 6, 1), cam+10 = M·(0, 19, −30, 1); solve 1 |
| 1, 0x26, 0x27 | `sign` = 1.0, then the tail |
| anything else | the tail with `sign` 0.0 |

**The tail.** With freelook ≠ 0 (camera state 3) it clears cam+1 and cam+3
and sets cam+44 = 001B1240(D_008105D0, D_008105E0, D_008105E8); nothing
else. With freelook 0 (the follow):

1. **slack** = D_00810690 − |cam+C| (kept at 0x70003A20).
2. **slack > 0: the pull-in.** The eye moves toward the target along the
   horizontal eye-to-target direction by `slack` (cam+10, cam+18);
   00191D40(cam, cam+8C + 11 + cam+5C + other+A4, 4.0); cam+3 = 0.
   Then the idle timer (step 5).
3. **Otherwise** lim = −20 when cam+64 == 0xC23B3333 (−46.8), else −10.
   When slack < lim, **the push-out**:
   - the orbit direction is (sin, 0, cos) of 0x70003B54 (normalised), the
     pull direction the horizontal eye-to-target (normalised), their dot
     at 0x70003A28, step = slack − lim (0x70003A24);
   - D_00810690 ≤ 1.0 clears bit 0 of cam+5A and of cam+7;
   - otherwise `sign` becomes −1.0 when: `sign` is 1.0 and (cam+7 & 0x1F,
     or cam+5A & 1, or D_00810698 ≥ 23.3); or `sign` is not 1.0, the dot
     is ≥ 0 and cam+7 & 0x1F;
   - with `sign` not −1.0: when D_0081069C > 8.6 or D_00810698 > 23.3,
     the eye moves along the pull direction by `step` unless bit 0 of
     cam+5A or cam+7 is set. Otherwise the eye **orbits**: when cam+3 is 0
     the bearing 001B1240(cam+10, cam+20, cam+28) is stored at 0x70003A28
     and cam+3 = 2 when cam+90 − bearing ≤ 0, else 1. The word at
     0x70003A28 then moves by π·(0.3·step)/180 (plus for cam+3 == 1, minus
     otherwise) and the eye moves by `step` along (sin, 0, cos) of it.
     When cam+3 was already set, 0x70003A28 still holds the dot product
     stored above, and that is what moves: the original uses it as the
     angle (the oracle confirms the translation does the same; this is
     not a port choice).
4. **The height.** slack is recomputed from cam+C. When slack < lim the
   drop is 0.5·(slack − lim) for lim −20, else slack − lim clamped at
   −10; the goal is 11 + cam+8C + other+A4 + (cam+5C − drop). Otherwise
   the goal is 11 + cam+8C + cam+5C + other+A4. 00191D40(cam, goal, 4.0).
   In area 0 with D_00810702 5 or 6, cam+18 is held at ≥ −1449.
5. **The idle timer (00193448).** When cam+7 & 9, or other+230 (re-read)
   is neither 1 nor 2, the halfword cam+8 = 0. Otherwise cam+8 counts up;
   at 0x1E1 (481 frames) d = wrap(other+C4 − cam+44), and when |d| > 3° and
   the side's blocking bit is clear (cam+7 & 4 for d < 0, & 2 for d > 0)
   the auto re-orbit is armed: cam+48 = other+C4, cam+4C =
   |D_0081069C|, cam+1 = 2 (00195130 then runs 00193D90), cam+3 = 0
   (d < 0) or 1, cam+40 = max(0.022222·|d|, 0.2°). cam+8 = 0 either way.
6. **Close:** 0018D7B0(cam, 0), then cam+44 = 001B1240(D_008105D0,
   D_008105E0, D_008105E8).

### The leaves

- **0018C6A0(src, dst, max)**, per axis x then z: d = src − dst; |d| ≤ 1
  moves dst by d/4 and sets the axis bit (1 for x, 2 for z) of the return
  value; else dst moves by sign(d)·min(|d|/6, max).
- **0018C4B0(v, y, max):** the same on v.y with d/4 inside 1.0 (returns
  4) and min(|d|/8, max) outside (returns 0).
- **00191D40(cam, y, rate):** goal = min(y + cam+98, cam+54); d = goal −
  cam+14. Up (d > 0) unless cam+7 & 0x80; down unless cam+7 & 0x40 or
  cam+5A & 1; by d/5 inside 1.0, else by min(|d|/10, rate). Then in area
  0x10 room 1 camera 2/4/6 with cam+50 > 100: cam+14 ≤ cam+50 + 7.5; in
  area 3 room 1 with cam+14 > 250 and cam+10 < 356: cam+14 ≤ cam+54 − 2.
- **00192010(cam, y, up, down):** d = y + cam+98 − cam+14; it moves only
  when |d| exceeds the dead band of its direction (`down` for d > 0,
  `up` otherwise) and the blocking bit is clear (0x80 / 0x40 of cam+7);
  by d/5 inside 1.0, else by min(|d|/10, 3.0).
- **00191120(goal, current, rate, limit):** d = wrap(goal − current);
  |d| < limit returns wrap(current); |d| within rate returns goal;
  otherwise wrap(current ± rate).
- **The SDK vector leaves** are VU0 macro code on 16-byte vectors: subtract
  and add (all four lanes), normalise (xyz scaled by 1/sqrt of the xyz dot,
  w = 0), scale (xyz by s, w kept), dot (xyz), matrix × vector (four
  lanes), a 16-byte copy and a 12-byte copy.

### Where the readable C was not enough

001921D0 is NEARMISS C (96.3%); 00191390, 00191120, 00192010, 0018C6A0,
0018C4B0 and the SDK leaves are asm-word files. Every routine was
translated from the instructions (`build/asm` of the decomp); the oracle
executes the instructions, never the C. Two points a reader of the C
could get wrong, both checked by the oracle:

- the cam+64 tests compare against the patterns the instructions build
  (0xC1F99999 in 00191390, 0xC23B3333 in 001921D0), not a decimal literal;
- the idle timer re-reads other+230 after the tail's calls (the oracle's
  scripted sine/cosine/heading calls change it behind the routine's back,
  which is how the `state == 2` outcome of that test is reached at all).

## 2. The translation (`src/game/em_camera_follow_original.c/.h`)

- **Records.** `EmCameraFollowRecord` is the raw 0xD0-byte block
  (0x008101E0); the player is the raw `EmPlayerLiveActor`. Both are read
  and written by original offset, on raw bit patterns.
- **Globals** (`EmCameraFollowGlobals`) are pointers into the binder's
  canonical storage: eye D_008105D0 and target D_008105E0 (vec4, written),
  D_00810690 / D_00810698 / D_0081069C and D_00810700..702 (read only).
- **Scratch** (`EmCameraFollowScratch`): 0x700038A0, 38B0, 38C0, 3910,
  3A20..3A2C, 3B50 (read only), 3400 (matrix) and 3600. The binder owns
  one instance and must hand the same one to the solvers 0018DD20 /
  0018F870 / 0018D910, whose originals use 0x700038A0.. too.
- **Workers** (`EmCameraFollowWorkers`, 13): 001B12B0 approach, 001B1470
  wrap, 001B1240 heading, 0011E2A8 sin, 0011DE90 cos, 00230000 tether,
  0018DD20 solve, 0018F870 solve_aim, 0018D910 bounds, 0019A910 segment
  (returning its v0, the record's +0x1A halfword and the point's y),
  0019B7D0 ground, 001029C0 identity and 00102C58 euler.
- **Fail-stop.** `em_camera_follow_001921D0`, `_0018D7B0` and `_0018D330`
  check the records, every global pointer, the scratch and all 13 workers,
  and return −1 before their first write when anything is missing. A
  worker returning < 0 stops the routine at once (−1), leaving the writes
  made before the call, as the original order leaves them. 00191120 needs
  only `wrap`; 00191D40 only the three area pointers; the other leaves
  refuse only NULL arguments.
- **Arithmetic.** Every COP1 and VU0 macro operation goes through
  `em_ee_float.h` (docs/EE_FLOAT_MODEL.md); the VU forms used are the
  measured ones, and an unmeasured form would fault (−1).

## 3. Verification

### Unit oracle (`tools/test_camera_follow_original_reference.py`)

`CameraEE` is the fall test's `FallEE` (every COP1 and VU0 macro op through
`tools/ee_float_model.py`) plus two MMI forms (PEXTLW, PEXTUW) that other
callees of the camera frame execute; the shared files are unchanged.

- **The original runs unmodified**: all 18 routines of the table above.
  The 13 worker callees are hooked with a per-call scripted effect
  (return value, f0, writes into the camera block, the eye global,
  0x700038A0 or the player state word, the segment hit data, the matrix
  outputs); the native workers replay the same script.
- **Compared after every case:** the whole camera block, both player
  records (the a1 record and D_008102B0 differ in 30% of the cases), all
  globals, every scratch word, the return value (v0 or f0) and the full
  worker call sequence with arguments (vectors by value; the record
  pointers each worker receives are checked).
- **No unchecked write:** every store the original instructions make
  (outside the stack) must fall inside the compared set.
- **Callee set:** the hooked set equals the jal targets of the 18
  routines (29 targets, all hooked or translated); no jalr.
- **Branches:** each of the 142 conditional branches in the 18 routines
  is taken both ways (asserted); every dispatch state of 001921D0 and
  every 0018D7B0 style 0..7 runs.
- **Fault cut:** in one case of four (the worker-calling entries) a worker
  fails at a random call; the native returns −1 with exactly the calls up
  to it.
- **Refusals:** each of the 13 workers, the 5 world pointers and the 8
  global pointers missing, on 001921D0 / 0018D7B0 / 0018D330 (−1, no call,
  no write), plus 00191120 without `wrap` and 00191D40 without each area
  pointer (82 checks).
- **Mutants killed:** cam+64 pattern 0xC23B3334; `<` for the 1.0 chase
  bound; the pull-in's cam+3 clear dropped; the idle limit 0x1E0; the
  cam+40 store dropped; the second 0018D330 ray from p+B4. The one
  survivor (`≤` for the −10 clamp) is equivalent: both paths store −10.

### Captured check (default run)

The original camera frame 0018B9C0 runs once over every captured image:
the startup-reference state 04 (`playable_ee.bin` + its scratchpad) and
the source snapshot of every route beat (12 distinct), twice: all
original, and with the nine routines hooked by the native module whose
workers run the original callees in the same EE (`WorldBinding`: records,
globals and scratch synced in and out around every call). The whole 32 MB
RAM and the scratchpad must be identical, and so must the set of routines
reached.

### World mode (`EM_TEST_WORLD=1`, minutes)

Each beat is replayed from its source snapshot with its recorded pad
input: per frame the original player stage (as
`test_player_slide_reference.RouteReplay` drives it, camera globals from
the previous trace row), then the camera frame 0018B9C0 twice from the
same RAM and scratchpad, all original and translated; the two must be
identical (whole RAM + scratchpad, every frame), and the original result
carries on. The run also reports how far the replayed actual eye is from
the trace row's eye wherever the recorded camera was in follow mode
(cam+4 == 0), which shows the replay follows the recorded play.

Beats: 05_boxes, 06_hill_slide and 12_crevice_jump whole, and
10_cage_roof_roger to f400 (Roger's scenes take the camera after that
and the replay runs no scripts). `EM_WORLD_BEATS` / `EM_WORLD_FRAMES`
narrow a run.

### Sanitizer fixture (`tests/camera_follow_original_test.c`)

ASan/UBSan over the translation with stub workers (no claim about the
callees): every state both freelook values, every style, the leaves'
NULL refusals, 26 refusals and a fail-stop cut at every worker call of
every state.

### Results (2026-09-23)

- **Quick** (default): 4,000 of 60,000 cases (follow 1,823, solve 426,
  prepass 378, heights 298, yaw 297, height2 260, height 240, chase_xz 143,
  chase_y 135), 13,375 worker calls identical, all 142 branches both ways,
  717 fault cuts, 82 refusals, and the 13 captured images identical
  (native calls: follow 13, heights 13, chase_xz 14, chase_y 14). 9.8 s
  wall on a heavily loaded machine (load average about 230); about 5 s
  idle.
- **Full** (`EM_TEST_FULL=1`): 60,000 cases (follow 27,986), 201,775
  worker calls identical, all 142 branches both ways, 11,022 fault cuts,
  82 refusals, captured check as above. 132 s.
- **World** (`EM_TEST_WORLD=1`, 23 min under that load): every frame
  identical, whole RAM + scratchpad.

  | beat | frames | 001921D0 / 00191390 calls | +230 states in follow (frames) | replayed eye vs recorded eye (follow rows) |
  |---|---|---|---|---|
  | 05_boxes | 677 | 677 / 677 | 1:200, 3:319, 5:158 | median 1.4e-5, max 3.5e-5 (677 rows) |
  | 06_hill_slide | 212 | 212 / 212 | 1:85, 3:61, 0x13:66 | median 4.7e-6, max 8.0e-5 (206 rows) |
  | 12_crevice_jump | 345 | 345 / 345 | 1:129, 3:155, 5:61 | median 4.5e-6, max 6.6e-5 (337 rows) |
  | 10_cage_roof_roger (to f400) | 412 | 412 / 412 | 1:147, 3:120, 5:12, 6:59, 8:74 | median 3.9e-6, max 3.4e-5 (401 rows) |

  The trace prints the eye to 5 decimals, so the replayed follow camera
  reproduces the recorded one within printing precision on every row.
  The route reaches the tail (codes 1, 3, 5), the 0x13 pose (the slide
  beat) and the 6 / 8 poses (the cage roof); it never produces 2 / 4 / 0xF
  in these beats, so the tether 00230000 is not reached there.
- **Sanitizer fixture:** 56 runs, 26 refusals, 131 fail-stop cuts, clean.

## 4. Binding (coordinator)

Nothing is wired. The module can be bound only when its missing workers
exist (below); until then the live camera stays em_camera.c's (the
"follow camera after a release is the port's", FIRST_LEVEL_AUDIT H5 and
WP-16).

### Where it plugs in

- **The camera frame 0018B9C0** (scene worker `w_0018B9C0` →
  `em_camera_0018B9C0`, em_scene_bindings.c): in its cam+4 == 0 arm the
  first call is 00191390 → `em_camera_follow_00191390(cam, player)`,
  replacing em_camera.c's `camera_prestep_00191390`.
- **The area specials** (`EmCamSpecialsWorkers`,
  em_camera_area11_specials.h), one adapter each, context = the
  `EmCameraFollowWorld *`:
  - `w_001921D0(ctx, cam, player, mode)` → `em_camera_follow_001921D0(world,
    player, mode)` (assert `cam == world->cam->bytes`);
  - `w_0018D7B0(ctx, cam, style)` → `em_camera_follow_0018D7B0(world,
    style, NULL)`;
  - `w_0018C6A0(ctx, from, to, rate, result)` →
    `em_camera_follow_0018C6A0(from, to, rate, result)`; `w_0018C4B0`
    likewise;
  - `w_00191D40(ctx, cam, want, rate)` →
    `em_camera_follow_00191D40(world->cam, world->globals, want, rate)`;
  - `w_00192010(ctx, cam, f12, f13, f14)` →
    `em_camera_follow_00192010(world->cam, f12, f13, f14)`.
- **Other callers of 0018D7B0** (the scripts' camera hooks, 001B7B30,
  em_area_script) can use the same adapter once bound.

### Data

- `world.cam` = the camera block storage behind EM_SCENE_D_008101E0 (the
  same bytes em_camera_area11_specials gets as `cam`);
  `world.player` = the live player record.
- `globals.eye` / `globals.target` = the specials world's `d8105D0` /
  `d8105E0` storage; `d690` / `d698` / `d69C` = the words the commit
  0018C0D0 writes; `area` / `d701` / `d702` = em_scene_state's
  `d810700..702`.
- `scratch.s3B50` must hold 0x70003B50..5C when 001921D0 runs (read only:
  state 0x2F copies it to cam+30, the push-out reads 0x70003B54). Its
  canonical storage is em_scene_state's `spad3B40[4..7]`; copy it in
  before the call (the routines never write it). The other scratch words
  are written before they are read within a call, except those the
  solvers share (above).

### Workers

| slot | original | today |
|---|---|---|
| approach | 001B12B0 | `em_script_host_001B12B0(NULL, ...)` (no data) |
| wrap | 001B1470 | `em_player_001B1470` (em_player_stage_workers.c) |
| heading | 001B1240 | `em_script_host_001B1240(h, obj, x, z, &out)` |
| sine / cosine | 0011E2A8 / 0011DE90 | `em_sdk_math_original_0011E2A8` / `_0011DE90` (raw bits via memcpy; a fault is a fault) |
| segment | 0019A910 | `em_coll_segment_0019A910(seg, from, to, mask)`; `record_1A` = `em_coll_probe_record_node(grid, state)`, `point_y` = the bits of `state->point[1]` (0x700031B4) |
| identity | 001029C0 | `em_owner_services_identity_001029C0` |
| euler | 00102C58 | `em_pose_host_00102C58(h, out, in, angles)` |
| tether | 00230000 | **missing** (no translation) |
| solve | 0018DD20 | **missing** |
| solve_aim | 0018F870 | **missing** |
| bounds | 0018D910 | **missing** (em_camera_probe's `bounds11` is a float model of one branch, not a translation) |
| ground | 0019B7D0 | **missing** |

The five missing originals are the next lanes; until they exist, binding
this module would fault on the first frame (by design). On the route
every follow frame reaches 0018DD20 (solve 0 at the close) and both
prepass queries 0019B7D0 / 0019A910, so those come first; 0018F870,
0018D910 and 00230000 are needed for the bound check (and for the aim,
fixed-camera and locomotion states), not by the replayed beats.

## 5. Limits

- The oracle's scripted callees prove the routines' own logic for any
  callee behaviour; the captured and world modes prove it with the real
  callees on real AREA11 states. The world replay drives only the player
  stage and the camera frame, so frames where a script held the camera in
  the recording are replayed as follow frames (still compared original
  against translated, but not against the recording).
- Player states the replayed beats never produce in follow mode (2, 4,
  0xF, 7, 9, 0xA, 0x14, 0x15, 0x18, 0x19, 0x2C, 0x2D, 0x2F, 0x26, 0x27)
  and freelook 1 are proven by the unit oracle only; the beats cover 1, 3,
  5, 6, 8 and 0x13.
- Nothing here translates the five missing workers.
