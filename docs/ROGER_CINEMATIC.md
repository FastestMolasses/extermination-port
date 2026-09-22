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
