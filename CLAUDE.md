# CLAUDE.md — Extermination native port

Persistent project instructions. Load every session. (Rewritten 2026-09-23; the
previous ~500-line version's feature claims were largely unverified or wrong —
see git history only if you need an old note, and treat it as unreliable.)

## What this is

The **native source-port** companion to the Extermination (PS2, SCUS-97112)
matching decompilation (sibling repo `../Extermination/`). The decomp recovers
the game's logic as C; this repo turns it into a real native executable for
**macOS, Windows, and Linux** — no PS2 emulation, no ISO. The PS2 GPU path
(VU1 microcode + GS rasterizer) is **reimplemented** on modern APIs.

**Current goal (user, 2026-09-22):** the FIRST LEVEL — New Game → the AREA11
opening → everything in AREA11 → its exit — must behave and look exactly like the
original. Work outside that scope waits until the user moves the goal.

## Two profiles (user, 2026-09-23)

The finished port ships an **Original** profile and an **Enhanced** profile,
built from one codebase (full rules: `docs/PORT_PROFILES.md`).
- **Original** is the default and the only thing fidelity work measures. It
  uses the exact GS framebuffer (512x448 in the first level), shown at 4:3 with
  no smoothing and no CRT simulation. Its colours are GS-exact, and its logic
  and controls are the original's.
- **Enhanced** is the user's improvements: resolution, filtering, AA,
  widescreen, frame rate, QoL, better controls (the README's "Future
  Enhancements") and restored cut content (the decomp's `docs/CURIOSITIES.md`,
  disc-sourced only). Every item is a switch whose Original value reproduces the
  Original profile. Gameplay changes are patches applied on top of the verified
  translation, never rewrites of it.
- Enhancement work waits until fidelity work is done, unless the user asks
  for a specific item.

## Hard rules (non-negotiable)

- **Clean-room, zero third-party dependencies.** Windowing and rendering are
  written by us, directly on each OS's native APIs (macOS: Cocoa + Metal;
  Windows: Win32 + Direct3D 12; Linux: X11/Wayland + Vulkan). **Do NOT add SDL,
  GLFW, bgfx, sokol, or any external library**, and do not copy code from
  emulators (PCSX2, Play!) — their licenses (GPL) and IP would entangle the
  project. Every line here is our own original code.
- **Never commit, upload, or redistribute disc-derived material** — no ISO, boot
  ELF, extracted assets, original code/data, disassembly. Assets are generated
  locally from the user's own disc into ignored `assets/` / `data/` / `build/`.
  Scan every staged diff for data blobs before committing.
- **No disassembly in code comments or docs.** A translation cites original
  addresses and says what the code does (in words or C-like expressions over
  named fields); it never reproduces the instruction stream — no
  "mnemonic operands" comment per line, no pasted listing blocks. Naming a
  single instruction in running prose to explain a rule is fine. Run
  `python3 tools/check_no_disassembly.py --staged` in the leak scan before
  every commit.
- **Stay isolated from the user's other code/repos** (esp. the separate
  commercial game). The only permitted cross-repo relationship is the sibling
  `Extermination/` decomp.
- The north-star is a scrupulously clean, original codebase strong enough to
  someday pitch Sony a remake. Keep it that way.
- Never edit or commit the user's own uncommitted files (currently `README.md`
  edits and `tests/run_suite.sh`).

## Fidelity rules

- **A label is not evidence.** Comments/docs saying DECODED/VERIFIED/original are
  claims. Behavior counts as original only when an original-instruction oracle
  (below) or an original capture exercises it. Earlier agents added fabricated
  features; read the original function (decomp C, and the .s for NEARMISS) before
  trusting or writing anything.
- Never invent behavior, constants, UI, or content. Missing original workers
  **fault** (fail-stop) rather than silently succeed or substitute a stand-in.
- When removing a fabrication whose original replacement is not ready, leave the
  thing inert/static (not a new invention) and name the original function + the
  roadmap item that will replace it.
- Unfinished original modules stay built-and-tested but unwired (or gated off)
  until all their original workers are bound; never ship a live path that
  degrades play relative to what it replaces.

## Where the state lives (read at session start)

- `docs/FIRST_LEVEL_AUDIT.md` — adversarially verified audit of the live
  first-level path (H1..H22) and the roadmap WP-0..WP-18 with status.
- `docs/SCENE_COORDINATOR_DESIGN.md` — the live scene coordinator plan (S1..S13)
  and, in section 10, what was actually built plus lead decisions.
- `docs/ORIGINAL_FRAME_ORDER.md` — the original's per-frame call order and the
  49–52 AREA11 owners, measured in PCSX2.
- Per-subsystem docs in `docs/` (STARTUP, FIRST_CONTROL, AREA11_*, DOOR_ORIGINAL,
  ROGER_*, STATUS_HUB, PLAYER_*, SFX_*, ACTOR_LIGHTING, MESSAGE_SERVICE, ...).
- The decomp's `docs/HANDOFF.md` is the cross-repo entry point.
Keep these current in the same session as the work.

## Architecture

