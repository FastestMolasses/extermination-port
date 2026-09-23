# Player action machine and armed stances 0x1D..0x1F

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "player-weapon-states-a", 2026-09-23. This document covers the action
machine 001607D0, which enters +5 = 0x1D..0x22, and the three stance routines
that the FLOOR state closure lists for +4 = 1, +5 = 0x1D, 0x1E and 0x1F
(FIRST_CONTROL.md, "FLOOR state closure"). The same routines are the
original behind WP-15 P25/P26 (the R1 aim and the R2 stance,
FIRST_LEVEL_AUDIT.md). It records what each original does, the translation,
the binding the coordinator makes, the evidence and the limits. Nothing here
is wired into the live player yet.

Code: `src/game/em_player_weapon_states_a.c/.h`. Oracle:
`tools/test_player_weapon_states_a_reference.py` (`make
test-player-weapon-states-a-reference` once the target is added).

All four decomp functions are byte-matched C (`src/func_001607D0.c`,
`func_0016FCF0.c`, `func_001703E0.c`, `func_001729A0.c`, no NEARMISS marker).
The translation follows that C. The instructions (`build/asm`) were read as
well, for three things: which store sits in a call's delay slot (it happens
before the callee runs), which bytes are re-read after a call, and the
operand order of each COP1 op.

The header comment of the decomp's `func_0016FCF0.c` calls it a
"boss/large-enemy master state machine". That label is wrong. The only caller
is 0015B130 case 29, the player's +5 = 0x1D. The routine has the same shape
as 001703E0 (case 30) and 001729A0 (case 31).

## 1. What the originals do

### 001607D0(p): the action machine (returns 0 or 1, or a callee's value)

The routine always starts by storing +1FC = 1.0. It then switches on +1F0.
Five pad masks are read from the scratchpad (3B74, 3B76, 3B78, 3B7C, 3B7E)
and tested against D_00810E70, the held word, or D_00810E74, the pressed
word. "held X" below means `D_00810E70 & mask(X)`.

| +1F0 | Behaviour |
|---|---|
| 0 | Four tests, the first that fires wins. held 3B7E: returns 0 when +236 != 0, else +5 = 0x1E, +6 = 0, +1F0 = 0x32, +1F1 = 0, returns 1. held 3B7C: the same with +5 = 0x1D, +1F0 = 0x31. pressed 3B78: +5 = 0x21, +6 = 0, +1F0 = 0x36, returns 1 (no +236 test, +1F1 untouched). pressed 3B74: +5 = 0x22, +1F0 = 0x37. None: returns 0. |
| 1..7 | The same four tests. Each entry calls 0017C370(p) before its writes (after the +236 test). |
| 0x31 | held 3B7E: +5 = 0x1E, +1F0 = 0x32, +318 = 1, then the armed forwarding. Else, 3B7C not held: +6 = 0x63, 0016F5D0(p), returns 1. Else the armed forwarding. |
| 0x32 | 3B7E not held: when 3B7C is held, +5 = 0x1D, +1F0 = 0x31, +318 = 1 and returns 0; otherwise +6 = 0x63, 0016F5D0(p), returns 1. 3B7E held: the armed forwarding. |
| 0x33 | Returns 0 while 3B7C or 3B7E is held, else returns 1. |
| 0x27 | held 3B7E: +5 = 0x20, +6 = 0, +1F0 = 0x35, +1F1 = 0, returns 1. held 3B7C: +5 = 0x1F, +1F0 = 0x34 (same writes), returns 1. Else returns 0. |
| 0x34 | held 3B7E: +5 = 0x20, +1F0 = 0x35, +318 = 1, returns 0. Else 3B7C not held: +6 = 0x63, 0016F5D0(p), returns 1. Else the armed forwarding. |
| 0x35 | Like 0x32, with +5 = 0x1F / +1F0 = 0x34. |
| any other (0x36, 0x37, ...) | Returns 0. |

The **armed forwarding** runs these tests in order and stops at the first
that fires:

- pressed 3B78: returns 0 when D_00810C61 != 0, else returns 0017A8B0(p, 0).
- held 3B78: returns 0 when D_00810C61 == 0, else returns 0017A8B0(p, 0).
- pressed 3B74: returns 0017A970(p, 0).
- held 3B74: returns 0017A970(p, 1).
- pressed 3B76: returns (0017AAD0(p) != 0).
- None fired: returns 0.

