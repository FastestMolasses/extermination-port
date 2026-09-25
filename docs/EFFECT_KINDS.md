# Effect kinds, glow markers, effect colour, pool resets and room point-light lists

Status: 2026-09-23, lane `effect-kinds` (census lane **L27-effect-kinds**, plus the truck handler 001EBF10 of
**L23-truck**). The translation and its oracle are done. The module is **not wired**; the coordinator binds it
(section 4).

Files (all new, owned by this lane):
- `src/game/em_effect_kinds.{h,c}`: the native translation.
- `tools/test_effect_kinds_reference.py`: the original-instruction oracle, including the capture checks.
- `tests/effect_kinds_test.c`: the native contract test (ASan/UBSan, no disc data).

This document names addresses and describes behaviour. It contains no original code, data or disassembly.

## 1. Scope and census status

"Before" is the census row of `docs/FIRST_LEVEL_CENSUS.md` (2026-09-23). "After" is the status this lane
delivers. No row is live yet: every row is **verified-unbound** until the coordinator binds it.

| Function | Decomp | Translated from | Census before | After | First beat |
|---|---|---|---|---|---|
| 001EC1F0 subtype 0x0A handler | NM | the .s | missing | verified-unbound | 05 |
| 001EC3F0 subtype 0x05 handler (0x80000028) | NM | the .s | missing | verified-unbound | 00 |
| 001EC470 subtype 0x24 handler (0x80000065) | NM | the .s | missing | verified-unbound | 06 |
| 001EBF10 subtype 0x20 handler (0x80000049, L23) | NM | the .s (the NEARMISS C has two wrong constants) | missing | verified-unbound | 08 |
| 001CFB50 the handlers' transform block (added by the Effects step, 2026-09-24) | BM | C | boundary | verified-unbound | 00 |
| 001D0540 its depth scale (added by the Effects step) | NM | the .s | boundary | verified-unbound | 00 |
| 001F54E0 effect colour | AW | the .s | unverified (em_effect_color.h) | **live** since the render + UI step (2026-09-25): the indicator children's one colour (section 4.5); the old header copy is deleted | S2 |
| 001F5640 glow-marker list selector | BM | C | missing | verified-unbound | S2 |
| 001F5940 one glow marker | BM | C, float order from the .s | missing | verified-unbound | S2 |
| 001F5C20 glow-marker walker | BM | C | missing | verified-unbound | S2 |
| 001F5CA0 FX-record list selector | BM | C | missing | verified-unbound | S2 |
| 001F0310 effect-pool reset barrel | BM | C | missing | verified-unbound | S0 |
| 001F03D0 ring-decal lane reset | BM | C | missing | verified-unbound | S0 |
| 001F3FA0 particle pool reset | NM | the .s | missing | verified-unbound | S0 |
| 001F6760 primary point-light list selector | BM | C | missing (critic: stand-in) | verified-unbound | S1 |
| 001F6D60 auxiliary point-light list selector | BM | C | missing (critic: stand-in) | verified-unbound | S1 |
| 001F6640 point-light list registration | BM | C | missing (critic: stand-in) | verified-unbound | S1 |
| 001F66F0 point-light list release | BM | C | missing (critic: stand-in) | verified-unbound | S1 |
| 001F6850 primary list release | BM | C | missing | verified-unbound | S1 |
| 001F68B0 primary list latch dispatch | BM | C | missing | verified-unbound | S1 |
| 001F6E40 auxiliary list release + registration | BM | C | missing (critic: stand-in) | verified-unbound | S1 |

The SDK leaf 001029C0 (identity) is translated inline (it is reached by 001F03D0 and 001EBF10).

Critic section 7.2 says the five point-light rows are a stand-in (the offline export `tools/export_point_lights.py`
plus `em_point_light_load`), not missing effect code. That is right about the live port. This lane supplies
the faithful translation that replaces the stand-in (section 4.4).

"L-shadow draw handlers": no shadow function is a row of L27 or L23. The shadow rows belong to L29/L29b.

## 2. What the original does

The numbers below are read from the original code. Floats are named by value and their binary32 word is given
where the value is not exact in decimal.

### 2.1 The per-subtype handlers (called by 001EA240 through `D_00255434[subtype*2]`)

