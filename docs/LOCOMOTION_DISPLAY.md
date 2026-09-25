# Locomotion display: the idle and walk states and the gait display

Census lane **L12-locomotion-display** (docs/FIRST_LEVEL_CENSUS.md section 5). Translated 2026-09-23; **live in AREA11 since 2026-09-25** (section 4).

Files:

- `src/game/em_locomotion_display.h`, `src/game/em_locomotion_display.c`: the translations.
- `src/game/em_player_closure_live.c` (`bind_loco`, `em_player_closure_live_footstep`): the live binding over the player record.
- `src/game/em_player_motor.c` `em_player_motor_0017BC40` and `src/game/em_player_foot_stop.c` `em_player_foot_stop_0017B910`: the record-level translations of the `motor` and `foot_stop` workers (section 4).
- `tools/test_locomotion_display_reference.py` (`make test-locomotion-display-reference`), `tools/test_player_loco_workers_reference.py` (`make test-player-loco-workers-reference`) and `tools/test_player_pose_live_reference.py` (`make test-first-control-reference`): the evidence (sections 3 and 4).

In AREA11 (the scenes with the original collision world) 0015B130's `+5 = 0 / 1` entries are 00161020 / 001612D0 over the player record, and the display is the record's evaluated pose. The legacy idle/walk callbacks and their baked display remain only for the scenes without an original world (`EM_SCENE`), and for the frames a port stand-in owns the player (section 4).

## 1. What the lane covers

The player's ordinary standing and walking states are two entries of 0015B130's `+5` table:

- **00161020** runs while `+4 = 1, +5 = 0` (standing). It starts the idle row, counts down the fidget timer, and hands over to the walk entry when the stick turns the player.
- **001612D0** runs while `+4 = 1, +5 = 1` (walking). It steers, runs the speed motor, drives the gait display, translates the player, handles the reversal skid and its surface effects, and takes Use presses.

Both states finish with the same floor tail: the wall probes, a small downward nudge of `+B4`, the floor service, the clearance release and the fall check.

The gait display is **0017C030**, a dispatcher on `+1F0`:

- Mode 1 cross-fades between gait tiers with **0017B660** (anim_matrix_player). That routine seeds the node poses (**00179D20**) twice, before and after the clip change. Between the two seeds it snapshots the node matrices into two pose buffers. It then blends each node's pair with 001C9D50 and rebuilds the world matrices (**00179FF0**).
- Modes 3 to 7 are the stop, re-entry, slide-step and reversal-skid clips.

**0017B490** / **0017B460** choose clip ids from the ELF table D_00248AB0. **0017B5C0** starts the walk clip with an eight-frame blend. **00182D40** is the `+1F0 == 0x17` predicate that 00182DF0 uses.

## 2. Per function

Census status before the lane, then now. "Verified" means that `tools/test_locomotion_display_reference.py` executes the original instructions and compares every byte they write (section 3); "live" that the routine runs in the level smoke (the call counts of section 4).

