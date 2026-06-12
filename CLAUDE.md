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
- Keyboard map = the user's PCSX2 binding, verbatim (em_input.h is the
  authority): WASD/TFGH = left/right stick, arrows = d-pad, I/J/L/K =
  TRIANGLE/SQUARE/CIRCLE/CROSS, Q/E = L1/R1, 1/3 = L2/R2, 2/4 = L3/R3,
  Backspace/Return = SELECT/START. GAIT HOLD TIERS (2026-06-11; EM_KEY_ALT /
  EM_KEY_CMD — the platform synthesizes their KEY_DOWN/KEY_UP from modifier
  transitions): keyboard default is EM_INPUT_DEFLECT_FULL (1.0 = the RUN
  ring); holding COMMAND caps both sticks' vector magnitude at
  EM_INPUT_DEFLECT_WALK (0.8 = the WALK band, raw ~102 in the 88..122 ring);
  holding OPTION caps at EM_INPUT_DEFLECT_TURN (0.5 = the gait-1 TURN/creep
  band, raw 64 — the engine's slowest movement tier: turn-in-place, zero
  translation; there is NO slower translating band in the quantizer table,
  so this is the documented "slowest" mapping). Option wins over a held Cmd;
  diagonals normalize by 1/sqrt(2) so the quantized magnitude stays in the
  held ring — game code must use these em_input.h constants for gait
  thresholds. The mac content view
  consumes all keyDown/keyUp (no-op overrides) so unhandled game keys never
  reach NSWindow's no-responder NSBeep path.
- Faithful to the original presentation: NO persistent HUD — the status
  display is a TRIANGLE-toggled status screen (key I; opening it PAUSES the
  world simulation, exactly the original's menu pause — gameplay_frame gates
  on em_hud_is_open(); EM_HUD_FORCE stays render-only; the menu's 3D
  scene draws the ROTATING PLAYER MODEL, and 2026-06-11 its lighting is
  fixed: the engine lights the menu actor through the NORMAL room-rig
  path (draw class 0xB -> lighting-override mode 2, which func_001D89D0
  does NOT special-case; office rig ambient (57,57,57)/128 + two
  directional lights; the camera-light flag +0x2 bit 0x20 is never set
  on the static menu actor) — the port shader's fixed stand-in left him
  at the 0.30 ambient floor, black-on-black, so ui_scene_render now
  drives the existing forward spot term as a camera-anchored fill
  (cone opened full, scoped to the player draw only) putting
  camera-facing normals at ~1.0 like the engine's rig) and door use runs the
  full captured transit sequence (walk to staging, door clip, 64-frame
  fade-out, re-place behind the door, fade-in + the ARRIVAL WALK-OUT —
  FINDINGS.md "AREA TRANSITION LIFECYCLE"). TWO decoded LOCKS govern the
  transit (2026-06-11, em_door.h "THE TWO LOCKS"): MOVEMENT locks kickoff ->
  walk-out end (the engine's player state 5/1 — func_00183250's ~111-frame
  uninterruptible walk out through the door); the MENU locks kickoff ->
  fade-in completion only (the engine's open poll func_001AE7E0 gates on the
  fade machine D_0028A9A0 + scripted spad 3B8D, cleared at the re-place), so
  Triangle/Start works again about halfway through the walk-out. The FADE
  itself is decoded SUBTRACTIVE (GS ALPHA_2 0xA1: out = max(0, pixel -
  level) — shadows crush first, "exposure pulled down") and the overlay
  pass carries that exact op: em_gfx_overlay_rect_sub (reverse-subtract,
  ONE/ONE on RGB, dst alpha kept) draws a grey quad at the level — the
  former black-quad alpha approximation and its residual gap are retired
  (em_frame.h).
- PLAYER LOCOMOTION is the engine's (s31 + s38 decodes): the stick magnitude
  runs the real gait quantizer (rings 48/88/122 -> turn-in-place / walk
  6 u/s / run 18 u/s; keyboard full push = RUN like PCSX2, Cmd = the walk
  band, Option = the turn/creep band — see the keyboard-map bullet), the
  wall HITBOX is the engine's 4.5-unit five-direction radial
  probe set (ankle + chest passes, push-back response — em_game.c "PLAYER
  WALL RADIUS"), and standing still runs the decoded IDLE CYCLE: breathing
  idle (anim id 0) with the look-around fidget (id 349 = engine 0x15D)
  every 300 frames (func_00161020).
- CAMERA FIDELITY (2026-06-11, observed + DECODED — em_game.c "CAMERA
  FIDELITY" and the decoded constants blocks): NO free camera control (the
  old d-pad orbit is gone); L1 orients the camera behind the player at the
  engine's 2 deg/frame family rate; the IDLE AUTO-ORIENT is the DECODED
  engine machinery (func_001921D0 idle path + func_00193D90): the camera
  struct's own timer counts idle frames and at 481 (= the 300-frame fidget
  timer + the 180-frame look-around clip — the END of the look-around
  idle) arms a 0.2 deg/frame ORBIT around the saved eye<->target radius
  (3 deg deadband, wall-direction gates, cancels on any action/wall).
  WALL RESPONSE is the decoded s61 solver: constant-height PULL-IN (the
  apparent "rise" is emergent look-down geometry, not a lift). The
  WALK-STATE CAMERA is DECODED (s67, func_00230000/func_0022FCA0):
  while the player moves the camera has NO heading policy — the desired
  eye rides a TOW-ROPE behind the target (drag at fabs(camdist), 20/10-u
  dead band, back-out, a cramped 0.3 deg-per-unit swing away from the
  last wall) and the heading is recomputed FROM the eye each frame;
  heights drop to eye +9 / target +8 while moving (idle 19/17 — the low,
  nearly level walking ride). Camera-relative stick input therefore
  CURVES with the chase (the engine's emergent spiral; EM_MOVE_TEST's
  endpoints encode it — EM_MOVE_EXPECT/EM_MOVE_YAW override per scene).
  Optional scene key `camdist <f>` carries the engine's per-record
  camera distance (default -46.8; the office records are -31.2 — the
  exporter does not emit it yet).
- PROJECTION = THE ENGINE'S, exactly (2026-06-11, closes the old
  TODO(projection)): em_mat4_perspective_gs builds the world P from the
  camera's zoom s (EmCamera.zoom, engine ctx+0x2468, default 480; the
  scope camera's 224/tan and scripted lerps have their field ready) —
  tan(hfov/2) = 320/s, tan(vfov/2) = 224/s (67.38 x 50.03 deg at 480;
  the 10/7 tan ratio IS the original 512x448->4:3 pixel-aspect
  anisotropy, reproduced not corrected), near 0.1 / far 16711680
  decoded bit-exact from the GS Z-row literals (decomp FINDINGS
  "ENGINE PROJECTION EXACTLY DERIVED"). The gfx backend letterboxes
  every frame to the centered 4:3 game frame (PCSX2-style; bars black,
  overlay canvases map inside it), so window shape never distorts the
  image. EM_PROJ_TEST pins the whole chain to the engine's own state01
  K = P*V (see the headless-checks list). The status screen's 3D scene
  now uses the SAME engine P at s = 480 (s66 live read: ctx+0x2468
  stays 480 with the hub open; V = identity-with-y-flip; the s49
  empirical 0.74 pin and the implied "menu s ~= 324" are both dead) —
  the validated screen framing is preserved by a re-derived view-space
  placement (em_game.c UI_SCENE_* doc; the s49 actor-position decode
  cannot reproduce the framing under s = 480, so one link of that
  placement chain is misread — flagged open).
