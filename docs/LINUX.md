# Linux platform proof

The headless Original-profile port on Linux: a CPU GS backend
(`src/gfx/offscreen/em_gfx_offscreen.c`, `HEADLESS=1`), a display-free
platform layer, a mixer null sink and a decoded movie cache. A visible Linux
runtime (X11 + Vulkan) and Windows are later phases, not implemented.

**History.** Written on the `platforms` branch (`673e90a`, based on
`2f9cf6c`, 2026-10-18) and re-applied onto main at `028af35` (2026-10-29).
The measurements in "Local matrix and receipts" below were taken at
`2f9cf6c` in the `platforms` worktree, which has since been removed, and
its ignored `build/platforms/` receipts with it: they are a record of that
checkpoint, not current evidence, and have to be re-run on main.

**Status at `2f9cf6c` (2026-10-18):** all four Linux compiler/architecture
combinations reproduced the macOS baseline in the default CPU and gameplay
checks, and the full main route matched on both Linux architectures. That
was a portability checkpoint, not certification that every GS case is
exact: the macOS baseline had 203 documented OPEN raster cases. The fully
bit-exact phase-1 gate remains open; phases 2 and 3 have not begun.

**Status on main (2026-10-29, the re-application):**

- The CPU backend follows main's GS-frame contract since `2f9cf6c`: the
  headless raster skip (PERFORMANCE.md P18, `em_gfx_gs_world_raster`; the
  model is created with `em_gs_world_create_raster`, and a picture read or
  capture under the skip fail-stops, as in the Metal backend) and the status
  pages' GIF frames (`em_gfx_gs_gif_frame`, the Metal backend's checks). A
  headless Linux run therefore takes the raster skip by default, like a
  headless macOS run (`EM_HEADLESS_RASTER=1` draws).
- macOS: the executable built from the re-applied tree is byte-identical to
  main's (SHA-256 `09aed609...e8e5bb`); newgame-control PASS 9.599849,
  `test-level-smoke`, `test-start-at`, `test-cutscene-skip` (5/5),
  `test-gs-world`, `test_vu_host_lanes.py` and
  `test_gs_memory_reference.py` pass.
- Linux arm64 (Debian 13 container `exterm-platforms-arm64`, built from
  `tools/platforms/Containerfile`): `make HEADLESS=1` builds with Clang 19
  with **zero warnings** and with GCC 14 with 154 warnings, all in shared
  game code the platform work does not touch (101 `-Wunused-value`, 22
  `-Wmisleading-indentation`, 12 ignored `FP_CONTRACT` pragmas, the rest
  truncation, fallthrough, possible-uninitialized and overflow; see
  "Requests for main"). The runtime was not re-run: no decoded movie cache
  exists on this machine, and the build fail-stops on the missing
  `intro.mov.emmv` as designed. amd64 was not re-checked.

## Build and run

On native arm64 or x86-64 Linux (Debian 13), install the system compiler and
proof tools:

```sh
sudo apt-get install gcc clang libclang-rt-dev make libc6-dev python3 python3-numpy python3-pil binutils
make -j4 HEADLESS=1 CC=clang
EM_HEADLESS=1 EM_UNCAPPED=1 EM_STARTUP_TEST=newgame-control build/extermination
EM_HEADLESS=1 make HEADLESS=1 CC=clang test-level-smoke
```

Use `CC=gcc` for GCC. Use `make -B` when changing a build's compiler or flags,
or give each build a distinct `BIN` as the proof runner does. Per-compiler
object directories prevent mixing GCC and Clang objects. Linux always appends `-ffp-contract=off`; x86-64 also appends
`-msse2 -mfpmath=sse`. Do not pass fast-math flags. The default non-headless
backend selection remains Vulkan; its runtime is still unavailable.
`HEADLESS=1` selects the CPU backend at build time; `EM_HEADLESS=1` selects
display-free operation at run time. No display server or GPU is required.

