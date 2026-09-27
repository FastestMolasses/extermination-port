# Fidelity features

This is the public list of what the native port reproduces from the original
PlayStation 2 game, and how each claim was proven. It is written so that it
can be quoted when the port is finished. Every entry must carry its evidence
and an honest status. Nothing is listed on the strength of a label, a
comment or a screenshot judged by eye alone.

## What "faithful" means here

The port runs the original game's own code, translated routine by routine
from the original instructions and checked against them, natively on a
modern computer. It is not an emulator and it is not a remake. The aim is
the original **experience**: the same code behaviour, and the same timing of
everything the player sees and hears, including the timing the game's own
choreography depends on. The aim is not to reproduce the limits of the PS2
hardware; see "What we deliberately do not emulate" below.

**All accuracy claims are relative to PCSX2 recordings.** The project has no
real PS2 to measure. The original game was recorded in PCSX2 (RAM, GS memory,
audio-driver state, per-frame traces), and its original instructions were
executed in the project's own interpreters over those recordings. Where an
entry says "equal to the original", it means equal to the original
instructions on PCSX2-recorded inputs, or equal to a PCSX2 recording. Unless
an entry says otherwise, the scope is the **first level** (New Game, the
AREA11 opening, and the level up to Roger's encounter).

### Status tags

- **VERIFIED**: proven by an original-instruction oracle or a PCSX2 capture
  comparison, and live in the game.
- **PARTIAL**: part of the claim is proven and live; the entry says exactly
  what is and is not covered.
- **PLANNED**: work queued, not done. Do not advertise it as a feature yet.

Numbers are quoted only from the cited evidence. Entries name the port doc
(under `docs/`), the test, and the commit. "Decomp" means the companion
decompilation repository (`../Extermination`).

---

## What we reproduce

### Code and verification

**Built on a byte-identical rebuild of the original game executable**

The port does not guess at the game or re-imagine it. It rests on a
companion decompilation that rebuilds the original PS2 executable
(SCUS-97112, USA 1.00) and all 19 of its level/overlay modules byte for byte
from the project's sources. That rebuild is the reference the port's
translations are read from.

- How: game code is built with Metrowerks CodeWarrior `mwccps2` point
  releases (2.3.3/2.4/3.0, chosen per file). The original names MW MIPS C
  2.3.1.01, so these are the same compiler family, not the exact build. Sony
  SDK code is built with ee-gcc 2.9-991111. A function is rewritten as C until
  objdiff shows identical output. C believed correct but not yet
  byte-identical is kept marked `// NEARMISS`, and the link then uses the
  original code that each user generates locally from their own disc. The
  decomp's `tools/verify_all.py` (six stages) checks the rebuild.
- Evidence: decomp `docs/HANDOFF.md`, Verification toolkit: 2150/2211 units,
  boot ELF byte-identical, 19/19 overlays (commit bdd40fb). Decomp
  `docs/PROGRESS.md`: matched_code 98.60%; about 1,516 boot functions link
  from ordinary compiled C. At decomp HEAD 5f68d84, 731 of the 2,953 boot C
  sources and 3 overlay sources are NEARMISS. Target pinned by ELF SHA-256
  `ee052236...d1e17a` (decomp CLAUDE.md, Target identity).
- Status: **PARTIAL**. The byte-identical executable and overlays are
  verified by the gate. "Rebuilt from C" is only partly true: about 640
  sources are CodeWarrior inline-asm bodies, about 730 are NEARMISS and about
  15 are INCLUDE_ASM. The overlays are byte-identical mostly because their
  original code is reassembled (AREA11, the first level's overlay, has 5 C
  sources for the 34 overlay functions the census tracks). The port is
  checked against the **original instructions**, not against this C.
  Advertise "a byte-identical rebuild of the original executable, with
  readable C for most boot functions", not "fully decompiled to matching C".
  Re-run `tools/decomp/build.py build` and `verify_all.py` before quoting any
  count (the decomp tree has an uncommitted SDK relabel; HANDOFF and PROGRESS
  disagree on AREA01, 33 vs 32 of 41).

**Port code is translated from the original instructions and tested against the original code itself**

Each piece of first-level game logic (movement, camera, collision, scripts,
effects, menus) is a translation of the game's own code, not a remake that
"feels about right". Each translated function is tested by running the
original machine code on the same inputs and requiring the same results.

- How: the reference tests (`tools/test_*_reference.py`) are Python MIPS/VU0
  interpreters. They execute the original code, read at test time from the
  user's own local copy (never committed), over RAM captured from the
  original game in PCSX2, and compare every field written and every worker
  call made with the native C.
- Evidence: port HEAD dee7325 has 179 reference scripts and 236 `make test-*`
  targets; 236/236 pass after the EE-float harmonization (commit 7dea4ce).
  Example sweeps (`EE_FLOAT_MODEL.md` 5b): camera rotation 972 cases
  byte-exact; item trail 22,016 fixed-point triangles; weather 12,000 state
  comparisons; collision faces 4,800 cases.
- Status: **VERIFIED** per function, for functions that have a reference
  test. Coverage of the level is the census entry below. Inputs are PCSX2
  captures, so every equality is relative to PCSX2. Exceptions: some closure
  oracles stand in hooks for SDK VU0 routines, and
  `test_player_heading_reference` compares host `cosf`/`atan2f` within 3e-6
  (`EE_FLOAT_MODEL.md` 5a). VU1 microcode and GS rasterization are
  reimplemented natively and are checked by other means (see Visuals).

**First-level census: 91.8% of the original game-logic instructions on the route run live as verified translations**

Every original function the PS2 game runs on the first level, from New Game
to meeting Roger, was recorded, and the port was checked for each one.
Measured by instructions, 91.8% of that game logic runs in the port as a
verified translation.

- How: the decomp's `tools/route_census.py` set a one-shot breakpoint on
  each of 2,957 boot functions and 34 AREA11 overlay functions and played the
  original route in hidden PCSX2 (4 startup labels plus route beats 00..14):
  1,184 functions executed (111,764 instructions). Each was classified by
  reading its evidence. An instrumented port build recorded live
  caller/callee edges (census 1.22).
- Evidence: `FIRST_LEVEL_CENSUS.md` 1.1, 1.22, 2.1-2.3, recount through 1.31
  (2026-09-27). Of 741 non-boundary functions: live 660 (80,726 of 87,968
  instructions = 91.8%; 89.1% by function count); verified but unbound 77;
  unverified 3 (0015CF90, 001B1190, 001FC280); missing 1 (001CB3C0). 443
  boundary functions (SDK/libc/IOP/driver/GS/VU1, 23,796 instructions) are
  replaced by native platform services and the native renderer.
- Status: **PARTIAL**. First level only, and only the played route to
  Roger's encounter. Not covered: the level exit, unplayed branches
  (damage/death, pause/options/save, weapon and camera inputs, the truck-pit
  fall, the west-yard and plateau ladders) and boot before the title.
  Boundary functions are native replacements, not translations. "Stand-in 0"
  counts census rows only; census 2.3 "What still stands in" lists non-row
  stand-ins still on the route. Instrumented liveness was last measured at
  1.22; later rows were moved per step.

**The first-level route is replayed headless and checked phase by phase against PCSX2 recordings**

An automated run plays the first level from New Game to Roger with no window
open. In each scripted, climbing and cutscene window it checks positions,
script progress, camera, messages and cutscene flags against the PS2 game
as recorded in PCSX2 on the same route.

- How: `EM_STARTUP_TEST=newgame-level` plus `tools/test_level_smoke.py`. The
  runner steers closed-loop toward the capture's targets (open-loop pad
  replay does not reproduce the recordings, decomp `CAPTURES_C7.md`
  "Reproducibility"), then replays the tick log against the route captures.
  `--require-through` fails the run if any phase is not live, driven or not
  reached.
- Evidence: `LEVEL_SMOKE.md` "Route coverage": beats 01..14 live on the main
  line (18 phases), side beats 00 and 09, plus `fence_door_side1` against the
  C7 DOOR1 capture. Commit 4366957: test-level-smoke-full PASS through roger.
- Status: **PARTIAL**. First level only. The walks between scripted windows
  are navigation and are not compared. Pixels and sounds are not compared at
  all. `LEVEL_SMOKE.md` "What the full route does not yet compare" lists the
  relaxed checks (the panel prompt window, 7 ticks in the port against 30 in
  the original; line 0x7F's teardown 2 rows early; Roger's flags before his
  clip init; slide/ladder landings within one row; the opening's rand()
  values after its one missing draw (the husk creature, census L24); the
  opening's 1,302 post-steps reported, not drawn;
  001DDE10's frame-copy sprites). The level exit is not in the smoke.

**PS2 floating-point math reproduced bit for bit, as measured in PCSX2**

The PS2's math units do not round like a PC's. With normal PC floating
point, jumps, slides, camera moves and physics slowly drift from the
original. The port reproduces the PS2's rounding rules as PCSX2 emulates
them, so the same inputs give the same results down to the last bit.

- How: every EE FPU and VU0 instruction form the game uses was measured in
  PCSX2 (16 EE instructions, all 78 VU0 op/dest/bc forms in the ELF, plus 800
  free-running block runs). The rules (chop rounding, round-to-nearest
  division, denormals-as-zero, saturation, the add's operand pre-trim,
  MSUB = ACC - product) became `tools/ee_float_model.py` for the oracles and
  `src/game/em_ee_float.h` for the native code. The native model uses only
  integer arithmetic, so host rounding mode, FTZ/DAZ and FMA cannot change a
  result; its test fails on any rounding host FP instruction in the compiled
  shim.
- Evidence: `EE_FLOAT_MODEL.md` 1-6: all 33,800 recorded results and all 800
  block runs reproduced, 0 mismatches. Since commit 7dea4ce every oracle
  interpreter class runs the model (116/116 agree; 67 disagreed before). The
  old host formulas were wrong on 21-52% of cases (round-to-nearest
  add/sub/mul 22/21/52%, truncated add/sub 27%, truncated div 50%). Measured
  on PCSX2 v2.6.3 with the user's settings (FPU/VU0 chop, FPU divide nearest,
  VU0 DAZ, normal clamping; no GameDB override).
