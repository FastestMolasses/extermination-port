# Packet-chain builders 001CB5F0 / 001CB6B0 / 001CB760 / 001CB900 and the fog programmer 0021B9A0

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "packet-chain", 2026-09-24 (fix round the same day: per-call compares,
every worker adapter exercised, aliasing and slot-bit cases; section 4). The
translation and its oracle are done.

**Status (Effects step, 2026-09-24).** The module is in COMMON (with
em_status_ui_leftovers.c for 0021B900). **0021B920 is live**: the Metal world
fog's em_fog_gs_coefficients is a call of em_packet_chain_0021B920, so the
port has one translation of 0021B920 (section 5, "Required", done). The
builders and 0021B9A0 are not called live. Every consumer also reads the
render-context views, which no live code produces yet (EFFECT_MANAGER.md
5.0). Section 5 lists what each consumer binds.

Files:
- `src/game/em_packet_chain_original.{h,c}`: the native translation.
- `tools/test_packet_chain_reference.py`: the original-instruction oracle and
  the capture checks (`make test-packet-chain-reference`, section 7 has the
  Makefile hunk).

## 1. Census rows of this lane

| Function | Decomp | Census before (recount 2026-09-24) | After this lane | Module / evidence |
|---|---|---|---|---|
| 001CB5F0 open a packet | BM | missing | verified-unbound | em_packet_chain_001CB5F0; oracle + capture replay |
| 001CB6B0 reference block | BM | missing | verified-unbound | em_packet_chain_001CB6B0; oracle + capture replay |
| 001CB760 call block | BM | missing | verified-unbound | em_packet_chain_001CB760; oracle + capture replay |
| 001CB900 blend-state block | BM | missing | verified-unbound | em_packet_chain_001CB900; oracle + capture replay |
| 001CB9B0 blend-state address | (C leaves the default unset) | boundary ("GS/VIF packet build (sprites, flush)") | verified-unbound | em_packet_chain_001CB9B0; oracle over every mode; 177 captured blend blocks replay through it |
| 0021B9A0 fog programmer | NM | missing | verified-unbound | em_packet_chain_0021B9A0; oracle, from the .s and its jump table |
| 0021B920 fog coefficients | BM | verified-unbound (em_fog_gs) | **live** since the Effects step: em_fog_gs_coefficients calls it (the old host formula was not bit-exact, 6.4) | em_packet_chain_0021B920; oracle + the fog block of every beat; test_area11_fog_reference checks the Metal helper against the EE model and every in-scope beat |
| 0021B900 fog latch | BM | verified-unbound (em_sul_0021B900) | unchanged | reused, not translated again |

No row of this lane is left "missing".

## 2. What the original does (read from the original code)

### The three builders (001CB5F0, 001CB6B0, 001CB760)

All three append one 0x20-byte block to a depth-bucket chain table. The
table (D_007635C0 on the route) holds 0x1000 slot words, then 0x1000 head
words at +0x4000.

1. **Slot.** The id is a depth key. 0xFFF000 passes through. Any other id is
   floored at 0 and capped at 0xFFB000 (the cap compares unsigned). The slot
   is id >> 12, so slots 0xFFC..0xFFE are never reached and 0xFFF only by
   0xFFF000 exactly.
2. **Block.** The block sits at the render context's +0x18 cursor + 0x100.
   The context is the block D_00275670 points at (0x811CC0 on the route).
3. **Words.**
   - 001CB5F0: +0x00 = 0x20000000; +0x04 = the block's own +0x10 address,
     low 28 bits; +0x10 = count | 0x20000000. It returns block + 0x20, the
     packet its caller fills, and advances the cursor by (count + 2) · 16.
   - 001CB6B0: +0x00 = kind | 0x30000000; +0x04 = the address argument, low
     28 bits (a 64-bit register); +0x10 = 0x20000000. Cursor += 0x20.
   - 001CB760: +0x00 = 0x50000000; +0x04 = the address, low 28 bits; +0x10 =
     0x20000000. Cursor += 0x20. A fourth argument register some callers
     load is not read.
