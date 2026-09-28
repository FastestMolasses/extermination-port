# AREA00 assets: local export and capture checks

Lane "A00ASSETS", level-3/4 side track, 2026-09-28. This lane adds new files
only: four exporters, one checker and this document. It changes no port
source, no tracked port file and no decomp file, and nothing it adds is wired
into the game. Every exported byte comes from the user's own disc files and
ELF and is written into the ignored `assets/area00/` tree. Every export is
checked against the recorded original captures of AREA00. Nothing was run in
PCSX2.

The captures (11 in all, every one with RAM, scratchpad and GS freeze):
- the AREA00 arrival, the end of SECOND_LEVEL_ROUTE.md beat a01_07
  (`../Extermination/build/s87/route_a01/a01_07_level_exit/`);
- the route beats a00_00..a00_09 (`build/s87/route_a00/`).
a00_10 and a00_s0 end in AREA01 and are excluded by their area byte.

## Two sub-states were loaded, not one

The captures split by D_00810701, and each group matches only its own nested
block (measured by `export_area00_common.loaded_sub_proof`):
- **sub 0** (`chunk04.n0`): the arrival and a00_00..a00_07, 9 captures;
- **sub 1** (`chunk04.n1`): a00_08 and a00_09, 2 captures, after the switch.

For each capture, the load map of its own sub was compared with RAM, and so
was the map of the other sub:
- its own sub's map differs in 141 to 519 16-byte rows, all inside the cell
  directory;
- the other sub's map differs in about 225,400 to 225,800 rows.

The two nested blocks differ only in their last file (id 0x44). In RAM the two
loads are equal below 0x15CE380 and differ from there on, so the static
bank (the level geometry) differs between the subs. The grid, the cell
directory and the model bank lie below 0x15CE380. The exporter requires
them to be equal in both loads and exports them once. THIRD_LEVEL_ROUTE.md
(section 7 item 2, section 9) says that sub 1 was not played. The captures
show that it was (finding 1).

## Outputs

The exports use the formats the port already reads for AREA11 and AREA01:

| Output (`assets/area00/`) | What | Port loader |
|---|---|---|
| `sub<N>/level/00_f06_id44.emdl` (+ `.gsmat.json`, descriptive) | level geometry, texels, GS state codes of sub N | `em_model_load` |
| `sub<N>/level/static_bank.emsc` | the static bank `*D_0028A5A0` of sub N | `em_script_image_load` |
| `area00.emcl` (+ `scene.txt` from export_collision.py) | grid collision with node class, rank and axis sections | `em_collision_load` |
| `area00_cells.bin` | the cell directory `*0x70003250` (disc rest state) | `em_actor_cells_load`, which **rejects** it (finding 3) |
| `sub<N>/roster.emro` | placements 0x82BB50 + deferred group 0x826F80 (EMRO header carries the sub) | `em_actor_roster_load` |
| `spawn_table.emsp` | the global spawn export; its area 0 rows are 0x24AA50 (sub 0) and 0x24ACC0 (sub 1) | `em_spawn_table_load` / `_read` |
| `door_destinations.emsp` | EMSP windows: `D_0024E140[0..0x17)` and the area 0 row 0x24DF80 (8 records) | `em_spawn_table_load` / `_read` |
| `area00_scripts/scripts.emsc` | the script window 0x8284E0..0x82BB50 (14 chains, 183 records) | `em_script_image_load` |
| `overlay_data.emsc` | the overlay data section 0x826F80..0x82D480 | `em_script_image_load` |
| `message_data.emmd` | EMMD v1 for area 0: 54 global and 54 area records, 36 stream rows, both banks | `em_message_live_install`, which **rejects** area 0 (finding 2) |
| `world_models.emwm` + `.json` | the model bank `*D_0028A59C` (39 models, 1,182,656 bytes) | `em_world_models_parse` |
| `sfx/area00_banks.bin`, `sfx/banks.json` | the area's SShd container (3 banks, 567,328 bytes) | (the registry's source) |
| `sub<N>/sfx/sfx_registry.emsr` + `.json` | EMSR v2 for scope (0, N) | `em_sfx_registry_load` |
| `sub<N>/level/level.json`, `cells.json`, `tables.json` | counts, hashes and the verification summary (no text, no data) | none |

