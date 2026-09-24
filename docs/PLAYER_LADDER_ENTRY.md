# Player ladder entry and the Use surface actions (AREA11 cage ladders)

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "player-ladder-entry". This document covers the originals that run
when a Use press finds a surface attribute, and the state that takes the
player onto a ladder:

- 0015D4C0, the surface-attribute actions of the Use chain;
- its helpers 00176F90, 00177030, 00180300, 00199DB0 and 00199FA0;
- 00165B60, state 0xB (the ladder entry), with 00176DC0, 0017FC80 and
  00182A70;
- 001B61C0, the pad vibration request.

Along the route to Roger, they run for both cage climbs of beat 10
(`10_cage_roof_roger`):

- ladder A: Use at f267, state 0xB f268–f326, hand-off to state 0xC at f327;
- ladder B: Use at f779, state 0xB f780–f838, hand-off at f839.

Both climbs are on the attribute-0x32 column at x ≈ 360
(PLAYER_CLIMB_SLIDE.md section 7, FIRST_LEVEL_ROUTE.md section 6).

The translation is `src/game/em_player_ladder_entry.c/.h`. The module is
**live in AREA11** (bound by em_player_closure_live.c). Section 4 lists what the coordinator
binds.

## 1. What the original does

Every routine works on the 0x320-byte player record. The addresses in
brackets are the instructions translated there.

### 0015D4C0: the surface-attribute actions (returns 1 when an action started)

00160220 calls it at 00160318, after the interaction use scan finds nothing.

- 00176F90 runs first. If it returns 0, the result is 0.
- Otherwise the routine switches on +23B:

| +23B | Gate | Commit |
|---|---|---|
| 0x37, 0x38 | 00177030(p, 1) | +5 = 0x18, +6 = 0, +1F0 = 0x2C; 0x37: +D = 0; 0x38: the column search below |
| 0x32 | 00177030(p, 4) | +D = 0, +5 = 0xB, +6 = 0; +1F0 = 0x15, or 0x16 when 00180300(p, 0x700038A0, 0) ≠ 0 |
| 0x3B | 00177030(p, 1) | +D = 1, +5 = 0xB, +6 = 0, +1F0 = 0x15 |
| 0x33 | D_00810C7D ≠ 0 and 00177030(p, 1) | one unit back, +D = 2, +5 = 0xD, +6 = 0; +1F0 = 0x1B, or 0x1C when 00180300(p, 0x700038A0, 2) ≠ 0 |
| 0x3A | none | a probe 40 up (0019A570, mask 4) re-reads +23B; then 0x1E: +5 = 0x11, +1F0 = 0x20; 0x34: the pole entry below |
| 0x20 | D_00810C7C ≠ 0 | the record centre, a probe 40 up must hit attribute 0x3C: +218 = the record's yaw, +B0/+B8 = the centre, +5 = 0x16, +1F0 = 0x29 |
| 0x3D | 00177030(p, 0) | three probes (below); +290..+298, +294 = the top − 20.5, +5 = 0xA, +1F0 = 0x14 |

- **Case 0x32, the ladder columns.**
  - build_trs_matrix rebuilds +D0 (0015D7F8).
  - 0x700038A0 = +B0 with y + 10 (0015D818/0015D84C).
  - 00180300 probes 10 units along the body from that point. A hit on
    attribute 0x32 gives 0, so +1F0 = 0x15; any other result gives 0x16.
- **Cases 0x37/0x38.**
  - The point (0, 4.01, 10) goes through +D0 into 0x700038B0, then
    0019AD00(p, 0x700038B0, 7) runs.
  - **On a hit:** 0x700038C0 = the hit point minus the record's +0x24/+0x2C,
    at 0x700038B4's height. 0019BC40 then fills the column table, which the
    routine scans for the first entry with flag bit 0 whose height is above
    +B4 + 4.01. That height goes to +254. The action is "found" unless the
    height is below +B4 + D_002488B0 (24.0).
  - **Found:** +D = 3, and +B0/+B8 = the hit point + 1.5 × the record's
    +0x24/+0x2C.
  - **Not found:** +D = 1, and +B0 = one unit back ((0, 0, −1, 1) through +D0).
  - **On a miss, found is the caller's $s0.** The flag is only cleared on
    the hit path (0015D61C). 00160220, the only caller, holds the actor
    pointer in $s0 (00160248), so a miss commits "found" and reads the
    record 0x700031D0 names.
