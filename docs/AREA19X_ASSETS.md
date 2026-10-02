# AREA19 assets over the twelfth level's captures (the second load, entry 10)

Lane "A19X", level side track (twelfth level), 2026-10-01. This lane adds
new files only: five exporters, one checker and this document. It changes
no port source, no tracked port file, no earlier exporter or checker and no
decomp file, and nothing it adds is wired into the game. Every exported
byte comes from the user's own disc files and ELF and is written into the
ignored `assets/area19/reload/` (and the ignored scratch `build/area19x/`).
Nothing was run in PCSX2.

## Target and captures

AREA19 (area 0x13) **sub 0**, loaded a second time when the twelfth level
came down the hatch [63]'s ladder (TWELFTH_LEVEL_ROUTE.md: a19b_00, the
area change at f176). Its three captures (`../Extermination/build/s87/
route_a19b/`, ignored):

| Capture | Area bytes D_00810700..702 | Where |
|---|---|---|
| `a19b_00_arrival` | 13 00 0A | control at the foot of entry 10's ladder, after [7]'s first sequence |
| `a19b_01_ledge` | 13 00 0A | along the attribute-0x39 ledge, onto the platform at y 190 |
| `a19b_02_ladder1023` | 13 00 0A | up the ladder at z 974; [6]'s first stage, counter 0x1D 0 -> 3 |

The eight a13d folders (`route_a13d/`, AREA13) are listed and excluded.

**Entry 10 is a sub-0 entry** (measured). AREA19_ASSETS.md (target section
and known gap 1) placed "the parts behind entries 0, 1 / 2 and 10" behind
sub 1 (`chunk23.n1`). In all three captures D_00810701 = 0, the AREA19
module (MWo3 id 0x10) is resident, and `export_area13_common.
loaded_sub_proof` finds 362 differing 16-byte rows against the sub-0 map
(all in the cell directory) and 186,960 against sub 1. So the a19b captures
load the same sub-0 data as the first load; sub 1 is still loaded by no
capture.

**The second load sits 0x1B80 higher**, like AREA13's (AREA13X_ASSETS.md):
D_0028A73C = 0x133C1C0 (first load 0x133A640), D_0028A740 = D_0028A5A0 =
0x162AD00 (0x1629180), D_0028A59C = 0x133C1C0 (0x133A640), the grid
D_0028A598 = 0x1900500 (0x18FE980), and every relocation word D_0028A490[id]
of both descriptor lists is the first load's (a19_02's RAM) + 0x1B80 in
every capture. D_0028A5A4 is still the stale 0x1980000 (also in a13d_07, the
capture before the load).

## What the second load changes or adds