The driver 001EA240 calls `handler(node + 0xD0, depth, D_00275C34)`, where D_00275C34 = node + 0x1F0 is the work
block (`EmEffectOriginalWork`: +4 seed copy, +0x54 accumulator, +0x5C fraction). The handler mapping comes from
the ELF, and the test reads it from the captured RAM of every beat: 5 → 001EC3F0, 0xA → 001EC1F0,
0x20 → 001EBF10, 0x24 → 001EC470.

Every handler ends in the same two calls:
- 001CFB50(D_0081F8F0, 0, src, f12, f13, f14, f15, f16), which fills the transform block;
- 001CFBE0(depth, 1, source, D_0081F8F0, copy), which emits the packet chain.

| Handler | src | f12 | f13 | f14 | f15 | f16 | source(s) | copy |
|---|---|---|---|---|---|---|---|---|
| 001EC1F0 | a0 | work +0x54 | work +0x5C | 1.0 | 1e-6 (0x358637BD) | 5.0 | D_00256700 | 0 |
| 001EC3F0 | a0 | work +0x54 | work +0x5C | 1.0 | 1e-6 | 5.0 | D_002568B0 | 1 |
| 001EC470 (2 draws) | a0 | work +0x54 | random scalar | 1.0 | 1e-6 | 15.0 | D_00256940, then D_002569D0 | 1 |
| 001EBF10 (3 draws) | 0x700036A0 | work +0x54 | random scalar | 1.0 | 1e-6 | 0.0 | D_002565E0, D_00256670, D_00256670 | 0 |

The **random scalar** is computed before each draw of 001EC470 and 001EBF10:
1. v = work +4.
2. f13 = cvt((v >> 16) & 0xFFFF) / 65535.0, then + 1e-4 (0x38D1B717). The divide is DIV.S and the add is ADD.S.
3. work +4 = v·0x25 + 0xB, with 32-bit wrap.

The accumulator is read after that store.

**001EBF10** first writes the identity (001029C0) to the scratchpad matrix 0x700036A0..0x700036DF. Before each
of its three draws it sets row 3 of that matrix to (x, a0 +0x34, z, 1.0). a0 +0x34 is row 3's y of the node
matrix, so it is the truck puff's height. The (x, z) pairs are (370, 380), (345, 390) and (395, 390).

The decomp's NEARMISS C for 001EBF10 says the scalar offset is 1e-6 and f15 is 9.99e-7. The listing has 1e-4
(0x38D1B717) and 1e-6 (0x358637BD). The translation follows the listing.

### 2.1a 001CFB50 and 001D0540: the transform block (Effects step, 2026-09-24)