- Status: **PARTIAL**. Relative to PCSX2's float emulation under those
  settings, not a real PS2. Not measured: FCR31/MAC flags, Q timing, VU1
  microcode; unmeasured forms fault. Oracle exceptions as above (5a). Native
  gaps (5c): some VU0 per-lane helpers use a truncated host double (differs
  only at an opposite-sign edge case); duplicate SDK math copies remain; the
  snow wave uses host `sinf` (5,022 of 5,184 exact in the full sweep); the
  battery colour ramp's original is not established; the quaternion blend
  differs only on overflow.

**The main loop runs its steps in the original order, one game tick per 59.94 Hz field**

Each frame the game reads the pad, runs the world, updates audio and draws
in the PS2 game's order, one game tick per NTSC field. Everything that
counts frames (animations, timers, cutscene waits) keeps its original
timing.

- How: the original's per-frame call order was traced in PCSX2 with
  breakpoints on every call site of the main loop and frame machine, in 7
  save-state situations (idle, walking, movement locked, opening cinematic,
  selector 3, Roger, the status hub). `em_frame` implements steps A..W in
  that order; steps V (001D2300) and W (001D2580) run their originals since
  e99d8cb. `tools/compare_frame_order.py` compares the port's trace event by
  event.
- Evidence: `ORIGINAL_FRAME_ORDER.md` "How it was measured" and 1-4.
  `LEVEL_SMOKE.md` "Frame order" and commit e99d8cb: idle04, walk04, st03,
  cut02 and cut15 PASS (2026-09-27). `SCENE_COORDINATOR_DESIGN.md`: st14 PASS.
  Decomp `CAPTURES_C7.md` 1: every main-loop frame in the four stream
  stretches is exactly one field.
