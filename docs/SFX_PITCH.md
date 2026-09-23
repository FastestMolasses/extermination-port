# SFX pitch, gain and registry (WP-14, H19 / AM-01 / AM-02; sequencer in SFX_SEQUENCER.md)

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
| script grammar | `001152D8` loop, `00117088` fetch (running status), `00118E60` VLQ delta, `A0` → `00115850` (vel 0 → `001176E0`), `B0 41` → `00118078`, `FF 2F` → `00117C28` | `export_sfx_registry.script_events`; run natively by the driver (`docs/SFX_SEQUENCER.md`) |
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

`make test-area11-sfx-reference` (about two minutes) re-exports the
registry into `build/` and requires `assets/sfx/sfx_registry.emsr` to be
byte-identical to it. It then checks the following:

- **Bank binding.** In both AREA11 captures, the handles for group 1 banks
  0–2 and group 2 bank 0 point at RAM headers equal to the bound container
  banks, except for the per-track bend bytes. Every other slot is 0 or the
  group-3 music handle. The tick divisor is 60, the mono flag
  `D_0027F778` is 0 and voices 0..3 are stream voices.
- **Original execution, in lockstep with the native driver.** For every
  entry the AREA11 build can play (67: global plus 11.0) and 5 request
  pairs, `001FB9F0` and then `001152D8` run tick by tick with only
  `001157F0`/`001191F0` replaced. Every key-on voice's commands 6/1/5/3
  (pitch, both volume words, SPU address, ADSR) equal the exported
  operation: 475 voices. Absent entries return -1. Key-offs occur exactly
  where a script keys off a sustained tone. Every command, track and voice
  field equals the native driver's. `docs/SFX_SEQUENCER.md` lists the
  further passes (SPU2-model feedback, concurrency, 00117428, 001FC3C0).
- **Native volume words.** `em_sfx_volume_words` (ctypes) equals the
  original words for all cases. `em_sfx_request_word` equals the executed
  original `float_to_int` (`001281C0`).
- **Samples.** All 54 AREA11 samples equal the loaded SPU RAM in the
  opening capture, and the 3 loop points equal the loaded block flags.
- **Oracle extension.** `PCPYH`/`PCPYLD` are validated by running the
  original memset `00121A28` on known fills. They serve the `00118EC0`
  track free. `DIV1` serves `00116598`'s portamento step.

`make test-area11-sfx` checks the native side under ASan/UBSan:

- 28 malformed registries are refused transactionally.
- All 71 scoped entries resolve under their scope and not without it.
- Unscoped plays and an UNSUPPORTED fixture entry are refused and counted.
- Stop-all silences scripts with pending events.
- Ids 0x1A1, 0x14D and 0x452 at 44.1, 48 and 96 kHz, in callbacks of 1
  and 997 frames, and one positional request match an independent Python
  SPU2 model driven by the original command stream. The largest error is
  1.07e-7; the tolerance is 2e-7.
- The `001FC3C0` flame cadence through `em_sfx_loop_service`.

## EMSR v2 (`assets/sfx/sfx_registry.emsr`, local and ignored)

All fields are little-endian. The layout is:

1. **Header (24 bytes):** `"EMSR"`, u32 version=2, u32 samples,
   u32 entries, u32 ladder count (0x240), u32 0.
