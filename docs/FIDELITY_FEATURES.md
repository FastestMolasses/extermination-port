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
AREA11 opening, and the level through Roger's encounter and its exit into
AREA01's first frame).

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

**First-level census: 99.2% of the original game-logic instructions on the route and its recorded branches run live as verified translations**

Every original function the PS2 game runs on the first level, from New Game
through Roger and the level exit into AREA01's arrival, plus the recorded
side lanes (aiming, damage, the branches, the options), was recorded, and
the port was checked for each one. Measured by instructions, 99.2% of that
game logic runs in the port as a verified translation (98.9% of the main
route's own).

- How: the decomp's `tools/route_census.py` set a one-shot breakpoint on
  each of 2,957 boot functions and 34 AREA11 overlay functions and played the
  original route in hidden PCSX2 (4 startup labels plus route beats 00..14):
  1,184 functions executed (111,764 instructions). Each was classified by
  reading its evidence. An instrumented port build recorded live
  caller/callee edges (census 1.22, measured again in 1.33, 1.44 and,
  over the route through the AREA01 arrival and every side run, 1.67).
- Evidence: `FIRST_LEVEL_CENSUS.md` 1.1, 1.22, 1.33, 1.44, 1.67, 2.1-2.3. Recount
  1.44 (chain C8b ROUTE, 2026-09-28, port HEAD 6594182): of 756 non-boundary
  functions, live 703 (85,512 of 88,729 instructions = 96.4%; 93.0% by
  function count); verified but unbound 50; unverified 3 (0015CF90,
  001B1190, 001FC280); stand-in 0; missing 0. 428 boundary functions
  (SDK/libc/IOP/driver/GS/VU1, 23,035 instructions) are replaced by native
  platform services and the native renderer. The edge recorder confirmed
  all 703 live rows (677 by a live candidate, 26 inline in a live function
  of their module, read by hand) over the full route, the three side runs,
  newgame-control and the area change, and found no non-live row whose own
  translation runs live. Update 1.46 (chain step H7, 2026-09-29, recounted
  from the rows): 001FB100, 001FC6E0, 001FB370, 001FB3E0 and 001FB910 bound
  live (the area load's sound-bank upload and step H's whole 001FB100):
  live 708 (86,021 of 88,729 instructions = 96.9%; 93.7% by function
  count), verified but unbound 45, unverified 3, boundary 428. Update 1.53
  (the lighting step, 2026-10-02, recounted from the rows): the lighting
  stand-ins replaced by their originals: live 720 (86,702 = 97.7%),
  verified but unbound 33. Update 1.55 (chain step A11FIX, 2026-10-02,
  recounted from the rows): the flag-0x30 manager 0x823CE0, the flame's
  loop service 001FC3C0 and the contact pass 001A8660 bound live (the
  AREA11 overlay translations checked against the decomp's byte-identical
  C): live 723 (87,008 of 88,729 instructions = 98.1%; 95.6% by function
  count), verified but unbound 30, unverified 3, boundary 428. Update 1.56
  (chain step AIMCAP, 2026-10-02, recounted from the rows after the merge):
  the AIM capture lane's 114 further functions (aiming, firing, melee,
  reloads; 20,583 instructions) are rows, all live, measured over the
  eleven AIM side runs: with them live 837 of 870 (107,591 of 109,312
  instructions = 98.4%), the route's own 756 unchanged. Update 1.58
  (chain step EXIT, 2026-10-02, recounted from the rows after the merge
  onto 1.56..1.57): route beat 15, the level exit through the AREA01
  arrival, joins the count (11 functions, 001FC280 live): the route's own
  738 of 766 live (88,066 of 89,400 instructions = 98.5%), with the AIM
  beats live 852 of 880 (108,649 of 109,983 = 98.8%), verified but
  unbound 26, unverified 2, boundary 429; AREA01's frames after the
  arrival are level 2 (census 3.26). Update 1.59 (chain step DAMAGE,
  2026-10-02, recounted from the rows after the merge onto 1.56..1.58):
  the functions the DAMAGE recordings ran join the count (45 more; five of
  their 50 were AIM or beat-15 rows already), liveness measured over the
  three DAMAGE side runs: live 884 of 922 (112,833 of 115,339 instructions
  = 97.8%), verified but unbound 27, unverified 2, missing 9 (the title's
  load screen, which no port path reaches yet), boundary 432; the route's
  own 739 of 766 live (88,122 of 89,400 = 98.6%). Update 1.61 (chain step
  CAMERAS, recounted from the rows after its merge onto 1.57..1.60,
  2026-10-08): the timelines' start clock 0021BAB0 live: live 918 of 956
  (118,433 of 120,964 instructions = 97.9%), verified but unbound 26; the
  route's own through beat 15 740 of 766 live (88,125 of 89,400 = 98.6%).
  Update 1.64 (chain step GLUE, 2026-10-08, recounted from the rows): the
  pad read 001B57E0 / 001B5F40, the state-0 re-arm 001AFCA0 with 001AF690,
  the New Game's 001AB790, and 0015CF90 / 001B1190 with oracles executing
  them: live 925 of 956 (118,723 of 120,964 instructions = 98.1%),
  verified but unbound 21, unverified 1 (001CB480); the route's own
  through beat 15 747 of 766 live (88,415 of 89,400 = 98.9%). Update 1.65
  (chain step OPTIONS, 2026-10-08, recounted from the rows, liveness
  measured over its ten side runs): the options screen, its row screens and
  the memory-card screen (the OPTIONS recordings' 14 further functions, and
  the title's load screen's 11 DAMAGE rows to live): live 948 of 968
  (122,905 of 123,918 instructions = 99.2%), verified but unbound 19,
  unverified 1, missing 0; the route's own unchanged. Recount 1.67 (chain
  step ROUTE, 2026-10-08, port HEAD 2e5fa30): the edge recorder re-measured
  every row over the whole route through the AREA01 arrival and every side
  run (44 instrumented runs, each PASS in process): 946 of the 948 live
  rows confirmed by a run, the other two (001755B0, 0021E9C0) bound with
  oracles but entered by no port run; no status change (live 948 of 968,
  122,905 of 123,918 instructions = 99.2%; the route's own through beat 15
  747 of 766, 88,415 of 89,400 = 98.9%). Update 1.68 (the presentation
  step, 2026-10-09, recounted from the rows): main-loop steps R and U
  bound, 001AB4E0 live and the SDK's 001002E0 / 00100550 moved from the
  boundary group to rows, live: live 951 of 970 (123,152 of 124,121
  instructions = 99.2%), verified but unbound 18; the route's own through
  beat 15 750 of 768 (88,662 of 89,603 = 98.9%). Update 1.69 (the coverage
  step, 2026-10-09): the last two live rows no run entered, 001755B0 and
  0021E9C0, are entered by the DAMAGE side runs dmg_pit_fall (its fall
  start) and dmg_fan (the fan's hit, row for row with dmg_08), shown by a
  counted hook in a scratch build: every live row (951) is now entered by a
  port run; no status change.
- Status: **PARTIAL**. First level only, and only the played route
  through the level exit's AREA01 arrival plus the AIM side beats from
  route 08's end and (since chain step DAMAGE, census 1.59) the functions
  the DAMAGE, BRANCH and OPTIONS recordings ran. Not covered: unplayed
  branches the capture lanes did not record (the camera inputs, a landing
  that asks 001755B0, a chosen memory-card slot), the 18 verified-unbound
  rows and the one unverified row (census 1.69 lists them), and boot before
  the title.
  Boundary functions are native replacements, not translations; the sound
  library's boundary (the SPU2 output) is the largest uncompared one.
  "Stand-in 0" counts census rows only; census 2.3 "What still stands in"
  lists non-row stand-ins still on the route. The remaining gaps,
  prioritized, are `FIRST_LEVEL_AUDIT.md` section 1b.

**The first-level route is replayed headless and checked phase by phase against PCSX2 recordings**

An automated run plays the first level from New Game through Roger and the
level exit into AREA01's arrival with no window open. In each scripted, climbing and cutscene window it checks positions,
script progress, camera, messages and cutscene flags against the PS2 game
as recorded in PCSX2 on the same route.

- How: `EM_STARTUP_TEST=newgame-level` plus `tools/test_level_smoke.py`. The
  runner steers closed-loop toward the capture's targets (open-loop pad
  replay does not reproduce the recordings, decomp `CAPTURES_C7.md`
  "Reproducibility"), then replays the tick log against the route captures.
  `--require-through` fails the run if any phase is not live, driven or not
  reached.
- Evidence: `LEVEL_SMOKE.md` "Route coverage": beats 01..15 live on the main
  line (20 phases, through the level exit and the AREA01 arrival idle),
  side beats 00 and 09, plus `fence_door_side1` against the C7 DOOR1
  capture, the designed `status_pages` run replayed through the original
  instructions, and the AIM (11), DAMAGE (4), BRANCH (10), OPTIONS (9) and
  AUDIO (3) side runs against their capture lanes. Chain step ROUTE
  (2026-10-08, port HEAD 2e5fa30): `make test-level-smoke-full` PASS
  through a01_arrival (NOT-LIVE: none) with every side run, `make
  test-level-smoke-ps2-drive` PASS through roger, and every other make
  test-* target PASS. (Chain C8b ROUTE, 2026-09-28: the same through roger
  with the three side runs of the time.)
- Status: **PARTIAL**. First level only. The walks between scripted windows
  are navigation and are not compared. Pixels and sounds are not compared at
  all. `LEVEL_SMOKE.md` "What the full route does not yet compare" lists the
  relaxed checks (line 0x7F's teardown 2 rows earlier than the drive mode
  alone explains; Roger's flags before his clip init; slide/ladder landings
  within one row; the player's and Roger's units at the snapshots the
  navigation does not reach exactly; the opening's rand() values after its
  actors' spawn, which the stream request's wait moves (host speed, the
  policy); the fans' phase at the snapshot ticks (only their cycle is
  compared); the rand()-seeded sprites; the pixels of 001DDE10's
  depth-of-field pass; the load veil's length). At host speed (the policy) the status page's module
  load takes the loader's 10 steps where the recording's disc took 24, and
  the rows after it are compared at that shift. Sounds are compared at the
  command level (the EE sound state), not as audio output. AREA01 after its
  arrival is level 2 (its own phases, not part of this claim).

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
  `LEVEL_SMOKE.md` "Frame order": over a newgame-control trace idle04 (native
  index 1393), walk04 (1455, a walking window), st03 (1384), cut02 (89) and
  cut15 PASS event for event with an empty allow list (chain step H7,
  2026-09-29: the windows moved 63 ticks with the New Game's loads; with the
  PS2 disc-drive timing switch on: 1665, 1727, 1656, 361 and cut15, also
  PASS; 1676 / 1738 / 1667 / 372 and cut15 since 2026-10-09, the switch's
  17-field seek after a module-loader read, all PASS). Decomp `CAPTURES_C7.md` 1:
  every main-loop frame in the four stream stretches is exactly one field.
