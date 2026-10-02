# AREA19 assets: local export over every AREA19 capture

Lane "A19ASSETS", level side track (tenth level), 2026-10-01. This lane adds
new files only: three exporters, one shared module, one checker and this
document. It changes no port source, no tracked port file, no shared
exporter and no decomp file, and nothing it adds is wired into the game.
Every exported byte comes from the user's own disc files and ELF and is
written into the ignored `assets/area19/` tree. Nothing was run in PCSX2.

## Target and captures

AREA19 (area 0x13) **sub 0** is the only (area, sub) any recorded capture
loads in AREA19. Its four captures (`../Extermination/build/s87/`, ignored):

| Capture | Area bytes D_00810700..702 | Where |
|---|---|---|
| `route_a13/a13_05_shaft` | 13 00 09 | the arrival at the foot of the entry-9 ladder (NINTH_LEVEL_ROUTE.md) |
| `route_a19/a19_00_duct` | 13 00 08 | through the duct into the room behind door [32] (TENTH_LEVEL_ROUTE.md) |
| `route_a19/a19_01_pickup_g2` | 13 00 08 | after the Use of the pickup g[2] |
| `route_a19/a19_02_duct_back` | 13 00 09 | back through the duct at the ladder's foot |

Every other folder of the a13, a19 and a13b groups ends in another area
(AREA13, or AREA04 for a13b_05; a13b_00 starts in AREA19 but its RAM is
the AREA13 arrival) and is listed as excluded. Sub 1 (`chunk23.n1`, the
parts behind entries 0, 1 / 2 and 10) is loaded by no capture and is not
exported. Each capture's own sub is measured: `loaded_sub_proof.json`
records, per capture, 409 differing 16-byte rows for the sub-0 map (all in
the cell directory) against 186,960 for sub 1, identical in all four.

## Reuse: the AREA13 lane's AREA19 path, re-pointed

The AREA13 lane (AREA13_ASSETS.md) already exports AREA19 sub 0 for
a13_05 alone, including AREA19's load layout (a top block with a group-A
section and a resident offset, plus nested per-sub blocks;
`export_area13_common.build_load_map_both`, the resident-relative label
rule of the AREA06 / AREA22 lanes). This lane imports
`export_area13_common`, `export_area13_level`, `export_area13_tables`,
`export_area13_sfx` and `test_area13_assets_reference` **unchanged**, and
`export_area19_common.install()` re-points their AREA19 target:

- `export_area13_common.AREA19.out` -> `assets/area19/sub0`, and
  `export_area13_common.OUT` -> `assets/area19` (every exporter writes to
  `out_root / t.out.relative_to(OUT)`);
- `export_area13_common.capture_paths` -> the a13, a19 and a13b folders in
  route order; its `all_captures` keeps those whose area byte is 0x13 (and
  refuses one with another sub), which gives exactly the four above;
- `export_area13_common.SCRATCH` -> `build/area19/assets`.

The AREA13 target keeps its own paths; a process that imports
`export_area19_common` must not export or check AREA13 (the drivers never
do). Through the AREA13 lane's modules everything those modules reuse is
reused too (the AREA01 / AREA02 / AREA06 / AREA11 / AREA22 exporters and
checkers, the decomp's `export_level.py` and `export_collision.py`; the
list is in AREA13_ASSETS.md, "Reuse").

## Outputs

Under `assets/area19/sub0/`: `level/00_id44.emdl` (+ `.gsmat.json`),
`level/static_bank.emsc`, `level/level.json`, `area19.emcl` (+ `scene.txt`),
`area19_cells.bin`, `cells.json`, `roster.emro`, `message_data.emmd`,
`world_models.emwm` (+ `.json`), `sfx/area19_banks.bin`, `sfx/banks.json`,
`sfx/sfx_registry.emsr` (+ `.json`). Next to it in `assets/area19/`:
`spawn_table.emsp`, `door_destinations.emsp`, `scripts.emsc`,
`overlay_data.emsc`, `tables.json`, `loaded_sub_proof.json`. Port loaders as
in AREA13_ASSETS.md "Outputs" (there is no `background.embg`: no AREA19
capture arms the background). About 10 MB.

