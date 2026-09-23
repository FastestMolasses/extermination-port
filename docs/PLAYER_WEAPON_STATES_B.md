# Player weapon states B: +5 = 0x20, 0x21, 0x22 (00173000, 001735C0, 00173E60)

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "player-weapon-states-b". This document covers three entries of
0015B130's state table and the one private callee among them:

- 00173000, +5 = 0x20: the R2 aiming stance;
- 001735C0, +5 = 0x21: the light three-hit melee combo;
- 00173E60, +5 = 0x22: the heavy melee stab;
- 00173DD0: the stab's in-swing yaw steer. Only 00173E60 calls it.

It records what they do, the translation
(`src/game/em_player_weapon_states_b.c/.h`), how to bind it and the
evidence. The module is **built and tested but not wired**. All three states
are in the FLOOR state closure (FIRST_CONTROL.md, "FLOOR state closure":
+4 1, +5 0x1D..0x22). The first three stances, 0x1D..0x1F, and the action
machine 001607D0 that enters all six are lane A's
(`em_player_weapon_states_a.h`).

## 1. What the original does

Every routine works on the 0x320-byte player record. The addresses in
brackets are the instructions translated there. The three state routines
are byte-matched C in the decomp. 00173DD0 is NEARMISS C, so it was read
from its instructions.

### Entry

- **0x21 and 0x22.** 001607D0 enters them on a pressed button while the
  player is unarmed: the FIRE button (`D_00810E74 & 0x70003B78`) gives 0x21,
  and SQUARE (`& 0x70003B74`) gives 0x22 (the decomp's FINDINGS,
  "KNIFE/MELEE").
- **0x20.** 001607D0 enters it, and so does 00223C70 at 00223EFC and
  00224084.
- **Reachability.** These states are entered by the player's own presses,
  so AREA11 can reach them. No recorded route beat does: the census of +5
  over all 15 route traces shows only 0, 1, 2, 5, 6, 8, 0xB, 0xC, 0x1C and
  0x25 (section 3).

### 00173000: +5 = 0x20, the R2 stance (dispatch on +6)

It first stores 0 into D_008106E0 (00173014) on every call, whatever +6 is.

| +6 | What it does |
|---|---|
| 0 | 0017B300(p, 0), then by +317. **0:** +6 += 1 (re-read after the call), +278 = 0.5, +2F2 = 0, +2E = 0, +275 = 0, then 001749A0(p, 0x188, 0, 1.0). **Nonzero:** +6 += 2, then 0016F530(p, 0). Both paths then set +27C = 0.5, +7 = 0, +302 = 0, +276 = 0, +2F0 = 0 and +274 = 0. |
| 1 | When +200 has 0x1000: +6 = 2, 0016F530(p, 0), then the aim-pose clip (below). Always: anim_eval_skeleton(p), then copy_qw4(p + 2A0, node + 90). The node is *(D_00275B40 + 0x10), bone node 4. |
| 2 | +94 = 7 (a halfword) and +302 = 0, then 0017ABA0(p). Then by D_00810CA4. **0:** if +274 is set, +2F0 += 1, and a re-read value of 3 or more resets it to 0. Then 00199220(p). **1:** 00199220(p). **Other:** nothing. Then jtbl_0026D6C0 on +275 (re-read): 0 goes to 00170A60(p, 1), and 1..5 go to 00171320, 00171670, 00171B00, 00171E90 and 001723D0(p). Values of 6 and above do nothing. |
| 3 | 0016F600(p). +1F0 == 0x33 then sets +1 = 0. |
| 0x63 | The aim blend-out. +6 = 0x64 and +28 = 8. +26C = (0.5 − +27C) / 8 and +270 = (0.5 − +278) / 8. With L the record at the word +20, it takes atan2(−L+C8, L+C0) (0011E620). It stores that at 0x70003A20, reloads it, and sets +218 = 001B1470(π/2 + it). It then falls through into 0x64. |
| 0x64 | It counts +28 down (a signed halfword, post-decrement). **It read 0:** +6 += 1, +27C = +278 = 0.5, then the aim-pose clip. **Otherwise:** +27C += +26C and +278 += +270. Then, when +1F0 is 0x33: anim_matrix_dispatch(p) and nothing else. For any other +1F0: anim_matrix_dispatch(p), then +1F0 is re-read. On 0x32 or 0x35, or +275 == 4, or +2F2 set, node 4's matrix is copied to +2A0. Otherwise node 4's +C0/+C4/+C8 go to +2D0/+2D4/+2D8. |
| 0x65 | Holster: +6 = 0x66 and +276 = 0. Then 001749A0(p, 0x189, 0, 1.0), sound 0x163 (001FBD50(p, 0x163, 0, 300.0)), and +317 = 0. |
| 0x66 | When +200 has 0x1000: +5 = 0x14, +6 = 0 and +1F0 = 0x26. Otherwise +C4 = 001B12B0(+218, +C4, 0.043633234), a 2.5 degree turn. |
| other | nothing |

