# IOP stream backend: the IOP side of the music and voice streams (WP-8b)

Status: **live since WP-8b (2026-09-25)**: `em_stream_live` (docs/STREAM_LANES.md "Live binding") owns one
backend and its exported disc; the field runs at the top of every frame and the mixer is summed by em_bgm's callback.
This doc covers the IOP side that the stream lanes (`em_stream_lanes_original`, docs/STREAM_LANES.md) talk to
through 001157F0 and 0011A730; "Binding notes" records how it was bound. Since chain step H7 (2026-09-29) it also
carries the area load's sound-bank upload: command 0x20, channel 0's transfer callback, the IOP heap's first fit and
free, and the EE kernel's SIF DMA (section "The sound-bank transfer"), which the EE sound library
(`em_ee_sound_lib`) and the bound 001FB370 (`em_sound_bank`) run over.

Files:
- `src/game/em_iop_stream.{h,c}`: the backend.
- `src/em_settings.{h,c}`: the PS2 disc-drive timing switch (section "Host speed and the PS2 disc-drive timing
  switch"), which `em_stream_live` applies at the boot.
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
| 0xE98 (command 0x20) | A registered bank's upload: `sceSdVoiceTrans(0, write, IOP source, SPU destination, size)` with the source `(word1 & 0xFF) << 16 \| word2 >> 16`, the destination `(word2 & 0xFFFF) << 8 \| word3 >> 24` and the size `word3 & 0xFFFFFF` (00119400's packing), then `+0x349C` = `word1 >> 8`, the EE's command count |
| 0x544 | Channel 0's transfer callback (command 0x1E registers it with `sceSdSetTransCallback(0, 0x544)`): status word 112 (`+0x44B0 + 0x1C0`) = `+0x349C`; returns 1. The EE sees it as `D_002817C0 + 0x1C0` after the next exchange, 00119450's acknowledgement |

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
| Driver tick | every 64 H-lines (128 half-lines), phase 0 at boot | The driver's own timer setup (0x3C00 / 0xF0, H-line source). The tick phase is unknown. Since chain step AUDIO the SFX side (em_sfx_field) runs the same exchange for the SFX commands, on a tick grid of its own whose phase is 0 at the New Game's area load (this backend's counts from the boot, so its phase follows the title's dwell; SFX_SEQUENCER.md "Where the driver runs"); the audio captures confirm the exchange's two-tick ENVX latency, and a sample's end can land one tick apart where the phases differ |
| SPU2 clock | 48000 Hz against 4.5 MHz / 286 H-lines/s: 572/375 samples per half-line (800.8 per field) | 60 Hz / 800 samples per field fails the long captures (mutation control) |
| RPC 0x64 | once per field, at its start: queued commands in, the last tick's snapshot out | 001152D8's per-field `001191F0(0x64, ...)`. The phase inside the field is not captured |
| Transfer completion | issued at a tick, complete from the next tick; SPU RAM written at issue. A channel-0 transfer (command 0x20) runs its callback 0x544 at the start of the next tick | Channel 1: **not pinned by any capture** (completing in the same tick also passes every check). Channel 0: the count reaches the EE at the exchange after the field that ran the command, which route 00's `D_00275B18` = 0x4E pins (the sound-bank test's mutants: one field earlier leaves 0x4F, one later 0x4D); completing in the same tick gives the same exchange |
| Key-on | NAX = LSA = SSA, ADPCM history 0, ADSR from the 0x3E words | Standard SPU2 behaviour. Half A's first block also sets LSA (flag 6) |
| NAX readback | block address + 2 + 2 × (sample / 4) | Captured NAX values are block + 2..0xE |
| ADPCM | filter > 4 decodes as 0, shift > 12 as 12, no rounding, s16 clamp | Same as the port's exporter decoder. SPU2 Gaussian interpolation is not modelled (as for SFX) |
| Loop flags | bit 2 sets LSA at block start; bit 0 jumps to LSA; without bit 1 the voice stops with ENVX 0 | SPU2 register semantics |
| ADSR / volume | `em_sfx_envelope_*`, `em_sfx_volume_gain` (docs/SFX_SEQUENCER.md) | Shared with the SFX voices. Captured ENVX of the stream voices is 0x7FFF |
| IOP heap | first free block 0x85B00, 0x100-byte units, first fit; after the boot buffers the IOP holds 0x900 bytes (0xDDF00..0xDE800), a **measured occupancy whose owner is not established**: only its end, 0xDE800, is pinned by the captures, and 0x900 is the size that puts the next first fit there. The port allocates it once at start-up (em_iop_stream_boot_driver), before the first bank upload. Hypothesis only: the block's top in the title capture's IOP RAM holds IOP return addresses, like a thread stack (perhaps the driver thread command 0x1E creates). Follow-up for the lead: confirm the owner from the IRX's thread creation (its stack size and the allocation order); 0010F968 frees a block | All 27 images: `D_00275B50/28/24/20` = 0x85B00 / 0xADC00 / 0xBDD00 / 0xCDE00; the title capture and every route capture: `D_00282194` = 0xDE800 (the bank uploads' block, allocated and freed each time: first fit), and route 00's IOP RAM there holds the AREA11 bank |
| SIF DMA | `sceSifSetDma` of one descriptor copies the EE bytes into IOP RAM when queued; `sceSifDmaStat` answers done at its first query; the id is the port's own count | Host speed (the PS2's transfer time is hardware timing, port CLAUDE.md 2026-09-27). The kernel's ids (`D_002821A0` = 0x530D1702 in the title capture) are not modelled; 001FB910 only tests the id against 0 |
| Drive | By default host speed: a read is done at the first query after its issue. With the PS2 disc-drive timing switch on: one read at a time; a seek of 0, 2 or 6 fields by the signed distance from the drive's position, then the read (16 sectors at most) within one field; 00113280 answers 6 until the read in flight is done, also one the EE abandoned. In both modes 00113478 drops the read | Host speed: the Original profile's policy (the code is the oracle; PS2 hardware timing is not reproduced). The model: measured in the original (the C7 stream capture), section "Drive model" below |