4. **Link.** If the slot word is not 0, the block's +0x14 = that word, low 28
   bits (the previous block of this slot). Otherwise the head word (+0x4000)
   = the block, and +0x14 is left as it was. Then the slot word = the block.
   The slot is therefore a list from newest to oldest, and the head word is
   its oldest block.
5. **Order.** The memory operations run in this order: cursor read, +0x00,
   +0x04, +0x10, slot read, +0x14 or head, slot write, cursor re-read and
   write.

Nothing is bounded. 001CB800 (a renderer boundary) later walks slots
0..0xFFF, splices each slot's list into the frame chain (start tag → newest
block of the first slot → ... → its head → newest of the next slot ...), and
clears the slot words. It never clears the head words, so stale heads from
older frames stay in the table.

### 001CB900 and 001CB9B0

001CB900(table, id, mode) = 001CB6B0(table, id, 8, 001CB9B0(mode)).

001CB9B0 tests mode 4, 3, 2, 1, 0 in that order. It returns D_00275674 +
0x8A0 / 0x820 / 0x7A0 / 0x720 / 0x6A0 (D_00275674 = 0x814220 on the route).
Any other mode returns 0: the zeroing sits in the fall-through branch's delay
slot. The decomp C has no return value on that path, so the .s is the
source.

### 0021B9A0(mode, scale, bias): the fog / depth-range programmer

The context +0xB8/+0xBC hold the current near/far pair. +0xD8/+0xDC and
+0xF8/+0xFC hold two presets. The jump table has six entries: 0 → the +0xF8
pair, 1 → the +0xD8 pair, 2 and 4 → near scaled, 3 and 5 → far scaled.

- Mode 2 or 4: near = bias + near · scale (MUL.S, then ADD.S with the bias
  as the first operand); far unchanged.
- Mode 3 or 5: far = bias + far · scale; near unchanged.
- Any mode of 6 or more as an unsigned value (so every negative mode) takes
  the +0xF8 pair, like mode 0.

Then +0xB8 = near, +0xBC = far, and 0021B920(near, far) with the two values
passed in registers (not re-read). For modes 4, 5 (mode − 4 < 2, unsigned)
and 0, 1 (mode < 2, unsigned) only, 0021B900 follows. It copies +0xA0..+0xBF
to +0xC0..+0xDF through block_copy 00121870. The default path for other
modes does not latch.

### 0021B920(near, far)

1. k = 255.0 / (far − near): SUB.S (chopped), then DIV.S (nearest).
2. It stores +0xA0 = 255.0, +0xA4 = 2048.0, +0xA8 = far · k (MUL.S,
   chopped) and +0xAC = −k (NEG.S), in that order.

## 3. The translation

- **Functions.** Every routine above is a C function named
  `em_packet_chain_<address>`. The three builders share one `append`, which
  keeps the original order of memory operations. The 28-bit mask of the
  +0x04 address word is applied once, inside `append`.
- **Memory.** The routines address original EE memory: the render context,
  the packet buffer its cursor walks, and the chain table. The caller passes
  a list of mutable regions addressed by original address. D_00275670 and
  D_00275674 are plain values in `EmPacketChain`, because the routines never
  write them.
- **Checks before writing.** Every access is checked before the first write:
  the cursor word, the block's 0x18 bytes, the slot word and the head word.
  001CB5F0 also checks count · 16 packet bytes, because its caller writes
  them through the returned host pointer. The fog code maps context
  +0x00..+0xFF.
- **Floats** are raw bits. COP1 goes through `em_ee_float.h`: sub, div, mul,
  neg and add, each in the original's form.
- **Reused:** `em_sul_0021B900`. Its block_copy worker is a forward copy of
  32 bytes. The source and destination never overlap, so the result is the
  same as 00121870 on either of its paths.