There is no `dynamic_objects.emsc` and no `background.embg`, for the reasons
under "Level geometry" and "Background and fog" below.

The checker does not check the descriptive side files: `scene.txt`, the
`.gsmat.json` files, `world_models.json`, `sfx_registry.json`, `banks.json`,
`level.json`. It does compare these recorded lists with its own results:
- in `cells.json`: the moved hulls per capture, the 0019C6F0 calls per
  capture, and whether `moved_uids` is the union of the per-capture rows;
- in `tables.json`: the run-time words of each capture.

## Tools

All the tools are pure Python and run on native arm64 macOS. Run them from
the port root.

```sh
python3 tools/export_area00_level.py    # both subs' level + textures + materials, collision, cells (~13 s CPU)
python3 tools/export_area00_tables.py   # rosters, spawn, doors, scripts, overlay data, messages, world models (<1 s)
python3 tools/export_area00_sfx.py      # sound container + one registry per sub (~2 s)
python3 tools/test_area00_assets_reference.py                  # checker, 3 captures
EM_TEST_FULL=1 python3 tools/test_area00_assets_reference.py   # all 11 captures
EM_AREA00_ASSETS=<dir> python3 tools/test_area00_assets_reference.py   # check another export tree
```

Checker timings on the M1 (2026-09-28, after the review fixes). Other
lanes kept the load average between 90 and 160, so CPU time (user + sys)
is the figure to compare, and even that grows under contention:

| Run | CPU | Wall |
|---|---|---|
| quick | 7.6 to 8.6 s warm at load 90 to 130; the review measured 10.3 s at load about 160; about 2 s more when the loader library is rebuilt (after any `src/` header change) | 8 to 25 s |
| full | 13.0 to 13.3 s | 16 to 26 s |

The quick run therefore sits at the ~10 s budget, and slightly over it on a
heavily loaded machine.

**Reuse.** The AREA01 exporters and checker are reused by import. None of
them is edited. `export_area00_common.configure(sub)` sets the attributes of
`export_area01_common` for AREA00:
- `AREA`, `SUB`, the overlay path, size and id (AREA00.BIN, MWo3 id 1);
- the output paths and the capture list.

Every AREA01 function reads these attributes when it is called, so the
following run unchanged for AREA00:
- the load map, `LoadedImage` and `compare_load_map`;
- from `export_area01_level`: the bank walk, the display-list kick check,
  the zone builder and writer, `run_collision`, `verify_cell_directory` /
  `derive_cell_image`, `check_background` and `check_ctx_room_block`;
- from `export_area01_tables`: `export_doors`, `export_world_models`,
  `export_messages`, `walk_chain`, `match_nodes` and the spawn-field
  helpers;
- from `export_area01_sfx`: `bindings_from_captures`;
- from the AREA01 checker: `zone_problems`, `emcl_equal`, `grid_problems`,
  `roster_image`, `emsp_problems`, `messages_problems`,
  `world_models_problems`, `registry_from_ram` and the other comparators
  that the checker's docstring lists;
- through those modules: `export_level.py` (decomp), `export_collision.py`,
  `export_area11_roster`, `export_spawn_table`, `export_message_data`,
  `export_world_models` and `export_sfx_registry`.

A few module attributes are replaced:
- `export_area01_level.owner_matrix` is replaced for the whole process by
  AREA00's (below);
- `export_area01_tables.pool_nodes` and `AREA_BANK` are swapped for one
  call and then restored;
- the AREA01 checker's `GRID`, `BUILD` and `OVERLAY_SHA256` are set for
  AREA00.

Because of the first of these, a process should not import the AREA00 and
the AREA01 level exporters together.

## How each asset is derived and checked

**Load map.** The rule is AREA01_ASSETS.md "Load map". The map is built from
the descriptors and cursors, with INDEX.IDX sector 4 read from the disc
image:
- the top block `chunk04` (7 files) lies whole at D_0028A73C = 0x1335F40;
- the nested block's resident region starts at +0x173800 and lies at
  D_0028A740 = 0x13E0B80. It holds n<N> f04 from +0x21000, f05 and f06.