## Host speed and the PS2 disc-drive timing switch

The reader has two timings (user decision 2026-09-27, `LAUNCHER_OPTIONS.md` "PS2 disc-drive timing", built the
same day). The switch is `EmSettings.ps2_disc_drive_timing` (`src/em_settings.h`), 0 in the Original profile, read
at launch from `EM_PS2_DISC_DRIVE_TIMING=1` until the launcher sets it; `em_stream_live_boot` applies it with
`em_iop_stream_set_ps2_drive_timing`.

- **Off (the default): host speed.** The exported sectors are in memory, so a read is done as soon as it is issued:
  the first 00112D18 / 00113280 query after the 00112610 call finds it done and lands its sectors, whatever the
  distance. An abandoned read lands at the next ready query; a break before any query drops it. Everything the
  game's code does around the drive is unchanged: 001FA0D0 takes one step per field (its ready query, then the
  issue, then the poll in the next field), and the hold before a key-on is the lanes' own. In the main-loop-top
  rows a host-speed read shows one row of ready query (phase 1) and one row in flight (phase 2).
- **On: the drive model** (next section).
- Both modes keep the drive's position and classify every read's distance in the counters
  (`EmIopDriveStats.by_fields`, what the PS2 drive would have taken; `host_speed` counts the reads served at host
  speed). The level smoke prints them with the mode (`stream drive: host speed: ...` or `stream drive: PS2
  disc-drive timing on: ...`).

**What host speed gives the live route** (measured 2026-09-27, `LEVEL_SMOKE.md` "The stream drive's two modes"):
- The voiced lines: each voice read takes 1 field against the capture's 7, and the key-on follows 2 fields later as
  in the capture. 0x97 and 0x99 key on and tear down exactly 6 rows early. 0x7F is 8 rows early: the same 6, plus
  the 2 fields the original's sequencer spent on a lane-0 music refill first (navigation, below).
- The opening: the stream request reaches its key-on 7 fields after the request frame. That is 1 frame to the
  sequencer's first phase-1 row, 1 field of ready query (the area music's read the opening abandoned has landed),
  1 field of read, and the 4-field hold. The original takes 27, of which 21 are the drive's (15 extra fields of
  ready query while the area music's read finished, and 6 extra fields of read).
