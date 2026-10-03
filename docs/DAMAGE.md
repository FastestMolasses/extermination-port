# Damage, death and Continue in AREA11 (audit 1b item 13)

Chain step DAMAGE (track T1, 2026-10-02). Everything that hurts the player
in the first level, the death, the game-over screen and what the title
does after it, on the original code, checked against the decomp capture
lane DAMAGE (decomp docs/CAPTURES_C10.md "DAMAGE"; recordings
`../Extermination/build/c10/damage/dmg_00..dmg_08`). Addresses are the
original's; no instruction of the original is reproduced here.

## 1. State

| Path | Original | Port | Checked by |
|---|---|---|---|
| The flame's contact | 001A8BE0 / 001A8660 (class 0xD, kind 0), callback 0x823580 | live: the knock-back table D_0024A740 exported, 0x823580's 001EFE00 bound (section 2) | test-area11-effect-reference; dmg_flame (20 hits as recorded) |
| The stage's hit | 0021C440 / 0021C350 / 0021D800 (flinch), 0021D1A0, 0021D600, 0021BC40, 0017C370, the rumble 001B61C0 | live (the stage's own translations; em_player_reaction, em_player_stage_workers, em_pad_actuator) | their oracles; dmg_flame, dmg_crevice_fall |
| The burn node 0x80000027 | 001EFE00 -> 001EF9D0, record 0x27: 0022BBC0 subtype 9, its light 001D8100 and sound 0x14A | live (section 3) | test-area01-ui-reference, test-effect-original-reference; dmg_flame (spawned and freed on the recorded ticks) |
| Low health | 0015D000's heartbeat (+0x235 bit 0 at <= 35, the small motor) | live | dmg_flame (12 heartbeats) |
| The death | 0021D800's death reaction, 0021D2E0 (the death terminal), 0021D250 (the 0x5D floor), 001F77B0 (the decal) | live (section 4) | test-player-fall-reference, test-effect-001F77B0-reference; dmg_flame, dmg_pit_fall |
| The landing hit | 0017C580 / 00163E90 | live | test-player-fall-reference; dmg_crevice_fall |
| Game over | 001AD140, 001AD4E0 (screen module 0x27), 001ABF90, 001D2880, 001ADF00 | live (section 5) | test-scene-task-reference, test-render-context-live-reference (001D2880); dmg_flame, dmg_pit_fall |
| Continue | 001ADF00's 001AB790(001AC070); 001AC070 state 0 and 001AC480 after a death; New Game (state 4) | live (section 6) | test-title-menu-reference (both D_00275BDC contexts); dmg_flame |
| Infection, 0x80000023 / 001ED450, heavy landing, kill plane | | unreachable in AREA11: fail-stop kept (section 7) | the recordings |
| The title's load screen (dmg_05) | 00225A00 .. 00226070 | not bound: em_frontend leaves EM_STARTUP_LOAD_GAME pending | the OPTIONS step |
| The fan's hit (dmg_08) | 0x827630's fast arm, 0021E9C0 | not played: the fan needs Roger's departure | the EXIT step |

## 2. The flame's contact

The flame 0x8235F0 publishes itself on the class-0xD list and its
behaviour +0x34 is 0x823580 (chain step A11FIX). 001A8BE0's contact pass
runs 001A8660 between two player stages, in the close-out. The pieces that
used to fault:

- **The player record's close-out view.** After 0015BCF0's tail the
  original record holds the position at +0xA0 and the bone-1 hip at +0xB0;
  the port's record keeps the in-stage view (+0xB0 = position). The
  close-out passes see +0xA0..+0xBF as g.pos and the pose host's published
  hip, each with w = 1.0 (em_area11_bindings.c player_closeout; a write or a
  straddling access faults). 001A8660's distance and height tests read
  +0xA0: before this the contact was never in reach (distance about 530).
- **Vitals, one storage.** +0x220 health, +0x224 pending damage, +0x228
  infection and +0x22C pending infection are g.status.health, g.pd_pend_hp,
  g.status.infection and g.pd_pend_inf (player_vitals; the stage's view is
  loaded before 0015BA50 and stored after 0015BCF0's tail). 001A8660's
  store of +0x224 reaches the next stage's 0021C440.
- **+0x34 / +0x36.** The actor record keeps +0x36 as its own halfword; the
  flame's +0x34 word is composed from it (em_area11_effect_runtime
  record_load / record_store), so the stage's halfword store no longer
  clobbers the callback word.
- **The knock-back table D_0024A740** (0x440 bytes, indexed by the hazard's
  kind +0x0D): `assets/collision_knockback.emrg`, exported by
  `tools/export_collision_contact.py` beside the contact table.
- **0x823580's 001EFE00(0x80000027, player)** runs
  em_area01_side_001EFE00 through the aim / fire composition, as the gun
  cable's 001EFE00(0x80000045) does; for the call the player record's +0xA0
  / +0xB0 hold the close-out view.