Each sub's map has 10 files. Sub 0's map is 6,590,464 bytes and sub 1's is
6,549,504 bytes. Every mapped byte equals RAM in every capture of the sub,
except the bytes of the cell directory, which the directory check covers.
In a00_08 and a00_09 the descriptor buffer D_00289BC0 holds sector 29 (a
later read). The descriptors are therefore taken from the disc image,
which is the AREA01 finding 3 situation.

**Level geometry.**
- The static bank at D_0028A5A0 = 0x15CE380 lies whole in n<N> f06:
  - sub 0: 1,101 objects, extent 0x15CE380..0x19803E0;
  - sub 1: 1,100 objects, extent 0x15CE380..0x19767E0.
- The grid at render ctx +0x140 names exactly objects 1..N-1.
- Every record has matrix slot 0 and a colour. So each sub has one zone
  EMDL:

  | Sub | Objects | Vertices | Triangles | Textures |
  |---|---:|---:|---:|---:|
  | 0 | 1,100 | 29,658 | 18,092 | 203 |
  | 1 | 1,099 | 28,908 | 18,092 | 203 |

- Every textured level-kernel kick of both display lists REFs units inside
  a bank object: 10,305 kicks in the 9 sub-0 captures and 3,132 in the 2
  sub-1 captures. Every kick has the class-0 GS state (PRIM 0x3C, TEST_1
  0x5000D, ALPHA_1 0x80000000A8, TEX1_1 0x60, CLAMP_1 0).
- **No dynamic list.** The evidence is the captures: the kick check runs
  with an empty dynamic list, so any kernel-0x00237450 kick would fail it,
  and none of the 11 captures has one (10,305 sub-0 and 3,132 sub-1 level
  kicks, all of the static bank). Supporting, but weak on its own:
  - 001D5370 calls 001D5BD0, which walks *D_0028A5A4, only for the stages
    its switch names, and stages 0x0000 and 0x0001 are not among them. That
    C is a NEARMISS (39.18% objdiff), so it is not relied on alone.
  - D_0028A5A4 still holds AREA01's list address, 0x177A940. Word 0 there
    is not a count.
- The texels come from the level-load GS upload, replayed from the disc
  image (two sections). They must equal the GS-freeze decode of every
  capture: 1,827 comparisons for sub 0 and 406 for sub 1.
