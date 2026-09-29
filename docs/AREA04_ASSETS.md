# AREA04 assets: local export and capture checks

Lane "A04ASSETS", level-3/4 side track, 2026-09-28 (fix round the same day,
after the review: the drum as a 001A2370 caller, the full caller census,
the refusal reasons, the state-3 kind rule, and controls for the review's
named survivors). This lane adds new files
only: four exporters, one checker and this document. It changes no port
source, no tracked port file and no decomp file, and nothing it adds is wired
into the game. Every exported byte comes from the user's own disc files and
ELF and is written into the ignored `assets/area04/` tree. Every export is
checked against the recorded original captures of AREA04
(FIFTH_LEVEL_ROUTE.md). Nothing was run in PCSX2.

The captures (10 in all, every one with RAM, scratchpad and GS freeze):
- the AREA04 arrival, the end of beat a02_05
  (`../Extermination/build/s87/route_a02/a02_05_progression_exit/`);
- a04_00 .. a04_04 and the side beats a04_s0 .. a04_s3
  (`../Extermination/build/s87/route_a04/`).

a04_05 ends in AREA22 and is excluded by its area byte.

## One sub-state was loaded

Every capture has D_00810701 = 0: door [40] is a room move inside the sub
(spawn entries 4 and 3), not a sub change. The whole resident load map of
each nested block was compared with every capture's RAM
(`export_area04_common.loaded_sub_proof`, written to
`assets/area04/loaded_sub_proof.json`):

| Nested block | Differing 16-byte rows per capture |
|---|---:|
| `chunk08.n0` (sub 0) | 251, all in the cell directory |
| `chunk08.n1` (sub 1) | 295,855 |

Sub 1 is a different nested block (16 files against 14). No capture loads
it, so nothing of it is exported. The per-sub files are still written under
`sub0/`, so that a later sub-1 export can sit beside them.

## Outputs

The exports use the formats the port already reads for AREA11, AREA01,
AREA00 and AREA02:

| Output (`assets/area04/`) | What | Port loader |
|---|---|---|
| `sub0/level/NN_<file>.emdl` (+ `.gsmat.json`, descriptive) | level geometry, texels, GS state codes | `em_model_load` |
| `sub0/level/static_bank.emsc` | the static bank `*D_0028A5A0` | `em_script_image_load` |
| `sub0/level/dynamic_objects.emsc` | the dynamic list `*D_0028A5A4` (54 entries) | `em_script_image_load` |
| `sub0/area04.emcl` (+ `scene.txt` from export_collision.py) | grid collision with node class, rank and axis sections | `em_collision_load` |
| `sub0/area04_cells.bin` | the cell directory `*0x70003250` (disc rest state) | `em_actor_cells_load`, which **refuses** it (finding 3) |
| `sub0/roster.emro` | placements 0x82A130 and deferred groups of sub 0 | `em_actor_roster_load` |
| `sub0/message_data.emmd` | EMMD v1 for area 4: 54 global and 262 area records, 36 stream rows, both banks | `em_message_live_install` |
| `sub0/world_models.emwm` + `.json` | the model bank `*D_0028A59C` | `em_world_models_parse` |
| `sub0/sfx/area04_banks.bin`, `banks.json` | the sub's SShd container | (the registry's source) |
| `sub0/sfx/sfx_registry.emsr` + `.json` | EMSR v2 for scope (4, 0) | `em_sfx_registry_load` |
| `spawn_table.emsp` | the global spawn export; its area 4 sub-0 rows are 0x24BBF0 (13 records) | `em_spawn_table_load` / `_read` |
| `door_destinations.emsp` | EMSP windows: `D_0024E140[0..0x17)` and the area 4 row 0x24E000 (10 records) | `em_spawn_table_load` / `_read` |
| `area04_scripts/scripts.emsc` | the script window 0x8272A0..0x82C8D0 (20 chains, 212 records) | `em_script_image_load` |
| `overlay_data.emsc` | the overlay data section 0x826600..0x82C900 | `em_script_image_load` |
| `sub0/level/level.json`, `sub0/cells.json`, `tables.json`, `loaded_sub_proof.json` | counts, hashes and the verification summary (no text, no data) | none |

There is no `background.embg` (see "Background and fog").

The checker does not check the descriptive side files: `scene.txt`, the
`.gsmat.json` files, `world_models.json`, `sfx_registry.json`, `banks.json`,
`level.json`. It does compare the recorded lists in `cells.json` (the moved
hulls per capture, and `moved_uids` against its own rows) and in
`tables.json` (the run-time words of each capture) with its own results.