- Status: **PARTIAL**. `tools/frame_order_allow.json` holds no entry since
  chain C8b ROUTE: the last one (the walking footstep's effect node in
  walk04) was retired because the port's walk spawns that node through the
  original 00187EE0 -> 001EFD90 (census L26), and walk04 now passes in a
  walking window without it. Only the traced situations are covered (2-3
  frames each, plus the smoke's windows): cut07 (selector 3) and st14
  (Roger) find no matching window in a newgame-control trace and were not
  re-compared; the movie-gated steps M/N/O never ran in any trace. Tests
  run uncapped (`EM_UNCAPPED=1`), so host pacing is
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
  first 227 calls (the security gun's AE+1 draw, Roger's owner's face at
  AE+2 and the player's face from AE+5 included), up to its actors' spawn,
  which the stream request's wait moves (at host speed, the policy, 21
  frames earlier), so the values after it differ. In PCSX2 the original's own call order
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
  live route (census 2.3: the panel/terminal/item takeovers). Some duplicate translations remain
  (`EE_FLOAT_MODEL.md` 5c). Since chain C8b no status page the first level
  reaches stops the game: DATABASE, SPR4, MAP and the non-battery takes run
  their original pages (`STATUS_PAGES.md` section 7); the fail-stops left
  there guard branches section 7 proves unreachable in AREA11.

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
  (zero-latency and 7-field-seek drives fail at cage_roof; since the drive
  switch, a host-speed read one field slower fails at cage_roof and at the
  opening's end).
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
  advertise Windows or Linux yet. An iPhone build for the user's own device,
  played with a game controller (docs/IOS.md, 2026-10-04), runs the same
  game code on UIKit/Metal/RemoteIO: `newgame-control` reproduces the macOS
  displacement 9.599849 on an iPhone Air. Its movies are an HEVC re-encode
  made at build time (iOS has no MPEG-2 decoder; PSNR 46.8 dB against the
  macOS decode, timestamps and sound unchanged). The port contains C translations of
  original game and SDK routines by design; "no Sony SDK" means no SDK
  binaries are linked. The decomp repository still commits CodeWarrior asm
  function bodies (user decision 2026-09-23), so the "no original code" claim
  holds for the port repository only. No first-level asset reads a PCSX2
  capture (next entry).

**Every first-level asset from your own disc, rebuilt with the original loaders' rules**

The assets the first level reads are exported from the user's disc image,
its extract and the boot ELF alone: no PCSX2 capture, GS dump or RAM dump is
needed. The textures come
from the first level's GS memory rebuilt from the disc with the original's
own upload sequence (module 0x1B's library sheet, New Game's re-upload, the
AREA11 load and the player's texture packet; page modules for the status
pages), and the resource table D_0028A490 from the loaders' own relocation
rules (boot, title, New Game, area load). The two assets that hold values
the game writes at run time (the use-owners' first-tick fields in
`interaction.emis`, the level background's GS draw state in
`background.embg`) come from AREA11's first world frame, which the original
code itself builds from the disc: the boot's render builder, New Game, the
area load, the frame machine's bring-up and the first frame through its
actor walk, executed instruction by instruction over the disc memory.

- How: `tools/export_disc_textures.py` (textures, the font and the two
  first-frame assets) and the disc-first exporters of STARTUP.md;
  `export_disc_textures_gs.py` (`FirstLevel`, `ResourceTable`,
  `first_level_memory`); `export_disc_state.py` (the first world frame). Captures remain
  optional developer cross-checks (`--capture`, `--verify-ram`).
- Evidence: `make test-disc-textures-reference` (the original loaders
  00200830 / 00200890 / 00200970 / 001FF1E0 / 001FF830 / 001FFCD0 executed
  under the oracle; 5,504 GS blocks equal 19 captures; 15 texture files equal
  the pinned capture-derived files) and `make test-disc-assets-reference`
  (D_0028A490[0 .. 0xAF) from the disc equals all 18 AREA11 captures word for
  word; 33 more files byte-identical to the capture-derived ones, the
  Roger banks' table and regions equal, the hub's EMHS equal outside the
  arc words 00208AD0 rewrites; the player model's bake from the rebuilt
  memory identical to the GS-dump bake; since 2026-10-02 also
  `interaction.emis` and `background.embg` from the disc's first frame equal
  to the capture-derived files, every owner field stored by the owner's own
  first tick, and the whole `player.emdl` / `player_channels.empc`
  reproduced by steps 6..8). A copy of the tree with no capture visible
  re-ran 57 export steps, 113 of the 116 files the level smoke opens came
  out byte-identical (the other three differ by construction:
  DISC_TEXTURES.md 9.3), and the full level smoke passed on it; on
  2026-10-02 the same kind of copy rebuilt the first-frame assets, the
  player model and the light cone byte-identical, and the smoke passed again
  (9.6). DISC_TEXTURES.md sections 6 and 9.
- Status: **VERIFIED** for the first level's assets (byte-identical to the
  capture-derived files, smoke passed on a capture-free rebuild). Limits:
  only `--iso` is exercised, not `--disc DIR`; the first-frame model holds
  the flame's first tick (VU0 VMINI is outside the measured VU model) and
  is the first world frame, not the first-control frame (the compared bytes
  are equal; DISC_TEXTURES.md 9.4).

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
  assumed to follow the VU0 rules measured in PCSX2. Since census L24 the
  fan pair, the security gun and its cable are on this path (their units
  compared at the snapshots; the fans over the port's own spin angle through
  the original 001C6380). Since chain C8b's FACE step Roger and his
  equipment are on this path too (next entry), and since chain C8b's
  OPENING step the opening's actors (the opening cinematic entry). Since
  the units step (2026-10-02) the indicator children are too (the entry
  below). Object textures (465 TEX0) are decoded from
  the GS memory rebuilt from the user's disc (DISC_TEXTURES.md; equal to
  every route capture's decode). Metal only.

**The indicator lights on their original draw**

The green and red lights over the items, the power panel and the elevator
terminal, and the security gun's lamp, are drawn by the game's own draw
routine for them, lit by its own lighting mode, at the place in the frame's
list the original puts them.

- How: each indicator child's draw method 001CACB0 runs 001CABA0 over the
  child's own record (em_indicator_bind_live_draw): the channel-3 unit with
  lighting mode 1 (001D89D0 / 001D8C30 over the child's colour, which
  001F54E0 pulses with rand()), the model's blocks through 001D3990 /
  001D3D90 and the depth-sorted CALL 001CAAC0 inserts into the chain page;
  the chain page draws the unit there as a class-2 object unit (additive,
  no Z write). The security gun's lamp draws the same way; its colour is
  dark in the first level, so it adds almost nothing, as on the PS2.
- Evidence: `OWNER_DRAW.md` 11; test-owner-draw-reference part E (every
  store of the original 001CACB0 -> 001CABA0 over the captured children);
  test-object-unit-reference part K (the class-2 units through the original
  microcode); the level smoke's check_indicator_units: one call per child
  per tick, and at every aligned route snapshot the original 001CACB0 over
  the snapshot with the port's colour words, point lights and view writes
  the port's unit (colour, lighting rows; the position rows too in the
  camera-exact beats 10 and 14), the gun's lamp among them.
- Status: **VERIFIED** for AREA11 relative to the original code. The
  colour pulse follows the port's rand() stream (the values cannot match a
  recording); pixels are Metal's (no fb2 frame compares them).

**The area title card**

The area's title line appears after the fence door's room move, and at the
area entry its 300-tick timer runs through the opening, silenced while the
opening's scratchpad mode byte is 1..3; its text, position and timing come
from the game's own data and code.

- How: the area-title node 001C5930 runs its translation over its own
  record (em_area_title): the title string D_002671C0[D_00289B40[area] +
  sub] for 300 ticks, quiet while the scratchpad mode byte is 1..3 (the
  opening), then the infection band line D_0026726C[001C5860()] when the
  band is not 0; both through 001CC170 / 001CC1E0, the message service's
  glyph runs. The tables, strings and style come from the user's own ELF
  (`tools/export_area_title.py`).
- Evidence: `STATUS_UI_LEFTOVERS.md` 2.6 (test-status-ui-leftovers-reference:
  001C5930 against the original instructions); the level smoke's
  check_area_title (the node's state, timers, string index and band equal
  every aligned route snapshot's; at route 09's last row the room move's
  fresh node holds the capture's 240 ticks left, at another pool record
  than the original's; in every tick the title timer steps, a line is
  drawn exactly when the mode byte 0x70003B8D at the tick's end is not
  1..3); route 09's frame shows the card where `09_fence_door/original.png`
  does (by eye).
- Status: **VERIFIED** for AREA11's route relative to the original code:
  the node's fields at the aligned snapshots 08..14 and route 09's fresh
  node's fields. **UNVERIFIED**: the single title line at the area entry.
  In the port the node's first state-1 call comes one tick before the
  opening stores the mode byte 2, so that call draws one line; no capture
  shows that frame and no frame-order window covers it, so it is the
  port's order, inferred, not compared. Glyph pixels
  are the port's font atlas (the text entry above).

**Roger's face and the player's face in the cutscenes, morphed by the original face program**

Roger's talking face, and Dennis's face while a scripted conversation holds
it, are the original face units: the blink, expression and mouth weights of
the original face routine, uploaded with the face's lighting by the original
attachment draw and morphed by the original VU1 face program. Roger's body
and the equipment on him are the original object units.

- How: the attachment draw 001CB3C0 (with 001C7900, 001CB2C0, 001D3E40 and
  the face lighting 001D88B0) runs live inside 001CAA00 for Roger and, while
  a script holds the player's face slot (001CA700 .. 001CA770 on the
  player's record, over the shared bone-slot stack), for the player; the
  face unit is drawn by the translated face-morph program
  (`em_vu1_face_morph`).
- Evidence: `FACE_ATTACH.md` 4 and 7. Offline: every drawn face unit of the
  captures (routes 00..14, the opening, the encounter, the fence-door
  capture) is rebuilt byte for byte by the translation; 40 units, 30,104
  triangles equal the original face program (`make
  test-face-attach-reference`, full mode); the face slot sequence equals the
  original over 400 frames (`make test-face-slot-reference`). Live: the level
  smoke's `check_face` (15,328 face units on the full route; 78 sampled calls
  re-executed by the original 001CAA00 + 001CB3C0 over the port's own
  inputs, every written byte equal) and `check_owner_units` (Roger's and his
  equipment's units at every aligned snapshot, in full at beat 14).
- Status: **PARTIAL**. The face weights follow the port's rand() draws,
  which sit where the original's do but carry other values than a recording
  (the random-events entry), so a recorded frame's face expression is not
  matched frame for frame. Since chain C8b's OPENING step the opening's
  Roger and player are on this path too. Pixels compared only by eye.
  Metal only.

**The opening cinematic on its original records**

The New Game opening plays as the original plays it: its script runs on
the original script interpreter, its Roger and the equipment on him are
records the script spawns, and Dennis plays the opening on his own record
and stage. Their faces tick where the original ticks them and their
skeletons equal the original's bit for bit.

- How: the controller 00823E80's state 1 (translated from its
  instructions) starts 0x828FC0 on the AREA11 script host; op14 (001BAC00)
  spawns the two 001BB0E0 records (001BAD40, 001BA580, anim_advance_time,
  001C68C0, their units with Roger's face and shadow, the class-8 node's
  001C5C90); the player's stage takes the player over (bank 0x98's clip 1
  through 00183090, the face through 001D0C70, the post-step's shadow and
  unit). `OPENING_ORIGINAL.md`.
- Evidence: `make test-area-script-reference` (the opening script and three
  skip variants in lockstep with the original 001BA1F0 and its handlers);
  the level smoke's `check_opening_actors` (at the opening capture's camera
  cursor: the opening body's 21, its node's 1 and the player's 21 node
  matrices equal the capture's bit for bit, the records at the capture's
  addresses) and `check_rand_order` (every call equal up to the actors'
  spawn); `compare_frame_order` cut02 and st03 event for event with the
  records in the walk.
