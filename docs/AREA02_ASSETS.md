# AREA02 assets: local export and capture checks

Lane "A02ASSETS", level-3/4 side track, 2026-09-28. This lane adds new files
only: four exporters, one checker and this document. It changes no port
source, no tracked port file and no decomp file, and nothing it adds is wired
into the game. Every exported byte comes from the user's own disc files and
ELF and is written into the ignored `assets/area02/` tree. Every export is
checked against the recorded original captures of AREA02
(FOURTH_LEVEL_ROUTE.md). Nothing was run in PCSX2.

The captures (7 in all, every one with RAM, scratchpad and GS freeze):
- **sub 1**: the AREA02 arrival, the end of beat a01r_03
  (`../Extermination/build/s87/route_a01r/a01r_03_door16/`), and a02_s0 (the
  bed, from the arrival);
- **sub 0**: a02_00 .. a02_04 (`build/s87/route_a02/`), after the duct's sub
  change.

a02_05 ends in AREA04 and is excluded by its area byte.

## Two sub-states, and nothing shared below the top block

The captures split by D_00810701. Each one's whole resident load map equals
RAM only for its own nested block (`export_area02_common.loaded_sub_proof`,
written to `assets/area02/loaded_sub_proof.json`):

| Capture | Own map: differing 16-byte rows | Other sub's map |
|---|---:|---:|
| a01r_03, a02_s0 (sub 1) | 0 | 231,244 / 231,255 |
| a02_00 .. a02_04 (sub 0) | 47 .. 284, all in the cell directory | 108,048 |

Unlike AREA00, the two nested blocks (`chunk06.n0`, `chunk06.n1`) are
different blocks loaded at different cursors: D_0028A740 is 0x13DB640 in sub
1 and 0x13DCA00 in sub 0. The duct (a02_00) reloads the nested block without
reloading the overlay. The grid, the cell directory, the static bank, the
model bank, the area message bank and the sound container all lie in the
nested block, so every level file is per sub. Only the top block
(`chunk06`, 4 files at 0x1335F40) and the overlay are shared.

## Outputs

The exports use the formats the port already reads for AREA11, AREA01 and
AREA00:

| Output (`assets/area02/`) | What | Port loader |
|---|---|---|
| `sub<N>/level/NN_<file>.emdl` (+ `.gsmat.json`, descriptive) | level geometry, texels, GS state codes | `em_model_load` |
| `sub<N>/level/static_bank.emsc` | the static bank `*D_0028A5A0` | `em_script_image_load` |
| `sub<N>/area02.emcl` (+ `scene.txt` from export_collision.py) | grid collision with node class, rank and axis sections | `em_collision_load` |
| `sub<N>/area02_cells.bin` | the cell directory `*0x70003250` (disc rest state) | `em_actor_cells_load`, which **refuses** sub 0's (finding 3) |
| `sub<N>/roster.emro` | placements and deferred groups of the sub | `em_actor_roster_load` |
| `sub0/message_data.emmd` | EMMD v1 for area 2: 54 global and 4 area records, 36 stream rows, both banks | `em_message_live_install` |
| `sub<N>/world_models.emwm` + `.json` | the model bank `*D_0028A59C` of the sub | `em_world_models_parse` |
| `sub<N>/sfx/area02_banks.bin`, `banks.json` | the sub's SShd container | (the registry's source) |
| `sub<N>/sfx/sfx_registry.emsr` + `.json` | EMSR v2 for scope (2, N) | `em_sfx_registry_load` |
| `spawn_table.emsp` | the global spawn export; its area 2 rows are 0x24B560 (sub 0) and 0x24B6B0 (sub 1), 7 records each | `em_spawn_table_load` / `_read` |
| `door_destinations.emsp` | EMSP windows: `D_0024E140[0..0x17)` and the area 2 row 0x24DFC0 (4 records) | `em_spawn_table_load` / `_read` |
| `area02_scripts/scripts.emsc` | the script window 0x826780..0x829190 (12 chains, 75 records) | `em_script_image_load` |
| `overlay_data.emsc` | the overlay data section 0x825680..0x829280 | `em_script_image_load` |
| `sub<N>/level/level.json`, `sub<N>/cells.json`, `tables.json`, `loaded_sub_proof.json` | counts, hashes and the verification summary (no text, no data) | none |

