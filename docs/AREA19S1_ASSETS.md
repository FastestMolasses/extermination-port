# AREA19 sub 1 assets over the thirteenth level's captures

Lane "A19S1", level side track (thirteenth level), 2026-10-02. This lane
adds new files only: four exporters, one shared module, one checker and
this document, and it rewrites one paragraph of AREA19X_ASSETS.md (its
review paragraph, which now points at this lane's controls). It changes no
port source, no tracked port file, no earlier exporter or checker and no
decomp file, and nothing it adds is wired into the game. Every exported
byte comes from the user's own disc files and ELF and is written into the
ignored `assets/area19s1/` (and the ignored scratch `build/area19s1/`).
Nothing was run in PCSX2.

## Target and captures

AREA19 (area 0x13) **sub 1** (nested block `chunk23.n1` of INDEX.IDX sector
23). Every earlier capture loads sub 0 (AREA19_ASSETS.md, AREA19X_ASSETS.md).
The thirteenth level went into sub 1 by the ladder at z 859.5
(THIRTEENTH_LEVEL_ROUTE.md: request 13 01 07 01 at a19c_06 f1460) and then
through door [52] to sub 1's entry 1. Its two captures that end in sub 1
(`../Extermination/build/s87/route_a19c/`, ignored):

| Capture | Area bytes D_00810700..702 | Where |
|---|---|---|
| `a19c_06_ladder959` | 13 01 07 | control at the top of the ladder, sub 1 entry 7 (y 380) |
| `a19c_07_door52` | 13 01 01 | entry 1 after door [52]; [34]'s script 0x82E090 has run |

`a19c_00` .. `a19c_05` end in AREA19 **sub 0** (13 00 0A, and 13 00 02 for
a19c_05), with the AREA19 module (MWo3 id 0x10) resident; they are listed
as excluded (checked by the checker; `export_area13_common.all_captures`
refuses an AREA19 capture of another sub than its target's, so they are
not handed to it). a19c_05 is the last capture before the sub-1 load.

**Each capture's sub is measured**: `loaded_sub_proof.json`
(`export_area13_common.loaded_sub_proof`) finds 1,382 (a19c_06) and 1,410
(a19c_07) differing 16-byte rows for the sub-1 map (the cell directory and
words written at run time) against 221,988 for sub 0.

**The sub load keeps the top block.** D_0028A73C = D_0028A59C = 0x133C1C0
(as in the second sub-0 load, AREA19X_ASSETS.md), and every top-list
relocation word D_0028A490[id] equals a19c_05's in both captures; the
nested block lands at D_0028A740 = D_0028A5A0 = 0x162A580 (sub 0 in
a19c_05: 0x162AD00). The grid D_0028A598 = 0x1879580, the cell directory
D_0028A5A8 = 0x1893D80 (both nested ids), D_0028A5A4 is still the stale
0x1980000 (also in a19c_05). The nested list relocates ids 0x44, 0x42,
0x46, 0x97 .. 0x9D (sub 0's: 0x44, 0x42, 0x46, 0x66, 0x67, 0x97).

## Reuse: the AREA13 lane's AREA19 path, re-pointed at sub 1

`export_area19s1_common.install()` imports `export_area19_common` (the
AREA19 lane's re-pointing of the AREA13 lane's AREA19 target) and
`export_area19x_common` (the AREA19X lane's, whose [43] rule acts on sub-0
captures only and so on neither capture here), then
`export_area13_level` and `export_area13_tables` (and through them every
module AREA13_ASSETS.md "Reuse" lists), all **unchanged**, and re-points:

- the target object `export_area13_common.AREA19`: `sub` -> 1 (read by the
  load map, the roster walk, the spawn rows, the sound container and the
  checks), `out` -> `build/area19s1/assets/tree/sub1`; `OUT` -> the tree,
  `SCRATCH` -> `build/area19s1/assets/scratch`, `capture_paths` -> the two
  sub-1 folders;
- `export_area13_level.PREVIOUS['area19']` -> a19c_05;
- `export_area13_tables`: the sub-1 roster pins (placements 0x82ED60, 57
  records; one group 0x82A5F0 x21), the sub-1 spawn rows (0x24D1B0 x9),
  read by the original's table walks (`export_area11_roster.walk_roster`,
  `export_area13_tables.spawn_rows`) over the pinned ELF and overlay;
- five sub-1 rules, each from the owner's committed C and checked against
  the captures (below).

`uninstall()` restores the AREA19X lane's sub-0 state (the checker uses it
for the AREA19X review controls).