| Function | Decomp | Census before | Now | What it does |
|---|---|---|---|---|
| 00161020 | NM | stand-in (em_player.c / em_player_frame.c legacy idle) | **live** (`em_loco_00161020`) | Idle state, as described below. |
| 001612D0 | NM | stand-in (em_player.c legacy walk) | **live** (`em_loco_001612D0`) | Walk state, as described below. |
| 0017B660 | NM | stand-in (em_player_frame.c actor_update gait blend) | **live** (`em_loco_0017B660`) | Tier cross-fade, as described below. |
| 0017B460 | BM | stand-in (em_player_frame.c loco_clip_for_tier) | **live** (`em_loco_0017B460`) | The halfword `D_00248AB0[a][b]`, read through the pose host's regions. No table data is embedded. |
| 0017B5C0 | BM | unverified (em_player.c eight-tick entry blend) | **live** (`em_loco_0017B5C0`) | Walk entry, as described below. |
| 00179D20 | BM | missing | **live** (`em_loco_00179D20`) | Node pose seed, as described below. |
| 00179FF0 | BM | missing | **live** (`em_loco_00179FF0`) | The record's TRS matrix into `+D0` (build_trs_matrix), then each node's world matrix. A node with parent -1 uses the root; otherwise its parent's world matrix. Then `+303 = 1`. |
| 00182D40 | BM | unverified (em_player_pose_host.c release tail) | **translated + verified, unbound** (`em_loco_00182D40`; its caller 00182DF0 is not translated on the record) | Returns 1 when `+1F0 == 0x17`, else 0. |
| 0017B490 | BM | verified-unbound (a retired copy with embedded rows) | **live** (`em_loco_0017B490`) | Clip selection, as described below. |
| 0017C030 | BM | verified-unbound (a retired copy of cases 6/7) | **live** (`em_loco_0017C030`, all eight cases) | All eight cases, as described below. |
| 00178B90 | AI | verified-unbound (em_player_recovery) | **live** (the `translate` worker) | The translation along `+C4` by `+38` (worker `translate`). |
| 001749F0 | BM | verified-unbound (em_pose_host_workers) | live (the `arbiter` worker) | anim_clip_arbiter (worker `arbiter`). |
| 00187350 | BM | verified-unbound (em_player_floor, test_player_footstep_reference) | **live** (after every player stage, `em_player_closure_live_footstep`) | The footstep dispatch 0015BCF0 calls after 0015BA50. |
| 00187EE0 | BM | verified-unbound (em_player_floor) | live (inside 00187350, and the ladder dismount) | The footstep sound and effect that 00187350 calls. |
| 00187DC0 | BM | verified-unbound (em_player_floor, test_player_floor_reference) | inside the floor service (first contact with surface 0x5A) | The floor service's first-contact handler (surface 0x5A). |

**00161020 (idle state), by `+6`:**

- **0:** clears `+7`, `+38` and `+25C`, calls 00174A50(p, 12.0) and sets `+28 = 300`.
- **1:** returns at once while D_0028A9A0 is nonzero, or when 001607D0 or 00160220 returns nonzero. If 00174AC0(p, 0) returns nonzero, `+6` advances and 0017B5C0 runs. Otherwise:
  - with `+7 = 1`, when the clip ends (`+200 & 0x1000`) it returns to the idle row at blend 8.0;
  - with `+7 = 0` (and `+236 == 0`, `+235` even), `+28` counts down and at zero requests the fidget clip 0x15D (flags 1, blend 8.0).
- **2:** the same early returns, then 00174AC0(p, 1). Unless `+200 & 0x8000`:
  - `+240 == 0` sets `+6 = 0x63`;
  - otherwise, with `+25D == 0` it switches to the walk (`+5 = 1`, `+1F0 = +1F1 = 1`);
  - otherwise it clears `+204`.
- **0x63 / 0x64:** the return to the idle row.

The common tail runs after the switch: 001764E0, `+B4 += -0.2`, 00175900(p, 1), 001756E0, 001796C0.

**001612D0 (walk state), by `+6`:**

- **0 and 1:** 001607D0 and 00160220 (a nonzero result ends the callback), then 00174AC0(p, 1), 0017BC40, 0017C030 and 00178B90(p, 0). Then, by `+1F0`: 6 or 7 moves to `+6 = 2` (skid) and sets `+28 = 0`; 0 returns to idle.
- **2:** 001607D0 (a nonzero result ends the callback). A Use press (`D_00810E74 & 0x70003B76`) goes to 00184BA0(p, 1); a target that accepts gives 001798D0, `+5 = 0x25` and a return. Then 00174AC0(p, 1). During the skid (`+1F0` 6/7), surface effect 0x80000033 or 0x80000012 fires every 8th tick of `+28`. After the skid:
  - with `+240 != 0` it resumes: `+25C = +23F - 1`, `+38 = D_00248870[+25C]`, and the clip from 0017B490(p, 1, +235, +25C). The clip is requested with 001749A0(flags 0, blend 0) at `+1F1 == 3`, otherwise through the arbiter at half its frame count. Then `+6` decreases and `+1F0 = +1F1 = 1`.
  - with `+240 == 0` it goes to idle.

  Then 0017BC40, 0017C030, 00178B90(p, 0).
