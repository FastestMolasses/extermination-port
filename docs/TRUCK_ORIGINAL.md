# AREA11 truck set piece (WP-12, H16)

Module: `src/game/em_truck_original.{h,c}`. Oracle and PCSX2 replay:
`tools/test_truck_original_reference.py`. Unit test: `tests/truck_original_test.c`.

**Status: live since census L23 (2026-09-24).** Both owners run on their pool
nodes (`em_area11_boxes.c`, section "Binding" below); the legacy static truck
`em_truck.c` is deleted. The level smoke's `truck_preview` and
`truck_crossing` phases reproduce route beats 07 and 08 (LEVEL_SMOKE.md).

Two AREA11 overlay owners:

| Node | Record | Runtime callback | Role |
|---|---|---|---|
| #24 | 16 | `0x823FF0` | the truck |
| #25 | 17 | `0x8251E0` | the camera trigger |

Runtime address = AREA11.BIN file offset + `0x823500`. The splat labels for this overlay are `0x40`
lower (splat `func_overlay_AREA11_00823FB0` is `0x823FF0`, and `..._008251A0` is `0x8251E0`).

## Verified behaviour

Every item below was read from the overlay routines. The oracle checks it by executing the original
instructions, and the PCSX2 captures below confirm it.

**Trigger `0x8251E0`**
- **State 0:** goes to state 3 if `D_00810792 != 0`, otherwise to state 4.
- **State 4:** goes to state 3 if `D_00810792 != 0`. Otherwise it tests player `+0xA0` x/z (`D_00810350`,
  `D_00810358`) against the band union 312<x<336 × 413<z<427 or 319<x<336 × 390<z<427, and requires
  `D_008102B5` (player `+5`) < 2. On a hit it writes `+0x0B = 4`, calls `001BA1A0(+0x1F0, 0x8292C0)` and
  moves to state 1.
- **State 1:** calls `001BA1F0(actor)`. When that returns nonzero, it sets `D_00810792 = 1` and moves to state 3.
- **Any other state (3 included):** `001AFC10`.

The trigger does **not** arm the truck.

**Truck `0x823FF0`**
- **State 0:** calls `001B0FD0` and waits while it returns nonzero.
  - If `D_00810792 == 0xFF`, it writes the fallen matrix constant into `+0xD0`, then publishes the pose
    (`102958` into `*D_00275B40 + 0x90`) and the hull (`001A2370`), and goes to state 2.
  - Otherwise it runs `001C6380`, stores `+0x2E8 = +0xC0` and `+0x2E4 = +0xB4`, sets `+0x2EC = 0`, copies the
    matrix to `+0x1F0`, publishes the hull and goes to state 4.
- **State 4, idle:** arms when all three hold:
  - `D_008104C4` (player `+0x214`) is nonzero.
  - `D_008102BA` (player `+0x0A`) is nonzero.
  - That ground actor's byte `+0x0D` is 9.

  It does not check that the ground actor is the truck.
- **State 4, arming:** `001B1E20(0,0)`, then `+0x2EC = 1`. The translation gets the offset
  `((n%20)−10)/50` on y, and half of that on x (+) and z (−).
- **State 4, shake (`+0x2EC` 2..46):** the jump table `0x82ABC0` is indexed by `n & 15`:

  | `n & 15` | Action |
  |---|---|
  | 0 | Rx(−0.002)·rest, and y = rest_y − 0.04·(n−1) |
  | 3 | restore the rest matrix |
  | 4 | Rx(0.0005), plus 2 effects |
  | 5 and 7 | Rx(0.0015) |
  | 6 | Rx(0.0025), plus 2 effects |

  At n = 47 the truck restores the rest matrix, sets y = rest_y − 1.8, clears `+0x2DC`/`+0x2E0` and goes to state 1.
