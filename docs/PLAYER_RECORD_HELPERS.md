# Player record helpers, and the climb and slide on the live record

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "player-record-helpers" (2026-09-24). This document covers three
things:

- the one translation of the small player helpers that the fall, recovery,
  hang, ladder, closure and running-jump states call, with record-level
  entry points in their worker-slot shapes (`em_player_record_helpers.c`);
- the climb (states 2 and 3, the ledge probe 0015DF10) and the slide
  (state 0x1C) moved onto the measured EE float model, with every callee
  going through its one translation;
- the live adapters that run the climb and the slide on the player record,
  and the whole-world route replays that prove them on the record.

Nothing here is bound in the live player yet: FLOOR and USE stay gated
(FIRST_CONTROL.md "Live player states"). Sections 3 and 6 have the
binding recipe.

## 1. The helpers and their one translation

| Original | What it does | Body | Record entry | Fits these worker slots |
|---|---|---|---|---|
| 001755B0 | stick-versus-body heading test: 0x70003A20 = \|wrap(wrap(pi + +24C + D_008106A0) - +C4)\|; returns 1 when that is above pi/2 | (entry only) | `em_player_record_001755B0` | EmPlayerLandWorkers.test_001755B0 |
| 00177510 | ledge frame from the probe hit it follows (point, node normal, heading wrap(3pi/2 + atan2(-n.z, n.x)), its yaw matrix) | `em_player_helper_00177510` | `em_player_record_00177510` | EmPlayerRecoveryWorkers.ledge, EmPlayerRunningJumpWorkers.ledge |
| 001775E0 | the sweep across the ledge lip | `em_player_helper_001775E0` | `em_player_record_001775E0` | EmPlayerRecoveryWorkers.lip |
| 001776E0 | the side sweeps around a high ledge (hit mask, 0, or -1) | `em_player_helper_001776E0` | `em_player_record_001776E0` | EmPlayerRecoveryWorkers.sides |
| 00177CF0 | the hand sweeps at a high ledge | `em_player_helper_00177CF0` | `em_player_record_00177CF0` | EmPlayerRecoveryWorkers.hands |
| 0019A180 | the column-table entry attribute | `em_player_helper_0019A180` | `em_player_record_0019A180` | EmPlayerRecoveryWorkers.attribute |
| 0017F320 | the four hang clearance sweeps (1 when all are clear) | `em_player_helper_0017F320` | `em_player_record_0017F320` | EmPlayerLandWorkers.test_0017F320, EmPlayerHangWorkers.hang_clear, EmPlayerRecoveryWorkers.hang_clear |
| 00188550 | the hang row clip D_002754C0[+235 & 1] (0x7B / 0x8E) | `em_player_helper_00188550` | `em_player_record_00188550` | EmPlayerLandWorkers.pose_clip, EmPlayerRecoveryWorkers.hang_row, EmPlayerHangWorkers / EmPlayerLadderClimbWorkers / EmPlayerClosureWorkers clip_row, EmPlayerClosure1019Workers.clip_88550, EmPlayerMajor2Workers.w00188550 |
| 00174FD0 | the stick quadrant +24C, with +23F, +244, +248 and 0x70003A20 | (entry only) | `em_player_record_00174FD0` | EmPlayerHangWorkers / EmPlayerLadderClimbWorkers steer_input, EmPlayerClosureWorkers.steer, EmPlayerClosure1019Workers.steer |

Before this lane, 001755B0 had no translation, and the rest existed only as
statics of em_player_climb.c or over its and em_player_slide.c's mirrors.
The climb and the slide now call these bodies. The slot fits in the last
column were checked by compiling assignments of each entry into each slot
with `-Werror`. Every slot table has a single `context`, so a binder passes
a composite context whose first member is the `EmPlayerRecordHelpers`. This
is the same convention as `em_player_heading_record_worker`.

**What was read.** The byte-matched C of 001755B0, 00177510, 001775E0,
00174FD0 and 00188550 was read. So was the readable C of the NEARMISS
001776E0, 00177CF0, 0017F320 and 0019A180, with its operand and evaluation
order checked against the instructions.

**Callees reached through their owners (none re-translated):**

| Callee | Owner |
|---|---|
| 001B1470 | `em_player_001B1470` (em_player_stage_workers.c) through `em_player_helper_wrap` |
| 0011DF78 | `em_sdk_math_original_0011DF78` |
| 001029C0, 00102BB0, 00102918, build_trs_matrix | em_owner_services_original |
| 001026A0 | `em_effect_original_001026A0` |
| 001028B8 | the measured VADD.xyzw form (`em_vu_vec_bits`) |

`em_player_helper_wrap` uses the bound in em_script_host_workers.h: an
argument with \|x\| >= 4096.0 faults (-1) instead of looping. The original
would loop for thousands of iterations; no stick, camera or body angle
reaches the bound.

