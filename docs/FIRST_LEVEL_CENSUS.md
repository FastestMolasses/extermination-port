# First-level route census: every original function on the route and its port status

Date: 2026-09-23 (session s87); every row re-classified against the port HEAD 9e0715f on 2026-09-24 (section 1.4); totals recounted with a measured liveness pass at ce7271f + the full-route smoke step on 2026-09-25 (section 1.14) and recomputed from the rows by the shadow step on 2026-09-26 (section 1.20) by the status UI step on 2026-09-26 (section 1.21) by the owners step on 2026-09-26 (section 1.25) and by chain C7's takeover step on 2026-09-26 (section 1.26) and by chain C7's step-V step on 2026-09-27 (section 1.27) and rechecked by chain C8's EE-float step on 2026-09-27 (section 1.29, no status change) and by the fence door's side-1 step on 2026-09-27 (section 1.31, no status change); liveness re-measured over the whole route (00..14, both side beats) on 2026-09-26 (section 1.22) and again on 2026-09-27 at port HEAD 6da4eb5 (section 1.33, no status change); rechecked by chain C8b's MAP step on 2026-09-27 (section 1.37, no status change); recounted from the rows by chain C8b's module loader step on 2026-09-28 (section 1.38) and by the L24 step (the security gun, its cable and the fan pair) on 2026-09-28 (section 1.39). Target: the pinned boot ELF (SHA-256 `ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`) and the AREA11 overlay (id 9).

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
- **Live**: the translating module is in COMMON and the translating function is reachable from `main` in a call graph of the live build, with the test-only modules (`em_level_smoke_test.c`, `em_opening_control_test.c`, `em_game_selftest.c`) and the runtime-gated paths cut. The gated paths are the player FLOOR layer (`player_states_bind` engages only the STAGE mechanism since L01, so the floor service, the fall check and the FLOOR states never run; the stage's prelude and +4 = 4 handler were bound but unreached while the interaction runtime owned the takeover; a script owner's takeover runs them since chain C7, section 1.26), the reversal skid (`reversal_ready()` is false), and every binding that returns `unmirrored(UM_…)` (a reported no-effect binding).
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
  (0x80000065 every 8 ticks) run the effect binder since section 1.17, their
  chains not drawn (L26); the loop
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
  gap; no live effect owner, L26; live since section 1.17) and 001EBF10
  (their kind handler; live since section 1.17);
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

### 1.15 Update (2026-09-25, WP-8b: the stream lanes live; the director and Roger's voiced line live)

