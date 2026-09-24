# Player stage workers (0015BA50 / 0015B130 / 0015B770 callees)

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "player-stage-workers" (build lane `b5-player-stage-workers`). This
document records the workers that the player stage calls every frame, the
+4 = 4 handler, and the clip-rate data. It covers what each original routine
does, how the coordinator binds them, the evidence, and what is still open.
Code: `src/game/em_player_stage_workers.c/.h`. Exporter:
`tools/export_player_tables.py`. Oracle:
`tools/test_player_stage_workers_reference.py`. **Bound on the live player
stage since census L01 (2026-09-23)**: every player stage from first control
runs 0015BA50 / 0015B130 / 0015BCF0's tail with these workers
(`src/game/em_player_stage_live.c`, section 2.1). The workers that are not
translated or not live yet are fail-stop there.

## 1. What the originals do

All routines work on the raw 0x320-byte player record (`EmPlayerLiveActor`)
by their original offsets. The C cites the original address of every branch
and store.

| Stage slot | Original | What it does |
|---|---|---|
| `clip_rate` | D_00248C98 | The float at +8 of D_00248C90 row +20C. There are 459 rows, one per clip of the player's clip bank (the bank's count word is 459, and row 459 is all zeros). Only 4 distinct values: 1.0, 1.2, 1.4 and 0.8. 0015BA50 sets +34 = rate × +204. |
| `advance` | 001C64F0 anim_advance_time | Runs one iteration per whole or partial source frame of `step`. Each iteration resolves clip +2C against the bank word +40. Unless +2C has 0x8000, it ORs in the flags of the event whose frame equals float_to_int(+3C). If +3C <= 1.0, it wraps: with 0x8000 set, it clears the bit, sets +3C = frames − skeleton+8E and calls 001C8710; next == −2 gives 0x1000; next == −1 gives 0x3000, +3C = frames and 001C8710(0.0); otherwise +2C = next \| 0x8000, 0x4000, +3C = start, re-resolve and anim_sample_bones. If +3C > 1.0, it sets +3C −= step, calls 001C87C0(step), and adds 0x8000 while +2C carries it. The return value is a signed halfword, which 0015BA50 stores as a word in +200. |
| (helper) | 001281C0 float_to_int | libgcc `__fixsfsi` over the 001278C0 unpack. Exponent 0 gives 0 (denormals too) and NaN gives 0. Infinity and \|x\| >= 2^31 saturate by sign. Otherwise it truncates. |
| `commit` | 00183090 | Calls 001D0C70 when 0x70003B8F == 2. With +2F3 = 1 or 3, it calls bone_init_default_2(+1F2), sets +200 = 0 and +2F3 = 2 or 4, and returns 1. Any other nonzero +2F3 returns 1. With +2F3 = 0: an unchanged request (+1F2 == +20C) returns 1; otherwise it sets +20C = +1F2, calls anim_clip_init(+20C, +1F8, 0.0), sets +200 = 0 and returns 0. |
| `reaction` | 0021C440 | The damage and hit reaction; section 4 lists the states it enters. The early returns, which skip the tail, are: dead (+220 <= 0: pending +224/+22C cleared with +0 = 2); infected with only +22C pending; 0021BB00 (modes it leaves alone); and +F & 0x80. Hit codes +F 1..0xB each enter a +4 = 2 state. With no code, the order is: +1F0 0x3B, then 0x3C (+D 0 with damage: 0021C350, rumble 001B61C0(0, 0xC0, 5, 1), sound 0x152), then 0021D640/0021D6C0 (the hold reaction), then D_0081083C (2/0xB), then hit_a (infected on surface 0x5B/6: damage 3 or 5, effect 001F00A0 0x8000001B carrying the +D0 matrix and +250), then hit_b (surface mode 0xA, except area 8/2 with D_00810770 = 0xFF: damage 8, 001EFE00 0x80000044), and finally the pending +224/+22C (try_c, unless 0021BC40). The tail sets the +235 low-health latch at <= 35. s & 0x80 runs 0017C370 (the reaction clip and motion reset). s & 1 clears +2F2/+274/+276 and returns 1. |
| (callees) | 0021BB00, 0021BC40, 0021D640, 0021C3F0 | Leaf predicates on +1F0 (+4/+5/+6/+D) and on area bytes. |
| (callees) | 0021C350 / 0021C270 / 0021C200 / 0021D4E0 | 0021C350 applies +224 to +220 (latch at <= 35; dead: +220 = 0, +0 = 2). 0021C270 folds +22C into +228: at 100 it caps, clamps +220 to 60 and sets +234, D_00810707 and D_008106F1; it then calls 0021D4E0's effect 0x80000061/62 and sound 0x149. 0021C200 plays the infected death cue: sound 0x14D and 001EFE00 0x80000048. |
| (callees) | 0021D6C0, 0017C370, 001B1470 | 0021D6C0 is the hold reaction: it faces the +20 object (0x70003A20 = atan2(−o.C8, o.C0), +C4 = wrap(π/2 + that), +C0 = 0) and enters 2/0xA with mode 0x46. 0017C370: for modes 6/7 it requests clip 0017B490(p, +1F1 == 3 ? 2 : 4, +235, 0) with flags 1 and blend 0, turns +C4 by π and sets +1FC = 8. It then always clears +38, +240, +25C, +25E and +260..+268. 001B1470 wraps an angle into (−π, π]. |
| `drain` | 0015D100 | Returns when 0021BB00 holds. **Hazard-room arm** (not infected): only with 001B0070() & 4 (D_008106C8 bit 2), D_008106C8 & 0x60 and D_00810C7E == 0. Every 360 ticks of +300 it takes 1.0 from +220; at <= 1.0 it instead sets +0 = 2, +224 = 1.0 and +300 = −0x8000. **Infected**: every 240 ticks of +2FC it takes 2.0 and plays effect 0x80000063; at <= 2.0 it instead sets +0 = 2, +224 = 2.0 and +F = 0x63. Either arm sets the +235 latch once at <= 35 and calls 0015C9D0. |
| `heartbeat` | 0015D000 | Returns when +220 == 0. At <= 10, when the +210 counter is 0, it calls rumble 001B61C0(0, 0xE0, 4, 0), with period 0x3D. At <= 35 the rumble is 0xD0 with period 0x79. Above 35 it sets +210 = 0. |
| `scripted_check` | 00182B30 | 0 for +4/+5 = 5/1. Otherwise 1 when any of these holds: +220 <= 0, +25F, 0021BB00, D_008106F1, or +1F0 0x3C/0x3D. |
| `scripted_notify` | 00182D70 | Sets 0x70003B8F = 1 when it is 0. Clears +224, +22C, +F and +20E, and sets +0 = 1. Writes *(+1C)+4 = 2. Sets +1F2 = +20C, +1F8 = 0, +2F3 = 0, +1F4 = 1.0, +23F = 0, +38 = +240 = 0, +24C = −1 and +276 = 0. |
| `row_request` | 00174A50 | 001749A0(p, (short)0017B490(p, 0, +235, 0), 0, blend). |
| `stop_sound` | 0011A070 | Track = arg & 0x7FFF; hard = arg & 0x8000. The body is `em_sfx_driver_stop` (em_sfx_bank.c), which tools/test_area11_sfx_reference.py already verifies. |
| `major[4]` | 0015B530 | With 0x70003B8D == 0 it calls 00182DF0. Otherwise it switches on +5: 0 → 001837A0, 1 → 001837B0, 5 → 00162DB0, 8 → 00163B40, 0xC → 001838B0, 0x17 → 00183910, anything else → nothing. |

