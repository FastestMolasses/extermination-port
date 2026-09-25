# Script host workers (em_area_script callees) and the AREA11 script images

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "script-host-workers" (build lane `b6-script-host-workers`). This
document covers the callees that the AREA11 script host
(`em_area_script`, AREA_SCRIPT.md section 5) listed as untranslated, plus the
export and loader of the AREA11 overlay scripts that the host runs. It says
what each original does, how the coordinator binds it, what the evidence is,
and what remains open.

- Code: `src/game/em_script_host_workers.c/.h`.
- Exporter: `tools/export_area11_scripts.py`.
- Oracle: `tools/test_script_host_workers_reference.py`.
- Sanitizer test: `tests/script_host_workers_test.c`.

**Status (2026-09-24):** the loader is live: the AREA11 script host
(`em_area11_script_host`, AREA_SCRIPT.md section 6.1) loads `scripts.emsc`
and `director_quads.emsc` fresh for each visit and runs the truck preview
0x8292C0 from it (census L19 / L23). `em_script_host_001B6250` is live as the
pad actuator's stop (`em_pad_actuator`: 001B1E20 / 001B5B70 over D_00810E40,
the truck's rumbles); `em_script_host_approach` is bound in the player
modules. The other routines (00182BF0, 001B1240, 001B1380, 001B12B0 as a
script worker, 001B0C00, 001B0460) are reached only by the director's and
Roger's scripts, which are not bound (L21 waits on L22). Section 3 gives
their binding.

## 1. What the originals do

Every routine works on raw bit patterns, using the original offsets and the
original order. The C cites the original address of every branch and store.
Float operations and compares use `em_ee_float.h` (EE_FLOAT_MODEL.md).

| Original | Source read | What it does |
|---|---|---|
| 00182BF0 (op16 predicate) | NEARMISS C, checked against its listing | Returns 1 ("not now") or 0 ("the scripted frame may start"). The checks run in this order. (1) If D_008106BC is set: return 1 while D_0081083C is set, otherwise clear D_008106BC. (2) Return 1 when +0x220 <= 0 (`c.le.s`, so -0 and denormals count as 0). (3) Return 1 when +0xF == 0x63. (4) If D_0081083C is set: set D_008106BC = 1 and return 1. (5) Return 1 when D_008106F1 is set. (6) Return 1 when 0021BB00(p) is nonzero. (7) Return 1 when +0x1F0 is 0x3C, 0x3D or 0xB..0xD. (8) Finally, if +0x22C or +0x224 is not EE-zero, clear both words and set +0 = 1. Then return 0. |
| 001B1240 (bearing) | readable C, checked against its listing | wrap(atan2f(x - o.x, z - o.z)). It reads only o[0] and o[2]. atan2f is 0011E620 (y = the X difference, x = the Z difference). |
| 001B1380 (side test) | asm file | Takes d = from - to (X and Z, EE sub) and computes r = wrap(atan2f(d.x, d.z) - yaw). It returns 0 when r < 0 (`c.lt.s`), otherwise 1, so r = 0 and r = -0 both give 1. |
| 001B12B0 (turn toward) | asm-word file | d = wrap(target - current). If d == 0 (EE compare, so denormals count as 0), it returns wrap(current). If d <= 0, the likely slot negates d: it returns target when -d <= step, otherwise wrap(current - step). If d > 0, it returns target when d <= step, otherwise wrap(current + step). Returning target skips the wrap. |
| 001B6250 (pad actuator stop) | readable C, checked against its listing | On the pad block D_00810E40: nothing happens unless ready (+0x12) and active (+0x16) are both set. When they are, it clears +0x16, the halfword +0x28, +0x18 and +0x19, in that order. It then calls 00111018(word +4, word +8, &block[0x18]). |
| 001B0C00 (skip-landing fade) | byte-matched C | Calls 001AEDE0(a0, 0), then 001FAD70(channel, a0, 1) for channels 0, 1 and 2. |
| 001B0460 (camera re-seat) | C with a match note, read from its listing | The room's camera record is D_0024D650[D_00810700][D_00810701] + D_00810702 * 0x30 (ELF data). The routine calls 001B0250, then 001B0B50. It clears E8 (halfword) and E7, and sets D_008106CD = byte(D_008106C8 >> 16). It copies the record's +0x18 into D_00810244 and D_008101EC, and clears E1, E2 and E3. From the record's flag byte it sets E5 = bit 7 and E6 = the low 7 bits. If D_00275BE0 == 1, it clears both E6 and that byte. **E5 == 1:** with a0 != 0 and D_008104E0 (player +0x230) equal to 0x10 or 0x12, it sets E6 = 9 or 0xB and E1 = 0. Otherwise it sets cam+0x10 = D_0024A8D0[(flags >> 8, arithmetic) * 3] with w = 1.0, cam+0x20 = player +0xA0 with y + 15.0, then working target = cam+0x20 and working eye = cam+0x10. **E6 == 0xA:** it sets D_008106BE = 2 and cam+0x10 = player +0xA0 with y + 3.0. It then fills the 0x70003400 matrix with 001029C0 and rotates it with 00102C58 by player +0xC0, and sets 0x70003600 = (0, 0, 5, 0). It computes cam+0x20 = 001026A0(matrix, that vector) and adds cam+0x10 with 001028B8. Target and eye are then copied as in the E5 branch. **Otherwise:** it calls 001B0080(camera, 2.0). Then record +0x14 == 5 gives E6 = 0xD, and record +0x14 == 4 in area 0x13 gives E6 = 0xF. Every path ends with 0018C0D0(camera, 1) and then 001DD980(&D_008105D0, &D_008105E0). The four-word copy 00102948 is written inline. |

