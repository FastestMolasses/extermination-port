# AREA11 script host (`em_area_script`)

`src/game/em_area_script.{h,c}` runs the original event scripts of the AREA11
owners: 001BA1A0 (start) / 001BA1F0 (poll) with the ftab_0024D880 command
handlers those scripts use. Status: **live for the truck trigger 008251E0
(0x8292C0) since census L19 / L23 (2026-09-24)** and **for Roger 008237E0
(0x8283D0; 0x828990 and 0x828810 are bound, not on the route) since census
L22 (2026-09-24)** through the
binder `em_area11_script_host` (section 6); the level smoke's
`truck_preview` phase reproduces route 07 and its `roger` phase route 14
row for row. The director's scripts (L21) are not bound yet: Roger's
alternate script 0x828990, which writes the D_00810813 = 1 the director's
beat 0 waits for, now has every worker it needs (DIRECTOR_ORIGINAL.md
section 6).

## 1. What it is

- Sequencing is `em_script` (the verified 001BA1F0 translation). The host
  supplies the handlers.
- Handlers that already have a verified native translation are **called**, not
  re-translated:
  - 001B82D0 (op07) subs 0/2/4/13 → `em_interaction_frame_command`;
    subs 9–12 → `em_interaction_cinematic_command`. The host adapts them to
    the canonical storage (load before, publish after, publish/reload around
    every worker call). Subs 1/3/7/8 are the module's 0/2 plus, at the module's
    scope-zero event (after the skip byte, before 001D2610, the original order),
    3B91 = 1 (1/3/8) and 001B81D0 (7/8). Sub 5 is the D_00810758 flag store,
    then sub 4. Sub 6 has no native module and is translated here.
  - 001B7B30 sub0 (op0D) → `em_cinematic_playback_wait` (its time/duration are
    the camera's +0x74 cursor / +0x78 head); the host then calls 001D25F0 with
    the module's 480 zoom and publishes its up vector.
  - 001B7D60 (op0C) → worker `w_001B7D60`, bound to `em_message_op0c`.
  - fades, camera track, face, audio and all other callees → workers.
- Translated here (decomp C, checked against the original instructions by the
  oracle): 001B8FC0 op00 kinds 0–7, 9, 10 (sine ease on kind 1); 001B94F0 op01
  kinds 0–7, 9, 10; 001B9BA0 op02; 001B9C10 op04; 001BA080 op06; 001B99F0 op09
  (record callback); 001B9A00 op0A subs 0–7; 001B8020 op0B subs 0, 4 and 6 (sub 6's
  001FBD50 cue falls into sub 0's clip init; census L18); 001B7B30 op0D
  subs 1–8; 001B7A30 op0F; 001B7840 op10; 001B6FA0 op15; 001B6E40 op16;
  001B6BF0 op18; 001B81D0 (face attach, inline, with 001CA700/001D06D0 workers).
- Also translated here, from the original instructions (0011E2A8, 0011D770 and
  0011CCC8 are asm-void in the decomp; 0011C7B0 is NEARMISS): the SDK sine
  0011E2A8 over |x| ≤ 0x4016CBE3 as `em_area_script_sin_0011E2A8` /
  `em_area_script_w_0011E2A8`, the `w_0011E2A8` binding (section 2).
- **Not admitted (fault):** opcodes 03, 05, 08, 0E, 11–14, 17, 19, 1A; op00/op01
  kind 8 (settle/orbit: 0018C6A0/0018C4B0/0011DE90; `em_pickup_camera_settle`
  is the module to delegate op00 kind 8 to when a script needs it); op0A sub8
  (001798D0); op0B subs other than 0, 4 and 6; out-of-range kinds. Of the AREA11
  first-visit scripts only the pickup grab programs use them (op0E sub1 and
  op00 kind 8; the battery runs 0x266620, section 2). Those programs stay on
  `em_pickup_program`, which translates both. op14 (001BAC00 spawn) is used only
  by the opening 0x828FC0 (hosted by `em_opening_runtime`) and by 00823CE0's
  0x828C70, which is dormant in the first visit (FIRST_LEVEL_AUDIT INV-08).

### Data and calls

- Every global, player (D_008102B0), camera (D_008101E0) and owner field a
  handler touches is one pointer of `EmAreaScriptWorld`, named by its original
  address, into the binder's canonical storage. Vectors are four floats (quad
  copies and VU0 subtractions move four lanes). A reached NULL pointer faults at
  that address.
- Every original callee is one worker of `EmAreaScriptWorkers` (`w_ADDR`, the
  decomp parameter list; `r_` readers; `c_record` for op09). A NULL worker or a
  negative result faults at that address; the fault latches and later ticks
  return -1 without calling anything.
- Script block actor+0x1F0..+0x21F = `EmScript` + `st_0E` (op0B) + `st_10`/`st_20`
  (op00 kinds 1/5 and op01 kinds 3/5 save their start vectors there).
  3B91 has one storage (the world pointer); `EmScript.skip_request` is a
  per-tick view.
- `em_area_script_tick` returns the original 001BA1F0 result: 0 running,
  1 finished, **3 aborted by the skip path** (legitimate, not a failure), and
  -1 for a fault.
- Floats: FPU add/sub with the single guard bit, truncating mul, rounding div
  (`em_pose_math.h`); VU0 truncating (`em_effect_float32`) — the model of the
  truck/fan/pose oracles (TRUCK_ORIGINAL.md "Arithmetic").

## 2. Findings from the original (checked in the oracle)

- op07 phase 0 returns 1 (advance, skipping the enter) when 3B92 is already
  set, so a second frame request inside a scripted frame is a no-op.
- Only op07 subs 1/3/8/10/12 raise 3B91 to 1, so only scripts entered with
  them can be skipped (001AE6B0 promotes 1 → 2). The truck preview, both
  elevator scripts and the panel scripts use sub2: **not skippable**. The
  director beats (sub8, sub3), Roger's encounter (sub12) are.
- op18 (001B6BF0) unskipped marks the skip byte 3 and continues in the same
  tick; after a skip scan lands on it, it calls 001B0C00(8), waits for the
  transition substate 2, stops streams, sets D_002821B4 = 2, restores the
  player's default bank, and restores the camera when D_008101E4 == 3.
- op09 (001B99F0) **tail-jumps** (`jr`) into the record's callback. An oracle
  that intercepts only `jal`/`jalr` runs the callee's original code instead
  (the test hooks every control transfer to the callback address).
- op07 sub4/5 with 3B8D == 0 is a no-op returning 1 (panel scripts 0x247BE0 /
  0x247DA0 have no enter; they end the frame another script opened).
- `em_elevator_program` / `em_panel_program` treat `EM_SCRIPT_ABORTED` as a
  failure. Their scripts use sub2 and cannot be skipped, so this has no live
  effect today; with this host the original result 3 is reported as such.
- **SDK sine (settled by the route captures).** The op00 kind 1 ease calls
  0011E2A8 (sinf). Its path for the ease range is 0011D770 (sine kernel),
  0011CCC8 (cosine kernel) and the first-step branches of the reducer
  0011C7B0; it uses only add/sub/mul/neg/cvt. Replaying the host over the
  PCSX2 route captures settles the add.s model:
  - with the guard-bit add, every captured camera frame of the truck preview
    (beat 07, 1,432 eye/target comparisons) and of director beat 0 (beat 10,
    9,656) matches;
  - with plain truncation (`em_item_sdk_sine`), 188 truck frames and 1,862
    beat-10 comparisons differ, in the last digits of the eased eye/target.

  `em_area_script_sin_0011E2A8` translates that path with `pose_add/pose_sub`
  and truncating `pose_mul`, and `em_area_script_w_0011E2A8` is the worker form.
  It equals the original instructions on 88,818 arguments (full sweep). It
  refuses (-1) past |x| = 0x4016CBE3, where the untranslated larger-argument
  reducer branches begin; the ease never goes past pi/2.
  `em_item_sdk_sine` differs from the original on 257 of the 360 values the
  level scripts produce and on 41,543 of the sweep arguments. It is not a
  binding. The item/UI users of it are outside this module; see section 5.
  001B1470 equals `em_fan_original_wrap_001B1470` on every value observed.
- **The battery pickup runs the grab program.** Route beat 01 shows 00219550
  running 0x266620 (rows f125..f486), not the short program 0x2667E0. The
  grab program uses op0E sub1 (turn toward the owner) and op00 kind 8 (camera
  settle), and this host admits neither (they fault). `em_pickup_program`
  translates both (PICKUP_OWNERS.md). The pickup row in section 3 is corrected.
- **Owner-written records.** The panel owner 00157860 stores the message
  operand into the op0C record's +0x14 before it starts 0x246F20 / 0x2477A0
  (runtime 0x1577B8.. → 0x246FB4, 0x157A34 → 0x247834). The ELF image has 0
  there. A binder that runs the panel scripts from an ELF export must make
  the same store. `em_panel_program` does this; see AREA11_PANEL.md.

## 3. Scripts, owners, commands

Start sites are the `jal 001BA1A0` in each owner with its resolved `a1`.

| Script | Owner (start site) | Commands (op/sub, in record order) |
|---|---|---|
| 0x8292C0 truck camera preview | trigger 008251E0 (0x825388) | 07/2, 01/1, 04/8, 00/0, 00/1 (sine), 00/2, 00/1, 07/4 |
| 0x82A990 elevator refusal (no power) | 00827B10 (0x827D10) | 07/2, 01/1, 04/8, 0D/5, 0C/0 (line 0x8000001A), 07/4 |
| 0x82A750 elevator powered | 00827B10 (0x827CF0) | 07/2, 01/1, 04/8, 00/0, 0A/0 (clip 0x47), 0A/3, 09 (0x828050), 00/0, 07/4 |
| 0x8294C0 beat 0 | manager 008253F0 (0x8255A0) | 16, 07/8, 06/0 (0x3B), 00/0 ×5, 00/1, 02, 00/1, 02, 00/1, 02, 00/1, 02, 00/1, 02, 06/2 (0x3B), 18, 00/0, 07/5 (0x3B) |
| 0x829A40 beat 1 | 008253F0 (0x825688) | 16, 07/8, 00/0, 00/5, 0C/0 (0x97), 00/5, 02, 18, 0D/2, 07/4 |
| 0x829CC0 beat 2 | 008253F0 (0x825758) | 16, 07/8, 00/0, 0C/0 (0x99), 18, 0D/2, 07/4 |
| 0x829E80 beat 3 | 008253F0 (0x825880) | 16, 07/3, 06/0 (0x3C), 01/3, 00/0, 01/9, 0A/0, 09 (0x825900), 0C/1 (0x9B), 02, 00/5, 09 (0x825920), 00/0, 00/5, 0A/0, 02, 18, 01/9, 00/0, 06/0, 07/4 |
| 0x8283D0 Roger encounter | Roger 00823910 (0x823A7C) | 16, 07/12, 06/0, 0C/1, 0A/1 (bank 0x96), 00/6, 01/10, 0B/4, 0D/0, 10/1, 10/5, 18, 0A/5, 01/10, 01/9, 00/0, 07/4 |
| 0x828990 Roger alternate | 00823910 (0x823984) | 15 (line 0x7F), 06/3 |
| 0x828810 Roger armed talk | 00823B70 (0x823BB4) | 07/3, 15 (line 0x13), 18, 01/10, 0A/0, 07/4 |
| 0x828A10 Roger departure | 00823C40 (0x823C74) | 07/0, 10/1, 01/3, 10/5, 10/3, 0F, 07/5 |
| 0x246F20 / 0x2477A0 / 0x247BE0 / 0x247DA0 panel | 00159210 (AREA11_PANEL.md) | 07/2, 0D/3, 0C/0, 07/4 · 07/2, 0D/3, 0C/0, 09 (0x157F60) · 0D/3, 0A/0 (0x15C), 02, 09 (0x1575B0), 02, 09 (0x1580C0), 07/4 · 02, 07/4 |
| 0x248480 / 0x2667E0 pickup, short program | 0015AFA0 / 00219550 (PICKUP_OWNERS.md) | 07/13, 09 (0x1B6EA0), 07/4 |
| 0x2482C0 / 0x266620 pickup, grab program (**not admitted**) | 0015AFA0 / 00219550 | 0x266620: 07/13, 0E/1, 0A/0 (clip 0x42), 00/8, 0A/3, 09 (0x1B6EA0), 07/4. The battery (route beat 01) runs this program. It stays on `em_pickup_program`. |

Not hosted: 0x828FC0 opening (00823E80, 0x823F3C; op14) stays on
`em_opening_runtime`; 0x828C70 (00823CE0, 0x823D8C; op14) is revisit content.
The two manager callbacks are tiny: 0x825900 calls 001DFE10() and returns 1;
0x825920 calls 001DFE40() and returns 1.

## 4. Verification

- `python3 tools/test_area_script_reference.py` — the user's ELF interpreter
  and handlers execute as original instructions over the captured
  first-control RAM (`playable_ee.bin`); the native host runs the same bytes in
  lockstep. Per tick: result, ordered worker calls with arguments (and results
  of the math callees), the script block, every modeled global/player/camera/
  owner byte (3B84/3B8D/3B8F/3B91/3B92 included), the 0x9C-byte message block,
  and the mutated records; any original write outside the modeled storage fails.
  Math callees, 00182F90 and 001B7D60 run as original code in the oracle. On
  the native side, 0011E2A8 is the binding `em_area_script_sin_0011E2A8`. The
  other math workers are answered by a scratch execution of the same original
  function, 00182F90 by a four-lane VU model (checked by the comparison), and
  001B7D60 by the real `em_message_op0c`. Cases: every script in section 3
  (level scripts with skips at several ticks), test-generated records for the
  admitted sub-commands the level scripts do not reach, a jump record, an
  unported opcode, an unadmitted kind, and missing worker/pointer faults.
  Quick mode: 51 scripts/variants, level durations clamped to 5 ticks in both
  images. `EM_TEST_FULL=1`: captured durations (5,977 ticks; the 0x8294C0 beat
  alone is 2,421).
- **Route captures** (same test, every mode). The host is replayed over the
  original pad-only route traces in `../Extermination/build/s87/route/`
  (FIRST_LEVEL_ROUTE.md). Each case is seeded from the snapshot its beat was
  played from, plus the recorded fields of the start row. The host then ticks
  once per captured frame from the frame the owner started the script.
  `capture_case` states every rule; in short:
  - **Compared with the next row:** each script's block (active, phase, pc);
    3B8D/3B91/3B92; the camera byte D_008101E4; the camera eye/target at
    0x8101F0/0x810200 and 0x8105D0/0x8105E0 while the host owns the camera;
    player position and yaw while the host owns the player; the message block
    (except the message service's own 1 → 2 completion and its teardown to
    zero); D_00810790..93 and D_008107D8..0x810817; the letterbox start (3)
    and leave (2) against the host's 001AEB60 / 001AEBA0 calls.
  - **Owner writes applied after completion** (owner code, cited in the test):
    - the trigger's D_00810792 = 1 (0x8253B0..B8);
    - the director's D_00810813 = 0x10 / 0x20 / 0xFF (after 0x8255B4 /
      0x82569C / 0x82576C);
    - Roger's D_008107D8 |= 1 (after 0x823AB0);
    - the panel's message operand (section 2).
  - **Inputs from the capture:** 3B8F, the 3B91 1 → 2 promotion, the message
    block, the transition substate, the player hip, and the player and camera
    until the host owns them.
  - **Derived from the capture's script pointer.** Their state is not in the
    trace, so these records' durations are not claims: the op16 predicate
    (00182BF0), the op09 callback result and its phase word, the
    animation-done bit (op0A/3, op15), stream ready (op07/12 phase 3), and the
    playback cursor (op0D/0).

  Results (0 differences, every mode):

  | Beat | Script(s) | Frames |
  |---|---|---|
  | 07 | truck preview 0x8292C0 | 364 |
  | 02 | elevator refusal 0x82A990 | 157 |
  | 04 | elevator ride 0x82A750 | 363 |
  | 00 | panel 0x246F20 | 156 |
  | 03 | panel 0x2477A0 | 156 |
  | 03 | panel 0x247BE0 | 129 |
  | 10 | Roger alternate 0x828990 and director 0x8294C0, concurrent in walk order | 2,419 |
  | 11 | director 0x829A40 | 495 |
  | 13 | director 0x829CC0 | 219 |
  | 14 | Roger encounter 0x8283D0 | 1,475 |

  Totals: 5,933 frames. The comparisons include 14,740 camera vectors, 5,643
  player position/yaw rows and 8,299 script blocks. The 5-decimal trace
  rounding identifies a float exactly when |v| ≥ 128, which covers every
  camera and position component here. Mutations of the host each fail these
  cases: a plain add in the sine kernel, a linear kind-1 ease, a countdown
  one tick short, a perturbed op15 player yaw step, and a wrong op06 sub0
  value.