### The sub-1 rules

1. **Cell-hull owners** (`owner_matrix`): [36] 0x826C10 (the lift,
   func_overlay_AREA19_00826BD0.c, NEARMISS) and [35] 0x827430
   (func_overlay_AREA19_008273F0.c, NEARMISS) call 001A2370 with the node
   + 0xD0 (state 0, and each height step). Their hulls 44 and 40 are
   derived by the ORIGINAL 001A2370 in both captures, as are the pickups'
   55 / 57 and the 0x219870 node's 47 (the ORIGINAL 0x219F50).
2. **The uid words** (`flag_calls`, the 0019C6F0 calls whose last effect
   the capture decides), by owner state from the C: [38] 0x826570
   (byte-identical) key 7: state 0 no call yet, state 1 (7, 1); [36] key
   0x15: state 0 none, states 4 and 1 (0x15, 1); [46] 0x829A70 (NEARMISS)
   key 5: states 0 and 1 none, state 2 (5, 0); [34] 0x8279E0
   (byte-identical) and its chain's callback 0x827B20 (byte-identical)
   key 8, always (8, 1): [34] live in state 0, or in state 1 with +5 0, made
   none; in state 3, or not live with flag 0x46 (D_0081079E) = 0xFF, (8, 1).
   The last rule reads chain 0x82E090 (checked by the checker from the
   module bytes): it has no jump record, its 0x827B20 record lies before its
   stop record, and the stop record is op 7 on flag 0x46. Any other state
   is refused (ValueError). Measured: a19c_06 (7, 1), (0x15, 1); a19c_07
   (7, 1), (0x15, 1), (8, 1). Outside sub 1 the AREA13 lane's rule runs.
3. **Freed drums** (`verify_directory` around the AREA13 lane's, and
   `freed_drum_problems`). The eleven 0x827B60 placements
   (func_overlay_AREA19_00827B20.c, byte-identical: [6]..[8], [20], [26],
   [28]..[33]) wait for D_0081081E == 1, set by chain 0x82E090's callback
   0x827B10, count +0x9A frames down and in their state 2 set their
   behaviour to the drum 0x156620 (a 001A2370 caller with node + 0xD0). In
   a19c_07 eight of them (uids 23, 29, 30, 32 .. 36) have moved their hulls
   and been freed; their slots are reused by 0x1E4CE0 / 0x1E4610 nodes, so
   no matrix is left to run 001A2370 with, and no earlier capture has those
   hulls moved. Such a hull is **accepted, not derived**, when the capture
   is in sub 1, its uid is a 0x827B60 placement's, no live node carries the
   uid, D_0081081E != 0, and every word that differs from the disc is a word
   the ORIGINAL 001A2370 writes for that hull
   (`export_area02_level.written_words`); its proof reads "freed 0x827B60
   drum: not derived". The checker pins the set. (Uids 9, 10, 11, the
   other three, are freed too but their hulls hold the disc bytes.)
4. **The fire's overlay-data words** (`writer_problems` around the AREA13
   lane's). The fire [39] 0x823D10 (func_overlay_AREA19_00823CD0.c,
   NEARMISS; its header says the stores are the original's) writes its
   smoke packets 0x82B000 / 0x82B090 every frame of states 1 / 2 (not at
   entries 2, 4, 5) while its size +0x214 is non-zero and D_008107F9 bit 7
   is clear: eleven words size x 15, 10, 8, 8, 8, 8, 40, 20, 20, 20, 30,
   two words 12.0 (the last loop pass) and 0x82B0A4 = 0. Accepted: the
   module bytes, or those words for size 1.0 with one live fire node in
   state 1 of size 1.0, the bit clear and another entry. Both captures hold
   them (13 words).
5. **0x1E3D90 holds no model** (`NO_MODEL`, added to
   `export_area02_tables.NO_MODEL_BEHAVIOURS`, the AREA00 rule): the fire /
   flash driver 0x1E3D90 (src/func_001E3D90.c, NEARMISS; its .s has no
   access to the node's +0x44) that group 0x82A9C0 spawns (from the
   callback 0x827B20). In a19c_07 its six nodes (slots 42, 43, 80, 81, 82,
   84) are skipped by the model-binding check; each slot's +0x44 equals the
   same slot's in a19c_06 (42 / 43: the 0x827B60 objects' model 0x2B
   binding; the others 0).