Every float operation goes through `em_ee_float.h`:
- 0021C350's sub.s and 0021C270's add.s (pre-trim);
- 001B1470's loops and the π/2 and π adds;
- 0015D100's subtractions;
- anim_advance_time's sub.s and cvt.s.w;
- every c.eq/c.lt/c.le, so a denormal compares equal to 0.

## 2. Binding (for the coordinator)

### 2.1 The live binding (census L01)

`em_player_stage_live_bind()` (em_player_stage_live.c) runs at every area
build, at 001AF5C0's position in the scene bindings' w_001AFCA0, right after
`player_states_reset()` (001AF5C0's wipe and 0015C420's record values). It
fills the host below and calls `player_states_bind` with the stage part of
`EmPlayerStatesBinding` only, so em_player.c engages the STAGE mechanism
(`EM_PLAYER_MECH_STAGE`) and leaves FLOOR and USE gated. A missing
`assets/player_clip_rates.emcr` latches a scene fault at 0x0015BA50.

| Slot | Bound to |
|---|---|
| `clip_rate` | D_00248C98 from the local export (`em_player_clip_rates_load`) |
| `advance` | the live display's 001C64F0: `player_pose_stage_advance(step)`, i.e. `em_player_pose_advance` on the pose host's source (the one live translation of anim_advance_time; census row 001C64F0). It returns the +200 flags, or 0 when a stand-in holds the source or the interaction runtime owns it. `em_player_stage_anim_advance` (the record-level translation) waits for the display lane. |
| `commit`, `reaction`, `drain`, `heartbeat`, `scripted_check`, `scripted_notify`, `row_request`, `stop_sound` | the translations (`em_player_stage_workers_bind`) |
| `major[4]` | `em_player_stage_0015B530` (bound but not reached while the interaction runtime owns the takeover; census verified-unbound): 001837A0 bound (the byte-matched C is empty); 00182DF0 (record side), 001837B0, 001838B0, 00183910 fail-stop (untranslated); 00162DB0 / 00163B40 fail-stop (FLOOR, L02) |
| `major[6]` | `em_player_stage_0015D460` with the live 001AEDE0 (`em_frame_fade_start_colour(1, a0, a1)`) |
| `major[1]` / `state[0]` / `state[1]` | set by em_player.c: 0015B130 behind the takeover stand-in (`live_major1`), and the port's idle/walk callbacks (`live_port_state`) until L12 |
| `takeover` | `player_pose_stage_hook()`: the AREA11 interaction runtime at 0015B130's prelude position (consumes the stage while it owns the player) |
| `load` | before every stage: D_008106C8 (request word C8), D_00810701, D_0081083C and D_00810C7E (canonical progress bytes; D_0081083C migrated by L01) and the D_00810707 pointer. D_00810770 is not canonical (L19): the load refuses area 8 room 2, the only place 0021C3F0 reads it |

