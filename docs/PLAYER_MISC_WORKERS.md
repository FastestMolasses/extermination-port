# Player misc workers (sound at an object, rand, ledge probes, side clips, model kind)

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "player-misc-workers". The hang, recovery, major2, reaction, fall,
ladder and closure translations name these original routines as workers
without translating them. This module translates them from the original
instructions, so that the coordinator can bind them. It is **built and tested
but not wired**. Section 4 lists what the coordinator binds.

| Routine | What it is | Decomp source read |
|---|---|---|
| 00122BB8 | rand: the one shared LCG | byte-matched C |
| 001FBD50 | play a sound at an object | byte-matched C |
| 001FBF50 | positional gain (distance fade and pan) | byte-matched C |
| 001B15D0 | distance, used by 001FBF50 | byte-matched C (mwcc 2.3.3) |
| 00182250 | hang aim track | byte-matched C |
| 0017E250 | ledge-ahead probe | NEARMISS: the `.s` |
| 0017E510 | ledge-above probe | byte-matched C |
| 0017E7C0 | hang side probe | byte-matched C |
| 0017DF70, 0017E0D0, 0017E150, 0017E1D0 | side clip requests | byte-matched C |
| 0017DFB0 | side clip request with the ahead probe | asm body: the instruction words |
| 0017FF80 | clip request picked by +2F1 | byte-matched C |
| 00182AF0 | sound (00179B90 + 0x100) at range 300 | byte-matched C |
| 00177B80 | ledge depth probe | NEARMISS: the `.s` |
| 0021E650 | the +7 countdown over the clip clock +3C | byte-matched C |
| 0015C1F0 | player model kind (+2FF) and rebind | NEARMISS: the `.s` |
| 001EFE00 | effect spawned at the actor and linked to it | asm body: the instruction words |

Three routines on the task list are **not translated again**:
- 0021C120, 0021C190 and 0021D490 are already translated and
  oracle-tested in `em_player_reaction.c`, as
  `em_player_reaction_w0021C120`, `_w0021C190` and `_w0021D490`
  (docs/PLAYER_REACTION.md).
- This module supplies the callees those three did not have:
  001FBD50 (the `sound` slot), 001EFE00 (the `attach` slot) and 0015C1F0
  (the `model_refresh` slot).

## 1. What the original does

Every routine works on the 0x320-byte player record ("p") by original
offsets. In this section, "scratch" means the EE scratchpad words at
0x700034xx..0x70003Axx.

### 00122BB8: rand

- The state word is at `(*D_0024295C) + 0x58`. In the ELF image it is 1;
  the test checks this.
- Each call sets `state = state * 0x41C64E6D + 0x3039` and returns
  `state & 0x7FFFFFFF`.
- The port holds that state once, in `em_random.c` (`em_random_next`). The
  module adds no second copy. `em_player_misc_random` (u32 out) and
  `_random_i32` (i32 out) are the worker-shaped entries onto it.
- A search of `src/` found no private copy of the multiplier outside
  `em_random.c`.

### 001FBF50(obj, &a, &b, flat, radius, scale): the positional gain

`obj` is the object's +B0 quadword. The routine:

1. Clears both gains.
2. Copies the listener D_00810360 to 0x70003600 and obj to 0x70003610.
   When the low byte of `flat` is nonzero, it zeroes both y values.
3. Gets `dist = 001B15D0(3600, 3610)`. 001B15D0 subtracts the two points
   into 0x70003600 with 001028D0, then returns the sqrt (0011E748) of
   x*x + y*y + z*z. The sum is built as mul, mul, adda, madd.
4. If `dist` is not less than `radius`, it returns 0 (out of range).
5. Computes `scaled = scale * sin(pi/2 * (radius - dist) / radius)`.
6. Builds the listener yaw matrix in 0x70003400: identity, then 00102C58
   with the angles (0, D_0081027C, 0, 1). It transforms (0, 0, 1, 1) by that
   matrix into 0x70003600, which gives the facing vector.
7. Computes the direction from the camera eye D_008105D0 to obj into
   0x70003610 (y = 0), normalized.
8. Sets a weight: `weight = dist / 18` (as `0.055555556 * dist`) when
   dist <= 18, else 1.
