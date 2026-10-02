# AREA13 assets over the later captures (the second load and the south field)

Lane "A13X", level side track (tenth and eleventh levels), 2026-10-01. This
lane adds new files only: five exporters, one checker and this document. It
changes no port source, no tracked port file, no earlier exporter or checker
and no decomp file, and nothing it adds is wired into the game. Every
exported byte comes from the user's own disc files and ELF and is written
into the ignored `assets/area13/reload/` (and the ignored scratch
`build/area13x/`). Nothing was run in PCSX2.

## Target and captures

AREA13 (area 0x0D) **sub 0**, loaded a second time when the tenth level came
back up AREA19's ladder (TENTH_LEVEL_ROUTE.md, a13b_00) and still resident
through the eleventh level's south field (ELEVENTH_LEVEL_ROUTE.md). Its 13
captures (`../Extermination/build/s87/`, ignored), in route order:

| Captures | Area bytes D_00810700..702 |
|---|---|
| `route_a13b/a13b_00_ladder_up` | 0D 00 06 |
| `a13b_01_door17`, `a13b_s0_roof_ladder` | 0D 00 09 |
| `a13b_02_button15` | 0D 00 03 |
| `a13b_03_door8`, `a13b_04_lift_call` | 0D 00 01 |
| `route_a13c/a13c_00_recharger` | 0D 00 04 |
| `a13c_01_to_machine` .. `a13c_06_south` (6) | 0D 00 09 |

`a13b_05_lift_ride` ends in AREA04 and is the only excluded folder. The a13
group (the AREA04 arrival a04b_04 and a13_00 .. a13_04) is the AREA13 lane's
(AREA13_ASSETS.md) and is not re-run here.

**The south field loads nothing new** (measured): every a13c capture holds
sub 0, the AREA13 module (MWo3 id 10) and the a13b cursors; AREA13's
descriptor is flat (one resident region, no nested block), so there is no
other sub or block to load, and the a13c census delta records no run of the
loader 001FFCD0. All 13 captures share one load map.

**The second load sits 0x1B80 higher** (AREA19_ASSETS.md finding 4):
D_0028A73C = 0x133C1C0 (the a13 group: 0x133A640), D_0028A740 = D_0028A73C
+ 0x6E2800 (the resident length), and every relocation word D_0028A490[id]
of the descriptor's list is the a13 group's (a13_04's RAM) + 0x1B80 in every
capture. The static bank is D_0028A5A0 = 0x14741C0, the grid D_0028A598 =
0x17D39C0, the model bank D_0028A59C = 0x133C1C0. D_0028A5A4 is still the
stale 0x1980000 (also in a19_02, the capture before the load).

## What the second load changes

`export_area13x_split.py` compares the full export over the 13 captures with
the a13 group's export file by file (`assets/area13/reload/reload.json`):

- **15 files are byte-identical** and are not written again: the zone
  EMDL and its `.gsmat.json`, `background.embg`, `area13.emcl` and
  `scene.txt`, `area13_cells.bin`, `roster.emro`, `message_data.emmd`, the
  sound container, `sfx_registry.emsr` and its `.json`, and the four side
  files (`spawn_table.emsp`, `door_destinations.emsp`, `scripts.emsc`,
  `overlay_data.emsc`).
- **Two binaries differ only in their base address words**, each by +0x1B80:
  `sub0/level/static_bank.emsc` (+8 base and +0xC entry, 0x1472640 ->
  0x14741C0) and `sub0/world_models.emwm` (+8 table, 0x133A640 ->
  0x133C1C0). Every other byte is equal.
- **Five reports** describe the later captures: `sub0/cells.json`,
  `sub0/level/level.json`, `sub0/sfx/banks.json`, `sub0/world_models.json`,
  `tables.json`.

So a port that loads AREA13 needs one asset set; only the two base words
follow the cursor the load lands on (Binding).

## Reuse: the AREA13 lane's exporters, re-pointed, plus three rules

`export_area13x_common.install()` imports `export_area13_common`,
`export_area13_level` and `export_area13_tables` (and through them every
AREA01 / AREA02 / AREA06 / AREA11 / AREA22 exporter and the decomp's
`export_level.py` / `export_collision.py` they reuse, AREA13_ASSETS.md
"Reuse") **unchanged** and re-points their AREA13 target: `AREA13.out` ->
`build/area13x/assets/tree/sub0`, `OUT` -> the tree, `SCRATCH` ->
`build/area13x/assets/scratch`, `capture_paths` -> the a13b and a13c folders,
`export_area13_level.PREVIOUS['area13']` -> a19_02 (the capture before the
second load). It installs three rules the later captures need, each read
from the owner's committed C and checked by the original-code derivations:

1. **[47] 0x8293A0 with D_008107F4 bit 0x40 set** (`flag_calls`;
   func_overlay_AREA13_00829360.c, byte-identical). The AREA13 lane's rule
   refuses the bit (its known gap 6). [45] 0x827150 sets it: its sub-state
   +5 = 1 ORs 0x10 once bit 1 is set (a13c_02 f1149, 0x12) and its sub-state
   2 ORs 0x40 (f1150, 0x52; func_overlay_AREA13_00827110.c). State 0 with the bit clear set +0x0D = REC[0x2C] (0x13) and made
   (0x1F, 1), (0x20, 0); state 1 counts +0x28 down from 0x6E0 while the bit
   is set and at 0, with +0x0D = 0x13, restores REC[4] (0x14) and makes
   (0x1F, 0), (0x20, 1). Measured: a13c_02 holds +0x28 = 1732 and +0x0D
   0x13 ((0x1F, 1), (0x20, 0) last); a13c_03 .. a13c_06 hold +0x28 = 0 and
   +0x0D 0x14 ((0x1F, 0), (0x20, 1) last). The ORIGINAL 0019C6F0 run with
   those pairs reproduces every uid word of every capture. The census agrees:
   0019C6F0's only a13c run is in a13c_03 (first frame 1731; the frame base is
   not asserted beyond that). A node spawned with the bit already set
   (+0x0D = REC[4], any count) gives (0x1F, 0), (0x20, 1); any other state,
   count or record raises.
