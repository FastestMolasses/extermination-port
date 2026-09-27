# Husk creature, its partner's hit, and the fan's tail (`husk_fan`)

Lane "husk_fan" (build lane `b15/husk_fan`, 2026-09-27). It closes the pieces
that `SCRIPT_DOOR_FAN.md` section 5.3 and the C7 chain's OWNERS / RNGORDER
limitations list as missing before census L24 can bind:

- the husk creature 0x825940's lifecycles 1 and 4, with their overlay helpers;
- the creature's lifecycle-0 rand draw, its bone-slot writes, its child spawn
  (001AFA90) and its hull (001A2370), checked against the captures;
- the partner 0x827490's hit reach (001EFE00, 001B11E0, 001B1190) as far as
  the first level can reach it;
- the fan 0x827630's tail consumers: 001B0C60(1,1,4), `D_008107D8 |= 0x80`
  and the player-hit writes.

Files (all new; nothing tracked was edited):

- `src/game/em_husk_fan.{h,c}`: the translations.
- `tools/test_husk_fan_reference.py`: the original-instruction oracle.
  - Default: about 6 to 8.5 s wall, about 9 s user CPU. `EM_TEST_FULL=1`:
    about 10 s wall, about 20 s user CPU (measured 2026-09-27, 4 workers).
  - At most 4 worker processes.
  - It builds privately into `build/b15/husk_fan/`.

Addresses are original runtime addresses. The AREA11 overlay listing names
each function 0x40 lower (its vram is the MWo3 header address).

**Correction to SCRIPT_DOOR_FAN.md section 6.** The creature's two overlay
helpers are runtime **0x826F30** and **0x827400**, not 0x826F70 / 0x827440.
The jal instructions of 0x825940 encode 0x826F30 and 0x827400.
- 0x826F30 is listed as `..._00826EF0` (a 0x40-byte prologue) falling through
  into `..._00826F30`.
- 0x827400 is listed as `..._008273C0` falling through into `..._00827400`.

## 1. What was missing, and what it is now

