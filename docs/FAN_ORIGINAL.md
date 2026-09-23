# AREA11 fan pair: overlay behaviour 0x827630

Status: 2026-09-22. WP-11 component (audit H20/H21, INV-01/02). This covers the translation and its oracle. It is
not bound into the frame yet.

Files:
- `src/game/em_fan_original.{h,c}`: the native translation.
- `tools/test_fan_original_reference.py`: the overlay-instruction oracle.
- `tests/fan_original_test.c`: the native contract test.

Manifest suffix: `Extermination/tools/export_level.py --fan-owner`.

## Identity (checked)

- Placement table `D_0024D7C0[0x0B][0]` = 0x82A3C0. Records **[1]** and **[2]** have behaviour (+0x24) 0x827630 and
  class 4. The records are otherwise identical except for:
  - record byte +3 (`flags2`): 0 and 1;
  - Z: 159.8 and 160.8;
  - rot.y: -pi and 0.
- 001B6990 copies byte +3 into actor +0x2E (`lhu` in 0x827630).
- The overlay function lives at file offset 0x4130 of AREA11.BIN and runs at 0x827630. The splat listing calls it
  `func_overlay_AREA11_008275F0` because the splat vram base for this overlay is 0x40 low. Branch labels in that
  listing are shifted by the same 0x40.
- Every captured RAM image in `Extermination/build/startup-reference` holds the two fans in pool slots 10 and 11:
  callback 0x827630, lifecycle 1, draw callback +0x4C = 0x1CAA00.

## Behaviour (all numbers read from the original code)

**Lifecycle byte +0x04:**
- **0:** calls 001B0FD0(actor). Then +0x38 = 0, and rot.z (+0xC8) is set by +0x2E:
  - +0x2E == 0: rot.z = **+pi/4** (0x3F490FDB);
  - otherwise: rot.z = **-pi/4**.
  - 001B0FD0 then does one of two things:
    - increments +0x04;
    - or, through 001B0EA0 when the bone cap D_00275BCC is exceeded, sets +0x04 = 3 and does not increment it.
- **1:** runs the phase step, then the tail.
- **2 or 3:** calls 001AFC10(actor) (free).
- **Other values:** returns without doing anything.

**Phase byte +0x05 (the spin cycle):**

| Phase | Action |
|---|---|
| 0 | +0x28 = **60**; go to phase 1. |
| 1 | +0x28 -= 1. At 0, go to phase 2. Also at 0, if +0x2E == 0 and D_00810788 != 1: **001FBD50(actor, 0x451, 0, 300.0)**. |
| 2 | +0x38 += **0.0029088822** (0x3B3EA2F2). When the result is not < **0.34906587** (0x3EB2B8C3): go to phase 3, +0x28 = **30**. There is no clamp: the last step can overshoot. |
| 3 | +0x28 -= 1. At 0, go to phase 4 and set rot.z **absolutely** to **-1.8325958** (0xBFEA9280) when +0x2E == 0, otherwise **+1.8325958**. |
| 4 | +0x38 -= 0.0029088822. When the result is <= 0: +0x38 = 0, go to phase 0. |
| >= 5 | No step; the tail only. |

**Tail (lifecycle 1):**
1. rot.z += +0x38 when +0x2E == 0; otherwise rot.z -= +0x38.
2. Wrap rot.z with 001B1470 into (-pi, pi], then store it.
3. Call 001C6380(actor) (TRS matrix).
4. Only when +0x2E == **1** and D_008106B8 (request B8) == 0:
   - **Slow arm, +0x38 < 0.034906585 (0x3D0EFA35).** No publication and no player-state gate. If the player
     (D_008102B0) is inside Y (280, 320), X (318, 340) and Z < 156, call **exit-or-bit**.
   - **Fast arm.** Call 001B17A0(actor) (its result is ignored). Then, if player +0x00 == 1 and the player is in
     the same Y/X box:
     - Z < 156: exit-or-bit;
     - otherwise, Z < **166.5**: the **hit**. It writes player +0x224 = 5.0, +0x00 = 3, +0x0F = 6, +0x70 = 0,
       +0x74 = 0, +0x78 = 1.0, +0x7C = 1.0. The fan has no latch of its own. A repeat needs player +0x00 to be back at 1.
   - **Exit-or-bit.** If D_00810758 == 0xFF, call **001B0C60(1, 1, 4)**. Otherwise set **D_008107D8 |= 0x80**.
     The Roger controller 0x8237E0 (em_roger.c, oracle-verified) starts its departure script 0x828A10 when that
     bit is set.
5. Call `jalr *(actor + 0x4C)(actor)` (draw).

Box comparisons follow the original's ordered `c.le`/`c.lt` tests, so NaN never passes.

**Corrections to the audit wording (H20):**
- The hit also writes +0x70..+0x7C.
- The hit exists only in the fast arm and needs player +0x00 == 1.
- The Z < 156 exit is checked in both arms.
- The whole box belongs to record 2 alone (+0x2E == 1) and is skipped while B8 != 0.
- The 0x451 sound fires once per cycle, at the end of the 60-tick wait.
- Phase 3 ends by snapping rot.z to ∓105°.

