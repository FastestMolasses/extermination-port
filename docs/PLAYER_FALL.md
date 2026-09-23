# Player fall and landing (AREA11 drops, jumps and slide exits)

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "player-fall-landing". This document covers the original routines that
run while the player falls, drops and lands. Along the route to Roger they
run for:

- the drop off the cage roof in beat 10 (f88–f127);
- the drop onto the 270 plateau in beat 11 (f498–f539);
- the step-off in beat 12 (f42–f83);
- the crevice-jump landing in beat 12 (f277);
- the tower-jump landing in beat 14 (f288);
- not on the route: the slide's own landing (0016C6A0 sub-state 0x15 calls
  0017C580 and 00224290). Beat 06_hill_slide goes +5 0 -> 1 -> 0x1C -> 0
  without it; the unit oracle is its only evidence.

It covers what each routine does, the translation `src/game/em_player_fall.c`,
how it binds, and the evidence. The module is **built and tested but not
wired**. Section 4 lists what the coordinator binds.

## 1. What the original does

Every routine works on the 0x320-byte player record. The addresses in
brackets are the instructions translated there.

### 00162DB0: state 5, the fall (entered by 00179680: +5 = 5, +6 = 0, +1F0 = 11)

The sub-state is +6.

- **0: the edge test.**
  - 00179450(p, p+B0) runs first. Then, three times, 001026A0 transforms
    a D_00248580 point by the matrix at +D0 into 0x700038A0, and 00179450
    runs on that point.
  - Each query that finds a floor (a nonzero return) with +258 above
    −(D_002488B0 − 1.8) = −22.2 (00162E6C) sets a hit.
  - D_00248580 holds (0, 0, 6.75), (−4.5, 0, 6.75) and (4.5, 0, 6.75).
  - **With a hit:** +6 = 0xA, +38 = D_00248560[+25C] and +2EC = D_00248570[+25C].
  - **Without a hit:** 00174AC0(p, 0) (the turn) runs, and its result picks
    the tier byte: +23F if it returned nonzero, else +25C.
    - Tier 3 goes straight to 0xA with that tier's speed. On the +23F path
      this happens only when 001755B0 returns 0.
    - Otherwise 0017D080 (the ledge catch) runs. If it returns nonzero, the
      result is +6 + 1, +1F0 = 0xA and 001749A0(p, 0x83, 0, 4.0). If it
      returns 0, +6 = 0xA with that tier's speed.
  - **Every path** ends with +2F4 = +B4 and +25F = 2 (00163134/00163138).
  - The tables are D_00248560 = {0, 0.2, 0.3, 0.5} and D_00248570 = {0, −0.2,
    −0.2, −0.4}.
- **1 to 5: the ledge catch.**
  - **1:** waits while +200 has 0x8000.
  - **2:** when +3C ≤ 15, sets +B4 = +294 − 0.8.
  - **3:** when +3C ≤ 12, plays sound 0xFF.
  - **4:** when the clip ends (+200 & 0x1000):
    - the feet go to (+290, +294 − 20.5, +298);
    - 001749A0(p, 00188550(p), 0, 0.0) requests the clip;
    - +C4 = 001B1470(π + +C4), and build_trs_matrix rebuilds +D0;
    - then 0017F320 decides: nonzero sets +6 = 5; zero sets the hang
      +5 = 9, +6 = 0, +1F0 = 0x10, +D = 0 (00163290).
  - **5:** +2F4 = +B4, then +5 = 7, +6 = 0, +1F0 = 0xD and +2EC = −0.2
    (001632BC).
- **0xA, then 0xB every frame: the air.**
  - **0xA** runs once:
    - +7 = 0 and +2E0 = +38 / 60 (DIV.S, round to nearest);
    - 0x70003A20 = the frame count of clip 0x73 (CVT.S.W);
    - anim_clip_arbiter(p, 0x73, 8.0, frames − 10) and 00182870(p, 0);
    - it then falls through into 0xB.
  - **0xB, in order:**
    1. +C0 = 001B12B0(0, +C0, 0.06981317).
    2. `landed` = 00224290(p).
    3. It reads +38 and +2E0 **after** that call (0016336C).
    4. The speed:
       - while +38 > +2E0, +38 −= +2E0 and 00178B90(p, 1) translates;
       - otherwise +38 = 0 and 001764E0(p) runs.
    5. 00179880 and 00175900(p, 1).
    6. **On contact (+A):** 0017C580 lands, unless `landed` is set.
    7. **Without contact:** when the clip ended, +38 ≤ 0 and `landed` is 0,
       the state becomes the drop: +5 = 7, +6 = 0, +1F0 = 0xD, clip 0x72
       (00163420).
    8. When +23A is 0x5D, 0021D250(p, 0) runs.