- Status: **PARTIAL**. PASS is subject to `tools/frame_order_allow.json`'s 4
  known differences (the footstep effect node in walk04, WP-15; the opening
  script's actors and effect nodes in st03/cut02, WP-10). cut07 passes only
  with a scratch allow file. Only the traced situations are covered (2-3
  frames each, plus the smoke's windows); the movie-gated steps M/N/O never
  ran in any trace. Tests run uncapped (`EM_UNCAPPED=1`), so host pacing is
  not measured by any test.

**The game's own random-number generator, bit-exact**

Random effects such as flickering lights, snow puffs and sprite variations
draw from the game's own random-number routine, started from the same state
as the original.

- How: rand (00122BB8) is byte-matched C in the decomp. `em_random` is its
  translation, with unsigned arithmetic so the wraparound matches without C
  undefined behaviour. The start state is 1: the only srand is in the
  attract-demo start, and the C7 trace measured 1 at the NEW GAME commit.
- Evidence: `src/game/em_random.c`; `tools/test_random_reference.py`,
  `test_random_seed_reference.py`, `test_player_random_reference.py`. Census
  row 0x00122BB8: byte-matched, live. Decomp `CAPTURES_C7.md` 3: 36,512 rand
  calls traced over New Game with the chain intact; 25/25 return values
  checked per stretch; no srand in any stretch.
- Status: **PARTIAL**. The generator and start state are verified. The
  **order** of calls is audited (the random-events entry below;
  `RAND_ORDER.md`): the opening's sequence equals the original's for its
  first four calls and then misses one draw (the husk creature, census L24),
  so the values after it differ. In PCSX2 the original's own call order
  during the opening differs between runs after about 689 calls. The start
  state was measured on a path where the attract demo had not run.

**Nothing knowingly invented: an untranslated piece stops the game with a report**

The port's rules forbid made-up gameplay, UI, dialogue or content. When the
port reaches an original routine it has not translated yet, it stops with a
report naming the routine; it never quietly substitutes something that only
looks right. Invented features from earlier versions of the port have been
found and deleted.

- How: port CLAUDE.md "Fidelity rules" ("A label is not evidence"; missing
  original workers fault; unfinished originals stay built and tested but
  unwired). Chain C8 reduced duplicate translations to one bound owner.
  Earlier fabrications (invented status pages, a "Found" line, a hover cue)
  were audited and deleted.
- Evidence: `FIRST_LEVEL_AUDIT.md` H7/H9 and WP-5; `FIRST_LEVEL_CENSUS.md` 1.4
  and 2.1; commit 6e659ac (one bound owner per duplicated function, with
  mutations failing each owner's test); census 1.31 (three legacy door
  stand-ins retired).
- Status: **PARTIAL**. The rule is enforced, but stand-ins remain on the
  live route (census 2.3: the examine/aim camera, the fan pair's and husks'
  legacy meshes, the flame and snow drawn outside the chain page, 001C1D00's
  empty render-env step, the opening's displayed player, and the
  panel/terminal/item takeovers). Some duplicate translations remain
  (`EE_FLOAT_MODEL.md` 5c). The fail-stop is player-reachable today: choosing
  DATABASE, SPR4 or MAP on the status hub, or taking a non-battery item,
  stops the game.

**The tests are themselves tested (mutation testing)**

A test that always passes proves nothing. Key tests were checked by
deliberately breaking the port's code in small ways and confirming the test
catches each break.

- How: single-change mutants (a flipped comparison, a wrong mask, a dropped
  term, a zero-latency drive) are applied in a scratch copy, the reference
  test or level smoke is run, and the change is reverted. Each survivor is
  recorded with the reason it cannot be observed, or leads to a stronger
  test.
- Evidence: `IOP_STREAM.md` Verification: 17 caught, 1 survivor.
  `EFFECT_MANAGER.md`: 49 of 50. `PACKET_CHAIN.md`: 25 of 27 (2 equivalent).
  `CAMERA_LEFTOVERS.md`: 24 of 26. `COLL_LIST_PASSES.md`: 11 survivors of 117,
  each analysed. Commits e99d8cb (6 mutations killed), 6e659ac and 4366957
  (zero-latency and 7-field-seek drives fail at cage_roof).
- Status: **PARTIAL**. Done module by module, not as one sweep over the whole
  port. Some survivors are documented rather than killed. Mutation runs are
  scratch work; the counts come from the docs.

**Clean-room native code, bring your own disc, no emulator or third-party code**

The port is a native program, not an emulator wrapper. It contains no PCSX2
or other emulator code, links no Sony SDK binaries and uses no third-party
libraries. It ships no game data: you supply your own legally dumped disc,
and the tools extract the assets locally.

- How: built directly on each OS's native APIs (no SDL, GLFW or similar).
  The macOS build links only system frameworks. Assets are exported from the
  user's disc/ELF by `tools/export_*.py` into the ignored `assets/` folder;
  `.gitignore` blocks disc-derived files; `tools/check_no_disassembly.py`
  runs in the pre-commit leak scan. A GPL static recompiler was evaluated
  read-only and rejected; it is not linked, copied or run.
- Evidence: port CLAUDE.md "Hard rules"; Makefile FRAMEWORKS/LDFLAGS; port
  `.gitignore`; decomp `docs/HANDOFF.md` (decision 2026-09-26). PCSX2 is used
  only as an external measuring instrument.
- Status: **PARTIAL**. **macOS only for now**: the Windows and Linux
  backends are skeletons, and the Linux build has no movie playback. Do not
  advertise Windows or Linux yet. The port contains C translations of
  original game and SDK routines by design; "no Sony SDK" means no SDK
  binaries are linked. The decomp repository still commits CodeWarrior asm
  function bodies (user decision 2026-09-23), so the "no original code" claim
  holds for the port repository only. Some textures are still sourced from
  PCSX2 captures rather than the disc (see Visuals); these must move to
  disc-sourced exporters before release.

### Visuals

**Characters and objects are drawn by the game's own VU1 graphics program, translated to C**

In the first level, the crates, drums, truck, fence door, terminal/elevator,
panel, placed prop, pickups, parachute canopy, and the player with all seven
pieces of equipment are drawn from the triangles, vertex colours, texture
coordinates and fog values the game's own vector-unit program produces for
them. No remade models, no guessed lighting shader.

- How: the game's VU1 object microprogram and its clip program were
  translated to header-only C (`em_vu1_object_kernel.h`,
  `em_vu1_object_clip.h`) and run over the same unit that the original draw
  routine 001CAA00 builds live. Metal rasterizes the result.
- Evidence: `VU1_OBJECT_KERNEL.md` 5: all 950 captured units, 16,859 batches
  and 539,488 vertices from 21 captures equal the original microcode; the
  original 001CAA00 over 255 world owner-frames gives 119 drawn units, all
  equal; 46 of 46 injected defects caught. `OWNER_DRAW.md` 8 (full):
  375 owner-frames and 239 units triangle for triangle. `OWNER_DRAW.md` 9
  (live): the smoke's `check_owner_units` matches owners, byte counts, clip
  passes and position rows for all 22 owners at beat 10 and all 20 at beat
  14. Commits de64410, 06610be, eb94265.
- Status: **PARTIAL**. First level only. Verified: vertex and triangle
  output. Not verified: pixels against a GS framebuffer (Metal float
  interpolation, not the GS; the GPU test checks the port's shader against
  the port's own formulas, 99.73%/99.95% of compared pixels). The player and
  equipment are compared live only at beats 10 and 14. VU1 arithmetic is
  assumed to follow the VU0 rules measured in PCSX2. Roger, the fan pair, the
  husks, the indicator children and the opening's player are not on this
  path. Object textures (303 TEX0) are decoded from PCSX2 capture GS memory,
  not the disc, so an end user cannot build them yet. Metal only.

**The original light rig, bit-exact against the original instructions**

Characters and props are lit by the original lighting code: the room's light
directions, colours and ambient, the point-light fold and the player's
camera fill light. The point light's sway is random: it follows the port's
own draws, which sit where the original's do in every frame but carry other
values than a given recording (the random-events entry), so the sway does
not follow a recording frame for frame.

- How: 001D89D0 and everything it calls, including the SDK VU0 routines,
  translated with every float operation through `em_ee_float.h`, bound live
  as the owner draw's lighting worker.
- Evidence: `ACTOR_LIGHT_001D89D0.md` 5: all 375 captured owner-frames equal
  the original; full run adds 6,000 synthetic states (NaN, Inf, denormals,
  -0); the whole 001CAA00 chain with this translation bound is byte-exact for
  the drawn units; 29 of 31 defects caught (2 argued equivalent). Live
  (`OWNER_DRAW.md` 9): the colour matrix and lighting lanes y/z compared at
  route snapshots; the whole lighting rows equal the original's over the
  port's own point-light pool and view at every snapshot (the player and
  its equipment at the camera-exact 10 and 14); `check_sway` runs the
  original 001D7C30 over the port's pool and draws on 46 sampled ticks
  (`RAND_ORDER.md` 5).
- Status: **PARTIAL**. The routine is proven, and the live fold is compared
  over the port's own sway. The glow, other lighting modes and the +0xB0
  light point are exercised on synthetic states only.

**Effects drawn from the game's own packets**

Breath and footstep puffs, pickup glints, glow markers, head sprites and the
standing-on-object shadow decal are built and drawn by the original code and
the original VU1 sprite program.

- How: the effect producers build their packets byte for byte into the
  render context's chain page; `em_chain_page` walks it as the DMA/VIF/GIF
  would, runs the two page VU1 programs (translated, `em_vu1_page_programs.h`)
  and the blend presets of 001D0F20, and hands every GS primitive to Metal in
  GS order.
- Evidence: `CHAIN_PAGE.md` 8: 690 lane and 660 sprite program calls, 15,944
  kicks and 3,660,384 packet bytes equal the original microcode; the latest
  page of every route capture 00..14 draws the same 386 primitives as the
  model. Level smoke `check_chain_page`: 12,991 pages drawn on the full route;
  40 sampled pages re-walked with the original microcode draw exactly the
  port's primitives. Commit d7ef847.
- Status: **PARTIAL**. First level only. Sprite positions/colours follow the
  draws, whose values differ from a capture's at every snapshot
  (`RAND_ORDER.md`); only glow markers are compared with captures (count and
  geometry; their colour as the original 001F4D40 over each side's own
  draw, `check_marker_colour`), and the head sprites' phase follows 001E2560
  over the port's own draws (`check_head_sprites`). Rings are proven on synthetic batches only
  and are not drawn on the recorded route; do not advertise them. 001DDE10's
  four frame-sampling sprites are not drawn. Snow and the AREA11 flame draw
  outside the page. Page and decal textures come from PCSX2 captures. Metal
  only.

**The player's original projected drop shadow**

From first control, the player casts the original silhouette shadow
projected onto the surfaces below, not a generic round blob. Standing on a
crate or the elevator switches to the original's decal shadow, as the
original does.

- How: the original render-to-texture shadow 001DA6A0 runs live (a 128x128
  silhouette, two destination-alpha boxes, receiver re-draws); its two VU1
  clip kernels are translated. The decal route 0015BF90 is taken when the
  player stands on an actor.
- Evidence: `SHADOW_ORIGINAL.md` Verification: the executed original rebuilds
  the chain byte-identically in four captures; the receiver program's output
  equals the native values on all 108 batches (3,456 vertices). Level smoke
  `check_shadow`: 10,631 calls on the full route, 40 sampled calls re-executed
  as original instructions give the port's plan. `SHADOW_DECAL.md` 5. Commits
  c8e658f, d7ef847.
- Status: **PARTIAL**. Inputs and draw plan verified; pixels compared only
  by eye. The decal route is captured only at the ends of beats 02 and 04.
  The shadow is not computed during the opening. Needs framebuffer fetch
  (Apple GPUs). Decal texture from PCSX2 captures.

**Original sky background and world fog**

Where no level geometry covers the screen you see the original sky layer,
not the black earlier builds showed. Distance fog uses the original's
coefficients and colour.

- How: the background matrix (001E1E60) and the 32x32 grid of its VU1 kernel
  are translated and drawn when the translated main-loop step V says so; the
  texels are replayed from the user's own disc. Fog comes from the single
  translation of 0021B920 and the area fog 001D8FD0 on the live render
  context.
- Evidence: `BACKGROUND.md` Verification: the native matrix equals the
  uploaded one bit for bit in 6 captures; 11,904 vertices equal the original
  kernel (126,976 in full mode); disc texels equal the asset and three GS
  freezes. Sky at first control: 6,843 of 6,912 samples black before, 0
  after, mean (48.2, 48.2, 48.2) against the original's (48.0, 48.0, 48.0).
  `tools/test_area11_fog_reference.py` executes the original fog chain
  against captured AREA11 RAM. Commits 8c9a9ad, ce7271f, fc272a7, e99d8cb.
- Status: **VERIFIED** for AREA11, relative to PCSX2. Caveats: the ERLENG
  model is the same on both sides of the test; the shader's final fog
  evaluation is not executed against the original; the +0x1D8 channel-3 list
  is a native stand-in; pixels are Metal sampling, not compared with a GS
  framebuffer.

**Original GS material rules on level surfaces**

Railings, grates and cut-out surfaces use the game's own alpha-test rule and
material state instead of a generic "discard below 50%" shader.

- How: the level's GS state (TEST, ALPHA, TEX1, CLAMP, PRIM) was decoded from
  the original packets and written per texture into the exported meshes; the
  Metal pipeline applies all eight alpha-test compares and refuses state it
  does not implement.
- Evidence: `LEVEL_MATERIALS.md` Verification: every record equals the
  captured registers; mutations caught. Region metrics: railing histogram
  EMD 4.9 -> 3.0, bright fringe 4.42% -> 2.78% (original 0.04%); grate EMD
  5.4 -> 5.8. Commit 1c3eac6.
- Status: **PARTIAL**. The level geometry is still drawn from exported
  meshes, not through the original level VU1 kernel, and its vertex colours
  are the port's rig bake. The fringe is still far from the original's. The
  comparison is region metrics, not a pixel match.

**The status screen, BATTERY page and on-screen messages run on the original UI code**

The pause/status hub, the ITEM > BATTERY page (pickup notice, charge gauge,
Yes/No confirmation, discharge), the help lines and message text follow the
original's layout, timing, input handling and text layout, including how
long notices stay up.

- How: the BATTERY page record 002149F0 and its draws, the hub 0020CDC0 with
  its layout and arcs, the presenters and the message glyph layout are
  translated and bound live.
- Evidence: `STATUS_PAGE_RECORD.md`: 30,000 synthetic cases, both outcomes of
  all 62 branches, write order checked at 116,896 fault points. Level smoke:
  the battery notice lasts route 01's 239 frames; the panel confirmation, Yes
  and the discharge match route 03 row for row. `STATUS_HUB.md`: 17,520 cases;
  500 arc descriptors equal the original. `MESSAGE_GLYPH.md`: layout oracle
  plus a coarse capture check. Commit 097fbd9.
- Status: **PARTIAL**. Only the hub, ITEM and BATTERY pages. MAP, SPR4 and
  DATABASE are untranslated and fail-stop when chosen (player-reachable). The
  area-title card is not bound. Glyph pixels are drawn from the port's own
  atlas (bilinear, where the original samples nearest) and are not compared.
  The module load before the ITEM page is instant by policy (7 ticks against
  the original's 30; see "Resolved and open policy questions").

**The original camera: follow camera, scripted shots and director beats**

The walking camera follows, settles, orbits and retargets for the elevator,
panel, fence door and Roger scenes using the original camera code, and the
original's one-frame view lag is kept.

- How: the camera frame 0018B9C0, the follow update, solvers, area
  specials, event router, lock-on and commit are translated routine by
  routine over one storage of the original bytes.
- Evidence: `CAMERA_LIVE.md` 4 and 6: the original commit with every callee
  over 6,000 random states plus 17 captured states. In the smoke: the
  area-load seat byte for byte; refusal (02), elevator (04), Roger (14) and
  the door (09) row for row; the panel exact from f679; the truck preview
  converges to 1e-5. Commits 53b4378, cfc6372, c7a04a4.
- Status: **PARTIAL**. The opening's camera timeline is still a stand-in
  (releases one settle frame early). The aim camera and the examine shot are
  stand-ins. The slide entry is 0.863 units off (relaxation pending review).

**Recorded reference frames for a pixel-accurate Original profile**

The project holds exact displayed frames, rendered by PCSX2's software GS
renderer, at 18 points along the first level. The Original profile will be
measured against them pixel by pixel.

- How: the decomp's `tools/c7cap_partb.py fb2` loads each snapshot, steps two
  frames and de-swizzles the displayed buffer, draw buffer and Z from GS
  memory.
- Evidence: decomp `CAPTURES_C7.md` 5b (decode proof: block-seam ratio
  0.93..1.15, luma correlation 0.989..0.998; route03_end reproduced byte for
  byte). `PORT_PROFILES.md` "Queued work" 1-2; commit c3d1742.
- Status: **PLANNED**. The captures exist; the comparison harness and the
  GS-exact 512x224 renderer do not. The software renderer is PCSX2's model of
  the GS, not hardware. The field-to-buffer pairing rule is not established
  at 4 of 19 points.

**Still drawn by legacy or stand-in code (disclosure)**

These first-level visuals do not yet come from the original draw path.
Advertise the items above only.

- Evidence: `OWNER_DRAW.md` 11, `CHAIN_PAGE.md` 6, `LOAD_VEIL_PARTICLES.md`,
  census lanes L24/L38.
- Status: **PLANNED**. Roger is drawn from an exported model (the face-morph
  program is translated and proven on 60 face units but its builders are not
  bound). The fan pair is a static prop with **no spin** (the original spins;
  `em_fan_original` is verified but unbound). The husks and the opening's
  player use legacy meshes. The level geometry uses exported meshes. The
  area-load veil particles are translated but unwired, so **the load screen
  is black**. Roger's drop shadow is not computed (whether the original shows
  it is unknown).

### Sound and timing

**Music and voice streams run on the game's own stream code**

Music starts, stops, fades, loops and switches tracks through a translation
of the game's own stream code; nothing is scripted by hand.

- How: the EE stream-lane code (per-frame lane service, disc-read sequencer,
  lane start/restart, fades, volume ramps, voice ring, cue select/resume,
  boot-time setup) translated function by function, all floats through the
  EE-float model. `em_stream_live` is the only owner of the lanes (WP-8b);
  the legacy music stand-ins were deleted.
- Evidence: `STREAM_LANES.md` "Verification", "Capture evidence", "Live
  binding": the original instructions over 26 captured RAM images, 315,956
  single-call cases and 18,200 lockstep frames (2026-09-23); about 33
  mutation controls caught; 383 capture checks; every captured fade-in step
  is 16383/(270+s2) for exactly one s2. Commit c7a04a4.
- Status: **PARTIAL**. First level only. The area-entry and the fence-door
  room move's 001FAE70 run live since the rand() order audit
  (`RAND_ORDER.md` 2). 001FC280 and 001FBC50 run live without
  an oracle of their bodies. Part of 001FB100 is unbound. The last full sweep
  predates commit 7dea4ce. Audio output is not compared.

**Music plays from your disc's exact stream data, buffered by a translation of the PS2 sound driver**

The soundtrack and voice lines come from the stream files on your own disc
and reach the sound voices chunk by chunk as the PS2's driver fed them.
Loops wrap at the same sample. Nothing is re-encoded or shipped.

- How: the stream part of the IOP sound driver was read from the user's disc
  and translated (command ring, driver ticks, half-buffer transfers,
  key-on/off, cursor status words), feeding a 48-voice SPU2 voice model at
  48000 Hz (800.8 samples per NTSC field).
- Evidence: `IOP_STREAM.md` "Verification": 4,656 cases plus 10,240 lockstep
  calls on 16 IOP images; 1,749 of 1,777 translated words executed; 766
  capture checks over 27 images; the cursor matches in 23/23 images,
  including 140 s into cue 25; played samples equal an independent decode for
  3,000,000 samples (a whole loop and its wrap); 17 driver mutations caught.
  Commit 4366957.
- Status: **VERIFIED** for the data path and buffer/cursor state on the
  first level, relative to PCSX2 save states. The audio output itself has not
  been compared with a PCSX2 recording. Not modelled: SPU2 reverb, Gaussian
  interpolation, core master volumes. Transfer completion tick and driver
  tick phase are stated models.

**Voiced lines wait on the disc drive as the recording does**

In the original, a cutscene waits for a voice line to finish, and a line
cannot start until the disc has read it. The port reproduces that read
delay as measured in PCSX2, so conversations keep their pacing: in two of
the three Director beats, the line ends and the scene moves on on the same
frame as the recording. Ordinary loading is not slowed down.

- How: a drive model measured from the C7 capture: one read at a time; the
  position is where the last read ended; a 0-, 2- or 6-field seek by
  distance class; a read of up to 16 sectors completes within one field. The
  model faults on longer reads, which only module loads issue; those run at
  host speed.
- Evidence: `IOP_STREAM.md` "Drive model (measured, 2026-09-27)"; decomp
  `CAPTURES_C7.md` 1: the model equals 186 of 205 captured reads, the other
  19 one field off (sub-field poll phase); the four reads the first level's
  timing rests on took the model's 6 seek fields. `LEVEL_SMOKE.md`: lines
  0x97 and 0x99 change on the capture's rows. Commit 4366957: full route 419
  reads, 0 breaks; zero-latency and 7-field-seek mutations fail at cage_roof.
- Status: **PARTIAL**. Relative to PCSX2's CDVD emulation. Line 0x7F ends 2
  rows early (navigation timing of an earlier music refill). The opening's
  stream request reaches key-on in 12 fields where the original takes 27
  (the area-music read is not issued yet). Two Roger music reads fall outside
  the measured distances. Music timing is not claimed.

**Director beats and the Roger encounter on the original scripts, frame by frame against the recordings**

The three Director beats and the Roger encounter run on the game's original
scripts and owners. Within each scripted window, camera shots, letterbox,
message timing, player takeover/release and Roger's actions land on the
same frames as the recording.

- How: the Director (0x8253F0) and Roger (0x8237E0) run as translated owners
  on the original AREA11 script host with voiced lines on the live stream
  lanes; the smoke compares its tick log with the route captures row by row.
- Evidence: `DIRECTOR_ORIGINAL.md` 4: 22,526 owner cases and 2,318 polygon
  cases; an offline replay of all 12,424 frames of the 15 route traces.
  `LEVEL_SMOKE.md`: `check_director_beat` from route 10 f1090, 11 f706, 13
  f531 to the end of each capture; `check_roger` route 14 f288..f1818 (1,531
  rows), every compared field equal. Commits c7a04a4, 30c7496, 4366957.
- Status: **PARTIAL**. First level only. Line 0x7F's teardown is 2 rows
  early. Roger's clip flags are not compared before his clip init. Skips are
  covered by the oracle only. Roger's departure is bound but not on the
  captured route.

**Status screen stops and restores the music on the original schedule**

Opening the status screen stops sound effects and music in the same game
tick as the original; closing it brings the area music back through the
game's own randomised fade-in.

- How: the frame machine's status open and close run on the translated
  lanes; the status page core, including its exit, is original (WP-5).
- Evidence: `LEVEL_SMOKE.md` "status": ticks around the open and close equal
  status_04; the exit fade equals route 01 f484..f495 row for row; the close
  takes two ticks more than the open, as in the capture. `IOP_STREAM.md`
  "Co-simulation"; `STREAM_LANES.md` capture checks.
- Status: **PARTIAL**. The fade length draws from rand(). The draw sits
  where the original's does, but its value follows the port's stream, so one
  run's fade length can differ from the recording's (`RAND_ORDER.md`). The
  reverb-return volume call is kept but inaudible (no reverb). Sound output is
  not compared. `FIRST_LEVEL_AUDIT.md` 1 still says the menu sounds are
  silent, which later docs contradict; reconcile before quoting.

**First-level sound effects at their original pitch and volume (277-entry registry)**

Every sound effect the first level can request is present except two
modulated tones, each with the pitch, volume and envelope settings the
original sound code sends. Earlier builds played every sound about 7.4
semitones sharp and left footsteps, slides, ladders and the truck silent.
Sounds the original itself plays nothing for in this area stay silent.

- How: a reachability scan of the code the route reaches lists every sound
  id; the exporter resolves each through the original id-to-record-to-script
  path from the user's disc. The native driver (`em_sfx_bank`) runs a
  translation of the original sequencer and pitch ladder.
- Evidence: `SFX_REGISTRY_FIRST_LEVEL.md` 3-4: 277 entries (259 audible, 16
  absent, 2 unsupported), 141 samples; all 139 AREA11-reachable samples,
  loop points included, equal the SPU RAM the original loaded;
  `make test-area11-sfx-reference` (full) 1,355 entry x request cases and
  2,080 voices with pitch, volume, SPU address and ADSR equal to the
  original's commands at every key-on; the 16 absent/unsupported entries
  checked against the original over 32 runs; of the route's 464 original
  sound starts none contradicts the export. `SFX_PITCH.md`: the legacy pitch
  was x1.531..1.542 too high. Commit 7d1b6a9.
- Status: **PARTIAL**. First level only. Verified at the command level, not
  at the audio-output level; nothing compares output with a PCSX2 recording,
  and the smoke does not compare sounds. ADSR stepping is a
  documented-semantics model. 0x424/0x428 need voice modulation, which the
  native driver lacks. Several module docs and census rows (e.g.
  `PLAYER_CLIMB_SLIDE.md`, the truck row) still describe these ids as not
  exported; `SFX_REGISTRY_FIRST_LEVEL.md` 5 says they can be closed. Whether
  the slide now holds one looping track as the original does was not checked
  live.

**The original tick rate: 59.94 Hz, one field per game tick**

The game runs at its original NTSC tick rate, so every animation, fade and
timer counts fields as on the PS2.

- How: `em_frame.c` paces the main loop with absolute host deadlines (period
  16,683,350 ns), with no catch-up bursts, one field per iteration. The field
  bit and step W are driven in the phase the route snapshots hold. The frame
  is shown in the largest centred 4:3 rectangle.
- Evidence: `RENDER_CONTEXT.md` 9.3; `tools/test_render_context_live_reference.py`
  (steps V/W for both slots and fields). `MAIN_LOOP_AND_GAP.md` 2.4: across
  all 15 route snapshots the vsync-minus-frame offset is constant and the
  frame counter advances by exactly one per traced frame over 12,439 rows,
  so under PCSX2 the original ran one field per main-loop iteration on the
  whole recorded route, which is what the port does. Commits e99d8cb,
  dee7325.
- Status: **PARTIAL**. The tick-per-field logic and field phase are verified
  on the first-level route. The Original profile's exact 512x224 field
  output is not built yet (the port renders at the host resolution with
  Metal filtering). Pacing is a host timer, not display vsync; its period is
  about 17 ns longer than exact 60/1.001 Hz (about one field of drift every
  4-5 hours). Not measured by any test (tests run uncapped).

### World and gameplay

**Along the recorded route, the first level plays out tick for tick like the PCSX2 recordings**

If you play the first level (AREA11) along the recorded route, the game
advances one tick at a time exactly as it does in PCSX2. This covers the
battery, the terminal refusing you, powering the panel, the elevator ride,
the crate climbs, the hill slide, the truck set piece, the ladders, the tank
and pipe climbs, the crevice jump, the tower climb, the director's three
voiced beats and the meeting with Roger. The player's state, animation clip
and clock, the scripted camera shots, the letterbox, the messages and the
fades change on the same ticks as in the recordings.

- How: a headless "level smoke" starts from New Game and plays the port
  along the original route with closed-loop stick input (pad input only; it
  never sets positions or state). It checks each phase twice: once inside
  the running game against facts from the original, and once by replaying
  the tick log against the PCSX2 route recordings, row for row. A phase
  whose original owners are not live reports NOT-LIVE, and the full target
  counts that as a failure.
- Evidence: `LEVEL_SMOKE.md` "Phases", "What the live phases check", "What
  the full route does not yet compare". `make test-level-smoke-full` runs
  with `--require-through`: route beats 01..14 on the main line (18 phases),
  plus side beat 00 (the panel without the battery) and side beat 09 (the
  fence door), each in its own run. In the census 1.22 recount (2026-09-26)
  all 18 main-line phases passed and their capture checks passed again on
  the tick log. Census 1.30 (HEAD 4366957) and 1.31 list the same full
  target as passing. The truck preview compares every row from f164 through
  the release at f527, plus 25 rows after it. Roger's encounter compares
  1,531 rows (f288..f1818) with every compared field equal. Mutations fail
  the checks: truck placement Y +0.001 fails at f167, slide entry speed
  0.2→0.21 fails at f82, the ladder step 3.0→3.0156 fails at f356, the
  running-jump launch speed 1.8→1.8005 fails at f240, a zero-latency or a
  7-field-seek drive fails `check_voice_drive` at cage_roof.
- Status: **VERIFIED**. The reference is the PCSX2 recordings, not a real
  PS2. Only the first level (AREA11) is covered, and only route beats
  00..14. The level exit (beat 15) is a separate PLANNED entry. The smoke's
  own walking between the scripted and climbing windows is navigation and
  is not compared. Relaxations the smoke reports (`LEVEL_SMOKE.md` "What the
  full route does not yet compare" and "Known divergences"): (1) the status
  page's module-0x21 load: the original waits 24 loader dispatches before
  the BATTERY prompt, both at the battery (route 01) and at the panel (route
  03). The port's prompt comes 7 ticks after the request instead of 30, and
  that window is not compared (see "Resolved and open policy questions" below). (2) The
  voiced line 0x7F tears down 2 rows early because a music refill lands at
  a different phase. That phase comes from navigation timing. (3) Slide and
  beat-10 landings may land one row off, and slide heading changes two rows
  off, because the slide starts from a stance up to 0.86 units away (marked
  "pending lead review"). (4) Roger's idle-clip flags and the equipment's
  +0xB0 before f358 are exempt (navigation). (5) The status page's mode-4
  messages are skipped. The smoke does not compare sounds or pixels. Paths
  off the route are not exercised: damage and death, pause/options/save,
  weapons and aiming, the truck-pit fall, and the west-yard and plateau
  ladders (census section 6).

**The first-level game logic is the original code, translated and checked function by function**

Along the first-level route, the game's rules run on translations of the
original routines, and each translation is checked against the original.
Earlier stand-in code that invented behaviour has been removed: an invented
truck fall, a fan spinning at a constant 1 degree per frame, the wrong
director sounds, fake Continue stats, and a door walk at an invented 15
units per second.

- How: a census ran the original game hidden in PCSX2, with a one-shot
  breakpoint on every known function entry for each route label. It
  recorded which functions execute from the title screen through route
  beats 00..14: 1,184 functions in all. Each function was then classified
  against the port. A function counts as "live" only when its translation
  runs in the shipping build and an oracle that executes the original
  instructions, or a capture comparison, checks it. Comments and labels do
  not count as evidence. Instrumented builds measured which functions
  actually run.
- Evidence: `FIRST_LEVEL_CENSUS.md` sections 1.1, 2.1 and 2.3. Totals as of
  section 1.30 (committed at HEAD) and unchanged in 1.31 (uncommitted): 660
  of the 741 non-boundary functions are live and verified (89.1% of
  functions; 80,726 of 87,968 instructions, 91.8%). 77 are verified
  translations that the live app does not run yet, 3 are unverified
  (0015CF90, 001B1190, 001FC280) and 1 is missing (001CB3C0). 443 are
  platform boundaries (SDK, IOP, GS and similar). No row is classified
  stand-in. The last whole-route liveness measurement, section 1.22
  (2026-09-26), used an edge-recorder build that confirmed all 634 live
  rows of that time. Later rows were moved step by step, each with its own
  evidence. `FIRST_LEVEL_AUDIT.md` sections 1 and 4 list the removed
  fabrications (WP-0..WP-2; H8 fixed in 9d4a631; H13 via census L18; H16
  via census L23; H20).
- Status: **PARTIAL**. The census counts only functions the recorded route
  executes, once per label. It does not record which jump-table cases the
  route used (census 7.1). Beat 15 (the level exit) is not in the census
  tables. 0015BCF0 is live only in part. The four functions without a
  verified live translation include 001FC280's body and the player's face
  attachment draw 001CB3C0. "No stand-in rows" does not mean no stand-in
  code runs. Census 2.3 still lists stand-in behaviour on the route: the
  camera stand-ins that pre-empt the examine and aim actions (L28), the
  indicator children's +0x4C draw, the fan pair's and the husks' legacy
  meshes and logic (L24), parts of the chain page (the four-sprite pass, the
  AREA11 flame, the snow), the empty render-env step 001C1D00, the port's
  player mesh during the opening, and the interaction runtime's acquire and
  per-stage tick for the panel, terminal and item takeovers. Census section
  6 notes that oracle strength varies (the fade oracle compares against
  compiled decomp C, and the spawn helpers are checked only for spawn set
  and order).

**The original collision world: the same walls, floors, ledges and object hitboxes**

You bump into, stand on and climb exactly the surfaces the original game
uses. The crates, drums, truck, terminal/elevator platform, panel, items,
prop and canopy put their collision into the world as the original does,
and movement and camera collision queries hit the same geometry.

- How: the original collision library is translated from its instructions:
  the cell and grid walkers, the segment and camera queries, the
  floor/surface/object probes, the move walkers, the grid pass and hull
  locks, the column tables and the actor list passes. Every world object
  the port runs publishes its collision cells through the translated
  re-transform 001A2370 and the publication routine 001B1B70. Float
  arithmetic goes through the shared EE float model.
- Evidence: `COLL_GRID_HULL.md`, `COLL_MOVE.md`, `COLL_PROBES.md`,
  `COLL_SEGMENT_WALKERS.md`, `COLL_LIST_PASSES.md`, `ACTOR_COLLISION.md`.
  Oracles: `test_coll_grid_hull_reference`, `test_coll_move_reference`,
  `test_coll_segment_walkers_reference`, `test_coll_probe_reference`,
  `test_coll_list_passes_reference`, `test_actor_collision_reference`.
  `tools/test_collision_world_capture.py` plays the live game through the
  elevator ride and dumps the cell directory and the published class-4 list
  twice: after the area's first frame, compared with route capture 00, and
  at the last frame, compared with route capture 04. The terminal cell (uid
  4) equals the original's bytes at both floors, the item cells (uids 19,
  21..25) and the truck (uid 14) equal them too, and every other uid equals
  the disc directory. Census 1.25: the published class-4 list at beat 04 now
  equals the original's exactly and in order (`CRATES_DRUMS_ORIGINAL.md`:
  [4, 18, 10, 9, 8, 7, 23]). Census 1.28: 0019F330 is live, an instrumented
  run counted 65,478 calls and 103 crossings over the full smoke, and
  `test_actor_collision_reference` and `test_player_climb_reference` now run
  the original 0019BC40 with 0019F330 and its SDK calls unhooked.
- Status: **VERIFIED**. This covers first-level (AREA11) collision only,
  relative to PCSX2 captures. The world capture check compares two
  snapshots (beats 00 and 04), not every frame, and the later beats are
  covered only through the movement phases of the level smoke. One original
  cell is not published yet: the husk's plate (uid 15), which waits on the
  husk owner (L24). The test lists it instead of hiding it. There is one
  known intentional deviation, which the recorded route never triggers
  because the smoke would stop with a fault there. The original 0019BC40
  keeps up to 20 candidates but aliases its result arrays past 16. The port
  holds 16 and stops with a fault above that instead of reproducing the
  aliasing (`PLAYER_CLIMB_SLIDE.md` section 3). Float exactness is relative
  to the EE float model measured on PCSX2 v2.6.3 (chop/DAZ/clamp settings),
  not hardware.

**Climbing, sliding, ladders, step-offs and the running jump move like the original**

The crate climbs, the hill slide, the ladders, the tank and pipe climbs,
stepping off edges, the crevice jump and the tower climb have the same
states, animation clips, clocks and timing as the original. From the same
starting stance they end in the same place.

- How: the player's Use dispatcher, ledge climb, slope slide, floor
  service, fall check, ladder entry and climb, running jump and landing
  states are translated. They run on the live player record through the
  original state tables. An EE interpreter that runs the original player
  stage 0015BCF0 unmodified over captured AREA11 RAM produced the reference
  outcomes, and the oracles execute the original routines.
- Evidence: `PLAYER_CLIMB_SLIDE.md` section 2 (crate, stack, vault and
  slide outcomes from the original stage over captured RAM),
  `PLAYER_LADDER_ENTRY.md`, `PLAYER_LADDER_CLIMB.md`,
  `PLAYER_RUNNING_JUMP.md`, `PLAYER_FALL.md`, `PLAYER_FLOOR.md`. From
  `LEVEL_SMOKE.md`: `boxes` compares 91 rows exactly and lands on the
  original ground records at y 203.776 and 217.786, with X/Z displacement
  within 0.0062 and 0.0051. `slide` compares 122 rows, the authored downhill
  headings come in the original's order, and after the landing every step
  is equal within 0.00002. `cage_ladders` has ladder A f268..f581 and ladder
  B f780..f1090 with X/Z exact and the lift within 7.2e-6. `crevice_jump`
  compares f230..f304 with the step lengths within 2e-5. `east_tower_climb`
  compares f438..f531. `check_fall` shows beats 11 and 12 landing on the
  original's rows. Oracles: `test_player_slide_reference`,
  `test_player_climb_reference`, `test_player_ladder_entry_reference`,
  `test_player_ladder_climb_reference`,
  `test_player_running_jump_reference`, `test_player_fall_reference`,
  `test_player_floor_reference`.
- Status: **VERIFIED**. Compared against PCSX2 captures on the recorded
  route only (first level). The smoke's walk up to each move is its own
  navigation, so its starting stance differs from the original's, and the
  end X/Z follows that stance: the crevice climbs' X/Z residuals are 0.1385
  and 0.1097 from stances 0.138 and 0.407 away, and the running jump's
  take-off heading is -2.99699 against -2.97704. The slide starts 0.86 units
  off, so its node crossings may come up to two rows off and its landing one
  row off (a relaxation marked "pending lead review"). Beat 10's step-off
  lands one row earlier because it starts from a different height. The
  running vault (state 3) appears only in the whole-world oracle and is not
  on the route. The plateau and west-yard ladders and the truck-pit fall are
  not on the route and are not exercised. The movement sounds are not
  compared.

**Walking, running and stopping cover the same distances as the original**

From first control, the character accelerates, walks, runs down and stops
over the original distances, because the motion numbers come from the
original code.

- How: the idle and walk states 00161020 / 001612D0, the motor 0017BC40,
  the foot-stop 0017B910 and the locomotion workers run on the player record
  (census L12). The first-control hand-off and three input fixtures are
  compared with PCSX2 captures of the same inputs.
- Evidence: `FIRST_CONTROL.md` (top summary) and census 1.12.
  `newgame-control`: 30 input ticks travel 9.599849 units, the original's
  figure, and end at the original's XZ (245.475342, 216.987808).
  `EM_CONTROL_STOP_TEST`: 60 released ticks travel 18.649738, the
  original's (the old legacy walk gave 18.649982).
  `EM_CONTROL_REENTRY_TEST`: final XZ (238.753983, 226.391403) against the
  original's (238.753982, 226.391403). `test-first-control-reference`: over
  56 callbacks, the record's pose source, locomotion state, step phase +25E
  and feet equal the original's actor bytes. `LEVEL_SMOKE.md`
  `first_control`: the task, spad, request and fade bytes equal slot 04 /
  route 01 f0. Census 1.26..1.31 each re-ran `newgame-control` and still got
  9.599849.
- Status: **VERIFIED**. First level; measured against PCSX2 captures of
  these specific inputs only. The level smoke does not compare the walks
  between scripted windows (navigation). The heading (+C4) is exact except
  on 3 of the 56 callbacks, which follow a one-ulp difference in the live
  camera's D_008106A0. The re-entry endpoint differs by one unit in the last
  printed digit of X. The low-gait variants have no original capture
  (native only).

**From first control on, every player animation clip comes from the game's own bank and chains as the original does**

Once you have control in the first level, every move the player character
makes uses the game's own animation data, read from your disc, and plays
with the original blending and follow-on rules. The character and the
equipment are drawn from the same skeleton the PS2 computes.

- How: the player clip bank is exported from the user's disc and checked
  against captured RAM. The clip clock, the channels and the skeleton live
  on the player record and are worked by the translated pose-host routines
  (`em_pose_host_workers`, `em_player_stage_anim_advance`). The draws of the
  player and its seven equipment nodes run the original object draw
  001CAA00.
- Evidence: `PLAYER_CLIPS.md`: the bank equals captured RAM byte for byte
  in all 23 captures that hold the player record (section 3). The live
  path's oracle, `test_player_record_pose_reference` (section 6), with
  `EM_TEST_FULL=1` ran 1,968 clip cases over 16 images (126,720 callbacks),
  every one exact, and re-evaluating the captured records gives the
  captured node matrices byte for byte on all 16 images. Census 1.23:
  `test_object_unit_reference` checks the player's 15 and the equipment's
  105 captured owner-frames, and every triangle equals the original
  microcode's. `LEVEL_SMOKE.md` `check_owner_units`: in the camera-exact
  snapshots 10 and 14 the player and all seven equipment nodes stand at the
  snapshot's point with its pose, and all owners equal the original units in
  bytes, clip pass and position rows (22 owners at 10, 20 at 14).
- Status: **VERIFIED**. First level only, relative to PCSX2. The
  3,459,456-field `test_pose_chain_reference` run belongs to
  `em_pose_chain`, a verified translation that is not bound. It is not
  evidence for the live path. During the opening cinematic the displayed
  player is still the port's own mesh (design risk 2): the smoke reports
  1,302 post-steps there instead of drawing the original unit. A port
  stand-in's frames (for example the camera's examine/aim stand-ins, L28)
  keep the legacy baked display. The player's face attachment slot waits on
  the attachment draw 001CB3C0, which is still missing. The whole lighting
  rows are compared over the port's own point-light sway (the light-rig
  entry).

