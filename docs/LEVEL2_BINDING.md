# AREA01 first-visit binding

The hub for connecting AREA01 (the second level, first visit) to the
running game. Goal: the player arrives from the first level's exit and
runs around AREA01 with every live behaviour proven against the PCSX2
recordings (SECOND_LEVEL_ROUTE.md; recordings in the decomp's
`build/s87/route_a01/`, the EXIT capture `exit_01`). Topic documents:
[LEVEL2_RUNTIME.md](LEVEL2_RUNTIME.md) (composition, views, lifecycle),
[LEVEL2_COLLISION.md](LEVEL2_COLLISION.md),
[LEVEL2_RENDER.md](LEVEL2_RENDER.md) (textures, packets, models, lights),
[LEVEL2_SERVICES.md](LEVEL2_SERVICES.md) (scripts, messages, doors,
pickups, Use, effects) and [LEVEL2_AUDIO.md](LEVEL2_AUDIO.md); the census
is [SECOND_LEVEL_CENSUS.md](SECOND_LEVEL_CENSUS.md). The work was done on
the branch `level2` (Codex, then Claude) and merged into main on
2026-10-04; the branch and its worktree are gone. No emulator was launched.

## State (2026-10-07, the AREA01 crash sweep merged)

Codex's crash sweep (branch level2-crash, LEVEL2_CRASHES.md) is merged
into main (merge bbc4a08) after step DRAW (merge 6c9a688). It binds water
contact and its ripple / skid / impact effects (00187DE0, 001E8B90,
001EC270, 001EB7F0, 001ECB00), fire-contact damage (001E3D20 with the
restored 0015C420 spawn store), canonical player targets and per-world
hulls (001AA4E0's class-2 entries, 00185A10 / 00183C40), room transitions
and the original interaction requests (00157F60, 001BB400 / 7C0 / 7F0,
001B1DE0), the terminal's confirmation and No (0020CDC0 request 6,
00225A00; Yes, phase 6 / 00225AC0, still faults), bug hits (001B41F0,
001ED7A0, 001FC580) and the duct's frame-counter view (001C02E0). Its
re-exports (streams, effect tables plus `export_area01_water_effects.py`,
boot scripts) are in STARTUP.md.

**Where the game stops now.** The kind-6 stop the sweep reported at
0023D930 is gone (step DRAW). On main every exploration case and recorded
replay runs without a game fault (LEVEL2_CRASHES.md "Status on main after
the merge"): a01_00 passes all 781 rows; a01_s3 (262 rows) and a01_s4
(1,225 rows, PS2 drive timing) pass the checker; the main replay plays
a01_00..a01_06 with no fault and a01_07's pad to its end (the player
drifts and does not reach the exit). The checker stops at a01_01 row 3
(the harness's first-command lag, LEVEL_SMOKE.md "a01_00"), so a01_01
onward is not yet compared row for row. Census: SECOND_LEVEL_CENSUS §12
(56 live).

## State before the crash sweep merge (2026-10-04, step DRAWN: AREA01 drawn)

**AREA01 is drawn from its original packets, and the player runs the
whole train room.** What was already connected before this step (the
dynamic table pass 001D5BD0 in the render context since the resource
checkpoint, the area texture catalog `em_world_textures_live_bind(1, 0)` at
the rebuild, the dynamic VU programs 0x237450 / 0x237720 and the
floor-field / ripple programs on the chain page, the owners' model draws
and the shadows) ran in every AREA01 frame; this step drew the last
program the route reaches, put the drawn world under the level smoke and
compared pixels:
- **Kind 6.** 001E3D90's near-fire layer (001CFBE0 kind 6, from a01_00
  f405) is drawn: its program D_0023D930 is translated
  (`em_vu1_kind6_program_mscal`), recognised by the chain page and
  exported (`assets/effect_tables.emet` window 0x0023D930 + 0xF70). `make
  test-level2-kind6-vu-reference` compares it with the original microcode
  over every captured kind-6 page and 1,200 synthetic MSCALs (full);
  LEVEL2_RENDER.md "Kind-6 near-fire program". **a01_00 now passes all 781
  rows** (`python3 tools/test_level_smoke_area01.py --until a01_00`).
- **The smoke serializer's dynamic table.** The static-world sample of an
  AREA01 frame carries the dynamic table's identity and the call's whole
  output; `check_area01` re-executes the original 001C1D00 with its
  001D5BD0 over route 15's AREA01 capture: every written byte equal. The
  shadow samples restart at the AREA01 bind and `check_shadow_area01` replays the
  player's and the owner walk's shadows over the same capture. Both run in
  every smoke that reaches AREA01 (LEVEL_SMOKE.md "The AREA01 world").
  Census: 001D5BD0, 001D5A70, 001D4FC0 live (SECOND_LEVEL_CENSUS §11).
- **Pixels.** The fb2 point 15_level_exit (the arrival frame, two neutral
  frames after f801) against the port's frame of that tick: camera exact,
  **47.41 % of the pixels exact**, mean channel error 0.67 (`make
  test-fb2-pixels-area01`; GS_EXACT.md section 10). No other AREA01 frame
  has a recorded displayed field: the route_a01 save states hold GS memory
  without the displayed buffer or its row, so comparing a later beat's
  frame needs a new fb2-style PCSX2 recording of it.

**What a player sees** (a windowed run is the same code as the smoke's:
the headless renderer draws the same Metal frame; frames captured along
a01_00 in `build/level2/drawn/`): after Roger's movie the screen fades in
on the "UNDERGROUND TUNNEL - AREA B" title over the dark tunnel wall, the
player at the door, the crates at the left and the lit control-room window
with the guard behind it at the right, his shadow at his feet. Running into
the train room: the rails and the floor fields (the splashes and the wet
footprints), the crate stack with its two fires and their glow, the
near-fire layer filling the screen as the camera closes on them, the
ledge grab, hang and pull-up onto the crates, the fall and the walk under
the girders to the tunnel mouth, the camera following throughout. Not
checked by this run: a human-driven window (the runs here are headless,
by the test rules); sound.

**Where the game stops next** (unchanged by this step): a player who
walks on through the tunnel stops (fail-stop) on the shaft landing's
floor, a01_02 f39 in the recording: the floor service 00175900 reaches
00187DE0 (the surface-0x5B first contact, unbound; it needs 001EFD90,
001E8B90 and 001FB9F0); a player who uses the control-room door stops on
its Use (a class-2 D_00275B8C entry, a01_s0 f134). For the smoke only,
a01_01's first recorded pad command arrives two frames late (LEVEL_SMOKE.md
"a01_00"), a harness reading, not a game fault.

Receipts (ignored): `build/level2/kind6/` (full.log: the kind-6 full sweep;
a01_00d.log: the a01_00 run with the AREA01 checks; area01_full.log: those
checks in full mode; chain_page_full.log; smoke-default.log;
newgame-control.log; fb2_full.log) and `build/level2/drawn/` (the frames).

## State before step DRAWN (step CAMERA: the AREA01 camera)

**The camera follows the player as recorded wherever the run reaches.**
AREA01's camera is the first level's live camera (`em_camera_live`: the
follow, leftovers, specials and commit translations) over AREA01's
collision world; no AREA01-specific camera code exists or is needed for
the first visit's mode 0. The level smoke now compares the camera block
itself, not only the eye / target and flag word: on every AREA01 row the
block's eye +0x10, target +0x20 and the forward D_00810600, and at each
recording's last row the whole block D_008101E0..+0xCF, D_00810600..0F
and D_00810690..D_008106A3 byte for byte with the recording's saved RAM
(`level_smoke_area01.camera_vectors` / `check_camera_end`; LEVEL_SMOKE.md
"a01_arrival", "a01_00"). Release build: a01_arrival passes with them (all
61 rows and the whole camera at f801); a01_00 rows f0..f404 are exact,
then kind 6 stops the run as before. A private diagnostic build that
skipped only the kind-6 draw (deleted, never committed) passed a01_00 (781
rows, whole camera at f780) and, with a01_01's first pad command submitted
two rows later, all 306 a01_01 rows with the whole camera at its end
(LEVEL_SMOKE.md "a01_00" gives the reading of that recording); a01_02 then
matched f0..f38 and stopped at f39 in the floor service (00187DE0, the
surface-0x5B first contact, is not bound). So a01_00..a01_02's camera
(mode 0, its +7 = 0x40 rows in the tunnel, the commit) needs nothing
beyond what runs; what blocks the comparison is not camera code.

Bound in step CAMERA:
- **The scene-entry tables, area-aware.** Camera mode 1 (the control
  room, D_00810700..702 = 1 / 0 / 1 after the room move) is seated by
  001B0460 (at 001B07C0's placement) and re-seated by 001B0300 (mode 1's
  path when cam+1 is 0): both walk D_0024D650[area][room] + entry * 0x30
  and, for a record whose +0x10 has bit 7, read the eye row D_0024A8D0 +
  (word +0x10 >> 8) * 12. The AREA01 live composition could read neither
  (a diagnostic probe of its byte view returned nothing for D_0024D650),
  and the eye rows were in no export (AREA11 has no mode-1 record, so the
  first level never read them). Now the composition's host view
  (`em_scene_bindings.c` area01_extra_bytes) serves, read-only, the spawn
  table window the placement already uses (its one copy, s_spawn_table)
  and the eye rows from the camera's own table export; 001B0460's reader
  (`em_camera_live.c` room_read) takes the eye rows from the same export.
  `tools/export_camera_tables.py` writes `assets/camera_eye_rows.emrg`
  (D_0024A8D0 up to the first spawn entry array 0x24AA50: 32 rows),
  checking that every mode-1 spawn record indexes a row inside it (AREA01
  room 0 entries 1 and 8: rows 2 and 6). The probe then read area 1's
  record (1, 0, 1): +0x10 = 0x280, distance -31.2, eye (110, 25, -571),
  the control-room camera block the a01_s0 recording holds (+5 = 1, +0xC =
  -31.2, eye (110, 25, -571)). Test: `make test-area01-scratch-alias` runs
  the export through the accessor and 001B0460's reader against the
  pinned ELF word for word, with the delegation to the spawn reader and
  five refusals.
- **The control-room and duct workers.** Already bound (step a2352ad):
  mode 1's 001B0300 (`em_area01_sys_001B0300`), action 10's 00198D90
  (`em_area01_room_00198D90`) and 001D2830 (`em_rcl_001D2830`) through the
  camera host callback, the duct's 0018C4B0 / 0018C6A0 through
  `em_area01_camera_services`. None is reached yet: a01_s0 (the release
  build) stops at f134 on the control-room door's Use (a D_00275B8C entry
  of class 2, "player closure: 00161020 faulted at 0x00160220"), before
  the room move at f296, and a01_s5 (the duct) starts from a01_s0's end.
  Their census rows stay verified-unbound.