- First control comes exactly those 21 frames before the original's (AE+1303 against AE+1324 in the C7 newgame
  capture); the switch on gives AE+1313. newgame-control: locked_ticks 1301 (1311 with the switch on), the 30-tick
  displacement unchanged at 9.599849. The frame-order post-control window is at native index 1393 (counter 2650,
  first control + 11) since chain step H7 (1330 before; the New Game's loads take ticks); 1665 with the switch on.

## Drive model (measured, 2026-09-27)

The model runs only with the PS2 disc-drive timing switch on (previous section). The timing of the drive behind 00113280 / 00112610 / 00112D18 / 00113478, from the decomp's C7 stream capture
(`docs/CAPTURES_C7.md` section 1, `build/s87/c7cap/stream/`). The capture holds the lanes' bytes at every main-loop
top of four stretches (the New Game opening's stream request; routes 10, 11 and 13 around the voiced lines 0x7F,
0x97 and 0x99). In four windows it also holds every vsync ISR sample of the IOP's CDVD registers. It has 209
00112610 reads, 205 of them after another read of the same stretch.

What the capture shows, and the model:
- **One read at a time.** 00113280(1) reports ready (2) only when the drive has finished its read. A read the EE
  abandoned also counts: at the opening, 001FABB0 in n2 left the area music's read running, and the new read
  waited until it was done in n18. While a read is in flight the model answers 6, the value the decomp's
  00113280 returns when the drive is not ready (the lanes test only for 2). This replaces the old rule "an
  abandoned read lands at the next query, or faults".
- **Position.** The CDVD position registers hold the sector after the last read. The reference test checks this
  against 110 idle samples. A read begins with a seek: status 0x12, with the position held. Its length depends on
  the signed distance d = sector - position.
- **Read time.** After the seek, a read of up to 16 sectors (the lanes' largest) is done within one field. The
  model faults on a longer read. The panel prompt's 161-sector module read took 7 fields of reading (H7), which
  the lanes never issue.
- **Seek fields per distance.** "Busy polls" are the 00112D18 polls that answer 1. A read with s seek fields
  completes at the (s + 1)-th poll after its issue frame.

  | d | reads | busy polls | model |
  |---|---:|---|---:|
  | 0 (contiguous) | 119 | all 0 | 0 |
  | +14 (read-through ahead) | 1 | 0 | 0 |
  | -1604..-23 and +29..+194 | 12 | 11 × 2, 1 × 1 | 2 |
  | -89445..-88337 and +72124..+89372 | 73 | 55 × 6, 18 × 7 | 6 |

  The fast and the full seek were each measured in both directions, so the model applies each over the |d|
  range measured in either direction (23..1604 and 72124..89445).

  The one-field spreads follow the sub-field phase at which the EE polls. No capture records that phase, so the
  model takes each class's majority. The four reads the first level's timing rests on all took exactly 6: the
  opening's cue 0x3F prefill (d = 72124) and each voiced line's first voice read (d = 89272, 88363, 89372).
- **Unmeasured distances.** A distance outside every measured range takes the class of the nearest measured
  distance. `stats.unmeasured` counts these reads, and the level smoke reports them. On the route they are
  Roger's music cue 29 read and the cue 25 resume after it (d = +3026 and -4394, served as fast seeks): 2 of the
  full route's 419 reads. The Roger phase equals route 14 row for row with either class (both were tried), so the
  route capture does not pin them. A capture of route 14's stream would.
- **No position.** The port's first read has no position: the port does not model the boot's or the movie's
  reads. A read after a break also has none. Both are served as a full seek. In the first level the first read
  is the area music's (cue 25), which 0x1AE040's area-entry 001FAE70(1) issues (bound since the rand() order
  audit, RAND_ORDER.md section 2). 00113478 is never reached on the route (0 breaks).
- **Outside the model: the first lane read after a module-loader read** (measured 2026-10-09, the music-lead
  step; it replaces the earlier reading "from the intro movie's position"). In all three captured cases, the lane
  read that follows a read of the screen-module loader (00112440, MODULE_LOADER.md 1.7) seeks for 16 or 17
  fields, at distances whose lane-to-lane reads take 6 or 7:
  - The opening's area-music read (cue 25, row 1, sector 720927) sought for 16 fields from d = +131414. The C7
    capture's CDVD position bytes at its issue (f0/f1) decode, as BCD minute / second / frame with the minute
    byte truncated (the decode under which all 55 idle main-loop-top samples of the four C7 stretches equal the
    end of the 00112610 read before them), to sector 589513:
    the end of the New Game's last loader read, the AREA11 resident region (DATA.DAT sector 0xF015, 3292
    sectors). The head was not at the intro movie's position.
  - The music's resume after each status page (cue 25 again, the same sector): the decomp's audio captures
    battery_ui (route 01's ITEM page, f485..f505) and panel_power (route 03's BATTERY page, f527..f547) both
    show the ready query in the request frame, 18 rows of read in flight (17 seek fields) and the key-on 20
    rows after the request. The page's module 0x21 load is the drive's last read before it (its chunk ends at
    sector 642033, so d = +78894, inside the measured full-seek range). The audio captures hold no CDVD
    registers; the position is the loader's last read, not a sample.
  - The resume after Roger's cue 29 (no loader read between) keys on 5 rows after the request in
    roger_encounter (f1758..f1763), as the switch's fast seek gives.

  Whether that is PCSX2's drive state after a non-stream read or something else is not visible from the game
  side. The switch's model leaves it out: the IOP drive's head never moves for the loader's reads (the loader
  has its own measured drive), so with the switch on the opening's read is served as a first read (a full seek
  of 6) and each page resume as a fast seek of 2 from the music's last refill. That gives 10 frames of the
  switch's 11-frame opening lead (the original's 15 fields of waiting against the port's 5; FIRST_LEVEL_AUDIT.md
  1b item 4) and a music resume 15 fields early after every status page
  (the fork demo's "recurring loud sound 14 ticks early": the music's percussive hit, every 131 / 169 ticks;
  1b item 8). At host speed the resume is 17 fields early (one row of ready query, one in flight, the key-on
  three rows after the request). Disc timing is not part of the Original profile (CLAUDE.md, 2026-09-27);
  modelling the rule under the switch (16 or 17 seek fields for the first lane read after a loader read) is a
  lead / user decision (LAUNCHER_OPTIONS.md).

