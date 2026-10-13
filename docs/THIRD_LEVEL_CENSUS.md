# AREA00 first-visit census and binding inventory

Level-3 baseline, 2026-10-13, branch `level3`, port base `d86f73c`. This ledger separates existing translation evidence from AREA00 integration evidence. The implementation and smoke results belong in [LEVEL3_BINDING.md](LEVEL3_BINDING.md).

**Baseline:** **0 AREA00-route live, 141 verified-unbound census entries (19,012 instructions), 0 missing translations.** “Verified-unbound” here includes owners already installed for other levels: no row is promoted until a native AREA00 phase exercises that owner and compares its behavior with the selected original recording. The standalone oracle results below are historical evidence, not checks rerun for this inventory.

## 1. Scope and counting

The retained source is `../Extermination/build/s87/census/a00_delta.json`, passes A00 and A01, described in [THIRD_LEVEL_ROUTE.md](THIRD_LEVEL_ROUTE.md) §9. Arrival through the twelve AREA00 beats executed 1,270 census entries: 1,257 boot and 13 AREA00 overlay entries. Of the boot entries, 1,129 were already seen: 1,041 first-level, 58 first-level exit, and 30 AREA01. The delta is **141 entries, 76,048 bytes / 19,012 instructions: 128 boot and 13 overlay**.

| Original route group | Entries | Instructions | Boot | AREA00 overlay |
|---|---:|---:|---:|---:|
| Main, including AREA00 arrival | 128 | 17,678 | 115 | 13 |
| Side-only, ordinary shaft-door return | 0 | 0 | 0 | 0 |
| Exit-only, progression return to AREA01 | 13 | 1,334 | 13 | 0 |

Groups are exclusive in main/side/exit priority order. The side route has no new entry; this does not mean its return is tested or requires no binding. The 13 exit rows occur only after a00_10's area change, on the AREA01 arrival with D_0081075E set. Nine overlay hits while another overlay was resident are excluded; the source has no unattributed hits and no missing segments.

Keys are `(region, runtime address)`. The overlay's splat label is runtime minus 0x40; AREA00 00823580 is not AREA01 or AREA11 00823580. **001C2FF0 is an interior census piece of 001C2770**, not a missing function: the complete owner is `em_area01_math_001C2770`. Its 638 instructions stay in this inventory to preserve the source count. These totals count census entries, not independent C bodies.

`a00_arrival.json` contains 19 boot rows; adding the six arrival overlay rows in the delta gives 25 arrival entries (4,020 instructions). They are already part of the 141 and must not be added twice. The 1,129 reused boot entries outside this delta still require a correct live composition; this document is not a claim to audit every transitive worker.

## 2. Status and evidence rules

`live` means exercised and compared on AREA00's native route (including its recorded AREA01 return); `verified-unbound` means an original-instruction-tested translation exists but that route proof is absent; `unverified` means only implementation is available; `stand-in` means a substitute is selected; `missing` means no translation was found; `boundary` means a named native platform service replaces the original mechanism. Byte-identical decomp C alone does not prove a native adapter or its memory views.

At the base revision `em_scene_bindings.c::area_read` accepts AREA11 sub 0 and AREA01 sub 0 only. `world_scene()` likewise recognizes those two scenes. Thus the AREA00 load cannot complete into a native world at this baseline. Installing an owner, admitting the load, or running a standalone test alone cannot promote an inventory row.

The preparation documents' 2026-09-28 claims that all modules are unbuilt or unwired are historical. The current Makefile builds LOW, WORLD, HUD and most FX modules. Actual callers include `em_aim_fire_world_live.c`, `em_aim_fire_runtime.c`, `em_aim_fire_render_live.c`, `em_effects_live.c`, `em_camera_live.c`, `em_area01_light_live.c`, `em_area01_live.c`, `em_area01_script_workers.c` and `em_area11_boxes.c`. These provide owners/adapters to reuse, not AREA00 promotion evidence. The AREA00 overlay and FX glow module are not in the base executable's source list.

Likewise AREA00_ASSETS finding 2 is historical: the current `em_message_live.c::load_emmd` accepts area 0 (the limit check is `area >= AREA_TABLES`). LEVEL3_BINDING records remaining loader constraints. AREA00_OVERVIEW's arrival-only route columns and its old overlay decomp statuses must not be treated as the current census: decomp HANDOFF reports all 33 non-pad AREA00 functions plus the entry pad linked from C and byte-identical.