The captures record the mask words. All 15 route captures hold 3B74 = 0x80,
3B76 = 0x40, 3B78 = 0x20, 3B7C = 0x08 and 3B7E = 0x02. They also hold +236 =
0, D_00810C61 = 0 and D_00810CA4 = 0xFF. This lane does not map the bits to
buttons. The existing claim that 3B7C is R1 and 3B7E is R2 (em_player.c,
FINDINGS) comes from earlier lanes and was not re-verified here.

### The stance tops (dispatch on +6)

0016FCF0 is +5 = 0x1D, 001703E0 is +5 = 0x1E and 001729A0 is +5 = 0x1F.
The list below is the state set they share. The differences are in the
table after it.

- **Clip request** (used by states 1 and 0x64):
  `001749A0(p, clip, 0, 0.0)`. The clip is the halfword D_00248B88[+275]
  when +5 is 0x1D or 0x1E, else D_00248C68[+275]. It is sign-extended.
- **0**: `0017B300(p, 0)`, then +6 is re-read after the call.
  - +317 == 0: +6 += 1, +278 = 0.5, +2F2 = 0, +2E = 0 (halfword), +275 = 0,
    then the entry clip.
  - +317 != 0: +6 += 2, then `0016F530(p, 0)`.
  - Then +27C = 0.5, +7 = 0, +302 = 0, +276 = 0 and +274 = 0.
- **1**: when +200 & 0x1000, +6 = old + 1, `0016F530(p, 0)` and the clip
  request. Then `anim_eval_skeleton(p)` and `copy_qw4(p + 2A0, node + 0x90)`,
  where node is `*(D_00275B40 + 0x10)`.
- **2**: the stance, with +302 = 0 before `0017ABA0(p)`. Then the lock and
  the weapon handler by +275: 0 is 00170A60(p, a1), 1 is 00171320, 2 is
  00171670, 3 is 00171B00, 4 is 00171E90 and 5 is 001723D0. +275 >= 6 calls
  nothing.
- **3**: the holster, `0016F600(p)`.
- **0x63**:
  - +6 = old + 1, +28 = N;
  - +26C = (0.5 − +27C) / N and +270 = (0.5 − +278) / N, each a sub.s then a
    div.s;
  - q = +20. Then 0x70003A20 = atan2(−q+C8, q+C0) (SDK 0011E620), stored and
    reloaded;
  - +218 = 001B1470(pi/2 + 0x70003A20);
  - then state 0x64 runs in the same frame.
- **0x64**: t = +28 (signed halfword), then +28 = t − 1.
  - t == 0: +6 = +6 + 1, +27C = +278 = 0.5, and the clip request.
  - Otherwise +27C += +26C and +278 += +270. Then:
    - +1F0 == 0x33: `anim_matrix_dispatch(p)` only.
    - Otherwise `anim_matrix_dispatch(p)`, then +1F0 is re-read. When +1F0 is
      0x32 or 0x35, or +275 == 4, or +2F2 != 0, it runs `copy_qw4` into
      +2A0. Otherwise +2D0/+2D4/+2D8 = node +C0/+C4/+C8.
- **0x65**: +6 = old + 1 and +276 = 0. Then `001749A0(p, clip, 0, 1.0)`,
  `001FBD50(p, 0x163, 0, 300.0)` and +317 = 0.
- **0x66**: the exit test, or else +C4 = `001B12B0(+218, +C4, rate)`.
- **0x6E**: `00174AC0(p, 1)`. Then, when +23F >= 2, +6 += 1 (re-read) and
  `0017C440(p, 0)`. Otherwise +25C = 0 and `0017C540(p)`.
- **0x6F**: `00174AC0(p, 1)` and `00178B90(p, 0)`. Then `0017C540(p)` unless
  +200 & 0x8000.
- **Tail** (0016FCF0 and 001703E0 run it after every state, known or not):
  `001764E0(p)` with $s1 = p, +B4 += −0.2, `00175900(p, 1)`, `001796C0(p)`.

The lock (0016FCF0 and 001729A0, D_008106E0[0]):

- State 2: when +302 (after 0017ABA0) or +275 is non-zero, D_008106E0 = 0.
  Otherwise D_008106E0 = 00185A10(p, 0) when it was 0, else 00185E30(p, E0).
- State 3 does the same, but tests only +275.

The differences between the three routines:

