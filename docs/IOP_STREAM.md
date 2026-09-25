# IOP stream backend: the IOP side of the music and voice streams (WP-8b)

Status: **live since WP-8b (2026-09-25)**: `em_stream_live` (docs/STREAM_LANES.md "Live binding") owns one
backend and its exported disc; the field runs at the top of every frame and the mixer is summed by em_bgm's callback.
This doc covers the IOP side that the stream lanes (`em_stream_lanes_original`, docs/STREAM_LANES.md) talk to
through 001157F0 and 0011A730; "Binding notes" records how it was bound.

Files:
- `src/game/em_iop_stream.{h,c}`: the backend.
- `tools/export_streams.py`: the local stream exporter. It writes `assets/streams/streams.emst`, which is ignored
  and never committed.
- `tests/iop_stream_test.c`: the native contract test. It needs no assets.
- `tools/test_iop_stream_reference.py`: the original-instruction oracles and the capture comparisons.

Make target: `test-iop-stream` (the contract test, then the reference test).

## What was read

The IOP sound driver is `SNDN2DRV.IRX`. The EE loads it in `sub_cdrom0_IRX_SNDN2DRV_IRX_1`, and it is 0x3330
bytes of unoptimised code. The module was read locally from the user's disc image and located in every captured
IOP RAM image. The oracle checks that its 2,727 non-relocated words equal the disc's. The stream part of the
driver is translated here. Offsets are from the module's load address, which is 0x56330 in every capture.
Nothing of the module is reproduced in this repository.

| Driver code | What it does |
|---|---|
| 0x12C (RPC function 0x64) | Appends the received command bytes to a 0x1000-byte ring at `+0x66C0`, splitting the copy at the ring end. The write count is at `+0x34A0`. The reply is the 0x200-byte status block at `+0x44B0`. 001152D8 calls it once per field, and it lands in `D_002817C0` |
| 0x2C8 / 0x328 (driver thread) | The thread sleeps until the hard timer wakes it. Command 0x1E arms the timer: compare 0x3C00 / 0xF0 = 64 on the H-line source, repeating. On each wake the thread runs 0x328, then 0x20C8, then 0x23B8. 0x328 runs every ring command from the read count to the write count. It then snapshots, per voice, `status[48 + v]` = the voice's cursor and `status[v]` = ENVX & 0x7FFF, then 16 words of the `+0x8680` records |
| 0x634 | The command dispatcher (commands 1..0x4F through a jump table). The stream commands are below |
| 0x20C8 | For voices 47 down to 0 with bit 0 of `+0x00` set, it reads NAX into `+0x24`. NAX in (start, start + half] sets `+0x2C` = 0x1000000 (playing the first SPU half; target the second). NAX past start + half sets 0x2000000 (target the first). A changed `+0x2C` queues a transfer of `half` bytes from IOP `+0x10 + cursor` to the target, then `+0x30` = `+0x2C` |
| 0x23B8 | Runs only while `sceSdVoiceTransStatus(1, 0)` is 1. The finished transfer's voice advances its cursor, `+0x28 = (+0x28 + half * (+0x04 >> 16)) % +0x14`. Then one queue entry runs. 0x1000 is a transfer: copy `0x800 / stride` bytes from every 0x800 into the staging buffer `+0x46B0`, set the flag byte of its first and last ADPCM block, `sceSdVoiceTrans` to SPU RAM, mark it pending. 0x1010 is key-on: `KON` switches, then `+0x00` \|= 1 and `+0x30` = `+0x2C` = 0. 0x1011 is key-off: `+0x00` = cursor = `+0x30` = `+0x2C` = 0, then the `KOFF` switches |
| 0x2CF4 | The transfer queue push: 0x60 entries at `+0x8080`, write/read counts at `+0x3494` / `+0x3498`. A full queue drops the entry |

The stream voice record is `+0x76C0 + v * 0x34` (see `EmIopStreamVoice`).

### The commands the lanes send (their IOP meaning, now read from the driver)

