# AREA15 assets over the fourteenth level's captures

Lane "A15ASSETS", level side track, 2026-10-02. This lane adds new files
only: five exporters (`tools/export_area15_common.py`, `_level.py`,
`_tables.py`, `_sfx.py`, `_split.py`), one checker
(`tools/test_area15_assets_reference.py`) and this document. It changes no
port source, no tracked port file, no earlier exporter or checker and no
decomp file, and nothing it adds is wired into the game. Every exported
byte comes from the user's own disc files and ELF and is written into the
ignored `assets/area15/` (and the ignored scratch `build/area15/`). Nothing
was run in PCSX2.

## Target and captures

AREA15 (area 0x0F, `OVERLAY/AREA15.BIN` = MWo3 id 0x0C at 0x823500,
0x6A00 bytes). INDEX.IDX sector 19 holds a top block without sections
(resident offset 0) and two nested blocks (`chunk19.n0`, `chunk19.n1`, one
per sub), each with a sound container (section 0) and a GS texel upload
(section 1). The fourteenth level (FOURTEENTH_LEVEL_ROUTE.md) loaded each
sub once; one capture ends in each (`../Extermination/build/s87/`,
ignored):

| Sub | Capture | Area bytes D_00810700..702 | Capture before the load |
|---|---|---|---|
| 0 | `route_a19d/a19d_20_door50` | 0F 00 01 (after [0]'s scene) | `a19d_19_flights` (AREA19 sub 1) |
| 1 | `route_a15/a15_01_door51` | 0F 01 00 | `a15_00_door14` (AREA19 sub 1) |

Each sub's capture paths are its group's folders: a19d_00 .. a19d_19 and
a15_00 end in AREA19 and are listed as excluded (pinned by the checker).

**Each capture's sub is measured** (`loaded_sub_proof.json`,
`export_area13_common.loaded_sub_proof`): a19d_20 differs from the sub-0
map in 886 16-byte rows against 289,705 for sub 1; a15_01 in 668 rows of
the sub-1 map against 367,497 for sub 0.

**Cursors** (measured): both loads put the top block at D_0028A73C =
0x133C1C0, the previous AREA19 load's top cursor as well; the nested
blocks at D_0028A740 = 0x1820440 (sub 0) and 0x18211C0 (sub 1). Every
top-list relocation word is equal in both captures.

**The id-0x45 word** (D_0028A5A4, the dynamic-object list). Sub 0's nested
list relocates id 0x45 (chunk19.n0 +0x2F8000): D_0028A5A4 = 0x1B18440 =
D_0028A740 + 0x2F8000 (001FFCD0 state 11), a list of 78 entries that the
capture draws 40 kernel-0x00237450 kicks from. Sub 1's lists do not
relocate id 0x45, and D_0028A5A4 keeps 0x1B18440, sub 0's address: in
a15_01 and in the AREA19 sub-1 capture before it (a15_00), so neither the
AREA19 sub-1 load nor the AREA15 sub-1 load rewrote it. Before the sub-0
load it held 0x1980000 (a19d_19), the value the AREA13 lane found stale
since AREA04.

## Reuse: the AREA13 lane's exporters, pointed at AREA15

`export_area15_common.install(sub)` registers an AREA15 `Target`
(`'area15'`) in `export_area13_common.TARGETS` and points the AREA13 lane's
modules at it, **unchanged**: `export_area13_common` (load map, relocation
words, descriptor, sub proof), `export_area13_level`, `export_area13_tables`,
`export_area13_sfx` and, through them, every module AREA13_ASSETS.md "Reuse"
lists. Per sub it sets the per-target tables those modules key by name:
`PREVIOUS`, `CREATURES` (None: no outdoor-creature twin), `FLAG_OWNERS`,
`PLACEMENTS` / `GROUPS` (sub 0: 0x829800, 28 records, group 0x826980 x6;
sub 1: 0x829C90, 14, group 0x826AC0 x11), `SPAWN_SUB0` (sub 0: 0x24CB80 x3;
sub 1: 0x24CC10 x1), `DOOR_ROW` (0x24E0B8 = D_0024E140[0x0F]),
`SCRIPT_ENTRIES` (the 15 chains the committed AREA15 C starts, re-read by
the checker) and `WRITERS` (none). Each pin is the original's table walk
(`export_area11_roster.walk_roster`, `export_area13_tables.spawn_rows`) over
the pinned ELF and overlay.

