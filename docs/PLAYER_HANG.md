# Player ledge hang: state +5 = 9 (001647D0, 0017F240)

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "player-hang". This document covers the original hang state 001647D0
(0015B130's table entry 9) and its reaction test 0017F240. It records what
they do, how they were read, the native translation
(`src/game/em_player_hang.c/.h`), how to bind it and the evidence. Nothing
here is wired into the live player. The translation is a callback for the
player stage and waits for the coordinator to bind it.

## 1. What the original does

### Entry

- **From the fall.** The fall 00162DB0 turns the body round: +C4 =
  001B1470(pi + +C4). It then rebuilds +D0 (build_trs_matrix) and asks
  0017F320. If 0017F320 returns 0, it stores +5 = 9, +6 = 0, +1F0 = 0x10 and
  +D = 0 (00163290..001632A4). Any other result keeps the fall, with +6 = 5.
- **From the hang itself.** Sub-state 0x23 re-enters +5 = 9, +6 = 0 for
  +1F1 = 2 and for every +1F1 value it does not name (00165828).

It is in the FLOOR state closure (docs/FIRST_CONTROL.md, "FLOOR state
closure": +4 1, +5 9, 001647D0).

### Sub-states (+6)

Every hold sub-state first calls **0017F240(p, 0)**. Where marked **Chk**,
it then calls **0017F320**: a nonzero result drops the player, with +6 = 0xA
and +1F0 = 0x13.

| +6 | What it does |
|---|---|
| 0 | Sets +6 = 1. Clears +23C/+23D/+23E/+2F2/+2F1 and +38, then calls 00182250. Sets +316 = 0. In area 0x11 with +D == 0, +316 = 1 when sqrt((+B0-340)² + (+B8-270)²) <= 115 (mula/madd, then 0011E748). Falls through into 1. |
| 1 | Hold. 0017F240; Chk. Steering: under spad 0x70003B8D it sets +24C = 0 and +23F = 2, else it calls 00174FD0. Then it acts on +24C. **0**, with +D != 1: if 0017E250(p, p+B0) and 0017E510 both return 0, it sets +6 += 1, +1F0 = 0x11 and +26C = D_00248600[+23F], then calls 001749A0(p, 0x7C, 0, 5.0). Otherwise, under 3B8D, it drops. **1**: a Use press (`D_00810E74 & spad 0x70003B76`) drops. **2 / 3**: it sets +2F1 = 0 / 1 and calls 0017E7C0(p, side); a result of 1, 2 or 0xA gives +6 = 0x14, 0x1E or 0x28. Finally +7 = 0. |
| 2 | When the blend ends (+200 & 0x8000 clear): +6 = 3, sound 0x12C. |
| 3 | **At the clip end (+200 & 0x1000):** +25F = 0, then 001C68C0. It sets +B4 = bone1+C4 - 10.5 and +38 = 1.0 + bone1+8, then calls 00178B90(p, 1). +B4 += -0.2, then 00175900(p, 1) and 001749A0(p, 0x8C, 0, 0.0). On surface +23B 0x39 it pushes +B0 by (+D0 matrix × (0, 0, -5, 0)) through 001026A0 and 001028B8, then sets +5 = 7, +6 = 0, +1F0 = 0xD, +25F = 2, +2EC = 0 and +2F4 = +B4. Otherwise 001760C0(p, p+B0, 1, 18.0); on a nonzero result +236 = 1 and +235 \|= 2. Then +6 += 1 and +2EC = 0. **Before the end:** +3C <= 25 gives +204 = 0.5. Otherwise Chk, then steering as in 1. When +24C == 0, +26C = D_00248600[+23F] if +26C <= that entry. Then +204 = +26C. |
| 4 | +B4 += -0.2, then 00175900(p, 1). **0:** 001796C0. **Nonzero:** 00182870(p, 1), 00174AC0(p, 0). If +23F >= 2, +6 += 1 and 0017C440(p, 1); else +25C = 0 and 0017C540. |
| 5 | 00178B90(p, 1). Once the blend is over, 0017C540. |
| 0xA | Drop start. +6 = 0xB, then 001749A0(p, 0x80, 0, 4.0). Clears +21C/+38/+2E4 and sets +2F4 = +B4. |
| 0xB | **At the clip end:** +5 = 7, +6 = 0, +1F0 = 0xD (the fall's 001639E0). **While no blend:** root motion. +38 = bone0+8 - +21C, +21C = bone0+8, then 00178B90(p, 1). Then +2EC = bone0+4 - +2E4, +2E4 = bone0+4 and +B4 += +2EC. |
| 0x14 | Shimmy. 0017F240; Chk. **+7 0:** +7 = 1, then 0017DF70(p, +2F1, 8.0). **+7 1:** +7 = 2 once the blend is over. **+7 2:** 00174FD0. If +24C == D_00275498[+2F1], it calls 0017E7C0(p, +2F1). On result **1**: +38 = ∓D_002485E0[+23F] (negated for +2F1 == 0) and +204 = D_002485F0[+23F]. Then +B0 += +38·cos(+C4) and +B8 -= +38·sin(+C4), and 00182250. On result **2 / 0xA**: +6 = 0x1E / 0x28 and +7 = 0. With result 1, 2 or 0xA, a clip end then plays 00182AF0. **Otherwise** (any other result, or the quadrant test fails): +6 = 0, 001749A0(p, 00188550(p), 0, 16.0), then 00182AF0. |
| 0x1E, 0x28 | 0017F240 == 0: +6 += 1, 0017DFB0(p, +2F1, 4.0). |
| 0x1F, 0x29 | 0017F240 == 0 at the clip end: +6 += 1, 0017E0D0(p, +2F1, 1.0). |
| 0x20 | 0017F240 == 0, then one of three branches. **Use press:** +6 += 1, 0017E1D0(p, +2F1, 1.0), then +1F0 = 0x12. **Otherwise:** Chk; 00174FD0; if +24C != D_00275498[+2F1], +6 = 0x27 and 0017E150(p, +2F1, 1.0). |
| 0x2A | 0017F240 == 0. **Use press:** two points of the +D0 matrix, (∓9, 20, -2, 1) and (∓9, 20, 3.5, 1), with -9 for +2F1 == 0. It calls 0019AFE0(p, near, far, 7). When `& 6` and 00178910(p, 1) != 0: +1F0 = 0x12, +1F1 = 2, 0017E1D0(p, +2F1, 1.0), then +6 = 0x21. **Otherwise:** as 0x20, with +6 = 0x31. |
| 0x21 | Corner move start. It needs +315 == 0 with +3C <= 6.0, or +315 != 0 with the clip end. Then +6 += 1 and sound 0x187. It saves the targets: +2F4 = +2E0, +2F8 = +2E8, +258 = +2E4, and sets +28 = 8. The steps are +2E0/+2E8/+2E4 = (target - position)/8. +26C = \|001B1470(+218 - +C4)\|/8. The clip then depends on +1F1: 1 gives 0xE5 and 6 gives 0x96. 2 gives 0017E150(p, +2F1 or 1 - +2F1 when +315, 8.0). 5 sets +315 = 0, then calls 0017E150. Any other value calls 0017E150. |
| 0x22 | **While +28 != 0:** it steps +B0/+B8/+B4 and sets +C4 = 001B12B0(+218, +C4, +26C), then +28 -= 1. **At 0:** +6 = 0x23. It places +B0/+B8/+B4/+C4 at the targets, then plays the sound for +1F1: 1 gives 00182A70, 6 gives 0x119, anything else 0xFF. |
| 0x23 | At the clip end, by +1F1. **1:** 00182A70, then +5 = 0xC, +1F0 = 0x17 and +2F1 = 0, then 0017FC80(p, 16.0). **6:** sound 0x119, then +5 = 0xE, +1F0 = 0x1D, +D = 2 and +2F1 = 0, then 0017FF80(p, 16.0). **5:** +5 = 0x18, +1F0 = 0x2C, +1F1 = 0, +D = 2 and the row clip. **Any other value:** +5 = 9, +1F0 = 0x10, +D = 0 and the row clip. Each branch sets +6 = 0. |
| 0x27, 0x31 | At the clip end: +6 = 0, then the row clip. |
| other | Nothing. |

Here "the row clip" means 001749A0(p, 00188550(p), 0, 16.0). "bone0" and
"bone1" mean `*(D_00275B40)` and `*(D_00275B40 + 4)`. D_00275B40 =
D_00275B48 + 0x110 (anim_bone_array_setup): it is the player's own bone
array at record +0x40. In the captured state 04 it is 0x8102F0, the player
at 0x8102B0, and its words are 0xD689C0 and 0xD1C1C0.

**Exits written by the hang itself:**
- +5 = 7, when the 0x39 push runs or sub-state 0xB ends;
- +5 = 0xC, 0xE, 0x18 or 9, from sub-state 0x23;
- +4 = 2, +5 = 6 (0015B770's 00222AD0), through 0017F240.

Its workers can exit too:
- 0017C440 and 0017C540 return to +4 = 1, +5 = 0 or 1;
- 001796C0 can enter +5 = 5 or 0x1C.

All of these states are already in the FLOOR closure table.

### 0017F240(p, arg)

- **Reaction.** It reacts when +224 or +22C is nonzero, or when +F bit 1 is
  set. The float tests are c.eq.s against zero, so -0 and denormals count as
  zero. With arg == 0, or when the clip has ended, the reaction sets
  +302 = +5, +4 = 2, +5 = 6 and +6 = 0, and returns 1.
- **No reaction.** It returns 0. When arg != 0, the clip has not ended and
  +3C >= 3.0, it first sets +204 = +3C - 2.0.
- **Callers.** Only 001647D0 and 0016D130 call it, and both pass 0.

## 2. Reading: instructions, not the NEARMISS C

`src/func_001647D0.c` is a NEARMISS (98.84 %). The translation follows
`build/asm/matchings/main/code/func_001647D0.s`, read branch by branch. The
readable C agrees with the instructions on everything checked. The
instructions settle these points, which the C leaves implicit:

- **Delay-slot stores come before the call:**
  - +38 = 0 before 00182250 (001648F4);
  - +25F = 0 before 001C68C0 (00164BF4);
  - +38 before 00178B90 (00164C38), +B4 before 00175900 (00164C5C);
  - +26C before 001749A0 in sub-state 1 (00164A74);
  - +6 before 0017C440 (00164E7C), +25C before 0017C540 (00164E90);
  - +2F1 before 0017E7C0 (00164AF8 / 00164B50);
  - +6 = 0 / +D before 00188550;
  - +2F1 = 0 before 0017FC80 / 0017FF80;
  - +21C before 00178B90 in 0xB (00164F70).

  +7 = 0 after the sub-state 1 request (00164A7C) is in the delay slot of a
  `b`, so it executes after the call returns.
- **Re-reads after calls.** +6 is re-read after 00174AC0 (00164E68). +2F1 is
  re-read after 0017E7C0 (00165098). The bone words are read after 001C68C0
  (sub-state 3) and around 00178B90 (0xB). The +23F table index is read
  after the call that last wrote it.
- **Sub-state 0x21's side argument.** It is `$v1 - +2F1` with $v1 = 1 from
  001654B0. $v1 is reloaded after 001B1470 returns. The value is a 32-bit
  subtraction, so +2F1 = 2 would pass -1.
- **Sub-state 0.** It computes the radius with mula.s/madd.s, then calls
  0011E748. The result is also stored at spad 0x70003A2C.
- **Sub-state 3.** +26C keeps its loaded bits when it is above the table
  value (bc1tl's mov.s only runs on <=).

## 3. Translation

`em_player_hang.c` works on the live actor (EmPlayerLiveActor, the raw
0x320-byte record) by original offsets. Every line cites the instruction
it translates.

- **Arithmetic.** EE COP1 arithmetic and compares use the `_bits` forms of
  `em_ee_float.h`. The 001028B8 vadd uses `em_vu_vec_bits(EM_VU_ADD, 15)`.
  Moves copy bits.
- **Tables.** D_002485E0, D_002485F0, D_00248600 (4 words each) and
  D_00275498 (2 words) are embedded. The oracle checks the captured RAM's
  copies against the ELF, and cases read every entry.
- **Exports:**
  - `em_player_hang_state(void *context, EmPlayerLiveActor *)`: 001647D0.
    Its context is a `const EmPlayerHangWorkers *`.
  - `em_player_hang_0017F240(EmPlayerLiveActor *, int arg)`: for 0016D130's
    lane.
  - `em_player_hang_vadd`: 001028B8, which fits `EmPlayerHangWorkers.vadd`.
- **Fail-stop:**
  - If any worker is NULL, the callback returns -1 and writes nothing.
  - If a worker returns a negative value, the callback returns -1
    immediately. Earlier writes stay, as the original order leaves them.
  - If the +23F index is > 3 or the +2F1 index is > 1, the callback returns
    -1 at the read. The original would read neighbouring data; the native
    refuses instead.

## 4. Binding (coordinator)

- **Stage slot.** `EmPlayerStatesBinding.stage.state[9] =
  em_player_hang_state` and `stage.state_context[9] = &hang_workers` (an
  `EmPlayerHangWorkers`). 0015B130 dispatches +4 = 1, +5 = 9 there.
- **Scene.** `scene` fills `EmPlayerHangScene` once per callback:
  - `area` = D_00810700;
  - `scripted` = spad 0x70003B8D (`em_scene_state()->spad3B8D`);
  - `pad` = D_00810E74[0], the pad edge halfword;
  - `use_mask` = spad 0x70003B76 (0x40 in the captured config).
- **Bones.** `node(ctx, n, off, &v)` returns the float at
  `*(player +0x40 + 4n) + off`: the player's bone 0 / bone 1 record, as the
  display's skeleton evaluation left it. The worker is called at the
  original's read time, so a 001C68C0 worker that re-evaluates the skeleton
  must update what `node` returns.
- **Workers.** The table lists each worker, its original and the native
  candidates. A candidate counts only once it passes its own oracle and a
  live-actor adapter exists.

  | Field | Original | Native candidate / note |
  |---|---|---|
  | aim_track | 00182250 | untranslated |
  | hang_clear | 0017F320 | `em_player_climb_hang_clear` (mirror; needs a live adapter). The fall lane has `test_0017F320`. |
  | steer_input | 00174FD0 | `em_player_slide_steer_input` (slide mirror; needs a live adapter) |
  | ledge_ahead / ledge_above / ledge_side | 0017E250 / 0017E510 / 0017E7C0 | untranslated |
  | request | 001749A0 | the live pose host |
  | sound | 001FBD50(p, id, 0, 300.0) | the SFX bank |
  | skeleton | 001C68C0 | the display's skeleton evaluation |
  | translate | 00178B90 | em_player_recovery.h (in progress) |
  | floor | 00175900 | `player_states_floor_service` |
  | column | 001760C0(p, +B0, 1, 18.0) | the floor module's column probe over the actor-collision world |
  | land_sound | 00182870 | untranslated here |
  | heading | 00174AC0 | `em_player_heading_record_worker` (em_player_heading_record.h; context an `EmPlayerHeadingRecord`, `world.spad3A20` the shared 0x70003A20 word) |
  | reentry / handoff | 0017C440 / 0017C540 | the motor module; `em_player_reaction_0017C540` (in progress) |
  | fall | 001796C0 | `player_states_fall_check` |
  | clip_DF70 / DFB0 / E0D0 / E150 / E1D0 | 0017DF70 / 0017DFB0 / 0017E0D0 / 0017E150 / 0017E1D0 | untranslated. Per their decomp C, each is a 001749A0 request picked by side and +315: clips 0x7E/0x7F, 0x86/0x87 (0xCE/0xCF), 0x88/0x89 (0xD2/0xD3), 0x81/0x82 (0xD0/0xD1), and 0017DFB0's table (all-word). |
  | clip_FC80 / clip_FF80 | 0017FC80 / 0017FF80 | em_player_major2.h `w0017FC80` / `w0017FF80` fields (in progress) |
  | clip_row | 00188550 | D_002754C0[+235 & 1] = 0x7B / 0x8E. The climb module has it internally. |
  | sound_100 / sound_109 | 00182AF0 / 00182A70 | untranslated: sound base + 0x100 / + 0x109 at range 300 |
  | sweep | 0019AFE0(p, from, to, 7) | the collision sweep (`em_coll_*_original`) |
  | ledge_top | 00178910(p, 1) | untranslated |
  | transform | 001026A0 | `em_player_sdk_apply`, with an adapter that ignores the context |
  | vadd | 001028B8 | `em_player_hang_vadd` |
  | sqrt / cosine / sine | 0011E748 / 0011DE90 / 0011E2A8 | `em_sdk_math_original_float_*` (in progress) |
  | wrap | 001B1470 | `em_player_sdk_wrap` |
  | approach | 001B12B0 | `em_player_slide_approach` |

- **Shared scratch between sweep and ledge_top.** 00178910 reads the hit
  that 0019AFE0 just left, at spad 0x700031B0 and 0x700031D0. Bind both
  workers over one context that carries that hit.
- **Clips.** Met since the display step: the record pose loads the whole
  bank (0x7C, 0x80, 0x8C, 0x96, 0xE5, the helper clips above and the row
  clips 0x7B/0x8E included) and the display draws a bound state's record
  pose (`player_states_bind_display(1)`, PLAYER_CLIPS.md section 6). Bind
  the pose workers with `player_pose_record_host()` as their context.
- **Makefile.** When bound, add `src/game/em_player_hang.c` to COMMON. The
  test target is `test-player-hang-reference`.

## 5. Verification

`python3 tools/test_player_hang_reference.py` (make
`test-player-hang-reference`).

- **Setup.** The original 001647D0, 0017F240 and 001028B8 run from the
  captured AREA11 RAM (`playable_ee.bin`). The test first asserts that the
  executed code and the four tables in that RAM equal the pinned ELF. The
  player record is at its captured address 0x8102B0, with its captured bone
  pointers. The captured record seeds 30 % of the cases.
- **Interpreter.** The shared EE interpreter is subclassed (HangEE). Every
  COP1 op and the VU0 vadd go through `tools/ee_float_model.py`. Any other
  COP1/VU0 op raises.
- **Hooks.** Every 001647D0 callee except 0017F240 is hooked and scripted.
  The same return value and actor/bone writes are applied on both sides.
- **Checks:**
  - All 0x320 actor bytes after the call.
  - All 0x320 bytes at every callee entry, which checks every store-before-
    call and every re-read.
  - The callee sequence with every argument: floats as bits, and vectors as
    the words the callee receives.
  - Coverage: every conditional branch of 001647D0 and 0017F240 seen taken
    and not taken (224/224). Every one of the 20 handled sub-states runs,
    every entry of the four tables is read, and injected worker faults stop
    at exactly the original's bytes.
  - The missing-worker refusal (38 fields).
  - A +23F = 4 table fault.
- **Standalone cases.** 0017F240 runs with args 0/1/2/-1, including NaN,
  Inf and denormal words. 001028B8 runs with special lanes and all three
  aliasings.
- **Runs.** The default run is 12,000 + 3,000 + 3,000 cases and takes about
  1.5–7 s parallel, 10.6 s serial. `EM_TEST_FULL=1` runs 200,000 + 40,000 +
  40,000 cases (61 s, PASS).
- **Mutants killed.** A mutation run killed all 15 mutants:
  - +38 stored after 00182250;
  - the sign of sub-state 4's lowering;
  - 0017E510 called unconditionally;
  - 0x21's `1 - side`;
  - the 0x22 counter with `<=`;
  - 0017F240's `<` as `<=`;
  - a 0x21 subtraction without the EE pre-trim;
  - +2F1 cached across 0017E7C0;
  - sub-state 3's `<=` as `<` (-0 and denormal +26C);
  - 0x1F without 0017F240;
  - the sign of 0x2A's x;
  - 0017F240 not clearing +6;
  - a truncated 0x21 division (FTZ at 9e-38/8);
  - +21C stored after 00178B90;
  - vadd without w.
- **Route captures.** None of the route captures
  (`../Extermination/build/s87/route/`, beats 00–14) contains a frame with
  +5 = 9. The per-beat +5 census has 0, 1, 2, 5, 6, 8, 0xB, 0xC, 0x1C and
  0x25. So no original capture of the hang in AREA11 exists yet. The
  evidence is the instruction oracle above.

## 6. Limits

- **Spad temporaries.** The spad temporaries are not published: 0x70003A20,
  0x70003A28 and 0x70003A2C (sub-states 0 and 0x21), and the 0x700038A0..DC
  vectors (sub-states 3 and 0x2A). Their values reach the workers as
  arguments. The hang's direct callees reached after those writes do not
  read them: 0017F320, 0017E250, 0017E510, 0017E7C0 and 001749A0 have no
  0x70003A2x access, and 00174FD0 only writes 0x70003A20. 00182250 and
  00178910 overwrite the vectors. Nested callees were not checked.
- **Scene reads.** `EmPlayerHangScene` is read once. The original re-reads
  spad 0x70003B8D in sub-state 1 after 0017E250 and 0017E510, which are
  collision probes and are not known to write it.
- **Float workers.** The float workers (sqrt, cos, sin, wrap, approach)
  cannot fault. They follow the house signature of the other state modules.
- **Scripted hooks.** Behaviour of the hooked callees is not claimed. Those
  callees need their own translations and oracles (section 4).
- **No live evidence.** No whole-world run or PCSX2 capture exercises
  +5 = 9 yet.