| Cmd | EE sender | Driver effect |
|---|---|---|
| 0x3C | 0011A4B8 (001F9820) | Clears the `+0x8680` records (0x200), the stream voice table (0x9C0) and the transfer queue memory (0x600). The queue counts are not reset |
| 0x3E | 0011A4E8 (001F9820) | One voice's stream configuration, unpacking exactly 0011A4E8's packing: voice = word1 >> 24; `+0x04` = flags & 0xFF0000 (the stride: 0x10000 mono, 0x20000 interleaved L/R); `+0x08` = SPU address; `+0x0C` = the 0xFF00 byte (0x4000: the voice's SPU buffer, two halves of 0x2000); `+0x10` = IOP address; `+0x14` = IOP size. It then sets SSA = the SPU address, ADSR1 = 0x8080 and ADSR2 = 0x808A |
| 0x3D / 0x3F | none / none | 0x3D does nothing. 0x3F clears one voice record. Neither is sent by the lanes |
| 0x40 | 0011A608 | For each masked voice (word1 = voices 0..23, word2 = 24..47): `+0x18` = VOLL = word3 >> 16, `+0x1C` = VOLR = word3 & 0xFFFF |
| 0x41 | 0011A658 | `+0x20` = word3 (a sample rate in Hz). PITCH = (rate << 12) / 48000, so 0xBB80 gives 0x1000 |
| 0x42 | 0011A6A0 | Key-on. Per masked voice it queues a transfer of the first half-size chunk at the IOP buffer start into the SPU buffer start, typed as the second-half target (loop flags 6 ... 2). It then queues the key-on entry |
| 0x43 | 0011A6E8 | Key-off. It queues the key-off entry, which also zeroes the cursor |
| 0x16 | 00119828 (001FBC50, 001FC280, the opening) | Sets the effect return volume of core word1: EVOLL = word2, EVOLR = word3. It is the reverb return. The port models no reverb, so 0x16 is inaudible there; the backend keeps the words |

This settles docs/STREAM_LANES.md's open meanings:
- 0x42 / 0x43 are key-on / key-off, as inferred.
- 0x40 is the voice volume pair.
- 0x41 is the rate: 0xBB80 = 48000 Hz, pitch 0x1000.
- 0x3E's 0x5010 / 0x9010 / 0xD010 / 0x11010 are SPU RAM addresses, and its last word 0x4000 is each voice's SPU buffer size.

### The cursor word (D_00281880, 0011A730)

`D_00281880[v]` is the driver's `+0x28` for voice `v`: the offset in the IOP buffer of the next chunk to transfer
to SPU RAM. Each transfer moves one SPU half, 0x2000 bytes of one channel. On lane 0 that half is 0x4000 bytes of
the L/R-interleaved buffer, so the word steps 0, 0x4000, 0x8000, 0xC000. That is what all captures show. On the
mono voice lanes the word steps by 0x2000. No capture shows a playing voice lane, so the mono step is read from
the driver only. After a key-off the word is 0.

How the pieces meet (lane 0):
1. 001FA790 reads the first IOP half (16 sectors), then keys on.
2. The key-on transfers IOP 0..0x4000 into SPU half A, so the word is 0x4000.
3. As soon as NAX leaves the half's start, IOP 0x4000..0x8000 goes into half B, so the word is 0x8000.
4. The EE sees 0x4000 (half 1) and fills the second IOP half.
5. From then on, every SPU half crossing (14,336 samples) transfers the next chunk.

A word of 0 is ignored by 001F9CF0, so block 0 keeps the previous half.

### Stream data in SPU RAM

A voice's two SPU halves hold de-interleaved channel data. Voice 0 = IOP + 0x400 (the right channel, volume pair
(0, v)); voice 1 = IOP + 0 (the left channel, (v, 0)). The first block's flag byte is 6 (loop start + repeat) in
half A and 2 in half B. The last block's flag byte is 2 in half A and 3 (loop end + repeat, back to half A) in
half B. The stream files carry no flags; the driver writes these four bytes into each staged copy. The oracle
proves this against the SPU2 RAM of all 16 IOP images: SPU2 RAM sits at offset 0x10004 inside PCSX2's SPU2.bin,
derived from the captures.

## EE side (translated)

- **001FA6A0(size)** = 0010F8F8(size + 0x10), rounded up to 16 when not aligned (byte-matched C, oracle-checked).
- **sub_cdrom0_IRX_SNDN2DRV_IRX_1** ends with 001FA6A0(0x28000) into `D_00275B50`, then 0x10000 each into
  `D_00275B28` (mirrored into `D_00275B4C`), `D_00275B24` and `D_00275B20`. `em_iop_stream_boot_buffers` does the
  same. The oracle runs the original function with its IOP/module workers scripted and 0010F8F8 on the heap model.
