# Camera leftovers: the frame, the dispatch, the walking placement, the solvers

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "camera-leftovers" (census lanes L13-camera-follow, L14-camera-solver-dd20,
L15-camera-actions, L16-camera-area11-walk). This lane translates everything
the walking camera still lacked before it can be bound live:
em_camera_follow_original (docs/CAMERA_FOLLOW_ORIGINAL.md) and
em_camera_area11_specials (docs/CAMERA_AREA11_SPECIALS.md) listed their
missing workers, and the census listed the missing, unverified and stand-in
rows of the four lanes.

The module is **bound live** since census L13..L16 (em_camera_live.c,
docs/CAMERA_LIVE.md; section 4 below): the table's "after" column is the
status this lane left, and every row of it is now `live` in
docs/FIRST_LEVEL_CENSUS.md.

| original | what it is | source form read | census before | after |
|---|---|---|---|---|
| 0018B9C0 | the camera frame | NEARMISS C + the instructions | verified-unbound (mislabel: only executed by the follow test, never translated) | verified-unbound |
| 0018BC20 | the camera action dispatch | NEARMISS C + the instructions | stand-in (em_camera.c `camera_mode_dispatch`) | verified-unbound |
| 00190F20 | the area-transition trigger | byte-matched C | missing | verified-unbound |
| 0018C0C0 | the target copy | byte-matched C | unverified (em_camera.c) | verified-unbound |
| 001914A0 | camera action 8, the mode-8 settle | byte-matched C | stand-in (em_camera.c mode-8 settle, CAM-18/19) | verified-unbound |
| 00191580 | action 8's per-frame body | byte-matched C | stand-in (same) | verified-unbound |
| 0018C5A0 | action 8's height chase | NEARMISS C + the instructions | missing | verified-unbound |
| 001916C0 | the per-state target placement | NEARMISS C + the instructions | verified-unbound (mislabel: the specials test hooks it) | verified-unbound |
| 00191000 | the L1 orient-behind request | byte-matched C | verified-unbound (mislabel: hooked by the specials test) | verified-unbound |
| 0018DD20 | the desired-eye solver | NEARMISS C + the instructions | verified-unbound (mislabel: hooked by the follow test; em_camera.c holds an unverified duplicate) | verified-unbound |
| 0018CE60 | the floor/ceiling bounds under a point | NEARMISS C + the instructions | unverified (em_game.c `cam_bounds_settle_0018CE60`) | verified-unbound |
| 0018D910 | the fixed-camera bounds (style 5) | NEARMISS C + the instructions | verified-unbound (critic: partial, em_camera_probe.c AREA11 branch only) | verified-unbound (whole routine) |
| 0015CBA0 | the player state byte -> action code map | byte-matched C | stand-in (em_camera.c constant height row) | verified-unbound |
| 0018F870 | the aim solver (styles 2 / 6) | NEARMISS C + the instructions | not on the route (a follow-module worker) | verified-unbound |
| 00230000 | the locomotion tether (player codes 2 / 4 / 0xF) | byte-matched C | not on the route (a follow-module worker) | verified-unbound |
| 0022FCA0 | the tether's boom pull-in / push-out / orbit | NEARMISS C + the instructions | not on the route (a specials-module worker) | verified-unbound |
| 00194D10 | the tether's region-height test | NEARMISS C + the instructions | not on the route | verified-unbound |
| 00193D90 | the idle auto orbit (camera state 2) | asm-word file (the instructions) | not on the route (a specials-module worker) | verified-unbound |

"Not on the route" rows are not in FIRST_LEVEL_CENSUS.md: the recorded route
never ran them. They are translated because the walking camera reaches them
in AREA11 on ordinary play (the tether when the player's +236 latch is set,
the idle orbit after 481 still frames, the aim solver under the aim styles),
and a bound camera would otherwise fault there.

The census rows of these lanes that already had a verified translation need
only binding notes (section 4): 0018C4B0, 0018C6A0, 0018D330, 0018D7B0,
00191390, 00191D40, 00192010, 001921D0 (em_camera_follow_original), 00191210,
00193EB0, 00195130 (em_camera_area11_specials), 0019B7D0
(em_coll_list_passes_walkers).

Offsets below are on the 0xD0-byte camera block at 0x008101E0 ("cam+X") and
the 0x320-byte player record ("p+X"). "eye" is D_008105D0 and "target"
D_008105E0 (vec4 each). S(xxxx) is the scratchpad word 0x7000xxxx. Floats are
compared with the EE compare rules; "<=" is the EE's C.LE.S, and so on.