- **Sine sweep** (same test): `em_area_script_sin_0011E2A8` against the
  original 0011E2A8. Full mode: every ease argument for durations 1..400,
  the 0x3FC90FD0 bucket, the boundaries, and 20,000 random magnitudes of both
  signs (88,818 arguments). Quick mode: durations 5/80/90/120/180 plus the
  same edges and a 300-sample (1,022 arguments).
- Timing: about 4 s quick; about 27 s with `EM_TEST_FULL=1`.
- `tests/area_script_test.c` (ASan/UBSan) over `assets/scene_snow/roger/
  programs.emsc`, `elevator.emsc`, `panel/scripts.emsc` and the user's
  `AREA11.BIN`: every section-3 script runs to its end, skippable scripts abort
  through 3B91, each removed worker faults at its address, a failing worker
  latches, a NULL 3B92 faults before any write, out-of-image entries are
  refused, and the sine binding refuses past its range without writing
  (33 checks).
- Mutation checks run during development (sine ease removed, 0x50 → 0x51,
  native float ops, op18 skip byte, op15 clip order, sub8 skip/face, the
  store-before-face publish, the sub5 flag, the op0D sub0 zoom call): each
  fails the oracle.

Environment used by both tests: services outside the scripts (transition
substate, message completion, stream readiness, camera cursor, animation-done
bit, skip promotion, callbacks) are one scripted environment applied to both
sides; it makes no timing claim about the original services.