Reused translations (called, not re-translated): `em_player_0021BB00` and
`em_player_001B1470` (em_player_stage_workers.c), and
`em_sdk_math_original_0011E620` (em_sdk_math_original.c).

Where the decomp C and the listing differ in form: the NEARMISS 00182BF0 C has
the same logic but a different branch layout (the diff is a compiler artifact).
001B0460's C passes two arguments to 001B0250. That is register residue:
001B0250 reads no arguments, so the worker takes none.

### Translated domain of 001B1470

The original loops, subtracting or adding 2π, until its argument lies in
(-π, π]. At |x| = 4096 that takes 652 iterations. For huge or exponent-255
patterns, which the EE treats as ±MAX, it spins practically forever. The
native routines pass the wrap only arguments with |x| < 4096.0 (bits
`0x45800000`). Any other argument faults at 0x001B1470 without writing. Every
script and owner argument is an angle or an angle difference far below that
bound. The oracle checks the refusal. Whenever the original's own wrap
arguments cross the bound, the native side faults at 0x001B1470. Every other
case is bit-identical.

### The atan2f error path

0011E620 enters its error path when both operands are EE zero. That means ±0
or denormal differences, for example a bearing to a point at the object's own
X/Z. The path starts with 00128350. With the SDK module's error-path workers
unbound (SDK_MATH_ORIGINAL.md section 7), 001B1240/001B1380 fault at
0x00128350. The oracle checks that the original reaches 00128350 in exactly
those cases. Whether the route reaches this path has not been measured.

## 2. The AREA11 script images

`tools/export_area11_scripts.py` copies two ranges of the user's
`extract/OVERLAY/AREA11.BIN`. The file is MWo3, loaded whole at its header
address 0x823500, so no command is rewritten. The two ranges go into ignored
EMSC files. The format is the one `em_script_image_load` reads: "EMSC", u32 1,
base, entry, length, then the bytes.

| File (`assets/scene_snow/area11_scripts/`) | Range | Contents |
|---|---|---|
| `scripts.emsc` | 0x8292C0..0x82A3C0 (68 records) | truck camera preview 0x8292C0 (8 records), director beat 0 0x8294C0 (22), beat 1 0x829A40 (10), beat 2 0x829CC0 (7), beat 3 0x829E80 (21). The five chains tile the range. |
| `director_quads.emsc` | 0x82ABE0..0x82ACA0 | the director's three quads 0x82ABE0 / 0x82AC20 / 0x82AC60, four XYZW vertices each |

The elevator's powered (0x82A750) and refusal (0x82A990) scripts are in
`elevator.emsc` (`tools/export_elevator.py`). Roger's four scripts are in
`roger/programs.emsc` (`tools/export_roger_resources.py`).

`em_area11_scripts_load(out, scripts, quads, elevator, roger)` loads the new
pair. It also loads the elevator and Roger images when their paths are
given. Each image must have exactly its range as base and length. Every owner
entry that falls in a loaded image must reach a stop record inside it,
following jump records, within 64 records. The 11 entries are the five above,
0x82A750, 0x82A990, 0x8283D0, 0x828990, 0x828810 and 0x828A10. Anything else
is refused (-1, nothing loaded). `em_area11_scripts_image(entry)` returns the
image that holds an entry. `em_area11_scripts_director_quads` fills the
`EmDirectorOriginalWorld.quad` pointers.

