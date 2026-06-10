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
  em_gfx.h               cross-platform graphics contract (clear/present +
                         the translated PS2 skinned-draw path)
  em_input.h/.c          OS-free DualShock 2 pad model (keyboard-fed today)
  em_model.h/.c          EMDL asset loading (our own interchange format)
  em_audio.h             pull-model audio output contract
  main.c                 bring-up only: platform/gfx/audio init -> em_frame_run
  game/
    em_task.h/.c         the engine's 3-slot frame-task table (state byte +
                         fn per slot; START/WAKE->RUN promotion; dispatch)
    em_frame.h/.c        the per-frame phase sequence (PS2 main loop steps
                         A..W; the mapping table lives in em_frame.c) + the
                         frame input block (current/pressed/released + analog)
    em_game.h/.c         slot-0 game task chain: boot task -> game task ->
                         in-game frame machine -> gameplay frame (actor
                         update -> render chain build -> camera -> flush)
  platform/{mac,win,linux}/   native windowing per OS
  gfx/{metal,d3d12,vulkan}/   native renderer per API
```

Layering: `main.c` and the game code talk only to the `em_*` contracts. The
platform layer never touches a GPU API; the gfx layer attaches to the window's
native surface handle. `src/game/` holds the faithful structural translation
of the engine's game-loop architecture (decomp repo
`Extermination/docs/FINDINGS.md` "ENGINE FRAME ANATOMY" is the spec); its
files map each native stage to the PS2 function it stands in for.

## State

- macOS target works: Cocoa window + Metal, textured skinned character +
  level scene through the engine-shaped frame loop — build with `make`, run
  with `make run`.
- The game loop now has the engine's structure: em_frame runs the documented
  per-frame phases, em_task dispatches the slot-0 game task, and the gameplay
  frame stages (actor update, render chain build, camera apply, close-out
  flush) host today's rendering. Game logic fills the skeleton arms as the
  decomp recovers it.
- Windows (Win32 + D3D12) and Linux (X11 + Vulkan) backends are skeletoned with
  the same interface; not yet implemented.
- Faithful to the original presentation: NO persistent HUD — the status
  display is a TRIANGLE-toggled status screen (key I; dims the scene, gameplay
  keeps running) and door use runs the full captured transit sequence (input
  lock, walk to staging, door clip, 64-frame fade-out, re-place behind the
  door, fade-in, unlock — FINDINGS.md "AREA TRANSITION LIFECYCLE").
- The weapon states play the real player clips (FINDINGS "ANIM ID MAPPING";
  needs a player.emdl exported with
  `--clips 346,2,3,69,67,75,272,273,51,274,1,267,268,269,270,271` — the s36
  superset adds the knife clips 0x10B..0x10F):
  draw 0x110 @1.4, HELD aim pose 0x112 (em_game_anim_hold), reload 0x33,
  holster 0x111 — each state window gates on the honest clip length. FIRE
  RECOIL (s25, FINDINGS "FIRE ANIM MECHANISM"): the engine has NO separate
  fire clip — each shot rewinds the held aim clip to frame 0 at 2 frames/tick
  (em_game_anim_hold_restart, the fire-counter re-seed of
  bone_matrix_publish); the snap is baked into the clip's front frames and
  settles back into the clamped hold (12.5 ticks; full-auto restarts it every
  6). Aiming is PLANTED (movement locks to turn-in-place; engine evidence in
  em_game.c player_move) and lowers the camera follow target to the aim
  offset (struct +0x8C), the over-shoulder cut's mode-0 stand-in.
- KNIFE / MELEE (s36 decode — em_weapon.h "KNIFE / MELEE", FINDINGS
  "KNIFE/MELEE DECODED"): while HOLSTERED, CIRCLE (L) = the LIGHT 3-hit
  combo (engine mode 0x21: anims 0x10B/0x10C/0x10D, damage 3/3/5, sounds
  0x17D/0x17E/0x17F, FIRE-press chain buffering, hit-confirm recover 0x10F)
  and SQUARE (J) = the HEAVY stab (mode 0x22: anim 0x10E, damage 15).
  The two attacks are on TWO BUTTONS in the engine (not tap-vs-hold).
  Victims take the +0x36 mailbox at the impact tick (reach 12 = the
  engine's documented hands-reach, flagged stand-in). SQUARE while AIMING
  = the attachment-0 sub-weapon toggle (sound 0x179 — the decoded s29
  "unidentified action"). The knife model stays on the hip holster (node
  14) during attacks — no rebind found statically; flagged note.
- Headless checks: `EM_CAPTURE=<path.bmp>` (renders ~1 s, captures gameplay
  frame 60, exits; the default frame is the HUD-free one — `EM_HUD_FORCE=1`
  forces the status screen visible for overlay captures; `EM_CAPTURE_AIM=1`
  holds R1 from frame 0 + a short turn so the capture shows the armed
  stance, laser and aim camera; `EM_CAPTURE_AIM=2` adds one semi shot at
  frame 58 so the default capture frame samples the mid-recoil pose),
  `EM_AUDIO_TEST=1`
  (sine smoke test), `EM_INPUT_TEST=1` (pad-change prints), `EM_DOOR_TEST=1`
  (full door-transit sequence self-test), `EM_SFX_TEST=1` (3 overlapping
  one-shots through the shared BGM mixer — needs `assets/sfx/sfx.txt`; see
  `src/game/em_sfx.h`), `EM_MELEE_TEST=1` (knife-vs-crates run: light kill,
  heavy kill, whiff-combo chain — see melee_test_script),
  `EM_TRANSIT_TEST=1` (goto-door SCENE-SWITCH run: west-door transit ->
  runtime reload of scene_office0 at full black, player at the decoded
  arrival spawn — see transit_test_script / em_game_scene_switch),
  `make test-input` (OS-free pad-model unit test).

## Build

- macOS / Linux: `make` then `make run`. No external tools beyond the
  platform compiler + system frameworks.
- Windows: MSVC/clang-cl project (added when the D3D12 backend lands).
