# SPU2 output: public DSP model and comparison boundary

Written 2026-10-20/21 in a separate review lane and squash-merged into main
as `6c04534` (2026-10-28); that lane's branch and worktree are retired, and
nothing of its history is on main. Part B (the later-route audit) is in
[`LEVEL_ORACLE_AUDIT.md`](LEVEL_ORACLE_AUDIT.md). Native builds and tests ran
on macOS arm64 only.

**Receipts.** The receipts this document names (`build/spu2-output/`,
`build/spu2-a2/`, `build/level-oracle-audit/`) were ignored files in that
lane's worktree and were removed with it. The numbers below are a record of
those runs, not evidence that can be re-read; re-run the tools (each one's
`--help` names its private inputs) before relying on them. The fast tests
(`make test-spu2-output`, `make test-spu2-a2-tools`) run on main.

**Part A is not complete or waveform certified.** Original output retains the
existing renderer. This change adds independently written DSP stages, an
experimental SFX voice bus, an original-effects exporter, and strict measurement
tools. It does not claim that the speakers reproduce the fork's SPU2 output.
In particular, reverb and core master volumes are not enabled in Original.

The remaining integration crosses the user-assigned ownership boundary:
`em_iop_stream.*` owns stream samples and the applied `0x16` return-volume
commands; `em_sfx_bank.*` owns the legacy ENVX feedback; `em_bgm.c` sums the
independent producers. The existing stream tap exposes decoded samples, but a
hook in the earlier SFX render pass cannot replace that later independent mix
with a shared effect history and core-routing stage. Applying an effect to the
final host mix would not implement those stages. The required main work is
listed below. Part B's later-route audit is recorded in
[`LEVEL_ORACLE_AUDIT.md`](LEVEL_ORACLE_AUDIT.md), including the protected player
defects and the four missing C definitions retained as source blockers at the
user's direction. Part A2 below uses new tools and private copies only.

## Sources and clean-room boundary

Only public hardware descriptions informed the DSP:

