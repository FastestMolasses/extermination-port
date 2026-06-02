# CLAUDE.md — Extermination native port

Persistent project instructions. Load every session.

## What this is

The **native source-port** companion to the Extermination (PS2, SCUS-97112)
matching decompilation (sibling repo `Extermination/`). The decomp recovers the
game's logic as portable C; this repo builds it into a real native executable
for **macOS, Windows, and Linux** — no PS2 emulation, no ISO. The PS2 GPU path
(VU1 microcode + GS rasterizer) is **reimplemented** here on modern APIs, not
emulated or recompiled.

## Hard rules (non-negotiable)

- **Clean-room, zero third-party dependencies.** Windowing and rendering are
  written by us, directly on each OS's native APIs:
  - macOS: Cocoa (AppKit) + Metal
  - Windows: Win32 + Direct3D 12
  - Linux: X11/Wayland + Vulkan
  These are the platforms' own system frameworks — not third-party libraries.
  **Do NOT add SDL, GLFW, bgfx, sokol, or any external library**, and do not
  copy code from emulators (PCSX2, Play!) — their licenses (GPL) and IP would
  entangle the project. Every line here is our own original code.
- **Never commit, upload, or redistribute disc-derived material** — no ISO,
  boot ELF, extracted assets, or original game code/data. The port consumes the
  user's own decompiled C and their own legally-dumped disc assets at build
  time; nothing disc-derived lives in this repo. `.gitignore` covers
  `assets/`, `data/`, `*.iso`, `*.bin`, `*.dat`.
- **Stay isolated from the user's other code/repos** (esp. the separate
  commercial game). The only sibling this repo relates to is the `Extermination/`
  decomp, whose portable C this build compiles — that link is the project's
  whole point and is the *only* permitted cross-repo relationship.
- The north-star is a scrupulously clean, original codebase strong enough to
  someday pitch Sony a remake. Keep it that way.

## Architecture

```
src/
  em_platform.h          cross-platform windowing + input contract
  em_gfx.h               cross-platform graphics contract (clear/present today;
                         the translated PS2 draw pipeline grows on top)
  main.c                 platform-agnostic bootstrap loop
  platform/{mac,win,linux}/   native windowing per OS
  gfx/{metal,d3d12,vulkan}/   native renderer per API
```

Layering: `main.c` and the future game code talk only to `em_platform.h` /
`em_gfx.h`. The platform layer never touches a GPU API; the gfx layer attaches
to the window's native surface handle. This keeps the game/renderer split clean
and each platform swappable.

## State

- macOS shell (Cocoa window + Metal clear/present + input/quit loop) is the
  first working target — build with `make`, run with `make run`.
- Windows (Win32 + D3D12) and Linux (X11 + Vulkan) backends are skeletoned with
  the same interface; not yet implemented.
- The renderer is clear-and-present only. The PS2 GS/VU1 draw pipeline is
  reimplemented incrementally as the decomp repo reverse-engineers it (the
  bone/anim/skinning/CLUT/GS-packet characterization in `Extermination/docs/`
  is the renderer spec).

## Build

- macOS / Linux: `make` then `make run`. No external tools beyond the
  platform compiler + system frameworks.
- Windows: MSVC/clang-cl project (added when the D3D12 backend lands).