9. **Mono option** (D_0028215B == 1): both gains are `float_to_int(scaled)`,
   and the routine returns 1.
10. **Stereo:** `t = dot(facing, direction)` and `k = t^5 * weight`.
    - If k < 0, `k -= 1 - weight`. Otherwise `k += 1 - weight`.
    - 001B1380(obj, eye, D_0081027C) picks the side. Nonzero gives
      `a = float_to_int(scaled)` and `b = float_to_int(scaled * k)`.
      Zero gives the same values with a and b swapped.
    - The routine returns 1.

### 001FBD50(obj, id, flat, radius): the sound at an object

- It calls `001FBF50(obj, &a, &b, flat, radius, 4096.0)`.
- Out of range, it returns -1.
- Otherwise it returns `001FB9F0(id, 0x1000, a, b)`.
- The player routines call it as `(p, id, 0, 300.0)`.
- 0017DFB0 and 0021E650 reach it (sounds 0x105/0x106 and 0x156), and so
  does 00182AF0.

### 00182250: the hang aim track

1. Builds the actor matrix in 0x700036A0: identity, then euler with +C0,
   then translate by +B0.
2. Transforms (0, 20, 5, 1) into 0x700038B0.
3. Calls `0019AD00(p, 38B0, 6)`. With no hit, it returns.
4. With a hit, it reads the hit node's normal (+24 x, +2C z) and stores
   `atan2(-nz, nx)` at 0x70003A20.
5. Computes `yaw = wrap(3pi/2 + that)`. The wrapped error `wrap(yaw - +C4)`
   goes to 0x70003A24.
6. When |error| <= 0.43982297:
   - +B0 = hit point x + 1.5 * nx
   - +B8 = hit point z + 1.5 * nz
   - +C4 = yaw

### 0017E250(p, v): ledge ahead

1. Builds the actor rotation in 0x700036A0 (identity, then euler +C0).
2. Copies v's x, y, z into row 3 (0x700036D0), which makes v the
   translation. It then adds 20.5 to that row's y.
3. Calls `001760C0(p, 36D0, 1, 14.0)`. A nonzero result returns 1.
4. Otherwise it runs three sweeps `0019AFE0(p, 38C0, 38D0, 7)` and returns
   the OR of their results. Each sweep goes from (x, 4.01, 0, 1) to
   (x, 4.01, 5, 1), both through the matrix, with x = 0, then -3, then 3.

The hang calls it as (p, p + B0); the ladder climb passes a stack vector.

### 0017E510(p): ledge above

1. Builds the same matrix, translated by +B0.
2. Runs three columns `001760C0(p, 38B0, 1, 9.99)` at (x, 24.51, 3.62, 1),
   with x = 0, -4.5 and 4.5, each through the matrix.
3. Returns 1 when any column is nonzero, else 0.

### 0017E7C0(p, side): the hang side probe

It returns 0, 1, 2 or 0xA and can write +1F1, +D, +218 and +2E0..+2E8.

**When +D == 1 (climbing):**
1. Three edge probes `0017E6E0(p, side, x, -5)`, with x = 20, 10 and 0.
   The first nonzero one returns 0.
2. A sweep with mask 6 from (∓4.5, 20, 0) to (∓4.5, 20, 2), through the
   +D0 matrix. x is negative for side 0.
   - A hit on surface 0x3D sets +1F1 = 0 and returns 1.
   - No hit marks "found".
3. A sweep with mask 7 from (∓9, 19.5, 0) to (∓9, 19.5, 10). With no hit on
   bits 6, it returns 0xA.
4. Surface 0x32 or 0x3B calls 001782A0 (grab). On success it sets +1F1 = 1
   and +D = (surface == 0x3B), then returns 2. On failure it returns 0.
5. Otherwise, when found:
   - **In area 8 room 3** (D_00810700/701) with 120 < +B0 < 130 and
     160 < +B8 < 170: it sets +218 = +C4, +2E0..+2E8 =
     (123.5, 257.5, 156.4) and +1F1 = 5, then returns 2.
   - Otherwise it returns 0xA.