Callees (`host.callees`):

| Worker | Bound to |
|---|---|
| `sound` | the live 001FBD50: `em_sfx_play_at(id, record +B0, radius)` |
| `sound_stop` | `em_sfx_stop_track(track, hard)` (em_sfx.c: T_STOP, or T_HALT for 0x8000) |
| `w001D0C70`, `bone_init`, `clip_init` | fail-stop (the +4 = 4 commit; reached only after the prelude) |
| `cue` (001B61C0) | fail-stop (untranslated; 0015D000 at health <= 35, 0021C440's 0x3C path) |
| `w001EFE00`, `w001F00A0`, `w001F0060` | fail-stop (the effect manager is not live, L26) |
| `atan2`, `link20` | fail-stop (hit facing; the port keeps no +20 handle, census 7.2) |
| `clip_lookup`, `request` | fail-stop (0017B490 / 001749A0 on the record: 00174A50 and 0017C370; L12) |
| `w0015C9D0` | fail-stop (untranslated; 0015D100's low-health latch) |
| `link1C` | fail-stop (+1C is 0 in every route capture; the port keeps no +1C object) |
| `clip_resolve`, `skeleton_frame`, `w001C8710`, `w001C87C0`, `sample_bones` | not bound (they serve only `em_player_stage_anim_advance`) |

A fail-stop worker reports the callee once, is counted
(`em_player_stage_live_faults`) and fails the stage, which em_player.c turns
into `em_frame_request_quit`. Each is reached only by a hit, pending damage or
infection, a low-health latch, the +4 = 4 takeover or the area-8 room-2 hit
gate; every route capture (the playable image and beats 00..15) has health
100, +F 0, +224 = +22C = 0, +234 0, D_0081083C 0, D_008106C8 & 4 clear and
+1C 0, and the port has no AREA11 damage producer.

**The vitals.** The record's +220, +224, +228, +22C, +234 and +20E are a
per-stage view of the port's storage (g.status.health / infection,
g.pd_pend_hp / pd_pend_inf, g.pd_infected, g.pd_iframes): em_player.c loads
them before 0015BA50 and stores them (and +235 bit 0 into g.pd_low) after
0015BCF0's tail. The port's hit mailbox (em_enemy) is mapped onto the
pending floats before the stage (em_player_frame.c player_hit_mailbox).

**The takeover.** While the interaction runtime owns the player it consumes
the stage at the prelude position (its acquire stands in for 00174A50 +
00182D70 on the display, its tick for the +4 = 4 commit and advance, its
release for 00182DF0); 0015BA50's begin / end and 0015BCF0's writes still
run. On the port's idle/walk under 0x70003B8D without that owner (the
area-change fade after 001B0C60) 0015B130 does not run: the prelude would
request 00174A50, whose 0017B490 row lookup is not bound (L12), so the
port's callbacks keep those stages. The bound prelude workers (00182B30,
00182D70) and the +4 = 4 handler (0015B530, 001837A0) are therefore not
reached in the live app, although the original runs them on every scripted
takeover (12 of the 19 census labels); the census keeps them
verified-unbound until the takeover moves onto the stage.