| | 0016FCF0 (0x1D) | 001703E0 (0x1E) | 001729A0 (0x1F) |
|---|---|---|---|
| On entry | – | D_008106E0 = 0 | – |
| State 0 clip / blend | 0x110 / 1.0 | 0x110 / 1.0 | 0x188 / 8.0 |
| State 0 extras | D_008106E0 = 0 | +2F0 = 0 (no D_008106E0 write) | D_008106E0 = 0; +290..+298 = +B0..+B8 (001031E0); +294 += 20.5; +38 = 0 |
| State 1 skeleton + copy | on the draw frame | every frame of state 1 | on the draw frame |
| State 2 | the lock; then, when D_00810CA4 == 0 and +274 != 0, +2F0 = (+2F0 + 1) & 0xFF, back to 0 from 3; 00170A60(p, 0) | +94 = 7 first; D_00810CA4 read after 0017ABA0: when 0, the +2F0 cycle when +274 != 0, then 00199220(p); when 1, 00199220(p); 00170A60(p, 1) | the lock; 00170A60(p, 0); after 00171320 / 00171670, `00172860(p, 0.015)`; after 00171B00 `00172860(p, 0.01)`; after 001723D0 `00172860(p, 0.025)` |
| State 3 | the lock (+275 test), 0016F600 | 0016F600; then +1 = 0 when +1F0 == 0x33 | the lock, 0016F600 |
| 0x63 N | 4 | 8 | 8 |
| 0x65 clip | 0x111 | 0x111 | 0x189 |
| 0x66 exit | +3C <= 4.0 (c.le): +6 = 0x6E | +200 & 0x1000: +6 = 0x6E | +200 & 0x1000: +5 = 0x14, +6 = 0, +1F0 = 0x26 |
| 0x66 rate | 0.08726647 | 0.043633234 | 0.043633234 |
| 0x6E / 0x6F | yes | yes | no (no case) |
| Tail | yes | yes | no: returns |

## 2. Where they sit in the closure

- **001607D0** is the only routine that enters +5 = 0x1D..0x22 from
  idle/walk. The other entries are 0021D530 (0x1D, 0x1E) and 00223C70 (0x1F,
  0x20); FIRST_CONTROL.md's closure table cites their addresses.
  - Callers: 00161020 (+5 = 0, idle), whose sub-state 1 calls it only
    while D_0028A9A0 == 0 and whose sub-state 2 always calls it; 001612D0
    (+5 = 1, walk); 0016B790 case 3; and the stance weapon handlers
    00170A60, 00171320, 00171670, 00171B00, 00171E90 and 001723D0. The
    caller list comes from the decomp C, and 00161020's C is NEARMISS.
  - None of these callers is translated yet.
- **Exits** from the stance tops:
  - 0x6E goes through 0017C440 (re-entry) or 0017C540 (hand-off, which
    always ends with +4 = 1). 0x6F goes through 0017C540.
  - 001729A0 0x66 exits to +5 = 0x14 (0016B8A0, already in the closure).
  - 0016F5D0 (from 001607D0) starts the 0x63 blend back.
  - The weapon handlers' own exits were not followed by this lane (section 6).
- **Reachability in AREA11 is not decided.** No route beat reaches +5 =
  0x1D..0x22: the 15 route traces show only +5 0, 1, 2, 5, 6, 8, 0xB, 0xC,
  0x1C and 0x25. The captures hold D_00810CA4 = 0xFF, but none of these four
  routines tests it before entering a stance. 001607D0 is on the live
  idle/walk path, so pressing the buttons would enter the stances. Where
  that leads depends on the untranslated handlers.

## 3. Translation

`em_player_weapon_states_a.c` works on the raw 0x320-byte record
(`EmPlayerLiveActor`) by original offsets:

- Every original callee is a worker (`EmPlayerWeaponWorkers`), with two
  exceptions translated in place: the byte-matched leaves copy_qw4
  (00102958, a 64-byte copy) and 001031E0 (a 3-word copy).
- The three words the original loads directly come through data workers:
  - `clip_id`: D_00248B88 / D_00248C68 by index;
  - `bone(4)`: the 16 words at `*(D_00275B40 + 0x10) + 0x90`;
  - `link20`: the +C0 / +C8 words of the object at +20.
- The globals live in `EmPlayerWeaponScene`:
  - the five mask halfwords, D_00810E70/E74 and D_00810C61, read-only;
  - D_008106E0[0], D_00810CA4[0] and 0x70003A20.
- Each entry point checks, before its first write, that the scene and every
  worker it can reach are bound. The `em_player_weapon_*_bound` functions
  list the sets. A missing one faults (-1) with nothing written. A worker
  that returns a negative value stops the routine at once (-1), leaving the
  writes made before it.
- Every COP1 operation uses `em_ee_float.h`:
  - sub.s, div.s and add.s in 0x63/0x64;
  - the neg.s of the atan2 argument;
  - pi/2 + angle in operand order (pi/2, angle);
  - +294 + 20.5 and +B4 + (−0.2);
  - the c.le of 0016FCF0's 0x66.

  The float constants are the bit patterns the instructions build.

