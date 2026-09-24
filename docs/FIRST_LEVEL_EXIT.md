# First-level exit (route beat 15)

Date: 2026-09-23 (session s87, lane "emu-exit-and-io"). The route of FIRST_LEVEL_ROUTE.md
stopped when Roger's encounter released control (beat 14), so the level exit was neither
captured nor in the census (FIRST_LEVEL_CENSUS.md "Not covered: the level exit"). This
document adds it. Everything below was measured on the ORIGINAL game in the hidden PCSX2
through `../Extermination/tools/route_capture.py` and `tools/route_census.py`; it names
addresses, values and frames only.

## Beat 15: the level exit

### Route table row

Same conventions as FIRST_LEVEL_ROUTE.md section 3 (trace frames `f`, main-loop counter `c`).

| # | Beat (folder) | Source | Frames | Counters | Original owners and scripts | Port owner |
|---|---|---|---|---|---|---|
| 15 | `15_level_exit` | 14 | 801 | 15761..16562 | fan r2 00827630 (record [2]): Z < 156 exit-or-bit, D_008107D8 \|= 0x80; Roger r8 departure (runtime 0x823C40 starts script 0x828A10; 0x823C80 calls 001B0C60(1, 0, 4)); script op 0F plays movie selector 1 (`MOVIE/E001.PSS`); 001AD010 → 001ADF50 → 001FF080(1, 0) → 001FFCD0 area load; AREA01 sub 0 spawn entry 4 | em_fan_original (verified-unbound), em_roger (not bound, H3), em_area_script, em_scene_task (001AD010/001ADF50 live), em_scene_request_area_change_001B0C60; AREA01 not exported |

The main line becomes 01 → … → 13 → 14 → 15. The snapshot
`../Extermination/build/s87/route/15_level_exit/state.p2s` (with `eeMemory.bin`, `gs.bin`,
`scratchpad.bin`, `original.png`) is the first AREA01 state with control, and it resumes
(`route_capture.py run` verified it).

### 1. What the player does

Beat 14 ends on the west tower top at (338, 289.75, 192), yaw −2.53073, with
D_008107D8 = 1 and D_00810758[0] = 1. The fans are pool slots 10 and 11 (0x7A73A0 and
0x7A7690, records [1] and [2], both at (329.3, 309.3, 159.8 / 160.8)).

1. f0..f47: stick toward (331, 177), then (329.5, 172) at half magnitude. The player stops
   at (328.6, 290.06, 167.77) (action 2/4 at f50..f70, idle from f71). This is outside
   fan r2's hit band (Z < 166.5 with the fast arm).
2. f71..f303: idle while fan r2 runs its cycle: phase 2 (spin-up) from f31, phase 3 (hold)
   f152, phase 4 (spin-down) f182, phase 0 at f303. The beat waits for **phase 1 with the
   timer ≥ 55** (spin 0, the slow arm, which has no hit): phase 1 starts at f304 (timer 60).
3. f304: stick toward (329.5, 150). The walk starts at f320 (action 1). At f344 the player
   is at (330.24, 290.48, 155.89): Z < 156 inside Y (280, 320), X (318, 340). Fan r2 is in
   phase 1 (spin 0). The stick is released at f344; there is no other input in the beat.
4. Everything after f344 is automatic.

### 2. What the original does, frame by frame

