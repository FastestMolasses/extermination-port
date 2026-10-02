# The AREA11 security gun, its cable and the fan pair (census L24)

The AREA11 overlay owners 0x825940 (the **security gun**), 0x827490 (its
**power cable**) and 0x827630 (the **fan pair**). Since chain step L24
(2026-09-28) all three run live on their original owners in the pool walk
and draw on the object-unit path (section 5). Earlier notes and code called
the gun and its cable the "husk creature" and "husk partner"; that label is
wrong and is gone from the port.

**Identity** (decomp `build/workflows/verify-area11-husks.output.json`, read
from the code, the placement records and every AREA11 capture):
- Both come from area 11's spawn group at 0x828180 (records 0x8282B4 and
  0x8282E0, condition 0: every entry into area 11 sub 0). They sit at
  (387, 231.8, 290.3) above the fence door; the gun's yaw is −π/2.
- Their models are the per-area bank entries 8 (the gun: a housing with a
  barrel along +x, 4 nodes) and 6 (the cable: a strand hanging about 48
  units to the ground, 2 nodes) of `*D_0028A59C`, bound from +0x0D. The
  "husk" name came from reading the cable's +0x03 byte 0x29 as library model
  0x29 (the crate's burst debris).
- The gun is a fixed mount with a head bone (2) and a gun bone (3), a
  60-unit sight probe along the barrel and a lamp child (library model
  0x7A, behaviour 001C5680) that it tints green while scanning, red while
  aiming and leaves dark while dormant.
- On the first visit the gun stays dormant (lifecycle 0x64) for the whole
  level: it leaves 0x64 only when D_00810788 (event flag 0x30) is 0xFF, which
  only an AREA17 script (to 1) plus the AREA11 flag-0x30 manager 0x823CE0's
  script 0x828C70 (to 0xFF) on a later return visit can set. Every AREA11
  capture has the flag 0, the gun in 0x64 with +0x28 = 584, and the cable in
  lifecycle 1, not hit.
- Cutting the cable disables the gun for good (lifecycle 2, and the taken
  bit +0x9A = 0x50 in area 11's row, which its lifecycle 0 reads on every
  later visit). The gun has no health and no other off switch. In the
  original a light melee at the strand's foot cuts it; rounds aimed at the
  cable land on the pillar face behind it and do not (decomp
  CAPTURES_C10.md "AIM", beats aim_10 / aim_11).

**Files.**
- `src/game/em_security_gun.{h,c}` (was em_script_door_fan_husk): the
  gun's lifecycles 0, 0x64, 2, 3 and "other", the cable 0x827490 and the
  flag-0x30 manager 0x823CE0 (`em_gun_tick`, `em_gun_cable_tick`,
  `em_flag30_manager_tick`). Oracle: `tools/test_script_door_fan_reference.py`
  (SCRIPT_DOOR_FAN.md section 3.3).
- `src/game/em_security_gun_rest.{h,c}` (was em_husk_fan, phase B15): the
  rest of the pair's code: the gun's return-visit lifecycles 4 (scan) and 1
  (aim and fire) with the sight probe 0x826F30 and the shot node 0x827400
  (`em_gun_rest_tick`, not bound in the first level: 5.1), the taken-bit
  set 001B1190 (bound), and the cable-hit effect nodes 0021AAC0 / 0021A500
  and their spawn 001EFEB0 (bound through the aim / fire composition, in
  ordinary play since 2026-10-02: 5.2, AIM_FIRE.md section 10.5). Oracle:
  `tools/test_security_gun_rest_reference.py` (`make
  test-security-gun-rest-reference`).
  - Default: about 6 to 8.5 s wall, about 9 s user CPU. `EM_TEST_FULL=1`:
    about 10 s wall, about 20 s user CPU (measured 2026-09-27, 4 workers).
  - It builds privately into `build/security_gun_rest/`.
- `src/game/em_fan_original.{h,c}`: the fan (FAN_ORIGINAL.md).
- The live adapters: `src/game/em_area11_bindings.c` (`tick_gun`,
  `tick_gun_cable`, `tick_fan`).

Sections 1 to 4 are the B15 lane's record of the rest translations and their
evidence; section 5 is the live binding.

Addresses are original runtime addresses. The AREA11 overlay listing names
each function 0x40 lower (its vram is the MWo3 header address).

**Correction to SCRIPT_DOOR_FAN.md section 6.** The gun's two overlay
helpers are runtime **0x826F30** and **0x827400**, not 0x826F70 / 0x827440.
The jal instructions of 0x825940 encode 0x826F30 and 0x827400.
- 0x826F30 is listed as `..._00826EF0` (a 0x40-byte prologue) falling through
  into `..._00826F30`.
- 0x827400 is listed as `..._008273C0` falling through into `..._00827400`.

## 1. What the B15 lane added (em_security_gun_rest)

| Piece | Before | Now | Evidence |
|---|---|---|---|
| 0x825940 lifecycle 4 (0x825B74..0x826190) | faulted (untranslated) | `em_gun_rest_tick` | oracle, every branch both ways |
| 0x825940 lifecycle 1 (0x826190..0x826D60) | faulted | same | oracle, every branch both ways but one dead arm (3.3) |
| 0x826F30 sight probe | untranslated | inside the tick (`sight`) | oracle |
| 0x827400 shot node | untranslated | inside the tick (`shot`) | oracle |
| lifecycles 0, 0x64, 2, 3, other | `em_gun_tick` (verified) | **reused**; one entry point through a memory adapter | oracle over the adapter, all 24 AREA11 captures |
| lifecycle-0 rand draw (0x8259F0) | port draws nothing at AE+1 | reused setup, bound as below | C7 per-call capture + all captures (4.1) |
| bone-slot writes (bone 3 +0x78, bone 2 +0x74) | views only | read and written through D_00275B40's slots | captures (4.1) + oracle |
| 001AFA90 child / shot node | interim `spawn_child_record` | worker; `em_actor_pool_alloc_001AFA90` is the verified translation (byte-matched) | reused |
| 001A2370 hull | worker | worker; `em_actor_cells_retransform_001A2370` is verified and live | reused |
| 001B1190 taken-bit set | no verified implementation for the cable's `w_001B1190` | `em_gun_rest_001B1190` | oracle (30 cases) |
| 001B11E0 taken-bit test | verified, static in `em_actor_roster.c` | reused, exported as `em_actor_roster_001B11E0` (L24) | test_actor_census_reference |
| 001EFE00 (cable FX 0x80000045) | verified (`em_player_misc_001EFE00`, and `em_area01_side_001EFE00`); spawn view unbound | `em_area01_side_001EFE00` through em_aim_fire_world_live (AIM_FIRE.md; in ordinary play since 2026-10-02, section 10.5); `em_player_misc_001EFE00` stays the player's, unbound | see 2.5 |
| 0021AAC0 (the node 0x80000045 spawns) | untranslated | `em_gun_rest_0021AAC0` | oracle (65 runs incl. spawn to free) |
| 001EFEB0 (0021AAC0's spawn) | untranslated | `em_gun_rest_001EFEB0` | oracle |
| 0021A500 (the 0x8000003B strip node) | untranslated | `em_gun_rest_0021A500` | oracle (32 runs) |
| fan exit-or-bit consumers | unverified | 001B0C60 and Roger's departure are translated and live; reachability settled (5.3) | beat 15 capture |
| fan player-hit consumer | unverified | the original 0021C440 run over the original fan's writes equals the native `em_player_stage_reaction` | chain oracle (4.3) |

## 2. Behaviour

### 2.1 The gun 0x825940

The gun dispatches on its lifecycle byte +0x04.
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
  - Then the gun's bone-3 +0x90 matrix is copied onto the child's bone 3
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
- **Target.** Once +0x204 holds a target, the gun goes to lifecycle 1:
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
  non-zero, the gun fires:
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
- **Lost target.** A negative +0x2A returns the gun to lifecycle 4:
  - +0x224 = 0 and +0x204 = 0;
  - +0x1FC = wrap(asinf(−(+0x78 + 1.1344)/−0.8290)), which recovers the sway
    angle from the pitch;
  - +0x28 = 300 + (300·(rand>>16))>>15, and +0x200 = 1.

### 2.2 The sight probe 0x826F30 (gun, gun matrix)

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

### 2.5 The cable's hit reach

The cable's lifecycle 1 reacts to a hit (`em_gun_cable_tick`). It calls
001EFE00(0x80000045, cable), which is `em_player_misc_001EFE00`. That calls
001EF9D0.

**The spawned node.** Record 0x45 of the global effect table (read from the
user's ELF) gives:
- class 0xC;
- +3 = 0x63;
- subtype 0;
- callback **0x21AAC0**;
- kind 1, so 001D80E0 point light, which `em_effect_original` translates;
- no sound.

**0021AAC0** (the node's +0x24 is the cable):
- **State 0:**
  - six phases rand/2³¹ go to +0x268.., with zeros at +0x250..;
  - the node takes the cable's +0xD0 matrix;
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
  - **At 60** it sets the cable's +0x04 = 3, so the cable frees itself on
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

## 3. Verification (`tools/test_security_gun_rest_reference.py`)

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
- **Special case: em_gun_tick's two inlined 00102948 copies**
  (setup) execute unlogged.

**The native side** runs over its own copy of the same memory
(`EmGunRestMem`). Per case the test asserts:
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
case, checked once with `EM_GUN_REST_MODULE` on single-edit copies of
`em_security_gun_rest.c` (the copies were deleted afterwards):
- the named survivor (`lt` → `le` at −step) fails "heading d == −step" (call
  differs: the original calls 0011DF78, the mutant does not);
- `le` → `lt` at +step fails "heading d == +step";
- the four clamp compares (yaw min/max at runtime 0x826694 / 0x8266D4, pitch
  min/max at 0x826808 / 0x826848) fail their pinned case on memory at the
  next callee's entry.

## 4. Captures

### 4.1 The gun in every AREA11 image (24)

Every image has:
- the gun at 0x7A6AD0 in lifecycle 0x64;
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

Each image's gun is also ticked once through the adapter against the
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

## 5. Binding (live since census L24, 2026-09-28)

### 5.1 The gun 0x825940 (deferred g0.7, record 0x7A6AD0)

`em_area11_bindings.c` `tick_gun` runs `em_gun_tick` (em_security_gun.c)
over the node's record once per pool-walk call, in every walk mode (class 4
is not skipped by 001AFD70 modes 0 and 1). The legacy `em_enemy` group that
drew a static mesh in its place, the interim inline child spawn
(`spawn_child_record` for the 0x7A lamp) and em_enemy's gun and cable kinds
are deleted.

- **Record.** +0x00, +0x04, +0xB0 and +0xC0 are EmActor's; +0x28 is the
  node's (`Node.h28`); +0x1F4..+0x227 are the +0x1F0 block
  (`EmActor.scratch`); +0x220 holds the lamp's original record address.
- **Workers:**
  - 001B0FD0 → `em_area11_boxes_owner_001B0FD0` (entry 8 of the per-area
    bank, 4 bone slots);
  - the flag → `D_00810758[0x30]` of the canonical progress view
    (D_00810788);
  - 00122BB8 → `em_random_next` (the one rand() state);
  - 0011E2A8 → `em_sdk_math_original_w_0011E2A8` over the collision world's
    SDK context;
  - r_00275B40 / the +0x78 store → the gun's own slot 3
    (`em_area11_boxes_owner_slot`, `EmOwnerBone.rot[2]`);
  - 001C6380 → `em_area11_boxes_owner_001C6380`;
  - 001A2370 → `em_collision_world_retransform_001A2370` with bone 3's +0x90
    (the gun's plate, uid 15);
  - 001AFA90 → `em_actor_pool_alloc_001AFA90` (class 0xC); the lamp's stores
    go onto its record and its +0x10 = 001C5680 binds `tick_indicator`;
  - 001B17A0 → the interaction host's services (the placed prop's view);
  - +0x4C → `em_area11_boxes_owner_draw` (001CAA00 through
    em_owner_draw_live);
  - 001AFC10 → `em_actor_pool_free_001AFC10`.
- **Fail-stop (not bound, return visit only):** lifecycles 4 and 1 fault at
  0x825B74 / 0x826190 (`EM_GUN_FAULT_UNTRANSLATED`); so does the
  lifecycle-2 swing, which only D_00810788 == 0xFF reaches (its lamp view,
  00102958 and 0x70003A20 are not bound). The verified
  `em_gun_rest_tick` stays unbound until the port reaches a return visit
  (it also needs 0011E520, 0019AA80, 001E2BA0 and 001F5040, section 6).
- **The lamp** (001C5680 on model 0x7A) keeps its (0, 0, 0, 0.25) colour on
  the first visit and draws its one 001F54E0 rand() per tick
  (`tick_indicator`); its +0x4C 001CACB0 draws nothing yet (OWNER_DRAW.md
  section 11, the indicator children's 001CABA0 path).

### 5.2 The cable 0x827490 (deferred g0.8, record 0x7A6DC0)

`tick_gun_cable` runs `em_gun_cable_tick`. +0x00, +0x04, +0x36 and +0x9A
are EmActor's; +0x28 and +0x34 are the node's.

- **Workers:** 001B0FD0, 001C6380, 001B17A0, +0x4C and 001AFC10 as the
  gun's (entry 6 of the bank, 2 slots); 001B11E0 →
  `em_actor_roster_001B11E0` over the canonical D_00810860 rows; 001B1190 →
  `em_gun_rest_001B1190` over D_00810700 and those rows; 001FBD50 →
  `em_sfx_play_at` (cues 0x426 / 0x427, range 300); the record at +0x18 →
  the node before it in the pool list (the gun), whose +0x04 and +0x21C
  it writes.
- **Reached in the first level:** lifecycle 0 (001B0FD0, +0x34 = 1, +0 = 1,
  001B11E0(0x50) = 0) and lifecycle 1 every tick (+0x36 == 0: 001C6380,
  001B17A0, the draw).
- **The hit** (+0x36 ≠ 0: the gun to lifecycle 2 with +0x21C = 90, then
  001EFE00(0x80000045), cue 0x426, then lifecycle 2: cue 0x427 at 10 and the
  taken bit through 001B1190) is bound up to 001EFE00, which faults in
  ordinary play (since chain step AIM through em_aim_fire_binding, which
  has no world extension outside its diagnostic gate). Behind the aim/fire
  gate the chain is composed (AIM_FIRE.md): 001EFE00 is
  `em_area01_side_001EFE00`, the nodes 0021AAC0 / 0021A500 run
  em_security_gun_rest (bound as pool nodes in em_area11_bindings), and the
  strip packets 001CE860 run `em_area06_port_001CE860`
  (tools/test_area06_port_reference.py); it is covered at its adapter
  boundaries (make test-aim-fire-cable-live), not in a live run. As for the crates and drums, no live code
  writes a pool record's +0x36 (the port's weapon still hits only em_enemy
  instances), so the hit is not reachable in the port today. When a +0x36
  writer lands, 001EFE00's node view, the 0021AAC0 / 0021A500 node
  behaviours and 001CE860 must be bound first.

### 5.3 The fan 0x827630 (area11[1] and [2])

`tick_fan` runs `em_fan_original_tick` (FAN_ORIGINAL.md) over each node's
record: +0x04, +0x05, +0x2E and +0xC8 are EmActor's (`u04[0]`, `u04[1]`,
`flags2`, `rot[2]`); +0x28 and +0x38 are the node's.
- **Workers:** 001B0FD0 / 001C6380 / +0x4C as above (entry 0x13 of the bank;
  a bound fan retires the legacy em_pickup prop instance at its +0xB0);
  001B17A0 as above; 001FBD50 → `em_sfx_play_at` (cue 0x451); 001B0C60 →
  `em_scene_request_area_change_001B0C60`; 001AFC10 as above.
- **Globals:** D_00810788, D_00810758 and D_008107D8 are canonical progress
  bytes (D_008107D8 written back); D_008106B8 is the request byte B8; the
  player D_008102B0 is the live player record (`player_states_actor_mut`):
  +0x00 and +0xA0..+0xA8 read, +0x00, +0x0F, +0x70..+0x7C and +0x224 written.
- **The tail.** Record [2]'s box: with the player at z < 156 inside it,
  `D_008107D8 |= 0x80` (Roger's departure; the first visit takes it, beat
  15: D_008107D8 = 0x81); 001B0C60(1, 1, 4) needs D_00810758 == 0xFF, which
  only the departure script's end sets, one tick after Roger's own
  001B0C60(1, 0, 4) has set B8 = 1 (and B8 gates the box): revisit only. At
  156 ≤ z < 166.5 with the fast spin, the player hit (+0x00 = 3, +0x0F = 6,
  +0x224 = 5.0, +0x70 = (0, 0, 1, 1)), which the player stage's 0021C440 and
  0021E9C0 consume (live; 4.3). Neither box is on the level smoke's route,
  which ends at Roger's encounter; walking into the exit box after Roger now
  starts his departure 0x828A10, whose op0F handshake is still a fail-stop
  (FIRST_LEVEL_AUDIT H3).

### 5.4 Evidence (live)

- **Every route snapshot** (`tools/test_level_smoke.py` check_gun_fan, the
  full route and both side runs): the gun and the cable at their captured
  records equal all 15 route snapshots field for field on every tick after
  the gun's first call (lifecycle 0x64, +0x28 = 584, bone 3 +0x78 =
  0xBF91361E, the lamp at +0x220 with (0, 0, 0, 0.25); the cable in
  lifecycle 1, +0x34 = 1, +0x36 = 0); every snapshot's fan state (+0x04,
  +0x05, +0x28, +0x38, +0xC8 of both records) is one the port's fans run
  through; the aligned snapshots hold the same records.