## 4. Binding (for the coordinator)

**Stage slots.** Set `EmPlayerStageWorkers.state[0x1D]` to
`em_player_weapon_state1D`, `[0x1E]` to `em_player_weapon_state1E` and
`[0x1F]` to `em_player_weapon_state1F`. Each `state_context` is one
`EmPlayerWeaponStates { &workers, &scene }`.

**001607D0.** `em_player_weapon_001607D0(&states, actor, &result)` is the
worker for whoever translates 00161020, 001612D0, 0016B790 and the stance
handlers. Until then the live port's R2 gate (`em_player.c`, "R2-HELD ARMED
STANCE 0x1E") and em_weapon.c's trigger handling are the legacy stand-ins
for it. Retire them once 001607D0 runs from the translated idle/walk.

**Workers:**

| Field | Original | Existing translation / source |
|---|---|---|
| `w0017C370` | 0017C370(p) | `em_player_0017C370` (em_player_stage_workers.h), through an adapter to its `EmPlayerStageHost` context |
| `wrap` | 001B1470 | `em_player_001B1470` (bits in/out, em_player_stage_workers.h) |
| `atan2` | SDK 0011E620 | `em_sdk_math_original_0011E620` / `_float_0011E620` (em_sdk_math_original.h), through a bits adapter |
| `approach` | 001B12B0(target, current, rate) | `em_player_slide_approach` (em_player_slide.h), through a bits adapter, as PLAYER_HANG.md binds it |
| `link20` | loads of `*(p+20)` +C0/+C8 | the same shape as `EmPlayerStageCallees.link20` |
| `heading`, `reentry`, `handoff`, `translate` | 00174AC0, 0017C440, 0017C540, 00178B90 | `heading`: em_player_heading.h, which is not a live-actor signature and needs an adapter; `reentry`: the motor module (em_player_motor.h); `handoff`: `em_player_reaction_0017C540`, a void function, wrapped; `translate`: `em_player_recovery_translate_worker` (EmPlayerRecoveryLive context). This follows PLAYER_HANG.md's binding table |
| `probes`, `floor`, `fall_check` | 001764E0, 00175900, 001796C0 | `player_states_wall_probes`, `player_states_floor_service`, `player_states_fall_check` (em_player.h). 001764E0 tests ($s1 & 4) of its caller: here $s1 is the record address (0016FD0C / 00170404), as `EM_PLAYER_LAND_S1_RECORD` in em_player_fall.h |
| `request`, `sound`, `skeleton`, `matrix` | 001749A0, 001FBD50, anim_eval_skeleton (001C6DA0), anim_matrix_dispatch (0017A130) | the display / sound bindings shared with the other state lanes; anim_matrix_dispatch has no translation |
| `bone` | `*(D_00275B40 + 4 * slot) + 0x90`, 16 words (slot 4) | the display's bone work array (the node the skeleton evaluation just wrote) |
| `clip_id` | D_00248B88 / D_00248C68 halfwords | not exported yet. Needs an exporter from the user's ELF into ignored `assets/`, like `EM_PLAYER_CLIP_RATE_PATH`. Never embedded. |
| `w0016F5D0`, `w0017A8B0`, `w0017A970`, `w0017AAD0`, `w0017B300`, `w0016F530`, `w0017ABA0`, `w00185A10`, `w00185E30`, `w00199220`, `w00170A60`, `w00171320`, `w00171670`, `w00171B00`, `w00171E90`, `w001723D0`, `w00172860`, `w0016F600` | the weapon / aim routines | untranslated: the live states stay gated until they are |

**Scene** (one instance, shared with every worker that touches the same
words during the call):

- the mask halfwords: the scratchpad words 0x70003B74..7E;
- `d810E70` / `d810E74`: the pad words from the 001B5940 pad block
  (em_input);
- `d810C61`: D_00810C61;
- `d810CA4`: the canonical progress byte (`em_scene_progress_at(...,
  0x00810CA4, 1)`);
- `d8106E0`: D_008106E0[0], also used by the untranslated lock workers and
  legacy em_weapon.c;
- `spad3A20`: the 0x70003A20 scratch word. Other routines use the same word
  (EmPlayerStageGlobals.spad3A20, EmPlayerLandScratch.s3A20); unify or
  mirror it.

**Makefile.**

- Test target: `test-player-weapon-states-a-reference:` running
  `python3 tools/test_player_weapon_states_a_reference.py`.
