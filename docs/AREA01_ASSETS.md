# AREA01 sub-0 assets: local export and capture checks

Lane "assets", AREA01 side track, 2026-09-25 (session s87). This lane adds new
files only. It changes no port source and no decomp file, and nothing it adds is
wired into the game. Every exported byte comes from the user's own disc files
and ELF and is written into the ignored `assets/area01/` tree. Each export is
checked against the recorded original captures: the route beats a01_00..06,
the side beats a01_s0..s3, and the AREA01 arrival capture of the first level's
exit (`route/15_level_exit`), 12 AREA01 sub-0 captures in all. Beat a01_07 ends
in AREA00, so its area bytes exclude it. Nothing was run in PCSX2.

The exports use the formats the port already reads for AREA11:

| Output (`assets/area01/`) | What | Port loader |
|---|---|---|
| `level/NN_<file>.emdl` (+ `.gsmat.json`, descriptive, not checked) | level geometry, texels, GS state codes (12 files) | `em_model_load` |
| `level/static_bank.emsc`, `level/dynamic_objects.emsc` | the raw static bank `*D_0028A5A0` and dynamic list `*D_0028A5A4` | `em_script_image_load` (EMSC window) |
| `area01.emcl` (+ `scene.txt` line from `export_collision.py`) | grid collision with node class, rank and axis sections | `em_collision_load` |
| `area01_cells.bin` | the cell directory `*0x70003250` | `em_actor_cells_load`, which **rejects** it (finding 1) |
| `roster.emro` | placements 0x82BD50 + deferred groups 0x828A00 / 0x829220 | `em_actor_roster_load` |
| `spawn_table.emsp` | the global spawn export; its area 1 rows are 0x24B1A0 | `em_spawn_table_load` / `_read` |
| `door_destinations.emsp` | EMSP windows: `D_0024E140[0..0x17)` and the area 1 row 0x24DFA0 (8 records) | `em_spawn_table_load` / `_read` |
| `area01_scripts/scripts.emsc` | the overlay script window 0x829860..0x82BD50 (14 chains, 137 records, plus the op14 table 0x82A900 they reference) | `em_script_image_load` |
| `overlay_data.emsc` | the whole overlay data section 0x828A00..0x82CD00 (groups, nest group, scripts, both placement tables) | `em_script_image_load` |
| `message_data.emmd` | EMMD v1 for area 1: 54 global and 184 area records, stream rows, both banks | `em_message_live_install` |
| `world_models.emwm` + `.json` | the model bank `*D_0028A59C` (22 models, 288,448 bytes) | `em_world_models_parse` |
| `sfx/sfx_registry.emsr` + `.json`, `sfx/area01_banks.bin`, `sfx/banks.json` | EMSR v2 for scope (1, 0) and the area's SShd container | `em_sfx_registry_load` |
| `level/level.json`, `tables.json` | counts, hashes and the verification summary (no text, no data) | none |

**Exported but not checked by the checker** (descriptive side files; the
port loads none of them, and nothing in `src/` loads the AREA01 copies
yet): `scene.txt`, every `level/*.gsmat.json` (the per-texture GS state
record; the same codes are inside the EMDLs and are checked there, but
the JSON's fields, including the RGBAQ alpha set `rgbaq_a`, are checked
only by the exporter's `write_zone`), `world_models.json`,
`sfx/sfx_registry.json`, `sfx/banks.json`, `tables.json`, and
`level/level.json` except its `cells.moved_uids`, which the checker
compares.

## Tools

All the tools run on native arm64 macOS in pure Python. Run them from the port root.

```sh
python3 tools/export_area01_level.py    # level, textures, materials, collision, cells, background, ctx block (~18 s CPU)
python3 tools/export_area01_tables.py   # roster, spawn, doors, scripts, overlay data, messages, world models (<1 s)
python3 tools/export_area01_sfx.py      # sound banks + registry (~1 s)
python3 tools/test_area01_assets_reference.py                  # checker, 4 captures
EM_TEST_FULL=1 python3 tools/test_area01_assets_reference.py   # all 12 captures
EM_AREA01_ASSETS=<dir> python3 tools/test_area01_assets_reference.py   # check another export tree
```

Every `--out` is resolved to an absolute path before anything runs, so a
relative `--out` lands under the current directory (the collision export runs
`export_collision.py` with the decomp tree as its working directory and now
receives absolute input, output and RAM paths).

Measured on the M1 (2026-09-26, close-out) while other lanes loaded the
machine. CPU is user + sys of the process and its children. It is the
stable figure; the wall time depends on the load.
- Quick run: 8.6-8.7 s CPU warm (12.2-13.4 s wall, load average 84-89);
  7.4-7.5 s CPU warm at load average 7-9 (7.5-7.8 s wall); 11.0 s CPU
  cold (15.9 s wall; a cold run recompiles the port loaders).
- Full run: 14.2-14.3 s CPU (25-26 s wall, load average 85-87).
- Every pinned kill case runs in the default run, the matrix-slot plant
  included (it was full-only before the close-out); the close-out's
  controls and plants did not raise the warm quick CPU measurably (7.4 s
  before and after at the same low load). Round 5 for comparison:
  8.3-8.4 s CPU quick warm and 14.2-14.4 s full at load average 72-79.
- The close-out re-runs (4 workers, load average up to ~95): the third
  sweep's and the final review's mutants, 75 quick jobs in 157 s wall and
  530 s CPU, their 27 quick survivors in full mode in 150 s wall and 416 s
  CPU; the round-5 set, 233 quick jobs in 698 s wall and 1,795 s CPU, its
  42 quick survivors in full mode in 294 s wall and 612 s CPU.
- The quick captures are the arrival, a01_05, a01_s1 (the freed-owner hull)
  and a01_s3.

`tools/export_area01_common.py` holds the shared inputs: the pinned ELF, the
overlay, the capture list and the load map. The exporters reuse the existing
exporters by import and change none of them:
- `export_level.py`: record walker, strip builder, texture decode, GS material
  code, and the disc upload replay;
- `export_collision.py`, run as a subprocess;
- `export_area11_roster.walk_roster/encode_roster`;
- `export_spawn_table.build_spawn_table`;
- `export_message_data.export`, with its `AREA` and area-bank constants swapped
  for the call and then restored;
- `export_world_models.model_record/serialize`;
- the resolver and EMSR writer of `export_sfx_registry.py`, with its binding and
  id lists swapped for the call;
- from the tests, `test_level_material_reference.Frame` (the display-list walk),
  `test_actor_collision_reference.prim_size`,
  `test_effect_manager_reference.Oracle` (the original-instruction EE
  interpreter) and `export_render_context`.

## How each asset is derived and checked

**Load map (the common base).**
- The map is built from the loader's own descriptors, not from labels about file
  roles. The top descriptor is INDEX.IDX sector 5 (D_00810700 + 4), which the
  loader reads into D_00289BC0. The nested descriptor for sub 0 sits at +0x100.
  Both come from the disc image, and each capture whose buffer still holds them
  must equal them.
- In a01_s1 and a01_s2 a later read left sector 36 in that buffer, and the
  report records this.
- The top block (`chunk05`, 6 files, 0x1A5000 bytes) lies whole at the first
  cursor D_0028A73C = 0x1335F40.
- The nested block `chunk05.n0` has an upload table with two entries:
  - entry 0, +0..+0x75000: the SShd sound container. Its bank headers stay in
    EE RAM at 0x14DAF40 + 0x50...
  - entry 1, +0x75000..+0x14D800: the level-load GS upload.
- The resident region of the nested block runs from +0x14D800 to the block end
  and lies at the second cursor D_0028A740 = 0x14DC940.
- These 21 file spans, 6,696,960 bytes, equal RAM byte for byte in all 12
  captures. The one exception is the cell directory hulls described below.
  The checker's allowance for them is per byte (`compare_load_map`): it
  excuses exactly the directory's 33,300 bytes, so the 12 bytes of the
  16-byte row the directory ends in are compared like every other byte.

**Level geometry.**
- The original draws the level through two routines (from the committed C of
  001D5370, 001C6120, 001D4FB0 / 001D4F30 and 001D5BD0 / 001D4FC0):
  - 001D5370 walks the 32 x 32 grid at render ctx +0x140. For each id it takes
    object `001C6120(*D_0028A5A0, id)` and REFs the object's units (word +0,
    0x820 bytes each, from +0x40) to the level kernel 0x00237180 (clip
    0x00239C90).
  - For stage 0x0100 / 0x0101 it also walks the dynamic list `*D_0028A5A4`
    (kernel 0x00237450, with the entry +0x34 translation).
- The bank at 0x14DC940 holds 694 objects. Object 0 contains the grid, and the
  grid's ids name exactly objects 1..693.
- The records of every object are walked with export_level's walker. They are
  grouped by source file into 12 EMDLs: n0 f00 (bank objects 1..340), then f01
  through f11 (f11 only up to the dynamic list).
- Every record has matrix slot 0, and every record carries a colour (no record
  needs a light rig).
- Captures: every textured level-kernel kick in both display lists of all 12
  captures REFs units inside a bank object. That is 6,460 kicks, and they touch
  518 distinct REF addresses.
- The GS state at every kick is PRIM 0x3C, TEST_1 0x5000D, ALPHA_1
  0x80000000A8, TEX1_1 0x60 and CLAMP_1 0. These are the class-0 constants the
  AREA11 materials use, so the `.gsmat.json` codes are computed the same way
  and written into the EMDLs.
- All 104 kernel-0x00237450 kicks REF entries of the 12-entry dynamic list.

- The static bank and dynamic list extents are pinned in every capture
  (round 4). The bank ends where its objects end: `bank_end` runs
  001C6120's lookup (object = base + (word[1 + id] >> 2 << 2) for ids
  0..693) and takes the last byte any object's units reach (0x820 bytes
  each, from +0x40), 2,742,624 bytes. The dynamic list is its 16-byte head
  plus word 0 (12) entries of 0x860 bytes, 25,744 bytes.

**Textures.**
- The texels come from the level-load GS upload replayed from the disc image
  (`export_level.background_disc_localmem`). The upload is one section of 2
  PSMCT32 transfers.
- The level uses 137 distinct textures, which make up 342 texture slots across
  the 12 zones. Each slot is decoded both from that replay and from every
  capture's GS freeze, and all 4,104 decodes (342 x 12) are equal.