The workers are 0019AFE0 (the sweep, record-level: the shape of
EmPlayerRecoveryWorkers.sweep), 0011E620 (atan2f) and 0011DE90 (cosf).
The globals are read through pointers at the binder's canonical storage:
0x70003B8D, D_00810E57, D_00810E64, D_00810E65 and D_008106A0.

**The scratch.** The helpers write the shared `EmPlayerLandScratch`, the
fall's single instance:

- 00174FD0 and 001755B0 write 0x70003A20;
- 001775E0, 001776E0, 00177CF0 and 0017F320 write the 0x700038A0..AC
  quadword (the last vector each stores there).

Their other scratch words are not modelled: 0x700038B0..DF, 0x700036A0,
00177460's 0x70003680, 0x70003A24 (dz in 0015DF10 and the vault set-ups,
the steer angle in 0017F5F0) and 0x70003A28 (the vault tick count). That
nothing reads them before writing them is observed, not proven: in every
world replay below the frames stay identical with them unmodelled.
00177510's ledge frame (0x70003050, 0x70003060, 0x700031E4, 0x70003070)
is the caller's `EmPlayerRecoveryLedge`.

## 2. The climb and the slide on the measured float model

`em_player_climb.c` and `em_player_slide.c` used to truncate host doubles
(`em_effect_float32`). Their oracles ran the shared interpreter's older
model, which EE_FLOAT_MODEL.md 5a lists as deviating: no ADD/SUB pre-trim,
and truncated DIV. Both sides were therefore wrong together. Now:

- **Arithmetic.** Every COP1 operation and compare goes through
  em_ee_float.h. The distances are MULA.S then MADD.S, as the originals
  compute them.
- **SDK leaves.** They are the owners' measured translations above.
  em_player_floor.c's `em_player_sdk_*` are no longer used here.
  - Their 00102BB0 prologue computes pi/2 +- angle with a truncated
    exact sum, which the pre-trim changes for some angles in (0.25, 0.5).
  - Their 001B1470 does the same.
- **Other callees go through their one translation:**
  - the slide's 00174FD0 is `em_player_record_00174FD0`, and 00179880 is
    `em_player_fall_00179880`. Each runs over a record built from the
    mirror (`to_live`, the call, `from_live`), and every byte either writes
    is a mirror field;
  - 001B12B0 is `em_script_host_001B12B0`;
  - 001281C0 is `em_player_float_to_int`;
  - the slide's former copies of 00174FD0, 00179880 and 001B12B0 are gone.
    `em_player_slide_approach` remains only as a thin wrapper for the
    oracle's approach cases.