**Otherwise:**
1. Outside area 0x11, 001784E0 (reach) nonzero sets +1F1 = 2 and returns 2.
2. A mask-7 sweep at (∓9, 20, -2..3.5). A hit with bits 6 checks the
   surface:
   - 0x32/0x3B calls 001782A0 (grab). Nonzero sets +1F1 = 1 and
     +D = (surface == 0x3B), then returns 2. Zero falls through to step 3
     (unlike the climbing path, which returns 0).
   - 0x33 calls 00178440; nonzero sets +1F1 = 6 and returns 2.
3. `0017F130(p, side)` nonzero returns 0.
4. Eight edge probes: (20, -5), (12, -5), (4.01, -5), (-0.5, -5), (20, 0),
   (12, 0), (4.01, 0) and (-0.5, 0). The first nonzero one returns 0.
5. A mask-7 sweep at (∓4.4, 20, -2..4.5). With bits 6, `00178910(p, 1)`
   nonzero sets +1F1 = 0 and returns 1; zero returns 0.
6. Otherwise the last segment is shifted by (±0.1, 0, 0) through the +D0
   matrix, into 0x700038E0/38F0 with 0x70003900/3910 as temporaries. That
   segment is swept once more: a hit returns 1, a miss returns 0xA.

### The side clip requests (p, side, blend)

Every request is `001749A0(p, clip, 0, blend)`. The blend in $f12 passes
through unchanged.

- **0017DF70:** clip 0x7E for side 0, else 0x7F.
- **0017DFB0:**
  - When +D != 1 and 0017F1C0(p) is nonzero: +315 = 0, clip 0x84/0x85,
    then sound 0x105/0x106 at the actor (001FBD50, range 300).
  - Otherwise: +315 = 1, clip 0xCC/0xCD.
  - The +315 store sits in the request's delay slot, so it happens before
    the call.
- **0017E0D0, 0017E150, 0017E1D0:** the clip depends on side and on
  +315 == 0:
  - 0017E0D0: 0x86/0x87 when +315 is 0, else 0xCE/0xCF.
  - 0017E150: 0x88/0x89, else 0xD2/0xD3.
  - 0017E1D0: 0x81/0x82, else 0xD0/0xD1.
- **0017FF80(p, blend):** the clip is 00188570(p) when +2F1 == 0, else
  00188590(p). The full return value passes on as the clip.

### 00182AF0(p)

`001FBD50(p, 00179B90(p) + 0x100, 0, 300.0)`.

### 00177B80(p, y): ledge depth

It works over the ledge frame that 00177510 left: point 0x70003050,
normal 0x70003060 and matrix 0x70003070.

1. Sets `A = (point.x + 1.5 * normal.x, y, point.z + 1.5 * normal.z, 1)` in
   0x700038A0.
2. The offsets (-4.5, -20.5, 0, 0) and (4.5, -20.5, 0, 0), in 0x700038B0,
   are each transformed by the matrix into a stack vector and added to A.
3. Calls `0019AB20(p, that, p + 0x280, 6)` for each.
4. Returns 1 when either finds ground, else 0.

### 0021E650(p): the +7 countdown

- For +7 = 0..4, it compares +3C with the limits 173, 105, 85, 60 and 45.
  When +3C is past the limit for the current +7, +7 increments, and then:
  - +7 was 4: `001B61C0(0, 0xD0, 0xA, 1)`, then 0021D490(p).
  - otherwise: `001B61C0(0, 0xC0, 5, 1)`. For +7 = 0 it then calls
    00182870(p, 1); for +7 = 3 it plays sound 0x156.
- +7 > 4 does nothing.

### 0015C1F0(p): the player model kind

1. Picks the kind:
   - +234 == 0: 0x3B, or 0x3F/0x3E when D_00810C60 is 2/1.
   - +234 == 1: 0x40, or 0x3F/0x3E the same way.
   - otherwise: 0x3D.
2. Sets +2FF = kind.
3. Calls `001CA6E0(p, D_0028A490[+2FF])` to bind the model. That writes
   +44.
4. Sets +C = 001C6150(+44), the byte at model + 8.
5. Stores the halfword +96 = 0x28.
6. Calls 00200890().

### 001EFE00(id, p): an effect at the actor