- **Case 0x3A with attribute 0x34, the pole entry.**
  - +30C = the record pointer (0015DA64).
  - 00176F90 runs again.
  - +2E0/+2E8 = the record's +0x34/+0x3C.
  - 00177030(p, 2) places the body. Mode 2 always returns 1.
  - A second probe 40 up: on a miss the result is 1 with nothing more.
  - On a hit: +23B = 0x34, +254 = hit y − 20.5, +290 = the record centre,
    +5 = 0xF, +1F0 = 0x20.
- **Case 0x3D.**
  - The yaw +C4 is kept first (0015DC2C).
  - 00177030(p, 0) gates.
  - Three probes run:
    - 0019AD00 at (0, 10, 10) through +D0;
    - 0019A570 (mask 6) from 3.8 units past the hit, 30 up;
    - 0019AFE0 (mask 6) down from the second hit.
  - A miss on any of them restores +C4 and returns 0 (0015DE98).
  - After the third hit:
    - +290/+298 = the hit + 1.5 × the record normal;
    - 00199FA0 fills two stack vectors, and +294 = the second vector's
      y − 20.5.
  - 0015DE68 reads that stack word even when 00199FA0 returned 0 without
    writing it.

### 00176F90: the attribute refresh (returns +23B)

- 0x700038A0 = +B0..+BC, with y − 0.2.
- 0019BA80(p, 0x700038A0, p+280, 7) probes.
- On a hit, +23B = the record's attribute byte +0x1A. On a miss, +23B = 0
  unless it is 0x35.

### 00177030(p, mode): the facing gate and placement

- **Entry, every mode.**
  - 0x70003A20 = 001B1470(π/2 + atan2(−rec+0x3C, rec+0x34)): the yaw that
    faces the record.
  - 0x70003A24 = 001B1470(0x70003A20 − +C4).
- **Mode 0:** if −π/4 ≤ 0x70003A24 ≤ π/4, +C4 = 0x70003A20 and the result
  is 1; otherwise 0.
- **Mode 1:** the same gate. It also sets +B0/+B8 = the record centre
  (00199DB0 into 0x700038B0).
- **Mode 2:** always 1.
  - The side comes from |0x70003A24| ≤ π/2:
    - yes: +218 = 0x70003A20 and side = 1;
    - no: +218 = 001B1470(π + 0x70003A20) and side = −1.
  - From the record centre (dx, dz), with +B0/+B8 measured from it:
    - 0x70003A24 = atan2(−dz, dx);
    - 0x70003A2C = sqrt(dx² + dz²), computed as mula then madd;
    - step = 0x70003A2C × cos(001B1470(π/2 + 0x70003A24 − +218)).
  - +B0/+B8 = the centre − side × (rec+0x34/+0x3C × step).
- **Mode 3:** +B0/+B8 = the record centre. The result is 1.
- **Mode 4** (the ladder):
  - 0x700038B0 = normalize(rec+0x34, 0, rec+0x3C);
  - 0x700038D0 = (0, 0, 1, 0) through +D0;
  - 0x70003A24 = their dot product, stored whether or not the gate
    passes (00177408).
  - If the dot product is ≥ 0.5: +C4 = 0x70003A20, +B0/+B8 = the record
    centre (into 0x700038A0), and the result is 1. Otherwise 0.
- Other modes return 0.

### 00180300(p, v, check): the attribute probe (0 expected, 1 other, 2 no hit)

- 0x70003600 = (0, 0, 10, 0).
- 0x70003610 = that point through +D0, plus v.
- 0019AFE0(p, v, 0x70003610, 6) probes. On a miss the result is 2.
- On a hit, +23B = the record's +0x1A, and the result is 0 when that byte
  is the expected one:
  - check 0: 0x32;
  - check 1: 0x3B;
  - check 2: 0x33.
- Any other check gives 1.

### 00199DB0(out) and 00199FA0(a, b): the record centre and corners

- With no record, the result is 0.
- **The cell record D_700030B0.** The owner at 0x700031D4 must be set,
  0x700031D8 must be 2, and uid = owner +0x0E >> 8 must not be 0xFF. The
  directory *0x70003250 then gives the offset word for uid, which must not
  be 0. The hull's min and max (6 floats) are there:
  - 00199DB0 writes (min + max) / 2 per lane;
  - 00199FA0 writes min to a and max to b.
- **A grid node.** Its halfword pairs (+0/+2, +4/+6, +8/+A) name vertices
  of *0x700031FC:
  - 00199DB0 writes each lane as the mean of that lane of the pair;
  - 00199FA0 writes the first vertex's lane to a and the second's to b.
