# AREA11 aiming and firing (audit 1b item 14, census lane L28)

The player's armed stances (R1 0x1D, R2 0x1E and their 0x1F / 0x20
variants), their fire machines, the laser sight, the shots, the gun lamp,
the knife and the cable reaction. **Current state (2026-10-02, section 10):
the original aim / fire path is the only one in AREA11.** The diagnostic
gate is gone; em_weapon's stance, firing loop, gun tick, lamp gate, laser
and muzzle-flash drawers and the camera stand-in camera_mode1_aim are
retired, and em_weapon keeps only the four global bytes the originals read
and write (D_00810C61, D_00810C62, D_00810CB4, D_00810D3C). The history:
chain step AIMCAM (2026-10-01) bound the aim camera (CAMERA_LIVE.md section
7) and the side runs `aim_r1_hold` / `aim_r2_hold` (section 5); its fix
round bound the round's impact marker 0018ABA0 (section 3); chain step
AIMLIVE (2026-10-02, section 9) bound the muzzle node 001F5040 with its
draw, the shell casing, the reload and both melee states with the knife's
reach probe and trail node, and five more AIM replays. Section 10 closed
what kept the gate: the impact effect 0x80000060's VU1 program 0x230800
(the streak program, translated with the EFU model the background renderer
already used), the gun lamp 00187780 and its callees (translated and
instruction-tested), the ring decal's handler 001EBBB0, the class-1 pair
callee 001A9C40 the impact markers reach in the close-out, and for the
knife on the cable the pad actuator call 001B61C0, the cable hit's effect
handler 001EAB50 and the kind-2 VU1 program 0x232540 its hit node 0021AAC0
draws with; the side runs now replay aim_04 (the world hits) and aim_10 +
aim_11 (the cable) row for row as well. Chain step AIMCAP (2026-10-02,
section 11) compares every AIM capture beat, aim_05's burst fire through
the status screen included, with whole records (the player record, the gun
and knife nodes, the camera and the status block) row for row, and moves
the player's node slots onto the one bone-slot stack, which ends a melee
divergence (the knife's trail wrote into the player's node records).

History: the work was done on the Codex branch `codex/aim-fire` (three
commits on 7d7bdb5) and brought onto main by chain step AIM (2026-10-01)
after an audit (section 6). The branch and its worktree are not used any
more.

## 1. Gate (removed 2026-10-02)

There is no gate any more (section 10). Until chain step AIMLIVE's fix
round, `EM_AIM_FIRE_ORIGINAL=1` with an `EM_AIM_FIRE_TEST` fixture selected
the original stance slots 0x1D..0x22 and installed the composition's world
/ render / cable extension; ordinary play ran em_weapon's stand-ins. Now
`player_states_bind` always selects the original slots, the extension
(`em_aim_fire_runtime`) is installed for every AREA11 run, and the
equipment nodes' aim / shot callees and the gun lamp always go to the
composition.

The input fixture (`em_aim_fire_test.c`, phase 7 of the newgame-control
test) stays as a quick headless check, now without a switch: it waits for
the validated first control, waits 60 ticks, holds R1 (key E) or R2 (key
3), presses Circle (key L) at ticks 100..101, releases the trigger at 150
and checks the stance and a fire sub-state were seen and the player is
idle at tick 240. `EM_AIM_FIRE_TEST=r1hold` / `r2hold` hold and release
without the Circle press and require that no fire sub-state is seen. It
writes no game state. It is not a capture comparison: that is the level
smoke's side runs (section 5 and section 10).

```
EM_STARTUP_TEST=newgame-control EM_AIM_FIRE_TEST=r1hold build/extermination
EM_STARTUP_TEST=newgame-control EM_AIM_FIRE_TEST=r2hold build/extermination
EM_STARTUP_TEST=newgame-control EM_AIM_FIRE_TEST=r1 build/extermination
EM_STARTUP_TEST=newgame-control EM_AIM_FIRE_TEST=r2 build/extermination
```

All four PASS (section 10.6); `r1` and `r2` faulted at AIMLIVE on the
0x80000060 impact's VU1 program packet 0x230800, which is now translated.

## 2. Original path and owners

| Original | What it does | Port owner | Evidence |
|---|---|---|---|
| 001607D0 | the action / held / edge dispatch that enters the stances | em_player_weapon_states_a (live since census L12) | test-player-weapon-states-a-reference |
| 0016FCF0, 001703E0, 001729A0, 00173000 | the stance loops 0x1D..0x20 | em_player_weapon_states_a / _b (translated earlier) | test-player-weapon-states-a/b-reference |
| 001735C0, 00173E60 | melee 0x21 / 0x22 | em_player_weapon_states_b | same; selected by the gate since AIMLIVE (section 9.3) |
| 0016F530, 0016F5D0, 0016F600, 00172860, 0017A800, 0017A8B0, 0017A970, 0017AAD0, 0017ABA0, 0017AF70, 0017B300, 0017B420, 001B5DC0 | draw, release, holster / reload wait, spin, pitch to target, fire select, light / attachment switch, sub-weapon cycle, manual steer, target steer, reload, mode-4 reload, stick ring | em_aim_fire_control | test-aim-fire-control-reference |
| 00170A60 (byte-matched), 00171320, 00171670, 00171B00, 00171E90, 001723D0 | the six fire machines chosen by +275 | em_aim_fire_machines | test-aim-fire-machines-reference |
| 0017A130, 0017A0B0, 00179BC0, 00179CA0 | the armed pose: dispatch, slot clip, publish, blend | em_aim_fire_pose | test-aim-fire-pose-reference |
| 00185A10, 00185E30, 00199220, 001854E0, 00185760, 00183AC0, 00183B80 | lock acquisition and upkeep, the laser dot and beam drawers, the filters | em_aim_fire_target | test-aim-fire-target-reference |
| 001860A0, 001861C0, 001869A0, 00186A60, 001872C0, 00187CC0 | the equipment one-shots and the surface resolution | em_aim_fire_shots | test-aim-fire-shots-reference |
| 001DD170, 001DD2F0, 001DD600 | the target reticle and its packet builders | em_aim_fire_reticle | test-aim-fire-reticle-reference |
| 0018ABA0 | the round's impact marker (pool behaviour) | em_aim_fire_marker, bound by em_aim_fire_runtime (001861C0's 001AFA90 route and the settle bind; section 3) | test-aim-fire-marker-reference |
| 001EACF0, 001EBA20 | the impact effects' draw handlers (subtypes 0x23 / 0x1B: ids 0x80000060 from the marker's 001F00A0, 0x8000002C from 001861C0's 001EFD90) | em_effect_kinds (fix round), through em_effects_live's handler dispatch | test-effect-kinds-reference (both fully executed against the original) |
| 001F00A0 | the marker's effect spawn | em_area00_fx (its one bound owner, composed by em_aim_fire_world_live) | tools/test_area00_fx_reference.py |
| 00197D20, 00198650, 0018CA90 and what they own | the aim camera (camera actions 1 / 2 / 5) | em_camera_aim, bound in em_camera_live (CAMERA_LIVE.md section 7) | test-camera-aim-reference, test-level-smoke-aim (aim_00 / aim_01 row for row) |
| 001839A0, 001B1510 | type classification, angle reduction | em_aim_fire_leaves | test-aim-fire-leaves-reference |
| 15 SDK vector / matrix leaves (001026A0 .. 00103230) | with the original read / store order and aliasing | em_aim_fire_sdk_memory (arithmetic from em_owner_services_original) | test-aim-fire-sdk-memory-reference |
| 00183C40, 001B41F0, 001EFE00, 001F4F40 | acquisition validity, the hit call, the cable effect allocation, the muzzle node | existing later-level translations em_area02_math, em_area00_world, em_area01_side, em_area00_fx, composed by em_aim_fire_world_live | their own oracles (tools/test_area02_math / area00_world / area01_side / area00_fx_reference.py, all PASS at this merge) plus test-aim-fire-world-live |
| 001CD520, 001E2BA0 | the sprite and the beam | em_player_equipment_sprite, em_area00_hud, composed by em_aim_fire_render_live | test-aim-fire-render-reference (composition against the original) |
| 0021AAC0, 0021A500, 001EFEB0, 001CE860 | the cable-hit effect nodes and their strip packets | em_security_gun_rest, em_area06_port_strip, composed by em_aim_fire_cable_live | test-aim-fire-cable-live; tools/test_area06_port_reference.py |
| 001F5040 and its model-node callees 001C6120, 001CA5E0, 001C6150, 001AF780, 001CB5B0, 001C62C0, 001C63D0 / 001C9610, 001D80E0, 001CACB0 | the muzzle node (AIMLIVE) | em_area00_fx (behaviour), em_aim_fire_flash (record bytes, workers), em_area00_world (001C63D0), em_owner_services_original, em_effect_original (001D80E0) | tools/test_area00_fx_reference.py, the side runs aim_fire / aim_reload* (section 9.4) |
| 001CABA0 (the +0x4C draw 001CACB0) with 001D3900 / 001D3CF0 / 001CAAC0 | the node's class-2 object units on the chain page | em_owner_services_original, em_owner_draw_original / _live, em_object_unit (class 2), em_chain_page (unit CALLs) | test-owner-draw-reference part E, test-object-unit-reference part K |
| 001F4010, 001F2F90, 001F3340; 001F3620, 001F3E30 (001F40C0's) | the shell casing: seed, particle record, barrel sweep and draw | em_area02_misc, em_area00_fx_debris (the one owner under the gate), em_owner_draw_live (001CA7B0 / 001C7900 / 001CA940) | tools/test_area02_misc / area00_fx_reference.py, test-owner-draw-reference |
| 001AA840, 001AA7A0 | the knife's reach over the class-4 list | em_area00_fx, em_aim_fire_leaves | test-aim-fire-leaves-reference (001AA7A0, 48 reaches) |
| 0019B2C0 | the knife's actor-less probe | em_coll_move_original (`em_coll_move_probe_0019B2C0`) | test-coll-move-reference (60 cases quick, 216 full) |
| 00189EC0, 00189FE0, 0018A180, 001EFF10, 001F18C0 / 001F1550 / 001F15F0 | the knife's entity test, strike, reaction, trail spawn and trail node | em_area00_world, em_area00_fx (spawn, trail), em_aim_fire_trail (slots) | tools/test_area00_world / area00_fx_reference.py, the side run aim_melee |
| 0018A6B0, 00188630 | the equipment nodes and the gun tick | em_player_equipment through em_equipment_live (live) | test-player-equipment-reference, the level smoke's check_effects |
| 00827490 | the AREA11 cable | em_security_gun (live) | SECURITY_GUN.md |
| 00187780, 00187690, 001D9530, 001D91A0, 001DA290, 001DA1E0, 001D4E20, 001D4EB0, 001D4B80, 001D4C30 | the gun lamp: its matrix, probe, attenuation and flare (001CFBE0 kind 1 over D_002487E0), and the cone shells with their helpers (section 10.2) | em_aim_fire_lamp, dispatched by em_aim_fire_live; the equipment node's 00187780 call through em_equipment_live_set_lamp | test-aim-fire-lamp-reference (60 cases quick, 156 full); the side run aim_light |
| 001EBBB0 | the ring decal's draw handler (subtype 7, 0x8000000E from 001F0460) | em_effect_kinds | test-effect-kinds-reference |
| the VU1 program 0x230800 (kinds 0 / 4 of 001CFBE0) | the impact effect 0x80000060's streak quads | em_vu1_page_programs (`em_vu1_streak_program_mscal`), run by em_chain_page | test-chain-page-reference (the program's own microcode executed by tools/chain_page_model.py); the side runs aim_world / aim_cable (section 10.1) |
| 001A9C40 | 001A9D20's pair callee: a class-1 record (the impact marker) near a class-2 record (Roger) sets its +0x0A bit 0 | em_coll_list_passes, bound in em_collision_world | test-coll-list-passes-reference (executed on its own, 5,460 staged runs) |

