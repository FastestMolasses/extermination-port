# Player running jump (AREA11 crevice and tower jumps)

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "player-running-jump". This document covers the end of the Use chain
00160220 and the two player states it can enter there:

- **0015EC50**, the running-jump probe. When it finds a gap ahead with a
  floor more than 4.01 below the far side, it enters +5 = 6.
- **0015FDF0**, the aim solver, with its target scan **001AA4E0**. When a
  target exists, 00160220 enters +5 = 0x24.
- **001634A0**, the +5 = 6 callback (the jump itself).
- **001747F0**, the +5 = 0x24 callback.

On the route to Roger (`../Extermination/build/s87/route/`,
FIRST_LEVEL_ROUTE.md) the jump runs twice:

- beat 12_crevice_jump: Cross at f227, +5 = 6 / +1F0 = 0xC at f230, the
  landing (8/0xF, clip 0x6B) at f277;
- beat 14_roger_encounter: Cross at f238, 6/0xC at f241, landing at f288.

The ledge climb (+5 = 2, 00161790) and the vault (+5 = 3, 00162190), which
the Use chain reaches first through 0015DF10, are **not** in this lane. They
are translated and verified in `src/game/em_player_climb.c`
(PLAYER_CLIMB_SLIDE.md; `em_player_climb_live_state`,
`em_player_climb_live_probe`). The climb onto the boxes (beat 05) is state
2, compared there frame by frame against the capture. This lane only notes
how to bind them (section 5).

The module is **live in AREA11** (bound by em_player_closure_live.c). Section 5 lists what the
coordinator binds.

## 1. What the original does

Every routine works on the 0x320-byte player record. The addresses in
brackets are the instructions translated there.

### 0015EC50: the running-jump probe (returns 1 after +5 = 6)

00160220 calls it at 0016075C, after the three 0015DF10 ledge probes, when
+236 == 0 and +23B != 0x35. It returns 0 at once when:

- +314 == 1 (0015EC70);
- the +308 link owner's type byte (+3) is 0x28;
- that type is 2 and the body yaw +C4 is not strictly inside
  (−π/2, π/2) (0015ECCC, 0015ECEC);
- the feet +B0 lie in one of the hand-placed boxes of the current area
  (0015ED08..0015F738). Each box is y (+B4), x (+B0) and z (+B8) ranges
  with inclusive bounds:
  - area 4: one box;
  - area 0xD with D_00810701 == 0: three boxes;
  - area 0xF with D_00810701 == 1: two x/z boxes under one shared y range,
    then a third box;
  - area 0x10 with D_00810701 == 1: two boxes;
  - area 0x13 with D_00810701 == 0: three boxes;
  - area 0x16: one box.

  AREA11 is D_00810700 = 11 (every route snapshot), so no box applies
  there.

Otherwise it probes:

1. **Forward.** The reach r is 2, 3 or 4 by the tier +25C (< 2, == 2,
   else). The point (0, 1, 6r) is D_002489E0 = (0, 1, 6, 1) with z
   multiplied by r. It is transformed by the record's +D0 matrix into
   0x700038B0, and y is raised by 1. 0019AD00(p, 0x700038B0, 7) runs.
   With no hit, y is raised by 10 more and the probe repeats.
2. **The wall distance.** On a hit, 00177510 builds the ledge frame, and
   0x70003A20 = +B0 − point.x, 0x70003A28 = +B8 − point.z. The distance is
   dist = sqrt(madd(mula(dx, dx), dz, dz)), with dx re-read from
   0x70003A20.
3. **The side sweeps.** The yaw frame 0x700036A0 is identity, rotated by
   +C4 (00102BB0) and translated to (x, +B4, z). Here (x, z) is the ledge
   point on a hit, else the probe point. Two 0019AFE0 sweeps (mask 6) run
   in that frame: (−0.1, 10, 0) to (−5, 10, 0) (bit 1), then (0.1, 10, 0)
   to (5, 10, 0) (bit 2). The routine returns 0 when both sides are
   blocked without a forward hit, or when either side is blocked with one.
4. **The drop sweep.** It runs from (x, +B4 − 1, z) down to:
   - with +319 bit 0: the feet, one lower (+B0..+BC copied, y − 1);
   - otherwise: (0, −1, −2) through +D0.

   It must hit a face of class 0x2000 (node +1A & 0xFF00). With a forward
   hit, the far face must also be more than 13.5 farther than the wall:
   the routine returns 0 when dist ≤ 13.5 + sqrt(...) of the face point
   (stored at 0x70003A2C).