**What it gives the live route with the switch on.**
- The voiced lines' voice reads take the capture's 7 fields, and each key-on follows 2 fields later, as in the
  capture. 0x97 and 0x99 now tear down on the capture's rows (they were 6 rows early).
- 0x7F is 2 rows early. The original's read sequencer 001FA0D0 served a lane-0 music refill first: its read
  started in the voice's first frame, in f1164. That refill falls where it does because of the music's phase:
  1. The music was keyed on 3583 fields before the voice in the original (vsync 15472, after route 03's status
     close) and 3449 in the port. The difference is navigation.
  2. The level smoke's check_voice_drive allows exactly the fields each side's sequencer spent on lane 0 first.
- The opening's stream request reaches its key-on 17 fields after the request frame (12 before the area-entry
  001FAE70(1) was bound, 6 before the drive model).
  - The port's 17 fields are 1 frame to the read's issue request, 5 fields waiting for the area music's read in
    flight (the opening's 001FABB0 stopped the lane one frame after the area entry and left the read running, as
    in the original), then the capture's 7 fields for the read and 4 for the hold.
  - The original takes 27 fields: the same 12, plus 15 fields waiting for the area music's read, whose seek
    after the New Game's loader reads took 16 fields (above).
  - newgame-control reaches first control at locked_ticks 1311 (1301 at host speed).
  - The 30-tick displacement is unchanged at 9.599849.

## The sound-bank transfer (chain step H7, 2026-09-29)

The area load's first bank step, 001FF590(0xAB, 0), hands the area's sound bank to 001FB370 (MODULE_LOADER.md
1.9). Its steps (001FB3E0, one state per call, and 001FB910) run on this backend through `em_sound_bank`
(`src/game/em_sound_bank.{h,c}`, owned by `em_stream_live` next to the backend):