**The aim-pose clip** (0017314C, 001733B4) is the halfword
D_00248B88[+275] when +5 is 0x1D or 0x1E, and D_00248C68[+275] otherwise.
It goes to 001749A0(p, clip, 0, 0.0).

**No tail.** 00173000 does not run the standing tail (001764E0 and the
rest). Its callees keep **$s1 = the record address**, because 00173000
keeps p in $s1.

### 001735C0: +5 = 0x21, the light combo (dispatch on +6, then +7)

`row` is the byte +236 (0 or 1). The target T is the record the word +18
addresses (D_008102C8 holds the same word). The original re-loads +18 for
every access to T.

- **+6 = 0:** +6 = 1, +7 = 0, +38 = 0, T+A = 0 and +302 = 0xFF. It then
  falls into +6 = 1.
- **Hit 1, +6 = 1:**
  - **+7 0:** +7 = 1. Clip D_00248690[row].0 through 001749A0(p, clip, 0,
    +1FC), then +2E = 0 **after** the call (001736E0).
  - **+7 1:** when the blend is over (+200 & 0x8000 clear), +7 = 2.
  - **+7 2:** when +3C ≤ D_002486A0[row] (C.LE.S), +7 = 3 and **the
    impact** runs with damage 3, sound 0x17D and marker 0x81. Then the
    chain press.
  - **+7 3:** **the hit confirm.** Without a hit, the chain press runs.
    Then +3C ≤ D_002486A4[row] sets T+0 = 2. Then +3C ≤ D_002486D0[row]:
    if +2E is set, +6 += 1, +7 = 0, the clip D_00248690[row].1 plays at
    blend 1.0, and T+0 = 2. Otherwise +7 += 1.
  - **+7 4:** at the clip end (+200 & 0x1000), +6 = 0x63.
- **Hit 2, +6 = 2:**
  - **+7 0:** +7 = 1 and +2E = 0.
  - **+7 1:** the impact at D_002486A8 (damage 3, 0x17E, 0x82), then the
    chain press.
  - **+7 2:** the hit confirm. Without a hit, the chain press, then the
    chain at D_002486D4[row] with the clip D_00248690[row].2.
  - **+7 3:** the clip end sets +6 = 0x63. Otherwise +3C ≤
    D_002486AC[row] sets T+0 = 2.
- **Hit 3, +6 = 3:**
  - **+7 0:** +7 = 1.
  - **+7 1:** the impact at D_002486B0 (damage 5, 0x17F, 0x82). There is
    no chain press.
  - **+7 2:** the hit confirm. Without a hit, +3C ≤ D_002486B4[row] sets
    +7 = 3 and T+0 = 2.
  - **+7 3:** the clip end sets +6 = 0x63.
- **+6 = 0x50 to 0x64:** the melee exit (below).
- **Every call** ends with the melee tail.

**The pieces.**

- **The impact.** It sets T+0 = 1, then T+36 = damage (a halfword), each
  through its own load of +18. It then plays 001FBD50(p, sound, 0, 300.0)
  and stores the **low byte of the returned handle** in +302. Last, +25E =
  marker.
- **The hit confirm.** When T+A is nonzero:
  - +6 = 0x50;
  - 0011A070(+302), then +302 = 0xFF.

  The original tests "+302 != −1" on the zero-extended byte (001737BC,
  001739EC, 00173BF8, 00173FE8). That test is always true, so 0011A070
  runs even for 0xFF. The rest of the phase is skipped.
- **The chain press.** `D_00810E74 & *(u16 *)0x70003B78` nonzero sets
  +2E = 1.

**The tables** are read from the ELF by row (+236):

| table | row 0 | row 1 |
|---|---|---|
| D_00248690 (clips) | 0x10B 0x10C 0x10D | 0x1BD 0x1BE 0x1BF |
| D_002486A0 (A0 A4 A8 AC B0 B4) | 24 20 26 15 41 30 | 25 20 28 16 41 34 |
| D_002486D0 / D4 (chain) | 19 19 | 19 19 |

