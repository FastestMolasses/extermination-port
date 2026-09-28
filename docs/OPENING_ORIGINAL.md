# The New Game opening on its original records

Status (2026-09-28, chain C8b OPENING, design risk 2 closed): **live.** The
AREA11 opening runs as the original runs it: the controller 00823E80 starts
its script 0x828FC0 on the AREA11 script host, the script's op14 spawns the
two opening actors as pool records, and the player plays the opening on its
own record and stage. Nothing draws the opening from a baked track any more.
Section 4 is the live proof.

| File | Role |
|---|---|
| `src/game/em_area11_opening.{h,c}` | 00823E80's state 1 (its script machine), translated from the original instructions |
| `src/game/em_area11_bindings.c` | `tick_opening` (the controller's states 0 / 1 and its tail), `tick_opening_actor` (callback 001BB0E0), `em_area11_bindings_001BAC00` (op14's spawn over the pool) |
| `src/game/em_area_script.{h,c}` | op14 admitted: 001BA1F0 dispatches it to the `w_001BAC00` worker |
| `src/game/em_area11_script_host.{h,c}` | the opening's image (0x828F30..0x8292C0), `w_001BAC00`, 0022EC30 on the opening's track, the camera's w lanes |
| `src/game/em_area11_roger.{h,c}` | the second pair of records: the opening body and its class-8 node (`em_area11_roger_opening_tick`) |
| `src/game/em_opening_runtime.{h,c}` | the opening's lane: the New Game request, the busy state, the scene-0x22 camera timeline stand-in (census L33) |
| `tools/level_smoke_opening.py` | the level smoke's check against the opening capture (section 4) |

## 1. What the originals do

**00823E80** (the controller, area11[10]) runs, by its +0x04:

- **State 0:** 001B0FD0, 001C6380, then +0x04 = 1 and +0x00 = 1.
- **State 1:** 001BA1C0(self, 0x39). When D_00810758[0x39] is 0xFF,
  nothing more. Otherwise, by its +0x05:
  - **0:** 001BA1A0(self + 0x1F0, 0x828FC0), 001FABB0(), +0x05 = 1.
  - **1:** 001BA1F0(self). A nonzero result (1 finished, 3 the skip
    path) runs the completion: +0x2E = 0xFFFF, D_00810811 = 0xFF,
    001C4760(0, 1), 001FAE70(0), +0x05 = 2, 001AEE10(4, 0).
  - **Any other value:** nothing.
- **Every state-1 call** ends with 001B1B70(self) and its +0x4C (the canopy's
  001CAA00).

**The script 0x828FC0** runs these records in order (op / sub):

1. 07/12, the frame with the stream handshake. In phase 2, 001B81D0
   attaches the player's face once 0015B130 admitted the player
   (3B8F != 0).
2. 06/0 on flag 0x39.
3. 0C/1 with line 0x66.
4. 0A/1: bank 0x98 on the player (+0x40), clip 1 at rate 0.5, +0x2F3 = 1.
5. 14: 001BAC00 over the placement list 0x828F30.
6. 00/6: the camera timeline, scene 0x22, bank 0x98's clip 0.
7. 0D/0: waits for the timeline's end.
8. 18.
9. 0A/5.
10. 01/9.
11. 00/0.
12. 07/5 on flag 0x39.

Records 4 to 7 chain in one tick (flag 0x20000000).

**001BAC00** spawns two records through 001AFA90. Each gets the
behaviour 001BB0E0, +0x20 = its entry and +0x24 = the controller:

- **Entry 0x828F30:** class 9, model 0x47, bank 0x98, clip 2, command 0,
  step 0.5.
- **Entry 0x828F5C:** class 8, model 0x6B, command 5.

001BB0E0 then runs each record:

- **Phase 0:** 001BAD40 binds it.
  - Command 0: 001CA6E0 on D_0028A490[0x47]; +0x40 = D_0028A490[0x98];
    21 bone slots; 001BA8E0 (the face slot and the head-sprite node
    001F0120); 001CA6F0(2); 001C63E0(clip 2).
  - Command 5: 001C5C90 on the class-8 record, over its +0x18, the body.
  - In both cases phase 0 falls through into phase 1.
