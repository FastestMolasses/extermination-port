# Frame render heads (census lane L32)

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "frame-render-heads" (census lane **L32-frame-render-heads**,
`docs/FIRST_LEVEL_CENSUS.md`). This document covers the 18 original functions
of that lane: the per-frame render head, the frame close, the projection, the
zoom helpers, the area render-env step, the per-slot constant fill, the
render reset at area load, and the lighting-mode dispatch.

- Translation: `src/game/em_frame_render_heads.c` / `.h`.
- Oracle: `tools/test_frame_render_heads_reference.py`.

The module is **built and tested but not wired**. Section 4 lists what the
coordinator binds, and what each binding replaces.

The module contains no original code or data. It cites original addresses.
Its constants are the values the original materialises: float bit patterns,
the two 001E2260 tags, and offsets.

## 1. Model

These routines address original memory directly:

- the render context is whatever the word **D_00275670** points at (the
  captures hold 0x811CC0);
- the display list is wherever the context's **+0x10** cursor word points;
- the 001D8C30 operands are caller addresses.

So the module works on **views**. A view is an original address range backed
by storage the host owns (`EmFrhView`). Every load and store resolves its
original address through the views:

- An address no view covers, a store into a read-only view, or a misaligned
  word or halfword access latches `EM_FRH_FAULT_BAD_ADDRESS`.
- Quadword accesses clear the low four address bits, as the EE does.

**Workers.** Every original callee outside the lane is a worker, named by its
address (`EmFrhWorkers`, 31 slots). A NULL worker or a negative worker result
latches a fault. So does a refused float form.

**Fail-stop.** Before its first worker call or store, each entry checks that
every worker its call tree can reach is bound and that its fixed addresses are
mapped. Addresses that depend on memory contents are checked as soon as they
are known, and before the routine's first store through them. The oracle
asserts this for every reachable worker of every entry, and for an unmapped
context: the result is -1, the fault names the worker, no worker ran and
memory is unchanged.

**Arithmetic.** Every COP1 and VU0 macro instruction goes through
`em_ee_float.h` on raw bits, under the forms the original uses. Float
arguments and results cross the API as raw register bits.

**SDK leaves.** Four leaves are translated privately in the module:

- copy_qw4 00102958: four quadword loads, then four stores;
- 00102948: one quadword copy;
- 001026D0: the VU0 product. The rows of the second operand go through the
  first; the MULAbc x, MADDAbc y/z and MADDbc w forms apply to all lanes;
- 001029C0: the VU0 identity. It is formed with VSUB and VADD.w, then VMR32.

The oracle executes the original leaves.

## 2. Per function

In the table, **Before** is the census status and **After** is the status once
this lane is merged. **After** is "verified-unbound" for all 18 rows: an
oracle checks each translation, and nothing live runs it yet (section 4).

| Function | Decomp | Before | After |
|---|---|---|---|
| 001D1C50 per-frame head | NM | stand-in | verified-unbound |
| 001D1EA0 frame close | BM | stand-in | verified-unbound |
| 001D30A0 per-slot fill | NM | stand-in | verified-unbound |
| 001D2960 projection | BM | stand-in | verified-unbound |
| 001D2D20 perspective matrix | BM | stand-in | verified-unbound |
| 001D25F0 zoom store | BM | stand-in | verified-unbound |
| 001D2590 zoom from angle | AI | unverified | verified-unbound |
| 001D2610 scope zoom | BM | unverified | verified-unbound |
| 001C1D00 render-env step | BM | stand-in | verified-unbound |
| 001D1EF0 tear-down frame | BM | missing | verified-unbound |
| 001D19E0 render reset | BM | missing | verified-unbound |
| 001D2830 registration dispatch | AW | missing | verified-unbound |
| 001D9070 fade weights | NM | missing | verified-unbound |
| 001D19D0 thunk | BM | missing | verified-unbound |
| 001D8060 light-slot lookup | BM | missing | verified-unbound |
| 001D80B0 light-slot release | BM | missing | verified-unbound |
| 001D88B0 lighting dispatch | BM | unverified | verified-unbound |
| 001D8C30 fixed-light fill | NM | unverified | verified-unbound |

### 001D1C50: the per-frame render head (S0 onward, every frame)