1. Copies +B0 to the stack. For id 0x80000027, it adds 10 to y.
2. Calls `001EF9D0(id, &copy, 1.0)`.
3. On a nonzero record r:
   - `r+24 = p+14`
   - `r+B0..BF = p+B0..BF`
   - `r+C0..CF = p+C0..CF`
4. Returns r.

## 2. The translation (`src/game/em_player_misc_workers.c/.h`)

- **One entry point per routine.** `em_player_misc_<address>(EmPlayerMiscHost *h, ...)`
  returns 0 or -1, and the original's return value goes to `*result`.
- **The host** carries three things:
  - `EmPlayerMiscWorkers`: every untranslated callee, as a worker.
  - `EmPlayerMiscScene`: the globals the routines read, at the moment they
    read them. These are D_00810700/701, D_00810C60, D_0028215B, the
    listener D_00810360, the camera yaw D_0081027C, the eye D_008105D0 and
    the model table D_0028A490 with its count.
  - `EmPlayerMiscScratch`: the scratchpad words above, as raw bits.
    0x700036D0 is row 3 of the 0x700036A0 matrix, as in the original.
- **Fail-stop:**
  - Before its first write, each routine checks that every worker it can
    reach is bound, along with the scene and scratch it reads. Otherwise it
    returns -1 and writes nothing.
  - A worker that returns a negative value stops the routine with -1. The
    writes made before the call are kept, in the original order.
  - A D_0028A490 index at or beyond the table count faults. The original
    would read past the table.
- **Floats:**
  - COP1 arithmetic goes through `em_ee_float.h`: the `*_bits` operations
    and the c.lt/c.le compares on raw binary32.
  - Values that are only moved are copied as bits.
  - float_to_int is the existing `em_player_float_to_int` (001281C0,
    `em_player_stage_workers.c`).
  - There are no private float helpers.
- **Leaves translated inline:** 00102948 (quadword copy), 001031E0
  (three-word copy) and 0011DF78 (clear the sign bit).
- **Adapters:** the `em_player_misc_w_*` functions match the consumers'
  worker slot shapes. The context is an `EmPlayerMiscHost *`. Each refuses
  with -1 before any write when something it can reach is missing.

## 3. Verification

`tools/test_player_misc_workers_reference.py` (`make test-player-misc-workers-reference`).

### Unit oracle (default: about 2–4 s)

The user's ELF supplies every instruction; nothing is embedded in the test.
The oracle runs the original routines unmodified in the shared EE
interpreter. `MiscEE` is `FallEE` imported, not edited. It routes every COP1
and VU0 macro op through `tools/ee_float_model.py`.

- **Callee set.** The test asserts that the `jal` targets of all the
  routines are exactly:
  - the hooked workers;
  - the inline leaves;
  - the routines translated here.
  There are 43 targets. The test also asserts that no hooked address goes
  uncalled.
- **Pass-through leaves.** The SDK leaves are workers in the native module:
  VU0 matrix and vector routines, sin, atan2, sqrt, 001B1470 and
  001B1380. The hook still runs their original code, and the native side
  receives the products the original produced.
- **Scripted callees.** The game callees are scripted per case with a
  return value, record bytes and a hit record. These are the collision
  probes, clip requests, submit, 00179B90, 001B61C0, 00182870, 0021D490,
  model bind, 001EF9D0 and the others.
- **Deep paths.** A per-case `force` table pins early results so that the
  deep paths of 0017E7C0 run: the area 8 room 3 box branch and the ledge-end
  sweeps.
- **What is compared** for each case:
  - every worker call, with its arguments (pointers as the buffer they name
    and the words they point at);
  - all 0x320 record bytes;
  - every scratchpad word;
  - the return values;
  - 001FBF50's two gains;
  - 001EFE00's record bytes.
- **Adapters.** Every second case runs again through the adapter a consumer
  binds, and is compared the same way.
- **Fault-stop and refusal.** Every third case cuts one worker call with a
  fault: the routine must return -1 and call nothing after it. Every
  seventh case removes each worker, the scene and the scratch in turn:
  - when the removed item is reachable, the routine must refuse with no
    call and no write;
  - otherwise the routine must run identically.