| Frame (counter) | Event |
|---|---|
| f344 (16105) | Fan r2's exit-or-bit: D_00810758[0] = 1 (not 0xFF), so **D_008107D8 = 0x81** (no area request from the fan). In the same frame Roger r8 (0x7A8830) takes the departure branch and starts script **0x828A10** (runtime 0x823C40, first executed here). |
| f345 | 3B8D = 2, camera byte 0x8101E4 = 2 (no letterbox bars). |
| f346 | 3B92 = 1; player action 0x41, clip 0x4B; fade machine 0x28A9A0 state 3 (fade to black). Script record 0x828A90. The player holds (330.29, 290.52, 155.09) for f345..f348. |
| f348..f433 | The departure script walks the player: yaw −3.13389 at f348, −3.1312 from f349; from f349 Z falls by about 0.3305 per frame (154.76 at f349, 137.91 at f400) and the player arrives at (330.0, 289.0, 127.0), yaw −3.1312, at **f433** (no teleport: every frame of the walk is in the trace). |
| f430 | Fade state 2 (black, held to f446). |
| f434 | Player stands at (330.0, 289.0, 127.0) (reached at f433); script record 0x828AD0, then 0x828B10 (f435), 0x828B50 (f439), 0x828B90 (f444): 0x40-byte records, 7 in all (the op list of AREA_SCRIPT.md: 07/0, 10/1, 01/3, 10/5, 10/3, 0F, 07/5). |
| f440..f444 | Op 0F (001B7A30, from the interpreter, record 0x828B50) polls each frame. |
| **f442** | **Movie frame.** D_00275C78 = 1 after it; the main loop's movie arm calls 00203350 (return address 0x1AAFDC, see the note below the table) and plays selector 1 = `MOVIE/E001.PSS` inside this one frame (PCSX2 log "FMV started" … "FMV ended"; 79 to 254 s of host time in the three runs). D_00821058 is 0 again at the frame boundary. |
| f445 (16206) | Script ends (record 0x828B90, op 07/5): **D_00810758[0] = 0xFF**. Roger lifecycle 3, and **001B0C60(1, 0, 4)** is called with return address **0x823CA8** (inside runtime 0x823C80): 3B8D = 3, 3B92 = 0, camera byte 0, **B5..B8 = 01 00 04 01**. 001B0C00 and 001FAD70 run in the same frame. |
| f446 | 001AD010 (from the 0x1AE040 frame machine, return address 0x1AE31C, fade substate 2): **D_00810700/701/702 = 01/00/04**, slot 0 +9 = 5. Roger r8 and attachment r9 freed (lifecycle 0). |
| f447 | 001ADF50: fade record cleared, **001FF080(1, 0)** (return address 0x1ADFBC): slot 2 state 2, +8 = 1 (001FFCD0 path), D_00275BD8 = 1. |
| f453 | The resident overlay header at 0x823500 changes from id 9 (AREA11) to **id 2**. |
| f457 | D_00810703 = 01 (was 0B). |
| f658 | Loader done (+8 = 0x63); f659 BD8 = 0, slot 2 idle. |
| f660..f739 | Slot 0 +A = 2 (001ADF50's post-load wait; the veil 0021B840 first runs at f660). |
| f740 | Slot 0 +9 = 1, +A = 0, +B = 0; fade state 2. |
| **f741 (16502)** | **AREA01 arrival**: state-0 rebuild (001AFCA0, 001B07C0, 001B6990, 001AEE40, 001FAE70, 001C5C50 run here). Player at **(41.0, 0.0, −565.6), yaw −1.43411** (spawn entry 4 of AREA01 sub 0), 3B8D = 0, action 0, slot 0 +B = 1, fade 1 (fade-in, 01000005 → 01000006 → 01000002). Control is back in this frame. D_00282157 goes 1, 2 at f741/f742 (the stream starts). |
| f742 | Camera byte word 0x8101E4 = 00000800 from here on. |
| f801 (16562) | End (60 idle frames). The snapshot's screenshot shows the AREA01 area title card ("UNDERGROUND TUNNEL - AREA B") over the arrival view. |

Return addresses in this table (0x823CA8, 0x1AE31C, 0x1ADFBC, 0x1AAFDC) were read from the return-address register at the callee's entry breakpoint in a separate caller-check run of beat 15; that run's output was not kept. The receipt that is kept is a static check: `../Extermination/build/s87/route/15_level_exit/return_sites.json` (ignored) records that in the RAM of the beat-14 end snapshot (the code resident when beat 15 starts) the word two before each return address is a direct call to the named callee, and that the call site lies inside the named caller (0x823C80 for 0x823CA8; 001AE040 for 0x1AE31C; 001ADF50 for 0x1ADFBC; the main loop 001AAE40 for 0x1AAFDC). The static check proves each site calls that callee; that these sites were the ones taken in beat 15 rests on the caller-check run and on the frame-by-frame state above.

From the fan crossing to control in AREA01: 397 frames (f344 → f741), plus the movie inside
f442. From the area request to control: 296 frames, all under black.

### 3. The area change and the AREA01 load

Boundary-sampled task records (slot 0 at 0x28A750: +8/+9/+A/+B; slot 2 at 0x28A790). The
step meanings are the decomp's readable C of 001FFCD0 (NEARMISS; a label, cross-checked
here only by the step order).