- AIM CAMERA = the engine's MODE 1, DECODED (2026-06-11, func_00197D20 +
  func_00197740/func_00197870 — replaces the old +0x8C target-height
  hack): entry frames the player from the current camera heading
  (target/eye = player + rotY(cam yaw)*(0,19,6)/(0,19,-30)); steady looks
  from 30 u behind the player FACING (it tracks the turn-in-place) toward
  player + aim_dir*16 + 19 up — the view down the barrel toward the laser
  dot — with eye.y = player.y + 19 - 30*dir.y (clamps [+2,+30], anti-close
  raise <7 u, min horizontal distance 8 when high); target chases 0.4/0.6
  u/frame, eye 4.0; release re-seeds the chase yaw from the eye->player
  heading (mode-2 transition stand-in, flagged). It runs inside fixed-
  camera regions too; release there still snaps to the room spec.
- MANUAL AIM STEER, DECODED (2026-06-11, func_0017ABA0 — em_game.c
  "MANUAL AIM STEER", accessors em_game_aim_pitch/_yaw_blend/_dir):
  while aiming the stick (d-pad merged) drives the aim blends +0x278/
  +0x27C — PITCH IS INVERTED Y (stick up = aim DOWN, "W = down"), rates
  by the 49/89/123 deflection bands {0, 0.0025, 0.005, 0.015}/frame,
  pitch clamps [0,1], yaw pans the +-60 deg POSE LADDER first and turns
  the body only past the blend limit. The pose is the bilinear blend of
  the 9-step ladder 0x112..0x11A (pitch up 0x113 +81.3 deg / down 0x114
  -78.7 / yaw 0x115..0x11A — measured from the baked clips), substituted
  by em_game's anim dispatch while em_weapon holds the 0x112 base — the
  fire/laser ray follows the posed hand bone automatically. HELD R2 =
  the engine's SECOND armed stance 0x1E (func_001607D0; code 0x32, R2
  family rates, camera state 0x2A bases the target on the entry-saved
  position); em_game runs its pose + steer + camera, em_weapon's
  dot-only laser for it is pending (noted).
