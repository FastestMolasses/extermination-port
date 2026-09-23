# Player reversal skid (WP-15 / H11)

Pure logic: `src/game/em_player_reversal.{h,c}`. Host binding: the reversal
block in `src/game/em_player.c`. Oracle: `tools/test_player_reversal_reference.py`.
Host-binding test: `tests/player_reversal_host_test.c`. Clip export:
`tools/export_player_reversal_clips.py`.

## Original behaviour (read from the original code)

- **00174AC0** (byte-matched). This is the gait latch plus the heading gate. It
  applies only when player `+5 == 1` (the walk callback).
  - Stage `+1F0` 6 or 7 suppresses the turn.
  - Otherwise, when speed `+38 > 0.5` and gait `+23F >= 2`, the routine computes
    `e = 001B1470(ang - +C4)`, stored in `70003A20`.
    - `!(e <= 2.3561945)` sets `+1F0=7`, `+1F1=4`.
    - `e < -2.3561945` sets `+1F0=7`, `+1F1=3`.
    - Either result suppresses the turn.
- **0017C030 case 7**:
  - Clip `0017B490(p, 2 if +1F1==3 else 4, +235, 0)`.
  - `001749A0(p, clip, 0, 4.0)`.
  - `+1F0=6`.
  - `001FB9F0(0x137, 0x1000, 0x1000, 0x1000)`.
- **0017C030 case 6**. While `+200 & 0x1000` is clear, it writes `+204=0.75`.
  Once the bit is set:
  - Clip `0017B490(p, 3 or 5, +235, 0)`.
  - `001749A0(p, clip, 1, 0.0)`.
  - `+1F0=0`, `+25C=0`, `+38=0`.
  - `+C4 = 001B1470(pi + +C4)`.
- **0017BC40** mode 6 subtracts 0.05 per tick, floored at 0 (em_player_motor.c).
  Mode 7 is inert.
- **001612D0 case 1**. After 0017C030 and 00178B90, stage 6 or 7 sets `+6 += 1`
  and `+28 = 0`.
- **001612D0 case 2** (after 001607D0; the 00184BA0 door scan replaces the
  00160220 Use poll). The order is: 00174AC0(p,1), then the stage decision
  below, then 0017BC40, 0017C030 and 00178B90(p,0), then the common tail.
  - **Stage 6 or 7.** The surface effect fires when `(+28 & 7) == 0`:
    - `+23A` 5 or 6 → `001EFD90(0x80000033, +B0, +C0)`.
    - Otherwise, only when `+23C == 0` and `+23D == 0` → `0x80000012`.
    - Then `+28++`.
  - **Stage 0 with target `+240 != 0`.**
    - Tier `gait-1`, speed `D_00248870[tier]`.
    - Clip `0017B490(p,1,+235,+25C)`.
    - After variant 3: `001749A0(clip,0,0.0)`. Otherwise:
      `001749F0(clip, 0.0, length/2)`.
    - `+6 -= 1`, `+1F0 = 1`, `+1F1 = 1`.
  - **Stage 0 with no target.** `+5 = 0`, `+6 = 0`, `+1F0 = 0`. The next
    callback is 00161020 case 0.
- **Clip IDs.** These come from `D_00248AB0`, for the healthy row 0 with the row-4
  override clear:
  - Turn clips 6 and 7 (15 frames, non-looping).
  - Follow-up clips 8 and 9 (1 frame).
  - Resume: tier clips 0, 1, 2 and 3.
  - The native table also holds rows 1–4. The oracle checks it against the ELF
    by executing 0017B490 and 0017B460.

## Verified

`python3 tools/test_player_reversal_reference.py` runs the original 00174AC0,
0017C030, 0017BC40, 001612D0 (cases 1 and 2), 0017B490, 0017B460, 001B0070 and
001B1470 instructions. It compares every actor byte and each ordered side-effect
call bit for bit:

| Check | Cases |
|---|---|
| Interpreter extension, validated on 0017BC40 and 001B1470 | 5,770 |
| Heading gate, including ±3π/4 boundary ULPs | 4,000 (595 new skids) |
| 0017C030 | 4,000 |
| 001612D0 case 2 | 4,000 |
| 001612D0 case 1 | 3,000 (1,014 detections) |
| Whole sequences: detection, skid, end flag, then resume or idle | 160 |
| Fault cases | 8 |

