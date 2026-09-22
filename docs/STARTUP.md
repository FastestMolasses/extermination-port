# Original startup and first-level opening

The native entry point now runs the original startup sequence: warning, Sony,
Deep Space, E900 movie, and the three-choice title. New Game requests E900 again
before loading AREA11/sub0/entry0. `EM_SKIP_STARTUP=1` selects the older gameplay
fixture. It is a test override, not the normal opening.

Source evidence is maintained in the sibling decomp's local docs/STARTUP.md.
The original PS2 executable and local runtime are authoritative. The old and
current ports both contain approximations and incorrect translations.

## Local assets

Generate assets from the user's own extracted disc. Outputs remain ignored.
From the port directory:

```sh
python3 ../Extermination/tools/export_startup.py --help
python3 tools/export_movie.py --iso /path/to/owned.iso --out assets/startup/intro.mov
python3 tools/export_startup_audio.py --decomp-root ../Extermination
python3 tools/export_area11_flow.py
python3 tools/export_area11_opening.py
python3 tools/export_opening_camera.py \
  --source ../Extermination/extract/chunk15/f12_id44.bin \
  --bank-offset 0xD0800 --out assets/scene_snow/opening_camera.emcc
python3 tools/export_opening_media.py --decomp-root ../Extermination \
  --iso /path/to/owned.iso --out assets/scene_snow
```

The startup exporter replays the GS uploads, including their on-disc palettes,
and composes the actual sprites. The movie exporter preserves MPEG-2 access
units, presentation timestamps, and every PCM sample; macOS plays the result
through its system media frameworks. The port adds no third-party runtime.

The first-level animation/camera bank is embedded in chunk15/f12_id44 at 0xD0800,
not at the start of the unrelated file named f06_id98. Its three directory
entries contain the camera, Dennis animation, and Roger animation. Runtime
resource slot 0x98 points at this bank. The camera has 646 source frames and 647
samples including lookahead, and advances 0.5 per ordinary tick.

Opening body/equipment export currently also requires a captured original GS
state for its textures. See the sibling decomp docs/OPENING_ACTORS.md for the
exact command and byte comparisons. A disc-only texture pipeline remains work.

## Behavior and verification

- The task table preserves the original trailing bytes when replacing a task.
- Fullscreen fades and letterbox fades are separate original state machines.
- During movies the ordinary task loop, fade updates, frame count and parity
  suspend. Movie input still runs. The skip gate uses completed picture index11.
- Startup screen holds use the original post-decrement counter (301 draw ticks).
  Title navigation clamps, accepts START/CROSS, and respects the fade gate.
- Native scene asset lookup follows the active scene directory, including New
  Game and room transitions; it does not depend on the EM_SCENE symlink fixture.
- Collision queries distinguish camera, movement, and general segment filters.
  Their attribute predicates were checked against original ELF branch execution.
- The opening camera holds roll, respects negative-FOV cut markers, and rounds
  its finite arithmetic toward zero at each original operation. At one captured
  opening frame its six eye/target floats match original runtime bytes exactly.
  Cinematic mode 3 uses the authored eye without the gameplay forward push.
  Its native view matrix agrees with the captured original within 0.000031.
- Body animation uses the original stateful half-tick cursors, cut flags and
  unnormalized quaternion blend. Original rifle and knife child meshes attach
  to Dennis bones 4 and 14; Roger equipment follows his bone 1.
- The opening runs its exported script and original dialogue/fade tracks. Both
  normal and skip paths restore the final script position/camera and story
  flags once. Missing required resources fail explicitly.
- Random arithmetic matches the original SDK leaf, including 32-bit stored
  state and 31-bit output. Whole-game RNG call ordering is still unaudited.
- The original head meshes carry seven morph channels. Blink/mouth state and
  vertex blending pass original instruction comparisons; exact pooled initial
  weights and separate head-light selection remain work.
- Subtitle text draws after the letterbox subtraction and before fullscreen
  fades. The aligned source134.5 GPU capture now shows the original “Look up.”
- The canopy uses the original placement record. Six pickup-light children use
  original model73, owner transforms, random color arithmetic and additive draw.

Tests are asset-free unless explicitly described as reference comparisons:

```sh
make test-input test-task test-fade test-startup test-movie-export test-startup-audio
make test-area11-flow test-collision test-script test-area11-opening
make test-cinematic-camera test-opening-actor test-opening-media test-bgm-ticks
make test-opening-runtime
python3 tests/opening_media_export_test.py
python3 tools/test_random_reference.py
python3 tools/test_collision_reference.py --help
python3 tools/test_area_title_reference.py --help
```

Local runtime evidence lives in ignored build/startup_natural, startup_skip,
startup_newgame, opening_visual, opening_control, and reference_runtime.md.
A complete E900 playback and interactive menu succeeded. New Game tests skip
both E900 requests after two seconds, then run the entire in-engine opening.
The full GPU run completed the opening at gameplay frame 1304 and captured
normal gameplay at frame 1400. This is not a claim of complete visual fidelity.

The actual-input regression passed: held forward input produced zero movement
through 1,303 opening ticks. After correcting a 90-degree stick-heading error,
the original motor and final script camera now give 9.599989 units over30 input
ticks, versus9.599849 in the original. The horizontal endpoint differs by
0.000168. The heading helper passes2,360 original instruction cases and the
motor11,482 cases. Run release now plays original stop clip5 and restores idle;
other locomotion branches and pose blending remain under comparison. See
`FIRST_CONTROL.md` for the trace, original addresses and remaining limits.
Run from the port directory:

```sh
EM_STARTUP_TEST=newgame-control EM_CAPTURE=build/after_opening.bmp build/extermination
```

The frame/controller/media/actor integration passes AddressSanitizer and
UndefinedBehaviorSanitizer with real exported assets. Additional oracles execute
original ELF instructions for collision, script sequencing, and random state.
Host actor matrix differences from EE/VU arithmetic are measured up to 0.000092.
The native prefill handshake is faster than the original disc/IOP wait; align
camera/actor cursors when comparing screenshots, rather than scene frame alone.

## Remaining fidelity work

The first-level checkpoint remains under comparison. Exact face initial state,
whole-game random ordering, snowfall, other scenery behavior, lighting/materials
and ordinary player animation need further work. The old direct-to-control opening and fabricated
battery pickup were incorrect and have been removed from the normal path.
Existing later traversal cutscenes and Game Over presentation still contain
approximations and must not be treated as original behavior.

The previous “grate” was the original static switch panel. Its invented blocker
and slide have been removed; the elevator body and two indicator meshes now use
the original resource bindings. Roger's body was also mislabeled as a battery
console, so that false pickup/examine placement has been removed. Original panel
interaction scripts are still being connected. See docs/OPENING_SCENERY.md.

Weather controller C is present as an isolated verified module:12,000 original
instruction comparisons check state and random-call ordering. It is not yet
connected to the frame loop, pending recovery of the original snow renderer.

The title audio sequencer preserves the original events, waits, pitches and
sample data, but its current dry mixer does not reproduce SPU2 ADSR, Gaussian
interpolation, reverb or hardware voice allocation. Native storage currently
maps the successful card-check path to a local directory; full save/load and
space handling remain incomplete. Load Game, Option and attract services remain
pending rather than manufacturing an outcome. Windows/Linux movie backends and
other existing gameplay approximations also remain unfinished.
