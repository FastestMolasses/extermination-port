# SFX pitch and gain (WP-14, H19 / AM-01 / AM-02)

`em_sfx` now plays every registry id through the original A0 trigger path.
It uses integer SPU pitch words and the original Q14 volume words. The
legacy `assets/sfx/sfx.txt` WAV registry is retired. Its rates came from
the decomp's old `tone_rate()`, which assumed bend 0 and a ladder anchor
of 12542. The result was ×1.531–1.542 too high on all 5758 tone
references, about 118 ladder steps or 7.4 semitones sharp.

## Original path

The oracle executes `001FB9F0`, `001152D8` (with the `00117088`,
`00118E60`, `00115850`, `00117918` and `001179E0` code it reaches),
`00117918` directly, `0011A218` through `001FB9F0`, `001281C0`
(`float_to_int`) and `00121A28` (memset). In the registry check only
`001157F0` (the hardware command sink) and `001191F0` are replaced. It does not execute `001FBF50`.
It executes only the global (-1,-1) and 11.0 entries, not the office 2.1
entries.

| Step | Original | Native / exporter |
|---|---|---|
| id → record | `001FB9F0`: global tables `0025ECA0`/`00261570`, area remap/record tables `00264A70..00264B90` indexed by `D_00810700/701` | `export_sfx_registry.sound_record` |
| record → script | `D_00281D50[group*0x14+bank]` handle, `00119EA0` script-table walk | `Bank.script` |
| script grammar | `001152D8` loop, `00117088` fetch (running status), `00118E60` VLQ delta, `A0` → `00115850`, `FF 2F` → `00117C28` | `audio_export.parse_script` |
| event tick | track `+0x20` gains `delta<<12`; `+0x1C` = `0x1E0000/D_0027F740[0x1D]`; 60 in both AREA11 captures, so 8 delta units per tick | `tick` per event |
| tone | `00117088` SFX mode: program via `state+0x312` table; `00115850` tone = `note − program[6]` | exporter |
| pitch | `00115850` stores bend `0x40`, then `00117918` (`D_00241D70`, anchor `[0xD0]`=4096), then `*44100/48000` | `audio_export.a0_pitch`, EMSR `pitch` |
| volume | `001179E0`: `(ch[0xE]·ch[3]·tone[0xB]·vel·prog[1]·master)>>27`, × pan byte (`D_00242630[tone[0xC]>>2]` via `00117BA0(4,0)`) × track request, `>>19`, `(short)`, `>>1` | EMSR `scalar`/`pan`, `em_sfx_volume_words` |
| requests | `001FB9F0` → `0011A218` stores the pair only when both are in ±0x1000; otherwise the `00119EA0` defaults 0x1000/0x1000 remain | `em_sfx_volume_words` |
| positional | `001FBF50` `float_to_int(4096·gain)`: **float_to_int step only** (`001281C0`, software `__fixsfsi`, truncation, executed). The `001FBF50` gains themselves are not executed; they still come from the port's float `em_sfx_compute_gains` | `em_sfx_request_word` |

The mixer applies each 15-bit volume word as `word/16384`. This matches the
Q14 register interpretation already proven through the IOP driver in
`docs/AREA11_PANEL_SFX.md`. The arbitrary legacy `0.6` headroom factor is
gone.

## Verification

`make test-area11-sfx-reference` (about 13 s) re-exports the registry into
`build/` and requires `assets/sfx/sfx_registry.emsr` to be byte-identical to
it. It then checks the following:

- **Bank binding.** In both AREA11 captures, the handles for group 1 banks
  0–2 and group 2 bank 0 point at RAM headers equal to the bound container
  banks, except for the per-track bend bytes. Every other slot is 0 or the
  group-3 music handle. The tick divisor is 60 and the mono flag
  `D_0027F778` is 0.
- **Original execution.** For every entry the AREA11 build can play (67:
  global plus 11.0), the test executes `001FB9F0` with 5 request pairs.
  One pair is out of range. It then executes `001152D8` tick by tick with
  only `001157F0`/`001191F0` replaced. It compares every key-on voice's
  commands 6/1/5/3 (pitch, both volume words, SPU address, ADSR) with the
  exported event at that tick: 400 voices. Absent entries must return -1.
  No key-off may occur. Re-sent pitch/volume words must repeat.
- **Native volume words.** `em_sfx_volume_words` (ctypes) equals the
  original words for all cases. `em_sfx_request_word` equals the executed
  original `float_to_int` (`001281C0`).
- **Samples.** All 49 AREA11 samples equal the loaded SPU RAM in the
  opening capture.
- **Oracle extension.** `PCPYH`/`PCPYLD` are validated by running the
  original memset `00121A28` on known fills. They serve the `00118EC0`
  track free. `DIV1` is used only on the controller path of the
  unsupported elevator scripts.

`make test-area11-sfx` checks the native side under ASan/UBSan:

- 20 malformed registries are refused transactionally.
- All 71 scoped entries resolve under their scope and not without it.
- Unsupported and unscoped plays are refused and counted.
- Stop-all works on scripts with pending events.
- The 3-voice id 0x1A1 at 44.1, 48 and 96 kHz, in callbacks of 1 and 997
  frames, and one positional request match an independent Python mix.
  The largest error observed is 4.3e-8; the tolerance is 2e-7.

## EMSR v1 (`assets/sfx/sfx_registry.emsr`, local and ignored)

All fields are little-endian. The layout is:

1. **Header (20 bytes):** `"EMSR"`, u32 version=1, u32 samples,
   u32 entries, u32 0.
