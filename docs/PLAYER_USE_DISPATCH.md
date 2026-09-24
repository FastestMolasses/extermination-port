# Player Use dispatcher (00160220, 001798D0, 0017C440)

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "use-dispatch". This document covers three routines, all byte-matched
in the decomp (the compiled C's text equals the original bytes):

- **00160220**, the Use dispatcher. The idle and walk states poll it; on a
  Use press it picks one of the player's Use actions.
- **001798D0**, the accepted-Use reset. The dispatcher (and the walk's
  +6 = 2 path) runs it when the interaction scan finds an owner.
- **0017C440**, the gait re-entry request. 0017C030 mode 4 calls it (the
  run-stop interruption, FIRST_CONTROL.md), and so do several state
  routines when they hand back to walking.

Census rows (FIRST_LEVEL_CENSUS.md): 00160220 was "verified-unbound, live
head only"; 001798D0 was "live" but the census section 7.2 sample found
the live version to be a stand-in (`player_pose_use_accepted` resets the
legacy locomotion fields); 0017C440 was "live" through
`em_player_motor.c` `em_player_reentry_*`, which reproduces only the
request's metadata (tier, blend, clip time), with the translation and clip
callees as boundaries. This lane translates all three routines whole.

The module is **live** (section 4).

## 1. What the original does

Every routine works on the 0x320-byte player record. Addresses in brackets
are the instructions translated there.

### 00160220: the Use dispatcher (returns 1 when an action took the press)

1. Unless D_00810E74 & 0x70003B76 is nonzero, return 0 [00160240].
   D_00810E74 is the pad block's pressed-this-frame word; 0x70003B76 is
   the configured Use bit (default 0x0040, CROSS in the libpad layout).
2. The interaction scan 00184BA0(p) [0016024C]. A winner: 001798D0(p),
   +5 = 0x25, +6 = 0, return 1. (+5 = 0x25 is an empty case in 0015B130;
   the winning owner's controller drives the player from there.)
3. Area 0x15 only (D_00810700 read here): 001AAC00(p, p + 0x290, p + 0x218)
   [00160294]. A nonzero result: +1F0 = 0x38 (result 1), or 0x39 with the
   halfword +2E = 0 (result 2) or 1 (result 3), any other nonzero result
   leaves +1F0; then +5 = 0x23, +6 = 0, +1F1 = 0, +0 |= 2, return 1.
4. The surface actions 0015D4C0(p) [00160318] (in AREA11 only its case
   0x32, the ladder columns, +5 = 0xB; PLAYER_LADDER_ENTRY.md). Nonzero:
   return 1.
5. The trigger boxes (D_00810700 read again, [00160334]). In area 1, 4 or
   0xD the dispatcher skips step 6 when the body position is inside a box.
   Each bound is inclusive: a box is left on v < lo or on !(v <= hi).

   | Area | y (+B4) | x (+B0) | z (+B8) |
   |---|---|---|---|
   | 1 | -40 .. -20 | -35 .. 35 | -1050 .. -990 |
   | 4 | 10 .. 20 | 315 .. 360 | 315 .. 385 |
   | 0xD, first | 150 .. 210 | 720 .. 800 | 800 .. 840 |
   | 0xD, second | 150 .. 215 | 635 .. 720 | 1270 .. 1325 |

   (The decomp C's earlier "720" for the first 0xD box's z floor was a
   decode typo; the instructions use 800, the decomp note says so.)
6. The ledge probes, when +236 == 0 and +23B != 0x35 [0016063C]:
   - yaw = +C4; build_trs_matrix(p + D0, p + B0, p + C0, p + 60);
     0015DF10(p, 0, +C4). Nonzero: return 1.
   - +C4 = 001B1470(yaw - pi/4) (the store precedes the matrix rebuild);
     build_trs_matrix; 0015DF10(p, 1, +C4). Nonzero: return 1.
   - +C4 = 001B1470(pi/4 + yaw); build_trs_matrix; 0015DF10(p, 1, +C4).
     Nonzero: return 1.
   - +C4 = yaw; build_trs_matrix.

   pi/4 is the single 0x3F490FDB. 0015DF10 enters the ledge climb (+5 = 2)
   or the vault (+5 = 3) itself (PLAYER_CLIMB_SLIDE.md).
7. When +236 == 0 and +23B != 0x35 (read again after step 6, which the
   probes may change) [00160740]: the running jump 0015EC50(p) [0016075C].
   Nonzero: return 1 (it wrote +5 = 6; PLAYER_RUNNING_JUMP.md).
8. The aim solver 0015FDF0(p) [00160778]. Nonzero: +5 = 0x24, +6 = 0,
   +1F0 = 0x3A, +0 = 3, return 1. Otherwise return 0.

### 001798D0: the accepted-Use reset

+38 = 0 (word), +21C = 0 (word), +25C = 0, then 00174A50(p, 0.0), then
+5 = 0, +6 = 0, +1F0 = 0.

### 0017C440(p, arg): the gait re-entry request

1. +25C = +23F - 1 (a byte: 0xFF when +23F is 0).
2. +38 = D_00248870[+25C] (read back from the byte; the table holds 0,
   0.1, 0.3, 0.8 for tiers 0..3 and other data after them), stored
   before the call below.
3. 00178B90(p, arg).
4. clip = (s16)0017B490(p, 1, +235, +25C).
5. 0x70003A20 = (float)001C61D0(+40, clip) (cvt.s.w of the integer).
6. anim_clip_arbiter(p, clip, 4.0, 0x70003A20 - 18.0) when +25C (read
   again) is 2, else with 0x70003A20 - 46.0.
7. +1F0 = 1.

## 2. The translation (`src/game/em_player_use_dispatch.c/.h`)

- `em_player_use_00160220(workers, actor, &result)` and
  `em_player_use_001798D0(workers, actor)` over an `EmPlayerUseWorkers`;
  `em_player_reentry_0017C440(workers, actor, arg)` over an
  `EmPlayerReentryWorkers`. All three work on `EmPlayerLiveActor.bytes`
  by original offset.
- Every callee is a worker: 00184BA0, 00174A50, 001AAC00, 0015D4C0,
  build_trs_matrix, 0015DF10, 001B1470, 0015EC50, 0015FDF0; and 00178B90,
  0017B490, 001C61D0, anim_clip_arbiter. The data read D_00248870[tier]
  is a worker too (`speed`), so the binder maps the address and faults on
  one it does not hold. It does not assume the table ends at tier 3.
- `EmPlayerUseScene` carries D_00810E74, 0x70003B76 and D_00810700. The
  dispatcher reads `area` again at each place the original loads it.
- The float work is `em_ee_float.h`: the trigger boxes use c.lt / c.le on
  the raw words, yaw - pi/4 and pi/4 + yaw are EE sub / add in the
  original operand order, 0x70003A20 is cvt.s.w, and the frame is EE sub.
- Fail-stop. Each entry point checks every worker it can reach and returns
  -1 before its first write when one is missing. The exception is
  001AAC00: it is reached only in area 0x15, so a NULL `classify` faults
  there (after the scan, before any record write). A worker returning a
  negative value stops the routine with -1 at once, leaving the writes
  made before that call.
- Adapters: `em_player_use_dispatch_worker` (the shape of
  `EmLocoWorkers.ladder`), `em_player_use_accepted_worker`
  (`EmLocoWorkers.use_accepted`) and `em_player_reentry_worker` (the
  `int (*)(void *, EmPlayerLiveActor *, int)` 0017C440 slots, e.g.
  `EmLocoWorkers.reentry`, `EmPlayerLandWorkers.reentry`). Their context
  is the worker struct itself. Where a slot's table shares one context
  across all its workers (EmLocoWorkers does), the binder wraps the call.

## 3. Verification (`tools/test_player_use_dispatch_reference.py`)

`make test-player-use-dispatch-reference` (target to be added by the
coordinator, section 5). The oracle executes the original instructions of
all three routines from the user's pinned ELF. Every callee is hooked,
scripted per case and recorded, and the native module gets the same script
through its workers. It asserts that the hooked set is exactly the set of
jal targets. It compares:

- all 0x320 record bytes, D_00810700 and 0x70003A20 at exit, and the
  return value;
- every worker call with its arguments (float arguments as raw bits), and
  with the record bytes, D_00810700 and 0x70003A20 at its entry, so a
  store moved across a call fails.

Scripted workers write record bytes (+5, +C4, the position, +236, +23B,
+25C, +235, +40, ...) and sometimes change D_00810700, so re-reads are
tested. COP1 goes through tools/ee_float_model.py.

Default run (about 1 s): 4,000 of 40,000 random cases, plus 53
deterministic trigger-box boundary cases. The boundary cases put each axis
of each box at lo, one ULP below lo, hi and one ULP above hi, with the
other axes inside. They also cover the hand-over from the first 0xD box to
the second. Every one of the 45 conditional branches of the three routines
goes both ways (asserted). About one case in eight injects a worker fault
at a random call: the native must return -1 with the record, area and
scratch exactly as the original had them at that call. 18 missing-worker
checks cover every worker, the scene and the scratch word, the lazy
001AAC00 fault in area 0x15, and its absence in AREA11.
`EM_TEST_FULL=1` runs all 40,000 (about 6 s; passed 2026-09-23 with 3,096
injected faults and 363 D_00248870 read faults).

Mutants that fail the default run:

- the area read once instead of re-read after the scan;
- c.le for a lower bound, and c.lt for an upper bound;
- each of the 24 box constants moved by one ULP in either direction (48
  mutants);
- the +C4 stores moved after the matrix rebuild (the sweep and the
  restore);
- 001798D0's +5 cleared before 00174A50;
- the second probe's mode 1 passed as 0;
- the running-jump gate without +236;
- the +0 |= 2 write dropped;
- 0017C440's +38 stored after 00178B90;
- the tier test taken from the clip instead of +25C read again.

`yaw + (-pi/4)` for `yaw - pi/4` survives: the EE model gives the same
bits for both.

`EM_TEST_WORLD=1` (about 7 minutes) runs the original player stage on
the captured RAM (`../Extermination/build/s87/route/`) twice: once as is,
and once with 00160220, 001798D0 and 0017C440 replaced by this module. The
native workers there call the original callees in the same EE. Every
frame must leave the whole RAM, the scratchpad and the player record
byte-identical between the two runs. The sound/effect calls must be
identical, and every frame must match its PCSX2 trace row. There are two
kinds of run:

- **Replayed beats.** The route replay (0015BCF0 with the pad unpack,
  FIRST_LEVEL_ROUTE.md) runs from the beat's source snapshot to its last
  row. The rows are checked with `route_row_check`.
- **Seeded presses.** Each Use press from idle runs from the row before
  the press, seeded with that row's feet, body and yaw, as the climb
  capture route in test_player_climb_reference.py does. It runs until +5
  is idle again, with the same bounds as that route.

Results (2026-09-23):

| Run | Frames | What the native dispatcher did |
|---|---|---|
| 05_boxes, replayed | 677 (677 rows) | 514 polls: 512 return 0; both Cross presses (f175, f393) climb (+5 = 2, +1F0 = 8) through 00184BA0 -> 0015D4C0 -> build_trs_matrix -> 0015DF10 |
| 12_crevice_jump, replayed | 345 (337 rows) | 227 polls: 226 return 0; the Cross press runs the three ledge probes (two wraps), then 0015EC50 enters the running jump (+5 = 6, +1F0 = 0xC). 0017C440 runs natively twice after the landing (translate, select, frames and arbiter each twice) |
| 05_boxes, seeded f175 and f393 | 80 each | the climb; state, +1F0 and clip exact, and the placement within the trace precision, every frame |
| 13_east_tower, seeded f438 | 94 | the high ledge climb; as above |
| 09_fence_door, seeded f309 | 1 | the scan wins the door: native 001798D0 (00174A50 with 0.0), +5 = 0x25; the press frame's state, +1F0 and clip exact |

Beat 13 cannot be replayed from its start: the original replay itself
leaves the trace at f133, where the capture's walk stops. The stage-only
replay has no owners or scripts. The owner presses of beats 01..04 (panel
and elevator) cannot be seeded: their source snapshots hold D_008106EF
(the use inhibit 00184BA0 tests) = 0x31. Only owners and scripts that the
stage-only run lacks clear it. For the owner press (09), only the press
frame's state is compared: the winning owner runs later in the same frame
and moves the player (the door's alignment), and it takes the player over
on the next frame. No native poll reached 0015FDF0 (each press was taken
earlier, and without a press the dispatcher returns at once), and no route
beat enters +5 = 0x23 or 0x24; the unit cases cover those paths.

## 4. Binding (live, census L04 / L09)

The module is in the game build. `em_player_closure_live.c` binds all three
routines over the live player record (`player_states_actor_mut()`), and
`player_states_bind_use_chain(1)` holds wherever the original collision
world is loaded (AREA11; em_player_stage_live.c). The player-states report
reads "Use chain (ledge climb, vault, ladder, running jump): engaged".

- **The Use hook.** `player_use_poll` (the port's idle / walk callbacks, which
  poll where 00161020 / 001612D0 do) calls `em_area11_interaction_host_use`.
  That calls `em_player_closure_live_use_press`, which is 00160220 over the
  record with every worker bound. When it takes the press (result 1), the
  callback returns at once with no floor tail. The instructions do the same
  (LOCOMOTION_DISPLAY.md "Where the instructions differ from the NEARMISS
  C"; with the tail, route 05's entry row would keep the crate as its +214).
  The record owns the player from then on. The port takes its placement
  first, because the ledge probes may have turned +C4 (the climb snaps it
  to the ledge normal: -0.0 and -1.5708 on route 05). It then runs
  `player_pose_use_accepted_port` (the port's locomotion and pose-source
  bookkeeping; no record write) and parks its own locomotion
  (em_player.c).
- **EmPlayerUseScene**: `d810E74` = `em_scene_state()->d810E74` (the pad
  block's pressed word). `spad3B76` is the pad config's Use word (001AF470
  config 0, 0x0040), and `area` = D_00810700.
- **scan (00184BA0)**: `em_area11_interaction_host_scan_00184BA0`
  (`em_interaction_scene_scan_checked` with the claim; no mask test and no
  acceptance, which are this module's), set with
  `em_player_closure_live_set_scan` when the roster spawns (em_scene_bindings.c)
  and cleared at the area build.
- **row_request (00174A50)**: `em_player_stage_row_request`.
- **classify (001AAC00)**: a fail-stop worker (area 0x15 only).
- **surface (0015D4C0)**: `em_player_ladder_0015D4C0` with the closure's
  ladder-entry workers. A ladder / ledge action record (attribute 0x20..0x3D)
  faults, because the EMCL export lacks its +0x34..+0x3F axis.
- **ledge (0015DF10)**: `em_player_climb_live_ledge`, with the shared
  0x70003A20 word synchronized around it.
- **jump / aim**: `em_player_running_jump_use_probe` / `_use_aim`.
- **0017C440**: bound as described in the closure (`reentry_speed` reads
  D_00248870 through the record pose's regions, which map the exported
  `assets/player_loco_tables.emrg`; `select` is `em_loco_0017B490`).
- **The scan winner's hand-off.** The dispatcher leaves +5 = 0x25, which is
  an empty case in 0015B130. The interaction runtime (the stand-in for the
  scripted takeover) consumes the next stage. At that admission the record
  gets the writes of 0015B130's prelude: +5 = 0, +6 = 0, +1F0 = 0x41 (route
  04 shows them on the frame after the scan). The stand-in consumes the
  stage in place of +4 = 4 (em_player.c live_major1).

**Evidence.** `make test-level-smoke`: the battery, refusal, panel and
elevator presses win the scan through the native dispatcher, and every row
check of those phases is unchanged. Phase `boxes` shows both Cross presses
at the crates entering the ledge climb. The climbs equal route 05 row for row
(LEVEL_SMOKE.md "boxes").

## 5. Makefile

The coordinator adds the test target (this lane does not edit the
Makefile):

```
.PHONY: test-player-use-dispatch-reference
test-player-use-dispatch-reference:
	python3 tools/test_player_use_dispatch_reference.py
```

## 6. Limits

- The port's own idle / walk callbacks still poll (00161020 / 001612D0 are
  translated but unbound, census L12). They poll at the positions the
  instructions do.
- The world replays run the original callees for every worker. They prove
  the dispatcher, the reset and the re-entry request in place. They do not
  prove the native translations of those callees; each has its own
  oracle.
- The world replay is the player stage alone (FIRST_LEVEL_ROUTE.md
  RouteReplay): no owners, scripts or camera stage run, so beats end
  before the first scripted takeover.