- **0011A2B0(a0)**, translated from its .s over raw `D_0027CCC0` records:
  - a0 1 scans voices 0..23, a0 0 scans 0..47, a0 2 scans 24..47; any other a0 returns -1.
  - The first record with `+0x00 == 0` and `+0x1A != 3` is claimed (`+0x00 = 1`, `+0x1A = 3`).
  - Otherwise the lowest `+0x0A` among the records with `+0x1A != 3` is taken over: 001157F0(3, **end index**, 0, 0).
  - `D_0027F740+0x28 |= 1 << (end - start)`. This is the scan mask shifted once per visited record, not the
    chosen voice's bit. The C agrees on this.
  - The taken record is cleared (00121A28), then `+0x06/+0x22/+0x24/+0x26 = 0xFFFF`, `+0x4E = 0x78`, `+0x00 = 1`,
    `+0x1A = 3`.
  - **Finding against the decomp (repository not edited):** the NEARMISS C passes (3, 0, 0, 0). The .s leaves the
    loop counter in a1, so the command carries 0x18 or 0x30. The oracle catches the C's version (mutation
    control).
- **sub_O_STREAM_MUSIC_DAT_1** calls 00113280(0), then 00111C28 on `D_0026EBB0` ("\STREAM\MUSIC.DAT;1"), calling
  00113280(0) again before every retry. `D_00282188` = the file's start sector. The same happens for `D_0026EBD0`
  into `D_0028218C`. The port looks the names up in the exported directory. A name that is not exported
  **faults**; the original would retry forever.
- **001157F0**: the EE queue, 255 commands per exchange. When it is full it returns -1 and drops the command; the
  lanes' adapter maps that to 0, as the lanes doc requires.

## Stated models (not translations)

| Model | Value | Evidence / status |
|---|---|---|
| Field | 262.5 H-lines = 525 half-lines | NTSC. `D_00810E90` counts fields: route snapshots show `vsync_counter` == `D_00810E90` |
| Driver tick | every 64 H-lines (128 half-lines), phase 0 at boot | The driver's own timer setup (0x3C00 / 0xF0, H-line source). The tick phase is unknown |
| SPU2 clock | 48000 Hz against 4.5 MHz / 286 H-lines/s: 572/375 samples per half-line (800.8 per field) | 60 Hz / 800 samples per field fails the long captures (mutation control) |
| RPC 0x64 | once per field, at its start: queued commands in, the last tick's snapshot out | 001152D8's per-field `001191F0(0x64, ...)`. The phase inside the field is not captured |
| Transfer completion | issued at a tick, complete from the next tick; SPU RAM written at issue | **Not pinned by any capture**: completing in the same tick also passes every check |
| Key-on | NAX = LSA = SSA, ADPCM history 0, ADSR from the 0x3E words | Standard SPU2 behaviour. Half A's first block also sets LSA (flag 6) |
| NAX readback | block address + 2 + 2 × (sample / 4) | Captured NAX values are block + 2..0xE |
| ADPCM | filter > 4 decodes as 0, shift > 12 as 12, no rounding, s16 clamp | Same as the port's exporter decoder. SPU2 Gaussian interpolation is not modelled (as for SFX) |
| Loop flags | bit 2 sets LSA at block start; bit 0 jumps to LSA; without bit 1 the voice stops with ENVX 0 | SPU2 register semantics |
| ADSR / volume | `em_sfx_envelope_*`, `em_sfx_volume_gain` (docs/SFX_SEQUENCER.md) | Shared with the SFX voices. Captured ENVX of the stream voices is 0x7FFF |
| IOP heap | first free block 0x85B00, 0x100-byte units | All 27 images: `D_00275B50/28/24/20` = 0x85B00 / 0xADC00 / 0xBDD00 / 0xCDE00 |
| Drive | 00113280 → 2; 00112610 accepts a read (fault when one is in flight or a sector is not exported); 00112D18 answers busy for `latency` polls (default 0), then lands the data; 00113478 drops the read. A read the EE abandons without 00113478 (001FABB0 resets `D_00282157` while a read is in flight: the status open during a refill) is finished by the drive: with no latency left it lands by the next 00113280, and one still waiting for polls faults (outside the model) | libcdvd contract as the lanes use it. The drive's real latency is hardware timing: the live route shows it (the opening's prefill and the voiced lines' teardowns, STREAM_LANES.md "Drive latency"); the live binding runs latency 0 |

