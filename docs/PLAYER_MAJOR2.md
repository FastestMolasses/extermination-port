# Player +4 = 2 states of the FLOOR closure

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "player-major2-states", 2026-09-23. This document covers the +4 = 2
states that the FLOOR state closure lists (FIRST_CONTROL.md, "FLOOR state
closure"), and the four routines that enter them. It records what each
original does, the translation, the binding the coordinator makes, the
evidence and the limits. Nothing here is wired into the live player yet.

Code: `src/game/em_player_major2.c/.h`. Oracle:
`tools/test_player_major2_reference.py`.

## 1. What the originals do

0015B770 (the +4 = 2 handler of 0015BA50) calls 0021C440, then dispatches on
+5. The routines below are its targets for +5 = 3, 4, 5, 6, 7, 0x16 and 0x19.
Each one switches on the sub-state byte +6. The shared pieces are:

- **State-0 prologue** (00221FC0, 00222580, 00222AD0, 002230A0):
  - `001B61C0(0, 0xC0, 5, 1)`, then +6 = +6 + 1 (re-read after the call)
    and +7 = 0;
  - a selector: +302 (00221FC0, 00222580), `00122BB8() & 1` (00222AD0) or
    the +2F1 class 0/1/2 (002230A0). +F & 2 forces it to 1 (002230A0: only
    from class 0) and clears +F;
  - +224 != 0: `001FBD50(p, 0x152, 0, 300.0)` then `0021C350(p)`;
    +22C != 0: `001FBD50(p, 0x153, 0, 300.0)` then `0021C270(p)`;
  - +220 <= 0: +6 = 0xA and return. 00221FC0 and 00222AD0 first call
    `0021C200(p)`; 00222580 and 002230A0 do not;
  - !(+228 < 100.0) with D_008106F1 set: +6 = 0x14, and the selector becomes 0;
  - the clip request `001749A0(p, clip, 0, blend)`, then 00221FC0/00222580
    copy +294 into +B4, and 002230A0 copies +290/+298 into +B0/+B8.
- **Root motion** (00221FC0 0xC, 00222580 0xB, 00222AD0 0xB, 002230A0 0xC):
  - on +200 & 0x1000, go to the next sub-state;
  - otherwise +38 = node+8 − +21C, +21C = node+8, `00178B90(p, 1)`,
    +2EC = node+4 − +2E4, +2E4 = node+4, +B4 = +B4 + +2EC, then
    `00175900(p, 1)`. Here node is `*(*D_00275B40)`, the skeleton's first
    node. The subtractions and the add are EE sub.s/add.s.
- **Landing** (00221FC0 0xD, 00222580 0xC, 00222AD0 0xC, 002230A0 0xD):
  - `00179880(p, p+2EC)`, then `00175900(p, 1)`;
  - on a floor: `00182870(p, 1)` and `001FBD50(p, 0x156, 0, 300.0)`. With
    +F == 0x63 or +234 == 1 this is the **2/3 reset**: +4 = 2, +5 = 3,
    +6 = 0, +1F0 = 0x3F. Otherwise +6 + 1, clip 0x2A at blend 1.0 and
    +1F0 = 0x40. 00221FC0 alone then plays 0x156 a second time;
  - with no floor and +23A == 0x5D: `0021D250(p, 0)`. 00222580 has no such
    test.
- **The tail sub-states:**
  - the +3C countdown to `0021D490`: <= 20.0;
  - 0x1000 → +7 = 0 and `001B61C0(1, 0xEE, 0x3C, 1)`;
  - `0021D2E0(p, 0x78, 0)`;
  - the +3C test to `0021C120`: <= 21.0 in 00221FC0, 8.0 in 00222580 and
    002230A0, 35.0 in 00222AD0;
  - `0021C190` nonzero → next sub-state, else +204 = 0.1 (0x3DCCCCCD);
  - 0x1000 → exit, else +204 = 0.25 (0x3E800000).

| Routine | +5 | Clip table (state 0) | Exit on 0x1000 (sub-states 1 and 0x16) |
|---|---|---|---|
| 0021E830 | 3 | 0x1C4 at blend 0.0 (sub-state 0) | none; see below |
| 00221FC0 | 4 | +2F1 0 / else × selector 0 / 1: 0x102/0x104, 0x103/0x105; blend 8.0 | 1/0xC, +20E 0x3C, `0017FC80(p, 16.0)` |
| 00222580 | 5 | 0xB5/0xB7, 0xB6/0xB8; blend 8.0 | 1/0xE, +20E 0x3C, `0017FF80(p, 16.0)` |
| 00222AD0 | 6 | +234 0: 0x8F/0x90, else 0x1C8; blend 1.0 | +302 9: 1/9 +1F0 0x10, else 1/0x18 +1F0 0x2C; `001749A0(p, 00188550(p), 0, 16.0)`; +20E 0x3C |
| 002230A0 | 7 | class 0: 0xDD (+234 0) or 0x1C9; class 1: 0xDF; class 2: 0xE0; blend 8.0 | D_00275B14 0x1E: 1/0x12 +6 0; 0x36: 1/0x12 +6 0x28 (sub-state 1 only); else 1/0x10. +20E 0x3C, +2F1 = 0 (sub-state 1), `001749A0(p, 001885B0(p), 0, 16.0)` |

The sub-state 0xA clip is 0x106 (blend 8.0) in 00221FC0, 0xB9 (1.0) in
00222580, 0x91 (1.0) in 00222AD0 and 0xDE (8.0) in 002230A0; each also
clears +21C and +2E4. Sub-state 0xB waits for +200 & 0x8000 to clear in
00221FC0 and 002230A0 only. Where the others have 0xB as root motion,
their sub-states are numbered one lower.

**0021E830** (+5 3). Sub-states:
- 0: +6 = 1, +7 = 0, `001749A0(p, 0x1C4, 0, 0.0)`,
  `001EFE00(0x80000051, p)`, `001B61C0(0, 0xC0, 5, 1)`, sounds 0x146 and
  0x151 at 300.0, then +2EC = 0;
- 1: +3C <= 160.0 sets +6 = 2, +234 = 2 and D_00810707 = 2, then calls
  `0015C1F0(p)`. `0021E650(p)` runs either way;
- 2: 0x1000 sets +6 = 3, +7 = 0 and calls `001B61C0(1, 0xEE, 0x3C, 1)`;
  otherwise `0021E650(p)`;
- 3: `0021D2E0(p, 0x78, 1)`.

Every sub-state, any other value included, then runs `00179880(p, p+2EC)`
and `00175900(p, 1)`.

**00225570** (+5 0x16, entered by 0021D250 on a +23A 0x5D floor). Sub-state
0 sets +6 = 1 and +7 = 0 and falls through to 1: `0021D2E0(p, 0x78, 0)`.

**002255C0** (+5 0x19). 0015B770 writes +1 = 0 before calling it. Sub-states:
- 0: +224 != 0 plays 0x146 at 300.0, then `0021C350(p)`. Then, if
  +220 <= 0: +6 + 1, +7 = 0, sound 0x156, and the halfword +28 = 0x10 after
  the call;
- 1: the signed halfword +28 counts down; the store comes first. At zero:
  +6 + 1, `0021D490(p)`, `001B61C0(1, 0xEE, 0x3C, 1)`;
- 2: `0021D2E0(p, 0x78, 0)`.

**Entry routines.** 00181110, 00181180 and 00181D70 share one guard: they
return 0, and write nothing, when +224 == 0, +22C == 0 and (+F & 2) == 0.
Otherwise:
- 00181110(p, a1) writes +4 = 2, +5 = 4, +6 = 0 and +302 = a1 (low byte),
  and returns 1;
- 00181180(p, a1) does the same with +5 = 5;
- 00181D70(p) first sets D_00275B14: 0x34 when +5 == 0x10, else 0x36 when
  +6 >= 0x28, else 0x1E. It then writes 2/7 with +6 = 0 and returns 1.

001823E0(p) writes 2/0x19 with +6 = 0 and returns 1 when +224 != 0.
Otherwise it returns 0.

All compares are the EE FPU's equal / less-or-equal / less-than compares. A denormal or −0 therefore
counts as 0, and a NaN or Inf operand is saturated by sign before the
compare (EE_FLOAT_MODEL.md section 2).

## 2. Where they sit in the closure

| Entered (+4/+5) | By | Caller of the entry |
|---|---|---|
| 2/3 | 0017C580's reset (FIRST_CONTROL.md); **also** the landing sub-state of 00221FC0, 00222580, 00222AD0 and 002230A0 (+F 0x63 or +234 1) | — |
| 2/4 | 00181110 | 001662D0 (+5 0xC) |
| 2/5 | 00181180 | 00168050 (+5 0xE) |
| 2/6 | 0017F240 | 001647D0 (+5 9) |
| 2/7 | 00181D70 | 00169730 (+5 0x10), 0016AE40 (+5 0x12) |
| 2/0x16 | 0021D250 | 00162DB0, and the landing sub-states above on +23A 0x5D |
| 2/0x19 | 001823E0 | 0016DE40 (+5 0x19 of +4 1) |

Where these routines write +4/+5 themselves, they go to 1/0xC, 1/0xE, 1/9,
1/0x18, 1/0x12, 1/0x10 and 2/3. 0021D250 adds 2/0x16. Every one of these is
already in the closure table, so these routines add no new state. Their
callees were not followed for +4/+5 writes here: 0021D2E0, 0021C120,
0021C190, 0021C200, 0021C270, 0021C350, 0021D490, 0015C1F0, 0021E650,
0017FC80, 0017FF80, 00188550, 001885B0, 001EFE00, 00178B90 and 00182870.
Their translations own that question.

0015B770's +5 = 23 and 24 are aliases of 0 (0021D800) and 2 (0021E490).
None of this lane's routines is aliased: the oracle's dispatch section runs
the original 0015B770 over +5 = 23 and 24 and sees the calls go to those two
addresses.

## 3. Translation

- **Sources.** The byte-matched decomp C is the ground truth for 0021E830,
  00222580, 00222AD0, 002230A0 and 00225570 (all mwcc 2.3.3), and for
  00181D70 and 001823E0. Their built objects equal the expected objects
  word for word, relocations aside. For the rest the original instructions
  were read (`../Extermination/build/asm/.../func_*.s`):
  - 00221FC0 is NEARMISS (89.97%). Its readable C matches the instructions
    in every store, call and argument, including the two `0x156` sounds of
    the landing and the `0021C200` before the knock-down;
  - 00181110 and 00181180 are encoded as words, decoded by hand;
  - 002255C0 is hybrid asm with `.word` branches, decoded by hand.
- **Record.** Each routine works on `EmPlayerLiveActor` by the original
  offsets. Float words that the original only copies (+B4 = +294,
  +21C = node+8, ...) are copied as bits.
- **Float arithmetic.** Arithmetic and compares use `em_ee_float.h`:
  `em_ee_sub_bits`, `em_ee_add_bits` and `em_ee_c_eq/c_le/c_lt_bits`. The
  header's own test (`tools/test_ee_float_header.py`) was green when this
  was built.
- **Workers.** Every original callee is a worker in
  `EmPlayerMajor2Workers`, named by function or by address. Each routine
  checks every worker it can reach before its first write and returns −1 if
  one is NULL. A worker that returns a negative value stops the routine with
  −1; the writes made before the call stay.
- **Reads.** The skeleton node is read through `root_node(offset)` at each
  point the original loads it, since `00178B90` sits between the loads.

## 4. Binding (for the coordinator)

**Bound live in AREA11 since the Boxes step (2026-09-24):** `em_player_closure_live.c` binds this module over the live player record (FIRST_CONTROL.md "Engaged"). Workers with no translation are fail-stop workers that name their original. The notes below are the binding it follows.

**State table.** In `EmPlayerStageWorkers` (em_player_floor.h), with
`state2_context[n]` pointing at one `EmPlayerMajor2 { &workers, &scene }`:

| state2[] slot | callback |
|---|---|
| 3 | `em_player_major2_0021E830` |
| 4 | `em_player_major2_00221FC0` |
| 5 | `em_player_major2_00222580` |
| 6 | `em_player_major2_00222AD0` |
| 7 | `em_player_major2_002230A0` |
| 0x16 | `em_player_major2_00225570` |
| 0x19 | `em_player_major2_002255C0` (`em_player_stage_0015B770` writes +1 = 0 first) |

Slots 23 and 24 belong to the reaction lane (0021D800 / 0021E490).

**Scene** (`EmPlayerMajor2Scene`). The coordinator keeps one instance. Its
two progress/request bytes are pointers at the canonical storage (HK, lead
decision D2); a routine refuses (−1, nothing written) without either:
- `d8106F1` (D_008106F1[0]): the same canonical byte the stage scene points
  at (`em_scene_req_at(s, 0x008106F1u)`);
- `d810707` (D_00810707): written 2 by 0021E830; the canonical progress byte
  (`em_scene_progress_at(s, 0x00810707u, 1)`). 0015CF90 (live, the player
  stage), 0015C750, 001A8840, 001AF2C0, 001B07C0 (live), 00200890, 002160B0
  and 0021C270 also touch it;
- `d275B14` (D_00275B14): written by 00181D70 and read by 002230A0. It is
  shared with 001696A0, 0016ADE0 and 0016B8A0, so it must be a single
  storage.

**Workers** (`EmPlayerMajor2Workers`). The same originals already have
translations elsewhere:
- `em_player_reaction.h` (reaction lane): 00179880, 00182870, 0021D250,
  0021D2E0, 0021D490, 0021C120, 0021C190, 0021C270 and 0021C350;
- `em_player_fall.h`: 00179880, 0021D250 and 0021D2E0;
- `player_states_floor_service`: the `floor` worker (00175900), with the
  same signature.

Bind each through a thin adapter to these signatures. Still untranslated
anywhere: 001749A0, 001FBD50, 001B61C0, 001EFE00, 0015C1F0 (NEARMISS),
0021E650, 00178B90, 0021C200, 0017FC80, 0017FF80, 00122BB8 (SDK rand),
00188550 and 001885B0. `root_node` reads `*(*D_00275B40) + 4 / + 8` from
the display's skeleton, the same node that `EmPlayerSlideScene.root_forward`
samples.

**Entry routines.** Their callers (001662D0, 00168050, 00169730, 0016AE40
and 0016DE40) are not translated yet. When they are, they call
`em_player_major2_00181110/00181180(actor, a1)`,
`em_player_major2_00181D70(actor, &scene)` and
`em_player_major2_001823E0(actor)` in place of the originals. Each returns
the original's 0/1 (−1 only for a NULL argument).

**Makefile.** Add `src/game/em_player_major2.c` to `COMMON` when the table
is bound. The test target is:

```make
.PHONY: test-player-major2-reference
test-player-major2-reference:
	python3 tools/test_player_major2_reference.py
```

**Gate.** Do not engage the FLOOR gate on the strength of this module alone.
Its workers above are unbound, and FIRST_CONTROL.md's "Known gap" (0021C440's
reaction states, 0015D100, 00182DF0/001838B0) is separate from this lane.