**001CFB50(dst, a1, src, f12, f13, f14, f15, f16)** fills the 0x58-byte block
(D_0081F8F0 for every handler), in this order:
1. +0x44 = f12, +0x4C = f13, +0x48 = f14, +0x50 = f15;
2. +0x54 = 001D0540(src + 0x30, &0x70003AC0, f16): the source matrix's row 3
   (the node's position) and the camera matrix;
3. +0x40 = 001CD370(a1) = D_00275670 + (a1 << 6) + 0x2240 (the handlers pass
   a1 = 0: the clip matrix at context +0x2240);
4. +0x00..+0x3F = the 64 source bytes.

**001D0540(pos, m, d)** (NEARMISS; followed from the .s):
1. Scratchpad 0x70003660 = (pos.x, pos.y, pos.z, 1.0) and 0x70003670 =
   (pos.x, pos.y, pos.z − d, 1.0), the z by SUB.S.
2. The four rows of m are loaded once. Each point is transformed: ACC =
   row0·x (VMULAx), ACC += row1·y, ACC += row2·z (VMADDAy/z), result = ACC +
   row3·vf0.w (VMADDw, vf0.w = 1.0). Each result is stored back over its
   point.
3. For each point, z = z · (1.0 / w) (DIV.S, then MUL.S), stored.
4. The tail call 0011DF78 (fabsf) gets the first point's stored z minus the
   second's (SUB.S). Its result is +0x54: the projected depth span of an
   offset of d along z at the effect's position.

The translation is `em_effect_kinds_001CFB50` / `em_effect_kinds_001D0540`.
They take an `EmEffectKindsXfState`: D_00275670 and the 16 words at
0x70003AC0 (read), and the eight scratch words at 0x70003660 (written). The
tail call goes through the module's `w_0011DF78` worker, which is checked
before the first store.

### 2.2 001F54E0(obj, color): the effect colour

1. r = cvt(rand) (00122BB8).
2. t = −127 + 254·(2⁻³¹·r).
3. b = c3·t + 127.
4. For each channel i in 0..2: vᵢ = cᵢ·b − 127, clamped.
   - If vᵢ < −127, vᵢ becomes −127 (C.LT).
   - Otherwise, if vᵢ ≤ 127 is false, vᵢ becomes 127 (C.LE).
5. The stores:
   - obj +0x80/+0x84/+0x88 = v0, v1, v2;
   - obj +0x8C = 1.0.
6. It then makes the indirect call `obj->(+0x4C)(obj)`.

All four colour lanes are loaded before the first store, so `color` may alias obj +0x80. The pickup indicator
001C5680 does exactly that: it copies +0xA0..+0xAC into +0x80, then passes obj +0x80.

### 2.3 Room selectors (key = (D_00810700 << 8) + D_00810701)

| Selector | Keys that return a list | AREA11 (0x0B00) |
|---|---|---|
| 001F5640 glow markers | 0x0000, 0x0001, 0x0600, 0x0601 (the same list), 0x0700, 0x0703, 0x0802, 0x0B00, 0x0D00, 0x0E00, 0x0F00, 0x0F01, 0x1100, 0x1300, 0x1400, 0x1500 | D_0025B590 (11 records) |
| 001F5CA0 FX records | 0x0301, 0x0302, 0x0400, 0x0401, 0x0700, 0x0800, 0x0803, 0x0D00, 0x0F00, 0x1500 | 0 (001F6210 draws nothing) |
| 001F6760 primary lights | 0x0000, 0x0001, 0x0002, 0x0100, 0x0200, 0x0E00, 0x1100, 0x1301 | 0 |
| 001F6D60 auxiliary lights | 0x0100, 0x0700, 0x0702, 0x0B00, 0x1000, 0x1200, 0x1300 | D_0025D5A0 (1 record) |

Every other key returns 0. The lists are 0x28-byte records, and a negative first halfword ends a list. All of
them lie in vram 0x25AD80..0x25D800. The module loads that window from the user's ELF, and it faults on any
record outside it.

### 2.4 001F5940(kind, pos, t) and 001F5C20: the glow markers

- **001F5940.** A 10-way jump table picks (col[0..3], size, mode). Kinds 3 and 6 and every value ≥ 10 take the
  default.

  | kind | col | size | mode |
  |---|---|---|---|
  | 0 | (0, 0x80, 0, 0x7F) | 12 | 1 |
  | 1 | (0x80, 0x40, 0x40, 0x7F) | 8 | 0 |
  | 2 | (0x80, 0x80, 0x66, 0x7F) | 8 | 0 |
  | 4 | (0x80, 0x40, 0x40, 0x7F) | 3 | 0 |
  | 5 | (0x80, 0, 0, 0x7F) | 30 | 0 |
  | 7 | (0x20, 0x80, 0x20, 0x7F) | 3 | 2 |
  | 8 | (0x80, 0x80, 0x66, 0x7F) | 15 | 2 |
  | 9 | (0x80, 0x10, 0x10, 0x7F) | 3 | 2 |
  | default | (0x40, 0x40, 0x80, 0x7F) | 3 | 0 |

  half = 0.5·size (MUL.S). Then, by mode:
  - **Mode 0:** 001F4D40(pos, col, size, half).
  - **Mode 1:**
    1. col[3] = (clock 0x70003B68 + ((t·0x12D687) >> 16)) & 0x7F. The product is a 32-bit MULT and the shift
       is arithmetic.
    2. col[3] = float_to_int(2.0 · 0011DF78(cvt(col[3]) − 64.0)).
    3. Then the mode-0 emit.
  - **Mode 2:** 0021B9A0(2, 0, 100000.0), then 0021B9A0(3, 0, 1000000.0), then the emit, then 0021B9A0(1, 0, 0).
- **001F5C20.** For record i of the 001F5640 list: pos = (+0xC, +0x10, +0x14, 1.0), then 001F5940(+4 as a signed
  halfword, pos, i).
- **AREA11.** The list holds 3 kind-5 markers and 8 kind-4 markers, so only mode 0 runs: 11 calls of 001F4D40
  every frame. They run at the barrel 001F0360, which the census places in every beat.

### 2.5 Pool resets (S0/S1; called by 001D0660 and 001D1C10)

- **001F0310.** It calls 001F3FA0, then 001F03D0 for lanes 0, 1, 3, 4, 5 and 6. **Lane 2 is not reset.**
- **001F03D0(n).** It covers the 0x20 ring slots of lane n (D_0028F700 + 0x4DBEC0 + n·0xC00 + i·0x60, the
  `EmEffectOriginalDecals` of em_effect_original):
  - +0x00..+0x3F is set to the identity;
  - +0x50 (8 bytes), +0x58 and +0x5C are set to 0;
  - the parameter vector at +0x40 is left alone.

  It then sets D_0081F950[n] = 0.
- **001F3FA0.** Each of the 0x80 records at D_007709C0 (stride 0x90) is cleared, then its halfword +0x80 (the
  "free" mark that 001F40C0 tests) is set to 1. Then D_00275C40 = 0 and D_00275C44 = 0.

### 2.6 Room point-light lists (S1; called by 001D7BB0 after its slot reset)

- **001F6640(p).** For each record with handle +0x24 == −1:
  +0x24 = 001D7FA0((+0xC, +0x10, +0x14, 1.0), &D_0026EB70[+4·16], 1, 1.0, 0.0).
  A −1 result (a full light pool) is stored, so a later call retries.
- **001F66F0(p).** For each record with a handle other than −1: 001D80B0(handle), then handle = −1.
- **001F6850.** 001F66F0(001F6760()). For key 0x1301 it also runs 001F66F0(D_0025D2C0).
- **001F68B0.**
  1. obj = 001F6760(), then 001F6850().
  2. Per key, obj is registered (001F6640) under these latch-byte conditions:
     - 0: D_0081075D ≠ 0xFF;
     - 1: 75E == 0xFF;
     - 2: 784 == 0xFF;
     - 0x100: 75E ≠ 0xFF;
     - 0x200: 761 == 0xFF;
     - 0xE00: 784 == 0xFF;
     - 0x1100: 785 == 0xFF.
  3. Key 0x1301 registers D_0025D2C0 when 79E == 0xFF, and D_0025D270 when both 778 and 77B are ≠ 0xFF.
  4. The default arm releases obj if it is nonzero. That arm cannot run: every key with a primary list is a
     case key. The test checks this on all 65536 keys.
- **001F6E40.** p = 001F6D60(); if p ≠ 0: 001F66F0(p), then 001F6640(p).
- **AREA11 (0x0B00).** 001F68B0 does nothing. 001F6E40 releases the handle stored in the D_0025D5A0 record and
  registers preset 2 (D_0026EB90) at (451.6, 279, 277.2). In the captured opening that stored handle is 0.

## 3. Verification

### 3.1 Oracle (`python3 tools/test_effect_kinds_reference.py`)

The oracle is the bounded EE/VU0 interpreter of `tools/test_effect_original_reference.py`, imported and not
edited. This file extends it with the R5900 three-operand MULT and with an executed-address record.
- COP1 and VU0 arithmetic come from `tools/ee_float_model.py`.
- Memory is one of: the ELF image, a captured route snapshot, or the captured opening.
- Workers are recorded with scripted results. They are 001D7FA0, 001D80B0, 001F4D40, 0011DF78, 001281C0,
  0021B9A0, 00122BB8, the indirect call, 001CFB50 and 001CFBE0. The native side gets the same script.

After every call, three things are checked:
- every byte the original wrote (except stack) must be a modelled byte;
- every modelled byte must be equal on both sides;
- the worker call sequences must be equal, including argument bits and the bytes behind pointer arguments (the
  001CFB50 source matrix, the 001F4D40 position and colour, the 001D7FA0 position and preset).

**Coverage.** The test asserts that every instruction of all 20 functions executed. The exceptions are two
words that are statically unreachable (after an unconditional branch, with no branch target in the function),
and the two words of 001F68B0's unreachable default release (proved over all keys, as above).

**Capture checks.**
- **All 15 route beats:**
  - 001F5C20 over the captured RAM and clock gives 11 marker emits per beat (165 in total);
  - 001F54E0 runs over every captured pickup indicator (001C5680 / 001C5760 nodes) with its captured colour (109
    cases).
- **Live puffs.** The 13 live effect nodes go through their handlers from their captured work blocks and
  matrices:
  - beat 08: the eight 0x20 truck puffs through 001EBF10;
  - beats 05 and 12: five subtype-5 puffs through 001EC3F0.
- **The opening capture.** 001F68B0, then 001F6E40, over its lists: one release (001D80B0) and one registration
  of preset 2.

**001CFB50 / 001D0540 (Effects step).** The original 001CFB50 runs with
001D0540, 001CD370 and 0011DF78 executed (not hooked); the native 0011DF78
worker is the leaf's effect, the sign bit cleared. The run uses each in-scope
beat's RAM (quick mode: beats 05, 08 and 12). Its source matrices are the
beat's live effect, head-sprite and flame node matrices, plus random and
special matrices. It sweeps a1 (0..3, −1, INT_MAX, random), f12..f15 (random
bits) and f16 (0, 5, 15, specials, random). One case in eight replaces the
camera with special or random words (w = 0, ±Inf, NaN, denormals). After
every call the 0x58-byte block, the scratchpad 0x70003660..0x7000367F and the
written-byte set must be equal. Both functions execute in full.

**Capture evidence.** In beats 05, 08 and 12 the captured D_0081F8F0 is the
frame's last handler draw. The native 001CFB50 reproduces it byte for byte
from the node's captured matrix and work block and the captured
0x70003AC0, including the 001D0540 depth scale +0x54. That is 4642.01 for
beat 05's snow puff, and the truck's third draw at (395, y, 390) in beat 08,
whose f13 comes from the LCG word stepped back once. +0x44 is the
accumulator before the draw's step. The test checks that it is an EE-add
pre-image of the captured accumulator.

**Mutation check (scratch, not committed).** Eleven one-token mutants of the
two functions were each run against the default test. Nine were killed:
- d added instead of subtracted, and the SUB.S operands swapped;
- the w divisor changed;
- DIV.S in place of the MUL.S;
- the f13/f14 stores swapped;
- the 001CD370 shift;
- the matrix row passed as the position;
- a short source copy;
- the wrong broadcast lane.

The other two are equivalent: the final SUB.S operands swapped (fabsf
removes the sign), and VMADDw reading the point's w instead of vf0.w (both
are 1.0; the mutant does not compile with the unused constant under
-Werror).