- The result is 1 when written.

### 00165B60: state 0xB, the ladder entry (0015B130 state[0xB])

The sub-state is +6.

- **0.**
  - **Area 0xD with +B0/+B4/+B8 inside (1010..1030, 170..180, 830..850):**
    +6 = 0xA, +7 = 0, +1F0 = 0x15.
  - **Otherwise:** +6 + 1 and +7 = 0. Then +1F0 decides:
    - 0x15: clip 0xE3 at blend 8;
    - anything else: clip 0xE4 at blend 8, and +28 = 0.
- **1:** advances when +200 loses 0x8000.
- **2.**
  - **When the clip ends (+200 & 0x1000):**
    - 0x16 turns the yaw by π;
    - +D0 is rebuilt (identity, then the +C0 angles);
    - the offset (0, −10.4, 6.3) goes through +D0 and is added to the hip
      node *(D_00275B40 + 4) +C0 into +B0; +BC = 1;
    - +D0 is translated there;
    - 0x16 also sets +B4 = +294;
    - then +2F1 = 0, the clip D_002754D0[0], 0017FC80(p, 16), and +5 = 0xC,
      +6 = 0, +1F0 = 0x17.
  - **Else, for 0x16 only:**
    - **sub-state 0, at +3C ≤ 25:** the yaw is flipped and +D0 rebuilt.
      00180300 runs from the hip node.
      - If it hits 0x32 and 00199FA0 succeeds, the drop is measured:
        0x70003A20 = |top.y − bottom.y| − 16, then less 3 until it is
        below 3.
      - That gives +7 = 1, +294 = +B4 − (16 + 0x70003A20) and
        +2E4 = 0x70003A20 / (+3C − 1).
      - The yaw is flipped back.
    - **sub-state 1:** +B4 −= +2E4.
  - **The steps, after either branch.**
    - 0x16: on the halfword +28 at +3C ≤ 63, 52, 20 and 2.
    - Otherwise: on +7 at +3C ≤ 24 and 2.
    - Each step is 00182A70. The last one also plays sound 0x107.
- **0xA:** clip 0x70.
- **0xB:** when the clip ends, a fixed pose (1021.9, 187.2, 836.3) and
  clip 0xFE.
- **0xC:** when the clip ends, the same hand-off as case 2.
- **Area 2 only:** 00176DC0 runs after every case.

AREA11's ladders take the 0x15 path: 0 → 1 → 2, then the hand-off
(trace: 11/0x15 f268–f326, 12/0x17 from f327).

### 00176DC0, 0017FC80, 00182A70

- **00176DC0.**
  - +314 = 0.
  - For each yaw in D_0024895C (π/2, −π/2, 3π/4, −3π/4, π), 0x700036A0 is
    set to the Y rotation +C4 + yaw, translated to +B0.
  - The points (0, h, 5.5) for h = 4.01, 10, 18 go through 0x700036A0.
    Each is probed with 0019AD00(p, point, 6).
  - It stops when a probe's bit 2 is set and 001762E0 returns nonzero.
- **0017FC80(p, blend):** clip 001885D0(p) when +2F1 = 0, else 001885F0(p),
  requested with `blend`.
- **00182A70(p):** sound 00179B90(p) + 0x109 at 300.

### 001B61C0(big, small, duration, force): the pad vibration request

- Nothing happens unless all of these hold:
  - D_00810119 is set;
  - the pad block D_00810E40 is ready (+0x12);
  - D_00275BE0 is not 2;
  - without `force`, the pad is not already active (+0x16).
- Then:
  - +0x16 = 1 and +0x28 = duration;
  - +0x18 = 1 when big is nonzero. It is never cleared here;
  - +0x19 = small;
  - 00111018(port +4, slot +8, &block[0x18]) submits the actuator block.

### Where the readable C differs from the instructions

- 00177030 mode 4 keeps the dot product in a local. The instructions store
  it at 0x70003A24 (00177408).
- 0015D4C0 cases 0x37/0x38 read an uninitialized `found` on a 0019AD00
  miss. The instructions read the caller's $s0 (see above).
- 0015D4C0 case 0x3D: the C shows the stale stack read as ordinary code.

## 2. The translation (`src/game/em_player_ladder_entry.c/.h`)

- **The record.** Every routine works on the raw record by original offsets
  and cites the instruction beside each branch and store.
