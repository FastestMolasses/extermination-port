# Roger actor: lifecycle-0 init, face services and the equipment node

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "roger-actor-original". `em_roger.c` (docs/ROGER_ORIGINAL.md) translates
the Roger controller at overlay `008237E0` for lifecycles 1 to 3. For
lifecycle 0 it returns -1, so Roger can never start. This lane translates the
missing lifecycle-0 case and everything it reaches. It also covers the two face
services the controller calls every frame (`001BA580` and `001BA540`) and the
equipment node that rides on Roger (area11[9], callback `001C5C90`). The work
matters for WP-9 and route beat 14 (the Roger encounter).

The module is **built and tested but not wired**. Section 4 lists what the
coordinator binds.

## 1. What the original does

The records are 0x2F0-byte pool records. Roger is `007A8830`, with kind
(+0x0D) 0x47. The equipment is `007A8B20`, with kind 0x6B and +0x18 = Roger.
All 19 captures hold these records (the 15 route beats, playable, opening,
handoff and roger-encounter).

### 008237E0, case +0x04 == 0 (00823824..0082389C)

1. `001BA1C0(roger, 0)` tests D_00810758[0] == 0xFF. If it is set, the case
   stores +0x04 = 3 and does nothing else.
2. `001B10B0(roger, +0x0D, 0x4A)` binds the model and allocates the bones.
   **The case ignores its result.** Over the bone cap, 001B10B0 stores
   +0x04 = 3, and step 6 then overwrites it with 1.
3. `bone_init_default_2 (001C63E0)(roger, 8)`: clip 8.
4. `001CA6F0(roger, 2)`: +0x98 = 2.
5. `001BA8E0(roger, +0x0D)`: the face attach (below).
6. +0x30 = `00828BD0` (the candidate descriptor). +0x58 = the word at
   `0028A5C4`, which is D_0028A490[0x4D], loaded after 001BA8E0 returns.
   Then +0x04 = 1 and +0x00 = 1.

### 001B10B0(a, a1, a2): model bind by table index

- `001CA6E0(a, D_0028A490[a1])` tail-calls `001CA5E0`. That stores +0x44 =
  the model word, then `001CA5F0(a, 0)` sets +0x4C = `001CAA00`. 001CA6E0
  hard-wires kind 0, so the other twelve draw handlers are never selected.
- When a2 != -1, +0x40 = D_0028A490[a2].
- +0x0C = `001C6150(+0x44)`, the byte at model +0x08.
- If D_00275BCC (a signed halfword) < +0x0C, it stores +0x04 = 3 and returns 1.
- Otherwise +0x110[i] = `001AF780()` while i < +0x0C, re-reading +0x0C on
  every pass. Then +0x09 = +0x0C, `anim_bone_array_setup(+0x0C)` runs, and it
  returns 0.

### 001AF780 / 001AF890: the bone-slot stack

- **001AF780 (pop):** if D_00275BCC < 31, it returns 0 and changes nothing.
  Otherwise it decrements the count, advances the cursor D_00275BD0 by 4 and
  returns the word the cursor pointed at.
- **001AF890 (push):** it clears the 0xD0-byte slot (13 quadwords). Then it
  moves the cursor down 4, stores the slot address there and increments the
  count.

### 001BA8E0(a, kind): the first-tick face attach (NEARMISS; read from the instructions)

1. `001F0120(a, kind)` spawns the head sprite. It is **skipped** when
   D_00810788 == 1 and kind != 0x3B.
2. `001D8BF0(a, 1)` sets +0x02 |= 0x20. The argument is the constant 1, not
   the kind.
3. Kinds 0x6C and 0x6A store +0x96 = 0x34 and 0x33, store +0x56 = 0 and
   return.
4. Kinds without a row store +0x56 = 0 and leave +0x96 alone.
5. For a kind with a row, it stores +0x96 = `shadow` and calls
   `001CA700(a, D_0028A490[index], a2)`.
   - On success, `001D06D0(a, speed)` stores slot +0x81 = speed and +0x56 = 1.
   - On failure (no free slot), it stores +0x56 = 0.

