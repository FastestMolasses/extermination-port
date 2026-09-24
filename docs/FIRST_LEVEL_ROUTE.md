# AREA11 first-level route: original ground truth

Lane "route-captures", 2026-09-23. Original executable SHA-256
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

This document records the original game's first level as a player reaches it,
from first control (save state 04) to Roger. Every beat was played in the
original game (hidden PCSX2, exact one-frame steps) by
`../Extermination/tools/route_capture.py` (beats 00–10 by earlier revisions
of it; see section 7), with pad input only: no teleports
and no memory writes. Each beat has a per-frame trace and a snapshot that
later sessions can resume from. Nothing here is port behaviour.
It is the reference the port must match.

## 1. Tool and outputs

`../Extermination/tools/route_capture.py` (decomp `.venv` python, repo root):

```sh
.venv/bin/python tools/route_capture.py identify            # table of user slots
.venv/bin/python tools/route_capture.py run --beats all     # capture every beat in order
.venv/bin/python tools/route_capture.py run --beats 07,08   # re-capture some beats
.venv/bin/python tools/route_capture.py events --beats 03   # change log of a trace
```

- **Session.** It drives `tools/pcsx2_session.py`: hidden emulator, one frame
  per step at the main-loop top 0x1AAF28, pad through the DebugServer.
  - It waits until no other PCSX2 is running and retries start-up races.
  - It allows 30 s instead of 5 s for a frame boundary. Some snapshots
    reached their first boundary late after `-statefile`.
  - The game runs freely for a few frames between the state load and the
    first boundary, so `f = 0` is the first sampled frame (slot 04 beats
    start at counter 4085 to 4094).
- **Snapshots.** Every beat's snapshot is reloaded once to check that it
  resumes. If it does not, the beat is captured again with 23 more idle
  frames before the snapshot (`tail_idle_frames` in the trace; beat 06 has
  23).
- **Resuming.** A beat whose source is an earlier beat is resumed from that
  beat's `state.p2s`. The tool copies it to `build/s87/route/_resume/` under
  the serial-prefixed name that `snapshot()` needs, and deletes the copy
  afterwards.
- **Navigation.** It is closed-loop. The stick points at a world target
  relative to the camera forward 0x810600 (stick up = forward; stick right =
  (-fz, fx), measured).
- **Use.** Use is Cross. The status screen closes with Triangle. On the
  BATTERY prompt, LEFT moves the cursor to Yes.

Each beat writes `../Extermination/build/s87/route/<nn_beat>/` (ignored):
`trace.json`, `state.p2s` (resume point), `eeMemory.bin`, `gs.bin`,
`scratchpad.bin`, `original.png` (the snapshot frame) and `snapshot.json`.

`trace.json` holds:
- `inputs`: every pad change, with its frame index `f`;
- `teleports`: every position write, with the prior bytes and the reason;
- `rows`: one row per frame, starting with the start row `f = 0`.

Each row has:
- **Clock:** `counter` (0x70003B64).
- **Player:** `pos` (0x810350), `hip` (+0xB0), `yaw` (+0xC4), `p5` (+5
  state), `m1F0`/`m1F1`, `req1F2`, `b2F3`, `clip` (+0x20C), `clock` (+0x3C),
  `ground` (+0x214).
- **Frame control:** `spad` (0x70003B8C..93; byte 1 is 3B8D, byte 5 is 3B91,
  byte 6 is 3B92).
- **Camera:** `cam_mode` (0x8101E4..E7, where +4 is the cinematic byte),
  `cam_eye`/`cam_tgt` (+0x10/+0x20), `eye`/`tgt`/`fwd` (0x8105D0/E0/0x810600).
- **Requests:** `req` (D_008106B0..B9).
- **Fades and bars:** `fade` (0x28A9A0 block), `screen` (the 0x28A8D0
  letterbox block; see section 5).
- **Message and UI:** `msg` (0x2821B0: mode, active, token), `ui` (0x810130).
- **Progress:** `power` (0x81084C), `story792`, `story790` (0x810790..93),
  `d2` (0x8107D8..0x810817; 0x810813 is at byte 0x3B), `battery_item`
  (0x810C7F), `charge` (0x810CB2), `area`.