- The opening's camera (since chain step CAMERAS, 2026-10-02): its
  timeline is the original 0022EC30 / 0022EEF0, the same code as Roger's
  encounter, with the opening's event table (the fade-out at its end) and
  its rumble cue. The level smoke's `check_opening_timeline` finds every
  compared tick of the timeline (1,289 of 1,293; 4 cursors were not
  sampled) equal to the original's camera block of the same cursor, byte
  for byte, with the screen fade's state; `make
  test-cinematic-playback-reference` runs the original instructions.
- Status: **PARTIAL**. At host speed (the policy) the stream request's wait
  is shorter, so the actors spawn, the screen fades in and the first line
  shows, and the opening ends, 21 frames earlier than in the recording.
  With the PS2 disc-drive timing switch they come on the recording's
  frames since 2026-10-09 (the switch's model seeks the measured 17 fields
  for the area music's read after the New Game's last module-loader read;
  `IOP_STREAM.md` "Drive model"): the opening's rand() calls equal the
  original's in caller, frame and state through first control. Pixels
  compared only by eye. Metal only.

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
- The room's point lights are the original's at run time (the lighting
  step, 2026-10-02, audit 1b item 4): the area entry's 001D7BB0 registers
  the room lists through 001F68B0 / 001F6E40 / 001F6640 / 001D7FA0 from the
  ELF's lists, where an offline export did before. Evidence: the original
  001D7BB0 against the port's chain for every list key and latch value,
  twice in a row (60 entries; `AREA11_POINT_LIGHT.md`); live, the pool's
  counters and active slots equal the first-control capture and every
  aligned route snapshot (`check_room_lights`).
- One translation each: 001D8270 (the fold gate), 001D8690 (the actor RGB)
  and 001D8C30 (the fixed lighting modes) have one translation, the one the
  live draws run; the status hub's and the MAP page's models light through
  the same bound 001D89D0 (mode 1, mode 2), and the renderer's own rig
  composer faults if a first-level draw reaches it (`ACTOR_LIGHTING.md`;
  `make test-actor-lighting-reference`: the renderer's lighting over the
  bound A and B gives the original colour matrix and the kernel slice's
  13,581 colour words).
- Status: **PARTIAL**. The routine is proven, and the live fold is compared
  over the port's own sway. The glow, the 0x0F00 key and the +0xB0 light
  point are exercised on synthetic states only; lighting mode 1 runs live
  (the status hub's models, the muzzle node); the hub's drawn pixels are not
  compared.

**Effects drawn from the game's own packets**

Breath and footstep puffs, pickup glints, glow markers, head sprites, the
standing-on-object shadow decal, the falling snow and the AREA11 flame are
built and drawn by the original code and the original VU1 sprite and snow
programs.

- How: the effect producers build their packets byte for byte into the
  render context's chain page (the flame through its owner's 001D04B0; the
  snow through the weather's 108 tile requests 001CFFE0 in channel 3, which
  the frame close's 001E0D70 CALLs into the page); `em_chain_page` walks it
  as the DMA/VIF/GIF would, runs the three page VU1 programs (translated,
  `em_vu1_page_programs.h`) and the blend presets of 001D0F20, and hands
  every GS primitive to Metal in GS order.
- Evidence: `CHAIN_PAGE.md` 8: 690 lane and 660 sprite program calls, 15,944
  kicks and 3,660,384 packet bytes equal the original microcode, and the
  weather's 108 snow MSCALs of every captured page (chain C8b FLAMESNOW);
  the latest page of every route capture 00..14 draws the same primitives as
  the model. The weather's channel-3 packets equal the executed original
  001E67C0 / 001CFAE0 / 001CFFE0 byte for byte (`SNOW_PARTICLES.md`). Level
  smoke `check_chain_page`: 13,013 pages drawn on the full route, each world
  page with the weather's list and the flame; 40 sampled pages re-walked with
  the original microcode draw exactly the port's primitives; in the
  camera-exact snapshots 10 and 14 the snow's and the flame's packets (their
  camera, fog and matrix rows) equal the recordings'. Commits d7ef847 and
  chain C8b FLAMESNOW.
- Status: **PARTIAL**. First level only. Sprite positions/colours follow the
  draws, whose values differ from a capture's at every snapshot
  (`RAND_ORDER.md`); only glow markers are compared with captures (count and
  geometry; their colour as the original 001F4D40 over each side's own
  draw, `check_marker_colour`), and the head sprites' phase follows 001E2560
  over the port's own draws (`check_head_sprites`). Rings are proven on synthetic batches only
  and are not drawn on the recorded route; do not advertise them. 001DDE10's
  depth-of-field pass is drawn only by the Original profile's GS model
  (the GS frame entry below). The snow's and the flame's
  sprites follow their owners' seeds and phases (rand(), the flame's age), so
  their positions are compared with the recordings only through their
  packets' camera and fog rows. Page and decal textures come from the
  disc (`export_disc_textures.py`, equal to every route capture's decode).
  Metal only.

**The area-load veil drawn by the game's own code and its own GS state**

While an area loads, the game's own veil (a glowing wave line bent by two
lens passes over black) is drawn by the original code, through the frame's
own GS list, at the GS's resolution.

- How: the veil draw 0021B1B0 and its phase step 0021B500 run at their
  original caller 0021B550 and write their packets at the render context's
  channel 0; the GS state they REF (the boot builder's GS blocks: draw
  environments, clears, presets) is translated; main-loop step V's own list
  is walked as the DMA sends it (em_chain_page's list mode) and drawn by
  the GS frame stage: GS-memory surfaces at 512 x 224 and 256 x 256, the
  frame copied into a texture and drawn back through the lens strips, with
  the GS's texture, alpha-test and blend rules.
- Evidence: `LOAD_VEIL_PARTICLES.md` 3-5. The live run's veil packets are
  byte-equal to the ORIGINAL 0021B1B0 executed at the same call, and the
  seed it leaves equals all 15 route captures' (level smoke
  `check_load_veil`); the GS blocks equal the ORIGINAL 001D0F20 executed
  with its SDK callees and all 18 captures (`make
  test-gs-blocks-reference`); the drawn frame equals a model of the GS
  pixel path for a visible veil (both copies on every pixel, the lens passes
  on 439,792 unambiguous pixels; `make test-load-veil-gpu`).
- How long it shows (chain step H7, 2026-09-29): the area load runs the
  loader task's own steps (the area streamer 001FFCD0, its sound-bank
  upload 001FB370 over the translated EE sound library and the IOP's
  command 0x20; MODULE_LOADER.md 1.9, IOP_STREAM.md "The sound-bank
  transfer"), so the veil ramps and decays for as long as those steps take:
  55 frames at host speed (the Original profile: the disc answers at host
  speed), 257 with the PS2 disc-drive timing switch (the recorded New Game
  reads), where the PS2 drew 258. Evidence: the whole New Game load against
  the ORIGINAL loader code (`make test-module-loader-reference` part H:
  with the switch, every captured loader state appears at its captured
  frame up to the sound-bank step, and one frame earlier after it, the
  PS2's ninth 001FB370 call); the
  sound-bank chain against the ORIGINAL code and route capture 00, its
  SPU and IOP RAM included (`make test-sound-bank-reference`); the loader's
  states in the live run equal the New Game capture's (`make
  test-area-load-reference`).
- Status: **PARTIAL**. The veil runs, ramps and is drawn for the load's
  own length. The one frame the switch still misses is the PS2's ninth
  sound-bank call (most likely its SIF DMA's hardware time, reproduced by
  no mode). No capture holds a frame taken during a load, so the pixels
  are proven against the GS model, not against a recorded frame. Since
  chain step GSFRAME (2026-10-03) the veil's list frames are drawn by the
  CPU GS model into the 512x224 field (GS_EXACT.md section 9, `gsw_list_frame`);
  only the Metal backend presents that field so far.

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
  Since chain C8b's OPENING step it is computed during the opening too.
  Needs framebuffer fetch
  (Apple GPUs). Decal texture from the disc (a sub-rectangle of module
  0x1B's library sheet, DISC_TEXTURES.md).

**Roger's own projected drop shadow**

Roger casts the original silhouette shadow of his own proxy mesh, as the
original does where he stands in view (the east tower and his encounter).

- How: Roger's face update 001BA580 calls the same original shadow routine
  001DA6A0 on his record (kind 0x29, his proxy mesh D_0028A490[0x29]) in the
  owner walk; its passes are drawn after the walk's owner units.
- Evidence: `SHADOW_ORIGINAL.md` "Roger": the original over the route 13 and
  14 captures builds Roger's chain and both captured display lists hold his
  silhouette pass; level smoke `check_shadow` (check_actor): 11,277 calls on
  the full route (5,116 drawn), 40 sampled calls re-executed as original
  instructions give the port's plan, and the port draws it at the aligned
  ticks of routes 13 and 14. Chain C8b FACE.
- Status: **PARTIAL**. Inputs and draw plan verified; pixels not compared
  (no framebuffer capture). The walk's shadow passes draw after all the
  walk's owner units, not at Roger's position among them (equal for
  depth-tested opaque units). Needs framebuffer fetch (Apple GPUs).

**The level is drawn from the game's own packets, through its own VU1 level program**

The snow level's static geometry (the 701 objects of the area's
static-object bank) is drawn from the packets the original builds every
frame: the same objects the original's visibility walk picks, in its order,
through the same vector-unit program, which transforms, fogs and
back-face-culls each vertex; objects crossing the screen's guard band go
through the original's clipping program. The remade level meshes of earlier
builds are gone from the first level.

- How: 001C1D00 and its whole call tree (the grid walk 001D5370 over the
  static-object bank, the packet builders) run live over the bank exported
  from the user's disc (`tools/export_static_world.py`); the level kernel
  0x00237180 is translated to C (`em_vu1_level_kernel.h`) and the clip
  kernel 0x00239C90 reuses the shadow chain's translation; a DMA / VIF /
  GS walk (`em_static_world_draw`) hands every drawn triangle to Metal
  (`em_gfx_gs_opaque`: the object units' GS pixel path with MODULATE).
- Evidence: `STATIC_WORLD.md` 8. `test_static_world_draw_reference`: all 17
  captured runs replayed with the original VU1 microcode, every packet the
  kernels kick equal byte for byte and every triangle equal (237 to 4,292
  per frame); 576 synthetic batches equal over the whole VU1 data memory.
  `test_static_world_gpu`: 99.78 % of 1,007,297 interior pixels of 05_boxes'
  run exact against the GS pixel model, 100 % within 2. The level smoke's
  `check_static_world`: over the route to Roger every frame's run drawn;
  32 sampled frames re-executed by the original 001C1D00 and the original
  microcode (the port's run and triangles exactly); at the camera-exact
  snapshots 10 and 14 the port draws the triangles the capture's own run
  draws. The shadow harness: level pixels around the shadow within 1 of
  the PCSX2 screenshot's means (routes 01, 08, 12).
- Status: **VERIFIED** for AREA11's geometry, culling, order and per-vertex
  values, relative to PCSX2. Caveats: the rasterization is Metal's (float
  interpolation, not the GS's DDA), compared with a GS model, not a GS
  framebuffer of the same frame. The kernels run on the CPU every frame;
  their multiplies and adds use the host FPU in round-toward-zero /
  flush-to-zero mode (`em_vu_host_lanes.h`, proven equal to the integer
  float model), which keeps the first level inside the 16.68 ms NTSC tick
  on an M1: headless newgame-control, main-thread CPU per in-level tick
  mean 5.6 ms, p99 9.2 ms, none of 1,331 over the period (before this
  fix: mean 15.9 ms, 472 over); the capped run's locked window takes
  22.79 s, exactly its 1,366 NTSC periods, as HEAD's legacy meshes did
  (before the fix 25.90 s) (`STATIC_WORLD.md` 6).

**Original sky background and world fog**

Where no level geometry covers the screen you see the original sky layer,
not the black earlier builds showed. Distance fog uses the original's
coefficients and colour.

- How: the translated 001E1E60 builds render channel 3's list every world
  frame; when the translated main-loop step V CALLs it, the port walks the
  list as its DMA sends it and runs its VU1 grid program 0x0023C990 by a
  translation of the microcode (since the units step, 2026-10-02; before,
  a native model of the grid checked against the kernel), and draws the 31
  strips it kicks; the texels are replayed from the user's own disc. Fog comes from the single
  translation of 0021B920 and the area fog 001D8FD0 on the live render
  context.
- Evidence: `BACKGROUND.md` Verification: the grid program's translation
  kicks the original microcode's packets qword for qword over 6 captured
  uploads (11,904 vertices; 126,976 with the full sweep); the level smoke's
  check_background runs the original program over the port's own upload on
  sampled ticks (equal triangles) and, in the camera-exact beats 10 and
  14, over the capture's own list (equal triangles); disc texels equal the asset and three GS
  freezes. Sky at first control: 6,843 of 6,912 samples black before, 0
  after, mean (48.2, 48.2, 48.2) against the original's (48.0, 48.0, 48.0).
  `tools/test_area11_fog_reference.py` executes the original fog chain
  against captured AREA11 RAM. Commits 8c9a9ad, ce7271f, fc272a7, e99d8cb.
- Status: **VERIFIED** for AREA11, relative to PCSX2. Caveats: the ERLENG
  model is the same on both sides of the test; the shader's per-vertex F is
  not executed against the original kernel; the fog blend itself is the
  measured GS rule with the 8.7 weight (next entry); the EFU's ERLENG is a
  model no capture has checked (the same on both sides of the test); pixels
  are Metal sampling and rasterization, not compared with a GS framebuffer.

**The GS fog blend, as PCSX2's software GS computes it**

Fogged pixels blend toward the fog colour with the GS's own arithmetic,
FOGCOL + ((C - FOGCOL) * F7 >> 15), applied after the texture function, with
the fog weight at the GS's 1/128 precision across a triangle (F7; for a
constant F this is FOGCOL + ((C - FOGCOL) * F >> 8)). With F = 255 a trace
of the fog colour remains, as on the GS.

- How: one C function pair (`em_fog_gs_blend7` / `em_fog_gs_blend`,
  src/gfx/metal/em_fog_gs.h) and one shader copy serve every fog site of
  the Metal path: the objects drawn by the translated VU1 object program
  (crates, drums, truck, panel, ...), the level itself (`em_gfx_gs_opaque`),
  the chain page's decals, sprites and lines, the player's drop-shadow
  receivers, and (since the fb2 step, 2026-09-28) the skinned path's draws
  (the status hub's and MAP page's models), whose fogged colour is now the
  GS's integer result on the 8-bit colour instead of a float blend. The
  weight of an interpolated F is `em_fog_gs_weight7`, floor(128 * F +
  0.01).
- Evidence: decomp `GS_CONFORMANCE.md` 5.5 measured the rule in PCSX2's
  software GS (4,096 of 4,096 flat-F pixels, 4,096 of 4,096 fogged-MODULATE
  pixels); `GS_EXACT.md` 5.2. `make test-gs-fog-conformance` runs the C
  function and the Metal shader over those captured tests' inputs and
  requires all 8,192 pixels equal to the captures (16,384 with the repeat
  capture under `EM_TEST_FULL=1`); the form the port used before, (F * C +
  (255 - F) * FOGCOL) >> 8, matches only 1,040 and 640 of 4,096 there. For
  an F that varies across a triangle (the eight p3_start `fog_*` tests) the
  8.7 weight at the exact plane value matches 342,349 of 342,831 channel
  values, the 8-bit weight the shaders used before 186,635 (part C of the
  same test). The fb2 pixel harness (the entry "The Original profile's
  frame, measured pixel by pixel") measured the fb2 step's changes together
  on the drawn frame: at the camera-exact points 10 and 14 the exact pixels
  went from 28.63 % and 24.88 % to 30.89 % and 28.79 %.
- Status: **PARTIAL**. The rule is exact; its inputs are not yet the GS's:
  the per-pixel F is Metal's float interpolation at host resolution, not
  the GS's DDA stepping (GS_EXACT.md 3.2), and the colour entering the fog
  is the 8-bit texture function where the GS uses the 8.7 colour (5.1).

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
- Status: **PARTIAL**. Since the static-world step (next entry) the level
  is drawn from the original packets through the translated level kernel,
  with the class-0 state its packets send (TEST 0x5000D: alpha test
  GREATER 0, bilinear, no blending) and its own vertex colours; the
  exported-mesh path this entry measured no longer draws AREA11. The
  railing's bright fringe is gone in the port's first-control frame, as in
  the fb2 reference frame (by eye; no pixel metric is re-measured here).

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
- Since chain C8b (FAILSTOPS) the DATABASE page 00214020, the SPR4 page
  00211970 with its five part pages, and the ITEM children EQUIPMENT
  00214570, EVENT 00215870 and HEALING 002160B0 run live too
  (`em_status_pages_live`), with their page-module textures decoded from the
  GS memory the original holds (`em_gs_texture`, from the user's disc). The
  level smoke's designed `status_pages` run opens each from the hub, backs
  out, changes the fire mode on SELECTOR, and takes 0x1E, 0x1F, the key 0x32
  and the magazine 0x10 (a medicine used from health 30 through the
  count-up and 0015C700): all 1,143 page calls replayed through the original
  instructions over the status-hub capture give the port's 12,636 callee
  entries and every byte (`STATUS_PAGES.md` section 7). No capture shows a
  page open, so their pixels are not compared.
- The MAP page 0020F950 runs live too (chain C8b MAP): its 22 UI-pool nodes
  run 002101C0 in the status models' pool, the map models come from the
  disc's module 0x1E bank (D_0028A570, `tools/export_status_map.py`) and are
  lit as 001CB480 lights them (lighting mode 2: the room rig through
  001D89D0, with the +0x02 glow) and drawn inside the page's SCISSOR_1
  window between its 2D layers; the player's marker triangle (00210F30 /
  00208040) and the item markers draw on the map. The `status_pages` run
  opens MAP from the hub (no map, then map 8), and takes the map 0x08 (MAP
  zoomed on map 8; R1 / R2 zoom, the D-pad pans, Circle to the list, Cross
  zooms again): with the other pages, 6,643 page and node calls (242 MAP,
  5,258 of its nodes) replayed through the original instructions give the
  port's 19,660 callee entries and every view byte. 001C6120 / 001C6150 over
  the disc bank are compared with the original (`make
  test-status-map-reference`). The map models' pixels are the port
  renderer's (the hub models' skinned path with 001D89D0's matrices), not
  compared.
- Page sprites: the original's 00207E40 sets CLAMP_1 = 5 (clamp both axes)
  for each sprite, and every 00207D00 blend block sets TEX1_1 = 0x60
  (bilinear magnification and minification; the blocks' bytes in the
  status-hub capture). The port samples the page atlas bilinearly with each
  texture's edge texels repeated around it, which is that clamp; the GS's
  exact bilinear weights are the renderer's (not compared).
- Message glyphs: their prebuilt packets D_002510C0 and D_00251140 write
  TEX1_1 = 0 (nearest) before the passes (read from the ELF; the glyph
  reference test checks the packets equal to it). Since the fb2 step
  (2026-09-28) the glyph strips sample the port's font atlas nearest
  (`em_gfx_overlay_glyph_nearest`); before, bilinear (`MESSAGE_GLYPH.md`
  "The draw boundary").
- The area-title card (the opening line and, with infection, the band
  line) is drawn by its own node 001C5930 through the same 001CC1E0 glyph
  runs since the units step (2026-10-02; the entry "The area title card"
  below).
- Status: **PARTIAL**. Glyph pixels are
  drawn from the port's own atlas (the original's TEX0 / CLUT, TEST and
  ALPHA are not modelled) and are not compared (no fb2 frame shows text).
  The BATTERY page's module load runs the original loader's own steps
  since chain C8b LOADER, every other page's since chain step PAGELOADS
  (see "The status pages' module loads" below).

**The status pages' module loads: the original loader's own steps**

Before the BATTERY page (the battery pop-up and the panel's prompt) the ITEM
root loads module 0x21, the page's texture upload, and waits for it; every
other status page loads its module the same way (the ITEM root 0x1F, MAP
0x1E, SPR4 0x2C, DATABASE 0x24, EQUIPMENT / EVENT / HEALING 0x20 / 0x22 /
0x23, SPR4's part pages 0x2D..0x31). The
port runs the original loader for it, step by step, once per frame, as the
PS2 does: the request 001FF080, the slot-2 task 001FF0D0, the bank and chunk
streamers 001FF830 / 001FF3F0, the disc read, poll and DMA routines 00200780
/ 00200730 / 00200830, and the busy byte D_00275BD8 the page waits on. The
disc answers at host speed, so the load takes the loader's 10 steps where
the PS2 took 24 frames and the prompt comes 14 frames sooner (16 ticks after
the request instead of 30); with the PS2 disc-drive timing switch it takes
the recorded 24.

- How: `em_module_loader` (the I/O routines and a host drive over the
  user's exported disc sectors, `tools/export_module_loader.py`) and the
  translated state machine of `em_status_scene_original`, bound live on the
  task table's slot 2 (MODULE_LOADER.md section 4). The page's upload is
  applied when the loader's chunk step sends it; each page module's upload
  is proven equal to the texels the port draws the page with
  (MODULE_LOADER.md finding 8).
- Evidence: the level smoke's `check_module_load` (routes 01 and 03, every
  run through the battery and the panel): after every frame, the loader's
  record and busy byte equal the capture's rows without the drive's 14 busy
  polls (host speed), or all 24 rows (the switch, `make
  test-level-smoke-ps2-drive`); the panel's rows from the load's completion
  to the Yes press equal route 03's at that shift, and the prompt comes
  exactly 14 ticks sooner (0 with the switch). `make
  test-module-loader-reference`: the routines and whole loads against the
  original instructions over captured RAM, 45 of the disc's module headers
  included; part I (chain step PAGELOADS): the 13 page modules' whole loads
  against the original loader code, each upload against the port's GS data
  and ITEM atlas. The level smoke's `status_pages` run: its 20 page loads
  each take the loader's 10 host-speed steps with the loader's rows.
- Status: **VERIFIED** for module 0x21 on the recorded route, relative to
  PCSX2 recordings. **VERIFIED against the original code** for the other
  page modules (since chain step PAGELOADS, 2026-09-30): no capture shows a
  status page open, so their load time is not compared with a recording,
  and with the PS2 disc-drive timing switch they load at host speed (no
  recorded drive time exists for them). The area load is the loader's
  since chain step H7 (see the load veil entry).

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
- Since chain step CAMERAS (2026-10-02) the opening's camera timeline is
  the original's too (0022EC30 / 0022EEF0, equal to the original's camera
  block on every sampled frame of the opening, and the hand-off to first
  control on all 64 sampled frames), and nothing stands in for the camera
  (the examine shots are their scripts' original camera ops; the aim
  camera is the original's since 2026-10-01 / 02: "Aiming, firing, the gun
  lamp and the knife on the original code").
- Camera actions 9, 10, 11 and 14 (the player codes 0x10 / 0x11 / 0x12 /
  0x28: the router sets 9, 11 and 14, the actions hand on 9 -> 10 -> 11
  -> 0 and 14 -> 0) are the original's since the CAMERAS fix rounds
  (2026-10-02 / 03): instruction oracles over recorded RAM and the live
  dispatch equal to the original 0018BC20 on captured AREA11 scenes, one
  frame and the hand-offs frame by frame; no AREA11 recording reaches
  them, so no capture shows them in play (CAMERA_LIVE.md section 6).
- Status: **PARTIAL**. The slide entry is 0.863 units off (relaxation
  pending review).

**The Original profile's world frame is drawn by a model of the GS, at the GS's own 512x224**

The world frame is not drawn by the GPU: the game's own GS packets for it
(the sky grid, the level, the objects and faces, the drop shadow, the
effects page, the load veil) go to a CPU model of the PS2's Graphics
Synthesizer, which draws them into a 512x224 field in GS memory with the GS's
own rasterization, texturing, fog, alpha test, blending and Z rules, from
the textures the game's own uploads put in GS memory. The platform layer
only shows that field.

- How: `src/gs/em_gs_raster` is the clean-room GS model (GS_EXACT.md
  sections 1..8: 906 designed conformance tests drawn by PCSX2's software
  renderer, 703 bit-exact, the open items counted); `src/gs/em_gs_world`
  records each world frame's register writes, the original state blocks
  its lists REF (read from the boot builder's GS blocks), the kicked list's
  draw environment and clear, and runs them at the kick on worker threads
  (row bands that draw exactly what one model draws), while the game builds
  the next frame. The area load's texture uploads reach GS memory through
  the loader as the original sends them; the boot's library comes from the
  disc export (GS_EXACT.md section 9). The field is shown line-doubled at
  its interlaced height and placed by the display registers (the entry
  "Each field shown at its interlaced height" below).
- Evidence: `make test-gs-raster-reference` (the model; part F the row
  bands), `make test-gs-world` (1, 2, 3 and 8 workers draw the same
  fields, thread sanitizer), `make test-dof-pass-reference` (001DDE10's
  pass against the original's GS memory on both sides of it in 7 captured
  frames: bit-exact through the model and through the workers' path),
  `make test-gs-memory-reference` (the GS
  memory after the area load equals the disc model of the uploads in all
  5,504 blocks), `test_shadow_original_reference` (the shadow target packet
  in order); the level smoke through Roger with the GS frame on; the fb2
  pixel harness (next entry). Commit: chain step GSFRAME.
- Status: **PARTIAL**. The 2D overlay pass (message glyphs, letterbox bars,
  screen and transition fades) is still drawn by the GPU over the field; the
  status screens draw with the GPU. 001DDE10's depth-of-field pass (the
  soft background, CHAIN_PAGE.md section 6.2) is drawn by the model since
  step DOF (2026-10-09): its register sequence equals the original's walk
  of every captured page (`make test-chain-page-reference` part G), the
  camera-exact fb2 point 14 rose from 87.18 to 92.28 % exact pixels with
  it, and since 2026-10-10 its pixels are bit-exact against the original's
  GS memory on both sides of the pass in 7 captured frames (5 cutscene, 2
  play; all 4 MiB of local memory, through the model, the port's own
  packets and the Original profile's worker path: `make
  test-dof-pass-reference`, CHAIN_PAGE.md 6.2; one model rule changed for
  it, a UV sprite's row coordinate accumulated in binary32, GS_EXACT.md
  3.4). Not captured: a blend reaching pixels of a whole-line field
  (GS_EXACT.md 8.8). The GPU renderer does not draw it. Frame cost (GS_EXACT.md 10.2): in the first level every in-level
  tick was under the 16.68 ms period on this M1 Pro when the workers had
  free cores (re-measured at the merge, 2026-10-08, before the pass); under
  heavy machine load some ticks ran over on wall time. The pass adds about
  3.9 ms of the busiest worker's CPU per frame (+50 %); on a heavily loaded
  machine 652 to 740 of 1,300 in-level ticks then ran over the period, and
  the quiet-machine figure with the pass is not measured yet. AREA01 (level 2) draws through
  the same model since the merge, but 65 of its 840 measured ticks run over
  the period (its main thread alone takes 13.6 ms). The model is PCSX2's
  software renderer's behaviour as measured, not real hardware's.

**The Original profile's frame, measured pixel by pixel against PCSX2's software-renderer frames**

The project holds exact displayed fields, rendered by PCSX2's software GS
renderer, at 19 points along the first level, and a harness that compares
the port's Original-profile frame with them pixel by pixel: "looks like the
original" is a number.

- How: the decomp's `tools/c7cap_partb.py fb2` loads each snapshot, steps two
  frames and de-swizzles the displayed buffer, draw buffer and Z from GS
  memory. `tools/test_fb2_pixels.py` (`make test-fb2-pixels`) drives the
  port headless to the tick the level smoke aligns with the field's game
  state, captures that frame and compares the presented GS field (the
  field under the overlay pass, read at the field's pixel centres) with
  the original's, word for word: the exact-match fraction, mean and maximum
  channel error and a difference image (GS_EXACT.md section 10.1).
- Evidence: decomp `CAPTURES_C7.md` 5b (decode proof: block-seam ratio
  0.93..1.15, luma correlation 0.989..0.998; route03_end reproduced byte for
  byte). At route snapshot 14, where the port's camera and field phase are
  the original's: 105,973 of the 114,688 pixels exact (92.40 %), mean
  channel error 0.89, per-pixel error 0 at the median and at the 90th
  percentile, since step DOF (2026-10-09) drew 001DDE10's depth-of-field
  pass (87.18 % and 0.97 before; the sky region's ±1..3, untraced until
  then, was that pass: GS_EXACT.md 10.1) and its conformance step
  (2026-10-10) made the pass bit-exact (92.28 % before it); the rest is the
  fan blades' phase and the snow's rand() stream. At 13 (camera not
  exact): 62.60 % (55.92 % before the pass). With the GPU renderer, which does not draw the pass,
  the same points gave 28.79 % and 35.35 %. At the AREA01 arrival (point
  15, camera exact): 98.57 % (GPU 47.41 %), a frame mostly under the
  transition fade, unchanged by the pass (`EM_TEST_FULL=1 make
  test-fb2-pixels`; the earlier numbers were re-measured unchanged at chain
  step ROUTE, 2026-10-08, port HEAD 2e5fa30).
- Status: **PARTIAL**. At 5 of the 7 compared points the port's frame loop
  is in the other field phase (the field drawn half a line off: at
  snapshot 10, camera exact, 18.74 % with the pass, 15.96 % without); the cause is not traced (GS_EXACT.md
  10.1). The snow and the flame follow the port's rand() stream and the
  fans' phase the recording's timing. The software renderer is PCSX2's
  model of the GS, not hardware. The field-to-buffer pairing rule is not
  established at 4 of 19 points (the harness reads each point's pairing
  from its own registers).

**Each field shown at its interlaced height, and the options screen's SCREEN ADJUST moves the picture**

The game draws one 512x224 field per tick and every other one half a line
lower (OFY 1936.5); the PS2 showed them interlaced over 448 TV lines, at a
position its display registers set. The Original profile shows each field
line-doubled at its own interlaced height (the half-line field one line
lower, so the half-line offset cancels and the picture does not bob), and
places the picture where the original's display registers put it: the
options screen's SCREEN ADJUST moves it, with black (the display's
background colour) where it uncovers the window's 4:3 area. Nearest
neighbour, no smoothing, no CRT simulation (the user's decisions of
2026-10-09).

- How: main-loop steps R (001AB4E0 with the SDK's 001002E0) and U
  (00100550) run as translated original code every iteration and hand the
  display registers to the presenter; each field's line comes from the draw
  offset that drew it (GS_EXACT.md section 11), and the GPU-drawn overlay
  pass (letterbox bands, subtitles, fades) is placed with that same line,
  so on both parities the bands and text cover whole field rows and move
  with the field, as the original's do (it draws them into the field
  through the same draw offset). The movies stay at the
  default position whatever SCREEN ADJUST holds, as in the original: its
  movie driver (00203350 via 00205050) stores its own display environment
  at offset 0 with BGCOLOR 0, and the port shows the movie unplaced.
- Evidence: `make test-display-env-reference` (the original 001002E0,
  00100550 and 001AB4E0 executed against the translations, every branch
  both ways; at offset 0 the registers are the ones measured in PCSX2 at
  all 19 points, and the route captures' display environments equal the
  port's byte for byte); `make test-gs-display` (the placement: both
  parities, offsets 0 and +-20 per axis, the uncovered edge, the crop; the
  overlay pass's bands and a text strip covering exactly their whole field
  rows on both parities, and the old placement splitting rows 31 and 191);
  `tools/check_present_capture.py` over headless captures (2026-10-09:
  both parities at a still tick exact in all 2,764,800 pixels; the
  opening's letterbox frames exact outside the bars; after the overlay-line
  fix a letterbox and a subtitle frame on each parity with `--bands=32`:
  the overlay viewport carries the field's line, 32 + 32 band rows, no
  field row half band, half picture, the subtitle on the same field rows
  194..216 on both parities). The demo video's re-run (decomp
  VIDEO_COMPARE.md) shows the overlay share at 64 of 224 rows on both
  parities and the cutscene differences back at their earlier values.
- Status: **PARTIAL**. Horizontal edges still flicker by one line each
  field (each field has only every other line's detail; measured in the
  presentation preview), as with any field-by-field display without CRT
  simulation. The direction of the screen position (Left moves the picture
  right, Up moves it down) is inferred from the code and the register
  meaning, not observed: no recording shows a moved picture. The overlay
  pass is still drawn by the GPU at host resolution over the field (its
  rows are placed as the field's, its pixels are not the GS's), and
  GPU-drawn frames (the status pages, the options screen) take a new
  position one frame after it changes. Only the Metal
  backend (macOS, iOS) presents it.

**Still drawn by legacy or stand-in code (disclosure)**

These first-level visuals do not yet come from the original draw path.
Advertise the items above only.

- Evidence: `OWNER_DRAW.md` 11, `CHAIN_PAGE.md` 6, `LOAD_VEIL_PARTICLES.md`
  5, `BACKGROUND.md`, `STATUS_PAGES.md` 7, census lane L38,
  `FIRST_LEVEL_AUDIT.md` 1b (re-made 2026-10-08, chain step ROUTE).
- Status: **PLANNED**. The status
  hub's and the MAP page's models are drawn by the renderer's skinned path
  with the original's matrices. 001DDE10's depth-of-field pass is drawn by
  the GS model only (not by the GPU renderer); its pixels are bit-exact
  against 7 captured frames of it (`make test-dof-pass-reference`). The 2D overlay pass (message glyphs, letterbox, fades) is drawn by
  the GPU over the GS field, and the status screen's frames (the hub and
  its pages) still draw with the GPU renderer, not the GS model (GS_EXACT.md
  section 9; their uploads go to the status runtime's GS data). (The
  area-load veil, once listed here, runs
  for the load's own length since chain step H7 and is drawn by the GS
  model since GSFRAME: the load-veil entry above.) (Roger, his face
  and his shadow have been drawn by the original code since chain C8b's
  FACE step: see the face entry and "Roger's own projected drop shadow".)

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
  an oracle of their bodies. 001FB100 runs whole at step H since chain step
  H7 (its output-mode commit never fires on the route). The last full sweep
  predates commit 7dea4ce. Audio output is not compared.

**The AREA11 flame's looped sound runs on the game's own looped-voice service**

Near the flame in the first level the fire's sound loop (0x413) starts,
pans and stops as the game's own service decides it, from the flame's own
owner code, and once started it plays on as a held loop, as in the
original.

- How: the flame owner 008235F0 (decomp C byte-identical) runs on its pool
  record and calls the translated looped positional service 001FC3C0 /
  001FC520 (em_sfx_loop_service) with the original's arguments (sound
  0x413, radius 100), the scratchpad frame counter and the walk ordinal
  (`AREA11_EFFECT.md` "Sound", chain step A11FIX). Its distance is the
  original's listener D_00810360, the player's hip (chain step AUDIO).
  Its script keys the looping tone on and off in one exchange; the key-off
  is lost, so the voice sustains and its track stays held (measured in the
  decomp's audio captures: `SFX_SEQUENCER.md` "Measured").
- Evidence: test-area11-sfx-reference (001FC3C0 against the original
  instructions); test-area11-effect-reference (the owner's calls); the level
  smoke's check_overlay11 (no track requests 0x413 before first control);
  the sound check against the decomp's audio captures (`LEVEL_SMOKE.md`
  "The sound state"): D_00281B70 / D_00281C30 row for row and the held
  voice's fields with the beats flame and walk_room.
- Status: **VERIFIED** at the command level (the service, its track and its
  voice record). What reaches the speakers is the port's SPU2 voice model
  (`FIRST_LEVEL_AUDIT.md` 1b item 1): no claim about the sound itself.

**The sound effects follow the original's sound driver at the command level**

In the compared windows, which sound starts on which frame and when each
sound's voices are reset are the original's: the PS2's sound driver runs
inside the game's frame loop, once per field, as the PS2's sound thread
does at every vblank. The voice and track indices it takes, and the
footstep variants in the side runs (they follow rand()), are not
compared.

- How: em_sfx.c runs the translated driver (00119EA0 tracks, the 001152D8
  tick: 00118EC0's reaper, 00115850's key-on through 00117428, 001176E0,
  00118078, 00116598) on the game thread at every field, with the IOP
  exchange modelled: the reaper reads the IOP's previous reply, the
  commands reach the SPU2 model at the IOP driver's own ticks. The panel's
  0x3EF plays through it like every id; the voices carry the bank handles
  the game registered (chain step AUDIO, `SFX_SEQUENCER.md`).
- Evidence: the level smoke compares the port's per-frame sound state
  (the stream cues, the voice ring, the looped-service tables, the delayed
  cues, the voice records, the reaper's feedback) with the decomp's ten
  audio beats of the original: the opening, the battery's take and status
  page, the panel and its BATTERY page, the elevator ride, the fence door,
  Roger's conversation and encounter, two quiet walks and the flame
  (`LEVEL_SMOKE.md` "The sound state"); test-area11-sfx-reference (every
  command word in lockstep with the original 001152D8), test-area11-sfx
  (the native output against an independent SPU2 model on the original
  command stream).
- Status: **VERIFIED** at the command level in the compared windows (a
  sample's end lands one frame apart where the IOP timer's phase differs:
  hardware timing, one case in the fence door). Not compared: the voice
  and track indices, the footstep variants in the side runs (counted
  only), and cage_roof's sounds (its cue states only). No claim about
  what reaches the speakers.
  The audible stages (reverb, interpolation, ADSR, levels) are the port's
  documented model; no WAV of the original exists to compare them
  (`FIRST_LEVEL_AUDIT.md` 1b item 1).

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

**The disc drive: host speed by default, the recorded PS2 drive as a switch**

In the original, a cutscene waits for a voice line to finish, and a line
cannot start until the disc has read it. By default the port's disc answers
at host speed (the code is the oracle; PS2 hardware timing is not
reproduced): every step the game's code takes around a read still runs, but
the drive's own wait is gone. So each voiced line starts and ends 6 fields
sooner than in the recording, and the opening hands over control 21 frames
sooner. The launcher switch "PS2 disc-drive timing" (off by default,
`LAUNCHER_OPTIONS.md`) applies the drive timing measured in PCSX2 instead,
so conversations keep the recording's pacing.

- How: the IOP stream backend's reader (`IOP_STREAM.md` section 4) serves
  the exported sectors. Host speed: a read is done at the first query after
  its issue, whatever its distance. The read sequencer 001FA0D0 (one step
  per field, its ready query before each issue) and the hold before a
  key-on are the translated code in both modes. Switch on: a drive model
  measured from the C7 capture: one read at a time; the position is where
  the last read ended; a 0-, 2- or 6-field seek by distance class, and a
  17-field seek for the first stream read after a screen-module loader read
  (since 2026-10-09: the loader's reads reach the model; measured in the C7
  capture's opening and the audio captures' status-page resumes); a read
  of up to 16 sectors completes within one field. The model faults on
  longer reads, which only module loads issue; module loads go through the
  screen-module loader's own drive instead: host speed, and with the switch
  the busy fields recorded for module 0x21's two reads (the BATTERY page:
  24 frames) and the New Game's; the other page modules' reads have no
  recording and stay at host speed (counted as unmeasured). The switch lives in the one settings struct
  (`src/em_settings.h`, `EM_PS2_DISC_DRIVE_TIMING=1` until the launcher
  exists).
- Evidence: `LEVEL_SMOKE.md` "The stream drive's two modes". Host speed
  (`make test-level-smoke-full`): `check_voice_drive` requires each voiced
  line's lane to start on the capture's row, its read to take one field
  against the capture's 7, every ready query and lane-0 read before it to
  take the host-speed rows, and its hold to the key-on to equal the
  capture's; the teardown then lies exactly the key-on's shift from the
  capture's row (6 rows for 0x97 and 0x99, 8 for 0x7F with its 2 rows of
  navigation). `check_rand_order` / `make test-rand-order` require first
  control exactly 21 frames before the original's: the fields the capture's
  opening stream request waited on the drive (15 for the ready query, 6 for
  the read). Switch on (`make test-level-smoke-ps2-drive`): the voiced
  lines compare the capture's rows as before; since 2026-10-09
  `check_rand_order` / `make test-rand-order` require the opening's stream
  request to take the C7 capture's rows (16 ready query, 7 read, 4 hold),
  first control on the original's frame and every rand() call of the
  opening equal to the original's through first control (31,304 calls in
  the level smoke), and the audio check requires the music's key-on after
  the battery_ui and panel_power status closes on the captures' rows. `IOP_STREAM.md` "Drive model
  (measured, 2026-09-27)"; decomp `CAPTURES_C7.md` 1: the model equals 186
  of 205 captured reads, the other 19 one field off (sub-field poll phase);
  the four reads the first level's timing rests on took the model's 6 seek
  fields. `tests/iop_stream_test.c` covers both modes.
- Status: **VERIFIED** at host speed on the recorded route (the event order
  is the capture's and the timing difference is exactly the drive's wait),
  relative to PCSX2 recordings. **PARTIAL** with the switch on: relative to
  PCSX2's CDVD emulation; a voiced line's teardown moves with the lane-0
  music refill one side's sequencer served first (navigation timing: in
  the 2026-10-09 runs 0x7F ends 2 rows early and 0x99 1 or 2 rows late); two
  Roger music reads fall outside the measured distances; the loads no
  capture measured (every page module but 0x21, the game-over module, the
  level exit's AREA01 load) take the same 17-field rule (counted). With
  the switch the opening's first control comes on the original's frame
  and the music resumes on the audio captures' rows after the two
  captured status closes (since 2026-10-09, the user's decision,
  `LAUNCHER_OPTIONS.md`). At host speed music timing is not claimed: the
  music resumes 17 fields early after every status page (the policy;
  `FIRST_LEVEL_AUDIT.md` 1b item 8).

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
  with `--require-through`: route beats 01..15 on the main line (19 phases
  since chain C11 EXIT),
  plus side beat 00 (the panel without the battery) and side beat 09 (the
  fence door), each in its own run. In the census 1.22 recount (2026-09-26)
  all 18 main-line phases passed and their capture checks passed again on
  the tick log. Census 1.30 (HEAD 4366957) and 1.31 list the same full
  target as passing, and census 1.33 (HEAD 6da4eb5) and 1.44 (HEAD 6594182,
  chain C8b ROUTE) re-ran every phase on an instrumented build and the
  checker on its tick log (PASS). The truck
  preview compares every row from f164 through
  the release at f527, plus 25 rows after it. Roger's encounter compares
  1,531 rows (f288..f1818) with every compared field equal. Mutations fail
  the checks: truck placement Y +0.001 fails at f167, slide entry speed
  0.2→0.21 fails at f82, the ladder step 3.0→3.0156 fails at f356, the
  running-jump launch speed 1.8→1.8005 fails at f240; with the PS2
  disc-drive timing switch on, a zero-latency or a 7-field-seek drive fails
  `check_voice_drive` at cage_roof, and at host speed (the default) a read
  one field slower fails it there and fails the opening's exact end.
- Status: **VERIFIED**. The reference is the PCSX2 recordings, not a real
  PS2. Only the first level (AREA11) is covered, and only route beats
  00..15. The level exit (beat 15) has its own entry below. The smoke's
  own walking between the scripted and climbing windows is navigation and
  is not compared. Relaxations the smoke reports (`LEVEL_SMOKE.md` "What the
  full route does not yet compare" and "Known divergences"): (1) the status
  page's module-0x21 load runs the loader's own steps; at host speed it
  takes 10 dispatches against the original's 24, and the rows after it are
  compared at that shift of 14 (0 with the PS2 disc-drive timing switch). (2) The
  voiced line 0x7F tears down 2 rows early because a music refill lands at
  a different phase. That phase comes from navigation timing. (3) Slide and
  beat-10 landings may land one row off, and slide heading changes two rows
  off, because the slide starts from a stance up to 0.86 units away (marked
  "pending lead review"). (4) Roger's idle-clip flags and the equipment's
  +0xB0 before f358 are exempt (navigation). (5) The status page's mode-4
  messages are skipped. The smoke does not compare sounds or pixels. Paths
  off the route not exercised: pause/options/save (census section 6); the
  route's branches (the other ladders, the optional items, the boxes) have
  their own entry below. (Aiming, firing, reloading, the
  gun lamp and the knife are compared row for row with the AIM captures by
  separate side runs: see "Aiming, firing, the gun lamp and the knife on the
  original code"; damage, death and the game over with the DAMAGE
  recordings: see "Getting hurt, dying, the game-over screen and the title
  after it, as in the original".)

**Aiming, firing, the gun lamp and the knife on the original code**

R1 and R2 aim the rifle with the original stances and aim camera; the
rifle fires, ejects shells, flashes, reloads and runs dry as the original;
the rounds strike the ground and walls with the original impact marks and
sparks; Square switches the gun lamp (in the first level only its flare
draws, as in the original); the knife swings in the original combos, and
knifing the security gun's power cable parts it and switches the gun off
(the area's taken bit set), as in the original. The status screen's
SELECTOR switches the rifle to 3-round bursts as in the original. The
knife's swing leaves the original's trail. Sounds are played by the same
code; only the swing's sound handle is compared, and only whether one is
held (see below).

- How: the stances, fire machines, the gun node's aim / shot callees, the
  lamp, the knife and the cable reaction run their translations through
  the aim / fire composition (`AIM_FIRE.md` sections 2, 3, 9 and 10); the
  port's own aiming and firing code is retired (2026-10-02). The impact
  sparks and the cable's sparks are drawn by the original VU1 programs
  translated to C (the streak and kind-2 programs, `CHAIN_PAGE.md`
  section 3).
- Evidence: the original-instruction oracles named in `AIM_FIRE.md`
  section 2 (among them test-aim-fire-lamp-reference,
  test-chain-page-reference's streak and kind-2 batches,
  test-effect-kinds-reference, test-coll-list-passes-reference); the level
  smoke's side runs (`make test-level-smoke-aim`, `LEVEL_SMOKE.md` "The AIM
  replays"): all twelve PCSX2 AIM captures (eleven side runs, aim_05's
  burst through the status screen included since chain step AIMCAP)
  replayed from route 08's end with the captures' own pad input, all PASS
  on 2026-10-02: on every row the player's stance and fire-machine bytes,
  clip and clock, the aim's pitch and yaw, the fire mode, magazine,
  reserve and light byte; the shots' records (markers, muzzle nodes,
  impact effects, knife trails, the cable reaction's nodes) with their
  header bytes; the gun's and the cable's state on aim_10 + aim_11's 2,020
  rows; and, since chain step AIMCAP, the whole player record (+0x000..
  +0x31F), the gun and knife nodes, the camera and the status block
  (`LEVEL_SMOKE.md` "The AIM side runs' whole records"): the player's pose
  in the melee equals the recording's on every row since the player's node
  slots come off the original slot stack (`AIM_FIRE.md` section 11.2;
  before, the knife's trail bent the player's pose from aim_09's frame 27);
  the knife's trail is drawn from its first call, as the original's census
  run shows (`AIM_FIRE.md` section 11.3: its point is the hand, the knife
  bone's world matrix, as 00189D30 passes it).
- Status: **PARTIAL**. Proven: the state above, row for row, relative to
  PCSX2 captures. Not proven: where a round strikes is compared by
  direction only (the side runs stand about 0.65 from the capture's start,
  so the range along a surface differs); the sparks' VU1 programs use the
  EFU, whose results are a model (the background renderer's) that no
  capture has checked; the lamp's cone shells are not reached in the first
  level; pixels are not compared; no capture records a death while armed
  (the DAMAGE recordings die unarmed); the knife trail's pixels (no original frame of a swing); the swing's
  sound handle: the port's track choice follows the host audio clock, not
  the game's tick, so it is not reproducible and differs from the
  original's on most rows (a port defect for the sound step, `AIM_FIRE.md`
  section 11.4); the status pages' module loads are shorter at host speed
  than the recording's (the user's host-speed disc policy); the AIM
  captures hold no other sound state.

**Getting hurt, dying, the game-over screen and the title after it, as in the original**

In the first level the fire on the pipe end burns you as in the original:
each touch costs 5 health, knocks you back, flinches you with the
original's clip, shakes the pad and leaves the burn effect on you; below 35
health the pad's heartbeat starts, faster below 10. Walking off the
plateau short of the jump hurts you on landing; falling into the truck pit
kills you. A death plays the original's fall and blood decal, fades to the
original GAME OVER screen (the disc's own art), and after its hold returns
to the title menu with the cursor on its second entry; New Game there
starts the level again with the original opening.

- How: the flame's contact pass, its knock-back table and the burn node,
  the player stage's hit, flinch, heartbeat, landing and death states, the
  death decal, the game-over task with its screen module and the title
  flow after a death all run their translations (`DAMAGE.md`); the port's
  own game-over and Continue screens are deleted.
- Evidence: `make test-level-smoke-damage` (`LEVEL_SMOKE.md` "The DAMAGE
  side runs"): side runs replay the decomp's DAMAGE recordings
  dmg_00..04, 06 and 07 window by window, all PASS on 2026-10-02, and since
  2026-10-09 dmg_08 row for row (dmg_fan: fan r2's hit, 5 damage, the
  knock-back reaction 0021E9C0 for its 52 ticks, the hand-back and the
  protection; the place to 5 decimals, the camera, the player record and the
  vitals equal on every row f32..f285) and dmg_07's fall start (the gait,
  the sub-state, the fall's speed and drop on rows 309..315): 20 hits
  on the recorded fields (the knock-back's per-tick step within 0.0082), 12
  heartbeats, the death to the screen load in 320 ticks and the pit's fall
  in 188 ticks as recorded, 74 game-over ticks after the load, the title's
  install and the New Game's task on the recorded counters, first control
  at the recorded place; the oracles test-effect-001F77B0-reference (the
  decal, with the recording's live decal in lockstep),
  test-title-menu-reference (the title after a death),
  test-area01-ui-reference (the burn node), test-scene-task-reference (the
  game-over task).
- Status: **PARTIAL**. Proven: the state above relative to the PCSX2
  recordings. Not proven or not covered: the game-over screen module loads
  at host speed (10 ticks; the disc took 23: the PS2 disc-drive timing
  switch does not model it yet, `LAUNCHER_OPTIONS.md`), so the screen
  appears 13 ticks sooner and the title menu takes input sooner (67 ticks
  against 104); the heavy landing from the towers and a running landing
  that asks 001755B0 are not replayed (no capture of either exists); the
  title's Options entry is not bound (its Load entry
  is since chain step OPTIONS: the next entry);
  infection cannot happen in the first level; pixels and sounds are not
  compared.

**The options screen, the memory-card load screen and the title's LOAD GAME, as in the original**

SELECT in the first level opens the original options screen: its list,
the vibration and sound (stereo / mono) toggles, the screen position, the
brightness picture, the button configuration with its three types, the
default prompt, the load row's memory-card screen up to its slot choice and
the quit prompt, with the original cues, fades and screen modules. The
title menu's LOAD GAME after a death opens the same memory-card screen. The
settings, the button masks and the screen offset are the game's own bytes,
read where the original reads them (the rumble, the sound mix, the
controls). The memory cards are two host folders in the original's data
layout.

- How: the screens are translated from the original instructions
  (em_options_original, `OPTIONS.md`) and run over the port's one storage at
  the original addresses (em_options_live); the card I/O is the platform
  boundary em_memcard (GetInfo and Sync over data/memcard/slot1 / slot2).
- Evidence: `make test-options-reference`: the original routines executed
  over the OPTIONS and DAMAGE recordings' RAM, RAM, scratchpad and
  arguments equal at every callee entry (EM_TEST_FULL=1: 3,337 cases, 491
  of 492 branch outcomes, the other unreachable);
  `make test-level-smoke-options` and dmg_load (`make
  test-level-smoke-damage`): the nine OPTIONS recordings and dmg_05
  replayed, every state from the open equal to the recording's and lasting
  its rows (`LEVEL_SMOKE.md` "The OPTIONS side runs"), both PASS on
  2026-10-08; `make test-area11-sfx-reference` pass M (the mono arms).
- Status: **PARTIAL**. Proven: the state above relative to the PCSX2
  recordings. Not proven or not covered: the screen modules 0x2A / 0x2B
  load at host speed (about 10 ticks against the recordings' 23 rows); the
  screen position moves the picture since 2026-10-09 (the entry "Each field
  shown at its interlaced height"), in the direction the register meaning
  gives, which no recording shows; choosing a memory-card slot, loading,
  saving (the first level has no save point) and quitting with Yes are not
  recorded and stop the game or are untested; pixels and sounds are not
  compared.

**Off the main route in the first level: the optional pickups, the other ladders, breaking boxes, the elevator back up, declining the panel and talking to Roger, as in the original**

Away from the recorded route, AREA11's other branches play as in the
original: the five optional items (item types 0x1E twice, 0x1F, 0x32 on
the cage floor and 0x10 where box r6 stood) and the map item are taken with the
original take, page and taken bit; the west-yard and plateau ladders climb
up and down (the grab from above included); the knife breaks a box, which
leaves its broken husk, sounds and throws its debris, and a box resting on
it falls and breaks; the elevator carries you back up; answering No at the
panel's prompt leaves the power off with the original cancel script; and
talking to Roger after the meeting plays his line.

- How: the item owners, the ladder and climb states, the boxes' damage
  break (its cue 001FC580, the husk rebind, the debris node 001F2BA0 and
  the effect handlers 001EBD20 / 001EAD70), the knife's strike 00189FE0,
  the use dispatcher's target scan over the class-2 list (Roger) and the
  talk script run their translations (`LEVEL_SMOKE.md` "The BRANCH side
  runs"; `CRATES_DRUMS_ORIGINAL.md`; `EFFECT_MANAGER.md` 8.2;
  `AIM_FIRE.md` section 12). Roger's line streams VOICE.DAT cue 1.
- Evidence: `make test-level-smoke-branch`: ten side runs replay the
  decomp's BRANCH recordings br_00..br_14 window by window, all PASS on
  2026-10-03: the six takes (their pages row for row around the module
  load), five ladders (one realigned on the dismount after a pad glitch of
  the recording), two ledge climbs with their step-offs, the two box breaks
  (the four boxes row for row over 200 and 147 rows, r3's fall included),
  the ride up, the panel's decline and Roger's talk; the oracles named in
  `FIRST_LEVEL_CENSUS.md` section 1.60.
- Status: **PARTIAL**. Proven: the state above relative to the PCSX2
  recordings. Not proven or not covered: the page module loads and Roger's
  voice read run at host speed (9..10 ticks against 24..26; one row
  against seven, so his line ends six ticks sooner; the PS2 disc-drive
  timing switch puts the voice read on the drive model of the other voiced
  lines (not run for this beat), while the take pages' modules
  load at host speed in both modes, their reads' busy fields not being
  recorded; the BRANCH runs are checked with the switch off); the drums do not break (what damages them
  is not established); the fans' fast-arm exit and the security gun firing
  are not reachable on this visit; sounds and pixels are not compared.

**Skipping a first-level cutscene behaves as in the original**

Pressing START or SELECT during a cutscene the original lets you skip (the
AREA11 opening, the director's three voiced beats on the route, Roger's
encounter) fades out, lands the scene where the original lands it and
hands control back on the same frame after the press as in PCSX2. Until
2026-09-29 every such skip stopped the port about 34 frames after the
press ("0015BA50 D_00248C98 worker fault").

- How: the skip landing (script op18, 001B6BF0) stores -1 in the player's
  clip index; the player stage reads the rate of that row, the ELF's row
  before the clip-rate table (0.0), until the release restores the index.
  The port now exports that row from the user's ELF with the rest of the
  column and still stops on any index the original never stores.
- Evidence: `PLAYER_STAGE_WORKERS.md` section 2.2 ("Row -1"),
  `AREA_SCRIPT.md` "The skip path". `make test-cutscene-skip` (about 7 s)
  skips the opening in the headless game and compares every frame from
  the promotion to eight frames after control with the PCSX2 skip capture:
  the scratchpad bytes, the player's state, clip index, clip rates, the
  camera mode byte, position and facing, and the frame control returns
  (promotion + 34) all agree; `EM_TEST_FULL=1` adds a later opening press
  and the four route scenes (promotion + 36, + 35, + 35, + 35, as
  captured), each of which also passes the level smoke through roger. A
  build that refuses the -1 row fails the test with the user's fault
  line. `make test-player-stage-workers-reference` checks the 0015BA50
  prologue with the -1 index against the executed original.
- Status: **VERIFIED** for the opening, director beats 0..2 and Roger's
  encounter. Not covered: the record-13 manager 008257A0's script 0x829E80 and Roger's armed talk (both
  skippable, neither reached on the route) and the level exit's departure
  movie, which is a movie skip. The opening and Roger's encounter place the
  player and are compared to the bit; for director beats 1 and 2 the
  position and facing are compared as changes across the skip. Two open
  differences: the fade substate steps from 3 to 2 one frame later in the
  port than in the capture in every run (the landing that waits on it runs
  on the same frame; not explained), and in director beat 0 the position
  is identical but the facing is one scripted-turn step (0.0349 rad) off,
  because the port's skip lands 11 frames after the scene arms and the
  original's 13 (not explained).

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
  section 1.46 (2026-09-29, chain step H7, recounted from the rows): 708 of
  the 756 non-boundary functions are live and verified (93.7% of
  functions; 86,021 of 88,729 instructions, 96.9%). 45 are verified
  translations that the live app does not run yet, 3 are unverified
  (0015CF90, 001B1190, 001FC280) and none is missing. 428 are platform boundaries (SDK, IOP, GS
  and similar). No row is classified stand-in. The last whole-route
  liveness measurement, section 1.44, used an edge-recorder build that
  confirmed all 703 live rows (1.33 had confirmed the 660 of its time).
  `FIRST_LEVEL_AUDIT.md` sections 1 and 4 list the removed
  fabrications (WP-0..WP-2; H8 fixed in 9d4a631; H13 via census L18; H16
  via census L23; H20).
- Status: **PARTIAL**. The census counts only functions the recorded route
  executes, once per label. It does not record which jump-table cases the
  route used (census 7.1). Beat 15 (the level exit) is not in the census
  tables. 0015BCF0 is live only in part. 001FC280, 0015CF90 and 001B1190
  have oracles executing them since chain step GLUE (census 1.64); the one
  unverified row left is the MAP page's draw 001CB480. "No stand-in rows"
  does not mean no stand-in code runs. Census 2.3 listed the chain page's
  depth-of-field pass as stand-in behaviour on the route until step DOF
  drew it with the GS model (2026-10-09; bit-exact against 7 captured
  frames since 2026-10-10, `make test-dof-pass-reference`)
  (the examine
  camera and the opening's camera timeline are original since chain step
  CAMERAS; the panel's, terminal's and items' takeovers are the player
  stage's own since chain step TAKEOVERS). Census section
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
  covered only through the movement phases of the level smoke. Since census
  L24 the security gun's plate (uid 15) is re-transformed by the gun's own
  lifecycle 0 and equals the original's in both captures; no original cell
  is left unpublished. There is one
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
  evidence for the live path. Since chain C8b's OPENING step the opening's
  player draws its original unit too, its node matrices equal the opening
  capture's bit for bit (`check_opening_actors`). The player's face attachment slot waits on
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
  The panel matches route 03 row for row, across the status page's module
  load at the host-speed drive's shift (`AREA11_PANEL.md`, `LEVEL_SMOKE.md`). Census 1.25: the terminal,
  panel, prop, items and canopy draw their original units, and
  `check_indicator_children` compares against routes 00..14.
- Status: **VERIFIED**. Covers AREA11 and the recorded route, relative to
  PCSX2. The BATTERY prompt window is compared from the module-0x21 load's
  completion, at the battery (route 01, its loader rows) and at the panel
  (route 03, the rows to the Yes press): the load runs the loader's own
  steps, 10 at host speed against the original's 24, the drive's I/O time
  (see "Resolved and open policy questions" below). The map and the other non-battery items are drawn and
  animated on their records. Taking 0x1E / 0x1F (HEALING 002160B0), the key
  0x32 (DATABASE 00214020) or the magazine 0x10 (SPR4 00211970) opens its
  original page since chain C8b (the level smoke's `status_pages` run,
  replayed through the original instructions); taking the map 0x08 opens
  the MAP page 0020F950 zoomed on map 8, bound since chain C8b MAP (the same
  run; `PICKUP_OWNERS.md` "Requests and pages", fixture `other_take`). These
  takes are off the recorded route. The panel's, the terminal's and the
  items' hold on the player is the original player stage's since chain step
  TAKEOVERS (the scripted-sequences entry below).

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
crevice prompt, east tower) and Roger's encounter, and, since chain step
TAKEOVERS, the power panel, the elevator terminal and every item pickup:
your character's grab, lever and panel animations start, run and end on
the original's frames. The camera shots, the
letterbox bars, the messages and your character's placement follow the
original script, and the voiced lines hold the scenes for as long as they
did in the recordings.

- How: the game's own level scripts run on the translated script
  interpreter and host ops. A script owner's frame is the player stage's
  own takeover, as in the original: the translated prelude admits the
  player, the stage runs each step, and the original release 00182DF0 hands
  control back. The panel's, terminal's and items' programs ask for their
  animation clips through the player record as the original's 001B9A00
  does, and the stage's translated 00183090 commits them. The voiced lines play on the stream lanes, and the scripts
  wait for each line to end. By default the disc answers at host speed, so
  each line's read takes one field where the recording's took 7; with the
  PS2 disc-drive timing switch on, the reads use the drive model measured
  in PCSX2 (see the disc drive entry above).
- Evidence: `AREA_SCRIPT.md`, `SCRIPT_HOST_WORKERS.md`,
  `PLAYER_STAGE_WORKERS.md` section 2.1, `DIRECTOR_ORIGINAL.md`,
  `STREAM_LANES.md`, `IOP_STREAM.md` "Drive model". `LEVEL_SMOKE.md`
  `check_stage_takeover` requires +4 = 4 from the admission to the release
  in routes 07, 09, 10, 11, 13 and 14, with the admissions on the captures'
  first 3B8F = 1 rows and the releases on their first 3B8F = 0 rows. Before
  the drive model, beats 11 and 13 released 6 rows early; with the PS2
  disc-drive timing switch on they release on the capture's rows (census
  1.30, commit 4366957; `make test-level-smoke-ps2-drive`). The drive model
  equals 186 of the 205 captured reads. At host speed (the default) lines
  0x97 and 0x99 tear down exactly 6 rows early, the drive's read time, and
  `check_voice_drive` requires that shift and nothing else. `AREA_SCRIPT.md`: the script sine
  translation equals the original instructions on 88,818 arguments (full
  sweep). `test_player_cinematic_reference` covers 1,388 stages over bank
  0x96. `test_director_original_reference`'s SDK atan2f part is
  bit-identical on 726 cases (quick) and 18,366 (full), plus the owner-tick
  sweeps. For the panel, the terminal and the items `check_takeover_record`
  compares the record's clip request +1F2, its clip and its clock with
  routes 00..04 row for row (the grab clip 0x42 committed at route 01 f129,
  the panel's 0x15C at route 03 f528, the lever's 0x47 at route 04 f190) and
  `check_stage_takeover` holds the takeover from the admission to the
  release; the BRANCH side runs check the other takes, the ride up and the
  panel's No the same way (`LEVEL_SMOKE.md` "The stage's own takeover").
- Status: **VERIFIED**. First level, relative to PCSX2 recordings. At host
  speed the voiced lines end earlier by exactly the drive's read time. With
  the switch on, the drive model's seek and read timings come from PCSX2's
  CDVD emulation, not measured hardware, and 19 of the 205 captured reads
  are one field off, because of a sub-field poll phase that no capture
  records. Line 0x7F tears down 2 more rows early in both modes because a
  music refill lands at a different phase. That phase depends on how long the player has walked since the
  music last started (3583 fields in the original against 3449 in the
  port's run), so it is navigation, not a mechanism difference.

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

**The security gun, its cable and the fan pair on their original code**

The dormant security gun above the fence door, its power cable hanging to
the ground and the two big fans run the original code and draw the
original's object units. The fans spin up, hold and spin down on the
original cycle, which decides when you can pass the fan safely on the way to
the exit. The gun stays switched off for the whole first visit, as in the
original (it only wakes on a return visit). (Earlier port notes called the
gun and its cable "husks"; that label was wrong.)

- How: `em_area11_bindings.c` runs `em_gun_tick` / `em_gun_cable_tick`
  (em_security_gun) and `em_fan_original_tick` on their pool records; their
  +0x4C draws go through em_owner_draw_live (census lane L24 / WP-11;
  `SECURITY_GUN.md` section 5, `FAN_ORIGINAL.md`).
- Evidence: the original-instruction oracles `test_script_door_fan_reference`
  (part 3), `test_security_gun_rest_reference` and
  `test_fan_original_reference`; the level smoke's check_gun_fan (the gun and
  the cable equal all 15 route snapshots on every tick after their setup,
  and every captured fan state is one the port's fans run through) and
  check_owner_units (their units against the original 001CAA00; the fans at
  the port's angle through the original 001C6380); `make test-rand-order`
  (the gun's one setup draw is the original's AE+1 call); `make
  test-collision-world-capture` (the gun's plate, uid 15).
- Status: **VERIFIED** for the first visit (relative to PCSX2 captures):
  spawn, the dormant gun and its dark lamp's rand() draws, the idle cable,
  the fans' cycle and all four draws; since 2026-10-02 also the knife's hit
  on the cable (the cable's and the gun's state row for row with the AIM
  capture aim_11: see the aiming entry). Not covered: the fans' phase at a
  given moment (it follows the recording's timing; only the cycle is
  compared); the fans' fast-arm exit and direct area change (no
  recording). Since 2026-10-09 the hit box is exercised: the side run
  dmg_fan replays dmg_08 and the hit, its 5 damage and the reaction equal
  the recording row for row (before, the binding's pending-damage store
  went to the record image and the hit did no damage; DAMAGE.md section
  7a); the gun's
  own return-visit
  behaviour (it stops the game if reached); the lamp's draw. The pixels are
  Metal's, not the GS's.

**The level exit: Roger's departure, the exit movie and the arrival in AREA01 (route beat 15)**

Leaving AREA11 plays as in the original: you wait for the fan's slow phase
and walk under it, Roger's departure walks you back and plays the exit
movie, the screen goes black while the next area loads (at your machine's
speed), and you arrive in the underground tunnel (AREA01) at the original's
spot, facing the original's way, with its camera, its fade-in, its music
and its ambient hum starting. The first level ends there; AREA01 itself
is level 2, whose first second of standing still is reproduced (the entry
"Level 2 (AREA01)" below).

- How: the fan's exit box, Roger's departure script (its walk and its movie
  handshake), the movie (E001.PSS, the original's selector 1 and its START
  skip), the area-change request, the area load through the original
  loader's own steps (AREA01's files exported from your disc into the
  loader pack) and the arrival's area rebuild (the spawn placement, the
  camera re-seat, the roster spawn of every AREA01 owner record, the
  ambient loop and the music cue) all run their translated originals
  (`FIRST_LEVEL_EXIT.md` section 7).
- Evidence: the level smoke's `exit` phase (`LEVEL_SMOKE.md` "exit")
  against the decomp's EXIT capture: 415 rows of the departure (exit_00
  f31..f434 and exit_01 f0..f10) equal row for row in the player's
  position, heading, state, clip and clock, the camera, the letterbox, the
  messages, the fades, the fan's cycle and Roger's record and script; the
  load passes the capture's 18 loader states in order, with its chain and
  load veil replayed through the original instructions; the arrival frame
  equals the capture's f306 in the player's placement, the camera, the
  fade-in, the music's read, the ambient loop id 0x44E and all 78 records of
  the actor pool (headers, +0x18..+0x3F and +0xA0..+0xDF). `make
  test-level-smoke-full` requires the phase. Original-instruction oracles:
  `test_roger_reference` (the departure branch), `test_area_script_reference`
  (the script 0x828A10), `test_fan_original_reference` (the exit box).
- Status: **VERIFIED** (relative to PCSX2 captures) from the fan crossing
  to the first AREA01 frame. Not compared: the movie's pictures and sound,
  the SPU2 output (no audio capture), AREA01's pixels (the arrival frame is
  under the fade-in's black), and the player's animation clock in the
  arrival frame itself (the port prepares the player's pose one step
  earlier in that frame). At host speed the black
  stretch between the area request and AREA01 is shorter (the original's
  disc reads and its longer load veil); the PS2 disc-drive timing switch has
  no recording of these reads. The fan's direct exit on a later return and
  its hit box are not on the route.

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
  call for call (caller and value) for the first 227 calls, up to the
  opening's actors' spawn, then caller for caller at the drive's shift for
  32 frames (126 calls before chain C8b's OPENING step); every frame's
  fixed-schedule callers (the sway, the indicators, the glow markers, the
  music) equal the original's over the opening, the 30 frames after first
  control and two aligned route windows (01: 66 frames, 10: 311 frames).
- Status: **PARTIAL**. Every draw of the opening sits at its original caller
  (since chain C8b's OPENING step). Its values differ from the actors' spawn
  on, because the stream request's wait is shorter at host speed (the
  Original profile's policy: the spawn comes 21 frames earlier; 11 with the
  PS2 disc-drive timing switch). A recording's exact values cannot be
  reproduced: the original's own order in the opening varies between runs
  after about 689 calls, and the port reaches each moment by its own route.
  The smoke therefore checks each random value as the original code over
  the same draws.

**Level 2 (AREA01): the arrival, the train room, the tunnel, the water and the stairs, the first talk, the fire's damage and the east room's terminal match the recordings; the rest of the level runs without a fault**

The second level's route has been recorded, and the code it newly needs is
being translated and checked against the original ahead of time, so level 2
can meet the same standard. Arriving in AREA01 and standing still for the
first second plays as in the original, the camera settling behind the
player. Walking round the train room's crates, wading through its floor
fields, grabbing the crate stack's ledge, hanging and starting the pull-up
also play as in the original, frame for frame (the splash effects and wet
footprints run the original code; their pixels are not compared), as do
the pull-up, the fall and the walk to the tunnel mouth. Touching the
ground fire hurts the player exactly as in the original, and the east
room's save terminal opens its prompt and closes on No as in the
original. So do the tunnel, the water at the shaft, the lower tunnel and
the stairs up to the shaft landing, and the control room's first talk.
The rest of the level (the locked shaft door and its conversation, the
second and third talks, the pickups, the duct, the exit) runs without
stopping and is compared as far as the voice and page-load timing allows
(below); hanging and shimmying, climbing the ladder to the ledge,
shooting and knifing anywhere also run without a fault.

- How: phase 1 recorded the original AREA01 route in PCSX2, ran a census
  delta of the new functions, wrote an area overview and matched the
  overlay in the decomp. Phase 2 translates each new function on its own
  and checks it with an original-instruction oracle over the recorded
  AREA01 RAM, comparing memory at every callee entry. The AREA01 assets are
  exported locally and checked byte for byte against the captures. The binding
  work (LEVEL2_BINDING.md), done on the branch `level2`, is merged into main.
- Evidence: port commits e21bd95 (wave 1: 90 translations, split into
  overlay 14, math 30, render 21 and sys 25, plus asset exports) and efe9f83
  (wave 2: 65 side-path and exit translations, split into UI 17, side 20,
  exitA 12 and exitB 16). `AREA01_OVERVIEW.md`, `SECOND_LEVEL_ROUTE.md`,
  `AREA01_*.md`. Decomp `docs/HANDOFF.md` "Level 2": the census delta has
  154 new functions (89 on the main line). Overlay matching bdd40fb: 32 of
  41 functions are byte-identical C and 3 are NEARMISS. The lanes found
  wrong decomp NEARMISS bodies, which were then corrected: 7 in 07c4e32, 2
  in 837d548 and 7 more in f799141.
- Arrival idle (step GUARD, 2026-10-04): AREA01's world frames run on its
  bound owners in every run (any original without an owner still stops the
  game where it is reached), and the level smoke's `a01_arrival`
  (`LEVEL_SMOKE.md` "a01_arrival", required by `make
  test-level-smoke-full`) compares the rebuild and 60 world frames with
  route 15 f741..f801: the player, the camera (its one-shot seat after the
  original's camera-block reset 001AF690, then the settle), the progress
  and story bytes, the message, the bars and the fade, equal on every row.
  Not compared: the AREA01 owners' records (route 15 does not record them),
  AREA01's pixels and sound, and any frame after f801.
- Train room (step MOVE, 2026-10-04): the opt-in phase `a01_00`
  (`LEVEL_SMOKE.md` "a01_00") replays route beat a01_00's recorded pad and
  compares every row with the recording: rows f0..f404 equal in every
  field (player, camera, requests, task, health, progress, message, bars,
  fade, all 11 recorded owner records). Since step DRAWN all 781 rows
  match: 001E3D90's near-fire layer (001CFBE0's kind 6, VU1 program
  D_0023D930, translated and checked against its original microcode over
  every captured kind-6 page) draws from f405, and the pull-up, the fall,
  the landing and the walk to the tunnel mouth follow the recording. Not
  compared: AREA01's sound.
- Drawn world (step DRAWN, 2026-10-04): AREA01's world is drawn from its
  original packets: the static bank and the dynamic objects' table (the
  level smoke re-executes the original 001C1D00 and its 001D5BD0 over the
  port's inputs on sampled AREA01 frames: every byte equal), the area's
  textures, the floor-field, ripple, dynamic and near-fire VU programs, the
  owners' models and the player's and the owners' shadows (sampled shadow
  plans equal to the original 001DA6A0's over an AREA01 capture). Pixels,
  relative to PCSX2's software GS: the arrival frame (fb2 point
  15_level_exit, camera exact) has 47.41 % of its 114,688 pixels exact,
  mean channel error 0.67, per-pixel maximum error at the 90th percentile 1
  (`make test-fb2-pixels-area01`; GS_EXACT.md section 10). Not compared:
  frames after the arrival (the route_a01 save states record no displayed
  field).
- Camera (step CAMERA, 2026-10-04): the AREA01 phases also compare the
  camera block itself: on every row its eye, target and the forward
  vector, and at a recording's last row the whole camera block byte for
  byte with the recording's saved RAM. The arrival passes it (all 61 rows
  and the whole camera at f801); a01_00 rows f0..f404 are exact with it.
  The control room's camera (mode 1, seated from the original scene-entry
  and eye tables) is bound but not reached yet: the control-room door's
  Use stops the run first (`LEVEL2_BINDING.md`, step CAMERA).
- Fire damage and the east-room terminal (the AREA01 crash sweep,
  Codex, merged 2026-10-07; LEVEL2_CRASHES.md): the side phases `a01_s3`
  (`python3 tools/test_level_smoke_area01.py --side a01_s3`: the walk into
  the ground fire, 5 HP of damage and the reaction; all 262 rows and the
  whole camera block at the end) and `a01_s4` (`--side a01_s4` with the
  PS2 disc-drive timing switch: the east room, the terminal's prompt and
  No, the return; all 1,225 rows and the camera block) pass. At host
  speed (the default) a01_s4's message timing differs from the recording,
  as the switch documents. Water contact, impact effects, bug hits, room
  transitions and the duct are bound with original-instruction tests, and
  every recorded AREA01 replay and exploration probe runs without a game
  fault; those runs are not compared row for row yet (the a01_01 replay's
  first pad command lags in the harness). Accepting the terminal's save
  still stops the game (fail-stop: no memory card is written).
- The level-2 check (Claude, 2026-10-08; LEVEL2_CRASHES.md "Level-2
  check", LEVEL_SMOKE.md "a01_01..a01_07 and the side beats"): with each
  recording's first-command latency read from the recording, a01_01 (306
  rows) and a01_02 (591 rows: the water's first contact, the lower tunnel,
  the stairs) pass with their camera blocks, as do a01_s0 (the control
  room and the NPC's first talk, 1,435 rows, with the drive-timing switch)
  and a01_s6 (the raised bridge, 229 rows); a01_03 (the locked shaft door) matches rows
  f0..f956 of 991, then the voice line ends one frame early (seven at host
  speed); a01_s1 / a01_s2 / a01_s5 match up to their pickup's page request
  (AREA01's page loads answer at host speed). The first command's
  latency of a01_01 onward is taken from each recording, so it is not
  checked independently; every later row is compared strictly. With
  00187350's ripple draw named in the RNG table (fix round, 2026-10-08),
  `--until a01_02` exits 0 with all its checks; `--until a01_03` and later
  stop at the row-957 voice line (LEVEL_SMOKE.md). Five reachable faults were
  fixed with original-instruction oracles (shots near a floor field, the
  0x35 stairs, the locked door's message request, the hang's side probes
  and sound, AREA00's disc sectors for the exit). Accepting the save
  terminal still stops the game (no memory card is written), and the
  AREA00 arrival after the exit is level 3's work.
- Status: **PARTIAL**, arrival idle, the train room (a01_00), the tunnel
  (a01_01), the water and the stairs (a01_02), the control room's first
  talk (a01_s0, PS2 drive timing), the fire contact (a01_s3), the
  east-room terminal declined (a01_s4, PS2 drive timing), the raised
  bridge (a01_s6) and the arrival frame's pixels (above). The arrival's rebuild
  selects AREA01's message bank during that rebuild without resetting its
  service or clearing its stream/presenter bindings. `test-message-area-reference`
  checks 11,956 bank bytes against the capture and 54 service ticks in quick
  mode (3,330 full) against the original instructions; LEVEL2_SERVICES.md ("AREA01 message-bank binding")
  states the exact scope. This does not make AREA01 dialogue or its route
  playable.
  The route-plus-arrival census has 179 entries (SECOND_LEVEL_CENSUS.md).
  The older mutation sweeps did not converge and were closed on named
  survivors. The actor-cell bit-29 mirror was already accepted by chain C11
  EXIT. The segment walker's no-span fail-stop remains: LEVEL2_COLLISION.md ("AREA01 collision prerequisite audit")
  bounds the recorded camera queries and explains the original caller-state
  dependency; no success value has been substituted.

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
  - **Optional switch (built 2026-09-27):** the PS2 disc-drive timing
    measured from the PCSX2 recordings (`IOP_STREAM.md` "Drive model") can
    be turned on for PS2-identical dialogue timing; since chain C8b LOADER
    it also gives the BATTERY page's module load its recorded 24 frames
    (MODULE_LOADER.md 1.7). It is off by default
    (`LAUNCHER_OPTIONS.md`; `src/em_settings.h`, `EM_PS2_DISC_DRIVE_TIMING=1`
    until the launcher exists). The level smoke runs both ways.
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
   smoke's panel-prompt check aligns on the load's completion. Built in
   chain C8b LOADER (the status pages' module loads entry above), for every
   page module since chain step PAGELOADS.
2. **The opening's stream timing:** at host speed the drive's wait goes (the
   policy). The extra seek follows the New Game's last module-loader read, not
   the intro movie's position (corrected 2026-10-09, `IOP_STREAM.md` "Drive
   model"); the PS2 disc-drive timing switch models it since 2026-10-09 (the
   user's decision "Yes, model it (17)", `LAUNCHER_OPTIONS.md`). The area-entry
   001FAE70(1) is game code and is bound (`RAND_ORDER.md` 2).
3. **Field presentation:** deferred; the user will compare the options
   (`LAUNCHER_OPTIONS.md`).
4. **High frame rates:** an Enhanced display rate keeps logic and streamed
   audio at 59.94 Hz with rendering decoupled (`PORT_PROFILES.md`).
5. **The "PS2 hitches" example:** dropped; see above.
6. **The load screen:** the load veil is game code, so it must be shown for
   however long the host load takes. Since 2026-09-27 it is bound and drawn
   (the entry above); since chain step H7 (2026-09-29) the area read runs
   the loader task's own steps, so it shows for as many ticks as they take
   at host speed (55 at New Game).

Missing faithful behaviour that blocks a "first level complete" claim (the
whole prioritized list is `FIRST_LEVEL_AUDIT.md` section 1b, re-made on
2026-09-28 after chain C8b; its headline items):

- audio output: no SPU2 reverb, Gaussian interpolation or master volumes;
  sounds are not compared in the smoke;
- visuals: pixels not compared with the reference frames (no harness);
  the GS-exact renderer is queued (the clean-room GS model of `GS_EXACT.md`
  is measured but not wired); the draws of the disclosure entry above are
  not yet the original's;
- logic still on stand-ins on the route: the panel's, the terminal's and the
  items' takeovers (census 2.3);
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

Last updated: 2026-10-09 (the disc-lead step: the PS2 disc-drive timing switch models the first stream read after a module-loader read (17 seek fields): the disc-drive entry's How, Evidence and Status (the opening on the original's frame and the music's status-page resume on the captures' rows with the switch; the teardown's lane-0 phase in both directions), the opening entry's status, policy question 2. Before, the music-lead step: the disc-drive entry's music caveat (the music resumes 17 / 15 fields early after every status page, the seek after a module load) and the opening seek's corrected attribution (the New Game's last module-loader read, not the intro movie), no new claim. Before, the overlay-line fix: the field entry's overlay pass placed with the field's line, its evidence and caveat. Before, the presentation step: the entry "Each field shown at its interlaced height, and the options screen's SCREEN ADJUST moves the picture" (PARTIAL); the GS-frame entry's placeholder sentence replaced; census 1.68 in the census entry; the options entry's screen-position caveat. Before, 2026-10-08, chain step ROUTE: the census entry's headline and recount 1.67 (99.2%, every row re-measured over the whole route and every side run); the route entry's evidence (through a01_arrival with every side run, port HEAD 2e5fa30); the load veil drawn by the GS model since GSFRAME, and the legacy-draw disclosure's stale veil claim replaced by the status frames still on the GPU; the fb2 numbers re-measured, unchanged. Before, chain step OPTIONS: the entry "The options screen, the memory-card load screen and the title's LOAD GAME, as in the original" (PARTIAL); census 1.65 in the census entry; the damage entry's title Load entry. Before, the merge of chain step CAMERAS: the opening's camera timeline and camera actions 0, 9, 10, 11 and 14 on the original code (in the camera entries); the examine camera and the opening's timeline removed from the stand-in lists; census 1.61. Before, 2026-10-02, chain step AIMLIVE's fix round: the entry "Aiming, firing, the gun lamp and the knife on the original code" (PARTIAL); the aim camera removed from the stand-in lists; the security gun entry covers the cable hit. Before, chain step AIMLIVE: no new claim).