Receipts (ignored): `build/level2/camera/` (smoke-default.log,
smoke-arrival.log, a01_00.log and its run, newgame-control.log, and
diag-checks.log with the diagnostic build's checks and probe).

## State before step CAMERA (step MOVE: the player runs around the train room)

**How far the player gets (step MOVE).** The opt-in phase **a01_00**
(route beat a01_00_train_room, 780 frames: walk round the crates and
through the floor fields, Use against the crate stack, grab, hang,
pull-up, walk off its south side, fall, land, walk to the tunnel mouth)
runs on from a01_arrival with the recorded pad (`python3
tools/test_level_smoke_area01.py --until a01_00`, about 2 minutes) and its
rows are compared one by one with the recording by the same strict
checker as every AREA01 route phase (`level_smoke_area01.compare_route_row`:
player, camera, requests, task, health, progress windows, message, bars,
fade and all 11 recorded owner records). **Rows f0..f404 are exact**: the
walk, the floor-field wading with its splashes and wet-feet decals, the Use
scan, the ledge grab (f304), the hang (f357) and the first 44 frames of the
pull-up. At port counter 15473 (row f405) the run stops at a fail-stop in
the presentation: the fire owner 001E3D90's third layer asks 001CFBE0 for
kind 6 once its projected size D_00275C00 exceeds 0x100 (the camera is
close to the fires on the crate stack), and kind 6's VU1 program, the DMA
packet D_0023D930 (MPGs of 256 and 130 instructions; it shares 270 of its
386 instruction slots with the sprite program D_00231770 and differs in
the emission, micro 0x10D..0x180), is neither exported nor translated, so
the chain page refuses the page ("unmapped address fault at 0023D930").
That program is the one thing missing for the whole beat: a private
diagnostic build (deleted, never committed) that skipped only the kind-6
requests ran all 780 frames and **all 781 rows matched**, the fall (f491),
landing (f519) and the walk to the tunnel mouth included, and the level
smoke checker passed a01_00 with every first-level phase and a01_arrival.
When the run stops, the tool now prints how many recorded rows were exact
before the stop ("a01_00: NOT PASSED; 405 of 781 ...", never a PASS).

Bound or fixed in step MOVE (each to its existing owner):
- `00187EC0` (001A8840's floor-field contact event (6, owner +0x56) /
  (7, 0)): `em_area01_math_00187EC0` (byte-matched C; `python3
  tools/test_area01_math_reference.py`) in the runtime dispatch; it writes the
  player record's +0x0B, the surface byte +0x23A and +0x31E through the
  player view. `em_area01_math_player.c` joins the main build.
- The splash handlers `001EAF00` / `001EAF80` / `001EB020` (D_00255434
  entries 001EA240 calls for the wading splash): em_area01_render_hud's
  translations (`python3 tools/test_area01_render_reference.py`) through
  `em_effects_live.c` handler_splash over the node's work block; their
  001CFBE0 sources D_002557D0 + 0x90 n (n = 0..5) are a new
  `assets/effect_tables.emet` window (`tools/export_effect_tables.py`;
  equal to the ELF in all 35 route, route_a01 and startup captures).
  001EAF00 runs from f138; 001EAF80 / 001EB020 are bound with it (the same
  adapter) and are first reached on route a01_02.
- The wet-feet decal of `00187EE0` (surface 0 with the wet timer +0x212
  set, after the feet left a surface-6 floor field): `x_decal` builds the
  matrix in SPR 0x700036A0 (L.foot_36A0) with 001029C0 / 00102BB0 /
  00102B08 (em_owner_services) and calls 001F0460(1, M)
  (em_effects_live_001F0460). Row 3's fourth word is the stack word
  00187EE0's three-word 001031E0 copy never writes; it is **0 in every
  wet-feet decal the recordings hold** (45 distinct 001F0460 lane-1 slots
  across the route and route_a01 captures), and the port stores that 0.
  The wading ripple 001E8B90 of 00187350 (water depth +0x23C) stays
  unbound: no floor on the route so far sets +0x23C.
- `D_0028A9A0` in the live composition's byte view: the transition
  substate (001AEE70's, held by `em_frame_transition()`), read-only; the
  Use scan 00184BA0 reads it as its fade gate (it faulted on the first Use
  press in AREA01).
- The closure's hit-record reader (`em_player_closure_live.c` last_hit):
  *(0x700031D0) + 0x1A / + 0x24.. read the cell class 0x700030CA and the
  cell normal 0x700030D4.. when the walkers left the cell record
  D_700030B0 there, as the original's loads do, not a grid node's words.
  00182250's hang alignment hits the crate stack's class-4 cell (owner
  0x7AB150, uid 0x1B) at f358; with the stale grid normal the port turned
  the player 0.1077 rad and moved it 0.16 off the ledge. Oracle check
  (scratch, over the a01_00 capture): the original 0019AD00 with that
  query returns 2, record D_700030B0, point (15.82121, 27.5, -710.90039),
  cell normal (0, 0, 1); the native walkers return the same.
- The a01 tick log's progress windows (D_008107D8..+0x3F, D_00810758..+7,
  D_00810860..+0x3F, D_00810D00..+0x1F) are composed byte by byte from
  their owners: the canonical progress region, g.opening_complete for
  D_00810811 (a named mirror), and 0 for a byte with no port storage
  (001AF2C0's reset; live code reaches the region only through
  em_scene_progress_at or a named mirror). Before, a window with any
  non-canonical byte logged null and the route checker could never pass.

The segment walker's no-span arm (0019D770, camera queries): not reached
in any a01_00 frame (the diagnostic run went through all 780 frames
without that fault). Its original start / end / column registers are
unassigned on that arm (LEVEL2_COLLISION.md), so there is no original
value to substitute; the native fail-stop stays.

## State before step MOVE (step GUARD: the AREA01 guard is open)

**How far the game gets.** New Game plays the whole first level into
AREA01 and the player arrives there in every run (not only under a test
switch): the 0x1AE040 guard that stopped every AREA01 frame after the
arrival's rebuild is gone, and AREA01's live composition binds at its
rebuild in every run. `make test-level-smoke-full` now ends with the phase
**a01_arrival**: the rebuild (route 15 row 741, native counter 15007) and
**60 neutral world frames compared row for row with route 15 f742..f801**
(player state, pose, clock, ground, position and heading; camera eye,
target and flag word D_008101E4..E7; selector, request, area and task
bytes; story and progress bytes; message, screen, bars, power and fade;
`tools/level_smoke_area01.py check_arrival`). All 20 main-route phases pass
(first_control .. exit, a01_arrival), and the AREA01 frames' rand() draws go
through the RNG audit with every native caller named (`tools/rand_order.py`;
AREA01 has no per-call capture, so no per-call equality is claimed). An
AREA01 original without an owner still faults where it is reached: the
runtime's unknown-worker arm, the collision passes' unported pairs, a
missing byte view. `EM_LEVEL2_BINDING_PROBE` no longer exists;
`EM_A01_ARRIVAL_TICKS=N` (60..600) still lengthens the idle as a
diagnostic (frames past f801 are not recorded, so they prove nothing).
Receipts (ignored): `build/level2/guard/smoke-full.log` (`make
test-level-smoke-full`: the main route through a01_arrival and every side
run, rc 0), `smoke-default.log`, `newgame-control.log` (9.599849).

Bound in step GUARD (2026-10-04):
- `001AF690` at its 001AFCA0 position (after 001AF5C0) in `w_001AFCA0`:
  the existing `em_slg_001AF690` over a staging image of
  D_00810130..D_008102AF and D_0081060C, its camera half (the camera block
  D_008101E0..+0xCF and D_0081060C) stored into the camera's canonical
  storage by the new `em_camera_live_store_block`. The port had never
  cleared the camera block at a rebuild, so the AREA01 world's first camera
  frame ran 0018B9C0's state 1 (the follow camera, flag word 0) where the
  original runs its one-shot state 0 (+6 = 8 outside area 0x12 sub 0,
  then 0018C0C0 / 0018D7B0(1) / 0018C0D0(1)): route 15 f742 has
  D_008101E6 = 8 and the eye / target of the rebuild. The other two
  blocks start at the same zeros through their owners: D_008101D0 the
  render context's bind, D_00810130 the status runtime (created zeroed when
  the area's interaction host loads). The first level's rebuilds take the
  same reset; its whole route still passes.
- `001AA000` (001AA140's class-2 pairs in the close-out, call site
  0x1AA23C, reached on the third AREA01 world frame as in the original's
  census, c16505): the existing oracle-tested `em_area01_sys_001AA000`
  (`make test-area01-sys-reference`) through the collision world's AREA01
  pair binding (`EmCollisionWorldAreaPasses.pair` now carries a2 / a3 =
  a + 0x1F0 / b + 0x1F0); the halfword 0x70003B86 it may clear is published
  back to 001AA140's loop. It reads the radius / height pair D_00275668
  that 001289C0 stores at its owners' +0x30 (that routine is its only
  reference): a new immutable `assets/effect_tables.emet` window
  (`tools/export_effect_tables.py`, equal to the ELF in all 17 default
  captures and all 16 route_a01 captures).
- The AREA01 rand() forwarders and callers in `tools/rand_order.py`
  (00122BB8 adapters of the composition, and the callers 001E8E80,
  001E9580, 001F4BF0, 001F4A10, 001E7D20, 001E3D90, 0015A2C0 and AREA01
  00826D40).

Measured on the default path (a private diagnostic build that logs every
call through `em_area01_runtime_call` and the composition's worker; never
committed): 41 AREA01 census rows run in the 61 compared ticks
(SECOND_LEVEL_CENSUS.md, now live), each first reached on the same frame as
in the original's EXIT census where both record it. Same-module calls inside
a translation do not pass that funnel, so 23 further rows the original runs
in the window (e.g. 001C4FA0 inside 001C50B0, 001D0C80 / 001D0D40 inside
001C02E0, the dynamic pass 001D5BD0) are not counted.

Bound in step FRAMES (2026-10-04, each to its existing owner unless
stated):
- Character-bank textures: `tools/export_area01_world_textures.py` now
  collects the resource-table models AREA01 owners bind at +0x44
  (D_0028A490[id]: sector-3 files `chunk03/fNN_idXX.bin`, resident from
  0x10E99C0 in file order, checked byte-equal in every capture; others in
  the area load map): 00128C10's 0x0F, 001BFFD0's 0x20, 001C02E0's face
  resource 0x22 and the NPC 00825350's 0x47; the face 001BA8E0 binds for
  the NPC's type 0x47 (D_0028A490[0x88], func_001BA8E0.c); and the TCC 0
  words of 001E9E60 (floor fields) and 001E7D20 (ripple surface). 460
  keys, disc residency and all sixteen AREA01 GS freezes exact
  (LEVEL2_RENDER.md "AREA01 world texture delivery").
- The floor-field program D_002345E0 (001E9E60, 0015A2C0's 8 fields) and
  the ripple program D_00234B00 (001E7D20): new VU1 translations
  `em_vu1_floor_program_mscal` / `em_vu1_ripple_program_mscal`
  (em_vu1_page_programs.h) and the chain page's walk of their packets
  (MPG recognition, TOPS-relative batches, MSCNT after a batch of their
  own). Oracle: `make test-level2-floor-vu-reference` runs the original
  microcode (LEVEL2_RENDER.md "Floor-field and ripple programs"). The
  packets and 00128C10's clip-id table D_00242F20 are new
  `assets/effect_tables.emet` windows (re-export).
- The page pixel path draws TCC 0 MODULATE (Af = Av), the floor and
  ripple textures' form (Metal; `make test-chain-page-gpu` has 3 new pixel
  cases and a TCC 0 HIGHLIGHT refusal).