- When bound, add `src/game/em_player_weapon_states_a.c` to `COMMON`.
  The lane build with it appended links with zero warnings.

## 5. Verification

`tools/test_player_weapon_states_a_reference.py` executes the original
instructions of 001607D0, 0016FCF0, 001703E0 and 001729A0, with copy_qw4 and
001031E0 unhooked, from the user's pinned ELF.

- **Float model.** The interpreter is FallEE: COP1 and VU0 go through
  `tools/ee_float_model.py`. It is subclassed here, and no shared file is
  edited.
- **Callee set.** The test asserts that the hooked set is exactly the
  routines' jal targets minus the two leaves (35 targets).
- **Scripted callees.** Every other callee is hooked and scripted per case.
  The script can do any of these:
  - rewrite record bytes the routine reads later (+302, +275, +274, +1F0,
    +200, +23F, +6, +2F2, +2F0, +5, +28, +317);
  - rewrite D_008106E0 / D_00810CA4;
  - replace the bone node words;
  - return ints and floats.

  The native workers replay the same script.
- **Compared per case:**
  - all 0x320 record bytes;
  - D_008106E0, D_00810CA4 and 0x70003A20;
  - the 001607D0 return value;
  - the call sequence with every argument (floats as bits, and the $s1 of
    001764E0).
- **Store check.** Every store the original instructions make must land in
  the record, D_008106E0, 0x70003A20 or the stack (asserted).
- **Cases:**
  - Synthetic records.
  - Captured states: the player record of each of the 15 route captures,
    with their pad masks, globals and the real object at +20. +1F0, +5, +6
    and the pad words are set per case. The bone node is synthetic in both
    kinds of case.
- **Fault checks:**
  - Fault-stop cuts: a worker fails at a random call, and the native side
    must return -1 having made exactly the calls up to it.
  - Missing-worker checks, per entry point and field. A reachable missing
    field returns -1 with no call and nothing written. An unreachable one
    leaves the run unchanged.
- **Branch coverage.** Every one of the 167 conditional branches of the four
  routines is asserted taken and not taken.

Results (2026-09-23):

- **Default** (EM_TEST_FULL unset): 30,000 of 120,000 synthetic cases and
  600 of 15,750 captured-state cases. 74,634 worker calls identical, all 167
  branches both ways, 4,488 fault-stop cuts, 152 missing-worker checks. 6.5 s
  (7.1 s wall).
- **EM_TEST_FULL=1:** all 120,000 synthetic and 15,750 captured-state cases.
  312,682 worker calls identical, 18,891 cuts. 15.6 s (17.1 s wall).
- **Mutations.** Each of these deliberate mutations was caught:
  - dropping the +1F1 store;
  - div.s turned into mul.s;
  - dropping 001703E0's D_008106E0 clear;
  - `>=` turned into `>` on +23F;
  - skipping the +1F0 re-read after anim_matrix_dispatch.

## 6. Limits

- **No original run reaches the stances.** The evidence is instruction-level
  over synthetic and captured records, not a route replay. No capture holds
  +5 = 0x1D..0x22, and the route never presses the stance buttons.
- **The weapon/aim workers are untranslated.** These are 0016F5D0, 0017A8B0,
  0017A970, 0017AAD0, 0017B300, 0016F530, 0017ABA0, 00185A10, 00185E30,
  00199220, 00170A60, 00171320, 00171670, 00171B00, 00171E90, 001723D0,
  00172860, 0016F600 and anim_matrix_dispatch. Their writes of +4/+5 were
  not followed, and the closure through them stays open until they are
  translated. The +5 = 0x20..0x22 routines 00173000, 001735C0 and 00173E60
  belong to the sibling lane player-weapon-states-b
  (`em_player_weapon_states_b.*`). That lane keeps the same shared stance
  callees (0016F530, 0016F600, 00170A60 .., 0017ABA0, 0017B300, 00199220)
  as workers, so one binding of each serves both modules.
- **The clip tables are not exported.** D_00248B88 / D_00248C68 reach the
  native side only through `clip_id`. The oracle serves that worker from the
  ELF image.
- **D_00275B40 is not settled.** The captured D_00275B40 (0x8102F0 = player
  +0x40) belongs to whichever actor ran last in the frame. What it points to
  during the player's stage is the binder's to supply; this lane did not
  settle it. The oracle uses a synthetic node table.
- **The mask-to-button mapping is not verified.** Which pad button each mask
  bit is, and whether AREA11 can enter a stance at all (D_00810CA4 = 0xFF),
  are open questions. The translation does not depend on either.