2. **Knocked drums: an orphan proof** (`verify_directory`, the AREA13 lane's
   rules plus one). Hulls 7, 9 and 13 are moved in a13c_03 .. a13c_06 and no
   live node carries them; in a13c_02 each was a live drum 0x156620 at rest
   (state 1; nodes 0x7B0390, 0x7B0970, 0x7B1530). The drum's C
   (src/func_00156620.c, NEARMISS) re-transforms its hull with
   001A2370(node, node + 0xD0) every tick of its knocked state 2 and frees
   the node in state 3, so the hull keeps the last transform; the slots were
   reused, so the final matrix is not in RAM. The proof copies the a13c_02
   drum record into the capture's RAM, sets only its matrix's translation row
   to (centre x, min y, centre z) read from the capture's hull, runs the
   ORIGINAL 001A2370 and requires all 72 hull bytes equal and no other hull
   touched. Measured on the ORIGINAL (001C6380 then 001A2370 with +0xC0 =
   0.5, 1.0, ...): 001A2370 moves a drum box's centre (M * (0, 6.004, 0)) and
   keeps the half extents and every other word, and a tilted matrix with
   another translation gives the same bytes. So the proof shows that each
   hull is that drum's box, unchanged but moved; it does not recover where
   the drum stopped or its tilt, and since all drum boxes share their
   non-translation words it cannot tell which drum a position came from.
3. **[45] 0x827150's model after its sequence** (`explicit_model_nodes` /
   `explicit_model_problems`; func_overlay_AREA13_00827110.c, byte-identical).
   Its state 1 sub-state 3 sets the model 001CA6E0(self, 001C6120(table,
   0x12)) without changing +0x0D (0x11) and steps +5 to 4, where it stays.
   001CA6E0 (func_001CA5E0: +0x44 and the +0x4C handler) leaves the bone
   count, and 001C62C0 (bone_init_default_1.c) walks the old count, so +0x0C
   stays 3, model 0x11's count (model 0x12 has 1). Measured in a13c_03 ..
   a13c_06 (placement [45], node 0x7B23E0, +5 = 4); in every earlier capture
   it is still a generic owner.

