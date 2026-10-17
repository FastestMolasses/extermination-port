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

**After the early Z test and the decoded texture cache (Progress P12, P13, 2026-10-12 / 2026-10-15).** The GS frame on one worker is now 28.5-28.7 ms of CPU in the AREA11 cinematic (41.7-42.1 before P12) and 43.5-43.8 ms at the AREA01 fire peak, about 26.9 ns a drawn pixel. At the default worker count every measured range runs at full speed with no tick over; in AREA01 the game thread (about 10 ms) now sets the tick. The 3-worker iPhone proxy runs AREA11 at full speed (0-1 of 1,310 cinematic ticks over) and AREA01 at 93.1-98.6 % speed.

**After the specialized span loops (Progress P14, 2026-10-15).** 85-87 % of the drawn pixels now go through 18 dedicated loops (the depth-of-field pass, the snow and fire sprites, the textured triangles, gsw_alpha_clear's strip), the same bytes. The pixel cost is 15.9 ns (26.8 before, -41 %); the GS frame on one worker takes 17.6 ms of CPU in the AREA11 cinematic and 25.2 ms at the AREA01 fire peak. At the default 6 workers the busiest worker is 5.7-6.1 ms in AREA11 and 6.0-7.0 ms in AREA01, where the game thread (about 10 ms) sets the tick. The 3-worker iPhone proxy now runs every measured range at full speed with no tick over (busiest worker 7.8-8.3 ms in AREA11, 8.8-10.6 ms in AREA01). M4 and L1 remain.

**After parse once and bands from a queue (Progress P15, 2026-10-15).** The frame is parsed once and drawn in bands the workers take from a queue, a band still being drawn split for a worker that has none, and the next frame's parse starts on the game's thread while it waits; the same bytes. At 3 workers (the iPhone proxy) the AREA11 tick fell from 8.1-8.4 to 6.5-6.6 ms (busiest worker 7.8-8.1 -> 6.0-6.1 ms) and AREA01's fire peak from 11.0 to 10.1-10.2 ms, where the game thread now sets the tick. With 10 workers on this Mac (more than its 8 performance cores: the straggler case) the AREA11 tick fell from 7.8-8.4 to 6.0-6.4 ms. At the default 6 workers the busiest worker is 3.8-4.1 ms in AREA11 and 4.2-5.0 ms in AREA01; the game thread sets the tick everywhere. L1 remains.

**The medium items together (Progress P16, 2026-10-16).** Every scene was measured again against both baselines (the report's b9fd9f9 and the quick wins' 4af6bc3), interleaved on a quiet machine.
- *One worker:* the GS frame is 3.0-3.9x cheaper than the report's. It now takes 15.7-17.2 ms of CPU in AREA11 and 18.9-24.4 ms in AREA01, about 10-12 ns a pixel, against 35-38 ns.
- *This Mac, 6 workers:* every scene runs at full speed headless, with no tick over (windowed was measured in AREA11 only, also with none over). The busiest worker is 3.7-5.1 ms, and the game thread (5.8-6.3 ms in AREA11, 9.7-10.2 ms in AREA01) sets the tick.
- *The 3-worker iPhone proxy:* full speed everywhere, against 83-84 % in AREA11 and 50-61 % in AREA01 at the report.
- *Against the emulator:* the port's single-core GS cost is now 2.0-2.4x the fork's software renderer (6-8x at the report).
- *Not real time yet:* one worker, at 49-78 % speed with the frame drawn on the game's thread. That is the Windows path, which has no worker threads yet, and a Linux host with 4 or fewer cores.
- *Not measured:* the phone itself.
- *L1 (NEON) is not being done* (user decision 2026-10-15). It is not needed on this Mac; section 4's L1 entry and P16 record what it would have been for.
- *Next: L2* (user decision 2026-10-15): explore an exact GS model on the GPU, judged by energy and CPU time as well as by ticks over. A faster route that keeps the Original profile exact is always taken, so the port runs as efficiently as it can on battery (laptops, the phone), not just fast enough on this Mac.

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
   - Measured and changed since (Progress P15): the frame is parsed once and drawn in bands from a queue, with splits; at 10 workers the tick no longer waits for a straggler (8.2 -> 6.0 ms in the cinematic).
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
- *Part done (Progress P12):* the early Z test. plot() tests Z and DATE before the texture sample, and the triangle loop computes a pixel's other attributes only after it passed; the same bytes. 32-49 % of the samples are no longer taken; the GS frame on one worker 17-28 % cheaper, the busiest of 6 workers 1.6-2.4 ms lower.
- *Done (Progress P14):* 18 loops (textured sprites, textured triangles, untextured triangles; Z off, GEQUAL or GREATER; blend on or off) for CT32 frames with Z32 / Z24 and bilinear decoded textures, which take 85-87 % of the first level's pixels; the generic path for the rest, unchanged. The same bytes (the proof set, a live check of 1.12 billion primitives on the route against the generic path with EM_GS_SPEC_CHECK=1, a 241,116-primitive HEAD-against-M2 differential with twelve negative controls caught). The census's ns per drawn pixel 26.8 -> 15.9 (-41 %, 1.69x); the GS frame on one worker 38-43 % cheaper; the busiest of 6 workers 1.9-3.5 ms lower; at 3 workers AREA01 runs at full speed. The textured triangles' exact per-pixel coordinate arithmetic is now the largest cost.

**M3. Decoded texture cache.**
- *Change:* keep a linear 32-bit copy, palette applied, per (TEX0, CLUT, CLAMP). Invalidate it on upload, on CLUT load and on draws onto its blocks; the depth-of-field render-to-texture case invalidates at the barriers.
- *Gain:* estimated up to about 2x on textured pixels.
- *Effort:* 1-2 weeks. *Risk:* medium.
- *Proof:* gs_world_test.c (it has the barrier cases), test_gs_texture_reference.py, the GS proof set.
- *Done (Progress P13):* PSMCT32 textures and PSMT8 / PSMT4 with a CT32 CLUT, as linear 32-bit copies keyed by the texture, the rectangle its wrap reaches and the palette, decoded a row at a time and dropped by page stamps per span, transfers and uploads. The same bytes (the proof set, a 235,424-primitive HEAD-against-M3 differential with eight negative controls caught, and a self-check of 15.4 billion samples on the live route). The census's ns per drawn pixel 13.7 % lower (31.1 -> 26.9 ns), not the estimated 2x: the copies remove the address and palette work, not the bilinear arithmetic. The GS frame on one worker 10-21 % cheaper; at 6 workers the busiest worker 0.3-0.55 ms lower in AREA11 and 1.0-1.8 ms in AREA01; at 3 workers AREA01 runs at 93.1-98.6 % speed (79.2-93.3 % before).

**M4. Parse the command stream once, and assign bands dynamically.**
- *Change:* decode once and bin primitives by band; workers pull bands from a queue or by work stealing.
- *Why it stays exact:* each band is still drawn by one worker, in primitive order.
- *Gain:* the duplicated front end is about 13 ms of the 67 ms all-worker sum at 7 workers (measured basis), about 1.5-1.8 ms of busiest worker, plus part of the idle and straggler time.
- *Effort:* 1-2 weeks. *Risk:* medium.
- *Proof:* gs_world_test.c, raster test part F with EM_TEST_FULL=1.
- *Done (Progress P15):* the parse once into a list of spans, two bands a worker in each segment taken from a queue, a band still being drawn split for an idle worker, and the next frame's parse begun on the game's thread while it waits for the previous frame. The same bytes (the proof set; part F with row-range bands; gs_world_test with frames parsed early, band settings and the list's stamps; a 6,000-frame HEAD-against-M4 world differential at 1, 3, 6 and 8 workers, also under the thread sanitizer; fifteen negative controls caught, fourteen by the differential and the fifteenth by gs_world_test's stamp case). The busiest worker 1.4-1.9 ms lower at 3 and 6 workers; at 3 workers the AREA11 tick 1.5-1.8 ms lower (8.4 -> 6.6 ms); at 10 workers the AREA11 tick 1.5-2.2 ms lower (the stragglers no longer set it). The differential also found a recorder ordering gap shared with HEAD (fixed; the demo's frames keep their barrier counts).
- *Follow-up (Progress P17):* the adversarial review's items. A CLUT load from memory the frame itself draws is now drawn exactly at every worker count (it was refused with more than one worker); the recorder's two barrier rules after a read (the drawing target, the declared field) are held by deterministic gs_world_test cases; no measured change.

### Large work

**L1. NEON SIMD in the specialized loops.**
- *Gain:* estimated 2-4x on those loops.
- *Effort:* weeks. *Risk:* medium to high.
- *Proof:* the same set, plus the M2 differential check.
- *Status:* not pursued (user decision 2026-10-15: no L1; after the medium items, explore L2).
- *What it would have been for (Progress P16):*
  - *This Mac:* not needed. The game thread sets every tick, and the busiest worker is 3.7-5.1 ms.
  - *The iPhone:* by throughput on the Mac's figures, the phone's two performance cores should hold AREA11's frame and nearly all of AREA01's, with the efficiency cores for the rest. L1 (estimated 0.36-0.58 of today's GS work) is the margin for the efficiency cores' speed and sustained clocks. Measure the device first (section 7, item 2).
  - *Older iPhones* (iOS 17 reaches back to the A12): likely needed, or the GPU renderer.
  - *One-worker hosts* (the Windows path, without worker threads yet; Linux with 4 or fewer cores): L1 alone makes AREA11 real time (a tick of about 12-16 ms) but not AREA01 (about 19-24 ms). Worker threads, and drawing beside the game's thread, are the cheaper fix.

**L2. Optional: an exact GS model as Metal compute.**
- Explored 2026-10-17: [GS_GPU_DESIGN.md](GS_GPU_DESIGN.md). The pixel pipeline is mostly integer; the only per-pixel binary64 steps (the STQ triangle chain with stq_div, and the triangle Z step) have exact fp64-free integer formulations, checked bit for bit against the model on the CPU and on the M1 Pro's GPU (0 differences; about 0.4 and 0.16 ns a pixel). Hardware binary32 is not trusted for exact values (subnormals flush; division is not correctly rounded). The open questions are ordering and aliasing, and above all whether the GPU path saves energy: stage 2 (the depth-of-field pass) is the energy go / no-go.
- *Effort:* about 4-7 months for the Metal stages. *Risk:* high (energy unmeasured). *Status:* stage 0 done; stage 0.5 not started (user, 2026-10-17: "Not yet").
- *Next (user decision 2026-10-15):* explore it. Not only for the phone's budget: an exact GPU model would draw the frame with far less CPU time and energy on every host, and the work should carry over to other PS2 projects. Measure energy (powermetrics needs the user's sudo) or CPU time as its stand-in, not only ticks over.

### Where this lands (estimate)

- **Pixel cost.** Q3 + M1 + M3 + M2 take it from 36 ns to roughly 10-15 ns.
- **Single-core frame time.** That gives about 15-25 ms for AREA11 and about 25-35 ms for the fire, against 52-77 ms today.
- **Mac, 7 workers.** The busiest worker drops to about 3-5 ms in AREA11 and 5-7 ms in the fire.
- **iPhone, 3 workers.** AREA11 fits the period.
- **Is that realistic?** The fork already draws the same frames at 8.5-13 ms of single-core GS CPU, as Rosetta-translated code. That is evidence the hardware can do it, not a promise.
- **AREA01** also needs Q1 and Q2.

**Measured (Progress P16, 2026-10-16).** Each line is section 4's estimate, then the measurement:
- *Pixel cost:* 10-15 ns estimated; 9.7-11.7 ns measured.
- *Single-core frame:* 15-25 ms estimated for AREA11, measured 15.7-17.2 ms; 25-35 ms for the fire, measured 20.9 ms (the fire peak 24.4 ms).
- *Mac busiest worker:* 3-5 ms estimated for AREA11, measured 3.7-4.1 ms at 6 workers; 5-7 ms for the fire, measured 4.5-5.1 ms.
- *iPhone proxy:* AREA11 fits, as estimated, and so does AREA01. The device itself is not measured.

**Order:** Q1-Q5, then M1, M3, M2, M4: done (Progress P11-P15), measured together in P16. L1 is not pursued (user decision 2026-10-15); L2 is next to explore.

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
- **After the early Z test** (Progress P12, the same proxy): busiest worker cinematic 13.1-13.2 ms (99.9-100 % speed, 30-40 of 1,310 ticks over), calm 12.3-12.4 ms and slide 13.3 ms (100 %, none over), AREA01 beat 15.6 ms (93.4-93.7 %), fire 17.5-17.6 ms (89.1-89.3 %), fire peak 20.5-20.6 ms (79.6-79.8 %).
- **After the decoded texture cache** (Progress P13, the same proxy): busiest worker cinematic 11.9 ms (0-1 of 1,310 ticks over), calm 11.5 ms and slide 11.8-11.9 ms (none over), AREA01 beat 13.7 ms (98.6 %, 84 of 781 over), fire 15.0 ms (97.1-97.2 %), fire peak 17.2 ms (93.1 %, 81 of 149 over). Each worker holds up to 10.2 MiB of decoded texels on the route to a01_00, so 4 workers on the phone add about 41 MiB.
- **After the specialized span loops** (Progress P14, the same proxy): busiest worker cinematic 8.1-8.3 ms, calm 8.0-8.2 ms, slide 7.8-7.9 ms, AREA01 beat 8.8-8.9 ms, fire 9.4 ms, fire peak 10.6 ms; no tick over in any range (100 % speed; the same day's HEAD ran AREA01 at 92.8-98.6 %).
- **After parse once** (Progress P15, the same proxy): tick wall cinematic 6.61 ms (8.41-8.43 before), calm 6.51-6.54, slide 6.59; busiest worker 6.0-6.1 ms; AREA01 beat 10.3-10.5, fire 10.2-10.3, fire peak 10.1-10.2 ms (11.0 before), set by the game thread; no tick over in any range except one of the beat's in one run. The game thread now parses about 46 % of an AREA11 frame while it waits for the previous one (its CPU +0.4-0.7 ms). The 4 workers on the phone's 2 performance and 4 efficiency cores are the case the queue and the splits are for: a band on a slow core is split for a faster worker, and the frame no longer waits for the slowest core's whole share. Measured on this Mac with 10 workers (more than its 8 performance cores) as a proxy: the AREA11 tick 6.0-6.4 ms against HEAD's 7.8-8.4 ms (every run of P15's table); not measured on the device.
- **Per-core speed.** A19 Pro P-cores are faster per core than the M1 Pro's (IOS.md world steps p50 6.99 vs 7.52 ms, before the GS frame), but the E-cores are much slower. On balance the device is probably worse than these numbers; not measured on it.
- **All medium items, measured together** (Progress P16, 2026-10-16, the same proxy). Every range runs at full speed with no tick over:
  - *Busiest worker:* 6.1 ms in AREA11; 6.9 / 7.6 / 8.8 ms in the AREA01 beat / fire / fire peak.
  - *Tick:* 6.5-6.6 ms in AREA11 and 9.9-10.1 ms in AREA01, the game thread's.
  - *Before:* the report's baseline ran 83-84 % in AREA11 and 50-61 % in AREA01; the quick wins 80-88 % and 56-67 %.
  - *Windowed:* the cinematic, calm and slide also run with no tick over (79-87 % speed after the quick wins).
  - *Throughput:* a frame is about 18 ms of worker CPU in AREA11 and an estimated 20-26 ms in AREA01. The phone's two performance cores leave about 27 / 23 ms a period after the game thread. So AREA11 should fit on them, and the AREA01 fire peak needs a few ms of the efficiency cores. That is arithmetic on Mac figures; the device is not measured.
- **Stale doc.** IOS.md "Frame time" (2026-10-04, 7.6 ms headroom) predates the GS frame on main (2026-10-08) and the depth-of-field pass (2026-10-10).
- **Thermals.** The Air is a thin phone. Sustained all-core load will throttle; not measured.

**What to do:**
1. **User decision:** default to, or offer prominently, EM_GPU_RENDERER=1 on the phone until the exact path fits. It is not exact, so it is a profile-policy decision for LAUNCHER_OPTIONS.md. Nothing has been changed.
2. **Q5:** worker QoS and a perflevel-based count. Done (P3: 4 workers by default); still to measure 2-5 workers on the device.
3. **M4** dynamic bands, so E-cores do not set the frame time. Done (P15): bands from a queue and splits; measured on this Mac with 10 workers, not on the device.
4. **Q2** frees P-core time. Done (P5): 1.1-1.5 ms of game thread per tick on the Mac.
5. **M1, M3, M2 and L1** are the only route to an exact Original profile at full speed on the phone. Estimate: GS CPU must drop 2-3x for AREA11 and 3-4x for the fire. M1, M3, M2 and M4 are done (P11-P15): the GS frame is 3.0-3.9x cheaper than the report's, and the 3-worker proxy runs every range at full speed (P16). L1 remains as the device's margin. Whether it is needed is the device measurement (section 7, item 2), which should come first.

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
5. **After each item,** re-run cine / calm / slide / AREA01 beat / fire / peak at 7 and 3 workers on a quiet machine, recording load. Keep a table of ns per pixel, busiest worker, main thread and ticks over. Done for the quick wins as a whole (Progress P10) and for the medium items as a whole (Progress P16, at 1, 3 and the default worker count, and windowed for AREA11).
6. **Efficiency-core placement** of the workers under background load (per-worker CPU spread as a proxy).

## Progress

Each step of section 4's plan, with its measured before / after. Receipts: build/perf_investigation/ (runs/fpc_*, fpcontract/; runs/pres_*, present/; runs/q5_*, runs/q5w_*, qos/; runs/q1_*, q1/; runs/q2_*, q2/; runs/q3_*, q3/; runs/q4_*, runs/q4r2_*, runs/q4c_*, q4/; runs/q7_*, runs/q7c_*, runs/q7k_*, q7/; runs/q6_*, runs/q6r2_*, q6/; runs/sum_*, summary/; runs/m1*, m1/; runs/zt_*, runs/zc_*, zt/; runs/m3_*, runs/m3c_*, runs/m3p_*, m3/; runs/m2_*, runs/m2c_*, runs/m2s_*, m2/).

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

**EM_MUTE=1** (developer variable; em_audio_muted in em_platform.h). The audio device stays open and the game's mixer runs at the device's pull cadence, as when audible, so timing does not change. The backend zeroes what the mixer wrote, as headless runs always did, and the movie player is muted (AVPlayer.muted keeps its audio clock). macOS and iOS. Since 2026-10-16 (the field clock, GS_EXACT.md 10.4) there is no movie player: the movie's sound is one more producer of the game's mixer (em_movie_sound.h), so EM_MUTE silences it with the rest and no movie clock exists to keep. Uncapped and headless runs now take a movie's own field count in steps (E900 9,628 / 9,629, E001 4,740 / 4,741, about 12 s of wall time for the whole intro uncapped), where the host player's clock took real time (an uncapped run once took 728,724 steps for E001).

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

### P12. The Z test and DATE before the texture sample (the skeptic's early Z) (2026-10-12)

**Sizing first** (the counters below, built in the previous order: zt/raster_order0.c, zt/build_ctr0.sh; runs/zc_demo_o0, zc_a01_o0; default workers, the demo to counter 4650 and the route to a01_00). Every texture sample in these scenes is bilinear (four texels). A sampled pixel that the Z test then rejected, per frame:

| Range | Samples | Of them Z-rejected | Share |
|---|---|---|---|
| AREA11 cinematic (1,310 frames) | 1,179k | 420k | 35.6 % |
| AREA11 calm (85) | 1,169k | 524k | 44.9 % |
| AREA11 slide (121) | 1,391k | 563k | 40.5 % |
| AREA01 beat (781) | 1,741k | 772k | 44.3 % |
| AREA01 fire (376) | 1,870k | 690k | 36.9 % |
| AREA01 fire peak (149) | 2,119k | 644k | 30.4 % |

DATE rejected about 100 sampled pixels a frame in this order. The shares are a lower bound: a pixel that fails the alpha test with AFAIL KEEP returned before the Z test in the previous order, so it was not counted.

**The counters** (EmGs.pix; EM_GS_Z_COUNTERS=<file>, a developer switch, STARTUP.md "Developer switches"). One line per frame waited for, summed over the workers: texture samples taken and the bilinear ones, Z-test rejections (ZTST NEVER included) with the textured and textured bilinear ones among them, textured DATE rejections, and pixels drawn. The counts are always kept: an add per sample and per rejected pixel. Only the file needs the switch.

**Why the reorder is exact** (em_gs_raster.c, plot_dst / plot_src; GS_EXACT.md 5.3).
- A pixel is written only when it passes the scissor, SCANMSK, the Z test and DATE, and either passes the alpha test or fails it under an AFAIL mode that still writes. Every failing test before the writes returns without storing anything or counting a drawn pixel.
- The Z test reads the Z word and the pixel's own Z. DATE reads the frame word. Neither needs the texture, and nothing before the writes stores to memory, so the texture, Z and frame words read are the same in either order, also when the texture is the frame or the Z buffer itself.
- The alpha test needs the sampled alpha, so it stays after the sample. Its AFAIL modes only change what a passing pixel writes (FB_ONLY and RGB_ONLY drop the Z write, ZB_ONLY and RGB_ONLY the alpha or the colour). So the Z address is now computed whenever the test reads Z or ZMSK is 0, and the Z write is still guarded by the alpha test's outcome.
- So plot_dst (the scissor, SCANMSK, Z, then the frame read and DATE) runs first, and plot_src (the sample, TFX, fog, the alpha test, the blend and the writes) only for a pixel plot_dst passed. plot() is the two in sequence; points, lines and sprites call it.
- Triangles also defer the rest of the pixel's attributes. The colour, fog and texel coordinates (UV: two 128-bit floor divisions; STQ: the barycentric weights, three binary32 sums and two stq_div) are each a function of x and y alone, set again for every pixel. So the triangle loop now computes Z, calls plot_dst, and computes them only for a pixel that passed. The expressions are unchanged; -ffp-contract=off (P1) stays.
- Not changed: sprite_fill (P7: it reads no texture), the sprite's per-column U and per-row V (P6).

**Same bytes.**
- *The GS proof set* (EM_TEST_FULL=1; receipts in build/perf_investigation/zt/proof/). Each log equals M1's (m1/proof/) apart from timings and scratch paths:
  - the raster reference: 931 / 1158 bit-exact, 0 failures, part A's 1,712,128 table addresses;
  - gs_world_test: PASS at 1, 2, 3 and 8 workers and the host default (6), with the DOF pass and past the texture table's end;
  - the DOF pass: 8 captures bit-exact;
  - fog, GS memory and gs_display_test: PASS;
  - the texture reference: PASS, 11,129 decodes equal;
  - `make test-gs-swizzle`: 2,153,286,588 addresses, 0 differ;
  - the fb2 harness: 8 of 19 points, every exact-pixel line equal to M1's;
  - the AREA01 smoke: 20 live phases, none not live;
  - the full level smoke: the same 404 PASS lines as M1, no failures (61 minutes).
- *The counters agree:* the route to a01_00 drew 14,354,985,111 pixels in both orders (zc_a01_o0, zc_a01_o1), and every demo range the same pixels per frame.
- *A random differential* (zt/sweep/sweep.c, from M1's; built against HEAD's em_gs_raster.c, zt/head_src, and against this step's; zt/sweep/build.sh):
  - 1,500 seeded batches of 40 random primitive kicks (points, lines, strips, triangles, fans, sprites) over random local memory, with every state M1's sweep randomizes: frame, Z and texture formats, pages and widths, FBMSK, ZMSK, ZTE and every ZTST, the alpha test with every ATST and AFAIL (now on in one primitive of two), DATE, blend, fog, dither, SCANMSK, COLCLAMP, UV and STQ, nearest and bilinear, the wrap modes and the scissor anywhere in 0..2047. New for this step: in one primitive of six the texture is the frame or the Z buffer itself, so a pixel reads its own words as texels.
  - Each batch is drawn by one EmGs and by 3 and 8 row-band EmGs in lockstep. All 4,500 lines (the hash of the 4 MiB local memory, the drawn and refused primitives, the drawn pixels) are equal: 61,726 primitives and 121.1 million pixels at one band. The one-band run took 100.7 million samples and rejected 56.1 million pixels by Z (43.3 million textured) and 5.2 million textured pixels by DATE before sampling.
  - Four negative controls (zt/sweep/neg/), each a plausible mistake of this reorder, are caught in a 100-batch run: the Z address only when the test reads Z (ZTST ALWAYS writes Z to word 0) changes 267 of 300 lines; the triangle's Z computed after the test (the test sees the previous pixel's Z) 279; DATE skipped for textured pixels 207; ZTST NEVER passing 291.
- *The DOF captures:* the offline bench checks that all 4 MiB of local memory are equal between HEAD and this step after the pass in each of the 8 captures, at 1, 3 and 8 bands.

**Samples avoided** (the counters in the new order; runs/zc_demo_o1, zc_a01_o1). Per frame, the samples the previous order took, the Z- and DATE-rejected pixels that no longer sample, and the share avoided (of texel fetches, bilinear four, slightly less, as a DATE rejection is counted as one fetch):

| Range | Samples before | Now | Z-rejected textured | DATE-rejected | Avoided |
|---|---|---|---|---|---|
| AREA11 cinematic | 1,179k | 721k | 441k | 16.5k | 38.8 % |
| AREA11 calm | 1,169k | 600k | 546k | 23.0k | 48.7 % |
| AREA11 slide | 1,391k | 775k | 589k | 27.8k | 44.3 % |
| AREA01 beat | 1,741k | 938k | 786k | 16.9k | 46.1 % |
| AREA01 fire | 1,870k | 1,152k | 703k | 15.4k | 38.4 % |
| AREA01 fire peak | 2,119k | 1,439k | 659k | 20.5k | 32.1 % |

The Z rejections are higher than in the sizing (for example the slide 563k -> 589k) because pixels that also fail the alpha test under AFAIL KEEP now meet the Z test first; DATE likewise.

**Offline** (zt/bench/bench.py; the DOF pass of each fork capture through dof_replay, one EmGs, strict; HEAD's library and this step's alternated, best of 30): 15.0-16.7 ms -> 8.3-13.4 ms per pass, all 8 captures 127.2 -> 88.0 ms (-30.8 %). Three captures (open_w0520, open_wide_t0720, play_first_control) fall 42-45 %, the other five 19-33 %.

**Live** (zt/batch.sh, 11:20-12:55, plus zt/rerun.sh at 12:56; headless, uncapped; each group HEAD, P12, P12, HEAD; binaries zt/head_extermination, HEAD c35f3be from git archive, and zt/zt_extermination, a copy of build/extermination; runs/zt_*). The quiet gate (quiet.sh, 70 % idle) held before every run but two, which were re-run and are excluded: zt_demo_zt_b (idle 64.6 %; its figures agree: busiest 7.93 / 7.59 / 7.92 ms) and zt_demo3_head_a (idle 61.3 %; 16.28 / 16.04 / 17.56 ms). Elsewhere idle 70.2-85.6 %, 1-minute load 2.4-12.5. Mean ms per tick.

One worker (EM_GS_THREADS=1), the GS frame's CPU per frame:

| Range | HEAD a / b | P12 a / b | Change |
|---|---|---|---|
| AREA11 cinematic (1,310) | 41.67 / 42.10 | 33.25 / 33.31 | -8.6 ms (-20.5 %) |
| AREA11 calm (85) | 41.34 / 40.69 | 30.64 / 30.42 | -10.5 ms (-25.6 %) |
| AREA11 slide | 46.65 (121) / 44.52 (101) | 33.58 (121) / 30.17 (75) | -13.1 ms (-28 %, the a runs) |
| AREA01 beat (781) | 53.75 / 52.76 | 39.48 / 39.65 | -13.7 ms (-25.7 %) |
| AREA01 fire (376) | 57.64 / 56.67 | 45.14 / 45.31 | -11.9 ms (-20.8 %) |
| AREA01 fire peak (149) | 65.90 / 64.85 | 54.05 / 54.11 | -11.3 ms (-17.3 %) |

The b runs of the slide stopped before its end (the demo's stop is polled once a second, and one worker reaches it at about 20 ticks a second); the a runs cover all 121 ticks.

Default 6 workers. Each cell: busiest worker / tick wall / ticks over:

| Range | HEAD a, b | P12 a, c | Busiest worker |
|---|---|---|---|
| AREA11 cinematic (1,310) | 9.61, 9.49 / 10.02, 9.91 / 1, 2 | 7.94, 7.94 / 8.35, 8.40 / 0, 0 | -1.6 ms |
| AREA11 calm (85) | 9.56, 9.56 / 9.94, 9.92 / 0, 0 | 7.54, 7.43 / 8.02, 7.81 / 0, 0 | -2.1 ms |
| AREA11 slide (121) | 9.96, 10.12 / 10.37, 10.57 / 0, 0 | 7.94, 7.86 / 8.36, 8.32 / 0, 0 | -2.1 ms |

| Range | HEAD a, b | P12 a, b | Busiest worker |
|---|---|---|---|
| AREA01 beat (781) | 11.56, 11.67 / 12.23, 12.50 / 2, 12 | 9.19, 9.19 / 10.69, 10.75 / 0, 1 | -2.4 ms |
| AREA01 fire (376) | 12.30, 12.45 / 12.73, 13.05 / 1, 8 | 10.23, 10.21 / 11.12, 11.17 / 0, 1 | -2.2 ms |
| AREA01 fire peak (149) | 13.65, 13.79 / 14.05, 14.36 / 1, 5 | 11.76, 11.74 / 12.15, 12.23 / 0, 1 | -2.0 ms |

Every range ran at 99.8-100 % speed in both builds; the game thread is unchanged (AREA11 5.7-6.1 ms, AREA01 9.6-10.3 ms). Today's HEAD runs agree with P11's M1 runs (AREA11 busiest 9.5-10.1 ms, AREA01 11.6-13.8 ms). In AREA01 the tick is now 0.4-1.5 ms above the busiest worker: the game thread (about 10 ms) comes close to setting it.

Three workers (EM_GS_THREADS=3, the iPhone proxy). Each cell: busiest worker / ticks over / speed:

| Range | HEAD b, c | P12 a, b |
|---|---|---|
| AREA11 cinematic | 16.03, 15.97 / 548, 535 / 97.5, 97.7 % | 13.16, 13.11 / 40, 30 / 99.9, 100.0 % |
| AREA11 calm | 16.06, 15.97 / 10, 10 / 99.2, 99.2 % | 12.39, 12.34 / 0, 0 / 100, 100 % |
| AREA11 slide | 17.43, 17.33 / 66, 63 / 91.4, 91.7 % | 13.30, 13.30 / 0, 0 / 100, 100 % |

| Range | HEAD a, b | P12 a, b |
|---|---|---|
| AREA01 beat | 20.29, 20.23 / 622, 611 / 79.5, 79.7 % | 15.63, 15.57 / 406, 409 / 93.4, 93.7 % |
| AREA01 fire | 21.59, 21.58 / 356, 359 / 75.9, 76.0 % | 17.60, 17.52 / 247, 251 / 89.1, 89.3 % |
| AREA01 fire peak | 24.29, 24.30 / 149, 149 / 67.6, 67.7 % | 20.59, 20.54 / 149, 149 / 79.6, 79.8 % |

**Reading.**
- *Exactness.* Only the order of tests that read memory and store nothing changed, and the triangle no longer computes attributes a rejected pixel never uses; the proofs above are the evidence.
- *Gain.* 32-49 % of the texture samples are no longer taken (38.4 % in the fire, 44.3 % in the slide). The GS frame on one worker is 17-28 % cheaper; at 6 workers the busiest worker is 1.6-2.1 ms lower in AREA11 (the slide 10.0 -> 7.9 ms) and 2.0-2.4 ms lower in AREA01 (the fire peak 13.7 -> 11.8 ms). On one worker it is larger than M1's (cinematic -20.5 % against -15.7 %).
- *The Mac* now has 4.9 ms of the period to spare at the AREA01 fire peak's busiest worker, and no range ran more than one tick over.
- *The iPhone proxy* (3 workers): AREA11 now runs at full speed (busiest worker 12.3-13.3 ms; 30-40 of the cinematic's 1,310 ticks over, none in the calm or slide), AREA01 at 79.6-93.7 % (busiest 15.6-20.6 ms). Not measured on the device, where the efficiency cores make it worse (section 5).
- *What is left.* The remaining samples, the CT32 / T4 / T8 bilinear fetches, are M3's and M2's (the decoded texture cache, the specialized loops). Pixels the alpha test rejects under AFAIL KEEP still sample and are counted as samples.

### P13. The decoded texture cache (M3) (2026-10-15)

**What it is** (em_gs_raster.c section 4b; GS_EXACT.md 4.8). Every texture sample in the first level is bilinear: four texels, each a swizzled address, a memory read and, for PSMT8 / PSMT4, a palette lookup (P12's counters). Each EmGs now keeps PSMCT32 textures, and PSMT8 / PSMT4 textures with a PSMCT32 CLUT, as linear 32-bit copies with the palette applied. A copy covers the rectangle the texture's wrap can reach, and it is keyed by TBP0, TBW, PSM, the rectangle, CSA for PSMT4 and the palette it applied (compared in full, not only by hash). A row is decoded on its first read with fetch_rgba's own reads, and sample() and texel_rgba() read the copy at the indices the wrap gave. Every other texture, and a texture whose wrap can leave it (REGION_CLAMP with MINU > MAXU or past the texture, REGION_REPEAT past it), is read from memory as before.

**Why it gives the texels memory gives.** A decoded texel equals memory's for as long as its words and the CLUT stay unchanged since its row was decoded. Each way they can change is covered:
- *The CLUT.* The copy holds its own palette, and a bind finds a copy only when the current CLUT buffer's colours equal it. A CLUT load that changes the buffer bumps a version, so the next bind compares again. A load that changes it during a textured span ends the span first (clut_load), so a queued primitive still sees its own palette.
- *Draws.* Within a span FRAME, ZBUF, TEST and SCISSOR do not change (a write of one ends the span, section 3.7, and queue_prim checks that). Before a span draws, it stamps the 8 KiB pages that its frame, and its Z buffer when Z is written (ZTE and not ZMSK, the only case plot() and sprite_fill write Z), can reach inside the scissor. A copy whose pages were stamped since its rows were decoded is decoded again, and in the span that draws into its pages a texture is read from memory. So a primitive that samples its own frame or Z buffer, or what an earlier primitive of its span drew, reads memory as before. Over 8 page ranges in one span, all of memory counts as written.
- *Transfers and other writers.* HOST -> LOCAL data, LOCAL -> LOCAL, em_gs_write_pixel and em_gs_memory_written drop the copies over the pages they wrote. em_gs_memory_written is for memory written outside the EmGs: em_gs_world calls it for every worker after a memory image, a restore and an upload, each made while the workers wait. Recorded world frames hold no transfers (the recorder refuses them).
- *Pages.* Every layout's page index rises in x and in y, so the pages of an x / y rectangle are one range from bp. That holds for PSMT8 / PSMT4 at TBW 0 or 1 too: both the address functions and the copy use one page per row there.
- *Bands and threads.* Each EmGs holds its own copies, and every band takes every primitive, so each band stamps every span, whichever band draws its rows. In lockstep (EmGs.tee) a band can be ahead of another by the span being drawn, and that span's pages are read from memory. In em_gs_world, a copy's rectangle is exactly the reach the recorder orders for that texture (texture_entry's reach). A read of a block drawn since the last barrier waits for every band, and a draw into a block read since then waits for every band's reads. So a row decoded during a primitive reads what memory holds for all bands at that primitive, and stays right until a stamped span.

**Sizing** (the counters below). A copy table of 1,024 entries per EmGs (a full table is emptied) and 16 MiB of decoded texels per EmGs (the least recently used copy's texels are dropped first). The largest held were 6.25 MiB per worker over the demo to counter 4650 (6,551,616 bytes) and 10.19 MiB on the route to a01_00 (10,685,744 bytes), in 346 and 570 copies. No copy was evicted and no table was emptied. That is about 61 MiB over 6 workers on this Mac and about 41 MiB over the phone's 4. With no memory for a copy, the texels are read from memory.

**The counters** (EmGs.tcs; EM_GS_TC_COUNTERS=<file>, a developer switch, STARTUP.md "Developer switches"). One line per frame waited for, summed over the workers: textured primitives drawn from a copy and from memory, copies made and made again, evictions, resets, rows and texels decoded, samples taken from copies, and the most decoded bytes a worker held. EM_GS_TEXCACHE=0 turns the cache off for comparisons (the same bytes). The counts are always kept; only the file needs the switch.

| Range (workers) | Frames | Textured prims from a copy | Copies made / again | Texels decoded per texel read | Rows decoded per row read |
|---|---|---|---|---|---|
| Demo to 4650 (6) | 4,178 | 259.8M (all) | 2,076 / 215,340 | 0.32 | 0.17 % |
| Demo to 4650 (3) | 4,117 | 128.1M (all) | 1,038 / 106,023 | 0.30 | 0.15 % |
| Demo to 1780 (1) | 1,626 | 17.0M (all) | 343 / 13,357 | 0.21 | 0.11 % |
| Route to a01_00 (6) | 14,112 | 1,096.0M (all) | 3,420 / 781,020 | 0.28 | 0.15 % |
| Route to a01_00 (3) | 14,112 | 548.0M (all) | 1,710 / 390,510 | 0.27 | 0.14 % |

(m3/tcc_*.txt, runs/m3_tcc_*. Sums over workers, each worker making its own copies. A texel read is one of a bilinear sample's four (all but 0.03 % of the samples are bilinear). A decoded row also holds texels the scene never reads, so the texel ratio is an upper bound on the misses.)
- *Hit rate.* Every textured primitive drew from a copy. Of the row reads, 99.83-99.89 % found the row decoded. At most 0.32 texels were decoded per texel read, so the reads hit decoded texels at least 68 % of the time.
- *Copies made again.* These are textures that a later span drew into (render to texture), read again after the draw: about 9 per worker per frame.
- *Drawn in the span.* No textured primitive of the first level read a texture its own span draws into (that counter stays 0).

**Same bytes.**
- *The GS proof set* (EM_TEST_FULL=1, 2026-10-14, on these exact sources; receipts in build/perf_investigation/m3/proof/, src_md5.txt). Each log gives P12's results (zt/proof/) apart from timings and paths:
  - the raster reference: 931 / 1158 bit-exact, 0 failures;
  - gs_world_test: PASS at 1, 2, 3 and 8 workers and the host default (6), now also with the cache off against it and with an upload over a sampled texture between frames (1, 3 and 8 workers);
  - the DOF pass: 8 captures bit-exact;
  - fog, GS memory and gs_display_test: PASS;
  - the texture reference: 11,129 decodes equal;
  - `make test-gs-swizzle`: 2,153,286,588 addresses, 0 differ;
  - the AREA01 smoke: 20 live phases;
  - the full level smoke: the same 404 PASS lines as P12, no failures (61 minutes).
- *The fb2 harness* passes 8 of 19 points, and 7 of its 8 exact-pixel lines equal P12's. The eighth, 15_level_exit's presented frame (the GS field under the overlay pass, against the same-half-line original), reads 113,002 exact pixels instead of 113,053. HEAD's own binary gives 113,002 today too (m3/fb2check/: --point 15_level_exit with this step's binary, with EM_GS_TEXCACHE=0, and with HEAD d86f73c's). The port's presented and GS-field images are byte-identical in all three. So this step does not cause the change. The cause is P2's: the headless port frame follows the backing scale of the host's current display, and today it is 960x720 (m3/fb2check/*/result.json). P1 measured 113,002 at 960x720, and P2 113,053 at 1920x1440.
- *A self-check build* (m3/selfcheck/patch.py: every texel a copy gives compared with fetch_rgba's from memory at the same moment, aborting on a difference). It ran the demo to counter 4650 at 6 and 3 workers and the route to a01_00 (its 21 live phases passed): 15.37 billion samples (61.5 billion texels), 0 differences.
- *A random differential* (m3/sweep/sweep.c, from P12's; built against HEAD's em_gs_raster.c, m3/head_src, and this step's; re-run 2026-10-14 with m3/sweep/rerun.sh).
  - 1,500 seeded batches over every state P12's sweep randomizes, plus what the cache must follow: a pool of CT32 / T8 / T4 textures sampled again under several CLAMP words; render to texture (the frame and the Z buffer read back as CT32 / T8 / T4, in the drawing span and in later ones); HOST -> LOCAL transfers onto the textures and their CLUTs, some left unfinished; LOCAL -> LOCAL transfers; CLUT loads that change the CLUT; CSA changes; em_gs_write_pixel pokes; and every 25th batch 2,600 primitives over large textures (table resets and evictions).
  - Each batch is drawn by one EmGs and by 3 and 8 row-band EmGs in lockstep. All 4,500 lines (the hash of the 4 MiB local memory, the drawn and refused counts) are equal: 235,424 primitives and 127.6 million pixels at one band. Over all bands, 1.50 million textured primitives drew from copies and 0.74 million from memory (0.48 million because their span drew into the texture); 1.33 million copies made, 0.32 million made again, 1.13 million evictions, 540 table resets.
  - Eight negative controls (m3/sweep/neg/), each a plausible mistake, are caught in a 100-batch run (lines of 300 that differ):
    - transfers not dropping copies: 31;
    - draws not stamping pages: 33;
    - the span's own writes not checked: 3;
    - the palette not in the key: 174;
    - row flags kept when a copy is made again: 83;
    - the last bind reused across CLUT changes: 9;
    - em_gs_memory_written doing nothing: 16;
    - Z writes not stamped: 6.
- *The DOF captures.* The offline bench checks that all 4 MiB of local memory are equal between HEAD and this step after the pass in each of the 8 captures, at 1, 3 and 8 bands.

**Census per class** (m3/census: HEAD's and this step's raster with the same timing patch; one worker, the demo to counter 1780; runs m3c_head_c, m3c_m3_d, m3c_m3_e, m3c_head_d). Seconds and ns per drawn pixel, HEAD -> M3:

| Class | HEAD | M3 | Change |
|---|---|---|---|
| T4 triangles, bilinear | 15.73 s, 58.5 ns | 13.33-13.40 s, 49.6 ns | -15 % |
| CT32 sprites, bilinear, not blended | 9.36 s, 23.2 ns | 7.66-7.70 s, 19.0 ns | -18 % |
| T8 triangles, bilinear | 7.55 s, 46.7 ns | 6.42-6.46 s, 39.7 ns | -15 % |
| CT32 sprites, bilinear, blended | 7.29 s, 39.7 ns | 6.42-6.44 s, 35.0 ns | -12 % |
| T8 / T4 sprites, bilinear, blended | 0.34 s, 34.7-35.0 ns | 0.26 s, 24.0-26.7 ns | -23 to -31 % |
| Untextured triangles | 3.29 s, 16.5 ns | 3.23-3.25 s, 16.1-16.2 ns | -2 % |
| All | 46.99 / 47.04 s, 31.1 ns | 40.53 / 40.72 s, 26.9 ns | -13.7 % |

Not the estimated "up to 2x": the copy replaces the address, the memory read and the palette lookup, but the bilinear arithmetic, the texture function, the blend and the frame and Z words stay.

**Offline** (m3/bench/bench.py; the DOF pass of each fork capture through dof_replay, one EmGs, strict; both libraries built from source and alternated, best of 30; m3/bench/bench2.txt): 7.05-11.10 ms per pass, against HEAD's 8.14-13.26 ms. All 8 captures 87.1 -> 74.0 ms (-15.1 %).

**Live** (m3/batch.sh at 21:10-21:26 on 2026-10-14 for AREA11, and m3/batch2.sh at 23:40-00:50 for AREA01, the census and the counters; headless, uncapped; each group HEAD, M3, M3, HEAD; binaries m3/head_extermination, HEAD d86f73c from git archive, and m3/m3_extermination, both built 2026-10-14 with the Makefile's command, 0 warnings). The quiet gate (quiet.sh, 70 % idle) held before every kept run: idle 70.0-80.9 %, 1-minute load 3.3-13.6, except the first run (24.9, just after the builds, at 72.3 % idle). Mean ms per tick.

Excluded and re-run: the first AREA01 pass (kept as runs/m3p_*).
- m3_a01_m3_a stalled in the departure movie. The movie decoder gave no picture for 17 minutes, so START could not skip it, and it was stopped. The game thread was in the stream and sound fields, no GS code; the same binary passed the movie in every other run. Its log and stack are in m3/stuck/.
- Another session's full level smoke ran from about 22:00. It put m3_a013_m3_a at 55.7 % idle and m3_a013_m3_b at 3.2 %.
- The whole AREA01 part was run again after it ended.

One worker (EM_GS_THREADS=1), the GS frame's CPU per frame:

| Range | HEAD a / b | M3 a / b | Change |
|---|---|---|---|
| AREA11 cinematic (1,310) | 33.37 / 32.66 | 28.68 / 28.53 | -4.4 ms (-13.4 %) |
| AREA11 calm (85) | 30.47 / 29.61 | 26.49 / 27.30 | -3.1 ms (-10.5 %) |
| AREA11 slide | 31.75 (88) / 31.99 (98) | 28.31 (110) / 27.72 (100) | -3.9 ms (-12.1 %) |
| AREA01 beat (781) | 40.47 / 39.84 | 33.43 / 33.24 | -6.8 ms (-17.0 %) |
| AREA01 fire (376) | 46.25 / 45.57 | 37.20 / 37.07 | -8.8 ms (-19.1 %) |
| AREA01 fire peak (149) | 55.32 / 54.51 | 43.82 / 43.49 | -11.3 ms (-20.5 %) |

The slide's runs stop at different ticks (the demo's stop is polled once a second); the counts are in brackets.

Default 6 workers. Each cell: busiest worker / tick wall / ticks over:

| Range | HEAD a, b | M3 a, b | Busiest worker |
|---|---|---|---|
| AREA11 cinematic (1,310) | 7.88, 7.88 / 8.32, 8.33 / 0, 0 | 7.33, 7.33 / 7.81, 7.81 / 0, 0 | -0.55 ms |
| AREA11 calm (85) | 7.46, 7.49 / 7.88, 7.92 / 0, 0 | 7.17, 7.19 / 7.65, 7.60 / 0, 0 | -0.3 ms |
| AREA11 slide (121) | 7.84, 7.85 / 8.30, 8.31 / 0, 0 | 7.34, 7.22 / 7.72, 7.69 / 0, 0 | -0.55 ms |
| AREA01 beat (781) | 9.18, 9.33 / 10.64, 11.08 / 0, 0 | 8.28, 8.26 / 10.22, 10.23 / 0, 0 | -1.0 ms |
| AREA01 fire (376) | 10.19, 10.35 / 11.06, 11.49 / 0, 0 | 8.91, 8.90 / 10.26, 10.27 / 0, 0 | -1.4 ms |
| AREA01 fire peak (149) | 11.70, 11.88 / 12.10, 12.54 / 0, 0 | 9.99, 9.96 / 10.54, 10.54 / 0, 0 | -1.8 ms |

Every range ran at 100 % speed in both builds. The game thread is unchanged (AREA11 5.7-6.0 ms, AREA01 9.6-10.4 ms). In AREA01 the tick (10.2-10.5 ms) is now 0.6-1.9 ms above the busiest worker, so the game thread sets it.

Three workers (EM_GS_THREADS=3, the iPhone proxy). Each cell: busiest worker / ticks over / speed:

| Range | HEAD a, b | M3 a, b |
|---|---|---|
| AREA11 cinematic | 13.10, 13.13 / 38, 45 / 100.0, 99.4 % | 11.90, 11.91 / 0, 1 / 100, 100 % |
| AREA11 calm | 12.23, 12.28 / 0, 0 / 100, 100 % | 11.47, 11.51 / 0, 0 / 100, 100 % |
| AREA11 slide | 12.82 (99), 13.19 (115) / 0, 0 / 100, 100 % | 11.84, 11.86 (121) / 0, 0 / 100, 100 % |
| AREA01 beat | 15.69, 15.68 / 413, 409 / 93.3, 93.3 % | 13.70, 13.72 / 84, 84 / 98.6, 98.6 % |
| AREA01 fire | 17.63, 17.63 / 253, 251 / 88.8, 88.9 % | 14.96, 14.98 / 84, 84 / 97.2, 97.1 % |
| AREA01 fire peak | 20.67, 20.67 / 149, 149 / 79.2, 79.2 % | 17.15, 17.17 / 81, 81 / 93.1, 93.1 % |

**Reading.**
- *Exactness.* The copies hold the texels memory holds, the rules above say when they stop doing so, and the proofs above are the evidence. With the cache off (EM_GS_TEXCACHE=0) the path is HEAD's.
- *Gain.* The pixel cost is 13.7 % lower (31.1 -> 26.9 ns a drawn pixel). The GS frame on one worker is 10.5-13.4 % cheaper in AREA11 and 17-20.5 % in AREA01, where more of the frame is textured sprites. At 6 workers the busiest worker falls 0.3-0.55 ms in AREA11 and 1.0-1.8 ms in AREA01; at 3 workers 0.8-1.3 ms and 2.0-3.5 ms.
- *The Mac* runs every range at full speed with no tick over, with 6.1 ms of the period to spare at the AREA01 fire peak (tick 10.5 ms). The game thread is now AREA01's critical path.
- *The iPhone proxy* (3 workers, M1 performance cores) runs AREA11 at full speed (0-1 of the cinematic's 1,310 ticks over) and AREA01 at 93.1-98.6 % (busiest worker 13.7-17.2 ms; 81-84 ticks over in each AREA01 range). Not measured on the device, where the efficiency cores make it worse (section 5). Each worker's copies add up to 10.2 MiB of memory.
- *What is left.* The bilinear arithmetic and the per-pixel state switches of the generic loop (M2's specialized spans), the duplicated front end and the static bands (M4), and NEON (L1).

### P14. Specialized span loops (M2) (2026-10-15)

**Which states** (a state census: a measurement-only build, m2/census/state_patch.py, that times each drawn primitive by every drawing state a loop could fold; one worker, the demo to counter 1780, 40.75 s of GS CPU; m2/census/state_demo1.txt, and the route to a01_00 at 6 workers for the AREA01 states, state_a01.txt). All of them draw into the PSMCT32 field with a PSMZ24 Z buffer, COLCLAMP on, no DATE, no SCANMSK, no FBA or PABE, and every texture sample is bilinear from a decoded copy (P13). Shares of the demo's GS CPU:
- *Textured triangles* (53.8 %): T4 STQ, MODULATE or HIGHLIGHT, fog, alpha GREATER 0, Z GEQUAL (32.8 %); T8 STQ, CLAMP, Z ALWAYS with ZMSK (15.7 %); CT32 STQ, REGION_CLAMP, Z ALWAYS, with and without the additive blend (5.3 %).
- *The depth-of-field pass* (34.8 %): the copy (UV CT32 sprites, DECAL, REGION_CLAMP, Z ALWAYS with ZMSK, no blend; 18.9 %) and the blend back (MODULATE, alpha NEVER with FB_ONLY, Z GEQUAL against the pass's plane, (Cs - Cd) As + Cd; 15.9 %).
- *gsw_alpha_clear's strip and the drop shadow's boxes* (8.0 %): untextured flat triangles, A = Cd, B = 0, C = FIX 128, D = 0, alpha NEVER with FB_ONLY, Z GEQUAL with ZMSK (P7, "Not covered").
- *The snow sprites* (0.5 %; on the route the snow and fire sprites take 4.9 %): T8 / T4 STQ sprites, MODULATE, alpha NEVER with RGB_ONLY, Z GEQUAL, (Cs - Cd) As + Cd or Cs * FIX + Cd, with and without fog.
- Not taken: the DATE triangles (1.1 %), the untextured sprites (P7's fill, 0.9 %) and the lines.

**What changed** (em_gs_raster.c section 9b; GS_EXACT.md 5.4).
- *The loops.* One per kind (textured sprite, textured triangle, untextured triangle), Z mode (off or ALWAYS, GEQUAL, GREATER) and blend (on, off): 18 functions from three templates. spec_select sends a primitive to one when it is a sprite or a triangle drawn into a PSMCT32 frame without DATE or SCANMSK, with Z off or a Z32 / Z24 buffer where Z is read or written, and, when textured, bilinear from a decoded copy. Every other primitive, and every one with EM_GS_SPEC=0, draws through the generic path, whose source is unchanged: plot_dst, plot_src and fetch_rgba compile to the same instructions as HEAD's.
- *Hoisted.* The primitive's state is read once into locals (the generic path reads it through the Draw after every store, as a byte store may alias it). The frame's and the Z buffer's row in the address tables (P11) is looked up once a row. A sprite's texel columns and bilinear weights are computed once a column (its U depends on x alone, P6) and its two texel rows once a row, at the row's first pixel that passes the Z test, as sample() decodes them (P13). The decoded copy's storage is read once a primitive; only a row's flag is read again.
- *Inlined.* plot()'s tests and arithmetic, sample(), the copy's read and the wrap run inline in each loop, in plot()'s order: the Z test, the frame read, the sample, the texture function, fog, the alpha test, the blend, the clamp, FBA and FBMSK, the frame word before the Z word. The counters (EmGs.pix, drawn pixels) are plot()'s.
- *Two choices made per primitive.* The texture function's four TFX cases are one expression (DECAL as a multiply by 2^14 and a shift by 14, which gives the texel exactly), and what a pixel that fails the alpha test writes (AFAIL) is chosen once.
- *One rewrite.* The bilinear step a + (((b - a) * f) >> 4) is computed as (a * (16 - f) + b * f) >> 4. Both are floor((16 a + (b - a) f) / 16): 16 a is a multiple of 16, so it moves inside the floor exactly, and the sum is never negative. The four 8-bit channels then share one 64-bit multiply in 16-bit lanes; each lane is at most 255 * 16, so none carries into the next.
- *Not changed:* every coordinate and attribute expression (draw_sprite's and draw_triangle's, copied as written; -ffp-contract=off, P1), the spans, the decoded copies, sprite_fill (P7) and the generic path.
- *Developer switches* (STARTUP.md "Developer switches"): EM_GS_SPEC=0 (every primitive through the generic path), EM_GS_SPEC_CHECK=1 (each primitive a loop takes is drawn by the generic path first, its frame and Z words put back, then drawn by the loop, and the words and counters compared; a difference aborts) and EM_GS_SPEC_COUNTERS=<file> (the loops' primitives and pixels per frame). The counts are always kept (EmGs.spc).

**Same bytes.**
- *The GS proof set* (EM_TEST_FULL=1, 2026-10-15, on these exact sources; receipts in build/perf_investigation/m2/proof/, src_md5.txt). Each log gives P13's results (m3/proof/) apart from timings and paths:
  - the raster reference: 931 / 1158 bit-exact, 0 failures, its log line for line equal to P13's;
  - gs_world_test: PASS at 1, 2, 3 and 8 workers and the host default (6), with the DOF pass, the cache off and the upload between frames, and now the loops off (EM_GS_SPEC=0, 1 and 8 workers) and the loops checked against the generic path (EM_GS_SPEC_CHECK=1, 1 and 3 workers: 92 primitives of all three loops a run);
  - the DOF pass: 8 captures bit-exact;
  - fog, GS memory and gs_display_test: PASS;
  - the texture reference: 11,129 decodes equal;
  - `make test-gs-swizzle`: 2,153,286,588 addresses, 0 differ;
  - the fb2 harness: 8 of 19 points, every exact-pixel line equal to P13's (15_level_exit's presented frame 113,002, the display's backing scale as at P13);
  - the AREA01 smoke: 20 live phases, its PASS line equal to P13's;
  - the full level smoke: the same 404 PASS lines as P13, no failures (60 minutes).
- *The live check* (m2/selfcheck.sh, EM_GS_SPEC_CHECK=1 on the M2 binary): the demo to counter 4650 at 6 workers (260.0 million primitives checked, summed over the workers) and at 3 (129.1 million), and the route to a01_00 at 6 workers (1,117.2 million; its 21 live phases passed): 0 differences. On the route the loops drew 12.44 of the 14.35 billion pixels (86.7 %; 14,354,985,111 drawn, the count of P12's two orders), on the demo 85.1 %.
- *A random differential* (m2/sweep/sweep.c, from P13's; built against HEAD's em_gs_raster.c, m2/head_src, and against this step's; m2/sweep/build.sh):
  - 1,500 seeded batches with every state P13's sweep randomizes (formats, pages, widths, the tests, blend, fog, dither, SCANMSK, COLCLAMP, UV and STQ, the wrap modes, the scissor, transfers, CLUT changes, render to texture, pokes), biased to the loops' states: CT32 frames 7 in 8, Z24 / Z32 3 in 4, DATE 1 in 20, SCANMSK 1 in 16, bilinear 3 in 4, blend 1 in 2 with every A, B, C, D and FIX, fog 1 in 3, AREF near 0, 64, 128 and 192 half the time.
  - Each batch is drawn by one EmGs and by 3 and 8 row-band EmGs in lockstep. All 4,500 lines (the hash of the 4 MiB local memory, the drawn and refused counts) are equal between HEAD and M2, and equal again with the check on (sweep_m2chk) and with the loops off (sweep_m2off): 241,116 primitives and 134.4 million pixels at one band. Over all bands the loops took 829,824 primitives and drew 127.2 of the 403.2 million pixels; every loop and every value of the state they read occurred (m2/sweep/cov.err).
  - Twelve negative controls (m2/sweep/neg/), each a plausible mistake in the loops, are caught both by the differential (lines of 300 that differ in a 100-batch run) and by the check, which aborts on each: the bilinear step rounded 222; GEQUAL taken as GREATER 70; the Z24 read not masked 267; the Z24 write without its top byte 267; FBMSK ignored 90; RGB_ONLY writing the alpha 120; PABE ignored 81; DATE accepted by the loops 87; REGION_CLAMP taken as REGION_REPEAT 165; the triangle's fog lane one lane off 147; a sprite's V not advanced over the rows above the scissor 48; HIGHLIGHT's alpha without the vertex alpha 93.
- *The bilinear rewrite* (m2/bilerp/bilerp_check.c): equal to sample()'s step for every a, b in 0..255 and weight 0..15 (1,048,576 cases), and sp_bilinear equal to sample()'s loop on 204.2 million texel quadruples (every value of one channel against random others, and random ones).
- *The DOF captures.* The offline bench checks that all 4 MiB of local memory are equal between HEAD and this step after the pass in each of the 8 captures, at 1, 3 and 8 bands.

**Census per class** (m2/cen: HEAD's and this step's raster with P13's timing patch, adapted to M2's dispatch, m2/cen/patch.py; one worker, the demo to counter 1780; runs m2c_head_b, m2c_head_c, m2c_m2_b, m2c_m2_c, at 76.2-80.5 % idle). Seconds and ns per drawn pixel, HEAD -> M2:

| Class | HEAD | M2 | Change |
|---|---|---|---|
| T4 triangles, bilinear, Z GEQUAL | 13.19-13.50 s, 49.2-49.6 ns | 9.70-9.75 s, 36.1 ns | -27 % |
| CT32 sprites, bilinear, Z ALWAYS (the DOF copy) | 7.59-7.81 s, 18.9-19.1 ns | 3.02-3.04 s, 7.5 ns | -61 % |
| CT32 sprites, bilinear, blended, Z GEQUAL (the DOF blend) | 6.36-6.50 s, 34.6-35.3 ns | 2.93-2.94 s, 16.0 ns | -54 % |
| T8 triangles, bilinear, Z ALWAYS | 6.38-6.53 s, 39.6-39.8 ns | 4.20-4.22 s, 25.9 ns | -35 % |
| Untextured triangles, blended, Z GEQUAL (gsw_alpha_clear) | 3.19-3.29 s, 16.0-16.2 ns | 1.49-1.50 s, 7.4 ns | -54 % |
| CT32 triangles, bilinear, Z ALWAYS (two classes) | 2.15-2.16 s | 1.50-1.51 s | -30 % |
| T8 / T4 sprites, bilinear, blended (the snow) | 0.26-0.28 s, 23.8-26.7 ns | 0.15 s, 13.1-15.4 ns | -43 % |
| The generic path's classes (DATE triangles, P7's fills, lines) | 0.80-0.84 s | 0.81-0.82 s | 0 |
| All | 40.13 / 41.08 s, 26.7-26.9 ns | 23.95 / 24.06 s, 15.9 ns | -41 % |

The classes left on the generic path are unchanged, so P7's hazard (an edit that slows every class) did not occur. HEAD a (m2c_head_a, started at a 1-minute load of 40) gave 27.99 ns and the disturbed M2 a (52.7 % idle) 16.29 ns; neither is used. A first version of the loops (the texture function and the alpha test as plot_src's switches) drew the DOF blend at 13.4-13.7 ns and the copy at 7.3-7.4 ns but the T8 triangles at 28.6-28.7 ns (m2c_pre_m2, m2c_pre_m2b, measured at 70.7-73.2 % idle); the final form is faster over the whole frame (24.0 against 24.2-24.3 s).

**Offline** (m2/bench/bench.py; the DOF pass of each fork capture through dof_replay, one EmGs, strict; both libraries built from source and alternated, best of 30; m2/bench/bench2.txt, at 74 % idle): 2.97-4.89 ms per pass, against HEAD's 7.22-11.10 ms. All 8 captures 74.4 -> 32.0 ms (-57.0 %).

**Live** (m2/batch.sh at 02:27-03:13 on 2026-10-15 for AREA11 and a first AREA01 pass, m2/rerun.sh at 04:50-06:28 for AREA01 again, the census, the offline bench and the counters, m2/rerun2.sh at 06:29 for the census's M2 run; headless, uncapped; each group HEAD, M2, M2, HEAD; binaries m2/head_extermination, HEAD e33e1bf from git archive, and m2/m2_extermination, a copy of build/extermination, both built with the Makefile's command, 0 warnings). The quiet gate (quiet.sh, 70 % idle) held before every kept run: idle 70.0-80.5 %, 1-minute load 3.8-15.5. Mean ms per tick.

Excluded and re-run (another session's simulator and package tests, and a Spotlight re-index, loaded the machine in bursts from about 02:45):
- Started below the gate: m2_a01_m2_a (idle 17.5 %), m2_a01_m2_b (3.8 %), m2_a013_m2_a (51.8 %), m2_a01_head_c (46.9 %), m2c_m2_a (52.7 %).
- Passed the gate but ran into a burst: m2_a01_head_b (33 ticks over; busiest 9.05 / 9.48 / 11.33 ms) and m2_a013_head_b (501 ticks over, 87.9 % speed).
- The AREA01 runs at 6 and 3 workers were made again (m2/rerun.sh, each run after m2/gate.sh: three readings in a row at 70 % idle or more), the census's M2 run too (m2/rerun2.sh). The first pass's undisturbed AREA01 HEAD runs agree with the second's (6 workers m2_a01_head_a: 9.00 / 9.79 / 10.45 ms; 3 workers m2_a013_head_a: 13.79 / 15.03 / 17.29) and m2_a01_head_a is used below for the pair at 6 workers.
- m2/batch.sh was stopped before its one-worker AREA01 group, which m2/rerun.sh ran.

One worker (EM_GS_THREADS=1), the GS frame's CPU per frame:

| Range | HEAD a / b | M2 a / b | Change |
|---|---|---|---|
| AREA11 cinematic (1,310) | 28.95 / 29.46 | 17.63 / 17.58 | -11.6 ms (-39.7 %) |
| AREA11 calm (85) | 26.44 / 27.27 | 16.36 / 16.79 | -10.3 ms (-38.3 %) |
| AREA11 slide | 28.19 (101) / 28.49 (99) | 17.10 (121) / 17.94 (121) | -10.8 ms (-38.2 %) |
| AREA01 beat (781) | 33.98 / 34.34 | 19.71 / 19.75 | -14.4 ms (-42.2 %) |
| AREA01 fire (376) | 37.94 / 37.65 | 21.68 / 21.72 | -16.1 ms (-42.5 %) |
| AREA01 fire peak (149) | 44.54 / 44.10 | 25.14 / 25.20 | -19.2 ms (-43.2 %) |

The slide's runs stop at different ticks (the demo's stop is polled once a second); the counts are in brackets.

Default 6 workers. Each cell: busiest worker / tick wall / ticks over:

| Range | HEAD a, b | M2 a, b | Busiest worker |
|---|---|---|---|
| AREA11 cinematic (1,310) | 7.86, 7.92 / 9.11, 8.96 / 9, 0 | 5.77, 5.74 / 6.88, 6.82 / 0, 0 | -2.1 ms |
| AREA11 calm (85) | 8.51, 7.71 / 11.59, 8.74 / 0, 0 | 5.80, 6.14 / 6.80, 7.56 / 0, 0 | -2.1 ms |
| AREA11 slide (121) | 8.21, 7.44 / 11.10, 8.12 / 0, 0 | 5.91, 5.86 / 7.32, 7.45 / 0, 0 | -1.9 ms |

| Range | HEAD a, d | M2 c, d | Busiest worker |
|---|---|---|---|
| AREA01 beat (781) | 9.00, 8.56 / 11.92, 10.93 / 3, 1 | 5.98, 6.02 / 10.49, 10.53 / 0, 0 | -2.8 ms |
| AREA01 fire (376) | 9.79, 9.22 / 12.49, 11.17 / 3, 1 | 6.23, 6.31 / 10.37, 10.48 / 0, 0 | -3.2 ms |
| AREA01 fire peak (149) | 10.45, 10.31 / 11.78, 11.51 / 0, 1 | 6.79, 7.00 / 9.97, 10.28 / 0, 0 | -3.5 ms |

Every range ran at 99.9-100 % speed in both builds. The game thread is unchanged (AREA11 6.0-6.6 ms, AREA01 9.9-10.7 ms). HEAD a's calm and slide took 2.9-3.1 ms more wall time a tick than their busiest worker's CPU (HEAD b 0.7-1.0 ms), so its workers were off-core for part of those ticks; its busiest-worker figures are kept. In AREA01 the tick (10.0-10.5 ms in M2) is the game thread's: the busiest worker is 3.2-4.5 ms under it.

Three workers (EM_GS_THREADS=3, the iPhone proxy). Each cell: busiest worker / ticks over / speed:

| Range | HEAD a, b | M2 a, b |
|---|---|---|
| AREA11 cinematic | 12.11, 12.10 / 3, 3 / 100, 100 % | 8.14, 8.25 / 0, 0 / 100, 100 % |
| AREA11 calm | 11.65, 11.86 / 0, 0 / 100, 100 % | 8.15, 8.02 / 0, 0 / 100, 100 % |
| AREA11 slide | 12.21, 12.15 / 0, 0 / 100, 100 % | 7.93 (111), 7.83 (121) / 0, 0 / 100, 100 % |

| Range | HEAD c, d | M2 c, d |
|---|---|---|
| AREA01 beat | 13.75, 13.91 / 87, 103 / 98.6, 98.2 % | 8.83, 8.85 / 0, 0 / 100, 100 % |
| AREA01 fire | 15.02, 15.06 / 86, 84 / 97.1, 97.0 % | 9.40, 9.44 / 0, 0 / 100, 100 % |
| AREA01 fire peak | 17.21, 17.22 / 85, 80 / 92.9, 92.8 % | 10.59, 10.61 / 0, 0 / 100, 100 % |

**The loops' share** (EM_GS_SPEC_COUNTERS; m2/spc_demo.txt, spc_demo3.txt, spc_a01.txt; deterministic, the same in every run). Of the pixels drawn, summed over the workers:
- the demo to counter 4650 (6 workers): 85.1 % through the loops (textured sprites 1,435M, textured triangles 1,159M, untextured triangles 511M of 3,651M); at 3 workers 85.0 %;
- the route to a01_00 (6 workers): 86.7 % (5,842M, 4,109M and 2,490M of 14,355M).

The rest are P7's fills (the field's Z clear, the full clears, the colour fills), the DATE triangles and the lines.

**Reading.**
- *Exactness.* Only how the work is arranged changed; the loops run the generic path's rules in its order, and the proofs above (the proof set, the live check over the demo and the route, the differential with its twelve controls) are the evidence, not the label. With EM_GS_SPEC=0 the path is HEAD's.
- *Gain.* The census's pixel cost fell 26.8 -> 15.9 ns (-41 %), within the plan's estimate of 1.5-2x on the remaining pixel path (1.69x). The GS frame on one worker is 38-40 % cheaper in AREA11 (cinematic 29.2 -> 17.6 ms) and 42-43 % in AREA01 (fire peak 44.3 -> 25.2 ms). The DOF pass is 2.3 times cheaper (offline -57 %), the world's textured triangles 1.4-1.5 times (their per-pixel coordinate arithmetic, two double and two single divisions, is the exact model's and stays).
- *The Mac* (6 workers): the busiest worker fell 1.9-2.1 ms in AREA11 (cinematic 7.9 -> 5.8 ms) and 2.8-3.5 ms in AREA01 (fire peak 10.4 -> 6.9 ms). Every range ran at full speed in both builds; the game thread (6 ms in AREA11, 10 ms in AREA01) now sets AREA01's tick with 3-4 ms to spare on the workers.
- *The iPhone proxy* (3 workers, M1 performance cores): the busiest worker fell 3.7-4.3 ms in AREA11 (7.8-8.3 ms) and 5.0-6.6 ms in AREA01 (8.8-10.6 ms). AREA01 now runs at full speed (no tick over in any range; HEAD 92.8-98.6 % with 80-103 ticks over), and the GS frame fits the period with 6 ms to spare at the fire peak. Not measured on the device, where the efficiency cores make it worse (section 5).
- *What is left.* The textured triangles' exact coordinate arithmetic (STQ: barycentric weights, binary32 sums and two quotients per pixel), now the largest cost (55 % of the census's GS CPU before, DATE ones included, and 66 % after); the duplicated front end and the static bands (M4); NEON (L1). The DOF sprites would gain about 0.1-2.6 ns a pixel from the first version's pixel stage (plot_src's switches, measured above) while the triangles keep the final one: a per-loop choice left for later.
- *Not covered:* nothing was measured on the iPhone, and the iOS build was not re-run (plain C in files already in the project). The 18 loops make the binary 130 KiB larger (4,667,760 -> 4,800,416 bytes); the generic path is unchanged.

### P15. Parse once, bands from a queue (M4) (2026-10-15)

**The frame's structure** (EM_GS_LIST_COUNTERS, a diagnostic added with this step; m4/lc_demo3.txt, lc_demo.txt, lc_demo10.txt, lc_a01.txt, lc_a013.txt; the in-level AREA11 frames 400-1600 of the demo, the AREA01 frames of the route). An AREA11 frame holds about 605 spans and 12,200 primitives in 10.1 segments (the stretches between the recorder's barriers: the world, then the depth-of-field pass's copies and blends), an AREA01 frame 940 spans and 12,100 primitives in 10.2 segments. Parsing a frame once takes 1.6-1.8 ms of CPU (the register decode, the setup and the queue that every worker ran before).

**What changed** (em_gs_raster.c section 12, em_gs_world.c; GS_EXACT.md section 9, "Workers and order").
- *Parse once.* With more than one worker, one EmGs (the parser) takes the frame's writes and, where a span ends, appends the span to a list instead of drawing it: the primitives as queued (Draw, context, vertices, the grid decision), the CLUT buffer and DIMX they are drawn with, the rows each can reach (its vertices' rows, two rows wider, inside the scissor) and the pages the span writes. The storage is chunked and never moves, so the workers draw spans the parse has published while it goes on. With one worker the frame is drawn directly, as before.
- *Bands from a queue.* Each segment is cut into two bands a worker (EM_GS_BANDS) over the rows it draws, the first open to the top and the last to the bottom. The workers take bands in order; a worker left without one asks the band with the most work left for the lower half of its rows, which its owner gives at its next span (it keeps the upper half). Each band is drawn by one worker, its primitives in the list's order with their state; every band of a segment ends before any band of the next starts (the barrier); the field is read out in 16-row pieces after the last segment.
- *The decoded copies.* A band's walk gives its worker's copies (P13) the pages of the spans it skips, and a worker passes them the pages of the segments it drew none of, so a copy is renewed after every span that wrote its pages, as in the direct path.
- *The early parse.* The Metal backend calls em_gs_world_preparse before it waits for the previous frame's field: the game's thread parses the next frame while the previous one is drawn, until that frame is done, and the workers go on from there. A CLUT load that reads a page the previous frame draws waits for it first.
- *The parse's one memory read.* A CLUT load from a page a span of the frame's own list draws is refused (EM_GS_REFUSE_ORDER, fail-stop): what it would read depends on the drawing. The first level has none (the proof set below). (Since P17 such a load is drawn exactly: the segment ends there and the spans so far are drawn first.)
- *Developer switches* (STARTUP.md "Developer switches"): EM_GS_BANDS=<n>, EM_GS_LIST=1 (one worker draws the parsed list), EM_GS_EARLY_PARSE=0, EM_GS_EARLY_OPS=<n> (a test switch: the early parse at every kick, handing over after n ops), EM_GS_LIST_COUNTERS=<file>.

**Same bytes.**
- *The GS proof set* (EM_TEST_FULL=1, 2026-10-15, on these exact sources; receipts in build/perf_investigation/m4/proof/, src_md5.txt): the raster reference, the DOF pass, fog, GS memory, gs_display_test, the texture reference (11,129 decodes) and `make test-gs-swizzle` (2,153,286,588 addresses) give P14's logs line for line; part F of the raster reference now also draws every capture packet through row-range bands (3 cut at seeded rows; with EM_TEST_FULL=1 also 8, and 5 cut in the first 40 rows): 0 differ. gs_world_test passes with its new parts: frames parsed early while the one before is drawn (a CLUT loaded from that frame's field, so the early parse waits for it) at 3 and 8 workers, the early parse forced to hand over after 37 ops (3 and 8 workers, and one worker drawing the list), the early parse off, 1 and 8 bands a worker, and the list's texture-cache stamps with two drawing models in a fixed band order. The fb2 harness passes at 8 of 19 points with every exact-pixel line equal to P14's except the level exit's presented frame, 113,053 exact pixels instead of 113,002: HEAD's binary gives 113,053 today too, with byte-identical port images (m4/fb2check: HEAD, M4, HEAD, M4); the headless frame follows the display's backing scale (P13). The AREA01 smoke: 20 live phases, its PASS line equal to P14's. The full level smoke: the same 404 PASS lines as P14, no failures (48 minutes). One comment-only edit (the header of tests/gs_world_test.c) came after the proofs; `make test-gs-world` passes again with it (m4/proof/test-gs-world-after-comment.log, src_md5_after_comment.txt).
- *A random differential* (m4/sweep/wsweep.c; build.sh, run.sh, all.sh; final.txt). The world's own API, as the renderer drives it: each seeded batch restores a random 4 MiB memory and records four frames (world frames with a head, list frames; fields A and B alternating), each a random sequence of primitives with their environment (five buffers, among them a 512 x 300 one taller than the field, CT32 and CT16 frames, Z32 / Z24 with ZMSK, points, lines, triangles, sprites, UV and STQ, nearest and bilinear, every wrap mode, CT32 / T8 / T4 textures with CLD 0-5 and CSA, render to texture through the recorder's barriers, every Z test, the alpha test, DATE, every blend), FOGCOL and DIMX writes, GIF state blocks, the environment again, CLUTs loaded from a buffer an earlier frame drew, and a depth-of-field-like pass; after each frame the hash of all 4 MiB, the field and the counts. 1,500 batches (6,000 frames, 246,149 primitives, 1.08 billion pixels): HEAD at 1, 3 and 8 workers and M4 at 1 (direct), 3, 8 and the host default (6), with one worker drawing the list, with 1 and 8 bands a worker, with the early parse forced after 37 ops (3 and 8 workers), with it off and without em_gs_world_preparse: every line equal. Under the thread sanitizer (8 workers, 400 batches): no warning, every line equal. What it exercised at 8 workers: 2,776 frames with barriers, 15,648 segments, 242,722 bands, 29,469 splits, the early parse in 1,465 frames (643,043 ops) with 280 waits for the frame before.
- *Negative controls* (m4/sweep/neg/): fifteen plausible mistakes, each in a copy of the sources. Fourteen are caught by the differential (lines of 1,200 that differ at 3 workers / 8 workers / one worker drawing the list): a primitive's rows without the two-row margin 58 / 175 / 18; a band's top row off by one 13 / 82 / 4; a span's rows without its first primitive 1,194 in every configuration; the next segment started while one band still draws 63 / 334; the span's CLUT not taken 1,071; the CLUT snapshots keyed per list 29 / 45 / 14; DIMX not taken 44; the grid decision not stored 681; split rows drawn from the segment's first span 1,067 / 1,076; the split's owner keeping its rows 817 / 957; the last band closed at the partition's end 300 / 352; the early parse reading a CLUT the frame before still draws 9 / 8; the readout's pieces a row short 1,200; the segments a worker drew none of not passed to its copies 15 / 187. The fifteenth, the skipped spans' pages not stamped, draws the same in the differential (no batch had a worker skip a span that wrote pages its copies held and then sample them) and is caught by gs_world_test's stamp case, which catches the unpassed segments too.
- *Three defects the differential found during the step* (fixed before the proofs above; the first is also one of the controls): the CLUT snapshots were first keyed per list, and the two frames' lists (double-buffered for the early parse) gave equal keys to different palettes; a band read the segment's end before the published count, so it could draw the next segment's first spans before the barrier (seen as a few duplicate pixels under load); and the recorder's own ordering: after a barrier for a texture read, the drawing target's blocks were not drawn ones again, so a read of them later in the same stretch had no barrier. The last is HEAD's recorder too (its replaying workers did not show it in the differential); the recorder now marks the target again after such a barrier (GS_EXACT.md section 9). Over the demo the first level has the same segment counts as before (9, 10 or 12 a frame in level, frame for frame over the first 1,450 frames: m4/segs_before_remark.txt against m4/lc_demo3.txt).

**Live** (m4/batch.sh at 19:04-20:17 on 2026-10-15 and one re-run at 20:18, headless, uncapped; each group HEAD, M4, M4, HEAD; binaries m4/head_extermination, HEAD 8bc6ded from git archive, and m4/m4_extermination, a copy of build/extermination, both built with the Makefile's command, 0 warnings). The quiet gate held before every kept run: idle 70.2-76.7 %, 1-minute load 3.9-15.1. Excluded: m4_a01_t3_m4_b (it started at 67.4 % idle; re-run as m4_a01_t3_m4_c at 70.9 %, used below; the excluded run's figures agree within 0.2 ms). Mean ms per tick; busiest = the busiest worker's CPU per frame.

One worker (the GS frame's CPU per frame; the direct path in both builds):

| Range | HEAD a / b | M4 a / b |
|---|---|---|
| AREA11 cinematic (1,310) | 17.57 / 17.69 | 16.92 / 17.22 |
| AREA11 calm (85) | 16.64 / 16.67 | 16.05 / 16.17 |
| AREA11 slide (121) | 16.92 (110) / 17.10 | 16.60 / 16.59 |
| AREA01 beat (781) | 19.68 / 19.94 | 18.91 / 18.88 |
| AREA01 fire (376) | 21.64 / 21.83 | 20.97 / 20.95 |
| AREA01 fire peak (149) | 25.08 / 25.28 | 24.47 / 24.40 |

The direct path's work is HEAD's (this step only regroups its code); its 2-4 % lower CPU here is not claimed as a gain of the step.

Default 6 workers. Each cell: busiest worker / tick wall / ticks over:

| Range | HEAD a, b | M4 a, b |
|---|---|---|
| AREA11 cinematic | 5.49, 5.50 / 6.32, 6.28 / 0, 0 | 4.14, 4.14 / 5.81, 5.90 / 0, 0 |
| AREA11 calm | 5.54, 5.58 / 6.39, 6.41 / 0, 0 | 3.94, 3.91 / 6.08, 6.18 / 0, 0 |
| AREA11 slide | 5.28 (118), 5.26 / 6.19, 6.18 / 0, 0 | 3.82, 3.81 / 6.04, 6.08 / 0, 0 |
| AREA01 beat | 6.01, 5.91 / 10.70, 10.36 / 1, 0 | 4.22, 4.22 / 10.08, 10.13 / 0, 0 |
| AREA01 fire | 6.16, 6.15 / 10.16, 10.20 / 0, 0 | 4.49, 4.48 / 9.91, 9.95 / 0, 0 |
| AREA01 fire peak | 6.76, 6.73 / 9.86, 9.88 / 0, 0 | 4.99, 5.00 / 9.62, 9.65 / 0, 0 |

Three workers (EM_GS_THREADS=3, the iPhone proxy). Busiest worker / tick wall / ticks over, and the game thread's CPU:

| Range | HEAD a, b | M4 a, b (AREA01: a, c) | Game thread HEAD -> M4 |
|---|---|---|---|
| AREA11 cinematic | 8.05, 8.04 / 8.43, 8.41 / 0, 0 | 6.13, 6.13 / 6.61, 6.61 / 0, 0 | 5.81 -> 6.49 |
| AREA11 calm | 7.95, 7.92 / 8.32, 8.30 / 0, 0 | 6.01, 6.03 / 6.51, 6.54 / 0, 0 | 6.05 -> 6.44 |
| AREA11 slide | 7.77, 7.75 / 8.12, 8.12 / 0, 0 | 6.07, 6.08 / 6.59, 6.59 / 0, 0 | 6.00 -> 6.57 |
| AREA01 beat | 8.79, 8.78 / 10.53, 10.52 / 0, 0 | 7.05, 7.24 / 10.32, 10.53 / 0, 1 | 10.26 -> 10.35 |
| AREA01 fire | 9.39, 9.37 / 10.61, 10.61 / 0, 0 | 7.74, 7.80 / 10.22, 10.28 / 0, 0 | 10.09 -> 10.20 |
| AREA01 fire peak | 10.60, 10.57 / 11.04, 11.03 / 0, 0 | 8.89, 8.99 / 10.12, 10.22 / 0, 0 | 9.78 -> 10.10 |

Ten workers (EM_GS_THREADS=10: every core of this Mac, more than its 8 performance cores, so some workers share cores with the game's threads or run on the 2 efficiency cores: the mixed-core straggler case). Busiest worker / tick wall:

| Range | HEAD a, b | M4 a, b |
|---|---|---|
| AREA11 cinematic | 5.54, 5.49 / 8.24, 8.21 | 3.86, 3.86 / 6.00, 6.02 |
| AREA11 calm | 5.74, 5.73 / 8.36, 8.35 | 3.71, 3.71 / 6.28, 6.36 |
| AREA11 slide | 5.28, 5.24 / 7.80, 7.83 | 3.54, 3.56 / 6.29, 6.26 |
| AREA01 beat | 5.98, 5.95 / 10.91, 10.81 | 4.05, 4.01 / 10.67, 10.70 |
| AREA01 fire | 6.05, 6.08 / 10.82, 10.70 | 4.26, 4.21 / 10.54, 10.56 |
| AREA01 fire peak | 6.41, 6.48 / 10.55, 10.58 | 4.67, 4.62 / 10.34, 10.25 |

No tick ran over in either build at 10 workers. Every range ran at full speed at 3, 6 and 10 workers in both builds (HEAD a's AREA01 beat at 6 workers: 98.4 %, one tick over).

**The schedule** (the counters above, means per frame). At 3 workers an AREA11 frame has 60.5 bands and 6.0 splits, its primitives are drawn 1.00 times each (HEAD's every worker set up every primitive that reached its interleaved rows), the workers' CPU sums to 18.0 ms against 17.0-17.2 ms for one worker, they wait 1.25 ms for a task and 0.42 ms for the parse (summed), and the game's thread parses 58,166 of the frame's ops early (about 46 % of its 126,000 register writes). At 6 workers: 121 bands, 8.9 splits, each primitive 1.27 times, 21.2 ms summed, the early parse 310 ops (the game's thread is not waiting). At 10: 196 bands, 8.8 splits, 1.55 times, 27.8 ms. In AREA01 at 3 workers the early parse takes 4,294 ops (the game thread sets the tick).

**Reading.**
- *Exactness.* Only who draws which rows, and when, changed: the parser makes the calls the direct path makes, each band draws its rows in the list's order with the same state, and the barriers order the bands as before. The evidence is the proof set, the differential against HEAD's path with its controls, and the thread sanitizer, not the label. One worker draws directly, as before.
- *The iPhone proxy* (3 workers): the tick fell 1.5-1.8 ms in AREA11 (cinematic 8.4 -> 6.6 ms, -21 %): the busiest worker 1.7-1.9 ms lower, and the parse partly moved to the game's thread, which waited for the frame anyway (its CPU +0.4-0.7 ms; the tick is now about the game thread's). In AREA01 the game thread (10.1-10.4 ms) sets the tick in both builds; the fire peak's tick fell 0.8-0.9 ms (11.0 -> 10.1-10.2 ms), where HEAD's busiest worker (10.6 ms) set it.
- *The Mac* (6 workers): the busiest worker fell 1.4-1.6 ms in AREA11 (cinematic 5.5 -> 4.1 ms) and 1.7-1.8 ms in AREA01 (fire peak 6.7 -> 5.0 ms). The tick, already the game thread's, fell 0.1-0.5 ms (cinematic 6.3 -> 5.8-5.9 ms).
- *The straggler case* (10 workers): HEAD's tick ran 2.6-2.7 ms above its busiest worker's CPU in AREA11 (cinematic 8.2 ms against 5.5 ms of work): with equal interleaved bands, the frame and each barrier waited for the worker that lost its core. With bands from a queue and splits the tick is the game thread's (6.0-6.4 ms over the runs, the ranges' means 6.0-6.3 ms: -20 to -27 %), and the busiest worker does 3.5-3.9 ms. In AREA01 the game thread sets the tick in both builds (10.3-10.9 ms).
- *What it costs.* The worker CPU summed over a frame is now about one worker's plus 0.8 ms at 3 workers (each band walks the segment's span list and sets up the primitives that reach it: the parse is done once, but a primitive across two bands is set up twice). More workers set up more primitives twice (1.27 times at 6, 1.55 at 10). The parse itself (1.6-1.8 ms) is serial; the early parse hides it where the game's thread waits.
- *Not covered:* nothing was measured on the iPhone, and the iOS build was not re-run (plain C in files already in the project, and one call in the Metal backend shared with iOS). The phone's 4 workers over 2 performance and 4 efficiency cores are the case the queue and the splits are for; the 10-worker run on this Mac is a proxy for it, not a measurement. Windows has no worker threads yet (one worker: the direct path).

### P16. The medium items together: every scene against both baselines (2026-10-16)

**What was measured.** Every scene of section 2, headless at three worker counts and windowed for AREA11, on three binaries:
- *The report's baseline,* b9fd9f9 (summary/base_extermination, P10's binary). Its default is 7 workers at default QoS, with contraction unpinned.
- *The quick wins,* 4af6bc3 (summary/now_extermination, P10's binary of ec85b7e, whose sources 4af6bc3 shares; 4af6bc3 only adds docs). Its default is 6 workers.
- *The medium items,* 8ed78b8 (medsum/now_extermination). It is built from `git archive 8ed78b8` (medsum/now_src) with the Makefile's `make all`, 0 warnings, and is byte-identical to build/extermination. Its default is 6 workers. Its GS sources have the md5s of P15's proof receipts (m4/proof/src_md5.txt; tests/gs_world_test.c that of src_md5_after_comment.txt).

This step changes no code. The tree is green at 8ed78b8 (medsum/proof/): `make all` is up to date (the archive's build gave 0 warnings); with EM_TEST_FULL=1 the short part of the GS proof set passes (swizzle, raster reference, gs_world_test, DOF pass, fog, GS memory, gs_display_test, texture reference), each log equal to P15's apart from timings, and so does the AREA01 smoke (20 live phases, its PASS line equal to P15's). The fb2 harness and the full level smoke ran on these exact sources in P15.

**Method.**
- *Scripts:* demo.sh and a01.sh, headless and uncapped. medsum/batch.sh ran them in the order B Q N N Q B for each worker count: each binary's default, 3 (EM_GS_THREADS=3, the iPhone proxy) and 1 (the GS frame's CPU per frame). medsum/rerun.sh made the re-runs.
- *Windowed:* medsum/win.sh, through present/run.sh (paced at 59.94 Hz, muted with EM_MUTE=1, 79-86 s each, the window closed at each stop). The order was Q, N, N at 3 workers, Q at 3 workers, about 6 minutes in all. The baseline was not run windowed again, because it has no EM_MUTE and the visible time was to stay short. Its windowed figures are P10's present/before runs from 2026-10-12: the report's code plus P1's pin, P2's instrumentation and EM_MUTE.
- *Analysis:* medsum/summ.py is summary/summ.py with the scene ranges chosen by run name. medsum/table.py gives the ranges over the kept runs (medsum/table.txt).
- *Ticks counted:* demo.sh stops the game with a kill, so the last lines of the timing file, which is buffered, are lost. Some slide ranges therefore hold fewer than their 121 ticks: the baseline at 3 workers 72, and one run at the default count 87 or 106.
- *Load:* before every run, medsum/gate.sh waited for three top readings in a row at 70 % idle or more, with no other process at 80 % CPU or more and no Blender process. demo.sh / a01.sh's own quiet gate then also held. medsum/bgmon.sh logged the other processes' CPU every 15 s (medsum/bg.log). Kept runs started at 70.4-85.3 % idle with a 1-minute load of 2.8-9.3, from 21:37 on 2026-10-15 to 01:08 on 2026-10-16.
- *AREA01 smoke phases:* every AREA01 run passed the smoke's 21 live phases.

**Excluded runs.** Another session's Blender renders (an EEVEE sequence at about 50-70 % CPU, and at times 5-8 cores) started and stopped all evening.
- *The first attempt was stopped* (21:24-21:35; runs/ms0_*, medsum/batch_first_attempt.log): ms0_demo_def_qw_b started at 0.0 % idle.
- *Runs a render overlapped* (medsum/overlap.py over bg.log) were re-run:
  - ms_demo_def_base_a, ms_demo_def_qw_a;
  - ms_demo_t1_base_a, ms_demo_t1_qw_a;
  - ms_a01_def_qw_b, ms_a01_def_base_b;
  - ms_a01_t1_base_a, ms_a01_t1_qw_a, ms_a01_t1_qw_b, ms_a01_t1_now_a, ms_a01_t1_now_b.

  Their one-worker figures agree with the clean re-runs within 0.3-2 %, for example the AREA01 beat's 18.70 / 18.75 against 18.85 ms.
- *ms_a01_def_base_a* showed no render, but a burst hit its AREA01 section (5.9 % idle at 22:13:57). That gave 498 of the beat's 781 ticks over and a p95 of 30.9 ms, against 381-389 and 20.8 ms in the re-runs.
- *The re-runs* are the _c runs and ms_a01_def_base_d. None of them overlapped a render.

**One worker** (EM_GS_THREADS=1, the demo to counter 4650). The first figure is the GS frame's CPU per frame (ms), the cleanest meter. In brackets are the tick's wall time (ms) and the game speed. With one worker the frame is drawn directly on the game's thread, so the tick is the game's work plus the GS frame. Clean runs only: two per binary for AREA11; for AREA01, base b, quick wins c and medium items c.

| Range | b9fd9f9 | 4af6bc3 | 8ed78b8 | Against the report |
|---|---|---|---|---|
| AREA11 cinematic (1,310) | 50.88-52.02 (59.1-60.7, 27.5-28.2 %) | 48.52-49.59 (54.1-55.5, 30.1-30.9 %) | 16.83-17.15 (22.3-22.9, 72.7-74.7 %) | 3.0x less |
| AREA11 calm (85) | 50.15-51.39 (58.2-59.9, 27.8-28.6 %) | 47.79-49.00 (53.0-54.6, 30.5-31.4 %) | 15.72-15.83 (21.2-21.3, 77.5-77.9 %) | 3.2x |
| AREA11 slide (121) | 59.02-59.45 (68.2-68.6, 24.3-24.5 %) | 54.11-55.47 (60.0-61.8, 27.0-27.8 %) | 16.15 (21.8-21.9, 76.3-76.5 %) | 3.7x |
| AREA01 beat (781) | 73.56 (87.8, 19.0 %) | 66.19 (76.6, 21.8 %) | 18.85 (29.1, 57.3 %) | 3.9x |
| AREA01 fire (376) | 79.36 (93.4, 17.9 %) | 71.14 (81.4, 20.5 %) | 20.89 (31.0, 53.8 %) | 3.8x |
| AREA01 fire peak (149) | 91.04 (104.8, 15.9 %) | 81.40 (91.4, 18.2 %) | 24.39 (34.2, 48.7 %) | 3.7x |

Section 2's AREA01 estimates (about 73 / 77 / 86 ms, pixels x 36 ns) are now measured: 73.6, 79.4 and 91.0 ms. Against the quick wins the medium items take 2.9-3.5x off the GS frame.

The GS frame's CPU divided by section 2's pixel counts (a frame's pixel count is the same in every build):
- *before:* 34.6-35.4 ns a pixel in the cinematic, 35.5 in the slide, 37.1 in the fire, 38.0 at the fire peak;
- *now:* 11.4-11.7, 9.7, 9.8 and 10.2 ns.

Section 4's target was 10-15 ns.

**Default workers** (before 7, quick wins and now 6). Each cell: busiest worker / tick wall / ticks over / speed / game thread CPU, mean ms per tick, as ranges over two clean runs.

| Range | b9fd9f9 | 4af6bc3 | 8ed78b8 |
|---|---|---|---|
| AREA11 cinematic | 10.26-10.63 / 11.38-12.31 / 8-11 / 99.9-100 % / 8.46-8.66 | 10.88-10.92 / 11.54 / 3-6 / 100 % / 5.81-5.92 | 4.09 / 5.77-5.78 / 0 / 100 % / 5.75 |
| AREA11 calm | 10.63-11.50 / 12.26-12.82 / 0 / 100 % / 8.92-9.07 | 10.88-11.15 / 11.47-12.29 / 0-1 / 100 % / 6.13-6.25 | 3.84-4.10 / 6.02-6.27 / 0 / 100 % / 5.96-6.17 |
| AREA11 slide | 10.99-11.68 / 12.08-13.46 / 2-7 / 99.8-100 % / 8.75-9.02 | 11.01-11.57 / 11.58-11.99 / 0 / 100 % / 5.88-6.16 | 3.69-3.73 / 5.94-5.95 / 0 / 100 % / 5.92-5.93 |
| AREA01 beat | 14.05-14.08 / 16.75-16.79 / 381-389 / 94.2-94.3 % / 14.18-14.19 | 13.79-13.95 / 14.49-14.78 / 110-176 / 98.0-98.9 % / 10.38-10.39 | 4.24-4.25 / 10.20-10.22 / 0 / 100 % / 10.17-10.19 |
| AREA01 fire | 14.86-14.96 / 17.28-17.44 / 205-213 / 93.0-93.5 % / 14.05 | 14.75-14.91 / 15.26-15.60 / 83-107 / 97.3-98.1 % / 10.22-10.25 | 4.51-4.52 / 10.05 / 0 / 100 % / 10.01-10.02 |
| AREA01 fire peak | 16.61-16.63 / 18.91-19.06 / 125-131 / 87.2-87.8 % / 13.59 | 16.42-16.62 / 16.94-17.36 / 73-90 / 93.8-95.6 % / 9.90-9.91 | 5.02-5.08 / 9.72-9.81 / 0 / 100 % / 9.69-9.75 |

**Three workers** (EM_GS_THREADS=3, the iPhone proxy on M1 performance cores). Same cells.

| Range | b9fd9f9 | 4af6bc3 | 8ed78b8 |
|---|---|---|---|
| AREA11 cinematic | 19.51-19.67 / 19.87-20.12 / 1,310 / 82.9-84.0 % / 8.50-8.63 | 18.62-18.79 / 18.97-19.22 / 1,191-1,234 / 86.8-87.8 % / 5.71-5.89 | 6.12-6.14 / 6.62-6.64 / 0 / 100 % / 6.50-6.53 |
| AREA11 calm | 19.48-19.59 / 19.82-20.04 / 85 / 83.3-84.2 % / 8.92-9.04 | 18.59-18.72 / 18.92-19.10 / 85 / 87.3-88.2 % / 5.92-6.11 | 6.06-6.10 / 6.53-6.58 / 0 / 100 % / 6.43-6.48 |
| AREA11 slide | 19.40-19.43 / 19.76-19.82 / all 72 / 84.2-84.4 % / 8.78-8.86 | 20.56-20.60 / 20.95-20.98 / 121 / 79.5-79.6 % / 6.04-6.05 | 6.07 / 6.56 / 0 / 100 % / 6.54-6.55 |
| AREA01 beat | 26.88-26.95 / 27.25-27.45 / 754 / 60.7-61.2 % / 14.01-14.11 | 24.39-24.40 / 24.74-24.75 / 729-730 / 67.2 % / 10.17 | 6.94 / 10.09-10.12 / 0 / 100 % / 10.08-10.10 |
| AREA01 fire | 28.87-28.96 / 29.24-29.46 / 376 / 56.6-57.1 % / 13.88-13.98 | 26.08-26.09 / 26.44 / 376 / 63.1 % / 10.02 | 7.61-7.62 / 10.01-10.02 / 0 / 100 % / 9.99-10.00 |
| AREA01 fire peak | 32.76-32.80 / 33.13-33.31 / 149 / 50.1-50.4 % / 13.45-13.52 | 29.50-29.52 / 29.86-29.88 / 149 / 55.8-55.9 % / 9.71-9.72 | 8.76 / 9.90-9.93 / 0 / 100 % / 9.88-9.89 |

The baseline's 3-worker slide holds 72 ticks (the lost timing lines above), so it is the first part of the slide only.

**Windowed, paced** (the built-in 120 Hz display; the demo to 4650; one run each). Each cell: tick wall / ticks over / speed / busiest worker. The display wait is 0.00-0.04 ms in all four new runs.

| Scene | b9fd9f9, 7 workers (P10) | 4af6bc3, 6 | 8ed78b8, 6 | b9fd9f9, 3 (P10) | 4af6bc3, 3 | 8ed78b8, 3 |
|---|---|---|---|---|---|---|
| Cinematic | 13.30 / 425 / 95.4 % / 11.98 | 5.89 / 0 / 100 % / 10.83 | 5.98 / 0 / 100 % / 4.13 | 20.67 / 1,310 / 80.7 % / 20.10 | 19.22 / 1,251 / 86.8 % / 18.77 | 5.71 / 0 / 100 % / 6.49 |
| Calm | 16.17 / 20 / 97.4 % / 11.88 | 6.09 / 0 / 100 % / 10.83 | 6.29 / 0 / 100 % / 3.98 | 20.45 / 84 / 81.4 % / 20.00 | 19.06 / 83 / 87.3 % / 18.73 | 6.01 / 0 / 100 % / 6.37 |
| Slide | 15.15 / 40 / 91.9 % / 12.38 | 6.07 / 0 / 100 % / 11.64 | 6.11 / 0 / 100 % / 3.75 | 22.60 / 107 / 73.8 % / 22.07 | 21.05 / 121 / 79.2 % / 20.60 | 6.00 / 0 / 100 % / 6.33 |

Paced, a tick's wall time is the game thread's own work whenever the workers finish the previous frame within the period (P10). At 6 workers the quick wins already had that, so the windowed tick hardly changes (5.9-6.1 -> 6.0-6.3 ms), but the busiest worker now has 12-13 ms of the period to spare instead of 5-6. At 3 workers the window goes from 79-87 % speed with nearly every tick over to full speed with none over.

**Reading.**
- *The Mac* (6 workers) now runs every measured scene at full speed, headless and windowed, with no tick over. The busiest worker is 3.7-4.1 ms in AREA11 and 4.2-5.1 ms in AREA01 (the report: 10.3-11.7 and 14.1-16.6 ms, -60 to -70 %). The tick is the game thread's: 5.8-6.3 ms in AREA11 and 9.7-10.2 ms in AREA01. Before, it was 11.4-13.5 ms in AREA11, and 16.8-19.1 ms in AREA01 at 87-94 % speed. The GS frame is no longer the limit on this Mac: the next ms of tick would have to come off the game thread (em_vu_vec_bits, P5).
- *The iPhone proxy* (3 workers on M1 performance cores) runs everything at full speed with no tick over. Before, it was 83-84 % in AREA11 and 50-61 % in AREA01; after the quick wins, 80-88 % and 56-67 %. The busiest worker is 6.1 ms in AREA11 and 6.9-8.8 ms in AREA01. The tick is the game thread's, 6.5-6.6 and 9.9-10.1 ms. In AREA11 the game thread is 0.3-0.8 ms dearer here than at 6 workers because it does the early parse (P15).
- *One worker* is now 3.0-3.9x cheaper than the report's baseline. It is still not real time anywhere, because there the frame is drawn on the game's thread: 72.7-77.9 % speed in AREA11 and 48.7-57.3 % in AREA01. It is the case on every host that gets one worker:
  - the Windows path: em_gs_world.c builds no worker threads there, and the D3D12 backend is still a skeleton;
  - a Linux (or other non-Apple) host with 4 or fewer cores, whose count is the online cores less three;
  - any run with EM_GS_THREADS=1.
- *Against the emulator.* Section 1's fork benchmark drew the same frames at 6.6 / 7.4 / 10.6 ms of one core's software GS CPU (calm / slide / fire, extra threads 0), and 8.5 / 9.2 / 13.1 ms as process CPU with its threads. The port's exact model is now at 15.7 / 16.2 / 20.9 ms on one worker: 2.0-2.4x the fork's single-thread figure, against 6-8x at the report. Split over the workers, it now leaves the Mac's tick to the game thread.
- *Section 4's estimate,* checked:

  | Section 4's estimate | Measured |
  |---|---|
  | Pixel cost 10-15 ns | 9.7-11.7 ns |
  | One core, AREA11: 15-25 ms | 15.7-17.2 ms |
  | One core, the fire: 25-35 ms | 20.9 ms; the fire peak 24.4 |
  | Mac busiest worker, AREA11: 3-5 ms | 3.7-4.1 ms at 6 workers |
  | Mac busiest worker, the fire: 5-7 ms | 4.5-5.1 ms |
  | "iPhone, 3 workers: AREA11 fits" | the proxy fits AREA11 and AREA01 |

**What L1 (NEON in the span loops) would still be needed for.** It is no longer needed on this Mac. It is estimated, not measured, at 2-4x on the loops, which draw 85-87 % of the pixels (P14). If the loops take about 85 % of the one-worker frame, which also holds the parse (1.6-1.8 ms, P15) and the generic path's classes, L1 would bring the GS frame to roughly 0.36-0.58 of today's: about 6-10 ms in AREA11 and 9-14 ms at the fire peak on one M1 performance core. That is an estimate.
- *The iPhone itself.* The phone has 2 performance and 4 efficiency cores, and its default is 4 workers.
  - *The work:* by these measurements a frame is about 18 ms of worker CPU in AREA11 (the P15 counters at 3 workers) and, estimated from the one-worker figures plus the bands' extra setup, about 20-26 ms in AREA01, both in M1 performance-core time.
  - *The capacity:* the two performance cores give 33.4 ms a period, and the game thread needs 6.5 ms of it in AREA11 and 10 ms in AREA01. That leaves about 27 and 23 ms for the workers.
  - *What follows:* AREA11 should fit on the performance cores alone. The AREA01 fire peak needs a few ms of the efficiency cores, which the queue and the splits (P15) can use. That is arithmetic on Mac figures, not a measurement.
  - *What it leaves out:* the efficiency cores' speed (public descriptions put them well below the performance cores), the OS's and the audio and Metal threads' share, and above all sustained clocks. The Air is a thin phone, and a 10-15 minute run may throttle.

  L1 is the margin for those: at 0.36-0.58 of today's work the AREA01 frame would fit in the one performance core the game thread leaves. Whether that margin is needed is the device measurement of section 7, item 2 (EM_GS_THREADS 2-5, a sustained run, the slide and the fire). It should come before L1.
- *Older iPhones.* iOS 17 reaches back to the A12 (P9), whose two performance cores are slower than the A19 Pro's. On those, L1 or the GPU renderer (a user decision, section 5) is likely needed. Not measured.
- *One-worker hosts* (the Windows path; Linux with 4 or fewer cores). On one core the tick is the game thread's work plus the GS frame:
  - *AREA11:* 21-23 ms today. L1's 6-10 ms would make it 12-16 ms: real time, with little to spare.
  - *AREA01:* 29-34 ms today. With L1 it would be about 19-24 ms: still not real time, because the game thread alone is 10 ms.

  There the cheaper fix is worker threads on Windows, or one worker drawing beside the game's thread instead of on it (the pipelining the multi-worker path has). L1 does not replace them.
- *Exactness, for whoever does L1.* AArch64's Advanced SIMD float operations follow the same FPCR rounding as the scalar ones, so a lane-wise multiply, add, divide or convert gives the scalar's bits. But a fused multiply-add is one rounding where the source has two, so the intrinsics must not use one where -ffp-contract=off keeps them apart (P1). The integer parts (the bilinear lanes, P14) are exact by construction. The proof would be the same set plus the M2 check (EM_GS_SPEC_CHECK=1) and a differential, as for each medium item.

**The iOS app builds with the medium items.** `tools/ios/build.sh clean`, then `tools/ios/build.sh device`: a full build of 334 sources for iphoneos, arm64, iOS 17, -O2 -ffp-contract=off, 0 warnings. The app was built and signed (codesign: app bundle, thin arm64, a team identifier); medsum/ios_device_full.log. Then `tools/ios/build.sh clean` again, so no app bundle with the disc's data stays on the Mac (medsum/ios_clean.log). It was not installed or run on the phone.

**Not covered.**
- Nothing was measured on the iPhone.
- AREA01 was not measured windowed: the route takes 3-15 minutes, beyond the short visible runs the user approved.
- No 60 Hz display was measured.
- The GPU renderer and the 10-worker straggler case were not re-measured (P15 has the latter).

**Receipts** (build/perf_investigation/):
- medsum/: batch.sh, rerun.sh, win.sh, gate.sh, bgmon.sh, overlap.py, summ.py, table.py and table.txt; batch.log, batch_first_attempt.log, win.log and bg.log; proof.sh and proof/; build_now.log, ios_device_full.log, ios_clean.log; now_src/ and now_extermination;
- runs/ms_*, runs/ms0_*.

### P17. The parse-once follow-up: the review's items (2026-10-16)

An adversarial review of 8ed78b8 (P15) left five items. This step answers them, and fixes two defects its differential found. No first-level frame changes its segments or its bytes (the counters and the proof set below), so nothing was measured for speed.

**What changed** (em_gs_world.c, em_gs_raster.c section 12, tests/gs_world_test.c; GS_EXACT.md section 9, "Workers and order").
- *A read after a barrier: the drawing target (item 1).* P15's recorder fix (texture_read marks the drawing target drawn again after a barrier for a read) had no deterministic test: without it gs_world_test still passed, and the P15 differential caught it in 1 of 6,000 lines (batches 0-1499, 3 and 8 workers) and 0 of 800 (batches 1500-1699). gs_world_test now draws a frame whose second read needs that barrier: the 128x128 target, buffer Y drawn from it, Y's rows 150..223 drawn, the field's rows 0..40 drawn from them. One worker drawing the list (EM_GS_LIST=1) draws its bands one after another, top first, so without the barrier the field reads Y before Y's rows are drawn, every time. Also 3 and 8 workers; three segments counted.
- *The declared field before the first FRAME_1 write (item 2).* From em_gs_world_begin until FRAME_1 or SCISSOR_1 is recorded (and after the environment again), the drawing target is the head's, which the recorder does not read. em_gs_world_field_declare marked the field drawn but left the target unknown, so a barrier for a read there dropped the field from the drawn set. Fixed: a target register not written since is the declared field's (the kick checks the declaration against the head). The live path never meets it (the only declaration is the depth-of-field pass's, whose copy writes its target's FRAME_1 and SCISSOR_1 before its first read); gs_world_test now does: the field read before any FRAME_1 write, drawn, read again.
- *The early parse's waits (item 3).* The CLUT-from-the-previous-field case also runs with the early parse forced past each frame's end (EM_GS_EARLY_OPS=100000, 3 workers): the second frame's CLUT load then waits for the first whatever the timing, and gs_world_test asserts the wait (1 counted). The default early parse's waits depend on timing and stay printed only.
- *CLUT loads from the frame's own spans (item 5).* P15 refused them with more than one worker (EM_GS_REFUSE_ORDER, fail-stop) while one worker drew them, so the worker count decided whether such a frame was drawn. They are now drawn exactly. One worker drawing directly reads a CLUT with every span that ended before the load drawn and the span still queued not. So the parse ends the segment at the load (the queued span goes into the next) and waits until every segment so far is drawn, then reads (em_gs_list_drain, own_drain). A worker's parse waits for the other workers' bands. Where no other thread draws the frame (one worker drawing the list; the early parse, once the frame before is drawn) the parsing thread draws those segments itself, every row, through worker 0's model, and the bands skip them. Such an early parse puts the frame's spans into local memory before its kick (nothing in the port reads memory there); their pixels count for their own frame. EM_GS_LIST_COUNTERS gains a column, own_clut_waits, before the last two. The first level has no such load (its counters below).
- *A read of uploaded memory before its first draw (found by the extended differential; HEAD's too).* texture_entry lists only drawn blocks, so a texture read of uploaded blocks no draw had touched left nothing in the recorder's read set; a buffer then drawn over them for the first time in the same stretch got no barrier, and the read raced the draw (batch 635 of the extended generator: five pixels of the field, varying from run to run, with 3, 6 and 8 workers). Now em_gs_world_drawn_buffer places a barrier when it marks uploaded blocks drawn for the first time after a texture read since the last barrier (the declared field is exempt: it is the head's target, drawn from the frame's start). The first level draws over no upload (GS_EXACT.md section 9), so its frames keep their segments (below). gs_world_test draws the case: the field drawn from an uploaded buffer, then that buffer drawn. A barrier must fall where the span has ended, so em_gs_world_prims now marks the buffer after the environment's writes rather than between FRAME_1 and the rest: a list primitive that sets the same FRAME_1 with taller rows changes SCISSOR_1 alone, and the first version of this barrier fell before that write, inside the span (the fail-stop fault, 31 of M4's generator's frames, also with one worker). Barriers change nothing drawn, and in the first level no span was ever open at a mark (none faulted), so its segments stay the same.
- *A crash found on the way (HEAD's too).* An early parse that stops inside a span leaves the span in the list's storage; destroying the world after a fault then flushed it through the drawing path and crashed (HEAD at 3 and 8 workers, after the extended differential's refused loads). The world now drops that span with its list.
- *Docs (item 4).* GS_EXACT.md 10.2 and section 4's M4 line now say what P15 says: fifteen negative controls caught, fourteen by the differential and the fifteenth by gs_world_test's stamp case. Section 5's 10-worker AREA11 tick now reads 6.0-6.4 ms, every run of P15's table, as section 1, GS_EXACT.md 10.2 and FIDELITY_FEATURES.md give it (P15's reading gives the ranges' means, 6.0-6.3 ms, and says so now).

**Same bytes** (receipts in build/perf_investigation/m4fu/).
- *The GS proof set* (EM_TEST_FULL=1 on these sources, twice: over 6ac573c, proof_base6ac573c/, run beside the differential, and over 72fee14, proof/, the same results; src_md5.txt; times not comparable):
  - `make test-gs-swizzle`, the raster reference, the DOF pass, fog, GS memory, gs_display_test and the texture reference give P15's logs line for line, timings and scratch paths apart.
  - gs_world_test passes with P15's figures and its new parts (with EM_TEST_FULL=1 the barrier cases and the CLUT loads also at 8 workers, 8 bands a worker and the early parse off). Over 72fee14 the rebase first dropped a closing brace of the test; after the fix it passes with the same line plus 72fee14's head probe (test-gs-world-after-brace.log, src_md5_after_brace.txt).
  - The fb2 harness gives P15's exact-pixel lines (the level exit's presented frame 113,053 again).
  - The AREA01 smoke's PASS line equals P15's.
  - The full level smoke gives P15's 404 PASS lines, no failure (over 72fee14 also that commit's own "present again" line).
- *The first level's frames* (proof/summary.txt; lc_demo3.txt, lc_a013.txt, 3 workers): the segments per frame equal P15's frame for frame, over the demo's 4,061 frames both runs recorded and the AREA01 route's 14,112, and no frame has a CLUT load from its own spans. So the new barrier rules and the drain never act there.
- *A random differential* (sweep/: P15's wsweep.c, extended; build.sh, run.sh, all.sh). The reference is HEAD 6ac573c with one worker, drawing directly. Two generators, 1,500 batches (6,000 frames) each: P15's, unchanged, and an extended one (-DOWN) that also loads T8 / T4 CLUTs from buffers the frame itself drew (and draws over them after the load) and holds a read-after-barrier sequence (the 256x256 target drawn, the 128x128 drawn from it, drawn again, then read into the field).
  - *Every line equal with both generators:* the follow-up at 1 (direct), 3, 8 and the host default (6) workers, one worker drawing the list, 1 and 8 bands a worker, the early parse handing over after 37 ops (3, 8), each frame parsed whole early (3 workers, and one worker drawing the list; with and without em_gs_world_preparse), the early parse off, and without em_gs_world_preparse. (The extended generator omits a frame's memory hash when the next frame was preparsed: an early parse that drains puts that frame's spans into memory before the hash. The field, the counts and the next frame's hash are compared.)
  - *Thread sanitizer* (8 workers, 400 batches of the extended generator, the early parse by default and whole): no warning, every line equal.
  - *What the extended generator exercised* (EM_GS_LIST_COUNTERS summed, 8 workers): 4,494 frames with barriers, 27,531 segments, 388,763 bands, 37,923 splits; 4,609 CLUT loads from the frame's own spans in 2,098 frames. Parsing each frame whole early (3 workers): the early parse in 5,845 frames, 2,861 waits for the frame before.
  - *HEAD at 3 and 8 workers* refuses the first such frame of 12 batches (EM_GS_REFUSE_ORDER), then crashes destroying the world after one (the crash above), so its lines stop there.
- *Negative controls* (neg_test/, sweep/neg/): twelve plausible mistakes, each in a copy of the sources. gs_world_test fails on every one:
  - the target not marked again after a read barrier (8ed78b8 before its fix);
  - the declared field not taken as the target (8ed78b8), or only its FRAME_1 half;
  - the early parse not waiting for the frame before;
  - the early parse's pixels counted for the frame before;
  - a CLUT load from the frame's own spans refused (8ed78b8);
  - the parsing worker not waiting for the other workers' bands;
  - the early parse drawing without waiting for the frame before (the thread sanitizer's race);
  - the drained segments drawn again by the bands;
  - the drain drawing only its last segment;
  - the spans' pages not counted as undrawn;
  - no barrier at the first draw over uploaded memory.

  The differential catches nine of them (lines of 1,200 that differ in batches 0-299, at 3 workers / 8 workers / one worker drawing the list / 3 workers parsing whole): the target not marked again 75 / 87 / 21 / 50 (P15's differential: 1 of 6,000); the refused loads 750 in each; no wait for the other workers 22 / 16 / 0 / 0; no wait for the frame before 158 / 144 / 0 / 176; drawn again 102 / 91 / 654 / 654; the last segment only 65 / 81 / 595 / 595; the undrawn pages 23 in each; the early parse's CLUT wait 5 / 2 / 0 / 15; the early pixels 73 / 82 / 0 / 424. It misses the declared field's two (its generator never reads the field before the first FRAME_1 write) and, in these 300 batches, the upload barrier (batch 635 needs it).

**Not covered.** No speed measurement (see above); the iPhone was not run; the build without worker threads (Windows) was only syntax-checked; item 2's case does not occur in the random differential (only gs_world_test draws it).

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
- **MISSED, bit-exact: test Z before the texture sample in plot()** — plot() samples the texture (bilinear = 4 x fetch_rgba, together 44 % of worker time) and applies TFX and fog before the frame read, DATE and the Z test. Every failure path (Z never or fail, DATE mismatch, alpha fail with afail 0) returns without writing, and sampling has no side effects. Testing Z first when zte && ztst >= 2 therefore produces the same bytes and skips the fetch for occluded pixels. The alpha test must stay ahead of the Z write, because afail 1 / 3 change write_z (Unknown until a Z-fail counter is added. It could be large for the depth-tested fire and snow sprites behind geometry; nothing for the DOF pass if it does not test Z. Measured and done 2026-10-12, Progress P12: the counter (EM_GS_Z_COUNTERS) found 30-45 % of the samples Z-rejected; the reorder, with DATE moved too and the triangle's attributes deferred, draws the same bytes and avoids 32-49 % of the samples. The DOF pass does test Z: offline it is 30.8 % cheaper)
- **MISSED: equal 4-row interleaved bands on mixed core types put the slowest core on the critical path; Q5's count alone is not enough on the iPhone** — em_gs_world.c worker_count = online - 3 (A19 Pro: 6 - 3 = 3); own_row deals rows equally. With 2 performance + 4 efficiency cores, the frame and each of the 8 DOF barriers wait for an efficiency-core worker. Row ownership is a pure partition, so giving performance-core workers more rows, or using more workers on the 4 efficiency cores, draws the same bytes. gs_world_test.c (N workers == 1) is the proof and needs uneven-partition cases (Phone not measured. On M1 performance cores, 3 workers give 50-83 % speed, the clearest measured cause of 'really slow' on the phone)
- **MISSED, exactness hazard for every perf refactor and for Q6: Apple clang already fuses multiply-adds in the GS model** — The Makefile's CFLAGS are -O2 with no -ffp-contract (only some test targets pin it off). Apple clang 21 at -O2 contracts a + (b - a) * t into an FMA, and em_gs_raster.c compiled that way contains 14 fused multiply-add / subtract instructions (count only). These are double expressions: the STQ sprite interpolation, the barycentric weights, the zrow + (x - xd) * zstep Z. test_gs_raster_reference.py builds with cc -O2, so it proves the fused arm64 code. A baseline x86-64 Windows / Linux build will not fuse. Stepping Z incrementally or regrouping these expressions changes bits (Pin -ffp-contract explicitly, and decide which form is reference, before any raster refactor or flag change; possible cross-platform difference today. Resolved 2026-10-11: pinned off in every build, Progress P1)
- **CONFIRMED with caveats: the fork is about 6-8x cheaper per frame in software GS CPU, and 'fill-bound' and the hotspot ranking stand** — CPU_SUMMARY: software minus Null process CPU 8.5 / 9.2 / 13.1 ms (calm / slide / fire); with extra threads 0: 6.6 / 7.4 / 10.6 ms, so the multithreaded figure includes spin-waits. Port at 1 worker: 52.1 / 59.8 ms. The plot() code matches the claims: it is generic, switches on state per pixel, recomputes addresses for frame and Z through the exported em_gs_addr32, and does per-pixel 128-bit floordiv (UV triangles) and double divides plus stq_div (STQ). No lock contention seen: barriers are mutex / cond, 8 plus 1 per frame (The 6x is if anything understated (against the fork's x0 run: 7.9x calm))
- **FINAL VERDICT on the top three (Q1 digest gate, Q2 VU host floats, Q3 per-column U)** — All three can be made bit-exact (Q2 only with the bracketing above), but none addresses either measured cause of the user's complaint. The slide on the Mac is GS-bound and runs at 99 % speed headless, so the cause is elsewhere. The phone's slowness is the worker count and partition. Q1 and Q2 help only where the main thread gates the tick (AREA01, the phone); Q3 is about 0.25 ms. Suggested order: (0) measure the windowed Mac path and the device, with the user's OK; (1) a performance-core-aware worker count and a core-weighted row partition (measured: 6 > 7 under load; 3 workers = 50-83 %); (2) the early Z test plus texture-fetch specialization (fetch / sample 44 %, plot 28 %), after a Z-fail counter and with -ffp-contract pinned; (3) then Q1 / Q2 for AREA01's main thread (Re-ranking needed. The quick wins as ranked give an estimated 0-0.3 ms on the Mac slide tick (only Q3's 0.25 ms is measured, offline))
