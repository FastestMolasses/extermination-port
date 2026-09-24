# Render context lane (census L30-render-context)

Date: 2026-09-23. Module: `src/game/em_render_context.{h,c}`. Oracle:
`tools/test_render_context_reference.py`. Sanitizer fixture:
`tests/render_context_test.c`. Neither is in the Makefile yet (section 7
gives the hunks); the module is built and tested but not bound.

This lane covers the 16 functions FIRST_LEVEL_CENSUS.md groups under
L30-render-context. All 16 are translated from the original code and checked
against the original instructions over captured RAM. None is live: binding
belongs to the coordinator (section 3 names each call site).

Names used below describe what the instructions do. They are not claims
about what the player sees ("a label is not evidence"). The decomp's
comment on 001DDE10 calls it a "radar/altimeter HUD bar builder". Nothing
here confirms that, and this doc does not use the name.

## 1. Status, per function

"Before" is the census row. "After" is what this lane delivers: an oracle
that executes the original checks the translation, and nothing live calls it
yet (the census status verified-unbound).

| Function | Decomp | Before | After | What it does |
|---|---|---|---|---|
| 001DD7B0 | NM | missing | verified-unbound | GS block set-up (see 2.1) and clears the 0x24-byte block at context +0x24F0, with +0x20 = 1 |
| 001DD940 | BM | missing | verified-unbound | tail jump to 001DD7B0 |
| 001DD950 | BM | unverified | verified-unbound | context +0x2450 = the quadword at a0; +0x2460 = 16777215 / f12 (DIV.S); +0x2464 = f13 |
| 001DDA00 | BM | missing | verified-unbound | per-frame tick (2.2) |
| 001DDAA0 | BM | missing | verified-unbound | area-key dispatch to 001DE920 or 001DDE10 (2.2) |
| 001DDE10 | BM | missing | verified-unbound | the four-sprite pass (2.3) |
| 001DEEE0 | BM | missing | verified-unbound | the ramp machine on a 0x20-byte record (2.2) |
| 001E0C30 | BM | missing | verified-unbound | context bytes +0x170..+0x173 = 0, word +0x174 = 0, then tail jump to 001E1010 |
| 001E0C60 | BM | missing | verified-unbound | returns context word +0x174 AND (1 << ((a0 - 0x20) & 31)) |
| 001E0C80 | CL | missing | verified-unbound | sets (a1 != 0) or clears that bit; returns 1 when the bit was set BEFORE, in both directions |
| 001E0CC0 | BM | missing | verified-unbound | 001D2DE0(0, 0); context words +0x1D8 and +0x1E8 = 0 |
| 001E0D70 | BM | missing | verified-unbound | the pending kick of context word +0x2520 (2.4) |
| 001E0DF0 | BM | missing | verified-unbound | the release of +0x1D8 / +0x1E8 / +0x2520 through 001D21B0 tags (2.4) |
| 001E1010 | BM | missing | verified-unbound | 16 x 16 records of 0x18 bytes at D_0081E0F0: +0 = (256 * j) / 16, +4 = (256 * i) / 16 (CVT.S.W, MUL.S, DIV.S), +8 = +0xC = 0; +0x10..+0x17 untouched |
| 001D5370 | NM | missing | verified-unbound | the grid pass (2.5) |
| 001D52E0 | BM | missing | verified-unbound | grid header: e = 001C6120(*D_0028A5A0, 0); context +0x144 = e[0], +0x148 = e[1], +0x150..+0x164 = e[2..7], +0x140 = e + 0x20 |

The module also translates seven helpers the lane functions reach. The census
lists them as boundary (GS/VIF packet build). They are translated here
because they only read or write render-context words and packet bytes:

