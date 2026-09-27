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
| +0x4C (001CAA00) | `em_area11_boxes_door_draw`: the runtime's node palette (the nodes' +0x90) goes to the door's bone slots, then `em_owner_draw_live_001CAA00` builds the original unit of the bank's model 0x14, drawn through `em_gfx_object_unit` (OWNER_DRAW.md sections 6..10) |
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
- The locked program (subtype 0x15, 0x24DEC0) faults: AREA11's door is
  subtype 3.
- The door id's bit 7 (001B0C00, a whole-area change) faults: the exported
  row is AREA11's room move.
- The door's bone slots (+0x110) receive the runtime's palette at each
  +0x4C (the nodes' +0x90, which the runtime test compares with the
  first-control capture word for word); only the draw reads them. The
  runtime's model is no longer uploaded as a mesh.

## Side 1: the arrival walk-out (live, 2026-09-27)

Pressing Cross at the door from behind the fence (side 1) moves the player
to entry 1 (413.7, 184.84, 299.4), heading 0. The spawn record of entry 1
has the walk-out byte (+0x14) set, so state 4's 001B07C0(1) writes the
player's +4 = 5, +5 = 1, +6 = 0. The original capture is the decomp's C7
DOOR1 (`c7_door1_fence_door_side1`, CAPTURES_C7.md section 4).

The walk-out is the player's own stage, with no door code involved:

| Original | Port |
|---|---|
| 001B07C0(1)'s +4 / +5 / +6 | `spawn_commit` (em_scene_bindings.c) writes them into the live record |
| 0015BA50 +4 = 5 | `em_player_stage_dispatch`: the advance by +0x34, then major[5] |
| 0015B610 | `em_player_stage_0015B610` (em_player_floor.c), bound by em_player_stage_live over the stage's 00182B30 / 00174A50 / 00182D70 |
| 00183240 (+5 = 0) | an empty leaf (byte-matched) |
| 00183250 (+5 = 1) | `em_player_00183250` (em_player_floor.c), bound by em_player_closure_live: 0017B490 (`em_player_closure_live_0017B490`), 001749A0 (the record pose), 00178B90 (the recovery lane's translate), 00174A50, 00175900 (the floor service), 0x70003B8D through the stage's view |
| 001833F0 / 00183440 / 001834E0 (+5 = 2..4) | not translated: reaching one faults (no first-level spawn record or script writes them) |

00183250 runs the walk-out's phases:
- 1 frame starts clip 2 (0017B490(p, 1, +0x235, 2)) at speed 0.3;
- 51 frames stand (the timer counts down from 50);
- 31 frames: 30 moves (00178B90 along +0xC4 by +0x38), then the hand-over
  frame with no move (f453);
- 31 frames: 30 moves while +0x38 drops by 0.01137 per frame, then the exit
  frame, which returns +4 = 1, +5 = 0, +6 = 0, +0x1F0 = 0 and 0x70003B8D =
  0.

When +0x38 falls below 0 it is cleared and 00174A50(p, 12.0) requests the
row's clip. This happens on the 27th slowing frame (f480) and on every
slowing frame after it. The whole walk-out is 114 frames, f371..f484 in the
capture.

The door script's takeover still holds the player when 001B07C0 re-states
it. 0015B530's 00182DF0 never runs for that takeover (+0x1F0 stays 0x41
until f484). So the pose host's hold and the script owner's token end at
the re-place (`player_pose_takeover_restated`), without touching the
record.

**Evidence.**
- `tools/test_player_floor_reference.py` executes the original 0015B610
  (the "stage" cases: every +5 routine, selector 0 / 4, 00182B30's pass,
  both admission branches) and the original 00183250. The 00183250 cases
  are random phases, timers and speeds, plus the whole walk-out on one
  record: 114 frames, 60 moves, and the first 00174A50 on frame 109.
  Every record byte, 0x70003B8D and every callee call with its arguments
  are compared.
- The level smoke's `fence_door_side1` phase (LEVEL_SMOKE.md) equals the
  capture row for row from the Use scan (f228) to its end (f544):
  - the alignment, the program with clip 0x43, the room move with B7 = 1,
    the re-place at entry 1;
  - the walk-out's 52 standing frames, 30 walking, 30 slowing, the stop at
    f480 and control at f484, with the clip and its clock;
  - the door record;
  - the follow camera.

**Retired:** the AREA11 hook of em_door.c's legacy walk-out
(`em_door_legacy_walkout_tick` on the door's node, and the walk-out that
`em_door_room_move_arrival` armed at the re-place). `em_door_room_move_arrival`
and the S12b stage release `em_door_movement_stage_release` are removed with
it: without a legacy door in AREA11 they had nothing to release. The legacy
walk-out stays only for the legacy doors of the scenes without an original
roster.
