# AREA15 and AREA03 in the shared composition (lane K1)

Lane K1, 2026-10-30, branch `area15`. AREA15 (area byte 0x0F, subs 0 and 1) and
AREA03 sub 0 run in the shared AREA01 composition, as AREA04, AREA22 and AREA06
do (LEVEL3_BINDING.md "lane B1", LEVEL8_PORT.md "Start-card runs"). The route's
ground truth is FOURTEENTH_LEVEL_ROUTE.md and FIFTEENTH_LEVEL_ROUTE.md. This is
a start-card binding: row-exact New Game chaining is a chain lane's.

## Beats

`python3 tools/mid_route_beats.py a15a a15 a15b a03` (recorded pad, recorded
path, the recording's rand() word, end block and end position).
`python3 tools/test_area15_rows.py a15a a15b a03` compares the rows.

| Beat | Card | Status | Rows (position, heading, action) |
|---|---|---|---|
| a15a_00 (a19d_20 from f526: [0]'s scene 0x826E70) | `area15` | **passes**: end block, end position | 9,402 of 9,429 identical; 9,348 in one run (the last differences are the walk off the scene) |
| a15_00 (slider [13], door [14]) | `a15_00` | plays to door [14]; stops at AREA19's load | not compared |
| a15b_00 (the event) | `a15b_00` | **passes** | steered walk, not compared |
| a15b_01 (the forced return) | chain | **passes** | see below |
| a15b_02 ([1]'s and [2]'s scripts) | chain | **passes** | 9,160 and 1,746 consecutive identical rows through the two scripts |
| a15b_03 (slider, bed) | chain, or `a15b_03` | **passes** (health 100, infection 0) | the walk is steered, not compared |
| a15b_04 (slider, door [14]) | chain, or `a15b_04` | plays to door [14]; stops at AREA19's load | not compared |
| a03_00 (the lift, the ride) | `a03_00` | **passes**: end block, end position | 271 of 2,558; the walk is steered, and the scripted ride is the rest |

A card is not the route's machine state (the fork's rows include a walk under
the pad and creatures at their recorded places), so these are playable proofs
with row comparison where the script drives the player. Where a beat ends in
AREA19 the stop is the module pack's missing sectors for area 0x13: lane J1's.

Unrecorded: nothing of AREA03 after a03_00. FIFTEENTH_LEVEL_ROUTE.md section 6
says what stopped the exploration (six 0013D2D0 creatures, two rounds held).

## What runs where

- **Overlay state** (`em_area01_state.c`, `k_mid`): AREA15 header id 0x0C, text
  0x3440, data 0x3580, BSS 0x829F00 + 0x140C00; AREA03 id 4, text 0x33C0, data
  0x2800, BSS 0x829100 + 0x140C00. 001E7780's keys 0xF00 / 0xF01 / 0x300 / 0x301
  run the overlay init at 0x823580 (`overlay_AREA15_func_00823540.c`,
  byte-identical: four stores). `make test-area15-area03-state-reference` runs
  the original 001E7780 over both areas' fork captures.
- **Owners** (`em_area01_runtime_area15.inc`): `dispatch_a15` (em_level14_port's
  20 entries, em_area15_route's six), `dispatch_a03` (em_area03_route's seven,
  em_area03_arrival's seven), each entry's frame read from its first
  instruction. Oracles: test_level14_port_reference, test_area15_route_reference,
  test_area03_route_reference, test_area03_arrival_reference.
- **AREA03's creature** 0013D2D0 (`em_level15_creatures.c`): 0013D2D0, 0013D850,
  00140690, 00140C00, 00141C90, byte-matched C; test_level15_creatures_reference
  covers every reachable word (972 cases).
- **Boot owners** in these areas bind generally; a translated one runs through
  lane J1's fallback dispatch (`tools/gen_later_dispatch.py`, which also takes
  em_level8_port and em_area04_port here), an untranslated one faults at its
  address when it runs. Boot data: the windows of
  `export_area00_runtime_tables.py` and `assets/boot_constants.embc`.
- **Scripts** (`em_area01_script_live.c`): AREA15's 15 chains, window 0x826E70 ..
  0x8297F0; AREA03's five, 0x827170 .. 0x828FD0. Script callbacks: 0x823F60;
  0x826270, 0x826330, 0x826860, 0x826880, 0x8268B0.
- **Assets**: `assets/area15/sub<n>/`, `assets/levels/03/sub0/`; the level
  background of AREA15 loads at the area read when its export exists.

## Shared code this changed (each from the original)

- `em_sfx_bank`: controller B0 0A (00118418's pan slide, 00116598's second
  slide) and B0 60 (00118790's loop), SFX_SEQUENCER.md. EM_SFX_OP_MAX is 64.
- `em_area01_timeline`: 0022EC30's whole table and 0022EEF0's audio track and
  leading switch (scene 27's 3852..3960 ramp faults at +0x74: not translated).
- `em_area_script`: op0B sub 5 (001B8020's bone_init_default_2).
- `em_area01_actor_view`: a read inside the 16 header bytes of a record freed in
  the window answers the zeros 001AFC10 left (0x824510 reads +0xD after
  0x824560 may free it).
- 0022BBC0 on a pool actor: its target's +0x110 words and slots, the tint spans
  +0x60 / +0x80, 001CD2B0 and D_00275C00 for its kind-0 fade.
- Shadow proxies by kind (0x2A..0x34, `assets/npc<kind>_shadow.emdl`).
- `em_area01_script_player_banks`: bank row 0x9B (script 0x8277C0's op0A sub 1).
- Movie selector 7, E46S4.PSS. Its feed_end, 725, is the recording's 747-vblank
  step less the 22 the fork's E006S1 / E007 probes measured; not a probe of its
  own.
- `em_area01_runtime`: em_area06_port's boot routines for any area.

## Open items

- **The class-6 draw of AREA15 sub 1's [3]** (0x826850's +0xD 0xD method,
  001CB2B0 -> 001CB200 -> 001D4640 -> 001D4440, a screen-copy pass on channel 3)
  has no live chain page: the node is not drawn while D_008107FB >= 3 and the
  log says so once. em_level15_support translates it with its oracle; it needs
  001D2090, 001D6930, 001D6BA0, 001D6580 on the live channel.
- **Doors into AREA19** (a15_00, a15b_04, and a19e into AREA03) wait for lane J1.
- **a03_00's walk** and the a15b_03 / a15b_04 walks are steered: row-exact
  needs the New Game chain.
- The 0013D2D0 family's other states (00140760, 001408D0, 00140A90, 0013D980 ..
  00140440) run through whatever translation the fallback has; a fight in
  AREA03 is unrecorded.