| Helper | What it does |
|---|---|
| 001D2910 / 001D2710 | flag query. a0 < 0x20 (signed): context word +0x0C AND (1 << (a0 & 31)). 0x20 <= a0 < 0x40: 001E0C60. Anything else returns 0. |
| 001D2E00 / 001D2DE0 | read / write context word +0x2520 + 4 * a0 |
| 001D21B0 | at the context +0x08 cursor: byte +3 = 0x50, word +4 = a0, halfword +0 = 0; cursor += 0x10 |
| 001D6B10 | 001D6930(a0, a1, a2, a3, t0 = D_0026E510), then 001D1F20(a0); returns 001D6930's result |
| 001D6C90 | one 0x60-byte packet at the channel cursor (2.3) |

00102948, the quadword copy, is translated inline where 001DDE10 calls it.

## 2. What the code does

### 2.1 001DD7B0

The routine stores D_0081C050 = D_0081C054 = 0, D_0081C058 = 0x11000000 and
D_0081C05C = 0x50000009. It then edits the following fields read-modify-write:
- halfword D_0081C060: low 15 bits = 8, bit 15 kept;
- byte D_0081C061: bit 7 set;
- byte D_0081C065: bit 6 cleared;
- byte D_0081C067: bits 2..3 cleared, then bits 4..7 = 1;
- byte D_0081C068: low nibble = 0xE.

It then calls 001006D8(D_0081C070, 0, 0x100, 0x100, t0 = 0, t1 = 2). The
NEARMISS C drops t0 and t1; the .s passes them. After that call it reads
D_0027568C and stores halfword D_0081C070 = (old & 0xFE00) |
((D_0027568C >> 13, arithmetic) & 0x1FF). Last, it clears the context block
at +0x24F0.

### 2.2 001DDA00, 001DDAA0, 001DEEE0

**001DDA00** runs these steps in order:
1. 001DEEE0(context + 0x2470), then 001DEEE0(context + 0x2490).
2. If flag 1 is set: 001DDAA0.
3. If 0015D2F0() == 2, D_008106C6 == 2 and flag 6 is set: 001DDB70.
4. If flag 7 is set: 001DFF70.

**001DDAA0** builds key = (D_00810700 << 8) + D_00810701. When the key is
0xB00, 0xC00, 0xD00 or 0xE00 and 001B0070() & 0x60 is non-zero, it calls
001DE920. In every other case it calls 001DDE10.

**001DEEE0(p)** reads state byte p+0 and flag number p+8, and uses p+0x1C as
a counter with p+4 as its limit. Every flag test goes through 001D2910:
- state 0: counter = 0; next state 1 if the flag is set, else 3.
- state 1: flag clear → state 2. Counter += 8, clamped to the limit (signed).
  Then 001DF110(p + 0x10).
- state 2: counter -= 8; at <= 0 it becomes 0 and the state 3. Flag set →
  state 1. Then 001DF110(p + 0x10).
- state 3 and any other value: flag set → state 1.

### 2.3 001DDE10 (the four-sprite pass) and 001D6C90

1. key = (D_00810700 << 8) + D_00810701. The code is 0015D2F0(), forced to 2
   when D_008104E0 is 0xC, 0xD or 0x29. v = 0022EBE0().
2. v != 0:
   - q = the quadword at context +0x2450, then q = 001026A0(D_70003AC0, q);
   - q[2] = fabsf(q[2]) and q[3] = fabsf(q[3]) (worker 0011DF78);
   - q[3] += 100; q[2] = (16 * q[2]) / q[3]; q[2] /= 4.

   v == 0:
   - context +0x2450 = the quadword D_00810360;
   - +0x2460 = 16777215 / D_00275690 (the old value); +0x2464 = 1.0;
   - q = 001026A0(D_70003AC0, context +0x2450);
   - q[3] += D_00275694; q[2] = (16 * q[2]) / q[3] / 4.
3. The eases take their targets from the code: 0x82, 3 and 2 give
   (40500, 1500); 1 gives (30500, 1500); anything else gives (8500, 50).
   - D_00275690 += 0.05 * (T1 - D_00275690);
   - D_00275694 = 0.05 * (T2 - D_00275694). This is not an ease: the
     instructions store the product.
