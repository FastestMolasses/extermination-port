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

## Native binding (chain step AUDIO, 2026-10-08)

The cue plays through the sound driver like every registry id: the
registry's (11,0) entry 0x3EF (SFX_PITCH.md) on a driver track and voice,
and 0x3EE is the registry's absent entry. The decomp's audio capture
panel_power shows the original doing exactly that (one voice, note 33,
program 1, bank handle 4, keyed after the panel's status page closes and
reset 15 ticks later); the port keys it on the same row of that window and
resets it after the same 15 ticks (`tools/level_smoke_audio.py`). The
former separate EMSF bank (`panel_sfx.emsf`) and the slot pool that played
it beside the driver, with its own steady envelope, were a second owner of
001FB9F0's work for one id and are retired: em_sfx_bank_load /
em_sfx_cue_frame, their fixture tests/area11_sfx_test.c and the bank's
render test are deleted.

`tools/export_area11_sfx.py` now writes nothing. It resolves the cue from
the container exactly as above (source()) and asserts that the registry
entry the driver plays has its pitch 862, voice gains 2217 / 2217, ADSR
80FF / 5FD0, note 33 and the same 2,296 decoded samples, and that 0x3EE is
absent in both (`make test-area11-sfx-reference` runs it). The ADSR, the
reverb send (the tone's flag 0x80; the port's output is dry) and the
interpolation are the driver's SPU2 model's (SFX_SEQUENCER.md), with no
output comparison.

## Validation

Run `make test-area11-sfx-reference test-area11-sfx` (SFX_SEQUENCER.md
"Verification") and the level smoke's sound check (LEVEL_SMOKE.md "The
sound state"). No audible original-versus-native capture or SPU2 output
comparison is claimed.
