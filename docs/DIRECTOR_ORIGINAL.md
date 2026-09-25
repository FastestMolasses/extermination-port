# AREA11 director manager 0x8253F0 (`em_director_original`)

Module: `src/game/em_director_original.{h,c}`. Oracle and route replay:
`tools/test_director_original_reference.py`. Sanitizer test:
`tests/director_original_test.c`.

**Status: built and verified; binding prepared, not wired (2026-09-25).** The
adapter `tick_director_original` (em_area11_bindings.c) binds this module to
pool node #21 over em_area11_script_host; it runs only in the level smoke's
director verification run (`make test-level-smoke-director`), which
reproduces route 10 f1090..f1162 row for row and then stops at the first
voiced line (WP-8b). The live node keeps the legacy `em_director`
(`kCineBeats`, FIRST_LEVEL_AUDIT H10) until the voice lanes are live. Section
6 describes the binding.

## 1. The owner

The owner is AREA11 placement record 12, a class-9 node, with callback
`0x8253F0`. In the route captures its node is at `0x7A93F0`.

The splat labels for this overlay are `0x40` below the runtime addresses that
`jal` encodes. Splat also splits each beat body in two:

| Runtime function | Splat labels |
|---|---|
| owner `0x8253F0` | `func_overlay_AREA11_008253B0` |
| beat 0 `0x825500` | `_008254C0` and `_00825500` |
| beat 1 `0x825600` | `_008255C0` and `_00825600` |
| beat 2 `0x8256D0` | `_00825690` and `_008256D0` |

Every address in this document is a runtime address.

**Owner `0x8253F0` switches on node `+4`:**

| `+4` | Behaviour |
|---|---|
| 0 | `001BA1C0(self, 0x3B)` at 0x825438, which returns `D_00810758[0x3B] == 0xFF`, i.e. `D_00810793 == 0xFF`. If it is set, `+4 = 3` (0x825450). Otherwise `+0 = 1` and `+4 = 1` (0x825458, 0x825460). |
| 1 | It reads `D_00810813` once (0x825468) and dispatches on it: 0 or 1 → beat 0; 0x10 or 0x11 → beat 1; 0x20 → beat 2. Any other value does nothing. |
| 2, 3 | `001AFC10(self)` (0x8254E4). The node frees itself. |
| other | Returns. |

**Each beat body switches on node `+5`.**

- **`+5 = 0`:**
  1. Read Y from `D_00810354`.
  2. Apply the beat's height gate.
  3. Call `001B1EA0(0, &D_00810350, quad, 4)`.
  4. If that returns nonzero, call `001BA1A0(self + 0x1F0, script)` and set
     `+5 = 1`.
- **`+5 = 1`:** call `001BA1F0(self)`. Note that its argument is the node,
  not the block. On **any** nonzero result (1 finished, or 3 aborted by the
  skip path), run the beat's completion, then set `+5 = 0`.
- **Any other `+5`:** nothing.

| Beat | Body | Height gate | Quad | Script (start site) | Completion |
|---|---|---|---|---|---|
| 0 | 0x825500 | `Y < 260.0` rejects (`c.lt.s`, 0x825548); `!(Y <= 280.0)` rejects (`c.le.s`, 0x825564) | 0x82ABE0 | 0x8294C0 (0x8255A0) | `D_00810813 = 0x10` (0x8255D0), then `001C4760(1, 1)` (0x8255D4), then `+5 = 0` (0x8255DC) |
| 1 | 0x825600 | `Y < 275.0` rejects (0x82564C) | 0x82AC20 | 0x829A40 (0x825688) | `D_00810813 = 0x20` (0x8256B4) |
| 2 | 0x8256D0 | `Y < 285.0` rejects (0x82571C) | 0x82AC60 | 0x829CC0 (0x825758) | `D_00810813 = 0xFF` (0x825784) |

- **NaN heights.** Because of the comparison forms, a NaN Y passes the
  beat-1 and beat-2 gates and fails the beat-0 gate.
- **Beat 0's gate** is in the body, not at its entry. FIRST_LEVEL_ROUTE's
  "at runtime 0x825500" names the body; the compares are at 0x825548 and
  0x825564.
- **Quad 0x82AC20 is not a rectangle.** Its X/Z vertices are
  (452,282), (500,292), (500,280), (462,278).