## 1. What the original does

### 0018B9C0(cam): the camera frame

It decrements D_008106EF when nonzero, ORs the frame's input bits
(0x700031F0) into cam+8B, then dispatches on cam+0:

- **State 1** (every frame after the first): on cam+4: 0 runs 00191390(cam,
  player), 0018BC20(cam, player) and the commit 0018C0D0(cam, 1); 3 runs
  0022EEF0(cam, 1) and 0018C0D0(cam, 0); any other value runs
  0018C0D0(cam, 1). The player is D_008102B0.
- **State 0** (one frame): cam+0 = 1; D_008105F0 = (0, -1, 0, 1); cam+C =
  cam+64; clears cam+4, cam+7, cam+90, the halfwords cam+5A and cam+A0 and the
  bytes cam+6C, cam+6D; cam+54 = 1000, cam+50 = -200, cam+5C = 2, cam+8C = 6;
  cam+44 = 001B1240(eye, target.x, target.z); 0018CE60(cam, p+B0, 5). Then,
  unless cam+5 is 1 or cam+6 is 0xA / 0xF / 0xD: cam+6 = 8 (0 in area 0x12
  with D_00810702 = 0), 0018C0C0, 0018D7B0(cam, 1), 0018C0D0(cam, 1) and the
  heading again.
- Any other state does nothing after the first two steps.

### 0018BC20(cam, e): the action dispatch

It runs 00190F20(cam, e) first, then reads the mode cam+5 and the action cam+6
(after the trigger, which may set cam+6 = 7). Mode 0 dispatches through the
16-entry table at 0x26D950: 0 and out of range 00195130, 1 00197D20, 2
00198650, 3 001936E0, 5 0018CA90 then cam+6 = 7, 6 clears cam+6 when
D_0028A9A0 is 0, 8 001914A0 then 001DD980(eye, target), 9 00198CE0, 10
00198D90 then 001D2830(3, 1), 11 00198F10, 12 001963A0, 13 00196CE0, 14
00198AF0, 15 00197390; 4 and 7 do nothing. Mode 1 (table 0x26D910) has the
same handlers except 9, 11, 14, 0 and out of range, which take the
locomotion path:

- cam+1 = 0: cam+1 = 1, cam+2 = 0, the halfword cam+8 = 0, 001B0300(), then
  as 1; cam+1 = 1: continue; any other value: return.
- action 0xB with e+230 not 0x12 clears the action.
- on e+230: 5 chases cam+20 toward e+B0 (0018C6A0 at 0.8) and its height to
  e+B4 + cam+8C (0018C4B0 at 1.0); 6, 7, 8, 9, 0x2C, 0x2D nothing; 0x11 sets
  cam+6 = 10 and cam+1 = 0; every other code (0xA included) chases toward
  e+A0 at 0.8 and to 15 + e+A4 at 1.0.
- then 0018C0C0, 00193EB0(cam, e, 0) and cam+44 = 001B1240(eye, target.x,
  target.z).

Any mode other than 0 and 1 returns after the trigger.

### 00190F20(cam, e): the area trigger

Only when D_008106B8 is 0: in area 0x12, when e+A0 <= 285,
001B0C60(0xE, 0, 1) and cam+6 = 7; in area 0xE, when 0x70003B8D is 0 and
001B1EA0(0, e+A0, 0x24A4B0, 4) is nonzero, 001B0C60(0x12, 0, 0) and cam+6 = 7.

### 0018C0C0(cam)

target = cam+20 (16 bytes).

### 001916C0(cam, e, mode): the target placement

The jump table at 0x26D990 (48 entries on e+230; 0x30 and above take the
default) sorts the player codes into seven groups. Every group first moves
cam+20: mode 2 copies x/z from the group's source point, any other mode
chases toward it with 0018C6A0 (rate 2.0; the default group 1.5). The source
is e+A0, except the default group's e+B0. Then the height y:

| group | codes | y |
|---|---|---|
| A (and flag for 0, 1) | 0, 1, 3, 0xE, 0x14, 0x15, 0x16 | 11 + ((e+A4 + cam+8C) + 0.3 * S3A20) as one multiply-add |
| B | 0xA, 0x19 | (11 + e+A4) + cam+8C |
| C | 0x13 | e+A4 + cam+8C |
| D (and flag for 2) | 2, 4, 0xF | 11 + (e+A4 + cam+8C) |
| E | 8, 0xC, 0xD, 0x26, 0x27, 0x29, 0x2A, 0x2F | e+B4 + cam+8C |
| F | 0x1D..0x24 | (11 + e+A4) + cam+8C |
| default | the rest | e+B4 + cam+8C |