- The whole sequences cover all three outcomes: 40 resumes by request, 43 by
  arbiter, and 77 idle returns.
- Mutating the variant, limit test, effect ID, resume frame, the 0.75 rate, the
  8-tick mask, a clip ID, the EE-rounded π add, or the `+5` test each fails
  the oracle.

`tests/player_reversal_host_test.c` (ASan/UBSan) drives the real `player_move`
against a fake source. It checks the following:

- Detection keeps yaw, requests the turn clip with blend 4 (force 0), plays
  0x137, and translates 0.8.
- State-2 ticks do not poll Use. They write rate 0.75 and decay speed.
- An unbound effect worker faults.
- The end flag turns the body by π and requests the follow-up clip (force 1,
  blend 0).
- A held stick resumes at tier 2, speed 0.3625. It uses the clip-2 request
  (variant 3) or the arbiter at frame 22.5 (variant 4).
- A released stick hands off through stop phase 3 and then idle.
- A mode reset by another owner cancels the skid.
- A bank without the clips faults.

## Boundaries (not claimed)

- **Surface effect.** 001EFD90 spawns the class-0xC effect actor with behaviour
  001EA240, and no native worker exists yet. While
  `player_reversal_set_effect_worker` is unbound, the first state-2 tick is a
  counted fault (`player_reversal_faults`). The port then abandons the skid
  through `player_pose_unsupported_hold`.
- **Sound 0x137.** The sound is called through `em_sfx_play`, but the cue is not
  in the exported bank (WP-14). This is reported once.
- **Actor bytes without a port source.**
  - `+235` comes from health only: bit 0. Bit 1 (rows 2/3) is not modelled.
  - `+236` and `D_008106C8` bit 2 are clear in all 11 captured AREA11 states.
  - `+23C`/`+23D` water depth are 0.
  - `+23A` comes from the current floor probe. The original uses 00175900's
    value from the previous callback.
- **Timing and display.**
  - Clip-end timing comes from the original-channel pose core (flag 0x1000), not
    from this module.
  - No live original reversal capture exists. The oracle is instruction-level
    only.
- **Pose host.** `player_pose_finish_state` idle-enters on any callback with
  `+1F0 == 0` and gait 0. This includes the skid's exit tick, which in the
  original is still 001612D0 case 2. Its idle branch must be gated on
  `!player_reversal_owns_walk()`. Until that change, a skid released at its
  end enters idle two callbacks early.
- **Display.** Displaying the skid clips needs `player_reversal_palette()` in
  em_player_frame.c. That function uses exported matrices, as the stop display
  does. Node 0 is identity in the channel core, so the −5 root Z travel of clips
  6/7 is not displayed.

## Assets

`python3 tools/export_player_reversal_clips.py` stages its output in
`build/player_reversal_export/`, and `--install` replaces the assets. It appends:

- **To the EMPC bank:** the decoded original keys of clips 6/7/8/9. The encoder
  re-encodes an existing clip byte for byte first.
- **To the EMDL model:** 32 channel-core frames. Every existing byte of both
  files is kept.
- **Check.** The staged EMPC passes the `test_pose_bank_reference` decoder with
  18 clips.
- **Run.** A staged-asset run of `EM_STARTUP_TEST=newgame-control` passes
  (9.599989).

## Binding

The pool-node adapter drives these entry points with the owner's actor bytes:

- `em_player_reversal_heading`
- `em_player_reversal_animation`
- `em_player_reversal_walk_tail`
- `em_player_reversal_state2`

Workers:

- `request` → 001749A0
- `arbiter` → 001749F0
- `clip_frames` → 001C61D0
- `sound` → 001FB9F0
- `effect` → 001EFD90
- `turn` → the arg1==1 turn

A NULL worker that is reached returns −1.

## Live gate (lead, 2026-09-22)

In the live build the skid engages only when all of its original workers are
bound: the display stage declares `player_reversal_bind_display(1)` (it calls
`player_reversal_palette`), the 001EFD90 surface-effect worker is set with
`player_reversal_set_effect_worker`, and the model carries variant clips 6/7.
Until then `em_player.c` runs the ordinary 00174AC0 turn, exactly as before this
module existed, instead of a path that faults on every reversal. The
translation stays oracle-tested (`make test-player-reversal-reference`), and
`make test-player-reversal-host` scenario 0 checks that each missing binding
keeps the gate closed.