| Frames | Slot 0 (+8, +9, +A, +B) | Slot 2 (+8, +9, +A, +B) | BD8 | Note |
|---|---|---|---|---|
| f440..f445 | 3, 1, 0, 1 | 0x63 idle | 0 | world frame |
| f446 | 3, 5, 0, 0 | idle | 0 | 001AD010 |
| f447..f453 (7) | 3, 5, 1, 0 | 1, 1, 0, 0 | 1 | step 1: whole-file read poll |
| f454 (1) | | 1, 2 | 1 | step 2: header read kick |
| f455..f456 (2) | | 1, 3 | 1 | step 3: header poll |
| f457 (1) | | 1, 4 | 1 | step 4: open phase A |
| f458 (1) | | 1, 5 | 1 | step 5: first block kick |
| f459..f499 (41) | | 1, 6 | 1 | step 6: first block poll |
| f500 (1) | | 1, 7 | 1 | step 7: first fix-up |
| f501 (1) | | 1, 8, 0, 0 | 1 | step 8: open phase B |
| f502..f511 (10) | | 1, 8, 0, 4 | 1 | |
| f512..f536 (25) | | 1, 8, 0, 5 | 1 | |
| f537..f554 (18) | | 1, 8, 1, 2 | 1 | |
| f555 (1) | | 1, 8, 1, 3 | 1 | |
| f556 (1) | | 1, 9 | 1 | step 9: second block kick |
| f557..f656 (100) | | 1, 10 | 1 | step 10: second block poll |
| f657 (1) | | 1, 11 | 1 | step 11: second fix-up |
| f658 (1) | | 0x63 | 1 | |
| f659 (1) | | idle | 0 | BD8 cleared |
| f660..f739 (80) | 3, 5, 2, 0 | idle | 0 | |
| f740 | 3, 1, 0, 0 | idle | 0 | |
| f741 | 3, 1, 0, 1 | idle | 0 | control |

D_00282157 is 0 from f446 to f740. The area loader runs for 212 dispatches (f447..f658).
No per-poll return values were taken for this load (STATUS_LOAD_WAIT_PROBE.md explains why
the boundary sampling is the unperturbed reference).

### 4. Owners and exit targets

- **Two exits, one taken.** Fan record [2]'s exit-or-bit calls 001B0C60(1, **1**, 4)
  (AREA01 sub 1, entry 4) only when D_00810758[0] == 0xFF, that is after Roger's departure
  has run once. On the first pass (this route) it sets D_008107D8 |= 0x80 instead, and the
  exit is **Roger's departure: 001B0C60(1, 0, 4) = AREA01 sub 0, spawn entry 4**, captured
  with those arguments. The departure also stores D_00810758[0] = 0xFF, so a later return to
  AREA11 would take the fan's direct exit to sub 1. The sub-1 exit is not on the played route
  and was not captured.
- **Fan pair 00827630** (FAN_ORIGINAL.md): the Z < 156 test, the slow-arm/no-hit rule and
  the 0x80 bit are what the capture shows. Port: `em_fan_original` (verified-unbound; the
  live port draws the fans static).