There is no `dynamic_objects.emsc`, no `background.embg` and no
`sub1/message_data.emmd`, for the reasons under "Level geometry",
"Background and fog" and "Messages" below.

The checker does not check the descriptive side files: `scene.txt`, the
`.gsmat.json` files, `world_models.json`, `sfx_registry.json`, `banks.json`,
`level.json`. It does compare the recorded lists in `cells.json` (the moved
hulls and the 0019C6F0 calls per capture, and `moved_uids` against its own
rows) and in `tables.json` (the run-time words of each capture) with its own
results.

## Tools

All the tools are pure Python and run on native arm64 macOS. Run them from
the port root.

```sh
python3 tools/export_area02_level.py    # both subs: level, textures, materials, collision, cells (~7 s CPU)
python3 tools/export_area02_tables.py   # rosters, spawn, doors, scripts, overlay data, messages, world models (<1 s)
python3 tools/export_area02_sfx.py      # one container and one registry per sub (~1 s)
python3 tools/test_area02_assets_reference.py                  # checker, 3 captures
EM_TEST_FULL=1 python3 tools/test_area02_assets_reference.py   # all 7 captures
EM_AREA02_ASSETS=<dir> python3 tools/test_area02_assets_reference.py   # check another export tree
```

Checker timings on the M1 (2026-09-28, other lanes holding the load average
between 60 and 86): quick 5.3 to 6.4 s CPU (5.8 to 10.7 s wall), full 6.7 to
8.1 s CPU (7.6 to 12.1 s wall). The first run after any `src/` header change
adds about 2 s to rebuild the loader library.

**Reuse.** The AREA01 exporters and checker are reused by import, as
AREA00_ASSETS.md describes; none of them is edited.
`export_area02_common.configure(sub)` sets the attributes of
`export_area01_common` for AREA02 (area 2, the sub, AREA02.BIN = MWo3 id 3,
0x5D80 bytes, pinned by SHA-256; the output paths and the capture list).
From there these run unchanged:
- the load map, `LoadedImage`, `compare_load_map`;
- from `export_area01_level`: the bank walk, the kick check (with an empty
  dynamic list), the zone builder and writer, `derive_cell_image`,
  `check_background`, `check_ctx_room_block`;
- from `export_area01_tables`: `export_doors`, `export_world_models`,
  `export_messages`, `walk_chain`, `match_nodes`, the spawn-field helpers;
- from `export_area01_sfx`: `bindings_from_captures`;
- from the AREA01 checker: `zone_problems`, `emcl_equal`, `grid_problems`,
  `roster_image`, `emsp_problems`, `messages_problems`,
  `world_models_problems`, `registry_from_ram` and the other comparators
  the checker's docstring lists;
- through those: `export_level.py` and `export_collision.py` (decomp),
  `export_area11_roster`, `export_spawn_table`, `export_message_data`,
  `export_world_models`, `export_sfx_registry`.