- **Owners:** for each named owner node, `h` (+0..+0xF), `pos` (+0xB0),
  `s1F0` (+0x1F0..+0x1FF; the script pointer is at +0x1F8) and `t2DC`.

**Owner caveat:** after an owner frees itself (state 3, then 001AFC10), its
node can be reused by a later spawn. Read an owner's fields only while it is
live: the battery pickup after beat 01, the trigger after beat 07.

## 2. The user's save-state slots

The table was produced by `route_capture.py identify` and the probe scripts
kept in `../Extermination/build/startup-reference/`. None of these slots were
written by this lane.

| Slot | Counter | What it is | Provenance |
|---|---|---|---|
| 01 | 1306 | Title screen (area 0) | user |
| 02 | 2939 | Opening cutscene (3B8D=2, 3B8F=2, 3B90=2, 3B91=1, 3B92=1, +2F3=2) | user |
| 03 | 3963 | First control, movement still locked | user |
| 04 | 4083 | First control, walks (route start) | user |
| 06 | 8268 | Panel completed: power 0x80, charge 8/12, player at (239.7,223.8) | isolated panel fixture (AREA11_PANEL.md) |
| 07 | 8118 | Panel animation: clip 0x15C, charge 8, before power | `panel_probe.py` from 08 (LEFT, Cross) |
| 08 | 8003 | BATTERY confirmation prompt (ui 03 02 / 05 04) | isolated panel fixture |
| 11 | 8317 | Elevator clip 0x47, player (222,230,250) | `elevator_probe.py` from 06: writes arm bit 4 at 0x7AA88B |
| 12 | 8043 | Status root after BATTERY Back | `panel_root_probe.py` from 08 (Circle twice) |
| 13 | 8280 | Elevator refusal message phase | `elevator_refusal_probe.py` from 06: clears 0x81084C bit 0x80, writes arm bit 4 |
| 14 | 8059 | Normal status hub (ITEM Back) | `status_hub_probe.py` from 12 (Circle) |
| 15 | 4029 | Roger encounter, bank 96, 50 frames in | `roger_encounter_probe.py` from 03: teleports to (340,290,190) |

Slots 06, 07, 08 and 11 through 14 **do not come from real play**. In every
one of them:
- battery item 0x1B already has count 1 (0x810C7F);
- the battery pickup owner 0x7A5640 (00219550) is still live in state 1.

In a real pickup, 00219550 frees its owner (route beat 01: the node is freed
and reused). So the battery was injected, not taken. Slots 11 and 13 also
write the elevator owner's arm byte instead of pressing Use. Slot 15
teleports.

The route below replaces all of these with played beats. Keep the old slots
as oracle inputs: tests cite them, and the startup-reference tree is never
deleted.

Earlier capture folders:
- `../Extermination/build/startup-reference/panel/`: confirm.png with its
  EE/GS, the panel animation, completion and interaction traces, and
  `root/back_trace.json`.
- `.../elevator/`: `clip47_*`, `completed_ee.bin`, and `refusal/`
  (`refusal_trace.json`).
- `.../roger-encounter/`: the teleported encounter.
- `.../status-hub/`.
- `../Extermination/build/s87/truck/`: the teleported truck and trigger
  captures used by TRUCK_ORIGINAL.md.

## 3. The route

Frame numbers are trace frames `f`, and every beat starts at f=0. `c` is the
main-loop counter. Each beat ends idle with control back: 3B8D=0 and action
+0x1F0=0.