The checker adds two `+0x0D` field rules (the AREA13 checker's FIELD_RULES,
installed in-process): [47]'s states with the bit set (above), and the
**hatch [62] 0x826850 spawned opened**: after the second load its state 0
finds its side's D_00810839 bit set, sets +0x0D = 0xD and goes to state 2
(func_overlay_AREA13_00826810.c), so the copied field is 0xD, not the
record's 0x2B; a hatch that opens in play keeps the record's value (the
AREA13 lane's finding 6). In state 2 with the side bit set either value is
accepted; otherwise the record's.

## Outputs

`assets/area13/reload/` (ignored, about 4.7 MB): `reload.json` (per file:
identical with its SHA-256, or differing with both SHA-256s and, for the
binaries, every differing word with both values) and the seven differing
files at their paths (`sub0/level/static_bank.emsc`,
`sub0/world_models.emwm`, the five reports). The full export lies in
`build/area13x/assets/tree/` until it is deleted (it is regenerated by the
exporters). Port loaders as in AREA13_ASSETS.md "Outputs".

## Tools

Pure Python on native arm64 macOS; run from the port root.

```sh
python3 tools/export_area13x_level.py    # level, textures, background, collision, cells, ctx (full tree)
python3 tools/export_area13x_tables.py   # roster, spawn, doors, scripts, overlay data, messages, world models
python3 tools/export_area13x_sfx.py      # the container and the registry
python3 tools/export_area13x_split.py    # what differs from assets/area13/ -> assets/area13/reload/
python3 tools/test_area13x_assets_reference.py                  # checker: a13b_00, a13c_02, a13c_03
EM_TEST_FULL=1 python3 tools/test_area13x_assets_reference.py   # all 13 captures
EM_AREA13X_ASSETS=<dir> python3 tools/test_area13x_assets_reference.py   # check another reload/ tree
```

Timings on the M1 (2026-10-01): level 12.6 s CPU, tables 0.1 s, sound 0.9
s, split under 1 s; checker quick 10.1 to 11.4 s CPU, full 19.0 to 19.7 s CPU
(fix round, 97 controls). The
first checker run builds the port loaders privately into
`build/area13x/assets/test/`. Missing exports, ELF, ISO, overlay, captures
or either census delta print SKIPPED.

## What is checked

The checker builds a view (symlinks: reload.json's identical files from
`assets/area13/`, its differing ones from `reload/`) and runs on it:

**The AREA13 lane's checks** (`test_area13_assets_reference.run_checks`, its
port loaders and its canary), re-pointed at the view and the 13 captures,
with this lane's pins (grid 0x17D39C0; placement nodes live / at their
record (62, 61) through a13c_02, (52, 50) in a13c_03 / a13c_04, (51, 49) in
a13c_05 / a13c_06; model owners bound 64, then 54 from a13c_03) and rules:
load map (every mapped byte = RAM, every relocation word = its formula),
level (static bank = RAM, 14,708 level kicks over the 13 captures, all of
the class-0 GS state, no dynamic kick; 2,743 texture decodes equal the GS
freezes; background.embg = the export of every capture), collision
(`area13.emcl` = the RAM grid rebuild in every capture), cells (every byte
of every directory derived: uid words by the ORIGINAL 0019C6F0, hulls by the
ORIGINAL 001A2370 / 0x219F50, orphans proven: 75 from a13c_04 on by the
AREA13 lane's rule, 7 / 9 / 13 by the knocked-drum proof), tables (roster,
spawn / door windows, chains, overlay-data run-time words, messages, world
models and every model binding), sound (bindings 1.0..1.2 global, 2.0,
4.0, 4.1 area; 3.0 and 4.2 refused, as in the a13 group; the registry = the
RAM re-derivation of every capture), ctx (the ORIGINAL 001D8FD0 then
001D1C50, room entry 32). 13 of 13 files load through the port loaders. The
canary plants 51 sections in a copy of the first capture and of the view;
each is reported for the copy and not for the original. The AREA13 lane's
`controls` are not run: they are pinned to the a13 group, and one of them
(D_008107F4 bit 0x40 set must be refused) is exactly the state this lane
models.

**This lane's checks** (`lane_checks`):
- captures: exactly the 13, with the area / sub / entry bytes above;
  a13b_05 the only excluded folder; level.json, cells.json, tables.json and
  banks.json made over all 13; `export_area13_level.PREVIOUS['area13']` is
  a19_02 and level.json's D_0028A5A4 record names a19_02 as the previous
  capture;
- roster pins: placement nodes live / at their record's position in each of
  the 13 captures, in every mode (the AREA13 lane's roster check only sees
  the selected captures);
