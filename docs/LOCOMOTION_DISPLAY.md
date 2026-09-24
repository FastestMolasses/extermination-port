# Locomotion display: the idle and walk states and the gait display

Census lane **L12-locomotion-display** (docs/FIRST_LEVEL_CENSUS.md section 5). Date: 2026-09-23.

Files (new, unbound):

- `src/game/em_locomotion_display.h`, `src/game/em_locomotion_display.c`: the translations.
- `tools/test_locomotion_display_reference.py`: the original-instruction oracle (`make test-locomotion-display-reference` once the lead adds the target, section 5).

Nothing here is wired into the live app yet. The live idle and walk look still comes from the legacy code named in section 4, which the coordinator replaces.

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

Census status before, then after this lane. "Verified" means that `tools/test_locomotion_display_reference.py` executes the original instructions and compares every byte they write (section 3).

| Function | Decomp | Census before | After | What it does |
|---|---|---|---|---|
| 00161020 | NM | stand-in (em_player.c / em_player_frame.c legacy idle) | **translated + verified, unbound** (`em_loco_00161020`) | Idle state, as described below. |
| 001612D0 | NM | stand-in (em_player.c legacy walk) | **translated + verified, unbound** (`em_loco_001612D0`) | Walk state, as described below. |
| 0017B660 | NM | stand-in (em_player_frame.c actor_update gait blend) | **translated + verified, unbound** (`em_loco_0017B660`) | Tier cross-fade, as described below. |
| 0017B460 | BM | stand-in (em_player_frame.c loco_clip_for_tier) | **translated + verified, unbound** (`em_loco_0017B460`) | The halfword `D_00248AB0[a][b]`, read through the pose host's regions. No table data is embedded. |
| 0017B5C0 | BM | unverified (em_player.c eight-tick entry blend) | **translated + verified, unbound** (`em_loco_0017B5C0`) | Walk entry, as described below. |
| 00179D20 | BM | missing | **translated + verified, unbound** (`em_loco_00179D20`) | Node pose seed, as described below. |
| 00179FF0 | BM | missing | **translated + verified, unbound** (`em_loco_00179FF0`) | The record's TRS matrix into `+D0` (build_trs_matrix), then each node's world matrix. A node with parent -1 uses the root; otherwise its parent's world matrix. Then `+303 = 1`. |
| 00182D40 | BM | unverified (em_player_pose_host.c release tail) | **translated + verified, unbound** (`em_loco_00182D40`) | Returns 1 when `+1F0 == 0x17`, else 0. |
| 0017B490 | BM | verified-unbound (em_player_reversal, embedded rows) | **re-translated + verified here** (`em_loco_0017B490`) | Clip selection, as described below. |
| 0017C030 | BM | verified-unbound (em_player_reversal: cases 6/7 only) | **fully translated + verified here** (`em_loco_0017C030`) | All eight cases, as described below. |
| 00178B90 | AI | verified-unbound (em_player_recovery) | unchanged; binding note | The translation along `+C4` by `+38` (worker `translate`). |
| 001749F0 | BM | verified-unbound (em_pose_host_workers) | unchanged; binding note | anim_clip_arbiter (worker `arbiter`). |
| 00187350 | BM | verified-unbound (em_player_floor, test_player_footstep_reference) | unchanged; binding note | The footstep dispatch 0015BCF0 calls after 0015BA50. |
| 00187EE0 | BM | verified-unbound (em_player_floor) | unchanged; binding note | The footstep sound and effect that 00187350 calls. |
| 00187DC0 | BM | verified-unbound (em_player_floor, test_player_floor_reference) | unchanged; binding note | The floor service's first-contact handler (surface 0x5A). |

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

## 4. Binding (for the coordinator chain)

The two states are `EmPlayerStateCallback`s over the live record `live.a` (em_player.c). Bind them with the other state callbacks:

```c
EmLocoHost loco;                                   /* one instance, lives as long as the binding */
binding.stage.state[0] = em_loco_00161020;  binding.stage.state_context[0] = &loco;   /* idle */
binding.stage.state[1] = em_loco_001612D0;  binding.stage.state_context[1] = &loco;   /* walk */
```

**Workers (`loco.workers`, one shared `context`).** Each is a verified translation. The ones that exist only over mirrors need a thin live-record adapter.