| Piece | Before | Now | Evidence |
|---|---|---|---|
| 0x825940 lifecycle 4 (0x825B74..0x826190) | faulted (untranslated) | `em_husk_fan_creature_tick` | oracle, every branch both ways |
| 0x825940 lifecycle 1 (0x826190..0x826D60) | faulted | same | oracle, every branch both ways but one dead arm (3.3) |
| 0x826F30 sight probe | untranslated | inside the tick (`sight`) | oracle |
| 0x827400 shot node | untranslated | inside the tick (`shot`) | oracle |
| lifecycles 0, 0x64, 2, 3, other | `em_husk_creature_tick` (verified) | **reused**; one entry point through a memory adapter | oracle over the adapter, all 24 AREA11 captures |
| lifecycle-0 rand draw (0x8259F0) | port draws nothing at AE+1 | reused setup, bound as below | C7 per-call capture + all captures (4.1) |
| bone-slot writes (bone 3 +0x78, bone 2 +0x74) | views only | read and written through D_00275B40's slots | captures (4.1) + oracle |
| 001AFA90 child / shot node | interim `spawn_child_record` | worker; `em_actor_pool_alloc_001AFA90` is the verified translation (byte-matched) | reused |
| 001A2370 hull | worker | worker; `em_actor_cells_retransform_001A2370` is verified and live | reused |
| 001B1190 taken-bit set | no verified implementation for the partner's `w_001B1190` | `em_husk_fan_001B1190` | oracle (30 cases) |
| 001B11E0 taken-bit test | verified, static in `em_actor_roster.c` | reused (the chain must export it) | test_actor_census_reference |
| 001EFE00 (partner FX 0x80000045) | verified (`em_player_misc_001EFE00`); spawn view unbound | reused; the spawned node's behaviour is now translated | see 2.5 |
| 0021AAC0 (the node 0x80000045 spawns) | untranslated | `em_husk_fan_0021AAC0` | oracle (65 runs incl. spawn to free) |
| 001EFEB0 (0021AAC0's spawn) | untranslated | `em_husk_fan_001EFEB0` | oracle |
| 0021A500 (the 0x8000003B strip node) | untranslated | `em_husk_fan_0021A500` | oracle (32 runs) |
| fan exit-or-bit consumers | unverified | 001B0C60 and Roger's departure are translated and live; reachability settled (5.3) | beat 15 capture |
| fan player-hit consumer | unverified | the original 0021C440 run over the original fan's writes equals the native `em_player_stage_reaction` | chain oracle (4.3) |

## 2. Behaviour

### 2.1 The creature 0x825940

The creature dispatches on its lifecycle byte +0x04.
- Lifecycles 0, 0x64, 2, 3 and "other" are as `SCRIPT_DOOR_FAN.md` 2
  describes.
- Lifecycle 4 (patrol) is entered only after D_00810788 (event flag 0x30) ==
  0xFF.
- Lifecycle 1 (aim and fire) is entered from lifecycle 4.

Record words used by lifecycles 1 and 4:
- +0x200 is a toggle or fire counter;
- +0x204 is the target record;
- +0x208 is the alert countdown;
- +0x20C / +0x214 are sway timers, with +0x210 / +0x218 their rates;
- +0x1F4 / +0x1F8 / +0x1FC are the gun-sway rate, step and angle;
- +0x28 / +0x2A are the tick counters;
- +0x36 is the hit.

Bone 2 is the D_00275B40 slot 2 and bone 3 is slot 3. The pool walk sets
D_00275B40 = node + 0x110, so the slots are the record's +0x118 / +0x11C
words.

**Alert block** (both lifecycles, while +0x208 > 0). +0x208 counts down.
- **From 31 ticks left down**, the child's +0xA0 quad ramps.
  - The ramp value is (0x80 − 4·n)/128. It is stored at 0x70003A20.
  - Lifecycle 1 puts it in x; lifecycle 4 puts it in y. The quad's w is
    0.25.
  - Then the creature's bone-3 +0x90 matrix is copied onto the child's bone 3
    (00102958).
- **Otherwise, the head sway:**
  - bone 2 +0x74 += +0x210, clamped to ±1.1344 (0x3F91361E). A clamp negates
    the rate.
  - When +0x20C runs out, cue 0x428 plays (range 300) and +0x20C is redrawn
    as (30·(rand>>16))>>15. Bit 2 of the new value negates +0x210.
- **Then the gun sway:**
  - +0x1FC += +0x218, wrapped to −π past π;
  - bone 3 +0x78 = −0.8290 + 0.3054·sinf(+0x1FC);
  - when +0x214 runs out, it is redrawn the same way, and bit 3 negates
    +0x218.
  - The child's +0xA0 quad is then zeroed.
- The block ends with 001C6380, 001A2370(bone 3 + 0x90), 001B17A0 and the
  +0x4C draw, then +0x36 = 0.

**Lifecycle 4 (patrol)** with +0x208 ≤ 0:
- The child quad is (0, 1, 0, 0.25), and the bone-3 matrix is copied as above.
- +0x28 counts down. Below 0 it is redrawn:
  - as 60 + (180·(rand>>16))>>15 while +0x200 is set;
  - as 300 + (300·(rand>>16))>>15 otherwise;
  - and +0x200 toggles.
- While +0x200 is set:
  - cue 0x423 (range 60) plays when (+0x28 & 0x2F) == 2;
  - bone 2 +0x74 += +0x1F4, clamped to the side the rate's sign points to;
  - the gun sway runs as above with +0x1F8.
- The tail is 001C6380, 001A2370, 001B17A0, the draw and the sight probe.
- **Shot reaction.** A hit (+0x36) with +0x208 == 0 sets +0x208 = 480 and
  redraws both sway timers. Bit 3 negates +0x210 and bit 2 negates +0x218.
- **Target.** Once +0x204 holds a target, the creature goes to lifecycle 1:
  - +0x224 = 1, +0x28 = −43, +0x2A = 300, +0x200 = 0;
  - cue 0x424 plays when the frame counter 0x70003B68 & 0x3F == 0.

**Lifecycle 1 (aim)** with +0x208 ≤ 0:
- **Cue and child.** Cue 0x424 plays on the same frame test. The child quad is
  (1, 0, 0, 0.25), and the matrix is copied.
- **Heading.**
  - The direction target +0xB0 − bone 2's translation (bone 2 +0xC0) is
    flattened (y and w zeroed) and normalized (00102760).
  - d = −(nx·bone2 +0xB0) − nz·bone2 +0xB8 is computed with the EE
    accumulator (mula/madd). It is stored at 0x70003680.
  - d < −0.011635 turns bone 2 +0x74 by −0.011635, and d > 0.011635 turns it
    by +0.011635.
  - Otherwise, when |x| > 0.001 (0011DF78), bone 2 +0x74 snaps to
    wrap(heading − rot.y +0xC4), where the heading is π − atan(z/x) for
    x < 0, else −atan(z/x). The wrap is 001B1470; 0x70003684 holds the
    intermediate values.
  - Each clamp of ±1.1344 costs +0x2A four.
- **Pitch.**
  - pitch = atanf(dy / sqrtf(dx² + dz²)) (0011E748, 0011DBB8, madd).
  - Bone 3 +0x78 steps by ±0.011635:
    - it rises when pitch > +0x78 − step;
    - it falls when pitch < +0x78 + step;
    - otherwise it takes the pitch (a dead arm, 3.3).
  - It is clamped to [−1.1344, −0.5236]. Each clamp costs +0x2A four.
- **Tail and sight.** Then 001C6380, 001B17A0 and the draw (no hull here).
  +0x28 += 1, then the sight probe runs. A result of 2 resets +0x2A = 300;
  anything else costs one.
- **Fire.** While +0x200 is set, it counts up. At 14, with the sight's result
  non-zero, the creature fires:
  - cue 0x425 plays and 0x827400 spawns the shot;
  - 0x700038A0 = the probe point, and 0x700038B0 = the hit face's
    +0x24..+0x2C with w = 1.
  - **On an actor** (probe kind 0x700031D8 == 1):
    - class ≠ 0 gets FX 0x80000007 and hit +0x36 = 5;
    - the class-0 player without +0 bit 1 gets FX 0x80000006, +0x224 = 5.0
      and +0 |= 2, and +0x70 = normalize(0x700031A0 − 0x70003190) with w 0.
  - **Otherwise** the byte +0x1A of the face picks the FX: 0x5C → 0x80000067,
    0x5B → 0x80000026, 0x5A → 0x8000002C, else 0x80000003. That byte is
    re-read after 0019B6C0 when 0019B6C0 hits. When it does not hit, surface 5
    → 0x8000002C, else 0x80000003.
  - +0x200 = 1 after a fire, or when the sight saw nothing at 14.
  - With +0x200 clear, a positive +0x28 sets +0x200 = 1 and +0x28 = 0.
- The shot reaction runs as in lifecycle 4, then +0x36 = 0.
- **Lost target.** A negative +0x2A returns the creature to lifecycle 4:
  - +0x224 = 0 and +0x204 = 0;
  - +0x1FC = wrap(asinf(−(+0x78 + 1.1344)/−0.8290)), which recovers the sway
    angle from the pitch;
  - +0x28 = 300 + (300·(rand>>16))>>15, and +0x200 = 1.

### 2.2 The sight probe 0x826F30 (creature, gun matrix)

- **Beam end.** The sight vector (60, 0, 0, 0) is transformed into
  0x700038A0. The gun-tip point (3, −2, 0, 1) is transformed into a local
  (001026A0 twice, 001028B8). w = 1.
- **First probe.** 0019AA80(tip, end, 0x20). A hit copies 0x700031B0 into the
  end point and marks "blocked" (2).
- **Second probe.** 0019A570(tip, end, 7, 0x20), with 0x700031D0/D4/D8 saved
  first.
  - A miss restores those three words.
  - A hit measures |0x700031B0 − player +0xB0|² (001028D0, 00102738). The
    value is stored at 0x70003A20.
  - Beyond 10000 the result is "clear" (1). Otherwise, an actor hit whose
    byte +3 is in 0x10..0x13 is "clear" (1); every other hit is "blocked" (2).
- **Blocked (2).**
  - An empty +0x204 takes the hit actor (kind 1).
  - In lifecycle 4, the sight dot is drawn: 001CD520(0, 2, point,
    0x20045BA5154222DC, colour 0x80000040 + a rand-picked 0..31, 3, 3, 2),
    and a beam: 001E2BA0(tip, point, (0.8, 0, 0), 100).
  - Otherwise, with +0x200 ≥ 13, the muzzle beam is drawn from bone 3 through
    the 0x82A730 quad.
  - The result is 2 when +0x204 is the hit actor, else 1.
- **Clear (1).** A beam to 0x700038B0 is drawn. The result is 0.

### 2.3 The shot node 0x827400

- 001AFA90(0xC) allocates a node.
- It gets +0xB0 = the gun matrix translation and +0xD0 = the gun matrix.
- +0x100 = that matrix × the 0x82A740 quad.
- +0x10 = 001F5040.

### 2.4 001B1190

With (a0 & 0xFF) ≠ 0, it sets bit (a0 & 0x1F) of the word at D_00810860 +
(D_00810700 << 5) + ((a0 & 0xFF) >> 5)·4.

### 2.5 The partner's hit reach

The partner's lifecycle 1 reacts to a hit (`em_husk_partner_tick`). It calls
001EFE00(0x80000045, partner), which is `em_player_misc_001EFE00`. That calls
001EF9D0.

**The spawned node.** Record 0x45 of the global effect table (read from the
user's ELF) gives:
- class 0xC;
- +3 = 0x63;
- subtype 0;
- callback **0x21AAC0**;
- kind 1, so 001D80E0 point light, which `em_effect_original` translates;
- no sound.

**0021AAC0** (the node's +0x24 is the partner):
- **State 0:**
  - six phases rand/2³¹ go to +0x268.., with zeros at +0x250..;
  - the node takes the partner's +0xD0 matrix;
  - its +0xB0 = that matrix × (0, 0, 1, 1).
- **State 1** counts +0x288 up:
  - **From 31:** every 6th tick adds a rising point, up to six, each 8.0
    lower than the last (001028B8 with +0xB0). Each point draws two packets:
    - 001CCF70 gives the key;
    - 001CFA60 gets the phase and scale;
    - 001CFBE0 is called with kind 2 and table 0x2669C0, then kind 1 and
      table 0x266A50;
    - the phase then rises by 0.02.
  - **Below 60:** every 10th tick spawns 001EFEB0(0x8000003B, the π/2-rotated
    matrix at +0xB0). The new node gets +5 = 0, +0x1F0 = 12, +0x1F4 = 48.0
    and +0x1F8 = 0.5.
  - **At 60** it sets the partner's +0x04 = 3, so the partner frees itself on
    its next tick.
  - **At ≥ 121** it sets its own +0x04 = 3.
- **2 and 3** free the node.

**001EFEB0** calls 001EF9D0(id, m + 0x30, 1.0). When that returns a node, it
then calls 00102958(node + 0xD0, m).

**0021A500** (record 0x3B: callback 0x21A500, kind 1):
- **State 0:**
  - it seeds n − 2 lateral offset pairs, each 2.5·sinf(2π·rand/2³¹) (n =
    +0x1F0), plus a colour seed at +0x1FC;
  - +0x1F4 is divided by n − 1.
- **State 1:**
  - it builds n points into D_00821400 through the node's +0xD0 matrix;
  - 001CE860 draws them with the grey 192·(1 − g);
  - g (+0x200) grows by (1 − g)/8 + 0.0001.
- **Ending, with +5 == 0** (0021AAC0's spawns): the node ends at g > 1.0,
  after 55 ticks in the lockstep run.
- **With +5 == 1**, it also draws two end sprites per tick:
  - the sprites use 001CFB50 / 001CFBE0 with table 0x266930, and 001CD520
    with colours from float_to_int;
  - the colour seed steps ×37 + 11;
  - the node ends when +0x204 passes 1.1.

## 3. Verification (`tools/test_husk_fan_reference.py`)

### 3.1 How it compares

The oracle is the shared `FallEE` core, with COP1 through
`tools/ee_float_model.py`. It runs over copy-on-write views of the captured
AREA11 RAM.
- The test first asserts that the executed code equals the user's
  `extract/OVERLAY/AREA11.BIN` for the overlay, and the pinned ELF for the
  boot functions, in every image used.
- Route beat 15's snapshot (the AREA01 arrival) is excluded by that check.

**Callees:**
- **Run as original instructions, nested.** The SDK float leaves (0011E2A8,
  0011DF78, 0011DBB8, 0011E748, 0011E520, 001B1470, 001281C0) and the vector
  leaves (00102948, 00102958, 001026A0, 001028B8, 001028D0, 00102738,
  00102760, 001031E0, 001029C0, 00102B08, 00102918, 00103230). The native
  workers replay their logged outputs after their inputs are asserted equal.
- **Hooks that log and answer from a per-case script.** Every other callee.
  The probe hooks also write a scripted result block to 0x70003190..0x700031DB.
- **Special case: em_husk_creature_tick's two inlined 00102948 copies**
  (setup) execute unlogged.

**The native side** runs over its own copy of the same memory
(`EmHuskFanMem`). Per case the test asserts:
- the result;
- the ordered calls, with vector arguments compared by address (or "local")
  and by content;
- **memory at every callee entry**: every byte either side writes during the
  case, compared at each call (a two-pass run finds the set);
- every such byte's final value.

### 3.2 Cases (default / full)

| Group | Default | Full |
|---|---|---|
| lifecycles 1/4, random states (alert window, sways, clamps, sight outcomes, distances including exactly 10000.0, fire kinds) | 260 | 1,600 |
| directed: aim arms ×3, sway wrap ×2, fire kinds × surfaces × alloc | 38 | 38 |
| pinned thresholds (3.3): heading d == −step and == +step, bone 2 +0x74 == each yaw clamp, bone 3 +0x78 stepping exactly onto each pitch clamp | 6 | 6 |
| lockstep runs | patrol → sees the player → aims → fires three shots (90 ticks); aim → loses the target → patrol (14); alert countdown through the ramp (42) | same |
| delegated lifecycles through the adapter | 42 runs: setup (flag 0/1/0xFF, 001B0FD0 0/1, child or none), 0x64, 2 (angles, countdowns, +0x224), 3, 0x63, 0xFF, plus 4 captures | 62 runs: the same with 24 captures |
| 0021AAC0 | 65 (every state and count boundary, spawn to free in 122 ticks) | 65 |
| 0021A500 | 32 (every state, +5 = 0/1/2, n = 0..3/12, the end tests; two lockstep runs to the end) | 32 |
| 001EFEB0, 001B1190 | 2, 30 | 2, 30 |
| native fail-stop | NULL worker, failing worker, unmapped address, latched fault | same |
| fan hit → 0021C440 chain (4.3) | 6 | 72 |

### 3.3 Branch coverage and mutations

**Branches.** Every conditional branch of 0x825940 (the whole function),
0x826F30, 0x827400, 0021AAC0, 0021A500 and 001EFEB0 is observed both ways,
with one listed exception:
- runtime 0x8267CC, lifecycle 1's "take the pitch" arm;
- it needs the pitch ≤ +0x78 − step and ≥ +0x78 + step at once, which only
  rounding could allow at |+0x78| ≥ 2²⁰·step;
- the pitch is an atanf result in [−π/2, π/2], so that never happens.

**Mutations.** Nineteen injected defects each fail the default run:
- the alert timer bit;
- the aim step sign, the heading operand order, the pitch clamp;
- the fire threshold;
- the sight's 10000 compare (`<=` → `<`) and the sight colour;
- the shot handler, the player hit value;
- the patrol timer constant;
- the adapter without write-back;
- 0021AAC0's parent tick, spawn period, rise and child width;
- 0021A500's growth, colour LCG and offset operands;
- 001EFEB0's position offset.

One candidate was not a defect: replacing madd with a separate mul and add. It
is identical in the EE model for finite values (`em_ee_madd_bits` differs only
by saturation), so it was dropped from the list.

**Pinned thresholds (review round).** The review's 43-mutant sweep left one
survivor: the heading test `d < −step` (runtime 0x826544) weakened to
`d <= −step`. The random and directed cases never produced d exactly equal
to a threshold. Six pinned cases (`boundary_cases` in the test) now meet
every aim threshold with equality, and each proves from the oracle's own log
that it did:
- **d == −step and d == +step.** Bone 2 stands 4 units short of the target on
  x and level on z, so the normalized flat direction the original's 00102760
  logs is exactly (1, 0, 0, 0) and d = −(bone 2 +0xB0). +0xB0 is seeded to
  +step (0x3C3EA2F1) or −step. The case recomputes d from the logged
  normalize output with the EE model, asserts it equals the threshold, and
  asserts that the original took the snap arm (it called 0011DF78).
- **Bone 2 +0x74 == −1.1344 and == +1.1344.** The target is straight ahead
  on z, so d is ±0 and |x| = 0 (no snap). +0x74 stays on the clamp value;
  the case asserts no clamp ran (+0x2A ends at 300 − 1).
- **Bone 3 +0x78 steps exactly onto −0.5236 (rising) and −1.1344
  (falling).** The start value is the float c with c ± step == the clamp
  exactly (searched with the EE model). The case asserts the arm from the
  logged atanf pitch, the landed value, and no clamp cost.
Both sight probes miss in these cases, so the sight cannot reset +0x2A and
hide a clamp's cost of four.

Each of the six weakened or strengthened compares is killed by its pinned
case, checked once with `EM_HUSK_FAN_MODULE` on single-edit copies of
`em_husk_fan.c` (the copies were deleted afterwards):
- the named survivor (`lt` → `le` at −step) fails "heading d == −step" (call
  differs: the original calls 0011DF78, the mutant does not);
- `le` → `lt` at +step fails "heading d == +step";
- the four clamp compares (yaw min/max at runtime 0x826694 / 0x8266D4, pitch
  min/max at 0x826808 / 0x826848) fail their pinned case on memory at the
  next callee's entry.

## 4. Captures

### 4.1 The creature in every AREA11 image (24)

Every image has:
- the creature at 0x7A6AD0 in lifecycle 0x64;
- D_00810788 = 0;
- +0x28 = 584;
- bone 3 (+0x11C = 0x7D7B30) +0x78 = 0xBF91361E, bone 2 +0x74 = 0;
- the 0x7A child at 0x7ADD60 with +0xA0 = (0, 0, 0, 0.25).

The C7 per-call rand capture (`build/s87/c7cap/rng/newgame/rand.jsonl`) has
exactly one call returning to 0x8259F0. Its position in frame AE+1 is after
001D7D44, 001D7DD4 and 001F112C, and before 0x8236B4.
- It returned 0x794BDF32, which gives 300 + (300·0x794B)>>15 = 584.
- The native setup fed that value writes +0x28 = 584, bone 3 +0x78 =
  0xBF91361E and the child quad, equal to the captures.
- So the dormant wait draws nothing; the one draw is lifecycle 0's.

Each image's creature is also ticked once through the adapter against the
original.

### 4.2 Lifecycles 1 and 4 are revisit content

No capture reaches lifecycles 1 or 4 (SCRIPT_DOOR_FAN.md 6: D_00810788 stays 0
in the first visit). Their evidence is the oracle over captured RAM with
seeded states.

### 4.3 The fan's player hit and its consumer

The original fan 0x827630 runs on record [2] (0x7A7690) of each image with:
- the fast arm (spin 0.2), B8 = 0;
- the player at +0xA0 = (330, 300, 156..166.5) with +0 = 1.

It writes +0 = 3, +0x0F = 6, +0x224 = 5.0 and +0x70..+0x7C = (0, 0, 1, 1),
and calls no 001B0C60.

The player record it leaves then goes through
`test_player_stage_workers_reference.run_case('reaction', ...)`. That executes
the original 0021C440 and compares the native `em_player_stage_reaction` field
for field and call for call. Results:

| Health before | +4 | +5 | +F | Health after |
|---|---|---|---|---|
| 100 | 2 | 0x11 (0021E9C0, live in `em_player_closure_live`'s reaction table) | 0x86 | 95 |
| 5 or 4 | 2 | 1 (the death state) | 0x86 | 0 |

## 5. Binding (for the chain)

### 5.1 The creature 0x825940 (area11 record at 0x7A6AD0)

**Replace** `tick_enemy_00825940` in `em_area11_bindings.c`, which runs the
legacy `em_enemy_update` group plus the inline `spawn_child_record` child, with
`em_husk_fan_creature_tick(record_address, &mem, &workers, &fault)` per pool
walk. The pool walk must set D_00275B40 = node + 0x110 first, as for every
owner.
- Returns: 1 allocated, 0 freed, −1 fault. The fault codes are
  `EM_HUSK_FAULT_*`, the same values as `EM_SCENE_FAULT_*` 1 and 2.
- **Retire at the same time:**
  - the husk pair from `em_enemy`'s aggregate (no node may run twice);
  - `spawn_child_record` for the 0x7A child: the creature's lifecycle 0 now
    allocates it through 001AFA90;
  - the rand-order check's husk exception, in `tools/rand_order.py` (which
    `tools/test_rand_order.py` imports), not in the test file itself:
    - `KNOWN_FIRST_DIFFERENCE = (1, 0x825940)` names the missing draw.
      `check_opening` asserts that the first difference *is* that draw, so
      after binding it must name the next real divergence, or the check
      must assert that there is none;
    - the skeleton assertion in `check_opening` (each bad frame must be AE+1
      and equal the original's frame minus 0x825940) must be removed or
      rewritten, because the port's AE+1 frame then contains 0x825940;
    - the summary line that mentions "the husk 00825940's missing
      lifecycle-0 draw" must change with it (RAND_ORDER.md section 3 too).
- **Freed nodes.** A tick whose lifecycle frees the node (001AFC10) returns 0.
  The pool walk must not tick that node again. `em_husk_fan_creature_tick`
  builds a fresh view per call and keeps no use-after-free latch (the
  reused `em_husk_creature_tick`'s EM_HUSK_FAULT_FREED latch does not
  survive between calls); the original has no guard either.

**`EmHuskFanMem`** must map every byte below. Anything unmapped faults at its
address.
- **Creature record:** +0x00, +0x04, +0x28, +0x2A, +0x36, +0x4C (read), +0xB0..+0xCF,
  +0x118/+0x11C (slot words), +0x1F4..+0x227.
- **Bone records** named by the slots and by child +0x11C: +0x74, +0x78,
  +0x90..+0xCF.
- **Child record** (the 0x7A node; the shot node too): +0x03, +0x0D, +0x0E,
  +0x10, +0x2E, +0x54, +0x56, +0x9A, +0xA0..+0x10F, +0x11C.
- **Target / hit record** (0x700031D4, +0x204): +0x00, +0x02, +0x03, +0x36,
  +0x70..+0x7F, +0xB0..+0xBF, +0x224.
- **The hit face** (0x700031D0): +0x1A, +0x24..+0x2F.
- **Scratchpad:** 0x70003190..0x700031DB, 0x70003600..0x7000361F,
  0x70003680..0x70003687, 0x700038A0..0x700038DF, 0x70003910..0x7000391F,
  0x70003A20, 0x70003B68.
- **Globals:** D_00275B40, D_00810360 (player +0xB0), D_00810788, and the
  overlay quads 0x82A730 / 0x82A740.

In the first visit only lifecycle 0 (once, at AE+1) and 0x64 run. They touch
the record, bone 3 +0x78, the child record, D_00275B40, D_00810788 and
0x70003A20.

**Workers** (the port's verified translation for each):

| Worker | Bind to |
|---|---|
| 001B0FD0 | `em_area11_boxes_owner_001B0FD0` (the fan / terminal pattern) |
| 001C6380 | `em_area11_boxes_owner_001C6380` |
| 001A2370 | `em_actor_cells_retransform_001A2370` (uid = record +0x0E) |
| 001B17A0 | the interaction host's services (as Roger and the prop) |
| draw (+0x4C = 0x1CAA00) | `em_area11_boxes_owner_draw` (001CAA00 unit); retire the legacy husk mesh |
| 001AFC10 / 001AFA90 | `em_actor_pool_free_001AFC10` / `em_actor_pool_alloc_001AFA90` (class 0xC); the child's +0x10 = 001C5680 goes to `tick_indicator`, the shot node's 001F5040 has no translation (revisit only) |
| 00122BB8 | `em_random` (`em_player_misc_random_i32`) |
| 0011E2A8, 0011DF78, 0011DBB8, 0011E748, 001B1470 | `em_sdk_math_original` / `em_item_sdk_sqrt` / `em_player_001B1470` |
| 0011E520 (asinf) | no verified translation: fault (lifecycle 1 only) |
| vector leaves | `em_sdk_vu0` / `em_owner_services_original` / `em_coll_probe_original` (census rows) |
| 0019A570 / 0019B6C0 | `em_coll_segment_walkers` / `em_coll_probe_original` |
| 0019AA80, 001E2BA0 | no verified translation: fault (lifecycles 1/4 only) |
| 001EFD90 / 001CD520 | `em_effects_live` / `em_player_equipment_001CD520` |
| 001FBD50 | `em_sfx_play_at` (cues 0x423, 0x424, 0x425, 0x428) |

### 5.2 The partner 0x827490 and its hit

**Bind** `em_husk_partner_tick` (em_script_door_fan_husk, unchanged) in place
of `tick_enemies` for this node.
- `r_link_18`: the creature's +0x04 and +0x21C.
- 001B11E0: export `em_actor_roster.c`'s static `test_001B11E0` (verified) and
  bind it.
- 001B1190: `em_husk_fan_001B1190` over EmProgress (D_00810700, D_00810860).
  This gives the partner's `w_001B1190` a verified implementation. It
  retires nothing: the partner runs in the legacy `tick_enemies` today and
  never calls `em_pickup.c`'s taken_set, which is em_pickup's own persist
  path (keyed by the uid's area byte, not D_00810700).
- 001EFE00: `em_player_misc_001EFE00`, with its spawn view bound to
  `em_effects_live`'s 001EF9D0 (the node's +0x24, +0xB0, +0xC0).
- 001FBD50: `em_sfx_play_at` (cues 0x426 / 0x427).

**Node behaviours** in the effect binder:
- 0x21AAC0 → `em_husk_fan_0021AAC0`. Its 001EFEB0 goes to
  `em_husk_fan_001EFEB0`, whose 001EF9D0 is `em_effects_live`. Its 001CCF70,
  001CFA60 and 001CFBE0 go to the head-sprite / puff bindings.
- 0x21A500 → `em_husk_fan_0021A500`.

**`EmHuskFanMem` for the node functions** (anything unmapped faults at its
address):
- **The 0x80000045 node** (0021AAC0): +0x04, +0x24 (the partner), +0xB0..+0x10F, +0x1F0..+0x28B (six
  points at +0x1F0..+0x24F, phases +0x250..+0x267, scales +0x268..+0x27F,
  +0x280..+0x28B the height, point count and tick count).
- **The partner** (via +0x24): +0x04 (set to 3 at count 60) and
  +0xD0..+0x10F (the matrix copied in state 0).
- **Each 0x8000003B node** 001EFEB0 spawns: +0xD0..+0x10F (written through
  00102958) and, from 0021AAC0, +0x05 and +0x1F0..+0x1FB.
- **The strip node** (0021A500): +0x04, +0x05, +0xD0..+0x10F, and
  +0x1F0..+0x1F0 + 0x48 + 4n − 1, n = +0x1F0 (for 0021AAC0's n = 12,
  +0x1F0..+0x267).
- **D_00821400..D_00821400 + 16n − 1** (the strip points).
- **Scratchpad:** 0x700036A0..0x700036DF (0021AAC0's rotated matrix),
  0x700038A0..0x700038BF, 0x70003A20..0x70003A23 (0021A500's +5 == 1 fade).
- **001B1190:** D_00810700 (byte) and D_00810860 + (D_00810700 << 5) +
  0..0x1F.

**Blocker.** 0021A500's **001CE860** (the strip's GS packet builder, 1,656
bytes, undecompiled) has no translation. Until the renderer lane translates
it, 0021A500 faults on its first tick. A shot partner would then stop the game
task.
- Do not bind the hit path live before that.
- Binding the partner with the hit path is otherwise complete: 0021AAC0 frees
  the partner at +0x288 == 60 (the original behaviour the old port
  approximated).

### 5.3 The fan 0x827630

**Exit-or-bit.**
- **`D_008107D8 |= 0x80`** is taken on the first visit (beat 15, f344:
  D_008107D8 = 0x81). Its consumer is Roger 0x8237E0's departure (0x823C40 /
  0x823C80 on `em_area11_roger`, live), which calls
  `em_scene_request_area_change_001B0C60(1, 0, 4)`.
- **001B0C60(1, 1, 4)** needs D_00810758[0] == 0xFF. That byte becomes 0xFF
  only when the departure script ends. Roger's 001B0C60(1, 0, 4) sets B8 = 1
  in the same tick, and B8 gates the fan's box. So the fan's direct exit is
  revisit-only.
  - Beat 15 confirms it: B5..B8 = 01 00 04 01, the sub-0 exit, not the fan's
    sub 1.
  - Bind `w_001B0C60` to `em_scene_request_area_change_001B0C60` (byte-matched)
    anyway.

**Player hit.** No binding change is needed beyond binding the fan:
- the player's next stage runs 0021C440 (`em_player_stage_reaction`, live);
- case +F 6 calls none of the stage's stub workers;
- +5 0x11 is 0021E9C0, live in the reaction table.

Follow FAN_ORIGINAL.md "Binding" for the node itself.

## 6. Known gaps

- **001CE860** is untranslated (5.2). The partner's hit path stays unbound
  live until it is.
- **Revisit-only callees** with no verified translation: 0011E520, 0019AA80,
  001E2BA0, and the shot node's behaviour 001F5040. They are reached only in
  lifecycles 1/4, which need D_00810788 == 0xFF. They must fault.
- **Revisit-only effect handlers.** The FX ids the creature's shots spawn (3,
  6, 7, 0x26, 0x2C, 0x67: all 001EA240 subtypes) include handlers that fault
  today (EFFECT_MANAGER.md 8.2). This is revisit-only.
- **The oracle scripts** the collision probes, 001CCF70 / 001CFA60 / 001CFB50
  outputs and the effect / sound / draw callees. Their own translations are
  verified by their own tests.
- **Timer arithmetic.** The creature's timers use signed 32-bit words. The
  lockstep runs do not cover counter wrap-around (≥ 2³¹ ticks).
- **Defect in the decomp's NEARMISS C for 0021A500**
  (`Extermination/src/func_0021A500.c`, the `e[5] == 1` end-sprite loop).
  The C uses one variable, `color`, for both the LCG seed (seed·37 + 11) and
  the packed float_to_int sprite colour, so its second sprite would draw
  its fraction from the packed colour. The original keeps them in separate
  registers: the seed survives the colour packing. `em_husk_fan_0021A500`
  follows the original (the oracle kills a mutant that merges them). That C
  is treated as port ground truth, so it should be corrected before anyone
  translates from it; this lane did not edit it (tracked file).
- **Not re-verified here:** the fan's own translation (`em_fan_original`, its
  own oracle) and 0021E9C0's first tick (`test_player_reaction_reference`).
