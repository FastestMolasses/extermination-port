# An exact GS model on the GPU (L2): design

Design, 2026-10-17 (research pass and adversarial review; port HEAD 17c5bf9).
Status: recorded, not started. The user reviewed it and chose "Not yet" for
stage 0.5 (2026-10-17): no port code until the user approves it. Decided:
15(d), package the exact arithmetic layer as its own documented component
(yes). `exp*/`, `metal_probe/`, `spike/` and `energy_probe/` mean the
research pass's local receipts under `build/l2_explore_1017/` (git-ignored,
section 17).

Paths are relative to the port repository. `file:line` points at HEAD 17c5bf9.

---

## 1. Goal and principle

**Goal.** Run the Original profile's GS model as GPU compute and keep the output
**bit-exact with the CPU GS model**: every byte of the 4 MiB local memory after
every frame. The CPU model is `src/gs/em_gs_raster.c` with `src/gs/em_gs_world.c`
(docs/GS_EXACT.md). The existing GPU renderer (`EM_GPU_RENDERER=1`,
`src/gfx/metal/em_gfx_metal.m`) is not exact and is not the reference. This
design does not touch it.

**The user's principle (2026-10-15/17).** It is not enough for the port to run
fast enough on this Mac. It is a PS2 game, so it should run as efficiently as
possible to save battery and energy, while staying fully faithful. Whenever
there is room for more performance within the faithfulness rules, that route is
taken. The work should also help future PS2 recompilation projects. Three
consequences for this design:

- **Energy and CPU time are the measures, not ticks over.** On this Mac the GS
  already fits the tick: at 6 workers the busiest worker is 3.7-5.1 ms and the
  game thread sets the tick (PERFORMANCE.md P16, docs/PERFORMANCE.md:1574-1580).
  L2 is judged by energy per tick and CPU-ms per tick at the game's own pace.
  L2 has to earn its place by measurement. If it does not save energy, it stops
  (section 14).
- **Exactness is not traded for speed.** No relaxed step exists in this design.
  Anything that changes a byte belongs to the Enhanced profile (PERFORMANCE.md
  section 6) and is out of scope.
- **Reusable for other PS2 projects.** The arithmetic layer and its proofs
  depend only on the GS rules in GS_EXACT.md and the CPU model, not on this
  game. It is worth keeping as its own self-contained, documented component:
  integer-only emulation of binary64 and binary32 rounding chains, certified
  fixed point, and an exact stq_div (sections 4 and 7).
  *(review, narrowed)* The frame architecture is not game-independent. It
  leans on properties this game has and many PS2 games do not:
  - no transfer inside a recorded frame. The recorder faults on one
    (em_gs_world.c:1508-1514), and uploads happen only during loads
    (em_scene_bindings.c:4920-4942). Games that stream textures mid-list
    need transfers as ordered GPU operations;
  - no CLUT load from the frame's own drawing on the route
    (PERFORMANCE.md:1675);
  - no self-feedback span and no same-segment target alias (sections 6.3
    and 8.3, to be counted in stage 0.5);
  - only CT32 / Z24 targets on the route (PERFORMANCE.md:1330).

  A reusable GPU GS for other projects needs stage 5's general paths and
  these cases handled on the GPU, not routed away.

**Hosts.** The M1 Pro Mac (macOS) and the iPhone Air (A19 Pro, iOS) now;
Windows (D3D12) and Linux (Vulkan) later.

---

## 2. What must be bit-exact

**The reference is the CPU model, not the PS2.** The CPU model itself is proven
against the original's captures, with known and counted gaps:
- 703 of 906 conformance tests are bit-exact. The other 203 are recorded as open
  with exact counts (GS_EXACT.md section 1 and section 8; `OPEN` in
  tools/test_gs_raster_reference.py).
- The depth-of-field pass is bit-exact on the original's captures
  (GS_EXACT.md section 7, test_dof_pass_reference).
- The fb2 route points have recorded floors (GS_EXACT.md 10.3).

The GPU model must reproduce the CPU model, **including its open-item
behaviour**. The Z start's binary32 reciprocal and the binary64 STQ chain do not
match the reference exactly (GS_EXACT.md 8.1, 8.2), but they are what the
Original profile draws today. So every `OPEN` count, every fb2 floor and every
DOF capture result must come out unchanged on the GPU, not merely "no worse".

**What "bit-exact" covers:**
- all 4 MiB of local memory after each frame: the fields, the Z buffer, the
  256x256 copy buffer, the shadow target, and everything else;
- the presented field, which follows from memory;
- the frame's fault behaviour. Refusals, span faults and residency faults stay
  on the CPU parse (section 6), so they are unchanged by construction.
  *(review)* One refusal is raised at draw time, not at the parse: draw_one's
  "no memory for the address tables" (em_gs_raster.c:3352-3358), when a
  worker cannot build its swizzle tables. The GPU path's equivalent, a
  failed buffer or pipeline allocation, must latch a fault reported by the
  same em_gs_world_wait call, never a silent CPU redraw.

**What need not match.** The diagnostic counters (`EmGs.pix`, the spec-loop
counts, the decoded-copy statistics) are diagnostics. The GPU keeps drawn-pixel
counts where they are cheap, because the existing differentials compare them
(PERFORMANCE.md:1023).

---

## 3. How it is proven

The existing **GS proof set** (docs/PERFORMANCE.md:156-165) stays the gate, and
every item gains a GPU run:

1. **tools/test_gs_raster_reference.py** (906 conformance tests in strict mode,
   plus parts A-G). A GPU backend draws every capture packet. Each colour and Z
   word must equal the CPU model's, and therefore the same `OPEN` counts.
2. **tests/gs_world_test.c** (1, 2, 3 and 8 workers equal 1 worker; DOF
   barriers; early parse; every block uploaded). Add "GPU equals 1 worker" for
   each frame.
3. **tools/test_dof_pass_reference.py**: the 8 captures of the original's
   depth-of-field pass (field, copy buffer, Z, every other word), drawn by the
   GPU. This is the strongest external oracle for stage 2.
4. **tools/test_gs_fog_conformance.py** and **tools/test_gs_memory_reference.py**.
5. **The random differential that hashes all 4 MiB.** This is the `sweep.c`
   pattern used for every medium item: build/perf_investigation/{q3,q4,m1,zt,m3,m2,m4}/sweep.
   Random states and primitives over every format, wrap, CLUT, blend, test and
   scissor (PERFORMANCE.md:1022), compared as 4,500 hashes of the 4 MiB local
   memory plus drawn and refused counts. Add a GPU backend so each batch runs on
   CPU 1-band, CPU 3/8-band and GPU, all equal. It needs negative controls: each
   stage plants a few single-change mutants in the GPU kernels (as M2-M4 did, for
   example PERFORMANCE.md:247) and the sweep must catch every one.
6. **A dual-run mode, GPU against CPU per frame (new; developer switch, for
   example `EM_GS_GPU_CHECK=1`).**
   - Each frame is drawn by the GPU into the live buffer and by the CPU workers
     into a shadow 4 MiB copy, from equal memory.
   - After both finish, a full `memcmp` of 4 MiB, plus the drawn-pixel count.
   - A difference stops the game, in the existing fail-stop style. It dumps:
     - the frame's recorded stream (disc-derived, so it goes under `build/`
       only);
     - both memories;
     - the first differing word, decoded to buffer, pixel, span and primitive.
   - A hash mode keeps long runs cheap.
   - The level smokes (tools/test_level_smoke.py, test_level_smoke_area01.py)
     and the demo replay run with it on, at every stage.
   - This mirrors `EM_GS_SPEC_CHECK=1`, which checked 1.12 billion live
     primitives for M2 (PERFORMANCE.md:232).
   - *(review)* The shadow run needs its own set of worker EmGs bound to the
     shadow memory, each with its own decoded-texture cache, stamped from the
     same EmGsList (em_gs_list_draw / em_gs_list_pass, em_gs_raster.c:3971,
     :4021). The parse runs once. Its CLUT reads come from the live memory,
     which equals the shadow's at the frame's start because the previous
     frame matched. A frame with an own-span CLUT load (own_drain,
     em_gs_world.c:570) goes to the CPU in both arms.
7. **The fb2 pixel harness** (tools/test_fb2_pixels.py, GS_EXACT.md 10.3/10.4)
   with the GPU path. Every point's count of exact pixels must equal the CPU
   path's, not just pass the floor.
8. **Arithmetic unit proofs** (sections 4 and 7). The spike's CPU and GPU test
   programs become a permanent test, `make test-gs-gpu-arith`. It runs:
   - every formulation against the copied model expressions, on random,
     adversarial and exhaustive domains;
   - negative controls (for example, the midpoint test removed);
   - *(review)* the 15 exhaustive stq_div slices **on the GPU too**. The CPU
     slices prove the algorithm as clang compiled it, not as the Metal
     compiler did. Each slice is 4.3e9 cases, seconds of GPU time;
   - *(review)* a forced slow path on the GPU: the estimate perturbed by
     +-1..+-8 ulp and by garbage values. The spike never took the slow path
     (gpu_run_1e7.txt: "slow divides 0" on every line), so fx_rnq_slow has
     never run under Metal;
   - *(review)* the C build of the header under `-fsanitize=undefined`, and a
     lint that fails the build if a kernel source uses `float` or `half`
     anywhere except the one corrected estimate.
9. **A start-up self-test on each device.** A small fixed vector set of the
   kernels' arithmetic and a few packets, compared with values the CPU model
   computes at start-up. If it fails, the GPU path is disabled and the CPU model
   draws. This covers drivers and future GPUs that no one tested.

What is **not** claimed:
- The certified fast path's error bound (section 7, FX-C) is a hand-written
  argument (spike/fx.h, near :273). Measurement and negative controls check it,
  but it is not machine-checked.
- Random testing is not a proof. The exhaustive slices are proofs on their
  slice: 15 slices of stq_div, which are all 2^32 S words for 10 (Q, size)
  pairs and all 2^32 Q words for 5 S words (spike/cpu_run_final.txt). They
  ran on the CPU only.

---

## 4. What the research pass found

There were five lenses: exactness, Metal, architecture, energy, and the spike.
Every measurement below was made on this M1 Pro, headless.

### 4.1 The CPU model is mostly integer; the float steps are few and known

PERFORMANCE.md:264 says the pixel pipeline is integer arithmetic. That is close
but not true, and the doc should be corrected.

**Integer steps.** These are exact on any GPU with 32- and 64-bit integers:
- coverage and edges (int64, em_gs_raster.c:2341-2355, 2421-2445);
- colour and fog block DDA (rowstart128 :2320-2324, step512 :2325, lane128
  :2330, per pixel :2466-2476);