### 00173E60: +5 = 0x22, the heavy stab (dispatch on +6)

- **0:** +6 = 1, +7 = 0 and +38 = 0. Then 001749A0(p, D_002754A8[row], 0,
  +1FC). **After** that call, T+A = 0 and +302 = 0xFF (00173F1C).
  D_002754A8 is {0x10E, 0x1C0}.
- **1:** when the blend is over, +6 = 2.
- **2:** 00173DD0(p). Then, when +3C ≤ D_00248700[row] (43.0):
  - +6 += 1;
  - T+0 = 1 and T+36 = 0xF;
  - sound 0x17F, its low byte in +302;
  - +25E = 0x83.
- **3:** the hit confirm. Without a hit: 00173DD0(p), then +3C ≤
  D_00248704[row] (29.0) sets +6 += 1 and T+0 = 2.
- **4:** at the clip end, +6 = 0x63.
- **0x50 to 0x64:** the melee exit (below).
- **Every call** ends with the melee tail.

### 00173DD0: the in-swing steer

It copies D_002486F0 = {0, 0.5, 1.0, 2.0} to its stack and calls
00174AC0(p, 2). When that returns nonzero, it sets +C4 = 001B12B0(+218,
+C4, (π × rate[+23F]) / 180): a MUL.S, then a DIV.S. The rate uses the
**gait byte read after 00174AC0**.

### The melee exit and tail (shared by 001735C0 and 00173E60)

| +6 | What it does |
|---|---|
| 0x50 | +6 = 0x51 and +28 = 4, then it falls into 0x51. |
| 0x51 | It counts +28 down. **It read 0:** +6 += 1, then 001749A0(p, 0x10F when +236 is 0, else 0x1C1, 0, 4.0). **Otherwise:** +204 = 0. |
| 0x52 | At the clip end, +6 = 0x63. |
| 0x63 | 00174AC0(p, 1), then by gait +23F. **2 or more:** +6 += 1, then 0017C440(p, 0). **Below 2:** +25C = 0, then 0017C540(p). |
| 0x64 | 00174AC0(p, 1) and 00178B90(p, 0). When the blend is over, 0017C540(p). |

**The tail** runs 001764E0(p), +B4 += −0.2 (ADD.S), 00175900(p, 1) and
001796C0(p). Neither routine sets $s1, so 001764E0 sees the $s1 that
0015B130 inherited.

### Exits

- **00173000:** +6 0x66 sets +5 = 0x14 (0016B8A0, in the closure). Any
  other exit happens in its stance callees.
- **The melee routines:**
  - 0017C440 and 0017C540 exit to +4 1 with +5 0/1;
  - 001796C0 exits to +5 5 or 0x1C.

  All of these are in the closure.

## 2. The translation (`src/game/em_player_weapon_states_b.c/.h`)

- **The record.** Every routine works on `EmPlayerLiveActor` by original
  offset. Arithmetic uses `em_ee_float.h` on raw bit patterns: SUB.S / DIV.S
  / ADD.S / MUL.S / NEG.S, and C.LE.S through `em_ee_c_le_bits`.
- **Workers.** Every other original callee is a worker in
  `EmPlayerWeaponBWorkers`.
  - Three memory accessors are workers too:
    - `link18`, the target record of the +18 word, called at every load
      of +18;
    - `link20`, the +C0/+C8 of the +20 word's record;
    - `bone`, node 4's 16 matrix words, read when the original reads
      them.
  - `clip_id` reads the aim-pose clip tables.
  - `bone`, `clip_id` and `link20` have lane A's signatures, so one
    adapter serves both lanes.
- **The scene** (`EmPlayerWeaponBScene`) holds pointers to the binder's
  canonical words: D_008106E0, D_00810CA4, 0x70003A20, D_00810E74 and
  0x70003B78. The routines read and write each one where the original
  does.
- **Fail-stop.** Each state callback checks the workers and scene words
  its own routine can reach (`em_player_weapon_b_bound_state20/21/22`). It
  returns −1 before its first write when any is missing. The melee states
  do not need the stance workers, and 0x20 does not need the melee ones. A
  worker returning < 0 stops the routine at once with −1.
- **Entry points.**
  - `em_player_weapon_b_state20/21/22` are `EmPlayerStateCallback`s. The
    context is the `EmPlayerWeaponBWorkers *`.
  - `em_player_weapon_b_00173DD0` is exported for the oracle.
