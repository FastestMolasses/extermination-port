# AREA11 aiming and firing (audit 1b item 14, census lane L28)

The player's armed stances (R1 0x1D, R2 0x1E and their 0x1F / 0x20
variants), their fire machines, the laser sight, the shots and the cable
reaction. Current state: the original workers are translated and
instruction-tested, and they are bound behind a **diagnostic gate**. Ordinary
play still runs em_weapon's stand-ins for the armed stances. The live R1 / R2
run stops at the aim camera (00197D20 / 00198650, the next chain step).

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
  the player is idle at tick 240. It writes no game state. It is not a
  capture comparison.

```
EM_STARTUP_TEST=newgame-control EM_AIM_FIRE_ORIGINAL=1 EM_AIM_FIRE_TEST=r1 build/extermination
EM_STARTUP_TEST=newgame-control EM_AIM_FIRE_ORIGINAL=1 EM_AIM_FIRE_TEST=r2 build/extermination
```

Both runs FAIL today, honestly: R1 enters +5 = 0x1D / +1F0 = 0x31 and R2
+5 = 0x1E / +1F0 = 0x32 on fixture tick 61, then the scene coordinator
faults at the camera's action handler (00197D20 for R1, 00198650 for R2;
CAMERA_LIVE.md section 5).

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
| 001839A0, 001B1510 | type classification, angle reduction | em_aim_fire_leaves | test-aim-fire-leaves-reference |
| 15 SDK vector / matrix leaves (001026A0 .. 00103230) | with the original read / store order and aliasing | em_aim_fire_sdk_memory (arithmetic from em_owner_services_original) | test-aim-fire-sdk-memory-reference |
| 00183C40, 001B41F0, 001EFE00, 001F4F40 | acquisition validity, the hit call, the cable effect allocation, the muzzle node | existing later-level translations em_area02_math, em_area00_world, em_area01_side, em_area00_fx, composed by em_aim_fire_world_live | their own oracles (tools/test_area02_math / area00_world / area01_side / area00_fx_reference.py, all PASS at this merge) plus test-aim-fire-world-live |
| 001CD520, 001E2BA0 | the sprite and the beam | em_player_equipment_sprite, em_area00_hud, composed by em_aim_fire_render_live | test-aim-fire-render-reference (composition against the original) |
| 0021AAC0, 0021A500, 001EFEB0, 001CE860 | the cable-hit effect nodes and their strip packets | em_security_gun_rest, em_area06_port_strip, composed by em_aim_fire_cable_live | test-aim-fire-cable-live; tools/test_area06_port_reference.py |
| 0018A6B0, 00188630 | the equipment nodes and the gun tick | em_player_equipment through em_equipment_live (live) | test-player-equipment-reference, the level smoke's check_effects |
| 00827490 | the AREA11 cable | em_security_gun (live) | SECURITY_GUN.md |

Tables: `tools/export_aim_fire_tables.py` reads the pinned ELF (SHA-256
checked) and writes the ignored `assets/aim_fire_tables.emaf` (EMAF v3: the
clip / sound / tint rows 0x248680..0x248D00, the reticle templates
0x2533D0..0x253490, the beam reference 0x253720..0x253740, the cable state
templates 0x266930..0x266AE0); `em_aim_fire_tables` loads it on first use
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
  D_00821400..BF (0021A500's strip points, its only user).
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

## 5. Verification (this merge, 2026-10-01)

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
| test-aim-fire-tables | 14 containers | the EMAF loader rejects damaged files |
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

## 7. What a live R1 / R2 run meets after the camera

Measured by stubbing 00197D20 / 00198650 in a private probe build (not
committed) and fixing each stop in the probe only. In order:

1. 001854E0 (the laser dot, from the gun's tick) writes gun +0x200..+0x20F
   (its sub-record +0x10); em_player_equipment's node has no field there.
2. 001854E0 uses the scratchpad 0x700038C0..0x700038DF and its sprite
   001CD520 uses 0x70003600..0x7000361F: the binding has no views for them.
3. The dot sprite's TEX0 0x20045BA5154222DC is not in the page texture export
   (`export_page_textures.py`), so the chain page fails at 001CB800.
4. 00185760's beam 001E2BA0 calls 00102870 (divide a vector by a scalar),
   which the binding does not route (an existing translation is
   em_area00_low_00102870).
5. The first round: the gun link (finding 1, fixed on main).
6. 001861C0 (the shot) uses scratch 0x700038F0 and the collision query word
   0x700031E8, outside the collision fields the world adapter exposes.

With 1..4 bypassed in the probe, the R1 stance reached +6 = 2 sixteen
ticks after entering 0x1D, as in the AIM capture aim_00 (f13 to f29), and
the Circle press entered 00170A60's 0x0A / 0x0B before stopping at 5.

Also open:
- 001DD170 (the reticle) reads vf23 before the frame's render head; the
  runtime refuses it until vf23's last writer is tracked (the render
  adapter's `vf23_valid`).
- The numeric reticle (kind 1) needs the small-font closure 001CBA50 /
  001CBC20 (AREA11's 00199220 passes kind 0).
- Raw records for the projectile callbacks 0018ABA0 / 0018AF50 / 0018B3E0
  and the muzzle node 001F5040.
- Melee 0x21 / 0x22 stay em_weapon's: their +18 view and the knife probe
  0019B2C0 (which writes the cable's +0x36) are not bound.
- The fixture is not compared with the captures yet. Next: compare the gated
  run with aim_00 / aim_01 / aim_03 (decomp CAPTURES_C10.md) once the camera
  handlers are live.

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
- **An original quirk (LAUNCHER_OPTIONS.md "Bug fixes"):** 0017B300's manual
  reload (mode 2, byte-matched) compares the total against 30 minus the
  magazine: with magazine 16 and total 17 it loads 30. Reproduced.
- **em_weapon** keeps its historical comments; where they disagree with
  this document (the shot range, the hit call's arity, the reload rule)
  this document is right.