- **Moved to live (19 rows):** the stream lanes 001F9CF0, 001FA0D0, 001FA330, 001FA570, 001FA5F0, 001FA790,
  001FAAC0, 001FAB50, 001FABB0, 001FABF0, 001FAE70, 001FD470 (em_stream_lanes_original through em_stream_live), the
  voice ring push 001FA5A0 (em_message_voice_ring_push over the lanes' ring), the director 008253F0 and its six beat
  functions 00825500..00825710 (em_director_original on node #21) and the op15 handler 001B6FA0 (Roger's 0x828990).
  Measured, not inferred: a `-O0 -g` build of the live link line ran the full level smoke under lldb with
  auto-continue breakpoints; hits: 001F9CF0 / 001FA0D0 / 001FA5F0 / 001FA330 14,272 each (one per step H), the lane
  start 15, the fade-in 20, the music release 14, the lane release 74, 001FABB0 7, 001FAE70 5, 001FD470 2, 001F9820
  1, 001FA5A0 8 (the voiced lines' cues), 001BA1F0's op15 2,357, 008253F0 12,573.
- **Moved to unverified:** 001FC280. Its row named em_stream_lanes_original, whose oracle records the call as a
  worker; the live body is em_scene_bindings_001FC280 (partial: the D_00282160 cache is not modelled, a loop id
  other than -1 faults) and nothing original checks it.
- **Notes updated:** 001FAB80, 001FB100 (its 001F9CF0 call runs at step H; the rest is not bound), 001FBC50,
  001FCA10, 001FD580 / 001FD6A0 (voiced records no longer fault), 001B1EA0, 001B7D60, 001BA1A0, 001C4760 and
  00823950 (the director's and Roger's alternate paths), and the boundary kind "stream / voice IOP service tick".
- **Boundaries:** the IOP stream driver side (00112610, 00112D18, 00113280, 001157F0, 0011A2B0: em_iop_stream), the
  stream-lane SDK commands (00119828, 0011A4E8, 0011A608, 0011A658) and 001F9820 now run live inside em_stream_live
  and are checked by test_iop_stream_reference / test_stream_lanes_reference; they stay boundary rows here (moving
  them to section 3 is the lead decision of lane L36's IOP/disc boundary).

Result: live 469, verified-unbound 241, unverified 6, stand-in 3, missing 0, boundary 465.

### 1.16 Update (2026-09-25, census L32 + L30: the render context live)

The one canonical render context runs live (em_render_context_live,
docs/RENDER_CONTEXT.md section 8): main-loop step B 001D1AE0, the frame head
001D1C50, the frame close 001D1EA0, the area render init 001C1DC0, the zoom
writers and 001DD980's store 001DD950, over one storage whose .data comes
from the user's ELF (tools/export_render_context.py). Evidence per row: the
lane oracles, test_render_context_live_reference (the live composition
against the original over beats 00..14) and the level smoke's
check_render_context; liveness measured on the live link line (a coverage
build of the full-route smoke plus side beat 00; counts in RENDER_CONTEXT.md
8.5).

- **Moved to live (50 rows):** 001D1C50, 001D1EA0, 001D2590, 001D25F0,
  001D2610, 001D2830, 001D2960, 001D2D20, 001D30A0 (L32); 001D2710, 001D2910,
  001D2DE0, 001D2E00, 001D6B10, 001D6C90, 001DD950, 001DDA00, 001DDAA0,
  001DDE10, 001DEEE0, 001E0C60, 001E0C80, 001E0CC0, 001E0D70 (L30); 001CB760,
  0021B970, 0021B9A0, 0021BA80, 001D8FD0, 001C1DC0, 001C1E70, 001C1E80,
  001C1E90, 001C1F50, 001E2260, 001E2270 (L31, L39); 0021B8E0, 0021B900,
  0021BA70, 0022EBE0 (L35); 00100268, 00100610, 001006D8, 001D1F20,
  001D1FF0, 001D2040, 001D6930, 001D6BA0, 001D6E60 (L38, reached through
  001D1AE0 and 001DDE10); 0015D2F0 (L28, the context's calls; em_weapon.c
  keeps its own assumption).
- **Boundary rows now translated and live (5):** 001D1AE0, 001D2730, 001CB800,
  001CB8A0 and the empty 001CBA40 (new rows; removed from section 4).
- **Notes updated:** 0021B920 (its body now inside 0021B970 / 0021B9A0; the
  Metal fog reads the context's pair), 001D7B30 (em_actor_light_001D7B30 is
  the live translation, 001D8FD0's; the actor lighting's room rig stays the
  manifest's lines, L40).
- **Not bound (RENDER_CONTEXT.md 8.4):** 001C1D00 (001D5370 needs the
  static-object bank export; 001E0CF0 the background channel), 001D52E0
  (reported), 001D19E0, 001D1EF0 and the status / teardown / load-veil
  001D2830 calls (flag 3 needs step V 001D2300), 001D19D0 / 001D9070,
  001D8060 / 001D80B0, 001D88B0 / 001D8C30, 001DD7B0 / 001DD940, 001E0C30 /
  001E1010, 001E0DF0 / 001D21B0.

Result: live 524, verified-unbound 191, unverified 6, stand-in 3, missing 0,
boundary 460.

### 1.17 Update (2026-09-25, census L26 / L27 / L28 / L39: effects, equipment, head sprites live)

The effect originals, the head sprites and the player equipment run live
over the one render context: `em_effects_live` binds em_effect_original,
em_effect_kinds, em_effect_manager, em_head_sprite_original,
em_player_equipment_sprite and em_packet_chain_original (the chain table
D_007635C0 and the arena of em_render_context_live), and `em_equipment_live`
binds em_player_equipment on the seven 0018A6B0 nodes (EFFECT_MANAGER.md
section 8, PLAYER_EQUIPMENT.md section 8). The static ELF data comes from
the user's ELF (`tools/export_effect_tables.py`, `assets/effect_tables.emet`);
the equipment models from the Roger export's D_0028A56C spans
(`tools/export_roger_banks.py`, now with the equipment models 0x2F..0x3D,
0x40, 0x6A..0x6D).

- **Moved to live (49 rows):** the barrel 001F0360 with 001F6210, 001F6BB0,
  001F6EB0, 001F40C0, 001F0720 and the glow markers 001F5C20 / 001F5640 /
  001F5940 / 001F5CA0 / 001F4D40 / 001CD520; the resets 001F0310 / 001F03D0
  / 001F3FA0 (001AFCA0's 001D0660); the spawns 001EF9D0 / 001EF940 /
  001EFD90 / 001EFD20 (every player-side 001EFD90, the truck's, the crates'
  and drums' and the weather node's 001EFD20) and the driver 001EA240 with
  its depth key 001CCF70 and the handlers 001EC1F0, 001EC3F0, 001EC470,
  001EBF10 (001CFB50 / 001D0540, 001CFBE0); the pickup glint 001F0A60
  (001F1180 is whole now); the head sprites 001F0120 / 001E2290 / 001E23A0 /
  001E2560 with 001CFA60 / 001CD370; the chain builders 001CB5F0 / 001CB6B0 /
  001CB900 / 001CB9B0; the equipment 0018A6B0 / 0018A8D0 / 00188630 /
  00188A50 / 00188AC0 / 00188B80 / 00188DF0 / 00188ED0 / 0018A1F0 /
  00189D30.
- **Stand-ins retired:** the counted effect gaps (em_player.c
  `player_effect_gap`, em_area11_boxes' truck gap), the no-op pickup
  aura_draw, the reported no-effect w_001F0360 (kept for a scene without the
  roster), the bindings' own 001EF9D0 copy (the head sprite and weather
  spawns now use em_effect_original's), the unbound 0018A6B0 / 001E2560
  rows, and the snow's and the AREA11 effect's host fog coefficients: 001E67C0
  programs near 0 / far 300 with 0021B9A0 on the context and restores mode 1,
  the effect owner's draw takes the context's +0xA0 at its walk position (its
  001D04B0 copies it through 001CFBE0).
- **Measured, not inferred.** A coverage build of the live link line
  (`-fprofile-instr-generate`, scratch only) ran the full-route smoke and side
  beat 00: 001F0360 14,223 calls (11 glow markers and 6 lanes each), 001EF9D0
  131 (93 001EFD90, 34 001EFD20, 4 001F0120), 001EA240 7,231, the handlers
  7,106 (001EC3F0 3,468, 001EBF10 2,592, 001EC470 704, 001EC1F0 342),
  001CFBE0 25,318, 001E2560 28,446, 001F0A60 1,067, 0018A6B0 99,570 (0018A8D0
  23: the seven nodes plus the equipment change's respawns, three
  0015C310(player, 1) after status screens; 001C9610 28,430), 001F0310 2.
- **Against the captures (test_level_smoke.py check_effects):** at the port
  ticks the phase checks align with the last rows of routes 08, 10, 11, 12,
  13 and 14: the seven equipment nodes' +0x00..+0x0F, +0x44 and +0x4C, the two
  head sprites' lifecycle / key / owner / bone / offset and the effect nodes'
  state / subtype / step / limit / accumulator equal the snapshots (route
  08's eight truck puffs with their +0xB0 and +0x100 bit for bit; route 12's
  four player footstep puffs, the player-side 001EFD90 spawns); every barrel
  lane's packets 1..3 equal the snapshot's latest DMA buffer (the lane-3
  parameter quadwords excepted: identical in every capture from
  opening_ee.bin to route 14, so no routine of the level writes them; their
  earlier writer is open, EFFECT_MANAGER.md 8.4); in routes 10 and 14, where
  the port's camera equals the capture's, packet 4 (the view and fog) and
  route 10's five visible glow-marker primitives (colour excepted: rand())
  too. The head sprite's sub-state +0x05 follows rand() (its wait is
  00122BB8() % 40 + 60) and is not compared.
- **Not moved:** the room point-light lists 001F6640..001F6E40 (the offline
  export stays their stand-in, critic 7.2); 001CD390 and 001F0460 (no
  first-level effect reaches them in the port's runs); the effect chains and
  the equipment's +0x4C draw are the renderer's (no port stage draws the
  chains; the player mesh draws the seven equipment models at its nodes 4 /
  14; PLAYER_EQUIPMENT.md 8).
- **Counted gap (off the route only):** the two untranslated handlers
  checked to be packet-only, the skid's 001EAD70 (0x80000033, on snow) and
  001EC270 (0x80000012): each stores only the work block's +0x1F4, which
  001EA240 rewrites before every call, and draws; skipping one loses only
  its packets (counted, reported once, play goes on). A scratch run
  reversing the stick every 45 frames for 900 frames after first control
  counted both and played on. The level smoke asserts the route counts
  none.
- **Fail-stop:** every other handler em_effect_kinds does not translate
  (some do more than draw: 001EF510 spawns a child node) and 001EFE00's
  spawn fault at their address. None is reachable in AREA11 in the port
  (the surfaces, +23C, the hit requests and the crates' damage that would
  spawn them do not occur; EFFECT_MANAGER.md 8.2); a scratch spawn of
  0x80000009 faulted at 001EF510 and stopped the game task.

Result: live 573, verified-unbound 142, unverified 6, stand-in 3, missing 0,
boundary 460.

### 1.18 Update (2026-09-25, census L18: the fence door on its original owner)

The fence door (roster record 0, callback 001BC350) runs on its original
owner, `em_area11_door` (DOOR_ORIGINAL.md "Binding"): `em_door_original_tick`
over the pool record, 001BBDA0's 001B0F60 with 001B0EA0's bone slots,
001BBE40 (`em_door_transit`, now on the EE float model), the ELF program
0x24DE40 on the AREA11 script host (op0B subs 6 / 0 newly admitted in
em_area_script), 001BC150's room move, 001BC300's pose, publication
(001B1B30) and draw. The Use scan publishes and selects it (00183EF0's
class-5 branch, em_door_candidate). Side beat 09 is a live phase of the level
smoke (`make test-level-smoke-full`'s side-9 run).

- **Moved to live (10 rows):** 001BC350, 001BBDA0, 001BBE40, 001BC0E0,
  001BC240, 001BC290, 001BC300, 001BBD60 (section 3.12: all nine rows live
  now, with 001BC150), 001B1B30 and 001B0F60.
- **Fidelity fixes found on the way:** (1) 001BBE40's geometry ran with
  truncating host arithmetic, and its oracle on an IEEE interpreter: both put
  the alignment point one ULP off route 09's f309; on the EE float model
  (native and oracle) it is the capture's bit for bit. (2) D_008101E4 had two
  storages: the scene state's byte (written only by 0x1AE040 state 4, read by
  the effects' 001EF940 gate, the render context and the tick log) and the
  live camera's +0x04 (written by the scripts and the camera). State 4's
  clear now reaches the camera byte (at its 0018D7B0 call) and every reader
  reads the camera's. (3) 001AFCF0 clears 3B8F with 3B8D under a held player;
  the shared runtime keeps the hold and releases on the cleared 3B8D, as
  0015BA50 / 0015B530 / 00182DF0 do.
- **Stand-ins retired:** the legacy em_door in AREA11 (the manifest door,
  its scan, the H13 walk-to, its open script and door camera, the S12b room
  move adapter in em_door.c), `camera_area11_standins`' door cinematic,
  state 4's `g.doorcam = 3`, `EM_ROOM_MOVE_TEST`; duplicate handler copies of
  em_door_program (the program's op07 / op0A / op0B / op0D / op02) with
  their tests `test_door_program_reference.py`, `test_door_program_runtime.py`
  and `tests/door_program_test.c` (the script host's handlers are the one
  bound translation).
- **Against the capture:** the level smoke's `fence_door` equals route 09
  f309..f532 row for row (LEVEL_SMOKE.md); `test_area_script_reference.py`
  replays the program over route 09 with no difference; the render context
  and effects checks pass over the run (one state-4 re-seat tick counted).
- **Not moved:** side 1 (entry 1: no capture then; its walk-out was the
  legacy walk-out until section 1.31), the locked program (subtype 0x15) and the door id's bit
  7 fault; em_sdf_001BC240 / em_sdf_001BC290 stay unbound standalone copies
  of em_door_original's inline phases.

Result: live 583, verified-unbound 132, unverified 6, stand-in 3, missing 0,
boundary 460.

### 1.19 Update (2026-09-25, the object-unit step: GS-exact world-owner draw, OWNER_DRAW.md P1/P2)

Rows moved by the object-unit step. The evidence is the oracles named on
each row, tools/test_object_unit_reference.py (every triangle of the 119
captured owner units and of the 60 captured face units equals the original
VU1 microcode's) and the level smoke's check_owner_units (the live units'
colour matrix and rig lighting lanes equal the route snapshots'; in the
camera-exact beat 10 also the drawn set, the byte counts, the clip pass and
the position rows).

- **To live:** 001CAA00, 001CA990, 001C7420, 001CA7B0, 001CA940, 001D38A0,
  001D3BA0 and 001D1F80 (em_owner_draw_live, for the crates, drums, truck
  and fence door); 001D89D0's bit-exact translation (em_actor_light_001D89D0)
  as their worker (the row was live through em_lighting.c; both now run:
  the legacy chain keeps em_lighting).
- **Moved in from the GS/VIF boundary list and live:** 001D2090
  (vif_append_ref_tag, inside em_owner_draw_original), 001D37D0, 001D38F0,
  001D3AD0, 001D3C30 (em_owner_draw_original) and 001D2E20 (skin_arena_init,
  em_skin_arena_init.h, run by 001D19E0's binding at the area load).
- **Corrected:** 001CB3C0 verified-unbound -> missing. em_owner_services
  only names it as a worker slot; no port code translates it (a label is
  not evidence). An owner with +0x90 != 0 faults in em_owner_draw_live.
- **Unchanged, with a note:** 001D19E0 stays verified-unbound (its first
  callee runs live); 001D8C20 (now also the owner draw's store).
- **Not moved:** the player's, Roger's and the legacy-drawn owners' +0x4C
  (OWNER_DRAW.md section 11). The VU1 programs 0x0023C750 / 0x002354A0 /
  0x0023C480 are not EE functions and have no rows; the renderer runs their
  translations (em_object_unit_run).

The totals of section 2 are not recomputed here (as in 1.5 .. 1.18); the
subsection counts of 3.14 and 3.16 and section 4's boundary counts are.

### 1.20 Update (2026-09-26, census L29 + L29b: the player's drop shadow live)

Rows moved by the shadow step (docs/SHADOW_ORIGINAL.md "Binding",
SHADOW_ACTOR_ROUTE.md section 4, SHADOW_DECAL.md section 5). The evidence is
the oracles named on each row plus the level smoke's check_shadow
(tools/level_smoke_shadow.py, LEVEL_SMOKE.md "The drop shadow"): every
post-step of the run takes 0015C160's own route for its logged gate bytes and
every drawn shadow is flushed; the first-control frame draws the shadow (the
original's playable capture holds the 001DA290 chain); on sampled calls the
ORIGINAL 001DA6A0 executed over the port's own inputs builds the port's plan
(light globals, ctx+0x24B0, the silhouette VP, both box uploads, the UV upload
and the receiver sequence with its classes), and the ORIGINAL 0015BF90 and
001CE300 executed over the port's inputs (0019A570 answered with the port's
hit) submit the port's quad and write the port's packets byte for byte.

- **To live:** 0015C160 (w_0015C160: the gate, 001CB590, the route and the
  +0x4C request), 001DA6A0 with 001D98A0, 001DA080, 001DA290 / 001DA1E0
  (em_gfx_shadow_alpha_clear), 001DA310, 001D9EE0, 001D5C80, 001D4CD0,
  001D4FB0 and 001D4B50 (the class-2 re-pass); 0015BF90, 001F9100,
  001F8D30, 001CD390, 001CE300, 001CF470 (was unverified: the decal lane's
  oracle verifies it); 00102900 (em_shadow_actor_route's copy, run as
  original by its oracle).
- **Moved in from the boundary list and live** (the render-range rule):
  001CB950 (GS/VIF sprites) and 001CF870 / 001CF970 (resource / display-object
  registry: clipper leaves).
- **Stores added on the player record** (original writers cited in the
  code): +0x96 = 0x28 (0015C1F0 at 0x15C2F4, row stays verified-unbound: its
  model bind is still reported), +0x98 = 1 (0015C420's 001CA6F0(player, 1))
  and +0x09 = +0x0C (0015C420 after the node allocation).
- **D_00810771** (event 0x19, 0015C160's gate) is migrated into the
  EmProgress region (em_scene_state.h).
- **Still not live:** Roger's 001DA6A0 (reported UM_001DA6A0: his kind 0x29
  proxy D_0028A490[0x29] is not exported) and the post-step while the player
  record does not hold the displayed pose (the opening, design risk 2:
  reported UM_0015C160_OPENING, no shadow).

Recount (this step): the totals of section 2, the section 3 subsection
counts and section 4's boundary total are recomputed from the section 3 rows
with the 1.14 method (every row's status; instruction counts and labels from
`route_functions.json`); the previous updates 1.15..1.19 had not recomputed
them.

### 1.21 Update (2026-09-26, status UI step: the BATTERY page, the presenters, the fog record, the indicator binds)

Rows moved (evidence on each row):

- **stand-in -> live: 001FCB90, 0020CCB0, 0021BAE0.** The mode-3 / mode-4
  presenters run at step F (em_message_presenters_live over the service's
  own draw module: 001FD0E0 with the cue walker 001FDDB0, 001FCB90, 001FCF90,
  001FCF60); the status pages' message words D_002821B0 / B4 / B8 /
  D_00282240 are a view of the live block (em_status_runtime), so the page
  layer's own mode-4 copy and the step-F gate stand-in are deleted (WP-8
  decision (b)). 0020CCB0 is the BATTERY page's marker (below). 0021BAE0
  restores the fog record at END_PROJECTION on the live render context.
- **verified-unbound -> live: 001FCF10, 0020AE40, 0020B0D0, 0020B210,
  0020BEF0, 0020CD40, 0020CD60, 0020CDA0, 0021BAC0, 001C2360, 001C22A0.**
  002149F0 runs as em_spr_002149F0 (em_battery_page_live, the ITEM child
  page 5 of the runtime, over the UI block D_00810130) with its page draws
  and cues; its 2D leaves are drawn from the EMBA atlas by TEX0
  (em_battery_ui, rewritten as the leaf list; the hand-placed page is
  retired). 0020DFA0's 0021BAC0(0) and 0021B9A0(5, 0.0, 1e6) run on the
  CONFIGURE path. The indicator children bind their model and bone slots
  (001C2360 over D_0028A56C, now in the Roger export; 001C22A0 over the
  world bank) and place their slots with 001C6380 (em_indicator_bind_live).
- **unverified -> live: 0020DFA0.** Every callee runs on the CONFIGURE path
  in the original order: the runtime's CONFIGURE case issues the host's
  RESET_DRAW (001AFE60, em_status_models_clear), then 0020E020 (its trail),
  then the host's CONFIGURE (001029C0 / D_00810624, 0021BAC0, 0021B9A0,
  001D2610); test_census_unverified_reference executes the original
  0020DFA0 and probes the path and that order.
- **002149F0** stays live with a new module (em_battery_page_live instead of
  em_battery_ui.c's page and the host's battery_finished hook).
- **Notes changed:** 001C5680 / 001C5760 (the draw is the owner's mesh; the
  terminal's 0x827E6C copy of its node matrix into the child's slot is not
  bound), 001D2300 / 001E1E60 (the channel-3 gate reads flag 0x20, flag 4
  and D_008106C4 as 001D2300 does, and flag 0x21 for 001E0DF0's
  "ctx+0x1D8 != 0", the word 001E0CF0 builds only under 0x20 and 0x21, as
  001C1D00 is not bound; BACKGROUND.md "Why flag 0x21"; the manifest line
  only names the asset).

Evidence (level smoke, LEVEL_SMOKE.md): the battery notice (239 frames) and
the panel's confirmation and discharge row for row as before, now on the
original page; check_render_context compares the fog record's save slot
+0x120..+0x13F with routes 01..14 on every tick after the first status
screen (a build without the save fails at its close tick);
check_indicator_children compares every bound child's +0x09 / +0x0C /
+0x0D / +0x44 / +0x4C with the route snapshots and its first slot's
001C6380 matrix bit for bit (a shifted placement fails); the terminal's
child only before the elevator scan (routes 00..03), its later ticks are
skipped and counted, as its 0x827E6C copy is not bound.
tests/status_runtime_test.c runs the runtime-level BATTERY scenarios on the
bound page (em_battery_page_live_tick). The terminal-screen
colour: the port shows the red arrow in the refusal, as route 02's capture
does (+0x28 = 0, +0xA0 = (1, 0, 0, 0.25)); the green arrow in
startup-reference/elevator/refusal is that fixture's seeding (it cleared the
power byte of a powered state, so the level decays from 128), not the
original route (CENSUS_UNVERIFIED.md).

Not a row: 0020CD80 (em_spr_0020CD80, the no-device cue) is bound with the
page but the route never reaches the list's no-device path.

Recount (this step): section 2 and the section 3 subsection counts are
recomputed from the section 3 rows with the 1.14 method.

### 1.22 Recount (2026-09-26, full-route smoke step: liveness measured over the whole route)

Sections 1.15..1.21 moved rows by hand; the last measured liveness pass (section 1.14) ran the smoke only through
the director's preparation. This recount measures every row again over the whole live route.

- **Edge recorder.** A private `-O1 -fno-inline -finstrument-functions` build of the live link line (211 sources,
  the Makefile's link line at port HEAD 097fbd9) with a caller / callee edge recorder (scratch only, deleted) ran
  five times: the full level smoke (all 18 main-line phases PASS, their capture checks re-run on its tick log and
  PASS), the side beat 00 run, the side beat 09 run, `newgame-control` and `EM_AREA_CHANGE_TEST=1` (all PASS).
  A native function is live when it is reachable from `main`, the audio thread or a constructor with the test-only
  modules (`em_level_smoke_test.c`, `em_opening_control_test.c`, `em_game_selftest.c`) cut: 6,213 of the 6,329
  native functions that ran.
- **Row to native function.** Each row's candidates are the native functions whose name or preceding comment block
  (in the source or in the declaring header) cites the address. Rows whose candidates were only port bindings
  (`w_`, `um_`, stand-in names) or legacy code were read by hand, as were the live rows without a candidate that ran.
- **Live rows confirmed: 634 of 634.** 608 have a candidate that ran on the live path; the other 26 run inline in a
  live function of their named module (the title menu in em_startup_tick / title_draw, the panel program's execute,
  em_pickup_owner_tick, the ladder climb state, em_interaction_alignment, em_door_original_tick,
  em_cinematic_camera_sample, Roger's init and em_roger_tick, the owner-draw thunks, em_gfx_shadow_receiver /
  em_gfx_shadow_alpha_clear, em_point_light_register, em_shadow_original, em_item_root_tick,
  em_actor_class_publish_001B1B70, the director's beat functions and sdk_0011E080).
- **Upgrades to live (6 rows):** 00102948 (em_frame_render_heads copy_qw), 0011D878 (the tangent kernel of the
  FLOOR's tanf), 001AF890 (the slot return; the port's decomposition of 001AF800, see its row), 001B1380
  (op15 through the script host workers), 001B6F80 (em_area_script op01 kind 9: Roger's 01/9 placement, which the
  smoke compares exactly from route 14 f1756) and 001C6150 (inside the box and Roger allocations). Each
  translation ran on the live path, and its named oracle executes the original.
- **To unverified (1 row):** 00187DC0. Its handler is reached live now (the floor service's first contact, from
  the truck crossing on), but the live body is em_player.c's live_first_contact, which no oracle executes (the
  floor oracle's contact hook repeats it).
- **Notes corrected:** 001C7C00 (em_cinematic_playback is linked and samples Roger's bank 0x96 timeline live),
  001D52E0 (its live caller reaches the reported UM_001D52E0), 001E0CF0 / 001D4B50 / 001DA1E0 (em_render_verify_rest
  is linked; those copies are not called), 001C6150 (three unchecked live copies of the byte read remain).
- **Not upgraded although a function with the address in its name ran:** 0015C1F0 (spawn_w_0015C1F0 with the
  reported model bind), 001AFCA0 (the port's own re-arm), 001C1D00 (em_render_001C1D00), 001CA770 (the script
  host's frame event, the interaction runtime's skeleton release), 001CB5B0 (three no-op worker slots), 001D19E0 /
  001D52E0 / 0021B1B0 / 0021B500 (reported no-effect bindings), 001FBC50 (em_sfx_stop_all), 001FC280 (unverified,
  section 1.15) and 00825940 (the legacy group of the security gun and its cable, then called the husks; retired
  in section 1.39). Their rows already say so.

Result: live 640, verified-unbound 87, unverified 5, stand-in 0, missing 1, boundary 451 (was 634 / 94 / 4 / 0 / 1 /
451). Section 2 and the section 3 subsection counts are recomputed from the rows.

**Duplicate translations seen by the pass** (not changed here; each needs one bound owner): the byte read 001C6150
(w_001C6150 in em_status_models, em_indicator_bind_live, em_equipment_live), 0019F330 (em_coll_list_passes_walkers
against em_collision.c's column crossing, which the live 0019BC40 still uses), 001BC240 / 001BC290 (the unbound
em_sdf_ copies next to em_door_original), 001D4B50 / 001DA1E0 (the unbound em_rvr_ copies next to the shadow
passes) and 00102948 (inline copies in em_anim_runtime_rest, em_camera_retarget.c, em_camera.c,
em_camera_probe.c, em_player_ladder_entry.c). All of them are reduced in section 1.28.

### 1.23 Update (2026-09-26, the player step: the player and its equipment on the object-unit draw)

The player's +0x4C and the seven equipment nodes' +0x4C run the original 001CAA00 (OWNER_DRAW.md section 10;
PLAYER_EQUIPMENT.md section 4.2). Rows moved (evidence on each row):

- **verified-unbound -> live: 0015C1F0, 001AF5C0.** 0015C1F0 binds the player's model (spawn_w_0015C1F0 runs
  em_player_misc_0015C1F0: +0x2FF, 001CA6E0, +0x0C = 001C6150, +0x96; 00200890 is the boundary), so the record's
  +0x44 / +0x4C are the original's (0x00D1C1C0, 001CAA00) and the draw reads them. 001AF5C0 (em_slg_001AF5C0) wipes
  the player record at w_001AFCA0's position: +0x02 bit 0x20 (the camera fill 001D89D0 reads), the colour words
  +0x80..+0x8C and +0x94 = -1 (001C7420's collapsed bone) were 0 in the port's record before.
- **Notes updated (no status change):** 0015C160 (its +0x4C is 001CAA00(player)), 001CAA00 / 001CA990 / 001C7420
  (the player's 21 nodes and the equipment), 001D89D0 (the camera fill now runs live, over the player), 0018A6B0 /
  0018A8D0 (the equipment's +0x4C and its model bank), 001C6150 (em_equipment_live's copy reduced to the one
  em_owner_services_001C6150), 001CA6E0 / 001CA5E0 / 001CA5F0 / 001D8BF0 (the player's bind and wipe), 001AFCA0
  (its 001AF5C0 is live).
- **Evidence.** test_object_unit_reference (the player's 15 and the equipment's 105 captured owner-frames: every
  triangle equals the original microcode's), test_actor_light_001d89d0_reference (the native chain, including the
  native 001D89D0's camera fill, equals the original unit byte for byte over the same 120 owner-frames),
  test_level_smoke.py check_owner_units (the player and the equipment keyed into the snapshot comparison: in the
  camera-exact beats 10 and 14 all 16 owners, the player and its equipment included, equal the original's unit
  bytes, clip pass, position rows and colour / rig lanes; elsewhere B and the rig lanes wherever the player's point
  equals the snapshot's). The opening's 1,302 reported post-steps keep the port's mesh (design risk 2).

Result: live 642, verified-unbound 85, unverified 5, stand-in 0, missing 1, boundary 451 (was 640 / 87 / 5 / 0 / 1 /
451). Section 2 and the section 3 subsection counts are recomputed from the rows (the recount writes section 2 and
the subsection lines only).

### 1.24 Update (2026-09-26, WP-13: the chain page D_007635C0 drawn)

The page 001CB800 splices at every frame close is walked as the DMA sends it and drawn (docs/CHAIN_PAGE.md): the
DMA / VIF1 / GIF / GS walk (em_chain_page), the two VU1 programs the page CALLs translated from their microcode (the
lane program of D_00233290, the sprite program of table 0x231770; em_vu1_page_programs.h), 001D0F20's blend-preset
bank (em_gs_blocks_original) and the GS pixel path (em_gfx_gs_prims). No row changes status: the microcode, the DMA
list and the GS are the renderer boundary (no EE function of section 3), 001D0F20 runs at boot before the first
captured label (no row), and every producer on the page was live already.

- **Notes updated:** 001CB5F0, 001CB6B0, 001CB760, 001CB800, 001CB900 and 001CB950 (the blocks they build are walked
  and drawn), 001CD520, 001CE300, 001CFBE0, 001F0720 and 001F0A60 (their packets drawn; 001CE300's dedicated renderer
  entry retired), 001DDE10 and 001D6C90 (the four-sprite pass walked over, not drawn).
- **Evidence.** test_chain_page_reference (every MSCAL of the 15 captured pages and 600 + 600 synthetic batches equal
  the ORIGINAL microcode; the captured pages' 386 primitives equal the original walk's; the preset bank equal to every
  capture and to the executed original 001D0F20), test_chain_page_gpu (the GS pixel path against a GS pixel model),
  tests/chain_page_test.c (ASan / UBSan), test_level_smoke.py check_chain_page (40 sampled pages of the full route
  re-walked with the original microcode over the port's own bytes; route 10's glow markers equal the capture page's).

Result: live 642, verified-unbound 85, unverified 5, stand-in 0, missing 1, boundary 451 (unchanged).

### 1.25 Update (2026-09-26, the owners step: the remaining world owners on their records, the terminal's 0x827E6C)

The terminal 00827B10, the panel 00159210, the prop 001C4820, the six items 00219550 and the map item 0015AFA0, and
the opening controller 00823E80's canopy run their model binds, their placement and their +0x4C draw over their
own pool records (em_area11_boxes_owner_*: 001B0FD0 / 001B1020 over the world bank or the D_0028A56C library,
001C6380 into the record's bone slots, 001CAA00 through em_owner_draw_live; OWNER_DRAW.md section 10). The
terminal's per-frame copy of its node matrix into its 001C5760 child's slot (0x827E6C) is bound. Their legacy
meshes (the elevator platform, the panel, the item and canopy instances, the record-20 prop) are retired.

- **verified-unbound -> live: 001C4820** (em_sul_001C4820 at its node; its unit and its cell uid 17 checked
  against the captures).
- **unverified -> live: 0015AC00** (em_pickup_owner_0015AC00, the map item's state 0 over its record; the
  oracle now executes it: test_pickup_owner_reference's state-0 cases, which also execute 00219550's state 0 and
  found the decomp's NEARMISS C inverting its +0x03 test for the +0x2E rewrite).
- **Notes updated (no status change):** 00827B10, 00159210, 0015AFA0, 00219550, 00823E80, 001C5680 / 001C5760 (the stand-in draw at
  the child's own node; the 0x827E6C copy bound), 001B0FD0, 001B1020, 001B17A0, 001B1B70, 001A2370, 001CAA00,
  001CA990, 001C7420, 00102958.
- **Evidence.** test_level_smoke.py: check_owner_units (the new owners' 001CAA00 units against the snapshots:
  their colour matrix and rig lanes wherever both drew, and in the camera-exact beats 10 and 14 the set of owners
  that ran their +0x4C, byte counts, clip pass and position rows; the items only where the snapshot's +0x01 says
  their 001B17A0 found them visible), check_indicator_children (the terminal's child's slot equals the terminal's
  node 0 on every tick; that node, +0x04 / +0x09 / +0x0C / +0x44 / +0x4C and +0xB0 equal routes 00..03 before the
  ride and 04..14 after it), the elevator phase (the terminal record's +0x04 and +0xB0..+0xB8 row for row over
  route 04, the carry's +0xB4 steps included); test_collision_world_capture.py (the published class-4 list at
  beat 04 now equals the original's exactly: the prop's uid 17 and the canopy's uid 3 joined it);
  tools/test_owner_draw_reference.py / test_object_unit_reference.py (these owners' captured units, since the
  object-unit step).

Result: live 644, verified-unbound 84, unverified 4, stand-in 0, missing 1, boundary 451 (was 642 / 85 / 5 / 0 / 1 /
451). Section 2 and the section 3 subsection counts are recomputed from the rows.

### 1.26 Update (2026-09-26, chain C7: the scripted takeover on the stage, 001AF800's own loop, the first contact)

A script owner's frame (the truck trigger 008251E0, the fence door 001BC350, the director 008253F0 and Roger
008237E0 on em_area11_script_host) is the player stage's own takeover, as in the original: the interaction
host holds only a staged token, 0015B130's prelude admits the player (00182B30 returns 0: +4 = 4, +5 = 0,
+6 = 0, +1F0 = 0x41, 00174A50(p, 8.0), 00182D70), each following stage is 0015BA50's +4 = 4 path (00183090
with the face's 001D0C70 when 3B8F = 2, the advance by +1F4) and 0015B530 (001837A0), and 0015B530's 00182DF0
releases the player and ends the token (PLAYER_STAGE_WORKERS.md 2.1). 00182DF0 is translated once
(em_player_stage_00182DF0) and also runs the interaction runtime's release (panel, terminal, items), whose
approximations in the pose host are removed; 00182D40 is translated once (em_player_00182D40). The pool
free's 001AF800 pushes its slots in its own loop (em_roger_actor_001AF800) for every AREA11 binder, as the
original does, instead of calling 001AF890 per slot. The floor service's first contact runs
em_player_first_contact (00187DC0 / 00187EA0), which the floor oracle executes against the originals.

- **verified-unbound -> live: 0015B530, 00182B30, 001837A0, 00182D40.**
- **unverified -> live: 00187DC0.**
- **live -> verified-unbound: 001AF890.** The port no longer calls it in 001AF800's place; its route caller,
  the player's 001CA770 in 001B82D0 sub 4 (beats 10, 11, 13, 14), is the face host's detach. Binding 001CA700
  / 001CA770 on the player's +0x90 (a pool slot for the face) needs the attachment draw 001CB3C0 (missing): a
  nonzero +0x90 reaches it in the player's 001CAA00, which em_owner_draw_live would fault on.
- **Notes updated (no status change):** 0015B130, 00174A50, 00174AB0, 00182D70, 00182DF0 (one translation
  now), 00183090, 001AF800, 001CA700, 001CA770.
- **Evidence.** test_level_smoke.py: the full route and both side runs pass with --require-through; the new
  check_stage_takeover requires +4 = 4 from the admission to 00182DF0's release in routes 07, 09, 10, 11, 13
  and 14 (the tick log's `player` carries +4), and the phases' own comparisons (+5, +1F0, +1F1, clip, clock,
  +2F3, 3B8F) hold row for row; the tick log equals the stand-in build's on all 13,017 ticks apart from +4
  during the takeovers (and the slide's SFX track handle +0x31B, which differs between two runs of the same
  build: the audio thread). test_player_stage_workers_reference (00182DF0: 120 synthetic records, the
  captured player records with +2F3 0 and 1, fail-stop; every reachable instruction of 00182DF0, 00182D40 and
  001C6150 executed; the fetch loop's hooked tail call now returns to $ra); test_player_cinematic_reference
  (the stage's +4 = 4 composition, 0015B530 and 00182DF0 against the original 00183090 / 001C64F0 / 00182DF0
  over bank 0x96, 1,388 stages; +4, +5, +6, +1F0, +C, +20C and 3B8F after the release);
  test_roger_actor_original_reference (001AF800: 18 cases, three mutants); test_player_floor_reference
  (first_contact_section: 00187DC0 / 00187EA0 executed, every argument compared); tests/player_states_host_test.c
  (the staged prelude admitting and refusing). newgame-control 9.599849 (unchanged).

Result: live 648, verified-unbound 81, unverified 3, stand-in 0, missing 1, boundary 451 (was 644 / 84 / 4 / 0 /
1 / 451); 80,132 of the 87,791 non-boundary instructions are live (91.3%). Section 2 and the section 3 subsection counts are recomputed from the rows.

### 1.27 Update (2026-09-27, chain C7: main-loop steps V / W, 001D1EF0, the (3, 1) registrations)

Main-loop step V 001D2300 runs on the render context in every iteration (em_frame_kick through
em_rcl_001D2300, before the port presents the frame): the frame's main list at D_0028F700 + (slot << 14),
the slot's half-pixel offsets, the clear it selects (+0x3A0, or +0x420 under render flag 3, which it then
clears), 001E0DF0 and the NEXT chain; its hardware kick is the renderer's presentation. Step W 001D2580
stores the field, with a field model (one field per iteration, equal to D_00810E80 at step V: the route's
phase). With flag 3 cleared, the status frame's, the task chain's and the load veil's 001D2830(3, 1) run on the
context (UM_001D2830 removed), and 001D1EF0 is bound from the area build on (states 0 and 5, 001ADF00,
001AD4E0; its kick's page is drawn). The point-light pool is the context's +0x210..+0x221F (one storage;
`g.point_lights` removed), and the status pages write 0020DFA0's UI view into the camera pool's D_00810610
(em_status_models' copy removed), so the status frames' 001D1C50 projects it as the original does
(RENDER_CONTEXT.md section 9).

- **verified-unbound -> live: 001D2300, 001D1EF0, 001E0DF0.** 001D21B0 stays verified-unbound: the port
  builds no +0x1D8 list (001C1D00 unbound) and its +0x1E8 / +0x2520 words stay 0, so 001E0DF0 CALLs nothing.
- **boundary -> live: 001D2110, 001D2130, 001D2160, 001D2180, 001D21E0, 001D2580** (section 3.16, from the
  GS/VIF packet-build group) **and 001015A8, 00101810** (section 3.24, from the SDK DMA/VIF/VU1 group): each
  carries a translation in em_frame_kick that runs live, checked by the live oracle (section 4's rule).
- **Notes updated (no status change):** 001D2830 (the (3, 1) registrations bound), 001D7BB0 / 001D7C30 /
  001D7FA0 (the pool on the context), 0020DFA0 (D_00810610 on the camera pool).
- **Evidence.** test_render_context_live_reference (steps V and W after every frame, a status frame with its
  001D2830(3, 1), an opening-style frame, a tear-down frame, V under flag 4 and with flag 0x20 clear; all
  owned bytes equal the ORIGINAL 001D2300 / 001D2580 / 001D1EF0's after each entry, and 00101F08's list the
  native kick's; six mutations fail it); test_level_smoke.py check_render_context over the full route and both
  side runs: +0x98 = 1 - +0x9C on every tick (11,634 in the full run), 5,368 world frames' step V lists equal
  the route snapshots' (without the +0x1D8 CALL), 412 status frames hold the two status captures' flag words
  0x0B / 0x03, fog block, save slot and step V list (the black clear +0x420, the status order) and project the
  UI view those captures hold at +0x2380; test_point_light_reference and test_effect_original_reference
  compare the pool as the context's bytes. newgame-control 9.599849 (unchanged).

Result: live 659, verified-unbound 78, unverified 3, stand-in 0, missing 1, boundary 443 (was 648 / 81 / 3 / 0 /
1 / 451); 80,516 of the 87,968 non-boundary instructions are live (91.5%). Section 2, the section 3 subsection
counts and section 4's two boundary groups are recomputed from the rows.

### 1.28 Update (2026-09-27, chain C8: duplicate translations reduced to one bound owner)

Section 1.22 listed duplicate translations: the byte read 001C6150, 0019F330,
001BC240 / 001BC290, 001D4B50 / 001DA1E0 and 00102948. SHADOW_ACTOR_ROUTE.md
L5 listed the SDK VU0 leaves 001026D0 / 00102900. Each now has one bound
owner, and each reduced copy's oracle coverage runs through that owner.

- **SDK VU0 leaves (001026D0, 00102900, 00102948).**
  - They are now the header-only `em_sdk_vu0.h` (docs/SDK_VU0.md). It needs
    nothing but em_ee_float.h, so linking it drags no pose host into any
    test.
  - Its callers: em_shadow_actor_route, em_locomotion_display (its
    `em_loco_001026D0` export is removed), em_equipment_live,
    em_effect_manager, em_actor_light_001D89D0, em_camera_leftovers,
    em_status_models, em_frame_render_heads, em_shadow_original, and every
    host quadword copy whose comment cites 00102948 (SDK_VU0.md lists them).
  - New oracle: tools/test_sdk_vu0_reference.py executes all three
    originals.
  - Two copies remain because their oracles are not on the measured model:
    em_crate_original's multiply (its oracle's SDK semantics give a denormal
    where VU0 gives 0) and em_snow's inline tile-colour scale. SDK_VU0.md
    "Not reduced" has both.
- **001C6150.** em_status_models' and em_indicator_bind_live's w_001C6150
  now call em_owner_services_001C6150, as em_equipment_live's does.
  em_roger_actor_original's read over Roger's resource bytes stays (see its
  row).
- **0019F330 (verified-unbound -> live).**
  - em_collision.c's `column_node` is removed.
  - 0019BC40 pass 2 calls the column math's `cross` worker. em_collision_world
    binds it to `em_coll_list_passes_0019F330` over the world's grid, SDK
    context and one scratchpad state (COLL_LIST_PASSES.md item 3).
  - A private instrumented build over the full smoke counted 65,478 calls and
    103 crossings.
  - test_actor_collision_reference and test_player_climb_reference now run
    the original 0019BC40 with 0019F330 and its SDK calls unhooked. The climb
    sample previously hooked sqrt / atan to the host libm.
- **001BC240 / 001BC290.** The unbound em_sdf_ copies are removed.
  em_door_original's phases 4 and 5 are the one translation. Their cases
  moved to test_door_original_reference: every flag answer, request byte and
  armed byte against the original 001BC350, and the route 09 phase-4/5
  replay (66 frames: one commit, one restart on the capture's row, +0x0B).
- **001D4B50 / 001DA1E0 (and 001DA290).** The unbound em_rvr_ packet
  builders are removed. The shadow passes (em_shadow_original's worker calls
  drawn by em_gfx_shadow_*) are the one owner.
  - test_shadow_original_reference F compares their GS state with the
    captured chains.
  - test_render_verify_rest_reference asserts the original's 001DA290 /
    001DA1E0 arguments, and that 001D4B50 is called exactly for the class-2
    receivers.
- **Tests.** No test was retired. These moved to the surviving owner:
  - test_script_door_fan_reference's 001BC240 / 001BC290 cases and route 09
    replay, to test_door_original_reference;
  - test_locomotion_display_reference's 001026D0 leaf sweep, to
    test_sdk_vu0_reference;
  - test_render_verify_rest_reference's native record comparisons of the
    removed builders, to test_shadow_original_reference's drawn-state
    comparison.
- **Evidence.**
  - All make test-* targets pass, including the new test-sdk-vu0-reference.
  - make test-level-smoke-full passes with --require-through, including
    check_shadow on the measured-model em_shadow_original.
  - newgame-control gives 9.599849 (unchanged).
  - EM_TEST_FULL=1 test_shadow_original_reference passes (6,144 area keys,
    192 clip batches, 15 route beats).

Result: live 660, verified-unbound 77, unverified 3, stand-in 0, missing 1,
boundary 443 (was 659 / 78 / 3 / 0 / 1 / 443). 80,726 of the 87,968
non-boundary instructions are live (91.8%). Section 2 and the section 3
subsection counts are recomputed from the rows.

### 1.29 Update (2026-09-27, chain C8: EE-float harmonization of the older oracles)

The older oracle interpreters computed COP1 with host floats (no add/sub
pre-trim, no DAZ/FTZ/saturation, MSUB as product - ACC in two of them,
truncated DIV.S or round-to-nearest CVT.S.W in others) and VU0 with a
truncated host double. A translation verified only against them could carry
the one-ULP error found in 001BBE40. Every interpreter now runs COP1 through
`tools/ee_cop1.py` and VU0 through `ee_float_model.vu_lane` (EE_FLOAT_MODEL.md
section 5a lists every oracle and its model), and every translation that then
differed was moved onto `em_ee_float.h` (section 5b there).

- **No status changes.** Every route row touched was already live; only its
  evidence changed. The totals below are recounted from the section 3 rows.
- **Notes updated (live, now verified on the measured model):**
  - 001D7C30 (em_point_light: its COP1 sites through em_ee_*, the flicker
    matrix through em_owner_services' 001029C0 / 00102B08 / 00102BB0);
  - 0018CBD0 (em_camera_retarget) and the 00102C58 / 001026A0 behind
    em_camera_rotation (now the owner-services and em_effect_original
    translations);
  - 001E55F0 (em_weather: the strength quotient rounds to nearest) and
    001E67C0 (em_snow: its own arithmetic, and the tile colour through
    em_sdk_vu0_00102900; the captured tiles still equal);
  - 001D0720 / 001D0C70 (em_opening_face: COP1 on the model, was host
    round-to-nearest);
  - 00183EF0, 001B1630 and 001B1470 (em_interaction_scan, em_door_candidate,
    em_roger candidate, em_panel candidate, em_item_device);
  - 0011C7B0 / 0011CB90 / 0011CCC8 / 0011D770 and 0011C4C8 / 0011DBB8
    (em_item_sdk_math, em_interaction_scan's atan: they now equal the
    original on all 360 level-script sine values, test_area_script_reference
    asserts it; they were 257 misses);
  - 001B7F90 / 001B1240 / 001B12B0 / 001B8FC0 (em_pickup_motion);
  - 0022EEF0 (em_cinematic_camera and em_cinematic_playback; the opening
    capture's eye and target bytes and the Roger capture's up / zoom bytes
    still equal);
  - 001551B0 / 00156620 (the crates' and drums' SDK calls now go to the one
    bound translation of 001029C0, 00102A60 / 00102B08 / 00102BB0, 00102C58,
    00102918, 001026D0, 001026A0, 00102738, 001B1470 and 001281C0);
  - 00102738 (one translation, em_sdk_vu0.h; ten private copies and the
    crate's host-float one reduced);
  - 0021B550 (em_load_veil), 001B6F00 (em_interaction_alignment),
    001DD980 (em_interaction_projection), 008235F0 (em_area11_effect),
    0015AE20 (em_pickup_owner), 001B9BA0 (em_panel_program), 0017B910 /
    0017C030 (em_player_foot_stop), 001760C0 / 0019AB20 (em_player's probe
    glue, gated), 001B1EA0 (em_director_original's MULA / MADD / MSUB),
    001A4D10 / 001A50A0 (em_collision's second compact-face translation).
- **Evidence.**
  - All make test-* targets pass. The point-light, camera-retarget,
    cinematic, snow-tile (`--reference-ee/--reference-tiles`), crate and
    trail oracles still match their captures.
  - make test-level-smoke-full passes with --require-through.
  - newgame-control gives 9.599849 (unchanged).
  - EM_TEST_FULL=1 test_item_sdk_math_reference (6,418 sin/cos results
    exact) and test_snow_tiles_reference pass.
- **Left (EE_FLOAT_MODEL.md section 5c):** VU0 per-lane helpers on a
  truncated host double (exact except a tiny opposite-sign addend), the
  duplicate SDK math in em_item_sdk_math / em_interaction_scan, em_snow's
  host sinf wave, em_lighting (L40), em_status_draw's battery ramp.

Result: live 660, verified-unbound 77, unverified 3, stand-in 0, missing 1,
boundary 443 (unchanged). 80,726 of the 87,968 non-boundary instructions
are live (91.8%).

### 1.30 Update (2026-09-27, the stream drive's timing from the C7 capture)

The IOP stream backend's drive ran a zero-latency model. It now runs the model measured in the decomp's C7 stream
capture (CAPTURES_C7.md section 1; IOP_STREAM.md "Drive model"): one read at a time, the position after the last
read, and a seek of 0, 2 or 6 fields by the distance class. The model equals 186 of the 205 captured reads. The
other 19 are one field off: the sub-field poll phase, which no capture records.

- **No status changes.** The drive functions (00112610, 00112D18, 00113280, 00113478) are section 4 boundary rows
  (the IOP/disc boundary, lane L36). Their substitute's timing is now compared with the original's by
  test_iop_stream_reference, against every read of the C7 stream capture. The totals below are recounted from the
  section 3 rows and are unchanged.
- **Notes updated.**
  - 008253F0 (the director): check_director_beat's free early-teardown allowance is removed. check_voice_drive
    compares the first voice lane with the capture. 0x97 and 0x99 tear down on the capture's rows. 0x7F tears down
    2 rows early, because the original's sequencer served a lane-0 music refill first (navigation).
  - Lanes L21 and L36.
- **Evidence.**
  - make test-level-smoke-full with --require-through.
  - test-iop-stream: the drive-model check and the contract test. The co-simulation cursor is 46/46, where it was
    45/46.
  - newgame-control 9.599849. First control is 6 frames later.
  - All make test-* pass.
- **Left.** The opening's prefill waits 15 fields in the original for the area music's read, which 0x1AE040's
  unbound area-entry 001FAE70(1) would issue (the RNG order audit). That read's 16-field seek from the intro
  movie's position has no mechanism the capture shows.

Result: live 660, verified-unbound 77, unverified 3, stand-in 0, missing 1, boundary 443 (unchanged). 80,726 of the
87,968 non-boundary instructions are live (91.8%).

### 1.31 Update (2026-09-27, the fence door's side 1: the arrival walk-out)

The fence door's side 1 now runs on originals from the Use scan to control
(DOOR_ORIGINAL.md "Side 1"). The level smoke's new `fence_door_side1` phase
equals the decomp's C7 DOOR1 capture row for row (LEVEL_SMOKE.md).

- **No status changes.** The census labels are the route beats 00..14 and
  the startup labels, and none of them enters +4 = 5. So 0015B610, 00183250
  and 00183240 are not census rows. They are now live and checked by
  test_player_floor_reference (the executed originals) and by the smoke's
  side-1 phase (the capture). The census rows the side-1 capture also runs
  (001BBE40, 001BC150, 001B07C0, the room move) were already live.
- **Stand-ins retired (none were census rows):**
  - the AREA11 hook of em_door.c's legacy walk-out
    (`em_door_legacy_walkout_tick`);
  - `em_door_room_move_arrival`;
  - `em_door_movement_stage_release`.
- **Renderer.** The shadow backend's unprojection
  (`em_shadow_gs_clip_unproject`, SHADOW_ORIGINAL.md) now searches the
  binary32 neighbours of a rounded point that misses its GS pixel. Before
  this, the port quit at the side-1 stance, where a receiver vertex at
  w 0.24 missed by 0.08 pixel.
- **Evidence.**
  - make test-level-smoke-full with --require-through (the side-9 run now
    requires fence_door and fence_door_side1).
  - test-player-floor-reference.
  - newgame-control 9.599849.
  - compare_frame_order PASS: idle04 / walk04 / st03 at native index 1336,
    cut02 / cut15.
  - All make test-* pass.

Result: live 660, verified-unbound 77, unverified 3, stand-in 0, missing 1,
boundary 443, recounted from the section 3 rows (741 rows) and unchanged.
80,726 of the 87,968 non-boundary instructions are live (91.8%).

### 1.32 Update (2026-09-27, the rand() order audit)

The port's rand() calls (`EM_RAND_TRACE`, resolved by tools/rand_order.py)
are compared with the decomp's C7 per-call capture (RAND_ORDER.md).

- **Found.**
  - From the area entry the port equals the original call for call for 4
    calls.
  - The first difference is 00825940's lifecycle-0 draw at AE+1, which the
    port misses (L24; the security gun, bound since section 1.39, after
    which the first difference is the player face's AE+5 draw).
  - Every frame's fixed-schedule callers equal the original's over the
    opening, the 30 frames after first control and the smoke's aligned
    windows of routes 01 and 10.
  - The opening's faces come from em_opening_actor (design risk 2).
- **Bound.** 0x1AE040's state-0 area-entry 001FAE70(1) and the state-4 room
  move's 001FAE70(0) (row 001FAE70). The opening's prefill now waits for the
  area music's read, and first control comes 4 frames later.
- **Now compared in the smoke:**
  - the whole lighting rows over the port's own point-light pool
    (check_owner_units);
  - 001D7C30 (check_sway);
  - 001F4D40's colour (check_marker_colour);
  - 001E2560's rand()-driven fields (check_head_sprites).
- **No status changes.** Notes and evidence were updated on the rows
  001FAE70, 00122BB8, 001D7C30, 001D0720, 001E2560, 001F4D40 and 00825940.
- **Evidence.**
  - make test-rand-order.
  - make test-level-smoke-full with --require-through.
  - newgame-control 9.599849.
  - compare_frame_order PASS at native index 1340.
  - All make test-* pass.

Result: live 660, verified-unbound 77, unverified 3, stand-in 0, missing 1,
boundary 443, recounted from the section 3 rows (741 rows) and unchanged.
80,726 of the 87,968 non-boundary instructions are live (91.8%).

### 1.33 Recount (2026-09-27, full-route smoke to Roger: liveness measured again over the whole route)

Sections 1.23..1.32 moved rows by hand, each with its own evidence. This
recount measures every row again over the whole live route at port HEAD
6da4eb5, with the method of section 1.22.

- **Edge recorder.** A private `-O1 -fno-inline -finstrument-functions`
  build of the Makefile's link line (216 sources) was made with a caller /
  callee edge recorder. Both are scratch only and deleted. It ran five
  times:
  - the full level smoke through `roger` (13,039 ticks; all 18 main-line
    phases PASS, and the checker re-run on its tick log PASSES with
    `--require-through last`);
  - the side beat 00 run (PASS);
  - the side beat 09 run with the fence door's side 1 (PASS);
  - `newgame-control` (PASS, 9.599849);
  - `EM_STARTUP_TEST=newgame-control EM_AREA_CHANGE_TEST=1` (PASS).

  Checker note: the rand() trace of the instrumented run cannot be checked,
  because tools/rand_order.py resolves callers with the shipping binary's
  symbols. check_rand_order passes on the shipping build's own full run.
- **Live.** A native function is live when it is reachable from `main`,
  the audio thread or a constructor, with the test-only modules cut: 7,134
  of the 7,255 native functions that ran.
- **Row to native function.** As in 1.22: each row's candidates are the
  native functions whose name or preceding comment block cites the address.
  Since the one-owner step (1.28), the SDK VU0 leaves are header-only
  (em_sdk_vu0.h). So a candidate defined in a header counts as live when a
  function of that name ran live in any unit.
- **Live rows confirmed: 660 of 660.**
  - 634 have a candidate that ran on the live path.
  - The other 26 run inline in a live function of their named module, each
    read by hand:
    - the title menu (em_startup_tick, title_draw);
    - the panel program's op-9 callback and op 9 (em_panel_program_tick);
    - em_pickup_owner_tick;
    - the ladder climb state (em_player_ladder_climb_state);
    - em_actor_class_publish_001B1B70's class-7 push;
    - em_interaction_alignment;
    - em_area_script_tick's op01 kind 9;
    - em_door_original_tick's phases 4 and 5 (001BC240 / 001BC290);
    - em_cinematic_playback (001C7C00);
    - Roger's init and em_roger_tick;
    - em_owner_draw_001CA940's thunks (001D38F0 / 001D3C30);
    - the shadow passes (em_gfx_shadow_receiver / em_gfx_shadow_alpha_clear
      for 001D4B50 / 001DA1E0; em_shadow_original_001DA6A0 for 001DA080);
    - em_item_root_tick (0020EE50);
    - the director's beat bodies in em_director_original_tick;
    - sdk_0011E080.
- **Non-live rows.** No non-live row has its own translation running live.
  - 47 rows have a live candidate, but every one is one of these:
    - a stand-in or wrapper whose comment cites the address (for example
      em_frame's frame_input_read for 001B57E0 / 001B5F40, em_hud's area
      title for 001C5860 / 001C5930, char_rig_build for 001D8270);
    - a function whose name carries the address and which the row already
      names as its stand-in or reported binding: w_001AFCA0,
      em_render_001C1D00, w_001CA770 (the face host's detach), the no-op
      w_001CB5B0 slots, um_001D19E0, g_001D52E0 (UM_001D52E0),
      w_001FBC50 (em_sfx_stop_all), em_scene_bindings_001FC280
      (unverified), veil_0021B1B0 / veil_0021B500 (reported) and
      tick_enemy_00825940 (the legacy group of the security gun and its
      cable, then called the husks; retired in section 1.39).
- **Boundary rows with a live translation** (section 4's rule). Thirteen
  boundary functions have a translation named after them that ran live:
  - the IOP stream driver: 00112610, 00112D18, 00113280, 001157F0,
    0011A2B0;
  - the stream command packers: 0011A4E8, 0011A608, 0011A658, 00119828;
  - the lane init 001F9820;
  - the module loader's 001FF080 and 00200890;
  - the pad actuator's 00111018.

  They stay boundaries: they are translations inside the non-render
  boundaries, which section 4's note keeps, and moving them is lane L36's
  decision (VOICE, section 1.15).
- **The smoke's invocations** (LEVEL_SMOKE.md "Supported end phases"). Every
  end phase from `battery` on passes the make target. Runs that end at
  `first_control` or `status` fail check_render_context's 100-gameplay-tick
  minimum, which stays, so the doc no longer lists them.
- **Evidence.** make test-level-smoke-full passes with --require-through at
  HEAD; all 237 make test-* targets pass; make all builds with no warnings;
  newgame-control gives 9.599849.

Result: live 660, verified-unbound 77, unverified 3, stand-in 0, missing 1,
boundary 443 (unchanged). 80,726 of the 87,968 non-boundary instructions are
live (91.8%). What remains between the port and the original first level is
the prioritized list in FIRST_LEVEL_AUDIT.md section 1b.

### 1.34 Update (2026-09-27, the PS2 disc-drive timing behind a launcher switch)

The drive model of section 1.30 now runs only with the PS2 disc-drive timing
switch on (`src/em_settings.h`, `EM_PS2_DISC_DRIVE_TIMING=1`;
LAUNCHER_OPTIONS.md BUILT). By default the stream reads are served at host
speed: each is done at the first query after its issue (IOP_STREAM.md "Host
speed and the PS2 disc-drive timing switch").

- **No status changes.** The drive functions (00112610, 00112D18, 00113280,
  00113478) stay section 4 boundary rows (lane L36); only their
  substitute's timing has two modes. 001FA0D0, 001F9CF0 and the rest of the
  lanes are unchanged and still live.
- **Evidence.**
  - make test-level-smoke-full (host speed) and make
    test-level-smoke-ps2-drive (the switch on) with --require-through; the
    side runs in both modes. check_voice_drive and the opening's end follow
    the run's mode (LEVEL_SMOKE.md "The stream drive's two modes").
  - make test-rand-order in both modes (first control exactly 21 frames
    before the original's at host speed; 11 with the switch on).
  - make test-iop-stream: tests/iop_stream_test.c covers both modes; the
    reference co-simulation runs with the switch on (cursor 46/46 in full
    mode).
  - newgame-control 9.599849 in both modes (locked_ticks 1301 / 1311).
  - compare_frame_order PASS at native index 1330 (host) and 1340 (switch
    on).
  - 237 of 238 make test-* pass; test-scene-no-shadow fails only on the
    B15 file em_status_pages_item.c (commit 8655157).

Result: live 660, verified-unbound 77, unverified 3, stand-in 0, missing 1,
boundary 443, recounted from the section 3 rows (741 rows) and unchanged.
80,726 of the 87,968 non-boundary instructions are live (91.8%).

### 1.35 Update (2026-09-27, the load veil live)

The load veil's draw 0021B1B0 and phase step 0021B500 are bound at their
original caller 0021B550 (em_scene_bindings: em_rcl_0021B1B0 over the
render context; em_load_veil_particles_0021B500), and step V's list, which
sends the veil's channel-0 packets, is drawn by the GS frame stage
(em_load_veil_live, em_chain_page's list mode, em_gfx_gs_frame;
LOAD_VEIL_PARTICLES.md section 3). The boot builder's GS blocks are built
whole (em_gs_blocks_original: 001D0F20's banks, 00101898, 00101630,
001008C0; the last three and 001D0F20 have no rows: boot or boundary).

- **Status changes (verified-unbound -> live, six rows):** 0021B1B0,
  0021B500 (3.22), 001D63B0, 001D6B60, 001D7080 (3.16) and 001DFA40
  (3.17). Each runs live on every load (0021B550 in state 2 sub-state 0 at
  host speed; its 001DFA40, 001D6B60, 001D63B0 and 001D7080 inside
  0021B1B0). 001D7080's other route callers (the background 001E1E60 and the
  static world) are their own rows and stay as they were.
- **Evidence.**
  - The level smoke (`check_load_veil`, every run): the live run of the New
    Game load's veil frame is byte-equal to the ORIGINAL 0021B1B0 executed
    at the same call (the tick's block, slot and cursor; the strips' ST lane
    3 excepted), its seed equals the port's and all 15 route captures', and
    the ORIGINAL 0021B500 steps the phase to the port's. make
    test-level-smoke-full passes with --require-through.
  - make test-load-veil-particles-reference (unchanged), make
    test-area-load-reference (its chain replay now executes 0021B500 and
    0021B1B0), make test-gs-blocks-reference (the executed 001D0F20 and 18
    captures), make test-load-veil-gpu, make test-chain-page (list mode).
  - newgame-control 9.599849 (unchanged).
- **What the rows do not claim.** At host speed the port's area read ends
  inside 001FF080(1, 0), so the veil draws one frame per load at level 0
  (black) where the PS2 drew 258 (LOAD_VEIL_PARTICLES.md section 5).

Result: live 666, verified-unbound 71, unverified 3, stand-in 0, missing 1,
boundary 443, recounted from the section 3 rows (741 rows). 81,298 of the
87,968 non-boundary instructions are live (92.4%).

### 1.36 Update (2026-09-27, chain C8b: the status pages live)

The status hub's DATABASE (00214020) and SPR4 (00211970, with its selector
and five part pages) and the ITEM children EQUIPMENT (00214570), EVENT
(00215870) and HEALING (002160B0) run live through `em_status_pages_live`,
with their page-module textures decoded from the GS memory the original
holds (`em_gs_texture`, `tools/export_status_pages.py`); the page core
follows 0020CDC0 case 0's whole request map; the callee gaps are
translated (0020D930 mode 2 in the one owner `em_menu_hover`, 001C47E0,
0015C700 / 0015C7C0) or proven unreachable in AREA11 (0015C750, 00185420's
non-battery kinds, 00182B30; STATUS_PAGES.md section 7).

- **No status change.** None of these functions is a census row: the
  recorded route never opens a status page (the census records what the
  route beats ran). The 0x0020CDC0 row's limitation is now only the MAP
  page 0020F950 (not bound) and the unreachable branches; the 0x001C5FB0
  row names its one translation (em_status_draw_001C5FB0, which the hub's
  infection field now uses too).
- **Evidence.** The level smoke's designed side run `status_pages`: 1,143
  page calls replayed through the original instructions over the
  status-hub capture, 12,636 callee entries and every view byte equal
  (`tools/test_status_pages_live.py`); make test-status-page-reference,
  test-status-pages-reference, test-menu-hover-reference,
  test-pickup-items-reference, test-player-heal-reference,
  test-gs-texture-reference, test-area11-interaction-host.

Result: live 666, verified-unbound 71, unverified 3, stand-in 0, missing 1,
boundary 443 (unchanged), recounted from the section 3 rows (741 rows).

### 1.37 Update (2026-09-27, chain C8b's fix round: the MAP page live)

The MAP page 0020F950 runs live (STATUS_PAGES.md section 7, "MAP"): its
22 UI-pool nodes run 002101C0 in `em_status_models`' pool (the record is
now the original's byte layout), the map models come from the disc's
module 0x1E bank D_0028A570 (`tools/export_status_map.py`; the relocated
word from the EMSP), 001CB480 draws them lit by 001D89D0 in mode 2
(`em_owner_draw_live_light`), and 00210F30's marker and 00211400's item
markers draw on the map. The marker gates' event bytes D_0081077F, 782,
784, 789 and 78C are migrated progress bytes.

- **No status change.** 0020F950, 002101C0, 00210030, 00210A00, 00210C00,
  00210F30, 00211400, 00208040 and 001CB480 are not census rows (the
  recorded route never opens MAP). The hub's rows the MAP nodes also reach
  (001AFF10, 001AFF90, 001B0000, 001C6120, 001C6150, 001AF7C0, 001CB5B0,
  001C62C0, 001C6380, 001D89D0) keep their statuses. The 0x0020CDC0 row's
  limitation no longer names MAP.
- **Evidence.** The level smoke's designed side run `status_pages`: 6,643
  page and node calls (242 MAP, 5,258 of its nodes) replayed through the
  original instructions over the status-hub capture, 19,660 callee entries
  and every view byte equal (`tools/test_status_pages_live.py`); make
  test-status-map-reference (the original 001C6120 / 001C6150 over the disc
  bank at the relocated D_0028A570); make test-area11-interaction-host (the
  map take and hub hover 3 open MAP).

Result: live 666, verified-unbound 71, unverified 3, stand-in 0, missing 1,
boundary 443 (unchanged; no section 3 row changed status).

### 1.38 Update (2026-09-28, chain C8b LOADER: the module loader live for module 0x21)

The screen-module loader runs its own steps for module 0x21 (the BATTERY
page; MODULE_LOADER.md): one `em_module_loader` booted by em_scene_bindings
over the user's exported sectors, its slot-2 task 001FF0D0 dispatched after
the game task, 001FF830 / 001FF3F0 with 00200780 / 00200730 / 00200830 on
the host drive, D_00275BD8 unified in the scene state, the slot table
D_0028A490 modelled whole with the cursors as its slots. The page core's
exit calls the one 001FEF70. 001FFCD0 and 001FF590 (the area streamer) are
translated and verified but not bound (the sound-bank step, H7).

- **Status changes (verified-unbound -> live, two rows):** 001AB7D0 (3.1:
  the loader's 0x63 step on em_task slot 2, at the end of every module-0x21
  load) and 001FEF70 (3.20: em_status_page's exit now calls
  em_status_scene_bank_001FEF70; its duplicate is deleted).
- **Boundary rows** (section 4, "module loader and disc read"): 001FF080,
  001FF0D0, 001FF3F0, 001FF830, 00200730, 00200780 and 00200830 run live
  for module 0x21 with their oracles; 001FFCD0 and 001FF590 carry verified
  translations that are not bound; 00200890 and 00200970 stay
  verified-unbound translations; 002009E0 has none. They stay boundary rows
  (section 4 note: moving the loader into section 3 is a lead decision), so
  the totals do not count them.
- **Evidence.** The level smoke's `check_module_load` (routes 01 and 03,
  every run through the battery and the panel; with the PS2 disc-drive
  timing switch in `make test-level-smoke-ps2-drive`); make
  test-module-loader-reference, test-status-scene-reference (001FFCD0 /
  001FF590 and the widened table), test-status-page-reference (the exit's
  001FEF70), test-status-runtime and test-area11-interaction-host (the
  fixtures run the loader); newgame-control 9.599849 (unchanged).

Result: live 668, verified-unbound 69, unverified 3, stand-in 0, missing 1,
boundary 443, recounted from the section 3 rows (741 rows). 81,330 of the
87,968 non-boundary instructions are live (92.5%).

### 1.39 Update (2026-09-28, census L24: the security gun, its cable and the fan pair on their original owners)

The AREA11 "husk creature" 00825940 is a fixed security gun and the "husk
partner" 00827490 its power cable (decomp verify-area11-husks; docs
SECURITY_GUN.md); the labels are renamed in the port (em_security_gun,
em_security_gun_rest, the lane L24). em_area11_bindings.c runs the gun
(em_gun_tick), the cable (em_gun_cable_tick) and the fan pair
(em_fan_original_tick) on their records, and all four draw their 001CAA00
units. The legacy em_enemy group, the interim lamp spawn and em_pickup's
static fan draw are retired.

- **Status changes (verified-unbound -> live, three rows, 3.23):** 00825940
  (lifecycles 0 and 0x64 reached; 4 and 1 fault by design, return visit),
  00827490 (lifecycles 0 and 1 reached; its hit is unreached: no live +0x36
  writer, and faults at 001EFE00) and 00827630.
- **Row notes changed, no status change:** 001B11E0 (exported for the
  cable), 001B1190 (the verified em_gun_rest_001B1190 bound for the cable's
  lifecycle 2, unreached; the items still use em_pickup's taken_set:
  unverified), 001A2370 (the gun's plate, uid 15), 001B0FD0, 001B17A0,
  001C7420, 001CAA00 (the three owners), 001C5680 (the gun's own lamp
  spawn) and 00823CE0 (renamed; still a no-code node).
- **Evidence.** The level smoke's check_gun_fan (the gun and the cable
  equal all 15 route snapshots on every tick after their setup; every
  captured fan state is on the port's cycle), check_owner_units (their
  units against the original 001CAA00; the fans over the port's +0xC8
  through the original 001C6380), check_rand_order and `make
  test-rand-order` (the gun's 0x8259F0 draw is the original's AE+1 call:
  126 calls equal, was 4), `make test-collision-world-capture` (uid 15
  equals the original in captures 00 and 04), make
  test-script-door-fan-reference, test-security-gun-rest-reference (new
  target) and test-fan-original-reference; newgame-control 9.599849
  (unchanged); compare_frame_order idle04 / walk04 / st03 (--native-index
  1330), cut02 and cut15 PASS.

Result: live 671, verified-unbound 66, unverified 3, stand-in 0, missing 1,
boundary 443, recounted from the section 3 rows (741 rows). 83,150 of the
87,968 non-boundary instructions are live (94.5%).

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
| live | 671 | 83,150 | 621 (79,253) | 50 (3,897) |
| verified-unbound | 66 | 4,645 | 38 (2,857) | 28 (1,788) |
| unverified | 3 | 127 | 3 (127) | 0 (0) |
| stand-in | 0 | 0 | 0 (0) | 0 (0) |
| missing | 1 | 46 | 1 (46) | 0 (0) |
| boundary | 443 | 23,796 | 165 (10,496) | 278 (13,300) |
| **total** | **1184** | **111,764** | 828 | 356 |

Of the 741 non-boundary functions, 671 (90.6%) are live and verified; by instructions 83,150 of 87,968 (94.5%). One of them, 0015BCF0, is live only in part (its tail, its animate step and 00187350); the row says so. A further 66 functions (4,645 instructions, 5.3%) are verified translations the live app does not run. Only 4 functions (173 instructions) have no verified translation on the live path: no stand-in is left (001FCB90, 0020CCB0 and 0021BAE0 are live since the status UI step, section 1.21), 3 unverified (0015CF90, 001B1190 and, since WP-8b, 001FC280, section 1.15; 00187DC0 is live since chain C7, section 1.26) and 1 missing (001CB3C0, corrected by the object-unit step, section 1.19). The totals, the per-label table below and the section 3 subsection counts are computed from the section 3 rows (recount 2026-09-26, sections 1.22, 1.23, 1.25 and 1.26; 2026-09-27, sections 1.27, 1.28, 1.29, 1.30, 1.31, 1.32, 1.33 and 1.35; 2026-09-28, sections 1.38 and 1.39) with each function's instruction count and labels from `route_functions.json`; the method reproduces the 1.14 numbers exactly when fed its statuses.

### 2.2 Per route label

"Ran" counts every function the label executed; "first" counts the functions first seen in that label in route order (beat 00 is placed before 01).

| Label | Ran: live / verified-unbound / unverified / stand-in / missing / boundary | First seen here: live / v-u / unv / stand-in / missing / boundary |
|---|---|---|
| S0_title | 77 / 12 / 0 / 0 / 0 / 383 | 77 / 12 / 0 / 0 / 0 / 383 |
| S1_newgame_load | 164 / 28 / 1 / 0 / 0 / 86 | 99 / 22 / 1 / 0 / 0 / 18 |
| S2_opening | 419 / 34 / 2 / 0 / 1 / 124 | 302 / 27 / 1 / 0 / 1 / 32 |
| S3_first_control_idle | 324 / 29 / 1 / 0 / 1 / 116 | 11 / 1 / 0 / 0 / 0 / 0 |
| 00_panel_no_battery | 394 / 26 / 1 / 0 / 1 / 96 | 32 / 0 / 0 / 0 / 0 / 0 |
| 01_battery | 439 / 27 / 3 / 0 / 1 / 153 | 30 / 1 / 1 / 0 / 0 / 5 |
| 02_elevator_refusal | 409 / 26 / 1 / 0 / 1 / 98 | 15 / 0 / 0 / 0 / 0 / 2 |
| 03_panel_power | 474 / 29 / 2 / 0 / 1 / 124 | 20 / 1 / 0 / 0 / 0 / 1 |
| 04_elevator_ride | 401 / 26 / 1 / 0 / 1 / 131 | 2 / 0 / 0 / 0 / 0 / 2 |
| 05_boxes | 402 / 25 / 1 / 0 / 1 / 93 | 23 / 0 / 0 / 0 / 0 / 0 |
| 06_hill_slide | 366 / 25 / 1 / 0 / 1 / 96 | 12 / 0 / 0 / 0 / 0 / 0 |
| 07_truck_preview | 377 / 28 / 1 / 0 / 1 / 114 | 0 / 1 / 0 / 0 / 0 / 0 |
| 08_truck_crossing | 365 / 27 / 1 / 0 / 1 / 129 | 2 / 0 / 0 / 0 / 0 / 0 |
| 09_fence_door | 419 / 29 / 2 / 0 / 1 / 104 | 8 / 1 / 0 / 0 / 0 / 0 |
| 10_cage_roof_roger | 454 / 32 / 1 / 0 / 1 / 139 | 28 / 0 / 0 / 0 / 0 / 0 |
| 11_crevice_prompt | 450 / 31 / 1 / 0 / 1 / 136 | 2 / 0 / 0 / 0 / 0 / 0 |
| 12_crevice_jump | 382 / 28 / 1 / 0 / 1 / 92 | 6 / 0 / 0 / 0 / 0 / 0 |
| 13_east_tower | 424 / 30 / 1 / 0 / 1 / 137 | 0 / 0 / 0 / 0 / 0 / 0 |
| 14_roger_encounter | 447 / 33 / 2 / 0 / 1 / 104 | 2 / 0 / 0 / 0 / 0 / 0 |

### 2.3 What the numbers say

State at the full-route recount of 2026-09-27 (section 1.33, which re-measured every row's liveness over the whole route; the earlier measurement is section 1.22), with the row changes of sections 1.34..1.39. What remains, prioritized, is FIRST_LEVEL_AUDIT.md section 1b.

- **Live and verified: 671 of 741 non-boundary functions (94.5% by instructions).** Every main-line route phase the
  level smoke plays reproduces its capture (LEVEL_SMOKE.md): first control, the status screen, the battery, the
  refusal, the panel, the elevator, the boxes, the slide, the truck preview and crossing, the ladders, the director's
  three beats with Roger's voiced conversation (since WP-8b; since section 1.34 the lines 0x97 / 0x99 tear down
  exactly the drive's 6 rows early at host speed, the default, and on the capture's rows with the PS2 disc-drive
  timing switch; 0x7F 2 rows earlier still, the music refill's phase), the tank and pipe climbs, the crevice jump, the east-tower climb and Roger's encounter; the side beats
  00 (the panel without the battery) and 09 (the fence door and its room move, since section 1.18; with the door's
  side 1 and its arrival walk-out since section 1.31), each in its own run. Since section 1.20 the player's drop shadow runs on its originals from first control on (the projected
  shadow 001DA6A0 and, on an actor, the 0015BF90 decal), checked against the original re-executed over the port's
  own inputs (check_shadow). Since section 1.21 the status pages run on their originals too: the BATTERY page
  002149F0 with its page draws, the mode-3 / mode-4 presenters at step F (the step-F gate is gone), the fog record's
  save and restore around every status screen, and the indicator children's bind and placement. Since section 1.23
  the player and its seven equipment nodes draw their original 001CAA00 units (the player's model bound by
  0015C1F0 on a record 001AF5C0 wiped), equal to the original's in the camera-exact snapshots 10 and 14. Since
  section 1.25 the terminal, the panel, the prop 001C4820, the items and the opening controller's canopy draw their
  original units over their own records too, and the terminal copies its node into its indicator child's slot
  (0x827E6C) as the original does. Since section 1.39 the security gun, its cable and the fan pair run their
  original owners and draw their original units (the fans' spin cycle; the dormant gun's one setup rand() draw is
  the original's AE+1 call). Since section 1.27 the main loop's steps V and W run their originals (the frame's
  DMA list with its clear, 001E0DF0, the field), with the status frames' and the tear-down frames' render flag 3
  and the UI view of the status pages on the one storage.
- **What still stands in on the route:** the camera stand-ins that pre-empt action 0 for the examine and the aim
  (L28); the indicator children's +0x4C draw 001CABA0 (the child's model mesh drawn additively at the child's own
  node, section 1.25; the security gun's 0x7A lamp draws nothing); on the chain page (drawn since section 1.24), 001DDE10's four-sprite pass, which is
  walked over, and the AREA11 flame and the snow, which draw outside the page (008235F0's 001D04B0 and the weather's
  001E0D70 kick are not bound);
  001C1D00 (em_render_001C1D00, the empty render-env step: its 001D5370 needs the static-object bank); 001FC280's body
  (unverified since WP-8b: the D_00282160 cache is not modelled); the post-step during the opening (reported while
  the opening runtime owns the displayed player, design risk 2: the port's player mesh draws the displayed pose and
  carries the equipment models there; from the hand-off on, the player's and the equipment's +0x4C are their
  original units since section 1.23); the player's face slot: 001B82D0 sub 4's 001CA770 (and 001B81D0's
  001CA700) on the player is the face host's detach (attach), not a pool slot at +0x90, until the
  attachment draw 001CB3C0 is bound (section 1.26); the panel's, the terminal's and the items' takeovers
  (the interaction runtime's acquire and per-stage tick over their scripts' animation core; their release
  is the original 00182DF0 since section 1.26). A script owner's takeover is the stage's own since
  section 1.26 (0015B130's prelude, 0015B530, 00182DF0).
- **Verified but not run live: 66 functions (4,645 instructions).** The largest groups are the lighting and unbound
  render heads (section 3.16: 12), the effects' room point-light lists (3.18: 7), the anim runtime leaves (3.14: 6),
  the render context's unbound rows and weather (3.17: 5), the sound-side rows of 3.19 (9: the sound-bank loader,
  001FB100's rest, 001FBC50, 001FC6E0, the positional voice) and the AREA11 overlay rows (3.23: 2: the overlay init
  and the flag-0x30 manager 00823CE0).
- **No verified translation:** 4 functions (173 instructions): 3 unverified rows and 1 missing row (001CB3C0;
  section 2.1); no stand-in row is left.

## 3. Per-subsystem tables

Every non-boundary function, grouped by address range. Columns: address, name (when the decomp has one), decomp status, port status, the translating module and the test that verifies it, the live stand-in or a note, and the first label where the census saw it (a trailing `*` marks functions that run only before first control).

### 3.1 Main-loop tasks, frame machine and fades (0x1AA200..0x1AEFFF)

32 functions, 2,205 instructions: live 27, verified-unbound 5 (recount 2026-09-28, the module loader step).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001AAD00 | — | NM | live | em_collision_world (em_coll_list_passes_001AAD00_hooks, em_actor_class_lists_swap_001AAD00) — test_actor_collision_reference.py, test_coll_list_passes_reference.py | w_001AAD00 in both variants (roster scenes): the nine hooks, then the list block; its interactive list is the Use scan's one store | S2_opening |
| 0x001AB4E0 | — | BM | verified-unbound | em_startup_load_gaps em_slg_001AB4E0 — test_startup_load_gaps_reference | not bound (STARTUP_LOAD_GAPS.md section 4) | S0_title |
| 0x001AB590 | — | BM | verified-unbound | em_startup_load_gaps em_slg_001AB590 — test_startup_load_gaps_reference | a DMA CHCR watchdog: really a hardware boundary (STARTUP_LOAD_GAPS.md section 4 item 10) | S0_title |
| 0x001AB6A0 | — | BM | live | em_task.c em_task_dispatch — test_startup_load_gaps_reference (executes 001AB6A0 against em_task.c) |  | S0_title |
| 0x001AB740 | — | BM | live | em_task.c em_task_register — test_startup_load_gaps_reference | the live New Game registers the 001ACEC0 task through it (em_game_install_new) | S0_title |
| 0x001AB790 | — | BM | verified-unbound | em_task.c em_task_replace_current — test_startup_load_gaps_reference | em_task_replace_current never runs on the live path: the port's New Game registers the 001ACEC0 task with em_task_register (em_game_install_new) where 001AC070 state 4 calls 001AB790 | S0_title* |
| 0x001AB7D0 | — | BM | live | em_status_scene_original (the loader's 0x63 step, `*slot_state = 0` on em_task slot 2) via em_module_loader — test_status_scene_reference.py, test_module_loader_reference.py (the whole loads execute 001AB7D0); test_level_smoke.py check_module_load (slot 2 idle at the load's completion, routes 01 / 03) | runs live at the end of every module-0x21 load (chain C8b LOADER) | S0_title |
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

28 functions, 1,504 instructions: live 25, verified-unbound 3 (recount 2026-09-26, chain C7 takeover step).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001AF2C0 | — | NM | live | em_game.c em_game_new_game_reset_001AF2C0 + em_pickup_reset — test_continue_reset_reference |  | S0_title* |
| 0x001AF470 | — | AW | live | em_startup_load_gaps em_slg_001AF470 — test_startup_load_gaps_reference; test_continue_reset_reference | recount 2026-09-25: em_slg_001AF470 executed on the live path (6 calls over the five measured runs) | S0_title* |
| 0x001AF5C0 | — | BM | live | em_startup_load_gaps em_slg_001AF5C0 over the player record image, in em_scene_bindings.c w_001AFCA0 (the player step, section 1.23; its 001D8BF0 is em_roger_actor_001D8BF0) — test_startup_load_gaps_reference (through 001AFCA0); test_level_smoke.py check_owner_units (the player's unit reads the +0x02, +0x80 and +0x94 it writes) | | S1_newgame_load* |
| 0x001AF690 | — | BM | verified-unbound | em_startup_load_gaps em_slg_001AF690 — test_startup_load_gaps_reference | em_game.c em_game_legacy_state0 / em_scene_bindings.c w_001AFCA0 (native re-arm; the pool reset 001AF8E0 is translated) | S1_newgame_load* |
| 0x001AF710 | — | BM | live | em_startup_load_gaps em_slg_001AF710 (the boxes' bone-slot stack at every area build, em_area11_boxes.c) — test_startup_load_gaps_reference | only the boxes pop from the stack (CRATES_DRUMS_ORIGINAL.md Limitations) | S1_newgame_load* |
| 0x001AF780 | — | AW | live | em_roger_actor_original em_roger_actor_001AF780 (the boxes' pops) — test_roger_actor_original_reference.py |  | S2_opening |
| 0x001AF800 | — | NM | live | em_roger_actor_original em_roger_actor_001AF800 (its own slot loop, since chain C7 the one translation for the AREA11 pool: em_area11_boxes_001AF800 and Roger's, the equipment nodes' and the indicator children's binders run it over the record's +0x09 / +0x0C / +0x110 view); em_actor_pool (001AFC10's call); em_status_scene_original (the status hub's static pool over its own slots) — test_roger_actor_original_reference.py (18 cases, three mutants); test_actor_census_reference.py, test_actor_pool_reference.py, test_status_scene_reference.py; test_level_smoke.py (the equipment nodes and indicator children freed on the route; check_indicator_children) |  | S2_opening |
| 0x001AF890 | — | CL | verified-unbound | em_roger_actor_original em_roger_actor_001AF890 (bound in Roger's 001CA770, census L22) — test_roger_actor_original_reference.py | since chain C7 001AF800 pushes its slots in its own loop, as the original does (section 1.26), so the port no longer calls 001AF890 there. Its route caller in the original is the player's 001CA770 in 001B82D0 sub 4 (beats 10, 11, 13, 14), which is the face host's detach (the 001CA770 row) | S2_opening |
| 0x001AF8E0 | — | NM | live | em_actor_pool.c em_actor_pool_reset_001AF8E0, em_collision_world (class-list half) — test_actor_pool_reference; test_actor_census_reference; test_actor_collision_reference |  | S1_newgame_load* |
| 0x001AFA50 | — | BM | live | em_actor_pool — test_actor_census_reference.py, test_actor_pool_reference.py |  | S1_newgame_load |
| 0x001AFA90 | — | BM | live | em_actor_pool, em_player_closure_0e_18 — test_actor_census_reference.py, test_actor_pool_reference.py |  | S1_newgame_load |
| 0x001AFBC0 | — | BM | live | em_actor_pool — test_actor_census_reference.py, test_actor_pool_reference.py |  | S2_opening |
| 0x001AFC10 | — | BM | live | em_actor_pool.c em_actor_pool_free_001AFC10 — test_actor_pool_reference; test_actor_census_reference |  | S2_opening |
| 0x001AFCA0 | — | BM | verified-unbound | em_startup_load_gaps em_slg_001AFCA0 — test_startup_load_gaps_reference | em_scene_bindings.c w_001AFCA0: the port's own re-arm (player_states_reset, the stage bind, the collision world load) plus em_game_legacy_state0; its 001AF5C0 is live (em_slg_001AF5C0 over the player record, section 1.23) | S1_newgame_load* |
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
| 0x001B0EA0 | — | NM | live | em_owner_services_original (the boxes' allocation over the exported bank, em_area11_boxes.c; the fence door's through em_area11_boxes_door_001B0EA0, census L18) — test_owner_services_reference.py; test_collision_world_capture.py (+0x09 / +0x0C / +0x44 bound); test_level_smoke.py fence_door |  | S2_opening* |
| 0x001B0F60 | — | BM | live | em_startup_load_gaps em_slg_001B0F60 in the fence door's 001BBDA0 (em_area11_door; census L18) — test_startup_load_gaps_reference; test_level_smoke.py fence_door (the door header's +0x09 / +0x0C) |  | S2_opening* |
| 0x001B0FD0 | — | BM | live | em_crate_original / em_drum_original state 0 (001B0EA0, bone_init, +4 += 1); the truck 00823FF0 state 0 (em_owner_services_001B0FD0 via em_area11_boxes, census L23); since section 1.25 the terminal 00827B10, the panel 00159210, the prop 001C4820, the opening controller 00823E80 and the map item 0015AFA0 (0015AC00) state 0, and since section 1.39 the security gun, its cable and the fans (em_area11_boxes_owner_001B0FD0) — test_crate_original_reference.py, test_drum_original_reference.py, test_owner_services_reference.py; test_level_smoke.py (truck_crossing: the truck header +0x09 / +0x0C) |  | S2_opening* |

### 3.3 Input block and roster spawn (0x1B5000..0x1B6BEF)

14 functions, 865 instructions: live 12, verified-unbound 2 (recount 2026-09-26, shadow step).

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

34 functions, 10,019 instructions: live 33, unverified 1 (recount 2026-09-26, chain C7 takeover step).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x0015B130 | — | BM | live | em_player_floor.c em_player_stage_0015B130 (every +4 = 1 stage since L01, em_player.c live_major1) — test_player_floor_reference (stage cases); test_level_smoke.py | state[0]/[1] are 00161020 / 001612D0 since L12 (behind the port's stand-ins); a script owner's frame runs its prelude (00182B30's admission: +4 = 4, 00174A50, 00182D70; chain C7, section 1.26); the panel, terminal and item takeovers keep the interaction runtime's (it consumes the stage while it owns the player); under 0x70003B8D without an owner the idle / walk states keep the stage | S2_opening |
| 0x0015B530 | — | AI | live | em_player_stage_workers em_player_stage_0015B530 (stage major[4]: every +4 = 4 stage of a script owner's takeover, which is the stage's own since chain C7, section 1.26) — test_player_stage_workers_reference (27 cases; the 0015BA50 +4 = 4 composition); test_player_cinematic_reference (1,388 stages and the release against the original 00183090 / 001C64F0 / 00182DF0); test_level_smoke.py (check_stage_takeover: +4 = 4 from the admission to 00182DF0 on routes 07, 09, 10, 11, 13 and 14, whose +5 / +1F0 / +1F1 / clip / clock / +2F3 rows match) | its routines: 001837A0 and 00182DF0 bound; 001837B0, 001838B0 and 00183910 untranslated and 00162DB0 / 00163B40 FLOOR's (fail-stop workers, reached only through 0015B130's +1F0 0x2A / 0x17 branches or a +5 = 1 under the takeover, none on the route). The panel, terminal and item takeovers keep the interaction runtime (their scripts' animation core; +4 stays 1 there) | S2_opening |
| 0x0015BA50 | — | BM | live | em_player_floor.c em_player_stage_begin/_dispatch/_end over the record every stage (em_player.c player_states_stage, L01) — test_player_floor_reference (stage cases); test_level_smoke.py | the advance worker is the live display's 001C64F0 (em_player_pose_advance through player_pose_stage_advance); D_00248C98 from the local export; the B3 byte is still em_player_0015BCF0's stand-in expression | S2_opening |
| 0x0015BCF0 | — | BM | live (partial: tail, animate, 00187350) | em_player_floor.c em_player_stage_tail (+BC, the -200 check, the +31B loop-sound stop; L01), em_player_record_pose_animate (the animate step; display step) and 00187350 on the record (L12) inside em_player.c player_states_stage — test_player_floor_reference (stage cases); test_player_record_pose_reference; test-first-control-reference | 0015CBA0 is the camera's translation; the +A0/+B0 copies are the port's own position and camera paths | S2_opening |
| 0x0015BF90 | — | NM | live | em_shadow_actor_route em_shadow_actor_route_0015BF90 through em_shadow_live (w_0015C160 with +0x214 != 0; census L29) — test_shadow_actor_route_reference; test_level_smoke.py check_shadow (C: the ORIGINAL 0015BF90 + 001CE300 over the port's sampled inputs write the port's packets) | 0019A570's fourth hit word is not kept (0 in every capture, read by nothing on the path; SHADOW_ACTOR_ROUTE.md L2) | 02_elevator_refusal |
| 0x0015C160 | — | BM | live | em_scene_bindings.c w_0015C160 with em_shadow_original_route_0015C160 and em_shadow_live (census L29); its +0x4C 001CAA00(player) through em_player_draw_live and em_owner_draw_live (the player step, section 1.23) — test_shadow_original_reference (0015C160 executed over 36 gate cases); test_level_smoke.py check_shadow (A: the route of every post-step of the run, the draws flushed; the first-control frame draws the shadow) and check_owner_units (the player's unit against the route snapshots) | while the player record does not hold the displayed pose (the opening, design risk 2; em_scene_bindings_player_record_drawn) the post-step is reported (UM_0015C160_OPENING) and the +0x4C is the port's player mesh of the displayed pose | S2_opening |
| 0x0015C1F0 | — | NM | live | em_player_misc_workers em_player_misc_0015C1F0 through spawn_w_0015C1F0 (the player step, section 1.23: the kind +0x2FF, 001CA6E0 = em_roger_actor_001CA6E0, 001C6150 over the exported player model, +0x96; 00200890 is the boundary) — test_player_misc_workers_reference; test_level_smoke.py check_owner_units (the player's +0x44 model unit against the route snapshots) | only kind 0x3B's model is exported (the other kinds fault at 001C6150) | S1_newgame_load |
| 0x0015C310 | — | BM | live | em_area11_bindings.c em_area11_spawn_player_children_0015C420 — compare_frame_order.py; test_level_smoke.py (census 49) | spawn set and order only (node bytes not compared) | S2_opening |
| 0x0015C420 | — | BM | live | em_area11_bindings.c em_area11_spawn_player_children_0015C420 — compare_frame_order.py; test_level_smoke.py (census 49) | spawn set and order only; its 001CA6F0(player, 1) (+0x98 = 1) and +9 = +0xC are stored by player_states_reset / player_pose_attach (census L29) | S2_opening* |
| 0x0015CBA0 | — | BM | live | em_camera_leftovers em_camleft_0015CBA0 (em_player.c after the stage tail) — test_camera_leftovers_reference; test_level_smoke.py camera rows (census 1.11) | live since census L13..L16 (10706 calls): +0x236 from the player state byte | S2_opening |
| 0x0015CF90 | — | BM | unverified | em_player_frame.c em_player_0015BCF0: D_00810707 = +0x234 into the canonical progress byte (HK) and the B9 write, over the stage's vitals (em_player.c stores +220/+234 back to g.status / g.pd_infected after every stage, L01) | D_00810706/858/85C have no canonical storage (their port copies g.pd_low / g.status are the stage's store); no oracle executes 0015CF90 (the byte-matched C was read) | S2_opening |
| 0x0015D000 | — | AI | live | em_player_stage_workers em_player_stage_heartbeat (0015B130, L01) — test_player_stage_workers_reference | its rumble 001B61C0 (health <= 35) is a fail-stop worker (untranslated) | S2_opening |
| 0x0015D100 | — | BM | live | em_player_stage_workers em_player_stage_drain (0015B130, L01; em_player_damage.c's copy retired) — test_player_stage_workers_reference | 0015C9D0 and 001F0060 (the latch / infected paths) are fail-stop workers | S2_opening |
| 0x0015D2F0 | — | BM | live | em_player_equipment em_player_equipment_0015D2F0 through em_render_context_live (001D1C50, 001DDE10) over the camera's view of D_008102B0 — test_player_equipment_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context | em_weapon.c's own use still assumes variant 0 (lane L28) | S0_title |
| 0x0015D4C0 | — | NM | live | em_player_ladder_entry — test_player_ladder_entry_reference; test_level_smoke.py check_boxes (route 05 row for row) | bound since the Boxes step (em_player_closure_live; the Use chain's climb / surface probes); Use chain | 05_boxes |
| 0x0015DEC0 | — | AW | live | em_player_climb — test_player_climb_reference; test_level_smoke.py check_boxes (route 05 row for row) | bound since the Boxes step (em_player_closure_live; the Use chain's climb / surface probes) | 05_boxes |
| 0x0015DF10 | — | NM | live | em_player_climb em_player_climb_live_ledge (00160220's ledge probes, em_player_closure_live) — test_player_climb_reference; test_level_smoke.py check_boxes (route 05 row for row) |  | 05_boxes |
| 0x0015EC50 | — | NM | live | em_player_running_jump — test_player_running_jump_reference; test_level_smoke.py check_crevice_jump (route 12 row for row) | live since census L11 (em_player_closure_live: 00160220's running-jump probe); measured executing in the full smoke (census 1.9) | 12_crevice_jump |
| 0x00160220 | — | BM | live | em_player_use_dispatch em_player_use_00160220 over the live record (em_player_closure_live, scan = the host's 00184BA0) — test_player_use_dispatch_reference; test_level_smoke.py (01..05) |  | S3_first_control_idle |
| 0x001607D0 | — | BM | live | em_player_weapon_states_a em_player_weapon_001607D0 (the idle / walk states' `actions`, census L12) — test_player_weapon_states_a_reference | the stances it enters (+5 = 0x1D..0x22) run the port's stand-ins (em_weapon aim / R2 / melee, L28); D_00810C61 (its armed forwarding) is em_weapon's | S3_first_control_idle |
| 0x00161020 | — | NM | live | em_locomotion_display em_loco_00161020 over the record (0015B130 state[0], em_player_closure_live.c bind_loco; census L12) — test_locomotion_display_reference; test-first-control-reference (56 callbacks, the record against collision_run_poll.json); test_level_smoke.py | the port's stand-ins (examine / director lock, stance / R2 / melee: L21, L28; the legacy door no longer runs in AREA11 since census L18) pre-empt it while they hold the player; the legacy idle callback remains only in the scenes without an original world | S2_opening |
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

84 functions, 9,097 instructions: live 84 (recount 2026-09-26, chain C7 takeover step).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001749A0 | — | BM | live | em_pose_host_workers em_pose_host_001749A0 on the player record (em_player_record_pose; em_player_pose_host.c frame-0 requests: idle, acquire, Use, fidget, tier-2 stop) — test_player_record_pose_reference; test_pose_host_workers_reference; test_player_pose_live_reference (first-control trace) | em_player_pose.c em_player_pose_select still poses Roger and the status models | S2_opening |
| 0x001749F0 | anim_clip_arbiter | BM | live | em_pose_host_workers em_pose_host_001749F0 on the player record (em_player_record_pose; em_player_pose_host.c source-frame requests: walk entry, run stop, gait tier change) — test_player_record_pose_reference; test_pose_host_workers_reference; test_player_pose_live_reference |  | 00_panel_no_battery |
| 0x00174A50 | — | BM | live | em_player_pose_host.c player_pose_acquire (001749A0(p, 0, 0, 8.0) on the record) — test_player_pose_host_reference; test_player_record_pose_reference | the stage's translation em_player_stage_row_request is the idle / walk states' `row_request` since census L12 (00161020 case 0 / 0x63, 001756E0; 46 calls in the full smoke), over the bound 0017B490; since chain C7 also 0015B130's prelude (8.0) on a script owner's frame and 00182DF0's release request (16.0) | S2_opening |
| 0x00174AB0 | — | BM | live | em_player_ladder_climb (one owner since 2026-09-24; the closure states and, since chain C7, 00182DF0's translation run it through a bridge over their own 001749A0) — test_player_ladder_climb_reference, test_player_closure_0e_18_reference; test_level_smoke.py check_cage_ladders (route 10 row for row) | live since census L10 (the climb's clip request 001749A0(p, 0, 1, 0.0)); measured executing in the full smoke (census 1.9) | 01_battery |
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
| 0x00182B30 | — | BM | live | em_player_stage_workers em_player_stage_scripted_check (0015B130's prelude on a script owner's frame since chain C7, section 1.26) — test_player_stage_workers_reference; test_level_smoke.py (check_stage_takeover: the admission's +4 = 4, +5 = 0, +1F0 = 0x41 on the capture's first 3B8F = 1 row of routes 07, 09, 10, 11, 13 and 14) | the panel, terminal and item takeovers keep the interaction runtime's acquire (em_player_pose_host.c player_pose_acquire: 00182B30's refusal set not modelled there); under 0x70003B8D without an owner (the area-change fade) the idle / walk states keep the stage | S2_opening |
| 0x00182BF0 | — | NM | live | em_script_host_workers em_script_host_00182BF0 via em_area11_script_host (op16; census L22) — test_script_host_workers_reference; test_level_smoke.py (roger: route 14 row for row) |  | 10_cage_roof_roger |
| 0x00182D40 | — | BM | live | em_player_stage_workers em_player_00182D40 (the one translation since chain C7, inside 00182DF0's; em_locomotion_display's copy removed) — test_player_stage_workers_reference (executed inside 00182DF0, every instruction); test_locomotion_display_reference (the leaf against the original) |  | S2_opening |
| 0x00182D70 | — | BM | live | em_player_stage_workers em_player_stage_scripted_notify (0015B130's prelude on a script owner's frame since chain C7; the runtime's admission for the panel, terminal and items, census L22) — test_player_stage_workers_reference; test_level_smoke.py (panel, elevator, roger: +5 / +1F0 / +1F1 row for row) | link1C is a fail-stop worker (+1C is 0 in every route capture) | S2_opening |
| 0x00182DF0 | — | BM | live | em_player_stage_workers em_player_stage_00182DF0 (the one translation since chain C7: 0015B530's release on the stage's own takeover, and the interaction runtime's release, both through em_player_pose_host.c; 00174AB0 through em_player_ladder_climb's, 00174A50 em_player_stage_row_request, D_0028A580 from the Roger export's D_0028A490 table, 001C6150 over the exported player model) — test_player_stage_workers_reference (120 synthetic records over every branch, the captured player records with +2F3 0 and 1, fail-stop; every reachable instruction of 00182DF0 and 00182D40 executed); test_player_cinematic_reference; test_level_smoke.py (check_stage_takeover's release rows) | the pose host's approximations (record_release: clip 0 whatever +235 holds; record_release_special) are removed; player_pose_opening_release keeps a +20C = 0 / 001C63E0 reset at the opening's end, where the opening runtime owns the player (design risk 2) | S2_opening |
| 0x00182F90 | — | BM | live | em_player_pose_host.c player_pose_align — test_player_pose_host_reference; test_interaction_alignment_reference |  | S2_opening |
| 0x00183090 | — | BM | live | em_player_stage_workers em_player_stage_commit (0015BA50's +4 = 4 path on a script owner's takeover, the stage's own since chain C7, with 001D0C70 = the interaction host's face tick and 001C63E0 / 001C67E0 on the record pose); em_player_pose.c / em_interaction_animation.c commit for the runtime's takeovers (panel, terminal, items) — test_player_stage_workers_reference (commit cases, the 0015BA50 composition); test_player_cinematic_reference (1,388 stages against the original 00183090 / 001C64F0); test_level_smoke.py (routes 10, 11, 13, 14 row for row) |  | S2_opening |
| 0x001837A0 | — | BM | live | em_player_stage_live.c w_001837A0 (0015B530's +5 = 0 routine on every stage of a script owner's takeover since chain C7) — the byte-matched src/func_001837A0.c is an empty function; test_player_stage_workers_reference hooks it as 0015B530's target; test_player_cinematic_reference counts its 1,388 calls; test_level_smoke.py (check_stage_takeover) |  | S2_opening |
| 0x00183EF0 | — | BM | live | em_interaction_scan.c via em_area11_interaction_host use — test_interaction_scan_reference; test_level_smoke.py (routes 02-04) | the published interactive list (the collision world's, census L07) holds the panel, the terminal, the items, Roger (selector 0 class 10: em_roger_candidate, census L22) and the fence door (selector 0 class 5: em_door_candidate, census L18; test_door_candidate_reference, test_level_smoke.py fence_door) | 00_panel_no_battery |
| 0x00184BA0 | — | BM | live | em_interaction_scan.c via em_area11_interaction_host use — test_interaction_scan_reference; test_level_smoke.py (routes 02-04) | the published interactive list (the collision world's, census L07) holds the panel, the terminal, the items, Roger (census L22) and the fence door (census L18): W22 closed | 00_panel_no_battery |
| 0x00187350 | — | BM | live | em_player_floor em_player_footstep_tick over the record after 0015BCF0's animate step (em_player_closure_live_footstep, census L12) — test_player_footstep_reference; test-first-control-reference (+25E / +212 exact on 56 callbacks) | its 001EFD90 spawns run em_effects_live (census L26); the decal 001F0460 (its matrix's row 3 w is an unwritten stack word) and wading 001E8B90 are fail-stop (not reached on the route) | S2_opening |
| 0x00187DC0 | — | BM | live | em_player_floor.c em_player_first_contact case 0x5A (the floor service's first_contact worker's body since chain C7: em_player.c live_first_contact binds its 001FBD50 to em_sfx_play_at at the floor view's position) — test_player_floor_reference (first_contact_section: the original 00187DC0 executed, its 001FBD50 id 0x86, 0 flag and 300.0 bits compared; the real-world section runs the same translation inside the native floor service) | reached live from the floor service (census 1.22); the sound itself is em_sfx's (WP-14) | 08_truck_crossing |
| 0x00187EE0 | — | BM | live | em_player_floor — test_player_footstep_reference; test_level_smoke.py check_cage_ladders | inside 00187350 on every step since census L12, and as the ladder dismount's 00187EE0(p, p + B0, p + D0) (x_place, census L10); its 001EFD90 spawns run em_effects_live (census L26) | 00_panel_no_battery |
| 0x001885D0 | — | BM | live | em_player_ladder_climb — test_player_ladder_climb_reference; test_level_smoke.py check_cage_ladders (route 10 row for row) | live since census L10 (inside 0017FC80); measured executing in the full smoke (census 1.9) | 10_cage_roof_roger |
| 0x00188630 | — | BM | live | em_player_equipment em_player_equipment_00188630 through em_equipment_live (census L28) — test_player_equipment_reference; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks): the seven nodes' +0x00..+0x0F, +0x44, +0x4C | em_weapon.c em_weapon_update (the legacy gun tick) still runs beside it for the armed stances (L28 rest: the drawers 001854E0 / 00185760 and the one-shots fault when reached) | S2_opening |
| 0x00188A50 | — | BM | live | em_player_equipment through em_equipment_live — test_player_equipment_reference; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks) | | S2_opening |
| 0x00188AC0 | — | BM | live | em_player_equipment through em_equipment_live (the equipment change's 0015C310(player, 1) respawn after a status screen) — test_player_equipment_reference; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks) | | S2_opening |
| 0x00188B80 | — | BM | live | em_player_equipment through em_equipment_live — test_player_equipment_reference; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks) | | S2_opening |
| 0x00188DF0 | — | BM | live | em_player_equipment through em_equipment_live (variants 5 / 7: 001C9610) — test_player_equipment_reference; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks) | | S2_opening |
| 0x00188ED0 | — | BM | live | em_player_equipment through em_equipment_live — test_player_equipment_reference; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks) | the lamp 00187780 faults when reached (D_008106C7 is 0 on the route); em_weapon.c's flashlight gate + em_gfx spot term remain for the armed stances | S2_opening |
| 0x00189D30 | — | BM | live | em_player_equipment through em_equipment_live — test_player_equipment_reference; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks) | | S2_opening |
| 0x0018A1F0 | — | BM | live | em_player_equipment through em_equipment_live — test_player_equipment_reference; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks) | the knife's hit probes fault when reached (status bit 0 is never set on the route) | S2_opening |
| 0x0018A6B0 | — | BM | live | em_player_equipment em_player_equipment_tick through em_equipment_live on the pool nodes (em_area11_bindings) — test_player_equipment_reference; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks) | its +0x4C 001CAA00 runs through em_owner_draw_live since section 1.23 (in the opening's reported frames the port's player mesh carries the models instead) | S2_opening |
| 0x0018A880 | — | BM | live | em_area11_bindings.c spawn_0018A880 — compare_frame_order.py; test_level_smoke.py (census 49) | spawn only | S2_opening |
| 0x0018A8D0 | — | BM | live | em_player_equipment em_player_equipment_0018A8D0 through em_equipment_live (models from the Roger export's D_0028A56C spans, added to the equipment bank the draw REFs, slots from the one 001AF710 stack) — test_player_equipment_reference; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks): +0x44 / +0x4C / +0x09 / +0x0C equal | | S2_opening |
| 0x0018AB00 | — | BM | live | em_scene_task.c em_sf_0018AB00 — test_room_move_reference |  | 09_fence_door |

### 3.6 Camera (0x18B000..0x199FFF)

27 functions, 7,740 instructions: live 26, verified-unbound 1 (recount 2026-09-26, shadow step).

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

38 functions, 9,919 instructions: live 38 (recount 2026-09-27, one-owner step).

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
| 0x0019F330 | — | AW | live | em_coll_list_passes_walkers em_coll_list_passes_0019F330, 0019BC40 pass 2's `cross` worker through em_collision_world (section 1.28; em_collision.c's column_node removed) — test_coll_list_passes_reference.py (executes 0019F330); test_actor_collision_reference.py and test_player_climb_reference.py (the original 0019BC40 with 0019F330 and its SDK calls unhooked against the native column) | a private instrumented build over the full smoke: 65,478 calls, 103 crossings; the pass-2 node walk around it is 0019BC40's KNOWN INEXACT walk | 05_boxes |
| 0x0019F730 | — | NM | live | em_actor_collision — test_actor_collision_reference.py, test_coll_probe_reference.py | FLOOR engaged since the Boxes step (em_collision_world_bind_player; EE model) | S2_opening |
| 0x0019FE50 | — | NM | live | em_coll_move_original — test_coll_move_reference.py | recount 2026-09-25: em_coll_move_walk_0019FE50 executed on the live path (88348 calls over the five measured runs) | S2_opening |
| 0x001A0B10 | — | NM | live | em_coll_segment_walkers, em_enemy — test_coll_segment_walkers_reference.py | recount 2026-09-25: em_coll_segment_001A0B10 executed on the live path (5 calls over the five measured runs) | 02_elevator_refusal |
| 0x001A1390 | — | NM | live | em_coll_segment_walkers — test_coll_segment_walkers_reference.py | 0019A910's cell walker over the published class-4 cells | S2_opening |
| 0x001A2370 | — | NM | live | em_actor_collision (em_actor_cells_retransform_001A2370) — test_actor_collision_reference.py, test_collision_world_capture.py | the terminal 00827B10 (state 0, the ride's completion, over the +0xD0 its 001C6380 built since section 1.25), the items 00219550 (state 0, likewise) and since census L23 the truck 00823FF0 (every shake and fall tick) through em_collision_world, and since section 1.39 the security gun 0x825940's lifecycle 0 (its plate, uid 15, through bone 3); the drums (L25) wait on their owner | S2_opening |
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

9 functions, 738 instructions: live 8, verified-unbound 1 (recount 2026-09-26, shadow step).

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

10 functions, 2,678 instructions: live 10 (recount 2026-09-26, owners step).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001551B0 | — | NM | live | em_crate_original over its roster node (em_area11_boxes.c) — test_crate_original_reference, test_collision_world_capture.py (records equal route 04), test_level_smoke.py (05_boxes) | the damage and nest paths reach fail-stop workers (no live +0x36 writer); their 001EFD90 runs em_effects_live | S2_opening |
| 0x00156620 | — | NM | live | em_drum_original over its roster node (em_area11_boxes.c) — test_drum_original_reference, test_collision_world_capture.py (records equal route 04) | the break and flight paths reach fail-stop workers (no live +0x36 writer); their 001F0460 / 001EFD20 run em_effects_live | S2_opening |
| 0x001575B0 | — | BM | live | em_panel_program.c (op-9 callback: cue 0x3EF) — test_level_smoke.py (route 03 panel windows); test_area11_sfx_reference (cue 0x3EF) | tiny script callback translated inline | 03_panel_power |
| 0x00157860 | — | BM | live | em_panel.c via em_area11_interaction_host (node #26) — test_panel_reference; test_level_smoke.py (route 03) |  | S2_opening |
| 0x00157F60 | — | NM | live | em_panel_program.c + em_area11_interaction_host.c battery_open — test_level_smoke.py (route 03 B0/B1 request) | NEARMISS body; request tail only is exercised | 03_panel_power |
| 0x001580C0 | — | BM | live | em_panel_program.c + em_area11_interaction_host.c power — test_level_smoke.py (route 03 power bit); test_area_script_reference |  | 03_panel_power |
| 0x00159210 | — | BM | live | em_panel.c via em_area11_interaction_host (node #26); its state 0's 001B0FD0 / 001C6380 and its +0x4C 001CAA00 over its record (em_area11_boxes_owner_*) since section 1.25 — test_panel_reference; test_level_smoke.py (route 03; check_owner_units: its unit against the snapshots) | the legacy panel mesh (the manifest's `grate` model) is retired (section 1.25) | S2_opening |
| 0x0015AC00 | — | NM | live | em_pickup_owner em_pickup_owner_0015AC00 through em_area11_interaction_host_pickup_state0 (the map item's record: the scale switch into +0x60..+0x68, +0x80 = 4.0, 001B0FD0 / 001B1020, 001C6380, +0x00 / +0x08, 001F1110) — test_pickup_owner_reference (executes 0015AC00 through 0015AFA0's state 0: 176 cases, every call and record byte); test_level_smoke.py check_owner_units (the map item's unit) | the owners step 2026-09-26 (section 1.25); the +0x30 store is not modelled (no reader) | S2_opening* |
| 0x0015AE20 | — | BM | live | em_pickup_owner em_pickup_owner_tick (0015AFA0's state-1 call) — test_pickup_owner_reference (runs unhooked inside the executed 0015AFA0) | live since WP-6 (81414be) | S2_opening |
| 0x0015AFA0 | — | BM | live | em_pickup_owner em_pickup_owner_tick via em_pickup.c and the AREA11 interaction host — test_pickup_owner_reference (executes 0015AFA0 and compares the tick) | live since WP-6 (81414be); its aura's draw block runs em_effect_manager_aura_draw since the census L26 step (see 001F1180); its visible +0x4C is 001CAA00 over its record since section 1.25 (the legacy item mesh is retired) | S2_opening |

### 3.10 Vector math and owner services (0x1B1000..0x1B4FFF)

20 functions, 793 instructions: live 16, verified-unbound 3, unverified 1 (recount 2026-09-26, full-route recount).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001B1020 | — | AI | live | em_owner_services_original em_owner_services_001B1020 via em_area11_roger (the equipment's model 0x6B of D_0028A56C; census L22) and since section 1.25 via em_area11_boxes_owner_001B1020 (the six items 00219550's model 0x72 of D_0028A56C, the library bank) — test_owner_services_reference.py, test_roger_actor_original_reference.py; test_level_smoke.py (roger: route 14 row for row) |  | S2_opening* |
| 0x001B10B0 | — | BM | live | em_roger_actor_original em_roger_actor_001B10B0 via em_area11_roger (census L22) — test_roger_actor_original_reference.py; test_level_smoke.py (roger: route 14 row for row) |  | S2_opening* |
| 0x001B1190 | — | CL | unverified | em_pickup.c taken_set (the owner's PERSIST event) | test_pickup_owner_reference hooks 001B1190 (persistence); no oracle executes that copy. The verified translation em_gun_rest_001B1190 (em_security_gun_rest, test_security_gun_rest_reference: 30 cases) is bound for the gun cable's lifecycle 2 since section 1.39, which the route does not reach; the items' path still uses taken_set (to reduce to that one owner) | 01_battery |
| 0x001B11E0 | — | NM | live | em_actor_roster (the spawners' condition 1; exported as em_actor_roster_001B11E0 for the gun cable 00827490's lifecycle 0 since section 1.39), em_enemy — test_actor_census_reference.py; test_level_smoke.py check_gun_fan (the cable's lifecycle 0 on every run) |  | S1_newgame_load* |
| 0x001B1240 | — | BM | live | em_script_host_workers em_script_host_001B1240 (0018C0D0 and the camera workers, em_camera_live) — test_script_host_workers_reference; test_camera_live_reference; test_level_smoke.py camera rows (census 1.11) | live since census L13..L16 (21810 calls) | S1_newgame_load |
| 0x001B12B0 | — | AW | live | em_script_host_workers em_script_host_001B12B0 (bound in em_player_slide) — test_player_slide_reference.py, test_script_host_workers_reference.py, test_camera_follow_original_reference.py; test_level_smoke.py (06_hill_slide row for row, census L03) | live through the slide (0016C6A0 sub-states 1/2 turn +C4 onto +218); its camera and script-host callers are still unbound (legacy follow camera, L19) | 00_panel_no_battery |
| 0x001B1380 | — | AI | live | em_script_host_workers em_script_host_001B1380 through em_area11_script_host w_001B1380 (op15 001B6FA0: Roger's 0x828990 and the director's beats) — test_script_host_workers_reference.py (lockstep over the original); test_player_misc_workers_reference.py | recount 2026-09-26 (section 1.22): measured running on the full route (op15 only). Its other original caller on every beat, the positional sound request 001FBF50, is em_sfx.c em_sfx_play_at in the port, which does not call it (the 001FBF50 row) | S2_opening |
| 0x001B1470 | — | BM | live | em_player_stage_workers em_player_001B1470, em_effect_original / em_fan_original wraps — test_player_stage_workers_reference, test_effect_original_reference, test_fan_original_reference | the live copies (em_player_heading.c, em_camera.c cam_wrap_pi) are host wraps; test_player_heading_reference replaces 001B1470 with a host wrap (critic 7.2); recount 2026-09-25: wrap_001B1470 executed on the live path (152 calls over the five measured runs) | S1_newgame_load |
| 0x001B15D0 | — | BM | verified-unbound | em_player_misc_workers — test_player_misc_workers_reference.py |  | S2_opening |
| 0x001B1630 | — | NM | live | em_interaction_scan (em_interaction_visible) — test_interaction_pickup_reference.py, test_owner_services_reference.py | 001B17A0's gate for the panel, the terminal and the items (g.cam.eye / fwd) | S2_opening |
| 0x001B17A0 | — | BM | live | em_owner_services_original — test_owner_services_reference | the one 001B17A0 of the panel, the terminal, the items and Roger (census L22; the host's duplicate em_interaction_scene_offer is retired), the drums (L25) and since section 1.25 the prop 001C4820; since section 1.39 the security gun, its cable and the fans' fast arm (em_area11_bindings.c) | S2_opening |
| 0x001B1B30 | — | BM | live | em_script_door_fan em_sdf_001B1B30 in em_area11_door's publish hook (census L18) — test_script_door_fan_reference; test_level_smoke.py fence_door (route 09 f309..f532 row for row): the door published for the scan |  | S2_opening |
| 0x001B1B70 | — | BM | live | em_actor_collision (em_actor_class_publish_001B1B70) — test_actor_collision_reference.py, test_owner_services_reference.py, test_collision_world_capture.py | 001B17A0's worker for the panel, the terminal, the items and since section 1.25 the prop 001C4820 (their pool records); the crates and drums since L25, the truck since L23 and the opening controller 00823E80 since section 1.25 publish directly | S2_opening |
| 0x001B1CA0 | — | BM | verified-unbound | em_actor_collision (001B1B70's class 2/0xA push) — test_actor_collision_reference.py | bound in the live 001B1B70; no owner the port runs publishes class 2/0xA yet (Roger 008237E0, L22) | 03_panel_power |
| 0x001B1D20 | — | BM | live | em_actor_collision (001B1B70's class-4 push; em_actor_class_push4_001B1D20) — test_actor_collision_reference.py, test_collision_world_capture.py | the cells of the panel, the terminal and the items; the crates, drums and truck wait on L25/L23 | S2_opening |
| 0x001B1D60 | — | BM | live | em_actor_collision (001B1B70's class-7 push) — test_actor_collision_reference.py | the map item 0015AFA0 (class 0x87) and an armed 00219550 (0x87) | S2_opening |
| 0x001B1DA0 | — | BM | verified-unbound | em_actor_collision (001B1B70's class-0xD push) — test_actor_collision_reference.py | bound in the live 001B1B70; no owner the port runs publishes class 0xD (the flame 008235F0's 001B17A0 is not bound) | S2_opening |
| 0x001B1DE0 | — | BM | live | em_actor_collision (001B1B70's interactive push) — test_actor_collision_reference.py | the panel, the terminal and the items (class bit 0x80); the Use scan reads the published list | S2_opening |
| 0x001B1E20 | — | BM | live | em_owner_services_original via em_pad_actuator_001B1E20 (the truck's arm and fall rumbles, census L23) — test_owner_services_reference.py; test_level_smoke.py (truck_crossing) | the records D_0024D6F0 come from assets/pad_rumble.emrg (tools/export_pad_tables.py); em_gamepad.h keeps its own table copy (unused on the live path) | S2_opening |
| 0x001B1EA0 | — | AW | live | em_director_original em_director_original_001B1EA0_bound via em_area11_roger (Roger's trigger over the quad 0x82AB80 with 0011E620 of em_sdk_math_original; census L22), em_manager_008257A0 — test_director_original_reference.py (part 2: 0x82AB80); test_level_smoke.py (roger: route 14 row for row) (the script start f283) | the director's three quads run through it since WP-8b (node #21); the live camera's 00190F20 / 00194D10 quads (assets/camera_tables.emrg) since census L13..L16 | S2_opening |

### 3.11 Script host and script ops (0x1B6BF0..0x1BBD5F)

29 functions, 3,487 instructions: live 24, verified-unbound 5 (recount 2026-09-26, full-route recount).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001B6BF0 | — | NM | live | em_area_script op18 via em_area11_script_host (census L22) — test_script_door_fan_reference (part 1); test_level_smoke.py (roger: route 14 row for row) |  | S2_opening |
| 0x001B6E40 | — | BM | live | em_area_script op16 via em_area11_script_host (00182BF0 through em_script_host_workers; census L22) — test_script_door_fan_reference (part 1); test_level_smoke.py (roger: route 14 row for row) |  | 10_cage_roof_roger |
| 0x001B6EA0 | — | BM | live | em_pickup_owner em_pickup_owner_take via em_pickup.c original_take — test_pickup_owner_reference (executes 001B6EA0) |  | 01_battery |
| 0x001B6F00 | — | BM | live | em_interaction_alignment.c — test_interaction_alignment_reference |  | 00_panel_no_battery |
| 0x001B6F80 | — | BM | live | em_area_script op01 kind 9 (inline: the player's heading, then 00182F90) via em_area11_script_host — test_script_door_fan_reference (part 1: the handler runs as original code inside 001BA1F0 while the em_area_script lockstep passes); test_level_smoke.py (roger: the 01/9 placement and heading at route 14 f1756 and every row after, exact) | recount 2026-09-26 (section 1.22): Roger's 0x8283D0 runs it live; the opening's use (S2) is em_opening_runtime's own op01 | S2_opening |
| 0x001B6FA0 | — | BM | live | em_area_script op15 via em_area11_script_host (Roger's 0x828990 conversation, route 10, since WP-8b; 2,357 calls on the full route) — test_script_door_fan_reference (part 1: the handler runs as original code inside 001BA1F0 while the em_area_script lockstep passes); test_level_smoke.py (cage_roof) | | 10_cage_roof_roger |
| 0x001B7840 | — | BM | live | em_area_script op10 via em_area11_script_host (census L22) — test_script_door_fan_reference (part 1); test_level_smoke.py (roger: route 14 row for row) |  | 14_roger_encounter |
| 0x001B7B30 | — | BM | live | em_area_script op0D via em_area11_script_host (census L22), em_cinematic_playback — test_cinematic_playback_reference; test_script_door_fan_reference (part 1); test_level_smoke.py (roger: route 14 row for row) | em_camera.c retarget/chase hooks (cases 3/5; level smoke routes 02-04) | S2_opening |
| 0x001B7D60 | — | BM | live | em_message_service em_message_op0c via em_message_live (the opening's op0C; the AREA11 host's panel/terminal lines; since WP-8b the director's and Roger's voiced lines 0x7F / 0x97 / 0x99) — test_message_service_reference, test_panel_message_reference; test_level_smoke.py (the director beats) | | S2_opening |
| 0x001B7F90 | — | BM | live | em_pickup_motion em_pickup_turn via the interaction host — test_pickup_motion_reference.py (executes 001B7F90) |  | 01_battery |
| 0x001B8020 | — | BM | live | em_area_script op0B (subs 0, 4, 6 admitted) via em_area11_script_host (Roger's 0x8283D0, census L22; the fence door's 0x24DC40, census L18) — test_area_script_reference (synthetic subs 6 / 0; route 09); test_level_smoke.py (roger: route 14; fence_door: route 09 row for row) |  | 09_fence_door |
| 0x001B81D0 | — | BM | live | em_area_script face_attach via em_area11_script_host (census L22) — test_script_door_fan_reference (part 1); test_level_smoke.py (roger: route 14 row for row) |  | S2_opening |
| 0x001B82D0 | — | BM | live | em_script.c / em_opening_runtime.c execute; em_area_script op07 via em_area11_script_host (the truck and Roger's scripts, census L19 / L22) — test_interaction_frame_reference; test_script_reference; test_area_script_reference; test_level_smoke.py (roger: route 14 row for row) | op subset used by the opening, panel and elevator scripts | S2_opening |
| 0x001B8FC0 | — | BM | live | em_area_script op00 (kinds 0, 1, 2 live in 0x8292C0; census L19) — test_area_script_reference (route 07 replay); test_level_smoke.py (truck_preview: every camera shot row for row) | the opening camera track is em_opening_runtime.c + em_cinematic_camera.c; em_elevator_program runs its own op00 sub0 (one bound owner per handler is open, AREA_SCRIPT.md 5); the pickup grab program's op00 camera settle runs live as em_pickup_motion em_pickup_camera_settle (test_pickup_motion_reference executes 001B8FC0 for that case) | S2_opening |
| 0x001B94F0 | — | BM | live | em_area_script op01 (kind 1 live in 0x8292C0: 00182F90 through the pose host; census L19) — test_area_script_reference; test_level_smoke.py (truck_preview placement) | em_elevator_program runs its own op01 sub1 (AREA_SCRIPT.md 5) | S2_opening |
| 0x001B99F0 | — | NM | live | em_panel_program.c / em_opening_runtime.c op 9 — test_player_cinematic_reference; test_area_script_reference |  | 01_battery |
| 0x001B9BA0 | — | BM | live | em_panel_program.c / em_elevator_program.c op 2 — test_level_smoke.py (routes 02-04) |  | 03_panel_power |
| 0x001B9C10 | — | BM | live | em_player_pose_host.c player_pose_face — test_player_pose_host_reference |  | 02_elevator_refusal |
| 0x001BA080 | — | BM | live | em_area_script op06 via em_area11_script_host (census L22) — test_script_door_fan_reference (part 1); test_level_smoke.py (roger: route 14 row for row) |  | S2_opening |
| 0x001BA1A0 | — | BM | live | em_area_script em_area_script_start via em_area11_script_host (the truck trigger, census L19 / L23) — test_director_original_reference, test_area_script_reference; test_level_smoke.py (truck_preview) | em_script.c starts the opening, panel and elevator scripts (their own hosts); Roger's starts run here since census L22, the director's since WP-8b | S2_opening |
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

9 functions, 492 instructions: live 9 (recount 2026-09-26, shadow step).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001BBD60 | — | BM | live | em_script_door_fan em_sdf_001BBD60 in em_area11_door's 001BBE40 patch (census L18) — test_script_door_fan_reference; test_level_smoke.py fence_door (route 09 f309..f532 row for row): the patched program's cue 0x401 |  | 09_fence_door |
| 0x001BBDA0 | — | BM | live | em_door_original lifecycle 0 via em_area11_door (001B0F60 em_slg_001B0F60, 001B0EA0 through em_area11_boxes; census L18) — test_door_original_reference; test_level_smoke.py fence_door (route 09 f309..f532 row for row): +0x09 / +0x0C / +0x2E / +0x80 in the door header |  | S2_opening* |
| 0x001BBE40 | — | BM | live | em_door_transit em_door_transit_kickoff (EE float model) via em_area11_door (census L18) — test_door_transit_reference (on FallEE; route 09's f309 alignment); test_level_smoke.py fence_door (route 09 f309..f532 row for row) |  | S2_opening |
| 0x001BC0E0 | — | BM | live | em_door_original phases 1..3 over the AREA11 script host (em_area11_door; census L18) — test_door_original_reference; test_level_smoke.py fence_door (route 09 f309..f532 row for row): the door's block and +0x1FE |  | 09_fence_door |
| 0x001BC150 | — | BM | live | em_door_transit_commit via em_area11_door's transition hook (census L18) — test_door_transit_reference; test_level_smoke.py fence_door (route 09 f309..f532 row for row) with test_room_move_reference's sequence checks |  | 09_fence_door |
| 0x001BC240 | — | BM | live | em_door_original phase 4 via em_area11_door (census L18; the one translation since section 1.28, em_sdf_001BC240 removed) — test_door_original_reference (unhooked inside 001BC350 over every flag answer, request byte and armed byte; the route 09 phase-4/5 replay); test_level_smoke.py fence_door (route 09 f309..f532 row for row) |  | 09_fence_door |
| 0x001BC290 | — | BM | live | em_door_original phase 5 via em_area11_door (census L18; the one translation since section 1.28, em_sdf_001BC290 removed) — test_door_original_reference (as 001BC240; the replay restarts on the capture's row with its +0x0B); test_level_smoke.py fence_door (route 09 f309..f532 row for row) |  | 09_fence_door |
| 0x001BC300 | — | BM | live | em_door_original via em_area11_door (the runtime's pose, 001B1B30, the actor draw; census L18) — test_door_original_reference, test_door_original_runtime; test_level_smoke.py fence_door (route 09 f309..f532 row for row) |  | S2_opening |
| 0x001BC350 | — | BM | live | em_door_original em_door_original_tick via em_area11_door (node tick_door; census L18) — test_door_original_reference; test_level_smoke.py fence_door (route 09 f309..f532 row for row) |  | S2_opening |

### 3.13 Area services, child spawns, area title (0x1BC400..0x1C5FFF)

21 functions, 1,659 instructions: live 18, verified-unbound 3 (recount 2026-09-26, owners step).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001C1D00 | — | BM | verified-unbound | em_frame_render_heads em_frh_001C1D00 — test_frame_render_heads_reference | em_render_frame.c em_render_001C1D00 (port render-env init) | S2_opening |
| 0x001C1DC0 | — | BM | live | em_render_verify_rest em_rvr_001C1DC0 through em_render_context_live (w_001C1DC0, 0x1AE040 states 0 and 4) — test_render_verify_rest_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context | its 001C1E70 -> 001D52E0 reported (UM_001D52E0: the static-object bank is not exported) | S1_newgame_load |
| 0x001C1E70 | — | BM | live | em_render_verify_rest em_rvr_001C1E70 through em_render_context_live — test_render_verify_rest_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context | its 001D52E0 is reported (UM_001D52E0) | S1_newgame_load |
| 0x001C1E80 | — | BM | live | em_render_verify_rest em_rvr_001C1E80 through em_render_context_live — test_render_verify_rest_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S1_newgame_load |
| 0x001C1E90 | — | BM | live | em_render_verify_rest em_rvr_001C1E90 through em_render_context_live — test_render_verify_rest_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S1_newgame_load |
| 0x001C1EA0 | — | BM | live | em_area11_bindings.c em_area11_spawn_weather_001C1EA0 — compare_frame_order.py; test_level_smoke.py (census 49) | its 001EFD20 is em_effect_original's through em_effects_live (census L26) | S1_newgame_load |
| 0x001C1F50 | — | NM | live | em_render_verify_rest em_rvr_001C1F50 through em_render_context_live — test_render_verify_rest_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S1_newgame_load |
| 0x001C22A0 | — | BM | live | em_render_verify_rest em_rvr_001C22A0 through em_indicator_bind_live (the terminal's 001C5760 child over *D_0028A59C and the one bone-slot stack) — test_render_verify_rest_reference; test_level_smoke.py check_indicator_children (+0x09 / +0x0C / +0x44 / +0x4C equal the route snapshots', the 001C6380 placement bit for bit) | status UI step 2026-09-26 (section 1.21); the slot addresses are not the original's yet (the stack history before the children) | S2_opening* |
| 0x001C2360 | — | BM | live | em_render_verify_rest em_rvr_001C2360 through em_indicator_bind_live (the 001C5680 children over D_0028A56C: models 0x73 / 0x74 / 0x75 / 0x7A in the Roger export) — test_render_verify_rest_reference; test_level_smoke.py check_indicator_children | status UI step 2026-09-26 (section 1.21); as 001C22A0 | S2_opening* |
| 0x001C40B0 | — | NM | live | em_pickup_items_original em_pickup_items_001C40B0 via em_pickup.c — test_pickup_items_reference (executes 001C40B0); test_continue_reset_reference |  | S0_title |
| 0x001C4760 | — | BM | live | em_director_original_001C4760 through em_director_original_001C4760_scene (the canonical key bytes; the opening 00823E80's 001C4760(0, 1), and since WP-8b the original director's beat-0 001C4760(1, 1)) — test_continue_reset_reference.py (executes 001C4760, 90 cases, and the opening slice 0x823F6C..0x823F8C through its call), test_director_original_reference.py |  | S2_opening |
| 0x001C47A0 | — | BM | live | em_pickup.c pickup_take (B0 = 1, B1 = type) — test_level_smoke.py (battery phase; route 01 f220..f459) | called from em_pickup.c original_take through the original 00219550 program (81414be) | 01_battery |
| 0x001C4820 | — | BM | live | em_status_ui_leftovers em_sul_001C4820 at the area11[20] node (em_area11_bindings.c tick_prop_001C4820: 001B0FD0 / 001C6380 / +0x4C through em_area11_boxes_owner_*, 001B17A0 through the host's services) — test_status_ui_leftovers_reference; test_level_smoke.py check_owner_units (its 001CAA00 unit against the snapshots), test_collision_world_capture.py (its cell uid 17 on the published class-4 list) | the owners step 2026-09-26 (section 1.25); the manifest's legacy prop instance at its +0xB0 is retired | S2_opening |
| 0x001C5570 | — | BM | live | em_area11_bindings.c spawn_001C5570 — compare_frame_order.py; test_level_smoke.py (census 49) | spawn only | S2_opening* |
| 0x001C5680 | — | AI | live | em_indicator_child em_indicator_child_step, per node (em_area11_bindings.c tick_indicator), its bind 001C2360 and placement 001C6380 through em_indicator_bind_live — test_census_unverified_reference (every beat's children in walk order), test-indicator-child; test_level_smoke.py check_indicator_children | stand-in: the +0x4C draw 001CACB0 (001CABA0: channel 3, lighting mode 1, 001D3990 / 001D3D90, the 001CAAC0 depth sort into page D_007635C0) is not translated: the child's model mesh is drawn additively at the child's own node 0 (its slot +0x90, since the owners step, section 1.25), and the security gun's 0x7A lamp draws nothing (since section 1.39 the gun's own lifecycle 0 allocates it, no interim spawn); 001CABA0's 001CA7B0 cull is not modelled. The bind / placement stand-ins are retired (status UI step 2026-09-26 (section 1.21)) | S2_opening |
| 0x001C5760 | — | AI | live | em_indicator_child em_indicator_child_step, per node; its bind 001C22A0 and placement 001C6380 through em_indicator_bind_live; the terminal's colour tail 0x827EAC (em_indicator_00827B10_colour) — test_census_unverified_reference, test-indicator-child; test_level_smoke.py check_indicator_children | stand-in: the draw as 001C5680. The terminal's 00102958 copy of its node 0 matrix into the child's slot (0x827E6C) is bound since the owners step (section 1.25): check_indicator_children holds the child's slot equal to the terminal's node on every tick and the node equal to routes 00..03 before the ride and 04..14 after it | S2_opening |
| 0x001C5860 | — | BM | verified-unbound | em_status_ui_leftovers em_sul_001C5860 — test_status_ui_leftovers_reference | em_hud.c legacy area-title sub-location line | S2_opening |
| 0x001C5930 | — | NM | verified-unbound | em_status_ui_leftovers em_sul_001C5930 — test_status_ui_leftovers_reference | em_hud.c legacy area-title card; node lifecycle translated in em_area11_bindings.c tick_area_title (from the .s) | S2_opening |
| 0x001C5C50 | — | BM | live | em_actor_roster.c em_actor_roster_spawn_001C5C50 — test_actor_census_reference |  | S1_newgame_load |
| 0x001C5C90 | — | BM | live | em_roger_actor_original em_roger_actor_001C5C90 via em_area11_roger (the equipment node area11[9]; census L22) — test_roger_actor_original_reference.py; test_level_smoke.py (roger: route 14 row for row) (attach_r9 +0x00..+0x0F, +0xB0) | its +0x4C draw is the port actor draw of opening/equipment_6b.emdl at the bone-0 matrix | S2_opening |
| 0x001C5FB0 | — | NM | live | em_status_draw.c em_status_draw_001C5FB0 (the one translation: the hub's infection field, 00208AD0 / 00209280 and the status pages' numbers) — test_status_hub_ui_reference; test_status_draw_reference; test_level_smoke.py status_pages |  | 01_battery |

### 3.14 Animation runtime (0x1C6000..0x1CC16F)

58 functions, 4,381 instructions: live 51, verified-unbound 6, missing 1 (recount 2026-09-26, full-route recount).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001C6120 | — | BM | live | em_pose_host_workers on the player record (em_player_record_pose), em_owner_services_original — test_player_record_pose_reference, test_owner_services_reference.py, test_pose_host_workers_reference.py, test_shadow_original_reference.py |  | S0_title |
| 0x001C6150 | — | BM | live | em_owner_services_original em_owner_services_001C6150 (001B0EA0: the boxes and the fence door, em_area11_boxes; the player's 0015C1F0 through em_player_draw_live and the equipment's 0018A8D0 through em_equipment_live since section 1.23) and em_roger_actor_original model_bone_count (001B10B0: Roger, via em_area11_roger) — test_owner_services_reference.py, test_roger_actor_original_reference.py (both execute 001C6150 unhooked); test_player_misc_workers_reference.py | recount 2026-09-26 (section 1.22): measured running on the live path. Since section 1.28 the status pages' (em_status_models) and the indicator children's (em_indicator_bind_live) w_001C6150 call em_owner_services_001C6150 too, as em_equipment_live's does since section 1.23. em_roger_actor_original's model_bone_count stays a second read of the byte over Roger's original resource bytes (no EmOwnerModel view of his body model exists yet; its oracle executes 001C6150 unhooked) | S1_newgame_load |
| 0x001C61D0 | — | BM | live | em_pose_host_workers on the player record (em_player_record_pose: the host's clip frames), em_weapon — test_player_record_pose_reference; test_pose_host_workers_reference, test_player_fall_reference.py, test_player_reaction_reference.py, test_player_recovery_reference.py |  | 00_panel_no_battery |
| 0x001C62C0 | bone_init_default_1 | AW | live | em_status_models, em_owner_services_original — test_owner_services_reference.py |  | S2_opening |
| 0x001C6380 | — | BM | live | em_owner_services_original, em_status_models — test_owner_services_reference.py |  | S2_opening |
| 0x001C63E0 | bone_init_default_2 | BM | live | em_pose_host_workers on the player record (em_player_record_pose_default: the opening release, 0015C420's pose half at the attach, the legacy re-seed, the special-bank release) and on Roger's record (em_area11_roger, census L22), em_status_models — test_player_record_pose_reference; test_pose_host_workers_reference; test_player_cinematic_reference |  | S2_opening |
| 0x001C64F0 | anim_advance_time | NM | live | em_player_stage_workers em_player_stage_anim_advance on the player record (em_player_record_pose: 0015BA50's advance, the idle and script ticks; the chain step) — test_player_record_pose_reference; test_pose_host_workers_reference; test_player_pose_live_reference | em_player_pose.c em_player_pose_advance poses the status models; Roger's record runs em_player_stage_anim_advance through em_area11_roger (census L22); the interaction runtime's baked clock (em_interaction_animation.c) is checked against the record every tick | S2_opening |
| 0x001C67E0 | anim_clip_init | BM | live | em_pose_host_workers (clip init) on the player record (em_player_record_pose) — test_player_record_pose_reference; test_pose_host_workers_reference |  | 00_panel_no_battery |
| 0x001C68C0 | — | BM | live | em_pose_host_workers on the player record (0015BCF0's animate step for a zero D_00248C90 row: the takeover clips; 0015C420's pose half) and on Roger's record (em_area11_roger, census L22), em_door — test_player_record_pose_reference; test_pose_host_workers_reference; test_level_smoke.py (roger: route 14 row for row) |  | S2_opening |
| 0x001C6960 | — | BM | live | em_pose_host_workers via em_player_record_pose_animate (+0x2F3 = 2: the special bank on the record, census L22) — test_pose_host_workers_reference.py; test_player_cinematic_reference; test_level_smoke.py (roger: route 14 row for row) (clip / clock with +0x2F3 = 2) |  | S2_opening |
| 0x001C6DA0 | anim_eval_skeleton | AU | live | em_pose_host_workers em_pose_host_001C6DA0 on the player record (0015BCF0's animate step for a nonzero D_00248C90 row; 0017B910's foot-stop begin) — test_player_record_pose_reference (the captured skeletons re-evaluated byte for byte); test_pose_host_workers_reference | em_pose_bank / em_player_pose evaluation remains for Roger and the status models; other actors use exported matrices | S2_opening |
| 0x001C7420 | — | NM | live | em_owner_services_original through em_owner_draw_live (the crates, drums, truck, fence door; object-unit step; the player (21 nodes) and its equipment since section 1.23; the terminal, panel, prop, items and canopy since section 1.25; the security gun, its cable and the fan pair since section 1.39) — test_owner_services_reference, test_owner_draw_reference; test_level_smoke.py check_owner_units (the units against the route snapshots) | em_face_model.c / em_opening_actor.c basis collapse for the legacy-drawn actors (Roger) | S2_opening |
| 0x001C7900 | — | NM | verified-unbound | em_anim_runtime_rest em_anim_rest_001C7900 — test_anim_runtime_rest_reference | not bound | S2_opening |
| 0x001C7C00 | — | NM | live | em_cinematic_camera em_cinematic_camera_sample (em_render_frame.c -> em_opening_runtime_camera every opening frame; em_cinematic_playback_tick for Roger) — test_roger_cinematic_reference.py; test_level_smoke.py (roger) | live for S2 (critic 7.2) and, since census L22, for Roger's bank 0x96 timeline (em_cinematic_playback_tick through the AREA11 script host, measured 2026-09-26; test_level_smoke.py roger compares the camera eye / target from route 14 f358) | S2_opening |
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
| 0x001CA5E0 | — | BM | live | em_roger_actor_original, inline in em_roger_actor_001CA6E0 (the boxes', Roger's and, since section 1.23, the player's model bind; census L22) — test_roger_actor_original_reference.py; test_level_smoke.py (boxes, roger) |  | S1_newgame_load |
| 0x001CA5F0 | — | BM | live | em_status_models; em_roger_actor_original, inline in em_roger_actor_001CA6E0 (kind 0: the draw method 001CAA00; census L22; the player's +0x4C since section 1.23) — test_roger_actor_original_reference.py; test_level_smoke.py (boxes, roger) |  | S1_newgame_load |
| 0x001CA6E0 | — | BM | live | em_roger_actor_original em_roger_actor_001CA6E0 (the boxes' model bind; the player's in 0015C1F0 since section 1.23) — test_roger_actor_original_reference.py |  | S1_newgame_load |
| 0x001CA6F0 | — | BM | live | em_roger_actor_original, inline in em_roger_actor_008237E0_init (+0x98 = 2; census L22) — test_roger_actor_original_reference.py; test_level_smoke.py (roger) |  | S2_opening* |
| 0x001CA700 | — | BM | live | em_roger_actor_original em_roger_actor_001CA700 (Roger's face slot, census L22); the player's row-0x18 face through em_player_face_host (the script host's 001CA700) — test_roger_actor_original_reference.py; test_player_face_host.py; test_level_smoke.py (roger: route 14 row for row) | the player's 001CA700 in 001B81D0 is the face host's attach (no pool slot at +0x90: with 001CA770 it waits for 001CB3C0) | S2_opening |
| 0x001CA770 | — | BM | verified-unbound | em_roger_actor_original — test_roger_actor_original_reference.py | bound in em_area11_roger (001BA540, census L22; not reached on the route). The player's 001CA770 in 001B82D0 sub 4 (beats 10, 11, 13, 14) is em_player_face_host_detach: the port's player face is the face host's state, not a pool slot at the record's +0x90; binding 001CA700 / 001CA770 on the player needs the attachment draw 001CB3C0 (missing), which a nonzero +0x90 reaches in the player's 001CAA00 | S2_opening |
| 0x001CA7B0 | — | BM | live | em_owner_draw_original em_owner_draw_001CA7B0 through em_owner_draw_live (object-unit step) — test_owner_draw_reference; test_level_smoke.py check_owner_units (the units against the route snapshots) (the drawn set in the camera-exact beats) |  | S2_opening |
| 0x001CA940 | — | BM | live | em_owner_draw_original em_owner_draw_001CA940 through em_owner_draw_live (object-unit step) — test_owner_draw_reference; tools/test_object_unit_reference.py; test_level_smoke.py check_owner_units (the units against the route snapshots) |  | S2_opening |
| 0x001CA990 | — | BM | live | em_owner_services_original through em_owner_draw_live (object-unit step; the player and its equipment since section 1.23) — test_owner_services_reference.py, test_owner_draw_reference.py; test_level_smoke.py check_owner_units (the units against the route snapshots) |  | S2_opening |
| 0x001CAA00 | — | BM | live | em_owner_services_original em_owner_services_001CAA00 through em_owner_draw_live (the +0x4C of the crates, drums, truck and fence door; object-unit step; since section 1.23 the player's, 0015C160, through em_player_draw_live, and the seven equipment nodes', 0018A6B0; since section 1.25 the terminal's, the panel's, the prop 001C4820's, the items' and the opening controller's canopy, and since section 1.39 the security gun's, its cable's and the fans', em_area11_boxes_owner_draw) — test_owner_services_reference.py, test_owner_draw_reference.py (D: the native unit equals the original over 255 owner-frames), test_actor_light_001d89d0_reference.py (C: the native chain over 375 owner-frames: the world owners, the player's 15 and the equipment's 105), tools/test_object_unit_reference.py; test_level_smoke.py check_owner_units (the units against the route snapshots; the player and equipment in beats 10 / 14 with their pose) | Roger's +0x4C: the port's actor draw (em_area11_roger_draw, census L22; 001CB3C0 not bound); the fan pair's and the husks' legacy draws are not this function (OWNER_DRAW.md section 11) | S2_opening |
| 0x001CAAC0 | — | NM | verified-unbound | em_anim_runtime_rest em_anim_rest_001CAAC0 — test_anim_runtime_rest_reference | not bound | S2_opening |
| 0x001CACB0 | — | BM | verified-unbound | em_anim_runtime_rest em_anim_rest_001CACB0 — test_anim_runtime_rest_reference | not bound | S2_opening |
| 0x001CB2C0 | — | NM | verified-unbound | em_anim_runtime_rest em_anim_rest_001CB2C0 — test_anim_runtime_rest_reference | not bound | S2_opening |
| 0x001CB3C0 | — | BM | missing | — (em_owner_services_original only names it as 001CAA00's worker slot w_001CB3C0; no translation: corrected 2026-09-25, a label is not evidence) | not bound: an owner with +0x90 != 0 faults in em_owner_draw_live; Roger's face draws through the legacy mesh (OWNER_DRAW.md section 11) | S2_opening |
| 0x001CB590 | — | BM | live | em_scene_bindings, em_status_models — test_actor_pool_reference.py |  | S2_opening |
| 0x001CB5A0 | — | BM | live | em_scene_bindings.c w_001CB5A0 (empty leaf) — test_scene_frame_reference |  | S2_opening |
| 0x001CB5B0 | anim_bone_array_setup | BM | verified-unbound | em_anim_runtime_rest em_anim_rest_001CB5B0 — test_anim_runtime_rest_reference | em_status_models.c w_001CB5B0 (unverified; does not write the word) | S2_opening |
| 0x001CB5F0 | — | BM | live | em_packet_chain_original em_packet_chain_001CB5F0 through em_effects_live (the barrel's lanes, the handlers' and head sprite's 001CFBE0, 001CD520) and em_render_context_live — test_packet_chain_reference.py (oracle; 1,053 captured chain blocks replayed); test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks): the lane packets byte for byte | census L26 / L27 / L39 step (2026-09-25): the chains are built exactly; since WP-13 the chain page they build is drawn (em_chain_page_live, CHAIN_PAGE.md; section 1.24) | S2_opening |
| 0x001CB6B0 | — | BM | live | em_packet_chain_original em_packet_chain_001CB6B0 through em_effects_live (001CFBE0 with copy 0, 001CD520's state block) — test_packet_chain_reference.py (oracle; 1,053 captured chain blocks replayed); test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks) | census L26 / L27 / L39 step (2026-09-25); its blocks are walked as REF tags by the page's consumer since WP-13 (CHAIN_PAGE.md) | S2_opening |
| 0x001CB760 | — | BM | live | em_packet_chain_original em_packet_chain_001CB760 through em_render_context_live (001DDE10, 001E0D70) — test_packet_chain_reference.py; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context | its CALL blocks (the two VU1 program packets, 001DDE10's slot-0xFFF packet) are walked by the page's consumer since WP-13; 001DDE10's is walked over (CHAIN_PAGE.md section 6) | S2_opening |
| 0x001CB800 | — | BM | live | em_packet_chain_original em_packet_chain_001CB800 (001D1EA0's kick) through em_render_context_live — test_packet_chain_reference.py (added); test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context | moved from the boundary list: the splice and slot clear of the chain table; since WP-13 the page it splices is walked as the DMA sends it and drawn (em_chain_page_live, CHAIN_PAGE.md) | S0_title |
| 0x001CB8A0 | — | CL | live | em_packet_chain_original em_packet_chain_001CB8A0 (001D1AE0's) through em_render_context_live — test_packet_chain_reference.py (added); test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context | moved from the boundary list: the frame chain's start tag and the context words +0 / +4 | S0_title |
| 0x001CB900 | — | BM | live | em_packet_chain_original em_packet_chain_001CB900 through em_effects_live (the lanes, 001CFBE0, 001CD520) — test_packet_chain_reference.py (oracle; 1,053 captured chain blocks replayed); test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks) | census L26 / L27 / L39 step (2026-09-25); the presets it REFs are 001D0F20's bank, translated by WP-13 (em_gs_blocks_original) | S2_opening |
| 0x001CB950 | — | BM | live | em_shadow_decal_original em_shadow_decal_001CB950 (001CE300's TEX0 packet) through em_shadow_live (census L29) — test_shadow_decal_reference.py (inside every 001CE300 case, RAM-exact); test_level_smoke.py check_shadow (C: the ORIGINAL 0015BF90 + 001CE300 over the port's sampled inputs write the port's packets) | moved out of the GS/VIF packet build (sprites, flush) boundary group by the render-range rule (section 4); its TEX0 packet is drawn by the chain page since WP-13 | 02_elevator_refusal |
| 0x001CB9B0 | — | CL | live | em_packet_chain_original em_packet_chain_001CB9B0 (inside 001CB900) through em_effects_live — test_packet_chain_reference.py (every mode; 177 captured blend blocks) | moved out of the boundary group (PACKET_CHAIN.md 6.2): it picks 001CB900's blend-state block; the C leaves the default unset, the .s returns 0; live since the census L26 / L27 / L39 step | S2_opening |
| 0x001CBA40 | — | BM | live | em_frame_render_heads (an empty routine: 001D1AE0 performs nothing at its call) — test_frame_render_heads_reference (runs it inside 001D1AE0) | moved from the boundary list | S0_title |
| 0x001CBE10 | — | BM | live | em_message_glyph_original (the message glyph advance), em_hud's atlas advances — test_message_glyph_reference.py, test_message_draw_reference.py |  | S2_opening |

### 3.15 Message glyphs, object registry and face (0x1CC170..0x1D19CF)

20 functions, 2,235 instructions: live 20 (recount 2026-09-26, shadow step).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001CC170 | — | AI | live | em_message_draw_original via em_message_live — test_message_draw_reference.py |  | S2_opening |
| 0x001CC1E0 | — | AW | live | em_message_glyph_original em_message_glyph_cc1e0 via em_message_live — test_message_glyph_reference.py |  | S2_opening |
| 0x001CC3B0 | — | NM | live | em_message_glyph_original (message lines, drawn through the atlas by em_hud_glyph_strip); em_status_hub_ui.c render_text — test_message_glyph_reference, test_status_hub_ui_reference |  | S2_opening |
| 0x001CCF70 | — | NM | live | em_effect_original through em_effects_live (001EA240's depth key; the head sprites' 001CCF70) — test_effect_original_reference.py, test_head_sprite_reference.py; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks) | | S2_opening |
| 0x001CD370 | — | BM | live | em_head_sprite_original (001CFA60's) and em_player_equipment_sprite (001CD520's) through em_effects_live over the render context — test_head_sprite_reference.py; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks) | | S2_opening |
| 0x001CD390 | — | NM | live | em_effect_original em_effect_original_001CD390 through em_shadow_live (001F8D30's look-at, census L29) — test_effect_original_reference.py, test_shadow_actor_route_reference.py (every 001CD390 call replayed through em_effect_original); test_level_smoke.py check_shadow (C: the ORIGINAL 0015BF90 + 001CE300 over the port's sampled inputs write the port's packets) |  | 02_elevator_refusal |
| 0x001CD520 | — | NM | live | em_player_equipment_sprite em_player_equipment_001CD520 through em_effects_live (001F4D40's: the 11 AREA11 glow markers every barrel frame) — test_player_equipment_reference; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks): route 10's five visible markers' primitives byte for byte (colour excepted: rand()) | em_status_models.c w_001CD520 still faults on the D_008104E4 == 1 glow (not reached); its sprites are drawn from the chain page since WP-13 (check_chain_page: route 10's five markers equal the capture page's) | S2_opening |
| 0x001CE300 | — | NM | live | em_shadow_decal_original em_shadow_decal_001CE300 through em_shadow_live (census L29) — test_shadow_decal_reference.py (RAM and scratchpad byte-exact over 31 route cases; the Metal fan pixel against the GS equation); test_level_smoke.py check_shadow (C: the ORIGINAL 0015BF90 + 001CE300 over the port's sampled inputs write the port's packets) | its fans are drawn from the chain page since WP-13 (em_chain_page_live; em_shadow_live_page_drew checks the count); the texture comes from the route captures' GS memory (tools/export_page_textures.py: its uploader is not identified, SHADOW_DECAL.md section 4) | 02_elevator_refusal |
| 0x001CF470 | — | AW | live | em_shadow_decal_original em_shadow_decal_001CF470 (inside 001CE300) through em_shadow_live — test_shadow_decal_reference.py (the census generator cases, digest = CF470_REFERENCE; inside every 001CE300 case); test_level_smoke.py check_shadow (C: the ORIGINAL 0015BF90 + 001CE300 over the port's sampled inputs write the port's packets) |  | 02_elevator_refusal |
| 0x001CF870 | — | AI | live | em_shadow_decal_original em_shadow_decal_001CF870 (001CF470's edge outcode) through em_shadow_live (census L29) — test_shadow_decal_reference.py (leaf sweep; inside every clip) | moved out of the resource / display-object registry boundary group: a clipper leaf (SHADOW_DECAL.md section 1) | 02_elevator_refusal |
| 0x001CF970 | — | AI | live | em_shadow_decal_original em_shadow_decal_001CF970 (001CF470's plane crossing) through em_shadow_live (census L29) — test_shadow_decal_reference.py (leaf sweep; inside every clip) | moved out of the resource / display-object registry boundary group: a clipper leaf (SHADOW_DECAL.md section 1) | 02_elevator_refusal |
| 0x001CFA60 | — | BM | live | em_head_sprite_original through em_effects_live (the head sprites' ramp ticks) — test_head_sprite_reference.py; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks) | | S2_opening |
| 0x001CFB50 | — | BM | live | em_effect_kinds em_effect_kinds_001CFB50 through em_effects_live (every puff handler draw) — test_effect_kinds_reference.py (executed with 001D0540 / 001CD370 / 0011DF78; the captured D_0081F8F0 of beats 05, 08 and 12 reproduced); test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks) | moved out of the boundary group: the puff handlers' transform block; live since the census L26 / L27 / L39 step | 00_panel_no_battery |
| 0x001CFBE0 | — | BM | live | em_head_sprite_original through em_effects_live (the head sprites and every puff handler) — test_head_sprite_reference.py; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks) | its chains are drawn by the sprite program's translation from the chain page since WP-13 (CHAIN_PAGE.md) | S2_opening |
| 0x001D0540 | — | NM | live | em_effect_kinds em_effect_kinds_001D0540 (from the .s) through em_effects_live — test_effect_kinds_reference.py | moved out of the boundary group: 001CFB50's depth scale over 0x70003AC0; live since the census L26 / L27 / L39 step | 00_panel_no_battery |
| 0x001D0690 | — | BM | live | em_roger_actor_original via em_area11_roger (census L22) — test_roger_actor_original_reference.py; test_level_smoke.py (roger: route 14 row for row) |  | S2_opening |
| 0x001D06D0 | — | BM | live | em_roger_actor_original via em_area11_roger and the script host's w_001D06D0 (census L22) — test_roger_actor_original_reference.py; test_level_smoke.py (roger: route 14 row for row) |  | S2_opening |
| 0x001D06E0 | — | BM | live | em_player_face_host.c — test_player_face_host.py |  | S2_opening |
| 0x001D0720 | — | NM | live | em_opening_face.c — test_opening_face_reference; test_rand_order / check_rand_order (the draws' stage positions, RAND_ORDER.md) | during the opening its calls come from em_opening_actor inside the opening controller (design risk 2), not from Roger's owner and the player stage 00183090 (section 1.32) | S2_opening |
| 0x001D0C70 | — | BM | live | 00183090's call (em_player_stage_commit w001D0C70) -> em_player_face_host's tick of 001D0720 (em_opening_face) on the attached face (census L22) — test_player_cinematic_reference; test_opening_face_reference; test_level_smoke.py (roger: route 14 row for row) | Roger's own face slot ticks through em_area11_roger's w_001D0720 | S2_opening |

### 3.16 Render heads, projection, lighting, shadow, veil particles (0x1D19D0..0x1DAFFF)

76 functions, 6,076 instructions: live 64, verified-unbound 12 (recount 2026-09-27, load veil step).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001D19D0 | — | BM | verified-unbound | em_frame_render_heads em_frh_001D19D0 — test_frame_render_heads_reference | reported no-effect binding UM_001D19D0 (render init) | S0_title* |
| 0x001D19E0 | — | BM | verified-unbound | em_frame_render_heads em_frh_001D19E0 — test_frame_render_heads_reference | its first callee skin_arena_init runs live (um_001D19E0 -> em_rcl_skin_arena_init, object-unit step); the rest is a reported no-effect binding | S1_newgame_load* |
| 0x001D1AE0 | — | NM | live | em_frame_render_heads em_frh_001D1AE0 through em_render_context_live (main-loop step B, em_frame_set_step_b; its 001CBA40 is empty) — test_frame_render_heads_reference (added: unit + route); test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context | moved from the boundary list (GS/VIF packet build) by the render context step: it sets the context's slot index and cursors | S0_title |
| 0x001D1C50 | — | NM | live | em_frame_render_heads em_frh_001D1C50 through em_render_context_live (w_001D1C50: both world variants, the status frame) — test_frame_render_heads_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context | em_render_001D1C50 serves only a scene without the render context | S0_title |
| 0x001D1EA0 | — | BM | live | em_frame_render_heads em_frh_001D1EA0 through em_render_context_live (w_001D1EA0, then the renderer's presentation) — test_frame_render_heads_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S0_title |
| 0x001D1EF0 | — | BM | live | em_frame_render_heads em_frh_001D1EF0 through em_rcl_001D1EF0 (0x1AE040 states 0 and 5, 001ADF00, 001AD4E0; its kick's page drawn by em_render_001D1EF0) — test_frame_render_heads_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14, section 1.27); test_level_smoke.py check_render_context (step V lists) | chain C7 step V (section 1.27); before the area bind (the New Game bring-up 001ACEC0 / 001AD360) still reported (UM_001D1EF0): its frame head needs the views the area load hands over | S0_title |
| 0x001D1F20 | — | CL | live | em_load_veil_particles em_load_veil_particles_001D1F20 through em_render_context_live (001D1AE0, 001D6B10) — test_load_veil_particles_reference.py; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S0_title |
| 0x001D1F80 | — | CL | live | em_load_veil_particles em_load_veil_particles_001D1F80 through em_rcl_001D1F80 (em_owner_draw_live's 001CA990 worker, object-unit step) — test_load_veil_particles_reference.py, test_owner_services_reference.py, test_owner_draw_reference.py (D) |  | S0_title |
| 0x001D1FF0 | — | CL | live | em_load_veil_particles em_load_veil_particles_001D1FF0 through em_render_context_live (001D1AE0, 001DDE10, 001D6930) — test_load_veil_particles_reference.py; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S0_title |
| 0x001D2040 | — | CL | live | em_load_veil_particles em_load_veil_particles_001D2040 through em_render_context_live (001D1AE0, 001D6930) — test_load_veil_particles_reference.py; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S0_title |
| 0x001D2090 | — | CL | live | em_owner_draw_original (vif_append_ref_tag inside 001D37D0 / 001D3AD0: the arena REF 1, context +0x50, the CALL) through em_owner_draw_live (object-unit step) — test_owner_draw_reference (B, D); tools/test_object_unit_reference.py | its other callers' packets (the level, effects) are not this translation | S2_opening |
| 0x001D2110 | — | BM | live | em_frame_kick (001D2300's helper) through em_rcl_001D2300 — test_render_context_live_reference (the live composition vs the original over beats 00..14, section 1.27); test_level_smoke.py check_render_context (step V lists) | chain C7 step V (section 1.27); moved from the GS/VIF boundary group | S0_title |
| 0x001D2130 | — | BM | live | em_frame_kick (001D2300's helper) through em_rcl_001D2300 — test_render_context_live_reference (the live composition vs the original over beats 00..14, section 1.27); test_level_smoke.py check_render_context (step V lists) | chain C7 step V (section 1.27); moved from the GS/VIF boundary group | S0_title |
| 0x001D2160 | — | BM | live | em_frame_kick (001D2300's helper) through em_rcl_001D2300 — test_render_context_live_reference (the live composition vs the original over beats 00..14, section 1.27); test_level_smoke.py check_render_context (step V lists) | chain C7 step V (section 1.27); moved from the GS/VIF boundary group | S0_title |
| 0x001D2180 | — | BM | live | em_frame_kick (001D2300's helper) through em_rcl_001D2300 — test_render_context_live_reference (the live composition vs the original over beats 00..14, section 1.27); test_level_smoke.py check_render_context (step V lists) | chain C7 step V (section 1.27); moved from the GS/VIF boundary group | S0_title |
| 0x001D21B0 | — | BM | verified-unbound | em_render_context em_render_context_001D21B0 — test_render_context_reference | formerly a GS/VIF boundary row; a render-context helper | S2_opening |
| 0x001D21E0 | — | NM | live | em_frame_kick (001D2300's helper) through em_rcl_001D2300 — test_render_context_live_reference (the live composition vs the original over beats 00..14, section 1.27); test_level_smoke.py check_render_context (step V lists) | chain C7 step V (section 1.27): its closing tag; the DMA / VIF1 kick (0011B9E0, 0010BAA0, 00101F08) is the renderer's presentation, its list address compared; moved from the GS/VIF boundary group | S0_title |
| 0x001D2300 | — | NM | live | em_frame_kick em_frame_kick_001D2300 through em_rcl_001D2300 at main-loop step V (em_frame_step, every iteration) — test_render_context_live_reference (the live composition vs the original over beats 00..14, section 1.27); test_level_smoke.py check_render_context (step V lists); test_background_reference.py | chain C7 step V (section 1.27): the list, the clear it selects (+0x3A0 / +0x420 under flag 3) and 001E0DF0 exact; its hardware kick is the renderer's presentation; the renderer's channel-3 gate reads its gate code (em_rcl_001D2300_calls_001E0DF0); the draw environments' SCISSOR (boot bank A) is not modelled (RENDER_CONTEXT.md 9.4) | S0_title |
| 0x001D2580 | — | BM | live | em_frame_kick em_frame_kick_001D2580 through em_rcl_001D2580 at main-loop step W (the field model em_frame_d810E88) — test_render_context_live_reference (the live composition vs the original over beats 00..14, section 1.27); test_level_smoke.py check_render_context (step V lists) (+0x98 = 1 - slot on every tick) | chain C7 step V (section 1.27); moved from the GS/VIF boundary group (MAIN_LOOP_AND_GAP.md 8: a game-memory store) | S0_title |
| 0x001D2590 | — | AI | live | em_frame_render_heads em_frh_001D2590 (inside 001D2610) through em_render_context_live — test_frame_render_heads_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S2_opening |
| 0x001D25F0 | — | BM | live | em_frame_render_heads em_frh_001D25F0 through em_render_context_live (the interaction host, the script host, the timeline, the opening runtime; the boot store) — test_frame_render_heads_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context | the opening runtime's zoom VALUES are still its L33 stand-in's | S2_opening |
| 0x001D2610 | — | BM | live | em_frame_render_heads em_frh_001D2610 through em_render_context_live (SCOPE_ZOOM_ZERO, CONFIGURE, the script host, the opening) — test_frame_render_heads_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context | em_camera_scope_zoom and the host's 0x43F02F4F constant removed | S2_opening |
| 0x001D2710 | — | BM | live | em_render_context em_render_context_001D2710 (001D2910's) through em_render_context_live — test_render_context_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S0_title |
| 0x001D2730 | — | NM | live | em_render_context em_render_context_001D2730 (001D2830's, flags below 0x20) through em_render_context_live — test_render_context_reference (added: every a0 case, both a1 senses, the old bit both ways); test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context | moved from the boundary list: context-flag logic with the flag-0 fog block moves | S0_title |
| 0x001D2830 | — | AW | live | em_frame_render_heads em_frh_001D2830 through em_render_context_live (001D1C50's, 001C1DC0's and the script host's calls) — test_frame_render_heads_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context | the status / teardown / load-veil (3, 1) registrations run on the context since chain C7 step V (section 1.27; UM_001D2830 removed) | S0_title |
| 0x001D2910 | — | AW | live | em_render_context em_render_context_001D2910 through em_render_context_live — test_render_context_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S0_title |
| 0x001D2960 | — | BM | live | em_frame_render_heads em_frh_001D2960 (inside 001D1C50) through em_render_context_live — test_frame_render_heads_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context | the native world pass draws with its V / zoom; em_snow_projection_matrices removed | S0_title |
| 0x001D2D20 | — | BM | live | em_frame_render_heads em_frh_001D2D20 (inside 001D2960) through em_render_context_live — test_frame_render_heads_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S0_title |
| 0x001D2DE0 | — | BM | live | em_render_context em_render_context_001D2DE0 through em_render_context_live (001D1AE0, 001E0CC0) — test_render_context_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S0_title |
| 0x001D2E00 | — | BM | live | em_render_context em_render_context_001D2E00 (001E0D70's) through em_render_context_live — test_render_context_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S1_newgame_load |
| 0x001D2E20 | skin_arena_init | BM | live | em_skin_arena_init.h em_skin_arena_init_001D2E20 through em_rcl_skin_arena_init (001D19E0's binding at the area load; object-unit step) — tools/test_object_unit_reference.py (the records' templates equal the ELF's in every capture); test_render_context_live_reference | moved in from the GS/VIF boundary list (section 4) | S1_newgame_load |
| 0x001D30A0 | — | NM | live | em_frame_render_heads em_frh_001D30A0 (inside 001D1C50) through em_render_context_live — test_frame_render_heads_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context | the Metal world fog reads its skin-record copy of +0xA0 | S0_title |
| 0x001D37D0 | — | NM | live | em_owner_draw_original through em_owner_draw_live (object-unit step) — test_owner_draw_reference; tools/test_object_unit_reference.py | moved in from the GS/VIF boundary list (section 4) | S2_opening |
| 0x001D38A0 | — | CL | live | em_owner_draw_original em_owner_draw_001D38A0 through em_owner_draw_live (object-unit step) — test_owner_draw_reference; tools/test_object_unit_reference.py | the per-owner DMA unit; formerly a GS/VIF boundary row | S2_opening |
| 0x001D38F0 | — | BM | live | em_owner_draw_original (001CA940's thunk to 001D38A0) through em_owner_draw_live (object-unit step) — test_owner_draw_reference | moved in from the GS/VIF boundary list (section 4) | S2_opening |
| 0x001D3AD0 | — | BM | live | em_owner_draw_original through em_owner_draw_live (the clip pass's tags; object-unit step) — test_owner_draw_reference; tools/test_object_unit_reference.py | moved in from the GS/VIF boundary list (section 4) | S2_opening |
| 0x001D3BA0 | — | BM | live | em_owner_draw_original em_owner_draw_001D3BA0 through em_owner_draw_live (object-unit step) — test_owner_draw_reference; tools/test_object_unit_reference.py | the clip unit; formerly a GS/VIF boundary row | S2_opening |
| 0x001D3C30 | — | BM | live | em_owner_draw_original (001CA940's thunk to 001D3BA0) through em_owner_draw_live (object-unit step) — test_owner_draw_reference | moved in from the GS/VIF boundary list (section 4) | S2_opening |
| 0x001D4B50 | — | BM | live | em_gfx_shadow_receiver's class-2 re-pass (001D4FB0, 001D1F80(0,2,6), 001D4B50, 001D4CD0 as one call; the 0023E8A0 kernel in em_vu1_shadow_clip.h) through em_shadow_live (the one owner since section 1.28: em_rvr_001D4B50 removed) — test_shadow_original_reference (F / G: the clip kernel kick for kick, the backend's data memory); test_render_verify_rest_reference (the original 001DA6A0 calls it exactly for the plan's class-2 receivers); test_level_smoke.py check_shadow (B: the ORIGINAL 001DA6A0 over the port's sampled inputs builds the port's plan) | the GS side is the Metal backend's (SHADOW_ORIGINAL.md "GS side" approximations) | S2_opening |
| 0x001D4CD0 | — | BM | live | em_shadow_original (w_receiver_begin) and em_gfx_shadow_receiver_begin through em_shadow_live — test_shadow_original_reference.py (TEX0, the state blocks, the UV upload); test_level_smoke.py check_shadow (B: the ORIGINAL 001DA6A0 over the port's sampled inputs builds the port's plan) |  | S2_opening |
| 0x001D4FB0 | — | BM | live | em_shadow_original (the receiver list) and em_gfx_shadow_receiver through em_shadow_live — test_shadow_original_reference.py (C, F, G); test_level_smoke.py check_shadow (B: the ORIGINAL 001DA6A0 over the port's sampled inputs builds the port's plan) |  | S2_opening |
| 0x001D52E0 | — | BM | verified-unbound | em_render_context em_render_context_001D52E0 — test_render_context_reference | its live caller 001C1E70 (em_rvr_001C1E70 through em_render_context_live) reaches a reported no-effect binding instead (em_scene_bindings rcl_grid_header, UM_001D52E0; measured 2026-09-26) | S1_newgame_load |
| 0x001D5370 | — | NM | verified-unbound | em_render_context em_render_context_001D5370 — test_render_context_reference |  | S2_opening |
| 0x001D5C80 | — | NM | live | em_shadow_original receivers through em_shadow_live — test_render_verify_rest_reference (intercepted inside the executed 001DA6A0); test_shadow_original_reference; test_level_smoke.py check_shadow (B: the ORIGINAL 001DA6A0 over the port's sampled inputs builds the port's plan) (the receiver sequence and classes) | the grid and objects come from the exported static-object bank (assets/scene_snow/shadow_receivers.emsr); 001D52E0 is not bound | S2_opening |
| 0x001D63B0 | — | NM | live | em_load_veil_particles, through em_rcl_0021B1B0 (em_scene_bindings' veil_0021B1B0 at 0021B550, the load veil; step V's list drawn by em_load_veil_live, LOAD_VEIL_PARTICLES.md 3) — test_load_veil_particles_reference.py; test_level_smoke.py check_load_veil (the live run equals the ORIGINAL 0021B1B0 executed at the same call, section 1.35) | the load spans one tick at host speed: one black veil frame | S1_newgame_load* |
| 0x001D6930 | — | NM | live | em_load_veil_particles em_load_veil_particles_001D6930 (001D6B10's) through em_render_context_live — test_load_veil_particles_reference.py; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S1_newgame_load |
| 0x001D6B10 | — | CL | live | em_render_context em_render_context_001D6B10 (001DDE10's) through em_render_context_live — test_render_context_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context | its packets reach no native consumer | S2_opening |
| 0x001D6B60 | — | BM | live | em_load_veil_particles, through em_rcl_0021B1B0 (em_scene_bindings' veil_0021B1B0 at 0021B550, the load veil; step V's list drawn by em_load_veil_live, LOAD_VEIL_PARTICLES.md 3) — test_load_veil_particles_reference.py; test_level_smoke.py check_load_veil (the live run equals the ORIGINAL 0021B1B0 executed at the same call, section 1.35) | the load spans one tick at host speed: one black veil frame | S1_newgame_load* |
| 0x001D6BA0 | — | NM | live | em_load_veil_particles em_load_veil_particles_001D6BA0 (001DDE10's) through em_render_context_live — test_load_veil_particles_reference.py; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S1_newgame_load |
| 0x001D6C90 | — | NM | live | em_render_context em_render_context_001D6C90 (001DDE10's) through em_render_context_live — test_render_context_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context | the four-sprite packet; walked over by the chain page's consumer (not drawn: CHAIN_PAGE.md section 6) | S2_opening |
| 0x001D6E60 | — | NM | live | em_load_veil_particles em_load_veil_particles_001D6E60 (001D6930's) through em_render_context_live — test_load_veil_particles_reference.py; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S1_newgame_load |
| 0x001D7080 | — | AW | live | em_load_veil_particles, through em_rcl_0021B1B0 (em_scene_bindings' veil_0021B1B0 at 0021B550, the load veil; step V's list drawn by em_load_veil_live, LOAD_VEIL_PARTICLES.md 3) — test_load_veil_particles_reference.py; test_level_smoke.py check_load_veil (the live run equals the ORIGINAL 0021B1B0 executed at the same call, section 1.35) | live through 001DFA40 only; its other callers on the route (the background 001E1E60, the static world) are their own rows | S1_newgame_load |
| 0x001D7B30 | — | BM | live | em_actor_light_001D89D0 em_actor_light_001D7B30 (001D8FD0's) through em_render_context_live over the exported D_00251C50 — test_actor_light_001d89d0_reference; test_packet_chain_reference.py (001D8FD0 whole); test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context | the actor lighting's room rig is still the manifest's rig lines (lane L40) | S1_newgame_load |
| 0x001D7BB0 | — | BM | live | em_point_light.c — test_point_light_reference | on the render context's slots +0x210..+0x221F (em_rcl_point_lights; section 1.27) | S1_newgame_load* |
| 0x001D7C30 | — | NM | live | em_point_light.c — test_point_light_reference; test_level_smoke.py check_sway (the original over the port's pool and draws on sampled ticks) and check_rand_order | on the render context's slots +0x210..+0x221F (em_rcl_point_lights; section 1.27) | S0_title |
| 0x001D7FA0 | — | BM | live | em_point_light.c — test_point_light_reference | on the render context's slots +0x210..+0x221F (em_rcl_point_lights; section 1.27) | S1_newgame_load* |
| 0x001D8060 | — | BM | verified-unbound | em_frame_render_heads em_frh_001D8060 — test_frame_render_heads_reference |  | S1_newgame_load* |
| 0x001D80B0 | — | BM | verified-unbound | em_frame_render_heads em_frh_001D80B0 — test_frame_render_heads_reference |  | S1_newgame_load* |
| 0x001D8130 | — | BM | live | em_lighting.c — test_actor_lighting_reference.py | several actor models still carry the stand-in light (H18/WP-13) | S2_opening |
| 0x001D8270 | — | AW | verified-unbound | em_lighting.c em_lighting_fold_gate; em_actor_light_001D89D0 em_actor_light_001D8270 — test_actor_lighting_reference.py (part E); test_actor_light_001d89d0_reference | not called live: em_render_frame.c char_rig_build assumes the gate passes for the opening actors ("pass the 001D8270 gate") | S2_opening |
| 0x001D8340 | — | BM | live | em_lighting.c — test_actor_lighting_reference.py | several actor models still carry the stand-in light (H18/WP-13) | S2_opening |
| 0x001D8690 | — | NM | verified-unbound | em_lighting.c em_lighting_actor_rgb; em_actor_light_001D89D0 em_actor_light_001D8690 — test_actor_lighting_reference.py (part E); test_actor_light_001d89d0_reference | not called live: the actor RGB / self-glow stays the renderer's post-draw tint (em_render_frame.c char_rig_build note), not asserted equivalent | S2_opening |
| 0x001D88B0 | — | BM | verified-unbound | em_frame_render_heads em_frh_001D88B0 — test_frame_render_heads_reference | em_lighting.c face lighting mode | S2_opening |
| 0x001D89D0 | — | NM | live | em_actor_light_001D89D0 as 001C7420's w_001D89D0 through em_owner_draw_live (object-unit step) — test_actor_light_001d89d0_reference; test_level_smoke.py check_owner_units (the units against the route snapshots) (B and the rig lanes equal; the fold rows only where the point-light slots do); the camera fill (+0x02 bit 0x20) runs live for the player since section 1.23, checked over its 15 captured owner-frames; em_lighting.c for the legacy actor chain — test_actor_lighting_reference.py | the legacy chain (Roger, legacy-drawn owners, the player in the opening's reported frames) still runs char_rig_build + em_lighting_matrices, checked only under its port contract; several actor models still carry the stand-in light (H18/WP-13) | S2_opening |
| 0x001D8BF0 | — | BM | live | em_roger_actor_original via em_area11_roger (census L22) and, since section 1.23, 001AF5C0's call over the player record (em_scene_bindings.c wipe_001D8BF0) — test_roger_actor_original_reference.py; test_level_smoke.py (roger: route 14 row for row) |  | S1_newgame_load |
| 0x001D8C20 | — | BM | live | em_owner_draw_live (context +0x246C = 0 before 001C7420) and em_lighting.c — test_owner_draw_reference (D: the lighting mode), test_actor_lighting_reference.py | several legacy actor models still carry the stand-in light (H18/WP-13) | S2_opening |
| 0x001D8C30 | — | NM | verified-unbound | em_frame_render_heads em_frh_001D8C30 — test_frame_render_heads_reference | em_effect_color.h / em_status_models.c draw | S2_opening |
| 0x001D8FD0 | — | BM | live | em_packet_chain_original em_packet_chain_001D8FD0 through em_render_context_live (001C1DC0's 001C1E80) — test_packet_chain_reference.py (the whole original, 001D7B30 / 001B0070 / 0021B8E0 unhooked); test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context | the manifest's fog line now serves only scenes without the render context | S1_newgame_load |
| 0x001D9070 | — | NM | verified-unbound | em_frame_render_heads em_frh_001D9070 — test_frame_render_heads_reference | render init; no port counterpart (UM_001D19D0) | S0_title* |
| 0x001D98A0 | — | NM | live | em_shadow_original through em_shadow_live — test_shadow_original_reference.py; test_level_smoke.py check_shadow (B: the ORIGINAL 001DA6A0 over the port's sampled inputs builds the port's plan) (D_00817F20..D_00817FF0 and ctx+0x24B0) |  | S2_opening |
| 0x001D9EE0 | — | BM | live | em_shadow_original and em_gfx_shadow_silhouette through em_shadow_live — test_shadow_original_reference.py (the silhouette VP and bone palette; the Metal target texel for texel); test_level_smoke.py check_shadow (B: the ORIGINAL 001DA6A0 over the port's sampled inputs builds the port's plan) (the silhouette VP) | the proxy mesh is assets/player_shadow.emdl (D_0028A490[0x28], ../Extermination/tools/export_shadow_proxy.py) | S2_opening |
| 0x001DA080 | — | BM | live | em_shadow_original (inline) through em_shadow_live — test_render_verify_rest_reference (both outcomes of every branch asserted); test_shadow_original_reference; test_level_smoke.py check_shadow (B: the ORIGINAL 001DA6A0 over the port's sampled inputs builds the port's plan) (D_00817FA0 / D_00817FB0) |  | S2_opening |
| 0x001DA1E0 | — | BM | live | em_gfx_shadow_alpha_clear (001DA290's two-triangle strip) through em_shadow_live (the one owner since section 1.28: em_rvr_001DA1E0 / em_rvr_001DA290 removed) — test_shadow_original_reference (F: the draw's GS state against the captured chains); test_render_verify_rest_reference (the original's 001DA290 / 001DA1E0 arguments) |  | S2_opening |
| 0x001DA290 | — | CL | live | em_shadow_original and em_gfx_shadow_alpha_clear through em_shadow_live — test_shadow_original_reference.py (F) |  | S2_opening |
| 0x001DA310 | — | NM | live | em_shadow_original box_pass and em_gfx_shadow_box through em_shadow_live — test_render_verify_rest_reference; test_shadow_original_reference (the box uploads, 00237180 / 00239C90); test_level_smoke.py check_shadow (B: the ORIGINAL 001DA6A0 over the port's sampled inputs builds the port's plan) (both box uploads) |  | S2_opening |
| 0x001DA6A0 | — | NM | live | em_shadow_original em_shadow_original_001DA6A0 through em_shadow_live (the player's post-step, census L29); em_roger_actor_original — test_shadow_original_reference.py, test_shadow_actor_route_reference.py, test_roger_actor_original_reference.py; test_level_smoke.py check_shadow (B: the ORIGINAL 001DA6A0 over the port's sampled inputs builds the port's plan) | Roger's 001BA580 still reaches it as a reported no-effect binding (UM_001DA6A0: his kind 0x29 proxy D_0028A490[0x29] is not exported, census L22) | S2_opening |

### 3.17 Render context, background, weather and snow (0x1DB000..0x1EEFFF)

30 functions, 3,752 instructions: live 25, verified-unbound 5 (recount 2026-09-27, load veil step).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001DD7B0 | — | NM | verified-unbound | em_render_context em_render_context_001DD7B0 — test_render_context_reference | not bound | S1_newgame_load* |
| 0x001DD940 | — | BM | verified-unbound | em_render_context em_render_context_001DD940 — test_render_context_reference | not bound | S1_newgame_load* |
| 0x001DD950 | — | BM | live | em_render_context em_render_context_001DD950 through em_render_context_live (001DD980's tail: the camera, the interaction host, the script host) — test_render_context_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context | the camera's EmInteractionProjection copy removed | S1_newgame_load |
| 0x001DD980 | — | BM | live | em_interaction_projection.c (also the AREA11 script host's op00 publication, census L19) — test_interaction_projection_reference; test_level_smoke.py (truck_preview) | the camera's calls (0018BC20 action 8, 001B0460) publish the live camera's projection since census L13..L16 | S1_newgame_load |
| 0x001DDA00 | — | BM | live | em_render_context em_render_context_001DDA00 (001D1EA0's) through em_render_context_live — test_render_context_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context | 001DE920 / 001DDB70 / 001DFF70 / 001DF110 bound to a fault (never reached on the route) | S2_opening |
| 0x001DDAA0 | — | BM | live | em_render_context em_render_context_001DDAA0 through em_render_context_live — test_render_context_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S2_opening |
| 0x001DDE10 | — | BM | live | em_render_context em_render_context_001DDE10 through em_render_context_live — test_render_context_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context | the four sprites' packets are walked over by the chain page's consumer, not drawn: they sample the frame buffer (CHAIN_PAGE.md section 6) | S2_opening |
| 0x001DEEE0 | — | BM | live | em_render_context em_render_context_001DEEE0 through em_render_context_live — test_render_context_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context | its records set up by the boot 001DEDE0 (em_rcl_init) | S2_opening |
| 0x001DFA40 | — | NM | live | em_load_veil_particles, through em_rcl_0021B1B0 (em_scene_bindings' veil_0021B1B0 at 0021B550, the load veil; step V's list drawn by em_load_veil_live, LOAD_VEIL_PARTICLES.md 3) — test_load_veil_particles_reference.py; test_level_smoke.py check_load_veil (the live run equals the ORIGINAL 0021B1B0 executed at the same call, section 1.35) | the load spans one tick at host speed: one black veil frame | S1_newgame_load* |
| 0x001E0C30 | — | BM | verified-unbound | em_render_context em_render_context_001E0C30 — test_render_context_reference | not bound | S1_newgame_load* |
| 0x001E0C60 | — | BM | live | em_render_context em_render_context_001E0C60 (001D2910's) through em_render_context_live — test_render_context_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S0_title |
| 0x001E0C80 | — | CL | live | em_render_context em_render_context_001E0C80 (001D2830's) through em_render_context_live — test_render_context_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S1_newgame_load |
| 0x001E0CC0 | — | BM | live | em_render_context em_render_context_001E0CC0 through em_render_context_live (w_001E0CC0 at the status close) — test_render_context_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S1_newgame_load |
| 0x001E0CF0 | — | BM | verified-unbound | em_render_verify_rest em_rvr_001E0CF0 — test_render_verify_rest_reference | the module is linked; em_rvr_001E0CF0 is not called (its caller 001C1D00 is not bound) | S2_opening |
| 0x001E0D70 | — | BM | live | em_render_context em_render_context_001E0D70 (001D1EA0's) through em_render_context_live — test_render_context_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S2_opening |
| 0x001E0DF0 | — | BM | live | em_render_context em_render_context_001E0DF0 through step V (em_frame_kick) — test_render_context_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14, section 1.27); test_level_smoke.py check_render_context (step V lists) | chain C7 step V (section 1.27); the port builds no +0x1D8 list (001C1D00 unbound), so it CALLs nothing there | S1_newgame_load |
| 0x001E1010 | — | BM | verified-unbound | em_render_context em_render_context_001E1010 — test_render_context_reference | not bound | S1_newgame_load* |
| 0x001E1E60 | — | NM | live | em_background_gs, em_gfx_metal: em_gfx_background_draw first in every world frame, gated as 001D2300 / 001E0DF0 gate its CALL (flag 0x20; flag 0x21 standing for the +0x1D8 list 001E0CF0 builds) (status UI step 2026-09-26 (section 1.21)) — test_background_reference.py (sky metric: 0 black) |  | S2_opening |
| 0x001E2260 | — | BM | live | em_render_verify_rest em_rvr_001E2260 (001C1F50's) through em_render_context_live — test_render_verify_rest_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S1_newgame_load |
| 0x001E2270 | — | BM | live | em_render_verify_rest em_rvr_001E2270 (001C1F50's, the exported D_00250F30) through em_render_context_live — test_render_verify_rest_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S1_newgame_load |
| 0x001E2290 | — | BM | live | em_head_sprite_original through em_effects_live — test_head_sprite_reference | | S2_opening* |
| 0x001E23A0 | — | BM | live | em_head_sprite_original through em_effects_live — test_head_sprite_reference; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks) | | S2_opening* |
| 0x001E2560 | — | BM | live | em_head_sprite_original em_head_sprite_original_tick through em_effects_live on the pool nodes (the player's and Roger's) — test_head_sprite_reference; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks) and check_head_sprites (every tick's sub-state, wait, ramp and scalar over the port's own draws) | | S2_opening |
| 0x001E55F0 | — | NM | live | em_weather.c via em_snow_runtime (node 001E55F0) — test_weather_reference |  | S2_opening |
| 0x001E67C0 | — | NM | live | em_snow.c — test_snow_tiles_reference | its 0021B9A0(2, 0, 0) / (3, 0, 300) / (1, 0, 0) run on the render context since the census L26 step (em_snow_runtime; the draw takes the context's fog quadword) | S2_opening |
| 0x001EA240 | — | BM | live | em_effect_original em_effect_original_001EA240 through em_effects_live on the pool nodes (em_area11_bindings, callback 0x1EA240) — test_effect_original_reference.py; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks): the effect nodes' state, subtype, step, limit, accumulator (and route 08's positions) | | 00_panel_no_battery |
| 0x001EBF10 | — | NM | live | em_effect_kinds em_effect_kinds_001EBF10 through em_effects_live (the truck puffs) — test_effect_kinds_reference; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks) | | 08_truck_crossing |
| 0x001EC1F0 | — | NM | live | em_effect_kinds em_effect_kinds_001EC1F0 through em_effects_live — test_effect_kinds_reference | | 05_boxes |
| 0x001EC3F0 | — | NM | live | em_effect_kinds em_effect_kinds_001EC3F0 through em_effects_live (the snow footsteps) — test_effect_kinds_reference | | 00_panel_no_battery |
| 0x001EC470 | — | NM | live | em_effect_kinds em_effect_kinds_001EC470 through em_effects_live (the slide) — test_effect_kinds_reference | | 06_hill_slide |

### 3.18 Effects (0x1EF000..0x1F8FFF)

31 functions, 3,127 instructions: live 24, verified-unbound 7 (recount 2026-09-26, shadow step).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001EF940 | — | BM | live | em_effect_original through em_effects_live (every spawn; no first-level record carries a sound) — test_effect_original_reference.py | | S1_newgame_load |
| 0x001EF9D0 | — | NM | live | em_effect_original through em_effects_live (the one allocator: the puffs, the head sprites, the weather node) — test_effect_original_reference.py, test_head_sprite_reference.py, test_player_misc_workers_reference.py; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks) | em_player_misc_001EFE00's spawn view is not bound (faults; not reached) | S1_newgame_load |
| 0x001EFD20 | — | BM | live | em_effect_original through em_effects_live (the truck's 32 spawns, the weather node 0x80000017) — test_effect_original_reference.py, test_truck_original_reference.py; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks): route 08's eight truck puffs bit for bit | | S1_newgame_load |
| 0x001EFD90 | — | BM | live | em_effect_original through em_effects_live (the footsteps, the climb, the slide, the walk's skid, the crates) — test_effect_original_reference.py, test_player_climb_reference.py, test_player_fall_reference.py; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks) | | 00_panel_no_battery |
| 0x001F0120 | — | BM | live | em_head_sprite_original spawn through em_effects_live (0015C420's key 0x3B, Roger's 0x47) — test_head_sprite_reference.py; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks): the two nodes' key, owner, bone and offset | | S2_opening* |
| 0x001F0310 | — | BM | live | em_effect_kinds em_effect_kinds_001F0310 through em_effects_live at 001AFCA0's 001D0660 — test_effect_kinds_reference | 001D1C10 (the movie frame) does not run | S0_title* |
| 0x001F0360 | — | BM | live | em_effect_manager em_effect_manager_001F0360 through em_effects_live at w_001F0360 (both variants) — test_effect_manager_reference; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks): every barrel frame 11 markers and 6 lanes, the lanes' packets byte for byte | | S2_opening |
| 0x001F03D0 | — | BM | live | em_effect_kinds em_effect_kinds_001F03D0 through em_effects_live (001F0310's) — test_effect_kinds_reference; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks) (the ring lanes' packet 2) | | S0_title* |
| 0x001F0720 | — | NM | live | em_effect_manager em_effect_manager_001F0720 through em_effects_live (the barrel's six lanes) — test_effect_manager_reference; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks): packets 1..3 byte for byte in all five, packet 4 in 10 and 14 | the lane-3 parameter quadwords are not compared (the snapshots hold bytes no first-level routine wrote there); its lanes run the lane program's translation from the chain page since WP-13 (no route slot is active: nothing drawn, as in the captures) | S2_opening |
| 0x001F0A60 | — | AU | live | em_effect_manager em_effect_manager_001F0A60 through em_effects_live (the map pickup's glint, 001F1180's draw block) — test_effect_manager_reference (the captured glint packets of beats 03 / 05 / 13) | its line strips are drawn from the chain page since WP-13 | S3_first_control_idle |
| 0x001F1110 | — | BM | live | em_pickup_items_original em_pickup_aura_001F1110 (bound in em_area11_interaction_host.c) — test_pickup_items_reference (executes 001F1110) |  | S2_opening* |
| 0x001F1180 | — | NM | live | em_pickup_items_original em_pickup_aura_001F1180 (bound in em_area11_interaction_host.c) with its draw block em_effect_manager_aura_draw through em_effects_live — test_pickup_items_reference (executes 001F1180 with its SDK leaves), test_effect_manager_reference | whole since the census L26 step: the no-op aura_draw stand-in is replaced | S2_opening |
| 0x001F3FA0 | — | NM | live | em_effect_kinds em_effect_kinds_001F3FA0 through em_effects_live (001F0310's) — test_effect_kinds_reference | | S0_title* |
| 0x001F40C0 | — | BM | live | em_effect_manager em_effect_manager_001F40C0 through em_effects_live — test_effect_manager_reference | no particle entity is live on the route (001F3620 / 001F3E30 fault when reached) | S2_opening |
| 0x001F4D40 | — | AI | live | em_effect_manager em_effect_manager_001F4D40 through em_effects_live (the glow markers) — test_effect_manager_reference; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks) and check_marker_colour (the original over the port's draws; the capture's markers over its own) | | S2_opening |
| 0x001F54E0 | — | AW | live | em_effect_kinds em_effect_kinds_001F54E0 (the indicator children's colour; em_effect_delta deleted) — test_effect_kinds_reference, test_census_unverified_reference |  | S2_opening |
| 0x001F5640 | — | BM | live | em_effect_kinds em_effect_kinds_001F5640 through em_effects_live (001F5C20's) — test_effect_kinds_reference | | S2_opening |
| 0x001F5940 | — | BM | live | em_effect_kinds em_effect_kinds_001F5940 through em_effects_live — test_effect_kinds_reference; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks) | | S2_opening |
| 0x001F5C20 | — | BM | live | em_effect_kinds em_effect_kinds_001F5C20 through em_effects_live (the barrel's second call) — test_effect_kinds_reference; test_level_smoke.py check_effects (the route snapshots 08, 10, 11, 13, 14 at their aligned ticks) | | S2_opening |
| 0x001F5CA0 | — | BM | live | em_effect_kinds em_effect_kinds_001F5CA0 through em_effects_live (001F6210's) — test_effect_kinds_reference | | S2_opening |
| 0x001F6210 | — | BM | live | em_effect_manager em_effect_manager_001F6210 through em_effects_live — test_effect_manager_reference | AREA11's key has no list (its loop workers fault when reached) | S2_opening |
| 0x001F6640 | — | BM | verified-unbound | em_effect_kinds em_effect_kinds_001F6640 — test_effect_kinds_reference | the room point-light list is resolved offline by tools/export_point_lights.py and adopted by em_point_light.c (critic 7.2) | S1_newgame_load* |
| 0x001F66F0 | — | BM | verified-unbound | em_effect_kinds em_effect_kinds_001F66F0 — test_effect_kinds_reference | the room point-light list is resolved offline by tools/export_point_lights.py and adopted by em_point_light.c (critic 7.2) | S1_newgame_load* |
| 0x001F6760 | — | BM | verified-unbound | em_effect_kinds em_effect_kinds_001F6760 — test_effect_kinds_reference | the room point-light list is resolved offline by tools/export_point_lights.py and adopted by em_point_light.c (critic 7.2) | S1_newgame_load* |
| 0x001F6850 | — | BM | verified-unbound | em_effect_kinds em_effect_kinds_001F6850 — test_effect_kinds_reference | not bound | S1_newgame_load* |
| 0x001F68B0 | — | BM | verified-unbound | em_effect_kinds em_effect_kinds_001F68B0 — test_effect_kinds_reference | not bound | S1_newgame_load* |
| 0x001F6BB0 | — | NM | live | em_effect_manager em_effect_manager_001F6BB0 through em_effects_live — test_effect_manager_reference | keys 0 / 0x1301 (latch bytes not canonical) fault | S2_opening |
| 0x001F6D60 | — | BM | verified-unbound | em_effect_kinds em_effect_kinds_001F6D60 — test_effect_kinds_reference | the room point-light list is resolved offline by tools/export_point_lights.py and adopted by em_point_light.c (critic 7.2) | S1_newgame_load* |
| 0x001F6E40 | — | BM | verified-unbound | em_effect_kinds em_effect_kinds_001F6E40 — test_effect_kinds_reference | the room point-light list is resolved offline by tools/export_point_lights.py and adopted by em_point_light.c (critic 7.2) | S1_newgame_load* |
| 0x001F6EB0 | — | BM | live | em_effect_manager em_effect_manager_001F6EB0 through em_effects_live — test_effect_manager_reference | | S2_opening |
| 0x001F8D30 | — | NM | live | em_shadow_actor_route through em_shadow_live (census L29) — test_shadow_actor_route_reference.py; test_level_smoke.py check_shadow (C: the ORIGINAL 0015BF90 + 001CE300 over the port's sampled inputs write the port's packets) |  | 02_elevator_refusal |

### 3.19 Streams, sound and message service (0x1F9000..0x1FDFFF)

39 functions, 3,096 instructions: live 29, verified-unbound 9, unverified 1 (recount 2026-09-26, status UI step).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001F9100 | — | BM | live | em_shadow_actor_route through em_shadow_live (census L29) — test_shadow_actor_route_reference.py; test_level_smoke.py check_shadow (C: the ORIGINAL 0015BF90 + 001CE300 over the port's sampled inputs write the port's packets) |  | 02_elevator_refusal |
| 0x001F9CF0 | — | NM | live | em_stream_lanes_original at step H (001FB100's first call) via em_stream_live (WP-8b) — test_stream_lanes_reference.py, test_iop_stream_reference.py (co-simulation); test_level_smoke.py (the director beats) | | S0_title |
| 0x001FA0D0 | — | NM | live | em_stream_lanes_original via em_stream_live (WP-8b) — test_stream_lanes_reference.py; test_level_smoke.py (the director beats) | | S0_title |
| 0x001FA330 | — | NM | live | em_stream_lanes_original via em_stream_live (WP-8b) — test_stream_lanes_reference.py; test_level_smoke.py (the director beats) | | S0_title |
| 0x001FA570 | — | BM | live | em_stream_lanes_original (001FABB0's ring reset) via em_stream_live (WP-8b) — test_stream_lanes_reference.py | | S0_title |
| 0x001FA5A0 | — | BM | live | em_message_service em_message_voice_ring_push over the lanes' ring (em_stream_live_001FA5A0, the message service's voice_push; WP-8b) — test_message_service_reference.py; test_level_smoke.py (the voiced lines 0x7F / 0x97 / 0x99) | | 10_cage_roof_roger |
| 0x001FA5F0 | — | BM | live | em_stream_lanes_original via em_stream_live (WP-8b) — test_stream_lanes_reference.py; test_level_smoke.py (the director beats) | | S0_title |
| 0x001FA790 | — | NM | live | em_stream_lanes_original via em_stream_live (WP-8b) — test_stream_lanes_reference; test-opening-runtime (the opening's cue 63) | | S1_newgame_load |
| 0x001FAAC0 | — | BM | live | em_stream_lanes_original via em_stream_live (WP-8b) (the message service's stop_lane, the timers' releases) — test_stream_lanes_reference.py | | S0_title |
| 0x001FAB50 | — | BM | live | em_stream_lanes_original via em_stream_live (WP-8b) — test_stream_lanes_reference | | S0_title |
| 0x001FAB80 | — | BM | live | em_message_service via em_message_live (step F), its 001FAAC0 calls on the lanes (WP-8b) — test_message_service_reference.py; em_stream_lanes_original — test_stream_lanes_reference.py | | S0_title |
| 0x001FABB0 | — | BM | live | em_stream_lanes_original via em_stream_live (WP-8b) (the status open, 001AD360 step 0, 001FD470 bit 1, the scripts, Roger, the opening, 001AC3B0) — test_stream_lanes_reference, test_iop_stream_reference (the status page scenario) | | S0_title |
| 0x001FABF0 | — | NM | live | em_stream_lanes_original via em_stream_live (WP-8b) — test_stream_lanes_reference.py; test_level_smoke.py (the director beats) | | S1_newgame_load |
| 0x001FAE70 | — | BM | live | em_stream_lanes_original via em_stream_live (WP-8b) (the status close, the opening's and Roger's resume, the aborted cinematic; since section 1.32 the state-0 area entry and the state-4 room move) — test_stream_lanes_reference, test_iop_stream_reference (the opening and status scenarios), test_rand_order (the area-entry draw from state 1) | state 2 r == 1 and state 6 stay reported (UM_001FAE70; no smoke run reaches them) | S1_newgame_load |
| 0x001FB100 | — | BM | verified-unbound | em_startup_load_gaps_sound em_slg_001FB100 — test_startup_load_gaps_reference | since WP-8b its first call, 001F9CF0, runs at step H (em_stream_live); the rest (the output-mode commit, the D_00281B70 copy, 001FC6E0) is not bound | S0_title |
| 0x001FB370 | — | BM | verified-unbound | em_startup_load_gaps_sound em_slg_001FB370 — test_startup_load_gaps_reference | not bound (the sound-bank loader chain) | S1_newgame_load* |
| 0x001FB3E0 | — | NM | verified-unbound | em_startup_load_gaps_sound em_slg_001FB3E0 — test_startup_load_gaps_reference | not bound (the sound-bank loader chain) | S1_newgame_load* |
| 0x001FB910 | — | BM | verified-unbound | em_startup_load_gaps_sound em_slg_001FB910 — test_startup_load_gaps_reference | not bound (the sound-bank loader chain) | S1_newgame_load* |
| 0x001FB9F0 | — | NM | live | em_sfx.c sfx_start / em_sfx_bank.c — test_area11_sfx_reference; test_area11_sfx_runtime.py | AREA11 cues re-exported; legacy ids still sharp (H19); since census L10 the player closure's 001FB9F0(id, 0x1000, 0x1000, 0x1000) calls (the ladder's 0x107 / 0x10E / 0x10F, not in the exported registry: silent, WP-14) are em_sfx_play, other request words fault | S0_title |
| 0x001FBC50 | — | BM | verified-unbound | em_stream_lanes_original — test_stream_lanes_reference | w_001FBC50 -> em_sfx_stop_all (live translation, no oracle), then its two 00119828 calls on the stream lanes (WP-8b) | S0_title |
| 0x001FBD50 | — | BM | live | em_sfx.c / em_sfx_bank.c play path — test_area11_sfx_reference | as 001FB9F0 | S2_opening |
| 0x001FBDB0 | — | AW | verified-unbound | em_sfx_bank — test_area11_sfx_reference.py |  | 09_fence_door |
| 0x001FBF50 | — | BM | verified-unbound | em_player_misc_workers — test_player_misc_workers_reference | em_sfx.c em_sfx_play_at (no gain oracle, AM-19) | S2_opening |
| 0x001FC280 | — | NM | unverified | em_scene_bindings_001FC280 (the lanes' worker inside 001FAE70, WP-8b; partial: the D_00282160 cache is not modelled and an ambient loop other than -1 faults); the lanes' oracle scripts it as a worker | no oracle: the lanes' oracle records the call, not its body | S1_newgame_load |
| 0x001FC3C0 | — | BM | verified-unbound | em_sfx, em_sfx_bank — test_area11_sfx_reference.py, test_area11_sfx_runtime.py |  | S2_opening |
| 0x001FC6E0 | — | BM | verified-unbound | em_startup_load_gaps_sound em_slg_001FC6E0 — test_startup_load_gaps_reference | not bound (the sound-bank loader chain) | S0_title |
| 0x001FC770 | — | BM | live | em_message_draw_original via em_message_live — test_message_draw_reference.py |  | S2_opening |
| 0x001FC7B0 | — | NM | live | em_message_glyph_original via em_message_live — test_message_glyph_reference.py |  | S2_opening |
| 0x001FC9B0 | — | CL | live | em_message_service em_message_reset via em_message_live (w_001FC9B0 and the service's own teardown) — test_message_service_reference |  | S1_newgame_load |
| 0x001FCA10 | — | BM | live | em_message_service via em_message_live (step F, installed at bring-up) — test_message_service_reference, test_panel_message_reference, test_roger_media_reference | modes 3/4 fault (their presenters are untranslated; the status page presents its own mode 4) | S0_title |
| 0x001FCB90 | — | CL | live | em_census_standins em_cs_001FCB90 through em_message_presenters_live (step F's 001FCA10 mode 4, and 002149F0's 001FCF10) — test_census_standins_reference; test_level_smoke.py (status, battery, panel: the pages' mode-4 words are the live block's) | status UI step 2026-09-26 (section 1.21): the step-F gate stand-in and the page layer's own mode-4 copy are deleted | 01_battery |
| 0x001FCF10 | — | BM | live | em_render_verify_rest em_rvr_001FCF10 through em_battery_page_live (002149F0 state 4) — test_render_verify_rest_reference; test_status_page_record_reference (composition); test_level_smoke.py (panel) | status UI step 2026-09-26 (section 1.21) | 03_panel_power |
| 0x001FD470 | — | BM | live | em_stream_lanes_original via em_stream_live (WP-8b) as the message service's stream_stop (bit 0 w_001FBC50, bit 1 001FABB0) — test_stream_lanes_reference.py | | S2_opening |
| 0x001FD4C0 | — | BM | live | em_message_service em_message_stream_request via em_message_live (the opening's 001B82D0 op12) — test_message_service_reference.py, test_area_script_reference.py, test-opening-runtime |  | S2_opening |
| 0x001FD580 | — | BM | live | em_message_service via em_message_live (step F) — test_message_service_reference.py | | S2_opening |
| 0x001FD6A0 | — | BM | live | em_message_service via em_message_live (step F) — test_message_service_reference.py | | S2_opening |
| 0x001FD790 | — | NM | live | em_message_service via em_message_live (step F) — test_message_service_reference, test_roger_media_reference, test_panel_message_reference |  | S2_opening |
| 0x001FD950 | — | NM | live | em_message_service via em_message_live (step F); its draw half em_message_draw_original — test_message_service_reference, test_message_draw_reference, test_message_capture |  | S2_opening |
| 0x001FDB80 | — | NM | live | em_message_service via em_message_live (step F) — test_message_service_reference, test_roger_media_reference, test_panel_message_reference |  | S2_opening |

### 3.20 Message draw (0x1FE000..0x1FEFFF)

7 functions, 400 instructions: live 7 (recount 2026-09-28, the module loader step).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001FE070 | — | NM | live | em_message_draw_original via em_message_live — test_message_draw_reference.py |  | S2_opening |
| 0x001FE460 | — | BM | live | em_message_draw_original via em_message_live — test_message_draw_reference.py |  | S2_opening |
| 0x001FE480 | — | AI | live | em_message_draw_original via em_message_live — test_message_draw_reference.py |  | S2_opening |
| 0x001FE4B0 | — | BM | live | em_message_draw_original em_message_bank_records via em_message_live — test_message_draw_reference.py (executes 001FE4B0) |  | S2_opening |
| 0x001FE4D0 | — | BM | live | em_message_draw_original em_message_bank_record via em_message_live — test_message_draw_reference.py (executes 001FE4D0) |  | S2_opening |
| 0x001FE530 | — | AW | live | em_message_draw_original via em_message_live — test_message_draw_reference.py |  | S2_opening |
| 0x001FEF70 | — | BM | live | em_status_scene_original em_status_scene_bank_001FEF70, the one owner (em_status_page's exit calls it since chain C8b LOADER; its copy there is deleted) — test_status_scene_reference.py, test_status_page_reference.py (0020E0C0 executed with 001FEF70 inline) | live at every status exit (0020E0C0 case 0); its 001FF0D0 area-chaining call is not run (the area streamer is not bound, H7) | S1_newgame_load |

### 3.21 Status UI: hub, ITEM/BATTERY pages, pickups (0x207000..0x21AFFF)

19 functions, 3,798 instructions: live 19 (recount 2026-09-26, status UI step).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x00209280 | — | NM | live | em_status_draw.c — test_status_draw_reference |  | 01_battery |
| 0x0020A7A0 | — | NM | live | em_status_background.c — test_status_background_reference |  | 01_battery |
| 0x0020AE40 | — | BM | live | em_status_ui_leftovers em_sul_0020AE40 through em_battery_page_live — test_status_ui_leftovers_reference (packet bytes equal the captured BATTERY page); test_status_page_record_reference (composition); test_level_smoke.py (battery, panel) | status UI step 2026-09-26 (section 1.21): em_battery_ui.c's hand-placed page is retired; the leaves are drawn from the EMBA atlas by TEX0 | 01_battery |
| 0x0020B0D0 | — | BM | live | em_status_ui_leftovers em_sul_0020B0D0 through em_battery_page_live — test_status_ui_leftovers_reference; test_status_page_record_reference; test_level_smoke.py (battery, panel) | status UI step 2026-09-26 (section 1.21) | 01_battery |
| 0x0020B210 | — | NM | live | em_status_ui_leftovers em_sul_0020B210 through em_battery_page_live — test_status_ui_leftovers_reference; test_status_page_record_reference; test_level_smoke.py (battery, panel) | status UI step 2026-09-26 (section 1.21) | 01_battery |
| 0x0020BEF0 | — | BM | live | em_status_ui_leftovers em_sul_0020BEF0 (inside em_sul_0020B210) through em_battery_page_live — test_status_ui_leftovers_reference | status UI step 2026-09-26 (section 1.21) | 01_battery |
| 0x0020CCB0 | — | BM | live | em_census_standins em_cs_0020CCB0 through em_battery_page_live — test_census_standins_reference; test_status_page_record_reference (composition); test_level_smoke.py (panel) | status UI step 2026-09-26 (section 1.21): the hand-placed marker sprite is retired | 03_panel_power |
| 0x0020CD40 | — | BM | live | em_status_ui_leftovers em_sul_0020CD40 through em_battery_page_live — test_status_ui_leftovers_reference; test_status_page_record_reference | status UI step 2026-09-26 (section 1.21); the cue's sample is not exported (silent, WP-14) | 03_panel_power |
| 0x0020CD60 | — | BM | live | em_status_ui_leftovers em_sul_0020CD60 through em_battery_page_live — test_status_ui_leftovers_reference; test_status_page_record_reference | status UI step 2026-09-26 (section 1.21); silent (WP-14) | 01_battery |
| 0x0020CDA0 | — | BM | live | em_status_ui_leftovers em_sul_0020CDA0 through em_battery_page_live — test_status_ui_leftovers_reference; test_status_page_record_reference | status UI step 2026-09-26 (section 1.21); silent (WP-14) | 03_panel_power |
| 0x0020CDC0 | — | NM | live | em_status_runtime.c / em_status_page.c page core (w_0020CDC0; case 0's whole request map; pages 1..3 through em_status_pages_live since chain C8b) — test_status_page_reference; test_status_hub_reference; test_level_smoke.py (status, status_pages) | requests 6 and the passcode pages fault (unreachable in AREA11, STATUS_PAGES.md section 7); module loads instant (H7); MAP 0020F950 live since chain C8b's fix round (section 1.37) | 01_battery |
| 0x0020DFA0 | — | BM | live | the CONFIGURE event in 0020DFA0's order: em_status_runtime issues the host's RESET_DRAW (001AFE60, em_status_models_clear), runs 0020E020, then the host's status_page_event CONFIGURE (em_status_models_configure, em_rcl_0021BAC0(0), em_rcl_0021B9A0(5, 0.0, 1e6), em_rcl_001D2610(0)) — test_census_unverified_reference (the original 0020DFA0 executed: every callee in order; the CONFIGURE path runs each, 001AFE60 / 0020E020 / the rest in that order); test_level_smoke.py check_render_context (the save slot 0 +0x120 equals routes 01..14 after the first status screen) | status UI step 2026-09-26 (section 1.21); its D_00810610 is the camera pool's since section 1.27 (em_status_models_set_view): the status frames' 001D1C50 projects the UI view | 01_battery |
| 0x0020E020 | — | BM | live | em_item_root.c / em_status_hub_ui.c trail adapter — test_status_hub_ui_reference (executes 0020E020); test_item_root_reference hooks it as a worker |  | 01_battery |
| 0x0020E060 | — | BM | live | em_status_runtime.c page reset (w_0020E060) — test_status_hub_ui_reference; test_status_frame_reference |  | 01_battery |
| 0x0020E080 | — | BM | live | em_status_page.c / host status_page_event — test_status_page_reference |  | 01_battery |
| 0x0020E0C0 | — | BM | live | em_status_runtime.c exit — test_level_smoke.py (two-tick close) |  | 01_battery |
| 0x0020EE50 | — | BM | live | em_item_root.c — test_item_root_reference |  | 01_battery |
| 0x002149F0 | — | NM | live | em_status_page_record em_spr_002149F0 through em_battery_page_live (the runtime's ITEM child page 5, over the UI block D_00810130, the request bytes, the inventory view and the live message block) — test_status_page_record_reference; test_level_smoke.py (the 239-frame notice, the panel's confirmation and discharge row for row) | status UI step 2026-09-26 (section 1.21): em_battery_ui.c's page, em_panel_battery_begin / _step and the battery_finished hook are retired | 01_battery |
| 0x00219550 | — | NM | live | em_pickup_owner em_pickup_owner_tick via em_pickup.c and the AREA11 interaction host — test_pickup_owner_reference (executes 00219550 and compares the tick) | live since WP-6 (81414be) for the six AREA11 items; since section 1.25 its state 0 runs over its record (em_pickup_owner_00219550_state0, executed against the original by test_pickup_owner_reference: 001B1020 over D_0028A56C or 001B0FD0, the +0x2E rewrite, 001C6380, 001A2370) and its visible +0x4C is 001CAA00 over it (the legacy item mesh is retired) | S2_opening |

### 3.22 Load veil, fog, stage workers, cinematic timeline (0x21B000..0x22FFFF)

25 functions, 3,097 instructions: live 24, verified-unbound 1 (recount 2026-09-27, load veil step).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x0021B180 | — | BM | live | em_scene_bindings, em_load_veil — test_area_load_reference.py |  | S1_newgame_load* |
| 0x0021B1B0 | — | AW | live | em_load_veil_particles, through em_rcl_0021B1B0 (em_scene_bindings' veil_0021B1B0 at 0021B550, the load veil; step V's list drawn by em_load_veil_live, LOAD_VEIL_PARTICLES.md 3) — test_load_veil_particles_reference.py; test_level_smoke.py check_load_veil (the live run equals the ORIGINAL 0021B1B0 executed at the same call, section 1.35); test_area_load_reference (the chain replay executes it) | the load spans one tick at host speed: one frame at level 0 (black) | S1_newgame_load* |
| 0x0021B500 | — | BM | live | em_load_veil_particles, veil_0021B500 (em_scene_bindings, at 0021B550) — test_load_veil_particles_reference; test_area_load_reference (the chain replay executes it); test_level_smoke.py check_load_veil (the original phase step equals the port's) | one step per load at host speed | S1_newgame_load* |
| 0x0021B550 | — | BM | live | em_scene_bindings, em_load_veil — test_area_load_reference.py, test_room_move_reference.py, test_scene_task_reference.py |  | S1_newgame_load* |
| 0x0021B840 | — | BM | live | em_scene_bindings, em_load_veil — test_area_load_reference.py |  | S1_newgame_load* |
| 0x0021B8E0 | — | BM | live | em_status_ui_leftovers em_sul_0021B8E0 (001D8FD0's) through em_render_context_live — test_status_ui_leftovers_reference; test_packet_chain_reference.py (001D8FD0 whole); test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S1_newgame_load |
| 0x0021B900 | — | BM | live | em_status_ui_leftovers em_sul_0021B900 (the 0021B970 / 0021B9A0 / 0021BA70 latch) through em_render_context_live — test_status_ui_leftovers_reference; test_packet_chain_reference.py; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S0_title |
| 0x0021B920 | — | BM | live | em_packet_chain_original (its body inside 0021B970 / 0021B9A0 on the render context, em_render_context_live; em_packet_chain_0021B920 for em_fog_gs_coefficients in scenes without the context) — test_packet_chain_reference.py, test_area11_fog_reference.py; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context | the Metal world fog takes the context's (A, B) (em_gfx_fog_coefficients) | S0_title |
| 0x0021B970 | — | BM | live | em_packet_chain_original em_packet_chain_0021B970 through em_render_context_live (001D8FD0, 001D2610) — test_packet_chain_reference.py; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S1_newgame_load |
| 0x0021B9A0 | — | NM | live | em_packet_chain_original em_packet_chain_0021B9A0 through em_render_context_live (001D1C50's mode 0 every world frame, the script host's, the timeline's restore; since the census L26 step the effects' fog ranges 001EA240 / 001F0A60 through em_effects_live and 001E67C0's near 0 / far 300 through em_snow_runtime) — test_packet_chain_reference.py; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context | | S0_title |
| 0x0021BA70 | — | BM | live | em_status_ui_leftovers em_sul_0021BA70 (0021BA80's tail) through em_render_context_live — test_status_ui_leftovers_reference; test_packet_chain_reference.py; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S1_newgame_load |
| 0x0021BA80 | — | BM | live | em_packet_chain_original em_packet_chain_0021BA80 through em_render_context_live (001D8FD0's) — test_packet_chain_reference.py; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S1_newgame_load |
| 0x0021BAB0 | — | BM | verified-unbound | em_status_ui_leftovers em_sul_0021BAB0 — test_status_ui_leftovers_reference | not bound | S2_opening |
| 0x0021BAC0 | — | BM | live | em_status_ui_leftovers em_sul_0021BAC0 through em_rcl_0021BAC0 (0020DFA0) — test_status_ui_leftovers_reference; test_level_smoke.py check_render_context (the save slot) | status UI step 2026-09-26 (section 1.21) | 01_battery |
| 0x0021BAE0 | — | BM | live | em_census_standins em_cs_0021BAE0 through em_rcl_0021BAE0 (0020E080's END_PROJECTION) — test_census_standins_reference | status UI step 2026-09-26 (section 1.21): restores the fog record +0xA0 the status screen's 0021B9A0(5) programmed; the next frame head's fog programmer rewrites it in the original too, so no capture shows the restored bytes (check_render_context: every gameplay tick holds the snapshots' fog block) | 01_battery |
| 0x0021BB00 | — | AW | live | em_player_stage_workers em_player_0021BB00 (0021C440 / 0015D100 / 00182B30, L01), em_script_host_workers — test_player_stage_workers_reference.py, test_script_host_workers_reference.py |  | S2_opening |
| 0x0021C3F0 | — | CL | live | em_player_stage_workers em_player_0021C3F0 (0021C440's hit_b gate, L01) — test_player_stage_workers_reference.py | D_00810770 is not canonical (L19): the stage's view load refuses area 8 room 2, where it is read | S2_opening |
| 0x0021C440 | — | BM | live | em_player_stage_workers em_player_stage_reaction (0015B130 / 0015B770, L01; em_player_damage.c's processor copy retired) — test_player_stage_workers_reference; test_level_smoke.py (no-hit path every stage) | its hit, pending-damage and infection paths reach fail-stop workers (rumble, effects, atan2 / the +20 object, 0017B490 / 001749A0) and unbound +4 = 2 states; no route capture has a hit | S2_opening |
| 0x0021D640 | — | BM | live | em_player_stage_workers em_player_0021D640 (0021C440, L01), em_player_reaction — test_player_stage_workers_reference.py |  | S2_opening |
| 0x00224290 | — | AW | live | em_player_fall, em_player_slide — test_player_fall_reference.py, test_player_slide_reference.py; test_level_smoke.py check_fall (the step-offs of routes 10, 11, 12 row for row); test_level_smoke.py check_crevice_jump (route 12 row for row) | live since census L09..L11 (the landing check of the falls and the jump); measured executing in the full smoke (census 1.9) | 10_cage_roof_roger |
| 0x002243F0 | — | AW | live | em_player_recovery, em_player_running_jump — test_player_recovery_reference.py, test_player_running_jump_reference.py; test_level_smoke.py check_crevice_jump (route 12 row for row) | live since census L11 (the jump's hit sub-state machine); measured executing in the full smoke (census 1.9) | 12_crevice_jump |
| 0x00224B80 | — | BM | live | em_player_recovery em_player_recovery_react_00224B80_worker, bound as the slide's damage worker — test_player_recovery_reference.py, test_player_slide_reference.py; test_level_smoke.py (06_hill_slide row for row, census L03) | live through the slide (0016C6A0 sub-state 3, every tick; returned 0 on the route) | 06_hill_slide |
| 0x0022EBE0 | — | CL | live | em_status_ui_leftovers em_sul_0022EBE0 (001DDE10's) through em_render_context_live — test_status_ui_leftovers_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S2_opening |
| 0x0022EC30 | — | BM | live | em_cinematic_playback em_cinematic_playback_start via the script host's w_0022EC30 (op00 kind 6, bank 0x96 clip 0; census L22) — test_cinematic_playback_reference; test_level_smoke.py (roger: route 14 row for row) |  | S2_opening |
| 0x0022EEF0 | — | NM | live | em_cinematic_playback em_cinematic_playback_tick via em_area11_script_host_camera_0022EEF0 (em_camera top mode 3; census L22) — test_cinematic_playback_reference; test_level_smoke.py (roger: route 14 row for row) (camera eye / target while the camera byte is 3) | the live camera frame (census L13..L16) calls it at +4 == 3 (lw_0022EEF0); the opening track stays on em_opening_runtime.c (em_opening_runtime_camera_sample) | S2_opening |

### 3.23 AREA11 overlay owners (0x8235F0..0x828050, runtime addresses)

23 functions, 4,400 instructions: live 21, verified-unbound 2 (recount 2026-09-28, section 1.39).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x008235F0 | — | AU | live | em_area11_effect_runtime.c (node 008235F0) — test_area11_effect_reference | visuals; contact damage 00823580 not modelled (INV-17) | S2_opening |
| 0x008237C0 | — | CO | verified-unbound | em_startup_load_gaps em_slg_008237C0 — test_startup_load_gaps_reference | the roster spawn stands in for the overlay init | S1_newgame_load* |
| 0x008237E0 | — | AU | live | em_roger_actor_original em_roger_actor_008237E0_init via em_area11_roger (lifecycle 0; census L22) — test_roger_actor_original_reference; test_level_smoke.py (roger: route 14 row for row) | its 001CA6F0 bank and the +0x30 / +0x58 words from assets/scene_snow/roger/resources.emrs (tools/export_roger_banks.py) | S2_opening |
| 0x00823910 | — | AU | live | em_roger em_roger_tick via em_area11_roger (census L22) — test_roger_reference; test_level_smoke.py (roger: route 14 row for row) |  | S2_opening |
| 0x00823950 | — | AU | live | em_roger em_roger_tick via em_area11_roger (census L22) — test_roger_reference; test_level_smoke.py (roger: route 14 row for row) | the ordinary branch (0x8283D0, route 14) and, since WP-8b, the alternate 0x828990 (route 10, with the director) run | 10_cage_roof_roger |
| 0x00823B70 | — | AU | live | em_roger em_roger_tick via em_area11_roger (census L22) — test_roger_reference; test_level_smoke.py (roger: route 14 row for row) |  | 14_roger_encounter |
| 0x00823CE0 | — | AU | verified-unbound | em_security_gun em_flag30_manager_tick (the flag-0x30 manager; renamed from em_script_door_fan_husk in L24) — test_script_door_fan_reference (part 3) | dormant in the first visit (waits on D_00810788); the node is bound with no port code | S2_opening |
| 0x00823E80 | — | AU | live | em_area11_opening.c / em_opening_runtime.c; its state 0 (001B0FD0, 001C6380) and state 1's tail (001B1B70, +0x4C 001CAA00) over its record in em_area11_bindings.c tick_opening since section 1.25 — test_continue_reset_reference (0x823F74..80 slice); opening capture frame; test_level_smoke.py check_owner_units (the canopy's unit), test_collision_world_capture.py (cell uid 3 on the published class-4 list) | partial oracle coverage; the legacy canopy prop is retired (section 1.25) | S2_opening |
| 0x00823FF0 | — | AU | live | em_truck_original via em_area11_boxes em_area11_boxes_truck_tick (census L23) — test_truck_original_reference; test_level_smoke.py (truck_crossing: route 08 row for row from the arm) | its 001EFD20 spawns run em_effects_live (census L26; the eight live puffs equal route 08's); sounds 0x454 / 0x455 not in the exported registry (WP-14) | S2_opening |
| 0x008251E0 | — | AU | live | em_truck_original em_truck_trigger_tick via em_area11_boxes (census L23) — test_truck_original_reference; test_level_smoke.py (truck_preview: route 07 row for row) |  | S2_opening |
| 0x008253F0 | — | AU | live | em_director_original via em_area11_bindings tick_director_original (node #21, census L21 with WP-8b) — test_director_original_reference; test_level_smoke.py (cage_roof, crevice_prompt, east_tower: routes 10, 11, 13) | since the measured drive model (section 1.30) the voiced lines 0x97 / 0x99 tear down on the capture's rows; 0x7F 2 rows early, which check_voice_drive proves is the original's lane-0 music refill served first (the music's phase: navigation; LEVEL_SMOKE.md) | S2_opening |
| 0x00825500 | — | AU | live | em_director_original (the beats' gates and bodies, node #21 since WP-8b) — test_director_original_reference; test_level_smoke.py (the director beats) | | S2_opening |
| 0x00825540 | — | AU | live | em_director_original (the beats' gates and bodies, node #21 since WP-8b) — test_director_original_reference; test_level_smoke.py (the director beats) | | S2_opening |
| 0x00825600 | — | AU | live | em_director_original (the beats' gates and bodies, node #21 since WP-8b) — test_director_original_reference; test_level_smoke.py (the director beats) | | 10_cage_roof_roger |
| 0x00825640 | — | AU | live | em_director_original (the beats' gates and bodies, node #21 since WP-8b) — test_director_original_reference; test_level_smoke.py (the director beats) | | 10_cage_roof_roger |
| 0x008256D0 | — | AU | live | em_director_original (the beats' gates and bodies, node #21 since WP-8b) — test_director_original_reference; test_level_smoke.py (the director beats) | | 11_crevice_prompt |
| 0x00825710 | — | AU | live | em_director_original (the beats' gates and bodies, node #21 since WP-8b) — test_director_original_reference; test_level_smoke.py (the director beats) | | 11_crevice_prompt |
| 0x008257A0 | — | AU | live | em_manager_008257A0.c — test_manager_8257a0_reference |  | S2_opening* |
| 0x00825940 | — | AU | live | em_security_gun em_gun_tick (the security gun; lifecycles 0, 0x64, 2, 3) through em_area11_bindings.c tick_gun since section 1.39 — test_script_door_fan_reference (part 3), test_security_gun_rest_reference (em_gun_rest_tick, every lifecycle, 24 captures); test_level_smoke.py check_gun_fan (equal to all 15 route snapshots on every tick), check_owner_units (its 001CAA00 unit), check_rand_order (its 0x8259F0 draw at AE+1); test_collision_world_capture.py (its plate, uid 15) | lifecycles 4 and 1 (return visit, D_00810788 == 0xFF) fault by design; em_gun_rest_tick translates them, not bound | S2_opening |
| 0x00827490 | — | AU | live | em_security_gun em_gun_cable_tick (the gun's cable) through em_area11_bindings.c tick_gun_cable since section 1.39 — test_script_door_fan_reference (part 3); test_level_smoke.py check_gun_fan, check_owner_units | its hit (+0x36: no live writer) is bound up to 001EFE00, which faults (its node view, 0021AAC0 / 0021A500 and 001CE860 are not bound) | S2_opening |
| 0x00827630 | — | AU | live | em_fan_original through em_area11_bindings.c tick_fan since section 1.39 — test_fan_original_reference, test_security_gun_rest_reference (its player hit through the original 0021C440); test_level_smoke.py check_gun_fan (every captured fan state on the port's cycle), check_owner_units (its units, over the port's +0xC8 through the original 001C6380) | its exit and hit boxes are off the smoke's route | S2_opening |
| 0x00827B10 | — | AU | live | em_elevator.c / em_area11_interaction_host.c (node #27); since section 1.25 its record: state 0's 001B0FD0 / 001C6380, the carry's and the completion's 001C6380, 0x827E6C and the +0x4C 001CAA00 (em_area11_boxes_owner_*, em_area11_bindings.c terminal_copy_child) — test_elevator_reference; test_level_smoke.py (routes 02/04: the record's +0x04 / +0xB0 row for row over route 04; check_indicator_children; check_owner_units) | the legacy platform mesh is retired (section 1.25) | S2_opening |
| 0x00828050 | — | AU | live | em_elevator.c em_elevator_motion_tick — test_elevator_reference; test_level_smoke.py (route 04) |  | 04_elevator_ride |

### 3.24 SDK VU0 math (0x1026A0..0x103237) and the VU1/DMA library functions that carry a translation

29 functions, 640 instructions: live 28, verified-unbound 1 (recount 2026-09-27, chain C7 step V).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001000E0 | — | BM | live | em_render_verify_rest em_rvr_001000E0 — test_render_verify_rest_reference; em_player_fall passes it the whole 64-bit double (9e0715f) | recount 2026-09-25: em_rvr_001000E0 executed on the live path (5 calls over the five measured runs) | 10_cage_roof_roger |
| 0x00100268 | — | BM | live | em_load_veil_particles (inline in 00100610) through em_render_context_live — test_load_veil_particles_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S0_title |
| 0x00100610 | — | BM | live | em_load_veil_particles em_load_veil_particles_00100610 (001006D8's) through em_render_context_live — test_load_veil_particles_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context |  | S1_newgame_load |
| 0x001006D8 | — | AI | live | em_load_veil_particles em_load_veil_particles_001006D8 (001D6E60's) through em_render_context_live — test_load_veil_particles_reference; test_render_context_live_reference (the live composition vs the original over beats 00..14); test_level_smoke.py check_render_context | its read-modify-write packet dwords start from a zero arena (the boot fill is not modelled) | S1_newgame_load |
| 0x001015A8 | — | AW | live | em_frame_kick (half_offset) through step V's draw environment of slot + 0x40 — test_render_context_live_reference (the live composition vs the original over beats 00..14, section 1.27); test_level_smoke.py check_render_context (step V lists) | chain C7 step V (section 1.27); moved from the SDK DMA/VIF/VU1 boundary group; step S's call on the display environment stays the renderer's | S0_title |
| 0x00101810 | — | AW | live | em_frame_kick (half_offset) through step V's draw environment of slot + 0xC0 — test_render_context_live_reference (the live composition vs the original over beats 00..14, section 1.27); test_level_smoke.py check_render_context (step V lists) | chain C7 step V (section 1.27); moved from the SDK DMA/VIF/VU1 boundary group; step S's call stays the renderer's | S0_title |
| 0x001026A0 | — | AI | live | em_camera_rotation.c / em_owner_services_original.c (VU0 forms) — test_camera_rotation_reference; test_camera_retarget_reference |  | S1_newgame_load |
| 0x001026D0 | — | AI | live | em_sdk_vu0 em_sdk_vu0_001026D0, the one translation (section 1.28; docs/SDK_VU0.md): em_locomotion_display, em_equipment_live, em_effect_manager, em_shadow_actor_route, em_status_models, em_frame_render_heads and em_shadow_original call it — test_sdk_vu0_reference (executes 001026D0: specials, the three aliasing forms); test_locomotion_display_reference, test_effect_manager_reference, test_shadow_actor_route_reference, test_shadow_original_reference (run it inside their composites) | recount 2026-09-25: executed on the live path (18483 calls over the five measured runs, as em_loco_001026D0); em_crate_original's em_crate_sdk_multiply keeps its own arithmetic (its oracle's SDK semantics are not the measured VU0 model; SDK_VU0.md) | S0_title |
| 0x00102718 | — | AI | live | em_effect_original / em_coll_* (inline) — test_effect_original_reference | recount 2026-09-25: em_effect_original_00102718 executed on the live path (36094 calls over the five measured runs) | S1_newgame_load |
| 0x00102738 | — | AI | live | em_sdk_vu0 em_sdk_vu0_00102738, the one translation (section 1.29; docs/SDK_VU0.md): em_coll_probe_original sdk_dot (em_actor_collision vu_dot), em_coll_list_passes_walkers, em_coll_grid_hull, em_coll_move_original, em_pickup_items_original vdot, em_owner_draw_original, em_actor_light_001D89D0, the camera modules, em_crate_original — test_sdk_vu0_reference (executes 00102738); test_coll_probe_reference.py, test_pickup_items_reference (both run 00102738 as original code inside the executed callers) |  | S2_opening |
| 0x00102760 | — | AW | live | em_pickup_items_original vnormalize (001F1180's facing test) — test_pickup_items_reference (runs 00102760 as original code) | the em_interaction_scan.c / em_camera_probe.c copies are checked only against the tests' normalize models | S1_newgame_load |
| 0x00102798 | — | AI | live | em_camera_commit_original em_camera_commit_00102798 (0018C0D0) — test_camera_live_reference (runs the original leaf); em_actor_light_001D89D0 sdk_00102798 — test_actor_light_001d89d0_reference | live since census L13..L16 (12008 calls); em_render_frame.c char_rig_build still re-derives the view basis in host float | S1_newgame_load |
| 0x001027E0 | — | AW | live | em_render_verify_rest em_rvr_001027E0 (inside em_cs_00102CD0) — test_render_verify_rest_reference; test_census_standins_reference | live since census L13..L16 (12008 calls) | S1_newgame_load |
| 0x00102850 | — | AI | verified-unbound | em_render_verify_rest em_rvr_00102850 — test_render_verify_rest_reference | not bound | 01_battery |
| 0x001028B8 | — | AI | live | em_coll_probe_original sdk_add (the live grid walkers) — test_coll_probe_reference.py (runs 001028B8 as original code) | the em_camera_probe.c inline copy is checked only against test_camera_probe_reference's model of the leaf | S2_opening |
| 0x001028D0 | — | AI | live | em_coll_probe_original sdk_sub, em_pickup_items_original vsub4 — test_coll_probe_reference.py, test_pickup_items_reference (run 001028D0 as original code) | the em_camera_retarget.c / em_camera.c inline copies are checked only against the retarget/probe/commit tests' models of the leaf | S1_newgame_load |
| 0x00102900 | — | AI | live | em_sdk_vu0 em_sdk_vu0_00102900, the one translation (section 1.28): em_shadow_actor_route through em_shadow_live (001F8D30's colour, census L29), em_actor_light_001D89D0 through em_owner_draw_live, em_camera_leftovers — test_sdk_vu0_reference (executes 00102900); test_shadow_actor_route_reference, test_actor_light_001d89d0_reference, test_camera_leftovers_reference | em_snow.c's tile colour is an inline scale on its own host model (test_snow_tiles_reference models the leaf rather than executing it; SDK_VU0.md) | S1_newgame_load |
| 0x00102918 | — | AI | live | em_owner_services_original.c (live through em_status_models) — test_owner_services_reference |  | S1_newgame_load |
| 0x00102948 | — | AI | live | em_sdk_vu0 em_sdk_vu0_00102948, the one translation (section 1.28; SDK_VU0.md lists the callers): em_frame_render_heads copy_qw (inside em_frh_001D30A0 and the row guard, through em_render_context_live), em_anim_runtime_rest, em_player_ladder_entry, the camera leftovers, the shadow route, the effects and the other host quadword copies — test_sdk_vu0_reference (executes 00102948); test_frame_render_heads_reference, test_anim_runtime_rest_reference (run it unhooked); test_effect_manager_reference | recount 2026-09-26 (section 1.22): copy_qw measured running on the live path. Copies through a module's own record or address accessors stay accessor loads and stores (SDK_VU0.md); em_camera.c's mentions are comments, and em_camera_retarget.c / em_camera_probe.c hold none | S0_title |
| 0x00102958 | copy_qw4 | AI | live | em_owner_services_original em_owner_services_copy_qw4_00102958 — test_owner_services_reference (executes it alone) | the em_status_scene_original.c copy is checked only against the status-scene test's copy hook; the terminal's 0x827E6C copy runs this translation since section 1.25 (em_indicator_bind_live_set_node); recount 2026-09-25: em_owner_services_copy_qw4_00102958 executed on the live path (82747 calls over the five measured runs) | S0_title |
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

29 functions, 1,770 instructions: live 29 (recount 2026-09-26, full-route recount).

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x0011C4C8 | — | BM | live | em_sdk_math_original — test_sdk_math_original_reference | recount 2026-09-25: sdk_0011C4C8 executed on the live path (90061 calls over the five measured runs) | S1_newgame_load |
| 0x0011C7B0 | — | NM | live | em_item_sdk_math.c / em_sdk_math_original.c — test_item_sdk_math_reference; test_sdk_math_original_reference |  | S2_opening |
| 0x0011CB90 | — | AW | live | em_item_sdk_math.c / em_sdk_math_original.c — test_item_sdk_math_reference; test_sdk_math_original_reference |  | S0_title |
| 0x0011CCC8 | — | AW | live | em_item_sdk_math.c / em_sdk_math_original.c — test_item_sdk_math_reference; test_sdk_math_original_reference |  | S2_opening |
| 0x0011D770 | — | AW | live | em_item_sdk_math.c / em_sdk_math_original.c — test_item_sdk_math_reference; test_sdk_math_original_reference |  | S2_opening |
| 0x0011D878 | — | NM | live | em_sdk_math_original sdk_0011D878 (the tangent kernel inside tanf 0011E398: 00175CF0's slope tangent in the engaged FLOOR) — test_sdk_math_original_reference (executes the kernel directly) | recount 2026-09-26 (section 1.22): measured running on the live path | S2_opening |
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
| 0x00122BB8 | — | BM | live | em_random.c — test_random_seed_reference; test_random_reference.py; test_rand_order and test_level_smoke.py check_rand_order (the call order against the C7 per-call capture, RAND_ORDER.md) |  | S1_newgame_load |
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

443 functions (23,796 instructions) are SDK, libc, IOP, driver or GS/VU1 work that the port replaces with a native service. They are not counted in the five statuses. "Contract" says whether anything compares the port's substitute with the original's observable effect.

Rules used: the SDK ranges of the decomp's SUBSYSTEMS.md (0x100000..0x12FFFF: DMA/VU1 library, libmpeg, kernel syscalls, libpad, libcdvd, libmc/SIF RPC, the EE sound library, the C runtime), the GS/VIF packet builders, texture uploads and display-object registry in 0x1CB5C0..0x1DAFFF, the module loader and disc reads 0x1FF080..0x2009E0, the IOP service, heap and MPEG glue in 0x203350..0x206D80, and the 2D draw layer. A function in the render ranges that carries a translation (the load-veil particles, shadow kernels, head sprite, message glyphs) is classified normally instead. The VU0 math leaves (0x1026A0..0x103237), libm (0x11C4C8..0x11FD77), soft float, the float conversions and rand are **game-visible arithmetic, not boundaries**: they are classified in section 3.

| Kind | Functions | Instructions | Port substitute | Contract verified |
|---|---:|---:|---|---|
| SDK libmpeg / IPU movie decode | 102 | 6,893 | em_movie_mac.m (AVFoundation) over movies exported by tools/export_movie.py | no original comparison (movie export test only) |
| EE sound library (SPU2 voices, sequencer, IOP sound RPC) | 59 | 3,207 | em_sfx.c + em_sfx_bank.c + em_audio_mac.c | 41: no (dry mixer; no SPU2 ADSR/reverb, AM-03/04); 9: partly: test_area11_sfx_reference; 5: partly: test_stream_lanes_reference; 3: partly: test_area11_sfx_reference, test_area11_sfx_runtime; 1: partly: test_area11_sfx_reference, test_area11_sfx_runtime, test_stream_lanes_reference |
| EE kernel syscalls, interrupts, threads, SIF DMA glue | 57 | 1,085 | host OS; nothing to reproduce | n/a |
| GS/VIF packet build / VU1 kick | 28 | 878 | native renderer (em_render_frame.c, em_gfx.h contract, gfx/metal/em_gfx_metal.m) | partly: level material and overlay-blend tests; fog, snow and status draws checked in their own tests |
| unidentified title-only SDK glue (probable MPEG) | 29 | 984 | em_frontend.c title / movie path | no |
| MPEG movie glue | 29 | 995 | em_movie_mac.m | no |
| C runtime (heap, stdio, string, init) | 25 | 2,885 | host libc | n/a |
| IOP service management | 18 | 584 | host OS; nothing to reproduce | n/a |
| heap / runtime | 18 | 982 | host allocator | n/a |
| SDK DMA/VIF/VU1 library | 11 | 700 | native renderer (em_render_frame.c, em_gfx.h contract, gfx/metal/em_gfx_metal.m) | not compared per call |
| SDK libmc / SIF RPC | 13 | 747 | local directory for the card check (STARTUP.md); RPC not needed | no |
| module loader and disc read | 12 | 1,039 | module 0x21: the loader's own steps (em_module_loader over em_status_scene_original, host-speed drive; section 1.38); the area read: em_game_legacy_area_load; the other page modules load at once | module 0x21: test_module_loader_reference and the level smoke's check_module_load; 001FFCD0 / 001FF590 translated and verified, not bound (test_status_scene_reference); area-load tick sequence (test_area_load_reference) |
| SDK libcdvd disc read | 9 | 346 | host file reads of locally exported assets | n/a |
| 2D GS draw layer | 9 | 284 | em_gfx 2D layer (em_status_background_draw.c, em_status_draw.c) | 6: no; 1: draw commands compared: test_status_background_reference, test_status_draw_reference, test_status_hub_ui_reference, test_status_page_record_reference; 1: draw commands compared: test_status_background_reference, test_status_draw_reference, test_status_hub_ui_reference; 1: draw commands compared: test_status_draw_reference, test_status_hub_ui_reference |
| GS/VIF packet build (sprites, flush) | 3 | 202 | native renderer (em_render_frame.c, em_gfx.h contract, gfx/metal/em_gfx_metal.m) | not compared per call |
| resource / display-object registry | 4 | 333 | locally exported assets (EMDL, roster, props) | no |
| GS texture upload | 6 | 426 | native renderer (em_render_frame.c, em_gfx.h contract, gfx/metal/em_gfx_metal.m) texture upload | not compared per call |
| C runtime string/format | 3 | 222 | host snprintf/strlen in em_status_hub_ui.c / em_message_draw_original.c | through the callers' oracles (test_status_hub_ui_reference, test_message_draw_reference) |
| stream / voice IOP service tick | 3 | 304 | 001F9820: em_stream_lanes_original at 001AAE40's start-up (em_stream_live, WP-8b); 001F9B20, 001F9BF0: not bound | 001F9820: test_stream_lanes_reference (init cases) and the 26 captures' lane voices; 001F9B20, 001F9BF0: no |
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
- **GS/VIF packet build / VU1 kick** (28; 001D2090, 001D2E20, 001D37D0, 001D38F0, 001D3AD0 and 001D3C30 moved to section 3.16 by the object-unit step; 001D2110, 001D2130, 001D2160, 001D2180, 001D21E0 and 001D2580 by chain C7 step V, section 1.27): 001D1C10,
  001D3900, 001D3990, 001D3CF0, 001D3D90, 001D3E40,
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
- **SDK DMA/VIF/VU1 library** (11; 001015A8 and 00101810 moved to section 3.24 by chain C7 step V, section 1.27): 001000B0, 00100278, 001002E0, 00100550, 001008C0, 001009C8, 00100A60, 00101BB8,
  00101F08, 00101FE0, 00102468.
- **SDK libmc / SIF RPC** (13): 00112440, 00112610, 00112D18, 00112DC0, 00112E28, 00113280, 00113680, 00113C38, 00113C68, 00113CD0,
  00113D08, 00113F68, 001152B0.
- **module loader and disc read** (12): 001FF080, 001FF0D0, 001FF3F0, 001FF590, 001FF830, 001FFCD0, 00200730, 00200780, 00200830, 00200890,
  00200970, 002009E0.
- **SDK libcdvd disc read** (9): 00110AB8, 00110B38, 00110B80, 00111018, 001115D0, 00111818, 001118B8, 00111F18, 00112088.
- **2D GS draw layer** (9): 00207070, 002070A0, 002070D0, 00207100, 00207150, 00207290, 00207D00, 00207E40, 00207F80.
- **GS/VIF packet build (sprites, flush)** (3; 001CB950 moved to section 3.14 by the shadow step, census L29): 001CABA0, 001CB5C0, 001CBC20.
- **resource / display-object registry** (4; 001CF870 and 001CF970 moved to section 3.15 by the shadow step, census L29): 001CFAE0, 001CFFE0, 001D04B0, 001D0660.
- **GS texture upload** (6): 001CC8A0, 001CCB00, 001CCB10, 001CCBD0, 001CCCC0, 001CCE80.
- **C runtime string/format** (3): 00122EF0, 00123168, 001232E0.
- **stream / voice IOP service tick** (3): 001F9820, 001F9B20, 001F9BF0.
- **2D GS glyph/sprite draw** (1): 001CBA50.
- **IOP movie service** (2): 00203350, 002036E0.
- **C runtime memset** (1): 00121A28.
- **overlay module dispatch** (1): 001E7780.

Boundary notes:

- **Sound library.** The port plays AREA11 cues through the pitch path verified by `test_area11_sfx_reference` (00115850 bend, 00117918), but its mixer has no SPU2 ADSR, Gaussian interpolation or reverb (WP-14, AM-03/04). Since WP-8b the streams play from the IOP stream backend (em_iop_stream: the SNDN2DRV.IRX stream subset and an SPU2 voice model, test_iop_stream_reference); 00119828's 0x1999 is the driver's effect-return volume (command 0x16), kept but inaudible without SPU2 reverb.
- **Module loader.** Since chain C8b LOADER module 0x21 loads through the loader's own steps (10 dispatches at host speed, the captured 24 with the PS2 disc-drive timing switch; MODULE_LOADER.md). The native area read still replaces 001FF080(1, 0) (the area streamer is translated, blocked on the sound-bank step 001FB370; H7), and the other page modules load at once.
- **Message glyphs (WP-8).** 001CC1E0 (tall-font strip layout) is translated and live in `em_message_glyph_original`; since the recount it is a section 3 row (live), as the render-range rule above requires. 001CC8A0/001CCB00 remain its upload/reset boundary there; `test_message_glyph_reference.py` compares every upload call and every packed pass with the executed original. The glyph texels are the port's atlas.
- **Translations inside boundaries (recount 2026-09-24).** Moved to section 3 by the render-range rule: 001D21B0, 001D2710, 001D2910, 001D2DE0, 001D2E00, 001D6B10, 001D6C90 (em_render_context helpers, test_render_context_reference) and 001D38A0, 001D3BA0 (em_owner_draw_original, test_owner_draw_reference), all verified-unbound. Kept as boundaries although they carry translations (their oracles were not re-read in the recount; since WP-8b they run live inside em_stream_live, section 1.15): the IOP stream driver side 00112610, 00112D18, 00113280, 001157F0, 0011A2B0 (em_iop_stream, test_iop_stream_reference, WP-8b), the stream-lane SDK commands 00119828, 0011A4E8, 0011A608, 0011A658 and the IOP tick 001F9820 (em_stream_lanes_original), and the module loader 001FF080, 001FF0D0, 001FF3F0, 001FF830 (em_status_scene_original; live for module 0x21 since section 1.38, with 00200780, 00200730 and 00200830 of em_module_loader) and 001FFCD0, 001FF590 (translated, not bound). Whether those become section 3 rows is a lead decision (the IOP/disc boundary of lane L36).
- **GS/VU1.** The renderer is the port's own. Individual packet builders have no per-call comparison; the level material, overlay blend, fog, snow and status draw tests compare their results where they exist. The game-side render heads (001D1C50, 001D1EA0, 001D30A0) and the projection helpers are **not** treated as boundaries: they are stand-ins in section 3.
- **Movies.** Title-only MPEG code and the unidentified 0x205050..0x205F90 glue (title only, adjacent to the MPEG glue) are replaced by the native movie path; no original comparison exists.

## 5. Gap lanes (prioritized)

Every non-live, non-boundary function belongs to exactly one lane. Sizes are in original instructions. "bind" lanes wire verified translations that already exist and delete the stand-in; "translate" lanes need new translations and oracles first. The order follows the route and the dependencies: the player stage and collision come first because every beat from 05 on needs them.

| # | Lane | Kind | Instr. | Functions (status mix) | Gates beats | Depends on |
|---:|---|---|---:|---|---|---|
| 1 | **L01-player-stage-live**: Engage the translated player stage (FLOOR gate): 0015BA50/0015B130/0015BCF0 and the stage workers, retire player_damage_tick. **Bound 2026-09-23 (partial); the scripted takeover live since chain C7 (section 1.26):** 0015B530, 001837A0, 00182B30 and 00182D70 run on a script owner's takeover (the stage's own, +4 = 4); the panel's, terminal's and items' takeovers stay the interaction runtime's; 0015CF90 unverified (no oracle) | bind | 1,922 | 15 (live 10, verified-unbound 4, unverified 1) | every frame from first control; prerequisite of the floor, slide, climb, ladder and jump lanes | nothing (translations exist); retire em_player_damage.c copies in the same change (done) |
| 2 | **L05-coll-move-walkers**: Replace em_collision movement queries with the translated move/sweep walkers. **Live 2026-09-24 (Boxes step):** 0019AD00 / 0019AFE0 with em_coll_grid_hull's 0019CB60 / 001A6440 serve 001764E0's probes and the closure in AREA11; the hull world has no chain reader (AREA11 publishes no class-2 owner); the follow camera keeps em_collision.c (L13) | translate+bind | 2,163 | 8 (live 1, verified-unbound 5, missing 2) | every frame (player and camera movement queries) | a translation of 0019CB60 and 001A6440 with their oracles (COLL_MOVE.md section 4 item 2) |
| 3 | **L07-actor-collision-live**: Link em_actor_collision: grid/column scan, actor hulls, list classes and the 001AAD00 swap. **Live 2026-09-24:** the world, the lists, 001AAD00, the publication of the panel, terminal, items, crates and drums, and (Boxes step) the query half 0019AB20 / 0019F730 / 0019C830 / 0019BC40 / 001A5760 on em_coll_probe_original's prims and the EE model; the class 2/0xA and 0xD pushes wait on owners that publish them; the truck's cell is published since L23, the prop 001C4820's and the canopy 00823E80's since the owners step (section 1.25) | bind | 2,224 | 13 (live 6, verified-unbound 7) | 05 (standing on crates), 08 (standing on the truck), 10 | L05 (queries), L25/L23 (owner cells) |
| 4 | **L02-floor-fall-live**: Bind the floor service and fall check (00175900/001796C0) and the fall state, retiring the floor snap and PLAYER_FALL_ENTRY. **Live 2026-09-24 (Boxes step):** FLOOR is engaged in AREA11 with every closure state bound (em_player_closure_live.c; FIRST_CONTROL.md "Engaged"); off-route helpers are fail-stop workers naming their originals **Census L09..L11 (2026-09-24, section 1.9):** the fall 00162DB0, the landing 00163B40 / 00163C10, 00179680, 00179880, 0017C580 and 00224290 ran in the walks' step-offs of routes 10, 11 and 12 and the jump's landing, compared row for row (check_fall, check_crevice_jump); left: 001760C0 | bind | 1,755 | 14 (live 13, verified-unbound 1) | 05, 06, 08, 10 (step-offs, the cage-roof fall) | L01; L05 (0019CB60, 001A6440); L07 (column scan 0019BC40 and actor collision) |
| 5 | **L04-box-climb**: Bind ledge climb / vault (0015DF10, 00161790) for the boxes. **Live 2026-09-24 (Boxes step):** the level smoke's `boxes` phase equals route 05 row for row | bind | 1,960 | 12 (verified-unbound 12) | 05 (climbing the boxes) | L01, L02, L05, L06 |
| 6 | **L03-hill-slide**: Bind the slide state (0016C6A0 family) for the hill. **Live 2026-09-24 (L03 step):** the level smoke's slide phase equals route 06 row for row (section 1.7); the slide's 001EFD90 spawns run em_effects_live since section 1.17 (L26), and its sounds 0x12E and the skid/landing ids are not in the exported registry (WP-14) | bind | 1,560 | 8 (live 8) | 06 (sliding down the hill) | L02, L05 (0016C570 / 001791D0 probes) |
| 7 | **L06-coll-probe-walkers**: Bind the translated probe walkers (em_coll_probe_original). **Live 2026-09-24 (Boxes step: FLOOR engaged)** (em_collision_world_bind_player); 0019F1A0 / 0019ED80 are live under the camera's grid walkers | bind | 1,992 | 9 (live 2, verified-unbound 7) | every frame (probes); 05 | L02 (engages FLOOR) |
| 8 | **L06b-coll-segment-walkers**: Bind the translated segment walkers (em_coll_segment_walkers). **Bound 2026-09-24 (partial):** 0019A910 and its walkers under the scripted retarget and the item ray; 0019A570 waits on its callers (climb, ledge catch, drum; the shadow's 0015BF90 runs it live since section 1.20); **census L13..L16 (section 1.11):** the live camera's every segment query runs 0019A910 | bind | 2,137 | 7 (live 4, verified-unbound 3) | every frame (camera and segment queries) | L04/L25/L29 (0019A570's callers), L13 |
| 9 | **L19-script-host**: Bind the area-script op handlers. **Recount 2026-09-24:** every row is a verified translation now (the seven former unverified handlers pass test_script_door_fan_reference part 1; 001BA510 / 001BAD40 are em_script_door_fan). **Live 2026-09-24 (section 1.8):** em_area_script is in COMMON, bound by em_area11_script_host for the truck trigger's 0x8292C0 (route 07 row for row): 001BA1A0, 001B8FC0 (op00 kinds 0 / 1 / 2) and 001B94F0 (op01 kind 1), and 001B6250 through the pad actuator; **Roger 2026-09-24 (section 1.10):** Roger's 0x8283D0 runs op06, op07, op0A, op0B, op0C, op0D, op10, op16 and op18 live (route 14 row for row); the rest are reached only by the director's scripts (L21) | bind | 1,968 | 21 (live 4, verified-unbound 17) | 07 (truck camera preview), 10, 11, 13, 14 | nothing (the remaining handlers wait on the director, L21) |
| 10 | **L20-message-service**: WP-8: extend the live panel message service (001FCA10) to every caller, with voice pushes and the stream table. **Recount 2026-09-24:** 001FE4B0 / 001FE4D0 are live (em_message_bank_records / _record); left (001FA5A0 live since WP-8b): 001FCF10 (translated, em_render_verify_rest) and the untranslated mode-4 presenter 001FCB90 **Live 2026-09-26 (status UI step, section 1.21):** 001FCB90 at step F and 001FCF10 in the BATTERY page; the step-F gate stand-in is deleted | bind | 997 | 18 (live 17, verified-unbound 1) | 10, 11 (director lines), 14 (Roger) | L19 |
| 11 | **L23-truck**: WP-12: bind the truck and its camera trigger. **Live 2026-09-24 (section 1.8):** 00823FF0 and 008251E0 on their nodes (em_area11_boxes), routes 07 and 08 row for row; 001EBF10 (the truck effects' kind) live since section 1.17 (the eight puffs equal route 08's) | bind | 1,461 | 3 (live 3) | 07, 08 (truck preview and crossing) | nothing |
| 12 | **L17-pickups-use-arbiter**: WP-6: bind the pickup owners and publish them to the Use arbiter; retire the legacy take. **Recount 2026-09-24:** done except the leaves: the owners 0015AFA0 / 0015AE20 / 00219550, the take 001B6EA0, the inventory 001C40B0 and the facing 001B7F90 are live (81414be); left: 0015AC00 (live since the owners step, section 1.25: em_pickup_owner_0015AC00 with its oracle), 001B1190 (em_pickup.c taken_set, hooked by its test), 001C5680 / 001C5760 (the indicator child ticks) are live per node since the render + UI step (section 1.13) | verify | 1,069 | 10 (live 9, unverified 1) | 01 (battery pickup) and every other pickup | host (done) |
| 13 | **L09-ladder-use-chain**: Bind the Use chain and ladder entry (0015D4C0, 00160220 whole, 00165B60). **Live 2026-09-24 (Boxes step):** 00160220 / 001798D0 / 0017C440 over the live record, `player_states_bind_use_chain(1)`; the stand-in player_pose_use_accepted is retired; **Live 2026-09-24 (census L09, section 1.9):** the EMCL carries the grid nodes' +0x34..+0x3F axis (flags bit 3, verified against captured RAM), so 0015D4C0 case 0x32, 00177030 mode 4, 00199DB0, 00165B60 and 00182A70 run on the cage column; route 10's two climbs row for row (check_cage_ladders) | bind | 2,085 | 11 (live 11) | 05, 10, 13 (ladders and the Use chain) | L01, L02, L06 |
| 14 | **L10-ladder-climb**: Bind the ladder climb states (001662D0 family). **Live 2026-09-24 (census L10, section 1.9):** 001662D0 with 0017FC80, 0017FD00, 00180300, 00180420, 00180460, 00181110, 00174AB0 and 001885D0 climb both cage ladders and dismount, route 10 row for row (check_cage_ladders); its 001FB9F0 sounds are em_sfx_play (silent ids, WP-14) and its 00187EE0 is em_player_floor's translation | bind | 1,856 | 8 (live 8) | 10, 13 | L09 |
| 15 | **L21-director-beats**: WP-10: manager 008253F0 and its beat scripts through the script host. **Done 2026-09-25 with WP-8b (section 1.15):** node #21 runs em_director_original over em_area11_script_host on the live path; Roger's 0x828990 and the voiced lines 0x7F / 0x97 / 0x99 run on the stream lanes; the level smoke's cage_roof, crevice_prompt and east_tower phases equal routes 10, 11 and 13 (since section 1.30 the teardowns of 0x97 / 0x99 on the capture's rows, 0x7F's 2 rows early from the music refill's phase); em_director.c and em_area11_flow.c are deleted | bind | 410 | 9 (live 9) | 10, 11, 13 | — |
| 16 | **L22-roger-encounter**: WP-9: bind the Roger owner, equipment child and cutscene timeline. **Live 2026-09-24 (section 1.10):** em_area11_roger binds 008237E0 / em_roger_tick and 001C5C90 over their record bytes, their scripts run on em_area11_script_host, 0022EEF0 / 0022EC30 drive the bank 0x96 timeline, the player's takeover runs 00183090 / 00182DF0 on the record; the level smoke's `roger` phase equals route 14 f288..f1818 row for row. Left: 001BA540, 001AF890, 001CA770 on Roger (bound, not reached on the route); 001DA6A0 / 001CAA00 are reported / the port's draw; beat 10's alternate script 0x828990 waits on L21 | bind | 2,121 | 24 (live 21, verified-unbound 3) | 10, 14 (Roger) | L19, L20 |
| 17 | **L11-running-jump-recovery**: Bind the running jump and recovery (0015EC50, 001634A0, 0017C860). **Live 2026-09-24 (census L11, section 1.9):** the crevice jump of route 12 row for row (check_crevice_jump: states, clips, clocks and the arc's Y exact; the step length within 1e-4 on the free-flight rows); the landing's 0017DEB0 is em_player_climb's translation over the record; beat 14's tower jump waits on the roger phase | bind | 2,297 | 6 (live 6) | 12 (crevice jump), 14 | L02, L09 |
| 18 | **L13-camera-follow**: Bind em_camera_follow_original (follow core) in place of em_camera.c camera_update. **Recount 2026-09-24:** em_camera_leftovers translates 0018B9C0 (the camera frame), 0018C0C0, 0018C5A0, 00190F20, 001914A0, 00191580; every row except 0019B7D0 (live) is verified-unbound. **Live 2026-09-25 (census L13..L16, section 1.11):** em_camera_live binds the follow core, the frame and every solver on the canonical camera words; the legacy camera_update runs only for scenes without an original roster | bind | 1,587 | 15 (live 15) | every frame from first control | L06b (camera queries) |
| 19 | **L14-camera-solver-dd20**: Bind 0018DD20 (desired-eye solver) and retire the unverified duplicate. **Recount 2026-09-24:** both are translated (em_camera_leftovers_solver em_camleft_0018DD20 / _0018CE60); em_camera.c cam_solver_0018DD20 and em_game.c cam_bounds_settle_0018CE60 are the live duplicates to retire. **Live 2026-09-25 (section 1.11):** both translations run in AREA11; the two duplicates are reached only by the legacy camera of scenes without an original roster (0 calls in the full smoke) | bind | 2,051 | 2 (live 2) | every frame from first control | L13 |
| 20 | **L15-camera-actions**: Bind the camera action dispatch 0018BC20 and em_camera_area11_specials (001921D0, 00193EB0). **Recount 2026-09-24:** 0018BC20 and 00191000 are em_camera_leftovers translations. **Live 2026-09-25 (section 1.11)**; the examine and aim owners still pre-empt action 0 through named stand-ins (CAMERA_LIVE.md section 6; L28; the door cinematic retired with census L18) | bind | 1,951 | 5 (live 5) | every frame from first control | L13 |
| 21 | **L16-camera-area11-walk**: Bind 00195130 (AREA11 walking specials), 001916C0 and the 0015CBA0 state map. **Recount 2026-09-24:** 0015CBA0 and 001916C0 are translated (em_camera_leftovers). **Live 2026-09-25 (section 1.11)** | bind | 1,843 | 3 (live 3) | every frame from first control (AREA11 walking specials) | L15 |
| 22 | **L12-locomotion-display**: Replace the legacy idle/walk callbacks and gait display with 00161020/001612D0/0017B660 and their verified workers. **Live 2026-09-25 (section 1.12):** 00161020 / 001612D0 are 0015B130's state[0] / state[1] over the record in AREA11 (behind the port's stand-ins, L21 / L28), with 0017C030, 0017B660, 0017B5C0, 00179D20, 00179FF0, 0017B490 / 0017B460, 00178B90, 00187350 / 00187EE0 and the new record-level 0017BC40 / 0017B910; 00182D40 stays verified-unbound (its caller 00182DF0 is not on the record); 00187DC0 unchanged | bind | 1,742 | 15 (live 13, verified-unbound 2) | every frame from first control (idle/walk look and footsteps) | L01 |
| 23 | **L24 (the fan pair; the security gun and its cable, formerly "L24-fan-husk")**: WP-11: bind the fan pair, the security gun and its cable. **Live 2026-09-28 (section 1.39):** 00825940 (em_gun_tick; its return-visit lifecycles 4 / 1 fault by design), 00827490 (em_gun_cable_tick; its hit faults at 001EFE00, unreached) and 00827630 (em_fan_original_tick) on their records, drawn by 001CAA00; the flag-0x30 manager 00823CE0 (em_flag30_manager_tick) is verified, not bound | bind | 1,924 | 4 (live 3, verified-unbound 1) | every frame (the gun, the cable and the fans), level exit (fan) | L07 |
| 24 | **L25-crates-drums**: WP-18: bind crates and drums in place of em_enemy. **Live 2026-09-24 (Boxes step):** em_area11_boxes.c runs both owners over their roster nodes (CRATES_DRUMS_ORIGINAL.md "Binding"); the legacy AREA11 copies are retired; the damage paths are fail-stop (no live +0x36 writer; their effect spawns run em_effects_live since section 1.17) | bind | 1,885 | 2 (live 2) | every frame; 05 | L07 |
| 25 | **L18-door-original**: WP-7: bind the original door runtime/program/transit. **Recount 2026-09-24:** 001B1B30, 001BC240, 001BC290, 001BBD60 and 001B0080 are em_script_door_fan translations; every row is verified-unbound. **Live 2026-09-25 (section 1.18):** em_area11_door; route 09 row for row | bind | 881 | 11 (live 11) | 09 (fence door, side beat) | L17 (Use arbitration) |
| 26 | **L28-player-equipment**: Bind the player equipment/weapon actors (0018A6B0 nodes) and the gun tick. **Recount 2026-09-24:** em_player_equipment translates all 13 rows (3824c6f); the em_weapon.c gun tick, lamp gate and camera-code stand-ins go when bound **Render context step 2026-09-25:** 0015D2F0 runs live through the render context's calls (001D1C50, 001DDE10) **Live 2026-09-25 (effects step, section 1.17):** the seven 0018A6B0 nodes run em_player_equipment through em_equipment_live (models from the Roger export's D_0028A56C spans, slots from the one stack, the equipment change's respawn after a status screen); the records equal the route snapshots 08 / 10 / 11 / 13 / 14 (check_effects); the em_weapon.c armed-stance stand-ins stay until the aim drawers are translated **Player step 2026-09-26 (section 1.23):** the nodes' +0x4C is 001CAA00 through em_owner_draw_live (their units equal the original's in the camera-exact beats 10 / 14), and em_weapon reads the player's node 4 from the record instead of the gfx bone publish (retired) | bind | 2,101 | 13 (live 13) | every frame (rifle/knife children, gun tick, aim) | L01 |
| 27 | **L26-effect-manager**: Bind the effect manager barrel (em_effect_manager) and em_effect_original. **Recount 2026-09-24:** the barrel 001F0360 and its lanes are translated (3824c6f); 001F1110 / 001F1180 moved to live (the pickup aura, 81414be) and left this lane's count; 001F1180's draw block (001F0A60 here) is the no-op stand-in aura_draw. **Effects step 2026-09-24 (blocked):** every effect draw reads the render-context views (the +0x2240 / +0x22C0 clip matrices through 001CD370, the 0x70003AC0 / 0x70003A40 matrices, the +0xA0 fog), and no live code produces them; binding the spawns and the 001EA240 walk without them would fault on the first footstep (EFFECT_MANAGER.md 5.0) **Render context step 2026-09-25:** its prerequisite is met: the canonical render context runs live (section 1.16; EFFECT_MANAGER.md 5.0) **Live 2026-09-25 (effects step, section 1.17):** em_effects_live binds the barrel (w_001F0360), the spawns (every player-side 001EFD90, the truck's, the crates' and drums' and the weather node's 001EFD20), the driver 001EA240 on the pool nodes and the pickup glint; the old counted gaps are deleted (only the skid's two packet-only handlers 001EAD70 / 001EC270 are a counted gap; every other untranslated handler and 001EFE00 fault); the eight truck puffs, route 12's four footstep puffs, the lanes' packets and route 10's glow-marker primitives equal the snapshots (check_effects); the chains are not drawn (the renderer) | bind | 2,342 | 15 (live 15) | every frame (effects) | nothing |
| 28 | **L27-effect-kinds**: Bind the effect kinds/tables and the effect colour (em_effect_kinds). **Recount 2026-09-24:** every row is translated (3824c6f); the point-light list rows replace the offline tools/export_point_lights.py resolution. **Effects step 2026-09-24:** the handlers' 001CFB50 and its 001D0540 are translated in em_effect_kinds (boundary rows moved to section 3.15). **Render + UI step 2026-09-25 (section 1.13):** 001F54E0 is live (the indicator children's colour); em_effect_color.h's own copy is deleted **Live 2026-09-25 (effects step, section 1.17):** the handlers (001EC1F0, 001EC3F0, 001EC470, 001EBF10), the glow markers (001F5640 / 001F5940 / 001F5C20), 001F5CA0 and the resets (001F0310 at 001AFCA0's 001D0660); the room point-light lists (001F6640..001F6E40) stay the offline stand-in (critic 7.2) | bind | 1,251 | 18 (live 11, verified-unbound 7) | every frame (effects); 00, 05, 06 (footstep and slide effects) | L26 |
| 29 | **L08-coll-missing-and-list-passes**: Translate the untranslated collision originals and the actor list passes. **Translated 2026-09-23; bound 2026-09-24:** the nine hooks, 0019B7D0 / 0019E280 live; 001A8660 waits on a class-0xD owner (the flame), 0019E930 / 001A3980 on L09, 0019F330 on the column's pass 2 | bind | 2,081 | 14 (live 10, verified-unbound 4) | every frame; 05, 07..14 | L09, the flame owner, original-layout records for class 1/2/0xD |
| 30 | **L29-shadow-route**: Bind the shadow actor route (0015BF90) and its packet producers. **Live 2026-09-26 (section 1.20):** w_0015C160 runs 0015C160's gate and route; 0015BF90 / 001F9100 / 001F8D30 / 001CD390 and the decal packet 001CE300 / 001CF470 (with 001CF870 / 001CF970 / 001CB950, moved in from the boundary list) run through em_shadow_live; the fans are drawn by em_gfx_shadow_decal_fan at the page splice; left: the decal texture's uploader (the texture is exported from the route captures' GS memory) | bind+verify | 974 | 7 (live 7) | every frame from 02 (player shadow) | renderer |
| 31 | **L29b-shadow-gs**: Bind em_shadow_original / em_shadow_gs (receiver passes, clip kernels). **Recount 2026-09-24:** the five former unverified rows pass test_render_verify_rest_reference (part D; 001D5C80 with the em_ee_float.h fix). **Live 2026-09-26 (section 1.20):** 001DA6A0 and its steps run through em_shadow_live after the level and the walked actors, the Metal passes em_gfx_shadow_* draw them; the player's own draw moved behind them (0015C160's +0x4C); left: Roger's 001DA6A0 (kind 0x29, its proxy is not exported; census L22) and the opening's post-step (design risk 2) | bind | 1,779 | 11 (live 11) | every frame (shadow receivers) | L29-shadow-route |
| 32 | **L32-frame-render-heads**: Replace the port collectors at 001D1C50/001D1EA0/001D30A0 and the projection with translations. **Recount 2026-09-24:** em_frame_render_heads translates all 18 rows; the port collectors and em_math.h projection are the stand-ins to retire **Render context step 2026-09-25 (section 1.16):** bound through em_render_context_live: 001D1C50, 001D1EA0, 001D30A0, 001D2960, 001D2D20, 001D2830 and the zoom helpers live, plus 001D1AE0 (moved in from the boundary list); not bound: 001C1D00 (001D5370 needs the static-object bank export), 001D19E0, 001D19D0 / 001D9070, 001D8060 / 001D80B0, 001D88B0 / 001D8C30 (RENDER_CONTEXT.md 8.4) **Chain C7 step V 2026-09-27 (section 1.27):** 001D1EF0 bound with step V 001D2300 (before the area bind still reported) | bind | 1,545 | 18 (live 10, verified-unbound 8) | every frame (frame setup, projection) | the static-object bank export (001C1D00); L33 / L40 (lighting, fade weights) |
| 33 | **L30-render-context**: Bind the render-context / HUD bar / area-specials path (em_render_context). **Recount 2026-09-24:** all 16 rows translated, plus the seven render-range helpers that were boundary rows (001D21B0, 001D2710, 001D2910, 001D2DE0, 001D2E00, 001D6B10, 001D6C90) **Render context step 2026-09-25 (section 1.16):** 001DDA00 / 001DDAA0 / 001DDE10 / 001DEEE0 (the four-sprite path), 001E0D70, 001E0CC0, 001DD950, the flag and slot helpers live on the one context, plus 001D2730 (moved in from the boundary list); not bound: 001D5370 / 001D52E0 (the bank), 001DD7B0 / 001DD940 and 001E0C30 / 001E1010 (001D19E0), 001D21B0 (reached only with a +0x1D8 / +0x1E8 / +0x2520 word, none built in the port; 001E0DF0 is live through step V since section 1.27); the four sprites' packets reach no native consumer | bind | 1,754 | 23 (live 16, verified-unbound 7) | every frame (render context, HUD bar) | the static-object bank export; 001D19E0; a renderer consumer for the four-sprite packets |
| 34 | **L31-background-weather-load**: Bind em_background_gs and the area-load render passes (001C1DC0 family). **Recount 2026-09-24:** the 001C1DC0 family, 001C1F50, 001E0CF0, 001E2260 / 001E2270 and 001C22A0 / 001C2360 are em_render_verify_rest translations; 0021B9A0 (the fog programmer) is translated since afa091b (em_packet_chain_original, verified-unbound); **Effects step 2026-09-24:** 0021B920 is live as the one translation behind the Metal fog (em_fog_gs_coefficients is a call of em_packet_chain_0021B920). **Render + UI step 2026-09-25 (section 1.13):** 001E1E60 and kernel 0x0023C990 (em_background_gs) draw live first in every world frame, gated as 001D2300 gates its CALL **Render context step 2026-09-25:** the 001C1DC0 family (its 001C1E70 -> 001D52E0 reported), the area fog 001D8FD0, 0021B970 / 0021B9A0 / 0021BA80 live on the render context **Chain C7 step V 2026-09-27 (section 1.27):** 001D2300 live (em_frame_kick at main-loop step V) | translate+bind | 932 | 17 (live 16, verified-unbound 1) | S1 (area load), 09 (room move), every frame (background) | renderer |
| 35 | **L33-anim-runtime-rest**: Bind the remaining animation-runtime originals. **Bound 2026-09-24 (display step, partial):** em_pose_host_workers is the player's one pose owner (em_player_record_pose over the live record): 11 rows live. **2026-09-25 (section 1.12):** 001C9D50 / 001C9E40 live through 0017B660. Still unbound, each on another lane: 001C7900 (001D88B0, L32), 001CB2C0 (001CB3C0, L35), 001CAAC0 (001CB760 L39, 001F6210 L26), 001CACB0 (001CABA0, renderer boundary), 001CB5B0 (one canonical D_00275B40 / D_00275B48 for every host); 001C7C00 is live for S2 (critic 7.2) and stays in the lane for beat 14 | bind | 1,819 | 22 (live 14, verified-unbound 8) | every frame (animation) | nothing |
| 36 | **L36-stream-lanes-sound**: Bind the stream lanes (music/voice) and the gain/positional sound originals. **2026-09-25 (WP-8b, section 1.15):** the stream lanes are live (em_stream_live); left: 001FBC50's body (em_sfx_stop_all, no oracle), 001FC280's body (unverified) and the positional voices 001FBF50 / 001FC3C0 / 001FBDB0 (001FB100's rest and 001FC6E0 are lane L34's) | bind | 1,530 | 17 (live 12, verified-unbound 4, unverified 1) | every frame (music and voice streams) | IOP/disc boundary decisions; the area-entry 001FAE70(1) (the RNG order audit) for the opening's prefill wait (section 1.30) |
| 37 | **L38-load-veil-particles**: Bind the load-veil particles (0021B1B0/0021B500) so the load is not black **Render context step 2026-09-25:** the REF tags and 001D6930 / 001D6E60 / 001006D8 / 00100610 / 00100268 / 001D6BA0 run live under 001D1AE0 and 001DDE10. **DONE (load veil step, 2026-09-27, section 1.35):** the veil draw and phase step are bound at 0021B550 and step V's list is drawn by the GS frame stage; the veil stays black until the area read runs the loader task's own ticks (H7) | bind | 1,074 | 16 (live 16) | S1, 09 (loads) | nothing |
| 38 | **L39-head-sprite-effects**: Bind the head-bone sprite effect (001E2560 node) and its registry helpers. **Recount 2026-09-24:** the packet-chain builders 001CB5F0, 001CB6B0, 001CB760 and 001CB900 are translated since afa091b (em_packet_chain_original, docs/PACKET_CHAIN.md). **Effects step 2026-09-24:** the node's 001CCF70 and its 001CFBE0 packet read the render-context views, which no live code produces (EFFECT_MANAGER.md 5.0) **Render context step 2026-09-25:** 001CB760 live (001DDE10's); the render-context views the node reads exist live **Live 2026-09-25 (effects step, section 1.17):** the player's (0x3B) and Roger's (0x47) head sprites run em_head_sprite_original through em_effects_live, spawned by the one 001EF9D0; their static fields equal the snapshots (check_effects); the ramp and wait follow rand(); the chains are not drawn (the renderer) | bind | 868 | 12 (live 12) | every frame (head-bone sprite) | renderer (drawing the chains) |
| 39 | **L35-status-ui-leftovers**: Area-title card, UI cues, the BATTERY page draw, the owner draw and the remaining owner-service leaves. **Recount 2026-09-24:** em_status_ui_leftovers translates the area title, UI cues, context saves, 0022EBE0 and the BATTERY page draw 0020AE40 / 0020B0D0 / 0020B210 (moved here from live: em_battery_ui.c is a hand-placed stand-in, critic 7.2); em_owner_draw_original adds 001CA7B0 / 001CA940 and the former boundary rows 001D38A0 / 001D3BA0; left untranslated: 0020CCB0 and 0021BAE0 (stand-ins), 0020DFA0 (unverified) **Render context step 2026-09-25:** 0021B8E0, 0021B900, 0021BA70 and 0022EBE0 run live on the render context (the mix is recounted from the section 3 rows) **Object-unit step 2026-09-25:** the owner draw 001CA7B0 / 001CA940 / 001D38A0 / 001D3BA0 runs live (em_owner_draw_live, section 1.19) **Status UI step 2026-09-26 (section 1.21):** the BATTERY page draw 0020AE40 / 0020B0D0 / 0020B210 / 0020BEF0 / 0020CCB0, the cues 0020CD40 / 0020CD60 / 0020CDA0, 0020DFA0 with 0021BAC0 and 0021BAE0 run live **Owners step 2026-09-26 (section 1.25):** 001C4820 runs live at its node (the mix is recounted from the section 3 rows) | translate+bind | 1,937 | 33 (live 29, verified-unbound 3, missing 1) | S2 (area title), 01, 03 (UI cues), owner services | L20 (message lookup) |
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
- **L24** (the fan pair, the security gun and its cable; formerly L24-fan-husk): 00827630, 00825940, 00827490, 00823CE0.
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
- **L29-shadow-route** (live, section 1.20): 0015BF90, 0015C160, 001CE300, 001CF470, 001F8D30, 001F9100, 001CD390.
- **L29b-shadow-gs** (live, section 1.20): 001D4B50, 001D98A0, 001D9EE0, 001DA080, 001DA1E0, 001DA290, 001DA310, 001DA6A0, 001D5C80, 001D4CD0,
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
- **Snapshot.** Statuses reflect the port HEAD 9e0715f (2026-09-24, section 1.4) as updated by sections 1.5..1.17 (the last: the effects step, 2026-09-25); the census run itself is still the 2026-09-23 one (beats 00..14). `classified.json` was regenerated from these rows in the recount (its `recount` block lists every change; the 2026-09-23 file is kept beside it as `classified.2026-09-23.json`). Landing steps must re-classify their rows here and recompute section 2 and the lane mixes in the same change; the recount found five commits that had not (3824c6f, 81414be, 4b6ac3e, 9d2a129, 0f4cc8f). The level exit (beat 15, FIRST_LEVEL_EXIT.md) is still outside these tables.


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
