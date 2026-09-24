# Player recovery helpers (fall, slide, climb and hang support routines)

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "player-recovery-helpers". This lane translated the helpers that the
fall, slide, climb, running-jump and hang states call. Before this lane, the
slide and climb modules faulted on them (00178B90, 00224B80), and so did the
FLOOR closure's +5 = 4 entry (0017C860 -> 00162A40). Code:
`src/game/em_player_recovery.c/.h`. Oracle:
`tools/test_player_recovery_reference.py`. Nothing here is wired into the
live player yet. Section 3 is the binding for the coordinator.

## 1. What each original does

All offsets are in the player record (EmPlayerLiveActor, 0x320 bytes).
"Read from" names the source the translation was written from. Where a
NEARMISS or asm-word file exists, that was the split listing
(`../Extermination/build/asm/matchings/main/code/func_*.s`). The decomp C was
checked against it.

| Routine | Size | Read from | What it does |
|---|---|---|---|
| 00178B90(p, probe) | 0x330 | asm-word listing | Moves +B0/+B8 along the yaw +C4 by +38: +B0 += s·+38·sin(+C4), +B8 += s·+38·cos(+C4). The scale s is 1.0, or on a 0x35 surface (+23B, with +25F == 0 and \|+310 − +C4\| < π/2) cos(+9C) + \|+310 − +C4\|·(1 − cos(+9C))/(π/2). With +25F != 0 the scale is not applied. If \|+38\| >= 4.5 the move is split into float_to_int(+38/±4) steps of ±4 plus the remainder. Each step runs 001764E0 when `probe` != 0. |
| 00178EC0(p) | 0x144 | C (byte-matched form) | For +24C = 2 or 3: local (±+38·D_00248730[+23F], 0, 0, 0) through the +D0 matrix (001026A0). The x and z of the result are added to +B0/+B8. D_00248730 = {0, 0.1, 0.25, 0.5}. |
| 001751A0(p) | 0x1E4 | NEARMISS; listing | Returns if spad 0x70003B8D is set. Otherwise +23F = D_00810E57; with gait 0, +24C = −1. Else +244 = cos(π·E64/256), +248 = cos(π·E65/256), r = atan2(−(+248), +244). The relative heading is wrap(wrap(π + r + D_008106A0) − +C4), stored at 0x70003A20; its magnitude goes to 0x70003A24. +24C = 0 (magnitude ≤ π/4), 1 (≥ 3π/4), else 2 (negative) or 3. |
| 002243F0(p) | 0x204 | asm-word listing | Sub-state +7. 0: when +224 or +22C is non-zero, it plays 0x152 + 0021C350 and/or 0x153 + 0021C270, then +7++, 001B61C0(0, 0xC0, 5, 1) and clip 0x6F; returns 0 when both are zero, else 1. 1: on +200 & 0x1000, +7 = 0, +20E = 0x3C, clip 0x6C (+25C < 2) or 0x6B at frame count/2 through anim_clip_arbiter (count cast at 0x70003A20). |
| 00224B80(p) | 0x458 | C (file records objdiff 100%) and listing | The slide's hit sub-state machine on +7 (0, 1, 2, 0xA, 0xB, 0x14..0x18, 0x1E). It returns 0 (no pending hit), 1, or 2 (0xB after clip end: +6 = 0x1E, 0021D490; 0x1E: +4 = 2, +5 = 3, +6 = 0, +1F0 = 0x3F). |
| 0017D080(p) | 0x780 | NEARMISS; listing | The ledge catch of the fall (00162DB0). Gated by +220 > 0 and (+228 < 100 or !D_008106F1). The steps: a probe 8 behind (0019AD00, the & 6 kinds); a class-0x2000 face (0x46 only when the owner passes 0017D040); the ledge frame 00177510, facing within 0.6π of reverse (the soft-float compare 00128350/001000C0); a second probe 8 further back, whose heading must stay within 0.349; a column table 0.5 behind the face whose first entry lies below +B4 − 20.5; then (area 0x11 within 115 of (340, 270): 3.0, else 0.5) a second column table. Walking it from the top: flag bit 0, attribute != 0x46 (unless the owner passed), aux < 0.6283, \|h − +B4\| ≤ 2.8, 001776E0 clear (else next), 00177CF0 and 00177B80 clear (else fail), then a 20.5 drop segment 0019A570 1.5 in front clear (else fail). On success: +B0/+B8 = the face point, +C4 = wrap(heading − π), +290/+298 = point + 1.5·normal, +294 = h; returns 1. |
| 0017C860(p, reach) | 0x7DC | NEARMISS; listing | The ledge grab ahead (called by 001639E0 with +2EC and by 001634A0 with +2E4). Same +220/+228 gate. With a hit 18 up / 5.5 ahead (face class 0x2000, not 0x46), the frame is 00177510, and a second probe 4.01 up must not be more than 0.5 nearer. The column table is 1 behind the face; entries top-down: flag, attribute, aux, 001775E0(p, 0) clear, top = +B4 + 20.5 ≤ h ≤ top − reach, 001776E0 and 00177CF0(h − 1) clear. With no hit at 18, the probe is 4.01 up: 001775E0(p, 1), \|h − +B4\| ≤ 0.5 and a 0019AFE0 sweep from (+B0, h, +B8) along the ledge matrix's (0, 4.01, 9.5) clear. On success: +2E0/+2E8 = point + 1.5·normal, +2E4 = h − 20.5 (high) or h (low), +254 = h, +218 = heading, +25F = 1, **+5 = 4**, +6 = 0, +1F0 = 9, +1F1 = 1 (high) / 0 (low); returns 1. |
| 00162A40(p) | 0x36C | NEARMISS; listing | The +5 = 4 callback, on +6. 0: place at +2E0.., +C4 = +218; +1F1 == 1 selects clip 0x7A (+6 = 1, +2E = 0), else clip 0x7D, +6 = 0xA, +25F = 0 and 001764E0. 1: 0017F320 clear, else +6 = 3; on !(+200 & 0x8000) +6 = 2 and sound 0xFF. 2: 0017F320, else on clip end +5 = 9, +6 = 0, +1F0 = 0x10, +D = 0 and clip 00188550 at 16. 3: +2F4 = +B4, +5 = 7, +6 = 0, +1F0 = 0xD, +2EC = −0.2. 0xA: on clip end, anim_eval_skeleton, +B0..+BC = node 1 +C0..CC, +B4 −= 11, clip 0x8C, and 001760C0(p, p+B0, 1, 18) sets +236 = 1, +235 \|= 2; then +6++. 0xB: 00174AC0(p, 0); gait ≥ 2 gives +6++ and 0017C440(p, 1); else +25C = 0, 0017C540 and 001764E0. Then +B4 −= 0.2 and 00175900(p, 1); with no floor, +2F4 = +B4 and +5 = 7 / +6 = 0 / +1F0 = 0xD. 0xC: 00178B90(p, 1), 0017C540 unless +200 & 0x8000, +B4 −= 0.2, 00175900(p, 1), 001796C0. |

