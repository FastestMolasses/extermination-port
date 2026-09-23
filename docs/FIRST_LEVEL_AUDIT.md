# First-level fidelity audit (AREA11)

Date: 2026-09-22. Sources: a read-only audit of the live path in eight slices (orchestration, player, camera, UI, world, render, audio-media, startup-input, original-inventory); an adversarial check of every high-severity finding that was not already verified; and the results of three work-in-progress lanes (face-host, roger-media, status-hub-ui).

"First level" means: New Game, the AREA11 opening cinematic, first control, everything inside AREA11, and the transition out of it.

Rule used throughout: **a label is not evidence.** A module counts as *verified* only when an oracle that executes original ELF or overlay instructions, or a capture comparison, exercises it. *Live* means it is reached from `main` with no `EM_*` environment variable set. This file contains addresses and individual constants only. It contains no original data or disassembly.

---

## 1. Honest status

Most of the AREA11 live path is original-faithful and verified, **up to first control**:

- the boot/title state machine (decoded but not oracle-tested)
- the fade and letterbox machines (`em_fade`)
- the script interpreter (`em_script`)
- the opening cinematic: script 0x828FC0 actors, face, camera track and dialogue clock
- the snow weather and particles
- the AREA11 flame-effect visuals
- the point-light pool and the character lighting VU arithmetic
- the scalar player motor, heading and walk entry/stop source logic
- the collision box faces
- the pickup and prop indicator children

After first control, almost everything the player can interact with runs on **legacy stand-ins**, and many of them are fabricated:

- **Use handling and pickups:** three independent proximity scans (door, then pickup, then examine), a 2-frame synchronous take, and an invented "Found: <NAME>" line.
- **Doors:** the legacy `em_door`, which walks the player at an invented 15 u/s and re-places the player at a synthesized point.
- **Status screen:** the legacy `em_hud`, with invented "CONTENT TBD"/"PARTIAL" pages. Music keeps playing and the menu sounds are silent. Since S11b the original frame machine opens and closes it: 001AE7E0 returns 2 on the real START/TRIANGLE edge, states 3/5 run with the world frozen (the st14 call order), and the legacy screen is only the interim 0020E060/0020CDC0 worker (H6).
- **Director beats:** a hand-written keyframe player that plays the wrong sounds (0x97/0x99).
- **Truck:** an invented truck fall.
- **Fan pair (records 1/2):** a constant-spin "decor prop".
- **Continue:** restores invented demo stats.

The oracle-verified replacements exist but **nothing in `src/` calls them**:

- AREA11 interaction host (panel, battery page, elevator, face host, status runtime)
- original door runtime, program and transit
- original pickup owners
- Roger runtime and media
- cinematic playback
- status hub, draw and pages
- the shared Use arbiter

The consequences are serious:

- The level **cannot be progressed**. Nothing live sets terminal power, so the elevator always refuses.
- The **Roger encounter never happens** after the opening.
- There is **no exit from AREA11**.
- The original player pose source is **permanently invalidated** after the first aim, door, examine or hit.

Presentation also has confirmed global errors:

- no AREA11 world fog (record near -209, far 304, RGB 48)
- many actor models baked with an invented stand-in light
- every legacy SFX about 7.4 semitones sharp

Summary: the *pieces* are largely verified; the *wiring* and the *live scene coordinator* are the missing work, and several live fabrications must be removed.

**Status update (2026-09-22, after WP-0..WP-2 landed):** the truck fall, the fan spin, the director sounds, the Continue demo stats, the missing fog and the permanent pose invalidation above are fixed (see the WP-0..WP-2 status lines in §4). The invented fan spin is removed, so the fan pair is drawn static. The 00827630 init rot.z (±π/4 by record +0x03) is implemented in em_pickup, but it is applied only once the manifest pickup lines carry `owner 0x827630 <flags2>`. That is PENDING: the local manifest (assets/scene_snow/scene.txt) has not been updated, so the running port still draws both fans with placement yaw only. The wiring gaps (no terminal power, no Roger encounter, no exit, legacy Use/door/status paths) are still open.

---

## 2. Live call graph (normal run)

```
main.c:265 main → em_frame_init, em_random_seed(0x45) [SI-01: seed attribution wrong], em_frontend_install (:323), em_frame_run
em_frame_step (em_frame.c:274): gfx_begin (invented clear colour) → gamepad/frame_input_read → screen_fade_tick (001AEBE0)
    → em_task_dispatch → em_bgm_service (at the 001FCA10 step-F slot) → em_opening_media_render → transition_fade_tick → movie pump
slot0 startup_task: logos → E900 → title → New Game → em_game_install_new → em_scene_task_001ACEC0 (+8 = 0)
    [since S12a: 001AD1A0 → 001AD230 (001AF2C0) → 001AD360 (E900 at step 1) → 001ADF50: 001FF080(1,0) =
     em_game_legacy_area_load (formerly game_load_task): player EMDL, scene_manifest_load(assets/scene_snow/scene.txt),
     collision, em_sfx_init → state 0 (001B07C0(0) from the spawn table)]
       manifest installs: legacy door (no goto), elevator mesh, truck, panel-as-static-prop (grate), 1 examine "terminal",
       7 legacy pickups + 6 pickup_lights + 2 prop indicators, 2 type-0x13 "display props" (really fan pair 00827630),
       4 crates, 2 drums, husk pair, weather/snow, point lights, AREA11 effect, light rig (no fog line)
    → game_task → ingame_frame_machine (:5219): case 0 (init, falls through the same tick) → case 1 selects:
       [since S8: em_scene_task_001ACEC0 → cores 001ACEC0/001AD250/0x1AE040; state 0 → em_game_legacy_state0, and since S9 the tick ends there (no world frame, as the original);
        since S10a state 1 runs the cores em_sf_001AE5E0/em_sf_001AE6B0 in the original stage order, and the two
        lists below survive only as the 001AFD70 legacy blocks em_game_legacy_pool_gameplay/_cutscene; truck,
        director and grate now run after the player stage]
       cutscene_frame (:5192, selector≠0): point_light, em_opening_runtime_tick (script 0x828FC0), effect, grate,
                       snow, pickups render-only, render_chain_build, opening camera, frame_close_out
       gameplay_frame (:4679): [hud-open / game-over early-outs, retired by S11b: frame-machine states 3/5 and
                       the 001AD140 → 001AD4E0 → 001ADF00 chain replace them] → truck → director_tick (kCineBeats) → grate
                       → actor_update (pose_stage → player_move → pose finish → legacy matrix display) → point_light
                       → opening tail → effect → render_chain_build → em_door_update (legacy) → goto/warp
                       → snow → em_pickup_update (legacy) → indicators → Found line → em_examine_update (legacy terminal)
                       → elevator_tick (legacy) → em_enemy_update → damage/vitals → weapon → camera_update → sfx listener
                       → frame_close_out (world draw, effects, weapon spot/cone, em_hud status, Found, area title,
                         director letterbox bars, game over/continue)
NEVER CALLED (compiled, oracle-tested, dead): em_area11_interaction_host (→ panel/elevator/status runtimes, item/battery UI,
    player face host, panel message, interaction scene/runtime), em_roger_runtime/em_roger, em_door_original_runtime /
    em_door_program / em_door_transit / em_door_candidate, em_status_hub (+ em_status_draw), em_pickup_original_bind/tick,
    em_interaction_scan, em_cinematic_playback (not even in the Makefile app list), em_status_hub_ui (new, not in Makefile).
```