- **Workers.** Every callee not listed in the header is a worker in
  `EmPlayerLadderWorkers`:
  - the collision probes 0019BA80, 0019AD00, 0019A570, 0019AFE0 and
    0019BC40;
  - the VU0 routines, including build_trs_matrix;
  - the SDK scalars;
  - 001749A0, 001FBD50, 00179B90 and 001762E0;
  - the hip node read.
- **One owner (2026-09-24).** 0017FC80 (with 001885D0 / 001885F0, the
  D_002754D0 / D_002754D4 rows) is translated once, in
  em_player_ladder_climb.c; `em_player_ladder_0017FC80` and 00165B60's
  hand-off run it over this lane's `request` through a one-routine
  `EmPlayerLadderClimb`, so the former `clip_001885D0` / `clip_001885F0`
  workers are gone. 00180300 is translated only here: the closure states
  (em_player_closure_0e_18.c) run it through `em_player_ladder_probe_00180300`,
  which refuses only when the scratch, `apply`, `vadd` or `sweep_0019AFE0`
  is missing (the workers its instructions reach).
- **Faults.** A missing worker faults before the first write. A negative
  worker return stops the routine at once.
- **The copies.** The SDK copies 00102948 (quadword) and 001031E0 (three
  words) are translated inline.
- **The scratch.** `EmPlayerLadderScratch` holds every scratchpad word the
  routines use:
  - 0x700038A0..DC, 0x70003A20..2C, 0x70003600..1C and 0x700036A0..DC;
  - the probe state the collision workers leave: the point 0x700031B0, the
    record 0x700031D0 (kind, 32-bit word and bytes +0..+0x3F), the owner
    0x700031D4 with its +0x0E, 0x700031D8, and the column block
    0x700031E0 / 0x700030F0 / 0x70003170.
- **Data faults.** Reads the native world cannot give fault with
  `scratch->fault` naming the original instruction:
  - a record field when 0x700031D0 is 0 (the original reads low memory);
  - a vertex or directory entry outside the bound data;
  - column entry 32 or beyond;
  - the stale stack word of 0015DE68;
  - a drop loop whose subtraction no longer moves the word (the original
    never terminates).
- **Arithmetic.** Every float operation goes through `em_ee_float.h` on bit
  patterns: add, sub, mul, div, neg, mula/madd, and the c.lt/c.le compares.
- **00199DB0 / 00199FA0.** Both write all their output words or none. The
  original writes them one by one with nothing between that can fail, and
  the native side validates every read first.
- **001B61C0** is `em_player_rumble_001B61C0` over `EmPlayerRumble` (the pad
  block fields by original offset, the two bytes, and the 00111018 worker).
  `em_player_rumble_worker` fits the `int (*)(void *, int, int, int, int)`
  slots.

## 3. Verification

### Unit oracle (`tools/test_player_ladder_entry_reference.py`, default ~2 s)

The test executes the original instructions of all 13 routines from the
user's ELF on the fall oracle's `FallEE`, which routes COP1 and VU0 through
`tools/ee_float_model.py`. `LadderEE` adds branch recording and low-memory
read detection.

**What the setup guarantees.**

- Every one of the 36 jal targets is hooked (24) or executed as original
  code (12, including 001885D0 / 001885F0 inside 0017FC80). The test asserts
  this, and asserts that no hooked address goes uncalled.
- 0017FC80's native side is em_player_ladder_climb.c's translation reached
  through this lane's bridge, and odd-seeded 00180300 cases run through the
  narrow `em_player_ladder_probe_00180300` (its refusals are checked on the
  three workers it reaches). D_002754D0[0] is the ELF's halfword in every
  case: the table is read-only (no original code writes it), and the owner
  embeds the rows its own oracle checks against the ELF. (Before
  2026-09-24 the case drew non-original values for it.)
- Every hooked callee is scripted per case. The same script feeds the
  native workers:
  - probe results;
  - record kind, image, point and owner;
  - column tables;
  - VU0 outputs and scalar results.

**What each case compares.**

- all 0x320 actor bytes;
- every owned scratch word and the probe block;
- the return value;
- the corner outputs;
- the full worker call sequence, with argument values and, for vectors,
  which scratch vector is passed.

**Fault checks.**

- Every 5th case cuts a worker at a random call. The native side must
  return −1 with no call after the cut.
- When the native side reports a data fault, the test proves the original
  made that read:
  - a low-memory load for a null record;
  - the untouched poisoned stack word for 0015DE68.
  The calls up to the fault must also be identical.