- **Branch coverage.** The default sample has to take both outcomes of
  every conditional branch in the translated routines (91 branches). The
  test asserts this.
- **rand.** 00122BB8 runs on its own state word, and the native draws from
  `em_random.c` after `em_random_seed`. The test uses 15 seeds with 200
  draws each (71 × 2000 in the full sweep), including 0, 1, 0x7FFFFFFF,
  0x80000000 and 0xFFFFFFFF.

Latest default run: PASS on 2,400 of 30,000 cases, covering every entry.
The run made 13,063 worker calls, all identical, and took every one of the
91 branches both ways. It also ran 1,079 adapter runs, 764 fault-stop cuts,
3,641 missing-worker refusals and 3,000 rand draws.

`EM_TEST_FULL=1` runs all 30,000 cases. Latest full run (2026-09-23): PASS
on 30,000 cases, with 164,526 worker calls identical, all 91 branches taken
both ways, 13,968 adapter runs, 9,564 fault-stop cuts, 42,735 refusals and
142,000 rand draws (62 s).

### World mode (`EM_TEST_WORLD=1`, minutes)

Which routines does the route reach? A survey replayed the player stage over
the route beats (`shared.RouteReplay`, the whole trace) with every routine
here hooked and counted:

| Beat | Frames | 00122BB8 calls | Routines reached |
|---|---|---|---|
| 05_boxes | 677 | 28 | 001FBD50, 00122BB8 |
| 06_hill_slide | 212 | 12 | 001FBD50, 00122BB8 |
| 11_crevice_prompt | 1261 | 54 | 001FBD50, 00122BB8 |
| 12_crevice_jump | 345 | 32 | 001FBD50, 00122BB8 |

Beats 10 and 14 were not surveyed to the end. Their stage-only replay is
valid only for part of the trace (docs/PLAYER_FALL.md).

On the surveyed beats the route reaches only **001FBD50 (with 001FBF50 and
001B15D0 under it) and 00122BB8**. No surveyed beat hangs from a ledge,
changes the player model or runs the +7 countdown. For those routines the
unit oracle is the only evidence.

World mode replays each of those beats twice from its source snapshot:

- **original:** 001FBD50 and 00122BB8 execute their original instructions.
  001FB9F0 is recorded, as the shared replay records it.
- **native:**
  - 001FBD50 is `em_player_misc_001FBD50`. Its workers are bound to the
    ORIGINAL leaves in the same EE, with the scratch synced around each
    call and the scene read from RAM at the call.
  - 00122BB8 is `em_player_misc_random`, drawing from `em_random.c`'s state.
    That state is seeded from the snapshot's state word when the replay
    starts.

Each frame must match in the following:
- all of RAM and the scratchpad. The rand state word is left out: only
  00122BB8 reads it, and the native keeps it in `em_random.c`. Only the
  state's low 31 bits are ever observable, and they are compared through
  the draws;
- the player record;
- the trace rows, within the replay's precision;
- every 001FBD50 call and its return;
- every 001FB9F0 submit, with the id and both gains;
- every rand draw.

`EM_WORLD_BEATS=05_boxes,...` narrows the run.

Latest world run (2026-09-23, 05_boxes, 06_hill_slide and 12_crevice_jump,
about 17 minutes with other jobs running): PASS.

| Beat | Frames identical | Trace rows | 001FBD50 calls (all in range, submits identical) | rand draws |
|---|---|---|---|---|
| 05_boxes | 677 | 677 | 34 | 28 |
| 06_hill_slide | 212 | 206 | 14 | 12 |
| 12_crevice_jump | 345 | 337 | 36 | 32 |

Every route call was in range, so the -1 (out of range) return is covered
only by the unit oracle. 11_crevice_prompt (1261 frames) is left out of the
default world list to keep the run in minutes. Add it with
`EM_WORLD_BEATS`.

## 4. Binding (coordinator)

The context of every adapter is one `EmPlayerMiscHost`. The binder:

- keeps `scene` current (it is read at call time);
- points `scratch` at the frame's single scratchpad image, shared with the
  other player modules that use 0x700034xx..0x70003Axx;
- binds the workers below.