1. It calls 001D2830(4, 0).
2. **D_008106C4 nonzero:** it calls 001D2830(6, 0) and goes on at step 3.
   **Otherwise,** it tests 001B0070() & 0x80.
   - **Bit 0x80 set:** it works out a flag. The flag is 1 when D_008106C7 is
     nonzero. When D_008106C7 is zero, the flag is 1 only if 0015D2F0() == 2
     and D_008106C6 == 2. It then calls 0021B970:
     - (0, 50) when the flag is 0;
     - (50, 150) when the flag is 1 in area 8;
     - (0, 210) when the flag is 1 in any other area.

     Then it calls 0021BA80(8, 8, 0x15) and 001D2830(6, 1).
   - **Bit 0x80 clear:** when the scratchpad byte 0x70003B8D is 0 or 4, and
     not (0015D2F0() == 2 and D_008106C6 == 2), it calls 0021B9A0(0, 0, 0).
     Then it calls 001D2830(6, 1).
3. It copies the doubleword at context +0xB0 to slot +0x360. The slot is at
   D_00275674 + (context +0x9C) * 0x30.
4. It writes a DMA REF tag at the cursor:
   - byte 3 = 0x30;
   - word 1 = slot + 0x340;
   - halfword 0 = 3 (three quadwords).

   Bytes 2 and 8..15 are left as they are. The cursor then advances by 0x10.
5. It calls 001D2960(D_00810610).
6. It copies P (context +0x2340) to scratchpad 0x70003A40, and K (+0x23C0) to
   0x70003AC0.
7. It calls 001D7C30 (the point-light tick), then 001D30A0.

The translation follows the .s (NEARMISS). Pointers and the cursor are re-read
where the original re-reads them.

### 001D30A0: the per-slot fill

This routine copies into 14 skin records at D_00816440 + 0x100 k. Each record
has two slots of 0x80, and the slot index is context +0x9C.

1. **Twelve records** get three quadword copies each:
   - context +0x2220 to slot +0x60;
   - +0x2230 to slot +0x70;
   - +0xA0 to slot +0x50.
2. **Four words:** records 0x816B40 and 0x816C40 get +0x58 = 255.0 and
   +0x5C = 0.
3. **Record 0x816540** gets the same three copies.
4. **Record 0x816840** gets its own eight guard words at +0x60..+0x7C, and
   +0x50 from +0xA0.
5. **Finally,** scratchpad 0x70003AC0 (64 bytes) is copied to D_002513E0.

The slot index is re-read before every copy, as the .s does.

### 001D2960(view): the projection

s is the zoom at context +0x2468.

- **P (+0x2340):** word +0x2340 = 0.8 s and +0x2354 = 0.5 s; +0x2360 and
  +0x2364 = 2048, +0x2368 = 0x3F664CB3, +0x2378 = 0x49CCCCCC, +0x236C = 1,
  every other word 0.
- **V (+0x2380):** a copy of the view matrix.
- **K (+0x23C0):** K = V x P, by the 001026D0 product.
- **Alternate projections:** four matrices from 001D2D20, each multiplied with
  V into +0x2240, +0x2280, +0x22C0 and +0x2300. The arguments are
  (s, 1280, 560, 0.1, 16711680), (s, 1280, 560, 20, 16711680),
  (s, 3584, 3584, 0.1, 16711680) and (s, 2048, 2048, 0.1, 16711680).
- **Guard band (+0x2220..+0x223C):** eight constant words.
- **Cull planes (+0x2410..+0x244C):** four planes, each with its own sqrtf
  (0011E748) call of s·s + 1046529. With inv = 1 / root, plane i is
  (±s·inv on x for i < 2, or on y otherwise, 0, −(−1023·inv), 0).

### 001D2D20: one perspective matrix

001D2D20(focal, width, height, near, far) starts from the identity (001029C0)
and then sets:

- m0 = focal / (0.5·width)
- m5 = focal / (0.5·height)
- m10 = (far + near) / (far − near)
- m14 = (−2·(far·near)) / (far − near)
- m11 = 1 and m15 = 0

The native entry fills a native matrix, because its callers pass a stack
local.

### The zoom functions: 001D25F0, 001D2590, 001D2610