Replaced module attributes: `export_area01_level.owner_matrix` (by
AREA02's, for the whole process, so a process must not import the AREA00
or AREA01 level exporters together with AREA02's), and for one call each
`export_area01_tables.pool_nodes` and `AREA_BANK`; the AREA01 checker's
`GRID`, `BUILD` and `OVERLAY_SHA256`.

AREA02's own code, and why:
- `run_collision` hands export_collision.py only the grid section's
  resident bytes: the grid file (`n<N>/f02_id44`) is partly resident, and
  it also holds the cell directory, which export_collision's cell-list scan
  takes for a cell world (finding 7);
- `verify_directory`, `flag_calls`, `derive_words`, `orphan_problems`,
  `written_words` (the uid words and the orphaned hull);
- `trigger_words`, `run_time_words`, `car_x` (the trigger table);
- `area_container`, `block_bytes` (a container longer than its first file);
- `bank_source` (a message bank run from the resident image).

## How each asset is derived and checked

**Load map.** The rule is AREA01_ASSETS.md "Load map", with INDEX.IDX sector
6 read from the disc image (in a02_04 and a02_s0 the descriptor buffer holds
a later sector). Top block `chunk06`, 4 files, at 0x1335F40; nested block:

| Sub | Nested cursor | Resident from | Resident files | Map bytes |
|---|---|---|---|---|
| 0 | 0x13DCA00 | +0x14B000 | f02_id44 (from +0x10C800), f03_id43, f04_id72, f05_id41 | 4,380,672 |
| 1 | 0x13DB640 | +0x82000 | f02_id44 (from +0x79000), f03_id43 | 2,408,448 |

Every mapped byte equals RAM in every capture of the sub, except the bytes
of the cell directory, which the directory check covers. The checker also
requires the other sub's nested files not to fit RAM, and a single one of
them planted in RAM not to count as the other load (a control).

**Level geometry.**
- The static bank lies whole in the nested block: sub 0 at 0x141B200, 748
  objects, extent ..0x1604BE0; sub 1 at 0x13E4640, 646 objects, extent
  ..0x15576A0. The grid at render ctx +0x140 names exactly objects 1..N-1,
  and every record has matrix slot 0. The objects' units lie in several
  mapped files, so each sub has one zone EMDL per file:

  | Zone | Objects | Vertices | Triangles | Textures |
  |---|---:|---:|---:|---:|
  | sub0/00_f02_id44 | 249 | 3,895 | 2,261 | 40 |
  | sub0/01_f03_id43 | 479 | 8,441 | 4,929 | 84 |
  | sub0/02_f04_id72 | 19 | 320 | 178 | 18 |
  | sub1/00_f02_id44 | 414 | 2,960 | 2,042 | 23 |
  | sub1/01_f03_id43 | 231 | 1,450 | 946 | 55 |

- Every textured level-kernel kick REFs units inside a bank object: 2,528
  kicks in the 5 sub-0 captures, 526 in the 2 sub-1 captures, all with the
  class-0 GS state (PRIM 0x3C, TEST_1 0x5000D, ALPHA_1 0x80000000A8, TEX1_1
  0x60, CLAMP_1 0).
- **No dynamic list.** D_0028A5A4 is 0x177A940 in every capture, AREA01's
  list address, outside both load maps (the exporter stops if it lies in the
  map). The kick check runs with an empty list, and none of the 7 captures
  draws a kernel-0x00237450 kick.
- The texels come from the level-load GS upload of the sub's nested section
  1, replayed from the disc image. They equal the GS-freeze decode of every
  capture: 710 comparisons in sub 0, 156 in sub 1.
- The checker requires each zone file to equal a rebuild from the exported
  bank byte for byte (AREA01's `zone_problems`) and compares the bank file
  with RAM over its full length (`bank_end`).

**Collision.**
- The grid is D_0028A598: 0x13DCA00 in sub 0 (`n0/f02_id44` + 0x10C800) and
  0x13DB640 in sub 1 (`n1/f02_id44` + 0x79000), each the first resident
  byte of its nested block.
- `export_collision.py --node-class` runs over the grid section alone
  (header to the end of its last pool), and `--verify-ram` passes for every
  capture of the sub. Sub 0: 1,007 grid nodes, 1,587 EMCL vertices, 3,870
  indices (20 warped quads, reported by the tool, not failures). Sub 1: 121
  nodes, 321 vertices, 484 indices. No cell n-gons.
- The checker rebuilds the whole EMCL from each capture's RAM grid
  (`emcl_from_ram`) and requires the file to equal it. It pins D_0028A598 per
  sub and requires the grid block to lie in the load map, outside the
  directory allowance. The code that fills the pointer at load is not
  identified, as for AREA01 and AREA00.

**Cell directory.** Sub 0: 0x1405200, 54 uids, 89,652 bytes; sub 1:
0x13E0E40, 18 uids, 13,616 bytes. Each `sub<N>/area02_cells.bin` holds the
disc bytes and must equal the disc over its length and end where the
directory walk ends. Every byte of every capture's RAM directory is then
derived:
- **Hulls re-transformed by a live owner.** The ORIGINAL 001A2370 runs in the
  EE interpreter with the matrix each owner's code passes, read from the
  committed byte-identical C:

  | Owner (node +0x10) | Matrix | C |
  |---|---|---|
  | 0x825100 | self + 0xD0 | func_overlay_AREA02_008250C0 |
  | 0x823930 with +2 & 0x1F = 4 and +0x0D = 7 (the car, [32]) | *(self + 0x110) + 0x90 | 0x823930 -> 0x824020 -> 0x8242F0, func_overlay_AREA02_008242B0 |
  | 0x219550 (pickup) | node + 0xD0 | as in AREA01 |

  Result: hull 48 (a pickup) in all 5 sub-0 captures, hull 46 (the car) in
  a02_01. Sub 1 has no moved hull.
- **The orphaned car hull.** In a02_02 .. a02_04 hull 46 differs from the
  disc and no live node carries uid 46: the car freed itself when it stopped
  (FOURTH_LEVEL_ROUTE.md a02_02 f487). Its pool slot keeps +0x10 and +0xB0
  but its uid byte and bone matrices are cleared, so its last 001A2370 run
  cannot be repeated. The hull is accepted only when all of these hold
  (`orphan_problems`):
  - another capture of the run derives hull 46 from its live owner (a02_01);
  - every 32-bit word that differs from the disc is a word the ORIGINAL
    001A2370 writes in this hull (`written_words`: two probe runs with
    generic rigid matrices, over a scratch node carrying the uid);
  - the bytes are equal in every capture where the hull is orphaned.

  This is weaker than a derivation (known gap 2).
- **The uid words.** In a02_02 .. a02_04 bit 30 of uids 1, 2 and 3 is clear
  (the disc has 0xC000xxxx). The callers of 0019C6F0 in the overlay's C:
  - the gate kind 14 at +0xB0 > 170 ([38], 0x823D70,
    func_overlay_AREA02_00823D30): (0x1D, 1), (0x1E, 1) in the frame its
    countdown ends, when it also sets +4 = 3;
  - the kind-8 node of 0x823930 ([33], 0x8242F0 +6 case 3): (1, 1), then
    +4 = 3;
  - at state 0, only when D_00810761 is already set: the switch 0x823980
    ((0x1D, 1), (0x1E, 1)) and kinds 10 / 11 of 0x824020 ((1, 1)).

  `flag_calls` derives each capture's calls from its owners: the load must
  have been made with D_00810761 clear (the switch [31] live in state 1,
  which its state 0 enters only then; otherwise it stops, "not modelled"),
  and each gate [38] or kind-8 node that is no longer live in state 1 has
  made its calls. The calls only clear bit 30 of the word they select and
  select by bit 29, so their order does not matter. The ORIGINAL 0019C6F0
  then runs them over each capture's RAM and scratchpad with the directory
  set to the disc bytes; each call must return 1, may change nothing outside
  the uid words, and its code in RAM must equal the ELF. The class-0x0B
  records at the head of the sub-0 placement table map key 0x1E to uid 1,
  0x1D to uid 2 and 1 to uid 3. Result: no call in a02_00 and a02_01;
  (1, 1), (0x1D, 1), (0x1E, 1) in a02_02 .. a02_04, equal to the captured
  words.
- The export keeps the disc words and hulls (the load-time image).

**Background and fog.**
- No capture arms the background: render ctx +0x174 is 0 in all 7.
- The ORIGINAL 001D8FD0 runs over each capture with ctx +0xA0..+0xFF
  overwritten and rebuilds the 96 captured bytes from room entry 5 (key
  0x0200) in sub 0 and entry 6 (key 0x0201) in sub 1, on the non-constant
  branch. Flipping the entry changes 63 of the 96 bytes.
- The values come from the global `assets/render_context.emrc`, whose blocks
  equal RAM in every capture. There is no AREA02-specific file.

**Roster.**
- Sub 0: placement table 0x827830 (58 records), deferred groups 0x825680
  (27), 0x825C00 (8) and 0x8263F0 (18). Sub 1: placement table 0x828170 (14
  records), deferred group 0x825B50 (3).
- The group and placement bytes equal RAM in every capture. The checker
  rebuilds each EMRO by the original's walks over the pinned ELF + overlay
  and over each capture's RAM, and requires them to be equal.
- Live placement nodes keep the fields 001B6990 copies. (live, at rest) is
  pinned per capture: arrival and a02_s0 (14, 14); a02_00 (54, 53); a02_01
  (54, 51); a02_02 .. a02_04 (49, 48).

**Spawn and doors.**
- `spawn_table.emsp` is byte-identical to `assets/spawn/spawn_table.emsp`.
  In every capture D_008106C8 equals word +0x1C of the current spawn record
  (area 2, sub D_00810701, entry D_00810702: entry 1 in sub 1, entry 5 in
  sub 0), checked per capture with a canary plant and a control.
- The door file has the area 2 row 0x24DFC0 (4 records, 16 bytes) and the
  pointer array 0x24E140 (92 bytes); every window equals RAM in every
  capture. The doors the placements use:

  | Sub | Placement | Behaviour | Door id |
  |---|---|---|---|
  | 0 | [22] | 0x1BC350 | 0 (area change) |
  | 0 | [25] | 0x1BB860 | 3 (area change) |
  | 1 | [5] | 0x1BC350 | 1 (area change) |
  | 1 | [6] | 0x1BC350 | 2 |

**Scripts and overlay data.**
- The chains are the twelve the committed overlay C starts (every
  func_001BA1A0 argument in `src/overlays/AREA02/`): 0x826780, 0x826980,
  0x8269C0, 0x826A00, 0x826A40, 0x826A80, 0x826AC0, 0x826B40, 0x826F40,
  0x827670, 0x828C50, 0x828E10; 75 records.
- The placement tables lie between the chains, so the AREA01 rule (lowest
  entry up to the placement table) does not apply: the script window runs
  from the lowest entry to the end of the last record any chain reaches
  (0x826780..0x829190). The overlay data window is the module's data
  section (MWo3 header word 4 = 0x3C00), 0x825680..0x829280. Both are the
  pinned module's bytes.
- **The arrival (a01r_03) and a02_00 hold the load-time image**: no word of
  the data section differs from the disc module.
- Later captures show rewritten words, and each must be one of:
  - a word inside a record one of the 12 chains reaches (9 in a02_01, 18 in
    a02_02 .. a02_04);
  - a done word (+4) of the trigger table 0x827350 (18 records of 0x20
    bytes). The car calls 0x824910 (func_overlay_AREA02_008248D0,
    byte-identical C), which sets a record's done word to 1 once the car's
    +0xB0 exceeds the record's x (+0x10). `trigger_words` derives every done
    word from the car's x: the live car's +0xB0, or, once it has freed
    itself, the +0xB0 its pool slot keeps (the one free slot still carrying
    0x823930 at +0x10). In a02_02 .. a02_04 the car stands at x 210.9 and
    16 of the 18 records are done; records 2 and 8 (x 222.8 and 260) are not.
- The set of rewritten words in each capture must equal `tables.json`.

**Messages.**
- Sub 0: the area table D_00264DD0[3] = 0x26F620 has 4 records; the global
  table 54, 36 stream rows. The area bank is *D_0028A594 = 0x1764A00
  (`n0/f05_id41` + 0x14B000, 214 bytes); the global bank is 3,029 bytes. The
  checker compares every byte of `sub0/message_data.emmd` with RAM in every
  sub-0 capture (AREA01's `messages_problems`).
- Sub 1: no message file (finding 2).
- No text is printed or stored outside the ignored asset.

**World models.**
- Sub 0: *D_0028A59C = 0x1605200 (`n0/f04_id72` + 0xD800), 27 models,
  1,298,544 bytes. Sub 1: 0x1557E40 (`n1/f03_id43` + 0x82000), 16 models,
  173,456 bytes. Each span equals RAM, and `wm_span` over each capture's RAM
  gives the same extent.
- Every model owner whose +0x44 names a bank model: +0x44 = 001C6120(table,
  +0x0D), +0x0C = the model's bone count, every bone slot set. The bound
  count is pinned: 41, 41, 31, 31, 31 (a02_00 .. a02_04), 14 and 14 (sub 1).
  No AREA02 owner needed the AREA00 exception (a stale +0x44).

**Sound banks.**
- The area container is upload section 0 of the sub's nested descriptor
  (block +0 .. the section size). None of it is resident. In sub 0 the SShd
  container (631,824 bytes) is longer than the block's first file (0x28800),
  so the block bytes are assembled from the descriptor's own file list over
  `f00_id42`, `f01_id46` and the head of `f02_id44` (`block_bytes`).
- The bindings, the same in every capture of the sub:

  | Sub | Container | Group 1 | Group 2 | Group 4 | Refused |
  |---|---|---|---|---|---|
  | 0 | 3 banks | global rows 0..2 | area row 0 | area rows 1, 2 | group 3 |
  | 1 | 1 bank (168,368 bytes) | global rows 0..2 | area row 0 | refused | group 3, group 4 slots 0 and 1 |

  In sub 1 the group-4 handles 5 and 6 are still marked in use, but their
  headers (0x14DBC00, 0x14DC450) lie inside the resident `n1/f03_id43` and
  carry no SShd magic; 00119EA0 tests that word before it uses a bank, so the
  registry resolves ids that reach them as absent, as for group 3.
- Registries: sub 0, 1,000 ids: 479 audible, 520 absent, 1 UNSUPPORTED
  (script), 45 samples. Sub 1: 259 audible, 695 absent, 46 UNSUPPORTED
  ("unbound bank", finding 8), 15 samples.
- The checker re-derives every byte of each sub's registry from each
  capture's RAM (AREA01's `registry_from_ram`, samples from the disc
  container) and compares `area02_banks.bin` with the disc container over its
  full length.

## Checker, canary, controls and the mutation sweep

`tools/test_area02_assets_reference.py`:
- **loaders:** every file through its port loader, compiled privately from
  `src/` by the AREA01 checker's `build_loaders` into
  `build/area02/assets/test/`: 20 files load. `em_actor_cells_load` refuses
  `sub0/area02_cells.bin` (finding 3) and loads a copy with bit 29 cleared
  in every uid word; it loads `sub1/area02_cells.bin`. These three verdicts
  are asserted.
- **real checks:** every section above over the run's captures. Quick mode:
  the arrival, a02_01 and a02_02 (a02_01 is needed with a02_02: the orphan
  rule requires a derivation of the hull in the run). Full mode: all 7.
- **canary (75 sections).** `run_checks` runs again per sub over [a capture,
  a planted copy of it], the copy second in its list, and each per-capture
  comparison must report its plant for the copy and stay silent for the
  original. Plants: the static bank's last byte and extent, D_0028A5A0,
  TEST_1 of the level state, ctx +0xA4, a directory hull, uid 1's bit 30,
  the matrix of a moved hull's owner, the grid, the placement table and a
  placement node, the door row, the sub's spawn rows, a script word, a
  trigger done word, the message records (sub 0) or *D_0028A594 (sub 1),
  the model bank, the pitch ladder the registry reads, a model owner's
  +0x0D, D_008106C8 and the GS freeze texels. File plants: a dynamic-list
  file, an extra zone file, the cells file, cells.json, the sub-1 container,
  the sub-1 roster, the scripts file, tables.json, a sub-1 message file, the
  zone EMDL itself (its own pass); and each port loader must refuse a broken
  copy of its file (the spawn file keeps every window but loses the last
  byte of the sub-1 rows).
- **controls (129 changed inputs):** the file side and the RAM side of every
  comparator, plus the accept cases (the recorded calls, a freed gate or a
  kind-8 node out of state 1 selecting its calls, the orphan with a02_01, a
  word of the last chain, the car exactly at an undone trigger's x, clean
  copies under other names, the pinned bindings, one other-sub file in RAM).

**Mutation sweep (one bounded sweep, this lane's review).** 48
single-operation mutants over the checker's comparators and the exporter
functions it calls (`flag_calls`, `derive_words`, `orphan_problems`,
`written_words`, `verify_directory`, `owner_matrix`, `trigger_words`,
`run_time_words`, `chain_records`, `block_bytes`), each run in quick mode in
a private copy of `tools/`, at most 4 workers. The script is
`build/area02/sweep/sweep.py` (ignored).
- First pass: 40 killed, 8 survived. The survivors and what now kills them:

  | Mutant | Change | Killed by |
  |---|---|---|
  | M02 | other-sub test `all` -> `any` | accept control: one other-sub file in RAM |
  | M19 | "every bone slot set" -> "any" | control: the last slot of a multi-bone owner cleared |
  | M23 | refused set compared by intersection | controls: an extra refused slot, a missing one |
  | M28 | spawn loader reads only the sub-0 rows | loader canary: the sub-1 rows cut by one byte |
  | M32 | 0019C6F0's return value ignored | control: a call whose key names no record while the word keeps bit 30 |
  | M40 | trigger test `>` -> `>=` | accept control: the car exactly at an undone trigger's x |
  | M48 | per-capture scratchpad directory pointer check removed | control: the pointer moved in the scratchpad |

- Final state: 47 of 48 killed in quick mode, none only by a crash. M47
  (the gap test inside `block_bytes` removed) is equivalent: any gap or
  overlap also makes the assembled length differ from the requested range,
  which the next check refuses.
- The claim covers exactly these mutants. The reused AREA01 comparators keep
  their own sweep record and known gaps (AREA01_ASSETS.md G02..G10).

## Findings (for the lead)

1. **Every level file is per sub.** The two nested blocks are different
   blocks at different cursors (0x13DB640 / 0x13DCA00), and the duct's sub
   change reloads the nested block without an overlay reload. The grid, the
   directory, the static bank, the model bank, the area message bank and the
   sound container all change with the sub.
2. **Sub 1 has no area message bank.** *D_0028A594 is 0x1335F40 in both
   sub-1 captures: the top cursor D_0028A73C, and the value it had in AREA01
   (a01r_02). In AREA02 that address holds `chunk06/f00_id96`, and
   export_message_data's bank walk refuses it (a string runs past the
   resident data). Sub 0 loads a bank (`n0/f05_id41` + 0x14B000). The
   checker pins this (`message_bank_finding`). What the original does if a
   sub-1 owner shows an area record was not traced.
3. **`em_actor_cells_load` refuses the sub-0 directory**: uid 0's word
   carries bit 29 (AREA01_ASSETS.md finding 1, AREA00_ASSETS.md finding 3).
   A copy with bit 29 cleared loads. Sub 1's directory loads.
4. **Run-time uid words.** The gate [38] and the kind-8 node [33] clear bit
   30 of uids 1, 2 and 3 through 0019C6F0 (keys 0x1E, 0x1D and 1). A port
   that loads `sub0/area02_cells.bin` must make the same calls through a
   translated 0019C6F0.
5. **The car's hull is orphaned** once it frees itself; its final transform
   is not in any capture (known gap 2).
6. **The trigger table 0x827350** is rewritten at run time (done words), by
   the car's own x.
7. **export_collision's cell-list scan and the actor directory.** Given the
   whole sub-1 grid file, the scan takes the 18-uid directory at 0x13E0E40
   for a cell world and adds its 84 prims, at their disc positions, to the
   static EMCL. This lane passes the grid section alone. The AREA01 and
   AREA00 exports passed whole files; their logs report no cell n-gons, so
   they are not affected, but the tool's scan should be kept away from
   directories in later areas.
8. **46 sub-1 sound ids resolve to records that name no bound group.** In
   scope (2, 1), 001FB9F0's remap gives records whose group byte is -1 (41
   ids), 0 (2), 10, 14 or 18 (1 each); the registry marks them UNSUPPORTED
   ("unbound bank") rather than guess.
9. **Script entries: code, not the overview's scan.** `area_overview.py
   --area 2` lists 14 chains; two of them are not scripts by the C:
   0x827350 is the trigger table 0x824910 walks and 0x827630 is the vector
   0x824D50 writes through 001026A0. This lane uses the 12 func_001BA1A0
   arguments.
10. **D_0028A5A4 is stale** in AREA02 as in AREA00: it holds AREA01's list
    address, and no AREA02 capture draws a dynamic-list kick.

## Binding

Nothing is wired. The files are exported, checked and loadable, except
`sub0/area02_cells.bin` (finding 3). When AREA02 is bound:
- The scene selects `sub<D_00810701>/` for the level, collision, cells,
  roster, world models, message file and sound files. The spawn table,
  doors, scripts and overlay data are shared.
- The sub change inside the duct (a02_00) swaps every per-sub file without
  reloading the overlay; the overlay data keeps its run-time words across it.
- The live directory starts from `sub<N>/area02_cells.bin`. Two things change
  it at run time: owners 0x825100, 0x219550 and the car re-transform their
  hulls through a translated 001A2370 (node + 0xD0, or the car's bone 0 +
  0x90), and the gate [38] and the kind-8 node [33] make the 0019C6F0 calls.
  Neither 001A2370 nor 0019C6F0 is translated in the port yet.
- The script and overlay-data windows are the load-time image; the script
  host's record writes and the car's 0x824910 reproduce the run-time words.
- Sub 1 has no message file; the message loader needs a decision for a
  sub without an area bank (finding 2).
- The ctx +0xA0..+0xFF block is evaluated at run time from
  `assets/render_context.emrc` (room entries 5 / 6), as for AREA01 and AREA00.

## Known gaps

1. **Script run-time words** are located (each in a reached record, each
   capture's set pinned) but not re-derived with the original script ops.
2. **The orphaned car hull** is checked by its written-word set and its
   stability across the orphaning captures, not re-derived: the car's final
   matrix is not in any capture.
3. **The trigger rule** reads the car's x from a freed pool slot in a02_02 ..
   a02_04 (measured: the slot keeps +0xB0), and assumes the car's x only
   grows over its run (FOURTH_LEVEL_ROUTE.md: it runs east), so its current
   x stands for the largest x it reached.
4. **The grid pointer** D_0028A598 is pinned by measurement; the code that
   fills it is not identified.
5. **Not exported:** sub 2 (`chunk06.n2`, placement table 0x8283D0): no
   capture loads it; shadow receivers, owner object textures and door, prop
   and pickup models (as for AREA01 and AREA00).
6. **SPU residency** of the samples: there is no AREA02 SPU capture.
7. **The 46 UNSUPPORTED sub-1 sound ids** (finding 8) and the one sub-0
   script-op id are not resolved.
8. **Descriptive side files** are not checked (see Outputs).