- **A skipped beat still completes.** Director beats use op07 sub8 and sub3,
  so they can be skipped (AREA_SCRIPT.md section 2). A skip therefore still
  advances `D_00810813`, and beat 0 still calls `001C4760`.

## 2. Callees translated here

| Original | Source read | Translation |
|---|---|---|
| `001BA1C0(a0, a1)` | byte-matched C: `D_00810758[a1] == 0xFF` | inline in state 0 |
| `001C4760(a0, a1)` | byte-matched C (mwcc 2.3.3) | `em_director_original_001C4760`: `D_00810CC3[a0] += a1`. When `a0 >= 0x20` it also stores `D_008106B0 = 3`, then `D_008106B1 = a0`. The director calls it as (1, 1), so only `D_00810CC4` changes. |
| `001B1EA0` mode 0 | all-`.word` in the decomp; read from its instructions | `em_director_original_001B1EA0` (details below) |
| `0011E620` atan2f wrapper | decomp C and its instructions | `em_director_original_0011E620` |
| `0011C4C8` atan2f kernel | instructions (fdlibm; constants folded: pi 0x40490FDA, pi/2 0x3FC90FDB, the k > 60 value 0x3FC90FDC) | `em_director_original_0011C4C8` |
| `0011DBB8` atanf | instructions (see finding 3) | `em_director_original_0011DBB8` |

**`001B1EA0` mode 0:**
- A count below 3 (signed) returns 0. This is checked before the mode.
- For each vertex, the next vertex wraps to 0 after the last. With
  a = cur − p and b = next − p on X/Z:
  - cross = `mula(b.z, a.x); msub(b.x, a.z)`, which is ACC − b.x·a.z;
  - dot = `mula(b.x, a.x); madd(b.z, a.z)`;
  - total += `0011E620(cross, dot)`.
- The result is `total < 0 ? total < −pi : !(total <= pi)`, with
  pi = 0x40490FDB.
- Any mode other than 0, 1 or 2 returns 0. Modes 1 and 2 (the X/Y and Y/Z
  sums) are not translated and return −1.

**`0011E620`:**
- It calls the kernel first.
- When x == 0 and y == 0 (either sign) it returns `00127758(0.0)` = +0,
  because `D_0026C5D0` ≠ −1. The ELF image and the captured RAM both hold 1
  there. The kernel returns ±pi for (+0, −0) and (−0, −0), so the wrapper
  matters at polygon vertices.
- The zero-vector branch's `matherr`/errno side effect (0011DB90, 0011FD78)
  is **not modelled**. It writes only the SDK errno.

**Arithmetic.** EE add/sub use the single guard bit, mul truncates and div
rounds to nearest (`em_pose_math.h`). This is the model the truck, the sine
and the pose captures settled.

**Faults.** Non-finite inputs or intermediates are outside the translated
domain and fault (−1).

## 3. Findings