Findings:

- The earlier label on 0017C860 in the decomp C ("enemy melee/ranged scan")
  is wrong. The instructions are the player's ledge grab into +5 = 4, which
  FIRST_CONTROL.md's closure already had.
- 00178B90's `probe` argument selects the per-step wall probes. The walk
  callback 001612D0 passes 0; the slide (sub-states 0xB/0xC/0x15), the climb
  and 00162A40 pass 1.

## 2. Translations

`em_player_recovery_translate`, `_strafe`, `_stick_quadrant`,
`_react_002243F0`, `_react_00224B80`, `_ledge_catch`, `_ledge_grab` and
`_hang_entry` translate the eight routines, in the order of section 1. Each
one:

- **Data.** Works on EmPlayerLiveActor `bytes` by the original offsets. It
  never touches +214/+308.
- **Arithmetic.** Every float is a raw binary32 word, run through
  `em_ee_float.h` in the instruction's operand order: EE add/sub/mul/div,
  compares, cvt, mula+madd, and the VU0 forms (vmulax, vmadday, vmaddaz,
  vmaddw, vadd.xyzw) for 001026A0/001028B8.
- **Leaves.** The pure leaves are translated inline and checked
  instruction-for-instruction by the oracle, which runs them unhooked:
  - 0011DF78 fabs;
  - float_to_int 001281C0 (fp-bit fixsfsi: NaN and zero give 0, ±∞ and
    exponents ≥ 31 saturate, else truncation);
  - 001B1470 wrap;
  - 001026A0, 001028B8, 00102948;
  - 0017D040;
  - 00128350 + 001000C0. The fp-bit double compare reduces exactly to
    `x < 0x3FF1463A` for non-NaN non-negative x; NaN gives "not less".