**Results (2026-09-23).**

| Mode | Wall time | Cases |
|---|---|---|
| Default | ~3 s | 512 of 65,536 selector keys; 68 of 2,550 001F5940 cases; 160 of 2,400 handler cases; 250 of 4,000 001F54E0 cases; 184 light-list cases (231 worker calls); 18 walker cases; 10 reset cases; the capture checks; 8 fault cases. PASS. |
| `EM_TEST_FULL=1` | ~63 s | all 65,536 keys × 4 selectors; 2,550 / 2,400 / 4,000 cases; 1,554 light-list cases. PASS. |

**Mutation check.** One-token mutants of the translation were each run against the default test. All 16
behaviour-changing mutants were caught:
- the scalar offset (1e-4 to 1e-6);
- 2⁻³¹;
- the brightness add;
- the lower clamp;
- a marker colour;
- the half size;
- the mode-2 restore argument;
- the 0x12D687 multiplier;
- the lane list of 001F0310;
- the particle clear length;
- the AREA11 auxiliary selector target;
- the 001D7FA0 type;
- an 001EBF10 z;
- 001EBF10's y source;
- the D_00810785 latch sense;
- DIV.S replaced by a multiply.

Four mutants survived, and all four are equivalent rewrites:
- a commuted add followed by +0;
- a DAZ-denormal subtract;
- C.LE replaced by C.LT at 127;
- an upper clamp moved to 0x42FDFFFF, a value that c·b − 127 cannot produce (products near 254 are spaced 2⁻¹⁶).