4. For each of the four slots i, t is context +0x2460. r1 = float_to_int(t +
   (g * (q[2] - t)) / 8) and r2 is a width:
   - flag 7 set: with w = 2 * context +0x1F4, g = 8, 4 + 3.5w, 1.5 + 5.5w,
     1 + 5w, and r2 = float_to_int(62 + 30w);
   - else v != 0: g = 4, 2, 1, 0.5 and r2 = 0x3E;
   - else key == 0xB00: g = 8, 7, 6, 5 and r2 = 0x18, 0x28, 0x38, 0x48;
   - else: g = 8, 4, 1.5, 1 and r2 = 0x3E.

   The g = 1 forms have no multiply. Every operation keeps the original's
   operand order.
5. Context +0x24F0..+0x24FC and +0x2500..+0x250C hold the four values and
   widths. They ease by 0.15 toward (float)r1 / (float)r2 when v == 0 and
   the +0x2510 word is 0. Otherwise they are set directly. Then +0x2510 = v.
6. For each slot k, cc = float_to_int(value[k]) and rr =
   float_to_int(width[k]). The calls are 001D6B10(3, D_0027568C, 8, 8),
   001D6BA0(3, D_0027568C, 8, 8, 0, 0), 001D1FF0(3, 3), then 001D6C90 with
   (3, 0, 1, 0, 0, 1, 0, 0) and stack words (1, 2, 0, 1, 0, 1, 0). After
   those calls the routine writes one 0x80-byte packet at the channel 3
   cursor (context +0x1C) and advances the cursor by 0x80:

   | Offset | Contents |
   |---|---|
   | +0 | halfword 7; +3 = 0x10; word +4 = 0 |
   | +0x10..+0x1F | zero, then +0x1C = 0x50000006 |
   | +0x20 | dword 0x50AB4000_00008001 |
   | +0x28 | dword 0x43431 |
   | +0x30, +0x34, +0x38 | 0x80 |
   | +0x3C | rr |
   | +0x40, +0x44 | 8 |
   | +0x50, +0x54, +0x58, +0x5C | 0x7000, 0x7900, cc, 0x80 |
   | +0x60, +0x64 | 0x1008 |
   | +0x70, +0x74, +0x78, +0x7C | 0x9000, 0x8700, cc, 0x80 |

   The packet leaves +2, +8..+0xF, +0x48..+0x4F and +0x68..+0x6F as they
   were.
7. 001D1F20(3). An end tag goes at the cursor (+3 = 0x60, word +4 = 0,
   halfword +0 = 0; cursor += 0x10). Then 001CB760(D_007635C0, 0xFFF000,
   the cursor as it stood before step 4).

**001D6C90(a0..t3, 7 stack words)** writes a 0x60-byte packet at the
cursor context + 0x10 + 4 * a0 and advances it by 0x60:
- header: +3 = 0x10, word +4 = 0, halfword +0 = 5;
- +0x10..+0x1F: zero, then +0x1C = 0x50000004;
- the dwords at +0x20..+0x58 are 0x8003 | 0x10000000 << 32, 0xE,
  sign-extended a1, 0x3B, the +0x40 word, 0x47, the +0x50 word and 0x42;
- +0x40 = a2 | a3 << 1 | t0 << 4 | t1 << 12 | t2 << 14 | t3 << 15 | s0 << 16
  | s1 << 17, where s0..s6 are the stack words and every register is
  sign-extended to 64 bits;
- +0x50 = s2 | s3 << 2 | s4 << 4 | s5 << 6 | (u32)s6 << 32.

It returns the packet address + 0x10.

### 2.4 001E0D70, 001E0DF0

**001E0D70** reads pending = context +0x2520. It acts only when flag 0x20 is
set, 001B0070() & 0x0E000000 is 0 and pending != 0. Then it calls
001CB760(D_007635C0, 0xFFC000, pending) and 001D2DE0(0, 0).

**001E0DF0** acts only when flag 0x20 is set:
- it tags the context words +0x1D8 and +0x1E8 through 001D21B0 when they
  are non-zero;
- it tags the +0x2520 word when that is non-zero and flag 5 is clear.

### 2.5 001D5370 (the grid pass)