- [PSX-SPX SPU documentation](https://psx-spx.consoledev.net/soundprocessingunitspu/):
  Gaussian coefficients, volume/envelope operation, reverb equations and FIR
  coefficients.
- [ps2tek](https://psi-rockin.github.io/ps2tek/index.html): SPU2 memory, register
  organization and clock distinction.
- [PS2SDK register reference](https://ps2dev.github.io/ps2sdk/spu2regs_8h.html):
  SPU2 register names and offsets, not an emulator implementation.

**Leaked, confidential or NDA material is not a permitted source** (user
rule): Sony SDK manuals, overviews or headers, including copies posted in
third-party repositories, must not be read or cited, and nothing may be
derived from them. Only genuinely public documentation (the three above), the
decomp's own C, original captures and black-box measurements of the fork
are permitted.

An intermediate revision in the review lane (never on main) consulted a
non-permitted source for the core stage. Everything derived from it was
removed before the merge (2026-10-20), not re-derived, and is deliberately
not described here: a core-stage function and its unit test, the stage
bridge's core and routing stages, a routing tool and the compare and measure
tools' core inputs, a synthetic core-chain test, the register decodes in
`tools/spu2_stage_state.py` (its words are now raw, by PS2SDK name, schema
`spu2-normalized-initial-state-v2`), and the two stage results that code
produced. No permitted public source documents the SPU2 core stage, and no
isolating black-box measurement exists yet (the fork's tap gives only the
post-master mix), so nothing in this document or in the code describes how
the cores mix, route or apply their master volumes.

What remains uses only the public sources: the voice stages (Gaussian, ADSR,
voice volume, from PSX-SPX), the per-core dry/wet voice sums (24 voices per
core from the PS2SDK register layout; EON from PSX-SPX's reverb mode), the
reverb equations with a reverb-buffer write enable (PSX-SPX's SPUCNT bit 7, the PS1 reverb master
enable; nothing here maps it to an SPU2 register) and the signed effect-return (EVOL)
output gain, and register names/offsets from PS2SDK. The PS1-to-SPU2 correspondence of the PSX-SPX parts is unmeasured
(below). The core stage, master volumes and core routing need a permitted
source, normally a black-box measurement against the fork's SPU2 tap, before
any of them is implemented again.

No PCSX2, ARMSX2 or `pcsx2-fork` implementation source was opened. The fork is
used through the permitted decomp session/video-comparison interfaces as a
black-box reference. No external code or library was added.

The original game configuration is read locally by the existing instruction
oracle from the user's driver and captured resident LIBSD. It is not embedded
in source. No original executable, register image, coefficients from the game,
PCM, audio recording or other disc data belongs in the commit.

## What is implemented

`em_spu2_dsp.{c,h}` implements the public 512-entry Gaussian table, four-product
interpolation, signed gains, ADSR, and fixed/sweep volume registers. The table's
little-endian FNV32 is `637bada7`. Negative products round downward. The current
public envelope description distinguishes exponential attack shift ranges and
all-ones rate freezes. These differ from the existing model.

`em_spu2_reverb.{c,h}` provides a bounded caller-owned effect area, reflection,
comb and all-pass stages, signed effect-return gains, and a 39-tap half-rate
filter. It runs at 48 kHz with alternating half-rate channel calculations.
The caller supplies every game parameter and memory region.

These are implementations of the documented equations. PS1-to-SPU2
correspondence, internal rounding/saturation order, filter phase, sweep edge
cases and key-on startup delay require measurements. Synthetic tests do not
establish any of those as fork-exact.

`em_spu2_output.{c,h}` separates the 48 voices into two cores' dry and wet buses.
It has no core stage: effect return, core clipping, master volumes and routing
between the cores are not implemented (see the clean-room boundary above).
Noise is refused by the experimental path. Samples still come from the existing registry,
including its predecoded repeat body; repeated ADPCM history is not newly
proved here.

The only runtime change in `em_sfx.c` is an include and the call in `render_to`.
With `EM_SPU2_OUTPUT_TEST` unset, it delegates to the existing renderer. The
comparison-only value `voice` selects the experimental **pre-core dry SFX
bus**. It does not select a completed SPU2 output profile.

The observer sees the already scheduled command ring after rendering up to
the upcoming IOP tick, when the existing driver applies those same commands.
It never changes the commands, sequencer, timing, driver records, legacy
envelope, status replies or allocation. The original renderer still advances
all of those. This deliberate separation is why the new ADSR does not yet fix
the legacy feedback's known sustain-step difference.

Synthetic tests verify unchanged driver bytes and default PCM after each
render chunk, exact command boundaries, split-render invariance, both cores'
effect masks, non-loop/loop history and volume signs.

## Private game settings

`python3 tools/export_spu2_effects.py` executes the original driver dispatcher
and resident LIBSD for both cores and every supported effect mode. The
exporter writes only ignored `assets/spu2/` files. Its inputs include the
verified local driver and the fork's panel IOP capture.

The original `func_001FB210` definition sets mode 4 on both cores, effect
endpoints `0x1FFFFF` and `0x19FFFF`, and initial effect returns zero. The exporter
derives the tap addresses and coefficients by executing the original setters,
rather than inserting a named public preset. Its MMIO view does not alias low
IOP RAM. The focused effect-setting oracle does not establish boot-time master
volumes or MMIX. Those are intentionally not invented.

The C wrappers also enqueue `0x17` and `0x18`. A focused instruction-oracle
probe of both cores, in the captured effect context and with arguments 0, 1
and `0x1234`, observed effect-register writes but no master-volume or MMIX
writes. It did not independently assert the resident effect mode. This does
not establish complete command semantics or recover the missing core setup.
Receipt: `build/spu2-output/core-command-probe.json`; the local reproduction
script is retained alongside it.

The private format, loader validation and measurements are described in the
source files. The export is a configuration input, not evidence that the
reverb waveform or whole mix is correct.

## Tests and captures

Fast tests (no disc, emulator, device or window):

```sh
make test-spu2-output
```

The tests compile with warnings as errors, ASan and UBSan. Deliberate production
mistakes must compile and then fail unchanged assertions: Gaussian phase,
exponential envelope scaling, fixed-volume sign, effect routing, and the reverb
cases. The comparison tests also reject a one-LSB audio change, malformed
formats, missing coverage and uncertain alignment.

The existing SFX runtime test only gains the new source filenames in its
compile command. Its expected samples and assertions are unchanged.

An opt-in original capture uses the decomp's `ForkSession`, a fresh ignored
output folder and `../Extermination/build/.pcsx2.lock`:

```sh
python3 tools/capture_spu2_output.py --help
```

It starts a hidden session and closes only its own session. Existing user
sessions, states and locks are not stopped or replaced. The fresh receipt is
`build/spu2-output/original-authorized/receipt.json`; the WAV and synchronization
records stay beside it. It recorded 1,441,442 stereo PCM16 frames at 48 kHz:
30.030 seconds over 1,800 post-New-Game loop ticks. Sample count minus measured
SPU2 clock ranged from -1.065 to +0.760 samples. The session was closed and the
shared lock released.

There are 24 age-zero EE voice observations in that window. They bracket
key-on at a loop boundary; they are **not exact SPU2 register-cycle key-ons**.
Their alignment uncertainty is approximately one field (801 samples).
Mixed windows containing these observations are not isolated-voice recordings.

The fork's documented `audio_tap` API provides the full post-master stereo
mix. It has no documented voice selection or mute/solo operation.
`memwatch_add` is EE-only; IOP log probes report containing-block timestamps.
Adding forced execution stops can affect timing, and writing memory shadows
does not establish that a hardware register handler ran. Exact isolated-voice
evidence therefore still needs an appropriate fork interface or a controlled
original-driver fixture with proven isolation and effect history.

Comparison:

```sh
make test-spu2-output-compare \
  SPU2_COMPARE_MANIFEST=build/spu2-output/example-private-manifest.json
```

Supply a real private manifest as documented by
`tools/test_spu2_output_compare.py`. No example capture or manifest containing
game material is shipped. The tool reports integer PCM max/RMS errors overall,
per channel and per supplied event. It does not fit gain, resample, search for
a favorable offset, discard unmatched tails, or treat a matching prefix as a
passing whole mix. Separate isolated reference/native WAVs and their isolation
evidence are required for per-voice claims. Uncertain alignment or incomplete
coverage prevents an exactness PASS even if compared samples happen to match.

## Part A results (historical, load timing retained)

The baseline build is warning-free. Before the output hook, the private asset
export and these checks passed:

- `newgame-control`: displacement **9.599849**.
- `make test-level-smoke`: first control, status and battery, including the
  unchanged sound checks and two presentation-retention checks.

The fresh original and native replay use the same recorded input prefix.
Twenty-four age-zero voice observations pair at identical replay segment and
offset, forming eleven distinct observation ticks. The corresponding 4,800-frame
mixed windows include music and other voices. All carry 801-sample alignment
uncertainty; **none is a per-voice comparison**.

| Comparison | Max absolute error (PCM16 units) | RMS error (PCM16 units) |
|---|---:|---:|
| Baseline, common whole-mix span | 46,826 | 7,587.507850 |
| Experimental dry voice probe, common whole-mix span | 47,547 | 7,588.309793 |
| Probe left channel | 45,501 | 7,492.728819 |
| Probe right channel | 47,547 | 7,682.701731 |

The common span is 1,435,835 stereo frames. The original has 5,607 further
frames (seven load ticks); these are reported as unmatched, not dropped from
an exactness claim. The whole-mix alignment starts at the same first post-title
tick and retains the internal load-timing difference. Neither run is bit-exact
or passes the comparison gate. The probe does not improve whole-mix RMS.

| Mixed observation group | Baseline max / RMS | Probe max / RMS |
|---:|---:|---:|
| 1 | 35,216 / 9,899.025 | 35,335 / 9,914.023 |
| 2 | 21,918 / 6,604.043 | 21,918 / 6,563.631 |
| 3 | 32,439 / 10,721.193 | 32,182 / 10,685.215 |
| 4 | 37,070 / 12,392.881 | 37,471 / 12,396.925 |
| 5 | 21,945 / 6,705.825 | 21,496 / 6,495.632 |
| 6 | 16,001 / 4,662.500 | 15,255 / 4,516.494 |
| 7 | 10,687 / 2,551.315 | 8,839 / 2,362.182 |
| 8 | 7,660 / 1,879.790 | 8,221 / 2,255.944 |
| 9 | 8,057 / 1,948.893 | 7,943 / 1,897.897 |
| 10 | 11,347 / 2,768.967 | 11,564 / 2,676.198 |
| 11 | 12,639 / 2,840.426 | 12,375 / 2,780.085 |

Receipts: `build/spu2-output/native-baseline-authorized/metrics.json`,
`build/spu2-output/native-voice/metrics.json`, and each directory's
`alignment-receipt.json`. Both `native.rec` files and the entire 93,943,931-byte
`ticks.jsonl` files are byte-identical between the native baseline and probe.
That proves preservation of this replay's logged behavior; it does not prove
the waveform. The missing wet/core/stream stages and timing differences are
known confounders. These mixed measurements cannot apportion their error
between individual voices, ADSR, interpolation and reverb.

The baseline means the captured **default dry output**. A binary hash was not
recorded at launch. Its recording finished before the subsequently reused
baseline executable was rebuilt; that executable's current hash cannot identify
the captured binary. Supporting timestamps and this limitation are retained in
`build/spu2-output/capture-binary-provenance.json`.

Candidate regression results:

- `make all`: PASS, zero compiler warnings (`build-final.log`).
- Isolated staged-index `make all`: PASS, zero compiler warnings
  (`build-staged-index.log`, source object IDs in `staged-build-receipt.json`).
  The exported index tree and its executable were removed after validation.
- `make test-spu2-output`: PASS, including the interpolation, envelope,
  volume, effect-mask and six reverb mistake controls (`units-final.log`).
- `make test-area11-sfx test-area11-sfx-reference`: PASS, with existing
  assertions unchanged (`unit-and-sfx.log`).
- `newgame-control`: PASS, displacement **9.599849** (`newgame-final.log`).
- `make test-level-smoke`: PASS; its complete host-speed tick log is
  byte-identical to the pre-hook baseline (`smoke-final.log`,
  `baseline-host-ticks.jsonl`, `final-host-ticks.jsonl`).
- `make test-level-smoke-ps2-drive`: PASS, all 20 live phases through
  AREA01 arrival, unchanged audio assertions and three presentation-retention
  checks (`smoke-ps2-final.log`). No pre-hook PS2-drive log comparison is
  claimed: that baseline attempt stopped on missing private AREA01 assets,
  which were regenerated before the successful candidate run.

All paths in this list are under ignored `build/spu2-output/`. No omitted check
should be read as passing.

## Part A2: aligned capture and offline stage measurements

The comparison follows the decomp's `VIDEO_COMPARE.md` recipe for
`demo_level_1019`: native `EM_PS2_DISC_DRIVE_TIMING=1`,
`EM_PS2_DRIVE_REPLAY` with the freshly measured original read latencies,
`EM_IOP_TICK_PHASE=68`, and fork title delay **0**. The fresh original prefix
contains 2,400 post-New-Game ticks. The owned headless fork session was closed
and its lock released. All recordings and original state remain ignored.

Ordinary `EM_INPUT_PLAY` did not activate the existing drive-replay anchor;
only the level-smoke path called that API. The private preparer therefore
adds an anchor at each replay segment's first input step, before the EE stream
work, and uses the same private binary for every native pass. Its exact patch
and source/binary provenance are retained under
`build/spu2-a2/native-observer/`. No tracked replay or shared audio source was
edited. All **57/57** measured reads were served; there were **zero misses
inside the recorded span**, zero unreached entries, and one reported miss
outside the span. Fifty-one reads use the existing two-tick nearby lookup.
The replay latencies happened to equal the current drive model in this prefix;
the replay activation and complete coverage were nevertheless checked.

The baseline, dense observer, and sound-log passes have byte-identical WAVs,
input recordings and frame traces. Each WAV contains 3,031,028 stereo PCM16
frames at 48 kHz. The private observer retains 6,167,944 per-voice sample rows,
source history, loop/source identity, applied register/key events, DMA data,
and both producers' ring-drain mappings. These are native modeled states,
not observations of the original SPU2 registers. No DSP stage affects these
captures or the default executable.

Receipts under `build/spu2-a2/`:

- `original-prefix-2400-host/receipt.json`, `audio_sync.json`, and
  `drive_reads.json`: original capture, sample synchronization and reads.
- `native-baseline/receipt.json`, `native-observed/receipt.json`, and
  `native-sound/receipt.json`: native launch environment, input and binary
  hashes, coverage and counts.
- `observer-equivalence.json` and `sound-log-equivalence.json`: exact
  artifact equivalence across those native passes.
- `native-observed/observer/capture.json`: private observer schema,
  clocks, generations, counts and data hashes.
- `observer-test/receipt.json`: unchanged synthetic PCM, driver and voice
  state with observation off/on, including loop, DMA and key ordering.

### OPTIONS and sound identity

The original OPTIONS sound action `00201720` toggles `settings[4]` between
zero and one; the title and gameplay menus call that action. `001FB100`
selects stereo or mono channel levels, never zero. The existing original
`c10/options/opt_02_sound/trace.json` recording contains both transitions.
The native options/stream implementations agree. This option cannot mute BGM
independently, so this experiment does not alter settings or force a mute.

All **31** distinct age-zero EE sound groups in the captured gameplay prefix
match native groups at the same replay segment and offset. These localize
the EE onset observation to a game tick; the hardware KON sample and additional
EE-to-IOP-to-SPU2 latency remain unmeasured. The comparison reports the tick-aligned
100 ms window and, separately, a bounded waveform-lag diagnostic. It never
fits gain or resamples. Silent and ambiguous matches remain explicit.

### Original initialization and game controls

`probe_spu2_initialization.py` executes the original driver module entry,
created RPC thread, initialization command, and six game effect commands,
with the reached resident LIBSD bodies intact. Only the named OS service
boundaries are scripted. SPU2 register storage is separate from IOP RAM.
Three initial configuration fills produce **1,018 stable written bytes and
zero unstable written bytes**, including the master, MMIX and effect
registers. This closes the configuration gap left by the Part A effect-only
exporter. The master, MMIX, ATTR and AVOL/BVOL words are kept raw and are not
interpreted (see the clean-room boundary).

It does not establish a cold IOP reset, live register timing, or cleared
effect RAM: resident LIBSD RAM comes from the cited original capture, and
DMA/hardware execution is outside this oracle. Unwritten bytes remain
unknown. Receipt: `initialization-game-final/receipt.json`, which includes
the original driver, resident RAM and probe-tool hashes.

Runtime EON and effect-return changes are reconstructed from applied native
commands, with original-instruction probes for the command-to-register
mapping. `spu2_observer_controls.py` retains both producer clocks and maps
each change through its observed drain range. Initialization, runtime
commands and modeled routing are separate evidence sources.

The first initialization probe leaves both masters at zero. Using that state
for playback would merely silence the output and misleadingly reduce RMS
against an audible reference. `probe_spu2_master_restore.py` instead follows
the pure C startup/movie lifecycle: the movie tail mutes both cores, and the
wrapper restores their levels before clearing the movie flag. Six original
driver/LIBSD cases verify the exact master writes, including an asymmetric
control. The first captured play observation follows the movie wrapper's
return. The restored-state experiment executes all nine game initialization
requests and then those proven restore commands on the same oracle CPU.
It is a **conditional post-movie state after RPC processing**, not a measured
live-register timestamp. Receipts: `master-restore.json` and
`initialization-playback/receipt.json`.

The drain-to-WAV offset is **800 samples**, independently derived from every
one of the 7,568 producer pull records and replay sample positions. The
initial silent pull precedes both mixers. Applying this mapping makes the
reconstructed legacy voice sum **PCM16 bit-exact to the native baseline**
across segments 1–10: **1,916,315 stereo frames**. Startup/movie output is
outside that reconstruction and is explicitly excluded. This verifies the
offline experiment's input and timing reconstruction; it does not certify
any new DSP stage against the original. Receipts:
`offline/core-controls-final-clock.json` and
`observer-reconstruction-final.json`.

### Measured stage changes

Final measurements are in
`build/spu2-a2/offline/measure-playback-final/report.json`. The report includes
31 matched EE onset groups, seven stereo candidates and five trimmed
stereo-stream windows. It verifies actual reference, observer and generated
WAV hashes before comparing. A changed file of the same length is rejected.

The paired mix contains **1,916,312 frames**. Original unmatched coverage is
5,609 frames; native unmatched coverage is 1,114,716 frames, including
title/movie material and segment tails. No whole-WAV equality is claimed.
This prefix is longer than Part A's, so subtracting its RMS from the historical
7,588 result would not isolate the alignment improvement.

Each row below adds one stage cumulatively over exactly the same paired
samples, in signed PCM16 units. These are model experiments using native
phase, source history and voice lifetime. They do not measure the error of
each DSP primitive independently of those inputs.

| Added stage | RMS before | RMS after | Max after | Bit-exact to original? |
|---|---:|---:|---:|---|
| Gaussian interpolation | 6,318.336039 | 6,283.226058 | 50,686 | No |
| ADSR | 6,283.226058 | 6,283.209676 | 50,687 | No |
| Integer voice volume | 6,283.209676 | 6,283.186864 | 50,688 | No |

The Gaussian change reduces RMS modestly; ADSR and gains explain very little
of this prefix's residual error. Two further rows, "core routing and restored
master volumes" and "game effect settings, conditional zero-history reverb",
and the matching window columns are **withdrawn**: the code that produced
them was built from the non-permitted source and has been removed (clean-room
boundary above). Their receipts under ignored `build/spu2-a2/offline/` are
not evidence and are not cited. Core-0 PMON/NON fields not written by the
initialization oracle also remain unknown; the experiment retains the
captured native nonmodulated sources.

The five trimmed windows have exactly two hard-panned native stream voices
and zero native SFX rows at every sample. Four contain cue 63 and the last
contains cue 25. Original EE occupancy supports the stereo-sound identity,
but stale ENVX replies and possible wet tails prevent certified original
voice isolation. These are **single-stereo-sound candidates**, not a claim
that one hardware voice or only dry audio was recorded.

For each window, one bounded acoustic lag is selected from the baseline and
then reused unchanged for every stage on the same interior samples. This
diagnostic is separate from matching the EE onset tick; it is not a measured
KON offset. No stage receives its own favorable realignment.

| Window / full duration | Fixed baseline lag (samples) | Baseline RMS | +Gaussian | +ADSR | +Voice gain |
|---|---:|---:|---:|---:|---:|
| cue 63 / 501.625 ms | +251 | 3,292.123 | 3,291.797 | 3,291.655 | 3,291.402 |
| cue 63 / 152.000 ms | +70 | 6,210.362 | 6,195.179 | 6,195.087 | 6,194.956 |
| cue 63 / 152.729 ms | +466 | 4,285.990 | 4,292.411 | 4,292.254 | 4,292.091 |
| cue 63 / 153.458 ms | −70 | 5,497.526 | 5,487.662 | 5,487.544 | 5,487.389 |
| cue 25 / 87.062 ms | +739 | 88.504 | 90.989 | 90.975 | 91.025 |

Every entry remains non-bit-exact. RMS improvement does not imply smaller
worst-case error: in the first window, Gaussian increases max error from
13,839 to 14,693. In the last, it increases max from 311 to 319. The report
retains max/RMS and both channels for every stage and sound group, including
worse results.

The first window's full, unshifted RMS is 3,755.906883 over 24,078 frames.
On the common 22,476-frame interior, baseline RMS is 3,586.178 before the
lag diagnostic and 3,292.122609 after it. This is the comparable alignment
before/after pair. The remaining error cannot be assigned solely to omitted
DSP stages by this experiment; source phase/history, original wet state and
the additional request-to-hardware latency still need direct evidence.

| Stage | What main needs before production integration |
|---|---|
| Gaussian | Independent decoder history across every consumed source sample, loop and DMA boundary; establish startup phase against original output. |
| ADSR | An independent voice lifetime and original applied key timing; reconcile ENVX feedback with unchanged driver assertions. |
| Voice gain | Apply signed fixed/sweep controls at their sample boundary, preserving integer rounding. |
| Core/master | Not modelled. First establish the mixer order, MMIX/ATTR meaning, master stage and any routing between the cores from a permitted source (black-box measurement against the fork's SPU2 tap or the original code); then retain initialization, movie mute/restore and runtime controls. |
| Reverb | Needs the core stage above. Establish RAM/cursor/FIR history and effect changes; measure SPU2 filter phase and clipping before replacing the current dry output. |

### Reproduction and unchanged native gates

All A2 tools use the standard library and the project's existing native
compiler/oracles. They write private outputs under the port's ignored `build/`.
The observer preparer requires a fresh destination, rejects symlink ancestry,
and copies only tracked source/Makefile inputs. Its output is a disposable
comparison build. The synthetic observer test creates and removes its own
copy; it does not need a previous capture build.

```sh
make test-spu2-output
make test-spu2-a2-tools
python3 tools/prepare_spu2_observer.py --build --out build/spu2-a2/reproduce-native
python3 tools/capture_spu2_aligned.py --help
python3 tools/capture_spu2_native.py --help
```

Use `capture_spu2_aligned.py` to capture a fresh original prefix, then
`prepare_spu2_drive_replay.py` to generate its segment anchors. The latter
reproduced the actual 10-anchor/57-read table byte-for-byte
(`playback-reproduced.json`). Capture native baseline/observer passes with
`capture_spu2_native.py`; finalize observer hashes with the preparer's
`--finalize` mode. `compare_spu2_alignment.py --observer` verifies those
hashes and both timelines before identifying sound windows.

For offline stages, run `spu2_stage_compare.py --capture`, the initialization
and master-restore probes, `spu2_stage_state.py`, and
`spu2_observer_controls.py`. The resulting clock receipt feeds
`spu2_stage_measure.py` (voice stages only; the former core routing tool was
removed with the non-permitted source). Each tool's `--help` names its
required private inputs.
No default settings, silent missing stage, fitted gain or resampling are used
to obtain a fidelity PASS. The stage tools report diagnostic completion and
explicitly keep `fidelity_pass` false.

The canonical executable remains SHA-256
`0a10aad1f7302d68e323d8a69652e3f3e47d32ff76a5a5e4a1d99ad148776824`.
A2 has no production-source changes. The unchanged-binary Part B receipts
remain New Game **PASS 9.599849**, host smoke **PASS**, PS2-drive smoke
**PASS**, with both tick logs byte-identical to the pre-B baseline:
`build/level-oracle-audit/native-gates/receipt.json`. These gates were not
rerun for each offline tool edit; the binary/source identity is checked
instead. The A2 captures additionally prove that observation and sound
logging preserve this replay's PCM and complete tick traces.

Final A2 checks: **PASS** for the Part A arithmetic/mutation suite, the
self-contained observer test, eight alignment tests and ten stage tests.
The stage suite rejects a same-length WAV replacement and a one-LSB change.
(2026-10-20: the core-chain stage test and its external-gain control were
retired with the code they covered, which came from the non-permitted
source; the suite is now nine tests, unchanged except that the stage-values
test also checks that a core-stage manifest is rejected. The output unit test's
core-stage case was retired for the same reason.) The isolated staged-index
build passes with **zero
warnings**. `build/spu2-a2/final-verification.json` indexes those logs and
source receipts. Scratch source copies, capture executables and derived
binary caches are removed; referenced captures, comparison WAVs, patches
and receipts are retained.

## Requests for main

### Integration still required

The following are the lane's concrete boundaries for main; none has been
applied yet:

1. Integrate the recovered initialization at the actual applied command
   boundary. The A2 initialization oracle now supplies the written master,
   MMIX and effect register words (raw; MMIX/ATTR/AVOL/BVOL are not
   interpreted, see the clean-room boundary); preserve unknown fields and establish runtime
   changes and effect-memory initialization. Retain complete `0x14`/`0x15`
   state instead of treating a forwarded request as applied hardware state.
2. Feed stream voices and SFX voices into the same 48 kHz two-core output
   owner, before reverb, core routing and clipping. The existing
   `em_iop_stream_set_tap` exposes decoded stream samples in `spu_sample`;
   voice and clock accessors can supply their native modeled state. The
   tap skips source samples at pitches above one sample per output frame
   and follows the legacy envelope lifetime, so it cannot alone supply
   a replacement Gaussian/ADSR voice renderer. That needs its own decoder
   history and the applied register writes, including delayed stream key-ons.
   The
   output owner still needs a consumer hook that replaces the independent
   stream/SFX sum. Summing already mixed float output later loses the
   per-voice input and wet-send information.
3. Apply `0x16` effect-return changes at their IOP sample boundary, not at the
   later host audio callback. Existing
   `em_iop_stream_effect_volume` exposes only the latest pair, not a timeline.
4. Measure exact key-on/sample timing and original isolated output, including
   residual effect tails. A2 pairs all 31 EE onset groups at the same tick,
   but this does not identify the hardware KON sample. Resolve envelope
   feedback differences against unchanged sound assertions before replacing
   the legacy ENVX owner. The offline ADSR experiments retain native source
   histories and lifetimes; they do not validate new lifetime/feedback logic.
5. Establish the core stage from a permitted source before implementing it:
   the mixer order, the MMIX/ATTR bit meanings, the master stage and any
   routing between the cores, by black-box measurement against the fork's
   SPU2 tap (or the original code). Leaked or NDA SDK material may not be
   used. Then apply it from the measured register state and re-run the
   comparison on the whole route, including independently isolated sounds
   and effect tails.
6. If ordinary input replay is to support comparison-only drive replay, add
   its segment anchor at input step C. The private preparer emits the exact
   proposed hook. Preserve default host-speed loading and phase settings.
   Map observer drain clocks to WAV time with replay positions; A2 found an
   initial 800-sample silent pull before either producer mixer ran.

### Developer switch (recorded 2026-10-29)

The lane's proposed entry for the switch `em_spu2_output_render`
(`src/game/em_spu2_output.c`) reads; it is now in STARTUP.md "Developer
switches" and LAUNCHER_OPTIONS.md "Not launcher options":

> ### EM_SPU2_OUTPUT_TEST=voice: the experimental dry SFX voice bus (comparison only)
>
> Added with the SPU2 output model (SPU2_OUTPUT.md). Unset, or any value
> other than `voice`, changes nothing: `em_spu2_output_render` delegates to
> the established SFX renderer, and the PCM and the driver record are
> byte-identical to the build without the switch (`make test-spu2-output`
> checks unset and an unknown value; newgame-control gives 9.599849).
> `voice` writes `spu2 output: experimental dry voice bus; core/stream output
> unverified` to stderr and replaces the SFX mix with the pre-core dry voice
> sum of the public DSP model; the legacy renderer still advances the
> commands, sequencer, timing, envelopes and status replies, so nothing the
> game computes changes. It is not an SPU2 output profile: no reverb, master
> volume, core routing or stream voices, and no waveform certification. It
> aborts on an output rate other than 48 kHz or on noise routing. The value
> is read at the first render and again whenever the render clock restarts.
> Not a launcher option (comparison only; main to record it in
> LAUNCHER_OPTIONS.md "Not launcher options").

### Exact observation patch for main (not applied)

The following diff is an initial integration prerequisite, not a completed
output replacement. It exposes the existing backend's modeled sample clock,
command dispatch, actual default-model register writes, and every completed
sample (including silence). Delayed stream key-on is observed where the
transfer consumer applies it, separately from command arrival. Effect-area
and mode requests remain explicitly distinct from applied effect returns.
It adds no assumed master-volume, MMIX or wet-routing values.

`git apply --check` passed against the lane's tree (main at 2026-10-20); main
has moved since and must re-check it. A private
scratch copy compiled with `-Werror`, ASan and UBSan. Its synthetic test passed
command/register ordering, delayed key-on, silent-frame callbacks, observer
removal, and unchanged rendered PCM and driver state. This is synthetic
validation only; the patch was not applied to the native game build or the
protected tracked files. Main must review and validate it on its current tree.

Receipts: `build/spu2-output/requests-main.patch`,
`build/spu2-output/requests-main-validation.json`, and
`build/spu2-output/proposed-main/observation_test.c`. The local generation
script is `build/spu2-output/draft_main_request.py`; generated source copies
and test binaries were removed after validation.

A complete shared-renderer wiring diff remains outstanding: independent stream
decoder/ADSR lifetime, applied master/routing state, consumer replacement and
certified original isolated output. A2 establishes the two captured producer
clocks' mapping and executes initialization, but does not integrate either
into production. This observation patch does not satisfy those remaining
Part A requirements.

```diff
--- a/src/game/em_iop_stream.h
+++ b/src/game/em_iop_stream.h
@@ -428,6 +428,60 @@
  * ADPCM sample before envelope and volume (NULL clears). Game thread. */
 typedef void (*EmIopStreamTap)(void *ctx, int voice, int16_t sample);
 void em_iop_stream_set_tap(EmIopStream *s, EmIopStreamTap fn, void *ctx);
+/* Optional output observers: game thread, synchronous and read-only. Their
+ * callbacks must not re-enter/mutate this backend; copy arguments to retain
+ * them. No observer replaces the established mixer or changes driver timing.
+ * sample is s->samples in the backend's existing 48 kHz model, NOT a measured
+ * PS2 key-on timestamp or the SFX side's independent boot-relative clock. */
+enum {
+    EM_IOP_EFFECT_END_REQUEST = 1u,
+    EM_IOP_EFFECT_MODE_REQUEST = 2u,
+    EM_IOP_EFFECT_RETURN_LEFT_APPLIED = 4u,
+    EM_IOP_EFFECT_RETURN_RIGHT_APPLIED = 8u
+};
+typedef struct {
+    uint32_t known;
+    uint32_t end_request, mode_request;
+    uint16_t return_volume[2];
+    uint64_t end_sample, mode_sample, return_sample[2];
+} EmIopOutputEffectView;
+typedef struct {
+    uint64_t sample, tick, sequence;
+    uint32_t command[4];
+    /* 0x14/0x15 currently only reach forward. This says the sink returned,
+     * NOT that absent effect-area/mode/clear implementation was executed. */
+    int forwarded;
+} EmIopOutputCommandEvent;
+typedef enum {
+    EM_IOP_OUTPUT_PARAM, EM_IOP_OUTPUT_SWITCH, EM_IOP_OUTPUT_ADDR
+} EmIopOutputWriteKind;
+typedef struct {
+    void *ctx;
+    void (*command)(void *ctx, const EmIopOutputCommandEvent *event);
+    /* Supported writes after the default libsd model applies them. A
+     * substituted libsd implementation must provide its own observations.
+     * In particular, KON is here when the transfer consumer executes it,
+     * separately from the earlier 0x42 command-dispatch event. */
+    void (*register_write)(void *ctx, uint64_t sample,
+                           EmIopOutputWriteKind kind,
+                           uint16_t reg, uint32_t value);
+    /* Called once after all voices, including silent frames, so an observer
+     * can finish this sample and advance effect tails. Reuse set_tap above
+     * for raw per-voice input: inside that callback clock/spu_voice expose
+     * its current sample index, ENVX, volume words, pitch and decoded block.
+     * The tap follows legacy ENVX lifetime and omits source samples skipped
+     * by pitch > 0x1000; independent interpolation needs its own decoder.
+     * No dry/wet decision is fabricated; the shared owner still needs the
+     * original VMIXL/R and VMIXEL/ER state. */
+    void (*frame_end)(void *ctx, uint64_t sample);
+} EmIopOutputObservers;
+void em_iop_stream_set_output_observers(EmIopStream *s,
+                                        const EmIopOutputObservers *observers);
+/* Snapshot of observed requests/default-libsd EVOL writes. known starts at
+ * zero: no master/MMIX/effect initialization is inferred from calloc. */
+int em_iop_stream_output_effect_view(const EmIopStream *s, unsigned core,
+                                      EmIopOutputEffectView *out);
+
 /* FNV-1a over the rendered stereo frames (float bits), game thread. */
 uint64_t em_iop_stream_pcm_digest(const EmIopStream *s);

--- a/src/game/em_iop_stream.c
+++ b/src/game/em_iop_stream.c
@@ -55,6 +55,9 @@
     void *forward_ctx;
     EmIopStreamTap tap;
     void *tap_ctx;
+    EmIopOutputObservers output_observers;
+    EmIopOutputEffectView output_effect[2];
+    uint64_t output_command_sequence;
     EmIopSpuVoice spu[EM_IOP_VOICES];
     uint16_t evol[2][2];
     uint8_t trans_busy;
@@ -228,6 +231,8 @@
                 spu_block_end(s, p);
         }
     }
+    if (s->output_observers.frame_end)
+        s->output_observers.frame_end(s->output_observers.ctx, s->samples);
 }

 /* The ring the audio thread drains; every frame also feeds the digest. */
@@ -276,17 +281,35 @@
     return &s->spu[core * 24 + v];
 }

+static void output_register_write(EmIopStream *s, EmIopOutputWriteKind kind,
+                                  uint16_t reg, uint32_t value)
+{
+    if (s->output_observers.register_write)
+        s->output_observers.register_write(s->output_observers.ctx, s->samples,
+                                            kind, reg, value);
+}
+
 static void sd_set_param(void *ctx, uint16_t reg, uint16_t value)
 {
     EmIopStream *s = ctx;
     EmIopSpuVoice *p;
     if (reg & 0x80) {                     /* core parameters */
+        int side;
         if ((reg & 0xFF7E) == 0x0B00)
-            s->evol[reg & 1][0] = value;  /* EVOLL */
+            side = 0;
         else if ((reg & 0xFF7E) == 0x0C00)
-            s->evol[reg & 1][1] = value;  /* EVOLR */
-        else
+            side = 1;
+        else {
             fault(s, EM_IOP_DRV(0x314C), EM_IOP_FAULT_UNSUPPORTED);
+            return;
+        }
+        s->evol[reg & 1][side] = value;
+        EmIopOutputEffectView *view = &s->output_effect[reg & 1];
+        view->return_volume[side] = value;
+        view->return_sample[side] = s->samples;
+        view->known |= side ? EM_IOP_EFFECT_RETURN_RIGHT_APPLIED
+                            : EM_IOP_EFFECT_RETURN_LEFT_APPLIED;
+        output_register_write(s, EM_IOP_OUTPUT_PARAM, reg, value);
         return;
     }
     if (!(p = reg_voice(s, reg)))
@@ -297,8 +320,9 @@
     case 0x0200: p->pitch = value; break;
     case 0x0300: p->adsr1 = value; break;
     case 0x0400: p->adsr2 = value; break;
-    default: fault(s, EM_IOP_DRV(0x314C), EM_IOP_FAULT_UNSUPPORTED); break;
-    }
+    default: fault(s, EM_IOP_DRV(0x314C), EM_IOP_FAULT_UNSUPPORTED); return;
+    }
+    output_register_write(s, EM_IOP_OUTPUT_PARAM, reg, value);
 }

 static uint16_t sd_get_param(void *ctx, uint16_t reg)
@@ -329,6 +353,7 @@
         else if (p->on)
             em_sfx_envelope_key_off(&p->env);
     }
+    output_register_write(s, EM_IOP_OUTPUT_SWITCH, reg, value);
 }

 static void sd_set_addr(void *ctx, uint16_t reg, uint32_t value)
@@ -340,6 +365,7 @@
         return;
     }
     p->ssa = value;
+    output_register_write(s, EM_IOP_OUTPUT_ADDR, reg, value);
 }

 /* NAX: the block being played, plus the data halfword of the current sample
@@ -501,6 +527,22 @@
 {
     s->tap = fn;
     s->tap_ctx = ctx;
+}
+
+void em_iop_stream_set_output_observers(EmIopStream *s,
+                                        const EmIopOutputObservers *observers)
+{
+    if (!s) return;
+    if (observers) s->output_observers = *observers;
+    else memset(&s->output_observers, 0, sizeof s->output_observers);
+}
+
+int em_iop_stream_output_effect_view(const EmIopStream *s, unsigned core,
+                                      EmIopOutputEffectView *out)
+{
+    if (!s || !out || core >= 2) return -1;
+    *out = s->output_effect[core];
+    return 0;
 }

 /* ======================================================================== *
@@ -1298,6 +1340,7 @@
     EmIopStreamVoice *p;
     int32_t core, v, vv;
     uint32_t mask, bit;
+    int forwarded = 0;
     if (latched(s))
         return -1;
     switch (c[0]) {
@@ -1390,9 +1433,33 @@
         if (!s->forward)
             return fault(s, EM_IOP_DRV(0x634), EM_IOP_FAULT_UNSUPPORTED);
         s->forward(s->forward_ctx, c);
+        forwarded = 1;
         break;
     }
-    return latched(s) ? -1 : 0;
+    if (latched(s)) return -1;
+    /* The dispatcher ran after spu_render_to reached this sample. Keep
+     * request-only fields distinct from default-libsd-applied EVOL above. */
+    if (c[1] < 2 && (c[0] == 0x14 || c[0] == 0x15)) {
+        EmIopOutputEffectView *view = &s->output_effect[c[1]];
+        if (c[0] == 0x14) {
+            view->known |= EM_IOP_EFFECT_END_REQUEST;
+            view->end_request = c[2];
+            view->end_sample = s->samples;
+        } else {
+            view->known |= EM_IOP_EFFECT_MODE_REQUEST;
+            view->mode_request = c[2];
+            view->mode_sample = s->samples;
+        }
+    }
+    EmIopOutputCommandEvent event;
+    event.sample = s->samples;
+    event.tick = s->ticks;
+    event.sequence = s->output_command_sequence++;
+    memcpy(event.command, c, sizeof event.command);
+    event.forwarded = forwarded;
+    if (s->output_observers.command)
+        s->output_observers.command(s->output_observers.ctx, &event);
+    return 0;
 }

 /* 0x544: channel 0's transfer callback (command 0x1E registers it): the
```

### Proposed audit wording (exact documentation diffs)

These files are owned by main and were not edited. The proposed text preserves
the lack of waveform certification and corrects the obsolete claim that an
original recording cannot be obtained headlessly.

```diff
--- a/docs/FIDELITY_FEATURES.md
+++ b/docs/FIDELITY_FEATURES.md
@@
-  The audible stages (reverb, interpolation, ADSR, levels) are the port's
-  documented model; no WAV of the original exists to compare them
-  (`FIRST_LEVEL_AUDIT.md` 1b item 1).
+  Original output still uses the documented legacy model. A clean-room
+  public DSP model and an opt-in dry SFX voice probe now exist, with
+  synthetic mutation tests and a headless fork recording/comparator.
+  They do not certify the audible stages or the whole mix; see
+  `SPU2_OUTPUT.md` for measured errors, alignment limits and missing wiring.
@@
-  been compared with a PCSX2 recording. Not modelled: SPU2 reverb, Gaussian
-  interpolation, core master volumes. Transfer completion tick and driver
+  been certified against a PCSX2 recording. The new output primitives are
+  not wired into the stream/core path: Original still lacks reverb,
+  Gaussian stream interpolation and core master volumes. Transfer completion tick and driver
   tick phase are stated models.
--- a/docs/FIRST_LEVEL_AUDIT.md
+++ b/docs/FIRST_LEVEL_AUDIT.md
@@
-   has no oracle (AM-19; its listener D_00810360 is the original's since
-   AUDIO). The UI cues 2 / 5 / 0xD are in no captured window. What removes
-   it: an audio recording of the route (a user decision: decomp
-   CAPTURES_AUDIO.md, PCSX2 records audio only from its visible UI), then
-   the SPU2 reverb, interpolation and volume stages translated from the IOP
-   driver's own tables and an output comparison in the smoke.
+   has no oracle (AM-19; its listener D_00810360 is the original's since
+   AUDIO). The UI cues 2 / 5 / 0xD are in no captured window. The fork now
+   records SPU2 output headlessly in emulated time. SPU2_OUTPUT.md records
+   the public DSP primitives, a dry SFX probe, fresh mixed-output
+   measurements and the missing shared stream/core wiring. This item
+   remains open: exact key-on and isolated-voice comparisons, effect
+   history, master/routing state, and full-mix certification are required.
```