| Consumer slot | Adapter |
|---|---|
| `EmPlayerStageCallees.sound`, `EmPlayerHangWorkers.sound`, `EmPlayerMajor2Workers.sound`, `EmPlayerLadderWorkers.sound` (ladder climb), `EmPlayerClosureWorkers.sound`, `EmPlayerClosure1019Workers.sound`, `EmPlayerWeaponWorkers.sound` | `em_player_misc_w_sound` |
| `EmPlayerReactionWorkers.sound`, `EmPlayerRecoveryWorkers.sound` | `em_player_misc_w_sound_300` |
| `EmPlayerLandWorkers.sound`, `EmPlayerLadderWorkers.sound` (ladder entry), `EmPlayerRunningJumpWorkers.sound` | `em_player_misc_w_sound_300_i` |
| `EmPlayerWeaponBWorkers.sound` (keeps the handle) | `em_player_misc_w_sound_300_handle` |
| `EmEffectOriginalWorkers.w_001FBF50` | `em_player_misc_w_001FBF50` |
| `EmPlayerHangWorkers.aim_track` | `em_player_misc_w_aim_track` |
| `EmPlayerHangWorkers.ledge_ahead` | `em_player_misc_w_ledge_ahead_self` |
| `EmPlayerLadderWorkers.ledge_ahead` (ladder climb) | `em_player_misc_w_ledge_ahead` |
| `EmPlayerHangWorkers.ledge_above` / `.ledge_side` | `em_player_misc_w_ledge_above` / `_w_ledge_side` |
| `EmPlayerHangWorkers.clip_DF70..clip_E1D0`, `EmPlayerClosureWorkers.clip_DFB0..clip_E1D0` | `em_player_misc_w_clip_DF70` .. `_E1D0` |
| `EmPlayerHangWorkers.clip_FF80`, `EmPlayerMajor2Workers.w0017FF80`, `EmPlayerClosureWorkers.clip_FF80` | `em_player_misc_w_clip_FF80` |
| `EmPlayerHangWorkers.sound_100` | `em_player_misc_w_sound_100` |
| `EmPlayerRecoveryWorkers.depth` | `em_player_misc_w_depth` |
| `EmPlayerMajor2Workers.w0021E650` | `em_player_misc_w_0021E650` |
| `EmPlayerMajor2Workers.w0015C1F0`, `EmPlayerReactionWorkers.model_refresh` | `em_player_misc_w_0015C1F0` |
| `EmPlayerStageCallees.w001EFE00`, `EmPlayerMajor2Workers.w001EFE00` | `em_player_misc_w_001EFE00` |
| `EmPlayerReactionWorkers.attach` | `em_player_misc_w_attach` |
| `random` of `EmPlayerStepWorkers`, `EmPlayerMajor2Workers`, `EmPlayerReactionWorkers`, `EmPlayerRecoveryWorkers`, `EmCrateOriginalHooks`, `EmDrumOriginalHooks` (u32 out) | `em_player_misc_random` |
| `w_00122BB8` of `EmEffectOriginalWorkers`, `EmHeadSpriteOriginalWorkers`, `EmStreamLanesWorkers`, `EmStatusSceneWorkers` (i32 out) | `em_player_misc_random_i32` |

Some slots keep the actor in their own context: `EmPlayerStepWorkers.sound`
(ctx, id), `EmPlayerClimb*` (ctx, id), the slide's (ctx, id, *handle), the
crate, drum and truck `sound3d`, and `EmStatusBackground`'s
`int32_t random(void *)`. These need a small closure in the binder. It calls
`em_player_misc_001FBD50(host, actor + 0xB0, id, flags, radius, &v0)` or
`em_random_next()`. The slide stores v0 in +31B, so it needs the v0 (the
`_handle` shape).

**The one RNG.** Every consumer binds to `em_random.c`'s state. This
includes the status hub, the 0020A7A0 pulse timers and the 001FAE70 fades.
Nothing may keep a second LCG, since the original has one state word and
every draw advances it.

### Workers this module needs from others

"Exists" means a native translation of that original was found in `src/game/`
(2026-09-23). Its fit to this worker's signature still has to be checked
when binding.