- DOOR-TRANSIT CINEMATIC CAMERA, DECODED + LIVE-VERIFIED (2026-06-11,
  op 0x0D sub 5 = func_001B7B30 + func_0018CBD0, re-derived against two
  PCSX2 transits — the first reading's "+27/+25 along the live camera
  heading" was wrong on both axes): at the door script start the camera
  HARD-CUTS to 20 u behind the STAGING POINT along the THROUGH-DOOR
  axis (the cut Euler = spad 3B50, the kickoff's snapped pose — NEVER
  the live heading) at +19, looking at the staging point +13 (the .s:
  eye 11+f4+f5, target 11+f4; the live engine read eye y 19.0 / target
  y 13.0 exactly), then holds the eye while the target re-blends
  (<= 1.0 u/frame, 120-frame window cam+0xA0) as the player walks
  through. When the player crosses the doorway plane (the engine's room
  move) the chase RE-SEATS behind the through-door pose and the normal
  solve owns the camera again — the door wall right behind the eye
  RISES it (engine parked at +29 over the 21-u doorframe, live-read);
  goto doors re-seat at the warp re-place instead (op 0x18 restore).
  The LOCKED-TRY camera (func_001BBBF0: target at the door HANDLE =
  door + 8 u to its left + 10 up; eye 13 u back along the camera yaw at
  door.y + 12) is implemented behind EM_DOORCAM_LOCKED=1 as a flagged
  preview — em_door has no locked sequence yet. Per-room FIXED camera
  angles are DECODED AND PORTED via TWO mechanisms: (1) the mode-0
  DIRECTOR (decomp FINDINGS "MODE-0 CAMERA DIRECTOR DECODED"): per-area
  cases + the D_0024A5F0 trigger-volume table (scene_snow carries the
  one real AREA06 region; the drawbridge has none); (2) SPAWN-RECORD
  cameras (decoded + live-verified 2026-06-11, FINDINGS "SPAWN-RECORD
  FIXED CAMERAS" — the user-observed SUPPLY-ROOM corner camera): room-
  ENTRY spawn records (D_0024D650[area][room] +0x10 word) arm a fixed
  camera through func_001B0460 — bit 7 = fixed flag (cam+0x05), low 7
  bits = camera mode (cam+0x06), word>>8 indexes the eye table
  D_0024A8D0; the eye HARD-places at room entry and stays pinned
  (target = player + 15). AREA02 room 1 entry 3 = the supply room
  behind the office double doors -> eye (116, 33, -300), exported as
  the office scene's camregion line (rect = the room behind the
  doorway plane — the port's region stand-in for the entry-keyed pin).
  Both export as scene.txt `camregion x0 z0 x1 z1 ygate ex ey ez`
  lines (export_level.py --camregions, now sub-state aware). Inside a
  region the eye is PINNED to the room spec (chase + wall solve off;
  target still tracks the player), L1 and the idle auto-orient are
  NO-OPS, and the R1 aim camera still runs — release snaps back
  INSTANTLY (the observed behavior).
- The weapon states play the real player clips (FINDINGS "ANIM ID MAPPING";
  needs a player.emdl exported with `--attach --no-glow --clips
  349,2,3,69,67,75,272,273,283,51,274,275,276,277,278,279,280,281,282,1,
  267,268,269,270,271,0,450,10` — DIRECTORY ids after the 2026-06-11
  enumeration fix (the engine resolver's leading offset table; the old
  scan ids >= 54 were shifted), with 0 = the breathing idle, 349 = the
  look-around fidget for the idle cycle, 275..282 = the AIM POSE
  LADDER steps 0x113..0x11A the manual aim steer blends, and 283 =
  0x11B = THE TRUE RELOAD):
  draw 0x110 @1.4, HELD aim pose 0x112 (em_game_anim_hold), reload
  0x11B, holster 0x111 — each state window gates on the honest clip
  length, and the draw/reload clips are HOLD-type requests so their
  last frame clamps until the aim hold replaces it (no idle-pose pop
  at the window end). RELOAD CLIP DECODED (2026-06-11, the
  user-reported "stagger" run to ground): the engine's reload entry
  (func_0016F600, major 3) requests D_00248B98[sub-weapon] — sub 0 =
  283 (0x11B), a 60-frame shouldered reload (gun stays up, the
  support hand drops to the mag pouch and returns; root planted).
  The old 0x33 was the +0x1F0 ACTION CODE, and library clip 51 is a
  KNOCKDOWN (body folds to the ground) — the literal stagger the
  port used to play; the code/clip coincidence is the same trap s25
  sprang for the fire codes. Library clip 51 stays in the asset
  (it IS a real knockdown clip, future damage-reaction user). FIRE
  RECOIL (s25, FINDINGS "FIRE ANIM MECHANISM"): the engine has NO separate
  fire clip — each shot rewinds the held aim clip to frame 0 at 2 frames/tick
  (em_game_anim_hold_restart, the fire-counter re-seed of
  bone_matrix_publish); the snap is baked into the clip's front frames and
  settles back into the clamped hold (12.5 ticks). FIRE CADENCE
  (2026-06-11, func_0017A8B0 decoded — retires the flat 12-frame
  interval): every trigger press stores +0x2F4 = the aim-ladder clip's
  frame count (25 for the SPR4), so SEMI spaces shots 13 ticks apart
  (~4.6 rds/s — the cadence IS the recoil replay) and the +0x2A press
  queue only samples from counter >= interval-8 (early mash presses
  are DROPPED); the burst/auto fire states overwrite +0x2F4 = 12.0
  per round (6-frame in-burst cadence). LASER HIDE WINDOW
  (2026-06-11, player +0x2F2 decoded): every shot state clears +0x2F2
  and only the cadence expiry / WAIT ticks re-set it — the laser
  VANISHES from each shot tick to its cadence expiry (blinking one
  tick between chained rounds), exactly the original's laser dropping
  out while firing; the laser DOT billboard also offsets half its
  size off the wall along the hit normal (no more half-clipping).
  Aiming is PLANTED (movement locks to the manual aim steer; engine
  evidence in em_game.c player_move) and runs the DECODED mode-1 aim
  camera (the CAMERA FIDELITY bullets above — the +0x8C stand-in is
  retired).
- PICKUPS & INVENTORY (2026-06-11 s63 decode — em_pickup.h carries the
  full ledger; FINDINGS "ITEM PICKUP SYSTEM FULLY DECODED"): scene.txt
  `pickup` lines (export_level.py --pickups) place the engine's REAL
  collectible items from the decoded deferred-spawn registry
  D_0024D820 plus the placement tables' kind-0xB box stacks as
  render-only `prop` lines (the s11/s15/s17 "kind-0xB = item pickups"
  framing was OVERTURNED — those are scenery; the office bake no
  longer embeds them). Collection is the decoded contract: CROSS press
  edge -> the archetype-3 use scan (10-u ring, dy window [-20.5,+3.5],
  pi/4 facing with the 7-u auto pass, nearest wins) -> 2 scripted
  frames -> inventory count[type]++ (the D_00810C64 mirror; type 0x10
  SPR4 MAGAZINE additionally +1 pack and +30 reserve rounds, handed to
  em_weapon while holstered) -> despawn + the per-uid TAKEN BIT
  (D_00810860 mirror, uid = (area<<8)|puid) which suppresses respawn
  across scene reloads — the engine's own persistence. The "Found:"
  presentation is an em_hud line (real tall font + the message bank's
  group-4 name) — a FLAGGED stand-in for the engine's status-screen
  auto-open at the item's record; the ITEM page's magazine row reads
  the real count. Flagged gaps: grab-anim take variant (player clips
  0x40..0x42 unexported), pickup auras, map/key-item array split,
  story-gated spawn conds 3..6 (commented manifest lines).
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
  frame 58 so the default capture frame samples the mid-recoil pose;
  `EM_CAPTURE_AIM=3/4` instead holds stick down/up — the INVERTED-Y steer
  pitches the aim UP/DOWN: the ladder pose, the pitched laser and the
  counter-moved mode-1 eye (use EM_CAPTURE_FRAME=120);
  `EM_CAPTURE_DOOR=1` runs the door-test approach + CROSS with no asserts
  so the capture (default frame 110) samples the door-transit CINEMATIC
  camera (`EM_DOORCAM_LOCKED=1` previews the locked-look placement);
  `EM_CAPTURE_SUPPLY=1` walks the office double doors so the capture
  (default frame 360) samples the SUPPLY ROOM's spawn-record fixed
  corner camera (eye (116, 33, -300));
  `EM_CAPTURE_RISE=1` walks the player at the camera so a late capture
  frame shows the wall-RISE camera; `EM_CAPTURE_ORIENT=1` turn-in-place +
  idle for the slow auto-orient; `EM_CAMERA_TRACE=1` prints the camera
  wall-solve and auto-orient), `EM_PAUSE_TEST=1` (status-screen pause gate:
  open -> held stick dead -> close -> movement resumes; PLUS the door leg —
  menu press DROPPED mid-fade, menu OPENS mid-walk-out while movement stays
  locked, the open menu freezes the walk-out, completion after resume),
  `EM_PICKUP_TEST=1` (pickup self-test on two script-injected items:
  CROSS collect -> +count/+packs/+30 reserve/taken bit, despawn + the
  pi/4 facing gate, scene reload -> taken-uid respawn suppressed +
  inventory persisted; combine with EM_CAPTURE_FRAME=30 to capture the
  "Found:" line), `EM_EXAMINE_TEST=1` (examine self-test on three
  script-injected objects: CROSS arm nearest -> input pause + the mode-2
  radio line + the op00 camera-cue pin, timed dismiss + chase restore +
  the AREA11 300-frame cooldown, AREA-bank chain through em_examine's
  own presenter, pi/4 facing gate, re-arm; the REAL examine records ship
  in the scene manifests — export_level.py --examine, em_examine.h),
  `EM_CAPTURE_EXAMINE=N` (place the player at the scene's examine object
  N-1 + CROSS; scene_snow: 1 = the AREA06 switch message + camera cue,
  2 = the AREA11 "Switch / No power..." refusal; default capture frame
  60 samples the line presenting), `EM_AUDIO_TEST=1`
  (sine smoke test), `EM_INPUT_TEST=1` (pad-change prints), `EM_DOOR_TEST=1`
  (full door-transit sequence self-test incl. the arrival walk-out and the
  frame-290 two-lock split witness), `EM_SFX_TEST=1` (3 overlapping
  one-shots through the shared BGM mixer — needs `assets/sfx/sfx.txt`; see
  `src/game/em_sfx.h`), `EM_MELEE_TEST=1` (knife run: crate light kill,
  heavy stab WHIFFING through a worm — worms are not melee victims
  (s66) — whiff-combo chain; see melee_test_script),
  `EM_CAMREGION_TEST=1` (fixed-camera-region run on a FLAGGED synthetic
  office region — the office has no real ones, see the camera bullet:
  enter pins the eye at the room spec, L1 is a no-op, R1 aim moves the
  eye, release snaps back instantly — see camregion_test_script),
  `EM_TRANSIT_TEST=1` (goto-door SCENE-SWITCH run: west-door transit ->
  runtime reload of scene_drawbridge at full black, player at the decoded
  arrival spawn — see transit_test_script / em_game_scene_switch),
  `EM_AIM_TEST=1` (manual aim steer + mode-1 aim camera: inverted-Y
  pitch, clamps, full-down camera geometry, pose-pan-then-body-turn —
  see aim_test_script),
  `EM_PROJ_TEST=1` (engine-projection truth check: the port camera
  chain — lookat_gs + em_mat4_perspective_gs + the 4:3 frame mapping —
  must reproduce the engine's own K = P*V screen pixels recorded from
  the state01 savestate to 0.05 px on 5 world points; see the
  "Engine projection" block in em_game.c),
  `make test-input` (OS-free pad-model unit test).
- CRATE ASSET (2026-06-11, user-confirmed fidelity): the GLOBAL default
  `assets/enemy_crate.emdl` is the WOODEN shipping crate (the n0
  leaf-table entry 0x0D carve — decomp `export_props.py --crate
  --crate-dir extract/chunk06.n0`). Table nuance, recorded honestly: the
  old cardboard carve came from the n1 (office sub-1) table, but NO
  sub-1 placement spawns a crate (the captured office places zero), so
  no shipped scene genuinely binds cardboard; it stays local as
  `assets/enemy_crate_cardboard_n1.emdl` and the scene-local
  `<scene>/props/enemy_crate.emdl` probe can re-bind it per scene if a
  binding is ever proven.

## Build

- macOS / Linux: `make` then `make run`. No external tools beyond the
  platform compiler + system frameworks.
- Windows: MSVC/clang-cl project (added when the D3D12 backend lands).

## Test harness note (recurring agent confusion)
EM_SLIDER_TEST and EM_LOCKED_TEST REQUIRE `EM_SCENE=assets/scene_drawbridge`
(their door fixtures live there). Run bare they exit with no verdict — that
is NOT a failure. Verified passing at merged HEAD 2026-06-11.
