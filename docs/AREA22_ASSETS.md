# AREA22 assets: local export and capture checks

Lane "A22ASSETS", level-3/4 side track, 2026-09-28. This lane adds new files
only: four exporters, one checker and this document. It changes no port
source, no tracked port file and no decomp file, and nothing it adds is wired
into the game. Every exported byte comes from the user's own disc files and
ELF and is written into the ignored `assets/area22/` tree. Every export is
checked against the recorded original captures of AREA22
(SIXTH_LEVEL_ROUTE.md). Nothing was run in PCSX2.

The captures (6, every one with RAM, scratchpad and GS freeze):
- the AREA22 arrival, the end of beat a04_05
  (`../Extermination/build/s87/route_a04/a04_05_progression_exit/`);
- a22_00, a22_01 and the side beats a22_s0, a22_s1, a22_s2
  (`../Extermination/build/s87/route_a22/`).

a22_02 ends in AREA01 and a22_s3 in AREA04; both are excluded by their area
byte (the checker asserts exactly these two are excluded).

## The level block has no nested sub-block

INDEX.IDX sector 26 (D_00810700 + 4) is one descriptor, `chunk26` (7 files),
with nested count +0x18 = 0. Unlike the top descriptors of AREA01, AREA00,
AREA02 and AREA04, it carries upload sections and a resident offset itself:
section 0 (block +0 .. +0x51000, the sound container) and one group-A
section (+0x51000, 0x88800 bytes, the GS texel upload); resident offset
+0x14 = 0xD9800; no group-B section (+0x10 = 0).

`export_area01_common.build_load_map` refuses such a top descriptor
("not modelled"). `export_area22_common.build_load_map_flat` models it by the
loader's own rule, 001FFCD0 state 5 (NEARMISS C `src/func_001FFCD0.c`): the
block's bytes from +0x14 to +8 stream to the first cursor D_0028A73C, and
D_0028A740 = D_0028A73C + that length. Measured in every capture: D_0028A73C
= 0x1337500, D_0028A740 = 0x15D9500 = 0x1337500 + 0x2A2000. `configure()`
installs the flat builder as `export_area01_common.build_load_map`, so every
reused function gets it. D_00810701 is 0 in every capture (001FFCD0 state 7
clears it when +0x18 is 0), and the flat builder refuses any other value.

The resident region is 0x2A2000 bytes (2,760,704) at 0x1337500. Every
mapped byte equals RAM in every capture, except the cell directory's bytes,
which the directory check covers (176 differing 16-byte rows, all in the
directory).

**The file list is resident-relative; the extracted file names are not.**
The descriptor's file list (+0x1C entries of id << 24 | offset) is the list
001FFCD0 state 7 relocates (next paragraph), so its offsets count from the
resident offset +0x14, not from the block start. The decomp's
`tools/extract_data.py` cut `extract/chunk26/fII_idXX.bin` at the same
offsets read as block offsets, so every extracted file's name is shifted by
0xD9800 against the bytes it holds. Evidence, in every capture: RAM at
D_0028A490[id] equals the block at +0x14 + offset for all seven ids and
equals the start of the extracted file of that id for none of them (for
example RAM at D_0028A598 = 0x152E500 differs from `f02_id42.bin`, which
maps to 0x1454D00, inside the static bank). The extracted files are
contiguous slices of the block, so they serve as the byte source and the
bytes are unaffected; only their names are wrong. The load map is therefore
labelled by relocation id (`chunk26/idXX`), one or more rows per id, each
row naming the extracted file and offset its bytes are read from:

| Id | Word | Resident range | Size | Contents | Extracted byte source |
|---|---|---|---:|---|---|
| 0x43 | D_0028A59C | 0x1337500 .. 0x1366D00 | 0x2F800 | the model bank | `f01_id44.bin` +0xAA000 |
| 0x44 | D_0028A5A0 | 0x1366D00 .. 0x152E500 | 0x1C7800 | the static bank (all 481 objects) | end of `f01_id44`, `f02`..`f05` whole, start of `f06_id73` |
| 0x42 | D_0028A598 | 0x152E500 .. 0x1539D00 | 0xB800 | the collision grid | `f06_id73.bin` |
| 0x46 | D_0028A5A8 | 0x1539D00 .. 0x153D500 | 0x3800 | the cell directory | `f06_id73.bin` |
| 0x72 | D_0028A658 | 0x153D500 .. 0x155F500 | 0x22000 | not decoded here | `f06_id73.bin` |
| 0x71 | D_0028A654 | 0x155F500 .. 0x15D8500 | 0x79000 | not decoded here | `f06_id73.bin` |
| 0x73 | D_0028A65C | 0x15D8500 .. 0x15D9500 | 0x1000 | not decoded here | `f06_id73.bin` (its last 0x1000 bytes) |