## 5. Open items

- `em_item_sdk_sine` / `em_item_sdk_cosine` (`em_item_sdk_math.c`, not this
  module) model add.s as plain truncation. For 0011E2A8 the route captures
  reject that model. Their item/UI callers need the guard-bit model (the lead
  decides). Once they switch, `em_area_script_sin_0011E2A8` can forward to
  them.
- Not captured on the route (still lockstep-only): director beat 3 0x829E80,
  Roger 0x828810 / 0x828A10, and the skip paths (the route presses no skip).
- 00182BF0, 001B0C00, 001B6250, 001B0460 and 001B1240 / 001B12B0 / 001B1380
  are translated in `em_script_host_workers` (SCRIPT_HOST_WORKERS.md) but
  not bound to this host yet: only the director's and Roger's scripts reach
  them. 001DFE10 / 001DFE40 (the manager callbacks) and Roger's sub-owners
  00823910 / 00823B70 / 00823C40 remain.
- Script images: `roger/programs.emsc` (0x8283D0..0x828BD0), `elevator.emsc`
  (0x82A750..0x82AB10), `panel/scripts.emsc`, and since the script host
  workers lane `area11_scripts/scripts.emsc` (0x8292C0..0x82A3C0: the truck
  preview and the director beats) with `director_quads.emsc`
  (`tools/export_area11_scripts.py`).