| # | Beat (folder) | Source | Frames | Counters | Original owners and scripts | Port owner |
|---|---|---|---|---|---|---|
| 00 | `00_panel_no_battery` (side) | slot 04 | 260 | 4094..4354 | panel 00159210 (r18): script 0x246F20, message 0x80000018 | em_panel*, em_message_live, em_area11_interaction_host |
| 01 | `01_battery` | slot 04 | 516 | 4085..4601 | pickup 00219550 (g0.0, item 0x1B): take script 0x266620, request B0=1/B1=0x1B, status ITEM page | em_pickup_original / _owner / _program, em_status_* |
| 02 | `02_elevator_refusal` | 01 | 387 | 4602..4989 | elevator 0x827B10 (r19): refusal script 0x82A990, message 0x8000001A, letterbox | em_elevator*, em_message_live, em_fade |
| 03 | `03_panel_power` | 02 | 685 | 4990..5675 | panel: script 0x2477A0, then 00157F60 posts B0=1/B1=0x82 (BATTERY page); Yes discharges 12 to 8 (half units); script 0x247BE0, pointer first seen at 0x247C20 (clip 0x15C); power bit 0x80 | em_panel_runtime, em_battery_ui, em_status_* |
| 04 | `04_elevator_ride` | 03 | 576 | 5676..6252 | elevator: powered script 0x82A750, clip 0x47, carry 0x828050, down to y 190 | em_elevator_program / _runtime |
| 05 | `05_boxes` | 04 | 676 | 6253..6929 | Use against crate r4 (001551B0) and then r3: ledge climb (state 2, +1F0 8); step onto the 220 ledge | em_player_climb, em_crate_original |
| 06 | `06_hill_slide` | 05 | 205 | 6936..7141 | slope slide 0016C6A0 (state 0x1C, +1F0 0x30) | em_player_slide |
| 07 | `07_truck_preview` | 06 | 557 | 7142..7699 | trigger 0x8251E0 (r17): camera script 0x8292C0, letterbox, D_00810792=1 | em_truck_original (trigger), script host |
| 08 | `08_truck_crossing` | 07 | 239 | 7705..7944 | truck 0x823FF0 (r16): arm, shake, fall, D_00810792=0xFF | em_truck_original |
| 09 | `09_fence_door` (side) | 08 | 532 | 7945..8477 | door 001BC350 (r0): script 0x24DE40/0x24DC00, clip 0x45, room move B7=2/B8=2 to entry 2 | em_door_original / _transit / _program |
| 10 | `10_cage_roof_roger` | 08 | 3568 | 7956..11524 | two climbs on the x≈360 attribute-0x32 column; director 0x8253F0 (r12) beat 0 script 0x8294C0; Roger 0x8237E0 (r8) alternate script 0x828990 | none (ladder); em_director (legacy); em_roger |
| 11 | `11_crevice_prompt` | 10 | 1260 | 11525..12785 | climb onto the tank; pipes; climb onto the pipe end; director beat 1, script 0x829A40 ("I have to jump that crevice.") | em_player_climb; em_director (legacy) |
| 12 | `12_crevice_jump` | 11 | 336 | 12794..13130 | running jump (+1F0 0x0C, state 6) across the crevice onto the 270 north block | none |
| 13 | `13_east_tower` | 12 | 809 | 13131..13940 | high ledge climb onto the east tower top; director beat 2, script 0x829CC0 (line 0x99) | em_player_climb; em_director (legacy) |
| 14 | `14_roger_encounter` | 13 | 1818 | 13941..15759 | running jump west to the west tower top; in mid-air Roger's quad 0x82AB80 starts script 0x8283D0 (bank 96); end 0x8107D8=1 | em_roger*, cinematic playback; no jump module |

The main line is 01 → 02 → 03 → 04 → 05 → 06 → 07 → 08 → 10 → 11 → 12 → 13
→ 14.
Beats 00 and 09 are side beats with their own snapshots. The cage ladder in
beat 10 is reached from the strip north of the truck pit and does not need
the fence door.

## 4. What the original does, beat by beat

### 00 Panel with no battery (side beat)

1. The player walks to the panel's alignment point (239.7, 223.8), facing
   yaw 0, and presses Cross at f72.
2. At f75 the panel owner starts 0x246F20. 3B8D goes 0→2 and the camera
   cinematic byte becomes 1. The letterbox starts fading in (state 3).
3. At f79 message 0x80000018 opens in mode 2. It clears at f229.
   The letterbox reaches hold (state 1) at f139.
4. The script ends at f230. The bars fade out (state 2) and control returns.

### 01 Battery pickup (the pop-up)

1. The player walks toward g0.0 at (211.6, 229.9, 227.2) and is stopped by
   the box at (218.2, 222.4). Cross is pressed at f122.