Each id's size equals the size of the extracted file carrying its id for
0x43, 0x44, 0x42, 0x46, 0x72 and 0x71 (the same offsets, cut at the wrong
base). The last id differs: resident id 0x73 ends at the resident end
(0x1000 bytes), while `extract_data.py` ran `f06_id73.bin` to the block end
(0xDA800 bytes). `level.json` records each row's id, extracted source and
source offset. The checker's `label_problems` requires, in every capture, that the
rows tile D_0028A73C .. D_0028A740 without a gap and that each row labelled
idXX lies inside [D_0028A490[XX], the next relocated address); the map
labelled by the extracted names fails it (a control).

**Relocations and two unrelocated pointers.** 001FFCD0 state 7 applies the
descriptor's relocation words as D_0028A490[id] = D_0028A73C + offset. The
AREA22 list names ids 0x43, 0x44, 0x42, 0x46, 0x72, 0x71, 0x73; every one of
the seven words equals its formula in every capture (checked). The list names
neither 0x41 (D_0028A594, the area message bank) nor 0x45 (D_0028A5A4, the
dynamic list), and in every AREA22 capture those words hold 0x152E740 and
0x197D480, the values of the AREA04 capture a04_04 (checked when that
capture is present). So they are the previous area's words, not AREA22 data:
0x197D480 lies outside the AREA22 map, and 0x152E740 lies inside the AREA22
grid block (0x152E500 .. 0x15397FC).

## Outputs