Then the stage takes the hit: 0021C440 moves the pending damage, 0021C350
and the flinch 0021D800 (its clip pick through rand(), tools/rand_order.py
`em_player_reaction_0021D800`) with 0021D1A0 / 0021D600, and the pad
actuator 001B61C0 (em_pad_actuator) requests the recorded small motor. The
protection +0x20E counts after the flinch.

## 3. The burn node 0x80000027 (0022BBC0)

001EFE00's 001EF9D0 allocates the node on em_effects_live (record 0x27:
callback 0022BBC0, subtype 9). 001EF940 plays its sound 0x14A through
001FBF50 / 001FB9F0 (em_effects_live w_001FBF50 / w_001FB9F0: em_sfx's
gains with f13 = 4096.0, any other volume faults) and 001D8100 registers
its kind-2 light (em_effect_original).

The behaviour is em_area01_ui_0022BBC0 (with 0022B700, 0022B7A0, 0022BB70;
AREA01_UI.md), run by em_aim_fire_runtime's other_tick through the
composition with D_00275B40 = the node's +0x110 (em_bone_burst.c). In
AREA11 the node's one timeline is 0x267940, which sets burst kind 0 only;
the burst's 001CFBE0 source blocks are 0x268480 / 0x268510 (their TEX0 in
`scene_snow/page_textures.emot`), and the timeline tables and blocks
0x267310..0x268B3F are a window of `effect_tables.emet`. 001CD070 and
001F0190 / 001F0290 run in the composition (em_aim_fire_world_live);
001CD070's float_to_int store D_00275C04 is em_effects_live's one copy.

**The slots.** 0022B700(seq, 5) pops five slots from the one bone-slot
stack (001AF780, em_area11_boxes) into +0x110..+0x123, and 0022BB70 uses
them as a 65-entry ring. The stack only matches the original's because
0015C420's 21 pops now run at the player's spawn (em_area11_bindings.c:
the record's +0x110 words, fault on a mismatch): before them the node's
first pops aliased the player's own node records and the flinch's next
animation advance failed (audit 1b item 10). Chain step AIMCAP made the same
fix on main (pop_player_node_slots, with 0015C420's +0x58 / +0x5C words);
the merge keeps that one copy of the pops.

## 4. The death decal (001F77B0)

0021D2E0's skeleton step calls 001EFD90(0x80000043, the point, the
record's +0xB0); record 0x43 (class 0xC, subtype 2) is 001F77B0, a ring
of floor quads through the decal kernel 001CE300. Translated from the
instructions in `em_effect_001F77B0.c` (state 0's parameters by subtype,
the particles' rand / sinf / cosf seeding into 0x70003A20 / 0x70003A24,
state 1's quads and their growth 1 / +0xB8 per tick capped at 1.0, states
2 / 3 free). Its D_00810360 is the player record's +0xB0, the hip after
0015BCF0's tail (the pose host's bone 1). The quads draw through
em_shadow_live's 001CE300 (em_shadow_live_effect_001CE300, TEX0
0x2004108555322080 colour 0x80020220).

`make test-effect-001F77B0-reference` runs the original 001F77B0 (and the
SDK sinf / cosf in place) against the translation for every subtype and
state, and runs the dmg_02 end snapshot's live decal (record 0x007B0970,
sizes 186 / 210) in lockstep until the cap.

## 5. Game over

0x1AE040 state 1 calls 001AD140 when D_0028A9A0 is 2 (+8 = 3, +9 = 2);
001AD250 runs the byte-matched 001AD4E0 core (em_scene_task), then +9 = 4
and 001ADF00. The core owns the timing (the 0xF0 hold, the Cross skip, the
fades). Its workers (em_scene_bindings.c "game over"):

- **001FF080(0, 0x27)**: the loader task 001FF0D0 loads screen module 0x27
  (module loader pack `modules.emml`, `tools/export_module_loader.py`
  SCREEN_MODULES); its 0x63 step clears D_00275BD8. The chunk is the
  screen's one GS upload (em_status_runtime loader_chain counts it). At
  host speed the load takes 10 ticks; the disc took 22..23
  (LAUNCHER_OPTIONS.md "PS2 disc-drive timing").
