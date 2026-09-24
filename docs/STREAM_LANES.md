# Stream lanes: the original EE side of music and voice streams

Status: 2026-09-23. This covers the translation and its oracle. **Not wired**: nothing in the live game calls it
yet. WP-8 bound the message service without it; the WP-8 fix round added 001F9820 (the lanes' initial state).
What binding still needs is under "Binding notes" ("Still missing").

Files:
- `src/game/em_stream_lanes_original.{h,c}`: the native translation.
- `tools/test_stream_lanes_reference.py`: the original-instruction oracle.
- `tests/stream_lanes_test.c`: the native contract test (fail-stop faults, worker protocol).

Make target: `test-stream-lanes` runs both.

## Scope

| Original | What it does (read from the code) | Source read |
|---|---|---|
| 001F9820 | Initial state: four voices from 0011A2B0, their IOP stream configuration, the lane voice / mask / buffer / size / volume fields, then 001FA570. Called once from 001AAE40's start-up and at the end of 001F9BF0 | NEARMISS C + the .s (the C's logic matches) |
| 001F9CF0 | Per-frame lane service. Runs from 001FB100 (frame step H). | NEARMISS C + the .s (the C has a bug, below) |
| 001FA0D0 | Disc read sequencer over D_00282157 / D_00282158 | NEARMISS C + .s |
| 001FA5F0 | Voice ring consumer: ring slot -> first idle voice lane (1, then 2) | byte-matched C |
| 001FA330 | Volume ramps and volume words | NEARMISS C + .s |
| 001FA790 | Lane start from a clip row | NEARMISS C + .s |
| 001FABF0 | Lane (re)start with a fade-in step | NEARMISS C + .s |
| 001FAD70 | Fade-out step and release flag. 001B0C00 calls it for lanes 0, 1, 2 | byte-matched |
| 001FAAC0 | Lane release | byte-matched |
| 001FAB50 / 001FAB80 | Release lane 0 and clear D_008106F4 / release lanes 1, 2 and clear D_008106F5 | byte-matched |
| 001FA570 / 001FABB0 | Ring reset / stop everything: ring reset, 001FAB50, 001FAB80, D_00282157 = 0 | byte-matched |
| 001FD470 | Mask stop: bit 0 calls 001FBC50 (worker), bit 1 calls 001FABB0 | byte-matched |
| 001FAE70 | Music cue select / resume | byte-matched |
| 001FB0B0 | D_00810D38 = cue, then 001FAE70(1) | byte-matched |
| 00119828, 0011A608, 0011A6A0, 0011A6E8 | Pack arguments into 001157F0 commands 0x16, 0x40, 0x42, 0x43 | .s (eegcc) |
| 0011A4B8, 0011A4E8, 0011A658 | From 001F9820: commands 0x3C, 0x3E (0011A4E8 also sets or clears the voice's bit in D_0027F740 for flag bits 0 / 1) and 0x41 | .s (eegcc; 0011A4E8 NEARMISS) |
| 0011A730 | `voice < 0x30 ? D_00281880[voice] : 0` | byte-matched |
| 001281C0, 00128250 (+ 001278C0) | Soft-float float -> int and float -> unsigned. 00128250 does **not** read its a0 | .s (`.word` asm) |

## Storage (original addresses)

- Lane record `D_00281FD0 + lane * 0x60`, lanes 0 (music), 1 and 2 (voice). The record fields modelled are
  +0x00..+0x03 (bytes), +0x04 voice, +0x08 64-bit voice mask, +0x14 / +0x18 buffer address and size, +0x20 clip
  row +0xC, +0x24 / +0x28 / +0x2C loop, end and base sectors, +0x30 / +0x34 / +0x38 read sector, count and
  address, +0x48 frames left, +0x4C duration, +0x50 start counter, +0x54 fade step (f32), +0x58 volume (f32) and
  +0x5C release byte. No other byte of these records is read or written by these functions. The oracle proves
  this for every executed path.
- Record 3 contributes only `D_002820F4`, lane 0's second voice.
- Lane block: `D_00282154..56` active (lb), `D_00282157` read phase, `D_00282158` read lane, `D_0028215B` (lbu),
  `D_00282178[3]` cues, `D_00282188` / `D_0028218C` music / voice disc base.
- Ring `D_00281CF0[16]` (-1 = empty). The push index is `D_00275B30` (001FA5A0, owned by the message service); the
  pop index is `D_00275B34`. `D_00275B2C` holds the last lane-0 cue.
- 001F9820 also writes lane +0x04 / +0x08 / +0x14 / +0x18 / +0x58 and `D_002820F4`, reads the three buffer words
  `D_00275B20/24/28` (gp; lane 2 / 1 / 0; each a 001FA6A0 result at the IRX bring-up
  `sub_cdrom0_IRX_SNDN2DRV_IRX_1`, i.e. an IOP heap block) and, through 0011A4E8, updates the 64-bit
  `D_0027F740`. Lane 0's two voices share one buffer: the first voice is configured at buffer + 0x400, the second
  at the buffer, both with flags 0x20002; lanes 1 and 2 use flags 0x10000. Every configuration has size 0x10000 and
  last word 0x4000. The stored lane masks are built with 32-bit shifts (a voice of 32 or more wraps) and then
  sign-extended; the masks passed to the packers are 64-bit shifts.
- Globals read or written: `D_00810E90` (counter), `D_008106C8`, `D_00810D38`, `D_00810700`, `D_008104E4`,
  `D_008106F4` (lane-0 hold), `D_008106F5` (voice hold) and `D_00281880[0x30]` (read by 0011A730; written outside
  these functions).
- Clip rows: `D_0025DD30` (music, 68 rows: the table ends where the voice table starts) and `D_0025E170` (voice,
  179 rows). Of each 16-byte row only +0 (sector), +8 (size) and +0xC (row +0xC) are read. The 179 is a structural
  bound: the sector column rises monotonically through row 178 and breaks at 179. The original has no bound.
  Lane 0 reads `D_0025DD30 + 16 * cue`, so a lane-0 cue of 68 or more is voice row `cue - 68`
  (0x25DD30 + 16 × 68 = 0x25E170). The native code does the same over one contiguous view (music rows 0..67, then
  voice rows 0..178), and only when the music view has exactly 68 rows. A cue outside the supplied rows faults
  (fail-stop): below 0, past row 246 on lane 0, or past row 178 on lanes 1/2.

## Behaviour notes (from the original code)

- **001FA790(lane, cue)** returns when the lane is active or cue == 0. Otherwise it:
  1. stores the cue;
  2. uses the voice table (lanes 1, 2) or the music table (lane 0, also D_00275B2C = cue);
  3. computes the duration: `00128250(60 * (0.074666664 * (float)(size >> 11)) - 30)`. For lane 0 the product is
     divided by 2.0 before the `* 60`. The operations are EE mul/div/sub; `>> 11` is `sra`;
  4. sets end = `((size + 0x7FF) sra 11) + base` and read count = `min(buffer_size / 2, size)` rounded up to
     sectors. The count uses `srl` on one arm and `sra` on the other;
  5. sets +0..+3 = 1, 2, 1, 1 and active = 1;
  6. calls **001FABF0(lane, cue, 0, 0)**. This sets the step to 16383.0.
- **001FABF0(lane, cue, fade, enable)** with enable set works only on an idle lane. It sets volume = 0, calls
  001FA790(lane, cue) (a0 and a1 pass through unchanged), then 0011A608(mask, 0, 0). This happens even when
  001FA790 returned early because cue was 0. Then it sets release = 0 and step = `16383 / (float)fade` (EE div.s,
  round to nearest). A fade of 0 gives 16383.0.
- **001FAD70(lane, fade, release)**: for an active lane, release byte = (s8)release and step = `-(16383 / fade)`.
  A fade of 0 gives -16383.0.
- **001FA330**: for each active lane with step != 0:
  1. volume += step (EE add.s, pre-trim);
  2. rising: clamp at the cap. The cap is 0x3FFF, or **0x3000 for lane 0 while D_0028215B != 0**. The NEARMISS
     C's comment says the opposite; its code is right;
  3. falling: at <= 0, set volume = 0 and step = 0, then call 001FAAC0 when the release byte != 0;
  4. then send the volume words through 0011A608. Lanes 1 and 2 use (mask, v, v). This is still sent after the
     release in step 3.

  Lane 0 with D_0028215B == 0 uses (1<<voice, 0, v) and (1<<D_002820F4, v, 0). Otherwise both use (v, v). v is
  001281C0(volume).
- **001F9CF0**, per lane:
  - **Timer.** An active == 2 lane with +0x20 == 0 ends through 001FAAC0 when the counter delta is not below
    +0x4C. After a counter wrap the original compares the raw counter, not the elapsed time.
  - **Switch on +0x00:**
    - 2: waits for its hold byte to clear, then active += 1, start = counter, and 0011A6A0(mask).
    - 0: reads 0011A730(voice). A nonzero status selects the half, and a half change requests a read (+0x00 =
      +0x03 = 1).
    - 1: when +0x03 == 2 (the read is done), the read address advances by count·2048.
      - At the half boundary: +0 = +3 = 0, last half = half, sector advance with loop wrap. Then, for an
        active == 1 lane, a set hold byte gives state 2 and hold = 1; a clear one gives active 2, start =
        counter and 0011A6A0.
      - Not at the boundary: another read from the loop sector for `min(end - loop, bytes left in sectors)`.
  - After the lanes it calls 001FA0D0, 001FA5F0 and 001FA330.
  - Last, for each idle lane with a nonzero 0011A730 status, it calls 0011A6E8(mask), clears the cue and sets
    +0x03 = 0.
- **001FA0D0**:
  - Phase 0 scans lanes from D_00282158 for +0x03 == 1.
  - Phase 1 calls 00113280(1). When the result is 2, it calls 00112610(sector, count, address, {0,0,0}); a
    nonzero result advances the phase.
  - Phase 2 calls 00112D18(1). A 0 result marks +0x03 = 2. When the lane's +0x03 is no longer 1, it calls
    00113478(1) instead.
  - The lane index then advances, wrapping at 3.
- **001FAE70(a0)**:
  1. Calls 001FC280.
  2. cue = bits 8..15 of D_008106C8; when D_00810D38 != 0, cue = (cue & 0x80) | D_00810D38.
  3. Override: when D_00810700 != 0x15, D_00810D38 is not 0xB/0xC/0x17 and D_008104E4 == 1, lane 0 plays cue
     0x18 with fade 0x40 (unless it already holds 0x18), and nothing else happens.
  4. Otherwise fade = 270 + ((LCG >> 16) & 0x7F).
     - a0 != 0: release lane 0 and start cue & 0x7F when it is nonzero.
     - a0 == 0: cue & 0x7F == 0 releases lane 0. Any other cue restarts only if lane 0 is idle or holds a
       different cue.

### Findings against the decomp C (the decomp repository was not edited)

1. **001F9CF0 NEARMISS C has a min/max inverted.** At the refill that is not at the boundary, the .s sets
   `+0x34 = ((end - loop) << 11) < bytes_left ? (end - loop) : (bytes_left + 0x7FF) >> 11`, which is the smaller.
   The C has the two arms swapped. The translation follows the .s. The oracle catches the C's version
   (mutation control below).
2. **001FA790 C** passes `src[0]` to 00128250. That value is a leftover in a0: 00128250 stores only f12 and
   calls 001278C0. The function is float -> unsigned, not a two-argument call. The C's end-sector `>>` is
   unsigned where the .s uses `sra`. These are identical for every ELF row, and the synthetic-row cases pin the
   `sra`.
3. **001FABF0 C** declares `func_001FA790(void)`. The .s calls it with a0 and a1 untouched: (lane, cue).
4. **001FA330 C comment** gives the D_0028215B polarity backwards (see above). Its code matches the .s.

## EE float

Every COP1 operation follows docs/EE_FLOAT_MODEL.md:
- mul/add/sub truncate; add/sub pre-trim the smaller operand;
- div.s rounds to nearest;
- cvt.s.w truncates;
- neg.s saturates exponent-255 values;
- compares apply DAZ and saturation.

The module does every COP1 operation through the shared integer-only header `src/game/em_ee_float.h`
(docs/EE_FLOAT_MODEL.md section 6): `em_ee_add_bits`, `em_ee_sub_bits`, `em_ee_mul_bits`, `em_ee_div_bits`,
`em_ee_cvt_s_w_bits`, `em_ee_neg_bits` and `em_ee_c_eq/lt/le_bits`, on raw binary32 words. It has no float helpers
of its own and performs no host float operation (the compiled object has no arm64 FP arithmetic or conversion
instruction), so the host rounding mode, FTZ/DAZ state and contraction cannot change a result. It needs no `-lm`.
The oracle uses `tools/ee_float_model.py`, which the header is tested against. The two soft-float conversions are integer code, translated from 001278C0's unpack
(exponent field 0 counts as zero even when the fraction is not 0).

## Verification

`python3 tools/test_stream_lanes_reference.py`: about 5 s of CPU quick. `EM_TEST_FULL=1` runs the exhaustive
sweep. Its last run on 2026-09-23 (after 001F9820 was added) passed 315,956 single-call cases and 18,200 lockstep
frames (26 captures × 700) in 251 s. Setup:
- The original instructions of every function in the scope table run in an EE interpreter defined in the test,
  over captured RAM. That RAM is 11 images in `startup-reference/` and 15 route images in `build/s87/route/`.
- Code comes from the pinned ELF (sha256 checked). Every executed function's bytes in each capture are checked
  to be the ELF's, and so are both clip tables.
- Workers are recorded with scripted results, identical on both sides. The workers are 001157F0, 00122BB8,
  001FC280, 001FBC50, 00113280, 00112610, 00112D18, 00113478 and 0011A2B0 (001F9820's voice allocator).
  00121A28 (memset) is argument-checked and performed.

Per call, the test compares:
- the 0x1C0 bytes 0x281FD0..0x282190 (the native side writes only its fields into a copy of the input, so an
  original write to any unmodelled byte fails);
- the ring, D_00275B2C/30/34, the seven globals and D_0027F740;
- every worker call with all its arguments, in order.

Every original load and store outside the stack must fall in the modelled sets.

Case families (quick counts):
- leaves 1,030;
- fade 320;
- volume 520;
- start 360: in full mode all 247 lane-0 cues (68 music rows, then the voice rows as lane 0 reads them) and all
  179 voice cues on lanes 1/2; quick keeps 0x18, 25, 29, 63, 68, 127, 143..151 and the last rows;
- select 440: 001FAE70 and 001FB0B0 over D_008106C8, D_00810D38, area, D_008104E4, lane-0 cue and active;
- stop 56;
- read 360;
- ring 90: voice cues 143..151;
- service 666: 600 selected from a grid over every arm of 001F9CF0's switch, the timer with a counter wrap, both
  refill arms and the sector wrap, plus 54 fixed cases that pin its two unsigned (sltu) compares in every mode:
  status words 0x80000040, 0xFFFFFFF0, 0x80000000 and 0x7FFFFFFF against buffer_size >> 1 (state 0, both
  last halves), and the timer with a duration of 0x90000000 (elapsed 0x10), an elapsed value of 0x90000000, and a
  raw post-wrap counter of 0x80000000;
- 48 synthetic rows: sizes at the sign boundary, on lane 0 (music row 3 and cue 71 = voice row 3) and lane 1;
- init 100: 001F9820 over the boot's voices 0..3, -1 (no free voice), voices at and past 32 (32-bit lane masks
  against 64-bit packer masks), set and cleared D_0027F740 bits, buffer words near the wrap and a dirty prior lane
  state; plus 0011A4E8 run directly on arbitrary flag words so both of its D_0027F740 arms execute;
- lockstep, 4 captures × 330 frames (all 26 × 700 in full mode). Each frame runs 001F9CF0 with scripted IOP
  status and disc results. The runs include:
  - the Director's voice cues 150/149, then 143..148 and 151, pushed as 001FA5A0 would;
  - the hold bytes set and released as the message service does;
  - 001FAD70(0, 30, 1), 001FB0B0(29), 001FAE70(0), 001FABB0 and 001FAE70(1).

  The test asserts that voice starts, voice ends, 0x42/0x43 commands and disc reads all happen. The scripted
  inputs are seeded with `zlib.crc32(capture name) & 0xFFFF`, so every run replays the same trajectory; the seeds
  are in the report and are printed on a failure. Each run builds its bridge library in its own temporary
  directory under `build/stream_lanes_reference/` and removes it, so concurrent runs do not race.

Coverage is asserted: every reachable instruction of every function in the scope table executes. The only
exceptions are 8 words no path reaches: 7 compiler duplicates after an unconditional branch's delay slot, and
the padding word ending 001281C0.

Mutation controls, each caught by the test:
- the NEARMISS min/max;
- host float instead of the EE model at a call site: host add for the volume ramp, host div for the fade step,
  host arithmetic for the 001FA790 duration;
- em_ee_mul_bits in place of em_ee_div_bits for the fade step;
- a signed compare for the status word against buffer_size >> 1, and for elapsed against the duration;
- an off-by-one in the lane-0 row index past the music table;
- the lane-0 L/R volume order;
- the wrap compare;
- the skipped 0011A608 in 001FABF0;
- the inverted 0x3000 cap;
- sra -> srl in the count and the end sector, and srl on the size;
- 00128250's sign test;
- the ring lane order;
- the read-lane wrap;
- the LCG bits;
- the hold value;
- the idle-lane cue clear;
- the frames-left value;
- a state-3 arm;
- in 001F9820 / 0011A4E8 / 0011A658: the lane-0 L/R volume words, a 64-bit stored lane mask, a missing sign
  extension of the lane-0 mask, the D_0027F740 set/clear order, a missing D_002820F4 store, a missing + 0x400 on
  the first voice's buffer, a 32-bit instead of 24-bit high-mask shift, a missing ring reset, a missing
  D_00275B2C clear and swapped words in the 0x3E packing. (A signed shift of 0011A4E8's third word before its
  0xFFFF mask survives: it is equivalent, the mask keeps only bits 8..23.)

Moving a native store past a worker call is not observable, because workers do not see module state.

**Capture evidence (native only, no oracle):**
- Native 001F9820 with the boot's voices 0, 1, 2, 3 over each image's own `D_00275B20/24/28` reproduces every
  captured lane's voice, voice mask, buffer and buffer size and `D_002820F4` (13 checks per image, 338 in all).
- For every lane with a nonzero base sector in the 26 images, native 001FA790 on the ELF row that the base
  sector selects reproduces the captured +0x4C duration, +0x28 end, +0x24 loop and +0x2C base, covering lane 0
  cues 25 and 29 and the captured voice lanes. With the fade checks below and the 001F9820 fields above, the
  report counts 383 capture checks.
- Every captured lane-0 fade-in step is `16383 / (270 + s2)` for exactly one s2 in 0..127, and the captured volume
  is a whole number of EE add.s steps of it:
  - 280 or 281 frames in the startup, elevator and panel images;
  - 347 in route 01;
  - 271 in route 03;
  - 326 in route 14, the Roger encounter's end, which resumes cue 25 after the encounter's cue 29 (captured at
    0x282178).

## Boundaries (stated, not modelled)

- **IOP/SPU.** Commands go into 001157F0's queue. The draining of that queue, what the IOP driver does with
  0x16/0x40/0x42/0x43, the SPU voices and the sound output are all outside this module. `D_00281880` (0011A730) is
  written by the IOP status path, which was not read.
- **Disc streaming.** 00113280, 00112610, 00112D18 and 00113478 are workers. The decomp labels them libcdvd
  (sceCdInit/Read/Sync-like); that label was not verified here.
- **Initial state.** 001F9820 is translated (`em_stream_lanes_001F9820`); its voice allocator 0011A2B0 is a worker
  (it scans the sound sequencer's voice table `D_0027CCC0`, which this module does not own). The writer of
  `D_00282188`/`D_0028218C` is `sub_O_STREAM_MUSIC_DAT_1` (the start sectors of the disc's stream files, found
  through 00111C28); both are 0x9D911 and 0xC3257 in all 26 images; it is not translated. `D_0028215B` is copied
  from D_0081011C by 001FB100 / 001FB210, outside this lane.
- 001FB100 itself (step H: 001F9CF0, the D_0028215B change, block_copy, 001FC6E0) is not translated here.

## Binding notes for the coordinator chain

Storage: one `EmStreamLanes` as the single storage of the bytes in "Storage". The globals view must point at the
coordinator's own D_00810E90 / D_008106C8 / D_00810D38 / D_00810700 / D_008104E4 / D_008106F4 / D_008106F5 and at
the IOP status words. Seed `state` by running `em_stream_lanes_001F9820` where the original does (001AAE40's
start-up), never from a captured image. The clip rows come from the user's ELF (0x25DD30 × 68 and
0x25E170 × 179), exported locally and never committed.

| Original caller | Port slot | Bind to |
|---|---|---|
| 001FB100 (frame step H) | step H worker | `em_stream_lanes_001F9CF0`, called first when D_00821058 != 1; the rest of 001FB100 (D_0028215B update, block_copy, 001FC6E0) is not translated |
| em_area_script (op07 sub12 / sub4, op0F, per the lane brief) | `EmAreaScriptWorkers.w_001FAE70(ctx, a0)`, `.w_001FABB0(ctx)`, `.w_00119828(ctx, a0, a1, a2)` | `em_stream_lanes_001FAE70`, `_001FABB0`, `_00119828` (return 0 / -1 maps directly) |
| em_area_script `d282157` reader | `const int8_t *d282157` | `&lanes.state.read_phase` |
| em_scene_workers `w_001FAE70`, `w_001FABB0`, `w_00119828`, `w_001FA790`, `r_00282157` | same | `_001FAE70`, `_001FABB0`, `_00119828`, `_001FA790`, `state.read_phase` |
| message service `stop_lane` (001FAAC0), `stream_stop` (001FD470), `stream_play` (001FA790) | `EmMessageWorkers` | `_001FAAC0`, `_001FD470`, `_001FA790` |
| message service `busy155` / `busy156` | `EmMessageShared` | copy `state.active[1]` / `state.active[2]` before each tick |
| message service `voice_mode` / `stream_mode` | `EmMessageShared` pointers | the same bytes the lanes' globals view uses for D_008106F5 / D_008106F4 (one storage) |
| message service `voice_push` (001FA5A0) | `EmMessageWorkers.voice_push` | `em_message_voice_ring_push` on a ring view copied from/to `state.ring` + `state.ring_head` (001FA5A0 writes only those) |
| Roger STOP_STREAMS (001FABB0), RESUME_MUSIC (001FAE70(0)) | em_roger emits | `_001FABB0`, `_001FAE70(0)` |
| status frame STOP_STREAMS (001FABB0), RESUME_MUSIC (001FAE70(1)) | em_status_frame emits | `_001FABB0`, `_001FAE70(1)` |
| interaction RESUME_MUSIC (001FAE70(0)) | em_interaction_frame | `_001FAE70(0)` |
| 001B0C00(p) fades (area change, Roger) | scene bindings 001B0C00 | `_001FAD70(0, p, 1)`, `(1, p, 1)`, `(2, p, 1)` after 001AEDE0(p, 0) |
| 001FC280 (called by 001FAE70) | `workers.w_001FC280` | the room-ambience owner; its two 00119828 calls go to `_00119828` |
| 00122BB8 | `workers.w_00122BB8` | the shared game LCG (same stream as every other 00122BB8 caller) |
| 001157F0 | `workers.w_001157F0` | the port's IOP-command sink (audio backend); map the original's queue-full -1 to 0 |
| 001FBC50 | `workers.w_001FBC50` | the SFX stop-all owner |
| disc workers | `workers.w_00113280 / _00112610 / _00112D18 / _00113478` | the port's stream reader (which reads from local assets, never the disc at run time) |

### Still missing (WP-8, 2026-09-23; state after the WP-8 fix round)

The message service went live without the lanes. Its stream-facing workers are bound to what the port has, and
none of them is this module:
- 001FD470 -> `em_scene_bindings_001FD470`: bit 0 `w_001FBC50` (em_sfx_stop_all, then its two 00119828 calls), bit
  1 `w_001FABB0`, the port's stream-release stand-in (it stops em_bgm and the opening stream and clears
  `D_008106F4`/`D_008106F5`; it does no 001FA570 ring reset, no per-lane 001FAAC0 key-off and no `D_00282157`
  store);
