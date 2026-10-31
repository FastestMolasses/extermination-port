# Extermination — native port

A clean-room, native source port of the PlayStation 2 game **Extermination**
(Sony, 2001) to **macOS, Windows, and Linux**. Companion to the matching
decompilation (sibling `Extermination/` repo): that project recovers the game's
logic as portable C; this one builds it into a real native executable and
**reimplements the PS2 GPU path** (VU1 microcode + GS rasterizer) on modern
graphics APIs — no emulation, no ISO.

## Principles

- **Zero third-party dependencies.** Windowing and rendering are written
  directly on each OS's own APIs:
  | Platform | Windowing | Renderer |
  |----------|-----------|----------|
  | macOS    | Cocoa     | Metal    |
  | Windows  | Win32     | Direct3D 12 |
  | Linux    | X11 / Wayland | Vulkan |
  No SDL/GLFW/engine libraries, and no code from emulators. Every line is
  original.
- **No disc-derived material in the repo.** The build consumes your own
  decompiled C and your own legally-dumped disc assets locally; nothing
  copyrighted is redistributed here.

## Layout

```
src/
  em_platform.h   windowing + input contract (cross-platform)
  em_gfx.h        graphics contract (clear/present now; PS2 draw pipeline later)
  main.c          platform-agnostic bootstrap loop
  platform/mac    Cocoa window           (working)
  platform/win    Win32 window           (skeleton)
  platform/linux  X11 window             (skeleton)
  gfx/metal       Metal renderer         (working: clear + present)
  gfx/d3d12       D3D12 renderer         (skeleton)
  gfx/vulkan      Vulkan renderer        (skeleton)
```

## Build & run

macOS (and Linux once implemented):

```sh
make        # builds build/extermination
make run    # builds and launches
```

Press **Esc** or close the window to quit. The current shell opens a native
window and clears it to an animated colour each frame — the bootstrap that
proves the platform + graphics layers work end to end. Game logic and the
translated renderer plug in on top of this.

### Starting at a later point (`EM_START_AT`)

`EM_START_AT=<value> make run` boots to the title screen and loads a
start-point save through the game's own LOAD GAME path, so you arrive just as
if you had loaded a memory-card save there. The saves are built from your own
disc's recordings: run `make start-saves` once first (they land in
`build/start_saves/`). `EM_START_AT` cannot be combined with `EM_NEW_GAME=1`.