- **Workers.** Every other callee is an explicit worker in
  `EmPlayerRecoveryWorkers` (section 3).
  - A routine checks every worker it can reach before any write. A missing
    one returns −1 with nothing written.
  - A worker returning < 0 stops the routine with −1. The writes made before
    the call stay, exactly as the original had them at that call; the oracle
    checks this at every call.
- **Two fail-stops that stand in for hangs.**
  - 001B1470 loops for ever when a step of 2π leaves the angle unchanged
    (±∞, NaN, and magnitudes where the pre-trimmed 2π vanishes). The
    translation faults at that fixed point instead.
  - A column table with more than 16 entries faults, because the original's
    scratchpad arrays alias one another past 16 (as in em_player_climb.c).
- **Scratch words.** The words the original leaves in the scratchpad
  (0x70003A20/24/28/2C) are the in/out `EmPlayerRecoveryScratch`.
- **Vector w lanes.** The w lane of vectors passed to 0019A570 and 0019AFE0
  is not observable: both copy only x/y/z (0019A570 sets its own w = 1).
  0017C860 still writes 0x700038BC = 1.0, and the translation keeps that
  store.

## 3. Binding (coordinator)

**Precondition.** The translation reads the column table and the ledge
frame once (EmPlayerClimbTable, EmPlayerRecoveryLedge), where the original
re-reads the scratchpad (0x70003050/60/70/31E4) after each sweep call
(e.g. 0017CBBC, 0017CBF4, 0017CC7C, 0017D668, 0017D734). That is exact only
while the workers bound to lip/sides/hands/depth (001775E0, 001776E0,
00177CF0, 00177B80) and their callees do not rebuild those words; the
listings show they do not. A binding that changes this must split the
snapshot into per-read calls. The segment/sweep workers must also ignore
the w lane (0019AFE0 reads only x/y/z).

**Adapters** (context: an `EmPlayerRecoveryLive` holding the workers, `live`
= `player_states_actor_mut()`, the scene provider and the scratch):

- **State table.** `EmPlayerStatesBinding.stage.state[4] =
  em_player_recovery_state4`, with `state_context[4]` = the
  EmPlayerRecoveryLive. This is 0015B130's table entry for +5 = 4 (00162A40).
- **Workers of the other state modules** (all take `(void *context,
  EmPlayerLiveActor *, ...)`):
  - `em_player_recovery_translate_worker` (00178B90): em_player_fall.h,
    em_player_hang.h, em_player_reaction.h and em_player_major2.h
    `translate`;
  - `em_player_recovery_ledge_catch_worker` (0017D080): em_player_fall.h
    `test_0017D080`;
  - `em_player_recovery_ledge_grab_worker` (0017C860 with the raw reach
    word): em_player_fall.h `ledge`;
  - `em_player_recovery_stick_quadrant_worker` (001751A0),
    `em_player_recovery_strafe_worker` (00178EC0) and
    `em_player_recovery_react_002243F0_worker`: for the 001634A0 (+5 = 6),
    00169730, 0016A4B0 and 0016AE40 callbacks when they are translated;
  - `em_player_recovery_react_00224B80_worker`: for 00222AD0.
- **Slide.** `EmPlayerSlideWorkers.translate =
  em_player_recovery_slide_translate` and `.damage =
  em_player_recovery_slide_damage`. They write the mirror into `live`, run
  on the record and read it back.
- **Climb.** `EmPlayerClimbWorkers.translate =
  em_player_recovery_climb_translate`. It also keeps the non-byte
  `link_kind`.
- **Shared worker context.** Both worker tables share one `context` pointer.
  The coordinator's slide/climb context therefore has to reach the
  EmPlayerRecoveryLive: either put it first in the coordinator's context
  struct, or bind small trampolines.