## 5. Verification

`tools/test_player_major2_reference.py` (`make test-player-major2-reference`
once the target is added):

- **What runs.** The shared EE interpreter (test_player_slide_reference.EE)
  runs over the captured AREA11 RAM `playable_ee.bin` (state04). First the
  code bytes of all 12 routines in the capture are checked to equal the
  pinned ELF.
- **Float model.** COP1 arithmetic and compares go through
  `tools/ee_float_model.py` (subclass `ModelEE`). Any other COP1 op, and
  any VU0 macro op, stops the test.
- **Seeds.** The player record at 0x8102B0 is seeded from the state04
  capture and the route captures 05_boxes and 10_cage_roof_roger when
  present, or from random bytes. The key floats, flags and bytes come from
  boundary and special sets.
- **Callees.** Every callee is hooked on both sides with identical scripts:
  - its arguments are logged: the record pointer, the integers, `$f12` as
    bits, and 00179880's `a1 = p + 0x2EC`;
  - it returns scripted values (the 00175900 result, 0021C190, rand, the
    00188550/001885B0 clips);
  - it applies scripted writes to record bytes and to the skeleton node.
- **Compared.** All 0x320 record bytes, D_00810707, D_00275B14, D_008106F1,
  the node, the entry routines' return values and the callee sequence.