The level-3 prerequisite adds D_0081075B/75C and D_008107DA..DC to the existing canonical `EmProgress` ranges, without adding storage or resets. Door 00825170 writes event 3 and counter 3; terminal 00825480 sets counter-4 bit 0. Original script 001BA080 writes event 4 at record 0x829A20 and counter 4 at 0x82A060 / 0x82A4A0 (2 / 0). The arrival's counter 2 is also required: script 0x828D60 writes 1 at 0x828EE0, while shaft-door script 0x8284E0 waits for 1 at its entry and writes 2 at 0x8286A0. These are the existing op06 sub 3 byte store and sub 4 whole-word equality wait. Current C definitions and local EMSC script records establish those writers; `test_scene_no_shadow.py` lists their reachers. This permits existing owner views to bind and promotes no census row.

Additional prerequisites now have local evidence, separate from the route ledger:

- `em_area00_state.c` borrows the loader's AREA00 data/BSS and the existing six boot-global words. Its 00824E00 initializer reproduces the six original stores; it does not own the common 001E7780 reset or record clears. `build/level3/state-test.log` reports the quick original-instruction check passing: two of ten captures, four initializer cases, 10,942,560 compared bytes, including shared-memory/lifetime/bounds/fault checks. 00824E00 is outside the 13 observed overlay census entries, so this does not add a row or promote another owner.
- `export_area00_world_textures.py` exported both subareas from disc sources and checked every exported texture against the retained fork GS freezes: 579 textures each, nine sub-0 and two sub-1 freezes (`build/level3/texture-export.log`). These are resource checks, not a native drawing comparison.
- `export_area00_shadow_receivers.py` exported both receiver banks and checked captured bytes: sub 0 has 1,100 objects / 1,802 blocks over nine captures (34,553,376 compared bytes); sub 1 has 1,099 objects / 1,783 blocks over two captures (7,599,360 bytes), recorded in `build/level3/shadow-export.log`.

The exports and initializer are prerequisites; their existence does not prove the loader selects them, the world binds them, or AREA00 frames execute. LEVEL3_BINDING records the current live integration boundary.

## 3. Existing owners and historical verification

The table below names existing definitions, including macro-generated public wrappers; filenames are relative to `src/game/`. Bind these owners and their original workers. Do not add parallel implementations. Every external call in the standalone harnesses has a documented execute/replay/script policy; a passing owner test does not certify stubbed callees, live state aliasing, route call order, or pixels.

| Key | Original-instruction harness / evidence |
|---|---|
| O | `test_area00_overlay_reference.py`; AREA00_OVERLAY_PORT §3: 13 entries, 1,090 default cases, 638 fail-stop/hook-contract runs; full 1,938 cases (2026-09-28). |
| L | `test_area00_low_reference.py`; AREA00_LOW §4: 31 entries, default 341 cases plus side-effect/store-site variants; full branch accounting and API checks. |
| W | `test_area00_world_reference.py`; AREA00_WORLD §4: 15 entries, full 1,033 cases and 4,005 variants; default 271 cases and 145 variants. |
| H | `test_area00_hud_reference.py`; AREA00_HUD §4: 19 entries, default 146 cases and 10 variants; 17,341 compared calls. |
| F | `test_area00_fx_reference.py`; AREA00_FX §6: 28 entries, default 176 cases / 4,843 callee entries, full 1,564 cases / 39,618 entries. |
| E | `test_area00_fx_existing_reference.py`; AREA00_FX §§4,6: reuse checks for 0015B030, 001639E0, 001D80E0, 001D7000, 001EFEB0 on AREA00 images. |
| X | `test_area01_exita_reference.py`; AREA00_LOW §§1,7 and AREA01_EXITA: seven reused entries, including the AREA00 arrival's actor workers. |
| Y | `test_area01_exitb_reference.py`; AREA00_WORLD §4.7 and AREA00_HUD §4: arrival owners; HUD adds 24 001E9280 cases on AREA00 images. |
| B | `test_player_weapon_states_b_reference.py`; AREA00_WORLD §4.7 / PLAYER_WEAPON_STATES_B: light/heavy melee owners. |
| R | `test_player_reaction_reference.py`; AREA00_WORLD §4.7 / PLAYER_REACTION: reaction owners. |
| S | `test_player_stage_workers_reference.py`; AREA00_WORLD §4.7 / PLAYER_STAGE_WORKERS. |
| C | `test_actor_collision_reference.py`; AREA00_WORLD §4.7 / ACTOR_COLLISION: 001A58B0 is the existing `column_ngon` path. |
| G | `test_coll_grid_hull_reference.py`; AREA00_WORLD §4.7 / COLL_GRID_HULL: existing 001A7280. |
| M | `test_area01_math_reference.py`; AREA00_WORLD §1.1 / AREA01_MATH: the complete 001C2770 covers 001C2FF0. |
| K | `test_camera_leftovers_reference.py`; AREA00_LOW §1 / CAMERA_LEFTOVERS: existing 00193D90 (`orbit` cases). |