- 001FA790 on lane 0 with the opening row's cue -> the opening stream (`em_opening_media`, a lane-0 stand-in that
  moves `D_008106F4` from 2 to 1 on its first step H, i.e. an instant prefill, and starts its sound at 0);
- 001FAAC0 on lanes 1/2 -> nothing (no voice lane is ever started); 001FA5A0 -> unbound (a voiced line faults);
- 00119828 (0x16) -> a reported no-effect binding (`w_00119828`, also for the opening's (0/1, 0, 0) pair).

Binding this module as the single lane owner (and deleting every stand-in above) needs:
1. **Initial state: translated, not bound.** `em_stream_lanes_001F9820` (with 0011A4B8 / 0011A4E8 / 0011A658) runs
   the original's boot set-up. Binding it needs its worker 0011A2B0 (NEARMISS EE code over the sequencer's voice
   table `D_0027CCC0`; translate it with that table as canonical storage, or bind the sequencer owner) and the
   three buffer words `D_00275B20/24/28`, which are IOP heap blocks (001FA6A0 -> 0010F8F8 at the IRX bring-up):
   the audio backend's IOP-memory allocator must supply them.
2. **The IOP stream driver in the audio backend** (the 001157F0 boundary; the driver, `SNDN2DRV.IRX`, was never
   read). It must take commands 0x3C (init), 0x3E (a voice's stream configuration: voice, flags 0x20002 /
   0x10000, IOP buffer, size 0x10000, SPU address, 0x4000), 0x40 (volume words?), 0x41 (probably pitch; 0xBB80 would be 48000 Hz),
   0x42 / 0x43 (apparently key on / off — these IOP meanings are inferred from the EE side only; the driver SNDN2DRV.IRX was never read, so they are unverified) and 0x16 (from 001FBC50, 001FC280 and the opening), play the SPU ADPCM in the lane
   buffers, and write `D_00281880[voice]`, which 0011A730 returns. What the captures fix about that word: in all
   26 images every lane voice's word is 0, 0x4000, 0x8000 or 0xC000 (multiples of the configuration's last
   word, 0x4000, inside the 0x10000 buffer); lane 0's two voices always report the same word; a lane in state 0
   has half 1 when its word is 0x4000 and half 2 when it is 0x8000 or 0xC000, with its read address on the other
   half (the EE refills the half the driver is not in); idle voices report 0. The word is therefore the driver's
   block cursor in the buffer. When it advances relative to the sound (per transfer or per played block) is not
   in any capture, so it has to be a stated backend model (rate: 28 samples per 16 bytes at 48000 Hz, which the
   001FA790 constant 0.074666664 s per 2048-byte sector also encodes), advanced per game frame so that tests
   stay deterministic.