**The crates, drums, truck, elevator, panel, terminal and battery run on their original records**

The interactive objects on the first-level route behave as they do in the
original: the crates and drums, the truck set piece, the terminal and the
elevator ride, the power panel, and the battery pickup. They have the same
states, timings and positions, and their draws come from the original data.

- How: each object's original owner routine is translated and bound over
  its own pool record. Placement, model binds, collision publication and
  the per-frame draw unit all follow the original. The legacy meshes and
  legacy object code for these objects are retired.
- Evidence: `CRATES_DRUMS_ORIGINAL.md`: all four crates and both drums
  equal route 04 in every modelled span, and crates r4/r3 equal route 05
  row for row. `TRUCK_ORIGINAL.md`: `test_truck_original_reference` runs 204
  rotation cases, 914 trigger cases and a 175-tick standing timeline, and
  its `--compare-capture` mode replays a 230-frame PCSX2 capture. The
  smoke's `truck_preview` and `truck_crossing` phases compare row for row.
  `AREA11_ELEVATOR.md`: 2,520 original-instruction cases plus 240 carry
  cases, and the ride matches route 04 row for row. `PICKUP_OWNERS.md`:
  6,216 lifecycle/event/height cases, 144 consume-helper cases and 1,104
  facing cases, and the item cells equal captures 00 and 04 byte for byte.
  The panel matches route 03 row for row outside the status page's module
  load (`AREA11_PANEL.md`, `LEVEL_SMOKE.md`). Census 1.25: the terminal,
  panel, prop, items and canopy draw their original units, and
  `check_indicator_children` compares against routes 00..14.