- **0x70003A20.** `EmPlayerRecoveryLive.shared3A20` optionally points at
  another module's 0x70003A20 word. The fall lane's `EmPlayerLandScratch.s3A20`
  is one; it asks for one instance shared by every writer. The adapters load
  the word into the scratch before a call and store it back after.

**Workers** (`EmPlayerRecoveryWorkers`). The third column names the native
code that exists today in other lanes. This lane has not verified it.

| Field | Original | Candidate native (other lanes) |
|---|---|---|
| sine, cosine, atan2, sqrt | 0011E2A8, 0011DE90, 0011E620, 0011E748 | `em_sdk_math_original_float_0011E2A8` / `_0011DE90` (em_sdk_math_original.h), `em_director_original_0011E620`, `em_item_sdk_sqrt` |
| probes | 001764E0 | `player_states_wall_probes` (its `$s1` inheritance note: PLAYER_FLOOR.md P16) |
| floor, fall | 00175900, 001796C0 | `player_states_floor_service`, `player_states_fall_check` |
| request, arbiter, clip_frames | 001749A0, anim_clip_arbiter, 001C61D0(+40, clip) | the pose host (untranslated as a worker) |
| sound | 001FBD50(p, id, 0, 300) | `em_sfx_play_at` (em_sfx.h) |
| shake | 001B61C0 | untranslated (em_player_fall.h calls it the pad vibration request) |
| random | 00122BB8 | em_random.h |
| react_0021C350/270/120/190, react_0021D490 | 0021C350, 0021C270, 0021C120, 0021C190, 0021D490 | untranslated (em_player_reaction.h lane) |
| move, sweep, segment | 0019AD00, 0019AFE0, 0019A570 | em_coll_move_original.h (0019AD00, 0019AFE0); 0019A570 via em_collision. The worker fills EmPlayerProbeHit from what the probe leaves at 0x700031B0/D0/D4. |
| table, attribute | 0019BC40, 0019A180(0, i) | `em_collision_column_table`; 0019A180 is `entry_attribute` in em_player_climb.c (static) |
| ledge, lip, sides, hands | 00177510, 001775E0, 001776E0, 00177CF0 | `capture`, `lip_sweep`, `high_sides`, `high_hands` in em_player_climb.c, all static. The climb owner must export them over an `EmPlayerRecoveryLedge`. |
| depth | 00177B80 | untranslated |
| hang_clear, hang_row | 0017F320, 00188550 | `em_player_climb_hang_clear`; 00188550 static in em_player_climb.c |
| skeleton | anim_eval_skeleton + node 1 (+C0..CC of *(D_00275B40 + 4)) | the pose host |
| column | 001760C0(p, at, 1, h) | none standalone (also a worker in em_player_floor.h EmPlayerProbeWorkers) |
| heading, reentry, handoff | 00174AC0, 0017C440, 0017C540 | `em_player_heading_record_worker` (em_player_heading_record.h; context an `EmPlayerHeadingRecord`, `world.spad3A20` the shared 0x70003A20 word) (00174AC0), em_player_motor.h (0017C440); no standalone 0017C540 |

**Scene** (`EmPlayerRecoveryScene`, filled each call by `scene`):

| Field | Original |
|---|---|
| camera_yaw | D_008106A0 |
| d8106F1 | D_008106F1: a pointer at the canonical byte (HK; 00224B80 reads it after its 0021C270 worker); 0017C860, 0017D080 and 00224B80 refuse without it |
| spad3B8D | spad 0x70003B8D |
| pad_gait, pad_x, pad_y | D_00810E57, D_00810E64, D_00810E65 |
| area | D_00810700 |

**Data.** D_00248730 (the 00178EC0 gait table, 4 words) is the only
original table. It is embedded as its four raw words, and the oracle checks
them because it executes the ELF's table.

**Build, when bound.** Add `src/game/em_player_recovery.c` to COMMON. The
adapters call `em_player_slide_actor_*_live` and
`em_player_climb_actor_*_live`, so `src/game/em_player_slide.c` and
`src/game/em_player_climb.c` must be in COMMON as well (they are not today).

**FLOOR closure.** +5 = 4 now has a callback. 00162A40 exits to 7 (case 3,
and case 0xB without floor) and to 9 (case 2). Both are already in the
closure table. It also hands on through 0017C540/0017C440, as the other
closure states do.

