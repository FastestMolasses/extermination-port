# First-level route census: every original function on the route and its port status

Date: 2026-09-23 (session s87). Target: the pinned boot ELF (SHA-256 `ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`) and the AREA11 overlay (id 9).

This document answers one question: **which original functions execute on the first-level route, and what does the live port do for each of them?** It is the measuring stick for "the first level is ported". It lists addresses, names, statuses, port modules and tests only. It contains no original code, data or disassembly.

The machine-readable table is `../Extermination/build/s87/census/classified.json` (ignored, local). It holds one row per function with every field below, the boundary groups and the gap lanes. The census inputs are in the same folder: `route_functions.json`, `per_beat.json`, `method.md`.

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
| live | 212 | 22,111 | 190 (20,341) | 22 (1,770) |
| verified-unbound | 300 | 46,036 | 284 (44,837) | 16 (1,199) |
| unverified | 49 | 4,181 | 44 (3,445) | 5 (736) |
| stand-in | 40 | 6,192 | 36 (6,074) | 4 (118) |
| missing | 105 | 8,181 | 75 (6,476) | 30 (1,705) |
| boundary | 478 | 25,063 | 199 (11,606) | 279 (13,457) |
| **total** | **1184** | **111,764** | 828 | 356 |

Of the 706 non-boundary functions, 212 (30.0%) are live and verified; by instructions 22,111 of 86,701 (25.5%). One of them, 0015BCF0, is live only in part (its tail; the row says so). A further 300 functions (46,036 instructions) are verified translations waiting to be bound, which is where most of the remaining work is. The totals and the per-label table below are recounted from the section 3 rows (last recount 2026-09-24, after L01).

### 2.2 Per route label

"Ran" counts every function the label executed; "first" counts the functions first seen in that label in route order (beat 00 is placed before 01).

| Label | Ran: live / verified-unbound / unverified / stand-in / missing / boundary | First seen here: live / v-u / unv / stand-in / missing / boundary |
|---|---|---|
| S0_title | 25 / 22 / 6 / 7 / 13 / 399 | 25 / 22 / 6 / 7 / 13 / 399 |
| S1_newgame_load | 64 / 52 / 9 / 11 / 39 / 104 | 48 / 34 / 5 / 4 / 29 / 20 |
| S2_opening | 137 / 174 / 34 / 25 / 57 / 153 | 89 / 134 / 30 / 18 / 48 / 44 |
| S3_first_control_idle | 97 / 133 / 23 / 24 / 50 / 144 | 1 / 10 / 0 / 0 / 1 / 0 |
| 00_panel_no_battery | 133 / 153 / 25 / 25 / 55 / 127 | 12 / 10 / 1 / 2 / 5 / 2 |
| 01_battery | 154 / 171 / 27 / 28 / 59 / 184 | 22 / 4 / 0 / 3 / 3 / 5 |
| 02_elevator_refusal | 134 / 163 / 26 / 25 / 55 / 132 | 2 / 9 / 1 / 0 / 0 / 5 |
| 03_panel_power | 167 / 188 / 28 / 28 / 61 / 158 | 7 / 10 / 1 / 2 / 1 / 1 |
| 04_elevator_ride | 126 / 164 / 27 / 25 / 55 / 163 | 1 / 1 / 0 / 0 / 0 / 2 |
| 05_boxes | 112 / 179 / 23 / 27 / 55 / 126 | 1 / 18 / 0 / 3 / 1 / 0 |
| 06_hill_slide | 107 / 154 / 22 / 25 / 55 / 126 | 0 / 11 / 0 / 0 / 1 / 0 |
| 07_truck_preview | 119 / 150 / 27 / 25 / 56 / 144 | 0 / 0 / 0 / 0 / 1 / 0 |
| 08_truck_crossing | 106 / 153 / 23 / 24 / 56 / 161 | 0 / 1 / 0 / 0 / 1 / 0 |
| 09_fence_door | 133 / 166 / 32 / 26 / 63 / 135 | 3 / 3 / 2 / 1 / 0 / 0 |
| 10_cage_roof_roger | 139 / 199 / 32 / 28 / 59 / 170 | 1 / 24 / 2 / 0 / 1 / 0 |
| 11_crevice_prompt | 139 / 198 / 29 / 28 / 58 / 167 | 0 / 2 / 0 / 0 / 0 / 0 |
| 12_crevice_jump | 112 / 165 / 22 / 27 / 56 / 122 | 0 / 6 / 0 / 0 / 0 / 0 |
| 13_east_tower | 137 / 183 / 29 / 28 / 51 / 165 | 0 / 0 / 0 / 0 / 0 / 0 |
| 14_roger_encounter | 140 / 193 / 34 / 28 / 57 / 135 | 0 / 1 / 1 / 0 / 0 / 0 |

### 2.3 What the numbers say

- The **backbone is live and verified**: the task chain and frame machine (001ACEC0, 001AD250, 0x1AE040, 001AE5E0/001AE6B0, 001AE7E0), the fades, the actor pool and roster, spawn placement, the load veil state machine, the input block, the panel/battery/elevator interaction host, the status page core and BATTERY page, the pose host, the motor, the wall probes and the point lights.
- The **player, camera and collision run legacy code**, except the player stage itself: since L01 (2026-09-23) 0015BA50, 0015B130 and 0015BCF0's tail run every stage with the translated 0021C440, 0015D100 and 0015D000 around the port's idle/walk callbacks. The scripted takeover is still the interaction runtime's: it consumes the stage before 0015B130's prelude, so the bound prelude and +4 = 4 workers (00182B30, 00182D70, 0015B530, 001837A0) are not reached and stay verified-unbound. The other translations exist and are verified (FLOOR, fall, slide, climb, ladders, running jump, follow camera, AREA11 camera specials, move/probe/segment walkers, actor collision) but none is bound. Every route beat from 05 on depends on them.
- The **set pieces after the elevator are verified but unbound**: truck, director beats, Roger, fan, door, crates and drums, pickups. Their live counterparts are the legacy stand-ins named in the tables.
- The **true gaps** (missing) are concentrated in the effect manager and effect kinds, the render context / HUD bar path, the player equipment actors, a few animation-runtime leaves, the actor list passes and several startup/load helpers.

## 3. Per-subsystem tables

Every non-boundary function, grouped by address range. Columns: address, name (when the decomp has one), decomp status, port status, the translating module and the test that verifies it, the live stand-in or a note, and the first label where the census saw it (a trailing `*` marks functions that run only before first control).

### 3.1 Main-loop tasks, frame machine and fades (0x1AA200..0x1AEFFF)

32 functions, 2,205 instructions: live 24, verified-unbound 2, unverified 4, missing 2.

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001AAD00 | — | NM | verified-unbound | em_actor_collision — test_actor_collision_reference | em_area11_interaction_host_publish (Use list swap only; rest UM_001AAD00) | S2_opening |
| 0x001AB4E0 | — | BM | missing |  |  | S0_title |
| 0x001AB590 | — | BM | missing |  |  | S0_title |
| 0x001AB6A0 | — | BM | unverified | em_task.c | tests/task_test.c fixture only | S0_title |
| 0x001AB740 | — | BM | unverified | em_task.c | tests/task_test.c fixture only | S0_title |
| 0x001AB790 | — | BM | unverified | em_task.c | tests/task_test.c fixture only | S0_title* |
| 0x001AB7D0 | — | BM | verified-unbound | em_status_scene_original — test_status_scene_reference.py |  | S0_title |
| 0x001ABF90 | — | BM | live | em_scene_task, em_scene_bindings — test_scene_task_reference.py |  | S0_title* |
| 0x001AC070 | — | BM | unverified | em_frontend.c / em_game.c continue task | legacy continue prompt (interim); title/New Game route decoded, not oracle-tested | S0_title* |
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