The grid address is context +0x140, and its row stride is context +0x148.
The loops cover every x in [x0, x1) (the grid row, multiplied by the stride),
every z in [z0, z1) and every slot 0..3. The address is computed in 32-bit
wrap:

    grid + (((x * stride + z) * 4 + slot) * 4)

The bounds are 0..0x20 for both, with two exceptions:
- key 0x1300, rooms 5/7/8/9: x in [0, 0x1E), z in [0xC, 0x20);
- key 0x1300, room 4: x in [9, 0x20), z in [0, 0x19);
- key 0xD00, room < 4: x in [0x14, 0x17), z in [0xF, 0x17).

The room is D_00810702.

Before the loops the routine calls these, in order:
1. 001D4DA0;
2. 001D2D20(D_70003440, context +0x2468, 4096, 4096, 0.1, 16711680);
3. 001026D0(D_70003440, D_70003440, D_00810610);
4. the same two calls again for D_70003400, with 1024, 448 in place of 4096,
   4096.

It reloads context +0x2468 before each 001D2D20 call. The four rows at
D_70003400 are then loaded as the matrix M.

For each slot id > 0 (signed), obj = 001C6120(*D_0028A5A0, id). D_0028A5A0
is reloaded for every object. Two quadwords are then loaded: A = obj + 0x10
and B = obj + 0x20, each with its low four address bits cleared.

Eight corners are tested. Each corner takes its x from A.y or B.y, its y
from A.z or B.z and its z from A.w or B.w. The x source alternates fastest,
then the y source. The first four corners take their z from A.w, the second
four from B.w. For each corner the routine computes:

    ACC = M0 * x;  ACC += M1 * y;  ACC += M2 * z;  p = ACC + M3 * 1.0

These are the VU0 forms VMULAbc y, VMADDAbc z, VMADDAbc w and VMADDbc w,
dest xyzw. The clip flags of p.xyz against |p.w| (section 4) are then
collected into one 24-bit word per group of four: lo for the first four
corners and hi for the second four. The first corner of a group lands in
bits 18..23.

The object is skipped when all eight corners are outside the same plane.
That is v = lo & hi, folded over its four 6-bit lanes and masked to 0x3F,
with ((v ^ (v << 1)) & 0x2A) != 0.

If the object is not skipped:
- **(lo | hi) & 0xFFFFFF == 0:** 001D4FB0(obj). The matrix is not reloaded;
  the original keeps it in VU registers.
- **otherwise:** the eight tests run again against the four rows at
  D_70003440.
  - If none is outside: 001D4FB0(obj).
  - Else: 001D4FB0(obj), 001D1F80(0, 1, 0), 001D4B20(obj) and 001D4DA0().

  In both cases M is then reloaded from D_70003400.

After the loops, 001D5BD0 runs for keys 0x1500, 0x1100, 0x1001, 0x1000,
0xF00, 0x803, 0x806, 0x801, 0x805, 0x800, 0x703, 0x700, 0x601, 0x600,
0x401, 0x400, 0x301, 0x101 and 0x100.

### 2.6 What the first level exercises (captured RAM)

Every capture used here has the same state: the opening capture and all 15
route beats.
- The context is at 0x811CC0.
- Flags: context +0x0C = 0x43 (flags 0, 1 and 6) and +0x174 = 3 (flags 0x20
  and 0x21).
- Both 001DEEE0 records are in state 3. Their flags, 2 and 9, are clear.
- The key is 0x0B00. The room is 0, except 09_fence_door, where it is 2.
- D_008106C8 = 0x20081910 (09_fence_door: 0x20089910), so 001DDAA0 always
  reaches 001DDE10.
- 0022EBE0 is 1 in the opening (D_008101E4 = 3) and 0 in every route beat.
- Flag 7 is clear, so the route uses the key-0xB00 arm (widths 24/40/56/72).
  The captured widths at +0x2500 are exactly 24, 40, 56, 72 in every route
  beat.
- 001D5370 visits 701 bank objects in each capture.
  - opening: 19 reach 001D4FB0, 12 of them through the second test and
    001D4B20;
  - 05_boxes: 104 reach 001D4FB0, 9 through 001D4B20;
  - 14_roger_encounter: 72 reach 001D4FB0, 7 through 001D4B20.