Tables: `tools/export_aim_fire_tables.py` reads the pinned ELF (SHA-256
checked) and writes the ignored `assets/aim_fire_tables.emaf` (EMAF v4: the
clip / sound / tint rows 0x248680..0x248D00, the reticle templates
0x2533D0..0x253490, the beam reference 0x253720..0x253740, the cable state
templates 0x266930..0x266AE0 and, since chain step AIMCAM, the R2 aim
camera's eye offset 0x2754E8..0x2754F4, which 00198440 loads);
`em_aim_fire_tables` loads it on first use
and the binding serves those ranges read-only (STARTUP.md row 59). The
reference tests read the same bytes from the ELF.

## 3. Binding

- `em_aim_fire_live` composes the translated owners and forwards every other
  callee to the binding; unknown views and callees fault.
- `em_aim_fire_binding` maps canonical storage: the player record, the
  request and progress blocks (em_scene_state), em_weapon's fire mode
  D_00810C61, magazine D_00810C62, total D_00810CB4 and light D_00810D3C, the
  pad words, the frame counter, the equipment owner's typed fields, the
  tables, and the player closure's scratch views (0x70003A20..2F, 0x700036A0,
  0x700038A0..BF, the pad masks 0x70003B74..7F, the sticks). The pose work
  banks 0x286340..0x287F3F are owned here (0017A130 writes every node it
  reads there in the same call); 0x287F40..0x289B3F stay EmPlayerRecordPose's.
  Its callees: the pose host (001C61D0, 001749A0, 001749F0, 001C6DA0), the
  SDK math (0011E2A8, 0011DE90, 0011E620, 0011E748, 0011DBB8, 0011DF78),
  0011E860, 001281C0, rand 00122BB8, 001B0070, 0015D2F0, 001B1470, 001B1240,
  001B12B0, sound 001FBD50 / 001FB9F0 / 0011A070 (em_sfx, as the player
  closure's adapters) and 001607D0 (the closure's action dispatch).
- `em_aim_fire_runtime` (installed for every AREA11 run since section 10) adds pool header fields, effect node
  fields, the render context, the world adapter, the cable adapter, the
  private BSS words D_00275B00 (word 0: 00185760's phase, its only user) and
  D_00821400..BF (0021A500's strip points, its only user). Since chain step
  AIMCAM also: the scratch temporaries 0x700038C0..0x700038FF,
  0x70003600..0x7000363F and 0x700031E8 (section 7), 00102870 (the beam's
  divide, em_area00_low), the grid node's original address for
  *0x700031D0 (`grid_address`) and the node records' bytes
  (em_scene_bindings_grid_node_address / _bytes).
- **The records the composition allocates (fix round).** `001AFA90(class)`
  called from inside the composition (001861C0's 001AFA90(1); 001F4F40's
  001AFA90(0xC) reaches it too) allocates from the one actor pool
  (em_actor_pool_alloc_001AFA90); a refused allocation answers 0, as the
  original. The runtime keeps, per record, the bytes beyond the pool
  header the marker writes before it reads them: +0x28 (its countdown) and
  +0xA0..+0xAF (its normal), each byte readable only after a write since
  the allocation (001AFA90 / 001AFC10 leave them as the previous occupant
  had them, and nothing reads them first). When the root call returns
  (`em_aim_fire_binding_set_settle`), each record it allocated is bound by
  the +0x10 its caller stored: 0x0018ABA0 through the AREA11 binder's row
  (em_area11_bindings `tick_aim_record`, every walk mode, class 1), whose
  tick runs `em_aim_fire_runtime_tick` -> 0018ABA0 through the binding; any
  other +0x10 faults (the muzzle node 001F5040 is not bound, section 7).
  The marker's callees: 001B1470, 00102948, 00122BB8 and 001FBD50 (the
  ricochet) through the binding; 001B17A0 through the interaction host's
  byte-matched translation (`em_area11_interaction_host_offer_001B17A0`)
  over the record's +0x02 / +0x03 / +0x0D / +0x2E / +0xB0..+0xB8;
  001EFD90 / 001EFD20 through em_effects_live; 001F00A0 through
  em_area00_fx (em_aim_fire_world_live); 001AFC10 the pool's free. The
  spawned effects' handlers 001EACF0 / 001EBA20 are em_effect_kinds' (their
  source blocks D_00255620, D_002560D0, D_00256160 joined the effect-table
  export, STARTUP.md row 50).
- The player closure (`em_player_closure_live.c`) binds the stance modules'
  workers to the composition (`af_worker`); it flushes the stance modules'
  by-value scene copies before each worker and refreshes them after, also on
  failure.
- The equipment nodes' callees 001854E0, 00185760, 001861C0, 001869A0,
  00186A60, 001872C0 and 00187CC0 (and since section 10 the lamp 00187780)
  go to `em_aim_fire_binding_run`, with D_00275B40 switched to the node's
  own bone table for the call.
- **AIMLIVE additions (section 9).** The muzzle node's record bytes and
  model-node workers (`em_aim_fire_flash`), the knife trail's slots
  (`em_aim_fire_trail`), the gun tick's 001F4010 (`em_equipment_live_set_casing`
  -> `em_aim_fire_runtime_001F4010`), the knife's callees
  (`em_equipment_live_set_world` -> `em_aim_fire_runtime_world_*`: the
  node's staged scratch 0x700038A0..0x700038DF goes to the composition's
  scratchpad before each call and back after it, with D_00275B40 = the
  knife's own bone table) and the barrel's particle hook
  (`em_effects_live_set_particle_call`). The composition's views grew by the
  render context (`em_rcl_views`), the effect particle regions, the ELF
  windows D_0025A350 (0x34B0 bytes), D_0026EA80 (0x64), the Roger export's
  table word D_0028A56C and the slot stack's count D_00275BCC, the published
  class-4 list (D_00275B7C / D_00275B84 and its words) and the collision
  probe's result words (0x700031B0 as a quadword whose fourth word is 0, as
  in all 7272 per-frame AIM rows and the 29 snapshot scratchpads; *0x700031D0,
  *0x700031D4 and the grid node's 64 bytes). The scratchpad words
  0x70003190..0x700031D8 and the cell record D_700030B0's +0x1A..+0x2C have
  two port owners, the segment walkers' state and the move walkers'
  scratch (the original's one memory); the composition shows the one a
  composition call wrote last (0019B2C0 selects the move scratch, 0019A570
  / 0019B6C0 the segment state).

## 4. What changed for ordinary play

- **Player +0x18 / +0x20.** 0015C420 stores the knife node (0018A880(4, 0))
  at player +0x18 and 0015C310 the gun node (0018A880(0, 0)) at +0x20; the
  port now stores both pool addresses (it dropped them before). Every AREA11
  route snapshot holds 0x7AB440 / 0x7AB730; the level smoke's check_effects
  compares the two words at every aligned snapshot tick.
- **D_00810C61** is read from em_weapon's byte (its one storage,
  em_scene_state.h) by 001607D0's scene in every call; before, an armed
  +1F0 made the read fault.
- **Equipment and effect node headers.** em_equipment_live imports the
  node's header bytes from the pool record before each tick and publishes
  them after; em_effects_live imports the driver's and head sprite's fields
  from the record each tick. The record is their one storage; nothing in
  ordinary play writes it between ticks, so the level smoke's check_effects
  is unchanged.
- **The cable hit** (00827490 with +0x36 set): at this merge it faulted at
  001EFE00 in ordinary play. Since section 10 the original melee states
  run in ordinary play: the knife at the cable's foot hits it (+0x36 set,
  aim_11's f249) and the reaction (001EFE00, 001EFEB0, 0021AAC0, 0021A500)
  runs on its originals; the side run aim_cable compares it with aim_11 row
  for row (section 10.5).
- em_weapon's light flag is a byte (D_00810D3C's width).
- **Since section 10 there are no stand-ins left:** em_weapon keeps only the
  four bytes (section 10.3).