- **Fail-stop.** These cases latch `fault`, `fault_function` and
  `fault_address`, and nothing is written:
  - a NULL chain;
  - a NULL packet output;
  - an unmapped address;
  - an em_sul_0021B900 failure.

  Every later call returns −1 until `em_packet_chain_clear_fault`.
- **Adapters.** `em_packet_chain_w_*` match each consumer's worker signature
  when the worker `ctx` is the `EmPacketChain` itself (section 5).

## 4. Verification

`python3 tools/test_packet_chain_reference.py`. The ELF SHA-256 is asserted.
The oracle is the EE interpreter of test_effect_manager_reference, with
COP1 from ee_float_model. Its stack is in the scratchpad, so every changed
EE RAM byte comes from the routines.

- **Call sequences.** Each sequence runs on a copy of one beat's 32 MB of EE
  RAM, the two sides call by call. **After every call** the render context
  (+0x00..+0xFF: the cursor and all fog state), every byte the original
  wrote during that call, and the returned packet address must be equal.
  After the sequence, **all 32 MB must be equal**. The per-call compare
  matters for the fog: a mode-1 call re-loads +0xB8/+0xBC and +0xA0..+0xAF
  from the +0xD8 pair, so without it a consumer pattern ending in (1, 0, 0)
  would hide every mode-2/3 result before it. Both sides fill each opened
  packet with the same bytes, the native side through the returned host
  pointer. The sequences cover:
  - 19 id boundaries × 2 tables (D_007635C0 and a table in plain RAM);
  - 13 counts, including −2, −1, 0 and 0x100;
  - 64-bit payloads;
  - 14 modes for 001CB900 and 0021B9A0 (including INT_MIN and INT_MAX);
  - 16 special floats (±0, denormals, ±MAX, ±Inf, NaN), with near == far
    (the zero divisor);
  - presets poked into +0xB8..+0xFC;
  - slot words holding bits 28..31 (0x10000000, 0xF0000000, 0x80001230,
    0xFFFFFFFF) before an append: the link keeps the low 28 bits, and a word
    of high bits alone still counts as "not empty";
  - aliasing: the block's +0x00/+0x04/+0x10/+0x14, the slot word, or the head
    word placed on the cursor word itself, for each builder and 001CB900. The
    cursor's re-read after the stores then sees what they wrote, so the
    order of memory operations (section 2, step 5) is checked against the
    original, not only the final values;
  - the call patterns of every consumer (the 001F0720 lane, the 001F0A60
    glint, the 001EA240 puff, the head-sprite 001CFBE0, 001F4CC0, the area
    script's (0, 0, 0), and the render context's 0xFFF000 / 0xFFC000 kicks),
    plus the glint and kinds patterns without their closing (1, 0, 0), and a
    "scaled" pattern of modes 4, 5, 2, 3 with scale != bias that ends on a
    scaled call;
  - random mixed sequences.
- **Adapters.** The consumer patterns run three more times, once per adapter
  variant, with the same per-call and 32 MB compares:
  - variant 0: `w_0021B9A0` (bits) and `w_001CB760` (3 arguments);
  - variant 1: `w_0021B9A0_float` and `w_001CB760_4` (its 5th register is
    0xDEAD, which must not matter);
  - variant 2: `w_0021B9A0_heads` (floats first, mode last) and
    `w_001CB760`;
  - all variants: `w_001CB5F0`, `w_001CB6B0`, `w_001CB900`.

  In quick mode, 78 fog calls go through each 0021B9A0 adapter (modes 0..5,
  f12 != f13 on every scaled call), 60 calls through the 3-argument 001CB760
  adapter and 30 through `_4`. The float adapter is also compared with the
  bits entry for each non-NaN special value against 150.0 in both
  positions, modes 2 and 3 (180 checks).
- **001CB9B0** runs over 30 modes against the original.
- **Fail-stop checks.** Unmapped cursor, block, slot, head, packet and
  context, a NULL output, the latch, and its clear. Nothing may be written.