| kind | index | a2 (+0x94) | +0x96 | speed |
|---|---|---|---|---|
| 0x68 | 0x8D | 6 | 0x31 | 0 |
| 0x66 | 0x92 | 6 | 0x30 | 1 |
| 0x64 | 0x8C | 6 | 0x2F | 1 |
| 0x61 | 0x8B | 6 | 0x2E | 1 |
| 0x5E / 0x5D | 0x91 / 0x8A | 7 | 0x2D | 1 |
| 0x5A / 0x59 | 0x94 / 0x90 | 7 | 0x2C | 0 |
| 0x55 / 0x54 | 0x8F / 0x89 | 7 | 0x2B | 1 |
| 0x51 / 0x50 / 0x4F | 0x95 / 0x93 / 0x8E | 7 | 0x2A | 1 |
| 0x49 / 0x48 / **0x47 (Roger)** | 0x88 | 7 | **0x29** | 1 |
| 0x40 / 0x3F / 0x3E / 0x3B | 0x1B / 0x1A / 0x19 / 0x18 | 7 | 0 | 1 |

`001CA700(a, resource, a2)` lazily takes a face slot. If +0x90 is 0, it
stores +0x90 = `001AF780()` and returns 0 when that is 0. It then stores
+0x94 = a2 (halfword) and slot +0x60 = resource, and runs
`001D0690(slot + 0x70)`. That clears slot +0x80 (byte) and the words +0x70,
+0x78, +0x84, +0x88, +0x8C and +0x90..+0xA7. The blink and expression waits
(+0x74, +0x7C) and the speed (+0x81) are kept.

### 001BA580(a, kind): the face update (every live frame; em_roger.c EM_ROGER_FACE_UPDATE)