5. **The landing column.** 0019BC40 builds the column table at the face
   point plus 4.5 × the face normal (x, z), at y = +B4.
   - An empty table enters the jump.
   - Otherwise the entries are walked from the last down. The first one
     with flag bit 0 and height ≤ 4.01 + +B4 decides. +254 = height − +B4
     is stored, and the jump is entered only when +254 < −4.01. Otherwise
     the routine returns 0. No such entry also returns 0.
6. **Entering the jump:** +5 = 6, +6 = 0, +1F0 = 0xC, return 1
   (0015FD7C / 0015FDB0).

### 001AA4E0: the nearest target

It returns NULL while 0x70003B8D is set. Otherwise it walks the
D_00275B94 entries of D_00275B8C. An entry qualifies when:

- (+2 & 0x1F) == 2;
- the halfword +34 is nonzero;
- its type +3 is 1, 2, 4, 5, 6, 7, 8 or 0xC;
- 001AA2A0(p, entry, 001AA410(entry)) returns nonzero.

After a nonzero 001AA2A0 it reads 0x70003A20, the distance 001AA2A0 wrote
there. It keeps the entry with the smallest distance below 1000
(strictly smaller wins). D_00275B8C is the published auto-aim list
(001AAD00; CRATES_DRUMS_ORIGINAL.md): in every AREA11 route snapshot it is
empty, or it holds one entry of class 0xA, which does not qualify.

### 0015FDF0: the aim solver (returns 1 when a target exists)

00160220 calls it at 00160778 after 0015EC50 (or directly when +236 or
+23B blocks the probes). With no target it returns 0. With one:

1. 0x700038A0 = target.x − +B0, 0x700038A8 = target.z − +B8, and
   ang = 001B1470(π/2 + atan2(−dz, dx)).
2. 00174AC0(p, 2). When it returns nonzero and +23F ≥ 2: if
   |001B1470(ang − +218)| (stored at 0x70003A20) is not below 0.69813174
   (40°), +C4 = +218 and the routine returns 1.
3. If |001B1470(ang − +C4)| (0x70003A20) is not below 40°, it returns 1.
4. Otherwise it tries the side of the target the body is on. If
   001B1470(ang − +C4) < 0, the first angle is ang + 40°, else ang − 40°
   (each wrapped). A 25-unit probe runs along that angle from +B0
   (0x700036A0 frame, (0, 0, 25) into 0x700038B0, 0019AD00 mask 7). A
   clear probe commits the angle to +C4.
5. On a hit, it measures the clearance (0x70003A20/28 as in 0015EC50) and
   probes the other angle. A clear probe commits that angle. Otherwise +C4
   gets the second angle when its clearance is not smaller, else the first
   angle, recomputed.

00160220 then writes +5 = 0x24, +6 = 0, +1F0 = 0x3A, +0 = 3.

### 001634A0: state 6, the running jump (dispatch on +6)

- **0 (the take-off).**
  - +6 = 1 and +7 = 0.
  - Tier < 2: 001749A0(p, 0x6A, 0, 8.0).
  - Tier ≥ 2: n = 001C61D0(+40, 0x69); 0x70003A20 = (float)n;
    anim_clip_arbiter(p, 0x69, 4.0, n − 4.0).
  - +2F4 = +B4 (the take-off height).
- **1 (the launch, once the clip ends: +200 & 0x1000).**
  - +6 = 2.
  - Tier < 2: +38 = 4.1, 00178B90(p, 1), clip 0x6C.
  - Tier ≥ 2: +38 = 6.3, 00178B90(p, 1), clip 0x6B, +2EC = −0.03.
  - Then, by the tier:
    - +2EC = D_002485D0[tier] = {−0.04, −0.04, −0.03, −0.029};
    - (s, a) = D_002485B0[tier] = {(1, 0.9774), (1, 0.9774), (1.5, 0.8378),
      (1.8, 0.6632)};
    - +38 = s·cos a and +2E4 = s·sin a (0011DE90 / 0011E2A8);
    - +270 = +38 / 3.
  - +2E = 0, +25F = 1, and 0017DEB0 (the dust and sound).
