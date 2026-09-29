# AREA06 assets: local export and capture checks

Lane "A06ASSETS", level side track (seventh level), 2026-09-28. This lane adds
new files only: four exporters, one checker and this document. It changes no
port source, no tracked port file and no decomp file, and nothing it adds is
wired into the game. Every exported byte comes from the user's own disc files
and ELF and is written into the ignored `assets/area06/` tree. Every export is
checked against the recorded original captures of AREA06
(SEVENTH_LEVEL_ROUTE.md). Nothing was run in PCSX2.

The captures (8, every one with RAM, scratchpad and GS freeze):
- the AREA06 arrival, the end of beat a01u_02
  (`../Extermination/build/s87/route_a01u/a01u_02_progression_exit/`);
- a06_00 .. a06_05 and the side beat a06_s1 (`../Extermination/build/s87/route_a06/`).

a06_s0 ends in AREA01 (area bytes 01 00 07) and is excluded by its area byte;
the checker asserts that it is the only excluded folder. Every AREA06 capture
is sub 0 (D_00810701 = 0; door [2] is a room move inside the sub). Measured,
not assumed: `loaded_sub_proof.json` has, per capture, 337..355 differing
16-byte rows for the sub-0 map (all inside the cell directory) against 286,226
for sub 1. Sub 1 (`chunk10.n1`) is loaded by no capture and is not exported.

## The load map is labelled by relocation id

INDEX.IDX sector 10 is a nested descriptor, like AREA01 / AREA00 / AREA02 /
AREA04 (unlike AREA22): a top block with one file, id 0x41 (resident offset 0,
at D_0028A73C = 0x1337500), and one nested block per sub. Nested block 0 has
upload section 0 (block +0 .. +0xAB800, the sound container), one group-A
section (+0xAB800, 0xB8800 bytes, the GS texel upload) and resident offset
+0x14 = 0x164000; its resident region, 0x437000 bytes, lies at D_0028A740 =
0x133A1C0.

AREA22_ASSETS.md finding 8 applies here: the nested file list (id << 24 |
offset) is the list 001FFCD0 state 7 relocates as D_0028A490[id] =
D_0028A740 + offset, so its offsets count from +0x14, while the decomp's
`tools/extract_data.py` cut `extract/chunk10.n0/fII_idXX.bin` at the same
offsets read as block offsets. `export_area06_common.build_load_map_ids` keeps
the AREA01 builder's rows and bytes (the extracted files are contiguous slices
of the block) and relabels every nested row by relocation id, splitting rows
at id boundaries; each row still names the extracted file and offset its bytes
come from (`level.json` `load_map`):

| Id | Word | Resident range | Size | Contents | Extracted byte source |
|---|---|---|---:|---|---|
| 0x41 (top) | D_0028A594 | 0x1337500 .. 0x1337D00 | 0x800 | the area message bank | `chunk10/f00_id41.bin` (name correct: offset 0) |
| 0x42 | D_0028A598 | 0x133A1C0 .. 0x135C9C0 | 0x22800 | the collision grid | `f02_id44.bin` +0x138000 |
| 0x46 | D_0028A5A8 | 0x135C9C0 .. 0x13661C0 | 0x9800 | the cell directory | `f02_id44.bin` +0x15A800 |
| 0x44 | D_0028A5A0 | 0x13661C0 .. 0x16361C0 | 0x2D0000 | the static bank (886 objects) | end of `f02_id44`, `f03`..`f09` whole, start of `f10_id7a` |
| 0x45 | D_0028A5A4 | 0x16361C0 .. 0x16471C0 | 0x11000 | the dynamic list (32 entries) | `f10_id7a.bin` +0x2A000 |
| 0x43 | D_0028A59C | 0x16471C0 .. 0x16A69C0 | 0x5F800 | the model bank (26 models) | `f10_id7a.bin` +0x3B000 |
| 0x71, 0x7C, 0x72, 0x79, 0x73, 0x7A | D_0028A490[id] | 0x16A69C0 .. 0x17711C0 | 0xCA800 | not decoded here | `f10_id7a.bin` |