The export and the loaded images were checked against the captured RAM in all
18 captures (the three startup-reference captures and the 15 route
snapshots):
- The script range is byte-identical to every capture taken before those
  scripts ran (startup-reference and route beats 00..06).
- The quads are identical everywhere.
- After the scripts ran, the only differences are fields the game writes in
  place. Each one is checked by record op and word offset:
  - scripts: the op00 tick counter at +0x10 (001B8FC0), 172 bytes over the
    later snapshots;
  - elevator: the op00/op01 heights at +0x34 (00827B10 patches them);
  - Roger: op15 +0x10 (001B6FA0 saves the owner yaw) and op0A +0x30..+0x3F
    (001B9A00 sub 5 stores the player bone). Elevator and Roger together
    account for 62 bytes.

A live binder must therefore load fresh images for each visit and must not
reuse images that a previous run mutated.

## 3. Binding (for the coordinator)

One `EmScriptHostWorkers` per game. Its world pointers go into the canonical
storage. Its callees go to the services named below.

### EmAreaScriptWorkers slots

Each adapter has exactly the slot's signature. `EmAreaScriptWorkers` shares
one `ctx` across all its workers, so bind through one-line adapters that fetch
this module's `EmScriptHostWorkers` from the binder's context. Do not store it
as the shared ctx.

| Slot | Adapter |
|---|---|
| `w_00182BF0` | `em_script_host_w_00182BF0` (actor must be 0x8102B0) |
| `w_001B1240` | `em_script_host_w_001B1240` |
| `w_001B12B0` | `em_script_host_w_001B12B0` |
| `w_001B1380` | `em_script_host_w_001B1380` |
| `w_001B0C00` | `em_script_host_w_001B0C00` (op18 skip landing, a0 = 8) |
| `w_001B6250` | `em_script_host_w_001B6250` (address must be 0x810E40) |
| `w_001B0460` | `em_script_host_w_001B0460` (op0D sub1, a0 = 1) |

### Other consumers of the same originals

- `EmOwnerServicesWorkers.w_001B6250` (ctx only): `em_script_host_owner_001B6250`, through the same kind of adapter.
- The player modules' `approach` slots, i.e. 001B12B0 on raw bits: `em_player_fall.h`, `em_player_closure_0e_18.h`, `em_player_closure_10_12_19.h`, `em_player_weapon_states_b.h`. Bind them to `em_script_host_approach`, which ignores its ctx and so binds directly. `em_player_slide_approach` still uses the old float model (EE_FLOAT_MODEL.md 5c). This translation is the EE-model replacement.
- 001B0460(1) from the panel's 00157360 state 4, the door room-move (001B07C0), and 0016D130 / 0016DE40. `em_script_host_001B0460` replaces the S12a stand-in when those owners are bound.

### World → canonical storage

| Field | Storage |
|---|---|
| `player`, `player_address` | the live player record (`EmPlayerLiveActor`, the one em_player_stage_workers uses), 0x008102B0 |
| `d8106BC`, `d8106F1` | `em_scene_req_at(state, 0x008106BC / 0x008106F1)` |
| `d81083C` | D_0081083C. **It is not in a migrated EmProgress range yet** (`em_scene_progress_canonical` is 0 for it), so it must be migrated or given a single owner before binding. A NULL faults at 0x00182C10. The stage workers read the same byte (`EmPlayerStageGlobals.d81083C`). Unify or mirror the two. |
| `d810E40`, `pad_address` | the pad block D_00810E40 (0x2A bytes, original layout), 0x00810E40. `EmOwnerServicesWorld.d00810E56/d00810E68` point at bytes +0x16 and +0x28 of this same block. |
| `sdk_tables/_world/_workers` | the shared SDK math context (SDK_MATH_ORIGINAL.md section 7) |
| `elf`, `elf_size` | the user's boot ELF image (read-only tables D_0024D650 and D_0024A8D0) |
| `d810700/701/702` | `EmSceneState.d810700/d810701/d810702` |
| `d8101E1..E3, E5, E6, E7, d8101E8, d8101EC, cam_10, cam_20` | the camera object D_008101E0: one storage, shared with `EmAreaScriptWorld.d8101E1..` and `cam_10/cam_20` |
| `d810244` | camera +0x64 (D_00810244) |
| `d8106C8`, `d8106CD`, `d8106BE` | `em_scene_req_at(state, 0x008106C8 / 0x008106CD / 0x008106BE)`. The 001B0250 worker must write this same D_008106C8, because 001B0460 reads it right after the call. |
| `d275BE0` | the pause/mode byte D_00275BE0 (also read by 001B61C0) |
| `d8104E0` | player +0x230 (a word in the player record) |
| `d810350`, `d810370` | player +0xA0 / +0xC0 (vec4) |
| `d8105D0`, `d8105E0` | the working eye/target, the same storage as `EmAreaScriptWorld.d8105D0/E0` |
| `spad3400`, `spad3600` | scratchpad 0x70003400 (16 floats) and 0x70003600 (vec4; also `EmAreaScriptWorld.spad3600`) |