- **2 (rising) and 3 (falling).** Both start the same way:
  - r = 002243F0(p). When r == 0 and 0017C860(p, +2E4) grabs a ledge (it
    returns nonzero, having set +5 = 4), the callback returns at once.
  - 001751A0 (the stick quadrant) runs.
- **Case 2 continues:**
  - With +24C == 1 and +23F ≥ 2, +38 = max(0.9·+38, +270). Otherwise, when
    +24C != 1, 00178EC0 (the sideways impulse) runs.
  - 00178B90(p, 1); +2E4 += 2·+2EC; +B4 += +2E4.
  - The ceiling test: while +2E4 > 0, 001760C0(p, p+B0, 0, 18.0) zeroes
    +2E4 when the column above is covered. Otherwise +25F = 2.
  - When +B4 ≤ +2F4 or +314 == 1: +6 = 3, +2E0 = +38 / 30, +2EC = +2E4.
  - When +2E4 ≤ 0: 00175900(p, 1). On contact (+A): +38 = 0, and 0017C580
    lands unless r was set.
  - When +23A == 0x5D: 0021D250(p, 0).
- **Case 3 continues:**
  - +38 −= +2E0 with 00178EC0 while +38 > +2E0, else +38 = 0.
  - 00178B90(p, 1), then **00179880(p, p + 0x2E4)**: the drop accumulator
    on +2E4 (−0.04 a frame, clamped at −4; +B4 += +2E4; +25F = 2).
  - The same ceiling test, then 00175900(p, 1).
  - On contact: +38 = 0, and 0017C580 unless r.
  - Without contact, when +38 ≤ 0 and r == 0: the drop (+5 = 7, +6 = 0,
    +1F0 = 0xD, clip 0x72 at blend 8, +2EC = +2E4).
  - When +23A == 0x5D: 0021D250(p, 0).
- **0x63:** 0021D2E0(p, 0x78, 0). Any other +6 does nothing.

### 001747F0: state 0x24 (dispatch on +6)

- **0:**
  - +6 = 1, +7 = 0.
  - Clip 0x3E, or 0x5D when +236 is set (001749A0 at blend 0).
  - +38 = 0, +25C = 1, +21C = 0, +2E4 = 0.
  - Sound 0x186 (001FBD50(p, 0x186, 0, 300)).
- **1:** when +3C ≤ 10, +6 = 2 and +0 = 1. It then falls into 2.
- **2:**
  - When the clip ends (+200 & 0x1000): +5 = +6 = +1F0 = 0.
  - Otherwise +38 = c − +21C and +21C = c, where c is the word at
    *(D_00275B40) + 8 (the first skeleton node), read twice. Then
    00178B90(p, 0).
