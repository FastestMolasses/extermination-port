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

## Status

- macOS: window + Metal clear/present + input loop — **working**.
- Windows / Linux: backends skeletoned to the same interface — **TODO**.
- Renderer: clear-and-present only; the PS2 GS/VU1 draw pipeline is added
  incrementally, driven by the decomp repo's renderer reverse-engineering.

See `CLAUDE.md` for the full charter and the non-negotiable rules.