- **The draws** (check_owner_units): the gun, the cable and the fans are
  owner units; the ORIGINAL 001CAA00 over each snapshot equals the port's
  unit (colour matrix, lighting rows, and in the camera-exact beats 10 and
  14 the unit bytes, clip pass and position rows). The fans' phase at a
  snapshot follows the recording's timing, so where the port's +0xC8
  differs the ORIGINAL 001C6380 first places the fan at the port's +0xC8.
- **rand() order** (`make test-rand-order`, check_rand_order): the gun's
  lifecycle-0 draw (0x8259F0) is the original's AE+1 call; the port equals
  the original call for call from the area entry for 126 calls (4 before
  L24), up to the player face's missing draw at AE+5 (RAND_ORDER.md).
- **Collision** (`make test-collision-world-capture`): the gun's plate (uid
  15) equals the original's bytes in captures 00 and 04.
- **Frame order**: idle04 / walk04 / st03 (--native-index 1330), cut02 and
  cut15 PASS.

## 6. Known gaps

- **The cable's hit** (closed 2026-10-02): the original melee states run
  in ordinary play, the knife hits the cable and the reaction 001EFE00 /
  001EFEB0 / 0021AAC0 / 0021A500 runs through the aim / fire composition;
  the side run aim_cable compares the gun and the cable with the AIM
  capture aim_11 row for row (AIM_FIRE.md section 10.5).
