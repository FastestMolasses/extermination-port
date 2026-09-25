# 00174AC0 over the player record (heading-record lane)

Status (2026-09-24): translated, original-verified, **bound live in AREA11** (em_player_closure_live.c); it is
the one record-level 00174AC0, named as the `heading` worker of every
closure module (section 4), and bound as the heading worker in the
locomotion display's and the fall lane's oracles (section 4, "Status of the
slots"). Module `src/game/em_player_heading_record.c/.h`;
oracle `tools/test_player_heading_record_reference.py` (make target
`test-player-heading-record-reference`).

## 1. Why this exists

Before this lane, 00174AC0 existed only as partial translations over mirrors:
`em_player_heading.c` (the stick heading as a host trig model),
the reversal gate of a pure-logic skid module (retired with census L12) and
the banded turn inside `em_player.c`. The FLOOR mechanism and every other
player module that calls 00174AC0 name it as a worker over the raw record
(`EmPlayerLiveActor.bytes`). The FLOOR step also needs its 0x70003A20 stores:
0017C580 stores its drop at 0x70003A20, calls 00174AC0, then reloads that word
(PLAYER_FALL.md "The scratch").

This module is the whole routine over the record, with the original callees:

| callee | what | translation reused (not re-translated) |
|---|---|---|
| 0011DE90 | cosf | `em_sdk_math_original_0011DE90` |
| 0011E620 | atan2f | `em_sdk_math_original_0011E620` |
| 0011DF78 | fabsf | `em_sdk_math_original_0011DF78` |
| 001B1470 | wrap to (-pi, pi] | `em_player_001B1470` (em_player_stage_workers.c), bounded as below |
| 001B12B0 | turn toward | `em_script_host_001B12B0` (em_script_host_workers.c): the same original |

## 2. What the original does

Source: the decomp's byte-matched `src/func_00174AC0.c`; the original
instructions were read for evaluation and operand order.

1. **Scripted selector.** If the scratchpad byte 0x70003B8D is nonzero: +23F = 0,
   +240 = 0, +24C = 0, return 0.
2. **Gait latch.** +23F = the pad gait byte D_00810E57, re-read. Gait 3/2/1 write
   the target speed +240 = 0.8 / 0.3 / 0.1 (raw 0x3F4CCCCD / 0x3E99999A /
   0x3DCCCCCD). Gait 0 writes +240 = 0 and +24C = 0 and returns 0. Any other
   byte value keeps +240 and goes on.
3. **Stick heading.** Y first: y = pi * (float(D_00810E65) / 256). Then
   x = pi * (float(D_00810E64) / 256). +244 = cos(x), +248 = cos(y),
   +24C = atan2(-(+248), +244), ang = wrap((pi + +24C) + D_008106A0).
   Pi is the left operand of both products and of the first sum.
4. **Reversal gate** (only when +5 == 1). +1F0 == 7 or 6 sets arg = 0.
   Otherwise, when !(+38 <= 0.5) and +23F >= 2: e = wrap(ang - +C4) is stored to
   0x70003A20; !(e <= 3pi/4) sets +1F0 = 7, +1F1 = 4, arg = 0; else
   e < -3pi/4 sets +1F0 = 7, +1F1 = 3, arg = 0.
5. **arg == 1, standing** (+38 == 0.0, EE compare): +C4 = turn(ang, +C4, step),
   step by +23F: 1 → 4 deg, 2 → 8 deg, else 22.5 deg (0x3D8EFA35 / 0x3E0EFA35 /
   0x3EC90FDB). Then +23F < 2 and ang != +C4 (re-read) set +25D = 1.
6. **arg == 1, moving.** e = wrap(ang - +C4) stored to 0x70003A20, then
   e = fabs(e) stored again. !(e <= 0.3pi): step by speed <= 0.1 / <= 0.3 / else
   = 6 / 9 / 10.5 deg (0x3DD67750 / 0x3E20D97C / 0x3E3BA866); otherwise 4 / 6 / 7
   deg (0x3D8EFA35 / 0x3DD67750 / 0x3DFA35DE). +C4 = turn(ang, +C4, step).
7. **arg == 2** records ang at +218. Any other arg writes nothing more.
8. Returns the byte +23F, re-read.

0x70003A20 is written in exactly three places: the gate (4) and the moving turn
(6, twice). The standing turn, arg 0 / 2 and the early returns leave it as it was.

## 3. The native API

```
int em_player_heading_record_00174AC0(EmPlayerHeadingRecord *h,
        EmPlayerLiveActor *actor, int32_t arg, int32_t *result);
int em_player_heading_record_worker_result(void *ctx, EmPlayerLiveActor *, int arg, int *result);
int em_player_heading_record_worker(void *ctx, EmPlayerLiveActor *, int arg);
```

`EmPlayerHeadingRecordWorld` holds pointers into the binder's canonical storage:
`spad3B8D`, `d810E57`, `d810E64`, `d810E65`, `d8106A0` (raw bits), `spad3A20`
(raw bits, written), and the SDK `sdk_tables` / `sdk_world` / `sdk_workers`.
`sdk_workers` serves only the atan2f error path and may be NULL.

**Fail-stop.** Before its first write the routine checks the record, the result
and every pointer except `sdk_workers`. If one is missing it returns -1 with
`fault_address` = 0x00174AC0 and writes nothing. A failing callee returns -1
with the callee's address. The writes made before the call stay, in the
original's order.

**001B1470 domain.** The same bound as em_script_host_workers.h: an argument
with |x| >= 4096.0 faults at 0x001B1470 instead of looping. Every argument here
is ang ± a yaw or pi + atan2 + the camera yaw. The route never comes near the
bound. The oracle checks that the writes made before the fault (+23F, +240,
+244, +248, +24C) equal the original's own writes for camera yaw 4096.0.

## 4. Binding (for the binding chain)

**Bound live in AREA11 since the Boxes step (2026-09-24):** `em_player_closure_live.c` binds this module over the live player record (FIRST_CONTROL.md "Engaged"). Workers with no translation are fail-stop workers that name their original. The notes below are the binding it follows.

Every worker slot that names 00174AC0 and takes the record:

| slot | adapter |
|---|---|
| `EmLocoWorkers.heading` (em_locomotion_display.h) | `em_player_heading_record_worker_result` |
| `EmPlayerLandWorkers.heading` (em_player_fall.h) | `em_player_heading_record_worker_result` |
| `EmPlayerRunningJumpWorkers.heading` | `em_player_heading_record_worker_result` |
| `EmPlayerWeaponBWorkers.heading` | `em_player_heading_record_worker_result` |
| `EmPlayerHangWorkers.heading` | `em_player_heading_record_worker` |
| `EmPlayerLadderClimbWorkers.heading` | `em_player_heading_record_worker` |
| `EmPlayerReactionWorkers.heading` | `em_player_heading_record_worker` |
| `EmPlayerRecoveryWorkers.heading` | `em_player_heading_record_worker` |
| `EmPlayerWeaponWorkers.heading` (weapon states A) | `em_player_heading_record_worker` |

A scratch compile assigning each adapter to each slot is clean under
`-Wall -Wextra -Werror -Wpedantic`. **Not bindable directly:**
`EmPlayerClimbWorkers.heading` (em_player_climb.h) takes an
`EmPlayerClimbActor *`, a struct of named fields rather than the record. It
needs a record image (or the climb module moved onto the record) before this
routine can serve it.

**Context.** The adapters take `EmPlayerHeadingRecord *` as their context.
These worker structs share one `context` across all their slots, so the binder
must route `heading` to this context, as it does for the other lanes' adapters
(e.g. `em_player_reentry_worker`, `em_player_use_dispatch_worker`).

**The 0x70003A20 word.** Point `world.spad3A20` at the one binder-owned word
that every writer and reader of 0x70003A20 shares:
`EmPlayerLandScratch.s3A20` (0017C580 reloads it after this call),
`EmPlayerRunningJumpScratch.s3A20` (or `EmPlayerRunningJumpLive.shared3A20`),
`EmPlayerReentryWorkers.spad3A20`, `EmPlayerWeaponBScene.spad3A20`,
`EmPlayerRecoveryLive`-style `shared3A20` (em_player_recovery.h), and the
by-value copies in em_player_weapon_states_a.h (`spad3A20`) and
em_player_misc_workers.h (`EmPlayerMiscScratch.s3A20`, a float). The
by-value copies must be loaded from, and stored back to, the same word
around each call, as em_player_recovery.h's `shared3A20` does.

**Globals.** D_00810E57 / D_00810E64 / D_00810E65 are the pad block after
001B5940 / 001B5CC0 (em_frame.h), D_008106A0 is the committed camera yaw, and
0x70003B8D is the scene selector the coordinator already keeps.

**Status of the slots (2026-09-24, the one-owner step).** Every module doc's
binding table now names these adapters for its `heading` slot
(PLAYER_FALL, PLAYER_REACTION, PLAYER_HANG, PLAYER_LADDER_CLIMB,
PLAYER_RECOVERY, PLAYER_RUNNING_JUMP, PLAYER_WEAPON_STATES_A / _B,
LOCOMOTION_DISPLAY); no other record-level candidate remains. The closure
modules are not linked into the live build yet, so the binding is proven by
composition in two callers' oracles, each against the original with
00174AC0 running as original code:
- `test_locomotion_display_reference.py` binds it as `EmLocoWorkers.heading`
  in the captured-image cases, over the captured pad bytes, camera yaw and
  scratchpad, with the stick held in three cases per image (the moving turn, the standing turn and the
  reversal gate's 0x70003A20 store in 001612D0); 32 MB of RAM and the
  scratchpad are compared (LOCOMOTION_DISPLAY.md section 3);
- `test_player_fall_reference.py` binds it into `EmPlayerLandWorkers.heading`
  with `world.spad3A20` on the lane's scratch word and compares 0017C580,
  00162DB0 and 00163B40 (PLAYER_FALL.md section 3, "The bound heading").

**Live since census L12 (2026-09-25).** It is the `heading` worker of the
idle / walk states 00161020 / 001612D0 over the record in AREA11
(em_player_closure_live.c; LOCOMOTION_DISPLAY.md section 4), and the
first-control record matches the original's +C4 on every callback whose
camera heading input D_008106A0 matches (test-first-control-reference). The
mirror turn in `em_player.c` (`player_turn_rate` / `player_turn_toward`) and
`em_player_heading.c` remain only in the legacy callbacks of the scenes
without an original world and in the examine stand-in's face step
(em_game.c, census L21). `EmPlayerClimbWorkers.heading` (em_player_climb.h)
still takes the mirror `EmPlayerClimbActor` and cannot take this routine
until the climb module moves onto the record.

## 5. Verification

`python3 tools/test_player_heading_record_reference.py`. 00174AC0 runs
unmodified. Its whole call tree runs as original code under the measured EE
float model: cosf with its kernels and argument reduction, atan2f, fabsf,
001B1470 and 001B12B0. Each case compares all 0x320 record bytes, the
0x70003A20 word and the return value. It also asserts that the original wrote
nothing outside the record and 0x70003A20.

- **Synthetic records:** random record bytes with the read fields set to
  boundary and random values: speeds at the 0.1 / 0.3 / 0.5 edges ±1 ulp and at
  ±0 and a denormal, gait 0..3 plus out-of-range bytes, +5 and +1F0 values, arg
  0 / 1 / 2 / 3 / -1, and an occasional 0x70003B8D. A quarter of the cases aim
  the heading error at 3pi/4 or 0.3pi (±3 ulps, both signs) or at ang == +C4.
  Both outcomes of all 26 conditional branches are exercised, except the taken
  side of the two sign tests on a zero-extended stick byte, which cannot occur.
- **Captured RAM:** opening / handoff / playable and route beats 00..14. Each
  capture runs its own player record, pad bytes, camera yaw and scratchpad with
  arg 0 / 1 / 2 as captured, again with 0x70003B8D cleared where it was set,
  and again with every distinct stick value the beat's route inputs used
  (trace.json) × gait 1..3 × arg 0 / 1 / 2.
- **Fail-stop and adapters:** every unbound pointer, a NULL record or result,
  the 001B1470 bound (prefix writes equal to the original's), and both adapters
  against the routine.

Results (2026-09-24):

| mode | synthetic | captured | time |
|---|---|---|---|
| quick (default) | 6,000 of 200,000 records | 18 captures, 867 runs | 2.9 s |
| `EM_TEST_FULL=1` | 200,000 records (6,224 new skids, 51,574 turns, 1,148 +25D) | 18 captures, 11,577 runs (1,280 route stick values) | 56 s |

0 differences. Mutation check: changing a turn step, regrouping the heading
sum, dropping the second 0x70003A20 store or changing `<=` to `<` at the 3pi/4
gate each fail the quick run. Swapping the /256 and the *pi is the only
mutation that survives, and it is value-equivalent: division by 256 is an
exact scaling.

## 6. Makefile

The make target `test-player-heading-record-reference` exists. When the
binding chain wires the routine live, COMMON needs
`src/game/em_player_heading_record.c` and, unless it is already there,
`src/game/em_script_host_workers.c`. A private lane link of the live build
with both added had zero warnings and no duplicate symbols (2026-09-24).
