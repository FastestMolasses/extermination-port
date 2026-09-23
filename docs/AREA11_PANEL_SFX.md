# Original AREA11 panel sound bindings

The initial AREA11 power-panel callbacks submit `0x3EF` from `001575B0`
and `0x3EE` from `001580C0`. Both use `001FB9F0(id, 4096, 4096, 4096)`.
The current area remap makes `3EE` intentionally absent. `3EF` is a real
one-shot voice; its sample and playback parameters were missing from the
native registry.

## Original source and driver proof

`tools/export_area11_sfx.py` requires the verified US ELF and the extracted
`chunk15/f00_id43.bin`. It resolves the tables rather than trusting the old
port's sound map. Original binaries, samples, generated assets and captures
remain ignored local inputs.

| Item | Original value |
|---|---|
| Area/subarea | 11 / 0 |
| `3EE` remap | byte `FF` at `0025F396`; `FB9F0` returns `-1` |
| `3EF` remap | byte `00` at `0025F397` |
| Binding record | `0025F410`: group 2, slot 0, script group 1, index 0 |
| Registered bank | handle 4; header `013351F0`; SPU base `001A0000` |
| Source container | first 303152 bytes of `chunk15/f00_id43.bin` |
| Source header / image | offsets `30` / `D60` |
| Trigger script | offset `1B4`: one `A0` note 33, velocity 100, program 1 |
| Program / tone | source offsets `C5C` / `C74` |
| Tone | center 58, fine -8, bend reset 64, range 12 |
| Original pitch ladder result | 939 |
| SPU pitch | floor(939 × 44100 / 48000) = 862 |
| Effective source rate | 48000 × 862 / 4096 = 10101.5625 Hz |
| Left / right voice gain | 2217 / 2217, signed Q14 registers |
| ADSR1 / ADSR2 | `80FF` / `5FD0` |
| Sample | source offset `42D70`; SPU address `1E2010` |
| Source size | 1312 ADPCM bytes, 82 blocks, 2296 decoded samples |
| Loop behavior | loop-start flag on block 1; terminal block flag 1, no repeat |
| Effect routing | original tone enables reverb |

The two-byte quantity following the A0 event is a variable-length delta of
zero. `00118E60` consumes it, after which `00117C28` ends the track. This
does not key off the A0 voice: its original voice record remains unchanged.
The non-loop sample end terminates the sound.

The A0 pan path is significant: `00115850` calls `00117BA0(4, 0)`, selecting
the tone's pan directly. Applying the other event path's nested program and
channel pan lookup gives the wrong gains. The old exporter also used the
wrong bend default and an incorrect pitch-ladder anchor: the original
`00241D70[208]` value is 4096. Its 15480 Hz result is not valid for this cue.

`tools/test_area11_sfx_reference.py` executes original ELF instructions
through `FB9F0`, allocation, context setup, A0 setup, pitch and gain math,
delta decoding and track termination. Only `001157F0`, the hardware-command
submission endpoint, is stubbed. In the default case it receives:

```
pitch       voice 0, 862
voice gain  voice 0, 2217, 2217
sample      voice 0, 0x1E2010
ADSR        voice 0, 0x80FF, 0x5FD0
```

The test covers 288 combinations of initially free track/voice slots and
positive request gains, plus 216 original pitch-helper cases. Controlled
allocation is explicit; this is not a proof of the existing native allocator
under contention. Both immutable opening and playable EE captures agree
with the binding, bank registration and complete 3376-byte header apart
from the known mutable bend byte. All 1312 ADPCM bytes also match the loaded
SPU RAM in the immutable opening save state.

An additional test follows the IOP endpoint instead of assuming that the
queued values are hardware values. It extracts the shipped `SNDN2DRV.IRX`
from the local rebuilt disc, locates its loaded copy in the immutable
opening IOP capture, and checks every non-relocated instruction/data word
outside the loader's patched import metadata. The original dispatch at
module offset `634` and the captured resident `libsd` setters then execute
without call hooks. All 48 voice selections produce the expected 336
register stores: pitch and volume reach the registers unscaled, and the
ADSR pair and split sample address agree. This validates the Q14 gain
interpretation without relying on the earlier port documentation.