- **Capture evidence** (all beats 00..14, independent of the oracle):
  - **Fog.** The context +0xA0 quadword equals native (and original)
    0021B920 of the captured +0xB8/+0xBC pair, and +0xC0..+0xDF equals
    +0xA0..+0xBF.
  - **Chains.** Both DMA buffers' frame chains are walked from 001CB800's
    start tag (D_0028F700 + n·0x70000 + 0x1F3EC0). Every block has one of the
    three shapes above. For a buffer that replays:
    - the words are cleared;
    - the blocks are re-issued through the native builders in address order
      (= call order);
    - slots come from the link direction, with the captured head words where
      they are current;
    - blend blocks go through 001CB900.

    Every builder-written word must equal the captured one: +0x00, +0x04,
    +0x10, and +0x14 except on head blocks, where 001CB800 wrote it. So must
    every current head word and 001CB800's splice over the replayed slot
    words.

    1,053 blocks in 79 slot runs replay: 576 opens, 77 references, 177 blend
    blocks and 223 calls. 1,038 of them (all but the first of each beat) sit
    exactly where the previous builder call left the cursor. On the route,
    nothing else advances the cursor between these calls.

| Mode | Time | What ran |
|---|---|---|
| quick (default) | ~2.5 s | 3 of 15 beats with call cases: 789 sequences, 1,980 calls, 63,448 bytes written; 99 adapter sequences. Fog and chain replay on all 15 beats |
| full (`EM_TEST_FULL=1`) | ~20 s | 15 beats: 9,645 sequences, 35,055 calls, 1,142,640 bytes written; 495 adapter sequences. Same capture checks |

**Mutation check** (scratch, not committed; re-run after the fix round).
27 mutants; each of these 25 was killed by the default run:
- translation: clamp ceiling; negative floor; the 28-bit mask of the +0x04
  word; the 28-bit mask of the +0x14 link; cursor advance; head word;
  001CB9B0 default and mode 3; the fog ADD.S in host arithmetic; latch mode
  set; negative-mode preset; SUB.S and MUL.S in host arithmetic;
- reachable-state equivalents, now killed by the aliasing and slot-bit
  cases: the cursor written as base + advance instead of re-read (differs
  only when a store aliases the cursor word); the emptiness test on
  (prev & 0x0FFFFFFF) instead of prev (differs only for a slot word holding
  bits 28..31 alone). On the route neither state occurs, so the default run
  of the first round could not see them;
- adapters: f12/f13 swapped in each of `w_0021B9A0`, `w_0021B9A0_heads` and
  `w_0021B9A0_float`; a changed address in `w_001CB760`; `w_001CB760_4`
  passing its unread register; a changed kind in `w_001CB6B0`; a changed
  mode in `w_001CB900`; a changed id in `w_001CB5F0`. The three f12/f13
  swaps are killed by the first glint call (mode 2, scale 1, bias 150),
  compared before the closing (1, 0, 0).

Two mutations survive because they are equivalent for every input:
- NEG.S as a bare sign flip: k is never Inf or NaN, because DIV.S
  saturates;
- a clamp change that only moves 0xFFB001, which is in the same slot.

An ASan/UBSan scratch build over INT_MIN/INT_MAX ids, counts and modes,
UINT64_MAX payloads, a wrapping table address, every 0021B9A0 adapter over
special floats and an unmapped context is clean (re-run in the fix round).

## 5. Binding notes (for the coordinator chain; nothing is wired)

**Memory and ctx.** The coordinator owns one `EmPacketChain`:
- regions: the render context (0x100 bytes from D_00275670), the packet
  buffers the +0x18 cursor walks (the two 0x70000-byte halves of D_0028F700
  on the route), and each chain table used (0x8000 bytes; D_007635C0 on the
  route);
- d275670 and d275674: the values of those globals.

Every consumer's worker table shares a single `ctx` among all its workers.
So either that ctx is the `EmPacketChain`, or, the usual case, the binding
layer writes one-line shims that fetch its `EmPacketChain` and call the core
function. The adapters show the exact argument mapping.

