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

The current first-level export has 277 entries (259 audible, 16 absent, 2
unsupported) and 141 samples: docs/SFX_REGISTRY_FIRST_LEVEL.md.

## Where the driver runs: the game thread, one tick per field (chain step AUDIO, 2026-10-08)

The original's driver is EE code: the game's own calls (001FB9F0 with
00119EA0, 0011A218, 0011A070, 00119890) act on the track and voice tables
at once, and the tick 001152D8 runs on the sound thread 001FB0C0
(priority 2, created by 001F9780), which the vblank handler 001AB140 wakes
at every field before the main loop resumes. Its 001192D0 first waits for
the previous exchange with the IOP, whose reply is D_002817C0; then
001152D8 runs; its end sends the queued commands (001157F0's queue) to the
IOP driver (RPC 0x64), which runs them at its next own tick (every 64
H-lines) and replies with the status block (ENVX & 0x7FFF per voice) its
last tick wrote.

The port does the same (em_sfx.c): `em_sfx_field`, called from
em_stream_live_field at every field, runs the tick on the game thread with
the reaper reading the previous exchange's reply, then the exchange (the
tick's commands, and those the game's stops queued since the last tick,
join the IOP side's ring; the reply is the last status), then the field's
800 or 801 samples at 48 kHz: the commands reach the SPU2 model at the IOP
driver's ticks (every 128 half-lines of the SFX side's own clock, phase 0
at the first field after the registry's load, the New Game's area load:
the IOP timer's phase against the frame is hardware timing, which in the
original follows how long the title ran; a clock of the port's own keeps
every run's sound state the same from New Game on, as the stream
backend's clock, counted from the boot, would not),
each of which snapshots every voice's ENVX into the status. The samples go
into a lock-free ring the audio thread drains (em_sfx_mix), as the stream
backend's do. The earlier port ran the ticks in the audio device's
callback on wall-clock time, so the track a sound got, the voice records
and the reaper's timing followed host timing (the AIM fix round's
finding); now every table follows the game's field count.

**Measured against the original (the decomp's audio captures,
CAPTURES_AUDIO.md; `tools/level_smoke_audio.py`):**
- The reply latency: a voice keyed at tick K shows its ENVX in D_002817C0
  from K + 2 (0 at K and K + 1; battery_ui f192..f194), and the reaper
  resets a hard-stopped voice two ticks after the stop (f190 / f192). Both
  equal the port's exchange model row for row (a reaper reading the SPU2
  model at the tick reset it one tick early).
