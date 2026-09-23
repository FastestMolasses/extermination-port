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

## Native player bank ownership

The player pose host now accepts the original op0A/sub1 bank request through
`player_pose_cinematic_request`. This borrows a verified compatible bank until
release. The command publishes pending mode1, clip/rate and a cleared result;
it does not initialize or advance channels. On the next player callback,
`00183090` commits the requested clip at source0, changes mode1 to2 and returns1.
The caller therefore advances by the requested rate on that same callback:
Roger's first player sample is source0.5 with 690.5 frames remaining.

Mode2 uses the identity owner matrix (`001C6960`), so the host publishes its
world-positioned channel palette without adding the ordinary player position
and yaw. Hip mirrors come from that same palette. Release follows the original
nonzero2F3 branch: restore the default bank and healthy idle0 directly, leaving
80 frames before the next ordinary callback advances to79. Numeric clip IDs
from the two banks must not be blended as though they shared a bank.

`make test-player-cinematic-reference` executes the original request,
initializer, animation clock and release. It compares 1,388 player callbacks,
the first sample-call order, bank transitions and release state. The actual
bank and shared ownership fixture passes ASan/UBSan, including 120 frozen
status callbacks, world-palette/hip publication and the final consumed release
callback. The original channel sampler is intercepted in this timing proof;
the authored keys have the separate decoder proof above.

Shared player-ready2 now requires an explicitly installed face/body worker.
Status suppresses that whole worker, and a missing worker retains a fault.
The fixture's face and frame side effects are boundaries; this checkpoint does
not yet install the actual Dennis face, Roger camera or media into the scene.

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
`encounter.emod`, `encounter.wav`, `encounter_resume.wav` and `media.json`
from the user's ELF, AREA11 overlay, `extract/chunk15/f12_id44.bin` and
`STREAM/MUSIC.DAT`. The 19 message records come from timing table
`D_00264DD0[11 + 1]` (the index `001FD790` uses for area byte 11); every
record has voice -1 and the exporter rejects any other voice. The frame
command's stream key 0 maps through the `0026EC60` area table to cue 29;
the resume stream is cue 25 from `D_008106C8` bits 8..14.

`python3 tools/test_roger_media_reference.py` verifies:

- EMOD records/text against the export, WAV PCM hashes against the report,
  the capture's in-RAM text block byte for byte against the source file, and
  captured cue 29 at `00282178`. EMOD y is 388, twice the `001FD950` GS draw
  row 0xC2.
- 4,143 original `001FCA10` message ticks (start delays 0, 1 and 30) against
  the native clock in `em_opening_media.c`. On every tick the drawn record,
  the record index `+0x60`, the timer `+0x6C`, the loaded state `+0x5C` and
  the talk mask `+0x64` of `D_002821B0` match the native state. The test also
  checks the player talk calls (1,0,1,0) for the two speaker-0 lines, the
  terminal completion (mode 2, then two stop-lane calls and a clear), and nine
  message-block fields against the capture after the 52nd tick.
- 21 original handshake cases: `001FD4C0` key-to-cue mapping (0/25/102 and
  a miss), overlay program `001B82D0` phases 0-3 with the actual command
  bytes, and `001FAE70` resuming cue 25 with an injected RNG result.

Clock fix: after a line's completion draw, `001FDB80` only clears `+0x5C` and
advances `+0x60`. `001FD790` loads the next duration on the following tick.
The native clock used to preload that duration in the completion tick, so
its timer read 79 where the original read 0. The drawn line and talk mask
were already right. The clock now loads the duration lazily, and a new
`loaded` field mirrors `+0x5C`. The opening and panel-message users of the
clock still pass their tests.

Boundaries: glyph layout/width/drawing (`001FE480`, `001FE530`, `001CC170`,
`001FE070`), player-face calls (`001D06E0`), stream/SPU calls, the RNG and
the block clear are stubs or injected replies. Only message mode
`D_008106F5` 0 is exercised; the voice lookup hits in `001FD580`/`001FD6A0`
are unreachable with voice -1 and are not modeled natively. PCM is exact
ADPCM decode, not SPU2 output. Nothing installs this media, the clock or the
resume stream into the live Roger encounter yet.