- cursor: D_0028A73C = 0x133C1C0, D_0028A740 = it + 0x6E2800 and every
  relocation word = a13_04's + 0x1B80, in every capture; a13_04's cursor
  0x133A640; the bank bases D_0028A59C / D_0028A5A0 = 0x133A640 / 0x1472640
  in a13_04 and 0x133C1C0 / 0x14741C0 in every capture (the values the word
  rows below are pinned to);
- split: reload.json lists every file of `assets/area13/` (area19/ aside)
  exactly once; identical files are absent from reload/ and hash as
  recorded; the two binaries differ from the a13 group's only in the pinned
  words, each by +0x1B80, and reload.json's word rows equal rows pinned in
  the checker (static_bank +0x8 and +0xC 0x1472640 -> 0x14741C0,
  world_models +0x8 0x133A640 -> 0x133C1C0), not rows recomputed by the
  split tool's own `word_diffs`; the
  differing reports are exactly the five; reload/ holds nothing else;
- rules: cells.json's flag calls per capture = the [47] sequence above; its
  proofs = the rows the derivation recomputed in this run; knocked-drum
  proofs exactly for 7, 9, 13 in a13c_03 .. a13c_06, each from a13c_02's
  node; [45]'s rule applies exactly in a13c_03 .. a13c_06;
- census (`../Extermination/build/s87/census/a13b_delta.json`,
  `a13c_delta.json`): 001FFCD0 ran in a13b_00 and a13b_05 only and not in
  a13c; 0019C6F0 ran in a13b_00 only (the load) and in a13c only in a13c_03
  (first frame 1731); no other-overlay or unattributed a13c hit; the pass
  A13C over a13c_00 .. a13c_06 with no beat missing.

**Controls (97).** Captures: the last missing, an area byte, an entry byte,
three reports made over 12 captures, cells.json without a row. Cursor:
D_0028A73C, D_0028A740, the last relocation word, the a13 group's cursor.
Split: as exported (accepted); an identical file's hash; a static-bank byte
(and the same byte with reload.json updated to match); the world-model base
+ 0x10; a differing file missing; an extra file; an identical file copied
into reload/; one listed as differing; scene.txt copied and listed as a
differing report; the word list emptied. [47]: both captured pairs
(accepted); +0x28 = 0 with +0x0D 0x13, +0x0D 0x15, state 0, REC[0x2C] 0x14,
REC[4] 0x13 (each refused); a node spawned with the bit set ((0x1F, 0),
(0x20, 1), accepted) and a13c_02 read that way (the directory differs);
a13c_03 read as still counting down; a13b_00 under the lane's own rule
(accepted); the lane's verify_directory and this one equal on a13b_00
(accepted). Knocked drums: a13c_02 + a13c_03 (accepted); a13c_03 without a
donor; hull 7's extent, flag word, min x and centre x each changed; the
donor not at rest, not a drum, freed; the uid's last owner not a drum with a
drum before it; a13b_00 as the donor (accepted); the proof run with the
uid-9 drum for hull 7 (returns nothing). [45]: as captured (accepted); +5 =
3; bone count 1; +0x44 changed; a bone slot cleared; +0x0D 0x12 (left to the
generic rule, accepted). Field rules: a13b_00's hatch (accepted); hatch
+0x0D 0x22; 0xD without its D_00810839 bit; 0xD in state 1; a13c_02's [47]
(accepted); a13c_03's [47] 0x13 after the restore; a13c_02's 0x15. Proofs:
as exported (accepted); a13c_02 recorded with the restored pair; drum 13
recorded as a bare orphan; no recomputed rows; drum 13 as an ordinary orphan
in both the record and the rows; [45] before its sequence in a13c_03.
Census: as recorded (accepted); 001FFCD0 in a13c_02; 0019C6F0 in a13c_02,
first at 1732, in a13b_01; 001FFCD0 not in a13b_05; an unattributed a13c
hit; a beat missing; a beat dropped. `lane_checks` on a tampered tree and
with a tampered census. Fix round (`review_controls`; each one that must be
rejected names the text the rejecting check prints, so another check firing
does not count): `flag_calls` of a13c_03 with D_008107F4 = 0x64 (bit 0x40
alone: (0x1F, 0), (0x20, 1)) and of a13c_02 with +0x28 = 0x100 (a halfword
count: (0x1F, 1), (0x20, 0)); [45] in state 2 not an explicit node; a13b_00's
hatch with +0x0D set to its record's (opened in play) still an explicit
node with model 0xD and no problem; the directory of a13c_02 + a13c_04
(hull 75 the orphan equal to a13c_02's derivation, no problem; quick mode
has no a13c_04 otherwise); a13c_02 + a13c_03 + a later copy of a13c_02 whose
uid-7 node is no drum (no a13c_03 problem: the donor is searched among
earlier captures only); level.json naming another previous capture;
`PREVIOUS['area13']` not re-pointed; reload.json's word rows at offset + 4;
D_0028A5A0 changed; an extra excluded folder; tables.json in both lists;
level.json's sha256 zeroed; the drum donor a13b_00 in both record and rows;
an a13c hit in another overlay; census passes [A13C, X]; `lane_checks` with
a13b_02's entry byte flipped, with D_0028A73C changed, and with a13c_02's
flag pair changed in cells.json (its sha256 updated in reload.json); the
a13c_04 roster pinned (51, 50); a13c_04 with [47]'s node freed.