2. At f125 the use scan sets 3B8D=3 and the owner starts take script
   0x266620. 3B8D=1 follows at f126, with camera byte 2 and no letterbox.
   The player plays clip 66.
3. At f189 the item is added (0x810C7F=1, charge 12) and the script posts
   B0=1/B1=0x1B.
4. Status opens at f192–f194 (ui 03 02 … 03 01). The ITEM page's message
   token 0x1B (mode 4) is active from f220; the page switches to browse
   at f459.
5. The page browses from f459. Triangle at f479 enters the phase-5 exit
   (f482) and fades 02/01.
6. At f486 control returns and the pickup owner reaches state 3. At f487
   its node already holds another runtime actor.

### 02 Elevator terminal without power

1. The player approaches along −x and turns to yaw −1.3037. Cross is
   pressed at f197.
2. At f200 script 0x82A990 starts. 3B8D goes 3→2, with camera byte 1 and
   bars state 3.
3. Message 0x8000001A is active from f206 and clears at f356.
4. The bars hold from f265, 64 frames after they start, at +4 alpha per
   frame.
5. Control returns at f357, with the player placed at (222, 230, 250).

### 03 Panel with the battery

1. Cross at f228 starts script 0x2477A0 at f231. Message 0x80000018 is
   active from f235 and clears at f385.
2. At f386 00157F60 posts B0=1/B1=0x82. Status opens on the BATTERY page
   at f389.
3. At f416 it opens the "consume 2 units … Proceed?" prompt (ui 05 04,
   message mode 4, token 8). The default is **No**.
4. LEFT at f446 and Cross at f458 select Yes. The discharge runs 12→10 at
   f462 and 10→8 at f492.
5. The status exits at f522–f525 (3B8D=3 while exiting).
6. Script 0x247BE0 then plays clip 0x15C (348) from f528.
7. The power bit 0x81084C|=0x80 is set at f654. Control returns at f655.

### 04 Elevator ride

1. Cross at f180 starts 0x82A750 at f183. Clip 0x47 (71) plays from f190.
2. The bars hold from f248.
3. The carry runs until the script ends at f546. The player ends at
   (222, 190, 250).
4. The bars fade out at f546.

### 05 Crates

1. The player walks under r4 and turns to face +z. Cross at f172 starts a
   ledge climb at f175: state 2, +1F0 8, clips 0x70/0x78/0x8C. The climb
   ends on top at y 203.78 at f254.
2. The player walks west on r4 and turns to face −x. Cross at f390 climbs
   onto r3 at y 217.79 (f393–f472).
3. The player steps onto the ledge at (219.6, 219.9, 305.2).

The four crate owners' header bytes and positions do not change during the
beat. PLAYER_CLIMB_SLIDE.md describes the climb routines.

### 06 Short hill

1. From the ledge the stick is held down the slope toward (262, 356).
2. At f72 the slide begins (state 0x1C, +1F0 0x30, clips 0x5E/0x61) at
   (251.1, 207.1, 328.1).
3. The slide action ends at f138 on the low ground at (259.9, 185.0, 354.5).
   The player skids out (clips 0x60/0x65) to (265.7, 185.3, 373.7).

### 07 Truck camera preview

1. The player walks along (280,392), (300,400), (328,412) and enters the
   trigger band at f163.
2. The trigger starts 0x8292C0. 3B8D becomes 2 with camera byte 1, and the
   bars fade in from f164 and hold from f228.
3. The camera cuts at f169 to eye (307.2, 224.8, 430.6), target
   (336.1, 199.6, 403.8), then eases to eye (343, 267.9, 441.8), target
   (355, 228, 420). It holds there, then eases back. The eye and target are in
   the trace.
4. The script places the player at (327.4, 184.8, 396.7).
5. At f527 it ends. D_00810792 becomes 1 and the bars fade out. The trigger
   frees itself at f528.

In total the script runs 364 frames and matches the teleported TRUCK_ORIGINAL
trigger capture.

### 08 Truck crossing

The truck bridges the pit east of the trigger area. It is the only way to the
strip north of the pit.