- **The flag-0x30 manager 0x823CE0** (area11[11]) is still a no-code node
  ("manager: dormant"): `em_flag30_manager_tick` is verified but not bound.
  On the first visit it would only step lifecycle 0 → 1 and call 001B17A0
  every tick; its script path is return-visit content.
- **The gun's lamp** draws nothing (OWNER_DRAW.md section 11); it is dark
  on the first visit.
- **The fan's boxes** (the exit bit and the player hit) are off the level
  smoke's route (5.3); the hit's consumer chain is proven by the oracle
  (4.3), not live.
- **Revisit-only callees** with no verified translation: 0011E520, 0019AA80,
  001E2BA0, and the shot node's behaviour 001F5040. They are reached only in
  lifecycles 1/4, which need D_00810788 == 0xFF. They must fault.
- **Revisit-only effect handlers.** The FX ids the gun's shots spawn (3,
  6, 7, 0x26, 0x2C, 0x67: all 001EA240 subtypes) include handlers that fault
  today (EFFECT_MANAGER.md 8.2). This is revisit-only.
- **The oracle scripts** the collision probes, 001CCF70 / 001CFA60 / 001CFB50
  outputs and the effect / sound / draw callees. Their own translations are
  verified by their own tests.
- **Timer arithmetic.** The gun's timers use signed 32-bit words. The
  lockstep runs do not cover counter wrap-around (≥ 2³¹ ticks).
- **Defect in the decomp's NEARMISS C for 0021A500**
  (`Extermination/src/func_0021A500.c`, the `e[5] == 1` end-sprite loop).
  The C uses one variable, `color`, for both the LCG seed (seed·37 + 11) and
  the packed float_to_int sprite colour, so its second sprite would draw
  its fraction from the packed colour. The original keeps them in separate
  registers: the seed survives the colour packing. `em_gun_rest_0021A500`
  follows the original (the oracle kills a mutant that merges them). That C
  is treated as port ground truth, so it should be corrected before anyone
  translates from it; this lane did not edit it (tracked file).
- **Not re-verified here:** the fan's own translation (`em_fan_original`, its
  own oracle) and 0021E9C0's first tick (`test_player_reaction_reference`).