`export_area19x_split.py` compares the full export over the three captures
with the first load's export file by file (`assets/area19/reload/
reload.json`):

- **14 files are byte-identical** and are not written again: the zone EMDL
  and its `.gsmat.json`, `area19.emcl`, `scene.txt`, `area19_cells.bin`,
  `roster.emro`, `message_data.emmd`, the sound container,
  `sfx_registry.emsr` and its `.json`, and the four side files
  (`spawn_table.emsp`, `door_destinations.emsp`, `scripts.emsc`,
  `overlay_data.emsc`).
- **Two binaries differ only in their base address words**, each by
  +0x1B80: `sub0/level/static_bank.emsc` (+8 base and +0xC entry,
  0x1629180 -> 0x162AD00) and `sub0/world_models.emwm` (+8 table, 0x133A640
  -> 0x133C1C0).
- **Six reports** describe the a19b captures: `loaded_sub_proof.json`,
  `sub0/cells.json`, `sub0/level/level.json`, `sub0/sfx/banks.json`,
  `sub0/world_models.json`, `tables.json`.
- **Three files are new** (`sub0/creature/`): the resources the 0x82A590
  group's 0012E3A0 creature binds in a19b_02 (below). The first load's
  captures hold no such node; AREA13_ASSETS.md lists ids 0x70 and 0x71 as
  undecoded.

## Reuse: the AREA13 lane's AREA19 path, re-pointed again, plus two additions

`export_area19x_common.install()` imports `export_area19_common` first
(the AREA19 lane's re-pointing of the AREA13 lane's AREA19 target), then
`export_area13_level` and `export_area13_tables` (and through them every
module AREA13_ASSETS.md "Reuse" lists), all **unchanged**, and re-points the
AREA19 target once more: `AREA19.out` -> `build/area19x/assets/tree/sub0`,
`OUT` -> the tree, `SCRATCH` -> `build/area19x/assets/scratch`,
`capture_paths` -> the a13d and a19b folders,
`export_area13_level.PREVIOUS['area19']` -> a13d_07 (the capture before the
load). `export_area19_level.sub_proof` writes `loaded_sub_proof.json`. Two
additions, each from the owner's committed C and checked against the
captures:

1. **[43] 0x8255D0's model after counter 0x1D bit 0**
   (`swap_nodes` / `explicit_model_problems`;
   func_overlay_AREA19_00825590.c, byte-identical). In state 1, once
   D_008107F5 bit 0 is set, it sets func_001CA6E0(self, 001C6120(table,
   0x25)) and runs func_001C62C0 (bone_init_default_1.c) without changing
   +0x0D (0x1A, its record's), puts its child 0x1C5760 into state 3, clears
   +0x2EC and goes to state 2. 001CA6E0 (func_001CA5E0) stores +0x44 and
   leaves the bone count, so +0x0C stays model 0x1A's 4, and 001C62C0 walks
   those four bones over model 0x25's rest records although model 0x25 has
   two (finding 2). The rule: a node in state 2 whose +0x0D is not 0x25
   (the state-0 path with the bit already set writes +0x0D = 0x25 and is
   left to the generic rule) must hold +0x44 = model 0x25, +0x0C = the +8
   of model(+0x0D), every bone slot set, and in every one of those bones
   bone_init_default_1's words from model 0x25's records (+0x64 the record's
   +4 halfword, unit scale, zero words +0x70..+0x87, the record's 64-byte
   matrix). Measured in a19b_02 (counter 0x1D = 3, node 0x7B3E50; the
   child of a19b_01, 0x7BB6C0, is a free slot). Only an AREA19 sub-0
   capture is considered.
2. **The creature's resources** (`export_area19x_tables.export_creature`).
   0012E560 (src/func_0012E560.c, NEARMISS, readable C) binds the creature
   through 001B10B0(self, 0x70, 0x71) when +0x0D has bits 0 and 0x80;
   001B10B0 (byte-identical C) sets +0x44 = D_0028A490[0x70] through
   001CA6E0, +0x40 = D_0028A490[0x71], +0x0C = 001C6150(model). Measured in
   a19b_02: node 0x7BDCF0 (= D_008106C0), +0x0D 0x81, +0x44 0x15451C0 and
   +0x40 0x15719C0 (each id's relocated address), +0x0C 30, 30 bone slots.
   Each id's resident bytes are written as an EMSC address window
   (`em_script_image_load`'s format) from the load map (the disc files,
   `chunk23/id70`, `chunk23/id71`), equal to every capture's RAM. Id 0x70
   parses as a block model (`export_world_models.model_record`: 86 blocks,
   30 bones, 181,344 of its 182,272 bytes); id 0x71 (495,616 bytes) is
   exported as bytes, its format not decoded.

## Outputs

`assets/area19/reload/` (ignored, about 4.4 MB): `reload.json` (per file:
identical with its SHA-256, differing with both SHA-256s and, for the
binaries, every differing word with both values, or added with its SHA-256)
and the eight differing and three added files at their paths. The full
export lies in `build/area19x/assets/tree/` until it is deleted (the
exporters regenerate it). Port loaders as in AREA13_ASSETS.md "Outputs";
the two creature images load through `em_script_image_load`.

## Tools

Pure Python on native arm64 macOS; run from the port root.

```sh
python3 tools/export_area19x_level.py    # level, textures, collision, cells, ctx, sub proof (full tree)
python3 tools/export_area19x_tables.py   # roster, spawn, doors, scripts, overlay data, messages, world models, creature
python3 tools/export_area19x_sfx.py      # the container and the registry
python3 tools/export_area19x_split.py    # what differs from assets/area19/ -> assets/area19/reload/
python3 tools/test_area19x_assets_reference.py                  # checker, all three captures
EM_AREA19X_ASSETS=<dir> python3 tools/test_area19x_assets_reference.py   # check another reload/ tree
```

Timings on the M1 (2026-10-01): level 2.7 s CPU, tables 0.1 s, sound 0.8
s, split under 1 s; checker 6.0 to 6.7 s CPU (4.5 to 4.7 s user + 1.5 to
2.1 s system, 5.9 to 6.8 s wall) with the loaders built. Its first run builds the port loaders
privately into `build/area19x/assets/test/`. The three captures are the
whole set, so `EM_TEST_FULL=1` runs the same checks. Missing exports, ELF,
ISO, overlay, captures or the census delta print SKIPPED.

## What is checked

The checker builds a view (symlinks: reload.json's identical files from
`assets/area19/`, its differing and added ones from `reload/`) and runs on
it:

**The AREA13 lane's AREA19 checks** (`test_area13_assets_reference.
run_checks`, its port loaders and its canary), re-pointed at the view and
the three captures, with this lane's pins (grid 0x1900500; placement nodes
live / at their record (54, 53), (54, 53), (54, 51): in a19b_02 placements
6 (behaviour 0x8250F0) and 35 (0x1E7D20) have left their record's position
and rotation / height; model owners
bound 41, 41, 40, the swapped [43] counted by its rule and its child gone)
and [43]'s rule: load map (every mapped byte = RAM, every relocation word =
its formula), level (static bank = RAM; 779 level kicks, 194 / 239 / 346,
142 distinct REFs, all of the class-0 GS state, no dynamic kick; 258
texture decodes equal the GS freezes; no capture arms the background),
collision (`area19.emcl` = the RAM grid rebuild in every capture), cells
(every byte of every directory derived: the uid words by the ORIGINAL
0019C6F0 with [9]'s (0x21, 1), the hulls 23..27 by the ORIGINAL 0x219F50,
36 / 37 and the five pickups 47, 48, 50, 51, 52 by the ORIGINAL 001A2370;
no orphan), tables (roster, spawn rule D_008106C8 = +0x1C of record 10 =
0x10028000, doors, chains, run-time words, messages, world models and
every model binding), sound (bindings as in the first load, the registry =
the RAM re-derivation), ctx (the ORIGINAL 001D8FD0 then 001D1C50, room
entry 40). 13 of 13 files load through the port loaders: the AREA13
lane's 11 (the cells file refused for bit 29 and its cleared copy loading,
AREA13_ASSETS.md finding 9) and both creature images. The canary plants 51 sections; each
is reported for the planted copy and not for the original. The AREA13
lane's `controls` are not run: they are pinned to its a13_05 capture.

**This lane's checks** (`lane_checks`):
- captures: exactly the three, area bytes 13 00 0A; the eight a13d folders
  excluded; level.json, cells.json, tables.json, banks.json and
  creature.json made over all three; `PREVIOUS['area19']` is a13d_07 and
  level.json names it;
- cursor: D_0028A73C / D_0028A740 / D_0028A59C / D_0028A5A0 pinned in every
  capture and in a19_02; every relocation word of both lists a19_02's +
  0x1B80;
- split: reload.json lists every file of `assets/area19/` (reload/ aside)
  exactly once as identical or differing; identical files hash as recorded
  and are absent from reload/; the two binaries differ only in the pinned
  word rows (pinned in the checker, not recomputed by the split tool); the
  differing reports are the pinned six, the added files the pinned three;
  reload/ holds nothing else;
- sub proof: `loaded_sub_proof.json` = the recomputation, sub 0 over 100
  times better in every capture;
- rules: [43]'s rule holds exactly in a19b_02, with +0x2EC cleared;
  cells.json's proofs = the rows the directory derivation recomputed in this
  run; no orphan; the first group's records without a live node are
  exactly g[2], g[29], g[30] in all three captures and in a19_02 (g[2] is
  the pickup taken at a19_01: it is not spawned again in the second load),
  and hull 49 (g[2]'s) is not moved: the second load's directory holds its
  disc bytes;
- creature: creature.json and both images (header, length, hash, model
  record) against every capture's RAM and relocation words; the creature
  nodes pinned (a19b_02: 0x7BDCF0 = D_008106C0; none before) and bound as
  above;
- census (`../Extermination/build/s87/census/a19b_delta.json`): the pass
  A19B over the three beats, replays complete; 001FFCD0, 0019C6F0 and
  0x219F50 ran in a19b_00 only; no sub-1 owner
  and not the callback 0x827B20; of the call-site owners only modelled ones
  ran; no unattributed hit; the only other-overlay hits are the four AREA13
  hits at a19b_00 frame 1 (TWELFTH_LEVEL_ROUTE.md section 5, measured
  gaps).

**Controls (86).** Captures: the last missing; a19b_02's area, sub and
entry byte; an extra excluded folder; `PREVIOUS` not re-pointed; each of
the five reports made over two captures; level.json naming another
previous capture. Cursor: as captured (accepted); each of the four words
+0x10; the last nested relocation word; the first load read with the
second cursor. Split: as exported (accepted); a static-bank byte, alone and
with reload.json updated; a differing file missing; an extra file; an
identical file copied into reload/; scene.txt listed as added; an identical
hash; word rows at offset + 4; an added file missing; an extra file listed
as added. Sub proof: as exported (accepted); each capture's sub-1 count +1;
a capture read as sub 1. Rules: as exported (accepted); [43] back in state
1; its +0x44 another model; its bone count reset to model 0x25's; bone 2
not model 0x25's third record; a bone slot cleared; still holding its
child; spawned with the bit set (+0x0D 0x25, accepted); outside AREA19
(accepted: no node); no recomputed rows; hull 49 an orphan in the record,
and in both record and rows; hull 49 derived in both; hull 47 an orphan in
both; a hull recorded as underived; g[0] freed; g[2] live (its a13_05 node
copied into a free slot). Creature: as exported (accepted); +0x44 and +0x40
moved; bone count 29; +0x0D without bit 0x80; D_008106C0 cleared, alone and
with the creature unbound; an id 0x70 byte changed in RAM; creature.json
with 29 bones, without the a19b_02 node, with another id 0x71 hash; id70.emsc
with a byte changed (reload.json updated). Census: as recorded (accepted);
each of the five sub-1 owners and the callback 0x827B20 run; 001FFCD0 also
in a19b_02; 0019C6F0 absent; 0x219F50 in a19b_01; an unattributed hit; a
fifth other-overlay hit; one at frame 2; a second pass name; a beat
dropped; a beat missing; a replay error; a replay not completed.
`lane_checks` on an untouched copy (accepted), with an unattributed census
hit, with a19b_01's sub-0 count 1, with an orphan 49.

**Mutation sweep (one bounded sweep, this lane).** 49 single-operation
mutants over this lane's code: `export_area19x_common` (capture folders,
[43]'s node test, state and area guard, each of its model / bone-count /
rest-record checks, the record stride and walk, the installs, the cursor
pin, the list merge), `export_area19x_tables` (the creature's +0x0D mask,
its +0x44 / +0x40 / bone checks, the RAM guard, the id list, the model id),
`export_area19x_split` (the word diff, the identity test, the reload/
skip, the added rows) and the checker (each lane check, the grid pin, the
census call). Each ran in a private copy of the port root (real `tools/`,
a private `assets/area19/` of symlinked files with its own `reload/`, the
loader dylibs, symlinks for the rest); exporter mutants re-exported and
re-split first, the others got a copy of the real `reload/`; a survivor was
re-run on a planted `reload/` (the static bank's byte 0x20 flipped; the
baseline fails it). `build/area19x/sweep/sweep.py` (ignored), 4 workers,
1 min 30 s.
- First pass: 40 of 49 killed. Survivors: M14 (the merge's duplicate
  filter removed), M18 (the exporter's RAM guard on the creature ids
  removed), M31 (the added-set pin removed: the present-file check caught
  the same control), M34 (the +0x2EC check: no control), M36 (the hull-49
  check: the orphan / proof checks caught the same controls), M38 (the
  creature-node pin: the D_008106C0 check caught the same control), M42
  (`lane_checks` without the census: the census was tested only directly),
  M43 (the image hash check: the RAM check caught the same control), M46
  (the orphan check: the hull-49 check caught the same controls). A control
  was added for each of the seven non-equivalent ones (an extra file listed
  as added; [43] still holding its child; hull 49 derived in both record and
  rows; the creature unbound with D_008106C0 cleared, and creature.json
  without its node; `lane_checks` with an unattributed census hit;
  creature.json with another id 0x71 hash; hull 47 an orphan in both).
  M14's filter was removed from the code (the two lists hold nodes of
  different behaviours, so it never removed anything).
- Final: the seven re-run once with the baseline (passes): all killed. 47
  of 48 killed; M18 survives and is equivalent: a refuse-only guard whose
  output is unchanged for every recorded input, re-applied by the checker
  (`creature_file_problems` compares each image with every capture's RAM;
  the "id 0x70 byte changed in RAM" control exercises it). The reused AREA13 comparators keep their own sweep record
  (AREA13_ASSETS.md); this sweep covers exactly the mutants above.
- Review (2026-10-02): that sweep removed whole checks only. An
  independent sweep of 43 single-operation mutants inside the checks left
  18 non-equivalent survivors; most important, each per-field term of the
  [43] rule (bone +0x64, scale, zero words, matrix length) and
  creature_nodes' bit 0 has no control of its own. These are known gaps
  (one control per field would close them); the claim above covers whole
  checks, not each field.

## Findings (for the lead)

1. **Entry 10 is in sub 0** (measured above): the a19b captures load no
   sub, block or file the first load did not; AREA19_ASSETS.md's target
   section and known gap 1 group entry 10 with sub 1. Sub 1 (behind entries
   0, 1 / 2, unmeasured) is still loaded by no capture.
2. **[43]'s model swap keeps the old bone count** (rule 1): 001C62C0 puts
   four bones from model 0x25's rest records, but model 0x25 has two
   (+8 = 2, 25,184 bytes). Bones 2 and 3 take the 0x50-byte records at
   +0x6260 and +0x62B0 from model 0x25's start: 0x20 bytes past its end,
   then the first 0x80 bytes of the next table entry, model 0x26 at +0x6280.
   All four bone nodes of a19b_02 hold exactly those bytes. A port that
   rebinds the bone count from the new model, or bounds the walk by it,
   would differ (an engine quirk; CURIOSITIES.md is a decomp file this lane
   does not edit).
3. **The second load changes no asset but two base words** and re-reads the
   directory: the hull of the pickup taken in the first load (49) holds its
   disc bytes, and g[2] is not spawned again (how the spawn skips it is not
   traced here).
4. **The creature binds ids 0x70 / 0x71**, two of the ids AREA13_ASSETS.md
   lists as undecoded: 0x70 is a 30-bone block model, 0x71 the creature's
   +0x40 resource (format not decoded).
5. **Spawn record 10 is new but indistinguishable**: D_008106C8 =
   0x10028000, the +0x1C word of records 2, 3, 6 and 10 .. 13 alike
   (`tables.json` `spawn.record_words_1c`); the checks pin the entry byte.

## Binding

Nothing is wired. When AREA19 is bound, it is AREA19_ASSETS.md's binding
with one asset set for both loads: the files of `assets/area19/sub0/` and
the side files, except that the static bank image and the world-model table
carry the base the load lands on (0x162AD00 / 0x133C1C0 in the second load;
the `reload/` copies hold those). The cursor D_0028A73C must be the one the
original's streamer leaves (AREA13X_ASSETS.md finding 3), not a constant.
[43] must set model 0x25 without touching +0x0D or +0x0C and run the bone
reset over the old count (rule 1, finding 2); its child must leave. The
0012E3A0 creature binds `creature/id70.emsc` as its model and
`creature/id71.emsc` as its +0x40 resource (by D_0028A490, i.e. at the
load's addresses). A taken pickup is not respawned by the reload and its
hull keeps its disc bytes.

## Known gaps

1. **Sub 1** (entries 0, 1 / 2: the panel [24], doors [22], [25], the
   AREA03 / AREA15 exits) is loaded by no capture.
2. **[7]'s room / its Use (0x825930, door [27]) and [6]'s second stage
   (0x825420)** are reached by no capture (TWELFTH_LEVEL_ROUTE.md section
   7); nothing past a19b_02 is covered.
3. **Id 0x71's format** is not decoded; 0012E560's other model ids are
   not covered: 0x6E (in AREA19's top list) is bound by no captured node
   and not exported; 0x72 and 0x74 are not in AREA19's lists.
4. **How the reload skips g[2]** (finding 3) and the cursor's origin are
   measured, not traced.
5. Everything listed for AREA19 in AREA19_ASSETS.md's and AREA13_ASSETS.md's
   known gaps other than this document's corrections still holds.
6. **Descriptive side files** (`level.json`, `world_models.json`,
   `banks.json`, `scene.txt`, `.gsmat.json`) are checked only for their
   capture lists and their place in reload.json.