- On the route, 001DDA00 reaches none of 001DE920, 001DDB70, 001DFF70 or
  001DF110. The test asserts this for every beat, which agrees with the
  census.

## 3. Binding notes (for the coordinator)

### 3.1 Call sites

Each line names the original call site, then the live stand-in or gap, then
what to bind.

**001D1EA0(a0)**, frame close-out (both world variants; the status frame
uses a0 = 0).
- Live stand-in: `em_render_001D1EA0` in `em_render_frame.c`
  (`w_001D1EA0` in `em_scene_bindings.c`), which runs today's
  `frame_close_out`.
- The original runs `001E0D70(); 001DDA00();` when a0 != 0 and
  001D2910(4) == 0, and then 001CB800 in every case.
- Bind `em_render_context_001E0D70` and `em_render_context_001DDA00` in that
  order, gated by `em_render_context_001D2910(s, 4, ...)`. 001D1EA0 and
  001CB800 are not in this lane.

**001D19E0**, render init.
- Today it is the no-effect binding `UM_001D19E0` in
  `em_scene_bindings.c`.
- It calls 001DD940 (at 001D19F8), 001E0C30 (at 001D1A00) and 001E0CC0 (at
  001D1A80) among other callees.
- Bind `em_render_context_001DD940`, `_001E0C30` and `_001E0CC0` at those
  positions. The rest of 001D19E0 belongs to another lane.

**0x1AE040 state**, the `SF_CALLV(..., 0x1E0CC0u, w_001E0CC0, 0)` in
`em_scene_frame.c`.
- Today it is `um_001E0CC0`, the reported no-effect binding.
- Replace it with `em_render_context_001E0CC0`. The call site passes a0 = 0,
  which 001E0CC0 does not read.
- 001E0CC0 is also called by anim_frame_top_b and by 001E0CF0.

**001C1D00(D_008101D0)**, render-env step.
- Live stand-in: `em_render_001C1D00`, which calls `render_env_init()` and
  is described as a "skeleton no-op".
- The original calls 001E0CF0 and then 001D5370 in state 1 (and 001E2260
  first for key 0x1500).
- Bind `em_render_context_001D5370` after 001E0CF0. 001E0CF0 and 001E2260
  belong to L31.

**001C1E70**, a thunk to 001D52E0, reached from 001C1DC0 (L31).
- Bind `em_render_context_001D52E0` there.

**001D2300** (L31, reached from `gs_readback_queue_run`).
- Bind `em_render_context_001E0DF0` at its call.

**001DD980**, the live `em_interaction_projection_publish`
(`em_interaction_projection.c`). This is a stand-in for the 001DD950 part:
- it stores the target's xyz with w = 1 at +0x2450;
- the original copies the whole quadword at a0 (D_008105E0), w lane
  included. Its w is 1.0 in the captures, but it is not forced.
- it computes the +0x2460 quotient in host double arithmetic, where the
  original uses DIV.S (em_ee_div_bits).

Replace the store half with `em_render_context_001DD950(s, 0x008105E0,
bits(2 + 1.02 * d), bits(d))`. The distance d still comes from 001DD980,
which is live and verified elsewhere.

**001D2830**, the flag setter, called from many owners.
- For a0 in 0x20..0x3F it is 001E0C80. Bind `em_render_context_001E0C80`
  there.
- 001D2830 itself, and 001D2730 for flags below 0x20, are not in this lane.

### 3.2 Workers (EmRenderContextWorkers)

The table gives the binding the oracle test uses. Each "replay" worker still
needs a real translation, in the lane shown.