1. The player walks onto it at f43. The ground becomes 0x7A9FB0 and the
   truck's +0x2EC counter starts at 1 (state-4 arming, TRUCK_ORIGINAL.md).
2. +0x2EC then counts up by 1 per frame through the shake (2..0x2E) and
   reaches 0x2F at f89, when the fall starts (state 4→1); it holds 0x2F
   afterwards.
3. The player walks off the north side onto the ground at f122, at
   (352.2, 185.2, 378.4).
4. The fall ends at f209: state 2, D_00810792=0xFF, truck y 164→120.87.

If the player stays on the truck (explored, not kept), it carries them into
the pit at y ≈145, where there is no exit.

### 09 Fence door (side beat)

1. The player walks to the fence door at (423, 184.8, 290.3). Cross is
   pressed at f306.
2. At f309 the door owner starts 0x24DE40 and then 0x24DC00. 3B8D is 2 with
   camera byte **2**: this door has no letterbox (DOOR_ORIGINAL.md).
3. Clip 0x45 plays from f313.
4. At f407 the script posts B7=2/B8=2 and the fade machine starts (3, then
   2).
5. At f472 the room move commits at hold-black. The player appears at
   entry 2, (424.2, 184.8, 274.5), facing yaw π, and the request block
   clears. The area-title card then shows ("FORT STEWART – REAR ENTRANCE").

### 10 Cage roof: director beat 0 and Roger's conversation

1. The player walks north to ladder A, the cage's south face at z 287.5
   (x 356–363, y 185→225). Cross at f265 grabs it at f268:
   - +1F0 0x15 with state5 0x0B, clip 0xE3;
   - then 0x17 (clips 0xE8/0xEA, +3 y per cycle);
   - then 0x18 (clip 0xF0) at the top.
2. The player walks to ladder B at z 257.5 (y 225→265). Cross at f777,
   grab at f780.
3. On the roof, director r12 (0x8253F0, D_00810813=0) finds
   **260 ≤ Y ≤ 280** with the player's X/Z inside quad 0x82ABE0 (a rectangle,
   x 335–385, z 228–255). The height test is at runtime 0x825500: Y is loaded
   from 0x810354, `c.lt.s` against 260.0 rejects only Y < 260, and `c.le.s`
   against 280.0 keeps Y ≤ 280. At f1089 it starts 0x8294C0 (001BA1A0).
4. The script sets 3B8D 3→2 and 3B91/3B92=1 and starts the bars. At f1093
   D_00810793 becomes 1 (trace). That this is the script's op06 on flag 0x3B
   is inferred from AREA_SCRIPT.md's command table and the script-pointer
   timing, not from a watchpoint: a log-only memcheck for the writer got no
   hits.
5. With D_00810791 ≠ 1, D_00810793 = 1 and D_00810813 = 0, Roger r8 takes its
   alternate branch (full condition in section 5) and runs 0x828990 at
   f1094. That is the conversation: message token 0x7F
   from f1163 until it clears at f3383, player clips 358/357. The director script steps
   0x8294C0..0x829A00. An exploratory screenshot of this scene shows the
   camera looking at Roger on the west tower top.
6. The end:
   - At f3459 Roger's script pointer moves to 0x8289D0. At f3460
     D_00810813 becomes 1.
   - The director completes at f3508. D_00810793 becomes 0xFF (credited to
     the script's op07/5 on flag 0x3B by the same table-plus-timing
     inference as step 4), and the director's completion branch writes
     D_00810813=0x10 and calls 001C4760(1,1) (runtime 0x8255CC..0x8255D4).
   - At f3509 D_00810813 becomes 0x11. This is Roger's ordinary branch at
     runtime 0x823A04: when D_00810813 == 0x10 it writes 0x4036129F
     (about 2.845) to Roger's yaw +0xC4 and stores 0x11.

### 11 Tank, pipes and the crevice prompt

1. The player walks east over the bridge. Cross against the tank side is a
   22.4-unit ledge climb onto the tank top (286), with clip 0x79.
2. From the junction south-west of the tank, the pipe runs south-west, then
   south-east, down onto the 270 plateau (f498 is a short drop onto it).