- UV coordinates (floordiv, :2479-2482; sprites :2153-2154);
- texture fetch and swizzle (the M1 tables, :290-300);
- CLUT, wrap (:891-909), bilinear (:1471), TFX, fog, alpha test, DATE, Z test,
  blend, dither, COLCLAMP and the masks (plot_dst :1555, plot_src :1620).

**The "128-bit" divisions fit int64 on the GS domain.** For example,
|nx*16*512| < 2^31 * 2^13 < 2^63 (exactness lens).

**Float steps.** Per pixel:
- **Triangle Z.** `zz = zrow + (x - xd) * zstep`, then floor, in binary64
  (:2460-2461).
- **Triangle STQ.** w1 and w2 as binary64 quotients, w0 = 1 - w1 - w2, three
  binary64 dot products rounded to binary32, then stq_div (:2486-2493). The
  specialized loop repeats the same expression at :3028ff.

Per primitive, per row or per column:
- the binary32 `inv_area` and the binary64 zgx, zgy, zstep (:2380-2382);
- zrow per row (:2452);
- the STQ vertex grid (:49-56);
- sprite STQ per row and per column (:2146, :2156);
- the UV sprite's binary32 V accumulation, sequential per row (:2122-2131,
  :2139);
- stq_div's binary32 quotient (:62-72).
- *(review)* a point's STQ, in binary64 (draw_point :2188; stage 5);
- *(review)* the strict-mode check of a UV sprite's coordinates, in binary32
  (uv_start, sprite_uv_settled :1900-1934). It runs at setup in the CPU
  parse and refuses there, so it stays on the CPU.

*(review)* The specialized loops repeat these expressions (sprites
:2770-2830, triangles :2908-3033). Those are the only float sites in the
file (a grep for `double`, `float`, `floorf`, `ldexp` and `frexp`).

### 4.2 Metal facts that constrain the design

**Language and specification.** MSL 4.1,
https://developer.apple.com/metal/Metal-Shading-Language-Specification.pdf:
- **No double.** Metal has no fp64 type (section 2.1).
- **Subnormals.** They "may be flushed to zero" (section 8.1).
- **Rounding.** Round-to-nearest-even or round-toward-zero may be supported
  (section 8.2). Apple DTS confirms there is no API to query or select the
  rounding mode or denormal handling:
  https://developer.apple.com/forums/thread/844598