## Stream exporter

`python3 tools/export_streams.py [--iso DISC.iso | --music M --voice V --music-lsn N --voice-lsn N]` writes one
EMST file (layout in the tool). It contains:
- the pinned ELF's clip rows (68 music, 179 voice);
- the two search names;
- the disc directory paths and start sectors;
- the sectors of the first level's cues.

The cues exported are:
- Music: 29, 54 and 63 (the `D_0026EC60` rows of area 11); 25 (AREA11's 001FAE70 selection); 0x18 (the override
  cue); 0x1B (game over); and 13 (the music right after the AREA11 exit, captured in route 15).
- Voice: 143..151.

The export is 7 extents, 24.3 MB. A read of any other sector faults. The capture checks prove the exported
sectors are what the original held: every lane-0 IOP buffer half and every stream SPU half in the 16 save states
equals them.

## Verification

`python3 tools/test_iop_stream_reference.py`: quick mode runs in about 6 s. `EM_TEST_FULL=1` runs in 35.5 s.
It needs `assets/streams/streams.emst`, the decomp's disc image (for `SNDN2DRV.IRX`), the 11 startup-reference
images and the `build/s87/route` images. On its first run it extracts the IOP and SPU2 RAM of each route save
state, with the decomp's venv (zstd), into `build/iop_stream_reference/states/`, a regenerable cache.
Last runs, 2026-09-23:
- **Driver oracle.**
  - The original module runs in a MIPS interpreter over captured IOP RAM, with libsd and sysclib replaced by
    recorders with identical scripted answers.
  - Case families: commands (every stream command, voices 0/1/2/3/23/24/47, masks on both cores, rates and
    volumes at the sign boundaries), RPC ring copies around the ring end, tick drains plus snapshots, scans at
    every NAX boundary, consumer entries of every type (strides 1/2, halves 0..3, pending voices, status 0/1/2),
    and queue pushes around a full queue and the count wrap.
  - Lockstep: rpc, 0x328, 0x20C8 and 0x23B8 every tick, with volume words, key-off/on and 0x16 injected.
  - Every modelled byte and every libsd call must be equal.
  - Every original store outside the modelled regions must hit the stack.
  - Counts: quick 342 cases (120 of the 195 command cases) + 480 lockstep calls on 3 of the 16 IOP images;
    full 4,656 cases + 10,240 lockstep calls on all 16.
  - Coverage: 1,749 of 1,777 words of the translated ranges execute. The 28 that do not are pinned with reasons:
    the non-0x64 RPC function, which is not translated; a negative-remainder rounding that cannot occur; the
    dispatcher default; the divide traps; and 2 dead words.
- **EE oracle.** 0011A2B0 on the captured table, a zero table, random tables and an all-busy table, with a0 in
  {0, 1, 2, 3, -1}. Also 001FA6A0 over aligned and unaligned 0010F8F8 results, the boot allocations and
  sub_O_STREAM_MUSIC_DAT_1 with 0 and 2 failed lookups. Counts: quick 103, full 333. The captured code of these
  functions equals the ELF in all 27 images.
- **Capture evidence (766 checks, 27 images).**
  - Boot buffers and `D_00282188/8C` in every image.
  - In 16 IOP images: every stream voice's driver configuration (stride, SPU address, SPU size, IOP address, IOP
    size, rate) equals the native driver's after the native 001F9820.
  - The volume pairs: (0, v) / (v, 0) on lane 0, the boot words on the idle voice lanes.
  - The EE's `D_00281880` is the IOP snapshot or one transfer behind it.
  - Every lane-0 IOP half of the settled (state 0) images holds the exported sectors the lane's read state names.
  - Every stream SPU half holds the de-interleaved exported chunk with the model's four flag bytes.