- **Roger r8 departure**: runtime 0x823C40 (splat `func_overlay_AREA11_00823C00`, 64 bytes)
  and 0x823C80 (splat `func_overlay_AREA11_00823C40`, 96 bytes) run for the first time in this
  beat (both AU). 001B0C60(1, 0, 4) returns to 0x823CA8, inside 0x823C80. Port: `em_roger.c`
  has the departure branch (ROGER_ORIGINAL.md), not bound live (FIRST_LEVEL_AUDIT H3).
  **Naming note for the port:** `em_roger.h` already comments its completion worker
  `EM_ROGER_REMOVE_GROUP` as `1B0C60(1,0,4)`, and the capture confirms that call is the
  area-change request 001B0C60(1, 0, 4) (3B8D = 3, B5..B8 = 01 00 04 01). What misleads is
  the enum name and the ROGER_ORIGINAL.md sentence "completion removes the original actor
  group": both should be renamed / reworded to the area-change request, and the binding must
  go to the byte-matched `em_scene_request_area_change_001B0C60`.
- **Script 0x828A10** runs on the area-script interpreter; op 0F is 001B7A30 (BM, first
  executed in this beat). The movie itself is the main loop's movie arm (00203350, selector
  D_00275C78 = 1). The port's movie path is the S12a New Game one (selector 0, E900); nothing
  in the port selects selector 1 yet.
- **Area change consumer**: 001AD010 and 001ADF50 are live in the port (em_scene_task cores,
  S12a) but the native area read only knows AREA11 (0x0B/0) and faults for area 1
  (INV-02). The arrival spawn entry 4 of AREA01 sub 0 is (41.0, 0.0, −565.6), yaw
  −1.43411, as placed by 001B07C0 in the capture.

### 5. Census: the functions beat 15 adds

Beat 15 is kept out of the first-level census on purpose: `route_census.py run --segments
all` leaves it out (it runs only when named, `--segments 15`), and `route_census.py report`
never reads it, so FIRST_LEVEL_CENSUS.md's `route_functions.json`, `per_beat.json` and main
line stay beats 00..14 (a re-run of `report` after this change reproduced both files
byte-for-byte). Beat 15's functions are reported only by `exit-delta`, below.

`route_census.py run --segments 15 --pass A` replayed the beat with the one-shot breakpoint
on all 2991 candidates (2957 boot, 34 AREA11 overlay), from the beat-14 snapshot, then
`route_census.py exit-delta --passes A,B` compared it with every earlier label (S0..S3 and
00..14, passes A and B). Output: `../Extermination/build/s87/census/exit_delta.json` and
`runs/A/15_level_exit.json` (ignored).

- The replay completed: 802 frames against 801 recorded, started one frame earlier (row-0
  counter offset −1, as in section 7 of FIRST_LEVEL_ROUTE.md). Its row-by-row comparison with
  the recorded trace is **0/802 rows identical** (also 0 with counter, clock and r9 ignored),
  because the comparison is row-aligned and every row is one frame apart; and
  `recorded_inputs_equal` is **False**: 29 of the 50 input entries are identical, the other
  21 (the fan-wait release and the walk, f304..f344 recorded) carry the same stick values
  one frame later. The claim that the replay took the same path therefore rests on (a) the
  end digests: player and owners equal to the recorded snapshot, one global word (0x810D98)
  different; and (b) the event frames, which match after the −1 offset (request 001B0C60 at
  census f446 = recorded f445; arrival placement 001B07C0 at census f742 = recorded f741).
- **1020 functions ran in beat 15; 63 of them ran in no earlier label** (38,912 bytes).
  Frames below are converted to the recorded trace's numbering (counter − 15761).
- Phases: the area-change request (001B0C60, f445) and the arrival placement (001B07C0,
  f741) split them into 6 in AREA11 play and the departure, 4 in the change and load, and
  53 in the AREA01 arrival.
- Decomp status of the 63: BM 28, NM 25, AI 3, AW 2, CL 2, AU 2 (the two Roger departure
  functions), no source file 1 (0x001C0004).