Original AREA11 inventory, for reference. Placement table 0x82A3C0 (21 records) and deferred registry D_0024D820[11]:

| Record(s) | Owner | Port handling today |
|---|---|---|
| r0 | room-move door 001BC350 | legacy |
| r1/r2 | fan pair 00827630 (hazard, AREA11 exit) | fabricated spin |
| r3–6 | crates 001551B0 | decoded, no oracle |
| r7 | flame 008235F0 | visuals verified |
| r8/r9 | Roger 008237E0 + equipment 001C5C90 | unwired |
| r10 | opening controller 00823E80 | live |
| r11 | manager 00823CE0 | dormant in the first visit |
| r12 | manager 008253F0 (3 beats) | kCineBeats |
| r13 | manager 008257A0 | dormant in the first visit |
| r14/15 | drums 00156620 | decoded, no oracle |
| r16/17 | truck 00823FF0 + trigger 008251E0 | fabricated |
| r18 | battery panel 00159210 | static prop |
| r19 | elevator/terminal 00827B10 | legacy examine |
| r20 | prop 001C4820 | render-only |
| deferred | 7 pickups (00219550 ×6, 0015AFA0 ×1), husk pair 00825940/00827490 | legacy |

---

## 3. Confirmed high-severity problems

All rows below were adversarially CONFIRMED. Where the verifier corrected a finding, the table shows the corrected version. Findings that duplicate each other across slices are merged.