- The panel, elevator and pickup programs (`em_panel_program`,
  `em_elevator_program`, `em_pickup_program`) still run their own subsets of
  the same handlers (op00 sub0, op01 sub1, op04 sub8, op0A, op0C, op0D
  sub5, op09); moving them onto this host is open (one bound owner per
  handler).

## 6. Binding

### 6.1 As built (census L19 / L23 / L22, 2026-09-24)

`src/game/em_area11_script_host.{h,c}` binds the host for the AREA11 overlay
owners. Bound owner: the truck trigger 008251E0 (its 0x8292C0). At every
area build (`em_area11_bindings_reset`) the images are dropped; the first
start of a visit loads fresh `scripts.emsc` / `director_quads.emsc`
(`em_area11_scripts_load`; the scripts are mutated in place). Each owner
has one `EmAreaScript`; its block (+0x1F0..+0x21F) lives in the owner's
pool record and is loaded and stored around every start and tick.

World: the scratchpad bytes (3B84 / 3B8D / 3B8F / 3B91 / 3B92) and the
request bytes (D_008106D4..DF, EF, F3, F4) point into `EmSceneState`; the
camera bytes E1 / E3 / E4 / E6 into `g.cam` (sub_state, swing, top_mode,
mode), the message words into the live message block, D_0028A9A0 into the
transition, the owner's +0xB0 / +0xC0 into its pool record. The four-lane
vectors whose canonical storage has three lanes (the camera's +0x10 / +0x20,
D_008105D0 / E0 / F0, the player's +0xA0 / +0xB0 / +0xC0) are one view in
the binder, loaded before each tick and each worker call and stored after
(only their w lanes live there; a changed heading goes through the pose
host's `player_pose_face`). D_008101E2 (no port reader) lives in the binder.
Every other pointer is NULL: a script that reaches it faults.

Workers: the 001B82D0 frame events through the interaction host's bindings
(`em_area11_interaction_host_frame_event`: 001AEB60(4), 001D2610(0),
001AEBA0(4), 001CA770, 001D25F0(480), 001FAE70(0), 001AEE10(4, 0)); 00182F90
→ `player_pose_align`; 001DD980 → the host's projection publish of the
working vectors; 0011E2A8 → `em_sdk_math_original_w_0011E2A8` over the
collision world's SDK context (the one bound SDK sine; `tests/area_script_test.c`
shows it equal to `em_area_script_sin_0011E2A8` on every ease argument
pi * k/d - pi/2, d = 1..400). Every other worker is NULL (fail-stop).

**Census L22 (Roger).** The images include `roger/programs.emsc`
(0x8283D0..0x828BD0). A start checks every op06 / op07 sub 5 / sub 6 slot
of the script's record chain against the canonical D2 bytes
(`slots_canonical`; D_00810758[0] and [0x3B], D_008107D8[0] and [0x3B]
are canonical since L22 / HK) and refuses any other. World additions: the
flag and counter arrays D_00810758[] / D_008107D8[] and D_0081078F in the
D2 region, D_008106F4 (the stream hold), the camera's +0x50 / +0x54 /
+0xA0 (g.cam y_lo / y_hi / tgt_soft) and the timeline fields +0x6E / +0x70 /
+0x74 / +0x78 (g.cam cine_scene / cine_track / cine_time / cine_head,
added to the camera object), the live player record's +0x40, +0x1F2,
+0x1F4, +0x1F8, +0x200, +0x20C, +0x25C, +0x2F3, +0x2FF, and Roger's +0x40
(em_area11_roger's record). Workers added:
- 001AEDE0 / 001AEE10 / 001AED80 / 001AEDB0 → the transition fade
  (em_fade.c through em_frame); 001AEB60 with any step → the bars;
  001D25F0 with any zoom → em_rcl_001D25F0 (the render context's +0x2468;
  RENDER_CONTEXT.md section 8);
- 001FD4C0 → `em_message_live_stream_request`; 00119828 →
  `em_scene_bindings_00119828`; 001B7D60 → `em_message_live_op0c`;
  001FAE70 → `em_scene_bindings_001FAE70` (a0 == 0 translated: the resume);
  001FBC50 / 001FABB0 → the scene bindings' stand-ins of the same names;
- 001CA700 / 001D06D0 (001B81D0 on the player, row 0x18) → the
  interaction host's face attach (`em_area11_interaction_host_face_attach`);
- r_0028A490 → `em_area11_roger_table_word`; 001C6120 and r_track_head over
  `roger/resources.emrs`; 0022EC30 → `em_cinematic_playback_start` over
  `roger/encounter_camera.emcc` after checking that the camera's +0x70 is
  bank 0x96's clip 0; the camera stage's 0022EEF0 is
  `em_area11_script_host_camera_0022EEF0` (em_camera.c, top mode 3);
- 001C67E0 on Roger → `em_area11_roger_clip_init`;
- 001B0250 → `em_scene_bindings_001B0250`; 0021B9A0 and 001D2830 → the render
  context's translations (em_rcl_0021B9A0, em_rcl_001D2830; RENDER_CONTEXT.md
  section 8; before the render context step they were reported no-effect
  bindings);
- 00182BF0, 001B1240, 001B12B0, 001B1380 (em_script_host_workers over the
  live record and the collision world's SDK context), 001B1470
  (em_player_001B1470 over its domain), 001B0C00 (001AEDE0 and the reported
  001FAD70), 001B6250 (`em_pad_actuator_001B6250`), r_player_bone_C0 (the
  record's node 1 +0xC0).
**Census L21 (the director, prepared 2026-09-25; DIRECTOR_ORIGINAL.md
section 6).** op0D subs 2..5's `w_0018CBD0` is the port's one 0018CBD0 seed
(em_camera.c `camera_script_seed_0018CBD0`, the step the panel and terminal
retarget runs) from the seed Euler 0x70003B50 (the pose host's) and the
player's +0xA0, and `w_0018D7B0` is the live camera's solve dispatch
(`em_camera_live_solve`); the camera's +0x0C is the live camera block's
word (`em_camera_live_bytes(0x008101EC)`). A script that runs while another
owner's script holds the shared player takeover (Roger's 0x828990 inside
the director's 0x8294C0, route 10 f1094) does not claim it again
(`em_area11_interaction_host_script_held`): 0015B130's admission reads
0x70003B8D, not the owner. `em_area11_script_host_director_quads` hands the
director its three quads. Only the director verification run reaches these
today (the director is not bound until WP-8b).

**Census L18 (the fence door 001BC350, 2026-09-25; DOOR_ORIGINAL.md
"Binding").** The host resolves starts in 0x24DBC0..0x24DF80 to the ELF's
ordinary-door program (`door_original/program.emsc`,
`em_area11_script_host_door_program`), the image 001BBE40 patches before
each start; it is dropped at every area build with the overlay images (the
kickoff rewrites every patched word). Workers added: op0B's 001C67E0 on the
door → `em_area11_door_clip_init` (the door's source bank, blend and start
0), and op0B sub 6's 001FBD50(owner, id, 0, 300) → `em_sfx_play_at` at the
owner's +0xB0 (a flat cue, a2 != 0, faults). The door's takeover is the
scan's claim (00184BA0, the door record as token). 0x1AE040 state 4's
001AFCF0 clears 3B8D and 3B8F under the held player: the shared runtime
keeps the hold (`EmInteractionRuntime.acquired`, the original's +4 == 4) and
its next stage runs 00183090 and releases on the cleared 3B8D (0015BA50 /
0015B530 / 00182DF0). `test_area_script_reference.py` replays 0x24DE40 over
route 09 (the door record 0x7A70B0, the patched words from the beat's end
snapshot) with no difference, and its synthetic 'owner clip' script covers
op0B subs 6 and 0 against the executed 001B8020.

Still NULL (fail-stop): op01 kinds 3 / 5's D_0024D8F0, op0D sub 1 (001B0460),
op0F's stream handshake bytes (Roger's departure 0x828A10, not in the first
visit).

The player takeover: after a tick whose op07 opened the scripted frame
(3B8D != 0), the owner claims the interaction host's shared player runtime
(`em_area11_interaction_host_claim_script`, the stand-in for 0015B130's
00182B30 admission the panel and elevator scripts use); the runtime acquires
the player at its next stage (in the cutscene variant, after the pool walk:
3B8F = 1 one row after 3B8D = 2, route 07 f164 / f165) and releases it when
the selector clears. On the acquiring stage the record gets the admission's
+5 = 0, +6 = 0 and +1F0 = 0x41 (its +4 = 4 is not written: the stand-in
consumes the stage in place of the +4 = 4 handler), the port's idle/walk
mirrors are not loaded over the record while the takeover holds the player,
and the releasing stage writes 00182DF0's tail (+4 = 1, +5 = 0, +6 = 0,
+1F0 = 0) (em_player.c `live_major1`, `player_states_stage`); route 07 shows
exactly these values row for row.

Since census L22 the token is the owner's pool record (the Use scan claims
Roger with the same token for his armed talk 0x828810), the admission
also runs 00182D70 on the record (`em_player_stage_scripted_notify`:
+0x1F2 = +0x20C, +0x1F4 = 1.0, +0x1F8 = 0, +0x2F3 = 0 and its clears), and
every stage of a script owner's takeover runs 00183090 on the record
(`player_pose_commit_tick`: the face's 001D0C70 when 3B8F = 2, the special
bank of a nonzero +0x2F3 (001B9A00 sub 1 / 4: the bank at +0x40, here bank
0x96 of `roger/resources.emrs`, mapped read-only into the record's pose
host), a +0x1F2 request (op0A sub 0 / 7, op15), then 001C64F0 by +0x1F4 into
+0x200 when it returns 1) and 0015BCF0's animate step (001C6960 while
+0x2F3 is 2). The release takes 00182DF0's nonzero-+0x2F3 branch (+0x40 =
D_0028A580, +0x20C = D_00248A00[+0x235], 001C63E0). The legacy decoded
special bank (`player_pose_cinematic_*`, em_player_pose over
encounter_player.empc) is deleted. Route 14 reproduces the player record
(+5, +1F0, +1F1, the clip, the clock and +0x2F3) row for row through the
encounter, the release and 60 rows after it.