Group A first sets S3A20: mode 2 stores S38B0 = cam+20 - cam+10 and S3A20 =
sqrt(x*x + z*z) (0011E748; the sum is one multiply-add); otherwise S3A20 =
D_00810690 - |cam+C|. Then lim = -20 when cam+64 is the pattern 0xC23B3333,
else -10; when S3A20 < lim it becomes lim + (lim - S3A20), capped at -7 from
above.

Mode 0 chases cam+24 toward y (0018C4B0 at 4.0); mode 2 stores it; any other
mode leaves it. The tail: the halfword cam+A0 counts down when nonzero; mode
2 returns; when cam+A0 is 0, 0018C0C0 and return; otherwise target chases
cam+20 (0018C6A0 at 1.0), d = cam+24 - target.y, cam+A0 = 0 when |d| <= 0.15,
and target.y chases cam+24 at |d| / 20 (flag set) or |d| / 4.

### 00191000(cam, e): the orient-behind request

When D_00810E74 & 0x70003B80 is nonzero: cam+48 = wrap(pi + e+C4) when the
byte e+1F0 is 6, else e+C4. When |wrap(cam+48 - cam+44)| > 3 degrees
(0x3D567750): cam+6 = 3, cam+1 = 0, cam+4C = |D_0081069C|, raised to 7 when
below it, else lowered to |cam+64| when above that; returns 1. Otherwise 0.

### 00193D90(cam, e): the idle auto orbit

001916C0(cam, e, 1) (mode 1: the source chase, no height), then cam+44 =
001B12B0(cam+48, cam+44, 0.2 degree), cam+10 = cam+20 - cam+4C * sin(cam+44),
cam+18 = cam+28 - cam+4C * cos(cam+44). It hands back (cam+1 = 1, cam+3 = 0)
when cam+44 reached cam+48, when e+230 is neither 1 nor 2, and when cam+7 has
a blocking bit for the orbit's side (0xD with cam+3 = 0, else 0xB).

### 0022FCA0(cam): the boom

slack = D_00810690 - |cam+C| (S3A20). slack > 0: S38A0 = the horizontal unit
vector from cam+10 toward cam+20; cam+10 / cam+18 move along it by slack;
cam+3 = 0. Otherwise thresh = -20 (cam+64 = 0xC23B3333) or -10; slack not
below thresh: cam+3 = 0. Below it: S3A24 = slack - thresh; D_0081069C > 8.6
pulls along the same vector by S3A24; else the orbit: when cam+3 is 0 the
bearing S3A28 = 001B1240(cam+10, cam+20, cam+28) and cam+3 = 2 when cam+90 -
S3A28 <= 0, else 1; S3A28 moves by pi * (0.3 * S3A24) / 180 (added for cam+3
= 1, subtracted otherwise) and the eye moves by S3A24 along (sin, 0, cos) of
it.

### 00230000(cam, e): the tether, and 00194D10

On D_00810700: area 0xB runs 0022FCA0, then y = (6 when 00194D10(cam, e, 1)
else cam+8C) + (11 + (cam+5C + e+A4)), 00191D40(cam, y, 4.0), 00191000 for
e+230 = 2 or 0xF, and 0018D7B0(cam, 0). Area 0 with e+A4 < -83 is a fixed
seat: cam+98 = 0, cam+10 = 120, cam+18 = -1590, 00191D40(cam, -67.5, 4.0),
0018D7B0(cam, 5), and the eye chases cam+10 (0018C6A0 and 0018C4B0 at 4.0).
Every other case is area 0xB's without the region test. All end with cam+44 =
001B1240(eye, target.x, target.z).

00194D10(cam, e, i) is 1 when 001B1EA0(0, e+A0, 0x24A5F0 + 0x40 * i, 4) is
nonzero and |e+A4 - the word at 0x24A5F4 + 0x40 * i| < 4.

### 0018C5A0(v, y, max), 00191580(cam, e), 001914A0(cam, e): camera action 8

0018C5A0 chases v+4 toward y + D_00810278 (that global is cam+98): d = goal -
v+4; |d| <= 1 adds d / 4 and returns 4; otherwise the step is |d| / 8, set to
max when max <= step, negated for d < 0, added, and returns 0.

