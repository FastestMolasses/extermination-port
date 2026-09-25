# Roger encounter camera and player channels

The automatic AREA11 encounter uses bank `96`, with camera/player/Roger entries
0/1/2. The bank is in `chunk15/f12_id44.bin +0x41000`; its header is checked
against the bank pointer in the original playable EE state. This is distinct
from the opening's bank `98`.

`tools/export_roger_cinematic.py` produces ignored
`assets/scene_snow/roger/encounter_camera.emcc` and `encounter_player.empc`.
The camera has 692 samples over 691 source frames. The player has 21 bones,
terminal sentinel -2, no blend and no event stream. Its 5,010 authored keys
plus 63 terminal sentinels preserve the source channels and hold flags.
Roger's separate NPC entry is covered by `ROGER_ORIGINAL.md`.

`make test-roger-cinematic-reference` compares 1,385 camera samples with the
full original `001C7C00`, including half ticks and both endpoint boundaries.
All 32 coordinate/roll/FOV bytes agree, as do the active-cut byte and the
conditional camera cut counter write. All 5,010 player keys agree with the
original `001C84D0`/`001C85D0` decoders. The reference runner executes only
bounded original instructions for validation; it is not part of the native
game.

These checks establish resource decoding and camera sampling. Shared player
ownership, clip binding and playback timing, camera mode transitions, messages,
audio and the complete live encounter remain separate integration work.

## Native player bank ownership (census L22)

The legacy request `player_pose_cinematic_request` (a borrowed decoded bank
held off the record) is deleted. The live path is the original's: the
script's op0A sub 1 (001B9A00, em_area_script) stores the bank
D_0028A490[0x96] at the player record's +0x40, clip 1 at +0x1F2, the rate at
+0x1F4, +0x2F3 = 1 and +0x200 = 0; the AREA11 interaction host, while a script
owner holds the player, runs 00183090 on the record every stage
(`player_pose_commit_tick`: its initializer returns 1 and turns +0x2F3 to
2, then 001C64F0 by +0x1F4 into +0x200) and 0015BCF0's animate step, which
for +0x2F3 = 2 uses the identity owner matrix (001C6960): the palette is the
world-positioned channel palette. Bank 0x96 is mapped read-only into the
record's pose host from `roger/resources.emrs` at its EE address. The
release takes 00182DF0's nonzero-+0x2F3 branch (`record_release_special`:
+0x40 = the default bank, +0x20C = D_00248A00[+0x235], 001C63E0), leaving 80
frames before the next ordinary callback advances to 79.

`make test-player-cinematic-reference` runs the original 001B9A00 /
00183090 / 001C64F0 / 00182DF0 against that record path: 1,388 player
callbacks, the first sample-call order and the release state; its ASan/UBSan
fixture runs the record path under the shared runtime with 120 frozen status
callbacks and the consumed release callback. The level smoke's `roger` phase
compares the player record (+5, +1F0, +1F1, clip, clock, +0x2F3) with route 14
row for row through the encounter and the release.

## Original encounter capture

`make test-roger-encounter-capture` checks a fresh original encounter save
against the native raw pose and camera samplers. The fixture starts from
immutable first-control state03 and places only the player's position/hip
inside Roger's actual trigger polygon. This is a controlled trigger test,
not evidence of correct player movement into that polygon.

At source25.5, all1,050 player/Roger channel float words and126 key cursors
agree exactly. Player world matrices differ by at most0.000092; Roger's by
0.000123. Both actor owner matrices are identity: the player uses C6960,
while Roger's script zeroes its placement before C68C0. The camera samples
source25.0 before its time advances to25.5. Eye/target bytes agree exactly;
the native view matrix differs by at most0.000123. Camera ownership top3
coexists with camera mode8, so these fields must not be conflated.

Both actors have attached faces, suppressed body head7 and face speed1.
The stored body bone7 matrix remains intact; its draw upload is suppressed.
The capture is local under `build/startup-reference/roger-encounter` in the
decomp repository, with EE SHA256
`f68fff65edfee5cca97aed9daa3ce7de11e7eae528726b5760ed5e567e7a93dd`.