| Worker | Binding in the test | Live binding needed |
|---|---|---|
| 0011DF78 | `em_sdk_math_original_0011DF78` | same |
| 001281C0 | `em_player_float_to_int` | same |
| 001D6930, 001D1F20, 001D6BA0, 001D1FF0, 001D1F80, 001006D8 | `em_load_veil_particles_*` on one shared packet window | same (em_load_veil_particles.c is verified-unbound too) |
| 0015D2F0 | replay | L28 (live today: an `em_weapon.c` stand-in that assumes code 0) |
| 0022EBE0 | replay | L35 (missing) |
| 001B0070 | replay | the verified `em_player_stage_workers` / head-sprite copy (returns D_008106C8) |
| 001026A0 | replay (matrix at D_70003AC0) | an existing verified m*v translation, fed the D_70003AC0 matrix |
| 001CB760 | replay | L39 (only a worker today) |
| 001D2D20 | replay | L32 (stand-in: em_math.h native projection) |
| 001026D0 | replay | the em_status_models `w_001026D0` translation |
| 001C6120 | replay | the owner-services / pose-host translations |
| 001D4DA0, 001D4FB0, 001D4B20, 001D5BD0 | stub (call and arguments compared) | GS/VIF boundary and L29b (001D4FB0) |
| 001DE920, 001DDB70, 001DFF70, 001DF110 | stub | not reached on the first-level route; they must stay bound to fail-stop workers until translated |

### 3.3 Data

Every byte is reached by its original address through
`EmRenderContextWorld.views`. `world.ctx` is the value of D_00275670. The
views must cover:
- the render context, +0x08..+0x2533: flags +0x0C and +0x174, cursors
  +0x08 and +0x10..+0x1C, grid +0x140..+0x167, bytes +0x170..+0x177, handles
  +0x1D8 / +0x1E8, +0x1F4, +0x2450..+0x2467, the ramp records
  +0x2470 / +0x2490, +0x24F0..+0x2513 and +0x2520..;
- D_0027568C, D_00275690, D_00275694, D_0028A5A0, D_008104E0, D_008106C6,
  D_00810700..D_00810702 and the quadword D_00810360;
- 0x81C050..0x81C07F, and 0x81C070..0x81C0EF for the 001006D8 worker;
- 0x81E0F0..0x81F8EF;
- the scratchpad matrices 0x70003400..0x7000347F;
- the static-object bank: the grid and every object's +0x10..+0x2F;
- the packet memory the cursors point into.

The 001006D8 and packet workers must write through the same storage these
views read. No port subsystem owns these bytes today. The binding has to
provide the render-context image and the packet window, and the native
renderer then consumes the packets.

## 4. Arithmetic, and the one unmeasured rule

**COP1.** Every COP1 operation uses the measured EE model
(`em_ee_float.h`, docs/EE_FLOAT_MODEL.md section 2): ADD.S, SUB.S, MUL.S,
DIV.S (nearest-even) and CVT.S.W (truncating). fabsf and float_to_int are
workers.

**VU0 products.** The 001D5370 products use `em_vu_vec_bits` with the
measured forms VMULAbc (dest xyzw, bc y), VMADDAbc (bc z and w) and VMADDbc
(bc w).

**The clip test is not in the measured model.** EE_FLOAT_MODEL.md does not
record the VU clip test. The module applies DAZ to x, y, z and w and
compares magnitude bit patterns:
- x positive with |x| > |w| sets the plus flag;
- x negative with |x| > |w| sets the minus flag.

For finite values this is exactly x > |w| and x < -|w|. A lane with exponent
255 is not modelled, so the module faults (`EM_RC_FAULT_UNMEASURED`) and the
oracle raises. The oracle (RenderEE) implements the same rule
independently, and the test checks both on 4000 vectors. The test cannot
show that the hardware agrees. In every capture no exponent-255 lane occurs
(it would have failed). The objects' corners are finite, so the rule
reduces to the ordinary compares.

## 5. Verification

`python3 tools/test_render_context_reference.py`:
- about 5 s by default, sampling 420 of 1,712 unit cases;
- `EM_TEST_FULL=1` runs everything in about 10 s: 1,792 cases, with 001D5370
  on all 16 captures.