- The lives: every sound the aligned windows key (the status cues, the
  panel's 0x3EF and 0x4 / 0x0 / 0x6, the elevator's 0x19A and 0x453 with
  its portamento and key-offs, the door's sounds) is reset on the
  capture's tick, except that a sample's end can land one exchange apart:
  the IOP driver's tick drifts against the frame by 13 half-lines a field
  from where each run's IOP timer started, so the two runs' phases differ
  (one of the fence door's two voices lives 106 ticks in the port and 105
  in the capture; a probe that shifted the port's IOP clock by 32, 64 or
  96 half-lines gave 105). Hardware timing, not reproduced
  (PORT_PROFILES.md); the check allows it for a sound that ends unstopped
  and counts it (one in the compared windows).
- **A key-off in its key-on's exchange is lost.** The flame's 0x413 keys
  its looping tone on and off in one tick. In the captures its voice
  (sustain 1, release 1) keeps ENVX 0x7FFF..0x7DD9 for 4,515 ticks and is
  never reset, so its track is never freed and 001FC3C0 keeps re-panning
  the one handle (D_00281B70 holds 0x413 in every row of the beats flame
  and walk_room, and from its start in cage_roof and fence_door). The SPU2
  model follows the measurement: a key-off that reaches a voice before it
  has played a sample since its key-on does nothing (em_sfx_bank.c apply,
  case 0xB). The registry has 13 such scripts (0x411..0x413 in AREA11 and
  AREA01, 0x12E, 0x44E, 0x55B..0x55D, 0x8A9, 0x9B3): ambient loops that play
  until their requester stops them. Only 0x413 is observed: it is the one
  such id in the ten captures' D_00281B70 / D_00281C30 (all ten beats are
  AREA11; 0x44E at the AREA01 arrival is in none of them). The other
  twelve follow the same rule by the same mechanism (a key-off in the
  key-on's own tick), not by their own measurement.
- **The voice's +0x22 is the bank handle** 001FB9F0 reads from
  D_00281D50[group * 20 + bank] (track +0x24, then 00115850), not the
  record's (group, bank) pair: the panel cue's voice holds 4, the
  footsteps' 0, as in the captures (em_sfx_bind_bank_handles).

## What is translated (original evidence)

| Original | Native | Evidence checked |
|---|---|---|
| `001152D8` track loop: fetch/running status (`00117088`), VLQ delta (`00118E60`), accumulator `+0x20` against `+0x1C = 0x1E0000/60` | `script_events` in `tools/export_sfx_registry.py` gives operation ticks; `em_sfx_driver_tick` dispatches them in track order | Decomp C plus lockstep |
| `A0 note vel prog` → `00115850` (vel 0 → `001176E0`) | `EM_SFX_OP_KEY_ON` / `EM_SFX_OP_KEY_OFF` | NEARMISS C plus lockstep |
| `B0 41 len depth prog note` → `00118078` (cursor +6) | `EM_SFX_OP_PORTAMENTO` | asm of `00118078`/`00116598` |
| `FF 2F 00` → `00117C28` (SFX track: `+0x34=0`, `+0x42=0`, `+0x3E=1`) | `EM_SFX_OP_END` | Decomp C |
| `00117428` allocator | `em_sfx_driver_allocate` | **asm**, not the C |
| `00116598` per-voice pass: pitch re-send on `+0x44` (portamento) or track `+0x50`, the portamento step (`+0x62` countdown, `+0x60` depth, note clamp 0x0C..0xF3, fine `v % 16`, `track+0x58`), then `(ladder·track+0x44 >> 12)·0x1B9 / 0x1E0` (unsigned); volume re-send on track `+0x52` via `001179E0` | `voice_update` | **asm** |
| `00118EC0` reaper: frees an ended (`+0x3E`), allocated track that no voice `+0x06` names; resets a voice when D_002817C0 < 2, age `+0x1C` ≥ 2, `+0x08 == 1` and kind ≠ 3; ages every voice (one tick per field: `+0x1C` counts the fields since the voice's key-on or reset) | tick prologue, D_002817C0 = the previous exchange's reply | **asm**; the audio captures |
| `00119EA0` lowest free track (`+0x2E`, int `+0x30`, `+0x34` clear), `0011A270(0x1000)` → `+0x50`, `0011A218` → `+0x48/+0x4C/+0x52` | `em_sfx_driver_start(_at)`, `em_sfx_driver_request` | **asm** for `+0x42` |
| `0011A070(track \| hard<<15)` | `em_sfx_driver_stop` | **asm** |
| `00119890(1, track)` = 2 while `+0x32` | `em_sfx_driver_status`, `em_sfx_track_status` | Decomp C |
| `001FC3C0` + `001FBDB0` + `001FBD50`, `001FC520` | `em_sfx_service_step/_release`, `em_sfx_loop_service/_release` | **asm** |
| `001FB100` copies D_00281B70 → D_00281C30 (`block_copy(dst, src)`: quadword loads from src, quadword stores to dst) | `em_slg_001FB100` at step H over the view `em_sfx_tables` / `em_sfx_set_snapshot` (chain step H7; the stand-in `em_sfx_frame_snapshot` is deleted) | `block_copy` words |
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

**Runtime.** The runtime test compares the native fields (`em_sfx_field`, drained by `em_sfx_mix`) with an **independent Python SPU2 model** (ADSR, loops, pitch cursor, Q14 words, the lost same-exchange key-off) driven by the **original** `001152D8` command stream through the same exchange (the reply of the previous exchange as the reaper's feedback, the commands run at the IOP driver's ticks every 128 half-lines).

- Ids: 0x1A1, 0x14D and 0x452 at 48 kHz (the SPU2's rate; any other device rate mixes nothing and is counted), each render starting at its own IOP phase, plus one positional request.
- The tolerance is 2e-7.
- 1- and 997-frame drains of the mixer ring give the same samples.

It also covers:

- 28 malformed v2 registries refused transactionally;
- the `001FC3C0` flame service through `em_sfx_loop_service` (one start on frame 7, the track held and the loop sounding on every later field, `001FC520` releasing it to silence);
- stop-all: every track free at once, silence from the field after the next exchange;
- an UNSUPPORTED refusal fixture.

**Against the original game** (`tools/level_smoke_audio.py`, LEVEL_SMOKE.md "The sound state"): the port's per-tick sound state against the decomp's ten audio beats, row for row where the smoke aligns a phase with a route stretch the beat repeats.

## Hardware model versus verified

| Part | Status |
|---|---|
| Every command word, its tick, allocation, key-off targets, portamento pitch per tick, reaping and track freeing *given* the ENVX feedback | **Verified** against original execution |
| KON/KOFF/EON register mapping and order | **Verified** (original IOP driver) |
| ADSR stepping (rates, ×4 above 0x6000, exponential decay, 0x8000-sample wait cap, first output sample at ENVX 0) | **Hardware model** from documented SPU2 semantics |
| ENVX feedback timing: the previous exchange's reply, the IOP driver's ENVX snapshot at its tick (every 64 H-lines) | **Measured** against the audio captures (key-on to visible ENVX two ticks; stops and sample ends reset on the capture's tick, a sample's end one tick apart where the IOP tick's phase differs) |
| A key-off in the key-on's own exchange is lost (the voice sounds on) | **Measured** for the flame's 0x413 in the captures; the other 12 such scripts follow by the same mechanism, unobserved |
| Linear interpolation, dry output (no reverb), ADPCM decoder rounding | Unchanged boundaries |

The ADSR model's only external check is **PCSX2 consistency** (emulator, not hardware). Every captured AREA11 kind-2 voice's D_002817C0 word equals the model's level within the window its age allows. That is 16 voices in the 6 captures that hold any:

- `80FF/5FD0`: −5 every 0x8000 samples.
- `80FF/5350`: −7 every 256 samples.

The 0x8000 cap is taken from this evidence. The unbounded `1 << (shift−11)` formula would leave the 5FD0 voices at 0x7FFF.

### Consequence for the flame 0x413 (AM-12)

Its script keys the looping tone on and off in the same flush. The earlier
model applied the documented semantics to both writes (a release from
ENVX 0: silent, the voice reaped at its third tick, `001FC3C0` restarting
it every 20 frames). The audio captures settle it the other way (above):
the key-off is lost, the voice sustains, its track stays held and the
service re-pans the one handle every 10 frames until the player leaves the
radius (its `0011A070` then keys it off for good) or a stop-all ends it.
The port now plays the flame as a continuous positional loop. What the
SPU2 does to the two writes is measured from PCSX2's state (the captures),
not from a hardware output recording.

## Integration still needed (not in this lane's files)

- **Step H.** Done in chain step H7: `em_stream_live_step_h` runs the whole 001FB100 every frame it runs (gated with the other `D_00821058 != 1` work), its copy over `em_sfx_tables` / `em_sfx_set_snapshot`. `em_sfx_submit_001FB9F0` is 001FC6E0's 001FB9F0 with its request words; `em_sfx_voice_record` publishes D_0027CCC0's `+0x00` / `+0x22` for 001195A8 (IOP_STREAM.md "The sound-bank transfer").
- **Flame owner.** Done in chain step A11FIX (em_area11_effect_runtime).
- **The tick on the field clock.** Done in chain step AUDIO (above).

## Registry format

EMSR v2: see `docs/SFX_PITCH.md`.