## Native binding and playback boundary

The exporter writes `assets/sfx/area11/panel_sfx.emsf`, a scoped binary bank.
It also emits the raw ADPCM, a 48 kHz decoded-source WAV and a JSON provenance
report. The source WAV is not a final-pitch recording and must not be added
as a plain 48 kHz entry in `sfx.txt`.

`em_sfx_init()` loads the independent bank. The host calls
`em_sfx_set_area(11, 0)` after successful AREA11 binding and
`em_sfx_set_area(-1, -1)` on whole-world teardown. Selecting `(11,0)` returns
zero if the complete bank is unavailable; other pairs clear selection and
return one. `em_sfx_cue_state()` reports unavailable (0), audible (1), or
originally absent (2). An absent cue increments an explicit counter and
allocates no voice. An active voice retains immutable sample memory if the
host clears the area selection before it finishes.

The EMSF loader validates its version, scope, both records, pitch, gains,
ADSR profile, complete PCM payload, silent prefix and end of file before
replacing a loaded bank. Unknown profiles fail rather than silently using
the current envelope specialization. Load/free still require the existing
audio-device lifetime contract.

The new playback path uses an integer rational cursor, the original Q14
voice gains, and a manual-derived steady-envelope reduction. It bypasses
the legacy mixer's arbitrary `0.6` voice multiplier. The original sample's
first 41 decoded source samples are zero; its fastest attack completes
within that silence. The authored maximum sustain level spends one sample
period in decay, then the infinite sustain setting holds. The native dry
path uses `32767/32768` for all potentially nonzero samples and ends at the
non-loop sample boundary.

The register meanings, 48 kHz processing period, pitch denominator, Q14
volume format, sustain behavior and non-loop envelope termination are
documented in Sony's *SPU2 Overview v6.0*, pages 9, 24–25, 42, 44–46 and
72–77 ([archived manual](https://github.com/ninjadynamics/PS2Docs/blob/main/SPU2_Overview_Manual.pdf)).
The exact envelope register pair is verified by original instructions;
envelope microtiming and its first decay counter phase have not been
compared with live hardware/register output. This is a documented native
envelope reduction, not a claim of SPU2 waveform equality.

Native linear sample interpolation, ADPCM decoder rounding, command/key-on
scheduling, global voice allocation, reverb and final master/BGM mixing
remain outside this proof. In particular, the asset retains the original
reverb flag, but the present output is dry. No replacement loop, note-off,
alternate-area sound or guessed reverb tail was added.

## Validation

Run `make test-area11-sfx-reference test-area11-sfx`. The runtime test uses
the actual locally exported source and checks ten malformed resources,
transactional load failure, absent-cue behavior, scope changes, voice
retirement, stop-all and missing-bank failure under ASan/UBSan. Independent
linear dry-output calculations at 44.1, 48 and 96 kHz agree within `3e-8`;
single-frame and 997-frame callbacks produce identical output bytes.

The full native build also passes. Headless mixer validation exercises the
actual `em_sfx_play` / `em_sfx_mix` path; no audible original-vs-native
capture or SPU2 output comparison is claimed. Generated reports and mixed
fixtures are in `build/area11_sfx_reference/`.

## Relation to the EMSR registry (WP-14)

`docs/SFX_PITCH.md` describes the general registry. It resolves 0x3EF for
(11,0) through the same original path, with the same pitch 862 and
volume words 2217/2217. It resolves 0x3EE as absent. While (11,0) is
selected this bank still takes precedence, because it alone carries the
verified steady envelope. The registry oracle in
`tools/test_area11_sfx_reference.py` extends the dispatch check above to
every exported id AREA11 can play.
