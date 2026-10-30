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

The AREA02 points start beyond what has been proven to match the original
row by row, so expect rough edges there.

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