| Original | Port |
|---|---|
| 00119400, 001193A8, 00119528, 001194B8, 00119450, 001195A8, 001199F0 | `em_ee_sound_lib` (translations, see below) over `em_sound_bank`'s `D_0027C6C0` (the bank handles), `D_002819C0` (the handles' pending volumes) and `D_0027F740 + 0x48` (the transfer commands queued) |
| 001157F0 | this backend's EE queue (shared with the stream lanes, as the original's one queue is) |
| `D_002817C0 + 0x1C0` | this backend's EE status copy, word 112 |
| 0010F8F8 / 0010F968 | the heap model (`em_iop_stream_0010F8F8` / `_0010F968`) |
| 0010BC00 (sceSifSetDChain), 0010BAA0 (FlushCache) | no host effect (the SIF0 receive chain and the EE caches are not host state) |
| 0010BBE0 / 0010BBC0 (sceSifSetDma / sceSifDmaStat) | `em_iop_stream_sif_set_dma` / `_sif_dma_stat` from the bank file's bytes as the loader delivered them |
| `D_0027CCC0` (001195A8's scan: `+0x00`, `+0x22`) | `em_sfx_voice_record`: the SFX driver's records as its audio thread last published them |

The EE sound library functions, each read from the original's instructions (the decomp's C for 00119450, 001194B8,
00119528, 001193A8 is byte-matched ee-gcc; 00119400 and 001199F0 are mwcc; 001195A8 is undecompiled):

- **00119400(a0, a1, a2, a3)**: `D_0027F740 + 0x48` += 1, then (a tail call) 001157F0(a0, count << 8 | (a1 >> 16 &
  0xFF), a1 << 16 | (a2 >> 8 & 0xFFFF), a2 << 24 | (a3 & 0xFFFFFF)). **001193A8(a0, a1, a2)** = 00119400(0x20, a0, a1,
  a2), returns 0.
- **00119528(header, spu)**: -1 unless the word at `header + 0x0C` is "SShd"; else the first of entries 0..0x7E whose
  `+0` is 0 becomes {1, header, spu >> 3} and its index is returned (-1 when all are taken; entry 0x7F is never taken).
- **001194B8(iop, header, spu)**: 00119528, then, for a handle, 001193A8(iop, spu, the header's word +4).
- **00119450(a0)**: a0 0: `D_002817C0 + 0x1C0 == D_0027F740 + 0x48`; a0 1 spins until equal (not reached: the port
  faults); other: -1.
- **001195A8(h)**: -1 unless h < 0x80 and the entry's `+0` is 1; -1 while a `D_0027CCC0` voice with `+0x00` 1 has
  `+0x22` h; otherwise the entry is cleared and it returns 0.
- **001199F0(h, v)**: -1 unless h < 0x80 and 0 <= v < 0x80; else returns the signed byte `D_002819C0[2h]` and stores
  v there and 1 at `[2h + 1]`. Its consumer, the SFX sequencer's 00116DB8 (the bank's program volume), is not
  translated in the port (the SFX driver, section 4 of the census: item 1 of FIRST_LEVEL_AUDIT 1b).

