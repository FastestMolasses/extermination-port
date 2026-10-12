# Why the port runs slowly, and how to fix it while staying exact

Port performance investigation, 2026-10-10. Port main 8b68485, macOS `make all` (clang -O2), M1 Pro (8 performance + 2 efficiency cores).

All measurements were headless, on a quiet machine (73-83 % CPU idle before each run; 1-minute load average 3.7-15, including the runs' own threads, recorded in runs/*/load.txt), and run one at a time.

This report combines three inputs:
- a profile of the port (RESULTS.txt, scaling_summary.txt, dof_summary.txt, runs/sample_*/summary.txt);
- a code reading of the port (reader/);
- a black-box benchmark of the PCSX2 fork in the same scenes (fork_bench/SUMMARY.txt, fork_bench/CPU_SUMMARY.txt; launcher and DebugServer only, no emulator source read).

Where a figure is an estimate, the report says so.

## 1. The short answer

The game itself is native and fast. What is slow is the way the Original profile draws the picture.

- **Game logic.** It runs as native code and takes 8.5-8.9 ms per tick in AREA11 and 13.3-14.0 ms in AREA01. That includes the VU1 programs and the exact float emulation.
- **GPU renderer.** With EM_GPU_RENDERER=1 the whole tick takes 6-8 ms in AREA11 and 13.2 ms in AREA01. No tick runs over the 16.68 ms period in any scene measured.
- **Original profile.** It does not use the GPU. To produce the exact PS2 picture, it recreates the PS2's graphics chip (the GS) pixel by pixel on the CPU. That code is a reference model, written for exactness and readability, and it costs about 36 ns per pixel.
- **Why effects hurt.** The real GS was a dedicated chip built for huge fill rates, and this game uses that fill rate freely.
  - A normal AREA11 frame draws 1.47M pixels, 12.8 times the 512x224 field.
  - The game's depth-of-field pass covers the screen eight times in every in-level frame.
  - The snow spray and the fire are large, overlapping, transparent sprites. They push the count to 2.1M and 2.4-2.7M.
- **Result.** At 36 ns a pixel, one frame is 52-60 ms of one core's CPU time. The port spreads that over 7 threads. That just fits the 16.68 ms tick in AREA11, and does not fit near the AREA01 fire.
- **Why it looks like slow motion.** The game runs one logic tick per frame and never skips frames, as the original does. A 22 ms tick slows the game down; frames are not dropped.

**Why PCSX2 is faster.** PCSX2 runs this game at 148-212 vsyncs/s in-level (3-4x real time), under Rosetta, with its software renderer.
- **Its GS cost.** In the same frames, its software GS uses about 8.5-13 ms of one core's CPU per frame.
- **The port's GS cost.** 52-60 ms in AREA11, and about 77 ms of one core in the AREA01 fire (95-107 ms across 7 workers).
- **The gap.** About 6x single-core, and 7-9x counting all workers. The fork's figures are upper bounds (process CPU includes spin-waits), so the gap is at least that large.
- **Why.** PCSX2's renderer is built for speed. Its public descriptions mention per-state specialized SIMD code, decoded-texture caches and precomputed address tables. The port's model has had none of that work.
- **Effects in the fork.** They barely cost it anything: the slide adds 0.7 ms of GS CPU. In the port, the slide raises the busiest worker from about 10 ms to peaks of 16-22 ms.

So the port is not slow because it is native. It is slow because its exact renderer has never been optimized. Section 4 makes it faster without changing a single output byte.

Two things add to what the user sees:
- **Machine load.** On a busy Mac (load 10-16), GS_EXACT.md 10.2 recorded 78-740 of 1,300 ticks over the period, against 0-23 on a quiet machine. fileproviderd uses about one core all the time on this Mac.
- **Windowed presentation.** Measured and fixed on 2026-10-11 (Progress P2): each frame used to block 2-5.5 ms on the window's drawable on this Mac's 120 Hz display; it now waits 0.04 ms.

On the iPhone Air the port got only 3 drawing threads, and every tick measured that way is over (section 5). Since Progress P3 it gets 4, chosen from the core types; not yet measured on the device.

**After the quick wins (Progress P10, 2026-10-12).** On this Mac at the default worker count, every AREA11 scene now runs at full speed, headless and windowed (0-5 of 1,310 cinematic ticks over, none windowed), and the game thread is about 3 ms cheaper per tick. The GS model's pixel cost fell only 3-4 % (about 34 ns), so the AREA01 fire still runs at 91-96 % speed and the 3-worker iPhone proxy at 79-88 % in AREA11 and 55-66 % in AREA01. The medium items (M1-M4) are what is left.

**After M1 (Progress P11, 2026-10-12).** The separable swizzle tables make the GS frame 15.7 % cheaper on one worker (about 28.5 ns a pixel). At the default worker count the Mac now runs AREA01 at 98.8-99.8 % speed (the fire peak's busiest worker 16.5 -> 13.8-13.9 ms) and AREA11 at full speed with 0-1 ticks over. The 3-worker iPhone proxy runs at 97-99 % in the AREA11 cinematic and calm, 77-89 % in the slide and 65-79 % in AREA01. M2-M4 remain.

## 2. Where the time goes, scene by scene

Quiet M1 Pro, default 7 GS workers, means per tick.
- **Busiest worker.** The slowest drawing thread's CPU time per frame, which is the frame's critical path.
- **1-core GS.** The frame drawn on one thread.
- **Over.** Ticks above 16.68 ms.
- **Fork.** Wall time per vsync for the whole emulated PS2 (software renderer, MTVU on), median of 3 runs.

| Scene (game counters) | Pixels / frame (x field) | 1-core GS ms | All-worker CPU ms | Busiest worker ms | Main thread ms | Tick wall ms (p95 / max) | Over, 7 workers | 3 workers wall (over) | GPU renderer wall (over) | Fork ms/vsync |
|---|---|---|---|---|---|---|---|---|---|---|
| Opening, no DOF (75-340) | 476k (4.2x) | 15.8 | 19.0 | 2.8 | 1.9 | 4.3 | 1 / 257 (load step) | 7.2 (1) | 1.2 (1) | - |
| Cinematic with DOF (345-1654) | 1,471k (12.8x) | 52.7 | 67.0 | 10.1 | 8.5 | 11.3-11.4 (15.1 / 18.6-21.6) | 15-23 / 1310 | 19.9 (all) | 7.5 (0) | 5.17 |
| Calm (1656-1776) | 1,478k (12.9x) | 52.1 | 66.7-67.1 | 9.9-10.2 | 8.9 | 11.1-11.5 (13.7-15.9 / 14.9-16.9) | 0-1 / 85 | 19.9 (all) | 6.0 (0) | 5.13 |
| Hill slide (4500-4620) | 1,670k (14.6x), peak 2.10M (18.3x) | 59.8 | 73.1-73.8 | 10.8-11.1 (single ticks up to 16.7) | 8.7-8.8 | 12.0-12.5 (15.5-16.9 / 16.4-18.4; single ticks 20-22) | 0-8 / 121 | 22.4 (all) | 8.0 (0) | 5.74 |
| AREA01 beat (15068-15848) | 2,030k (17.7x) | ~73 (est.) | 89.0 | 13.5 | 13.8 | 15.7 (20.9 / 28.2) | 213 / 781 | 27.4 (755) | 13.3 (0) | 6.16 (before f405) |
| AREA01 fire (15473-15848) | 2,140k (18.7x) | ~77 (est.) | 94.1 | 14.2 | 13.7 | 15.9 (21.1 / 28.2) | 105 / 376 | 29.4 (all) | 13.2 (0) | 6.76 |
| AREA01 fire peak (15700-15848) | 2,397k (20.9x); last ticks 2.70M (23.6x) | ~86 (est.) | 106.8 | 16.1 | 13.3 | 17.8 (24.4 / 28.2) | 89 / 149 | 33.3 (all) | - | worst 6-vsync chunk 10.7 |

The AREA01 1-core values are pixels x 36 ns, not measured runs.

**Frame contents.**
- **Cinematic:** 12.6k primitives, 0.99M pixels written, 126k GS register writes.
- **Snow:** its program runs 108 MSCALs in every AREA11 frame, even standing still. During the slide, sprite MSCALs rise from 2 to 7-9.
- **AREA01 fire:** 11.4k primitives, kind-6 MSCALs 1-3, sprite MSCALs 33-45.

**Depth-of-field pass (001DDE10).** It ran in 99-100 % of in-level frames, and adds 720,896 pixels and 8 barriers per frame. Measured with ext_nodof, a measurement-only build that skips the pass:

| Scene | Busiest worker, with -> without | All-worker CPU | Ticks over |
|---|---|---|---|
| Calm | 9.9-10.2 -> 6.6 ms | 67 -> 44 ms (-35 %) | 0-1 -> 0 |
| Slide | 10.8-11.1 -> 7.5 ms | 73 -> 50 ms | 0-8 -> 0 |
| AREA01 fire | 14.2 -> 11.9 ms | 94 -> 74 ms | 105 -> 66 / 376 |
| AREA01 fire peak | 16.1 -> 14.0 ms | 107 -> 87 ms | 89 -> 61 / 149 |

The AREA01 runs without the pass had a load spike to 15.

**Worker hotspots** (macOS sample):

| Function | Slide | Fire |
|---|---|---|
| fetch_rgba | 31.5 % | 31.1 % |
| plot | 28.5 % | 27.6 % |
| sample | 12.4 % | 13.2 % |
| draw_one | 11.8 % | 10.2 % |
| em_gs_write (per-worker replay of the command stream) | 6.0 % | 6.3 % |
| em_gs_addr32 | 4.7 % | 5.4 % |
| setup | ~3 % | ~3 % |
| idle | 13.6 % | 21.5 % |

**Main thread:**
- exact float emulation: 26-31 % of busy samples;
- chain page: 19-23 % (2.1 ms per tick in AREA11, 1.8 ms in AREA01);
- the snow program: 21.6 % of slide samples;
- textured_kick: 4.5-8.8 % (its texture lookup's linear search; a hash since Progress P8);
- ungated FNV digests: 2-4 % in AREA11, about 12 % (about 1.5 ms) in AREA01;
- AREA01's own logic: 27 %;
- waiting for the GS frame: 22 % calm, 33 % slide, 10 % fire.

## 3. Root causes, ranked by measured cost

1. **The CPU GS model's per-pixel cost: about 36 ns, 52-60 ms of one core per AREA11 frame.**
   - The path in em_gs_raster.c is generic, scalar and out of line.
   - plot() is not inlined (cost 2415). It calls sample(), which calls fetch_rgba() four times.
   - Each fetch does a wrap switch, a full swizzled address, a palette lookup and a format switch.
   - The frame and Z addresses are recomputed for every pixel, and every state switch is checked for every pixel.
   - Sprites and triangles divide per pixel.
   - clang vectorizes 0 loops in the file.
   - Fetch and sample are 44 % of worker time; plot and addresses are about 33 %.
2. **The depth-of-field pass in every in-level frame.** It costs 35 % of GS CPU and 3.4-3.6 ms of critical path (measured). It is original behaviour; it is expensive only because of cause 1.
3. **Effects multiply the overdraw.**
   - The slide adds 0.2M pixels on average and 0.6M at the peak.
   - The fire adds 0.7-1.2M.
   - The snow VU program is not the main slide cost: at most 2.1 ms of main-thread chain-page time.
4. **Poor thread scaling.**
   - Every worker replays every register write and sets up every primitive: +2.1 ms of CPU per extra worker, about 75 % efficiency at 7 workers, and 8 workers is worse than 7.
   - Bands are static 4-row interleaves, so the frame and each of the pass's 8 barriers wait for the slowest worker, including one on an efficiency core.
   - Workers are idle 13.6-21.5 % of the time.
5. **The main-thread budget, which is the bottleneck in AREA01.**
   - AREA01 uses 13.3-14.0 ms per tick, leaving about 3 ms of headroom.
   - The VU1 page programs (snow, sprite/flame, kind-6, lane, streak, kind-2) still use the integer float model. The level and object kernels already use proven host floats, which took them from 15.9 to 5.6 ms.
   - Ungated digests cost about 1.5 ms per tick in AREA01 (gated since P4); textured_kick 4.5-8.8 % (a hash since P8).
6. **The iPhone gets only 3 workers** (section 5).
7. **Unmeasured:**
   - the windowed Mac path: measured and fixed since (Progress P2);
   - a possible texture_entry cliff (a linear search of 1024 entries, plus a 16 KiB memset per miss once full): measured since (Progress P8). The cliff is never reached (at most 348 entries), but the linear search over the ~345 entries cost 0.5-0.9 ms of the game thread per tick; it is now a hash.

**Not a cause:** build settings. macOS uses -O2; the iOS project has optimization level 2 in both configurations, and build.sh builds Release. Measured since (Progress P9): -O3, LTO and -mcpu tuning do not shorten the tick. Presentation is about 0 % headless.

## 4. Optimization plan for the Original profile (bit-exact)

Gains are estimates unless marked measured. Re-measure each item with demo.sh, a01.sh and analyze.py.

**The GS proof set:**
- tools/test_gs_raster_reference.py (conformance colour and Z, pixel for pixel; part F is the band split);
- tests/gs_world_test.c (1, 2, 3 and 8 workers must equal 1 worker, including the depth-of-field pass and its barriers);
- tools/test_dof_pass_reference.py;
- tools/test_gs_fog_conformance.py;
- tools/test_gs_memory_reference.py;
- the fb2 pixel harness (GS_EXACT 10.3 / 10.4);
- tools/test_level_smoke.py and tools/test_level_smoke_area01.py.

Run the proof set with EM_TEST_FULL=1 for each item.

### Quick wins

**Q1. Gate the per-frame FNV digests** (em_chain_page_live.c, em_static_world_live.c).
- *Change:* compute them only behind a diagnostic variable, which the smoke harness sets, because the level smokes consume them.
- *Gain:* about 1.5 ms of main thread per tick in AREA01, about 0.2-0.35 ms in AREA11.
- *Effort:* hours. *Risk:* very low.
- *Proof:* the level smokes, test_chain_page_reference.py.
- *Done (Progress P4):* AREA01's main thread 1.9 ms less per tick, AREA11's 0.6-0.9 ms; the tick itself is unchanged where the GS workers set it.

**Q2. Host floats in the VU1 page programs** (em_vu1_page_programs.h).
- *Change:* switch emvup_add/sub/mul to the host-lane functions inside em_vu_host_enter / em_vu_host_leave. emvup_chk already faults on exponent-255 operands, which is the host-lane precondition. The divide needs a proven host truncated quotient.
- *Gain:* the snow program is about 1.9 ms of the slide's main thread. Estimate 1-2 ms of main thread in AREA11 and in the AREA01 fire.
- *Why it matters:* this is critical-path time in AREA01 and on the GPU renderer. On the iPhone it also frees performance-core time for the workers.
- *Effort:* 1-3 days. *Risk:* low.
- *Proof:* test_vu_host_lanes.py (extended to the divide), test_chain_page_reference.py, test_snow_tiles_reference.py, test_level2_kind6_vu_reference.py, the level smokes.
- *Also check:* whether the remaining exact-float hot spots (corner / vu_row / rotate) meet the same precondition.
- *Done (Progress P5):* the game thread 1.1 ms less per tick in AREA01 and 1.4-1.5 ms less in AREA11; on the GPU renderer the AREA01 tick fell 1.3 ms. The GS-frame tick is unchanged on the Mac, where the busiest worker sets it. The corner / vu_row / rotate check is not done; they are now the game thread's largest self time in AREA01 (em_vu_vec_bits, 17.5 %).

**Q3. Per-column U in sprites.**
- *Change:* compute U once per column (UV: floordiv64; STQ: t plus stq_div). V stays per row.
- *Gain:* measured offline at -6 % of the pass (24.5 -> 22.9 ms single-thread, about 0.25 ms of busiest worker). Estimated 15-30 % of the snow and fire STQ sprite pixel cost.
- *Effort:* about 1 day. *Risk:* very low.
- *Proof:* the GS proof set.
- *Done (Progress P6):* the same bytes (the proof set, a 60,000-sprite HEAD-against-Q3 differential, the 8 DOF captures). The DOF pass is 7.5 % cheaper offline, and one worker's GS frame 2.1 ms cheaper in the cinematic. At 6 workers the busiest worker is 0.26 ms cheaper in the AREA11 cinematic and calm, 0.57 ms in the slide, and 1.1-1.8 ms in AREA01. AREA01's ticks over the period fall by about half.

**Q4. Fast path for trivial-state sprites** (the clears, gsw_alpha_clear).
- *Gain:* about 1.5 ms of all-worker CPU per frame, about 0.2 ms of busiest worker.
- *Effort:* about 1 day. *Risk:* low.
- *Proof:* the GS proof set.
- *Done (Progress P7):* the same bytes (the proof set, a 65,824-primitive HEAD-against-Q4 differential). The trivial sprites (the field's Z clear, the full clears and the colour fills) cost 7.4 times less; one worker's GS frame 1.1 ms less in the cinematic and calm, and the busiest of 6 workers 0.26-0.30 ms less in AREA11. gsw_alpha_clear is not one of them: it is a blended triangle strip with a Z test, so it stays on the generic path (P7, "Not covered").

**Q5. Worker QoS and worker count.**
- *Change:* give the workers USER_INTERACTIVE QoS, and choose the count from hw.perflevel0 / hw.perflevel1 instead of "online less 3". EM_GS_THREADS still overrides.
- *Gain:* fewer efficiency-core stragglers on the Mac; mainly the iPhone. Not yet sized.
- *Effort:* hours. *Risk:* none for exactness.
- *Proof:* gs_world_test.c.
- *Done 2026-10-11 (Progress P3):* 6 workers on this Mac, USER_INTERACTIVE QoS; the band partition is left to M4.

**Q6. Build flags, last.**
- *Change:* try -O3, -mcpu and LTO.
- *Gain:* estimated 5-15 %.
- *Risk:* floating-point contraction. Pinned off since P1 (Progress); keep it pinned under any new flag, and never use -ffast-math.
- *Proof:* the full reference suite with EM_TEST_FULL=1, plus test_vu_host_lanes.py.
- *Done 2026-10-12 (Progress P9):* nothing kept. -O3, -flto and -mcpu draw the same bytes, but none shortened the tick: -O3 is 0.8-1.7 % faster on the DOF pass offline and about 0.3 ms slower on the game thread; LTO is 0.1-0.2 ms faster on the game thread and about 0.2 ms slower on the busiest worker; -mcpu=apple-m1 is already the macOS default, and -mcpu=apple-a12 (iOS 17's oldest chip) was no faster on the M1. The build stays -O2 -ffp-contract=off.

**Q7. Counters on texture_entry.**
- *Change:* count entries and misses per frame. If the 1024-entry cliff is real, replace the search with a hash.
- *Proof:* test_gs_memory_reference.py, test_gs_texture_reference.py.
- *Done 2026-10-12 (Progress P8):* counters behind EM_GS_TEX_COUNTERS; no cliff, but the search cost 0.5-0.9 ms of game-thread CPU per tick, so it is now a hash.

### Medium work

**M1. Separable swizzle tables.**
- *Change:* the PSMCT32 and PSMZ32 layouts split into a row part and a column part, wrapped by the existing 4 MiB mask. Compute rowtab[y] + coltab[x] for frame, Z and texel addresses.
- *Gain:* estimated 15-25 % of pixel cost. Calm busiest worker about 10 -> 8 ms; fire about 14 -> 11-12 ms.
- *Effort:* 2-4 days. *Risk:* low; part A of the raster test decodes through the measured swizzle maps.
- *Proof:* the GS proof set.
- *Done (Progress P11):* PSMCT32 / Z32 (Z24) frames and Z buffers, PSMCT32 / T8 / T4 textures and the CT32 CLUT loads, the layouts the counts found in the pixel path. The same bytes (the proof set, `make test-gs-swizzle`'s 2.15 billion addresses, part A against the measured maps, a 62,130-primitive HEAD-against-M1 differential). The GS frame on one worker 15.7 % cheaper (about 28.5 ns a pixel); at 6 workers the busiest worker 1.3-1.9 ms lower in AREA11 (calm 11.0 -> 9.5 ms) and 2.1-2.7 ms in AREA01 (fire 14.9 -> 12.5 ms), which now runs at 98.8-99.8 % speed.

**M2. Per-state specialized span loops.**
- *Change:* hoist the state switches out of the pixel loop and inline plot, sample and fetch. Write dedicated loops for the depth-of-field copy and blend, the snow and fire STQ PSMT8 / CLUT32 sprites, and the Z-tested textured triangles. Keep the generic fallback for every other state.
- *Gain:* estimated 1.5-2x on the remaining pixel path.
- *Effort:* 1-2 weeks. *Risk:* medium.
- *Proof:* the GS proof set, plus a new differential dev check (generic against specialized) with EM_TEST_FULL=1.

**M3. Decoded texture cache.**
- *Change:* keep a linear 32-bit copy, palette applied, per (TEX0, CLUT, CLAMP). Invalidate it on upload, on CLUT load and on draws onto its blocks; the depth-of-field render-to-texture case invalidates at the barriers.
- *Gain:* estimated up to about 2x on textured pixels.
- *Effort:* 1-2 weeks. *Risk:* medium.
- *Proof:* gs_world_test.c (it has the barrier cases), test_gs_texture_reference.py, the GS proof set.

**M4. Parse the command stream once, and assign bands dynamically.**
- *Change:* decode once and bin primitives by band; workers pull bands from a queue or by work stealing.
- *Why it stays exact:* each band is still drawn by one worker, in primitive order.
- *Gain:* the duplicated front end is about 13 ms of the 67 ms all-worker sum at 7 workers (measured basis), about 1.5-1.8 ms of busiest worker, plus part of the idle and straggler time.
- *Effort:* 1-2 weeks. *Risk:* medium.
- *Proof:* gs_world_test.c, raster test part F with EM_TEST_FULL=1.

### Large work

**L1. NEON SIMD in the specialized loops.**
- *Gain:* estimated 2-4x on those loops.
- *Effort:* weeks. *Risk:* medium to high.
- *Proof:* the same set, plus the M2 differential check.

**L2. Optional: an exact GS model as Metal compute.**
- The GS pixel pipeline is integer arithmetic, so it can run exactly on the GPU in principle. stq_div would need an exact formulation, because Metal's float division is not correctly rounded.
- *Effort:* months. *Risk:* high. Only if L1 does not reach the phone's budget.

### Where this lands (estimate)

- **Pixel cost.** Q3 + M1 + M3 + M2 take it from 36 ns to roughly 10-15 ns.
- **Single-core frame time.** That gives about 15-25 ms for AREA11 and about 25-35 ms for the fire, against 52-77 ms today.
- **Mac, 7 workers.** The busiest worker drops to about 3-5 ms in AREA11 and 5-7 ms in the fire.
- **iPhone, 3 workers.** AREA11 fits the period.
- **Is that realistic?** The fork already draws the same frames at 8.5-13 ms of single-core GS CPU, as Rosetta-translated code. That is evidence the hardware can do it, not a promise.
- **AREA01** also needs Q1 and Q2.

**Order:** Q1-Q5, then M1, M3, M2, M4, then L1.

## 5. iPhone Air (A19 Pro)

- **Cores.** 2 performance + 4 efficiency, so worker_count was 6 - 3 = 3. Since Progress P3 it is 4: the performance cores less two leave none, so every core less two.
- **QoS.** Before P3 only the game thread was USER_INTERACTIVE (em_platform_ios.m). The workers had default QoS, could land on efficiency cores, and shared the 2 performance cores with the game thread. Since P3 the workers are USER_INTERACTIVE too. With 4 workers on 6 cores, most of them still run on efficiency cores, and the equal bands then wait for those (P3, "The band partition").
- **Measured upper bound** (3 workers on M1 P-cores), every tick over:

  | Scene | Wall per tick | Game speed |
  |---|---|---|
  | Cinematic | 19.9 ms | about 84 % |
  | Slide | 22.4 ms | about 74 % |
  | AREA01 beat | 27.4 ms | |
  | Fire | 29.4 ms | |
  | Fire peak | 33.3 ms | about 50 % |

- **After the quick wins** (Progress P10, the same 3 workers on M1 P-cores): cinematic 19.0 ms (about 88 %), slide 20.7-21.0 ms (about 80 %), AREA01 beat 25.1 ms, fire 26.8 ms, fire peak 30.3 ms (about 55 %). Still over on nearly every tick.
- **After M1** (Progress P11, the same proxy): busiest worker cinematic 16.1-16.2 ms (97 % speed, 568-582 of 1,310 ticks over), calm 99 %, slide 17.5-17.9 ms (77-89 %), AREA01 beat 20.4 ms (79 %), fire 21.7-21.8 ms (74-75 %), fire peak 24.6-24.7 ms (65-67 %).
- **Per-core speed.** A19 Pro P-cores are faster per core than the M1 Pro's (IOS.md world steps p50 6.99 vs 7.52 ms, before the GS frame), but the E-cores are much slower. On balance the device is probably worse than these numbers; not measured on it.
- **Stale doc.** IOS.md "Frame time" (2026-10-04, 7.6 ms headroom) predates the GS frame on main (2026-10-08) and the depth-of-field pass (2026-10-10).
- **Thermals.** The Air is a thin phone. Sustained all-core load will throttle; not measured.

**What to do:**
1. **User decision:** default to, or offer prominently, EM_GPU_RENDERER=1 on the phone until the exact path fits. It is not exact, so it is a profile-policy decision for LAUNCHER_OPTIONS.md. Nothing has been changed.
2. **Q5:** worker QoS and a perflevel-based count. Done (P3: 4 workers by default); still to measure 2-5 workers on the device.
3. **M4** dynamic bands, so E-cores do not set the frame time.
4. **Q2** frees P-core time. Done (P5): 1.1-1.5 ms of game thread per tick on the Mac.
5. **M1, M3, M2 and L1** are the only route to an exact Original profile at full speed on the phone. Estimate: GS CPU must drop 2-3x for AREA11 and 3-4x for the fire.

## 6. Changes that relax exactness (Enhanced or GPU renderer only)

These change what is displayed. Each is a user decision.

- **The GPU renderer** (exists; fast, 0 ticks over; not exact).
- **Skip or cheapen the depth-of-field pass.** Measured -35 % of GS CPU, but the picture changes.
- **Decouple drawing from the tick or skip frames.** The original never shows a skipped frame.
- **Lower or dynamic internal resolution.**
- **Fewer or cheaper particles.** This also changes game content; not recommended even for Enhanced.

None is needed on the Mac. They are stop-gaps for the phone only.

## 7. What to measure next

1. **Windowed Mac EM_FRAME_TIMING against headless** in the same windows. Done (Progress P2). Not measured: a 60 Hz display, and AREA01 windowed.
2. **On the iPhone Air,** headless offscreen:
   - EM_GS_THREADS 2-5, with and without worker QoS;
   - a 10-15 minute sustained run for thermals;
   - the slide and fire windows.
3. **texture_entry counters** in the slide and the fire. Done (Progress P8).
4. **Pixel share per primitive class** (DOF, snow, fire, world, clears), to rank M2's loops.
5. **After each item,** re-run cine / calm / slide / AREA01 beat / fire / peak at 7 and 3 workers on a quiet machine, recording load. Keep a table of ns per pixel, busiest worker, main thread and ticks over. Done for the quick wins as a whole (Progress P10).
6. **Efficiency-core placement** of the workers under background load (per-worker CPU spread as a proxy).

## Progress

Each step of section 4's plan, with its measured before / after. Receipts: build/perf_investigation/ (runs/fpc_*, fpcontract/; runs/pres_*, present/; runs/q5_*, runs/q5w_*, qos/; runs/q1_*, q1/; runs/q2_*, q2/; runs/q3_*, q3/; runs/q4_*, runs/q4r2_*, runs/q4c_*, q4/; runs/q7_*, runs/q7c_*, runs/q7k_*, q7/; runs/q6_*, runs/q6r2_*, q6/; runs/sum_*, summary/; runs/m1*, m1/).

### P1. Floating-point contraction pinned off (2026-10-11)

**The hazard (the skeptic's finding).** The Makefile's CFLAGS had no -ffp-contract, so each compiler used its own default. Apple clang contracts a * b + c inside one expression into a fused multiply-add on arm64, GCC contracts even across statements, and a baseline x86-64 build has no FMA instruction at all. The same source therefore built different arithmetic on the Mac, the iPhone, Linux and Windows.

**Every fused site** (build/perf_investigation/fpcontract/survey.sh compiles each of the 336 macOS and iOS sources both ways; count.py counts fused instructions per function; counts only):
- 915 fused instructions in 29 of the 336 files. The other 307 objects are byte-identical with and without the pin.
- The GS model (em_gs_raster.c, 13 in draw_one): the STQ sprite's S and T interpolation, the triangle's Z row start and in-row Z, the barycentric weights and the S / T / Q sums. em_gs_world.c (4): only the timing clock's nanosecond conversion.
- The Metal backend's overlay geometry (em_gfx_metal.m, 74: arcs, rects, glyphs, triangles, scissor), which the Original profile uses for the 2D overlay pass over the field.
- Legacy and glue code: em_camera.c (195), em_enemy.c (108), em_render_frame.c (104), em_level_smoke_test.c (66), em_door.c (57), em_player_pose.c (48), em_camera_live.c, em_status_models.c (36 each), em_game.c (31), em_player_pose_host.c (24), em_player_frame.c (22), em_player.c (19), em_pickup.c (16), em_collision.c (15), em_hud.c (13), em_page_draw.c (9), em_model.c (5), em_opening_control_test.c (4), the audio mixers (em_sfx.c, em_sfx_bank.c, em_startup_audio.c, em_iop_stream.c: 2 each), em_examine.c, em_player_damage.c, em_status_hub_ui.c (2 each), em_input.c (1, the host stick's radius).
- One explicit fmaf() stays, by design: uv_axis_settled in em_gs_raster.c tests whether a fused start would change a UV sprite's texels, so that strict mode can refuse it (GS_EXACT.md 3.4).

**Which form is the original's.** Neither form is measured to be the original's: the proof set does not tell them apart.
- Each form was built and run through the whole GS proof set with EM_TEST_FULL=1: the raster reference (931 / 1158 bit-exact, 0 failures, the same count in every group), gs_world_test, the DOF pass (8 captures bit-exact), fog, GS memory and the GS texture reference. Both forms gave the same output line for line.
- The full level smoke (404 PASS lines, the side runs included), the AREA01 smoke and the fb2 harness (8 points, the same exact-match counts) also passed both ways, with identical output.
- The 8 fb2 frames the port drew, both the GS field and the presented frame, are pixel-identical between the two builds.
- In the main level smoke's 13,813-tick trace, every game-state field is identical. Only the host-timed counters differ (the stream counter, the sound sample counter, the loader's pre-state), and they also differ between two runs of one binary.
- The other tests that compile a fused file also gave the same results both ways: collision, continue reset, input block, AREA11 SFX, AREA01 assets, object unit GPU, overlay blend, static world GPU and player states host.

**Decision: -ffp-contract=off for every build.**
- Each a * b + c rounds twice, exactly as the source is written. That is what every host computes, and it is what the PS2's EE FPU and VU do (they have no fused form).
- The project's original-function tests already pinned it off: 45 Makefile test rules and 226 tool scripts, plus #pragma STDC FP_CONTRACT OFF in 12 translation units.

**Where it is pinned:**
- The Makefile's CFLAGS: macOS, Linux and the iOS library (make ios-lib).
- The GS proof set's own library builds (the raster, DOF, memory and fog tests) and the other tests that compile a fused file. The test-gs-world, test-frame-input and test-player-states-host rules.
- The Xcode project's OTHER_CFLAGS (Debug and Release), and tools/ios/build.sh's movie tool.
- Windows has no project yet. The Makefile comment records what it needs: MSVC /fp:precise without /fp:contract (VS 2022 or later), or clang-cl /clang:-ffp-contract=off.
- Never -ffast-math.
- Not covered: the runtime-compiled Metal shaders keep Metal's default fast-math. The Original profile's world frame is drawn on the CPU and does not use them, but the overlay pass and the GPU renderer do. test_gs_fog_conformance.py checks the shader fog against the C rules (16,384 of 16,384 exact). This is a separate open item.

**Cost** (quiet-machine gate: CPU idle 60-79 % before each run, 1-minute load 5.6-16.5; runs/fpc_*; same scenes as section 2):

| Measure | Before (contracted) | After (-ffp-contract=off) |
|---|---|---|
| 1 worker, GS frame CPU, cinematic | 52.14 / 52.08 ms | 53.21 / 53.12 ms (+2.0 %) |
| 1 worker, GS frame CPU, calm | 52.44 / 52.18 ms | 53.44 / 52.92 ms (+1.4-1.9 %) |
| 7 workers, busiest worker, cinematic / calm / slide | 11.20 / 11.53 / 11.87 and 10.79 / 11.15 / 11.52 ms | 11.52 / 11.93 / 12.59 and 11.62 / 11.48 / 11.73 ms |
| 7 workers, ticks over, cinematic / calm / slide | 679 / 12 / 39 and 148 / 12 / 14 | 362 / 15 / 41 and 315 / 9 / 8 |
| Main thread per tick (cinematic, 7 workers) | 8.70 / 8.65 ms | 8.65 / 8.72 ms |
| AREA01 fire, 7 workers: busiest worker / ticks over | 16.22 ms / 316 of 376 | 14.55 ms / 161 of 376 |

- The single-core GS cost rises by about 1 ms per frame (+2 %), because draw_one's 13 fused operations become separate multiplies and adds.
- The 7-worker and AREA01 figures move by less than the run-to-run noise at this load: the two "before" runs differ by 0.4 ms busiest and 148 to 679 ticks over.
- The main thread is unchanged.
- These multi-worker runs were noisier than section 2's (wall means 13-19 ms against 11-12), so they are not comparable with section 2's baseline. Compare only the interleaved pairs.
- Expected: a pin is a fidelity fix, not a speed-up. Every later raster refactor (Q3, M1, M2, L1) must now keep each expression's unfused rounding.

### P2. The windowed presentation path (2026-10-11)

The user approved a visible window for this measurement. Every windowed run used EM_MUTE=1 (new, see below) and lasted under 90 s, and the window was closed after each run. Receipts: build/perf_investigation/runs/pres_*; scripts in build/perf_investigation/present/ (run.sh, present.py).

**What was measured.**
- EM_FRAME_TIMING now ends every step with "p display_wait_ns field_wait_ns" (when the backend reports them):
  - the time the game thread was blocked in nextDrawable;
  - the time it waited for the GS model's field before committing a field frame.
- Its exit summary adds a "blocked on the display" line.
- A windowed run with EM_FRAME_TIMING prints the window's settings once ("present:"). Here:
  - 3 drawables, display sync on, a 1920x1440 drawable;
  - the built-in display: 120 Hz at most, refresh interval 8.33-41.67 ms (ProMotion).
- The workload: the demo replay to the hill slide (rec counter 4650), paced at 59.94 Hz as a player runs it, 7 workers. The step wall time excludes the pacing sleep.
- Speed = n x 16.68 / sum(max(wall, 16.68)), the skeptic's measure.

**The cause.** em_gfx_begin_frame took the frame's drawable from nextDrawable at the top of every step. A field frame then kept it un-presented until the next step's kick (gsw_complete).
- So when a step started, one drawable was held by the game and the two presented before it were usually still queued or on screen.
- nextDrawable then blocked until the display released one.

**The fix** (em_gfx_metal.m):
- Every frame is now drawn into the offscreen texture, in windowed runs as headless runs always did.
- present_commit takes the drawable only when the frame is committed (gsw_complete for a field frame, end_frame otherwise). It copies the frame into the drawable with a blit (same BGRA8 format and size, byte for byte), presents it and commits.
- The game thread no longer holds a drawable while it builds a frame.
- On macOS the drawable takes the frame's size, and a fully covered or minimized window presents nothing (nothing of it is visible, and the layer may then hold its drawables for up to a second). The frame is still drawn and committed.
- The field presentation (a) and SCREEN ADJUST are untouched: the field and overlay passes draw exactly as before, into the offscreen target.
- iOS uses the same code (em_gfx_metal.m). A drawable whose size differs from the frame's (only while the view is resized) is not presented. The changed files compile warning-free for iphoneos; not run on the device.

**Before / after** (rec counter ranges, movie steps excluded; mean ms per tick, ticks over 16.68 ms, speed):

| Scene | Headless, before | Windowed, before | Windowed, after | Headless, after |
|---|---|---|---|---|
| Cinematic (1,310 ticks): wall / over / speed | 10.48 / 158 / 98.8 % | 12.14 / 195 / 98.2 % | 10.89 / 208 / 98.3 % | 10.27 / 124 / 99.1 % |
| Cinematic: blocked on the display, mean (p95, max) | 0 | 2.52 (6.71, 10.28); 761 ticks over 1 ms | 0.04 (0.05, 0.07); none over 1 ms | 0 |
| Calm (85): wall / over / speed | 10.09 / 0 / 100 % | 11.89 / 8 / 98.8 % | 11.43 / 9 / 99.0 % | 10.45 / 5 / 99.8 % |
| Calm: blocked on the display | 0 | 1.97 (3.26, 9.43); 74 ticks over 1 ms | 0.04 (0.05, 0.06); none | 0 |
| Slide (121): wall / over / speed | 13.40 / 36 / 94.5 % (113 ticks) | 17.25 / 43 / 92.3 % | 12.96 / 38 / 96.3 % | 11.37 / 18 / 97.5 % (99 ticks) |
| Slide: blocked on the display | 0 | 5.47 (7.59, 13.98); 120 of 121 ticks over 1 ms | 0.04 (0.06, 0.06); none | 0 |
| Slide: waiting for the GS field | 3.90 | 2.27 | 3.41 | 1.91 |

The headless runs lost their last timing lines when the run was stopped, so they have fewer slide ticks. Main-thread CPU was 9.2-9.8 ms per tick and the busiest worker 10.9-12.6 ms in every run.

**Machine load.** The 70 % idle gate held for the two "before" runs (idle 70.4-70.6 %, 1-minute load 7.0-9.1). It was not reached for the two "after" runs (idle 61.8-65.0 %, load 5.2-9.4): fileproviderd at about one core and a repeating xctest at about 0.6 core. Ticks over the period and the speeds move with that load. The display wait is not affected by it.

**Reading.**
- On this Mac's 120 Hz display, the windowed path cost 2.0-5.5 ms of every tick before the fix. Most of that time the game thread was blocked in nextDrawable, and the slide's ticks were 3.9 ms longer than headless.
- After the fix the wait is 0.04 ms, and the slide's windowed tick (12.96 ms) is back at the headless level (11.4-13.4 ms).
- The game thread now reaches the kick sooner and waits there for the GS field instead (slide 2.27 -> 3.41 ms). The GS workers are again the critical path, as headless.
- It was not the 15 ms per tick IOS.md measured on a 60 Hz panel: at 120 Hz the display releases a drawable every 8.33 ms. On a 60 Hz display the old wait would have been longer; the fix removes the mechanism there too. Not measured: no 60 Hz display was attached.
- The remaining slow motion in these runs is the GS frame under load (section 2 and the appendix), not the window.

**The presented pixels.** A scratch build (present/verify) read back every presented drawable and its offscreen frame in the same command buffer. In a windowed run to the end of the calm (counter 1780), 1,800 presented frames, 0 differ, 1,608 of them not all black (runs/pres_verify_win). The offscreen frame is the target headless runs and every capture-based test already draw and check.

**The proof set** (EM_TEST_FULL=1, receipts in present/proof/):
- The GS unit tests give P1's results line for line: the raster reference 931 / 1158 bit-exact with 0 failures; gs_world_test PASS; the DOF pass 8 captures bit-exact; fog 16,384 / 16,384; GS memory PASS; and gs_display_test PASS (the field placement and SCREEN ADJUST).
- The full level smoke gives the same 404 PASS lines as P1. The AREA01 smoke passes (20 live phases).
- The fb2 harness passes at 8 of 19 points.
  - Its port frames are now 1920x1440 where P1's were 960x720. The headless offscreen target follows the window's backing scale on the host's current display, and that changes between sessions.
  - At that size the presented frame at 15_level_exit has 113,053 exact pixels (P1: 113,002). It is the only figure that moved.
  - The binary from before the fix, run at 15_level_exit on the same display, gives the identical result (113,053). So the presentation change did not move it.
- Not runnable, and already failing at HEAD with the same undefined em_gfx_gs_display_release: test-fade and test_main_loop_and_gap_frame_audit_reference.py (the earlier note lists test-frame-input and test_status_hub_visual).

**EM_MUTE=1** (developer variable; em_audio_muted in em_platform.h). The audio device stays open and the game's mixer runs at the device's pull cadence, as when audible, so timing does not change. The backend zeroes what the mixer wrote, as headless runs always did, and the movie player is muted (AVPlayer.muted keeps its audio clock). macOS and iOS.

**Not measured.** AREA01 windowed: the level smoke's route to the fire takes 4-5 minutes, longer than the approved window time. The fix removes the same per-tick wait there. Also not measured: the iPhone, and a 60 Hz display.

### P3. GS worker count and QoS (Q5) (2026-10-11)

**The change** (src/gs/em_gs_world.c, worker_count and the worker threads):
- EM_GS_THREADS still overrides (1..16).
- Otherwise, on Apple hosts, the count comes from the core types: hw.perflevel0.logicalcpu (performance) and hw.perflevel1.logicalcpu (efficiency). Two cores are left as headroom: one for the game's thread, one for the audio, movie and window threads.
  - If the performance cores less two still give 4 workers, the workers stay on performance cores. This Mac (8 + 2): 6 workers, where it had 7.
  - Otherwise every core less two draws. iPhone Air (2 + 4): 4 workers, where it had 3. An M1 with 4 + 4 cores: 6.
  - At most 8, as before.
- Hosts that report no core types (Intel Macs, Linux) keep the old count, the online processors less three. Windows has no worker threads yet.
- The workers get USER_INTERACTIVE QoS on Apple hosts. That is the class of the macOS main thread, which runs the game there, and of the iOS game thread. Before, they had the default class, one step below the game's thread.
- The bytes cannot change: the count and the QoS only decide where the same bands are drawn. gs_world_test.c now also runs the host default (6 here) next to 1, 2, 3 and 8 workers, and all must equal 1.

**Headless.** The demo replay to rec counter 4650 (demo.sh, uncapped). Five configurations, interleaved over five rounds (runs/q5_*; qos/batch.sh, qos/batch2.sh; table qos/table.txt).
- Before = HEAD's binary at its default (7 workers, default QoS). After = this step's binary at its default (6 workers, USER_INTERACTIVE). Also this step's binary at 7 and 8, and HEAD's binary at 6, to separate the count from the QoS.
- Each cell: the median over the runs of the per-run means, with the range. Tick wall ms / ticks over 16.68 ms / speed / busiest worker's CPU ms.

| Configuration (runs) | Cinematic (1,310 ticks) | Calm (85) | Slide (121) |
|---|---|---|---|
| Before: HEAD, 7, default QoS (3 clean of 5) | 14.86 (13.72-17.44) / 212 (120-727) / 94.8 % / 11.17 | 13.73 / 4 / 99.8 % / 11.01 | 17.20 (15.98-17.32) / 44 (41-64) / 93.1 % / 12.28 |
| HEAD, 6, default QoS (2) | 14.13 / 195 / 98.2 % / 11.98 | 14.75 / 10 / 97.7 % / 12.14 | 15.67 / 41 / 96.0 % / 13.04 |
| Q5, 8, USER_INTERACTIVE (2) | 14.11 / 116 / 99.5 % / 10.39 | 14.38 / 9 / 99.3 % / 10.43 | 17.45 / 60 / 90.1 % / 11.48 |
| Q5, 7, USER_INTERACTIVE (5) | 13.59 (12.95-16.10) / 100 (57-444) / 99.2 % / 10.56 | 13.17 / 2 / 99.8 % / 10.69 | 16.87 (14.10-18.33) / 61 (16-79) / 93.2 % / 12.11 |
| **After: Q5 default, 6, USER_INTERACTIVE (4 clean of 5)** | **13.46 (12.46-13.99) / 79 (22-175) / 99.3 % / 11.62** | **12.88 / 0.5 / 99.9 % / 11.59** | **14.00 (13.76-17.36) / 18 (8-73) / 98.5 % / 12.66** |

- **Not running when it should.** The GS frame's wall time minus its busiest worker's CPU time is time the frame's critical worker was not running: preempted, or waiting at a barrier for another worker (qos/lost.py, lost.txt). Median per run, cinematic / slide:
  - after (6, USER_INTERACTIVE): 0.65-1.90 / 0.64-3.78 ms;
  - 7, USER_INTERACTIVE: 2.22-4.67 / 2.24-5.47 ms;
  - 8: 3.42-3.63 / 3.72-6.44 ms;
  - before (HEAD, 7): 2.65-4.77 / 2.17-5.06 ms, and 9.9 and 23.8 ms in the cinematic during the two load spikes.
- **Paired by round,** the new default beat both 7-worker configurations in the cinematic and the slide in every round but two: the one it ran at 6 % idle, and the slide of round e (17.36 against 16.87 for Q5 at 7).

**Windowed** (the user's approval for a visible window; muted with EM_MUTE=1; about 80 s each; closed after each run). present/run.sh, paced at 59.94 Hz to counter 4650 on the built-in 120 Hz display (runs/q5w_*).

| Run (order) | Cinematic: wall / over / speed | Calm | Slide | Display wait |
|---|---|---|---|---|
| Before, run 1 (first; started in the tail of a load spike, 1-minute load 181) | 17.18 / 588 / 80.3 % | 11.81 / 14 / 98.8 % | 27.16 / 121 / 61.4 % | 0.04 ms |
| After, run 1 | 8.86 / 0 / 100 % | 9.28 / 0 / 100 % | 9.21 / 0 / 100 % | 0.04 ms |
| After, run 2 (first) | 8.82 / 0 / 100 % | 9.37 / 0 / 100 % | 9.48 / 0 / 100 % (112 ticks) | 0.04 ms |
| Before, run 2 | 9.73 / 46 / 99.4 % | 10.52 / 8 / 99.8 % | 9.62 / 0 / 100 % | 0.02-0.04 ms |

- Paced, the tick's wall time is the game thread's work, because the frame drawn by the workers overlaps the next tick.
- With the new default, no tick of either run went over. Before, 46 and 8 ticks went over on the quieter run, and the run started in the load spike lost 20-39 % of game speed.
- The game thread also waited less for the GS field before its kick: the field wait fell from 0.40 / 0.70 ms to 0.01 / 0.08 ms. Its CPU per tick fell from 9.29-9.81 ms to 8.78-9.23 ms.

**AREA01** (a01.sh, one pair; the fire is GS-bound and the beat is main-thread-bound; runs/q5_a01_*). Before / after: busiest worker 16.82 / 16.69 ms in the fire.

| Range | Before: wall / over / speed | After |
|---|---|---|
| Beat (781 ticks) | 21.78 / 668 / 75.9 % | 18.41 / 536 / 87.8 % |
| Fire (376) | 23.74 / 373 / 70.3 % | 19.36 / 313 / 85.5 % |
| Fire peak (149) | 26.06 / 149 / 64.0 % | 21.26 / 149 / 78.5 % |

The main thread used 15.05 / 14.42 ms of CPU per tick in the beat. Both runs are slower than P1's AREA01 runs (fire 94.4 %), because the machine was busier, so only the pair is comparable.

**Machine load.** The quiet gate (70 % idle) was not reached for every run. Another job's Swift test runner kept starting, and the 1-minute load jumped to about 250 at 9:20 and again at 9:50. Every run's idle and load are in its load.txt; the table above marks what was left out:
- after, round c (6 % idle before the run);
- before, rounds a and e (the two spikes: cinematic 27.2 and 56.8 ms per tick).

All other runs started at 26-73 % idle and a 1-minute load of 9-20. Because of this load, compare the interleaved runs with each other, not with section 2.

**Reading.**
- The new default draws the frame faster on this Mac, although each worker draws more (busiest worker 11.6 ms at 6 against 10.6 at 7). With 7 workers, the game's thread and the audio, movie and window threads need more than the 8 performance cores, and the background load (fileproviderd at about one core, WindowServer, mediaanalysisd) takes more. A preempted worker holds up the frame and each of the 8 depth-of-field barriers. Six workers leave room for them.
- The QoS alone, at 6 workers: the cinematic's tick 14.13 -> 13.46 ms, the slide's 15.67 -> 14.00 ms (2 and 4 runs; the same direction in each).
- 8 workers is the worst setting, as section 3 found.
- The busiest worker's CPU is unchanged by the QoS at equal count. QoS changes where and when the workers run, not their work.
- The iPhone is not measured. 4 workers is a reasoned default, not a measured one: EM_GS_THREADS overrides it for the device sweep (section 7, item 2).

**The band partition (the skeptic's point).** Not changed in this step, left to M4.
- **On this Mac:** the new default keeps every worker on a performance core: 6 workers plus the game thread on 8 performance cores. The measured lost time above is now 0.6-1.9 ms in the cinematic.
- **On the iPhone:** the 4 workers run mostly on efficiency cores, and equal bands make the frame wait for the slowest one. There, the partition still matters.
- **Why no cheap exact fix.** Every worker replays every register write of the frame, because a band's state must equal one EmGs's. So a band cannot simply be handed to whichever worker is free:
  - With more bands than workers, each band would need its own replay of the stream. That is about 2 ms of CPU per band and frame (section 3, item 4).
  - At each of the 8 depth-of-field barriers every band must be done, so bands cannot be pulled across a barrier without one EmGs per band.
  - A weighted static partition (more rows for performance-core workers) needs to know which worker runs on which core type. macOS and iOS do not expose that, and thread affinity is not available.
- **M4 is the right place.** Once the stream is decoded once and binned by band, a free worker can pull a band cheaply and still draw it in primitive order.

**The proof set** (EM_TEST_FULL=1, the after build; receipts in build/perf_investigation/qos/proof/):
- The GS unit tests give P2's results line for line: the raster reference 931 / 1158 bit-exact with 0 failures; the DOF pass 8 captures bit-exact; fog 16,384 / 16,384; GS memory PASS; gs_display_test PASS.
- gs_world_test passes with the new host-default run: 1, 2, 3, 8 and the default (6) workers draw the same fields and memory.
- The fb2 harness passes at 8 of 19 points, with every exact-pixel count equal to P2's.
- The full level smoke gives the same 404 PASS lines as P1 and P2, with no failures.
- The AREA01 smoke passes (20 live phases).

### P4. The diagnostic digests gated (Q1) (2026-10-11)

**What they were.** Several live draw modules hashed what they had just drawn, on every frame, in normal play. The hashes go into log structs whose only reader is the scene tick log (em_scene_bindings.c, written when EM_AREA_CHANGE_LOG names a file). tools/test_level_smoke.py and level_smoke_chain_page.py rebuild them from the original and compare. They never change what is drawn or what the game does. The modules found:
- in the profile: the chain page (em_chain_page_live.c: the primitives' digest, computed twice, with and without the unit CALLs, and the depth-of-field pass's digest); the static world (em_static_world_live.c: the run's bytes and its triangles); the owner units (em_owner_draw_live.c: the face unit, the pose, the colour, position, lighting and rig rows); the effects' barrel lanes and glow sprites (em_effects_live.c: bitwise CRC-32 over about 24 KiB a frame, inlined into em_effects_live_001F0360);
- by reading the code, smaller: the background grid (em_background_live.c), the load veil (em_load_veil_live.c), the snow's packet 3 (em_snow_runtime.c, 256 bytes), the effect overlay's packets 1 and 4 (em_effects_live.c) and the AREA01 dynamic table on each 001C1D00 call (em_render_context_live.c).

Not gated: the audio digests (em_sfx.c, em_iop_stream.c). They run on the mixer, about 6,400 multiplies per tick, and are not in any profile.

**The switch** (src/game/em_diag.h, em_diag_digests()). The digests are computed only when EM_AREA_CHANGE_LOG names a file, or with EM_DIAG_DIGESTS=1 (a developer variable: the digests without the log, to measure their cost; STARTUP.md "Developer switches"). Every harness that reads them already sets EM_AREA_CHANGE_LOG: the level smoke and its side runs, the AREA01 smoke, the fb2 harness, and the area-load, room-move and rand-order references. Without the switch the digest fields stay 0. Everything else in the log structs is still filled, and every check the draws make (the fault paths) is unchanged.

**The harnesses still see them.**
- A negative control (scratch build, not kept): the switch always off, run through the level smoke to battery with its tick log. tools/test_level_smoke.py fails at the first digest it checks (check_background, port tick 80: the original grid program's digest over the port's upload against 0). The real build passes the same run.
- The proof set below passes with the build's own logs, so the smoke's digest checks ran and matched: the background grid, the static world (level_smoke_static_world.py), the chain page and its pass (level_smoke_chain_page.py), the owner units, the barrel lanes and glow markers, the snow's packet 3 and the flame's packets.
- test_chain_page_reference.py does not use the live digests: it compiles em_chain_page.c into its own library. It passes unchanged.

**AREA01** (a01.sh to a01_00, headless, uncapped, the default 6 workers; ABBA order: HEAD, Q1, Q1, HEAD; then the Q1 binary with EM_DIAG_DIGESTS=1; runs/q1_a01_*). Mean ms per tick; "main" is the game thread's CPU.

| Range | HEAD a / b: main / wall / over / speed | Q1 a / b: main / wall / over / speed | Q1 with EM_DIAG_DIGESTS=1: main |
|---|---|---|---|
| Beat (781 ticks) | 14.10 / 14.09; 16.32 / 16.34; 369 / 369; 95.9 / 95.8 % | 12.18 / 12.30; 15.93 / 16.46; 367 / 389; 95.7 / 94.2 % | 14.11 |
| Fire (376) | 13.97 / 13.96; 16.99 / 16.95; 196 / 195; 94.8 / 95.0 % | 12.01 / 12.11; 16.81 / 17.07; 193 / 199; 94.7 / 93.7 % | 13.99 |
| Fire peak (149) | 13.54 / 13.53; 18.65 / 18.63; 142 / 142; 89.4 / 89.5 % | 11.64 / 11.73; 18.73 / 19.01; 141 / 139; 89.0 / 87.7 % | 13.53 |

The busiest worker was the same in every run: 15.1-15.4 ms in the beat, 16.3-16.4 ms in the fire, 18.2-18.3 ms at the peak.

**AREA01 on the GPU renderer** (EM_GPU_RENDERER=1, one pair; here the game thread is the whole tick): the beat 13.69 -> 11.78 ms per tick (1 -> 0 ticks over), the fire 13.57 -> 11.65 ms, the peak 13.14 -> 11.28 ms.

**AREA11** (demo.sh to counter 4650, one pair; runs/q1_demo_*). The main thread per tick: cinematic 8.61 -> 7.98 ms, calm 9.13 -> 8.25, slide 8.93 -> 8.12. The tick: 11.91 -> 11.89, 12.40 -> 11.98, 12.99 -> 12.95 ms; ticks over 6 / 7 / 4 -> 5 / 0 / 0. The busiest worker is unchanged (11.4 / 11.4-11.6 / 12.4-12.5 ms).

**The profile** (runs/q1_a01_q1_sample: the Q1 build in the AREA01 fire, 3 s; against runs/sample_a01_fire). No digest function appears: digest_more (8.6 % of the main thread's busy samples before), prim_digest (2.6 %) and the owner draw's fnv (0.9 %) are gone, and em_effects_live_001F0360's own time (1.1 % before) left the top 25. The game thread now waits for the GS frame in 25.0 % of its samples (10.9 % before).

**Machine load.** Idle before each run 66.7-77.6 % (one run, Q1 b, started at 66.7 % after quiet.sh's 3-minute limit); 1-minute load 7.7-17.5. Runs 12:09-12:41.

**Reading.**
- The game thread saves 1.9 ms per tick in AREA01 (14.1 -> 12.2 ms, -13 %) and 0.6-0.9 ms in AREA11. The same binary with EM_DIAG_DIGESTS=1 gives HEAD's figure, so the whole difference is the digests. The estimate was 1.5 ms (AREA01) and 0.2-0.35 ms (AREA11), from sample shares; both were low.
- On the GPU renderer, where the game thread is the tick, AREA01's tick is 1.9 ms shorter (13.7 -> 11.8 ms).
- With the GS frame at 6 workers, AREA01's tick did not get shorter. Here the busiest worker (15.1-18.3 ms) is longer than the game thread (12-14 ms), so the workers set the tick. The skeptic's "the main thread gates AREA01" held at 7 workers on a quiet machine (section 2); it does not at today's default and load. The saving becomes tick time where the game thread is the longer side: the GPU renderer, the iPhone (where the game thread shares the performance cores with the workers), and after the GS-side items (M1-M4).
- The AREA11 ticks over the period fell (17 -> 5 in one pair), but that is within the run-to-run noise at this load.

**The proof set** (EM_TEST_FULL=1, the Q1 build; receipts in build/perf_investigation/q1/proof/):
- The GS unit tests give P3's results line for line: the raster reference 931 / 1158 bit-exact with 0 failures; gs_world_test PASS (1, 2, 3, 8 and the default 6 workers); the DOF pass 8 captures bit-exact; fog 16,384 / 16,384; GS memory PASS; gs_display_test PASS.
- test_chain_page_reference.py passes (mode full: 15 random, 15 weather and 15 captured pages).
- The fb2 harness passes at 8 of 19 points, with every exact-pixel count equal to P3's.
- The AREA01 smoke passes (20 live phases).
- The full level smoke gives the same 404 PASS lines as P1-P3, with no failures. Its side runs each write and check their own tick log, so the digest checks ran in every one.

### P5. Host floats in the VU1 page programs (Q2) (2026-10-11)

**What changed.** Every chain-page VU1 program (lane, sprite, snow, kind 6, streak, kind 2, the background grid, the AREA01 floor and ripple) now runs through one entry, em_vu1p_mscal (em_vu1_page_programs.h), which has two arithmetic paths:
- `host` 0, the model: every lane operation on em_ee_float.h's integer arithmetic, as before.
- `host` 1, the live path (every em_vu1_*_program_mscal): the program runs inside em_vu_host_enter / em_vu_host_leave, in one EM_VU_HOST_NOINLINE function. A multiply, add or subtract of finite operands is the host lane (em_vu_host_lanes.h). VDIV and the EFU's ERCPR use a new host divide, emvuh_div. An operation with an exponent-255 operand still runs on the model, so its result, and the fault it causes, are the model's.

**The exactness trap (the skeptic's flag).** The programs contain double-precision math (the EFU's ERLENG model) and call the XGKICK consumer, which must not run with round-toward-zero and flush-to-zero switched on. Instead of splitting each program around its arithmetic, both leave the environment through EM_VU_HOST_NOINLINE trampolines (emvup_erleng_outside, emvup_kick_outside), which restore the caller's environment, do the work and re-enter before they return. The program's own code is integer work plus the host lanes: in the compiled object the functions under emvup_program_host contain only single-precision add, subtract, multiply and divide instructions and register moves; every double conversion, square root and compare is in emvup_erleng_double (counted in a local object, not kept).

**The divide.** emvuh_div is the IEEE quotient rounded toward zero with flush-to-zero and denormals-are-zero; a zero or denormal divisor returns the model's +-MAX itself. That is the truncated quotient em_eei_quotient computes, by the same argument as the multiply's. tools/test_vu_host_lanes.py now covers it against em_vu_div_bits (0, 0): zero and denormal operands, quotients at the underflow, unit and overflow edges for every exponent pair difference, a / a and its neighbours, and random operands. Full mode: 8,187,909 operations, 0 mismatches.

**The whole-program proof** (tools/test_vu_page_host_differential.py, `make test-vu-page-host-differential`). Each MSCAL runs on the host path (kicks to the real consumer), then on the model path from a copy, with the host run's kick results replayed. Result code, all 16 KiB of data memory, every register and every kick (address, and the data memory at the kick) must agree. Each kick consumer must see the caller's FP environment, and the environment must be restored on return. Full mode (6.6 s): the 15 AREA11 route pages with the weather's 108 snow MSCALs each; 5 kind-6 and 17 floor / ripple AREA01 pages; 6 grid uploads plus 64 swept views; 3,000 synthetic MSCALs from the reference tests' generators; 13,500 fuzzed MSCALs (exponent-255 words, denormals, signed zeros, extreme values, consumers that refuse a kick). 20,305 MSCALs in all, including 3,520 operand faults and 2,256 refused kicks, all identical. Two negative controls fail as they should (scratch builds, not kept): the host path without entering the environment (111 MSCALs differ on the first page), and a kick trampoline that does not leave it (113 kicks see the wrong environment).

**The reference tests, on the host path** (EM_TEST_FULL=1; receipts in q2/proof/):
- test_chain_page_reference.py: PASS (690 lane, 660 sprite, 1,920 snow, 600 streak and 600 kind-2 MSCALs against the original microcode; 15 captured pages' primitives).
- test_snow_tiles_reference.py, test_level2_kind6_vu_reference.py (1,209 kind-6 MSCALs), test_level2_floor_vu_reference.py (17 AREA01 captures, 5,338 synthetic batches), test_background_reference.py (6 captures, 64 sweep views): PASS.
- make test-chain-page (ASan / UBSan): PASS.

**Offline cost per MSCAL** (the differential's --bench, one captured starting state each, best of two 400-run means; model -> host): sprite 6.5 -> 1.4 us, snow 16.8 -> 3.3, kind 6 17.2 -> 4.9, streak 10.7 -> 2.9, kind 2 19.2 -> 4.0, floor 6.8 -> 2.4, ripple 24.8 -> 7.9, grid 178 -> 78. The weather's 108 snow MSCALs a frame go from about 1.8 to about 0.36 ms. The grid gains least: its 1,024 ERLENGs each switch the environment twice.

**Live** (headless, uncapped, the default 6 workers; interleaved: HEAD, Q2, Q2, HEAD; runs/q2_*). Idle before each run 70.8-79.9 %, 1-minute load 5.6-10.5, 14:59-15:19. Mean ms per tick.

AREA01 (a01.sh to a01_00):

| Range | Game thread CPU, HEAD a / b | Game thread CPU, Q2 a / b | Tick wall, HEAD / Q2 | Over, HEAD / Q2 | Busiest worker, HEAD / Q2 |
|---|---|---|---|---|---|
| Beat (781 ticks) | 12.29 / 12.17 | 11.18 / 11.08 | 16.25, 15.82 / 16.02, 15.69 | 394, 357 / 382, 359 | 15.25, 15.11 / 15.21, 15.11 |
| Fire (376) | 12.12 / 12.00 | 11.01 / 10.92 | 17.24, 16.73 / 16.88, 16.71 | 218, 187 / 199, 189 | 16.42, 16.27 / 16.33, 16.28 |
| Fire peak (149) | 11.71 / 11.63 | 10.64 / 10.58 | 19.17, 18.64 / 18.77, 18.66 | 145, 139 / 140, 136 | 18.36, 18.16 / 18.22, 18.20 |

AREA01 on the GPU renderer (EM_GPU_RENDERER=1, one pair; the game thread is the tick): beat 12.05 -> 10.77 ms, fire 11.92 -> 10.62, peak 11.55 -> 10.22; no tick over in either.

AREA11 (demo.sh to counter 4650): the game thread, cinematic 7.96 / 7.93 -> 6.50 / 6.52 ms, calm 8.21 / 8.21 -> 6.78 / 6.77, slide 8.10 / 8.14 -> 6.61 / 6.60. The tick's wall time is unchanged (cinematic 11.78-11.85, calm 11.72-11.79, slide 12.90-13.00 ms both ways), as is the busiest worker (11.3 / 11.3 / 12.4 ms); one slide tick over the period in one HEAD run, none otherwise.

**The profile** (runs/q2_a01_q2_sample: the Q2 build, AREA01 counters 15306-15557, 5 s). The page programs have nearly left the game thread: emvup_floor_vertex 1.1 % of its busy samples, the ERLENG trampoline 0.9 %. Before (runs/q1_a01_q1_sample, the fire), em_eei_exact_sum alone was 9.5 %, a quarter of it from the floor program. The largest self time is now em_vu_vec_bits (17.5 %: corner, rotate, vu_row), the VU0 hot spots Q2's "also check" names; not changed in this step.

**Reading.**
- The game thread saves 1.1 ms per tick in AREA01 (-9 %) and 1.4-1.5 ms in AREA11 (-18 %). The estimate was 1-2 ms in both.
- With the GS frame at 6 workers the tick is set by the busiest worker in both levels, so the Mac's tick does not change (AREA01 ticks over the period within run-to-run noise). The saving is tick time where the game thread is the longer side: on the GPU renderer (AREA01 -1.3 ms per tick), and on the iPhone, where the game thread shares the two performance cores with the workers (not measured).
- Together with Q1 (P4), AREA01's game thread went from 14.1 to 11.1 ms per tick.

**The GS proof set** (EM_TEST_FULL=1, the Q2 build; receipts in build/perf_investigation/q2/proof/):
- The GS unit tests give P4's results line for line (only timings and scratch paths differ): the raster reference 931 / 1158 bit-exact with 0 failures; gs_world_test PASS; the DOF pass 8 captures bit-exact; fog; GS memory; gs_display_test.
- The fb2 harness passes at 8 of 19 points, with every exact-pixel count equal to P4's.
- The AREA01 smoke passes (20 live phases).
- The full level smoke gives the same 404 PASS lines as P1-P4, with no failures; its chain-page, snow, flame and background-grid digest checks ran on the host path.

### P6. Per-column U in sprites (Q3) (2026-10-11)

**What changed** (em_gs_raster.c, draw_sprite).
- A sprite's texel column coordinate U depends only on the column x and on the primitive. For UV it is the corners' U through floordiv64; for STQ it is S interpolated at t = (16x - X0) / (X1 - X0), then stq_div by the second vertex's Q.
- It used to be recomputed for every pixel of every row. It is now computed once per column into a table, with the same expression, and each row reads it. The table has at most 2,048 entries, the scissor's width. V is still computed per row, as before.
- A band computes the table on the first row it owns, so a worker that draws no row of the sprite computes nothing.
- -ffp-contract=off (P1) still keeps the STQ expression's product and sum rounded separately.

**Same bytes.**
- *The GS proof set* (EM_TEST_FULL=1, the Q3 build; receipts in build/perf_investigation/q3/proof/) gives P5's results line for line; only timings and scratch paths differ:
  - the raster reference: 931 / 1158 bit-exact, 0 failures;
  - gs_world_test: PASS;
  - the DOF pass: 8 captures bit-exact;
  - fog, GS memory and gs_display_test: PASS;
  - the fb2 harness: 8 of 19 points, every exact-pixel count equal to P5's;
  - the AREA01 smoke: 20 live phases;
  - the full level smoke: the same 404 PASS lines as P1-P5, no failures.
- *A random differential* (q3/sweep/sweep.c, built once against HEAD's em_gs_raster.c and once against Q3's):
  - 1,500 seeded batches of 40 random textured sprites each, over random local memory. The sprites cover UV and STQ, corners in either order, sub-pixel positions, random scissors, Q from 0.01 to 4, nearest and bilinear sampling, the clamp / repeat / region modes, the Z test on or off, and alpha blending.
  - Each batch is drawn by one EmGs and by 3 and 8 row-band EmGs in lockstep.
  - All 4,500 hashes of the 4 MiB local memory are equal (60,000 sprites, 245.8 million pixels).
  - Two negative controls (scratch copies, not kept) each change the last column's U by one. The STQ change alters 54 of the 90 lines of a 30-batch run, the UV change 60.
- *The DOF captures:* the offline bench below also compares the memory. After the pass, all 4 MiB of local memory are equal between HEAD and Q3 in each of the 8 captures, at 1, 3 and 8 bands.

**Offline** (q3/bench/bench.py). The DOF pass of each of the 8 fork captures runs through dof_replay (one EmGs, strict), with HEAD's library and Q3's alternated, best of 30.
- Per pass: HEAD 20.4-22.2 ms, Q3 18.8-20.7 ms.
- All 8 captures: 170.6 -> 157.9 ms (-7.5 %). GS_EXACT.md 10.2's scratch variant had measured -6 %.

**Live.** Headless and uncapped. Each round runs HEAD, Q3, Q3, HEAD (binaries q3/head_extermination and q3/q3_extermination; runs/q3_* and runs/q3r2_*). Mean ms per tick.

*Round 1* (q3/batch.sh, 18:40-19:18) ran on a **busy** machine. quiet.sh's 70 % idle was reached before only 4 of 12 runs (idle 55.2-73.4 %, 1-minute load 5.7-13.5). Another job's xctest and fileproviderd ran beside them. Only its one-worker runs are used below.

One worker (demo.sh to counter 1780, EM_GS_THREADS=1). The GS frame's CPU per frame, the cleanest meter:

| Range | HEAD a / b | Q3 a / b | Change |
|---|---|---|---|
| Cinematic (1,310 ticks) | 54.02 / 53.16 | 51.44 / 51.54 | -2.1 ms (-3.9 %) |
| Calm (the first 41-75 ticks) | 53.56 / 52.33 | 51.42 / 51.24 | -1.6 ms |

*Round 2* (q3/batch2.sh, 20:51-21:08) ran at the default 6 workers. Idle before each run was 70.9-77.4 %, and every run passed the gate. The 1-minute load was 6.7-11.1, on the tail of an earlier spike (15-minute load 75 -> 36).

AREA11 (demo.sh to counter 4650). Each cell: busiest worker CPU / tick wall / ticks over:

| Range | HEAD a, b | Q3 a, b | Busiest worker |
|---|---|---|---|
| Cinematic (1,310) | 11.34, 11.31 / 11.82, 11.75 / 0, 1 | 11.07, 11.05 / 11.54, 11.50 / 1, 1 | -0.26 ms |
| Calm (85) | 11.32, 11.31 / 11.73, 11.72 / 0, 0 | 11.06, 11.05 / 11.46, 11.50 / 0, 0 | -0.26 ms |
| Slide (121) | 12.44, 12.43 / 12.95, 12.89 / 0, 0 | 11.87, 11.86 / 12.31, 12.30 / 0, 0 | -0.57 ms |

All four runs ran at 100 % speed in every range.

AREA01 (a01.sh to a01_00; all four runs passed the smoke's 21 live phases). Each cell: busiest worker CPU / tick wall / ticks over / speed:

| Range | HEAD a, b | Q3 a, b | Busiest worker |
|---|---|---|---|
| Beat (781) | 15.26, 15.18 / 16.55, 15.85 / 402, 355 / 92.4, 95.2 % | 14.08, 14.12 / 15.00, 15.17 / 194, 249 / 98.1, 97.5 % | -1.1 ms |
| Fire (376) | 16.41, 16.41 / 17.89, 17.06 / 214, 188 / 89.6, 93.2 % | 14.99, 15.09 / 15.61, 16.07 / 99, 140 / 97.7, 96.4 % | -1.4 ms |
| Fire peak (149) | 18.40, 18.53 / 20.99, 19.47 / 147, 144 / 79.5, 85.7 % | 16.62, 16.74 / 17.06, 17.76 / 79, 104 / 95.1, 92.8 % | -1.8 ms |

The game thread's CPU is unchanged in both rounds (AREA11 6.4-6.8 ms, AREA01 10.6-11.3 ms).

Round 1's six-worker runs (runs/q3_demo_*, runs/q3_a01_*, under the load above) point the same way, with more spread: busiest worker cinematic -0.15 ms, slide -0.8 to -1.4 ms, AREA01 -1.2 to -1.9 ms.

**Reading.**
- *Exactness.* Nothing drawn changes. The proofs above are the evidence, not the label.
- *One worker.* The GS frame costs 2.1 ms (3.9 %) less in the cinematic.
- *AREA11 at 6 workers.* The cinematic and calm gain 0.26 ms of the busiest worker, close to the skeptic's 0.25 ms. The slide gains 0.57 ms, and its tick is 0.6 ms shorter (12.9 -> 12.3 ms).
- *AREA01.* The fire gains far more: 1.1-1.8 ms of the busiest worker. There the workers set the tick (P4), so the tick follows. Ticks over the period fall by about half (beat 355-402 -> 194-249, peak 144-147 -> 79-104), and the fire peak's speed rises from 80-86 % to 93-95 %.
- *Why the slide and the fire gain most.* The report classifies their snow and fire sprites as STQ sprites (section 4, M2). For those, each removed per-pixel step was a double interpolation, a divide and stq_div, not just an integer divide. This step did not count pixels per sprite class.
- *What is left.* AREA01's peak still runs over the period on most ticks (busiest worker 16.6-16.7 ms). The remaining large items are unchanged: the texel fetch / sample (M2 and the early Z test) and the swizzle tables (M1).

### P7. A fast path for trivial-state sprites (Q4) (2026-10-11)

**Which sprites.** A per-state census (a measurement-only build, q4/census/, that times each drawn primitive by its state; one worker, the demo to counter 1780, the same 18,000,000 primitives in every run) found these untextured sprites in the AREA11 frames:
- *The field's Z clear:* one 512x224 sprite (114,688 pixels) in each in-level frame, with ZTST ALWAYS and an alpha test that never passes with AFAIL ZB_ONLY, so only Z is written. 1,397 in the window, 160.2M pixels.
- *Full clears:* 256 sprites that write colour and Z, no alpha test (29.4M pixels).
- *Colour fills:* 1,410 sprites of 16,384 pixels, Z masked (23.1M pixels).
- Two single sprites of the same kind.

Through plot() they cost 10.7-13.0 ns a pixel: 2.65 s of the run's 73.8 s of GS CPU (3.6 %).

**What changed** (em_gs_raster.c, sprite_fill, called by draw_sprite).
- A sprite with no texture, no DATE, no Z test that compares (ZTE 0 or ZTST ALWAYS) and no active blend takes every pixel through plot() with the same values. The colour (with fog at the sprite's one fog weight), the alpha test's outcome, the source word and its write mask, and the Z value are all per-primitive.
- sprite_fill computes them once, by plot()'s rules, then writes each pixel in plot()'s order: rows top to bottom, each left to right, the frame word before the Z word. Frame and Z buffers that share memory are therefore written exactly as before. It counts the pixels plot() counts.
- Every other state returns 0 and draws through plot(), unchanged. plot()'s source is untouched (see "Round 1" below for why).

**Same bytes.**
- *The GS proof set* (EM_TEST_FULL=1, the final build; receipts in build/perf_investigation/q4/proof/) gives P6's results:
  - the raster reference: 931 / 1158 bit-exact, 0 failures, its log line for line equal to P6's apart from timings;
  - gs_world_test: PASS, its log equal to P6's;
  - the DOF pass: 8 captures bit-exact;
  - fog, GS memory and gs_display_test: PASS;
  - the fb2 harness, the AREA01 smoke and the full level smoke: see "Long proofs" below.
- *A random differential* (q4/sweep/sweep.c, built once against HEAD's em_gs_raster.c and once against Q4's):
  - 1,500 seeded batches of 40 random primitives, mostly untextured sprites, over random local memory. Every state the fast path reads is random: the frame (CT32 / CT24 / CT16 / CT16S, FBMSK) and Z (Z32 / Z24 / Z16 / Z16S, ZMSK) formats, a Z buffer that sometimes shares the frame's memory, ZTE / ZTST, ATE / ATST / AREF / AFAIL, DATE, ABE / PABE / ALPHA, FBA, COLCLAMP, fog and FOGCOL, SCANMSK, DTHE, the scissor and sub-pixel corners. About one primitive in ten is textured or a triangle strip.
  - Each batch is drawn by one EmGs and by 3 and 8 row-band EmGs in lockstep. 45 % of the sprite draws took the fast path; the rest are its fallbacks.
  - All 4,500 hashes of the 4 MiB local memory and every drawn-pixel count are equal (65,824 primitives, 332.0M pixels at one band).
  - Three negative controls (scratch copies, not kept) each break one rule of the fast path: the Z24 write's kept top byte, the SCANMSK row skip, the FBMSK merge. They change 66, 213 and 282 of the 300 lines of a 100-batch run.
  - The sweep's random frame widths and Z bases overlap rows, so its band counts differ from each other; HEAD and Q4 agree line by line for each band count.

**Round 1 (superseded).** The first version also moved plot()'s alpha-test switch into the shared helper. That changed plot()'s compiled code, and in the census every class drawn through plot() got 2-3 % slower (the textured triangles 24.11 -> 24.73 s). The round-1 live runs (runs/q4_*) used that build and are not used below. The final code leaves plot()'s source as it was: its compiled code, and that of sample, fetch_rgba and span_end, compare equal to HEAD's instruction by instruction, addresses aside.

**Census, final code** (runs/q4c_*, HEAD, Q4, Q4, HEAD; idle 69-73 % before each run, 1-minute load 5.0-7.6):

| | HEAD a / b | Q4 a / b |
|---|---|---|
| The trivial sprites (212.9M pixels) | 2.650 / 2.662 s | 0.357 / 0.356 s |
| Everything else | 71.15 / 71.11 s | 71.17 / 71.18 s |
| All GS CPU | 73.80 / 73.78 s | 71.53 / 71.54 s (-3.0 %) |

Per pixel: the Z clear 12.65 -> 1.61 ns, the full clears 12.97 -> 2.58 ns, the colour fills 10.73 -> 1.19 ns.

**Live** (q4/batch2.sh, 22:41-23:04; each round HEAD, Q4, Q4, HEAD; headless, uncapped; binaries q4/head_extermination and q4/q4_extermination; runs/q4r2_*). Idle before each run 70.4-77.6 %; the 1-minute load was 4.6-9.5 for AREA11 and 8.1-13.4 for AREA01. Mean ms per tick.

One worker (demo.sh to counter 1780, EM_GS_THREADS=1), the GS frame's CPU per frame:

| Range | HEAD a / b | Q4 a / b | Change |
|---|---|---|---|
| Cinematic (1,310 ticks) | 50.60 / 50.55 | 49.30 / 49.65 | -1.1 ms (-2.2 %) |
| Calm | 50.06 / 49.86 | 48.90 / 48.88 | -1.1 ms |

Default 6 workers, AREA11 (demo.sh to counter 4650). Each cell: busiest worker CPU / tick wall / ticks over:

| Range | HEAD a, b | Q4 a, b | Busiest worker |
|---|---|---|---|
| Cinematic (1,310) | 11.07, 11.06 / 11.57, 11.56 / 0, 0 | 10.81, 10.81 / 11.33, 11.32 / 2, 1 | -0.26 ms |
| Calm (85) | 11.08, 11.11 / 11.55, 11.64 / 0, 0 | 10.80, 10.79 / 11.23, 11.33 / 0, 0 | -0.30 ms |
| Slide (121) | 11.89, 11.86 / 12.37, 12.35 / 0, 0 | 11.60, 11.62 / 12.08, 12.09 / 0, 0 | -0.27 ms |

All four runs ran at 100 % speed in every range. The game thread is unchanged (6.5-6.8 ms).

AREA01 (a01.sh to a01_00; every run passed the smoke's live phases). Each cell: busiest worker CPU, beat / fire / fire peak:
- HEAD a, b: 14.03 / 15.01 / 16.64 and 14.07 / 15.02 / 16.69.
- Q4 b: 14.02 / 14.97 / 16.60.
- Q4 a was disturbed and is not used: its busiest worker was 14.7 ms, but its tick took 25.6 ms (771 of 781 ticks over), so the workers were off-core.
- An extra pair at load 11-12 (runs/q4r2_a01_q4_c, q4r2_a01_head_c): Q4 14.25 / 15.22 / 16.93, HEAD 14.52 / 15.48 / 17.32.
- So the change in AREA01 is between 0 and -0.4 ms, inside the run-to-run spread at this load.

**Long proofs** (q4/proof_long.sh, the final build, EM_TEST_FULL=1):
- the fb2 harness: 8 of 19 points, every exact-pixel line equal to P6's;
- the AREA01 smoke: 20 live phases, none not live;
- the full level smoke: the same 404 PASS lines as P6, no failures (89 minutes).

**Reading.**
- *Exactness.* Nothing drawn changes. The proofs above are the evidence.
- *Gain.* The trivial sprites are 7.4 times cheaper. One worker's GS frame is 1.1 ms cheaper; at 6 workers the busiest worker is 0.26-0.30 ms cheaper in AREA11 (the report's estimate was about 0.2 ms) and the tick 0.24-0.31 ms shorter. AREA01 shows no change outside the noise.
- *Not covered: gsw_alpha_clear.* It is not a sprite: it is a 4-vertex triangle strip (PRIM 0x044, blend on) in state block (2,9), and the drop shadow's boxes follow it in the same state (GS_EXACT.md section 9).
  - The census has exactly one untextured triangle class with blending: A = Cd, B = 0, C = FIX 128, D = 0 (which leaves the colour equal to Cd), ATST NEVER with AFAIL FB_ONLY, and ZTST GEQUAL. That is the strip and the boxes.
  - Its Z test can fail, so it falls outside this step's contract and stays on the generic path.
  - It costs 3.73 s of the 73.8 s (5 %), with 198M pixels drawn.
  - A fast path that keeps the per-pixel Z test and skips the rest would be exact for it. It is a candidate for M2 (specialized span loops).
- *A lesson for M2.* Any edit to plot()'s source can change its compiled code and slow every class (round 1). Compare plot()'s object code with HEAD's and measure per class with the census.

### P8. Counters on texture_entry, and a hash for its search (Q7) (2026-10-12)

**What texture_entry is.** The GS frame's recorder runs on the game thread. For every textured primitive it checks that the texture reads only uploaded or drawn GS memory, and lists the drawn blocks it reads, so the workers can be ordered by barriers. The answer for each (TEX0 without CLD, CLAMP_1) pair is kept in a table of up to 1,024 entries (CHECKED_MAX). The table is cleared by an upload, a memory restore, or a draw over an uploaded block. The lookup was a linear scan of that table. Past 1,024 entries a new texture would be checked again at every kick: the "cliff".

**The counters** (em_gs_world.c; EM_GS_TEX_COUNTERS=<file>, a developer switch, STARTUP.md "Developer switches"). One line per frame handed to the workers: searches, probes, hits, misses, misses past the table's end, texels the misses walk, table resets, the entries and pool blocks held, and the time of the hits and of the misses. The counts are always kept (a few integer adds per lookup); the file and the two clock reads per lookup only run with the switch. Counts are the same in every run of the same route.

Linear search (q7/ctr_extermination, HEAD plus the counters; runs/q7c_demo_ctr, q7c_demo_ctr2, q7c_a01_ctr). Means per frame:

| Range | Searches (max) | Entries | Entries compared (max) | Misses | Hit time, linear -> hash |
|---|---|---|---|---|---|
| AREA11 cinematic (1,310 frames) | 12,211 (22,540) | 345 | 1.82M (3.41M) | 0.00 (max 1) | 1,086-1,098 -> 305 us |
| AREA11 calm (85) | 12,557 (20,680) | 345 | 2.00M (3.28M) | 0 | 1,154-1,231 -> 308 us |
| AREA11 slide (121) | 10,544 (11,409) | 347.5 | 1.54M (1.66M) | 0.01 | 915-919 -> 273 us |
| AREA01 beat (781) | 11,680 (15,500) | 338 | 2.16M (2.55M) | 0.11 (max 12) | 1,198 -> 297 us |
| AREA01 fire (376) | 11,041 (12,628) | 347 | 2.13M (2.43M) | 0.00 | 1,176 -> 281 us |
| AREA01 fire peak (149) | 10,622 (11,054) | 347 | 2.05M (2.12M) | 0 | 1,149 -> 272 us |

- **No cliff.** At most 348 entries in the whole demo and the whole route to a01_00, so no lookup ever fell past the table's end. The table was reset 2 times in the demo and 4 times on the route.
- **Misses are rare and early.** At most 174 in one frame: the first in-level frame, which GS_EXACT.md 10.2 already measured at about 2.6 ms of residency checks. After that, 0 to 12 a frame. The 16 KiB memset per miss is therefore not a cost.
- **The search was the cost.** About 150 entries compared per lookup (about half the table), so 1.5-2.2M compares a frame. The hit times above include the two clock reads per lookup that both builds pay, about 0.25 ms of the hash's figure.

**What changed** (em_gs_world.c). The table keeps its entries, its order and its 1,024 cap; a 4,096-slot open-addressing index (linear probing, slot = index + 1) now finds them, keyed exactly as the scan was: TEX0 with CLD masked, and CLAMP_1. An entry is added only after a search found no equal key, so a key is in the table at most once, and the index finds what the scan found. It is cleared wherever the table is. A full table behaves as before: the texture is checked again, and nothing is stored. The hash averages 1.05 probes per lookup (12,813 for 12,211 searches in the cinematic).

**Same answers.**
- *A self-check build* (scratch, since deleted; q7/check_patch.py and q7/NOTES.txt rebuild it: the hash plus the old scan, aborting on any disagreement) ran the demo to counter 4650 and the route to a01_00: 225,859,109 lookups (43,178,026 + 182,681,083), 0 disagreements; the route's 21 live phases passed.
- *gs_world_test* gains a part past the table's end: the same frames with 1,100 extra texture keys (the resident texture under distinct REGION_CLAMP words, kicked by triangles of no area) draw the same fields and memory with 1 and 8 workers as without them, under the thread sanitizer. Its EM_GS_TEX_COUNTERS lines show the table at 1,024 and 113 lookups checked again.
- *The GS proof set* (EM_TEST_FULL=1, the Q7 build; receipts in q7/proof/) gives P7's results:
  - the raster reference: 931 / 1158 bit-exact, 0 failures; the DOF pass: 8 captures bit-exact; fog, GS memory and gs_display_test: PASS; each of these logs equal to P7's apart from timings;
  - the texture reference (test_gs_texture_reference.py, not in P7's set): PASS, 11,129 decodes equal;
  - gs_world_test: PASS, with the new part;
  - the fb2 harness: 8 of 19 points, every exact-pixel line equal to P7's;
  - the AREA01 smoke: 20 live phases, none not live;
  - the full level smoke: the same 404 PASS lines as P7, no failures (81 minutes).

**Live** (q7/batch.sh, 01:40-01:58; HEAD, Q7, Q7, HEAD; headless, uncapped, default 6 workers; binaries q7/head_extermination and q7/hash_extermination, the latter equal to build/extermination apart from its name; runs/q7_*). The machine was busy: idle 70.6-77.7 % before each run, but 1-minute load 10.0-31.1. Mean ms per tick, HEAD a / b -> Q7 a / b.

| Range | Game thread CPU | Busiest worker | Tick wall | Speed |
|---|---|---|---|---|
| AREA11 cinematic | 6.76 / 6.75 -> 6.07 / 6.08 (-0.68) | 11.25 / 11.17 -> 11.24 / 11.19 | 12.64 / 12.95 -> 12.88 / 13.01 | 99.2-99.8 -> 99.7-99.9 % |
| AREA11 calm | 7.03 / 7.39 -> 6.36 / 6.27 (-0.9) | 11.18 / 13.05 -> 11.34 / 11.13 | 12.33 / 16.66 -> 13.12 / 12.48 | 100.0 / 92.9 -> 100.0 / 100.0 % |
| AREA11 slide | 6.83 / 7.20 -> 6.36 / 6.28 (-0.7) | 11.91 / 12.65 -> 11.70 / 11.97 | 12.94 / 16.97 -> 13.74 / 14.03 | 99.9 / 93.9 -> 99.4 / 99.8 % |
| AREA01 beat (781) | 11.38 / 11.55 -> 10.56 / 10.62 (-0.88) | 14.13 / 14.48 -> 14.16 / 14.19 | 16.73 / 17.89 -> 16.04 / 16.07 | 92.5 / 88.7 -> 95.5 / 95.5 % |
| AREA01 fire (376) | 11.20 / 11.45 -> 10.41 / 10.47 (-0.89) | 15.11 / 15.52 -> 15.12 / 15.15 | 18.09 / 19.06 -> 16.87 / 16.85 | 88.9 / 86.8 -> 94.8 / 94.0 % |
| AREA01 fire peak (149) | 10.85 / 10.97 -> 10.05 / 10.19 (-0.79) | 16.83 / 16.88 -> 16.78 / 16.90 | 21.35 / 19.35 -> 18.36 / 18.69 | 78.0 / 85.9 -> 90.4 / 88.9 % |

HEAD b's AREA11 calm and slide were disturbed (its busiest worker 13.05 and 12.65 ms, 36 and 56 ticks over); its game-thread figures are kept above. Q7 a recorded 91 of the slide's 121 ticks before the stop. Against HEAD a alone the calm and slide differences are -0.7 and -0.5 ms.

**Reading.**
- *Exactness.* Only the lookup's data structure changed; the answers are the same (the self-check), and the proofs above give the previous step's results.
- *Gain.* 0.5-0.9 ms of game-thread CPU per tick in every scene measured, in line with the counters' 0.6-0.9 ms (the hit times' difference). In AREA11 the tick does not shorten, since the GS workers set it. In AREA01 the tick's wall time fell by 0.7-1.8 ms and the speed rose 3-12 points in these runs. The game thread shares the cores with the workers there, but the load was high, so take the size of that gain as uncertain.
- *Where it matters most.* On a host with fewer cores (the iPhone Air) the game thread's time competes directly with the workers.
- *Q7 is closed.* The counters stay, behind EM_GS_TEX_COUNTERS.

### P9. Build flags (Q6) (2026-10-12)

**Result: nothing kept.** The build stays -O2 -ffp-contract=off, with clang's default CPU and no LTO. Every variant tried draws the same bytes, but none shortened the tick, and the two that save time in one place lose it in another. The Makefile's CFLAGS comment records this.

**What was tried.** The same working tree (Q7's code, HEAD 39a9530), the -ffp-contract=off pin kept in every build, never -ffast-math. Builds: q6/build_variant.sh (the Makefile's command with -O2 replaced) and q6/gs3/cmd.txt.
- -O2 (base): its code is identical to build/extermination's.
- -O2 -mcpu=apple-m1: code identical to -O2. Apple clang already targets apple-m1 for arm64 macOS, and every Apple Silicon Mac has at least the M1's features, so the flag is a no-op there.
- -O3; -O2 -flto; -O3 -flto.
- gs3: em_gs_raster.c alone at -O3, the rest at -O2.
- iOS: the iOS target's default CPU is apple-a7. iOS 17 (IOS_MIN) runs on A12 and later, so -mcpu=apple-a12 is the tuning the project could use without dropping a device. It was measured on the M1 as a stand-in (the M1's cores are A14-class), offline only.
- Every binary has exactly one fused multiply-add instruction: uv_axis_settled's explicit fmaf() (counted, not listed). So no flag brought contraction back.
- Sizes: -O2 4.67 MB, -O3 4.97 MB, -O2 -flto 4.14 MB, -O3 -flto 4.22 MB.

**Same bytes.** The GS unit references with EM_TEST_FULL=1, their library builds' flags replaced by each variant's, in scratch trees (q6/flagproof/run.sh; logs in q6/flagproof/logs/): the raster reference (931 / 1158 bit-exact, 0 failures), the DOF pass (8 captures bit-exact), GS memory and fog all pass for -O3, -O2 -flto, -O3 -flto and -O2 -mcpu=apple-a12. Each log equals the -O2 log apart from timings. The offline bench (below) also checks that every library leaves the same GS memory as -O2's for all 8 DOF captures at 1, 3 and 8 bands. Since nothing was kept, the full proof set (fb2, level smokes) was not re-run: build/extermination is unchanged.

**Offline: the DOF pass** (q6/bench/bench.py; one EmGs, best of N per capture, the libraries alternated each iteration; bench.txt, bench2.txt). Sum over the 8 captures:

| Flags | Round 1 (best of 30) | Round 2 (best of 40) |
|---|---|---|
| -O2 | 161.6 ms | 157.8 ms |
| -O3 | 158.9 ms (-1.7 %) | 156.5 ms (-0.8 %) |
| -O2 -flto | 161.5 ms (-0.0 %) | 158.5 ms (+0.4 %) |
| -O3 -flto | 160.3 ms (-0.8 %) | |
| -O2 -mcpu=apple-a7 (iOS default) | 162.2 ms (+0.4 %) | |
| -O2 -mcpu=apple-a12 | 162.5 ms (+0.6 %) | |

-O3 was faster on every capture in round 1, by 0.06-0.58 ms. The CPU tuning makes no difference on these cores.

**Live** (q6/batch1.sh, batch2.sh; 04:31-06:01; headless, uncapped, interleaved ABCD DCBA; runs/q6_*, runs/q6r2_*). The machine was busy: another job's Swift test runner, and a load spike to 217 at 04:57. Idle before each run was 70-79 % for most runs. Ten runs started below 70 % idle (1.5-59 %) or in the spike: q6_t1_base_a, q6_t1_lto_a, q6_t1_o3lto_a / b, q6_demo_lto_b, q6r2_t1_gs3_a, q6r2_t1_o3_b, q6r2_demo_gs3_b, q6r2_demo_lto_b and q6r2_demo_o3_b. They are kept, marked * in the one-worker table and included in the 6-worker medians; leaving them out changes no conclusion. 1-minute load 3.9-81 outside the spike. Mean ms per tick.

One worker, the demo to counter 1780, GS frame CPU in the cinematic (1,310 frames):

| Build | Runs |
|---|---|
| -O2 | 50.24*, 50.08, 50.51, 49.99 |
| -O3 | 49.70, 49.57, 50.92, 49.13* |
| -O2 -flto | 50.92*, 50.20 |
| -O3 -flto | 50.11*, 50.88* |
| gs3 | 50.04*, 49.84 |

The differences are inside the run-to-run spread (up to 1.8 ms within one build here), which is wider than the 0.8-1.7 % (0.4-0.9 ms) the bench measures.

Default 6 workers, the demo to 4650, the AREA11 cinematic (1,310 ticks). Each cell: game thread CPU / busiest worker; median of the runs, then the range:

| Build (runs) | Game thread | Busiest worker | Tick wall |
|---|---|---|---|
| -O2 (4) | 5.95 (5.90-5.97) | 10.92 (10.88-11.06) | 11.69 (11.47-12.19) |
| -O3 (4) | 6.23 (6.18-6.28) | 10.87 (10.77-10.92) | 12.01 (11.49-12.43) |
| -O2 -flto (4) | 5.84 (5.75-5.91) | 11.12 (11.00-11.35) | 12.65 (11.69-13.29) |
| -O3 -flto (2) | 6.05 (5.99-6.10) | 11.07 (10.98-11.15) | 12.33 (11.87-12.78) |
| gs3 (2) | 5.95 (5.90-6.00) | 10.83 (10.77-10.88) | 12.13 (11.56-12.69*) |

The calm ranks the same way. In the slide, LTO's game-thread gain does not show.

AREA01 (a01.sh to a01_00, default workers; one run each in each half of the round). Game thread / busiest worker / tick wall:

| Build | Beat (781 ticks) | Fire peak (149) |
|---|---|---|
| -O2 | 10.23 / 13.74 / 14.35 and 10.45 / 13.89 / 14.90 | 9.72 / 16.36 / 16.81 and 9.97 / 16.56 / 17.75 |
| -O3 | 10.38 / 13.63 / 14.28 and 10.67 / 13.85 / 15.12 | 9.89 / 16.30 / 16.78 and 10.36 / 16.73 / 18.44 |
| -O2 -flto | 10.25 / 14.42 / 15.98 and 10.09 / 14.05 / 15.13 | 9.59 / 16.71 / 17.39 and 9.62 / 16.75 / 17.88 |
| gs3 | 10.37 / 13.99 / 15.24 and 10.36 / 13.81 / 14.82 | 9.86 / 16.38 / 17.19 and 9.75 / 16.32 / 16.72 |

**Reading.**
- *-O3:* the game thread is 0.15-0.33 ms slower per tick in every pair of runs (AREA11 cinematic +0.28 ms median, AREA01 beat +0.19 ms). The GS gain (0.8-1.7 % offline, about -0.05 ms of busiest worker live) does not show in the tick. Not kept.
- *LTO:* the game thread is about 0.1 ms faster in the AREA11 cinematic and 0.17 ms in the AREA01 beat. The busiest worker is 0.1-0.7 ms slower in every pair (demo and AREA01), and the tick is longer in 5 of the 6 pairs. On this Mac the workers set the tick, so LTO loses. Not kept.
- *gs3 (-O3 for the raster only):* it avoids -O3's game-thread cost. Its GS gain is the bench's 0.8-1.7 %, about 0.1 ms of busiest worker, below what these runs can resolve. It would need a per-file build rule and a per-file rule in the reference tests' builds. Not kept for now. Re-measure after M1 / M2, when the specialized span loops may give -O3's vectorizer something to work on.
- *-mcpu:* no-op on macOS. For iOS, apple-a12 tuning gave no gain on the M1 stand-in (offline +0.6 %). Not measured on the iPhone. Not kept.
- *Where the time is.* The build flags are worth at most about 1-2 % of the GS frame. The plan's medium items (M1-M4) remain the route to the large gains.
- *If a flag is adopted later:* the reference tests build their libraries with -O2 (test_gs_raster_reference.py, test_dof_pass_reference.py, test_gs_memory_reference.py and the others). They must then build with the shipped flags, or they no longer prove the shipped code.

### P10. The quick wins together: every scene against the report's baseline (2026-10-12)

**What was measured.** Every scene of section 2, before and after all of P1-P9, on two binaries:
- *Before:* the report's baseline, b9fd9f9 (port main 8b68485 plus this report), built from `git archive` with its own Makefile (summary/base_extermination). Its default is 7 workers at default QoS, with contraction unpinned.
- *After:* today's main, ec85b7e (summary/now_extermination, equal to build/extermination). Its default is 6 workers at USER_INTERACTIVE QoS.
- *Windowed "before":* the baseline binary has no EM_MUTE, so it would play the game's sound. The windowed before is present/before instead: the report's code plus P1's pin and P2's instrumentation and EM_MUTE, with the old presentation and 7 workers. P1 costs it about 2 % of GS CPU.

**Method.** demo.sh and a01.sh (headless, uncapped) and present/run.sh (windowed, paced at 59.94 Hz, muted with EM_MUTE=1, 80-93 s each, the window closed at each stop); summary/batch.sh, batch2.sh, win.sh; summary/summ.py (the GS steps in each range, movie steps excluded; the speed is section 7's measure). Before and after were interleaved: B N N B (batch.sh), then N B (batch2.sh), then one more B N demo pair (sum_demo_base_d, sum_demo_now_d). The baseline binary is rebuilt by `git archive b9fd9f9 src Makefile` into a scratch folder and `make all` there. summary/summary.txt holds every run's figures.
- *Load.* quiet.sh's gate (70 % idle) held before every run: idle 70.6-90.8 %, 1-minute load 3.5-19.5 at the start, 06:36-07:26. Another job's xctest (about 1.3 cores) and fileproviderd ran beside them.
- *Disturbed runs, left out:* sum_demo_base_b's calm and slide (a load step: 39 and 35 ms per tick, busiest worker 16 ms); and the sum_demo_base_c / sum_demo_now_c pair (the load stepped to 19-20 during it; both kept in runs/).
- *Every AREA01 run passed the smoke's 21 live phases.*
- Today's machine was busier than section 2's: the baseline's AREA01 beat ran 16.8-17.1 ms per tick here against 15.7 there. So compare the before and after columns with each other.

**Headless, default workers** (before 7, after 6). Each cell: tick wall / ticks over 16.68 ms / speed / game thread CPU / busiest worker CPU, mean ms per tick. Ranges over the clean runs (before: 2 runs, the cinematic 3; after: 3 runs; AREA01: 2 each).

| Scene | Before (b9fd9f9) | After (ec85b7e) | Change |
|---|---|---|---|
| Cinematic (1,310 ticks) | 12.09-12.47 / 42-100 / 98.3-99.6 % / 8.55-8.64 / 10.54-10.76 | 11.21-11.50 / 2-5 / 99.9-100 % / 5.79-5.90 / 10.81-10.92 | tick -0.8 ms; game thread -2.7 ms; busiest +0.2 ms |
| Calm (85) | 11.28, 14.64 / 1, 9 / 99.2-99.9 % / 8.96-9.12 / 10.19, 11.83 | 11.11-11.33 / 0 / 100 % / 5.97-6.13 / 10.78-10.85 | game thread -3.0 ms |
| Slide (121) | 12.75, 13.09 / 2, 14 / 99.2-99.9 % / 8.87-8.97 / 11.37, 11.58 | 12.00-12.27 / 0-1 / 100 % / 5.88-6.13 / 11.58-11.73 | tick -0.8 ms; game thread -2.9 ms |
| AREA01 beat (781) | 16.77-17.10 / 365-404 / 92.7-94.1 % / 14.20 / 14.20-14.26 | 15.87-16.69 / 310-398 / 94.1-96.2 % / 10.61-10.71 / 14.11-14.27 | game thread -3.5 ms; busiest unchanged |
| AREA01 fire (376) | 17.20-18.01 / 184-243 / 90.6-94.0 % / 14.06-14.08 / 15.09-15.16 | 16.06-16.84 / 142-183 / 95.1-95.7 % / 10.39-10.46 / 15.00-15.09 | tick -1.1 ms; game thread -3.6 ms |
| AREA01 fire peak (149) | 18.63-18.94 / 118-130 / 87.8-89.1 % / 13.62-13.63 / 16.73-16.81 | 17.66-18.27 / 98-119 / 90.6-93.3 % / 10.03-10.14 / 16.64-16.78 | tick -0.8 ms; game thread -3.5 ms |

**Headless, 3 workers** (EM_GS_THREADS=3, the iPhone proxy; AREA11 2 runs each, AREA01 one pair). Same cells.

| Scene | Before | After | Change |
|---|---|---|---|
| Cinematic | 19.96-20.24 / 1310 / 82.4-83.6 % / 8.55-8.61 / 19.54-19.66 | 19.00-19.05 / 1179-1190 / 87.5-87.7 % / 5.73-5.76 / 18.63-18.64 | busiest -1.0 ms (-5 %); speed +4.6 points |
| Calm | 19.83-19.90 / 85 / 83.8-84.1 % / 8.88-8.95 / 19.46 | 18.88-18.90 / 85 / 88.3-88.4 % / 5.91-5.92 / 18.54 | busiest -0.9 ms |
| Slide | 21.33-22.32 / 94-121 / 74.7-78.2 % / 8.75-8.93 / 20.92-21.95 | 20.69-20.95 / 113-121 / 79.6-80.6 % / 5.94-6.09 / 20.33-20.59 | busiest -0.9 ms |
| AREA01 beat | 27.64 / 756 / 60.3 % / 14.14 / 27.06 | 25.13 / 734 / 66.3 % / 10.43 / 24.70 | busiest -2.4 ms (-9 %) |
| AREA01 fire | 29.66 / 376 / 56.2 % / 14.01 / 29.07 | 26.79 / 376 / 62.3 % / 10.25 / 26.37 | busiest -2.7 ms |
| AREA01 fire peak | 33.53 / 149 / 49.8 % / 13.57 / 32.91 | 30.26 / 149 / 55.1 % / 9.92 / 29.82 | busiest -3.1 ms |

**One worker** (EM_GS_THREADS=1, the demo to counter 1780; the GS frame's CPU per frame, the cleanest meter; pairs B N at 06:45 and N B at 07:14):
- Cinematic: 51.18 -> 49.50 ms and 52.80 -> 50.49 ms (-1.7 and -2.3 ms, -3.3 and -4.4 %). About 34 ns per plotted pixel (1.47M a frame), against 35-36 before.
- Calm: 50.30 -> 48.10 and 52.28 -> 49.70 ms.
- This is P6 (-2.1 ms, Q3) and P7 (-1.1 ms, Q4) less P1's pin (+1 ms). Q5 and Q7 do not change it; Q1 and Q2 are game-thread work.

**Windowed, paced** (the built-in 120 Hz display, 3 drawables, a 1920x1440 drawable; the demo to 4650). Each cell: tick wall / ticks over / speed / blocked on the display / game thread CPU / busiest worker:

| Scene | Before, 7 workers | After, 6 workers | Before, 3 workers | After, 3 workers |
|---|---|---|---|---|
| Cinematic | 13.30 / 425 / 95.4 % / 2.89 / 9.32 / 11.98 | 5.94 / 0 / 100 % / 0.04 / 5.93 / 10.90 | 20.67 / 1310 / 80.7 % / 4.89 / 9.25 / 20.10 | 19.32 / 1304 / 86.4 % / 0.03 / 5.96 / 18.86 |
| Calm | 16.17 / 20 / 97.4 % / 5.34 / 9.83 / 11.88 | 6.08 / 0 / 100 % / 0.04 / 6.05 / 10.87 | 20.45 / 84 / 81.4 % / 4.92 / 9.72 / 20.00 | 18.98 / 82 / 87.0 % / 0.04 / 6.22 / 18.83 |
| Slide | 15.15 / 40 / 91.9 % / 3.02 / 9.52 / 12.38 (107 ticks) | 6.20 / 0 / 100 % / 0.04 / 6.19 / 11.73 | 22.60 / 107 / 73.8 % / 4.98 / 9.46 / 22.07 | 21.17 / 121 / 78.8 % / 0.03 / 6.19 / 20.72 |

Paced, a tick's wall time is the game thread's own work whenever the workers finish the previous frame within the period (the frame overlaps the next tick). So the after column's 6 ms is the game thread alone; before, the tick also held 2.9-5.3 ms of display wait and 1-2.6 ms of waiting for the field.

**Reading.**
- *On the Mac, at the default worker count, every AREA11 scene now runs at full speed*, headless and windowed: 0-5 of 1,310 cinematic ticks over, none in the calm, 0-1 in the slide. Windowed, no tick went over (before: 425 / 20 / 40, speed 92-97 %). The windowed path (P2) and the worker count and QoS (P3) did that: P2 removed the display wait, and P3 cut the time a frame's critical worker is off-core.
- *The busiest worker did not get cheaper at the default count.* Six workers each draw more than seven did (+1.0-1.5 ms, P3), and Q3 + Q4 give that back (P6, P7), so it stays at 10.8-11.7 ms in AREA11 and 14.1-16.8 ms in AREA01. On the Mac the tick is now set by it, not by the game thread.
- *The game thread is 2.7-3.0 ms cheaper per tick in AREA11 (8.6-9.1 -> 5.8-6.1 ms, -32 %) and 3.5-3.6 ms in AREA01 (13.6-14.2 -> 10.0-10.7 ms, -25 %)*: Q1 (P4), Q2 (P5) and Q7 (P8). On the Mac that gives the workers their cores and the window its slack; it does not shorten a GS-bound tick.
- *AREA01 is still GS-bound and still slow at times:* 90.6-96.2 % speed headless, 98-398 ticks over per range, the fire peak's busiest worker 16.6-16.8 ms, at the period itself. Its game thread (10-11 ms) is no longer the limit.
- *The iPhone proxy (3 workers) gained 3.6-6.1 points of speed* (AREA11 79-88 %, AREA01 55-66 %) and is still over the period on nearly every tick. Its busiest worker fell 0.9-1.0 ms in AREA11 and 2.4-3.1 ms in AREA01 (Q3 and Q4 at equal count). On the phone the game thread's 2.7-3.6 ms saving also frees performance-core time for the workers; not measured on the device.
- *The windowed 3-worker runs* follow the headless ones (the display wait is gone, 4.9 -> 0.03 ms); the tick is the busiest worker's.
- *Not measured here:* the GPU renderer, AREA01 windowed (the route takes 4-5 minutes), a 60 Hz display, and the iPhone itself.

**The iOS app builds with every change.** `tools/ios/build.sh clean`, then `tools/ios/build.sh device` (a full build: 334 sources for iphoneos, arm64, iOS 17, -O2 -ffp-contract=off; 0 warnings; the app built and signed), then `tools/ios/build.sh clean` again, so no app bundle with the disc's data stays on the Mac. Not installed or run on the phone.

**What is left for the medium items (M1-M4).** The quick wins are done: what remains is the GS model's pixel cost, about 34 ns a pixel and 49.5-50.5 ms of one core per AREA11 frame (section 4's target is 10-15 ns).
- *The Mac's AREA01 fire* needs the busiest worker 2-3 ms lower to stop running over the period: about 15-20 % of the GS frame. M1 (the separable swizzle tables, estimated 15-25 %) is the first item that is that large.
- *The phone* needs GS CPU 2-3x lower for AREA11 and 3-4x for the fire (section 5): M1, M3 (the decoded texture cache) and M2 (specialized span loops, including gsw_alpha_clear's Z-tested strip and an early Z test, P7), then L1.
- *M4* (parse the stream once, dynamic bands): the duplicated front end is still in every worker, and on the phone's efficiency cores the equal bands still wait for the slowest worker (P3, "The band partition").
- *Order unchanged:* M1, M3, M2, M4, then L1. Each must keep -ffp-contract=off's unfused rounding (P1), keep plot()'s compiled code under watch (P7), and pass the GS proof set with EM_TEST_FULL=1 and a HEAD-against-new differential (q3/sweep, q4/sweep).
- *Also open:* gs3 (-O3 for the raster only) after M1 / M2 (P9); the remaining game-thread hot spot em_vu_vec_bits (P5); the device sweep of EM_GS_THREADS 2-5 and a sustained thermal run (section 7, item 2).

### P11. Separable swizzle tables (M1) (2026-10-12)

**Which layouts.** A counting build (m1/count/, one worker, the demo to counter 1780, 18 million primitives) counted the addresses the pixel path computes:
- texel fetches: PSMCT32 4.61 billion, PSMT4 1.68 billion, PSMT8 0.68 billion; no other texture format;
- frame words: PSMCT32 only (1.90 billion through plot(), 53 million through the Q4 fill);
- Z words: PSMZ24 only (1.27 billion through plot(), 190 million through the fill), which uses the Z32 layout;
- CLUT loads: 885,476 loads, 15.3 million CT32 entries (0.2 % of the addresses).

**What changed** (em_gs_raster.c section 1b; GS_EXACT.md 2.0).
- These layouts are separable. The address is bp * unit + row[bw][y] + col[s][x], wrapped by the 4 MiB mask (unit: 64 words, 256 bytes or 512 nibbles a block). The row part holds the page row, the block row and the in-block row; the column part the page column, the block column and the in-block column. No part carries into another, and the block number's wrap at 16,384 blocks is the 4 MiB wrap of the sum.
- In PSMT8 / PSMT4 one column bit is flipped on alternate pairs of rows (the swapped 4-pixel halves). Those rows, s = ((y >> 1) ^ (y >> 2)) & 1, read a second column table. PSMCT32 and PSMZ32 have one.
- The tables are the existing address functions sampled: row[bw][y] is the address of (0, y) at block 0, and col[s][x] the address of (x, y_s) less that of (0, y_s), with y_0 = 0 and y_1 = 2. So they define no layout of their own.
- Each EmGs builds its own (EmGs.swz, heap; em_gs_release frees them): the column tables (48 KiB) on its first draw, and a width's row table (8 KiB) on its first use. There is no global state, and no worker shares a table.
- draw_one binds the frame (CT32 / CT24), Z (Z32 / Z24) and texture (CT32, T8, T4) tables to the primitive. plot(), sprite_fill, fetch_rgba and the CT32 CLUT load read their addresses through them. CT16 / Z16 buffers and the other texture formats keep the functions.
- Fail-stop: with no memory for a table the primitive is refused (EM_GS_REFUSE_MEMORY) and draws nothing. The GS world checks that bit in every worker, since one worker can meet it alone.
- -ffp-contract=off (P1) is unchanged; the change is integer address arithmetic only.

**The tables proven.**
- *Against the address functions:* tests/gs_swizzle_test.c (`make test-gs-swizzle`). It compares em_gs_swz_addr, the same inline code the pixel path runs, with em_gs_addr32 / em_gs_addr8 / em_gs_addr4 for the four layouts:
  - every x and y (0..2047) at every width 0..63 (default 8 widths), each with a base block that varies with x, y and the width;
  - every base block 0..16383, at 64 random positions and widths each, and (EM_TEST_FULL=1) at every x < 128, y < 64 for widths 1 and 8;
  - one million positions past 2047 (the functions wrap at 2048);
  - the CT24 / Z24 aliases.
  - EM_TEST_FULL=1: 2,153,286,588 addresses, 0 differ (25 s under ASan / UBSan). Default: 140 million, 2 s.
  - Negative controls in the test: the T8 / T4 sum with the other column table differs at 256 of 512 positions each, and the CT32 tables against the Z32 function at 512 of 512.
- *Against the measured maps:* test_gs_raster_reference.py part A now also compares the CT32 and Z32 tables with the layout batch's measured maps over eight buffers (the measured page, the 512- and 640-wide fields, the 2048-pixel width, the top page of local memory): 1,712,128 addresses, 0 differ. A first version of the check used a 4032-pixel width, which the GS wraps at 2048; it failed as it should, and the width was corrected.

**Same bytes.**
- *The GS proof set* (EM_TEST_FULL=1, the M1 build; receipts in build/perf_investigation/m1/proof/). Each log equals Q7's (q7/proof/) apart from timings and scratch paths, and the raster reference's one added line:
  - the raster reference: 931 / 1158 bit-exact, 0 failures;
  - gs_world_test: PASS at 1, 2, 3 and 8 workers and the host default (6), with the DOF pass and past the texture table's end;
  - the DOF pass: 8 captures bit-exact;
  - fog, GS memory and gs_display_test: PASS;
  - the texture reference: PASS, 11,129 decodes equal;
  - the fb2 harness: 8 of 19 points, every exact-pixel line equal to Q7's;
  - the AREA01 smoke: 20 live phases, none not live;
  - the full level smoke: the same 404 PASS lines as Q7, no failures (68 minutes);
  - and the new `make test-gs-swizzle` and part A table check (below).
- *A random differential* (m1/sweep/sweep.c, built once against HEAD's em_gs_raster.c and once against M1's):
  - 1,500 seeded batches of 40 random primitive kicks over random local memory: points, lines, line strips, triangles, strips, fans and sprites.
  - Every state an address goes through is random: the frame (every page, widths 0..32, CT32 / CT24 / CT16 / CT16S, FBMSK), Z (every page, Z32 / Z24 / Z16 / Z16S, ZMSK, ZTE / ZTST), the texture (every base block, widths 0..63, CT32 / T8 / T4 / CT24 / CT16 / T8H / T4HL / T4HH, TW / TH 0..10), the CLUT (every CBP, CT32 / CT16, CSA, CLD 0..5), the four wrap modes with random region bounds, nearest and bilinear, UV and STQ, and the scissor anywhere in 0..2047, so high page rows, page columns past the width and the 4 MiB wrap all occur. Alpha test, blend, fog, DATE, dither, SCANMSK and COLCLAMP are random too. One batch in four runs in strict mode.
  - Each batch is drawn by one EmGs and by 3 and 8 row-band EmGs in lockstep. All 4,500 lines (the hash of the 4 MiB local memory, the drawn and refused primitives and the drawn pixels) are equal: 62,130 primitives and 127.5 million pixels at one band.
  - Four negative controls (scratch copies, m1/sweep/neg/), each breaking one table, are caught in a 100-batch run: the T8 / T4 column-table choice from (y >> 1) & 1 alone changes 255 of 300 lines, Z with the CT32 column table 300, the row tables of widths above 8 built one width short 294, and the CLUT load's base one word off 291.
  - Two other CLUT controls changed nothing, correctly: the CLUT load's row table for width 2, and its Z32 column table. A CLUT reads at most 16 x 16 pixels, where the widths and the two column tables give the same addresses (they differ from x 32 or y 16 on). They were replaced by the base control above.
- *The DOF captures:* the offline bench (below) checks that after the pass all 4 MiB of local memory are equal between HEAD and M1 in each of the 8 captures, at 1, 3 and 8 bands.

**Offline** (m1/bench/bench.py; the DOF pass of each fork capture through dof_replay, one EmGs, strict; HEAD's library and M1's alternated, best of 30): 18.5-21.1 ms -> 14.9-17.3 ms per pass, all 8 captures 157.4 -> 127.7 ms (-18.9 %).

**Census per class** (the q4 census instrumentation applied to HEAD and to M1; one worker, the demo to counter 1780, the same 1,967.5 million plots in every run; HEAD, M1, M1, HEAD; m1/census/summary.txt). Seconds of GS CPU, then ns per plot:

| Class | HEAD a / b | M1 a / b |
|---|---|---|
| T4 triangles, bilinear, Z GEQUAL (416.9M plots) | 24.49 / 24.25 s, 58.2-58.7 ns | 20.60 / 20.57 s, 49.3-49.4 ns |
| CT32 sprites, bilinear, blended, Z GEQUAL (640.9M) | 18.50 / 18.24 s, 28.5-28.9 ns | 14.86 / 14.83 s, 23.1-23.2 ns |
| CT32 sprites, bilinear, Z ALWAYS (399.9M) | 11.37 / 11.23 s | 9.32 / 9.31 s |
| T8 triangles, bilinear (160.3M) | 8.74 / 8.66 s | 7.53 / 7.51 s |
| Untextured triangles, blended (217.6M) | 3.81 / 3.75 s | 3.14 / 3.12 s |
| CT32 triangles, three classes (110.9M) | 4.75 / 4.71 s | 4.15 / 4.15 s |
| The Q4 trivial sprites | 0.37 / 0.36 s | 0.39 / 0.38 s |
| All | 72.78 / 71.95 s, 36.6-37.0 ns | 60.61 / 60.50 s, 30.8 ns (-16.3 %) |

Every large class drawn through plot() is 10-24 % cheaper (the textured ones 13-24 %); the untextured Z-ALWAYS triangles (8.5M plots) 7 %, and the 0.6M line pixels are unchanged. So P7's hazard (an edit to plot() slowing every class) did not occur. Only the trivial sprites' fill, which already computed one address per pixel inline, is 0.02 s slower (+5 %, 0.03 % of the run).

**Live** (m1/batch.sh, 08:13-09:06; headless, uncapped; each group HEAD, M1, M1, HEAD; binaries m1/head_extermination, HEAD 4af6bc3 from git archive, and m1/m1_extermination, equal to build/extermination; runs/m1_*). quiet.sh's gate held before every run: idle 70.3-79.8 %, 1-minute load 3.8-13.6. Mean ms per tick.

One worker (the demo to counter 1780, EM_GS_THREADS=1), the GS frame's CPU per frame:

| Range | HEAD a / b | M1 a / b | Change |
|---|---|---|---|
| Cinematic (1,310 frames) | 49.90 / 49.64 | 41.82 / 42.13 | -7.8 ms (-15.7 %) |
| Calm | 49.42 / 49.19 | 41.31 / 41.26 | -8.0 ms |

About 28.5 ns per plotted pixel in the cinematic (1.47M a frame), against 34 after the quick wins (P10).

Default 6 workers, AREA11 (the demo to counter 4650). Each cell: busiest worker / tick wall / ticks over:

| Range | HEAD a, b | M1 a, b | Busiest worker |
|---|---|---|---|
| Cinematic (1,310) | 10.90, 10.82 / 11.74, 11.36 / 5, 3 | 9.58, 9.48 / 10.33, 9.97 / 1, 1 | -1.3 ms |
| Calm (85) | 10.99, 10.96 / 12.01, 11.68 / 0, 0 | 9.61, 9.49 / 10.17, 9.90 / 0, 0 | -1.4 ms |
| Slide (121) | 11.87, 12.12 / 12.54, 14.30 / 0, 14 | 10.07, 10.08 / 10.66, 10.62 / 0, 0 | -1.9 ms |

HEAD b's slide was disturbed (14 ticks over, 98.9 % speed; its busiest worker is kept). Every other range ran at 100 % speed. The game thread is unchanged (5.9-6.3 ms).

Default 6 workers, AREA01 (a01.sh to a01_00; every run passed the smoke's 21 live phases). Each cell: busiest worker / tick wall / ticks over / speed:

| Range | HEAD b | M1 a, b | Busiest worker |
|---|---|---|---|
| Beat (781) | 13.88 / 14.98 / 208 / 97.8 % | 11.75, 11.80 / 13.27, 13.11 / 36, 36 / 99.7, 99.7 % | -2.1 ms |
| Fire (376) | 14.85 / 15.77 / 117 / 96.9 % | 12.45, 12.54 / 13.51, 13.72 / 12, 32 / 99.8, 99.5 % | -2.4 ms |
| Fire peak (149) | 16.51 / 17.39 / 92 / 94.3 % | 13.77, 13.91 / 14.52, 15.21 / 9, 31 / 99.6, 98.8 % | -2.7 ms |

HEAD a (m1_a01_head_a) was disturbed: its busiest worker was 14.32 / 15.23 / 16.93 ms, near HEAD b's, but its tick took 18.5-22.0 ms (86.2 / 83.8 / 76.0 % speed, 519 / 292 / 146 over) with 6.6-10.7 ms of waiting for the field, so the workers were off-core. Its busiest-worker figures agree with the change above; its tick figures are not used. The game thread is unchanged (9.9-10.8 ms).

Three workers (EM_GS_THREADS=3, the iPhone proxy). Each cell: busiest worker / ticks over / speed:

| Range | HEAD a, b | M1 a, b |
|---|---|---|
| Cinematic | 18.70, 18.94 / 1202, 1287 / 87.4, 85.9 % | 16.08, 16.15 / 568, 582 / 97.0, 96.9 % |
| Calm | 18.63, 18.79 / 85, 85 / 87.8, 86.9 % | 16.02, 16.21 / 10, 13 / 99.2, 98.9 % |
| Slide | 20.85, 20.60 / 121, 121 / 73.1, 79.4 % | 17.52, 17.89 / 66, 105 / 89.0, 76.9 % |
| AREA01 beat | 24.61, 24.60 / 736, 731 / 66.5, 66.5 % | 20.40, 20.41 / 629, 627 / 78.9, 78.5 % |
| AREA01 fire | 26.31, 26.35 / 376, 376 / 62.4, 62.3 % | 21.74, 21.83 / 359, 363 / 75.3, 74.3 % |
| AREA01 fire peak | 29.71, 29.85 / 149, 149 / 55.4, 55.0 % | 24.55, 24.65 / 149, 149 / 66.8, 65.2 % |

The slide's speed at 3 workers is noisy in both builds (73-79 % HEAD, 77-89 % M1, with the busiest worker 17.5-17.9 ms in M1); its busiest worker fell 2.7-3.3 ms.

**Reading.**
- *Exactness.* Only how an address is computed changed; the proofs above are the evidence, not the label.
- *Gain.* The GS frame is 15.7 % cheaper on one worker and 16.3 % in the census, inside the plan's 15-25 % estimate. At 6 workers the busiest worker is 1.3-1.9 ms lower in AREA11 (the estimate was about 10 -> 8 ms in the calm; measured 11.0 -> 9.5) and 2.1-2.7 ms lower in AREA01 (estimate 14 -> 11-12 in the fire; measured 14.9 -> 12.5).
- *AREA01 on the Mac* now runs at 98.8-99.8 % speed at the default count (9-36 ticks over per range, against 92-208), the target P10 set for it (busiest worker 2-3 ms lower). The fire peak's busiest worker is 13.8-13.9 ms, under the period with 2.8 ms to spare.
- *The iPhone proxy* (3 workers) gains 10-12 points of speed: AREA11 cinematic and calm 97-99 %, the slide 77-89 %, AREA01 65-79 %. Its busiest worker is still over the period in AREA01 (20.4-24.7 ms) and on about half of the cinematic's ticks. Not measured on the device.
- *What is left.* About 28.5 ns a plotted pixel. M3 (the decoded texture cache) and M2 (specialized span loops, the early Z test) are next; fetch_rgba still switches on the format and the four bilinear fetches each look their row and column up again, which M2's specialization can share.
- *Not covered here:* the field readout (em_gs_world.c, one em_gs_addr32 per field pixel, 114,688 a frame spread over the workers) and the residency marks keep the function; they are not on the per-plot path.

## Appendix: the skeptic's corrections (adversarial review, 2026-10-10)

Skeptic review, with 5 new headless runs and a code re-read. The synthesis's mechanism holds up: the Original profile draws on the CPU, and the depth-of-field pass and the effects multiply the overdraw. But it does not explain the user's complaint on the Mac. Headless, at the default 7 workers, the hill slide runs at 98.9-100 % of real time, and the AREA01 fire at 91-96 %. The pacer drops the time a late tick loses, but those late ticks only lose 1-9 %. "Really slow" fits only (a) the 3-worker iPhone default, measured at 74-83 % speed in AREA11 and 50-57 % near the fire on M1 performance cores, or (b) the windowed path, which was not measured. Each frame takes a drawable from nextDrawable at frame start and holds the previous one for a tick. IOS.md already measured waits of about 15 ms per step there. This should be ranked first, not last.

The 7-worker numbers depend on machine load. On today's ordinary desktop load, my re-runs gave 18-24 slide ticks over, against 0-8 in the synthesis's runs. Here 6 workers beat 7 (5 over).

Q1 (FNV digest gate) and Q2 (host floats in the VU page programs) only save main-thread time. In AREA11 the main thread waits about 33 % of the slide, so neither changes the slide's tick time on the Mac. Q2 also has an exactness trap: the page programs contain double math, which must stay outside the region where round-toward-zero and flush-to-zero are switched on. Q3 is bit-exact but small (its only measured gain is 0.25 ms of busiest worker).

The synthesis also missed: a pure-reorder early Z test (most of plot's cost, the texture sample, comes before the Z test), a core-weighted row partition for mixed core types, and the 14 multiply-adds clang already fuses in em_gs_raster.c.

- **CONFIRMED: the slide's tick time at 3 and 7 workers reproduces; instrumentation overhead is negligible** — Stock build/extermination, demo to counter 4650. 3 workers: slide wall 22.57 ms, busiest 22.07 (synthesis 22.39 / 22.03); cine 20.25 (19.92). Under the same load, ext_instr at 7 workers matches stock at 7: slide 14.40 vs 14.47 / 14.72, busiest 12.22 vs 12.31 / 12.33. Runs: runs/skeptic_stock_t3, skeptic_stock_t7, skeptic_stock_t7c, skeptic_instr_t7. Load 1-min 3.6-10, idle 70-76 % before each run (36 ns per plotted pixel and the scaling table stand)
- **CORRECTION: the 7-worker AREA11 numbers are best case; on an ordinary desktop the slide misses more ticks, and 6 workers beat 7** — Background at re-run time: mediaanalysisd 61 %, fileproviderd 61-89 %, the Claude app 34-39 %, WindowServer 35-45 %. Slide at 7 workers: wall 14.4-14.7 ms (synthesis 12.0-12.5), p95 17.4-18.0, max 18.6-22.9, 18-24 / 121 over (synthesis 0-8); busiest 12.2-12.3 ms (synthesis 10.8-11.1). At 3 workers nothing changed, so the gap is oversubscription: 7 workers + main + audio + movie threads on 8 performance cores, with about 2 cores of background. 6 workers under the same load: slide 13.82 ms, 5 over; cine 32 over vs 54-69 at 7 (Busiest worker +1.2-1.5 ms; slide ticks over 0-8 -> 18-24 / 121. Measured support for Q5: count only performance cores, and leave headroom)
- **CORRECTION, MOST IMPORTANT: headless numbers do not explain 'really slow' on the M1 Pro; the late ticks cost 1-9 % of game speed** — em_frame.c frame_pace_ntsc resets the deadline when late, so a late tick loses (wall - 16.68 ms) of game time and nothing more. Speed = n x 16.68 / sum(max(wall, 16.68)); script build/perf_investigation/skeptic/speed.py. 7 workers: slide 98.9-100 %, cine 99.6-99.9 %; AREA01 beat 96.2 %, fire 96.0 %, fire peak 91.1 % (a01_t7). Without the DOF pass: fire 98.3 %. 3 workers: slide 73.9 %, calm 83.2 %, cine 82.4 %, AREA01 fire 56.7 %, fire peak 50.1 % (Mac at the default worker count: 1-9 % slow motion (measured headless). The 3-worker (iPhone) default: 17-50 % slow motion. The Mac complaint needs another cause)
- **MISSED / MIS-RANKED: the windowed presentation path is the leading candidate for the Mac complaint and should be measured first** — em_gfx_metal.m: each frame start calls [layer nextDrawable] (line 921), which blocks while no drawable is free. A field frame holds its drawable un-presented until the next kick (gsw_complete). With the layer's default drawable count, one is held, one is queued and one is on screen, so nextDrawable can wait up to a refresh. docs/IOS.md measured about 15 ms of every step in that wait on a 60 Hz panel (before its CADisplayLink fix, which is iOS only; macOS has none). Under the reset pacer, a 15 ms wait plus a 12-14 ms tick would run at about 55-60 % speed (Not measured (needs a window, so ask the user first). Possibly the largest effect on the Mac. Measured and fixed 2026-10-11, Progress P2: on the 120 Hz built-in display the wait was 2.0-5.5 ms per tick, not 15 ms; the drawable is now taken at commit and the wait is 0.04 ms)
- **CONTEXT MISSED: this is a recent regression; the CPU GS frame became the Original profile's renderer on 2026-10-08** — Port be2f04d (2026-10-08, 'GSFRAME, the Original profile drawn by the CPU GS model'); aa43764 / 2a233ae (2026-10-10, the DOF pass). IOS.md Frame time (2026-10-04, Metal world): iPhone Air world steps p50 6.99 / p99 9.04 ms, M1 Pro 7.52 / 11.85, with 7.6 ms of headroom on the phone (Explains why it 'used to be fine'. The phone's performance cores match the M1's on the main thread, so the phone's problem is the GS workers)
- **CORRECTION: the DOF pass covers about 6.3 fields per frame, not 'eight times'** — 720,896 plotted / (512 x 224 = 114,688) = 6.29. Eight is the number of barriers (shrink and stretch steps), not full-screen coverages (Wording only; its measured 35 % of GS CPU stands)
- **CORRECTION to Q1 / Q2: they do not shorten the hill-slide tick on the Mac, because the main thread is not on the critical path in AREA11** — sample_slide main thread: 33.5 % waiting in em_gs_world_wait / gsw_complete; main 8.7-9.0 ms vs busiest worker 10.8-12.3 ms; frames are pipelined (1-worker wall = main; GPU wall = main). In AREA01, main (13.3-14.0) gates the tick: removing the DOF pass cut the busiest worker by 2.3 ms but the tick by only 0.7 ms (Q1 / Q2: about 0 ms in the AREA11 slide on the Mac. They help in AREA01, on the GPU renderer, and on the phone (shared cores). The AREA01 gains (1.5 ms; 1-2 ms) come from sample percentages and were not measured. Q1 measured 2026-10-11, Progress P4: AREA01 main thread -1.9 ms, AREA11 -0.6 to -0.9 ms; the GS-frame tick unchanged at 6 workers, because there the busiest worker, not the main thread, set the AREA01 tick; on the GPU renderer the AREA01 tick fell 1.9 ms)
- **EXACTNESS FLAG on Q2: host lanes run under round-toward-zero and flush-to-zero; the page programs also contain double math, which must stay outside that environment** — em_vu_host_lanes.h: the lanes must run inside an EM_VU_HOST_NOINLINE function bracketed by em_vu_host_enter / em_vu_host_leave, and 'nothing else runs in it'. em_vu1_page_programs.h has double arithmetic (around lines 899-920: s == 0.0, a quotient path) and calls em_eei_quotient for VDIV; today the emvup_* helpers are static inline inside the larger program functions. emvup_chk only sets c->bad, and the output is dropped at the fault returns. That drop is what lets exponent-255 operands become inf / NaN under host lanes (Safe only if each program is split so that just add / sub / mul / madd run in the bracketed function. Extend test_vu_host_lanes.py and add a whole-program reference sweep. Handled differently in Progress P5: the double math and the kick consumers leave the environment through trampolines, exponent-255 operations stay on the model, and a whole-program differential test runs both paths)
- **VERDICT on Q3 (per-column U in sprites): bit-exact, but small, and most of its gain is unmeasured** — draw_sprite: U16 (UV: floordiv64 over x; STQ: s0 + (s1 - s0) * t with t from x, then stq_div) depends only on x and per-primitive constants. Computing it once per column with the same expression gives the same bits, as long as the expression is kept verbatim (see the FMA finding). Measured: -6 % on the pass offline, about 0.25 ms of busiest worker. The '15-30 % of snow / fire sprite cost' is an estimate (About 0.25 ms measured; it will not fix the slide by itself. Measured 2026-10-11, Progress P6: the same bytes; offline -7.5 % on the pass; at 6 workers the busiest worker 0.26 ms less in the cinematic and calm, as estimated, but 0.57 ms less in the slide and 1.1-1.8 ms less in AREA01, so the snow / fire share was larger than this verdict allowed)
- **MISSED, bit-exact: test Z before the texture sample in plot()** — plot() samples the texture (bilinear = 4 x fetch_rgba, together 44 % of worker time) and applies TFX and fog before the frame read, DATE and the Z test. Every failure path (Z never or fail, DATE mismatch, alpha fail with afail 0) returns without writing, and sampling has no side effects. Testing Z first when zte && ztst >= 2 therefore produces the same bytes and skips the fetch for occluded pixels. The alpha test must stay ahead of the Z write, because afail 1 / 3 change write_z (Unknown until a Z-fail counter is added. It could be large for the depth-tested fire and snow sprites behind geometry; nothing for the DOF pass if it does not test Z)
- **MISSED: equal 4-row interleaved bands on mixed core types put the slowest core on the critical path; Q5's count alone is not enough on the iPhone** — em_gs_world.c worker_count = online - 3 (A19 Pro: 6 - 3 = 3); own_row deals rows equally. With 2 performance + 4 efficiency cores, the frame and each of the 8 DOF barriers wait for an efficiency-core worker. Row ownership is a pure partition, so giving performance-core workers more rows, or using more workers on the 4 efficiency cores, draws the same bytes. gs_world_test.c (N workers == 1) is the proof and needs uneven-partition cases (Phone not measured. On M1 performance cores, 3 workers give 50-83 % speed, the clearest measured cause of 'really slow' on the phone)
- **MISSED, exactness hazard for every perf refactor and for Q6: Apple clang already fuses multiply-adds in the GS model** — The Makefile's CFLAGS are -O2 with no -ffp-contract (only some test targets pin it off). Apple clang 21 at -O2 contracts a + (b - a) * t into an FMA, and em_gs_raster.c compiled that way contains 14 fused multiply-add / subtract instructions (count only). These are double expressions: the STQ sprite interpolation, the barycentric weights, the zrow + (x - xd) * zstep Z. test_gs_raster_reference.py builds with cc -O2, so it proves the fused arm64 code. A baseline x86-64 Windows / Linux build will not fuse. Stepping Z incrementally or regrouping these expressions changes bits (Pin -ffp-contract explicitly, and decide which form is reference, before any raster refactor or flag change; possible cross-platform difference today. Resolved 2026-10-11: pinned off in every build, Progress P1)
- **CONFIRMED with caveats: the fork is about 6-8x cheaper per frame in software GS CPU, and 'fill-bound' and the hotspot ranking stand** — CPU_SUMMARY: software minus Null process CPU 8.5 / 9.2 / 13.1 ms (calm / slide / fire); with extra threads 0: 6.6 / 7.4 / 10.6 ms, so the multithreaded figure includes spin-waits. Port at 1 worker: 52.1 / 59.8 ms. The plot() code matches the claims: it is generic, switches on state per pixel, recomputes addresses for frame and Z through the exported em_gs_addr32, and does per-pixel 128-bit floordiv (UV triangles) and double divides plus stq_div (STQ). No lock contention seen: barriers are mutex / cond, 8 plus 1 per frame (The 6x is if anything understated (against the fork's x0 run: 7.9x calm))
- **FINAL VERDICT on the top three (Q1 digest gate, Q2 VU host floats, Q3 per-column U)** — All three can be made bit-exact (Q2 only with the bracketing above), but none addresses either measured cause of the user's complaint. The slide on the Mac is GS-bound and runs at 99 % speed headless, so the cause is elsewhere. The phone's slowness is the worker count and partition. Q1 and Q2 help only where the main thread gates the tick (AREA01, the phone); Q3 is about 0.25 ms. Suggested order: (0) measure the windowed Mac path and the device, with the user's OK; (1) a performance-core-aware worker count and a core-weighted row partition (measured: 6 > 7 under load; 3 workers = 50-83 %); (2) the early Z test plus texture-fetch specialization (fetch / sample 44 %, plot 28 %), after a Z-fail counter and with -ffp-contract pinned; (3) then Q1 / Q2 for AREA01's main thread (Re-ranking needed. The quick wins as ranked give an estimated 0-0.3 ms on the Mac slide tick (only Q3's 0.25 ms is measured, offline))