- The checker decodes the texels again from the GS freeze of EVERY capture
  of the run (round 5; before, only the first capture's freeze was used)
  and compares each texture of each zone with every one of those decodes.
  Every capture must have a freeze (`gs_captures` stops the run
  otherwise, so no capture's texels can be skipped). The 342 slots name
  137 distinct textures (base, width, format, size, CLUT base), so each
  freeze is decoded once per texture (`gs_texels`, one
  `build_texture_blob` call per freeze).
- The checker compares every byte of each zone EMDL with a rebuild from the
  exported bank (`zone_problems`): the header, the bone parents, the texture
  entries (w = 1 << tw, h = 1 << th, the blob offset = the sum of the
  earlier textures' 4 * w * h bytes, the GS code), the clip block, every
  vertex (position, colour, UV, bone slot, texture slot), the indices, the
  palette and the texel blob, which must end the file. Some of these fields are
  constants of the EMDL format for static geometry, not original data: one
  bone plus the identity slot, one frame and one clip at 30.0 fps, the two
  identity palette matrices, and flags COLOR | GSMAT. The checker requires
  exactly these values. The 30.0 is the value `write_zone` passes, and it
  has no meaning for a single frame.

**Collision.**
- `export_collision.py --node-class` runs over n0 f12+f13. Those files hold the
  grid header D_0028A598 = 0x17C7940 (f12 +0x19800) and its pools.
- Its `--verify-ram` passes for all 12 captures, covering:
  - the header;
  - 854 nodes (+0x00..+0x3F each);
  - 2,027 grid vertices;
  - 3,344 edge normals and 3,344 indices;
  - the 12 rank tables;
  - the scratchpad pointers 0x700031FC..0x7000323C and 0x7000320C.
- The checker rebuilds the whole EMCL from the RAM grid (`emcl_from_ram`),
  written from the container layout without running the exporter, and
  requires the file to equal it byte for byte in every capture it runs:
  header and counts, bbox, the vertex pool (the grid vertices de-duplicated
  by float equality in first-use order), every polygon record (plane, first
  index, vertex count, set 4, attr +0x1A, class +0x1B), the node's s16
  vertex indices through the pool, the node's edge normals (node +0x1C /
  +0x20 into the header's index and edge-normal pools), the EMRK section
  (grid vertices, node words +0x00..+0x17, the 12 tables, padding) and the
  EMAX axes. A difference is reported with its offset and section.
- The grid pointer is pinned (round 4). D_0028A598 (word 0 of the
  world-section directory, which 00199C50 stages into the scratchpad) is
  0x17C7940 = chunk05.n0/f12 + 0x19800 in all 12 captures. The code that
  fills the directory at level load is not identified (FINDINGS), so the
  checker pins that measured value (`GRID`). It also requires the whole
  grid block, 0x17C7940..0x17EB4DC (the header, vertex pool, edge normals,
  index list, rank tables and 854 nodes, extents from the disc header), to
  lie in the load map and outside the cell-directory allowance. Since round
  5 it also requires every node's index run (node +0x1C, count byte
  +0x18, 2 bytes each) and edge-normal run (+0x20, 12 bytes each) to lie
  inside the index and edge-normal sections (all 854 do). So in every
  capture that passes, every byte that `emcl_from_ram` reads equals the
  disc, and the rebuild is the same in every such capture.
- The vertex pool de-duplicates by float equality, as `export_collision.py`
  does. That tool refuses a grid whose de-duplication would change a
  vertex's bits (+0.0 and -0.0 are float-equal), and since round 5 the
  rebuild refuses it too, as a bad RAM grid.
- The EMCL has no cell n-gons: `collision_probe.find_cell_lists` finds none in
  these files. The cell world is the directory below.

**Cell directory.**
- `area01_cells.bin` holds the disc bytes of the directory at 0x17EB940 (f13
  +0x6000): 38 uids, 24 hulls, 33,300 bytes. The directory's pointer and
  count in the scratchpad match in every capture. The checker compares the
  file with the disc bytes the load map names, over its full length.
- The load map excuses the arrival's directory bytes in every capture, so
  since round 5 the checker also requires each capture's scratchpad
  pointer 0x70003250 to equal the arrival's (the exporter's `check_cells`
  always did). Then every byte of that allowance is compared by the
  directory check below in every capture.
- Every byte of every capture's RAM directory is then compared. A byte
  equals the disc byte, except inside a hull whose captured bytes differ.
  Such a hull must equal what the **original 001A2370** (the hull
  re-transform, run in the EE interpreter `test_coll_move_reference.FloatEE`
  over the capture's RAM, with the directory set to the disc bytes) leaves
  when called for a live owner of that uid with the matrix that owner's own
  code passes (`export_area01_level.owner_matrix`). The RAM code of
  001A2370, 001026A0 and 00102738 must equal the ELF.
  - A derivation counts only if 001A2370 changed no byte outside the hull
    the directory walk gives it. That can happen: by its C, 001A2370 tests
    the extended flag 0x800 on the first primitive only and then steps
    every primitive by its extended size (box 0x24, capsule 0x2C, mesh
    0x24 + 0x30n), while the walk (`prim_size`) steps each primitive by
    its own flag. A control builds such a hull (hull 10 as [extended box,
    compact box, compact box]); the original writes 12 bytes past the
    walk's end, and the checker must report the hull.
  - The derivations are memoised on every input of the call (the ELF, the
    capture's RAM and scratchpad, the directory, the node and the matrix;
    the images by identity, and the memo entry keeps them alive, so an
    identity cannot be reused).
  - 0x219550 (pickup; the NEARMISS C passes self + 0xD0, and the original's
    call site sets the second argument to the node + 0xD0) and 0x8261A0
    (byte-identical C of 0x826200 / 0x826440: self + 0xD0).
  - 0x826D40 (original code, four call sites): `*(D_00275B40 + 0xC) + 0x90`.
    While a behaviour runs D_00275B40 = node + 0x110 (001CB590 and
    anim_bone_array_setup, compiled C; docs/AREA01_OVERLAY.md), so the
    matrix is `*(node + 0x11C) + 0x90`, the form AREA11's 0x825940 uses.
- Result, all 12 captures: hulls 10, 11 and 12 (0x826D40 nodes 0x7A70B0,
  0x7A7690, 0x7A7C70), 31 and 32 (0x8261A0 nodes 0x7B1530, 0x7B1240), 33 and
  34 (0x219550 nodes 0x7A5640, 0x7A5930) are re-derived byte for byte; every
  other hull, uid 0's included, equals the disc. No other hull differs.
- How much evidence this is: 83 runs of 001A2370, but only 7 distinct
  inputs. For each of the 7 owner nodes, the node address, the matrix
  address and the 64 matrix bytes are identical in every capture, so the
  83 runs repeat the same 7 owner transforms over 12 captures. They show
  that the derivation reproduces those 7 transforms. They are not 83
  independent tests of 001A2370.
- a01_s1: no live node carries uid 34 (slot 0x7A5930 holds another
  behaviour there). A freed owner's hull must equal a 001A2370 derivation
  of the same hull in another capture; hull 34 in a01_s1 equals it (every
  moved hull is identical in all 12 captures). This is the only exclusion,
  it is per uid and per capture, and it is proven, not assumed. A control
  pins the "per capture" part: the arrival plus a copy of it whose live
  owner of hull 31 (node 0x7B1530, behaviour 0x8261A0) has one matrix byte
  at node + 0xD0 + 3 changed. The copy's hull 31 bytes still equal the
  arrival's derivation, but its owner is live, so the checker must report
  "hull 31 differs and no owner derivation reproduces it" for the copy.
- The file must end where the directory ends: its length equals the
  directory size that `cell_directory` walks (4 + 4 * count, then every
  hull's primitives). A control appends the next disc byte, which the
  file-vs-disc comparison cannot see (it runs over the file's own length),
  and the size check must reject it.
- Every hull must start after the count and uid words (round 5,
  `hulls_inside_table`); the first starts exactly there, at 4 + 4 * 38 =
  0x9C. So those words are outside every hull, and the directory check
  compares them with the disc in every capture.
- The moved set is recorded in `level/level.json` (`cells.moved_uids`, with
  the proving node and behaviour per capture); the checker requires its own
  result to equal it.
- The exported bytes stay the disc rest state, as for AREA11: the runtime
  re-transforms them per owner through 001A2370.

**Background and the render-context block 001D8FD0 writes.**
- No capture arms the background: render ctx +0x174 bits 0/1 are 0 in all 12
  captures. AREA01 therefore has no `background.embg`; the port's AREA11
  background asset has no AREA01 counterpart.
- Render ctx +0xA0..+0xFF. 001D8FD0 is, by its committed C, a camera/transform
  update: `p = 001D7B30()` selects the first 0x78-byte entry of the room table
  D_00251C50 whose word 0 equals 0xF00 when 001D2910(8) is non-zero, else
  `D_00810700 << 8 | D_00810701`. When `001B0070() & 0x80` (001B0070 returns
  D_008106C8) it passes the constants 0.0 / 110.0 and 0, 0, 0; otherwise p
  +4 / +8 to 0021B970 (ctx +0xB8 / +0xBC) and p +0xC / +0x10 / +0x14 to
  0021BA80 (one packed 24-bit word). docs/RENDER_CONTEXT.md reads the context
  word +0xB0 as the GS FOGCOL value; that is where the word "fog" in earlier
  versions of this doc came from, and this doc no longer uses it as a role.
- Check: the ORIGINAL 001D8FD0 runs through the EE oracle
  (`test_effect_manager_reference.Oracle`) over each capture, with ctx
  +0xA0..+0xFF first overwritten, and rebuilds all 96 captured bytes. In all
  12 captures D_008106C8 is 0x8D00 or 0x8D01 (bit 0x80 clear, so the room
  branch ran) and the rebuild read entry 3 (key 0x0100, area 1 sub 0):
  flipping that entry's +4..+0x17 changes the rebuilt bytes (63 of 96 in
  a01_00), flipping entry 34 (key 0xF00) changes none.
- So the bytes come from the room table, which the global
  `assets/render_context.emrc` exports (its blocks equal RAM in all 12
  captures, 8,794 compared bytes each, run-time words excepted), selected by
  run-time state (the area bytes, 001D2910(8) and D_008106C8). A port must
  evaluate that selection at run time; the export does not hold a
  precomputed AREA01 block.

**Roster.**
- `walk_roster(area 1, sub 0)` gives placement table 0x82BD50 with 54 records,
  and deferred groups 0x828A00 (40 records) and 0x829220 (6 records).
- The group and placement bytes equal RAM in every capture. Since round 4
  the checker rebuilds the whole EMRO itself (`roster_image`) by the
  original's walks and requires the file to equal it byte for byte: once
  over the pinned ELF + overlay, and once over each capture's RAM.
  - 001B6910: the group list D_0024D820[1][0], each group item walked
    until a 0x2C-byte record whose s16 word 0 is -1, while the list's next
    word is non-zero;
  - 001B6990: D_0024D7C0[1][0], walked until a 0x28-byte record whose s16
    word 0 is 0xFF.
  So the header, the group count and addresses, every record count, every
  record and the length are pinned to the disc.
- For each record, the live nodes whose copied fields all match were checked.
  The copied fields are the ones 001B6990 writes (+0x03, +0x2E, +0x0D, +0x0E /
  +0x9E, +0x54, +0x56, +0x9A, +0x10) and the ones 001B6660 writes for groups.
- In every capture 49 placement records have their node:
  - 41 match every copied field;
  - in 8, the owner has rewritten one field: the seven door nodes cleared
    +0x2E, and [36] set +0x56 to 1.
- 46 of the 49 are still at the record's position and rotation. [37] (0x826CF0)
  and [41] / [42] (0x8261A0) are not.
- The checker asserts exactly (49, 46) in every capture it runs, so an empty
  or unmatched pool cannot pass vacuously. Controls: an empty actor pool,
  and one node moved off its record position, must each be reported.
- Group records 0x828A00 [30..35], [37] and [38] have moved as well.

**Spawn and doors.**
- The global `spawn_table.emsp` is byte-identical to `assets/spawn/spawn_table.emsp`.
- The area 1 sub-0 rows at 0x24B1A0 (10 records) and the room pointers equal RAM.
- The checker compares every window of both EMSP files with RAM in every
  capture it runs, and their headers (magic, version 1, the zero word
  +0x0C) and exact lengths. `spawn_table.emsp` has 16 windows (11,500
  bytes): 0x24AA50 (11,356 bytes, holding the area 1 rows) and 15 small
  windows 0x275500..0x2755E8 (144 bytes). The round-2 checker skipped those
  15; all of them equal RAM in all 12 captures. `door_destinations.emsp`
  has 2 windows (0x24DFA0, 32 bytes; 0x24E140, 92 bytes).
- The window SET is pinned too (round 4): the list of (address, size)
  must equal a walk the checker does itself over the pinned ELF, so a
  dropped or shortened window fails.
  - The area count is the index of D_0024E140's first zero word: 0x17
    (areas 0..0x16 each name a door row; the disc's OVERLAY directory holds
    AREA00..AREA22).
  - Spawn (`spawn_layout`, as 001B07C0 / 001B0250 index the table):
    D_0024D650[0..0x17); each non-null area's room pointer array up to and
    including its first zero word, at most 4 words; each distinct room
    entry array up to the next one in address order (the last one up to
    the lowest room pointer array or D_0024D650); touching windows merge.
  - Doors (`doors_layout`): the area 1 row D_0024E140[1] = 0x24DFA0 up to
    the next address the array names (or the array itself), and the array
    D_0024E140[0..0x17), 92 bytes.
- In every capture D_008106C8 equals +0x1C of the record for the current
  D_00810702: 0x8D00 for entries 4 and 2, 0x8D01 for entry 1.
- The door row 0x24DFA0 (8 records) and D_0024E140 equal RAM.
- Every door placement's id resolves inside the row: [12] id 0, [14] 1, [15] 2,
  [16] 3, [17] 4, [18] 5 and [19] 6.

**Scripts and overlay data.**
- All 14 chains walk inside the window, and the window equals the arrival RAM.
- Later beats show words the game rewrote while a chain ran. All of them are
  word +0x10 of a record: 0x829B20 (from a01_03), 0x82A260 (from a01_05) and
  0x829EA0 (a01_s0, a01_s2).
- The overlay data window shows these same words and no others.
- The export is the load-time image, so the checker compares both windows
  with the arrival RAM only, every byte, and their EMSC headers: magic,
  version 1, the entry word equal to the base (a data window has no entry
  point; the exporters write the base there) and a file length of exactly
  20 + the window length.
- Round 4 pins each window's extent and bytes to the disc module. The
  checker pins `extract/OVERLAY/AREA01.BIN` by its SHA-256, as it pins the
  boot ELF; the constant is a hash, not disc data. Then:
  - the overlay data window is the module's data section: its last
    data-size bytes (MWo3 header word 4 = 0x4300), loaded whole at the
    header's load address, 0x828A00..0x82CD00;
  - the scripts window runs from the lowest of the 14 chain entries
    (`export_area01_tables.SCRIPT_ENTRIES`, 0x829860) to the placement
    table D_0024D7C0[1][0] = 0x82BD50;
  - both windows must equal the module bytes (`disc_reader`), and the
    arrival RAM.

**Messages.**
- The area table D_00264DD0[2] = 0x26F060 holds 184 records. The global table
  holds 54 and there are 36 stream rows.
- The area bank is `chunk05/f00_id41.bin` at 0, which is what
  `*D_0028A594 = 0x1335F40` points at; its extent is 8,927 bytes. The global
  bank is 3,029 bytes.
- Records and both banks equal RAM in all 12 captures. The checker compares
  every byte of the file (`messages_problems`):
  - the header: magic, version 1, area 1, the cursor token 0x264D10, and
    the counts and bank sizes, which must add up to the file length;
  - the colour, line-config and template blocks (0x26EC10, 0x264CD0,
    0x264BF0), against RAM;
  - the records, at the D_00264DD0 pointers (which must equal the ELF's);
  - the 36 stream rows, against RAM at D_0026EC60. The RAM row after the
    last one must be the -1 row, and no row before it may be;
  - both banks.
  - the counts and bank sizes against their own sources (round 4). Each
    record count is where the next table the original points at begins,
    read from the pinned ELF: every non-zero D_00264DD0 word, D_00275848
    [0..2) and D_00264E40[0..0x17), giving 54 and 184. Each bank size comes
    from the RAM bank's own header, as the accessors 001FE460 / 001FE480 /
    001FE4B0 / 001FE4D0 reach it: h = word 0 + word 2, and the size is
    h + word(h) + word(h + 8), giving 3,029 and 8,927 bytes.
- No text is printed or stored anywhere other than the ignored asset.

**World models.**
- `*D_0028A59C = 0x1781140` (n0 f11 +0x56000) holds 22 block models, which
  pass export_world_models' header and VIF checks. The span equals RAM.
- The checker compares the whole file (`world_models_problems`): magic,
  version 1, the table address (= D_0028A59C in every capture), the span
  length (= file length - 32), the model count (= the table's own word 0),
  the three zero words, and every span byte against RAM.
- The span is pinned (round 5; before, it was only the file's own length
  - 32, and a file grown by the RAM bytes after the span, header
  consistent, passed). `wm_span` computes the bank's extent over each
  capture's RAM from the original's reads: the table (word 0 = the count,
  then one offset word per model) and, for each model at table + (word[1 +
  id] >> 2 << 2) (001C6120's lookup, signed), its 0x40-byte header, its
  block data (+0x40, header word 1 quadwords) and its skeleton (header
  word 3 = its offset, word 2 records of 0x50 bytes), as the draw reads
  them (export_world_models' docstring, docs/OWNER_DRAW.md). The largest
  end is 288,448 bytes (0x466C0), the exported span, in all 12 captures.
  This is the exporter's rule (the largest model offset + skeleton +
  0x50 * bones), written from the layout rather than from `model_record`.
- 31 owners are bound in every capture, 30 in a01_s2. For each one:
  - +0x44 = 001C6120(table, +0x0D);
  - the bone count matches;
  - every bone slot is set.
- The index lists which behaviours use each model. Ids 0x0, 0x1, 0x7, 0x9 and
  0xE were bound by no owner in any capture.

**Sound banks.**
- The bindings come from the driver tables of all 12 captures (D_00281D50 and
  D_0027C6C0):
  - group 1 → handles 0..2, the global container rows, as in AREA11;
  - group 2 → handle 4, area container row 0 (type 2);
  - group 4 → handles 5 and 6, rows 1 and 2 (type 4).
- Each bound header equals its container row except the 48 per-track bytes
  +0x0A. This is the AREA11 rule.
- Group 3 → handle 3, a header without the SShd magic, which is refused as in
  AREA11.
- The container is `chunk05.n0/f00_id44.bin` +0..0x74C10, the nested
  descriptor's upload entry 0.
- The registry resolves ids 0x3E8..0x5DB and 0x7D0..0x9C3 for scope (1, 0):
  - 403 audible;
  - 561 originally absent (remap 0xFF);
  - 36 unsupported, exported as UNSUPPORTED rather than approximated:
    23 modulation and 13 whose record names a bank no capture binds.
  - The audible entries use 46 samples.
- Content checks in the checker:
  - `area01_banks.bin` equals `chunk05.n0/f00_id44.bin` over its full length
    (headers and every bank body), its length equals the container total
    (word 0) and lies inside upload section 0, and the bank bodies end at
    that total.
  - `sfx_registry.emsr` equals, byte for byte, a re-derivation written in
    the checker without export_sfx_registry.py (`registry_from_ram`): every
    table is read from the capture's RAM at the addresses the original reads
    (001FB9F0's remap and record tables, D_00281D50 / D_0027C6C0 for the
    handle and header, 00119EA0's script-table walk from its C with the
    header's own offsets, 001152D8's event loop with the step 0x1E0000 /
    D_0027F740[0x1D], the program and tone records, the 00117918 ladder
    D_00241D70 and the pan table D_00242630); every sample is decoded from
    the disc bank body its tone names with the SPU ADPCM rule. Entries,
    reasons, operations, the ladder and all PCM are covered; it runs over
    every capture of the run.
- Not verified: SPU residency of the samples. No AREA01 SPU capture exists.
- The 13 "unbound bank" ids are 0x9B7..0x9C3, the tail of the area 1
  sub 0 record table, whose records read (0,0,0,0), (0,1,2,3), (4,5,6,7) ...
  (32,33,34,35) and three (-1,-1,-1,-1): a table running on past its
  entries. 001FB9F0 (NEARMISS C, logic faithful) does not test the handle
  it reads from D_00281D50 + 4 (group * 0x14 + slot). Following the original
  over the arrival RAM: 11 of them return -1 in 00119EA0 (handle 0 without
  that script, or a handle word >= 0x80), so the original plays nothing;
  0x9B8 and 0x9B9 read handle 0 (the in-use global row 0) and find a script
  there. The export keeps all 13 UNSUPPORTED rather than claiming what they
  play; the checker reproduces exactly that. No capture shows these ids
  requested.

## Checker and mutation sweep

`tools/test_area01_assets_reference.py` compares every byte of each file
listed below with an independent source, and pins that file's extent
(where its tables, windows or data start and end) to a source that the
file itself does not supply:
- `area01.emcl`: the RAM grid rebuild, with D_0028A598 pinned to 0x17C7940
  and the grid block inside the load map (see Collision);
- `area01_cells.bin`: the disc bytes (over its length, which must be the
  directory's), plus the 001A2370 derivation per capture;
- `area01_banks.bin`: the disc file;
- `sfx_registry.emsr`: the RAM re-derivation, for every capture of the run;
- the zone EMDLs: a rebuild from the bank, and the GS freezes;
- `roster.emro`: the whole file equals the rebuild by the original's walks,
  over the pinned ELF + overlay and over every capture's RAM;
- both EMSP files: the window set equals the checker's own walk over the
  pinned ELF, and every window equals RAM in every capture;
- the message file: RAM in every capture; the record counts come from the
  pinned ELF and the bank sizes from each RAM bank's own header;
- the world models: RAM in every capture (the count = the table's word 0,
  the span = the file length - 32 = the bank's extent `wm_span` over each
  capture's RAM; round 4 took the span from the file alone, which the
  round-5 review showed: a file grown by the RAM bytes after its span
  passed);
- the static bank and the dynamic list: RAM in every capture (with
  D_0028A5A0 / D_0028A5A4); their lengths from the bank's object table and
  the list's entry count in RAM;
- the scripts and overlay data: the arrival RAM (the load-time image) and
  the pinned disc module; their extents from the module header and the
  placement table.
The pinned inputs are the boot ELF and `extract/OVERLAY/AREA01.BIN`, each
checked against its SHA-256 before use. The load map itself must be in
address order with no entry overlapping the next (`load_map_problems`,
close-out), so the checker's sorted `LoadedImage.map` and the exporter's
map order agree and every mapped address belongs to one file.

All real checks run in one function, `run_checks`. The ctx block is checked
in every capture of the run in both modes (~0.01 s per capture).

**Canary** (every run). `run_checks` runs a second time over two captures,
the arrival and a copy of it named `canary`, and over a copy of the export
tree (symlinks plus changed files, under the ignored
`build/area01/assets/test/canary-<pid>/`, removed afterwards). Each section
must report its own planted difference.

Round 5 rebuilt the plants after the review found four per-capture
comparisons that read only the first capture and still passed the canary
(the roster RAM rebuild, the dynamic list, the EMCL rebuild and the GS
texels). The cause: their plants were in the files, so every capture
reported, including under a mutant that compared the first capture's data
under the looping capture's name. The rule now:
- **Every per-capture comparison below is planted in the canary copy only**
  (its RAM, its scratchpad or its GS freeze), and the canary requires the
  planted message for `canary` AND the same message's absence for the
  arrival. So a comparison that reads only the first capture fails the
  canary, whatever name it reports under.
- **File plants are used only where the comparison is not per capture**,
  and the arrival's silence on every per-capture message shows that they
  reach no per-capture comparison.

Plants in the canary copy (34 sections in all, with the file plants):
- load map: the static bank's last byte (also the static bank compare);
- the cell-directory pointer: the directory copied to unused RAM
  0x1D00000 and the scratchpad pointer moved there; uid 0's hull byte
  +0x19C flipped in that copy (the RAM directory compare: "hull 0
  differs");
- the static bank: its last byte, and the object that ends last one unit
  longer (the bank extent `bank_end`);
- the dynamic list: its count word 12 -> 13 (the list bytes and the list
  length); D_0028A5A0 and D_0028A5A4, byte 2 each;
- the GS state: TEST_1 at 0x8153A0; the zone texels: the canary's own GS
  freeze (a copy in the canary tree) with the texel block of the second
  zone's first texture inverted (that zone is neither renamed nor
  file-planted, so only a memo that ignored the freezes could reuse its
  real-run verdict);
- the grid: the block copied to unused RAM 0x1C07940, whose low half
  equals GRID's (so a pointer compare of the low half alone cannot report
  it), D_0028A598 pointed there (the grid pin), and a vertex byte of the
  copy flipped (the EMCL rebuild);
- the roster: the last group record's last byte (0x829327, the RAM
  rebuild), one placement node's +0x03 (a copied field) and another node
  moved off its record position (the (49, 46) count);
- the spawn and door windows: the last byte of each;
- the messages: the global bank's last byte, the area bank's header word 2
  (its size: the counts / bank sizes check), D_00264DD0[0], the
  line-config block, an area record and a stream row;
- the world models: the span's last byte, and one bone more on the model
  that ends last (the span pin);
- the registry: a pitch-ladder byte (the RAM re-derivation);
- ctx +0xA4 (the 001D8FD0 rebuild).

File plants (not per capture): a position byte of the last zone EMDL (the
zone rebuild), the first zone renamed in its file part (00_f00 -> 00_x00)
and the third zone renamed in its id part only (id + 1; close-out, S23)
(the name check compares the whole source stem), a 13th zone file (the
count check), the last uid dropped from `level.json`'s moved set, and the
bank container's last byte (the container against the disc). Every run
(full mode only before the close-out) also plants a matrix-slot bit in
the first record of the last bank object, in its own `check_level` call
over the arrival alone (the zone rebuild changes, which costs a second
rebuild, and a bank-file plant would reach every capture's bank compare);
it is the kill of S24 and S26.

Per-capture comparisons that the canary does not plant (the bank-binding
walk of `export_area01_sfx.bindings_from_captures`, an exporter function
the checker reuses, and the scripts / overlay data, which are compared with
the arrival only by design) are covered by the single-capture controls.

The loaders get their own canary: every file the port loads is replaced by
one its loader must refuse, and each of the 24 loader verdicts (the 13 zone files of the canary tree included) must report.
- The spawn file keeps every window except the one holding 0x24B1A0: the
  load passes, and the read of the area 1 rows fails.
- The door file keeps only the pointer array: the read of 0x24DFA0 fails.
- Each EMSC keeps its header, with a 32-byte window (less than one 64-byte
  record).
- Every other file, and every zone file in the tree (including the renamed
  one and the extra 13th), is cut to its first 16 bytes.
Both modes report 59 canary sections (34, the matrix-slot plant and 24
loader verdicts; before the close-out: 57 quick, 58 full).

**Controls** (every run, only after every real check and the canary
passed; each one changes an input and requires its comparator to report,
or, where marked, to accept):
- **roster:** the header (+0x08, +0x09 sub, +0x14), the first and the last
  placement record, the group table (+0x25, a clean problem, not a crash),
  the last byte of the first and of the last group, a trailing byte, the
  last group record dropped and the last placement dropped (header counts
  consistent), a group record byte in RAM only, and the same byte in the
  file and RAM but not on the disc.
- **placement nodes:** the first and the last byte of each copied field
  (+0x03, +0x0D, +0x54 / +0x55), +0x03 again through `roster_problems` (the
  node still counts, so only the copied-field verdict can report it), the
  behaviour word's bytes +0x12 and +0x13 (the node no longer counts), an
  empty actor pool, and a node moved off its record position. A record
  whose class byte is 0x8B is still processed (accept: 49 / 46); one whose
  class byte is 0x0B is skipped (48 live).
- **EMSP:** for both files, the last byte, the first window's last byte,
  the header words +0x04 and +0x0C, and a trailing byte; for the spawn
  file, a byte of the area 1 rows and one window byte in RAM only. Window
  sets: for the spawn file, the last window dropped, window 1 dropped, the
  last window one byte short, and the first window four bytes short; for
  the door file, the pointer array one word short and the row one word
  short.
- **scripts, overlay data:** the first, middle and last window bytes, the
  version and entry words, a trailing byte, the window four bytes short,
  for the overlay data the base moved by 4 (first word dropped), a byte
  changed in the file and RAM but not on the disc, and a byte changed in
  RAM only. A changed overlay module is refused by the SHA-256 pin.
- **world models:** the first and the last span byte, a trailing byte, the
  version, table and count words, each of the three zero words, a span of 0
  (a clean problem), and D_0028A59C in RAM.
- **messages:** every byte of the header and of the three ELF blocks
  (+0x00..+0x8B), the first and the last global record, the global bank
  (middle and last byte), the area bank's first byte, the last area record,
  a middle and the last stream row, a trailing byte, and each count or bank
  size one unit short with the header kept consistent (global bank, area
  bank, global records, area records, stream rows). On the RAM side: the -1
  row removed, a -1 row planted early, a -1 row at row 0 (in the file and
  RAM), the area table pointer, and D_00264DD0[0] + 8. A row whose first
  word is 0x0001FFFF, in the file and RAM, is not a -1 row (accept).
- **check_tables end to end:** the last byte of each of the seven table
  files, and a truncated world-model file (a clean FAIL, not a crash).
- **moved hulls:** a wrong moved-hull set, on either side.
- **load map** (single RAM bytes): the first and the twelfth byte after the
  cell directory (the 16-byte row it ends in; the allowance is per byte),
  and the last byte of the first and of the last mapped file. Also
  `LoadedImage.locate` at every file seam must return the next file.
- **level data:** the static bank at 3/4 of its length, its last byte, a
  trailing byte, one and four bytes short; the dynamic list's last byte,
  one byte short, and its last 0x860-byte entry dropped (count word still
  12); the version, base and entry words of the bank and the list's
  version and base words; D_0028A5A0 / D_0028A5A4 in RAM; the level GS
  state with each of its five fields changed.
- **grid:** D_0028A598 changed in RAM, the allowance over the first and
  over the last node byte, the map without f13 (the nodes leave it), the
  form word 0xD, a count word 0x8000, and a first vertex index equal to
  the vertex count. The last three must be refused as a bad RAM grid,
  not reported as a difference from the file: a count word >= 0x8000 is
  negative for the original's s16 read (no nodes). Also node 0 made one
  vertex longer (3,345 indices): the rebuild must pad the odd index list
  so that the edge normals start where the layout puts them.
- **EMCL:** vertices (+0x289), indices (+0xA126), a plane, an edge normal, a
  rank byte, the bbox, the EMAX tail (the last byte) and trailing bytes.
- **ctx block:** a captured byte at ctx +0xA0, +0xA4, +0xFC and +0xFF, and a
  capture forced onto 001D8FD0's constant branch.
- **cell file:** uid 0's hull (+0x19C), a hull word, a moved hull's derived
  and source lanes, the last hull, the last byte, and the file plus the next
  disc byte (see Cell directory). Also the last byte flipped in both the
  file and RAM: RAM agrees, the disc does not, and the check must still
  fail.
- **RAM directory:** hull 0, the last byte, a hull word, the count word, and
  the freed-owner hull 34 (a01_s1). Also 001A2370's code, the scratchpad
  count one above and two below, and the two-capture owner-matrix control
  (see Cell directory).
- **sound:**
  - the registry middle and one entry; the bank body (+0x20000), byte 0, a
    container row (+0x40), the last byte and a truncated file;
  - a second capture whose ladder D_00241D70 differs (its registry must be
    re-derived and reported on its own);
  - a container longer than upload section 0;
  - the ADPCM decode saturating at 32767 / -32768 on two synthetic blocks
    (filter 1, shift 0, all nibbles +7 or -8); header bounds: shift 12 and
    filter 4 decode, shift 13 and filter 5 are refused;
  - the binding verdict, both as a unit (four or two area-bank bindings, no
    group 3 refusal) and through `sfx_problems`, where group 4 slot 1's
    handle is cleared and two area-bank bindings are left;
  - the handle bounds: (2, 0) is bound, while (1, 0x14), (6, 0), (-1, 0)
    and (0, -1) read no handle;
  - the channel defaults: row 47 byte 3 or 0xC and row 1 byte 0xE differing
    are refused, equal rows are accepted;
  - the tick step: a synthetic script at tempo 25 with delta 0x60 ends at
    tick 6 (the integer step 0x1E0000 // 25 = 78,643 leaves 0x60 << 12 at
    5.00001 steps; a float step gives 5), and a 60-event script longer than
    0x100 bytes still parses;
  - the pitch (`pitch_00117918`): note == center reads ladder[0xD0 + fine];
    a ladder entry of 50,000 at an octave up wraps the 32-bit product to
    pitch 2,396; ladder 17,832 gives 0x3FFF, while 17,833 (0x4000) and 1
    (0) are refused.
- **zone EMDL (25 fields):** texel, position, colour, U and V (first
  vertex), bone slot, texture slot, index, the last vertex (a position
  byte, V and its texture slot), the last index, fps, flags, both bone
  parent words (+36, +40, +43), texture-entry offset, width and height
  (round 5), GS code, clip block, second palette matrix, last byte, and a
  trailing byte. Also a texture offset shifted by one texel, and a rebuild
  coordinate a quarter float32 ulp away (exact in the host double), which
  writes the same float32 and must compare equal.
- **Round 5 (`sweep2_controls`),** one input per non-equivalent survivor
  of the second independent sweep, each on the comparator it weakened:
  - world models: the span grown by the 4 RAM bytes after it (header
    consistent: WM-GROW) and shrunk by 4 are reported; a table without
    models (span 4, count 0, RAM word 0) is accepted; `wm_span` over
    synthetic tables where each term is the largest in turn (the header,
    the block data, the skeleton, an offset word with its low bits set,
    and models below the table, whose signed offsets leave the table's own
    extent);
  - a capture without a GS freeze stops the run;
  - D_0028A5A0, D_0028A5A4, D_0028A59C and D_0028A598, byte 2 and byte 3
    each;
  - two captures, only the second one changed: the last group record's
    last byte (exactly one problem, the second capture's roster RAM) and
    dynamic list +0x100 (exactly the second capture's list);
  - the dynamic list with count word 0x10001 and one entry, in the file
    and RAM (0x10001 entries, not 1);
  - object 5's offset word -0xC in the file and RAM, RAM there 0: accepted
    (001C6120's >> 2 << 2 keeps the sign; an unsigned read leaves RAM);
  - placement record word 0 = 0x010B is class 0x0B (skipped, 48 live);
    an at-rest node's +0xB0, +0xBB, +0xC0, +0xC9 and +0xCB each give
    (49, 45);
  - the grid: an allowance over the first and over the last byte of each
    of the six sections reports that section alone; allowances that only
    touch the block (ending at 0x17C7940, starting at 0x17EB4DC) are
    accepted; the last node's index or edge-normal run one entry past its
    section is refused, ending exactly at the section end is accepted;
  - the EMCL rebuild: grid vertex 1 a twin of vertex 0 with x = -0.0
    against +0.0 is refused (the pool would change its bits), an exact
    twin is merged; a vertex index -1 is a bad RAM grid;
  - the moved set: level.json's list with two uids swapped, or one
    repeated, is reported;
  - 00119EA0: a header +0x20 word of 0x0000FFFF gives the unmodified
    result, 0xFFFFFFFF gives none;
  - 001152D8: a B0 event whose first byte is 0x07, and the end event
    FF 2F 01, are refused;
  - ADPCM: a tone whose end block is the body's last 16 bytes decodes; a
    repeating block whose loop does not settle (filter 1, shift 8) is
    refused;
  - a container exactly as long as upload section 0 is accepted;
  - the 001A2370 overrun: hull 10 rebuilt as [extended box, compact box,
    compact box] (Cell directory), the capture showing the derived hull
    and the disc bytes after it: the original writes 12 bytes past the
    walk's end, and the hull must be reported;
  - a derivation that changes only the hull's last byte is a proof (accept):
    hull 10 rebuilt as [extended box, compact mesh of one lane pair]
    (walk sizes 0x24 and 0x2C). 001A2370 steps the mesh as extended,
    reads its axis and lanes after the walk's end and writes its lanes up
    to exactly that end; the directory is the call's own result with the
    last byte changed, so the call changes that byte alone;
  - two owners of different uids passing one matrix (hull 10's 0x826D40
    owner made to pass hull 31's owner's node + 0xD0; hull 10 in RAM is
    that derivation): accepted, each derivation is its own;
  - the texture key: zone 0's first texture made two pages high (so tbw
    matters), and a texture differing from it in each of tbp0, tbw, psm,
    tw, th and cbp decodes to its own texels (not the cached ones);
  - the unchanged first zone file compares equal after all the zone
    controls (the zone memo is keyed on the file bytes).
- **Close-out (`sweep3_controls` and the main list),** one input per named
  survivor of the third sweep and the final review (the table under
  "Close-out"): the zone GS code's top byte, clip block word 0 and the
  first palette byte; the first byte of the first and of the last mapped
  file (single RAM bytes, like the other load-map controls); a load map
  with two entries swapped, and one with an entry one byte into the next;
  the static bank's extent over two synthetic banks (an object below the
  bank ending it in the first capture only; a unit word 0x10001) and object
  5's offset word + 1 (accept); grid form 0x10C; the last byte of each
  001A2370 code range; room entry 44 + 0x30; hull 31's owner in pool slot
  0xFF with byte 0 = 2 (accept, with that node as the proof); remap byte
  0x80; two loop-start blocks; handle 0x80; a group word 0x8000 | x; a
  B0 41 portamento event; and a (1, 1) record without a handle (unbound).

**Close-out** (2026-09-26, after the third independent sweep and the final
review; it replaces the open-ended sweep loop).

What this standard claims, exactly: **305 single-operation mutants were
tried across the rounds that ran against the round-5 code** (the sets
below); **239 are killed, 66 are proven equivalent** (each listed with its
proof: the 40 of the round-5 set under "Mutation sweep, round 5" and
"round 4" below, the other 26 here); **no named survivor remains; the
sweeps did not converge, so this covers these mutants only.** It is not a
claim about every possible mutation.
- The round-5 set (`build/area01/assets/fix5/`, re-run on the close-out
  code in `build/area01/assets/close/fix5/`): 186 code mutations (round
  4's W / N list, the second independent sweep's M list and the round-5 R
  list) and 45 asset mutations: 191 killed (146 code, 45 asset), 40
  proven equivalent.
- The third independent sweep's set (`build/area01/sweep_ASSETS_3/muts.py`):
  S01..S49 and 8 asset mutations: 32 killed (24 code, 8 asset), 25 proven
  equivalent.
- The final review's F01..F17 (`build/area01/final_ASSETS/muts.py`): 16
  killed, 1 proven equivalent (F10).
- The baselines (W00, M00, S00, no change) survive, as they must, and are
  not counted. Mutants of rounds 1..3 and of the first two independent
  sweeps as they ran against older code are not counted either (receipts
  in `build/area01/assets/fix`, `fix2`, `fix3`, `build/area01/review_ASSETS`,
  `sweep_ASSETS_1`, `sweep_ASSETS_2`); the ones that still apply were
  re-anchored into the round-5 set.
- The judge is the default (quick) run; the `EM_TEST_FULL=1` run was
  repeated on every quick survivor and killed none of them. A crash or a
  hang (300 s timeout) counts as killed. Receipts (ignored scratch):
  `build/area01/assets/close/` (`muts.py`, `job.py`, `drive.py`,
  `drive_ids.py`, `drive_quick.log`, `drive_full.log` and their
  `results_*.jsonl`) and `build/area01/assets/close/fix5/` (the same for
  the round-5 set). Each mutation is exec'd in a subprocess; no lane file
  is written.
- Scope, unchanged from round 5: the checker, the shared
  `compare_load_map` / `LoadedImage`, and the level exporter's derivation
  functions the checker calls. The canary and the controls are the killing
  machinery; a mutation that only weakens them is detected only when a
  comparator is also broken, and no claim is made for such mutations. The
  exporter functions the checker reuses as its reference are not covered
  (listed at the end of this section).

**Checker changes in the close-out** (each stricter; the real data passes):
- `load_map_problems` (in `run_checks`, every run): the map
  `build_load_map` returns must already be in address order (the order
  `LoadedImage.map` sorts into and the exporter's `file_of_order` uses),
  and no entry may overlap the next. The real map (21 files) passes, with
  touching seams.
- The canary renames the third zone in its id part only (`02_f02_id44`
  -> `02_f02_id45`): the name check compares the whole source stem.
- The canary's matrix-slot plant runs in every mode (it was full-only). It
  kills S24 and S26 in the default run.
- New controls (`sweep3_controls`, plus three zone fields and two load-map
  bytes in the main control list), one per named survivor below.

**Named survivors** (the final verdict's list, sweep 3's non-equivalent
survivors and the review's F17), each re-run on the close-out code
(`build/area01/assets/close/drive_quick.log`, `results_quick.jsonl`):

| Mutant | Outcome | Killing input (the first failure) or proof |
|---|---|---|
| S01 GS code compared on its low 24 bits | killed | zone control: the first texture entry's GS code top byte (+15) flipped |
| S02 first palette matrix not compared | killed | zone control: the first palette byte flipped |
| S03 clip block word 0 not compared | killed | zone control: clip block +0 flipped |
| S05 bank extent `!=` -> `<` | killed | two captures over a synthetic bank at 0x1C00100 (count 1, offset word -0x40, so the object and its unit word lie below the bank): 1 unit in the first (the objects end exactly at the bank's end), 0 in the second; exactly "second: static bank length" must be reported |
| S07 bank_end unit word read as u16 | killed | a synthetic bank with one object at +8 whose unit word is 0x10001, the length by its low half: "static bank length" |
| S08 bank_end without `>> 2 << 2` | killed | object 5's offset word + 1 in the file and RAM: accepted |
| S18 grid form on its low byte | killed | form word 0x10C: "RAM grid: grid form 0x10c" |
| S23 zone name check on `fII` only | killed | canary "zone id" (the id-only rename) |
| S24 zones memo without the bank file | killed | canary matrix-slot plant (now every run) |
| S26 matrix-slot records tolerated up to 1 | killed | canary matrix-slot plant (now every run) |
| S31 remap sentinel `>= 0x80` | killed | a played id's remap byte 0x80 is record 0x80 |
| S32 ADPCM loop start at the first flagged block | killed | blocks flagged 4, 4, 3: loop frame 28 |
| S34 handle bound `<= 0x80` | killed | D_0027C6C0 entry 0x80 a copy of a valid one: still no script |
| S36 empty group `x & 0x8000` | killed | a group word 0x8000 \| x (with the words it selects planted): the planted script address |
| S37 `locate` searches the map in reverse | proven equivalent | see below |
| S38 the map kept in load order | proven equivalent | see below |
| S39 the last 001A2370 code range not compared | killed | the last byte of each of the three code ranges changed in RAM: exactly "001A2370 code in RAM differs from the ELF" |
| S40 the render-context block compare dropped | killed | room entry 44 + 0x30 (0x253120, never read by the rebuild) changed in RAM: `check_ctx_room_block` must stop with that address |
| S41 live node test `== 1` | killed | hull 31's owner moved to pool slot 0xFF with node byte 0 = 2, the other moved hulls set back to the disc bytes: accepted, with the slot-0xFF node as the proof |
| S42 the last pool slot never scanned | killed | the same input |
| S48 A0 test `& 0xE0` | killed | `B0 41 01 02 03 04 00 FF 2F 00` parses as one porta event and the end |
| S49 unbound test `g != 1` | killed | a played id's record set to (1, 1) with D_00281D50[0x15] = 0: state 3, reason 7 (unbound) |
| F17 the first 16-byte row of each file not scanned | killed | load-map control: the first byte of the first and of the last mapped file |

Every sweep-2 survivor was already killed or proven in round 5; the final
review re-ran them (its `prior` run, all killed) and the close-out re-ran
the whole round-5 set (above).

**Proofs of the close-out's 26 equivalent mutants.** Where a proof cites a
port loader, the loader's verdict is part of every run (`check_loaders`
runs first, and a refusal fails the run).
- **S37 (`LoadedImage.locate` searches the map in reverse).** In a run
  where `load_map_problems` passes, the entries of `K.lmap` are pairwise
  disjoint (sorted, and each ends at or before the next starts), so at most
  one entry holds any address, and both search orders return it (or None).
  Every `LoadedImage` the checker builds is over `K.lmap` or a subset of it
  (the grid control's map without f13); `PatchedImage` reads through one.
  In a run where it fails, the run fails either way.
- **S38 (the map kept in load order, not sorted).** In a run where
  `load_map_problems` passes, `list(lmap) == sorted(lmap)`, so both
  constructors store the same list; the check reads `K.lmap` itself, not
  `LoadedImage.map`. Otherwise the run fails either way.
- **S04 (the bone slot's high-byte mask 0x7F000000).** Every builder
  `_zone_problems` receives is a `MeshBuilder` from `build_zones` or the
  float32 control's `copy.copy` of one, and `MeshBuilder.bone` has one
  writer, `append(0)` (the decomp's `tools/export_level.py`): bone = 0, and
  both masks give slot 0.
- **S06 (the dynamic list length `!=` -> `<`).** They differ only when
  dlen > 0x10 + 0x860 * K. K is the RAM count word, inside the compared
  span, so K equals the file's word 0 in every capture of a passing
  compare. Then the canary (the count planted K ^ 1: for K odd, or when
  dlen >= 0x10 + 0x860 * (K + 1), the mutant stays silent) or the
  "dynamic list - 1" control (dlen - 1 >= 0x10 + 0x860 * K, silent again)
  fails the run.
- **S09 (bank_end's object count read as u16).** They differ only for a RAM
  count >= 0x10000. That word lies in the compared span (the EMSC loader
  refuses a window under 64 bytes), so the file's count equals it, and
  `bank_objects` in `check_level` stops the run for a count >= 0x8000.
- **S10, S11, S12, S13 (`wm_span` reads: qwc u16, bones u16, skeleton
  offset signed, count u16).** In a passing run `em_world_models_parse`
  (`src/game/em_owner_draw_original.c`, `model_ok`) accepts the file, and
  every model header lies inside the span, which equals RAM. The loader
  requires count <= 64 with word 0 equal to it (S13), bones <= 0xFF (S11),
  qwc = blocks * 0x82 with blocks <= 0x10000 and skel = 0x40 + 16 * qwc
  <= 0x8200040 < 2^31 (S12, and S10: the block term then never exceeds the
  skeleton term, so lowering it does not change the maximum). The
  synthetic `wm_span` controls use values under 0x10000.
- **S14 (the EMSP window order compared sorted).** `em_spawn_table_parse`
  (`src/game/em_spawn_table.c`) refuses windows that are not ascending and
  disjoint; for an ascending file `sorted` is the identity.
- **S15 (the EMSP zero word on its low byte).** The same parser refuses
  a non-zero word +0x0C.
- **S16 (the EMSC version on its low byte), S17 (the short-blob guard
  `<= 20`).** `em_script_image_load` (`src/game/em_script.c`) refuses a
  version other than 1 and a window under one 64-byte record; all four
  EMSC files are loaded, so a 20-byte file fails the run either way.
- **S19 (the grid header from RAM, not the disc).** A header outside the
  map makes the original raise (a crash) and the mutant report; otherwise
  the load-map compare forces RAM = disc there in every capture of a
  passing run, and every control that reaches the header read keeps the
  real header (the pointer controls return before it).
- **S20 (`grid_problems`' count u16).** They differ only for a negative
  s16 count; the header equals the disc's (854) in a passing run, and a
  negative count makes `emcl_from_ram` raise, which `emcl_equal` reports.
- **S21 (the EMRK pad `% 2`).** The rank section is 4 + 16 + 12v + 24n +
  24n bytes, a multiple of 4, so both pads are empty.
- **S22 (the EMCL bbox over every grid vertex).** The pool is the grid
  vertices without later bit-identical repeats (a repeat that changes bits
  is refused). Python's `min` / `max` keep the current value on a repeat
  equal to an earlier element (and keep a leading NaN), per axis, so the
  bbox is identical.
- **S25 (the zones memo without the dynamic list).** `build_zones` reads
  the bank, the objects and `file_of`, whose image is `K.image` in all
  three `check_level` calls.
- **S27 (the zone memo without the name).** The name appears only in the
  message text; within one `run_checks` each (bytes, builder, freezes) key
  occurs once, and the canary's freezes differ from the real run's.
- **S28 (the zone memo without `prim`).** `prim` comes from the pinned ELF:
  one value per process.
- **S30 (the container word 0 check dropped).** A file of at least 4
  bytes that equals the disc prefix has word 0 = the disc's total; a
  different prefix is reported by the separate compare.
- **S44, S45, S46 (loader verdict tests).** `em_actor_roster_load` returns
  -1 or `em_actor_roster_parse`'s -1 / 0 (`src/game/em_actor_roster.c`);
  `em_world_models_parse` returns -1 or 0; `em_message_live_install`
  returns `load()`, 0 or 1 (`src/game/em_message_live.c`). On those values
  the two tests agree.
- **S47 (the roster group terminator as u16 0xFFFF).** The same 16 bits.
- **F10 (the doors row end without the array address).** `doors_layout`
  reads only the SHA-256-pinned ELF and overlay: one fixed input, on which
  the survival shows the same output (as N11..N42).

**Proofs that cite files other chains own.** Recheck these proofs when a
chain changes the file:
- tracked port loaders: `src/game/em_script.c` (M34, S16, S17),
  `src/game/em_sfx_bank.c` (M35), `src/game/em_collision.c` (M36),
  `src/game/em_spawn_table.c` (M37, M38, S14, S15),
  `src/game/em_owner_draw_original.c` (S10..S13, S45),
  `src/game/em_actor_roster.c` (S44), `src/game/em_message_live.c` (S46);
- tracked port tools: `tools/export_render_context.py` (W49: its BLOCKS,
  RUNTIME and `verify`, which stops on a differing byte);
  `tools/test_effect_manager_reference.py` (the Oracle that runs 001D8FD0
  for W48 and M49) and `tools/test_coll_move_reference.py` (FloatEE, which
  runs 001A2370 for the derivations);
- the decomp repo: `tools/export_level.py` (`MeshBuilder.bone`: S04, M58)
  and the committed C of 001D8FD0 / 0021B970 (W48, M49).

**Contracts.** Tested: a capture without a GS freeze stops the run
(control); a truncated world-model file is a clean FAIL, not a crash
(control); the overlay pin refuses a changed module (control); each port
loader refuses a broken file (loader canary); a failing check makes the
exit status 1 (every killed mutant). Not tested: missing inputs print
SKIPPED and exit 0; the loader library is rebuilt when a source, header or
the checker is newer; a linked `abort()` stub that runs fails the test;
`C.read_elf` refuses any ELF but the pinned one; the canary tree is removed
after each run; the controls run only when the real checks and the canary
passed (by construction). `em_actor_cells_load`'s -1 on the AREA01
directory is printed, not asserted (Finding 1).

**Mutation sweep, round 5** (2026-09-26). Round 5 claimed that every
single-operation change to the checker's comparators (and to the shared
`compare_load_map` / `LoadedImage` and the level exporter's derivation
functions) either fails the run or is proven equivalent. That claim was
false: the third independent sweep found 20 non-equivalent survivors and
the final review one more (F17). The close-out above kills or proves each
of them and replaces the claim with the exact one. What follows is the
round-5 record, still valid for the mutants it lists.

The scratch harness is in the ignored `build/area01/assets/fix5/`:
`muts.py` (with `muts_wn.py`, the round-4 list, and `muts_m.py`, the second
independent sweep's list), `job.py`, `drive.py`, `drive_list.py`, and the
logs `drive_quick_pass1.log` (the first pass), `drive_quick.log` (the
final code), `drive_quick_r3.log` (the uid-table rule) and
`drive_full.log`, with their `results*.jsonl`. It runs 188 code mutations
(the round-4 W / N list, the second sweep's M list with the patterns the
round-5 code moved re-anchored, and 39 R mutations of the round-5 code) and
45 asset mutations (round 4's 38, plus the review's WM-GROW 1 / 4 / 16,
the moved-set order, the spawn windows swapped, and a zone texture's
width and height one less). Each is exec'd in a subprocess (4 workers, a
300 s timeout; a crash or a hang counts as killed). No source file is
written.

- **Asset mutations: 45 of 45 killed**, each with a clean FAIL. WM-GROW
  now fails on the span pin ("world model span 0x466c4, the bank extends
  0x466c0").
- **Every survivor the review listed is killed**, first by:
  - WM-GROW, M11: the span pin and the grow / shrink / no-model controls;
  - M40, M41: the second-capture-only controls, and the canary's RAM-only
    roster and dynamic-list plants;
  - M43: the canary's grid-copy vertex plant (and the node runs are now
    pinned inside their sections, see Collision);
  - M02: the canary's own GS freeze;
  - M03..M06: the byte-2 / byte-3 pointer controls, and the canary's grid
    copy at 0x1C07940;
  - M07, M09: record word 0x010B and the at-rest rotation bytes;
  - M12..M15: the per-section allowance controls;
  - M16, M17: the +0.0 / -0.0 twins (now refused), index -1;
  - M18: the moved-set order; M21..M24, M26, M29, M30: their unit
    controls; M31: the count-0x10001 control;
  - M47: the 001A2370 overrun control;
  - M01, M45, M46 (killed only by the old canary's file plant, which is
    gone): the unchanged-zone control, the derivation memo now keyed on
    every input (the hull-10 source control), and the last-byte-only
    derivation control.
  Also killed: R19 and R32..R36 (a texture-key field dropped) by the
  texture-key control, R23 (the zone memo without the freezes) by the
  canary's second-zone GS plant, R24 (the derivation memo without the
  node) by the shared-matrix control, and R37..R40 (the uid-table rule).
- **Survivors: 40 of 188, each proven below.** The full run
  (`drive_full.log`) kills none of them. Round 4's 23 keep their proofs
  (listed further down: W05, W07, W33, W35, W38, W42, W46, W64, W48, W49,
  W51, W54, W56, W55, N11, N12, N15, N17, N20, N23, N42, N70, N72). The
  other 17:
  - **M27 (the pitch quotient floored, not truncated).** The two differ
    only for a negative product, where truncation gives <= 0 and floor
    gives <= -1; `pitch_00117918` refuses every pitch outside 1..0x3FFF,
    so both refuse. For a product >= 0 they are equal.
  - **M28 (the 001FB9F0 page split at <= 0x5DC).** The only caller,
    `registry_from_ram`, passes ids from 0x3E8..0x5DB and 0x7D0..0x9C3,
    never 0x5DC; for every other id the two comparisons agree.
  - **M32, M33 (a texture entry's h, or w, not compared).** The texel
    compare then takes 4 * w * h bytes at the entry's offset, with the
    file's w and h, and the length check (the file ends at the blob plus
    the sum of those sizes) keeps that slice whole. With w equal and h
    different (or the reverse) its length differs from the decode's
    4 * (1 << tw) * (1 << th), so it is reported for every capture, and
    every run has at least one (`C.captures` stops the run when there is
    none; `gs_captures` has one entry per capture).
  - **M34, M35 (`== 1` -> `!= 0` for em_script_image_load and
    em_sfx_registry_load) and M36 (`== 0` -> `>= 0` for
    em_collision_load).** In `src/game/em_script.c` and
    `src/game/em_sfx_bank.c` every return of those loaders is 0 or 1; in
    `src/game/em_collision.c` every return of em_collision_load is -1 or
    0. On those values the two tests agree.
  - **M37 (the door read at 0x24E140 instead of 0x24E144) and M38 (the
    spawn read of 0x30 bytes instead of 0x1E0).** `em_spawn_table_read`
    returns non-NULL exactly when one loaded range holds the whole read,
    and `em_spawn_table_parse` loads the file's own (address, size) table
    (`src/game/em_spawn_table.c`). In a run where `emsp_problems` passes,
    the windows are the ELF walk's: the door window 0x24E140 + 0x5C holds
    both 4-byte reads, and the spawn window 0x24AA50 + 11,356 (to
    0x24D5AC) holds both 0x24B1A0 + 0x30 and 0x24B1A0 + 0x1E0 (to
    0x24B380). In a run where it fails, the run fails either way.
  - **M39 (the message table starts without the area words).**
    `message_record_count` reads only the SHA-pinned ELF and overlay, and
    its pointers come from the pinned ELF: one fixed input, on which the
    survival shows the same output (as N42).
  - **M49 (the room-entry flip ^0x40 -> ^0x80).** By W48's proof, whenever
    the rebuild reads the entry, the float at entry +4 is stored unchanged
    at ctx +0xB8 and nothing later writes +0xB8, so flipping bit 6 or bit
    7 of those bytes changes the rebuild either way; when the rebuild does
    not read the entry, neither flip changes it.
  - **M52 (the load-map allowance's first byte no longer excused).** It
    differs only at the directory's byte table + 0. In a passing run every
    capture's directory is at `table` (the pointer check), no hull starts
    before the count and uid words end (the round-5 uid-table rule: the
    first hull starts exactly at 4 + 4 * 38), and `verify_cell_directory`
    compares every directory byte of every capture with an image that is
    the disc outside the hulls (a derivation that changed any byte
    outside its hull is not used, and an orphan replaces only its hull).
    So byte table + 0 equals the disc in every capture of a passing run,
    and the mutant's extra comparison of it never reports; a failing run
    fails either way.
  - **M58 (the bone-slot bound < 1 -> < 2).** x = bone & 0xFFFFFF >= 0:
    for x = 0 both keep 0, for x = 1 the original takes the else branch,
    1, and the mutant keeps x = 1; for x >= 2 both give 1.
  - **R14 (the node read guard n > 0 -> n >= 0).** They differ only at
    n = 0, where `image.read(a, 0)` returns b'' (its loop does not run),
    the else branch's value.
  - **R25 (the derivation memo without the matrix).** The matrix is
    `owner_matrix(cap.ram, node)`, a function of the RAM image (in the key
    by identity, kept alive by the entry) and the node (in the key).
  - **R26 (the derivation memo without the scratchpad).** Every checker
    call site gives each RAM image object one scratchpad: the captures
    each have their own RAM, every control and canary copy builds a new
    RAM image, and the only copies that share a RAM image with another
    capture (the scratchpad-count controls) change the count word, so
    `verify_cell_directory` stops with "directory count/size" before any
    derivation.
  - **R27 (the derivation memo without the ELF).** Every ELF passed comes
    from `C.read_elf()`, which refuses any file but the SHA-256-pinned
    boot ELF: every ELF object has the same bytes.
- **Proof dependence.** The proofs of M34..M38 read tracked port loader
  sources; the close-out's list ("Proofs that cite files other chains
  own") gives every standing proof that does. (The review's M11 proof
  cited `em_owner_draw_original.c`; M11 is killed by a control instead.)

**Mutation sweep, round 4** (2026-09-26). The scratch harness is in the
ignored `build/area01/assets/fix4/`: `muts.py`, `job.py` and `drive.py`
(the independent sweep's harness, with a 240 s timeout per job), plus
`drive_pass1.log`, `drive_pass2.log`, `drive_final.log` and
`drive_full.log` with their `results*.jsonl`. It runs:
- the independent sweep's 55 W mutations and its no-change baseline
  (W18..W20 are retired: the header compare they targeted is now a
  whole-file rebuild);
- 40 N mutations of the round-4 code (the ELF walks, extent pins, grid pin,
  roster rebuild, overlay pin, message counts, the factored sound helpers,
  and the robustness changes);
- 38 asset mutations: the independent sweep's 34, including all 17
  A-TRUNC extent changes, plus 4 more extent changes.
Each mutation is exec'd in a subprocess (4 workers), and no source file is
written. The judge is the default quick run, then the full run for the
quick survivors. A crash, and a hang past the timeout, count as killed.

- **Asset mutations: 38 of 38 killed**, each with a clean FAIL. The
  17 A-TRUNC cases fail on the extent pins: the spawn window set, the door
  array and row, the scripts and overlay windows (disc module and header),
  the static bank end and dynamic list length, the message counts and bank
  sizes, and the roster rebuild. The scripts window with its base moved by
  4 crashed in this round's first pass (`walk_chain` exits). `check_tables`
  now catches that exit, and the case is a clean FAIL.
- **Code mutations: 95 besides the baseline.** The quick run kills 72.
  The full run (`drive_full.log`) kills none of the 23 quick survivors,
  and the doc proves each of them equivalent below. The baseline survives,
  as it must. Every survivor the review listed as
  non-equivalent is killed. The first failure each one causes:
  - W01, W02, W04 and W03 (a loader verdict dropped, or the last zone never
    loaded): the loader canary;
  - W08 and W11: the second bone-parent word and V controls;
  - W13, W15 and W17 (grid count, index pad, form): the grid count-word,
    odd-index-count and form controls. W14 (a vertex index equal to the
    count) crashes on the index control;
  - W18 (the sub byte): the roster is now a whole-file rebuild, and a
    control flips +0x09;
  - W21, W23 and W24: the node field-end controls, the class-byte 0x8B /
    0x0B controls, and the behaviour +0x12 / +0x13 controls;
  - W29 and W30: every message header byte (+0x74 is the first one W29
    misses). W31, W32 and W62: the 0x0001FFFF row (accept), D_00264DD0[0]
    + 8, and the -1 row at row 0;
  - W34: the binding count through `sfx_problems`. W36: the handle-bound
    control. W37: the tempo-25 tick. W41 and W43: the pitch controls;
  - W47: ctx +0xFF;
  - W58 (`LoadedImage.locate` end bound `<=`): the run hangs, because
    `read` stalls at a file seam with take = 0 (timeout). The seam control
    reports it in a run that reaches the controls;
  - B2 (D_0028A598 never checked): the grid pin, which the canary and the
    RAM control kill when it is dropped (N50, N53);
  - the unlisted W25, W26, W27, W28, W40, W44, W45 and W66 are killed too,
    by the version-word, zero-word, span-0, ADPCM-header, channel-default
    and long-script controls.
- **Survivors, each equivalent for every input the original can receive at
  that site (the proof is given for each):**
  - **Walks over the pinned ELF / overlay: N11, N12, N15, N17, N23 and N42.**
    They cover the spawn floor without D_0024D650, the merge on `<=`, the
    door row end without the array, the room array cap 3, the data window
    from the text size, and the record count without the stream lists.
    These functions read only the boot ELF and AREA01.BIN, and the checker
    verifies both against their SHA-256 before use. Their input is
    therefore one fixed byte string. The survival shows that each
    mutant's output on it equals the original's (the output is compared
    whole with the file), so no input exists on which they differ.
  - **N20 (the EMSC extent compare reduced to the base).** On the real
    path `disc` is always given: `_check_tables` passes the disc slice of
    exactly the pinned window length, and `d != disc` already fails any
    other length.
  - **N70 (the 48-row length check in `channel_defaults` dropped).** Its
    callers pass exactly 48 rows: `registry_from_ram` builds them with
    `range(48)`, and the controls pass lists of 48.
  - **N72 (SystemExit not caught in check_tables).** A table that leaves
    its window becomes an uncaught SystemExit instead of a clean FAIL. The
    exit status is non-zero either way, so the verdict is the same.
  - **W05 (emdl_parts without the frame count).** `zone_equal` requires
    the header's frame count to be 1 before it uses the blob offset, and
    64 * fc * bc = 64 * bc when fc = 1.
  - **W07 (the no-texture sentinel 0xFFFF).** The rebuild's input is the
    static bank file, which must equal RAM in every capture. RAM at the
    bank must in turn equal the disc through the load map. On that input
    the rebuild has 0 untextured vertices (level.json `zones`), so the
    sentinel is never compared.
  - **W33 (the bank bodies-end-at-total check dropped).** It reads only the
    extracted disc container (`chunk05.n0/f00_id44.bin`), where the bodies
    end at the total.
  - **W35 (record bytes read unsigned).** Signed and unsigned readings make
    the same decisions:
    - `bank_handle` needs 0 <= g < 6 and 0 <= bi < 0x14: a negative byte
      fails the lower bound, and the same byte read as >= 0x80 fails the
      upper;
    - 00119EA0 needs 0 <= group, index < 0x80, and the same argument holds;
    - `(g, bi) != (1, 0)` is equal in both readings;
    - the bank field is masked with 0xFF.
  - **W38 (`>> 6` as `// 64`).** Python's right shift of an int is floor
    division by 64, for negative values too.
  - **W42 (the inner s32 of the ladder shift dropped).** The only use of
    `lad` on this branch is `product = s32(lad * 44100)`. Since s32(x) is
    congruent to x modulo 2^32, s32(s32(x) * 44100) = s32(x * 44100) for
    every integer x. An exhaustive check over every ladder value
    (0..65,535) and every d (0..255) confirms that the pitch outcome is
    identical.
  - **W46 (the bank field masked with 0x7F).** A played entry has either
    a non-zero `bank_handle` (so 0 <= g < 6) or (g, bi) = (1, 0).
  - **W64 (the registry sub byte fixed at 0).** `C.captures()` yields
    only captures whose D_00810700..01 is (1, 0), and no control changes
    the sub byte.
  - **W48 (the room-entry sensitivity flip limited to +4..+7).** By the
    committed C, 001D8FD0 passes `*(float *)(p + 4)` to 0021B970, which
    stores it unchanged at ctx +0xB8. A float load and store move the bits
    exactly. No later writer touches +0xB8: 0021B920 writes +0xA0..+0xAF,
    and 0021B900 / 0021B8E0 copy from +0xA0..+0xBF to other bytes. So
    whenever the rebuild reads this entry, flipping +4..+7 changes the
    rebuild, and so does flipping +4..+0x17. When it does not read it, both
    flips change nothing.
  - **W49 (the room-entry search stops before the last entry).** The room
    table D_00251C50 (0x251C50..0x253168, 45 entries of 0x78) lies in the
    render-context block 0x250F30..0x253180, and none of its bytes is in
    `export_render_context.RUNTIME`. `check_ctx_room_block` compares that
    block with the pinned ELF in every capture (`ERC.verify` stops the run
    on the first differing byte), so a moved entry is refused. That compare
    is itself pinned since the close-out: the room-table control (entry 44
    + 0x30, a byte the rebuild never reads) kills its removal (S40). In the
    ELF, key 0x0100 is entry 3 of 45, and every capture is area 1 sub 0.
  - **W51 (the derivation loop does not stop at a match).** A later match
    writes the same hull bytes, because `hull == img[s:e]`, and replaces
    only the node recorded in the proof. The verdict and the moved set,
    which is keyed by uid, are unchanged.
  - **W54 (the hull primitive count read as u16).** The count is read from
    the directory the checker verifies. That directory must equal the disc
    bytes, where the 24 counts are 1..36.
  - **W56 (the scratchpad cell count read as u16).** It is compared with
    the directory count, 38. Readings that differ (>= 0x8000) are both
    != 38.
  - **W55 (the load-map row scan runs 16 bytes past a file tail).** All 21
    mapped spans are multiples of 16 (sector spans from the descriptors),
    so a+size-x >= 16 for every row start x, and min(16, ...) is always 16.
  - From round 3, still valid:
    - **R06** (the node vertex count read as u16): the grid is now pinned
      by the checker (D_0028A598 = 0x17C7940, the block inside the load
      map). On the disc, +0x19 is 0 in all 854 nodes;
    - **R14** (the 0x5A prefill dropped): by the committed C, every byte of
      ctx +0xA0..+0xFF is written on every path;
    - **R28** (the 31-operation limit): 475 reachable scripts, none with
      more than 15 events;
    - **K18** (the zone memo keyed by id): every builder passed to
      `zone_equal` stays referenced while the memo is in use. `_ZONES`
      holds the rebuilds, and the float32 control's builder is a local of
      `main`, alive until `main` returns.
    Earlier rounds: N08, N30..N38 and the round-1 list, as recorded in round
    2. Round 3's receipts are in `build/area01/assets/fix3/`.
- **What this standard covers, and what it does not.** The sweep mutates:
  - the checker;
  - the shared `compare_load_map` and `LoadedImage`;
  - the level exporter's derivation functions that the checker calls
    (`derive_cell_image`, `verify_cell_directory`, `owner_matrix`,
    `check_ctx_room_block`).
  It also changes exported bytes and extents. The checker uses these
  other exporter functions as part of its reference:
  - `export_area01_level.bank_objects`, `build_zones` (export_level's
    record walker and strip builder), `dynamic_entries`, `check_kicks`
    and `cell_directory`;
  - export_level's `build_texture_blob`, `gs_material_code` and
    `walk_records`;
  - `export_area01_tables.spawn_fields_placement`, `pool_nodes`,
    `walk_chain` and `SCRIPT_ENTRIES`;
  - `export_area01_sfx.bindings_from_captures`;
  - `build_load_map`.
  A mutation inside one of those changes the reference and the asset
  together. It is detected only if the asset still differs from the
  changed reference. **Those functions are not covered by this standard,
  and no claim is made for them.** A mutation in the rest of an exporter
  changes an exported file, and the checker compares that file with the
  sources above. The side files listed as not checked (at the top) are
  not compared at all.

## Findings (for the lead)

1. **The AREA01 cell directory sets bit 29 in uid 0's word** (0xA000009C; bit 31
   is set too). The original masks with 0x3FFFFFFF (the committed C of
   0019F730 line 117, 001A0B10 line 128 and 001A32C0 line 106); 001A2370 does
   not mask at all (its C adds the whole word). Hull 0 equals the disc in all
   12 captures, so no capture shows either address in use.
   The resulting address table + 0x2000009C is, on the EE memory map, the
   uncached view of the same RAM (this comes from the hardware map, not from a
   capture). `em_actor_cells_init` masks the same way and rejects the image:
   `em_actor_cells_load(area01_cells.bin)` returns -1. A diagnostic copy with
   bit 29 cleared loads (0). The port rejects it in `em_actor_cells_init`
   (src/game/em_actor_collision.c line 103: `hull_valid(..., word &
   0x3FFFFFFF)` fails because 0x2000009C exceeds the image). The loader needs
   a decision before AREA01 wiring. This lane edits nothing.
2. **The level is the static bank, not whole files.** The retired drawbridge
   export (unverified, FINDINGS s45) walked each file whole. Its f00 start
   (+0x1523D0) agrees with the bank, because object 1's units begin at f00
   +0x1523C0. Its per-file walks still differ from the bank objects in every
   other file, by 3 to 86 records, at the file seams, where bank objects run on
   into the next file. Its f11 range [0x10, 0x56000) also takes 36 records from
   the dynamic list (f11 +0x4F800..; those units are drawn with a translation,
   not in world space). Its "12_placed" bake is not needed: those owners draw
   from the model bank at f11 +0x56000.
3. **The area descriptor buffer D_00289BC0 is reused**: sector 36 was found
   there in a01_s1 and a01_s2. Tools must not read the area layout from that
   buffer in arbitrary captures.
4. **Script records are rewritten at run time** (word +0x10 of three records),
   so a script image loaded mid-level is not the disc image.

## What is left

- The dynamic-list entries are not re-derived with the original code
  (kernel 0x00237450 with the entry translation). The dynamic list is
  exported raw, not as an EMDL. (The moved cell hulls are now re-derived with
  001A2370 per owner in every capture; see Cell directory.)
- Shadow receivers: the kernel-0x0023C200 passes over bank objects that the
  captures show. There is no AREA01 counterpart of `shadow_receivers.emsr`.
- Sub 1 (`chunk05.n1`, placement table 0x82C5F0): the export is sub 0 only.
  No sub-1 capture exists.
- The object textures (`object_textures.emot`) of the AREA01 owners, and the
  door, prop and pickup models, are not part of this lane.
- SPU residency of the AREA01 samples.

## Known gaps from the close-out spot check (2026-09-26)

The close-out verifier's fresh single-operation mutants that neither run kills. None is a mistranslation (the module passes each killing input); each is a test blind spot, recorded here instead of opening another sweep round.

- G02 test_area01_assets_reference.registry_from_ram: 'if tone[10]:' -> 'if tone[10] & 0x7F:'. A tone whose sweep byte is 0x80 is no longer refused as 'sweep'. Survives quick and full. Non-equivalent for tone[10] == 0x80. No control covers it.
- G03 registry_from_ram: 'if len(ops) > 32:' -> '> 33'. A script with exactly 33 ops is accepted instead of refused. Survives quick and full. No control has a 33-op script.
- G06 export_area01_level.derive_cell_image: 'if matrix is None: continue' -> 'break'. When a uid has several live owners and an earlier one has an unknown behaviour (owner_matrix None), the later owners are never tried. Survives quick and full. No capture or control has that owner order.
- G09 events_001152D8: status test 'if ram[at] & 0x80:' -> 'if ram[at] >= 0xA0:'. Bytes 0x80..0x9F become data under running status. Demonstrated in build/area01/closeout_ASSETS_1/demo_g09.py on script A0 10 20 30 00 85 01 02 00 FF 2F 00: the original gives Refuse('script'), while the mutant accepts it as two a0 events plus end. Survives quick and full.
- G10 registry_from_ram: 'if slot < 0: continue' -> 'break'. A note below the program's base note drops every later event of the script instead of only that note. Survives quick and full. No control has such a note followed by other events.