- **Fixed scenarios** (5,670):
  - each clip, the knock-down (−0 and denormal gauges included), the
    alternate exit and its gate, and every exit branch by +302 and
    D_00275B14;
  - landing reset, next, 0x5D and air; root motion;
  - each +3C threshold, at the value and one ULP above;
  - 0021C190 both ways, and 002255C0's countdown at 0, 1, −1 and 0x10;
  - every entry guard term, return and D_00275B14 value;
  - every compare against every special pattern (±0, denormals, ±Inf,
    NaNs, MAX);
  - read-after-call order: for every routine, sub-state and callee, that one
    callee overwrites the control bytes, or the float bytes, and the node.
- **Random cases.** 1,500 in the default run, 30,000 with `EM_TEST_FULL=1`.
- **Coverage asserted.** Every switch label and default of every routine,
  and the named branch classes of the `NEED` table (each clip, each exit
  triple, reset/next/0x5D/air, root, the 0021E830 and 002255C0 branches,
  both returns of every entry routine, all three D_00275B14 values). The run
  prints the path classes it reached: 137 by default, 143 in full.
- **Dispatch.** The original 0015B770 runs against `em_player_stage_0015B770`
  with the native routines bound in `state2[]`. The reaction routines and
  phases are hooked on both sides. It covers every slot, 8, 0xD/0xE phases,
  0x1A, 0xFF and the 23/24 aliases; 120 cases by default and 3,000 in full.