- Status: **VERIFIED**. Covers AREA11 and the recorded route, relative to
  PCSX2. The BATTERY prompt window is not compared, at the battery (route
  01) or at the panel (route 03): the original's module-0x21 load takes 24
  loader dispatches (a 30-tick prompt window against the port's 7). Under
  the host-speed disc policy the I/O part of that difference is intended (see "Resolved and open policy questions" below). The map and the other non-battery items are drawn and
  animated on their records, but taking one (types 0x1E/0x1F, 0x10, key
  0x32, map 0x08) opens a status page that is not bound in AREA11
  (002160B0, 00211970, 00214020, 0020F950), and the port stops with a fault
  (`PICKUP_OWNERS.md` "Requests and pages", fixture `other_take`; still true
  at HEAD). These takes are off the recorded route. The fan pair and the
  husks are not on their records yet (PLANNED entry). The panel, terminal
  and item takeovers still use the port's interaction runtime for acquire
  and per-stage ticking.

**The fence door and the room move, from both sides**

Using the fence door lines you up, plays the door animation, fades and
moves you to the other side with the original timing. Going back through
from the far side, you walk out of the doorway with the original 52-frame
stand, the steady walk and the slow-down.

- How: the door owner 001BC350 and its program run on the AREA11 script
  host. The room move is the original frame machine's state 4. On side 1
  the arrival walk-out is the player's own stage (+4 = 5), which runs the
  translated 0015B610 and 00183250 and replaces the old legacy walk-out
  code.
- Evidence: `DOOR_ORIGINAL.md` and `LEVEL_SMOKE.md` `fence_door` (route 09,
  side beat, census L18, commit b7868e1): row for row from the scan at f309
  to f532 (224 rows). `fence_door_side1` equals the decomp's C7 DOOR1
  capture (decomp `CAPTURES_C7.md` section 4) row for row from the Use scan
  at f228 to f544 (317 rows). `test_door_original_reference` covers every
  flag answer, request byte and armed byte against the original 001BC350,
  plus the 66-frame replay of route 09's phases 4/5 (census 1.28).
  `test_player_floor_reference` executes the original 0015B610 and
  00183250, including the whole 114-frame walk-out on one record. A mutation
  of 00183250's standing timer (49 instead of 50) fails at f422. Census
  1.31.
- Status: **PARTIAL**. Relative to PCSX2. Side 2 (route 09) is committed and
  verified. Side 1 is verified only in the port's uncommitted working tree
  while chain C7 runs: census 1.31, the `fence_door_side1` phase and
  `DOOR_ORIGINAL.md` "Side 1" are not in HEAD dee7325. Treat side 1 as
  VERIFIED only once it is committed. Only one press stance was captured for
  side 1 (`CAPTURES_C7.md` section 4). The walk-out routines 001833F0,
  00183440 and 001834E0 are not translated and fault by design, because no
  first-level record reaches them. The legacy `em_door` walk-out remains for
  scenes without an original roster (not the first level).

**Scripted sequences take and release the player as the original does**

When the game takes control for a scripted moment, it takes and hands back
control on the same ticks as the original. That covers the truck preview
camera, the fence door, the director's three voiced beats (cage roof,
crevice prompt, east tower) and Roger's encounter. The camera shots, the
letterbox bars, the messages and your character's placement follow the
original script, and the voiced lines hold the scenes for as long as they
did in the recordings.

