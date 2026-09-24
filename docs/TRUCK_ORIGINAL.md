# AREA11 truck set piece (WP-12, H16)

Module: `src/game/em_truck_original.{h,c}`. Oracle and PCSX2 replay:
`tools/test_truck_original_reference.py`. Unit test: `tests/truck_original_test.c`.

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
| `hull` | `001A2370`, re-transform the hull and rebuild its AABB (not translated) |
| `hull_bounds` | the AABB header in table `*0x70003250[(+0xE>>8)&0xFF]` |
| `publish` | `001B1B70` |
| `draw` | `+0x4C` |
| `rumble` | `001B1E20` |
| `effect` | `001EFD20` |
| `sound` | `001FBD50` |
| `free_owner` | `001AFC10` |
| trigger `script_start` / `script_tick` | `001BA1A0` / `001BA1F0` running `0x8292C0` (script host, WP-10) |

In the trigger capture, the camera script also moves the player to (327.4, 184.8, 396.7). That belongs to the
script host.

## Binding (for the coordinator)

- Bind node #24 to `em_truck_original_tick(owner, world, hooks)` and node #25 to
  `em_truck_trigger_tick(owner, world, hooks)`. Order within a frame: #24, then #25.
- `EmTruckWorld` holds pointers into canonical storage:
  - `D_00810792`: the canonical progress byte, `em_scene_progress_at(s, 0x00810792u, 1)` (migrated in HK;
    no other port code reads or writes it)
  - player `+5` and `+0x0A`
  - `&ground[+0x0D]`, or NULL when player `+0x214` is 0
  - player `+0xA0` (the z lane is written)
  - player `+0xB0`
  - spad `0x700031F0`

  The module keeps no shadow copies.
- The riding surface comes from the collision hull that `001A2370` re-transforms. It does not come from the carry,
  whose z velocity is always 0. The legacy moving-surface AABB carry in `em_collision` does not match this code.