## Tools

All the tools are pure Python and run on native arm64 macOS. Run them from
the port root.

```sh
python3 tools/export_area04_level.py    # level, textures, materials, dynamic list, collision, cells (~15 s CPU)
python3 tools/export_area04_tables.py   # roster, spawn, doors, scripts, overlay data, messages, world models (<1 s)
python3 tools/export_area04_sfx.py      # the container and the registry (~1 s)
python3 tools/test_area04_assets_reference.py                  # checker, 3 captures
EM_TEST_FULL=1 python3 tools/test_area04_assets_reference.py   # all 10 captures
EM_AREA04_ASSETS=<dir> python3 tools/test_area04_assets_reference.py   # check another export tree
```

Checker timings on the M1 (2026-09-28, fix round, other lanes running):
quick 6.7 to 8.4 s CPU, full 10.2 s CPU (10.7 s wall). The first run after any `src/`
header change adds a few seconds to rebuild the loader library.

**Reuse.** The AREA01 and AREA02 exporters and the AREA01 checker are
reused by import; none of them is edited. `export_area04_common.configure(sub)`
sets the attributes of `export_area01_common` for AREA04 (area 4, the sub,
AREA04.BIN = MWo3 id 5, 0x9400 bytes, pinned by SHA-256; the output paths
and the capture list). From there these run unchanged:
- the load map, `LoadedImage`, `compare_load_map`;
- from `export_area01_level`: the bank walk, `dynamic_entries`, the kick
  check with the dynamic list, the zone builder and writer,
  `cell_directory`, `verify_cell_directory` / `derive_cell_image` (the
  ORIGINAL 001A2370 in the EE interpreter), `check_background`,
  `check_ctx_room_block`;
- from `export_area02_level`: `run_collision` and `grid_extent`
  (export_collision.py over the grid section alone);
- from `export_area01_tables`: `export_doors`, `export_messages`,
  `walk_chain`, `match_nodes`, the spawn-field helpers;
- from `export_area02_tables`: `export_world_models` with its model-owner
  filter, `overlay_data_window`, `area_bank`, `bank_source`;
- from `export_area01_sfx`: `bindings_from_captures`; from
  `export_area02_sfx`: `nested_block`, `block_bytes`, `area_container`;
- from the AREA01 checker: `level_bank_problems`, `zone_problems`,
  `emcl_equal`, `grid_problems`, `roster_image`, `roster_nodes`,
  `emsp_problems`, `messages_problems`, `world_models_problems`,
  `registry_from_ram`, `load_map_problems`, `build_loaders` and the
  other helpers the checker's docstring lists;
- through those: `export_level.py` and `export_collision.py` (decomp),
  `export_area11_roster`, `export_spawn_table`, `export_message_data`,
  `export_world_models`, `export_sfx_registry`.

Importing the AREA02 modules sets the shared module state to AREA02, and
`export_area02_level` sets `export_area01_level.owner_matrix` to AREA02's.
Each AREA04 module therefore calls `configure` again after its imports, and
`export_area04_level.install()` puts AREA04's `owner_matrix` back. The
checker and the cell check call `install()` before each directory
derivation. A process must not mix AREA04 work with another area's level
exporter.

AREA04's own code, and why:
- `owner_matrix`, `HULL_OWNERS`, `DRUM`, `NODE_D0_OWNERS`: the owners
  live in AREA04 that call 001A2370, with the matrix each passes;
- in the checker, `RETRANSFORM_SITES` / `retransform_census_problems`:
  every call of 001A2370 in the pinned ELF and overlay, so that
  `owner_matrix` provably names every caller (finding 8);
- in the checker, `c_script_entries` / `script_entry_problems`: the chain
  set read from the committed C, compared with `SCRIPT_ENTRIES` and
  `tables.json`;
- `SCRIPT_ENTRIES`, `chain_records`, `scripts_window`, `run_time_words`:
  the chains the AREA04 C starts, and the rule for run-time words;
- `export_roster`, `export_spawn`: the pinned table addresses of the sub;
- in the checker, `kind_allowed` / `roster_node_problems`: [47]'s C rewrites
  its +0x0D (finding 4).

## How each asset is derived and checked