- **0x63:** 0021D2E0(p, 0x78, 0).

### 001639E0: state 7, the drop

The sub-state is +6.

- **0:** +6 = 1, +7 = 0, +38 = 0, +25C = 0, then clip 0x72 (blend 8.0).
  It continues straight into 1.
- **1:** in this order:
  1. `landed` = 00224290(p).
  2. On +23B 0x39 (a surface mode AREA11 does not use), `shaft` is set
     unless +B4 < +2F4 − 12.
  3. With neither set, 0017C860(p, +2EC) (the ledge grab, which enters
     +5 = 4) may take over. A nonzero result returns at once.
  4. Then 001764E0, 00179880 and 00175900(p, 1).
  5. On contact with `landed` clear, 0017C580 runs.
  6. When +23A is 0x5D, 0021D250(p, 0) runs.
- **2:** 0021D2E0(p, 0x78, 0).

### 0017C580: land (+5 = 8, +6 = 0, +1F0 = 0xF at 0017C590)

1. 00182870(p, 1) plays the landing sound, then +25C = 0 and +38 = 0.
2. **Reset.** +F == 0x63, or 001000E0(00128350(+220), 0) with +234 == 1,
   resets to the reaction +0 = 2, +4 = 2, +5 = 3, +6 = 0, +1F0 = 0x3F
   (0017C5F4).
3. **Otherwise** d = +B4 − +2F4 (the height fallen) is stored at 0x70003A20.
   - d < −104 is the heavy landing, +6 = 1:
     - rumble (1, 0xEE, 0x3C, 1);
     - +0 = 2, +220 = 0, +25F = 0;
     - sound 0x151, clip 0x2B, +1F0 = 0x40.
   - Otherwise 00174AC0(p, 0) runs first, then:
     - **+220 ≤ 0:** +6 = 3.
     - **+228 ≥ 100 with D_008106F1 set:** +6 = 0xA.
     - **Otherwise d is re-read from 0x70003A20** (0017C714). 00174AC0
       writes that word in some paths.
       - **d ≤ −50:** +6 = 3 with rumble (0, 0xD0, 0xA, 1), +224 = 5.0,
         0021C350 and sound 0x151.
       - **d ≤ −14.5:** +6 = 4.
       - **Otherwise:** +6 = 5.
     - On gait 3 (+23F), the −50 and −14.5 cases first try 001755B0. When
       it returns 0, the result is +6 = 2 with sound 0x13D instead.
   - Each of these ends with +7 = 0.

### 00163B40: state 8, the landing (dispatch on +6, 00163B40)

The sub-state routines use +7.

| +6 | routine | what it does |
|---|---|---|
| 5 | 00163C10 | clip 0x6E, wait for its end, then the walk hand-back |
| 4 | 00163D50 | the same with clip 0x6D |
| 2 | 00164220 | clip 0x74 at speed 0.8, which decays by D_0024889C (1/44) per frame through 00178B90; then the hand-back |
| 3 | 00163E90 | clip 0x75 with +302 = 1 (the drop keeps running), then by health: the reaction 2/3 or 0x1C3 and the fade (below) |
| 0xA | 001643B0 | clip 0x75; rumble; +220 capped at 60; 0021C120 then 0021C190 (revive) |
| 1 | 0021D2E0(p, 0x78, 0) | |
| other | nothing | |

- **The walk hand-back** (the +7 = 2/3 or 3/4 or 4/5 steps):
  - 00174AC0(p, 0) runs first.
  - At gait ≥ 2: +7 + 1, then 0017C440(p, 0) (the re-entry).
  - Otherwise: +25C = 0, then 0017C540.
  - Then 00178B90(p, 0) runs until the blend flag 0x8000 clears, and
    0017C540 follows.