- Missing-worker refusals: each of the 24 workers, the scratch and the
  world, times 8 entry points, the narrow 00180300 entry on its three
  workers, plus the rumble's pad and actuator. Every
  entry point must return −1 with no call and no write.
- Data-fault checks: a vertex index past the pool, and a directory offset
  past the image.

**Branch coverage.** Every one of the 124 conditional branches in the
translated routines is taken both ways in compared (non-fault) cases,
except one outcome no input can produce: 0015DA94 taken. That branch tests
00177030(p, 2), which returns 1 on every path.

**Results (2026-09-23).**

- **Default:** 5,000 of 40,000 cases, PASS in 1.2–3.9 s:
  - 23,484 worker calls identical;
  - 770 fault-stop cuts and 225 refusals;
  - 198 null-record and 4 stale-stack faults proven against the original.
- **`EM_TEST_FULL=1`:** 40,000 cases, PASS in 9.6 s:
  - 186,744 calls identical;
  - 1,603 null-record and 16 stale-stack faults proven.

### World mode (`EM_TEST_WORLD=1`, minutes)

Route beat 10 is replayed from its source snapshot (08_truck_crossing's end
state) with `shared.RouteReplay`: the original player stage, its pad input
and the camera from the trace. Two replays run:

- **Original:** the stage alone.
- **Native:** every translated routine hooked in at its original address.
  The hook addresses are 0015D4C0, 00165B60, 00176F90, 00177030, 00180300,
  00199DB0, 00199FA0, 00176DC0, 0017FC80, 00182A70 and 001B61C0. Their
  workers run the original callees in the same EE:
  - the actor and the owned scratch words are synced around every call;
  - the probe state is re-read after each call.

**Checks, every frame:** the player record, the whole RAM + scratchpad
hash, and the sound/effect calls. Every trace row is checked with
`route_row_check`.

**Length.** The default replays 340 frames:

- f267: the Use;
- f268–f326: state 0xB;
- f327: the hand-off;
- then 13 frames of state 0xC.

`EM_TEST_FULL=1` replays the whole valid span, to counter 9046, before the
Roger script, and includes ladder B. `EM_WORLD_FRAMES=n` overrides the
length.

**Results (2026-09-23), both PASS:**

- **Default (4 min 48 s):** 352 frames identical (whole RAM + scratchpad +
  player record), 341 trace rows within precision and 40 sound/effect
  calls. Native calls: 0015D4C0 once (returned 1), 00165B60 59 times,
  00180300 once (returned 0, so +1F0 = 0x15) and 0017FC80 once. States seen:
  0xB/0x15, then 0xC/0x17.