**Arithmetic.**
- add.s and sub.s follow the single-guard-bit EE model, using `pose_add`/`pose_sub` from em_pose_math.h.
- The oracle's capture check steps the cycle from spawn and finds **all 9 distinct captured fan states per fan** (18
  in total, from 11 RAM images) on the trajectory.
- Plain truncation reproduces none of the 9 states, and neither does round-to-nearest. The rounding model therefore
  matters here, and the captures confirm it.

## Verification

`python3 tools/test_fan_original_reference.py` executes the original instructions of:
- 0x827630, from the user's AREA11.BIN;
- 001B0FD0, 001B0EA0 and 001B1470, from the pinned ELF.

It stubs only their leaf callees. It compares every call and argument, including rot.z at the matrix call, the
cue, the range and the (1,1,4) arguments. It also compares every modelled byte: the actor, the player and the four
globals.

It asserts that the original writes no byte outside the modelled or worker-owned set. The worker-owned bytes are
001B0EA0's +9, +0xC and +0x110 slots.

Case groups:
- lifecycles and the bone cap;
- phase × timer × flags2 × spin × rot.z edges;
- box edges on both arms;
- globals;
- 1500 random states;
- 8400 lockstep ticks over whole cycles, with a player walking through the exit and hit bands;
- 514 direct 001B1470 wraps;
- the capture check.

Six injected native defects were each caught: hit Z, the +0x0F write, rounding model, arm selector, the 758 test,
and hold length.

`tests/fan_original_test.c` pins the native contract:
- fail-stop faults;
- the latched fault;
- no tick after free;
- the 001B0FD0 result range;
- phase lengths 60/30;
- the sound firing once for record 1 only.

## Boundaries (workers; each is required when reached, and a NULL or negative result faults)

| Worker | Original | Contract |
|---|---|---|
| `w_001B0FD0` | 001B0FD0 minus its +0x04 writes | 001B0EA0's model/bone init plus bone_init_default_1 (001C62C0) on success. Returns 0 or 1. The module applies +1 or =3. |
| `w_001FBD50` | 001FBD50(actor, 0x451, 0, 300.0) | Sound. The adapter maps the original's -1 ("no channel") to 0. |
| `w_001C6380` | 001C6380(actor) | World matrix from pos, rot (with `rot_z`) and scale. |
| `w_001B17A0` | 001B17A0(actor) | Publication. The result is ignored by 0x827630. |
| `w_001B0C60` | 001B0C60(1, 1, 4) | **The area-change request:** 3B8D = 3, 001B0C00(4), B5 = 1, B6 = 1, B7 = 4, B8 = 1. The consumer (001AD010 → 001ADF50 → state 0) is the coordinator's (ORCH-06). |
| `w_draw_4C` | *(actor+0x4C) | The actor's draw callback (0x1CAA00 in every capture). |
| `w_001AFC10` | 001AFC10(actor) | Pool free. |

**Data views** (they fault with the data address when needed and NULL):
- `EmFanOriginalGlobals` (D_00810788, D_008106B8, D_00810758, D_008107D8): required in lifecycle 1.
- `EmFanOriginalPlayer` (D_008102B0 +0x00, +0x0F, +0x70..7C, +0xA0..A8, +0x224): read only by record 2's box.

Player fields are named by offset only. `em_player_damage.c` treats +0x224 and +0x0F as a pending-damage pair; this
lane did not verify that consumer.

Not modelled: whatever 001B0C60, 001B17A0 and the draw callback do beyond being called. The area-change targets
(AREA01 sub 1 entry 4) are not exported (INV-02).

## Binding (for the coordinator, nodes #10–11 = area11[1]/[2])

- **Spawn.** Call `em_fan_original_spawn(&fan, flags2, placement_rot_z)` from the roster record: flags2 = record
  byte +3, rot.z = record +0x20.
- **Per tick.** Call `em_fan_original_tick(&fan, &player_view, &globals_view, &workers, &scene->fault-compatible
  record)`:
  - fault codes equal `EM_SCENE_FAULT_*`;
  - the call returns 1 while allocated, 0 after the free, and -1 on a fault.
- **Views.** Map the views onto the canonical storage and write them back after the tick:
  - B8 → `EmSceneState.req[EM_SCENE_REQ_B8]`;
  - 758/788/7D8 → the D2 `EmProgress` region.
- **Draw.** The draw worker renders the fan with `rot_z` as the Z leg of the TRS. The static pose in em_pickup
  already applies the lifecycle-0 rot.z when the manifest carries `owner 0x827630 <flags2>`.
- **Manifest.** `export_level.py --fan-owner assets/scene_snow --area 11 --sub 0 --overlay
  extract/OVERLAY/AREA11.BIN` writes that suffix. em_scene.c accepts it: a lane-build `EM_STARTUP_TEST=newgame-control`
  run loads both lines and passes.