- **001D25F0(z):** stores z to scratchpad 0x70003B60, then to context +0x2468.
- **001D2590(a, b):** calls 001D25F0(a / tanf(b / 2)). tanf is 0011E398.
- **001D2610(x):**
  1. It calls 001D2590(224, 0.017453292·(5 + 45·(1 − x))).
  2. a = context +0xF8. The original reads it in the 001B0070 call's delay
     slot.
  3. b = context +0xFC when 001B0070() & 0x80, or when D_00810700 == 0x11.
     The area byte is read only when bit 0x80 is clear. Otherwise, with
     fc = +0xFC, b = fc + x·(450 − fc) if fc < 450, else fc + 200·x.
  4. It calls 0021B970(a, b).

  For x = 0 the zoom is 0x43F02F4F (≈ 480.37), from the original tanf.

### 001C1D00(state): the area render-env step

- The state byte at `state`:
  - 0: set to 1, then run;
  - 1: run;
  - any other value: return.
- Run:
  1. When (D_00810700 << 8) + D_00810701 == 0x1500, it calls 001E2260 with
     the 64-bit tag 0x20076C0121323740 if 001D2910(0x24) is nonzero, else
     with 0x20076A8121323700.
  2. It calls 001E0CF0(), then 001D5370().

### 001D1EA0(a0): the frame close

1. **a0 nonzero:** when 001D2910(4) == 0, it calls 001E0D70() and
   001DDA00().
2. **Always:** it calls 001CB800(0x7635C0, 0, context, context + 4).

### 001D1EF0: the tear-down frame

It calls 001D1C50(), then 001D2830(3, 1), then 001D1EA0(0).

### 001D19E0: the render reset (S1 area load)

1. It calls skin_arena_init (001D2E20), 001D9720, 001DD940, 001E0C30, 001D9060,
   001D71F0 and 001D7BB0.
2. It calls 001D2830 with (2, 0), (9, 0), (0x24, 0) and (5, 0).
3. It calls 001D2DE0(1, 0x320) and 001D2DE0(2, 0).
4. It calls 001D2830(7, 0), 001D2830(8, 0), 001E0CC0() and 001D2DE0(0, 0).
5. It sets context +0x1D8 = 0 and +0x1E8 = 0.
6. In area/sub 0x11/0x00 it calls 001E0380().

### 001D2830(a0, a1): the registration dispatch (asm-word unit)

This is a signed test on a0:

- a0 < 0x20: v0 = 001D2730(a0, a1);
- a0 < 0x40: v0 = 001E0C80(a0, a1);
- otherwise: v0 = 0.

### 001D19D0 and 001D9070: the fade weights

001D19D0 is a jump to 001D9070.

001D9070 takes the model = 001C6120(D_0028A56C, 0x16). For each of the
model's `*model` groups (stride 0x820), and for each of the 32 entries e of a
group (stride 0x40, starting at model + 0x50):

1. t = e[+0x38] / 190.
2. v is picked by t:
   - t ≤ 0.3 and t < 0: v = 1;
   - t ≤ 0.3 and t ≥ 0: v = clamp(1 − t, 0, 1);
   - t > 0.3: v = 0.
3. e[+0x2C], e[+0x28], e[+0x24] and e[+0x20] are all set to 2v.

### 001D8060 and 001D80B0: the light slots

- **001D8060(id):** −1 gives 0. Otherwise it returns the first of the 32
  slots at context +0x220 (stride 0x80) whose +0xC word equals id, or 0.
- **001D80B0(id):** for a slot it finds, it sets +0x2C = 0, then +0xC = −1.

### 001D88B0 and 001D8C30: lighting

**001D88B0(a0, a1, a2, a3)** reads the mode at context +0x246C.

- Modes 1, 3, 4, 5 and 6 go to 001D8C30(mode, a1, a2, a3). The registers
  pass through.
- Any other mode:
  1. D_00275688 = 0x817BC0.
  2. It calls 001D8130(0x20, a0).
  3. It calls 001D8340(0, a1, a2, 0x20, a0). The fifth argument goes in t0.
  4. It calls 001D8690(a1, a2, a3, 0x20).

**001D8C30(mode, m, out, in)** first sets t = max(in.w − 1, 0). Here
bias = 8388608. The case comes from a jump table for mode < 7 (unsigned).
Any other mode takes case 0.

| Mode | m | out |
|---|---|---|
| 0, 1, ≥ 7 | cleared column by column | out[0..11] cleared; out[12..14] = bias + (128 + in.xyz); out[15] = bias + 64t |
| 2 | cleared column by column | out[0..11] cleared; out[12..14] = bias + in.xyz; out[15] = bias + 64t |
| 3 | not touched | out[0..2] = bias + 128·in.xyz; out[3] = bias + 64t |
| 4 | the context V | out[0..2] = bias + 128·in.xyz; out[3] = 8388672 |
| 5 | the context V | out[0..2] as mode 4; out[3] = 0.2·(128·in.w) |
| 6 | the context V | out[0..2] as mode 4; out[3] = bias + 128·in.w |