- **Fail-stop.** For each routine, removing any single worker that the
  original was seen to call faults with −1 before any write or call.
  Removing any other worker does not fault (the set equality is asserted).
  A worker returning a fault stops the routine. NULL context or record
  returns −1.
- **Timing.** The default run takes about 2.5 s; `EM_TEST_FULL=1` about
  10.5 s.
- **Mutants killed** (each applied to the C, then reverted):
  - thresholds (21.0 → 20.0), the second landing sound, +6 from the cached
    sub-state;
  - host float sub/add, host `<=`/`<`/`==` instead of the EE compares;
  - `>= 0x28` → `> 0x28`, an unsigned +28 countdown, the default sub-state
    skipping 0021E830's tail, and +302 as a flag;
  - order mutants: +6 before 001B61C0, +B4 before 001749A0, +2EC = 0 before
    the sounds, 00178B90 before the +21C store, +2F1 after 001885B0, the +F
    test after the hit cues, +20E before the request, and the +F read before
    the landing sound;
  - D_00810707's value, a missing pre-check, and a spurious pre-check.

  The one survivor is equivalent: re-reading +6 in 002255C0 sub-state 1,
  with no call between the reads.

## 6. Limits

- **No original capture exercises these states.** The route traces record
  +5 but not +4. Along all 15 beats +5 takes the values 0, 1, 2, 5, 6, 8,
  0xB, 0xC, 0x1C and 0x25. It is never 3, 4, 7, 0x16 or 0x19. Every +5 = 5
  row (beats 10, 11, 12) carries +1F0 0xB and every +5 = 6 row (beats 12,
  14) +1F0 0xC. That is consistent with the +4 = 1 fall (00179680 writes
  +1F0 0xB) and running jump; +5 = 5 goes on to 8, the +4 = 1 landing. The
  +4 = 2 states are therefore reached only by damage, a 0x63 landing
  request, a 0x5D floor, or the hang/crouch/ledge states that AREA11's route
  does not take. The evidence is the instruction oracle over captured RAM,
  not a capture of the states running.
- **Callees are hooked, not verified here.** The oracle proves that each
  routine makes the same calls, with the same arguments, in the same order,
  and reads their effects at the same points. What the callees do is their
  translations' own evidence.
- **Not live.** Nothing binds these callbacks yet (section 4).