**Load map.** The rule is AREA01_ASSETS.md "Load map", with INDEX.IDX sector
8 read from the disc image (in a04_s2 the descriptor buffer holds a later
sector). The top block `chunk08` (11 files) lies at 0x1335F40, and the
nested block `chunk08.n0` at 0x1664C80, resident from +0x143800
(`f01_id44` from +0xE9800, then `f02_id45` .. `f13_id8e`). There are
8,372,224 mapped bytes. Every mapped byte equals RAM in every capture,
except the bytes of the cell directory, which the directory check covers.
The checker also requires the sub-1 nested files not to fit RAM. As a
control, a single one of them planted in RAM must not count as the sub-1
load.

**Level geometry.**
- The static bank lies in the nested block at 0x16BEC80 (`n0/f01_id44`),
  1,013 objects, extent ..0x197CDE0. The grid at render ctx +0x140
  (0x16BFCA0, in object 0) names exactly objects 1..1012, and every
  record has matrix slot 0. The objects' units lie in ten mapped files, so
  there is one zone EMDL per file:

  | Zone | Objects | Vertices | Triangles | Textures |
  |---|---:|---:|---:|---:|
  | 00_f01_id44 | 582 | 8,384 | 4,679 | 119 |
  | 01_f02_id45 | 33 | 900 | 504 | 33 |
  | 02_f03_id42 | 53 | 936 | 517 | 38 |
  | 03_f04_id46 | 18 | 303 | 158 | 12 |
  | 04_f05_id6f | 2 | 84 | 42 | 5 |
  | 05_f06_id6e | 47 | 1,335 | 692 | 29 |
  | 06_f07_id72 | 49 | 1,000 | 503 | 20 |
  | 07_f08_id4e | 72 | 1,839 | 939 | 35 |
  | 08_f09_id4f | 86 | 2,466 | 1,308 | 34 |
  | 09_f10_id6c | 70 | 618 | 354 | 24 |

- **A dynamic list.** D_0028A5A4 = 0x197D480 lies in the map
  (`n0/f10_id6c`), 54 entries of 0x860 bytes after the 16-byte head. It is
  exported raw, as for AREA01 (`dynamic_objects.emsc`). The captures draw
  388 kernel-0x00237450 kicks. Every one of them REFs an entry of the
  list, and the file equals RAM over its whole extent in every capture.
- Every textured level-kernel kick REFs units inside a bank object: 5,471
  kicks in the 10 captures (721 distinct REFs), all with the class-0 GS
  state (PRIM 0x3C, TEST_1 0x5000D, ALPHA_1 0x80000000A8, TEX1_1 0x60,
  CLAMP_1 0).
- The texels come from the level-load GS upload of the nested section 1,
  replayed from the disc image. They equal the GS-freeze decode of every
  capture (3,490 comparisons).