| # | Class | Port file:line | What the original does | Fix |
|---|---|---|---|---|
| H1 (ORCH-01, W01, W22, INV-03, P11) | NOT_WIRED | `src/game/em_area11_interaction_host.c:457` (no caller); live stand-ins: `em_props.c:108/139` grate, `em_props.c:56` elevator_tick, `em_examine.c:315` | Panel 00159210 is a Use-armed actor. 00157860 aligns the player and runs its script. Callback 00157F60 posts the battery page (D_008106B0=1, B1=0x80+cost). Callback 001580C0 sets the power bit D_00810841[area] bit 7 and plays 0x3EE. Use is polled inside the player callbacks 00161020/001612D0 via 00160220, before movement. | Load the host at AREA11 scene load and tick it from the coordinator (WP-3/WP-4). Install `player_use_set_hook`/`player_pose_set_stage_hook`. Then retire grate interaction, `elevator_tick` and the examine terminal. |
| H2 (ORCH-02, W02, UI-05, INV-03) | NOT_WIRED (blocker) | `em_examine.c:315-334`; `em_game.c:5799` | Terminal 00827B10 picks between the powered script 0x82A750 and the refusal script 0x82A990 from the power bit. The panel script sets that bit. | Same as H1. Do **not** add a `battery_terminal` manifest line (that path is fabricated, see H20). |
| H3 (ORCH-04, W06, R20, INV-04, AM-23) | NOT_WIRED | `src/game/em_roger_runtime.c:186` (no caller); `em_game.c:1927` draws Roger only while opening actors are active | Roger 008237E0 at (331.7,290,192.5) is live and drawn at first control (captured RAM, owner 0x7A8830, story byte 0x8107D8=0). It dispatches on 0x8107D8: automatic polygon 0x82AB80 → encounter script 0x8283D0 (bank 96, stream stop 001FABB0, cue 29, resume 001FAE70(0)); armed talk 0x828810; departure 0x828A10 → 001B0C60(1,0,4); alternate 0x828990. | Bind the Roger owner (EM_INTERACTION_ROGER) in the host. `em_roger_runtime_load` needs message, scene, camera, player and frame workers, otherwise it fails explicitly (WP-8, after WP-7). |
| H4 (CAM-04) | NOT_WIRED | `src/game/em_cinematic_playback.c:90` (not in the Makefile COMMON list) | 0022EEF0 drives the Roger scene-1 camera, zoom and roll (roger-encounter capture, camera+0x74=25). | Add to the build and use it as the single camera-op binding for the opening, Roger and event scripts. |
| H5 (CAM-05) | NOT_WIRED | `src/game/em_camera.c:1865` (callers only in the unwired host) | 001B7B30 cases 2–5: 0018CBD0 seed → 0018D7B0 styles 5 and 1 → cam+0xA0=0x78. Used by the refusal, panel and elevator scenes. | Wired as part of H1. |
| H6 (ORCH-07, UI-01) | PARTIAL (S11b, 2026-09-23): the frame machine now opens and closes the status screen as the original does (001AE7E0 r==2 → state 3 → 0020CDC0 until nonzero → state 5 → state 1, world frozen; the st14 frame order matches), but 0020E060/0020CDC0 are bound to the legacy em_hud (`em_hud_status_open`/`em_hud_status_tick`) until WP-5 | `src/game/em_hud.c:1212`; `em_game.c:1991/1999`; host `native_status_hooks` (`em_area11_interaction_host.c:355`) leaves `other_page_tick/render` NULL, so `em_status_runtime_open` returns 0 | 001AE7E0 returns 2 on Triangle/Start (D_00810E74 & 0x810) or on a pending B0/C5 request, and anim_frame_top_b enters state 3: 001D1C50, 001D2830(3,1), 0020CDC0 until it returns nonzero, then state 5. | Frame-machine state 3 driven by `em_status_runtime` + `em_status_hub` + `em_status_draw` + `em_status_hub_ui` (WP-5). Note: `em_status_hub_ui.c` now exists (status-hub-ui lane) but is not in the Makefile. |
| H7 (UI-02, W12, R23) | FABRICATED (self-flagged "PORT STAND-IN") | `src/game/em_hud.c:2206`; `em_game.c:5076-5078`; `em_pickup.c:509` | The take path 001B6EA0 → 001C47A0/4720/4760 posts B0=1/2/3 with B1=type. 001AE7E0 then auto-opens the status screen, and 0020CDC0 case 0 maps B0/B1 to a page and message (battery 0x1B–0x1D → ITEM, message 3). There is no in-world toast. | Delete `em_hud_found_show/render`. Post the B0/B1 request into `em_status_runtime` (WP-5/WP-6). |
| H8 (UI-03, ORCH-20, SI-15) | FABRICATED | `src/game/em_game.c:5505` (Continue literal 75/60/4/120/4-6, copied from the demo fixture at :5695) | Continue: 001AC070 → 001ACEC0 route 1 → 001AD230 → 001AF2C0, which clears the whole 0x640-byte block at D_00810700 (also progress flags such as D_00810811/D_00810841), copies the restart-area record, and sets health 100, mag 30, reserve 60, battery 0. | Apply the same 001AF2C0 reset `em_game_install_new` uses, clear the progress flags, and reload the restart area (WP-1). |
| H9 (UI-04) | FABRICATED | `src/game/em_hud.c:1758` (strip text, assumed rows, hard-coded "x01") | 0020CDC0 phase 3 dispatches to real pages: 0020EE50 ITEM, 0020F950 MAP, 00211970 SPR4, 00214020 DATABASE. | Route through `em_status_page` + `em_item_root/ui`. Leave pages with no recovered implementation unreachable rather than showing invented content (WP-5). |
| H10 (ORCH-10, CAM-17, AM-05/ORCH-11, INV-06/07) | APPROXIMATION (corrected: keyframe coordinates and durations *are* the original op00/op02 records; the problem is how they are executed) | `src/game/em_director.c:28` (kCineBeats), `:175-182` (em_sfx_play 0x97/0x99) | Manager 008253F0 starts scripts 0x8294C0 / 0x829A40 / 0x829CC0 via 001BA1A0 and polls 001BA1F0. These scripts contain op07 sub8 enter (D_008101E4=1, 3B8D=2, skeleton bind 001B81D0, zoom 0); op06 on flag 0x3B; 0x16/0x18/0D sub2; sine-eased op00 kind-1 blends; and an op07 sub4/5 teardown (sub5 sets D_00810758[0x3B]=0xFF). Op0C sub0 is the **message** op 001B7D60: lines 0x97/0x99 show a text line (118/198 frames) and push VOICE.DAT cues 150/149 through 001FD580 → 001FA5A0. Beat 0 completion calls 001C4760(1,1). | Run the three scripts through `em_script` with shared op bindings (op00 kind 1 sine ease, op07 sub8, op0C → message service with voice push). Delete kCineBeats, the em_sfx_play call and CINE_BAR_FADE. Implement 001C4760 (WP-10). |
| H11 (P09) | MISSING | `src/game/em_player.c:870` | 00174AC0 (byte-matched): walking with speed > 0.5, gait ≥ 2 and \|wrap Δ\| > 2.3561945 → +0x1F0=7. 0017C030 case 7: turn clip variant 2/4, blend 4, SFX 0x137. Case 6: follow-up clip, zero speed, yaw += π. 001612D0 case 2: surface effect every 8 ticks, resume. | Implement mode 7/6 and the 001612D0 resume. Export the clips. Add an oracle (WP-15). |
| H12 (P10) | NOT_WIRED (port-only invalidation) | `src/game/em_player_pose_host.c:83`; callers `em_player.c:268,307,334,355,391,509,612,664,830` | 00182DF0 always re-seeds the default channel state on release. The original has no permanently invalid pose. Once invalid in the port, foot-stop, fidget and entry-return stop working, and `player_pose_acquire` returns -1, which **faults the interaction runtime**. | Re-seed at the default clip on every legacy release, as 00182DF0 does. This is a prerequisite for H1 (WP-2). |
| H13 (P20) | FABRICATED (corrected: the walk target is the near-side staging point) | `src/game/em_player.c:265`; `em_game_internal.h:119` WALK_SPEED 15 | 001BBE40 (byte-matched) snaps yaw and translates the player instantly with 00182F90 to door ±5 − 5·(sin,cos)(yaw), then starts the script (player clips 0x45/0x43, waits 90/70). | Replace with `em_door_transit` + `em_door_program` (WP-7). Interim fix: snap via 00182F90 semantics. |
| H14 (W07) | NOT_WIRED | `src/game/em_door.c:1` (legacy live); `em_door_transit_active` is defined in legacy `em_door.c:1850` | 001BC350 lifecycle → 001BBE40 kickoff → 001BC0E0 pump → 001BC240/001BC150 commit → 001BC290 close. | Bind `em_door_original_runtime` + transit + program, and use `door_original/model.emdl` (WP-7). |
| H15 (W10, INV-12, ORCH-08) | NOT_WIRED (corrected: `em_pickup_original_bind` is called only by tests, not even by the host) | `src/game/em_pickup.c:559` (legacy countdown), `:751` (bind) | 0015AFA0/0015AE20 and 00219550: wait for the armed bit 4, start take script 0x2482C0 / 0x248480 (0x266620 / 0x2667E0 for 00219550), wait for 001BA1F0; op-9 take; 00219550 completion plays cue 0x194 and sets taken-bit persistence. | Bind the pickups into the host's interaction scene, tick them from the coordinator, and remove `pickup_trigger_scan` and the countdown (WP-6). |
| H16 (W16, INV-10) | FABRICATED | `src/game/em_truck.c:263` (AABB trigger, 65-frame fall, -0.9 tumble) | The trigger 008251E0 **only starts camera script 0x8292C0** (gate D_00810792==0, a two-band X/Z union, D_008102B5<2) and then sets D_00810792=1. The truck 00823FF0 arms when the player **stands on it** (the D_008104C4 actor kind 9), shakes for 47 frames, then falls with beats up to 119 frames and X+Y velocity; sound 0x454 plays at frame 8 and 0x455 at frame 110; 24 FX spawns; 3 rumbles; the end state is D_00810792=0xFF. | Freeze the truck static (drop the invented trigger and fall) until 00823FF0/008251E0 are translated with an overlay oracle (WP-1 now, WP-12 later). |
| H17 (R01) | NOT_WIRED | `src/game/em_scene.c:167`; `em_game.c:1889-1892`; the live `scene_snow/scene.txt` has no fog line | 001D8FD0 (byte-matched) loads the rig record key 0x0B00 (near -209, far 304, RGB 48,48,48). 001D1C50 restores fog every frame. The face PRIM has FGE=1. The snow test's fog constants agree. | Export fog from the record (not the -208 from the old backup manifest). Check fog_apply against GS F=255·(far−z)/(far−near) (WP-1 quick fix, WP-13 verification). |
| H18 (R02) | APPROXIMATION of invented lighting (corrected; medium-high) | `../Extermination/tools/export_props.py:404` `attr_color`, `export_level.py` `attr_to_color` | Actors use the default mode 0 of 001D89D0: per-vertex rig from 001D8130/001D8340 plus the point-light fold. No original path computes 0.30+0.70·max(N·L,0). | Re-export parachute, truck, door_m03, husk pair, crate, egg, item_13, item_0b and gibs with real normals and flags=0, and remove the stand-in branch (WP-13). |
| H19 (AM-01) | INACCURATE pitch model (corrected from FABRICATED) | `src/game/em_sfx.c:195`; WAV rates from `audio_export.py:462` `tone_rate` | For A0 events, 00115850 stores bend 0x40 before 00117918. The table anchor is D_00241D70[0xD0]=4096. The legacy rates are ×1.531 (+118 steps), about 7.4 semitones sharp and 35% shorter. Cue 0x3EF (oracle): 10101.56 Hz, not 15480. | Re-export every registry id through the verified pitch path used by `export_startup_audio.py`/`export_area11_sfx.py`, storing an integer SPU pitch. Retire `tone_rate` (WP-14). |
| H20 (INV-01) | FABRICATED | `src/game/em_pickup.c:58` (constant 1°/frame spin) | 00827630 is a timed spin cycle: 60-tick wait, ramp to 0.349 rad/f, hold, ramp down. Record 1 plays 0x451 unless D_00810788==1. Record 2 player box X(318,340) Y(280,320): hit (+0x224=5.0, byte0=3, +0x0F=6); at Z<156, 001B0C60(1,1,4) if D_00810758==0xFF, else D_008107D8 \|= 0x80 (Roger departure trigger). | Translate 00827630 with an overlay oracle (WP-11). Interim: stop the invented spin. |
| H21 (INV-02) | MISSING | `src/game/em_game.c:5529` (level-exit arms "pending") | There are two AREA11 exits: fan 001B0C60(1,1,4), and Roger departure 0x828A10 → 001B0C60(1,0,4). The area-change request is D_008106B5..B8 → 001AD010 → sub-state 5 → frame case 0. | Implement the area-change consumer (ORCH-06) and targets for AREA01 sub 1 (not exported) and sub 0 (scene_drawbridge) at entry 4 (WP-11). **S12a: the consumer is live** (001AD010 → 001ADF50 native area read → state-0 rebuild → 001B07C0(0) from the exported D_0024D650; `em_scene_request_area_change_001B0C60` is the translated request, exercised by EM_AREA_CHANGE_TEST with AREA11 0x0B/0/0). Still missing: the fan/Roger requests (WP-11/WP-9) and the AREA01 targets (the area read faults for any area but 0x0B/0). |
| H22 (AM-06) | PARTIAL (S11b: the core emits all five calls; 001FBC50 is bound to `em_sfx_stop_all`; 001FABB0, 00119828 ×2 and 001FAE70(1) are reported no-effect bindings, so the music keeps playing; severity medium, reached on every status open) | `src/game/em_scene_bindings.c` (status workers) | anim_frame_top_b state 1, r==2: 001FBC50 stop-all SFX, 001FABB0 stop streams, 00119828 ×2. On exit, 001FAE70(1) restarts cue 25 with a 270+rand fade. | Comes free with WP-5. `em_status_frame` emits these. Update the stale note at `em_sfx.c:477`. |