- **Co-simulation (native lanes + backend, field by field, from each captured stream start; 23 lane-0 images).**
  - At the lane service cadence of 1 field, the cursor word matches in 23/23 images, including 140 s into
    cue 25 (+8405 fields).
  - The lane state (state, half, last half, load, read sector and address) matches in 21/23. The other 2 were
    captured mid-read (route 09 and route 12: state 1); their settled read (half and sector once the read lands)
    matches. That makes 46/46 settled across both cadences.
  - At cadence 2 the cursor matches 22/23. The miss is route 14 at +56 fields, which sits on a transfer boundary.
  - The IOP-side status snapshot matches for 31 of the 32 stream voices of the 16 route images. The miss is route
    14's voice 0: that image was captured between the two voices' transfers.
  - Voice lanes (routes 10..14: cues 147, 148, 150, 149, played to their timer end): the final record matches
    4/4.
  - The opening's stream request (cue 63 with the `D_008106F4` = 2 hold) matches `opening_ee` at +270, the
    `handoff_ee` state (state 1, load 2, the field before cue 25's key-on, with a stale nonzero cursor word) and
    `playable_ee` at +119.
  - The status page: open (001FABB0) leaves every lane idle and every status word 0, as in the status-hub,
    panel and panel-root images. Close (001FAE70(1)) reproduces the panel-animation and route-01 images 12
    fields after the resume.
- **Continuity.** The voices' played samples equal an independent decode of the exported cue 25 for 700,000
  samples in quick mode (12 buffer passes). In full mode it is 3,000,000 samples, a full loop and its wrap.
- **Mutation controls** (each applied to the module, then reverted). The reference test caught:
  - the first-half flag bytes;
  - no de-interleave;
  - no stride in the cursor advance;
  - `<=` at the scan boundary;
  - the NEARMISS 0011A2B0 command;
  - 800 samples per field;
  - one driver tick per field;
  - the status copied after the field's ticks;
  - loop-end ignored by the SPU;
  - pitch shift 11;
  - key-on clearing the cursor;
  - the ring-split source offset;
  - an ENVX mask of 0xFFFF;
  - the queue full at > 0x60;
  - a 0x10 heap unit;
  - key-off keeping the cursor;
  - the scan order 0..47.

  One mutation survives both tests: a transfer completing in the same tick. That timing is a stated model.

`tests/iop_stream_test.c`:
- heap blocks;
- 0011A2B0's claim, take-over and end-index command;
- the directory and its fault;
- the command decode;
- reader latency and faults;
- a synthetic stereo cue through 001F9820 / 001FA790 / 001F9CF0 for 900 fields. It checks the cursor order
  0x4000 → 0x8000 → 0xC000 → 0x4000, and that both voices' samples play every channel block in order over 5
  buffer passes and 5 loops;
- key-off to 0;
- the mixer's rate and overrun counters.

## Boundaries

- Not translated: the driver's SFX commands (1, 3, 5, 6, 0xA..0xD, ...), which go to the forward sink; its PCM
  input path (0x46..0x4F, the `+0x8680` records); and the non-0x64 RPC function.
- Not modelled: SPU2 reverb, which makes 0x16 inaudible; Gaussian interpolation; core master volumes; the drive's
  latency; the SIF DMA's timing inside a field; and the driver tick's phase against the field.
- One EE queue is shared with SFX in the original (255 commands per field). In the port the SFX driver has its
  own path, so that shared limit is not modelled.
- 001FB100 (step H, which calls 001F9CF0) is not bound here (docs/STREAM_LANES.md "Still missing" 4).

## Binding notes (for the coordinator chain)

**Bound in WP-8b (2026-09-25)** exactly as listed below, by `em_stream_live` (docs/STREAM_LANES.md "Live binding").
Pacing: `em_frame_step` paces fields at 59.94 Hz (`frame_pace_ntsc`); with `EM_UNCAPPED=1` (the tests) the ring
overruns are counted and dropped, and the tests do not listen. The mixer hook is in em_bgm's callback (em_bgm is
now only the device and the mixer). The forward sink stays unset: a command outside the stream set faults (none is
sent from a fresh voice table). Step 7.7's 001FB100 is bound only as its 001F9CF0 call (see STREAM_LANES.md).

**Clock domains (binding requirement).** The game thread renders exactly 800.8
samples per `em_iop_stream_field` into a 16384-frame ring, and the audio
device drains it at its own 48 kHz clock. The port's field pacing must
therefore run at the original's field rate (59.94 Hz NTSC); at any other rate
the ring will over- or underrun (`em_iop_stream_mix` counts both). Binding must
state the pacing it uses.