- **Scratch writes are modelled** (into `workers.scratch` when it is set).
  - 0015DF10: its workspace vector 0x700038A0 (the probe vector, the
    table point, the lip base, the top, the depth test's up vector, the
    feet and side columns, the high-ledge ahead point) and 0x70003A20 (dx
    of each hit, the third probe's heading error, the high-ledge reach).
  - 0017D800: the rise tick count in 0x70003A20.
  - The vault set-ups: dx in 0x70003A20; for tier 3, 00162190 then stores
    the clip length there.
  - The slide: 0016C570's side vectors and 0016CD70's step vector in
    0x700038A0; the clip lengths of 0016C6A0 and 0017F5F0's speed factor
    in 0x70003A20.

## 3. The live adapters (the record binding)

`em_player_slide_live_state` (+5 0x1C), `em_player_climb_live_state` (+5 2
and 3), `em_player_climb_live_probe` and `em_player_climb_live_ledge` run
the translations over the live record:

- **`workers`** (the mirror table) binds only the callees whose original
  takes no record. For the climb these are move, sweep, segment, column,
  table, effect, atan2 and sqrt. For the slide they are stop_sound,
  effect, move, sweep, sine, cosine and atan2. The em_coll_move_climb_*
  / _slide_* adapters fit them.
- **The record-level slots** (context = `live_context`) bind every callee
  that takes the record. For both: floor, fall, request, arbiter,
  clip_frames (handed the +40 bank word), sound, land_sound, translate and
  land. Climb only: probes, heading, reentry, handoff, and skeleton (with
  node 1's +C4 / +8). Slide only: damage, land_check, step_sound, and
  surface5d(p, 0) / teleport(p, 0x78, 0) (the constants 0016C6A0 passes).
- **`scratch`**: the shared EmPlayerLandScratch. It is required.
- **The rule.** Before every worker call the adapter stores the mirror into
  the record. After it, the adapter reads the record back. A worker that
  reads the record sees exactly the original's bytes, and a worker's writes
  anywhere in the record survive. For example, the pose host's 001749A0
  rewrites +20C, +2C and the +3C clock, and the mirror also carries +3C
  (measured on the 06_hill_slide snapshot). Before this lane, only floor,
  probes and fall were synced. A 001749A0 bound through the pose view would
  have had its +3C write overwritten by the stale mirror at the end of the
  stage.
- **The slide's move** (0019AD00 with bit 31) writes its x/z response into
  the `position` it is handed. The adapter hands it the record's +B0 lanes
  and stores x and z back into +B0 / +B8. Anything else it writes, it
  writes on the record. The live unit cases found the first version of
  this adapter storing y as well, which overwrote a worker's +B4 write.
- **The Use chain.** `em_player_climb_live_ledge` has the shape of
  EmPlayerUseWorkers.ledge (angle as raw bits).
- **Link kind.** The climb's link kind comes from `link_kind(link_context,
  live->link_prev)` (em_actor_collision_player_link_kind).
- **Fail-stop.** A missing slot or pointer faults (-1) before the record
  is touched.

The existing record-level owners fit these slots directly (checked by
compiling the assignments with `-Werror`):

| Slots | Owner |
|---|---|
| floor / probes / fall | `player_states_floor_service` / `_wall_probes` / `_fall_check` |
| request / arbiter / clip_frames | `em_pose_host_request` / `_arbiter` / `_clip_frames` |
| handoff | `em_pose_host_handoff` |
| heading | `em_player_heading_record_worker` |
| translate | `em_player_recovery_translate_worker` |
| slide damage | `em_player_recovery_react_00224B80_worker` |

land, land_check, surface5d and teleport (em_player_fall_*), and the
sounds and the skeleton, need a one-line wrapper each.

## 4. Verification

**`tools/test_player_record_helpers_reference.py`** (new).

- **Setup.** The nine originals run on FallEE (the measured model). Their
  leaves 001B1470, 0011DF78, 001029C0, 00102BB0, 00102918, 001026A0 and
  001028B8 run as original code. The hooked set (0019AFE0, 0011E620,
  0011DE90) plus the executed set is asserted to equal the routines' jal
  targets.
- **What each case compares:** all 0x320 record bytes, the 0x700038A0..AC
  and 0x70003A20 words, the ledge frame 00177510 leaves (point, normal,
  heading, 16 matrix words), the return value, and the worker calls with
  their arguments.
- **Inputs.** Edge words (signed zeros, denormals, the largest finite
  value, angles past +-pi) and boundary values:
  - \|error\| == pi/2 and one ULP either side (001755B0);
  - +-3pi/4 and +-pi/4 atan2 results (00174FD0);
  - +-0.70020753 / +-1.7320508 and their neighbours (0019A180);
  - node byte 0x32 (00177CF0);
  - sweeps mostly clear, so 001776E0 reaches its side sweeps.
- **Faults and refusals.**
  - Fail-stop: a sweep faulting stops the routine with no later call.
  - Each missing worker or pointer is refused before any write.
  - Three out-of-domain 001755B0 sums are refused before 0x70003A20 is
    written.
- **Branches.** Every conditional branch of the nine routines goes both
  ways. Three outcomes are excluded, each found by decoding, not by
  address: 00174FD0's two sign tests on a zero-extended stick byte, and
  0019A180's arg0 != 0 exit (all three callers pass 0).
- **Route captures.** The end snapshot of each PCSX2 route beat
  (`../Extermination/build/s87/route/<beat>/`) gives a real world. On it,
  each routine runs as original code with every callee original: the
  sweeps are 0019AFE0 over the captured geometry, and the SDK math is
  0011E620 and 0011DE90. The record entry runs on a second copy with its
  workers bound to the same originals, the record and the scratch synced
  around each call (`RecordWorld`).
  - Inputs: each routine as captured, plus variants on the stick angle,
    the stick bytes and gait, and ledge heights over the ledge frame the
    capture's last probe left.
  - Record, scratch, ledge frame and result must be identical, and so
    must the whole RAM after all runs.
  - Skipped: a capture whose +24C is not a stick angle (00174FD0's integer
    quadrant, which 001755B0 never meets: its callers run 00174AC0 first),
    and 00177510 without a probe hit left in the capture.
  - Quick run: beats 05, 06, 11 and 13, 82 runs. EM_TEST_FULL=1: beats
    00..14, 309 runs, 6 skipped.
- **Results.** Quick: 2,700 of 36,000 cases plus the four captures, about
  3.5 s. EM_TEST_FULL=1: 36,000 cases (50,237 worker calls) plus all 15
  captures, 9.5 s.

**`tools/test_player_climb_reference.py` / `test_player_slide_reference.py`.**

- **Unit oracles.** Both now run on FallEE, with 0011DF78 and the SDK leaves
  executed as original code instead of host models. Every case also
  compares the five scratch words, which covers 0015DF10's own stores and
  the 0017D800 and vault set-up stores.
- **Live-record unit cases** (new). The original 00161790, 00162190,
  0015DF10 or 0016C6A0 runs against the live adapter on a whole record
  (random bytes plus the case's fields). At every worker call both sides
  record the 0x320 record bytes and the scratch words the callee is handed.
  They then apply the same scripted effect plus scripted writes anywhere in
  the record (mirrored fields and others such as +20C, +214, +2FC). The
  worker calls, every handed record, the final record and the scratch must
  be identical.
  - Mutations caught: dropping the store-before or the read-after around
    one request worker fails both tests.
  - Quick: climb 450 cases (1,003 worker calls, at least one probe starts a
    climb), slide 300 cases (2,361 calls, every sub-state).
  - Full: climb 6,000 (13,049 calls), slide 4,000 (31,432 calls).
- **Default runs.** Climb about 7 s. Slide about 6 s. Mode and counts are
  printed in the banner.
- **EM_TEST_FULL=1.** Both pass:
  - climb: 3,000 TRS, 20,000 probe, 8,000 state, 8,000 vault, 1,000 hang,
    1,982 column points, 1 min 46 s;
  - slide: 43,915 approach, 12,000 tick, 6,000 motion, 6,000 steer, 3,000
    steer-input, 2,000 sweeps, 2,000 side, 1,000 release, 2 min 13 s.

**Whole-world route replays on the record (`EM_TEST_WORLD=1`).**

The stage runs on FallEE. The native adapter replaces the original callback
and is handed an EmPlayerLiveActor copied from the world. Every worker is
the original routine on the same world, with the record and the shared
scratch synced around each call (`RecordWorld`). Per frame, the record
(0x320 bytes), the whole 32 MB RAM, the scratch words and the sound/effect
calls must be identical, and the capture routes check every trace row.

| Route | Result |
|---|---|
| 06_hill_slide (PCSX2 beat, RouteReplay from the 05_boxes snapshot) | 206 rows, 109 native 0x1C callbacks, 25 identical sound/effect calls, end (265.699, 185.28, 373.673) |
| slide (synthetic run down the hill) | 175 frames, 116 native callbacks, end (257.335, 185.401, 377.223) |
| 05_boxes crate r4, press 6428 | 80 frames, probe 1 + state2 79, end (228.785, 203.776, 288.268) |
| 05_boxes crate r3, press 6646 | 80 frames, end (219.233, 217.786, 288.309) |
| 11_crevice_prompt tank / pipe end, presses 11697 / 12167 | 93 / 65 frames |
| 13_east_tower, press 13569 | 94 frames |
| crate / stack / vault (synthetic) | 124 / 124 / 164 frames; vault: probe 1 + state3 71, end (228.762, 203.776, 287.768) |
| column | 1,982 points, 3,094 entries identical, 2 rank-excluded (known, PLAYER_CLIMB_SLIDE.md 3) |

The capture ends are unchanged from the older model's. The synthetic slide
and vault ends moved by the model change: previously (257.334, 185.386,
376.373) and x 228.761. Those synthetic runs have no PCSX2 trace; the
beats that do all match their rows. Run times: slide about 5 min; climb
capture 81 s; the rest about 4 min.

## 5. Link set (Makefile and other lanes' tests)

- **What the climb and slide now link against:**
  - em_player_climb.c: em_player_record_helpers.c, em_effect_original.c,
    em_owner_services_original.c, em_player_stage_workers.c,
    em_sdk_math_original.c.
  - em_player_slide.c: those, plus em_player_fall.c,
    em_script_host_workers.c and em_script.c.
- **Builds that must add them** (none is in this lane):
  - the Makefile's `player_states_host_test` line;
  - `tools/test_player_recovery_reference.py`;
  - `tools/test_player_running_jump_reference.py`.

  All three were run here with the set appended (without editing them),
  and all pass.
- **The app.** It does not compile em_player_climb.c or em_player_slide.c
  yet, so COMMON needs nothing until FLOOR is bound.

## 6. What binding still needs

- **The coordinator's closure binder** (em_player.c,
  `player_states_bind`) must fill `EmPlayerClimbLive` / `EmPlayerSlideLive`
  as in section 3, and the fall's EmPlayerLandWorkers `test_001755B0` /
  `test_0017F320` / `pose_clip` with the record entries (one
  `EmPlayerRecordHelpers` and the one `EmPlayerLandScratch`).
- **FLOOR's other blockers** are unchanged by this lane: the move walkers'
  grid pass 0019CB60 and hull lock 001A6440 (census L05), and the live
  effect owner for 001EFD90 (L26).
- **Duplicate translations elsewhere, left alone** (not this lane's files):
  - em_player_floor.c's `em_player_sdk_*` (old model) are duplicates of
    the SDK owners;
  - em_player_recovery.c translates 001B1470, 001026A0, 001028B8 and
    float_to_int inline;
  - em_effect_original.c has its own 001B1470.