Export private assets from your own disc using the commands in
[STARTUP.md](STARTUP.md), including the AREA01 exports and script walk clips
needed by the departure movie. Keep the sibling `Extermination` checkout
and its existing local reference captures for oracle tests. Neither exports
nor captures are part of this branch or its container images. A missing
export is an error, not a test exemption.

### Decoded movie cache

The headless Linux port reads `intro.mov.emmv` and `e001.mov.emmv` beside
the `.mov` files made by `tools/export_movie.py`. The format is EMMV v1:
a 32-byte little-endian header (`EMMV`, version, picture count, width,
height, zero reserved word, uint64 PCM frame count), then each picture's
binary64 presentation timestamp and tightly packed top-down RGBA8 pixels,
then stereo PCM16 at 48000 Hz. The reader rejects wrong lengths, headers,
changing order and truncated streams. Its video and audio cursors are
independent. It never schedules, resamples, drops or duplicates pictures.
The unchanged game movie code owns field pacing.

To make an Apple-decoder-identical cache **locally on arm64 macOS**:

```sh
clang -O2 -Isrc tools/platforms/export_movie_cache.c src/platform/mac/em_movie_mac.m \
  -framework Foundation -framework AVFoundation -framework CoreMedia -framework CoreVideo \
  -o build/export_movie_cache
build/export_movie_cache assets/startup/intro.mov assets/startup/intro.mov.emmv
build/export_movie_cache assets/movies/e001.mov assets/movies/e001.mov.emmv
python3 tools/platforms/test_movie_cache.py assets/movies/e001.mov --native \
  --out build/platforms/movie-verify
```

Use fresh output paths. These decoded caches total roughly 8 GB and are
disc-derived: keep them in private ignored storage on the same machine.
The tested departure cache contains 2,305 pictures and 3,832,064 stereo PCM
frames. Every picture byte, timestamp and PCM byte read through the Linux
cache API matched the native Apple API; receipt:
`build/platforms/movie-api/results.json`.

On **native Linux**, after exporting the same `.mov` from your own disc:

```sh
sudo apt-get install ffmpeg
python3 tools/platforms/export_movie_cache.py assets/startup/intro.mov
python3 tools/platforms/export_movie_cache.py assets/movies/e001.mov
```

This invokes the installed FFmpeg tools only at export time. It stream-copies
PCM and preserves every picture and rational timestamp. The departure
movie's timestamp sequence and PCM bytes matched the Apple cache exactly
(`build/platforms/ffmpeg-cache/comparison.json`). **FFmpeg's video pixels
have not been proven identical to Apple's decoder.** A Linux-exported cache
is sufficient for the gameplay/timing proofs, not a claim of movie image
parity. No FFmpeg code or library is copied into or linked with the port.

## One-script proof on real x86-64 Linux

After local exports and reference inputs are present, run from this checkout:

```sh
python3 tools/platforms/prove.py --compiler gcc --runtime \
  --out build/platforms/physical-gcc
python3 tools/platforms/prove.py --compiler clang --runtime \
  --out build/platforms/physical-clang
```

Use a new output folder for each run. `--baseline path/to/mac/results.json`
requires the same source digest, entire `ticks.jsonl` bytes, random
counter/state stream and New Game result. It fails on any difference.
`--full` runs exhaustive CPU cases and the main route through AREA01;
`--ps2-drive` enables the existing optional disc timing for that route.
The script uses ordinary system compilers and local wrappers; it does not
change compiler alternatives. It fails on missing oracle inputs and does
not weaken existing test assertions.

Only send back `results.json`. It contains platform/compiler metadata,
source and executable hashes, test exit statuses, durations and trace hashes.
Raw `.log`, `.trace`, `.jsonl`, assets, caches and images remain local.
The source digest is checked again at completion, so a concurrent source
edit fails the proof. A passing report describes its checks; it does not
claim all platform work is complete (`phase1_complete` remains false).
Physical x86-64 results are still outstanding; Rosetta results cannot prove
the behavior of a physical machine's MXCSR.

## Local matrix and receipts (2026-10-18, at `2f9cf6c`)