**Known regression for L02.** Outside AREA11 a port enemy hit (em_enemy.c
`s.player_hit`, mapped onto +224 / +22C) now reaches 0021C440's +4 = 2
reaction states, which are not bound, so the stage fails and the app quits
where the legacy flinch used to play. No AREA11 owner posts a hit, so the
first-level route is unaffected; binding the +4 = 2 reaction states (L02)
restores hit behaviour.

### 2.2 The contract

**The workers.** Fill an `EmPlayerStageHost` and call
`em_player_stage_workers_bind(&binding.stage, &host)`. That call sets
`stage.context = &host` and fills `clip_rate`, `advance`, `commit`,
`reaction`, `drain`, `heartbeat`, `scripted_check`, `scripted_notify`,
`row_request` and `stop_sound`. The callbacks are left as they are.

**The +4 = 4 handler.** Set `stage.major[4] = em_player_stage_0015B530` and
`stage.major_context[4] = &major4`. The `EmPlayerStageMajor4 major4` has:
- `stage` = the same `EmPlayerStageScene`;
- `routine[]` = 00182DF0, 001837A0, 001837B0, 00162DB0 (the state-5 fall
  routine), 00163B40 (state 8), 001838B0 and 00183910, each with its own
  context.

All seven are required. 00162DB0 and 00163B40 are FLOOR-closure state
routines that another lane is translating (`em_player_fall.h`:
`em_player_fall_state5` / `_state8`; not checked here). Bind them here with
their own contexts. The other five (00182DF0,
001837A0, 001837B0, 001838B0, 00183910) are untranslated.

**The host.**

- `host.stage` must point at the **same** `EmPlayerStageScene` that
  `player_states_*` passes to `em_player_stage_begin` / `EmPlayerStage`, so
  0x70003B8D, 0x70003B8F and D_00810700 have one per-stage view (00182D70
  writes 0x70003B8F, and `player_states_stage_end` writes it back). Its
  `d8106F1` and `d810CB6` are pointers at the canonical bytes (em_player.c
  `live_scene_load` sets them from `em_scene_req_at(s, 0x008106F1u)` and
  `em_scene_progress_at(s, 0x00810CB6u, 1)`); 0021C270 writes D_008106F1
  through that pointer.
- `host.globals` (`EmPlayerStageGlobals`): D_008106C8 (the word 001B0070
  returns), D_00810701, D_00810770, D_0081083C and D_00810C7E, plus the
  0x70003A20 scratch word, as a per-stage view the binder loads before the
  stage; `d810707` is a pointer at the canonical progress byte D_00810707
  (`em_scene_progress_at(s, 0x00810707u, 1)`), which 0021C270 sets to 1.
  A host without the D_008106F1 or D_00810707 pointer is not ready: every
  worker refuses (−1) before its first write.
- `host.rates`: load with `em_player_clip_rates_load(&rates,
  EM_PLAYER_CLIP_RATE_PATH)`. The file is `assets/player_clip_rates.emcr`,
  written by `python3 tools/export_player_tables.py` from the user's ELF,
  ignored and never committed. The worker faults on a clip outside 0..458,
  which the original would read as unrelated data.

**The callees** (`host.callees`, one context). Every one is required: a
routine faults (−1) before its first write when any worker it can reach is
NULL.