All 13 binary files are byte-identical to the AREA13 lane's a13_05-only
export (`assets/area13/area19/`): the three tenth-level captures change no
exported asset; they extend what each one is checked against.

## Tools

Pure Python on native arm64 macOS; run from the port root.

```sh
python3 tools/export_area19_level.py    # level, textures, collision, cells, ctx check, loaded_sub_proof.json
python3 tools/export_area19_tables.py   # roster, spawn, doors, scripts, overlay data, messages, world models
python3 tools/export_area19_sfx.py      # the container and the registry
python3 tools/test_area19_assets_reference.py                 # checker, all four captures
EM_AREA19_ASSETS=<dir> python3 tools/test_area19_assets_reference.py   # check another export tree
```

Missing exports, ELF, ISO, overlay or captures make the checker print
SKIPPED; once those exist, the a19 census delta
(`../Extermination/build/s87/census/a19_delta.json`, written by the decomp's
`tools/route_census.py` from the a19 captures) is required, and a missing
census fails.

Timings on the M1 (2026-10-01): level 3.4 s CPU, tables 0.1 s, sound 0.8 s;
checker 7.2 to 9.3 s CPU (8.3 to 11.1 s wall, by host load) with the loaders built. Its first run builds
the port loaders privately from `src/` into `build/area19/assets/test/`. The
default run already takes all four captures (inside the ~10 s budget), so
`EM_TEST_FULL=1` runs the same set.

## What is checked, over all four captures

**The AREA13 lane's AREA19 checks**, run by its checker's own `run_checks`,
`canary` and `controls` (AREA13_ASSETS.md describes each):

| Section | Result over a13_05, a19_00, a19_01, a19_02 |
|---|---|
| load map | one map from every capture's descriptor (pinned INDEX.IDX sector 23 hash); every row labelled by relocation id inside its range; every mapped byte equals RAM except the cell directory's; D_0028A73C 0x133A640, D_0028A740 0x1629180 in all four; id 0x45 not relocated, D_0028A5A4 = 0x1980000 (stale) in all four |
| level | static bank 0x1629180, 808 objects -> one zone EMDL (807 objects, 25,160 vertices, 15,916 triangles, 86 textures); 842 level kicks (60 / 274 / 260 / 248), 120 distinct REFs, all class-0 GS state, no dynamic kick; 344 texture decodes equal to the four GS freezes; no capture arms the background |
| collision | `area19.emcl` (3,032 vertices, 1,462 polygons) = the RAM grid rebuild in every capture; `--verify-ram` passes for all four |
| cells | `area19_cells.bin` = the disc bytes (57 uids, 36,456 bytes); every byte of every capture's directory derived: uid words by the ORIGINAL 0019C6F0 ([9] 0x825C70 in state 1: (0x21, 1) in all four), hulls 23..27 by the ORIGINAL 0x219F50, 36, 37 by the ORIGINAL 001A2370 from the creatures 0x827DD0, 47..52 from the pickups; the orphan 49 below |
| tables | roster (60 placements, groups 0x829E00 x43, 0x82A590 x1; (54, 53) live in all four), spawn rows 0x24CF10 x14 with D_008106C8 = +0x1C of record D_00810702, door row 0x24E0F0, the 18 chains of the committed C, overlay data equal to the module except the flame [11]'s 0x82B234 = 5 + y (-105.0, in all four), messages (96 area / 54 global records), world models (48, 901,072 bytes, 41 owners bound in all four) |
| sound | `area19_banks.bin` = nested block 0's table entry 0 (726,944 bytes, 4 banks); bindings 1.0..1.2 global, 2.0 and 4.0..4.2 area, 3.0 refused, identical in all four; the registry (508 audible, 475 absent, 17 unsupported, 75 samples) = the RAM re-derivation of each capture |
| ctx | the ORIGINAL 001D8FD0 then 001D1C50 rebuild ctx +0xA0..+0xFF from room entry 40 in all four |
| loaders | 11 of 11 files load; `area19_cells.bin` is refused (bit 29, AREA13_ASSETS.md finding 9), its bit-29-cleared copy loads |
| canary | 51 sections, each reporting its plant for a planted copy of a13_05 and silent for the original; every loader refuses a broken copy |
| controls | 65 changed inputs (the AREA19 branch of the AREA13 lane's controls) |

**This lane's checks** (`test_area19_assets_reference.py`, function
`lane_checks`):