- **Every sub-state then runs:**
  - 001764E0 (with the caller's $s1);
  - +B4 += −0.2;
  - 00175900(p, 1) and 001796C0;
  - +0 = 1 when +4 == 1, +5 != 0x24 and +0 != 1.

### Where the readable C differs from the instructions

0015EC50, 0015FDF0 and 001634A0 are NEARMISS C, so each was re-read from
its listing. These readings disagree with the C:

- **001634A0 cases 2 and 3.** They return early when 002243F0 returned 0
  and 0017C860 then returned **nonzero** (00163698 / 00163864). The C
  returns when 0017C860 returned 0, the opposite polarity. (The NEARMISS
  note calls this a "guard-combination polarity flip".)
- **001634A0 case 3** passes **p + 0x2E4** to 00179880 (001638B8). The one
  translation of 00179880 is em_player_fall.c's `em_player_fall_00179880(p,
  at)` (2026-09-24, PLAYER_FALL.md "One owner"); this module calls it with
  0x2E4 (`em_player_fall_drop` is its +2EC form). The oracle executes the
  original 00179880 there, so it checks that call.
- **001634A0 case 0** calls 001C61D0 with (+40, 0x69); the C shows a
  third argument.
- **The distances** in 0015EC50 and 0015FDF0 use the EE accumulator:
  madd(mula(dx, dx), dz, dz), with dx re-read from 0x70003A20. The C
  writes a plain sum of products.
- **The 0015EC50 C** is otherwise faithful. That includes the level-0xF
  shared-y test and the store of +254 on both sides of the −4.01 test
  (0015FD74 sits in a delay slot).

## 2. The translation (`src/game/em_player_running_jump.c/.h`)

- **The record.** Every routine works on `EmPlayerLiveActor` by original
  offset. The +308 owner is read through `link_prev`, with its type byte
  from the `link_type` worker. Arithmetic is `em_ee_float.h` on raw bit
  patterns.
- **SDK leaves.**
  - 001029C0 / 00102BB0 / 00102918 are
    `em_owner_services_identity_001029C0` / `_rotate_y_00102BB0` /
    `_translate_00102918` (em_owner_services_original.h). They are
    bit-exact, and a refused float form faults.
  - 001B1470 is `em_player_recovery_wrap`. It faults where the original
    would never return.
  - 001026A0 (four VU0 macro ops through `em_vu_vec_bits`), 00102948 (a
    quadword copy) and 0011DF78 (fabs: the sign bit cleared) are written
    inline.
- **Workers.** Every other callee is a worker in
  `EmPlayerRunningJumpWorkers`: 28 callee workers, plus four data reads
  (`link_type`, `target`, `target_xz`, `root_clock`) and the scratch.
  - **Fail-stop:** every entry point refuses (−1) before its first write
    unless every worker and the scratch are bound. A worker returning < 0
    stops the routine there. The four SDK float workers cannot signal
    faults, like em_player_recovery's.
- **The scratch** (`EmPlayerRunningJumpScratch`). It holds the words the
  routines write: 0x700036A0 (16 words), 0x700038A0..DC (16 words) and
  0x70003A20/28/2C. The routines re-read these words after worker calls
  exactly where the original does.
- **Entry points.**
  - `em_player_running_jump_probe` (0015EC50) and `_aim` (0015FDF0), each
    with an `EmPlayerRunningJumpScene` (D_00810700, D_00810701,
    0x70003B8D, D_00275B94);
  - `_target` (001AA4E0);
  - `_tick` (001634A0) and `_state24_tick` (001747F0);
  - the binding adapters of section 5.
- **The small tables** are D_002489E0, D_002485D0 and D_002485B0 (16
  words), plus the box bounds of 0015EC50, all kept as the constants the
  instructions build. The oracle checks every one: each case compares what
  the original read from the ELF with what the native wrote or passed.
- **Two faults stand in for out-of-range original reads:**
  - 001634A0 case 1 with +25C > 3 reads past the four table rows;
  - 001AA4E0 with a negative D_00275B94 would walk the list for 2^32
    entries.

  A column table count outside 0..16 also faults, as in em_player_climb
  and em_player_recovery (the original's arrays alias past 16).

## 3. Verification

### Unit oracle (`tools/test_player_running_jump_reference.py`, default ~9 s)

The test runs the original instructions of 0015EC50, 0015FDF0, 001AA4E0,
001634A0, 001747F0 and 00179880 on `JumpEE`. JumpEE is
`test_player_recovery_reference.ModelEE`: the shared interpreter with every
COP1 and VU0 macro op on `tools/ee_float_model.py`, plus branch recording.
It is a closed world: a call to anything neither hooked nor listed is
refused.

- **Unhooked leaves.** 00102948, 001026A0, 001029C0, 001029E8, 00102BB0,
  00102918, 001B1470 and 0011DF78 run as the original instructions.
- **Hooks and scripts.** Every other callee is hooked, with a per-call
  scripted effect that the native workers replay. The effects are:
  - the return value;
  - rewrites of record bytes (+B0.., +C4, +218, +38, +2E4, +2EC, +270,
    +2E0, +2F4, +24C, +23F, +23A, +A, +314, +25C, +200, +6, +5, +4, +0,
    +3C, +21C, +319, +D0 words);
  - rewrites of any compared scratch word;
  - the probe hit (0x700031B0 point, the 0x700031D0 node record);
  - the column table and the ledge frame;
  - the float results.

  Re-reads after a call are therefore checked.
- **What must match:**
  - all 0x320 record bytes;
  - the 35 scratch words;
  - the return value;
  - the full worker call sequence, with arguments as bit patterns (the
    sweep vectors by x/y/z, since 0019AFE0 reads no w).

  The oracle asserts that 001760C0 gets p + B0, that the sound call passes
  (id, 0, 300.0), and that 001764E0 sees the caller's $s1 (the oracle
  hands in one with bit 2 set).
- **Checks beyond the byte comparison:**
  - The hooked set equals the jal targets of the six routines: 37 targets,
    all hooked, translated or leaves.
  - Every one of the 180 conditional branches is taken both ways, except
    0015FCE0 taken (a negative 0x700031E0: 0019BC40 counts up from 0).
    That outcome is listed as unreachable, and the test fails if it is
    ever reached.
  - The box positions come from the original's own compare immediates.
    The test reads the (lower, upper) pair of every c.lt.s / c.le.s test
    in each area segment; nothing is copied. On top of the random cases,
    2,706 boundary cases run. They cover every area, every interval
    triple, the midpoint, the value just below and the value just above
    per axis, and each bound exactly. `EM_TEST_FULL=1` runs all 125
    combinations per triple (10,250 cases).
  - A fault cut in one case of five: a worker fails at a random call (not
    an SDK float worker). The native must return −1 with exactly the calls
    up to it.
  - The binding adapters run in one case of seven, with and without a
    shared 0x70003A20 word, and must give the same result.
  - The refusals: each of the 33 bindings (32 workers plus the scratch) is
    removed on each of the 5 entry points (165 checks). Two more checks
    cover the tier-4 and negative-count faults.
- **Results:** section 6.
- **Mutants.** All of these were killed:
  - 13.5 changed by one ulp;
  - the 0017C860 early-return polarity (the NEARMISS C's reading);
  - 00179880 on +2EC instead of +2E4;
  - the area-0xF D_00810701 test;
  - `<=` for the nearest-target compare;
  - +21C not taken from the node word;
  - `<=` for the −4.01 drop test;
  - `<` for the 4.01 lip test;
  - `<=` for the clearance compare;
  - a box's upper y bound one ulp high;
  - a box's lower x bound one ulp low.

  The only survivor, `<=` for 00179880's −4 clamp, is equivalent: both
  paths store −4.

### World mode (`EM_TEST_WORLD=1`, minutes)

The test replays the route beats over the captured RAM
(`shared.RouteReplay` on JumpEE, from each beat's source snapshot, with
its recorded pad input). Two replays run for each beat:

- the original stage;
- the stage with the native routines hooked in at 0015EC50, 0015FDF0,
  001AA4E0, 001634A0 and 001747F0. Their workers run the original callees
  in the same EE (`WorldJump`). The record and the 35 scratch words are
  written back before each call and read after it, and vectors go where
  the original passes them.

After every frame, the **whole 32 MB RAM and the scratchpad** (SHA-1), the
player record and the sound/effect calls must be identical, and every
trace row must match (`route_row_check`). A replay ends before the first
scripted-takeover row (`test_player_recovery_reference.replay_end`). The
beats are 12_crevice_jump and 14_roger_encounter (`EM_WORLD_BEATS` picks
others). Section 6 has the results and section 4 the paths the route
takes.

## 4. Coverage along the route

The world mode records the branch outcomes of the original replay inside
these routines. On both beats the route takes exactly one path:

- **0015EC50, once per beat (the Cross press).**
  - +314 != 1, and there is no +308 owner.
  - Area 11 skips every box.
  - The tier is 3 (reach 4).
  - Neither forward probe hits, and neither side sweep is blocked.
  - +319 bit 0 is set, so the drop sweep ends at the feet − 1. It hits a
    class-0x2000 face.
  - The column table is not empty. Its last flagged entry at or below
    4.01 + +B4 is more than 4.01 below the feet (0015FD70 not taken).
  - The routine enters the jump and returns 1.
- **001634A0, 47 callbacks per beat (f230..f276 of beat 12, f241..f287 of
  beat 14).**
  - Sub-state 0 at tier ≥ 2: 001C61D0 and clip 0x69.
  - Sub-state 1 waits 7 frames for the clip end, then launches with clip
    0x6B and +2EC = D_002485D0[3].
  - Sub-state 2 rises and falls for 38 frames. The landing is on its
    floor contact: +2E4 ≤ 0, 00175900, +A set, 0017C580 → +5 = 8 with
    landing reaction +6 = 5.
  - **Sub-state 3 is never reached.** The feet never drop below the
    take-off height +2F4 before the contact.
- **0015FDF0 and 001AA4E0 are not reached.** The only Use press that
  gets that far on each beat is taken by 0015EC50.

The unit oracle is the only evidence for:

- 001634A0's sub-state 3, the ceiling hit, 0017C860's grab, the drop to
  +5 = 7, 0x63 and tier < 2;
- 0015EC50's other exits;
- 0015FDF0, 001AA4E0 and 001747F0.

001747F0 and 0015FDF0's aim need a qualifying D_00275B8C entry, and no
AREA11 route snapshot holds one.

## 5. Binding (coordinator)

**Bound live in AREA11 since the Boxes step (2026-09-24):** `em_player_closure_live.c` binds this module over the live player record (FIRST_CONTROL.md "Engaged"). Workers with no translation are fail-stop workers that name their original. The notes below are the binding it follows.

**Live and compared since census L11 (2026-09-24):** the level smoke's
`crevice_jump` phase plays route 12's jump through this module and equals
the capture row for row (LEVEL_SMOKE.md "crevice_jump": +5, +1F0, +1F1,
clip, clock, ground and the arc's Y from the entry f230 through the landing
f277 and the hand-back f304; the step length within 1e-4 on the free-flight
rows). `dust` (0017DEB0) is em_player_climb.c's one translation over the
record (`em_player_climb_live_0017DEB0`). The targets (001AA4E0 over
D_00275B8C, 001AA410, 001AA2A0) stay fail-stop: AREA11 publishes no class-2
owner. Beat 14's tower jump waits on the level smoke's roger phase (WP-9).

### Stage slots (0015B130's table; only 0015B130 dispatches these)

- `b.stage.state[6] = em_player_running_jump_state6` (001634A0);
- `b.stage.state[0x24] = em_player_running_jump_state24` (001747F0);
- each `state_context[...] = &jump_live`, an `EmPlayerRunningJumpLive`
  (the workers, the scene provider, and optionally `shared3A20`).

The ledge climb and the vault are already translated:

- `b.stage.state[2]` and `[3]` = `em_player_climb_live_state`, with an
  `EmPlayerClimbLive` (PLAYER_CLIMB_SLIDE.md section 6);
- the Use chain's three 0015DF10 calls are `em_player_climb_live_probe`.

### The Use chain (00160220, untranslated; the coordinator's use hook)

After 00184BA0, 001AAC00 (area 0x15), 0015D4C0 and the trigger boxes of
areas 1, 4 and 0xD:

1. With +236 == 0 and +23B != 0x35, the three
   `em_player_climb_live_probe` calls run, each after build_trs_matrix of
   +D0 at the probe yaw (yaw, wrap(yaw − π/4), wrap(yaw + π/4); +C4 is
   restored and +D0 rebuilt after the third). Then, still under that
   condition, `em_player_running_jump_use_probe` runs (0016075C). A result
   of 1 ends the chain.
2. `em_player_running_jump_use_aim` runs (00160778). A result of 1 makes
   the hook write +5 = 0x24, +6 = 0, +1F0 = 0x3A, +0 = 3 and end the chain.

The trigger boxes jump straight to step 1's 0015EC50 condition (the
original's `trigger:` label), skipping the 0015DF10 probes.
`EM_PLAYER_NEED_USE_STATES` needs +5 = 6 and 0x24 bound, and both now
have callbacks.

### Workers of `EmPlayerRunningJumpWorkers`

| worker | original | binds to |
|---|---|---|
| `sine`, `cosine`, `atan2`, `sqrt` | 0011E2A8, 0011DE90, 0011E620, 0011E748 | `em_sdk_math_original_float_0011E2A8` / `_0011DE90` / `_0011E620` / `_0011E748` |
| `move`, `sweep` | 0019AD00, 0019AFE0 | em_coll_move_original.h, as for em_player_recovery (the worker fills `EmPlayerProbeHit` from 0x700031B0/D0) |
| `table` | 0019BC40 | `em_collision_column_table` into an `EmPlayerClimbTable` (the same binding as em_player_recovery's `table`) |
| `ledge` | 00177510 | `capture` in em_player_climb.c (static: the climb owner must export it over an `EmPlayerRecoveryLedge`, as PLAYER_RECOVERY.md also asks) |
| `column` | 001760C0(p, at, arg, h) | none standalone (em_player_floor.h's probe worker has the arg fixed at 1; this caller passes 0) |
| `link_type` | (+308)+3 | the owner's EmActor type byte (`EmPlayerLiveActor.link_prev`) |
| `target`, `target_xz` | D_00275B8C[i] +2/+3/+34, +B0/+B8 | the published auto-aim list. No port module keeps it yet; it is the copy 001AAD00 makes of D_00275B90, fed by 001B1CA0 (CRATES_DRUMS_ORIGINAL.md). Until it exists, bind an empty list only if the coordinator can show D_00275B94 == 0 (true in every AREA11 route snapshot but beat 14's, whose one entry is class 0xA). |
| `target_radius`, `target_sight` | 001AA410, 001AA2A0 | untranslated. 001AA2A0 must store its distance into `scratch->s3A20`. |
| `heading` | 00174AC0 | `em_player_heading_record_worker_result` (em_player_heading_record.h), context an `EmPlayerHeadingRecord` whose `world.spad3A20` is the shared 0x70003A20 word (`scratch->s3A20`) |
| `request`, `arbiter`, `clip_frames`, `sound` | 001749A0, anim_clip_arbiter, 001C61D0(+40, clip), 001FBD50(p, id, 0, 300) | the pose host and `em_sfx`, as for the fall lane |
| `translate`, `strafe`, `quadrant`, `react`, `grab` | 00178B90, 00178EC0, 001751A0, 002243F0, 0017C860 | `em_player_recovery_translate_worker`, `_strafe_worker`, `_stick_quadrant_worker`, `_react_002243F0_worker`, `_ledge_grab_worker` (same signatures, an `EmPlayerRecoveryLive` context) |
| `dust` | 0017DEB0 | static in em_player_climb.c (operates on `EmPlayerClimbActor`); needs a live-record adapter |
| `land`, `surface5d`, `teleport` | 0017C580, 0021D250(p, 0), 0021D2E0(p, 0x78, 0) | `em_player_fall_land`, `_surface5d`, `_teleport` through small adapters (their first argument is the `EmPlayerLandWorkers *`) |
| `probes`, `floor`, `fall_check` | 001764E0, 00175900, 001796C0 | `player_states_wall_probes` (with the caller's $s1: `EM_PLAYER_LAND_S1_CALLER`), `player_states_floor_service`, `player_states_fall_check` |
| `root_clock` | *(*D_00275B40) + 8 | the first skeleton node's word +8 (the pose host) |

**Shared context.** The worker tables share one `context` pointer, so the
coordinator's context has to reach each lane's own context (recovery,
fall, climb). Either put them first in one coordinator struct or bind
small trampolines.

**0x70003A20.** Set `EmPlayerRunningJumpLive.shared3A20` to the fall
lane's `EmPlayerLandScratch.s3A20` (or `EmPlayerRecoveryLive.shared3A20`'s
word). Then every writer shares one word, as in the original: 001634A0
case 0 writes it, and 0017C580, 0017C860 and 002243F0 read or write it.

**Build, when bound.** Add `src/game/em_player_running_jump.c` to COMMON,
with `src/game/em_owner_services_original.c` and the em_player_recovery
set (`em_player_recovery.c`, `em_player_slide.c`, `em_player_climb.c`,
`em_player_floor.c`), for `em_player_recovery_wrap`. The Makefile test
target is in the lane report.

## 6. Results and limits

**Results of the last runs** (2026-09-23):

- **quick:** PASS. 7,706 cases (5,000 random, 2,706 box), 26,423 worker
  calls identical, 180 branches both ways, 1,090 fault cuts, 1,056 adapter
  runs, 167 refusals, 7-9 s.
- **full** (`EM_TEST_FULL=1`): PASS. 50,250 cases (40,000 random, 10,250
  box), 155,449 worker calls identical, 6,523 fault cuts, 6,687 adapter
  runs, 27.8 s.
- **world** (`EM_TEST_WORLD=1`, 5 min for both beats in parallel): PASS.

  | beat | frames identical (whole RAM + scratchpad + record) | trace rows | sound/effect calls | native calls |
  |---|---|---|---|---|
  | 12_crevice_jump (to counter 13130) | 345 | 337 | 51 | 0015EC50 → 1 once; 001634A0 47 (leaving +6 = 1 ×8, 2 ×38, 5 ×1) |
  | 14_roger_encounter (to counter 14229, before Roger's takeover) | 289 | 289 | 17 | the same |

**Limits:**

- **Untranslated workers:** 001AA410 and 001AA2A0 (targets), 001760C0 with
  its arg, and the export of 00177510. 0017DEB0 is bound live through
  em_player_climb's translation (census L11).
- **The aim path** is verified only by the unit oracle: no AREA11 capture
  holds a qualifying target.
- **Other areas' boxes** are verified only by the unit oracle, which reads
  their bounds from the instructions.
- **Out-of-range reads fault** (tier > 3, a negative list count, more than
  16 column entries) instead of reproducing whatever the original would
  read.