| Worker | Original | Native translation |
|---|---|---|
| identity, euler, transform, normalize, dot | 001029C0, 00102C58, 001026A0, 00102760, 00102738 | exists: `em_owner_services_identity_001029C0` / `_euler_00102C58`, `em_effect_original_00102C58` / `_00102760` / `_001026A0`, `em_crate_sdk_*` (out may alias an input) |
| translate, vadd, vsub | 00102918, 001028B8, 001028D0 | no standalone translation found |
| sine, atan2, sqrt | 0011E2A8, 0011E620, 0011E748 | exists: `em_sdk_math_original_0011E2A8` / `_0011E620` / `_0011E748` |
| wrap | 001B1470 | exists: `em_player_001B1470` (em_player_stage_workers.h) |
| side | 001B1380 | exists: `em_script_host_w_001B1380` (same shape) |
| submit | 001FB9F0 | the sound bank path (em_sfx_bank.h); no worker-shaped entry yet |
| sound_base | 00179B90 | none (floor and closure name it as their `random5` worker) |
| request | 001749A0 | exists for pose hosts: `em_pose_host_001749A0` |
| clip_2F1_0 / clip_2F1_1 | 00188570 / 00188590 | none |
| ahead | 0017F1C0 | none |
| move, sweep | 0019AD00, 0019AFE0 | exists: `em_coll_move_0019AD00` / `em_coll_move_sweep_0019AFE0` |
| hit_node_word, hit_node_byte, hit_point_word | reads of 0x700031D0 / 0x700031B0 | the `EmCollMoveScratch` the probe wrote |
| column | 001760C0 | none as an exported routine (hang and running-jump take it as a worker) |
| ground | 0019AB20 | exists: `em_actor_collision_ground_0019AB20`; it writes record +280.. itself |
| ledge_top | 00178910 | exists: `em_pose_host_00178910` (EmPoseHost over a record; it reads the sweep hit the preceding 0019AFE0 left) |
| edge, grab, grab_33, reach, blocked | 0017E6E0, 001782A0, 00178440, 001784E0, 0017F130 | none |
| cue | 001B61C0 | exists: `em_player_rumble_001B61C0` (em_player_ladder_entry.h) |
| land_sound | 00182870 | exists: `em_player_reaction_00182870` |
| w0021D490 | 0021D490 | exists: `em_player_reaction_w0021D490` |
| bind_model | 001CA6E0 | exists for Roger's record (`em_roger_actor_001CA6E0`); the player record needs its own binding |
| bone_count, w00200890 | 001C6150, 00200890 | none |
| spawn | 001EF9D0 | exists: `em_effect_original_001EF9D0`. `*node` is the record handle, and `view` points at the record's +24/+B0/+C0 storage |

The three hit readers work as follows:
- `hit_point_word(off)` reads `point[off/4]`.
- `hit_node_word(0x24/0x2C)` and `hit_node_byte(0x1A)` read the record
  0x700031D0 names. For a grid node that is the `record_normal[0]/[2]` and
  the low byte of `record_node`. For the cell record it is `cell_normal`
  and `cell_class`.

**The ledge frame (00177B80).** The routine hands `ledge->matrix` to the
transform worker. In the original that pointer is 0x70003070. A binder that
shares one scratch image must pass the same storage.

## 5. Limits and open items

- **No route evidence** for the ledge, hang and model routines: 00182250,
  0017E250, 0017E510, 0017E7C0, 00177B80, 0015C1F0, 0021E650, 001EFE00 and
  the side clips. No route beat reaches them. Their evidence is the unit
  oracle over randomized records with scripted callees: the original
  instructions, both ways of every branch.
- **World mode runs the player stage only.** No camera stage, owners or
  scripts run. So 001FBD50 calls from owners (doors, the elevator, the
  truck) are not in the route comparison. The same routine serves them.
- **Unbound workers.** Several workers above have no native translation
  yet: 00102918, 001028B8, 001028D0, 001FB9F0 (as a worker), 00179B90,
  00188570, 00188590, 0017F1C0, 001760C0, 0017E6E0, 001782A0, 00178440,
  001784E0, 0017F130, 001C6150 and 00200890. Until they are
  bound, the routines that reach them refuse (-1).
- **Unwired.** The module is not wired into the live game. The Makefile
  hunks are in the lane report.
