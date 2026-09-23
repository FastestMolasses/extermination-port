# Player foot stop (0017B910 entry, 0017C030 mode 5)

Module: `src/game/em_player_foot_stop.{h,c}`. Oracle:
`tools/test_player_foot_stop_reference.py` (`make test-player-foot-stop-reference`).

## What is verified

- `em_player_foot_stop_begin` covers the 0017B910 entry path with the
  +0x236 == 0 arm. It picks the planted foot (node 18 when the clock is below
  the row limit, otherwise node 17). The step duration is the halved residual
  clamped to 1 for tier 1 (walk) and 10 for tier 2 (jog). It also computes the
  planar distance and the rotated step. The words it produces are the ones the
  original writes to +0x260/+0x264/+0x268, and they match the original bit for
  bit.
- `em_player_foot_stop_tick` covers 0017C030 mode 5. It advances the position,
  decrements the counter, sets the tier-1 rate of 2, and exits (tier 1, or
  tier 2 with animation flag 0x1000). Each tick matches the original bit for
  bit.
- The clock has no lower bound. 0017B910 has no check on +0x3C. A clock below 1
  takes the `cur < lim` arm with a negative residual `cur - 1`. Walk then clamps
  to 1 and jog uses 10. The oracle covers 0, 1e-6, 0.001, 0.25, 0.5, 0.999,
  0.9999999 and random clocks in [0, 120) or [0, 45). Its report includes
  `clock_below_one_cases`.

## Boundaries

- The caller passes in the evaluated source-pose foot nodes 17/18. The skeleton
  evaluator is not part of this module.
- The row limits are fixed at 58 (tier 1) and 24 (tier 2), taken from the
  healthy D_0024875C rows. Other rows are not modelled.
- Non-finite inputs and a tier outside {1, 2} make the function refuse
  (returns 0). That refusal is native-only; the original has no such check.