`make test-face-allocation-reference` executes the full original player
face attach/reset/free path. The first-control and status-hub captures
allocate zeroed pool blocks, and AF890 clears all208 object bytes on free.
All88 native face-state bytes match original initialization. Another256
existing-face cases confirm that repeated attachment uses a partial reset:
weights and wait fields survive, then speed becomes1. This closes the
initial-pool-content question for those tested player allocations; subsequent
random-call order and face rasterization remain separate work.

Player dialogue calls D06E0 directly when slot0 starts/stops. The player's
83090 callback ticks its face before the body; it does not consume Roger's
activity-byte convention. The live message binding must preserve that order.

## Camera playback worker

`em_cinematic_playback` runs the original event-free scene1 camera path
(`001B8FC0` sub6 and `0022EEF0`). Each ordinary tick samples the exported
track, publishes the DD980 camera block, derives the up vector through the
original rotation helper and converts the clamped FOV to zoom through the
original finite tangent kernel. Time advances by 0.5 per tick. At the
endpoint every invocation repeats the three restore services until the
script releases ownership; the 001B7B30 sub0 wait restores presentation
without moving the cursor or giving up camera_top3.

The tangent coefficients are 13 floats sliced from the user's own ELF by
`tools/export_roger_cinematic.py` into ignored
`assets/scene_snow/roger/camera_projection.emcp`; no original data lives in
source. `make test-cinematic-playback-reference` compares 1,526 original
driver cases and 1,470 script-wait cases with original instruction
execution, checks 1,547 ordered service observations, and matches the
captured source25 up vector and zoom (428.7239) byte for byte. An
ASan/UBSan fixture covers endpoints, malformed resources and each failing
service. Only scene1 is admitted; other scene IDs need their own timeline
workers. The worker is not yet installed in the live encounter.

## Roger encounter media

`tools/export_roger_media.py` writes ignored `assets/scene_snow/roger/`
`encounter.wav`, `encounter_resume.wav` and `media.json` from the user's
ELF, AREA11 overlay and `STREAM/MUSIC.DAT`. The 19 message records come from
timing table `D_00264DD0[11 + 1]` (the index `001FD790` uses for area byte
11); every record has voice -1 and the exporter rejects any other voice. The
lines themselves run on the message service (WP-8, docs/MESSAGE_SERVICE.md;
the former `encounter.emod` is no longer written). The frame command's
stream key 0 maps through the `0026EC60` area table to cue 29; the resume
stream is cue 25 from `D_008106C8` bits 8..14.

`python3 tools/test_roger_media_reference.py` verifies:

- WAV PCM hashes against the report, the capture's in-RAM AREA11 bank byte
  for byte against the source file, and captured cue 29 at `00282178`.
- 4,143 original `001FCA10` message ticks (start delays 0, 1 and 30) against
  the LIVE message service (`em_message_live` on the exported data, posted
  as 001B7D60 case 0 with game mode 2 and `D_008106F4` = 1). On every tick
  the drawn record, the record index `+0x60`, the timer `+0x6C`, the loaded
  state `+0x5C`, the talk mask `+0x64` of `D_002821B0` and `D_008106F4` (0
  once the stream row claims line 0) match. The test also checks the player
  talk calls (1,0,1,0) for the two speaker-0 lines on both sides (the
  service's 001D06E0 host hook), the terminal completion (mode 2, then the
  teardown and a clear block), and nine message-block fields of the original
  against the capture after the 52nd tick.
- 21 original handshake cases: `001FD4C0` key-to-cue mapping (0/25/102 and
  a miss), overlay program `001B82D0` phases 0-3 with the actual command
  bytes, and `001FAE70` resuming cue 25 with an injected RNG result.

Boundaries: glyph layout/width/drawing (`001FE480`, `001FE530`, `001CC170`,
`001FE070`), player-face calls (`001D06E0`), stream/SPU calls, the RNG and
the block clear are stubs or injected replies on the original side. Only
message mode `D_008106F5` 0 is exercised; the voice lookup hits in
`001FD580`/`001FD6A0` are unreachable with voice -1. PCM is exact ADPCM
decode, not SPU2 output. Nothing posts the encounter into the live service
or installs the streams into the live Roger encounter yet (WP-9).