### 6.2 The design binding (for the remaining owners)

One `EmAreaScript` per owner actor (the original block is that actor's
+0x1F0): `world.self` = the owner node address, `s040/s0B0/s0C0` = its +0x40,
+0xB0, +0xC0. The owner starts with `em_area_script_start` where the original
calls 001BA1A0 and polls `em_area_script_tick` where it calls 001BA1F0
(e.g. `EmTruckTriggerHooks.script_start/script_tick` for 008251E0).

World → canonical storage:

| Pointers | Storage |
|---|---|
| spad3B84/3B8D/3B8F/3B91/3B92 | `EmSceneState` spad fields |
| d8106EF/F3/F4, d8106D4[12] | `EmSceneState.req[0x3F]`, `[0x43]`, `[0x44]`, `[0x24..0x2F]` |
| d810758, d8107D8, d81078F | `EmProgress.bytes + 0x58`, `+ 0xD8`, `+ 0x8F` |
| d8101E1..E6, cam_* | the camera object (one storage for D_008101E0; E4 is the camera mode) |
| d8105D0/E0/F0 | camera working eye/target/up |
| d2821B0/B4/B8/BC | `EmMessageService.block` mode/phase/line/delay |
| p* | the player object's canonical fields (bank +0x40, A0/B0/C0 mirrors, clip +0x1F2, +0x1F4/+0x1F8, flags +0x200, +0x20C, +0x25C, cinematic mode +0x2F3, model +0x2FF) |
| d28A9A0 | `EmTransitionFade.substate` |
| d282157, d275C78, d821058 | the stream busy / cue bytes behind `r_00282157`, `s_00275C78`, `s_00821058` |
| d24D8F0 | 9 shorts from the user's ELF at 0x24D8F0 |