### 3.2 Contract test

It runs the module on synthetic tables and checks bounds, latching, fail-stop and the worker plumbing:

```
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffp-contract=off -Isrc \
   tests/effect_kinds_test.c src/game/em_effect_kinds.c -lm -o build/effect_kinds_test && ./build/effect_kinds_test
```

### 3.3 The former `em_effect_color.h` copy of 001F54E0

The header's own 001F54E0 arithmetic (`em_effect_delta`) was measured here
against the original: one-ulp differences in 818 of 4,109 swept cases and 2
of 109 captured pickup colours (it summed exactly in double and truncated,
where the EE ADD/SUB pre-trims the smaller operand; docs/EE_FLOAT_MODEL.md).
It was deleted when the indicator children were bound to this translation
(section 4.5); em_effect_color.h keeps only em_effect_float32 and the
001D8C30 mode-1 conversion.

## 4. Binding (coordinator)

em_effect_kinds.c is in COMMON since the render + UI step (2026-09-25), for 001F54E0 only (4.5); nothing else here is bound. Section 5 gives the sources hunk. Each subsection names the call site, the stand-in
the translation replaces, and the workers it needs.

### 4.1 Handlers → `em_effect_original`'s `w_handler`

Bind `EmEffectOriginalWorkers.w_handler(ctx, handler, node, depth, work)` to
`em_effect_kinds_handler(k, handler, node->matrix, depth, work)`.
- An untranslated handler (001EAD70, 001EAF00, 001EAF80, 001EB600, 001EBBB0, 001EBD20, 001EC5F0, 001EC820,
  001EB980, ...) faults with code 6. None is live on the route snapshots, but 001EBD20 (crate) and 001EC5F0 /
  001EC820 / 001EB980 (footstep variants) belong to other lanes.