Confirmed problems in non-high findings that other rows depend on:

- The port's pose invalidation (H12) makes host `acquire()` fault. **Fix H12 before H1.**
- The host binds only PANEL and ELEVATOR (`em_area11_interaction_host.c:515-518`). **Do not delete the legacy door, pickup or examine scans until the DOOR, PICKUP and ROGER owners are bound to the shared scene** (W22 correction).

---

## 4. Roadmap to a faithful first level

The order follows dependencies and impact. "Removes fabrication" marks packages that delete live invented behavior.

### WP-0 Lead housekeeping for finished lanes (no code risk)
- **Scope:**
  - Add `src/game/em_status_hub_ui.c` to COMMON.
  - Add Makefile targets `test-status-hub-ui-reference` (`python3 tools/test_status_hub_ui_reference.py`, about 84 s) and `test-roger-media-reference` (needs `tools/export_roger_media.py` assets).
  - Re-run `tools/test_area11_interaction_host.py`, because `EmOpeningDialogue` gained `loaded` (roger-media lane). The face-host lane's PASS may predate that change.
  - Commit the three lanes together with the uncommitted host, test and tool diffs.
- **Lane state:**
  - face-host: DONE. Fixture corrected so `cinematic_face` runs after `first_battery`. B81D0/D0C70/CA770/FD950 wiring rechecked. 9/9 scenarios pass.
  - roger-media: DONE. Clock off-by-one fixed per 001FD790/001FD950/001FDB80. 4,143 ticks match.
  - status-hub-ui: DONE. v2 EMHS exporter. 167 streams / 19,968 commands against executed 00209DF0. Clock and trail are now caller-owned.
- **Removes fabrication:** no.
- **Status: DONE.** The three lanes are committed (face-host 62c58ed, roger-media 8efed7d, status-hub-ui e1d88a4). `em_status_hub_ui.c` is in COMMON and both Makefile targets exist. `tools/test_area11_interaction_host.py` was re-run on 2026-09-22 after those commits: all three scenarios PASS.

### WP-1 Neutralize live fabrications that need no new infrastructure
- **Scope. Each item is independently committable:**
  - **H8:** Continue applies the 001AF2C0 reset (the same values as New Game) and clears the D_00810700 block progress flags. **DONE** (9d4a631). The 001AF2C0 inventory seeds (item counts 0/5/7/0x17 = 1, 0x10 = 2, magazine packs 2) are now applied by `em_pickup_reset` and checked against the executed original (cleanup-game lane, uncommitted at the time of writing).
  - **H10 partial:** delete `em_sfx_play(0x97/0x99)` in `em_director.c:175-182`. **DONE** (da41660).
  - **H16:** truck static (remove the AABB trigger and fall), leaving a TODO. **DONE** (da41660).
  - **H20:** stop the constant spin on type-0x13 props. **DONE** (da41660). The static pose now also applies 00827630's init rot.z (±π/4 by record +0x03) when the manifest line carries `owner 0x827630 <flags2>` (cleanup-game lane). PENDING: the local manifest lines do not carry the suffix yet, so the live fans are still yaw-only.
  - **H17:** add the fog record (−209, 304, 48,48,48) to the scene_snow manifest via the exporter. **DONE** (port da41660, exporter decomp 7ae3b07; `test-area11-fog-reference`).
  - **AM-22/INV-23:** do not auto-start manifest BGM on the New Game path. **DONE** (9d4a631).
  - **SI-02/AM-17:** route `wpn_rand` and `footstep_rand5` through `em_random_next()` with the original bit extraction (00179B90: rand()&7 folded). **DONE** (7095fd6, `test-player-random-reference`).
  - **R03/R04: REFUTED — do not gate the cone off.** The original draws the flashlight cone: 0017A970 sets the draw enable D_008106C7 with D_00810D3C, 00188ED0 calls 00187780 while it is set, and 00187780 calls 001D9530, the cone-shell draw (unless area flag 001B0070() & 0x20000000). Recorded in 7095fd6. The port's per-pixel spot term is still a stand-in (`em_gfx.h`).
  - **R09/ORCH-27:** clear to black until the original clear is found. OPEN (no commit addresses it).
  - **ORCH-03, W24, INV-14, INV-15:** delete the dead `battery_terminal` path and the type-0x11 hook, and rename `have_battery` to the opening-complete byte D_00810811. **DONE:** `battery_terminal` and the rename in 9d4a631; the type-0x11 take hook and `em_game_set_battery` / `em_game_has_battery` removed by the cleanup-game lane.
- **Originals:** 001AF2C0, 001B7D60, 001D8FD0, 00122BB8, 00179B90.
- **Verification:**
  - The existing suites must stay green.
  - Fog: capture pixel/fog-coefficient check against the opening GS dump (fog constants 255/2048/151.11/−0.497 already appear in `test_snow_particles_reference.py`).
  - Continue: add a decomp-C/instruction check of 001AF2C0 field writes.
- **Depends on:** nothing.
- **Removes fabrication:** YES.