## 5. Verification (this merge, 2026-10-01; chain step AIMCAM below)

| Target | Default run | What it executes |
|---|---|---|
| test-aim-fire-control-reference | 776 cases, ~2 s | the original 13 entries over random records and the 15 route records; ordered stores, callee arguments and memory at every callee entry, 176 fault cuts, every conditional both ways |
| test-aim-fire-machines-reference | 1848 cases, ~1 s | the six machines; 448 / 448 branch outcomes, 156 fault cuts, 60 missing-worker checks |
| test-aim-fire-pose-reference | 305 cases, ~1 s | the pose quartet; 25 conditional sites both ways |
| test-aim-fire-target-reference | 497 cases, ~1 s | the seven target entries; the store sequence before every callee, whole memory at the end |
| test-aim-fire-shots-reference | 609 cases, ~2 s | the six shot entries; 184 / 184 branch outcomes |
| test-aim-fire-reticle-reference | 140 cases, ~1 s | the reticle; the VU state at the end |
| test-aim-fire-leaves-reference | 460 cases | all 256 type bytes, angle boundaries |
| test-aim-fire-sdk-memory-reference | 560 cases, ~2 s | 15 SDK entries with overlapping / misaligned operands |
| test-aim-fire-render-reference | 46 cases, ~5 s | the sprite / beam / reticle composition against the original, whole RAM and scratchpad at each root exit, the vf23 hand-off |
| test-aim-fire-tables | 16 containers (EMAF v4) | the EMAF loader rejects damaged files |
| test-aim-fire-live, -world-live, -cable-live, -effects-live, -equipment-live | ASan / UBSan | the composition seams (not original-instruction tests) |

`EM_TEST_FULL=1` runs the exhaustive sweeps; all passed at this merge:
control 6437 cases (30719 stores and callee boundaries), machines 18034
(30202 worker calls), pose 1625 (17142 ordered stores), target 762 (7349
callee entries), shots 6004 (109234 worker calls), reticle 309 (3875
callee entries), leaves 1360, render 86 (5526 external boundaries,
63464 byte addresses compared at root exits). The machines test's native
adapter now hands the machines the gun's +0x2E halfword (section 6,
finding 1).

Ordinary play at this merge: every make test-* target, the full level
smoke (18 live phases through Roger and the three side runs, NOT-LIVE
none; check_effects now also compares the player's +0x18 / +0x20 at the
snapshots 08 and 10..14), newgame-control 9.599849.

Chain step AIMCAM adds `make test-camera-aim-reference` (the aim camera
against the original instructions, CAMERA_LIVE.md section 7), the
equipment node's +0x200 (test-aim-fire-equipment-live,
test-player-equipment-reference), EMAF v4 (test-aim-fire-tables) and the
dot's texture (test-disc-textures-reference).

**The live aim camera against the captures (AIMCAM fix round).** `make
test-level-smoke-aim` (part of test-level-smoke-side and -full; about
3 min: two level-smoke runs side by side, then their checks) plays the
main line through truck_crossing, then the side phase `aim_r1_hold` or
`aim_r2_hold` (LEVEL_SMOKE.md "aim_r1_hold, aim_r2_hold"): it walks
straight in to the AIM captures' start (route 08's end, the start pose
0.65 off and the heading 0.015 off: the stick's resolution), waits for the
idle clip's +3C to reach the capture's value at f12 (13.0), and holds R1
(R2) for the capture's 79 ticks. tools/test_level_smoke.py
`check_aim_hold` then compares every row from the stance (f13) to the
capture's end (aim_00 f146, aim_01 f154) with decomp
build/aimfire/capture/aim_00_r1_hold / aim_01_r2_hold trace.json:
- exactly, on every row: the player's +5, +6, +7, +1F0, +1F1, clip, clock,
  the action code +230 and the camera bytes D_008101E4..E7 (the stance,
  the draw, the hold, the release 0x63 / 0x64, the holster 0x65 / 0x66,
  the idle return; camera action 1 / 2, then 0);
- in the player's frame (lateral, up, along the heading), within 0.002 on
  every row: the heights of the eye D_008105D0, the target D_008105E0 and
  the camera block's eye (+0x10) and target (+0x20), the eye's distance,
  the camera block's target on all three axes, and the player's +A0 /
  +B0. Two components depend on where the run stands and are checked for
  what the code does there: the eye's lateral offset starts from the
  follow camera's rest (0.346 in the capture after its walk off the
  truck, 0 after the run's straight walk-in), never exceeds that start
  difference and agrees within 0.002 from the settled hold (f69) on; the
  smoothed target D_008105E0 chases each world axis by at most 1.0 per
  tick in the release (aim_00 f94..f99), so its local x / z depend on the
  heading: on those capped rows the port's world steps equal the
  capture's (within 1e-4), and elsewhere it stays within 0.2 and agrees
  within 0.002 on the settled hold and the last row;
