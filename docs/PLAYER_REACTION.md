# Player reaction states (+4 = 2)

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "player-reaction-states". This document records:
- which original routines run the player's +4 = 2 reaction states;
- how the stage workers 0021C440 and 0015D100 enter them (closing the
  FLOOR closure's known gap);
- what each state routine does;
- the translations, the evidence, the binding for the coordinator, and the
  limits.

Nothing here is wired into the live player yet. The FLOOR gate
(FIRST_CONTROL.md "FLOOR state closure") stays off until the coordinator
binds every worker.

Where things are:
- Code: `src/game/em_player_reaction.c` / `.h`.
- Oracle: `tools/test_player_reaction_reference.py`.
- Binding test: `tests/player_states_host_test.c` section 12.
- The stage workers 0021C440 / 0015D100 are translated by lane
  player-stage-workers (`em_player_stage_workers.c`, PLAYER_STAGE_WORKERS.md).
  They are described below only as evidence for the closure.

## 1. What the original does

### Entry: 0021C440 (every +4 = 1 and +4 = 2 stage)

0015B130 (the +4 = 1 handler) calls 0021C440 before its state dispatch and
skips the dispatch when it returns 1. 0015B770 (the +4 = 2 handler) calls it
before its own dispatch and ignores the result. The decomp C is
byte-matched. In order:

1. Health +220 <= 0: clear a nonzero +224 / +22C (each sets +0 = 2) and
   return 0.
2. +234 == 1 with +22C nonzero: clear +22C. With +F == 0 and +224 == 0, set
   +0 = 1 and return 0.
3. 0021BB00 (a +1F0 list): unless +F == 7, clear +224 and +22C, turn
   +0 & 2 into +0 = 1 and +F 2 into 0; return 0.
4. +F & 0x80 (already serviced): return 0.
5. Switch on the pending request code +F. Each entry sets +F |= 0x80:

   | +F | Effect | +5 store |
   |---|---|---|
   | 1 | 0021C270, +4 2 +5 0xC, +1F0 0x3E, +1F1 1 | 0021C5B0 |
   | 2 | +1F0 in 0021C440's list: nothing; 0021D640: 0021D6C0 (+5 0xA at 0021D7C8); else 0021C350, +5 0x10 | 0021C6BC |
   | 3 | 0021C270, +5 0xF, +1F1 1 | 0021C700 |
   | 4 | 0021C270 only | |
   | 5 | +220 = 0, +5 0xF, +1F1 1 | 0021C744 |
   | 6 | 0021C350; health <= 0: +5 3 (+234 1) or 1; else +5 0x11 | 0021C7E0 / 0021C7FC / 0021C81C |
   | 7 | +1F0 0x17: +5 4, +224 = +220, +302 0, sound 0x159; else +5 0x12, +220 = 0; then +0 = 2 | 0021C860 / 0021C89C |
   | 8 | 0021C270, 0021C350 | |
   | 9 | 0021C350 | |
   | 0xA / 0xB | +220 = 0, +5 0x13 / 0x14 | 0021C93C / 0021C978 |

6. Any other +F below 0x80 (0 included):
   - +1F0 0x3B: nothing. +1F0 0x3C: with +D 0 and +224 nonzero, 0021C350,
     001B61C0(0, 0xC0, 5, 1) and sound 0x152.
   - 0021D640: 0021D6C0.
   - D_0081083C set: +5 0xB, +1F0 0x3B, +1F1 1 (0021CA7C).
   - hit_a: +234 1, +0 1 and +23A 0x5B or 6, when the player is idle (+4 1
     +5 0), in +5 1, 0x21 or 0x22, or in 0x1D / 0x1E with +1F1 1.
     - +224 = 3.0 (5.0 on +23A 6 with +31E set), then 0021C350.
     - The 001F00A0 effect object (0x8000001B at +B0 / +C0). The 64 bytes
       at +D0 and the +250 value are copied into it.
   - hit_b: 0021C3F0 (not area 8 with D_00810701 2 and D_00810770 0xFF),
     +0 1 and +23B 0xA, with the same +5 test. +224 = 8.0, 0021C350 and
     001EFE00(0x80000044).
   - Both then enter one of:
     - on health <= 0, +5 3 (+234 1) or +5 1;
     - +5 0;
     - +5 0x17 from +5 0x1D / 0x1E, after turning +C4 toward the source
       (+20's +C0 / +C8 through 0011E620).

     +1F1 = 2 (hit_a) or 3 (hit_b).
   - try_c: a pending +224 or +22C outside 0021BC40's list.
     - 0021C350 (+F 0xC gives +1F1 4 and +F 0, else +1F1 0), then 0021C270
       (+1F1 1).
     - Health <= 0: +F 0x63 or +234 1 gives 0021C200 and +5 3, else +5 1.
     - Otherwise: +5 0, or 2 with +228 >= 100 and D_008106F1. From +4 1
       +5 0x1D / 0x1E it is 0x17 or 0x18, after turning toward the source.
7. Tail: health <= 35 sets +235 bit 0. Flag 0x80 runs 0017C370 (the
   0017B490 clip and +C4 + pi on +1F0 6 / 7, then the locomotion bytes
   cleared). Flag 1 clears +2F2, +274 and +276 and returns 1.

Two static stores cannot run, as the instructions show:
- 0021CC20, the +5 1 of hit_a's death with +234 != 1. hit_a is entered
  only with +234 1 (0021CA98), and nothing on the way changes it.
- 0021C604, case 2's +1F0 0x2C with +D != 2. 0021BB00 has already returned
  for that pair.

This round's first oracle, which executed 0021C440 before the translation
moved to lane player-stage-workers, reached every other instruction and
branch of 0021C440.

### 0015D100 (every +4 = 1 stage unless +0 & 2 or +20E counts down)

Byte-matched. It returns at once on 0021BB00. Otherwise:
- With +234 == 0, the room drain runs only when D_008106C8 & 4,
  D_008106C8 & 0x60 and D_00810C7E == 0. The +300 counter ticks to 0x168.
  Then health drops by 1.0, or at <= 1.0 it sets +0 = 2, +224 = 1.0 and
  +300 = -0x8000.
- With +234 != 0, the +2FC counter ticks to 0xF0. Health drops by 2.0 with
  001F0060(0x80000063, 0), or at <= 2.0 it sets +0 = 2, +224 = 2.0 and
  +F = 0x63.
- Either path, at health <= 35 with +235 bit 0 clear, sets the bit and calls
  0015C9D0.

It writes no +4 / +5. It only arms 0021C440 (+224, +F 0x63) for the next
stage.

### The states (0015B770's table)

Each routine dispatches on the sub-state +6. Most keep the tails 00179880
(drop) and 00175900(p, 1) (floor service); some also call 001764E0
(probes). "Root" means +38 = root8 - +21C, +21C = root8 (the root node's
forward translation, node 0 +8). "The vertical" means +2EC = root4 - +2E4,
+2E4 = root4, +B4 += +2EC.

| +5 | Routine | Summary | Exits |
|---|---|---|---|
| 0 / 0x17 | 0021D800 (NEARMISS; translated from the instructions) | 0: rumble, sound 0x153/0x152 by +1F1, rand & 1 picks the clip pair, 0021D1A0 picks front/back, sound 0x146/0x147 unless 0021D600. 1: on the clip end, +20E 0x5A/0x3C; +319 with +5 0x17 gives 0021D530; else drop and floor: landed gives 0017C540, airborne gives +4 1 +5 7 +1F0 0xD. Before the end: root, 00178B90(p, 1), drop and floor, +1F0 0x3E / 0xD. Every path then takes the +23A 0x5D tail (0021D250(p, 0)); the NEARMISS C returns early instead. | +4 1 +5 0/1 (0017C540), 7 (0021DAF8), 0x1C/0x1D/0x1E (0021D530); +4 2 +5 0x16 |
| 1 | 0021E240 | clip 0x5C/0x2A; sounds at +3C 80/50/16 (0x156, 00182870(p, 1), 0021D490 + rumble); on the clip end +6 2 runs 0021D2E0(p, 0x78, 1): a +7 countdown of 0x78 frames to 001AEDE0(4, 0), then parked | none (the fade) |
| 2 / 0x18 | 0021E490 (instruction words) | clip 0x1F/0x57, then at +3C 22 0021C120, then 0021C190 (the +31F countdown: 0015C1F0, +1C = 001EFE00(0x80000048), D_008106F0 0, 001FAFD0, D_008106F1 0), then on the clip end 0021D530 (+5 0x18) or 0017C540 | +4 1 +5 0/1/0x1C/0x1D/0x1E |
| 0xA | 00223C70 | entered from 0021D6C0. 0: rumble, sound 0x152 / 0x153 by +1F1; health <= 0 goes to 0x1E (+F 0x63 or +234 1) or 0xA, +228 >= 100 with D_008106F1 to 0x14. 1: clip 0x185 / 0x184 (+F & 2 or rand & 1). 2 / 0x17: on the clip end +20E 0x3C, +4 1 with +5 0x20 / 0x1F / 0x14 by D_00810E70 & 0x70003B7E / 0x70003B7C. 0xA..0xC: clip 0x187, then 0021D2E0(p, 0x78, 1). 0x14..0x16: clip 0x184, 0021C120 at +3C 30, 0021C190. 0x1E..0x21: clip 0x186, D_00275B08 = 1, root with the vertical clamped at -4, then drop and floor to 00182870(p, 1), sound 0x156, +4 2 +5 3 | +4 1 +5 0x14/0x1F/0x20; +4 2 +5 3 (00224244), 0x16 |
| 0xB | 0021F330 | 00174AC0 and 001754E0(p, 6) (an input count in +28); D_0081083C 0 gives 0017C540 and D_008106BC 0; a pending +224 runs 0021C350 (health <= 0: +4 2 +5 3/1, with no tail) and a pending +22C runs 0021C270 (to +6 0xA when +228 >= 100 with D_008106F1); +6 0xA..0xD go through 0021C120 / 0021C190 | +4 1 +5 0/1; +4 2 +5 1/3 (0021F530 / 0021F518) |
| 0xC | 0021F850 | clip 0x2E, D_008106BC 1, the +228 >= 100 branch through 0021C120 / 0021C190, clip 0x24, 0017C540 | +4 1 +5 0/1 |
| 0xF | 002202C0 | clip 0x32, the 001C61D0 / anim_clip_arbiter hand-over at frames - 30, root with the vertical, landing, the same branch, 0021D2E0(p, 0x78, 0) | +4 1 +5 0/1; +4 2 +5 0x16 |
| 0x10 | 0021DBB0 | sounds 0x152 + 0x148, 0021D1A0 picks clips, root x 0.45 with the vertical, landing, health <= 0 to 0xA..0xD and 0021D2E0 | +4 1 +5 0/1; +4 2 +5 0x16 (0021D250(p, 1) while +6 < 0xA) |
| 0x11 | 0021E9C0 | sound 0x154, clip 0x20, root, on the clip end +F 0, +20E 0x3C, 0017C540 | +4 1 +5 0/1 |
| 0x12 / 0x13 | 0021EAD0 | sound 0x159, three 001EFD90(0x80000023) effects at nodes 7/2/3 with (0, wrap(+C4 + pi), 0, 1), root x 0.75 / 0.5, landing, 0021D2E0 | none (the fade) |
| 0x14 | 0021EF30 | 0021EAD0 without the drop / floor tail in sub-states 4..7 | none (the fade) |

## 2. The closure

The FLOOR closure (FIRST_CONTROL.md, `kFloorStates` in em_player.c) now also
follows the stage workers:
- **0021C440** (and 0021D6C0) add +4 2 with +5 0, 0x17, 1, 2, 0x18, 0xA,
  0xB, 0xC, 0xF, 0x10, 0x11, 0x12, 0x13 and 0x14 (store addresses above).
  +5 3 (0021E830) and 4 (00221FC0) were already listed. These are owned by
  lane player-major2-states and are not translated here.
- **0015D100** writes no +4 / +5.
- **0015B530** (+4 4, bound whole). 00182DF0 writes +4 1 with +5 0, or 0xC
  when +1F0 is 0x17 (00182D40). 001838B0 runs 001662D0 and, if that left
  +4 1, writes +4 4 +5 0. 001837B0 writes +5 0, and 001796C0 writes 0x1C /
  5, both under +4 4. 00183910, 00162DB0 and 00163B40 write only pairs
  already listed.
- **The reaction states' exits** are all already in the closure: +4 1 +5 7,
  0x14, 0x1C..0x20, 0 / 1, and +4 2 +5 3 / 0x16.

Method: the +4/+5 byte stores of each routine and of every callee up to six
calls deep were listed from the user's split listing. Each value was
confirmed in the decomp C, or in the instructions for NEARMISS and
instruction-word files. Each pair was read from its block. The scan found
no pair outside the table.

## 3. Translations

`em_player_reaction.c` translates:
- the eleven states;
- the helpers 0017C540, 0021D530, 0021D250, 0021D2E0, 00179880, 00182870,
  0021D490, 0021C120, 0021C190, 0021D1A0, 0021D600, 001754E0 and 001B1470.

How they are written:
- **The actor.** Every routine works on the live record by its original
  offsets.
- **Floats.** Every EE float op (add.s / sub.s / mul.s / cvt.s.w / neg.s
  and the compares) goes through `em_ee_float.h`.
- **001B1470.** The module has its own translation of 001B1470 over
  `em_ee_float.h`. `em_player_sdk_wrap` in em_player_floor.c has no add/sub
  pre-trim (EE_FLOAT_MODEL.md section 5c).
- **Sources.**
  - For NEARMISS 0021D800 and 0021D530 the translation follows the
    instructions. It departs from the C at 0021D800's sub-state 1 tail (see
    the table).
  - For the instruction-word files 0021E490 and 0021D1A0 it follows the
    decoded instructions.
- **Workers.** Every callee not translated here is a worker in
  `EmPlayerReactionWorkers`:
  - 001749A0, anim_clip_arbiter and 001C61D0;
  - 001FBD50, 001B61C0, 00122BB8, 001EFD90 and 001EFE00;
  - 00175900, 00178B90, 001764E0, 00174AC0 and anim_eval_skeleton (with
    node 1's +C0 / +C8);
  - 001AEDE0, 0015C1F0 and 001FAFD0;
  - SDK 0011E620;
  - 0021C270 and 0021C350 (`w0021C270` / `w0021C350`, translated by lane
    player-stage-workers).
- **Faults.** A missing worker, or a negative return, faults (-1). The live
  adapters refuse before any write when any worker, the scene, its
  D_008106F1 pointer or the refresh is missing.

D_008106F1 is one byte shared by several owners:
- 0021C270 sets it;
- 0021C190 clears it;
- 0021C440 and the states read it (0021F330 reads it right after
  `w0021C270`).

So `EmPlayerReactionScene.d8106F1` is a pointer to that byte (the
canonical D_008106F1 the stage's `EmPlayerStageScene.d8106F1` also points
at), not a copy.

`em_player_reaction_w0021D2E0` / `_w0021D250` / `_w0021D490` /
`_w0021C120` / `_w0021C190` fit the worker signatures that lane
player-major2-states declares for the same originals (`EmPlayerMajor2Workers`),
with an `EmPlayerReaction` as context.

## 4. Verification

`python3 tools/test_player_reaction_reference.py` (make target below)
executes the original instructions in the shared EE interpreter of
test_player_slide_reference.py. That file is not edited. Its COP1 is
overridden here to use `tools/ee_float_model.py`, and VU0 macro ops fail.

What each case compares:
- all 0x320 actor bytes;
- D_008106F1, D_008106F0, D_008106BC and D_00275B08;
- the return value;
- every worker call, in order, with its arguments (float arguments as bit
  patterns);
- at entry to every hooked call, before the hook runs: all 0x320 actor bytes
  and the four scene bytes above, as the callee is handed them. The native
  side records the same at entry to every worker callback, from the case's
  own actor and scene. So a store moved from before a call to after it (or
  the reverse) fails even at offsets no hook mutates; for example 00178B90
  must see the +38/+21C root integration already stored. The default run
  compares 16,739 such call snapshots.

How the hooks are checked:
- Actor-taking hooks write scripted bytes into the actor on both sides, so
  every read after a call is tested.
- The 0021C270 hook also writes a scripted D_008106F1, which tests the read
  after it.
- The hooks assert the original's a0 (the actor) where the worker contract
  omits it.
- 001FBD50's a2 = 0 and f12 = 300.0 are asserted.
- Execution is confined to the translated routines, so an unlisted callee
  fails the test.

Coverage is asserted every run. All 3,334 statically reachable instructions
of the 11 states and 14 helpers run (jump-table targets included), and every
conditional branch goes both ways. No instruction had to be listed as
infeasible.

The default run takes about 5 s. It covers:
- 8,000 state cases and 400 helper cases;
- every 00182870 attribute;
- 001B1470;
- the adapter refusals.

`EM_TEST_FULL=1` runs 40,000 + 6,000 cases. Both pass.

Mutants that fail the default run:
- 0021D800 returning early after 0021D530 (the NEARMISS C);
- a host-float add in 00179880;
- `<` for 0021E490's `<= 22`;
- 0021F330's tail order;
- 0021DBB0's +236 step;
- round-to-nearest cvt.s.w;
- 002202C0 losing +21C;
- 0021EAD0's node order;
- 0021D2E0 without the +300 test;
- 0021F330 reading D_008106F1 before `w0021C270`, or +220 before
  `w0021C350`;
- 0021C190 not clearing D_008106F1;
- 0021E9C0 sub-state 1 calling `translate` (00178B90) before the +38/+21C
  root integration (caught only by the per-call snapshots: the actor
  differs at +38..+3B/+21C..+21F on entry to 00178B90);
- 0021E9C0 sub-state 0 storing +7 = 0 after the clip request (001749A0);
- 0021C190 storing the 001EFE00 handle in +1C, or D_008106F0 = 0, after
  001FAFD0 instead of before it.

`tests/player_states_host_test.c` section 12 (ASan/UBSan) binds 0021E9C0 as
`stage.state2[0x11]`. A stand-in `stage.reaction` makes 0021C440's 0021C81C
store. The test checks:
- the stage skips the dispatch on a stage 0015B130 owns;
- 0015B770 runs the state with its refresh, sound 0x154, rumble, clip 0x20
  and the real floor service (`player_states_floor_service`);
- root integration gives +38 = 1.5;
- the clip end hands back to +4 1 +5 0, and the port's idle takes over
  through stop phase 3;
- the adapter refuses without its scene.

## 5. Binding (coordinator)

- **The states.** Set `EmPlayerStatesBinding.stage.state2[i] =
  em_player_reaction_live_XXXXXXXX` with `state2_context[i] =` an
  `EmPlayerReaction`:
  - `[0]` and `[0x17]` 0021D800;
  - `[1]` 0021E240;
  - `[2]` and `[0x18]` 0021E490;
  - `[0xA]` 00223C70;
  - `[0xB]` 0021F330;
  - `[0xC]` 0021F850;
  - `[0xF]` 002202C0;
  - `[0x10]` 0021DBB0;
  - `[0x11]` 0021E9C0;
  - `[0x12]` and `[0x13]` 0021EAD0;
  - `[0x14]` 0021EF30.
- **The stage workers.** `stage.reaction` / `stage.drain` come from lane
  player-stage-workers (`em_player_stage_reaction` / `em_player_stage_drain`
  over its host; PLAYER_STAGE_WORKERS.md).
- **Workers** (all receive `workers.context`; wrap the ones whose owner
  needs another context):
  - `floor = player_states_floor_service` and `probes =
    player_states_wall_probes` (same signatures);
  - `w0021C270` / `w0021C350` call `em_player_0021C270` /
    `em_player_0021C350` with the stage-workers host;
  - `translate` (00178B90), `heading` (00174AC0), `request` / `arbiter` /
    `clip_frames` and `skeleton` come from the display / pose owner;
  - `sound`, `rumble`, `effect` and `attach` come from the audio / effect
    owners (001EFD90 is being written by another lane);
  - `fade = em_transition_fade_out` (001AEDE0, AREA_SCRIPT.md);
  - `model_refresh` (0015C1F0) and `stream_check` (001FAFD0) have no
    translation yet;
  - `atan2` is the SDK host model, as for the floor.
- **The scene.** `EmPlayerReaction.scene` is the binder's.
  - `refresh` fills the read fields before every call: root node +4 / +8,
    nodes 2 / 3 / 7 +C0, D_00810E70 / D_00810E74, 0x70003B76 / 7C / 7E /
    8D and D_0081083C.
  - `d8106F1` is the stage scene's `d8106F1` pointer (the canonical byte).
  - Copy D_008106F0, D_008106BC and D_00275B08 back to their owners after
    each call.
- **Lane player-major2-states.** Its `w0021D2E0` / `w0021D250` /
  `w0021D490` / `w0021C120` / `w0021C190` workers can bind
  `em_player_reaction_w*` with the same `EmPlayerReaction`.
- **The port's idle / walk.** The live layer does not run 0015B130 on the
  port's own idle/walk stages (`live_stage_dispatch` returns early for
  +4 1 +5 0/1). So 0021C440, 0015D100 and 0015D000 do not run there, and a
  reaction can start only from a stage the translated 0015B130 owns. To be
  faithful from idle/walk, the coordinator must:
  - run 0015B130's prelude, 0021C440, the +20E countdown, 0015D100 and
    0015D000 around the port's idle/walk callbacks (em_player.c, not this
    lane);
  - retire `player_damage_tick`'s copies, as FIRST_CONTROL.md already notes.