2. **Entry (16 bytes):** u32 id, s16 area, s16 sub, u8 state, u8 events,
   u16 reason, u32 0. The scope is (-1,-1) for group-1 records from the
   global tables; otherwise it is the (area, sub) the id was resolved for.
   The states are:
   - **1 (audible):** 1 to 8 events.
   - **2 (absent):** `001FB9F0`/`00119EA0` return -1.
   - **3 (unsupported):** carries a reason code.
3. **Event (20 bytes):** u16 tick, u16 sample, u16 pitch, u16 pan,
   u32 scalar, u16 adsr1, u16 adsr2, u8 tone flags, u8 0, u16 0.
4. **Sample:** u32 frames, followed by that many s16 samples. This is the
   ADPCM decoded at the 48 kHz base clock.

Reason codes:

| Code | Reason | Code | Reason |
|---|---|---|---|
| 1 | non-A0 script status | 6 | sustained key-off |
| 2 | looping sample | 7 | unbound bank |
| 3 | sweep volume | 8 | track-dependent defaults |
| 4 | modulation | 9 | pitch range |
| 5 | noise | 10 | no voice |

The loader rejects the following: pitch 0 or above 0x3FFF; tone flags 0x02
or 0x20; ticks that decrease; out-of-range scalars or samples; duplicate
scopes; trailing bytes.

## Current export (71 entries: 64 audible, 3 absent, 4 unsupported)

- **Unsupported:**
  - 0x452/0x453 (elevator): scripts with `B0` controllers. The original
    runs 145 ticks, 6 key-ons and 2 key-offs.
  - 0x413 (steam): looping sample, keyed off on the same tick.
  - 0x14D: looping sample with a sustained key-off.
  - All four are recorded by the oracle and need AM-03 (loop-aware voices,
    key-off/ADSR release, controllers).
- **Absent:** 0x3EE in 11.0, and 0x7D8/0x3F2 in 2.1. The legacy registry
  borrowed 0x7D8/0x3F2 through `gen_sfx_registry`'s region fallback. In 2.1
  the original remap is FF, so they now play nothing.
- **Key-off no-ops:** velocity-0 A0 events whose tone lacks flag 0x01 are
  exported as no-ops. `001176E0` keys off only voices with `+0x0C == 1`.

## Boundaries (not reproduced, not claimed)

- **ADSR.** The envelope words are exported but not applied; the gain is
  unity. The panel cue keeps its separately verified steady envelope.
- **Reverb.** Every SFX track sets the effect mask (`+0x42==1`). Output is
  dry.
- **Interpolation.** Linear, not SPU2 Gaussian. ADPCM decoder rounding is
  unverified.
- **Tick timebase.** Event starts use `tick·rate·1001/60000`: one tick per
  VBlank at the NTSC field rate. The VBlank handler `001AB140` passes the
  sequencer thread's id (`D_00282184`) to the kernel wrapper `0010C710`;
  that wrapper's syscall was not re-derived. The field rate is the video
  standard, not measured.
  Key-on latency to the next VBlank is not modeled.
- **Voice allocation.** `00117428`, used by the A0 path, is not reproduced.
  The port keeps its oldest-slot 48 budget, one slot per script instance.
- **Mono option.** `D_0027F778` (and `D_0028215B` in `001FBF50`) is not
  modeled.
- **Office scope (2.1).** It comes from the `gen_sfx_registry` preset and a
  script-coverage region match (`chunk04.n0`, coverage 1.0, not unique).
  There is no capture, so these entries are not executed.

## Effect on existing port behaviour

- **`EM_SFX_TEST` (suite self-test) now FAILs.** It plays 0x162, 0x164
  and 0x7D8 without binding a scope and requires 63 plays. 0x7D8 is
  area-dependent: it resolves through the `001FB9F0` area remap and was
  exported only for 2.1, where it is FF (absent). With no scope it is
  refused as unscoped, so the run gives 62 plays and FAIL. The legacy
  `sfx.txt` mapped 0x7D8 to a region-fallback WAV, which is why the test
  used to pass. The self-test lives in `em_game_selftest.c`, which is
  coordinator-owned. The fix is to replace the third id with a global
  (-1,-1) audible id such as 0x165 and to assert that 0x7D8 is
  unavailable without a scope. It was checked in a private build: 63 plays,
  PASS.
- **Enemy death sound 0x7D8 is silent in the office.** `em_enemy.c` plays
  it through `em_sfx_play_at` in two places. With no scope bound it is
  refused as unscoped. With 2.1 bound it is original-absent. It is audible
  only if the office turns out to be 2.0, which would need a 2.0 export.

## Binding

`em_sfx` is a service, not a pool node. The coordinator must call
`em_sfx_set_area(D_00810700, D_00810701)` whenever a scene is bound, and
`(-1,-1)` on teardown:

- AREA11 must be bound as (11,0). This call already exists in the AREA11
  interaction host.
- The office scene must be bound with its real sub-area.

Until a scope is bound, area-dependent ids are refused, counted in
`em_sfx_unscoped_cues`, and reported once on stderr. Global group-1 ids play
without a scope. That group-1 binding (the `D_00281D50` handles for
group 1 banks 0–2) was checked only in the two AREA11 captures; that it is
the same in other areas is inferred from the `001FB9F0` global-table path,
not observed. Workers keep calling `em_sfx_play` (center request) or
`em_sfx_play_at` (the `001FBF50` gains).

Regenerate: `python3 tools/export_sfx_registry.py`. It needs the decomp
checkout, `config/SCUS_971.12`, `extract/` and `extract/OVERLAY` for the
door pairs.