- **Captures.** The AREA19 captures are exactly the four, with the area
  bytes above; the excluded list is the 13 other folders; `level.json`,
  `cells.json`, `tables.json` and `banks.json` were each made over all four.
- **Sub proof.** `loaded_sub_proof.json` equals
  `export_area13_common.loaded_sub_proof` of the run's captures (every
  capture's row, both subs' counts, no extra row), each capture's own sub
  fitting RAM over 100 times better.
- **Cell proofs.** Every capture's `proofs` in `cells.json` (which node and
  behaviour proves each moved hull, or that it is an orphan) equal the rows
  the AREA13 lane's `verify_directory` recomputed in the same run over the
  four captures (the ORIGINAL 001A2370 / 0x219F50 and the orphan rule).
  The rows are recorded by a wrapper around that function, installed only
  while the AREA13 lane's `run_checks` runs; the shared module is not
  edited.
- **The pickup g[2]** (group 0x829E00 record 2, a 00219550 pickup at
  (750.3, 200, 1051.8)). Its node (0x7A5C20) is live in a13_05 and a19_00
  and gone in a19_01 and a19_02 (TENTH_LEVEL_ROUTE.md: taken at a19_01
  f194); no other group record changes liveness. While it lives,
  `cells.json` proves its hull, uid 49, by the ORIGINAL 001A2370 from that
  node; once it is gone, hull 49 is the only orphan and is proved equal, byte
  for byte, to its derivation in a13_05 / a19_00 (the AREA13 lane's orphan
  rule, no written-words fallback).
- **The census** (`../Extermination/build/s87/census/a19_delta.json`, the
  a19 pass over a19_00 .. a19_02; required): no sub-1 owner of the AREA13
  lane's call-site census ran; of the call-site owners, only modelled ones
  ran (0x825C70, 0x827DD0, the pickups, the drums, 0x219870); no hit in
  another overlay and no unattributed hit; the loader 001FFCD0 did not run
  (one load across the four captures); the summary names the single pass
  A19 over exactly a19_00 .. a19_02, with no beat missing or incomplete and
  each of the three replays completed without error. 0019C6F0 and 0x219F50
  did not run in the a19 beats either: the uid words and the 0x219870 hulls
  the four captures share were set before a13_05's end.
- **Controls (61).**
  - Captures (9): the last capture missing; each capture with its sub byte
    D_00810701 and, separately, its entry byte D_00810702 changed.
  - `lane_checks` itself on a private copy of the report files (20): the
    copy as made (accepted); `level.json`, `cells.json`, `tables.json` and
    `sfx/banks.json` each made over a13_05 alone; each capture's sub-0 and
    sub-1 count in `loaded_sub_proof.json` raised by one (8); an extra row
    there; a19_02 hull 50 recorded as an orphan; a19_00 hull 47 recorded
    from another node; a13_05 hull 23 recorded as underived; a19_01 hull 49
    without a proof; no recomputed directory rows; the census missing.
  - `lane_checks` with g[0]'s node freed in a19_01's RAM (1).
  - The sub proof's rule (1): a capture read as sub 1.
  - The pickup (10): as captured (accepted); g[2] freed before the Use;
    g[2] live after it; g[0], g[1], g[3] and the last record g[42] each
    freed with it; the taken pickup's hull recorded as derived; the live
    pickup's hull recorded from another node; a second orphan.
  - The orphan proof (3): hull 49 without a deriving capture, with a13_05
    (accepted), and with its bytes changed.
  - The census (17): as recorded (accepted); each of the five sub-1 owners
    run; the callback 0x827B20 run (chain 0x82E090, started only by the
    sub-1 [34]); the loader run; a hit in another overlay; an unattributed
    hit; another pass (a beat dropped); a second pass name; a beat missing;
    a beat incomplete; a replay not completed; a replay error; a replay
    absent.