3. Cross against the east pipe end at z 285 climbs it at f642.
4. On top (y 279.9), director beat 1 finds Y ≥ 275 inside quad 0x82AC20.
   That quad is irregular: its bounding box is x 452–500, z 278–292, and
   the vertices are in section 5. At f705 it starts 0x829A40.
5. The bars run and the camera pans to (465, 325, 254). Line 0x97, "I have to
   jump that crevice.", is active from f873 and clears at f1044.
6. The script ends at f1200 and D_00810813 becomes 0x20.

### 12 Crevice jump

1. The player runs north from (477, 262). Cross is set at the plateau's
   edge, at z ≤ 249.5 (f227).
2. +1F0 0x0C (state 6, clips 0x69/0x6B) starts at f230. The jump carries
   about 59 units, from (484, 247) to the north block at
   (476.4, 269.8, 188.1). The landing is at f277 (+1F0 0x0F, state 8,
   clip 0x6E).

### 13 East tower top: director beat 2

1. On the north block the player walks to (444.2, 179.8), at the north end of
   the east tower's east face, and turns to face −x.
2. Cross at f435 starts a high ledge climb at f438 (clips 0x70/0x79/0x8C).
   The same climb from (444.2, 190.8), further south along the face, does
   nothing.
3. The player tops out at (437.6, 289.75, 179.8). Director beat 2 finds
   Y ≥ 285 inside quad 0x82AC60 and starts 0x829CC0 at f530. It runs 3B8D
   3→2, 3B91/3B92=1 and the bars.
4. Line 0x99 (message mode 2) is active from f536 and clears at f747.
5. The script ends at f749 and D_00810813 becomes 0xFF.

### 14 Tower jump and Roger's encounter

1. The player walks to (436, 190) on the east tower top and runs west.
2. Cross is set at x ≤ 411.5 (f238). The running jump (+1F0 0x0C, state 6)
   starts at f241.
3. Roger r8's ordinary branch (D_00810791 ≠ 1, and D_00810793 ≠ 1 or
   D_00810813 ≠ 0; section 5) tests the player position 0x810350 against its
   quad 0x82AB80 (a rectangle, x 330–358, z 160–204) with 001B1EA0 in mode 0
   (a0 = 0, 4 vertices; the X/Z point-in-polygon test, FINDINGS.md), at
   runtime 0x823A64. There is no height test, and it fired here with the
   player in the air. In mid-air, at about
   (356.8, 293.8, 184.4), the player crosses into it, and Roger starts
   0x8283D0 at f283.
4. The player lands on the west tower top at (349.7, 289.75, 183.8) at f288,
   and 3B8D goes 3→2.
5. The fade machine fades out (f289–f352) and back in (f355–f370). Under the
   black the bars start at f353 and are at hold by f354.
6. At f358 +2F3 becomes 2 and camera byte 3: bank 96 cinematic playback.
   Message mode 2 is active from f357 and clears at f1738.
7. The script continues through 0x828650..0x8287D0 with a second fade at
   f1740.
8. At f1758:
   - control returns, +2F3=0;
   - 0x8107D8=1 (Roger story progress);
   - the player ends at (338, 289.75, 192), yaw −2.531.

From script start to release the encounter takes 1475 frames.

## 5. Machines seen along the route

- **Letterbox block 0x28A8D0.** Byte 0 is the state: 3 fade-in, 1 hold,
  2 fade-out, 0 idle. The halfword at +4 is the alpha. In beats 00, 02,
  03, 04, 07, 10 and 11 the alpha rises by 4 per frame and holds after 64
  frames. In 14 the bars reach hold one frame after they start, under the
  black fade. The bars come with the cinematic byte 0x8101E4=1. The door (09)
  and the pickup (01) use camera byte 2 and get no bars.
- **Fade machine 0x28A9A0.** It runs to black for the room move (09) and for
  the Roger encounter (14). A short 02/01 fade happens at every status exit
  (01, 03).
- **Message machine 0x2821B0.**
  - Mode 2 carries the examine and terminal lines 0x80000018 and 0x8000001A,
    Roger's 0x7F, and the director's 0x97 and 0x99.
  - Mode 4 carries the status page messages: item 0x1B, prompt 8 and
    discharge 0x1B.