Workers → native services:

| Worker | Service |
|---|---|
| w_001AEB60 / w_001AEBA0 | `em_screen_fade_out` / `em_screen_fade_in` (the bars) |
| w_001AEDE0 / w_001AEE10 / w_001AED80 / w_001AEDB0 | `em_transition_fade_out` / `_in` / `_clear` / `_full` |
| w_001B7D60 | `em_message_op0c` (verified in lockstep) |
| w_001FD4C0 | `em_message_stream_request` |
| w_001FAE70, w_001FBC50, w_001FABB0, w_00119828, w_001D2610, w_001D2830, w_0018D7B0 | the `EmSceneWorkers` bindings of the same names |
| w_001C6120 + r_track_head + w_0022EC30 | the bank/index camera track (`em_cinematic_camera`) and `em_cinematic_playback_start` (scene id = the record short written to cam_6E) |
| w_001B0250, w_0021B9A0 | the `em_cinematic_playback` restore events (RESTORE_ROOM, EFFECT_OFF) |
| w_001D25F0 | projection zoom |
| w_001DD980 | world-camera publish |
| w_00182F90 | the player position-mirror service |
| w_0018CBD0 | camera retarget (`em_camera_retarget` holds its scalar part) |
| w_001CA700, w_001D06D0, w_001CA770 | `em_player_face_host` attach / speed / detach |
| w_001C67E0 | the owner's `anim_clip_init` (Roger's pose) |
| w_001B1470 | `em_fan_original_wrap_001B1470` (verified on observed values) |
| w_0011E2A8 | `em_area_script_w_0011E2A8` (verified: sweep, lockstep, route captures) |
| w_001B1240, w_001B12B0, w_001B1380 | no verified native binding yet (section 5) |
| w_00182BF0, w_001B0C00, w_001B6250, w_001B0460 | untranslated (section 5) |
| r_0028A490 | the bank-table words (handles the player/owner bank fields hold) |
| r_player_bone_C0 | the player's bone-1 translation (quad at *(player+0x114)+0xC0) |
| c_record | 0x828050 elevator mover; 0x157F60 / 0x1575B0 / 0x1580C0 panel callbacks; 0x825900 / 0x825920 (001DFE10 / 001DFE40, return 1); 0x1B6EA0 pickup take (short program only; the grab program the battery runs stays on `em_pickup_program`) |