### Callees → services

| Callee | Service |
|---|---|
| `w_00111018` | libpad actuator write (the pad driver) |
| `w_001AEDE0` | `em_transition_fade_out` (as `EmAreaScriptWorkers.w_001AEDE0`) |
| `w_001FAD70` | the stream channel fade (`UM_001FAD70` in em_scene_bindings.c) |
| `w_001B0250`, `w_001B0B50` | the room restore (writes D_008106C8) and `em_player_closure1019_001B0B50` (D_008106BE) |
| `w_001B0080`, `w_0018C0D0` | camera distance setup and camera commit (em_camera; 0018C0D0 is `UM_0018C0D0_STATE4` today) |
| `w_001DD980` | world-camera publish (as `EmAreaScriptWorkers.w_001DD980`) |
| `w_001029C0`, `w_00102C58`, `w_001026A0`, `w_001028B8` | the VU0 leaves, e.g. `em_owner_services_identity_001029C0` / `em_owner_services_euler_00102C58`. The workers must allow the aliasing 00102C58(dst == src) and 001028B8(out == a). |

### Loader

At AREA11 entry, call `em_area11_scripts_load(&s, EM_AREA11_SCRIPTS_PATH,
EM_AREA11_QUADS_PATH, EM_AREA11_ELEVATOR_PATH, EM_AREA11_ROGER_PATH)`. For
each owner, call `em_area_script_init(host, em_area11_scripts_image(&s,
entry), ...)`: the trigger 008251E0 uses 0x8292C0, the director 008253F0 uses
0x8294C0 / 0x829A40 / 0x829CC0 / 0x829E80, the elevator 00827B10 uses
0x82A750 / 0x82A990, and Roger uses his four entries. Pass
`em_area11_scripts_director_quads(&s, world.quad)` to `em_director_original`.
The images are mutated in place (section 2), so reload them for every visit.

### Makefile (the lead edits; not edited here)

```make
.PHONY: test-script-host-workers-reference
test-script-host-workers-reference:
	python3 tools/test_script_host_workers_reference.py

.PHONY: test-script-host-workers
test-script-host-workers:
	@mkdir -p build/script_host_workers
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -Isrc tests/script_host_workers_test.c src/game/em_script_host_workers.c src/game/em_player_stage_workers.c src/game/em_sdk_math_original.c src/game/em_script.c -o build/script_host_workers/script_host_workers_test
	build/script_host_workers/script_host_workers_test build/script_host_workers
```

When the module is bound, add `src/game/em_script_host_workers.c` and
`src/game/em_player_stage_workers.c` to `COMMON` if the latter is not there
yet. A private build of COMMON plus both files links with zero warnings.

## 4. Verification

`python3 tools/test_script_host_workers_reference.py` runs the six routines
above and 001B0460 as original instructions from the user's pinned ELF. The
callees 0021BB00, 0011E620 (with its kernels) and 001B1470 run as original
code; 001B1470 is spied, recording its arguments, and still runs its own
body. Every COP1 operation goes through `tools/ee_float_model.py`. Hooked and
recorded: 00111018, 001AEDE0 and 001FAD70, the atan2f error path (00128350),
and 001B0460's nine callees. The hooks have scripted effects that are
identical on both sides: 001B0250 writes D_008106C8, 001B0B50 writes
D_008106BE, 001B0080 writes E6, 0018C0D0 writes the eye/target, and the VU0
leaves write their outputs.

- **Unit cases.** Each case compares the result, all 0x320 record bytes, the
  flag bytes, the pad block, every worker call (arguments and the vector or
  matrix contents at call time), and all 13 storage regions of 001B0460. It
  also asserts that the original writes nothing outside the compared storage.
  - The +0x1F0 sweep covers all 256 values.
  - 001B0460 runs over the real D_0024D650 records and over test-generated
    records planted for areas 5 and 0x13 in a copy of the ELF that both sides
    read. The planted records cover mode 1, sub 0xA, kinds 4 and 5, and
    D_0024A8D0 rows.
  - Branch coverage: all 30 conditional branches of the seven routines are
    asserted to take both outcomes.