| Worker | Original | Binder notes |
|---|---|---|
| `w001D0C70` | 001D0C70() | Called only when 0x70003B8F == 2 (commit). |
| `bone_init`, `clip_init` | bone_init_default_2 001C63E0, anim_clip_init 001C67E0 | The player display's clip start. |
| `clip_resolve` | anim_clip_resolve 001C8480 | Returns the clip header fields for (bank word +40, clip +2C including 0x8000): frames (+2, u16), next (+4), start (+6), the event table (+0x14 offset, s16 count, {frame, flags} pairs). It is **stateful** in the original: it also sets D_00275BF4/BF0/BEC, which the sampler workers below read. Its binder must therefore keep "the resolved clip" for them. The player bank is disc file chunk28 f01 (`extract/chunk28/f01_id3c.bin`, 459 clips). The port's EMPC export keeps frames/next/start but asserts that no clip has events. |
| `skeleton_frame` | *(short *)(*(p+110) + 0x8E) | The skeleton record's hold frame, read on a 0x8000 wrap. |
| `w001C8710`, `w001C87C0`, `sample_bones` | 001C8710 / 001C87C0 / anim_sample_bones 001C8D50 (on p+110, +C) | The display's channel seed, step and sample. |
| `sound` | 001FBD50(p, id, 0, 300.0) | ids 0x149, 0x14D, 0x152, 0x159. |
| `cue` | 001B61C0(big, small, dur, force) | Pad rumble: (0, 0xC0, 5, 1), (0, 0xE0, 4, 0), (0, 0xD0, 4, 0). |
| `w001EFE00` | 001EFE00(id, p) | 0x80000044, 0x80000048. |
| `w001F00A0` | 001F00A0(0x8000001B, p+B0, p+C0, 0) | Returns the effect record (≥ 0x110 bytes, original layout) or NULL. The caller writes +D0..+10F (the player's +D0 matrix) and +104 = +250. |
| `w001F0060` | 001F0060(id, 0) | 0x80000061/62 (0021D4E0), 0x80000063 (drain). |
| `atan2` | SDK 0011E620 | The same binding as the floor workers' atan2: an adapter over the SDK translation (`em_sdk_math_original_0011E620`, SDK-math lane) or `em_director_original_0011E620`. |
| `link20` | the +C0/+C8 words of *(p+20) | The object the player faces in 0021D6C0 and on hits in state 0x1D/0x1E. |
| `clip_lookup`, `request` | 0017B490, 001749A0 | The same row lookup and request the reversal and climb lanes bind. |
| `w0015C9D0` | 0015C9D0(p) | Clip re-trigger (001749A0 requests only, no state writes). Untranslated. |
| `link1C` | *(u8 *)(*(p+1C) + 4) = 2 | The linked object's major byte (00182D70). |
| `sound_stop` | em_sfx_driver_stop(track, hard) | `em_sfx.c` has no public stop-by-track call: its `loop_stop` (T_STOP hand-off) is static. The binder needs a public wrapper for it in em_sfx.c (owned elsewhere). |

**Shared with the +4 = 2 lanes.** `em_player_0021C200`, `em_player_0021C270`,
`em_player_0021C350` and `em_player_0021D4E0` take `(void *host,
EmPlayerLiveActor *)`, the signature of `EmPlayerMajor2Workers.w0021C200 /
w0021C270 / w0021C350` (em_player_major2.h). The reaction-state lane
(em_player_reaction.h) calls the same originals. They cast their context
to the stage host, and EmPlayerMajor2Workers has a single `context` for
all its workers, so the coordinator needs a one-line adapter from the
Major2 context to the stage host; they cannot be bound directly.
D_008106F1 / D_00810707 have one storage (HK, lead decision D2): the stage
scene, the stage globals, EmPlayerMajor2Scene (`d8106F1`, `d810707`) and
EmPlayerRecoveryScene (`d8106F1`) hold pointers, and the binder points all
of them at the canonical bytes, so a value 0021C270 stores in the middle of
a routine is what the rest of that routine and the next reader see.

**Retire the stand-ins when bound.** `player_damage_tick` (em_player_damage.c)
models 0021C440 / 0015D100 for the port's own idle/walk. FIRST_CONTROL.md
says it must stop on stages that the translated 0015B130 owns.

## 3. Verification

`python3 tools/test_player_stage_workers_reference.py` (default about 6 s of
CPU; `EM_TEST_FULL=1` about 95 s of CPU).

- **The interpreter.** The shared interpreter is wrapped, never edited.
  Every COP1 op (add/sub/mul/div/neg/mov/cvt/compares/acc forms) goes
  through `tools/ee_float_model.py`. The file's own fetch loop records every
  executed original instruction and adds branch-likely forms, mult/mflo,
  movn/movz, variable shifts, dsll and ld/sd.
- **The comparison.** Every case compares:
  - all 0x320 record bytes;
  - the stage scene (0x70003B8D/8F, D_00810700, D_008106F1, D_00810CB6,
    D_008106B3);
  - the globals, including 0x70003A20;
  - the 001F00A0 effect record and the *(+1C)+4 byte;
  - the return value;
  - the callee sequence, with every argument and float bit pattern.
- **What runs.** Originals run unhooked: 0021C440 with all its own callees
  (including copy_qw4), 0015D100 with 001B0070, 0015D000, 00183090, 00182B30,
  00182D70, 00174A50, anim_advance_time with float_to_int and 001278C0,
  0015B530 and 0011A070. Only the callees in the table above are hooked.
- **Synthetic sweeps.** Reaction 700, commit 60, drain 150, heartbeat 100,
  scripted_check 60, notify 20, row 20 and advance 260 cases; `EM_TEST_FULL=1`
  runs 12,000 / 1,500 / 3,000 / 1,500 / 1,500 / 600 / 600 / 4,000.
- **0021C440 scenarios.** 135 deterministic ones run in both modes: every
  path class, plus 48 operand pairs whose exact result needs rounding (sub.s
  / add.s).
- **Composition with the stage.** The original 0015BA50, 0015B130 and
  0015B770 run with only the state routines and the external callees
  hooked. The native side runs `em_player_stage_begin/dispatch/end`,
  `em_player_stage_0015B130` and `_0015B770` from em_player_floor.c, with
  this module bound through `em_player_stage_workers_bind` and major[4] =
  `em_player_stage_0015B530`. Cases: 120 / 160 / 40 by default and 3,000 /
  3,000 / 1,000 in the full run. This proves the binding and the clip-rate /
  advance / commit composition.
- **Captured RAM.** The player record at 0x8102B0 in `playable_ee.bin` and
  the route captures (05_boxes, 06_hill_slide and 08_truck_crossing by
  default; all 15 beats in the full run). The cases are the record as
  captured, every hit code, pending damage and infection, hit_a/hit_b
  setups, drain counters and three advance steps. On these, anim_clip_resolve
  and 001C6120 run over the real player bank, with real headers and event
  tables. The +20, +40 and +110 words point into the capture. That is 116
  cases on 4 images by default and 464 on 16 in the full run.
- **Helpers alone.**
  - float_to_int and 001B1470 over boundary patterns (NaN with and without
    fraction bit 20, ±Inf, ±2^31, denormals, ±π) and random bits: 832 by
    default, 24,032 in the full run.
  - 0021BB00/0021BC40/0021D640 over every +1F0 value with 9 byte contexts
    (6,912 cases).
  - 0015B530: 27 cases.
- **0011A070's decode.** The original frees track arg & 0x7FFF through
  00121A28 and sends command 3 to the track's kind-2 voice only when
  arg & 0x8000. The forwarder hands the same (track, hard) to the stop
  worker (16 cases).
- **The clip-rate data.** The test runs the exporter into the lane folder
  and loads the output with `em_player_clip_rates_load`. All 459 rows must
  equal D_00248C98 bit for bit. Clips −1, 459 and 0x7FFF must fault, and
  truncated, extended, bad-magic and wrong-count files must be refused.
  Reported but not asserted: on every capture, +34 equals rate[+20C]
  (4/4 by default, 16/16 in the full run).
- **Fail-stop.** For every routine and every worker it can reach, a NULL
  worker must give −1 with no record, scene, global or callee change. The
  same holds for a NULL stage scene or globals pointer (39 checks).
- **Coverage (asserted).** Every reachable instruction of the 23 executed
  original functions must run in the default run: 1,935 instructions. The
  only exclusions are 20 value-dead instructions, each listed with its
  reason in `DEAD_BY_VALUE`:
  - anim_advance_time's unsigned fix-up after `lhu` (never negative);
  - 0021C440 hit_a's non-infected dead exit (hit_a requires +234 == 1);
  - 0021C440 hit code 2 with mode 0x2C and +D != 2 (0021BB00 returns first).
- **Mutants.** 25 hand mutants were all killed (run from a scratch harness,
  not committed):
  - constants: the 35 latch, the 60 clamp, 0x168, 0x3D, 31 (float_to_int),
    0x3000, 5.0 (hit_a), π/2 (facing), the hit_b effect id and the +104
    source word;
  - compares: `le` against `lt` in 001B1470, the 0x3D mode of 00182B30 and
    the 0x18 gate of try_c;
  - float rounding: RN subtraction in 0021C350, and 0021C270's add without
    the pre-trim;
  - order and stores: the 0x3C rumble/sound order, a dropped +C0, +276 or
    +24C store, the +2F3 step and the event-loop break;
  - reads: the +8E sign and the D_00810C7E test;
  - routing: 0015B530's 0x17 route and the 0011A070 decode.

Build: the module and its live binder (em_player_stage_live.c) are in the
Makefile's COMMON list since L01. The live binding is proven by the level
smoke (its tick log is byte-identical to the pre-L01 build over the six live
phases), by `EM_STARTUP_TEST=newgame-control` (30 ticks, 9.599989, the
EM_FRAME_TRACE byte-identical to the pre-L01 build) and by
`tests/player_states_host_test.c` section 13 (the takeover stand-in, the
prelude gate and the vitals view).

## 4. What 0021C440 enters (FLOOR closure input)

0021C440 runs at the start of every 0015B130 and 0015B770 stage, so these
+4 = 2 states can follow any FLOOR state:

| Path | +5 (+1F0) | 0015B770 routine |
|---|---|---|
| +F 1 | 0xC (0x3E) | 0021F850 |
| +F 2, 3/5, 6, 7, 0xA, 0xB | 0x10, 0xF, 0x11 or dead 3/1, 4 (mode 0x17) or 0x12, 0x13, 0x14 | 0021DBB0, 002202C0, 0021E9C0 / 0021E830 / 0021E240, 00221FC0 / 0021EAD0, 0021EAD0, 0021EF30 |
| D_0081083C | 0xB (0x3B) | 0021F330 |
| hit_a / hit_b / try_c | 0 or 0x17, 2 or 0x18, dead 3 / 1 | 0021D800, 0021E490, 0021E830 / 0021E240 |
| **0021D6C0** (mode 0x27/0x34/0x35, or 0x33 on 1/0x1F..0x20) | **0xA (0x46)** | **00223C70** |

+5 = 0xA (00223C70) is **not** in FIRST_CONTROL.md's "Known gap" list. It is
reached from any hit or pending damage while +1F0 is 0x27, 0x34 or 0x35, or
0x33 in state 1/0x1F..0x20. It must be added before the gate opens, unless
original evidence shows those modes cannot occur with pending damage in
AREA11.

The other stage workers enter no state:
- 0015D100 and 0015D000 write only +0 / +224 / +F, which the next stage's
  0021C440 consumes;
- 0015C9D0 only requests clips;
- 00182B30 and 00182D70 write no +4/+5.

0015B530's seven routines (00182DF0, 001837A0, 001837B0, 001838B0, 00183910
and the two state routines) have not been followed. They are the remaining
part of the gap.