- Four AREA11 candidate addresses were hit after f742 while overlay id 2 was resident; they
  are AREA01 code, not AREA11 functions, and are excluded. Three pauses at non-candidate
  AREA01 overlay addresses (0x82579C, 0x8258FC) were recorded as unexpected and ignored.
- Already seen before beat 15 (so not in the lists): the fan and Roger controllers
  (S2), the movie driver 00203350/00203460/002034C0 (S0), 001AD010 (09), 001ADF50,
  001FFCD0, 001B07C0, 001AFCA0, 001B6990, 001AEE40, 001FAE70, 001C5C50 and the veil
  0021B180/0021B550/0021B840 (S1), 001B7840 (14). 001FF830 and 001B6BF0 (op 18) do not run
  in beat 15.
- The "port files" column is a plain grep of the port's `src/` and `tools/` for the address;
  it is not evidence of a translation or of verification.

The 53 AREA01 functions are the arrival of the next level (AREA01 world, owners and
effects). They are beyond the first level except for what the exit's last frames need;
they are listed so the scope decision can be made on facts.

#### AREA11 play and the departure (before the request): 6

| Address | Splat name | Bytes | Decomp | First frame | Subsystem | Port files naming the address (grep only) |
|---|---|---:|---|---|---|---|
| 0x00194D10 | func_00194D10 | 148 | NM | f325 | init_io | `em_camera.c`, `em_camera_area11_specials.c`, `em_camera_area11_specials.h`, `em_camera_leftovers.c` (+6) |
| 0x0022FCA0 | func_0022FCA0 | 856 | NM | f325 | sdk_gs | `em_camera.c`, `em_camera_area11_specials.c`, `em_camera_area11_specials.h`, `em_camera_leftovers.c` (+4) |
| 0x00230000 | func_00230000 | 548 | BM | f325 | data | `em_camera.c`, `em_camera_follow_original.c`, `em_camera_follow_original.h`, `em_camera_leftovers.c` (+4) |
| 0x00823C40 | func_overlay_AREA11_00823C00 | 64 | AU | f344 | overlay_AREA11 | `em_script_host_workers.c` |
| 0x00823C80 | func_overlay_AREA11_00823C40 | 96 | AU | f344 | overlay_AREA11 | — |
| 0x001B7A30 | func_001B7A30 | 256 | BM | f440 | unknown_02 | `em_area_script.c`, `em_area_script.h`, `em_main_loop_and_gap.h` |

#### Area change and load (request to arrival placement): 4

| Address | Splat name | Bytes | Decomp | First frame | Subsystem | Port files naming the address (grep only) |
|---|---|---:|---|---|---|---|
| 0x001B0C00 | func_001B0C00 | 88 | BM | f445 | entity_sys | `em_area_script.c`, `em_area_script.h`, `em_door.c`, `em_door.h` (+10) |
| 0x001B0C60 | func_001B0C60 | 112 | BM | f445 | entity_sys | `em_camera_area11_specials.c`, `em_camera_area11_specials.h`, `em_camera_leftovers.c`, `em_camera_leftovers.h` (+11) |
| 0x001FAD70 | func_001FAD70 | 244 | BM | f445 | stream_music | `em_opening_media.h`, `em_scene.c`, `em_scene_bindings.c`, `em_scene_bindings.h` (+6) |
| 0x001195A8 | func_001195A8 | 168 | BM | f514 | lowmem | `em_startup_load_gaps.h`, `em_startup_load_gaps_sound.c`, `test_startup_load_gaps_reference.py` |

#### AREA01 arrival (placement at f741 to the end at f801): 53