**The state before the New Game load.** The boot's and the title's bank uploads (001AB7E0 step 1's common bank,
handles 0..2 in bucket 1; the title module's bank, handle 3 in bucket 3) do not run in the port. Their result is
seeded from the title capture (startup-reference slot 01): `EM_SOUND_BANK_SEEDS` in em_sound_bank.c, the transfer
count 5 (with the backend's `+0x349C`, status word 112 and its EE copy). They are addresses and counters, not disc data;
the test checks each against the capture.

**What the AREA11 bank does** (INDEX.IDX sector 0x0F entry 0; one record, bucket 2): call 1 reads the header (the
record table, the payload offset 0xD60), call 2 selects bucket 2 (`D_00282198` = its SPU base 0x1A0000 from
`D_00264890`) and allocates 0x492E0 bytes of IOP heap (0xDE800), call 3 sends the 0x492D0 bytes of samples by SIF
DMA, call 4 finds the previous upload acknowledged, call 5 registers the SShd header as handle 4 (command 0x20
queued with the count 6), calls 6 and 7 wait for the acknowledgement (it arrives at the exchange two fields after
the call that queued it), call 8 sets the handle's volume (001199F0(4, 0x64)), frees the heap block and returns
the bank's end 0x1335F40 (`D_0028A73C`). At host speed that is 8 of the loader's dispatches.

The PS2 took 9 (the New Game capture holds 001FF590's sub-state 5 over nine frames): the one more is most likely
the SIF DMA of 0x492D0 bytes, not done at 001FB910's first `sceSifDmaStat` in the same call (the wait before the
acknowledgement is pinned by route 00's `D_00275B18`, so the extra call is not there). That is DMA hardware time,
which neither the Original profile nor the PS2 disc-drive timing switch reproduces (LAUNCHER_OPTIONS.md).

**Verification.** `make test-sound-bank-reference` (`tools/test_sound_bank_reference.py`, about 3 s):
- the seven library functions executed over the title RAM (001157F0 and 00121A28 as original code) on the
  argument edges, full and random handle tables, random voice records, SShd and non-SShd headers and a full queue:
  140 cases (680 with `EM_TEST_FULL=1`), every byte of the three tables, the return value and every queued
  command equal;
- the ORIGINAL 001FB370 chain (001FB3E0, 001FB910, the library, 001157F0) over the title RAM with the AREA11 bank at
  0x13351C0, one call per frame, against `em_sound_bank` on this backend: every modelled global, the result, the
  queued commands after each of the 8 calls equal. Only the kernel and RPC leaves are hooked, answered by an
  independent model of the stated rules, and the acknowledgement comes from an independent model of the exchange;
- the seeds equal the title capture; after the chain every modelled global equals route capture 00 (except the
  kernel's DMA id and `D_002819C0`, which the SFX sequencer consumes before that capture); the SPU RAM at 0x1A0000
  and the IOP RAM at 0xDE800 equal route 00's SPU2 and IOP RAM there (0x492D0 bytes each);
- the exchange's timing is pinned: its mutants (the acknowledgement one field earlier or later) end with a
  `D_00275B18` the capture refutes.
The driver oracle (`test_iop_stream_reference.py`) executes command 0x20 and 0x544 from the user's IRX (the IOP data
compared by content, `+0x349C` and status word 112 by value): the translated ranges are now 1,845 words, of which
1,817 execute, the same 28 pinned as before. `tests/iop_stream_test.c` pins the contract: first fit and free, the
start-up block, the SIF DMA and its faults, command 0x20 on the SPU2 model with the callback one tick later and the
count at the exchange after.

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
- Voice: 143..151; and 1 (since chain step BRANCHES, 2026-10-03: line 0x13, Roger's talk after the encounter,
  its area-11 message record's voice word; the BRANCH recording br_14 plays it).

The export is 8 extents, 24.6 MB (7 extents, 24.3 MB before cue 1). A read of any other sector faults. The capture checks prove the exported
sectors are what the original held: every lane-0 IOP buffer half and every stream SPU half in the 16 save states
equals them.

## Verification

`python3 tools/test_iop_stream_reference.py`: quick mode runs in about 6 s. `EM_TEST_FULL=1` runs in 35.5 s.
It needs `assets/streams/streams.emst`, the decomp's disc image (for `SNDN2DRV.IRX`), the 11 startup-reference
images and the `build/s87/route` images. On its first run it extracts the IOP and SPU2 RAM of each route save
state, with the decomp's venv (zstd), into `build/iop_stream_reference/states/`, a regenerable cache.
Last runs, 2026-09-23 (the co-simulation, 2026-09-27 with the drive model):
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
  It compares the PS2's own timing, so it runs with the PS2 disc-drive timing switch on.
  - At the lane service cadence of 1 field, the cursor word matches in 23/23 images, including 140 s into
    cue 25 (+8405 fields).
  - The lane state (state, half, last half, load, read sector and address) matches in 21/23. The other 2 were
    captured mid-read (route 09 and route 12: state 1); their settled read (half and sector once the read lands)
    matches. That makes 46/46 settled across both cadences.
  - At cadence 2 the cursor matches 23/23 with the drive model. The zero-latency drive missed route 14 at +56
    fields, which sits on a transfer boundary. The lane state at cadence 2 matches 20/23, as before.
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
- **Drive model** (section "Drive model"). It covers every read of the C7 stream capture:
  - 205 reads: 186 equal to the model and 19 one field off, the sub-field phase;
  - each class matches the majority of its reads, and the four key reads match exactly;
  - the position register equals the last read's end in 110 idle samples.
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
- the switch: host speed by default;
- the drive model (the switch on): every measured class and the nearest-measured rule, the first read with no
  position, contiguous, read-through, fast and full seeks, the abandoned read at 00113280, the break, and the faults
  (a read in flight, more than 16 sectors, a sector not exported);
- host speed (the default): the same reads, each done at the first query whatever its distance, with its data; a
  query in the issue field; the abandoned read landing at the next ready query; the break; the counters;
- a synthetic stereo cue through 001F9820 / 001FA790 / 001F9CF0 for 900 fields, in both modes. It checks the cursor order
  0x4000 → 0x8000 → 0xC000 → 0x4000, and that both voices' samples play every channel block in order over 5
  buffer passes and 5 loops;
- key-off to 0;
- the mixer's rate and overrun counters.

## Boundaries

- Not translated: the driver's SFX commands (1, 3, 5, 6, 0xA..0xD, ...), which go to the forward sink; commands 0x21
  (the SPU-to-IOP read of a bank) and 0x22 (a transfer status query), which no first-level caller sends; its PCM
  input path (0x46..0x4F, the `+0x8680` records); and the non-0x64 RPC function.
- Not modelled: SPU2 reverb, which makes 0x16 inaudible; Gaussian interpolation; core master volumes; the drive's
  sub-field timing (the one-field spreads of section "Drive model"); the SIF DMA's timing inside a field; and the
  driver tick's phase against the field.
- One EE queue is shared with SFX in the original (255 commands per field). In the port the SFX driver has its
  own path, so that shared limit is not modelled; the stream lanes and the sound-bank upload share it, as in the
  original.
- The SPU RAM the boot's and the title's bank uploads fill (the common bank at 0x15040 and the title's at 0x122000)
  is not modelled: the port's SFX voices play the exported registry's samples (SFX_SEQUENCER.md).
- 001FB100 (step H) runs whole in `em_stream_live_step_h` since chain step H7 (STREAM_LANES.md "Binding").

## Binding notes (for the coordinator chain)

**Bound in WP-8b (2026-09-25)** exactly as listed below, by `em_stream_live` (docs/STREAM_LANES.md "Live binding").
Pacing: `em_frame_step` paces fields at 59.94 Hz (`frame_pace_ntsc`); with `EM_UNCAPPED=1` (the tests) the ring
overruns are counted and dropped, and the tests do not listen. The mixer hook is in em_bgm's callback (em_bgm is
now only the device and the mixer). The forward sink stays unset: a command outside the stream set faults (none is
sent from a fresh voice table). Step 7.7's 001FB100 was bound only as its 001F9CF0 call until chain step H7, which
binds all of it (see STREAM_LANES.md).

**Clock domains (binding requirement).** The game thread renders exactly 800.8
samples per `em_iop_stream_field` into a 16384-frame ring, and the audio
device drains it at its own 48 kHz clock. The port's field pacing must
therefore run at the original's field rate (59.94 Hz NTSC); at any other rate
the ring will over- or underrun (`em_iop_stream_mix` counts both). Binding must
state the pacing it uses.

Drive timing: `em_stream_live_boot` sets the backend's drive from `em_settings()` (host speed unless the PS2
disc-drive timing switch is on), before the first read.

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
   6. The game over's 001FA790(0, 0x1B) / 001FAB50 (UM_001FA790 / UM_001FAB50) → the lanes (done:
      em_stream_live; reached in the DAMAGE side runs since chain step DAMAGE, DAMAGE.md section 5).
   7. Step H: bind 001FB100 (`em_slg_001FB100`) so 001F9CF0 runs when `D_00821058 != 1`.
   8. Voiced lines additionally need `voice_push` (001FA5A0) bound (WP-9 / WP-10). The voice cues 143..151 are
      exported.
8. **Assets.** Add `tools/export_streams.py` to the asset pipeline next to the other exporters.
