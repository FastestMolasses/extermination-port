# AREA00 pre-translation survey (a00_04 .. a00_10, a00_s0)

2026-10-28, pre-translation agent P1 for the drawbridge goal, branch
`pretrans-a00` (from `drawbridge` 426a85f). Purpose: get ahead of the
frontier stepper (LEVEL3_BINDING.md "Current state", iteration 15 stops in
a00_06) by listing every original function the original first runs in
AREA00 beats a00_04 .. a00_10 and the side return a00_s0, and translating
the ones the port lacks.

**Result: no translation is missing.** Every game function in those beats
already has a port translation with an original-instruction reference test
(the A00LOW / A00HUD / A00FX / A00WORLD lanes, AREA00_LOW.md, AREA00_HUD.md,
AREA00_FX.md, AREA00_WORLD.md, plus the first-level and AREA01 modules). No
new translation files, Makefile entries or reference tests were added. What
remains is binding: seven call sites in a00_09 and the a00_10 exit that the
live composition does not reach yet (section 2). They are the next
function stops the frontier will hit after the view/field stops of a00_06..a00_08.

## 1. Method

- Census: the decomp's `build/s87/census/a00_delta.json` (`functions`, 1270
  functions the original executed on the AREA00 route; entry breakpoints,
  no unattributed hits). Selected: `first_beat` in a00_04_cage_terminal ..
  a00_10_progression_exit or a00_s0_shaft_door_back. **448 functions**
  (a00_04 25, a00_05 31, a00_06 13, a00_07 16, a00_08 266, a00_09 50,
  a00_10 43, a00_s0 4). a00_s0's four (001BA580, 001CB3C0, 001D0720,
  001D0C70, its exit phase at frame 563) are first-level functions that are live
  on AREA11's path.
  `a00_arrival.json` is the AREA01 a01_07 exit's arrival; its 19 functions
  first run before a00_00 and are bound already.
- Port coverage: a translation counts only when a function definition (or
  an ENTRY / HANDLER_ENTRY macro) carries the address in `src/game`, not when
  a comment mentions it. Hand-checked: 001A58B0 (static in
  em_actor_collision.c), 001647D0, 0016D130, 001639E0, 00173E60 (player
  state functions), 001B7A30 (em_area_script op 0x0F), 001C0004 (second piece
  of 001BFFD0).