| Address | Splat name | Bytes | Decomp | First frame | Subsystem | Port files naming the address (grep only) |
|---|---|---:|---|---|---|---|
| 0x00128390 | func_00128390 | 52 | CL | f742 | lowmem | `em_enemy.c`, `em_enemy.h` |
| 0x001289C0 | func_001289C0 | 240 | BM | f742 | lowmem | `em_enemy.h` |
| 0x00128AB0 | func_00128AB0 | 204 | BM | f742 | lowmem | `em_enemy.h` |
| 0x00128C10 | func_00128C10 | 2924 | NM | f742 | lowmem | `em_enemy.c`, `em_enemy.h` |
| 0x00158D30 | func_00158D30 | 388 | NM | f742 | entity_logic | — |
| 0x00159B90 | func_00159B90 | 724 | NM | f742 | entity_logic | — |
| 0x0015A2C0 | func_0015A2C0 | 1164 | NM | f742 | entity_logic | `em_enemy.c`, `em_enemy.h`, `em_game.c`, `em_main_loop_and_gap.h` (+1) |
| 0x001BB520 | func_001BB520 | 56 | BM | f742 | math_vector | `em_door.c` |
| 0x001BB860 | func_001BB860 | 628 | NM | f742 | math_vector | `em_door.c`, `em_door.h`, `em_scene.c` |
| 0x001BFFD0 | func_001BFFD0 | 52 | NM | f742 | math_vector | — |
| 0x001C0004 | func_001C0004 | 724 | none (no src file) | f742 | math_vector | — |
| 0x001C02E0 | func_001C02E0 | 1016 | NM | f742 | math_vector | `em_main_loop_and_gap.h` |
| 0x001C4FA0 | func_001C4FA0 | 172 | BM | f742 | math_vector | — |
| 0x001C50B0 | func_001C50B0 | 1212 | BM | f742 | math_vector | — |
| 0x001CD070 | func_001CD070 | 272 | NM | f742 | gs_upload | — |
| 0x001D0C80 | func_001D0C80 | 188 | BM | f742 | obj_registry | — |
| 0x001D0D40 | func_001D0D40 | 32 | BM | f742 | obj_registry | — |
| 0x001D4FC0 | func_001D4FC0 | 424 | NM | f742 | render_vif | — |
| 0x001D5A70 | func_001D5A70 | 344 | AW | f742 | render_vif | — |
| 0x001D5BD0 | func_001D5BD0 | 164 | BM | f742 | render_vif | `em_render_context.c`, `em_render_context.h`, `test_render_context_reference.py` |
| 0x001E3D90 | func_001E3D90 | 2160 | NM | f742 | weapon_equip | `em_sfx.h` |
| 0x001E7D20 | func_001E7D20 | 3608 | NM | f742 | stream_archive | — |
| 0x001E9580 | func_001E9580 | 2264 | BM | f742 | hud_objects | `em_enemy.c`, `em_enemy.h` |
| 0x00129780 | func_00129780 | 1916 | BM | f743 | lowmem | `em_enemy.h` |
| 0x00157CE0 | func_00157CE0 | 592 | BM | f743 | entity_logic | — |
| 0x00158590 | func_00158590 | 632 | NM | f743 | entity_logic | — |
| 0x0019B4C0 | func_0019B4C0 | 512 | NM | f743 | level_world | — |
| 0x0019CF50 | func_0019CF50 | 992 | NM | f743 | level_world | — |
| 0x001A06A0 | func_001A06A0 | 1132 | NM | f743 | level_world | — |
| 0x001BB560 | func_001BB560 | 608 | BM | f743 | math_vector | `em_door.c`, `em_door.h` |
| 0x001BF630 | func_001BF630 | 124 | AI | f743 | math_vector | — |
| 0x001C2540 | func_001C2540 | 152 | AW | f743 | math_vector | — |
| 0x001C3BE0 | func_001C3BE0 | 384 | BM | f743 | math_vector | — |
| 0x001C3DB0 | func_001C3DB0 | 764 | NM | f743 | math_vector | — |
| 0x001CB360 | func_001CB360 | 84 | BM | f743 | anim_runtime | `em_status_models.c` |
| 0x001E7CB0 | func_001E7CB0 | 100 | BM | f743 | stream_archive | — |
| 0x001E9E60 | func_001E9E60 | 932 | NM | f743 | hud_objects | `em_enemy.c`, `em_enemy.h` |
| 0x001F4A10 | func_001F4A10 | 480 | NM | f743 | fx_render | — |
| 0x001F4BF0 | func_001F4BF0 | 200 | AI | f743 | fx_render | `em_area11_interaction_host.c`, `em_status_models.h`, `em_status_scene_original.c`, `em_status_scene_original.h` (+1) |
| 0x001F4CC0 | func_001F4CC0 | 120 | BM | f743 | fx_render | — |
| 0x001287F0 | func_001287F0 | 56 | CL | f744 | lowmem | `em_enemy.c`, `em_enemy.h` |
| 0x00128B80 | func_00128B80 | 132 | NM | f744 | lowmem | `em_enemy.c`, `em_enemy.h` |
| 0x001AA000 | func_001AA000 | 312 | AI | f744 | frame_main | `em_coll_list_passes.c`, `em_coll_list_passes.h`, `test_coll_list_passes_reference.py` |
| 0x001B13F0 | func_001B13F0 | 124 | BM | f744 | math_vector | `em_enemy.c`, `em_enemy.h` |
| 0x001B2140 | func_001B2140 | 2500 | NM | f744 | math_vector | — |
| 0x001C25E0 | func_001C25E0 | 168 | BM | f744 | math_vector | `em_enemy.h` |
| 0x001C2770 | func_001C2770 | 2176 | NM | f744 | math_vector | `em_enemy.h` |
| 0x001C39F0 | func_001C39F0 | 488 | BM | f744 | math_vector | — |
| 0x001C3D60 | func_001C3D60 | 68 | BM | f744 | math_vector | `em_enemy.h` |
| 0x001C69A0 | func_001C69A0 | 1016 | NM | f744 | anim_runtime | `em_status_models.c`, `em_status_models.h`, `em_status_scene_original.c`, `em_status_scene_original.h` (+1) |
| 0x001CD180 | func_001CD180 | 304 | NM | f748 | gs_upload | — |
| 0x001CD2B0 | func_001CD2B0 | 192 | BM | f748 | gs_upload | — |
| 0x001B0D80 | func_001B0D80 | 60 | BM | f773 | entity_sys | `em_drum_original.c`, `test_drum_original_reference.py` |