- **The small tables** are D_00248690, D_002486A0, D_002486D0,
  D_002754A8, D_00248700 and D_002486F0: 32 values embedded as
  constants. The oracle checks them, because every case compares what the
  original read from the ELF with what the native wrote or passed.
- **Two additions** to the original:
  - a row byte +236 above 1 faults (−1) at a melee table read, instead of
    reading the neighbouring table;
  - a gait byte +23F above 3 faults at 00173DD0's rate read, instead of
    reading past the 4-entry stack copy.

  Both bytes are 0..1 and 0..3 in the original. 001764E0 and the hang's
  sub-state 3 set +236 to 1, and 00179680 clears it.

## 3. Verification

### Unit oracle (`tools/test_player_weapon_states_b_reference.py`)

The test runs the original instructions of 00173000, 001735C0, 00173E60,
00173DD0 and copy_qw4 on `FallEE`. That is the shared interpreter with
every COP1 and VU0 macro op routed through `tools/ee_float_model.py`,
imported from `test_player_fall_reference.py`; no shared file is edited.

**Hooks and scripts.** Every callee is hooked, with a per-call scripted
effect, and the native workers replay the same script. The effects are:

- the return value and the float return;
- stores to the record, among them +6, +1F0, +275, +2F2, +274, +2F0, +317,
  +5, +200, +3C, +B4, +23F and +2E, plus +18 retargeted to a second target
  record;
- stores to node 4's matrix;
- stores to D_00810CA4, D_00810E74 and 0x70003B78.

About 30% of the cases start from the startup-reference capture's player
record (`playable_ee.bin`, 0x8102B0). The rest start from random bytes.

**What must match:**

- all 0x320 record bytes;
- both target records (0x40 bytes each);
- node 4;
- D_008106E0, D_00810CA4, D_00810E74, 0x70003B78 and 0x70003A20;
- the full worker call sequence with its arguments. The sound calls are
  checked to pass (id, 0, 300.0).

**Checks beyond the byte comparison:**

- **The call arguments.** $a0 is checked to be the record wherever the
  original passes it.