- Native boundaries: the 220 functions without a port definition are all
  listed as native boundaries in LEVEL_READINESS.md (SDK libmpeg / IPU 101,
  title-only MPEG glue 26, EE kernel / SIF 19, heap 18, MPEG glue 16, EE sound
  library 14, IOP services 13, 2D GS layer 6, libmc 4, DMA/VIF 3). All of them
  ran on the first level already; their census `first_beat` is a00_08 frame
  357 (the switch's movie). Of the 112 first-level functions that the port
  names, 77 have a translation. The other 35 are named only in comments: 34
  are native boundaries of the same kinds (GS upload, VIF packets, IOP
  stream / movie services), and 00179680 is live inside em_player_floor.c.
- Reached-by: an address case (`case 0x...u`, `== 0x...u`, a k_bindings
  row) in a live file, a call from a caller inside the same translated module,
  or a player state / worker table entry. Callers come from the direct calls
  of each original function (the decomp's normalised instruction stream);
  indirect callers come from the ELF's pointer tables.

## 2. Binding stops ahead (in beat order)

None of these needs new translation work. Each has its translation, its
reference test and an existing live pattern to copy. Only the binding is
missing. These files belong to the frontier (em_effects_live*, em_area11_bindings,
em_player_stage_live, em_render_context_live), so P1 did not edit them.

| Stop | First run | Function | Translation (entry point) | Where the original reaches it | Live binding pattern |
|---|---|---|---|---|---|
| S1 | a00_09 f481 | 001ECFB0 | `em_area00_hud_001ECFB0(EmArea00Hud *, a0 = node + 0xD0, a1 = depth)` | 001EA240 effect handler, subtype 3 (table D_00255430, AREA00_HUD.md) | `w_handler` in em_effects_live.c: copy `handler_001ED7A0` (same impact_worker: 001CFB50 / 001CFBE0) |
| S2 | a00_09 f483 | 001F6FB0 | `em_area00_fx_001F6FB0` (em_area00_fx_glow.c) | pool callback of the glow effect 0x8000000F (descriptor word 0x00257F6C) | a k_bindings row in em_area11_bindings.c with `tick_effect_node`, as for 001F2BA0 / 001F18C0 |
| S3 | a00_09 f755 | 001EF510 | `em_area00_fx_001EF510(S *, a0, a1)` (em_area00_fx_spawn.c) | 001EA240 effect handler, subtype 6 (AREA00_FX.md) | `w_handler` in em_effects_live.c, as the hazard handlers |
| S4 | a00_09 f756 | 001E7310 | `em_area00_low_001E7310(EmArea00Low *, a0)` | pool callback (descriptor word 0x00258ECC; neighbours 001EA240 at 0x00258E9C, 001E7440 at 0x00258EFC) | a k_bindings row with an em_area00_low adapter |
| S5 | a00_09 f756 | 001F0060 | `em_area00_fx_001F0060(S *, id, owner)` (em_area00_fx_spawn.c) | 0021D4E0 (from 0021C270) and 0015D100 | replace `stub_001F0060` in em_player_stage_live.c (`c->w001F0060`) |
| S6 | a00_09 f757 | 001DF110 | `em_area00_hud_001DF110(EmArea00Hud *, a0 = record + 0x10)` | 001DEEE0, the render context's ramp record (em_render_context.c `w_001DF110`) | replace `w->w_001DF110 = unbound_u` in em_render_context_live.c |
| S7 | a00_10 exit f979 | 001B0CD0 | `em_area00_world_001B0CD0` | the AREA01 overlay in the return arrival (AREA01 / AREA15 overlay callers) | em_area00_return's `w_001B0CD0` hook (module UNWIRED; the return phase) |

Callees each binding must answer (direct calls of the original; internal
means the translation calls its own module directly):

- **S1 001ECFB0:** 001CFB50, 001CFBE0. Data: descriptor rows D_002571B0 +
  0x90 k, k = 0..2 (0x1B0 bytes). They are not in
  export_area00_runtime_tables.py yet (compare HAZARD_EFFECT_WINDOWS).
  Views: D_00275C34 (work pointer), work +0x04 (LCG) and +0x54.
- **S2 001F6FB0:** 00102900, 00102918, 00102948, 00102958, 001029C0 (VU0
  vector helpers), 00122BB8, 001281C0, 0011DE90, 0011E2A8, 001CA7B0,
  001C7900, 001C6120, 001CA940, 001CCF70, 001CFA60, 001CFBE0, 001AFC10.
  Data: D_0025D800..D_0025D818, D_0028A56C. Scratch: 0x700036A0..0x700036BC,
  0x700036E0..0x70003710, 0x700038A0..0x700038C8, 0x70003A20. Decomp has
  no C (asm only). The translation follows the instructions; see AREA00_FX.md.
- **S3 001EF510:** 001EFD90 (spawn; em_effects_live already answers it),
  001CFB50, 001CFBE0, 001EEEB0 (bound as the hazard hit handler's
  translation, em_area00_hud_001EEEB0). Data: tables D_00257990, D_00257AB0,
  D_00257A20 (not exported yet), D_00257510 (exported, HAZARD_EFFECT_WINDOWS),
  D_0081F8F0. Views: D_00275C30 / D_00275C34, work +0x04 / +0x54.
- **S4 001E7310:** 001D2830, 001281C0, 001AFC10, and the em_area00_hud
  translations 001DEE80 / 001DEEC0 (internal call to 001DEDB0) and
  001DF5A0 (internal call to 001DF180, which calls 001D6B60, 001D6BA0,
  001D1F20, 001D1FF0, 001D2040, 001D7080, 0011DF78, 00122BB8, 00102948,
  001281C0). 001DF5A0 also calls 001CB760. Data: D_0026E970, D_0026E860.
  The f12 that 001E7310 passes to 001DF5A0 is 001DF180's amplitude
  (AREA00_HUD.md).
- **S5 001F0060:** 001EF9D0 (bound in em_effects_live, `spawn_result`).
  Then on a node, +0x24 = owner.
- **S6 001DF110:** 001CB760, and internally 001DF020, which calls 001D1F20,
  001D1F80, 001D1FF0 and the translations 001D6DD0 / 001D7510 / 001D7A80
  (em_area00_fx_gs.c) and 001D7000 (em_static_world.c).
- **S7 001B0CD0:** 001F4F40 (em_area00_fx_exit), 00102948, 00102958, 001026A0.

## 3. Every function first run in a00_04 .. a00_10

These are the 116 functions that are new on the route or first ran in
AREA01 / beat 15: those the census does not mark `already_ran: first_level`.
The 112 first-level functions in these beats are live on AREA11's path or
are native boundaries (section 1; FIRST_LEVEL_CENSUS.md). The 220
native-boundary functions are in section 1. Decomp: BM byte-matched C, NM
NEARMISS (body-correct C; the instructions decide), asm (no committable C;
the translation follows the instructions).

| Function | First run | Decomp | Port entry point | Reached by (live) | Reference test |
|---|---|---|---|---|---|
| 0012E0B0 | a00_04 f104 | BM | em_area00_low.c: em_area00_low_0012E0B0 | inside 0012C490 (em_area00_low; em_area01_live case) | test_area00_low_reference, test_area01_light_live_reference |
| 0012E260 | a00_04 f104 | BM | em_area00_low.c: em_area00_low_0012E260 | inside 0012C490 (em_area00_low; em_area01_live case) | test_area00_low_reference |
| 0021BD60 | a00_04 f104 | asm | em_area00_world.c: em_area00_world_0021BD60 | case in em_area01_light_live | test_area00_world_reference, test_area01_light_live_reference |
| 001B0300 | a00_04 f345 | BM | em_area01_sys.c: em_area01_sys_001B0300; em_camera_live.c: lw_001B0300 | case in em_scene_bindings | test_area01_scratch_alias, test_area01_sys_reference |
| 008253E0 | a00_04 f831 | overlay C | em_area00_overlay.c: em_area00_ovl_008253E0 | case in em_area01_runtime, em_area01_script_live | test_area00_overlay_reference |
| 001B7670 | a00_04 f992 | asm | em_area00_low.c: em_area00_low_001B7670 | case in em_area01_script_live, em_area01_script_workers | test_area00_low_reference |
| 001181B0 | a00_04 f1269 | NM | em_area00_low.c: em_area00_low_001181B0 | only from 001152D8 (EE sound library: native boundary) | test_area00_low_reference |
| 00119080 | a00_04 f1269 | BM | em_area00_low.c: em_area00_low_00119080 | only from 00116598 (EE sound library: native boundary) | test_area00_low_reference |
| 001B76D0 | a00_04 f1541 | BM | em_area01_sys.c: em_area01_sys_001B76D0 | case in em_area01_script_live | test_area01_sys_reference |
| 001B8AB0 | a00_04 f1542 | NM | em_area00_low.c: em_area00_low_001B8AB0 | case in em_area01_script_live, em_area01_script_workers | test_area00_low_reference, test_area01_script_workers_reference |
| 0012B970 | a00_05 f1 | BM | em_area00_low.c: em_area00_low_0012B970 | case in em_area01_exita, em_area01_live | test_area00_low_reference |
| 001A58B0 | a00_05 f1057 | asm | em_actor_collision.c (static, pass 1 worker) | em_actor_collision pass 1 worker (static) | test_actor_collision_reference |
| 001776E0 | a00_06 f1154 | NM | em_player_closure_live.c: w_001776E0; em_player_record_helpers.c: em_player_helper_001776E0 | em_player_closure_live w_001776E0 | test_player_climb_reference, test_player_record_helpers_reference |
| 00177CF0 | a00_06 f1154 | NM | em_player_closure_live.c: w_00177CF0; em_player_record_helpers.c: em_player_helper_00177CF0 | em_player_closure_live w_00177CF0 | test_player_climb_reference, test_player_record_helpers_reference |
| 0017F320 | a00_06 f1189 | NM | em_player_closure_live.c: w_0017F320; em_player_record_helpers.c: em_player_helper_0017F320 | em_player_closure_live w_0017F320 | test_player_climb_reference, test_player_fall_reference |
| 00188550 | a00_06 f1210 | BM | em_player_closure_live.c: w_00188550; em_player_record_helpers.c: em_player_helper_00188550 | em_player_closure_live w_00188550 | test_player_climb_reference, test_player_major2_reference |
| 00191120 | a00_06 f1210 | asm | em_camera_follow_original.c: em_camera_follow_00191120 | em_camera_follow_original (camera owner) | test_area01_sys_reference, test_area06_port_reference |
| 001647D0 | a00_06 f1211 | NM | em_player_hang.c: em_player_hang_state | player state 0x09 (em_player_closure_live set1) | test_area01_render_existing_reference, test_player_hang_reference |
| 0017F240 | a00_06 f1211 | asm | em_player_hang.c: em_player_hang_0017F240 | inside em_player_hang | test_area01_render_existing_reference, test_player_closure_0e_18_reference |
| 00182250 | a00_06 f1211 | BM | em_player_misc_workers.c: em_player_misc_00182250 | em_player_misc_workers (player workers) | test_player_misc_workers_reference |
| 0017E250 | a00_06 f1214 | NM | em_player_misc_workers.c: em_player_misc_0017E250 | em_player_misc_workers (player workers) | test_player_misc_workers_reference |
| 0017E510 | a00_06 f1214 | BM | em_player_misc_workers.c: em_player_misc_0017E510 | em_player_misc_workers (player workers) | test_player_misc_workers_reference |
| 0016D130 | a00_07 f889 | BM | em_player_closure_0e_18.c: em_player_closure_state18 | player state 0x18 (em_player_closure_live set1) | test_area01_room_reference, test_player_closure_0e_18_reference |
| 00198CE0 | a00_07 f889 | BM | em_camera_live.c: lw_00198CE0; em_area00_low.c: em_area00_low_00198CE0 | case in em_camera_leftovers, em_camera_live | test_area00_low_reference, test_camera_interaction_fixture |
| 0016DE40 | a00_07 f1035 | BM | em_player_closure_10_12_19.c: e0016DE40_crawl; em_player_closure_10_12_19.c: em_player_closure1019_0016DE40 | em_player_closure_10_12_19 (player closure) | test_area01_room_reference, test_player_closure_10_12_19_reference |
| 001F4E20 | a00_07 f1035 | BM | em_area00_fx_gs.c: em_area00_fx_001F4E20 | case in em_area01_runtime | test_area00_fx_reference, test_area00_overlay_reference |
| 0016EBA0 | a00_07 f1036 | NM | em_player_closure_10_12_19.c: em_player_closure1019_0016EBA0 | em_player_closure_10_12_19 (player closure) | test_area01_room_reference, test_player_closure_10_12_19_reference |
| 00198F10 | a00_07 f1036 | NM | em_camera_live.c: lw_00198F10; em_area00_low.c: em_area00_low_00198F10 | case in em_camera_leftovers | test_area00_low_reference, test_camera_interaction_fixture |
| 001639E0 | a00_07 f1120 | asm | em_player_fall.c: em_player_fall_state7 | player state 7 (em_player_closure_live) | test_area00_fx_existing_reference, test_player_fall_reference |
| 00163D50 | a00_07 f1131 | asm | em_player_fall.c: em_player_fall_00163D50 | inside em_player_fall | test_area01_render_existing_reference, test_player_fall_reference |
| 001B7A30 | a00_08 f355 | BM | em_area_script.c (op 0x0F) | em_area11_script_host (op 0x0F, live since AREA11) | test_level_smoke (route) |
| 001B7700 | a00_08 f360 | NM | em_area00_low.c: em_area00_low_001B7700 | case in em_area01_script_live, em_area01_script_workers | test_area00_low_reference |
| 001FF030 | a00_08 f363 | BM | em_area00_low.c: em_area00_low_001FF030; em_scene_bindings.c: w_001FF030 | em_scene_bindings w_001FF030 (001AE040) | test_area00_low_reference, test_scene_frame_reference |
| 0019C6F0 | a00_08 f456 | NM | em_area00_world.c: em_area00_world_0019C6F0; em_area00_world.c: em_area00_world_view_0019C6F0 | case in em_area01_runtime | test_area00_assets_reference, test_area00_overlay_reference |
| 00823820 | a00_08 f456 | overlay C | em_area00_overlay.c: em_area00_ovl_00823820 | case in em_area01_runtime, em_scene_bindings | test_area00_overlay_reference |
| 001C6200 | a00_09 f190 | asm | em_area00_world.c: em_area00_world_001C6200 | case in em_aim_fire_world_live | test_area00_fx_reference |
| 001CA3B0 | a00_09 f190 | asm | em_area00_world.c: em_area00_world_001CA3B0 | case in em_aim_fire_world_live | test_area00_fx_reference, test_level11_port_reference |
| 001EBC30 | a00_09 f190 | NM | em_effects_live.c: handler_001EBC30; em_area00_hud.c: 001EBC30 | case in em_effects_live | test_area00_box_lift_reference, test_area00_hud_reference |
| 001F2BA0 | a00_09 f190 | NM | em_area00_fx_debris.c: em_area00_fx_001F2BA0 | case in em_aim_fire_world_live, em_area11_bindings | test_area00_fx_reference |
| 001F2E90 | a00_09 f190 | BM | em_area00_fx_debris.c: em_area00_fx_001F2E90 | inside 001F2BA0 (em_area00_fx; k_bindings row) | test_area00_fx_reference |
| 001F2F90 | a00_09 f190 | BM | em_area00_fx_debris.c: em_area00_fx_001F2F90 | case in em_aim_fire_world_live | test_area00_fx_reference |
| 001CA4D0 | a00_09 f191 | asm | em_area00_world.c: em_area00_world_001CA4D0 | case in em_aim_fire_world_live | test_area00_fx_reference, test_area00_world_reference |
| 001F3340 | a00_09 f191 | NM | em_area00_fx_debris.c: em_area00_fx_001F3340 | case in em_aim_fire_world_live | test_area00_fx_reference |
| 001F3620 | a00_09 f191 | NM | em_area00_fx_debris.c: em_area00_fx_001F3620; em_effects_live.c: w_001F3620 | case in em_aim_fire_runtime, em_aim_fire_world_live | test_area00_fx_reference, test_effect_manager_reference |
| 001F3E30 | a00_09 f191 | BM | em_area00_fx_debris.c: em_area00_fx_001F3E30; em_effects_live.c: w_001F3E30 | case in em_aim_fire_world_live | test_area00_fx_reference, test_effect_manager_reference |
| 0012D850 | a00_09 f193 | asm | em_area00_low.c: em_area00_low_0012D850 | case in em_area01_live | test_area00_low_reference |
| 00173E60 | a00_09 f386 | BM | em_player_weapon_states_b.c: em_player_weapon_b_state22 | player state 0x22 (em_player_closure_live) | test_level_smoke, test_player_weapon_states_b_reference |
| 00173DD0 | a00_09 f388 | NM | em_player_weapon_states_b.c: em_player_weapon_b_00173DD0 | inside 00173E60 | test_player_weapon_states_b_reference |
| 001B41F0 | a00_09 f396 | NM | em_area00_world.c: em_area00_world_001B41F0 | case in em_aim_fire_live, em_aim_fire_world_live | test_area00_world_reference, test_area01_target_hit_reference |
| 001ED7A0 | a00_09 f396 | NM | em_effects_live.c: handler_001ED7A0; em_area00_hud.c: 001ED7A0 | case in em_effects_live | test_area01_target_hit_reference |
| 0012E070 | a00_09 f397 | C (asm-linked) | em_area00_low.c: em_area00_low_0012E070 | case in em_area01_live | test_area00_low_reference, test_area01_sys_reference |
| 00129FC0 | a00_09 f398 | BM | em_area00_low.c: em_area00_low_00129FC0 | case in em_area01_live | test_area00_low_reference |
| 0012CAA0 | a00_09 f424 | BM | em_area00_low.c: em_area00_low_0012CAA0 | case in em_area01_live | test_area00_low_reference |
| 0017C370 | a00_09 f424 | asm | em_player_closure_live.c: wa_0017C370; em_player_stage_workers.c: em_player_0017C370 | em_player_closure_live wa_0017C370 | test_player_stage_workers_reference, test_player_weapon_states_a_reference |
| 0015B770 | a00_09 f425 | BM | em_player_floor.c: em_player_stage_0015B770 | em_player_floor (player stage) | test_player_floor_reference, test_player_major2_reference |
| 0021F330 | a00_09 f425 | BM | em_player_reaction.c: em_player_reaction_0021F330 | player reaction 0x0B (em_player_closure_live) | test_player_reaction_reference, test_scene_no_shadow |
| 001754E0 | a00_09 f426 | BM | em_player_reaction.c: em_player_reaction_001754E0 | inside 0021F330 (em_player_reaction) | test_player_reaction_reference |
| 001ECFB0 | a00_09 f481 | NM | em_area00_hud.c: em_area00_hud_001ECFB0 | **unbound: stop S1** | test_area00_hud_reference |
| 0021C350 | a00_09 f482 | asm | em_player_closure_live.c: w_0021C350; em_player_reaction.c: w0021C350 | em_player_closure_live w_0021C350 | test_area01_side_reference, test_player_fall_reference |
| 00129F00 | a00_09 f483 | BM | em_area00_low.c: em_area00_low_00129F00 | inside 00129FC0 (em_area00_low; em_area01_live case) | test_area00_low_reference |
| 001EFE00 | a00_09 f483 | asm | em_area01_side.c: em_area01_side_001EFE00; em_area11_bindings.c: cable_001EFE00 | case in em_aim_fire_world_live, em_area01_runtime | test_area00_fx_reference, test_area01_math_reference |
| 001F6FB0 | a00_09 f483 | asm (no C) | em_area00_fx_glow.c: em_area00_fx_001F6FB0 | **unbound: stop S2** | test_area00_fx_reference |
| 0012D240 | a00_09 f705 | NM | em_area00_low.c: em_area00_low_0012D240 | case in em_area01_live | test_area00_low_reference |
| 0012DE90 | a00_09 f705 | NM | em_area00_low.c: em_area00_low_0012DE90 | inside 0012D240 (em_area00_low; em_area01_live case) | test_area00_low_reference, test_area01_revisit_reference |
| 001D8100 | a00_09 f755 | BM | em_effect_original.c: em_effect_original_001D8100 | em_effect_original (effect node) | test_effect_original_reference |
| 001EF510 | a00_09 f755 | NM | em_area00_fx_spawn.c: em_area00_fx_001EF510 | **unbound: stop S3** | test_area00_fx_reference |
| 001DEDB0 | a00_09 f756 | asm | em_area00_hud.c: em_area00_hud_001DEDB0 | inside 001DEE80 / 001DEEC0 (stop S4) | test_area00_hud_reference, test_render_context_reference |
| 001DEE80 | a00_09 f756 | asm | em_area00_hud.c: em_area00_hud_001DEE80 | from 001E7310 (stop S4) | test_area00_hud_reference, test_render_context_reference |
| 001DEEC0 | a00_09 f756 | asm | em_area00_hud.c: em_area00_hud_001DEEC0 | from 001E7310 (stop S4) | test_area00_hud_reference, test_render_context_reference |
| 001DF180 | a00_09 f756 | NM | em_area00_hud.c: em_area00_hud_001DF180 | inside 001DF5A0 (stop S4) | test_area00_hud_reference |
| 001DF5A0 | a00_09 f756 | C (asm-linked) | em_area00_hud.c: em_area00_hud_001DF5A0 | from 001E7310 (stop S4) | test_area00_hud_reference |
| 001E7310 | a00_09 f756 | BM | em_area00_low.c: em_area00_low_001E7310 | **unbound: stop S4** | test_area00_low_reference |
| 001F0060 | a00_09 f756 | BM | em_area00_fx_spawn.c: em_area00_fx_001F0060; em_player_stage_live.c: stub_001F0060 | **stub in em_player_stage_live: stop S5** | test_area00_fx_reference, test_player_stage_workers_reference |
| 0021C270 | a00_09 f756 | BM | em_player_closure_live.c: w_0021C270; em_player_reaction.c: w0021C270 | em_player_closure_live w_0021C270 | test_player_fall_reference, test_player_ladder_climb_reference |
| 0021D4E0 | a00_09 f756 | BM | em_player_stage_workers.c: effect_0021D4E0; em_player_stage_workers.c: em_player_0021D4E0 | em_player_stage_workers effect_0021D4E0 | test_player_stage_workers_reference |
| 0021F850 | a00_09 f756 | BM | em_player_reaction.c: em_player_reaction_0021F850 | player reaction 0x0C (em_player_closure_live) | test_player_reaction_reference |
| 001D6DD0 | a00_09 f757 | asm | em_area00_fx_gs.c: em_area00_fx_001D6DD0 | from 001DF020 (stop S6) | test_area00_fx_reference |
| 001D7000 | a00_09 f757 | asm | em_static_world.c: em_static_world_001D7000 | from 001DF020 (stop S6) | test_area00_fx_existing_reference, test_static_world_reference |
| 001D7510 | a00_09 f757 | NM | em_area00_fx_gs.c: em_area00_fx_001D7510 | from 001DF020 (stop S6) | test_area00_fx_reference |
| 001D7A80 | a00_09 f757 | NM | em_area00_fx_gs.c: em_area00_fx_001D7A80 | from 001DF020 (stop S6) | test_area00_fx_reference |
| 001DF020 | a00_09 f757 | BM | em_area00_hud.c: em_area00_hud_001DF020 | inside 001DF110 (stop S6) | test_area00_hud_reference |
| 001DF110 | a00_09 f757 | BM | em_area00_hud.c: em_area00_hud_001DF110 | **unbound_u in em_render_context_live: stop S6** | test_area00_hud_reference, test_render_context_reference |
| 00193D90 | a00_09 f905 | asm | em_camera_leftovers.c: em_camleft_00193D90; em_camera_live.c: sp_00193D90 | em_camera_live sp_00193D90 | test_camera_area11_specials_reference, test_camera_leftovers_reference |
| 001B0C00 | a00_10 f675 | BM | em_area11_script_host.c: w_001B0C00; em_script_host_workers.c: em_script_host_001B0C00 | case in em_area01_transition_services | test_area01_sys_reference, test_area_script_reference |
| 001B0C60 | a00_10 f675 | BM | em_area11_bindings.c: fan_001B0C60; em_scene_bindings.c: em_scene_request_area_change_001B0C60 | case in em_area01_live | test_area00_overlay_reference, test_area_load_reference |
| 001FAD70 | a00_10 f675 | BM | em_area11_script_host.c: shw_001FAD70; em_scene_bindings.c: ac_001FAD70 | case in em_area01_live | test_area01_sys_reference, test_iop_stream_reference |
| 00159B90 | a00_10 f973 | NM | em_area01_sys.c: em_area01_sys_00159B90 | case in em_scene_bindings | test_area01_sys_reference, test_area01_terminal_progress_reference |
| 001BFFD0 | a00_10 f973 | NM | em_area01_math_owner.c: em_area01_math_001BFFD0 | case in em_scene_bindings | test_area01_math_reference, test_level8_port_reference |
| 001C0004 | a00_10 f973 | piece | em_area01_math_owner.c (second piece of 001BFFD0) | with 001BFFD0 | test_area01_math_reference |
| 001C02E0 | a00_10 f973 | NM | em_area01_math_owner.c: em_area01_math_001C02E0 | case in em_scene_bindings | test_area01_exploration_harness, test_area01_frame_counter_view |
| 001C4FA0 | a00_10 f973 | BM | em_area01_light_owner.c: em_area01_light_001C4FA0 | case in em_area01_runtime | test_area01_light_owner_reference, test_scene_no_shadow |
| 001C5050 | a00_10 f973 | asm | em_area00_world.c: em_area00_world_001C5050 | case in em_area01_light_live | test_area00_world_reference |
| 001C50B0 | a00_10 f973 | BM | em_area01_light_owner.c: em_area01_light_001C50B0 | case in em_area01_runtime, em_scene_bindings | test_area00_fx_reference, test_area01_light_owner_reference |
| 001D0C80 | a00_10 f973 | BM | em_area01_math_owner.c: em_area01_math_001D0C80 | em_area01_math_owner (exit owner) | test_area01_math_reference, test_area04_port_reference |
| 001D0D40 | a00_10 f973 | BM | em_area01_math_owner.c: em_area01_math_001D0D40 | em_area01_math_owner (exit owner) | test_area01_math_reference, test_area04_port_reference |
| 001D4FC0 | a00_10 f973 | NM | em_area01_render_vif.c: body_001D4FC0; em_area01_render_vif.c: em_area01_render_001D4FC0 | em_area01_render_vif (exit owner) | test_area01_render_reference |
| 001D5A70 | a00_10 f973 | asm | em_area01_render_vif.c: body_001D5A70; em_area01_render_vif.c: em_area01_render_001D5A70 | em_area01_render_vif (exit owner) | test_area01_render_reference |
| 001D5BD0 | a00_10 f973 | BM | em_area01_render_vif.c: em_area01_render_001D5BD0 | em_area01_render_vif (exit owner) | test_area01_render_reference, test_render_context_reference |
| 001F5490 | a00_10 f973 | BM | em_area00_fx_exit.c: em_area00_fx_001F5490 | case in em_area01_light_live | test_area00_fx_reference |
| 00102870 | a00_10 f974 | asm | em_area00_low.c: em_area00_low_00102870 | case in em_aim_fire_runtime | test_area00_low_reference |
| 00157CE0 | a00_10 f974 | BM | em_area01_sys.c: em_area01_sys_00157CE0 | em_area01_sys (exit owner) | test_area01_sys_reference |
| 00158590 | a00_10 f974 | NM | em_area01_sys.c: em_area01_sys_00158590 | em_area01_sys (exit owner) | test_area01_render_existing_reference, test_area01_sys_reference |
| 001BF630 | a00_10 f974 | asm | em_area01_math_owner.c: em_area01_math_001BF630 | em_area01_math_owner (exit owner) | test_area01_math_reference, test_level8_port_reference |
| 001CB360 | a00_10 f974 | BM | em_area01_math_owner.c: em_area01_math_001CB360; em_owner_draw_live.c: em_owner_draw_live_001CB360 | case in em_area01_live, em_area01_model_draw | test_area01_math_reference |
| 001E2800 | a00_10 f974 | NM | em_area00_hud.c: em_area00_hud_001E2800 | inside 001E2BA0 (em_area00_hud; em_aim_fire_render_live case) | test_area00_hud_reference |
| 001E2BA0 | a00_10 f974 | NM | em_area00_hud.c: em_area00_hud_001E2BA0 | case in em_aim_fire_render_live, em_aim_fire_runtime | test_area00_hud_reference, test_area01_revisit_reference |
| 001F4CC0 | a00_10 f974 | BM | em_area01_render_gs.c: em_area01_render_001F4CC0 | case in em_area01_flame_services | test_area01_render_reference |
| 001F5F60 | a00_10 f974 | NM | em_area00_fx_exit.c: em_area00_fx_001F5F60 | case in em_area01_light_live | test_area00_fx_reference |
| 001B0CD0 | a00_10 f979 | BM | em_area00_world.c: em_area00_world_001B0CD0 | AREA01 overlay caller in the return arrival; em_area00_return w_001B0CD0 (module unwired): stop S7 | test_area00_return_reference, test_area00_world_reference |
| 001C63D0 | a00_10 f979 | BM | em_area00_world.c: em_area00_world_001C63D0 | case in em_aim_fire_world_live | test_area00_fx_reference, test_area00_world_reference |
| 001CD940 | a00_10 f979 | NM | em_area00_fx_gs.c: em_area00_fx_001CD940 | inside 001F4F90 (em_area00_fx; under 001F5040 row) | test_area00_fx_reference |
| 001F4F40 | a00_10 f979 | C (asm-linked) | em_area00_fx_exit.c: em_area00_fx_001F4F40 | case in em_aim_fire_live | test_area00_fx_reference |
| 001F4F90 | a00_10 f979 | BM | em_area00_fx_exit.c: em_area00_fx_001F4F90 | inside 001F5040 (em_area00_fx; k_bindings row) | test_area00_fx_reference |
| 001F5040 | a00_10 f979 | NM | em_area00_fx_exit.c: em_area00_fx_001F5040 | case in em_aim_fire_world_live, em_area11_bindings | test_area00_fx_reference, test_area01_exploration |
| 001EBBB0 | a00_10 f980 | NM | em_effect_kinds.c: em_effect_kinds_001EBBB0; em_area00_hud.c: 001EBBB0 | em_effect_kinds_translates (em_effects_live w_handler) | test_effect_kinds_reference |
| 001D5170 | a00_10 f1060 | NM | em_area01_render_vif.c: body_001D5170; em_area01_render_vif.c: em_area01_render_001D5170 | em_area01_render_vif (exit owner) | test_area01_render_reference |

## 4. Reproduce

```sh
# native arm64 macOS, from the port checkout; reads the decomp's census and
# grep-checks src/game (no emulator, no build)
python3 - <<'PY'
import json
d = json.load(open('../Extermination/build/s87/census/a00_delta.json'))
beats = {'a00_04_cage_terminal', 'a00_05_ferry_deck', 'a00_06_cab_roof',
         'a00_07_duct_to_ne_room', 'a00_08_switch', 'a00_09_ne_room_out',
         'a00_10_progression_exit', 'a00_s0_shaft_door_back'}
fs = [f for f in d['functions'] if f['first_beat'] in beats]
print(len(fs), 'functions first run in a00_04..a00_10 / a00_s0')
PY
```

Then check each address against function definitions in `src/game` and
against LEVEL_READINESS.md's native-boundary rows (section 1).