The preparation lanes supplied 106 translations and reused 35 existing entries. All 141 have an owner; that is a translation-availability statement. Native route results and fresh test receipts must be recorded separately before changing status.

## 4. Exact inventory

`M` = main; `X` = progression-return exit. `A07` means the AREA00 arrival within `a01_07_level_exit`; `00`..`10` mean the corresponding `a00` beat. **First-hit frames are the legacy census replay's frames**, not fork recording rows and not native frames. All rows retain `verified-unbound` at this baseline.

| Runtime | Region | Instructions | Group; first hit | AREA00 status | Existing owner / symbol | Evidence |
|---|---|---:|---|---|---|---|
| 0x001000C0 | boot | 8 | M; 03 f101 | verified-unbound | `em_area00_low.c` / `em_area00_low_001000C0` | L |
| 0x00102870 | boot | 8 | X; 10 f974 | verified-unbound | `em_area00_low.c` / `em_area00_low_00102870` | L |
| 0x00102990 | boot | 4 | M; 03 f101 | verified-unbound | `em_area00_low.c` / `em_area00_low_00102990` | L |
| 0x00113478 | boot | 45 | M; A07 f3468 | verified-unbound | `em_area01_exita.c` / `em_area01_exita_00113478` | X |
| 0x001181B0 | boot | 153 | M; 04 f1269 | verified-unbound | `em_area00_low.c` / `em_area00_low_001181B0` | L |
| 0x00119080 | boot | 13 | M; 04 f1269 | verified-unbound | `em_area00_low.c` / `em_area00_low_00119080` | L |
| 0x00128600 | boot | 16 | M; A07 f4487 | verified-unbound | `em_area01_exita.c` / `em_area01_exita_00128600` | X |
| 0x00128640 | boot | 105 | M; A07 f4486 | verified-unbound | `em_area01_exita.c` / `em_area01_exita_00128640` | X |
| 0x00128830 | boot | 38 | M; 02 f486 | verified-unbound | `em_area00_low.c` / `em_area00_low_00128830` | L |
| 0x001288D0 | boot | 57 | M; 02 f485 | verified-unbound | `em_area00_low.c` / `em_area00_low_001288D0` | L |
| 0x00129F00 | boot | 46 | M; 09 f483 | verified-unbound | `em_area00_low.c` / `em_area00_low_00129F00` | L |
| 0x00129FC0 | boot | 385 | M; 09 f398 | verified-unbound | `em_area00_low.c` / `em_area00_low_00129FC0` | L |
| 0x0012A5D0 | boot | 508 | M; A07 f531 | verified-unbound | `em_area01_exita.c` / `em_area01_exita_0012A5D0` | X |
| 0x0012ADC0 | boot | 127 | M; A07 f4488 | verified-unbound | `em_area01_exita.c` / `em_area01_exita_0012ADC0` | X |
| 0x0012AFC0 | boot | 273 | M; A07 f4487 | verified-unbound | `em_area01_exita.c` / `em_area01_exita_0012AFC0` | X |
| 0x0012B410 | boot | 269 | M; 01 f176 | verified-unbound | `em_area00_low.c` / `em_area00_low_0012B410` | L |
| 0x0012B970 | boot | 297 | M; 05 f1 | verified-unbound | `em_area00_low.c` / `em_area00_low_0012B970` | L |
| 0x0012BE20 | boot | 411 | M; 02 f485 | verified-unbound | `em_area00_low.c` / `em_area00_low_0012BE20` | L |
| 0x0012C490 | boot | 385 | M; 03 f72 | verified-unbound | `em_area00_low.c` / `em_area00_low_0012C490` | L |
| 0x0012CAA0 | boot | 488 | M; 09 f424 | verified-unbound | `em_area00_low.c` / `em_area00_low_0012CAA0` | L |
| 0x0012D240 | boot | 207 | M; 09 f705 | verified-unbound | `em_area00_low.c` / `em_area00_low_0012D240` | L |
| 0x0012D580 | boot | 177 | M; 03 f73 | verified-unbound | `em_area00_low.c` / `em_area00_low_0012D580` | L |
| 0x0012D850 | boot | 58 | M; 09 f193 | verified-unbound | `em_area00_low.c` / `em_area00_low_0012D850` | L |
| 0x0012DE90 | boot | 120 | M; 09 f705 | verified-unbound | `em_area00_low.c` / `em_area00_low_0012DE90` | L |
| 0x0012E070 | boot | 15 | M; 09 f397 | verified-unbound | `em_area00_low.c` / `em_area00_low_0012E070` | L |
| 0x0012E0B0 | boot | 105 | M; 04 f104 | verified-unbound | `em_area00_low.c` / `em_area00_low_0012E0B0` | L |
| 0x0012E260 | boot | 21 | M; 04 f104 | verified-unbound | `em_area00_low.c` / `em_area00_low_0012E260` | L |
| 0x0012E2C0 | boot | 56 | M; 02 f487 | verified-unbound | `em_area00_low.c` / `em_area00_low_0012E2C0` | L |
| 0x00156F30 | boot | 268 | M; A07 f531 | verified-unbound | `em_area01_exitb.c` / `em_area01_exitb_00156F30` | Y |
| 0x001576E0 | boot | 96 | M; A07 f532 | verified-unbound | `em_area01_exitb.c` / `em_area01_exitb_001576E0` | Y |
| 0x001581A0 | boot | 79 | M; A07 f531 | verified-unbound | `em_area01_exitb.c` / `em_area01_exitb_001581A0` | Y |
| 0x00158810 | boot | 237 | M; A07 f531 | verified-unbound | `em_area01_exitb.c` / `em_area01_exitb_00158810` | Y |
| 0x00158BD0 | boot | 85 | M; A07 f531 | verified-unbound | `em_area01_exitb.c` / `em_area01_exitb_00158BD0` | Y |
| 0x0015AB00 | boot | 61 | M; A07 f531 | verified-unbound | `em_area01_exitb.c` / `em_area01_exitb_0015AB00` | Y |
| 0x0015B030 | boot | 62 | M; A07 f531 | verified-unbound | `em_area01_exitb.c` / `em_area01_exitb_0015B030` | Y; E |
| 0x001639E0 | boot | 88 | M; 07 f1120 | verified-unbound | `em_player_fall.c` / `em_player_fall_state7` | E |
| 0x001735C0 | boot | 516 | M; 03 f89 | verified-unbound | `em_player_weapon_states_b.c` / `em_player_weapon_b_state21` | B |
| 0x00173DD0 | boot | 35 | M; 09 f388 | verified-unbound | `em_player_weapon_states_b.c` / `em_player_weapon_b_00173DD0` | B |
| 0x00173E60 | boot | 218 | M; 09 f386 | verified-unbound | `em_player_weapon_states_b.c` / `em_player_weapon_b_state22` | B |
| 0x001754E0 | boot | 52 | M; 09 f426 | verified-unbound | `em_player_reaction.c` / `em_player_reaction_001754E0` | R |
| 0x00189EC0 | boot | 70 | M; 03 f101 | verified-unbound | `em_area00_world.c` / `em_area00_world_00189EC0` | W |
| 0x00189FE0 | boot | 104 | M; 03 f101 | verified-unbound | `em_area00_world.c` / `em_area00_world_00189FE0` | W |
| 0x0018A180 | boot | 27 | M; 03 f103 | verified-unbound | `em_area00_world.c` / `em_area00_world_0018A180` | W |
| 0x00193D90 | boot | 70 | M; 09 f905 | verified-unbound | `em_camera_leftovers.c` / `em_camleft_00193D90` | K |
| 0x00198CE0 | boot | 41 | M; 07 f889 | verified-unbound | `em_area00_low.c` / `em_area00_low_00198CE0` | L |
| 0x00198F10 | boot | 194 | M; 07 f1036 | verified-unbound | `em_area00_low.c` / `em_area00_low_00198F10` | L |
| 0x0019AA80 | boot | 39 | M; 02 f589 | verified-unbound | `em_area00_world.c` / `em_area00_world_0019AA80` | W |
| 0x0019B2C0 | boot | 128 | M; 03 f101 | verified-unbound | `em_area00_world.c` / `em_area00_world_0019B2C0` | W |
| 0x0019C6F0 | boot | 80 | M; 08 f456 | verified-unbound | `em_area00_world.c` / `em_area00_world_0019C6F0` | W |
| 0x001A58B0 | boot | 221 | M; 05 f1057 | verified-unbound | `em_actor_collision.c` / `column_ngon` | C |
| 0x001A7280 | boot | 380 | M; 02 f589 | verified-unbound | `em_coll_grid_hull.c` / `em_coll_grid_hull_001A7280` | G |
| 0x001AA840 | boot | 33 | M; 03 f101 | verified-unbound | `em_area00_fx_spawn.c` / `em_area00_fx_001AA840` | F |
| 0x001B0CD0 | boot | 41 | X; 10 f979 | verified-unbound | `em_area00_world.c` / `em_area00_world_001B0CD0` | W |
| 0x001B41F0 | boot | 391 | M; 09 f396 | verified-unbound | `em_area00_world.c` / `em_area00_world_001B41F0` | W |
| 0x001B5360 | boot | 160 | M; 02 f577 | verified-unbound | `em_area00_low.c` / `em_area00_low_001B5360` | L |
| 0x001B7670 | boot | 24 | M; 04 f992 | verified-unbound | `em_area00_low.c` / `em_area00_low_001B7670` | L |
| 0x001B7700 | boot | 78 | M; 08 f360 | verified-unbound | `em_area00_low.c` / `em_area00_low_001B7700` | L |
| 0x001B8AB0 | boot | 321 | M; 04 f1542 | verified-unbound | `em_area00_low.c` / `em_area00_low_001B8AB0` | L |
| 0x001C2430 | boot | 37 | M; A07 f532 | verified-unbound | `em_area01_exitb.c` / `em_area01_exitb_001C2430` | Y |
| 0x001C24D0 | boot | 26 | M; 02 f485 | verified-unbound | `em_area00_world.c` / `em_area00_world_001C24D0` | W |
| 0x001C2FF0 | boot | 638 | M; 02 f593 | verified-unbound | `em_area01_math_actor.c` / `em_area01_math_001C2770 (interior)` | M |
| 0x001C5050 | boot | 22 | X; 10 f973 | verified-unbound | `em_area00_world.c` / `em_area00_world_001C5050` | W |
| 0x001C6160 | boot | 11 | M; A07 f755 | verified-unbound | `em_area01_exitb.c` / `em_area01_exitb_001C6160` | Y |
| 0x001C6200 | boot | 46 | M; 09 f190 | verified-unbound | `em_area00_world.c` / `em_area00_world_001C6200` | W |
| 0x001C63D0 | boot | 4 | X; 10 f979 | verified-unbound | `em_area00_world.c` / `em_area00_world_001C63D0` | W |
| 0x001CA3B0 | boot | 70 | M; 09 f190 | verified-unbound | `em_area00_world.c` / `em_area00_world_001CA3B0` | W |
| 0x001CA4D0 | boot | 65 | M; 09 f191 | verified-unbound | `em_area00_world.c` / `em_area00_world_001CA4D0` | W |
| 0x001CD940 | boot | 288 | X; 10 f979 | verified-unbound | `em_area00_fx_exit.c` / `em_area00_fx_001CD940` | F |
| 0x001D0400 | boot | 43 | M; A07 f531 | verified-unbound | `em_area01_exitb.c` / `em_area01_exitb_001D0400` | Y |
| 0x001D6DD0 | boot | 36 | M; 09 f757 | verified-unbound | `em_area00_fx_gs.c` / `em_area00_fx_001D6DD0` | F |
| 0x001D7000 | boot | 31 | M; 09 f757 | verified-unbound | `em_static_world.c` / `em_static_world_001D7000` | E |
| 0x001D7510 | boot | 52 | M; 09 f757 | verified-unbound | `em_area00_fx_gs.c` / `em_area00_fx_001D7510` | F |
| 0x001D7A80 | boot | 43 | M; 09 f757 | verified-unbound | `em_area00_fx_gs.c` / `em_area00_fx_001D7A80` | F |
| 0x001D80E0 | boot | 8 | M; 03 f101 | verified-unbound | `em_effect_original.c` / `em_effect_original_001D80E0` | E |
| 0x001DEDB0 | boot | 10 | M; 09 f756 | verified-unbound | `em_area00_hud.c` / `em_area00_hud_001DEDB0` | H |
| 0x001DEE80 | boot | 13 | M; 09 f756 | verified-unbound | `em_area00_hud.c` / `em_area00_hud_001DEE80` | H |
| 0x001DEEC0 | boot | 8 | M; 09 f756 | verified-unbound | `em_area00_hud.c` / `em_area00_hud_001DEEC0` | H |
| 0x001DF020 | boot | 57 | M; 09 f757 | verified-unbound | `em_area00_hud.c` / `em_area00_hud_001DF020` | H |
| 0x001DF110 | boot | 25 | M; 09 f757 | verified-unbound | `em_area00_hud.c` / `em_area00_hud_001DF110` | H |
| 0x001DF180 | boot | 263 | M; 09 f756 | verified-unbound | `em_area00_hud.c` / `em_area00_hud_001DF180` | H |
| 0x001DF5A0 | boot | 24 | M; 09 f756 | verified-unbound | `em_area00_hud.c` / `em_area00_hud_001DF5A0` | H |
| 0x001E2800 | boot | 232 | X; 10 f974 | verified-unbound | `em_area00_hud.c` / `em_area00_hud_001E2800` | H |
| 0x001E2BA0 | boot | 182 | X; 10 f974 | verified-unbound | `em_area00_hud.c` / `em_area00_hud_001E2BA0` | H |
| 0x001E2E80 | boot | 486 | M; 02 f589 | verified-unbound | `em_area00_hud.c` / `em_area00_hud_001E2E80` | H |
| 0x001E7310 | boot | 74 | M; 09 f756 | verified-unbound | `em_area00_low.c` / `em_area00_low_001E7310` | L |
| 0x001E8E80 | boot | 253 | M; A07 f531 | verified-unbound | `em_area01_exitb.c` / `em_area01_exitb_001E8E80` | Y |
| 0x001E9280 | boot | 189 | M; A07 f532 | verified-unbound | `em_area01_exitb.c` / `em_area01_exitb_001E9280` | Y |
| 0x001EAB50 | boot | 103 | M; 03 f103 | verified-unbound | `em_area00_hud.c` / `em_area00_hud_001EAB50` | H |
| 0x001EB980 | boot | 39 | M; 00 f34 | verified-unbound | `em_area00_hud.c` / `em_area00_hud_001EB980` | H |
| 0x001EBBB0 | boot | 31 | X; 10 f980 | verified-unbound | `em_area00_hud.c` / `em_area00_hud_001EBBB0` | H |
| 0x001EBC30 | boot | 57 | M; 09 f190 | verified-unbound | `em_area00_hud.c` / `em_area00_hud_001EBC30` | H |
| 0x001ECB00 | boot | 218 | M; 03 f101 | verified-unbound | `em_area00_hud.c` / `em_area00_hud_001ECB00` | H |
| 0x001ECFB0 | boot | 83 | M; 09 f481 | verified-unbound | `em_area00_hud.c` / `em_area00_hud_001ECFB0` | H |
| 0x001ED7A0 | boot | 211 | M; 09 f396 | verified-unbound | `em_area00_hud.c` / `em_area00_hud_001ED7A0` | H |
| 0x001EEBA0 | boot | 195 | M; 02 f590 | verified-unbound | `em_area00_hud.c` / `em_area00_hud_001EEBA0` | H |
| 0x001EEEB0 | boot | 195 | M; 03 f40 | verified-unbound | `em_area00_hud.c` / `em_area00_hud_001EEEB0` | H |
| 0x001EF510 | boot | 155 | M; 09 f755 | verified-unbound | `em_area00_fx_spawn.c` / `em_area00_fx_001EF510` | F |
| 0x001EFEB0 | boot | 21 | M; 02 f589 | verified-unbound | `em_security_gun_rest.c` / `em_gun_rest_001EFEB0` | E |
| 0x001EFF10 | boot | 48 | M; 03 f101 | verified-unbound | `em_area00_fx_spawn.c` / `em_area00_fx_001EFF10` | F |
| 0x001EFFD0 | boot | 34 | M; 02 f589 | verified-unbound | `em_area00_fx_spawn.c` / `em_area00_fx_001EFFD0` | F |
| 0x001F0060 | boot | 15 | M; 09 f756 | verified-unbound | `em_area00_fx_spawn.c` / `em_area00_fx_001F0060` | F |
| 0x001F00A0 | boot | 31 | M; 03 f103 | verified-unbound | `em_area00_fx_spawn.c` / `em_area00_fx_001F00A0` | F |
| 0x001F02C0 | boot | 19 | M; 03 f39 | verified-unbound | `em_area00_fx_spawn.c` / `em_area00_fx_001F02C0` | F |
| 0x001F1550 | boot | 39 | M; 03 f101 | verified-unbound | `em_area00_fx_trail.c` / `em_area00_fx_001F1550` | F |
| 0x001F15F0 | boot | 180 | M; 03 f101 | verified-unbound | `em_area00_fx_trail.c` / `em_area00_fx_001F15F0` | F |
| 0x001F18C0 | boot | 120 | M; 03 f101 | verified-unbound | `em_area00_fx_trail.c` / `em_area00_fx_001F18C0` | F |
| 0x001F2BA0 | boot | 185 | M; 09 f190 | verified-unbound | `em_area00_fx_debris.c` / `em_area00_fx_001F2BA0` | F |
| 0x001F2E90 | boot | 61 | M; 09 f190 | verified-unbound | `em_area00_fx_debris.c` / `em_area00_fx_001F2E90` | F |
| 0x001F2F90 | boot | 235 | M; 09 f190 | verified-unbound | `em_area00_fx_debris.c` / `em_area00_fx_001F2F90` | F |
| 0x001F3340 | boot | 182 | M; 09 f191 | verified-unbound | `em_area00_fx_debris.c` / `em_area00_fx_001F3340` | F |
| 0x001F3620 | boot | 515 | M; 09 f191 | verified-unbound | `em_area00_fx_debris.c` / `em_area00_fx_001F3620` | F |
| 0x001F3E30 | boot | 91 | M; 09 f191 | verified-unbound | `em_area00_fx_debris.c` / `em_area00_fx_001F3E30` | F |
| 0x001F4A00 | boot | 2 | M; 02 f485 | verified-unbound | `em_area00_fx_gs.c` / `em_area00_fx_001F4A00` | F |
| 0x001F4E20 | boot | 5 | M; 07 f1035 | verified-unbound | `em_area00_fx_gs.c` / `em_area00_fx_001F4E20` | F |
| 0x001F4F40 | boot | 18 | X; 10 f979 | verified-unbound | `em_area00_fx_exit.c` / `em_area00_fx_001F4F40` | F |
| 0x001F4F90 | boot | 44 | X; 10 f979 | verified-unbound | `em_area00_fx_exit.c` / `em_area00_fx_001F4F90` | F |
| 0x001F5040 | boot | 276 | X; 10 f979 | verified-unbound | `em_area00_fx_exit.c` / `em_area00_fx_001F5040` | F |
| 0x001F5490 | boot | 17 | X; 10 f973 | verified-unbound | `em_area00_fx_exit.c` / `em_area00_fx_001F5490` | F |
| 0x001F5F60 | boot | 171 | X; 10 f974 | verified-unbound | `em_area00_fx_exit.c` / `em_area00_fx_001F5F60` | F |
| 0x001F6FB0 | boot | 512 | M; 09 f483 | verified-unbound | `em_area00_fx_glow.c` / `em_area00_fx_001F6FB0` | F |
| 0x001FC580 | boot | 87 | M; 02 f576 | verified-unbound | `em_area00_low.c` / `em_area00_low_001FC580` | L |
| 0x001FF030 | boot | 18 | M; 08 f363 | verified-unbound | `em_area00_low.c` / `em_area00_low_001FF030` | L |
| 0x0021BD60 | boot | 56 | M; 04 f104 | verified-unbound | `em_area00_world.c` / `em_area00_world_0021BD60` | W |
| 0x0021C270 | boot | 53 | M; 09 f756 | verified-unbound | `em_player_stage_workers.c` / `em_player_0021C270` | S |
| 0x0021D4E0 | boot | 18 | M; 09 f756 | verified-unbound | `em_player_stage_workers.c` / `em_player_0021D4E0` | S |
| 0x0021F330 | boot | 328 | M; 09 f425 | verified-unbound | `em_player_reaction.c` / `em_player_reaction_0021F330` | R |
| 0x0021F850 | boot | 187 | M; 09 f756 | verified-unbound | `em_player_reaction.c` / `em_player_reaction_0021F850` | R |
| 0x0022DCD0 | boot | 599 | M; A07 f531 | verified-unbound | `em_area01_exita.c` / `em_area01_exita_0022DCD0` | X |
| 0x00823580 | AREA00 | 165 | M; 00 f1 | verified-unbound | `em_area00_overlay.c` / `em_area00_ovl_00823580` | O |
| 0x00823820 | AREA00 | 268 | M; 08 f456 | verified-unbound | `em_area00_overlay.c` / `em_area00_ovl_00823820` | O |
| 0x00824EA0 | AREA00 | 180 | M; A07 f531 | verified-unbound | `em_area00_overlay.c` / `em_area00_ovl_00824EA0` | O |
| 0x00825170 | AREA00 | 156 | M; 00 f1 | verified-unbound | `em_area00_overlay.c` / `em_area00_ovl_00825170` | O |
| 0x008253E0 | AREA00 | 37 | M; 04 f831 | verified-unbound | `em_area00_overlay.c` / `em_area00_ovl_008253E0` | O |
| 0x00825480 | AREA00 | 93 | M; A07 f531 | verified-unbound | `em_area00_overlay.c` / `em_area00_ovl_00825480` | O |
| 0x00825600 | AREA00 | 198 | M; A07 f531 | verified-unbound | `em_area00_overlay.c` / `em_area00_ovl_00825600` | O |
| 0x00825920 | AREA00 | 215 | M; A07 f531 | verified-unbound | `em_area00_overlay.c` / `em_area00_ovl_00825920` | O |
| 0x00825C80 | AREA00 | 59 | M; 00 f1 | verified-unbound | `em_area00_overlay.c` / `em_area00_ovl_00825C80` | O |
| 0x008261E0 | AREA00 | 59 | M; A07 f532 | verified-unbound | `em_area00_overlay.c` / `em_area00_ovl_008261E0` | O |
| 0x008262D0 | AREA00 | 59 | M; 00 f1 | verified-unbound | `em_area00_overlay.c` / `em_area00_ovl_008262D0` | O |
| 0x008263C0 | AREA00 | 181 | M; A07 f532 | verified-unbound | `em_area00_overlay.c` / `em_area00_ovl_008263C0` | O |
| 0x008266A0 | AREA00 | 58 | M; 00 f1 | verified-unbound | `em_area00_overlay.c` / `em_area00_ovl_008266A0` | O |