In every capture all twelve relocated words (0x41 from the top list, eleven
from the nested list) equal their cursor + offset (`relocation_problems`), and
every mapped byte equals RAM except the cell directory's own 37,844 bytes (the
allowance from the directory's address to its size, derived by the directory
check below). 4,421,632 bytes are mapped per capture (0x437000 nested + 0x800
top, including the directory's 0x9800 row), so 4,383,788 of them are compared
equal. The checker's `label_problems` requires that the nested rows
tile from D_0028A740 without a gap and that a row labelled idXX lies inside
[D_0028A490[XX], the next address D_0028A490 holds for any id of the nested
list); the map labelled by the extracted names fails it (a control). The
descriptor is hash-pinned.

## Outputs

| Output (`assets/area06/`) | What | Port loader |
|---|---|---|
| `sub0/level/00_id44.emdl` (+ `.gsmat.json`, descriptive) | level geometry, texels, GS state codes: the static bank, id 0x44 | `em_model_load` |
| `sub0/level/static_bank.emsc` | `*D_0028A5A0` (886 objects) | `em_script_image_load` |
| `sub0/level/dynamic_objects.emsc` | `*D_0028A5A4` (32 entries) | `em_script_image_load` |
| `sub0/area06.emcl` (+ `scene.txt` from export_collision.py) | grid collision with node class, rank and axis sections | `em_collision_load` |
| `sub0/area06_cells.bin` | the cell directory `*0x70003250` (disc rest state) | `em_actor_cells_load` (accepts it) |
| `sub0/roster.emro` | placements 0x827AC0 (57) and the deferred group 0x826000 (33) | `em_actor_roster_load` |
| `sub0/message_data.emmd` | EMMD v1 for area 6: 54 global and 22 area records, 36 stream rows, both banks | `em_message_live_install` (accepts it) |
| `sub0/world_models.emwm` + `.json` | the model bank `*D_0028A59C` (26 models, 390,128 bytes) | `em_world_models_parse` |
| `sub0/sfx/area06_banks.bin`, `banks.json` | the sub's SShd container (701,200 bytes, 4 banks) | (the registry's source) |
| `sub0/sfx/sfx_registry.emsr` + `.json` | EMSR v2 for scope (6, 0) | `em_sfx_registry_load` |
| `spawn_table.emsp` | the global spawn export (byte-identical to `assets/spawn/spawn_table.emsp`); its area 6 sub-0 rows are 0x24C0D0 (4 records) | `em_spawn_table_load` / `_read` |
| `door_destinations.emsp` | EMSP windows: `D_0024E140[0..0x17)` and the area 6 row 0x24E028 (6 records) | `em_spawn_table_load` / `_read` |
| `area06_scripts/scripts.emsc` | the four chains' window 0x826D40 .. 0x827AC0 (52 records) | `em_script_image_load` |
| `overlay_data.emsc` | the overlay data section 0x826000 .. 0x828F00 | `em_script_image_load` |
| `sub0/level/level.json`, `sub0/cells.json`, `tables.json`, `loaded_sub_proof.json` | counts, hashes and the verification summary (no text, no data) | none |

The checker does not check the descriptive side files (`scene.txt`, the
`.gsmat.json` file, `world_models.json`, `sfx_registry.json`, `banks.json`,
`level.json`, `loaded_sub_proof.json`); it compares the recorded lists in
`cells.json` (moved hulls and 0019C6F0 calls per capture) and in
`tables.json` (script chains, run-time words) with its own results.

## Tools

All tools are pure Python on native arm64 macOS; run them from the port root.

```sh
python3 tools/export_area06_level.py    # level, textures, collision, cells, ctx (~7 s CPU)
python3 tools/export_area06_tables.py   # roster, spawn, doors, scripts, overlay data, messages, world models (<1 s)
python3 tools/export_area06_sfx.py      # the container and the registry (~1 s)
python3 tools/test_area06_assets_reference.py                  # checker, 3 captures
EM_TEST_FULL=1 python3 tools/test_area06_assets_reference.py   # all 8 captures
EM_AREA06_ASSETS=<dir> python3 tools/test_area06_assets_reference.py   # check another export tree
```

Checker timings on the M1 (2026-09-28, round-7 close): quick 7.4 s CPU
(7.4 s wall), full 10.0 s CPU (10.0 s wall). The first run after a `src/` header change adds the
private loader build (in `build/area06/assets/test/`).