## 4. Verification

`make test-player-recovery-reference` runs
`python3 tools/test_player_recovery_reference.py`. The default run takes
about 3 to 4 s; `EM_TEST_FULL=1` runs the exhaustive sweep in about 60 s. Route
mode (`EM_TEST_ROUTE=1`) is not part of the default run. It replays all six
beats below by default (`EM_ROUTE_BEATS` picks beats); the full run takes
about 15 minutes.

- **The interpreter.** It is the shared bounded EE core with COP1 and every
  VU0 macro form replaced by `tools/ee_float_model.py`. Any instruction the
  model does not define raises. It runs as a closed world: a call to any
  target that is neither hooked nor one of the leaves above raises.
- **Unit cases.** For each routine the original runs over a record built from
  the captured player record (state 04), with fields drawn at their boundary
  values. Every worker callee is hooked; its results are scripted, and each
  hook may rewrite record fields. In a third of the cases one call k (biased
  to the first calls) rewrites every field the routine reads or writes, and
  no other call rewrites anything. A store on the wrong side of call k is
  therefore caught, with no later rewrite to mask it.
  - Compared: all 0x320 bytes, the four scratch words, the return value and
    the callee sequence with every argument (floats as bits, the ledge frame
    each sweep reads included). The oracle also asserts that a0 is the
    record, that 001C61D0 gets +40, and that 001FBD50 gets (0, 300.0).
  - **Call-order sweep.** It runs over every distinct path the cases took
    (the dispatch byte and each call with its small int argument; 160 paths
    in quick mode). For every call k on the path it reruns the case with call
    k alone rewriting every field the routine reads or writes. This is done
    once with sentinel values no routine stores (0xA5, 0xA5A5, −1234.5) and
    once with drawn values. The sweep is deterministic, so every store/call
    order on every observed path is checked.
  - Constructed exact boundaries (and ±1 ulp): \|+38\| = 4.5, the π/2 turn
    on 0x35, the π/4 and 3π/4 quadrant limits, 0.6π, 0.349, 2.8, 0.5, 115,
    0.5 + near = far, h = top, h = top − reach, aux = 0.62831855, +228 = 100
    and +3C = 32. Extreme ledge points (±1e20, ±MAX) are included too.
- **Coverage.** Every instruction of all eight originals is executed in the
  default run. The only exceptions are 26 words no input can execute: words
  after an unconditional branch and its delay slot that no branch targets,
  and 001751A0's `bltz` halves of the unsigned-byte-to-float idiom (the byte
  comes from lbu). They are listed in `DEAD` and were checked by hand. The
  return values 0/1/2 of the value routines are asserted.
- **Fail-stop.** For sampled cases a worker fails at call k, for every k.
  The routine must return −1 with the record and scratch exactly as the
  original had them at that call. Each required worker, removed, must fault
  with nothing written and nothing called. The workers a routine reaches must
  all be in its required set.
- **Leaves.** float_to_int, 001B1470 and the 00128350 + 001000C0 compare are
  swept directly against the original: every exponent class, the
  2^31/π/0.6π edges and random words. The wrap fixed point is checked to
  fault.
- **Adapters.** The slide translate/damage, the climb translate (with
  `link_kind` kept), state4 and the seven worker-shaped adapters (with
  0x70003A20 kept in a `shared3A20` word) give the same record, mirror,
  result, scratch and calls as the direct routine. A scene-reading adapter
  without a scene provider faults with nothing written.
- **Mutants.** 32 hand-made mutants of em_player_recovery.c were run
  against the default run. The mutation script is scratch and not kept. 25
  fail, among them every `<`/`<=` swap at a gate, each store/call reorder
  tried, and wrong commit bytes. The 7 survivors change nothing the original
  can observe:
  - `x·(−t)` vs `−(x·t)`;
  - `mul(step, scale)` vs `mul(scale, step) + 0`, which differ only on a −0
    product;
  - `x/2` vs `x·0.5` on an integer-valued x;
  - mula+madd vs `add(mul, mul)`, equal unless a product overflows;
  - the w lane of the 0019AFE0 target;
  - a local updated before or after a call that cannot see it;
  - the soft-float compare vs `c.lt.s` on the non-negative `fabs` result.