28 functions, 1,504 instructions: live 13, verified-unbound 9, stand-in 4, missing 2.

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001AF2C0 | — | NM | live | em_game.c em_game_new_game_reset_001AF2C0 + em_pickup_reset — test_continue_reset_reference |  | S0_title* |
| 0x001AF470 | — | AW | missing |  |  | S0_title* |
| 0x001AF5C0 | — | BM | stand-in |  | em_game.c em_game_legacy_state0 (native re-arm; the pool reset 001AF8E0 is translated) | S1_newgame_load* |
| 0x001AF690 | — | BM | stand-in |  | em_game.c em_game_legacy_state0 (native re-arm; the pool reset 001AF8E0 is translated) | S1_newgame_load* |
| 0x001AF710 | — | BM | stand-in |  | em_game.c em_game_legacy_state0 (native re-arm; the pool reset 001AF8E0 is translated) | S1_newgame_load* |
| 0x001AF780 | — | AW | verified-unbound | em_roger_actor_original, em_owner_services_original — test_owner_services_reference.py, test_roger_actor_original_reference.py |  | S2_opening |
| 0x001AF800 | — | NM | live | em_actor_pool, em_status_scene_original — test_actor_census_reference.py, test_actor_pool_reference.py, test_status_scene_reference.py |  | S2_opening |
| 0x001AF890 | — | CL | verified-unbound | em_roger_actor_original — test_roger_actor_original_reference.py |  | S2_opening |
| 0x001AF8E0 | — | NM | live | em_actor_pool.c em_actor_pool_reset_001AF8E0 — test_actor_pool_reference; test_actor_census_reference |  | S1_newgame_load* |
| 0x001AFA50 | — | BM | live | em_actor_pool — test_actor_census_reference.py, test_actor_pool_reference.py |  | S1_newgame_load |
| 0x001AFA90 | — | BM | live | em_actor_pool, em_player_closure_0e_18 — test_actor_census_reference.py, test_actor_pool_reference.py |  | S1_newgame_load |
| 0x001AFBC0 | — | BM | live | em_actor_pool — test_actor_census_reference.py, test_actor_pool_reference.py |  | S2_opening |
| 0x001AFC10 | — | BM | live | em_actor_pool.c em_actor_pool_free_001AFC10 — test_actor_pool_reference; test_actor_census_reference |  | S2_opening |
| 0x001AFCA0 | — | BM | stand-in |  | em_game.c em_game_legacy_state0 (native re-arm; the pool reset 001AF8E0 is translated) | S1_newgame_load* |
| 0x001AFCF0 | — | BM | live | em_scene_task, em_scene_bindings — test_room_move_reference.py, test_scene_task_reference.py |  | S1_newgame_load |
| 0x001AFD70 | — | BM | live | em_actor_pool.c em_actor_pool_walk_001AFD70 — test_actor_pool_reference; test_actor_census_reference | nodes run legacy code or no code per em_area11_bindings.c | S2_opening |
| 0x001AFE60 | — | BM | live | em_status_scene_original.c via em_status_models / host — test_status_scene_reference |  | 01_battery |
| 0x001AFEB0 | — | BM | live | em_status_scene_original.c via em_status_models / host — test_status_scene_reference |  | 01_battery |
| 0x001B0070 | — | BM | live | em_player_stage_workers (0015D100's read of the canonical D_008106C8 word, bound by em_player_stage_live, L01), em_head_sprite_original — test_player_stage_workers_reference.py, test_head_sprite_reference.py |  | S0_title |
| 0x001B0080 | — | BM | verified-unbound | em_script_host_workers — test_script_host_workers_reference | legacy door camera re-seat (CAM-07) | S1_newgame_load |
| 0x001B0250 | — | BM | live | em_spawn_table, em_script_host_workers — test_spawn_place_reference.py |  | S1_newgame_load |
| 0x001B0460 | — | BM | verified-unbound | em_script_host_workers — test_script_host_workers_reference | em_game.c em_game_legacy_camera_rearm (reported UM_001B0460) | S1_newgame_load |
| 0x001B07C0 | — | BM | live | em_spawn_table, em_scene_bindings — test_spawn_place_reference.py | reads D_00810707 from the canonical progress byte (HK) | S1_newgame_load |
| 0x001B0B50 | — | BM | verified-unbound | em_player_closure_10_12_19, em_script_host_workers — test_player_closure_10_12_19_reference.py, test_script_host_workers_reference.py |  | S1_newgame_load |
| 0x001B0DC0 | — | BM | verified-unbound | em_owner_services_original — test_owner_services_reference.py |  | S2_opening* |
| 0x001B0EA0 | — | NM | verified-unbound | em_owner_services_original, em_fan_original — test_fan_original_reference.py, test_owner_services_reference.py |  | S2_opening* |
| 0x001B0F60 | — | BM | missing |  |  | S2_opening* |
| 0x001B0FD0 | — | BM | verified-unbound | em_owner_services_original, em_truck_original — test_crate_original_reference.py, test_drum_original_reference.py, test_fan_original_reference.py |  | S2_opening* |

### 3.3 Input block and roster spawn (0x1B5000..0x1B6BEF)

14 functions, 865 instructions: live 9, verified-unbound 3, unverified 2.

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001B57E0 | — | BM | unverified | em_frame.c em_frame_scene_input (step C) |  | S0_title |
| 0x001B5940 | — | AI | live | em_input.c / em_frame.c pad block — test_input_block_reference |  | S0_title |
| 0x001B5B70 | — | BM | verified-unbound | em_owner_services_original — test_owner_services_reference | step I rumble countdown not called live (ORCH-22) | S0_title |
| 0x001B5C90 | — | AI | live | em_input.c / em_frame.c pad block — test_input_block_reference |  | 00_panel_no_battery |
| 0x001B5CC0 | — | AI | live | em_input.c / em_frame.c pad block — test_input_block_reference |  | S0_title |
| 0x001B5D70 | — | AW | live | em_input.c / em_frame.c pad block — test_input_block_reference |  | 00_panel_no_battery |
| 0x001B5E20 | — | AW | live | em_input.c / em_frame.c pad block — test_input_block_reference |  | 03_panel_power |
| 0x001B5F40 | — | AW | unverified | em_frame.c frame_input_read (pad state byte) | partial | S0_title |
| 0x001B61C0 | — | BM | verified-unbound | em_player_ladder_entry, em_player_recovery — test_owner_services_reference.py, test_player_fall_reference.py, test_player_ladder_climb_reference.py |  | S2_opening |
| 0x001B6250 | — | BM | verified-unbound | em_script_host_workers, em_owner_services_original — test_owner_services_reference.py, test_script_host_workers_reference.py |  | S2_opening |
| 0x001B65C0 | — | NM | live | em_actor_roster — test_actor_census_reference.py |  | S1_newgame_load* |
| 0x001B6660 | — | BM | live | em_actor_roster, em_scene_state — test_actor_census_reference.py |  | S1_newgame_load* |
| 0x001B6910 | — | BM | live | em_actor_roster, em_scene_bindings — test_actor_census_reference.py |  | S1_newgame_load* |
| 0x001B6990 | — | BM | live | em_scene_bindings, em_actor_roster — test_actor_census_reference.py |  | S1_newgame_load* |

### 3.4 Player states (0x15B000..0x173FFF)

34 functions, 10,019 instructions: live 2, verified-unbound 27, unverified 1, stand-in 4.

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x0015B130 | — | BM | live | em_player_floor.c em_player_stage_0015B130 (every +4 = 1 stage since L01, em_player.c live_major1) — test_player_floor_reference (stage cases); test_level_smoke.py | state[0]/[1] are the port's idle/walk callbacks (L12); the interaction runtime still stands in for the takeover while it owns the player (acquire / +4 = 4 ticks / release); under 0x70003B8D without that owner the port's idle/walk keep the stage (the prelude's 00174A50 needs 0017B490, L12) | S2_opening |
| 0x0015B530 | — | AI | verified-unbound | em_player_stage_workers em_player_stage_0015B530 (bound as stage major[4] by L01, but unreached) — test_player_stage_workers_reference | the original runs it on every scripted takeover (12 of 19 labels); the port runs the interaction runtime instead (em_player_pose_host.c player_pose_acquire / the takeover tick through player_pose_stage_hook / player_pose_release), which consumes the stage at 0015B130's prelude position, so the +4 = 4 stage is never entered; reached once the takeover moves onto the stage. Of its routines 001837A0 is bound; 00182DF0's record side, 001837B0, 001838B0, 00183910 are untranslated and 00162DB0/00163B40 are FLOOR (fail-stop workers) | S2_opening |
| 0x0015BA50 | — | BM | live | em_player_floor.c em_player_stage_begin/_dispatch/_end over the record every stage (em_player.c player_states_stage, L01) — test_player_floor_reference (stage cases); test_level_smoke.py | the advance worker is the live display's 001C64F0 (em_player_pose_advance through player_pose_stage_advance); D_00248C98 from the local export; the B3 byte is still em_player_0015BCF0's stand-in expression | S2_opening |
| 0x0015BCF0 | — | BM | live (partial: tail only) | em_player_floor.c em_player_stage_tail (+BC, the -200 check, the +31B loop-sound stop; L01) inside em_player_frame.c em_player_0015BCF0 — test_player_floor_reference (stage cases) | the skeleton evaluation, 0015CBA0, 00187350 and the +A0/+B0 copies are the port's own display and camera paths | S2_opening |
| 0x0015BF90 | — | NM | verified-unbound | em_shadow_actor_route — test_shadow_actor_route_reference | no live player shadow | 02_elevator_refusal |
| 0x0015C160 | — | BM | verified-unbound | em_shadow_original — test_shadow_original_reference | live w_0015C160 is a reported no-effect binding (UM_0015C160) | S2_opening |
| 0x0015C1F0 | — | NM | verified-unbound | em_player_misc_workers — test_player_misc_workers_reference | live spawn_w_0015C1F0 is a reported no-effect binding (UM_0015C1F0) | S1_newgame_load |
| 0x0015C310 | — | BM | live | em_area11_bindings.c em_area11_spawn_player_children_0015C420 — compare_frame_order.py; test_level_smoke.py (census 49) | spawn set and order only (node bytes not compared) | S2_opening |
| 0x0015C420 | — | BM | live | em_area11_bindings.c em_area11_spawn_player_children_0015C420 — compare_frame_order.py; test_level_smoke.py (census 49) | spawn set and order only | S2_opening* |
| 0x0015CBA0 | — | BM | stand-in |  | em_camera.c constant height row (no +0x236 state map) | S2_opening |
| 0x0015CF90 | — | BM | unverified | em_player_frame.c em_player_0015BCF0: D_00810707 = +0x234 into the canonical progress byte (HK) and the B9 write, over the stage's vitals (em_player.c stores +220/+234 back to g.status / g.pd_infected after every stage, L01) | D_00810706/858/85C have no canonical storage (their port copies g.pd_low / g.status are the stage's store); no oracle executes 0015CF90 (the byte-matched C was read) | S2_opening |
| 0x0015D000 | — | AI | live | em_player_stage_workers em_player_stage_heartbeat (0015B130, L01) — test_player_stage_workers_reference | its rumble 001B61C0 (health <= 35) is a fail-stop worker (untranslated) | S2_opening |
| 0x0015D100 | — | BM | live | em_player_stage_workers em_player_stage_drain (0015B130, L01; em_player_damage.c's copy retired) — test_player_stage_workers_reference | 0015C9D0 and 001F0060 (the latch / infected paths) are fail-stop workers | S2_opening |
| 0x0015D2F0 | — | BM | stand-in |  | em_weapon.c assumes variant 0 (ordinary camera mode) | S0_title |
| 0x0015D4C0 | — | NM | verified-unbound | em_player_ladder_entry — test_player_ladder_entry_reference | none: the live player has no climb/vault/ladder/jump states (level smoke phase NOT-LIVE); Use chain | 05_boxes |
| 0x0015DEC0 | — | AW | verified-unbound | em_player_climb — test_player_climb_reference | none: the live player has no climb/vault/ladder/jump states (level smoke phase NOT-LIVE) | 05_boxes |
| 0x0015DF10 | — | NM | verified-unbound | em_player_climb — test_player_climb_reference | none: the live player has no climb/vault/ladder/jump states (level smoke phase NOT-LIVE) | 05_boxes |
| 0x0015EC50 | — | NM | verified-unbound | em_player_running_jump — test_player_running_jump_reference | none: the live player has no climb/vault/ladder/jump states (level smoke phase NOT-LIVE) | 12_crevice_jump |
| 0x00160220 | — | BM | verified-unbound | em_player_ladder_entry (whole function) — test_player_ladder_entry_reference | em_area11_interaction_host.c Use poll (the 00160220 head + 00184BA0/00183EF0; level smoke routes 02-04); live head only | S3_first_control_idle |
| 0x001607D0 | — | BM | verified-unbound | em_player_weapon_states_a — test_player_weapon_states_a_reference |  | S3_first_control_idle |
| 0x00161020 | — | NM | stand-in |  | em_player.c/em_player_frame.c legacy idle callback; partial: pose-host 00161020 cases and foot stop are translated (test_player_pose_host_reference) | S2_opening |
| 0x001612D0 | — | NM | stand-in |  | em_player.c legacy walk callback; partial: motor 0017BC40 and heading 00174AC0 are translated and live | 00_panel_no_battery |
| 0x00161690 | — | BM | verified-unbound | em_player_climb — test_player_climb_reference | none: the live player has no climb/vault/ladder/jump states (level smoke phase NOT-LIVE) | 05_boxes |
| 0x00161790 | — | BM | verified-unbound | em_player_climb — test_player_climb_reference | none: the live player has no climb/vault/ladder/jump states (level smoke phase NOT-LIVE) | 05_boxes |
| 0x00162DB0 | — | NM | verified-unbound | em_player_fall — test_player_fall_reference | em_player.c player_move_collide floor snap / PLAYER_FALL_ENTRY (port-side) | 10_cage_roof_roger |
| 0x001634A0 | — | NM | verified-unbound | em_player_running_jump — test_player_running_jump_reference | none: the live player has no climb/vault/ladder/jump states (level smoke phase NOT-LIVE) | 12_crevice_jump |
| 0x00163B40 | — | AW | verified-unbound | em_player_fall — test_player_fall_reference | em_player.c player_move_collide floor snap / PLAYER_FALL_ENTRY (port-side) | 10_cage_roof_roger |
| 0x00163C10 | — | BM | verified-unbound | em_player_fall — test_player_fall_reference | em_player.c player_move_collide floor snap / PLAYER_FALL_ENTRY (port-side) | 10_cage_roof_roger |
| 0x00165B60 | — | BM | verified-unbound | em_player_ladder_entry — test_player_ladder_entry_reference | none: the live player has no climb/vault/ladder/jump states (level smoke phase NOT-LIVE) | 10_cage_roof_roger |
| 0x001662D0 | — | NM | verified-unbound | em_player_ladder_climb — test_player_ladder_climb_reference | none: the live player has no climb/vault/ladder/jump states (level smoke phase NOT-LIVE) | 10_cage_roof_roger |
| 0x0016C520 | — | BM | verified-unbound | em_player_slide — test_player_slide_reference | em_player.c collide-and-slide movement on the slope (no slide state) | 06_hill_slide |
| 0x0016C570 | — | BM | verified-unbound | em_player_slide — test_player_slide_reference | em_player.c collide-and-slide movement on the slope (no slide state) | 06_hill_slide |
| 0x0016C6A0 | — | NM | verified-unbound | em_player_slide — test_player_slide_reference | em_player.c collide-and-slide movement on the slope (no slide state) | 06_hill_slide |
| 0x0016CD70 | — | BM | verified-unbound | em_player_slide — test_player_slide_reference | em_player.c collide-and-slide movement on the slope (no slide state) | 06_hill_slide |

### 3.5 Player workers, pose and animation glue (0x174000..0x18AFFF)

84 functions, 9,097 instructions: live 23, verified-unbound 45, unverified 2, stand-in 4, missing 10.

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001749A0 | — | BM | live | em_player_pose.c em_player_pose_select — test_player_pose_reference |  | S2_opening |
| 0x001749F0 | anim_clip_arbiter | BM | verified-unbound | em_pose_host_workers — test_pose_host_workers_reference | em_player_pose.c em_player_pose_select (no oracle of this entry) | 00_panel_no_battery |
| 0x00174A50 | — | BM | live | em_player_pose.c em_player_pose_acquire — test_player_pose_reference | the stage's translation em_player_stage_row_request is bound too (L01) but reached only by 0015B130's prelude outside the port's idle/walk; its 0017B490 / 001749A0 on the record are fail-stop (L12) | S2_opening |
| 0x00174AB0 | — | BM | verified-unbound | em_player_closure_0e_18 — test_player_closure_0e_18_reference |  | 01_battery |
| 0x00174AC0 | — | BM | live | em_player_heading.c + em_player.c turn — test_player_heading_reference | turn path only; the reversal arm (+1F0 = 7) is gated; its SDK trig is host-modelled | S3_first_control_idle |
| 0x00174FD0 | — | BM | verified-unbound | em_player_slide — test_player_slide_reference | em_player.c collide-and-slide movement on the slope (no slide state) | 06_hill_slide |
| 0x001751A0 | — | NM | verified-unbound | em_player_recovery — test_player_recovery_reference |  | 12_crevice_jump |
| 0x00175640 | — | BM | verified-unbound | em_actor_collision (em_player_link_00175640) — test_player_floor_reference |  | 06_hill_slide |
| 0x001756E0 | — | NM | live | em_player_floor.c em_player_clearance_release (via em_player.c) — test_player_probe_reference |  | S2_opening |
| 0x00175900 | — | NM | verified-unbound | em_player_floor.c floor service (gated) — test_player_floor_reference | em_player.c player_move_collide floor snap / PLAYER_FALL_ENTRY (port-side) | S2_opening |
| 0x00175CF0 | — | NM | verified-unbound | em_player_floor.c floor service (gated) — test_player_floor_reference | em_player.c player_move_collide floor snap / PLAYER_FALL_ENTRY (port-side) | S2_opening |
| 0x001760C0 | — | BM | verified-unbound | em_player_misc_workers — test_player_misc_workers_reference | em_player.c probe_column over em_collision_segment_query (not the 0019F730/0019C830 walkers) | S2_opening |
| 0x001762E0 | — | BM | live | em_player_floor.c wall probes — test_player_probe_reference | its area-2 target-shove worker faults if reached | 01_battery |
| 0x00176390 | — | NM | live | em_player_floor.c em_player_wall_probes — test_player_probe_reference |  | 01_battery |
| 0x001764E0 | — | NM | live | em_player_floor.c em_player_wall_probes — test_player_probe_reference |  | S2_opening |
| 0x00176BE0 | — | AW | live | em_player_floor.c em_player_wall_probes — test_player_probe_reference |  | 01_battery |
| 0x00176C80 | — | BM | live | em_player_floor.c em_player_wall_probes — test_player_probe_reference |  | S2_opening |
| 0x00176F90 | — | BM | verified-unbound | em_player_ladder_entry — test_player_ladder_entry_reference | none: the live player has no climb/vault/ladder/jump states (level smoke phase NOT-LIVE) | 05_boxes |
| 0x00177030 | — | NM | verified-unbound | em_player_ladder_entry — test_player_ladder_entry_reference | none: the live player has no climb/vault/ladder/jump states (level smoke phase NOT-LIVE) | 10_cage_roof_roger |
| 0x00177460 | — | AW | verified-unbound | em_player_climb — test_player_climb_reference | none: the live player has no climb/vault/ladder/jump states (level smoke phase NOT-LIVE) | 05_boxes |
| 0x00177510 | — | BM | verified-unbound | em_player_climb — test_player_climb_reference | none: the live player has no climb/vault/ladder/jump states (level smoke phase NOT-LIVE) | 05_boxes |
| 0x001775E0 | — | BM | verified-unbound | em_player_climb — test_player_climb_reference | none: the live player has no climb/vault/ladder/jump states (level smoke phase NOT-LIVE) | 05_boxes |
| 0x00177F40 | — | NM | verified-unbound | em_player_climb — test_player_climb_reference | none: the live player has no climb/vault/ladder/jump states (level smoke phase NOT-LIVE) | 05_boxes |
| 0x00178B90 | — | AI | verified-unbound | em_player_recovery — test_player_recovery_reference | em_player.c walk translation (displacement 9.599989 vs original 9.599849) | 00_panel_no_battery |
| 0x00178EC0 | — | BM | verified-unbound | em_player_recovery — test_player_recovery_reference |  | 12_crevice_jump |
| 0x001791D0 | — | BM | verified-unbound | em_player_slide — test_player_slide_reference | em_player.c collide-and-slide movement on the slope (no slide state) | 06_hill_slide |
| 0x00179450 | — | BM | verified-unbound | em_player_floor.c fall check (gated) — test_player_floor_reference | em_player.c player_move_collide floor snap / PLAYER_FALL_ENTRY (port-side) | 06_hill_slide |
| 0x00179680 | — | BM | verified-unbound | em_player_floor.c fall check (gated) — test_player_floor_reference | em_player.c player_move_collide floor snap / PLAYER_FALL_ENTRY (port-side) | 10_cage_roof_roger |
| 0x001796C0 | — | NM | verified-unbound | em_player_floor.c fall check (gated) — test_player_floor_reference | em_player.c player_move_collide floor snap / PLAYER_FALL_ENTRY (port-side) | S2_opening |
| 0x00179880 | — | BM | verified-unbound | em_player_fall — test_player_fall_reference |  | 10_cage_roof_roger |
| 0x001798D0 | — | BM | live | em_player_pose_host.c player_pose_use_accepted — test_player_pose_host_reference |  | 00_panel_no_battery |
| 0x00179B90 | — | BM | live | em_player.c footstep_rand5 / em_weapon wpn_rand over em_random — test_player_random_reference |  | 00_panel_no_battery |
| 0x00179D20 | — | BM | missing |  | hooked (not compared) by test_player_pose_reference | 00_panel_no_battery |
| 0x00179FF0 | — | BM | missing |  | hooked (not compared) by test_player_pose_reference | 00_panel_no_battery |
| 0x0017B460 | — | BM | stand-in |  | em_player_frame.c loco_clip_for_tier (D_00248AB0 lookup) | S2_opening |
| 0x0017B490 | — | BM | verified-unbound | em_player_reversal — test_player_reversal_reference | em_player_frame.c loco_clip_for_tier (unverified duplicate) | S2_opening |
| 0x0017B5C0 | — | BM | unverified | em_player.c eight-tick entry blend | first-control capture endpoint close, not identical | 00_panel_no_battery |
| 0x0017B660 | anim_matrix_player | NM | stand-in |  | em_player_frame.c actor_update gait blend (matrix lerps; WP-15 P12/P13); source-state side effects translated in em_player_pose.c em_player_pose_gait_base (test_player_pose_reference) | 00_panel_no_battery |
| 0x0017B910 | — | NM | live | em_player_pose_host.c / em_player_foot_stop.c — test_player_foot_stop_reference; test_player_pose_host_reference |  | 02_elevator_refusal |
| 0x0017BC40 | — | BM | live | em_player_motor.c em_player_motor_tick — test_player_motor_reference |  | 00_panel_no_battery |
| 0x0017C030 | — | BM | verified-unbound | em_player_reversal — test_player_reversal_reference | em_player.c legacy clip requests (gait tiers, stop clip 5) | 00_panel_no_battery |
| 0x0017C440 | — | BM | live | em_player_motor.c em_player_reentry_tick — test_player_reentry_reference.py | stop-interruption metadata; the translation/clip callees are boundaries in that oracle | 10_cage_roof_roger |
| 0x0017C540 | — | BM | live | em_player_motor.c em_player_reentry_tick — test_player_reentry_reference.py | stop-interruption metadata; the translation/clip callees are boundaries in that oracle | 05_boxes |
| 0x0017C580 | — | NM | verified-unbound | em_player_fall — test_player_fall_reference |  | 10_cage_roof_roger |
| 0x0017C860 | — | NM | verified-unbound | em_player_recovery — test_player_recovery_reference |  | 12_crevice_jump |
| 0x0017D800 | — | BM | verified-unbound | em_player_climb — test_player_climb_reference | none: the live player has no climb/vault/ladder/jump states (level smoke phase NOT-LIVE) | 05_boxes |
| 0x0017D8D0 | — | BM | verified-unbound | em_player_climb — test_player_climb_reference | none: the live player has no climb/vault/ladder/jump states (level smoke phase NOT-LIVE) | 05_boxes |
| 0x0017DEB0 | — | AW | verified-unbound | em_player_climb — test_player_climb_reference | none: the live player has no climb/vault/ladder/jump states (level smoke phase NOT-LIVE) | 05_boxes |
| 0x0017F5F0 | — | NM | verified-unbound | em_player_slide — test_player_slide_reference | em_player.c collide-and-slide movement on the slope (no slide state) | 06_hill_slide |
| 0x0017FC80 | — | BM | verified-unbound | em_player_ladder_climb — test_player_ladder_climb_reference | none: the live player has no climb/vault/ladder/jump states (level smoke phase NOT-LIVE) | 10_cage_roof_roger |
| 0x0017FD00 | — | BM | verified-unbound | em_player_ladder_climb — test_player_ladder_climb_reference | none: the live player has no climb/vault/ladder/jump states (level smoke phase NOT-LIVE) | 10_cage_roof_roger |
| 0x00180300 | — | BM | verified-unbound | em_player_closure_0e_18 / em_player_ladder_entry — test_player_ladder_entry_reference | none: the live player has no climb/vault/ladder/jump states (level smoke phase NOT-LIVE) | 10_cage_roof_roger |
| 0x00180420 | — | BM | verified-unbound | em_player_ladder_climb — test_player_ladder_climb_reference | none: the live player has no climb/vault/ladder/jump states (level smoke phase NOT-LIVE) | 10_cage_roof_roger |
| 0x00180460 | — | BM | verified-unbound | em_player_ladder_climb — test_player_ladder_climb_reference | none: the live player has no climb/vault/ladder/jump states (level smoke phase NOT-LIVE) | 10_cage_roof_roger |
| 0x00181110 | — | AW | verified-unbound | em_player_major2 — test_player_major2_reference |  | 10_cage_roof_roger |
| 0x00182430 | — | AW | live | em_player.c footstep sound id (00182430 mapping over em_random) — test_player_random_reference | the footstep clock itself is legacy (00187350) | 00_panel_no_battery |
| 0x00182870 | — | BM | verified-unbound | em_player_reaction — test_player_reaction_reference |  | 05_boxes |
| 0x00182A70 | — | BM | verified-unbound | em_player_ladder_entry — test_player_ladder_entry_reference |  | 10_cage_roof_roger |
| 0x00182B30 | — | BM | verified-unbound | em_player_stage_workers em_player_stage_scripted_check (bound in 0015B130's prelude by L01, but unreached) — test_player_stage_workers_reference | em_player_pose_host.c player_pose_acquire / the takeover tick / player_pose_release through the interaction runtime (partial refusal set), which consumes the stage before the prelude; the prelude runs only under 0x70003B8D outside the port's idle/walk, which the live app cannot reach before FLOOR (L02) and L12. Reached once the takeover moves onto the stage | S2_opening |
| 0x00182BF0 | — | NM | verified-unbound | em_script_host_workers — test_script_host_workers_reference |  | 10_cage_roof_roger |
| 0x00182D40 | — | BM | unverified | em_player_pose_host.c release tail | hooked in the pose oracles | S2_opening |
| 0x00182D70 | — | BM | verified-unbound | em_player_stage_workers em_player_stage_scripted_notify (bound in 0015B130's prelude by L01, but unreached) — test_player_stage_workers_reference | as 00182B30: the interaction runtime's player_pose_acquire / takeover tick / player_pose_release stand in (00182D70's record-side writes +0 = 1, +24C = -1, +1F4 and the pending clears are not made); link1C is a fail-stop worker (+1C is 0 in every route capture). Reached once the takeover moves onto the stage | S2_opening |
| 0x00182DF0 | — | BM | live | em_player_pose_host.c player_pose_release / legacy_reseed — test_player_pose_host_reference; test_player_cinematic_reference |  | S2_opening |
| 0x00182F90 | — | BM | live | em_player_pose_host.c player_pose_align — test_player_pose_host_reference; test_interaction_alignment_reference |  | S2_opening |
| 0x00183090 | — | BM | live | em_player_pose.c / em_interaction_animation.c commit — test_player_cinematic_reference |  | S2_opening |
| 0x001837A0 | — | BM | verified-unbound | em_player_stage_live.c w_001837A0 (0015B530's +5 = 0 routine, bound by L01 but unreached) — the byte-matched src/func_001837A0.c is an empty function; test_player_stage_workers_reference hooks it as 0015B530's target | as 0015B530: the interaction runtime (player_pose_acquire / the takeover tick / player_pose_release) stands in for the takeover, so 0015B530 and its routines are not entered. Reached once the takeover moves onto the stage | S2_opening |
| 0x00183EF0 | — | BM | live | em_interaction_scan.c via em_area11_interaction_host use — test_interaction_scan_reference; test_level_smoke.py (routes 02-04) | em_pickup.c / em_door.c legacy scans for the unpublished owners; published list holds only the panel and elevator (W22) | 00_panel_no_battery |
| 0x00184BA0 | — | BM | live | em_interaction_scan.c via em_area11_interaction_host use — test_interaction_scan_reference; test_level_smoke.py (routes 02-04) | em_pickup.c / em_door.c legacy scans for the unpublished owners; published list holds only the panel and elevator (W22) | 00_panel_no_battery |
| 0x00187350 | — | BM | verified-unbound | em_player_floor.c footstep (gated) — test_player_footstep_reference | em_player.c footstep_play (legacy clock) | S2_opening |
| 0x00187DC0 | — | BM | verified-unbound | em_player_floor.c first contact (gated) — test_player_floor_reference |  | 08_truck_crossing |
| 0x00187EE0 | — | BM | verified-unbound | em_player_floor — test_player_footstep_reference | em_player.c footstep_play | 00_panel_no_battery |
| 0x001885D0 | — | BM | verified-unbound | em_player_ladder_climb — test_player_ladder_climb_reference |  | 10_cage_roof_roger |
| 0x00188630 | — | BM | stand-in |  | em_weapon.c em_weapon_update (legacy gun tick) | S2_opening |
| 0x00188A50 | — | BM | missing |  | player equipment/weapon actor code | S2_opening |
| 0x00188AC0 | — | BM | missing |  | player equipment/weapon actor code | S2_opening |
| 0x00188B80 | — | BM | missing |  | player equipment/weapon actor code | S2_opening |
| 0x00188DF0 | — | BM | missing |  | player equipment/weapon actor code | S2_opening |
| 0x00188ED0 | — | BM | stand-in |  | em_weapon.c flashlight gate + em_gfx spot term (stand-in, R03/R04) | S2_opening |
| 0x00189D30 | — | BM | missing |  | player equipment/weapon actor code | S2_opening |
| 0x0018A1F0 | — | BM | missing |  | player equipment/weapon actor code | S2_opening |
| 0x0018A6B0 | — | BM | missing |  | pool node bound with no port code ('player equipment: no port draw') | S2_opening |
| 0x0018A880 | — | BM | live | em_area11_bindings.c spawn_0018A880 — compare_frame_order.py; test_level_smoke.py (census 49) | spawn only | S2_opening |
| 0x0018A8D0 | — | BM | missing |  | player equipment/weapon actor code | S2_opening |
| 0x0018AB00 | — | BM | live | em_scene_task.c em_sf_0018AB00 — test_room_move_reference |  | 09_fence_door |

### 3.6 Camera (0x18B000..0x199FFF)

27 functions, 7,740 instructions: live 2, verified-unbound 17, unverified 2, stand-in 3, missing 3.

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x0018B9C0 | — | NM | verified-unbound | em_camera_follow_original — test_camera_follow_original_reference | em_render_frame.c em_camera_0018B9C0 -> em_camera.c camera_update (legacy follow camera with partly translated solvers); D_008106EF cooldown translated live | S2_opening |
| 0x0018BC20 | — | NM | stand-in |  | em_camera.c camera_mode_dispatch (port's own action dispatch) | S2_opening |
| 0x0018C0C0 | — | BM | unverified | em_camera.c camera_solve target copy |  | S2_opening |
| 0x0018C0D0 | — | BM | live | em_camera.c camera_commit_original — test_camera_commit_reference | state-4 call is a reported no-effect binding | S1_newgame_load |
| 0x0018C4B0 | — | AW | verified-unbound | em_camera_follow_original — test_camera_follow_original_reference | em_camera.c cam_chase_h/v | S2_opening |
| 0x0018C5A0 | — | NM | missing |  |  | S2_opening |
| 0x0018C6A0 | — | AW | verified-unbound | em_camera_follow_original — test_camera_follow_original_reference | em_camera.c cam_chase_h/v | S2_opening |
| 0x0018CBD0 | — | NM | live | em_camera_retarget.c + em_camera_rotation.c — test_camera_retarget_reference; test_camera_rotation_reference; test_level_smoke.py (routes 02/04) |  | 00_panel_no_battery |
| 0x0018CE60 | — | NM | unverified | em_game.c cam_bounds_settle_0018CE60 |  | S2_opening* |
| 0x0018D330 | — | BM | verified-unbound | em_camera_follow_original — test_camera_follow_original_reference | em_camera.c camera_update (legacy follow camera with partly translated solvers); live only inside the scripted retarget (em_camera_probe.c, test_camera_probe_reference) | S2_opening |
| 0x0018D7B0 | — | BM | verified-unbound | em_camera_follow_original — test_camera_follow_original_reference | em_camera.c camera_update (legacy follow camera with partly translated solvers); styles 5/1 live in em_camera.c camera_interaction_retarget_distance_area11 (level smoke routes 02/04); state-4 call reported no-effect | S2_opening |
| 0x0018D910 | — | NM | verified-unbound | em_camera_follow_original — test_camera_follow_original_reference | em_camera.c camera_update (legacy follow camera with partly translated solvers); AREA11 bounds branch live in em_camera_probe.c (test_camera_probe_reference) | 00_panel_no_battery |
| 0x0018DD20 | — | NM | verified-unbound | em_camera_follow_original — test_camera_follow_original_reference | em_camera.c cam_solver_0018DD20 (unverified duplicate; scripted cameras match captures) | S2_opening |
| 0x00190F20 | — | BM | missing |  |  | S2_opening |
| 0x00191000 | — | BM | verified-unbound | em_camera_area11_specials — test_camera_area11_specials_reference | em_camera.c camera_mode_dispatch (port's own action dispatch) | S3_first_control_idle |
| 0x00191210 | — | BM | verified-unbound | em_camera_area11_specials — test_camera_area11_specials_reference | em_camera.c camera_mode_dispatch (port's own action dispatch) | S3_first_control_idle |
| 0x00191390 | — | AW | verified-unbound | em_camera_follow_original — test_camera_follow_original_reference | em_camera.c camera_prestep_00191390 (unverified duplicate) | S2_opening |
| 0x001914A0 | — | BM | stand-in |  | em_camera.c mode-8 settle (CAM-18/19) | S2_opening |
| 0x00191580 | — | BM | stand-in |  | em_camera.c mode-8 settle (CAM-18/19) | S2_opening |
| 0x001916C0 | — | NM | verified-unbound | em_camera_area11_specials — test_camera_area11_specials_reference | em_camera.c camera_mode_dispatch (port's own action dispatch) | S2_opening |
| 0x00191D40 | — | NM | verified-unbound | em_camera_follow_original — test_camera_follow_original_reference | em_camera.c cam_eye_y_seek_00191D40 (unverified duplicate) | S3_first_control_idle |
| 0x00192010 | — | AW | verified-unbound | em_camera_follow_original — test_camera_follow_original_reference | em_camera.c camera_update (legacy follow camera with partly translated solvers) | 06_hill_slide |
| 0x001921D0 | — | NM | verified-unbound | em_camera_follow_original — test_camera_follow_original_reference | em_camera.c camera_update (legacy follow camera with partly translated solvers) | S3_first_control_idle |
| 0x00193EB0 | — | NM | verified-unbound | em_camera_area11_specials — test_camera_area11_specials_reference | em_camera.c camera_mode_dispatch (port's own action dispatch) | S3_first_control_idle |
| 0x00195130 | — | NM | verified-unbound | em_camera_area11_specials — test_camera_area11_specials_reference | em_camera.c camera_mode_dispatch (port's own action dispatch) | S3_first_control_idle |
| 0x00199C50 | — | NM | missing |  | reported no-effect binding UM_00199C50 (state 0) | S1_newgame_load* |
| 0x00199DB0 | — | NM | verified-unbound | em_player_ladder_entry — test_player_ladder_entry_reference | none: the live player has no climb/vault/ladder/jump states (level smoke phase NOT-LIVE) | 10_cage_roof_roger |

### 3.7 Collision walkers (0x19A000..0x1A7FFF)

38 functions, 9,919 instructions: verified-unbound 33, stand-in 5.

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x0019A180 | — | NM | verified-unbound | em_player_climb — test_player_climb_reference.py, test_player_recovery_reference.py | em_collision.c (the port's own segment/move/camera queries) | 05_boxes |
| 0x0019A310 | — | AW | verified-unbound | em_player_floor — test_player_floor_reference.py, test_player_probe_reference.py | em_collision.c (the port's own segment/move/camera queries) | S2_opening |
| 0x0019A570 | — | NM | verified-unbound | em_coll_segment_walkers, em_weapon — test_coll_segment_walkers_reference.py, test_drum_original_reference.py, test_player_climb_reference.py | em_collision.c (the port's own segment/move/camera queries) | 02_elevator_refusal |
| 0x0019A910 | — | NM | verified-unbound | em_coll_segment_walkers, em_camera_area11_specials — test_camera_area11_specials_reference.py, test_camera_follow_original_reference.py, test_coll_segment_walkers_reference.py | em_collision.c (the port's own segment/move/camera queries) | S2_opening |
| 0x0019AB20 | — | NM | verified-unbound | em_actor_collision, em_player_misc_workers — test_actor_collision_reference.py, test_coll_probe_reference.py, test_crate_original_reference.py | em_collision.c (the port's own segment/move/camera queries) | S2_opening |
| 0x0019AD00 | — | NM | verified-unbound | em_coll_move_original, em_player_slide — test_coll_move_reference.py, test_player_climb_reference.py, test_player_closure_0e_18_reference.py | em_collision.c (the port's own segment/move/camera queries) | S2_opening |
| 0x0019AFE0 | — | NM | verified-unbound | em_coll_move_original, em_player_misc_workers — test_coll_move_reference.py, test_player_climb_reference.py, test_player_closure_0e_18_reference.py | em_collision.c (the port's own segment/move/camera queries) | S2_opening |
| 0x0019B6C0 | — | NM | verified-unbound | em_coll_probe_original, em_player_floor — test_coll_probe_reference.py, test_coll_segment_walkers_reference.py, test_player_floor_reference.py | em_collision.c (the port's own segment/move/camera queries) | S2_opening |
| 0x0019B7D0 | — | BM | verified-unbound | em_camera_follow_original, em_camera — test_camera_follow_original_reference.py | em_collision.c (the port's own segment/move/camera queries) | S2_opening |
| 0x0019B8C0 | — | NM | verified-unbound | em_coll_probe_original, em_player_floor — test_coll_probe_reference.py, test_player_floor_reference.py | em_collision.c (the port's own segment/move/camera queries) | S2_opening |
| 0x0019BA80 | — | NM | verified-unbound | em_player_ladder_entry — test_player_ladder_entry_reference.py | em_collision.c (the port's own segment/move/camera queries) | 05_boxes |
| 0x0019BC40 | — | NM | verified-unbound | em_actor_collision, em_player_running_jump — test_actor_collision_reference.py, test_player_climb_reference.py, test_player_floor_reference.py | em_collision.c (the port's own segment/move/camera queries) | 05_boxes |
| 0x0019C830 | — | NM | verified-unbound | em_player, em_enemy — test_actor_collision_reference.py, test_coll_probe_reference.py | em_collision.c (the port's own segment/move/camera queries) | S2_opening |
| 0x0019CB60 | — | NM | verified-unbound | em_coll_move_original — test_coll_move_reference.py | em_collision.c (the port's own segment/move/camera queries) | S2_opening |
| 0x0019D330 | — | NM | verified-unbound | em_coll_segment_walkers, em_enemy — test_coll_segment_walkers_reference.py, test_collision_reference.py | em_collision.c (the port's own segment/move/camera queries) | 02_elevator_refusal |
| 0x0019D770 | — | NM | verified-unbound | em_coll_segment_walkers — test_coll_segment_walkers_reference.py | em_collision.c (the port's own segment/move/camera queries) | S2_opening |
| 0x0019DF10 | — | NM | verified-unbound | em_coll_probe_original — test_coll_probe_reference.py, test_coll_segment_walkers_reference.py | em_collision.c (the port's own segment/move/camera queries) | S2_opening |
| 0x0019E280 | — | BM | stand-in |  | em_collision.c (the port's own segment/move/camera queries) | S2_opening |
| 0x0019E640 | — | NM | verified-unbound | em_coll_probe_original — test_coll_probe_reference.py | em_collision.c (the port's own segment/move/camera queries) | S2_opening |
| 0x0019E930 | — | NM | stand-in |  | em_collision.c (the port's own segment/move/camera queries) | 05_boxes |
| 0x0019ED80 | — | AW | verified-unbound | em_coll_probe_original, em_coll_segment_walkers — test_actor_collision_reference.py, test_coll_probe_reference.py, test_coll_segment_walkers_reference.py | em_collision.c (the port's own segment/move/camera queries) | S2_opening |
| 0x0019F1A0 | — | NM | verified-unbound | em_coll_probe_original, em_coll_segment_walkers — test_actor_collision_reference.py, test_coll_probe_reference.py, test_coll_segment_walkers_reference.py | em_collision.c (the port's own segment/move/camera queries) | S2_opening |
| 0x0019F330 | — | AW | stand-in |  | em_collision.c (the port's own segment/move/camera queries) | 05_boxes |
| 0x0019F730 | — | NM | verified-unbound | em_actor_collision, em_player — test_actor_collision_reference.py, test_coll_probe_reference.py | em_collision.c (the port's own segment/move/camera queries) | S2_opening |
| 0x0019FE50 | — | NM | verified-unbound | em_coll_move_original — test_coll_move_reference.py | em_collision.c (the port's own segment/move/camera queries) | S2_opening |
| 0x001A0B10 | — | NM | verified-unbound | em_coll_segment_walkers, em_enemy — test_coll_segment_walkers_reference.py | em_collision.c (the port's own segment/move/camera queries) | 02_elevator_refusal |
| 0x001A1390 | — | NM | verified-unbound | em_coll_segment_walkers — test_coll_segment_walkers_reference.py | em_collision.c (the port's own segment/move/camera queries) | S2_opening |
| 0x001A2370 | — | NM | verified-unbound | em_actor_collision, em_truck_original — test_actor_collision_reference.py, test_truck_original_reference.py | em_collision.c (the port's own segment/move/camera queries) | S2_opening |
| 0x001A2AE0 | — | NM | verified-unbound | em_coll_probe_original, em_coll_segment_walkers — test_coll_probe_reference.py, test_coll_segment_walkers_reference.py | em_collision.c (the port's own segment/move/camera queries) | S2_opening |
| 0x001A32C0 | — | NM | verified-unbound | em_coll_probe_original — test_coll_probe_reference.py | em_collision.c (the port's own segment/move/camera queries) | S2_opening |
| 0x001A3980 | — | NM | stand-in |  | em_collision.c (the port's own segment/move/camera queries) | 05_boxes |
| 0x001A4030 | — | BM | verified-unbound | em_coll_probe_original, em_coll_move_original — test_actor_collision_reference.py, test_coll_move_reference.py, test_coll_probe_reference.py | em_collision.c (the port's own segment/move/camera queries) | 01_battery |
| 0x001A4650 | — | AW | verified-unbound | em_coll_probe_original, em_actor_collision — test_actor_collision_reference.py, test_coll_probe_reference.py | em_collision.c (the port's own segment/move/camera queries) | 00_panel_no_battery |
| 0x001A4D10 | — | AW | verified-unbound | em_coll_move_original — test_coll_move_reference.py, test_collision_faces_reference.py | em_collision.c (the port's own segment/move/camera queries) | 04_elevator_ride |
| 0x001A50A0 | — | NM | verified-unbound | em_coll_segment_walkers, em_coll_probe_original — test_coll_probe_reference.py, test_coll_segment_walkers_reference.py | em_collision.c (the port's own segment/move/camera queries) | 02_elevator_refusal |
| 0x001A5760 | — | AW | verified-unbound | em_actor_collision — test_actor_collision_reference.py | em_collision.c (the port's own segment/move/camera queries) | 05_boxes |
| 0x001A6440 | — | NM | verified-unbound | em_coll_move_original, em_enemy — test_coll_move_reference.py, test_coll_segment_walkers_reference.py | em_collision.c (the port's own segment/move/camera queries) | S2_opening |
| 0x001A7870 | — | NM | stand-in |  | em_collision.c (the port's own segment/move/camera queries) | S2_opening |

### 3.8 Actor list passes (0x1A8000..0x1AA1FF)

9 functions, 738 instructions: stand-in 2, missing 7.

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001A8660 | — | BM | missing |  | actor list swaps/passes of 001AAD00 (hooked in the actor-collision oracle) | 07_truck_preview |
| 0x001A8BE0 | — | BM | stand-in |  | em_enemy.c legacy pair/contact pass; actor list pass hooked by test_actor_collision_reference | S2_opening |
| 0x001A8DA0 | — | BM | missing |  | actor list swaps/passes of 001AAD00 (hooked in the actor-collision oracle) | S2_opening |
| 0x001A9000 | — | BM | stand-in |  | em_enemy.c legacy pair/contact pass; actor list pass hooked by test_actor_collision_reference | S2_opening |
| 0x001A97B0 | — | BM | missing |  | actor list swaps/passes of 001AAD00 (hooked in the actor-collision oracle) | S2_opening |
| 0x001A9B10 | — | BM | missing |  | actor list swaps/passes of 001AAD00 (hooked in the actor-collision oracle) | S2_opening |
| 0x001A9D20 | — | BM | missing |  | actor list swaps/passes of 001AAD00 (hooked in the actor-collision oracle) | S2_opening |
| 0x001A9F60 | — | BM | missing |  | actor list swaps/passes of 001AAD00 (hooked in the actor-collision oracle) | S2_opening |
| 0x001AA140 | — | BM | missing |  | actor list swaps/passes of 001AAD00 (hooked in the actor-collision oracle) | S2_opening |

### 3.9 Entity owners: crates, drums, panel, pickups (0x130000..0x15AFFF)

10 functions, 2,678 instructions: live 5, verified-unbound 4, unverified 1.

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001551B0 | — | NM | verified-unbound | em_crate_original — test_crate_original_reference | em_enemy.c legacy em_enemy_update (pool group 'enemies'); crates r3-6 | S2_opening |
| 0x00156620 | — | NM | verified-unbound | em_drum_original — test_drum_original_reference | em_enemy.c legacy em_enemy_update (pool group 'enemies'); drums r14/15 | S2_opening |
| 0x001575B0 | — | BM | live | em_panel_program.c (op-9 callback: cue 0x3EF) — test_level_smoke.py (route 03 panel windows); test_area11_sfx_reference (cue 0x3EF) | tiny script callback translated inline | 03_panel_power |
| 0x00157860 | — | BM | live | em_panel.c via em_area11_interaction_host (node #26) — test_panel_reference; test_level_smoke.py (route 03) |  | S2_opening |
| 0x00157F60 | — | NM | live | em_panel_program.c + em_area11_interaction_host.c battery_open — test_level_smoke.py (route 03 B0/B1 request) | NEARMISS body; request tail only is exercised | 03_panel_power |
| 0x001580C0 | — | BM | live | em_panel_program.c + em_area11_interaction_host.c power — test_level_smoke.py (route 03 power bit); test_area_script_reference |  | 03_panel_power |
| 0x00159210 | — | BM | live | em_panel.c via em_area11_interaction_host (node #26) — test_panel_reference; test_level_smoke.py (route 03) |  | S2_opening |
| 0x0015AC00 | — | NM | unverified | em_pickup.c (INIT scale switch only) | em_pickup.c legacy pickup; partial; legacy owner | S2_opening* |
| 0x0015AE20 | — | BM | verified-unbound | em_pickup_owner — test_pickup_owner_reference | em_pickup.c legacy scan + 2-frame take; WP-6 | S2_opening |
| 0x0015AFA0 | — | BM | verified-unbound | em_pickup_owner — test_pickup_owner_reference | em_pickup.c legacy scan + 2-frame take; WP-6 | S2_opening |

### 3.10 Vector math and owner services (0x1B1000..0x1B4FFF)

20 functions, 793 instructions: live 3, verified-unbound 11, unverified 5, stand-in 1.

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001B1020 | — | AI | verified-unbound | em_roger_actor_original, em_owner_services_original — test_owner_services_reference.py, test_roger_actor_original_reference.py |  | S2_opening* |
| 0x001B10B0 | — | BM | verified-unbound | em_roger_actor_original, em_enemy — test_roger_actor_original_reference.py |  | S2_opening* |
| 0x001B1190 | — | CL | stand-in |  | em_pickup.c legacy take sets the canonical taken bit | 01_battery |
| 0x001B11E0 | — | NM | live | em_actor_roster, em_enemy — test_actor_census_reference.py |  | S1_newgame_load* |
| 0x001B1240 | — | BM | live | em_camera.c camera_commit_original (inline) — test_camera_commit_reference |  | S1_newgame_load |
| 0x001B12B0 | — | AW | verified-unbound | em_script_host_workers, em_player_slide — test_camera_follow_original_reference.py, test_player_slide_reference.py, test_script_host_workers_reference.py |  | 00_panel_no_battery |
| 0x001B1380 | — | AI | verified-unbound | em_script_host_workers, em_player_misc_workers — test_player_misc_workers_reference.py, test_script_host_workers_reference.py |  | S2_opening |
| 0x001B1470 | — | BM | live | em_player_heading.c / em_camera.c cam_wrap_pi — test_player_heading_reference |  | S1_newgame_load |
| 0x001B15D0 | — | BM | verified-unbound | em_player_misc_workers — test_player_misc_workers_reference.py |  | S2_opening |
| 0x001B1630 | — | NM | verified-unbound | em_owner_services_original — test_owner_services_reference | em_area11_interaction_host.c publication tail (panel and elevator only) | S2_opening |
| 0x001B17A0 | — | BM | verified-unbound | em_owner_services_original — test_owner_services_reference | em_area11_interaction_host.c publication tail (panel and elevator only) | S2_opening |
| 0x001B1B30 | — | BM | unverified | em_door.c (legacy door) |  | S2_opening |
| 0x001B1B70 | — | BM | verified-unbound | em_owner_services_original, em_actor_collision — test_actor_collision_reference.py, test_crate_original_reference.py, test_owner_services_reference.py |  | S2_opening |
| 0x001B1CA0 | — | BM | unverified | em_actor_collision (list classes) | module not linked | 03_panel_power |
| 0x001B1D20 | — | BM | verified-unbound | em_enemy, em_actor_collision — test_actor_collision_reference.py, test_drum_original_reference.py |  | S2_opening |
| 0x001B1D60 | — | BM | unverified | em_actor_collision (list classes) | module not linked | S2_opening |
| 0x001B1DA0 | — | BM | unverified | em_actor_collision (list classes) | module not linked | S2_opening |
| 0x001B1DE0 | — | BM | unverified | em_actor_collision (list classes) | module not linked | S2_opening |
| 0x001B1E20 | — | BM | verified-unbound | em_owner_services_original, em_gamepad — test_owner_services_reference.py |  | S2_opening |
| 0x001B1EA0 | — | AW | verified-unbound | em_director_original, em_manager_008257A0 — test_director_original_reference.py |  | S2_opening |

### 3.11 Script host and script ops (0x1B6BF0..0x1BBD5F)

29 functions, 3,487 instructions: live 8, verified-unbound 10, unverified 9, missing 2.

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001B6BF0 | — | NM | unverified | em_area_script | script op handlers, module not linked | S2_opening |
| 0x001B6E40 | — | BM | unverified | em_area_script | script op handlers, module not linked | 10_cage_roof_roger |
| 0x001B6EA0 | — | BM | verified-unbound | em_pickup_owner — test_pickup_owner_reference | em_pickup.c pickup_take (legacy 2-frame take posting the original request) | 01_battery |
| 0x001B6F00 | — | BM | live | em_interaction_alignment.c — test_interaction_alignment_reference |  | 00_panel_no_battery |
| 0x001B6F80 | — | BM | unverified | em_area_script | script op handlers, module not linked | S2_opening |
| 0x001B6FA0 | — | BM | unverified | em_area_script | script op handlers, module not linked | 10_cage_roof_roger |
| 0x001B7840 | — | BM | unverified | em_area_script | script op handlers, module not linked | 14_roger_encounter |
| 0x001B7B30 | — | BM | verified-unbound | em_cinematic_playback — test_cinematic_playback_reference | em_camera.c retarget/chase hooks (cases 3/5; level smoke routes 02-04) | S2_opening |
| 0x001B7D60 | — | BM | live | em_message_service em_message_op0c via em_message_live (the opening's op0C; the AREA11 host's panel/terminal lines) — test_message_service_reference, test_panel_message_reference | em_director.c kCineBeats for the director lines (WP-10) | S2_opening |
| 0x001B7F90 | — | BM | verified-unbound | em_pickup_motion — test_pickup_motion_reference.py |  | 01_battery |
| 0x001B8020 | — | BM | verified-unbound | em_roger_runtime, em_area_script — test_roger_encounter_reference.py |  | 09_fence_door |
| 0x001B81D0 | — | BM | unverified | em_opening_runtime.c (skeleton bind) / em_area_script | mentioned by test_face_allocation_reference and test_roger_media_reference | S2_opening |
| 0x001B82D0 | — | BM | live | em_script.c / em_opening_runtime.c execute — test_interaction_frame_reference; test_script_reference | op subset used by the opening, panel and elevator scripts | S2_opening |
| 0x001B8FC0 | — | BM | verified-unbound | em_area_script / em_cinematic_playback — test_area_script_reference | em_opening_runtime.c + em_cinematic_camera.c camera track (capture-checked at one frame) | S2_opening |
| 0x001B94F0 | — | BM | verified-unbound | em_area_script / em_roger_runtime — test_roger_encounter_reference | em_door.c legacy walk-to (H13) | S2_opening |
| 0x001B99F0 | — | NM | live | em_panel_program.c / em_opening_runtime.c op 9 — test_player_cinematic_reference; test_area_script_reference |  | 01_battery |
| 0x001B9BA0 | — | BM | live | em_panel_program.c / em_elevator_program.c op 2 — test_level_smoke.py (routes 02-04) |  | 03_panel_power |
| 0x001B9C10 | — | BM | live | em_player_pose_host.c player_pose_face — test_player_pose_host_reference |  | 02_elevator_refusal |
| 0x001BA080 | — | BM | unverified | em_opening_runtime.c op 6 / em_area_script |  | S2_opening |
| 0x001BA1A0 | — | BM | verified-unbound | em_director_original / em_area_script — test_director_original_reference | em_script.c / em_truck.c legacy starts | S2_opening |
| 0x001BA1C0 | — | CL | live | em_manager_008257A0.c — test_manager_8257a0_reference |  | S2_opening |
| 0x001BA1F0 | — | NM | live | em_script.c — test_script_reference |  | S2_opening |
| 0x001BA510 | — | CL | missing |  |  | S2_opening |
| 0x001BA540 | — | BM | verified-unbound | em_roger_actor_original — test_roger_actor_original_reference.py |  | S3_first_control_idle |
| 0x001BA580 | — | NM | verified-unbound | em_roger_actor_original — test_roger_actor_original_reference.py |  | S2_opening |
| 0x001BA8E0 | — | NM | verified-unbound | em_roger_actor_original, em_head_sprite_original — test_roger_actor_original_reference.py |  | S2_opening* |
| 0x001BAC00 | — | BM | unverified | em_opening_runtime.c op 0x14 spawn |  | S2_opening* |
| 0x001BAD40 | — | NM | missing |  |  | S2_opening* |
| 0x001BB0E0 | — | AW | unverified | em_opening_actor.c | opening actors checked by fixtures and one capture frame | S2_opening |

### 3.12 Door (0x1BBD60..0x1BC3FF)

9 functions, 492 instructions: live 1, verified-unbound 5, unverified 2, stand-in 1.

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001BBD60 | — | BM | stand-in |  | em_door.c legacy door sound patch | 09_fence_door |
| 0x001BBDA0 | — | BM | verified-unbound | em_door_original — test_door_original_reference | em_door.c legacy door | S2_opening* |
| 0x001BBE40 | — | BM | verified-unbound | em_door_transit — test_door_transit_reference | em_door.c legacy walk-to at 15 u/s (H13) | S2_opening |
| 0x001BC0E0 | — | BM | verified-unbound | em_door_original — test_door_original_reference | em_door.c legacy door | 09_fence_door |
| 0x001BC150 | — | BM | live | em_door.c door_commit_001BC150 -> em_door_transit_commit — test_door_transit_reference; test_room_move_reference |  | 09_fence_door |
| 0x001BC240 | — | BM | unverified | em_door.c (legacy door sub 4/5) |  | 09_fence_door |
| 0x001BC290 | — | BM | unverified | em_door.c (legacy door sub 4/5) |  | 09_fence_door |
| 0x001BC300 | — | BM | verified-unbound | em_door_original — test_door_original_reference | em_door.c legacy door | S2_opening |
| 0x001BC350 | — | BM | verified-unbound | em_door_original — test_door_original_reference | em_door.c legacy em_door_update (node tick_door) | S2_opening |

### 3.13 Area services, child spawns, area title (0x1BC400..0x1C5FFF)

21 functions, 1,659 instructions: live 5, verified-unbound 3, unverified 4, stand-in 3, missing 6.

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001C1D00 | — | BM | stand-in |  | em_render_frame.c em_render_001C1D00 (port render-env init) | S2_opening |
| 0x001C1DC0 | — | BM | unverified | em_scene_bindings.c w_001C1DC0 (weather spawn) | 001D2830 registrations and 001C1E70..001C1F50 passes reported | S1_newgame_load |
| 0x001C1E70 | — | BM | missing |  | reported under UM_001C1DC0 | S1_newgame_load |
| 0x001C1E80 | — | BM | missing |  | reported under UM_001C1DC0 | S1_newgame_load |
| 0x001C1E90 | — | BM | missing |  | reported under UM_001C1DC0 | S1_newgame_load |
| 0x001C1EA0 | — | BM | live | em_area11_bindings.c em_area11_spawn_weather_001C1EA0 — compare_frame_order.py; test_level_smoke.py (census 49) | spawn only | S1_newgame_load |
| 0x001C1F50 | — | NM | unverified | em_background_gs | module not linked | S1_newgame_load |
| 0x001C22A0 | — | BM | missing |  |  | S2_opening* |
| 0x001C2360 | — | BM | missing |  |  | S2_opening* |
| 0x001C40B0 | — | NM | verified-unbound | em_pickup_owner — test_pickup_owner_reference; test_continue_reset_reference | em_pickup.c inventory_add | S0_title |
| 0x001C4760 | — | BM | live | em_director_original_001C4760 through em_director_original_001C4760_scene (the canonical key bytes; the opening 00823E80's 001C4760(0, 1), and the legacy director stand-in's beat-0 001C4760(1, 1)) — test_continue_reset_reference.py (executes 001C4760, 90 cases, and the opening slice 0x823F6C..0x823F8C through its call), test_director_original_reference.py |  | S2_opening |
| 0x001C47A0 | — | BM | live | em_pickup.c pickup_take (B0 = 1, B1 = type) — test_level_smoke.py (battery phase; route 01 f220..f459) | called from the legacy take | 01_battery |
| 0x001C4820 | — | BM | missing |  | pool node render-only; drawn from the scene props | S2_opening |
| 0x001C5570 | — | BM | live | em_area11_bindings.c spawn_001C5570 — compare_frame_order.py; test_level_smoke.py (census 49) | spawn only | S2_opening* |
| 0x001C5680 | — | AI | unverified | em_pickup.c em_pickup_lights_tick / em_props indicators | no oracle in this census executes the child tick | S2_opening |
| 0x001C5760 | — | AI | unverified | em_pickup.c em_pickup_lights_tick / em_props indicators | no oracle in this census executes the child tick | S2_opening |
| 0x001C5860 | — | BM | stand-in |  | em_hud.c legacy area-title sub-location line | S2_opening |
| 0x001C5930 | — | NM | stand-in |  | em_hud.c legacy area-title card; node lifecycle translated in em_area11_bindings.c tick_area_title (from the .s) | S2_opening |
| 0x001C5C50 | — | BM | live | em_actor_roster.c em_actor_roster_spawn_001C5C50 — test_actor_census_reference |  | S1_newgame_load |
| 0x001C5C90 | — | BM | verified-unbound | em_roger_actor_original — test_roger_actor_original_reference.py |  | S2_opening |
| 0x001C5FB0 | — | NM | live | em_status_hub_ui.c (inline number format) — test_status_hub_ui_reference |  | 01_battery |

### 3.14 Animation runtime (0x1C6000..0x1CC16F)

53 functions, 4,258 instructions: live 15, verified-unbound 31, unverified 1, missing 6.

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001C6120 | — | BM | verified-unbound | em_pose_host_workers, em_owner_services_original — test_owner_services_reference.py, test_pose_host_workers_reference.py, test_shadow_original_reference.py |  | S0_title |
| 0x001C6150 | — | BM | verified-unbound | em_status_models, em_roger_actor_original — test_owner_services_reference.py, test_player_misc_workers_reference.py, test_roger_actor_original_reference.py |  | S1_newgame_load |
| 0x001C61D0 | — | BM | verified-unbound | em_pose_host_workers, em_weapon — test_player_fall_reference.py, test_player_reaction_reference.py, test_player_recovery_reference.py |  | 00_panel_no_battery |
| 0x001C62C0 | bone_init_default_1 | AW | live | em_status_models, em_owner_services_original — test_owner_services_reference.py |  | S2_opening |
| 0x001C6380 | — | BM | live | em_owner_services_original, em_status_models — test_owner_services_reference.py |  | S2_opening |
| 0x001C63E0 | bone_init_default_2 | BM | verified-unbound | em_pose_host_workers, em_status_models — test_pose_host_workers_reference.py |  | S2_opening |
| 0x001C64F0 | anim_advance_time | NM | live | em_player_pose.c em_player_pose_advance / em_interaction_animation.c — test_player_pose_reference |  | S2_opening |
| 0x001C67E0 | anim_clip_init | BM | live | em_player_pose.c (clip init) — test_player_pose_reference |  | 00_panel_no_battery |
| 0x001C68C0 | — | BM | verified-unbound | em_pose_host_workers, em_door — test_pose_host_workers_reference.py |  | S2_opening |
| 0x001C6960 | — | BM | verified-unbound | em_pose_host_workers — test_pose_host_workers_reference.py |  | S2_opening |
| 0x001C6DA0 | anim_eval_skeleton | AU | live | em_pose_bank.c / em_player_pose.c evaluation — test_player_pose_host_reference; test_player_foot_stop_reference | player and pose-bank actors; other actors use exported matrices | S2_opening |
| 0x001C7420 | — | NM | verified-unbound | em_owner_services_original — test_owner_services_reference | em_face_model.c / em_opening_actor.c basis collapse | S2_opening |
| 0x001C7900 | — | NM | missing |  |  | S2_opening |
| 0x001C7C00 | — | NM | verified-unbound | em_cinematic_camera — test_roger_cinematic_reference.py |  | S2_opening |
| 0x001C8480 | anim_clip_resolve | BM | verified-unbound | em_pose_host_workers — test_pose_host_workers_reference.py |  | S2_opening |
| 0x001C84D0 | — | BM | live | em_pose_bank.c / em_pose_transition.c — test_pose_bank_reference; test_pose_transition_reference |  | S2_opening |
| 0x001C85D0 | anim_decode_translation | BM | live | em_pose_bank.c / em_pose_transition.c — test_pose_bank_reference; test_pose_transition_reference |  | S2_opening |
| 0x001C86A0 | — | BM | live | em_pose_transition.c — test_pose_transition_reference |  | S2_opening |
| 0x001C8710 | — | AW | verified-unbound | em_pose_host_workers — test_pose_host_workers_reference.py |  | S2_opening |
| 0x001C87C0 | — | NM | live | em_pose_transition.c — test_pose_transition_reference |  | S2_opening |
| 0x001C8D50 | anim_sample_bones | BM | live | em_pose_transition.c — test_pose_transition_reference |  | 00_panel_no_battery |
| 0x001C8F10 | anim_sample_rotation | BM | verified-unbound | em_pose_host_workers — test_pose_host_workers_reference.py |  | S2_opening |
| 0x001C90D0 | — | BM | verified-unbound | em_pose_host_workers — test_pose_host_workers_reference.py |  | S2_opening |
| 0x001C92C0 | — | NM | verified-unbound | em_pose_host_workers — test_pose_host_workers_reference.py |  | S2_opening |
| 0x001C94B0 | build_trs_matrix | AI | verified-unbound | em_pose_host_workers — test_pose_host_workers_reference.py |  | S2_opening |
| 0x001C9610 | — | NM | live | em_owner_services_original, em_status_models — test_owner_services_reference.py |  | S2_opening |
| 0x001C9940 | — | NM | verified-unbound | em_pose_host_workers, em_door — test_pose_host_workers_reference.py |  | S2_opening |
| 0x001C9D50 | — | BM | missing |  |  | 00_panel_no_battery |
| 0x001C9E40 | — | AW | missing |  |  | 00_panel_no_battery |
| 0x001CA0A0 | quat_nlerp | NM | live | em_pose_transition.c — test_pose_transition_reference |  | S2_opening |
| 0x001CA1C0 | quat_to_mat3 | BM | live | em_pose_transition.c — test_pose_transition_reference |  | S2_opening |
| 0x001CA5E0 | — | BM | verified-unbound | em_roger_actor_original — test_roger_actor_original_reference.py |  | S1_newgame_load |
| 0x001CA5F0 | — | BM | verified-unbound | em_status_models, em_roger_actor_original — test_roger_actor_original_reference.py |  | S1_newgame_load |
| 0x001CA6E0 | — | BM | verified-unbound | em_roger_actor_original, em_status_models — test_player_misc_workers_reference.py, test_roger_actor_original_reference.py |  | S1_newgame_load |
| 0x001CA6F0 | — | BM | verified-unbound | em_roger_actor_original — test_roger_actor_original_reference.py |  | S2_opening* |
| 0x001CA700 | — | BM | verified-unbound | em_roger_actor_original — test_roger_actor_original_reference.py |  | S2_opening |
| 0x001CA770 | — | BM | verified-unbound | em_roger_actor_original — test_roger_actor_original_reference.py |  | S2_opening |
| 0x001CA7B0 | — | BM | verified-unbound | em_owner_services_original — test_owner_services_reference.py |  | S2_opening |
| 0x001CA940 | — | BM | verified-unbound | em_owner_services_original — test_owner_services_reference.py |  | S2_opening |
| 0x001CA990 | — | BM | verified-unbound | em_owner_services_original — test_owner_services_reference.py |  | S2_opening |
| 0x001CAA00 | — | BM | verified-unbound | em_owner_services_original, em_roger_actor_original — test_owner_services_reference.py, test_roger_actor_original_reference.py |  | S2_opening |
| 0x001CAAC0 | — | NM | missing |  |  | S2_opening |
| 0x001CACB0 | — | BM | missing |  |  | S2_opening |
| 0x001CB2C0 | — | NM | missing |  |  | S2_opening |
| 0x001CB3C0 | — | BM | verified-unbound | em_owner_services_original — test_owner_services_reference.py |  | S2_opening |
| 0x001CB590 | — | BM | live | em_scene_bindings, em_status_models — test_actor_pool_reference.py |  | S2_opening |
| 0x001CB5A0 | — | BM | live | em_scene_bindings.c w_001CB5A0 (empty leaf) — test_scene_frame_reference |  | S2_opening |
| 0x001CB5B0 | anim_bone_array_setup | BM | unverified | em_status_models.c w_001CB5B0 |  | S2_opening |
| 0x001CB5F0 | — | BM | verified-unbound | em_head_sprite_original — test_head_sprite_reference.py |  | S2_opening |
| 0x001CB6B0 | — | BM | verified-unbound | em_head_sprite_original — test_head_sprite_reference.py |  | S2_opening |
| 0x001CB760 | — | BM | verified-unbound | em_head_sprite_original — test_head_sprite_reference.py |  | S2_opening |
| 0x001CB900 | — | BM | verified-unbound | em_head_sprite_original — test_head_sprite_reference.py |  | S2_opening |
| 0x001CBE10 | — | BM | live | em_message_glyph_original (the message glyph advance), em_hud's atlas advances — test_message_glyph_reference.py, test_message_draw_reference.py |  | S2_opening |

### 3.15 Message glyphs, object registry and face (0x1CC170..0x1D19CF)

15 functions, 1,896 instructions: live 3, verified-unbound 10, unverified 2.

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001CC170 | — | AI | live | em_message_draw_original via em_message_live — test_message_draw_reference.py |  | S2_opening |
| 0x001CC3B0 | — | NM | live | em_message_glyph_original (message lines, drawn through the atlas by em_hud_glyph_strip); em_status_hub_ui.c render_text — test_message_glyph_reference, test_status_hub_ui_reference |  | S2_opening |
| 0x001CCF70 | — | NM | verified-unbound | em_effect_original, em_head_sprite_original — test_effect_original_reference.py, test_head_sprite_reference.py |  | S2_opening |
| 0x001CD370 | — | BM | verified-unbound | em_head_sprite_original — test_head_sprite_reference.py |  | S2_opening |
| 0x001CD390 | — | NM | verified-unbound | em_effect_original, em_shadow_actor_route — test_effect_original_reference.py, test_shadow_actor_route_reference.py |  | 02_elevator_refusal |
| 0x001CD520 | — | NM | unverified | em_status_models.c w_001CD520 | faults on the D_008104E4 == 1 glow | S2_opening |
| 0x001CE300 | — | NM | verified-unbound | em_shadow_actor_route — test_shadow_actor_route_reference.py |  | 02_elevator_refusal |
| 0x001CF470 | — | AW | unverified | em_shadow_actor_route | module not linked | 02_elevator_refusal |
| 0x001CFA60 | — | BM | verified-unbound | em_head_sprite_original — test_head_sprite_reference.py |  | S2_opening |
| 0x001CFBE0 | — | BM | verified-unbound | em_head_sprite_original — test_head_sprite_reference.py |  | S2_opening |
| 0x001D0690 | — | BM | verified-unbound | em_roger_actor_original, em_opening_face — test_roger_actor_original_reference.py |  | S2_opening |
| 0x001D06D0 | — | BM | verified-unbound | em_roger_actor_original — test_roger_actor_original_reference.py |  | S2_opening |
| 0x001D06E0 | — | BM | live | em_player_face_host.c — test_player_face_host.py |  | S2_opening |
| 0x001D0720 | — | NM | live | em_opening_face.c — test_opening_face_reference |  | S2_opening |
| 0x001D0C70 | — | BM | verified-unbound | em_roger_actor_original — test_roger_actor_original_reference.py |  | S2_opening |

### 3.16 Render heads, projection, lighting, shadow, veil particles (0x1D19D0..0x1DAFFF)

53 functions, 5,339 instructions: live 10, verified-unbound 19, unverified 9, stand-in 6, missing 9.

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001D19D0 | — | BM | missing |  | reported no-effect binding UM_001D19D0 (render init) | S0_title* |
| 0x001D19E0 | — | BM | missing |  | reported no-effect binding (um_001D19E0) | S1_newgame_load* |
| 0x001D1C50 | — | NM | stand-in |  | em_render_frame.c em_render_001D1C50 (port collector; fog/GS setup, point-light tick) | S0_title |
| 0x001D1EA0 | — | BM | stand-in |  | em_render_frame.c em_render_001D1EA0 (native world/page draw) | S0_title |
| 0x001D1EF0 | — | BM | missing |  | reported no-effect binding (um_001D1EF0) | S0_title |
| 0x001D1F20 | — | CL | verified-unbound | em_load_veil_particles — test_load_veil_particles_reference.py |  | S0_title |
| 0x001D1F80 | — | CL | verified-unbound | em_load_veil_particles, em_owner_services_original — test_load_veil_particles_reference.py, test_owner_services_reference.py |  | S0_title |
| 0x001D1FF0 | — | CL | verified-unbound | em_load_veil_particles, em_shadow_original — test_load_veil_particles_reference.py |  | S0_title |
| 0x001D2040 | — | CL | verified-unbound | em_load_veil_particles, em_status_models — test_load_veil_particles_reference.py |  | S0_title |
| 0x001D2300 | — | NM | verified-unbound | em_background_gs — test_background_reference.py |  | S0_title |
| 0x001D2590 | — | AI | unverified | em_area11_interaction_host.c UI projection (zoom 224 / tan 25 deg) |  | S2_opening |
| 0x001D25F0 | — | BM | stand-in |  | em_math.h / em_render_frame.c native projection | S2_opening |
| 0x001D2610 | — | BM | unverified | em_area11_interaction_host.c UI projection (zoom 224 / tan 25 deg) |  | S2_opening |
| 0x001D2830 | — | AW | missing |  | reported no-effect binding (um_001D2830; veil registrations) | S0_title |
| 0x001D2960 | — | BM | stand-in |  | em_math.h / em_render_frame.c native projection | S0_title |
| 0x001D2D20 | — | BM | stand-in |  | em_math.h / em_render_frame.c native projection | S0_title |
| 0x001D30A0 | — | NM | stand-in |  | em_render_frame.c render chain / frame_close_out | S0_title |
| 0x001D4B50 | — | BM | unverified | em_gfx |  | S2_opening |
| 0x001D4CD0 | — | BM | verified-unbound | em_gfx, em_shadow_original — test_shadow_original_reference.py |  | S2_opening |
| 0x001D4FB0 | — | BM | verified-unbound | em_shadow_original, em_gfx — test_shadow_original_reference.py |  | S2_opening |
| 0x001D52E0 | — | BM | missing |  |  | S1_newgame_load |
| 0x001D5370 | — | NM | missing |  |  | S2_opening |
| 0x001D5C80 | — | NM | unverified | em_shadow_original | module not linked | S2_opening |
| 0x001D63B0 | — | NM | verified-unbound | em_load_veil_particles — test_load_veil_particles_reference.py |  | S1_newgame_load* |
| 0x001D6930 | — | NM | verified-unbound | em_load_veil_particles — test_load_veil_particles_reference.py |  | S1_newgame_load |
| 0x001D6B60 | — | BM | verified-unbound | em_load_veil_particles — test_load_veil_particles_reference.py |  | S1_newgame_load* |
| 0x001D6BA0 | — | NM | verified-unbound | em_load_veil_particles — test_load_veil_particles_reference.py |  | S1_newgame_load |
| 0x001D6E60 | — | NM | verified-unbound | em_load_veil_particles — test_load_veil_particles_reference.py |  | S1_newgame_load |
| 0x001D7080 | — | AW | verified-unbound | em_load_veil_particles — test_load_veil_particles_reference.py |  | S1_newgame_load |
| 0x001D7B30 | — | BM | live | em_lighting.c room-rig lookup — test_actor_lighting_reference.py |  | S1_newgame_load |
| 0x001D7BB0 | — | BM | live | em_point_light.c — test_point_light_reference |  | S1_newgame_load* |
| 0x001D7C30 | — | NM | live | em_point_light.c — test_point_light_reference |  | S0_title |
| 0x001D7FA0 | — | BM | live | em_point_light.c — test_point_light_reference |  | S1_newgame_load* |
| 0x001D8060 | — | BM | missing |  |  | S1_newgame_load* |
| 0x001D80B0 | — | BM | missing |  |  | S1_newgame_load* |
| 0x001D8130 | — | BM | live | em_lighting.c — test_actor_lighting_reference.py | several actor models still carry the stand-in light (H18/WP-13) | S2_opening |
| 0x001D8270 | — | AW | live | em_lighting.c — test_actor_lighting_reference.py | several actor models still carry the stand-in light (H18/WP-13) | S2_opening |
| 0x001D8340 | — | BM | live | em_lighting.c — test_actor_lighting_reference.py | several actor models still carry the stand-in light (H18/WP-13) | S2_opening |
| 0x001D8690 | — | NM | live | em_lighting.c — test_actor_lighting_reference.py | several actor models still carry the stand-in light (H18/WP-13) | S2_opening |
| 0x001D88B0 | — | BM | unverified | em_lighting.c face lighting mode |  | S2_opening |
| 0x001D89D0 | — | NM | live | em_lighting.c — test_actor_lighting_reference.py | several actor models still carry the stand-in light (H18/WP-13) | S2_opening |
| 0x001D8BF0 | — | BM | verified-unbound | em_roger_actor_original — test_roger_actor_original_reference.py |  | S1_newgame_load |
| 0x001D8C20 | — | BM | live | em_lighting.c — test_actor_lighting_reference.py | several actor models still carry the stand-in light (H18/WP-13) | S2_opening |
| 0x001D8C30 | — | NM | unverified | em_effect_color.h / em_status_models.c draw |  | S2_opening |
| 0x001D8FD0 | — | BM | verified-unbound | em_fog_gs — test_area11_fog_reference | exported fog record in the scene manifest (values verified) | S1_newgame_load |
| 0x001D9070 | — | NM | missing |  | render init; no port counterpart (UM_001D19D0) | S0_title* |
| 0x001D98A0 | — | NM | verified-unbound | em_shadow_original — test_shadow_original_reference.py |  | S2_opening |
| 0x001D9EE0 | — | BM | verified-unbound | em_shadow_original, em_shadow_gs — test_shadow_original_reference.py |  | S2_opening |
| 0x001DA080 | — | BM | unverified | em_shadow_original | module not linked | S2_opening |
| 0x001DA1E0 | — | BM | unverified | em_shadow_original | module not linked | S2_opening |
| 0x001DA290 | — | CL | verified-unbound | em_shadow_original, em_shadow_gs — test_shadow_original_reference.py |  | S2_opening |
| 0x001DA310 | — | NM | unverified | em_shadow_original | module not linked | S2_opening |
| 0x001DA6A0 | — | NM | verified-unbound | em_shadow_original, em_roger_actor_original — test_roger_actor_original_reference.py, test_shadow_actor_route_reference.py, test_shadow_original_reference.py |  | S2_opening |

### 3.17 Render context, background, weather and snow (0x1DB000..0x1EEFFF)

30 functions, 3,752 instructions: live 3, verified-unbound 6, unverified 4, missing 17.

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001DD7B0 | — | NM | missing |  |  | S1_newgame_load* |
| 0x001DD940 | — | BM | missing |  |  | S1_newgame_load* |
| 0x001DD950 | — | BM | unverified | em_interaction_projection.c |  | S1_newgame_load |
| 0x001DD980 | — | BM | live | em_interaction_projection.c — test_interaction_projection_reference |  | S1_newgame_load |
| 0x001DDA00 | — | BM | missing |  |  | S2_opening |
| 0x001DDAA0 | — | BM | missing |  |  | S2_opening |
| 0x001DDE10 | — | BM | missing |  |  | S2_opening |
| 0x001DEEE0 | — | BM | missing |  |  | S2_opening |
| 0x001DFA40 | — | NM | verified-unbound | em_load_veil_particles — test_load_veil_particles_reference.py |  | S1_newgame_load* |
| 0x001E0C30 | — | BM | missing |  |  | S1_newgame_load* |
| 0x001E0C60 | — | BM | missing |  |  | S0_title |
| 0x001E0C80 | — | CL | missing |  |  | S1_newgame_load |
| 0x001E0CC0 | — | BM | missing |  | reported no-effect binding (um_001E0CC0) | S1_newgame_load |
| 0x001E0CF0 | — | BM | unverified | em_background_gs | module not linked | S2_opening |
| 0x001E0D70 | — | BM | missing |  |  | S2_opening |
| 0x001E0DF0 | — | BM | missing |  |  | S1_newgame_load |
| 0x001E1010 | — | BM | missing |  |  | S1_newgame_load* |
| 0x001E1E60 | — | NM | verified-unbound | em_background_gs, em_gfx_metal — test_background_reference.py |  | S2_opening |
| 0x001E2260 | — | BM | unverified | em_background_gs | module not linked | S1_newgame_load |
| 0x001E2270 | — | BM | unverified | em_background_gs | module not linked | S1_newgame_load |
| 0x001E2290 | — | BM | verified-unbound | em_head_sprite_original — test_head_sprite_reference | pool node 'head-bone sprite effect: UNBOUND' | S2_opening* |
| 0x001E23A0 | — | BM | verified-unbound | em_head_sprite_original — test_head_sprite_reference | pool node 'head-bone sprite effect: UNBOUND' | S2_opening* |
| 0x001E2560 | — | BM | verified-unbound | em_head_sprite_original — test_head_sprite_reference | pool node 'head-bone sprite effect: UNBOUND' | S2_opening |
| 0x001E55F0 | — | NM | live | em_weather.c via em_snow_runtime (node 001E55F0) — test_weather_reference |  | S2_opening |
| 0x001E67C0 | — | NM | live | em_snow.c — test_snow_tiles_reference |  | S2_opening |
| 0x001EA240 | — | BM | verified-unbound | em_effect_original — test_effect_original_reference.py |  | 00_panel_no_battery |
| 0x001EBF10 | — | NM | missing |  |  | 08_truck_crossing |
| 0x001EC1F0 | — | NM | missing |  |  | 05_boxes |
| 0x001EC3F0 | — | NM | missing |  |  | 00_panel_no_battery |
| 0x001EC470 | — | NM | missing |  |  | 06_hill_slide |

### 3.18 Effects (0x1EF000..0x1F8FFF)

31 functions, 3,127 instructions: verified-unbound 6, unverified 1, missing 24.

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001EF940 | — | BM | verified-unbound | em_effect_original — test_effect_original_reference.py |  | S1_newgame_load |
| 0x001EF9D0 | — | NM | verified-unbound | em_area11_bindings, em_player_misc_workers — test_effect_original_reference.py, test_head_sprite_reference.py, test_player_misc_workers_reference.py |  | S1_newgame_load |
| 0x001EFD20 | — | BM | verified-unbound | em_area11_bindings, em_effect_original — test_effect_original_reference.py, test_truck_original_reference.py |  | S1_newgame_load |
| 0x001EFD90 | — | BM | verified-unbound | em_effect_original, em_player_slide — test_effect_original_reference.py, test_player_climb_reference.py, test_player_fall_reference.py |  | 00_panel_no_battery |
| 0x001F0120 | — | BM | verified-unbound | em_area11_bindings, em_head_sprite_original — test_head_sprite_reference.py |  | S2_opening* |
| 0x001F0310 | — | BM | missing |  | effect manager / effect kinds | S0_title* |
| 0x001F0360 | — | BM | missing |  | reported no-effect binding UM_001F0360 (effect-manager barrel) | S2_opening |
| 0x001F03D0 | — | BM | missing |  | effect manager / effect kinds | S0_title* |
| 0x001F0720 | — | NM | missing |  | effect manager / effect kinds | S2_opening |
| 0x001F0A60 | — | AU | missing |  | effect manager / effect kinds | S3_first_control_idle |
| 0x001F1110 | — | BM | missing |  | effect manager / effect kinds | S2_opening* |
| 0x001F1180 | — | NM | missing |  | effect manager / effect kinds | S2_opening |
| 0x001F3FA0 | — | NM | missing |  | effect manager / effect kinds | S0_title* |
| 0x001F40C0 | — | BM | missing |  | effect manager / effect kinds | S2_opening |
| 0x001F4D40 | — | AI | missing |  | effect manager / effect kinds | S2_opening |
| 0x001F54E0 | — | AW | unverified | em_effect_color.h |  | S2_opening |
| 0x001F5640 | — | BM | missing |  | effect manager / effect kinds | S2_opening |
| 0x001F5940 | — | BM | missing |  | effect manager / effect kinds | S2_opening |
| 0x001F5C20 | — | BM | missing |  | effect manager / effect kinds | S2_opening |
| 0x001F5CA0 | — | BM | missing |  | effect manager / effect kinds | S2_opening |
| 0x001F6210 | — | BM | missing |  | effect manager / effect kinds | S2_opening |
| 0x001F6640 | — | BM | missing |  | effect manager / effect kinds | S1_newgame_load* |
| 0x001F66F0 | — | BM | missing |  | effect manager / effect kinds | S1_newgame_load* |
| 0x001F6760 | — | BM | missing |  | effect manager / effect kinds | S1_newgame_load* |
| 0x001F6850 | — | BM | missing |  | effect manager / effect kinds | S1_newgame_load* |
| 0x001F68B0 | — | BM | missing |  | effect manager / effect kinds | S1_newgame_load* |
| 0x001F6BB0 | — | NM | missing |  | effect manager / effect kinds | S2_opening |
| 0x001F6D60 | — | BM | missing |  | effect manager / effect kinds | S1_newgame_load* |
| 0x001F6E40 | — | BM | missing |  | effect manager / effect kinds | S1_newgame_load* |
| 0x001F6EB0 | — | BM | missing |  | effect manager / effect kinds | S2_opening |
| 0x001F8D30 | — | NM | verified-unbound | em_shadow_actor_route — test_shadow_actor_route_reference.py |  | 02_elevator_refusal |

### 3.19 Streams, sound and message service (0x1F9000..0x1FDFFF)

39 functions, 3,096 instructions: live 6, verified-unbound 26, stand-in 2, missing 5.

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
| 0x001FABB0 | — | BM | verified-unbound | em_stream_lanes_original — test_stream_lanes_reference | w_001FABB0 -> em_bgm_stop / em_opening_media_stop (native streams) | S0_title |
| 0x001FABF0 | — | NM | verified-unbound | em_stream_lanes_original — test_stream_lanes_reference.py |  | S1_newgame_load |
| 0x001FAE70 | — | BM | verified-unbound | em_stream_lanes_original — test_stream_lanes_reference | em_scene_bindings.c w_001FAE70 (state 5 translated, unverified; state 0 reported) | S1_newgame_load |
| 0x001FB100 | — | BM | stand-in |  | em_sfx.c per-frame bookkeeping | S0_title |
| 0x001FB370 | — | BM | missing |  | required worker of the unbound loader | S1_newgame_load* |
| 0x001FB3E0 | — | NM | missing |  |  | S1_newgame_load* |
| 0x001FB910 | — | BM | missing |  |  | S1_newgame_load* |
| 0x001FB9F0 | — | NM | live | em_sfx.c sfx_start / em_sfx_bank.c — test_area11_sfx_reference; test_area11_sfx_runtime.py | AREA11 cues re-exported; legacy ids still sharp (H19) | S0_title |
| 0x001FBC50 | — | BM | verified-unbound | em_stream_lanes_original — test_stream_lanes_reference | w_001FBC50 -> em_sfx_stop_all (live translation, no oracle); stream gain 0x1999 reported (H22) | S0_title |
| 0x001FBD50 | — | BM | live | em_sfx.c / em_sfx_bank.c play path — test_area11_sfx_reference | as 001FB9F0 | S2_opening |
| 0x001FBDB0 | — | AW | verified-unbound | em_sfx_bank — test_area11_sfx_reference.py |  | 09_fence_door |
| 0x001FBF50 | — | BM | verified-unbound | em_player_misc_workers — test_player_misc_workers_reference | em_sfx.c em_sfx_play_at (no gain oracle, AM-19) | S2_opening |
| 0x001FC280 | — | NM | verified-unbound | em_stream_lanes_original — test_stream_lanes_reference | em_scene_bindings.c w_001FAE70 (state 5 translated, unverified; state 0 reported) | S1_newgame_load |
| 0x001FC3C0 | — | BM | verified-unbound | em_sfx, em_sfx_bank — test_area11_sfx_reference.py, test_area11_sfx_runtime.py |  | S2_opening |
| 0x001FC6E0 | — | BM | missing |  |  | S0_title |
| 0x001FC770 | — | BM | live | em_message_draw_original via em_message_live — test_message_draw_reference.py |  | S2_opening |
| 0x001FC7B0 | — | NM | live | em_message_glyph_original via em_message_live — test_message_glyph_reference.py |  | S2_opening |
| 0x001FC9B0 | — | CL | live | em_message_service em_message_reset via em_message_live (w_001FC9B0 and the service's own teardown) — test_message_service_reference |  | S1_newgame_load |
| 0x001FCA10 | — | BM | live | em_message_service via em_message_live (step F, installed at bring-up) — test_message_service_reference, test_panel_message_reference, test_roger_media_reference | em_director.c for the director lines (WP-10); modes 3/4 fault (their presenters are untranslated; the status page presents its own mode 4) | S0_title |
| 0x001FCB90 | — | CL | stand-in |  | em_hud.c legacy message lookup | 01_battery |
| 0x001FCF10 | — | BM | missing |  |  | 03_panel_power |
| 0x001FD470 | — | BM | live | em_scene_bindings_001FD470 (bit 0 w_001FBC50, bit 1 w_001FABB0) as the message service's stream_stop; em_stream_lanes_original — test_stream_lanes_reference.py |  | S2_opening |
| 0x001FD4C0 | — | BM | live | em_message_service em_message_stream_request via em_message_live (the opening's 001B82D0 op12) — test_message_service_reference.py, test_area_script_reference.py, test-opening-runtime |  | S2_opening |
| 0x001FD580 | — | BM | live | em_message_service via em_message_live (step F) — test_message_service_reference.py | a voiced record faults at 001FA5A0 (the voice lanes are not live) | S2_opening |
| 0x001FD6A0 | — | BM | live | em_message_service via em_message_live (step F) — test_message_service_reference.py | as 001FD580 | S2_opening |
| 0x001FD790 | — | NM | live | em_message_service via em_message_live (step F) — test_message_service_reference, test_roger_media_reference, test_panel_message_reference |  | S2_opening |
| 0x001FD950 | — | NM | live | em_message_service via em_message_live (step F); its draw half em_message_draw_original — test_message_service_reference, test_message_draw_reference, test_message_capture |  | S2_opening |
| 0x001FDB80 | — | NM | live | em_message_service via em_message_live (step F) — test_message_service_reference, test_roger_media_reference, test_panel_message_reference |  | S2_opening |

### 3.20 Message draw (0x1FE000..0x1FEFFF)

7 functions, 400 instructions: verified-unbound 7.

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001FE070 | — | NM | live | em_message_draw_original via em_message_live — test_message_draw_reference.py |  | S2_opening |
| 0x001FE460 | — | BM | live | em_message_draw_original via em_message_live — test_message_draw_reference.py |  | S2_opening |
| 0x001FE480 | — | AI | live | em_message_draw_original via em_message_live — test_message_draw_reference.py |  | S2_opening |
| 0x001FE4B0 | — | BM | verified-unbound | em_message_draw_original — test_message_draw_reference.py |  | S2_opening |
| 0x001FE4D0 | — | BM | verified-unbound | em_message_draw_original — test_message_draw_reference.py |  | S2_opening |
| 0x001FE530 | — | AW | live | em_message_draw_original via em_message_live — test_message_draw_reference.py |  | S2_opening |
| 0x001FEF70 | — | BM | verified-unbound | em_status_scene_original — test_status_scene_reference.py |  | S1_newgame_load |

### 3.21 Status UI: hub, ITEM/BATTERY pages, pickups (0x207000..0x21AFFF)

19 functions, 3,798 instructions: live 14, verified-unbound 1, stand-in 3, missing 1.

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x00209280 | — | NM | live | em_status_draw.c — test_status_draw_reference |  | 01_battery |
| 0x0020A7A0 | — | NM | live | em_status_background.c — test_status_background_reference |  | 01_battery |
| 0x0020AE40 | — | BM | live | em_battery_ui.c — test_battery_reference; test_battery_pickup_reference |  | 01_battery |
| 0x0020B0D0 | — | BM | live | em_battery_ui.c — test_battery_reference; test_battery_pickup_reference |  | 01_battery |
| 0x0020B210 | — | NM | live | em_battery_ui.c — test_battery_reference; test_battery_pickup_reference |  | 01_battery |
| 0x0020BEF0 | — | BM | missing |  |  | 01_battery |
| 0x0020CCB0 | — | BM | live | em_battery_ui.c — test_battery_reference; test_battery_pickup_reference |  | 03_panel_power |
| 0x0020CD40 | — | BM | stand-in |  | em_hud.c / em_battery_ui.c UI cue requests (no samples exported; silent, WP-14) | 03_panel_power |
| 0x0020CD60 | — | BM | stand-in |  | em_hud.c / em_battery_ui.c UI cue requests (no samples exported; silent, WP-14) | 01_battery |
| 0x0020CDA0 | — | BM | stand-in |  | em_hud.c / em_battery_ui.c UI cue requests (no samples exported; silent, WP-14) | 03_panel_power |
| 0x0020CDC0 | — | NM | live | em_status_runtime.c / em_status_page.c page core (w_0020CDC0) — test_status_page_reference; test_status_hub_reference; test_level_smoke.py | MAP/SPR4/DATABASE pages fault (untranslated); module-0x21 wait instant (H7) | 01_battery |
| 0x0020DFA0 | — | BM | live | em_status_page.c / host status_page_event — test_status_page_reference |  | 01_battery |
| 0x0020E020 | — | BM | live | em_item_root.c — test_item_root_reference |  | 01_battery |
| 0x0020E060 | — | BM | live | em_status_runtime.c page reset (w_0020E060) — test_status_hub_ui_reference; test_status_frame_reference |  | 01_battery |
| 0x0020E080 | — | BM | live | em_status_page.c / host status_page_event — test_status_page_reference |  | 01_battery |
| 0x0020E0C0 | — | BM | live | em_status_runtime.c exit — test_level_smoke.py (two-tick close) |  | 01_battery |
| 0x0020EE50 | — | BM | live | em_item_root.c — test_item_root_reference |  | 01_battery |
| 0x002149F0 | — | NM | live | em_area11_interaction_host.c battery_finished + em_battery_ui.c — test_battery_pickup_reference; test_level_smoke.py (239-frame notice) |  | 01_battery |
| 0x00219550 | — | NM | verified-unbound | em_pickup_owner — test_pickup_owner_reference | em_pickup.c legacy scan + 2-frame take (pool group pickups); WP-6 | S2_opening |

### 3.22 Load veil, fog, stage workers, cinematic timeline (0x21B000..0x22FFFF)

25 functions, 3,097 instructions: live 4, verified-unbound 15, missing 6.

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x0021B180 | — | BM | live | em_scene_bindings, em_load_veil — test_area_load_reference.py |  | S1_newgame_load* |
| 0x0021B1B0 | — | AW | verified-unbound | em_load_veil_particles — test_load_veil_particles_reference | live calls reported, not drawn (the load shows black) | S1_newgame_load* |
| 0x0021B500 | — | BM | verified-unbound | em_load_veil_particles — test_load_veil_particles_reference | live calls reported, not drawn (the load shows black) | S1_newgame_load* |
| 0x0021B550 | — | BM | live | em_scene_bindings, em_load_veil — test_area_load_reference.py, test_room_move_reference.py, test_scene_task_reference.py |  | S1_newgame_load* |
| 0x0021B840 | — | BM | live | em_scene_bindings, em_load_veil — test_area_load_reference.py |  | S1_newgame_load* |
| 0x0021B8E0 | — | BM | missing |  |  | S1_newgame_load |
| 0x0021B900 | — | BM | missing |  |  | S0_title |
| 0x0021B920 | — | BM | verified-unbound | em_fog_gs — test_area11_fog_reference.py |  | S0_title |
| 0x0021B970 | — | BM | verified-unbound | em_fog_gs, em_game — test_area11_fog_reference.py |  | S1_newgame_load |
| 0x0021B9A0 | — | NM | verified-unbound | em_game, em_effect_original — test_effect_original_reference.py |  | S0_title |
| 0x0021BA70 | — | BM | missing |  |  | S1_newgame_load |
| 0x0021BA80 | — | BM | verified-unbound | em_fog_gs, em_game — test_area11_fog_reference.py |  | S1_newgame_load |
| 0x0021BAB0 | — | BM | missing |  |  | S2_opening |
| 0x0021BAC0 | — | BM | missing |  |  | 01_battery |
| 0x0021BAE0 | — | BM | live | em_status_page.c / host status_page_event — test_status_page_reference |  | 01_battery |
| 0x0021BB00 | — | AW | live | em_player_stage_workers em_player_0021BB00 (0021C440 / 0015D100 / 00182B30, L01), em_script_host_workers — test_player_stage_workers_reference.py, test_script_host_workers_reference.py |  | S2_opening |
| 0x0021C3F0 | — | CL | live | em_player_stage_workers em_player_0021C3F0 (0021C440's hit_b gate, L01) — test_player_stage_workers_reference.py | D_00810770 is not canonical (L19): the stage's view load refuses area 8 room 2, where it is read | S2_opening |
| 0x0021C440 | — | BM | live | em_player_stage_workers em_player_stage_reaction (0015B130 / 0015B770, L01; em_player_damage.c's processor copy retired) — test_player_stage_workers_reference; test_level_smoke.py (no-hit path every stage) | its hit, pending-damage and infection paths reach fail-stop workers (rumble, effects, atan2 / the +20 object, 0017B490 / 001749A0) and unbound +4 = 2 states; no route capture has a hit | S2_opening |
| 0x0021D640 | — | BM | live | em_player_stage_workers em_player_0021D640 (0021C440, L01), em_player_reaction — test_player_stage_workers_reference.py |  | S2_opening |
| 0x00224290 | — | AW | verified-unbound | em_player_fall, em_player_slide — test_player_fall_reference.py, test_player_slide_reference.py |  | 10_cage_roof_roger |
| 0x002243F0 | — | AW | verified-unbound | em_player_recovery, em_player_running_jump — test_player_recovery_reference.py, test_player_running_jump_reference.py |  | 12_crevice_jump |
| 0x00224B80 | — | BM | verified-unbound | em_player_recovery, em_player_slide — test_player_recovery_reference.py, test_player_slide_reference.py |  | 06_hill_slide |
| 0x0022EBE0 | — | CL | missing |  |  | S2_opening |
| 0x0022EC30 | — | BM | verified-unbound | em_cinematic_playback — test_cinematic_playback_reference | H4: not in COMMON | S2_opening |
| 0x0022EEF0 | — | NM | verified-unbound | em_cinematic_playback — test_cinematic_playback_reference | H4: not in COMMON | S2_opening |

### 3.23 AREA11 overlay owners (0x8235F0..0x828050, runtime addresses)

23 functions, 4,400 instructions: live 5, verified-unbound 14, stand-in 2, missing 2.

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x008235F0 | — | AU | live | em_area11_effect_runtime.c (node 008235F0) — test_area11_effect_reference | visuals; contact damage 00823580 not modelled (INV-17) | S2_opening |
| 0x008237C0 | — | CO | missing |  | overlay init; the roster spawn replaces it | S1_newgame_load* |
| 0x008237E0 | — | AU | verified-unbound | em_roger_actor_original — test_roger_actor_original_reference | pool node tick_roger: static draw, only its 001F0120(0x47) child | S2_opening |
| 0x00823910 | — | AU | verified-unbound | em_roger / em_roger_runtime — test_roger_reference; test_roger_encounter_reference | none (Roger unbound, WP-9) | S2_opening |
| 0x00823950 | — | AU | verified-unbound | em_roger / em_roger_runtime — test_roger_reference; test_roger_encounter_reference | none (Roger unbound, WP-9) | 10_cage_roof_roger |
| 0x00823B70 | — | AU | verified-unbound | em_roger / em_roger_runtime — test_roger_reference; test_roger_encounter_reference | none (Roger unbound, WP-9) | 14_roger_encounter |
| 0x00823CE0 | — | AU | missing |  | dormant in the first visit (waits on D_00810788); node bound with no port code | S2_opening |
| 0x00823E80 | — | AU | live | em_area11_opening.c / em_opening_runtime.c — test_continue_reset_reference (0x823F74..80 slice); opening capture frame | partial oracle coverage | S2_opening |
| 0x00823FF0 | — | AU | verified-unbound | em_truck_original — test_truck_original_reference | em_truck.c legacy (static truck; invented trigger and fall removed) | S2_opening |
| 0x008251E0 | — | AU | verified-unbound | em_truck_original — test_truck_original_reference | em_truck.c legacy (static truck; invented trigger and fall removed) | S2_opening |
| 0x008253F0 | — | AU | verified-unbound | em_director_original — test_director_original_reference | em_director.c kCineBeats keyframe player (H10); its beat step is the canonical D_00810813 and its beat-0 completion runs 001C4760(1, 1) (HK) | S2_opening |
| 0x00825500 | — | AU | verified-unbound | em_director_original — test_director_original_reference | em_area11_flow.c trigger boxes / em_director.c | S2_opening |
| 0x00825540 | — | AU | verified-unbound | em_director_original — test_director_original_reference | em_area11_flow.c trigger boxes / em_director.c | S2_opening |
| 0x00825600 | — | AU | verified-unbound | em_director_original — test_director_original_reference | em_area11_flow.c trigger boxes / em_director.c | 10_cage_roof_roger |
| 0x00825640 | — | AU | verified-unbound | em_director_original — test_director_original_reference | em_area11_flow.c trigger boxes / em_director.c | 10_cage_roof_roger |
| 0x008256D0 | — | AU | verified-unbound | em_director_original — test_director_original_reference | em_area11_flow.c trigger boxes / em_director.c | 11_crevice_prompt |
| 0x00825710 | — | AU | verified-unbound | em_director_original — test_director_original_reference | em_area11_flow.c trigger boxes / em_director.c | 11_crevice_prompt |
| 0x008257A0 | — | AU | live | em_manager_008257A0.c — test_manager_8257a0_reference |  | S2_opening* |
| 0x00825940 | — | AU | stand-in |  | em_enemy.c legacy em_enemy_update (pool group 'enemies') (husk pair; interim spawn) | S2_opening |
| 0x00827490 | — | AU | stand-in |  | em_enemy.c legacy em_enemy_update (pool group 'enemies') (husk pair; interim spawn) | S2_opening |
| 0x00827630 | — | AU | verified-unbound | em_fan_original — test_fan_original_reference | static fans (em_pickup draw, no spin) | S2_opening |
| 0x00827B10 | — | AU | live | em_elevator.c / em_area11_interaction_host.c (node #27) — test_elevator_reference; test_level_smoke.py (routes 02/04) |  | S2_opening |
| 0x00828050 | — | AU | live | em_elevator.c em_elevator_motion_tick — test_elevator_reference; test_level_smoke.py (route 04) |  | 04_elevator_ride |

### 3.24 SDK VU0 math (0x1026A0..0x103237) and the VU1/DMA library functions that carry a translation

27 functions, 572 instructions: live 19, verified-unbound 5, missing 3.

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x001000E0 | — | BM | missing |  |  | 10_cage_roof_roger |
| 0x00100268 | — | BM | verified-unbound | em_load_veil_particles — test_load_veil_particles_reference |  | S0_title |
| 0x00100610 | — | BM | verified-unbound | em_load_veil_particles — test_load_veil_particles_reference |  | S1_newgame_load |
| 0x001006D8 | — | AI | verified-unbound | em_load_veil_particles — test_load_veil_particles_reference |  | S1_newgame_load |
| 0x001026A0 | — | AI | live | em_camera_rotation.c / em_owner_services_original.c (VU0 forms) — test_camera_rotation_reference; test_camera_retarget_reference |  | S1_newgame_load |
| 0x001026D0 | — | AI | live | em_status_models.c w_001026D0 — test_status_scene_reference |  | S0_title |
| 0x00102718 | — | AI | verified-unbound | em_effect_original / em_coll_* (inline) — test_effect_original_reference |  | S1_newgame_load |
| 0x00102738 | — | AI | verified-unbound | em_effect_original / em_coll_* (inline) — test_effect_original_reference |  | S2_opening |
| 0x00102760 | — | AW | live | em_interaction_scan.c normalize / em_camera_probe.c — test_camera_probe_reference; test_camera_commit_reference |  | S1_newgame_load |
| 0x00102798 | — | AI | live | em_camera.c camera_commit_original (inline) — test_camera_commit_reference |  | S1_newgame_load |
| 0x001027E0 | — | AW | missing |  |  | S1_newgame_load |
| 0x00102850 | — | AI | missing |  |  | 01_battery |
| 0x001028B8 | — | AI | live | em_camera_probe.c (inline) — test_camera_probe_reference |  | S2_opening |
| 0x001028D0 | — | AI | live | em_camera_retarget.c / em_camera.c (inline) — test_camera_retarget_reference; test_camera_commit_reference |  | S1_newgame_load |
| 0x00102900 | — | AI | live | em_snow.c (inline) — test_snow_tiles_reference |  | S1_newgame_load |
| 0x00102918 | — | AI | live | em_owner_services_original.c (live through em_status_models) — test_owner_services_reference |  | S1_newgame_load |
| 0x00102948 | — | AI | live | em_camera_retarget.c / em_camera.c (inline) — test_camera_retarget_reference; test_camera_commit_reference |  | S0_title |
| 0x00102958 | copy_qw4 | AI | live | em_elevator.c / em_status_scene_original.c copy — test_elevator_reference; test_status_scene_reference |  | S0_title |
| 0x001029C0 | — | AI | live | em_owner_services_original.c (live through em_status_models) — test_owner_services_reference |  | S0_title |
| 0x001029E8 | — | NM | live | em_owner_services_original.c (live through em_status_models) — test_owner_services_reference |  | S1_newgame_load |
| 0x00102A60 | — | AI | live | em_owner_services_original.c (live through em_status_models) — test_owner_services_reference |  | S1_newgame_load |
| 0x00102B08 | — | AI | live | em_owner_services_original.c (live through em_status_models) — test_owner_services_reference |  | S1_newgame_load |
| 0x00102BB0 | — | AI | live | em_owner_services_original.c (live through em_status_models) — test_owner_services_reference |  | S1_newgame_load |
| 0x00102C58 | — | BM | live | em_owner_services_original.c (live through em_status_models) — test_owner_services_reference |  | S1_newgame_load |
| 0x00102CD0 | — | BM | live | em_camera.c camera_commit_original (inline) — test_camera_commit_reference |  | S1_newgame_load |
| 0x001031E0 | — | BM | live | em_status_scene_original.c / em_camera.c (inline) — test_camera_commit_reference; test_status_scene_reference |  | S2_opening |
| 0x00103230 | — | AI | live | em_camera_probe.c (inline) — test_camera_probe_reference |  | S2_opening |

### 3.25 SDK libm, soft float and rand (0x11C4C8..0x12FFFF)

29 functions, 1,770 instructions: live 13, verified-unbound 16.

| Address | Name | Decomp | Port | Module / test | Stand-in / note | First |
|---|---|---|---|---|---|---|
| 0x0011C4C8 | — | BM | verified-unbound | em_sdk_math_original — test_sdk_math_original_reference |  | S1_newgame_load |
| 0x0011C7B0 | — | NM | live | em_item_sdk_math.c / em_sdk_math_original.c — test_item_sdk_math_reference; test_sdk_math_original_reference |  | S2_opening |
| 0x0011CB90 | — | AW | live | em_item_sdk_math.c / em_sdk_math_original.c — test_item_sdk_math_reference; test_sdk_math_original_reference |  | S0_title |
| 0x0011CCC8 | — | AW | live | em_item_sdk_math.c / em_sdk_math_original.c — test_item_sdk_math_reference; test_sdk_math_original_reference |  | S2_opening |
| 0x0011D770 | — | AW | live | em_item_sdk_math.c / em_sdk_math_original.c — test_item_sdk_math_reference; test_sdk_math_original_reference |  | S2_opening |
| 0x0011D878 | — | NM | verified-unbound | em_sdk_math_original — test_sdk_math_original_reference |  | S2_opening |
| 0x0011DB90 | — | BM | verified-unbound | em_sdk_soft_float — test_sdk_soft_float_reference |  | 03_panel_power |
| 0x0011DBB8 | — | NM | verified-unbound | em_director_original / em_sdk_math_original — test_sdk_math_original_reference | em_player.c probe_atan (host atanf) | S1_newgame_load |
| 0x0011DE90 | — | AW | verified-unbound | em_sdk_math_original — test_sdk_math_original_reference | em_player_heading.c (host trig model) | S3_first_control_idle |
| 0x0011DF78 | — | AI | live | em_sdk_math_original.c sdk_0011DF78 — test_sdk_math_original_reference; test_camera_commit_reference |  | S1_newgame_load |
| 0x0011E080 | — | AI | verified-unbound | em_sdk_math_original — test_sdk_math_original_reference |  | S0_title |
| 0x0011E2A8 | — | AW | live | em_sdk_math_original.c (status background sine) — test_sdk_math_original_reference; test_status_background_reference |  | S2_opening |
| 0x0011E398 | — | AI | verified-unbound | em_sdk_math_original — test_sdk_math_original_reference |  | S2_opening |
| 0x0011E620 | — | BM | live | em_camera.c camera_commit_original (inline atan2) — test_camera_commit_reference |  | S1_newgame_load |
| 0x0011E748 | — | NM | live | em_item_sdk_math.c em_item_sdk_sqrt — test_item_sdk_math_reference; test_sdk_math_original_reference | the live wall probes use host sqrtf instead | S0_title |
| 0x0011FD78 | — | BM | verified-unbound | em_sdk_soft_float — test_sdk_soft_float_reference |  | 03_panel_power |
| 0x00122BB8 | — | BM | live | em_random.c — test_random_seed_reference; test_random_reference.py |  | S1_newgame_load |
| 0x00126AB8 | — | AW | verified-unbound | em_sdk_soft_float — test_sdk_soft_float_reference |  | 03_panel_power |
| 0x00126BE8 | — | AW | verified-unbound | em_sdk_soft_float — test_sdk_soft_float_reference |  | 03_panel_power |
| 0x00127398 | — | AW | verified-unbound | em_sdk_soft_float — test_sdk_soft_float_reference |  | 03_panel_power |
| 0x001274B0 | — | BM | verified-unbound | em_sdk_soft_float — test_sdk_soft_float_reference |  | 03_panel_power |
| 0x00127728 | — | BM | verified-unbound | em_sdk_soft_float — test_sdk_soft_float_reference |  | 03_panel_power |
| 0x00127758 | — | AI | verified-unbound | em_sdk_soft_float — test_sdk_soft_float_reference |  | 03_panel_power |
| 0x001277B0 | — | AW | verified-unbound | em_sdk_soft_float — test_sdk_soft_float_reference |  | 03_panel_power |
| 0x001278C0 | — | AW | live | em_weather.c / em_status_background.c (EE conversions) — test_weather_reference; test_status_background_reference |  | S1_newgame_load |
| 0x001281C0 | float_to_int | AI | live | em_status_background.c to_int / em_snow.c — test_status_background_reference; test_snow_tiles_reference |  | S1_newgame_load |
| 0x00128250 | — | AW | live | em_weather.c / em_status_background.c (EE conversions) — test_weather_reference; test_status_background_reference |  | S1_newgame_load |
| 0x00128320 | — | BM | verified-unbound | em_sdk_soft_float — test_sdk_soft_float_reference |  | 03_panel_power |
| 0x00128350 | — | AI | live | em_item_root.c (EE compare) — test_item_root_reference |  | 03_panel_power |

## 4. Platform boundaries

478 functions (25,063 instructions) are SDK, libc, IOP, driver or GS/VU1 work that the port replaces with a native service. They are not counted in the five statuses. "Contract" says whether anything compares the port's substitute with the original's observable effect.

Rules used: the SDK ranges of the decomp's SUBSYSTEMS.md (0x100000..0x12FFFF: DMA/VU1 library, libmpeg, kernel syscalls, libpad, libcdvd, libmc/SIF RPC, the EE sound library, the C runtime), the GS/VIF packet builders, texture uploads and display-object registry in 0x1CB5C0..0x1DAFFF, the module loader and disc reads 0x1FF080..0x2009E0, the IOP service, heap and MPEG glue in 0x203350..0x206D80, and the 2D draw layer. A function in the render ranges that carries a translation (the load-veil particles, shadow kernels, head sprite, message glyphs) is classified normally instead. The VU0 math leaves (0x1026A0..0x103237), libm (0x11C4C8..0x11FD77), soft float, the float conversions and rand are **game-visible arithmetic, not boundaries**: they are classified in section 3.

| Kind | Functions | Instructions | Port substitute | Contract verified |
|---|---:|---:|---|---|
| SDK libmpeg / IPU movie decode | 102 | 6,893 | em_movie_mac.m (AVFoundation) over movies exported by tools/export_movie.py | no original comparison (movie export test only) |
| EE sound library (SPU2 voices, sequencer, IOP sound RPC) | 59 | 3,207 | em_sfx.c + em_sfx_bank.c + em_audio_mac.c | 41: no (dry mixer; no SPU2 ADSR/reverb, AM-03/04); 9: partly: test_area11_sfx_reference; 5: partly: test_stream_lanes_reference; 3: partly: test_area11_sfx_reference, test_area11_sfx_runtime; 1: partly: test_area11_sfx_reference, test_area11_sfx_runtime, test_stream_lanes_reference |
| EE kernel syscalls, interrupts, threads, SIF DMA glue | 57 | 1,085 | host OS; nothing to reproduce | n/a |
| GS/VIF packet build / VU1 kick | 51 | 1,615 | native renderer (em_render_frame.c, em_gfx.h contract, gfx/metal/em_gfx_metal.m) | partly: level material and overlay-blend tests; fog, snow and status draws checked in their own tests |
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
| GS/VIF packet build (sprites, flush) | 8 | 325 | native renderer (em_render_frame.c, em_gfx.h contract, gfx/metal/em_gfx_metal.m) | not compared per call |
| resource / display-object registry | 8 | 557 | locally exported assets (EMDL, roster, props) | no |
| GS texture upload | 6 | 426 | native renderer (em_render_frame.c, em_gfx.h contract, gfx/metal/em_gfx_metal.m) texture upload | not compared per call |
| C runtime string/format | 3 | 222 | host snprintf/strlen in em_status_hub_ui.c / em_message_draw_original.c | through the callers' oracles (test_status_hub_ui_reference, test_message_draw_reference) |
| stream / voice IOP service tick | 3 | 304 | em_bgm.c + em_opening_media.c native streams | no |
| 2D GS glyph/sprite draw | 2 | 228 | em_gfx 2D layer | 1: draw commands compared by test_status_draw_reference / test_status_hub_ui_reference; 1: draw commands compared by test_status_hub_ui_reference |
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
- **GS/VIF packet build / VU1 kick** (51): 001D1AE0, 001D1C10, 001D2090, 001D2110, 001D2130, 001D2160, 001D2180, 001D21B0, 001D21E0, 001D2580,
  001D2710, 001D2730, 001D2910, 001D2DE0, 001D2E00, 001D2E20, 001D37D0, 001D38A0, 001D38F0, 001D3900,
  001D3990, 001D3AD0, 001D3BA0, 001D3C30, 001D3CF0, 001D3D90, 001D3E40, 001D3F50, 001D4650, 001D4740,
  001D4750, 001D4960, 001D49D0, 001D4A90, 001D4B10, 001D4B20, 001D4B80, 001D4C20, 001D4DA0, 001D4E20,
  001D4EA0, 001D4F30, 001D6B10, 001D6C90, 001D6F60, 001D7100, 001D71A0, 001D71F0, 001D7410, 001D9060,
  001D9720.
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
- **GS/VIF packet build (sprites, flush)** (8): 001CABA0, 001CB5C0, 001CB800, 001CB8A0, 001CB950, 001CB9B0, 001CBA40, 001CBC20.
- **resource / display-object registry** (8): 001CF870, 001CF970, 001CFAE0, 001CFB50, 001CFFE0, 001D04B0, 001D0540, 001D0660.
- **GS texture upload** (6): 001CC8A0, 001CCB00, 001CCB10, 001CCBD0, 001CCCC0, 001CCE80.
- **C runtime string/format** (3): 00122EF0, 00123168, 001232E0.
- **stream / voice IOP service tick** (3): 001F9820, 001F9B20, 001F9BF0.
- **2D GS glyph/sprite draw** (2): 001CBA50, 001CC1E0.
- **IOP movie service** (2): 00203350, 002036E0.
- **C runtime memset** (1): 00121A28.
- **overlay module dispatch** (1): 001E7780.

Boundary notes:

- **Sound library.** The port plays AREA11 cues through the pitch path verified by `test_area11_sfx_reference` (00115850 bend, 00117918), but its mixer has no SPU2 ADSR, Gaussian interpolation or reverb (WP-14, AM-03/04). Stream gain 0x1999 is only reported (H22).
- **Module loader.** The native area read replaces 001FF080(1, 0) and the load arms run through the verified cores; the status-page module 0x1F/0x21 load is instant where the original waits 24 dispatches (H7).
- **Message glyphs (WP-8).** 001CC1E0 (tall-font strip layout) is translated and live in `em_message_glyph_original`, and 001CC8A0/001CCB00 are its upload/reset boundary there; `test_message_glyph_reference.py` compares every upload call and every packed pass with the executed original. The glyph texels are the port's atlas.
- **GS/VU1.** The renderer is the port's own. Individual packet builders have no per-call comparison; the level material, overlay blend, fog, snow and status draw tests compare their results where they exist. The game-side render heads (001D1C50, 001D1EA0, 001D30A0) and the projection helpers are **not** treated as boundaries: they are stand-ins in section 3.
- **Movies.** Title-only MPEG code and the unidentified 0x205050..0x205F90 glue (title only, adjacent to the MPEG glue) are replaced by the native movie path; no original comparison exists.

## 5. Gap lanes (prioritized)

Every non-live, non-boundary function belongs to exactly one lane. Sizes are in original instructions. "bind" lanes wire verified translations that already exist and delete the stand-in; "translate" lanes need new translations and oracles first. The order follows the route and the dependencies: the player stage and collision come first because every beat from 05 on needs them.

| # | Lane | Kind | Instr. | Functions (status mix) | Gates beats | Depends on |
|---:|---|---|---:|---|---|---|
| 1 | **L01-player-stage-live**: Engage the translated player stage (FLOOR gate): 0015BA50/0015B130/0015BCF0 and the stage workers, retire player_damage_tick. **Bound 2026-09-23 (partial):** 10 live (0015BCF0 tail only), 4 bound but unreached (0015B530, 001837A0, 00182B30, 00182D70: the interaction runtime still stands in for the scripted takeover and consumes the stage before 0015B130's prelude), 0015CF90 unverified (no oracle); the prelude on the port's idle/walk waits on 0017B490 (L12) | bind | 1,922 | 15 (live 10, verified-unbound 4, unverified 1) | every frame from first control; prerequisite of the floor, slide, climb, ladder and jump lanes | nothing (translations exist); retire em_player_damage.c copies in the same change (done) |
| 2 | **L05-coll-move-walkers**: Replace em_collision movement queries with the translated move/sweep walkers | bind | 2,163 | 8 (verified-unbound 8) | every frame (player and camera movement queries) | nothing (em_coll_move_original verified) |
| 3 | **L07-actor-collision-live**: Link em_actor_collision: grid/column scan, actor hulls, list classes and the 001AAD00 swap | bind | 2,224 | 13 (verified-unbound 9, unverified 4) | 05 (standing on crates), 08 (standing on the truck), 10 | L05 |
| 4 | **L02-floor-fall-live**: Bind the floor service and fall check (00175900/001796C0) and the fall state, retiring the floor snap and PLAYER_FALL_ENTRY | bind | 1,755 | 14 (verified-unbound 14) | 05, 06, 08, 10 (step-offs, the cage-roof fall) | L01; L07 (column scan 0019BC40 and actor collision) |
| 5 | **L04-box-climb**: Bind ledge climb / vault (0015DF10, 00161790) for the boxes | bind | 1,960 | 12 (verified-unbound 12) | 05 (climbing the boxes) | L01, L02, L05, L06 |
| 6 | **L03-hill-slide**: Bind the slide state (0016C6A0 family) for the hill | bind | 1,560 | 8 (verified-unbound 8) | 06 (sliding down the hill) | L02 |
| 7 | **L06-coll-probe-walkers**: Bind the translated probe walkers (em_coll_probe_original) | bind | 1,992 | 9 (verified-unbound 9) | every frame (probes); 05 | L05 |
| 8 | **L06b-coll-segment-walkers**: Bind the translated segment walkers (em_coll_segment_walkers) | bind | 2,137 | 7 (verified-unbound 7) | every frame (camera and segment queries) | L05 |
| 9 | **L19-script-host**: Bind the area-script op handlers and write oracles for the unverified ones | bind+verify | 1,968 | 21 (verified-unbound 11, unverified 8, missing 2) | 07 (truck camera preview), 10, 11, 13, 14 | nothing |
| 10 | **L20-message-service**: WP-8: extend the live panel message service (001FCA10) to every caller, with voice pushes and the stream table | bind | 997 | 18 (verified-unbound 16, stand-in 1, missing 1) | 10, 11 (director lines), 14 (Roger) | L19 |
| 11 | **L23-truck**: WP-12: bind the truck and its camera trigger | bind | 1,461 | 3 (verified-unbound 2, missing 1) | 07, 08 (truck preview and crossing) | L02, L07, L19 |
| 12 | **L17-pickups-use-arbiter**: WP-6: bind the pickup owners and publish them to the Use arbiter; retire the legacy take | bind | 1,069 | 10 (verified-unbound 6, unverified 3, stand-in 1) | 01 (battery pickup) and every other pickup | host (done) |
| 13 | **L09-ladder-use-chain**: Bind the Use chain and ladder entry (0015D4C0, 00160220 whole, 00165B60) | bind | 2,069 | 10 (verified-unbound 10) | 05, 10, 13 (ladders and the Use chain) | L01, L02, L06 |
| 14 | **L10-ladder-climb**: Bind the ladder climb states (001662D0 family) | bind | 1,856 | 8 (verified-unbound 8) | 10, 13 | L09 |
| 15 | **L21-director-beats**: WP-10: manager 008253F0 and its beat scripts through the script host | bind | 410 | 9 (verified-unbound 9) | 10, 11 | L19, L20 |
| 16 | **L22-roger-encounter**: WP-9: bind the Roger owner, equipment child and cutscene timeline | bind | 2,121 | 24 (verified-unbound 24) | 10, 14 (Roger) | L19, L20 |
| 17 | **L11-running-jump-recovery**: Bind the running jump and recovery (0015EC50, 001634A0, 0017C860) | bind | 2,297 | 6 (verified-unbound 6) | 12 (crevice jump) | L02, L09 |
| 18 | **L13-camera-follow**: Bind em_camera_follow_original (follow core) in place of em_camera.c camera_update | bind | 1,587 | 15 (verified-unbound 10, unverified 1, stand-in 2, missing 2) | every frame from first control | L06b (camera queries) |
| 19 | **L14-camera-solver-dd20**: Bind 0018DD20 (desired-eye solver) and retire the unverified duplicate; oracle 0018CE60 | bind | 2,051 | 2 (verified-unbound 1, unverified 1) | every frame from first control | L13 |
| 20 | **L15-camera-actions**: Bind the camera action dispatch 0018BC20 and em_camera_area11_specials (001921D0, 00193EB0) | bind | 1,951 | 5 (verified-unbound 4, stand-in 1) | every frame from first control | L13 |
| 21 | **L16-camera-area11-walk**: Bind 00195130 (AREA11 walking specials) and 001916C0; translate the 0015CBA0 state map | bind+translate | 1,843 | 3 (verified-unbound 2, stand-in 1) | every frame from first control (AREA11 walking specials) | L15 |
| 22 | **L12-locomotion-display**: Replace the legacy idle/walk callbacks and gait display with 00161020/001612D0/0017B660 and their verified workers | translate+bind | 1,742 | 15 (verified-unbound 7, unverified 2, stand-in 4, missing 2) | every frame from first control (idle/walk look and footsteps) | L01 |
| 23 | **L24-fan-husk**: WP-11: bind the fan pair; translate the husk pair (overlay, no C) | bind+translate | 1,924 | 4 (verified-unbound 1, stand-in 2, missing 1) | every frame (husk pair), level exit (fan) | L07 |
| 24 | **L25-crates-drums**: WP-18: bind crates and drums in place of em_enemy | bind | 1,885 | 2 (verified-unbound 2) | every frame; 05 | L07 |
| 25 | **L18-door-original**: WP-7: bind the original door runtime/program/transit | bind | 881 | 11 (verified-unbound 7, unverified 3, stand-in 1) | 09 (fence door, side beat) | L17 (Use arbitration) |
| 26 | **L28-player-equipment**: Translate the player equipment/weapon actors (0018A6B0 nodes) and the gun tick | translate | 2,101 | 13 (verified-unbound 1, unverified 1, stand-in 3, missing 8) | every frame (rifle/knife children, gun tick, aim) | L01 |
| 27 | **L26-effect-manager**: Translate the effect manager barrel and bind em_effect_original | translate+bind | 2,342 | 15 (verified-unbound 5, missing 10) | every frame (effects) | nothing |
| 28 | **L27-effect-kinds**: Translate the effect kinds/tables and the effect colour | translate | 1,251 | 18 (unverified 1, missing 17) | every frame (effects); 00, 05, 06 (footstep and slide effects) | L26 |
| 29 | **L08-coll-missing-and-list-passes**: Translate the untranslated collision originals and the actor list passes | translate | 2,081 | 14 (stand-in 7, missing 7) | every frame; 05, 07..14 | L07 |
| 30 | **L29-shadow-route**: Bind the shadow actor route (0015BF90) and its packet producers | bind+verify | 974 | 7 (verified-unbound 6, unverified 1) | every frame from 02 (player shadow) | renderer |
| 31 | **L29b-shadow-gs**: Bind em_shadow_original / em_shadow_gs (receiver passes, clip kernels) | bind+verify | 1,779 | 11 (verified-unbound 6, unverified 5) | every frame (shadow receivers) | L29-shadow-route |
| 32 | **L32-frame-render-heads**: Replace the port collectors at 001D1C50/001D1EA0/001D30A0 and the projection with translations | translate | 1,545 | 18 (unverified 4, stand-in 7, missing 7) | every frame (frame setup, projection) | renderer |
| 33 | **L30-render-context**: Translate the render-context / HUD bar / area-specials path | translate | 1,612 | 16 (unverified 1, missing 15) | every frame (render context, HUD bar) | renderer |
| 34 | **L31-background-weather-load**: Bind em_background_gs and the area-load render passes (001C1DC0 family) | bind+verify | 932 | 17 (verified-unbound 7, unverified 5, missing 5) | S1 (area load), 09 (room move), every frame (background) | renderer |
| 35 | **L33-anim-runtime-rest**: Translate the remaining animation-runtime originals and bind em_pose_host_workers | translate+bind | 1,819 | 22 (verified-unbound 15, unverified 1, missing 6) | every frame (animation) | nothing |
| 36 | **L36-stream-lanes-sound**: Bind the stream lanes (music/voice) and the gain/positional sound originals | bind | 1,530 | 17 (verified-unbound 17) | every frame (music and voice streams) | IOP/disc boundary decisions |
| 37 | **L38-load-veil-particles**: Bind the load-veil particles (0021B1B0/0021B500) so the load is not black | bind | 1,074 | 16 (verified-unbound 16) | S1, 09 (loads) | nothing |
| 38 | **L39-head-sprite-effects**: Bind the head-bone sprite effect (001E2560 node) and its registry helpers | bind | 868 | 12 (verified-unbound 12) | every frame (head-bone sprite) | renderer |
| 39 | **L35-status-ui-leftovers**: Area-title card, UI cues, message lookup and the remaining owner-service leaves | translate | 943 | 25 (verified-unbound 12, stand-in 5, missing 8) | S2 (area title), 01, 03 (UI cues), owner services | L20 (message lookup) |
| 40 | **L34-startup-and-load-gaps**: Close the title/New Game/load gaps (task table oracle, continue task, state-0 re-arm, sound-bank load) | translate+verify | 1,626 | 25 (verified-unbound 3, unverified 7, stand-in 5, missing 10) | S0..S2 (title, New Game, load, opening) | nothing |
| 41 | **L37-sdk-math-leaves**: Bind the remaining SDK math / soft-float translations at their call sites | bind | 997 | 20 (verified-unbound 17, missing 3) | wherever the bound callers run | nothing |

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
- **L09-ladder-use-chain**: 0015D4C0, 00160220, 00165B60, 00176F90, 00177030, 00199DB0, 0019BA80, 001B61C0, 00182A70, 001885D0.
- **L10-ladder-climb**: 001662D0, 0017FC80, 0017FD00, 00180300, 00180420, 00180460, 00181110, 00174AB0.
- **L21-director-beats**: 008253F0, 00825500, 00825540, 00825600, 00825640, 008256D0, 00825710, 001B1EA0, 0011E080 (001C4760 is live since HK: the world's d810CC3/d8106B0/d8106B1 are bound through em_director_original_001C4760_scene).
- **L22-roger-encounter**: 008237E0, 00823910, 00823950, 00823B70, 001C5C90, 001BA540, 001BA580, 001BA8E0, 0022EEF0, 0022EC30,
  001CA5E0, 001CA5F0, 001CA6E0, 001CA6F0, 001CA700, 001CA770, 001D0690, 001D06D0, 001D0C70, 001B10B0,
  001B1020, 001AF780, 001AF890, 001D8BF0.
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
  001E0DF0, 001E1010, 001D5370, 001D52E0, 001E0CC0, 001DD950.
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
  001CA7B0, 001CA940, 001CA990, 001CAA00, 001CB3C0.
- **L34-startup-and-load-gaps**: 001AB4E0, 001AB590, 001AB6A0, 001AB740, 001AB790, 001AC070, 001AF470, 001AF5C0, 001AF690, 001AF710,
  001AFCA0, 001B0F60, 001B57E0, 001B5F40, 001FB100, 001FC6E0, 001FB3E0, 001FB910, 001FB370, 008237C0,
  00199C50, 001BB0E0, 0015C1F0, 001AB7D0, 001FEF70.
- **L37-sdk-math-leaves**: 0011C4C8, 0011D878, 0011E398, 0011DBB8, 0011DE90, 0011DB90, 0011FD78, 00126AB8, 00126BE8, 00127398,
  001274B0, 00127728, 00127758, 001277B0, 00128320, 00102718, 00102738, 001027E0, 00102850, 001000E0.

## 6. Limits

- **Census coverage.** Boot before the title (crt0, IOP bring-up, logos, card checks) is not instrumented; only listed entries are armed (jumps into a function body and the unlisted 0x1050E4..0x105148 gap are unseen); one hit per function per label (no counts, no order); only the played route is exercised (no damage/death, pause/options/save, Circle/Square/R1/weapon/camera inputs, truck-pit fall, the west-yard and plateau ladders); nothing after Roger's encounter, so **the level exit (fan 00827630's area change, Roger's departure script) is not in the census**. Several beat replays follow slightly different closed-loop paths from the recordings (census method.md).
- **Classification.** The evidence search is textual and was then read by hand; it can miss a translation that cites no address, or credit a test that names an address it only hooks. The rows marked "evidence rule" in `classified.json` (193) were not individually re-read. Reachability is a static call graph with the known runtime gates cut; code behind other `getenv`/state gates may be counted as live.
- **Oracle strength varies.** `test_fade_reference.py` compares with locally compiled decomp C (the fade functions are byte-matched), not executed instructions. The spawn helpers (0015C310/0015C420, 0018A880, 001C1EA0, 001C5570) are checked only for spawn set and order (`compare_frame_order.py`, census 49), not node bytes. The heading helper's SDK trig and the live wall probes' sqrt/atan are host models.
- **Snapshot.** Statuses reflect the port tree on 2026-09-23 while other lanes were still committing (for example `tools/test_pickup_items_reference.py` appeared during the run). Re-running the classification after each lane lands keeps this document honest; the decomp census itself only needs re-running when the route changes. The section 2 totals (and `classified.json`) are those of the classification run; landing steps since then (WP-8, HK) re-classified their rows in sections 3 and 5 only, so the totals lag those rows until the next classification pass.


## 7. Critic notes (completeness review, 2026-09-23)

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
| 00128350 | live (em_item_root.c "EE compare", test_item_root_reference) | `test_item_root_reference.py` replaces it with a host conversion, and em_item_root.c uses a host cast. The real translation is `em_sdk_soft_float_00128350`, verified by `test_sdk_soft_float_reference.py`, but `em_sdk_soft_float.c` is not in COMMON. | verified-unbound (the live cast is a host boundary) |
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
- 00183EF0 / 00184BA0 (live): the published scan list holds only the panel and the elevator (W22). The crate Use in beat 05 and the door in beat 09 go through this scan in the original, so on those beats the function is not live-original.

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