**Reuse.** No earlier exporter or checker is edited; they are imported:
- `export_area01_common`: the AREA01 nested `build_load_map` (wrapped, not
  replaced), `LoadedImage`, `compare_load_map`, `_files`, `iso_descriptor`,
  `Capture`, `emsc`, `static_reader`;
- `export_area01_level`: the bank and dynamic-list walks, `check_kicks`,
  `texture_localmem`, `build_zones`, `texture_alphas`, `write_zone`,
  `cell_directory`, `derive_cell_image` (the ORIGINAL 001A2370),
  `check_background`;
- `export_area02_level`: `run_collision` / `grid_extent`, `derive_words` (the
  ORIGINAL 0019C6F0, with AREA06's `flag_calls` installed), `orphan_problems` /
  `written_words`, `pool`;
- `export_area22_level`: `check_ctx_block` / `ctx_rebuild` (the ORIGINAL
  001D8FD0 and 001D1C50), `drum_rows`;
- `export_area01_tables`: `export_doors`, `export_messages`, `walk_chain`,
  `match_nodes`, the spawn-field helpers; `export_area02_tables`:
  `export_world_models` with its model-owner filter, `overlay_data_window`,
  `area_bank`, `bank_source`;
- `export_area02_sfx.area_container`, `export_area04_sfx.refusal_reasons`,
  `export_area01_sfx.bindings_from_captures`;
- from the AREA01 checker: `zone_problems`, `emsc_header_problems`,
  `gs_captures`, `gs_state_problems`, `level_bank_problems`, `emcl_equal`,
  `grid_problems`, `hulls_inside_table`, `roster_image`, `roster_equal`,
  `roster_nodes`, `emsp_problems`, `spawn_layout`, `doors_layout`,
  `messages_problems`, `world_models_problems`, `registry_from_ram`,
  `load_map_problems`, `build_loaders`, the file and blob helpers;
- through those: `export_level.py`, `export_collision.py` (decomp),
  `export_area11_roster`, `export_spawn_table`, `export_world_models`,
  `export_sfx_registry`, `export_message_data`.

Importing the AREA02 / AREA04 / AREA22 modules sets the shared module state to
their area (AREA22's replaces `build_load_map` with its flat builder), so each
AREA06 module imports `export_area06_common` first (it keeps the AREA01 nested
builder and refuses to import after another area replaced it) and calls
`configure()` after the other imports; `export_area06_level.install()` puts
AREA06's `owner_matrix` and `flag_calls` back. A process must not mix AREA06
work with another area's exporters.

AREA06's own code: `build_load_map_ids`, `relocation_problems`,
`relocation_lists`, `descriptors` (common); `owner_matrix`, `flag_calls`,
`derive_flag_image`, `derive_scaled_hull`, `verify_directory`, `check_cells`
(level); `export_roster`, `export_spawn`, `run_time_words`, `doors_used`
(tables); in the checker the id labels, the relocation words, the pinned
descriptor, the beam's +0x0D rule, the three call-site censuses and the
AREA06 controls.

## How each asset is derived and checked

**Level geometry.**
- The static bank D_0028A5A0 = 0x13661C0 is the start of id 0x44: 886 objects,
  extent .. 0x1635C20, inside id 0x44. The grid at render ctx +0x140 (in object
  0) names exactly objects 1..885, and every record has matrix slot 0. The
  zones are grouped by relocation id, so there is ONE zone EMDL, `00_id44`:
  885 objects, 43,712 records, 20,861 vertices, 12,765 triangles, 82 textures.
- The dynamic list D_0028A5A4 = 0x16361C0 (id 0x45) holds 32 entries.
- Every textured level-kernel kick REFs units inside a bank object (4,166 kicks
  in the 8 captures, 538 distinct REFs), all with the class-0 GS state; every
  kernel-0x00237450 kick (158) REFs a dynamic-list entry.
- The texels come from the level-load GS upload of the nested descriptor's
  group-A section, replayed from the disc image; they equal the GS-freeze
  decode of every capture (82 textures x 8 captures = 656 comparisons). The
  checker rebuilds the zone file from the exported bank byte for byte.

**Collision.** The grid is D_0028A598 = 0x133A1C0 (id 0x42); the cell
directory (id 0x46) follows it, so `export_collision.py` gets the grid
section alone (`run_collision`). `--verify-ram` passes for every capture: 884
grid nodes, 1,319 grid vertices, 3,296 indices, no cell n-gons; the EMCL has
1,273 vertices and 884 polygons. The checker rebuilds the EMCL from each
capture's RAM grid and requires the file to equal it.

**Cell directory** (0x135C9C0, id 0x46): 51 uids, 37,844 bytes. uid 0's word
carries bits 31 and 30 (0xC00000D0 on disc), no word carries bit 29. The file
holds the disc bytes; every byte of every capture's RAM directory is derived:
- **The uid words (0019C6F0).** [9] (0x823580, `overlay_AREA06_func_00823540.c`,
  byte-identical C) calls 0019C6F0(0xD, 1) in the frame its sub-state +5
  becomes 1 and 0019C6F0(0xD, 0) in the frame it returns to 0; the ELF has no
  call and the module no other. Key 0xD selects uid 0; (0xD, 1) clears bit 30,
  (0xD, 0) sets it. `flag_calls` takes the last call from the live node's +5
  and the ORIGINAL 0019C6F0 runs it over the capture: +5 = 1 in a06_01, a06_04,
  a06_05, a06_s1 (uid 0 = 0x800000D0 in RAM), +5 = 0 in the other four (the
  disc word). With +5 = 0 the checker also requires that (0xD, 0) leaves the
  disc directory unchanged, so "(0xD, 0) was last" and "no call yet" agree.
  The model is not complete: [9]'s state 0 (its spawn) sets +5 = 0 without
  calling 0019C6F0, so a respawn of [9] after (0xD, 1) in the same load would
  leave bit 30 clear with +5 = 0. The model then derives the disc word and
  the checker reports a directory mismatch; it fails closed. No capture has
  that history.
- **Hulls re-transformed by a live owner** (moved in every capture: 26, 28,
  45..48):

  | Owner (node +0x10) | Hulls | How the checker derives them |
  |---|---|---|
  | 0x219550, pickups g[0]..g[3] | 45..48 | ORIGINAL 001A2370(node, node + 0xD0) |
  | 0x824560, the beam [11] | 28 (uid 0x1C) | ORIGINAL 001A2370(node, node + 0xD0); its three calls pass self + 0xD0 (byte-identical C `func_overlay_AREA06_00824520.c`) |
  | 0x219870, g[11] | 26 | the ORIGINAL 0x219F50 run whole on the node over the capture (below) |
  | 0x156620, drums [13]..[16] | 4..7 | never moved: all four are in state 1 in every capture |

- **The 0x219870 node (g[11]).** Its NEARMISS C calls 0x219F50 in state 0
  only (that C's 001B0FD0 / 001AFC10 arguments were corrected against the
  original instructions on 2026-09-28: both take the node, not the state
  byte; the 0x219F50 call is unaffected); 0x219F50 (byte-matched C,
  `src/func_00219F50.c`, linked from its compiled object) copies node + 0xD0
  to the scratchpad 0x700036A0,
  scales its third row by a length it computes (with a hit test, 0019A570) and
  calls 001A2370 with that copy. No matrix model is used: the checker runs the
  ORIGINAL 0x219F50 on the node in the EE interpreter over each capture, with
  the directory at disc, and it reproduces hull 26 exactly and touches no other
  hull (the node's +0xB0 and +0xD0 are unchanged since its spawn). The earlier
  checkers failed on any live 0x219870 node; AREA06 is the first capture set
  with one, and the census below keeps 0x219F50's only caller pinned.
- **Every caller is named.** The ELF and the module hold exactly 6 calls of
  001A2370 (0x156EF0 drum, 0x219668 pickup, 0x21A104 in 0x219F50, and
  0x8248C4 / 0x824CD8 / 0x825B74 in the beam), one of 0x219F50 (0x2198E4 in
  0x219870) and two of 0019C6F0 (0x823730, 0x823ADC in [9]); no address-as-data
  word names any of them. The checker re-counts all three every run.
- **The drums carry the 0x800 bit here.** Unlike AREA22 (and AREA04), all four
  AREA06 drum hulls have 0x4800 in their first prim, so a knocked drum would
  move its hull. No capture has a drum out of state 1 (the export refuses
  one), and their hulls equal the disc. The checker proves the drum path with
  a knocked-drum control (the ORIGINAL 001A2370 moves exactly its hull, and
  only `owner_matrix` naming the drum accepts it).
- **Orphans.** a06_s1: no live node carries uid 26 (the 0x219870 node's slot
  0x7A7690 is free there: +0 = 0, its behaviour word reads 0x21A500); hull 26
  equals its derivation in the other captures and is
  accepted by the AREA01 orphan rule. a06_04 and a06_05: at the end of the
  collapse the beam reloads its pose from the record after its own (REC =
  record 11, REC + 0x2C.. = record 12, a class-0x0B record) and takes uid 27
  (+0x0E..+0x0F from REC + 0x2E), whose hull has no 0x800 bit, so the ORIGINAL
  001A2370 leaves hull 27 at disc (checked) and hull 28 keeps the bytes of the
  beam's last falling frame with no live owner. That pose is in no capture, so
  hull 28 there is accepted only by `export_area02_level.orphan_problems`:
  another capture derives hull 28 from the live beam, every differing word is
  one the ORIGINAL 001A2370 writes, and the bytes are equal in both
  orphaning captures (known gap 3).

**Background and the fog block.** No capture arms the background (render ctx
+0x174 = 0). The ORIGINAL 001D8FD0 and then the ORIGINAL 001D1C50, run over
each capture with ctx +0xA0..+0xFF overwritten, rebuild all 96 bytes in all 8
captures from room entry 14 (key 0x600). Spawn word bit 0x80 is clear in
every AREA06 capture (D_008106C8 = 0x11608, 0x19609, 0x19608), so 001D1C50
changes none of them (AREA22 finding 4 does not arise); flipping the room
entry changes 63 bytes, setting bit 0x80 changes 41. The values come from
the global `assets/render_context.emrc` (its blocks equal RAM in every capture).

**Roster.** Placement table 0x827AC0 (57 records) and the deferred group
0x826000 (33), the one group 001B6910's list names for sub 0. The group and
placement bytes equal RAM in every capture; the checker rebuilds the EMRO by
the original's walks over the pinned ELF + overlay and over each capture's
RAM. (live, at the record) placement nodes: 55 live in every capture, of
which 54 at their record's position and rotation in the arrival, 53 in a06_00
.. a06_03 and a06_s1, 52 in a06_04 / a06_05. Off the record: [10] (0022DCD0)
in every capture, the beam [11] in a06_00 (state 4, its sway run) and in
a06_04 / a06_05 (state 2, record 12's pose), the crate [31] from a06_01 on
(state 2, broken). Every live placement node keeps +0x03,
+0x54 and +0x0D, except the beam, whose C writes +0x0D = 0xC (state 0 with story
flag 16 set) or REC[0x2C] = record 12's +4 = 12 (the collapse's end); the
checker accepts 11 in states 0, 1, 4 and 0xC / 12 in state 2 (the captures
show 11 in states 4 and 1, 12 in state 2).

**Spawn and doors.**
- D_0024D650[6] = 0x275520: sub 0 at 0x24C0D0, sub 1 at 0x24C190, so 4 sub-0
  records; their +0x1C words are 0x11608, 0x19609, 0x19608, 0x19608. In every
  capture D_008106C8 = +0x1C of entry D_00810702 (0 for the arrival and a06_00,
  1 for a06_01 / a06_02, 2 for the rest). Bit 0x80 is clear in all four.
- The door row is 0x24E028 (6 records, 24 bytes, to 0x24E040). The placement
  doors: [1] 001BC350 id 0 (area change) = 01 07 00 00; [2] 001BC350 id 1 =
  02 01 00 00; [3] 001BB860 id 2 (area change) = 10 00 00 00.
  `export_area01_tables.export_doors` also lists 0x823580 as a door behaviour
  (AREA01's); in AREA06 that address is [9], so `doors_used` recomputes the
  list from 001BC350 / 001BB860 only. The checker recomputes the same list
  on its own (`doors_used_problems`: the roster file's placements whose +0x24
  is 001BC350 or 001BB860, their door id and area-change bit from +0x03) and
  requires `tables.json` `doors.used` to equal it, and each listed record to
  equal the door row in every capture's RAM.

**Scripts and overlay data.**
- The chains are the four the committed AREA06 overlay C starts (checked
  against the C every run): 0x826D40 (12 records) and 0x827040 (5) by the
  keypad 0x824340, 0x827180 (19) by the beam, 0x8276C0 (16) by the sub-1 owner
  0x825E20. Window 0x826D40 .. 0x827AC0 (52 records); the overlay data window
  is the module's data section 0x826000 .. 0x828F00.
- Every word equals the disc module in every capture except 18 words of chain
  0x827180 (0x827550 .. 0x8275BC) in a06_04 and a06_05, after the beam ran
  that chain; each lies in a reached chain record.

**Messages.** D_00264DD0[7] = 0x26FFC0 names 22 area records; the area bank
*D_0028A594 = 0x1337500 is the top block's id 0x41 (1,495 bytes by the bank's
own header). The file carries 54 global records, 36 stream rows and the global
bank (3,029 bytes); the checker compares every byte with RAM in every capture.
No text is printed or stored outside the ignored asset.

**World models.** *D_0028A59C = 0x16471C0 (id 0x43): 26 models, 390,128 bytes
inside id 0x43 (0x5F800). Every model owner whose +0x44 names a bank model has
+0x44 = 001C6120(table, +0x0D), its bone count and every bone slot set; bound
owners: 43 (arrival, a06_00), 42 (a06_01 .. a06_05), 41 (a06_s1).

**Sound banks.** The area container is upload section 0 of nested block 0
(block +0 .. +0xAB800): an SShd container of 701,200 bytes with 4 banks, none
of it resident. Its bytes are read from the extracted `f00_id42`, `f01_id46`
and `f02_id44` files, whose names do not describe them (`banks.json`
`container_byte_sources`). The bindings are the same in every capture:

| Group 1 | Group 2 | Group 4 | Refused |
|---|---|---|---|
| global rows 0..2 | area row 0 | slots 0..2: area rows 1..3 | group 3 slot 0 |

Group 3 slot 0 names handle 3, header 0x1800030 without the SShd magic (as in
the earlier areas). Registry for scope (6, 0), 1,000 ids: 518 audible, 473
absent, 9 UNSUPPORTED (reason "script": the exporter's sequence parser meets
controller bytes 0x07 (7 ids) and 0x60 (2 ids) it does not support), 77
samples. The checker re-derives every byte of the registry from each
capture's RAM and compares `area06_banks.bin` with the disc container.

## Checker, canary, controls and the mutation sweep

`tools/test_area06_assets_reference.py`:
- **loaders:** every file goes through its port loader, compiled privately
  from `src/` by the AREA01 checker's `build_loaders`: 13 of 13 load,
  including `sub0/area06_cells.bin` (no uid word carries bit 29, unlike AREA01
  / AREA00 / AREA02 / AREA04) and `sub0/message_data.emmd` (AREA06 has an area
  table, unlike AREA22).
- **real checks:** every section above, over the run's captures. Quick mode
  uses the arrival, a06_04 (the beam at uid 27, hull 28 orphaned, [9] +5 = 1,
  the chain's run-time words) and a06_s1 (the 0x219870 node freed, hull 26
  orphaned). Full mode uses all 8.
- **canary (53 sections):** `run_checks` over [the arrival, a planted copy],
  and a copy of the export tree with file plants. RAM plants: a static-bank
  byte, an object header, D_0028A5A0, the dynamic list's count, relocation
  word 0x72 (also moves the id label check), TEST_1, ctx +0xA4, a directory
  hull, uid 1's bit 30, [9]'s sub-state, the grid, the placement table and a
  node, the door row, the spawn rows, the first chain's first record, an
  overlay gap word, an area message record, the model bank, the pitch ladder,
  the beam's and the 0x219870 node's matrices, a model owner's +0x0D,
  D_008106C8 and the GS freeze texels. File plants: an extra zone file,
  `cells.json` (a moved hull and a flag call), the cells file, the container,
  the roster, the scripts file, `tables.json` (a run-time word set and a
  door record in `doors.used`), the zone EMDL and the
  dynamic-list file. Each port loader must refuse a broken copy of its file.
- **controls (191 changed inputs):** the file side and the RAM side of every
  comparator and the accept cases, among them: each moved hull's last byte;
  the beam's and the pickup's matrices; the 0x219870 node's matrix
  translation, third row and position (each rejected), the node read as a
  node + 0xD0 owner (rejected: the unscaled matrix does not give hull 26),
  0x219F50's code in RAM, a 0x219F50 run cut before its last written byte
  (refused); the orphans (a06_s1 alone and a06_04 alone rejected, each with
  the arrival accepted, the orphaned bytes changed, the prim count changed,
  two orphaning captures that disagree); [9] in sub-state 1 with the word at
  rest, with bit 30 cleared (accepted), bit 30 cleared in sub-state 0, [9] in
  state 2 / sub-state 2 / absent, 0019C6F0's code, a disc word without bit 30;
  the knocked drum (accepted) and its hull without the matrix; owner_matrix
  unit cases; the three call-site censuses with an extra site each; the
  run-time words (a gap word, the last chain, the last reached record's last
  and next byte, a placement word, a group word, the last window word,
  a06_04's words with and without chain 0x827180); the chain set against the
  C and `tables.json` (one dropped, one extra); `doors.used` (as exported
  accepted; a door dropped, an area-change bit flipped, a wrong record, and
  the RAM door row changed, each rejected); window and table file variants;
  the message file one unit short in each of its five parts; the roster
  (a node moved, an empty pool, the beam's +0x0D in each state); the model
  bindings; the spawn entry; the door loader; the load map (edge bytes, a
  moved cursor, the sub byte, the sub-1 image, the scratchpad pointer); the
  id labels (the extracted names read as ids, the grid row as id 0x44, the
  dynamic-list row as id 0x44, the last row as an earlier id, a file-name
  label, a nested row dropped, the top row dropped) and relocation words 0x42,
  0x41 and 0x7A; the pinned descriptor and a changed copy; the zone file, GS
  state and bank / list files; the sound bindings; the EMCL; the ctx block
  (three bytes, and bit 0x80 set); `cells.json`; clean copies; a bank record
  with a matrix slot.

**Mutation sweep (one bounded sweep, this lane).**
- 48 single-operation mutants over the checker's comparators and the AREA06
  exporter functions it calls (`build_load_map_ids`, `relocation_problems`,
  `owner_matrix` / `NODE_D0_OWNERS` / `HULL_OWNERS`, `flag_calls`,
  `derive_flag_image`, `derive_scaled_hull`, `verify_directory`,
  `run_time_words`, `chain_records`). Each ran in quick mode in a private copy
  of `tools/`, 4 workers (`build/area06/sweep/sweep.py`, ignored; 3 min 24 s
  wall).
- First pass: 42 killed, 6 survived, none only by a crash. The survivors, each
  given a control and re-applied alone (`sweep.py M04 M07 M32 M33 M38 M39`):
  M04 and M07 (the label check's upper bound weakened / dropped): killed by
  "the dynamic-list row labelled id 0x44" and "the last row labelled by an
  earlier id" (the check now bounds each id by every id of the descriptor's
  list, not only the labelled ones); M32 (the descriptor hash guard off):
  killed by "a descriptor with a changed nested relocation word"; M33 (the
  chain-set check weakened to a subset): killed by "the checker's chains with
  an extra entry"; M38 (derive_flag_image's agreement guard off): killed by "a
  disc uid-0 word without bit 30"; M39 (derive_scaled_hull's other-hull guard
  off): killed by the cut-range control, which also exposed that its memo key
  lacked the hull range (fixed: the key now holds it).
- Final state: 48 of 48 killed. The claim covers exactly these mutants; the
  reused AREA01 / AREA02 / AREA04 / AREA22 comparators keep their own sweep
  records and known gaps.

## Findings (for the lead)

1. **The resident-offset label rule holds for AREA06.** All eleven nested ids
   and the top id 0x41 relocate as cursor + list offset in every capture; the
   extracted `chunk10.n0` names are shifted by 0x164000 (for example the
   dynamic list and the model bank are read from `f10_id7a.bin`). AREA06's
   load map is labelled by id; the earlier nested areas' labels still carry
   the shift (AREA22 finding 8).
2. **A live 0x219870 node owns a moved hull.** g[11] re-transforms hull 26
   once, at its spawn, through 0x219F50's scaled scratchpad matrix. The
   earlier checkers fail on any live 0x219870 node; AREA06 derives it by
   running the ORIGINAL 0x219F50. A port that re-transforms it with node + 0xD0
   (the pickup's rule) gets hull 26 wrong (a control).
3. **[9] toggles uid 0's bit 30 every few seconds** through 0019C6F0(0xD, 1 /
   0) (sub-state 1 for 90 frames, 0 for 240). The live directory's uid-0 word
   is run-time state, as AREA02's uids 1..3.
4. **The drums' hulls carry the 0x800 bit in AREA06** (0x4800), unlike AREA22
   and AREA04: a knocked AREA06 drum would move its hull. No capture knocks
   one.
5. **The beam changes uid at the end of its collapse** (28 -> 27, +0x0D 11 ->
   12, pose from record 12, a class-0x0B record read as REC + 0x28..). Hull 28
   keeps its last falling-frame bytes with no owner; hull 27 has no 0x800 bit
   and stays as on disc.
6. **The door behaviour list of `export_area01_tables.export_doors`** includes
   0x823580, AREA01's overlay door; in AREA06 that address is [9]. The AREA06
   exporter recomputes `used`; the shared function is not edited.
7. **9 sound ids are UNSUPPORTED** by the registry exporter's sequence parser
   (controller bytes 0x07 and 0x60 in area bank row 0's scripts), not by an
   unbound bank.

The census delta (`../Extermination/build/s87/census/a06_delta.json`, 32 new
functions) names none of the load-path functions these exports model
(001FFCD0, 001B6910, 001B6990, 001C6120, 001A2370, 0019C6F0, 0x219F50,
001D8FD0, 001D1C50). Of its three AREA06 overlay entries, 0x823580 ([9]) and
0x824560 (the beam) are modelled here as the 0019C6F0 and 001A2370 callers.

## Binding

Nothing is wired. Every file is exported, checked and loadable. When AREA06
is bound:
- The scene selects `sub0/` for the level, collision, cells, roster, world
  models, message file and sound files; the spawn table, doors, scripts and
  overlay data are shared. Sub 1 needs its own export (known gap 1).
- The level is one zone, `sub0/level/00_id44.emdl`; the dynamic list is
  `sub0/level/dynamic_objects.emsc`.
- The load must reproduce the nested layout: the top block (id 0x41) at the
  first cursor, the nested resident region at the second, and D_0028A490[id]
  for the twelve listed ids.
- The live directory starts from `sub0/area06_cells.bin`. The pickups, drums
  and the beam call a translated 001A2370 with node + 0xD0 (the port has
  `em_actor_cells_retransform_001A2370`, keeping the 0x800 gate); 0x219870
  must go through a translated 0x219F50 (its scaled scratchpad copy), which the
  port does not have yet; [9] needs a translated 0019C6F0 (none yet either).
- The beam's collapse must switch the node to record 12's data (+0x0D, uid,
  pose) and leave hull 28 as its last frame left it.
- The ctx block is evaluated at run time by 001D8FD0 at a room load and by
  001D1C50 every frame, from `assets/render_context.emrc`.

## Known gaps

1. **Sub 1** (`chunk10.n1`, placements 0x8283D0 and group 0x8265E0, the
   director 0x825E20) is loaded by no capture and not exported.
2. **Ids 0x71, 0x7C, 0x72, 0x79, 0x73, 0x7A** (0x16A69C0 .. 0x17711C0) are not
   decoded; their bytes are only checked equal to RAM through the load map.
3. **Hull 28 after the collapse** is accepted by the written-words orphan rule
   only; the beam's last falling pose is in no capture.
4. **Not exported:** shadow receivers, owner object textures, door, prop and
   pickup models (as for the earlier areas).
5. **SPU residency** of the samples: there is no AREA06 SPU capture.
6. **The 9 UNSUPPORTED sound ids** (finding 7) are not resolved.
7. **Descriptive side files** are not checked (see Outputs).
8. **A knocked drum and a thrown pickup** (0x219870 -> 0x219F50 after spawn is
   not a path: 0x219870 calls it in state 0 only) appear in no capture; the
   drum path is proven on a control only.
9. **The 001D1C50 branch** with spawn bit 0x80 set does not occur in AREA06;
   only its bit-clear path shows here.
