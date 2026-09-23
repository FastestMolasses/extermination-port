# SFX sequencer, voice allocation and SPU2 envelopes (WP-14: AM-03/12/18/26/27, H19 follow-up)

The native SFX path now runs a translation of the original sound driver
instead of refusing scripts it could not play. Registry trigger scripts
are decoded into timed operations. A 48-track, 48-voice driver
(`src/game/em_sfx_bank.c`) runs them one sequencer tick at a time and
drives a 48-voice SPU2 model with ADSR stepping and loop replay. The four
ids that used to be refused are now audible registry entries:

- 0x452 / 0x453: elevator (loops, B0 portamento, key-offs).
- 0x413: AREA11 flame/steam.
- 0x14D: looping tone with a sustained key-off.

The current export has 71 entries: 68 audible, 3 absent and 0 unsupported.

## What is translated (original evidence)

| Original | Native | Evidence checked |
|---|---|---|
| `001152D8` track loop: fetch/running status (`00117088`), VLQ delta (`00118E60`), accumulator `+0x20` against `+0x1C = 0x1E0000/60` | `script_events` in `tools/export_sfx_registry.py` gives operation ticks; `em_sfx_driver_tick` dispatches them in track order | Decomp C plus lockstep |
| `A0 note vel prog` → `00115850` (vel 0 → `001176E0`) | `EM_SFX_OP_KEY_ON` / `EM_SFX_OP_KEY_OFF` | NEARMISS C plus lockstep |
| `B0 41 len depth prog note` → `00118078` (cursor +6) | `EM_SFX_OP_PORTAMENTO` | asm of `00118078`/`00116598` |
| `FF 2F 00` → `00117C28` (SFX track: `+0x34=0`, `+0x42=0`, `+0x3E=1`) | `EM_SFX_OP_END` | Decomp C |
| `00117428` allocator | `em_sfx_driver_allocate` | **asm**, not the C |
| `00116598` per-voice pass: pitch re-send on `+0x44` (portamento) or track `+0x50`, the portamento step (`+0x62` countdown, `+0x60` depth, note clamp 0x0C..0xF3, fine `v % 16`, `track+0x58`), then `(ladder·track+0x44 >> 12)·0x1B9 / 0x1E0` (unsigned); volume re-send on track `+0x52` via `001179E0` | `voice_update` | **asm** |
| `00118EC0` reaper: frees an ended (`+0x3E`), allocated track that no voice `+0x06` names; resets a voice when D_002817C0 < 2, age `+0x1C` ≥ 2, `+0x08 == 1` and kind ≠ 3; ages every voice | tick prologue | **asm** |
| `00119EA0` lowest free track (`+0x2E`, int `+0x30`, `+0x34` clear), `0011A270(0x1000)` → `+0x50`, `0011A218` → `+0x48/+0x4C/+0x52` | `em_sfx_driver_start(_at)`, `em_sfx_driver_request` | **asm** for `+0x42` |
| `0011A070(track \| hard<<15)` | `em_sfx_driver_stop` | **asm** |
| `00119890(1, track)` = 2 while `+0x32` | `em_sfx_driver_status`, `em_sfx_track_status` | Decomp C |
| `001FC3C0` + `001FBDB0` + `001FBD50`, `001FC520` | `em_sfx_service_step/_release`, `em_sfx_loop_service/_release` | **asm** |
| `001FB100` copies D_00281B70 → D_00281C30 (`block_copy(dst, src)`: quadword loads from src, quadword stores to dst) | `em_sfx_frame_snapshot` | `block_copy` words |
| Flush: NON (`0xD`, when changed), EON (`0xC`, when changed), KON (`0xA`), KOFF (`0xB`) | tick epilogue | Decomp C plus IOP driver |

### Where the decomp C is wrong (the lockstep caught or the asm settled)

