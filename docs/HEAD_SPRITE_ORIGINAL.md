# Head sprite effect: 001E2560 (effect entry 0x10) and its helpers

Status: 2026-09-23, lane `head-sprite-effect`. This covers the translation and its oracle. It is **not bound
into the frame yet**.

Files:
- `src/game/em_head_sprite_original.{h,c}`: the native translation.
- `tools/test_head_sprite_reference.py`: the original-instruction oracle and the capture check.
- `tests/head_sprite_original_test.c`: the native contract test.

Translated functions (all byte-matched decomp C; the `.s` was read where the C hides a detail):

| Function | What it is (read off the code) |
|---|---|
| 001F0120(owner, key) | Spawner. If 001E2290(key), calls 001EF9D0(0x80000010, 0, 1.0). On success it writes record +0x0D = key and +0x24 = owner +0x14. |
| 001E2290(key) | 1 for the 20 keys 0x3B, 0x3D–0x40, 0x47–0x49, 0x4E–0x51, 0x54, 0x55, 0x58–0x5A, 0x61, 0x68, 0x6A; otherwise 0. |
| 001E2560(record) | The node behaviour (pool callback +0x10). |
| 001E23A0(record) | Maps key +0x0D to one of 8 entries of D_002535F0 (16 bytes: word, x, y, z). Writes +0xA0/A4/A8 = x/y/z, +0xAC = 1.0, +0x28 = word. Returns 1 when the key has no entry. |
| 001B0070() | Returns D_008106C8. |
| 001CFA60(obj, src, f12, f13) | Fills the 0x58-byte block: +0x44 = f12, +0x4C = f13, +0x48 = 1.0, +0x50 = 0x358637BD, +0x54 = 0, +0x40 = 001CD370(0), then copies the 64-byte `src`. |
| 001CFBE0(id, kind, st, xf, copy) | Emits the four-packet GIF chain for one effect instance (details below). |

## Identity (checked in the captures)