3. **The disc side.** `sub_O_STREAM_MUSIC_DAT_1` (writes `D_00282188`/`D_0028218C`, the start sectors of the two
   stream files, through 00111C28) is not translated; the two stream files need a local exporter (from the
   user's disc, never committed) and a sector reader bound to 00113280 / 00112610 / 00112D18 / 00113478 whose
   reads land in the backend's IOP buffers. A zero-latency reader completes the prefill in the state machine's
   minimum number of frames; the original's drive latency is hardware timing and is not modelled.
4. **Step H.** 001FB100 (`em_slg_001FB100`, translated, not bound) calls 001F9CF0 when `D_00821058 != 1`; binding
   it also binds its `D_0028215B`/`D_0081011C` volume commit (00119870, 0011A608) and its 001FC6E0 cue list
   (001FB9F0, the SFX owner) and the `D_00281B70` copy.
5. **Every legacy stream path replaced at once:** `em_opening_media` (lane-0 stand-in), em_bgm's cue-25 resume in
   `w_001FAE70`, `stream_release_all` (`w_001FABB0`), `w_00119828`, the 001B0C00 fades (UM_001FAD70) and the game
   over's 001FA790(0, 0x1B) / 001FAB50 (UM_001FA790 / UM_001FAB50).
6. **Voice clips.** The VOICE.DAT cues 143..151 are reached only by the director's and Roger's lines (WP-10,
   WP-9), which also need `voice_push` (001FA5A0) bound.

Items 1..5 are a work package of their own (an IOP-side audio backend plus its disc reader); the WP-8 fix round
proposed splitting them out of WP-8 (docs/FIRST_LEVEL_AUDIT.md WP-8). Until they are bound, the module stays
unwired (CLAUDE.md fidelity rules), and a voiced line (the director's cues 150/149, Roger's voiced 0x7F chain)
faults at 001FA5A0 because `voice_push` is unbound.