## 5. Limits

- **Bound with fail-stop workers (L01, section 2.1).** Still missing on the
  live path:
  - the record-level advance (a clip-header/resolve binder over the player
    bank, stateful as described in section 2.2, and the display sampler
    workers; display lane);
  - the effect and rumble binders;
  - the two object links (+20, +1C);
  - 0015C9D0, 00182DF0's record side, 001837B0, 001838B0 and 00183910,
    which are untranslated;
  - 0017B490 / 001749A0 on the record (L12).
- **Bound but unreached (L01).** 00182B30, 00182D70, 0015B530 and 001837A0
  wait for the scripted takeover to move from the interaction runtime onto
  the stage (00182DF0's record side, the display's commit/advance).
- **Hits outside AREA11 (L02).** Port enemy hits fault in the unbound
  +4 = 2 reaction states (section 2.1).
- **Scripted hooks.** atan2 is the host model on both sides, as in the floor
  oracle. SDK 0011E620 fidelity belongs to the SDK-math lane.
- **Captured coverage.** The captured records exercise the paths the route
  reached. The captures contain no hit in progress, so the rare hit
  paths are proven on synthetic records only.
- **Non-terminating inputs.** The original's 001B1470 loops never end on
  NaN/Inf or huge angles; so does the translation. The test keeps its
  inputs finite for that routine.
