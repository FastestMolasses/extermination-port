# AREA11 aiming and firing (audit 1b item 14, census lane L28)

The player's armed stances (R1 0x1D, R2 0x1E and their 0x1F / 0x20
variants), their fire machines, the laser sight, the shots and the cable
reaction. Current state: the original workers are translated and
instruction-tested, and they are bound behind a **diagnostic gate**. Ordinary
play still runs em_weapon's stand-ins for the armed stances. Since chain step
AIMCAM (2026-10-01) the aim camera is translated and bound
(CAMERA_LIVE.md section 7): behind the gate R1 and R2 draw, hold and holster
on their original bodies with no fault, and the level smoke's side runs
`aim_r1_hold` / `aim_r2_hold` compare them row for row with the AIM
captures aim_00 / aim_01 (section 5). Since its fix round the round's
impact marker 0018ABA0 is bound (section 3); the first round still stops,
at the muzzle node the flash 00187CC0 allocates (section 7).

History: the work was done on the Codex branch `codex/aim-fire` (three
commits on 7d7bdb5) and brought onto main by chain step AIM (2026-10-01)
after an audit (section 6). The branch and its worktree are not used any
more.

## 1. Gate

- `EM_AIM_FIRE_ORIGINAL=1` together with `EM_AIM_FIRE_TEST=r1` or `r2` (a
  developer switch, LAUNCHER_OPTIONS.md "Not launcher options") selects the
  original stance slots 0x1D..0x20 in `player_states_bind`, skips em_weapon's
  R2 hold, its SPR4 update and its weapon draw, installs the world / render /
  cable extension (`em_aim_fire_runtime`) and routes the equipment nodes'
  seven aim / shot callees to the composition.
- Without the switch, ordinary play is unchanged apart from the faithful
  changes in section 4.
- The input fixture (`em_aim_fire_test.c`, phase 7 of the newgame-control
  test) waits for the validated first control, waits 60 ticks, holds R1
  (key E) or R2 (key 3), presses Circle (key L) at ticks 100..101, releases
  the trigger at 150 and checks the stance and a fire sub-state were seen and
  the player is idle at tick 240. `EM_AIM_FIRE_TEST=r1hold` / `r2hold` hold
  and release without the Circle press and require that no fire sub-state
  is seen. It writes no game state. It is not a capture comparison: that is
  the level smoke's side runs (section 5), which set the gate with
  `EM_AIM_FIRE_TEST=smoke` (any fixture name enables it; the newgame-control
  fixture runs only under `EM_STARTUP_TEST=newgame-control`).

```
EM_STARTUP_TEST=newgame-control EM_AIM_FIRE_ORIGINAL=1 EM_AIM_FIRE_TEST=r1hold build/extermination
EM_STARTUP_TEST=newgame-control EM_AIM_FIRE_ORIGINAL=1 EM_AIM_FIRE_TEST=r2hold build/extermination
EM_STARTUP_TEST=newgame-control EM_AIM_FIRE_ORIGINAL=1 EM_AIM_FIRE_TEST=r1 build/extermination
EM_STARTUP_TEST=newgame-control EM_AIM_FIRE_ORIGINAL=1 EM_AIM_FIRE_TEST=r2 build/extermination
```

`r1hold` and `r2hold` PASS (the stance, +6 = 2 sixteen ticks later, the
release and holster, idle by tick 240, no fault). `r1` and `r2` FAIL,
honestly: the Circle press enters 00170A60's fire states, the round
001861C0 resolves and allocates its impact marker (bound since the fix
round), and the muzzle flash 00187CC0 stops at the node 001F4F40 allocated
for it: its +0xD0 matrix has no owner (section 7).

## 2. Original path and owners

| Original | What it does | Port owner | Evidence |
|---|---|---|---|
| 001607D0 | the action / held / edge dispatch that enters the stances | em_player_weapon_states_a (live since census L12) | test-player-weapon-states-a-reference |
| 0016FCF0, 001703E0, 001729A0, 00173000 | the stance loops 0x1D..0x20 | em_player_weapon_states_a / _b (translated earlier) | test-player-weapon-states-a/b-reference |
| 001735C0, 00173E60 | melee 0x21 / 0x22 | em_player_weapon_states_b | same; **not** selected by the gate (section 7) |
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
| 0018A6B0, 00188630 | the equipment nodes and the gun tick | em_player_equipment through em_equipment_live (live) | test-player-equipment-reference, the level smoke's check_effects |
| 00827490 | the AREA11 cable | em_security_gun (live) | SECURITY_GUN.md |

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
- `em_aim_fire_runtime` (gate only) adds pool header fields, effect node
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
  00186A60, 001872C0 and 00187CC0 go to `em_aim_fire_binding_run` under the
  gate, with D_00275B40 switched to the node's own bone table for the call;
  outside the gate they fault as before.

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
- **The cable hit** (00827490 with +0x36 set) still faults at 001EFE00 in
  ordinary play; it now reaches the fault through the binding (no world
  extension outside the gate), after publishing the cable's earlier stores.
  No live code writes the cable's +0x36 (the melee states are em_weapon's).
