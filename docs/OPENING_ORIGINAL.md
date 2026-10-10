# The New Game opening on its original records

Status (2026-09-28, chain C8b OPENING, design risk 2 closed): **live.** The
AREA11 opening runs as the original runs it: the controller 00823E80 starts
its script 0x828FC0 on the AREA11 script host, the script's op14 spawns the
two opening actors as pool records, and the player plays the opening on its
own record and stage. Nothing draws the opening from a baked track any more.
Section 4 is the live proof.

| File | Role |
|---|---|
| `src/game/em_area11_opening.{h,c}` | 00823E80, the whole function (`em_area11_opening_tick`; ground truth the decomp's byte-identical func_overlay_AREA11_00823E40.c; oracle `make test-area11-opening-reference`) |
| `src/game/em_area11_bindings.c` | `tick_opening` (the controller's workers on its record), `tick_opening_actor` (callback 001BB0E0), `em_area11_bindings_001BAC00` (op14's spawn over the pool) |
| `src/game/em_area_script.{h,c}` | op14 admitted: 001BA1F0 dispatches it to the `w_001BAC00` worker |
| `src/game/em_area11_script_host.{h,c}` | the opening's image (0x828F30..0x8292C0), `w_001BAC00`, 0022EC30 / 0022EEF0 on the opening's track (scene 0x22) as on Roger's, the camera's w lanes |
| `src/game/em_cinematic_playback.{h,c}` | 0022EC30 / 0022EEF0 / 001B7B30 sub 0, the scripted timeline (CAMERA_LIVE.md section 5; oracle `make test-cinematic-playback-reference`) |
| `src/game/em_area11_roger.{h,c}` | the second pair of records: the opening body and its class-8 node (`em_area11_roger_opening_tick`) |
| `src/game/em_opening_runtime.{h,c}` | the opening's lane: the New Game request, the busy state, the scene-0x22 timeline's track and its D_0026AE00 table |
| `tools/level_smoke_opening.py` | the level smoke's checks against the opening capture and the per-frame samples (section 4) |

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
- **States 2 / 3:** 001AFC10(self). **Any other state:** returns.

State 0 does nothing else: it calls no item or prop service (chain step
A11FIX, 2026-10-02, removed the port's em_pickup_prop_retire there; the
legacy manifest instance of the canopy is retired when the roster spawns the
record, em_area11_bind_roster, see OPENING_SCENERY.md).

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

- **The controller.** `tick_opening` runs `em_area11_opening_tick` (the
  whole function; its +0x00 / +0x04 / +0x05 / +0x2E are the EmActor's
  `status`, `u04[0]`, `u04[1]` and `flags2`) with these workers:
  - 001B0FD0 / 001C6380 / the +0x4C 001CAA00: em_area11_boxes' owner
    services on the record; 001B1B70: the collision world's class lists;
    001AFC10: the pool's free (states 2 / 3, not reached in the first
    level);
  - 001BA1C0: the canonical D2 flag byte;
  - 001BA1A0 / 001BA1F0: `em_area11_script_host_start` / `_tick` on the
    controller's record;
  - 001FABB0, 001FAE70: the scene bindings;
  - D_00810811: `g.opening_complete`;
  - 001C4760: `em_director_original_001C4760_scene` (the key byte
    D_00810CC3[0]);
  - 001AEE10: the transition fade.

  The completion also ends the opening lane's busy state
  (`em_opening_runtime_complete`). `make test-area11-opening-reference`
  executes the original 0x823E80 over every +0x04 value, +0x05 0..3, the
  three callee results and the record bytes, and compares the calls (with
  their arguments), +0x00 / +0x04 / +0x05 / +0x2E and D_00810811 (quick: 160
  of 2,144 cases; EM_TEST_FULL=1 all).
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
  - The host's 0022EC30 resolves bank 0x98's clip 0 and starts the
    original timeline on it (em_cinematic_playback_start for scene 0x22:
    the +0x80 table D_0026AE00, the start clock D_00275C98), over the track
    and the table em_opening_runtime loaded with New Game's scene
    (opening_camera.emcc, opening.emfx), its head checked against the
    exported track's duration.
  - The camera stage's +4 == 3 frame is the original 0022EEF0 on the host
    (em_area11_script_host_camera_0022EEF0, since chain step CAMERAS,
    2026-10-02; CAMERA_LIVE.md section 5): the cue 001B1E20(6, 0) at the
    cursor 1.0 through em_pad_actuator, the +0x80 records (the -1 record at
    0, the fade-out 001AEDE0(16, 0) at 634), the sample, the 001DD980
    publication, the up vector and the 001D25F0 zoom, the 0.5 advance and
    at the end the original's restores. Until then it was the opening
    lane's stand-in (em_opening_runtime_camera_sample with
    em_opening_media's fade track, census L33).
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

- **The drive's timing** (the user's policy). At host speed the stream
  request's read completes at once, so the script's op14 spawns the actors
  21 frames before the original's AE+31 (with the PS2 disc-drive timing
  switch on the original's frame since 2026-10-09; 11 frames before
  then). The opening ends that much earlier (RAND_ORDER.md section 3).
  This is also the side-by-side video tool's "early" fade-in and first
  subtitle (decomp VIDEO_COMPARE.md: 20 ticks at host speed, 12 with the
  switch): relative to the timeline's cursor the fade and the camera are
  the original's on every captured frame (section 4, the timeline), so
  the lead is before the timeline starts, in the stream request's wait
  (07/12's handshake), not in 0022EEF0 or the fade path. With the switch
  on nothing is left since 2026-10-09: the opening music's extra seek after
  the New Game's last module-loader read (not the intro movie's position)
  is in the drive model (17 fields, IOP_STREAM.md "Drive model";
  LAUNCHER_OPTIONS.md, the drive switch). Before, 11 or 12 frames were
  left.
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
- **The camera timeline against the original's per-frame samples**
  (check_opening_timeline, `tools/level_smoke_opening.py`, chain step
  CAMERAS). Every tick on top mode 3 from the timeline's first frame to
  its tear-down (1,293 ticks) holds the original's camera block of the
  same cursor byte for byte (newgame_samples.jsonl from the start,
  cinematic_samples.jsonl from cursor 135 to the end; 1,289 compared, 4
  cursors not sampled), the event cursors +0x7C..+0x8A included, and the
  transition record after each compared tick is the frame's: the +0x80
  fade-out 001AEDE0(16, 0) fires in the frame that samples the cursor 634,
  as in the original. At
  the opening capture's cursor the whole block, D_008105D0..FF (eye,
  target, up) and the render context's zoom +0x2468 equal opening_ee.bin.
  Before this step the +0x80 cursor and +0x89 state differed on all but
  25 of those ticks.
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