- **0x63:** 00174AC0(p, 1), 00178B90(p, 0), and 0017C540 once `+200 & 0x8000` clears.

The common tail follows: 001764E0, then `+B4 += -0.8` when `+23B == 0x35` or `-0.4` otherwise (`+23B` is read after 001764E0), then 00175900(p, 1), 001756E0, 001796C0.

**0017B660 (tier cross-fade):**

- With `+1F1 == 0` it only re-times: if the clip for the current tier differs from `+20C`, the arbiter changes to it at the same cycle fraction.
- Otherwise it seeds the pose (00179D20) and copies every node matrix into D_00288D40. It changes the clip (arbiter, tier `+25C +- 1`) at the matching cycle fraction, seeds again and copies into D_00287F40. Then it blends each node, `node+90 = 001C9D50(old, new, +208)`, runs 00179FF0, and, while `+208 < 1.0`, restores the old clip at the old clock.

**0017B5C0 (walk entry):** 0017B490(p, 1, +235, 1) gives the clip. Then 001C61D0 is staged at 0x70003A20, and the arbiter runs at blend 8.0 and frame = frames − D_00248740[+235].

**00179D20 (node pose seed):**

- Node 0: identity, the Euler angles `+70`, translation `+7C`, and rows scaled by the 4.12 halfwords `+88..`.
- Every other node: R = quat_to_mat3(nlerp(`+30`, `+40`, `+50`)) scaled by `+18..`. L = Euler `+70`, translation `+7C`, and 4.12 scales. Then `node+90` = the product of the rows of L through R (001026D0).

The scratchpad at 0x70003400 / 3440 / 3600 / 3760 is used exactly as the original uses it.

**0017B490 (clip selection):** 001B0070 & 4 gates an override. Commands 1 and 6 read `D_00248AB0[cmd][tbl + 4 * idx]` (override: `tbl + 16`). The other commands read `D_00248AB0[cmd][idx]` (override: 4). `cmd >= 7` has no case in the original, which returns the caller's `$s0`; the port faults there.

**0017C030 (all eight cases):**

- 1: 0017B660 while `+25C != 0`.
- 3: stop clip (0017B490 command 6) at blend 6, or 0017B910; then `+38 = 0`.
- 4: re-entry when `+23F >= 2`, or the end code 0x83.
- 5: the slide step along `+260/+264`, with end codes 0x81/0x82 and `+204 = 2.0`.
- 7: the skid clip and sound 0x137.
- 6: the follow-up clip, `+204 = 0.75` while it waits, and `+C4` turned by pi through 001B1470.

### Where the instructions differ from the NEARMISS C

These three were translated from the decomp's `build/asm`, not from the readable C:

- **00161020:** when D_0028A9A0, 001607D0 or 00160220 stop the state in `+6 = 1` or 2, the routine returns without the floor tail. The C falls through to the tail. 001607D0 receives the record. The C passes it the state byte.
- **001612D0:** the same early returns. `$s1` reaches 001764E0 unchanged, except after the `+6 = 2` resume path, which leaves the 0017B490 clip id in it. 001764E0 tests bit 2 of that register.
- **0017B660:** 0017B490 receives the record as its first argument. The C drops it. The cross-fade frame reloads `+3C` after the first seed. D_00275B40 and `+208` are reloaded for every node.

## 3. Verification

`python3 tools/test_locomotion_display_reference.py` runs the user's pinned ELF in the shared EE interpreter. COP1 and the VU0 macro ops go through `tools/ee_float_model.py` (test_coll_move_reference.FloatEE).