**Mutation sweep (one bounded sweep, this lane).** 28 single-operation
mutants over this lane's own code (`export_area19_common`: capture
folders, install, the pinned names; `export_area19_level.sub_proof`; the
checker's lane checks and its re-pointing of the AREA13 checker), each in a
private copy of the port root (a real copy of `tools/`, an empty `build/`,
symlinks for the rest; `build/area19/sweep/sweep.py`, ignored; 4 workers,
about 1 min 40 s). Exporter mutants re-export into a private tree that the
checker then reads. Each mutant runs the checker on the real tree (must
pass) and, if it passes, on a planted copy (the static bank's byte 0x20
flipped; must fail). The unmutated baseline passes the first and fails the
second.

- First pass: 4 survivors. M18 (the census's sub-1 check removed) survived
  because the sub-1 owners are also unmodelled site owners, so the
  "unmodelled" check caught the same control; the two checks now report
  disjoint sets (unmodelled excludes sub-1). M27 (the AREA13 checker not
  re-pointed at `EM_AREA19_ASSETS`) survived because the default tree is
  the same path; the planted-tree run kills it.
- Final: 26 of 28 killed (M04, the export tree moved to `sub1/`, by a crash
  on the missing files). Two survive, both equivalent: M06 (the exporter's
  sub-proof factor weakened to 1) and M08 (the exporter's pin of the
  four capture names removed). Both are refuse-only guards whose output is
  unchanged for every recorded input; the checker re-applies both rules
  itself (M12 and M11 remove those and are killed).

The reused AREA13 comparators keep their own sweep record
(AREA13_ASSETS.md); this sweep covers exactly the mutants above.

**Review round (2026-10-01).** The reviewer's own bounded sweep of the
checker's lane code (25 mutants) left 13 non-equivalent survivors, each with
a demonstrated killing input. The cause: the controls called
`capture_problems`, `sub_proof_problems` and `pickup_problems` directly with
edited inputs, no control ran `lane_checks` itself on a tampered tree (the
planted-tree run only flips a static-bank byte, which the AREA13 checks
catch), several branches had no control at all, and a missing census was
skipped. Fixes: `lane_checks` takes the tree, the census path and the
recomputed directory rows; the controls above run it on tampered copies;
one control per previously uncovered branch; the census is required; the
cell-proof check is new. No new sweep was run: the reviewer's named
mutants, adapted to the fixed code, and three removal mutants of the new
checks were re-run once each (`build/area19/fixkill/kill.py`, ignored,
4 workers, about 45 s); each must make the checker fail on the real tree.

| Mutant | Killed by |
|---|---|
| N01 `capture_problems` not called by `lane_checks` | `level.json` / `tables.json` made over a13_05 alone |
| N02 `sub_proof_problems` not called | `loaded_sub_proof.json` a13_05 sub 0 + 1 |
| N03 `pickup_problems` not called | `lane_checks` with g[0] freed in a19_01 |
| N04 unattributed hits ignored | an unattributed hit |
| N05 the pass name not compared | a second pass name |
| N06 the last sub-1 owner left out | the sub-1 owner 0x829A70 run |
| N07 the census path pointing at a missing file | the census is required: the real run fails |
| N08 / N09 `banks.json` / `tables.json` not compared | each made over a13_05 alone |
| N17 the sub proof over the first capture only | `loaded_sub_proof.json` a19_00 sub 0 + 1 |
| N31 the area bytes from the second capture on | a13_05 with its sub or entry byte changed |
| N35 only sub 0 compared | `loaded_sub_proof.json` a13_05 sub 1 + 1 |
| N37 g[0] and g[1] ignored | g[0] freed with g[2] |
| X01 the cell-proof check not called | a19_00 hull 47 from another node; a13_05 hull 23 underived |
| X02 the beats-missing / incomplete / replay test removed | a beat missing |
| X03 the proof's capture set not compared | an extra row in `loaded_sub_proof.json` |

Three survive and are equivalent (the reviewer classed them so too):

- **N10** (`cells.json` dropped from the report-coverage list). A
  `cells.json` made over fewer captures is caught by the cell-proof check
  (its rows must be exactly the recomputed four) and by the AREA13 lane's
  `cells_problems` ("not recorded in cells.json"); no input passes one and
  fails only this entry.
- **N39** (g[2] live where it should be gone no longer flagged by the
  liveness pin). In such a capture the live branch runs and sets the uid:
  if `cells.json` proves hull 49 from the node, hull 49 is not an orphan
  there while the orphan rule requires exactly [49], a problem; otherwise
  the live branch's proof comparison is a problem. Either way the mutant
  reports a problem wherever the original does.
- **N40** (the first group's address not compared with 0x829E00).
  On the real run `roster.emro` is compared byte for byte with the rebuild
  from the disc and with every capture's RAM records by the AREA13 lane's
  `roster_problems`, so its first group is the game's own (0x829E00 x43,
  the exporter's `GROUPS` pin); a file whose first group were another one
  fails there, and the four g[2] liveness pins fail on any other group's
  records.

## Findings (for the lead)

1. **The tenth-level captures change no AREA19 asset.** Over a19_00 ..
   a19_02 the load map, the cell directory derivation, the spawn rule, the
   overlay-data writers, the model bindings and the sound bindings of the
   AREA13 lane's a13_05 model all hold unchanged; every exported file is
   byte-identical to `assets/area13/area19/`.
2. **The pickup g[2]'s hull 49 becomes an orphan** after its Use (a19_01,
   a19_02) and is proved by the orphan rule. It is the first AREA19 orphan.
3. **Spawn entry 8 is indistinguishable by D_008106C8 alone.** In the duct
   D_00810702 goes 9 -> 8; records 5, 7, 8 and 9 carry the same +0x1C
   (0x28002; `tables.json` `spawn.record_words_1c`), so the AREA13 lane's
   spawn rule holds for both entries and cannot tell any of the four apart. This lane pins the entry byte per capture instead.
4. **The AREA13 reload in the a13b captures uses other cursors:**
   D_0028A73C = 0x133C1C0 and D_0028A740 = 0x1A1E9C0 in a13b_00 .. a13b_04
   and a13b_s0, against 0x133A640 and 0x1A1CE40 in the a13 group (both
   0x1B80 higher). a13b_05 (AREA04 after the lift) also has D_0028A73C =
   0x133C1C0. Not checked here (AREA13 is not this lane's target); the
   AREA13 export was checked only against the a13 group.

## Binding

Nothing is wired. When AREA19 is bound, the binding is AREA13_ASSETS.md's
"AREA19 from `area19/`" with this tree in its place: the scene selects
`assets/area19/sub0/` for the level, collision, cells, roster, world models,
message file and sound files, and the spawn table, doors, scripts and
overlay data next to it; the load reproduces the top + nested layout with
D_0028A490[id] for every listed id and keeps D_0028A5A4 stale; the cells
file loads once the loader keeps bit 29 out of the offset. The pickup g[2]
must free its node on its Use and leave hull 49 as last transformed.

## Known gaps

1. **AREA19 sub 1 and the parts behind entries 0, 1 / 2 and 10** (the
   panel [24], doors [22], [25], [27], the AREA03 / AREA15 exits) are loaded
   by no capture (TENTH_LEVEL_ROUTE.md section 7); nothing there is
   exported or checked.
2. Everything listed for AREA19 in AREA13_ASSETS.md's known gaps (the
   undecoded ids, models not exported, SPU residency, the 17 unsupported
   sound ids, the flag-call models failing closed outside [9]'s states 1 /
   2, the cursor gap measured not explained, no knocked drum) still holds.
3. **No capture holds a moved pickup or drum**; the g[2] pickup was taken
   (freed), not thrown.
4. **Spawn records 5, 7, 8 and 9** share their +0x1C word 0x28002
   (finding 3).
5. **Descriptive side files** (`scene.txt`, `.gsmat.json`,
   `world_models.json`, `sfx_registry.json`, `level.json` apart from its
   capture list) are not checked.
