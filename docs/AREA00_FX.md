# AREA00 effects and packets lane (A00FX)

Lane A00FX of the level-3/4 side track. The census delta
(`../Extermination/build/s87/census/a00_delta.json`) lists 33 new boot
functions of AREA00 in the subsystems fx_render, gs_upload, render_vif,
frame_update and frame_main (14,468 bytes). Those names are the census's
labels only; nothing below relies on them.

- 28 are translated here, in six modules grouped by caller.
- 5 were already translated in the port and verified by their own
  original-instruction tests. They are not duplicated or edited; section 4
  checks them on AREA00 inputs.

Nothing is wired. Each module is a standalone translation, and every callee
outside the lane is reached through one callee interface by its original
address (section 1).

## 1. Modules and functions

`src/game/em_area00_fx.h` is the public header, and
`src/game/em_area00_fx_internal.h` holds the shared helpers.

**Memory.** Every original byte is reached through the views of
`EmArea01RenderCore` (em_area01_render_mem.h) by its original address: EE
RAM, the scratchpad at 0x70000000 and the EE stack. Each routine carves its
frame below `sp` with the original's frame size, so a stack address that a
callee receives equals the original's. Quadword loads and stores ignore the
low four address bits, as the EE does.

**Callees.** Every function outside the lane goes through
`EmArea00Fx.call(ctx, target, sp, regs, &v0, &f0)`. `regs` carries a mask and
the 64-bit images of the integer registers the original sets up for that
call, or that the callee reads, plus the float argument registers as
binary32 bits. 00102948 and 00102958 (quadword copies) run inline. Lane
routines call each other directly. 001F5040's call through the node's +0x4C
passes the loaded pointer as the target.

**Arguments** are the original's register images. Where the original
compares or moves a whole 64-bit register (the mode tests of 001CD940 and
001F3E30, the kind tests of 001F2F90 / 001F3340 / 001F3620, the strip indices
of 001F15F0, the results of 001CCF70 and 001CA7B0), the translation does too.