### 6. Reproduce, verification, limits

Decomp repo, `.venv` python, repo root; the emulator runs hidden and is closed after each
session; no save-state slot is written (the snapshot slot is moved out of `sstates/`).

```
.venv/bin/python tools/route_capture.py run --beats 15            # capture + resume check (opt-in:
                                                                  # `--beats all` leaves beat 15 out)
.venv/bin/python tools/route_capture.py events --beats 15         # change log
.venv/bin/python tools/route_census.py run --segments 15 --pass A
.venv/bin/python tools/route_census.py exit-delta --passes A,B
```

- Beat 15 samples extra fields on top of the route sampler (fan pair phase/timer/spin/rot.z,
  D_00810758, the full area bytes 0x810700..703, task slots 0..2, D_00275BD8, D_00282157,
  the overlay header, D_00275C78 and D_00821058); beats 00..14 keep their row format. The
  beat raises the frame-step timeout to 900 s because the movie plays inside one frame; the
  beat costs several minutes of host time (the movie frame alone 79 to 254 s), which is why
  both tools run it only when it is named.
- Three independent runs (an exploratory run, the recorded capture, the census replay) and a
  fourth caller-check run took the same path: fan crossing at f343..f345, script
  0x828A10, movie in one frame, 001B0C60(1, 0, 4), AREA01 arrival 296 frames after the
  request, at the same spawn point. Frame numbers shift by one frame between runs (the
  emulator's free-run between state load and the first boundary).
- Not covered: the fan's direct exit (sub 1, needs D_00810758[0] == 0xFF), a fan hit
  (the beat avoids the fast arm on purpose), and anything in AREA01 after the arrival idle.
- The PCSX2 movie playback time is host time; the game sees one frame.