- **Captured RAM.** All 18 captures are used. For each one the checks are:
  - 00182BF0 on the captured player record and flag bytes (every capture
    gives 0);
  - 001B6250 on the captured pad block;
  - 001B1240/001B1380 between the player's +A0/+B0/+C4 and each owner's
    +B0/+C4 (Roger, director, trigger, elevator, panel);
  - 001B12B0 from those bearings, using the op15 rates found in Roger's
    scripts and the op01 rate 0x3D8EFA35;
  - 001B0460 on the captured area/room/camera state.
- **The script host with these translations bound.** The test imports
  `tools/test_area_script_reference.py` without editing it. It answers that
  test's `w_001B1240/w_001B12B0/w_001B1380` workers with this module instead
  of scratch original executions.
  - Lockstep: 6 scenarios (Roger 0x828990, 0x828810, 0x828A10, and director
    beat 3 0x829E80 with skips at 4 and 12) against the original handlers.
    That is 706 calls in quick mode.
  - Route capture of beat 10 (Roger 0x828990's op15 turn, concurrent with
    director 0x8294C0): 2,419 frames and 0 differences, with 342 calls
    answered by this module. The comparisons include 2,417 player
    position/yaw rows and 9,656 camera vectors.
- **Export and loader.** See section 2 for the RAM check. Five malformed
  images are refused: wrong base, truncated, no stop record, wrong magic,
  and short quads. All 11 entries resolve.
- **Fail-stop** (30 checks). Every missing pointer or worker faults at its
  address before any write or call. A failing worker stops the routine and
  leaves the earlier stores. The first fault is kept.
- **Results.**
  - Quick mode, about 4.5 s: 1,156 predicate records (342 take the frame);
    001B12B0 1,300, 001B1240 158 and 001B1380 152 argument sets (5 + 2
    error-path, 94 + 14 domain refusals); 27 pad blocks; 8 001B0C00
    arguments; 120 001B0460 cases.
  - `EM_TEST_FULL=1`, about 20 s: 20,256 predicate records; 30,400 / 6,008 /
    6,002 math argument sets (64 + 2 error-path, 1,697 + 563 refusals);
    180 pad blocks; 3,000 001B0460 cases.
- **Mutations.** Each of these fails the oracle:
  - 001B12B0 `c.le` → `c.lt`;
  - a host-float subtraction in 001B1240;
  - an integer zero test in place of the EE compare in 001B12B0;
  - swapped atan2 operands;
  - `c.lt` → `c.le` in 001B1380;
  - a dropped D_008106BC store;
  - `mode - 0xB < 2`;
  - a dropped +0x19 clear;
  - swapped E6 values 9/0xB;
  - +15 → +3;
  - kind 5 → 4;
  - D_008106C8 >> 8;
  - 5.0 → 4.0 in the 0x70003600 vector;
  - swapped 001028B8 operands;
  - `D_00275BE0 != 0`.

  Two mutations survive, and both are equivalent to the original. Writing
  `current + (-step)` for `current - step` gives the same result in the EE
  model. The second E1 = 0 in 001B0460's armed branch repeats the earlier
  clear at 001B0510.
- `tests/script_host_workers_test.c` (ASan/UBSan) runs 61 checks: the loader
  over the user's assets and malformed copies, and the adapters' fail-stop.

## 5. Limits

- 00182BF0 is verified on synthetic records and on the 18 captured player
  records. The route replay does not reach it, because the trace lacks
  +0x220, +0xF, +0x22C/+0x224 and the three flag bytes. The area script
  test derives op16's result from the script pointer instead.
- 001B0460 is verified with its nine callees hooked. The VU0 leaves, 001B0080
  and 0018C0D0 are the binder's modules, and their own oracles cover them.
  No route beat is known to run op0D sub1. The panel state-4 and door
  room-move callers are not replayed here.
- The atan2f error path faults at 0x00128350 until the SDK module's
  error-path workers are bound (section 1).
- The 001B1470 domain (|x| < 4096) is a host validation bound, not an
  original rule. Beyond it the native side faults, where the original would
  loop for a long time.
- The export's RAM check covers the captured visits only. The first-visit
  images are the ones the owners start from.
