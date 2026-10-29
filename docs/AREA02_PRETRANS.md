# AREA02 pre-translation survey (a01r_03, a02_00 .. a02_05, a02_s0)

2026-10-29, revisit agent R1, branch `revisit`. Purpose: get ahead of the
frontier (F1 moves from a01r_02 into a01r_03 and AREA02) by listing every
original function the original runs on the fourth level's remaining route
(FOURTH_LEVEL_ROUTE.md: a01r_03 into AREA02, the AREA02 main line and its
side beat) and translating the ones the port lacks.

**Result: no translation is missing.** Every function the census saw has
one of the following:
- a port definition that names its address;
- a native boundary;
- a translation under another name, or as part of a split function.

No new translation files, Makefile entries or reference tests were needed.
None of the functions lacks readable C. What remains is **binding**:
AREA02 has no live composition yet (section 2).

## 1. Method

**Census.** A hidden, owned fork census, run under the run lock:

    route_census.py run --segments a01r_03_door16,a02 --pass A02F

The outputs are addresses and frames only, under the decomp's
`build/fork_refs/s87/census/runs/A02F/`; the log is
`build/fork_refs/probes_1028_a01r_survey/a02_census.log`. The census
arms one-shot entry breakpoints on the census candidates: the boot ELF
plus the AREA01 overlay for a01r_03, and the boot ELF plus the AREA02
overlay for the a02 beats (runtime address = splat label + 0x40). It
re-arms them for every beat. Every beat completed:

| Beat | Frames | Functions | Rows equal to the recording |
|---|---|---|---|
| a01r_03_door16 | 941 | 742 | 731 / 941 |
| a02_00_duct | 1422 | 691 | 1423 / 1423 |
| a02_01_switch | 629 | 849 | 630 / 630 |
| a02_02_ladder_escape | 1013 | 606 | 3 / 1014 |
| a02_03_over_wreck | 830 | 582 | 831 / 831 |
| a02_04_panel | 1576 | 707 | 1577 / 1577 |
| a02_05_progression_exit | 1725 | 807 | 1726 / 1726 |
| a02_s0_mts_bed | 1007 | 595 | 1008 / 1008 |

Two replays left their recorded rows early: a01r_03 at row 731, and
a02_02 from its first rows. Both still finished their beats, so their
function sets are what the original ran on the replayed paths. The
legacy census pass A02 (`a02_delta.json`) lists the same AREA02-first
functions.

**Union: 1,331 functions.** In the a02 beats, AREA02 overlay hits are
keyed to the function that contains them. The a01r_03 hits in AREA01
code are the ones AREA01_REVISIT.md's survey covers. Four hits at the
end of a02_05 fall in the next area's overlay (id 5); they belong to
the fifth level's census and are not counted here.

**Port coverage.** A function counts as translated when a function
definition names its address, using level_readiness.py's
source_definitions and its ALIASES:
- 861 are named that way.
- 341 are native boundaries, by the census classification.
- 116 are translated under names without the address: the classified
  module exists in `src/game`.
- 13 were checked by hand:
  - 0016D130: a player state function.
  - 001790B0: in the em_player_closure_0e_18 group.
  - 001A58B0: em_actor_collision's column_round.
  - 001B62C0: em_item_stick_sample (AREA02_MISC.md).
  - 001B7A30: em_area_script op 0x0F.
  - 001C0004 / 001C2FF0: the false-split tails of 001BFFD0 / 001C2770.
  - 001CD070: em_area01_render_001CD070.
  - 002082B0 / 00208AD0: the status pages' arc and health gauge.
  - 00209860: em_status_ammo_draw (AREA02_UI.md).
  - 0020AC70: em_item_trail's analog trail.
  - AREA01 0x8240E0, a02_05's return into AREA01:
    em_area00_return_008240E0.

## 2. The AREA02 host (built on revisit, 2026-10-28)

AREA02 runs in the shared AREA01 composition (one runtime, one live
composition, area index 2), not in a separate em_area02_runtime /
em_area02_live: the pool, the actor views, the boxes owners, the player,
the camera and the scripts are the same storages AREA01 and AREA00 use.