- **Routines executed unmodified:** the ten above, plus 001026D0 and 00103230. The native module translates these two SDK VU0 leaves privately, because no module exports them.
- **Display leaves:** 001029C0, 00102C58, quat_nlerp, quat_to_mat3, build_trs_matrix, copy_qw4, and 001C9D50 with its 001C9E40 and SDK sqrtf. They run as original instructions on the oracle side. The native side calls the verified translations it links (em_owner_services_original, em_pose_host_workers, em_anim_runtime_rest, em_sdk_math_original), so the comparison is of the composite.
- **Callee set:** the test asserts that the jal targets of the routines are exactly the hooked workers, the translated routines and the display leaves.
- **Unit cases:** a synthetic player record and a 1 to 4 node skeleton in the ELF image. Every worker is hooked on the oracle side with a scripted effect (return value, record and node writes), and the native worker applies the same script. Each case compares:
  - all 32 MB of RAM (record, nodes, pose buffers, D_00275B40);
  - the 16 KB scratchpad;
  - the return value;
  - the fault latch;
  - the full worker call sequence with arguments (float arguments as bit patterns, the probe's `$s1`, 001EFD90's `p+B0`/`p+C0`).
- **Captured-image cases:** the playable image and the route beats (quick: 00, 05, 08; full: all 15). D_00275B40 points at the player's `+110`, as anim_bone_array_setup leaves it for the player callback. The animation workers (001749F0, 001749A0, 001C61D0, 001B0070) run as **original instructions on both sides**: a nested call with every hook lifted on the oracle side, and a side interpreter over the native memory on the native side. The real clip bank and the 21 captured player nodes therefore drive 00179D20, 00179FF0, 0017B660 (every `+1F1` / tier / row variant, including the re-time path), 0017B5C0, 0017C030 cases 1/3/6/7 and the idle and walk states. Since the one-owner step (2026-09-24) the heading worker (00174AC0) is bound in these cases as the binder will bind it: the oracle side runs the original 00174AC0 with its whole call tree, the native side runs `em_player_heading_record_worker_result` over the native image (its pad bytes D_00810E57/64/65, camera yaw D_008106A0, 0x70003B8D and 0x70003A20). Three cases per image hold the stick (`heading_walk1_g3`: gait 3 at speed 0.8, the reversal gate's 0x70003A20 store; `heading_walk1_g1`: the standing turn; `heading_walk2_g2`: the moving turn from +6 = 2); the whole RAM and scratchpad comparison covers 0x70003A20, and a negative control (the record-level routine's 0x70003A20 pointed at another word) fails on the scratchpad.
- **Branch coverage:** every conditional branch inside the twelve translated routines must be seen both ways. The one exception is 0017B490's `cmd >= 7` range check, where the original has no case.
- **Leaf sweep:** 001026D0 (aliasing dst with a or b) and 00103230 against the original, over random and special operands (inf, NaN, denormals, ±MAX).
- **Fail-stop:** for each of the seven entry points, every worker, scene pointer and display view is unbound in turn, then the shared-scratch identity is broken, a 001C9D50 fault is latched, and the sqrt worker is removed. Each must return -1 with a fault address, run no worker and write nothing. Unmapped nodes, more than 56 nodes, and `cmd >= 7` are also checked.

Results (2026-09-23):

- Default run: `mode quick: 1,200 of 6,000 unit cases, 55 captured-image cases over 4 images, 300 leaf cases, 227 fail-stop checks`. 4,729 worker calls were compared (284 in captured images, 15 of them the bound 00174AC0), every conditional branch was seen both ways, 8 to 14 s on this machine.
- Full run: `EM_TEST_FULL=1`: `6,000 unit cases, 374 captured-image cases over 17 images, 3000 leaf cases, 227 fail-stop checks`, 23,797 worker calls (1,762 in captured images, 68 of them the bound 00174AC0), about 40 s (2026-09-24).

Quick mode runs a covering sample, then further cases from the same fixed list in batches of 400 until every branch outcome is covered. The run is deterministic.

The oracle found one translation error during development, and it is fixed. The walk tail read `+23B` before 001764E0; the original reads it after.


## 4. Binding (live since 2026-09-25)

`em_player_closure_live_bind` (`bind_loco`) fills the one `EmLocoHost` and installs `em_loco_00161020` / `em_loco_001612D0` as 0015B130's `state[0]` / `state[1]` through the closure's slot runner (`slot_run`: the one 0x70003A20 word, the scene refresh, the SDK fault check). `em_loco_bound` must hold at bind time, or the closure bind fails (and the player stage faults at the area build).

**Workers (all over the one player record):**

| Field | Original | Bound to |
|---|---|---|
| `actions` | 001607D0 | `em_player_weapon_001607D0` over the weapon scene's pad masks (0x70003B74..7E from 001AF470 config 0) and D_00810E70 / E74. D_00810C61, which only its armed forwarding reads (+1F0 0x31 / 0x32 / 0x34 / 0x35, never an idle/walk value), is em_weapon's (census L28): reaching it faults. |
| `ladder` | 00160220 | `em_player_use_00160220` over the closure's Use workers (the interaction host's scan). The legacy `player_use_poll` does not run in AREA11. |
| `heading` | 00174AC0 | `em_player_heading_record_worker_result` (the pad bytes D_00810E57 / 64 / 65, the camera's D_008106A0). |
| `row_request` | 00174A50 | `em_player_stage_row_request` on the stage host. |
| `request`, `arbiter`, `clip_frames` | 001749A0, 001749F0, 001C61D0 | the record pose (`em_pose_host_request` / `_arbiter` / `_clip_frames`). |
| `probes` | 001764E0 | `player_states_wall_probes_s1` with the caller's `$s1` the state passes. |
| `floor`, `fall_check` | 00175900, 001796C0 | `player_states_floor_service` / `player_states_fall_check`. |
| `clearance` | 001756E0 | `player_states_clearance_release` (new): em_player_floor's translation over the record; +236 / +235 are on the record before its 00174A50 runs, as the original stores them first. |
| `motor` | 0017BC40 | `em_player_motor_0017BC40` (new, below). |
| `translate` | 00178B90 | `em_player_recovery_translate_worker`. |
| `use_scan` | 00184BA0 | the interaction host's scan (the original reads only its first argument; 001612D0 passes (p, 1)). |
| `use_accepted` | 001798D0 | `em_player_use_001798D0`. |
| `handoff`, `reentry` | 0017C540, 0017C440 | `em_pose_host_handoff`, `em_player_reentry_worker` (record-level). |
| `foot_stop` | 0017B910 | `em_player_foot_stop_0017B910` (new, below). |
| `sound` | 001FB9F0 | the closure's 001FB9F0 binding (em_sfx; an id outside the exported registry is silent and reported, WP-14). |
| `effect` | 001EFD90 | `player_effect_gap` at the record's +B0 / +C0: counted, nothing spawned (no live effect owner behind the player's spawns, census L26). |
| `wrap`, `mode` | 001B1470, 001B0070 | the closure's 001B1470 and D_008106C8 reads. |