- The checker requires each zone file to equal, byte for byte, a rebuild
  from the exported bank (AREA01's `zone_problems`). It also compares the
  bank file with RAM over its full length, with the length set by
  `bank_end`.

**Collision.**
- The grid is D_0028A598 = 0x1501B80 = n<N>/f06 + 0xA7000. It is the same in
  both subs.
- `export_collision.py --node-class` runs over f06, and `--verify-ram`
  passes for all 11 captures. The result has 1,683 vertices, 769 polygons
  and 3,052 indices, and no cell n-gons.
- The checker rebuilds the whole EMCL from each capture's RAM grid
  (`emcl_from_ram`) and requires the file to equal it.
- It pins D_0028A598 to 0x1501B80 and requires the grid block to lie in
  the load map, outside the directory allowance. The code that fills the
  pointer at load is not identified, as for AREA01.

**Cell directory** (0x1523380, 73 uids, 61,080 bytes).
- `area00_cells.bin` holds the disc bytes. The file must equal the disc
  over its length, and end where the directory walk ends.
- Every byte of every capture's RAM directory is then derived. There are two
  kinds of run-time change.
- **Hulls re-transformed by their owner.** The ORIGINAL 001A2370 runs in the
  EE interpreter with the matrix that each owner's own code passes.
  - The owners and matrices are read from the committed C: 0x825600
    (`func_overlay_AREA00_008255C0`, byte-identical, four call sites) and
    0x8263C0 (`func_overlay_AREA00_00826380`, byte-identical) call
    `func_001A2370(self, self + 0xD0)`. 0x219550, the pickup, passes node
    + 0xD0, as in AREA01.
  - These hulls are re-derived byte for byte:

    | Hulls | Owner |
    |---|---|
    | 23, 43, 62 | 0x825600 |
    | 63, 64 | 0x219550 |
    | 26 (a00_04 on) | 0x8263C0 |

  - Every other hull equals the disc. No hull is orphaned (every moved
    hull has a live owner).
- **The uid words.** In a00_08 and a00_09 the uid 1 word is 0xC0000228; the
  disc holds 0x80000228 (bit 30 set).
  - The shaft door 0x823580 (`overlay_AREA00_func_00823540.c`,
    byte-identical) calls 0019C6F0 in state 1, talk step 0. It calls (2, 0)
    when D_0081075D is 0xFF, and (2, 0) then (0, 1) when D_0081075E is 0xFF.
  - `export_area00_level.derive_words` runs the ORIGINAL 0019C6F0 over each
    capture's RAM and scratchpad, with the directory set to the disc bytes.
    It makes exactly the calls that the capture's two flag bytes select.
    - Its code in RAM must equal the ELF.
    - A live shaft-door node must exist.
    - The calls may change nothing outside the uid words.
  - Result: no call in the 9 sub-0 captures, and (2, 0) in a00_08 and
    a00_09. The result equals the captured words.
- The export keeps the disc words (the load-time image).

**Background and fog.**
- No capture arms the background: render ctx +0x174 bits 0/1 are 0 in all
  11 captures.
- The ORIGINAL 001D8FD0 runs over each capture with +0xA0..+0xFF
  overwritten. It rebuilds the 96 captured bytes that
  docs/RENDER_CONTEXT.md reads as FOGCOL and the related context. The
  rebuild uses room entry 0 (key 0x0000) in the sub-0 captures and entry 1
  (key 0x0001) in the sub-1 captures.
- Flipping that entry changes 63 of the 96 bytes, so the rebuild really
  reads the room table.
- The values come from the global `assets/render_context.emrc`, whose
  blocks equal RAM in every capture. There is no AREA00-specific file.

**Roster.**
- `walk_roster(0, sub)` gives placement table 0x82BB50 (69 records) and
  deferred group 0x826F80 (61 records) for both subs. The only difference
  between the two EMROs is the sub byte of the header.
- The group and placement bytes equal RAM in every capture.
- The checker rebuilds the EMRO by the original's walks, over the pinned
  ELF + overlay and over each capture's RAM, and requires them to be equal.
  It also requires the file to equal the disc rebuild.
- Live placement nodes keep the fields 001B6990 copies. The checker pins
  (live, still at the record's position and rotation) per capture:

  | Captures | (live, at rest) |
  |---|---|
  | the arrival, a00_00..a00_02 | (66, 65) |
  | a00_03 (the padlock broken) | (65, 64) |
  | a00_04..a00_09 | (65, 61) |

**Spawn and doors.**
- `spawn_table.emsp` is byte-identical to `assets/spawn/spawn_table.emsp`
  and carries the area 0 rows: 0x24AA50 (13 records) for sub 0 and
  0x24ACC0 (13 records) for sub 1.
- In every capture, D_008106C8 equals word +0x1C of the current spawn
  record (area 0, sub D_00810701, entry D_00810702, the records read from
  the pinned ELF). The exporter stops if it does not, and the checker
  asserts it per capture (`spawn_entry_problems`, with a canary plant and
  a control); `tables.json` records both values.
- The door file has the area 0 row 0x24DF80 (8 records, 32 bytes) and the
  pointer array 0x24E140 (92 bytes).
- The checker walks the window set over the pinned ELF and compares every
  window with RAM in every capture.
- These doors use the row:

  | Placement | Behaviour | Door id |
  |---|---|---|
  | [51] | 0x825170 | 3 |
  | [52] | 0x823580 | 0 (area change) |
  | [55] | 0x1BC350 | 2 |
  | [56] | 0x1BC350 | 1 |
  | [58] | 0x1BB860 | 4 |

**Scripts and overlay data.**
- The script window runs from the lowest chain entry, 0x8284E0, to the
  placement table D_0024D7C0[0][sub] = 0x82BB50 (the AREA01 rule).
- It holds the 14 chains of AREA00_OVERVIEW.md section 9 that sub 0 and
  sub 1 start, 183 records. The chain 0x82D070 lies after the placement
  table. Only the sub-2 owner 0x826790 starts it, so it is not in this
  window. It is in `overlay_data.emsc`.
- The overlay data window is the module's data section (MWo3 header word
  4 = 0x6500), 0x826F80..0x82D480.
- Both windows are the pinned disc module's bytes.
- **No capture holds the load-time image.** The arrival script 0x828D60 ran
  before the arrival snapshot, so even the arrival shows 8 rewritten words.
- The checker compares every capture's RAM with the disc bytes, except each
  word that the capture shows rewritten. Such a word must be one of these:
  - inside a record that one of the 14 chains reaches. These are 8 to 30
    words per capture, at record offsets +0x10 and, in the ferry chain
    0x829BE0, +0x20..+0x2C and +0x3C;
  - a lane of the vector 0x82CCE0..+0xB. Owner 0x8263C0 writes this
    vector in its state 1 (`self[4] == 1`), once per update: by its
    byte-identical C (func_overlay_AREA00_00826380), lane k is a constant
    (-16.099998, 12.0, -40.400024) plus self +0xB0 + 4k, written before
    the same update copies the followed node's +0xB0/+0xB4 into self. So
    a captured vector equals the derivation from the captured self +0xB0
    only when that copy did not change self in the captured frame. The
    checker accepts a rewritten lane only when it equals that derivation,
    re-derived with the EE single add (`ee_float_model`); all three lanes
    are accepted on that condition. In the captures only lanes 0 and 1
    are ever rewritten (a00_04..a00_09); lane 2's derivation equals the
    disc value in all 11. Measured, not checked: all three lanes equal
    the derivation in all 11 captures, the unrewritten ones included.

  The set of rewritten words in each capture must also equal `tables.json`.

**Messages.**
- The area table D_00264DD0[1] = 0x26EEB0 has 54 records. The global table
  has 54 and there are 36 stream rows.
- The area bank is `chunk04/f00_id41.bin` at 0, which *D_0028A594 =
  0x1335F40 names. It is 3,078 bytes; the global bank is 3,029 bytes.
- The checker compares every byte with RAM in all captures, with the counts
  taken from the ELF and the bank sizes from the banks' own headers (AREA01's
  `messages_problems`).