All receipts below were ignored files under the removed `platforms`
worktree's `build/platforms/`; they no longer exist. Tests were run
headlessly. Own images `exterm-platforms-arm64:latest` and
`exterm-platforms-amd64:latest` were built from Debian 13 using
`tools/platforms/Containerfile`. The existing `exterm-permuter` image was
not used or changed. The amd64 image ran with Apple's `container` Rosetta
mode. The worktree and decomp mounts were read-only; only each architecture's
private `build` mount was writable. No asset entered an image layer.

Reproduce on **arm64 macOS**, from this worktree (amd64 runs under Rosetta):

```sh
container build --arch arm64 -t exterm-platforms-arm64:latest \
  -f tools/platforms/Containerfile tools/platforms
container build --arch amd64 -t exterm-platforms-amd64:latest \
  -f tools/platforms/Containerfile tools/platforms
mkdir -p build/platforms/container-arm64 build/platforms/container-amd64
container run --rm --name exterm-platforms-proof-arm64 --cpus 4 --memory 4G \
  --mount "type=bind,source=$PWD,target=/work/port,readonly" \
  --mount "type=bind,source=$PWD/build/platforms/container-arm64,target=/work/port/build" \
  --mount "type=bind,source=$PWD/../Extermination,target=/work/Extermination,readonly" \
  exterm-platforms-arm64:latest \
  python3 tools/platforms/prove.py --compiler clang --runtime --out build/proof-clang
container run --rm --arch amd64 --rosetta --name exterm-platforms-proof-amd64 --cpus 4 --memory 4G \
  --mount "type=bind,source=$PWD,target=/work/port,readonly" \
  --mount "type=bind,source=$PWD/build/platforms/container-amd64,target=/work/port/build" \
  --mount "type=bind,source=$PWD/../Extermination,target=/work/Extermination,readonly" \
  exterm-platforms-amd64:latest \
  python3 tools/platforms/prove.py --compiler clang --runtime --out build/proof-clang
```

Repeat each run with `--compiler gcc --out build/proof-gcc`. Never run two
proof runners concurrently against the same writable build mount: existing
fixtures have fixed scratch names. The small build context is deliberately
`tools/platforms`, not the worktree containing private assets.

Final reports use a single frozen source digest. Before-commit Git HEAD is
the base commit; the digest covers the actual Makefile, source, tests and
tools (including uncommitted platform additions), and each final report
checks that digest again when it finishes. Documentation is outside that
digest. `matrix-comparison.json` records the direct cross-platform checks.

Source digest:
`c40e90e0a4d552f1bb00ff07453998776dc0af7bed4086b48b1acb6caf1199b5`.

| Host | Compiler | CPU set, New Game, default smoke | Exact baseline ticks and random counter/state | Final report |
| --- | --- | --- | --- | --- |
| arm64 macOS | Apple Clang 21.0.0 | PASS, including Metal fog and present-again | Baseline | `mac-release/results.json` |
| arm64 Debian 13 | Clang 19.1.7 | PASS | PASS | `arm64/release-clang/results.json` |
| arm64 Debian 13 | GCC 14.2.0 | PASS | PASS | `arm64/release-gcc/results.json` |
| amd64 Debian 13 under Rosetta | Clang 19.1.7 | PASS | PASS | `amd64/release-clang/results.json` |
| amd64 Debian 13 under Rosetta | GCC 14.2.0 | PASS | PASS | `amd64/release-gcc/results.json` |

Established measurements:

- New Game: displacement **9.599849**, **1301 locked ticks** on macOS,
  Linux arm64 GCC/Clang and Linux x86-64 GCC/Clang.
- Default smoke: all three phases pass the existing checker. Tick file
  is 65,543,433 bytes. SHA256:
  `3d3c2bdd31b0576927507dedc62f40f8a739351610bf46bed577fd464d08b116`.
- Random counter/state SHA256:
  `816a20c83ec61ce6e6644ed7b635fc6205f7dd1e9d8a398a7d26e0248fd7228c`.
  Only native return-address columns are excluded; counters and states are
  compared in order, with no tick alignment or rounding.