- **`00119EA0` `+0x42`**: the asm sets the value to 1 and then conditionally clears it to 0 when `handle & 0x8000` is zero. So `+0x42 = 1` only for handles with 0x8000. The C has this inverted.
  - Registry tracks therefore do **not** force the effect send.
  - Only tones with flag 0x80 route to reverb.
  - The earlier statement in `SFX_PITCH.md` that every SFX track sets the effect mask was wrong. It is corrected there.
- **`001176E0`**: the asm collects two masks. One holds matches owned by this track. The other (an OR done in a branch-likely delay slot) holds matches owned by other tracks. A conditional move uses the second only when the first is empty. The C ORs both into the same mask.
- **`00117428` pass 3**: the three minimum trackers start at −1 and compare with `slt` (signed), so no candidate is ever recorded.
  - Only a released kind-1 voice is returned. **Kind-2 SFX voices are never stolen.**
  - With all 44 non-stream voices busy, a key-on gets −1 and is dropped.
  - The retired port policy ("oldest voice stolen at 48") had no original basis.

### Controlled facts from the captures

- Voices 0..3 are stream voices (`+0x00=1`, `+0x1A=3`, `+0x06=0xFFFF`) in every AREA11 capture. The driver reserves them (`SFX_STREAM_VOICES`).
- Free voice records carry `+0x06/+0x22/+0x24/+0x26 = 0xFFFF` and `+0x4E = 0x78`.
- The tick divisor `D_0027F740+0x3A` is 60. This fixes `00118078`'s `(len<<2)·60/60`.
- All D_00281D50 handles are below 0x80, so none carries 0x8000.

## Verification (`make test-area11-sfx-reference`, `make test-area11-sfx`)

**Lockstep oracle.** The original `001FB9F0`, `0011A218`, `0011A070` and `001152D8` run over the captured bank headers. Only `001157F0` (the command sink) and `001191F0` (the IOP DMA) are replaced. The native driver runs beside them, tick by tick, compiled from `em_sfx_bank.c` through a test shim. After every tick, these must be identical:

- every `001157F0` command (the SPU address in command 5 is mapped from the native sample index);
- every track's `+0x32/+0x34/+0x3E/+0x58`;
- 16 fields of every voice record;
- the allocation cursor and serial.

The oracle runs the following passes:

- **Pass A: feedback 0.** All 67 AREA11-playable entries × 5 request pairs.
  - Absent ids return −1.
  - Each exported key-on starts exactly one voice with the exported pitch, volume, SSA and ADSR words.
  - Key-offs occur exactly where a script keys off a sustained tone.
- **Pass B: the SPU2 model's ENVX as the D_002817C0 feedback.** Each of the 66 audible entries runs until every voice has ended and every track is free: 2608 ticks. This includes:
  - 0x452/0x453 portamento sweeps (the pitch is re-sent every tick, including after key-off);
  - 0x14D loop release;
  - 0x413.
- **Pass C: scenarios.**
  - *services*: flame re-triggers, gain updates, soft and hard stops.
  - *exhaustion*: 49 × 0x14D. 48 tracks, 44 voices; the 49th track and a later 0x413 are refused, 52 key-ons find no voice, and starved key-offs fall back to other tracks.
  - *random*: 400 ticks, seeded.

**Direct oracles.**

- `00117428` on 1500 random voice tables, including the cursor.
- `001FC3C0` (with the original `001FBDB0`/`001FBD50`) and `001FC520` on 3000 random cases. Every callee call (`00119890`, `001FBF50`, `0011A218`, `0011A070`, `001FB9F0`), the handle and D_00281B70 must agree. The cadence is the signed `(frame + ordinal) % 10`.
- The **original IOP driver** (`SNDN2DRV.IRX` plus the resident libsd) executes commands 0xC, 0xA and 0xB:
  - Word a addresses core 0 (voices 0..23); word b addresses core 1.
  - EON goes to VMIXEL/VMIXER (0x18C/0x194). KON goes to 0x1A0 and KOFF to 0x1A4, each as two halves per core.
  - A KON and a KOFF of one flush reach the SPU2 in that order, about 73 IOP instructions apart when measured once in the oracle, well inside one 48 kHz sample period (the test asserts the order).

