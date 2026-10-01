# AREA13 assets (and the AREA19 arrival): local export and capture checks

Lane "A13ASSETS", level side track (ninth level), 2026-10-01. This lane adds
new files only: four exporters, one checker and this document. It changes no
port source, no tracked port file and no decomp file, and nothing it adds is
wired into the game. Every exported byte comes from the user's own disc
files and ELF and is written into the ignored `assets/area13/` tree. Every
export is checked against the recorded original captures of the ninth level
(NINTH_LEVEL_ROUTE.md). Nothing was run in PCSX2.

Two targets, each the area and sub a capture loaded:

| Target | Area / sub | Captures | Export tree |
|---|---|---|---|
| `area13` | AREA13 (0x0D) sub 0 | the arrival (end of a04b_04, `../Extermination/build/s87/route_a04b/a04b_04_lift/`) and a13_00 .. a13_04 (`build/s87/route_a13/`): 6 | `assets/area13/sub0/` + `assets/area13/` |
| `area19` | AREA19 (0x13) sub 0 | a13_05 (`route_a13/a13_05_shaft/`, the foot of AREA19's entry-9 ladder): 1 | `assets/area13/area19/sub0/` + `assets/area13/area19/` |

Every capture holds RAM, scratchpad and GS freeze. a13_05 is the only
AREA19 capture; it covers the arrival only (the camera at the ladder foot
draws 60 level kicks). Each target's checker asserts that the other
target's captures are exactly the ones its area byte excludes. Which sub
each capture loaded is measured: D_00810701 is 0 in all seven, and for
AREA19 `area19/loaded_sub_proof.json` records 409 differing 16-byte rows
for the sub-0 map (all in the cell directory) against 186,960 for sub 1.

## The two load layouts

001FFCD0 (NEARMISS C `../Extermination/src/func_001FFCD0.c`) state 5 streams
a block's bytes from its resident offset (+0x14) to its size (+8) to the
first cursor D_0028A73C, state 7 relocates the block's file list as
D_0028A490[id] = D_0028A73C + offset; for a nested block, state 9 streams it
to D_0028A740 and state 11 relocates its list from there. The file lists are
resident-relative (AREA22_ASSETS.md finding 8), so every load-map row is
labelled by relocation id; the decomp's `extract/chunkNN[.nS]/fII_idXX.bin`
names read the lists as block offsets and serve only as byte sources (each
row names the file and offset its bytes come from, `level.json`
`load_map`).

**AREA13 (INDEX.IDX sector 17) is flat**, like AREA22: no nested block
(+0x18 = 0), table entry 0 = the sound container (block +0 .. +0xA6800),
one group-A section (+0xA6800, 0xF8800 bytes, the GS texel upload), resident
offset 0x19F000. `export_area22_common.build_load_map_flat` maps it,
imported unchanged. The resident region is 0x6E2800 bytes (7,219,200) at
D_0028A73C = 0x133A640 (the earlier areas' first cursor was 0x1337500;
measured, not explained). Fifteen ids, every relocation word equal to its
formula in every capture:

| Id | Word | Resident range | Size | Contents |
|---|---|---|---:|---|
| 0x43 | D_0028A59C | 0x133A640 .. 0x1472640 | 0x138000 | the model bank (44 models) |
| 0x44 | D_0028A5A0 | 0x1472640 .. 0x17D1E40 | 0x35F800 | the static bank (461 objects) |
| 0x42 | D_0028A598 | 0x17D1E40 .. 0x1830640 | 0x5E800 | the collision grid |
| 0x46 | D_0028A5A8 | 0x1830640 .. 0x1840640 | 0x10000 | the cell directory |
| 0x41 | D_0028A594 | 0x1840640 .. 0x1841E40 | 0x1800 | the area message bank |
| 0x96, 0x61, 0x62, 0x63, 0x7F, 0x7D, 0x7E, 0x80, 0x97, 0x8B | D_0028A490[id] | 0x1841E40 .. 0x1A1CE40 | 0x1DB000 | not decoded here |

**AREA19 (sector 23) is a shape no earlier lane met** (finding 1): its top
descriptor has a group-A section (+0x0C = 0, +0x0E = 1: the GS texel upload,
block +0 .. +0x128800) and a resident offset 0x128800, and it has two nested
blocks (one per sub), whose table entry 0 is the sound container (+0x0C =
1, +0x0E = 0; sub 0: block +0 .. +0xB1800, resident offset 0xB1800).
`export_area01_common.build_load_map` refuses a top block with sections and
the flat builder refuses nested blocks, so
`export_area13_common.build_load_map_both` composes the loader's two rules.
Top region: 0x2EC000 bytes at 0x133A640 (ids 0x43 model bank, 0x41 area
message bank, then 0x96, 0x92, 0x6E, 0x70, 0x71, 0x6F, 0x79, 0x7B, 0x7C,
0x7A). Nested region: 0x367000 bytes at D_0028A740 = 0x1629180 (ids 0x44
static bank, 0x42 grid, 0x46 cell directory, then 0x66, 0x67, 0x97). The
capture's D_0028A740 lies 0x2B40 above D_0028A73C + the top length that
state 5 stores (the AREA06 captures show a gap there too); the builder reads
the cursor from the capture and refuses one below the top region's end; what
moves it is not traced.

In every capture every mapped byte equals RAM except the cell directory's
own bytes (the directory check derives those): 7,219,200 bytes mapped per
AREA13 capture, 6,631,424 for AREA19.

**D_0028A5A4 is stale in both** (finding 2). Neither descriptor relocates id
0x45; the word holds 0x1980000 in every capture and in the capture before
each load (a04b_03 in AREA04, a13_04 in AREA13). That address lies inside
both load maps by coincidence (AREA13 id 0x7F, AREA19 id 0x66). No capture
draws a kernel-0x00237450 kick, so no dynamic list is exported (as AREA22).

## Outputs

Per target, under `sub0/` (AREA13) or `area19/sub0/` (AREA19):

| Output | What | Port loader |
|---|---|---|
| `level/00_id44.emdl` (+ `.gsmat.json`, descriptive) | level geometry, texels, GS state codes: the static bank, id 0x44 | `em_model_load` |
| `level/static_bank.emsc` | `*D_0028A5A0` | `em_script_image_load` |
| `background.embg` (AREA13 only) | the level background (finding 4) | `em_background_gs_parse` (src/gfx/metal/em_background_gs.h) |
| `<target>.emcl` (+ `scene.txt` from export_collision.py) | grid collision with node class, rank and axis sections | `em_collision_load` |
| `<target>_cells.bin` | the cell directory `*0x70003250` (disc rest state) | `em_actor_cells_load` (AREA19: refused, finding 9) |
| `cells.json` | the directory verification | none |
| `roster.emro` | the placements and deferred groups of 001B6910's list | `em_actor_roster_load` |
| `message_data.emmd` | EMMD v1: 54 global and 96 area records, 36 stream rows, both banks | `em_message_live_install` |
| `world_models.emwm` + `.json` | the model bank `*D_0028A59C` | `em_world_models_parse` |
| `sfx/<target>_banks.bin`, `sfx/banks.json` | the sound container (table entry 0) | (the registry's source) |
| `sfx/sfx_registry.emsr` + `.json` | EMSR v2 for scope (area, 0) | `em_sfx_registry_load` |
| `level/level.json` | counts, hashes and the verification summary (no text, no data) | none |

Next to each `sub0/` (`assets/area13/` resp. `assets/area13/area19/`):
`spawn_table.emsp` (the global spawn export, byte-identical to
`assets/spawn/spawn_table.emsp`), `door_destinations.emsp` (D_0024E140[0..0x17)
and the area's row), `scripts.emsc` (the chains' window), `overlay_data.emsc`
(the module's data section) and `tables.json`; `area19/loaded_sub_proof.json`.

The checker does not check the descriptive side files (`scene.txt`, the
`.gsmat.json` files, `world_models.json`, `sfx_registry.json`, `banks.json`,
`level.json`, `loaded_sub_proof.json`); it compares the recorded lists in
`cells.json` (moved hulls and 0019C6F0 calls per capture) and `tables.json`
(chains, run-time words, doors used) with its own results.

## Tools

All tools are pure Python on native arm64 macOS; run them from the port root.

```sh
python3 tools/export_area13_level.py    # both targets: level, textures, background, collision, cells, ctx
python3 tools/export_area13_tables.py   # roster, spawn, doors, scripts, overlay data, messages, world models
python3 tools/export_area13_sfx.py      # the containers and the registries
# each takes --target area13|area19 (repeatable; default both)
python3 tools/test_area13_assets_reference.py                  # checker: AREA13 a04b_04, a13_01, a13_04 + AREA19 a13_05
EM_TEST_FULL=1 python3 tools/test_area13_assets_reference.py   # all 7 captures
EM_AREA13_ASSETS=<dir> python3 tools/test_area13_assets_reference.py   # check another export tree
```

Timings on the M1 (2026-10-01): level export 5.7 s CPU (AREA13) + 1.5 s
(AREA19), tables 0.3 s, sound 1.5 s; checker quick 9.9 s CPU (10.6 s wall),
full 12.2 s CPU (12.9 s wall). The first checker run after a `src/` header
change adds the private loader builds (`build/area13/assets/test/`).

**Reuse.** No earlier exporter or checker is edited; they are imported:
- `export_area22_common.build_load_map_flat` (AREA13's load map);
- `export_area01_common`: `LoadedImage`, `compare_load_map`, `_files`,
  `_extract_files`, `iso_descriptor`, `Capture`, `emsc`, `static_reader`;
- `export_area01_level`: the bank walk, `check_kicks`, `texture_localmem`,
  `build_zones`, `texture_alphas`, `write_zone`, `cell_directory`,
  `derive_cell_image` (the ORIGINAL 001A2370), `check_background`;
- `export_area02_level`: `derive_words` (the ORIGINAL 0019C6F0, with this
  lane's `flag_calls` installed), `grid_extent`, `pool`;
- `export_area06_level`: `derive_scaled_hull` (the ORIGINAL 0x219F50) and
  `SCALED_CODE`;
- `export_area22_level`: `check_ctx_block` (the ORIGINAL 001D8FD0 then
  001D1C50) and `drum_rows`;
- `export_area01_tables`: `export_doors` (its windows; `used` recomputed),
  `export_messages`, `walk_chain`, `match_nodes`, the spawn-field helpers;
  `export_area02_tables`: `export_world_models` (through a contiguous-run
  view, below), `model_owner_nodes`, `overlay_data_window`, `area_bank`,
  `bank_source`;
- `export_area02_sfx.block_bytes`, `export_area04_sfx.refusal_reasons`,
  `export_area01_sfx.bindings_from_captures`;
- the decomp's `tools/export_level.py` (`export_background` and the level
  pipeline) and `tools/export_collision.py` (run with a relaxed header
  scan, finding 3);
- from the AREA01 checker: `zone_problems`, `emsc_header_problems`,
  `emsc_window`, `gs_captures`, `gs_state_problems`, `level_gs_state`,
  `bank_end`, `grid_problems`, `emcl_equal`, `hulls_inside_table`,
  `roster_image`, `roster_equal`, `roster_nodes`, `emsp_problems`,
  `spawn_layout`, `doors_layout`, `messages_problems`,
  `message_bank_size`, `world_models_problems`, `registry_from_ram`,
  `load_map_problems`, `build_loaders`, `quiet_fds`, `emsp_windows`,
  `emsp_file`, `emdl_parts`.

`export_area13_common.configure(target)` sets every `export_area01_common`
attribute either target reads; the level module's `install()` re-installs
its `owner_matrix` and `flag_calls` per call, so one process exports or
checks both targets in turn. `export_area13_common` must be imported first
(it keeps AREA01's builder for the AREA06 import that `derive_scaled_hull`
needs). A process must not mix this lane's work with another area's
exporters.

This lane's own code: `Target`, `descriptor`, `build_load_map_both`,
`relocation_lists`, `relocation_problems`, `loaded_sub_proof` (common);
`owner_matrix`, `flag_calls`, `verify_directory`, `run_collision`,
`export_background`, `previous_word` (level); `c_script_entries`,
`writer_problems`, `run_time_words`, `explicit_model_problems`,
`RunImage`, `spawn_rows`, `doors_used` (tables); `container_block`,
`area_container` (sound); in the checker the label, stale, background,
field-rule, writer, census, door and callback checks and every control.

## How each asset is derived and checked

**Level geometry.** The static bank is the start of id 0x44 in both
targets, so each is ONE zone EMDL (`00_id44`):

| | Objects | Records | Vertices | Triangles | Textures | Level kicks (captures) | Texture comparisons |
|---|---:|---:|---:|---:|---:|---:|---:|
| AREA13 | 461 (zone: 1..460) | 53,238 | 40,367 | 22,518 | 211 | 7,238 (6), 26 distinct REFs | 1,266 |
| AREA19 | 808 (zone: 1..807) | 44,192 | 25,160 | 15,916 | 86 | 60 (1), 6 distinct REFs | 86 |

Every textured level kick REFs units inside a bank object with the class-0
GS state; the grid at render ctx +0x140 names exactly objects 1..N-1. The
texels come from the level-load GS upload replayed from the disc image
(AREA13: the top block's group-A section; AREA19: the top block's, the
nested block having none) and equal the GS-freeze decode of every capture.
The checker rebuilds each zone file from the exported bank byte for byte.

**Background (AREA13).** All six AREA13 captures arm the background (render
ctx +0x174 bits 0 and 1). `export_level.export_background` (the decomp's
`--background` path, imported) runs on every capture, each run comparing
the disc-replayed texels with that capture's GS freeze; all six write the
same asset. a13_05 (AREA19) does not arm it, and there is no AREA19 file.

**Collision.** The grid is D_0028A598 (id 0x42); the checker rebuilds the
EMCL from each capture's RAM grid and requires the file to equal it.

| | Grid nodes | Grid vertices | Indices | EMCL vertices / polygons |
|---|---:|---:|---:|---:|
| AREA13 (0x17D1E40) | 2,567 | 3,447 | 8,471 | 3,203 / 2,567 |
| AREA19 (0x18FE980) | 1,462 | 3,338 | 5,789 | 3,032 / 1,462 |

`--verify-ram` passes for every capture (header, pools, the 12 rank tables,
nodes, and the scratchpad words 0x700031FC..0x7000323C).

**Cell directory.** AREA13: 0x1830640, 86 uids, 64,404 bytes; uid words 0
and 1 carry bits 31 and 30, uid 2 bit 31. AREA19: 0x193B980, 57 uids,
36,456 bytes; uids 0 and 1 carry bits 31 and 29, uid 2 bits 31 and 30, uids
3..5 bit 31. Each file holds the disc bytes; every byte of every capture's
RAM directory is derived:
- **The uid words (0019C6F0).** AREA13: uid 1's bit 30 is clear in every
  capture. [47] 0x8293A0 (byte-identical C `func_overlay_AREA13_00829360.c`)
  calls (0x1F, 1), (0x20, 0) in its state 0 when D_008107F4 bit 0x40 is
  clear, (0x1F, 0), (0x20, 1) when it is set, and in state 1 only the
  second pair, only with the bit set; the bit is clear in every capture
  (D_008107F4 = 1), so `flag_calls` gives (0x1F, 1), (0x20, 0), which the
  ORIGINAL 0019C6F0 runs over each capture: both calls find a record, and
  the pair clears uid 1's bit 30 and changes nothing else.
  AREA19: uid 2's bit 30 is clear. [9] 0x825C70 (byte-identical C
  `func_overlay_AREA19_00825C30.c`) calls (0x21, 1) on entering state 1 and
  (0x21, 0) on entering state 2; it is live in state 1, so (0x21, 1) last.
  Any other state, node count or (AREA13) the bit set is refused (fails
  closed).
- **Hulls re-transformed by a live owner** (the ORIGINAL 001A2370, or the
  ORIGINAL 0x219F50 run whole on the node):

  | Owner (node +0x10) | AREA13 hulls | AREA19 hulls | Matrix |
  |---|---|---|---|
  | 0x219550, pickups | 69, 71, 74..78, 80, 81 | 47..52 | node + 0xD0 |
  | 0x824BB0 (AREA13) / 0x827DD0 (AREA19), the outdoor creatures | 50..53 | 36, 37 | *(node + 0x11C) + 0x90 (finding 10) |
  | 0x219870 | 46 | 23..27 | 0x219F50's scaled scratchpad copy |
  | 0x156620, drums | none moved | none moved | node + 0xD0 |

- **Orphans.** a13_03 and a13_04: the pickup g[5] (item 0x27, taken in
  a13_03) has freed its node; hull 77 equals, byte for byte, its derivation
  from the live pickup in the earlier captures. This lane accepts an orphan
  only that way (no written-words fallback): every orphan here meets it.
- **Drums.** AREA13's twelve drums carry 0x4800 (the extended bit) and are
  all in state 1; AREA19's five (four 0x4000, one 0x4800) too. The export
  refuses a drum out of state 1.
- **Every caller is named.** The census of 001A2370, 0x219F50 and 0019C6F0
  call sites (jal, j or address word) in the ELF and the module equals the
  pinned list, each site inside its function. AREA13: 001A2370 at 0x156EF0
  (drum), 0x219668 (pickup), 0x21A104 (0x219F50), four in 0x824BB0;
  0x219F50 at 0x2198E4 (0x219870); 0019C6F0 six sites, all in 0x8293A0.
  AREA19: 001A2370 also four in 0x827DD0 and six in sub-1 owners (0x826C10
  [36], 0x827430 [35]); 0019C6F0 three in [9] and eight in sub-1 code
  (0x826570 [38], 0x826C10 [36], 0x8279E0 [34], 0x829A70 [46], and
  0x827B20, the op09 callback of chain 0x82E090, which only [34] starts).
  The checker requires no live node of a sub-1 owner, no sub-0 record
  naming one, and the callback's chain to name it and to be started only by
  [34] (its C).

**Background state and the fog block.** The ORIGINAL 001D8FD0 and then
001D1C50, run over each capture with ctx +0xA0..+0xFF overwritten, rebuild
all 96 bytes: AREA13 from room entry 32 (57 bytes change when the entry is
flipped), AREA19 from room entry 40 (60 bytes). Spawn word bit 0x80 is clear
in every capture (AREA13 D_008106C8 0x91902, 0x99901, 0x30099944, 0x99902;
AREA19 0x28002), so 001D1C50 changes none of them.

**Roster.**

| | Placement table | Groups (001B6910's list) | (live, at the record) per capture |
|---|---|---|---|
| AREA13 | 0x82D570, 71 records | 0x829D00 (29), 0x82A230 (2) | (66, 66) arrival, a13_00, a13_01; (64, 63) a13_02; (62, 61) a13_03, a13_04 |
| AREA19 | 0x82E3D0, 60 records | 0x829E00 (43), 0x82A590 (1) | (54, 53) |

Group and placement bytes equal RAM in every capture; the checker rebuilds
the EMRO by the original's walks over the pinned ELF + overlay and over
each capture's RAM. Every live placement node keeps +0x03, +0x0D and +0x54,
except [47] 0x8293A0: its C sets +0x0D = REC[0x2C] (the next record's +4,
0x13; [48] is that record) in state 0 while D_008107F4 bit 0x40 is clear;
the checker requires exactly that value (finding 5).

**Spawn and doors.**
- AREA13: D_0024D650[0x0D] = 0x275550, one sub pointer; 11 records at
  0x24C910. AREA19: D_0024D650[0x13] = 0x2755B0; sub 0's 14 records at
  0x24CF10 (to sub 1's 0x24D1B0). In every capture D_008106C8 = +0x1C of
  record D_00810702 (AREA13 entries 0, 2, 8, 4; AREA19 entry 9).
- The door behaviours are those whose C reaches 001BC150 (the commit that
  reads D_0024E140[area] + 4 * (door id & 0x7F)): 001BC350 and both
  overlays' 0x823580 (through 001BC240), 001BB860, 001BD9F0 and 001BD560;
  the checker recomputes that set from the C (comments stripped) every run.

  | Area (row) | Placement: behaviour, door id, record |
  |---|---|
  | AREA13 (0x24E0A0, 6 records) | [8] 001BC350 id 1 = 02 01 00 00; [10] 001BD560 id 0 area change = 04 07 00 00; [15], [16] 001BD9F0 id 2 = 08 03 00 00; [17] 0x823580 id 3 = 09 04 00 00; [20] 001BC350 id 4 = 05 0A 00 00 |
  | AREA19 (0x24E0F0, 10 records) | [22] 001BB860 id 0 = 06 05 00 00; [25] 001BB860 id 1 area change = 03 01 00 00; [27] 0x823580 id 2 = 01 02 00 00; [31] 001BC350 id 4 = 03 04 00 00; [32] 001BC350 id 5 = 08 07 00 00 |

  These agree with NINTH_LEVEL_ROUTE.md's reading (door [8] entries 2 / 1,
  [16] entry 8, [17] entries 9 / 4, the lift [10] to AREA04 entry 7). The
  checker compares `tables.json` `doors.used` with its own recomputation and
  each record with the row in every capture's RAM.

**Scripts and overlay data.**
- The chains are the ones the committed overlay C starts (every
  func_001BA1A0 argument, re-read every run): 18 for AREA13 (0x82A360 ..
  0x82CFD0, window 0x82A360 .. 0x82D190, 181 records) and 18 for AREA19
  (0x82AD50 .. 0x82F690, window 0x82AD50 .. 0x82F790, 186 records). The
  overlay data window is each module's data section (AREA13 0x829D00 ..
  0x82E280, AREA19 0x829E00 .. 0x82F880).
- Every word equals the disc module in every capture except: AREA13
  0x82A590..0x82A59C (from a13_00 on), in a reached chain record, whose
  writer is not modelled (only its place is checked); AREA13
  0x82CB00..0x82CB08 (a13_04), also in a reached record, written by the
  hatch (below); and two windows outside every record, written by
  byte-identical code (finding 7): AREA13 0x82CA00..0x82CA14, the hatch's
  count-4 constants (`func_overlay_AREA13_00826810.c`: the z > 1000 set,
  the [62] side, in a13_04, whose [62] has counted +0x28 past 4), and
  AREA19 0x82B234 = 5 + y (`func_overlay_AREA19_00824650.c`, the flame [11]
  in state 1; y = -110 with D_00810775 bit 0 clear: -105.0).
- The hatch's point window 0x82CAB0 .. 0x82CB0C: in state 1 sub-state 0,
  with +0x0B bit 2, the same C stores three points by z (z > 1000: 730.2 /
  192.9 / 1276.4 at 0x82CAB0, 725.7 / 174.6 / 1262.6 at 0x82CAC0, 719.8 /
  160.5 / 1252.3 at 0x82CB00; else 1098.5 / 188.9 / 854.0, 1081.5 / 173.2 /
  850.9, 1071.5 / 160.5 / 845.2) before it starts script 0x82CA50. The
  module already holds the first two z > 1000 points, so in a13_04 only
  0x82CB00..0x82CB08 differ. The window must hold the module bytes or one
  whole set (the bytes between the points the module's), with a hatch of
  that side in state 1 with +5 >= 1 or in state 2.
- The checker computes every modelled value from the C's rule.

**Messages.** D_00264DD0[area + 1] names 96 area records for both (AREA13
0x2709E0, AREA19 0x272620); the area banks are each target's id 0x41
(AREA13 0x1840640, 4,782 bytes; AREA19 0x1416640, 4,833 bytes, by each
bank's own header). The file carries 54 global records, 36 stream rows and
the global bank (3,029 bytes); every byte is compared with RAM. No text is
printed or stored outside the ignored asset.

**World models.** AREA13: *D_0028A59C = 0x133A640 (id 0x43), 44 models,
1,276,752 bytes; AREA19: 0x133A640 (its id 0x43), 48 models, 901,072
bytes. `export_area01_tables.export_world_models` reads the bank up to
`image.span()`'s end, which crosses AREA19's cursor gap; `RunImage` gives it
the contiguous run of rows that holds the bank. Every model owner's +0x44 =
001C6120(table, +0x0D), bone count and slots, except the opened hatch
(finding 6): +0x44 = 001C6120(table, 0xD) with +0x0D unchanged. Bound
owners: 64 in every AREA13 capture (the hatch counted by its rule in
a13_04), 41 in a13_05.

**Sound banks.** The container is table entry 0 of the descriptor whose
+0x0C is 1 (001FFCD0 state 4 hands it to the IOP stream loader through
001FF590(0xAB, 0); state 8 the nested one with 0xAC): AREA13's flat top
descriptor (680,832 bytes, 3 banks, byte source `chunk17/f00_id43.bin`),
AREA19's nested block 0 (726,944 bytes, 4 banks, byte source
`chunk23.n0/f00_id44.bin`). None of it is resident.

| | Group 1 | Group 2 | Group 4 | Refused | Registry (1,000 ids) |
|---|---|---|---|---|---|
| AREA13 | global rows 0..2 | area row 0 | slots 0, 1: area rows 1, 2 | 3.0 (no SShd magic), 4.2 (a freed handle, finding 8) | 497 audible, 477 absent, 26 UNSUPPORTED (6 modulation, 2 script, 18 unbound bank), 49 samples |
| AREA19 | global rows 0..2 | area row 0 | slots 0..2: area rows 1..3 | 3.0 (no SShd magic) | 508 audible, 475 absent, 17 UNSUPPORTED (13 script, 4 modulation), 75 samples |

The checker re-derives every byte of each registry from the capture's RAM
and compares each container file with the disc.

## Checker, canary, controls and the mutation sweep

`tools/test_area13_assets_reference.py`, per target:
- **loaders:** every file through its port loader, compiled privately from
  `src/` by the AREA01 checker's `build_loaders`, and `background.embg`
  through `em_background_gs_parse` + `em_background_gs_unsupported` (a shim
  over the header; NULL, i.e. supported). AREA13: 13 of 13 load. AREA19: 11 of its 12 files
  load; its cells file is refused (finding 9) and a copy with bit 29
  cleared in every uid word loads.
- **real checks:** every section above over the run's captures. Quick mode:
  AREA13 a04b_04 (the arrival), a13_01 (outdoors, entry 8) and a13_04 (the
  hatch writer, orphan 77, the opened hatch's model); AREA19 a13_05.
- **canary (51 sections per target):** `run_checks` over [the first
  capture, a planted copy] and a copy of the export tree with file plants.
  RAM plants: a static-bank pad byte and the last object's unit count,
  D_0028A5A0, D_0028A5A4, the last top-list relocation word, TEST_1, ctx
  +0xA4, a directory hull, the grid, the placement table and a placement
  node, the door row, the spawn rows, D_008106C8, a global message record,
  the area bank, the model bank, the pitch ladder, the first chain record,
  the writer window, the creature's matrix, the 0x219870 node's matrix and
  the GS freeze texels. File plants: an extra zone file, `cells.json` (a
  moved hull and the flag calls), the cells file, the container, the
  roster, the overlay data file, the message file, `tables.json` (a
  run-time word set, a chain, a door), the zone EMDL, a dynamic-list file
  and the background file. Each port loader must refuse a broken copy of
  its file.
- **controls (76 AREA13, 65 AREA19):** the load-map labels (the extracted
  names read as ids, the first row relabelled, the first or a middle row
  dropped, a duplicated row, a row labelled by an id its block does not
  list, the static bank rows labelled by the id before it), D_0028A5A4 and
  the previous capture's word, a relocation word, the checker's map one row
  short, the static bank file past its objects' end; the directory baseline
  (accepted), the first and last moved hull, an unmoved hull, uid 0 bit 30,
  the flag owner in states 0 and 3, and in state 3 with the disc uid words,
  D_008107F4 bit 0x40 (AREA13), [9] in state 2 (AREA19), the 0x219870
  node's position and the dominant lane of its matrix third row, the
  0x219870 node and the creature each read as a node + 0xD0 owner, the
  creature matrix, owner_matrix unit cases, the orphan 77 alone, with the
  arrival (accepted) and changed; the call-site census (an extra site, a
  site credited to another owner, a live sub-1 owner, the callback's chain
  credited to a sub-0 or another sub-1 starter or not naming it); the
  writer windows (both hatch sets with and without an opened hatch of that
  side, a changed last word, D_00810775 bit 0, the flame out of state 1,
  the word after the window, a gap word); the entry byte; `doors.used` (as
  exported accepted, an area-change bit, the RAM record); a model owner's
  +0x0D and +0x44; the opened hatch with +0x0D = 0xD (accepted) and with
  +0x44 changed; [47]'s +0x0D at its record's value and with the bit set;
  the background TEX0 and an unarmed capture; an area message record; a
  pinned binding and the refused set; D_0028A740 inside the top region and
  a relocation list not starting at offset 0 (both refused by the
  builder). The review round added a pinned case for each named survivor
  (`survivor_controls`, each filtered for the one check it pins): the last
  row labelled by a chunk outside the descriptor, a top list relocating id
  0x45, the static bank's last byte changed in RAM, a zone file named after
  another id (and the exported names accepted), the background file missing
  (AREA13), the cells file 0x10 disc bytes past its directory, cells.json's
  moved uids without one uid, a sub-0 placement naming a sub-1 owner
  (AREA19), a live placement node freed, `overlay_data.emsc` with its base
  word + 0x10, a bound owner's bone count and first bone slot, the opened
  hatch's bone count and first bone slot, DOOR_BEHAVIOURS without 001BD560
  (and as pinned, accepted), SCRIPT_ENTRIES and tables.json both without
  the last chain; and for the hatch point window: as captured (accepted),
  the z <= 1000 points with only the z > 1000 hatch opened, the z > 1000
  points with that hatch back in state 1 sub-state 0, the third point
  changed, a byte between the points changed.

**Mutation sweep (one bounded sweep, this lane).**
- 48 single-operation mutants over the checker's comparators and the lane's
  exporter functions it calls (`build_load_map_both`, `_resident_rows`,
  `relocation_problems`, `flag_calls`, `owner_matrix`, `verify_directory`,
  `writer_problems`, `run_time_words`, `explicit_model_problems`,
  `c_script_entries`). Each ran in quick mode in a private copy of the
  port root (real copies of `tools/`, symlinks for the rest), 4 workers
  (`build/area13/sweep/sweep.py`, ignored; about 2 min 15 s wall).
- First pass: 29 of the 48 survived, 16 of them the exporter mutants: the
  harness symlinked the unmutated modules, and a module that resolves its
  own path put the real `tools/` first on `sys.path`, so the exporter
  mutants were never loaded (fixed: whole-directory copies). A second
  pass found that the directory controls ran on a13_04 alone, whose orphan
  77 cannot be proven without another capture, so every directory control
  was rejected for that reason, not its own (fixed: the controls run on
  the arrival and a baseline control requires it to pass alone). Controls
  were added for each remaining survivor (the label bounds, gap and id-set
  checks, the previous-capture word, the map comparison, the bank extent,
  the site-in-function bound, the callback's chain checks, the bound count,
  the refused set, the flag owner's states, the writer window's end, the
  builder's cursor and list checks).
- Final state: 46 of 48 killed. Two survive, both equivalent: M09 (the
  background check's "every capture armed" guard removed: the per-capture
  `export_background` refuses an unarmed capture itself, so the same input
  is still reported) and M29 (the recorded bit-29 refusal of the cells
  loader forced true: the port loader refuses every such file, so no input
  tells them apart). The claim covers exactly these mutants; the reused
  comparators keep their own sweep records and known gaps.

**Review survivors (fix round, no new sweep).** The reviewer's independent
sweep (38 single-operation mutants R01..R38 over the checker and the
lane's exporter functions, quick mode) killed 17 and left 21. Seven are
equivalent or redundant, as the review argued:
- R08 (the owners-minus-modelled set compared with SUB1): it compares
  only the checker's own pinned constants (SITES, the exporter's modelled
  owners, CALLBACKS, SUB1); no export file or capture reaches it, so no
  input tells the mutant apart.
- R18 (the container total): the byte compare with the disc container runs
  first and reports any length change.
- R24 (the directory pointer): `verify_directory` reads the directory at
  each capture's own pointer, so a changed pointer makes it compare the
  wrong bytes (probed: the pointer + 0x10 in the arrival reports hulls 0,
  3, 4, ... as differing).
- R25 (a dynamic kick): `check_kicks` itself refuses a dynamic kick.
- R30 (the drum's re-transform branch): no capture holds a moved drum
  (known gap 10).
- R31 (the directory compared over half its size): the hulls cover every
  directory byte after the 348 / 232-byte uid table, and the hull proof
  path compares every hull byte.
- R37 (`room > count`): room == count reads the record past the table,
  whose +0x1C differs.

The other 14 were not equivalent. Each now has a pinned control (above),
and the 14 mutants were re-run against the fixed checker with the
reviewer's harness (`build/area13/survivors/rsweep.py`, ignored; the old
strings of R04 / R16 / R17 moved into `zone_name_problems`,
`door_behaviour_problems` and `script_entry_problems`, with the same
mutation). All 14 are killed:

| Mutant | Disabled check | Killing control |
|---|---|---|
| R01 | rows outside the descriptor's blocks | the last row labelled `chunk99/...` |
| R02 | the descriptor relocates id 0x45 | a top list with (0x45, 0) |
| R03 | static bank = RAM (first half only) | the bank's last byte changed in RAM |
| R04 | the zone file name | a zone file named after another id |
| R05 | no background.embg although armed | the background file missing |
| R06 | cells file size = directory size | the cells file + 0x10 disc bytes |
| R07 | moved_uids = union of the rows | moved_uids without its first uid |
| R10 | the sub-0 roster names a sub-1 owner | an AREA19 placement +0x24 = 0x826C10 |
| R11 | (live, at rest) = ROSTER_LIVE | a live placement node freed |
| R13 | the EMSC header base / size | `overlay_data.emsc` base word + 0x10 |
| R15 | a bound owner's bone count / slots | its +0x0C changed |
| R16 | DOOR_BEHAVIOURS = the C's door callers | DOOR_BEHAVIOURS without 001BD560 |
| R17 | SCRIPT_ENTRIES = the C's chains | both lists without 0x82CFD0 |
| R29 | the opened hatch's bone count / slots | its +0x0C changed |

Three mutants of the new hatch point rule (a set accepted on its first
point only, the state condition widened to any state 1, the side test
dropped) are killed by the point controls. With these, every non-equivalent
mutant of both sweeps is killed; the survivors left are M09, M29 and
the seven above, each argued equivalent here.

## Findings (for the lead)

1. **AREA19's descriptor is a new layout:** a top block with a group-A
   section (the GS texel upload) and a resident offset, plus nested blocks
   whose table entry 0 is the sound container. Neither the AREA01 builder
   nor the flat AREA22 builder maps it; `build_load_map_both` composes
   001FFCD0's state 5/7 and 9/11 rules. D_0028A740 lies 0x2B40 past the
   top region's end (not traced).
2. **D_0028A5A4 = 0x1980000 survives two area loads** (AREA04 -> AREA13 ->
   AREA19): neither descriptor relocates id 0x45, and the stale address
   lies inside both maps. A port that reads the dynamic list through it
   would draw nothing the original draws; no capture draws one.
3. **AREA13's grid keeps its rank tables at a halfword-aligned offset**
   (header +0x18 = 0x270FE). The decomp's `export_collision.py`
   `find_grid_header` requires every header offset to be 4-aligned and finds
   no grid. The original names that address itself (the scratchpad words
   0x70003210 + 4k = grid + 0x270FE + 2 * N * k, checked by `--verify-ram`
   in every capture). This lane runs the tool with a relaxed scan (tried
   only when the strict one fails, only at offset 0, every other signature
   test kept) and does not edit it; a fix in the shared tool is the
   decomp's call.
4. **AREA13 arms the level background**, the first area since AREA11:
   `background.embg` 256 x 16, TEX0 0x2006D50121323240, CLAMP_1 5, TEX1_1
   0x60, TEST_1 0x30003, RGBAQ 0x80808080, PRIM 0x1C; three GS transfers
   from the group-A section; the port's `em_background_gs_unsupported`
   accepts it.
5. **[47] 0x8293A0 takes its pose from the next record.** With D_008107F4
   bit 0x40 clear its state 0 loads REC + 0x2C.. (record [48], a class-0x0B
   record 001B6990 skips) into +0x0D, +0x0E, +0xB0.., +0xC0.., and makes
   the 0019C6F0 pair that clears uid 1's bit 30; with the bit set it would
   make the other pair, and its state 1 restores record [47]'s pose after
   +0x28 counts down from 0x6E0.
6. **The opened hatch breaks the model-binding rule** of the earlier lanes:
   when its script ends it sets +0x44 = 001C6120(D_0028A59C, 0xD) without
   changing +0x0D. The checker binds it by its C (`EXPLICIT_MODELS`).
7. **Run-time overlay-data words outside the chain records** are written by
   byte-identical code: the hatch's count-4 constants and the flame's
   5 + y. Their values are checked, not only their place. The hatch's
   three points (0x82CAB0 / 0x82CAC0 / 0x82CB00) lie inside reached
   records and are value-checked by the same C rule; 0x82A590..0x82A59C
   (inside a reached record) are the only run-time words checked by place
   alone.
8. **AREA13 group 4 slot 2 is a freed handle** (use word 0, header 0) in
   every AREA13 capture: 18 sound ids resolve to "unbound bank".
9. **AREA19's cells file is refused by the port loader** (uids 0 and 1
   carry bit 29, as in AREA01 / AREA00 / AREA02 / AREA04: the loader's
   0x3FFFFFFF mask keeps it); AREA13's loads.
10. **The outdoor creatures 0x824BB0 / 0x827DD0** (twins of AREA01's
    0x826D40) re-transform their hulls with *(node + 0x11C) + 0x90; the
    ORIGINAL 001A2370 reproduces all six moved hulls that way, and node +
    0xD0 does not (a control).
11. **`export_area01_tables.export_doors`' `used` list** names 001BC350,
    001BB860 and 0x823580 only; in these areas 001BD9F0 (door [14]'s
    buttons) and 001BD560 (the lift [10]) also commit through 001BC150, and
    both overlays' 0x823580 are doors. The lane recomputes `used`; the
    shared function is not edited.

The census delta (`../Extermination/build/s87/census/a13_delta.json`, 44
new functions) names none of the load-path functions these exports run
(001FFCD0, 001B6910, 001B6990, 001C6120, 001A2370, 0019C6F0, 0x219F50,
001D8FD0, 001D1C50). Of its AREA13 overlay entries, 0x824BB0 (the
creatures), 0x826850 (the hatches) and 0x8293A0 ([47]) are modelled here as
a hull owner, a data writer / model rule and the 0019C6F0 caller. AREA19's
functions are not attributed there, but they are recorded:
`overlay_hits_other_overlay` holds 11 sampled PCs of overlay id 16 (AREA19),
all in a13_05: four inside the creature 0x827DD0 (from its entry) and seven
inside the flame 0x824690 (0x8246D0 .. 0x824A30, within its 0x3F4 bytes),
both modelled here (a hull owner and a data writer).

## Binding

Nothing is wired. Every file is exported, checked and loadable (AREA19's
cells file once the loader keeps bit 29 out of the offset). When AREA13 is
bound:
- The scene selects `sub0/` for the level, background, collision, cells,
  roster, world models, message file and sound files; the spawn table,
  doors, scripts and overlay data are the files next to it.
- The level is one zone, `sub0/level/00_id44.emdl`; there is no dynamic
  list, and D_0028A5A4 must keep the previous area's value (finding 2).
- The background is drawn before the level from `sub0/background.embg`
  (the AREA11 path, docs/BACKGROUND.md).
- The load must reproduce the flat layout (AREA13) or the top + nested
  layout (AREA19), with D_0028A490[id] for every listed id.
- The live directory starts from the cells file. The pickups, drums and
  creatures call a translated 001A2370 with their matrix (the creatures'
  *(node + 0x11C) + 0x90); 0x219870 needs a translated 0x219F50, and [47] /
  [9] a translated 0019C6F0 (the port has neither yet).
- [47] must take record [48]'s pose and model (finding 5); the opened hatch
  must set its model without +0x0D (finding 6); the hatch and the flame
  write their overlay-data words (finding 7).
- The ctx block is evaluated at run time by 001D8FD0 at a room load and by
  001D1C50 every frame, from `assets/render_context.emrc`.
- AREA19 from `area19/`: only the arrival at entry 9 is covered (known gap 1).

## Known gaps

1. **AREA19 past its arrival.** One capture, at the foot of the entry-9
   ladder: 60 level kicks, 6 distinct REFs. Sub 1 (`chunk23.n1`) is loaded
   by no capture and not exported. An AREA19 capture group (the A19CAP
   lane) would let this export be checked further.
2. **Undecoded ids:** AREA13 0x96, 0x61, 0x62, 0x63, 0x7F, 0x7D, 0x7E, 0x80,
   0x97, 0x8B; AREA19 0x96, 0x92, 0x6E, 0x70, 0x71, 0x6F, 0x79, 0x7B, 0x7C,
   0x7A, 0x66, 0x67, 0x97. Their bytes are only checked equal to RAM through
   the load map.
3. **Not exported:** shadow receivers, owner object textures, door, prop and
   pickup models (as for the earlier areas).
4. **SPU residency** of the samples: there is no SPU capture.
5. **UNSUPPORTED sound ids** (AREA13 26, AREA19 17) are not resolved.
6. **The flag-call models fail closed** outside the captured states:
   D_008107F4 bit 0x40 set (AREA13), [9] outside states 1 / 2 (AREA19), a
   respawn of either owner after its calls.
7. **The 001D1C50 branch with spawn bit 0x80 set** does not occur in these
   captures.
8. **The collision export depends on the relaxed header scan** (finding 3).
9. **The cursor gap** (finding 1) is measured, not explained; the second
   cursor is read from the capture.
10. **A knocked drum and a thrown pickup** appear in no capture.
11. **Descriptive side files** are not checked (see Outputs).