## 5. Captures, limits, and promotion requirements

The function census above is the retained v2.6.3 A00 replay. No full AREA00 fork census is present; decomp PCSX2_FORK's re-recording inventory records only the first-level census smoke. The twelve fork route recordings themselves are present under `../Extermination/build/fork_refs/s87/route_a00/`, with the AREA00 arrival in `route_a01/a01_07_level_exit/`. `tools/reference_root.py::capture` selects these by default; `EM_REFERENCE=v263` explicitly selects `../Extermination/build/s87/route_a00/`. Do not infer a missing fork census or a missing fork fixture from the legacy files, and do not silently fall back.

The old census replays completed their closed-loop beat checks but did not reproduce every recorded row. THIRD_LEVEL_ROUTE §9 lists those mismatches (including a00_09's 2,280 replay frames against 2,027 recorded frames). Its function set is evidence for the same route shape, not tick-exact native coverage. The fork route has its own load lengths and phase data; compare on each recorded row and original logical load boundary, never by copying a census first-hit number into a fork phase.

A phase promotion needs the native call evidence plus the original comparison: arrival/rebuild and neutral world frames; movement and collision on every beat; camera; drawing; the ferry, cab, stack, crates and duct; switch and door [51]; and both shaft-door return cases. Store the first divergent row and original/native fields when a phase fails. A neutral-frame result cannot certify an interaction owner that never ran. Existing translation oracles do not compare a rendered framebuffer.

The source records only 13 of AREA00's 34 overlay functions. The other 21 are outside this measured route set; sub 2 is unplayed. Sub 1 is in scope after the a00_08 switch and during a00_09 (AREA00_ASSETS `loaded_sub_proof`, chunk04.n1). That resource change requires its own live load and state proof. Original uncertainties remain as recorded: the attacker in a00_09 and the causes of several creature taken bits were not identified; they cannot be replaced by guessed behavior.

## 6. Reproduce the inventory and evidence

Native arm64 macOS, in the `extermination-port-level3` worktree. The inventory was checked against the retained JSON, current C definitions/call sites and harness definitions; no emulator or heavy oracle suite was run to write it. Run suites serially on the shared machine; exhaustive variants require `EM_TEST_FULL=1`.

```sh
nice -n 15 python3 tools/test_area00_overlay_reference.py
nice -n 15 python3 tools/test_area00_low_reference.py
nice -n 15 python3 tools/test_area00_world_reference.py
nice -n 15 python3 tools/test_area00_hud_reference.py
nice -n 15 python3 tools/test_area00_fx_reference.py
nice -n 15 python3 tools/test_area00_fx_existing_reference.py
python3 tools/check_no_disassembly.py docs/THIRD_LEVEL_CENSUS.md
```

These commands are reproduction instructions, not a claim they passed on this branch. See LEVEL3_BINDING for executed checks and LEVEL_SMOKE for the AREA00 phase checker. Keep all local captures, RAM and binaries in ignored `build/`, `assets/` and `data/`.