| Consumer | Worker | Core call | Notes |
|---|---|---|---|
| em_head_sprite_original (001CFBE0) | `w_001CB5F0(ctx, a0, id, count, &out)` | `em_packet_chain_001CB5F0(pc, a0, id, count, &addr, &out)` | a0 = EM_HEAD_SPRITE_ORIGINAL_CHAIN; the consumer then fills count·16 bytes |
| | `w_001CB6B0(ctx, a0, id, 9, address)` | `em_packet_chain_001CB6B0(pc, a0, id, 9, address)` | |
| | `w_001CB760(ctx, a0, id, tbl, ent)` | `em_packet_chain_001CB760(pc, a0, id, tbl)` | the 4th register is not read (`_w_001CB760_4`) |
| | `w_001CB900(ctx, a0, id, mode)` | `em_packet_chain_001CB900` | |
| em_effect_manager (001F0720, 001F0A60) | `w_001CB5F0`, `w_001CB760`, `w_001CB900` | as above | chain D_007635C0 + (a0 << 15). Any a0 ≠ 0 needs that table mapped, or it faults |
| | `w_0021B9A0(ctx, mode, f12, f13)` bits | `em_packet_chain_0021B9A0` | **after each call, refresh `view->fog` with `em_packet_chain_fog`**; the view keeps a copy that the module re-reads after the calls |
| em_player_equipment_sprite | `w_001CB5F0(chain, z, 6)`, `w_001CB6B0(chain, z, 2, D_00251220)`, `w_001CB900(chain, z, mode)` | as above | |
| em_anim_runtime_rest | `w_001CB760(ctx, EM_ANIM_REST_DEPTH_TABLE, key, payload)` | `em_packet_chain_001CB760` | the 28-bit masking happens inside, as the header says |
| em_render_context (001DE8DC and the tail) | `w_001CB760(ctx, EM_RC_D_007635C0, 0xFFF000 / 0xFFC000, address)` | `em_packet_chain_001CB760` | 0xFFF000 → slot 0xFFF; 0xFFC000 clamps to slot 0xFFB |
| em_effect_original (001EA240) | `w_0021B9A0(ctx, channel, float, float)` | `em_packet_chain_w_0021B9A0_float` semantics (bit copy) | calls (2, 1, 100), (3, 1, 100) … (1, 0, 0) |
| em_effect_kinds | `w_0021B9A0(ctx, a0, f12 bits, f13 bits)` | `em_packet_chain_0021B9A0` | (2, 0, 1e5), (3, 0, 1e6), (1, 0, 0) (001F4CC0) |
| em_area_script | `w_0021B9A0(ctx, mode, float, float)` | float adapter | (0, 0, 0) |
| em_frame_render_heads (001D1DC0) | `w_0021B9A0(ctx, f12, f13, a0)` | `em_packet_chain_w_0021B9A0_heads` | **argument order differs**: floats first, mode last; the call is mode 0 |
| em_snow_runtime | (none: hard-coded fog) | when 001E67C0 is bound: 0021B9A0(2, 0, 0), (3, 0, 300) … (1, 0, 0) | its constants (255, 2048, 300·k, −k) with k = 255/300 equal this module's EE result bit for bit |

