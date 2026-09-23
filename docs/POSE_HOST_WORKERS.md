# Pose host workers (clip requests, channel sampling, skeleton evaluation)

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "pose-host-workers" (build lane `b6-pose-host-workers`). Every player
state calls these routines. They request a clip, seed the transition to it,
step and sample the node channels, and evaluate the skeleton's world
matrices. The stage workers (PLAYER_STAGE_WORKERS.md) call them through
`EmPlayerStageCallees`. The fall, hang, reaction, recovery, major2, closure,
climb and slide lanes call them through their own worker slots.

Code: `src/game/em_pose_host_workers.c/.h`. Oracle:
`tools/test_pose_host_workers_reference.py`. **The module is built and
tested but not wired.** Section 3 lists what the coordinator binds.

## 0. What already existed (checked first)

- `em_pose_bank.c`, `em_pose_transition.c`, `em_player_pose.c` and
  `em_player_pose_host.c` model 001749A0/001749F0, 001C8D50/001C86A0 and the
  per-tick channel step. They work over an **exported** bank (`EmPoseBank`,
  decoded keys as host floats) and a separate channel state
  (`EmPoseChannels`). They do not work over the raw record, the EE clip bank
  or the node records at record +110. They use host float arithmetic (no
  `em_ee_float.h` call). They cannot be bound as raw-record stage workers
  without a lossy re-projection, so no adapter over them was written.
- There are no `em_anim*` files.
- 0017C540 is already translated and verified as
  `em_player_reaction_0017C540` (tools/test_player_reaction_reference.py). This
  module only adapts it (`em_pose_host_handoff`), and the oracle checks it
  again against the original.
- 001C64F0 (anim_advance_time) is already translated as
  `em_player_stage_anim_advance`. anim_clip_init's zero-blend path reaches it
  through `EmPoseHostCallees.advance` (`em_pose_host_player_advance`).
- 001281C0 (float_to_int) and 001B1470 are reused from
  `em_player_stage_workers.c`. 0011E620 (atan2f) is
  `em_sdk_math_original_float_0011E620`, which has the same signature as
  `EmPoseHostCallees.atan2`.
- The SDK matrix routines are reused from `em_owner_services_original.c`
  (already in the game's source list, verified by
  tools/test_owner_services_reference.py): `em_owner_services_identity_001029C0`,
  `_euler_00102C58`, `_build_trs_matrix` (001C94B0, which calls
  `_rotate_x_00102B08` / `_rotate_y_00102BB0` / `_rotate_z_00102A60` and
  `_translate_00102918`). This module has no rotate worker and no matrix
  code of its own; `em_pose_host_build_trs_matrix` and `em_pose_host_euler`
  are thin adapters with the lane slot signatures. Those routines move every
  word with `memcpy`, so the adapters pass the callers' `uint32_t` matrices
  through a `float *` cast unchanged, and they read each argument where the
  original does (see section 2). (`em_effect_original.c` also has a 00102C58
  and a static rotation, but it is not in the game's source list; the
  owner-services exports are.)
