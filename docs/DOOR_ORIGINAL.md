# Original AREA11 distant door

The canonical door is source `0082A3C0`, callback `001BC350`, model-table
index14 and animation bank39. `export_door_original.py` verifies the model
and bank against the original first-control RAM and produces ignored
`assets/scene_snow/door_original/` resources. The initialized pose uses
clip0 at source0, which differs from the model's rest pose.

`em_door_original` implements the original pooled controller and its nested
script phases. An ordinary passive callback does not advance animation.
It builds the pose, publishes actor+B0 plus (0,10,0), then calls draw even
when the visibility result is zero. The runtime retains canonical owner
status, class and armed bytes, original placement, raw channels and current
palette. Scene bindings and GPU objects must be removed before freeing it.

`make test-door-original` compares 5,662 original instruction state/order
cases. `make test-door-original-runtime` verifies 123 compressed channel
keys and the first-control capture: all 50 channel floats, all16 owner
matrix words and all32 door-palette words agree exactly. An actual-resource
ASan/UBSan fixture exercises 240 passive callbacks, canonical binding,
required-worker failure retention and teardown.

`em_door_transit` implements kickoff `001BBE40` and request commit
`001BC150`. It sets the side latch, patches the script, faces the player,
aligns its position mirrors, starts the script, then performs its first pump.
The original side decision chooses destination 2 or 1 from row 2755F8.
Sound 401/402 is also selected by side; it is not an open/close sound pair.
Every FPU operation of 001BBE40's geometry is the EE model
(`em_ee_float.h`), and its callees 001B1240, 001B1470, 0011E2A8 and 0011DE90
are workers (`EmDoorTransitMath`). `tools/test_door_transit_reference.py`
executes the original on the EE float model (FallEE, docs/EE_FLOAT_MODEL.md)
with the native workers answered by the same original callees: 146 kickoff
cases by default (1,350 with `EM_TEST_FULL=1`), 112 destination cases, and
route beat 09's press stance, whose alignment point equals the capture's f309
player position. (Before census L18 the test ran an IEEE interpreter and the
native used truncating arithmetic; both put that point one ULP off the
capture.)

`em_door_program` is only 001BBE40's patch of the ELF program image
(0x24DC14 player clip, 0x24DC54 door clip, 0x24DC58 the 001BBD60 sound word,
0x24DC8C the wait). The program itself (0x24DE40: op07 sub 0, op0D sub 5, op0A
sub 0, op0B sub 6, op02) runs on the AREA11 script host, em_area_script's
handlers (AREA_SCRIPT.md). Its own handler copies and their tests
(`test_door_program_reference.py`, `test_door_program_runtime.py`,
`tests/door_program_test.c`) were retired with census L18: the script host's
handlers are the one bound translation, and `test_area_script_reference.py`
replays the program over route 09 (98 frames, the door's block, 3B8D/91/92,
the camera byte and the camera) with no difference.

## Binding (live, census L18, 2026-09-25)

`src/game/em_area11_door.{h,c}` runs the fence door's pool node (roster
record 0, callback 001BC350) on `em_door_original_tick`, in both walk
variants, over the pool record's canonical bytes (+0x00, +0x01, +0x02,
+0x03, +0x04, +0x05, +0x0B, +0x2E, +0x56, +0x80, +0xB0, +0xC0 and the script
block +0x1F0.., whose +0x0C gates 001BC0E0's advance and whose +0x0E holds
the advance flags; the door id +0x34 lives in the binder). Its hooks:

| Original | Binding |
|---|---|
| 001BBDA0's 001B0F60 | `em_slg_001B0F60` with 001B0EA0 = `em_area11_boxes_door_001B0EA0` (the exported bank and the shared bone-slot stack: +0x09 = 2, +0x0C = 2 as in the captures) and bone_init_default_2(door, 0) = the source bank's clip 0; +0x40 = D_0028A574 |
| 001BBE40 | `em_door_transit_kickoff` (above); the patch writes the script host's program image (`em_area11_script_host_door_program`) with 001BBD60's word from `em_sdf_001BBD60` over the D_0024DB80 pair `source.emdo` carries; +0xC4 = `player_pose_face`; 00182F90 = `player_pose_align` |
| 001BA1A0 / 001BA1F0 | `em_area11_script_host_start` / `_tick` on the door record (op0B's 001C67E0 = `em_area11_door_clip_init`, its 001FBD50 = the positional cue at the door) |
| anim_advance_time / anim_clip_init / 001C68C0 | the runtime's source-bank playback and pose (`em_door_original_runtime_advance` / `_animation` / `_place`) |
| 001BC150 | `em_door_transit_commit` with 001AEDE0(4, 0): B8 = 2, B7 = the destination row's side byte |
| 001B1B30 | `em_sdf_001B1B30`: 001B1630 over the camera (`em_area11_interaction_host_visible_001B1630`), 001B1B70 onto the collision world's lists (class 0x85: the interactive list) |
| +0x4C (001CAA00) | the actor draw chain draws the runtime's model at its node palette (`em_area11_door_draw`) |
| 001AFC10 | the pool free |

The Use scan (00184BA0) sees the published door through the interaction
host (`em_area11_interaction_host_bind_door`: the EMIS record bound to the
pool record's +0x00 / +0x02 / +0x0B), selects it with 00183EF0's class-5
branch (`em_door_candidate`), arms +0x0B bit 2 and claims the shared player
for the door's script (the script owner token is the record). 0x1AE040
state 4's 001AFCF0 clears 3B8D and 3B8F under the held player; the shared
runtime keeps the hold (`EmInteractionRuntime.acquired`) and releases on the
cleared 3B8D, as 0015BA50's +4 == 4 path and 0015B530's 00182DF0 do. State
4's D_008101E4 = 0 reaches the camera byte (its one storage, the live
camera's +0x04) at its 0018D7B0 call.

**Retired with the binding:** the manifest door in AREA11 (em_scene.c places
no legacy em_door there), em_door.c's S12b room-move adapter
(`door_bind_original`, its 001BC150 commit and sub-5 wait, the
`EM_ROOM_MOVE_TEST` hook `em_door_room_move_request_test`), the AREA11
camera's door cinematic stand-in (`camera_area11_standins`) and state 4's
`g.doorcam = 3`.

**Evidence.** The level smoke's `fence_door` phase (LEVEL_SMOKE.md) plays
route beat 09 from the truck crossing's end and equals the capture row for
row from the Use scan (f309) to its end (f532): spad, camera byte, letterbox,
message, power, B0/B1, the player's placement, heading and record (+5,
+1F0, +1F1, clip; the clock from clip 0x45), the door record's +0x00..+0x0F
and +0x1F0..+0x1FF, the room move (B5..B9 and the fade block from f407,
001AD010 against the executed original, the nine state-4 calls, the
weather and title nodes) and the follow camera from the re-place.

**Limits.**
- Side 1 (entry 1) is not captured. Its arrival's walk-out (001B07C0 writes
  5/1/0) is still em_door.c's legacy walk-out, ticked by the door's node
  (`em_door_legacy_walkout_tick`).
- The locked program (subtype 0x15, 0x24DEC0) faults: AREA11's door is
  subtype 3.
- The door id's bit 7 (001B0C00, a whole-area change) faults: the exported
  row is AREA11's room move.
- The door's bone slots (+0x110) hold no pose: the runtime's palette is the
  pose the draw uses (no port reader of the slots exists).
- The draw is the port's actor draw of the model at the palette, as for the
  boxes; the VU1 object kernel stays with RENDER (OWNER_DRAW.md).