- **Route mode** (`EM_TEST_ROUTE=1`, `EM_ROUTE_BEATS`). The original player
  stage replays PCSX2 route beats (`../Extermination/build/s87/route`) twice,
  once as is and once with the eight native routines hooked in at their
  original addresses. In the second run their workers run the original
  callees in the same EE, with vectors at the original scratchpad addresses.
  Every frame, the 0x320 player bytes, the scratch words and the sound/effect
  calls must be identical, and both replays must match every trace row
  (state, +1F0 and clip exact; position, yaw and clock within the trace's
  printing).
  - RouteReplay runs no scripts or cinematics. A replay therefore ends before
    the first scripted-takeover row (+5 = 0, +1F0 = 0x41, after the player
    has moved).
  - Results: see section 5.

## 5. Results and limits

- **Unit oracle, all PASS on 2026-09-23.**
  - Quick mode: 2,060 routine cases (translate 260, strafe 120, quadrant 160,
    react_a 160, react_b 320, catch 360, grab 360, hang 320), 1,108
    call-order runs over 160 paths, 126 fail-stop checks, 23,058 leaf inputs
    and 48 adapter checks.
  - `EM_TEST_FULL=1` (about 60 s): 43,000 routine cases, 4,486 call-order
    runs over 504 paths, 742 fail-stop checks, 768,600 leaf inputs and 480
    adapter checks.
- **Route mode, PASS (2026-09-23).** Both replays match the capture on every
  trace row, and the native replay equals the original one byte for byte on
  every frame.
  - 06_hill_slide: 212 frames, 206 rows. Native 00178B90 ran 101 times and
    00224B80 65 times (all returning 0: no hit pending).
  - 12_crevice_jump: 345 frames, 337 rows. Native 00178B90 ran 238 times;
    0017C860, 001751A0, 002243F0 and 00178EC0 ran 38 times each, in the
    running jump (+5 = 6). Every 0017C860 and 002243F0 returned 0: no ledge
    grab happened.
  - 14_roger_encounter, up to counter 14229 (before the scripted
    takeover): 289 frames and rows. 00178B90 ran 218 times; 0017C860,
    001751A0, 002243F0 and 00178EC0 ran 38 times each (running jump), with
    every result 0.
  - 05_boxes (677 frames and rows, the two crate climbs) ran 00178B90 400
    times.
  - 11_crevice_prompt, up to counter 12231: 707 frames and rows, with
    00178B90 run 459 times.
  - 10_cage_roof_roger, up to counter 9046: 1,102 frames, 1,091 rows, with
    00178B90 run 349 times.
  - In total the route mode compared 3,332 frames with the native routines
    in place: 1,765 native 00178B90 calls, 76 each of 0017C860, 001751A0,
    002243F0 and 00178EC0, and 65 of 00224B80.
  - **Never reached on the route.**
    - 0017D080: none of the replayed falls called it. 00162DB0 calls it only
      in its sub-state 0, when 00174AC0 != 0 and not (gait 3 with
      001755B0 == 0).
    - 00162A40: +5 = 4 is in no captured beat.
    - 0017C860 and 002243F0 never returned 1, and 00224B80 never left
      sub-state 0.

    These paths are covered only by the unit oracle.
- **Not translated here** (workers, section 3): 00177B80, 001B61C0 and the
  0021C* / 0021D490 reactions. The ledge sweeps and 00188550 exist only as
  statics in em_player_climb.c.
- **The FLOOR "Known gap"** (FIRST_CONTROL.md) is not closed by this lane.
  0021C440's reaction states and 0015D100 / 00182DF0 / 001838B0 are other
  lanes' work.
- 0017C860's reach comes from +2EC (001639E0) or +2E4 (001634A0). The worker
  adapter takes the raw word, as em_player_fall.h passes it.
- 00178B90 splits \|+38\| ≥ 4.5 into float_to_int(+38/4) steps. A +38 of
  2^33 or more would make the original loop about 2^31 times. The
  translation does the same (no such speed occurs).
- 00178EC0 faults on a gait byte +23F above 3: D_00248730 has four entries.
  The pad unpack writes 0..3, and em_player_slide.c's gait tables fault the
  same way.
