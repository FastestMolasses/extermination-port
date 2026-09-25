# Player reversal skid (WP-15 / H11)

The skid has one translation: 00174AC0's gate in
`src/game/em_player_heading_record.c` (the `heading` worker) and 0017C030
cases 6 / 7, 001612D0 case 2 and 0017BC40 mode 6 in
`src/game/em_locomotion_display.c` / `src/game/em_player_motor.c`, live in
AREA11 since census L12 (docs/LOCOMOTION_DISPLAY.md section 4). The skid's
clips are the record pose's (every first-level clip of the bank), so no
display hook or clip export is needed. The earlier pure-logic module
`em_player_reversal.{c,h}`, its host binding in em_player.c, its host test
and its oracle were retired with that step (a partial second translation of
the same routines); the interpreter the oracle carried is
`tools/player_callback_oracle.py`.

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
- **0017BC40** mode 6 subtracts 0.05 per tick, floored at 0.
  Mode 7 is inert.
- **001612D0 case 1**. After 0017C030 and 00178B90, stage 6 or 7 sets `+6 += 1`
  and `+28 = 0`.
- **001612D0 case 2** (after 001607D0; a Use press goes to 00184BA0(p, 1)
  instead of the 00160220 Use dispatcher). The order is: 00174AC0(p,1), then the stage decision
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
  - The live path reads every row from the exported ELF span
    (assets/player_loco_tables.emrg) through em_loco_0017B490 / 0017B460.

## Evidence and limits

- `tools/test_locomotion_display_reference.py` executes 001612D0 (cases 1 and
  2), 0017C030 (all cases, 6 and 7 included) and, in its captured-image
  cases, the bound 00174AC0 with the stick held (the reversal gate's
  0x70003A20 store compared); `tools/test_player_heading_record_reference.py`
  sweeps the gate's ±3π/4 boundary; `tools/test_player_loco_workers_reference.py`
  covers 0017BC40 mode 6 on the record.
- No route beat reverses the stick above speed 0.5, so the skid has no live
  capture; the evidence is instruction-level only.
- The skid's surface effects (0x80000033 / 0x80000012, 001EFD90) go to the
  counted effect gap (census L26); sound 0x137 plays through the sfx registry
  if exported (WP-14).