| Output (`assets/area22/`) | What | Port loader |
|---|---|---|
| `sub0/level/00_id44.emdl` (+ `.gsmat.json`, descriptive) | level geometry, texels, GS state codes: the one zone, id 0x44 | `em_model_load` |
| `sub0/level/static_bank.emsc` | the static bank `*D_0028A5A0` (481 objects) | `em_script_image_load` |
| `sub0/area22.emcl` (+ `scene.txt` from export_collision.py) | grid collision with node class, rank and axis sections | `em_collision_load` |
| `sub0/area22_cells.bin` | the cell directory `*0x70003250` (disc rest state) | `em_actor_cells_load` (accepts it) |
| `sub0/roster.emro` | placements 0x823A20 and the deferred group 0x8236B0 | `em_actor_roster_load` |
| `sub0/message_data.emmd` | EMMD v1 for area 0x16: 54 global records, 0 area records, 36 stream rows, the global bank, no area bank | `em_message_live_install`, which **refuses** it (finding 2) |
| `sub0/world_models.emwm` + `.json` | the model bank `*D_0028A59C` (15 models) | `em_world_models_parse` |
| `sub0/sfx/area22_banks.bin`, `banks.json` | the area SShd container | (the registry's source) |
| `sub0/sfx/sfx_registry.emsr` + `.json` | EMSR v2 for scope (0x16, 0) | `em_sfx_registry_load` |
| `spawn_table.emsp` | the global spawn export; its area 0x16 rows are 0x24D4E0 (6 records) | `em_spawn_table_load` / `_read` |
| `door_destinations.emsp` | EMSP windows: `D_0024E140[0..0x17)` and the area 0x16 row 0x24E130 (4 records) | `em_spawn_table_load` / `_read` |
| `overlay_data.emsc` | the overlay data section 0x823600..0x823E00 | `em_script_image_load` |
| `sub0/level/level.json`, `sub0/cells.json`, `tables.json` | counts, hashes and the verification summary (no text, no data) | none |

There is no dynamic-list file, no scripts file and no `background.embg`
(sections below). The files sit under `sub0/` so that the layout matches the
earlier areas; AREA22 has one sub-state.

The checker does not check the descriptive side files: `scene.txt`, the
`.gsmat.json` files, `world_models.json`, `sfx_registry.json`, `banks.json`,
`level.json`. It does compare the recorded lists in `cells.json` (the moved
hulls per capture) and in `tables.json` (no script chain, no overlay-data
run-time word) with its own results.

## Tools

All the tools are pure Python and run on native arm64 macOS. Run them from
the port root.

```sh
python3 tools/export_area22_level.py    # level, textures, materials, collision, cells, ctx (~3 s CPU)
python3 tools/export_area22_tables.py   # roster, spawn, doors, overlay data, messages, world models (<1 s)
python3 tools/export_area22_sfx.py      # the container and the registry (~0.5 s)
python3 tools/test_area22_assets_reference.py                  # checker, 3 captures
EM_TEST_FULL=1 python3 tools/test_area22_assets_reference.py   # all 6 captures
EM_AREA22_ASSETS=<dir> python3 tools/test_area22_assets_reference.py   # check another export tree
```

Checker timings on the M1 (2026-09-28, after the fix round): quick 3.4 s CPU
(3.6 s wall), full 4.45 s CPU (4.6 s wall); the review measured 3.57 s and
4.68 s CPU before it. The first run after any `src/` header change adds a few seconds to
build the loader library (in `build/area22/assets/test/`).

**Reuse.** No earlier exporter or checker is edited; they are imported:
- `export_area01_common`: `LoadedImage`, `compare_load_map`, `_files`,
  `_extract_files`, `iso_descriptor`, `Capture`, `emsc`, `static_reader`;
- `export_area01_level`: the bank walk, `check_kicks` (with an empty dynamic
  list), `texture_localmem` (the decomp's `background_disc_localmem` already
  replays a flat descriptor's group-A upload), `build_zones`,
  `texture_alphas`, `write_zone`, `cell_directory`, `verify_cell_directory` /
  `derive_cell_image` (the ORIGINAL 001A2370 in the EE interpreter),
  `check_background`;
- `export_area02_level`: `run_collision` and `grid_extent`
  (export_collision.py over the grid section alone);
- `export_area01_tables`: `export_doors`, `match_nodes`, `pool_nodes`, the
  spawn-field helpers; `export_message_data`: its ELF reader, constants and
  `bank_extent`;
- `export_area02_tables`: `export_world_models` with its model-owner filter,
  `overlay_data_window`;
- `export_area01_sfx.bindings_from_captures`; `export_area02_sfx.block_bytes`;
  `export_area04_sfx.refusal_reasons`;
- from the AREA01 checker: `zone_problems`, `emsc_header_problems`,
  `gs_captures`, `gs_state_problems`, `bank_end`, `emcl_equal`,
  `grid_problems`, `hulls_inside_table`, `roster_image`, `roster_equal`,
  `roster_nodes`, `emsp_problems`, `spawn_layout`, `doors_layout`,
  `message_record_count`, `message_bank_size`, `MSG_BLOCKS`,
  `world_models_problems`, `registry_from_ram`, `load_map_problems`,
  `build_loaders`, `emsp_file`, `emsc_blob`, `mm_blob`, `quiet_fds`;
- through those: `export_level.py` and `export_collision.py` (decomp),
  `export_area11_roster`, `export_spawn_table`, `export_world_models`,
  `export_sfx_registry`.

Importing the AREA02 / AREA04 modules sets the shared module state to their
area, and `export_area02_level` installs AREA02's `owner_matrix`. Each AREA22
module therefore calls `configure()` again after its imports, and
`export_area22_level.install()` puts AREA22's `owner_matrix` back. A process
must not mix AREA22 work with another area's exporters.

AREA22's own code, and why:
- `build_load_map_flat`, `top_files` (common): the flat descriptor, labelled
  by relocation id;
- `relocations`, `relocation_problems` (level): the D_0028A490 words;
- `owner_matrix`, `NODE_D0_OWNERS` (level): the pickup and the drum;
- `ctx_rebuild`, `check_ctx_block` (level): the fog block needs 001D1C50
  too (below);
- `export_messages`, `message_blob` (tables): no area table;
- `spawn_rows`, `LOAD_ENTRY`, `nest_group`, `c_script_calls`,
  `run_time_words` (tables);
- `area_container` (sfx): section 0 of the flat descriptor;
- in the checker: the flat load, id-label (`label_problems`) and
  stale-pointer checks, the pinned descriptor hash, the level without a
  dynamic list, `messages_problems` for a file without an area table,
  `window_problems` without chains, `spawn_entry_problems` with the load
  entry, `RETRANSFORM_SITES` for the ELF and the AREA22 overlay, the message
  loader diagnostic.

## How each asset is derived and checked

**Level geometry.**
- The static bank D_0028A5A0 = 0x1366D00 is the start of id 0x44: 481
  objects, extent ..0x152E360, all inside id 0x44 (which ends at the grid,
  0x152E500). The grid at render ctx +0x140 (0x1367520, in object 0) names
  exactly objects 1..480, and every record has matrix slot 0. The zones are
  grouped by the id the objects lie in, so there is ONE zone EMDL:

  | Zone | Objects | Vertices | Triangles | Textures |
  |---|---:|---:|---:|---:|
  | 00_id44 | 480 (1..480) | 15,233 | 9,993 | 77 |

  (Before the fix round the zones were grouped by the extracted file names
  and split the bank at six boundaries the original does not have.)

- **No dynamic list.** D_0028A5A4 is not relocated at the AREA22 load (see
  above) and lies outside the map. `check_kicks` runs with an empty list: no
  capture draws a kernel-0x00237450 kick. The checker requires that no
  dynamic-list file exists and that D_0028A5A4 lies outside the map.
- Every textured level-kernel kick REFs units inside a bank object: 4,676
  kicks in the 6 captures (479 distinct REFs), all with the class-0 GS state.
- The texels come from the level-load GS upload of the flat descriptor's
  group-A section, replayed from the disc image. They equal the GS-freeze
  decode of every capture (77 textures x 6 captures = 462 comparisons).
- The checker requires each zone file to equal a rebuild from the exported
  bank byte for byte, and compares the bank with RAM over its full length
  and its length with `bank_end` over RAM.

**Collision.** The grid is D_0028A598 = 0x152E500 (relocation id 0x42,
0x152E500 .. 0x1539D00), and the cell directory (id 0x46) follows it
directly, so `export_collision.py --node-class` gets the grid section alone
(0x152E500 .. 0x15397FC, `grid_extent`; the rest of id 0x42 up to 0x1539D00
is not read) and its cell-list scan cannot take the directory for a cell
world. `--verify-ram
passes for every capture: 282 grid nodes, 536 grid vertices, 1,038 indices,
no cell n-gons; the EMCL has 484 vertices and 282 polygons. The checker
rebuilds the whole EMCL from each capture's RAM grid and requires the file
to equal it, pins D_0028A598, and requires the grid block to lie in the map,
outside the directory allowance.

**Cell directory.** It lies at 0x1539D00 (relocation id 0x46): 25 uids,
13,636 bytes. No uid word carries a flag bit (29..31). The file holds the
disc bytes; every byte of every capture's RAM directory is derived:
- **Hulls re-transformed by a live owner.** The ORIGINAL 001A2370 runs in the
  EE interpreter with the matrix the owner's code passes. AREA22's overlay
  has no owner code (its text is the entry pad and the area init,
  SIXTH_LEVEL_ROUTE.md section 2.1), so the live callers are boot ones:

  | Owner (node +0x10) | Matrix | Source | Hulls |
  |---|---|---|---|
  | 0x219550 (pickups g[0]..g[3]) | node + 0xD0 | as in AREA01 | 19 .. 22, moved in every capture |
  | 0x156620 (drums [14]..[16]) | node + 0xD0 | original call at 0x156EF0 (NEARMISS C `src/func_00156620.c`) | 0 .. 2, never moved (below) |

- **Every caller is named.** The pinned ELF and the AREA22 overlay hold
  exactly 3 calls of 001A2370 and no jump or address word naming it:
  0x156EF0 (the drum), 0x219668 (the pickup) and 0x21A104 (in 0x219F50,
  whose only caller is 0x219870 and whose matrix is the scratchpad copy
  0x700036A0). The checker re-counts them every run and fails on a live
  0x219870 node; no AREA22 capture has one.
- **The drums' hulls never move in AREA22.** Hulls 0..2 carry 0x4000 in their
  first prim, without the extended bit 0x800 that 001A2370 requires. The
  ORIGINAL 001A2370, run for each of the three drums with its matrix changed,
  leaves the whole directory equal to the disc. With the bit planted in a
  copy, the same run moves exactly the drum's hull, and the checker accepts
  it only because `owner_matrix` names the drum. All three drums are in
  state 1 in every capture.
- **The orphaned pickup hull.** a22_s0 took the document g[2]: its node
  (0x7A5C20) was freed and reused by a 0x18A6B0 node with uid 0, so hull 21
  has no live owner there. The AREA01 orphan rule accepts it: its bytes equal
  the derivation for the live g[2] in the other captures. a22_s0 alone is
  rejected ("no live node owns it"), with the arrival accepted.
- No uid word differs from the disc in any capture; `verify_cell_directory`
  compares every byte, the uid words included.

**Background and the fog block.**
- No capture arms the background: render ctx +0x174 is 0 in all 6.
- The ctx bytes +0xA0..+0xFF are three 0x20-byte fog records. The earlier
  lanes rebuilt them with the ORIGINAL 001D8FD0 alone
  (`export_area01_level.check_ctx_room_block`). In AREA22 that fails from
  door [7] on: spawn entries 1, 3, 4 and 5 carry bit 0x80 in word +0x1C (entry
  2 does not; D_008106C8 = 0x108080 after door [7]), and on that bit the per-frame 001D1C50 (NEARMISS
  C, logic recovered: D_008106C4 == 0, D_008106C8 & 0x80, D_008106C7 == 0
  and not (0015D2F0() == 2 and D_008106C6 == 2)) calls 0021B970(0, 50) and
  0021BA80(8, 8, 0x15), which rewrite +0xA0..+0xBF and copy them to +0xC0
  (0021B900), leaving 001D8FD0's +0xE0 record.
- So the ORIGINAL 001D8FD0 and then the ORIGINAL 001D1C50 run over each
  capture with the block overwritten, and rebuild all 96 captured bytes in
  all 6 captures. 001D1C50 changes 14 bytes of 001D8FD0's result from door
  [7] on and none in the arrival (D_008106C8 = 0x100000, its other branch).
- Dependencies, checked per capture: in the arrival, flipping room entry 44
  (key 0x1600) changes 66 bytes; from door [7] on, the room entry is not read
  (0 bytes), the bit 0x80 changes 33 and D_008106C7 changes 18.
- The values come from the global `assets/render_context.emrc`, whose blocks
  equal RAM in every capture. There is no AREA22-specific file.

**Roster.**
- Placement table 0x823A20 (21 records) and the deferred group 0x8236B0 (19),
  the one group 001B6910's list D_0024D820[0x16][0] names.
- The group 0x823600 (3 records; area_overview's "nest group" at
  D_0024D820[0x16][nest base], nest base = s16 D_0024A850[0x16], 0 read as 1)
  is not on that list, so it is not in the EMRO; its bytes are in
  `overlay_data.emsc` and equal RAM in every capture. Which code walks it is
  not identified here.
- The group and placement bytes equal RAM in every capture. The checker
  rebuilds the EMRO by the original's walks over the pinned ELF + overlay and
  over each capture's RAM, and requires them to be equal. All 21 placement
  nodes are live and at their record's position in every capture, and keep
  +0x03, +0x0D and +0x54.

**Spawn and doors.**
- `spawn_table.emsp` is byte-identical to `assets/spawn/spawn_table.emsp`.
  D_0024D650[0x16] = 0x2755E0 names one room array; its entries run from
  0x24D4E0 to the next table or room array the ELF names: 6 records.
- D_008106C8 is set from word +0x1C of the spawn record at a room load. In
  a22_s1 and a22_s2 the entry byte D_00810702 is 3 (the ladder climb writes
  it at the top-out without a room load, SIXTH_LEVEL_ROUTE.md section 2.2),
  and D_008106C8 is 0x108080, which is not entry 3's word (0x108081). The
  word alone does not name the entry: entries 1 and 5 both carry 0x108080
  (the six words are 0x100000, 0x108080, 0x108000, 0x108081, 0x108081,
  0x108080). Entry 1 is identified by the entry byte D_00810702 = 1 in the
  captures after door [7]'s room load (a22_00, a22_01, a22_s0) and by the
  route record (SIXTH_LEVEL_ROUTE.md: no room load between door [7] and the
  ladder). So the check uses the entry of each capture's last room load
  (`LOAD_ENTRY`: 0 for the arrival, 1 for the rest), and a control shows the
  entry byte would fail; an entry-5 load would give the same word and is
  not excluded by the word check alone.
- The door row is 0x24E130 (4 records, 16 bytes, up to the pointer array
  0x24E140). The doors the placements use:

  | Placement | Behaviour | Door id | Record |
  |---|---|---|---|
  | [6] | 0x1BC350 | 0 (area change) | 04 01 00 00 |
  | [7] | 0x1BB860 | 1 | 01 02 00 00 |
  | [8] | 0x1BB860 | 2 (area change) | 01 06 00 00 |
  | [10] | 0x1BB860 | 3 | 03 04 00 00 |

**Overlay data, no scripts.**
- The committed AREA22 overlay C (`src/overlays/AREA22/`) calls
  func_001BA1A0 nowhere, so there are no script chains and no scripts file.
  The checker re-reads the C every run.
- The overlay data window is the module's data section (MWo3 header word 4
  = 0x800), 0x823600..0x823E00. Every word equals the disc module in every
  capture: AREA22 has no run-time word.

**Messages.**
- D_00264DD0[0x17] (area 0x16 + 1) is 0 in the ELF and in every capture:
  AREA22 has no area message table. The descriptor has no id-0x41 file, so
  D_0028A594 keeps AREA04's bank address.
- `export_message_data.export` refuses a zero table pointer, so
  `message_blob` assembles the same EMMD v1 layout from the same ELF reads
  and bank walk, with the area record count and area bank size 0: 54 global
  records, 36 stream rows, the global bank (3,029 bytes).
- The checker compares every byte with RAM in every capture: the header (area
  0x16, 0 / 0), the three ELF blocks, the global records, D_00264DD0[0x17] =
  0, the stream rows up to the -1 row, the global bank at *D_0028A4E8, and the
  count and bank size against the original's own bounds. No text is printed
  or stored outside the ignored asset.

**World models.** *D_0028A59C = 0x1337500 (relocation id 0x43, the first
resident byte, block +0xD9800; its bytes are read from the extracted
`f01_id44.bin` + 0xAA000, a name shifted by the resident offset). 15 models,
194,000 bytes, inside id 0x43 (0x2F800 bytes). The span
equals RAM, and `wm_span` over each capture's RAM gives the same extent.
Every model owner whose +0x44 names a bank model has +0x44 =
001C6120(table, +0x0D), its bone count and every bone slot set; 19 owners
are bound in every capture.

**Sound banks.**
- The area container is section 0 (block +0 .. +0x51000) of the flat
  descriptor: an SShd container of 329,984 bytes with 3 banks, none of it
  resident (the resident region starts at +0xD9800). No relocation id owns
  these bytes (the file list describes the resident region only); they are
  read from the extracted `f00_id43.bin` and the start of `f01_id44.bin`,
  whose names do not apply to them (`banks.json` records them as
  `container_byte_sources`).
- The bindings are the same in every capture:

  | Group 1 | Group 2 | Group 4 | Refused |
  |---|---|---|---|
  | global rows 0..2 | area row 0 | slot 0: area row 1; slot 1: area row 2 | group 3 slot 0 |

  Group 3 slot 0 names handle 3, whose record is in use with header
  0x1800030, which lacks the SShd magic (as in the earlier areas).
- Registry for scope (0x16, 0), 1,000 ids: 426 audible, 515 absent, 59
  UNSUPPORTED ("unbound bank"), 33 samples. The checker re-derives every
  byte of the registry from each capture's RAM (the samples from the disc
  container) and compares `area22_banks.bin` with the disc container.

## Checker, canary, controls and the mutation sweep

`tools/test_area22_assets_reference.py`:
- **loaders:** every file goes through its port loader, compiled privately
  from `src/` by the AREA01 checker's `build_loaders`: 10 files load,
  `sub0/area22_cells.bin` included. `em_message_live_install` refuses
  `sub0/message_data.emmd` (finding 2); a diagnostic copy with one all-zero
  area record and the global bank repeated as an area bank loads (built in
  the test build tree and removed). Both verdicts are asserted.
- **real checks:** every section above, over the run's captures. Quick mode
  uses the arrival, a22_s0 (the orphaned hull, the descriptor buffer reused)
  and a22_s1 (the entry byte rewritten by the ladder). Full mode uses all 6.
- **canary (47 sections; 52 before the fix round, the 5 fewer are the
  loader refusals of the 5 zone files that no longer exist).** `run_checks` runs again over [the arrival, a
  planted copy of it], with the copy second. Each per-capture comparison must
  report its plant for the copy and stay silent for the original.
  - RAM plants: a static-bank padding byte (the bank's last byte is VIF data
    the kick walker parses), an object header (extent), D_0028A5A0,
    D_0028A5A4 moved into the map, D_0028A594, relocation word 0x72, TEST_1 of
    the level state, ctx +0xA4, a directory hull, uid 1's bit 30, the grid,
    the placement table and a placement node, the door row, the spawn rows,
    a global message record, D_00264DD0[0x17] set, the model bank, the pitch
    ladder the registry reads, a pickup's matrix, a model owner's +0x0D,
    D_008106C8 and the GS freeze texels.
  - File plants: an extra zone file, `cells.json`, the cells file, the
    container, the roster, the overlay data, the message file,
    `tables.json` (a run-time word and a chain), and, in their own pass, a
    zone EMDL and a dynamic-list file.
  - Each port loader must refuse a broken copy of its file.
- **controls (162 changed inputs in quick mode, 165 in full mode; 146
  before the fix round):** the file side and the RAM side of every
  comparator, plus the accept cases:
  - every moved hull, and each live pickup's matrix;
  - the orphan: a22_s0 alone (rejected), with the arrival (accepted), and
    the orphaned hull changed (rejected);
  - the drums: each drum with its matrix changed leaves the directory as on
    disc (ORIGINAL 001A2370); with the 0x800 bit planted, a knocked drum's
    derived hull is accepted, the same hull with the matrix at rest rejected,
    and rejected by an owner list without the drum;
  - the 001A2370 census (accepted), an extra call site, a live 0x219870 node;
  - owner_matrix unit cases (pickup and drum node + 0xD0; 0x219870,
    0x219F50, 0x15AFA0 and the AREA04 / AREA01 overlay owners None);
  - the overlay data: a byte at the window's first and last address, in
    the placement table and in the nest group (rejected), every capture
    (accepted), file variants;
  - no chain in the C (accepted), a planted func_001BA1A0 call (rejected);
  - the message file: each unit one short, a global record counted as an area
    record, an area bank appended, the last byte of each of the three ELF
    blocks in the file and in RAM, one trailing byte (the length check
    itself must report it), the area word, an area pointer in RAM, a global
    bank byte, the -1 row cleared; every capture (accepted);
  - the unrelocated words (accepted; each changed, rejected);
  - the spawn word: the load entries (accepted); a22_s1 with its entry byte
    as the load entry, the entry byte past the rows, sub 1, an unrecorded
    capture (rejected);
  - the id labels: the map as built in every capture (accepted); the map
    labelled by the extracted files' names, the grid row labelled id 0x44,
    a row dropped from the middle, the last row dropped (rejected);
  - the descriptor: its SHA-256 is pinned; a copy whose first list entry
    relocates id 0x41, and one relocating id 0x45 (rejected by the stale
    model);
  - the load map: first and last bytes of the first and last row, the
    bytes next to the directory, the second cursor off the resident length,
    both cursors moved, the sub byte 1, a relocation word, the scratchpad
    directory pointer;
  - the door loader: the file (accepted), without the pointer array or the
    row (refused); the message-loader verdicts;
  - the ctx block: a byte of the first, second and third record in the
    arrival and in a22_s1, D_008106C7 set (rejected), and 001D8FD0 alone
    must not rebuild a22_s1's block;
  - the zone file, the GS state, the bank file variants, the sound bindings
    (a slot unbound, in the second capture only, another row, one missing,
    nothing / one more refused), the EMCL bytes, the grid pointer and a node,
    the directory allowance over the grid, clean copies (accepted), a bank
    record with a matrix slot.

**Mutation sweep (one bounded sweep, this lane).**
- 48 single-operation mutants over the checker's comparators and the AREA22
  exporter functions it calls (`build_load_map_flat`, `relocation_problems`,
  `owner_matrix` / `NODE_D0_OWNERS`, `ctx_rebuild`, `run_time_words`). Each
  ran in quick mode in a private copy of `tools/`, 4 workers
  (`build/area22/sweep/sweep.py`, ignored; 80 s wall).
- First pass: 47 killed, 1 survived, none only by a crash. M35 (the load
  entry replaced by the entry byte) is killed by the real a22_s1 check, not
  by a control.
- The survivor, M26 (the message check tests the area record count only, not
  the area bank size), is killed by the control "an area bank appended"
  added in this lane (re-applied alone: killed).
- Final state: 48 of 48 killed in quick mode. The claim covers exactly these
  mutants; the reused AREA01 / AREA02 / AREA04 comparators keep their own
  sweep records and known gaps.

**Review sweep and the fix round.** The review's own sweep (42 mutants)
left 3 non-equivalent survivors. Each now has a pinned control and was
re-applied alone in quick mode (`build/area22/sweep/sweep.py R20 R18 R03
L01`; not a new sweep):
- R20 (the three ELF blocks compared without their last byte): killed by
  "the last byte of message block ..." (file and RAM, all three blocks).
- R18 (the length check weakened to `<`): killed by "a trailing byte on the
  message file", which requires the length check's own text.
- R03 (the "descriptor relocates id 0x41 / 0x45" guard disabled): killed by
  the planted-descriptor controls; the disc descriptor is also hash-pinned.
- L01 (the fix-round regression: rows labelled by the extracted files'
  names): killed by `label_problems` in the real checks.
None was killed only by a crash. The review's other survivors are
equivalent or redundant, as the review recorded: R04 and R15 (the EMSC
header check and the loader catch the same input), R09 (the cells file must
equal the disc bytes), R19 (the ELF is SHA-pinned and RAM is checked), R26
(the `len != total` check catches it), R33 (an oracle-dependence
assertion; by_c7 = 18 in every capture), R34 (without the 0x5A pre-fill the
result is identical, since the originals write all 96 bytes) and X01 (the
static bank's last 4 bytes are already zero).

## Findings (for the lead)

1. **AREA22's level block is flat.** No nested descriptor; the top
   descriptor carries the upload sections and the resident offset. The
   earlier areas' `build_load_map` refuses it; `build_load_map_flat` models
   001FFCD0 state 5.
2. **No area message table.** D_00264DD0[0x17] = 0 and no id-0x41 file.
   `em_message_live_install` requires at least one area record and a
   non-empty area bank, so it refuses the AREA22 file. The in-area messages
   seen on the route (the reader, door [10]) are then global or script
   messages; which table they index was not established here.
3. **Two unrelocated pointers.** D_0028A594 and D_0028A5A4 keep AREA04's
   values (0x152E740, 0x197D480). 0x152E740 lies inside the AREA22 grid
   block: any area-bank read in AREA22 would read grid bytes. A port that
   clears or re-derives these words at an area load would differ from the
   original in these two words.
4. **The fog block needs 001D1C50.** From door [7] on (spawn word bit 0x80),
   001D8FD0 alone does not give the captured ctx +0xA0..+0xDF; 001D1C50's
   fixed (0, 50) and colour (8, 8, 0x15) do. The earlier areas' check
   (001D8FD0 only) passed because their captures never had the bit.
5. **The entry byte and D_008106C8 can disagree.** The ladder top-out writes
   D_00810702 = 3 without a room load; D_008106C8 keeps the word of the last
   room load (0x108080, not entry 3's 0x108081). That word is shared by
   entries 1 and 5; entry 1 comes from the entry byte before the ladder and
   the route record, not from the word.
6. **59 sound ids resolve to records that name no bound group**; the
   registry marks them UNSUPPORTED ("unbound bank"), as AREA04 finding 6.
7. **The drum in the owner lists (A04ASSETS lead note).**
   `export_area22_level.owner_matrix` includes the drum 0x156620 (node +
   0xD0). The earlier exporters' owner lists that still omit it (this lane
   does not edit them): `export_area01_level.owner_matrix` (0x219550,
   0x8261A0, 0x826D40), `export_area00_level.owner_matrix` (0x219550,
   0x825600, 0x8263C0) and `export_area02_level.owner_matrix` (0x219550,
   0x825100, 0x823930 kind 4 / +0x0D 7). `export_area04_level` includes it.
   None of them models 0x219F50's scratchpad matrix. Whether the drum hulls
   of AREA01, AREA00 or AREA02 carry the 0x800 bit is not checked here.
8. **Extracted file names are shifted by the resident offset (systemic).**
   In every level block checked, the file list is resident-relative:
   001FFCD0 state 7 relocates D_0028A490[id] = cursor + offset. Checked
   (build/area22/fix/probe_nested.py, ignored) on the relocation words of
   the first and last AREA01 capture, sub 0 (+0x14 = 0x14D800), and of the
   last capture of each other earlier level block: AREA00 subs 0 and 1 (0x173800), AREA02 sub 0
   (0x14B000) and sub 1 (0x82000), AREA04 sub 0 (0x143800); every listed
   word equals the nested cursor D_0028A740 + offset, none equals cursor +
   offset - resident. The decomp's `tools/extract_data.py` cuts the files
   at those offsets read as region offsets, so for every region with +0x14
   != 0 the `fII_idXX.bin` names are shifted, and the earlier exporters'
   nested load-map labels (`export_area01_common.build_load_map`, used by
   the AREA01 / AREA00 / AREA02 / AREA04 exporters) and their zone EMDL
   names (for example AREA04's `00_f01_id44`, `01_f02_id45`, `02_f03_id42`)
   carry the shift. Their bytes are unaffected (contiguous slices). This
   lane edits none of them; relabelling them, and giving `extract_data.py`
   the resident offset, is for the lead.

The census delta (`../Extermination/build/s87/census/a22_delta.json`, 11 new
boot functions) names none of the load-path functions these exports model
(001FFCD0, 001B6910, 001B6990, 001C6120, 001A2370, 001D8FD0, 001D1C50).

## Binding

Nothing is wired. The files are exported, checked and loadable, except
`sub0/message_data.emmd` (finding 2). When AREA22 is bound:
- The scene selects `sub0/` for the level, collision, cells, roster, world
  models, message file and sound files; the spawn table, doors and overlay
  data are shared.
- The level is one zone, `sub0/level/00_id44.emdl` (the whole static bank);
  a binding must not expect one zone per extracted file.
- The load must reproduce the flat layout: the resident region at the first
  cursor, and D_0028A490[id] only for the seven listed ids, leaving
  D_0028A594 and D_0028A5A4 as the previous area left them.
- The live directory starts from `sub0/area22_cells.bin`. The pickups
  0x219550 and the drums 0x156620 call a translated 001A2370 with node + 0xD0
  (the port has `em_actor_cells_retransform_001A2370`); the translation must
  keep the 0x800 gate (the drums' hulls lack it). A thrown pickup (0x219870 ->
  0x219F50) passes the scaled scratchpad copy instead; that path is not in any
  AREA22 capture.
- There is no dynamic list and no script chain.
- The message service needs either a loader that accepts an area with no
  area table or a decision on what AREA22's area lookups do (finding 2).
- The ctx block is evaluated at run time by 001D8FD0 at a room load and by
  001D1C50 every frame (finding 4), from `assets/render_context.emrc`.

## Known gaps

1. **The message file is not loadable** by the current port loader
   (finding 2).
2. **The nest group 0x823600** is exported only as overlay-data bytes; the
   code that walks it is not identified.
3. **Not exported:** shadow receivers, owner object textures, and door, prop
   and pickup models (as for the earlier areas); the room behind door [10]
   was not entered, so its owners appear only at rest.
4. **SPU residency** of the samples: there is no AREA22 SPU capture.
5. **The 59 UNSUPPORTED sound ids** (finding 6) are not resolved.
6. **Descriptive side files** are not checked (see Outputs).
7. **The 0x219870 / 0x219F50 matrix** is not modelled by `owner_matrix`; the
   checker fails on a live 0x219870 node rather than guess.
8. **A moved drum hull** is proved only on a planted directory (the 0x800
   bit set): no AREA22 drum hull can move, and no capture has a drum in
   state 2.
9. **The 001D1C50 branch** is selected by the original instructions in the
   EE interpreter; only two of its paths show in the captures (bit 0x80 clear
   with 3B8D = 0, and bit 0x80 set with D_008106C7 = 0).
10. **Ids 0x72, 0x71 and 0x73** (0x153D500 .. 0x15D9500) are not decoded;
    their bytes are only checked equal to RAM through the load map.
11. **The earlier areas' labels** (finding 8) still carry the extraction
    shift; only AREA22 is labelled by relocation id.