**Fail-stop**, as in the UI lane. The first fault is latched: address, code
and detail. After it, no store is made, no callee is called and every load
reads 0. Every entry then returns −1, and every loop also stops on the latch.
The codes are:
- 1: `call` is NULL (the address is the callee's);
- 2: a callee returned a negative value (the address is the callee's);
- 4: an address that no view covers (the address is the running routine's,
  the detail is the data address);
- 6: a VU form or clip lane outside the measured model, or an integer
  division by zero.

**Arithmetic.** Every COP1 and VU0 operation goes through `em_ee_float.h` on
bit patterns, and every VU form used is in its measured table. The VU clip
test follows the render lane's rule (DAZ, then magnitude compares against
|w|; an exponent-255 lane is code 6).

### packets (`em_area00_fx_gs.c`)

| addr | decomp source | what it does |
|---|---|---|
| 001CD940(mode, p0, c0, p1, c1) | NEARMISS | Clip-tests both ends through the matrix from 001CD370(0); any flag of either end returns. Each end is then projected through 0x70003AC0 with the context +0xA0 quadword: x, y ← x, y / w; w ← w − 1.0; z ← z / (w − 1); the fog lane is ca0.z + ca0.w·(w − 1), then the smaller of that and ca0.x, then the larger of that and +0; all four lanes go to 12.4, at 0x70003600 / 0x70003610. c0 and c1 are copied to 0x70003620 / 0x70003630. For a nonzero mode, each end's fog word becomes w >> 4, clamped to 0..0xFF. Mode 1 then scales the colour's alpha, and modes 2 and 3 scale its r, g and b; each scale is the low word of the product, shifted right 8. The fog word then becomes 0xFF0. Last, 001CB5F0(D_007635C0, the 0x70003608 word, 4) gives the slot: C0, P0, C1, P1 are copied there, then 001CB6B0(…, 2, D_00251240) and 001CB900(…, mode) run. Both clip-space vectors are also spilled to the frame. |
| 001D6DD0(ch, a1, a2) | asm words | One tag on the channel cursor at context + ch·4 + 0x10 (id 0x10, qwc 3; the cursor word is loaded again before each tag store), cursor += 0x40. The body is a zero quadword, 0x50000002, the GIF tag 0x1000000000008001, 0xE, the data `sext(a1) \| a2 << 32` and the register 0x18. Returns the body address. |
| 001D7510(ch, a1, a2) | NEARMISS | The same tag with qwc 4, cursor += 0x50. Its fourth doubleword is `sext(a1) << 35 \| sext(a2) << 34 \| 0x2_24020000 \| (context +0x9C ≠ 0 ? 0x700 : 0)`. The flag word is read before the cursor. |
| 001D7A80(ch, a1, a2, a3, t0) | NEARMISS | The same tag with qwc 7, cursor += 0x80. It holds the tag `sext(a1 \| 0x116) << 47 \| 0x5000400000008001`, then 0x43431, then five quadwords copied in the order t0, a3, a2, a3 + 0x10, a2 + 0x10 (each loaded just before its store). Returns the body address. |
| 001F4A00(a0, a1) | byte-matched C | A tail call of 001F4BF0(a0, a1). |
| 001F4E20(a0, a1, f12) | byte-matched C | A tail call of 001F4D40(a0, a1, f12, f13 = 5.5). |

### spawn wrappers and the subtype-6 handler (`em_area00_fx_spawn.c`)

| addr | decomp source | what it does |
|---|---|---|
| 001AA840() | NEARMISS | Walks the D_00275B84 (halfword count) pointers at `*D_00275B7C`. A non-null entry with +0 == 1, (+2 & 0x1F) == 4 and +3 == 0x29 goes to 001AA7A0, **in a1** (the loop register); a nonzero answer stops the walk. |
| 001EF510(a0, a1) | NEARMISS | The 001EA240 handler of effect subtype 6 (effect 0x80000009). If work +0x54 == 0 (EE compare), it calls 001EFD90(0x80000036, D_00275C30 +0xB0, +0xC0). Then it runs three rounds with the tables D_00257990, D_00257AB0 and D_00257A20, with 001EEEB0(a0, a1) after the first. Each round sets f13 = ((w >> 16) & 0xFFFF) / 65535 + 1e-4 from the LCG word w = work +4 (which becomes w·37 + 11 before the divide's result is used). It reloads D_00275C34 for f12 = work +0x54, then calls 001CFB50(D_0081F8F0, 0, a0; f12, f13, 1.0, 1e-6, 6.0) and 001CFBE0(a1, 1, table, D_0081F8F0, 0). |
| 001EFF10(id, a1..t1, f12) | inline asm | Calls 001EF9D0(id, a1 + 0x30; 1.0). On a node, it sets +0x1F0 = a1 and +0x1FC = f12, and copies the quadwords a2, a3, t0 and t1 to +0x200.., in that order. Returns the node. |
| 001EFFD0(id, a1, a2, a3, f12) | inline asm | Calls 001EF9D0(id, a1; 1.0). On a node, it copies +0xB0 ← a1 and +0xC0 ← a2, then sets +0x20 = f12 and the halfword +0x94 = a3. Returns the node. |
| 001F0060(id, owner) | byte-matched C | Calls 001EF9D0(id, 0; 1.0); on a node, +0x24 = owner. |
| 001F00A0(id, a1, a2, a3) | inline asm | Calls 001EF9D0(id, a1; f12 = a2 +0xC). On a node, it copies +0xB0 ← a1 and +0xC0 ← a2, then sets +0xBC = 1.0 and +0x38 = a3. Returns the node. |
| 001F02C0(p, id, f12) | byte-matched C | Copies p to its frame (+0xE0), then calls 001FBD50(frame +0x30, id, 0; f12): a sound at a point. |

### the trail, effect 0x8000000D (`em_area00_fx_trail.c`)

| addr | decomp source | what it does |
|---|---|---|
| 001F1550(node, n) | byte-matched C | Sets +0xC = n. If the halfword D_00275BCC is below +0xC, it sets +4 = 3 and returns 0. Otherwise it fills +0x110.. with +0xC answers of 001AF780, calls 001CB5B0, sets +9 = +0xC and returns 1. |
| 001F15F0(node, slot, row) | NEARMISS | The strip packet. The page is 001CCF70(*(node +0x1F0) + 0x30); 0xFFFFFF returns. 001CB5F0(D_007635C0, page, 0x3E) gives the block, which gets a zero quadword, the DIRECT word 0x5000003D and the GIF tag 0x402640000000800F / 0x4141. It then walks 15 samples from (slot, row): row −= 2, and below 0 it wraps to 8 with slot − 1 (below 0 it wraps to 2), until (slot, row) returns. Each sample scales +0x220 / +0x230 by the sample's age / 10 (00102900 into 0x70003600 / 0x70003610, then 00102990 into the packet) and projects the history points row and row + 1. An age of at most 0 sets the ADC bit 0x8000 in the **next** block's +0x1C / +0x2C words (the cursor has already moved on); otherwise the age drops by 1. It ends with 001CB900(…, page, 2). |
| 001F18C0(node) | byte-matched C | The node callback. States 2 / 3 call 001AFC10. State 0 runs 001F1550(node, 3); on success it zeroes the ages of the three 10-sample histories, seeds every history point pair with 001026A0 (the bone matrix at +0x1F0, the offsets +0x200 / +0x210), clears the row and slot words and falls into state 1. State 1 sets the current sample's age to 10.0, seeds its pair, draws with 001F15F0, and moves the row by 2 (wrapping at 10 into the next slot, and at 3 slots back to 0). |

### the debris effects (`em_area00_fx_debris.c`)

The node callback of effects 0x8000000A..C, 0x21, 0x31, 0x32, 0x36, 0x4C,
0x4D, 0x5E and 0x6E (subtypes 0, 1, 2, 4, 5, 6, 0xA, 0x11, 0x12, 0x16 and
0x17); the crate breaks of a00_09.

| addr | decomp source | what it does |
|---|---|---|
| 001F2BA0(node) | NEARMISS | Mode 0: +0xB4 grows by 7.0, 3.5, 7.0, 3.5 or 2.0 for subtypes 0, 1, 5, 6 and 4. It then rebuilds the matrix at +0xD0 (001029C0, 00102C58 from +0xC0, 00102918 to +0xB0) and seeds the pieces with 001F2E90(node, row). The row is D_0025A350 + subtype·0x60. Mode 1, pass 1 runs over the pieces: a piece with a nonzero halfword +0x80 counts as dead. A live one is moved by 001F3620(piece, subtype). If it is still live, 001F3E30(0x700036A0, piece + 0x40, row +0x50, row +0x54 + 4·(i & 1), row +0x5C) draws it. When every piece is dead, +4 = 3. Pass 2 sweeps up to four pieces whose +0x78 is 0 with 001F3340. Modes 2 / 3 call 001AFC10. |
| 001F2E90(node, row) | byte-matched C | Sets +0xC = row +0x4C and checks it against D_00275BCC (as 001F1550 does). It allocates the bone slots, calls 001CB5B0, sets +9, runs 001C6200 and seeds every piece with 001F2F90(node + 0xD0, piece, row, subtype), then sets +4 = 1. |
| 001F2F90(frame, piece, row, kind) | byte-matched C | Draws two random numbers first: ang = 2π·r/2³¹ − π and spin = ½·π·r/2³¹. Kinds 3, 7, 8 and 9 place the piece at the frame's origin (+0x30), with a 0.04 offset along sin / cos of ang, and a matrix turned by π/2 (001C9E40). Other kinds offset the origin by the row's +0 / +4 / +8 spreads along cos(ang), cos(spin) and sin(ang); the velocity is three random spreads less the biases, rotated by the frame (001026A0, 001028D0); a random tumble goes through 001CA3B0. Then the colour is copied from the row, and the size is row +0x40 + row +0x44·rand. The counters are set: the life +0x74 = 10000 and the halfword +0x82 = kind. |
| 001F3340(piece, row, kind) | NEARMISS | Kinds 0x16, 0x12 and 0x11 set +0x74 = 180; the other listed kinds sweep. Steps: 12 for kinds 0, 1, 4, 5, 6; 6 for 2, 0xA, 0x17; 4 for 3, 7, 8, 9. The sweep runs 0x78 / step times, moving the segment 0x700038A0 → 0x700038C0 by velocity·step (y gains row +0x48·step first) and asking 0019A570 (with the caller's s4 = the piece: see section 5). A hit copies 0x700031B0 to +0x60 and the hit record's +0x24..+0x2C to +0x50..; otherwise the segment advances (001031E0) and +0x74 += step. The end sets +0x7C / +0x74 / +0x78. Unlisted kinds set +0x74 = +0x78 = 1. |
| 001F3620(piece, kind) | NEARMISS | The piece's four states. **0:** a random tumble by kind group (8° or 4°; kind 7 a fixed −0.5 turn). The 4° group splashes below y = 10: effect 0x8000005F at (x, 0, z), the piece dies, and every second splash plays 0xDB at 800. The fade drops 0.05 below 20 frames, and at the end of the count the state becomes 1. **1:** a kill request kills. Kinds 2, 10 and 23 re-home, colour 0.6, spin ·(2 + rand), state 2, then recurse. Kinds 3, 7, 8 and 9 re-home, take a random-yaw velocity with vy = min(1, −0.4·vy) and 45 frames in state 3. With D_00275C44 < 0 they also reseed it (rand % 20 + 30) and, unless the scratchpad byte 0x70003B8D is set, play 0x16A / 0x5E0 / 0x5E0 / 0x5DE at 200. Other kinds die. **2:** rebuilds the matrix from the home quaternion and fades by 0.01. **3:** a 16° tumble, fade and countdown. States 0 and 3 then integrate: quaternion to matrix (001CA1C0), rows scaled by spin·fade, translate by velocity (001028B8), vy += the kind's gravity (D_0025A398 + kind·0x60). |
| 001F3E30(a0, a1, a2, a3, mode) | byte-matched C | h = 001CA7B0(a0 + 0x30; 10.0); a negative h returns. Mode 0 calls 001C7900(a0, a1, 0x3F5, 0), then 001CA940(h, 001C6120(D_0028A490[a2], a3)). Mode 2 calls 001CB760(D_007635C0, 001CCF70(a0 + 0x30), the channel-3 cursor), 001D8C20(1), 001C7900(…, 3) and 001D3990(the entry), then writes a 0x60 end tag and calls 001D8C20(0). |

### the exit-phase effect and 001C50B0's helpers (`em_area00_fx_exit.c`)

| addr | decomp source | what it does |
|---|---|---|
| 001F4F40(sub) | C linked from asm | 001AFA90(0xC); on a node, +0xD = sub and +0x10 = 001F5040. Returns the node or 0. |
| 001F4F90(node, f12) | byte-matched C | Draws eight lines. The start is (node +0xD0)·f12 (00102900), and the point begins at +0x100. Each step advances the point (001028B8) and draws 001CD940(2, point, colour[pair.0], next, colour[pair.1]). The pairs are consecutive words at D_0026EAC0; the colours are quadwords at D_0026EA80. |
| 001F5040(node) | NEARMISS | It first copies D_0025AD70 to its frame. **State 0** picks the clip by subtype through 001C6120 / 001CA5E0(…, 2): sub 2 gives 0xF, 4 gives 0xE, 1 gives 0xB and the rest 0xD. It then reads the bone count (001C6150) and checks it as 001F1550 does, allocates, and runs 001CB5B0 / 001C62C0. The jitter 0.15 + 0.049999997·rand·2⁻³¹ goes to +0x60..+0x68, and 0.15 to the steps. The base is −96 (subs 3, 4) or 0. It ends with 001D80E0(+0x100, the frame copy) and falls into state 1. **State 1** takes frame = +0x28, then +0x28 = frame + 1. Frame 15 sets state 2. Frames 0–2: sub 0 calls the clip (8, **2**) and 001F4F90(node, 2.4), and sub 1 calls 001F4F90 only. Frame 3 calls the clip (7, **2**) for subs other than 1..4. Any other frame eases +0x80..+0x88 toward −128 by 0.35. Every frame integrates +0x60..+0x68 by the steps, decays the steps by 0.8, and calls 001C63D0, 001B17A0 and the node's +0x4C. States 2 / 3 call 001AFC10. |
| 001F5490(p) | byte-matched C | 001C22A0(p) nonzero → 3; else 001C6380(p), 1. |
| 001F5F60(pos, rot, a2, entry) | NEARMISS | Remembers the channel-3 cursor, then builds a matrix from rot and pos on its frame. It writes a qwc-5 tag, zeroes 0x70003400..0x7000346F and calls 001028B8(0x70003470, a2, D_0026EB50). A 0x6C0403F5 GIF block follows, copying the four quadwords at 0x70003440 (each loaded just before its store). Then comes a qwc-9 tag with a 0x6C080000 block of two 001026D0 products (0x70003AC0 and 0x70003400 by the frame matrix), 001D3990(entry), a 0x60 end tag, and 001CAAC0(frame pos, the remembered cursor, the context). |

### the glow, effect 0x8000000F (`em_area00_fx_glow.c`)

| addr | decomp source | what it does |
|---|---|---|
| 001F6FB0(node) | undecompiled | The parent is node +0x24 (001EFE00 stores the parent's +0x14 there). **State 0** copies the parent's +0x60, zeroes the phase (work +0) and +4, and seeds the LCG (work +8) with rand. **State 1** first checks the parent: in state 3, the node takes state 3 too and ends. Otherwise it copies the parent's matrix (+0xD0, whose fourth row is then overwritten by the parent's +0xB0) and sets the LCG word +0xC = +8. By the byte +5: 0 advances +4 by 0.016 until the parent's halfword +0x2E4 has 0x1000; 1 blends the parent's +0x60..+0x68 and +0x80..+0x88 by 1 − +4 and advances +4 to 1.0; 2 zeroes the parent's +0x60..+0x68. The glow scale g = min(phase / 1.5, 1) sets 0x700038A0.. (1.2g, 0.2, 4.8g, 1) and the colour pair at 0x700036A0 / 0x700036B0. While the phase is below 3.5 it draws 24 sparks. Each spark takes f20 = 2·phase − 2·k from the LCG (MULA then MSUB); outside [0, 4] it is −1, otherwise its fraction (001281C0). Its direction comes from two more LCG draws (0011DE90 / 0011E2A8), and the direction scaled by f20 and moved by the matrix goes to 0x700038B0, with its y lane plus 0. The colour is the pair's blend by (f20 − 0.5) / 0.5, clamped to 0..255 (VU max / min), or the first colour. For f20 ≥ 0 the spark is drawn through 001C7900 / 001C6120(…, 6) / 001CA940 when 001CA7B0 accepts it. Then a sprite: 001CFA60 and 001CFBE0(depth, 1, D_0025D800, frame, 1), with D_0025D810.. set to the scale. The phase grows by 0.016; above 4.0 the parent and the node take state 3. States 2 / 3 call 001AFC10. |

## 2. Where the decomp's C differs from the instructions

The translations follow the instructions. The lead may want to carry these
points into the decomp:

- **001F5040 (NEARMISS).** In state 1 the C passes `frame` as 001CA5E0's third
  argument, in the frames 0–2 call (sub 0) and in the frame-3 call. The
  instructions pass the constant 2 in both.
- **001AA840 (NEARMISS).** The C calls `func_001AA7A0(e)`, which a compiler
  puts in a0. The instructions set nothing for the call; the element is
  already in a1 (the loop register), and 001AA7A0 reads a1 (its a0 is never
  read).
- **001F2BA0 (NEARMISS).** The C's structure matches. The five "+= const"
  tails also match; their residual is register pairing only.
- **001CD940 (NEARMISS).** The C writes the clip test and the fog clamps as
  host float compares. The instructions use the VU clip test (DAZ, then
  magnitude against |w|) and the raw-order VU min / max (a positive NaN
  pattern counts as huge, and −0 is below +0). The translation follows the
  VU rules.
- **001F6FB0** has no C (undecompiled). **001D6DD0** is asm words, and
  **001EFF10 / 001EFFD0 / 001F00A0** are inline asm. All five are described
  here from the instructions.

No other difference was found. The NEARMISS C of 001D7510, 001D7A80,
001EF510, 001F15F0, 001F3340, 001F3620 and 001F5F60 agrees with the
instructions. In 001F5F60 the C passes two extra registers, but the
callees never read them.

## 3. Verification (original-instruction oracle)

`python3 tools/test_area00_fx_reference.py`

The method is the render lane's (AREA01_RENDER.md section 3), with the UI
lane's callee interface. Its buffers, dirty-page tracker, entry check and EE
access rules are imported from `tools/test_area01_render_reference.py`.

- **Oracle.** The original routine runs over a captured AREA00 beat
  (`../Extermination/build/s87/route_a00/<beat>/`, 12 beats), and every callee
  outside the lane runs as original code too, nested. Each callee's entry is
  logged: the address, the stack pointer, the 64-bit images of the policy's
  integer registers and the float argument registers.
- **Native.** The module runs over its own copy. Each callee it reaches runs
  the same original code in a second interpreter that shares the native
  memory, with every unpassed register poisoned (GPRs, HI/LO, FPRs, the FPU
  ACC, VF1..VF31, the VU ACC and Q).
- **Entry check.** At every callee entry, all 32 MiB of RAM, the 16 KiB
  scratchpad and the logged entry must equal the oracle's at the same call.
  RAM is compared by pages, exactly as the render lane does. The end check
  compares the results, the call log, RAM and scratchpad.
- **Only executions the EE completes** (aligned accesses inside the TLB
  map). The map was read from the 48-entry TLB of every PCSX2 save state and
  is identical in all 16 AREA01 and 12 AREA00 route states (scratch
  `build/area00/fx/tlbcmp.py`: 1 distinct TLB over 28 states).
- **Callee policy.** The start-up checks that the set of direct call targets
  of the 28 routines is exactly the policy plus the two inline copies. Every
  policy is checked against RegScan (`tools/test_area01_sys_reference.py`,
  imported unchanged). A callee reading a register outside its policy fails
  the test, except for these exceptions, each enforced exactly:
  - 001CFBE0 reads s0 / s1 only when its a1 is 7 or more; the lane passes 1;
  - 00102BB0 / 00102C58 read vf4.yzw through 001029E8, which multiplies them
    by 0 and then overwrites y or subtracts z/w from themselves;
  - 001CD390 reads vf6.w, which 00102718 subtracts from itself.

  0019A570's read of s4 is in the policy: 001F3340 passes the piece, which
  is what its s4 holds.
- **Case states** are built from the captures by original code. The
  spawners (001EF9D0, 001EFD20, 001EFE00, 001EFF10) and, where a case starts
  mid-effect, the node's own first frame (001F2BA0, 001F18C0, 001F6FB0,
  001EA240) run over the captured RAM before the case. That setup is not
  compared. Node callbacks then run for several frames as the case's steps,
  with D_00275B44 / 48 / 40 set as the pool sets them before a callback.

Case groups (full mode; quick mode runs a set of items that together reach
every branch outcome, plus a fixed-seed sample of each group):

| group | cases | what |
|---|---|---|
| 001AA840 | 146 | every beat as captured and with the player's bone array; the list's entries with every byte pattern around the match and positions around 001AA7A0's radius and height window; an empty list |
| packets | 216 | 001D6DD0 / 001D7510 / 001D7A80 in their call-site shapes on every beat, and random channels, words (negative, upper halves set), flag words and misaligned sources |
| 001CD940 | 80 | modes 0–4 and 0xFF, ends near the player or far away, colour patterns, fog quadwords that clamp to 0, to ca0.x and past 256 |
| spawn wrappers | 324 | 001EFF10 / 001EFFD0 / 001F0060 / 001F00A0 with their callers' effect ids (and an id whose record is empty), 001F02C0 with 001F3620's sound ids, 001F4A00 / 001F4E20 as their callers pass them |
| 001EF510 | 54 | an effect-0x80000009 node seeded by 001EA240 on every beat; the handler with work +0x54 zero, as seeded, or random, and random LCG words |
| trail | 52 | effect 0x8000000D (as 00189D30 spawns it: bone 0 + 0x90, 0x700038A0..D0, 10.0) for 1–17 frames on three beats; too few bone slots; states 2, 3, 4, 0xFF; running strips with random ages (0, −1, 0.5, 10, −0, sub-normal); a zero strip matrix |
| 001F15F0 | 40 | the strip packet directly at every slot and row, with random ages, and with the page lookup off screen (0xFFFFFF) |
| debris | 65 | every debris effect id for 1 and 3 frames at the a00_09 crates, three for 12 frames, and seeded nodes with dead / sweeping / varied pieces, all pieces dead, too few bone slots, states 2–4 |
| pieces | 340 | 001F3620 over every state × kind (0..0x18, 0xFF) with counts, kill words, heights around the splash, fades, D_00275C44 sign, the mute byte and the splash parity; 001F3340 over every kind with velocities and heights that hit and miss; 001F2F90 every kind; 001F3E30 modes 0, 1, 2, 3, −1 and a far piece (001CA7B0 refuses); 001F2E90 with too few bone slots |
| glow | 39 | effect 0x8000000F on the three first parents (0x128C10 nodes of a00_09) and every beat; the sub byte 0..3 and 7, phases around 1.5, 3.5 and 4.0, the parent in state 3, the 0x1000 flag toggled, random LCG words |
| exit | 49 | the three live 0x1F5040 nodes of a00_10 as captured (1 and 4 frames) and re-run from state 0 for all 18 frames; random subs, frames (0..4, 14, 15, −1) and states |
| exit helpers | 60 | 001F4F40, 001F4F90, 001F5490 and 001F5F60 on the a00_10 / a00_s0 001C50B0 node |
| edges | 56 | targeted outcomes: mode 4 of 001F2BA0, a pool below 10 free nodes (001F4F40), too few bone slots in 001C22A0 (001F5490), 001F5040 state 0 with subs 3–5 and frame 3 with subs 1–7, 001F3620 state 1 for kinds 2, 3, 7, 8, 9, 10, 23 × D_00275C44 sign × mute, vy below −2.5, 001F1550 directly |
| clip and mask edges | 43 | always run: the call-site shapes of 001D6DD0 / 001D7510 / 001D7A80, each clip flag alone on either end of 001CD940 (+z cannot occur: this cull matrix's z column is its w column less 0.2), 001AA840's +2 byte 0x14 / 0x24 / 0x84 / 4 / 5, 001EFFD0 with a3's upper half set, 001F1550 at 2, 3 and 4 free bone slots, the halfword above 001F5040's frame counter, nonzero 0x70003460.. for 001F5F60, a negative fog limit (001CD940, 001F15F0), a spark with f20 exactly 4.0 and one whose transformed y lane is −0, 00122BB8 answers where the jitter constant matters, 001D6DD0 with a1 negative |

**Faults** (native only, in a forked child). One real case state per entry:
- a latched fault: −1, no call, no store;
- no views: code 4 from a lane routine (the three entries that only call
  run);
- a NULL `call`: code 1 naming the original's first callee, with RAM and
  scratchpad equal to the original's at that callee's entry, so nothing is
  written after the fault;
- a failing callee: code 2 naming it, no further call, and RAM and
  scratchpad equal to the original's at that call's entry. Quick mode fails
  the first, second, middle and last call; full mode fails every call (every
  7th, with the last, past 40 calls).

**Branch coverage.** Every conditional branch of the 28 routines must be seen
both ways: 179 branches, 358 outcomes. Nine outcomes are not reachable, and
the test asserts that none of them is ever seen:
- 001AA840's second count test;
- the "fog word below 0" tests of both ends of 001CD940: the lane is the
  12.4 value of a number just raised to at least +0;
- three outcomes of 001F2BA0 that need a negative piece index;
- 001F3340's empty sweep: the count is 30, 20 or 10;
- 001F3620's fall-through of the sound dispatch: it runs only for kinds 9,
  8, 7 and 3.

One more outcome is listed as not reached rather than impossible:
001F3340's `+0x74 ≤ 0` after a hit. The counter starts at 0 and grows by the
step. It could only be reached if 0019A570 or 001031E0 stored to the
piece's +0x74, and no case shows them storing outside the scratchpad and
their stack.

Results: section 6.

## 4. Existing translations (verified, reused, not duplicated)

`python3 tools/test_area00_fx_existing_reference.py`

The file imports each translation's own original-instruction harness
unmodified, feeds it AREA00 inputs, and builds every library under
`build/area00/fx/existing/`.

| addr | port module (owner) | AREA00 check |
|---|---|---|
| 0015B030 | em_area01_exitb.c (lane EXITB) | the 0015B030 owner node of each AREA00 capture, in the harness's own case shapes (as captured, states 2/3/4/0xFF, states 0/1 × record +4 states × 0015AC00 answers) under its lockstep check |
| 001639E0 | em_player_fall.c | the state-7 entry over each AREA00 player record (+6 in 0..3, +7 in 0..4), with the render lane's fall entry check |
| 001D80E0 | em_effect_original.c | its spawn sweep over AREA00 beats (the kind-1 records reach 001D80E0 through 001EF9D0) |
| 001D7000 | em_static_world.c | its 001D7000 leaf cases on every channel, over AREA00 captures added to its capture list |
| 001EFEB0 | em_security_gun_rest.c | its 001EFEB0 check with AREA00 images as the base |

Results: section 6.

## 5. Limits and what is left

- **Register saves are not stored.** The translations carve the original's
  frames but do not write the callee-saved register images. The stack is
  outside the compared memory. The only stack bytes a callee reads are the
  buffers the lane writes (001F02C0, 001F4F90, 001F5040, 001F5F60,
  001F6FB0). 001F6FB0's spark colour quadword reads two frame words it never
  writes (lanes z / w of `frame +0xD0`); neither lane is used.
- **The clip test and the exponent-255 lanes are unmeasured**, as in the
  render lane. No case reaches code 6.
- **Calls are direct.** The captures are end-of-beat images. The debris,
  trail, glow and handler cases start from nodes that original spawners
  create over those images; they are not states caught mid-frame in the
  route.
- **Caller-register reads.** 0019A570 reads 001F3340's s4 (the piece). In the
  AREA01 SYS lane's list this happens on 0019D330's path when no cell span
  beats the word 0x7000320C. The translation passes s4 as the original holds
  it.
- **The cull matrix of every AREA00 capture has z = w − 0.2**, so +z is never
  flagged. 001CD940's +z behaviour is the shared clip rule, and no case
  tests it.

## 6. Results

Measured 2026-09-28 on the shared machine (load average 115–160 on 10 cores;
`/usr/bin/time -p`, user and sys include the worker processes).

```
$ python3 tools/test_area00_fx_reference.py
mode quick: 6 of 146 001AA840 cases, 6 of 216 packets cases, 7 of 80 001CD940 cases, 14 of 324 spawn
wrappers cases, 3 of 54 001EF510 cases, 6 of 52 trail cases, 1 of 40 001F15F0 cases, 13 of 65 debris
cases, 34 of 340 pieces cases, 11 of 39 glow cases, 10 of 49 exit cases, 5 of 60 exit helpers cases, 17
of 56 edges cases, 43 of 43 clip and mask edges cases, 176 cases in all (0 refused on both sides), 4843
callee calls, 4843 entries with RAM + scratchpad + arguments equal to the original's, 179 conditional
branches: 349 of 358 outcomes seen, 9 not reachable (listed), 153 fault cases
area00 fx reference: PASS                      (real 16.4 s, user 12.9 s, sys 0.8 s)

$ EM_TEST_FULL=1 python3 tools/test_area00_fx_reference.py
mode full: 146 001AA840 cases, 216 packets cases, 80 001CD940 cases, 324 spawn wrappers cases, 54
001EF510 cases, 52 trail cases, 40 001F15F0 cases, 65 debris cases, 340 pieces cases, 39 glow cases, 49
exit cases, 60 exit helpers cases, 56 edges cases, 43 clip and mask edges cases, 1564 cases in all (0
refused on both sides), 39618 callee calls, 39618 entries with RAM + scratchpad + arguments equal to the
original's, 179 conditional branches: 349 of 358 outcomes seen, 9 not reachable (listed), 272 fault cases
area00 fx reference: PASS                      (real 80.5 s, user 81.3 s, sys 3.0 s)

$ python3 tools/test_area00_fx_existing_reference.py
mode quick: 50 0015B030 cases over 2 AREA00 captures (64 callee calls in lockstep, 14 branch outcomes of
0015B030 seen), 40 001639E0 cases on AREA00 player records (98 scripted calls, 98 with actor + scratch +
arguments equal at entry, 18 of 22 branch outcomes of 001639E0 seen), 384 spawn cases over 2 AREA00 beats
(001D80E0 through the kind-1 records), 16 001D7000 cases over 2 AREA00 captures, 4 001EFEB0 cases over 2
AREA00 images
area00 fx existing translations: PASS          (real 3.0 s, user 2.4 s, sys 1.1 s)

$ EM_TEST_FULL=1 python3 tools/test_area00_fx_existing_reference.py
mode full: 250 0015B030 cases over 10 AREA00 captures (320 callee calls in lockstep, 14 branch outcomes
of 0015B030 seen), 240 001639E0 cases on AREA00 player records (519 scripted calls, 519 with actor +
scratch + arguments equal at entry, 18 of 22 branch outcomes of 001639E0 seen), 5570 spawn cases over 10
AREA00 beats (001D80E0 through the kind-1 records), 288 001D7000 cases over 12 AREA00 captures, 24
001EFEB0 cases over 12 AREA00 images
area00 fx existing translations: PASS          (real 13.9 s, user 13.0 s, sys 3.9 s)
```

The existing-translation file does not re-derive those modules' branch
cover. Their own tests hold it; the numbers above are the outcomes the
AREA00 inputs reach.

**Mutation evidence** (one bounded sweep). Driver:
`build/area00/fx/mut/run_mutants.py`. Each mutant copies the lane sources,
makes one change, builds a private library (`EM_A00FX_SRC`) and runs the
reference test in quick mode. It covers 84 mutants: 83 single-operation
changes across all six modules and the shared header (constants, operand
orders, widths, reloads, loop bounds, shifts, masks, the VU fog order, the
quadword address mask), plus one no-op control.

- Round 1 (`mut/run1.log`) killed 67 and 17 survived.
- Each survivor either got a case that kills it (the "clip and mask edges"
  group, plus quick-mode samples in every group) or is shown equivalent
  below.
- The final run on the final test file (`mut/final.log`) killed 77 of 84.
  The survivors were the control, the five equivalents below, and
  `7a80_or`, which the always-run call-site shapes then killed (rerun).

**The result is 78 killed, 5 equivalent, and the control survives.**

| mutant | why it is equivalent |
|---|---|
| `tag_reload`: the channel tag's qwc store uses the cursor loaded for it, and the cursor store reuses that value (no fourth reload) | The two differ only when the qwc halfword store lands on the cursor word. That requires the cursor the store uses to be the slot address itself. If the first loaded cursor already was, the id byte 0x10 lands on the cursor's top byte, and the reloaded cursor is 0x10xxxxxx, outside the modelled TLB map (the EE register page). Otherwise the id byte made it so (first cursor = slot − 3, slot ≡ 0x10 mod 0x100), and the qwc store then leaves the cursor ≡ qwc (3, 4 or 7) mod 16. The body's word or doubleword stores at +0x1C / +0x20 are then misaligned, an address error. So the EE completes no execution in which they differ. |
| `ef510_reload`: 001EF510 reads work +0x54 through the pointer loaded before the seed store | The seed store is a word store at work + 4. It changes D_00275C34 only if work = 0x275C30. The seed read there is 0x275C30, and the stored 0x275C30·37 + 11 = 0x05B052FB makes the reloaded work's +0x54 a misaligned word load. The render lane's `r3_hud_random_cached_work` is the same case. |
| `eff10_order`: 001EFF10's stores to +0x1F0 and +0x1FC swapped | Two word stores to different addresses of one base, with no load or call between them. |
| `2f90_kindh`: 001F2F90's halfword store of the kind at +0x82 widened to a word store of kind << 16 at +0x80 | The wider store also writes +0x80..+0x81 = 0. The routine stored 0 there earlier, with only word stores to +0x7C / +0x74 in between. The kind's upper bits are dropped either way. |
| `6fb0_msub`: 001F6FB0's MSUB replaced by SUB(ACC, MUL) | They differ only when the product 2·k saturates (MSUB does not saturate its product). k = q / 65535 + 1e-4 with q in 0..65535, so 2·k ≤ 2.0002 is finite. |

## Binding

Nothing is bound. To wire a module, give `EmArea00Fx.call` a dispatcher from
original addresses to port translations, give it views of the live memory,
and give it the EE stack pointer. Candidates:

| callee | existing translation |
|---|---|
| 001CB5F0 / 001CB6B0 / 001CB760 | em_packet_chain_original.c |
| 001CFB50 | em_effect_kinds.c |
| 001CFBE0 | em_head_sprite_original.c |
| 001EF9D0 / 001EFD20 / 001EFD90 / 001CCF70 / 001CD390 | em_effect_original.c |
| 001CAAC0, 001C7900 | em_anim_runtime_rest.c |
| 001281C0 | em_stream_lanes_original.c |
| 001C6120 | em_owner_draw_original.c |
| 001D80E0 | em_effect_original.c |
| 001F4BF0 | em_status_scene_original.c |
| 001AFA90 / 001AFC10 | em_actor_pool (the pool) |

Files that define a function or adapter named by the address of another
callee. These are not checked to be verified translations; check each one
before binding:
- 0019A570: em_coll_segment_walkers.c;
- 001CA1C0: em_pose_host_workers.c;
- 001CA7B0 / 001CA940: em_owner_draw_original.c;
- 001C6150 / 001C62C0 / 001C6380 / 001B17A0: em_owner_services_original.c;
- 001CFA60: em_head_sprite_original.c;
- 001C22A0: em_render_verify_rest.c;
- 001FBD50: em_player_misc_workers.c;
- 001F4D40: em_effect_manager.c;
- 001CA5E0: em_indicator_bind_live.c;
- 001D8C20: em_owner_draw_live.c;
- 001CA3B0 / 001CA4D0 / 001C6200 / 001C63D0: em_area00_world.c (a sibling
  AREA00 lane's new file).

No port file defines 001AA7A0, 001EEEB0 or 001D3990. The first-level port's
copies of the VU0 leaves (0010xxxx) are also candidates.

Wiring order: the debris effect (001F2BA0) and the glow (001F6FB0) are the
a00_09 content; the exit effect (001F5040) is a00_10's. Each needs its
spawner (001EFD20, 001EFE00, 001F4F40) and the pool walk to call it.

## Known gaps

- The mutation sweep (section 6) is evidence for those 84 mutants only. It
  is not a proof that the test rejects every wrong translation, and no
  review round has run.
- The quick run takes about 14 s of CPU on the loaded shared machine, above
  the ~10 s target. The debris frames dominate (each first frame seeds 6 to 16
  pieces through 001F2F90, by the row's count).