- **$s1.** It is checked at every stance callee (the record address) and
  at 001764E0 (the caller's value, marked with bit 2 set).
- **Stores.** Every store the original makes, instructions and hooks
  alike, must land in the compared memory or the stack.
- **The callee set.** The hooked set equals the jal targets of the five
  routines: 28 targets, all hooked or translated.
- **Branches.** All 101 conditional branches are seen both ways. The
  exceptions are the four "+302 == −1" tests, which are asserted never
  taken.
- **A fault cut.** In one case of five, a worker fails at a random call.
  The native must return −1 with exactly the calls up to it.
- **Binding checks (108).** Each binding is removed in turn from each
  state callback:
  - a binding the routine needs must give −1 with no call and no write;
  - a binding it does not need must leave it running.
- **Table bounds (40).** +236 = 2 at a melee table read, and +23F = 4 at
  the steer, must return −1.

**Scale.** Quick runs 3,000 of 16,000 cases (under 1 s). `EM_TEST_FULL=1`
runs all 16,000 (about 1.6 s). The last runs (2026-09-23) passed:

- quick: 3,000 cases, 8,953 identical worker calls and 583 fault cuts;
- full: 16,000 cases, 47,709 identical worker calls and 3,042 fault cuts.

**Mutants.** 21 of 22 were killed:

- 0011A070 skipped for 0xFF;
- C.LT.S for C.LE.S;
- hit 2's damage;
- the +2F2 copy test dropped;
- the +2F0 wrap at > 3;
- +6 not re-read after 0017B300;
- the tail's 001764E0 after 00175900;
- the stab's T+A cleared before the clip request;
- the stab's marker;
- the steer rate row;
- +1F0 not re-read after anim_matrix_dispatch;
- +2E cleared before the hit-1 request;
- the hit-2 end mask;
- the gait test at > 2;
- 0x70003A20 not stored;
- the handle's high byte;
- D_008106E0 not cleared;
- the aim-pose table choice;
- the +2D0 word;
- the 0x66 turn rate;
- +204 not cleared.

The survivor loads +18 once for both impact stores. It is equivalent:
nothing between the two loads can change +18.

### Route and captures

No world mode exists, because no route beat reaches these states. The
census of +5 over the 15 route traces in
`../Extermination/build/s87/route/*/trace.json` shows only 0, 1, 2, 5, 6,
8, 0xB, 0xC, 0x1C and 0x25. The unit oracle is the only evidence. A capture
of a melee press and an R2 hold in AREA11 would let a world replay cover
them (section 5).

## 4. Binding (coordinator)

Nothing is wired. When the FLOOR closure is bound (FIRST_CONTROL.md):

### Stage slots

In 0015B130's table:

- `b.stage.state[0x20] = em_player_weapon_b_state20`;
- `state[0x21] = em_player_weapon_b_state21`;
- `state[0x22] = em_player_weapon_b_state22`;
- each `state_context[...] = &weapon_b_workers`, one
  `EmPlayerWeaponBWorkers`.

0x21 and 0x22 can be bound as soon as the melee workers are. 0x20 waits
for the stance callees.

### The scene

Each pointer must be the one canonical storage the other owners use:

| field | original | storage |
|---|---|---|
| `d8106E0` | D_008106E0 | The lock-target slot E0 of the D_008106B0 block. Lane A's `EmPlayerWeaponScene.d8106E0` and the workers 00199220 / 00185A10 / 00185E30 / 0017ABA0 share it. No canonical storage exists yet. |
| `d810CA4` | D_00810CA4 | `em_scene_state` progress byte (`EM_SCENE_PROGRESS_BASE` + 0x5A4). |
| `spad3A20` | 0x70003A20 | The same word as `EmPlayerLandScratch.s3A20` (em_player_fall.h) and lane A's `spad3A20`. |
| `pad_pressed` | D_00810E74 | `em_scene_state`'s `d810E74`. |
| `spad3B78` | 0x70003B78 | The FIRE button configuration halfword, the same storage as lane A's `spad3B78`. |

### Workers

| worker | original | binds to |
|---|---|---|
| `request` | 001749A0 | the clip request (blend as raw bits; convert with `em_ee_float` in the adapter) |
| `sound` | 001FBD50 | (p, id, 0, 300.0), returning the handle |
| `stop_sound` | 0011A070 | `em_player_stage_stop_sound` (em_player_stage_workers.h) |
| `heading` | 00174AC0 | the heading translation (em_player_heading) |
| `approach` | 001B12B0 | the approach translation on the measured model |
| `wrap` | 001B1470 | `em_player_001B1470` |
| `atan2` | 0011E620 | `em_sdk_math_original_0011E620` |
| `translate` | 00178B90 | |
| `probes` | 001764E0 | the wall probes, with `EmPlayerProbeScene.inherited_s1` = the stage's inherited $s1 |
| `floor` | 00175900 | `player_states_floor_service` |
| `fall_check` | 001796C0 | `player_states_fall_check` |
| `reentry` | 0017C440 | the run-stop re-entry |
| `handoff` | 0017C540 | `em_player_reaction_0017C540` |
| `link18` | the +18 word's record | the melee target: *record points to its raw bytes (+0 event, +A hit confirm, +36 damage mailbox). The owner of the +18 word is open (below). |
| `link20` | the +20 word's record | +C0/+C8 (`EmPlayerStageCallees.link20` has this signature) |
| `bone` | *(D_00275B40 + 0x10) + 0x90 | node 4's matrix (lane A's `bone`) |
| `clip_id` | D_00248B88 / D_00248C68 | lane A's `clip_id` |
| `skeleton`, `matrix` | anim_eval_skeleton, anim_matrix_dispatch | |
| `reload`, `draw`, `reload_wait`, `pose`, `acquire`, `fire_*` | 0017B300, 0016F530, 0016F600, 0017ABA0, 00199220, 00170A60 / 00171320 / 00171670 / 00171B00 / 00171E90 / 001723D0 | untranslated, shared with lane A's stances. **At these calls $s1 is the record address**, which matters if any of them calls 001764E0 without setting $s1. |

## 5. Limits and open items

- **Not wired.** The FLOOR gate stays closed until the whole closure is
  bound.
- **00173000's stance callees are untranslated:** 0017B300, 0016F530,
  0016F600, 0017ABA0, 00199220, and the six fire sub-machines 00170A60,
  00171320, 00171670, 00171B00, 00171E90 and 001723D0. Until they are
  translated, state 0x20 faults when entered.
- **The +18 and +20 records.** Nobody has identified them. The FINDINGS
  note "the +0x18 WRITER was not found statically". In the startup
  capture +18 is 0x7AB440 and +20 is 0x7AB730. The binder must map both
  words to the port's storage of those records.
- **No route coverage.** A PCSX2 capture of a CIRCLE press (0x21), a
  SQUARE press (0x22) and an R2 hold (0x20) in AREA11 would allow a
  world replay like the fall lane's.
- **The two table-bound faults** (section 2) are the only departures from
  the original's reads.