**Required: one translation of 0021B920 (done in the Effects step,
2026-09-24).** `em_fog_gs_coefficients` (`src/gfx/metal/em_fog_gs.h`) was a
second translation of 0021B920, and it was not bit-exact (6.4). It is now a
thin call of `em_packet_chain_0021B920` on a private context block, and it
returns the +0xA8/+0xAC pair. `em_gfx_fog` aborts if that call ever
faults; it cannot, because the block is mapped.
`tools/test_area11_fog_reference.py` checks the helper against:
- the executed original (AREA11's pair);
- the EE model's 0021B920 on 2,006 pairs;
- each in-scope route beat's context +0xA8/+0xAC.

The old host formula fails the model check at (−110, 330) (0x433F4000
against 0x433F3FFF). The census 0021B920 row is live.

What remains is the fog block itself. The Metal fog is still handed the
scene manifest's (near, far) record pair. The original writes the context
block through 0021B970 at the area load and 0021B9A0(0, 0, 0) at the frame
head. Once the render-context block is canonical (L32 / L30), the renderer
should read +0xA8/+0xAC from it.

**Scene order.** The fog programmer mutates shared state: +0xB8/+0xBC, +0xA0
and the +0xC0 latch. Every consumer that reads the fog quadword must read it
from the same context block the programmer writes, never from a copy made
before the call.

## 6. Corrections found on the way (for the lead; no existing file was edited)

1. **The census "No translation" rows** for 001CB5F0, 001CB6B0, 001CB760,
   001CB900 and 0021B9A0 become verified-unbound (section 1).
2. **001CB9B0** is listed as a renderer boundary. It is game-visible (it
   picks the blend-state block). It now has a verified translation.
3. **Wrong labels for 0021B9A0**, which is the fog programmer:
   - port: `em_effect_original.h` ("pad rumble", flagged by
     EFFECT_MANAGER.md finding 1; corrected in the Effects step);
   - decomp comments: `func_001EB600.c` ("chan, vol, pan") and
     `func_001F4CC0.c` ("audio/effect parameter ranges"). Other decomp
     prototypes call its first argument "chan" or "id".
4. **em_fog_gs_coefficients is not bit-exact.** It lives in
   `src/gfx/metal/em_fog_gs.h` and is the census's translation of 0021B920.
   It computes in host binary32, rounding to nearest, while the original's
   SUB.S and MUL.S chop:
   - Over 10⁶ random (near, far) pairs (near in −1000..1000, far above it),
     58.6% of results differ, by up to 3 ulp.
   - AREA11's (−209, 304) happens to agree, and the AREA11 fog test passes.
   - Capture proof: for (−110, 330), the fog pair of beat 15 (after the
     level exit, outside the first level), the capture holds +0xA8 =
     0x433F3FFF. em_packet_chain_0021B920 gives that value; em_fog_gs gives
     0x433F4000.

   Required (section 5): the Metal backend must take its coefficients from
   the context block this module writes, or call em_packet_chain_0021B920,
   and em_fog_gs_coefficients is retired or made a thin call of it. A
   reviewer independently reproduced the beat-15 values (capture and EE
   translation 0x433F3FFF, em_fog_gs 0x433F4000).
5. **The head sprite's 001CB760 worker** has a fifth argument (`ent`).
   001CB760 never reads it. That is harmless, but the header should say so.

## 7. Makefile hunk (not applied; the Makefile belongs to the lead)

```make
.PHONY: test-packet-chain-reference
test-packet-chain-reference:
	python3 tools/test_packet_chain_reference.py
```

Since the Effects step `src/game/em_packet_chain_original.c` and
`src/game/em_status_ui_leftovers.c` (for em_sul_0021B900) are in `COMMON`.
The two tests that compile `em_fog_gs.h` shims, test_area11_fog_reference
and test_shadow_original_reference, link both files.

## 8. Limits

- **001CB800** (the splice and slot clear) and **001CB8A0** are not
  translated. They are renderer boundaries. The replay checks their
  observable result over the captured chains, but no native 001CB800 exists.
- **The capture replay** re-sets the cursor before each block. Of 1,053
  blocks, 1,038 were contiguous, so no other cursor writer was observed
  between builder calls; the first block of each beat is not checked this
  way. Slot numbers are exact only where the captured head word is current
  (61 of 79 runs). A run can join two slots whose blocks do not interleave.
  Such a run replays to the same bytes.
- **Quick mode** runs the call sequences on 3 beats with a 20-sequence random
  sample. The builders depend on a beat only through the cursor and context
  values; full mode runs all 15.
- **NaN payloads** through the float adapter are compared only where the host
  ABI cannot quieten them (the bits adapter is exact).