The translation keeps the original's order of loads and stores. That order
matters when the operands alias.

## 3. Verification

`tools/test_frame_render_heads_reference.py` (`make test-frame-render-heads-reference`).

### Unit mode

- **Coverage.** The default run is 700 of 6,000 cases in about 4 s.
  `EM_TEST_FULL=1` runs all 6,000 (about 45 s). All 18 entries run, and all
  eight 001D8C30 jump-table cases.
- **Memory.** The base is the 01_battery capture, with seeded pokes: flags,
  area, slot index, zoom, the V matrix, light ids, synthetic 001D9070 models,
  and aliasing 001D8C30 operands. Float values include boundaries, NaN, Inf
  and denormal patterns.
- **What is compared.** Both sides run from the same image. Afterwards the
  test compares the whole 32 MB RAM and the 16 KB scratchpad byte for byte,
  the ordered worker-call list with every argument, and the v0 of 001D2830
  and 001D8060.
- **Callees.** The test asserts that the worker set is exactly the set of jal
  targets of the translated routines (46 targets).
- **Branch coverage.** Every conditional branch of the translated routines
  and leaves (42) is asserted both ways. Two outcomes are exempt as
  unreachable: the 001D9070 clamps (v = 1 − t with 0 ≤ t ≤ 0.3).
- **sqrtf and tanf.** Their results come from the original 0011E748 and
  0011E398, run in a separate interpreter.
- **Fail-stop.** The run ends with 35 fail-stop checks.

### Route mode

The route mode runs in the default run on 3 beats (00, 14 and a fixed-seed
pick). `EM_TEST_FULL=1` runs all 15. `EM_TEST_WORLD=1` runs it alone.

From each beat's snapshot the test runs, in order:

- 001D1C50, 001C1D00(0x8101D0) and 001D1EA0(1), twice;
- 001D2610(0) and 001D2610(1);
- 001D88B0 (lighting mode 0 on every beat).

This happens twice. Once, everything is original. Once, the native
translations work directly on the second interpreter's memory. On both sides,
every worker runs the ORIGINAL callee. The recorded boundaries, which return 0
on both sides, are:

- 001CB800 (the GS/DMA kick);
- 001E0D70 and 001DDA00 (the world flush);
- 001D5370. It is in lane L30, and its clip test uses VCLIP, which the shared
  interpreter does not model.

After every call the test compares the whole RAM, the scratchpad and the
worker log. **Result: 15 of 15 beats identical.**

### Mutations

Mutations were checked by hand. The oracle caught four of five injected
defects:

- a P scale constant;
- the second-plane negate;
- a 0x816840 guard word;
- the REF tag byte.

The fifth (t < 0 against t ≤ 0 in 001D9070) is an equivalent mutant: both give
2.0 at t = 0.

### Arithmetic

The test's interpreter takes every COP1 and VU0 op from
`tools/ee_float_model.py`. An op the model does not define raises an error.
The route callees needed the VU0 forms VSQRT, VDIV, VMULq, VADDq, VOPMULA and
VOPMSUB, plus the MMI PEXTLW and PEXTUW. All are handled in this file.

### Limits

- **Beats never at these moments.** The route beats never start at S0 or S1.
  So 001D19E0, 001D19D0, 001D9070, 001D1EF0, 001D8060 and 001D80B0 are
  checked by the unit oracle only.
- **Lighting modes on the route.** Every beat's lighting mode is 0. 001D8C30
  is exercised on the route only through the unit cases. The census shows it
  running from S2, so its callers must set a mode for draws; this oracle does
  not see those calls.
- **Callers with native arguments.** 001D2D20 and 001D2610 are unit-checked
  with synthetic arguments. The route checks 001D2610 at x = 0 and x = 1.
- **Not checked here.** Downstream GS and VU1 consumption of the REF tag, the
  matrices and the slots is not checked. The REF tag bytes are compared as
  memory.

## 4. Binding (for the coordinator)

### Storage the live side must provide as views