- No text is printed or stored outside the ignored asset.

**World models.**
- *D_0028A59C = 0x13E0B80 (n<N> f04 + 0x21000) holds 39 models, 1,182,656
  bytes. The span equals RAM, and `wm_span` over each capture's RAM gives
  the same extent.
- The checker checks every model owner whose +0x44 names a bank model:
  - +0x44 must equal 001C6120(table, +0x0D);
  - +0x0C must be the model's bone count;
  - every bone slot must be set;
  - the number of bound owners in each capture is pinned (61, 60 or 53).
- One exception: node behaviour 001EA240 (the rumble / shake effect
  driver). The evidence that its +0x44 is a stale pointer is the pool
  history of slot 102 (node 0x7B81E0) in the captures:
  - the arrival and a00_00..a00_02: live, behaviour 0x1581A0 (the
    padlock), +0x44 = 0x1408D00;
  - a00_03..a00_05: free, +0x10 already 0x1EA240, +0x44 still 0x1408D00;
  - a00_06: live again with behaviour 001EA240 and the same +0x44;
  - a00_07, a00_08: free, unchanged; a00_09: free, +0x44 = 0x11351C0.

  Freeing a slot does not clear +0x44, and 001EA240 did not rewrite it.
  001EA240's own byte-matched C does not touch +0x44; the per-type
  handlers it dispatches through D_00255434 and 001AFC10 were not checked,
  so that C argument is incomplete and the pool history is the evidence.
  These nodes are skipped by behaviour (`NO_MODEL_BEHAVIOURS`).