- **Phase 1:** once the controller's +0x2E mask names the record, +0x04 = 2.
  Otherwise, per command:
  - Command 0: 001BA580 (the face 001D0720, the shadow 001DA6A0),
    anim_advance_time(0.5), 001C68C0, +0x01 = 1, then +0x4C (001CAA00
    with the face unit 001CB3C0).
  - Command 5: 001C5C90 (the node copies the body's bone 1 and draws).
- **Phase 2:** +0x04 = 3. For the body, 001BA540 releases the face.
- **Phase 3:** 001AFC10 frees the record.

**The player** plays the opening on its own record:

- **The first stage after the 001AF5C0 wipe** is 0015BA50's +4 = 0 call.
  Its switch is 0015C420 alone: no advance and no state callback.
- **The idle state** runs from the next stage.
- **The takeover.** Once the script's 07/12 sets 3B8D = 2, 0015B130's
  prelude admits the player (+4 = 4, 3B8F = 1). Every stage after that is
  0015BA50's +4 = 4 path: 00183090 plays the special bank's clip, and
  001D0C70 ticks the face once 001B81D0 has set 3B8F = 2.
- **The post-step** 0015C160 draws the shadow and the unit from the record.
- **The release.** After 07/5 clears 3B8D, 0015B530's 00182DF0 releases the
  player (the nonzero-+0x2F3 branch).

## 2. Binding (done 2026-09-28)

- **The controller.** `tick_opening` runs `em_area11_opening_state1` with
  these workers:
  - 001BA1C0: the canonical D2 flag byte;
  - 001BA1A0 / 001BA1F0: `em_area11_script_host_start` / `_tick` on the
    controller's record;
  - 001FABB0, 001FAE70: the scene bindings;
  - D_00810811: `g.opening_complete`;
  - 001C4760: `em_director_original_001C4760_scene` (the key byte
    D_00810CC3[0]);
  - 001AEE10: the transition fade.

  Its +0x05 and +0x2E are the EmActor's `u04[1]` and `flags2`. The
  completion also ends the opening lane's busy state
  (`em_opening_runtime_complete`).
- **The script.**
  - The host loads the opening's image `assets/scene_snow/opening.emsc`
    (tools/export_area11_opening.py; base 0x828F30, entry 0x828FC0, 0x390
    bytes) when a start names it, and drops it at every area build.
  - em_area_script admits op14. The host's `w_001BAC00` calls
    `em_area11_bindings_001BAC00`: em_sdf_001BAC00 over the owner's record
    with the pool's 001AFA90. Each spawned record then takes the entry's
    stores and is bound by its +0x10, and its node keeps +0x20 / +0x24.
  - The script host claims the shared player token when 3B8D != 0, as for
    every script owner.
- **The camera.**
  - 001B8FC0 kind 6 writes the camera's +0x6E / +0x70 / +0x74 / +0x78 (the
    canonical words).
  - The host's 0022EC30 resolves bank 0x98's clip 0. For that track, the
    opening lane starts its timeline (em_opening_runtime_camera_start,
    which checks the head against the exported track's duration).
  - The camera stage's +4 == 3 timeline samples it at the camera's +0x74
    and advances it by 0.5 (the stand-in for 0022EEF0's scene 0x22, census
    L33, with the fade track and the rumble at 1.0).
  - The host's four-lane views now carry the w lanes of the camera's +0x10 /
    +0x20 and of D_008105D0 / E0 in the live camera's own bytes
    (em_camera_live_bytes). 001B8FC0's quad copies write all four words.
- **The actors.** `tick_opening_actor` hands the record, its entry (read from
  the opening's image) and the controller to
  `em_area11_roger_opening_tick`. It runs em_slg_001BB0E0 with these
  workers:
  - 001BAD40: em_sdf_001BAD40 over the record's bytes, with em_roger_actor's
    001CA6E0 / 001C6150 / 001AF780 / 001BA8E0 / 001CA6F0 / 001C5C90 and the
    pose host's 001C63E0;
  - 001BA580 / 001BA540: em_roger_actor;
  - anim_advance_time: em_player_stage_anim_advance over the body's pose
    host;
  - 001C68C0: em_pose_host;
  - 001AFC10: the pool;
  - +0x4C: em_owner_draw_live with the face attachment.

  The body and its node are em_area11_roger's second pair (`R.pair[1]`):
  the same workers as Roger's own pair, run on whichever pair the call
  works on (`R.cur`). 001F9660 (command 6) is not in the opening's list and
  faults.
- **The player.**
  - w_0015BCF0 runs `em_player_0015BCF0` in the cutscene variant as in
    gameplay. The UM_0015BCF0_CUTSCENE exception is gone.
  - The first gameplay call after the wipe spawns 0015C420's children and
    marks the stage as the +4 = 0 call (`player_states_stage_rebuild`).
  - The pose host's source is the record's from `player_pose_attach` on.
  - `em_game_player_interact_busy` no longer counts the opening: the port's
    stand-in lock that held the player there is gone. The takeover owns
    the player now.
  - `em_scene_bindings_player_record_drawn` reads only whether the record
    holds the display.
- **Retired:**
  - src/game/em_opening_actor.{c,h} and its assets
    (`assets/scene_snow/opening/*.emdl`, `*_face.emfm`, from the decomp's
    export_opening_actors.py / export_opening_faces.py: no longer read);
  - em_opening_runtime's script executor and actor draw;
  - `player_pose_opening_release`;
  - `em_opening_runtime_tick` / `_actors_active` / `_half_tick`;
  - the render frame's opening draws;
  - `em_opening_face_position`, the host-float face morph the baked actors
    drew with (a second translation of the VU1 face program's morph,
    VU1_FACE_MORPH.md section 6), with test_opening_face_reference's
    `--morph-assets` part and test_vu1_face_morph_reference's part-E count
    of its differences;
  - tests/opening_actor_test.c and `test-opening-actor`;
  - tests/opening_runtime_test.c and `test-opening-runtime`.

## 3. What stays a stand-in

- **The scene-0x22 camera timeline** (census L33). The opening lane samples
  the exported bank-0x98 camera track and runs the fade track. Its eye /
  target sampling, the camera's +0x80 event cursor and +0x89 are not the
  original 0022EEF0's: at the opening capture's cursor, the camera block
  differs from the capture at +0x14 / +0x15, +0x24, +0x80..+0x82 and +0x89.
  The timeline words +0x6C..+0x7B are the original's.
- **The drive's timing** (the user's policy). At host speed the stream
  request's read completes at once, so the script's op14 spawns the actors
  21 frames before the original's AE+31 (11 with the PS2 disc-drive timing
  switch). The opening ends that much earlier (RAND_ORDER.md section 3).