**Mutation sweep (one bounded sweep, this lane).** 39 single-operation
mutants over this lane's code: `export_area13x_common` (capture folders,
the [47] rule's branches and guards, the [45] rule's conditions and
checks, the knocked-drum proof's translation row, its min-y source, its
other-hull guard, donor order, donor state / behaviour guards, its call,
the installs, the cursor pin), `export_area13x_split` (the word diff, the
identity test, the reload/ skip) and the checker (each lane check, both
field rules, the grid pin, the census call). Each ran in a private copy of
the port root (real `tools/`, a private `assets/area13/` of symlinked files
with its own `reload/`, the loader dylibs, symlinks for the rest);
exporter mutants re-exported and re-split first, the others got a copy of
the real `reload/`; a survivor was re-run on a planted `reload/` (the static
bank's byte 0x20 flipped; the baseline fails it). `build/area13x/sweep/
sweep.py` (ignored), 4 workers, 2 min 9 s.
- First pass: 34 of 39 killed. Survivors: M13 (the other-hull guard of
  the drum proof removed: no capture input makes 001A2370 touch another
  hull), M28 (the pinned-word comparison removed: the recorded-word check
  caught the same control), M31 (the knocked-drum pin removed: the
  recomputed-rows check caught the same control), M36 (`lane_checks` without
  the census) and M39 (the differing-report pin removed). A control was
  added for each (the proof run with the uid-9 drum; a static-bank byte with
  reload.json updated; drum 13 as an ordinary orphan in both record and
  rows; `lane_checks` with a census hit; scene.txt as a differing report).
- Final: the five re-run once with the baseline (accepted): all killed. 39 of
  39 killed. The reused AREA13 comparators keep their own sweep record
  (AREA13_ASSETS.md); this sweep covers exactly the mutants above.

**Review sweep (round 11, independent, 40 mutants R01 .. R40).** It left 19
survivors in quick mode and 17 in full mode. The fix round added the pinned
controls above (no new sweep) and re-ran only the named survivors with the
reviewer's harness (copied to `build/area13x/fix/recheck.py`, ignored; the
baseline R00 passes; quick mode):
- killed by a pinned control: R01 (bit test 0x10), R02 (count read as a
  byte), R06 ([45] without its state test), R09 (the lane's hatch rule
  dropped), R14 (the orphan-equal path disabled; before, only full mode
  killed it), R15 (all captures as donor candidates; the later-copy control),
  R17 (`PREVIOUS` not re-pointed), R20 (word rows at k + 4), R26 (excluded
  check removed), R29 (the both-lists test removed), R30 (the reload/ sha256
  check dropped), R31 (donor-name check removed), R32 (other-overlay test
  dropped), R33 (passes test dropped), R35 / R36 / R37 (`lane_checks`
  without captures / cursor / rules), R38 (a13c_04's roster pin changed;
  before, only full mode killed it). R01 and R02 first stopped the run with
  an uncaught refusal; the controls now take the refusal as a value and
  print a failure.
- R21 (identity by the first hex digit of the sha256): equivalent on the
  committed export (none of the seven differing files shares its first digit
  with the a13 group's file), and whenever it changes the output it is
  caught: a differing file recorded as identical carries the new file's
  hash, which the identical-hash check compares with the a13 group's file.
  In the re-run it was killed, because the private root's re-export changes
  cells.json's embedded paths and so its sha256 (cells.json was recorded
  identical and the AREA13 lane's check found the later captures missing
  from it).
- Quick mode: the uid-75 orphan rule and the a13c_04 .. a13c_06 roster pins
  are now exercised by default (the a13c_02 + a13c_04 directory control and
  the 13-capture roster pin). run_checks itself still sees only a13b_00,
  a13c_02 and a13c_03 unless EM_TEST_FULL=1.

## Findings (for the lead)

1. **The second load changes no asset but two base words.** Over the 13
   captures every exported file equals the a13 group's except the static
   bank image's base / entry and the world-model table address, each +0x1B80.
2. **The south field (a13c) loads nothing**: no sub, no block, no loader run;
   it shares the a13b load map.
3. **The cursor**: D_0028A73C is set by 001FF830 state 5 (byte-matched C) to
   D_00275C74 + (+8 - +0x14) of a kind-0 bank (ids 2 / 3, D_00275C74 =
   *D_0028A738); D_0028A738 = 0x10E99C0 in each capture read for this
   (a04b_03, a04b_04, a13_04, a13_05, a19_02, a13b_00, a13b_05, a13c_06). The measured cursors climb 0x1338AC0 (a04b_03),
   0x133A640 (the a13 group, also in AREA19), 0x133C1C0 (a13b / a13c, also
   a13b_05 in AREA04). Neither INDEX.IDX sector 2's nor sector 3's span
   added to 0x10E99C0 gives one of them; what adds the rest is not traced.
4. **[47] restores its first pose in the south field**: the bit-0x40 states
   the AREA13 lane refused are reached (a13c_02 counting, a13c_03 restored);
   its uid pair flips to (0x1F, 0), (0x20, 1), matching the directory change
   ELEVENTH_LEVEL_ROUTE.md records (from a13c_03 on, entry 0's bit 30
   cleared and entry 1's set).
5. **The blast knocks three drums** (hulls 7, 9, 13): the first knocked drums
   in any capture (AREA13_ASSETS.md known gap 10); their nodes are freed by
   a13c_03 and their hulls keep the last transform (proved as drum boxes,
   above).
6. **[45]'s sequence binds model 0x12 without +0x0D or a new bone count**
   (rule 3); a port that rebinds through +0x0D or resets +0x0C from the
   model would differ.
7. **The hatch [62] spawns opened after the reload** with +0x0D = 0xD (its
   state 0), unlike the a13 group's in-play opening (finding 6 there).

## Binding

Nothing is wired. When AREA13 is bound, it is AREA13_ASSETS.md's binding with
one asset set for both loads: the files of `assets/area13/sub0/` and the side
files, except that the static bank image and the world-model table carry the
base the load lands on (0x133C1C0 / 0x14741C0 in the second load; the
`reload/` copies hold those). The cursor D_0028A73C must be the one the
original's streamer leaves (finding 3), not a constant. [47] must restore
its first pose when its count ends with the bit set (rule 1); a knocked drum
must free its node and leave its hull as last transformed (rule 2); [45]
must set model 0x12 without touching +0x0D or +0x0C (rule 3); the hatch must
take +0x0D = 0xD when it spawns opened.

## Known gaps

1. **The cursor's origin** (finding 3) is measured, not traced.
2. **The knocked drums' final poses** are not recoverable from RAM; the proof
   covers the hull words a pose does not decide (rule 2).
3. **[47] histories**: a node spawned with the bit set is modelled but occurs
   in no capture; a bit cleared after being set is not modelled.
4. **Door [20]'s north side and AREA19 entry 10** (ELEVENTH_LEVEL_ROUTE.md
   open items) are reached by no capture; nothing past a13c_06 is covered.
5. Everything listed for AREA13 in AREA13_ASSETS.md's known gaps other than
   gap 10 (knocked drums, now met) and gap 6's AREA13 part (bit 0x40, now
   modelled) still holds: undecoded ids, models not exported, SPU residency,
   the 26 unsupported sound ids, the relaxed collision header scan,
   undescribed side files.
6. **Descriptive side files** (`level.json`, `world_models.json`,
   `banks.json`, `scene.txt`, `.gsmat.json`) are checked only for their
   capture lists and their place in reload.json.