- `0012D580` (00128C10's state 8 sub-machine): `em_area00_low_0012D580`
  (oracle `make test-area00-low-reference`), which gained an optional
  per-access `view` after its regions; the AREA01 binder passes the live
  composition's checked byte views and re-enters the runtime for callees.
- `001F9100` from 001B5360 (that owner's ground decal):
  `em_shadow_actor_route_001F9100` through the new
  `em_shadow_live_owner_001F9100` (the same decal kernel; its fan
  triangles are added to the page's decal count).
- `001C9D50` (00128C10's pose blend): `em_anim_rest_001C9D50` over the
  composition's scratch 0x700034C0.. and 0x70003760.
- `001B12B0`: the first level's approach step `em_script_host_approach`.
These four adapters run in the arrival idle (step GUARD: the default path); their owners are
the oracle-tested translations named, but no AREA01 oracle test covers
the adapters themselves (as for 001B1B30).

Bound in the previous step (each to its existing owner):
- `00102798` (SDK 4x4 transpose, 00128C10's matrix) in the shared SDK
  memory adapter `em_aim_fire_sdk_memory.c` over
  `em_camera_commit_00102798`; the four row loads precede the four
  stores. Original-instruction proof: `make
  test-aim-fire-sdk-memory-reference` now runs the original 00102798
  (16 entries, 598 overlap cases, in-place and misaligned).
- `0x700034C0..FF`, 001C3DB0's product matrix (00129780's turn), as the
  live composition's write-first scratch (`EmArea01Live.scratch_34C0`);
  001C3DB0's first 001026D0 writes it before the second reads it.
- `001B1B30` / `001B1630` (the shaft door 00823580's visibility publish):
  the first level's door owner `em_sdf_001B1B30` storing straight into
  the actor's +0x01 before `001B1B70`, over the camera cone owner
  `em_area11_interaction_host_visible_001B1630`.
- `001F4A10` from 00158D30 / exitb: the third argument lane is not an
  input (the original writes `$a2` at 0x001F4A70 before any read).
- Head sprites of AREA01 owners (`001E2560` with an 00825350 / 00825740
  owner): `em_effects_live_set_head_owner` gives the effect owner the
  AREA01 record image and slot arena. The talk owner 00825740 frees
  itself on the first visit while its head still ticks; the head reads
  the freed record's +0x01 / +0x02 / +0x220, all cleared by 001AFC10, and
  ends (lifecycle 3), so the pool's image of the freed record is exact
  for every byte it reads.
- `001A7870` in the close-out reads a listed owner's +0x58 hull record in
  the delivered area file while the collision view is committed: the
  view now serves the immutable file bytes outside the cell directory
  between transactions (read-only; cells, mirror and scratch still
  refused; `make test-area01-collision-view-reference` checks it).
- The AREA01 texture catalog gained the global library models that
  AREA01 owners bind (409 keys then; 460 since step FRAMES).

**What is missing, in dependency order** (sizes are estimates):
1. Originals the idle run does not reach stay unbound and fault when
   reached (e.g. 001F9180, 001B5360's decal for byte +3 = 4 or 8); the
   arrival's run is idle only.
2. Owner bytes in the arrival idle: route 15 records no AREA01 owner, so
   the 60 compared frames check the player, camera and progress, not the
   owners' records (the a01_00.. captures record 11 owners; their beats are
   item 4 onward). Small.
3. World drawn: the frames' pages (dynamic programs
   0x237450 / 0x237720, the floor and ripple programs, object units) are
   drawn, but no screenshot or pixel comparison exists; the smoke
   serializer omits the dynamic table; owner render hooks and the AREA01
   player shadow are unproven. Large (verification-heavy).
4. Movement and collision (step MOVE): route a01_00 is exact through
   f404 and, with only kind 6's draw skipped, through all 780 frames. The
   next item is kind 6's VU1 program D_0023D930 (export the packet, add
   its MSCAL translation beside the sprite program in
   em_vu1_page_programs.h, the chain page's recognition and an oracle
   test with tools/chain_page_model.py VuOracle), then a01_01 / a01_02.
   Medium.
5. Camera (step CAMERA): the scene-entry tables are area-aware and the
   control-room / duct workers bound; mode 1 and the duct are reached only
   through the control-room door (item 6: a01_s0 stops at f134 on its
   class-2 Use entry). For a01_01 / a01_02: the recording's first a01_01
   pad command reached the original two frames late (a reading of the
   recording, LEVEL_SMOKE.md "a01_00"; the harness timing is unchanged),
   and a01_02 f39 needs 00187DE0. Small.
6. Doors, NPC scripts, messages and pickups: beats a01_01..a01_07 and the
   8 side routes; adapters exist, unverified live. Large in total.

**Local exports AREA01 needs** (ignored, from the user's disc/ELF; see
STARTUP.md): `assets/area01_world_textures.emot`
(`tools/export_area01_world_textures.py`),
`assets/area01_shadow_receivers.emsr` (`tools/export_area01_shadow_receivers.py`),
`assets/area01_boot_scripts/` (`tools/export_area01_boot_scripts.py --out
assets/area01_boot_scripts`), and re-exports of
`assets/effect_tables.emet`, `assets/scene_snow/roger/resources.emrs` and
the SFX registry. The area load binds the texture catalog and shadow
receivers, so the AREA01 rebuild faults without them.

The a01_arrival phase is the full route's last phase (`make
test-level-smoke-full`; `EM_LEVEL_SMOKE_UNTIL=a01_arrival make
test-level-smoke` alone, about 2 minutes). Its checker compares the
existing tick-log fields with route 15 rows 741–801; it does not claim to
compare owner bytes absent from that recording. The AREA01 route beats
after it (a01_00 .. a01_07, the side beats) stay opt-in.

## Census and dependencies

See [SECOND_LEVEL_CENSUS.md](SECOND_LEVEL_CENSUS.md): the current 174-row
`a01_delta.json` and the 62 post-arrival rows in FIRST_LEVEL_CENSUS section
3.26 have **179** distinct region-qualified entries, **29,547** instructions.
Overlay identity matters: AREA01 `00823580` is not AREA11 `00823580`.

At the mapping checkpoint: 0 AREA01-live, 172 verified-unbound, 5 missing,
2 boundary. After the prerequisite translations: **0 live, 177
verified-unbound, 0 missing, 2 boundary**. First-level shared-live evidence
is recorded separately; it does not prove an AREA01 adapter is bound.
The five initially omitted arrival routines were `001C4FA0`, `001C50B0`,
`001D0C80`, `001D0D40`, AREA01 `00825740`.

[SECOND_LEVEL_CENSUS.md ("AREA01 arrival binding dependencies")](SECOND_LEVEL_CENSUS.md#area01-arrival-binding-dependencies) records original callers,
existing owners and the canonical-state mapping work still required.
[LEVEL2_COLLISION.md ("AREA01 collision prerequisite audit")](LEVEL2_COLLISION.md#area01-collision-prerequisite-audit) records why the segment walker's
no-span refusal must not be replaced by a made-up return value. Actor-cell
bit 29 is already accepted by the baseline.

## Binding ledger

No new AREA01 behaviour has been bound at the phase-1 checkpoint. Existing
arrival bindings are documented in FIRST_LEVEL_EXIT section 7. The call
chain after arrival is `001ACEC0 -> 001AD250 -> 001AD4D0 -> 001AE040`, then
the classifier and `001AE5E0` / `001AE6B0`. The guard prevents reaching
unbound actors, rendering and interaction services.

The first phase-2 prerequisite checkpoint supplies the missing translations
without changing that guard:

- `001C50B0`, placed by AREA01 roster record 40, calls its predicate
  `001C4FA0`; new `em_area01_light_owner` uses the existing math address
  contract and canonical workers. Quick **161 cases / 216 worker
  boundaries**; full **2,309 / 2,536**, plus six fail-stop contracts.
- `001C02E0` state 0 now calls its same-module `001D0C80` and `001D0D40`.
  Quick **40 / 21 helper cases**, caller **57 cases / 363 boundaries**;
  full **48,851 helper cases**, caller **1,606 / 7,656**. The entire math
  suite passes; two pre-existing unrelated branch outcomes remain
  uncovered. See LEVEL2_RUNTIME.md ("AREA01 bone-slot initialization helpers").
- AREA01 roster record 38's `00825740` runs setup before its first-visit
  teardown gate. Its standalone owner has **184 quick / 1,131 full
  cases**; full overlay suite **14,034 cases / 21,199 executions**, all
  15 entries. See LEVEL2_RUNTIME.md ("AREA01 placement [38]: talk owner 0x825740").

These are translation results, not AREA01 route or live-worker evidence.

The message resource adapter now runs during `001AFCA0`'s native area
rebuild. It selects the delivered area's EMMD view for `001FD790` and
`001FD950`, preserving the request block, draw/glyph state, styles, streams
and presenters. `001FC9B0` keeps its original reset sites. The original
service caller is `001FCA10 -> 001FDB80 -> 001FD790`; AREA01 dialogue itself
is still unbound. See LEVEL2_SERVICES.md ("AREA01 message-bank binding"): **4 state-preserving selections,
11,956 capture-equal bank bytes, 54 quick / 3,330 full original service
ticks**, plus missing-bank and persistent-fault checks. The fixture's
synthetic draw/glyph state is distinguished from its recorded inputs.

The math storage contract now accepts direct views of canonical owners,
with no full-RAM arena or fallback for missing spans. Its sanitizer alias,
bounds and fail-stop checks pass; the complete math and light original-code
oracles pass again in quick and full modes. LEVEL2_RUNTIME.md ("AREA01 math workers over canonical memory views") separates
the new adapter checks from the existing linear-memory oracle evidence.
The future actor binder still has to supply those views.

The resource checkpoint connects three adapters during the live state-0
rebuild, while retaining the world-frame guard:

- After pool/model-owner reset, select `D_0028A59C` from the loader's slot
  0x43 through the existing world-model owner. The EMWM table must match
  the delivered address. LEVEL2_RENDER.md ("AREA01 world model bank selection") proves **1,664 lookups,
  832 owner initializations and 1,362 bone records** in full mode. Direct
  AREA01-to-AREA11 without module-3 reload relocates that bank in both the
  original and native loader; the fixed-address export correctly refuses
  it. That additional transition is not claimed supported.
- Bind RCL for both world areas. AREA01 keeps its delivered static bank
  and borrows dynamic table slot 0x45; it does not reload AREA11's static
  export. The existing `001D5370` dispatch reaches the prepared dynamic
  packet workers and existing depth/page owners. Full standalone
  composition: **16 snapshots, 276 entries, 2,278 tracked original calls**.
  First-level RCL regression: **15 beats / 1,335 entries**. See
  LEVEL2_RENDER.md ("AREA01 dynamic packet binding") for the remaining VU/pixel limits.
- After `001F0310`, run AREA01's `001E7780 -> 00823A50` initialization
  before the spawn calls. Six globals are owned by `EmArea01State`; data
  and BSS alias the loader. Full initializer oracle: **416 cases,
  570,775,296 state bytes**, plus the original BSS-clear proof. See
  LEVEL2_RUNTIME.md ("AREA01 canonical overlay state and initialization"). Rebuilds do not clear the whole overlay again.

The loader-to-model test exposed an existing `region_for` bug: a shorter
same-base read retained the old allocation extent, so a later overlapping
read could discard still-resident AREA01 top resources. The replacement
preserves untouched prefixes/suffixes and gives each read its exact
extent. Full loader regression passes **5,544 read cases and 45 additional
whole modules**, plus its existing page, GS and sanitizer checks.

The live resource probe still reaches the rebuild at counter **15007** and
then the intentional `001AE040` world-frame fault. The new first-control
probe passes **1,301 locked ticks, zero motion, 30 move ticks, 9.599849**.
Receipts: `build/level2/resource-arrival/` and
`build/level2/resource-newgame-control.log`. No AREA01 actor frame or
dialogue is claimed by these resource checks.

## Verification

- Initial `make all`: passed, zero compiler warnings.
- `EM_STARTUP_TEST=newgame-control`: passed; 1,301 locked ticks, zero locked
  motion, 30 move ticks, displacement **9.599849**, census 49.
- Resource-checkpoint first-level main smoke: **19 live phases through exit**,
  capture checker passed with the matching indexed binary. Measured-drive
  smoke also passed through Roger. The panel/no-battery, status pages,
  fence-side-1, all **11 aim**, all **3 damage** and all **10 branch** side
  runs passed. Recursive make initially tried to rebuild
  from concurrent unstaged work; that link failure was isolated from the
  completed gameplay runs. Receipts: `build/level2/resources/main-check.log`,
  `ps2-drive-smoke.log`, `side-smoke.log`, and `remaining-smoke.log`.
- Ten AREA01 quick oracle suites passed. Exact counts and receipts are in
  SECOND_LEVEL_CENSUS. The existing-render suite initially failed to link
  the point-light module's shared matrix workers; adding its existing
  `em_owner_services_original.c` dependency fixed the harness.
- The 277 baseline `make test-*` targets have per-target results in
  `build/level2/verification/results.json`. Sandboxed native
  GPU tests cannot create Metal devices; these require a headless run with
  normal host access. That infrastructure failure is not a game pass.
- The initial 277-target sweep completed: 255 passed and 22 failed in
  the sandbox. Fifteen failed targets then passed with normal headless
  host access, including all GPU pixel checks, cutscene skip, first
  control and message capture. **270/277 distinct baseline targets have
  passed**; the seven smoke targets are being completed separately.
  The full main/panel/status/fence/11-aim components passed before a
  concurrent source addition exposed a missing application link entry at
  the next rebuild. The source list is repaired; this interrupted full
  invocation is not counted as a full-suite pass.
- Missing-worker staged-index build: `make -B all`, zero warnings,
  receipt `build/level2/prerequisites-index-build.json`.

The resource staged-index build passed `make -B all` with **zero warnings**
(`build/level2/resources/index-build.json`). Completed large trace receipts
are preserved as gzip files; `build/level2/compressed-receipts.json` lists
the paths. The resource checkpoint retains the AREA01 world-frame guard.

## Shared-file edits

Phase 1:

- `src/game/em_level_smoke_test.c`: opt-in arrival idle phase; explicit
  first-level default endpoint remains `exit`.
- `tools/test_level_smoke.py`: dispatch the AREA01 checker, preserve exit
  checks on longer logs, and keep `last` meaning the first-level endpoint.
- `tools/test_area01_render_existing_reference.py`: link the matrix-worker
  dependency already used by the primary effect oracle.
- `tools/test_coll_segment_walkers_reference.py`: AREA01 route bounds and
  original-selector evidence, without changing the game walker.

New phase-1 files: this document, SECOND_LEVEL_CENSUS, SECOND_LEVEL_CENSUS.md ("AREA01 arrival binding dependencies"),
LEVEL2_COLLISION.md ("AREA01 collision prerequisite audit") and `tools/level_smoke_area01.py`.

Phase-2 missing-worker checkpoint:

- `em_area01_math_owner.c/.h` and its existing oracle: two missing helpers
  and direct calls from their existing owner; no live host added.
- `em_area01_overlay.c/.h` and its existing oracle: roster record 38's
  missing owner; no live hook table added.
- New `em_area01_light_owner.c/.h`, its oracle and the three evidence
  documents LEVEL2_RUNTIME.md ("AREA01 flicker-light owner"), LEVEL2_RUNTIME.md ("AREA01 bone-slot initialization helpers") and LEVEL2_RUNTIME.md ("AREA01 placement [38]: talk owner 0x825740").
- SECOND_LEVEL_CENSUS and SECOND_LEVEL_CENSUS.md ("AREA01 arrival binding dependencies") record availability; neither
  first-level census nor first-level audit was edited.

Message-resource checkpoint:

- `em_message_live.c/.h`: area selection preserves dynamic service state;
  the EMMD area index is checked against the actual 23-element array.
- `em_scene_bindings.c`: select the AREA01 or AREA11 message resource in
  the area rebuild, before owners can request a message.
- `Makefile`: `test-message-area-reference` target. FIDELITY_FEATURES
  describes the arrival-only state; no launcher option was added.
- New `tests/message_area_bridge.c`, `tools/test_message_area_reference.py`
  and LEVEL2_SERVICES.md ("AREA01 message-bank binding").

Math-view checkpoint:

- `em_area01_math_core.c/.h`: optional direct memory resolver, preserving
  the existing linear oracle mode and fault/store-trace contracts.
- `tools/test_area01_math_reference.py`: appended ctypes view field.
- `Makefile`: native math-view contract and light-owner oracle targets.
- New `tests/area01_math_views_test.c` and LEVEL2_RUNTIME.md ("AREA01 math workers over canonical memory views").

Area-resource checkpoint:

- `em_scene_bindings.c`: bind AREA01 render resources; select the world
  model bank after owner reset; initialize and detach canonical AREA01
  overlay state at the original lifecycle points.
- `em_area11_boxes.c/.h`: transactional world-bank selection and
  nonallocating owner metadata projection, preserving the shared stack.
- `em_module_loader.c/.h`: bounded mutable view and interval-preserving
  read allocations; no replacement loader state machine.
- `em_render_context_live.c/.h`: dynamic table views and dispatch through
  existing packet/depth/page translations.
- `tools/test_render_context_live_reference.py`: existing dependency
  sources required by the composed packet adapter.
- `tools/test_level_smoke.py`: random-call symbolication now honors
  `EM_LEVEL_SMOKE_BIN`, like the other smoke scripts, so an isolated binary
  is checked against its own symbols.
- `Makefile`: compile existing AREA01 VIF and the new state provider;
  add state, bank and composed-render oracle targets.
- New AREA01 state source/header, three reference suites, two test bridges,
  and LEVEL2_RUNTIME.md ("AREA01 canonical overlay state and initialization"), LEVEL2_RENDER.md ("AREA01 world model bank selection"), LEVEL2_RENDER.md ("AREA01 dynamic packet binding").
- LEVEL2_COLLISION.md ("AREA01 collision prerequisite audit") records the completed full sweep; no collision game
  code changed.

Crash sweep (branch level2-crash, merged 2026-10-07; the complete,
corrected list with what each edit does is LEVEL2_CRASHES.md "Complete
shared-file edit list"):

- Shared files that also run in AREA11: `em_player.c` (0015BCF0's +A0 /
  +B0 publication, 0015C420's spawn store), `em_player_closure_live.c/.h`
  (class-2 targets, water contact, 00188610), `em_collision_world.c/.h`
  and `em_actor_collision.c/.h` (per-world hull provider, 001B1DE0 push),
  `em_panel.c/.h` (one 00157F60 owner), `em_status_page.c/.h` and
  `em_area11_interaction_host.c` (request 6), `em_effects_live.c`
  (counted gap removed, impact handlers), `em_aim_fire_runtime.c` and
  `em_aim_fire_world_live.c/.h` (target reads, bug-hit views and cue),
  `em_frame.c/.h` (const counter view). On main after the merge the
  first level's main route passes (every phase, checked by the a01_00
  run); the review passed the damage, status-page, panel and branch side
  runs on the branch.
- `em_scene_bindings.c/.h`, `em_scene_state.h` (D_00810040[0xD4],
  D_00810710..2F), `em_level_smoke_test.c` (the opt-in exploration
  hooks), the Makefile (four sources, the exploration and 16 reference
  targets) and the exporters `export_streams.py`,
  `export_area01_boot_scripts.py` (+ new `export_area01_water_effects.py`).

## Known gaps

Current (2026-10-07): a01_arrival, a01_00, a01_s3 and a01_s4 are compared
row for row and pass; the other main and side beats run natively without
a game fault but are not yet compared (the a01_01 first-command lag stops
the checker; a01_07 drifts before the exit; see "State"). AREA00 arrival
is the intended stopping boundary. Conditional branches no run reaches
still fault: LEVEL2_CRASHES.md "Static first-visit inventory". (Phase-1
wording, kept for the record: all beats were then unplayed, and only the
arrival idle, route 15 f741..f801, was compared.)
The extraction resident-offset label shift is not fixed; the decomp's
`tools/extract_data.py` is outside the allowed decomp edit scope. Any
source correction there must be reported in permitted docs, not applied.
Decomp documentation commit `f5a27bc` records the proven `00219550`
child-spawn argument order at `002196A8`; only FINDINGS and NEARMISS were
changed after an empty-index check and staged leak scan. No decomp source
was changed.

## Integration checkpoint (committed 2026-10-04)

The resource binary has now passed all first-level smoke components: main
**19 phases**, **3** panel/status/fence runs, **11** aim, **3** damage,
**10** branch, plus measured-drive smoke. The interrupted aggregate make
command is not presented as a successful aggregate invocation; it will be
run again after the next live integration checkpoint.

The composition, actor/player/collision/door/model adapters and native
texture/dynamic-VU integration are committed but stay behind the probe;
see LEVEL2_RUNTIME.md, LEVEL2_RENDER.md and LEVEL2_SERVICES.md.
The diagnostic-only AREA01 binding probe does not change the live census
status or remove the ordinary frame guard.

## Integration topics and commits

The integration was committed on 2026-10-04 as one commit per topic below,
each building with zero warnings, then merged into main. The group texts
were written before the files listed under "Additional integration edits"
existed; those went into: shared services, director modes and script-host
store callbacks -> scripts `3741895`; lights, the channel-3 matrix
service, props and the equipment proof -> models `aabcb60`; RCL workers
and scratch views -> dynamic packets `597453e`; the crate registry ->
views `1026855`; flames, glow and weather packets -> effects `6c9a5ba`;
the SFX loop and bank callbacks -> audio `f57fad3`; camera leftovers and
commit resolvers -> camera `a2352ad`; progress bytes 00810767 / 0081080F,
`condition_canonical` and the matrix-scratch proof -> composition
`04798af`. The commit is authoritative for exact files. A listed
implementation or oracle is not a live census promotion; no AREA01 main
or side route has passed the native checker.

### Composition, dispatch and lifecycle

Commit: `9806573` (dispatcher), `04798af` (live composition and scene attachment).

The diagnostic AREA01 composition resolves original addresses into existing
actor, player, scene, loader and service owners. Worker calls publish and
refresh the active views, preserve original stack boundaries, and fail on
unhandled entries. Scene integration attaches it at the original rebuild and
roster lifetime points, routes camera/Use/native-owner callbacks, and retains
the ordinary frame guard. Companion allocation rebinding remains part of the
guarded probe work. The scene logger supplies read-only owner observations
and actual post-frame loader tails. The build adds the corresponding sources
and scoped verification targets.

Existing files: `Makefile`, `src/game/em_scene_bindings.c`.

New files: `src/game/em_area01_live.c`, `src/game/em_area01_live.h`, `src/game/em_area01_runtime.c`, `src/game/em_area01_runtime.h`, `src/game/em_area01_runtime_dispatch.inc`, `src/game/em_area01_runtime_overlay.inc`, `src/game/em_area01_scene_view.c`, `src/game/em_area01_scene_view.h`, `tests/area01_runtime_bridge.c`, `tools/test_area01_runtime_reference.py`.

Evidence and limits: [LEVEL2_RUNTIME.md ("AREA01 composition and live binding probe")](LEVEL2_RUNTIME.md#area01-composition-and-live-binding-probe).

### Actor/player views and existing model-slot ownership

Commit: `1026855`.

Actor records combine native fields, retained original-only bytes and
existing shared model fields under generation-checked begin/commit/refresh
transactions. Player records publish position, vitals, action state and
validated owner links through the existing player owner. Box services expose
001B0EA0 and their existing metadata/bone-slot storage, including typed/raw
slot synchronization. These adapters introduce no second pool, pose stack or
model allocator.

Existing files: `src/game/em_area11_boxes.c`, `src/game/em_area11_boxes.h`, `src/game/em_player.c`, `src/game/em_player.h`.

New files: `src/game/em_area01_actor_view.c`, `src/game/em_area01_actor_view.h`, `src/game/em_area01_player_view.c`, `src/game/em_area01_player_view.h`, `tests/area01_actor_view_bridge.c`, `tests/area01_player_view_bridge.c`, `tools/test_area01_actor_view_reference.py`, `tools/test_area01_player_view_reference.py`.

Evidence and limits: [LEVEL2_RUNTIME.md ("AREA01 canonical actor view")](LEVEL2_RUNTIME.md#area01-canonical-actor-view), [LEVEL2_RUNTIME.md ("AREA01 external player record segments")](LEVEL2_RUNTIME.md#area01-external-player-record-segments).

### Authoritative borrowed-memory contracts

Commit: `3f7ea5b` (the 0011E520 view; the other contracts were committed before the merge `6a4ecfe`).

SYS, EXITA, EXITB, ROOM, SIDE, overlay and shared render/UI/FX accessors accept an
optional authoritative resolver with exact read/write intent. Refused spans
fault rather than falling back to an array; the existing array-backed oracle
mode remains available. The Python ctypes layouts and reusable view fixture
are updated to match the appended contract. The status-page initializer uses
designated fields for the extended render-world structure. The aim-world
caller retains its array mode with an explicit null resolver, and the existing
AREA02 miscellaneous SDK owner exposes call-local canonical access for
0x11E520 without changing its region-backed ABI.

Existing files: `src/game/em_aim_fire_world_live.c`, `src/game/em_area00_fx_internal.h`, `src/game/em_area01_exita.c`, `src/game/em_area01_exita.h`, `src/game/em_area01_exitb.c`, `src/game/em_area01_exitb.h`, `src/game/em_area01_overlay.h`, `src/game/em_area01_overlay_internal.h`, `src/game/em_area01_render_mem.h`, `src/game/em_area01_room.c`, `src/game/em_area01_room.h`, `src/game/em_area01_side.c`, `src/game/em_area01_side.h`, `src/game/em_area01_sys.c`, `src/game/em_area01_sys.h`, `src/game/em_area01_ui_internal.h`, `src/game/em_area02_misc.c`, `src/game/em_area02_misc.h`, `src/game/em_status_pages_live.c`, `tools/test_area01_exita_reference.py`, `tools/test_area01_exitb_reference.py`, `tools/test_area01_overlay_reference.py`, `tools/test_area01_render_reference.py`, `tools/test_area01_room_reference.py`, `tools/test_area01_side_reference.py`, `tools/test_area01_sys_reference.py`.

New files: `tests/area01_memory_view_bridge.c`, `tools/area01_reference_view.py`, `tools/test_area01_memory_view_reference.py`.

Evidence and limits: [LEVEL2_RUNTIME.md ("AREA01 canonical memory callbacks")](LEVEL2_RUNTIME.md#area01-canonical-memory-callbacks).

### Collision records, scratch and original table correction

Commit: `c3ae737`; the class-7 base correction is its own commit `e2e0d23`.

The collision view exposes existing world/probe/results and exact scratch
spans to original callers. The actor-cell API adds bounded primitive lookup
and a ground-query entry that preserves the shared probe state; original
record/node/span/result writes are retained. Probe/face scratch keeps the W
lanes needed by full-quadword consumers. The class-7 table base is corrected
to the original 0x28AC30. The segment walker's no-span refusal is unchanged.

Existing files: `src/game/em_actor_collision.c`, `src/game/em_actor_collision.h`, `src/game/em_coll_probe_original.h`, `src/game/em_coll_segment_walkers.h`, `src/game/em_collision_world.c`.

New files: `src/game/em_area01_collision_view.c`, `src/game/em_area01_collision_view.h`, `tests/area01_collision_view_bridge.inc`, `tools/test_area01_collision_view_reference.py`.

Evidence and limits: [LEVEL2_COLLISION.md ("AREA01 borrowed collision boundary")](LEVEL2_COLLISION.md#area01-borrowed-collision-boundary), [LEVEL2_AUDIO.md ("AREA01 positional audio and shared scratch")](LEVEL2_AUDIO.md#area01-positional-audio-and-shared-scratch).

### Camera and cross-owner scratch lifetime

Commit: `a2352ad`.

The existing camera forwards AREA01 control-room/duct workers through an
optional host callback with publication and reload around nested calls.
Camera, aim and RCL can borrow canonical scratch; detach preserves the last
writer's bytes. Camera segment callbacks publish/reload around collision.
The AREA00 readiness predicate now follows its actual subarea/story guard,
so the authorized sub-0 arrival is not rejected by an unreachable worker.
New narrow XYZ services reuse the existing camera-follow originals; their
alias/bounds contracts and native scratch handoff are tested.

Existing files: `src/game/em_aim_fire_runtime.c`, `src/game/em_aim_fire_runtime.h`, `src/game/em_camera_area11_specials.c`, `src/game/em_camera_live.c`, `src/game/em_camera_live.h`, `tools/test_camera_area11_specials_reference.py`.

New files: `src/game/em_area01_camera_services.c`, `src/game/em_area01_camera_services.h`, `tests/area01_camera_services_bridge.c`, `tests/area01_scratch_alias_test.c`, `tools/test_area01_camera_services_reference.py`, `tools/test_area01_scratch_alias.py`, `tools/test_area01_scratch_views.py`.

Evidence and limits: [LEVEL2_RUNTIME.md ("AREA01 camera worker binding")](LEVEL2_RUNTIME.md#area01-camera-worker-binding), [LEVEL2_AUDIO.md ("AREA01 positional audio and shared scratch")](LEVEL2_AUDIO.md#area01-positional-audio-and-shared-scratch).

### Dynamic packet presentation and shared VU kernels

Commit: `597453e`.

AREA01 0x237450/0x237720 select parameterized versions of the existing
level/box-clip kernels. Chain-page processing admits their VIF continuation,
TOPS-relative buffers, emitted GIF packets and supported GS state in the
existing primitive order; unknown program/state remains a fault. RCL's
scratch view can borrow the canonical matrix owner. The effect exporter adds
the two microprogram windows and six immutable interaction descriptors;
exports remain local and ignored. Existing chain-page proof is updated for
the appended counter fields.

Existing files: `src/game/em_chain_page.c`, `src/game/em_chain_page.h`, `src/game/em_chain_page_live.c`, `src/game/em_chain_page_live.h`, `src/game/em_render_context_live.c`, `src/game/em_render_context_live.h`, `src/game/em_vu1_level_kernel.h`, `src/game/em_vu1_shadow_clip.h`, `tools/export_effect_tables.py`, `tools/test_chain_page_reference.py`.

New files: `tools/test_level2_dynamic_vu_reference.py`.

Evidence and limits: [LEVEL2_RENDER.md ("AREA01 dynamic VU programs")](LEVEL2_RENDER.md#area01-dynamic-vu-programs), [LEVEL2_RENDER.md ("AREA01 dynamic packet binding")](LEVEL2_RENDER.md#area01-dynamic-packet-binding), [LEVEL2_SERVICES.md ("AREA01 canonical Use and shared status services")](LEVEL2_SERVICES.md#area01-canonical-use-and-shared-status-services).

### Area texture selection and invalidation

Commit: `3b0bef9`.

A single world catalog selects textures for the delivered area and resource
epoch. Object and chain-page consumers use that same catalog. The graphics
contract adds world-TEX0 registry invalidation, implemented by Metal; the
other existing backend stubs explicitly refuse it. The AREA01 exporter
collects the required world/owner/shared texture sources and validates the
local output. This changes resource selection, not framebuffer presentation
or UI/background texture ownership.

Existing files: `src/em_gfx.h`, `src/gfx/d3d12/em_gfx_d3d12.c`, `src/gfx/metal/em_gfx_metal.m`, `src/gfx/vulkan/em_gfx_vk.c`.

New files: `src/game/em_world_textures_live.c`, `src/game/em_world_textures_live.h`, `tests/world_textures_bridge.c`, `tools/export_area01_world_textures.py`, `tools/test_world_textures_reference.py`.

Evidence and limits: [LEVEL2_RENDER.md ("AREA01 world texture delivery")](LEVEL2_RENDER.md#area01-world-texture-delivery).

### Generic model, draw, morph and shadow services

Commit: `aabcb60`.

Canonical AREA01 actors use the existing Box/model-slot owner and borrowed
loader banks. Generic draw/shadow hooks reuse owner_draw_live and shadow_live;
face setup and morph draws retain one owner. Shared draw-method selection is
factored from the status-model implementation into owner services. Face units
accept up to the original four nodes before the batch buffers. Shadow receiver
selection follows the active area. Owner-draw texture loading delegates to the
single catalog named above; its generic hooks preserve model/source lifetimes.
The newly added placed-prop adapter forwards 0x1C4820 through its existing
status-leftovers owner and refreshes model metadata at worker boundaries;
its original-caller adapter proof passes 1,536 cases and 32 exact worker
boundaries (LEVEL2_RENDER.md ("AREA01 placed-prop adapter")). Its guarded native integration remains pending.

Existing files: `src/game/em_object_unit.c`, `src/game/em_owner_draw_live.c`, `src/game/em_owner_draw_live.h`, `src/game/em_owner_services_original.c`, `src/game/em_owner_services_original.h`, `src/game/em_shadow_live.c`, `src/game/em_shadow_live.h`, `src/game/em_status_models.c`.

New files: `src/game/em_area01_model_draw.c`, `src/game/em_area01_model_draw.h`, `src/game/em_area01_model_live.c`, `src/game/em_area01_model_live.h`, `src/game/em_area01_prop_live.c`, `src/game/em_area01_prop_live.h`, `tests/area01_model_bridge.c`, `tests/area01_model_draw_bridge.c`, `tests/area01_shadow_bridge.c`, `tools/export_area01_shadow_receivers.py`, `tools/test_area01_model_draw_reference.py`, `tools/test_area01_model_live_reference.py`, `tools/test_area01_shadow_live_reference.py`.

Evidence and limits: [LEVEL2_RENDER.md ("AREA01 generic model and animation workers")](LEVEL2_RENDER.md#area01-generic-model-and-animation-workers), [LEVEL2_RENDER.md ("AREA01 generic model drawing and shadows")](LEVEL2_RENDER.md#area01-generic-model-drawing-and-shadows).

### Global library resource delivery

Commit: `56b40c6`.

Roger's existing global-resource owner exposes bounded library-word and
resource-tail access for generic AREA01 model lookup. The existing exporter
adds the two previously absent pickup models (IDs 0x4D and 0x58), preserves
all prior regions and the FADE16 disc-source policy, and checks all AREA01
snapshots. It refuses symlinked output paths. No global model table or model
bytes are embedded in C.

Existing files: `src/game/em_area11_roger.c`, `src/game/em_area11_roger.h`, `tools/export_roger_banks.py`.

Evidence and limits: [LEVEL2_RENDER.md ("Shared library models for AREA01 pickups")](LEVEL2_RENDER.md#shared-library-models-for-area01-pickups).

### Placed doors and shared mutable programs

Commit: `f36312a`.

Multiple AREA01 door records borrow canonical actor/resource views and
call the one existing door state machine, program and transit implementation.
Shared boot leaves are exposed from that implementation, and kickoff store/
worker ordering is retained. Overlay shaft callers and boot doors forward
model, animation, script and draw work through the runtime; no fence-only
singleton or per-door pose/resource owner is introduced.

Existing files: `src/game/em_door_original.c`, `src/game/em_door_original.h`, `src/game/em_door_program.c`, `src/game/em_door_program.h`, `src/game/em_door_transit.c`, `src/game/em_door_transit.h`.

New files: `src/game/em_area01_door_live.c`, `src/game/em_area01_door_live.h`, `tests/area01_door_bridge.c`, `tools/test_area01_door_live_reference.py`.

Evidence and limits: [LEVEL2_SERVICES.md ("AREA01 placed doors")](LEVEL2_SERVICES.md#area01-placed-doors).

### Shared interpreter, AREA01 script resources and camera timeline

Commit: `3741895`.

The sole area interpreter gains the reached original subcommands and an
explicit external-handler boundary. The existing script host accepts area
images/resources, canonical owner-bank get/set, overlay-qualified callbacks
and camera workers. It preserves owner/script/player state across nested
worker calls, and its timeline globals outlive resource rebinds. New AREA01
adapters borrow delivered mutable scripts and sparse boot windows, reuse
existing helper originals, map player banks 0x96..0x98, and route scenes 2/35
through the sole camera sampler. The shared cinematic core accepts an already
sampled frame without duplicating playback behavior. Exported boot programs
and timeline tables are local ignored assets, not capture bootstrap state.

Existing files: `src/game/em_area11_script_host.c`, `src/game/em_area11_script_host.h`, `src/game/em_area_script.c`, `src/game/em_area_script.h`, `src/game/em_cinematic_playback.c`, `src/game/em_cinematic_playback.h`, `tools/test_area_script_reference.py`.

New files: `src/game/em_area01_script_live.c`, `src/game/em_area01_script_live.h`, `src/game/em_area01_script_workers.c`, `src/game/em_area01_script_workers.h`, `src/game/em_area01_timeline.c`, `src/game/em_area01_timeline.h`, `tests/area01_script_binding_test.c`, `tools/export_area01_boot_scripts.py`, `tools/test_area01_script_host_reference.py`, `tools/test_area01_script_workers_reference.py`, `tools/test_area01_timeline_reference.py`.

Evidence and limits: [LEVEL2_SERVICES.md ("AREA01 live script binding")](LEVEL2_SERVICES.md#area01-live-script-binding).

### Pickups and canonical inventory

Commit: `4b0a7e9`.

The generic adapter drives 0x15AFA0/0x219550, their script callbacks and aura
through canonical actor fields and existing pickup/take/item owners. Shared
pickup_owner exposes the same 0x15AE20 step for callbacks without duplicating
its behavior. The inventory owner exposes its original 0x1C40B0 entry. Long
and short scripts are supplied by the script adapter; no AREA11 placement
roster or seven-slot side cache is used.

Existing files: `src/game/em_pickup.c`, `src/game/em_pickup.h`, `src/game/em_pickup_owner.c`, `src/game/em_pickup_owner.h`.

New files: `src/game/em_area01_pickup_live.c`, `src/game/em_area01_pickup_live.h`, `tests/area01_pickup_bridge.c`, `tools/test_area01_pickup_aura_reference.py`, `tools/test_area01_pickup_live_reference.py`.

Evidence and limits: [LEVEL2_SERVICES.md ("AREA01 canonical pickup owners")](LEVEL2_SERVICES.md#area01-canonical-pickup-owners).

### Use scan, shared status and owner claims

Commit: `86bc4aa`.

The existing interaction host factors common status/frame/token setup out
of AREA11 placement loading and accepts a player-bank mapper. Canonical Use
scan borrows the published owner list, existing predicates and shared score,
arms the actual winner, then claims it through that same runtime. Pickup
candidate scratch writes can target authoritative spans while the old API
remains a wrapper. The existing shared-host tests retain their original
scenarios and add shared-only status and claim checks.

Existing files: `src/game/em_area11_interaction_host.c`, `src/game/em_area11_interaction_host.h`, `src/game/em_interaction_scan.c`, `src/game/em_interaction_scan.h`, `tests/area11_interaction_host_test.c`.

New files: `src/game/em_area01_interaction_live.c`, `src/game/em_area01_interaction_live.h`, `tests/area01_interaction_bridge.c`, `tools/test_area01_interaction_live_reference.py`.

Evidence and limits: [LEVEL2_SERVICES.md ("AREA01 canonical Use and shared status services")](LEVEL2_SERVICES.md#area01-canonical-use-and-shared-status-services).

### Effects, indicator children and shared leaf reuse

Commit: `6c9a5ba`.

Canonical effect services reuse the existing spawn/kill/driver owner and
publish original actor writes at worker boundaries. Indicator children use
the canonical actor/model views and existing effect-kind originals. AREA11
bindings expose their existing 0x1C5570 child spawn; security_gun exposes its
existing 0x1BA1C0 flag predicate so the runtime does not duplicate either.
Effects expose bounded readonly resource windows and their owned scratch
needed by the composite view.

Existing files: `src/game/em_area11_bindings.c`, `src/game/em_area11_bindings.h`, `src/game/em_effects_live.c`, `src/game/em_effects_live.h`, `src/game/em_security_gun.c`, `src/game/em_security_gun.h`.

New files: `src/game/em_area01_effects_services.c`, `src/game/em_area01_effects_services.h`, `src/game/em_area01_indicator_live.c`, `src/game/em_area01_indicator_live.h`, `tests/area01_effects_services_test.c`, `tests/area01_indicator_bridge.c`, `tools/test_area01_effects_services_reference.py`, `tools/test_area01_indicator_live_reference.py`.

Evidence and limits: [LEVEL2_SERVICES.md ("AREA01 effects service boundary")](LEVEL2_SERVICES.md#area01-effects-service-boundary), [LEVEL2_SERVICES.md ("AREA01 canonical indicator children")](LEVEL2_SERVICES.md#area01-canonical-indicator-children), [LEVEL2_RUNTIME.md ("AREA01 composition and live binding probe")](LEVEL2_RUNTIME.md#area01-composition-and-live-binding-probe).

### Positional audio services

Commit: `f57fad3`.

Narrow adapters reuse original gain/pan/player-misc workers and publish
canonical scratch before existing SFX submission. The stream owner exposes
its current mono option as a bounded readonly view; no stereo default or
second voice table is supplied. Audio fixtures distinguish the explicit
submit boundary from original arithmetic and alias proof.

Existing files: `src/game/em_stream_live.c`, `src/game/em_stream_live.h`.

New files: `src/game/em_area01_audio_services.c`, `src/game/em_area01_audio_services.h`, `tests/area01_audio_services_bridge.c`, `tools/test_area01_audio_services_reference.py`.

Evidence and limits: [LEVEL2_AUDIO.md ("AREA01 positional audio and shared scratch")](LEVEL2_AUDIO.md#area01-positional-audio-and-shared-scratch).

### Main and side route harness

Commit: `87c06a4`.

The native phase table and independent checker now cover all eight main
and eight side input routes. Side prerequisites follow the recorded source
capture and exact counter gap; input order, including repeated-frame commands,
is preserved. The final main route ends only at actual AREA00 pre-rebuild
arrival. The runner adds --side and the actual-C --verify-harness test. Unknown
native RNG callers fail; available original endpoint RNG states are reported
as diagnostics, never substituted for an unavailable per-call AREA01 oracle.
These harness tests do not establish a native route pass.

Existing files: `src/game/em_level_smoke_test.c`, `tools/level_smoke_area01.py`, `tools/test_level_smoke.py`.

New files: `tools/test_level_smoke_area01.py`.

Evidence and limits: [section "AREA01 recorded route harness"](#area01-recorded-route-harness).

### Gun auxiliary workers in development

Commit: `17df6fc`.

The new canonical adapter composes the existing AREA01 revisit beam owner
and SDK inverse-sine owner, and supplies the original child-spawn boundary.
Persistent bytes remain borrowed and worker failures propagate. This source
was added during the inventory pass; its independent proof and any later
worker additions must be recorded before certification.

New files: `src/game/em_area01_gun_aux.c`, `src/game/em_area01_gun_aux.h`.

Evidence and limits: [LEVEL2_RUNTIME.md ("AREA01 composition and live binding probe")](LEVEL2_RUNTIME.md#area01-composition-and-live-binding-probe).

### Documentation and private exports

Integration documents: this hub, `docs/SECOND_LEVEL_CENSUS.md` and the topic documents `docs/LEVEL2_RUNTIME.md`, `docs/LEVEL2_COLLISION.md`, `docs/LEVEL2_RENDER.md`, `docs/LEVEL2_SERVICES.md` and `docs/LEVEL2_AUDIO.md` (consolidated 2026-10-04 from 46 per-step documents; git history keeps the originals).

The export receipts and generated data stay under ignored `build/` and
`assets/` entries: AREA01 boot scripts, texture and shadow catalogs, effect
windows, the expanded Roger library and the SFX registry are local exports
(the exporters are listed in STARTUP.md's asset table). The library receipt
checks every old region for preservation. See the topic documents for
paths, counts and retained limitations.

This section makes no aggregate test or gameplay claim.

## Canonical callback dependency checkpoint

SYS, EXITA, EXITB, ROOM, SIDE, overlay and shared render/UI/FX accessors now
accept authoritative byte providers with the original read/write direction.
A refused provider never falls back to an array. The scene view exposes only
already canonical named fields and migrated progress ranges. No normal
AREA01 frame gate is opened by this dependency checkpoint.

Shared edits: the named AREA01 contexts/helpers and their ctypes oracle
layouts; AREA00 FX helpers; the two SIDE initializers in aim/fire; designated
render-world initializers in status pages and render context; the new
`test-area01-memory-view-reference` Makefile target. Exact files and full
oracle counts are documented in LEVEL2_RUNTIME.md ("AREA01 canonical memory callbacks"). Composite live
binding and scratch lifetime work remain in progress outside this checkpoint.

The isolated staged-source build passed `make -B all` with **zero warnings**
in **40.352 s**. The same export passed **89 canonical boundary checks**.
Binary SHA-256:
`28fb6c803248043709ee3bf4e653dcac0dd1988b067ad8fe938ddf1d4399222e`.
Receipts: `build/level2/canonical-callbacks/index-build.json`,
`index-build.log`, and `index-contract.log`. Full module oracle evidence
is listed in the callback document; AREA01 route completion remains unproven.

## Native state dependency checkpoint

The existing actor-pool walk accepts an optional `001CB590` selection hook,
after publishing its current actor and before clearing drawn or invoking the
behavior. The old walk entry delegates to the same body with no hook.
AREA01's pending composition uses it to publish the existing current-bone
selector; this commit adds no second pool or bone storage. Original-code
full proof passes **246 cases, 328 walks, 5,014 visits, 80 freed-next cases,
1,136 reserve allocations and 89 refused allocations**. It compares the
original `001CB590 -> 001CB5B0` result after every operation. Sanitizer tests
cover selection order in all three modes and failure before the behavior.
Receipts: `build/level2/pool-select-{unit,quick,full}.log`.

Three exact bytes become canonical in the existing scene progress region:
`00810766` (`001C02E0` bypass store), `00810842` (AREA01 door flags), and
`00810845` (`001C02E0` mechanism gate). Their initialization is the existing
`001AF2C0` reset; no per-arrival reset or copied capture is introduced.
The reset oracle now compares **14 gameplay/progress fields and 73 inventory
fields**, including dirty values for these bytes. Six neighboring reserved
spans are refused. The no-shadow audit names their actual readers/writers.

Shared files: `em_actor_pool.c/.h`, `em_scene_state.h`, the terminal-power
comment/log wording in `em_game.c`, actor-pool unit/oracle tests,
`continue_reset_probe.c`, its oracle, and `test_scene_no_shadow.py`.
No normal AREA01 frame gate changes. The isolated staged-source
`make -B all` passed with **zero warnings in 39.860 s**; actor-pool,
reset and no-shadow targets passed in that export. Receipts are in
`build/level2/native-state/`; binary SHA-256
`52664816ec7e71c4a8d777a7ca05ce2dd65ac60f5a06ecd17ad227810581489f`.

## AREA01 sound-resource checkpoint

The existing shared SFX exporter now includes all 1,000 AREA01 area-paged
IDs and global failed-grab cue `01AC`. The private native registry contains
**1,278 entries and 188 samples**; all previous **278 entries and 143
samples** retain identical serialized values and PCM. The finite table
coverage includes explicit absent and unsupported entries; it is not a
claim that every ID is requested on the first visit. The existing EMSR
loader's entry cap increases to **2,048**, with larger/malformed inputs
still refused. No audio algorithm changes in this checkpoint.

Full original-code verification passes **16,016 dispatches across 16
captures, 404 sequencer cases and 839 key-ons**. Quick verifies **1,001
native lookups**, 64 original dispatches, six sequencer cases and four
loader boundaries. AREA11 original and sanitizer audio regressions pass.
LEVEL2_AUDIO.md ("AREA01 SFX resources") gives exact scopes, conditional-call evidence and remaining
modulation/bank refusals. Main's **15 SFX files / 4,668,860 bytes** retain
identical hashes; only the registry changed.

Shared edits: `tools/export_sfx_registry.py`, the EMSR entry cap in
`em_sfx_bank.c`, four area-parameter/lookup helpers in
`test_area11_sfx_reference.py`, and the `test-area01-sfx-registry` Makefile
target. New files are `test_area01_sfx_registry.py` and LEVEL2_AUDIO.md ("AREA01 SFX resources").
The exact staged-source `make -B all` passed with **zero warnings in
39.923 s**, and its AREA01 registry test passed. Receipts:
`build/level2/sfx-checkpoint/index-{build.json,build.log,tests.log}`.
Binary SHA-256:
`987f34a7fd5c2f4b1050f3903c8b64179e6a3ddc91942607c3534ad2c49a9535`.
Normal AREA01 world frames remain gated and route verification is pending.

## Additional integration edits after the initial file inventory

These changes remain part of the guarded composition unless a dependency
checkpoint above explicitly commits them. Standalone proof is distinct from
a completed AREA01 world frame.

- Shared predicates, counters and lists: `em_area01_shared_services.c/.h`
  borrows canonical player/progress storage and calls existing owners for
  `00182BF0`, `001B11E0`, `001B1EA0`, `001C4760` and `001B1B70`.
  `em_script_host_workers.c/.h` adds exact store callbacks for the existing
  predicate; `em_director_original.c/.h` extends its existing polygon owner
  to the original XY/YZ modes. Proof files: `area01_shared_services_bridge.c`,
  `test_area01_shared_services_reference.py`, `director_original_test.c`,
  `test_director_original_reference.py`, `test_script_host_workers_reference.py`.
  `DIRECTOR_ORIGINAL.md` and LEVEL2_RUNTIME.md ("AREA01 shared worker binding") document them.
- Placed props, equipment and guarded gun helpers reuse their existing
  owners. Additional proof files are `test_area01_prop_live_reference.py`,
  `test_area01_equipment_live_reference.py`, `area01_gun_aux_bridge.c` and
  `test_area01_gun_aux_reference.py`; see LEVEL2_RENDER.md ("AREA01 placed-prop adapter"),
  LEVEL2_SERVICES.md ("AREA01 equipment child") and LEVEL2_SERVICES.md ("AREA01 gun auxiliary workers").
- Light helpers and their native point-light/packet services are composed by
  `em_area01_light_live.c/.h`. Existing `em_object_unit.c/.h` gains a
  separately selected parser for the original light packet, preserving the
  default parser's refusal rules. `area01_light_live_bridge.c` and
  `test_area01_light_live_reference.py` compare original workers, packets and
  VU output; LEVEL2_RENDER.md ("AREA01 light worker binding") records all counts. LEVEL2_RUNTIME.md ("AREA01 flicker-light owner")
  corrects the arrival fixture description: the light record is already freed.
- `em_sfx.c/.h` exposes its existing loop service; `em_sfx_bank.c/.h` adds
  exact handle-store callbacks and propagates negative gain-provider results.
  It keeps the same requested/snapshot/track owners and original cadence.
  The existing SFX oracle and AREA01 audio adapter prove the shared behavior;
  details are in LEVEL2_AUDIO.md ("AREA01 positional audio and shared scratch").
- The camera's existing `700038A0..38FF` accesses can borrow the player/aim
  owners. `em_camera_leftovers.c/.h`, `em_camera_leftovers_internal.h`,
  `em_camera_leftovers_solver.c`, and `em_camera_commit_original.c/.h` carry
  the optional resolver through native camera workers. The existing camera
  unit/oracle layouts are updated in `camera_leftovers_test.c`,
  `test_camera_leftovers_reference.py` and `test_camera_live_reference.py`.
  The scene installs aliases before player/camera execution and detaches
  before unload; `em_area01_live.c` refuses cross-owner or missing spans.
- `condition_canonical` in `em_scene_bindings.c` corrects condition 4/5's
  counter base to the original `008107D8`, preserving the signed index for
  condition 4. `test_actor_census_reference.py` checks the actual preflight
  helper for all 256 high bytes and compares the existing roster owner with
  original execution. No deferred-condition result is substituted.
- `001C69A0` is consolidated in `em_area01_math_actor.c/.h`: the complete
  AREA01 model call and `em_status_models.c/.h` share its root and post-nlerp
  matrix stages. The status view supplies its existing preblended channels;
  AREA01 borrows canonical raw slots and scratch, including the new
  `EmArea01ModelSource.scratch3480` field. Original proofs pass for 750 model
  cases, 54 caught actor calls, 255 C69A0 field variants, 30 typed/raw slot
  cases, and 145 status cases; existing status capture remains 27/27 matrices
  bit-exact under ASan/UBSan. The isolated census link adds the shared
  dependencies. See LEVEL2_RENDER.md ("C69A0 live pose binding and shared status stages") for exact files, contracts and limits.
  This does not claim native route completion or promote the census row.

## Shared C69A0 pose checkpoint

The status renderer now uses the root and post-blend matrix stages of the
existing complete `em_area01_math_001C69A0` translation. Its former separate
calculation and duplicate root scaling are removed. The status channel
evaluator supplies its already blended quaternion; the adapter invents no
keys or blend fraction. Live status preserves the original root, animation,
rest, quaternion and product scratch writes and previous slot matrices.
The public helper retains its existing immutable-input/output contract.

Original-code checks pass **145 direct status cases, 4 compatibility cases
and 3 refusal contracts**; the complete math sweep passes, including
**756 C69A0 cases, 32,940 boundary calls and all 4 branch outcomes** over
12 beats. The existing status sanitizer fixture retains **27/27 bit-exact
matrices and 63 lighting/reset pairs**. The existing census fixture retains
**4,451 equal comparisons** and its documented previous divergences.

Shared edits are `em_area01_math_actor.c/.h`, `em_status_models.c/.h`, the
census fixture's isolated link, and the main/status-test Makefile links.
New files are the direct status-pose bridge and reference test. Their
source contains no captured data. The AREA01 model adapter and live
composition remain separate integration work; normal AREA01 gameplay is
still guarded. No census row is promoted by this checkpoint.

The exact staged-source `make -B all` passed with **zero warnings in 38.793 s**; all three focused status/census targets passed. Receipts: `build/level2/pose69-checkpoint/index-{build.json,build.log,tests.log}`. Binary SHA-256: `e64a260d3f8ae534afc11e4cf42658cb0a3daa1dbdedb9a722369d20fb0a09b8`.


## AREA01 first-visit progress checkpoint

Six additional reached bytes now use the scene's canonical progress storage:
`00810759`, `0081075A`, `0081075D`, `00810760`, `008107D9` and `008107E0`.
Their readers are the first-visit NPC setup/completion, placed light setup,
bridge halves and exit-door/story checks. The existing `001AF2C0` reset
initializes these bytes; loading AREA01 adds no new seed or copied state.
The header records each original reader and writer. Live adapters remain
separate integration work, with normal AREA01 world frames still guarded.

The original reset check passes **20 game/progress fields and 73 inventory
fields**; the existing key-add check passes **90 executed cases**. The
canonical-view checker passes over **676 indexed files**, with **6 original
writer fields, 18 tracked progress/request bytes, 9 view loads and 25 listed
exceptions**. Its access-contract checks cover three accepted merged spans
and fifteen rejected reserved/invalid spans.

Shared edits are `em_scene_state.h`, `continue_reset_probe.c`,
`test_continue_reset_reference.py` and `test_scene_no_shadow.py`. No captured
state or original byte table is included. The exact staged-source
`make -B all` passed with **zero warnings in 40.735 s**; reset and canonical-view
targets both passed. Receipts:
`build/level2/progress6-checkpoint/index-{build.json,build.log,tests.log}`.
Binary SHA-256:
`e44d2b58d0e54bb1451d1c33f8bbdc06f6cb0195c1eaa7af531039f8570f8ab6`.

## Connected probe 12 follow-up (not a phase completion)

Probe 12 still completed zero AREA01 world frames. It passed the first flame
setup and stopped in existing crate callback `001551B0`, uid `1900`, at the
unbound static-kind input to `0019F730`. The scene now binds the immutable
placement `+8` bytes through `em_collision_world_bind_static_kinds`; the
original walker, cells and actor lists are unchanged. The focused original
proof passes 928 ground calls across 16 captures and 22 refusal/cleanup
contracts. It also demonstrates that the absent view reproduces the failure.

Channel-3 `001C7900` now reuses the existing draw owner's matrix/lighting
service. `001F4A10` registers its completed packet with the existing page cache
after its original depth-list insertion, so the page renderer can consume it.
The full proof passes 306 direct calls, 34 original caller cases, 68 exact
worker boundaries and 34 VU1 units containing 40 triangles. The adapter and
page parser retain 15 refusal contracts. See LEVEL2_RENDER.md ("AREA01 channel-3 matrix upload").

The flame adapter reuses existing projection, transform, GS, packet and
collision workers. Its full proof passes 1,376 service calls, 262 state-0
flame bodies, 144 GS/HUD bodies and nine fault prefixes; the AREA11 snow
regression passes 5,184 tiles and 3,151,872 packet bytes. The effects manifest
loader now rejects an oversized window list rather than silently ignoring
entries after 32. See LEVEL2_SERVICES.md ("AREA01 flame and shared render services").

Additional shared edits for these bindings are `em_collision_world.c/.h`,
`em_owner_draw_live.c/.h`, `em_weather_packets.c/.h`, `em_effects_live.c`,
`export_effect_tables.py`, `em_scene_bindings.c` and the Makefile. AREA01's
new live adapter dispatches these existing owners and supplies only their
explicitly reached scratch spans. These are integration prerequisites; the
normal AREA01 guard remains until a verified playable route exists.


## Static placement-kind checkpoint

The existing collision world can now bind the immutable roster kind byte
needed by original `0019F730`. It keeps a packed read projection of each
placement's `+8` byte and clears it on unload. The existing collision walker,
actor lists and cell data remain the owners of collision behavior. The
AREA01 scene hookup remains part of the continuing guarded integration.

The original-code proof passes **928 ground queries across 16 captures**,
including the actual crate-initializer arguments, and **22 refusal/cleanup
contracts**. It reproduces the missing-view failure before binding. The
isolated staged quick target passes **144 queries**. See
LEVEL2_COLLISION.md ("AREA01 static collision kinds") for the tested paths and limits.

Shared files in this checkpoint are `em_collision_world.c/.h` and the
Makefile's new test target; new files are the bridge, reference test and
proof note. The separate class-7 list-address correction is not included.
The exact staged-source `make -B all` passes with **zero warnings in
48.484 s**. Receipts:
`build/level2/static-kinds-checkpoint/index-{build.json,build.log,tests.log}`.
Binary SHA-256:
`3654b196bde61e22bc12434e9fbb6f27e469b79fcb936d5b5a83ca329b66069b`.
This is a storage prerequisite, not an AREA01 world-frame or route pass.

## AREA01 recorded route harness

The opt-in route names a01_00 through a01_07 and all eight side beats in
`em_level_smoke_test.c`, `level_smoke_area01.py` and the shared smoke checker.
The normal first-level endpoint remains `exit`. Pad files contain input only;
no captured player, owner, camera or world state is installed in the native
run. Prepare or run with `tools/test_level_smoke_area01.py --until a01_06`
(add `--prepare` for input export only). Every missing worker fails
visibly (the former diagnostic `--probe` is gone with the guard).

All eight main captures are contiguous, contain no teleports, and retain their
source capture/counter gaps. The exporter writes 3,596 commands and provenance
in `pads/manifest.json`, including source hashes, counters and endpoint. With
`--include-side-pads`, it also exports the eight side captures and their source
relationships: 4,447 commands total. `--side a01_s0` through `--side a01_s7`
automatically export these files and run the requested branch from ordinary
New Game. The manifest includes the native source and complete AREA01 phase
path. The `--side` and `--until` arguments are mutually exclusive.

The recording can contain several commands at the same frame, for example
a01_00 f34. There are 79 such repeated-frame entries on the main line. Their
recorded order is preserved. The C reader now admits equal frame numbers while
still rejecting backwards order; the existing last-command-at-or-before-frame
selection applies the last command. The original input alignment assumption
is unchanged: submit two rows after the recorded command, to reach the task at
the recording's three-row command latency.

Every main row through a01_06 and every side row checks the existing fields without exemptions:
player state/pose/clock/ground, position and yaw, camera, request/area/task state,
message, screen, fade, power, health, progress, and all 11 captured owner record
addresses. This comprises 11,237 main rows before the final exit beat and
8,328 side rows. Each alignment must follow the already checked source endpoint
by exactly its recorded counter gap; all native ticks in that gap must remain
consecutive. Capture metadata must match the declared native source, and its
source must end with the same neutral input released by the driver.

### Side branches

| Run | Native source phase | Counter gap | Rows |
|---|---|---:|---:|
| `a01_s0` first NPC talk | `a01_arrival` | 1 | 1,435 |
| `a01_s1` sentry document | `a01_01` | 1 | 397 |
| `a01_s2` control-room items | `a01_s0` | 6 | 680 |
| `a01_s3` fire contact | `a01_arrival` | 1 | 262 |
| `a01_s4` east room | `a01_arrival` | 1 | 1,225 |
| `a01_s5` duct | `a01_s0` | 1 | 3,118 |
| `a01_s6` blocked bridge | `a01_arrival` | 1 | 229 |
| `a01_s7` third NPC talk | `a01_05` | 2 | 982 |

For example, `python3 tools/test_level_smoke_area01.py --side a01_s5`
plays the first-level main route, AREA01 arrival idle, s0, then s5. It skips
s2 and the AREA01 main beats. The C phase table uses the existing `side` and
`from_side` dispatch, and the shared checker requires every prerequisite to
pass. Selecting a main endpoint skips every side. The default first-level
`exit` endpoint and its phase order are unchanged. The side captures include
33 repeated-frame commands; their order is preserved exactly. A missing
worker, observation, source phase or unmapped native RNG caller fails.

### RNG evidence and limits

The complete native `EM_RAND_TRACE`, including AREA01 and side frames, goes
through the existing symbolicator; unknown callers still fail. The first-level
C7 deterministic caller comparisons are retained. AREA01 itself has no
per-call RNG capture, so the harness makes no per-call AREA01 equality claim.
It verifies continuity of the native SDK LCG from cold-boot state 1 to each
checked capture endpoint, without seeding or changing game state.

Each original endpoint RAM records `D_0024295C` and its state word at +0x58
(address 0x2426C8); the snapshot counter must equal the trace endpoint. The
checker reports native/original endpoint equality as a diagnostic. A mismatch
is explicitly labeled DIFFERENT, not parity: the first-level evidence in
RAND_ORDER.md already establishes value-dependent divergence between original
runs during the opening. No per-frame state is inferred from the endpoint.
The a01_07 saved RAM is after level-3 gameplay, so it is explicitly unavailable
for the row-529 stopping boundary. No extra scene-log field is required.

### Exit boundary

The user-authorized endpoint is a01_07 row 529, counter 28329: AREA00 is loaded,
area/sub/entry are zero, slot 0 bytes +8..+B are `(3,1,0,0)`, and the next task
would execute its state-0 rebuild. Row 530 is that first completed level-3
rebuild and is outside this harness endpoint. The driver stops on the actual
area/task condition, not on elapsed row count; the recorded row is an upper
bounded test timeout, not a way to bypass an AREA01 failure.

The checker compares a01_07 rows 0..233 consecutively. Its loader starts at row
234 and has 19 distinct states through row 529. It uses the same loader state
segmentation as the existing first-level exit: exact state order, each
single-tick state retained, and only repeated host-wait states allowed fewer
ticks than the capture. It compares the full route fields on every emitted
native loading tick, reports each shortened hold count, and runs the existing
original-instruction `replay_chain` and `replay_veil` checks. Gameplay ticks are
never removed by this alignment.

The final arrival ends the current frame without requesting the usual extra
tick, which would enter level 3. The normal post-frame tail supplies its actual
message, fade and 27-byte loader snapshot. The logger retains AREA01 owner
observations while AREA01's canonical binding is alive during the next
area's load. All tails remain available to the checker: the first-level exit
selects its AREA01 rebuild pool witness by counter, and the final boundary
selects its own post-frame tail. No synthetic gameplay tick is introduced.

### Validation status

`python3 tools/test_level_smoke_area01.py --verify-harness --out build/level2/side-harness`
compiles the actual C side selector, input reader and frame driver with only
their game-facing services mocked. It passes all **16 C branch paths**, all
**4,447 recorded commands**, and **20,101 native driver frames** under ASan/UBSan.
It rejects three malformed pad files: a truncated final command, backward frame
order and an out-of-range stick. The truncated EOF case exposed and fixed the
reader accepting a partial command after a valid prefix.

The same test exercises the strict checker over **19,565 capture-shaped rows**
(all main beats before the final loader and all side beats), and rejects **120**
owner/camera/progress/clock/health/missing-owner/source-counter corruptions.
It finishes in about six seconds on native arm64 macOS. Python compilation,
C syntax with warnings as errors, and scoped whitespace checks pass. These
are harness-contract tests, not native gameplay parity. The earlier exit
checker-contract exercise also covered all 530 rows through its endpoint.

Full native route verification remains pending the live binding work; a
prepared input file or in-process PASS marker is not a verified route until
the independent checker passes. No side or main gameplay pass is claimed here.
Since step GUARD the frame guard and the diagnostic probe are gone; the route beats after a01_arrival stay opt-in.

Shared edits for this harness: `em_level_smoke_test.c` (phase dispatch and
strict pad reader), `tools/test_level_smoke.py` (phase requirements and complete
RNG audit entry), `tools/level_smoke_area01.py` (capture comparisons and source
checks), plus the new runner `tools/test_level_smoke_area01.py`. The earlier
narrow scene logger additions supply loader tails and AREA01 owner observations;
this side-run extension needed no additional scene observations.