**Loop points** come from the loaded ADPCM block flags. The SPU2 repeats from the last 0x04 block when the end block also carries 0x02. The driver never writes a loop address: no command outside {1,3,5,6,0xA,0xB,0xC,0xD} occurs. Three exported samples loop. All their block flags equal the live SPU RAM. The exporter decodes the replayed body with the ADPCM history carried over the loop end, and requires the third pass to equal the second.

**Runtime.** The runtime test compares native `em_sfx_mix` output with an **independent Python SPU2 model** (ADSR, loops, pitch cursor, Q14 words) driven by the **original** `001152D8` command stream.

- Ids: 0x1A1, 0x14D and 0x452 at 44.1, 48 and 96 kHz, plus one positional request.
- The largest error is 1.07e-7; the tolerance is 2e-7.
- 1- and 997-frame callbacks agree within 1.2e-7 (voice summation order only).

It also covers:

- 28 malformed v2 registries refused transactionally;
- the `001FC3C0` flame cadence through `em_sfx_loop_service` (starts on frames 7/27/47, drops on 17/37/57);
- stop-all silence;
- an UNSUPPORTED refusal fixture.

## Hardware model versus verified

| Part | Status |
|---|---|
| Every command word, its tick, allocation, key-off targets, portamento pitch per tick, reaping and track freeing *given* the ENVX feedback | **Verified** against original execution |
| KON/KOFF/EON register mapping and order | **Verified** (original IOP driver) |
| ADSR stepping (rates, ×4 above 0x6000, exponential decay, 0x8000-sample wait cap, first output sample at ENVX 0) | **Hardware model** from documented SPU2 semantics |
| ENVX feedback timing (sampled at the tick boundary, no IOP latency) | **Hardware model** |
| Linear interpolation, dry output (no reverb), ADPCM decoder rounding | Unchanged boundaries |

The ADSR model's only external check is **PCSX2 consistency** (emulator, not hardware). Every captured AREA11 kind-2 voice's D_002817C0 word equals the model's level within the window its age allows. That is 16 voices in the 6 captures that hold any:

- `80FF/5FD0`: −5 every 0x8000 samples.
- `80FF/5350`: −7 every 256 samples.

The 0x8000 cap is taken from this evidence. The unbounded `1 << (shift−11)` formula would leave the 5FD0 voices at 0x7FFF.

### Consequence for the flame 0x413 (AM-12)

Its script keys the looping tone on and off in the same flush. The original IOP writes KON, then KOFF, within one sample period. Under the documented semantics, key-on starts ENVX at 0 and key-off releases from the current level, so the voice releases from 0 and is **silent**. The reaper resets the voice at its third tick and frees the track at the fourth, and `001FC3C0` restarts it every 20 frames. Whether real hardware lets one sample of attack through (0x3800, then a linear 0x0B release of about 37 ms) depends on where the two register writes fall relative to the 48 kHz sample clock. Without an SPU2 output capture this is **not settled**. The port implements the documented model and does not add a loop.

## Integration still needed (not in this lane's files)

- **Step H.** Call `em_sfx_frame_snapshot()` once per frame where `001FB100` runs. Frame step H is gated with the other `D_00821058 != 1` work; see `ORIGINAL_FRAME_ORDER.md`.
- **Flame owner.** Call `em_sfx_loop_service(&owner_handle, 0x411/0x412/0x413, pos, 100.0f, D_70003B68, D_70003B8A)` from the translated flame owner (`001E3D90` / owner `008235F0`), and `em_sfx_loop_release` from its teardown (`001FC520`).
- **`EM_SFX_TEST` self-test** (`em_game_selftest.c`). It encodes the retired steal policy: 12–15 steals, 63 plays and 0 drops for a 60-play burst. The original refuses the 49th concurrent track (`em_sfx_drops`) and never steals (`em_sfx_steals() == 0`), and 00117428 drops key-ons past 44 busy voices (`em_sfx_voice_refusals`).

## Registry format

EMSR v2: see `docs/SFX_PITCH.md`.
