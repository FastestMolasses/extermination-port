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

| Beat | Status alone from its card |
|---|---|
| a19_00, a19_01, a19_02 | ends on the recording's block and position |
| a19b_01 | stops: ERCPR of zero, frame ~800 (open item 1) |
| a19b_02 | no end in 240 s: bugs bite at frame ~204 where the recording climbs the ladder (the creatures' rand() stream) |
| a19c_00 | position right; D_00810ACE, a first bite the recording took a beat later (rand() stream) |
| a19c_01 | stops: ERCPR of zero, frame ~500 (open item 1) |
| a19c_02, a19c_04, a19c_06, a19c_07 | ends on the recording's block and position |
| a19c_03 | 10.68 off the end: on the slide the recording's yaw turns to -0.52 from frame 968, the port's stays 0 until 1008. Not resolved: the path steering replaces the stick there; the slide code has its oracle |
| a19c_05 | walks off the y-170 step 1.3 from the recorded path and falls to y 150: steering |
| a19d_00, 02, 05, 06, 08, 09, 10, 16, 17, 18, 19, 20 | ends on the recording's block and position; a19d_20 through door [50] into AREA15 |
| a19d_01 | no end in 240 s alone: earlier bites move the player off the ladder at (892, 929.5). In the chain it takes the ladder and reaches the ceiling |
| a19d_03 | position right; infection and ammunition words differ (the fight's stream) |
| a19d_04 | runs to its end, 35.20 off, after bites the recording did not take; 00130AB0's grab now runs |
| a19d_07 | card artefact: the recording starts inside the status page and uses the health item there; the card does not |
| a19d_11 | card artefact: the area script at 0x82E050 fires again at the arrival (3B8D 2 for the whole beat) |
| a19d_12, 13, 14 | cards start in mid-air on a bar; they need the chain, and the chain does not reach them |
| a19d_15 | the latch runs (kind 0, 0021FB40 then 0021FED0); then stops: ERCPR of zero, frame ~500 (open item 1) |

19 of 34 beats pass alone. No beat stops at an unbound owner any more; the three
stops left are all open item 1. Chains on the final tree: a19 passes a19_00 and
a19_01, then a19_02's infection byte differs; a19b stops at open item 1 in
a19b_01; a19c stops at open item 1 in a19c_01; a19d passes a19d_00, takes the
ladder and the ceiling, and the player dies there at a19d_01..02 (bites the
recording did not take), so it never reaches a19d_11. a13_05 ends in AREA19, and
a15_00, a15b_00 .. a15b_04 run through AREA15's doors into it (all pass).

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
- **The player's ledge, bar and ceiling** (`em_player_closure_ledge.inc`): 0016EF50
  and 0017F9E0 (test_level12_port_reference), 00169250 (test_area06_port_reference),
  0016AC50 (test_level14_port_reference), the stick classifier 001751A0 (now
  test_level12_port_reference). The closure's adapters to them have no oracle.
- **The latch**: 0021BF90, 0021FB40, 0021FED0, 002208C0, 00220B50, 00224FE0, 0017FB90
  and kinds 2, 3, 4 (test_level12_port_reference, every reachable word of each).
  0015B770 runs them by +0xD:

  | +0xD (hit +3) | +5 0xD | +5 0xE | what the code does |
  |---|---|---|---|
  | 2 (7) | 00220D30 | 00221060 | 0xD: clip 0x3C then 0x3D, moved by the root node through the matrix at +0xD0, swept against the wall. 0xE: 0x26, 0x27 at the landing, then the deaths or the recovery |
  | 3 (6) | 00221630 | 002217C0 | 0xD: 0x39 then 0x3A, height from the root's +4, swept against the wall. 0xE: 0x3B, 0x1C5 while +0x314 & 0xE0, then 0x1C6 or the deaths |
  | 4 (5) | 00221C70 | none | 0x33, 0x34; 001754E0(8) or the gate frees with 0x38 and D_008106BD; a hit in +0x224 goes through 0021C350, 0x1CA, and ends in +5 3 / 0x3F or +5 1 / 0x40. 0021C040 ends this kind outright |

  Kind 4 has run live in a04d_01, which still does not reach its end. The recording
  is never grabbed and stays at health 60. The port takes a second bite by pad frame
  200 (health 55), is grabbed at (356.42, 14.42, 224.56), and the script has no
  stick input for 001754E0(8), so bites of 25, 12, 12, then the remaining 6 kill the
  player, with no fault. Why the port is bitten and grabbed is not established; it
  starts before the latch. No beat has exercised kinds 2 and 3, so those rest on the
  oracle alone.
  The grab 00130AB0, 00133CD0
  (test_level10_port_reference); 0019A440 and its twins 001A1B80, 0019DB50
  (test_coll_segment_walkers_reference, the only sight probes over captures).
- **Others**: the ring burst 001F28D0 (NEARMISS C at 98.80%, only a schedule differs),
  the crawl node 00188340 family (test_level9_port_reference); script [18]'s
  callbacks 0x826470, 0x826500, 0x826540 (test_level14_port_reference).
- **Scripts** (`em_area01_script_live`): the window 0x82AD50 .. 0x82F790 and the
  op09 callbacks 0x826B30, 0x827540, 0x827B10, 0x827B20, 0x826470, 0x826500, 0x826540.
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

1. **ERCPR and ERLENG of zero** (`em_vu1_page_programs.h`). A streak quad whose
   head has w exactly 0 stops the page: "VU1 translation fault ... (2)". The head
   (1056.25, 160, 920) sits on the camera plane: the exact w is 2.2e-5, which the
   truncating sum takes to 0. It is not rare: a19b_01, a19c_01 and a19d_15 all hit
   it, so a player in AREA19's streak rooms can too.
   Measured on the fork, a VU1 program of ours started by a VIF1 MSCAL from the BIOS
   (recompiler, MTVU off): ERCPR(1.0) = 1.0, ERCPR(+0) = 0x7F800000, ERLENG(0,0,0) =
   0x7F800000; MULx by that P gives 0x7F800000 for 1.0, 0xFF800000 for -2.0 and
   0xFFC00000 for 0.0. That is the emulator's IEEE behaviour, not a hardware
   measurement, and the rest of the quad (the lanes after P, VFTOI4 of those words,
   what the GS does with them) was not measured. The translation stays fail-stop:
   choosing a result is the user's decision.
2. **Not drawn**: 001CB1F0, 001CB130, 001CB060, the tendril field's and the worms'
   draw methods, whose VU1 programs have no translation; reported once.
3. **Scene 27's ramp** faults at +0x74; no recorded beat reaches it.
4. **a19b_00**, the arrival by the hatch [63], is the AREA13 side's.