- **Workers:**
  - **w_001CFB50.** Bind it to `em_effect_kinds_001CFB50` (section 2.1a), with an `EmEffectKindsXfState` over
    the live D_00275670 and scratchpad 0x70003AC0, and a block that stands for D_0081F8F0. It has the same
    shape as the block `em_head_sprite_original_001CFA60` fills (`EmHeadSpriteOriginalXf`), but it is a
    different function. **Blocked (Effects step, 2026-09-24):** no live code produces 0x70003AC0 or the
    render-context block (EFFECT_MANAGER.md 5.0).
  - **w_001CFBE0.** Bind it to `em_head_sprite_original_001CFBE0` (verified-unbound). Pass the adapter's
    D_0081F8F0 block as `xf`, the source by address (D_00256700 / D_002568B0 / D_00256940 / D_002569D0 /
    D_002565E0 / D_00256670, 0x90 bytes each from the ELF), `kind` = 1 and `copy`, with the frame's packet
    cursor.
- **Globals.** `globals->spad36A0` is the scratchpad matrix 001EBF10 writes. Nothing else on the route reads it
  between frames.
- **What it replaces.** `em_effect_original` currently has no live handler binding, and the census rows were
  missing. The truck's live `em_truck_original` effect hook (`EM_TRUCK_EFFECT_ID`) spawns only. It draws once
  the driver and these handlers are bound (L26 then L23).

### 4.2 Glow markers → the effect barrel 001F0360 (L26)

The barrel calls, in order: 001F6210, 001F5C20, 001F6BB0, 001F6EB0, 001F40C0, then 001F0720 ×6. Its live stand-in
is the reported no-effect binding `UM_001F0360` in `em_scene_bindings.c`. When L26 translates the barrel, its
second call is `em_effect_kinds_001F5C20(k)`, with `globals->d810700/701` and `spad3B68` from the scene state.
- **w_001F4D40.** L26 (asm-inline, missing). It is the only worker AREA11 reaches: kinds 4 and 5 are mode 0.
- **w_0011DF78.** Bind to `sdk_0011DF78` (`em_sdk_math_original.c`, live).
- **w_001281C0.** Bind to `em_effect_original_float_to_int`.
- **w_0021B9A0.** Bind to the verified 0021B9A0 translation (census: em_game / em_effect_original).

Modes 1 and 2 are not reached in AREA11.

001F6210 (L26) calls `em_effect_kinds_001F5CA0`, which returns 0 for AREA11.

### 4.3 Pool resets → the area-load render init (001D0660 / 001D1C10, boundary)

Call `em_effect_kinds_001F0310(k)` where the original calls 001F0310. That is S0 and S1: 001D1C10 on the title
and 001D0660 on the new-game load.
- `k->decals` must be the **same** `EmEffectOriginalDecals` that em_effect_original's 001F0460 fills and that
  L26's 001F0720 ages.
- `k->particles` must be the particle pool that L26's 001F4D40 / 001F40C0 use (D_007709C0).
- `globals->d275C40/44` are the counters that 001F40C0 decrements.

No live stand-in exists. The port has no ring or particle state yet.

### 4.4 Room point lights → `em_point_light` at the 001D7BB0 phase