1. Kinds 0x6C and 0x6A call `001DA6A0(a)` whatever +0x56 holds, and return.
2. Every other kind maps to an activity index:
   - 0x68 → 9, 0x66 → 8, 0x64 → 7, 0x61 → 6;
   - 0x5E/5D → 5, 0x5A/59 → 4, 0x55/54 → 3, 0x51/50/4F → 2;
   - 0x49/48/**47** → **1**, 0x40/3F/3E/3B → 0;
   - any other kind → none.
3. Nothing more runs unless +0x56 != 0 and the kind has an index.
4. It calls `001DA6A0(a)` (the drop shadow). Kind 0x61 with
   D_00810700 == 0xD calls `001BA7F0(a)` instead.
5. The activity byte D_008106D4[index] (Roger: `008106D5`):
   - 1 runs `001D06E0(a, 1)` and stores 0;
   - 2 runs `001D06E0(a, 0)` and stores 0;
   - any other value is left alone.

   `001D06E0` stores slot +0x80 = a1. When a1 is 0 it also clears
   +0x90..+0xA7.
6. `001D0C70(a)` tail-calls the face kernel `001D0720`.

### 001BA540(a): the face release (em_roger.c EM_ROGER_RELEASE_FACE)

When +0x56 != 0, `001CA770(a)` runs. If +0x90 is set, it pushes the slot back
with `001AF890` and stores +0x90 = 0 and +0x94 = -1. Then `001D8BF0(a, 0)`
clears +0x02 bit 0x20.

### 001C5C90(e): the equipment node (byte-matched C)

The dispatch is on +0x04. The parent is p = +0x18.

- **2 or 3:** `001AFC10(e)`.
- **Any other value except 0 and 1:** nothing.
- **0:**
  - If p+0x04 >= 2, it stores +0x04 = 3.
  - Otherwise it calls `001B1020(e, +0x0D, -1, -1)`, ignores its result,
    and stores +0x04 = 1 (001C5D0C). Whatever 001B1020 stored in +0x04 on
    the way (its own += 1, or 001B0DC0's 3 over the bone cap) is
    overwritten.
- **1:**
  1. If p+0x04 >= 2, it stores +0x04 = 3 and returns. If p+0x09 is 0, it
     returns.
  2. For parent kinds 0x47, 0x4E, 0x54, 0x55, 0x58, 0x59, 0x5A, 0x5D, 0x5E
     and 0x6A, `copy_qw4` copies the parent's bone-1 world matrix
     (*(p+0x114) + 0x90) over the current record's bone-0 matrix
     (*D_00275B40 + 0x90).
  3. Scratch `70003600` is set to (0, 1, 0, 1), and va = `001026A0(bone0
     matrix, 70003600)`. The scratch is set to (5.4, 1, 0, 1) and vb is taken
     the same way.
  4. +0xC0 = `001028D0(vb, va)`, then `00102760` normalises it in place.
     Its w lane is 0, because the result register is cleared from the
     constant register before the xyz multiply.
  5. +0xA0 = va and +0xB0 = vb, each with w forced to 1.0.
  6. +0x01 = 1. If p+0x01 is set, the draw method `*(e+0x4C)(e)` runs.

### Where the decomp C differs from the instructions

- **`001BA8E0` (NEARMISS C).** The C file is readable-only and is not
  linked. It is wrong in five ways:
  1. It calls `001F0120(self, 1)` or `001F0120(self, type)`. The
     instructions call `001F0120(self, type)` only when D_00810788 != 1 or
     type == 0x3B, and otherwise make no call.
  2. It passes `type` to `001D8BF0`. The instructions pass the constant 1.
  3. Its D_0028A490 index column is shifted one row for kinds 0x49..0x68.
     For example, it maps 0x68 to "none" and 0x66 to 0x8D; the instructions
     map them to 0x8D and 0x92.
  4. It leaves +0x96 at 0 for kinds 0x47 and 0x48. The instructions store
     0x29.
  5. It passes a2 = 1 to 001CA700 for kinds 0x3B..0x40. The instructions
     pass 7.

  The translation follows the instructions. The oracle runs all 256 kind
  values against them.
- **`001BA580` (NEARMISS C)** agrees with the instructions on every kind.
- **docs/ROGER_ORIGINAL.md** says "Roger's initialized auxiliary kind is 0,
  so the `001DA6A0` call exits immediately". That is not what the original
  does. 001BA8E0 stores +0x96 = 0x29 for kind 0x47, and all 19 captures hold
  0x29. So `001DA6A0` (the drop shadow, docs/SHADOW_ORIGINAL.md) runs for
  Roger every frame while +0x56 != 0. That doc is outside this lane's
  ownership; the lead should correct it.

## 2. The translation (`src/game/em_roger_actor_original.c/.h`)

- `EmRogerActorRecord` models exactly the record bytes these routines read
  or write, each named by its original offset. It serves both Roger and the
  equipment node.
- `EmRogerActorWorld` holds the global views:
  - D_00275BCC and D_00275BD0;
  - the slot-stack words and the slot-arena bytes, each with its original
    base address;
  - D_0028A490;
  - the bytes 00810758, 00810788 and 00810700;
  - the ten activity bytes;
  - the words at *D_00275B40;
  - scratch 70003600;
  - a `resource(address, size)` view for the model byte 001C6150 reads.
- `EmRogerActorWorkers` holds one worker per untranslated callee.
- Faults latch as in the other `*_original` modules:
  - a NULL worker or view gives code 1;
  - a negative worker result gives 2;
  - a result outside the original range, a parent view that is not +0x18,
    or (001C5C90 state 1 with the parent drawn) a draw method at +0x4C
    other than 001CAA00 gives 3, because `w_draw` stands for 001CAA00 only;
  - an address or index outside a view, a bone count over 56, or the init
    called in the wrong lifecycle gives 4.
- Workers and views are checked before the first write of each entry point.
  The native test pins that no record, slot, stack, activity or scratch
  byte changes on a refusal. The init checks the D_0028A490[0x4D] index up
  front but reads the word after 001BA8E0 returns, where the original
  loads it.
- VU0 work in 001C5C90 (001026A0, 001028D0, 00102760) runs as its real
  macro forms through `em_ee_float.h` on raw bits. The module has no host
  float operation. Copies use memcpy, so NaN and denormal bits survive.

Entry points:

- `em_roger_actor_008237E0_init`;
- `_001BA8E0`, `_001BA580`, `_001BA540` and `_001C5C90`;
- the helpers `_001B10B0`, `_001AF780`, `_001AF890`, `_001CA6E0`,
  `_001CA700`, `_001D0690`, `_001D06D0`, `_001D06E0`, `_001D8BF0`,
  `_001CA770` and `_001BA1C0` (entry 0 only).

`001B1020` is **not** re-translated. It is `em_owner_services_001B1020`,
reached through `w_001B1020` as it is, including its own +0x04 stores.
001C5C90 only checks the result lies in {0, 1} and then stores +0x04 = 1
itself (001C5D0C), so those stores never survive the call.

**Existing translations considered and not reused.**

- `em_opening_face_reset` / `em_opening_face_talk` (em_opening_face.c)
  translate the same 001D0690 / 001D06E0. They work on a native
  `EmOpeningFace` struct with host `float` fields. This module must address
  the face slot as original bytes inside the shared 0xD0-byte slot arena
  (the slot is popped by 001AF780, cleared by 001AF890 and compared byte
  for byte), so it re-states the two stores-only routines on bytes. Neither
  does float arithmetic, so the two copies cannot drift in rounding; the
  oracle here proves this module's copy against the instructions directly
  (including direct 001D0690/001D06D0/001D06E0 calls).
- `em_effect_original_001026A0` / `em_effect_original_00102760`
  (em_effect_original.c) translate the same SDK leaves, but on `float`
  arrays through that module's private lane helpers, not `em_ee_float.h`.
  The EE float model rule requires every COP1/VU0 macro op in new native
  code to go through `em_ee_float.h`, so this module states the two leaves
  (and 001028D0) with `em_vu_vec_bits` / `em_vu_sqrt_bits` /
  `em_vu_div_bits` on raw bits. One shared implementation should
  eventually exist: em_effect_original's SDK leaves should be moved onto
  `em_ee_float.h` by their owner, after which both modules can call one
  exported leaf.

## 3. Verification

### `tools/test_roger_actor_original_reference.py` (`make test-roger-actor-original-reference`)

**Setup.** The oracle executes the original routines over captured RAM:

- **Instructions:** it copies them from the pinned ELF and from
  `extract/OVERLAY/AREA11.BIN`, asserting they equal the capture's bytes.
  The routines are 008237E0 case 0, 001BA1C0, 001B10B0, 001AF780, 001AF890,
  001C6150, 001CA6E0, 001CA5E0, 001CA5F0, 001CA6F0, 001BA8E0, 001CA700,
  001D0690, 001D06D0, 001D06E0, 001D0C70, 001D8BF0, 001BA580, 001BA540,
  001CA770, 001C5C90, 00102958, 001026A0, 001028D0 and 00102760.
- **Float model:** COP1 and VU0 go through `tools/ee_float_model.py`, using
  FallEE from `test_player_fall_reference.py`, subclassed and not edited.
- **Hooks:** every other call target is hooked and recorded: 001C63E0,
  001CB5B0, 001F0120, 001DA6A0, 001BA7F0, 001D0720, 001B1020, 001AFC10 and
  the draw method `001CAA00`. The test asserts the hooked set equals the set
  of call and tail-call targets: 29 targets, all hooked or translated.

**Each case compares:**

- the return value;
- the worker call list (order and arguments);
- every modelled record byte;
- the whole slot arena (`007D6950..00810040`) and slot stack
  (`007D4640..007D6950`);
- D_00275BCC and D_00275BD0;
- the activity bytes;
- scratch 3600.

It also asserts that every byte the original stores lies inside those
compared fields.

**Two passes per case.** Every case (unit, float and route) runs twice, and
both passes are compared in full:

1. on the case's own bytes;
2. on the same bytes with every byte the original wrote *before reading
   it* in pass 1 set to a value the original never stores there (distinct
   per byte). The original never saw those old values, so its result, calls
   and stored bytes must not change, and the test asserts they do not. The
   native side starts from the same perturbed bytes, so a store it omits,
   misplaces or gets wrong fails even where the capture (or a fresh slot)
   already held the value the original writes.

Before this pass existed, six stores were invisible in every case: the
init's +0x00 = 1, +0x30, +0x58 and +0x98 = 2; 001CA5F0's +0x4C = 001CAA00;
and 001C5C90's +0x01 = 1. The captures already hold those values. The
store-visibility check below also found two more: 001BA8E0's +0x56 = 1 and
001C5C90's first scratch set-up (the captured scratch already holds 1.0 and
0 there).

**Store visibility.** The interpreter keeps a byte-level data-flow record of
every store the translated instructions make, except the register saves:

- A store instance is the n-th execution of one store instruction in a run.
- It is *live* when a later instruction reads a byte it stored, or when the
  byte is still there at the end of the run.
- It is *visible* when one of those live bytes held a different value just
  before the store. Only then does deleting the store change a compared byte
  or a value the original uses later.

The test asserts four things:

- every live instance is visible in some run;
- every store site is executed live, with two lists of exceptions:
  - `DEAD_STORES`: 001C5C90's +0xAC = va.w and +0xBC = vb.w, which the 1.0
    stores always overwrite. The test asserts these run and are never live.
  - the eleven unreached handlers of 001CA5F0;
- `STORE_EXEMPT` holds only 001C5C90's second scratch set-up. It rewrites
  70003604/08/0C with the 1.0, 0 and 1.0 the first set-up stored. The test
  asserts these three are never visible;
- the stack bytes 001026A0 hands back count through the caller's read.

Result: 86 store sites and 158 live store instances, every one visible.
Without pass 2, exactly the eight stores listed above fail this check.

**Case classes:**

- **sentinel slot bytes.** The captured face slot and a freshly popped slot
  (001AF890 zeroed it) already hold 0 in every byte the face routines clear,
  so on captured bytes alone a missing clear is invisible. The init,
  attach, update and release cases therefore first fill whole slots
  (+0x00..+0xCF) with distinct nonzero sentinel bytes, on both sides (they
  start from the same RAM): the captured face slot, and the slots the
  stack hands out next (cursor word 0 = the attach's lazy pop; cursor word
  21 = the init's face pop after the 21 bone pops). That makes every clear
  of 001D0690 (+0x70, +0x78, +0x80, +0x84, +0x88, +0x8C, +0x90..+0xA7),
  the conditional 001D06E0 target clear, 001AF890's 13 quadwords, and the
  kept bytes (+0x74, +0x7C, +0x81) visible;
- 001D0690 on the face slot, and 001D06D0 / 001D06E0 with a1 in 0, 1, 2,
  0x7F, 0x80, 0xFF, 0x100, 0x12345601, called directly (inside 001BA8E0,
  001D06D0 always rewrites the +0x81 that 001D0690 keeps, so only a direct
  call shows it is kept);
- the init: gates 758 and 788, an existing face slot, and free counts 0, 20,
  21, 30, 31, 32, 40, 51, 52 and 1035 (the bone cap and 001AF780's 31);
- 001BA8E0 over all 256 kinds × 788 × face slot × free count 30/31/1035;
- 001BA580 over 256 kinds × +0x56 (0, 1, -1) × activity (0..3) × D_00810700
  (0x0D, 0x11);
- 001BA540;
- 001B10B0 directly;
- 001C5C90 over own lifecycle × parent lifecycle / +0x09 / kind (12) /
  +0x01 × 001B1020 result. The equipment's bone-0 matrix starts as a
  sentinel matrix that differs from Roger's bone-1 matrix in every word (in
  every capture the two are equal, so the copy_qw4 would otherwise be a
  no-op), so each of the ten copying kinds and the two non-copying kinds
  give different +0xA0..+0xCF;
- 001C5C90 with Roger's bone-1 matrix and the equipment's bone-0 matrix
  both replaced by random bit patterns, the parent kind cycling through the
  same 12 kinds (so every copying kind copies differing data and the
  non-copying kinds run the VU0 path on the equipment's own matrix). The
  patterns include zeros, denormals, ±MAX, Inf and NaN.

**Coverage.** Every conditional branch of the translated routines runs both
ways (87 sites). The single exception is 001CA5F0's table-range check: kind
0 is hard-wired by 001CA6E0.

**Route mode.** On every capture the test:

- asserts the identity above: callback, kind, lifecycle, descriptor, the
  +0x44/+0x58 table words, +0x4C, +0x56 = 1, +0x94 = 7, +0x96 = 0x29,
  +0x98 = 2, class bit 0x20, face slot +0x60 = D_0028A490[0x88], speed 1,
  and the equipment's parent, callback and kind;
- runs the face update, the equipment tick, the release and the init (Roger
  returned to +0x04 = 0, +0x90 = 0) through both implementations, then the
  init and the face update again over sentinel slot bytes.

**The captures are fixed points of the equipment tick.** Across all 19
images, the captured +0xA0..+0xCF equal what the original tick produces from
the captured matrices. The native values are asserted equal to the captured
bytes.

**Fail-stop checks (9).** A NULL worker for each of the init's three, the
update's two and the equipment's draw worker, and a +0x4C of 0, 001CAA04
or 001CAB00 with the parent drawn: each faults before any record, slot,
stack, activity or scratch byte changes, and (NULL workers) the latched
fault refuses the next call.

**Results (2026-09-23, fix round 3):**

- Default mode: 2,400 of 11,661 unit cases, 400 float cases and 4 captures
  (playable, 00, 10, 14), 24 route runs, each in both passes. It takes
  about 5.5 s of CPU.
- `EM_TEST_FULL=1`: all 11,661 unit cases, 3,000 float cases and 19
  captures, 114 route runs, each in both passes. It takes about 29 s of
  CPU and finds the same 158 live store instances, all visible.
- Mutation check, reproducible with
  `python3 tools/test_roger_actor_original_reference.py --mutants`. It
  compiles each `MUTANTS` edit separately and requires the default run to
  fail an assertion; a compile failure or an edit that does not apply
  counts as an error, not a kill. All 20 are killed (about 6 s wall, run in
  parallel):
  - the six stores that were invisible before pass 2: init +0x00, +0x30,
    +0x58 and +0x98; 001CA5F0's +0x4C; 001C5C90's +0x01;
  - 001BA8E0's +0x56 = 1, and the first scratch +4 = 1.0;
  - 001D0690 without its +0x80, +0x8C or +0x84 clear, or with five target
    words instead of six;
  - 001D06E0 always clearing, or never clearing;
  - 0x4E no longer copying the matrix;
  - 001B10B0 without +0x0C;
  - 001AF890 without its stack-word push;
  - 001CA700 without +0x94;
  - 001C5C90 without the +0xC0 = vb - va store;
  - 001BA580 not consuming activity 1.
- Earlier rounds' mutation checks. The first round's six single-line bugs (Roger's +0x96,
  the w = 1.0 store, the 001D8BF0 argument, one VU0 add, the 001AF780
  threshold, the 788 gate) were caught. After the fix round 24 more were
  each caught in the default mode: 001D0690 without the +0x80, +0x84 or
  +0x8C clear, with 5 instead of 6 target words, or clearing the kept
  +0x74, +0x7C or +0x81; 001D06E0 always or never clearing the target;
  001D06D0 writing +0x80; 001AF890 clearing 12 quadwords; the copy_qw4
  dropped for each of the ten copying kinds, or added for 0x6B; the draw
  method check removed; the 001C5D0C +0x04 = 1 removed.

### `tests/roger_actor_original_test.c` (`make test-roger-actor-original`)

This test runs under ASan and UBSan. It checks the fail-stop contract:

- each unconditional init worker missing;
- the wrong lifecycle;
- a bone count over 56;
- a short slot stack;
- an unknown resource;
- a table index out of range;
- a push outside the slot view;
- a face slot outside the view before the activity byte is consumed;
- a failing 001DA6A0;
- a parent that is not +0x18;
- a 001B1020 result of 2;
- the equipment draw worker missing;
- an equipment draw method other than 001CAA00;
- a latched fault refusing the next call.

The world in this test is synthetic bookkeeping, not original data.

## 4. Binding (as built, census L22, 2026-09-24)

`src/game/em_area11_roger.{h,c}` binds both nodes on the live path
(em_area11_bindings `tick_roger`, both walk variants):

- **Roger node** (area11[8], callback `008237E0`): `+0x04 == 0` runs
  `em_roger_actor_008237E0_init`; any other lifecycle runs `em_roger_tick`
  (em_roger.c: 00823910 / 00823950 / 00823B70 / 00823C40).
- **Equipment node** (area11[9], callback `001C5C90`):
  `em_roger_actor_001C5C90` with the parent at +0x18 (the pool's prev link,
  checked to be Roger's record) and `world.d00275B40` = the equipment's own
  +0x110 words.
- **Storage.** The EmActor fields are the canonical record bytes they name
  (+0x00..+0x1F, +0x2E..+0x33, +0x36, +0x52..+0x9A, +0x9C..+0x9E, +0xB0..+0xCF,
  the +0x1F0 block); the binder keeps the record's other bytes (+0x20..+0x2D,
  +0x38..+0x51, +0x9B, +0x9F..+0xAF, +0xD0..+0x1EF: +0x40, +0x44, +0x4C,
  +0xA0, +0xD0, the +0x110 words) and syncs both ways around every owner call
  and every hook that reaches another owner of the bytes (the script host,
  the publication, the pool free). `EmRogerActorRecord` and em_roger's
  `EmRoger` / `EmRogerStory` are views loaded before and stored after each
  call: status +0x00, rendered +0x01, class +0x02, lifecycle +0x04, phase
  +0x05, armed +0x0B, kind +0x0D, the animation result +0x1FE, yaw +0xC4;
  the story bytes D_008107D8, D_00810791, D_00810793, D_00810813 are
  canonical D2 progress (em_scene_state.h; D_00810758 / D_0081078F /
  D_00810791 / D_008107D8 migrated in this step).
- **Slots.** The node and face records are 0xD0-byte slots of the one
  001AF710 arena and stack (`em_area11_boxes_slot_world`): 001AF780 /
  001AF890 through this module's translations, 001AF800 (the pool's free)
  pushes Roger's and the equipment's +0x110 words back
  (`em_area11_roger_001AF800`).
- **Resources.** `assets/scene_snow/roger/resources.emrs`
  (`tools/export_roger_banks.py`): D_0028A490[0..0xC0), the bank file
  chunk15/f12_id44 from +0x41000 (banks 0x96 and 0x4A, the +0x58 chain) at
  its load address, chunk15/f18_id94 (model 0x47, face resource 0x88) and
  the equipment's model 0x6B of chunk27/f01_id37 with D_0028A56C's table
  head, each checked byte for byte against RAM in every AREA11 capture.
- **Workers.**
  - `w_001C63E0`, 001C67E0, 001C64F0, 001C68C0: `em_pose_host_workers` over
    Roger's record (`em_pose_host_001C63E0 / _001C67E0 / _001C68C0` and
    `em_player_stage_anim_advance` with the pose host's clip workers), the
    banks mapped read-only at their addresses and the slot arena writable.
  - `w_001CB5B0`: nothing to write (D_00275B40 is the view of the ticking
    record's +0x110).
  - `w_001F0120`: `em_area11_bindings_spawn_001F0120(owner, 0x47)` (the
    head-bone sprite node; the former INTERIM first-tick spawn is deleted).
  - `w_001DA6A0`: reported no-effect binding (UM_001DA6A0,
    em_scene_bindings.c). The port draws no actor shadow; the player's own
    post-step is reported the same way (UM_0015C160). All 11 captured runs of
    001DA6A0 on Roger return at its clip test (SHADOW_ORIGINAL.md).
  - `w_001D0720`: `em_opening_face_tick` over the face slot bytes
    (+0x40..+0x5F, +0x70..+0xA7) with the shared 00122BB8 RNG, then the face
    morph of the slot's weights on Roger's mesh (em_opening_face_position,
    em_gfx_mesh_update_positions).
  - `w_001BA7F0`: NULL (kind 0x61 in area 0x0D only).
  - `w_001B1020`: `em_owner_services_001B1020` over the equipment's view,
    D_0028A56C's table and model 0x6B from the export; the one slot its
    001C62C0 fills is laid into the slot bytes.
  - `w_draw` / Roger's `EM_ROGER_DRAW`: 001CAA00 → the port's actor draw
    chain (`em_area11_roger_draw`): Roger's mesh (roger/roger.emdl with the
    opening's face attached, em_face_model_attach) at the 21 node world
    matrices (node +0x90) and the owner matrix +0xD0 for the exporter's
    trailing slot; the equipment's mesh (opening/equipment_6b.emdl, whose
    vertices all use node 0) at its bone-0 matrix. The light reference is
    the record's +0x98 node (2 for Roger), with the camera fill of +0x02 bit
    0x20 and the 001D88B0 face rig for the face vertices.
  - `w_001AFC10`: the actor pool free.
- **em_roger hooks.** `script_start` / `script_tick` →
  `em_area11_script_host_start` / `_tick` (AREA_SCRIPT.md 6.1);
  `trigger` → `em_director_original_001B1EA0_bound(0, D_00810350 (g.pos),
  the quad 0x82AB80 of roger/trigger.empg, 4)` with
  `em_sdk_math_original_float_0011E620` over the collision world's SDK
  context (one translation of 001B1EA0: em_roger_trigger is deleted);
  `publish` → `em_area11_interaction_host_offer_001B17A0` (the class lists:
  class 0x0A onto the class-2 list, bit 0x80 onto the interactive list);
  `EM_ROGER_STOP_STREAMS` → `em_scene_bindings_001FABB0`;
  `EM_ROGER_RESTORE_DEFAULT_BANK` → +0x40 = D_0028A5B8;
  `EM_ROGER_RESUME_MUSIC` → `em_scene_bindings_001FAE70(0)`;
  `EM_ROGER_FADE_IN` → 001AEE10(4, 0); `EM_ROGER_REMOVE_GROUP` →
  `em_scene_request_area_change_001B0C60(1, 0, 4)`; the face, pose, draw,
  release and free events as above.
- **The collision world.** Roger's record and the chain his +0x58 names are
  the collision world's owners' bytes (`em_collision_world_bind_owners`):
  the close-out passes read the class-2 list (001A7870 marks his +0x50) and
  the hull locks 001A6440 / 001A6AD0 test his +0x58 chain at his node
  matrices.
- **The Use scan.** Lifecycle 0 binds his EMIS record (source 0x82A500) to
  the pool record (`em_area11_interaction_host_bind_roger`); the scan's
  predicate is em_roger_candidate (00183EF0 selector 0, class 10), its
  claim arms +0x0B, and the armed talk 0x828810 runs as a script owner.

**Evidence.** The level smoke's `roger` phase (LEVEL_SMOKE.md) compares
route 14 row for row from the scripted frame's opening (f288) to the end of
the capture (f1818): Roger's +0x00..+0x0F, +0xB0 and script block, the
equipment's +0x00..+0x0F and +0xB0 (001C5C90's vb from Roger's node 1),
and the whole encounter (AREA_SCRIPT.md, ROGER_CINEMATIC.md).

## 5. Limits and open items

- 001D0720's kernel (em_opening_face) uses host float, not the EE model
  (a limit of em_opening_face). 001DA6A0 is a reported no-effect binding
  (no actor shadow is drawn by the port). 001BA7F0 is unreachable.
- Lifecycle 0 runs once, at area load, before any capture, so there is no
  capture from the frame the init ran. Its evidence is the unit oracle and
  the init route runs on every capture, with Roger returned to +0x04 = 0.
  Pass 2 and the store-visibility check prove that each init store is seen.
  The capture identity shows only what the original left behind; it does
  not check the native writes.
- The NEARMISS 001BA8E0 C in the decomp needs correcting by its owner (the
  ROGER_ORIGINAL.md auxiliary-kind claim is corrected).
- Paths bound but not on the route (no capture compares them): the armed
  talk 0x828810 (Use on Roger: em_roger_candidate, the claim, the script
  owner's 00183090), the free (001BA540, 001CA770, 001AF890 through
  001AF800) and the alternate 0x828990, which needs the director's
  D_00810793 (census L21). The departure 0x828A10 is not in the first
  visit: its op0F stream handshake is still a fail-stop NULL worker.
- The draws are the port's actor draw at the original node matrices (the
  face through the 001D88B0 face rig); no GS capture of Roger is compared.