**Scene:** D_0028A9A0 is the transition substate (`em_frame_transition()`), D_00810E74 the canonical pressed word, 0x70003B76 the pad configuration word 1, and the caller's `$s1` is 1 (what 0015B130 leaves), all refreshed before each call.

**Display:** `pose` is the record pose; its regions now also map D_00287F40..D_00289B40 (0017B660's two pose buffers, writable, `EmPlayerRecordPose.pose_buffers`; `EM_POSE_REGION_MAX` is 12 so the special banks a script maps still fit). `rest` is one `EmAnimRest` whose `spad3760` is the pose's and whose sqrt is `em_anim_rest_sqrt_0011E748` over the collision world's SDK context; 0x700034C0 / D0 / E0 are the closure's words. D_00275B40 is the record's +110 (0x8103C0), as 001CB5B0 leaves it for the player.

**The two record-level workers (new; one translation each):**

- `em_player_motor_0017BC40(record, read, context, spad3A20)`: the whole +1F0 switch of 0017BC40 (byte-matched C) over the record, with its tier tables D_0024886C / 70 / 74 / 80 / 90 read by EE address through the exported span 0x248740..0x248ACC and the blend fraction written to 0x70003A20 before it is copied, as the original does. `em_player_motor_tick` (the mirror of the legacy callbacks) is now a thin adapter over it.
- `em_player_foot_stop_0017B910(workers, scratch, d275B40, record)`: 0017B910 (NEARMISS C; the instructions were followed) over the record: anim_eval_skeleton, the D_0024875C row, the planted foot of nodes 17 / 18, the residual and duration +268, 001749A0 for tier 2, the planar distance through the COP1 accumulator and 0011E748, the 0x700036A0 rotation and 0x700038B0 step vector, +260 / +264 and +1F0 = 5; with +236 set, the row default against +20C. 0x70003A20 and 0x700038A0..AC are the one storage the closure shares; 0x70003A24..2C, 0x700036A0..DF and 0x700038B0..BF have no other reader on the route and are the closure's own words. 0017C030 case 5 (the per-tick slide step) is em_locomotion_display's.

`tools/test_player_loco_workers_reference.py` executes the original 0017BC40 (a grid of every mode, sub-mode and tier with the speed below, at and above the target, plus random records) and 0017B910 (the SDK sqrtf and the VU0 leaves unhooked, the four other callees scripted) and compares every record byte, 0x70003A20, the 0017B910 scratch words and the worker calls. Quick: 5,208 motor and 250 foot-stop cases, about 3 s; `EM_TEST_FULL=1`: 8,608 and 3,000.

**00187350 (footsteps).** 0015BCF0's footstep dispatch runs after the animate step of every player stage in AREA11 (`em_player_closure_live_footstep`): em_player_floor's translation over the record's fields (clip +20C, clock +3C, flags +200, +1F0, +25C, +25E, +212, +23A, +23C, +A, +314) and nodes 17 / 18 (+C0), with the frame counter 0x70003B68 and D_00810700. Its workers are the closure's: the shared LCG (00179B90, 00122BB8), 001FBD50(p, id, 0, 300.0) at the record, the counted effect gap; the wet-feet decal 001F0460 and the wading 001E8B90 are fail-stop (no floor on the route sets the wet timer or the water depth). It writes +25E and +212 back to the record. The legacy step clock (`step_crossed` / `footstep_play`) runs only in the legacy display of the scenes without an original world and of the stand-in frames; the mirror-based `player_footstep_0187350` and its mailbox are deleted.

**The port's stand-ins.** The legacy callbacks' non-locomotion owners stay the port's until their lanes land: the door transit, walk-out and movement lock (L18), the examine lock and the legacy director's lock (L21), the armed stance, R2 and melee (L28). Each stage, `live_idle` / `live_walk` first run `player_standin_callbacks` (the legacy preamble of the old callbacks); when one holds the player it consumes the callback, the record waits in 00161020 case 0 (`+5 = +6 = +1F0 = 0`, the tail 00182DF0 writes when a scripted owner releases the player), and the legacy display shows it. 001607D0 enters the armed stances `+5 = 0x1D..0x22` on the stance buttons; those states run `live_stance` (the same stand-in, and 00161020 once none holds), since the stances' own workers are not bound (L28).

**What was replaced (deleted or no longer reached in AREA11):**

- em_player.c: the reversal glue and `em_player_reversal.{c,h}` (deleted: a partial second translation of 0017C030 cases 6 / 7, 001612D0 case 2 and the 00174AC0 gate; the one translation is this module's), `tests/player_reversal_host_test.c` and `tools/test_player_reversal_reference.py` (retired, rule 4: superseded by test_locomotion_display_reference, which executes 001612D0 / 0017C030 / 00174AC0 whole; its interpreter moved to `tools/player_callback_oracle.py` for the four oracles that build on it), the mirror 00187350 (`player_footstep_0187350`, never called) and its mailbox.
- In AREA11 the legacy idle cycle, eight-tick entry, gait blend, stop / re-entry / foot-stop metadata and the legacy step clock no longer run (the call counts below: `player_move_callbacks`, `footstep_play`, `player_pose_foot_stop_begin`, `em_player_motor_tick` 0 calls). They stay only for the scenes without an original world.

**Evidence.**

- `make test-first-control-reference` (new): the New Game control fixture with the run-stop interruption (30 held input ticks, 18 released, 8 held again) against the original's actor bytes from save state 04 (`collision_run_poll.json`). All 56 callbacks equal the original byte for byte in the pose source (+20C, +3C, +2C, +200), the locomotion state (+4..+7, +A, +38, +1F0, +1F1, +204, +208, +212, +235, +236, +23A..+23C, +23F, +240, +244, +248, +24C, +25C, +25E, +260..+268, +314) and the feet position (native +B0..+B8 against the original's +A0..+A8). The heading +C4 is exact except at callbacks 17, 19 and 20, one callback after the live camera's heading D_008106A0 differed from the original's sample by one ulp (the camera lane's residual, CAMERA_LIVE.md); +28 differs by a constant (the save state was taken 58 idle callbacks after the fade clear, the fixture presses at once).
- `EM_STARTUP_TEST=newgame-control`: 30 input ticks travel **9.599849**, the original's first-control displacement (the legacy walk travelled 9.599989). The re-entry's final XZ is (238.753983, 226.391403) against the original (238.753982, 226.391403).
- The level smoke (`make test-level-smoke-full`): all 15 live phases PASS against the captures with the translated idle/walk navigating every beat.
- Call counts (a private `-finstrument-functions` build over the full smoke, deleted): 00161020 1,435, 001612D0 2,496, 0017C030 2,494, 0017B660 2,006 (251 cross-fades: 00179D20 502, 00179FF0 251, 001C9D50 5,271), 0017B5C0 30, 0017B490 / 0017B460 2,107, 0017BC40 2,494, 0017B910 10, 001607D0 / 00160220 3,667, 00174AC0 3,663, 00178B90 2,683, 001756E0 3,776, 0017C440 6, 0017C540 11, 00187350 10,706 (133 steps, 79 surface effects); the legacy `player_move_callbacks`, `footstep_play`, `em_player_motor_tick`, `player_pose_foot_stop_begin` and `player_use_poll` 0.
- Frame order (`tools/compare_frame_order.py`, walk04 / idle04 / st03 / cut15 over post-control windows of a newgame-control trace): unchanged, PASS (the stage is inside 0015BCF0).

## 5. Limits (honest)

- **Stand-ins.** The door sequence, the examine and director locks, and the armed stances / R2 / melee still pre-empt the idle/walk states with the legacy callbacks and display (L18, L21, L28). A stance button press spends one stage in `+5 = 0x1D..0x22` before the stand-in or the idle takes it.
- **Case coverage in play.** The smoke reaches every routine above; which 0017C030 cases it reached is not recorded (the reversal skid 6 / 7 needs a stick reversal above speed 0.5, which no route beat makes). The skid is verified only by the instruction oracle.
- **Effects and decals.** 001612D0's skid effects and the footstep's surface effects go to the counted effect gap (L26). The footstep's decal 001F0460 and wading 001E8B90 fault if reached.
- **Legacy scenes.** The scenes without an original world keep the legacy callbacks, `em_player_motor_tick` (now an adapter over the record-level motor), the mirror foot stop `em_player_foot_stop_begin` / `_tick` and the legacy re-entry / stop metadata. They are outside the first level.
- **00182D40** stays unbound: its caller 00182DF0 is not translated on the record (the pose host's release mirrors it).
- **D_00275B40** is the player's +110 by construction in the closure (001CB5B0's value for the player); a canonical D_00275B40 / D_00275B48 pair for every host (census L33, 001CB5B0) is not built.