```
src/
  em_platform.h       windowing + input contract (+ em_headless())
  em_gfx.h            graphics contract (skinned draws, overlays, fog, rigs)
  em_input.h/.c       DualShock 2 pad model; 001B5940 pad block translation
  em_model.h/.c       EMDL asset loading (our own interchange format)
  main.c              bring-up: platform/gfx/audio init -> em_frame_run
  game/
    em_frame.*        main-loop steps A..W (original 0x1AAE40 order)
    em_task.*         3-slot frame-task table
    em_scene_*        live scene coordinator cores: state (single storage for
                      original bytes), 001AE7E0 classifier, 0x1AE040 frame
                      machine + 001AE5E0/001AE6B0 variants, task chain,
                      bindings (worker table, fail-stop)
    em_actor_pool.*, em_actor_roster.*   original actor pool / AREA11 roster
    em_area11_*       AREA11 bindings, interaction host (panel/battery/elevator)
    em_*_original.*   standalone translations of original owners (fan, truck,
                      crate, drum, door, shadow, ...) bound as pool nodes
    em_game.c + em_player_frame.c + em_render_frame.c   legacy glue being
                      replaced step by step by the coordinator
  platform/{mac,win,linux}/   native windowing per OS
  gfx/{metal,d3d12,vulkan}/   native renderer per API
```
Layering: game code talks only to the `em_*` contracts; the platform layer never
touches a GPU API. Each native stage names the original address it stands in for.

## Verification

- **Original-instruction reference tests** (`tools/test_*_reference.py`, run via
  `make test-*`): a Python MIPS oracle executes the ORIGINAL ELF/overlay code over
  captured original RAM (`../Extermination/build/startup-reference/`) and compares
  every written field and worker call with the native module. This is the primary
  evidence.
- **Sanitizer fixtures** (`tests/*.c`, ASan/UBSan) with real exported assets.
- **Original runtime**: `../Extermination/tools/pcsx2_session.py` — exact
  one-frame stepping, pad input, memory reads, snapshots with screenshots.
- **Live run**: `EM_STARTUP_TEST=newgame-control build/extermination` (New Game
  → opening → first control; 30 input ticks must travel 9.599989) and the level
  smoke test. Frame order: `tools/compare_frame_order.py` against the original
  traces.
- Parallel agents build privately:
  `make -n -B all | grep -- '-o build/extermination' | tail -1 | sed 's#-o build/extermination#-o build/<lane>/extermination#'`
  (zero warnings). Before committing, build the staged index in isolation
  (`git checkout-index -a --prefix=<scratch>/ && make all` there).

## Tests

**Scope.** Tests cover the first level and the path into it (startup, title,
New Game, AREA11 opening, first control, everything in AREA11, its exit). Do not
add tests for other areas, the old office/drawbridge fixtures, or legacy
mechanics. A test that encodes non-original behavior is deleted, not adjusted
to keep passing.

**Speed.** A default `make test-*` run should finish in about 10 seconds or
less. Use representative cases plus the boundary cases in the default run;
put exhaustive sweeps behind `EM_TEST_FULL=1` and run them only when the module
or its original function changes. Where the underlying decomp function is
byte-matched and the translation has passed a full sweep, a small verified
sample per run is enough. Python reference tests use `tools/reference_mode.py`
(`select`/`pick` for the quick sample, `banner` for the "mode quick: N of M"
line, `parallel_map` to spread independent oracle cases over forked workers;
`EM_TEST_JOBS=1` forces serial).

**No windows.** Automated runs must never put a window in front of the user.
The app is headless automatically whenever a test/capture/trace variable is set
(`EM_*TEST`, `EM_CAPTURE*`, `EM_FRAME_TRACE`); use `EM_HEADLESS=0` only when the
user wants to watch. Launch PCSX2 only through `pcsx2_session.py` (hidden by
default); never leave an emulator running.

**Retiring tests.** Delete a test (its file, make target, and doc mentions) when:
1. the user asks to remove it;
2. it covers content outside the first level, or a mechanism no longer on the
   live path;
3. it encodes behavior shown to be non-original;
4. a stricter test of the same behavior supersedes it (e.g. an original-trace
   comparison or the level smoke covering it end to end);
5. its module is certified — the translation passed a full original-instruction
   sweep, the underlying decomp function is byte-matched (or the translation
   has matched the original through three consecutive sessions without
   change), and it is wired and exercised by the level smoke: then shrink the
   default run to a smoke sample and keep the exhaustive sweep only behind
   `EM_TEST_FULL=1`; drop it entirely once the level smoke plus capture
   comparisons cover the behavior.
Never retire a test to make a change pass. Record every retirement in the commit
message and the relevant doc.

## Temporary artifacts and cleanup

- **Where.** Screenshots, captures, traces, emulator snapshots and scratch
  builds go under ignored `build/<task>/` (or the session scratchpad) — never the
  repo root, `docs/`, or `src/`. Name folders by task so they can be removed as a
  unit.
- **Never delete:** `../Extermination/build/startup-reference/**` (original
  captures and save states = oracle inputs), the user's save-state slots 01–15,
  `assets/` (regenerable but slow), and receipts a committed doc cites (until
  the doc stops citing them).
- **Checkpoints.** (1) Before a work package's final commit: delete its
  intermediate screenshots/BMP/PNG, emulator snapshots (state.p2s, eeMemory,
  gs dumps) and private lane builds that no doc or test references. (2) At
  session end (or before handing off): remove scratch trees (exported index
  trees, baseline checkouts such as `../port-baseline-*`, `build/<lane>/`
  binaries), stray files in repo roots, and any running emulator. (3) Whenever
  `build/` exceeds ~5 GB, or artifacts are older than 7 days and unreferenced.
- **Screenshot comparisons** keep only the latest before/after pair per topic
  (`build/captures/<topic>/`), overwritten rather than accumulated.
- Clean only generated artifacts; never user files.

## Build

- macOS / Linux: `make` then `make run`. No external tools beyond the platform
  compiler and system frameworks.
- Windows: MSVC/clang-cl project (added when the D3D12 backend lands).
- Assets: see `docs/STARTUP.md` and each subsystem doc for the exporters (they
  read the user's own disc/ELF and write ignored assets).