## Outputs

`assets/area19s1/` (ignored, about 9 MB), in `assets/area19/`'s layout:

- `sub1/`: `level/00_id44.emdl` (+ `.gsmat.json`), `level/static_bank.emsc`,
  `level/level.json`, `area19.emcl` (+ `scene.txt`), `area19_cells.bin`,
  `cells.json`, `roster.emro`, `message_data.emmd`, `world_models.emwm`
  (+ `.json`), `sfx/area19_banks.bin`, `sfx/banks.json`,
  `sfx/sfx_registry.emsr` (+ `.json`);
- `tables.json` and `loaded_sub_proof.json` (they describe the a19c
  captures);
- `sub1.json`: per `sub1/` file its SHA-256; the four side files
  (`spawn_table.emsp`, `door_destinations.emsp`, `scripts.emsc`,
  `overlay_data.emsc`) are byte-identical to `assets/area19/`'s and are
  recorded by hash, not copied; and which `sub1/` files equal a sub-0 file
  (`message_data.emmd` and `scene.txt` = `assets/area19/sub0/`'s,
  `world_models.emwm` = `assets/area19/reload/sub0/`'s: the model bank is
  the top block's, at the second load's base 0x133C1C0).

**Not `assets/area19/sub1/`** (the lane's brief named that folder): the
AREA19X lane's split and checker (`export_area19x_split.tree_files`,
`test_area19x_assets_reference.split_problems`) require every file of
`assets/area19/` outside `reload/` to be a sub-0 file its `reload.json`
lists, so a `sub1/` folder there makes both fail; this lane may not edit
them. Moving the folder needs `tree_files` to skip `sub1` as it skips
`reload` (one word in that lane's file) and the default `OUT` here changed.

Contents: level 984 bank objects -> one zone EMDL (983 objects, 12,724
vertices, 7,126 triangles, 59 textures); the grid `chunk23.n1/id42` (1,233
vertices, 644 polygons); the cell directory 64 uids, 76,648 bytes; the
roster 57 placements and one group of 21; doors used: [49] id 3, [50] id 6,
[51] id 7, [52] id 8 (door row 0x24E0F0 x10); 18 chains / 186 records;
96 area / 54 global message records; 48 world models (901,072 bytes); the
sound container (nested block 1 table entry 0, 714,448 bytes, 4 banks),
bindings as in sub 0 (group 3 bank 0 refused, no SShd magic), registry
1,000 ids: 485 audible, 506 originally absent, 9 unsupported, 66 samples.
Port loaders as in AREA13_ASSETS.md "Outputs".

## Tools

Pure Python on native arm64 macOS; run from the port root.

```sh
python3 tools/export_area19s1_level.py    # level, textures, collision, cells, ctx, sub proof (scratch tree)
python3 tools/export_area19s1_tables.py   # roster, spawn, doors, scripts, overlay data, messages, world models
python3 tools/export_area19s1_sfx.py      # the container and the registry
python3 tools/export_area19s1_split.py    # the scratch tree -> assets/area19s1/
python3 tools/test_area19s1_assets_reference.py                   # checker, both captures
EM_AREA19S1_ASSETS=<dir> python3 tools/test_area19s1_assets_reference.py   # check another area19s1/ tree
```

Timings on the M1 (2026-10-02, other lanes running): level 2.4 s CPU,
tables 0.1 s, sound 0.9 s, split under 1 s; checker 6.3 to 6.7 s CPU
(user + system; 8.5 s wall, 10 to 20 s while other lanes ran) with the loaders built. Its
first run builds the port loaders privately into
`build/area19s1/assets/test/`. The two captures are the whole set, so
`EM_TEST_FULL=1` runs the same checks. Missing exports, ELF, ISO, overlay,
captures or the census delta print SKIPPED.

## What is checked

The checker builds a view (symlinks: `sub1/` and the two reports from
`assets/area19s1/`, the four identical side files from `assets/area19/`)
and runs on it:

**The AREA13 lane's AREA19 checks** (`test_area13_assets_reference.
run_checks`, its port loaders and its canary), re-pointed at the view and
the two captures with this lane's rules installed, and with three of its
functions replaced by name for sub 1: `census_problems` (its sub-0 form
requires the sub-1 owners to be absent; here every jal / j / address word
of 001A2370, 0x219F50 and 0019C6F0 in the ELF and module is still its
SITES entry inside its function, the owners modelled are sub 1's plus the
pickup, drum and 0x219870 / 0x219F50, the rest are exactly [9] 0x825C70
and the creature 0x827DD0 with no live node and no sub-1 record, and
0x827B20 is started by [34] only), `field_problems` ([40] 0x823780,
func_overlay_AREA19_00823740.c, NEARMISS, sets +0x03 = 3 and +0x0D = 0 in
its state 0) and `canary_plants` (its creature plant needs the sub-0
creature; here [36]'s and [35]'s matrices). Pins: grid 0x1879580;
placement nodes live / at their record (52, 50) and (40, 38) (the flames
[40] and the fire [39] set their own position); model owners bound 48 and
37. Load map (every mapped byte = RAM, every relocation word = its
formula, labels), level (static bank = RAM; 704 + 1,060 level kicks, 484
distinct REFs, all of the class-0 GS state, no dynamic kick; 118 texture
decodes equal the GS freezes; no capture arms the background), collision
(`area19.emcl` = the RAM grid rebuild), cells (every byte of both
directories derived or accepted by rule 3; the uid words through the
ORIGINAL 0019C6F0 with rule 2's calls), tables (roster = the rebuild from
the disc, spawn rule D_008106C8 = +0x1C of the entry's record, doors,
chains, run-time words with rule 4, messages, world models and every model
binding with rule 5), sound (bindings, the registry = the RAM
re-derivation), ctx (the ORIGINAL 001D8FD0 then 001D1C50, room entry 41).
12 of 12 files load through the port loaders. The canary plants 52
sections; each is reported for the planted copy and not for the original.

**This lane's checks** (`lane_checks`):
- captures: exactly the two, area bytes as above; a19c_00 .. a19c_05 are
  AREA19 sub 0 with the module resident; the four reports made over both;
  `level.json` sub 1 and its previous capture a19c_05;
- cursor: the four words pinned; every top-list relocation word = a19c_05's;
  a19c_05 under the same top cursor with another nested cursor;
- split: `sub1.json` lists every side file once; the placed ones are
  exactly the two reports (and differ from `assets/area19/`'s); the
  identical ones hash as `assets/area19/`'s; every `sub1/` file hashes as
  recorded; nothing else under `assets/area19s1/`;
- sub proof: `loaded_sub_proof.json` = the recomputation, sub 1 over 100
  times better in both;
- rules: rule 2's calls pinned per capture; the 0x1E3D90 slots pinned and
  each one's +0x44 = a19c_06's same slot; cells.json's proofs = the rows
  the directory derivation recomputed in this run; the freed-drum hulls
  exactly 23, 29, 30, 32 .. 36 in a19c_07 and none in a19c_06; no orphan or
  underived hull;
- census (`../Extermination/build/s87/census/a19c_delta.json`): the pass
  A19C over the eight beats, replays complete; 001FFCD0 in a19c_06 only;
  sub 1's owners (0x826C10, 0x827430, 0x826570, 0x8279E0, 0x829A70,
  0x823D10, 0x827B60) in a19c_06 and a19c_07; the callbacks 0x827B10 and
  0x827B20 in a19c_07 only; [9] 0x825C70 and the creature 0x827DD0 not in
  a19c_07; the drum 0x156620 in a19c_07; no unattributed or other-overlay
  hit.

**AREA19X review controls (14)** (`area19x_controls`; AREA19X_ASSETS.md,
review): on a19b_02 with the AREA19X lane's sub-0 state (`uninstall()`),
tied to its exports (the [43] node of `reload/sub0/world_models.json`
`explicit_models`, the creature node and model record of
`reload/sub0/creature/creature.json`): as captured (accepted); in [43]'s
bones, one control per term of `bone_rest_problems`: +0x64 low and high
byte, scale x, y and z (high byte), the first zero word and the last zero
byte, the matrix's first and last byte (bone 3, so the 0x50 record stride
counts), a bone slot cleared; the creature's +0x0D 0x80 (without bit 0),
0x01 (without bit 0x80) and 0xFF (accepted: both bits set).

**Controls (126)** (`lane_controls`, `census_site_controls`). Captures: the
last missing; a19c_07's area, sub and entry byte; the sub-0 captures as
read (accepted), a19c_05 read as sub 1, a19c_00 missing; `PREVIOUS` not
re-pointed; each of the four reports made over one capture; level.json
naming another previous capture, and sub 0. Cursor: as captured
(accepted); each of the four words + 0x10; the last top-list word moved;
a19c_06 read as the capture before the load. Split: as exported
(accepted); a static-bank byte; a `sub1/` file missing; an extra file; an
identical side file copied in; a placed report missing; an identical side
file listed as placed; an identical side-file hash; a side file not
listed; tables.json listed as identical with sub 0's hash. Sub proof: as exported (accepted); each capture's sub0 count + 1; a
capture read as sub 0. Rules: as exported (accepted); [38] in state 2;
[36] in states 2 and 0; [46] in state 2; [34] with its chain running, in
state 3, freed without flag 0x46; a19c_07 with flag 0x46 = 1; exact call
lists ([36] in state 1, [38] in state 0, [46] in state 2, [34] in state 3,
[34] freed with flag 0x46 = 0xFF, a19c_06 read as sub 0; all accepted with
their values); flag calls with two [38] nodes refused; owner_matrix of
[36] in a capture read as sub 0 (none) and in sub 1 (node + 0xD0; both
accepted); a seventh 0x1E3D90 node whose +0x44 is a19c_06's; cells.json
naming another owner for hull 44; [40]'s fields as captured (accepted), +0x03, +0x0D and +0x54
changed; two [38] nodes; a 0x1E3D90 node with another +0x44; no recomputed
rows; cells.json without the freed hull 23, and record and rows without
it; a freed hull in a19c_06; an a19c_06 orphan. The freed-drum rule: hull
23 as captured (accepted); hull 40 (live); hull 23 in a19c_06; D_0081081E
= 0; outside sub 1; a word 001A2370 does not write changed; a live node
carrying uid 23; hull 9 (freed, not moved); hull 40 with [35] freed (not a
0x827B60 uid); the directory wrapper on that changed word. The fire: as
captured (accepted); each of its 13 words changed; a word between them;
the fire in state 2; size 0.5; D_008107F9 bit 7; entry 2; the fire freed;
the module bytes (accepted). Chain 0x82E090: as on the disc (accepted); a
jump bit; the stop on flag 0x45; the stop op 6; no 0x827B20 record; a jump bit to the next record.
Census: as recorded (accepted); 001FFCD0 also in a19c_07; [36] absent;
the fire also in a19c_05; 0x827B20 in a19c_06; [9] in a19c_07; the drum
not in a19c_07; an unattributed hit; an other-overlay hit; a second pass
name; a beat dropped; a beat missing; a replay error; a replay not
completed. `lane_checks` on an untouched copy (accepted), with an
unattributed census hit, with a sub proof count changed, with an orphan
23, with a cells byte (sub1.json stale). Call-site census: as captured
(accepted); a live node of [9]; a sub-1 placement of the creature; [46]
not modelled; the callback started by [38].

**Mutation sweep (one bounded sweep, this lane).** 73 single-operation
mutants: 63 over this lane's code (`export_area19s1_common`: the capture
list, the roster / spawn / group pins, `owner_matrix` and its sub guard,
each state entry of rule 2 and the [34] rule's tests, each test of
`freed_drum_problems` and the wrapper's orphan match, each term of the
fire's writer rule, its constants, `NO_MODEL`, each install line;
`export_area19s1_split`: the identity test both ways, the copy record, the
stale-file removal; the checker: each lane check, each census and chain
test, the [40] field rule, the grid pin, the census call, the replaced
census function) and 10 X mutants of the AREA19X lane's
`bone_rest_problems` (the +0x64 halfword as one byte, the scale as one or
two halfwords, the zero words short by four bytes or dropped, the matrix
short by four bytes, the +0x64 term dropped) and `creature_nodes` (mask
0x80, 1, or the whole byte == 0x81), applied to private copies only. Each
ran in a private copy of the port root (real `tools/`, a private
`assets/area19s1/`, the loader dylibs, symlinks for the rest); exporter
mutants started from a copy of the real tree plus a stale file and
re-exported and re-split; a survivor was re-run on a planted tree (the
static bank's byte 0x20 flipped; the baseline fails it).
`build/area19s1/sweep/sweep.py` (ignored), 4 workers.
- First pass: 67 of 73 killed (S35, the copy record removed, by a crash).
  Survivors: S04 (`owner_matrix` without its sub guard: no control fed a
  sub-0 capture), S12 (the two-node refusal removed: the control was caught
  by the call pin instead), S41 (the placed-set pin: the identical-hash
  check caught the same control), S46 (the 0x1E3D90 slot pin: the +0x44
  check caught the same control), S50 (cells.json proofs against the
  recomputed rows: every control changed both), S56 (the chain's jump-bit
  test: the planted jump left the window, so the walk failed first). A
  control was added for each (owner_matrix in a capture read as sub 0, and
  node + 0xD0; flag calls with two [38] nodes refused; tables.json listed
  as identical with sub 0's hash; a seventh 0x1E3D90 node whose +0x44 is
  a19c_06's; cells.json naming another owner for hull 44; a jump bit to the
  next record).
- Final: the six re-run once with the baseline (passes): all killed. 73 of
  73 killed, the 10 X mutants among them. The reused AREA13 comparators
  keep their own sweep record (AREA13_ASSETS.md); this sweep covers exactly
  the mutants above.
- Review (2026-10-02): an independent bounded sweep of 46 mutants inside
  the checker's guards left 17 survivors, 16 of them non-equivalent: many
  guard lines have no control of their own. The "73 of 73" above holds
  for this lane's own mutant set only; those 16 are recorded known gaps
  (build/r13review/A19S1/pass1.txt names them).

## Findings (for the lead)

1. **Entries 7 and 1 are sub-1 entries** (measured: D_00810701 = 1 and the
   sub proof). AREA19_ASSETS.md's target section places entries 0, 1 / 2
   (and 10, corrected by AREA19X_ASSETS.md) behind sub 1; entry 7 was not
   named.
2. **The sub change keeps the top block and reloads the module's data**:
   the top cursor and every top-list relocation word stay as in a19c_05,
   and the ten overlay-data words a19c_05 holds rewritten (the flame [11]'s
   0x82B234 and nine words of sub-0 chain records) hold the module bytes
   again in a19c_06 (001FFCD0 ran in a19c_06; the step that rewrites them
   was not traced).
3. **Eight drums are freed in a19c_07 with their hulls moved** (rule 3):
   the cell directory keeps a freed drum's last transform; a port that
   restores a freed owner's hull, or re-derives it, would differ.
4. **0x1E3D90 leaves +0x44 alone** (rule 5): its nodes keep the slot's
   previous model pointer (model 0x2B in slots 42 / 43). Whether a draw
   path reads it was not traced.
5. **The fire writes its smoke packets into the module's data** (rule 4),
   as the sub-0 flame [11] does at 0x82B234.

## Binding

Nothing is wired. When AREA19 sub 1 is bound, it is AREA19_ASSETS.md's
binding with sub 1's asset set: the files of `assets/area19s1/sub1/` and
the four side files of `assets/area19/`. The static bank image carries the
base the nested block lands on (0x162A580 in these captures) and the
world-model table the top block's (0x133C1C0, the second load's); the
cursors must be the ones the original's streamer leaves (AREA13X_ASSETS.md
finding 3), not constants. The sub-1 owners must reproduce rules 1 to 5:
[36] / [35] re-transform their hulls with node + 0xD0; the uid words take
keys 7, 0x15, 5 and 8 as rule 2 says; a 0x827B60 object turns into a drum
after its countdown and a freed drum's hull stays moved; the fire writes
its smoke words; 0x1E3D90 does not bind a model.

## Known gaps

1. **The freed-drum hulls are not derived** (rule 3): eight hulls of
   a19c_07 are accepted by a weaker rule (only words 001A2370 writes
   differ), not reproduced byte for byte; no capture holds their owners'
   last matrices.
2. **Untaken branches of rule 2**: [38] and [36] in state 2 are refused
   (their last call depends on history); [36] in state 1 and [46] in state
   2 are modelled from the C but no capture holds them (the valve [38] and
   the lift [36] were used only in exploration runs, which were not kept).
3. **NEARMISS sources**: rules 1, 2 ([36], [46]), 4, 5 and [40]'s fields
   read NEARMISS readable C; the captures confirm every value they hold,
   not the C's other paths.
4. **Sub 1 beyond a19c_07** (the stair tower, door [49] and its seal [48],
   the AREA15 doors [50] / [51], the valve [38], the lift [36]) is held by
   no capture (THIRTEENTH_LEVEL_ROUTE.md sections 6 and 7).
5. **Descriptive side files** (`level.json`, `world_models.json`,
   `banks.json`, `scene.txt`, `.gsmat.json`) are checked only for their
   capture lists and hashes.
6. The output folder is not the one the brief named (Outputs).