**Sound banks.**
- Upload section 0 of the nested descriptor (block +0 .. +0x8B000) lies in
  the block's first file, `f00_id43.bin`. In AREA00 that file is not
  resident at all, so the container is taken from the nested descriptor's
  own file list (the entry at block offset 0). n0 and n1 carry
  byte-identical copies of the file.
- The bindings are the same in all 11 captures, as in AREA01:
  - group 1 → global rows 0..2 (handles 0..2);
  - group 2 → area row 0 (handle 4);
  - group 4 → area rows 1 and 2 (handles 5 and 6);
  - group 3's header is refused.
- The registry for scope (0, N) resolves 1,000 ids per sub: 461 audible,
  530 originally absent and 9 UNSUPPORTED (7 modulation, 2 script). The
  audible entries use 46 samples.
- The checker re-derives every byte of each sub's registry from each
  capture's RAM with AREA01's `registry_from_ram`, decoding the samples from
  the disc container. It also compares `area00_banks.bin` with the disc
  container over its full length.

## Checker, canary, controls and the mutation sweep

`tools/test_area00_assets_reference.py` runs these steps:
- **loaders:** every file goes through its port loader, compiled privately
  from `src/` by the AREA01 checker's `build_loaders` into
  `build/area00/assets/test/`. That is 14 files, and the two refusals are
  findings 2 and 3. Their diagnostics are asserted: a copy of the message
  file with area 1 loads, and a copy of the directory with bit 29 cleared
  loads.
- **real checks:** every section above, over the run's captures. Quick
  mode uses the arrival, a00_04 and a00_08; full mode uses all 11.
- **canary (68 sections).** `run_checks` runs again, per sub, over [a
  capture, a planted copy of it]:
  - Each per-capture comparison has its plant in the copy's RAM or GS
    freeze, and the copy is second in its list, so a comparison that reads
    only the first capture, or is not run at all, misses it. It must
    report the copy and stay silent for the original. The review found
    that the model-binding comparison had no plant; it now has one (the
    first bound owner whose +0x0D with bit 0 flipped names another model:
    slot 29 in both subs), and the spawn-entry word D_008106C8 has one
    too.
  - Comparisons that are not per capture have file plants in a copy of the
    tree: the cells file, the sound container, the sub-1 roster, the
    scripts file, `cells.json` and `tables.json`, an extra zone file and a
    dynamic-list file. The extra zone file (`00_a_extra.emdl`, a copy)
    sorts before the real one, so both the zone count check and the zone
    name check must report it.
  - The zone EMDL plant has its own pass, because a structural difference
    stops `zone_problems` before the texel check.
  - Each port loader must refuse a broken copy of its file.
  - The file side and the RAM side are compared with independent sources:
    - the file against the disc: roster rebuild, container, module bytes;
    - RAM against the disc as well, not against the file.

    A file plant therefore never reaches a per-capture comparison.
- **controls (94 changed inputs).** The file side and the RAM side of every
  comparator, plus the accept cases:
  - D_0081075E = 0xFF selects both calls;
  - an unplanted copy under another name passes the cells and table
    checks;
  - a rewritten word in the last chain's records is explained;
  - each known owner behaviour gets node + 0xD0.

  They include the killing input of every sweep survivor below, and of
  the review's named survivors.