- **D_008106B3** still takes the port's stand-in gate
  (`em_opening_runtime_busy` in em_player_frame.c), as before (FIRST_CONTROL.md).

## 4. The live proof

- **The script against the original interpreter.**
  `make test-area-script-reference` runs 0x828FC0 over the first-control RAM.
  The original 001BA1F0 and its handlers run on one side, em_area_script on
  the other. Every tick's result, worker calls and modelled bytes are
  compared:
  - the script ends after 22 ticks;
  - three skip variants take the abort path after 12, 18 and 21 ticks.
- **The actors against the opening capture** (the level smoke's
  check_opening_actors, `tools/level_smoke_opening.py`). It uses the port
  tick whose camera holds the opening capture's timeline words (scene 0x22,
  cursor 135 of 646). At that tick:
  - the body at 0x7A96E0, the class-8 node at 0x7AE920 and the head sprite
    after them sit at the capture's records;
  - their node matrices are the capture's bit for bit: the body's 21, the
    node's 1 and the player's 21, with their lighting points;
  - the player's row equals the capture's (+4 = 4, +2F3 = 2, +1F0 = 0x41,
    +3C = 511);
  - over the run, the body draws with its face unit (and the node with it)
    on all 1,293 ticks from its spawn to the done mask, before first
    control, and neither draws after.
- **The rand() order** (check_rand_order, RAND_ORDER.md section 3).
  - Every call equals the original's in caller and state from the area
    entry to the actors' spawn (227 calls at host speed):
    - the security gun's AE+1 draw;
    - Roger's owner's face at AE+2;
    - the player's face in the player stage after the barrel from AE+5.
  - From the spawn, each frame's callers equal the original's frame the
    drive's shift later. This holds for 32 frames, until a value-driven
    timer differs. It includes the body's first face tick and the head
    sprite 001BA8E0 spawned.
- **The frame order.** compare_frame_order runs with the three opening
  allow entries removed:
  - cut02 PASSes event for event from native index 26 (the spawn);
  - st03 PASSes at native index 1321 (the records' last walk);
  - idle04 and walk04 PASS at 1330, as before.
- **The first control.**
  - newgame-control: 9.599849 over 30 ticks, census 49 (unchanged).
  - The level smoke's first_control check: the state-0 frame's camera
    equals newgame_samples frame 2640, the hand-off settle frames
    4004..4027 byte for byte. The camera's timeline words are no longer
    exempt.