- How: the game's own level scripts run on the translated script
  interpreter and host ops. A script owner's frame is the player stage's
  own takeover, as in the original: the translated prelude admits the
  player, the stage runs each step, and the original release 00182DF0 hands
  control back. The voiced lines play on the stream lanes. Their timing uses
  the disc-drive model measured in PCSX2, because the scripts wait for each
  line to end.
- Evidence: `AREA_SCRIPT.md`, `SCRIPT_HOST_WORKERS.md`,
  `PLAYER_STAGE_WORKERS.md` section 2.1, `DIRECTOR_ORIGINAL.md`,
  `STREAM_LANES.md`, `IOP_STREAM.md` "Drive model". `LEVEL_SMOKE.md`
  `check_stage_takeover` requires +4 = 4 from the admission to the release
  in routes 07, 09, 10, 11, 13 and 14, with the admissions on the captures'
  first 3B8F = 1 rows and the releases on their first 3B8F = 0 rows. Before
  the drive model, beats 11 and 13 released 6 rows early; now they release
  on the capture's rows (census 1.30, commit 4366957). The drive model
  equals 186 of the 205 captured reads. Lines 0x97 and 0x99 tear down on the
  capture's rows (`check_voice_drive`). `AREA_SCRIPT.md`: the script sine
  translation equals the original instructions on 88,818 arguments (full
  sweep). `test_player_cinematic_reference` covers 1,388 stages over bank
  0x96. `test_director_original_reference`'s SDK atan2f part is
  bit-identical on 726 cases (quick) and 18,366 (full), plus the owner-tick
  sweeps.
