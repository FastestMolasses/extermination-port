# First-level route census: every original function on the route and its port status

Date: 2026-09-23 (session s87); every row re-classified against the port HEAD 9e0715f on 2026-09-24 (section 1.4); totals recounted with a measured liveness pass at ce7271f + the full-route smoke step on 2026-09-25 (section 1.14). Target: the pinned boot ELF (SHA-256 `ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`) and the AREA11 overlay (id 9).

This document answers one question: **which original functions execute on the first-level route, and what does the live port do for each of them?** It is the measuring stick for "the first level is ported". It lists addresses, names, statuses, port modules and tests only. It contains no original code, data or disassembly.

The machine-readable table is `../Extermination/build/s87/census/classified.json` (ignored, local; it holds the 2026-09-24 statuses: since then the section 3 rows of this document are the reference). It holds one row per function with every field below, the boundary groups and the gap lanes. The census inputs are in the same folder: `route_functions.json`, `per_beat.json`, `method.md`.

## 1. Method

### 1.1 The census (which functions run)

`tools/route_census.py` in the decomp repo drove the hidden PCSX2 through `tools/pcsx2_session.py`:

- Every candidate entry got a one-shot exec breakpoint: 2957 boot functions from `docs/FUNCTIONS.csv` and 34 AREA11 overlay functions (at splat label + 0x40, because the MWo3 header loads at 0x823500). Overlay hits count only while the resident overlay id is 9.
- All entries were re-armed at the start of every label, so each label has its own complete set. Only the EE was paused; no memory was written and no save-state slot was written or created.
- The labels follow FIRST_LEVEL_ROUTE.md: four startup labels from user slot 01 (S0 title to NEW GAME, S1 movie and AREA11 load, S2 opening to first control, S3 200 idle frames) and the closed-loop route beats 00..14, each from its recorded source snapshot. Beats 00 and 09 are side beats.
- Determinism: startup pass A against a new pass B (same frame counts; B adds 22/24 periodic SDK functions at the last opening frame, all of which pass A also ran in gameplay), each beat against its recorded trace, and beats 01 and 06 re-run. The report is the union over passes per label.

Result: **1184 functions executed** (1161 boot, 23 overlay), 447,056 bytes = 111,764 instructions. 356 run only before first control (S0..S2); 828 run from first control on.

### 1.2 The classification (what the port does for each)

Every executed function was matched against the port tree (`src/`, `docs/`, `tools/test_*.py`, `tests/`, the Makefile COMMON list) by its address in every spelling (`0015B130`, `0x15B130`, `15B130`, identifiers such as `w_0015B130`, and addresses inside the function body, which translations cite). The evidence was then read:

- **Translation**: a port function named after the address, a translation header comment, file-header inventories, or citations of the function's own internal addresses.
- **Verification**: a `tools/test_*_reference.py` oracle (or a capture comparison: `test_level_smoke.py`, `test_roger_encounter_capture.py`, `test_door_original_runtime.py`, `test_area11_sfx_runtime.py`, `test_player_face_host.py`, `compare_frame_order.py`) that names the function and builds the translating module. `tests/*.c` fixtures do not count.
- **Live**: the translating module is in COMMON and the translating function is reachable from `main` in a call graph of the live build, with the test-only modules (`em_level_smoke_test.c`, `em_opening_control_test.c`, `em_game_selftest.c`) and the runtime-gated paths cut. The gated paths are the player FLOOR layer (`player_states_bind` engages only the STAGE mechanism since L01, so the floor service, the fall check and the FLOOR states never run; the stage's prelude and +4 = 4 handler are bound but unreached while the interaction runtime owns the takeover), the reversal skid (`reversal_ready()` is false), and every binding that returns `unmirrored(UM_…)` (a reported no-effect binding).
- 513 rows were decided by hand after reading the port code and the per-module docs; 193 follow the evidence rules; 478 SDK/driver rows follow the boundary ranges of section 4. Where a translation exists but the live app runs something else, the row is `verified-unbound` or `unverified` and the **stand-in** column names what runs instead.

**A label is not evidence.** A comment that says DECODED or VERIFIED was never counted. A test that names a function only as a hook or stub does not verify it; where that was seen (for example 00179D20, 00182D40, 001AA140) the row says so.

### 1.4 Recount (2026-09-24, port HEAD 9e0715f)

Five commits after the census landed translations without re-classifying their rows (3824c6f: thirteen gap lanes; 81414be: the pickup owners live; 4b6ac3e: the Use dispatcher and the list passes; 9d2a129: the IOP stream driver; 0f4cc8f: the owner draw, the record-level heading and the clip chaining), and the critic corrections of section 7.2 had not been applied. Every row was re-classified against HEAD (9e0715f; the working tree differed only in this document and the user's own files). Method:

- **Translation.** Every function defined anywhere in `src/` (from `clang -S -emit-llvm` of each file) was indexed by the addresses in its name and in the comment block above it, and every file by the addresses it cites in any spelling. Each row's candidates were then read against the lane docs of those commits (STARTUP_LOAD_GAPS, LOCOMOTION_DISPLAY, PLAYER_EQUIPMENT, CAMERA_LEFTOVERS, EFFECT_MANAGER, EFFECT_KINDS, RENDER_CONTEXT, FRAME_RENDER_HEADS, RENDER_VERIFY_REST, ANIM_RUNTIME_REST, SCRIPT_DOOR_FAN, STATUS_UI_LEFTOVERS, PICKUP_OWNERS, PLAYER_USE_DISPATCH, COLL_LIST_PASSES, OWNER_DRAW, PLAYER_HEADING_RECORD) and the translation files themselves.
- **Verification.** A test counts only where it executes the original: an entry run, or an unhooked callee inside an executed caller that the native side translates. Every live row's named tests were scanned for the address in hook tables (`calls[...]`, `calls.update`, `ignored` sets, worker dicts, `lambda` models, `elif target == ...` leaf models) and every hit was read. The 28 reference tests behind the changed rows were re-run at HEAD (quick mode, `build/census_recount/`), all PASS: startup_load_gaps, locomotion_display, player_equipment, camera_leftovers, coll_list_passes, coll_probe, pickup_owner, pickup_motion, pickup_items, script_door_fan, script_host_workers, frame_render_heads, render_context, render_verify_rest, status_ui_leftovers, anim_runtime_rest, owner_draw, owner_services, effect_manager, effect_kinds, message_draw, player_use_dispatch, roger_cinematic, actor_lighting, actor_light_001d89d0, sdk_soft_float, sdk_math_original, main_loop_and_gap.
- **Live.** Measured, not inferred: the live build (the Makefile's link line, 146 sources) was rebuilt privately with `-finstrument-functions` and a caller/callee edge recorder (scratch only, nothing committed) and run headless through `newgame-level` (the level smoke: title, New Game, opening, first control, status, battery, refusal, panel, elevator; 6 live phases PASS), `newgame-control`, `EM_AREA_CHANGE_TEST=1` and `EM_ROOM_MOVE_TEST=1` (all PASS). A translation is live when it executed on a path from `main` with the test-only modules (`em_level_smoke_test.c`, `em_opening_control_test.c`, `em_game_selftest.c`) cut. For rows first seen at beat 05 or later (the smoke stops at the elevator), a static call graph of the same build (LLVM IR references, function pointers included) gives the upper bound, and the runtime gates of section 1.2 were applied by hand; those rows kept their earlier liveness unless the code was shown otherwise.
- **Boundaries.** Section 4's own rule ("a function in the render ranges that carries a translation is classified normally") now moves ten rows out of the boundary groups: 001CC1E0 (live, em_message_glyph_original) and 001D21B0, 001D2710, 001D2910, 001D2DE0, 001D2E00, 001D38A0, 001D3BA0, 001D6B10, 001D6C90 (verified-unbound, em_render_context / em_owner_draw_original). Translations inside the non-render boundaries (the IOP stream driver, the module loader, the stream-lane SDK commands) stay boundaries (section 4 note).

Result: 226 rows changed status or evidence, plus the ten boundary moves. Status moves: missing → verified-unbound 96, unverified → verified-unbound 38, stand-in → verified-unbound 31, live → verified-unbound 15, verified-unbound → live 10, boundary → verified-unbound 9, verified-unbound → missing 5, live → stand-in 3, unverified → live 2, missing → live 2, boundary → live 1, stand-in → unverified 1, live → unverified 1. Downgrades found by reading (a label is not evidence):

- **Live copies that no oracle checks** (the named test hooks or models the address): 001798D0 (player_pose_use_accepted resets legacy fields), 0020AE40 / 0020B0D0 / 0020B210 (em_battery_ui.c hand-placed page; the translations are em_status_ui_leftovers), 0020CCB0 (no port code names it; `ignored` in its test), 0021BAE0 (the event only clears a flag), 0020DFA0 (hooked as worker event 2), 00128350 (em_item_root.c host compare), 001B1470 (host wraps), 001B1240 / 00102798 / 00102CD0 / 0011E620 (all hooked by test_camera_commit_reference; the live commit uses host sqrtf and em_mat4_lookat_gs and computes no atan2), 00102900 / 00102948 / 00102958 / 001026D0 (their named tests model or hook the leaf).
- **Not called live**: 001D8270 and 001D8690: em_lighting_fold_gate / em_lighting_actor_rgb are verified but char_rig_build assumes the gate passes and keeps the renderer's own tint.
- **No translation after all**: 001CB5F0, 001CB6B0, 001CB760, 001CB900 (worker slots only, EFFECT_MANAGER.md finding 4) and 0021B9A0 (stubbed or bound to the original on both sides, finding 2).

Upgrades to live: the task table 001AB6A0 / 001AB740 (em_task.c; 001AB790 stays verified-unbound because the live New Game registers the task instead of replacing it); the pickup owners 0015AFA0 / 0015AE20 / 00219550, the take 001B6EA0, the facing 001B7F90, the inventory 001C40B0 and the aura 001F1110 / 001F1180 (81414be; 001F1180's draw block is the no-op aura_draw); the message bank records 001FE4B0 / 001FE4D0; 001C7C00 for S2 (critic 7.2); 00102738 (em_coll_probe_original / em_pickup_items_original, executed inside the probes and the aura). Module text was corrected where the live translation is a different function than the row named (001028B8, 001028D0, 00103230, 00102760, 001281C0, 0020E020).

**Sample check of this recount.** Ten changed rows were re-read end to end (translation body, test case code, live call site): 001AB790 (em_task_replace_current is absent from the live edge set; em_game_install_new calls em_task_register), 0015AE20 (0015AFA0 calls it and the pickup-owner test runs 0015AFA0 with only the program, sound, persistence, publication, aura and free calls hooked), 001B6EA0 (test_pickup_owner_reference runs 0x1B6EA0 and compares em_pickup_owner_take), 001D5C80 (test_render_verify_rest_reference PASS with the tree file), 0020CCB0 (no citation anywhere in `src/`), 00102CD0 / 0011E620 (em_camera.c camera_commit_view read), 001D8270 (char_rig_build read), 001CB760 (only NEED/CALL worker uses), 0021B9A0 (only worker slots). All held.

### 1.5 Update (2026-09-24, Boxes: FLOOR, crates / drums, Use chain)

Rows moved by the Boxes step. The evidence is the oracles named on each row
plus the level smoke (seven live phases; phase `boxes` equals route 05 row
for row) and `tools/test_collision_world_capture.py` (the six box records
equal route 04).

- **To live:**
  - the crates 001551B0 and drums 00156620;
  - the Use dispatcher 00160220, 001798D0 and the ledge climb 0015DF10 /
    00161790 with the climb and surface helpers first seen at 05_boxes;
  - the floor service 00175900 / 00175CF0, the fall check 001796C0 and the
    FLOOR probe and column walkers (0019AB20, 0019C830, 0019B6C0, 0019B8C0,
    0019BC40, 0019DF10, 0019E640, 0019F730, 001A2AE0, 001A32C0, 001A4650,
    001A5760, 0019A310);
  - the move walkers 0019AD00 / 0019AFE0 with 0019CB60 / 001A6440
    (em_coll_grid_hull);
  - the SDK 0011DBB8 / 0011E398 / 0011E620;
  - the boxes' allocation 001B0EA0 / 001B0FD0, the bone-slot stack 001AF710 /
    001AF780 and the model bind 001CA6E0;
  - 0017B490.
- **Bound, not yet reached by the live smoke.** These closure states and
  helpers are first seen at beat 06 or later: the fall 00162DB0 family,
  00175640, 00179450, 00179680, 00187DC0, the ladder, running-jump and
  weapon states. They are bound (em_player_closure_live) and keep
  `verified-unbound` until a live phase reaches them. Their rows say so.
- **Not re-measured.** This update did not re-run the instrumented call-graph
  build of section 1.4. The rows first seen at 05_boxes were moved on the
  row-for-row evidence of the boxes phase. A future recount should confirm
  them with the edge recorder.

### 1.6 Update (2026-09-24, Effects step: packet chain, fog, the handlers' transform block)

Rows moved by the Effects step (evidence on each row):

- **To live:** 0021B920. em_fog_gs_coefficients (the Metal world fog) is now a
  call of em_packet_chain_0021B920; em_packet_chain_original.c and
  em_status_ui_leftovers.c are in COMMON. test_area11_fog_reference compares
  the helper with the EE model on 2,006 pairs (the old host formula fails
  at (-110, 330)) and with each in-scope route beat's context +0xA8/+0xAC.
- **missing → verified-unbound:** 001CB5F0, 001CB6B0, 001CB760, 001CB900 and
  0021B9A0 (translated in afa091b; test_packet_chain_reference).
- **boundary → verified-unbound:** 001CB9B0 (em_packet_chain_original), 001CFB50
  and 001D0540 (em_effect_kinds, translated in this step;
  test_effect_kinds_reference reproduces the captured transform block
  D_0081F8F0 of beats 05, 08 and 12).
- **Not moved (blocked):** the effect manager, the effect spawn chain and
  driver, the handlers, the head sprite and the equipment sprite stay
  verified-unbound. Every one of their draws reads the render-context views
  (the +0x2240 / +0x22C0 clip matrices through 001CD370, the 0x70003AC0 /
  0x70003A40 matrices and the +0xA0 fog). No live code produces those bytes
  (FRAME_RENDER_HEADS.md section 4: "The port has no canonical
  render-context storage today"). EFFECT_MANAGER.md 5.0 has the detail.

The totals of section 2 and the per-label table are not recomputed here (as
in 1.5); the subsection counts of 3.14, 3.15 and 3.22 and section 4's
boundary counts are.

### 1.7 Update (2026-09-24, census L03: the hill slide live)

Rows moved by the L03 step. The evidence is the oracles named on each row
plus the level smoke's new `slide` phase, which equals route 06 row for row
(LEVEL_SMOKE.md "slide": +5, +1F0, +1F1, clip, clock and ground exact from
the entry at f72 through the landing at f138, the skid-out, the hand-back
at f181 and 12 idle rows; the heading's authored values in order, node
crossings within one row; per-row motion within 0.0025).

- **To live:** the L03 lane 0016C520, 0016C570, 0016C6A0, 0016CD70,
  00174FD0, 001791D0, 0017F5F0 and 00224B80; with them 00175640 and 00179450
  (the floor service and fall check under the slide), 001B12B0 (the slide's
  turn), 00182870 (the slide landing; also the climb's since the Boxes step)
  and 00182430.
- **00182430** has one translation now: em_player_floor.c
  em_player_step_sounds. The record's standalone callers (the slide's skid
  steps, the ladder and closure states) reach it through
  em_player_closure_live.c instead of a fail-stop, and em_player.c's own
  copy of the mapper (footstep_block) is retired; the legacy step clock
  footstep_play calls the translation.
- **Measured, not inferred.** A private `-O0 -g` build of the live link line
  ran the smoke through `boxes` and through `slide` under lldb with counting
  breakpoints on the native functions (scratch only). Through `slide`:
  0016C6A0 109 callbacks, 0016C570 1, 0016C520 1, 0016CD70 57, 0017F5F0 57,
  00174FD0 57, 001791D0 65, 00224B80 65, 00182430 3, 001B12B0 72, 00175640
  56, 00179450 11. Through `boxes` every one of these is 0 except 00182870.
- **Known gaps on this path (not moved):** the slide's 001EFD90 spawns
  (0x80000065 every 8 ticks) go to the counted effect gap (L26); the loop
  sound 0x12E and the skid, landing and climb ids are not in the exported
  sfx registry (WP-14), so 001FBD50 returns -1: the record's +31B stays -1
  and 0016CD70 re-requests 0x12E each motion tick (57 silent requests)
  where the original keeps one track; 0021D250 / 0021D2E0 (surface 0x5D,
  sub-state 0x1E), 00224290 and 0017C580 (the airborne exits) are bound
  but not reached on the route.

The totals of section 2 and the per-label table are not recomputed here (as
in 1.5 and 1.6). The section 3 subsection headers' status counts are now
counted from their rows (they had not been updated since the recount).

### 1.8 Update (2026-09-24, census L23 / L19: the truck set piece and the script host)

Rows moved by the L23 / L19 step. The evidence is the oracles named on each
row plus the level smoke's two new phases (LEVEL_SMOKE.md): `truck_preview`
equals route 07 row for row from the script's frame (f164) through the
release (f527) and 25 rows (spad, camera byte, letterbox, message, power,
D_00810792, the player record, the placement, the heading, every camera
shot, the re-grounded Y), and `truck_crossing` equals route 08's truck
record (+0x00..+0x0F, +0xB0, +0x2DC..+0x2EF) and D_00810792 from the arm
(f43) through the rest (f209) and 10 rows.

- **To live:** the truck 00823FF0 and its trigger 008251E0 (em_area11_boxes);
  the script host's 001BA1A0 and its handlers 001B8FC0 (op00 kinds 0 / 1 /
  2) and 001B94F0 (op01 kind 1) (em_area11_script_host over em_area_script);
  the rumble chain 001B1E20, 001B61C0, 001B6250 and main-loop step I's
  001B5B70 (em_pad_actuator over the new pad block D_00810E40). 001B0FD0,
  001A2370, 001B1B70, 001DD980 and 0011E2A8 were live already; their rows
  name the new callers.
- **Not moved:** 001EFD20 (the truck's 32 effect spawns reach the counted
  gap; no live effect owner, L26) and 001EBF10 (their kind handler);
  001CAA00 (the truck's +0x4C is the port's actor draw at the original
  matrix, as for the boxes); 00182B30 / 00182D70 (the takeover is still the
  interaction runtime's stand-in, but the stage now writes the admission's
  +5 / +6 / +1F0 and 00182DF0's release tail on the record, route 07 row for
  row).
- **L21 blocked:** the director 008253F0 and its beat bodies stay
  verified-unbound on the legacy em_director.c: beat 0's 06/2 waits for
  D_00810813 = 1, which only Roger's alternate script 0x828990 writes (route
  10 f3460), and Roger is unbound (L22); DIRECTOR_ORIGINAL.md section 6.
- **Duplicates:** 0011E2A8 has one bound owner (em_sdk_math_original; the
  host's `em_area_script_sin_0011E2A8` is equal on every ease argument and
  no longer bound). The panel, elevator and pickup programs still run their
  own subsets of the 001B8FC0 / 001B94F0 / 001B9C10 / 001B82D0 handlers
  (AREA_SCRIPT.md section 5).
- **Sounds:** the truck's 0x454 / 0x455 are not in the exported AREA11 sfx
  registry (WP-14): silent.

The totals of section 2 and the per-label table are not recomputed here (as
in 1.5..1.7).

### 1.9 Update (2026-09-24, census L09 / L10 / L11: ladders, crevice jump, tower climb)

Rows moved by the ladder / jump step. The evidence is the oracles named on
each row plus the level smoke's four new live phases (LEVEL_SMOKE.md;
`make test-level-smoke-full`): `cage_ladders` equals route 10's two ladder
climbs row for row (f268..f581, f780..f1090: +5, +1F0, +1F1, clip, clock,
ground, heading and X/Z exactly, Y as the lift on the ladder and exactly
after it), `crevice_climbs` route 11's tank and pipe-end climbs and
`east_tower_climb` route 13's high ledge climb (the same fields; X/Z
within the stance offset), `crevice_jump` route 12's running jump
f230..f304, and `check_fall` the walks' step-offs of routes 10, 11 and 12.

- **The blocker was data.** 0015D4C0 case 0x32 and 00177030 read the grid
  node's +0x34..+0x3F (the ladder's facing axis), which the EMCL did not
  carry. `../Extermination/tools/export_collision.py --node-class` now
  appends an axis section (flags bit 3, "EMAX": node +0x34..+0x3F, byte for
  byte the level's own; `--verify-ram` compares node bytes +0x00..+0x3F with
  the route captures' RAM, equal on beats 06 and 10), em_coll_probe_original
  reads it, and em_player_closure_live fills the ladder probe state's record
  bytes from it. An EMCL without the section still faults on an action
  node; a cell record with an action byte still faults (none on the route).
- **Workers bound in this step** (em_player_closure_live.c): the climb's
  001FB9F0(id, 0x1000, 0x1000, 0x1000) sounds as em_sfx_play (the live
  binding of 001FB9F0; other request words fault); the climb's standalone
  00187EE0(p, p + B0, p + D0) as em_player_floor.c's one translation (made
  public: em_player_ground_effect_00187EE0; its 001F0460 decal faults); the
  running jump's 0017DEB0 as em_player_climb.c's one translation over the
  record (em_player_climb_live_0017DEB0). No new translation of an original
  was written; no duplicate was added.
- **To live:** L09 00165B60, 00177030, 00199DB0, 00182A70, 001885D0 and
  0019BA80 (00176F90's probe); L10 001662D0, 0017FC80, 0017FD00, 00180300,
  00180420, 00180460, 00181110 and 00174AB0; L11 0015EC50, 001634A0,
  001751A0, 00178EC0, 0017C860 and 002243F0; the fall family 00162DB0,
  00163B40, 00163C10, 00179680, 00179880, 0017C580 and 00224290; 00187EE0.
- **Measured, not inferred.** A private `-O1 -fno-inline
  -finstrument-functions` build of the live link line with an entry
  counter (scratch only, deleted) ran the smoke twice: through
  `truck_crossing` and through the whole live route. Every row moved above
  executed in the full run (for example 001662D0 508 callbacks, 00165B60
  118, 00180300 196, 001634A0 47, 0015EC50 1, 00181110 310, the recovery
  lane 38 each, 00162DB0 41, 00163B40 135, 00187EE0 2, 0017DEB0 1). The
  ladder and jump translations ran in none of the quick run; the fall
  family ran there once (the port's walk off the truck strip in beat 08,
  which the smoke does not compare).
- **Not moved:** the director 008253F0 and its beat bodies (L21) and Roger
  (L22). The smoke drives the director's three beats through the legacy
  em_director.c (`cage_roof`, `crevice_prompt`, `east_tower` report
  NOT-LIVE driven): beat 0's script waits on Roger's 0x828990, so the
  director cannot be bound before Roger (DIRECTOR_ORIGINAL.md section 6),
  and the lines 0x97 / 0x99 reach em_message_live only through the bound
  director's op0C. 00199FA0 (the climb's hit_probe) did not run on the
  route. The hang (state 9, 001647D0) is bound but not on the route (no
  row of routes 10..14 has +5 = 9).
- **Sounds:** the ladder's 0x107, 0x10E and 0x10F are not in the exported
  AREA11 sfx registry (WP-14): silent, reported once.

The totals of section 2 and the per-label table are not recomputed here (as
in 1.5..1.8); the subsection counts of 3.4..3.7 and 3.22 and the lane mixes
of L02, L09, L10 and L11 are.

### 1.10 Update (2026-09-24, census L22: Roger's encounter live)

Rows moved by the Roger step. The evidence is the oracles named on each row
plus the level smoke's new live phase `roger` (LEVEL_SMOKE.md;
`make test-level-smoke-full`): route 14 row for row from the scripted
frame's opening (f288) to the end of the capture (f1818, 1531 rows): the
spad bytes, the camera byte, the letterbox, the fade block, the message
block, Roger's +0x00..+0x0F, +0xB0 and script block, the equipment node's
+0x00..+0x0F (and +0xB0 from Roger's clip init at f358), D_008107D8,
D_00810813, the player record's +5, +1F0, +1F1, clip, clock and +0x2F3,
the camera eye / target of the bank 0x96 timeline (camera byte 3) and the
release placement; plus the script start row (f283) in Roger's record and
block.

- **Bound owners.** `em_area11_roger` binds Roger (008237E0, area11[8]) and
  the equipment node (001C5C90, area11[9]) over their original record bytes
  (ROGER_ACTOR_ORIGINAL.md section 4); their scripts run on
  `em_area11_script_host` (AREA_SCRIPT.md 6.1); 0022EEF0 drives the camera
  in top mode 3 (em_camera.c -> `em_area11_script_host_camera_0022EEF0`);
  the player's takeover runs 00183090 on the record for a script owner
  (`player_pose_commit_tick`: the special bank 0x96 through +0x40 / +0x2F3,
  the face 001D0C70 inside it) and 00182DF0's nonzero-+0x2F3 release
  (`record_release_special`); Roger's EMIS record is the Use scan's
  class-10 candidate (em_roger_candidate) and his record the collision
  world's class-2 owner (`em_collision_world_bind_owners`).
- **Data.** `tools/export_roger_banks.py` exports D_0028A490's table, the
  clip banks 0x96 / 0x4A, Roger's model file and the equipment model 0x6B at
  their EE addresses (assets/scene_snow/roger/resources.emrs, checked byte
  for byte against RAM in all 16 AREA11 captures; STARTUP.md).
- **Canonical bytes migrated.** D_00810758, D_0081078F, D_00810791 (was
  `g.opening_event_39`), D_008107D8 (em_scene_state.h).
- **Retired duplicates.** em_roger_runtime.{c,h} and em_roger_assets.{c,h}
  (a native Roger script interpreter and loader, never on the live path)
  with tests/roger_runtime_test.c, tests/roger_assets_test.c,
  tools/test_roger_encounter_reference.py and the make targets
  test-roger-runtime / test-roger-assets; em_roger_trigger (a second
  001B1EA0: em_director_original_001B1EA0 is the one translation, its
  0x82AB80 cases in test_director_original_reference part 2); the player
  pose host's legacy cinematic request (`player_pose_cinematic_*`: a foreign
  bank held off the record) in favour of the record path. The host test's
  foreign-request step and test_player_cinematic_reference's bridge were
  retargeted to the record path (the latter still against the original
  001B9A00 / 00183090 / 001C64F0 / 00182DF0, 1388 callbacks).
- **Fixes found on the way (original evidence).** em_point_light's rotation
  sine takes the magnitude (VU0 VSQRT |x|; a tiny flicker angle rounded 1 -
  cos^2 below zero; test_point_light_reference's new tiny-angle cases);
  0015C1F0's +0x2FF kind store (the face attach reads it); 0015B130's
  major-1 admission calls 00182D70.
- **Measured, not inferred.** A private `-O1 -fno-inline
  -finstrument-functions` build (scratch, deleted) ran the smoke through
  `east_tower_climb` and through `roger`; every row moved to live above ran
  (for example em_roger_tick 11963 calls, 001C5C90 11964, 0022EEF0 1400,
  0022EC30 1, 001C6960 2802 in the encounter, 00182D70 6, 001B8020 1,
  001B7B30 (op0D) 1382, 001B7840 (op10) 18, 001B6E40 (op16) 4, 001B6BF0
  (op18) 1, 001B81D0 1, 00182BF0 4). Bound but not reached on the route:
  001BA540, 001AF890 (Roger is never freed), 001CA770 on Roger.
- **Reported, not drawn:** Roger's 001DA6A0 (the port draws no actor
  shadow), the script's 0021B9A0 and 001D2830 (UM_ rows, em_scene_bindings).
- **Not moved:** beat 10's alternate script 0x828990 (D_00810793) needs the
  director (L21), which stays on em_director.c; it is now unblocked (Roger
  is bound). The voiced lines are WP-8b (the encounter's stream plays
  through the lane-0 stand-in em_opening_media, cue 29).

The totals of section 2 and the per-label table are not recomputed here (as
in 1.5..1.9); the subsection counts of 3.5, 3.10, 3.11, 3.13, 3.14, 3.15,
3.16, 3.22 and 3.23 are.

### 1.11 Update (2026-09-25, census L13..L16: the walking camera live)

Rows moved by the camera step (docs/CAMERA_LIVE.md). The evidence is the
oracles named on each row plus the level smoke's camera rows
(LEVEL_SMOKE.md; `make test-level-smoke-full`): the area load's 001B0460
seat and 0018B9C0 state-0 frame byte for byte against frames 2639 / 2640
(newgame_samples.jsonl; the state-0 ceiling +0x60 / +0x5A bit 0x80
excepted); the hand-off settle byte for byte on the 24 frames 4004..4027
(postcinema_samples.jsonl) and converging over the 40 before; after the
releases of routes 02, 04 and 14 the follow camera (eye, target, desired
eye / target, +4..+7) row for row to each capture's end; route 03 exact
from f679; route 07 converging (0.14 to 1e-5); the battery's post on the
original's row (64; it was 61 under the legacy camera).

- **Bound owners.** `em_camera_live.c` holds the one camera block
  0x008101E0, the pool D_008105D0..6A3 and the camera scratchpad words, and
  runs em_camleft_0018B9C0 as the camera frame (em_render_frame.c
  em_camera_0018B9C0 / _opening) with em_camera_follow_original,
  em_camera_leftovers(_solver) and em_camera_area11_specials as its
  workers, over em_collision_world (0019A910, 0019B7D0), the SDK math
  (0011E620, 0011E748), 001B1240 and 001B1EA0. 0018C0D0 and its leaves
  00102798 / 00193660 are new translations (em_camera_commit_original.c,
  byte-matched C / an asm-word file read); 00102CD0 is em_cs_00102CD0.
  001B0460 (with 001B0080, 001B0B50, 001B0250) runs on the live block at
  the area build (spawn_w_001B0460), 0018C0D0 state 4 and 0018D7B0 are
  bound in em_scene_bindings (their UM_ rows are removed); 0015CBA0 runs
  after the player stage tail.
- **Data.** `tools/export_camera_tables.py` exports D_0024A4B0..6F0
  (00190F20's and 00194D10's quads) to assets/camera_tables.emrg (STARTUP.md
  step 48; required: without it the area build faults at 0x0018B9C0).