- **`EM_TEST_FULL=1` (6 min 52 s):** 1,102 frames identical, 1,091 trace rows
  and 101 sound/effect calls. Native calls:
  - 0015D4C0 twice (both returned 1: ladders A and B);
  - 00165B60 118 times;
  - 00180300 196 times (14 returned 0, 180 returned 1 and 2 returned 2; the
    others come from state 0xC's 001662D0);
  - 00182A70 22 times (0xC's steps);
  - 0017FC80 twice.

  States seen: 0xB/0x15, 0xC/0x1, 0xC/0x17 and 0xC/0x18.
- **Not reached on the route:** 00176F90, 00177030, 00199DB0, 00199FA0,
  00176DC0 and 001B61C0 were never called from original code at their own
  addresses. Inside the native 0015D4C0 they run natively, as part of it.
  Every case other than 0x32 is covered only by the unit oracle.

## 4. Binding (coordinator)

**Bound live in AREA11 since the Boxes step (2026-09-24):** `em_player_closure_live.c` binds this module over the live player record (FIRST_CONTROL.md "Engaged"). Workers with no translation are fail-stop workers that name their original. The notes below are the binding it follows.

### Stage slot

- `EmPlayerStageWorkers.state[0xB] = em_player_ladder_state_b`, with
  `state_context[0xB]` = a `const EmPlayerLadderWorkers *`.
- The Use chain calls `em_player_ladder_0015D4C0(workers, actor, &r)` where
  00160220 calls 0015D4C0 (00160318). This happens after the interaction
  scan (00184BA0), and in area 0x15 after 001AAC00. If r is 1, 00160220
  returns. The call sits inside the coordinator's use hook
  (`player_states_bind_use_chain`), which is not this lane's file.
- 0015D4C0's other states (0x18, 0xD, 0x11, 0xF, 0x16, 0xA) are reached
  only through attributes AREA11 does not author on the route. Their
  callbacks belong to other lanes.
- State 0xC (001662D0), which 00165B60 hands off to, belongs to another
  lane too.

### Workers of `EmPlayerLadderWorkers`

| Slot | Original | Existing translation to adapt |
|---|---|---|
| probe_0019BA80 | 0019BA80(p, point, p+280, 7) | none (untranslated; must fault until it is) |
| move_0019AD00 | 0019AD00 | `em_coll_move_0019AD00` (fill the probe block from its EmCollMoveScratch) |
| segment_0019A570 | 0019A570 | none as an original translation (em_collision.c's segment query is the older model) |
| sweep_0019AFE0 | 0019AFE0 | `em_coll_move_sweep_0019AFE0` |
| column_0019BC40 | 0019BC40(at, record) | `em_actor_collision_column_0019BC40` (heights/flags/count into the scratch) |
| trs | build_trs_matrix | `em_owner_services_build_trs_matrix` |
| apply / normalize | 001026A0 / 00102760 | `em_effect_original_001026A0` / `_00102760` |
| vadd | 001028B8 | `em_player_hang_vadd` |
| identity / euler / translate / rotate_y | 001029C0 / 00102C58 / 00102918 / 00102BB0 | `em_owner_services_identity_001029C0`, `_euler_00102C58`, `_translate_00102918`, `_rotate_y_00102BB0` |
| dot | 00102738 | `em_crate_sdk_dot3` |
| atan2 / cos / sqrt / fabs | 0011E620 / 0011DE90 / 0011E748 / 0011DF78 | `em_sdk_math_original_*` (int forms, bits in and out) |
| wrap_001B1470 | 001B1470 | `em_player_001B1470` |
| request / sound | 001749A0 / 001FBD50 | the adapters bound to `EmPlayerLandWorkers.request` / `.sound` |
| sound_base_00179B90 | 00179B90 | none (a rand() fold, em_player_floor.h note) |
| wall_001762E0 | 001762E0 | none (unported area-2 shove; AREA11 is not area 2) |
| node | *(D_00275B40 + 4) +C0..+CC | the pose host's hip node (as `EmPlayerLandWorkers.hip`, four words) |

**Adapter rules.**

- The VU0 slots may receive the same array as output and input, where the
  original passes one address for both.
- Each collision adapter must write the probe block exactly as the original
  leaves the scratchpad:
  - `record`: NONE, CELL or OTHER;
  - `record_word`: an identity the readers of +30C resolve;
  - `record_bytes`: node +0x00..+0x3F, or the cell record 0x700030B0..;
  - `s31B0`, `entity`/`entity_0E`, `s31D8`;
  - for 0019BC40, `s31E0`/`s30F0`/`s3170`.

### Scratch and world

- The binder owns **one** `EmPlayerLadderScratch` and gives the same
  instance to every worker.
- `EmPlayerLadderWorld`:
  - `directory` / `directory_size` = the `EmActorCellTable` bytes;
  - `verts` / `vert_count` = `EmCollProbeGrid` verts;
  - `area` = D_00810700;
  - `d810C7C` / `d810C7D` = the progress bytes;
  - `d2754D0` = D_002754D0[0].

### Rumble

`EmPlayerRumble` {
  pad block (port, slot, ready, active, act[6], duration);
  &D_00810119;
  &D_00275BE0;
  a 00111018 worker (the platform's force feedback)
}. `em_player_rumble_worker` binds into `EmPlayerLandWorkers.rumble` and the
stage workers' `cue`. No native force-feedback backend exists yet. The
worker is where it plugs in.

## 5. Limits and open items

- **World run.** The default world run covers ladder A only. Ladder B is
  covered only by `EM_TEST_FULL=1`, which takes about 7 minutes.
- **Workers without an original translation.** 0019BA80, 0019A570,
  00179B90 and 001762E0 have none (001885D0 / 001885F0 run inside
  em_player_ladder_climb.c's 0017FC80). Until they exist,
  the binder must leave those slots unbound, so the module refuses (−1).
  The ladder therefore cannot go live yet: 00176F90 needs 0019BA80, and the
  0x32 path needs 0019AFE0 through 00180300.
- **Record identity.** The +30C record identity (case 0x3A) has no port
  representation. No AREA11 node on the route carries 0x3A.
- **Data faults.** They are faithful refusals of reads the original makes
  from memory the port does not model: null records, the stale stack word,
  and a runaway drop loop. None occurs on the route.