- Status: **VERIFIED**. First level, relative to PCSX2 recordings. The
  drive model's seek and read timings come from PCSX2's CDVD emulation, not
  measured hardware. 19 of the 205 captured reads are one field off,
  because of a sub-field poll phase that no capture records. Line 0x7F
  tears down 2 rows early because a music refill lands at a different
  phase. That phase depends on how long the player has walked since the
  music last started (3583 fields in the original against 3449 in the
  port's run), so it is navigation, not a mechanism difference. The panel,
  terminal and item takeovers still go through the port's interaction
  runtime for acquire and per-stage ticking; only their release is the
  original 00182DF0.

**Roger's encounter plays out as in the original**

The running jump onto Roger's tower, his conversation, the scripted camera
and your final placement follow the original tick for tick.

- How: Roger's owner 008237E0, his quad trigger and his script run on the
  AREA11 script host. The cutscene camera runs the original bank-0x96
  timeline 0022EEF0, and the player's scripted clip runs through 00183090.
  The equipment is the original 001C5C90.
- Evidence: `ROGER_ACTOR_ORIGINAL.md`, `ROGER_CINEMATIC.md`,
  `LEVEL_SMOKE.md` `roger` (census L22). The smoke compares rows
  f288..f1818 (1,531 rows): spad bytes, camera byte, letterbox, messages,
  fade block, Roger's record and script block, the equipment node, the
  player's +5/+1F0/+1F1/clip/clock/+2F3, the camera eye and target during
  the timeline, and the 01/9 placement at f1756. Every compared field is
  equal on every row. `test_roger_actor_original_reference` with
  `EM_TEST_FULL=1` runs all 11,679 unit cases, 3,000 float cases and 19
  captures. `test_roger_encounter_capture` is a further capture check.
- Status: **VERIFIED**. Relative to the PCSX2 route 14 capture. Before his
  clip starts at f358, Roger's idle-clip flags (+0x1FE) and the equipment's
  +0xB0 are exempt. Their phase depends on the time since the area load,
  which the smoke's walk does not share with the capture (a navigation
  exemption, pending lead review). The script's landing wait takes 4 ticks
  in the port against 5 rows in the original; this comes from the jump's
  navigation. Roger's voiced conversation is covered by the
  scripted-sequence entry. Roger's departure at the level exit is not bound
  (see the level-exit entry).

**The husks and the fan pair on their original code**

When this lands, the husk creatures and the two big fans will behave and
look as in the original. That includes the fans' timed spin-up, hold and
spin-down cycle, which decides when you can pass the fan safely on the way
to the exit.

- How: the husk creature 00825940, its partner 00827490, the manager
  00823CE0 and the fan 00827630 are translated and checked by
  original-instruction oracles. They are not bound into the game yet
  (census lane L24 / WP-11).
- Evidence: `SCRIPT_DOOR_FAN.md` (L24) and `FAN_ORIGINAL.md`; oracles
  `test_script_door_fan_reference` (part 3) and
  `test_fan_original_reference`. The census rows for 0x00823CE0,
  0x00825940, 0x00827490 and 0x00827630 are verified-unbound, and census
  section 2.3 lists the fan pair's and the husks' draws (their legacy
  meshes) as still standing in. `FIRST_LEVEL_EXIT.md` section 1 records the
  fan cycle during beat 15.
- Status: **PLANNED**. Today the live game runs the legacy `em_enemy.c`
  `em_enemy_update` for the husks, with an interim child spawn, and draws
  the fans static with no spin. Per `FIRST_LEVEL_AUDIT.md` H20 (last updated
  as PENDING), the fans may also still lack the original ±π/4 initial roll.
  The husk translation is itself partial: lifecycles 1 and 4 fault. The
  husk manager is dormant on the first visit (it waits on D_00810788). None
  of this may be advertised as original until it is bound and checked
  against a capture.

**The level exit into level 2 (route beat 15)**

