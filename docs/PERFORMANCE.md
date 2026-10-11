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
- textured_kick: 4.5-8.8 %;
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
   - Ungated digests cost about 1.5 ms per tick in AREA01; textured_kick 4.5-8.8 %.
6. **The iPhone gets only 3 workers** (section 5).
7. **Unmeasured:**
   - the windowed Mac path: measured and fixed since (Progress P2);
   - a possible texture_entry cliff (a linear search of 1024 entries, plus a 16 KiB memset per miss once full).

**Not a cause:** build settings. macOS uses -O2; the iOS project has optimization level 2 in both configurations, and build.sh builds Release. Presentation is about 0 % headless.

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

**Q2. Host floats in the VU1 page programs** (em_vu1_page_programs.h).
- *Change:* switch emvup_add/sub/mul to the host-lane functions inside em_vu_host_enter / em_vu_host_leave. emvup_chk already faults on exponent-255 operands, which is the host-lane precondition. The divide needs a proven host truncated quotient.
- *Gain:* the snow program is about 1.9 ms of the slide's main thread. Estimate 1-2 ms of main thread in AREA11 and in the AREA01 fire.
- *Why it matters:* this is critical-path time in AREA01 and on the GPU renderer. On the iPhone it also frees performance-core time for the workers.
- *Effort:* 1-3 days. *Risk:* low.
- *Proof:* test_vu_host_lanes.py (extended to the divide), test_chain_page_reference.py, test_snow_tiles_reference.py, test_level2_kind6_vu_reference.py, the level smokes.
- *Also check:* whether the remaining exact-float hot spots (corner / vu_row / rotate) meet the same precondition.

**Q3. Per-column U in sprites.**
- *Change:* compute U once per column (UV: floordiv64; STQ: t plus stq_div). V stays per row.
- *Gain:* measured offline at -6 % of the pass (24.5 -> 22.9 ms single-thread, about 0.25 ms of busiest worker). Estimated 15-30 % of the snow and fire STQ sprite pixel cost.
- *Effort:* about 1 day. *Risk:* very low.
- *Proof:* the GS proof set.

**Q4. Fast path for trivial-state sprites** (the clears, gsw_alpha_clear).
- *Gain:* about 1.5 ms of all-worker CPU per frame, about 0.2 ms of busiest worker.
- *Effort:* about 1 day. *Risk:* low.
- *Proof:* the GS proof set.

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

**Q7. Counters on texture_entry.**
- *Change:* count entries and misses per frame. If the 1024-entry cliff is real, replace the search with a hash.
- *Proof:* test_gs_memory_reference.py, test_gs_texture_reference.py.

### Medium work

**M1. Separable swizzle tables.**
- *Change:* the PSMCT32 and PSMZ32 layouts split into a row part and a column part, wrapped by the existing 4 MiB mask. Compute rowtab[y] + coltab[x] for frame, Z and texel addresses.
- *Gain:* estimated 15-25 % of pixel cost. Calm busiest worker about 10 -> 8 ms; fire about 14 -> 11-12 ms.
- *Effort:* 2-4 days. *Risk:* low; part A of the raster test decodes through the measured swizzle maps.
- *Proof:* the GS proof set.

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

- **Per-core speed.** A19 Pro P-cores are faster per core than the M1 Pro's (IOS.md world steps p50 6.99 vs 7.52 ms, before the GS frame), but the E-cores are much slower. On balance the device is probably worse than these numbers; not measured on it.
- **Stale doc.** IOS.md "Frame time" (2026-10-04, 7.6 ms headroom) predates the GS frame on main (2026-10-08) and the depth-of-field pass (2026-10-10).
- **Thermals.** The Air is a thin phone. Sustained all-core load will throttle; not measured.

**What to do:**
1. **User decision:** default to, or offer prominently, EM_GPU_RENDERER=1 on the phone until the exact path fits. It is not exact, so it is a profile-policy decision for LAUNCHER_OPTIONS.md. Nothing has been changed.
2. **Q5:** worker QoS and a perflevel-based count. Done (P3: 4 workers by default); still to measure 2-5 workers on the device.
3. **M4** dynamic bands, so E-cores do not set the frame time.
4. **Q2** frees P-core time.
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
3. **texture_entry counters** in the slide and the fire.
4. **Pixel share per primitive class** (DOF, snow, fire, world, clears), to rank M2's loops.
5. **After each item,** re-run cine / calm / slide / AREA01 beat / fire / peak at 7 and 3 workers on a quiet machine, recording load. Keep a table of ns per pixel, busiest worker, main thread and ticks over.
6. **Efficiency-core placement** of the workers under background load (per-worker CPU spread as a proxy).

## Progress