- **Terminal states.** 0021D2E0's countdown ends in 001AEDE0(4, 0), and the
  state then parks (+7 = 2) with +4 2. What follows is the scene's
  game-over flow (decomp FINDINGS "GAME OVER", trigger open). The area load
  then calls `player_states_reset()`.

## 6. Limits

- **Reachability in AREA11 is not decided.** All 15 route captures and both
  startup captures show:
  - health 100, +F 0, +224 = +22C = 0, +234 0;
  - D_0081083C 0, D_008106F1 0;
  - D_008106C8 0x20081910 (0x20089910 at the fence door). Bit 2 is clear,
    so 0015D100's room drain is inert at those points.
  - The grid census has no +23A 6 / 0x5B and no +23B 0xA node. Actor
    cells can carry other attributes, however.

  These are observations, not a proof that no hitter or hazard runs. The
  states are translated so the gate does not depend on the question.
- **Not translated here** (other lanes own them):
  - +5 3, 4, 5, 6, 7, 0x16 and 0x19 (0021E830, 00221FC0, 00222580,
    00222AD0, 002230A0, 00225570, 002255C0);
  - the stage workers and 0021C270 / 0021C350.
- **Untranslated workers:** 0015C1F0, 001FAFD0 and the display / effect
  workers listed above.
- **Scratchpad temporaries are not mirrored.** These are 0x70003A20 / 3A24
  (0021D1A0, 002202C0, 001754E0) and 0x700038A0 / 38B0 (the effect
  vectors, compared through the effect calls).
- **Host models.** atan2 (0011E620) is a host model on both sides, as in
  the floor and slide oracles.