- **Runtime** (`em_area01_runtime`, area 2; 51ab8e5). The 15 route-reached
  overlay entries (0x823580, 0x823900, 0x823930, 0x823980, 0x823D70,
  0x824020, 0x8242F0, 0x824800, 0x824910, 0x824AC0, 0x824C40, 0x824CD0,
  0x824D50, 0x824FA0, 0x825100) dispatch to em_area02_overlay's
  translations through `em_area02_runtime_overlay.inc` (each hook paired
  with the runtime by address; frames as the original's). 0x825520 is not
  wired (reached only through 0x825100's D_0081083F == 2). Every other
  callee goes to the host worker; an unbound one is the fail-stop
  (`em_area01: unbound worker ...`).
- **Area state** (`em_area02_state`, 9d69286): the overlay header check and
  001E7780's 0x823900 init for area 2.
- **Boot routines** (`em_area02_math`, dispatched for area 2; 51ca93d,
  6d212fb, 01646c6): the 23 node behaviours and helpers, among them the
  panels 00158EC0 / 00159210 and their use reaction 00157860.
- **Draw method 001CAF60** (01646c6): the 001C4AF0 owners' +0x4C goes to
  the draw host (em_area01_model_draw -> em_owner_draw_live_001CAF60:
  em_area02_math's 001CAF60 over the render context, its callees the
  owner-draw module's). Its unit is GS class 1 (001D1F80(3, 2, 1),
  D_00815990, ALPHA 0x44; skin records D_00816D40 / D_00816E40), drawn on
  the chain page.
- **Boxes owners**: em_area11_boxes_owner_001CA5E0 (001C4AF0's model
  switch on the canonical +0x44 / +0x4C; 6d212fb).
- **Use scan** (029fc00): 00183EF0's selector 2 and 5 paths and the 0x2C
  panel's line of sight; the use descriptors D_00275458 and D_00275478 /
  D_00275480 joined export_effect_tables.py's descriptor windows (the
  separate use_descriptors.emrg of 029fc00 was folded in).
- **Scene and assets** (c9b7037, 5804f88; the area read is F2's lane from
  2026-10-28): the arrival scene sub 0 / 1, the module loader areas, the
  start point `EM_START_AT=area02` (sub 1 arrival). Sub 1 has no message
  bank of its own: sub 0's emmd serves the area.

- **00199C50** (9135d25): at its original caller (001AE040 state 0, after
  001B6990) the collision view stages its scratch metadata; AREA11 keeps
  the report (no reader).
- **Sub 0's data** (22e1722, 319d4eb; found by the discovery runs below):
  the library models 0x57 (a pickup) and 0x7D (001C4960's,
  export_area02_library.py), the crate registry roots D_0024A854 /
  D_0024D828, 0022DCD0's light list D_00269680, 0019C6F0's room-list root
  D_0024D7C8, the use descriptors as one window D_00275458..D_0027548F,
  the static world run bounded by the packet arena, and the live worker's
  00121A28 (memset; 51eec5b).

Assets to export (port root, from the user's disc and ELF):
`export_module_loader.py`, `export_area02_level.py --sub 1` (and 0),
`export_area02_sfx.py`, `export_area02_tables.py`,
`export_area02_world_textures.py`, `export_area02_shadow_receivers.py`,
`export_sfx_registry.py`, `export_effect_tables.py` (then
`export_area01_water_effects.py` and `export_area00_runtime_tables.py`, as
STARTUP.md step 50 says), `export_roger_banks.py` then
`export_area00_library.py` then `export_area02_library.py`, then
`build_start_saves.py` (start points `area02` and `a02_01` .. `a02_05`:
each a loaded game placed at that route_a02 beat's start, for discovery).

Reference tests: test_area02_runtime_reference.py (the 15 entries through
the runtime over the a02 captures), test_area02_state_reference.py,
test_area02_math_reference.py, test_object_unit_reference.py section K1
(the 001CAF60 units through the original microcode),
test_area01_interaction_live_reference.py (the AREA02 forms).

Runs (headless, 2026-10-28): `EM_START_AT=area02` idles 300 s in sub 1
without a fault. With a02_00_duct's recorded pad and path it runs 900+
frames without a fault, but the duct entry at row 225..228 (the player's
action 0x2C at the snapped point (90.4, 0.047, -175.0)) does not happen
natively: the player stays at about (90.1, 0.05, -174.5). That is the
first behaviour divergence, not yet located (row comparison is F2's lane).

Discovery ahead (2026-10-29, `EM_START_AT=a02_0N` with the beat's
recorded pad and path; not row-exact, positions drift from the recording):
a02_01 and a02_02 run to the pad's end without a fault after the sub-0
data above; a02_03 ran 600+ of its 830 frames in a 2,400 s limit without a
fault. That 0.25 frames/s is the pad script's clock, not the engine's
(P1 lane, 2026-10-29, PERFORMANCE.md P18): the engine ran about 58 frames/s
headless there (81 since P18). The native player follows the recorded
path to about its row 130, (193.4, 4.9), where the recording stands still
until row ~300; from engine frame ~4,100 it stands at (196.3, 0.06, 0.9),
4.2 units off the path, and does not move again. EM_START_AT_PATH's clock
holds each pad frame up to 300 engine frames while the player is more than
2.5 units from the recorded position, so the script then advances one pad
frame per ~300 engine frames. The divergence itself is not yet located. a02_s0 (sub 1, from `area02`)
fail-stops at 00184BA0 on the owner 0015AFA0 (node 0x7A5640) near pad
frame 300+: the Use scan itself or its winner claim refused (not yet
diagnosed). a02_04 and a02_05 have not been run.

001C7EB0 / 001C8140: em_area02_math's are the one translation of each (the
duplicate em_area01_revisit_spawn and its test were removed; the runtime runs
em_area02_math's for area 1's placement spawn and for area 2).