### WP-2 Pose-source lifetime (H12)
- **Scope:** replace permanent `player_pose_invalidate` with a re-seed at the default clip when each legacy path releases (aim, R2, melee, door, examine, interact, hit, low health), mirroring 00182DF0. In the long term, give aim, melee and door their own raw-channel workers.
- **Originals:** 00182DF0, 00182D70, 00161020, 001612D0, 0017B910.
- **Verification:** `test_player_pose_host_reference.py`, `test_player_pose_live_reference.py`, `test_player_foot_stop_reference.py`. Add a new case: aim, release, then run stop, and assert the foot-stop still fires.
- **Depends on:** nothing. **Blocks:** WP-4 (acquire faults on an invalid pose).
- **Removes fabrication:** yes (port-only state).
- **Status: DONE** (7095fd6). Legacy releases re-seed through 00182DF0's path; `test_player_pose_host_reference.py` executes 0x182DF0 and includes the aim-release-then-stop case.

### WP-3 Live scene coordinator (backbone)
- **Scope:**
  - Port anim_frame_top_b (0x001AE040) states 0–6 with the byte-matched 001AE7E0 classifier as the single dispatch point. Case 0 returns without a world frame. Case 1 checks D_008106B8/B9 → 001AD010/001AD140.
  - Build a native pooled-actor list walked at the 001AFD70 positions:
    - gameplay 001AE5E0 order: player 0015BCF0 → 001D1C50 → 001C1D00 → 001AFD70(0) → 0015C160 → 001F0360 → 0018B9C0
    - cutscene 001AE6B0 order: 001AFD70(1) → player → 001AFD70(2)
  - Move truck, director, grate, enemies and effects into owners on the list.
  - Move the em_hud and game-over early-outs out of `gameplay_frame` (done in S11b).
  - Wire the letterbox gate `em_frame_screen_fade_gate` (SI-17).