- Full main route through AREA01 arrival: macOS and both Linux Clang builds reach all
  20 phases and pass the original checker. Entire tick files are
  419,066,905 bytes and byte-identical, SHA256
  `2866e0def000ebe612b999c15535688f05f93a299663a148f6ad2001f6ce9425`.
  Random counter/state SHA256
  `2d26af22847b8972a26d1d3019bce16619c352d94ca7fe8a012cf83907da83fe`.
  Receipts: `mac-full2/{run,check}.log`, `arm64/full/{run,check}.log`,
  `amd64/full/{run,check}.log`, `route-comparison.json` and each directory's
  original traces. The macOS full and PS2-drive runs also pass the real
  retained-image check (`present-check.log` in each directory).
- PS2-drive main route through AREA01 arrival: macOS and arm64 Clang pass
  all 20 phases; entire ticks and random counter/state streams are identical.
  Receipts: `mac-drive/`, `arm64/drive/`, `route-comparison.json`.
  Tick SHA256 `d2cb9bede617ddd01b1c6e25d45d900c8ac4c99a7a0c9ee5510c348ed14c1188`;
  random counter/state SHA256
  `85a9c2df2e818dcd8eb0e93bea56903f0c434a2273b7c7306c98a091b7b18ebc`.
- AREA01 through `a01_02`: macOS and arm64 Clang pass the existing recorded
  input route and its checker, with byte-identical entire ticks and random
  counter/state streams. Receipts: `mac-area01/`, `arm64/area01/`.
  Tick SHA256 `4f2dfca62fc141df16917562f9c0630da0f37660738f2a354a8cddf8abd085dc`;
  random counter/state SHA256
  `212b36898069badb0322b7723a7d5fb5ee38907237bd64321cab974b705ce622`.
- The actual Linux `make HEADLESS=1 CC=clang test-level-smoke` target passed:
  `arm64/make-level-smoke.log`.

### CPU GS and floating-point scope

The proof runner exercises the existing VU host-lane test twice (normal
FPCR/MXCSR path and forced integer fallback), the EE integer model, raster,
DOF, fog, memory, texture, ASan/UBSan swizzle and the GS worker fixture.
The worker fixture itself tests 1/2/3/8 workers, bands and barriers.
The full-route tests above exercise the main route, not the entire
`test-level-smoke-full` side-suite target. Extra PS2-drive and `a01_02`
comparisons were run on arm64; the amd64 extras beyond the main route and
the exhaustive CPU mode remain unmeasured.

The lane sweep checks 587,909 operations with zero mismatches in either
path. The host path's out-of-environment probe must differ, proving the
rounding environment actually matters. The integer fallback does not claim
to exercise MXCSR. The EE test additionally checks the recorded bits/float
cases, blocks, refusal rules and seeded integer arithmetic sweep.

