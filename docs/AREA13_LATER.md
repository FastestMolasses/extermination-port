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

| Beat | Card | Status (final tree unless noted) | Rows (position, heading, action) |
|---|---|---|---|
| a13b_00 (the ladder up, AREA13 entry 6) | chain `a13b_arrival`, card `a19_02` | **passes** (136 s; a loaded run timed out at 200 s) | not compared |
| a13b_01 (ledge climb, door [17]) | `a13b_01` | **passes** | door transit 337..548: 212 of 212 |
| a13b_02 (button [15]) | `a13b_02` | **passes** | not compared |
| a13b_03 (door [8]) | `a13b_03` | **passes** | not compared |
| a13b_04 (the lift's button [12]) | `a13b_04` | **passes**; bites vary by run (chain `a13b`: 4.04 from the end, one infection word) | not compared |
| a13b_05 (the car, the lift, AREA04 entry 7) | chain `a13b_lift` | **fails on the final steering**: the walk stops short of the inner button (no fault). An earlier steering of this tree took the lift to AREA04 entry 7 with the end position in range and only the health and infection bytes different. Its own card has the lift closed | not compared |
| a13b_s0 (roof ladder, side) | `a13c_00` | ends on the recorded block, 9.70 from the recorded end (a railing corner); passed once on an earlier steering | not compared |
| a13c_00 (door [17], recharger [60]) | `a13c_00` | **passes** (charge 12) | script, prompt, charge 507..800: 294 of 294 |
| a13c_01 (roof ladder, walkway) | `a13c_01` | **passes** (with a path edit, below) | not compared |
| a13c_02 ([44], the battery page) | `a13c_02` | **passes** | 6..1176: 1,170 of 1,171, 1,170 consecutive |
| a13c_03 (the blast, [45]) | `a13c_03` | **passes** | scene 464..1283: 817 of 820, 817 consecutive; the player stands still for most of it |
| a13c_04 (g[3], the cure) | `a13c_04` | block equal (health 100, infection 0), 5.21 from the recorded end: the hit's slide comes at another time. **Fails the 4-unit rule** | not compared |
| a13c_05 (the boom, [7]'s scene) | `a13c_05` | **passes** (path edit, EM_START_AT_FINE=2) | 1140..1813: 0 identical, the player stands 0.3 from the recorded trigger spot; [7]'s script ran to its end |
| a13c_06 (the south region) | `a13c_06` | **passes** | not compared |
| a13d_00 .. a13d_05 | their cards | **pass** | not compared (steered) |
| a13d_06 (door [20]) | `a13d_06` | **passes** | 331..542: 212 of 212 |
| a13d_07 (hatch [63]) | `a13d_07` | **passes** | 114..543: 424 of 430, 424 consecutive |
| a19b_00 (the ladder, AREA19 entry 10) | chain `a13d_hatch` | **passes** | not compared |

Whole groups in one run: `a13d` from a13d_00: all eight pass. `a13c` from a13c_00: 00..02 pass, a13c_03 differs by one health byte, then the walk leaves the route at a13c_04 (no fault). `a13b` from a13b_01: 01..03 pass.

Identical rows prove less where the player stands in a script: only position,
heading and action are compared.

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

None in the game path. EM_START_AT_FINE and mid_route_beats' PATH_EDIT are test
steering, comparison only: PATH_EDIT moves the steered target at two presses
(a13c_01's door [17], whose recorded spot is at the edge of the Use's reach, and
a13c_05's corner on [44]'s platform); no pad byte changes.

## Fail-stops that remain

These callees of the beasts have no live binding, so they fault at their address:
00146F80 (00144040 state 4's ground probe), 0021BED0, 0021BF90, 0021C040 (the grab
that holds the player, reaction 0x3C). No recorded AREA13 beat shows reaction 0x3C
or 0x3D (the rows hold no such action), so the route does not need them, but free
play near a beast can reach them.

## Open items

- **a13b_05** and **a13b_s0**: steering misses (above). No fault.
- **a13c_04**: the end position, 5.21.
- **Row-exact**: nothing here runs from New Game. It waits for a chain lane; AREA19's
  host is on main, so the route can now cross a19_00 .. a19_02 into a13b_00.
- **The menu player in the coat** has no original frame to compare.
- **Fail-stops**: the four callees above.
- A tick-logged run and a plain run of a13c_01 once went different ways; the plain
  run that missed door [17] later hit "exponent-255 word" in a page program after
  thousands of frames in the east room, seen twice, not traced (EM_VU1P_FAULT_OPERAND,
  chain page). The game task stops there and mid_route_beats reports a timeout.