When this lands, leaving AREA11 will play Roger's departure, the fan
crossing, the exit movie and the move into the next area on the original
schedule, with loading at your machine's speed.

- How: beat 15 is recorded in PCSX2 and documented frame by frame: the
  player waits for the fan's slow phase and crosses it, the fan sets its
  exit bit, Roger's departure script walks the player and plays the movie,
  and the script's area request leads into the AREA01 arrival. The port
  needs the fan and Roger's departure bound, plus the AREA01 area data,
  before it can run this.
- Evidence: `FIRST_LEVEL_EXIT.md`: route table row 15 and the
  frame-by-frame table, with the fan crossing and the start of Roger's
  departure script at f344, the movie inside f442, the area request
  001B0C60(1, 0, 4) from Roger's script at f445 (001AD010 at f446, 001FF080
  at f447), the loader done at f658, a post-load wait with the veil
  f660..f739, and control in AREA01 at f741. Census section 6: "the level
  exit ... is not in the census". The census row 0x00827630 is
  verified-unbound, and `FIRST_LEVEL_EXIT.md` lists Roger's departure as not
  bound live (audit H3).
- Status: **PLANNED**. Not live: the first level cannot yet be finished in
  the port. The fan does not issue the area request on this route. Roger's
  departure script does (the fan's own exit applies only on a later
  return). Under the user policy, the 296 frames under black between the
  area request and AREA01 control are not all load time. The loader runs
  roughly f447..f658, and an 80-frame post-load wait with the veil follows
  (f660..f739). Which of these frames are load waiting (host speed) and
  which are choreography has not been decided. The movie itself took 79 to
  254 s of PCSX2 host time inside one game frame, so its original on-screen
  length has not been measured.

**Random events (light flicker, sprite variations, puffs) in the original order**

Small random details such as the swaying point light, the glow markers'
pulse, the head sprites and the music's fade length draw their random
numbers at the same places in each frame as the original.

- How: the original rand() (00122BB8) is byte-matched in the decomp and
  live in the port (`em_random.c`). The C7 capture recorded every rand()
  call with its caller, frame and value; the port's `EM_RAND_TRACE`
  records its own, and `tools/rand_order.py` compares the two. The audit
  bound 0x1AE040's area-entry and room-move 001FAE70.
- Evidence: `RAND_ORDER.md`; `make test-rand-order`; the level smoke's
  `check_rand_order`, `check_sway`, `check_marker_colour` and
  `check_head_sprites`. From the area entry the port equals the original
  call for call (caller and value) for the first four calls; every frame's
  fixed-schedule callers (the sway, the indicators, the glow markers, the
  music) equal the original's over the opening, the 30 frames after first
  control and two aligned route windows (01: 66 frames, 10: 311 frames).
- Status: **PARTIAL**. The opening misses one draw at its second frame
  (the husk creature, census L24), so its values differ after it. Its
  faces draw from the opening's stand-in actors, not from the player stage
  and Roger's owner (design risk 2). A recording's exact values cannot be
  reproduced: the original's own order in the opening varies between runs
  after about 689 calls, and the port reaches each moment by its own route.
  The smoke therefore checks each random value as the original code over
  the same draws.

**Level 2 (AREA01) groundwork: translated and checked, not playable yet**

The second level's route has been recorded, and the code it newly needs is
being translated and checked against the original ahead of time, so level 2
can meet the same standard. It is not playable in the port yet.

- How: phase 1 recorded the original AREA01 route in PCSX2, ran a census
  delta of the new functions, wrote an area overview and matched the
  overlay in the decomp. Phase 2 translates each new function on its own
  and checks it with an original-instruction oracle over the recorded
  AREA01 RAM, comparing memory at every callee entry. The AREA01 assets are
  exported locally and checked byte for byte against the captures. Phase 3,
  binding it into the game, starts only after the first level is done.
- Evidence: port commits e21bd95 (wave 1: 90 translations, split into
  overlay 14, math 30, render 21 and sys 25, plus asset exports) and efe9f83
  (wave 2: 65 side-path and exit translations, split into UI 17, side 20,
  exitA 12 and exitB 16). `AREA01_OVERVIEW.md`, `SECOND_LEVEL_ROUTE.md`,
  `AREA01_*.md`. Decomp `docs/HANDOFF.md` "Level 2": the census delta has
  154 new functions (89 on the main line). Overlay matching bdd40fb: 32 of
  41 functions are byte-identical C and 3 are NEARMISS. The lanes found
  wrong decomp NEARMISS bodies, which were then corrected: 7 in 07c4e32, 2
  in 837d548 and 7 more in f799141.
- Status: **PLANNED**. Nothing is bound: the Makefile is unchanged and
  nothing of level 2 runs in the game. The mutation sweeps did not converge
  and were closed on named survivors. Known binding blockers:
  `em_actor_cells` rejects AREA01's cell directory, and
  `em_coll_segment_walkers` returns -1 on 0019D770's no-span path (decomp
  HANDOFF). The HANDOFF's "33 of 41" disagrees with the bdd40fb commit and
  `PROGRESS.md`, which both say 32.

---

## What we deliberately do not emulate

The port reproduces the game, not the console. The rule (user, 2026-09-27):
**the original code is the oracle.** Everything the game's code does is
reproduced, including the 59.94 Hz tick its logic counts. Timing that comes
from the PS2 hardware rather than the code is not reproduced by default.

- **The disc answers at host speed.** Loads, module loads and streamed audio
  are served as fast as the host allows. The game's own code still runs every
  step it runs on the PS2 (for example the status panel's loader keeps its own
  state steps); only the hardware wait is gone.
  - **Optional switch:** the PS2 disc-drive timing measured from the PCSX2
    recordings (`IOP_STREAM.md` "Drive model") can be turned on for
    PS2-identical dialogue timing. It is off by default
    (`LAUNCHER_OPTIONS.md`; moving today's always-on model behind the switch
    is queued).
- **No PS2 slowdowns or hitches.** The port runs one game tick per field. On
  the recorded first-level route the original under PCSX2 also ran one field
  per iteration (`MAIN_LOOP_AND_GAP.md` 2.4); the extra fields seen in some
  captures came right after PCSX2 loaded a save state and are emulator
  artifacts (decomp `CAPTURES_C7.md` 5b).
- **No CRT, scanline or interlace simulation.** The Original profile shows the
  exact GS frame at 4:3 with no smoothing. How the 512x224 fields are placed
  on a progressive screen is a pending user decision (`LAUNCHER_OPTIONS.md`);
  it is presentation only, so the frame's pixels stay exact either way.
- **No emulated hardware.** The original's VU1 microprograms, the sound driver
  and the float rules are translated to C and run natively; nothing interprets
  PS2 machine code at run time. The interpreters exist only in the test tools,
  to check the translations.

Enhanced options (resolution, frame rate, controls, cut content and so on) are
separate switches over the same logic (`PORT_PROFILES.md`,
`LAUNCHER_OPTIONS.md`).

---

## Resolved and open policy questions

Resolved by the user on 2026-09-27:

1. **The panel page's load (H7):** the loader's own state steps are game code
   and stay; the drive's I/O time is hardware and goes (host speed). The
   smoke's panel-prompt check aligns on the load's completion.
2. **The opening's stream timing:** the extra seek from the intro movie's disc
   position is not modelled (the code does not model it). The area-entry
   001FAE70(1) is game code and is bound (`RAND_ORDER.md` 2).
3. **Field presentation:** deferred; the user will compare the options
   (`LAUNCHER_OPTIONS.md`).
4. **High frame rates:** an Enhanced display rate keeps logic and streamed
   audio at 59.94 Hz with rendering decoupled (`PORT_PROFILES.md`).
5. **The "PS2 hitches" example:** dropped; see above.
6. **The load screen:** the load veil is game code, so it must be shown for
   however long the host load takes; wiring it is queued.

Missing faithful behaviour that blocks a "first level complete" claim:

- player-reachable fail-stops: DATABASE/SPR4/MAP on the status hub and
  non-battery item takes;
- the rand() order's two remaining differences: the husk creature's draw
  (census L24) and the opening's faces (design risk 2) (`RAND_ORDER.md` 6);
- the load veil (above);
- audio output: no SPU2 reverb, Gaussian interpolation or master volumes;
  sounds are not compared in the smoke;
- visuals: pixels not compared with the reference frames; the GS-exact
  renderer is queued; the fan does not spin; some owners draw legacy meshes;
- disc-sourced textures: object, page, decal and status-model textures and
  the font atlas still come from PCSX2 captures or a RAM dump, which end
  users will not have;
- platforms: Windows and Linux have no renderer yet.

---

## Maintaining this list

- Every chain step or lane that makes a player-facing behaviour provably
  original adds or updates its entry here, with the doc section, test name,
  commit and numbers, in the same commit.
- Entries move PLANNED -> PARTIAL -> VERIFIED only on evidence: an
  original-instruction oracle or a PCSX2 capture comparison, with the
  behaviour live in the game. A doc description, a label or a by-eye
  screenshot comparison does not promote an entry.
- Caveats are removed only when the gap they name is closed, and the closing
  commit is cited.
- Numbers are re-checked against their source before any public quote;
  decomp counts need a fresh `tools/decomp/build.py build` and `verify_all.py`.
- No disassembly, game text or game data in this file
  (`python3 tools/check_no_disassembly.py`).

Last updated: 2026-09-27.