- at the last row, the render context's eased pair D_00275690 /
  D_00275694 (001DDE10 eases it toward the aim mode's targets and back)
  and the context's +0x245C..+0x2467 equal the capture's end snapshot
  bytes.
Measured: both runs pass; the largest differences are the start's lateral
offset (0.3462) and, in aim_00's release, D_008105E0's x 0.125 / z 0.056;
everything else is within 0.0013 (the eye's distance at f13), most within
1e-4. The first version of the side run started 3 units off the
capture's place and found the follow camera's eye climbing six ticks late
after the release: the prepass 0018D330's ground test (+0x6D) misses the
ground under the hip there, so the per-state height 00191390 waits; at the
captures' place the release equals the capture. The place matters, the
binding does not differ.

The fix round also adds the impact handlers to `make
test-effect-kinds-reference` (001EACF0 / 001EBA20 against the original,
both fully executed; EM_TEST_FULL=1 2,400 handler cases) and two later-level
owners the first level binds to the default set: `make
test-area22-port-reference` (001028E8 / 00183010 / 0018C850 / 0018C920)
and `make test-area00-low-reference` (00102870). `test-aim-camera-hold` is
retired: the side runs compare the same draw / hold / holster with the
captures row for row (Tests rule 4).

## 6. Audit of the Codex branch (2026-10-01)

Every translation was read against the original instructions or the
byte-matched decomp C, and every oracle re-run: each executes the original
ELF code in the shared EE interpreter and compares callee arguments and the
memory (or the ordered store sequence) at every callee entry. No translation
mistake was found in the instruction-tested cores. Findings:

1. **Fixed: the gun link could never be served.** The machines asked their
   `link20` worker for a contiguous 0x30-byte view of the gun record to
   write its +0x2E event. The gun's canonical storage is
   em_player_equipment's typed node, which has no such view, so the first
   round of any live run would fault in 00170A60 (seen with the camera
   handlers stubbed, section 7). The worker now returns the +0x2E halfword
   (em_equipment_live's `h2E`); the machines oracle passes unchanged in its
   coverage.
2. **Fixed: a run that stopped without a verdict passed.** A player-stage
   fault ends the game through `em_frame_request_quit` without a FAIL line;
   the newgame-control test (and so the aim fixture) exited 0. It now fails
   when the run ends before its PASS.
3. **Corrected: "the live path stops only at the camera".** With the two
   camera handlers stubbed in a private probe build (not committed), the
   R1 run continues and stops in order at (section 7): the gun's +0x200
   vector, the scratch views 0x700038C0..DF / 0x70003600..1F, the laser
   dot's texture, 00102870 inside the beam, the gun link (finding 1), the
   shot's scratch 0x700038F0 and the collision query word 0x700031E8.
4. **Corrected doc claim: the reload button.** The branch read 0x70003B76
   (Cross) as the reload forwarding. The masks at 0x70003B74..7E are Square,
   Cross, Circle, Triangle, R1, R2 (decomp CAPTURES_C10.md "AIM"): Circle
   is the trigger (0x70003B78), Square the light / attachment switch
   0017A970, Cross the sub-weapon cycle 0017AAD0; the reload is L3 (the
   pressed word's 0x200, read directly by 00170A60).
5. **Superseded: the capture plan.** The branch's four requested captures
   were recorded by the C10 AIM lane (decomp CAPTURES_C10.md, twelve beats
   under build/aimfire/capture/); no PCSX2 run belongs to this module.
6. **Duplicate translation noted.** 001EFE00 has two verified translations:
   em_player_misc_001EFE00 (the player's hit / death paths, unbound) and
   em_area01_side_001EFE00 (bound here). Whichever binds the damage paths
   (audit item 13) must use the same owner.
7. **Header placement.** em_effect_original.h's new declaration moved inside
   its `extern "C"` block.
8. **Kept, with the reason recorded:** the private pose bank, the two
   private BSS words and the stance-scene flush (section 3); the later-level
   modules the world adapter reuses (their oracles PASS at this merge); the
   `.gitignore` entries for symlinked `assets` / `data`; the two bounded
   mutation tools (`tools/mutate_aim_fire_pose.py`, `mutate_aim_fire_leaves.py`,
   developer tools, not make targets).

Nothing was dropped: every branch file is on main.

## 7. What a live run meets, and what is left (chain step AIMCAM, 2026-10-01)

**Closed in chain step AIMCAM** (each now runs live behind the gate):

1. The aim camera: 00197D20, 00198650, 0018CA90 and what they own,
   translated (em_camera_aim, oracle test-camera-aim-reference) and bound
   (CAMERA_LIVE.md section 7), with the release 00197490 and its aim
   workers.
2. The gun's +0x200..+0x20F: em_player_equipment's node now holds the laser
   dot (`v200`; 001854E0 writes it, 00198440 aims the R2 camera at it);
   em_equipment_live serves +0x1F0..+0x217 as one view.
3. Scratch views with no other owner, each written before it is read in
   the same call (em_aim_fire_runtime, section 3): 0x700038C0..0x700038FF
   (001854E0 / 00185760's dot and beam vectors, 001861C0's hit and normal),
   0x70003600..0x7000363F (001CD520's screen point, the beam, 001B41F0's
   temporaries; 001CD520 also loads 0x7000361C before storing it, and does
   not use it: PLAYER_EQUIPMENT.md section 5) and 0x700031E8 (the shot's
   flag; only 001861C0 and 00186A60 address it).
4. The dot's TEX0 0x20045BA5154222DC: export_page_textures.py now decodes it
   (a code immediate, like the decal's and the glow marker's; its texels
   equal the route captures' GS memory, test-disc-textures-reference).
5. 00102870 in the beam: em_area00_low's translation (now in the build),
   over the two quadwords it addresses.
6. The beam's PRIM 0x69 (a LINE list of two vertices): the chain page draws
   it like a two-vertex line strip (em_gfx_metal.m). Evidence (CHAIN_PAGE.md
   section 5): the GS assembles a list every two vertices and a strip from
   its last two (GS_EXACT.md 3.0), and its measured line rule (decomp
   GS_CONFORMANCE.md 5.2: 136 lines, 0 mismatches) was measured on PRIM
   LINE packets, with the line strips of the same suite following it.
7. The collision grid-node base for *0x700031D0: a grid hit names its node's
   original address, D_0028A598 entry 0 (the loader's relocation slot 0x42)
   + the grid header's +0x20 + 64 * node, and the node's record bytes
   (+0x1A, +0x1C, +0x24..+0x2C and the rest of its 64 bytes) are the area
   data the loader delivered (em_scene_bindings_grid_node_address /
   _bytes); nothing is made up when the loader has not delivered them.

**Closed in the AIMCAM fix round** (behind the gate):

8. The impact marker 0018ABA0 is bound: 001861C0's 001AFA90 route, the
   marker's +0x28 / +0xA0..+0xAF, the settle bind by +0x10 and the pool
   dispatch (`tick_aim_record`), with its callees (section 3). A private
   probe build that skipped the two stops below (not committed) ran the
   marker's state 0 and state 1 live: 001F00A0 spawned the 0x80000060
   effect, the ricochet sound played, and the effect's handler ran.
9. The impact handlers 001EACF0 (0x80000060) and 001EBA20 (0x8000002C) are
   translated (em_effect_kinds) and instruction-tested; their source blocks
   are exported.
10. 001F00A0 is bound (em_area00_fx, its one bound owner).
11. The live aim camera is compared row for row with aim_00 / aim_01
    (section 5).

**Left after the fix round** (the same probe's order): the muzzle node
001F5040, the shell casing 001F4010 and its particle records, the impact
effect's VU1 program 0x230800, a live reload, melee 0x21 / 0x22 and a fire
side run against aim_03. Chain step AIMLIVE closed all of them but the VU1
program; section 9 has each, with what is still left (9.5).

**Not reached in any AIM beat (stay fail-stops):** the handlers 001ECB00
(0x80000019: the marker's state 1 with +0xD != 0 and no 0x300 bit in
+0x2E), 001EB7F0 (0x80000026: the marker's +0xA set) and 001EC820
(0x80000067: 001861C0's surface type 8 or a 0x300 code). None of the three
ran in the twelve beats (decomp build/aimfire/capture/census_delta.json).
The captured markers agree: in the AIM rows' pool records (+0x10 =
0018ABA0) aim_03's three and aim_04's first (the ground hit) hold +0xD = 0
(no effect at all), and aim_04's two later and aim_10's two hold +0xD = 1
with code 0x201 (bit 0x200 only: 0x80000060, 001EACF0, which did run).
em_effects_live faults on each (an untranslated handler).

**Not reachable in AREA11 (fail-stops, with the reason):**

- **001DD170 (the reticle) and its vf23 input.** Its callers: 001854E0 /
  00185760 (they loop over the locks D_008106E0..E8), 00199220 (its tail
  draws the reticle at D_008106E0, or at all three locks) and 00199770
  (001999C0's D_00810CA4 == 2 arm). The locks have two writers: 00185A10
  and 00199220, which clears all three and refills them. Both writers, and
  00199770, take a class-2 list entry only when it passes 00183B80, which
  requires (+2 & 0x1F) == 2 (00185A10 also through 00183AC0). 00199220 is
  called only by the stance loops 001703E0 and 00173000, and only when
  D_00810CA4 is 0 or 1; 00199770 needs D_00810CA4 == 2. D_00810CA4 is 0xFF
  in every capture, and neither 00199220, 00199770 nor 00183B80 ran in any
  AIM beat (decomp build/aimfire/capture/census_delta.json). In every AREA11 capture
  (route 00..14 and the twelve AIM beats) the class-2 list D_00275B8C holds
  nothing or only Roger (0x7A8830, +2 = 0xAA: class 0x0A), no allocated
  pool record of class 2 exists (the AREA11 pool snapshots; AREA01's
  arrival, beat 15, has eight), and the locks stay 0 in every AIM row. So
  001DD170, the numeric reticle (kind 1, 001CBA50 / 001CBC20) and the vf23
  bookkeeping are not reached; the render adapter keeps refusing an
  uncertified vf23.
- **The projectiles 0018AF50 / 0018B3E0** (001869A0 / 001872C0) need the
  sub-weapon byte +275 != 0; with D_00810CA4 = 0xFF and D_00810CA6 = 0,
  Square toggles the light and Cross (0017AAD0) changes nothing (aim_08),
  and +275 stays 0 in every AIM beat.
- The aim camera's sight drawers, 0022E7F0, 00182F90 and 001B0300
  (CAMERA_LIVE.md section 7).

## 8. Findings for other owners

- **Decomp C (NEARMISS bodies; the decomp lane):** 001854E0's probe scale is
  260.0 (the C says 65.0f beside the constant 0x43820000); 00185A10's
  full-scan distance uses player +A0..+A8 while its dot delta uses the muzzle
  scratch vector; 0017A130's first centre-pose lookup receives the player
  pointer (NEARMISS C passes null); 00171320's 0x5DD sound gets the actor as
  a0; 00171670's empty start advances +7 before clearing the latch and
  sound, and its state 12 resets to 1 only when the action return and latch
  are zero; 0017AF70's C reverses the yaw blend signs, misreads the target z,
  omits the pitch atan2 baseline and picks the wrong pitch branches;
  001861C0's unlocked range is 260 and both its 001B41F0 calls pass six
  arguments (fifth 0, sixth 5); 0016F600's case-1 expiry skips the matrix
  dispatch. The native follows the instructions in each case (its oracle
  passes). Re-checked at this merge against the NEARMISS C: 001854E0
  (65.0f), 0017A130 (the null first argument) and 00171320 (the 0x5DD
  call's first argument); the others are the branch's readings.
- **Decomp C, the aim camera (chain step AIMCAM; NEARMISS bodies, the
  decomp lane):** 00197D20's state 3 calls 00197490(cam, player, 1) (the C
  passes one argument); 001999C0 calls 001DB800 (which reads no argument),
  returns straight after its D_00810CA4 0 / 1 / 2 arms (only the
  D_00810CA7 arms reach the D_00810CA5 == 6 call of 0022E7F0; the C runs
  it after every arm) and its D_00810CA7 == 9 arm calls 001D2830(1, 0)
  (the C passes one argument). The translation follows the instructions;
  test-camera-aim-reference executes them.
- **An original quirk (LAUNCHER_OPTIONS.md "Bug fixes"):** 0017B300's manual
  reload (mode 2, byte-matched) compares the total against 30 minus the
  magazine: with magazine 16 and total 17 it loads 30. Reproduced.
- **em_weapon** keeps its historical comments; where they disagree with
  this document (the shot range, the hit call's arity, the reload rule)
  this document is right.

## 9. Chain step AIMLIVE (2026-10-02): the fire path, the reload and the knife behind the gate

Everything below runs behind the gate (section 1); ordinary play is
unchanged (section 9.5 has why the gate stays).

### 9.1 The muzzle node 001F5040

00187CC0's 001F4F40 allocates a class-0xC record (+0x10 = 001F5040);
`em_aim_fire_runtime` claims it for `em_aim_fire_flash` at the allocation,
the settle accepts it and the AREA11 binder's row (`tick_aim_record`) runs it
with D_00275B40 = its +0x110 (the walk's 001CB590). Its behaviour is
em_area00_fx_001F5040 (composed by em_aim_fire_world_live). The record bytes
it owns beyond the pool header, each readable only after a write since the
allocation: +0x28, +0x44 / +0x4C (001CA5E0: kind 2, +0x4C = 001CACB0),
+0xD0..+0x10F (00187CC0's matrix), the +0x110 slot words and +0x230..+0x23B.
Its workers, each one translation: 001C6120 over the Roger export's library
bank D_0028A56C (the export now carries the common bank's spans 0x07..0x0F
and 0x19, flagged as library spans so the pose hosts' regions skip them;
of the library models 0x0E..0x19 only model 0x16's weight words differ from
the disc at run time, rewritten by 001D19D0 -> 001D9070, FRAME_RENDER_HEADS.md),
001C6150 / 001C62C0 /
001C9610 (em_owner_services_original; 001C63D0 is em_area00_world's and
tail-calls 001C9610 back here), 001AF780 / 001AF800 on the one slot stack,
001D80E0 (em_effect_original -> 001D7FA0 on the render context's point
lights) and the draw 001CACB0 = 001CABA0(node, +0x44).

001CABA0 is the channel-3 draw of a class-2 object unit: the GS state block
at 0x815A20 (TEST 0x53001, ZBUF with ZMSK 1, ALPHA 0x8000000068), the skin
records 0x816B40 / +0x80 and the clip records 0x816E40 / +0x80, the
template PRIM 0x07C (the clip pass 0x07B; both with ABE). The unit is CALLed
from the chain page D_007635C0 at 001CAAC0 / 001CB760: em_chain_page keeps
the CALL as a unit marker and em_chain_page_live runs em_object_unit's class
2 there, so the unit's triangles land in the page's order with the class-2
state (Metal: the page shader's HIGHLIGHT and textured type-3 triangles).
Oracles: test-owner-draw-reference part E (32 of the 218 captured 001CABA0
draws quick, plain and clip, every store against the original
instructions; the EE oracle hooks 001CAAC0, whose VU op it lacks) and
test-object-unit-reference part K (13 class-2 units, 7 of them clip, 50
triangles equal the original microcode's).

### 9.2 The shell casing

The gun tick 00188630's 001F4010(3, 0x700036A0): em_equipment_live hands the
frame it staged to `em_aim_fire_runtime_001F4010`, which copies it into the
composition's 0x700036A0..0x700036DF (the one scratchpad) and runs
em_area02_misc_001F4010 -> 001F2F90 / 001F3340 (em_area00_fx_debris). The
barrel's sweep 001F40C0 calls 001F3620 / 001F3E30 through em_effects_live's
particle hook into the same composition: em_area00_fx_debris is their one
owner (em_effect_manager's copies are not bound; outside the gate nothing
seeds a particle, and a live one without the hook faults as before).
001F3620's 001CA3B0 / 001CA4D0 are em_area00_world's, its bounce sound
001F02C0 em_area00_fx's (-> 001FBD50, em_sfx); 001F3E30 draws the library model 0x19 through
em_owner_draw_live (001CA7B0 cull, 001C7900 face attach, 001CA940 the
library's REFs). Data: D_0025A350's block (0x34B0 bytes: the debris rows,
gravity, 001D80E0's colour), D_0026EA80 (0x64) in the effect-table export;
the casing's texture in the object-texture export.

### 9.3 Melee 0x21 / 0x22 and the knife

The gate now selects the melee states (em_player.c: every slot 0x1D..0x22).
Their bodies (001735C0 / 00173E60, em_player_weapon_states_b) drive the knife
node (flavour 4: 0018A1F0 / 00189D30, em_player_equipment through
em_equipment_live). The knife's callees go to the composition through
`em_equipment_live_set_world`: each call copies the node's staged scratch
0x700038A0..0x700038DF to the composition's scratchpad, runs the original
with D_00275B40 = the knife's bones, and copies it back.

- 001AA840 (em_area00_fx) walks the published class-4 list; its 001AA7A0
  (em_aim_fire_leaves, new: 48 original-instruction reaches in
  test-aim-fire-leaves-reference) sets the struck record's +0x36.
- 0019B2C0(player + 0xB0, 0x700038A0, 6), the reach probe, is
  `em_coll_move_probe_0019B2C0` (new in em_coll_move_original): the move
  walk without an actor (query class -1, 0x70003254 = 0, bit 0 never
  tested, bit 31 moves a0's x / z) over the move walkers' world and scratch.
  test-coll-move-reference executes the original 0019B2C0 against it (60
  cases quick, 216 full, returns 0 / 2 / 4, every scratchpad byte equal).
  The node then reads 0x700031B0 / *0x700031D0 / *0x700031D4 (the move
  scratch's point, record and entity; a cell hit names D_700030B0 and its
  +0x1A / +0x24 are the walker's 0x700030CA / 0x700030D4).
- 00189EC0, 00189FE0 and 0018A180 are em_area00_world's; 001F00A0 and
  001EFF10 em_area00_fx's; 0019A570 the segment walker's.
- 001EFF10(0x8000000D, bone, 0x700038A0..D0, 10.0) spawns the trail node
  (an effect record, +0x10 = 001F18C0) through em_effects_live; the binder's
  row runs it as an extended effect node (em_aim_fire_runtime's other tick)
  with D_00275B40 = its +0x110. Its behaviour is em_area00_fx_001F18C0 (with
  001F1550 / 001F15F0); `em_aim_fire_trail` holds its three slot words and
  serves the slots' bytes from the one slot stack's raw records (001F18C0
  uses each slot as an eleven-quadword point history with ten age words
  overlapping its +0xA0; 001AF890 clears a pushed slot, so the bytes after a
  pop are the original's). 00189D30 hands 001EFF10 the knife bone's slot
  + 0x90 (its world matrix), so 001EF9D0 reads the hand's place at the
  slot's +0xC0 and the trail node seeds and draws its points through that
  matrix (section 11.3).
- The trail's packet: 001F15F0 takes its depth key from 001CCF70 of the
  node's +0x1F0 + 0x30, the knife's world matrix row 3 (the hand). Its two
  00102990 calls and the GIF packet (PRIM 0x4C, an untextured Gouraud
  blended strip) go to the chain page, which draws it on the flat GS path
  (section 11.3).

### 9.4 Verification

The level smoke's AIM replays (LEVEL_SMOKE.md "The AIM replays";
`make test-level-smoke-aim`, part of test-level-smoke-side and -full) play
the main line through truck_crossing, align on a capture's row f13 and feed
the capture's pad script; `check_aim_replay` compares every row to the
capture's last: exactly the player's +5 +6 +7 +1F0 +1F1, clip, clock, +230,
+0x274..+0x27F, the fire mode, magazine, reserve, light and the gun's +2E;
per row the set of fire-path records (markers, muzzle nodes, impact effects
0x1B / 0x23, knife trails) with their +4 / +0x0C / +0x0D / +0x28 exactly;
each muzzle node's +0xB0 in the player's frame within 0.002, each struck
point's bearing within the start heading's difference + 0.002 (the range
along a grazed surface depends on where the run stands), the trail's +0xB0
as stored. Measured (2026-10-02):

| Side run | Capture | Rows | Fire-path records | Largest |
|---|---|---|---|---|
| aim_fire | aim_03_single_fire | f13..f460 | 343 | muzzle 0.0000; struck points 13.52 along the surface |
| aim_both | aim_02_r1_r2_both | f13..f368 | 0 | - |
| aim_reload | aim_06_reload_partial | f13..f417 | 343 | muzzle 0.0000; struck points 12.45 |
| aim_reload_empty | aim_07_reload_empty | f13..f1231 | 2940 | muzzle 0.0000; struck points 12.45 |
| aim_melee | aim_09_melee | f13..f267 | 40 (the trail on every captured row) | - |

Seams and data: test-aim-fire-world-live now also checks 0019B2C0's move
scratch views (they replace the segment walker's until a segment probe
runs; bit 31 writes a0 back; a refused probe faults) (92 checks);
test-disc-textures-reference pins the grown exports (469 object textures,
9 page textures, each decoding identically from all 15 route captures'
GS memory).

Default: aim_r1_hold, aim_r2_hold, aim_fire and aim_melee (about 4.5 min,
four runs side by side); EM_TEST_FULL=1 adds aim_both, aim_reload and
aim_reload_empty (all seven PASS). Every run also passes the level smoke's
whole-run checks (render context, chain page, effects, owner units, rand
order, sway, marker colour, shadow, static world).

### 9.5 What was left at AIMLIVE (all closed in section 10)

At AIMLIVE this section listed what kept the gate. Corrected and closed:

- **The impact effect's kind-0 VU1 program.** A round whose marker
  resolves to code 0x201 (aim_04's two later hits, aim_10's rounds, the r1
  / r2 fixtures' round) makes the marker spawn 0x80000060, whose handler
  001EACF0's 001CFBE0 chains kind 0: the VU1 program at 0x230800 (the
  streak program), a variant of the sprite program that uses the EFU
  (ERCPR and ERLENG, read back with MFP after WAITP). This section said
  "the port has no measured EFU model". That was wrong: the background
  renderer already models ERLENG (`em_background_gs.h`, the per-vertex
  normal length), and section 10.1 uses the same model for both EFU
  instructions. What is true is that neither EFU result is measured bit
  for bit against the original: no capture holds this program's output
  (section 10.7).
- **The gun lamp.** 00187780 / 00187690 and 001D9530 -> 001D91A0 (with
  001DA290) are translated (section 10.2); in AREA11 001D9530 is not
  reached (D_008106C8 has 0x20000000).
- **aim_11 (the knife on the cable)** is replayed: the stick replays walk
  with the capture's own stick bytes (section 10.4).
- **The gate** is gone (section 10.3).

## 10. Chain step AIMLIVE fix round (2026-10-02): live by default

The original aim / fire path became the only one in AREA11. This section
records what that needed and how it is verified.

### 10.1 The streak program 0x230800 and the EFU

`em_vu1_page_programs.h` translates the program 001CFBE0 kinds 0 / 4 load
(the packet at 0x230800, 0xF70 bytes, exported with the effect tables:
`tools/export_effect_tables.py`, STARTUP.md): its particle loop (micro
addresses 0x0C3..0x0F3) and its quad emission (0x119..0x173), with the
sprite program's batch kick (`emvup_sprite_batch(c, streak)`). The EFU:
ERCPR is the reciprocal 1 / x as VDIV computes it (truncated), ERLENG is
1 / sqrt(x^2 + y^2 + z^2) evaluated in double and truncated to single,
denormals flushed, the model `em_background_gs.h` already uses for the
background's normal lengths. The P register's latency is modelled as the
program's schedule needs it: ERCPR 12 cycles, ERLENG 24, WAITP stalls
until P is ready, MFP reads P without an interlock (as the hardware);
`tools/chain_page_model.py` executes the program's own microcode from the
user's ELF with the same EFU model (`EFU_LATENCY`, `efu_ercpr`,
`efu_erleng`) and compares every vertex the C emits
(test-chain-page-reference: a synthetic streak page, the timing producers,
the branches at 0x0FD / 0x0FF / 0x142 / 0x174 and an EFU fault case). The
chain page counts the program's MSCALs and primitives (`mscal_streak`,
`streak_prims`); the level smoke re-walks the first 12 pages that run it
with the original microcode (level_smoke_chain_page.py).

The EFU model's equality with the hardware is not proven bit for bit: the
model is consistent with the background renderer's, and the two
instructions are a single rounding step each, but no capture holds the
program's output. Section 10.7 names the capture that would settle it.

### 10.2 The gun lamp

Square in the stance switches D_00810D3C (0017A970), and while D_008106C7
is set 00188ED0 calls 00187780(node, a1, a2) from the gun's tick.
`em_aim_fire_lamp.c` translates, from the original instructions (the
decomp's C of 00187780 and 001D91A0 is NEARMISS): 00187780 (the lamp
matrix from the bone's +0x90..+0xBF, a quarter turn and the node's +0xB0;
the 250-unit probe 0019A570(.., 6, 0); the cone length and scale; the view
attenuation in mode 0; 0021B9A0; the flare 00187690, twice in mode 1; the
cone 001D9530 unless 001B0070() has 0x20000000), 00187690 (the flare's
source block D_002487E0, its rows written, through 001CCF70 / 001CFA60 /
001CFBE0 kind 1), and the cone path 001D9530 / 001D91A0 / 001DA290 /
001DA1E0 with the render-context helpers 001D4E20 / 001D4EB0 / 001D4B80 /
001D4C30. `tools/test_aim_fire_lamp_reference.py` executes each original
and compares every store, call and fault (60 cases by default, about 5 s;
156 with EM_TEST_FULL=1; a mutation sweep, sign-extension cases included).

In AREA11 D_008106C8 is 0x20081910 (bit 0x20000000 set) in every capture,
so the cone shells are skipped and the lamp draws only the flare sprite
(decomp CURIOSITIES "The gun light draws"); the composition refuses
001D9530 with a message if it is ever reached here. The flare's block
D_002487E0 lies inside the pose tables' read-only region 0x248740..0x248ACC
(the EMAF export); the composition owns the block's 0x90 bytes as a
writable view into the same table bytes (`aim_regions`,
em_player_closure_live). The flare's TEX0 words (modes 0 and 1) joined the
page textures (`tools/export_page_textures.py` FLARE_TEX0;
test-disc-textures-reference pins the file). 0x700031BC, the scratchpad
word after the probe's point that the quadword loads of 0x700031B0 read
(001028D0 in 00187780, 00102948 in 001F3340), has one owner
(`EmAimFireWorldLive.word_31BC`); the knife's 0019B2C0 binding
(`w_0019B2C0`, em_equipment_live) faults unless its a / b are the
player's +0xB0 and the scratch vector 0x700038A0, and reads the 16 bytes
at 0x700031B0 through that owner.

### 10.3 What was retired

- The gate `EM_AIM_FIRE_ORIGINAL` and its diagnostic header
  (`em_aim_fire_diagnostic.h`, deleted).
- em_weapon's stand-ins: the stance hold, the firing loop, the gun tick,
  the lamp gate, the laser and muzzle-flash drawers, the reload and melee
  stand-ins (em_weapon.c went from about 2,750 lines to 30: the four global
  bytes and their accessors).
- The camera stand-in `camera_mode1_aim` (em_camera.c) and the Metal beam
  pass (em_gfx_beam*: the stand-ins' laser dot, muzzle-flash sheets and
  flashlight cone mesh, em_gfx_metal.m); the gun light's per-pixel spot term no longer follows the gun
  (`em_gfx_spot_light`'s one remaining user is the menu turntable's fill).
- The test `test-weapon` (tests/weapon_fire_test.c, deleted): it tested the
  retired stand-in. Its subjects are now covered by the oracles in section
  2 and the side runs.
- The legacy death-clip choice in em_player_damage.c read the stand-in's
  state; it now reads the live record's stance (+5 0x1D..0x22). The
  original death states 0021E240 / 0021E830 stay untranslated
  (FIRST_LEVEL_AUDIT L02).

### 10.4 The side runs

`make test-level-smoke-aim` (tools/test_level_smoke_aim.py) plays each side
run as its own headless process, side by side, then checks each with
tools/test_level_smoke.py. Default: aim_r1_hold, aim_r2_hold, aim_fire,
aim_melee, aim_light, aim_world, aim_cable; EM_TEST_FULL=1 adds aim_both,
aim_reload, aim_reload_empty. The stick replays aim_world (aim_04) and
aim_cable (aim_10, then aim_11 from aim_10's end) take the captures' pad
scripts with both stick bytes (the runner writes them from the captures'
trace.json to build/level_smoke_aim/<run>.pad, `EM_AIM_PAD_SCRIPT`), align
on the idle clip's +3C at the capture's row f7 and start at the capture's
first stick input (f5). The checker finds the aligned tick by the run
log's "aligned counter=" line (for the button replays it also requires the
first stance tick to agree). The struck points are compared by direction
only (their bearing from the player and their elevation from the muzzle),
because the run stands about 0.65 from the capture's start (section 5).

Duration: each run must play the main line to route 08's end (about 5,550
ticks, about 50 s alone) before its capture starts: the port has no state
restore, and a run cannot start from a save state. So a side run cannot
take about 10 s. Measured 2026-10-02 on the 10-core development machine
while other sessions kept its load average near 50: the seven default runs
side by side and their checks took 8 min 57 s (`make test-level-smoke-aim`;
it is part of test-level-smoke-side and -full, not of the default
test-level-smoke). The ten with EM_TEST_FULL=1 add aim_both, aim_reload and
aim_reload_empty to the same parallel batch. A shorter run needs a state
restore at route 08's end, which the port does not have; that is the
lead's decision (section 10.7).

### 10.5 The world hits and the cable

The two stick replays met five things the earlier runs never reached; each
is now an original's translation with its oracle:

- **001A9C40** (aim_04, the first concrete hit): the impact marker is a
  class-1 record, and the close-out pass 001A9D20 walks class 1 against
  class 2 (Roger alone in AREA11) through 001A9C40, which sets the class-2
  record's +0x0A bit 0 when the marker lies within its type's radius (15 or
  20). Translated in em_coll_list_passes (`em_coll_list_001A9C40`, bound in
  em_collision_world); test-coll-list-passes-reference executes the
  original on its own (every type 0..9 and 0x13 / 0x80 / 0xFF, the +0 bit 1
  gate, both radii at equality and one ulp beyond). The pass reads the
  marker's +0 and +0xB0 through the composition's own record view
  (`em_collision_world_bind_records`, em_aim_fire_runtime).
- **The ring decal's lanes**: the shots' ring decals (001F0460) activate
  slots of 001F0720's lanes, which then draw; the chain page counts their
  strips (`lane_strips`) and the level smoke allows them only from a side
  run's first tick, re-walking the first 12 such pages with the original
  microcode.
- **001B61C0** (aim_11, the knife's hit 0018A180): the pad actuator
  request goes to its one owner em_pad_actuator.
- **001EAB50** (the cable hit's effect 0x80000045, subtype 0): translated
  in em_effect_kinds from the instructions:
  the fading sprite 001CD520 while work +0x54 < 0.5, the colour words at
  0x70003600..08, then 001CFB50 / 001CFBE0 kind 0 with D_00255590;
  test-effect-kinds-reference executes it (40 cases by default, every
  instruction). D_00255590 joined the effect tables and the sprite's TEX0
  word the page textures.
- **The kind-2 VU1 program 0x232540** (0021AAC0's sparks, 001CFBE0 kind 2):
  its micro 0x000..0x0F8 are the streak program's; the emission draws each
  particle as a line from the head to the tail (P = ERCPR of the head's w,
  Q = 1 / the tail's w, the colour times the tail's fog weight).
  Translated in em_vu1_page_programs.h (`em_vu1_kind2_program_mscal`);
  test-chain-page-reference executes its microcode over 40 synthetic
  batches (every conditional branch both ways, the Q / P / clip producers,
  an EFU operand fault); its packet (0xD50 bytes) joined the effect tables.
  0021A500's strand strips are DIRECT packets (`direct_strips`), and its
  TEX0 word joined the page textures.

`aim_cable` replays aim_10 and then aim_11 from aim_10's end (aim_11 was
recorded from aim_10's end save state, seven idle frames later by the
frame counter; the run plays those seven ticks idle). Besides the replay's
rows it compares, on every row, the security gun's and the cable's +0, +4,
+5, +9 and the cable's +0x36 with the capture's (the knife hits at aim_11's
f249; the gun goes to lifecycle 2, the cable to 2 and 3 and is freed at
f310), and the cable reaction's nodes 0021AAC0 / 0021A500 (+0xB0 equal in
the world) and the hit's effect (its height and the pillar face's z equal;
where along the face follows where the run stands). check_gun_fan's
"static gun and cable" comparison stops at the replay's first tick.

### 10.6 Verification

All on 2026-10-02 with the final binary (machine load average 30..70 from
other sessions):

- Every make test target outside the level smoke (267 targets) PASS,
  among them test-aim-fire-lamp-reference (60 cases; 156 with
  EM_TEST_FULL=1), test-chain-page-reference (40 streak and 40 kind-2
  synthetic batches; 600 + 600 with EM_TEST_FULL=1, every MSCAL's memory,
  registers and XGKICKs equal), test-effect-kinds-reference (2,400 handler
  cases with EM_TEST_FULL=1, 001EBBB0 and 001EAB50 included),
  test-coll-list-passes-reference (5,460 staged 001A9C40 runs),
  test-disc-textures-reference (16 page TEX0, the file pinned),
  test-aim-fire-world-live (95 checks).
- `make test-level-smoke-full` PASS (24 min 1 s): the main line through
  roger (18 live phases), the side runs, and all ten AIM side runs row for
  row (aim_r1_hold, aim_r2_hold, aim_fire, aim_melee, aim_light, aim_world,
  aim_cable, aim_both, aim_reload, aim_reload_empty). On the stick replays:
  aim_world 1,397 rows and 319 records, 48 pages with the streak program
  (4,830 triangles, 16 re-walked with its microcode), 914 lane triangles of
  the ring decals (26 pages re-walked); aim_cable 2,020 rows and 1,002
  records, the gun and the cable on every row, 112 streak pages, 91 pages
  with the kind-2 program (13,035 primitives, 12 re-walked), 2,148 lane
  triangles, 7,128 DIRECT strip triangles of the parted strand; aim_light
  117 pages with the lamp's flare (12 re-walked).
- `make test-level-smoke-ps2-drive` PASS (5 min 21 s).
- `EM_STARTUP_TEST=newgame-control` PASS (displacement 9.599849, as
  before), and with `EM_AIM_FIRE_TEST=r1`, `r2`, `r1hold`, `r2hold` PASS.
- `make all` builds with no warning; tools/check_no_disassembly.py is
  clean on every file this step changed.

### 10.7 What is left

- **The EFU's results, measured.** A capture of the streak or kind-2
  program's output settles sections 10.1 and 10.5: an AIM beat paused on a
  frame where a 0x80000060 effect draws (aim_04 after f592, aim_10 after
  f331) or the cable's sparks draw (aim_11 after f250), with the VU1 data
  memory and the GS primitives (or the page bytes) of that frame. It needs
  PCSX2 and is left to the lead (this step launched no emulator).
- **The side runs' duration.** About 10 s per side run is not reachable:
  each run must play the main line to route 08's end first (section 10.4).
  A state restore at that point (a port-side snapshot of the whole game
  state, or a new capture-derived start) would be a new mechanism; the lead
  decides whether to build one.
- **The decomp's FUNCTIONS.csv** still lists 001EAB50 and 001A9C40 as
  undecompiled while their C exists (without a NEARMISS marker); that is
  the decomp lane's to reconcile.
- **001D9530's cone shells** are translated and instruction-tested but no
  AREA11 frame reaches them (D_008106C8); a later level that clears
  0x20000000 (AREA01's 0x8D00 / 0x8D01) is where they first draw.
- The original death states (0021E240 / 0021E830) are not translated;
  em_player_damage keeps the legacy death sequence (L02).

## 11. Chain step AIMCAP (2026-10-02): every AIM beat against its recording, whole records

### 11.1 The side runs

Eleven side runs (`make test-level-smoke-aim`; LEVEL_SMOKE.md "The AIM
replays" and "The AIM side runs' whole records") cover all twelve AIM
beats: aim_r1_hold / aim_r2_hold (aim_00 / aim_01), aim_fire (aim_03),
aim_both (aim_02), aim_world (aim_04), aim_burst (aim_05, new), aim_reload
(aim_06), aim_reload_empty (aim_07), aim_light (aim_08), aim_melee (aim_09)
and aim_cable (aim_10, then aim_11). aim_burst plays aim_05's own pad
script: START opens the status hub, the stick and Cross open the SPR4 page
00211970 and its SELECTOR part page 00217FA0, Down / Cross / Left / Cross
pick the 3-round burst (D_00810C61 = 1), Triangle closes, and R1 fires one
short press and two 60-frame holds (00170A60 states 0x14..0x17). Every
press lands on the capture's frame: the status screen runs main-loop
iterations that close out neither a world nor a status frame, so the
phase's frame function now runs once per iteration (Phase.every_tick,
em_level_smoke_test_tick_end at the game task's end); without it the
runner fell two frames behind the pad script at START.

Besides the fields of sections 9 and 10, each run now compares, on every
row, the whole player record D_008102B0 +0x000..+0x31F, the gun node
(+0x20) and the knife node (+0x18) at +0x00..+0x3F, +0xA0..+0xCF and
+0x1F0..+0x21F, the camera bytes D_008101E4..E7 and the status block
D_00810130..+0x5F (the tick log's `aimrec`, EM_LOG_AIM_RECORDS=1), and in
the button replays the camera's eye and targets in the player's frame.
What cannot be equal bytes is compared for what the code does there
(tools/test_level_smoke.py check_aim_records; LEVEL_SMOKE.md lists every
rule): the place and heading words in the player's frame; the words the two
runs' histories leave before the capture's start (the state countdown
+0x28 / +0x2A, +0x208, +0x248, the snap target +0x258 / +0x2F8, +0x260,
+0x264, +0x294, the velocity +0x2E0) change on exactly the capture's rows,
to its value or by its step; the laser dot by its bearing only (its range
follows where the run stands: 137.8 ahead in aim_03, 125.3 in the run);
in the stick replays the walk's foot and contact words and the camera's
eye and target (the follow camera meets the walls where the run walks)
are not compared, and the gun's world vectors only on the aim-stance rows.

### 11.2 Fixed: the knife's trail wrote into the player's node records

The whole-record comparison found the player's hip (+0xB0, the pose
host's bone 1) leaving aim_09 at f27, one frame after the first swing's
hit window: the port's pose replayed the clip from its start, offset 1.0
sideways, and from f165 the camera's eye stood 20 units low. Cause: the
player's 21 node records live in em_player_record_pose's storage at
0x7D5840.. (the record's +0x110 words) and were never popped from the one
bone-slot stack 001AF710, so the first 21 pops of everything else returned
the player's node addresses; the trail node 001F18C0 (spawned by the hit
window's 001EFF10) popped three of them, and its point history, written
through the aim/fire composition's address map (em_aim_fire_live_map
resolves the player's pose regions before the trail's slots), went into the
player's nodes 0..2.

The fix is 0015C420's own (byte-matched): it pops +0x0C slots into
+0x110.. before its first child (0018A880(4, 0), the knife). The port's
0015C420 path (em_area11_spawn_player_children_0015C420) now pops them from
the one stack and faults unless each pop is the address the record's
storage holds; it also writes 0015C420's +0x30 (&D_00275490), +0x58
(D_0028A578[0], 0xD1B9C0 in every captured AREA11 image, as +0x40's
D_0028A580[0] is 0xD689C0) and +0x5C (0x10101), which the record image
lacked. Every later pop now returns the original's address: the knife's
bone is 0x7D9E20, as in aim_09's snapshot (FIRST_LEVEL_AUDIT.md 1b item
10, FACE_ATTACH.md section 5). aim_melee's hip now equals the capture's
on every row (0.0000 in the player's frame), and so does the camera.

### 11.3 Fixed: the knife's trail is drawn

The census run of the AIM lane (decomp `build/s87/census/runs/AIM/
aim_09_melee.json`) first hits 00102990 at aim_09 f26, with 001F15F0's
first call. In the AIM beats only 001F15F0 calls 00102990 (001CEFD0 and
00201F70 run in none), and only after 001CCF70 returned a key, so the
original drew the trail from its first call. The port's 001CCF70 answered
0xFFFFFF on all 40 calls of aim_melee.

The offline check, from aim_09's own capture:
- The camera is the same on all 267 rows of aim_09 (eye, target, cam_eye,
  cam_tgt), so the end snapshot's clip matrix (render context 0x811CC0 +
  0x2240, 001CD370(0)) is the one 001CCF70 used at f26.
- Against that matrix the world origin is outside the clip volume: its clip
  coordinates are x -388.7, y 303.7 against w 145.8. So the original's
  point at f26 was not the origin, and the port's identity-matrix point
  (the origin) was the divergence, not the clip matrix.
- 00189D30 passes 001EFF10 `*(*(knife + 0x14) + 0x110) + 0x90` (decomp
  src/func_00189D30.c): the knife bone's slot + 0x90, its world matrix. In
  aim_09's snapshot `*(0x7AB440 + 0x110)` is 0x7D9E20, so the trail's
  +0x1F0 is 0x7D9EB0 and its point is the slot's +0xC0, the hand (370.45,
  193.95, 359.58 at the end), with clip coordinates x -1.5, y 11.5, z 53.05
  against w 53.25: inside. The slot's +0x00 matrix is the identity in every
  AIM snapshot; the knife's translation at f26 itself is not recorded, but
  the hand's place is the point the code tests.
- The port's live worker (em_equipment_live w_001EFF10) passed the slot
  without + 0x90, and a read-only view of the slot's +0x00..+0x3F served
  it. Fixed: it passes the slot + 0x90 and the view is removed.

The packet path that follows was not reachable before, and it needed two
bindings:
- 00102990 (the colour words) is bound to em_area00_low's translation in
  em_aim_fire_runtime, as 00102870 is.
- The chain page refused untextured Gouraud triangles. 001F15F0's GIF tag
  has PRIM 0x4C (a blended Gouraud triangle strip, untextured), and
  em_gfx_metal now draws it on the flat path (Cf = Cv, Af = Av), as it
  already drew untextured lines.

Result (2026-10-02): all 40 trail calls of aim_melee get a key and write
their packet, and aim_melee passes with every rule. No mid-swing GS frame
of the original exists, so the trail's pixels are not compared (a frame
capture at aim_09 f26..f40 would compare them). Decomp CURIOSITIES.md
entry 28 ("never drawn") is wrong: the trail is drawn.

### 11.4 The sound handle (open: the port's track choice is not deterministic)

The melee states keep 001FBD50's return, the track the sound driver
00119EA0 allocates for the swing, at +0x302 to stop it later. 00119EA0
takes the lowest free track. In the port the value is not reproducible,
and the original's values differ from the port's on most handle rows, not
only on a few:

- **Cause (a port defect).** em_sfx.c's TRACK HAND-OFF frees tracks on the
  host audio thread: the 00118EC0 reaper runs in the device callback, which
  stores FREE on wall-clock time, while the game thread's sfx_start (the
  00119EA0 side) takes the lowest FREE track on the game's tick. The level
  smoke runs EM_UNCAPPED, with game ticks faster than real time, so which
  tracks are free when the swing starts depends on host timing and load.
  The record's +0x302 is therefore game-visible state that follows the
  host clock.
- **Evidence.** Five runs of aim_melee on 2026-10-02 gave another track
  than the capture's on 17, 222 and 66 rows (three runs of the AIMCAP
  tree) and 66 and 17 rows (two runs after this round's trail fix). In
  the 222-row run, the handle rows from f13 read 3, FF, 1, FF, 1, 5, 1, FF, 1
  in aim_09_melee's `pl` and 3, FF, 0, FF, 0, 4, 0, FF, 0 in the port's
  `aimrec`: one track lower on almost every handle row.
- **What the side runs check.** check_aim_records requires a handle on the
  same rows as the capture (0xFF in both, or a track in both) and counts
  the rows with another track. The tolerance covers this known port
  nondeterminism. It does not stand for a measured sound-state difference.
- **What removes it.** FIRST_LEVEL_AUDIT.md 1b item 1, for the AUDIO step:
  the 00119EA0 track state that the game thread sees must follow the sound
  driver's per-field tick (001152D8 and its 00118EC0 reaper, on the game's
  field count), not the device callback. Then +0x302 is deterministic and
  can be compared with the capture byte for byte.

### 11.5 The census

The AIM lane's delta (114 functions no route beat runs) is now rows of
FIRST_LEVEL_CENSUS.md section 3 (section 1.56), all 114 live: 113 measured
over the eleven side runs with an instrumented build, and 00102990 live
since the trail's fix (11.3; its 80 calls in aim_melee).

### 11.6 What is left

- The trail's pixels (11.3): no original frame of a swing to compare.
- The sound handle's track (11.4): the port's track hand-off follows the
  host audio clock; the AUDIO step (audit 1b item 1).
- The EFU and the side runs' duration (section 10.7) stand.


## 12. Chain step BRANCHES (2026-10-03): the knife on the boxes

The BRANCH recordings br_04 / br_06 (decomp CAPTURES_C10.md "BRANCH") break
the AREA11 boxes r5 and r6 with the light melee; the side runs
br_crate_stack and br_west_ledge (LEVEL_SMOKE.md "The BRANCH side runs")
compare them row for row. What the knife's strike needed:

- **00189FE0's read of the knife's +0x36.** The strike copies the knife
  node's +0x36 (the damage 001735C0 wrote: 3) to the box's +0x36 with
  0x1000. The node's one storage of that halfword is its pool record's h36
  (em_player_closure_live wb_link18 publishes the melee's write there); the
  composition now has it as a read view of every equipment node
  (em_equipment_live_field, em_equipment_live_regions).
- **The debris node 001F2BA0.** The break's 0x8000000A spawns a node whose
  behaviour is em_area00_fx_001F2BA0 (with 001F2E90 / 001F2F90 / 001F3620 /
  001F3340 / 001F3E30 and em_area00_world's 001C6200), run through the
  composition like the trail (other_tick, em_aim_fire_world_live). Its slots
  (one per piece: the debris row's +0x4C, sixteen for subtype 0) are
  em_aim_fire_trail's, which now keeps the slots of both callbacks (at most
  56 words: more would reach the node's +0x1F0). The SDK leaf 00102C58 (its
  in-place euler on +0xD0) joined the composition's SDK leaves
  (em_aim_fire_sdk_memory, em_owner_services_euler_00102C58; a partial
  overlap is refused). The area binding row is 0x001F2BA0.