- **State 1, fall beats** (counter `+0x28`):
  - **Rotation:** every beat applies Rx(0) and then Rz(−0.003) to the matrix.
  - **Velocity:** −0.0333 on x and y in f<10, 15..29, 42..51 and 65..89. It is (−0.1333, −0.6667) in 10..14,
    30..41, 52..64 and 90..118. The z velocity is always 0.
  - **Sounds:** `0x454` at f=8 and `0x455` at f=110 (`001FBD50`, radius 300).
  - **Effects:** `0x80000049` at f = 8, 28, 40, 50, 64, 88 and 110 (2+4+2+2+2+4+4).
  - **Rumbles:** `001B1E20(2,0)` at f=14 and at f=87, each only while the arm condition still holds.
  - **End:** at f≥119, `D_00810792 = 0xFF`, the truck goes to state 2 and the velocity becomes 0.
  - **Carry test:** player `+0xB0` x/z is tested against the hull AABB (read before this tick's `001A2370`). Inside
    it, the z velocity is added to player `+0xA8` and spad `0x700031F0 = 1`.
  - **Update:** then pos += velocity, f++, `+0x2E0 = 0.5` when (f&3)=0, `+0x2DC = 0.2` when (f&15)=4, and
    translation = pos + (`+0x2E0`, 0, `+0x2DC`).
- **State 2:** `001B1B70` and the draw only.
- **Any other state (3 included):** `001AFC10`.

**Totals for a full set piece:** 32 effects (12 in the shake, 20 in the fall), 2 sounds, and up to 3 rumbles.

## Arithmetic

- EE `add.s`/`sub.s` use the single-guard-bit model (`em_pose_math.h`, the same one the fan uses). Plain truncation
  misses the PCSX2 capture by one ulp on the arm tick.
- EE `mul.s` truncates, and `div.s` rounds to nearest.
- The SDK rotations `00102B08`/`00102A60` use VU0 truncation and the `001029E8` polynomial, with
  `vsqrt(|1−s²|)`.

## Verification

- **`python3 tools/test_truck_original_reference.py`** runs the following checks:
  - The `div`/`mult`/`mfhi` oracle extension, first validated on the byte-matched `001F8880`.
  - 204 rotation cases.
  - 914 trigger cases, covering every state, story value, phase and band edge.
  - Init and state cases.
  - A 175-tick standing timeline, a leave-after-arm timeline, three never-arm worlds and 480 fuzz ticks.
  - A check that the original writes no byte outside the modelled fields.
- **PCSX2 capture (original game, save state 04):**
  - Run `--capture truck` with the decomp `.venv` python. It teleports the player onto the truck at
    (375, 200, 391) and breaks at `0x823FF0`, `0x8251E0`, the loop top and every effect/sound/rumble call site
    in the truck.
  - `--compare-capture` replays the recorded inputs through one continuous native state. All 230 frames are
    bit-identical, covering the matrix, rest matrix, position, counters, jitter, `D_00810792`, player `+0xA8`,
    `0x700031F0` and the velocity scratch. The 36 worker calls also match in order, including 32 effect
    positions.
  - In that run the player was airborne at f=14, so the original skipped that rumble, and the native module
    also skipped it.
  - For the trigger (`--capture trigger`, band point (325, 235, 420)), all 366 frames match: the script starts on
    frame 0, is polled 364 times, sets `D_00810792 = 1` on frame 364, and the node is freed on frame 365.
  - Captures are stored in `../Extermination/build/s87/truck/` (gitignored).

## Boundaries (workers; a missing worker faults with -1)

| Worker | Original call |
|---|---|
| `model_bind` | `001B0FD0` |
| `placement_matrix` | `001C6380` TRS and the bone slots |
| `pose` | `102958` into bone-0 `+0x90` |
| `hull` | `001A2370`, re-transform the hull and rebuild its AABB (em_actor_collision) |
| `hull_bounds` | the AABB header in table `*0x70003250[(+0xE>>8)&0xFF]` |
| `publish` | `001B1B70` |
| `draw` | `+0x4C` |
| `rumble` | `001B1E20` |
| `effect` | `001EFD20` |
| `sound` | `001FBD50` |
| `free_owner` | `001AFC10` |
| trigger `script_start` / `script_tick` | `001BA1A0` / `001BA1F0` running `0x8292C0` (the AREA11 script host, census L19) |

In the trigger capture, the camera script also moves the player to (327.4, 184.8, 396.7). That belongs to the
script host.

## Binding (live since census L23)

Both nodes are bound in `em_area11_bindings.c` (`tick_truck`) to
`em_area11_boxes_truck_tick` (#24) and `em_area11_boxes_trigger_tick` (#25),
in both walk variants, in the pool walk's node order (#24, then #25). The
truck shares the crates' and drums' world-model services
(`em_area11_boxes.c`): the one owner-services context, the exported model
bank (`assets/scene_snow/world_models.emwm`, model 9), the 001AF710 bone-slot
stack (the truck holds one slot, +0x09 = 1, as route 08's header) and the draw
list.

Record and state. The truck's pool record keeps +0x04, +0xB0, +0xC0 and the
+0x1F0 block (the rest matrix at +0x1F0, +0x2DC / +0x2E0 / +0x2E4 / +0x2E8 /
+0x2EC); the fall counter +0x28, +0xD0 and the velocity scratch 0x700038A0
live in its slot. The trigger keeps +0x04 and +0x0B in its record.

`EmTruckWorld` over canonical storage:

| Pointer | Storage |
|---|---|
| `story` | `em_scene_progress_at(s, 0x00810792, 1)` |
| `player_phase`, `player_0a` | the live player record's +0x05 and +0x0A (`player_states_actor_mut`) |
| `ground_kind` | +0x0D of the pool record the player's +0x214 names (`EmPlayerLiveActor.link_owner`), NULL when none |
| `player_a0` | `g.pos` (D_00810350; the carry adds to its z lane) |
| `player_b0` | the pose's hip (`player_pose_hip`), read only while the truck falls (the carry test 00825014) |
| `carry` | 0x700031F0 in the boxes module (no live reader: its reader 0018B9C0 is unbound, census L13) |

Workers:

| Worker | Binding |
|---|---|
| `model_bind` | `em_owner_services_001B0FD0` over the record's view |
| `placement_matrix` | `em_owner_services_001C6380`, then the view's +0xD0 |
| `pose` | copy_qw4 into bone slot 0's +0x90 |
| `hull` | `em_collision_world_retransform_001A2370` (cell uid 14) |
| `hull_bounds` | `em_actor_collision_owner_hull_bounds` over the collision world |
| `publish` | `em_collision_world_publish_001B1B70` (class 4: the player's floor service finds the truck's cell and stands on the record) |
| `draw` | +0x4C 001CAA00: `assets/scene_snow/props/area_truck.emdl` at bone slot 0's matrix through the actor draw chain (the object kernel stays with RENDER, as for the boxes) |
| `rumble` | `em_pad_actuator_001B1E20(effect, 0)`: 001B1E20 over the D_0024D6F0 records (`assets/pad_rumble.emrg`, `tools/export_pad_tables.py`), 001B61C0 on the pad block D_00810E40, the libpad write 00111018 at the platform boundary (`em_gamepad_rumble`); main-loop step I (001B5B70) counts the duration down and 001B6250 stops it |
| `effect` | 001EFD20 over the live effect binder (em_effects_live, census L26, EFFECT_MANAGER.md section 8); `em_area11_boxes_effect_spawns` counts the calls; route 08's eight puffs equal the snapshot (LEVEL_SMOKE.md check_effects) |
| `sound` | `em_sfx_play_at(id, +0xB0, radius)` (its result is not read); 0x454 / 0x455 are not in the exported AREA11 registry (WP-14), so they are silent |
| `free_owner` | `em_actor_pool_free_001AFC10` |
| trigger `script_start` / `script_tick` | `em_area11_script_host_start` / `_tick`: 0x8292C0 on the AREA11 script host (AREA_SCRIPT.md section 6) |

**Live evidence** (the level smoke, LEVEL_SMOKE.md):
- `truck_preview` (route 07): from the trigger's script frame (f164) through
  the release (f527) and 25 rows after, row for row: the spad bytes, the
  camera byte, the letterbox, the message block, D_00810792, the player
  record (+5, +1F0, +1F1, clip, ground), the placement (f167), the heading
  (f168), the camera eye/target of every shot, and the re-grounded Y after
  the release; the trigger frees itself.
- `truck_crossing` (route 08): from the arm (f43, the player standing on the
  record 0x7A9FB0) through the rest (f209) and 10 rows, the truck record's
  +0x00..+0x0F, +0xB0 and +0x2DC..+0x2EF and D_00810792 row for row; 32
  effect spawns; the step-I countdown stops the rumble (the pad block's
  +0x16 / +0x28 are 0 again, as in route 08's end snapshot).