Storage: one `EmIopStream` (`em_iop_stream_create`) and one `EmIopStreamDisc` (`em_iop_stream_disc_load` of
`assets/streams/streams.emst`). If the file is missing, the boot must fail-stop, not play silence.

Wiring:
1. **IRX bring-up (001AAE40's start-up, where `sub_cdrom0_IRX_SNDN2DRV_IRX_1` runs).**
   - Call `em_iop_stream_boot_buffers` and store `out[0..4]` as `D_00275B50/28/4C/24/20`.
   - Set the lanes' globals `d275B28/24/20` from them.
   - Then `em_iop_stream_sub_O_STREAM_MUSIC_DAT_1` into the lanes' `music_sector` / `voice_sector`.
2. **Voice table.** 0011A2B0 works on raw `D_0027CCC0` records plus `D_0027F740+0x28`
   (`em_iop_stream_set_voice_table`).
   - The SFX driver (`em_sfx_bank`, parsed `EmSfxVoice` fields) owns that table. Today's stand-in is
     `em_sfx_driver_init(stream_voices = 0xF)`.
   - On a fresh table the oracle-checked 0011A2B0 returns 0, 1, 2, 3 and marks them kind 3, so the stand-in's
     mask equals it. Binding must keep one storage: either the SFX driver exposes its records as the raw view,
     or the binder asserts the four voices 001F9820 got equal the SFX driver's stream mask.
3. **Lanes.**
   - Bind with `em_iop_stream_lanes_data(disc)`, `globals.d281880 = em_iop_stream_ee_status(s) + 48` and
     `em_iop_stream_lane_workers(s, &w)`.
   - The worker ctx object must hold the `EmIopStream *` as its first member.
   - Seed the lanes with `em_stream_lanes_001F9820` at 001AAE40's start-up. Its 0x3C/0x3E/0x40/0x41 commands
     configure the driver at the next field.
4. **Time.** Call `em_iop_stream_field(s)` once per field, where `D_00810E90` advances, before that frame's step
   H. The co-simulation shows one call per field with the lane service each field reproduces the captures.
5. **Mixer hook (em_bgm is a live file; not edited here).** Add `em_iop_stream_mix(stream, out, frames,
   device_rate)` to em_bgm's render callback next to `em_sfx_mix` (em_bgm.c line ~158). The stream needs the
   device at 48000 Hz (`em_bgm_device_ensure(48000)`; other rates add nothing and are counted).
6. **Forward sink.** `em_iop_stream_set_forward` to the SFX owner. The only non-stream command the first level
   can produce here is 0011A2B0's take-over command 3. It is never sent from a fresh boot table.
7. **Replace the stand-ins in one change set, after 1..6 are live** (so lane 0 never has two owners):
   1. `em_opening_media` (the lane-0 stand-in: `D_008106F4` 2 → 1 on its first step H, sound at 0) →
      001FA790 / 001F9CF0 with the real hold protocol. The opening scenario verifies it against `opening_ee`,
      `handoff_ee` and `playable_ee`.
   2. em_bgm's cue-25 resume in `w_001FAE70` → `em_stream_lanes_001FAE70`. em_bgm keeps only the device and SFX
      mixing: `em_bgm_play*` no longer carries streams.
   3. `w_001FABB0` (the stand-in: stops em_bgm and the opening stream, clears the holds) →
      `em_stream_lanes_001FABB0` (ring reset, per-lane key-off, `D_00282157` = 0). The status page scenario
      verifies it.
   4. `w_00119828` (the reported no-effect binding) → `em_stream_lanes_00119828` → 0x16. It stays inaudible
      without reverb, but the EVOL words are kept.
   5. The 001B0C00 fades (UM_001FAD70) → `em_stream_lanes_001FAD70(0/1/2, p, 1)` after 001AEDE0(p, 0).
   6. The game over's 001FA790(0, 0x1B) / 001FAB50 (UM_001FA790 / UM_001FAB50) → the lanes.
   7. Step H: bind 001FB100 (`em_slg_001FB100`) so 001F9CF0 runs when `D_00821058 != 1`.
   8. Voiced lines additionally need `voice_push` (001FA5A0) bound (WP-9 / WP-10). The voice cues 143..151 are
      exported.
8. **Assets.** Add `tools/export_streams.py` to the asset pipeline next to the other exporters.