00191580 runs 001916C0(cam, e, 0), S3A20 = D_0081069C - |cam+C|, lo = -20 or
-10 (cam+64 test as above). S3A20 < lo: S3A24 = 0.5 * (S3A20 - lo), raised to
-1.5 when below it, y = 11 + (cam+8C + (e+A4 + (cam+5C - S3A24))); otherwise
y = 11 + (cam+8C + (cam+5C + e+A4)). Then 0018C5A0(cam+10, y, 4.0).

001914A0 runs 00191580, the eye's height chase to cam+14 and its x/z chase to
cam+10 (0018C4B0 / 0018C6A0 at 0.4), and clears cam+6 when D_0028A9A0 is 0
and the byte e+4 is not 5.

### 0018CE60(cam, v, style) and 0018D910(cam, e, mask): the bounds

0018CE60 copies the vector at v to S39C0 and probes 200 down and 200 up from
it (0019A910, mask 7 for style 2, else 6). The floor S3A38: a hit takes the
hit point's y, raised by 6 or 17 (cam+5C == 1.0 or not; 2 for style 2) when
the record has 0x5000 or its normal's up component (dot with (0, 1, 0)) is
above 0.17; a miss takes cam+50 - 200. The ceiling S3A3C: a hit takes the
point's y, lowered by 1 when the record has 0x8800 or its normal faces down
(dot with (0, -1, 0) above 0.17); a miss takes 200 + v[1], re-read from v. The
floor is capped at the ceiling - 3; cam+50 / cam+54 take them; for styles
other than 5 cam+14 is clamped into [cam+50, cam+54].

0018D910 builds its probe point 1 unit from cam+10 toward (e+A0 with y + 11),
probes 200 down (a hit on a 0x7000 record: y + 6 or 17 by cam+5C) and 200 up
(a 0x8800 record: y - 1). When the up probe finds nothing usable and the area
is 0x12 it probes up from e+B0: a hit whose normal faces down more than 0.2
takes y - 1, another hit takes cam+14 + 200, and **a miss leaves S3A3C as it
was** (the readable C writes cam+14 + 200 there; the instructions do not). In
other areas the ceiling is cam+14 + 200. Then the same cap and cam+50 / 54.

### 0018DD20(cam, e, style, mask): the desired-eye solver

It returns a flag byte (0018D7B0 stores it at cam+7). In order:

1. **The first probe** from the target cam+20 to 1.5 units beyond the desired
   eye cam+10. A hit (flag 1): cam+58 = the record's +1A halfword, S38C0 = the
   hit point, S3950 its copy, S3960 = the horizontal unit probe direction,
   S38E0 = the record normal (the horizontal part first: a dot with S3960
   below 0.707 marks the surface steep), cam+90 = wrap(atan2 of the normal's
   x/z). For styles other than 3, when D_00810690 - |cam+C| <= 0 a second
   probe from (target x, 13 or 17.5 over e+A4, target z) toward the same end
   drops the first hit when it misses, or when it hits a 0x8800 record
   within 1 unit of the end.
2. **The first probe's reaction** (when kept): style 3 pushes the eye to the
   hit point plus half the reversed probe direction, adjusting cam+50 (0x8800:
   flag 8) or cam+54 (other 0xD800 surfaces: flag 0x10), or only x/z for
   other surfaces; other styles: 0x8800 surfaces drop the eye 1 below the hit
   and widen cam+50 / cam+54 (flag 8); 0x2000 surfaces probe back and, when
   the wall faces the probe (dot with S38E0 above -0.3), slide 4 units out
   along the wall's normal; otherwise x/z go to the hit plus half the reversed
   direction.
3. **The wall slide** (when the first probe was dropped or steep): two side
   probes 5.5 units to either side of the view (A and B), each rejected when
   its surface faces the first probe's (the tests on S3960 / S38E0 and a
   re-probe from the first hit's x/z at the eye height), each kept one moving
   a candidate 0.1 along the slide (flags 2 / 0xA / 3 on side A, 4 / 8 / 1 on
   side B). Both kept: the eye goes to the midpoint of the two hit points;
   one: that side's candidate (or the midpoint when the other side still
   hit).
4. **The clamps**: area 0x12 holds cam+18 in [169.5, 230.6]; a kept first
   probe bounds cam+14 by the hit point's y (below it for 0x8800 surfaces,
   above it otherwise, unless 0x2000); cam+14 into [cam+50, cam+54]; the
   floor and ceiling over the eye (as 0018D910 with the probe point stretched
   by 1.5 first, 00102900); area 0x15 with cam+18 > 260 sets cam+50 = 70;
   cam+14 is clamped once more, flagging 0x40 (floor) and 0x80 (ceiling).

