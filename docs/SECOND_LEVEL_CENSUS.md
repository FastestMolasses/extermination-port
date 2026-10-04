# AREA01 first-visit census and binding inventory

Phase-1 baseline and phase-2 prerequisites, 2026-10-03, branch `level2`, port `3d482f6`. This is an AREA01 binding ledger. It does not change the first-level census.

**Current (2026-10-04, after the level-2 integration merged into main):** **0 live, 177 verified-unbound, 0 missing, 2 boundary** (the five initially missing routines are translated; §2's table is the phase-1 baseline). The committed live composition runs only under `EM_LEVEL2_BINDING_PROBE=1` and completes no AREA01 world frame (it faults at the unbound worker 00102798 in the first world frame; LEVEL2_BINDING.md "State"), so no row is promoted. The last section, "AREA01 arrival binding dependencies", records original callers, existing owners and the canonical-state mapping.

## 1. Scope and counting

The retained local census is evidence of original execution, not native execution. The sources are:

- `../Extermination/build/s87/census/a01_delta.json`, `new_functions`: 174 entries, 116,132 bytes / 29,033 instructions. The sixteen beats run 982 entries (968 boot and 14 AREA01 overlay); 808 were already in the old first-level baseline. The new set is 89 main-line, 57 side-only (including the 20 room-only entries), and 28 exit/change/AREA00-only.
- `docs/FIRST_LEVEL_CENSUS.md` §3.26: 62 post-arrival entries, 11,154 instructions. Its source is `../Extermination/build/c10/exit/census_delta.json`, phase `area01_arrival`, after excluding 001C69A0 and 001CD070, which subsequent first-level captures classified separately. Both excluded entries remain in the a01 delta and therefore in this ledger.
- The union below is **179 census entries, 118,188 bytes / 29,547 instructions**: 174 + 62 − 57 overlaps. It has 164 boot entries and 15 AREA01 overlay entries. Five entries appear only in §3.26. The 28 exit-only entries are retained to preserve the source set; many execute after AREA00 arrival and are beyond this task's allowed stop.

`SECOND_LEVEL_ROUTE.md` §6 describes the first twelve beats (154 new entries); §9.7 adds twenty room entries and gives the current 174. The old baseline is the s87 startup/AREA11 census, not the current first-level port: new in this delta does not mean still missing in the port. The a01 exit consumer is f233 and placement rebuild is f530. Thirteen candidate overlay hits while AREA00 was resident were excluded by the original census. No unattributed hits remain.

Keys are `(region, runtime address)`. In particular AREA01 00823580 is the shaft door, not the AREA11 flame at the same address. Overlay splat names are runtime − 0x40. The ledger preserves the source's boot split: 001C0004 is the interior of 001BFFD0 and is handled by the same native owner, not a new function to implement. Its 181 instructions plus the 13 at 001BFFD0 sum to the complete 194-instruction body. Thus these are census-entry totals, not a claim of 179 independent C bodies.

## 2. Status rules and phase-1 baseline

`live` means bound and exercised on AREA01's first-visit path; `verified-unbound` means an original-instruction-checked native translation exists but AREA01 integration has not been proved; `unverified` means an available implementation lacks that verification; `stand-in` means a substitute is selected for the path; `missing` means no native translation was found; `boundary` means a named platform service replaces the original mechanism. Historical oracle evidence is distinguished from checks rerun here. A first-level live owner is reuse evidence, not an AREA01 promotion.

At this baseline `em_scene_bindings.c::w_001AD4D0` faults at 0x001AE040 whenever AREA01's frame state is nonzero. `em_area01_arrival_bind` records spawned nodes and installs no ticks. Therefore no post-arrival row is counted live, including shared code already exercised in AREA11. Opening that gate alone cannot promote any row.

| AREA01 status | Entries | Instructions |
|---|---:|---:|
| live | 0 | 0 |
| verified-unbound | 172 | 28,902 |
| unverified | 0 | 0 |
| stand-in | 0 | 0 |
| missing | 5 | 514 |
| boundary | 2 | 131 |

The shared column in §4 records first-level evidence only: `L` = 44 boot rows classified live in the current first-level ledger; `U` = its unverified MAP draw 001CB480; `V` = its verified-unbound 00225A00; `B` = the two 2D boundaries; `—` = no corresponding current first-level row. AREA01 00823580 never inherits the AREA11 row. This cross-reference is a classification check, not a rerun of all first-level evidence.

## 3. Verification and owner rules

Every file named in §4 is relative to `src/game/`. A translation named `em_area01_*` is the existing implementation to bind, not permission to add another implementation. Shared installed owners take precedence where explicitly named. 001AF7C0 and 001C4720 have pre-existing standalone AREA01 copies as well as a first-level owner; use the shared owner or consolidate with oracle evidence, never add a third. C69A0 is consolidated: the complete math entry and typed status view share its root and post-nlerp matrix stages (see LEVEL2_RENDER.md ("C69A0 live pose binding and shared status stages")); this worker proof does not promote its route status. 00225A00 also exists as a worker in startup/status modules; the generic ROOM implementation is available, but its AREA01 save-terminal adapter is not bound.

The evidence key maps to `tools/test_area01_<name>_reference.py` and the matching `docs/AREA01_<LANE>.md`. These Python oracles execute locally retained original code and captured RAM. They compare individual translations with external calls recorded/replayed or scripted as documented by each harness. They do not run a connected native world, prove all caller inputs, or compare rendered pixels. The lane docs' full sweeps and mutation claims were not rerun in this phase.

| Key | Harness name | Current quick-run evidence |
|---|---|---|
| O | overlay | PASS: 2,008 cases; all 14 entries; 636 fail-stop/hook-contract native runs on 26 cases |
| M | math | PASS: 3/12 beats; 42 actor-route, 29 actor-perturbation, 18 owner-route, 262 owner-perturbation, 609 script-op, 53 player, 26 001C25E0, 186 scripted 001C2770, 6,033/75,297 table cases. Nine reused helper entries rely on the separate historical oracles in AREA01_MATH §4; this run checks the lane's 20 complete translated bodies, including the 001C0004 piece. |
| R | render | PASS: 257 capture cases over 16 beats; 60 VIF unit, 123 GS unit, plus the boundary/alias/fault sets printed in the log |
| E | render_existing | PASS after dependency repair: 60 fall cases / 246 entry-checked calls / 28 of 30 branch outcomes; 360 hang, 102 decal, 170 glow cases |
| S | sys | PASS: 227/2,065 cases; 11 side-effect and 7 single-load variants; 14 further variants; 27,498 worker calls; 113 existing-owner cases |
| D | side | PASS: 270/775 cases, 90/1,110 effect cases, 535 calls, 116 changed fields, 10 branches both ways, 2,115 existing-translation checks |
| U | ui | PASS: 266 cases (one zero-divisor refusal on both sides), 4,228 calls and entry comparisons |
| X | exita | PASS: 184/656 cases, 40 side-effect cases / 1,100 scripted writes, 286 store-site variants, 6,223 calls, 63 callee policies, 7 API checks |
| Y | exitb | PASS: 147/467 cases, 52/2,499 variants, 5,196 calls, 41 changed fields, 108/108 branches both ways |
| Q | room | PASS: 180/1,202 cases, 60/1,166 effect cases, 636 calls, 526 changed fields, 19 branches both ways; 600 + 600 closure cases, 60 world/aim cases, 24 health, 16 arc, 8 healing-page cases. 001823E0's separate room check is full-only and was not rerun. |

All ten quick harnesses were run headlessly on native arm64 macOS with `EM_TEST_JOBS=2`. The first E run failed before decal comparison because its private link omitted `em_owner_services_original.c`; adding the same dependency as the existing effect oracle made the rerun pass. Receipts: `build/level2/oracles/area01_*.log`, `area01_render_existing_rerun.log`, and `area01_quick_summary.json` (the JSON retains the initial E failure; the rerun log supersedes that result). No emulator was launched. No full mode was run.

## 4. Current exact inventory

The rows include the phase-2 prerequisite promotions in §7; the phase-1 totals in §2 are preserved as the baseline.

`M`, `S`, `X` are the original delta's main, side-only and exit/change/AREA00-only groups. `A` is arrival-only. First-hit cells use the census replay, not the recording's frame number: `00` means `a01_00_train_room`, `s4` means `a01_s4_east_room`, etc.; `E c…` is the later EXIT census's `exit_01` counter. Both are retained when present. A row can first appear in the a01 exit group yet have an earlier post-arrival EXIT hit.

| Runtime | Region | Instructions | Group; first hit | AREA01 status | Existing owner / symbol | Evidence | Shared |
|---|---|---:|---|---|---|---|---|
| 0x00113478 | boot | 45 | X; 07 f3468 | verified-unbound | `em_area01_exita.c` / `em_area01_exita_00113478` | X | — |
| 0x001195A8 | boot | 42 | X; 07 f282 | verified-unbound | `em_area01_exita.c` / `em_area01_exita_001195A8` | X | — |
| 0x00128390 | boot | 13 | X; 07 f531; E c16503 | verified-unbound | `em_area01_exita.c` / `em_area01_exita_00128390` | X | — |
| 0x00128600 | boot | 16 | X; 07 f4487 | verified-unbound | `em_area01_exita.c` / `em_area01_exita_00128600` | X | — |
| 0x00128640 | boot | 105 | X; 07 f4486 | verified-unbound | `em_area01_exita.c` / `em_area01_exita_00128640` | X | — |
| 0x001287F0 | boot | 14 | M; 00 f1; E c16505 | verified-unbound | `em_area01_sys.c` / `em_area01_sys_001287F0` | S | — |
| 0x001289C0 | boot | 60 | X; 07 f531; E c16503 | verified-unbound | `em_area01_exita.c` / `em_area01_exita_001289C0` | X | — |
| 0x00128AB0 | boot | 51 | X; 07 f531; E c16503 | verified-unbound | `em_area01_exita.c` / `em_area01_exita_00128AB0` | X | — |
| 0x00128B80 | boot | 33 | M; 00 f1; E c16505 | verified-unbound | `em_area01_sys.c` / `em_area01_sys_00128B80` | S | — |
| 0x00128C10 | boot | 731 | M; 00 f1; E c16503 | verified-unbound | `em_area01_sys.c` / `em_area01_sys_00128C10` | S | — |
| 0x00129780 | boot | 479 | X; 07 f532; E c16504 | verified-unbound | `em_area01_exita.c` / `em_area01_exita_00129780` | X | — |
| 0x0012A5D0 | boot | 508 | X; 07 f531 | verified-unbound | `em_area01_exita.c` / `em_area01_exita_0012A5D0` | X | — |
| 0x0012ADC0 | boot | 127 | X; 07 f4488 | verified-unbound | `em_area01_exita.c` / `em_area01_exita_0012ADC0` | X | — |
| 0x0012AFC0 | boot | 273 | X; 07 f4487 | verified-unbound | `em_area01_exita.c` / `em_area01_exita_0012AFC0` | X | — |
| 0x00156F30 | boot | 268 | X; 07 f531 | verified-unbound | `em_area01_exitb.c` / `em_area01_exitb_00156F30` | Y | — |
| 0x001576E0 | boot | 96 | X; 07 f532 | verified-unbound | `em_area01_exitb.c` / `em_area01_exitb_001576E0` | Y | — |
| 0x00157CE0 | boot | 148 | M; 00 f1; E c16504 | verified-unbound | `em_area01_sys.c` / `em_area01_sys_00157CE0` | S | — |
| 0x001581A0 | boot | 79 | X; 07 f531 | verified-unbound | `em_area01_exitb.c` / `em_area01_exitb_001581A0` | Y | — |
| 0x00158590 | boot | 158 | M; 00 f1; E c16504 | verified-unbound | `em_area01_sys.c` / `em_area01_sys_00158590` | S | — |
| 0x00158810 | boot | 237 | X; 07 f531 | verified-unbound | `em_area01_exitb.c` / `em_area01_exitb_00158810` | Y | — |
| 0x00158BD0 | boot | 85 | X; 07 f531 | verified-unbound | `em_area01_exitb.c` / `em_area01_exitb_00158BD0` | Y | — |
| 0x00158D30 | boot | 97 | M; 00 f1; E c16503 | verified-unbound | `em_area01_sys.c` / `em_area01_sys_00158D30` | S | — |
| 0x00159B90 | boot | 181 | M; 00 f1; E c16503 | verified-unbound | `em_area01_sys.c` / `em_area01_sys_00159B90` | S | — |
| 0x0015A2C0 | boot | 291 | M; 00 f1; E c16503 | verified-unbound | `em_area01_sys.c` / `em_area01_sys_0015A2C0` | S | — |
| 0x0015AB00 | boot | 61 | X; 07 f531 | verified-unbound | `em_area01_exitb.c` / `em_area01_exitb_0015AB00` | Y | — |
| 0x0015B030 | boot | 62 | X; 07 f531 | verified-unbound | `em_area01_exitb.c` / `em_area01_exitb_0015B030` | Y | — |
| 0x0015B610 | boot | 88 | M; 04 f1810 | verified-unbound | `em_area01_render_frame.c` / `em_area01_render_0015B610` | R | — |
| 0x0015B770 | boot | 182 | S; s3 f210 | verified-unbound | `em_player_floor.c` / `em_player_stage_0015B770` | D | L |
| 0x0015FDF0 | boot | 267 | S; s6 f158 | verified-unbound | `em_player_running_jump.c` / `em_player_running_jump_aim` | Q | L |
| 0x00163D50 | boot | 79 | M; 04 f1255 | verified-unbound | `em_player_fall.c` | E | — |
| 0x00164220 | boot | 97 | M; 00 f520 | verified-unbound | `em_player_fall.c` | E | — |
| 0x001647D0 | boot | 1251 | M; 00 f358 | verified-unbound | `em_player_hang.c` | E | — |
| 0x0016D130 | boot | 833 | S; s5 f136 | verified-unbound | `em_player_closure_0e_18.c` | Q | — |
| 0x0016DE40 | boot | 855 | S; s5 f282 | verified-unbound | `em_player_closure_10_12_19.c` | Q | — |
| 0x0016EBA0 | boot | 235 | S; s5 f3021 | verified-unbound | `em_player_closure_10_12_19.c` | Q | — |
| 0x001755B0 | boot | 33 | M; 00 f492 | verified-unbound | `em_player_record_helpers.c` | M | L |
| 0x001776E0 | boot | 293 | M; 00 f304 | verified-unbound | `em_player_record_helpers.c` | M | — |
| 0x00177CF0 | boot | 148 | M; 00 f304 | verified-unbound | `em_player_record_helpers.c` | M | — |
| 0x00179010 | boot | 38 | S; s5 f282 | verified-unbound | `em_player_closure_10_12_19.c` | Q | — |
| 0x001790B0 | boot | 39 | S; s5 f376 | verified-unbound | `em_player_closure_0e_18.c` | Q | — |
| 0x00179150 | boot | 32 | S; s5 f376 | verified-unbound | `em_player_closure_0e_18.c` | Q | — |
| 0x00179910 | boot | 160 | S; s5 f376 | verified-unbound | `em_player_closure_10_12_19.c` | Q | — |
| 0x0017C370 | boot | 52 | S; s3 f209 | verified-unbound | `em_player_stage_workers.c` | D | L |
| 0x0017E250 | boot | 174 | M; 00 f360 | verified-unbound | `em_player_misc_workers.c` | M | — |
| 0x0017E510 | boot | 115 | M; 00 f360 | verified-unbound | `em_player_misc_workers.c` | M | — |
| 0x0017F240 | boot | 56 | M; 00 f358 | verified-unbound | `em_player_hang.c` | M | — |
| 0x0017F320 | boot | 177 | M; 00 f336 | verified-unbound | `em_player_record_helpers.c` | M | — |
| 0x00182250 | boot | 100 | M; 00 f358 | verified-unbound | `em_player_misc_workers.c` | M | — |
| 0x001823E0 | boot | 17 | S; s5 f346 | verified-unbound | `em_player_major2.c` | Q | — |
| 0x00183250 | boot | 104 | M; 04 f1810 | verified-unbound | `em_area01_math_player.c` / `em_area01_math_00183250` | M | — |
| 0x00187DE0 | boot | 45 | M; 02 f39 | verified-unbound | `em_area01_math_player.c` / `em_area01_math_00187DE0` | M | — |
| 0x00187EC0 | boot | 8 | M; 00 f137 | verified-unbound | `em_area01_math_player.c` / `em_area01_math_00187EC0` | M | — |
| 0x00188550 | boot | 7 | M; 00 f357 | verified-unbound | `em_player_record_helpers.c` | M | — |
| 0x00188610 | boot | 7 | S; s5 f282 | verified-unbound | `em_area01_room.c` / `em_area01_room_00188610` | Q | — |
| 0x00191120 | boot | 59 | M; 00 f357 | verified-unbound | `em_camera_follow_original.c` / `em_camera_follow_00191120` | S | — |
| 0x00198D90 | boot | 93 | S; s5 f282 | verified-unbound | `em_area01_room.c` / `em_area01_room_00198D90` | Q | — |
| 0x0019B4C0 | boot | 128 | M; 00 f1; E c16504 | verified-unbound | `em_area01_sys.c` / `em_area01_sys_0019B4C0` | S | — |
| 0x0019CF50 | boot | 248 | M; 00 f1; E c16504 | verified-unbound | `em_area01_sys.c` / `em_area01_sys_0019CF50` | S | — |
| 0x001A06A0 | boot | 283 | M; 00 f1; E c16504 | verified-unbound | `em_area01_sys.c` / `em_area01_sys_001A06A0` | S | — |
| 0x001A8840 | boot | 76 | M; 00 f6 | verified-unbound | `em_area01_sys.c` / `em_area01_sys_001A8840` | S | — |
| 0x001A9E00 | boot | 88 | M; 00 f6 | verified-unbound | `em_area01_sys.c` / `em_area01_sys_001A9E00` | S | — |
| 0x001AA000 | boot | 78 | M; 00 f57; E c16505 | verified-unbound | `em_area01_sys.c` / `em_area01_sys_001AA000` | S | — |
| 0x001AA4E0 | boot | 86 | S; s6 f158 | verified-unbound | `em_player_running_jump.c` / `em_player_running_jump_target` | Q | L |
| 0x001AF7C0 | boot | 13 | S; s2 f280 | verified-unbound | `em_status_models.c` / `w_001AF7C0 (also standalone SIDE)` | D | L |
| 0x001AFF10 | boot | 30 | S; s2 f279 | verified-unbound | `em_status_scene_original.c` | D | L |
| 0x001B0000 | boot | 27 | S; s2 f280 | verified-unbound | `em_status_scene_original.c` | D | L |
| 0x001B0300 | boot | 86 | M; 04 f1810 | verified-unbound | `em_area01_sys.c` / `em_area01_sys_001B0300` | S | — |
| 0x001B0C00 | boot | 22 | M; 07 f169 | verified-unbound | `em_script_host_workers.c` / `em_script_host_001B0C00` | S | L |
| 0x001B0D80 | boot | 15 | M; 00 f35; E c16534 | verified-unbound | `em_area01_sys.c` / `em_area01_sys_001B0D80` | S | — |
| 0x001B13F0 | boot | 31 | M; 00 f1; E c16505 | verified-unbound | `em_area01_math_actor.c` / `em_area01_math_001B13F0` | M | — |
| 0x001B2140 | boot | 625 | M; 00 f1; E c16505 | verified-unbound | `em_area01_math_actor.c` / `em_area01_math_001B2140` | M | — |
| 0x001B6D70 | boot | 52 | M; 03 f142 | verified-unbound | `em_area01_sys.c` / `em_area01_sys_001B6D70` | S | — |
| 0x001B76D0 | boot | 9 | M; 03 f431 | verified-unbound | `em_area01_sys.c` / `em_area01_sys_001B76D0` | S | — |
| 0x001B9CF0 | boot | 191 | M; 03 f301 | verified-unbound | `em_area01_math_owner.c` / `em_area01_math_001B9CF0` | M | — |
| 0x001BB400 | boot | 71 | S; s4 f250 | verified-unbound | `em_area01_room.c` / `em_area01_room_001BB400` | Q | — |
| 0x001BB520 | boot | 14 | X; 07 f531; E c16503 | verified-unbound | `em_area01_exitb.c` / `em_area01_exitb_001BB520` | Y | — |
| 0x001BB560 | boot | 152 | M; 00 f1; E c16504 | verified-unbound | `em_area01_math_owner.c` / `em_area01_math_001BB560` | M | — |
| 0x001BB7C0 | boot | 10 | S; s4 f246 | verified-unbound | `em_area01_room.c` / `em_area01_room_001BB7C0` | Q | — |
| 0x001BB7F0 | boot | 27 | S; s4 f334 | verified-unbound | `em_area01_room.c` / `em_area01_room_001BB7F0` | Q | — |
| 0x001BB860 | boot | 157 | M; 00 f1; E c16503 | verified-unbound | `em_area01_math_owner.c` / `em_area01_math_001BB860` | M | — |
| 0x001BBAE0 | boot | 68 | M; 03 f143 | verified-unbound | `em_area01_math_owner.c` / `em_area01_math_001BBAE0` | M | — |
| 0x001BBBF0 | boot | 75 | M; 03 f75 | verified-unbound | `em_area01_math_owner.c` / `em_area01_math_001BBBF0` | M | — |
| 0x001BF630 | boot | 31 | M; 00 f1; E c16504 | verified-unbound | `em_area01_math_owner.c` / `em_area01_math_001BF630` | M | — |
| 0x001BFFD0 | boot | 13 | M; 00 f1; E c16503 | verified-unbound | `em_area01_math_owner.c` / `em_area01_math_001BFFD0` | M | — |
| 0x001C0004 | boot | 181 | M; 00 f1; E c16503 | verified-unbound | `em_area01_math_owner.c` / `em_area01_math_001BFFD0 (interior piece)` | M | — |
| 0x001C02E0 | boot | 254 | M; 00 f1; E c16503 | verified-unbound | `em_area01_math_owner.c` / `em_area01_math_001C02E0` | M | — |
| 0x001C2430 | boot | 37 | X; 07 f532 | verified-unbound | `em_area01_exitb.c` / `em_area01_exitb_001C2430` | Y | — |
| 0x001C2540 | boot | 38 | X; 07 f532; E c16504 | verified-unbound | `em_area01_exitb.c` / `em_area01_exitb_001C2540` | Y | — |
| 0x001C25E0 | boot | 42 | M; 00 f1; E c16505 | verified-unbound | `em_area01_math_actor.c` / `em_area01_math_001C25E0` | M | — |
| 0x001C2770 | boot | 544 | M; 00 f1; E c16505 | verified-unbound | `em_area01_math_actor.c` / `em_area01_math_001C2770` | M | — |
| 0x001C39F0 | boot | 122 | M; 00 f1; E c16505 | verified-unbound | `em_area01_math_actor.c` / `em_area01_math_001C39F0` | M | — |
| 0x001C3BE0 | boot | 96 | M; 00 f1; E c16504 | verified-unbound | `em_area01_math_actor.c` / `em_area01_math_001C3BE0` | M | — |
| 0x001C3D60 | boot | 17 | M; 00 f1; E c16505 | verified-unbound | `em_area01_math_actor.c` / `em_area01_math_001C3D60` | M | — |
| 0x001C3DB0 | boot | 191 | X; 07 f532; E c16504 | verified-unbound | `em_area01_exitb.c` / `em_area01_exitb_001C3DB0` | Y | — |
| 0x001C4720 | boot | 13 | S; s2 f246 | verified-unbound | `em_pickup_owner.c` / `em_pickup_owner_take (also standalone SIDE)` | D | L |
| 0x001C4FA0 | boot | 43 | A; E c16503 | verified-unbound | `em_area01_light_owner.c` / `em_area01_light_001C4FA0` | L2 | — |
| 0x001C50B0 | boot | 303 | A; E c16503 | verified-unbound | `em_area01_light_owner.c` / `em_area01_light_001C50B0` | L2 | — |
| 0x001C6160 | boot | 11 | X; 07 f755 | verified-unbound | `em_area01_exitb.c` / `em_area01_exitb_001C6160` | Y | — |
| 0x001C69A0 | boot | 254 | M; 00 f1 | verified-unbound | `em_area01_math_actor.c` / complete entry and shared stages; status typed view and AREA01 model adapter (LEVEL2_RENDER.md ("C69A0 live pose binding and shared status stages")) | M | L |
| 0x001CB360 | boot | 21 | M; 00 f1; E c16504 | verified-unbound | `em_area01_math_owner.c` / `em_area01_math_001CB360` | M | — |
| 0x001CB480 | boot | 28 | S; s2 f280 | verified-unbound | `em_area01_side.c` / `em_area01_side_001CB480` | D | U |
| 0x001CD070 | boot | 68 | M; 00 f1 | verified-unbound | `em_area01_render_gs.c` / `em_area01_render_001CD070` | R | L |
| 0x001CD180 | boot | 76 | M; 00 f1; E c16509 | verified-unbound | `em_area01_render_gs.c` / `em_area01_render_001CD180` | R | — |
| 0x001CD2B0 | boot | 48 | M; 00 f1; E c16509 | verified-unbound | `em_area01_render_gs.c` / `em_area01_render_001CD2B0` | R | — |
| 0x001D0400 | boot | 43 | X; 07 f531 | verified-unbound | `em_area01_exitb.c` / `em_area01_exitb_001D0400` | Y | — |
| 0x001D0C80 | boot | 47 | A; E c16503 | verified-unbound | `em_area01_math_owner.c` / `em_area01_math_001D0C80` | K | — |
| 0x001D0D40 | boot | 8 | A; E c16503 | verified-unbound | `em_area01_math_owner.c` / `em_area01_math_001D0D40` | K | — |
| 0x001D0D60 | boot | 111 | S; s5 f904 | verified-unbound | `em_area01_room.c` / `em_area01_room_001D0D60` | Q | — |
| 0x001D4FC0 | boot | 106 | M; 00 f1; E c16503 | verified-unbound | `em_area01_render_vif.c` / `em_area01_render_001D4FC0` | R | — |
| 0x001D5170 | boot | 92 | M; 00 f53 | verified-unbound | `em_area01_render_vif.c` / `em_area01_render_001D5170` | R | — |
| 0x001D5A70 | boot | 86 | M; 00 f1; E c16503 | verified-unbound | `em_area01_render_vif.c` / `em_area01_render_001D5A70` | R | — |
| 0x001D5BD0 | boot | 41 | M; 00 f1; E c16503 | verified-unbound | `em_area01_render_vif.c` / `em_area01_render_001D5BD0` | R | — |
| 0x001D8100 | boot | 9 | S; s3 f208 | verified-unbound | `em_effect_original.c` | D | L |
| 0x001E3D20 | boot | 27 | S; s3 f208 | verified-unbound | `em_area01_side.c` / `em_area01_side_001E3D20` | D | — |
| 0x001E3D90 | boot | 540 | M; 00 f1; E c16503 | verified-unbound | `em_area01_sys.c` / `em_area01_sys_001E3D90` | S | — |
| 0x001E7CB0 | boot | 25 | M; 00 f1; E c16504 | verified-unbound | `em_area01_sys.c` / `em_area01_sys_001E7CB0` | S | — |
| 0x001E7D20 | boot | 902 | M; 00 f1; E c16503 | verified-unbound | `em_area01_sys.c` / `em_area01_sys_001E7D20` | S | — |
| 0x001E8B90 | boot | 187 | M; 02 f39 | verified-unbound | `em_area01_render_hud.c` / `em_area01_render_001E8B90` | R | — |
| 0x001E8E80 | boot | 253 | X; 07 f531 | verified-unbound | `em_area01_exitb.c` / `em_area01_exitb_001E8E80` | Y | — |
| 0x001E9280 | boot | 189 | X; 07 f532 | verified-unbound | `em_area01_exitb.c` / `em_area01_exitb_001E9280` | Y | — |
| 0x001E9580 | boot | 566 | X; 07 f531; E c16503 | verified-unbound | `em_area01_exitb.c` / `em_area01_exitb_001E9580` | Y | — |
| 0x001E9E60 | boot | 233 | M; 00 f1; E c16504 | verified-unbound | `em_area01_render_hud.c` / `em_area01_render_001E9E60` | R | — |
| 0x001EAF00 | boot | 31 | M; 00 f138 | verified-unbound | `em_area01_render_hud.c` / `em_area01_render_001EAF00` | R | — |
| 0x001EAF80 | boot | 39 | M; 02 f44 | verified-unbound | `em_area01_render_hud.c` / `em_area01_render_001EAF80` | R | — |
| 0x001EB020 | boot | 140 | M; 02 f39 | verified-unbound | `em_area01_render_hud.c` / `em_area01_render_001EB020` | R | — |
| 0x001EC270 | boot | 96 | M; 02 f599 | verified-unbound | `em_area01_render_hud.c` / `em_area01_render_001EC270` | R | — |
| 0x001EFE00 | boot | 43 | S; s3 f208 | verified-unbound | `em_area01_side.c` / `em_area01_side_001EFE00` | D | L |
| 0x001F0190 | boot | 64 | S; s3 f209 | verified-unbound | `em_area01_side.c` / `em_area01_side_001F0190` | D | L |
| 0x001F0290 | boot | 12 | S; s3 f209 | verified-unbound | `em_area01_side.c` / `em_area01_side_001F0290` | D | L |
| 0x001F0460 | boot | 176 | M; 00 f178 | verified-unbound | `em_effect_original.c` | E | L |
| 0x001F4A10 | boot | 120 | M; 00 f1; E c16504 | verified-unbound | `em_area01_render_gs.c` / `em_area01_render_001F4A10` | R | — |
| 0x001F4BF0 | boot | 50 | M; 00 f1; E c16504 | verified-unbound | `em_status_scene_original.c` | E | — |
| 0x001F4CC0 | boot | 30 | M; 00 f1; E c16504 | verified-unbound | `em_area01_render_gs.c` / `em_area01_render_001F4CC0` | R | — |
| 0x001FAD70 | boot | 61 | M; 07 f169 | verified-unbound | `em_stream_lanes_original.c` / `em_stream_lanes_001FAD70` | S | L |
| 0x001FCF60 | boot | 11 | S; s1 f330 | verified-unbound | `em_census_standins.c` | D | L |
| 0x001FCF90 | boot | 82 | S; s1 f330 | verified-unbound | `em_census_standins.c` | D | L |
| 0x001FE660 | boot | 20 | S; s1 f330 | verified-unbound | `em_census_standins.c` | D | L |
| 0x00207D90 | boot | 43 | S; s2 f280 | boundary | `em_page_draw.c` native draw contract; standalone `em_area01_ui_pages.c` | U | B |
| 0x00208040 | boot | 88 | S; s2 f280 | boundary | `em_page_draw.c` native draw contract; standalone `em_area01_ui_pages.c` | U | B |
| 0x002082B0 | boot | 295 | S; s5 f1633 | verified-unbound | `em_item_geometry.c` / `em_item_geometry_arc` | Q | L |
| 0x00208AD0 | boot | 490 | S; s5 f1633 | verified-unbound | `em_status_draw.c` / `em_status_health_draw` | Q | L |
| 0x0020F950 | boot | 438 | S; s2 f279 | verified-unbound | `em_area01_ui_pages.c` / `em_area01_ui_0020F950` | U | L |
| 0x00210030 | boot | 98 | S; s2 f280 | verified-unbound | `em_area01_ui_pages.c` / `em_area01_ui_00210030` | U | L |
| 0x002101C0 | boot | 527 | S; s2 f280 | verified-unbound | `em_area01_ui_pages.c` / `em_area01_ui_002101C0` | U | L |
| 0x00210A00 | boot | 127 | S; s2 f280 | verified-unbound | `em_area01_ui_pages.c` / `em_area01_ui_00210A00` | U | L |
| 0x00210C00 | boot | 203 | S; s2 f280 | verified-unbound | `em_area01_ui_pages.c` / `em_area01_ui_00210C00` | U | L |
| 0x00210F30 | boot | 195 | S; s2 f280 | verified-unbound | `em_area01_ui_pages.c` / `em_area01_ui_00210F30` | U | L |
| 0x00211400 | boot | 244 | S; s2 f280 | verified-unbound | `em_area01_ui_pages.c` / `em_area01_ui_00211400` | U | L |
| 0x002131B0 | boot | 194 | S; s1 f330 | verified-unbound | `em_area01_ui_pages.c` / `em_area01_ui_002131B0` | U | L |
| 0x002134C0 | boot | 333 | S; s1 f330 | verified-unbound | `em_area01_ui_pages.c` / `em_area01_ui_002134C0` | U | L |
| 0x00213F30 | boot | 60 | S; s1 f329 | verified-unbound | `em_area01_ui_pages.c` / `em_area01_ui_00213F30` | U | L |
| 0x00214020 | boot | 338 | S; s1 f329 | verified-unbound | `em_area01_ui_pages.c` / `em_area01_ui_00214020` | U | L |
| 0x002160B0 | boot | 1014 | S; s5 f1632 | verified-unbound | `em_status_pages_item.c` / `em_status_pages_002160B0` | Q | L |
| 0x0021BC40 | boot | 50 | S; s3 f209 | verified-unbound | `em_player_stage_workers.c` | D | L |
| 0x0021C350 | boot | 37 | S; s3 f209 | verified-unbound | `em_player_stage_workers.c` | D | L |
| 0x0021D1A0 | boot | 43 | S; s3 f210 | verified-unbound | `em_player_reaction.c` | D | L |
| 0x0021D600 | boot | 14 | S; s3 f210 | verified-unbound | `em_player_reaction.c` | D | L |
| 0x0021D800 | boot | 236 | S; s3 f210 | verified-unbound | `em_player_reaction.c` | D | L |
| 0x00225A00 | boot | 5 | S; s4 f679 | verified-unbound | `em_area01_room.c` / `em_area01_room_00225A00` | Q | V |
| 0x0022B700 | boot | 40 | S; s3 f209 | verified-unbound | `em_area01_ui_effect.c` / `em_area01_ui_0022B700` | U | L |
| 0x0022B7A0 | boot | 243 | S; s3 f209 | verified-unbound | `em_area01_ui_effect.c` / `em_area01_ui_0022B7A0` | U | L |
| 0x0022BB70 | boot | 17 | S; s3 f209 | verified-unbound | `em_area01_ui_effect.c` / `em_area01_ui_0022BB70` | U | L |
| 0x0022BBC0 | boot | 1509 | S; s3 f209 | verified-unbound | `em_area01_ui_effect.c` / `em_area01_ui_0022BBC0` | U | L |
| 0x0022DCD0 | boot | 599 | X; 07 f531 | verified-unbound | `em_area01_exita.c` / `em_area01_exita_0022DCD0` | X | — |
| 0x00823580 | AREA01 | 147 | M; 00 f1; E c16503 | verified-unbound | `em_area01_overlay.c` / `em_area01_ovl_00823580` | O | — |
| 0x00825130 | AREA01 | 65 | M; 05 f139 | verified-unbound | `em_area01_overlay.c` / `em_area01_ovl_00825130` | O | — |
| 0x00825240 | AREA01 | 65 | M; 05 f2491 | verified-unbound | `em_area01_overlay.c` / `em_area01_ovl_00825240` | O | — |
| 0x00825350 | AREA01 | 88 | M; 00 f1; E c16503 | verified-unbound | `em_area01_overlay.c` / `em_area01_ovl_00825350` | O | — |
| 0x008254B0 | AREA01 | 53 | M; 00 f1; E c16504 | verified-unbound | `em_area01_overlay.c` / `em_area01_ovl_008254B0` | O | — |
| 0x00825590 | AREA01 | 55 | M; 03 f296 | verified-unbound | `em_area01_overlay.c` / `em_area01_ovl_00825590` | O | — |
| 0x00825670 | AREA01 | 52 | M; 05 f3932 | verified-unbound | `em_area01_overlay.c` / `em_area01_ovl_00825670` | O | — |
| 0x00825740 | AREA01 | 113 | A; E c16503 | verified-unbound | `em_area01_overlay.c` / `em_area01_ovl_00825740` | T | — |
| 0x008261A0 | AREA01 | 24 | M; 00 f1; E c16503 | verified-unbound | `em_area01_overlay.c` / `em_area01_ovl_008261A0` | O | — |
| 0x00826200 | AREA01 | 143 | M; 00 f1; E c16503 | verified-unbound | `em_area01_overlay.c` / `em_area01_ovl_00826200` | O | — |
| 0x00826440 | AREA01 | 221 | M; 00 f1; E c16503 | verified-unbound | `em_area01_overlay.c` / `em_area01_ovl_00826440` | O | — |
| 0x008267C0 | AREA01 | 99 | M; 00 f1; E c16503 | verified-unbound | `em_area01_overlay.c` / `em_area01_ovl_008267C0` | O | — |
| 0x00826CF0 | AREA01 | 17 | M; 00 f1; E c16503 | verified-unbound | `em_area01_overlay.c` / `em_area01_ovl_00826CF0` | O | — |
| 0x00826D40 | AREA01 | 1386 | M; 00 f1; E c16503 | verified-unbound | `em_area01_overlay.c` / `em_area01_ovl_00826D40` | O | — |
| 0x00828850 | AREA01 | 102 | M; 00 f1; E c16503 | verified-unbound | `em_area01_overlay.c` / `em_area01_ovl_00828850` | O | — |

## 5. The five baseline missing entries and caller evidence

The five arrival-only entries total 514 instructions. At phase 1, searching native definitions, declarations and callers found no implementations; references to helpers in a worker table do not count as translations. The original C and the relevant native caller were inspected for these edges:

| Missing entry | Original caller / role | Binding implication |
|---|---|---|
| 001C4FA0 (43 instructions) | 001C50B0 calls the type-dependent progress predicate. | Implement once with the original progress-byte tests. |
| 001C50B0 (303) | AREA01 placement [40] installs this light owner; the EXIT census sees it at counter 16503, though the later a01 beats never hit it. It calls 001C4FA0 and shared light/effect helpers. | Preserve its initialization/teardown; disappearance before the route snapshot does not make it optional. |
| 001D0C80 (47) | 001C02E0 state 0 calls the bone/segment setup; original C allocates through 001AF780 after the slot-limit test. Native `em_area01_math_owner.c` still calls it through F_001D0C80. | Bind the one canonical slot stack and preserve the original failure path. |
| 001D0D40 (8) | 001C02E0 state 0 follows setup with this animation-record binder. Native F_001D0D40 is a call-out only. | Translate the original helper, then bind its storage view; a worker declaration is insufficient. |
| AREA01 00825740 (113) | Placement [38] talk owner, original `func_overlay_AREA01_00825700.c` (runtime +0x40); its script is 0x82A7B0. EXIT sees it at counter 16503, but no a01 beat sees it. | Add it to the AREA01 owner set with an oracle; the existing overlay harness covers only the other fourteen. |

Original-source locations are `../Extermination/src/func_001C4FA0.c`, `func_001C50B0.c`, `func_001D0C80.c`, `func_001D0D40.c`, and `src/overlays/AREA01/func_overlay_AREA01_00825700.c`; the bone caller is `src/func_001C02E0.c`. The first four helper/owner sources are byte-matched C; the caller 001C02E0 is NEARMISS, so its native MATH oracle is the instruction-level evidence, not the decomp's label.

Other original caller relationships checked against the existing translated owner/oracle interfaces: 001BFFD0 spans 001C0004; 001D5370 reaches the RENDER lane through 001D5BD0; 00128C10/0012A5D0 reach actor math; 001BB860 reaches slider-door workers 001BB520/001BB560/001BB7C0/001BB7F0; the overlay NPC dispatches 008254B0/00825590/00825670 by its story byte. These hooks still need a real AREA01 memory and worker adapter.

## 6. Coverage limits and promotion checklist

- The 808 old-baseline entries are inherited infrastructure, not duplicated in §4; this ledger is the requested a01 delta plus §3.26, not a complete proof that every shared caller or switch case is ready. For an all-entry list use `a01_delta.json.functions`.
- Census presence means an entry ran at least once. It gives neither call counts nor full dispatcher-case coverage. Most a01 census replays completed the same route with different frame timings from their captures; SECOND_LEVEL_ROUTE §6 and §9.7 list the exact differences.
- The initial AREA01 scene gate prevents a connected run. Common player/camera code, asset availability, the AREA01 messages and the native platform draw contract each need route evidence before promoting their rows. The standalone GS/VIF translations are retained as verified game-visible packet logic; they are not all reclassified as platform boundaries.
- The AREA00 portion of a01_07 extends beyond this task's arrival stop. Keep its historical rows in the inventory without treating them as a requirement to continue AREA00 gameplay.
- Each promotion must name the original caller, canonical native owner, live adapter and compared AREA01 beat/window, then recompute status totals. Record shared-file changes in LEVEL2_BINDING.md. Do not edit the first-level census or claim that a standalone PASS proves AREA01 play.

Phase 1 established this inventory without implementing the five missing entries. The private reference-harness dependency repair and later shared-file edits are recorded in LEVEL2_BINDING.md by the integrating phase.


## 7. Phase-2 prerequisite checkpoint

All five phase-1 missing entries now have standalone original-instruction
verification. This changes availability, not connected AREA01 execution.
The exact inventory in §4 is current; §2 preserves the original baseline.

| Current AREA01 status | Entries | Instructions |
|---|---:|---:|
| live | 0 | 0 |
| verified-unbound | 177 | 29,416 |
| unverified | 0 | 0 |
| stand-in | 0 | 0 |
| missing | 0 | 0 |
| boundary | 2 | 131 |

| Evidence key | New owner and original caller | Verification |
|---|---|---|
| K | 001D0C80 / 001D0D40 in the existing MATH module, called directly by 001C02E0 state 0 | LEVEL2_RUNTIME.md ("AREA01 bone-slot initialization helpers"): quick 40 setup cases / 1,443 worker boundaries / all 4 branch outcomes, 21 record-bind cases; changed caller 57 cases / 363 boundaries / all 24 outcomes. Complete math quick suite passes in 8.1 s. Full: 14,135 setup cases / 760,551 boundaries; 34,716 bind cases; caller 1,606 cases / 7,656 boundaries; complete math full suite passes in 458.7 s. |
| L2 | 001C4FA0 / 001C50B0 in em_area01_light_owner, placement [40] and its predicate | LEVEL2_RUNTIME.md ("AREA01 flicker-light owner"): quick 161 cases / 216 boundaries; full 2,309 cases / 2,536 boundaries, plus six fail-stop contracts. Original input is the EXIT arrival's retained light node. |
| T | AREA01 00825740 in the existing OVERLAY module, placement [38] | LEVEL2_RUNTIME.md ("AREA01 placement [38]: talk owner 0x825740"): owner quick 184 cases, full 1,131; whole overlay full 14,034 cases / 21,199 executions, all 15 entries. These new owner cases are designed calls on captured memory, not a captured continuing conversation. |

K was run by the census/bone lane. L2 and T are the implementing lanes'
reported original-instruction results, with their details and local
receipts recorded in the linked documents. These promotions do not mean
that native AREA01 calls any of the workers with the right live state.
The frame, owner, script, collision, camera, message, renderer and canonical
memory bindings still require route evidence. The census instruction
counts retain the source-entry extents; a complete native/oracle body can
also cover trailing instructions not included in an old entry's size.

## AREA01 arrival binding dependencies

Read-only mapping, 2026-10-03. The map follows the original callers and names
implementation owners. It is not a claim that any unbound path runs live.
The user-requested scope supersedes the older first-level-only instructions.

### Original frame chain

The task chain is 001ACEC0 -> 001AD250 -> 001AD4D0 -> 001AE040
(`anim_frame_top_b.c` in the decomp, translated by `em_scene_frame.c`). State
0 advances the state byte, then calls 001AFCA0, 001AFCF0, 001B07C0(0),
001B6990, 001D19E0, 001C1DC0, 00199C50, 001AEE40(4), 001FAE70(1),
001C5C50, 001D1EF0 and returns. The arrival smoke already reaches that return.

On the next frame state 1 runs 001AE7E0. With the ordinary classifier result
and no loader busy byte, it selects 001AE5E0 when scratchpad 3B8D is zero,
otherwise 001AE6B0. These original routines are byte-matched C. The gameplay
variant runs player context/0015BCF0, render setup/001C1D00, pool walk mode 0,
player shadow/draw, effects, camera context/0018B9C0, collision close-out and
presentation. The cutscene variant runs render setup and pool mode 1 before
effects/player, then pool mode 2, shadow, camera, close-out and presentation.
Use the existing `em_sf_001AE5E0` / `em_sf_001AE6B0` owners and retain that order.

The deliberate fail-stop is `em_scene_bindings.c:w_001AD4D0`: AREA01 with
frame state other than zero faults at 001AE040. Do not remove that condition
until the workers below are complete. `em_area01_arrival_bind` only records
the callback and spawn record for the exit comparison; it binds no behavior.

### Smallest complete arrival package

1. **Canonical storage and owner dispatch.** `EmActor` is a native struct,
   not an EE-byte overlay (`em_actor_pool.h`). AREA01's standalone modules
   use original addresses and original-layout bytes. New adapters must map
   those reads/writes to the canonical pool, player, progress, request,
   scratchpad, resource and bone-slot owners. The overlay has a `bytes` hook;
   math uses `EmA01Math` RAM/spad views; sys uses `EmArea01SysRegion` views.
   A second unsynchronized arena is not a live binding. Commit/reload record
   views around workers where the original calls and re-reads them. Reuse
   existing owner adapters for already-bound functions.
2. **Area state and assets.** Arrival already loads AREA01 roster, spawn,
   EMCL/cells, and static-object resource 0x44 from the module loader.
   Actually run the AREA01 overlay init (runtime 00823A50, decomp
   `func_overlay_AREA01_00823A10.c`) through the shared 001E7780 dispatch:
   its effect-grid globals and BSS storage are read by owner 001E7D20.
   The resource checkpoint now performs those stores over the loader's
   canonical overlay data/BSS and six owned globals (LEVEL2_RUNTIME.md ("AREA01 canonical overlay state and initialization")).
   The grid owner's frame adapter still needs those views.
3. **Shared player and collision.** `w_001AFCA0` already binds the shared
   player stage and camera over AREA01 collision. The first 0015BCF0 after
   arrival spawns the 0015C420 player children and runs stage rebuild.
   Keep the existing player, equipment, effect and indicator owners.
   `a01_00` exercises grab/hang/pull-up/fall/land; the route's new historical
   addresses do not by themselves mean missing implementations after later
   first-level work. Check every census address against current owners.
   0019B4C0/001A06A0/0019CF50 need the sys dispatcher (see
   LEVEL2_COLLISION.md ("AREA01 collision prerequisite audit")).
4. **Render world and shadow.** State 0 now calls `rcl_bind` for both world
   areas, retaining AREA01's delivered static bank and borrowing dynamic
   slot 0x45. The AREA11 default export is loaded only for AREA11. The
   dynamic packet adapter composes the AREA01 VIF routines
   (`em_area01_render_vif.c`: 001D4FC0, 001D5170, 001D5A70, 001D5BD0),
   with the existing depth/page owners (LEVEL2_RENDER.md ("AREA01 dynamic packet binding")).
   Dynamic VU presentation, owner render hooks and the AREA01 player
   shadow still need connected-frame evidence.
5. **Every pool callback, including hidden owners.** `spawn_area01` must
   use a real callback binder. Pool scheduling reaches offscreen/dormant
   owners too. Shared callback translations already live in
   `em_area11_bindings.c` include pickups, crates, 001BC350 doors, props,
   title, equipment, effects and indicator children. Reuse their underlying
   original modules; several current hosts have AREA11-only resources and
   must accept area inputs before reuse. New callback families are listed
   below. A state that frees an owner still must run its original setup and
   teardown workers in the original order.
6. **Camera.** Keep `em_camera_live` / follow / leftovers / specials. The
   AREA01 state-0 placement already binds and uses them. First-visit area 1
   has no special arm in 00195130's area switch. Camera mode 1's 001B0300
   presently faults in the host, despite its existing translation in
   `em_area01_sys.c`; bind it if reached. The cinematic host and scene-entry
   tables must become area-aware before script camera modes are enabled.

### Pool modules required before the first world frame

| Original callback or family | Existing owner / next binding |
|---|---|
| 00128C10, 00158D30, 00159B90, 0015A2C0, 001E3D90, 001E7D20 | `em_area01_sys.c`; worker dispatcher into canonical owner, animation, collision, sound and render services |
| 001BB860, 001BFFD0, 001C02E0 | `em_area01_math_owner.c`; 001BFFD0 includes the census's separate 001C0004 piece |
| 001B13F0, 001B2140, 001C25E0, 001C2770, 001C39F0, 001C3BE0, 001C3D60 | `em_area01_math_actor.c`, called by the new owners |
| 00128390, 001289C0, 00128AB0, 00129780 | `em_area01_exita.c`, already needed by arrival's 00128C10 family despite the module name |
| 001BB520, 001C2540, 001C3DB0, 001E9580 | `em_area01_exitb.c`, also needed by arrival |
| 00823580 shaft door; 00825350 NPC and 008254B0 first talk; 008261A0 with 00826200/00826440; 008267C0; 00826CF0; 00826D40 and 00828850 | `em_area01_overlay.c` / `_826d40.c`; bind its typed hooks and record mapper |
| 001CD180, 001CD2B0, 001F4A10, 001F4CC0 | `em_area01_render_gs.c` |
| 001E9E60 | `em_area01_render_hud.c` |
| 001F4BF0 | existing `em_status_scene_original.c` owner |
| 001C4FA0, 001C50B0 | new standalone `em_area01_light_owner.c`; original-instruction verified, unbound; reuse `em_area00_world_001C5050` and the canonical point-light pool |
| 001AA000 | existing live `em_coll_list_passes.c`, not a new sys copy |

`FIRST_LEVEL_CENSUS.md` 3.26 adds 62 post-arrival functions, including
functions that the older a01 delta omitted because beat 15 had already run
them. Union that set with the a01 route delta. There are real holes in the
prepared translations:

| Missing original | Source and dependency size |
|---|---|
| AREA01 00825740 | `src/overlays/AREA01/func_overlay_AREA01_00825700.c`, 464 bytes, 113 census instructions. Another talk owner. Its first-visit setup calls 001B10B0, 001BA8E0, 001C63E0, writes descriptor/yaw/bank, then sets lifecycle 3 because 75A is zero. Next tick calls 001BA540 then frees. Same shared NPC workers as 00825350; its later talk arm uses script 82A7B0. No port implementation found. |
| 001D0C80 | `src/func_001D0C80.c`, 47 census instructions. Mode-5 model setup via 001CA5E0, bone-count 001C6150, canonical bone-slot cap/allocation 001AF780, bone-array publication, extra control record at +90. Requires the real shared bone allocator; do not assume infinite capacity. No port implementation found. |
| 001D0D40 | `src/func_001D0D40.c`, 8 census instructions. Initializes the control record pointed to by actor +90 with descriptor, float count, zero time and mode. Reuse EE integer-to-float behavior. No port implementation found. |
| 001C4FA0 / 001C50B0 | Initially absent; now translated and original-instruction verified in `em_area01_light_owner.c`, still unbound. Their matching C files have 43 / 303 census instructions. Setup can immediately enter teardown; the owner uses 001F5490 / 001F5F60 already in `em_area00_fx_exit.c`, existing `em_area00_world_001C5050`, light release and random/vector workers. See LEVEL2_RUNTIME.md ("AREA01 flicker-light owner"). |

The five source translations now exist (LEVEL2_RUNTIME.md ("AREA01 bone-slot initialization helpers"),
LEVEL2_RUNTIME.md ("AREA01 placement [38]: talk owner 0x825740") and LEVEL2_RUNTIME.md ("AREA01 flicker-light owner")); the table records why they
were needed. Their adapters remain unbound. Owners that are subsequently
freed still require their setup behavior. Original overlay addresses must also carry the
area identity; multiple overlays use 00823580.

### Scripts, doors, pickups and messages

The first three movement beats do not start their own AREA01 conversation,
but all NPC/door setup and polling still runs. After those, a01_03 starts the
shaft-door scripts 829860 and 8298E0; a01_05 starts NPC script 829FA0.
Continue using `em_area_script` and its existing op implementations. Supply
AREA01 EMSC images/quads and bind op09 callbacks 825130/825240 to
`em_area01_overlay.c`. `em_area11_script_host` currently loads AREA11-only
images/camera resources and handles only AREA11 op09 targets; it cannot be
used unchanged. Its owner clip dispatch distinguishes only AREA11 door and
Roger, so generalize the host service input instead of copying interpreter
logic.

The shared 001BC350 door owner already exists, but
`em_area11_door.c:h_transition` refuses areas other than 0x0B and loads an
AREA11 destination row. Bind AREA01's exported door table, descriptors,
model bank and shared script host. The special shaft callback wraps these
same door workers; it sets story byte D_008107D9 to 0x80 after the locked try
and sets linked r13 +0xB each frame from D9==0x81. The NPC's 825590 completion
stores 759=FF and D9=81. Keep progress in `EmSceneState`.

`em_area11_boxes.c:load_bank` is AREA11-hardcoded, and its loaded bank is
cached. Common crate/prop/model workers need the area's `world_models` bank
before reuse. The interaction host also needs area-specific placements,
Use/class-list scan, descriptors, pickup data and player takeover hooks.
Do not bind AREA01 pickup callbacks to an unloaded AREA11 host.

Message loading supports EMMD area 1 already, and the AREA01 export exists.
The live service only has one installed area bank; `w_draw_line` faults on
an area mismatch. `em_message_live_install` calls shutdown and clears hosts,
streams and presenters, so area change needs either a preserve-service bank
switch or explicit rebinding of all three. AREA01 has 54 global / 184 area
records. Original 001FD950 remains the one draw/service owner. Keep message
state reset at the original reset sites, not at a convenient arbitrary tick.

### First route milestone

Arrival is spawn entry 4 at (41, 0, -565.6), yaw -1.43411. a01_00 takes 780
frames and exercises ledge grab (f304), hang (f357), pull-up (f360), top of
crate (f464), fall (f491) and landing (f519). a01_01 adds 305 frames through
the tunnel; a01_02 adds 590 to the shaft landing. The route remains at 100
health. Compare player, camera, pool lifecycle/state, progress, messages,
random-dependent owners and collision traces against these recordings; an
end-position-only comparison is insufficient. Binding one owner at a time
behind the AREA01 gate is safe; enabling the frame before every scheduled
owner has an implementation is not a complete arrival milestone.