- the D_00275670 / D_00275674 / D_00275688 words;
- the render context: this module reaches +0x10..+0x246F (the workers reach
  more, e.g. 001D2730 +0x0C and 001D2DE0 +0x2520.., through their own
  bindings);
- the slot block at D_00275674 (idx·0x30 + 0x340..0x368);
- the display-list buffer the +0x10 cursor points into;
- D_00810610 (64 bytes);
- the request bytes D_008106C4/C6/C7 (EmSceneState request block) and
  D_00810700/701 (the EmProgress region);
- the scratchpad 0x70003A40..0x70003B90;
- D_00816440..D_00817240;
- D_002513E0 (64 bytes);
- D_0028A56C and the model block 001C6120 returns;
- the D_008101D0 state byte.

The port has no canonical render-context storage today. The coordinator must
add it, as one owner, before binding.

### What each entry replaces

| Entry | Replaces | Notes |
|---|---|---|
| em_frh_001D1C50 | `w_001D1C50` in `em_scene_bindings.c` → `em_render_001D1C50`, which runs only `point_light_tick()`, in both variants and the status frame | Workers: w_001D7C30 = the point-light tick (`em_point_light_tick`, `em_point_light.c`). The fog workers (0021B970, 0021B9A0, 0021BA80) are the L31 translations (`em_fog_gs`, `em_game`). w_001B0070 = the request word D_008106C8 (L01). w_0015D2F0 is lane L28; its live `em_weapon.c` is a stand-in. |
| em_frh_001C1D00 | `w_001C1D00` → `em_render_001C1D00` (`render_env_init`, an empty skeleton) | 001E2260 and 001E0CF0 are lane L31 (`em_background_gs`). 001D5370 is lane L30. |
| em_frh_001D1EA0 | `w_001D1EA0` → `em_render_001D1EA0` / `frame_close_out` | The native renderer stays the GS/VU1 boundary. 001CB800 is the kick, where the renderer consumes the list. 001E0D70 and 001DDA00 are lane L30. |
| em_frh_001D1EF0, em_frh_001D19E0 | `um_001D1EF0`, `um_001D19E0` | Unmirrored bindings today. |
| em_frh_001D19D0 | `UM_001D19D0` in the 001AD1A0 binding (`em_scene_bindings.c`, near line 979) | |
| em_frh_001D2830 | `um_001D2830` (scene) and `veil_001D2830` (load veil) | 001D2730 and 001D2910 are boundary-classified in the census, but they are context-flag logic. 001E0C80 is lane L30. |
| em_frh_001D2610, em_frh_001D25F0 | three live stand-ins, listed below | |
| em_frh_001D2960, 001D2D20 | the native projection in `em_render_frame.c` / `em_math.h` | The renderer should read P, K, the alternate matrices, the guard band and the planes from the context. |
| em_frh_001D88B0, 001D8C30 | `em_lighting.c`'s face lighting mode, and the derived case-1 colours in `em_status_models.c` and `em_effect_color.h` | Those compute the post-bias value, not the original's words. Workers: 001D8130, 001D8340 and 001D8690 are `em_lighting.c` (live translations). |
| em_frh_001D8060, 001D80B0 | nothing | No port counterpart today. |

The three stand-ins that em_frh_001D2610 and em_frh_001D25F0 replace:

- **`em_area_script.c`.** Its `w_001D2610` and `w_001D25F0` slots take a
  float. Adapters pass `em_ee_bits(x)`.
- **`em_area11_interaction_host.c`** `EM_INTERACTION_SCOPE_ZOOM_ZERO`. It
  hard-codes the zoom 0x43F02F4F. That value matches 001D2610(0), but the
  stand-in skips the 0021B970(+0xF8, b) fog call.
- **`em_camera.c` `em_camera_scope_zoom`.** It uses host tanf and clamps t to
  [0, 1], and the original has no such clamp.

### Leaf workers

- **sqrtf and tanf:** `em_sdk_math_original_float_0011E748` and
  `em_sdk_math_original_float_0011E398` (`em_sdk_math_original.c`, in COMMON).
  They are reached through an adapter that checks the context's fault field.
- **001C6120:** `em_owner_services` / `em_pose_host_workers` (lane L33).

### Makefile

Makefile target (report only; the lead edits the Makefile):

```
.PHONY: test-frame-render-heads-reference
test-frame-render-heads-reference:
	python3 tools/test_frame_render_heads_reference.py
```

When the lead binds the module, add `src/game/em_frame_render_heads.c` to
COMMON.