- The checker requires each zone file to equal a rebuild from the exported
  bank byte for byte (AREA01's `zone_problems`), and compares the bank and
  the list with RAM over their full lengths (`level_bank_problems`).

**Collision.**
- The grid is D_0028A598 = 0x1999C80 (`n0/f13_id8e` + 0x1800). The cell
  directory lies in the same file (0x19C0480), so `export_collision.py
  --node-class` gets the grid section alone (AREA02_ASSETS.md finding 7),
  and `--verify-ram` passes for every capture: 931 grid nodes, 2,206 grid
  vertices, 1,901 EMCL vertices, 3,454 indices, no cell n-gons. The tool
  reports 7 warped quads; they are reports, not failures.
- The checker rebuilds the whole EMCL from each capture's RAM grid
  (`emcl_from_ram`) and requires the file to equal it. It pins D_0028A598,
  and requires the grid block to lie in the load map, outside the directory
  allowance. The code that fills the pointer at load is not identified, as
  for the earlier areas.

**Cell directory.** It lies at 0x19C0480: 82 uids, 42,724 bytes. uid 0's word
carries bits 31 and 29. `sub0/area04_cells.bin` holds the disc bytes. It
must equal the disc over its whole length and end where the directory walk
ends. Every byte of every capture's RAM directory is then derived:
- **Hulls re-transformed by a live owner.** The ORIGINAL 001A2370 runs in the
  EE interpreter with the matrix each owner's code passes. The matrix is
  read from the committed byte-identical C (runtime = link name + 0x40):

  | Owner (node +0x10) | Matrix | C | Hull |
  |---|---|---|---|
  | 0x825510 ([62]) | self + 0xD0 | func_overlay_AREA04_008254D0 | 42 |
  | 0x825B00 ([65]) | self + 0xD0 | func_overlay_AREA04_00825AC0 | 22 |
  | 0x8260C0 (the reel [70]) | self + 0xD0 | func_overlay_AREA04_00826080 | 46 |
  | 0x219550 (pickup) | node + 0xD0 | as in AREA01 | 77 .. 81 |
  | 0x156620 (drum, five nodes) | node + 0xD0 | original call at 0x156EF0 (NEARMISS C `src/func_00156620.c`) | 3 .. 7, never moved (below) |

  The same eight hulls are moved in all 10 captures. Each one equals the
  derivation for its live owner, including the reel's after it rolled
  down and was placed (a04_02 onward).
- **Every caller is named.** The pinned ELF and the AREA04 overlay hold
  exactly 11 calls of 001A2370 and no other reference to it (no jump, no
  address word, no address built in a register pair): 0x156EF0 (the drum),
  0x219668 (the pickup), 0x21A104 (in 0x219F50), and 8 in the three
  overlay owners (0x825510 three, 0x825B00 two, the reel three). The
  checker re-counts them from the ELF and the module every run. 0x219F50
  is called only from 0x219870 (at 0x2198E4), and passes the scratchpad
  0x700036A0: a copy of node + 0xD0 whose row +0x20 it scales by a path
  length. `owner_matrix` does not model that matrix; no AREA04 capture has
  a live 0x219870 node, and the checker fails on one.
- **The drums' hulls never move in AREA04.** The drum's state-2 path calls
  001A2370(self, self + 0xD0) (the instruction before the call at
  0x156EF0 sets the second argument to self + 0xD0, and the one in its
  delay slot the first to self). But 001A2370 changes a hull only when
  its first prim carries the extended bit 0x800, and drum hulls 3 .. 7
  carry 0x1000 without it (the moved hulls carry 0x1800). The ORIGINAL
  001A2370, run in the EE interpreter for each of the five drums with its
  matrix changed, leaves the whole directory equal to the disc bytes. With
  the bit planted in a copy of the directory, the same run moves exactly
  the drum's hull, and the checker accepts that hull only because
  `owner_matrix` names the drum. All five drums are in state 1 in every
  capture.
- **The uid words** equal the disc in every capture. No AREA04 overlay
  function calls 0019C6F0. `verify_cell_directory` compares every byte, the
  uid words included.
- No hull is orphaned.
- The export keeps the disc words and hulls (the load-time image).

**Background and fog.**
- No capture arms the background: render ctx +0x174 is 0 in all 10.
- The ORIGINAL 001D8FD0 runs over each capture with ctx +0xA0..+0xFF
  overwritten. It rebuilds the 96 captured bytes from room entry 11 (key
  0x0400), on the non-constant branch (D_008106C8 is 0x340D08, 0x348D09 or
  0x348D08). Flipping the entry changes 63 of the 96 bytes.
- The values come from the global `assets/render_context.emrc`, whose blocks
  equal RAM in every capture. There is no AREA04-specific file.

**Roster.**
- Placement table 0x82A130 (90 records), deferred groups 0x826930 (26),
  0x8268D0 (1) and 0x826600 (8).
- The group and placement bytes equal RAM in every capture. The checker
  rebuilds the EMRO by the original's walks over the pinned ELF + overlay
  and over each capture's RAM, and requires them to be equal.
- Live placement nodes keep the fields 001B6990 copies, except [47]'s
  +0x0D, which its C rewrites (finding 4). (live, at rest) is pinned per
  capture: the arrival (89, 89); a04_00, a04_01, a04_s0 .. a04_s3 (87, 87);
  a04_02 .. a04_04 (87, 86), after the reel was moved.

**Spawn and doors.**
- `spawn_table.emsp` is byte-identical to `assets/spawn/spawn_table.emsp`.
  D_0024D650[4] = 0x275510 names sub 0 at 0x24BBF0 and sub 1 at 0x24BE60
  (13 sub-0 records). In every capture D_008106C8 equals word +0x1C of
  the current spawn record: entry 0 (0x340D08), 4 (0x348D09) or 3
  (0x348D08). This is checked per capture with a canary plant and a
  control.
- The door file has the area 4 row 0x24E000 (10 records, 40 bytes; the
  area 5 pointer names the same row) and the pointer array 0x24E140 (92
  bytes). Every window equals RAM in every capture. The doors the
  placements use:

  | Placement | Behaviour | Door id | Record |
  |---|---|---|---|
  | [35] | 0x1BB860 | 8 (area change) | 02 04 00 00 |
  | [37] | 0x1BC350 | 1 (area change) | 16 00 00 00 |
  | [38] | 0x1BC350 | 2 (area change) | 03 00 01 01 |
  | [40] | 0x1BC350 | 3 | 04 03 00 00 |
  | [42] | 0x1BC350 | 4 (area change) | 14 00 00 00 |
  | [45] | 0x823700 | 5 | 08 09 00 00 |
  | [49] | 0x1BC350 | 6 | 0B 0C 00 00 |

**Scripts and overlay data.**
- The chains are the twenty that the committed overlay C starts (every
  func_001BA1A0 argument in `src/overlays/AREA04/`): 0x8272A0, 0x8275A0,
  0x8278D0, 0x827D10, 0x827D90, 0x8280D0, 0x828450, 0x8286D0, 0x828BE0,
  0x829020, 0x8293A0, 0x829570, 0x829930, 0x829CF0, 0x82BD60, 0x82C060,
  0x82C120, 0x82C1F0, 0x82C3B0, 0x82C650. Together they reach 212
  records.
- The placement tables lie between the chains, as in AREA02. The script
  window therefore runs from the lowest entry to the end of the last
  record any chain reaches (0x8272A0..0x82C8D0). The overlay data window
  is the module's data section (MWo3 header word 4 = 0x6300),
  0x826600..0x82C900. Both are the pinned module's bytes.
- Run-time words: 4 in the arrival (0x827790..0x82779C, records of the
  arrival chain 0x8275A0), and 9 in every a04 capture (those 4, plus
  0x827A80..0x827A8C and 0x827AE0 in chain 0x8278D0, the event's script).
  Each word lies in a record a chain reaches. The set in each capture must
  equal `tables.json`. AREA04 has no rewritten table like AREA02's
  trigger table.
- The checker reads the chain set from the C on every run (the data
  symbol of each func_001BA1A0 call in `src/overlays/AREA04/`) and
  requires `SCRIPT_ENTRIES` and the `chains` keys of `tables.json` to
  equal it.

**Messages.**
- The area table D_00264DD0[5] = 0x26F790 has 262 records. The global
  table has 54 records, and there are 36 stream rows.
- The area bank *D_0028A594 = 0x152E740 (`chunk08/f04_id41` + 0, 13,660
  bytes) lies in the top block. The global bank is 3,029 bytes.
- The checker compares every byte of `sub0/message_data.emmd` with RAM in
  every capture (AREA01's `messages_problems`). No text is printed or
  stored outside the ignored asset.

**World models.**
- *D_0028A59C = 0x1664C80: the nested cursor, the first resident byte
  (`n0/f01_id44` + 0xE9800). 36 models, 367,552 bytes. The span equals
  RAM, and `wm_span` over each capture's RAM gives the same extent.
- For every model owner whose +0x44 names a bank model, +0x44 must equal
  001C6120(table, +0x0D), +0x0C must equal the model's bone count, and
  every bone slot must be set. The bound count is pinned per capture: 74
  in the arrival, a04_00, a04_01, a04_s0, a04_s1 and a04_s3; 73 in a04_02
  .. a04_04 and a04_s2.

**Sound banks.**
- The area container is upload section 0 (0x0 .. 0x53000) of the nested
  descriptor: an SShd container of 339,760 bytes with 2 banks, all in
  `n0/f00_id43`, and none of it resident.
- The bindings are the same in every capture:

  | Group 1 | Group 2 | Group 4 | Refused |
  |---|---|---|---|
  | global rows 0..2 | area row 0 | slot 0: area row 1 | group 3 slot 0; group 4 slot 1 |

  Group 3 slot 0 names handle 3, whose record is in use (use word 1)
  with header 0x1800030, which lacks the SShd magic at +0xC. Group 4 slot
  1 names handle 6, whose record in D_0027C6C0 has use word 0 and header
  pointer 0: a freed handle (finding 5). `bindings_from_captures` refuses
  both. `export_area04_sfx.refusal_reasons` reads the reason of each from
  the handle record in every capture (they agree), and `banks.json`
  (`refused_why`) and `sfx_registry.json` (`refused_slots`) carry it. No id
  of scope (4, 0) reaches either refused slot: all 556 absent ids are
  001FB9F0 remap -1, and no entry carries a 00119EA0 refusal note.
- Registry for scope (4, 0), 1,000 ids: 406 audible, 556 absent, 38
  UNSUPPORTED ("unbound bank", finding 6), 42 samples.
- The checker re-derives every byte of the registry from each capture's
  RAM (AREA01's `registry_from_ram`, with the samples taken from the disc
  container). It also compares `area04_banks.bin` with the disc container
  over its full length.

## Checker, canary, controls and the mutation sweep

`tools/test_area04_assets_reference.py`:
- **loaders:** every file goes through its port loader, compiled privately
  from `src/` by the AREA01 checker's `build_loaders` into
  `build/area04/assets/test/`: 21 files load. `em_actor_cells_load` refuses
  `sub0/area04_cells.bin` (finding 3), and loads a copy with bit 29 cleared
  in every uid word. Both verdicts are asserted.
- **real checks:** every section above, over the run's captures. Quick
  mode uses the arrival, a04_02 (the reel moved) and a04_s2 (the
  descriptor buffer reused, one model owner fewer). Full mode uses all 10.
- **canary (55 sections).** `run_checks` runs again over [the arrival, a
  planted copy of it], with the copy second. Each per-capture comparison
  must report its plant for the copy and stay silent for the original.
  - RAM plants: the static bank's last byte and extent, D_0028A5A0, the
    dynamic list's count word (the list's own entries are VIF data the
    display-list walker parses), TEST_1 of the level state, ctx +0xA4, a
    directory hull, uid 1's bit 30, the matrix of a hull's overlay owner,
    the grid, the placement table and a placement node, the door row, the
    sub-0 spawn rows, a script word, an overlay-data word outside every
    chain, the message records, the model bank, the pitch ladder the
    registry reads, a model owner's +0x0D, D_008106C8 and the GS freeze
    texels.
  - File plants: an extra zone file, the cells file, `cells.json`, the
    container, the roster, the scripts file, `tables.json`, and, in their
    own pass, a zone EMDL and the dynamic-list file.
  - Each port loader must refuse a broken copy of its file. The spawn file
    keeps every window but loses the last byte of the sub-0 rows.
- **controls (158 changed inputs):** the file side and the RAM side of
  every comparator, plus the accept cases:
  - every moved hull, and the matrix of each owner whose hull is moved in
    the capture (the three overlay owners and the pickup; owners whose
    hull is at rest are skipped, since nothing derives it);
  - the drums: each drum with its matrix changed leaves the directory as
    on disc (ORIGINAL 001A2370); with the 0x800 bit planted, a knocked
    drum's derived hull is accepted and the same hull with the matrix at
    rest rejected;
  - the 001A2370 census (accepted), an extra call site in the module, and
    a live 0x219870 node (rejected);
  - a freed reel, with no derivation elsewhere (rejected) and with a live
    copy in the run (accepted);
  - owner_matrix unit cases (the drum node + 0xD0; 0x219870 and 0x219F50
    None);
  - the chain-record bounds (the last byte of the last record accepted,
    the byte after it rejected; a placement-table word between the chains
    rejected; the window's last word 0x82C8FC, which lies in no chain,
    rejected by `run_time_words` and against `tables.json`);
  - the chain set: equal to the C (accepted, twenty chains); the checker's
    set or `tables.json` without chain 0x828BE0 (rejected); a word of chain
    0x828BE0's first record (accepted);
  - [47]'s kinds (the recorded states accepted; state 2 with the first
    kind, state 0 with the first kind, state 3 with the first or second
    kind accepted; the second kind in state 1, a kind outside the table in
    states 0, 2 and 3, state 4 with any kind, and the first kind changed
    in RAM rejected); [47]'s record param pinned equal to its second kind;
  - a binding changed in the second capture only (rejected: 'a different
    bank binding'); the refusal reasons (no magic / freed), and handle 6's
    record set in use (reason changes);
  - the door loader: the file (accepted), without the pointer array or
    without the row (refused);
  - a capture whose sub byte is 1;
  - the directory allowance moved over the grid, and an allowance starting
    0x10 below the grid (covering its header without starting at it);
  - the dynamic list count + 1, and D_0028A5A4 moved;
  - the pinned bindings;
  - only the first, or only the last, sub-1 nested file in RAM (accepted);
  - clean copies under other names (accepted).

**Mutation sweep (one bounded sweep, this lane's review).**
- There were 48 single-operation mutants over the checker's comparators and
  the exporter functions it calls:
  - `owner_matrix` and `HULL_OWNERS`;
  - `run_time_words`, `chain_records` and `scripts_window`;
  - `kind_allowed` and `roster_node_problems`;
  - the load, level, EMCL, cells, window, binding, spawn and sfx checks;
  - the loader canaries.
- Each mutant ran in quick mode in a private copy of `tools/`, with at most
  4 workers. The script is `build/area04/sweep/sweep.py` (ignored).
- First pass: 44 killed, 3 survived, and 1 pattern was not found (it named
  a line of the AREA01 checker; it was replaced by a mutant of the
  model-lookup test). The survivors, and what now kills them:

  | Mutant | Change | Killed by |
  |---|---|---|
  | M11 | the grid_problems loop removed | control: the directory allowance moved over the grid |
  | M38 | the spawn check's sub test removed | control: a capture whose sub byte is 1 |
  | M24 | window extent compared by base only | equivalent (below) |

- Final state: 47 of 48 killed in quick mode, none only by a crash.
- M24 is equivalent. The header check ties the file length to the window
  length, so a window of another length also fails `d != disc`, which the
  next line reports.
- The review then ran its own 40 mutants (N01..N40; its harness and
  results are in `../Extermination/build/r5review/A04ASSETS/`): 23 killed,
  8 non-equivalent survivors, the rest equivalent or masked. The fix
  round added the controls above for each survivor and re-applied those
  mutants alone to the fixed checker in quick mode
  (`build/area04/fix/named.py`, ignored; not a new sweep):

  | Mutant | Change | Now killed by |
  |---|---|---|
  | N01 | any state -> {record, first, second} | state 4 rejected |
  | N03 | states 2/3 drop the first kind | state 2 / state 3 with the first kind accepted |
  | N04 | state 0 -> {record} | state 0 with the first kind accepted |
  | N12 | sub-1 fit test on the first nested file only | only the first sub-1 file in RAM accepted |
  | N13 | directory allowance (table, table + 1) | allowance 0x10 below the grid |
  | N17 | bindings from the first capture only | binding changed in the second capture |
  | N18 | `run_time_words` skips the window's last word | last word 0x82C8FC |
  | N19 | `SCRIPT_ENTRIES` without 0x828BE0 | the chain set against the C (real check) |
  | N25 | door loader skips the pointer-array read | door file without the pointer array |
  | D01 | the drum removed from `owner_matrix` | the planted knocked drum; the owner_matrix unit case |

  N36 (state 3 treated like state 0, {record, first}) survives and is
  equivalent: [47]'s record param equals its second kind (9, pinned by a
  control), so {record, first} = {record, first, second} for every AREA04
  input. N01's and N36's original form (the checker stricter than the C
  for state 3) no longer applies: state 3 now allows every kind the node
  can carry into it.
- The claim covers exactly these mutants. The reused AREA01 and AREA02
  comparators keep their own sweep records and known gaps
  (AREA01_ASSETS.md G02..G10, AREA02_ASSETS.md).

## Findings (for the lead)

1. **Only sub 0 is captured.** Every AREA04 capture is sub 0 (chunk08.n0).
   Sub 1 (chunk08.n1, 16 files, placement table 0x82AF70, group 0x826DE0)
   is not exported. FIFTH_LEVEL_ROUTE.md section 8 lists it as not visited.
2. **AREA04 draws a dynamic list.** Unlike AREA00 and AREA02, D_0028A5A4
   (0x197D480) is AREA04's own. It has 54 entries, and 8 of the 10
   captures draw from it.
3. **`em_actor_cells_load` refuses the directory**: uid 0's word carries
   bit 29 (with bit 31), as in AREA01, AREA00 and AREA02 sub 0. A copy with
   bit 29 cleared loads.
4. **[47] rewrites its +0x0D.** func_overlay_AREA04_00823EA0
   (byte-identical C) sets +0x0D from the 8-byte table 0x827530 indexed by
   D_00810701:
   - the first kind (8 in sub 0) in state 0 while D_00810764 != 0xFF;
   - the second kind (9, equal to the record's param) when its sub-state 1
     leaves for state 2.

   Its C never sets state 1 or 3 itself: 001B0FD0 advances state 0 to 1
   (unless 001B0EA0 holds it), and state 3 is set elsewhere; its only
   action is 001AFC10. So state 0 carries the record's value or the first
   kind, state 1 the first kind, and states 2 and 3 any of the three.
   The arrival shows 8 (state 1), and every a04 capture shows 9 (state 2,
   after the event). The checker's placement-node check applies this rule
   to 0x823EE0 nodes only.
5. **Group 4 slot 1 names a freed handle.** In every capture, D_00281D50
   group 4 slot 1 still holds handle 6, whose record has use word 0 and
   header pointer 0. The registry treats it as refused, like group 3 (whose
   header lacks the SShd magic). No id of scope (4, 0) reaches either
   refused slot, so the refusal changes no registry entry.
6. **38 sound ids resolve to records that name no bound group.** In scope
   (4, 0), 001FB9F0's remap gives records whose group byte is -1 (14 ids),
   0 (6) or one of 4, 8, .., 68 (18). The registry marks them
   UNSUPPORTED ("unbound bank") rather than guess.
7. **The area message bank lies in the top block**
   (`chunk08/f04_id41` + 0), not in the nested block as in AREA02 sub 0.
8. **001A2370 has two callers the earlier owner lists omit.** The boot
   drum 0x156620 passes self + 0xD0 (call at 0x156EF0), and 0x219F50
   (reached only from 0x219870) passes the scratchpad 0x700036A0, a copy
   of node + 0xD0 with row +0x20 scaled. `export_area01_level`,
   `export_area00_level` and `export_area02_level` name neither (this
   lane does not edit them). In AREA04 the drums' hulls lack the 0x800
   extended bit, so 001A2370 leaves them unchanged; whether that holds for
   the drum hulls of AREA01 (four drums per sub, AREA01_OVERVIEW.md) is
   not checked here. No AREA04 capture has a live 0x219870 node.

## Binding

Nothing is wired. The files are exported, checked and loadable, except
`sub0/area04_cells.bin` (finding 3). When AREA04 is bound:
- The scene selects `sub0/` for the level, dynamic list, collision, cells,
  roster, world models, message file and sound files. The spawn table,
  doors, scripts and overlay data are shared. A sub-1 load has no export
  yet.
- The live directory starts from `sub0/area04_cells.bin`. Owners
  0x825510, 0x825B00, 0x8260C0, 0x219550 and the drum 0x156620 call a
  translated 001A2370 with node + 0xD0 (the port has
  `em_actor_cells_retransform_001A2370`). The translation must keep the
  original's 0x800 gate: the drums' AREA04 hulls lack the bit, so a
  knocked drum leaves its hull as loaded. A thrown pickup (0x219870 ->
  0x219F50) passes the scaled scratchpad copy instead; that path is not
  in any AREA04 capture.
- The dynamic list is drawn by kernel 0x00237450 with each entry's +0x34
  translation (001D5BD0 for stage 0x0100 / 0x0101), as in AREA01. It is
  exported raw, not as EMDL.
- The script and overlay-data windows are the load-time image. The script
  host's record writes reproduce the run-time words.
- The ctx +0xA0..+0xFF block is evaluated at run time from
  `assets/render_context.emrc` (room entry 11), as for the earlier areas.

## Known gaps

1. **Script run-time words** are located (each in a reached record, each
   capture's set pinned) but not re-derived with the original script ops.
2. **The grid pointer** D_0028A598 is pinned by measurement. The code that
   fills it is not identified.
3. **Not exported:**
   - sub 1 (`chunk08.n1`): no capture loads it;
   - shadow receivers, owner object textures, and door, prop and pickup
     models (as for the earlier areas);
   - the dynamic list as geometry: it is exported raw, as in AREA01.
4. **SPU residency** of the samples: there is no AREA04 SPU capture.
5. **The 38 UNSUPPORTED sound ids** (finding 6) are not resolved.
6. **[47]'s kind rule** (finding 4) is read from its C, and the captures
   show only two of its paths: state 1 with the first kind, and state 2
   with the second kind. The second kind equals the record's value, so in
   states 2 and 3 the check cannot tell a switched node from one that went
   straight to state 2 (mutant N36 is equivalent for that reason).
7. **Descriptive side files** are not checked (see Outputs).
8. **The 0x219870 / 0x219F50 matrix** (finding 8) is not modelled by
   `owner_matrix`; the checker fails on a live 0x219870 node rather than
   guess.
9. **A moved drum hull** is proved only on a planted directory (the 0x800
   bit set): no AREA04 hull of a drum can move, and no capture has a drum
   in state 2.