- **001ABF90(TEX0 words)**: em_render_001ABF90 draws the four 256 x 256
  sprites of `assets/startup/game_over.emui`, which
  `tools/export_game_over.py` composes from module 0x27's upload with the
  four TEX0 words 001AD4E0 pushes (the decomp's export_startup.py
  composer; `--capture` checks every texel and palette entry against the
  dmg_02 end snapshot's GS memory). Any other packet, or the screen before
  the chunk arrived, faults.
- 001AEE10 / 001AEDE0 (em_fade), 001D2880 (em_rcl_001D2880 on the render
  context; test-render-context-live-reference executes the original),
  001FA790(0, 0x1B) / 001FAB50 (em_stream_live).

The legacy game-over and Continue overlays (em_hud_game_over,
em_hud_continue, the GO_SCREEN / GO_PROMPT states) are deleted.

## 6. Continue: the title after a death

001ADF00's 001AEBA0(0xFF) is em_screen_fade_in, its D_00275BDC = 1 the
scene state's, and its 001AB790(0x1AC070) installs the title flow again
(em_frontend_install_001AC070: em_startup_reinstall_001AC070 on the same
startup machine, the resources already resident). 001AC070 state 0 reads
D_00275BDC: after a death it goes to state 2 (attract mode 3) instead of
the movie, and 001AC480's sub 0 starts the cursor on the second entry. The
menu's three entries are New Game, Load and Options; none continues the
level. New Game (state 4) is em_game_reinstall_new_001AC070: the pending
damage cleared, the opening request, D_00275BE0 = 0, and the game task
001ACEC0 replacing the title task; the New Game then resets the progress
and plays the opening to first control as the boot New Game does.

`make test-title-menu-reference` executes 001AC070 state 0 for both
D_00275BDC values and 001AC480 in both contexts against em_startup.

## 7. What AREA11 cannot reach (fail-stop kept)

- **Infection.** +0x22C is raised only by sphere kinds 3 / 4 (the flame is
  kind 0) and class-3 pads (none in AREA11); the area drain needs
  D_008106C8 & 0x60, clear in AREA11. In every frame of the nine DAMAGE
  recordings +0x228 and +0x234 are 0.
- **The blast reaction (0x80000023, effect handler 001ED450).** 0021EAD0 /
  0021EF30 are entered only from the hit requests +0x0F = 7, 0xA or 0xB.
  AREA11's writers of +0x0F are the flame (0xC) and the fan (6, then
  0x86); the security gun is dormant on the first visit. The recordings
  hold no other value.
- **The heavy landing** (0017C580, drop below -104): only from the two
  tower tops; the capture lane could not produce it (no running jump at
  the edges). **The kill plane** (y < -200, 0015D460): state 1 only, and
  the 0x5D floor kills first.
- **The load screen** (dmg_05, 00225A00 / 00225AC0 / 00225720 / 00225A20 /
  00225CF0 / 00225D20 / 00226070, 001FCBD0, 001FE8D0 / 001FE9A0 /
  001FECB0, 00114848 / 00114930 / 00114988): the title's Load entry. The
  port's frontend leaves EM_STARTUP_LOAD_GAME pending (no outcome is
  invented); it belongs to the OPTIONS step.
- **The fan's hit** (dmg_08: 0x827630's fast arm, 0021E9C0, the camera's
  00194D10 / 0022FCA0 / 00230000): the fans spin fast only after Roger's
  departure, which the EXIT step binds.

## 8. The side runs

`make test-level-smoke-damage` (part of `make test-level-smoke-side`) plays
three side runs (src/game/em_level_smoke_test.c "damage"; the capture
lane's own closed-loop policies) and checks each with
`tools/test_level_smoke.py` and, window by window, with
`tools/level_smoke_damage.py` against the recordings (every window aligned
on its event, then compared tick by tick):

- **dmg_flame** (from crevice_prompt; dmg_00..dmg_04): 20 hits as recorded
  (the knock-back's per-tick step within 0.0082), 12 heartbeats, the death
  and its decal to the load request (320 ticks, exact), screen module 0x27
  loaded in 10 ticks (the disc 23), 74 game-over ticks after it (exact),
  001AC070 65 ticks after the hold (equal), the menu taking input after 67
  (the disc 104: the title module resident at host speed), the game task 65
  ticks after the confirm (equal), first control 1382 ticks later (the boot
  New Game: 1382) at (250.8, 229.9, 209.0), yaw 0.61087, health 100.
- **dmg_crevice_fall** (from crevice_prompt; dmg_06): the walking jump
  short of the north block, the landing hit and 172 ticks after it as
  recorded.
- **dmg_pit_fall** (from truck_preview; dmg_07): the 0x5D floor death and
  the fall to the load request (188 ticks, exact, with the height path),
  the game over as above.

The tick log's `dmg` field carries the damage fields and the burn / decal
nodes, `pad_pre` the pad block before the frame (em_scene_bindings.c).
The run checks after a death stop at the second New Game where they
compare the main line (tools/test_level_smoke.py `second_game`).

Census: the 50 functions the DAMAGE recordings added are rows since
FIRST_LEVEL_CENSUS.md section 1.59 (35 live, 2 verified-unbound, 9 missing:
the load screen, 4 boundary), with their liveness measured over these
three runs. Five of them (001EFE00, an AIM row, and 00194D10, 0022FCA0,
00230000, 001195A8, beat-15 rows) were rows from the AIMCAP and EXIT steps
too; the merge keeps one row each. The camera tether 00194D10 / 0022FCA0 /
00230000 is exercised by the level smoke's exit phase.

## 9. Re-exports

    python3 tools/export_collision_contact.py   # + collision_knockback.emrg
    python3 tools/export_effect_tables.py       # + the 0x267310 block
    python3 tools/export_page_textures.py       # + 0x268480 / 0x268510's TEX0
    python3 tools/export_module_loader.py       # + screen module 0x27
    python3 tools/export_game_over.py           # startup/game_over.emui