### 0018F870(cam, e, style, mask): the aim solver

The same structure over the actor's aim point e+B0 instead of the target:
the first probe from e+B0 beyond the eye (steep below 0.99 against the view
direction); a kept probe on a non-0xD800 surface re-bounds through 0018CE60
when 0019A910 returned 4, else clamps cam+14. Style 6 stops there. A kept,
not steep probe takes the **side push**: from cam+10 + the surface normal, a
probe 5.5 units to one side, then the other when the first did not act; a
0x2000 surface whose normal is less than 0.9 aligned with the first one moves
the eye to the hit point minus (then plus) the side vector (flags 2 / 4). A
steep or missed probe takes the wall slide of 0018DD20 (without the
re-probe, and with side offsets from the eye alone). Then the area-0x12 depth
clamp and the first-hit height bound as in 0018DD20; when any of the flags
0x1F is set, a probe from e+B0 to the eye moves the eye to its hit; last the
floor and ceiling over the eye (no 1.5 stretch, a floor lift of 2). It
returns the flags.

### 0015CBA0(p): the state map

The jump table at 0x26D4E0 (71 entries) maps the byte p+1F0 to the action
code p+230, using p+1F1, p+236 and p+0D for the refined codes (state 51
leaves p+230 as it is; 66 and out of range store 0). It is byte-matched C;
the translation is its switch.

## 2. The translation (`src/game/em_camera_leftovers*.c/.h`)

- `em_camera_leftovers.h`: the types and entry points; `em_camera_leftovers.c`:
  0018B9C0, 0018BC20, 00190F20, 0018C0C0, 001914A0, 00191580, 0018C5A0,
  001916C0, 00191000, 00193D90, 0022FCA0, 00230000, 00194D10, 0015CBA0;
  `em_camera_leftovers_solver.c`: 0018DD20, 0018F870, 0018CE60, 0018D910;
  `em_camera_leftovers_internal.h`: the float operations, record access and
  the SDK vector leaves (001028D0, 001028B8, 00102760, 00103230, 00102900,
  00102738, 00102948, 001031E0) as their VU0 macro instructions.
- **Records.** The camera block is `EmCameraFollowRecord` (the same type the
  follow module uses); players are `EmPlayerLiveActor`. Everything is read
  and written by original offset on raw bit patterns.