- **The tails.**
  - 00163C10, 00163D50 and 00164220: 001764E0, +B4 −= 0.2 (ADD.S of −0.2),
    00175900(p, 1), 001796C0.
  - 00163E90 and 001643B0 test +302 first. While it is set: 001764E0,
    00179880, 00175900(p, 1). Otherwise the same tail.
- **00163E90 by +7** (jtbl_0026D600: 5–9, 15 and ≥ 16 do nothing):
  - **0:** clip 0x75, +302 = 1 and +2EC = 0.
  - **1:** when +3C ≤ 40:
    - health +220 ≤ 0 with +234 == 1 gives +4 = 2, +5 = 3, +6 = 0,
      +1F0 = 0x3F;
    - health ≤ 0 otherwise gives +7 = 0xA, +1F0 = 0x40;
    - otherwise +7 = 2, +20E = 0x3C and +302 = 0.
  - **2 to 4:** the hand-back.
  - **10:** clip 0x1C3.
  - **11:** waits while +200 has 0x8000.
  - **12:** when +3C ≤ 38: rumble, then voice 0x14F (+234 == 1) or 0x14E.
  - **13:** at the clip end:
    - +28 = 0x78;
    - anim_eval_skeleton;
    - 001EFD90(0x80000043, 0x700038A0, p+B0), where 0x700038A0 is
      (node x, 0.1 + +250, node z, 1.0) and the node is *(D_00275B40 + 4)'s
      +C0/+C8.
  - **14:** counts +28 down. On the frame it read 0: +7 + 1 and
    001AEDE0(4, 0).
- **001643B0 by +7** (jtbl_0026D640: ≥ 6 do nothing):
  - **0:** the same as 00163E90's 0.
  - **1:** when +3C ≤ 40:
    - rumble (1, 0xEE, 0x3C, 1);
    - +220 = min(+220, 60);
    - 0021C120;
    - +7 + 1.
  - **2:** when 0021C190 returns nonzero, +7 + 1 and +302 = 0. Otherwise
    +204 = 0.
  - **3:** at the clip end, +7 + 1. Otherwise +204 = 0.25.
  - **4 and 5:** the hand-back.

### 00224290: the landing hit check (returns 0 or 1)

- **+7 == 0:**
  - With +224 == 0 and +22C == 0 it returns 0.
  - Otherwise:
    - a nonzero +224 plays sound 0x152 and runs 0021C350;
    - a nonzero +22C (re-read) plays 0x153 and runs 0021C270;
    - then +7 + 1, rumble (0, 0xC0, 5, 1), clip 0x76, and it returns 1.
- **+7 == 1:** at the clip end: +7 = 0, clip 0x72 and +20E = 0x3C. It
  returns 1.
- **Any other +7:** returns 1.
- The comparisons are C.EQ.S, so −0.0 and denormals count as zero.

### 0021D250(p, a1): surface 0x5D

It writes +0 = 2, +220 = 0, +4 = 2, +5 = 0x16 (0021D270), +6 = 0, +7 = 0 and
+1F0 = 0xE. With a1 == 0 it plays clip 0x72 (blend 8). It always rumbles
(1, 0xEE, 0x3C, 1) and plays sound 0x159. AREA11 has 70 grid nodes of
attribute 0x5D (FIRST_CONTROL.md census).

### 0021D2E0(p, frames, hold): the fade wait

- **+7 == 0:**
  - +7 = 1, +0 = 2, +28 = frames (a halfword) and +220 = 0.
  - When +25F == 0 and +300 bit 15 is clear, the skeleton effect runs, as
    in 00163E90's 13.
- **+7 == 1:**
  - With +1F0 == 0xE and +319 set, it plays clip 0x2B.
  - It counts +28 down. On the frame it read 0: +7 + 1, and 001AEDE0(4, 0)
    unless +F == 0xB.
- **Tail**, when hold == 0: +B4 −= 0.2, 00179880, then 00175900(p, 1). A
  nonzero result clears +2EC.

### 00179880(p, p+2EC): the drop accumulator

It does +2EC += −0.04 (clamped at −4.0 by C.LT.S), then +B4 += +2EC and
+25F = 2.

### Where the readable C differs from the instructions

The routines were translated from the instructions, not from the C:

- **00162DB0 (NEARMISS).** Both 0017D080 exits of sub-state 0 continue to
  the +2F4/+25F tail, where the C returns early. Sub-state 0xB reads +38 and
  +2E0 after 00224290, where the C reads them before.
- **0017C580 (NEARMISS).** It matches the instructions.
- **The others are asm-word files.** 001639E0, 00163B40, 00163C10,
  00163D50 and 00224290 were read from `build/asm`.

## 2. The translation (`src/game/em_player_fall.c/.h`)

- **The record.** Every routine works on `EmPlayerLiveActor` (the raw
  record) by original offset. Arithmetic is `em_ee_float.h` on raw bit
  patterns (ADD.S with pre-trim, DIV.S round-to-nearest, CVT.S.W truncated,
  and the DAZ/saturating compares).
- **Workers.** Every other original callee is a worker in
  `EmPlayerLandWorkers`: 32 callee workers, two data reads (the node
  *(D_00275B40 + 4) and D_008106F1) and the 0x70003A20/0x700038A0 scratch.
  - **Fail-stop:** every entry point refuses (−1) before its first write
    unless every worker and the scratch are bound. A worker returning < 0
    stops the routine there.
- **Entry points.**
  - `em_player_fall_state5/7/8` are `EmPlayerStateCallback`s (the context
    is the `EmPlayerLandWorkers *`).
  - `em_player_fall_land` (0017C580), `_land_check` (00224290),
    `_surface5d` (0021D250), `_teleport` (0021D2E0), `_drop` (00179880)
    and `_00163C10/_00163D50/_00163E90/_00164220/_001643B0`.
- **The small tables** are D_00248560, D_00248570, D_00248580, D_002488B0
  and D_0024889C: 18 words as constants. The oracle checks them, because
  every case compares what the original read from the ELF with what the
  native wrote or passed.
- **One addition** to the original: a tier byte above 3 faults (−1) instead
  of reading past D_00248560's four rows. +25C and +23F are 0..3 in the
  original.

## 3. Verification

### Unit oracle (`tools/test_player_fall_reference.py`, default ~2 s)

The test runs the original instructions of all 13 routines on `FallEE`.
`FallEE` is the shared interpreter (`test_player_slide_reference.EE`), with
every COP1 and VU0 macro op routed through `tools/ee_float_model.py`; the
shared file is unchanged.

**Hooks and scripts.** Every callee is hooked, with a per-call scripted
effect, and the native workers replay the same script. The effects are:

- the return value;
- writes to actor bytes (+A, +23A, +200, +23F, +C4, +258, +38, +2E0, +22C,
  +314, +B0.., +5);
- 0x70003A20 clobbers from 00174AC0/001755B0/0017D080;
- new node x/z from anim_eval_skeleton;
- the apply and build_trs_matrix outputs.

**What must match:**

- all 0x320 actor bytes;
- the five scratch words;
- the return value (00224290);
- the full worker call sequence with arguments (vectors by value). The
  sound calls are checked to pass (id, 0, 300.0), and 001EFD90, 001026A0
  and build_trs_matrix to pass the original's pointers;
- the $s1 source that 001764E0 sees: 0 in 00162DB0, the record in
  001639E0, the caller's in the landing sub-states. The oracle hands the
  routines a caller $s1 with bit 2 set to tell them apart.

**Checks beyond the byte comparison:**

- The hooked set equals the jal targets of the 13 routines. There are 42
  targets, all hooked or translated.
- Every one of the 127 conditional branches in the 13 routines is seen
  both ways. This is asserted.
- A fault cut: in one case of five, a worker fails at a random call. The
  native must return −1 with exactly the calls up to it.
- The refusal: each of the 35 bindings (34 workers, plus the scratch) is
  removed in turn, on each of 7 entry points (245 checks). Every run must
  return −1 with no call and no write.

**Scale.** Quick runs 4,000 of 24,000 cases. `EM_TEST_FULL=1` runs all
24,000 (13 s). The last runs (2026-09-23) passed:

- quick: 4,000 cases and 11,121 identical worker calls;
- full: 24,000 cases and 65,143 identical worker calls.

**Mutants.** Twelve were killed:

- the NEARMISS C's early return after 0017D080;
- the NEARMISS C's +38/+2E0 read before 00224290;
- −14.5 compared with `<` instead of `<=`;
- the 0x70003A20 reload dropped;
- one wrong D_00248560 word;
- one wrong D_00248580 word;
- truncated DIV.S;
- the wrong $s1 source;
- the +2EC clear inverted;
- the +F 0xB fade test dropped;
- +302 not cleared;
- the drop tail calling 001796C0.

The only survivor, `<=` for the −4.0 clamp, is equivalent: both paths
store −4.0.

### World mode (`EM_TEST_WORLD=1`, minutes)

The test replays the route beats over the captured RAM (`RouteReplay` on
FallEE, from each beat's source snapshot, with its recorded pad input).
Two replays run for each beat:

- the original stage;
- the stage with the native routines hooked in at 00162DB0, 001639E0,
  00163B40, 0017C580, 00224290, 0021D250 and 0021D2E0. Their workers run
  the original callees in the same EE (`WorldLand`).

After every frame, the **whole 32 MB RAM and the scratchpad** (SHA-1), the
actor and the sound/effect calls must be identical, and every trace row
must match within its printing precision (`route_row_check`). Beat 14 is
row-checked to f282, because Roger's script starts at f283 and the replay
runs only the player stage.

Results (2026-09-23; about 6 min for beat 12 alone and 14 min for 10, 11
and 14 together):

| beat | frames identical | trace rows | sound/effect calls | native callbacks | (+5, +6) reached |
|---|---|---|---|---|---|
| 10_cage_roof_roger (to f170) | 182 | 171 | 31 | state5 12, state8 27 | 5/0 5/A 5/B 8/5 |
| 11_crevice_prompt (to f560) | 561 | 561 | 62 | state5 14, state8 27 | 5/0 5/A 5/B 8/5 |
| 12_crevice_jump (whole) | 345 | 337 | 51 | state5 14, state8 54, land 1 (the jump) | 5/0 5/A 5/B 8/5 |
| 14_roger_encounter (to f300, rows to f282) | 301 | 283 | 17 | land 1 (the jump), state8 12 | 8/5 |

Every frame was identical, with the whole RAM and scratchpad included. The
original replay on the measured float model also matches every checked
PCSX2 row.

**Coverage.** The route reaches only these paths:

- the edge test with no hit (5/0);
- the air (5/A, 5/B);
- 0017C580's "otherwise" branch (d > −14.5, +6 = 5);
- 00163C10;
- 0017C580 through the jump state 001634A0.

Everything else is proven only by the unit oracle: state 7, the other
landing sub-states, 00224290's hit paths, 0021D250 and 0021D2E0. The slide
landing (beat 06) is not replayed here. `test_player_slide_reference.py`
binds the original 0017C580/00224290 there.

## 4. Binding (coordinator)

Nothing is wired. When the FLOOR layer's closure is bound
(FIRST_CONTROL.md "FLOOR state closure"):

### Stage slots

- **0015B130:**
  - `b.stage.state[5] = em_player_fall_state5`;
  - `state[7] = em_player_fall_state7`;
  - `state[8] = em_player_fall_state8`;
  - each `state_context[...] = &land_workers`, an `EmPlayerLandWorkers`.
- **0015B530** (+4 = 4, `EmPlayerStageMajor4` in em_player_stage_workers.h):
  - `routine[EM_PLAYER_MAJOR4_00162DB0] = em_player_fall_state5`;
  - `routine[EM_PLAYER_MAJOR4_00163B40] = em_player_fall_state8`;
  - with the same context.

### Slide workers

These take `EmPlayerSlideActor *`: `land` (0017C580), `land_check`
(00224290), `surface5d` (0021D250 with a1 = 0) and `teleport` (0021D2E0
with 0x78, 0). Each adapter:

1. writes the mirror into the live record
   (`em_player_slide_actor_to_live(actor, player_states_actor())`);
2. calls `em_player_fall_land`, `_land_check`, `_surface5d` or `_teleport`;
3. reads the mirror back with `em_player_slide_actor_from_live`.

The routines write bytes outside the slide mirror (+0, +220, +224, +28,
+2F4, +302, +4). They land in the live record directly, which is the
original behaviour.

### Workers of `EmPlayerLandWorkers`

Each worker's original, and what can fill it today:

| worker | original | binds to |
|---|---|---|
| `floor` | 00175900 | `player_states_floor_service` |
| `fall_check` | 001796C0 | `player_states_fall_check` |
| `probes` | 001764E0 | the wall probes over the record (`player_states_wall_probes`, through an adapter that sets the $s1 source below) |
| `floor_query` | 00179450 | `em_player_floor_query` after the column worker 0019BC40 at `point`; it writes +258 |
| `apply` | 001026A0 | the SDK matrix apply |
| `trs` | build_trs_matrix | |
| `wrap` | 001B1470 | `em_player_001B1470` (em_player_stage_workers.h, bit patterns) |
| `approach` | 001B12B0 | `em_player_slide_approach` exists, but on the older float model (EE_FLOAT_MODEL.md 5c) |
| `heading` | 00174AC0 | the port's heading translation (em_player_heading) must also publish its 0x70003A20 stores into `scratch->s3A20`; see the scratch below |
| `reentry` | 0017C440 | the port's run-stop re-entry (em_player_motor / em_player.c, checked by test_player_reentry_reference.py) |
| `handoff` | 0017C540 | `em_player_reaction_0017C540` |
| `react_0021C350`, `react_0021C120`, `test_0021C190`, `react_0021C270` | 0021C350, 0021C120, 0021C190, 0021C270 | the player-reaction lane's translations |
| `land_sound` | 00182870 | `em_player_reaction_00182870` |
| `convert_00128350` | 00128350 | the SDK float-to-int |
| `test_001000E0` | 001000E0 | |
| `test_001755B0` | 001755B0 | |
| `test_0017D080` | 0017D080 | em_player_recovery lane |
| `test_0017F320` | 0017F320 | |
| `pose_clip` | 00188550 | |
| `ledge` | 0017C860 | |
| `request` | 001749A0 | |
| `arbiter` | anim_clip_arbiter | |
| `clip_frames` | 001C61D0 | on the `+40` bank word |
| `sound` | 001FBD50 | (p, id, 0, 300) |
| `rumble` | 001B61C0 | |
| `effect` | 001EFD90 | |
| `fade` | 001AEDE0 | |
| `skeleton` | anim_eval_skeleton | |
| `hip` | | node *(D_00275B40 + 4) +C0/+C8 after the skeleton |
| `progress_8106F1` | | the canonical D_008106F1 byte (the reaction lane's `EmPlayerReactionScene.d8106F1`: 0021C270 sets it, 0021C190 clears it) |

### The $s1 source of `probes`

001764E0 tests ($s1 & 4) of its caller (PLAYER_FLOOR.md P16). The native
passes where $s1 comes from, and the binder sets
`EmPlayerProbeScene.inherited_s1` from it:

- `EM_PLAYER_LAND_S1_ZERO`: 0;
- `EM_PLAYER_LAND_S1_RECORD`: the original record address 0x8102B0, whose
  bit 2 is clear;
- `EM_PLAYER_LAND_S1_CALLER`: the stage's inherited $s1. 0015BCF0,
  0015BA50, 0015B130 and 0015B530 never write $s1, so this is the same
  value the idle and walk callbacks see.

### The scratch

`scratch` is one binder-owned `EmPlayerLandScratch`. Every worker whose
original writes 0x70003A20 must store into `scratch->s3A20`: 00174AC0 at
00174CF8/00174E24/00174E4C, 001755B0 and 0017D080. 0017C580 re-reads that
word after 00174AC0.

### Duplicate translations

The player-reaction lane (em_player_reaction.c) also translates 0021D250,
0021D2E0 and 00179880, with its own oracle. Both are original-verified. The
lead should keep one and bind it on both sides.

## 5. Limits and open items

- **Not wired.** The FLOOR gate stays closed until the whole closure and
  the stage workers are bound. FIRST_CONTROL.md "Known gap" still applies.
- **Exits to states outside this lane:**
  - 9 (001647D0, hang);
  - 4 (00162A40 via 0017C860);
  - +4 = 2/+5 = 3 (0021E830, via 0017C580's reset and 00163E90's health 0);
  - +4 = 2/+5 = 0x16 (00225570, via 0021D250).
- **The world mode cost.** It takes minutes (about 1 s per replayed frame
  per process). Run it when this module or one of its originals changes.
- **Tier bytes above 3** fault instead of reading the neighbouring table.
