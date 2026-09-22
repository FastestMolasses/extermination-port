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