| `EM_START_AT` | Area | Where you start | What comes next |
|---|---|---|---|
| `area01` | AREA01 (second level) | The level's arrival, in the train room with the derailed train | The tunnel and the shaft landing |
| `a01_03` | AREA01 | The shaft landing, in front of the shaft door | The door is still locked; trying it starts a scripted conversation |
| `a01_05` | AREA01 | Next to the NPC | The NPC's second conversation |
| `a01_s7` | AREA01 | Next to the NPC, after the second conversation | The NPC's third (optional) conversation |
| `a01_07` | AREA01 | The shaft landing, with the shaft door now open | Through the shaft door into AREA00 |
| `area00` | AREA00 (third level) | The level's arrival, through the shaft door | Down the stairs to the main floor |
| `a00_01` | AREA00 | At the foot of the stairs | East past the fires to door [51], which is locked |
| `a00_04` | AREA00 | In front of door [55], the way into the cage room | The cage room's terminal, which starts the ferry; the nest creatures are nearby |
| `area01-revisit` | AREA01 (return visit) | Back in AREA01 on the shaft landing, arriving from AREA00 | The tunnel back to the train room |
| `a01r_01` | AREA01 (return visit) | The train room, just before the event | The event cutscene and the infection cure item |
| `area02` | AREA02 | The arrival through door [16]: the room with the bed | The bed (cures the infection) and the duct |
| `a02_01` | AREA02 | The duct's exit | The south tunnel, the turntable and switch [31] |
| `a02_02` | AREA02 | By switch [31], just after pulling it | The car runs east and smashes gate [38]; the ladder |
| `a02_03` | AREA02 | At the foot of the ladder, after the car has stopped | Climbing over the wreck to the platform |
| `a02_04` | AREA02 | On the raised platform past the wreck | Battery panel [24] |
| `a02_05` | AREA02 | At battery panel [24], after charging it | Door [25], the exit into AREA04 |
| `area22` | AREA22 | The arrival from AREA04, at door [6] | North to door [7], a room move |
| `a22_01` | AREA22 | Past door [7], in the north arm | The corridor and its bugs, to the south arm |
| `a22_02` | AREA22 | The south arm, at door [8] | Door [8], up to AREA01's upper floor |
| `a01u` | AREA01 (upper floor) | The arrival from AREA22 (entry 6) | South across the gap (a running jump) |
| `a01u_s0` | AREA01 (upper floor) | Past the gap | An optional pickup on the platform |
| `a01u_01` | AREA01 (upper floor) | Past the gap, on the platform | The catwalk and its cutscene with the creature |
| `a01u_02` | AREA01 (upper floor) | On the catwalk after the cutscene | West to door [19], into AREA06 |
| `area06` | AREA06 | The arrival from AREA01 (entry 0) | The beam room |
| `a06_01` .. `a06_05` | AREA06 | Where that route beat starts: the crate and door [2], the keypad, the way out of the room, the beam's collapse, locked door [3] | The next beat |
| `a06b` | AREA06 (the way back) | Outside the keypad room | The jump over the beam, back to door [1] |
| `a06b_01` | AREA06 (the way back) | Past the beam | Door [1], back into AREA01 |
| `a01v` | AREA01 (upper floor, the way back) | The arrival from AREA06 (entry 7) | East along the catwalk |
| `a01v_01` | AREA01 (upper floor, the way back) | At the gap | The gap jump north |
| `a01v_02` | AREA01 (upper floor, the way back) | North of the gap | Door [18], into AREA22 |
| `a22b` | AREA22 (the way back) | The arrival from AREA01 (entry 5) | Back to door [7] |
| `a22b_01` | AREA22 (the way back) | At door [7] | Door [6], back into AREA04 |
| `a04_00` | AREA04 (fifth level) | The arrival from AREA02, in the hall | The event at door [45], which unlocks door [40] |
| `a04_01` | AREA04 | At door [45], after the event | Door [40] into the store room |
| `a04_02` | AREA04 | In the store room, inside door [40] | Console [69], which sends the cable reel down the conveyor |
| `a04_03` | AREA04 | At console [69], after using it | Door [40] back to the hall |
| `a04_04` | AREA04 | In the hall, outside door [40] | Up the conveyor to the east balcony |
| `a04_05` | AREA04 | On the east balcony, at door [37] | Door [37] into AREA22 |
| `a04b_00` | AREA04 (the way back) | The arrival from AREA22 (entry 1), on the east balcony | Down the conveyor to door [45], now unlocked |
| `a04b_01` | AREA04 (the way back) | Inside door [45] | The NPC's long conversation and the access key |
| `a04b_02` | AREA04 (the way back) | Inside door [45], with the access key | Door [45] back to the hall |
| `a04b_03` | AREA04 (the way back) | In the hall, outside door [45] | Up the conveyor to the card reader at the lift; the first boss arrives |
| `a04b_04` | AREA04 (the way back) | At the card reader, the creature and bugs at the lift | Past the creature into the lift, down to AREA13 |
| `boss` | AREA04 (the way back) | In the hall, outside door [45], with the SHOTGUN UNIT fitted and six shells (the room's pickup, taken in the fork recording) | Up the conveyor to the card reader, which wakes the first boss at the lift |
| `area13` | AREA13 | The lift's arrival (entry 0), in the car | Out of the car, the lobby, door [8] and the scene behind it |
| `a13_01` | AREA13 | Behind door [8] (entry 2), after the scene | The button [16] of door [14], the way outside |
| `a13_02` | AREA13 | Outside (entry 8), in the cold | Round the building to door [17], the scene and the coat |
| `a13_03` | AREA13 | The east room (entry 4), after the scene | The pickup g[5] |
| `a13_04` | AREA13 | The east room, with the pickup's item | Over the grating step to hatch [62] |
| `a13_05` | AREA13 | At the open hatch [62] | The ladder down, which falls into AREA19 |
| `a13b_01` | AREA13 (the second load) | Out of hatch [62] (entry 6), after AREA19's ladder | The ledge climb and door [17] outside |
| `a13b_02` | AREA13 | Outside door [17] (entry 9) | Round to door [14] and its button [15] |
| `a13b_03` | AREA13 | Inside door [14] (entry 3) | Door [8] into the lobby |
| `a13b_04` | AREA13 | The lobby (entry 1) | The lift's outer button [12] |
| `a13b_05` | AREA13 | At the outer button, the lift called | Into the car, the inner button, down to AREA04 entry 7 |
| `a13c_00` | AREA13 | Outside door [17] | Door [17], the recharger [60] |
| `a13c_01` | AREA13 | At the recharger, the battery full | Door [17], the roof ladder, the walkway to the battery machine [44] |
| `a13c_02` | AREA13 | At [44]'s use point on the walkway | [44] with the battery: its page and script |
| `a13c_03` | AREA13 | At [44], flag 0x1C set | Back along the walkway: the blast, [45]'s sequence, the ground |
| `a13c_04` | AREA13 | On the ground after the blast | The roof, its north part, the stair, the pickup g[3] and the cure |
| `a13c_05` | AREA13 | North of the building, cured | Back up, the drop onto the fallen boom, [7]'s area and its scene |
| `a13c_06` | AREA13 | On the boom after [7]'s scene | Down the boom into the region south of the pipe fence |
| `a13d_00` | AREA13 | South of the pipe fence | East to the switchback stair south of the big building |
| `a13d_01` | AREA13 | At the top of the stair | The ladder to the roof corridor |
| `a13d_02` | AREA13 | On the roof corridor | East to the first block's ladder |
| `a13d_03` | AREA13 | On the first block | The running jump onto the second block |
| `a13d_04` | AREA13 | On the second block | Its east ladder down to the corridor |
| `a13d_05` | AREA13 | On the corridor | The ladder down to door [20]'s side |
| `a13d_06` | AREA13 | At the ladder's foot | Door [20] from the north (entry 5) |
| `a13d_07` | AREA13 | Inside door [20] | The hatch [63], whose ladder falls to AREA19 entry 10 |
| `area15` | AREA15 sub 0 | Entry 0, the arrival through door [50] | The long conversation in the medical room |
| `a15_00` | AREA15 sub 0 | After that conversation | The slider and door [14] to the stair tower (AREA19's load) |
| `a15b_00` | AREA15 sub 1 | Entry 0, through door [51] | Up the stairs to the mezzanine: the event, then the forced return |
| `a15b_01`, `a15b_02` | AREA15 | Inside the event's scenes (their source states) | Discovery points: the scenes restart from the save |
| `a15b_03` | AREA15 sub 0 | After the two conversations | The slider and the bed |
| `a15b_04` | AREA15 sub 0 | At the bed | The slider and door [14] to the stair tower (AREA19's load) |
| `a03_00` | AREA03 sub 0 | Entry 1, through AREA19's door [25] | The lift's outer button, the car, the ride down |
| `a03b_00` | AREA03 sub 0 | At the lift's foot | Past the creatures round the beam, door [13] to sub 1 |
| `a03b_01`, `a03b_02` | AREA03 sub 1 | Entry 1, then room A | The ducts west and east, to [22]'s room |
| `a03b_03` | AREA03 sub 1 | [22]'s room | g[3], [22]'s long script, items 0x2A and 0x12 |
| `a03b_04`, `a03b_05` | AREA03 sub 1 | [22]'s room | Back through the ducts, the curtains, the slider to sub 2 |
| `a03b_06` | AREA03 sub 2 | Entry 0 | The lift's buttons, the ride to AREA08's load |
| `a03c_00` | AREA03 sub 2 | Entry 1, at the lift | The slider to sub 1 |
| `a03c_01` | AREA03 sub 1 | Entry 6 | The curtains, door [26], the duct to the mouth |
| `a03c_02`, `a03c_03` | AREA03 sub 1 | The mouth | The seal [23], door [24] to AREA04 sub 1 (first line) |
| `a03d_00`, `a03d_01` | AREA03 sub 1 | The mouth | The seal, door [24]: the line that goes on to AREA07 |
| `a04c_00` .. `a04c_02` | AREA04 sub 1 | Entry 2, on the balcony | The event, item 0x1E, the north room, the lift (first line) |
| `a04d_00` .. `a04d_02` | AREA04 sub 1 | Entry 2, on the balcony | The same, with item 0x1E kept: the lift to AREA07 |
| `area19`, `a19_01`, `a19_02` | AREA19 sub 0 | The foot of the ladder under AREA13's hatch, then the room behind door [32] | The duct, the pickup, the duct back |
| `a19b_01`, `a19b_02` | AREA19 sub 0 | Entry 10's side | The ledge, the ladder |
| `a19c_00` .. `a19c_07` | AREA19 sub 0, then sub 1 | Where each beat of the way to door [52] starts | The bars, the slide, the walkway, door [27], the ladder, door [52] |
| `a19d_00` .. `a19d_20` | AREA19 sub 1 | Where each beat of the way to door [50] starts | The beasts, the ceiling, the cage, the bars, the seal, door [50] to AREA15 (`a19d_12` .. `a19d_14` start in mid-air and fall) |

The route these points sit on is proven row by row from New Game, through the
first boss fight with the pistol and the lift, through AREA13 to the ladder down
into AREA19 (docs/LEVEL_SMOKE.md). Starting from a point is a different path: a loaded game
puts creatures, timers and the random numbers where a save does, so a run from
one plays the same code but can go differently from that route. `python3
tools/mid_route_beats.py` reports which beats play through from their points
(docs/LEVEL8_PORT.md "Start-card runs"). AREA19 is not ported yet, so the ladder
at `a13_05` stops at its load.

## Status

- macOS: window + Metal clear/present + input loop — **working**.
- Windows / Linux: backends skeletoned to the same interface — **TODO**.
- Renderer: clear-and-present only; the PS2 GS/VU1 draw pipeline is added
  incrementally, driven by the decomp repo's renderer reverse-engineering.

See `CLAUDE.md` for the full charter and the non-negotiable rules.

## Future Enhancements
- Slide down ladder
- No inverted aiming
- Toggle between classic camera control or modern camera
- Move while aiming
- Going through some doors wont trigger a full black screen fade in / fade out
- Macos window update when changing themes (dark/light)
- Subtitle fixes
