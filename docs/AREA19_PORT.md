# AREA19 in the shared composition (lane J1)

Lane J1, 2026-10-30, branch `area19`. AREA19 (area byte 0x13, overlay id 0x10,
subs 0 and 1) runs in the shared AREA01 composition, as AREA04, AREA22, AREA06,
AREA13, AREA15 and AREA03 do. The route's ground truth is TENTH_LEVEL_ROUTE.md,
TWELFTH_LEVEL_ROUTE.md, THIRTEENTH_LEVEL_ROUTE.md and FOURTEENTH_LEVEL_ROUTE.md.
This is a start-card binding: row-exact New Game chaining is a chain lane's, and
a beat that passes here ends on its recording's block and position, nothing more.

## Beats

`python3 tools/mid_route_beats.py --single <beat>` (recorded pad, recorded path,
the recording's rand() word for comparison only, end block and end position).
No row comparison has been made for AREA19 yet: nothing here is row-exact.

BEAT_TABLE

A card loads a saved progress block, so the creatures' rand() stream is not the
recording's. Most of the block differences above are that: health and infection
words, and D_00810ACC's first-bite flags, which follow them. They are not
behaviour defects until a row comparison says so.

## What is bound, and its evidence

- **State** (`em_area01_state`): the overlay row, the init 0x8250C0 (five stores,
  D_00275C20 = the first BSS record 0x82F880). `make test-area19-state-reference`
  runs the original 001E7780 and 0x8250C0 over the AREA19 captures.
- **Owners** (`em_area01_runtime_a19.inc`): `dispatch_area19`, each entry's frame
  from its first instruction; the translations are em_level10..14 and
  em_area19_route, with their oracles. Boot owners bind generally and run through
  `em_area01_runtime_later` (tools/gen_later_dispatch.py); an untranslated one
  faults at its address when it runs.
- **New translations**, each from byte-matched C with an original-instruction
  oracle: the worm 00153F10 / 00154040 / 00154120 and the drum 00156620
  (test_level9_port_reference); the hit family 0013B350, 0013B420, 0013B5B0,
  0013B790, 0013B9A0 (same oracle); 0021BED0 (test_level8_port_reference); the
  player's +4 2 / +5 9, 002236F0 (test_player_major2_reference).
- **The player's ledge, bar and ceiling** (`em_player_closure_ledge.inc`): 0016EF50,
  00169250, 0016AC50 and the surface 0017F9E0. They run in the beats; they have no
  oracle of their own yet, and neither has the stick classifier 001751A0.
- **Scripts** (`em_area01_script_live`): the window 0x82AD50 .. 0x82F790 and the
  op09 callbacks 0x826B30, 0x827540, 0x827B10, 0x827B20.
- **Boot data**: the AREA19 windows of `export_area00_runtime_tables.py`, checked
  against every AREA19 snapshot; `assets/boot_constants.embc`; the whole model
  library (`export_later_library.py`: AREA19's owners bind library models by
  number from their code).
- **Sound**: the area loops 0x415 .. 0x417, 0x44F, 0x52E. A B0 60 loop restarts
  whatever the accumulator was (they jump with one left over);
  test_area11_sfx_reference runs 0x44F in lockstep past its first loop.
- **Timeline**: every scene id 0022EC30's table knows runs over a blank track
  against the original (test_area01_timeline_reference).

## Shared code this changed (each from the original)

- `em_area01_live`: 001C9940, 0011CB90, 0011E080, 0011DF98, 001F6640, 001F66F0,
  001F0460, 001FC580, 001CD370, 001CF470; the clipper stage D_008112C0 as a window
  over em_shadow_live's one storage.
- `em_area01_actor_view`: a freed record's +0 reads the 0 001AFC10 stored, and its
  +0xA0 .. +0x1EF keep their last values (a stale tracker pointer reads them).
- `em_camera_live`: camera actions 12, 13 and 15.
- `em_effects_live`: the boot constants; 001F6BB0's key 0x1301.
- `em_aim_fire`: 001CA6E0 on a burning creature, D_0028A490 read-only.
- `em_chain_page`: capacity (prims 32768, units 512).
- EMET: the window limit is 192.
- `em_level9_port` hooks: 0019AB20, 0019AD00, 0011E620, 001B1D20, 001B2B10, 001B4CF0,
  001F0460, 001FC580, 0021C040; 001B0D80 returns its result.

## Open items

- **ERCPR and ERLENG of zero** (`em_vu1_page_programs.h`): a streak quad whose
  head has w exactly 0 stops the page: "VU1 translation fault ... (2)". It is real:
  the head (1056.25, 160, 920) sits on the camera plane under a fixed camera, and
  a19b_02 and a19c_01 reach it. The result is not established and is not invented
  here. A measurement on the fork did not start VU1 from a hand-placed program, so
  the question stays open. It needs a measured value.
- **Not drawn**: 001CB1F0, 001CB130, 001CB060, the tendril field's and the worms'
  draw methods, whose VU1 programs have no translation; reported once.
- **Not bound**: 0017FB90 (0017F9E0's handler 1); the script [18] callbacks
  0x826470, 0x826500, 0x826540; scene 27's ramp (faults at +0x74). Each faults
  when reached.
- **The player's +4 2 states** 0x0D, 0x0E, 0x15 (00224FE0) have no translation.
- **a19d_12 .. a19d_14**: their cards start in mid-air on a bar, so the player falls
  and dies. They need a chain from a19d_11.
- **a19b_00**, the arrival by the hatch [63], is the AREA13 side's.