Two behaviour tables of `export_area13_tables` name addresses of other
modules, so `install` replaces them while AREA15 is installed and
`uninstall` restores them: `DOOR_BEHAVIOURS` without 0x823580 (the AREA13 /
AREA19 overlay door; no AREA15 C calls 001BC150 or 001BC240, which the
AREA13 checker's door rule re-reads) and `EXPLICIT_MODELS` without 0x826850
(the AREA13 hatch; in AREA15, 0x826850 is the behaviour of sub 1's
placements [1] .. [3]). No AREA15 node behaviour or door is at 0x823580
(the AREA15 init 00823540 runs there, decomp docs/AREA15_OVERLAY.md).

### The AREA15 rules

1. **Cell-hull owners** (`owner_matrix`), from the overlay C of every jal
   of 001A2370 in the module: 0x825320 (`func_overlay_AREA15_008252E0.c`,
   byte-identical) and [8] 0x826600 (`func_overlay_AREA15_008265C0.c`,
   byte-identical) pass node + 0xD0 in their state 1; 0x825430
   (`func_overlay_AREA15_008253F0.c`, NEARMISS) and its mirrored twin
   0x825D10 (`func_overlay_AREA15_00825CD0.c`, NEARMISS) pass
   D_00275B40[0] + 0x90, i.e. *(node + 0x110) + 0x90 (D_00275B40 = node +
   0x110 while a behaviour runs, the reading `export_area13_level`'s
   creature rule uses). The NEARMISS headers name only two extra scratchpad
   reloads as the divergence. The evidence for every owner's argument is
   the ORIGINAL instructions at its one jal of 001A2370, read from the
   user's overlay by the checker (`ov_arg_problems`, fields of the words):
   0x825320 / [8] build a1 as R + 0xD0 just before the call and copy the
   same R into a0 in its delay slot; 0x825430 / 0x825D10 (sites 0x825C50 /
   0x826540) build a1 as R + 0x90 in the delay slot, where R was loaded
   from the word D_00275B40 points at. Measured: the ORIGINAL 001A2370
   reproduces every moved hull with these matrices: a19d_20 hulls 0, 1, 2
   (0x825320), 3 (0x825D10), 4 (0x825430), 14 (the pickup 0x219550);
   a15_01 hulls 5, 6, 7 (0x219870 through the ORIGINAL 0x219F50), 8 ([8]),
   13 .. 17 (pickups). No orphan and no underived hull. **The capture does
   not tell the bone-0 rule from node + 0xD0**: both swing nodes (0x7AA590
   hull 3, 0x7AA880 hull 4) are at rest in a19d_20 and the 64 bytes at
   node + 0xD0 and at *(node + 0x110) + 0x90 are equal, so the derivation
   reproduces hulls 3 and 4 under either; it does rule out the other
   bones. The bone-0 rule rests on the original instructions above.
2. **The uid words** (`flag_calls`): the only 0019C6F0 caller in the module
   is [8] 0x826600 (sub 1): state 0 calls (0x22, 1) and moves on; states 2
   / 3 call (0x22, 0) and free the node. A live node in state 1 made
   (0x22, 1) last; one in state 0 none yet; any other state, or two nodes,
   is refused (ValueError). Measured: a19d_20 no call, a15_01 (0x22, 1); the
   ORIGINAL 0019C6F0 with those calls reproduces every uid word. Outside
   AREA15 the AREA13 lane's rule runs.
3. **Sub 0's dynamic list** (`export_area15_level.export_dynamic`):
   `export_area13_level.export_target` refuses a load that relocates id
   0x45, so sub 0 runs `export_dynamic`, the same sequence of the same
   helpers with the AREA01 lane's dynamic-list step (`dynamic_entries`,
   `check_kicks` with the list, `level/dynamic_objects.emsc`) in place of
   the stale-pointer step, and `dynamic_list_problems` (the nested list
   relocates id 0x45; D_0028A5A4 is that address). Sub 1 runs
   `export_area13_level.export_target` unchanged (its stale-pointer model
   holds: the previous capture's word equals the capture's).