- 00128250 (float to unsigned) is reused from `em_stream_lanes_original.c`
  (`em_stream_lanes_00128250`, verified by tools/test_stream_lanes_reference.py
  and again here). That file is not yet in the game's source list (one
  addition, section 3). Other copies exist as private statics (for example
  em_shadow_original.c's `f2u_00128250`), so they cannot be bound.

Everything else below is a new translation. It was read from the original
instructions (the decomp's build/asm): 001C87C0, 001C92C0, quat_nlerp,
001C9940 and 00178910 are NEARMISS C, 001C8710 is an asm-word file, and
anim_eval_skeleton is an INCLUDE_ASM placeholder (it has no C).

## 1. What the originals do

All record offsets are offsets into the raw record (`EmPlayerLiveActor`
bytes for the player). The fields are: +C node count, +2C current clip
(halfword, 0x8000 = transition mark), +3C clip clock, +40 clip bank (EE
address), +60 scale, +B0 position, +C0 rotation, +D0 world matrix, +110.. node
pointers, and +20C the requested clip. A node record (0xD0 bytes) holds the
rotation keys +30/+40 with their fraction +50, rate +54 and remaining time
+60; translation +0 with velocity +C and remaining time +58; scale +18 with
velocity +24 and remaining time +5C; the step counters +66/+68/+6A; the
parent +64; the Euler angles +70; the local translation +7C; the 4.12 scales
+88/+8A/+8C; the hold frame +8E; and the world matrix +90.

| Original | What it does |
|---|---|
| 001749A0(p, clip, flags, blend) | With flags 0 and +20C equal to clip (as sign-extended halfwords), it returns 1 and writes nothing. Otherwise it sets +20C = clip, calls anim_clip_init(p, clip, blend, 0.0) and returns 0. |
| 001749F0 anim_clip_arbiter(p, clip, blend, frame) | It calls anim_clip_init first. A different +20C is then replaced and the call returns 1; an equal one returns 0. |
| 001C6120(bank, clip) | Header = bank + (the directory word at bank + 4 + 4·(clip & 0x7FFF), rounded down to a word). |
| 001C8480 anim_clip_resolve | D_00275BF8 = header; D_00275BF4/BF0/BEC = header + its words +8/+C/+10 (the rotation, translation and scale sections). |
| 001C61D0(bank, clip) | D_00275BF8 = header; it returns the frame count, the unsigned halfword +2. |
| 001C67E0 anim_clip_init(p, clip, blend, frame) | +2C = clip \| 0x8000, then resolve. With a zero blend (an EE equality compare): +3C = 1.0, node 0 +8E = 00128250(frame), anim_sample_bones(new frame, prev 1.0), then 001C64F0(p, 1.0). Otherwise: +3C = blend, the same +8E, and anim_sample_bones(frame, blend). |
| 001C63E0 bone_init_default_2(p, clip) | +2C = clip, then resolve. +3C = (float) the frame count. For each node i: +64 = the header's halfword at +20 + 4i (the parent), the scales = 0x1000, and +70.. and +7C.. = 0. Then 001C8710(p+110, n, 0.0). |
| 001C8710(bones, n, frame) | Each node samples rotation (001C8F10), translation (001C90D0) and scale (001C92C0) directly at float_to_int(frame). |
| 001C8F10 / 001C90D0 / 001C92C0 | Starting from the node's stream (the section's directory word i), the walk counts 12-byte records into the step counter (seeded 1) until key(+A) ≤ t < next key(+16). It then decodes the key pair (001C84D0: four 20-bit lanes; 001C85D0: three 26-bit lanes, staged in scratchpad 0x70003600). Rotation: +60 = next − t, +54 = 1/(next − key), +50 = +54·(t − key). Translation and scale: velocity = (next − key)/span through 001C86A0 (the reciprocal goes to scratchpad 0x70003A3C), remaining time = next − t, value += velocity·(t − key). |
| 001C87C0(bones, n, dt) | Per node and channel, remaining −= dt. At or below zero the stream steps one record: decode, new span, remaining += span, counter + 1, and the in-between value restarts at −old·rate. Otherwise the value moves by dt·rate. The scale crossing reads the key's 0x8000 flag (the last crossing decides). If that flag is set, or D_008106F3 is nonzero, every node's +54 and its translation and scale velocities are cleared. |
| 001C8D50 anim_sample_bones(bones, n, new_t, prev_t) | Per node: freeze the current rotation (quat_nlerp of +30/+40 by +50, into +30). Sample the target at new_t through the scratch channel record D_008111F0. Seed a prev_t-long transition: +40 = target rotation, +60/+58/+5C = prev_t, +50 = 0, +54 = 1/prev_t, step counters copied, translation and scale velocities = (target − current)/prev_t. |
| 001CA0A0 quat_nlerp | t is clamped above at 1.0. The dot product is accumulated through the COP1 accumulator. A negative dot gives b·t − a·(1−t), otherwise a·(1−t) + b·t. There is no normalization. |
| 001CA1C0 quat_to_mat3 | The nine products are staged at 0x70003760..88, then the rotation rows are formed. The translation row is copied raw, and m[15] = 1.0. |
| 00128250 | Float to unsigned (the soft-float class rules of 001278C0). Bound: `em_stream_lanes_00128250`. |
| 001C94B0 build_trs_matrix | Identity (001029C0), rotated about X, Y, then Z; rows 0..2 × scale (VU broadcast multiply); row 3 += position (00102918). Bound: `em_owner_services_build_trs_matrix`. |
| 00102C58(out, in, angles) | Rotate about Z (from in), then Y, then X, in place. Bound: `em_owner_services_euler_00102C58`. |
| 001C9940(bones, n, root) | Per node: R = quat_to_mat3(nlerp(+30, +40, +50), +0) with rows × the scale words, in 0x70003400. L = identity rotated by +70 (00102C58), row 3 = +7C.., rows × 2^-12·the 4.12 scales, then L = R × L, in 0x70003440. World +90 = parent world × L, or root × L for parent −1. |
| 001C6DA0 anim_eval_skeleton(p) | +D0 = build_trs_matrix(+B0, +C0, +60). Node 0 with R = identity (its channels are not used). Nodes 1..n−1 as in 001C9940, with root +D0. |
| 001C68C0(p) / 001C6960(p) | build_trs_matrix into +D0 (or the identity for 001C6960), then 001C9940(p+110, +C, p+D0). |
| 00178910(p, arg) | Ledge-top column search. Query point 0x700038B0 = the sweep hit backed off along its normal (4·normal when +316 is set), with y = 20.5 + +B4 and w = 1.0. 0019BC40 fills the column table. The first column with flags bit 0, aux < 0.62831855 and \|y − height\| < 1.0 (0x70003A20) returns 1. With arg set it first stores +2E0/+2E8 = hit + 1.5·normal, +2E4 = height − 20.5, and +218 = 001B1470(4.712389 + atan2(−nz, nx)). No column returns 0. |

## 2. The translation

- Every routine works on EE addresses. `EmPoseHost.region[]` maps EE ranges
  to host bytes: the clip bank, the node records and the player record's
  node pointers. The globals the originals keep outside the records are
  `EmPoseGlobals` fields. D_00275BF8/BF4/BF0/BEC and D_008111F0 are held by
  value: no other port module holds them (grep), and in the decomp only the
  clip routines of this module touch them. D_008106F3, the scratchpad words
  0x70003400/3440/3600/3760/3A3C/38B0/3A20 and the column table are
  **pointers** to storage the host shares with other modules (section 3).
  Every pointer must be bound; a null one faults before any write.
- Float arithmetic, compares, the COP1 accumulator forms and the VU0 lanes all
  go through `em_ee_float.h` on bit patterns.
- **Fail-stop.** Before its first write, every entry checks that every
  worker it can reach is bound, and that every node record, the node-pointer
  array, the clip header and its sections, and every parent world matrix are
  mapped. If not, it returns −1 with nothing written. A key walk that leaves
  the mapped regions, where the original would read unrelated memory, also
  faults, and so does a worker that returns a negative value. Writes already
  made at that point stay, in the original's order.
- **Argument read order.** The bound 00102C58 and build_trs_matrix read each
  angle (a word read) just before its rotation, read a source row just before
  its result row is stored, and read the scale and the position after the
  rotations, as the originals do. The adapters pass the callers' own
  pointers, so an argument inside `out` sees the same values in both. The
  oracle checks the angles inside `out`, `in == out`, `in` four words above
  `out`, the position as row 3 and the scale as row 0, and a rotation
  straddling the end of `out`. Contract: the rows, the scale and the
  position are quadword reads in the original, which ignore the low four
  address bits; the adapters read at the exact pointer. They agree for
  16-byte-aligned EE addresses, which every caller passes (record +60 / +B0 /
  +D0 / node +90 of 16-byte-aligned records, and the scratchpad matrices).
- **One deliberate refusal.** 001B1470 subtracts 2π while the angle is above
  π (then adds 2π while it is at or below −π). With the EE's rounding
  (`tools/ee_float_model.py`, the same results as `em_ee_float.h`), that step
  leaves every angle of biased exponent 0x9A..0xFE unchanged, so the original
  never returns; an exponent-0xFF encoding clamps to 0x7F7FFFFF and then
  stalls the same way. Every smaller angle is changed by the step and
  reduces, up to about 2^24 steps at exponent 0x99. So if 00178910's heading
  4.712389 + atan2(...) has a biased exponent ≥ 0x9A, the native side
  returns −1, and nothing else differs from the original. The atan2 worker
  is pure and its arguments come from the second sweep-hit read, so the
  native calls it just before the record stores (not after them, as at
  00178B00), which leaves the record unwritten on the refusal; 0x70003A20 is
  still written in the original order. A real atan2 (|result| ≤ π) never
  comes near the threshold.

## 3. Binding (for the coordinator)

One `EmPoseHost` for the player (one `EmPoseGlobals`, shared by every pose
host if more are made).

**Regions.** The RAM ranges holding the player's clip bank (+40), its node
records (0xD0 each) and its node-pointer array. **Storage constraint:** the
core routines and the actor adapters read the node pointers from the
record bytes themselves (`actor->bytes + 0x110`), while the view adapters
and the D_00275B40 readers reach them through `region[]`. The region that
covers the player's EE address + 0x110.. must therefore map
`actor->bytes + 0x110..` (the actor's own bytes), not a separate RAM copy;
otherwise a write by one path is not seen by the other.

**Globals** (`EmPoseGlobals`):

| Field | Bind to |
|---|---|
| `d275BF8/BF4/BF0/BEC`, `d8111F0` | By value, in the one `EmPoseGlobals` (no other module holds them). |
| `d8106F3` | The coordinator's D_008106F3 byte, the same storage `EmAreaScriptWorld.d8106F3` points at (the area script writes it; 001C87C0 reads it). |
| `spad3A20` | `&EmPlayerStageGlobals.spad3A20` of the player's stage host (the stage workers 0021C440 / 0021D6C0 store their atan2 there). Other lanes that keep 0x70003A20 (weapon states B's `spad3A20` pointer; the recovery, ladder-climb and weapon states A copies by value) should point at the same word; the by-value copies are those lanes' own binding question. |
| `column` | The one column table the coordinator keeps for 0019BC40's results (D_70003170 flags, D_700030F0 heights, D_00282250 aux, count 0x700031E0). Today the port has no persistent copy: the floor lane's 001796C0 translation fills a stack table per call (em_player_floor.c). 00178910 itself rebuilds the table through 0019BC40 before it reads it and leaves it behind, as the original does. Other originals also read D_70003170 (the decomp shows it in 0013CD50, 0015EC50, 00178080, 0017C860, 0017D080 and others); whether any of them reads it without rebuilding it first is for their lanes to establish, which is why this field is a pointer to shared storage and not a private copy. |
| `spad3400`, `spad3440`, `spad3600`, `spad3760`, `spad3A3C`, `spad38B0` | The coordinator's scratchpad words (16 / 16 / 4 / 11 / 1 / 4 words). Each routine here writes them before reading them, and the other port modules that name them (em_script_host_workers' `spad3400` / `spad3600`, the lanes' `s38B0`) do the same, so a private array per host is also correct; sharing one scratchpad image only matters for comparing the scratchpad bytes with a capture. |

The oracle binds these exactly this way: `d8106F3` points into the native
RAM image's own byte, and `spad3A20` points at the stage host's
`EmPlayerStageGlobals.spad3A20`.

**Callees:**

| `EmPoseHostCallees` | Bind to |
|---|---|
| `advance` / `advance_context` | `em_pose_host_player_advance` with the player's `EmPlayerStageHost` (its clip workers bound as in the next table). |
| `column` (0019BC40) | The same worker as `EmPlayerFloorWorkers.column`. |
| `ledge_hit` | The hit left by the sweep 0019AFE0 that precedes 00178910: 0x700031B0/B8 and *(0x700031D0) +24/+2C. It shares state with `EmPlayerHangWorkers.sweep` (em_player_hang.h). |
| `atan2` (0011E620) | `em_sdk_math_original_float_0011E620`. |

**Stage workers** (`EmPlayerStageCallees`, context = the `EmPoseHost`):
`bone_init` = `em_pose_host_stage_bone_init`, `clip_init` =
`em_pose_host_stage_clip_init`, `clip_resolve` =
`em_pose_host_stage_clip_resolve`, `skeleton_frame` =
`em_pose_host_stage_skeleton_frame`, `w001C8710` = `em_pose_host_stage_8710`,
`w001C87C0` = `em_pose_host_stage_87C0`, `sample_bones` =
`em_pose_host_stage_sample_bones`, `request` = `em_pose_host_stage_request`.
The `clip_resolve` adapter runs 001C8480 with its side effects and copies the
event table into `EmPoseHost.events` (at most 256 pairs; more faults).

**State-lane slots** (context = the `EmPoseHost`, actor = the player). Every
assignment below was type-checked against the lane headers (a `-Werror`
syntax-only compile of the assignments, 2026-09-23):

| Lane struct (header) | Slot = worker |
|---|---|
| `EmPlayerLandWorkers` (em_player_fall.h) | request = `em_pose_host_request`, arbiter = `_arbiter`, clip_frames = `_clip_frames`, skeleton = `_eval_skeleton` (anim_eval_skeleton, not 001C68C0), trs = `_build_trs_matrix`, handoff = `_handoff` |
| `EmPlayerHangWorkers` (em_player_hang.h) | request, skeleton = `_skeleton` (001C68C0), handoff, ledge_top = `_ledge_top` |
| `EmPlayerMajor2Workers` | request |
| `EmPlayerClosureWorkers` (em_player_closure_0e_18.h) | request, skeleton = `_skeleton` (001C68C0), arbiter, clip_frames, trs = `_build_trs_matrix` |
| `EmPlayerRecoveryWorkers` | request, arbiter, clip_frames = `_clip_frames_actor`, handoff |
| `EmPlayerReactionWorkers` | request, arbiter, clip_frames = `_clip_frames_actor` |
| `EmPlayerRunningJumpWorkers` | request, arbiter, clip_frames |
| `EmPlayerClosure1019Workers` | request, arbiter, skeleton = `_eval_skeleton`, trs = `_build_trs_matrix` |
| `EmPlayerMiscWorkers` | request, ledge_top = `_ledge_top` |
| `EmPlayerWeaponWorkers` (weapon states A) | request, skeleton = `_eval_skeleton`, handoff |
| `EmPlayerWeaponBWorkers` | request = `em_pose_host_request_bits` (the blend as raw bits), skeleton = `_eval_skeleton`, handoff |
| `EmPlayerLadderWorkers` (em_player_ladder_entry.h) | trs = `_build_trs_matrix`, euler = `em_pose_host_euler` (00102C58), request |
| `EmPlayerLadderWorkers` (em_player_ladder_climb.h) | request, skeleton = `_skeleton` (001C68C0), handoff, clip_frames |

Where a lane header comments "anim_eval_skeleton (001C6DA0) and
anim_matrix_dispatch (0017A130)" (the weapon states), only the first is this
module's. anim_matrix_dispatch is a separate worker.

**View slots** (context = an `EmPoseActorView {host, actor, d275B40}`).
`d275B40` points at the coordinator's live D_00275B40 word. anim_bone_array_setup
(001CB5B0) sets that word to D_00275B48 + 0x110 for the owner being run.

| Lane struct | Slot = worker |
|---|---|
| `EmPlayerSlideWorkers`, `EmPlayerClimbWorkers`, `EmPlayerReversalWorkers` | request = `em_pose_view_request`, arbiter = `_arbiter`, clip_frames = `_clip_frames` |
| `EmPlayerHangWorkers` | node = `em_pose_view_node_float` |
| `EmPlayerClosureWorkers`, `EmPlayerLadderWorkers` (ladder climb) | node = `em_pose_view_node_bits` |
| `EmPlayerMajor2Workers`, `EmPlayerClosure1019Workers` | root_node = `em_pose_view_root_node` |
| `EmPlayerLandWorkers` | hip = `em_pose_view_hip_xz` (node 1 +C0 / +C8) |
| `EmPlayerRunningJumpWorkers` | root_clock = `em_pose_view_root_clock` (node 0 +8) |
| `EmPlayerLadderWorkers` (ladder entry) | node = `em_pose_view_node1_words` (node 1 +C0..+CC) |
| `EmPlayerWeaponWorkers`, `EmPlayerWeaponBWorkers` | bone = `em_pose_view_bone` (node `slot` +90..+CF) |
| `EmPlayerRecoveryWorkers` | skeleton = `em_pose_view_eval_node1` (00162A40: anim_eval_skeleton, then node 1 +C0..+CC) |
| `EmPlayerReactionWorkers` | skeleton = `em_pose_view_eval_node1_xyz` (anim_eval_skeleton, then node 1 +C0..+C8) |
| `EmPlayerClimbWorkers` | skeleton: `em_pose_view_eval_hip` (00161790 pull-up: anim_eval_skeleton, then node 1 +C4 and +8), through a shim. The climb lane passes its projected `EmPlayerClimbActor`, so the shim writes the projection back to the raw record before the call and reads +D0 back after it. The climb `handoff` takes the projected actor for the same reason, and needs the same shim around `em_pose_host_handoff`. |

Not this module's (no binding given): the 0e_18 / 10_12_19 `to_int`
(001281C0, `em_player_float_to_int` in em_player_stage_workers.c), the
ladder-entry `identity`, `translate`, `rotate_y` and `apply` matrix leaves,
the closure 10_12_19 `rotate_x`, and the clip-row / clip-id readers.
(`em_owner_services_original.c` exports 001029C0, 00102918, 00102B08 and
00102BB0; adapting them to those lanes' slot signatures is those lanes'
binding.)

**Sources needed when bound:** `src/game/em_pose_host_workers.c`, plus
`em_player_stage_workers.c` (float_to_int, 001B1470, 001C64F0),
`em_player_reaction.c` (0017C540) and `em_stream_lanes_original.c`
(00128250). None of these four is in the game's source list yet.
`em_owner_services_original.c` (the SDK matrix routines) and
`em_player_floor.c` already are. No existing file needs an edit or a new
export. A trial link on 2026-09-23 (the private lane build command with
those four sources appended) built with zero warnings and no duplicate
symbols.

**Note for the lead.** PLAYER_HANG.md section 1 and PLAYER_CLOSURE_0E_18.md
say that D_00275B40 is the player's own +40. In every capture it holds
0x8102F0 (= player + 0x40) only because the last owner run was the record
at 0x8101E0 (0x8101E0 + 0x110). During the player's own callbacks it is
D_00275B48 + 0x110 of the player. The view adapters do not assume either
value. They read whatever the live word holds.

## 4. Verification

`python3 tools/test_pose_host_workers_reference.py` (quick: about 7 s wall,
4 images; `EM_TEST_FULL=1`: about 96 s wall, 16 images). Suggested target:
`make test-pose-host-workers-reference`.

- **Interpreter.** The shared EE of test_player_slide_reference.py with the
  COP1/VU0 float model of `test_coll_move_reference.FloatEE`
  (`tools/ee_float_model.py`). The test file only subclasses it to record the
  executed instruction addresses. Every original listed in the header
  docstring runs unmodified. The only hooks are 0019BC40 and 0011E620 in the
  00178910 cases, which get the same scripted column table and libm
  `atan2f` (or the same fixed result) on both sides.
- **Library.** The test builds its own shared library from this module,
  `em_player_stage_workers.c`, `em_player_floor.c`, `em_player_reaction.c`,
  `em_owner_services_original.c` and `em_stream_lanes_original.c`: the
  production sources, with no shim.
- **Comparison.** The native side runs over a copy of the same 32 MB image.
  After every case, the native RAM with its globals written back must equal
  the original RAM byte for byte, and the scratchpad must equal the original
  scratchpad. Every return value is compared as well.
- **Cases.**
  - On each captured image (playable_ee; route beats 05_boxes,
    06_hill_slide and 08_truck_crossing in quick mode; all 15 beats in full
    mode): anim_eval_skeleton, 001C68C0, 001C6960, a non-root parent on node 0, 001C87C0 with
    the captured rate and with 1.0 and 2.5, the D_008106F3 latch, 001C8710,
    anim_sample_bones, 001749A0 (same clip, forced, zero blend), the arbiter
    (same clip, zero blend), bone_init, 001C61D0 over five clips, 0017C540,
    and four request-then-advance sequences (a hold clip, a loop clip, a
    follow-on clip run past its end, a blended request).
  - Random perturbations of the captured node channels (28 quick / 700 full).
  - Synthetic multi-key banks (16 quick / 400 full), because the player
    bank's scale streams have a single interval. These cover key crossings,
    the 0x8000 end flag, event tables, and hold, loop and follow-on clips.
  - 00178910 with scripted column tables and hits (60 quick / 1500 full),
    plus fixed atan2 results whose heading 001B1470 must reduce over many
    2π steps (±9000, 40000, −50000 in quick mode; ±3.0e6, exponent 0x94,
    about 480,000 original loop passes, added in full mode).
- **Leaves.** The bound 00128250 (`em_stream_lanes_00128250`: special values
  and random bits), both key decoders, quat_nlerp and quat_to_mat3 with their
  scratch, the `em_pose_host_build_trs_matrix` and `em_pose_host_euler`
  adapters (random arguments, and the aliased placements of section 2 in one
  buffer compared whole), the bound `em_owner_services` rotations against
  00102A60/00102B08/00102BB0, and 0017C540 over random records.
- **Adapters, views, fail-stop.** The `EmPlayerLiveActor` adapters and
  stage entries are checked against the original. The view adapters are
  checked with D_00275B40 = player + 0x110: request, arbiter, clip_frames,
  request_bits, every node reader, the three skeleton-then-node-1 steps, and
  `em_pose_host_euler`. 00102C58 and build_trs_matrix are also checked with
  the angles aliased inside `out`. For fail-stop, a missing advance worker,
  an unbound shared-storage pointer (0x70003440, 0x70003400, 0x70003760,
  D_008106F3, the column table, 0x70003A20), unmapped memory, a missing or
  unmapped D_00275B40, or a short record must all return −1 with nothing
  written (17 core checks plus the view entries). The heading refusal has 5
  more: the float model shows the 2π step moving every angle below exponent
  0x9A and none from 0x9A to 0xFE; the native reduces headings of exponent
  0x99 (±1.0e8) into (−π, π] and refuses exponent 0x9A (±2.0e8) and an
  infinite atan2 result with the record unwritten.
- **Coverage.** Every reachable instruction of the executed originals must
  run at least once. The only exception is the negative-word block of the
  unsigned-to-float idiom after a halfword load, which cannot be taken.
- **Capture re-evaluation.** Native only. From the captured channels and the
  captured +A0 position, the evaluator that the player tail 0015BCF0 selects
  must reproduce the captured node world matrices bit-exactly. This is
  asserted: the run fails if any checked image differs or if every image is
  skipped (the tail's `mode == 0` with the hold byte set). Quick mode checks
  4 of 4 images; full mode checks 16 of 16.

Results (2026-09-23, after the review fixes): quick passes in 7.1 s with
1,684 leaf comparisons, 212 cases (652 original calls), 10 adapter, 29 view
and 22 fail-stop checks, and capture exact on 4/4. Full passes in 95.7 s
with 84,377 leaf comparisons and 3,022 cases (10,305 original calls) over 16
images, the same adapter, view and fail-stop checks, and capture exact on
16/16.

## 5. Limits

- The route beats are captured at frame boundaries. The oracle executes
  these routines over those states and over perturbed or synthetic data, but
  no capture records a mid-callback call sequence. What a given state passes
  as clip, blend and frame is each state lane's evidence, not this one's.
- 00178910's column table and sweep hit are scripted. Its real inputs are
  the 0019BC40 and 0019AFE0 workers of the floor and hang lanes.
- The bound SDK matrix adapters read the scale, the position and the
  matrix rows at the exact pointer, where the original's quadword reads
  ignore the low four address bits (section 2). Every caller passes 16-byte
  aligned EE addresses, so the two agree; an unaligned caller would not.
- The climb skeleton needs the projection shim described in section 3.
  The slide and climb lanes' request, arbiter and clip_frames need the view
  over the raw record, which those lanes keep in their projected struct.
- The existing `em_player_pose*` / `em_pose_transition` / `em_pose_bank` path
  is still the live pose path, over the exported bank. Replacing it with
  these raw-record workers is the coordinator's step. They are separate
  storage and must not both drive the same record.