- **Fast math is the default.** `fastMathEnabled` defaults to true
  (https://developer.apple.com/documentation/metal/mtlcompileoptions/fastmathenabled).
  On this SDK, a new MTLCompileOptions reports `mathMode` 2, which is fast
  (metal_probe/run_1e7.txt:6). See also
  https://developer.apple.com/forums/thread/759550.
  - Measured: fast `a/b` differs from IEEE on about 31 % of normal quotients, by
    up to 2 ulp (metal_probe/run_1e7.txt).
  - Measured: fast `sqrt` and fast `1/y` are also off (exp1_fp_probe/run.log:8).
- **Safe math still contracts a*b+c within one statement.** Only
  `#pragma METAL fp contract(off)` or an offline build with `-ffp-contract=off`
  stopped it. No MTLCompileOptions setting tested stops it: a run-time
  compile with `mathMode` safe still fused all 1,000,000 cases
  (metal_probe/run_1e7.txt:101).
  - Spec: MSL p.16.
  - API: https://developer.apple.com/documentation/metal/mtlcompileoptions
    (`mathMode`, `mathFloatingPointFunctions`).
  - Measured: metal_probe (a') table (run_1e7.txt:84-104).
- *(review)* **Hazard tracking.** Metal tracks hazards on tracked resources
  only for work sent to an `MTLCommandQueue`. It "doesn't apply hazard
  tracking to commands you submit to an MTL4CommandQueue"
  (https://developer.apple.com/documentation/metal/mtlhazardtrackingmode).
  The port uses `MTLCommandQueue` (em_gfx_metal.m:987, :1012). A move to the
  Metal 4 command API would make every inter-dispatch barrier the port's
  own job (section 8.2).

**Measured on this M1 Pro:**
- Subnormal operands or results of mul, div and fma are flushed to zero in
  **every** math mode.
- Normal add, mul and precise divide are round-to-nearest-even
  (exp1_fp_probe/run.log:3-21).

**Device.**
- The M1 series is Apple7; the A19 series is Apple10.
- 64-bit integer math from Apple3; 64-bit atomics from Apple9 (so not on the
  M1); raster order groups from Apple4; 32 KB threadgroup memory.
- Sources: https://developer.apple.com/metal/feature-sets (text copy:
  apple_docs/feature_tables.txt).
- Measured: threadExecutionWidth 32; hasUnifiedMemory 1.

**The port today** compiles its MSL at run time with `options:nil`, which means
fast math (em_gfx_metal.m:879-881, :2605-2606). The GS kernels must not follow
that pattern (section 8.4).

**64-bit integers and emulated 128-bit division** gave 0 mismatches in 10^7
cases per operation (metal_probe (b)).

**Cost floors:**
- A command buffer takes about 0.23-0.56 ms from commit to completion
  (metal_probe/run_1e7.txt:144-175).
- A 512x224 pass costs 0.05 ns a pixel for a trivial write and 0.09 ns for a
  Z test plus blend, on the whole GPU (lines 156-157).

### 4.3 The spike: exact fp64-free formulations, and their cost (stage 0)

The spike copied (not linked) the model's expressions into spike/ref_model.h,
built with `-ffp-contract=off` as the port's Makefile:27 does. It tested
integer-only formulations in one header shared by C and MSL (spike/fx.h). They
use u32/u64/i64, `mulhi(ulong)` and `clz`, with no double and no 128-bit type on
the GPU.

| Formulation | What it is | CPU result (spike/cpu_run_final.txt) | GPU result (spike/gpu_run_1e7.txt) | GPU ns a pixel, whole M1 Pro |
|---|---|---|---|---|
| **FX-E** | Exact integer emulation of the model's binary64 STQ chain (unpacked significands, per-triangle reciprocal plus exact remainder correction) | 0 of 1.0e8 pixels differ (S/T/Q words and U/V) | 0 of 2x1e7 | 1.74-1.83 |
| **FX-C** | Certified fixed point: X = c_row + k*b in int64 per channel; a written error bound decides when the model's binary32 rounding is settled, and FX-E handles the rest | 0 differ; fallback 0.01 % of realistic channels (perspective 0.0102 %, affine 0.0091 %), up to 0.42 % on random words | 0 of 2x1e7 | **0.39-0.45** |
| FX-P | FX-C without the per-row table | 0 differ | 0 of 2x1e7 | 0.87-0.93 |
| fx_stq_div | A hardware binary32 estimate on normalized significands, corrected by an exact integer midpoint test (a long-division slow path is never taken) | 0 of 1e8 random cases, and **all 2^32 S words for 10 (Q, size) pairs and all 2^32 Q words for 5 S words** | 0 of 1e7 direct | (inside the above) |
| fx_zpix | Triangle Z: floor(RN64(zrow + RN64(k*zstep))), exact for k <= 2047 | 0 of 1e8 | 0 of 1e7 direct | 0.16-0.18 |
| k_rows | Per-row exact row starts (the c_row of FX-C) | equal to the CPU's int128 on all 3.98M rows | 0 differ | 0.53-0.57 ns a row |
| float-float (double-binary32) | A cheaper idea | **REFUTED: 10,448 of 1.0e8 differ** | - | - |

**Other exhaustive slices on the CPU:**
- every covered (A, N1, N2) with |A| <= 256: 22.9M chains;
- every triangle on a 9x9 half-pixel lattice: 785,448 pixels;
- RN64(N/A) for |A| <= 4096.

All gave 0 differences.

**Fast math does not matter here.** The GPU results are equal with the strict
library (`-fno-fast-math -ffp-contract=off`) and with the default fast-math
library. The only hardware float in FX is an estimate that exact integer
arithmetic then corrects, so the design does not depend on the GPU's rounding
mode or denormal handling.

*(review)* This holds for the spike's FX kernels only. metal_probe's STQ
pixel uses a hardware binary32 divide, and under the default (fast) library
14 of 114,688 pixels differ from the model (metal_probe/run_1e7.txt:163);
under the safe library none differ (:151, :175). So any kernel code that
keeps a hardware float result needs a proof of its own; the strict build
rule of section 7 is not optional.

**For comparison:**
- The model's own double STQ chain costs 8.6 ns a pixel on one CPU core
  (spike bench).
- Earlier probes, superseded by the spike:
  - exp1's soft binary64: 3.1 ns;
  - exp2's certified rational path: 4.5 ns, or 3.1 ns with the hardware
    divide;
  - exp2's naive soft double: 10.5 ns.

**Caveat on the spike's timing.** It used 1e7-pixel coherent spans. A real frame
has small triangles, divergent lanes, texture reads and Z/frame traffic, so
these figures are a floor for the arithmetic, not a frame cost. exp2's kernels
were also limited by register pressure: maxThreads was 384-448
(exp2_softfloat/run.log). Occupancy has to be watched in stage 4.

**Stage 0 verdict: GO on arithmetic.** The hardest per-pixel step, the
perspective-texture triangle, costs well under 1 ns a pixel exactly. At the
level's 1.47M pixels a frame (PERFORMANCE.md:22), even if every pixel were an
STQ triangle, FX-C would be about 0.6 ms of GPU time a frame. Arithmetic is no
longer the main risk. Ordering, memory and the energy gate are.

---

## 5. Options considered

| Option | Idea | For | Against | Verdict |
|---|---|---|---|---|
| **A. Tile-ordered compute** | One threadgroup owns a screen tile and walks the frame's primitive list in order, one thread a pixel. Barriers become dispatch boundaries. | Per-pixel order without atomics or raster-order features. Deterministic. Integer-only. The same kernel maps to D3D12 and Vulkan compute. It follows the exactness argument the CPU bands already use (GS_EXACT.md section 9: each pixel takes its writes in list order). | Divergence on small primitives. Needs ordered per-tile binning. One dispatch per segment. | **Recommended** |
| B. Render pipeline plus raster order groups | Draw each primitive's bounding quad. The fragment shader does the exact integer coverage test and discards. ROG orders fragments at each (x,y) in submission order (MSL p.137). | Hardware binning (TBDR). ROVs exist on D3D12, and fragment shader interlock on Vulkan (VK_EXT_fragment_shader_interlock). | Ordering of device-memory (buffer) writes under ROG, and its cost on Apple, are unmeasured. Interlock is optional and uneven on Vulkan. Semantics differ per API. The existing pipelines needed framebuffer fetch, which the iOS simulator lacks (docs/IOS.md "Simulator"). | Keep as a later experiment only if A's binning costs too much |
| C. Per-primitive compute with atomics | Each primitive is a dispatch, or pixels resolve order by atomics. | Simple kernels. | Thousands of dispatches a frame, or 64-bit atomics (not on Apple7/M1), and order is hard to keep exact. | Rejected |
| D. A cheaper arithmetic shared by CPU and GPU | Change the CPU model's binary64 sequences (a modelling choice; GS_EXACT 8.1/8.2 show the reference does neither exactly) to an exact rational form. | Simpler kernels. | Changes the Original profile's reference. It needs the full proof set to come out with unchanged counts, and a user decision. The spike made it unnecessary for speed. | Not now. Open user decision (section 15) |
| E. Keep improving the CPU (L1 NEON) | — | — | The user declined it (2026-10-15). It does not cut energy as much as moving work to the GPU could, if section 12 confirms the GPU saves energy. | Out of scope |

---

## 6. Recommended architecture

### 6.1 Division of labour

**The CPU keeps:**
- The **parse**, unchanged: one EmGs takes every write, does the vertex queue,
  spans, CLUT loads and the STQ grid decision, and appends spans to the
  `EmGsList` (em_gs_raster.c:3706-3726; GS_EXACT.md section 9).
  - It costs 1.5-1.9 ms a frame today and can already run early on the game
    thread (`em_gs_world_preparse`, em_gs_world.h:150-160).
- The **recorder**, unchanged: residency, barriers and the field declaration
  (em_gs_world.c:1525-1610).
- **Strict mode, refusals and faults**, unchanged.
- **Uploads**: em_gs_world_upload_chain writes local memory on the CPU after a
  wait.
- **Per-primitive setup.** At first this is computed by the CPU model's own code
  (planes, step512, lane128, inv_area, zgx/zgy/zstep, the STQ grid, the sprite
  UV start and step), so it is exact by construction. It moves to a GPU setup
  kernel only if measurement says so (stage 4); that kernel would be proven
  against the CPU code by a differential.
- **Small sequential tables**: the UV sprite's V accumulation per row
  (em_gs_raster.c:2129-2144, no closed form). These are a few hundred entries a
  frame, computed by the model's own expression.

**The GPU does:**
- per-row data;
- per-pixel coverage, attributes, texture sampling, the tests, the blend and the
  writes, straight into the 4 MiB local memory;
- the field's de-swizzle for presentation.

**The CPU model stays for:**
1. **The reference.** The dual-run mode, the sweeps and the conformance tests
   compare against it. The GPU is never its own reference.
2. **Fallback.** Any span with a state the GPU kernels do not support yet, or a
   frame whose ordering the GPU path cannot express (section 8.3), is drawn by
   the CPU workers in the same shared memory.
3. **Hosts without a suitable GPU**, a failed start-up self-test, or a user
   switch (EM_GS_GPU=0).
4. **The readable specification** that GS_EXACT.md documents. Any later change
   to a rule (for example a fix to an open item in GS_EXACT section 8) is made
   in the CPU model first. The GPU kernels follow, and the dual-run check and
   sweep catch any drift.

### 6.2 Data flow per frame

1. **Parse on the CPU** (unchanged). The result is the EmGsList (spans, their
   primitives with the Draw state, CLUT snapshots, DIMX, row ranges) and the
   recorder's segments (the spans between barriers).
2. **Route.** Each span is marked GPU or CPU. *(review)* The mark is a pure
   function of three things:
   - whether every primitive's Draw state is in the GPU kernels' supported set,
     like `spec_select` (em_gs_raster.c:3071);
   - the span's own write pages: the frame, and the Z buffer when Z is
     written, as the list records them (em_gs_raster.c:969-977). A span whose
     texture pages meet its own write pages is a self-feedback span (a CLUT
     load from drawn pages is the parse's own_drain case, not this one),
     and it goes to the CPU, drawn by one EmGs (section 8.3);
   - the alias check of section 8.3.

   Segments already drawn by own_drain (`Parse.predrawn`, em_gs_world.c:79,
   :570-605) are CPU work by definition.
   - Runs of consecutive spans with the same route are executed in list order:
     CPU runs by the workers, GPU runs by dispatches.
   - Splitting a segment at a span boundary into sequential parts never changes
     bytes. It is the same as adding a barrier, and barriers only order work
     (one EmGs draws what the bands draw: raster part F, gs_world_test).
   - Each CPU/GPU switch costs one GPU round trip (about 0.25-0.6 ms of latency,
     not CPU). In a switch the CPU waits for the GPU, or the GPU for the CPU,
     so the parts run in series.
   - *(review)* **Suffix rule for the live route.** A frame has 610-796 spans on
     average (p95 1,530, max 2,353) and 10-12 segments
     (build/perf_investigation/m4/lc_demo3.txt and lc_a013.txt, summarised in
     this review). A state-class router would interleave GPU and CPU spans
     hundreds of times a frame, and "a few switches, else all CPU" would
     route almost nothing. So the live route uses a CPU prefix and a GPU
     suffix, with at most one switch. The GPU takes the longest suffix of the
     frame (from a span boundary) whose spans are all GPU-routable. The
     depth-of-field pass is the body's last CALL (GS_EXACT.md:1232), so its
     suffix comes first.
   - More switches a frame is a later, measured option. A frame with at most
     N switches would be routed only after stage 0.5's stream measurement
     (section 13) shows what each extra switch buys.
   - *(review)* **The CPU workers' texture caches see GPU spans.** Every
     worker's decoded-texture cache must be stamped with the pages of every
     span the GPU drew, as a worker stamps the spans it skips
     (em_gs_list_pass, em_gs_raster.c:4021; em_gs_world.c:392-395). Without
     that stamp, a CPU prefix in the *next* frame could sample a stale decoded
     copy of a buffer the GPU drew. Each stage has a negative control: the
     stamp is dropped, and the sweep or gs_world_test must catch it.
   - This router is what makes the stages below incremental.
3. **Pack.** The GPU runs' primitives are packed into one shared buffer per
   frame: the vertex data and the CPU setup values, about 100-300 bytes a
   primitive. CLUT snapshots go into another (1 KiB each).
4. **Encode** one compute encoder per frame, with
   [serial dispatch](https://developer.apple.com/documentation/metal/mtldispatchtype/serial).
   Each GPU run in each segment is one dispatch over the tiles its primitives
   reach. The field de-swizzle dispatch comes last (section 9).
5. **Commit** at the kick; the next kick waits for it. This is the existing
   pipelining contract: em_gs_world_kick / em_gs_world_wait (em_gs_world.h:147,
   :170) and GS_EXACT.md section 9 "Pipelining".
   - *(review)* Today the parse need not be finished at the kick. A worker's
     parse runs on while the bands draw behind it (parse_ops, em_gs_world.c:485-540).
     The early parse stops once the previous frame is drawn (early_parse, :607-628), which
     will be early when the GPU is fast. The suffix route needs the parse to
     reach the frame's end before it can choose the suffix. So one CPU thread
     parses, draws the CPU prefix with the workers, then encodes and commits
     the suffix. The fault reporting is unchanged: a failed parse is
     reported by the same em_gs_world_wait call, and the GPU never dispatches
     a frame whose parse failed.

### 6.3 The tile kernel

- **Tile size: 16x16 pixels**, aligned to the 8x8-pixel blocks of PSMCT32 and
  PSMZ32. A tile's frame and Z words are then whole 256-byte blocks. The 512x224
  field is 32x14 = 448 tiles, about 28 per GPU core: enough to fill the M1 Pro.
  Stage 1 measures 16x16 against 32x8.
- **Ordered binning inside the tile.** No global binning pass. The threadgroup
  walks the run's primitives in chunks of 256. Each thread tests one primitive's
  bounding box against the tile, and a threadgroup prefix sum compacts the hits
  into a tile-local list **in list order**. The cost is about tiles x primitives
  / 256 box tests. *(review, measured)* A frame has 10,742 primitives on
  average in the demo (p95 21,057, max 23,259) and 13,420 on the AREA01
  route (max 23,405) (m4/lc_demo3.txt, lc_a013.txt, column `prims`). At 448
  tiles that is about 4.8-6.0M tests a frame on average and 10.5M at the
  maximum. (The earlier "about 12,500" came from PERFORMANCE.md:710's 18M
  primitives, which cover the demo only up to counter 1780.) If that
  measures too high, a coarse pre-bin by the row ranges the parse already
  has (`LsRows`, em_gs_raster.c:3714) cuts it.
- **Per primitive:**
  - The tile's rows are computed cooperatively, one thread a row, into
    threadgroup memory: the edge solve lo/hi, xd/jd, the colour and fog row
    starts, zrow, and FX-C's c_row. This is batched for 16 primitives per
    threadgroup barrier.
  - *(review)* **Every row anchor is the row's own, never the tile's.** The
    colour and fog DDA, zrow, the Z step k = x - xd and FX-C's c_row are all
    anchored at the row's first drawn pixel xd: the first covered pixel
    clipped to the scissor. The 4-pixel blocks are aligned to xr = xd & ~3
    (em_gs_raster.c:2443-2452, :2466-2476). A tile in the middle of a row
    must solve the whole row's lo to get xd. Anchoring at the tile's left edge
    would change colour bytes silently. It is a required negative control in
    stage 4.
  - *(review)* **Row setup is repeated in every tile a row crosses.** A
    triangle row that spans t tiles horizontally has its setup computed t
    times: 3 int64 floor divisions for the edges, 5 rowstart128 divisions,
    a soft-binary64 zrow and 3 c_row values. Stage 4 measures this against
    a global per-row pass, one dispatch that writes the row records to a
    buffer (k_rows already measures 0.53-0.57 ns a row, gpu_run_1e7.txt).
  - *(review)* **Threadgroup memory and occupancy.** 16 primitives x 16 rows
    of row records (about 80 B each) is about 20 KiB of the 32 KB limit
    (apple_docs/feature_tables.txt:232). That may allow only one threadgroup
    per core. Measure occupancy in stage 1 with the real record layout.
  - Then every thread shades its pixel in plot()'s exact order: scissor and
    SCANMSK, Z, frame read, DATE, sample, TFX, fog, alpha test, blend, dither,
    clamp, FBA, FBMSK, frame word, then Z word (em_gs_raster.c:1555-1752).
  - The early-Z order of P12 (attributes only after the Z test) is an
    optimisation with the same bytes, and the GPU may use it too.
- **Ownership.** Each pixel's frame and Z words are written only by the thread
  owning that pixel, so there are no atomics and no races, and results are
  deterministic run to run. This needs the alias rule in section 8.3.
- **Texture reads within a segment** go straight to the GS memory buffer through
  the M1 separable swizzle tables (rowtab[y] + coltab[x], PERFORMANCE.md:991).
  *(review, corrected)* The recorder puts a barrier *before* a primitive that
  reads blocks drawn since the last barrier (texture_read,
  em_gs_world.c:1729-1773). That orders a read against **earlier**
  primitives' draws. It does not separate a span from itself. The CPU model
  explicitly supports a primitive that samples its own frame or Z buffer, or
  what an earlier primitive of its own span drew; it reads memory in raster
  order (em_gs_raster.c:973-977, `tcs.uncached_written` :1412). On the GPU,
  pixel order within one primitive is not defined across threads. Such a
  span is therefore routed to the CPU, drawn by one EmGs (section 6.2). The
  recorder's drawn set comes from FRAME_1 only (mark_drawn and drawn_buffer, em_gs_world.c:1551-1601), so
  the GPU's check uses the list's own write pages, which include Z.
  The CPU's decoded texture copies (M3) are a CPU-side cache with no rule of
  their own (GS_EXACT.md 4.8). The GPU does not use them, but it must keep
  them correct for the CPU's later runs (the stamp in section 6.2).
- **Optional staging.** For a run whose FRAME and ZBUF are constant (one span,
  or adjacent spans with the same buffers), the tile's frame and Z blocks can be
  staged in threadgroup memory (2 KiB) and written back once. This is a later
  optimisation, with its own sweep.

---

## 7. Arithmetic plan per pipeline step

"Proven" means the spike's or a probe's 0-difference results on the stated
domain, plus exhaustive slices where listed. "Structural" means that integer
code ported from the model's integer code is exact by construction, and the
sweep must still show it.

| Step | CPU source | Type | GPU plan | Status |
|---|---|---|---|---|
| XYOFFSET, area A, edges kx/ky/c | em_gs_raster.c:1872-1873, :2341-2355 | int32/int64 | same integers | structural |
| Row range, scissor clip | :2361-2365 | int | same | structural |
| Edge solve per row (lo, hi, xd, jd) | :2421-2445 | int64 floordiv | per row in-tile (one thread a row) | structural; 64-bit divide is slow on the GPU (metal_probe (b)), so a per-row cost |
| Colour and fog planes, step512, lane128 | :2301-2334 | int64 | CPU setup values passed in | exact by construction |
| Colour and fog row start (rowstart128) | :2320-2324, :2448-2451 | int64, num*128 < 2^50 | per row on the GPU, int64 floordiv | k_rows-style: proven equal to int128 on 3.98M rows |
| Colour and fog per pixel | :2466-2476 | int64 sum truncated to int, then clamped | 32-bit wrapping arithmetic on the low words (identical, because the C code truncates to int before the clamp) | structural (exactness lens) |
| Z setup (inv_area, zgx, zgy, zstep) | :2380-2382 | binary32 / binary64 | CPU setup values passed in as binary64 words | exact by construction |
| Z row start zrow | :2452 | binary64: two exact subtractions (X/16 has 4 fraction bits), 2 mul and 2 add, left to right | soft binary64 per row (FX unpacked ops), or the CPU per row | **not proven** *(review)*: the soft ops are only randomly tested (exp2: 0 of 2e7 per op; metal_probe (d) 0 of 1e7, which *flags* out-of-range values instead of handling them, run_1e7.txt:130-135), and the spike's GPU took zrow as an input word from the CPU (spike/gpu_types.h GRow.zrow). Name one implementation; test it with a TestFloat-style generator (ties, near-ties, cancellation, exponent extremes, zero) plus the expression itself, before stage 4 |
| Z per pixel | :2460-2461, clamp :1567-1570 | binary64, floor | fx_zpix | proven: 0 of 1e8 CPU, 0 of 1e7 GPU; k <= 2047 is guaranteed by the scissor width |
| STQ vertex grid | :49-56, :2393-2399 | bit operations on binary32 values | CPU setup | exact by construction |
| STQ triangle per pixel | :2485-2493 | binary64 chain, then binary32 | FX-C, with FX-E fallback | proven (section 4.3); strongest result of the pass |
| stq_div | :62-72 | binary32 quotient, scale, floor, clamp | fx_stq_div | proven, with exhaustive 2^32 slices |
| UV triangle (FST 1) per pixel | :2479-2482 | i128 floordiv (fits int64) | per-triangle reciprocal plus remainder correction (FX-E's ud_div_int pattern), or a per-row DDA | not yet tested in this exact form; metal_probe's i128 divisions are exact but cost 1.2-2.8 ns; little use in the level (the census triangles are STQ, PERFORMANCE.md:1331) |
| Sprite UV: U per column | :2153-2154 | int64 floordiv | per column on the GPU, or a CPU table (2048 max) | structural |
| Sprite UV: V per row | :2122-2131, :2144 | sequential binary32 accumulation | **CPU table** from the model's own expression | exact by construction |
| Sprite STQ: per row and per column | :2146-2147, :2156-2157 | binary64, then stq_div | CPU tables (the model's expression), or soft binary64 per row and column | exact by construction |
| Points, lines | :2176-2300 | mixed; a point's STQ is binary64 (:2188) *(review)* | stage 5; CPU fallback until then | — |
| Texture fetch, CLUT, wrap, bilinear | :891-1053, :1461-1508; P14's bilinear rewrite PERFORMANCE.md:1342 | integer | same integers (the plain form or P14's lane form, which are proven equal) | structural |
| TFX, fog, alpha test, DATE, Z test, blend, dither, clamp, masks | :1555-1752 | integer | same | structural |
| CT16 frames, Z16 | :340-370, :1616-1712 | 16-bit halves | stage 5; needs whole-word writes per thread (section 8.3) | not used in the first level (census, PERFORMANCE.md:1330) |

*(review)* **Bounds for every int128 site.** The claim that the 128-bit
divisions fit int64 (section 4.1) needs a written bound for each site, from
the input widths. RGBA and F are 8 bits, U and V 14 bits, Z 32 bits, and the
window X and Y lie in (-2^16, 2^16), so differences are under 2^17:
- rowstart128: |num * 128| < 2 * 2^26 * 2^17 * 2^7 = 2^51;
- step512 and lane128: |nx * 16 * 512| < 2^26 * 2^13 = 2^39;
- the FST UV numerator: |nu| < 2 * 2^32 * 2^17 = 2^50.

The arithmetic test asserts each bound over its domains. The CPU model keeps
its 128-bit fallback (`fits64`, em_gs_raster.c:29-37), so a GPU kernel that
assumes int64 must refuse (route to the CPU) any primitive outside the bound
rather than wrap.

**Build rule.**
- Kernels are built offline into a `.metallib` with
  `-fno-fast-math -ffp-contract=off`, and the sources also carry
  `#pragma METAL fp contract(off)`.
- They use no hardware float result unless integer arithmetic corrects it (the
  fx_stq_div estimate).
- This holds even though the spike showed the FX kernels are exact under fast
  math too: it is defence in depth.

---

## 8. GS memory and synchronization

### 8.1 One shared buffer

**Storage.** The 4 MiB local memory becomes one `MTLBuffer` in shared storage.
On Apple GPUs that is unified memory, the same pages for CPU and GPU:
https://developer.apple.com/documentation/metal/choosing-a-resource-storage-mode-for-apple-gpus.
The EmGsWorld's `mem` points at its contents, so the CPU parse, the CPU workers
(fallback runs), uploads and test reads all use the same bytes with no copies.

**CPU access rules.** The CPU touches the buffer only when no GPU work that
writes it is in flight. That is already the model's contract:
- uploads wait for the workers (em_gs_world.h:73-80);
- reads come after `em_gs_world_wait`.

The wait becomes "the frame's command buffer has completed" (a completion
handler or MTLSharedEvent) plus the CPU runs.

*(review)* **Every wait, not only em_gs_world_wait.** The module has several
places that wait for drawing, and each must count the GPU part:
- em_gs_world_wait (em_gs_world.c:2094), which every reader and the kick
  call;
- prior_wait (:544-556): the early parse's wait, before a CLUT load from a
  page the running frame draws (em_gs_list_prior; list_clut_drawn
  em_gs_raster.c:3797-3822). `w->running` must stay set until the GPU suffix
  has completed;
- the early parse's `live` flag (:607-628), cleared when the running frame
  is drawn. With the GPU it is cleared from the completion handler;
- own_drain (:570-605): it waits for the segments so far. Such a frame is
  CPU-only at first (section 8.2);
- em_gs_world_upload_chain, memory_load and memory_restore, which wait for
  the workers and then drop the workers' decoded copies of the blocks they
  wrote (workers_memory_written :294);
- the field readout (read_rows :334-342) and em_gs_world_read.

A negative control per wait: skip the GPU part of that wait, and
gs_world_test's early-parse and CLUT cases (PERFORMANCE.md:1662) must catch
it.

### 8.2 Ordering inside a frame

**Within a segment.** Pixels are owned by tiles, and textures are read-only
inside the segment (the barrier rule). So nothing crosses threadgroups.

**Between segments, and between GPU runs.** The next dispatch must see the
previous one's writes. *(review, corrected)* Apple's page for the serial
dispatch type says only that the encoder dispatches "encoded commands
serially"; it says nothing about memory visibility
(https://developer.apple.com/documentation/metal/mtldispatchtype/serial).
Visibility comes from hazard tracking on a tracked buffer sent through an
`MTLCommandQueue` (section 4.2). A test cannot prove that a race is absent,
so the design does not rely on a test for it. Between dependent dispatches
the encoder issues an explicit
`memoryBarrier(scope: .buffers)`, which "enforces the order of write and
read operations"
(https://developer.apple.com/documentation/metal/mtlcomputecommandencoder/memorybarrier(scope:)).
Stage 1 still runs the ordering tests: a frame whose second segment reads
what the first drew, as the DOF pass's eight barriers do (GS_EXACT.md
section 9 "The depth-of-field pass and the workers").

**CPU runs between GPU runs.** CPU runs are separate submissions: commit the GPU
part, wait, run the CPU workers, then encode the next GPU part. That is correct
but has a cost, so the router limits switches (section 6.2).

**CLUT loads.** The parser reads memory only for CLUT loads (GS_EXACT.md
section 9). A load from a page the frame itself draws (`own_drain`,
em_gs_world.c:570) needs the frame drawn up to that point, which is a CPU wait
on the GPU in mid-frame.
- The first level has none (P17, PERFORMANCE.md "Follow-up").
- Such a frame is drawn wholly by the CPU at first.
- Later, a GPU CLUT-load kernel at that point in the stream would remove the
  wait.

### 8.3 Aliasing

Tile ownership is exact only if no two pixels in different tiles write the same
32-bit word within one segment.

**Within one buffer** this holds for 32-bit formats: one pixel to one word.
16-bit formats (CT16, CT16S, Z16, Z16S) put two pixels in one word. *(review)*
There are no 8- or 4-bit drawing targets: the model's frame path is 32- or
16-bit only (plot_dst, em_gs_raster.c:1590-1599). 8- and 4-bit data reach
a target only through aliasing (section 8.3, across buffers).
With block-aligned tiles those pixels are in the same tile, but in different
threads. So those formats need writes of whole words: each thread owns a word,
or the tile is staged in threadgroup memory. These formats are stage 5 work.

**Across buffers.** Two buffers drawn in one segment (two FRAME_1 values, or a
frame and a Z buffer) could alias the same blocks with different coordinates.
The GPU path adds a per-segment check on the blocks each GPU run writes. If two
different targets overlap, the run is split at that span, or goes to the CPU.

Two more cases go to the CPU at the span level (section 6.2):
- a primitive whose own FRAME and ZBUF overlap. sprite_fill and plot write
  the frame word, then the Z word, so the two may land in different pixels'
  words (em_gs_raster.c:1984-1986);
- a self-feedback span (section 6.3).

**Open question for the CPU path.** The row bands carry the same theoretical
hazard if two targets alias with different row mappings. *(review)* They also
carry it for a self-feedback span, because bands of one primitive run
concurrently. The random sweeps draw their bands "in lockstep" (PERFORMANCE.md:1023),
which hides this. I found no write-alias or self-read check in the
recorder (em_gs_world.c:1534-1773 handles drawn-against-read between
primitives). Whether these cases are impossible by construction should be
checked. This is not claimed as a bug.

*(review, corrected)* The first level is **not** alias-free. The DOF copy
buffer is the drop shadow's 128x128 target memory too (GS_EXACT.md:1455-1457):
FBP 0x12C at FBW 4 (256x256) and at FBW 2 (128x128) (tests/gs_world_test.c:418-419),
so the same blocks hold different pixel coordinates. The two are drawn in different segments, with a barrier
between them, so tile ownership holds. The earlier text cited "GS_EXACT
section 9: the fields, the Z buffer and the copy buffer are disjoint"; no such
statement exists there. Stage 0.5 therefore runs the alias and self-feedback
checks over the recorded streams of the demo and the AREA01 route, and
records the counts (expected: 0 same-segment cases).

### 8.4 The frame recorder and the bench recorder

**The world recorder** (em_gs_world.c `push`, `barrier_op` :1441-1545) is
unchanged. Its op list, segments and the EmGsList are the GPU path's input.

**A bench stream recorder** (new, recommended first; the energy lens). It
records each frame's calls at the em_gs_world_* entry points (em_gs_world.h:
upload_chain, begin, write, prims, gif, env_again, field_declare, page_pass,
kick, memory_restore) to a file under `build/`. *(review)* The list must also
hold the entry points the live backend calls (em_gfx_metal.m):
- `drawn_buffer` (:4562): it places barriers and marks drawn blocks, and
  without it a replay faults on residency;
- `list_frame` (:4720): the load veil's frames;
- `memory_load` (:4307): the boot image;
- `preparse` (:4359): its timing is free, but its head must match the
  kick's. The streams are disc-derived and
are never committed. A replayer feeds the same frames to:
- the CPU model, at N workers;
- the GPU model;
- both (the dual-run check).

The replayer checks equal memory per frame, and it can loop one scene for 60
seconds or more with no game thread in the measurement. This is the A/B bench
for both time and energy (section 12).

---

## 9. Presentation

**Today.** After `em_gs_world_wait`, the field's RGBA rows are copied from CPU
memory into the slot texture with `replaceRegion` (em_gfx_metal.m:4860-4868),
about 458 KB a field. A frame that draws nothing is checked by reading memory
back (em_gfx_metal.m:5046-5055).

**With the GPU model:**
- The field is de-swizzled by a small kernel at the end of the frame's compute
  work, straight into the slot texture: the same bytes as em_gs_world_field,
  through the same address tables.
- *(review, corrected)* A CPU copy of the field remains, unless the scan
  buffers are redesigned. After each presented field, the backend copies its
  bytes into the scan buffer (em_gfx_metal.m:4875-4879). The no-draw frame
  check compares against that copy (:5046-5055), and the present log hashes
  it (:4880-4885). em_gfx_gs_field_read (:4371-4380) also returns it. So
  either the CPU still reads the field from the shared buffer after
  completion (read_rows' de-swizzle, about 458 KB a frame), or the GPU
  kernel also writes a linear copy into a shared buffer that these readers
  take. The second keeps the bytes identical and costs a memcpy.
- The existing rotation of three slot textures and the one-tick display delay
  are unchanged (GS_EXACT.md section 9 "Pipelining").
- The field presentation rules are unchanged: the interlaced line doubling and
  the picture placed by the display registers (GS_EXACT.md section 11). So is
  the 2D overlay drawn over the field.
- The no-draw-frame check reads the shared buffer after completion, as it reads
  CPU memory now. It is rare, so no new mechanism is needed.
- In the dual-run mode, the presented field is also compared byte for byte with
  em_gs_world_field's CPU result.

The Original profile's picture is still exactly the 512x448 field shown at 4:3
with no smoothing (PORT_PROFILES.md). Presentation work never touches the
field's bytes.

---

## 10. iOS specifics (iPhone Air, A19 Pro)

- **Same kernels, same metallib build**, targeting iOS.
  - The A19 is Apple10: it has every feature the design needs (64-bit
    integers, 32 KB threadgroup memory) and 64-bit atomics the design does not
    use (apple_docs/feature_tables.txt).
  - The start-up self-test (section 3, item 9) runs on the device.
- **Why it matters most here.** The phone has 2 performance and 4 efficiency
  cores. The CPU GS uses 4 workers there and depends on efficiency-core speed
  and sustained clocks (PERFORMANCE.md section 5). A GPU GS removes the worker
  load, which is the case where battery and thermal gains should be largest.
  Not measured on the device.
- **Background.** iOS does not allow GPU work while the app is in the
  background. The kick and wait path must handle a command buffer that fails
  with that error: fail over to the CPU model, or pause with the game. The
  port's existing Metal presentation already lives under this rule (docs/IOS.md
  "Renderer"); the compute path must be checked against it. *(review)*
  Apple's article: after the app moves to the background, "if Metal sees a
  new command buffer from your app, it returns an error, rather than
  scheduling the command buffer"; work already committed should be forced to
  schedule before the app returns control
  (https://developer.apple.com/documentation/metal/preparing-your-metal-app-to-run-in-the-background).
  The port parks the game thread at the top of its next frame
  (docs/IOS.md:162-166).
- *(review)* **Failover must be exact.** "Fail over to the CPU model" is
  exact only if the failed command buffer wrote nothing. A buffer that ends
  in an error (background, timeout, page fault) may have run in part. So a
  frame's GPU suffix starts from a 4 MiB snapshot of local memory taken
  before its commit. On an error, the CPU restores the snapshot and draws
  the suffix itself. Otherwise the frame fail-stops. The snapshot is a
  memcpy, done only on the iPhone or behind a switch. Whether it is needed
  on macOS is a user decision (section 15).
- **Measuring.** powermetrics and IOReport are not available on iOS. Candidates,
  none verified in this pass:
  - *(review)* Instruments' Power Profiler on the device, recorded with the
    phone connected or "on the go" (WWDC25 session 226,
    https://developer.apple.com/videos/play/wwdc2025/226). Whether its CPU and
    GPU "power impact" lanes are calibrated energy or a relative score must
    be checked before a gate relies on them.
  - MetricKit's `MXCPUMetric` and `MXGPUMetric` (cumulative CPU and GPU time):
    https://developer.apple.com/documentation/metrickit. *(review)* It is not
    usable for A/B runs. The system "delivers metric reports at most once per
    day per metric source", covering the past 24 hours
    (https://developer.apple.com/documentation/metrickit/mxmetricmanager).
  - Xcode Instruments' energy and GPU tools, on a connected device.
  - A 10-15 minute sustained run for thermals (PERFORMANCE.md section 7,
    item 2).
- **Simulator.** The world cannot be shown there today because the GS frame's
  render pipelines need framebuffer fetch (docs/IOS.md "Simulator"). A
  compute-only GS path might not have that dependency. That is unverified and
  not a fidelity target.
- **Memory.** One 4 MiB buffer plus a per-frame primitive buffer, against the
  CPU path's decoded copies of up to 10.2 MiB per worker (PERFORMANCE.md
  section 5, P13). The GPU path should use less memory.

---

## 11. Vulkan and D3D12 portability

**Today.** The Original profile's field exists only on Metal. The d3d12 and
Vulkan backends refuse it (GS_EXACT.md section 9 "Backends";
src/gfx/d3d12/em_gfx_d3d12.c, src/gfx/vulkan/em_gfx_vk.c). One-worker hosts run
at 49-78 % speed (PERFORMANCE.md P16 "Reading"). So a portable exact GPU GS
would also be the route to an exact Original profile at full speed on Windows
and Linux.

**Design choices that keep it portable:**
- Compute only, integer only. There is no reliance on rounding modes, denormal
  behaviour, contraction, raster order or fragment interlock.
- **64-bit integers in shaders** are an optional feature on both APIs: Vulkan
  `shaderInt64`, and D3D12's `Int64ShaderOps` capability (Shader Model 6.0+).
  Both are widely supported on desktop GPUs. If a device lacks them, the kernels
  need a 32x32 emulation path, or that host falls back to the CPU model. Not
  verified against device databases in this pass.
- **`mulhi` on 64 bits** has no HLSL or GLSL intrinsic. The shared header
  provides it from 32-bit pieces. Cost is unmeasured off Apple.
- **One kernel source.** The spike already uses one header for C and MSL
  (spike/fx.h). Extend it with a macro layer for HLSL (DXC builds DXIL and
  SPIR-V). The C build of the same header is what the CPU arithmetic test runs,
  so all backends share one tested source.
- **Memory.** Discrete GPUs have no unified memory. Uploads and CPU fallback
  runs then need explicit copies of the touched blocks, or the whole 4 MiB,
  which is small. Measure there.
- **Ordering.** Barriers become UAV barriers (D3D12) or pipeline barriers
  (Vulkan) between dispatches.

**Effort.** This is a later stage, after Metal stage 5. *(review)* It is not
estimated here. The earlier "3-6 weeks per API, including the field
presentation those backends lack" had no basis. Each API needs:
- the Original field presentation, which those backends refuse today
  (GS_EXACT.md:1438);
- non-unified memory and its copies;
- 64-bit mulhi built from 32-bit pieces;
- device testing on the user's Windows and Linux machines.

Re-estimate after stage 4, from the Metal stages' actuals.

---

## 12. Energy and time: measurement protocol and today's baselines

### 12.1 Meters (tested on this M1 Pro, macOS 27.2; energy_probe/)

| Meter | Needs sudo? | Status |
|---|---|---|
| Per-process CPU energy: `proc_pid_rusage` RUSAGE_INFO_V6 `ri_energy_nj` / `ri_penergy_nj` (equal to TASK_POWER_INFO_V2) | no | **works** (energy_probe/probe_result.txt, pidenergy.c) |
| System GPU energy: IOReport "GPU Energy" channel | no | **works**: idle 0.45-0.91 W (energy_probe/ioreport_idle.txt). The energy lens reported 16.4 W under full Metal compute load, but no receipt file was saved, so re-measure it in stage 0.5 |
| Per-process GPU time from the kernel | no | reads 0 (probe_result.txt) |
| IOReport CPU energy | no | reads 0 without root |
| MTLCommandBuffer GPUStartTime / GPUEndTime (https://developer.apple.com/documentation/metal/mtlcommandbuffer) | no | works: probe_result.txt sums 0.842 s over 59 buffers |
| `powermetrics --samplers cpu_power,gpu_power` | **yes** | needs the user (section 15) |

*(review)* The probe's own CPU-time column is wrong: it reads 0.012 s for a
1.000 s spin (probe_result.txt:1). probe.m:32 scales `ri_user_time` and
`ri_system_time` as nanoseconds, but on Apple silicon they are mach ticks.
esample.m:59 and pidenergy.c:14 convert with mach_timebase_info, so the
protocol's meter is right. Do not quote probe.m's CPU-time figures.
`ri_energy_nj` is the kernel's energy estimate for the task, not a meter
reading, and it holds neither DRAM nor the fabric. The cross-checks below
exist for that reason.

The sampler and the paced run script exist (energy_probe/esample,
energy_run.sh, escenes.py). They were checked once, on a busy machine (ffmpeg at
490 % CPU), so that run is functional only and is **not a baseline**: 6.57 W,
110 mJ a tick, 1.82 cores busy (energy_probe/func_def_summary.txt).

### 12.2 Protocol

1. **Paced, not uncapped.** The game runs at its own 59.94 Hz pace; demo.sh and
   a01.sh force EM_UNCAPPED=1, which measures work, not energy. It is headless,
   with the replayed input demo_level.rec, and EM_FRAME_TIMING on.
   - energy_run.sh currently also sets EM_PS2_DISC_DRIVE_TIMING=1. Whether the
     replay needs it should be settled once, and then kept the same for every
     arm.
2. **Fixed scenes:**
   - AREA11 cinematic (345-1654), calm and slide;
   - AREA01 beat, fire and fire peak (the counter ranges in
     baselines/scenes.txt).
   - At least 60 s per window. The bench replayer (section 8.4) loops short
     scenes.
3. **Quiet gate.** At least 70 % CPU idle before each run (the quiet.sh rule of
   P13), with load and battery state recorded. Same display state each time,
   because WindowServer's GPU use is in the system GPU meter. Arms are
   interleaved A, B, B, A.
4. **Report per tick:**
   - process CPU energy (mJ);
   - system GPU energy minus the idle baseline measured right before (mJ);
   - their sum;
   - CPU-ms (all threads);
   - GPU-busy ms per frame (command-buffer times);
   - ticks over.
5. **Cross-check** one run of each arm with powermetrics (sudo).
   *(review)* A second, whole-machine candidate needs no sudo: the battery's
   own current and voltage while the Mac runs on battery (the
   AppleSmartBattery service in the I/O registry). It is not verified in this
   pass. It is the measure closest to "battery", which is the user's stated
   aim.
   - *(review)* Compare the arms on raw totals. The idle-subtracted GPU figure
     is for reporting only; the GPU path changes how long the GPU stays awake,
     so subtracting a fixed idle baseline would bias the comparison.
   - *(review)* **Noise and pass rule.** Noise is the difference between the
     two A arms of an A, B, B, A set. A gate's saving passes when it exceeds
     twice that noise in each of two sets.
6. **First comparison, before any GPU work.** The CPU path at 1, 3 and 6 workers,
   paced. More workers cost 17-54 % more CPU for the same bytes (baselines/,
   section 12.3), so the default worker count may not be the energy-optimal
   one. Any worker count is exact (gs_world_test), but changing the default is
   a user decision. This also produces the CPU baselines L2 has to beat.

### 12.3 Today's baselines (CPU; from P15/P16 receipts)

Joined in baselines/scenes.txt (build/perf_investigation/m4/lc_*.txt with the
scene ranges). Each cell gives: all-worker GS CPU / busiest worker / game
thread, in ms per frame. *(review)* These come from uncapped runs (demo.sh /
a01.sh, EM_UNCAPPED=1). They measure work, not the paced protocol's energy,
and the all-worker figure includes the parse (1.5-1.9 ms), which stays on
the CPU.

| Scene | 3 workers | 6 workers (default) | 10 workers |
|---|---|---|---|
| AREA11 cinematic | 18.10 / 6.23 / 6.61 | 21.32 / 4.15 / 5.80 | 27.88 / 3.84 / 5.79 |
| AREA11 calm | 17.25 / 6.00 / 6.40 | 21.07 / 4.00 / 6.03 | 27.68 / 3.64 / 5.99 |
| AREA11 slide | 17.34 / 5.97 / 6.45 | 21.32 / 3.91 / 6.05 | 27.09 / 3.42 / 5.95 |
| AREA01 beat | 20.66 / 7.13 / 10.32 | 23.72 / 4.33 / 10.31 | — |
| AREA01 fire | 22.55 / 7.80 / 10.23 | 25.54 / 4.60 / 10.13 | — |
| AREA01 fire peak | 26.00 / 8.95 / 10.12 | 29.17 / 5.12 / 9.83 | — |

- One worker (pure work, PERFORMANCE.md:1555): AREA11 15.7-17.2 ms, AREA01
  18.9-24.4 ms, about 10-12 ns a pixel.
- Parse: 1.5-1.9 ms a frame.
- The ceiling for L2 is removing the worker CPU less the parse: about
  15.6-27.6 ms a frame, or about 65-71 % of the measured per-frame CPU
  (baselines/scenes.txt: cpu_sum minus parse, over total CPU per frame). The
  parse, the game thread and encode/submit stay.
- **No energy baseline exists yet.**

### 12.4 Break-even (arithmetic, not measured)

**CPU side.** GS CPU energy per frame is roughly removable worker CPU-ms
times the power of a busy P-core. *(review, corrected)* The only receipt for
a spinning core is 0.836 W (energy_probe/probe_result.txt:1, at about
1.5 GHz by its cycle count). The earlier "1.6 W per core when spinning" has
no receipt. About 3.6 W per busy core comes from the busy-machine functional
run (6.57 W / 1.82 cores, func_def_summary.txt). So AREA11 is about
13-71 mJ a frame, and the AREA01 fire peak about 23-99 mJ. The spread is
the DVFS state, which only the paced measurement settles.

**GPU side, at full load.** At 16.4 W (to be re-measured), the GPU wins only if
its busy time is under about 0.8-4.3 ms a frame in AREA11 (1.4-6.1 ms at
the AREA01 peak). At 1.47M pixels that is about 0.5-2.9 ns of whole-GPU time
a pixel. A paced load lets the GPU run at lower clock states,
which are usually more efficient per operation. That is unmeasured, and it is
why the gate is a measurement.

**What it costs today.** Today's GPU is not idle: it already draws the field
quad and the 2D overlay every frame, so L2 adds GPU busy time to a GPU that is
already awake.

**Rough GPU estimate for an AREA11 frame:** about 1-4 ms on the M1 Pro, from the
probes' per-pixel costs plus binning, texture and Z/frame traffic. This is an
estimate with wide error, and stage 2 replaces it with a measurement.

---

## 13. Staged plan

Effort is in focused weeks of agent work with user review. Estimates are rough
and grow with every surprise. Every stage has the same entry and exit rules:

- **Entry:** user approval for port code.
- **Exit:** every item below plus the whole GS proof set with EM_TEST_FULL=1,
  the dual-run check over the demo and the AREA01 route, and the stage's
  negative controls caught.
- **Records:** results are written in PERFORMANCE.md "Progress" in the existing
  style (P18 onward).
- **Energy gate:** every gate is measured with section 12's protocol, against
  the CPU path at the default worker count *and* at the energy-optimal worker
  count from step 6 of the protocol.
- *(review)* **One pass rule for every energy gate.** A stage's measured
  energy saving per tick (process CPU plus system GPU, raw totals) must:
  - be at least half of that stage's *projected* saving. The projection is
    the stage's routable-suffix share of GS CPU, from stage 0.5, times the
    measured CPU energy per tick of the GS workers;
  - exceed twice the A-A noise in each of two A, B, B, A sets;
  - leave ticks over unchanged.

  If a stage fails, its class stays on the CPU (the router keeps it there),
  the result is recorded, and the next stage goes ahead only if its own
  projected saving is still above the measured fixed overhead.

### Stage 0: the arithmetic spike. DONE (this pass)

- **Result:** GO. See section 4.3: the STQ chain, stq_div and Z are exact and
  fp64-free, at 0.39-0.45 ns a pixel (FX-C) and 0.16 ns a pixel (Z) on the
  M1 Pro.
- **Not covered:**
  - the UV-triangle division in its final form;
  - the zrow expression as a unit;
  - the triangle edge solve on the GPU;
  - any frame-level cost.

### Stage 0.5: infrastructure (1.5-3 weeks)

- **Build:**
  - the bench stream recorder and replayer (section 8.4);
  - the dual-run mode (section 3, item 6);
  - an offline metallib build in the Makefile and the iOS build, with the
    strict flags;
  - the shared FX header promoted from the spike, with its CPU and GPU
    arithmetic test (`make test-gs-gpu-arith`);
  - the start-up self-test;
  - the GPU backend hook in the sweep.
- **Measure:**
  - the CPU path's energy baselines at 1, 3 and 6 workers (needs a quiet machine
    and, for the cross-check, the user's sudo);
  - the idle and full-load GPU power, again, with receipts.
- *(review)* **Also measure, from the recorded streams (no GPU code
  needed):**
  - for each stage's supported state set, the share of GS CPU in the longest
    GPU-routable suffix of each frame (the parse plus the state census's
    per-state times, PERFORMANCE.md:1330). This is each stage's real
    ceiling under the suffix rule (section 6.2), and it fixes the stage
    order;
  - the number of route switches a frame would need without the suffix
    rule;
  - the alias and self-feedback span counts on the demo and the AREA01 route
    (section 8.3).
- *(review)* **Fix first:** probe.m's CPU-time units (section 12.1).
- **Gate:** the replayer reproduces the live frames' memory exactly on the CPU
  path, and the baselines are recorded. **No-go** if the replay is not
  deterministic: fix that first. *(review)* **Re-plan** if the DOF suffix
  share is below 25 % of GS CPU in the paced cinematic, or if any
  same-segment alias or self-feedback span occurs on the route. Those are
  the design's two load-bearing assumptions.
- *(review)* Effort: add about 1-2 weeks for the items above (2.5-5 weeks
  in all).

### Stage 1: untextured sprites and clears on the GPU, with dual-run (2-3 weeks)

- **Scope:**
  - the head's Z clear and colour fills (sprite_fill's trivial sprites,
    em_gs_raster.c:1992; P7's class);
  - untextured sprites in CT32 / Z24 / Z32;
  - the router (CPU for everything else), the tile kernel skeleton with ordered
    binning, the shared GS buffer, and the field de-swizzle and present.
- **Proof:**
  - the conformance tests' untextured sprite packets on the GPU, equal to the
    CPU;
  - the sweep restricted to these states;
  - dual-run over the demo and the AREA01 route;
  - deliberate ordering tests (a second dispatch reading the first one's
    writes; the alias split).
- **Measure:**
  - the per-frame overhead of the GPU path: encode, commit and wait latency, and
    the CPU/GPU switches;
  - the tile size (16x16 against 32x8).
- **Gate:** correctness, plus overhead. The class is about 1 % of GS CPU, so
  there is no energy win to show yet. *(review)* Under the suffix rule the
  head's clear is a *prefix*, so it is not routed to the GPU on the live
  route. Stage 1's kernels are proven on the conformance tests and the sweep.
  On the live route they are proven through dual-run with a developer switch
  that forces every routable span to the GPU, whatever the switch count.
  **No-go**, quantified *(review)*: measure the fixed overhead E_fix as the
  energy per tick with the GPU path on but committing an empty suffix every
  frame, minus the same with it off (the protocol's pass rule for noise). If
  E_fix exceeds a quarter of stage 2's projected saving (the DOF suffix share
  from stage 0.5 times the GS workers' measured CPU energy per tick),
  redesign the submission before going on.

### Stage 2: the depth-of-field pass (2-3 weeks)

- **Scope:** 001DDE10's pass, which is 34.8 % of the demo's GS CPU
  (PERFORMANCE.md:1332):
  - the copy: UV CT32 sprites, DECAL, REGION_CLAMP, bilinear, Z ALWAYS with
    ZMSK;
  - the blend back: MODULATE, alpha NEVER with FB_ONLY, Z GEQUAL,
    (Cs - Cd) As + Cd;
  - its eight barriers.
  
  All of it is integer, plus the CPU's V table. It is the most GPU-friendly
  large class.
- **Proof:**
  - **the 8 captures of the original** (test_dof_pass_reference parts A and C
    on the GPU);
  - the conformance UV sprite tests;
  - the sweep;
  - dual-run.
- **Energy gate, the first real one.** In the paced cinematic, energy per tick
  (process CPU plus system GPU) must fall measurably: at least half of the DOF's
  estimated CPU-energy share, outside run-to-run noise. Ticks over must not
  increase. The pass's GPU time per frame is recorded.
- **Stop rule.** If the DOF pass, the cheapest and most regular large class,
  does not save energy, the rest will not. **Stop L2** and record why
  (section 14).

### Stage 3: textured sprites (2-3 weeks)

- **Scope:**
  - the snow and fire STQ sprites (T8 / T4 with CT32 CLUT, MODULATE, alpha
    NEVER with RGB_ONLY, Z GEQUAL, two blends, with and without fog);
  - the per-row and per-column STQ tables from the CPU's own expressions;
  - CLUT snapshots per span.
- **Proof:** the conformance sprite and STQ tests, the sweep, and dual-run on the
  AREA01 fire (where these sprites peak).
- **Energy gate:** the AREA01 fire peak's energy per tick falls against stage 2,
  by the pass rule above. *(review)* Under the suffix rule these sprites save
  energy only where every span after them in the frame is GPU-routable. If
  stage 0.5's suffix shares show that the chain page's sprites are followed
  by unsupported triangles, merge stage 3 into stage 4, or order the two by
  the measured shares.

### Stage 4: textured triangles, the STQ path (4-8 weeks)

- **Scope:** the level class and the other census triangles, 53.8 % of the
  demo's GS CPU (PERFORMANCE.md:1331):
  - T4 STQ MODULATE/HIGHLIGHT with fog, alpha GREATER and Z GEQUAL;
  - T8 STQ CLAMP with Z ALWAYS and ZMSK;
  - CT32 STQ REGION_CLAMP with and without the additive blend;
  - gsw_alpha_clear's strip and the shadow's boxes (untextured flat triangles,
    8.0 %).
- **Methods:**
  - FX-C with the FX-E fallback;
  - fx_zpix with a GPU zrow (soft binary64);
  - the in-tile edge solve and row starts;
  - the Gouraud and fog block DDA.
  
  Decide by measurement whether per-primitive setup stays on the CPU or moves
  to a GPU setup kernel (proven by a setup differential against the CPU code).
- **Proof:**
  - the conformance tests with the level class (gscap `level_class`, p3_stq
    `level_0..3`, p7_lvl): the same `OPEN` counts, value for value;
  - the sweep with the fallback forced on, and forced off as a negative control;
  - dual-run;
  - the fb2 harness's exact-pixel counts unchanged.
- **Energy gate:** the cinematic and the AREA01 beat fall again, by the pass
  rule above. The total for a frame drawn mostly on the GPU must beat the CPU
  path's best worker count. *(review)* **Stop rule:** if the triangles, which
  are 53.8 % of GS CPU, fail the gate, L2 stops at the DOF suffix and is
  recorded as such; stage 5 is not started.
- **Watch:** register pressure and occupancy (exp2 saw maxThreads 384-448), and
  divergence on small triangles. If occupancy limits throughput, split the
  STQ triangle into its own kernel.

### Stage 5: everything else; the CPU leaves the hot loop (4-8 weeks)

- **Scope:**
  - UV triangles, lines, points;
  - DATE, SCANMSK, dither, CT16 / Z16 targets (whole-word
    writes);
  - PABE, FBA;
  - the generic states;
  - mid-frame CLUT loads from drawn memory (a GPU CLUT-load kernel);
  - the load veil's list frames (em_gs_world_list_frame).
- **Then:** the CPU workers are no longer started by default when the GPU path
  is active (EM_GS_THREADS reports 0 workers). The parse stays, early on the
  game thread. The CPU model remains the fallback and the reference
  (section 6.1).
- **Proof:** the whole proof set on the GPU with no CPU fallback taken on the
  first level's route (counted), and dual-run on every route test.
- **Energy gate:** the final paced numbers on every scene, against both CPU
  baselines, recorded in PERFORMANCE.md.
- **Docs:** update PERFORMANCE.md (L2 and section 5), GS_EXACT.md section 9
  ("Binding"), FIDELITY_FEATURES.md and LAUNCHER_OPTIONS.md (a GPU-GS switch
  with the CPU model as the alternative, both exact).

### Later: the iPhone, then D3D12 and Vulkan

- **The iPhone.** Device measurement runs from stage 2 onward whenever the user
  can run the phone (section 15). The phone is where the gain should be
  largest.
- **D3D12 and Vulkan.** Section 11, 3-6 weeks each.

**Total for the Metal stages 0.5-5:** roughly 4-7 months. That fits
PERFORMANCE.md's "months" for L2. *(review)* That is the plain sum of the
stage ranges (15.5-28 weeks), with no contingency. The review adds 1-2 weeks
to stage 0.5. GPU kernels are harder to debug than the CPU model, and stages 4 and 5 hold the
divergent and generic paths. Treat 7 months as the expected case, not the
pessimistic one, and re-estimate at each gate from the actuals.

---

## 14. Risks, and what would make us stop

| Risk | Likelihood | Effect | Mitigation and stop rule |
|---|---|---|---|
| The GPU path does not save energy (fixed overhead, GPU power at low load, the field already presented by the GPU anyway) | **medium**: unmeasured | L2 not worth it | Gates at stages 1 and 2. **Stop if the DOF stage does not save energy.** |
| Ordering error: aliasing, a missed barrier, texture read-after-write within a segment | medium | wrong pixels, maybe rarely | The alias check (8.3), the dual-run check on every route test, the sweep with every-block-uploaded frames (as gs_world_test does), deliberate ordering tests |
| GPU nondeterminism (driver, scheduling) | low: integer-only, single writer a pixel | flaky results | A repeat run equal to the first in every test. **Stop the stage if any run-to-run difference appears** and cannot be explained by our code. |
| The certified bound (FX-C) is wrong in a corner the tests miss | low | a rare 1-ulp texel | The FX-E fallback is exact by itself, and FX-C can be switched off (cost 1.8 ns against 0.4 ns a pixel). Midpoint stress tests and negative controls are kept permanently. |
| Divergence and occupancy on small triangles make stage 4 slow | medium | GPU time a frame too high | Batching, a split STQ kernel, coarse pre-binning. Accept the stage only if its energy gate passes. |
| Two implementations drift (a rule fix lands in the CPU model only) | high over time | dual-run failures | CPU first, then the GPU. Dual-run in the level smokes. One shared arithmetic header. |
| A driver or OS update changes float behaviour | low (the design uses integers; the estimate is always corrected) | — | The start-up self-test, with fallback to the CPU |
| iOS background or thermal behaviour | medium | stalls | Failover to the CPU, and a measured sustained run on the device |
| Effort overrun | high | months become more | Stage gates. Every stage leaves the port shippable, because the router keeps the CPU path for the rest. |
| *(review)* Mixed routing leaves a CPU worker's decoded texture copy stale (a GPU-drawn span not stamped) | medium, if forgotten | wrong texels in a later CPU run | Stamp every GPU span (em_gs_list_pass), with a negative control per stage (section 6.2) |
| *(review)* A self-feedback span or a same-segment alias drawn on the GPU | low on this route (to be counted) | order-dependent pixels | Route it to the CPU, drawn by one EmGs (sections 6.3, 8.3); count it in stage 0.5 |
| *(review)* A wait that ignores the GPU part (prior_wait, `live`, the upload wait) | medium | a CLUT read or an upload races the GPU | The wait inventory of section 8.1, a negative control for each |
| *(review)* A command buffer fails after running in part | low | an inexact "fallback" | The pre-commit snapshot, or fail-stop (section 10) |
| *(review)* The suffix rule leaves little on the GPU before stage 4 | medium | stages 1-3 save nothing on the route | Measure the suffix shares in stage 0.5, and re-order or merge stages from them |

**Stop L2 entirely if:**
- the stage 2 energy gate fails;
- an exactness difference is found that cannot be fixed without changing the CPU
  model's output;
- the per-frame fixed overhead alone outweighs the CPU energy it could save
  (stage 1).

In each case the CPU model stays the Original profile's renderer, exact as
today, and the measurements are recorded in PERFORMANCE.md.

---

## 15. What the user must provide or decide

1. **Approval to start port code at stage 0.5.** Nothing in this pass touched
   the port. *(2026-10-17: the user chose "Not yet".)*
2. **Sudo for `powermetrics`.** Only for the cross-check runs (section 12.2,
   step 5); the main meters need none. A short session per gate, run by the
   user, or a command the user starts.
3. **A quiet machine** for the baseline and gate runs: no ffmpeg and similar
   background loads (the functional run had 0 % idle).
4. **Device runs on the iPhone Air**, connected, for the start-up self-test and
   the energy and thermal runs, from stage 2.
5. **Decisions:**
   - (a) Option D, a cheaper shared arithmetic in the CPU model (section 5).
     Recommended: no, not now.
   - (b) The energy-optimal worker count as the CPU path's default
     (section 12.2, step 6). This is the user's decision, informed by the
     baseline runs.
   - (c) How the GPU GS appears in LAUNCHER_OPTIONS.md / FIDELITY_FEATURES.md.
     Recommended: both renderers are exact, and the GPU GS becomes the default
     only after stage 5's gate.
   - (d) Whether to package the arithmetic layer (the FX header and its proofs)
     as a separately documented component for other PS2 projects.
     **Decided (user, 2026-10-17): yes.** It stays our own code and proofs,
     written from our model, with no game data; it is built in stage 0.5.
   - (e) Where the bench streams live: `build/` only, never committed
     (disc-derived).
   - (f) *(review)* Whether the pre-commit memory snapshot (section 10) runs
     on macOS too, or only on iOS. It costs about 4 MiB of copying a frame.
     Without it, a failed command buffer fail-stops on macOS.
   - (g) *(review)* Whether the live route may ever use more than one
     CPU/GPU switch a frame (section 6.2), once stage 0.5 has measured what
     that would buy.

---

## 16. Open questions and where the lenses disagree

- **Subnormal binary32 add on the M1 Pro.**
  - exp1_fp_probe/run.log:3 reports 0 of 1,048,576 differ.
  - metal_probe/run_1e7.txt:79-81 reports 833,203 of 833,203 differ, returned
    as 0.
  - The two input sets differ (exp1 possibly has mostly normal results). This
    is unresolved, and the design does not depend on it.
- **The error of fast-math division:** "up to 1 ulp" (exactness lens) against
  "up to 2 ulp, about 31 %" (metal_probe). These are different input sets. The
  design does not depend on it either.
- **STQ costs across probes:**
  - exp1, 3.1 ns;
  - metal_probe, 3.0 ns;
  - exp2, 4.5 / 3.1 ns;
  - the spike, 0.39 ns.
  
  They are different formulations and workloads. The spike's figure is the most
  recent and the most optimised, on the most favourable workload (section 4.3
  caveat).
- **The cross-buffer write-alias case in the CPU bands** (section 8.3): to be
  checked, not claimed.
- ~~Whether serial dispatch alone orders memory~~ *(review: settled by design)*:
  Apple's page does not promise it, so the design always issues an explicit
  `memoryBarrier(scope: .buffers)` between dependent dispatches (section 8.2).
- *(review)* **Self-feedback and same-target aliasing in the CPU bands**
  (section 8.3): bands of one primitive run concurrently, and the random sweeps'
  lockstep bands cannot show such a race. To be checked, not claimed.
- *(review)* **The early parse with a fast GPU.** It runs only while the
  previous frame is still drawn (em_gs_world.c:607-628). With a GPU suffix of
  a few ms, the early parse's share of the parse will shrink. Whether the
  parse then moves onto the critical path of the tick is measured in stage 2.
- **Doc fix pending approval:** PERFORMANCE.md:264 ("integer arithmetic") should
  name the float steps (section 4.1).

---

## 17. Receipts (scratch, git-ignored, under build/l2_explore_1017/)

| Folder | What it holds |
|---|---|
| `spike/` | fx.h (the shared C/MSL arithmetic), ref_model.h (copied model expressions), cpu_test.c, gpu_test.m, kern.metal, cpu_run_final.txt, gpu_run_1e7.txt |
| `exp1_metal_exact_probe/` | the exactness lens's soft-binary64 probe (run_scale4*.txt) |
| `exp1_fp_probe/` | hardware binary32 behaviour per math mode (run.log) |
| `exp2_softfloat/` | integer IEEE ops and the certified rational path (sd.h, harness.m, midpoint.c, run.log) |
| `metal_probe/` | the Metal lens: compile modes, contraction, 64/128-bit integers, timings (run_1e7.txt; off_*.metallib built with the strict flags) |
| `energy_probe/` | the meters (pidenergy.c, ioreport.m, esample.m), the paced run script, receipts |
| `baselines/` | scenes.py and scenes.txt: CPU baselines per scene |
| `web_msl/`, `web_fst/`, `apple_docs/` | text copies of Apple's public documentation that the lenses read |

**Clean room.** No PCSX2, ARMSX2 or `../pcsx2-fork` source was opened by this
pass. The only sources were the port's own files and Apple's public
documentation and forums.

---

## Review notes (adversarial review, 2026-10-17)

An adversarial review read this draft in full, along with the cited port code
(em_gs_raster.c, em_gs_world.c/.h, em_gfx_metal.m, em_scene_bindings.c), the
spike's sources and receipts, the probes' receipts, and Apple's public
documentation. It edited the doc in place, and every change is marked
*(review)*. No port or decomp file was edited. The only new files are a
counter-summary script and copies of Apple's documentation pages, both in
the session scratchpad.

**Changed: exactness and ordering**
1. Self-feedback spans (section 6.3). The draft said texture reads in a
   segment are race-free because of the recorder's barriers. The barrier
   goes *before* a reading primitive (em_gs_world.c:1746-1770). The CPU model
   explicitly draws a primitive that samples its own frame or Z
   (em_gs_raster.c:973-977). Such spans now route to the CPU, drawn by one
   EmGs. The same hazard is added as an open question for the CPU bands.
2. Aliasing (section 8.3). The cited "fields, Z buffer and copy buffer are
   disjoint" is not in GS_EXACT.md. The copy buffer *is* the shadow target's
   memory at another FBW (GS_EXACT.md:1455-1457). It is separated by a
   barrier, so it is safe, but the claim was wrong. An overlapping
   FRAME/ZBUF primitive also goes to the CPU. The "8/4-bit targets" in
   the draft do not exist; the model draws only 32- and 16-bit frames.
3. Mixed routing leaves CPU workers' decoded-texture caches stale unless
   GPU-drawn spans are stamped (section 6.2). Added, with a negative control.
4. Wait inventory (section 8.1). prior_wait, the early parse's `live`, own_drain,
   the upload, memory and readout waits must all include the GPU part. The
   draft named only em_gs_world_wait.
5. Inter-dispatch ordering (section 8.2). Apple's serial-dispatch page does
   not promise memory visibility, so a test cannot settle it. The design now
   always issues `memoryBarrier(scope:)`. Metal 4 queues do no hazard
   tracking (sourced).
6. Row anchors are the row's xd, never the tile's (section 6.3). Added as a
   required negative control.
7. Arithmetic (sections 3 and 7). zrow is now marked **not proven**: the GPU
   spike took zrow from the CPU, and the soft-binary64 tests are random only
   and flag out-of-range values. Added:
   - the exhaustive stq_div slices on the GPU;
   - a forced slow path (never taken on the GPU in the spike);
   - UBSan, and a no-float lint;
   - written int64 bounds for every int128 site;
   - the missing float sites (point STQ, the UV sprite check).

   "Fast math does not matter" is narrowed to the FX kernels: metal_probe's
   hardware-divide STQ differs in 14 of 114,688 pixels under fast math.
8. Presentation (section 9). "No CPU copy" was wrong: the scan buffer, the
   no-draw-frame check and the present log use a CPU copy of the field.
9. Failover (section 10). "Fail over to the CPU" is exact only from a
   snapshot taken before the GPU commit, or the frame fail-stops.

**Changed: routing, gates, energy, effort**

10. Router (section 6.2). With 610-796 spans a frame (measured), a
    state-class router with "a few switches, else CPU" would route almost
    nothing. The live route now uses a CPU prefix and one GPU suffix. The DOF
    pass is the body's last CALL, so it is the natural first suffix.
11. Gates (section 13). One quantified pass rule for all energy gates, a
    quantified stage 1 no-go, a stage 4 stop rule, and new stage 0.5
    stream measurements: suffix shares, switch counts, alias and
    self-feedback counts. Those decide the stage order and add a re-plan
    gate.
12. Energy (section 12).
    - "1.6 W per spinning core" has no receipt; the receipt is 0.836 W. The
      break-even is recomputed (GPU busy time under 0.8-4.3 ms a frame in
      AREA11).
    - probe.m's CPU-time column is in mach ticks.
    - The baselines are uncapped and include the parse.
    - Arms are compared on raw totals, with a noise rule; a battery meter is
      added as a candidate.
13. Measured primitive counts replace the 12,500 estimate (section 6.3).
14. The bench recorder's entry list was missing drawn_buffer, list_frame,
    memory_load and preparse (section 8.4).
15. Effort. The 4-7 months is the plain sum with no contingency; 7 months is
    now the expected case. The D3D12/Vulkan "3-6 weeks" had no basis and is
    withdrawn. The reusability claim is narrowed: the frame architecture leans
    on this game's properties.
16. iOS (section 10). The background article is now cited. MetricKit is
    once a day, so it cannot serve A/B runs; Instruments' Power Profiler is
    the candidate.

**Checked and found sound:**
- the spike's copied expressions against em_gs_raster.c:2338-2493 and :62-72;
- the receipts' numbers in section 4.3;
- the MSL 4.1 sections 8.1 and 8.2 text;
- the feature-table rows (64-bit integers Apple3, 64-bit atomics Apple9 with
  Apple8 min/max on macOS, 32 KB of threadgroup memory, raster order groups
  Apple4);
- the port's runtime `options:nil` compiles;
- the PERFORMANCE.md citations used, and the first level having no own-span
  CLUT load (PERFORMANCE.md:1675).

**What remains for the user**
- Decisions 15(a)-(g). (f) and (g) are new.
- Approval of the stage 0.5 plan, which now includes the stream measurements.
  Their results may reorder stages 1-4 or stop L2 early (the re-plan gate).
- Sudo for powermetrics, a quiet machine, and on-battery runs if the battery
  meter is adopted.
- Phone runs with Instruments' Power Profiler.
- Whether the CPU-band hazards (self-feedback, same-target alias) should be
  checked now as a CPU-path item. They are not reachable on the first
  level's route as far as the counters show, and they are not claimed as
  bugs.