The oracle executes the ORIGINAL instructions of all 23 routines from the
pinned ELF. It uses FallEE's COP1 and VU0 model and adds vclipw and the CLIP
register read. It runs over:
- the opening capture (`opening_ee.bin` + `opening_scratchpad.bin`);
- every route beat (`build/s87/route/<beat>/eeMemory.bin` +
  `scratchpad.bin`).

Every case runs the original over one copy of the image and the native
module over another. It then compares:
- all 32 MB of RAM and the 16 KB scratchpad, byte for byte;
- the ordered worker log (address and arguments);
- the return value.

The test also asserts the following:
- **Callee set.** The jal and j targets of the 23 routines are exactly the
  translated routines, the 24 workers and the inline 00102948 (38 targets).
- **Stub trees.** The trees under 001D4FB0, 001D4B20 and 001D4DA0 (11
  functions, from the decomp's FUNCTIONS.csv sizes) contain no COP2, lqc2 or
  sqc2, and no indirect call. Their scratchpad bases are used only at
  offsets 0x0000, 0x0004 and 0x3AC0. That is why the stub treatment and the
  no-reload path of 001D5370 are safe.
- **Branch coverage.** Every one of the 103 conditional branches in the
  translated routines is taken both ways, except four listed outcomes. At
  001DE160, 001DE2D8, 001DE418 and 001DE548, 001DDE10's per-slot dispatch
  falls through only for a slot index outside 0..3, and the loop counter is
  always 0..3.
- **Route beats.** On every beat 001DDA00 reaches 001DDE10 (one 001CB760
  call) and none of the four unreached effect callees.
- **Capture evidence.**
  - In all 16 captures, the 001E1010 table and the D_0081C050..D_0081C07B
    block equal what one executed original call leaves.
  - The captured grid header (context +0x140..+0x167) equals what 001D52E0
    rewrites.
- **Fail-stop.** 23 probes each leave one reachable worker unbound. The
  routine returns NULL_WORKER with that address before writing any byte;
  the 32 MB and the scratchpad are unchanged.

Mutation check, run by hand on a scratch copy and not kept:
- these injected errors all fail the default run: the 0.15 constant one ulp
  off, the plus/minus clip flags swapped, `id < 0` instead of `id <= 0`,
  and one packet word off by one;
- a 001E0C80 that returns 0 on a clear also fails the default run now. It
  passed before the 001E0C80 cases were made to force the old bit both
  ways.

`tests/render_context_test.c` runs under ASan/UBSan. It checks:
- the flag words;
- that the packet view bound is checked before the first write;
- the latch;
- the view and worker fail-stop;
- the clip rule.

## 6. Limits (honest)

- **Nothing is bound.** Each stand-in named in section 3 still runs live.
- **Replay workers.** In the test, 0015D2F0, 0022EBE0, 001B0070, 001026A0,
  001CB760, 001D2D20, 001026D0 and 001C6120 take their effects from the
  original. The test proves the calls and arguments, not those callees.
- **Stubs.** The stubbed callees do not run in the test. That is sound for
  this lane's own logic (see the stub-tree check), but their packets are not
  compared here.
- **No consumer.** The packets 001DDE10 builds are compared byte for byte.
  No native renderer consumes them yet, so the port does not draw these four
  sprites. What they look like on screen is not established.
- **Fail-stop is entry-checked.** A worker that returns an error mid-routine
  faults at once and leaves the bytes written before it. The requirement
  checks are conservative: 001DDE10 requires the D_00810360 view even when
  v != 0.
- **vclipw.** The rule is unmeasured (section 4).
- **Unit cases** poke synthetic states into the opening capture. Only the
  capture cases are states the game really reached.

## 7. Makefile hunks (for the lead)

Test targets:

```make
.PHONY: test-render-context-reference
test-render-context-reference:
	python3 tools/test_render_context_reference.py

.PHONY: test-render-context
test-render-context:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffp-contract=off -Isrc tests/render_context_test.c src/game/em_render_context.c -o build/render_context_test && ./build/render_context_test
```

Add `src/game/em_render_context.c` to COMMON when the first call site in
section 3 is bound. It compiles with zero warnings in the private app build
and under `-Wpedantic -Wconversion -Wshadow`.