## Outputs

`assets/area15/` (ignored, about 16 MB):

- `sub0/` and `sub1/`, each in `assets/area19/sub0/`'s layout:
  `level/00_id44.emdl` (+ `.gsmat.json`), `level/static_bank.emsc`,
  `level/level.json`, `background.embg`, `area15.emcl` (+ `scene.txt`),
  `area15_cells.bin`, `cells.json`, `roster.emro`, `message_data.emmd`,
  `world_models.emwm` (+ `.json`), `sfx/area15_banks.bin`, `sfx/banks.json`,
  `sfx/sfx_registry.emsr` (+ `.json`), `tables.json` (that sub's report);
  sub 0 also `level/dynamic_objects.emsc`;
- `spawn_table.emsp`, `door_destinations.emsp`, `scripts.emsc`,
  `overlay_data.emsc` once (both subs' exports are byte-identical; the split
  refuses otherwise), `loaded_sub_proof.json` (both captures);
- `area15.json`: every file's SHA-256 and size, the captures per sub, and
  the sub-1 files equal to their sub-0 namesake (`message_data.emmd`,
  `scene.txt`).

| | Sub 0 (a19d_20) | Sub 1 (a15_01) |
|---|---|---|
| static bank | 0x184C440, 980 objects -> one zone EMDL: 979 objects, 17,569 vertices, 11,328 triangles, 102 textures | 0x18AC9C0, 211 objects -> 210 objects, 18,839 vertices, 12,051 triangles, 106 textures |
| kicks | 650 level (204 distinct REFs), 40 dynamic | 1,884 level (69 distinct REFs), none dynamic |
| dynamic list | 0x1B18440, 78 entries | none (D_0028A5A4 stale, sub 0's) |
| background | armed, 256 x 16 | armed, 256 x 16 |
| ctx room entry | 34 | 35 |
| collision | `chunk19.n0/id42`: 1,801 vertices, 1,147 polygons | `chunk19.n1/id42`: 2,325 vertices, 1,538 polygons |
| cell directory | 20 uids, 41,344 bytes; moved 0..4, 14 | 22 uids, 35,504 bytes; moved 5..8, 13..17; flag uid 0 |
| roster | 28 placements + 6 group records | 14 placements + 11 group records |
| spawn rows | 0x24CB80 x3 (entry 1) | 0x24CC10 x1 (entry 0) |
| doors used | [13] slider id 1 -> 02 01 00 00; [14] id 0 -> 13 04 01 01 | [9] id 2 -> 13 05 01 01 |
| scripts | 15 chains, 144 records, window 0x826E70 .. 0x8297F0 | the same |
| run-time words | 0x827060 .. 0x82706C (inside reached records) | none |
| messages | 436 area / 54 global records | the same bytes |
| world models | 16 at 0x1820440 (179,568 bytes), 17 owners bound | 15 at 0x18211C0 (571,056 bytes), 12 bound |
| sound | chunk19.n0 +0: 214,336 bytes, 1 bank; bound 1.0-1.2 global, 2.0 area; refused 3.0, 4.0, 4.1, 4.2 (headers without SShd magic) | chunk19.n1 +0: 481,168 bytes, 3 banks; bound 1.0-1.2, 2.0, 4.0, 4.1; refused 3.0 (no magic), 4.2 (a freed handle record) |
| registry (1,000 ids) | 233 audible, 753 absent, 14 unsupported, 19 samples | 418 audible, 530 absent, 52 unsupported, 31 samples |

Port loaders as in AREA13_ASSETS.md "Outputs"; `dynamic_objects.emsc`
loads through `em_script_image_load` (as AREA01's).

## Tools

Pure Python on native arm64 macOS; run from the port root.

```sh
python3 tools/export_area15_level.py    # both subs: level, textures, background, collision, cells, ctx, sub proof
python3 tools/export_area15_tables.py   # roster, spawn, doors, scripts, overlay data, messages, world models
python3 tools/export_area15_sfx.py      # the containers and the registries
python3 tools/export_area15_split.py    # build/area15/assets/tree/ -> assets/area15/
python3 tools/test_area15_assets_reference.py                   # checker
EM_AREA15_ASSETS=<dir> python3 tools/test_area15_assets_reference.py   # check another area15/ tree
```

Timings on the M1 (2026-10-02, other lanes running): level 3.6 s CPU,
tables 0.4 s, sound 0.8 s, split under 1 s; checker 9.9 s CPU (user +
system; 11 s wall alone, up to 20 s while other lanes ran) with its loaders built (its first run builds them
privately into `build/area15/assets/test/`). The two captures are the whole
set, so `EM_TEST_FULL=1` runs the same checks. Missing exports, ELF, ISO,
overlay, captures or the census delta print SKIPPED.

## What is checked

Per sub the checker builds a view (symlinks: `sub<s>/`, its `tables.json` as
the side report, the shared side files) and runs:

**The AREA13 lane's checks** (`test_area13_assets_reference.run_checks`, its
port loaders, its canary), imported unchanged and pointed at AREA15 with
its per-target pins (descriptor hash of sector 19, the grid word, (live, at
rest) placement nodes (18, 15) and (13, 11), model owners bound 17 and 12,
the bank bindings and refusals above, the excluded folders), with four of
its functions replaced by name:

- `stale_problems`: no top list relocates id 0x45; sub 0: the nested list
  does and D_0028A5A4 is its address; sub 1: no list does, and D_0028A5A4
  is 0x1B18440 in the capture and in a15_00;
- `level_problems` (sub 0 only; sub 1 runs the original):
  `dynamic_objects.emsc` = *D_0028A5A4 over its extent in every capture
  (the AREA01 checker's `level_bank_problems`), every kernel-0x00237450 kick
  inside one of its entries, and the original's bank, GS-state, zone,
  texel and background checks;
- `census_problems`: every jal / j / address word of 001A2370, 0x219F50 and
  0019C6F0 in the ELF and the module is a pinned site inside its function
  (sizes from the C headers); the owners are exactly the modelled ones;
  each AREA15 owner's a1 is the one its original instructions build
  (`ov_arg_problems`, rule 1); the other sub's AREA15 owners have no live
  node and no record in this sub's roster;
- `canary_plants`: the original's plants without its creature and writer
  plants (no outdoor creature, no writer window in AREA15) and without its
  0x219870 plant in sub 0 (no such node), plus a plant per live AREA15
  001A2370 owner (a byte of the matrix it passes) and, in sub 0, a byte of
  the dynamic list.

Load map (every mapped byte = RAM, every relocation word = its formula,
labels), level, collision, cells (every byte through the ORIGINAL 0019C6F0
/ 001A2370 / 0x219F50), tables, sound and ctx (the ORIGINAL 001D8FD0 then
001D1C50, room entries 34 and 35) pass in both subs; 14 + 13 files load
through the port loaders (sub 0's dynamic list among them, and a cut copy
of it is refused). The canary reports 53 + 50 planted sections, and its
owner plants are pinned (`plant_problems`): one per moved hull whose owner
is an AREA15 owner (sub 0 hulls 0 .. 4, sub 1 hull 8).

**This lane's checks** (`lane_checks`):
- captures: the one capture per sub, its area bytes, the excluded folders,
  the capture before the load is in AREA19 (area byte 0x13), has the
  module header at 0x823500 and overlay id 0x10 resident (three guards),
  the four
  reports made over the capture, `level.json`'s sub and previous capture,
  `PREVIOUS` re-pointed;
- cursors: six words pinned per sub; the top-list words equal in both subs;
  the top cursor the previous capture's;
- the D_0028A5A4 word (`stale_problems` above, also run here);
- pins: each sub's placement table, groups and spawn rows equal the
  original's walks over the pinned ELF and overlay (`walk_roster`,
  `spawn_rows`), the door row D_0024E140[0x0F], and `install` put exactly
  those into `export_area13_tables`, without the AREA13 hatch rule and the
  door 0x823580;
- split: `area15.json` lists every file once with its hash and size,
  nothing else; the side files once at the root; each sub's `tables.json`;
  `sub1_same_as_sub0` recomputed;
- sub proof: `loaded_sub_proof.json` = the recomputation, the own sub over
  100 times better;
- rules: the flag calls pinned per capture; `cells.json`'s proofs = the
  rows the directory derivation recomputed in this run; each moved hull's
  owner pinned (rule 1's list); no orphan or underived hull; the committed
  C makes exactly one 001A2370 call in each owner and passes rule 1's
  argument (two guards, comments stripped) and [8]'s C
  calls 0019C6F0 exactly (0x22, 1) in case 0 and (0x22, 0) in case 3 (case
  2 falls through), each owner file's runtime address its own;
- census (`../Extermination/build/s87/census/a15_delta.json`): the passes
  A19D and A15 over the 23 beats, replays complete; 001FFCD0 in both AREA15
  loads; 0x825320 / 0x825430 / 0x825D10 in a19d_20 and not in a15_01; [8]
  0x826600 in a15_01 only; no unattributed hit; other-overlay hits only of
  AREA19 (id 0x10).

**Controls (189)**, each required to report its guard's own message (a
substring of that guard's text, so a control caught by a neighbouring
guard does not count) or to be accepted where marked. Per sub, capture
checks: as recorded (accepted); the capture missing; the entry byte; an
excluded folder missing; the capture before the load read as AREA15,
without its module header, with overlay id 0x0F resident; each
of the four reports made over no capture; `level.json` naming the other
sub; naming another previous capture; without its D_0028A5A4 record;
`PREVIOUS` not re-pointed. Cursors: as captured (accepted); each of the six
words + 0x10; the other sub's last top-list word moved; the previous
capture's top cursor moved. D_0028A5A4: sub 0 as captured (accepted); not
the relocated id 0x45; a nested list without id 0x45; the rule as captured
(accepted); a top list relocating id 0x45; sub 1 as captured (accepted);
not sub 0's list; a nested list relocating id 0x45; the capture before the
load read as a19d_19. Split: as exported (accepted); the manifest missing;
naming another capture; an extra file; a sub-1 file missing; a static-bank
byte; a side file placed in `sub0/`; `sub1/tables.json` not placed;
`sub1_same_as_sub0` short; `export_area15_split.tree_files` on the tree
(accepted), a stray file, a part's side file missing, the proof missing,
`sub0/` missing, the side files differing. Pins, per sub: as installed (accepted); the placement, group and spawn
pin changed; `export_area13_tables` holding the other sub's spawn pin; the
door row + 4; the door 0x823580 left installed; the AREA13 hatch rule
left installed; a 0x826850 node of
a15_01 set to state 2 is no hatch (accepted). Sub proof: as exported
(accepted); without a15_01; a count + 1; a15_01 read as sub 0. Rules: as
exported (accepted); [8] in state 0; no recomputed rows; `cells.json`
naming another hull's proof; hull 8 owned by the pickup (record and rows);
hull 13 an orphan; `rule_problems` with 0x825430's C passing node +
0xD0; the C as committed (accepted); each owner's C passing
another argument (4), with a second 001A2370 call (4); [8]'s C calling (0x22, 1) in states 2 / 3; an owner
address that is not its file's runtime address. Exporter rules: [8] in
state 2 refused; state 3 refused; two [8] nodes refused; state 0 no call, state 1 (0x22, 1)
and a capture read as AREA19 (the AREA13 lane's rule) (accepted);
`owner_matrix` of 0x825320, 0x825430 and the pickup, and of 0x825320 /
0x825430 outside AREA15 (accepted with their values; the 0x825430 value
is the one `ov_arg_problems` pins in the original, so the positive
control is not the only evidence). Canary owner plants, per sub: as made
(accepted); one dropped. Census: as recorded
(accepted); a third pass; a beat dropped; a beat missing; a beat
incomplete; a replay error; a replay not completed; 001FFCD0 not in a15_01;
0x825320 not in a19d_20; 0x825D10 also in a15_01; [8] also in a15_00; an
unattributed hit; an AREA13 other-overlay hit; no functions list.
`lane_checks` as exported (accepted), with an unattributed hit, without the
proof, with the sub-0 spawn pin + 1. Call-site census per sub: as captured (accepted); a stray jal, j and
address word of 001A2370 in the module (3); the original 001A2370
arguments (accepted); per self-matrix owner a1 built as R + 0xD4, another
register copied into a0, no jal; per bone-0 owner a1 built as R + 0xD0, R
loaded from 4(R), R read from gp + 4 past D_00275B40; 0x825430 sized 0x100; 0x825D10 not modelled; a live
node of the other sub's owner; a placement and a group record naming it.
Sub 0's level replacement: as exported (accepted); no dynamic-list file; a
dynamic-list byte; the list emptied in RAM and file (its kicks outside it).

**The canary's flag-call-record section in sub 0.** The AREA13 canary plants
an empty flag-call list into `cells.json`; a19d_20 has no 0019C6F0 call, so
that plant equals the record and cannot report. The checker drops exactly
that one canary failure line and replaces the section by the same
comparison with a planted non-empty list ([0x22, 1]), which
`cells_problems` must report (`flag_record_canary`).

## Mutation sweep (one bounded sweep, this lane)

Single-operation mutants of this lane's code, each run in a private copy
of the port root (`tools/` copied and mutated, `src/`, `assets/*` and the
export tree symlinked, `../Extermination` symlinked, the loader dylibs
copied), 4 workers; the mutant that changes the split (X19) re-ran
`export_area15_split.py` into a private `assets/area15/` first.
`build/area15/sweep/` (ignored: `mutants.py`, `sweep.py`, `pass1.txt`,
`final.txt`). Killed = the checker exits non-zero.

- Mutants: the checker's lane guards (capture, cursor, D_0028A5A4, sub-0
  level, call-site census, split, sub proof, rules, C arguments, census,
  canary plants, the flag-record replacement, its output filter) and the
  exporter rules (`owner_matrix`'s three branches, `flag_calls`' guards and
  states, the install lines for the door / hatch tables and `PREVIOUS`, a
  spawn pin, the self-matrix owner list, `dynamic_list_problems`' two
  guards, `tree_files`' three guards, the split's twin test).
- Pass 1: 75 mutants, 70 killed. Survivors: L04 (an extra accepted
  dynamic-list range), R06 (`rule_problems` without its C-argument check:
  the C controls called `c_arg_problems` directly), Y04 (the canary output
  filter), X10 (the AREA13 hatch rule left installed: no 0x826850 node of
  a15_01 is in state 2), X12 (the sub-1 spawn pin: the checker read the
  rows from the disc and never compared the pin). Added: `rule_problems`
  takes the C sources and a control feeds it a changed argument;
  `pin_problems` (each pin against the original's walks and against what
  `install` put into `export_area13_tables`, without the hatch rule and
  the door 0x823580), its controls, a control with a 0x826850 node in
  state 2, a `lane_checks` control with a changed spawn pin; 8 mutants of
  the new guards (Q01 .. Q08).
- Final pass: 83 mutants, 80 killed. Survivors: Q06 (the door-0x823580
  term of the table guard); a control was added (the door left
  installed) and Q06 re-run: killed. L04 and Y04 are equivalent: L04 adds
  an accepted range at 0x100040 .. 0x100860 (boot text), which no kick of
  either capture references; Y04 changes only which lines are printed
  (the verdict is the failure list).
- Result: 81 of 83 killed, 2 equivalent.
- Review (r14) found five more survivors in its own 12-mutant sweep,
  each a term of a compound guard or an unpinned canary count: R02 (an
  owner's C with a second 001A2370 call), R03 (the j / address-word site
  forms), R04 (the MWo3 and overlay-id terms of the before-capture
  guard), R05 (`FLAG_STATES` accepting state 3), R10 (the canary planting
  only 0x825320). Fix (no new sweep): each compound guard split into one
  guard per term, each with a control of its own (listed above), and the
  canary's owner plants pinned. The five were re-run in the sweep's
  sandboxes (`build/area15/fix/named.py`, R04 as two mutants): all
  killed, each by its new control (R10 by `plant_problems`). The review
  also showed the lane's X02 (the bone-0 owners given node + 0xD0) was
  killed only by the positive `owner_matrix` control, not by the
  derivation; `ov_arg_problems` now pins that argument in the original.
- The reused AREA13 / AREA01
  comparators keep their own sweep records (AREA13_ASSETS.md,
  AREA01_ASSETS.md); this sweep covers exactly the mutants above.

## Findings (for the lead)

1. **AREA15 sub 0 relocates id 0x45**: D_0028A5A4 = the relocated list
   (78 entries, 40 kicks drawn in a19d_20). No earlier AREA13 / AREA19
   capture does; their exporter refuses such a load.
2. **The id-0x45 word outlives the area**: after AREA15 sub 0, the AREA19
   sub-1 load (a15_00) and the AREA15 sub-1 load (a15_01) leave D_0028A5A4
   at sub 0's address. In a15_01 that address lies inside the sub-1 load
   map; no capture draws a dynamic kick in sub 1.
   A port that clears or re-derives the word on every load would differ.
3. **AREA15's top block lands where AREA19's did** (0x133C1C0): the
   streamer's top cursor is the same for the AREA19 sub-1 loads and both
   AREA15 loads.
4. **The swinging owners pass a bone matrix**: 0x825430 / 0x825D10 call
   001A2370 with *(node + 0x110) + 0x90 (bone 0), not node + 0xD0. The
   evidence is the original instructions at their call sites (rule 1,
   `ov_arg_problems`). a19d_20 does not distinguish the two: both nodes
   are at rest there and the two 64-byte matrices are equal, so a port
   that used node + 0xD0 would match this capture and differ only once
   they swing.
5. **Sub 0's sound refusals**: group 4's three slots point at headers
   without the SShd magic (0x16290C0, 0x1629910, 0x1629FB0), so 00119EA0
   does not use them in a19d_20 (the AREA13 lane's refusal rule); sub 1
   binds 4.0 and 4.1 to its own banks.

## Binding

Nothing is wired. When AREA15 is bound, each sub is AREA13_ASSETS.md's
binding with that sub's asset set: the files of `assets/area15/sub<s>/` and
the four shared side files; sub 0 also its dynamic list (the AREA01
binding of `dynamic_objects.emsc`). The static bank, dynamic list and
world-model images carry the bases the captures hold (top 0x133C1C0;
nested 0x1820440 / 0x18211C0); the cursors must be the ones the original's
streamer leaves, not constants. D_0028A5A4 must be relocated for sub 0 and
left as it was for sub 1 (finding 2). The AREA15 owners must reproduce
rules 1 and 2: 0x825320 / [8] re-transform their hulls with node + 0xD0,
0x825430 / 0x825D10 with their bone-0 matrix; [8] sets key 0x22 at its
state 0 and clears it when it leaves.

## Known gaps

1. **One capture per sub.** Every per-capture pin (live counts, flag
   calls, hull owners, refusals) holds for that frame; sub 0 is held only
   after [0]'s scene (a19d_20), sub 1 only at its arrival (a15_01).
   AREA15 sub 1's event ([4] / [6] 0x823850, flag 0x23, the forced return)
   and sub 0 after it (FOURTEENTH_LEVEL_ROUTE.md section 7) are in no
   capture.
2. **Untaken branches of rule 2**: [8] in states 2 / 3 (it calls (0x22, 0)
   and frees itself) is refused, not modelled; no capture holds it.
3. **The bone-0 argument is not tested by a capture**: 0x825430 /
   0x825D10's argument is in NEARMISS C and pinned by the original
   instructions (`ov_arg_problems`); in a19d_20 node + 0xD0 and the bone-0
   matrix are byte-equal, so the derivation cannot tell them apart. No
   capture holds a swing node away from rest.
4. **The canary's flag-call-record section** cannot fire in sub 0 (see
   above); it is replaced, not run.
5. **Descriptive side files** (`level.json`, `world_models.json`,
   `banks.json`, `scene.txt`, `.gsmat.json`) are checked only for their
   capture lists and hashes.
6. The run-time words 0x827060 .. 0x82706C of a19d_20 are accepted because
   they lie in records a chain reaches (the AREA13 lane's rule); which
   chain op wrote them was not traced.