**What it replaces.** Today `em_point_light_reset` (the 001D7BB0 slot writes) is followed by adopting the
offline export (`tools/export_point_lights.py` → `point_lights.emlp` → `em_point_light_load`). That offline
registration is the stand-in.

**The faithful chain.**
1. `em_point_light_reset(pool)`.
2. `em_effect_kinds_001F68B0(k)`.
3. `em_effect_kinds_001F6E40(k)`.

**Workers.**
- **w_001D7FA0.** Bind to `em_point_light_register(pool, pos, color, type, 1.0, 0.0)` and return its value in
  `*handle`, −1 included. 001F6640 stores that value. This differs from em_effect_original's 001D7FA0 worker,
  which discards it.
- **w_001D80B0.** This is the release. 001D80B0 is census L32 and missing: it looks up the active slot whose
  handle (+0xC) equals the argument (001D8060) and, if found, sets +0x2C = 0 and +0xC = −1. `em_point_light`
  has no release yet. Until it gains a verified translation, the worker must fault.

  In AREA11 after the reset it is called once, with the list's stored handle. That is 0 in the captured
  opening, and 0 matches no active slot, because the reset set every active handle to −1.

**State.** `k->tables->lists` must persist across area loads, because the handles live in the list records.
Load it once from the ELF (`em_effect_kinds_load_tables`). The latch bytes D_0081075D..D_0081079E come from the
scene state.

### 4.5 Effect colour → pickup and prop indicators (live)

Every 001C5680 / 001C5760 node calls `em_effect_kinds_001F54E0` from its own
behaviour (em_area11_bindings.c `tick_indicator`, em_indicator_child.c;
docs/CENSUS_UNVERIFIED.md "001C5680 and 001C5760"):
- `out` = `color` = the child's +0x80 (001C5680 copies +0xA0 there first);
- `w_00122BB8` = `em_random_next`, the game RNG;
- `w_indirect` = the +0x4C method 001CACB0: it queues the child mesh's draw
  (`em_pickup_light_submit` / `em_props_indicator_submit`), which converts
  +0x80 with 001D8C30 mode 1 (`em_effect_color_gs`).

The instance is a stack `EmEffectKinds` with only these two workers: 001F54E0
reads no tables or globals.

## 5. Makefile hunks (for the lead; this lane does not edit the Makefile)

Test targets:

```make
.PHONY: test-effect-kinds
test-effect-kinds:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffp-contract=off -Isrc tests/effect_kinds_test.c src/game/em_effect_kinds.c -lm -o build/effect_kinds_test && ./build/effect_kinds_test

.PHONY: test-effect-kinds-reference
test-effect-kinds-reference:
	python3 tools/test_effect_kinds_reference.py
```

Sources, when bound: add `src/game/em_effect_kinds.c` to `COMMON`. It needs `em_effect_original.h` (types only),
so em_effect_original.c must be linked when the handlers are bound through it.

## 6. Limits

- **Workers translated elsewhere.**
  - 001F4D40 is L26 (em_effect_manager).
  - 001D80B0 is L32 (em_frame_render_heads).
  - 001CFBE0 is L39 (em_head_sprite_original).

  None of them is bound live. The handlers and the markers also read the render-context views
  (EFFECT_MANAGER.md 5.0), so they stay unwired until those exist.
- **The 001CFB50 capture check** covers the last draw of each captured frame only (one per beat with a live
  puff); the sweeps cover the rest.
- **Scripted worker results.** The oracle scripts the results of 0011DF78 (except under 001CFB50, where the
  original leaf runs), float_to_int and 001D7FA0. Their own fidelity is their modules' claim.
- **Beat coverage.** The route beats reach only AREA11's key (0x0B00):
  - the glow markers use kinds 4 and 5 (mode 0);
  - 001F5CA0 and 001F6760 return 0;
  - the handler captures cover 001EBF10 and 001EC3F0 only (001EC470 and 001EC1F0 had no live node in any
    snapshot).

  The other keys, kinds and handlers are covered by the ELF-image sweeps, not by captures.
- **What the module does not own.** The module takes the work block and the node matrix as given. The driver
  001EA240 (em_effect_original) owns the node lifecycle and the depth key.
- **Float model.** Arithmetic follows `docs/EE_FLOAT_MODEL.md`. Non-finite inputs are compared between the
  translation and the model, not against PCSX2.