- **Canonical bytes migrated.** D_0081078B..C (00191210's event byte) and
  D_00810803..4 (00195130's counter), em_scene_state.h.
- **Retired.** em_camera_probe.{c,h} (a partial 0018D330 / 0018D910 copy)
  with tools/test_camera_probe_reference.py; the host-math commit
  (camera_commit_original's sqrtf / look-at) with
  tools/test_camera_commit_reference.py (it hooked 00102CD0, 00102798,
  0011E620 and 001B1240); em_math.h em_mat4_lookat_gs (every look-at is
  now em_cs_00102CD0 through camera_view_00102CD0); the fabricated g.cam.yaw
  atan2 at 001B8FC0 kind 0 (em_opening_runtime.c).
- **Measured, not inferred.** A private `-O1 -fno-inline
  -finstrument-functions` build (scratch, deleted) ran the full smoke;
  every row moved to live above ran: 0018B9C0 12004 calls, 0018BC20 8006,
  0018C0D0 12008 (00102CD0 / 001027E0 / 00102798 12008 each), 0018C4B0 /
  0018C6A0 10669, 0018D330 / 0018D7B0 5255, 0018DD20 5252, 0018C0C0 5274,
  00190F20 8006, 00191390 8006, 00191D40 4751, 00192010 377, 001921D0
  5248, 00195130 / 00191210 / 00193EB0 5248, 001916C0 5314, 00191000 1863,
  0018C5A0 / 001914A0 / 00191580 66, 0018D910 3, 0018CE60 1, 0015CBA0
  10706, 001B1240 21810, 001B0460 / 001B0080 / 001B0B50 1. The legacy
  camera_update, camera_mode_dispatch, cam_solver_0018DD20 and
  cam_bounds_settle_0018CE60 ran 0 times.
- **Still stand-ins in AREA11:** the director's, examine, door-cinematic and
  aim owners pre-empt camera action 0 through `camera_area11_standins`
  (2692 of the 7940 calls of the 00195130 slot in the full smoke; CAMERA_LIVE.md section
  6; lanes L21, L18, L28). The opening's timeline words +0x6C..+0x7B stay
  em_opening_runtime's (L33).
- **Not moved:** 00199C50 (reported no-effect state 0, em_startup_load_gaps);
  scenes without an original roster (after the level exit) keep the legacy
  camera_update and its duplicates.
- **Smoke changes:** check_first_control_camera and
  check_follow_after_release are new; check_battery now requires the
  original's post row; check_slide aligns on the landing and scales the
  allowed rows for the heading crossings and the landing to the entry
  offset (one row up to 0.6, two up to 0.9; was one row, exact landing):
  the steered walk now enters the slide 0.86 from the original's entry
  (0.58 before). check_roger compares Roger's block halfword +0x0E from his
  clip init (f358), next to the +0xB0 exemption. Both are navigation-induced
  and pending lead review (CAMERA_LIVE.md section 4).
- **Substitutions kept** (CAMERA_LIVE.md section 5): before first control
  the camera's player view reads +B0 from the placement and 0x70003B50 from
  the record's +C0 / heading / +C8 (the port evaluates no pose there); the
  pose evaluated from the area load replaces both.

The totals of section 2 and the per-label table are not recomputed here (as
in 1.5..1.10); the subsection counts of 3.2, 3.4, 3.6, 3.10 and 3.24 and
the lane mixes of L13..L16 and L37 are.

### 1.12 Update (2026-09-25, census L12 + L33: the idle / walk states live)

Rows moved by the idle / walk step (docs/LOCOMOTION_DISPLAY.md section 4).

- **Bound owners.** em_player_closure_live.c `bind_loco` binds the one
  `EmLocoHost` and installs 00161020 / 001612D0 as 0015B130's state[0] /
  state[1] over the player record; em_player.c runs them behind the port's
  stand-ins (the legacy door sequence, the examine and director locks, the
  armed stances / R2 / melee: `player_standin_callbacks`, lanes L18 / L21 /
  L28; +5 = 0x1D..0x22, which 001607D0 enters, run the same stand-in).
  Workers: 001607D0, 00160220, 00174AC0 (record-level), 00174A50, 001749A0 /
  001749F0 / 001C61D0 (record pose), 001764E0, 00175900, 001756E0 (new
  record adapter), 001796C0, 00178B90, 00184BA0, 001798D0, 0017C540,
  0017C440, 001FB9F0, 001B1470, 001B0070, the counted 001EFD90 gap, and two
  new record-level translations: **0017BC40** (em_player_motor_0017BC40:
  the tier tables from the exported span, 0x70003A20 written) and
  **0017B910** (em_player_foot_stop_0017B910), both against the executed
  original in `tools/test_player_loco_workers_reference.py`. 001C9D50 /
  001C9E40 run inside 0017B660 over the record pose, whose regions now map
  the pose buffers D_00287F40..D_00289B40. 00187350 runs on the record after
  every player stage's animate step (em_player_closure_live_footstep).
- **Measured, not inferred.** A private `-O1 -fno-inline
  -finstrument-functions` build (scratch, deleted) ran the full smoke (15 live
  phases PASS): 00161020 1,435 calls, 001612D0 2,496, 0017C030 2,494,
  0017B660 2,006 (251 cross-fades; 00179D20 502, 00179FF0 251, 001C9D50
  5,271, 001C9E40 10,542), 0017B5C0 30, 0017B490 / 0017B460 2,107, 0017BC40
  2,494, 0017B910 10, 001607D0 / 00160220 3,667, 00174AC0 3,663, 00178B90
  2,683, 001756E0 3,776, 0017C440 6, 0017C540 11, 00187350 10,706. The legacy
  `player_move_callbacks`, `footstep_play`, `em_player_motor_tick`,
  `player_pose_foot_stop_begin` and `player_use_poll` ran 0 times.
- **Against the original.** `make test-first-control-reference` (new): the
  record on 56 first-control callbacks (30 held, 18 released, 8 re-entry)
  equals the original's actor bytes (collision_run_poll.json) in the pose
  source, the locomotion state, the step phase +25E and the feet; +C4 exact
  except on 3 callbacks after the live camera's D_008106A0 differed by one
  ulp. newgame-control travels 9.599849, the original's (9.599989 before).
  The level smoke's 15 live phases PASS; its slide and Roger tolerances are
  unchanged (the entry offset 0.863 comes from the smoke's own stick path,
  not from the walk translation).
- **Retired.** em_player_reversal.{c,h} (a partial second translation of
  0017C030 cases 6 / 7, 001612D0 case 2 and the 00174AC0 gate), its host
  binding in em_player.c, tests/player_reversal_host_test.c and
  tools/test_player_reversal_reference.py (rule 4: superseded by
  test_locomotion_display_reference; the interpreter the four player oracles
  build on moved to tools/player_callback_oracle.py); the mirror-based
  `player_footstep_0187350` and its mailbox (never called).
- **Not moved:** 00182D40 (its caller 00182DF0 is not on the record),
  00187DC0 (unchanged), 001CB5B0 and the other L33 rows named in the lane
  table.

The totals of section 2 and the per-label table are not recomputed here (as
in 1.5..1.11); the lane mixes of L12 and L33 are.

### 1.13 Update (2026-09-25, render + UI step: the indicator children, the level background)

Rows moved (evidence on each row):

- **unverified → live: 001C5680, 001C5760.** Each indicator child is its own
  pool node running `em_indicator_child_step` (em_indicator_child.c) in walk
  order (docs/CENSUS_UNVERIFIED.md "001C5680 and 001C5760"). The old pair of
  aggregates stopped at the battery pickup (the freed head never handed them
  on), so the terminal arrow stayed red after the panel powered it; it now
  turns green as in route 04, through 00827B10's colour tail 0x827EAC
  (`em_indicator_00827B10_colour`, after em_elevator_tick's level step). The
  0x825940 child now draws its 001F54E0 (one 00122BB8 value) in walk order;
  its own model draw waits on the object-unit draw (OWNER_DRAW.md P1). The
  0x825940 and 00827B10 children are the owners' inline spawns (0x825A74,
  0x827BD8), no longer interim. Both rows keep a stand-in note: the live
  bind (001C2360 / 001C22A0) never refuses and the placement (001C6380) is
  a no-op, pinned in test_census_unverified_reference as `live-init-stub` /
  `live-place-stub`; 001CABA0's 001CA7B0 cull is not modelled. The panel's
  completion skips an empty child slot, as 00159210 case 2 does.
- **verified-unbound → live: 001F54E0** (em_effect_kinds_001F54E0; the
  header copy `em_effect_delta`, not bit-exact, is deleted) and **001E1E60**
  with kernel 0x0023C990 (em_background_gs: the manifest's `background` line
  loads the disc-replayed texture; frame_close_out draws it first in every
  world frame when D_008106C4 == 0 and no movie played in the frame, as
  001D2300 gates its CALL). On newgame-control's first-control frame the
  sky region is 0 black pixels, mean (48, 48, 48), the original's value
  (`tools/test_background_reference.py --native ... --original ...`).
- **Not moved:** 001D2300 itself (the list kick is the renderer boundary; its
  gate is mirrored), 001C1F50 / 001E2260 / 001E2270 (the manifest line stands
  in for flag 0x20 and the asset carries TEX0 / colour).

The totals of section 2 and the per-label table are not recomputed here (as
in 1.5..1.12); the lane mixes of L17, L27 and L31 are.

### 1.14 Recount (2026-09-25, full-route smoke step: the director prepared, side beat 00 live)

The totals of section 2, the per-label table and every section 3 subsection count are recomputed from the section
3 rows (they had not been since the 2026-09-24 recount; sections 1.5..1.13 moved rows without them). Liveness was
measured, not inferred, for the rows the earlier updates moved by hand:

- **Edge recorder.** A private `-O1 -fno-inline -finstrument-functions` build of the live link line with a
  caller / callee edge recorder (scratch only, deleted) ran five times: the full level smoke (15 live phases PASS),
  the side beat 00 run, `newgame-control`, `EM_AREA_CHANGE_TEST=1` and `EM_ROOM_MOVE_TEST=1` (all PASS). A native
  function is live when it is reachable from `main` (or the audio thread) with the test-only modules
  (`em_level_smoke_test.c`, `em_opening_control_test.c`, `em_game_selftest.c`) cut: 2,855 of the 2,962 executed
  native functions.
- **Row to native function.** Each row's candidates are the native functions whose name or preceding comment block
  cites the address, plus the identifiers of its module column; every live row whose candidates did not run was read
  by hand.
- **Live rows confirmed: 419 of 420.** The 21 rows without an address-named candidate that ran were read: 20 run
  under other names (the title menu 001AC480 / 001AC7F0 in em_startup_tick / title_draw; the fades 001AEBE0 /
  001AEE70 in em_screen_fade_tick / em_transition_fade_tick; 00175CF0 in em_player_floor_service; 001662D0 and
  001885D0 in em_player_ladder_climb_state; 00178EC0 in em_player_recovery_translate; 001575B0 / 001B99F0 / 001B9BA0
  in em_panel_program's execute; 001CA6F0 inline in Roger's init; 0015C310 in
  em_area11_spawn_player_children_0015C420; 001D7BB0 / 001D7FA0 in em_point_light_reset / _register and 001E67C0 in
  em_snow_tiles, which their tests execute against; 001D7B30 in the room-rig lookup; 00209280, 0020E0C0 and 0020EE50
  in the battery page, the status exit and em_item_root_tick). **001ABF90 is downgraded to verified-unbound**: its
  translation is on the game-over path only and never ran (the port's title does not reach it).
- **Upgrades, verified-unbound to live (30 rows):** rows whose own translation (named after the address, not a
  worker slot or a port stand-in) ran on the live path: 001AF470, 001B0DC0, 001760C0, 0019A180, 0019A570, 0019D330,
  0019E930, 0019FE50, 001A0B10, 001A3980, 001A4D10, 001B1470, 001000E0, 001026D0, 00102718, 00102958, 0011C4C8,
  0011DB90, 0011DE90, 0011E080, 0011FD78, 00126AB8, 00126BE8, 00127398, 001274B0, 00127728, 00127758, 001277B0,
  00128320, 00128350. The Boxes, slide, ladder and walk steps made them live (the move and segment walkers, the SDK
  math and soft float, the heading wrap) without updating these rows. Not upgraded although a function with the
  address in its name ran: 001C1D00, 001D1C50 and 001D1EA0 (em_render_frame.c's em_render_* are the port's frame
  heads, not the em_frh_ translations, which did not run) and 001F0120 (the live spawn binding is not the
  em_head_sprite_original translation).
- **This step's rows.** The director 008253F0 and its beats stay verified-unbound (the binding is prepared, section
  5 L21); 001BA1A0 / 001BA1F0 / 0018CBD0 / 0018D7B0 gained no new live caller on the live path.

Result: live 449, verified-unbound 262, unverified 5, stand-in 3, missing 0, boundary 465 (was, in the stale table:
246 / 452 / 7 / 4 / 7 / 468).

### 1.3 Status values

| Status | Meaning |
|---|---|
| live | A translation runs in the live app and an original-instruction oracle or an original capture checks it. |
| verified-unbound | A translation exists and an oracle checks it, but the live app does not run it (not in COMMON, not bound, or gated off). |
| unverified | A translation exists, possibly partial, and nothing original checks it. |
| stand-in | The live app does something in its place that is not a translation (legacy code, an approximation, or a native substitute of game logic). |
| missing | No port code at all, including calls the bindings report as no-effect. |
| boundary | SDK, libc, IOP, driver or GS/VU1 work that the port legitimately replaces with a native platform service. Listed separately in section 4 and not counted in the five statuses above. |

Decomp status codes: BM byte-matched C, NM NEARMISS (readable C, the build links the original assembly), AI asm with mnemonics, AW asm of `.word`s, CL C linked from asm, AU undecompiled, CO overlay C.

## 2. Totals

### 2.1 All 1184 executed functions

| Status | Functions | Instructions | From first control on | Startup only (S0..S2) |
|---|---:|---:|---:|---:|
| live | 449 | 63,801 | 417 (61,232) | 32 (2,569) |
| verified-unbound | 262 | 22,832 | 218 (20,006) | 44 (2,826) |
| unverified | 5 | 463 | 4 (330) | 1 (133) |
| stand-in | 3 | 56 | 3 (56) | 0 (0) |
| missing | 0 | 0 | 0 (0) | 0 (0) |
| boundary | 465 | 24,612 | 186 (11,155) | 279 (13,457) |
| **total** | **1184** | **111,764** | 828 | 356 |

Of the 719 non-boundary functions, 449 (62.4%) are live and verified; by instructions 63,801 of 87,152 (73.2%). One of them, 0015BCF0, is live only in part (its tail, its animate step and 00187350), and 001F1180 runs live without its draw block; the rows say so. A further 262 functions (22,832 instructions, 26.2%) are verified translations the live app does not run. Only 8 functions (519 instructions) have no verified translation on the live path: 3 stand-ins (001FCB90, 0020CCB0, 0021BAE0; em_census_standins translates them, unbound) and 5 unverified (0015AC00, 0015CF90, 001B1190, 001CF470, 0020DFA0); no row is missing. The totals, the per-label table below and the section 3 subsection counts are computed from the section 3 rows (recount 2026-09-25, section 1.14) with each function's instruction count and labels from `route_functions.json`; the method reproduces the 2026-09-24 summary of `classified.json` exactly when fed its statuses. Before this recount the table still showed the 2026-09-24 numbers (246 / 452 / 7 / 4 / 7 / 468).

### 2.2 Per route label

"Ran" counts every function the label executed; "first" counts the functions first seen in that label in route order (beat 00 is placed before 01).

| Label | Ran: live / verified-unbound / unverified / stand-in / missing / boundary | First seen here: live / v-u / unv / stand-in / missing / boundary |
|---|---|---|
| S0_title | 31 / 45 / 0 / 0 / 0 / 396 | 31 / 45 / 0 / 0 / 0 / 396 |
| S1_newgame_load | 80 / 99 / 0 / 0 / 0 / 100 | 59 / 62 / 0 / 0 / 0 / 19 |
| S2_opening | 268 / 168 / 2 / 0 / 0 / 142 | 209 / 115 / 2 / 0 / 0 / 37 |
| S3_first_control_idle | 200 / 136 / 1 / 0 / 0 / 134 | 10 / 2 / 0 / 0 / 0 / 0 |
| 00_panel_no_battery | 254 / 149 / 1 / 0 / 0 / 114 | 27 / 5 / 0 / 0 / 0 / 0 |
| 01_battery | 278 / 169 / 3 / 2 / 0 / 171 | 21 / 7 / 2 / 2 / 0 / 5 |
| 02_elevator_refusal | 260 / 154 / 2 / 0 / 0 / 119 | 6 / 5 / 1 / 0 / 0 / 5 |
| 03_panel_power | 302 / 177 / 3 / 3 / 0 / 145 | 16 / 4 / 0 / 1 / 0 / 1 |
| 04_elevator_ride | 252 / 155 / 2 / 0 / 0 / 151 | 2 / 0 / 0 / 0 / 0 / 2 |
| 05_boxes | 261 / 145 / 2 / 0 / 0 / 114 | 21 / 2 / 0 / 0 / 0 / 0 |
| 06_hill_slide | 234 / 140 / 1 / 0 / 0 / 114 | 11 / 1 / 0 / 0 / 0 / 0 |
| 07_truck_preview | 236 / 152 / 1 / 0 / 0 / 132 | 0 / 1 / 0 / 0 / 0 / 0 |
| 08_truck_crossing | 224 / 148 / 2 / 0 / 0 / 149 | 0 / 2 / 0 / 0 / 0 / 0 |
| 09_fence_door | 258 / 174 / 1 / 0 / 0 / 122 | 4 / 5 / 0 / 0 / 0 / 0 |
| 10_cage_roof_roger | 303 / 166 / 1 / 0 / 0 / 157 | 24 / 4 / 0 / 0 / 0 / 0 |
| 11_crevice_prompt | 302 / 162 / 1 / 0 / 0 / 154 | 0 / 2 / 0 / 0 / 0 / 0 |
| 12_crevice_jump | 251 / 142 / 1 / 0 / 0 / 110 | 6 / 0 / 0 / 0 / 0 / 0 |
| 13_east_tower | 282 / 157 / 1 / 0 / 0 / 153 | 0 / 0 / 0 / 0 / 0 / 0 |
| 14_roger_encounter | 299 / 165 / 1 / 0 / 0 / 122 | 2 / 0 / 0 / 0 / 0 / 0 |

### 2.3 What the numbers say

State at the recount of 2026-09-25 (section 1.14).

- **Live and verified: 449 of 719 non-boundary functions (73.2% by instructions).** Every main-line route phase the
  level smoke plays live reproduces its capture (LEVEL_SMOKE.md): first control, the status screen, the battery, the
  refusal, the panel, the elevator, the boxes, the slide, the truck preview and crossing, the ladders, the tank and
  pipe climbs, the crevice jump, the east-tower climb and Roger's encounter; side beat 00 (the panel without the
  battery) in its own run since this step.
- **What still stands in on the route:** the director 008253F0 and its beats (L21; the three driven phases
  cage_roof, crevice_prompt and east_tower): its binding is prepared and verified up to route 10 f1162, and waits on
  the voice lanes (WP-8b: 001FA5A0, 001F9CF0 and the IOP stream driver, STREAM_LANES.md "Still missing") because
  Roger's 0x828990 and the lines 0x97 / 0x99 are voiced; the fence door 001BC350 (side beat 09, L18: the legacy
  em_door_update; its room move has its own capture test); the camera stand-ins that pre-empt action 0 for the
  director, examine, door cinematic and aim (L21, L18, L28); the mode-4 presenters 001FCB90 / 0020CCB0 / 0021BAE0
  (stand-ins, translated in em_census_standins, unbound); the effect draws and the render-context consumers (L26,
  L30, L32).
- **Verified but not run live: 262 functions (22,832 instructions).** The largest groups are the render heads,
  projection, lighting and shadow (section 3.16: 53), the render context and weather (3.17: 26), the effects (3.18:
  28), the stream lanes and sound (3.19: 24), the anim runtime leaves (3.14: 18), the AREA11 overlay owners (3.23:
  12: the director, the door, the husks, the fan) and the door (3.12: 8).
- **No verified translation:** 8 functions (519 instructions): 3 stand-ins and 5 unverified rows (section 2.1). No row
  is missing.

## 3. Per-subsystem tables

Every non-boundary function, grouped by address range. Columns: address, name (when the decomp has one), decomp status, port status, the translating module and the test that verifies it, the live stand-in or a note, and the first label where the census saw it (a trailing `*` marks functions that run only before first control).

### 3.1 Main-loop tasks, frame machine and fades (0x1AA200..0x1AEFFF)

32 functions, 2,205 instructions: live 26, verified-unbound 6 (recount 2026-09-25).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001AAD00 | — | NM | live | em_collision_world (em_coll_list_passes_001AAD00_hooks, em_actor_class_lists_swap_001AAD00) — test_actor_collision_reference.py, test_coll_list_passes_reference.py | w_001AAD00 in both variants (roster scenes): the nine hooks, then the list block; its interactive list is the Use scan's one store | S2_opening |
| 0x001AB4E0 | — | BM | verified-unbound | em_startup_load_gaps em_slg_001AB4E0 — test_startup_load_gaps_reference | not bound (STARTUP_LOAD_GAPS.md section 4) | S0_title |
| 0x001AB590 | — | BM | verified-unbound | em_startup_load_gaps em_slg_001AB590 — test_startup_load_gaps_reference | a DMA CHCR watchdog: really a hardware boundary (STARTUP_LOAD_GAPS.md section 4 item 10) | S0_title |
| 0x001AB6A0 | — | BM | live | em_task.c em_task_dispatch — test_startup_load_gaps_reference (executes 001AB6A0 against em_task.c) |  | S0_title |
| 0x001AB740 | — | BM | live | em_task.c em_task_register — test_startup_load_gaps_reference | the live New Game registers the 001ACEC0 task through it (em_game_install_new) | S0_title |
| 0x001AB790 | — | BM | verified-unbound | em_task.c em_task_replace_current — test_startup_load_gaps_reference | em_task_replace_current never runs on the live path: the port's New Game registers the 001ACEC0 task with em_task_register (em_game_install_new) where 001AC070 state 4 calls 001AB790 | S0_title* |
| 0x001AB7D0 | — | BM | verified-unbound | em_status_scene_original — test_status_scene_reference.py |  | S0_title |
| 0x001ABF90 | — | BM | verified-unbound | em_scene_task, em_scene_bindings — test_scene_task_reference.py | recount 2026-09-25: not executed in the measured runs; its translation (push_packet_001ABF90, em_render_001ABF90) is on the game-over path only, and the port's title (em_startup) does not reach it | S0_title* |
| 0x001AC070 | — | BM | verified-unbound | em_startup_load_gaps em_slg_001AC070 — test_startup_load_gaps_reference | em_game.c em_game_legacy_continue_task_001AC070 (installed through w_001AB790) and the em_startup.c / em_frontend.c title flow | S0_title* |
| 0x001AC480 | — | BM | live | em_startup.c title menu — test_title_menu_reference |  | S0_title* |
| 0x001AC7F0 | — | NM | live | em_startup.c title menu — test_title_menu_reference |  | S0_title* |
| 0x001ACEC0 | — | BM | live | em_game, em_scene_task — test_area_load_reference.py, test_scene_task_reference.py |  | S0_title |
| 0x001AD010 | — | BM | live | em_scene_task, em_scene_bindings — test_area_load_reference.py, test_room_move_reference.py, test_scene_task_reference.py |  | 09_fence_door |
| 0x001AD1A0 | — | BM | live | em_scene_bindings, em_game — test_area_load_reference.py |  | S0_title* |
| 0x001AD230 | — | BM | live | em_scene_bindings, em_game — test_area_load_reference.py |  | S0_title* |
| 0x001AD250 | — | BM | live | em_scene_task, em_game — test_scene_task_reference.py |  | S0_title |
| 0x001AD360 | — | BM | live | em_scene_task, em_scene_bindings — test_scene_task_reference.py |  | S0_title* |
| 0x001AD4D0 | — | BM | live | em_scene_bindings, em_scene_task — test_scene_task_reference.py |  | S1_newgame_load |
| 0x001ADF50 | — | BM | live | em_scene_task, em_load_veil — test_scene_task_reference.py |  | S1_newgame_load* |
| 0x001AE040 | anim_frame_top_b | NM | live | em_scene_frame, em_scene_bindings — test_scene_frame_reference.py |  | S1_newgame_load |
| 0x001AE5E0 | — | BM | live | em_scene_frame.c em_sf_001AE5E0 — test_scene_frame_reference; test_level_smoke.py | stage workers bound to legacy stages where noted | S2_opening |
| 0x001AE6B0 | — | BM | live | em_scene_frame, em_scene_bindings — test_scene_frame_reference.py |  | S2_opening |
| 0x001AE7E0 | — | BM | live | em_scene_classify, em_scene_state — test_scene_classify_reference.py, test_scene_frame_reference.py |  | S2_opening |
| 0x001AEB60 | — | BM | live | em_fade.c — test_fade_reference.py |  | S2_opening |
| 0x001AEBA0 | — | BM | live | em_fade.c — test_fade_reference.py |  | S2_opening |
| 0x001AEBE0 | — | BM | live | em_fade.c — test_fade_reference.py |  | S0_title |
| 0x001AED80 | — | BM | live | em_fade.c — test_fade_reference.py |  | S1_newgame_load |
| 0x001AEDB0 | — | BM | live | em_fade.c — test_fade_reference.py |  | S1_newgame_load |
| 0x001AEDE0 | — | BM | live | em_fade.c — test_fade_reference.py |  | S0_title |
| 0x001AEE10 | — | BM | live | em_fade.c — test_fade_reference.py |  | S2_opening |
| 0x001AEE40 | — | BM | live | em_fade.c — test_fade_reference.py |  | S1_newgame_load |
| 0x001AEE70 | — | BM | live | em_fade.c — test_fade_reference.py |  | S0_title |

### 3.2 Actor pool, spawn placement and entity services (0x1AF000..0x1B0FFF)

28 functions, 1,504 instructions: live 23, verified-unbound 5 (recount 2026-09-25).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001AF2C0 | — | NM | live | em_game.c em_game_new_game_reset_001AF2C0 + em_pickup_reset — test_continue_reset_reference |  | S0_title* |
| 0x001AF470 | — | AW | live | em_startup_load_gaps em_slg_001AF470 — test_startup_load_gaps_reference; test_continue_reset_reference | recount 2026-09-25: em_slg_001AF470 executed on the live path (6 calls over the five measured runs) | S0_title* |
| 0x001AF5C0 | — | BM | verified-unbound | em_startup_load_gaps em_slg_001AF5C0 — test_startup_load_gaps_reference | em_game.c em_game_legacy_state0 / em_scene_bindings.c w_001AFCA0 (native re-arm; the pool reset 001AF8E0 is translated) | S1_newgame_load* |
| 0x001AF690 | — | BM | verified-unbound | em_startup_load_gaps em_slg_001AF690 — test_startup_load_gaps_reference | em_game.c em_game_legacy_state0 / em_scene_bindings.c w_001AFCA0 (native re-arm; the pool reset 001AF8E0 is translated) | S1_newgame_load* |
| 0x001AF710 | — | BM | live | em_startup_load_gaps em_slg_001AF710 (the boxes' bone-slot stack at every area build, em_area11_boxes.c) — test_startup_load_gaps_reference | only the boxes pop from the stack (CRATES_DRUMS_ORIGINAL.md Limitations) | S1_newgame_load* |
| 0x001AF780 | — | AW | live | em_roger_actor_original em_roger_actor_001AF780 (the boxes' pops) — test_roger_actor_original_reference.py |  | S2_opening |
| 0x001AF800 | — | NM | live | em_actor_pool, em_status_scene_original — test_actor_census_reference.py, test_actor_pool_reference.py, test_status_scene_reference.py |  | S2_opening |
| 0x001AF890 | — | CL | verified-unbound | em_roger_actor_original — test_roger_actor_original_reference.py | bound in em_area11_roger (001AF800's push of the +0x110 slots, census L22); Roger is not freed on the route | S2_opening |
| 0x001AF8E0 | — | NM | live | em_actor_pool.c em_actor_pool_reset_001AF8E0, em_collision_world (class-list half) — test_actor_pool_reference; test_actor_census_reference; test_actor_collision_reference |  | S1_newgame_load* |
| 0x001AFA50 | — | BM | live | em_actor_pool — test_actor_census_reference.py, test_actor_pool_reference.py |  | S1_newgame_load |
| 0x001AFA90 | — | BM | live | em_actor_pool, em_player_closure_0e_18 — test_actor_census_reference.py, test_actor_pool_reference.py |  | S1_newgame_load |
| 0x001AFBC0 | — | BM | live | em_actor_pool — test_actor_census_reference.py, test_actor_pool_reference.py |  | S2_opening |
| 0x001AFC10 | — | BM | live | em_actor_pool.c em_actor_pool_free_001AFC10 — test_actor_pool_reference; test_actor_census_reference |  | S2_opening |
| 0x001AFCA0 | — | BM | verified-unbound | em_startup_load_gaps em_slg_001AFCA0 — test_startup_load_gaps_reference | em_scene_bindings.c w_001AFCA0: the port's own re-arm (player_states_reset, the stage bind, the collision world load) plus em_game_legacy_state0 | S1_newgame_load* |
| 0x001AFCF0 | — | BM | live | em_scene_task, em_scene_bindings — test_room_move_reference.py, test_scene_task_reference.py |  | S1_newgame_load |
| 0x001AFD70 | — | BM | live | em_actor_pool.c em_actor_pool_walk_001AFD70 — test_actor_pool_reference; test_actor_census_reference | nodes run legacy code or no code per em_area11_bindings.c | S2_opening |
| 0x001AFE60 | — | BM | live | em_status_scene_original.c via em_status_models / host — test_status_scene_reference |  | 01_battery |
| 0x001AFEB0 | — | BM | live | em_status_scene_original.c via em_status_models / host — test_status_scene_reference |  | 01_battery |
| 0x001B0070 | — | BM | live | em_player_stage_workers (0015D100's read of the canonical D_008106C8 word, bound by em_player_stage_live, L01), em_head_sprite_original — test_player_stage_workers_reference.py, test_head_sprite_reference.py |  | S0_title |
| 0x001B0080 | — | BM | live | em_script_door_fan em_sdf_001B0080 (001B0460's worker, em_camera_live) — test_script_door_fan_reference; test_level_smoke.py (first_control seat row) | live since census L13..L16 (1 call, inside 001B0460 at the area build) | S1_newgame_load |
| 0x001B0250 | — | BM | live | em_spawn_table, em_script_host_workers — test_spawn_place_reference.py |  | S1_newgame_load |
| 0x001B0460 | — | BM | live | em_script_host_workers em_script_host_001B0460 via em_camera_live_001B0460 (spawn_w_001B0460) — test_script_host_workers_reference; test_level_smoke.py (first_control: the seat row f2639 exact) | live since census L13..L16 (1 call per area build); em_game.c em_game_legacy_camera_rearm only for scenes without an original roster | S1_newgame_load |
| 0x001B07C0 | — | BM | live | em_spawn_table, em_scene_bindings — test_spawn_place_reference.py | reads D_00810707 from the canonical progress byte (HK) | S1_newgame_load |
| 0x001B0B50 | — | BM | live | em_player_closure_10_12_19, em_script_host_workers — test_player_closure_10_12_19_reference.py, test_script_host_workers_reference.py; test_level_smoke.py (first_control seat row) | live since census L13..L16 as 001B0460's worker (1 call) | S1_newgame_load |
| 0x001B0DC0 | — | BM | live | em_owner_services_original — test_owner_services_reference.py | recount 2026-09-25: em_owner_services_001B0DC0 executed on the live path (6 calls over the five measured runs) | S2_opening* |
| 0x001B0EA0 | — | NM | live | em_owner_services_original (the boxes' allocation over the exported bank, em_area11_boxes.c) — test_owner_services_reference.py; test_collision_world_capture.py (+0x09 / +0x0C / +0x44 bound) |  | S2_opening* |
| 0x001B0F60 | — | BM | verified-unbound | em_startup_load_gaps em_slg_001B0F60 — test_startup_load_gaps_reference | not bound | S2_opening* |
| 0x001B0FD0 | — | BM | live | em_crate_original / em_drum_original state 0 (001B0EA0, bone_init, +4 += 1); the truck 00823FF0 state 0 (em_owner_services_001B0FD0 via em_area11_boxes, census L23) — test_crate_original_reference.py, test_drum_original_reference.py, test_owner_services_reference.py; test_level_smoke.py (truck_crossing: the truck header +0x09 / +0x0C) |  | S2_opening* |

### 3.3 Input block and roster spawn (0x1B5000..0x1B6BEF)

14 functions, 865 instructions: live 12, verified-unbound 2 (recount 2026-09-25).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001B57E0 | — | BM | verified-unbound | em_startup_load_gaps em_slg_001B57E0 — test_startup_load_gaps_reference | em_frame.c em_frame_scene_input / frame_input_read (step C) | S0_title |
| 0x001B5940 | — | AI | live | em_input.c / em_frame.c pad block — test_input_block_reference |  | S0_title |
| 0x001B5B70 | — | BM | live | em_owner_services_original em_owner_services_001B5B70 via em_pad_actuator at main-loop step I (census L23) — test_owner_services_reference; test_level_smoke.py (truck_crossing: the rumble ends, pad block +0x16 / +0x28 = 0 as route 08's end) |  | S0_title |
| 0x001B5C90 | — | AI | live | em_input.c / em_frame.c pad block — test_input_block_reference |  | 00_panel_no_battery |
| 0x001B5CC0 | — | AI | live | em_input.c / em_frame.c pad block — test_input_block_reference |  | S0_title |
| 0x001B5D70 | — | AW | live | em_input.c / em_frame.c pad block — test_input_block_reference |  | 00_panel_no_battery |
| 0x001B5E20 | — | AW | live | em_input.c / em_frame.c pad block — test_input_block_reference |  | 03_panel_power |
| 0x001B5F40 | — | AW | verified-unbound | em_startup_load_gaps em_slg_001B5F40 (+ em_slg_001B62A0) — test_startup_load_gaps_reference | em_frame.c frame_input_read (pad state byte) | S0_title |
| 0x001B61C0 | — | BM | live | em_player_ladder_entry em_player_rumble_001B61C0 via em_pad_actuator (the typed pad view over D_00810E40; census L23) — test_owner_services_reference.py, test_player_fall_reference.py, test_player_ladder_climb_reference.py; test_level_smoke.py (truck_crossing) | the player states' 001B61C0 workers (em_player_closure_live w_rumble: landings, hits, ladder rungs; em_player_stage_live's 0015D000 heartbeat) call the same em_pad_actuator_001B61C0 since census L23 (off the smoke's route) | S2_opening |
| 0x001B6250 | — | BM | live | em_script_host_workers em_script_host_001B6250 via em_pad_actuator (001B5B70's stop; census L23) — test_owner_services_reference.py, test_script_host_workers_reference.py; test_level_smoke.py (truck_crossing) | the libpad write 00111018 is the platform boundary (em_gamepad_rumble) | S2_opening |
| 0x001B65C0 | — | NM | live | em_actor_roster — test_actor_census_reference.py |  | S1_newgame_load* |
| 0x001B6660 | — | BM | live | em_actor_roster, em_scene_state — test_actor_census_reference.py |  | S1_newgame_load* |
| 0x001B6910 | — | BM | live | em_actor_roster, em_scene_bindings — test_actor_census_reference.py |  | S1_newgame_load* |
| 0x001B6990 | — | BM | live | em_scene_bindings, em_actor_roster — test_actor_census_reference.py |  | S1_newgame_load* |

### 3.4 Player states (0x15B000..0x173FFF)

34 functions, 10,019 instructions: live 28, verified-unbound 5, unverified 1 (recount 2026-09-25).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x0015B130 | — | BM | live | em_player_floor.c em_player_stage_0015B130 (every +4 = 1 stage since L01, em_player.c live_major1) — test_player_floor_reference (stage cases); test_level_smoke.py | state[0]/[1] are 00161020 / 001612D0 since L12 (behind the port's stand-ins); the interaction runtime still stands in for the takeover while it owns the player (acquire / +4 = 4 ticks / release); under 0x70003B8D without that owner the idle / walk states keep the stage (the prelude's +4 = 4 reaches 001837B0 and the record's 00182DF0, not bound) | S2_opening |
| 0x0015B530 | — | AI | verified-unbound | em_player_stage_workers em_player_stage_0015B530 (bound as stage major[4] by L01, but unreached) — test_player_stage_workers_reference | the original runs it on every scripted takeover (12 of 19 labels); the port runs the interaction runtime instead (em_player_pose_host.c player_pose_acquire / the takeover tick through player_pose_stage_hook / player_pose_release), which consumes the stage at 0015B130's prelude position, so the +4 = 4 stage is never entered; reached once the takeover moves onto the stage. Of its routines 001837A0 is bound; 00182DF0's record side, 001837B0, 001838B0, 00183910 are untranslated and 00162DB0/00163B40 are FLOOR (fail-stop workers) | S2_opening |
| 0x0015BA50 | — | BM | live | em_player_floor.c em_player_stage_begin/_dispatch/_end over the record every stage (em_player.c player_states_stage, L01) — test_player_floor_reference (stage cases); test_level_smoke.py | the advance worker is the live display's 001C64F0 (em_player_pose_advance through player_pose_stage_advance); D_00248C98 from the local export; the B3 byte is still em_player_0015BCF0's stand-in expression | S2_opening |
| 0x0015BCF0 | — | BM | live (partial: tail, animate, 00187350) | em_player_floor.c em_player_stage_tail (+BC, the -200 check, the +31B loop-sound stop; L01), em_player_record_pose_animate (the animate step; display step) and 00187350 on the record (L12) inside em_player.c player_states_stage — test_player_floor_reference (stage cases); test_player_record_pose_reference; test-first-control-reference | 0015CBA0 is the camera's translation; the +A0/+B0 copies are the port's own position and camera paths | S2_opening |
| 0x0015BF90 | — | NM | verified-unbound | em_shadow_actor_route — test_shadow_actor_route_reference | no live player shadow | 02_elevator_refusal |
| 0x0015C160 | — | BM | verified-unbound | em_shadow_original — test_shadow_original_reference | live w_0015C160 is a reported no-effect binding (UM_0015C160) | S2_opening |
| 0x0015C1F0 | — | NM | verified-unbound | em_player_misc_workers — test_player_misc_workers_reference | live spawn_w_0015C1F0 is a reported no-effect binding (UM_0015C1F0) | S1_newgame_load |
| 0x0015C310 | — | BM | live | em_area11_bindings.c em_area11_spawn_player_children_0015C420 — compare_frame_order.py; test_level_smoke.py (census 49) | spawn set and order only (node bytes not compared) | S2_opening |
| 0x0015C420 | — | BM | live | em_area11_bindings.c em_area11_spawn_player_children_0015C420 — compare_frame_order.py; test_level_smoke.py (census 49) | spawn set and order only | S2_opening* |
| 0x0015CBA0 | — | BM | live | em_camera_leftovers em_camleft_0015CBA0 (em_player.c after the stage tail) — test_camera_leftovers_reference; test_level_smoke.py camera rows (census 1.11) | live since census L13..L16 (10706 calls): +0x236 from the player state byte | S2_opening |
| 0x0015CF90 | — | BM | unverified | em_player_frame.c em_player_0015BCF0: D_00810707 = +0x234 into the canonical progress byte (HK) and the B9 write, over the stage's vitals (em_player.c stores +220/+234 back to g.status / g.pd_infected after every stage, L01) | D_00810706/858/85C have no canonical storage (their port copies g.pd_low / g.status are the stage's store); no oracle executes 0015CF90 (the byte-matched C was read) | S2_opening |
| 0x0015D000 | — | AI | live | em_player_stage_workers em_player_stage_heartbeat (0015B130, L01) — test_player_stage_workers_reference | its rumble 001B61C0 (health <= 35) is a fail-stop worker (untranslated) | S2_opening |
| 0x0015D100 | — | BM | live | em_player_stage_workers em_player_stage_drain (0015B130, L01; em_player_damage.c's copy retired) — test_player_stage_workers_reference | 0015C9D0 and 001F0060 (the latch / infected paths) are fail-stop workers | S2_opening |
| 0x0015D2F0 | — | BM | verified-unbound | em_player_equipment em_player_equipment_0015D2F0 — test_player_equipment_reference | em_weapon.c assumes variant 0 (ordinary camera mode) | S0_title |
| 0x0015D4C0 | — | NM | live | em_player_ladder_entry — test_player_ladder_entry_reference; test_level_smoke.py check_boxes (route 05 row for row) | bound since the Boxes step (em_player_closure_live; the Use chain's climb / surface probes); Use chain | 05_boxes |
| 0x0015DEC0 | — | AW | live | em_player_climb — test_player_climb_reference; test_level_smoke.py check_boxes (route 05 row for row) | bound since the Boxes step (em_player_closure_live; the Use chain's climb / surface probes) | 05_boxes |
| 0x0015DF10 | — | NM | live | em_player_climb em_player_climb_live_ledge (00160220's ledge probes, em_player_closure_live) — test_player_climb_reference; test_level_smoke.py check_boxes (route 05 row for row) |  | 05_boxes |
| 0x0015EC50 | — | NM | live | em_player_running_jump — test_player_running_jump_reference; test_level_smoke.py check_crevice_jump (route 12 row for row) | live since census L11 (em_player_closure_live: 00160220's running-jump probe); measured executing in the full smoke (census 1.9) | 12_crevice_jump |
| 0x00160220 | — | BM | live | em_player_use_dispatch em_player_use_00160220 over the live record (em_player_closure_live, scan = the host's 00184BA0) — test_player_use_dispatch_reference; test_level_smoke.py (01..05) |  | S3_first_control_idle |
| 0x001607D0 | — | BM | live | em_player_weapon_states_a em_player_weapon_001607D0 (the idle / walk states' `actions`, census L12) — test_player_weapon_states_a_reference | the stances it enters (+5 = 0x1D..0x22) run the port's stand-ins (em_weapon aim / R2 / melee, L28); D_00810C61 (its armed forwarding) is em_weapon's | S3_first_control_idle |
| 0x00161020 | — | NM | live | em_locomotion_display em_loco_00161020 over the record (0015B130 state[0], em_player_closure_live.c bind_loco; census L12) — test_locomotion_display_reference; test-first-control-reference (56 callbacks, the record against collision_run_poll.json); test_level_smoke.py | the port's stand-ins (legacy door, examine / director lock, stance / R2 / melee: L18, L21, L28) pre-empt it while they hold the player; the legacy idle callback remains only in the scenes without an original world | S2_opening |
| 0x001612D0 | — | NM | live | em_locomotion_display em_loco_001612D0 over the record (0015B130 state[1]; census L12) — test_locomotion_display_reference; test-first-control-reference (newgame-control travels 9.599849, the original's); test_level_smoke.py | as 00161020 (stand-ins pre-empt; legacy walk only without an original world) | 00_panel_no_battery |
| 0x00161690 | — | BM | live | em_player_climb — test_player_climb_reference; test_level_smoke.py check_boxes (route 05 row for row) | bound since the Boxes step (em_player_closure_live; the Use chain's climb / surface probes) | 05_boxes |
| 0x00161790 | — | BM | live | em_player_climb (state 2, em_player_closure_live) — test_player_climb_reference; test_level_smoke.py check_boxes (route 05 row for row) |  | 05_boxes |
| 0x00162DB0 | — | NM | live | em_player_fall — test_player_fall_reference; test_level_smoke.py check_fall (the step-offs of routes 10, 11, 12 row for row) | live since census L09..L11 (state 5, the walks' step-offs); measured executing in the full smoke (census 1.9) | 10_cage_roof_roger |
| 0x001634A0 | — | NM | live | em_player_running_jump — test_player_running_jump_reference; test_level_smoke.py check_crevice_jump (route 12 row for row) | live since census L11 (state 6 on the live record); measured executing in the full smoke (census 1.9) | 12_crevice_jump |
| 0x00163B40 | — | AW | live | em_player_fall — test_player_fall_reference; test_level_smoke.py check_fall (the step-offs of routes 10, 11, 12 row for row) | live since census L09..L11 (state 8, the landings); measured executing in the full smoke (census 1.9) | 10_cage_roof_roger |
| 0x00163C10 | — | BM | live | em_player_fall — test_player_fall_reference; test_level_smoke.py check_fall (the step-offs of routes 10, 11, 12 row for row) | live since census L09..L11 (the landing body, clip 0x6E); measured executing in the full smoke (census 1.9) | 10_cage_roof_roger |
| 0x00165B60 | — | BM | live | em_player_ladder_entry — test_player_ladder_entry_reference; test_level_smoke.py check_cage_ladders (route 10 row for row) | live since census L09 (state 0xB on the live record); measured executing in the full smoke (census 1.9) | 10_cage_roof_roger |
| 0x001662D0 | — | NM | live | em_player_ladder_climb — test_player_ladder_climb_reference; test_level_smoke.py check_cage_ladders (route 10 row for row) | live since census L10 (state 0xC on the live record); measured executing in the full smoke (census 1.9) | 10_cage_roof_roger |
| 0x0016C520 | — | BM | live | em_player_slide — test_player_slide_reference (every field and worker call; world mode replays route 06); test_level_smoke.py (06_hill_slide row for row, census L03) | live since census L03: em_player_slide_live_state is 0015B130's state[0x1C] (em_player_closure_live.c); reached by the level smoke's slide phase | 06_hill_slide |
| 0x0016C570 | — | BM | live | em_player_slide — test_player_slide_reference (every field and worker call; world mode replays route 06); test_level_smoke.py (06_hill_slide row for row, census L03) | live since census L03: em_player_slide_live_state is 0015B130's state[0x1C] (em_player_closure_live.c); reached by the level smoke's slide phase | 06_hill_slide |
| 0x0016C6A0 | — | NM | live | em_player_slide — test_player_slide_reference (every field and worker call; world mode replays route 06); test_level_smoke.py (06_hill_slide row for row, census L03) | live since census L03: em_player_slide_live_state is 0015B130's state[0x1C] (em_player_closure_live.c); reached by the level smoke's slide phase | 06_hill_slide |
| 0x0016CD70 | — | BM | live | em_player_slide — test_player_slide_reference (every field and worker call; world mode replays route 06); test_level_smoke.py (06_hill_slide row for row, census L03) | live since census L03: em_player_slide_live_state is 0015B130's state[0x1C] (em_player_closure_live.c); reached by the level smoke's slide phase | 06_hill_slide |

### 3.5 Player workers, pose and animation glue (0x174000..0x18AFFF)

84 functions, 9,097 instructions: live 70, verified-unbound 14 (recount 2026-09-25).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001749A0 | — | BM | live | em_pose_host_workers em_pose_host_001749A0 on the player record (em_player_record_pose; em_player_pose_host.c frame-0 requests: idle, acquire, Use, fidget, tier-2 stop) — test_player_record_pose_reference; test_pose_host_workers_reference; test_player_pose_live_reference (first-control trace) | em_player_pose.c em_player_pose_select still poses Roger and the status models | S2_opening |
| 0x001749F0 | anim_clip_arbiter | BM | live | em_pose_host_workers em_pose_host_001749F0 on the player record (em_player_record_pose; em_player_pose_host.c source-frame requests: walk entry, run stop, gait tier change) — test_player_record_pose_reference; test_pose_host_workers_reference; test_player_pose_live_reference |  | 00_panel_no_battery |
| 0x00174A50 | — | BM | live | em_player_pose_host.c player_pose_acquire (001749A0(p, 0, 0, 8.0) on the record) — test_player_pose_host_reference; test_player_record_pose_reference | the stage's translation em_player_stage_row_request is the idle / walk states' `row_request` since census L12 (00161020 case 0 / 0x63, 001756E0; 46 calls in the full smoke), over the bound 0017B490 | S2_opening |
| 0x00174AB0 | — | BM | live | em_player_ladder_climb (one owner since 2026-09-24; the closure states run it through a bridge) — test_player_ladder_climb_reference, test_player_closure_0e_18_reference; test_level_smoke.py check_cage_ladders (route 10 row for row) | live since census L10 (the climb's clip request 001749A0(p, 0, 1, 0.0)); measured executing in the full smoke (census 1.9) | 01_battery |
| 0x00174AC0 | — | BM | live | em_player_heading_record em_player_heading_record_worker_result (the idle / walk states' `heading`, census L12; the closure's states) — test_player_heading_record_reference; test_locomotion_display_reference (captured images, stick held); test-first-control-reference (+C4 exact wherever the camera input D_008106A0 is) | em_player_heading.c and the em_player.c turn serve only the legacy scenes and the examine stand-in's face step | S3_first_control_idle |
| 0x00174FD0 | — | BM | live | em_player_record_helpers em_player_record_00174FD0 through em_player_slide — test_player_slide_reference, test_player_record_helpers_reference; test_level_smoke.py (06_hill_slide row for row, census L03) | live since census L03: em_player_slide_live_state is 0015B130's state[0x1C] (em_player_closure_live.c); reached by the level smoke's slide phase (0017F5F0 steering) | 06_hill_slide |
| 0x001751A0 | — | NM | live | em_player_recovery — test_player_recovery_reference; test_level_smoke.py check_crevice_jump (route 12 row for row) | live since census L11 (the jump's per-frame stick quadrant, em_player_recovery over the record); measured executing in the full smoke (census 1.9) | 12_crevice_jump |
| 0x00175640 | — | BM | live | em_actor_collision (em_player_link_00175640) — test_player_floor_reference | reached live since census L03 (the slide's floor service on the hill); test_level_smoke.py (06_hill_slide row for row, census L03) | 06_hill_slide |
| 0x001756E0 | — | NM | live | em_player_floor.c em_player_clearance_release over the record (player_states_clearance_release: the idle / walk states' `clearance`; +236 / +235 stored before its 00174A50) — test_player_probe_reference |  | S2_opening |
| 0x00175900 | — | NM | live | em_player_floor.c floor service over the collision world (FLOOR engaged) — test_player_floor_reference; test_level_smoke.py (post-release Y equals the captures) |  | S2_opening |
| 0x00175CF0 | — | NM | live | em_player_floor.c floor service (FLOOR engaged) — test_player_floor_reference; test_level_smoke.py |  | S2_opening |
| 0x001760C0 | — | BM | live | em_player_misc_workers — test_player_misc_workers_reference | recount 2026-09-25: em_actor_collision_player_001760C0 executed on the live path (36676 calls over the five measured runs) | S2_opening |
| 0x001762E0 | — | BM | live | em_player_floor.c wall probes — test_player_probe_reference | its area-2 target-shove worker faults if reached | 01_battery |
| 0x00176390 | — | NM | live | em_player_floor.c em_player_wall_probes — test_player_probe_reference |  | 01_battery |
| 0x001764E0 | — | NM | live | em_player_floor.c em_player_wall_probes — test_player_probe_reference |  | S2_opening |
| 0x00176BE0 | — | AW | live | em_player_floor.c em_player_wall_probes — test_player_probe_reference |  | 01_battery |
| 0x00176C80 | — | BM | live | em_player_floor.c em_player_wall_probes — test_player_probe_reference |  | S2_opening |
| 0x00176F90 | — | BM | live | em_player_ladder_entry — test_player_ladder_entry_reference; test_level_smoke.py check_boxes (route 05 row for row) | bound since the Boxes step (em_player_closure_live; the Use chain's climb / surface probes) | 05_boxes |
| 0x00177030 | — | NM | live | em_player_ladder_entry — test_player_ladder_entry_reference; test_level_smoke.py check_cage_ladders (route 10 row for row) | live since census L09 (mode 4 over the node axis the EMCL axis section carries); measured executing in the full smoke (census 1.9) | 10_cage_roof_roger |
| 0x00177460 | — | AW | live | em_player_climb — test_player_climb_reference; test_level_smoke.py check_boxes (route 05 row for row) | bound since the Boxes step (em_player_closure_live; the Use chain's climb / surface probes) | 05_boxes |
| 0x00177510 | — | BM | live | em_player_climb — test_player_climb_reference; test_level_smoke.py check_boxes (route 05 row for row) | bound since the Boxes step (em_player_closure_live; the Use chain's climb / surface probes) | 05_boxes |
| 0x001775E0 | — | BM | live | em_player_climb — test_player_climb_reference; test_level_smoke.py check_boxes (route 05 row for row) | bound since the Boxes step (em_player_closure_live; the Use chain's climb / surface probes) | 05_boxes |
| 0x00177F40 | — | NM | live | em_player_climb — test_player_climb_reference; test_level_smoke.py check_boxes (route 05 row for row) | bound since the Boxes step (em_player_closure_live; the Use chain's climb / surface probes) | 05_boxes |
| 0x00178B90 | — | AI | live | em_player_recovery em_player_recovery_translate_worker (the idle / walk states' `translate`, census L12) — test_player_recovery_reference; test-first-control-reference (the feet +A0 exact on 56 callbacks) |  | 00_panel_no_battery |
| 0x00178EC0 | — | BM | live | em_player_recovery — test_player_recovery_reference; test_level_smoke.py check_crevice_jump (route 12 row for row) | live since census L11 (the jump's sideways impulse); measured executing in the full smoke (census 1.9) | 12_crevice_jump |
| 0x001791D0 | — | BM | live | em_player_slide — test_player_slide_reference (every field and worker call; world mode replays route 06); test_level_smoke.py (06_hill_slide row for row, census L03) | live since census L03: em_player_slide_live_state is 0015B130's state[0x1C] (em_player_closure_live.c); reached by the level smoke's slide phase | 06_hill_slide |
| 0x00179450 | — | BM | live | em_player_floor.c fall check (gated) — test_player_floor_reference | reached live since census L03 (the fall check under the slide); test_level_smoke.py (06_hill_slide row for row, census L03) | 06_hill_slide |
| 0x00179680 | — | BM | live | em_player_floor.c fall check (gated) — test_player_floor_reference; test_level_smoke.py check_fall (the step-offs of routes 10, 11, 12 row for row) | live since census L09..L11 (the fall entry em_player_fall_enter); measured executing in the full smoke (census 1.9) | 10_cage_roof_roger |
| 0x001796C0 | — | NM | live | em_player_floor.c fall check (FLOOR engaged) — test_player_floor_reference; test_level_smoke.py |  | S2_opening |
| 0x00179880 | — | BM | live | em_player_fall `em_player_fall_00179880` (the one translation; the reaction, running-jump and 10_12_19 lanes call it) — test_player_fall_reference, test_player_reaction_reference, test_player_running_jump_reference; test_level_smoke.py check_fall (the step-offs of routes 10, 11, 12 row for row); test_level_smoke.py check_crevice_jump (route 12 row for row) | live since census L09..L11 (the fall and the jump's drop accumulator); measured executing in the full smoke (census 1.9) | 10_cage_roof_roger |
| 0x001798D0 | — | BM | live | em_player_use_dispatch em_player_use_001798D0 (em_player_closure_live) — test_player_use_dispatch_reference; test_level_smoke.py (01..04) | the port's own locomotion bookkeeping is player_pose_use_accepted_port (no record write); the stand-in player_pose_use_accepted is retired | 00_panel_no_battery |
| 0x00179B90 | — | BM | live | em_player.c footstep_rand5 / em_weapon wpn_rand over em_random — test_player_random_reference |  | 00_panel_no_battery |
| 0x00179D20 | — | BM | live | em_locomotion_display em_loco_00179D20 (0017B660's seeds; census L12) — test_locomotion_display_reference |  | 00_panel_no_battery |
| 0x00179FF0 | — | BM | live | em_locomotion_display em_loco_00179FF0 (0017B660; census L12) — test_locomotion_display_reference |  | 00_panel_no_battery |
| 0x0017B460 | — | BM | live | em_locomotion_display em_loco_0017B460 (inside 0017B490, over the exported D_00248AB0 rows) — test_locomotion_display_reference |  | S2_opening |
| 0x0017B490 | — | BM | live | em_locomotion_display em_loco_0017B490 over the exported D_00248AB0 rows (the idle / walk states, 0017B910, the stage callees' clip lookup, 0017C440 / 00174A50) — test_locomotion_display_reference |  | S2_opening |
| 0x0017B5C0 | — | BM | live | em_locomotion_display em_loco_0017B5C0 (00161020 case 1; census L12) — test_locomotion_display_reference; test-first-control-reference |  | 00_panel_no_battery |
| 0x0017B660 | anim_matrix_player | NM | live | em_locomotion_display em_loco_0017B660 (0017C030 case 1; the pose buffers D_00287F40 / D_00288D40 mapped on the record pose) — test_locomotion_display_reference; test-first-control-reference (+204 / +208 / clip / clock through the tier ramp) | the legacy gait blend runs only in the stand-ins' and the legacy scenes' display | 00_panel_no_battery |
| 0x0017B910 | — | NM | live | em_player_foot_stop.c em_player_foot_stop_0017B910 over the record (0017C030 case 3; census L12) — test_player_loco_workers_reference (record, 0x70003A20..2C / 36A0 / 38A0..BF and calls against the executed original) | the mirror em_player_foot_stop_begin / player_pose_foot_stop_begin serve only the legacy scenes | 02_elevator_refusal |
| 0x0017BC40 | — | BM | live | em_player_motor.c em_player_motor_0017BC40 over the record (the tables read from the exported span; 0x70003A20 written; census L12) — test_player_loco_workers_reference; test_player_motor_reference (the first-control speed sequence); test-first-control-reference | em_player_motor_tick (the mirror adapter over it) serves only the legacy scenes | 00_panel_no_battery |
| 0x0017C030 | — | BM | live | em_locomotion_display em_loco_0017C030 (all eight cases; 001612D0) — test_locomotion_display_reference; test-first-control-reference (cases 1, 3, 4, 5 on the route) | the skid cases 6 / 7 have no live capture (no route beat reverses the stick above speed 0.5) | 00_panel_no_battery |
| 0x0017C440 | — | BM | live | em_player_use_dispatch em_player_reentry_0017C440 over the record (the idle / walk states' and the closure's `reentry`) — test_player_use_dispatch_reference; test-first-control-reference (the interrupted stop, 8 callbacks) | em_player_motor.c em_player_reentry_begin / _tick (metadata) serve only the legacy scenes | 10_cage_roof_roger |
| 0x0017C540 | — | BM | live | em_player_reaction em_player_reaction_0017C540 (em_pose_host_handoff; the idle / walk states' and the closure's `handoff`) — test_player_reentry_reference.py; test-first-control-reference | em_player_motor.c em_player_reentry_tick serves only the legacy scenes | 05_boxes |
| 0x0017C580 | — | NM | live | em_player_fall — test_player_fall_reference; test_level_smoke.py check_crevice_jump (route 12 row for row) | live since census L11 (the jump's landing, w_land); measured executing in the full smoke (census 1.9) | 10_cage_roof_roger |
| 0x0017C860 | — | NM | live | em_player_recovery — test_player_recovery_reference; test_level_smoke.py check_crevice_jump (route 12 row for row) | live since census L11 (the jump's ledge-grab probe); measured executing in the full smoke (census 1.9) | 12_crevice_jump |
| 0x0017D800 | — | BM | live | em_player_climb — test_player_climb_reference; test_level_smoke.py check_boxes (route 05 row for row) | bound since the Boxes step (em_player_closure_live; the Use chain's climb / surface probes) | 05_boxes |
| 0x0017D8D0 | — | BM | live | em_player_climb — test_player_climb_reference; test_level_smoke.py check_boxes (route 05 row for row) | bound since the Boxes step (em_player_closure_live; the Use chain's climb / surface probes) | 05_boxes |
| 0x0017DEB0 | — | AW | live | em_player_climb — test_player_climb_reference; test_level_smoke.py check_boxes (route 05 row for row), check_crevice_jump | bound since the Boxes step (em_player_closure_live; the Use chain's climb / surface probes); since census L11 the running jump's landing calls the same translation standalone over the record (em_player_climb_live_0017DEB0) | 05_boxes |
| 0x0017F5F0 | — | NM | live | em_player_slide — test_player_slide_reference (every field and worker call; world mode replays route 06); test_level_smoke.py (06_hill_slide row for row, census L03) | live since census L03: em_player_slide_live_state is 0015B130's state[0x1C] (em_player_closure_live.c); reached by the level smoke's slide phase | 06_hill_slide |
| 0x0017FC80 | — | BM | live | em_player_ladder_climb (one owner; the ladder entry runs it through a bridge) — test_player_ladder_climb_reference, test_player_ladder_entry_reference; test_level_smoke.py check_cage_ladders (route 10 row for row) | live since census L10; measured executing in the full smoke (census 1.9) | 10_cage_roof_roger |
| 0x0017FD00 | — | BM | live | em_player_ladder_climb — test_player_ladder_climb_reference; test_level_smoke.py check_cage_ladders (route 10 row for row) | live since census L10 (the climb's clip pair); measured executing in the full smoke (census 1.9) | 10_cage_roof_roger |
| 0x00180300 | — | BM | live | em_player_ladder_entry (one owner since 2026-09-24; the closure states run it through `em_player_ladder_probe_00180300`) — test_player_ladder_entry_reference, test_player_closure_0e_18_reference; test_level_smoke.py check_cage_ladders (route 10 row for row) | live since census L10 (the climb's attribute probe); measured executing in the full smoke (census 1.9) | 10_cage_roof_roger |
| 0x00180420 | — | BM | live | em_player_ladder_climb (one owner; the closure states run it through a bridge) — test_player_ladder_climb_reference, test_player_closure_0e_18_reference; test_level_smoke.py check_cage_ladders (route 10 row for row) | live since census L10; measured executing in the full smoke (census 1.9) | 10_cage_roof_roger |
| 0x00180460 | — | BM | live | em_player_ladder_climb — test_player_ladder_climb_reference; test_level_smoke.py check_cage_ladders (route 10 row for row) | live since census L10; measured executing in the full smoke (census 1.9) | 10_cage_roof_roger |
| 0x00181110 | — | AW | live | em_player_major2 — test_player_major2_reference; test_level_smoke.py check_cage_ladders (route 10 row for row) | live since census L10 (major2's 00181110 inside the climb); measured executing in the full smoke (census 1.9) | 10_cage_roof_roger |
| 0x00182430 | — | AW | live | em_player_floor em_player_step_sounds, the one translation: the footstep dispatch 00187350 (em_player_footstep_tick), the legacy step clock em_player.c footstep_play and the record's standalone callers (the slide's skid steps; the ladder and closure states) through em_player_closure_live.c x_surface_sound — test_player_footstep_reference, test_player_random_reference; test_level_smoke.py (06_hill_slide row for row, census L03) | the walking step clock is still legacy (00187350 verified-unbound); em_player.c's own copy of the mapper (footstep_block) is retired | 00_panel_no_battery |
| 0x00182870 | — | BM | live | em_player_reaction — test_player_reaction_reference | reached live since the Boxes step (the climb's 0017DEB0) and on the slide landing (0016C6A0 sub-state 0xA); test_level_smoke.py (06_hill_slide row for row, census L03) | 05_boxes |
| 0x00182A70 | — | BM | live | em_player_ladder_entry — test_player_ladder_entry_reference; test_level_smoke.py check_cage_ladders (route 10 row for row) | live since census L09; measured executing in the full smoke (census 1.9) | 10_cage_roof_roger |
| 0x00182B30 | — | BM | verified-unbound | em_player_stage_workers em_player_stage_scripted_check (bound in 0015B130's prelude by L01, but unreached) — test_player_stage_workers_reference | em_player_pose_host.c player_pose_acquire / the takeover tick / player_pose_release through the interaction runtime (partial refusal set), which consumes the stage before the prelude (since census L23 the stage writes the admission's +5 = 0, +6 = 0, +1F0 = 0x41 on the acquiring stage and keeps the port's mirrors off the record while the takeover holds it; route 07 row for row); the prelude runs only under 0x70003B8D outside the idle / walk states (which keep those stages: the prelude's +4 = 4 reaches 001837B0 and the record's 00182DF0, not bound). Reached once the takeover moves onto the stage | S2_opening |
| 0x00182BF0 | — | NM | live | em_script_host_workers em_script_host_00182BF0 via em_area11_script_host (op16; census L22) — test_script_host_workers_reference; test_level_smoke.py (roger: route 14 row for row) |  | 10_cage_roof_roger |
| 0x00182D40 | — | BM | verified-unbound | em_locomotion_display em_loco_00182D40 — test_locomotion_display_reference | em_player_pose_host.c release tail (hooked in the pose oracles); its caller 00182DF0 is not translated on the record | S2_opening |
| 0x00182D70 | — | BM | live | em_player_stage_workers em_player_stage_scripted_notify (0015B130's major-1 admission; census L22) — test_player_stage_workers_reference; test_level_smoke.py (panel, elevator, roger: +5 / +1F0 / +1F1 row for row) | link1C is a fail-stop worker (+1C is 0 in every route capture) | S2_opening |
| 0x00182DF0 | — | BM | live | em_player_pose_host.c record_release / record_default (001C63E0 on the record; the D_00248C90 +0 row from assets/player_clip_row0.emch) — test_player_pose_host_reference; test_player_cinematic_reference | the pose host's release sets the healthy row's clip 0 whatever +235 holds (00182DF0's row default through 0017B490 is not used there); the nonzero-+0x2F3 branch (record_release_special: +0x40 = the default bank, D_00248A00[+0x235]) since census L22 | S2_opening |
| 0x00182F90 | — | BM | live | em_player_pose_host.c player_pose_align — test_player_pose_host_reference; test_interaction_alignment_reference |  | S2_opening |
| 0x00183090 | — | BM | live | em_player_stage_workers em_player_stage_commit via player_pose_commit_tick (a script owner's takeover, census L22); em_player_pose.c / em_interaction_animation.c commit for the other takeovers — test_player_cinematic_reference (1388 callbacks against the original 00183090 / 001C64F0); test_level_smoke.py (roger: route 14 row for row) |  | S2_opening |
| 0x001837A0 | — | BM | verified-unbound | em_player_stage_live.c w_001837A0 (0015B530's +5 = 0 routine, bound by L01 but unreached) — the byte-matched src/func_001837A0.c is an empty function; test_player_stage_workers_reference hooks it as 0015B530's target | as 0015B530: the interaction runtime (player_pose_acquire / the takeover tick / player_pose_release) stands in for the takeover, so 0015B530 and its routines are not entered. Reached once the takeover moves onto the stage | S2_opening |
| 0x00183EF0 | — | BM | live | em_interaction_scan.c via em_area11_interaction_host use — test_interaction_scan_reference; test_level_smoke.py (routes 02-04) | em_door.c legacy scan for the unpublished door; the published interactive list (the collision world's, census L07) holds the panel, the terminal, the items and Roger (selector 0 class 10: em_roger_candidate, census L22) | 00_panel_no_battery |
| 0x00184BA0 | — | BM | live | em_interaction_scan.c via em_area11_interaction_host use — test_interaction_scan_reference; test_level_smoke.py (routes 02-04) | em_door.c / Roger legacy scans for the unpublished owners; the published interactive list (the collision world's, census L07) holds the panel, the terminal and the items (W22) | 00_panel_no_battery |
| 0x00187350 | — | BM | live | em_player_floor em_player_footstep_tick over the record after 0015BCF0's animate step (em_player_closure_live_footstep, census L12) — test_player_footstep_reference; test-first-control-reference (+25E / +212 exact on 56 callbacks) | its 001EFD90 spawns go to the counted effect gap (L26); the decal 001F0460 and wading 001E8B90 are fail-stop (not reached on the route) | S2_opening |
| 0x00187DC0 | — | BM | verified-unbound | em_player_floor.c first contact (gated) — test_player_floor_reference | bound since the Boxes step (FLOOR engaged); first reached at 08_truck_crossing, beyond the live smoke | 08_truck_crossing |
| 0x00187EE0 | — | BM | live | em_player_floor — test_player_footstep_reference; test_level_smoke.py check_cage_ladders | inside 00187350 on every step since census L12, and as the ladder dismount's 00187EE0(p, p + B0, p + D0) (x_place, census L10); its 001EFD90 spawns go to the counted effect gap (L26) | 00_panel_no_battery |
| 0x001885D0 | — | BM | live | em_player_ladder_climb — test_player_ladder_climb_reference; test_level_smoke.py check_cage_ladders (route 10 row for row) | live since census L10 (inside 0017FC80); measured executing in the full smoke (census 1.9) | 10_cage_roof_roger |
| 0x00188630 | — | BM | verified-unbound | em_player_equipment em_player_equipment_00188630 — test_player_equipment_reference | em_weapon.c em_weapon_update (legacy gun tick) | S2_opening |
| 0x00188A50 | — | BM | verified-unbound | em_player_equipment em_player_equipment_00188A50 — test_player_equipment_reference | not bound (no port code runs for the equipment nodes) | S2_opening |
| 0x00188AC0 | — | BM | verified-unbound | em_player_equipment em_player_equipment_00188AC0 — test_player_equipment_reference | not bound (no port code runs for the equipment nodes) | S2_opening |
| 0x00188B80 | — | BM | verified-unbound | em_player_equipment em_player_equipment_00188B80 — test_player_equipment_reference | not bound (no port code runs for the equipment nodes) | S2_opening |
| 0x00188DF0 | — | BM | verified-unbound | em_player_equipment em_player_equipment_00188DF0 — test_player_equipment_reference | not bound (no port code runs for the equipment nodes) | S2_opening |
| 0x00188ED0 | — | BM | verified-unbound | em_player_equipment em_player_equipment_00188ED0 — test_player_equipment_reference | em_weapon.c flashlight gate + em_gfx spot term (R03/R04) | S2_opening |
| 0x00189D30 | — | BM | verified-unbound | em_player_equipment em_player_equipment_00189D30 — test_player_equipment_reference | not bound (no port code runs for the equipment nodes) | S2_opening |
| 0x0018A1F0 | — | BM | verified-unbound | em_player_equipment em_player_equipment_0018A1F0 — test_player_equipment_reference | not bound (no port code runs for the equipment nodes) | S2_opening |
| 0x0018A6B0 | — | BM | verified-unbound | em_player_equipment em_player_equipment_tick — test_player_equipment_reference | the pool node is bound with no port code ('player equipment: no port draw') | S2_opening |
| 0x0018A880 | — | BM | live | em_area11_bindings.c spawn_0018A880 — compare_frame_order.py; test_level_smoke.py (census 49) | spawn only | S2_opening |
| 0x0018A8D0 | — | BM | verified-unbound | em_player_equipment em_player_equipment_0018A8D0 — test_player_equipment_reference | not bound | S2_opening |
| 0x0018AB00 | — | BM | live | em_scene_task.c em_sf_0018AB00 — test_room_move_reference |  | 09_fence_door |

### 3.6 Camera (0x18B000..0x199FFF)

27 functions, 7,740 instructions: live 26, verified-unbound 1 (recount 2026-09-25).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x0018B9C0 | — | NM | live | em_camera_leftovers em_camleft_0018B9C0 via em_camera_live_frame (em_render_frame.c em_camera_0018B9C0 / _opening) — test_camera_leftovers_reference; test_level_smoke.py camera rows (census 1.11) | live since census L13..L16 (12004 calls in the full smoke); runs the D_008106EF countdown itself; legacy em_camera.c camera_update remains only for scenes without an original roster (after the level exit) | S2_opening |
| 0x0018BC20 | — | NM | live | em_camera_leftovers em_camleft_0018BC20 — test_camera_leftovers_reference; test_level_smoke.py camera rows (census 1.11) | live since census L13..L16 (8006 calls); em_camera.c camera_mode_dispatch only on the legacy camera | S2_opening |
| 0x0018C0C0 | — | BM | live | em_camera_leftovers em_camleft_0018C0C0 — test_camera_leftovers_reference; test_level_smoke.py camera rows (census 1.11) | live since census L13..L16 (5274 calls) | S2_opening |
| 0x0018C0D0 | — | BM | live | em_camera_commit_original em_camera_commit_0018C0D0 via em_camera_live (the frame, state 4 and 001B0460) — test_camera_live_reference (executes the whole original routine); test_level_smoke.py camera rows (census 1.11) | translated and live since census L13..L16 (12008 calls): D_00810610 / 650 / 690..6A0 and cam+9C, +B0; the earlier host-math camera_commit_original and its hooked test are retired | S1_newgame_load |
| 0x0018C4B0 | — | AW | live | em_camera_follow_original — test_camera_follow_original_reference; test_level_smoke.py camera rows (census 1.11) | live since census L13..L16 (10669 calls); em_camera.c cam_chase_h/v only on the legacy camera | S2_opening |
| 0x0018C5A0 | — | NM | live | em_camera_leftovers em_camleft_0018C5A0 — test_camera_leftovers_reference; test_level_smoke.py camera rows (census 1.11) | live since census L13..L16 (66 calls, camera action 8) | S2_opening |
| 0x0018C6A0 | — | AW | live | em_camera_follow_original — test_camera_follow_original_reference; test_level_smoke.py camera rows (census 1.11) | live since census L13..L16 (10669 calls) | S2_opening |
| 0x0018CBD0 | — | NM | live | em_camera_retarget.c + em_camera_rotation.c — test_camera_retarget_reference; test_camera_rotation_reference; test_level_smoke.py (routes 02/04) |  | 00_panel_no_battery |
| 0x0018CE60 | — | NM | live | em_camera_leftovers_solver em_camleft_0018CE60 — test_camera_leftovers_reference; test_level_smoke.py camera rows (census 1.11) | live since census L13..L16 (1 call on the route); em_game.c cam_bounds_settle_0018CE60 only on the legacy camera | S2_opening* |
| 0x0018D330 | — | BM | live | em_camera_follow_original — test_camera_follow_original_reference; test_camera_interaction_fixture; test_level_smoke.py camera rows (census 1.11) | live since census L13..L16 (5255 calls) with the 0019A910 / 0019B7D0 workers over em_collision_world; em_camera_probe.c is deleted | S2_opening |
| 0x0018D7B0 | — | BM | live | em_camera_follow_original — test_camera_follow_original_reference; test_camera_interaction_fixture (styles 5 / 1); test_level_smoke.py camera rows (census 1.11) | live since census L13..L16 (5255 calls; the state-4 call and the scripted retarget included) | S2_opening |
| 0x0018D910 | — | NM | live | em_camera_leftovers_solver em_camleft_0018D910 (whole routine) — test_camera_leftovers_reference; test_camera_interaction_fixture | live since census L13..L16 (3 calls: the scripted retargets); the partial em_camera_probe.c copy is deleted | 00_panel_no_battery |
| 0x0018DD20 | — | NM | live | em_camera_leftovers_solver em_camleft_0018DD20 — test_camera_leftovers_reference; test_level_smoke.py camera rows (census 1.11) | live since census L13..L16 (5252 calls); em_camera.c cam_solver_0018DD20 only on the legacy camera | S2_opening |
| 0x00190F20 | — | BM | live | em_camera_leftovers em_camleft_00190F20 — test_camera_leftovers_reference; test_level_smoke.py camera rows (census 1.11) | live since census L13..L16 (8006 calls; its quad D_0024A4B0 from assets/camera_tables.emrg through 001B1EA0) | S2_opening |
| 0x00191000 | — | BM | live | em_camera_leftovers em_camleft_00191000 — test_camera_leftovers_reference; test_level_smoke.py camera rows (census 1.11) | live since census L13..L16 (1863 calls) | S3_first_control_idle |
| 0x00191210 | — | BM | live | em_camera_area11_specials — test_camera_area11_specials_reference; test_level_smoke.py camera rows (census 1.11) | live since census L13..L16 (inside 00195130, 5248 calls); D_0081078B canonical | S3_first_control_idle |
| 0x00191390 | — | AW | live | em_camera_follow_original — test_camera_follow_original_reference; test_level_smoke.py camera rows (census 1.11) | live since census L13..L16 (8006 calls); em_camera.c camera_prestep_00191390 only on the legacy camera | S2_opening |
| 0x001914A0 | — | BM | live | em_camera_leftovers em_camleft_001914A0 — test_camera_leftovers_reference; test_level_smoke.py camera rows (census 1.11) | live since census L13..L16 (66 calls) | S2_opening |
| 0x00191580 | — | BM | live | em_camera_leftovers em_camleft_00191580 — test_camera_leftovers_reference; test_level_smoke.py camera rows (census 1.11) | live since census L13..L16 (66 calls) | S2_opening |
| 0x001916C0 | — | NM | live | em_camera_leftovers em_camleft_001916C0 — test_camera_leftovers_reference; test_level_smoke.py camera rows (census 1.11) | live since census L13..L16 (5314 calls) | S2_opening |
| 0x00191D40 | — | NM | live | em_camera_follow_original — test_camera_follow_original_reference; test_level_smoke.py camera rows (census 1.11) | live since census L13..L16 (4751 calls); em_camera.c cam_eye_y_seek_00191D40 only on the legacy camera | S3_first_control_idle |
| 0x00192010 | — | AW | live | em_camera_follow_original — test_camera_follow_original_reference; test_level_smoke.py camera rows (census 1.11) | live since census L13..L16 (377 calls) | 06_hill_slide |
| 0x001921D0 | — | NM | live | em_camera_follow_original — test_camera_follow_original_reference; test_level_smoke.py camera rows (census 1.11) | live since census L13..L16 (5248 calls) | S3_first_control_idle |
| 0x00193EB0 | — | NM | live | em_camera_area11_specials — test_camera_area11_specials_reference; test_level_smoke.py camera rows (census 1.11) | live since census L13..L16 (inside 00195130, 5248 calls) | S3_first_control_idle |
| 0x00195130 | — | NM | live | em_camera_area11_specials — test_camera_area11_specials_reference; test_level_smoke.py camera rows (census 1.11) | live since census L13..L16 (5248 calls; on the other 2692 calls of its slot in the smoke a named stand-in of CAMERA_LIVE.md section 6 held the camera); D_00810803 canonical | S3_first_control_idle |
| 0x00199C50 | — | NM | verified-unbound | em_startup_load_gaps em_slg_00199C50 — test_startup_load_gaps_reference | the live um_00199C50 is a reported no-effect binding (state 0) | S1_newgame_load* |
| 0x00199DB0 | — | NM | live | em_player_ladder_entry — test_player_ladder_entry_reference; test_level_smoke.py check_cage_ladders (route 10 row for row) | live since census L09 (the column node's centre); measured executing in the full smoke (census 1.9) | 10_cage_roof_roger |

### 3.7 Collision walkers (0x19A000..0x1A7FFF)

38 functions, 9,919 instructions: live 37, verified-unbound 1 (recount 2026-09-25).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x0019A180 | — | NM | live | em_player_climb — test_player_climb_reference.py, test_player_recovery_reference.py | recount 2026-09-25: em_player_helper_0019A180 executed on the live path (5 calls over the five measured runs) | 05_boxes |
| 0x0019A310 | — | AW | live | em_player_floor — test_player_floor_reference.py, test_player_probe_reference.py | FLOOR engaged since the Boxes step (em_collision_world_bind_player; EE model) | S2_opening |
| 0x0019A570 | — | NM | live | em_coll_segment_walkers, em_weapon — test_coll_segment_walkers_reference.py, test_drum_original_reference.py, test_player_climb_reference.py | recount 2026-09-25: em_coll_segment_0019A570 executed on the live path (5 calls over the five measured runs) | 02_elevator_refusal |
| 0x0019A910 | — | NM | live | em_coll_segment_walkers — test_coll_segment_walkers_reference.py, test_camera_interaction_fixture.py | the live camera's segment workers (em_camera_live.c fw_segment / lw_segment: 0018D330, 0018D910, 0018DD20 and the specials; 36797 calls in the full smoke, census 1.11) and 00183EF0's item ray (the interaction host) over em_collision_world | S2_opening |
| 0x0019AB20 | — | NM | live | em_actor_collision (EE model; prims and segment state em_coll_probe_original's) — test_actor_collision_reference.py (ground exact), test_coll_probe_reference.py, test_crate_original_reference.py | FLOOR's ground, 001764E0's 001760C0 column and the crates' / drums' probe | S2_opening |
| 0x0019AD00 | — | NM | live | em_coll_move_original (with em_coll_grid_hull) — test_coll_move_reference.py, test_coll_grid_hull_reference.py | 001764E0's move probe and the closure (em_collision_world); the hull world has no chain reader (no AREA11 class-2 owner) | S2_opening |
| 0x0019AFE0 | — | NM | live | em_coll_move_original (with em_coll_grid_hull) — test_coll_move_reference.py, test_coll_grid_hull_reference.py | 001764E0's sweep and the closure (em_collision_world) | S2_opening |
| 0x0019B6C0 | — | NM | live | em_coll_probe_original, em_player_floor — test_coll_probe_reference.py, test_coll_segment_walkers_reference.py, test_player_floor_reference.py | FLOOR engaged since the Boxes step (em_collision_world_bind_player; EE model) | S2_opening |
| 0x0019B7D0 | — | BM | live | em_coll_list_passes_walkers (em_coll_list_passes_0019B7D0) — test_coll_list_passes_reference.py, test_camera_interaction_fixture.py | the live camera's EmCameraFollowWorkers.ground (em_camera_live.c fw_ground, 5255 calls; census 1.11) | S2_opening |
| 0x0019B8C0 | — | NM | live | em_coll_probe_original, em_player_floor — test_coll_probe_reference.py, test_player_floor_reference.py | FLOOR engaged since the Boxes step (em_collision_world_bind_player; EE model) | S2_opening |
| 0x0019BA80 | — | NM | live | em_coll_list_passes_walkers em_coll_list_passes_0019BA80 — test_coll_list_passes_reference.py (the ladder-entry test only hooked it); test_level_smoke.py check_cage_ladders (route 10 row for row); test_level_smoke.py check_boxes | live since census L09: 00176F90's probe in the Use chain (le_probe_0019BA80; the boxes and both ladders); the port's other queries keep em_collision.c; measured executing in the full smoke (census 1.9) | 05_boxes |
| 0x0019BC40 | — | NM | live | em_actor_collision, em_player_running_jump — test_actor_collision_reference.py, test_player_climb_reference.py, test_player_floor_reference.py | FLOOR engaged since the Boxes step (em_collision_world_bind_player; EE model) | 05_boxes |
| 0x0019C830 | — | NM | live | em_coll_probe_original em_coll_probe_0019C830 (0019AB20's grid pass over the EMCL rank section) — test_actor_collision_reference.py (ground exact), test_coll_probe_reference.py | the former em_collision.c grid walk is deleted | S2_opening |
| 0x0019CB60 | — | NM | live | em_coll_grid_hull (the move walkers' grid pass) — test_coll_grid_hull_reference.py, test_coll_move_reference.py |  | S2_opening |
| 0x0019D330 | — | NM | live | em_coll_segment_walkers, em_enemy — test_coll_segment_walkers_reference.py, test_collision_reference.py | recount 2026-09-25: em_coll_segment_0019D330 executed on the live path (5 calls over the five measured runs) | 02_elevator_refusal |
| 0x0019D770 | — | NM | live | em_coll_segment_walkers — test_coll_segment_walkers_reference.py | 0019A910's grid walker | S2_opening |
| 0x0019DF10 | — | NM | live | em_coll_probe_original — test_coll_probe_reference.py, test_coll_segment_walkers_reference.py | FLOOR engaged since the Boxes step (em_collision_world_bind_player; EE model) | S2_opening |
| 0x0019E280 | — | BM | live | em_coll_list_passes_walkers — test_coll_list_passes_reference.py, test_camera_interaction_fixture.py | 0019B7D0's walker for 0018D330's ground probe (em_camera.c interaction_camera_query) | S2_opening |
| 0x0019E640 | — | NM | live | em_coll_probe_original — test_coll_probe_reference.py | FLOOR engaged since the Boxes step (em_collision_world_bind_player; EE model) | S2_opening |
| 0x0019E930 | — | NM | live | em_coll_list_passes_walkers — test_coll_list_passes_reference.py | recount 2026-09-25: em_coll_list_passes_0019E930 executed on the live path (9 calls over the five measured runs) | 05_boxes |
| 0x0019ED80 | — | AW | live | em_coll_probe_original — test_coll_probe_reference.py, test_coll_segment_walkers_reference.py, test_coll_list_passes_reference.py | the node test of the live grid walkers 0019D770 and 0019E280 | S2_opening |
| 0x0019F1A0 | — | NM | live | em_coll_probe_original — test_coll_probe_reference.py, test_coll_segment_walkers_reference.py, test_coll_list_passes_reference.py | the ranks of the live grid walkers 0019D770 and 0019E280 (the installed flags-7 EMCL) | S2_opening |
| 0x0019F330 | — | AW | verified-unbound | em_coll_list_passes_walkers — test_coll_list_passes_reference.py | 0019BC40 pass 2's crossing; em_collision.c column_node still stands in | 05_boxes |
| 0x0019F730 | — | NM | live | em_actor_collision — test_actor_collision_reference.py, test_coll_probe_reference.py | FLOOR engaged since the Boxes step (em_collision_world_bind_player; EE model) | S2_opening |
| 0x0019FE50 | — | NM | live | em_coll_move_original — test_coll_move_reference.py | recount 2026-09-25: em_coll_move_walk_0019FE50 executed on the live path (88348 calls over the five measured runs) | S2_opening |
| 0x001A0B10 | — | NM | live | em_coll_segment_walkers, em_enemy — test_coll_segment_walkers_reference.py | recount 2026-09-25: em_coll_segment_001A0B10 executed on the live path (5 calls over the five measured runs) | 02_elevator_refusal |
| 0x001A1390 | — | NM | live | em_coll_segment_walkers — test_coll_segment_walkers_reference.py | 0019A910's cell walker over the published class-4 cells | S2_opening |
| 0x001A2370 | — | NM | live | em_actor_collision (em_actor_cells_retransform_001A2370) — test_actor_collision_reference.py, test_collision_world_capture.py | the terminal 00827B10 (state 0, the ride's completion), the items 00219550 (state 0) and since census L23 the truck 00823FF0 (every shake and fall tick) through em_collision_world; the drums (L25) and 0x825940 (L24) wait on their owners | S2_opening |
| 0x001A2AE0 | — | NM | live | em_coll_probe_original, em_coll_segment_walkers — test_coll_probe_reference.py, test_coll_segment_walkers_reference.py | FLOOR engaged since the Boxes step (em_collision_world_bind_player; EE model) | S2_opening |
| 0x001A32C0 | — | NM | live | em_coll_probe_original — test_coll_probe_reference.py | FLOOR engaged since the Boxes step (em_collision_world_bind_player; EE model) | S2_opening |
| 0x001A3980 | — | NM | live | em_coll_list_passes_walkers — test_coll_list_passes_reference.py | recount 2026-09-25: em_coll_list_passes_001A3980 executed on the live path (9 calls over the five measured runs) | 05_boxes |
| 0x001A4030 | — | BM | live | em_coll_probe_original — test_coll_probe_reference.py, test_coll_move_reference.py, test_actor_collision_reference.py | 001A1390's n-gon test and 0019AB20's (one owner since the Boxes step: em_actor_collision.c calls it) | 01_battery |
| 0x001A4650 | — | AW | live | em_coll_probe_original, em_actor_collision — test_actor_collision_reference.py, test_coll_probe_reference.py | FLOOR engaged since the Boxes step (em_collision_world_bind_player; EE model) | 00_panel_no_battery |
| 0x001A4D10 | — | AW | live | em_coll_move_original — test_coll_move_reference.py, test_collision_faces_reference.py | recount 2026-09-25: em_coll_move_prim_001A4D10 executed on the live path (2352 calls over the five measured runs) | 04_elevator_ride |
| 0x001A50A0 | — | NM | live | em_coll_segment_walkers, em_coll_probe_original — test_coll_probe_reference.py, test_coll_segment_walkers_reference.py | 001A1390's face prim test (the panel's cell 18); also 001A2AE0's pass-2 worker (gated FLOOR) | 02_elevator_refusal |
| 0x001A5760 | — | AW | live | em_collision.c em_collision_column_box_face (via em_actor_collision) — test_actor_collision_reference.py | FLOOR engaged since the Boxes step (em_collision_world_bind_player; EE model) | 05_boxes |
| 0x001A6440 | — | NM | live | em_coll_grid_hull (the class-0 hull lock) — test_coll_grid_hull_reference.py, test_coll_move_reference.py |  | S2_opening |
| 0x001A7870 | — | NM | live | em_coll_list_passes — test_coll_list_passes_reference.py | 001AAD00's hook; the port's owners publish no class 1/2/0xD entry, so it walks empty lists (records are fail-stop) | S2_opening |

### 3.8 Actor list passes (0x1A8000..0x1AA1FF)

9 functions, 738 instructions: live 8, verified-unbound 1 (recount 2026-09-25).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001A8660 | — | BM | verified-unbound | em_coll_list_passes — test_coll_list_passes_reference.py | reached from the live 001A8BE0 only for a class-0xD type-1 entry; no owner the port runs publishes one (the flame 008235F0; its +0x34 behaviour 0x00823580 and D_0024A740 are fail-stop) | 07_truck_preview |
| 0x001A8BE0 | — | BM | live | em_coll_list_passes — test_coll_list_passes_reference.py | 001AAD00's hook; the port's owners publish no class 1/2/0xD entry, so it walks empty lists (records are fail-stop); em_enemy.c's legacy contact pass still runs for the port's own enemy owners (L25) | S2_opening |
| 0x001A8DA0 | — | BM | live | em_coll_list_passes — test_coll_list_passes_reference.py | 001AAD00's hook; the port's owners publish no class 1/2/0xD entry, so it walks empty lists (records are fail-stop) | S2_opening |
| 0x001A9000 | — | BM | live | em_coll_list_passes — test_coll_list_passes_reference.py | 001AAD00's hook; the port's owners publish no class 1/2/0xD entry, so it walks empty lists (records are fail-stop); em_enemy.c's legacy contact pass still runs for the port's own enemy owners (L25) | S2_opening |
| 0x001A97B0 | — | BM | live | em_coll_list_passes — test_coll_list_passes_reference.py | 001AAD00's hook; the port's owners publish no class 1/2/0xD entry, so it walks empty lists (records are fail-stop) | S2_opening |
| 0x001A9B10 | — | BM | live | em_coll_list_passes — test_coll_list_passes_reference.py | 001AAD00's hook; the port's owners publish no class 1/2/0xD entry, so it walks empty lists (records are fail-stop) | S2_opening |
| 0x001A9D20 | — | BM | live | em_coll_list_passes — test_coll_list_passes_reference.py | 001AAD00's hook; the port's owners publish no class 1/2/0xD entry, so it walks empty lists (records are fail-stop) | S2_opening |
| 0x001A9F60 | — | BM | live | em_coll_list_passes — test_coll_list_passes_reference.py | 001AAD00's hook; the port's owners publish no class 1/2/0xD entry, so it walks empty lists (records are fail-stop) | S2_opening |
| 0x001AA140 | — | BM | live | em_coll_list_passes — test_coll_list_passes_reference.py | 001AAD00's hook; the port's owners publish no class 1/2/0xD entry, so it walks empty lists (records are fail-stop) | S2_opening |

### 3.9 Entity owners: crates, drums, panel, pickups (0x130000..0x15AFFF)

10 functions, 2,678 instructions: live 9, unverified 1 (recount 2026-09-25).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001551B0 | — | NM | live | em_crate_original over its roster node (em_area11_boxes.c) — test_crate_original_reference, test_collision_world_capture.py (records equal route 04), test_level_smoke.py (05_boxes) | the damage and nest paths reach fail-stop workers (no live +0x36 writer; effects L26) | S2_opening |
| 0x00156620 | — | NM | live | em_drum_original over its roster node (em_area11_boxes.c) — test_drum_original_reference, test_collision_world_capture.py (records equal route 04) | the break and flight paths reach fail-stop workers (no live +0x36 writer; effects L26) | S2_opening |
| 0x001575B0 | — | BM | live | em_panel_program.c (op-9 callback: cue 0x3EF) — test_level_smoke.py (route 03 panel windows); test_area11_sfx_reference (cue 0x3EF) | tiny script callback translated inline | 03_panel_power |
| 0x00157860 | — | BM | live | em_panel.c via em_area11_interaction_host (node #26) — test_panel_reference; test_level_smoke.py (route 03) |  | S2_opening |
| 0x00157F60 | — | NM | live | em_panel_program.c + em_area11_interaction_host.c battery_open — test_level_smoke.py (route 03 B0/B1 request) | NEARMISS body; request tail only is exercised | 03_panel_power |
| 0x001580C0 | — | BM | live | em_panel_program.c + em_area11_interaction_host.c power — test_level_smoke.py (route 03 power bit); test_area_script_reference |  | 03_panel_power |
| 0x00159210 | — | BM | live | em_panel.c via em_area11_interaction_host (node #26) — test_panel_reference; test_level_smoke.py (route 03) |  | S2_opening |
| 0x0015AC00 | — | NM | unverified | em_pickup.c (INIT scale switch only) | em_pickup.c legacy pickup; partial; legacy owner | S2_opening* |
| 0x0015AE20 | — | BM | live | em_pickup_owner em_pickup_owner_tick (0015AFA0's state-1 call) — test_pickup_owner_reference (runs unhooked inside the executed 0015AFA0) | live since WP-6 (81414be) | S2_opening |
| 0x0015AFA0 | — | BM | live | em_pickup_owner em_pickup_owner_tick via em_pickup.c and the AREA11 interaction host — test_pickup_owner_reference (executes 0015AFA0 and compares the tick) | live since WP-6 (81414be); its aura draw block is the no-op stand-in aura_draw (see 001F1180) | S2_opening |

### 3.10 Vector math and owner services (0x1B1000..0x1B4FFF)

20 functions, 793 instructions: live 14, verified-unbound 5, unverified 1 (recount 2026-09-25).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001B1020 | — | AI | live | em_owner_services_original em_owner_services_001B1020 via em_area11_roger (the equipment's model 0x6B of D_0028A56C; census L22) — test_owner_services_reference.py, test_roger_actor_original_reference.py; test_level_smoke.py (roger: route 14 row for row) |  | S2_opening* |
| 0x001B10B0 | — | BM | live | em_roger_actor_original em_roger_actor_001B10B0 via em_area11_roger (census L22) — test_roger_actor_original_reference.py; test_level_smoke.py (roger: route 14 row for row) |  | S2_opening* |
| 0x001B1190 | — | CL | unverified | em_pickup.c taken_set (the owner's PERSIST event) | test_pickup_owner_reference hooks 001B1190 (persistence); no oracle executes it | 01_battery |
| 0x001B11E0 | — | NM | live | em_actor_roster, em_enemy — test_actor_census_reference.py |  | S1_newgame_load* |
| 0x001B1240 | — | BM | live | em_script_host_workers em_script_host_001B1240 (0018C0D0 and the camera workers, em_camera_live) — test_script_host_workers_reference; test_camera_live_reference; test_level_smoke.py camera rows (census 1.11) | live since census L13..L16 (21810 calls) | S1_newgame_load |
| 0x001B12B0 | — | AW | live | em_script_host_workers em_script_host_001B12B0 (bound in em_player_slide) — test_player_slide_reference.py, test_script_host_workers_reference.py, test_camera_follow_original_reference.py; test_level_smoke.py (06_hill_slide row for row, census L03) | live through the slide (0016C6A0 sub-states 1/2 turn +C4 onto +218); its camera and script-host callers are still unbound (legacy follow camera, L19) | 00_panel_no_battery |
| 0x001B1380 | — | AI | verified-unbound | em_script_host_workers, em_player_misc_workers — test_player_misc_workers_reference.py, test_script_host_workers_reference.py |  | S2_opening |
| 0x001B1470 | — | BM | live | em_player_stage_workers em_player_001B1470, em_effect_original / em_fan_original wraps — test_player_stage_workers_reference, test_effect_original_reference, test_fan_original_reference | the live copies (em_player_heading.c, em_camera.c cam_wrap_pi) are host wraps; test_player_heading_reference replaces 001B1470 with a host wrap (critic 7.2); recount 2026-09-25: wrap_001B1470 executed on the live path (152 calls over the five measured runs) | S1_newgame_load |
| 0x001B15D0 | — | BM | verified-unbound | em_player_misc_workers — test_player_misc_workers_reference.py |  | S2_opening |
| 0x001B1630 | — | NM | live | em_interaction_scan (em_interaction_visible) — test_interaction_pickup_reference.py, test_owner_services_reference.py | 001B17A0's gate for the panel, the terminal and the items (g.cam.eye / fwd) | S2_opening |
| 0x001B17A0 | — | BM | live | em_owner_services_original — test_owner_services_reference | the one 001B17A0 of the panel, the terminal, the items and Roger (census L22; the host's duplicate em_interaction_scene_offer is retired); the drums and the fan tail wait on their owners | S2_opening |
| 0x001B1B30 | — | BM | verified-unbound | em_script_door_fan em_sdf_001B1B30 — test_script_door_fan_reference | em_door.c (legacy door) | S2_opening |
| 0x001B1B70 | — | BM | live | em_actor_collision (em_actor_class_publish_001B1B70) — test_actor_collision_reference.py, test_owner_services_reference.py, test_collision_world_capture.py | 001B17A0's worker for the panel, the terminal and the items (their pool records); the crates and drums since L25 and the truck since L23 publish directly; the prop waits on L35 | S2_opening |
| 0x001B1CA0 | — | BM | verified-unbound | em_actor_collision (001B1B70's class 2/0xA push) — test_actor_collision_reference.py | bound in the live 001B1B70; no owner the port runs publishes class 2/0xA yet (Roger 008237E0, L22) | 03_panel_power |
| 0x001B1D20 | — | BM | live | em_actor_collision (001B1B70's class-4 push; em_actor_class_push4_001B1D20) — test_actor_collision_reference.py, test_collision_world_capture.py | the cells of the panel, the terminal and the items; the crates, drums and truck wait on L25/L23 | S2_opening |
| 0x001B1D60 | — | BM | live | em_actor_collision (001B1B70's class-7 push) — test_actor_collision_reference.py | the map item 0015AFA0 (class 0x87) and an armed 00219550 (0x87) | S2_opening |
| 0x001B1DA0 | — | BM | verified-unbound | em_actor_collision (001B1B70's class-0xD push) — test_actor_collision_reference.py | bound in the live 001B1B70; no owner the port runs publishes class 0xD (the flame 008235F0's 001B17A0 is not bound) | S2_opening |
| 0x001B1DE0 | — | BM | live | em_actor_collision (001B1B70's interactive push) — test_actor_collision_reference.py | the panel, the terminal and the items (class bit 0x80); the Use scan reads the published list | S2_opening |
| 0x001B1E20 | — | BM | live | em_owner_services_original via em_pad_actuator_001B1E20 (the truck's arm and fall rumbles, census L23) — test_owner_services_reference.py; test_level_smoke.py (truck_crossing) | the records D_0024D6F0 come from assets/pad_rumble.emrg (tools/export_pad_tables.py); em_gamepad.h keeps its own table copy (unused on the live path) | S2_opening |
| 0x001B1EA0 | — | AW | live | em_director_original em_director_original_001B1EA0_bound via em_area11_roger (Roger's trigger over the quad 0x82AB80 with 0011E620 of em_sdk_math_original; census L22), em_manager_008257A0 — test_director_original_reference.py (part 2: 0x82AB80); test_level_smoke.py (roger: route 14 row for row) (the script start f283) | the director's three quads wait on L21; the live camera's 00190F20 / 00194D10 quads (assets/camera_tables.emrg) run through it since census L13..L16 | S2_opening |

### 3.11 Script host and script ops (0x1B6BF0..0x1BBD5F)

29 functions, 3,487 instructions: live 22, verified-unbound 7 (recount 2026-09-25).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001B6BF0 | — | NM | live | em_area_script op18 via em_area11_script_host (census L22) — test_script_door_fan_reference (part 1); test_level_smoke.py (roger: route 14 row for row) |  | S2_opening |
| 0x001B6E40 | — | BM | live | em_area_script op16 via em_area11_script_host (00182BF0 through em_script_host_workers; census L22) — test_script_door_fan_reference (part 1); test_level_smoke.py (roger: route 14 row for row) |  | 10_cage_roof_roger |
| 0x001B6EA0 | — | BM | live | em_pickup_owner em_pickup_owner_take via em_pickup.c original_take — test_pickup_owner_reference (executes 001B6EA0) |  | 01_battery |
| 0x001B6F00 | — | BM | live | em_interaction_alignment.c — test_interaction_alignment_reference |  | 00_panel_no_battery |
| 0x001B6F80 | — | BM | verified-unbound | em_area_script op01 sub9 (inline) — test_script_door_fan_reference (part 1: the handler runs as original code inside 001BA1F0 while the em_area_script lockstep passes) | script op handlers; em_area_script is not in COMMON | S2_opening |
| 0x001B6FA0 | — | BM | verified-unbound | em_area_script op15 — test_script_door_fan_reference (part 1: the handler runs as original code inside 001BA1F0 while the em_area_script lockstep passes) | script op handlers; em_area_script is not in COMMON | 10_cage_roof_roger |
| 0x001B7840 | — | BM | live | em_area_script op10 via em_area11_script_host (census L22) — test_script_door_fan_reference (part 1); test_level_smoke.py (roger: route 14 row for row) |  | 14_roger_encounter |
| 0x001B7B30 | — | BM | live | em_area_script op0D via em_area11_script_host (census L22), em_cinematic_playback — test_cinematic_playback_reference; test_script_door_fan_reference (part 1); test_level_smoke.py (roger: route 14 row for row) | em_camera.c retarget/chase hooks (cases 3/5; level smoke routes 02-04) | S2_opening |
| 0x001B7D60 | — | BM | live | em_message_service em_message_op0c via em_message_live (the opening's op0C; the AREA11 host's panel/terminal lines) — test_message_service_reference, test_panel_message_reference | em_director.c kCineBeats for the director lines (WP-10) | S2_opening |
| 0x001B7F90 | — | BM | live | em_pickup_motion em_pickup_turn via the interaction host — test_pickup_motion_reference.py (executes 001B7F90) |  | 01_battery |
| 0x001B8020 | — | BM | live | em_area_script op0B (sub 4 admitted) via em_area11_script_host (Roger's 0x8283D0; census L22) — test_area_script_reference; test_level_smoke.py (roger: route 14 row for row) |  | 09_fence_door |
| 0x001B81D0 | — | BM | live | em_area_script face_attach via em_area11_script_host (census L22) — test_script_door_fan_reference (part 1); test_level_smoke.py (roger: route 14 row for row) |  | S2_opening |
| 0x001B82D0 | — | BM | live | em_script.c / em_opening_runtime.c execute; em_area_script op07 via em_area11_script_host (the truck and Roger's scripts, census L19 / L22) — test_interaction_frame_reference; test_script_reference; test_area_script_reference; test_level_smoke.py (roger: route 14 row for row) | op subset used by the opening, panel and elevator scripts | S2_opening |
| 0x001B8FC0 | — | BM | live | em_area_script op00 (kinds 0, 1, 2 live in 0x8292C0; census L19) — test_area_script_reference (route 07 replay); test_level_smoke.py (truck_preview: every camera shot row for row) | the opening camera track is em_opening_runtime.c + em_cinematic_camera.c; em_elevator_program runs its own op00 sub0 (one bound owner per handler is open, AREA_SCRIPT.md 5); the pickup grab program's op00 camera settle runs live as em_pickup_motion em_pickup_camera_settle (test_pickup_motion_reference executes 001B8FC0 for that case) | S2_opening |
| 0x001B94F0 | — | BM | live | em_area_script op01 (kind 1 live in 0x8292C0: 00182F90 through the pose host; census L19) — test_area_script_reference; test_level_smoke.py (truck_preview placement) | em_door.c legacy walk-to (H13); em_elevator_program runs its own op01 sub1 (AREA_SCRIPT.md 5) | S2_opening |
| 0x001B99F0 | — | NM | live | em_panel_program.c / em_opening_runtime.c op 9 — test_player_cinematic_reference; test_area_script_reference |  | 01_battery |
| 0x001B9BA0 | — | BM | live | em_panel_program.c / em_elevator_program.c op 2 — test_level_smoke.py (routes 02-04) |  | 03_panel_power |
| 0x001B9C10 | — | BM | live | em_player_pose_host.c player_pose_face — test_player_pose_host_reference |  | 02_elevator_refusal |
| 0x001BA080 | — | BM | live | em_area_script op06 via em_area11_script_host (census L22) — test_script_door_fan_reference (part 1); test_level_smoke.py (roger: route 14 row for row) |  | S2_opening |
| 0x001BA1A0 | — | BM | live | em_area_script em_area_script_start via em_area11_script_host (the truck trigger, census L19 / L23) — test_director_original_reference, test_area_script_reference; test_level_smoke.py (truck_preview) | em_script.c starts the opening, panel and elevator scripts (their own hosts); Roger's starts run here since census L22; the director's wait on L21 | S2_opening |
| 0x001BA1C0 | — | CL | live | em_manager_008257A0.c — test_manager_8257a0_reference |  | S2_opening |
| 0x001BA1F0 | — | NM | live | em_script.c — test_script_reference |  | S2_opening |
| 0x001BA510 | — | CL | verified-unbound | em_script_door_fan em_sdf_001BA510 — test_script_door_fan_reference | not bound (also inline in em_interaction_frame) | S2_opening |
| 0x001BA540 | — | BM | verified-unbound | em_roger_actor_original — test_roger_actor_original_reference.py | bound in em_area11_roger (census L22); not reached on the route | S3_first_control_idle |
| 0x001BA580 | — | NM | live | em_roger_actor_original em_roger_actor_001BA580 via em_area11_roger (census L22) — test_roger_actor_original_reference.py; test_level_smoke.py (roger: route 14 row for row) | its 001DA6A0 is a reported no-effect binding (UM_001DA6A0) | S2_opening |
| 0x001BA8E0 | — | NM | live | em_roger_actor_original em_roger_actor_001BA8E0 via em_area11_roger (Roger's face slot; census L22) — test_roger_actor_original_reference.py; test_level_smoke.py (roger: route 14 row for row) |  | S2_opening* |
| 0x001BAC00 | — | BM | verified-unbound | em_script_door_fan em_sdf_001BAC00 — test_script_door_fan_reference | em_opening_runtime.c op 0x14 case is a no-op stand-in | S2_opening* |
| 0x001BAD40 | — | NM | verified-unbound | em_script_door_fan em_sdf_001BAD40 — test_script_door_fan_reference | not bound | S2_opening* |
| 0x001BB0E0 | — | AW | verified-unbound | em_startup_load_gaps em_slg_001BB0E0 — test_startup_load_gaps_reference | em_opening_actor.c (opening actors checked by fixtures and one capture frame) | S2_opening |

### 3.12 Door (0x1BBD60..0x1BC3FF)

9 functions, 492 instructions: live 1, verified-unbound 8 (recount 2026-09-25).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001BBD60 | — | BM | verified-unbound | em_script_door_fan em_sdf_001BBD60 — test_script_door_fan_reference | em_door.c legacy door sound patch | 09_fence_door |
| 0x001BBDA0 | — | BM | verified-unbound | em_door_original — test_door_original_reference | em_door.c legacy door | S2_opening* |
| 0x001BBE40 | — | BM | verified-unbound | em_door_transit — test_door_transit_reference | em_door.c legacy walk-to at 15 u/s (H13) | S2_opening |
| 0x001BC0E0 | — | BM | verified-unbound | em_door_original — test_door_original_reference | em_door.c legacy door | 09_fence_door |
| 0x001BC150 | — | BM | live | em_door.c door_commit_001BC150 -> em_door_transit_commit — test_door_transit_reference; test_room_move_reference |  | 09_fence_door |
| 0x001BC240 | — | BM | verified-unbound | em_script_door_fan em_sdf_001BC240; em_door_original phase 4 inline — test_script_door_fan_reference, test_door_original_reference | em_door.c (legacy door sub 4/5) | 09_fence_door |
| 0x001BC290 | — | BM | verified-unbound | em_script_door_fan em_sdf_001BC290; em_door_original phase 5 inline — test_script_door_fan_reference, test_door_original_reference | em_door.c (legacy door sub 4/5) | 09_fence_door |
| 0x001BC300 | — | BM | verified-unbound | em_door_original — test_door_original_reference | em_door.c legacy door | S2_opening |
| 0x001BC350 | — | BM | verified-unbound | em_door_original — test_door_original_reference | em_door.c legacy em_door_update (node tick_door) | S2_opening |

### 3.13 Area services, child spawns, area title (0x1BC400..0x1C5FFF)

21 functions, 1,659 instructions: live 10, verified-unbound 11 (recount 2026-09-25).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001C1D00 | — | BM | verified-unbound | em_frame_render_heads em_frh_001C1D00 — test_frame_render_heads_reference | em_render_frame.c em_render_001C1D00 (port render-env init) | S2_opening |
| 0x001C1DC0 | — | BM | verified-unbound | em_render_verify_rest em_rvr_001C1DC0 — test_render_verify_rest_reference | em_scene_bindings.c w_001C1DC0: the 001C1EA0 weather spawn only; the registrations and 001C1E70..001C1F50 passes are reported | S1_newgame_load |
| 0x001C1E70 | — | BM | verified-unbound | em_render_verify_rest em_rvr_001C1E70 — test_render_verify_rest_reference | reported under UM_001C1DC0 | S1_newgame_load |
| 0x001C1E80 | — | BM | verified-unbound | em_render_verify_rest em_rvr_001C1E80 — test_render_verify_rest_reference | reported under UM_001C1DC0 | S1_newgame_load |
| 0x001C1E90 | — | BM | verified-unbound | em_render_verify_rest em_rvr_001C1E90 — test_render_verify_rest_reference | reported under UM_001C1DC0 | S1_newgame_load |
| 0x001C1EA0 | — | BM | live | em_area11_bindings.c em_area11_spawn_weather_001C1EA0 — compare_frame_order.py; test_level_smoke.py (census 49) | spawn only | S1_newgame_load |
| 0x001C1F50 | — | NM | verified-unbound | em_render_verify_rest em_rvr_001C1F50 — test_render_verify_rest_reference | not bound | S1_newgame_load |
| 0x001C22A0 | — | BM | verified-unbound | em_render_verify_rest em_rvr_001C22A0 — test_render_verify_rest_reference | not bound | S2_opening* |
| 0x001C2360 | — | BM | verified-unbound | em_render_verify_rest em_rvr_001C2360 — test_render_verify_rest_reference | not bound | S2_opening* |
| 0x001C40B0 | — | NM | live | em_pickup_items_original em_pickup_items_001C40B0 via em_pickup.c — test_pickup_items_reference (executes 001C40B0); test_continue_reset_reference |  | S0_title |
| 0x001C4760 | — | BM | live | em_director_original_001C4760 through em_director_original_001C4760_scene (the canonical key bytes; the opening 00823E80's 001C4760(0, 1), and the legacy director stand-in's beat-0 001C4760(1, 1)) — test_continue_reset_reference.py (executes 001C4760, 90 cases, and the opening slice 0x823F6C..0x823F8C through its call), test_director_original_reference.py |  | S2_opening |
| 0x001C47A0 | — | BM | live | em_pickup.c pickup_take (B0 = 1, B1 = type) — test_level_smoke.py (battery phase; route 01 f220..f459) | called from em_pickup.c original_take through the original 00219550 program (81414be) | 01_battery |
| 0x001C4820 | — | BM | verified-unbound | em_status_ui_leftovers em_sul_001C4820 — test_status_ui_leftovers_reference | pool node render-only; drawn from the scene props | S2_opening |
| 0x001C5570 | — | BM | live | em_area11_bindings.c spawn_001C5570 — compare_frame_order.py; test_level_smoke.py (census 49) | spawn only | S2_opening* |
| 0x001C5680 | — | AI | live | em_indicator_child em_indicator_child_step, per node (em_area11_bindings.c tick_indicator) — test_census_unverified_reference (every beat's children in walk order), test-indicator-child | stand-ins: the 0x7A child's model draw waits on OWNER_DRAW.md P1; the live 001C2360 bind always succeeds and the 001C6380 placement is a no-op (the draw uses the owner's palette), pinned as 001C5680/live-init-stub and /live-place-stub; 001CABA0's 001CA7B0 cull is not modelled (CENSUS_UNVERIFIED.md, section 1.13) | S2_opening |
| 0x001C5760 | — | AI | live | em_indicator_child em_indicator_child_step, per node; the terminal's colour tail 0x827EAC (em_indicator_00827B10_colour) — test_census_unverified_reference, test-indicator-child | stand-ins: the live 001C22A0 bind always succeeds and the 001C6380 placement is a no-op (also the +0x0A re-run, latent), pinned as 001C5760/live-init-stub and /live-place-stub; 001CABA0's 001CA7B0 cull is not modelled (CENSUS_UNVERIFIED.md) | S2_opening |
| 0x001C5860 | — | BM | verified-unbound | em_status_ui_leftovers em_sul_001C5860 — test_status_ui_leftovers_reference | em_hud.c legacy area-title sub-location line | S2_opening |
| 0x001C5930 | — | NM | verified-unbound | em_status_ui_leftovers em_sul_001C5930 — test_status_ui_leftovers_reference | em_hud.c legacy area-title card; node lifecycle translated in em_area11_bindings.c tick_area_title (from the .s) | S2_opening |
| 0x001C5C50 | — | BM | live | em_actor_roster.c em_actor_roster_spawn_001C5C50 — test_actor_census_reference |  | S1_newgame_load |
| 0x001C5C90 | — | BM | live | em_roger_actor_original em_roger_actor_001C5C90 via em_area11_roger (the equipment node area11[9]; census L22) — test_roger_actor_original_reference.py; test_level_smoke.py (roger: route 14 row for row) (attach_r9 +0x00..+0x0F, +0xB0) | its +0x4C draw is the port actor draw of opening/equipment_6b.emdl at the bone-0 matrix | S2_opening |
| 0x001C5FB0 | — | NM | live | em_status_hub_ui.c (inline number format) — test_status_hub_ui_reference |  | 01_battery |

### 3.14 Animation runtime (0x1C6000..0x1CC16F)

54 functions, 4,292 instructions: live 36, verified-unbound 18 (recount 2026-09-25).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001C6120 | — | BM | live | em_pose_host_workers on the player record (em_player_record_pose), em_owner_services_original — test_player_record_pose_reference, test_owner_services_reference.py, test_pose_host_workers_reference.py, test_shadow_original_reference.py |  | S0_title |
| 0x001C6150 | — | BM | verified-unbound | em_status_models, em_roger_actor_original — test_owner_services_reference.py, test_player_misc_workers_reference.py, test_roger_actor_original_reference.py |  | S1_newgame_load |
| 0x001C61D0 | — | BM | live | em_pose_host_workers on the player record (em_player_record_pose: the host's clip frames), em_weapon — test_player_record_pose_reference; test_pose_host_workers_reference, test_player_fall_reference.py, test_player_reaction_reference.py, test_player_recovery_reference.py |  | 00_panel_no_battery |
| 0x001C62C0 | bone_init_default_1 | AW | live | em_status_models, em_owner_services_original — test_owner_services_reference.py |  | S2_opening |
| 0x001C6380 | — | BM | live | em_owner_services_original, em_status_models — test_owner_services_reference.py |  | S2_opening |
| 0x001C63E0 | bone_init_default_2 | BM | live | em_pose_host_workers on the player record (em_player_record_pose_default: the opening release, 0015C420's pose half at the attach, the legacy re-seed, the special-bank release) and on Roger's record (em_area11_roger, census L22), em_status_models — test_player_record_pose_reference; test_pose_host_workers_reference; test_player_cinematic_reference |  | S2_opening |
| 0x001C64F0 | anim_advance_time | NM | live | em_player_stage_workers em_player_stage_anim_advance on the player record (em_player_record_pose: 0015BA50's advance, the idle and script ticks; the chain step) — test_player_record_pose_reference; test_pose_host_workers_reference; test_player_pose_live_reference | em_player_pose.c em_player_pose_advance poses the status models; Roger's record runs em_player_stage_anim_advance through em_area11_roger (census L22); the interaction runtime's baked clock (em_interaction_animation.c) is checked against the record every tick | S2_opening |
| 0x001C67E0 | anim_clip_init | BM | live | em_pose_host_workers (clip init) on the player record (em_player_record_pose) — test_player_record_pose_reference; test_pose_host_workers_reference |  | 00_panel_no_battery |
| 0x001C68C0 | — | BM | live | em_pose_host_workers on the player record (0015BCF0's animate step for a zero D_00248C90 row: the takeover clips; 0015C420's pose half) and on Roger's record (em_area11_roger, census L22), em_door — test_player_record_pose_reference; test_pose_host_workers_reference; test_level_smoke.py (roger: route 14 row for row) |  | S2_opening |
| 0x001C6960 | — | BM | live | em_pose_host_workers via em_player_record_pose_animate (+0x2F3 = 2: the special bank on the record, census L22) — test_pose_host_workers_reference.py; test_player_cinematic_reference; test_level_smoke.py (roger: route 14 row for row) (clip / clock with +0x2F3 = 2) |  | S2_opening |
| 0x001C6DA0 | anim_eval_skeleton | AU | live | em_pose_host_workers em_pose_host_001C6DA0 on the player record (0015BCF0's animate step for a nonzero D_00248C90 row; 0017B910's foot-stop begin) — test_player_record_pose_reference (the captured skeletons re-evaluated byte for byte); test_pose_host_workers_reference | em_pose_bank / em_player_pose evaluation remains for Roger and the status models; other actors use exported matrices | S2_opening |
| 0x001C7420 | — | NM | verified-unbound | em_owner_services_original — test_owner_services_reference | em_face_model.c / em_opening_actor.c basis collapse | S2_opening |
| 0x001C7900 | — | NM | verified-unbound | em_anim_runtime_rest em_anim_rest_001C7900 — test_anim_runtime_rest_reference | not bound | S2_opening |
| 0x001C7C00 | — | NM | live | em_cinematic_camera em_cinematic_camera_sample (em_render_frame.c -> em_opening_runtime_camera every opening frame) — test_roger_cinematic_reference.py | live for S2 (critic 7.2); unbound for Roger's encounter (em_cinematic_playback.c is not in COMMON) | S2_opening |
| 0x001C8480 | anim_clip_resolve | BM | live | em_pose_host_workers on the player record — test_player_record_pose_reference; test_pose_host_workers_reference |  | S2_opening |
| 0x001C84D0 | — | BM | live | em_pose_host_workers on the player record; em_pose_bank.c / em_pose_transition.c (Roger, status models) — test_player_record_pose_reference; test_pose_host_workers_reference; test_pose_bank_reference; test_pose_transition_reference |  | S2_opening |
| 0x001C85D0 | anim_decode_translation | BM | live | em_pose_host_workers on the player record; em_pose_bank.c / em_pose_transition.c (Roger, status models) — test_player_record_pose_reference; test_pose_host_workers_reference; test_pose_bank_reference; test_pose_transition_reference |  | S2_opening |
| 0x001C86A0 | — | BM | live | em_pose_host_workers on the player record; em_pose_transition.c (Roger, status models) — test_player_record_pose_reference; test_pose_host_workers_reference; test_pose_transition_reference |  | S2_opening |
| 0x001C8710 | — | AW | live | em_pose_host_workers on the player record — test_player_record_pose_reference; test_pose_host_workers_reference |  | S2_opening |
| 0x001C87C0 | — | NM | live | em_pose_host_workers on the player record; em_pose_transition.c (Roger, status models) — test_player_record_pose_reference; test_pose_host_workers_reference; test_pose_transition_reference |  | S2_opening |
| 0x001C8D50 | anim_sample_bones | BM | live | em_pose_host_workers on the player record; em_pose_transition.c (Roger, status models) — test_player_record_pose_reference; test_pose_host_workers_reference; test_pose_transition_reference |  | 00_panel_no_battery |
| 0x001C8F10 | anim_sample_rotation | BM | live | em_pose_host_workers on the player record — test_player_record_pose_reference; test_pose_host_workers_reference |  | S2_opening |
| 0x001C90D0 | — | BM | live | em_pose_host_workers on the player record — test_player_record_pose_reference; test_pose_host_workers_reference |  | S2_opening |
| 0x001C92C0 | — | NM | live | em_pose_host_workers on the player record — test_player_record_pose_reference; test_pose_host_workers_reference |  | S2_opening |
| 0x001C94B0 | build_trs_matrix | AI | live | em_pose_host_workers (em_owner_services_build_trs_matrix) on the player record's +D0 — test_player_record_pose_reference; test_pose_host_workers_reference |  | S2_opening |
| 0x001C9610 | — | NM | live | em_owner_services_original, em_status_models — test_owner_services_reference.py |  | S2_opening |
| 0x001C9940 | — | NM | live | em_pose_host_workers on the player record (through 001C68C0), em_door — test_player_record_pose_reference; test_pose_host_workers_reference |  | S2_opening |
| 0x001C9D50 | — | BM | live | em_anim_runtime_rest em_anim_rest_001C9D50 (0017B660's node blend, census L12 + L33) — test_anim_runtime_rest_reference; test_locomotion_display_reference (composite) |  | 00_panel_no_battery |
| 0x001C9E40 | — | AW | live | em_anim_runtime_rest em_anim_rest_001C9E40 (inside 001C9D50) — test_anim_runtime_rest_reference |  | 00_panel_no_battery |
| 0x001CA0A0 | quat_nlerp | NM | live | em_pose_host_workers on the player record; em_pose_transition.c (Roger, status models) — test_player_record_pose_reference; test_pose_host_workers_reference; test_pose_transition_reference |  | S2_opening |
| 0x001CA1C0 | quat_to_mat3 | BM | live | em_pose_host_workers on the player record; em_pose_transition.c (Roger, status models) — test_player_record_pose_reference; test_pose_host_workers_reference; test_pose_transition_reference |  | S2_opening |
| 0x001CA5E0 | — | BM | live | em_roger_actor_original, inline in em_roger_actor_001CA6E0 (the boxes' and Roger's model bind; census L22) — test_roger_actor_original_reference.py; test_level_smoke.py (boxes, roger) |  | S1_newgame_load |
| 0x001CA5F0 | — | BM | live | em_status_models; em_roger_actor_original, inline in em_roger_actor_001CA6E0 (kind 0: the draw method 001CAA00; census L22) — test_roger_actor_original_reference.py; test_level_smoke.py (boxes, roger) |  | S1_newgame_load |
| 0x001CA6E0 | — | BM | live | em_roger_actor_original em_roger_actor_001CA6E0 (the boxes' model bind) — test_roger_actor_original_reference.py |  | S1_newgame_load |
| 0x001CA6F0 | — | BM | live | em_roger_actor_original, inline in em_roger_actor_008237E0_init (+0x98 = 2; census L22) — test_roger_actor_original_reference.py; test_level_smoke.py (roger) |  | S2_opening* |
| 0x001CA700 | — | BM | live | em_roger_actor_original em_roger_actor_001CA700 (Roger's face slot, census L22); the player's row-0x18 face through em_player_face_host (the script host's 001CA700) — test_roger_actor_original_reference.py; test_player_face_host.py; test_level_smoke.py (roger: route 14 row for row) |  | S2_opening |
| 0x001CA770 | — | BM | verified-unbound | em_roger_actor_original — test_roger_actor_original_reference.py | bound in em_area11_roger (census L22); not reached on the route (the player's 001CA770 is the script host's release of the frame skeleton) | S2_opening |
| 0x001CA7B0 | — | BM | verified-unbound | em_owner_draw_original em_owner_draw_001CA7B0 — test_owner_draw_reference |  | S2_opening |
| 0x001CA940 | — | BM | verified-unbound | em_owner_draw_original em_owner_draw_001CA940 — test_owner_draw_reference |  | S2_opening |
| 0x001CA990 | — | BM | verified-unbound | em_owner_services_original — test_owner_services_reference.py |  | S2_opening |
| 0x001CAA00 | — | BM | verified-unbound | em_owner_services_original, em_roger_actor_original — test_owner_services_reference.py, test_roger_actor_original_reference.py | Roger's and the equipment's +0x4C: the port's actor draw (em_area11_roger_draw, census L22) | S2_opening |
| 0x001CAAC0 | — | NM | verified-unbound | em_anim_runtime_rest em_anim_rest_001CAAC0 — test_anim_runtime_rest_reference | not bound | S2_opening |
| 0x001CACB0 | — | BM | verified-unbound | em_anim_runtime_rest em_anim_rest_001CACB0 — test_anim_runtime_rest_reference | not bound | S2_opening |
| 0x001CB2C0 | — | NM | verified-unbound | em_anim_runtime_rest em_anim_rest_001CB2C0 — test_anim_runtime_rest_reference | not bound | S2_opening |
| 0x001CB3C0 | — | BM | verified-unbound | em_owner_services_original — test_owner_services_reference.py |  | S2_opening |
| 0x001CB590 | — | BM | live | em_scene_bindings, em_status_models — test_actor_pool_reference.py |  | S2_opening |
| 0x001CB5A0 | — | BM | live | em_scene_bindings.c w_001CB5A0 (empty leaf) — test_scene_frame_reference |  | S2_opening |
| 0x001CB5B0 | anim_bone_array_setup | BM | verified-unbound | em_anim_runtime_rest em_anim_rest_001CB5B0 — test_anim_runtime_rest_reference | em_status_models.c w_001CB5B0 (unverified; does not write the word) | S2_opening |
| 0x001CB5F0 | — | BM | verified-unbound | em_packet_chain_original em_packet_chain_001CB5F0 — test_packet_chain_reference.py (oracle; 1,053 captured chain blocks replayed) | no live consumer: the effects, the head sprite, the equipment sprite, the animation runtime and the render context need the render-context views first (L32/L30; EFFECT_MANAGER.md section 5.0) | S2_opening |
| 0x001CB6B0 | — | BM | verified-unbound | em_packet_chain_original em_packet_chain_001CB6B0 — test_packet_chain_reference.py (oracle; 1,053 captured chain blocks replayed) | no live consumer: the effects, the head sprite, the equipment sprite, the animation runtime and the render context need the render-context views first (L32/L30; EFFECT_MANAGER.md section 5.0) | S2_opening |
| 0x001CB760 | — | BM | verified-unbound | em_packet_chain_original em_packet_chain_001CB760 — test_packet_chain_reference.py (oracle; 1,053 captured chain blocks replayed) | no live consumer: the effects, the head sprite, the equipment sprite, the animation runtime and the render context need the render-context views first (L32/L30; EFFECT_MANAGER.md section 5.0) | S2_opening |
| 0x001CB900 | — | BM | verified-unbound | em_packet_chain_original em_packet_chain_001CB900 — test_packet_chain_reference.py (oracle; 1,053 captured chain blocks replayed) | no live consumer: the effects, the head sprite, the equipment sprite, the animation runtime and the render context need the render-context views first (L32/L30; EFFECT_MANAGER.md section 5.0) | S2_opening |
| 0x001CB9B0 | — | CL | verified-unbound | em_packet_chain_original em_packet_chain_001CB9B0 — test_packet_chain_reference.py (every mode; 177 captured blend blocks) | moved out of the boundary group (PACKET_CHAIN.md 6.2): it picks 001CB900's blend-state block; the C leaves the default unset, the .s returns 0 | S2_opening |
| 0x001CBE10 | — | BM | live | em_message_glyph_original (the message glyph advance), em_hud's atlas advances — test_message_glyph_reference.py, test_message_draw_reference.py |  | S2_opening |

### 3.15 Message glyphs, object registry and face (0x1CC170..0x1D19CF)

18 functions, 2,117 instructions: live 8, verified-unbound 9, unverified 1 (recount 2026-09-25).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001CC170 | — | AI | live | em_message_draw_original via em_message_live — test_message_draw_reference.py |  | S2_opening |
| 0x001CC1E0 | — | AW | live | em_message_glyph_original em_message_glyph_cc1e0 via em_message_live — test_message_glyph_reference.py |  | S2_opening |
| 0x001CC3B0 | — | NM | live | em_message_glyph_original (message lines, drawn through the atlas by em_hud_glyph_strip); em_status_hub_ui.c render_text — test_message_glyph_reference, test_status_hub_ui_reference |  | S2_opening |
| 0x001CCF70 | — | NM | verified-unbound | em_effect_original, em_head_sprite_original — test_effect_original_reference.py, test_head_sprite_reference.py |  | S2_opening |
| 0x001CD370 | — | BM | verified-unbound | em_head_sprite_original — test_head_sprite_reference.py |  | S2_opening |
| 0x001CD390 | — | NM | verified-unbound | em_effect_original, em_shadow_actor_route — test_effect_original_reference.py, test_shadow_actor_route_reference.py |  | 02_elevator_refusal |
| 0x001CD520 | — | NM | verified-unbound | em_player_equipment_sprite em_player_equipment_001CD520 — test_player_equipment_reference | em_status_models.c w_001CD520 (unverified; faults on the D_008104E4 == 1 glow) | S2_opening |
| 0x001CE300 | — | NM | verified-unbound | em_shadow_actor_route — test_shadow_actor_route_reference.py |  | 02_elevator_refusal |
| 0x001CF470 | — | AW | unverified | em_shadow_actor_route | module not linked | 02_elevator_refusal |
| 0x001CFA60 | — | BM | verified-unbound | em_head_sprite_original — test_head_sprite_reference.py |  | S2_opening |
| 0x001CFB50 | — | BM | verified-unbound | em_effect_kinds em_effect_kinds_001CFB50 — test_effect_kinds_reference.py (executed with 001D0540 / 001CD370 / 0011DF78; the captured D_0081F8F0 of beats 05, 08 and 12 reproduced) | moved out of the boundary group: the puff handlers' transform block; not bound (needs the render-context views, L32/L30) | 00_panel_no_battery |
| 0x001CFBE0 | — | BM | verified-unbound | em_head_sprite_original — test_head_sprite_reference.py |  | S2_opening |
| 0x001D0540 | — | NM | verified-unbound | em_effect_kinds em_effect_kinds_001D0540 (from the .s) — test_effect_kinds_reference.py | moved out of the boundary group: 001CFB50's depth scale over 0x70003AC0; not bound (L32/L30) | 00_panel_no_battery |
| 0x001D0690 | — | BM | live | em_roger_actor_original via em_area11_roger (census L22) — test_roger_actor_original_reference.py; test_level_smoke.py (roger: route 14 row for row) |  | S2_opening |
| 0x001D06D0 | — | BM | live | em_roger_actor_original via em_area11_roger and the script host's w_001D06D0 (census L22) — test_roger_actor_original_reference.py; test_level_smoke.py (roger: route 14 row for row) |  | S2_opening |
| 0x001D06E0 | — | BM | live | em_player_face_host.c — test_player_face_host.py |  | S2_opening |
| 0x001D0720 | — | NM | live | em_opening_face.c — test_opening_face_reference |  | S2_opening |
| 0x001D0C70 | — | BM | live | 00183090's call (em_player_stage_commit w001D0C70) -> em_player_face_host's tick of 001D0720 (em_opening_face) on the attached face (census L22) — test_player_cinematic_reference; test_opening_face_reference; test_level_smoke.py (roger: route 14 row for row) | Roger's own face slot ticks through em_area11_roger's w_001D0720 | S2_opening |

### 3.16 Render heads, projection, lighting, shadow, veil particles (0x1D19D0..0x1DAFFF)

62 functions, 5,535 instructions: live 9, verified-unbound 53 (recount 2026-09-25).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001D19D0 | — | BM | verified-unbound | em_frame_render_heads em_frh_001D19D0 — test_frame_render_heads_reference | reported no-effect binding UM_001D19D0 (render init) | S0_title* |
| 0x001D19E0 | — | BM | verified-unbound | em_frame_render_heads em_frh_001D19E0 — test_frame_render_heads_reference | reported no-effect binding (um_001D19E0) | S1_newgame_load* |
| 0x001D1C50 | — | NM | verified-unbound | em_frame_render_heads em_frh_001D1C50 — test_frame_render_heads_reference | em_render_frame.c em_render_001D1C50 (port collector; fog/GS setup, point-light tick) | S0_title |
| 0x001D1EA0 | — | BM | verified-unbound | em_frame_render_heads em_frh_001D1EA0 — test_frame_render_heads_reference | em_render_frame.c em_render_001D1EA0 (native world/page draw) | S0_title |
| 0x001D1EF0 | — | BM | verified-unbound | em_frame_render_heads em_frh_001D1EF0 — test_frame_render_heads_reference | reported no-effect binding (um_001D1EF0) | S0_title |
| 0x001D1F20 | — | CL | verified-unbound | em_load_veil_particles — test_load_veil_particles_reference.py |  | S0_title |
| 0x001D1F80 | — | CL | verified-unbound | em_load_veil_particles, em_owner_services_original — test_load_veil_particles_reference.py, test_owner_services_reference.py |  | S0_title |
| 0x001D1FF0 | — | CL | verified-unbound | em_load_veil_particles, em_shadow_original — test_load_veil_particles_reference.py |  | S0_title |
| 0x001D2040 | — | CL | verified-unbound | em_load_veil_particles, em_status_models — test_load_veil_particles_reference.py |  | S0_title |
| 0x001D21B0 | — | BM | verified-unbound | em_render_context em_render_context_001D21B0 — test_render_context_reference | formerly a GS/VIF boundary row; a render-context helper | S2_opening |
| 0x001D2300 | — | NM | verified-unbound | em_background_gs — test_background_reference.py | its channel-3 CALL gate is mirrored in frame_close_out (D_008106C4, flag 4 = the D_00821058 movie mirror, flag 0x20 = the manifest's `background` line); the list build itself is the renderer boundary | S0_title |
| 0x001D2590 | — | AI | verified-unbound | em_frame_render_heads em_frh_001D2590 — test_frame_render_heads_reference | em_area11_interaction_host.c UI projection (zoom 224 / tan 25 deg) | S2_opening |
| 0x001D25F0 | — | BM | verified-unbound | em_frame_render_heads em_frh_001D25F0 — test_frame_render_heads_reference | em_math.h / em_render_frame.c native projection | S2_opening |
| 0x001D2610 | — | BM | verified-unbound | em_frame_render_heads em_frh_001D2610 — test_frame_render_heads_reference | em_area11_interaction_host.c UI projection (zoom 224 / tan 25 deg) | S2_opening |
| 0x001D2710 | — | BM | verified-unbound | em_render_context em_render_context_001D2710 — test_render_context_reference | formerly a GS/VIF boundary row; context-flag logic | S0_title |
| 0x001D2830 | — | AW | verified-unbound | em_frame_render_heads em_frh_001D2830 — test_frame_render_heads_reference | reported no-effect binding (um_001D2830; veil registrations; the area script's w_001D2830 since census L22) | S0_title |
| 0x001D2910 | — | AW | verified-unbound | em_render_context em_render_context_001D2910 — test_render_context_reference | formerly a GS/VIF boundary row; context-flag logic (FRAME_RENDER_HEADS.md) | S0_title |
| 0x001D2960 | — | BM | verified-unbound | em_frame_render_heads em_frh_001D2960 — test_frame_render_heads_reference | em_math.h / em_render_frame.c native projection | S0_title |
| 0x001D2D20 | — | BM | verified-unbound | em_frame_render_heads em_frh_001D2D20 — test_frame_render_heads_reference | em_math.h / em_render_frame.c native projection | S0_title |
| 0x001D2DE0 | — | BM | verified-unbound | em_render_context em_render_context_001D2DE0 — test_render_context_reference | formerly a GS/VIF boundary row | S0_title |
| 0x001D2E00 | — | BM | verified-unbound | em_render_context em_render_context_001D2E00 — test_render_context_reference | formerly a GS/VIF boundary row | S1_newgame_load |
| 0x001D30A0 | — | NM | verified-unbound | em_frame_render_heads em_frh_001D30A0 — test_frame_render_heads_reference | em_render_frame.c render chain / frame_close_out | S0_title |
| 0x001D38A0 | — | CL | verified-unbound | em_owner_draw_original em_owner_draw_001D38A0 — test_owner_draw_reference | formerly a GS/VIF boundary row; the per-owner DMA unit | S2_opening |
| 0x001D3BA0 | — | BM | verified-unbound | em_owner_draw_original em_owner_draw_001D3BA0 — test_owner_draw_reference | formerly a GS/VIF boundary row; the per-owner DMA unit | S2_opening |
| 0x001D4B50 | — | BM | verified-unbound | em_render_verify_rest em_rvr_001D4B50 — test_render_verify_rest_reference | em_gfx | S2_opening |
| 0x001D4CD0 | — | BM | verified-unbound | em_gfx, em_shadow_original — test_shadow_original_reference.py |  | S2_opening |
| 0x001D4FB0 | — | BM | verified-unbound | em_shadow_original, em_gfx — test_shadow_original_reference.py |  | S2_opening |
| 0x001D52E0 | — | BM | verified-unbound | em_render_context em_render_context_001D52E0 — test_render_context_reference |  | S1_newgame_load |
| 0x001D5370 | — | NM | verified-unbound | em_render_context em_render_context_001D5370 — test_render_context_reference |  | S2_opening |
| 0x001D5C80 | — | NM | verified-unbound | em_shadow_original receivers — test_render_verify_rest_reference (intercepted inside the executed 001DA6A0; passes with the em_ee_float.h fix of 3824c6f) | module not linked | S2_opening |
| 0x001D63B0 | — | NM | verified-unbound | em_load_veil_particles — test_load_veil_particles_reference.py |  | S1_newgame_load* |
| 0x001D6930 | — | NM | verified-unbound | em_load_veil_particles — test_load_veil_particles_reference.py |  | S1_newgame_load |
| 0x001D6B10 | — | CL | verified-unbound | em_render_context em_render_context_001D6B10 — test_render_context_reference | formerly a GS/VIF boundary row | S2_opening |
| 0x001D6B60 | — | BM | verified-unbound | em_load_veil_particles — test_load_veil_particles_reference.py |  | S1_newgame_load* |
| 0x001D6BA0 | — | NM | verified-unbound | em_load_veil_particles — test_load_veil_particles_reference.py |  | S1_newgame_load |
| 0x001D6C90 | — | NM | verified-unbound | em_render_context em_render_context_001D6C90 — test_render_context_reference | formerly a GS/VIF boundary row; the four-sprite packet | S2_opening |
| 0x001D6E60 | — | NM | verified-unbound | em_load_veil_particles — test_load_veil_particles_reference.py |  | S1_newgame_load |
| 0x001D7080 | — | AW | verified-unbound | em_load_veil_particles — test_load_veil_particles_reference.py |  | S1_newgame_load |
| 0x001D7B30 | — | BM | live | em_lighting.c room-rig lookup — test_actor_lighting_reference.py |  | S1_newgame_load |
| 0x001D7BB0 | — | BM | live | em_point_light.c — test_point_light_reference |  | S1_newgame_load* |
| 0x001D7C30 | — | NM | live | em_point_light.c — test_point_light_reference |  | S0_title |
| 0x001D7FA0 | — | BM | live | em_point_light.c — test_point_light_reference |  | S1_newgame_load* |
| 0x001D8060 | — | BM | verified-unbound | em_frame_render_heads em_frh_001D8060 — test_frame_render_heads_reference |  | S1_newgame_load* |
| 0x001D80B0 | — | BM | verified-unbound | em_frame_render_heads em_frh_001D80B0 — test_frame_render_heads_reference |  | S1_newgame_load* |
| 0x001D8130 | — | BM | live | em_lighting.c — test_actor_lighting_reference.py | several actor models still carry the stand-in light (H18/WP-13) | S2_opening |
| 0x001D8270 | — | AW | verified-unbound | em_lighting.c em_lighting_fold_gate; em_actor_light_001D89D0 em_actor_light_001D8270 — test_actor_lighting_reference.py (part E); test_actor_light_001d89d0_reference | not called live: em_render_frame.c char_rig_build assumes the gate passes for the opening actors ("pass the 001D8270 gate") | S2_opening |
| 0x001D8340 | — | BM | live | em_lighting.c — test_actor_lighting_reference.py | several actor models still carry the stand-in light (H18/WP-13) | S2_opening |
| 0x001D8690 | — | NM | verified-unbound | em_lighting.c em_lighting_actor_rgb; em_actor_light_001D89D0 em_actor_light_001D8690 — test_actor_lighting_reference.py (part E); test_actor_light_001d89d0_reference | not called live: the actor RGB / self-glow stays the renderer's post-draw tint (em_render_frame.c char_rig_build note), not asserted equivalent | S2_opening |
| 0x001D88B0 | — | BM | verified-unbound | em_frame_render_heads em_frh_001D88B0 — test_frame_render_heads_reference | em_lighting.c face lighting mode | S2_opening |
| 0x001D89D0 | — | NM | live | em_lighting.c — test_actor_lighting_reference.py | live through em_render_frame.c char_rig_build + em_lighting_matrices, which test_actor_lighting_reference checks only under its port contract; the bit-exact translation em_actor_light_001D89D0 (test_actor_light_001d89d0_reference) is not bound; several actor models still carry the stand-in light (H18/WP-13) | S2_opening |
| 0x001D8BF0 | — | BM | live | em_roger_actor_original via em_area11_roger (census L22) — test_roger_actor_original_reference.py; test_level_smoke.py (roger: route 14 row for row) |  | S1_newgame_load |
| 0x001D8C20 | — | BM | live | em_lighting.c — test_actor_lighting_reference.py | several actor models still carry the stand-in light (H18/WP-13) | S2_opening |
| 0x001D8C30 | — | NM | verified-unbound | em_frame_render_heads em_frh_001D8C30 — test_frame_render_heads_reference | em_effect_color.h / em_status_models.c draw | S2_opening |
| 0x001D8FD0 | — | BM | verified-unbound | em_fog_gs — test_area11_fog_reference | exported fog record in the scene manifest (values verified) | S1_newgame_load |
| 0x001D9070 | — | NM | verified-unbound | em_frame_render_heads em_frh_001D9070 — test_frame_render_heads_reference | render init; no port counterpart (UM_001D19D0) | S0_title* |
| 0x001D98A0 | — | NM | verified-unbound | em_shadow_original — test_shadow_original_reference.py |  | S2_opening |
| 0x001D9EE0 | — | BM | verified-unbound | em_shadow_original, em_shadow_gs — test_shadow_original_reference.py |  | S2_opening |
| 0x001DA080 | — | BM | verified-unbound | em_shadow_original (inline) — test_render_verify_rest_reference (both outcomes of every branch asserted) | module not linked | S2_opening |
| 0x001DA1E0 | — | BM | verified-unbound | em_render_verify_rest em_rvr_001DA1E0 — test_render_verify_rest_reference | module not linked | S2_opening |
| 0x001DA290 | — | CL | verified-unbound | em_shadow_original, em_shadow_gs — test_shadow_original_reference.py |  | S2_opening |
| 0x001DA310 | — | NM | verified-unbound | em_shadow_original box_pass — test_render_verify_rest_reference | module not linked | S2_opening |
| 0x001DA6A0 | — | NM | verified-unbound | em_shadow_original, em_roger_actor_original — test_roger_actor_original_reference.py, test_shadow_actor_route_reference.py, test_shadow_original_reference.py | Roger's 001BA580 reaches it: reported no-effect binding (UM_001DA6A0, census L22; the port draws no actor shadow) | S2_opening |

### 3.17 Render context, background, weather and snow (0x1DB000..0x1EEFFF)

30 functions, 3,752 instructions: live 4, verified-unbound 26 (recount 2026-09-25).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001DD7B0 | — | NM | verified-unbound | em_render_context em_render_context_001DD7B0 — test_render_context_reference | not bound | S1_newgame_load* |
| 0x001DD940 | — | BM | verified-unbound | em_render_context em_render_context_001DD940 — test_render_context_reference | not bound | S1_newgame_load* |
| 0x001DD950 | — | BM | verified-unbound | em_render_context em_render_context_001DD950 — test_render_context_reference | em_interaction_projection.c | S1_newgame_load |
| 0x001DD980 | — | BM | live | em_interaction_projection.c (also the AREA11 script host's op00 publication, census L19) — test_interaction_projection_reference; test_level_smoke.py (truck_preview) | the camera's calls (0018BC20 action 8, 001B0460) publish the live camera's projection since census L13..L16 | S1_newgame_load |
| 0x001DDA00 | — | BM | verified-unbound | em_render_context em_render_context_001DDA00 — test_render_context_reference | not bound | S2_opening |
| 0x001DDAA0 | — | BM | verified-unbound | em_render_context em_render_context_001DDAA0 — test_render_context_reference | not bound | S2_opening |
| 0x001DDE10 | — | BM | verified-unbound | em_render_context em_render_context_001DDE10 — test_render_context_reference | not bound | S2_opening |
| 0x001DEEE0 | — | BM | verified-unbound | em_render_context em_render_context_001DEEE0 — test_render_context_reference | not bound | S2_opening |
| 0x001DFA40 | — | NM | verified-unbound | em_load_veil_particles — test_load_veil_particles_reference.py |  | S1_newgame_load* |
| 0x001E0C30 | — | BM | verified-unbound | em_render_context em_render_context_001E0C30 — test_render_context_reference | not bound | S1_newgame_load* |
| 0x001E0C60 | — | BM | verified-unbound | em_render_context em_render_context_001E0C60 — test_render_context_reference | not bound | S0_title |
| 0x001E0C80 | — | CL | verified-unbound | em_render_context em_render_context_001E0C80 — test_render_context_reference | not bound | S1_newgame_load |
| 0x001E0CC0 | — | BM | verified-unbound | em_render_context em_render_context_001E0CC0 — test_render_context_reference | the live um_001E0CC0 is a reported no-effect binding | S1_newgame_load |
| 0x001E0CF0 | — | BM | verified-unbound | em_render_verify_rest em_rvr_001E0CF0 — test_render_verify_rest_reference | module not linked | S2_opening |
| 0x001E0D70 | — | BM | verified-unbound | em_render_context em_render_context_001E0D70 — test_render_context_reference | not bound | S2_opening |
| 0x001E0DF0 | — | BM | verified-unbound | em_render_context em_render_context_001E0DF0 — test_render_context_reference | not bound | S1_newgame_load |
| 0x001E1010 | — | BM | verified-unbound | em_render_context em_render_context_001E1010 — test_render_context_reference | not bound | S1_newgame_load* |
| 0x001E1E60 | — | NM | live | em_background_gs, em_gfx_metal: em_gfx_background_draw first in every world frame, gated as 001D2300 gates its CALL (section 1.13) — test_background_reference.py (sky metric: 0 black, mean (48,48,48) as the original) |  | S2_opening |
| 0x001E2260 | — | BM | verified-unbound | em_render_verify_rest em_rvr_001E2260 — test_render_verify_rest_reference | module not linked | S1_newgame_load |
| 0x001E2270 | — | BM | verified-unbound | em_render_verify_rest em_rvr_001E2270 — test_render_verify_rest_reference | module not linked | S1_newgame_load |
| 0x001E2290 | — | BM | verified-unbound | em_head_sprite_original — test_head_sprite_reference | pool node 'head-bone sprite effect: UNBOUND' | S2_opening* |
| 0x001E23A0 | — | BM | verified-unbound | em_head_sprite_original — test_head_sprite_reference | pool node 'head-bone sprite effect: UNBOUND' | S2_opening* |
| 0x001E2560 | — | BM | verified-unbound | em_head_sprite_original — test_head_sprite_reference | pool node 'head-bone sprite effect: UNBOUND' | S2_opening |
| 0x001E55F0 | — | NM | live | em_weather.c via em_snow_runtime (node 001E55F0) — test_weather_reference |  | S2_opening |
| 0x001E67C0 | — | NM | live | em_snow.c — test_snow_tiles_reference |  | S2_opening |
| 0x001EA240 | — | BM | verified-unbound | em_effect_original — test_effect_original_reference.py |  | 00_panel_no_battery |
| 0x001EBF10 | — | NM | verified-unbound | em_effect_kinds em_effect_kinds_001EBF10 — test_effect_kinds_reference | not bound: the truck's 0x80000049 entities are not spawned (the counted gap, L26) | 08_truck_crossing |
| 0x001EC1F0 | — | NM | verified-unbound | em_effect_kinds em_effect_kinds_001EC1F0 — test_effect_kinds_reference | not bound | 05_boxes |
| 0x001EC3F0 | — | NM | verified-unbound | em_effect_kinds em_effect_kinds_001EC3F0 — test_effect_kinds_reference | not bound | 00_panel_no_battery |
| 0x001EC470 | — | NM | verified-unbound | em_effect_kinds em_effect_kinds_001EC470 — test_effect_kinds_reference | not bound | 06_hill_slide |

### 3.18 Effects (0x1EF000..0x1F8FFF)

31 functions, 3,127 instructions: live 3, verified-unbound 28 (recount 2026-09-25).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001EF940 | — | BM | verified-unbound | em_effect_original — test_effect_original_reference.py |  | S1_newgame_load |
| 0x001EF9D0 | — | NM | verified-unbound | em_area11_bindings, em_player_misc_workers — test_effect_original_reference.py, test_head_sprite_reference.py, test_player_misc_workers_reference.py |  | S1_newgame_load |
| 0x001EFD20 | — | BM | verified-unbound | em_area11_bindings, em_effect_original — test_effect_original_reference.py, test_truck_original_reference.py | the truck 00823FF0's 32 spawns (0x80000049) reach the counted gap since census L23 (em_area11_boxes_effect_gap; no live effect owner, L26) | S1_newgame_load |
| 0x001EFD90 | — | BM | verified-unbound | em_effect_original, em_player_slide — test_effect_original_reference.py, test_player_climb_reference.py, test_player_fall_reference.py |  | 00_panel_no_battery |
| 0x001F0120 | — | BM | verified-unbound | em_area11_bindings, em_head_sprite_original — test_head_sprite_reference.py | Roger's 008237E0 init calls it with key 0x47 (em_area11_bindings_spawn_001F0120, census L22); the former first-tick spawn is deleted | S2_opening* |
| 0x001F0310 | — | BM | verified-unbound | em_effect_kinds em_effect_kinds_001F0310 — test_effect_kinds_reference | not bound | S0_title* |
| 0x001F0360 | — | BM | verified-unbound | em_effect_manager em_effect_manager_001F0360 — test_effect_manager_reference | the live w_001F0360 is a reported no-effect binding (UM_001F0360) | S2_opening |
| 0x001F03D0 | — | BM | verified-unbound | em_effect_kinds em_effect_kinds_001F03D0 — test_effect_kinds_reference | not bound | S0_title* |
| 0x001F0720 | — | NM | verified-unbound | em_effect_manager em_effect_manager_001F0720 — test_effect_manager_reference | no live effect owner (L26) | S2_opening |
| 0x001F0A60 | — | AU | verified-unbound | em_effect_manager em_effect_manager_001F0A60 — test_effect_manager_reference | no live effect owner (L26) | S3_first_control_idle |
| 0x001F1110 | — | BM | live | em_pickup_items_original em_pickup_aura_001F1110 (bound in em_area11_interaction_host.c) — test_pickup_items_reference (executes 001F1110) |  | S2_opening* |
| 0x001F1180 | — | NM | live | em_pickup_items_original em_pickup_aura_001F1180 (bound in em_area11_interaction_host.c) — test_pickup_items_reference (executes 001F1180 with its SDK leaves) | live except its draw block (0x1F136C..0x1F1470), which is the no-op stand-in aura_draw in em_area11_interaction_host.c; the translated draw is em_effect_manager 001F0A60 (L26) | S2_opening |
| 0x001F3FA0 | — | NM | verified-unbound | em_effect_kinds em_effect_kinds_001F3FA0 — test_effect_kinds_reference | not bound | S0_title* |
| 0x001F40C0 | — | BM | verified-unbound | em_effect_manager em_effect_manager_001F40C0 — test_effect_manager_reference | no live effect owner (L26) | S2_opening |
| 0x001F4D40 | — | AI | verified-unbound | em_effect_manager em_effect_manager_001F4D40 — test_effect_manager_reference | no live effect owner (L26) | S2_opening |
| 0x001F54E0 | — | AW | live | em_effect_kinds em_effect_kinds_001F54E0 (the indicator children's colour; em_effect_delta deleted) — test_effect_kinds_reference, test_census_unverified_reference |  | S2_opening |
| 0x001F5640 | — | BM | verified-unbound | em_effect_kinds em_effect_kinds_001F5640 — test_effect_kinds_reference | not bound | S2_opening |
| 0x001F5940 | — | BM | verified-unbound | em_effect_kinds em_effect_kinds_001F5940 — test_effect_kinds_reference | not bound | S2_opening |
| 0x001F5C20 | — | BM | verified-unbound | em_effect_kinds em_effect_kinds_001F5C20 — test_effect_kinds_reference | not bound | S2_opening |
| 0x001F5CA0 | — | BM | verified-unbound | em_effect_kinds em_effect_kinds_001F5CA0 — test_effect_kinds_reference | not bound | S2_opening |
| 0x001F6210 | — | BM | verified-unbound | em_effect_manager em_effect_manager_001F6210 — test_effect_manager_reference | no live effect owner (L26) | S2_opening |
| 0x001F6640 | — | BM | verified-unbound | em_effect_kinds em_effect_kinds_001F6640 — test_effect_kinds_reference | the room point-light list is resolved offline by tools/export_point_lights.py and adopted by em_point_light.c (critic 7.2) | S1_newgame_load* |
| 0x001F66F0 | — | BM | verified-unbound | em_effect_kinds em_effect_kinds_001F66F0 — test_effect_kinds_reference | the room point-light list is resolved offline by tools/export_point_lights.py and adopted by em_point_light.c (critic 7.2) | S1_newgame_load* |
| 0x001F6760 | — | BM | verified-unbound | em_effect_kinds em_effect_kinds_001F6760 — test_effect_kinds_reference | the room point-light list is resolved offline by tools/export_point_lights.py and adopted by em_point_light.c (critic 7.2) | S1_newgame_load* |
| 0x001F6850 | — | BM | verified-unbound | em_effect_kinds em_effect_kinds_001F6850 — test_effect_kinds_reference | not bound | S1_newgame_load* |
| 0x001F68B0 | — | BM | verified-unbound | em_effect_kinds em_effect_kinds_001F68B0 — test_effect_kinds_reference | not bound | S1_newgame_load* |
| 0x001F6BB0 | — | NM | verified-unbound | em_effect_manager em_effect_manager_001F6BB0 — test_effect_manager_reference | no live effect owner (L26) | S2_opening |
| 0x001F6D60 | — | BM | verified-unbound | em_effect_kinds em_effect_kinds_001F6D60 — test_effect_kinds_reference | the room point-light list is resolved offline by tools/export_point_lights.py and adopted by em_point_light.c (critic 7.2) | S1_newgame_load* |
| 0x001F6E40 | — | BM | verified-unbound | em_effect_kinds em_effect_kinds_001F6E40 — test_effect_kinds_reference | the room point-light list is resolved offline by tools/export_point_lights.py and adopted by em_point_light.c (critic 7.2) | S1_newgame_load* |
| 0x001F6EB0 | — | BM | verified-unbound | em_effect_manager em_effect_manager_001F6EB0 — test_effect_manager_reference | no live effect owner (L26) | S2_opening |
| 0x001F8D30 | — | NM | verified-unbound | em_shadow_actor_route — test_shadow_actor_route_reference.py |  | 02_elevator_refusal |

### 3.19 Streams, sound and message service (0x1F9000..0x1FDFFF)

39 functions, 3,096 instructions: live 14, verified-unbound 24, stand-in 1 (recount 2026-09-25).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001F9100 | — | BM | verified-unbound | em_shadow_actor_route — test_shadow_actor_route_reference.py |  | 02_elevator_refusal |
| 0x001F9CF0 | — | NM | verified-unbound | em_stream_lanes_original — test_stream_lanes_reference.py |  | S0_title |
| 0x001FA0D0 | — | NM | verified-unbound | em_stream_lanes_original — test_stream_lanes_reference.py |  | S0_title |
| 0x001FA330 | — | NM | verified-unbound | em_stream_lanes_original — test_stream_lanes_reference.py |  | S0_title |
| 0x001FA570 | — | BM | verified-unbound | em_stream_lanes_original — test_stream_lanes_reference.py |  | S0_title |
| 0x001FA5A0 | — | BM | verified-unbound | em_message_service — test_message_service_reference.py |  | 10_cage_roof_roger |
| 0x001FA5F0 | — | BM | verified-unbound | em_stream_lanes_original — test_stream_lanes_reference.py |  | S0_title |
| 0x001FA790 | — | NM | verified-unbound | em_stream_lanes_original — test_stream_lanes_reference | live call is a reported no-effect binding | S1_newgame_load |
| 0x001FAAC0 | — | BM | verified-unbound | em_stream_lanes_original, em_scene_bindings — test_stream_lanes_reference.py |  | S0_title |
| 0x001FAB50 | — | BM | verified-unbound | em_stream_lanes_original — test_stream_lanes_reference | live call is a reported no-effect binding | S0_title |
| 0x001FAB80 | — | BM | live | em_message_service via em_message_live (step F) (its 001FAAC0 calls on the idle voice lanes do nothing) — test_message_service_reference.py; em_stream_lanes_original — test_stream_lanes_reference.py |  | S0_title |
| 0x001FABB0 | — | BM | verified-unbound | em_stream_lanes_original — test_stream_lanes_reference | w_001FABB0 / em_scene_bindings_001FABB0 -> em_bgm_stop / em_opening_media_stop (native streams; Roger's EM_ROGER_STOP_STREAMS since census L22) | S0_title |
| 0x001FABF0 | — | NM | verified-unbound | em_stream_lanes_original — test_stream_lanes_reference.py |  | S1_newgame_load |
| 0x001FAE70 | — | BM | verified-unbound | em_stream_lanes_original — test_stream_lanes_reference | em_scene_bindings.c: state 5 and the a0 = 0 resume (Roger's EM_ROGER_RESUME_MUSIC, 001B7A30) translated, unverified; the stream lane itself is the port's em_opening_media / em_bgm | S1_newgame_load |
| 0x001FB100 | — | BM | verified-unbound | em_startup_load_gaps_sound em_slg_001FB100 — test_startup_load_gaps_reference | em_sfx.c per-frame bookkeeping | S0_title |
| 0x001FB370 | — | BM | verified-unbound | em_startup_load_gaps_sound em_slg_001FB370 — test_startup_load_gaps_reference | not bound (the sound-bank loader chain) | S1_newgame_load* |
| 0x001FB3E0 | — | NM | verified-unbound | em_startup_load_gaps_sound em_slg_001FB3E0 — test_startup_load_gaps_reference | not bound (the sound-bank loader chain) | S1_newgame_load* |
| 0x001FB910 | — | BM | verified-unbound | em_startup_load_gaps_sound em_slg_001FB910 — test_startup_load_gaps_reference | not bound (the sound-bank loader chain) | S1_newgame_load* |
| 0x001FB9F0 | — | NM | live | em_sfx.c sfx_start / em_sfx_bank.c — test_area11_sfx_reference; test_area11_sfx_runtime.py | AREA11 cues re-exported; legacy ids still sharp (H19); since census L10 the player closure's 001FB9F0(id, 0x1000, 0x1000, 0x1000) calls (the ladder's 0x107 / 0x10E / 0x10F, not in the exported registry: silent, WP-14) are em_sfx_play, other request words fault | S0_title |
| 0x001FBC50 | — | BM | verified-unbound | em_stream_lanes_original — test_stream_lanes_reference | w_001FBC50 -> em_sfx_stop_all (live translation, no oracle); stream gain 0x1999 reported (H22) | S0_title |
| 0x001FBD50 | — | BM | live | em_sfx.c / em_sfx_bank.c play path — test_area11_sfx_reference | as 001FB9F0 | S2_opening |
| 0x001FBDB0 | — | AW | verified-unbound | em_sfx_bank — test_area11_sfx_reference.py |  | 09_fence_door |
| 0x001FBF50 | — | BM | verified-unbound | em_player_misc_workers — test_player_misc_workers_reference | em_sfx.c em_sfx_play_at (no gain oracle, AM-19) | S2_opening |
| 0x001FC280 | — | NM | verified-unbound | em_stream_lanes_original — test_stream_lanes_reference | em_scene_bindings.c w_001FAE70 (state 5 translated, unverified; state 0 reported) | S1_newgame_load |
| 0x001FC3C0 | — | BM | verified-unbound | em_sfx, em_sfx_bank — test_area11_sfx_reference.py, test_area11_sfx_runtime.py |  | S2_opening |
| 0x001FC6E0 | — | BM | verified-unbound | em_startup_load_gaps_sound em_slg_001FC6E0 — test_startup_load_gaps_reference | not bound (the sound-bank loader chain) | S0_title |
| 0x001FC770 | — | BM | live | em_message_draw_original via em_message_live — test_message_draw_reference.py |  | S2_opening |
| 0x001FC7B0 | — | NM | live | em_message_glyph_original via em_message_live — test_message_glyph_reference.py |  | S2_opening |
| 0x001FC9B0 | — | CL | live | em_message_service em_message_reset via em_message_live (w_001FC9B0 and the service's own teardown) — test_message_service_reference |  | S1_newgame_load |
| 0x001FCA10 | — | BM | live | em_message_service via em_message_live (step F, installed at bring-up) — test_message_service_reference, test_panel_message_reference, test_roger_media_reference | em_director.c for the director lines (WP-10); modes 3/4 fault (their presenters are untranslated; the status page presents its own mode 4) | S0_title |
| 0x001FCB90 | — | CL | stand-in |  | untranslated mode-4 presenter: the AREA11 status page presents its mode-4 lines from its own copy of the request block (em_area11_interaction_host.c, WP-5); em_hud.c legacy message lookup | 01_battery |
| 0x001FCF10 | — | BM | verified-unbound | em_render_verify_rest em_rvr_001FCF10 — test_render_verify_rest_reference | not bound | 03_panel_power |
| 0x001FD470 | — | BM | live | em_scene_bindings_001FD470 (bit 0 w_001FBC50, bit 1 w_001FABB0) as the message service's stream_stop; em_stream_lanes_original — test_stream_lanes_reference.py |  | S2_opening |
| 0x001FD4C0 | — | BM | live | em_message_service em_message_stream_request via em_message_live (the opening's 001B82D0 op12) — test_message_service_reference.py, test_area_script_reference.py, test-opening-runtime |  | S2_opening |
| 0x001FD580 | — | BM | live | em_message_service via em_message_live (step F) — test_message_service_reference.py | a voiced record faults at 001FA5A0 (the voice lanes are not live) | S2_opening |
| 0x001FD6A0 | — | BM | live | em_message_service via em_message_live (step F) — test_message_service_reference.py | as 001FD580 | S2_opening |
| 0x001FD790 | — | NM | live | em_message_service via em_message_live (step F) — test_message_service_reference, test_roger_media_reference, test_panel_message_reference |  | S2_opening |
| 0x001FD950 | — | NM | live | em_message_service via em_message_live (step F); its draw half em_message_draw_original — test_message_service_reference, test_message_draw_reference, test_message_capture |  | S2_opening |
| 0x001FDB80 | — | NM | live | em_message_service via em_message_live (step F) — test_message_service_reference, test_roger_media_reference, test_panel_message_reference |  | S2_opening |

### 3.20 Message draw (0x1FE000..0x1FEFFF)

7 functions, 400 instructions: live 6, verified-unbound 1 (recount 2026-09-25).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001FE070 | — | NM | live | em_message_draw_original via em_message_live — test_message_draw_reference.py |  | S2_opening |
| 0x001FE460 | — | BM | live | em_message_draw_original via em_message_live — test_message_draw_reference.py |  | S2_opening |
| 0x001FE480 | — | AI | live | em_message_draw_original via em_message_live — test_message_draw_reference.py |  | S2_opening |
| 0x001FE4B0 | — | BM | live | em_message_draw_original em_message_bank_records via em_message_live — test_message_draw_reference.py (executes 001FE4B0) |  | S2_opening |
| 0x001FE4D0 | — | BM | live | em_message_draw_original em_message_bank_record via em_message_live — test_message_draw_reference.py (executes 001FE4D0) |  | S2_opening |
| 0x001FE530 | — | AW | live | em_message_draw_original via em_message_live — test_message_draw_reference.py |  | S2_opening |
| 0x001FEF70 | — | BM | verified-unbound | em_status_scene_original — test_status_scene_reference.py |  | S1_newgame_load |

### 3.21 Status UI: hub, ITEM/BATTERY pages, pickups (0x207000..0x21AFFF)

19 functions, 3,798 instructions: live 10, verified-unbound 7, unverified 1, stand-in 1 (recount 2026-09-25).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x00209280 | — | NM | live | em_status_draw.c — test_status_draw_reference |  | 01_battery |
| 0x0020A7A0 | — | NM | live | em_status_background.c — test_status_background_reference |  | 01_battery |
| 0x0020AE40 | — | BM | verified-unbound | em_status_ui_leftovers em_sul_0020AE40 — test_status_ui_leftovers_reference (packet bytes equal the captured BATTERY page) | em_battery_ui.c hand-placed sprites; test_battery_reference ignores and test_battery_pickup_reference hooks it (critic 7.2); the UI+0x20 clock ownership is checked by test_status_hub_ui_reference | 01_battery |
| 0x0020B0D0 | — | BM | verified-unbound | em_status_ui_leftovers em_sul_0020B0D0 — test_status_ui_leftovers_reference (packet bytes equal the captured BATTERY page) | em_battery_ui.c hand-placed sprites; test_battery_reference ignores and test_battery_pickup_reference hooks it (critic 7.2) | 01_battery |
| 0x0020B210 | — | NM | verified-unbound | em_status_ui_leftovers em_sul_0020B210 — test_status_ui_leftovers_reference (packet bytes equal the captured BATTERY page) | em_battery_ui.c hand-placed sprites; test_battery_reference ignores and test_battery_pickup_reference hooks it (critic 7.2) | 01_battery |
| 0x0020BEF0 | — | BM | verified-unbound | em_status_ui_leftovers em_sul_0020BEF0 — test_status_ui_leftovers_reference | not bound | 01_battery |
| 0x0020CCB0 | — | BM | stand-in |  | em_battery_ui.c native page draw: no port code names 0020CCB0 and test_battery_reference lists it in its ignored set; no translation | 03_panel_power |
| 0x0020CD40 | — | BM | verified-unbound | em_status_ui_leftovers em_sul_0020CD40 — test_status_ui_leftovers_reference | em_hud.c / em_battery_ui.c UI cue requests (no samples exported; silent, WP-14) | 03_panel_power |
| 0x0020CD60 | — | BM | verified-unbound | em_status_ui_leftovers em_sul_0020CD60 — test_status_ui_leftovers_reference | em_hud.c / em_battery_ui.c UI cue requests (no samples exported; silent, WP-14) | 01_battery |
| 0x0020CDA0 | — | BM | verified-unbound | em_status_ui_leftovers em_sul_0020CDA0 — test_status_ui_leftovers_reference | em_hud.c / em_battery_ui.c UI cue requests (no samples exported; silent, WP-14) | 03_panel_power |
| 0x0020CDC0 | — | NM | live | em_status_runtime.c / em_status_page.c page core (w_0020CDC0) — test_status_page_reference; test_status_hub_reference; test_level_smoke.py | MAP/SPR4/DATABASE pages fault (untranslated); module-0x21 wait instant (H7) | 01_battery |
| 0x0020DFA0 | — | BM | unverified | em_status_models.c configure (the page event CONFIGURE) | test_status_page_reference only hooks it as worker event 2; the models fixture (tests/*.c) is not an oracle | 01_battery |
| 0x0020E020 | — | BM | live | em_item_root.c / em_status_hub_ui.c trail adapter — test_status_hub_ui_reference (executes 0020E020); test_item_root_reference hooks it as a worker |  | 01_battery |
| 0x0020E060 | — | BM | live | em_status_runtime.c page reset (w_0020E060) — test_status_hub_ui_reference; test_status_frame_reference |  | 01_battery |
| 0x0020E080 | — | BM | live | em_status_page.c / host status_page_event — test_status_page_reference |  | 01_battery |
| 0x0020E0C0 | — | BM | live | em_status_runtime.c exit — test_level_smoke.py (two-tick close) |  | 01_battery |
| 0x0020EE50 | — | BM | live | em_item_root.c — test_item_root_reference |  | 01_battery |
| 0x002149F0 | — | NM | live | em_area11_interaction_host.c battery_finished + em_battery_ui.c — test_battery_pickup_reference; test_level_smoke.py (239-frame notice) |  | 01_battery |
| 0x00219550 | — | NM | live | em_pickup_owner em_pickup_owner_tick via em_pickup.c and the AREA11 interaction host — test_pickup_owner_reference (executes 00219550 and compares the tick) | live since WP-6 (81414be) for the six AREA11 items | S2_opening |

### 3.22 Load veil, fog, stage workers, cinematic timeline (0x21B000..0x22FFFF)

25 functions, 3,097 instructions: live 13, verified-unbound 11, stand-in 1 (recount 2026-09-25).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x0021B180 | — | BM | live | em_scene_bindings, em_load_veil — test_area_load_reference.py |  | S1_newgame_load* |
| 0x0021B1B0 | — | AW | verified-unbound | em_load_veil_particles — test_load_veil_particles_reference | live calls reported, not drawn (the load shows black) | S1_newgame_load* |
| 0x0021B500 | — | BM | verified-unbound | em_load_veil_particles — test_load_veil_particles_reference | live calls reported, not drawn (the load shows black) | S1_newgame_load* |
| 0x0021B550 | — | BM | live | em_scene_bindings, em_load_veil — test_area_load_reference.py, test_room_move_reference.py, test_scene_task_reference.py |  | S1_newgame_load* |
| 0x0021B840 | — | BM | live | em_scene_bindings, em_load_veil — test_area_load_reference.py |  | S1_newgame_load* |
| 0x0021B8E0 | — | BM | verified-unbound | em_status_ui_leftovers em_sul_0021B8E0 — test_status_ui_leftovers_reference | not bound | S1_newgame_load |
| 0x0021B900 | — | BM | verified-unbound | em_status_ui_leftovers em_sul_0021B900 — test_status_ui_leftovers_reference | not bound | S0_title |
| 0x0021B920 | — | BM | live | em_packet_chain_original em_packet_chain_0021B920, called by em_fog_gs_coefficients for the Metal world fog (em_gfx_fog) — test_packet_chain_reference.py, test_area11_fog_reference.py (the helper equals the EE model on 2,006 pairs and each in-scope route beat's context +0xA8/+0xAC) | the (near, far) it is given is the scene manifest's record pair, not a live render-context block: 0021B970 / 0021B9A0 and the frame head 001D1C50 are not bound (L32) | S0_title |
| 0x0021B970 | — | BM | verified-unbound | em_fog_gs, em_game — test_area11_fog_reference.py |  | S1_newgame_load |
| 0x0021B9A0 | — | NM | verified-unbound | em_packet_chain_original em_packet_chain_0021B9A0 (from the .s and its jump table) — test_packet_chain_reference.py | no live caller binds it: the frame head 001D1C50 (L32), the effects (001EA240, 001F0A60, 001F5940), 001E67C0 (em_snow_runtime keeps its own fog constants); the area script's (Roger's 0x8283D0) is a reported no-effect binding (UM_0021B9A0, census L22) | S0_title |
| 0x0021BA70 | — | BM | verified-unbound | em_status_ui_leftovers em_sul_0021BA70 — test_status_ui_leftovers_reference | not bound | S1_newgame_load |
| 0x0021BA80 | — | BM | verified-unbound | em_fog_gs, em_game — test_area11_fog_reference.py |  | S1_newgame_load |
| 0x0021BAB0 | — | BM | verified-unbound | em_status_ui_leftovers em_sul_0021BAB0 — test_status_ui_leftovers_reference | not bound | S2_opening |
| 0x0021BAC0 | — | BM | verified-unbound | em_status_ui_leftovers em_sul_0021BAC0 — test_status_ui_leftovers_reference | not bound | 01_battery |
| 0x0021BAE0 | — | BM | stand-in |  | em_status_page.c END_PROJECTION event only clears a flag; the original copies a 32-byte record (slot 0 +0x120 to +0xA0 of D_00275670); test_status_page_reference intercepts it as worker event 10 (critic 7.2) | 01_battery |
| 0x0021BB00 | — | AW | live | em_player_stage_workers em_player_0021BB00 (0021C440 / 0015D100 / 00182B30, L01), em_script_host_workers — test_player_stage_workers_reference.py, test_script_host_workers_reference.py |  | S2_opening |
| 0x0021C3F0 | — | CL | live | em_player_stage_workers em_player_0021C3F0 (0021C440's hit_b gate, L01) — test_player_stage_workers_reference.py | D_00810770 is not canonical (L19): the stage's view load refuses area 8 room 2, where it is read | S2_opening |
| 0x0021C440 | — | BM | live | em_player_stage_workers em_player_stage_reaction (0015B130 / 0015B770, L01; em_player_damage.c's processor copy retired) — test_player_stage_workers_reference; test_level_smoke.py (no-hit path every stage) | its hit, pending-damage and infection paths reach fail-stop workers (rumble, effects, atan2 / the +20 object, 0017B490 / 001749A0) and unbound +4 = 2 states; no route capture has a hit | S2_opening |
| 0x0021D640 | — | BM | live | em_player_stage_workers em_player_0021D640 (0021C440, L01), em_player_reaction — test_player_stage_workers_reference.py |  | S2_opening |
| 0x00224290 | — | AW | live | em_player_fall, em_player_slide — test_player_fall_reference.py, test_player_slide_reference.py; test_level_smoke.py check_fall (the step-offs of routes 10, 11, 12 row for row); test_level_smoke.py check_crevice_jump (route 12 row for row) | live since census L09..L11 (the landing check of the falls and the jump); measured executing in the full smoke (census 1.9) | 10_cage_roof_roger |
| 0x002243F0 | — | AW | live | em_player_recovery, em_player_running_jump — test_player_recovery_reference.py, test_player_running_jump_reference.py; test_level_smoke.py check_crevice_jump (route 12 row for row) | live since census L11 (the jump's hit sub-state machine); measured executing in the full smoke (census 1.9) | 12_crevice_jump |
| 0x00224B80 | — | BM | live | em_player_recovery em_player_recovery_react_00224B80_worker, bound as the slide's damage worker — test_player_recovery_reference.py, test_player_slide_reference.py; test_level_smoke.py (06_hill_slide row for row, census L03) | live through the slide (0016C6A0 sub-state 3, every tick; returned 0 on the route) | 06_hill_slide |
| 0x0022EBE0 | — | CL | verified-unbound | em_status_ui_leftovers em_sul_0022EBE0 — test_status_ui_leftovers_reference | not bound | S2_opening |
| 0x0022EC30 | — | BM | live | em_cinematic_playback em_cinematic_playback_start via the script host's w_0022EC30 (op00 kind 6, bank 0x96 clip 0; census L22) — test_cinematic_playback_reference; test_level_smoke.py (roger: route 14 row for row) |  | S2_opening |
| 0x0022EEF0 | — | NM | live | em_cinematic_playback em_cinematic_playback_tick via em_area11_script_host_camera_0022EEF0 (em_camera top mode 3; census L22) — test_cinematic_playback_reference; test_level_smoke.py (roger: route 14 row for row) (camera eye / target while the camera byte is 3) | the live camera frame (census L13..L16) calls it at +4 == 3 (lw_0022EEF0); the opening track stays on em_opening_runtime.c (em_opening_runtime_camera_sample) | S2_opening |

### 3.23 AREA11 overlay owners (0x8235F0..0x828050, runtime addresses)

23 functions, 4,400 instructions: live 11, verified-unbound 12 (recount 2026-09-25).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x008235F0 | — | AU | live | em_area11_effect_runtime.c (node 008235F0) — test_area11_effect_reference | visuals; contact damage 00823580 not modelled (INV-17) | S2_opening |
| 0x008237C0 | — | CO | verified-unbound | em_startup_load_gaps em_slg_008237C0 — test_startup_load_gaps_reference | the roster spawn stands in for the overlay init | S1_newgame_load* |
| 0x008237E0 | — | AU | live | em_roger_actor_original em_roger_actor_008237E0_init via em_area11_roger (lifecycle 0; census L22) — test_roger_actor_original_reference; test_level_smoke.py (roger: route 14 row for row) | its 001CA6F0 bank and the +0x30 / +0x58 words from assets/scene_snow/roger/resources.emrs (tools/export_roger_banks.py) | S2_opening |
| 0x00823910 | — | AU | live | em_roger em_roger_tick via em_area11_roger (census L22) — test_roger_reference; test_level_smoke.py (roger: route 14 row for row) |  | S2_opening |
| 0x00823950 | — | AU | live | em_roger em_roger_tick via em_area11_roger (census L22) — test_roger_reference; test_level_smoke.py (roger: route 14 row for row) | the ordinary branch (0x8283D0) runs; the alternate 0x828990 needs D_00810793, which only the director (L21, still em_director.c) sets | 10_cage_roof_roger |
| 0x00823B70 | — | AU | live | em_roger em_roger_tick via em_area11_roger (census L22) — test_roger_reference; test_level_smoke.py (roger: route 14 row for row) |  | 14_roger_encounter |
| 0x00823CE0 | — | AU | verified-unbound | em_script_door_fan_husk em_husk_manager_tick — test_script_door_fan_reference (part 3) | dormant in the first visit (waits on D_00810788); the node is bound with no port code | S2_opening |
| 0x00823E80 | — | AU | live | em_area11_opening.c / em_opening_runtime.c — test_continue_reset_reference (0x823F74..80 slice); opening capture frame | partial oracle coverage | S2_opening |
| 0x00823FF0 | — | AU | live | em_truck_original via em_area11_boxes em_area11_boxes_truck_tick (census L23) — test_truck_original_reference; test_level_smoke.py (truck_crossing: route 08 row for row from the arm) | the effects 001EFD20 reach the counted gap (L26); sounds 0x454 / 0x455 not in the exported registry (WP-14) | S2_opening |
| 0x008251E0 | — | AU | live | em_truck_original em_truck_trigger_tick via em_area11_boxes (census L23) — test_truck_original_reference; test_level_smoke.py (truck_preview: route 07 row for row) |  | S2_opening |
| 0x008253F0 | — | AU | verified-unbound | em_director_original — test_director_original_reference | em_director.c kCineBeats keyframe player (H10); its beat step is the canonical D_00810813 and its beat-0 completion runs 001C4760(1, 1) (HK). The binding is prepared (em_area11_bindings tick_director_original over em_area11_script_host, census L21 2026-09-25) and selected only by the level smoke's director verification run: route 10 f1090..f1162 equal row for row, then the voiced line 0x7F stops it at 001FA5A0 (WP-8b) | S2_opening |
| 0x00825500 | — | AU | verified-unbound | em_director_original — test_director_original_reference | em_area11_flow.c trigger boxes / em_director.c | S2_opening |
| 0x00825540 | — | AU | verified-unbound | em_director_original — test_director_original_reference | em_area11_flow.c trigger boxes / em_director.c | S2_opening |
| 0x00825600 | — | AU | verified-unbound | em_director_original — test_director_original_reference | em_area11_flow.c trigger boxes / em_director.c | 10_cage_roof_roger |
| 0x00825640 | — | AU | verified-unbound | em_director_original — test_director_original_reference | em_area11_flow.c trigger boxes / em_director.c | 10_cage_roof_roger |
| 0x008256D0 | — | AU | verified-unbound | em_director_original — test_director_original_reference | em_area11_flow.c trigger boxes / em_director.c | 11_crevice_prompt |
| 0x00825710 | — | AU | verified-unbound | em_director_original — test_director_original_reference | em_area11_flow.c trigger boxes / em_director.c | 11_crevice_prompt |
| 0x008257A0 | — | AU | live | em_manager_008257A0.c — test_manager_8257a0_reference |  | S2_opening* |
| 0x00825940 | — | AU | verified-unbound | em_script_door_fan_husk em_husk_creature_tick (partial: lifecycles 1 and 4 fault) — test_script_door_fan_reference (part 3) | em_enemy.c legacy em_enemy_update (pool group 'enemies'; interim spawn) | S2_opening |
| 0x00827490 | — | AU | verified-unbound | em_script_door_fan_husk em_husk_partner_tick — test_script_door_fan_reference (part 3) | em_enemy.c legacy em_enemy_update (pool group 'enemies') | S2_opening |
| 0x00827630 | — | AU | verified-unbound | em_fan_original — test_fan_original_reference | static fans (em_pickup draw, no spin) | S2_opening |
| 0x00827B10 | — | AU | live | em_elevator.c / em_area11_interaction_host.c (node #27) — test_elevator_reference; test_level_smoke.py (routes 02/04) |  | S2_opening |
| 0x00828050 | — | AU | live | em_elevator.c em_elevator_motion_tick — test_elevator_reference; test_level_smoke.py (route 04) |  | 04_elevator_ride |

### 3.24 SDK VU0 math (0x1026A0..0x103237) and the VU1/DMA library functions that carry a translation

27 functions, 572 instructions: live 21, verified-unbound 6 (recount 2026-09-25).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001000E0 | — | BM | live | em_render_verify_rest em_rvr_001000E0 — test_render_verify_rest_reference; em_player_fall passes it the whole 64-bit double (9e0715f) | recount 2026-09-25: em_rvr_001000E0 executed on the live path (5 calls over the five measured runs) | 10_cage_roof_roger |
| 0x00100268 | — | BM | verified-unbound | em_load_veil_particles — test_load_veil_particles_reference |  | S0_title |
| 0x00100610 | — | BM | verified-unbound | em_load_veil_particles — test_load_veil_particles_reference |  | S1_newgame_load |
| 0x001006D8 | — | AI | verified-unbound | em_load_veil_particles — test_load_veil_particles_reference |  | S1_newgame_load |
| 0x001026A0 | — | AI | live | em_camera_rotation.c / em_owner_services_original.c (VU0 forms) — test_camera_rotation_reference; test_camera_retarget_reference |  | S1_newgame_load |
| 0x001026D0 | — | AI | live | em_locomotion_display em_loco_001026D0, em_effect_manager sdk_001026D0 — test_locomotion_display_reference, test_effect_manager_reference (run 001026D0 unhooked) | em_status_models.c w_001026D0: test_status_scene_reference records the call ("mul") and test_render_context_reference replays the original's bytes, so no oracle compares it; recount 2026-09-25: em_loco_001026D0 executed on the live path (18483 calls over the five measured runs) | S0_title |
| 0x00102718 | — | AI | live | em_effect_original / em_coll_* (inline) — test_effect_original_reference | recount 2026-09-25: em_effect_original_00102718 executed on the live path (36094 calls over the five measured runs) | S1_newgame_load |
| 0x00102738 | — | AI | live | em_coll_probe_original sdk_dot, em_actor_collision vu_dot, em_pickup_items_original vdot — test_coll_probe_reference.py, test_pickup_items_reference (both run 00102738 as original code inside the executed callers) |  | S2_opening |
| 0x00102760 | — | AW | live | em_pickup_items_original vnormalize (001F1180's facing test) — test_pickup_items_reference (runs 00102760 as original code) | the em_interaction_scan.c / em_camera_probe.c copies are checked only against the tests' normalize models | S1_newgame_load |
| 0x00102798 | — | AI | live | em_camera_commit_original em_camera_commit_00102798 (0018C0D0) — test_camera_live_reference (runs the original leaf); em_actor_light_001D89D0 sdk_00102798 — test_actor_light_001d89d0_reference | live since census L13..L16 (12008 calls); em_render_frame.c char_rig_build still re-derives the view basis in host float | S1_newgame_load |
| 0x001027E0 | — | AW | live | em_render_verify_rest em_rvr_001027E0 (inside em_cs_00102CD0) — test_render_verify_rest_reference; test_census_standins_reference | live since census L13..L16 (12008 calls) | S1_newgame_load |
| 0x00102850 | — | AI | verified-unbound | em_render_verify_rest em_rvr_00102850 — test_render_verify_rest_reference | not bound | 01_battery |
| 0x001028B8 | — | AI | live | em_coll_probe_original sdk_add (the live grid walkers) — test_coll_probe_reference.py (runs 001028B8 as original code) | the em_camera_probe.c inline copy is checked only against test_camera_probe_reference's model of the leaf | S2_opening |
| 0x001028D0 | — | AI | live | em_coll_probe_original sdk_sub, em_pickup_items_original vsub4 — test_coll_probe_reference.py, test_pickup_items_reference (run 001028D0 as original code) | the em_camera_retarget.c / em_camera.c inline copies are checked only against the retarget/probe/commit tests' models of the leaf | S1_newgame_load |
| 0x00102900 | — | AI | verified-unbound | em_actor_light_001D89D0 sdk_00102900, em_shadow_actor_route vu_scale_00102900 — test_actor_light_001d89d0_reference, test_shadow_actor_route_reference | em_snow.c inline scale; test_snow_tiles_reference models the leaf | S1_newgame_load |
| 0x00102918 | — | AI | live | em_owner_services_original.c (live through em_status_models) — test_owner_services_reference |  | S1_newgame_load |
| 0x00102948 | — | AI | verified-unbound | em_anim_runtime_rest (inline, 001CAAC0 / 001CB2C0) — test_anim_runtime_rest_reference (runs 00102948 unhooked); test_effect_manager_reference | the em_camera_retarget.c / em_camera.c / em_camera_probe.c copies are checked only against the tests' copy hooks | S0_title |
| 0x00102958 | copy_qw4 | AI | live | em_owner_services_original em_owner_services_copy_qw4_00102958 — test_owner_services_reference (executes it alone) | the em_elevator.c / em_status_scene_original.c copies are checked only against the elevator and status-scene tests' copy hooks; recount 2026-09-25: em_owner_services_copy_qw4_00102958 executed on the live path (82747 calls over the five measured runs) | S0_title |
| 0x001029C0 | — | AI | live | em_owner_services_original.c (live through em_status_models) — test_owner_services_reference |  | S0_title |
| 0x001029E8 | — | NM | live | em_owner_services_original.c (live through em_status_models) — test_owner_services_reference |  | S1_newgame_load |
| 0x00102A60 | — | AI | live | em_owner_services_original.c (live through em_status_models) — test_owner_services_reference |  | S1_newgame_load |
| 0x00102B08 | — | AI | live | em_owner_services_original.c (live through em_status_models) — test_owner_services_reference |  | S1_newgame_load |
| 0x00102BB0 | — | AI | live | em_owner_services_original.c (live through em_status_models) — test_owner_services_reference |  | S1_newgame_load |
| 0x00102C58 | — | BM | live | em_owner_services_original.c (live through em_status_models) — test_owner_services_reference |  | S1_newgame_load |
| 0x00102CD0 | — | BM | live | em_census_standins em_cs_00102CD0 (0018C0D0's look-at; em_camera.c camera_view_00102CD0 elsewhere) — test_census_standins_reference; test_camera_live_reference; test_level_smoke.py camera rows (census 1.11) | bound since census L13..L16 (12008 calls); em_math.h em_mat4_lookat_gs is deleted; the renderer takes em_cs_view_to_native of D_00810610 | S1_newgame_load |
| 0x001031E0 | — | BM | live | em_status_scene_original.c / em_camera_commit_original.c (inline, 0018C0D0) — test_camera_live_reference (executes the original 0018C0D0 with its leaves); test_status_scene_reference |  | S2_opening |
| 0x00103230 | — | AI | live | em_coll_probe_original sdk_scale (the live grid walkers) — test_coll_probe_reference.py (runs 00103230 as original code) | the em_camera_probe.c inline copy is checked only against test_camera_probe_reference's model of the leaf | S2_opening |

### 3.25 SDK libm, soft float and rand (0x11C4C8..0x12FFFF)

29 functions, 1,770 instructions: live 28, verified-unbound 1 (recount 2026-09-25).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x0011C4C8 | — | BM | live | em_sdk_math_original — test_sdk_math_original_reference | recount 2026-09-25: sdk_0011C4C8 executed on the live path (90061 calls over the five measured runs) | S1_newgame_load |
| 0x0011C7B0 | — | NM | live | em_item_sdk_math.c / em_sdk_math_original.c — test_item_sdk_math_reference; test_sdk_math_original_reference |  | S2_opening |
| 0x0011CB90 | — | AW | live | em_item_sdk_math.c / em_sdk_math_original.c — test_item_sdk_math_reference; test_sdk_math_original_reference |  | S0_title |
| 0x0011CCC8 | — | AW | live | em_item_sdk_math.c / em_sdk_math_original.c — test_item_sdk_math_reference; test_sdk_math_original_reference |  | S2_opening |
| 0x0011D770 | — | AW | live | em_item_sdk_math.c / em_sdk_math_original.c — test_item_sdk_math_reference; test_sdk_math_original_reference |  | S2_opening |
| 0x0011D878 | — | NM | verified-unbound | em_sdk_math_original — test_sdk_math_original_reference |  | S2_opening |
| 0x0011DB90 | — | BM | live | em_sdk_soft_float — test_sdk_soft_float_reference | recount 2026-09-25: em_sdk_soft_float_w_0011DB90 executed on the live path (10 calls over the five measured runs) | 03_panel_power |
| 0x0011DBB8 | — | NM | live | em_director_original / em_sdk_math_original — test_sdk_math_original_reference | FLOOR engaged since the Boxes step (em_collision_world_bind_player; EE model) | S1_newgame_load |
| 0x0011DE90 | — | AW | live | em_sdk_math_original — test_sdk_math_original_reference | recount 2026-09-25: em_sdk_math_original_float_0011DE90 executed on the live path (3690 calls over the five measured runs) | S3_first_control_idle |
| 0x0011DF78 | — | AI | live | em_sdk_math_original.c sdk_0011DF78 — test_sdk_math_original_reference; test_camera_commit_reference |  | S1_newgame_load |
| 0x0011E080 | — | AI | live | em_sdk_math_original — test_sdk_math_original_reference | recount 2026-09-25: sdk_0011E080 executed on the live path (233277 calls over the five measured runs) | S0_title |
| 0x0011E2A8 | — | AW | live | em_sdk_math_original.c (status background sine; since census L19 the AREA11 script host's op00 ease, em_area11_script_host w_0011E2A8) — test_sdk_math_original_reference; test_status_background_reference; tests/area_script_test.c (equal to em_area_script_sin_0011E2A8 on every ease argument); test_level_smoke.py (truck_preview camera eases) |  | S2_opening |
| 0x0011E398 | — | AI | live | em_sdk_math_original — test_sdk_math_original_reference | FLOOR engaged since the Boxes step (em_collision_world_bind_player; EE model) | S2_opening |
| 0x0011E620 | — | BM | live | em_sdk_math_original em_sdk_math_original_float_0011E620 — test_sdk_math_original_reference | FLOOR engaged since the Boxes step (em_collision_world_bind_player; EE model); also the live camera's atan2 since census L13..L16 (em_camera_live: 0018C0D0's heading into D_008106A0 and the camera workers' 0011E620 calls; test_camera_live_reference) | S1_newgame_load |
| 0x0011E748 | — | NM | live | em_item_sdk_math.c em_item_sdk_sqrt — test_item_sdk_math_reference; test_sdk_math_original_reference | `em_sdk_math_original_float_0011E748` (with the soft-float workers) is bound into the column and FLOOR (engaged since the Boxes step); the port's legacy wall-probe path is retired in AREA11; the live camera's sqrt worker (0018C0D0 and the follow core) since census L13..L16 | S0_title |
| 0x0011FD78 | — | BM | live | em_sdk_soft_float — test_sdk_soft_float_reference | recount 2026-09-25: em_sdk_soft_float_w_0011FD78 executed on the live path (10 calls over the five measured runs) | 03_panel_power |
| 0x00122BB8 | — | BM | live | em_random.c — test_random_seed_reference; test_random_reference.py |  | S1_newgame_load |
| 0x00126AB8 | — | AW | live | em_sdk_soft_float — test_sdk_soft_float_reference | below the soft-float worker 00128350, bound with them (soft-float step, 2026-09-24); recount 2026-09-25: em_sdk_soft_float_00126AB8 executed on the live path (25 calls over the five measured runs) | 03_panel_power |
| 0x00126BE8 | — | AW | live | em_sdk_soft_float — test_sdk_soft_float_reference | below 0011DB90 and 00127758, bound with them (soft-float step, 2026-09-24); recount 2026-09-25: em_sdk_soft_float_00126BE8 executed on the live path (40 calls over the five measured runs) | 03_panel_power |
| 0x00127398 | — | AW | live | em_sdk_soft_float — test_sdk_soft_float_reference | below the soft-float worker 0011DB90, bound with them (soft-float step, 2026-09-24); recount 2026-09-25: em_sdk_soft_float_00127398 executed on the live path (15 calls over the five measured runs) | 03_panel_power |
| 0x001274B0 | — | BM | live | em_sdk_soft_float — test_sdk_soft_float_reference | below the soft-float worker 0011DB90, bound with them (soft-float step, 2026-09-24); recount 2026-09-25: em_sdk_soft_float_001274B0 executed on the live path (15 calls over the five measured runs) | 03_panel_power |
| 0x00127728 | — | BM | live | em_sdk_soft_float — test_sdk_soft_float_reference | below the soft-float worker 00128350, bound with them (soft-float step, 2026-09-24); recount 2026-09-25: em_sdk_soft_float_00127728 executed on the live path (25 calls over the five measured runs) | 03_panel_power |
| 0x00127758 | — | AI | live | em_sdk_soft_float — test_sdk_soft_float_reference | recount 2026-09-25: em_sdk_soft_float_w_00127758 executed on the live path (10 calls over the five measured runs) | 03_panel_power |
| 0x001277B0 | — | AW | live | em_sdk_soft_float — test_sdk_soft_float_reference | below the soft-float worker 00127758, bound with them (soft-float step, 2026-09-24); recount 2026-09-25: em_sdk_soft_float_001277B0 executed on the live path (10 calls over the five measured runs) | 03_panel_power |
| 0x001278C0 | — | AW | live | em_weather.c / em_status_background.c (EE conversions) — test_weather_reference; test_status_background_reference |  | S1_newgame_load |
| 0x001281C0 | float_to_int | AI | live | em_player_stage_workers em_player_float_to_int (live through the player stage) — test_render_context_reference (a bound worker: the original callee runs and the native worker is compared), test_player_stage_workers_reference | the em_status_background.c to_int / em_snow.c copies are host models (both of their tests hook 001281C0; critic 7.2) | S1_newgame_load |
| 0x00128250 | — | AW | live | em_weather.c / em_status_background.c (EE conversions) — test_weather_reference; test_status_background_reference |  | S1_newgame_load |
| 0x00128320 | — | BM | live | em_sdk_soft_float — test_sdk_soft_float_reference | below the soft-float worker 00127758, bound with them (soft-float step, 2026-09-24); recount 2026-09-25: em_sdk_soft_float_00128320 executed on the live path (10 calls over the five measured runs) | 03_panel_power |
| 0x00128350 | — | AI | live | em_sdk_soft_float em_sdk_soft_float_00128350 — test_sdk_soft_float_reference | recount 2026-09-25: em_sdk_soft_float_w_00128350 executed on the live path (20 calls over the five measured runs) | 03_panel_power |

## 4. Platform boundaries

465 functions (24,612 instructions) are SDK, libc, IOP, driver or GS/VU1 work that the port replaces with a native service. They are not counted in the five statuses. "Contract" says whether anything compares the port's substitute with the original's observable effect.

Rules used: the SDK ranges of the decomp's SUBSYSTEMS.md (0x100000..0x12FFFF: DMA/VU1 library, libmpeg, kernel syscalls, libpad, libcdvd, libmc/SIF RPC, the EE sound library, the C runtime), the GS/VIF packet builders, texture uploads and display-object registry in 0x1CB5C0..0x1DAFFF, the module loader and disc reads 0x1FF080..0x2009E0, the IOP service, heap and MPEG glue in 0x203350..0x206D80, and the 2D draw layer. A function in the render ranges that carries a translation (the load-veil particles, shadow kernels, head sprite, message glyphs) is classified normally instead. The VU0 math leaves (0x1026A0..0x103237), libm (0x11C4C8..0x11FD77), soft float, the float conversions and rand are **game-visible arithmetic, not boundaries**: they are classified in section 3.

| Kind | Functions | Instructions | Port substitute | Contract verified |
|---|---:|---:|---|---|
| SDK libmpeg / IPU movie decode | 102 | 6,893 | em_movie_mac.m (AVFoundation) over movies exported by tools/export_movie.py | no original comparison (movie export test only) |
| EE sound library (SPU2 voices, sequencer, IOP sound RPC) | 59 | 3,207 | em_sfx.c + em_sfx_bank.c + em_audio_mac.c | 41: no (dry mixer; no SPU2 ADSR/reverb, AM-03/04); 9: partly: test_area11_sfx_reference; 5: partly: test_stream_lanes_reference; 3: partly: test_area11_sfx_reference, test_area11_sfx_runtime; 1: partly: test_area11_sfx_reference, test_area11_sfx_runtime, test_stream_lanes_reference |
| EE kernel syscalls, interrupts, threads, SIF DMA glue | 57 | 1,085 | host OS; nothing to reproduce | n/a |
| GS/VIF packet build / VU1 kick | 42 | 1,419 | native renderer (em_render_frame.c, em_gfx.h contract, gfx/metal/em_gfx_metal.m) | partly: level material and overlay-blend tests; fog, snow and status draws checked in their own tests |
| unidentified title-only SDK glue (probable MPEG) | 29 | 984 | em_frontend.c title / movie path | no |
| MPEG movie glue | 29 | 995 | em_movie_mac.m | no |
| C runtime (heap, stdio, string, init) | 25 | 2,885 | host libc | n/a |
| IOP service management | 18 | 584 | host OS; nothing to reproduce | n/a |
| heap / runtime | 18 | 982 | host allocator | n/a |
| SDK DMA/VIF/VU1 library | 13 | 768 | native renderer (em_render_frame.c, em_gfx.h contract, gfx/metal/em_gfx_metal.m) | not compared per call |
| SDK libmc / SIF RPC | 13 | 747 | local directory for the card check (STARTUP.md); RPC not needed | no |
| module loader and disc read | 12 | 1,039 | native area/module reads (em_game_legacy_area_load; status pages load instantly) | area-load tick sequence (test_area_load_reference); the module-0x21 wait is instant (H7) |
| SDK libcdvd disc read | 9 | 346 | host file reads of locally exported assets | n/a |
| 2D GS draw layer | 9 | 284 | em_gfx 2D layer (em_status_background_draw.c, em_status_draw.c) | 6: no; 1: draw commands compared: test_status_background_reference, test_status_draw_reference, test_status_hub_ui_reference, test_battery_reference; 1: draw commands compared: test_status_background_reference, test_status_draw_reference, test_status_hub_ui_reference; 1: draw commands compared: test_status_draw_reference, test_status_hub_ui_reference |
| GS/VIF packet build (sprites, flush) | 7 | 291 | native renderer (em_render_frame.c, em_gfx.h contract, gfx/metal/em_gfx_metal.m) | not compared per call |
| resource / display-object registry | 6 | 451 | locally exported assets (EMDL, roster, props) | no |
| GS texture upload | 6 | 426 | native renderer (em_render_frame.c, em_gfx.h contract, gfx/metal/em_gfx_metal.m) texture upload | not compared per call |
| C runtime string/format | 3 | 222 | host snprintf/strlen in em_status_hub_ui.c / em_message_draw_original.c | through the callers' oracles (test_status_hub_ui_reference, test_message_draw_reference) |
| stream / voice IOP service tick | 3 | 304 | em_bgm.c + em_opening_media.c native streams | no |
| 2D GS glyph/sprite draw | 1 | 113 | em_gfx 2D layer | draw commands compared by test_status_draw_reference / test_status_hub_ui_reference (001CC1E0 moved to section 3 in the recount) |
| IOP movie service | 2 | 230 | em_frontend.c movie pump (skip mask from 002036E0) | no |
| C runtime memset | 1 | 48 | host memset | trivially identical |
| overlay module dispatch | 1 | 309 | none: AREA11 code is native | n/a |

Addresses per kind:

- **SDK libmpeg / IPU movie decode** (102): 001033E0, 00103718, 00103DC8, 001041E8, 00104378, 001043F0, 00104488, 00104540, 00104610, 001046C0,
  00104778, 00104870, 00104970, 00104A18, 00104AC8, 00104BB0, 00104C98, 00104D78, 00104E48, 00104F70,
  00105088, 00105148, 00105170, 00105518, 00105628, 00105750, 00105878, 00105A78, 00105B48, 00106070,
  001060F8, 00106278, 001063B8, 001063E8, 00106490, 00106540, 001066F8, 00106830, 00106948, 00106AB0,
  00106B18, 00106B88, 00106CB0, 00106D80, 00106E30, 00107060, 00107098, 00107178, 001074B0, 00107590,
  00107648, 00107A28, 00107CB8, 00107CF0, 00107E88, 00108248, 00108300, 001084B0, 00108608, 00108640,
  00108660, 001086F8, 00108748, 00108790, 001087E8, 00108818, 00108AA0, 00108DB0, 00108EA8, 00108FF8,
  00109068, 001095F0, 00109698, 001098D8, 00109918, 00109A30, 00109A50, 00109A90, 00109AF8, 00109B20,
  00109B70, 00109C40, 00109C58, 00109C68, 00109C78, 00109CF8, 00109E68, 00109F90, 0010A140, 0010A1B0,
  0010A1C0, 0010A248, 0010A298, 0010A4D8, 0010A4F8, 0010A650, 0010A998, 0010AA80, 0010AB40, 0010ACA8,
  0010B0F8, 0010B160.
- **EE sound library (SPU2 voices, sequencer, IOP sound RPC)** (59): 001152D8, 001157F0, 00115850, 00116598, 00116DB8, 00117088, 00117428, 001176E0, 00117918, 001179E0,
  00117BA0, 00117C28, 00118078, 00118E60, 00118EC0, 001191F0, 001192D0, 001193A8, 00119400, 00119450,
  001194B8, 00119528, 00119810, 00119828, 00119890, 00119978, 001199F0, 00119EA0, 0011A070, 0011A198,
  0011A218, 0011A270, 0011A2B0, 0011A470, 0011A4B8, 0011A4D0, 0011A4E8, 0011A5C8, 0011A608, 0011A658,
  0011A6A0, 0011A6E8, 0011A730, 0011A758, 0011A770, 0011A788, 0011A7F0, 0011A830, 0011A848, 0011A888,
  0011A8C8, 0011A918, 0011A938, 0011A9D8, 0011AE88, 0011B328, 0011B5E0, 0011B910, 0011B9E0.
- **EE kernel syscalls, interrupts, threads, SIF DMA glue** (57): 0010B500, 0010B520, 0010B530, 0010B550, 0010B560, 0010B570, 0010B580, 0010B590, 0010B620, 0010B630,
  0010B640, 0010B670, 0010B6D0, 0010B720, 0010B740, 0010B750, 0010B760, 0010B800, 0010B820, 0010B830,
  0010B840, 0010B850, 0010B860, 0010B870, 0010BAA0, 0010BB90, 0010BBC0, 0010BBE0, 0010BC00, 0010BC10,
  0010BC50, 0010BCD0, 0010BD30, 0010BE68, 0010BF10, 0010BF18, 0010C020, 0010C0C8, 0010C290, 0010C2F8,
  0010C360, 0010C3C8, 0010C710, 0010C7E8, 0010CE28, 0010DD00, 0010DE38, 0010DEB8, 0010DFD8, 0010E088,
  0010E270, 0010E318, 0010E3A8, 0010E8A8, 0010EA60, 0010F8F8, 0010F968.
- **GS/VIF packet build / VU1 kick** (42): 001D1AE0, 001D1C10, 001D2090, 001D2110, 001D2130, 001D2160, 001D2180, 001D21E0, 001D2580, 001D2730,
  001D2E20, 001D37D0, 001D38F0, 001D3900, 001D3990, 001D3AD0, 001D3C30, 001D3CF0, 001D3D90, 001D3E40,
  001D3F50, 001D4650, 001D4740, 001D4750, 001D4960, 001D49D0, 001D4A90, 001D4B10, 001D4B20, 001D4B80,
  001D4C20, 001D4DA0, 001D4E20, 001D4EA0, 001D4F30, 001D6F60, 001D7100, 001D71A0, 001D71F0, 001D7410,
  001D9060, 001D9720.
- **unidentified title-only SDK glue (probable MPEG)** (29): 00205050, 00205240, 00205700, 00205740, 00205810, 002058D0, 00205990, 00205A00, 00205A50, 00205A80,
  00205A90, 00205B00, 00205BC0, 00205C60, 00205C80, 00205CD0, 00205D00, 00205D40, 00205DD0, 00205E10,
  00205E30, 00205E40, 00205EA0, 00205EE0, 00205F20, 00205F40, 00205F50, 00205F80, 00205F90.
- **MPEG movie glue** (29): 00206010, 00206030, 00206170, 002061B0, 00206200, 00206210, 00206320, 002063A0, 002063B0, 00206470,
  00206500, 00206520, 002065D0, 00206770, 00206810, 00206970, 00206A00, 00206B00, 00206B10, 00206B20,
  00206B30, 00206B80, 00206B90, 00206BA0, 00206BE0, 00206BF0, 00206CC0, 00206D10, 00206D80.
- **C runtime (heap, stdio, string, init)** (25): 0011FD88, 0011FE90, 00120058, 00120578, 001205D8, 00120AD0, 00120B10, 00120B98, 00120CE8, 00120F40,
  001216B8, 001216F8, 00121870, 00121920, 00121AE8, 00121AF0, 00122B58, 00122C48, 00122DE8, 001235D8,
  001236D8, 00123750, 00124EF8, 00124F58, 00125F48.
- **IOP service management** (18): 00203460, 002034C0, 00203980, 00203990, 002039A0, 002039D0, 00203A10, 00203A80, 00203AE0, 00203B20,
  00203B70, 00203B80, 00203BA0, 00203C30, 00203C90, 00203D30, 00203E60, 00203F40.
- **heap / runtime** (18): 00204080, 002040A0, 002040E0, 00204140, 002041A0, 002041D0, 00204250, 00204390, 00204490, 002044F0,
  00204700, 002047D0, 00204AE0, 00204B30, 00204B80, 00204BD0, 00204D60, 00204E90.
- **SDK DMA/VIF/VU1 library** (13): 001000B0, 00100278, 001002E0, 00100550, 001008C0, 001009C8, 00100A60, 001015A8, 00101810, 00101BB8,
  00101F08, 00101FE0, 00102468.
- **SDK libmc / SIF RPC** (13): 00112440, 00112610, 00112D18, 00112DC0, 00112E28, 00113280, 00113680, 00113C38, 00113C68, 00113CD0,
  00113D08, 00113F68, 001152B0.
- **module loader and disc read** (12): 001FF080, 001FF0D0, 001FF3F0, 001FF590, 001FF830, 001FFCD0, 00200730, 00200780, 00200830, 00200890,
  00200970, 002009E0.
- **SDK libcdvd disc read** (9): 00110AB8, 00110B38, 00110B80, 00111018, 001115D0, 00111818, 001118B8, 00111F18, 00112088.
- **2D GS draw layer** (9): 00207070, 002070A0, 002070D0, 00207100, 00207150, 00207290, 00207D00, 00207E40, 00207F80.
- **GS/VIF packet build (sprites, flush)** (7): 001CABA0, 001CB5C0, 001CB800, 001CB8A0, 001CB950, 001CBA40, 001CBC20.
- **resource / display-object registry** (6): 001CF870, 001CF970, 001CFAE0, 001CFFE0, 001D04B0, 001D0660.
- **GS texture upload** (6): 001CC8A0, 001CCB00, 001CCB10, 001CCBD0, 001CCCC0, 001CCE80.
- **C runtime string/format** (3): 00122EF0, 00123168, 001232E0.
- **stream / voice IOP service tick** (3): 001F9820, 001F9B20, 001F9BF0.
- **2D GS glyph/sprite draw** (1): 001CBA50.
- **IOP movie service** (2): 00203350, 002036E0.
- **C runtime memset** (1): 00121A28.
- **overlay module dispatch** (1): 001E7780.

Boundary notes:

- **Sound library.** The port plays AREA11 cues through the pitch path verified by `test_area11_sfx_reference` (00115850 bend, 00117918), but its mixer has no SPU2 ADSR, Gaussian interpolation or reverb (WP-14, AM-03/04). Stream gain 0x1999 is only reported (H22).
- **Module loader.** The native area read replaces 001FF080(1, 0) and the load arms run through the verified cores; the status-page module 0x1F/0x21 load is instant where the original waits 24 dispatches (H7).
- **Message glyphs (WP-8).** 001CC1E0 (tall-font strip layout) is translated and live in `em_message_glyph_original`; since the recount it is a section 3 row (live), as the render-range rule above requires. 001CC8A0/001CCB00 remain its upload/reset boundary there; `test_message_glyph_reference.py` compares every upload call and every packed pass with the executed original. The glyph texels are the port's atlas.
- **Translations inside boundaries (recount 2026-09-24).** Moved to section 3 by the render-range rule: 001D21B0, 001D2710, 001D2910, 001D2DE0, 001D2E00, 001D6B10, 001D6C90 (em_render_context helpers, test_render_context_reference) and 001D38A0, 001D3BA0 (em_owner_draw_original, test_owner_draw_reference), all verified-unbound. Kept as boundaries although they now carry translations (not bound; their oracles were not re-read in the recount): the IOP stream driver side 00112610, 00112D18, 00113280, 001157F0, 0011A2B0 (em_iop_stream, test_iop_stream_reference, WP-8b), the stream-lane SDK commands 00119828, 0011A4E8, 0011A608, 0011A658 and the IOP tick 001F9820 (em_stream_lanes_original), and the module loader 001FF080, 001FF0D0, 001FF3F0, 001FF830 (em_status_scene_original). Whether those become section 3 rows is a lead decision (the IOP/disc boundary of lane L36).
- **GS/VU1.** The renderer is the port's own. Individual packet builders have no per-call comparison; the level material, overlay blend, fog, snow and status draw tests compare their results where they exist. The game-side render heads (001D1C50, 001D1EA0, 001D30A0) and the projection helpers are **not** treated as boundaries: they are stand-ins in section 3.
- **Movies.** Title-only MPEG code and the unidentified 0x205050..0x205F90 glue (title only, adjacent to the MPEG glue) are replaced by the native movie path; no original comparison exists.

## 5. Gap lanes (prioritized)

Every non-live, non-boundary function belongs to exactly one lane. Sizes are in original instructions. "bind" lanes wire verified translations that already exist and delete the stand-in; "translate" lanes need new translations and oracles first. The order follows the route and the dependencies: the player stage and collision come first because every beat from 05 on needs them.

| # | Lane | Kind | Instr. | Functions (status mix) | Gates beats | Depends on |
|---:|---|---|---:|---|---|---|
| 1 | **L01-player-stage-live**: Engage the translated player stage (FLOOR gate): 0015BA50/0015B130/0015BCF0 and the stage workers, retire player_damage_tick. **Bound 2026-09-23 (partial):** 10 live (0015BCF0 tail only), 4 bound but unreached (0015B530, 001837A0, 00182B30, 00182D70: the interaction runtime still stands in for the scripted takeover and consumes the stage before 0015B130's prelude), 0015CF90 unverified (no oracle); the prelude on the port's idle/walk waits on 0017B490 (L12) | bind | 1,922 | 15 (live 10, verified-unbound 4, unverified 1) | every frame from first control; prerequisite of the floor, slide, climb, ladder and jump lanes | nothing (translations exist); retire em_player_damage.c copies in the same change (done) |
| 2 | **L05-coll-move-walkers**: Replace em_collision movement queries with the translated move/sweep walkers. **Live 2026-09-24 (Boxes step):** 0019AD00 / 0019AFE0 with em_coll_grid_hull's 0019CB60 / 001A6440 serve 001764E0's probes and the closure in AREA11; the hull world has no chain reader (AREA11 publishes no class-2 owner); the follow camera keeps em_collision.c (L13) | translate+bind | 2,163 | 8 (live 1, verified-unbound 5, missing 2) | every frame (player and camera movement queries) | a translation of 0019CB60 and 001A6440 with their oracles (COLL_MOVE.md section 4 item 2) |
| 3 | **L07-actor-collision-live**: Link em_actor_collision: grid/column scan, actor hulls, list classes and the 001AAD00 swap. **Live 2026-09-24:** the world, the lists, 001AAD00, the publication of the panel, terminal, items, crates and drums, and (Boxes step) the query half 0019AB20 / 0019F730 / 0019C830 / 0019BC40 / 001A5760 on em_coll_probe_original's prims and the EE model; the class 2/0xA and 0xD pushes wait on owners that publish them; the truck and prop cells wait on L23/L35 | bind | 2,224 | 13 (live 6, verified-unbound 7) | 05 (standing on crates), 08 (standing on the truck), 10 | L05 (queries), L25/L23 (owner cells) |
| 4 | **L02-floor-fall-live**: Bind the floor service and fall check (00175900/001796C0) and the fall state, retiring the floor snap and PLAYER_FALL_ENTRY. **Live 2026-09-24 (Boxes step):** FLOOR is engaged in AREA11 with every closure state bound (em_player_closure_live.c; FIRST_CONTROL.md "Engaged"); off-route helpers are fail-stop workers naming their originals **Census L09..L11 (2026-09-24, section 1.9):** the fall 00162DB0, the landing 00163B40 / 00163C10, 00179680, 00179880, 0017C580 and 00224290 ran in the walks' step-offs of routes 10, 11 and 12 and the jump's landing, compared row for row (check_fall, check_crevice_jump); left: 001760C0 | bind | 1,755 | 14 (live 13, verified-unbound 1) | 05, 06, 08, 10 (step-offs, the cage-roof fall) | L01; L05 (0019CB60, 001A6440); L07 (column scan 0019BC40 and actor collision); L26 (001EFD90 on the slide) |
| 5 | **L04-box-climb**: Bind ledge climb / vault (0015DF10, 00161790) for the boxes. **Live 2026-09-24 (Boxes step):** the level smoke's `boxes` phase equals route 05 row for row | bind | 1,960 | 12 (verified-unbound 12) | 05 (climbing the boxes) | L01, L02, L05, L06 |
| 6 | **L03-hill-slide**: Bind the slide state (0016C6A0 family) for the hill. **Live 2026-09-24 (L03 step):** the level smoke's slide phase equals route 06 row for row (section 1.7); the slide's 001EFD90 spawns still go to the counted effect gap (L26) and its sounds 0x12E and the skid/landing ids are not in the exported registry (WP-14) | bind | 1,560 | 8 (live 8) | 06 (sliding down the hill) | L02, L05 (0016C570 / 001791D0 probes), L26 (001EFD90) |
| 7 | **L06-coll-probe-walkers**: Bind the translated probe walkers (em_coll_probe_original). **Live 2026-09-24 (Boxes step: FLOOR engaged)** (em_collision_world_bind_player); 0019F1A0 / 0019ED80 are live under the camera's grid walkers | bind | 1,992 | 9 (live 2, verified-unbound 7) | every frame (probes); 05 | L02 (engages FLOOR) |
| 8 | **L06b-coll-segment-walkers**: Bind the translated segment walkers (em_coll_segment_walkers). **Bound 2026-09-24 (partial):** 0019A910 and its walkers under the scripted retarget and the item ray; 0019A570 waits on its callers (climb, ledge catch, drum, shadow); **census L13..L16 (section 1.11):** the live camera's every segment query runs 0019A910 | bind | 2,137 | 7 (live 4, verified-unbound 3) | every frame (camera and segment queries) | L04/L25/L29 (0019A570's callers), L13 |
| 9 | **L19-script-host**: Bind the area-script op handlers. **Recount 2026-09-24:** every row is a verified translation now (the seven former unverified handlers pass test_script_door_fan_reference part 1; 001BA510 / 001BAD40 are em_script_door_fan). **Live 2026-09-24 (section 1.8):** em_area_script is in COMMON, bound by em_area11_script_host for the truck trigger's 0x8292C0 (route 07 row for row): 001BA1A0, 001B8FC0 (op00 kinds 0 / 1 / 2) and 001B94F0 (op01 kind 1), and 001B6250 through the pad actuator; **Roger 2026-09-24 (section 1.10):** Roger's 0x8283D0 runs op06, op07, op0A, op0B, op0C, op0D, op10, op16 and op18 live (route 14 row for row); the rest are reached only by the director's scripts (L21) | bind | 1,968 | 21 (live 4, verified-unbound 17) | 07 (truck camera preview), 10, 11, 13, 14 | nothing (the remaining handlers wait on the director, L21) |
| 10 | **L20-message-service**: WP-8: extend the live panel message service (001FCA10) to every caller, with voice pushes and the stream table. **Recount 2026-09-24:** 001FE4B0 / 001FE4D0 are live (em_message_bank_records / _record); left: the voice lanes 001FA5A0 (L36), 001FCF10 (translated, em_render_verify_rest) and the untranslated mode-4 presenter 001FCB90 | bind | 997 | 18 (live 15, verified-unbound 2, stand-in 1) | 10, 11 (director lines), 14 (Roger) | L19 |
| 11 | **L23-truck**: WP-12: bind the truck and its camera trigger. **Live 2026-09-24 (section 1.8):** 00823FF0 and 008251E0 on their nodes (em_area11_boxes), routes 07 and 08 row for row; 001EBF10 (the truck effects' kind) waits on the effect owner (L26) | bind | 1,461 | 3 (live 2, verified-unbound 1) | 07, 08 (truck preview and crossing) | L26 (for 001EBF10) |
| 12 | **L17-pickups-use-arbiter**: WP-6: bind the pickup owners and publish them to the Use arbiter; retire the legacy take. **Recount 2026-09-24:** done except the leaves: the owners 0015AFA0 / 0015AE20 / 00219550, the take 001B6EA0, the inventory 001C40B0 and the facing 001B7F90 are live (81414be); left: 0015AC00 (em_pickup.c INIT scale switch, no oracle), 001B1190 (em_pickup.c taken_set, hooked by its test), 001C5680 / 001C5760 (the indicator child ticks) are live per node since the render + UI step (section 1.13) | verify | 1,069 | 10 (live 8, unverified 2) | 01 (battery pickup) and every other pickup | host (done) |
| 13 | **L09-ladder-use-chain**: Bind the Use chain and ladder entry (0015D4C0, 00160220 whole, 00165B60). **Live 2026-09-24 (Boxes step):** 00160220 / 001798D0 / 0017C440 over the live record, `player_states_bind_use_chain(1)`; the stand-in player_pose_use_accepted is retired; **Live 2026-09-24 (census L09, section 1.9):** the EMCL carries the grid nodes' +0x34..+0x3F axis (flags bit 3, verified against captured RAM), so 0015D4C0 case 0x32, 00177030 mode 4, 00199DB0, 00165B60 and 00182A70 run on the cage column; route 10's two climbs row for row (check_cage_ladders) | bind | 2,085 | 11 (live 11) | 05, 10, 13 (ladders and the Use chain) | L01, L02, L06 |
| 14 | **L10-ladder-climb**: Bind the ladder climb states (001662D0 family). **Live 2026-09-24 (census L10, section 1.9):** 001662D0 with 0017FC80, 0017FD00, 00180300, 00180420, 00180460, 00181110, 00174AB0 and 001885D0 climb both cage ladders and dismount, route 10 row for row (check_cage_ladders); its 001FB9F0 sounds are em_sfx_play (silent ids, WP-14) and its 00187EE0 is em_player_floor's translation | bind | 1,856 | 8 (live 8) | 10, 13 | L09 |
| 15 | **L21-director-beats**: WP-10: manager 008253F0 and its beat scripts through the script host. **Blocked 2026-09-24 (section 1.8):** beat 0's 06/2 waits for D_00810813 = 1, which only Roger's alternate script writes; the director stays on em_director.c until Roger is bound (DIRECTOR_ORIGINAL.md section 6). **Unblocked 2026-09-24 (section 1.10):** Roger is bound (L22); his 0x828990 runs on em_area11_script_host once D_00810793 is set. **Prepared, blocked on WP-8b 2026-09-25 (section 1.14):** the binding exists (em_area11_bindings tick_director_original, the script host's 0018CBD0 / 0018D7B0 workers for op0D sub 2) and reproduces route 10 f1090..f1162 row for row in the director verification run; at f1163 Roger's 0x828990 presents the voiced line 0x7F, whose 001FA5A0 voice push and 001F9CF0 voice lane are not live, so node #21 keeps em_director.c | bind | 410 | 9 (verified-unbound 7, live 2) | 10, 11 | L19 (live), L22 (live), WP-8b (the voice lanes: Roger's voiced 0x7F chain, the lines 0x97 / 0x99) |
| 16 | **L22-roger-encounter**: WP-9: bind the Roger owner, equipment child and cutscene timeline. **Live 2026-09-24 (section 1.10):** em_area11_roger binds 008237E0 / em_roger_tick and 001C5C90 over their record bytes, their scripts run on em_area11_script_host, 0022EEF0 / 0022EC30 drive the bank 0x96 timeline, the player's takeover runs 00183090 / 00182DF0 on the record; the level smoke's `roger` phase equals route 14 f288..f1818 row for row. Left: 001BA540, 001AF890, 001CA770 on Roger (bound, not reached on the route); 001DA6A0 / 001CAA00 are reported / the port's draw; beat 10's alternate script 0x828990 waits on L21 | bind | 2,121 | 24 (live 21, verified-unbound 3) | 10, 14 (Roger) | L19, L20 |
| 17 | **L11-running-jump-recovery**: Bind the running jump and recovery (0015EC50, 001634A0, 0017C860). **Live 2026-09-24 (census L11, section 1.9):** the crevice jump of route 12 row for row (check_crevice_jump: states, clips, clocks and the arc's Y exact; the step length within 1e-4 on the free-flight rows); the landing's 0017DEB0 is em_player_climb's translation over the record; beat 14's tower jump waits on the roger phase | bind | 2,297 | 6 (live 6) | 12 (crevice jump), 14 | L02, L09 |
| 18 | **L13-camera-follow**: Bind em_camera_follow_original (follow core) in place of em_camera.c camera_update. **Recount 2026-09-24:** em_camera_leftovers translates 0018B9C0 (the camera frame), 0018C0C0, 0018C5A0, 00190F20, 001914A0, 00191580; every row except 0019B7D0 (live) is verified-unbound. **Live 2026-09-25 (census L13..L16, section 1.11):** em_camera_live binds the follow core, the frame and every solver on the canonical camera words; the legacy camera_update runs only for scenes without an original roster | bind | 1,587 | 15 (live 15) | every frame from first control | L06b (camera queries) |
| 19 | **L14-camera-solver-dd20**: Bind 0018DD20 (desired-eye solver) and retire the unverified duplicate. **Recount 2026-09-24:** both are translated (em_camera_leftovers_solver em_camleft_0018DD20 / _0018CE60); em_camera.c cam_solver_0018DD20 and em_game.c cam_bounds_settle_0018CE60 are the live duplicates to retire. **Live 2026-09-25 (section 1.11):** both translations run in AREA11; the two duplicates are reached only by the legacy camera of scenes without an original roster (0 calls in the full smoke) | bind | 2,051 | 2 (live 2) | every frame from first control | L13 |
| 20 | **L15-camera-actions**: Bind the camera action dispatch 0018BC20 and em_camera_area11_specials (001921D0, 00193EB0). **Recount 2026-09-24:** 0018BC20 and 00191000 are em_camera_leftovers translations. **Live 2026-09-25 (section 1.11)**; the director, examine, door-cinematic and aim owners still pre-empt action 0 through named stand-ins (CAMERA_LIVE.md section 6; L21 / L18 / L28) | bind | 1,951 | 5 (live 5) | every frame from first control | L13 |
| 21 | **L16-camera-area11-walk**: Bind 00195130 (AREA11 walking specials), 001916C0 and the 0015CBA0 state map. **Recount 2026-09-24:** 0015CBA0 and 001916C0 are translated (em_camera_leftovers). **Live 2026-09-25 (section 1.11)** | bind | 1,843 | 3 (live 3) | every frame from first control (AREA11 walking specials) | L15 |
| 22 | **L12-locomotion-display**: Replace the legacy idle/walk callbacks and gait display with 00161020/001612D0/0017B660 and their verified workers. **Live 2026-09-25 (section 1.12):** 00161020 / 001612D0 are 0015B130's state[0] / state[1] over the record in AREA11 (behind the port's stand-ins, L18 / L21 / L28), with 0017C030, 0017B660, 0017B5C0, 00179D20, 00179FF0, 0017B490 / 0017B460, 00178B90, 00187350 / 00187EE0 and the new record-level 0017BC40 / 0017B910; 00182D40 stays verified-unbound (its caller 00182DF0 is not on the record); 00187DC0 unchanged | bind | 1,742 | 15 (live 13, verified-unbound 2) | every frame from first control (idle/walk look and footsteps) | L01 |
| 23 | **L24-fan-husk**: WP-11: bind the fan pair and the husk pair. **Recount 2026-09-24:** the husk creature 00825940 (lifecycles 1 and 4 fault), its partner 00827490 and the manager 00823CE0 are translated in em_script_door_fan_husk | bind | 1,924 | 4 (verified-unbound 4) | every frame (husk pair), level exit (fan) | L07 |
| 24 | **L25-crates-drums**: WP-18: bind crates and drums in place of em_enemy. **Live 2026-09-24 (Boxes step):** em_area11_boxes.c runs both owners over their roster nodes (CRATES_DRUMS_ORIGINAL.md "Binding"); the legacy AREA11 copies are retired; the damage paths are fail-stop (no live +0x36 writer, effects L26) | bind | 1,885 | 2 (verified-unbound 2) | every frame; 05 | L07 |
| 25 | **L18-door-original**: WP-7: bind the original door runtime/program/transit. **Recount 2026-09-24:** 001B1B30, 001BC240, 001BC290, 001BBD60 and 001B0080 are em_script_door_fan translations; every row is verified-unbound | bind | 881 | 11 (verified-unbound 11) | 09 (fence door, side beat) | L17 (Use arbitration) |
| 26 | **L28-player-equipment**: Bind the player equipment/weapon actors (0018A6B0 nodes) and the gun tick. **Recount 2026-09-24:** em_player_equipment translates all 13 rows (3824c6f); the em_weapon.c gun tick, lamp gate and camera-code stand-ins go when bound | bind | 2,101 | 13 (verified-unbound 13) | every frame (rifle/knife children, gun tick, aim) | L01 |
| 27 | **L26-effect-manager**: Bind the effect manager barrel (em_effect_manager) and em_effect_original. **Recount 2026-09-24:** the barrel 001F0360 and its lanes are translated (3824c6f); 001F1110 / 001F1180 moved to live (the pickup aura, 81414be) and left this lane's count; 001F1180's draw block (001F0A60 here) is the no-op stand-in aura_draw. **Effects step 2026-09-24 (blocked):** every effect draw reads the render-context views (the +0x2240 / +0x22C0 clip matrices through 001CD370, the 0x70003AC0 / 0x70003A40 matrices, the +0xA0 fog), and no live code produces them; binding the spawns and the 001EA240 walk without them would fault on the first footstep (EFFECT_MANAGER.md 5.0) | bind | 2,342 | 15 (live 2, verified-unbound 13) | every frame (effects) | L32 + L30: one canonical render-context block with 001D2960's P / K / clip matrices over the live view and the 001D1C50 copies (found by the Effects step; the census had "nothing") |
| 28 | **L27-effect-kinds**: Bind the effect kinds/tables and the effect colour (em_effect_kinds). **Recount 2026-09-24:** every row is translated (3824c6f); the point-light list rows replace the offline tools/export_point_lights.py resolution. **Effects step 2026-09-24:** the handlers' 001CFB50 and its 001D0540 are translated in em_effect_kinds (boundary rows moved to section 3.15). **Render + UI step 2026-09-25 (section 1.13):** 001F54E0 is live (the indicator children's colour); em_effect_color.h's own copy is deleted | bind | 1,251 | 18 (live 1, verified-unbound 17) | every frame (effects); 00, 05, 06 (footstep and slide effects) | L26 (and through it L32 / L30) |
| 29 | **L08-coll-missing-and-list-passes**: Translate the untranslated collision originals and the actor list passes. **Translated 2026-09-23; bound 2026-09-24:** the nine hooks, 0019B7D0 / 0019E280 live; 001A8660 waits on a class-0xD owner (the flame), 0019E930 / 001A3980 on L09, 0019F330 on the column's pass 2 | bind | 2,081 | 14 (live 10, verified-unbound 4) | every frame; 05, 07..14 | L09, the flame owner, original-layout records for class 1/2/0xD |
| 30 | **L29-shadow-route**: Bind the shadow actor route (0015BF90) and its packet producers | bind+verify | 974 | 7 (verified-unbound 6, unverified 1) | every frame from 02 (player shadow) | renderer |
| 31 | **L29b-shadow-gs**: Bind em_shadow_original / em_shadow_gs (receiver passes, clip kernels). **Recount 2026-09-24:** the five former unverified rows pass test_render_verify_rest_reference (part D; 001D5C80 with the em_ee_float.h fix) | bind | 1,779 | 11 (verified-unbound 11) | every frame (shadow receivers) | L29-shadow-route |
| 32 | **L32-frame-render-heads**: Replace the port collectors at 001D1C50/001D1EA0/001D30A0 and the projection with translations. **Recount 2026-09-24:** em_frame_render_heads translates all 18 rows; the port collectors and em_math.h projection are the stand-ins to retire | bind | 1,545 | 18 (verified-unbound 18) | every frame (frame setup, projection) | renderer |
| 33 | **L30-render-context**: Bind the render-context / HUD bar / area-specials path (em_render_context). **Recount 2026-09-24:** all 16 rows translated, plus the seven render-range helpers that were boundary rows (001D21B0, 001D2710, 001D2910, 001D2DE0, 001D2E00, 001D6B10, 001D6C90) | bind | 1,754 | 23 (verified-unbound 23) | every frame (render context, HUD bar) | renderer |
| 34 | **L31-background-weather-load**: Bind em_background_gs and the area-load render passes (001C1DC0 family). **Recount 2026-09-24:** the 001C1DC0 family, 001C1F50, 001E0CF0, 001E2260 / 001E2270 and 001C22A0 / 001C2360 are em_render_verify_rest translations; 0021B9A0 (the fog programmer) is translated since afa091b (em_packet_chain_original, verified-unbound); **Effects step 2026-09-24:** 0021B920 is live as the one translation behind the Metal fog (em_fog_gs_coefficients is a call of em_packet_chain_0021B920). **Render + UI step 2026-09-25 (section 1.13):** 001E1E60 and kernel 0x0023C990 (em_background_gs) draw live first in every world frame, gated as 001D2300 gates its CALL | translate+bind | 932 | 17 (live 2, verified-unbound 15) | S1 (area load), 09 (room move), every frame (background) | renderer |
| 35 | **L33-anim-runtime-rest**: Bind the remaining animation-runtime originals. **Bound 2026-09-24 (display step, partial):** em_pose_host_workers is the player's one pose owner (em_player_record_pose over the live record): 11 rows live. **2026-09-25 (section 1.12):** 001C9D50 / 001C9E40 live through 0017B660. Still unbound, each on another lane: 001C7900 (001D88B0, L32), 001CB2C0 (001CB3C0, L35), 001CAAC0 (001CB760 L39, 001F6210 L26), 001CACB0 (001CABA0, renderer boundary), 001CB5B0 (one canonical D_00275B40 / D_00275B48 for every host); 001C7C00 is live for S2 (critic 7.2) and stays in the lane for beat 14 | bind | 1,819 | 22 (live 14, verified-unbound 8) | every frame (animation) | nothing |
| 36 | **L36-stream-lanes-sound**: Bind the stream lanes (music/voice) and the gain/positional sound originals | bind | 1,530 | 17 (live 1, verified-unbound 16) | every frame (music and voice streams) | IOP/disc boundary decisions |
| 37 | **L38-load-veil-particles**: Bind the load-veil particles (0021B1B0/0021B500) so the load is not black | bind | 1,074 | 16 (verified-unbound 16) | S1, 09 (loads) | nothing |
| 38 | **L39-head-sprite-effects**: Bind the head-bone sprite effect (001E2560 node) and its registry helpers. **Recount 2026-09-24:** the packet-chain builders 001CB5F0, 001CB6B0, 001CB760 and 001CB900 are translated since afa091b (em_packet_chain_original, docs/PACKET_CHAIN.md). **Effects step 2026-09-24:** the node's 001CCF70 and its 001CFBE0 packet read the render-context views, which no live code produces (EFFECT_MANAGER.md 5.0) | bind | 868 | 12 (verified-unbound 12) | every frame (head-bone sprite) | L32 + L30 (views), renderer |
| 39 | **L35-status-ui-leftovers**: Area-title card, UI cues, the BATTERY page draw, the owner draw and the remaining owner-service leaves. **Recount 2026-09-24:** em_status_ui_leftovers translates the area title, UI cues, context saves, 0022EBE0 and the BATTERY page draw 0020AE40 / 0020B0D0 / 0020B210 (moved here from live: em_battery_ui.c is a hand-placed stand-in, critic 7.2); em_owner_draw_original adds 001CA7B0 / 001CA940 and the former boundary rows 001D38A0 / 001D3BA0; left untranslated: 0020CCB0 and 0021BAE0 (stand-ins), 0020DFA0 (unverified) | translate+bind | 1,937 | 33 (live 2, verified-unbound 28, unverified 1, stand-in 2) | S2 (area title), 01, 03 (UI cues), owner services | L20 (message lookup) |
| 40 | **L34-startup-and-load-gaps**: Close the title/New Game/load gaps. **Recount 2026-09-24:** em_startup_load_gaps translates every row (3824c6f); 001AB6A0 / 001AB740 are live (em_task.c, verified by test_startup_load_gaps_reference); 001AB790 is verified but the live New Game registers the task instead | bind | 1,626 | 25 (live 2, verified-unbound 23) | S0..S2 (title, New Game, load, opening) | nothing |
| 41 | **L37-sdk-math-leaves**: Bind the remaining SDK math / soft-float translations at their call sites. The soft-float workers (0011DB90, 0011FD78, 00127758 and their callees) are bound into the collision world's SDK context since 2026-09-24 (SDK_SOFT_FLOAT.md 4). **Recount 2026-09-24:** 001000E0 / 001027E0 / 00102850 are em_render_verify_rest translations; the live copies that no oracle checks moved here from live: 00128350 (em_item_root.c host compare), 0011E620 / 00102798 / 00102CD0 / 001B1240 (hooked by test_camera_commit_reference), 00102900 / 00102948 / 00102958 / 001026D0 (tests model or hook the leaf), 001B1470 (host wraps); 00102CD0 has no translation. **Census L13..L16 (2026-09-25, section 1.11):** 00102CD0 (em_cs_00102CD0 with 001027E0), 00102798 and 001B1240 are live in the translated commit 0018C0D0 (test_camera_live_reference executes the original routine with its leaves); test_camera_commit_reference is retired | bind | 1,234 | 30 (live 5, verified-unbound 25) | wherever the bound callers run | nothing |
| 42 | **L40-actor-light**: Bind em_actor_light_001D89D0 (the bit-exact 001D89D0 chain) in place of em_render_frame.c char_rig_build's recomposition. New in the recount: 001D8270 (fold gate) and 001D8690 (actor RGB) are not called live | bind | 185 | 2 (verified-unbound 2) | every frame (actor lighting) | renderer (em_gfx rig contract) |

Functions per lane:

- **L01-player-stage-live**: 0015B130, 0015BA50, 0015BCF0, 0015B530, 0015D000, 0015D100, 0021C440, 0021BB00, 0021C3F0, 0021D640,
  00182B30, 00182D70, 001837A0, 0015CF90, 001B0070.
- **L05-coll-move-walkers**: 0019AD00, 0019AFE0, 0019FE50, 0019CB60, 001A4D10, 001A6440, 001A4030, 0019A310.
- **L07-actor-collision-live**: 0019AB20, 0019BC40, 0019C830, 0019F730, 001A2370, 001A5760, 001AAD00, 001B1CA0, 001B1D20, 001B1D60,
  001B1DA0, 001B1DE0, 001B1B70.
- **L02-floor-fall-live**: 001760C0, 00182870, 00175900, 00175CF0, 00179450, 00179680, 001796C0, 00175640, 00162DB0, 00163B40,
  00163C10, 00179880, 0017C580, 00224290.
- **L04-box-climb**: 0015DEC0, 0015DF10, 00161690, 00161790, 00177460, 00177510, 001775E0, 00177F40, 0017D800, 0017D8D0,
  0017DEB0, 0019A180.
- **L03-hill-slide**: 0016C520, 0016C570, 0016C6A0, 0016CD70, 00174FD0, 001791D0, 0017F5F0, 00224B80.
- **L06-coll-probe-walkers**: 0019B6C0, 0019B8C0, 0019DF10, 0019E640, 0019ED80, 0019F1A0, 001A2AE0, 001A32C0, 001A4650.
- **L06b-coll-segment-walkers**: 0019A570, 0019A910, 0019D330, 0019D770, 001A0B10, 001A1390, 001A50A0.
- **L19-script-host**: 001B6BF0, 001B6E40, 001B6F80, 001B6FA0, 001B7840, 001B81D0, 001BA080, 001BAC00, 001BAD40, 001BA510,
  001B8FC0, 001B7B30, 001B8020, 00182BF0, 001B0460, 001B0B50, 001B12B0, 001B1380, 001B15D0, 001B6250,
  001BA1A0.
- **L20-message-service**: 001FD4C0, 001FD580, 001FD6A0, 001FC9B0, 001FA5A0, 001FAB80, 001FCB90, 001FC770, 001FC7B0, 001FE070,
  001FE460, 001FE480, 001FE4B0, 001FE4D0, 001FE530, 001CBE10, 001FCF10, 001CC170.
- **L23-truck**: 00823FF0, 008251E0, 001EBF10.
- **L17-pickups-use-arbiter**: 0015AFA0, 0015AE20, 0015AC00, 00219550, 001B6EA0, 001C40B0, 001B7F90, 001B1190, 001C5680, 001C5760.
- **L09-ladder-use-chain**: 0015D4C0, 00160220, 00165B60, 00176F90, 00177030, 00199DB0, 0019BA80, 001B61C0, 00182A70, 001885D0,
  001798D0.
- **L10-ladder-climb**: 001662D0, 0017FC80, 0017FD00, 00180300, 00180420, 00180460, 00181110, 00174AB0.
- **L21-director-beats**: 008253F0, 00825500, 00825540, 00825600, 00825640, 008256D0, 00825710, 001B1EA0 (live for Roger's trigger since section 1.10), 0011E080 (001C4760 is live since HK: the world's d810CC3/d8106B0/d8106B1 are bound through em_director_original_001C4760_scene).
- **L22-roger-encounter**: 008237E0, 00823910, 00823950, 00823B70, 001C5C90, 001BA540, 001BA580, 001BA8E0, 0022EEF0, 0022EC30,
  001CA5E0, 001CA5F0, 001CA6E0, 001CA6F0, 001CA700, 001CA770, 001D0690, 001D06D0, 001D0C70, 001B10B0,
  001B1020, 001AF780, 001AF890, 001D8BF0 (live since section 1.10 except 001BA540, 001AF890 and 001CA770: bound, not reached on the route).
- **L11-running-jump-recovery**: 0015EC50, 001634A0, 001751A0, 00178EC0, 0017C860, 002243F0.
- **L13-camera-follow**: 0018B9C0, 0018C4B0, 0018C6A0, 0018D330, 0018D7B0, 0018D910, 00191390, 00191D40, 00192010, 0018C0C0,
  0018C5A0, 00190F20, 001914A0, 00191580, 0019B7D0.
- **L14-camera-solver-dd20**: 0018DD20, 0018CE60.
- **L15-camera-actions**: 0018BC20, 001921D0, 00191000, 00191210, 00193EB0.
- **L16-camera-area11-walk**: 00195130, 001916C0, 0015CBA0.
- **L12-locomotion-display**: 00161020, 001612D0, 0017B660, 0017B460, 0017B5C0, 0017B490, 0017C030, 00178B90, 00179D20, 00179FF0,
  001749F0, 00187350, 00187EE0, 00187DC0, 00182D40.
- **L24-fan-husk**: 00827630, 00825940, 00827490, 00823CE0.
- **L25-crates-drums**: 001551B0, 00156620.
- **L18-door-original**: 001BC350, 001BBE40, 001BBDA0, 001BC0E0, 001BC300, 001BC240, 001BC290, 001BBD60, 001B1B30, 001B0080,
  001B94F0.
- **L28-player-equipment**: 00188630, 00188A50, 00188AC0, 00188B80, 00188DF0, 00188ED0, 00189D30, 0018A1F0, 0018A6B0, 0018A8D0,
  0015D2F0, 001CD520, 001607D0.
- **L26-effect-manager**: 001F0360, 001F6210, 001F0720, 001F0A60, 001F1110, 001F1180, 001F6BB0, 001F6EB0, 001F40C0, 001F4D40,
  001EA240, 001EF940, 001EF9D0, 001EFD20, 001EFD90.
- **L27-effect-kinds**: 001F5640, 001F5940, 001F5C20, 001F5CA0, 001F6640, 001F66F0, 001F6760, 001F6850, 001F68B0, 001F6D60,
  001F6E40, 001F0310, 001F03D0, 001F3FA0, 001F54E0, 001EC1F0, 001EC3F0, 001EC470.
- **L08-coll-missing-and-list-passes**: 0019E280, 0019E930, 0019F330, 001A3980, 001A7870, 001A8660, 001A8BE0, 001A8DA0, 001A9000, 001A97B0,
  001A9B10, 001A9D20, 001A9F60, 001AA140.
- **L29-shadow-route**: 0015BF90, 0015C160, 001CE300, 001CF470, 001F8D30, 001F9100, 001CD390.
- **L29b-shadow-gs**: 001D4B50, 001D98A0, 001D9EE0, 001DA080, 001DA1E0, 001DA290, 001DA310, 001DA6A0, 001D5C80, 001D4CD0,
  001D4FB0.
- **L32-frame-render-heads**: 001D1C50, 001D1EA0, 001D30A0, 001D2960, 001D2D20, 001D25F0, 001D2590, 001D2610, 001C1D00, 001D1EF0,
  001D19E0, 001D2830, 001D9070, 001D19D0, 001D8060, 001D80B0, 001D88B0, 001D8C30.
- **L30-render-context**: 001DD7B0, 001DD940, 001DDA00, 001DDAA0, 001DDE10, 001DEEE0, 001E0C30, 001E0C60, 001E0C80, 001E0D70,
  001E0DF0, 001E1010, 001D5370, 001D52E0, 001E0CC0, 001DD950, 001D21B0, 001D2710, 001D2910, 001D2DE0,
  001D2E00, 001D6B10, 001D6C90.
- **L31-background-weather-load**: 001C1F50, 001E0CF0, 001E2260, 001E2270, 001E1E60, 001D2300, 001C1DC0, 001C1E70, 001C1E80, 001C1E90,
  001C22A0, 001C2360, 001D8FD0, 0021B920, 0021B970, 0021BA80, 0021B9A0.
- **L33-anim-runtime-rest**: 001C7900, 001C9D50, 001C9E40, 001CAAC0, 001CACB0, 001CB2C0, 001C8480, 001C8710, 001C8F10, 001C90D0,
  001C92C0, 001C94B0, 001C9940, 001C68C0, 001C6960, 001C63E0, 001C61D0, 001C6120, 001C6150, 001C7420,
  001C7C00, 001CB5B0.
- **L36-stream-lanes-sound**: 001FABB0, 001FBC50, 001F9CF0, 001FA0D0, 001FA330, 001FA570, 001FA5F0, 001FA790, 001FAAC0, 001FAB50,
  001FABF0, 001FAE70, 001FC280, 001FD470, 001FBF50, 001FC3C0, 001FBDB0.
- **L38-load-veil-particles**: 0021B1B0, 0021B500, 00100268, 00100610, 001006D8, 001DFA40, 001D1F20, 001D1F80, 001D1FF0, 001D2040,
  001D63B0, 001D6930, 001D6B60, 001D6BA0, 001D6E60, 001D7080.
- **L39-head-sprite-effects**: 001E2560, 001E2290, 001E23A0, 001F0120, 001CFA60, 001CFBE0, 001CB5F0, 001CB6B0, 001CB760, 001CB900,
  001CCF70, 001CD370.
- **L35-status-ui-leftovers**: 001C5930, 001C5860, 0020CD40, 0020CD60, 0020CDA0, 0020BEF0, 0021BAC0, 0021B8E0, 0021B900, 0021BA70,
  0021BAB0, 001C4820, 0022EBE0, 001B0DC0, 001B0EA0, 001B0FD0, 001B1630, 001B17A0, 001B1E20, 001B5B70,
  001CA7B0, 001CA940, 001CA990, 001CAA00, 001CB3C0, 001D38A0, 001D3BA0, 0020AE40, 0020B0D0, 0020B210,
  0020CCB0, 0020DFA0, 0021BAE0.
- **L34-startup-and-load-gaps**: 001AB4E0, 001AB590, 001AB6A0, 001AB740, 001AB790, 001AC070, 001AF470, 001AF5C0, 001AF690, 001AF710,
  001AFCA0, 001B0F60, 001B57E0, 001B5F40, 001FB100, 001FC6E0, 001FB3E0, 001FB910, 001FB370, 008237C0,
  00199C50, 001BB0E0, 0015C1F0, 001AB7D0, 001FEF70.
- **L37-sdk-math-leaves**: 0011C4C8, 0011D878, 0011E398, 0011DBB8, 0011DE90, 0011DB90, 0011FD78, 00126AB8, 00126BE8, 00127398,
  001274B0, 00127728, 00127758, 001277B0, 00128320, 00102718, 00102738, 001027E0, 00102850, 001000E0,
  001026D0, 00102798, 00102900, 00102948, 00102958, 00102CD0, 0011E620, 00128350, 001B1240, 001B1470.
- **L40-actor-light**: 001D8270, 001D8690.


## 6. Limits

- **Census coverage.** Boot before the title (crt0, IOP bring-up, logos, card checks) is not instrumented; only listed entries are armed (jumps into a function body and the unlisted 0x1050E4..0x105148 gap are unseen); one hit per function per label (no counts, no order); only the played route is exercised (no damage/death, pause/options/save, Circle/Square/R1/weapon/camera inputs, truck-pit fall, the west-yard and plateau ladders); nothing after Roger's encounter, so **the level exit (fan 00827630's area change, Roger's departure script) is not in the census**. Several beat replays follow slightly different closed-loop paths from the recordings (census method.md).
- **Classification.** The evidence search is textual and was then read by hand; it can miss a translation that cites no address, or credit a test that names an address it only hooks. The 2026-09-24 recount (section 1.4) measured liveness by instrumenting the live build over the level smoke and three headless drivers, which reach only S0..04 plus the area-change and room-move paths; rows first seen at 05 or later keep a static-reachability judgement with the known runtime gates cut, so code behind other `getenv`/state gates may still be counted as live there. Its hook scan read the named tests' hook tables and leaf models; a test that verifies through an indirect harness the scan cannot parse (for example the spawn-order checks) keeps its earlier reading. The lighting rows 001D7B30, 001D8130, 001D8340, 001D89D0 and 001D8C20 stay live through em_render_frame.c char_rig_build and em_lighting_matrices, which test_actor_lighting_reference checks only under its port contract; the bit-exact em_actor_light_001D89D0 is unbound (lane L40).
- **Oracle strength varies.** `test_fade_reference.py` compares with locally compiled decomp C (the fade functions are byte-matched), not executed instructions. The spawn helpers (0015C310/0015C420, 0018A880, 001C1EA0, 001C5570) are checked only for spawn set and order (`compare_frame_order.py`, census 49), not node bytes. The heading helper's SDK trig and the live wall probes' sqrt/atan are host models.
- **Snapshot.** Statuses reflect the port HEAD 9e0715f (2026-09-24, section 1.4); the census run itself is still the 2026-09-23 one (beats 00..14). `classified.json` was regenerated from these rows in the recount (its `recount` block lists every change; the 2026-09-23 file is kept beside it as `classified.2026-09-23.json`). Landing steps must re-classify their rows here and recompute section 2 and the lane mixes in the same change; the recount found five commits that had not (3824c6f, 81414be, 4b6ac3e, 9d2a129, 0f4cc8f). The level exit (beat 15, FIRST_LEVEL_EXIT.md) is still outside these tables.


## 7. Critic notes (completeness review, 2026-09-23)

**Applied in the 2026-09-24 recount (section 1.4):** every row of 7.2 (0021BAE0 stand-in, 00128350 / 001798D0 / 0020B210 / 0020B0D0 / 0020AE40 out of live, 001281C0 and 001B1470 re-read, 0018D910 whole routine now em_camera_leftovers_solver, 001C7C00 live for S2, the point-light list rows and 008237C0 now verified translations with their offline or roster stand-ins named). 001C6DA0 is live since the display step (em_pose_host_001C6DA0 on the record). The 7.1 coverage items (the level exit, the S3-to-slot-04 gap, 0x1AAE40, the 0x1050E4 gap) are unchanged: the main loop 0x1AAE40 and the gap routine 001050E8 are translated (em_main_loop_and_gap, test_main_loop_and_gap_reference PASS) but still carry no census row.

A read-only review of the census and this classification. Every point below was checked against the census JSON, the pinned ELF (static scans, addresses only), the port tree and the named tests. Nothing here changes the tables above; it lists the corrections the next classification pass should apply.

### 7.1 Coverage

- **Route beats.** The census replays every beat of FIRST_LEVEL_ROUTE.md section 3: the terminal without a battery (00), the battery pickup and ITEM pop-up (01), the no-power refusal with bars (02), the terminal with the battery and the BATTERY page (03), the elevator ride (04), the crates (05), the hill slide (06), the truck preview with bars (07), the crossing (08), the fence door (09, side), the cage roof (10), the crevice prompt and jump (11, 12), the east tower (13) and Roger's encounter (14). Every beat reports `completed=True`.
- **Not covered: the level exit.** The route stops when Roger's encounter releases control. The project goal ends at "all of AREA11 → its exit", so the census is not yet the whole first level. A beat 15 (Roger's departure and the fan 00827630 area change) is needed before the count can be called complete.
- **Not chained: first control to slot 04.** S3 runs 200 idle frames from the census's own opening (counter 3063..3263). The beats start from the user's slot 04 (counter 4083) and not from S3. The position is the same, but slot 03 (counter 3963) is labelled "movement still locked". Nothing in the census covers the frames between S3's end and slot 04, or the first input after the lock lifts. This interval is probably idle, but no census run shows it.
- **Missing executed function: the main loop 0x1AAE40.** It holds the frame boundary 0x1AAF28 and runs every frame. Its entry ran before slot 01 and never runs again, so a one-shot entry breakpoint cannot see it. It is absent from `route_functions.json` and from every table here. Add it by hand; the port's counterpart is `em_frame` (steps A..W). The same blind spot would apply to any long-lived thread body. The only other game thread is created by 001F91C0 with entry 001FB0C0 (64 bytes). 001F91C0 did not run in the census, and 001FB0C0's only callee 00119870 never ran, so that thread is idle on this route.
- **Unlisted code: the 0x1050E4..0x105148 gap.** The gap is not just padding. It holds two return instructions (at 0x1050F8 and 0x105118), so it contains at least two or three small routines that `FUNCTIONS.csv` does not list. Two functions that ran in S0 take the address 0x105130: 00104D78 and 00104E48. Add entries for the gap and re-arm them in the next census pass.
- **Callbacks.** The breakpoints catch pointer-only functions, provided the function is a listed entry. A static scan found 87 boot entries that are referenced only by address and never by a direct call; 26 of them ran and were recorded. It also found:
  - 18 data references to addresses inside a listed function that start right after a return. All 18 are jump-table case labels (runs of neighbouring words pointing into the same function), not hidden functions.
  - In the overlay, all 17 address constants are listed entries, except seven, which are the truck 00823FF0's own jump table.
  - No direct call targets an address that is not a listed entry.
  So no callback-only function is hidden inside another row.
- **Per-case coverage is not measured.** A row counts when its entry runs once. Big dispatchers with jump tables ran: 0015B130, 0015CBA0, 001BC350, 001CA5F0, 001CFBE0, 0020EE50 and 0021B9A0 (static scan). The census cannot say which cases the route used. So "translated and verified" on a sample does not prove that the route's cases are covered. The next pass should record case or basic-block hits for those dispatchers, and for the script host's opcode switch 001B7840/001B81D0.
- **Single pass for most beats.** Pass B re-ran only the startup, 01 and 06. In S2/S3, pass B added 22/24 phase-dependent SDK functions. So a single pass over a short label can miss periodic work. Beats 02, 05, 07, 09, 10, 11 and 13 also took different closed-loop paths from their recordings. The union over the whole route probably absorbs this, but only a second pass over 02–14 would show it.

### 7.2 Classification corrections (sampled: 12 live, 13 verified-unbound, 15 missing, plus a scan of the named tests for hook-only use)

**Wrong: the named test hooks or stubs the original instead of executing it.**

| Function | Row now | Finding | Should be |
|---|---|---|---|
| 0021BAE0 | live (em_status_page / host `status_page_event`) | `test_status_page_reference.py` intercepts it as worker event 10. The original copies a 32-byte record (slot 0 +0x120 to +0xA0 of the block at D_00275670). The port's END_PROJECTION event only clears a flag. | stand-in |
| 00128350 | live (em_item_root.c "EE compare", test_item_root_reference) | `test_item_root_reference.py` replaces it with a host conversion, and em_item_root.c uses a host cast. The real translation is `em_sdk_soft_float_00128350`, verified by `test_sdk_soft_float_reference.py`; `em_sdk_soft_float.c` is in COMMON and bound into the collision world's SDK context (soft-float step, 2026-09-24), but em_item_root does not call it. | verified-unbound (the live cast is a host boundary) |
| 001798D0 | live (player_pose_use_accepted) | The original zeroes player +0x38, +0x21C, +0x25C, +5, +6 and +0x1F0 and requests 00174A50(p, 0). `player_pose_use_accepted` resets the legacy locomotion fields (`g.loco_*`, `g.gait`, `g.idle_t`, ...). The test compares the original's request against a fixed native expectation, not against the original's state. | stand-in (legacy player) |
| 001C6DA0 anim_eval_skeleton | live (em_pose_bank / em_player_pose) | Both named tests stub it (`calls[0x1C6DA0] = lambda o: None`). The verified translation is `em_pose_host_001C6DA0` (`test_pose_host_workers_reference.py`), but `em_pose_host_workers.c` is not in COMMON. The live evaluation is not compared with the original anywhere. | verified-unbound; the live evaluator is unverified |
| 0020B210 (BATTERY list), 0020B0D0 | live (em_battery_ui.c) | `test_battery_reference.py` has them in its `ignored` set, and `test_battery_pickup_reference.py` hooks both. No test executes either one. `em_battery_ui.c` draws the page with hand-placed sprites ("the highest available pack is the only row"). This is the BATTERY pop-up the user named. | stand-in (unverified native draw) |
| 0020AE40 | live (em_battery_ui.c) | Hooked by both named tests. `test_status_hub_ui_reference.py` executes it, but only checks the UI+0x20 clock ownership. The sprite calls in em_battery_ui.c are not compared. | partial: clock verified, draw unverified |
| 001281C0 float_to_int | live (em_status_background to_int / em_snow) | Both named tests hook it with host models. Verified copies exist only in modules that are not live (crate, area11 sfx, stream lanes, player stage). | unverified for the live copies |
| 001B1470 | live (em_player_heading / em_camera cam_wrap_pi) | `test_player_heading_reference.py` replaces it with a host wrap. It is executed only by tests of modules that are not live (crate, fan, effect, ladder entry, camera follow). | unverified for the live copies |
| 0018D910 | verified-unbound (em_camera_follow_original) | em_camera_follow_original only calls it as the `bounds` worker, and its test binds that worker to the ORIGINAL routine. The actual translation is the AREA11 branch in `em_camera_probe.c` (COMMON, `test_camera_probe_reference.py`, with vector math as a declared boundary). em_camera.c still carries the legacy `cam_solver_0018D910`. | module/test corrected; partial translation (AREA11 branch) |

**Wrong the other way: the row should be live.**
- 001C7C00 is marked verified-unbound. `em_cinematic_camera_sample` is its verified translation (`test_roger_cinematic_reference.py` executes 0x1C7C00). `em_render_frame.c` calls it every opening frame through `em_opening_runtime_camera`, and both modules are in COMMON. So it is **live for S2_opening**. It is unbound only for the Roger encounter (beat 14), because `em_cinematic_playback.c` is not in COMMON.

**Weak evidence, and the doc should say so on the row.**
- 0015C310 / 0015C420 (live) are checked only by spawn set and order. The port also drops the handles that the original stores at player +0x18 and +0x20, which matters if a later original reads them.
- 00183EF0 / 00184BA0 (live): the published scan list holds the panel, the terminal, the items and (class 4, not interactive) the crates and drums (W22). Beat 05's Cross presses go through this scan as in the original and fall through to the ledge probes; the door in beat 09 is not published yet.

**Missing rows that are really substitutes or mis-grouped.**
- 001F6D60, 001F6E40, 001F66F0, 001F6640 and 001F6760 are grouped as "effect manager / effect kinds". They are the room point-light list selection and registration (AREA11_POINT_LIGHT.md). The port replaces the chain offline: `tools/export_point_lights.py` resolves the list, and the live `em_point_light.c` adopts it. Classify them as stand-in (offline export), not as missing effect code.
- 008237C0 (overlay init) has the note "the roster spawn replaces it". By section 1.3 that is a stand-in, not missing.

The other sampled missing rows are confirmed to have no port code: 0018A1F0, 001CACB0 (appears only as a method-table constant in em_status_models.c), 00179D20 (hooked in a test), 001C7900 (appears only as a range end in tests), 001DDAA0, 001F4D40, 001FB3E0, 001DEEE0, 001D19D0 (a reported no-effect binding), 001FCF10 (in a test's ignore set) and 001E0C60.

The sampled live and verified-unbound rows that held up: 001AD250, 0021B840, 001CA0A0, 001CA1C0, 0020A7A0, 0011E748 (item UI only, as noted), 001BC350, 0011DB90, 001A4030, 00163C10, 001E2560, 001F9CF0, 001FD470, 00823FF0, 0015DEC0, 0019D330 and 00187EE0. The fade rows 001AEDB0 and 001AEE10 hold, with the oracle caveat already given in section 6. The player-stage lane (0015B530, 0015D000, 0015D100, 00182B30, 00182D70, 0021C440, 0021BB00, 0021C3F0, 0021D640) is executed unhooked by `test_player_stage_workers_reference.py`.

**Rate.** Of 25 sampled live and verified-unbound rows, 4 were wrong (0021BAE0, 00128350, 001798D0, 0018D910), 1 was wrong in the conservative direction (001C7C00), and 2 are weaker than their status says (0015C310, the fade pair). A targeted scan then found 6 more wrong live rows (001C6DA0, 0020B210, 0020B0D0, 0020AE40, 001281C0, 001B1470). Treat the live total of 187 as an upper bound until the next pass does the following for every row: open the named test and require that the address is **executed** (an entry run, or inside an asserted executed range), not merely present in a hook, `calls[...]`, `ignored` or worker-binding table.

### 7.3 What the census cannot see (fidelity items with no function row)

- **GS state.** The fog and background colour, the clear colour, the framebuffer and display registers (512x448 shown at 4:3), alpha and test modes, dithering. The letterbox bars and fades are GS sprites whose words come from the 0x28A8D0/0x28A9A0 machines. The census proves those machines ran, not what reached the GS. This needs GS dumps or framebuffer captures per beat (route beats already save `gs.bin` and `original.png` at their snapshots only).
- **VU1 microcode.** The census has no VU1 candidates. The two large non-function regions (from 0x2307E4, and near 0x243C20) are referenced only by address, by EE code that builds pointers to 0x230800..0x241090. They are most likely VU microcode and DMA packets, but this review did not characterize them. Wherever transform, lighting, fog or clipping runs as VU1 microcode, it has no entry to break on.
- **VU0 micro-mode programs.** Same as above. VU0 macro-mode code sits inside EE functions and is covered.
- **IOP side.** SPU2 mixing, reverb, the streamed BGM and voice ADPCM, CD read timing, pad hardware. Only the EE-side requests (001FBD50, the stream lanes and the SIF RPC) are counted.
- **IPU/MPEG.** The intro movie decode in S1.
- **Data.** Script bytecode (0x246F20, 0x82A990, 0x8292C0, 0x8283D0, ...), message text, animation clips, collision grids, level geometry. The census shows that the interpreters ran, not which opcodes or data paths they took. See the per-case note in 7.1.
- **Order, counts and timing.** One hit per label: no call counts, no per-frame order, no vsync or interrupt phase. Use ORIGINAL_FRAME_ORDER.md traces for order.