- **Originals:** 0x001AE040, 001AE7E0, 001AE5E0, 001AE6B0, 001AFD70, 001AD010, 001ADF50.
- **Verification:** 001AE7E0 is byte-matched, so an ELF-instruction oracle over all inputs is cheap. `test_status_frame_reference.py` already executes 0x1AE040..0x1AE5E0 for the status branches; extend it to states 0/1/2/6. The frame order has no oracle today; compare the ordered call trace against the NEARMISS C bodies.
- **Depends on:** nothing. **Blocks:** WP-4…WP-12.
- **Removes fabrication:** removes the port-ordered update list (ORCH-14/15/16).
- **Status: IN PROGRESS.** Phase 1 cores landed (f519488, ac74c14, 61796a0). S8 (legacy-mode wiring) landed: the slot-0 task runs the translated 001ACEC0/001AD250/0x1AE040 cores and the letterbox gate is fed 3B90/C4 (SI-17 wired; C4 is still always 0). S9 landed: the state-0 tick returns without a world frame (the port's same-tick fall-through is removed; no frame-index constant needed re-baselining, since they all count world frames). S10a landed: both world-frame variants run as the translated 001AE5E0/001AE6B0 cores with their stages in the original order; the port-ordered monoliths gameplay_frame/cutscene_frame are deleted (ORCH-14/15/16 closed for the stage order). S10b landed: the native actor pool is live. State 0 spawns the AREA11 roster (deferred group, placements, weather and title nodes), the first player stage spawns the player's attachment and effect children, and every 001AFD70 position walks the pool node by node in the original list order (frame-order traces match the original node for node, callback and record, except the allow-listed record 13, footstep and opening-actor nodes). Each owner node runs the port's legacy code for it or is an explicit no-port-code node; scenes without an original roster keep the old block as one legacy_world node. The canonical D2 progress region exists (taken bits and D_00810CA4..CA7 migrated from em_pickup). S11a and S11b landed together (lead decision D4): spad 3B8D/3B91/3B92 have one storage (g.frame_selector, the opening's private skip promotion and `s.cinematic_ready` are deleted; 001AE6B0 promotes 3B91), the input words D_00810E74/E70/E50 are written every tick in the original layout, and the frame machine acts on the classifier: START/TRIANGLE opens the status screen through states 3/5 with the world frozen (legacy em_hud as the interim 0020E060/0020CDC0; H6 PARTIAL), and death writes B9 at the player stage (0015CF90), which leads at fade 2 to 001AD140 → the byte-matched 001AD4E0 → 001ADF00 → the interim 001AC070 continue task. The em_hud self-toggle, its menu-inhibit copy (B3 is canonical, written at the player stage by an interim stand-in for 0015BA50's tail) and the status/game-over early-outs are deleted; the unported classifier arms fault. S12a landed: New Game (and Continue) register the task with a cleared record and run the original load arms 001AD1A0 → 001AD230 (001AF2C0) → 001AD360 (the intro movie at step 1) → 001ADF50 (the native area read and the load veil) → the state-0 rebuild, whose 001B07C0(0) places the player from the exported D_0024D650 (byte-matched translation, oracle-tested; the manifest spawn is gone for AREA11); game_load_task is retired; the area-change consumer 001AD010 → +9 = 5 is live; the weather node reads the canonical D_008106C8 (001B0250); record 13's manager 008257A0 is translated and frees itself on the second world frame, so the New Game census at first control is the original 49. See SCENE_COORDINATOR_DESIGN.md section 6 "Phase 2 status".

### WP-4 Install the AREA11 interaction host (panel, battery, power, elevator, face)
- **Scope:**
  - Call `em_area11_interaction_host_load` from the AREA11 scene arm.
  - Tick the panel and elevator owners from the coordinator.
  - Install the Use and stage hooks (`player_use_set_hook`, `player_pose_set_stage_hook`) so Use is polled inside the player callbacks (00160220).
  - Render the panel message and face host.
  - Retire `grate` interaction, `elevator_tick`/`elevator_descent_begin` and the `em_examine` terminal for scene_snow. Keep the panel mesh, cell18 collision and indicators, which are verified.
- **Originals:** 00159210, 00157860, 00157F60, 001580C0, 00827B10, 00828050, scripts 0x82A750/0x82A990, 001B7B30, 0018CBD0, 0018D330, 0018D910, 001B81D0, 001D0C70, 001FD950.
- **Verification:** `test_area11_interaction_host.py`, `test_panel_reference.py`, `test_panel_message_reference.py`, `test_elevator_reference.py` (2,520 cases), `test_elevator_commands_reference.py`, `test_camera_interaction_fixture.py` (panel/animation_ee.bin and elevator/refusal captures), `test_battery_ui_reference.py`, `test_face_allocation_reference`. New work: a live-path smoke test that drives New Game → battery pickup → panel → elevator and checks the power bit and the elevator descent against build/startup-reference/panel and elevator/completed.
- **Depends on:** WP-2 and WP-3. Battery-page UI needs WP-5 (or the host's battery_open route, which does not need the other_page hooks).
- **Removes fabrication:** yes (examine terminal pivot W04, grate stand-in, legacy elevator conflation W05). **Unblocks level progression (H2).**

### WP-5 Status stack live (frame-machine state 3)
- **Scope:**
  - Replace the interim 0020E060/0020CDC0 bindings (`em_hud_status_open`/`em_hud_status_tick`, S11b) and `em_hud_render` with `em_status_runtime`.
  - The runtime owns the UI+20 clock and zeroes it in 0020E060; it resets the shared trail on 0020E020.
  - Call `em_status_hub_ui_prepare/render` at EM_STATUS_HUB_DRAW.
  - Bind the `other_page_tick/render` hooks so open is not refused.
  - Wire `em_status_page`, `em_item_root/ui` and `em_battery_ui`.
  - Deactivate the hub/ITEM texture slot on each switch.
  - Handle pickup B0/B1 auto-open.
  - Delete the invented `em_hud` pages, Found line and hover cue 4.
  - Keep 0020A7A0 and the model workers 0020E250/E3A0/E1E0/E6F0 as explicit required workers; do not substitute `em_hud_background_sprite`.
- **Originals:** 001AE7E0, 0020CDC0, 0020D930, 00209DF0, 00208AD0, 00209280, 00209860, 0020E060, 0020E020, 0020E0C0, 0020EE50, 002149F0.
- **Verification:** `test_status_frame_reference`, `test_status_page_reference`, `test_status_hub_reference` (17,520), `test_status_draw_reference`, `test_status_hub_ui_reference`, `test_item_root/ui/trail/geometry_reference`, `test_battery_ui/pickup_reference`, plus a capture compare against build/startup-reference/status-hub. Still missing: an oracle for 0020A7A0 and for the model workers.
- **Depends on:** WP-3.
- **Removes fabrication:** YES (H7, H9, UI-06/07/09, R05 orbit if the menu scene is redone). Also fixes the H22 audio.

### WP-6 Pickups and the single Use arbiter
- **Scope:**
  - Bind all 7 AREA11 pickups via `em_pickup_original_bind/tick` into the host's interaction scene.
  - Publish the owners and resolve Use through `em_interaction_scene_scan_checked`: one pass over the published list, lowest score wins, result 2 commits immediately.
  - Remove `pickup_trigger_scan`, the 2-frame take and the flat `inventory_add`, so the subtype families (maps, keys) are correct (W11).
  - Apply the 001C40B0 case-0x10 magazine write directly (W13).
- **Originals:** 00184BA0, 00183EF0, 0015AFA0, 0015AE20, 00219550, 001B6EA0, 001C47A0/4720/4760, 001C40B0.
- **Verification:** `test_interaction_scan_reference`, `test_interaction_pickup_reference`, `test_pickup_owner_reference`, `test_pickup_motion_reference`, `pickup_original_test`.
- **Depends on:** WP-4 (host) and WP-5 (status requests).
- **Removes fabrication:** yes (legacy take, Found, W11, W13, W14).

### WP-7 Original door (room move)
- **Scope:**
  - Bind `em_door_original_runtime` + `em_door_program` + `em_door_transit` + `em_door_candidate` for the AREA11 door, publishing it in the shared scene.
  - Draw `door_original/model.emdl` from the runtime palette.
  - Commit through 001BC150. For this door the id bit 7 is clear, so the commit is a B8=2 room move to spawn entry 2 (side 0) or entry 1 (side 1) from D_0024D650[11], writes the entry byte 0x810702, and keeps actors, overlay and audio.
  - Camera: `camera_interaction_retarget_distance_area11(-20)` (CAM-06), and re-seat via 001B0080 (camdist −46.8) instead of CAM_DIST 33 (CAM-07).
  - Delete the legacy MOVE-TO walk, walk-out constants and `door_m03` for AREA11.
- **Originals:** 001BC350, 001BBE40, 00182F90, 001BC150, 001BC290, 001AEDE0, 001B0460, 001B0080, 00183250.
- **Verification:** `test_door_original_reference` (5,662), `test_door_original_runtime.py`, `test_door_transit_reference`, `test_door_program_reference/runtime`, `test_door_candidate_reference`. New: a spawn-table placement check against captured RAM entries 1/2.
- **Depends on:** WP-3, and WP-6 for the Use arbitration.
- **Removes fabrication:** YES (H13 walk, synthesized re-place, CAM_DIST).

### WP-8 Single message service (001FCA10)
- **Scope:**
  - One native message machine over the D_002821B0/B4/B8/BC state, ticked at main-loop step F.
  - Route the opening, director, examine refusal, panel and Roger requests into it. Handle modes 2/3/4, voice pushes (001FD580 → 001FA5A0), the stream table (001FD4C0) and `D_008106F5` modes.
  - Use `em_hud_subtitle` with the record's fill, outline and skew.
- **Originals:** 001FCA10, 001B7D60, 001FDB80, 001FD790, 001FD950, 001FD580, 001FD6A0, 001FD4C0, 001FA5A0.
- **Verification:** `test_roger_media_reference` (4,143 ticks), `test_panel_message_reference` (3,156 callbacks). Add mode-1/2 gates and a voice-cue case.
- **Depends on:** WP-3. **Blocks:** WP-9 and WP-10.
- **Removes fabrication:** partly (UI-15 plain-text refusal).

### WP-9 Roger encounter live
- **Scope:**
  - Bind the Roger owner (callback 0x8237E0, kind 10) in the host.
  - Supply the message (WP-8), camera (`em_cinematic_playback`; add it to the Makefile), player/face (`em_player_face_host` via the host) and frame workers.
  - Install the media, the clock and the resume stream (cue 29 during the encounter, 001FAE70(0) resume).
  - Draw Roger and the equipment child 001C5C90 through the rig path.
- **Originals:** 008237E0, 00823910/00823B70/00823C40, scripts 0x8283D0/0x828810/0x828A10/0x828990, polygon 0x82AB80, 0022EEF0, 001B7B30, 001FABB0, 001FAE70.
- **Verification:** `test_roger_reference` (6,912 controller cases), `test_roger_pose_reference`, `test_roger_encounter_reference`, `test_roger_cinematic_reference`, `test_cinematic_playback_reference`, `test_roger_media_reference`, the face host tests, plus a capture compare against build/startup-reference/roger-encounter.
- **Depends on:** WP-3, WP-4, WP-8.
- **Removes fabrication:** no. Adds a missing encounter.

### WP-10 Director beats as scripts
- **Scope:**
  - Run manager 008253F0's three scripts through `em_script`, using the same op bindings as WP-9: op00 kinds 0/1/5, op07 sub8/sub4/sub5, op06, 0x16, 0x18, 0D sub2, op0C via WP-8.
  - Implement 001C4760(1,1).
  - Letterbox via `em_frame_screen_fade_start`.
  - Delete kCineBeats, CINE_BAR_FADE and the chase re-seat.
  - Selector≠0 during beats blocks the menu (ORCH-19).
- **Originals:** 008253F0, 00825500/00825600/008256D0, 001B8FC0, 001B82D0, 001BA080, 001C4760, 001AEBE0.
- **Verification:** `test_script_reference`, `test_cinematic_playback_reference`. New work: an overlay-instruction oracle for the manager gates and a camera track compare (ORCH-13).
- **Depends on:** WP-3, WP-8, WP-9 (shared op07 bindings).
- **Removes fabrication:** YES.

### WP-11 Fan pair 00827630 and the AREA11 exit
- **Scope:**
  - Translate 00827630: spin cycle, 0x451, hit box, the Roger 0x80 bit and the exit request.
  - Port the area-change consumer: D_008106B5..B8 → 001AD010 → 001ADF50 load wait → frame case 0 → 001AFCA0 → 001B07C0 spawn placement (ORCH-06). **Done in WP-3 S12a** (bind the fan's `w_001B0C60` to `em_scene_request_area_change_001B0C60`); the AREA01 targets remain.
  - Export the targets AREA01 sub 1 and sub 0 at entry 4.
- **Originals:** 00827630, 001B0C60, 001AD010, 001ADF50, 001AFCA0, 001B07C0, 001E7780.
- **Verification:** new overlay-instruction oracle for 00827630 (pattern: `test_roger_reference` overlay loading); RAM captures for spawn placement.
- **Depends on:** WP-3; the Roger departure exit also needs WP-9.
- **Removes fabrication:** YES (H20). Adds the exit (H21).

### WP-12 Truck set piece
- **Scope:** translate 008251E0 (camera script 0x8292C0) and 00823FF0 (stand-on arm, shake, fall beats, Z-only carry, sounds 0x454/0x455, FX, rumble, persisted 0xFF), and publish its hull instead of the port's AABB carry (P19).
- **Verification:** new overlay oracle.
- **Depends on:** WP-3, WP-10 (script host).
- **Removes fabrication:** YES (finishes H16).

### WP-13 Render fidelity
- **Scope:**
  - Fog verification (H17).
  - Actor re-exports with real normals (H18).
  - `door_original` model (W09/R21).
  - Decode the level alpha/test/blend state (R10), the level vertex-colour scale (R11), and TEX1/CLAMP (R25).
  - The 001D8270 fold gate and +0x98 light node (R13).
  - Menu identity camera and yaw π+t (R05/R06).
  - Additive actor RGB (R07).
- **Verification:** `audit_opening_lighting.py`, `test_point_light_reference`, plus GS-dump comparisons from opening_gs.bin.
- **Depends on:** WP-3.
- **Removes fabrication:** YES (the stand-in light and menu orbit).

### WP-14 Audio
- **Scope:**
  - Re-export the pitch (H19) and Q14 gains (AM-02).
  - General A0 sequencer with loop-aware VAG voices, SPU2 ADSR and Gaussian interpolation (AM-03/26/27).
  - Area-scoped banks from D_00264A70/D_00264AD0 with absent entries (AM-15). This removes the office 0x7D8 stand-in (AM-14).
  - UI cues 0/1/2/5/0xB/0xD (AM-07).
  - 00117428 allocator (AM-18).
  - 001FC3C0 looped positional voices for the flame 0x413 (AM-12).
  - Full 001FAE70 branch selection (AM-21).
  - Reverb bus (AM-04).
- **Verification:** `test_area11_sfx_reference.py` pattern, extended to every exported id. New: 001FBF50 gain oracle (AM-19).
- **Depends on:** partly WP-4/5/6 for call sites.
- **Removes fabrication:** YES.

### WP-15 Player locomotion completeness
- **Scope:**
  - Reversal skid (H11).
  - Ordinary display from `EmPlayerPose` + 0017B660 instead of matrix lerps (P12/P13/P27/P29).
  - Footsteps from the remaining clock and 00187350 (P14/P15).
  - 001764E0 walk-gated probes and 001760C0 (P16).
  - 00175900 floor service and 001796C0 fall state (P17/P18).
  - Aim release states (P24), R2 stance via 001703E0 and R1 via 0016FCF0 (P25/P26).
  - Single-tick melee chain (P28).
- **Verification:** extend the `test_player_*_reference` oracles. New oracles for 0017ABA0, 00187350, 001764E0, 00175900.
- **Depends on:** WP-2.
- **Removes fabrication:** yes (P14 timing, P26 attribution).

### WP-16 Camera completeness
- **Scope:**
  - Run the 0018D330 prepass for every style and consume the 0x5A/0x6D/0x60 bits (CAM-10).
  - 001921D0 tail for codes 1/3 (CAM-08).
  - 00195130 case 0xB AREA11 specials (CAM-09).
  - L1 via 00193EB0 gating plus 001936E0 (CAM-11).
  - Mode-8 settle and the 001B0460 entry seat, deleting opencam (CAM-18/19).
  - 00197490 aim release (CAM-16).
- **Verification:** `test_camera_probe_reference`, `test_camera_interaction_fixture`. New: a gameplay-frame fixture from playable_ee.bin.
- **Depends on:** WP-3.
- **Removes fabrication:** yes (CAM-06/07/08 stand-ins).

### WP-17 Input and startup
- **Scope:**
  - 001B5940 analog block in `frame_input_read`: D-pad → stick with gait 3 (SI-03/P22), stick → D-pad bits (SI-04), quantize (SI-05), repeat word 0x810E78 (SI-06), raw axes (SI-08).
  - New Game movie skip on START only (SI-09). Since S12a the movie is requested by the game task itself (001AD360 step 1: D_00275C78 = 0, D_00821058 = 1 → `em_frontend_movie_request`), and the skip mask is 002036E0's with spad 3B90 = 2.
  - Black plus the 001ADF50 loading shimmer instead of fade-in 4 (SI-10). **S12a: PARTIAL.** 001ADF50 runs natively (001AED80 clear, the veil state machine 0021B180/0021B550/0021B840, 001AEDB0 full black) and the state-0 rebuild runs 001AEE40(4), as captured; the veil's particles (0021B1B0/0021B500) are not drawn, so the load shows black.
  - Attract completion (SI-11).
  - RNG seed: cold boot is 1, not 0x45 (SI-01).
- **Verification:** new oracles for 001B5940 and 001AC480.
- **Depends on:** nothing.
- **Removes fabrication:** YES (SI-01/08/09, P22 aim-only d-pad).

### WP-18 Enemies and hazards verification
- **Scope:**
  - Oracles for crates 001551B0 (alert, leap, husk rebind 0x22/0x29; drop the gibs; W19/W21/INV-19) and drums 00156620 (FX, no debris; W20).
  - Husk partner taken-bit persistence and 0x426/0x427 (W17 residue).
  - AREA11 crawler mesh param (W18).
  - Flame contact damage 00823580 through the +0x224/+0x0F/+0x00 contract, not the 0x4000 mailbox (INV-17/INV-28).
- **Depends on:** WP-3.
- **Removes fabrication:** yes (gibs, the 0x7D8 stand-in).

---

## 5. Refuted and uncertain items

### Refuted (do not re-report)
- **ORCH-05, "the AREA11 door is the level exit; route it through an area transition": REFUTED.** 001BC150 with door id bit 7 clear (captured RAM, all AREA11 captures) is a **same-area room move** (B8=2) to spawn entry 2 or 1. It is never an area change. The surviving defect is medium: the port re-places at a computed point about 10 u off with the wrong yaw and never writes 0x810702 (tracked in WP-7).
- **W17, "the door husk pair should be an active set piece in the first level": REFUTED.** 00825940 stays dormant in state 0x64 while event flag 0x30 (D_00810788) is clear. Every AREA11 capture shows the flag at 0, creature state 0x64, and the partner in state 1. The inert creature is faithful for the first visit. The residue is low: missing partner taken-bit persistence (it respawns on reload), the approximated partner shot reaction (FX 0x80000045, 0x426/0x427, stays drawn), and the invisible model-0x7A child.
- **R03/R04, "the boot ELF draws nothing for the flashlight; gate the spot and cone off": REFUTED.** 0017A970 sets D_008106C7 with D_00810D3C; 00188ED0 calls 00187780 while D_008106C7 is set; 00187780 calls 001D9530, which draws the cone shell (chunk27 meshes 0x10/0x11/0x16) under the gun light matrix, skipped only when 001B0070() & 0x20000000. The original cone is not translated; the port's spot term is a stand-in.
- **INV-08, "manager 1 (00823CE0) second cinematic reached in the first level": REFUTED.** It only waits on flag 0x30. Within AREA11, only its own script sets that flag, after the cinematic has already started. The only op06 sub0 record that sets it to 1 is in AREA17.BIN. This is revisit content, low priority. INV-09 (manager 3, 008257A0) has the same D_00810788 gate and is therefore also revisit content (not adversarially checked, but it goes dormant when D_00810788==0 per its C).
- **Partial corrections to confirmed items. Do not repeat the original wording:**
  - ORCH-10/CAM-17: kCineBeats values *are* original record data.
  - ORCH-07: the `em_status_hub_ui.h` declarations are now defined.
  - UI-05: the request global is B1=0x80+cost via 00157F60, not C5.
  - AM-01: the problem is a wrong derivation, not an invention.
  - R02: an approximation, not a fabricated feature.
  - P20: the staging point is on the near side.
  - W10: bind is test-only.
  - W22: fades do happen live via the legacy door. The host binds only panel and elevator.
  - AM-06: medium.
  - INV-02: there are two exits, not one.

### Uncertain or unverified. Evidence that would settle each
| Item | Open question | Settling evidence |
|---|---|---|
| face-host limitation | B82D0 op7/8/11/12 immediate route calls B81D0 while 3B8F==0; does the AREA11 Roger encounter take it? | Trace ev+0x14 in scripts 0x8283D0/0x828810 in the oracle; roger-encounter capture of 70003B8F. |
| face-host limitation | Priority order in cinematic_player (deferred foreign bank vs shared animation vs idle) | Show from 0015B130/0015BA50 whether a shared animation and a foreign-bank request can co-occur. |
| roger-media boundary | D_008106F5 modes 1/2 and voice lookups 001FD580/001FD6A0 are never exercised (voice −1) | Oracle case with a non-negative voice record (e.g. director lines 0x97/0x99, voice cues 150/149). |
| status-hub-ui boundary | Unsupported ammo selectors (primary≠2, secondary>4) use an inherited register as TEX0 | Capture with those selectors, or proof they are unreachable in AREA11. |
| R01 | Is the status-menu actor draw fogged? | GS dump of the status-hub capture (FGE bit on the menu actor PRIM). |
| R11 | Level vertex colour 1.0 → GS 128 or 255? | Compare RGBAQ in opening_gs.bin against record colours. |
| R09/ORCH-27 | Original frame clear colour | Locate the clear in 001D1C50/001D2830 display-list setup; check the GS dump background. |
| CAM-09 | Is arm 1 of the 00195130 case 0xB (y<185, z<220, 359<x<394.8) floor reachable? | Collision query at that XZ against AREA11 EMCL. |
| P31 | Does AREA11 enable the passive hazard drain (D_008106C8 & 0x60)? | Read D_008106C8 in playable_ee.bin. |
| SI-27/AM-21/INV-24 | Which 001FAE70 branch AREA11 takes (D_008104E4, weapon id, D_008106C8 track) | Read those globals in the handoff and playable captures; oracle 001FAE70. |
| W18 | AREA11 crawler model param → mesh | Per-area model table (chunk15/f05_id97 +0x5000) against the crate record param; capture model pointer. |
| INV-27 | Record 20 (001C4820) mesh binding | Same capture method as the canopy (OPENING_SCENERY.md). |
| SI-22 | Does op 0x14 spawn (001BAC00) frame coincide with op10 sub1 visibility? | CONTINUE flags in the 0x828FC0 records; opening capture frame index. |
| ORCH-22/SI-28 | Main-loop phase order has no oracle; step I (001B5B70 rumble countdown) is not called | Instruction trace of 0x001AAE40 over a captured frame. |
| INV-02 | AREA01 sub 1 scene is not exported | Exporter run for area 1 sub 1 from the user's disc. |

### Label and documentation corrections found (status as of 2026-09-22)
- Port:
  - CLAUDE.md gait-hold paragraph (P30: gait 1 translates at 0.1). CORRECTED (9f00668).
  - CLAUDE.md "DECODED + LIVE-VERIFIED" door camera (CAM-06). CORRECTED (9f00668); the style 5/1 wording was made exact by the cleanup-game lane.
  - Stale "eye +9 / target +8" (CAM-08). CORRECTED in CLAUDE.md (9f00668, walking camera heights).
  - `em_camera.c:366` calls 0022EEF0 "scope/sniper" (it is the cutscene timeline). CORRECTED (9f00668).
  - `render_chain_build` is labelled 001D1C50 (that function is the per-frame fog/GS setup). CORRECTED: `em_game.c` now calls it a port-only collector that runs at the 001D1C50 slot.
  - `em_hud.c` says 0020CDC0 is undecompiled. CORRECTED (9f00668); the same claim in `em_hud.h` and `em_game_internal.h` was corrected by the cleanup-game lane.
  - `em_sfx.c:477` "NOT YET CALLED" is stale. CORRECTED (9f00668).
  - `em_random.c` / `main.c` 0x45 seed attribution (SI-01). CORRECTED (f338b46: the cold-boot seed is 1).
  - `em_opening_runtime.c` op 0x14/op6 handler addresses (SI-22). CORRECTED: the handlers now cite ftab_0024D880[6] = 001BA080 and [0x14] = 001BAC00, which match the ELF table. The open SI-22 timing question in the table above remains.
  - Also corrected by the cleanup-game lane: `em_game.h` "0x1AE040 still undecompiled" (it is NEARMISS `anim_frame_top_b.c`), the D_00810811 "battery" docs, the director's op0C "music" names (now message lines), the fog comments (FOGCOL 0..255, near -209, F per vertex) and the "boot ELF draws NOTHING" flashlight comment.
- Decomp: the four function headers are CORRECTED in decomp 7ae3b07; the FINDINGS item carries a CORRECTION paragraph in "ENGINE FRAME ANATOMY".
  - 001FAE70 "reticle selector": it is the stream-resume cue selector.
  - 0020CDC0 / 002149F0 / anim_frame_top_b "save/load / title-attract": they are the status/battery/in-game frame machines.
  - 001FBC50 "subsystem init": it is stop-all SFX.
  - 001735C0 "boss machine": it is the player light melee.
  - FINDINGS "ENGINE FRAME ANATOMY" lists steps N/O as unconditional; they are gated on D_00821058==1. CORRECTED.