2. **Ladder:** 0x240 u16 values of `D_00241D70` (00117918's pitch ladder),
   so the driver can recompute portamento pitches.
3. **Entry (16 bytes):** u32 id, s16 area, s16 sub, u8 state, u8 operations,
   u16 reason, u16 bank key (group << 8 | bank index, the voice `+0x22`
   identity), u16 0. The scope is (-1,-1) for group-1 records from the
   global tables; otherwise it is the (area, sub) the id was resolved for.
   The states are:
   - **1 (audible):** 1 to 32 operations, ending with exactly one END.
   - **2 (absent):** `001FB9F0`/`00119EA0` return -1.
   - **3 (unsupported):** carries a reason code.
4. **Operation (32 bytes):** u16 tick, u8 kind (1 key-on, 2 key-off,
   3 portamento, 4 end), u8 note, u8 prog, u8 tone flags, u16 sample,
   u16 pitch, u16 pan, u32 scalar, u16 adsr1, u16 adsr2, u8 center,
   s8 fine, u8 range, u8 alloc (tone byte 0), u8 priority (tone byte 1),
   u8 length, u8 depth, u8 0, u32 0.
5. **Sample:** u32 frames, u32 loop start (0xFFFFFFFF = one-shot), then
   that many s16 samples (the ADPCM decoded at the 48 kHz base clock from
   zero history), then for a looping sample the (frames - loop start) body
   samples as replayed with the history carried over the loop end.

Reason codes:

| Code | Reason | Code | Reason |
|---|---|---|---|
| 1 | script status/controller not translated | 6 | retired (key-off now runs natively) |
| 2 | loop without loop start / unsettled body | 7 | unbound bank |
| 3 | sweep volume | 8 | track-dependent defaults |
| 4 | modulation | 9 | pitch range |
| 5 | noise | 10 | no voice |

The loader rejects the following:

- a key-on pitch that is 0, above 0x3FFF, or not the ladder result for its
  (center, note, fine);
- tone flags 0x02 or 0x20;
- non-zero padding;
- ticks that decrease;
- a missing or early END;
- out-of-range scalars or samples;
- a loop start at or after the end;
- duplicate scopes;
- trailing bytes.

## Current export (71 entries: 68 audible, 3 absent, 0 unsupported)

- **Formerly unsupported, now audible:** 0x452/0x453 (elevator: 145 ticks,
  6 key-ons, portamento on the two looping motor tones, 2 key-offs), 0x413
  (flame: looping tone keyed on and off in one flush) and 0x14D (looping
  tone keyed off after 29 ticks). See `docs/SFX_SEQUENCER.md`.
- **Absent:** 0x3EE in 11.0, and 0x7D8/0x3F2 in 2.1. The legacy registry
  borrowed 0x7D8/0x3F2 through `gen_sfx_registry`'s region fallback. In 2.1
  the original remap is FF, so they now play nothing.
- **Key-offs:** every velocity-0 A0 event is a key-off operation.
  `001176E0` matches only sustained (voice `+0x0C`) voices at run time, so
  key-offs of one-shot tones find nothing.

## Boundaries (not reproduced, not claimed)

- **ADSR.** Applied through a documented-semantics SPU2 envelope model,
  checked only against PCSX2's ENVX feedback words in the captures (see
  `docs/SFX_SEQUENCER.md`). No SPU2 output comparison exists. The panel
  cue keeps its separately verified steady envelope.
- **Reverb.** Correction: registry tracks do NOT set the effect mask.
  `00119EA0` sets `+0x42` only for handles with 0x8000 (asm `movz`; the
  decomp C is inverted), and `001FB9F0` handles never carry it. Only tones
  with flag 0x80 (97 of the 99 exported key-ons) are routed to the effect
  send (command 0xC). Output is dry.
- **Interpolation.** Linear, not SPU2 Gaussian. ADPCM decoder rounding is
  unverified.
- **Tick timebase.** One tick per VBlank at the NTSC field rate: the VBlank
  handler `001AB140` passes the sequencer thread's id (`D_00282184`) to the
  kernel wrapper `0010C710`, whose syscall was not re-derived, and the
  field rate is the video standard, not measured. The native tick grid
  starts at the first audio callback after the driver was idle, so a
  first play has no wait; later plays wait for the next tick, as in the
  original.
- **Voice allocation.** Translated (`00117428`, verified). The initial
  allocation cursor and key-on serial are 0 at boot; the captures only
  show later values.
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
- **`EM_SFX_TEST` voice-steal expectations are obsolete.** The self-test's
  60-play burst expects 12–15 oldest-voice steals and no drops. The
  original never steals an SFX voice (`00117428`) and refuses a 49th
  concurrent track (`00119EA0`), so `em_sfx_steals()` is now always 0 and
  the burst is counted in `em_sfx_drops()`/`em_sfx_voice_refusals()`
  (`docs/SFX_SEQUENCER.md`).
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