- **Director 0x8253F0** (splat `func_overlay_AREA11_008253B0`; in this
  overlay the splat labels are 0x40 below the runtime addresses that `jal`
  encodes). It switches on its owner state +4:
  - state 0: 001BA1C0(owner, flag 0x3B). If the flag is already set the
    state becomes 3; otherwise +0 = 1 and the state becomes 1;
  - states 2 and 3: 001AFC10 frees the owner;
  - state 1: it dispatches on D_00810813. Each beat body has two sub-states
    in +5: sub-state 0 runs the height and quad tests and starts the script
    with 001BA1A0, then sets +5 = 1; sub-state 1 waits for 001BA1F0, then
    runs the completion and sets +5 = 0.
    - 0/1 → beat 0 (runtime 0x825500): **260 ≤ Y ≤ 280** (`c.lt.s` 260.0
      rejects Y < 260, `c.le.s` 280.0 keeps Y ≤ 280) and quad 0x82ABE0;
      script 0x8294C0; completion writes 0x10 and calls 001C4760(1,1).
    - 0x10/0x11 → beat 1 (runtime 0x825600): Y ≥ 275 (275.0, `c.lt.s`
      rejects Y < 275) and quad 0x82AC20; script 0x829A40; completion
      writes 0x20.
    - 0x20 → beat 2 (runtime 0x8256D0): Y ≥ 285 (285.0, same form) and quad
      0x82AC60 (x 410–439, z 175–203, the east tower top); script 0x829CC0;
      completion writes 0xFF.
  All quad tests are 001B1EA0 mode 0 over 4 vertices at the player position
  0x810350, which tests X/Z only. Quads 0x82ABE0, 0x82AC60 and Roger's
  0x82AB80 are axis-aligned rectangles, so their x/z ranges are exact.
  **Quad 0x82AC20 is not a rectangle.** Its (x, z) vertices, in order, are
  (452, 282), (500, 292), (500, 280), (462, 278). The "x 452–500, z 278–292"
  in beat 11 is only its bounding box. All three beats are on the route
  (10, 11, 13). This replaces the legacy `em_director` kCineBeats
  (FIRST_LEVEL_AUDIT H10).
- **Roger r8 controller** (0x8237E0; the story-branch function is at
  runtime 0x823910, splat `func_overlay_AREA11_008238D0`, with the same 0x40
  label offset):
  - D_00810791 == 1: it clears Roger's +1 byte and returns (neither branch runs).
  - Otherwise, if D_00810793 == 1 and D_00810813 == 0: the alternate branch.
    Sub-state +5 = 0 starts 0x828990; +5 = 1 waits for 001BA1F0.
  - Otherwise, the ordinary branch. If D_00810813 == 0x10 it writes
    0x4036129F to +0xC4 and stores D_00810813 = 0x11. Then, in sub-state
    +5 = 0, the quad 0x82AB80 test (001B1EA0 mode 0) starts 0x8283D0 and
    calls 001FABB0; +5 = 1 waits for the script and then handles
    0x8107D8.

## 6. Limits and open items

- **Level layout.** Everything after the truck is reached by ladders,
  climbs and two running jumps: the crevice, then tower to tower. The
  obvious-looking alternatives are blocked in the original:
  - the ground under the towers, blocked at z ≈ 209;
  - the grate walkway into the west tower, blocked at z 209.7;
  - the cage-roof north railing;
  - the tank-top railing;
  - the east tower's east face south of z ≈ 180.
  The fence door (09) and the west-yard ladder to the 250 ledge are side
  paths.
