# AREA13's later story in the shared composition (lane M1)

Lane M1, 2026-10-31, branch `area13x`. The beats after the player comes back to
AREA13: groups a13b, a13c and a13d (TENTH, ELEVENTH and TWELFTH_LEVEL_ROUTE.md),
played from start cards. The first pass, a13_00 .. a13_05, is LEVEL3_BINDING.md
"lane H1". This is a start-card binding: row-exact New Game chaining is a chain
lane's, and no walk here is row-exact.

## How to run

`python3 tools/mid_route_beats.py --single <beat>`, or a chain by name (a13b_lift,
a13b_arrival, a13c, a13d, a13d_hatch). `python3 tools/test_area13_rows.py` compares
the scripted stretches. The start points are in README's table and STARTUP.md.
These runs set EM_START_AT_FINE=1 (STARTUP.md): the default steering stands 0.6
from a recorded press, too far for AREA13's ladders and door edges; other lanes'
beats keep the default, so their results do not move.

## Beats

@@TABLE@@

## What runs where

- **The battery machine [44]** (0x823E90, class 4, model 0x25) is the battery
  page's owner: `em_scene_bindings_panel_owner_read` takes it in area 0x0D beside
  00158EC0 and 00159210. a13c_02's rows through its scripts and the page match
  (see the table); charge 12 -> 4 and flag 0x1C are in the end block.
- **The recharger [60]**: 001575B0 (script 0x247420's op09, byte-matched: cue
  0x3EF, returns 1) in the live worker. Its page's recharge path is the shared
  battery page; a13c_00 ends with the recorded charge.
- **The player's face in the coat**: 001B81D0 picks D_0028A490 row 0x19 while
  +0x2FF is 0x3E; `tools/export_coat_face.py` adds extract/chunk03/f17_id19.bin
  to the retained resources (RAM equals the file in the fork capture).
- **The menu player in the coat**: 0020E6F0 binds D_0028A588 with D_00810C60 = 1;
  `tools/export_status_coat.py`, em_status_models load_coat (optional: without the
  files such a page faults at 001CA6E0). **Card-only**: no fork capture shows the
  status hub in the coat, so its texels are the outdoor snapshot's GS memory and
  were not compared with an original status frame.
- **The blast**: [45]'s sequence and the thrown pieces ran through
  em_level11_port's existing entries. New around them: 001E4610 and the boot
  owners bind in area 0x0D as in AREA15; 001CDDC0's stage and 001CD370 / 001CF470
  are lane J1's (em_area01_live); 00102850 (em_rvr_00102850); 001F8D30
  (em_shadow_live_owner_001F8D30, 001F9140's call); the contact 0x8284E0 through
  001A8660; music cue 0x12 ([44] step 3, tools/export_streams.py).
- **Knocked drums** (00156620): the query reads the tick's own +0x00 store (the
  guard that refused it is gone), and with +0x00 bit 0 and a nonzero class 0019AD00
  takes its 001A7280 branch against the player's hull
  (em_aim_fire_world_live_hulls).
- **The 0x141D20 beasts** (group 0x82A230): behaviours 3..8 and the death states
  are em_level11_port_creature_b.c, ten functions with their oracle
  (test_level11_port_reference: 546 cases default, 2,040 with EM_TEST_FULL=1,
  every case identical). 0011CB90 and 0011E080 serve the family in the live
  worker. Boot constants serve area 0x0D (export_boot_constants takes route_a13b,
  a13c, a13d).
- **The sentries 0x824BB0**: the scratch window reaches 0x7000391F (its hit path's
  0x70003910). This is the attacker the eleventh level could not name (see the
  report's CURIOSITIES candidate): a13d_06's and a13c_04's runs fire from it.

## Stand-ins

None in the game path. EM_START_AT_FINE is test steering, comparison only.

## Fail-stops that remain

These callees of the beasts have no live binding, so they fault at their address:
00146F80 (00144040 state 4's ground probe), 0021BED0, 0021BF90, 0021C040 (the grab
that holds the player, reaction 0x3C). No recorded AREA13 beat shows reaction 0x3C
or 0x3D (the rows hold no such action), so the route does not need them, but free
play near a beast can reach them.

## Open items

@@OPEN@@