| Field | Original | Verified translation to bind |
|---|---|---|
| `actions` | 001607D0 | `em_player_weapon_001607D0` (em_player_weapon_states_a, test_player_weapon_states_a_reference). The signature already matches. |
| `ladder` | 00160220 | `em_player_use_00160220` (em_player_use_dispatch; live over the record since the Boxes step, em_player_closure_live_use_press). This worker binds it when L12 binds 00161020 / 001612D0 |
| `heading` | 00174AC0 | `em_player_heading_record_worker_result` (em_player_heading_record.h; context an `EmPlayerHeadingRecord`, `world.spad3A20` the shared 0x70003A20 word). The live turn in em_player.c (em_player_heading / em_player_reversal, on mirrors) stays until 001612D0 runs over the record. |
| `row_request` | 00174A50 | `em_player_stage_row_request` (em_player_stage_workers; the EmPlayerStageHost context). |
| `request`, `arbiter`, `clip_frames` | 001749A0, 001749F0, 001C61D0 | `em_pose_host_request`, `em_pose_host_arbiter`, `em_pose_host_clip_frames` (EmPoseHost context). |
| `probes`, `clearance` | 001764E0, 001756E0 | `em_player_wall_probes`, `em_player_clearance_release` (em_player_floor) over the live record. The `s1` argument feeds `EmPlayerProbeScene.inherited_s1` for that call. |
| `floor`, `fall_check` | 00175900, 001796C0 | The floor service and fall check of em_player_floor, the same adapters `live` uses for the fall states. The floor service reaches 00187DC0 on first contact. |
| `motor` | 0017BC40 | em_player_motor (test_player_motor_reference). Needs a live-record adapter. |
| `translate` | 00178B90 | `em_player_recovery_translate_worker` (EmPlayerRecoveryLive context; set `shared3A20` to the pose globals' 0x70003A20 word). |
| `use_scan` | 00184BA0 | em_interaction_scan through em_area11_interaction_host's use worker. The census says the published list holds only the panel and the elevator (W22). |
| `use_accepted` | 001798D0 | Not yet a faithful translation. The census 7.2 correction marks the live `player_pose_use_accepted` as a stand-in. A translation must zero `+38`, `+21C`, `+25C`, `+5`, `+6` and `+1F0`, then call 00174A50(p, 0). This worker stays a gap. |
| `handoff` | 0017C540 | `em_pose_host_handoff` (em_player_reaction_0017C540, test_player_reentry_reference). |
| `reentry` | 0017C440 | Gap. The only verified form is em_player_motor's re-entry metadata (`em_player_reentry_begin` / `_tick`, test_player_reentry_reference), which works on a mirror. A record-level translation is still needed before this worker can be bound. |
| `foot_stop` | 0017B910 | em_player_foot_stop (test_player_foot_stop_reference). Needs a live-record adapter. |
| `sound` | 001FB9F0 | em_sfx_bank (test_area11_sfx_reference). |
| `effect` | 001EFD90 | em_effect_original's 001EFD90 with `(id, p+B0, p+C0)`. |
| `wrap` | 001B1470 | `em_player_001B1470` (em_player_stage_workers). |
| `mode` | 001B0070 | 001B0070 returns D_008106C8 (em_head_sprite_original.h). Bind a one-line getter of `EmPlayerStageGlobals.d8106C8`, the same word 0015D100 reads. |

**Scene (`loco.scene`).**

- `d28A9A0`: the D_0028A9A0 halfword. Its owner is the 0x28A9A0 machine, which also drives the letterbox bars and fades.
- `d810E74` and `spad3B76`: the button mask D_00810E74 and the pad word 0x70003B76. The walk ANDs them before the 00184BA0 Use scan. Both must be the storage em_input's 001B5940 pad-block translation writes (the halfword the original reads).
- `caller_s1`: point at `EmPlayerProbeScene.inherited_s1`.

**Display (`loco.display`).**

- `pose`: the EmPoseHost the arbiter uses. It must map, by EE address:
  - the player record (its `+110` node-pointer array) and its 21 node records, writable;
  - D_00287F40..D_00289B40 (the two pose buffers), writable;
  - D_00248740, D_00248870 and D_00248AB0, plus the rows it points at (0x248A00..0x248AD0), read-only, loaded from the user's ELF (for example with an EMCR-style exporter). Nothing is embedded.
- `rest`: the EmAnimRest of em_anim_runtime_rest with the sqrt worker bound (`em_anim_rest_sqrt_0011E748` over one EmSdkMathContext). `rest->world.spad3760` must equal `pose->globals->spad3760`; `em_loco_bound` refuses otherwise.
- `d275B40`: the D_00275B40 word. It is the same storage as `EmAnimRestWorld.d275B40`, and 001CB5B0 (anim_bone_array_setup) sets it to the player's `+110` before the callback.
- `pose->globals->spad3A20` is the one 0x70003A20 word. The stage lane, the fall lane and the recovery adapters must share it.

**What each piece replaces in the live app.**

- **00161020 / 001612D0** replace the legacy idle and walk callbacks: em_player.c's ordinary locomotion (`g.loco_*`, `g.idle_*`, the eight-tick entry `g.loco_entry_ticks` at em_player.c ~1864..1900, and the reversal glue at ~1248..1290 and ~1725..1760), and the idle-cycle block in em_player_frame.c (~287..). They also replace `player_use_poll`'s emulation of their Use checks (em_player_pose_host.c ~65..90).
- **0017C030 / 0017B660 / 00179D20 / 00179FF0** replace em_player_frame.c `actor_update`'s gait blend (the `walk_w` ramp and the tier lerp, ~210..260), `loco_clip_for_tier` (~54..70), and em_player_reversal's partial 0017C030 cases 6/7 and its embedded clip rows.
- **0017B5C0** replaces the eight-tick entry blend in em_player.c.
- **00182D40**: 00182DF0 calls it twice. The live `player_pose_release` / `reset_default_state` mirror in em_player_pose_host.c assumes it returns 0. A faithful 00182DF0 must call `em_loco_00182D40(record)`.
- **00187350 / 00187EE0 (footsteps):** remove the `footstep_play` triggers in em_player_frame.c (~262..282: a loco-cycle playhead against hand-listed trigger frames) together with `footstep_play` itself. Call `em_player_footstep_tick` once per frame after 0015BA50, in 0015BCF0's order (after 0015CF90 / 0015CBA0). Build its EmPlayerStepActor from the live record (`+B0`, `+C0`, `+3C`, `+38`, `+9C`, `+250`, `+200`, `+20C`, `+25C`, `+1F0`) rather than from `g.pos` / `g.loco_*`. `player_footstep_0187350` (em_player.c ~2237) already calls the translation, but it feeds it port mirrors.
- **00187DC0** runs inside the bound floor service. Nothing separate is needed.

Once bound, `player_states_missing()` should report the idle and walk entries as bound. The ordinary walk (census beat 00 onward) then runs through `em_player_stage_0015B130` → `em_loco_001612D0`.

## 5. Makefile (for the lead)

Test target:

```make
.PHONY: test-locomotion-display-reference
test-locomotion-display-reference:
	python3 tools/test_locomotion_display_reference.py
```

Sources to add to `COMMON` when the states are bound. Skip any that another lane has already added:

```make
           src/game/em_locomotion_display.c src/game/em_pose_host_workers.c \
           src/game/em_anim_runtime_rest.c src/game/em_player_stage_workers.c \
           src/game/em_player_reaction.c src/game/em_stream_lanes_original.c \
```

A trial link of the current app command plus these six files (a private lane build, removed afterwards) built with zero warnings and no duplicate symbols on 2026-09-23.

## 6. Limits (honest)

- **Unbound.** Nothing in this lane runs in the live app. The census rows stay stand-in / unverified for the live path until the coordinator binds them, and the legacy stand-ins named in section 4 are still what the player sees.
- **Workers are scripted in the unit cases.** The oracle proves that each routine makes exactly the original's calls, in order, with the original's arguments, and reacts to their results exactly as the original does. It does not re-verify the workers. Each has its own oracle, named in section 4.
  - 001798D0 has no faithful translation yet (census 7.2).
  - 00160220 has no exported translation (lane L09).
  - The heading, motor and foot-stop translations exist only over mirrors.
  - 0017C440 exists only as re-entry metadata.
  - So the full idle/walk path cannot be bound until those gaps close.
- **Captured-image cases run perturbed states.** The captured player stands in every route image (`+5 = 0`, `+6 = 1`, `+1F0 = 0`). The display paths are reached by setting `+1F0` / `+1F1` / `+25C` / `+208`. The routines themselves are executed, over the real bank and nodes, but these are not recorded frames of a walk. No per-frame trace of 0017B660 across a real tier change was compared. That needs a PCSX2 capture during the ramp from walk to run (not run here: this lane may not launch PCSX2).
- **001C9D50's sqrtf error path is not exercised.** A negative argument takes a soft-float path that em_sdk_math_original faults on (docs/SDK_MATH_ORIGINAL.md section 7). The unit cases that reach the blend use rigid nodes (unit quaternions, unit scales). The 16 captured images never reached that path. On a real skeleton it would surface as a fault, not as a wrong value.
- **0017B490 with `cmd >= 7`** faults natively where the original returns its caller's `$s0`. No caller on the route passes it.
- **D_0028A9A0** is read as the original reads it (a halfword, nonzero stops the idle `+6 = 1` work). This lane does not interpret it further.