Each step of section 4's plan, with its measured before / after. Receipts: build/perf_investigation/ (runs/fpc_*, fpcontract/; runs/pres_*, present/; runs/q5_*, runs/q5w_*, qos/).

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
- **CORRECTION to Q1 / Q2: they do not shorten the hill-slide tick on the Mac, because the main thread is not on the critical path in AREA11** — sample_slide main thread: 33.5 % waiting in em_gs_world_wait / gsw_complete; main 8.7-9.0 ms vs busiest worker 10.8-12.3 ms; frames are pipelined (1-worker wall = main; GPU wall = main). In AREA01, main (13.3-14.0) gates the tick: removing the DOF pass cut the busiest worker by 2.3 ms but the tick by only 0.7 ms (Q1 / Q2: about 0 ms in the AREA11 slide on the Mac. They help in AREA01, on the GPU renderer, and on the phone (shared cores). The AREA01 gains (1.5 ms; 1-2 ms) come from sample percentages and were not measured)
- **EXACTNESS FLAG on Q2: host lanes run under round-toward-zero and flush-to-zero; the page programs also contain double math, which must stay outside that environment** — em_vu_host_lanes.h: the lanes must run inside an EM_VU_HOST_NOINLINE function bracketed by em_vu_host_enter / em_vu_host_leave, and 'nothing else runs in it'. em_vu1_page_programs.h has double arithmetic (around lines 899-920: s == 0.0, a quotient path) and calls em_eei_quotient for VDIV; today the emvup_* helpers are static inline inside the larger program functions. emvup_chk only sets c->bad, and the output is dropped at the fault returns. That drop is what lets exponent-255 operands become inf / NaN under host lanes (Safe only if each program is split so that just add / sub / mul / madd run in the bracketed function. Extend test_vu_host_lanes.py and add a whole-program reference sweep)
- **VERDICT on Q3 (per-column U in sprites): bit-exact, but small, and most of its gain is unmeasured** — draw_sprite: U16 (UV: floordiv64 over x; STQ: s0 + (s1 - s0) * t with t from x, then stq_div) depends only on x and per-primitive constants. Computing it once per column with the same expression gives the same bits, as long as the expression is kept verbatim (see the FMA finding). Measured: -6 % on the pass offline, about 0.25 ms of busiest worker. The '15-30 % of snow / fire sprite cost' is an estimate (About 0.25 ms measured; it will not fix the slide by itself)
- **MISSED, bit-exact: test Z before the texture sample in plot()** — plot() samples the texture (bilinear = 4 x fetch_rgba, together 44 % of worker time) and applies TFX and fog before the frame read, DATE and the Z test. Every failure path (Z never or fail, DATE mismatch, alpha fail with afail 0) returns without writing, and sampling has no side effects. Testing Z first when zte && ztst >= 2 therefore produces the same bytes and skips the fetch for occluded pixels. The alpha test must stay ahead of the Z write, because afail 1 / 3 change write_z (Unknown until a Z-fail counter is added. It could be large for the depth-tested fire and snow sprites behind geometry; nothing for the DOF pass if it does not test Z)
- **MISSED: equal 4-row interleaved bands on mixed core types put the slowest core on the critical path; Q5's count alone is not enough on the iPhone** — em_gs_world.c worker_count = online - 3 (A19 Pro: 6 - 3 = 3); own_row deals rows equally. With 2 performance + 4 efficiency cores, the frame and each of the 8 DOF barriers wait for an efficiency-core worker. Row ownership is a pure partition, so giving performance-core workers more rows, or using more workers on the 4 efficiency cores, draws the same bytes. gs_world_test.c (N workers == 1) is the proof and needs uneven-partition cases (Phone not measured. On M1 performance cores, 3 workers give 50-83 % speed, the clearest measured cause of 'really slow' on the phone)
- **MISSED, exactness hazard for every perf refactor and for Q6: Apple clang already fuses multiply-adds in the GS model** — The Makefile's CFLAGS are -O2 with no -ffp-contract (only some test targets pin it off). Apple clang 21 at -O2 contracts a + (b - a) * t into an FMA, and em_gs_raster.c compiled that way contains 14 fused multiply-add / subtract instructions (count only). These are double expressions: the STQ sprite interpolation, the barycentric weights, the zrow + (x - xd) * zstep Z. test_gs_raster_reference.py builds with cc -O2, so it proves the fused arm64 code. A baseline x86-64 Windows / Linux build will not fuse. Stepping Z incrementally or regrouping these expressions changes bits (Pin -ffp-contract explicitly, and decide which form is reference, before any raster refactor or flag change; possible cross-platform difference today. Resolved 2026-10-11: pinned off in every build, Progress P1)
- **CONFIRMED with caveats: the fork is about 6-8x cheaper per frame in software GS CPU, and 'fill-bound' and the hotspot ranking stand** — CPU_SUMMARY: software minus Null process CPU 8.5 / 9.2 / 13.1 ms (calm / slide / fire); with extra threads 0: 6.6 / 7.4 / 10.6 ms, so the multithreaded figure includes spin-waits. Port at 1 worker: 52.1 / 59.8 ms. The plot() code matches the claims: it is generic, switches on state per pixel, recomputes addresses for frame and Z through the exported em_gs_addr32, and does per-pixel 128-bit floordiv (UV triangles) and double divides plus stq_div (STQ). No lock contention seen: barriers are mutex / cond, 8 plus 1 per frame (The 6x is if anything understated (against the fork's x0 run: 7.9x calm))
- **FINAL VERDICT on the top three (Q1 digest gate, Q2 VU host floats, Q3 per-column U)** — All three can be made bit-exact (Q2 only with the bracketing above), but none addresses either measured cause of the user's complaint. The slide on the Mac is GS-bound and runs at 99 % speed headless, so the cause is elsewhere. The phone's slowness is the worker count and partition. Q1 and Q2 help only where the main thread gates the tick (AREA01, the phone); Q3 is about 0.25 ms. Suggested order: (0) measure the windowed Mac path and the device, with the user's OK; (1) a performance-core-aware worker count and a core-weighted row partition (measured: 6 > 7 under load; 3 workers = 50-83 %); (2) the early Z test plus texture-fetch specialization (fetch / sample 44 %, plot 28 %), after a Z-fail counter and with -ffp-contract pinned; (3) then Q1 / Q2 for AREA01's main thread (Re-ranking needed. The quick wins as ranked give an estimated 0-0.3 ms on the Mac slide tick (only Q3's 0.25 ms is measured, offline))