- **World** (`EmCamLeftWorld`): the camera block, the player D_008102B0
  (0018B9C0's base), the globals, the scratch window, the hit words, the
  workers and `fault` (the first missing or failing callee's address).
- **Globals** (`EmCamLeftGlobals`): `follow` points at the follow module's
  `EmCameraFollowGlobals` (eye, target, D_00810690 / 98 / 9C, D_00810700..702:
  one storage for both modules), plus D_008105F0, D_008106EF, D_008106B8,
  D_00810E74, D_0028A9A0, 0x70003B80, 0x70003B8D, 0x700031F0 and the
  D_0024A5F0 region table (words from the user's ELF; bounds-checked).
- **Scratch** (`EmCamLeftScratch`): the words 0x700038A0..0x70003A3C, one slot
  per word (`em_camleft_spad`). It overlaps the follow module's
  `EmCameraFollowScratch`; `em_camleft_scratch_from_follow` / `_to_follow`
  copy the shared words (0x700038A0..CC, 0x70003910..1C, 0x70003A20..2C).
- **Hit** (`EmCamLeftHit`): what 0019A910 leaves for its callers: the point
  0x700031B0..BC (four words: the solvers copy the quad) and, through
  *0x700031D0, the record's +1A halfword and +24..+2C normal. The segment
  worker updates it after every call, as the scratchpad reads after it.
- **Workers** (`EmCamLeftWorkers`, 30): the SDK/math callees 001B1470 wrap,
  001B12B0 approach, 001B1240 heading, 0011E2A8 sin, 0011DE90 cos, 0011E620
  atan2, 0011E748 sqrt; 0019A910 segment, 001B1EA0 inside, 0018D7B0 the
  solve dispatch, 0018C0D0 commit, 0022EEF0, 001B0C60, the twelve action
  handlers of 0018BC20, 00193EB0, 001DD980, 001D2830 and 001B0300.
- **Direct calls** (translations of other modules, not workers): 0018C6A0,
  0018C4B0, 00191D40, 00191390 (em_camera_follow_original.c) and 0011DF78
  (em_sdk_math_original.c). The records' bytes are copied into word vectors
  around each call and written back.
- **Fail-stop.** Each entry point checks the world pointers, the globals and
  the workers its path can reach (a superset where the path depends on a
  callee's result) and returns -1 before its first write when one is missing;
  `fault` names it. A worker returning < 0 stops the routine at once (-1,
  `fault` = that callee), leaving the writes made before the call, as the
  original order leaves them. A worker pointer that is NULL at its call is
  treated the same way.
- **Arithmetic.** Every COP1 operation (add, sub, mul, div, neg, the
  multiply-adds, the compares) and every VU0 macro operation goes through
  `em_ee_float.h` (docs/EE_FLOAT_MODEL.md); an unmeasured VU form faults.

## 3. Verification (`tools/test_camera_leftovers_reference.py`)

The test executes the original instructions of all 18 routines (and the
leaves they reach) from the user's pinned ELF on the follow test's
`CameraEE` (the fall test's float model: every COP1 and VU0 macro op through
`tools/ee_float_model.py`) and compares em_camera_leftovers through a small
C shim built with the module.

- **Unit oracle.** Random camera blocks, players, globals, scratch and hit
  words, with every float field placed near the thresholds the routines test.
  Every worker callee (53 jal targets in all, each either hooked or
  translated; asserted) is hooked with a per-call scripted effect (return
  value, f0, writes into the camera block, the eye / target, the scratch or
  the player's +230; the segment hook leaves hit words chosen relative to the
  segment and to earlier surfaces). The native workers replay the same
  script. Compared after every case: every byte of the camera block, both
  player records, every global and every scratch word (the original may write
  nothing else: asserted), the return value and the full worker call
  sequence with arguments.
- **Branches.** Each of the 300 conditional branches in the 18 routines is
  taken both ways, except 8 outcomes that no input can produce, which are
  asserted never to occur:
  - 0018DD20 0x18E910 and 0x18E930 taken, 0x18EF00 taken, and 0018F870
    0x190214 / 0x190234 / 0x190650 taken: the wall slide runs only when the
    first probe was steep or dropped, so where these tests see "not steep"
    the first probe was dropped and S3A3C is 0 (or -1 after a side-B
    rejection);
  - 0x18F248 and 0x190998 not taken: flag 2 is set by every side-A slide, so
    "flag 4 without flag 2" never has a kept side-A hit.
- **Fault cuts.** In one case of four a worker fails at a random call; the
  native returns -1 with exactly the calls up to it and `fault` = that callee.
- **Refusals.** For a sample of cases, every worker, every global pointer and
  every world pointer missing in turn: a worker the full run called must be
  refused before any call or write; an unused one may be refused the same way
  or must leave the run unchanged.
- **0015CBA0** over its whole input domain: every p+1F0 byte x p+1F1 in {0,
  1, 2, 3} x p+236 in {0, 1} x p+0D in {0, 1, 2, 3} (8,192 inputs; the
  routine only compares those fields against these values).
- **Captured check.** The original camera frame 0018B9C0 runs over every
  captured image, all original and with the translations hooked in (workers
  bound to the original callees in the same EE, the native state synced in and
  out around every call); the whole 32 MB RAM and the scratchpad must be
  identical. Images: startup-reference state 04 and the 12 distinct route-beat
  sources (state 1, +4 = 0, action 0: the walking camera), the opening
  hand-off `handoff_ee.bin` (+4 = 0, action 8: the mode-8 settle; no
  scratchpad was captured with it, so a zeroed one stands in, which is valid
  because both runs start from the same bytes), the opening and Roger's
  encounter (+4 = 3) and the panel and status-hub scenes (+4 = 1).
- **World mode** (`EM_TEST_WORLD=1`): the beats 05_boxes, 06_hill_slide,
  12_crevice_jump and 10_cage_roof_roger (to f400) replayed from their
  sources with their recorded pad input (the player stage as the follow test
  drives it), the camera frame run twice per frame (original and translated,
  whole RAM + scratchpad compared). 0015CBA0 is checked inside the original
  player stage on every call (the translation run on a copy of the record
  must leave the same bytes).
- **Sanitizer fixture** (`tests/camera_leftovers_test.c`): ASan/UBSan over
  every entry with stub workers (no claim about the callees), a fail-stop cut
  at every worker call of every run, and missing world pointers.
- **Mutants.** 26 mutants of the translation were run against the quick
  test (section 3, results).

Commands (lane build dir via `EM_LANE`, default `build/b7-camera-leftovers`):

```
python3 tools/test_camera_leftovers_reference.py                  # quick
EM_TEST_FULL=1 python3 tools/test_camera_leftovers_reference.py   # full sweep
EM_TEST_WORLD=1 python3 tools/test_camera_leftovers_reference.py  # route replay
```

### Results (2026-09-23)

- **Quick** (default): 8,000 of 80,000 cases (solve 1,906, solve_aim 1,678,
  placement 529, frame 506, dispatch 493, boom 409, tether 391, bounds_d910
  363, bounds_ce60 334, orient 268, settle 200, settle_body 191, orbit 182,
  height_5a0 176, trigger 176, region 163, target_copy 35), 39,485 worker
  calls identical, all 300 conditional branches both ways (the 8 unreachable
  outcomes never seen), 1,532 fault cuts, 738 refusals over 60 cases,
  0015CBA0 over 8,192 inputs, and the 18 captured images identical (the
  original reached 0018B9C0 18 times, 0018BC20 / 00190F20 / 001916C0 14,
  0018DD20 / 00191000 / 0018C0C0 13, the action-8 chain once). 14 s wall
  under a load average near 150 (37 s of CPU over 8 workers); about 6 s on
  an idle machine.
- **Full** (`EM_TEST_FULL=1`): 80,000 cases, 392,803 worker calls identical,
  all 300 branches both ways, 15,584 fault cuts, 133,370 refusals over 11,429
  cases, the same captured check. 271 s.
- **World** (`EM_TEST_WORLD=1`): every frame identical (whole RAM + scratchpad),
  676 s under that load.

  | beat | frames | 0018DD20 / 001916C0 / 00191000 calls | 0015CBA0 calls identical (p+1F0 values seen) | replayed eye vs recorded eye (follow rows) |
  |---|---|---|---|---|
  | 05_boxes | 677 | 677 / 677 / 200 | 677 (0, 1, 5, 8) | median 1.4e-5, max 3.5e-5 (677 rows) |
  | 06_hill_slide | 212 | 212 / 212 / 85 | 212 (0, 1, 48) | median 4.7e-6, max 8.0e-5 (206 rows) |
  | 12_crevice_jump | 345 | 345 / 345 / 129 | 345 (0, 1, 2, 4, 5, 11, 12, 15) | median 4.5e-6, max 6.6e-5 (337 rows) |
  | 10_cage_roof_roger (to f400) | 412 | 412 / 412 / 147 | 412 (0, 1, 2, 4, 5, 11, 15, 21, 23) | median 3.9e-6, max 3.4e-5 (401 rows) |

  The trace prints the eye to 5 decimals: the replayed camera, with these
  translations in place, reproduces the recorded one within printing
  precision.
- **Sanitizer fixture:** 7,200 runs, 14,045 fail-stop cuts, 36,469 refusals,
  clean.
- **Mutants** (26, each a single change to the translation): 23 killed by
  the quick run, one (the height order of 001916C0's group D) killed by the
  full sweep only. Two survive: 0018C5A0's clamp written the C's way
  (equivalent: the step is always a positive normal number, so the two
  forms pick the same bits) and 0018DD20's steep threshold moved from 0.707
  to 0.70703125 (a boundary no random input lands on). Killed, among others:
  0018D910's and 0018DD20's area-0x12 miss read the C's way, 0018DD20's
  second wall pass subtracting instead of adding, 0018F870's floor lift 6
  instead of 2 and the 1.5 stretch applied to it, 0018F870's side push
  adding on its first side, its re-probe enabled, its 0x1F test narrowed,
  00191580's add order, 0022FCA0's turn order, 00193D90's blocking masks
  swapped and its placement mode 0.

## 4. Binding (coordinator)

Bound since census L13..L16 by `src/game/em_camera_live.c`
(docs/CAMERA_LIVE.md, which also lists the new translations of the commit
0018C0D0, 00102798 and 00193660 in em_camera_commit_original.c).

- **The camera frame.** `w_0018B9C0` (em_scene_bindings.c) calls
  `em_camera_0018B9C0` (gameplay) or `_opening` (cutscene): both run
  `em_camera_live_frame`, i.e. `em_camleft_0018B9C0` over the canonical
  camera block, in AREA11. The `em_sfx_listener` feed follows the frame
  (with cam+9C as the pan heading). The D_008106EF countdown helper of
  em_render_frame.c runs only for a scene without the live camera.
- **Storage.** `world.cam` is the live camera's block; `world.player` its
  view of D_008102B0; `globals.follow` the follow module's globals over the
  canonical pool; `d5F0` the pool's up vector; `d6EF` / `d6B8` / `dE74` /
  `s3B8D` / `d28A9A0` the scene state's canonical bytes and the transition
  substate; `s3B80` word 6 of the player closure's pad assignment block;
  `s31F0` the low byte of the AREA11 boxes' 0x700031F0 word; `d24A5F0` the
  region quads of `assets/camera_tables.emrg` (tools/export_camera_tables.py).
  `world.scratch` and `world.hit` are the canonical scratchpad words
  (CAMERA_LIVE.md section 3).
- **Workers.**

  | slot | original | bound to |
  |---|---|---|
  | wrap, approach, heading | 001B1470, 001B12B0, 001B1240 | `em_player_001B1470`, `em_script_host_001B12B0`, `em_script_host_001B1240` |
  | sine, cosine, atan2, sqrt | 0011E2A8, 0011DE90, 0011E620, 0011E748 | `em_sdk_math_original_*` over the collision world's SDK context |
  | segment | 0019A910 | `em_coll_segment_0019A910` on the world's one probe state |
  | inside | 001B1EA0 | `em_director_original_001B1EA0_bound` over the exported quads |
  | solve_dispatch | 0018D7B0 | `em_camera_follow_0018D7B0` |
  | commit | 0018C0D0 | `em_camera_commit_0018C0D0` |
  | w_0022EEF0 | 0022EEF0 | the binder's timeline: the opening's track (em_opening_runtime) while the opening owns the camera, else `em_area11_script_host_camera_0022EEF0` |
  | w_00195130 | 00195130 | `em_cam_specials_action_00195130`, pre-empted by the legacy stand-ins of CAMERA_LIVE.md section 6 |
  | w_001936E0, w_00193EB0 | | `em_cam_specials_action_001936E0`, `em_cam_specials_call_00193EB0` |
  | w_001DD980 | 001DD980 | `em_interaction_projection_publish` on the canonical projection record |
  | w_001B0C60, w_00197D20, w_00198650, w_00198AF0, w_0018CA90, w_00198CE0, w_00198D90, w_00198F10, w_001963A0, w_00196CE0, w_00197390, w_001D2830, w_001B0300 | | NULL: no translation; reaching them faults (never on the route) |

  The follow module's `tether` / `solve` / `solve_aim` / `bounds` are
  `em_camleft_00230000` / `_0018DD20` / `_0018F870` / `_0018D910`; the
  specials module's `w_001916C0` / `w_00191000` / `w_0022FCA0` /
  `w_00193D90` / `w_00194D10` are this module's routines.
- **0015CBA0** runs in the player stage after 0015BCF0's tail
  (em_player.c `player_states_stage`), writing the live record's +230.
- **Retired:** em_camera.c's `cam_solver_0018DD20` / `_0018F870` /
  `_0018D910`, `camera_prestep_00191390`, `camera_mode_dispatch` and
  `camera_solve` no longer run in AREA11 (they stay for a scene without an
  original collision world); em_camera_probe.c is deleted;
  `cam_bounds_settle_0018CE60` (em_game.c) is only the legacy camera's.

## 5. Limits and open items

- **Bound** (section 4; CAMERA_LIVE.md section 5 lists the camera's
  known differences).
- **Route coverage.** The captures and the replayed beats reach 0018B9C0
  (state 1: +4 = 0, 1, 3), 0018BC20 (action 0 and the opening's action 8),
  00190F20, 001916C0, 0018C0C0, 00191000, 0018DD20, 001914A0 / 00191580 /
  0018C5A0 and 0015CBA0 with real data. 0018B9C0 state 0 (the one-shot seat,
  and so 0018CE60 there), 0018F870, 0018D910, 00230000, 0022FCA0, 00194D10
  and 00193D90 are proven by the unit oracle only (any callee behaviour; no
  route capture reaches them: the route never aims, never takes the tether
  and never idles 481 frames).
- **The hand-off image has no scratchpad**: its captured check uses a zeroed
  one (original and translation from the same bytes).
- **Mutants that survive** (section 3 results) are listed with the reason
  (equivalent, or a boundary no random input lands on).
- **Missing workers** of 0018BC20 (section 4) are faults when reached.
- The readable C differs from the instructions at 0018D910's area-0x12 miss
  (it writes cam+14 + 200 where the instructions keep S3A3C; the oracle's
  mutant with the C's reading is killed). The translations follow the
  instructions everywhere; 0018C5A0's clamp is written the instructions' way
  (max when max <= step), which is equivalent to the C's here because the
  step is always a positive normal number.