- em_weapon's light flag is a byte (D_00810D3C's width).

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

**Left (the gate stays).** The order a live R1 round meets them (the same
probe), with what each needs:

- **The muzzle node (reachable: every round; aim_03..aim_07, aim_10).**
  00187CC0 calls 001F4F40, which allocates a class-0xC record (001AFA90,
  now served) with +0x10 = 001F5040; 00187CC0 then writes its +0xB0, its
  +0xD0..+0x10F matrix (00102958 from D_00275B40[0] + 0x90), +0x100 and
  +0x10C. The live run stops there: the record's +0xD0..+0x10F has no
  owner. 001F5040 (em_area00_fx, translated and instruction-tested by
  tools/test_area00_fx_reference.py, not bound) is a model owner: it binds
  model 001C6120(*D_0028A56C, 0xD / 0xE / 0xF / 0xB by +0xD) through
  001CA5E0, takes +0x0C bone slots (001C6150, 001AF780, 001CB5B0,
  001C62C0), sets +0x60..+0x8C and +0x1F0 + 0x40..0x48, registers a point
  light (001D80E0 at +0x100), then per tick switches its clip (7 / 8),
  draws its eight fogged lines (001F4F90 -> 001CD940), places itself
  (001C63D0), publishes (001B17A0) and draws through +0x4C. Binding it
  needs the indicator children's kind of model-node storage
  (em_indicator_bind_live: +0x44, +0x4C, +0x110 slots, +0xD0) for a node
  of the common bank D_0028A56C, the export of those models and clips (the
  Roger export holds only the equipment's), 001D80E0 / 001C63D0 / 001CD940
  on the live context, and the node's draw.
- **The shell casing (reachable: every round).** The gun tick 00188630
  calls 001F4010 (em_equipment_live's worker faults today): it seeds one
  0x90-byte particle record D_007709C0[D_00275C40] from D_0025A350
  (001F2F90, 001F3340; em_area02_misc and em_area00_fx_debris translate
  them, unbound), which the barrel's sweep 001F40C0 then moves and draws
  (001F3620 / 001F3E30, translated in em_effect_manager and again in
  em_area00_fx_debris: one owner must be chosen; 001F3620 calls 001F02C0,
  the bounce sound, and 001CA3B0 / 001CA4D0). The census shows all of
  them running in aim_03..aim_07 and aim_10.
- **The impact effect's VU1 program.** 001EACF0's 001CFBE0 is kind 0, whose
  program table is the DMA packet at 0x230800 (001CFBE0's jump table): the
  chain page walks it and faults (0x230800 is not exported, and its VU1
  program is not one of the three the chain page translates, CHAIN_PAGE.md
  section 3). It needs the packet in the export, the program's
  translation, and GS evidence for what it draws. 001EBA20 (kind 1) uses
  the translated sprite program 0x231770.
- **Reload and its L3 path** (0017B300 / 0016F600, aim_06 / aim_07): the
  translations are oracle-tested; a live reload follows a fired round, so
  no fixture can exercise it until the round runs.
- **Melee 0x21 / 0x22 (reachable in AREA11:** aim_09, Circle / Square from
  idle enter +5 0x21 / 0x22, clips 0x10B / 0x10E; aim_11 cuts the cable).
  They stay em_weapon's, even behind the gate. The census of aim_09 lists
  what their bodies (001735C0 / 00173E60, translated in
  em_player_weapon_states_b) reach: the yaw steer 00173DD0, the knife probe
  0019B2C0 (em_player_equipment; it writes the cable's +0x36), 001AA840 /
  001AA7A0 (em_area00_fx_spawn; unbound), the trail effect 0x8000000D
  (001EFF10 -> 001F18C0 with 001F1550 / 001F15F0, em_area00_fx_trail;
  unbound) and 00102990 (em_area00_low); aim_11 adds the cable reaction
  (bound). None of these is bound for the player's melee yet.
- **The gate.** It can go once a round, a reload, the holster and both
  melee states run with no fault, and a fire side run (aim_03 from route
  08, like aim_r1_hold) equals the capture. Ordinary play then switches
  from em_weapon's stand-ins (and the camera stand-in camera_mode1_aim) to
  the original stances in one step.

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