1. **MSUB.S is ACC − fs·ft.**
   - **Evidence.**
     - mwcc compiles `b[k]*t - a[k]*inverse` in `quat_nlerp` to
       `mula.s b,t; msub.s a,inverse` (Extermination `src/quat_nlerp.c`,
       NEARMISS with 67 of 69 instructions identical).
     - `em_pose_transition`'s `pose_msub` uses the same order.
   - **Tools that use the opposite order.** Two existing tools use
     product − ACC:
     - `test_item_sdk_math_reference.Original` (`fn == 29`);
     - the comment in `em_roger_trigger` (deleted 2026-09-24, census L22:
       Roger's trigger is `em_director_original_001B1EA0_bound`).
   - **Where the order changes a result.** For axis-aligned quads (Roger's
     0x82AB80 and the director's quads 0 and 2) the order does not change
     any result: every term changes sign together. For the irregular quad
     0x82AC20 it does. At a point exactly on a slanted edge the cross product
     is exactly +0 in both orders, so only the other three terms flip:
     - example: (464, 284.5) on the edge (452,282)–(500,292);
     - ACC − product gives outside (0);
     - product − ACC would give inside;
     - the oracle kills that mutant.
   - **Action.** This is outside this lane: the lead decides whether
     the SDK oracle changes (`em_roger_trigger` is gone).
2. **`em_interaction_sdk_atan2` is not the guard-bit model.** It models
   add.s as plain truncation.
   - Against `em_director_original_0011C4C8`, which is bit-identical to the
     original instructions under the guard-bit model, it differs by an ulp
     on 70,569 of 200,000 random argument pairs in ±300.
   - It is not used here. Its callers (the interaction candidates,
     including `em_roger_candidate`) inherit the difference. The lead decides; compare
     AREA_SCRIPT.md section 5 on `em_item_sdk_sine`.
3. **The decomp's `src/func_0011DBB8.c` (NEARMISS) swaps atanhi and
   atanlo.**
   - The instructions (0x11DE10..0x11DE44) subtract `D_0026C5E8[id]` and
     return `D_0026C5D8[id] − (…)`.
   - The C does the reverse, and its extern comments name the two arrays
     the other way round. The ELF at 0x26C5E4 holds atanhi[3].
   - The binary is unaffected, because NEARMISS C is not linked. The C
     should be corrected in the decomp repository.
   - The oracle kills the swapped form.
4. **Frame order on the route.**
   - Rows are sampled at the loop top, so row f+1 is the state after
     frame f.
   - The director runs in the pool walk after the player stage. It
     therefore reads row f+1's position:
     - beat 0 starts in the frame between rows 1088 and 1089;
     - row 1088's y of 249.4 fails the gate and row 1089's 264.9 passes;
     - beats 1 and 2 behave the same way.
   - The director also runs after Roger (#17), so Roger's writes of
     `D_00810813` (1 at beat-10 row 3460, 0x11 at row 3509) are visible to
     it in the same frame.
5. **Evidence for `001C4760`.** `D_00810CC3[1]` (0x810CC4) is 0 in the
   beat-08 snapshot and 1 in the beat-10 snapshot. It stays 1 through beats
   11, 13 and 14. `D_00810CC3[0]` is 1 throughout. That is consistent with
   the opening controller's `001C4760` call at 0x823F84
   (`test_continue_reset_reference`), which runs before these beats.

## 4. Verification

**`python3 tools/test_director_original_reference.py`** runs in about 3 s
quick and about 45 s with `EM_TEST_FULL=1`. Every part runs in every mode.

- **SDK atan2f.** The original 0011DBB8, 0011C4C8 and 0011E620 against the
  translations: every atanf branch boundary, signed zeros, x = 1,
  huge/tiny ratios and random words. They are bit-identical: 726 cases
  quick, 18,366 full.
- **Owner ticks.** The original owner executes with the ELF's 001BA1C0,
  001C4760, 001B1EA0, 0011E620 and kernel. The inputs are:
  - every `+4` (full) with flag 0x3B and `+0`;
  - `D_00810813` (all 256 values in full) × `+5` × poll result 0/1/3 ×
    inside/outside;
  - every gate edge (260/275/280/285 and their float neighbours, NaN,
    ±inf, ±0, ±1e30) for each beat step, inside and outside;
  - integrated points around each quad.

  The test compares:
  - the ordered worker calls and their arguments;
  - the recorded 001B1EA0 arguments;
  - node `+0/+4/+5`, `D_00810813`, `D_00810CC3[0..1]` and
    `D_008106B0/B1`;
  - that the original writes no other byte (0011E620's errno on a zero
    vector excepted);
  - the kernel on every argument pair the original produced.

  Counts: 882 of 1,246 quick; 22,526 full.
- **Polygon.** The original 001B1EA0 against the translation over the three
  quads and Roger's quad:
  - vertices;
  - points on every edge and at 1e-3, 0.05 and 1 off it;
  - edge lines extended beyond the vertices;
  - points inside the bounding box of quad 0x82AC20 but outside the quad;
  - the float neighbours of the bounding boxes;
  - random points;
  - counts −1..3, modes 1, 2, 3, −1 and 0x7FFFFFFF, and non-finite points.

  Counts: 600 of 782 quick; 2,318 full.
- **Fail-stop.** 13 missing or failing workers and storage fault at their
  addresses.
- **Route replay, every mode.** All 12,424 frames of the 15 pad-only route
  traces.
  - **Per frame:** node `+0/+4/+5` and `D_00810813` against the next row;
    every script start (frame, block and entry) against the captured script
    block.
  - **Totals:** 245 gate evaluations and 3,133 polls.
  - **Starts:** 0x8294C0 at beat-10 row 1089, 0x829A40 at beat-11 row 705,
    0x829CC0 at beat-13 row 530.
  - **Completions:** 0x10 at 10/3508, 0x20 at 11/1200, 0xFF at 13/749.
  - **Snapshots:** the `D_00810CC4` evidence in finding 5.
  - **Inputs taken from the capture:** the poll result (the block's active
    byte leaving 1), Roger's same-frame `D_00810813` writes, and the player
    position.
- **Mutations.** Each of these fails the test:
  - msub order;
  - `<=` gate;
  - beat-0 NaN form;
  - no 001C4760;
  - atanhi/atanlo swap;
  - round-to-nearest add;
  - no zero-vector wrapper;
  - no 0x11 dispatch;
  - free result;
  - sub-state 2 treated as 0.

**`tests/director_original_test.c` (ASan/UBSan, 58 checks).** It runs over
the user's AREA11.BIN and SCUS_971.12 and covers:
- the three beats in route order at the captured start positions, including
  a skip (poll result 3) and Roger's interleaved writes;
- the gate edges;
- the irregular quad's bounding-box point;
- every idle state and step;
- 12 fail-stop paths;
- the 001B1EA0 count and mode edges;
- the zero vector;
- 001C4760's `a0 >= 0x20` branch;
- the loaders refusing bad images.

Build command (a Makefile hunk for the lead, section 6):

```
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc -ffp-contract=off \
   tests/director_original_test.c src/game/em_director_original.c -lm -o build/director_original_test
./build/director_original_test ../Extermination/extract/OVERLAY/AREA11.BIN ../Extermination/config/SCUS_971.12
```

## 5. Limits

- `001B1EA0` modes 1 and 2 are not translated. They fault, and the director
  never uses them.
- The atan path covers finite arguments only. The player position is
  refused if non-finite; the original would compute on it.
- The errno write of 0011E620's zero-vector branch is not modelled.
- The route presses no skip, so the skip completion (poll result 3) is
  covered by the oracle only.
- The scripts themselves (0x8294C0 / 0x829A40 / 0x829CC0) belong to
  `em_area_script`. It replays them over the same route beats
  (AREA_SCRIPT.md section 4).

## 6. Binding (for the coordinator)

**Status (census L21, 2026-09-25): binding prepared, blocked on WP-8b.**
- **The adapter** (em_area11_bindings.c `tick_director_original`, selected by
  `em_area11_bindings_select_director_original`, which only the level
  smoke's director run calls): the node's +0 / +4 / +5 are EmActor.status /
  u04[0] / u04[1]; D_00810813, D_00810793, D_00810CC3[] canonical D2 bytes;
  D_008106B0 / B1 the request block; D_00810350 = g.pos; the quads from
  `director_quads.emsc` through `em_area11_script_host_director_quads`;
  001B1EA0's 0011E620 is the one bound SDK atan2f (`EmDirectorOriginalWorld.atan2`,
  new: em_sdk_math_original over the collision world's SDK context, as
  Roger's trigger); 001BA1A0 / 001BA1F0 on em_area11_script_host; 001AFC10
  the pool free. It ticks in both walk variants.
- **Script host additions:** op0D sub 2's `w_0018CBD0` (the port's one seed,
  `camera_script_seed_0018CBD0`, split out of the panel's retarget) and
  `w_0018D7B0` (`em_camera_live_solve`), and the camera's +0x0C
  (`em_camera_live_bytes(0x008101EC)`). A second script owner running inside a
  frame another owner's script opened (Roger's 0x828990 inside the
  director's 0x8294C0) runs under the takeover that owner holds (0015B130's
  admission reads 0x70003B8D, not the owner).
- **Verified:** the director verification run equals route 10 f1090..f1162
  (spad, camera byte, bars, message, power, the camera shots, the player,
  D_008107D8 / D_00810793 / D_00810813, Roger's record and script block;
  LEVEL_SMOKE.md "cage_roof prefix").
- **Blocker (WP-8b):** at f1163 Roger's 0x828990 presents the voiced line
  0x7F (VOICE.DAT cues 143..). A voiced line waits in 001FD790 while
  D_008106F5 is 2 until the voice lane service 001F9CF0 starts the voice,
  and its end waits on the lanes' busy bytes D_00282155 / 156; 001FA5A0
  (`voice_push`) and the stream lanes are not live (STREAM_LANES.md "Still
  missing"), so the message service faults. Bound now, the director would
  stop the level there, where the stand-in completes its beat. Beats 1 and 2
  (lines 0x97 / 0x99, cues 150 / 149) have the same dependency.

History (2026-09-24): not bound. Unblocked by census L22
(2026-09-24): Roger is bound (`em_area11_roger`; ROGER_ACTOR_ORIGINAL.md
section 4) and runs his scripts on the script host; his alternate branch
starts 0x828990 as soon as D_00810793 = 1 with D_00810813 = 0. The
host's 00182BF0, 001B0C00 / 001B6250, 001B81D0 (the player face through
the interaction host), D_00810758 / D_0081078F / D_008107D8 and op0C are
live for Roger's 0x8283D0 (route 14 row for row). The history below is
kept for the binding step.**
The AREA11 script host is live since L19 / L23 (`em_area11_script_host`,
AREA_SCRIPT.md section 6.1), but beat 0 cannot run faithfully without
Roger's owner:
- 0x8294C0 opens the scripted frame (07/8), raises flag 0x3B (06/0: D_00810793
  = 1), runs its camera shots, and then its 06/2 on counter 0x3B **waits
  until D_00810813 != 0** (001BA080 sub 2).
- On the route only Roger's alternate script writes that byte: Roger r8 sees
  D_00810793 = 1 with D_00810813 = 0, runs 0x828990 (its op15 conversation,
  line 0x7F) and D_00810813 becomes 1 at f3460; the director completes at
  f3508 (FIRST_LEVEL_ROUTE.md beat 10, steps 4..6).
- Roger 008237E0 / 00823910 was unbound until census L22. Bound alone, the
  director would hold the player in the scripted frame at 06/2 for ever
  where the legacy stand-in completes its beat: a live path that degrades
  play. So node #21 keeps `em_director.c` (kCineBeats) until L22 binds Roger
  and its line 0x7F; the voiced director lines 0x97 / 0x99 (beats 1 and 2)
  need WP-8b's stream lanes as well.
- Since census L09..L11 (2026-09-24) the level smoke plays routes 10..13's
  climbs and jump live around the director: its `cage_roof`,
  `crevice_prompt` and `east_tower` phases are driven through the stand-in
  (reported NOT-LIVE driven, not compared; LEVEL_SMOKE.md). Binding this
  module (with Roger) makes those three phases live; their capture checks
  then compare the scripts' frames, bars, camera shots, the lines 0x7F /
  0x97 / 0x99 through em_message_live's op0C, and D_00810813's 1 / 0x10 /
  0x11 / 0x20 / 0xFF.
- What the director's scripts need from the host beyond the truck preview's
  workers: 00182BF0 (op16, `em_script_host_w_00182BF0` over the live record
  and the canonical D_0081083C / D_008106BC / D_008106F1), 001B0C00 /
  001B6250 (op18 skip landing), 001B81D0's face attach (001CA700 / 001D06D0
  through the interaction host's face, with D_0081078F and the player's
  +0x2FF), op0D sub 2's 0018CBD0 / 0018D7B0 with cam +0x0C / +0xA0, the
  flag and counter arrays D_00810758 / D_008107D8 (slots 0x3A..0x3C and
  0x3B were canonical; census L22 added D_00810758 / D_008107D8 slots 0..1
  and D_0081078F..D_00810795, em_scene_state.h), and op0C's lines through
  `em_message_live_op0c`.

**Node and stage.**
- Pool node **#21** (record 12, callback `0x8253F0`) gets one behaviour
  adapter, `tick_director_original(actor, node, world)`, at its existing
  pool-walk position.
- The pool walk places it after Roger #17 and before the truck #24 and the
  trigger #25. It runs in whichever `001AFD70` walks tick class 9, like
  every other pool node.
- The director polls its script (`001BA1F0`) **inside** its own tick.
  Script effects (camera, bars, messages, flag 0x3B) therefore happen at
  this node's position in the walk.

**The adapter** (the same pattern as `tick_manager_8257A0`):

```c
EmDirectorOriginalNode n = {&actor->status, &actor->u04[0], &actor->u04[1], address_of(actor)};
int rc = em_director_original_tick(&n, &s_director_world, &s_director_workers, &at);
/* rc 1: ran; 0: freed itself through w_001AFC10 (the pool continues);
 * -1: scene fault at `at` (EM_SCENE_FAULT_NULL_WORKER for a NULL,
 *     WORKER_FAILED otherwise). */
```

**World → canonical storage.** These must be canonical before binding:

| Pointer | Storage |
|---|---|
| `d810813` | `em_scene_progress_at(s, 0x00810813, 1)`, canonical since HK (`g.cine_step` is deleted). Until this module is bound, the legacy stand-in em_director.c reads and writes it there. Roger's translation writes the same byte (0x11, 0x823A04). |
| `d810793` | `em_scene_progress_at(s, 0x00810793, 1)`, canonical since HK. Written by the director scripts' op06 on flag 0x3B (`em_area_script`'s `d810758 + 0x3B`: the same storage once the script host's flag array is canonical, L19) and read by Roger. |
| `d810CC3` | `em_scene_progress_at(s, 0x00810CC3, 2)`, canonical since WP-6 (`opening_key_item_zero` deleted in HK). The opening's `001C4760(0, 1)` and the legacy director's beat-0 `001C4760(1, 1)` already run `em_director_original_001C4760` on it through `em_director_original_001C4760_scene` (em_director_original.c is in COMMON for that). |
| `d8106B0` / `d8106B1` | The request block (`EmSceneState.req[0]/[1]`). It is never reached from the director, because a0 = 1. |
| `d810350` | The player's canonical `+0xA0` vector (x, y, z), the same pointer `EmTruckWorld.player_a0` uses. |
| `quad[0..2]` | `em_director_original_load_quads` over the user's AREA11 overlay image (MWo3, 0x7800 bytes). The live path needs that image anyway for the director scripts (`EmScriptImage {overlay, 0x823500, …}`, as in `tests/area_script_test.c`). `area11_flow.emaf` holds only X/Z and is the legacy input; it is not used. |
| `d26C5D8` | The 76 SDK table bytes at ELF 0x26C5D8. They have the same layout and bytes as `EmInteractionMath` (`interaction.emis`), so a `memcpy` into an `EmDirectorAtanTables` works, as does `em_director_original_load_atan_tables(elf)`. |

**Workers.**

| Worker | Binding |
|---|---|
| `w_001BA1A0(ctx, block, entry)` | Check `block == self + 0x1F0`, then call `em_area_script_start(&director_script, entry)`. It returns −1 on failure. |
| `w_001BA1F0(ctx, self, &r)` | `r = em_area_script_tick(&director_script)`. Map −1 to −1; otherwise pass the result (0, 1 or 3) through. |
| `w_001AFC10(ctx, self)` | `em_actor_pool_free_001AFC10(s_pool, s_scene, actor)`, marking the node freed as the 8257A0 adapter does. |

**The script host for this owner.** `director_script` is one `EmAreaScript`
with:
- `world.self = 0x7A93F0` (the node address);
- `s040`/`s0B0`/`s0C0` = the node's `+0x40`, `+0xB0` and `+0xC0`;
- an image that covers 0x8294C0..0x829E80 (the overlay);
- the worker table of AREA_SCRIPT.md section 6.

The records these scripts need are admitted by `em_area_script` except
op16's predicate `00182BF0` and op18's skip landing
`001B0C00`/`001B6250`, which are still untranslated there
(AREA_SCRIPT.md section 5). Until those are bound, the director is not
fully live. **Do not wire it with stand-ins.**

**Retire when bound.**
- `em_director.c` `director_tick`, `kCineBeats`, `cine_*` and the `g.cine_*`
  fields (the step byte is already canonical: HK deleted `g.cine_step`).
- `em_area11_flow.c` `em_area11_trigger_contains`, which uses host
  `atan2f`, and `em_area11_step_after_beat`.
- The `area11_flow.emaf` export, if nothing else reads it.
- FIRST_LEVEL_AUDIT H10 and SCENE_COORDINATOR_DESIGN 4.4 row #21 then name
  this module.

**Makefile hunk (for the lead):**

```make
.PHONY: test-director-original
test-director-original:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc -ffp-contract=off tests/director_original_test.c src/game/em_director_original.c -lm -o build/director_original_test && ./build/director_original_test ../Extermination/extract/OVERLAY/AREA11.BIN ../Extermination/config/SCUS_971.12
	python3 tools/test_director_original_reference.py
```

Add `src/game/em_director_original.c` to the app sources in the step that
wires it.