**Mutation sweep (one bounded sweep, this lane's review).**
- The sweep ran 41 single-operation mutants over the checker's own
  comparators and over the exporter functions that the checker calls
  (`door_calls`, `derive_words`, `owner_matrix`, `verify_directory`,
  `run_time_words`, `vector_words`, `chain_records`, `model_owner_nodes`).
  The script is `build/area00/sweep/sweep.py` (ignored). Each mutant ran
  in a private copy of `tools/`, with at most 4 workers.
- The first pass left 12 survivors. They were killed as follows:

  | Mutant | Weakening | Killed by |
  |---|---|---|
  | M03 | load-map allowance widened past the directory | control: the mapped bytes on either side of the directory |
  | M04 | static bank compare without its last byte | control: the bank's last byte alone (`bank_problems`) |
  | M10 | directory size check `>` | control: its own message on a one-byte-longer file |
  | M21 | last chain dropped from the reached records | accept control: a word in that chain |
  | M23 | run-time sets compared by count | control: one word replaced by its neighbour |
  | M28 | bindings checked only for the group-3 refusal | `binding_problem` unit controls |
  | M29 | container length check `<` | control: its own message |
  | M30 | planted copies lose their pinned-table key | clean-copy accept controls |
  | M32 | loader diagnostics joined by `or` | `findings_ok` unit controls |
  | M33 | "a different load map" never reported | control: a moved load cursor |
  | M34 | the kick GS state check removed | a canary TEST_1 plant at 0x8153A0 |
  | M37 | 0019C6F0's outside-the-words guard removed | control: a directory whose count stops before uid 1 |

- The second pass added M35, M39, M40 and M41 against the new checks:
  - M39: a capture that `cells.json` does not record is now reported. The
    union check it replaced was redundant.
  - M41: an unused scratchpad-edit path was removed from the helper.
  - M35 and M40 were killed by the checks and controls above.
- Final state: all 41 mutants are killed in quick mode. One of them, M31,
  was killed only by a crash in the controls. The pinned model-owner counts
  (61 / 60 / 53) now kill it with a FAIL.
- The claim covers exactly these mutants and the review's below, not all
  mutations. The AREA01
  comparators reused here keep their own sweep record, including their
  known gaps G02..G10 (AREA01_ASSETS.md), which apply here unchanged.

**Review sweep (the reviewer's 41 mutants R01..R41).** 27 were killed; 14
survived quick and full mode. The six non-equivalent survivors now die in
quick mode (re-run in private copies with the reviewer's exact edits,
`build/area00/sweep/rkill.py`, ignored):

| Mutant | Weakening | Killed by |
|---|---|---|
| R04 | zone-name check removed | canary file plant `00_a_extra.emdl`: "sub0/00_a_extra.emdl: zone name" |
| R05 | "records with a matrix slot" guard removed | control: bank file and RAM both with bit 3 of one record's w word set (matrix slot 1; the first record of the first object after object 0 that has one), through `level_problems` in a scratch tree |
| R12 | "every bone slot set" replaced by True | control: slot 29's first bone slot (+0x110) cleared |
| R13 | `tables_problems` no longer calls `model_binding_problems` | canary model-binding plant (slot 29 +0x0D bit 0, both subs) |
| R25 | `model_binding_problems` reads only the first capture | the same canary plant (the copy is second in its list) |
| R41 | bound-owner count compared with `>` | control: slot 29's +0x44 cleared ("60 model owners bound, not 61") |

The other eight survivors are equivalent or redundant on these inputs:
- R01 (per-capture scratchpad directory pointer check removed): a moved
  pointer is still reported by `cells_problems` ("area00_cells.bin differs
  from the disc bytes", "hull 0 differs"); the reviewer ran it.
- R02 (other-sub check shortened to 16 bytes): the first 16 bytes from
  0x15CE380 already differ between the loads in every capture, so the
  shortened check reports the same.
- R06 (`grid_problems` removed): `emcl_equal` catches the D_0028A598
  control.
- R07 (`hulls_inside_table` removed): the file must equal the disc bytes,
  and the disc directory has no hull inside its uid table.
- R10 (window length check reduced to the base): a different length makes
  the window bytes differ from the disc bytes, which is reported.
- R32 (-16.1 in place of -16.099998): the float bits differ by one ulp,
  and in the EE add with every captured self +0xB0 the difference rounds
  away, so the derivation is the same on the captures.
- R36 (`captures()` mixes the subs): only the exporters call it, not the
  checker.
- R37 (container total > section-0 bound removed): the container must
  equal the disc container, which already bounds it.

## Findings (for the lead)

1. **Sub 1 was loaded on the recorded route.** The loaded block is
   `chunk04.n1` in a00_08 and a00_09:
   - The n1 map equals RAM, except 519 rows, all in the directory. The n0
     map differs in 225,791 rows.
   - The static bank differs: 1,100 objects against 1,101.
   - THIRD_LEVEL_ROUTE.md section 7 item 2 and section 9 ("Sub 1 and sub 2
     were not played") should be updated by the capture lane. This lane may
     not edit those documents.
2. **The port's message loader refuses area 0.** `em_message_live.c`
   `load()` returns 0 for `area == 0`. The original indexes D_00264DD0
   [area + 1] for area 0 as for any other area (D_00264DD0[1] = 0x26EEB0).
   A copy of the file with the header's area set to 1 loads, so nothing
   else in the file is refused. The loader needs a decision before AREA00
   wiring.
3. **`em_actor_cells_load` refuses the AREA00 directory.** The cause is the
   same as AREA01_ASSETS.md finding 1: uid 0's word carries bit 29, and the
   loader's 0x3FFFFFFF mask leaves 0x2000xxxx. A copy with bit 29 cleared
   in every word loads.
4. **Run-time uid-word flags.** The shaft door's 0019C6F0(2, 0) sets bit 30
   of uid 1's directory word after the switch. The ORIGINAL 0019C6F0
   reproduces it. A port that loads `area00_cells.bin` must make the same
   calls through a translated 0019C6F0.
5. **D_0028A5A4 is stale in AREA00.** It still holds AREA01's list address,
   and stages 0x0000 and 0x0001 draw no dynamic list.
6. **No capture shows the load-time script image.** The arrival script had
   already rewritten 8 words. Later captures show up to 30 rewritten words
   plus the owner vector 0x82CCE0.
7. **A freed owner's model pointer survives in the pool.** The rumble node
   001EA240 in a00_06 slot 102 still holds the padlock's +0x44 (0x1408D00);
   the slot history is under "World models".

## Binding

Nothing is wired. The files are exported, checked and loadable, except for
the two loader decisions (findings 2 and 3). When AREA00 is bound:
- The scene selects `sub<D_00810701>/` for the level, roster and registry.
  It uses the shared files for the rest: collision, cells, spawn, doors,
  scripts, overlay data, messages, models and the sound container.
- The live directory starts from `area00_cells.bin`. Two things change it
  at run time:
  - owners 0x825600, 0x8263C0 and 0x219550 re-transform their hulls through
    a translated 001A2370 with node + 0xD0;
  - the shaft door's talk step makes the 0019C6F0 calls. Neither 001A2370
    nor 0019C6F0 is translated in the port yet.
- The script and overlay-data windows are the load-time image. The script
  host's own record writes, and owner 0x8263C0's writes to 0x82CCE0,
  reproduce the run-time words.
- The ctx +0xA0..+0xFF block must be evaluated at run time from
  `assets/render_context.emrc` (room entries 0 / 1), as for AREA01.

## Known gaps

- **Script run-time words.** The words the script host rewrites are located
  (each lies in a reached record, and each capture's set is pinned) but not
  re-derived with the original script ops.
- **The owner vector.** Only its rewritten lanes are compared with the
  derivation. A lane equal to the disc value is compared with the disc
  (measured: it also equals the derivation in all 11 captures).
- **The grid pointer.** D_0028A598 is pinned by measurement; the code that
  fills it is not identified.
- **Not exported:**
  - sub 2 (`chunk04.n2`, placement table 0x82C640, the chain 0x82D070): no
    capture loads it;
  - shadow receivers, owner object textures, and door, prop and pickup
    models (as for AREA01).
- **SPU residency** of the samples: there is no AREA00 SPU capture.
- **Sub-1 sound container name.** The sub-1 registry names the n0 container
  file. The bytes are identical, and the exporter requires that.
- **Descriptive side files** are not checked (see Outputs).
- **Equal triangle totals.** Both zone EMDLs have 18,092 triangles
  (54,276 indices) although sub 1 has 750 fewer vertices and one object
  fewer. Measured over the two banks: 17,433 triangles are position-identical
  (as multisets) in both subs; 659 differ in each, all in the same region
  and with no common offset. Aligned object by object (by their triangle
  counts, in bank order), 898 of sub 1's 1,099 objects match a sub-0
  object's count and 201 do not. That the differing triangles are equal in number is not
  explained. Both files equal the reused AREA01 builder's rebuild, both
  have max index = vertex count - 1 and no degenerate triangles, so this
  is not an export fault.