- **Ladders.** PLAYER_CLIMB_SLIDE.md section 7 identifies the AREA11
  ladders: three columns of attribute-0x32 grid nodes, reached through
  0015D4C0 case 0x32 (00177030(p, 4) gate, +5 = 0xB, +1F0 0x15 or 0x16,
  state-0xB callback 00165B60 with clip 0xE3/0xE4, then state 0xC at
  001662D0). The climbs in these captures map onto those columns:
  - column x 359.8–360 (y 185 to 264.9, z 252–292.5): both cage climbs of
    beat 10. The grab at the cage's south face (z 287.5, y 185→225) and the
    grab inside the cage (z 257.5, y 225→265) are two climbs on this one
    column, not two separate ladders;
  - column x 316.9–325.6: the west-yard ladder to the 250 ledge (a side
    path, not captured);
  - column x 467.6–476.7 (y 270 to 355): the tall ladder on the plateau
    (not on the captured route).
  The traces show the sequence +5/+1F0 = 0xB/0x15, then 0xC/0x17 (clips
  0xE8/0xEA, +3 y per cycle), then 0xC/0x18 (clip 0xF0) at the top, which
  agrees with that section. No port module implements the ladder states or
  the running jump, and both are on the main route (10, 12, 14).
- **Older fixtures.** The slot-13 elevator refusal and the slot-11 clip 0x47
  fixtures were made from slot 06 by memory writes. Beats 02 and 04 are their
  played equivalents. Beat 14 is the played equivalent of the teleported
  slot 15: the same script, 0x8283D0. An exploratory run teleported into the
  quad from the beat-12 state (not kept) released the player at the same
  position and yaw as beat 14: (338, 289.75, 192) and −2.53073. The teleported truck captures in `s87/truck/` remain valid owner
  evidence; beats 07 and 08 add the played approach.
- **Pad latency.** Pad latency is 2 frames (PCSX2_TESTING.md). Frame indices
  of presses are the frame the input was set.

## 7. Verification of this lane

- **All beats captured.** The final chain was captured in three runs: 00–05,
  06–10 and 11–14. Each beat resumed from its source's final snapshot, so the
  chain is consistent. `route_capture.py verify --beats all` then reloaded all
  15 snapshots, and every one resumed.
- **Tool revision per trace.** The committed `route_capture.py` was last
  changed at 06:15:50 on the capture day. Beats 00–10 were written before
  that (00–05 at 06:06–06:08 by a revision that did not yet record
  `tail_idle_frames`; 06–10 at 06:11–06:14). Beats 11–14 were written by the
  committed tool. The recorded traces are all genuine pad-only play of the
  original, but not every one is reproduced exactly by the committed tool.
- **Re-runs with the committed tool.** Each beat was re-run from its
  *recorded* source snapshot (without saving a snapshot) and compared row by
  row with the recorded trace:
  - exactly equal in every row: 01 (517/517), 03 (686/686), 04 (577/577)
    and 11 (1261/1261) in this lane's re-run; the reviewer's re-run found
    01 (517/517) and 14 (1819/1819) exactly equal;
  - same frame count and pad inputs, but a different start row: 00, 06, 08,
    12 and 14 (in this lane's re-run). In 00, 06 and 08 every counter is
    offset by a constant 9, 6 and 5 frames: the emulator ran freely for a
    different number of frames between `-statefile` and the first
    main-loop boundary (section 1), and that shifts the frame-phase-dependent
    fields (clock, r9 attachment, and in 06 the player/camera floats);
  - a different closed-loop path from f3 on: 02 (389 against 387 frames),
    05 (679 against 676 here; the reviewer got 664), 07 (557 in one re-run,
    560 in another), 09 (534 against 532), 10 (3570 against 3568, differing
    from f0) and 13 (802 against 809, from f133).
  So frame numbers quoted in section 4 hold for the recorded traces only;
  re-captures shift them by a few frames (13 frames at most seen, in 05).
  Scripted segments kept their lengths: 07 is 364 frames from script
  0x8292C0 starting to D_00810792=1 in every capture and in this lane's
  560-frame re-run (f166..f530); 14 is 1475 frames from the encounter's
  start to release in both earlier captures and in the reviewer's
  row-identical re-run. The beat-10 conversation message end moved by 2 frames
  (f3381 against f3383) between two earlier captures.
- **Source slots unchanged.** `pcsx2_session` hashes the source slot before
  and after every session. The tool never writes into `sstates/` except
  through `snapshot()`, which moves the new slot file out again.
- **No emulator left running.** After every session and at the end of this
  lane, `pgrep -fl PCSX2` finds no emulator process.