- The effect table (`*D_00259C70` = 0x257C90) holds entry 0x10 at 0x257F90. Its fields: pool class byte 0x0C,
  +4 = 4 (copied to the node's +0x03), and +0xC = **0x1E2560** (the callback that 001EF9D0 copies into node +0x10).
- Every AREA11 RAM image holds these nodes (lifecycle 1, draw callback +0x4C = 0):

| Pool slot | Record | Owner (+0x24) | Owner identity | Key (+0x0D) | Where |
|---|---|---|---|---|---|
| 39 | 0x7AC8D0 | 0x8102B0 | the player | 0x3B | all 26 images |
| 47 | 0x7AE050 | 0x7A8830 | Roger, callback 0x8237E0 | 0x47 | all 26 images |
| 51 | 0x7AEC10 | 0x7A96E0 | callback 0x1BB0E0 | 0x47 | opening only; lifecycle 3 at handoff, freed afterwards |

  The player node comes from 0015C420 (`func_001F0120(player, 0x3B)`). The Roger node comes from 001BA8E0, which
  passes its type, or 1 instead of 0x3B when D_00810788 == 1.
- Entry word (+0x28) = **7** for both keys, that is, owner bone slot 7. The offsets (+0xA0) are (1.5, −0.6, 0, 1)
  for key 0x3B (entry 0) and (1.8, −0.5, 0, 1) for key 0x47 (entry 1). These are ELF data, read at run time by
  `em_head_sprite_original_load_tables`.
- D_00253670 (the source block) has mode word +0x8C = 1. With kind 1 this selects row 2 of D_00251260 and the
  table address 0x231770. D_002535F0, D_00253670 and D_00251260 are identical in the ELF and in every capture.

## Behaviour (all numbers read from the original code)

**Lifecycle byte +0x04:**
- **0:**
  1. +0x1F0 = 00122BB8() % 40 + 60 (signed `div`/`mfhi`).
  2. +0x0C = 0, +0x09 = 0, +0x04 = 1.
  3. Then 001E23A0: if the key has no entry, +0x04 = 3.
  4. Otherwise, when D_008106C8 & 8, +0x04 = 3. 001B0070 is not called when 001E23A0 returned 1.
- **1:** Let `owner` = +0x24.
  - If owner +0x02 & 0x1F == 0: when owner +0x220 <= 0 (`c.le.s` against +0), +0x04 = 3 and return.
  - Otherwise: when owner +0x04 >= 2, +0x04 = 3 and return.
  - If owner +0x01 == 0: return.
  - Sub-state +0x05:
    - **0 (wait):** +0x1F0 -= 1. When it goes below 0: +0x05 += 1, +0x244 = 0, and +0x24C = cvt.s.w(00122BB8()) /
      2^31 (`div.s` by 0x4F000000).
    - **1 (ramp):**
      1. +0x244 += **0.02** (0x3CA3D70A, `add.s`).
      2. If the result is not <= **1.5**: +0x244 = 1.5 (an integer `sw` of 0x3FC00000), +0x1F0 = 00122BB8() % 40 +
         60, and +0x05 = 0.
      3. Then, **every ramp tick including the clamping one**:
         - 001026A0(+0xB0, *(owner + 0x110 + bone·4) + 0x90, +0xA0): the anchor on the owner's bone.
         - 001029C0(+0xD0), 00102C58(+0xD0, +0xD0, owner + 0xC0), 00102918(+0xD0, +0xD0, +0xB0).
         - handle = 001CCF70(+0xB0).
         - 001CFA60(sp+0x40, +0xD0, +0x244, +0x24C).
         - 001CFBE0(handle, 1, D_00253670, sp+0x40, 0).
    - **Other values:** nothing.
- **2 or 3:** 001AFC10(record).
- **Other values:** nothing.

As a result, a cycle is a wait of 61–100 ticks (the countdown runs to −1), then 76 ramp ticks. That is 75 values
from 0.02 up to the last value <= 1.5, plus the clamping tick. Each ramp tick emits once.

**001CFBE0:**
1. **Free-space guard.** `end` is D_004F35C0 when D_00810E80[0] == 0, otherwise D_005635C0. The rest runs only when
   the signed `end − *(D_00275670 + 0x18)` >= 0x8000.
2. **Selection.** The mode `*(st + 0x8C)` and `kind` select `(row, table)`:
   - mode 1: kinds 0..6 → rows 0, 2, 4, 2, 7, 6, 2;
   - modes 2–4: kinds 0..6 → rows 1, 3, 5, 3, 7, 6, 3;
   - tables for kinds 0..6: 0x230800, 0x231770, 0x232540, 0x233800, 0x230800, 0x231770, 0x23D930.
3. **The chain.** All packets go through 001CB5F0(D_007635C0, id, n):
   - **Packet 1, 7 quadwords:**
     - GIFtag word 0x6C050059;
     - xf +0x44, +0x48, +0x50, +0x4C;
     - the 64-byte matrix;
     - a quadword with 0x14000000 and 0x11000000.
   - **Packet 2:** a reference to `st` through 001CB6B0(…, 9, st). When `copy` is set, 9 copied quadwords
     instead.
   - **Packet 3, 1 quadword:** GIFtag 0x6C090050.
   - **Packet 4, 16 quadwords:**
     - words 0x01000101 and 0x6C0F006E;
     - D_70003A40 (64 bytes), *(xf + 0x40) (64), D_70003AC0 (64), D_00275670 + 0xA0 (16);
     - the words 0, 0, xf +0x54, 0;
     - the selected D_00251260 row.
   - Then 001CB760(D_007635C0, id, table, row address) and 001CB900(D_007635C0, id, mode).
4. **Undefined path.** When the mode is not 1..4, or kind >= 7, the original does not skip. The test runs these
   inputs through the original and sees it emit the chain with whatever s0/s1 held. The native module faults
   instead (`EM_HEAD_SPRITE_FAULT_UNDEFINED`, before any worker call).

**Arithmetic** (docs/EE_FLOAT_MODEL.md):
- `add.s`: pre-trim plus truncation;
- `div.s`: round to nearest-even;
- `cvt.s.w`: truncated;
- `c.le.s`: DAZ plus sign-keeping saturation.

The module implements these on bit patterns (`em_head_sprite_ee_*`). They are checked against
`tools/ee_float_model.py` on 4,350 vectors, or 60,350 under EM_TEST_FULL.

**What the effect is.** The code attaches a sprite packet to the owner's head bone, and the ramp scalar and a
per-cycle random scalar go into the packet. "Breath vapour" fits: it is a cold area and the anchor is at the mouth
offset. Nothing checked here proves that reading. There is no projectile and no AI, so the decomp's "turret" comment
on 001E2560 is wrong.

## Verification

`python3 tools/test_head_sprite_reference.py` (default ~3 s; `EM_TEST_FULL=1` ~12 s) executes the ORIGINAL
instructions from the pinned ELF of 001E2560, 001E23A0, 001B0070, 001CFA60, 001CFBE0, 001F0120 and 001E2290.

The oracle is the crate oracle with div/mfhi, aligned lq/sq and `ee_float_model` COP1. The workers are stubbed
identically on both sides. For every call the test compares:
- every worker call with all its arguments (matrix, vector and rotation bits included);
- every modelled record byte;
- every byte of every emitted packet.

It also asserts that the original wrote nothing outside the record fields, the packet buffers and the stack.

| Group | Quick | Full |
|---|---|---|
| lifecycle 0 (keys × D_008106C8 × RNG) | 344 | 6,400 |
| owner gates (+0x02 × +0x220 specials incl. NaN/Inf/denormal × +0x04 × +0x01) | 160 | 440 |
| sub-states × timer edges × ramp edges × RNG | 220 | 2,520 |
| edges (lifecycles 2/3/4/0x80/0xFF, bone slots, the free-space guard) | 16 | 16 |
| random states | 120 | 2,000 |
| 001F0120 + 001E2290 (keys −2..0x101, 0x13B, INT extremes × alloc/NULL) | 240 | 1,052 |
| 001CFBE0 alone (mode × kind × copy, the ELF's D_00253670, guard edges, undefined paths) | 54 | 70 |
| lockstep from 001F0120 spawn through whole cycles to the owner-death free (keys 0x3B, 0x47) | 840 ticks | 1,800 ticks |

**Coverage.** Every reachable instruction of the seven functions executes (the test asserts it).

**Latched fault.** Every entry point that takes the fault block (`_tick`, `_spawn_001F0120`, `_001E23A0`,
`_001CFA60`, `_001CFBE0`) returns −1 at once when `fault->code` is already set, with no worker call and no write.
The reference test checks this for all five.

**Capture check** (all 15 `build/s87/route` images and 11 `build/startup-reference` images):
- **Ramp values.** All 53 captured nodes have +0x244 on the EE-add ramp from 0, or equal to the 1.5 clamp. Nodes
  in sub-state 0 hold 0 or 1.5.
  - Of the 21 distinct captured values, 16 are off a round-to-nearest ramp.
  - Plain truncation and the EE model agree on this ramp, so the captures cannot tell those two apart.
- **Mid-ramp replay.** The test replays all 16 nodes captured mid-ramp in the route images. Starting from the
  previous ramp value, the ORIGINAL 001E2560 runs with its real callees (SDK matrices, 001CFA60, 001CFBE0,
  001CB5F0/6B0/760/900/9B0) over the captured RAM and scratchpad. It rewrites +0xB0 and +0xD0..+0x10F byte-for-byte,
  and it writes a chain byte-identical to the captured one: at least 0x1C0 bytes of packets and tags per node.
  - D_00810E80 is set from the captured cursor's buffer, because the image is taken after the buffer flip.
  - The native module, with the SDK workers bound to em_crate_original's translations, produces the same record
    bytes and packet bytes.
- **Replay exclusions.** startup-reference/roger-encounter and elevator/clip47 are left out of the replay. In
  those images the owner's bone moved after the node's tick (roger-encounter also seeds player writes). They are
  still included in the ramp check.

**Injected native defects, 13 of 13 caught:**
- the ramp constant; RN vs truncation in div; the +0x09 write; packet word order; the mode 2 row;
- the timer test; the guard bound; the +0x04 bound; the pre-trim; a missing key;
- the D_008106C8 mask; the wait formula; the row index.

`tests/head_sprite_original_test.c` (ASan/UBSan) pins the native contract:
- the spawn gate;
- the 80-tick wait for RNG 99 and the 76 ramp ticks with 3 packets each;
- the owner-death free;
- no tick after free, and the latched fault (tick);
- every ramp worker's failure address;
- NULL workers and views, the bone-slot guard and the owner-view mismatch;
- the undefined 001CFBE0 paths;
- the guard edge.

**Not verified here:**
- The pixels: the GS output this chain produces (VU1 microcode selected through 001CB9B0(1), table 0x231770, row 2)
  was not compared with `opening_gs.bin` or a beat `gs.bin`. The chain bytes that feed VU1 are byte-identical to the
  captures. Rendering them is the renderer's job; no port stage draws this packet type yet.
- The SDK matrix workers themselves: their translations belong to em_crate_original and em_effect_original. This
  test proves the crate versions only on the 16 captured ticks.

## Boundaries (workers; each is required when reached, and a NULL or negative result faults)

| Worker | Original | Contract |
|---|---|---|
| `w_00122BB8` | 00122BB8() | Game RNG; `*value` = v0. |
| `w_001EF9D0` | 001EF9D0(0x80000010, 0, 1.0) | Effect allocation. `*record` = the node, or NULL (the original 0). |
| `w_001AFC10` | 001AFC10(self) | Pool free. |
| `w_001026A0` | 001026A0(+0xB0, m, +0xA0) | `m` = owner bone slot + 0x90 (an address). |
| `w_001029C0` | 001029C0(+0xD0) | Identity. |
| `w_00102C58` | 00102C58(+0xD0, +0xD0, owner + 0xC0) | Z, Y, X rotations, in place. |
| `w_00102918` | 00102918(+0xD0, +0xD0, +0xB0) | Translation. |
| `w_001CCF70` | 001CCF70(+0xB0) | Depth key / chain id (0xFFFFFF when clipped). |
| `w_001CD370` | 001CD370(0) | D_00275670 + 0x2240: the address and its 64 bytes. |
| `w_001CB5F0` | 001CB5F0(D_007635C0, id, n) | Opens n quadwords; returns writable bytes. |
| `w_001CB6B0` | 001CB6B0(D_007635C0, id, 9, st) | Reference tag. |
| `w_001CB760` | 001CB760(D_007635C0, id, table, row) | |
| `w_001CB900` | 001CB900(D_007635C0, id, mode) | Through 001CB9B0 and 001CB6B0. |

**Data views** (a missing view faults with its address):
- `EmHeadSpriteOriginalOwner`: owner +0x01, +0x02, +0x04, +0x220, the +0x110 slot words and +0xC0. It must
  describe the record's +0x24 owner; a mismatch gives BAD_RESULT.
- `EmHeadSpriteOriginalWorld`:
  - D_008106C8 and D_00810E80[0];
  - the cursor *(D_00275670 + 0x18);
  - D_70003A40, D_70003AC0 and D_00275670 + 0xA0;
  - the ELF tables.

## Binding (for the coordinator chain)

**Blocked on the render-context views (Effects step, 2026-09-24).** The ramp
tick's 001CCF70 and 001CFBE0 read context +0x2240, +0xA0, the scratchpad
0x70003A40 / 0x70003AC0 and the packet cursor, and no live code produces
them (EFFECT_MANAGER.md 5.0). The packet-chain workers are translated
(em_packet_chain_original, docs/PACKET_CHAIN.md section 5), so the views are
the only missing input. The notes below apply once they exist.

- **Spawn: the player.** 0015C420 (player init) calls 001F0120(player, 0x3B) after it sets player +0x00 = 1. Call
  `em_head_sprite_original_spawn_001F0120(owner14, 0x3B, &workers, &rec, &fault)` there, where `owner14` is the
  **word stored at player +0x14** (001F0120 copies that word, not the player's address, into record +0x24). The
  player record is at 0x8102B0 and its +0x14 word holds its own address, 0x8102B0, in all 26 captured images, so
  the value passed is 0x8102B0. Roger's record 0x7A8830 likewise holds 0x7A8830 at +0x14. Read the word; do not
  substitute the record address.
- **Spawn: Roger.** 001BA8E0(self, type) calls 001F0120(self, type), or (self, 1) when D_00810788 == 1 and type ==
  0x3B. Roger's captured key is 0x47. Call the spawner at the same point, with the same key rule, from the Roger
  init path.
- **`w_001EF9D0`.** Bind it to the effect lane's `em_effect_original_001EF9D0(e, 0x80000010, NULL, 1.0f, &node)`
  (docs/EFFECT_ORIGINAL.md). The adapter then maps that node's canonical pool record to an `EmHeadSpriteOriginal`
  with `self` = the record address.
  - The pool (001AFA90) clears +0x04/+0x05 and the +0x1F0 block. The record starts in lifecycle 0.
  - Do not duplicate 001EF9D0.
- **Per tick.** The actor-pool walk (001AFD70) reaches callback 0x1E2560 at node +0x10. Bind that callback slot to
  `em_head_sprite_original_tick(&rec, &owner_view, &world_view, &workers, &fault)`. It returns 1 while allocated, 0
  after the free and −1 on a fault. Fault codes 1..4 equal `EM_SCENE_FAULT_*`; map 6 (UNDEFINED) to BAD_RESULT.
  - **Order.** In the route images the node ticks after its owner's bone palette is final for the frame: its B0/D0
    reproduce from the captured bone matrix. The adapter must keep that order: the owner's 0015BCF0 palette stage
    first, then this node. The pool slots are player node 39, Roger node 47 and Roger himself 17. This lane did not
    measure the pool walk order itself.
- **Views.**
  - The owner view for the player comes from the player record at 0x8102B0: +0x01/+0x02/+0x04/+0x220, the slot words
    at +0x110 (bone 7's matrix is that slot + 0x90) and the Euler at +0xC0. For Roger, use the 0x7A8830 record.
  - The world view: D_008106C8 is an area flags word (the word 001B0070 returns); bit 3 kills the effect at init.
    What the other bits mean was not established here. Also D_00810E80, the packet cursor, the scratchpad matrices
    D_70003A40 and D_70003AC0 (camera), and ctx + 0xA0.
  - **Refresh the cursor at every 001CFBE0 entry.** `EmHeadSpriteOriginalWorld.cursor` is a snapshot of
    *(D_00275670 + 0x18), which 001CFBE0's free-space guard reads on entry. Every emission earlier in the frame
    (other nodes, other packet builders, and this node's previous emissions) advances that word. The adapter must
    re-read *(D_00275670 + 0x18) into `cursor` immediately before each call that can reach 001CFBE0 (each
    `em_head_sprite_original_tick` of a node in the ramp, and any direct `_001CFBE0` call). A value cached once
    per frame makes the guard pass where the original skips.
- **SDK workers.** These translations are available:
  - em_crate_original's `em_crate_sdk_apply`, `_identity`, `_euler` and `_translate` (proven here on the 16 captured
    ticks);
  - or the effect lane's `em_effect_original_001026A0` and `_00102C58`. That module exports no identity or
    translate, so those two still come from em_crate_original.
- **`w_001CCF70`.** Bind it to `em_effect_original_001CCF70`.
- **Packet workers.** `w_001CD370` and the chain workers (001CB5F0/6B0/760/900) belong to the renderer's packet sink.
  - For trace and capture comparisons, build the original chain bytes. The layout of the byte-matched C: a packet of
    n quadwords at cursor + 0x120, the cursor advancing (n + 2)·16, and each tag advancing it 0x20.
  - For drawing, hand the 4×4 D0 matrix, the ramp +0x244, the scalar +0x24C, st = D_00253670, table 0x231770 and
    row 2 to a VU1 sprite stage. That stage does not exist yet and is out of this lane's ownership. Until it exists
    the node must stay unwired, or be wired with the packet sink as a recorder. Never substitute a stand-in sprite.

**Makefile hunks needed:**
```
.PHONY: test-head-sprite-original
test-head-sprite-original:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffp-contract=off -Isrc tests/head_sprite_original_test.c src/game/em_head_sprite_original.c -lm -o build/head_sprite_original_test && ./build/head_sprite_original_test

.PHONY: test-head-sprite-reference
test-head-sprite-reference:
	python3 tools/test_head_sprite_reference.py
```