Raster results reproduce the macOS baseline: **703/906 bit-exact, zero
unexpected failures**. The other 203 cases are the pre-existing, precisely
counted OPEN cases in [GS_EXACT.md section 8](GS_EXACT.md#8-open-items-exact-counts-2026-09-27).
This is not 906/906 exactness. Fog's blend cases are 8192/8192 exact;
its Gouraud interpolation retains the baseline 342,349/342,831 exact
channel values. DOF's three default captures have exact fields, copies and
untouched Z. Swizzle checks 140,020,668 addresses under ASan/UBSan. No OPEN
count, tolerance, expected value or assertion was changed by this port.

### macOS-only presentation tests

| Check | Why it remains macOS-only in phase 1 |
| --- | --- |
| `check_present_again.py`, `EM_PRESENT_LOG` | Reads Metal's retained presented frame; a null presenter has no such image. |
| `test_gs_fog_conformance.py` part B | Compiles and reads the actual Metal shader. All CPU portions still run on Linux. |
| `test_fb2_pixels.py` presented BMP comparisons | Samples the Metal compositor's scaled image and overlays; CPU fields alone are not that image. |
| Metal sections of `test_shadow_original_reference.py` | Exercise GPU silhouettes, receivers and presented captures, beyond the requested CPU proof set. |

The null backend refuses presented-image capture requests instead of
fabricating passing logs or images. Its overlays, movie display and legacy
GPU meshes have no displayed output. CPU GS object/shadow/page adapters run
the existing algorithms and preserve their faults, uploads and ordered
worker barriers. This proves game and CPU behavior, not Vulkan presentation.

## macOS invariant and portability changes

The native macOS executable before and after the shared-source guards has
SHA256 `42ae803db23b4cff34e623cb7d4d303843aa7684a9ae6e03e51b237202063939`.
Receipts: `mac-before.sha256`, `mac-padding.sha256`, and their build logs.
The isolated staged-index build also has this exact hash and zero warnings:
`index-build.sha256`, `index-build.log`.

Linux GCC requires out-of-line `noinline` header functions, an explicit
`__int128` extension under pedantic warnings, and bounded diagnostic output.
Linux GCC also copied indeterminate padding after `EmShadowOriginalReceiver.cls`
into a zeroed traced plan. Copying its members preserves the zero padding;
every actual shadow value was already identical. That fixed 15 differing
default-smoke rows without rewriting the logs. These guards preserve the
original macOS compilation path. The VU fixture explicitly rejects empty
input on GCC before its one-element environment probe. ELF image-base
reading and the known `rand_value` forward let the unchanged random-order
assertions resolve GCC/Clang Linux stacks.

## Requests for main

GCC's complete application build reports existing warnings in shared game
code and protected level bindings. The full diagnostics are retained at
`arm64/final-gcc/build.log`; Clang Linux and macOS compile without warnings.
Warnings have not been suppressed. They include ignored `FP_CONTRACT`
pragmas (the Makefile pins contraction off), unused callback return values,
misleading indentation, fallthrough, bounded-string truncation, and possible
uninitialized/overflow diagnostics. The latter require investigation, not
automatic zero initialization that might hide a missing original worker.

One exact, behavior-preserving formatting request in the protected AREA01
binding is recorded below. Other warning sites remain listed in the build
receipt and require the lead's review; this patch is not represented as a
complete warning cleanup. The protected file is unchanged on this branch.

```diff
--- a/src/game/em_area01_indicator_live.c
+++ b/src/game/em_area01_indicator_live.c
@@ -15,7 +15,8 @@
 static uint8_t *mem(Frame *f,uint32_t a,uint32_t n,int write)
 {
     uint8_t *p=f->fault ? NULL : f->host->bytes(f->host->ctx,a,n,write);
-    if(!p)fail(f,a);return p;
+    if(!p)fail(f,a);
+    return p;
 }
 static int load(Frame *f)
 {
```

The existing GS OPEN items also remain main's fidelity work. Changing their
raster behavior would change the required byte-identical macOS executable;
no speculative raster fix or relaxation is proposed here. Phase 2 needs
the Vulkan presenter, X11 window/input, native-rate audio and evdev, with
presented-image comparison. Phase 3 needs Windows workers, build, D3D12,
WASAPI and controller/movie validation. Neither is certified by these CPU
receipts.

## Dependencies and licences

The runtime headless port uses system libc/libm, pthreads and libdl
([glibc, GNU LGPL](https://www.gnu.org/software/libc/), with the installed
package's applicable exceptions); no third-party source is
copied. GCC/binutils/GNU make are system build tools (GPL with the GCC
runtime exception where applicable), Clang/LLVM is Apache-2.0 with LLVM
exceptions, Python is PSF, NumPy BSD-3-Clause and
[Pillow MIT-CMU](https://pillow.readthedocs.io/en/stable/about.html). Apple
frameworks are system frameworks on the reference Mac only.

FFmpeg is an optional, separate export-time command, normally
[LGPL-2.1-or-later, or GPL with optional components](https://ffmpeg.org/legal.html).
Its installed build determines its licence; no FFmpeg binary is distributed
here. The exporter uses documented [streamcopy and timestamp-preserving
output](https://ffmpeg.org/ffmpeg.html). No emulator source, PS2Recomp source,
or third-party implementation was opened or copied for this work.
