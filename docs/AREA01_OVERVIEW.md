# AREA01 static overview (the next level)

Date: 2026-09-25 (session s87, side lane "a01-overview", revised by lane "overview-doc"). Static analysis of the pinned boot ELF, the AREA01 overlay (`OVERLAY/AREA01.BIN`), the extracted `chunk05` blocks, the decomp's committed sources and splat trees, and the captured RAM of the AREA01 arrival (`../Extermination/build/s87/route/15_level_exit/eeMemory.bin`, FIRST_LEVEL_EXIT.md beat 15, frame f801: area bytes 01 00 04 01, overlay id 2 resident). Execution facts are quoted from recorded census outputs (the beat-15 exit census and the AREA01 route census of SECOND_LEVEL_ROUTE.md); no emulator was run for this document. It lists addresses, sizes, counts, statuses and table values only: no original code, no disassembly and no on-screen text.

**Scope and isolation.** Preparation for the second level while the first level (AREA11) is being finished. Nothing here changes the live port. Per the project rule, a label quoted from FINDINGS.md, a decomp comment or a name is a claim, not evidence, and is marked as such.

## 0. How this document is made

**One tree state per repository.** Every decomp status here is read from decomp commit **bdd40fb** (2026-09-25 19:43, the AREA01 overlay matching commit) through git, never from the working tree; every port fact (first-level census rows, the script host's admitted ops, the address grep) is read from port commit **b7868e1** (2026-09-25 19:52). The report records both commit ids; the tool also records whether the working tree's `src/` equals the decomp commit (it did for this run).

**Reproduce** (decomp repo root, macOS arm64; `--compile-check` runs the committed overlay C through mwcc in the `exterm-toolchain` container, the rest is host Python):

```
.venv/bin/python tools/area_overview.py --area 1 \
    --ram build/s87/route/15_level_exit/eeMemory.bin --compile-check \
    --doc ../extermination-port/docs/AREA01_OVERVIEW.md
.venv/bin/python tools/area_overview.py --area 1 \
    --ram build/s87/route/15_level_exit/eeMemory.bin --compile-check \
    --check-doc ../extermination-port/docs/AREA01_OVERVIEW.md   # exit 1 when a block is stale
```

It writes `build/s87/area01/overview.json` and `build/s87/area01/tables.md` (both ignored). **Every table and every block between `<!-- area_overview:NAME -->` markers in this document is printed by the tool**; `--doc` rewrites those blocks in place and `--check-doc` compares them (without `--compile-check` the Evidence column of section 8 falls back to the build's own compiled objects, so that block then differs). Everything outside the markers is manual prose. Where prose states a number, its source is named: a generated block of this document, `overview.json`, or a recorded census output. The lane map of section 11 (`LANES`), the claim labels of sections 10 and 11 (`CLAIMS`, the lanes' `claim`) and the dated script-host admission snapshot of section 9 (`ADMISSION_SNAPSHOT`) are constant tables inside `tools/area_overview.py`.

Inputs besides the two commits: the census outputs `build/s87/census/route_functions.json` (the first-level census), `exit_delta.json` (beat 15), `a01_delta.json` (the AREA01 route census; the report records its sha256 because the route lane may still refresh it) and `provenance.json` (the boot link-route audit of 2026-09-23 13:08; 274 boot sources changed after it, none changed its marker class, which the tool checks).

**Tool check on a known area.** `--area 11 --ram build/startup-reference/playable_ee.bin` reproduces the known AREA11 facts: placement table 0x82A3C0 (21 records), 49 live pool nodes (the census's 49 at first control), 162 area message records (MESSAGE_SERVICE.md), 34 splat pieces grouped into 26 functions, text byte-identical to the disc file. The run also checked the `--out` fix (an output path outside the repo).

## 1. Summary

<!-- area_overview:summary begin -->
| Item | Value |
|---|---|
| Overlay | `AREA01.BIN`, MWo3 id 2, text 0x823540..0x828a00, data ..0x82cd00, BSS 0x82cd00..0x977980 |
| Sub-states | 2 (spawn descriptor entries); placement tables sub 0: 0x82bd50, sub 1: 0x82c5f0 |
| Title (RAM) | D_00289B40[1] = 0x00020003; sub 0: D_002671C0[3] -> 0x2739f0, sub 1: D_002671C0[4] -> 0x2739f0 |
| Level data | INDEX sector 5: `chunk05` (6 files); `chunk05.n0` (15 files); `chunk05.n1` (15 files) |
| Overlay functions | 41 (from 62 splat pieces), 21,696 bytes; at decomp bdd40fb: C 33, AU 4, NM 3, AI 1 |
| Sub 0 roster | 54 placement records; deferred groups 0x828a00 (40), 0x829220 (6) |
| Sub 1 roster | 32 placement records; deferred groups 0x829440 (22) |
| Nest groups | link 0: 0x829360 (4) |
| Live pool (RAM) | 91 nodes, 26 behaviours |
| Scripts | 14 chains, started by 12 overlay functions |
| Doors | destination table 0x24dfa0, 8 records |
| BGM at the capture | lane 0 cue 13 (state 2), D_008106C8 = 0x8d00 |
| Messages | D_00264DD0[2] = 0x26f060..0x26f620, 184 records |
| Static census delta | 285 boot functions (154,976 bytes) not in the first-level census; 184 (90,752 bytes) from sub 0 / nest owners |
<!-- area_overview:summary end -->

Manual notes to the summary:

- **Arrival from AREA11:** sub 0 spawn entry 4 (Roger's departure calls 001B0C60(1, 0, 4)); the fan's direct exit goes to sub 1 entry 4, same coordinates (FIRST_LEVEL_EXIT.md section 4).
- **Title:** both subs point D_002671C0 at the same string (the Title row above); the text itself is not reproduced here.
- **Decomp status at bdd40fb:** the 33 C functions are the 32 matched in bdd40fb plus the init 0x823A50 (matched earlier). With `--compile-check` each committed C file was compiled with its own `// COMPILER:` / `// CFLAGS:` and, relocated as the overlay link does, equals the original bytes (the Evidence column of section 8). The four AU functions are 0x823580, 0x823CD0, 0x824340 (jr-table dispatchers, blocked by the overlay link address; decomp PROGRESS.md 2026-09-25) and 0x826D40 (not started); the AI function is the leading nop sled; the three NM functions are in the decomp's NEARMISS.md.

## 2. Overlay, naming and residency

- The MWo3 header (0x40 bytes) loads with the file at 0x823500, so `.text` starts at 0x823540. splat names overlay code by its link address, which is **0x40 low**: engine address = splat label + 0x40. All addresses in this document are engine (runtime) addresses; the splat pieces are listed in section 8.
- Because every intra-overlay call target lands 0x40 into the called function, splat opens a fake function there. The functions are grouped by the decomp's `tools/overlay/overlay_match.py` (`true_functions`: a piece starts a function when it is the first piece, opens a stack frame, or follows a return/jump, its delay slot and zero padding), the same grouping the overlay link uses since bdd40fb: 62 pieces, 41 functions.
- The captured RAM holds the overlay: header id 2, text byte-identical to the disc file, no data word changed at f801 (`overview.json` `ram`), so the tables below are identical in the file and in the live game at the arrival.
- Boot → overlay entry: the only boot call to an AREA01 function entry is 001E7780's area dispatch for keys 0x100 and 0x101 (`area_dispatch_off0550_state0100` = 0x823A50, the overlay init; the keys are in the decomp's `func_001E7780.c`, the calls in `overview.json` `boot_calls_into_arena`). The one other direct boot call into the arena that lands in AREA01's range (00195130 → 0x823FE0) lands inside 0x823CD0, not on a function entry, so it does not target AREA01 code (presumably another area's overlay). The init stores D_00275C2C = 1, D_00275C28 = 0x20, D_00275C20 = 0x82CD00, D_00275C24 = 0, D_00275C18 = 0 and D_00275C1C = 0x836D80 (the committed C, byte-identical; both pointers are into the overlay BSS). 001E7D20 (placement [35]) uses the D_00275C20 records per its decomp comment (claim).
- Everything else is reached through data: placement and group behaviours (actor +0x10), script op09 callbacks and overlay-internal calls (section 8).

## 3. Level data the area loads

001FFCD0 reads INDEX.IDX sector `D_00810700 + 4` (= 5 for area 1) and, for the second block, the nested descriptor at `D_00289BC0 + 0x100 + sub * 0x70`. The tool lists the extracted blocks of sector 5, reads the loaded descriptors from the capture and names the block each describes by its disc offset (`extract/manifest.txt`), and samples every file for residency: 64-byte windows at eight evenly spaced offsets (uniform windows skipped) searched in the whole RAM image.

<!-- area_overview:level_data begin -->
| Block | Files (index: id, bytes; resident windows found/sampled) |
|---|---|
| `chunk05` | f00: 41, 10,240 (6/6); f01: 96, 1,101,824 (7/7); f02: 97, 26,624 (7/7); f03: 98, 86,016 (7/7); f04: 71, 495,616 (7/7); f05: 73, 4,096 (4/4) |
| `chunk05.n0` | f00: 44, 2,744,320 (5/8); f01: 45, 26,624 (8/8); f02: 43, 288,768 (8/8); f03: 42, 147,456 (8/8); f04: 46, 34,816 (8/8); f05: 4a, 86,016 (8/8); f06: 4d, 2,048 (8/8); f07: 4c, 8,192 (8/8); f08: 99, 2,048 (8/8); f09: 52, 112,640 (8/8); f10: 47, 331,776 (8/8); f11: 4b, 536,576 (8/8); f12: 4e, 227,328 (7/7); f13: 72, 139,264 (7/8); f14: 88, 1,650,688 (7/7) |
| `chunk05.n1` | f00: 44, 2,588,672 (0/7); f01: 45, 14,336 (0/8); f02: 43, 288,768 (0/8); f03: 42, 247,808 (0/8); f04: 46, 30,720 (0/8); f05: 7a, 4,096 (0/8); f06: 81, 4,096 (0/8); f07: 7c, 129,024 (0/8); f08: 84, 399,360 (0/8); f09: 72, 139,264 (0/8); f10: 74, 145,408 (0/8); f11: 82, 165,888 (0/8); f12: 83, 194,560 (0/8); f13: 79, 57,344 (0/8); f14: 7b, 1,925,120 (0/7) |

| Descriptor | At | Disc offset | Size | Nested count | File count | Block |
|---|---|---|---|---:|---:|---|
| top (D_00289BC0) | 0x289bc0 | 0x199f800 | 0x1a5000 | 2 | 6 | `chunk05` |
| nested 0 (+0x100 + 0x70 * 0) | 0x289cc0 | 0x1b44800 | 0x60b800 | 0 | 15 | `chunk05.n0` |
| nested 1 (+0x100 + 0x70 * 1) | 0x289d30 | 0x2150000 | 0x60a800 | 0 | 15 | `chunk05.n1` |

D_00275C70 = 0x289cc0 (nested descriptor 0); load cursors D_0028A73C..48 = 0x1335f40, 0x14dc940, 0x199a940, 0x199a940.
<!-- area_overview:level_data end -->

Reading of the block above: the top descriptor is `chunk05` with 2 nested blocks, D_00275C70 points at nested descriptor 0 (`chunk05.n0`), and nested descriptor 1 is `chunk05.n1`. Every sampled `chunk05` window and nearly every `chunk05.n0` window is found in RAM; no `chunk05.n1` window is. So sub 0 loads `chunk05` + `chunk05.n0` and sub 1 loads `chunk05` + `chunk05.n1`. Two n0 files are only partly found (f00 id 0x44 5 of 8 windows, f13 id 0x72 7 of 8): those parts are either not resident at f801 or changed in RAM after the load; the sample does not say which.

File roles are **not** derived here. FINDINGS s45 (the old drawbridge export, a claim) calls n0 id 0x43 the main world, id 0x44 the collision head plus render tail, id 0x4b world zones plus the per-area model table at concat 0x3F2000 (22 entries), and places the collision grid at concat 0x438800; FINDINGS s69 pairs the per-area text bank with `chunk05` id 0x41 (its 184 lines = the 184 message records of section 4, a count cross-check only). AREA11 is sector 15 = `chunk15` (19 files, no nested blocks). The port's area read (`em_game_legacy_area_load`) knows only 0x0B/0 and faults for area 1 (FIRST_LEVEL_AUDIT INV-02).

## 4. Registries, sub-states and title

<!-- area_overview:registries begin -->
| Registry | Area entry | Sub 0 | Sub 1 |
|---|---|---|---|
| D_0024D7C0 placements (0x28-byte records, 0xFF end) | 0x2758d0 | 0x82bd50 (54) | 0x82c5f0 (32) |
| D_0024D820 deferred groups (0x2C-byte records, -1 end) | 0x829848 | list 0x829838: 0x828a00 (40), 0x829220 (6) | list 0x2758c0: 0x829440 (22) |
| D_0024D820 nest slots (D_0024A850[1] = 2) | 0x829848 | [2] link 0: 0x829360 (4) | |
| D_0024D650 spawn entries (0x30-byte records) | 0x275500 | 0x24b1a0 (10) | 0x24b380 (10) |
| D_0024E140 door destinations (4 bytes) | 0x24dfa0 | 8 records, shared (ends at 0x24dfc0) | same |
| D_00264DD0[2] message line records (8 bytes) | 0x26f060 | 184 records (184 nonzero), shared, end 0x26f620 | same |
| D_00289B40 title base/count (RAM) | 0x00020003 | D_002671C0[3] | D_002671C0[4] |
| D_0026EC60 area music rows | 2 rows | trigger 88 → cue 30; trigger 165 → cue 64 |  |
<!-- area_overview:registries end -->

The descriptors 0x2758D0, 0x275500 and 0x2758C0 are ELF-static data (next list, correction 3). The 0x82A900 "group" of section 7.2 is the op14 table of script 0x82AD90.

**Corrections to FINDINGS.md (for the lead; this lane does not edit it):**

1. FINDINGS s45 ("serving the area's 7 sub-states", and the soundmap pairing (1, 7) → chunk15) is wrong: AREA01 has exactly 2 sub-states (spawn, placement and deferred descriptors, the loader's nested count and D_00289B40[1] all say 2). chunk15 is AREA11's sector.
2. FINDINGS s45 flagged "table A = sub 0" as a presumption. Confirmed: the descriptor puts 0x82BD50 at sub 0, the arrival's live pool holds its records (section 7), and the route capture measured it (SECOND_LEVEL_ROUTE.md section 2).
3. FINDINGS s22/s45 say the AREA01 spawn descriptor (0x275500) and the other 0x2755xx..0x2759xx slots are BSS filled by the overlay at load. They are static ELF data: the RAM equals the ELF over 0x275400..0x275B00 except the three words 0x275688..0x275697 (a one-off comparison of the capture with the ELF, re-checked by the a01-overview review; not a tool block).
4. FINDINGS' music table labels cue 20 "AREA01 BGM". At the arrival lane 0 plays cue **13** (from the spawn record); cue 20 is requested only by the sub-1 owner 0x826BA0 when its script 0x82BAD0 ends (the only overlay function with a direct call to 001FB0B0; `overview.json` `calls_boot`, and its committed C).

## 5. Spawn entries, doors and exits

Spawn records: word +0x1C is copied to D_008106C8 by 001B0250; its bits 8..14 are the BGM cue 001FAE70 starts (the capture has D_008106C8 = 0x8D00 and lane-0 cue 13, state 2).

<!-- area_overview:spawn begin -->
| Entry | Position | Yaw | +0x10 | +0x14 | +0x1C sub 0 | +0x1C sub 1 | +0x20 |
|---:|---|---:|---|---|---|---|---|
| 0 | -40, -35, -1275 | 0 | 0x0 | 1 | 0x0d00 (cue 13) | 0x1600 (cue 22) | 0x044e3fff |
| 1 | 64, 0, -563 | 1.5708 | 0x280 | 1 | 0x8d01 (cue 13) | 0x9601 (cue 22) | 0x044e0ccc |
| 2 | 50, 0, -563 | -1.5708 | 0x0 | 1 | 0x8d00 (cue 13) | 0x9600 (cue 22) | 0x044e3fff |
| 3 | -25, 0, -197 | 3.14159 | 0x0 | 1 | 0x8d00 (cue 13) | 0x9600 (cue 22) | 0x044e3fff |
| 4 | 41, 0, -565.6 | -1.43411 | 0x0 | 0 | 0x8d00 (cue 13) | 0x9600 (cue 22) | 0x044e3fff |
| 5 | 39, 0, -225 | -1.5708 | 0x0 | 1 | 0x8d00 (cue 13) | 0x9600 (cue 22) | 0x044e3fff |
| 6 | 119, 60, -336 | 3.14159 | 0x0 | 1 | 0x8d00 (cue 13) | 0x9600 (cue 22) | 0x044e3fff |
| 7 | -97.5, 60, -670.3 | 1.5708 | 0x0 | 1 | 0x8d00 (cue 13) | 0x9600 (cue 22) | 0x044e3fff |
| 8 | 143, 0, -609.7 | 1.5708 | 0x680 | 0 | 0x8d01 (cue 13) | 0x9601 (cue 22) | 0x044e3fff |
| 9 | 115.5, 0, -609.7 | -1.5708 | 0x0 | 1 | 0x8d00 (cue 13) | 0x9600 (cue 22) | 0x044e3fff |
<!-- area_overview:spawn end -->

The two subs share every position, yaw, +0x10, +0x14 and +0x20; they differ only in +0x1C (cue 13 vs 22). Entry 4 is the AREA11 arrival. The high half 0x044E of +0x20 equals the area-1 ambient id SFX_REGISTRY_FIRST_LEVEL.md reports as cached at beat 15 (field meaning not verified here).

Door destinations: a door id with bit 7 set reads the record as {area, entry, has_sub, sub}, otherwise as {entry from side 0, entry from side 1} (FINDINGS s22; byte-matched 001BC150). "fl" is the placement's flags2 (door id | 0x80 for an area change), "m" its model.

<!-- area_overview:doors begin -->
| Door id | Record | Placements using it | Meaning |
|---:|---|---|---|
| 0 | 00 00 00 00 | s0[12] 0x823580 fl 0x80 m 0x03 (-35.5, -35, -1276.5)<br>s1[9] 0x1bc350 fl 0x80 m 0x03 (-35.5, -35, -1277) | area change → area 0 entry 0 sub 0 |
| 1 | 02 00 00 00 | s0[14] 0x1bc350 fl 0x81 m 0x15 (-20.5, 0, -192)<br>s1[11] 0x1bc350 fl 0x81 m 0x15 (-20.5, 0, -192.5) | area change → area 2 entry 0 sub 0 |
| 2 | 01 02 00 00 | s0[15] 0x1bc350 fl 0x02 m 0x03 (60.5, 0.5, -559) | room move → entry 1 (side 0) / 2 (side 1) |
| 3 | 02 01 01 01 | s0[16] 0x1bc350 fl 0x83 m 0x03 (50, 0, -220.5)<br>s1[12] 0x1bc350 fl 0x83 m 0x03 (50, 0, -220.5) | area change → area 2 entry 1 sub 1 |
| 4 | 09 08 00 00 | s0[17] 0x1bb860 fl 0x04 m 0x09 (128.6, 0, -610)<br>s1[13] 0x1bb860 fl 0x04 m 0x09 (128.6, 0, -610) | room move → entry 9 (side 0) / 8 (side 1) |
| 5 | 16 05 00 00 | s0[18] 0x1bb860 fl 0x85 m 0x09 (120, 60, -318.8)<br>s1[14] 0x1bb860 fl 0x85 m 0x09 (120, 60, -318.8) | area change → area 22 entry 5 sub 0 |
| 6 | 06 00 00 00 | s0[19] 0x1bc350 fl 0x86 m 0x03 (-109.5, 60, -674.5)<br>s1[15] 0x1bc350 fl 0x86 m 0x03 (-109.5, 60, -674.5) | area change → area 6 entry 0 sub 0 |
| 7 | 00 00 00 00 | — | no placement uses it |
<!-- area_overview:doors end -->

Door 0 is the overlay-scripted door 0x823580 in sub 0 (a plain 001BC350 door in sub 1). The AREA11 side is FIRST_LEVEL_EXIT.md section 4. Lock gating (D_00810841[1]) is FINDINGS s63 territory and not re-checked here. The route capture exercised doors 0 and 2 only (SECOND_LEVEL_ROUTE.md section 2).

## 6. Music, sound scope and messages

- **BGM.** Sub 0 spawn records carry 0x8D00 (entry 0: 0x0D00) and sub 1 0x9600, so cues 13 and 22 (section 5). The captured lane 0 plays cue 13 (summary). Cue 13 is already in the port's stream export (IOP_STREAM.md, "the music right after the AREA11 exit"). Other music requests in the overlay (the only direct calls to 001FABB0/001FB0B0/001FAE70, `overview.json` `calls_boot`; order from the committed C): 0x826BA0 (sub 1 owner) calls 001FABB0 then 001FB0B0(0x14 = cue 20) when its script 0x82BAD0 ends; 0x825F00 calls 001FAE70(0) after spawning groups 0x829220 and 0x8291C0. Area music rows: registries table.
- **Sound scope** (001FB9F0 area-paged ids; D_00264A70/D_00264AD0 remap tables, 0xFF = absent):

<!-- area_overview:sound_scope begin -->
| Sub | 0x3E8..0x5DB present | 0x7D0..0x9C3 present |
|---|---|---|
| 0 | 285 (remap 0x25f590): 0x3e8, 0x3ee, 0x3f2, 0x3fc..0x400, 0x40f..0x414, 0x423..0x428, 0x444..0x44e, 0x451..0x455, 0x45f..0x4fd, 0x50d..0x50e, 0x559..0x55e, 0x56f..0x5b7, 0x5be..0x5bf, 0x5c1..0x5c2, 0x5c5, 0x5cd..0x5ce, 0x5d5..0x5d6 | 163 (remap 0x2619a0): 0x7d0..0x7e1, 0x8a8..0x8ac, 0x938..0x9c3 |
| 1 | 351 (remap 0x260ac0): 0x3e8, 0x3ee, 0x3f2, 0x3fc..0x400, 0x423..0x428, 0x42d..0x434, 0x43b..0x441, 0x444..0x44f, 0x45f..0x518, 0x51e, 0x524, 0x52b..0x530, 0x53f..0x544, 0x55d..0x571, 0x574..0x57f, 0x58f..0x5db | 198 (remap 0x261b70): 0x7d0..0x7f3, 0x816..0x826, 0x8a8..0x8ac, 0x938..0x9c3 |
<!-- area_overview:sound_scope end -->

  The port's registry (SFX_REGISTRY_FIRST_LEVEL.md) covers area 11 only; the area-1 cells and their banks (the per-area container in `chunk05`, id 0x71 by size, unverified) are new work.
- **Messages.** D_00264DD0[2] = 0x26F060, 184 eight-byte records (registries table). The port's message exporter handles only the AREA11 table and bank (`tools/export_message_data.py`, `AREA = 11`); the AREA01 bank location must be found the same way (FINDINGS s69 names `chunk05` id 0x41; unverified).

## 7. Owner roster

Behaviour = the actor callback (+0x10). "Live" = the pool slot at f801 of the arrival capture that holds the record (matched by behaviour and position, else by behaviour and +0x9A); "—" = not live at f801 (never spawned, deferred, or already freed).

### 7.1 Sub 0 placement table 0x82BD50

<!-- area_overview:placements_sub0 begin -->
| # | Class | Model | Flags2 | Param | UID | Kind | Link | Position | Yaw | Behaviour | Live |
|---:|---|---|---|---|---|---|---|---|---:|---|---|
| 0 | 0x0b | 0x00 | 0x00 | 0x1 | 0x0000 | 0x52 | 0xffff | -15, 47.5, -419.5 | 0 | 0x1c2420 | — |
| 1 | 0x0b | 0x00 | 0x00 | 0x29 | 0x0100 | 0x51 | 0xffff | -5.3, 0, -541 | 0 | 0x1c2420 | — |
| 2 | 0x04 | 0x0a | 0x00 | 0x13 | 0x0500 | 0x46 | 0xffff | 25.3, -60, -1226.4 | 0 | 0x156620 | 25 |
| 3 | 0x04 | 0x18 | 0x00 | 0x15 | 0x0200 | 0x46 | 0xffff | 15.1, 0, -270 | -0.68766 | 0x156620 | 26 |
| 4 | 0x04 | 0x18 | 0x00 | 0x14 | 0x0300 | 0x46 | 0xffff | 33.8, 0, -293.9 | 0 | 0x156620 | 27 |
| 5 | 0x04 | 0x18 | 0x00 | 0x14 | 0x0400 | 0x46 | 0xffff | 25.1, -60, -1217.8 | 0 | 0x156620 | 28 |
| 6 | 0x04 | 0x06 | 0x00 | 0x4 | 0x1900 | 0xd | 0xffff | -23, 0, -253 | 0 | 0x1551b0 | 29 |
| 7 | 0x04 | 0x06 | 0x00 | 0x4 | 0x1a01 | 0xd | 0x0 | -25, 0, -279 | 0.33161 | 0x1551b0 | 30 |
| 8 | 0x04 | 0x06 | 0x00 | 0x4 | 0x1b00 | 0xd | 0xffff | 14.7, 14, -717.9 | 0 | 0x1551b0 | 31 |
| 9 | 0x04 | 0x06 | 0x00 | 0x4 | 0x1c00 | 0xd | 0xffff | -20.3, 15, -706.6 | 0.53233 | 0x1551b0 | 32 |
| 10 | 0x04 | 0x06 | 0x00 | 0x4 | 0x1d00 | 0xd | 0xffff | -43.1, 0, -641.5 | 0 | 0x1551b0 | 33 |
| 11 | 0x04 | 0x06 | 0x00 | 0x4 | 0x1e00 | 0xd | 0xffff | 23, 0, -772.7 | 0 | 0x1551b0 | 34 |
| 12 | 0x85 | 0x03 | 0x80 | 0x6 | 0x1500 | 0x3 | 0x303 | -35.5, -35, -1276.5 | 0 | 0x823580 | 35 |
| 13 | 0x08 | 0x02 | 0x00 | 0x12 | 0x0600 | 0x0 | 0xffff | -40, -13, -1277 | 0 | 0x158d30 | 36 |
| 14 | 0x85 | 0x15 | 0x81 | 0xf | 0x0d00 | 0x4 | 0x200 | -20.5, 0, -192 | 0 | 0x1bc350 | 37 |
| 15 | 0x85 | 0x03 | 0x02 | 0x5 | 0x1700 | 0x3 | 0x200 | 60.5, 0.5, -559 | -1.5708 | 0x1bc350 | 38 |
| 16 | 0x85 | 0x03 | 0x83 | 0x5 | 0x1800 | 0x3 | 0x200 | 50, 0, -220.5 | -1.5708 | 0x1bc350 | 39 |
| 17 | 0x85 | 0x09 | 0x04 | 0xb | 0x1200 | 0x4 | 0x100 | 128.6, 0, -610 | 1.5708 | 0x1bb860 | 40 |
| 18 | 0x85 | 0x09 | 0x85 | 0xb | 0x1100 | 0x4 | 0x100 | 120, 60, -318.8 | 0 | 0x1bb860 | 41 |
| 19 | 0x85 | 0x03 | 0x86 | 0x5 | 0x1600 | 0x3 | 0x200 | -109.5, 60, -674.5 | 1.5708 | 0x1bc350 | 42 |
| 20 | 0x84 | 0x38 | 0x00 | 0xa | 0x1300 | 0xe | 0xffff | 167, 7, -626.7 | 0 | 0x159b90 | 43 |
| 21 | 0x0d | 0x01 | 0x00 | 0x2 | 0x0000 | 0x0 | 0xffff | 14, -2, -821 | 0 | 0x1e3d90 | 44 |
| 22 | 0x0d | 0x01 | 0x00 | 0x2 | 0x0000 | 0x0 | 0xffff | -17, -26, -963 | 0 | 0x1e3d90 | 45 |
| 23 | 0x0d | 0x01 | 0x00 | 0x2 | 0x0000 | 0x0 | 0xffff | 20, -60, -1196 | 0 | 0x1e3d90 | 46 |
| 24 | 0x0d | 0x01 | 0x00 | 0x1 | 0x0000 | 0x0 | 0xffff | -12, 0, -724 | 0 | 0x1e3d90 | 47 |
| 25 | 0x0d | 0x01 | 0x00 | 0x1 | 0x0000 | 0x0 | 0xffff | 1, 0, -734 | 0 | 0x1e3d90 | 48 |
| 26 | 0x0d | 0x01 | 0x00 | 0x1 | 0x0000 | 0x0 | 0xffff | 10, 0, -798 | 0 | 0x1e3d90 | 49 |
| 27 | 0x0d | 0x01 | 0x00 | 0x1 | 0x0000 | 0x0 | 0xffff | -2, -4, -833 | 0 | 0x1e3d90 | 50 |
| 28 | 0x0d | 0x01 | 0x00 | 0x1 | 0x0000 | 0x0 | 0xffff | -20, -22, -943 | 0 | 0x1e3d90 | 51 |
| 29 | 0x0d | 0x01 | 0x00 | 0x1 | 0x0000 | 0x0 | 0xffff | -1, -60, -1201 | 0 | 0x1e3d90 | 52 |
| 30 | 0x0d | 0x01 | 0x00 | 0x0 | 0x0000 | 0x0 | 0xffff | -11, 0, -653 | 0 | 0x1e3d90 | 53 |
| 31 | 0x0d | 0x01 | 0x00 | 0x0 | 0x0000 | 0x0 | 0xffff | 0, 0, -722 | 0 | 0x1e3d90 | 54 |
| 32 | 0x0d | 0x01 | 0x00 | 0x0 | 0x0000 | 0x0 | 0xffff | 31, 0, -710 | 0 | 0x1e3d90 | 55 |
| 33 | 0x0d | 0x01 | 0x00 | 0x0 | 0x0000 | 0x0 | 0xffff | 24, 0, -794 | 0 | 0x1e3d90 | 56 |
| 34 | 0x0d | 0x01 | 0x00 | 0x0 | 0x0000 | 0x0 | 0xffff | 0, -24, -955 | 0 | 0x1e3d90 | 57 |
| 35 | 0x0c | 0x00 | 0x00 | 0x0 | 0x0000 | 0x0 | 0x0 | -30, -28, -1019.5 | 0 | 0x1e7d20 | 58 |
| 36 | 0xaa | 0x01 | 0x00 | 0x47 | 0x0000 | 0x0 | 0x0 | 81, 0, -521 | -0.95993 | 0x825350 | 59 |
| 37 | 0x08 | 0x01 | 0x00 | 0x6b | 0x0000 | 0x0 | 0x0 | 0, 0, 0 | 0 | 0x826cf0 | 60 |
| 38 | 0xaa | 0x01 | 0x00 | 0x47 | 0x0000 | 0x0 | 0x0 | 81, 0, -521 | -0.95993 | 0x825740 | — |
| 39 | 0x08 | 0x01 | 0x00 | 0x6b | 0x0000 | 0x0 | 0x0 | 0, 0, 0 | 0 | 0x826cf0 | — |
| 40 | 0x0c | 0x07 | 0x00 | 0x0 | 0x0000 | 0x0 | 0xffff | 75.3, 7.2, -516.9 | 0 | 0x1c50b0 | — |
| 41 | 0x04 | 0x02 | 0x00 | 0x2 | 0x2000 | 0x46 | 0xffff | 0, 3, -525 | -3.14159 | 0x8261a0 | 64 |
| 42 | 0x04 | 0x02 | 0x00 | 0x3 | 0x1f00 | 0x46 | 0xffff | 0, 3, -315 | -3.14159 | 0x8261a0 | 65 |
| 43 | 0x04 | 0x00 | 0x00 | 0x8 | 0x1400 | 0x3 | 0xffff | 75.3, 7.2, -516.9 | -0.7854 | 0x1c4820 | 66 |
| 44 | 0x08 | 0x00 | 0x00 | 0xc | 0x0f00 | 0x3 | 0xffff | 0, 156.9, -502.3 | 0 | 0x1c4820 | 67 |
| 45 | 0x04 | 0x00 | 0x00 | 0xc | 0x1000 | 0x3 | 0xffff | 0, 156.9, -337.9 | -3.14159 | 0x8267c0 | 68 |
| 46 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0000 | 0x1 | 0x0 | 114.151, 0.01, -635.102 | 0 | 0x15a2c0 | 69 |
| 47 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0001 | 0x1 | 0x0 | -21.064, -58.934, -1249.4 | 0 | 0x15a2c0 | 70 |
| 48 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0002 | 0x0 | 0x0 | 18.539, -58.934, -1230.18 | 0 | 0x15a2c0 | 71 |
| 49 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0003 | 0x5 | 0x0 | 10.4, -58.934, -1219.79 | 0 | 0x15a2c0 | 72 |
| 50 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0004 | 0x1 | 0x0 | -23.81, -59.843, -1189 | 0 | 0x15a2c0 | 73 |
| 51 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0005 | 0x1 | 0x0 | -29.426, 0.01, -784.562 | 0 | 0x15a2c0 | 74 |
| 52 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0006 | 0x1 | 0x0 | 29.417, 0.01, -771.617 | 0 | 0x15a2c0 | 75 |
| 53 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0007 | 0x1 | 0x0 | 36.478, 0.01, -662.169 | 0 | 0x15a2c0 | 76 |
<!-- area_overview:placements_sub0 end -->

- Records 0 and 1 are class 0x0B (skipped at load by 001B6990, queried later by 0019C6F0).
- Record 36 (0x825350) is the control-room NPC: in the AREA01 route capture a character stands at the console, both conversations run on this node, and its 0x80 branch wrote D_008107D9 = 0x81 at a01_05 (SECOND_LEVEL_ROUTE.md section 2). The old "drawbridge crank" label (FINDINGS s74, the retired drawbridge export) is wrong for this record.
- Records 38 (0x825740, same position and class as 36), 39 (0x826CF0) and 40 (0x1C50B0) are not live at f801. The AREA01 route census armed 0x825740's entry and never hit it in any of its 12 beats (the Route column of section 8), and SECOND_LEVEL_ROUTE.md section 7 says [38] was not spawned in this load. The beat-15 census logged two pauses at 0x82579C and 0x8258FC (inside 0x825740) as unexpected and ignored them (FIRST_LEVEL_EXIT.md section 5); whether [38] ran at the arrival is open.

### 7.2 Groups (deferred, nest, and groups reached from code or scripts)

<!-- area_overview:groups begin -->
| Group | Records | How it is reached | Behaviours |
|---|---:|---|---|
| 0x828a00 | 40 | sub0 deferred group 0x828a00; D_0024D820[1][0] list | 0x128c10, 0x15afa0, 0x1c02e0, 0x1e3d90, 0x219550, 0x823cd0, 0x825950, 0x826cf0, 0x826d40, 0x828850 |
| 0x8291c0 | 1 | group 0x8291c0; code 0x825f00 | 0x15afa0 |
| 0x829220 | 6 | sub0 deferred group 0x829220; D_0024D820[1][0] list | 0x12a5d0 |
| 0x829360 | 4 | nest group link 0 0x829360; D_0024D820[1][2+0] | 0x12a5d0 |
| 0x829440 | 22 | sub1 deferred group 0x829440; D_0024D820[1][1] list | 0x1383c0, 0x147390, 0x15afa0, 0x1bf6b0, 0x1c06e0, 0x219550, 0x826d40, 0x828850 |
| 0x82a900 | 8 | group 0x82a900; word 0x82aea4 (script 0x82ad90) | 0x0, 0x8237d0, 0x823900, 0x8239c0 |
| 0x829110 | 3 | unreferenced (no pointer found) | 0x12a5d0 |
<!-- area_overview:groups end -->

Records of the groups that belong to sub 0 (the current sub of the capture) or to no sub list:

<!-- area_overview:group_records begin -->
| Group record | Cond (+0, +2) | Class | Model | Param | UID | Kind | Link | Position | Behaviour | Live |
|---|---|---|---|---|---|---|---|---|---|---|
| 0x828a00[0] | 3, 1536 | 0x02 | 0x00 | 0x43 | 0x0000 | 0x0 | 0x0 | -45.6, 45, -568.3 | 0x823cd0 | — |
| 0x828a00[1] | 3, 1536 | 0x02 | 0x00 | 0x33 | 0x0000 | 0x0 | 0x0 | -45.6, 50, -568.3 | 0x823cd0 | — |
| 0x828a00[2] | 3, 1536 | 0x02 | 0x00 | 0x23 | 0x0000 | 0x0 | 0x0 | -45.6, 40, -568.3 | 0x823cd0 | — |
| 0x828a00[3] | 3, 1536 | 0x02 | 0x00 | 0x13 | 0x0000 | 0x0 | 0x0 | -45.6, 30, -568.3 | 0x823cd0 | — |
| 0x828a00[4] | 3, 1536 | 0x02 | 0x00 | 0x3 | 0x0000 | 0x0 | 0x0 | -45.6, 35, -568.3 | 0x823cd0 | — |
| 0x828a00[5] | 3, 1536 | 0x2a | 0x01 | 0x47 | 0x0000 | 0x0 | 0x0 | -9, 0, -576 | 0x825950 | — |
| 0x828a00[6] | 3, 1536 | 0x08 | 0x01 | 0x6b | 0x0000 | 0x0 | 0x0 | 0, 0, 0 | 0x826cf0 | — |
| 0x828a00[7] | 3, 1536 | 0x2a | 0x01 | 0x4b | 0x0000 | 0x0 | 0x0 | -5, 0, -543 | 0x825950 | — |
| 0x828a00[8] | 1, 7 | 0x84 | 0x02 | 0x72 | 0x2100 | 0x46 | 0x12d | -34.8, 14.8, -722.6 | 0x219550 | 0 |
| 0x828a00[9] | 1, 8 | 0x84 | 0x02 | 0x72 | 0x2200 | 0x46 | 0x12c | 12.5, -29, -984.5 | 0x219550 | 1 |
| 0x828a00[10] | 1, 1 | 0xc7 | 0x00 | 0x4d | 0xff00 | 0x46 | 0xffff | 14.8, 14.5, -717.3 | 0x15afa0 | 2 |
| 0x828a00[11] | 1, 2 | 0xc7 | 0x02 | 0x58 | 0xff00 | 0x46 | 0xc9 | 136.5, 60.1, -473.1 | 0x15afa0 | 3 |
| 0x828a00[12] | 1, 3 | 0xc7 | 0x02 | 0x58 | 0xff00 | 0x46 | 0xca | 111.8, 8, -509.5 | 0x15afa0 | 4 |
| 0x828a00[13] | 1, 4 | 0xc7 | 0x00 | 0x4d | 0xff00 | 0x46 | 0xffff | 142.8, 2.1, -416.2 | 0x15afa0 | 5 |
| 0x828a00[14] | 1, 6 | 0xc7 | 0x00 | 0x4d | 0xff00 | 0x46 | 0xffff | -69.3, 25.4, -731.1 | 0x15afa0 | 6 |
| 0x828a00[15] | 1, 9 | 0xc7 | 0x02 | 0x58 | 0xff00 | 0x46 | 0x64 | 62.1, 16, -574.4 | 0x15afa0 | 7 |
| 0x828a00[16] | 3, 1802 | 0xc7 | 0x00 | 0x6c | 0xff00 | 0x46 | 0x4 | -11.392, 0.192, -607.441 | 0x15afa0 | — |
| 0x828a00[17] | 1, 11 | 0x87 | 0x01 | 0xd | 0xff00 | 0xb | 0xffff | 61.8, 15, -549.6 | 0x15afa0 | 8 |
| 0x828a00[18] | 0, 0 | 0x04 | 0x1a | 0x10 | 0x0a00 | 0x3 | 0xffff | -45, -3, -1140 | 0x826d40 | 9 |
| 0x828a00[19] | 0, 80 | 0x04 | 0x29 | 0x11 | 0x0800 | 0x3 | 0xffff | -45, -3, -1140 | 0x828850 | 10 |
| 0x828a00[20] | 0, 0 | 0x04 | 0x1a | 0x10 | 0x0b00 | 0x3 | 0xffff | -45, 37, -900 | 0x826d40 | 11 |
| 0x828a00[21] | 0, 81 | 0x04 | 0x29 | 0x11 | 0x0900 | 0x3 | 0xffff | -45, 37, -900 | 0x828850 | 12 |
| 0x828a00[22] | 0, 0 | 0x04 | 0x1a | 0x10 | 0x0c00 | 0x3 | 0xffff | 30, 17, -1020 | 0x826d40 | 13 |
| 0x828a00[23] | 0, 82 | 0x04 | 0x29 | 0x11 | 0x0700 | 0x3 | 0xffff | 30, 17, -1020 | 0x828850 | 14 |
| 0x828a00[24] | 3, 1636 | 0x02 | 0x00 | 0x3 | 0x0000 | 0x0 | 0x0 | 31.6, 12.4, -793.8 | 0x128c10 | — |
| 0x828a00[25] | 3, 1637 | 0x02 | 0x00 | 0x3 | 0x0000 | 0x0 | 0x0 | -33.6, 2.5, -802 | 0x128c10 | — |
| 0x828a00[26] | 3, 1638 | 0x02 | 0x00 | 0x8 | 0x0000 | 0x0 | 0x0 | -38.8, -62, -1212.5 | 0x128c10 | — |
| 0x828a00[27] | 3, 1639 | 0x02 | 0x00 | 0x8 | 0x0000 | 0x0 | 0x0 | -51.9, 12.2, -731.2 | 0x128c10 | — |
| 0x828a00[28] | 3, 1640 | 0x02 | 0x00 | 0x8 | 0x0000 | 0x0 | 0x0 | -41.1, 8.1, -686.9 | 0x128c10 | — |
| 0x828a00[29] | 3, 1641 | 0x02 | 0x00 | 0x8 | 0x0000 | 0x0 | 0x0 | -46.9, 1.9, -724 | 0x128c10 | — |
| 0x828a00[30] | 1, 124 | 0x02 | 0x00 | 0x8 | 0x0000 | 0x0 | 0x0 | 128.9, 84.6, -671.5 | 0x128c10 | 15 |
| 0x828a00[31] | 1, 125 | 0x02 | 0x00 | 0x8 | 0x0000 | 0x0 | 0x0 | 82.8, 75.9, -660.4 | 0x128c10 | 16 |
| 0x828a00[32] | 1, 126 | 0x02 | 0x00 | 0x8 | 0x0000 | 0x0 | 0x0 | 129.7, 58.4, -392.4 | 0x128c10 | 17 |
| 0x828a00[33] | 1, 127 | 0x02 | 0x00 | 0x8 | 0x0000 | 0x0 | 0x0 | 133.9, 58.4, -383.8 | 0x128c10 | 18 |
| 0x828a00[34] | 1, 128 | 0x02 | 0x00 | 0x8 | 0x0000 | 0x0 | 0x0 | 132.6, 58.4, -397.5 | 0x128c10 | 19 |
| 0x828a00[35] | 1, 129 | 0x02 | 0x00 | 0x8 | 0x0000 | 0x0 | 0x0 | 125.3, 58.4, -395.4 | 0x128c10 | 20 |
| 0x828a00[36] | 1, 131 | 0x02 | 0x12 | 0x20 | 0x0000 | 0x8 | 0xffff | 140.2, 66.5, -450.1 | 0x1c02e0 | 21 |
| 0x828a00[37] | 2, 1536 | 0x0d | 0x01 | 0x2 | 0x0000 | 0x0 | 0xffff | -35, 0, -677 | 0x1e3d90 | 22 |
| 0x828a00[38] | 2, 1536 | 0x0d | 0x01 | 0x2 | 0x0000 | 0x0 | 0xffff | -60, 0, -684 | 0x1e3d90 | 23 |
| 0x828a00[39] | 2, 1536 | 0x0d | 0x01 | 0x1 | 0x0000 | 0x0 | 0xffff | -52, 0, -661 | 0x1e3d90 | 24 |
| 0x8291c0[0] | 3, 1802 | 0xc7 | 0x00 | 0x6c | 0xff00 | 0x46 | 0x4 | -11.392, 0.192, -607.441 | 0x15afa0 | — |
| 0x829220[0] | 3, 1898 | 0x02 | 0x00 | 0x3 | 0x0000 | 0x0 | 0x0 | -12.4, -1.6, -547 | 0x12a5d0 | — |
| 0x829220[1] | 3, 1899 | 0x02 | 0x00 | 0x3 | 0x0000 | 0x0 | 0x0 | 11.2, -1.6, -549 | 0x12a5d0 | — |
| 0x829220[2] | 3, 1900 | 0x02 | 0x00 | 0x3 | 0x0000 | 0x0 | 0x0 | 17.9, -1.6, -548 | 0x12a5d0 | — |
| 0x829220[3] | 3, 1901 | 0x02 | 0x00 | 0x3 | 0x0000 | 0x0 | 0x0 | -1.7, 18.6, -192.4 | 0x12a5d0 | — |
| 0x829220[4] | 3, 1902 | 0x02 | 0x00 | 0x3 | 0x0000 | 0x0 | 0x0 | 22, 3.6, -277 | 0x12a5d0 | — |
| 0x829220[5] | 3, 1903 | 0x02 | 0x00 | 0x3 | 0x0000 | 0x0 | 0x0 | 2, 15.9, -192 | 0x12a5d0 | — |
| 0x829360[0] | 1, 112 | 0x02 | 0x00 | 0x4 | 0x0000 | 0x0 | 0x0 | -1, 1, -1 | 0x12a5d0 | — |
| 0x829360[1] | 1, 113 | 0x02 | 0x00 | 0x4 | 0x0000 | 0x0 | 0x0 | -1, 1, 0 | 0x12a5d0 | — |
| 0x829360[2] | 1, 114 | 0x02 | 0x00 | 0x4 | 0x0000 | 0x0 | 0x0 | -1, 1, 1 | 0x12a5d0 | — |
| 0x829360[3] | 1, 115 | 0x02 | 0x00 | 0x4 | 0x0000 | 0x0 | 0x0 | 1, 1, 1 | 0x12a5d0 | — |
| 0x82a900[0] | 9, 1 | 0x4e | 0x96 | 0x4 | 0x0000 | 0x0 | 0x3f00 | 0, 0, 0 | 0x0 | — |
| 0x82a900[1] | 8, 1 | 0x67 | 0x96 | 0x4 | 0x0005 | 0x0 | 0x0 | 0, 0, 0 | 0x0 | — |
| 0x82a900[2] | 9, 1 | 0x4b | 0x96 | 0x5 | 0x0006 | 0x0 | 0x3f00 | 0, 0, 0 | 0x0 | — |
| 0x82a900[3] | 9, 2 | 0x6c | 0x96 | 0x7 | 0x0002 | 0x0 | 0x3f00 | 0, 0, 0 | 0x0 | — |
| 0x82a900[4] | 9, 2 | 0x4f | 0x96 | 0x8 | 0x0002 | 0x0 | 0x3f00 | 0, 0, 0 | 0x0 | — |
| 0x82a900[5] | 9, 4 | 0x270e | 0x96 | 0x9 | 0x0007 | 0x0 | 0x3f00 | 0, 0, 0 | 0x8237d0 | — |
| 0x82a900[6] | 9, 4 | 0x270e | 0x96 | 0xa | 0x0007 | 0x0 | 0x3f00 | 0, 0, 0 | 0x823900 | — |
| 0x82a900[7] | 9, 4 | 0x270e | 0x96 | 0xb | 0x0007 | 0x0 | 0x3f00 | 0, 0, 0 | 0x8239c0 | — |
<!-- area_overview:group_records end -->

- 0x829360 is the nest group of the one nest-linked crate (placement [7], link 0; its four 0x12A5D0 records spawn only when that crate breaks). 0x8291C0 is spawned by 0x825F00 (with 0x829220). The group at 0x829110 is referenced by no pointer the tool found (unreferenced, or reached by an address computation it does not see).
- 0x82A900 is reached only from the op14 record of script 0x82AD90; its layout differs from the deferred groups (first halfwords 9/8), and its last three records carry 0x8237D0, 0x823900 and 0x8239C0 at +0x28, the only way those three overlay functions are reached.
- Cond-3 records are not live at the arrival (their condition, em_actor_roster.c condition_001B6660, is not met at this point); the class-2 records [30..35] of 0x828A00 are live, matched by +0x9A because they had already moved.

### 7.3 Live pool at the arrival (f801)

The last column is the port's first-level census row for that behaviour at port b7868e1: the Port status and the module part of the Module / test cell, whole (`overview.json` `port_census_rows` keeps every cell).

<!-- area_overview:live_pool begin -->
| Behaviour | Nodes | From | First-level census row (port status and module) |
|---|---:|---|---|
| 0x1e3d90 | 17 | sub0 deferred group 0x828a00 ×3; place ×14 | not in census |
| 0x15a2c0 | 8 | place ×8 | not in census |
| 0x15afa0 | 7 | sub0 deferred group 0x828a00 ×7 | live (em_pickup_owner em_pickup_owner_tick via em_pickup.c and the AREA11 interaction host) |
| 0x18a6b0 | 7 | runtime spawn ×7 | live (em_player_equipment em_player_equipment_tick through em_equipment_live on the pool nodes (em_area11_bindings)) |
| 0x128c10 | 6 | sub0 deferred group 0x828a00 ×6 | not in census |
| 0x1551b0 | 6 | place ×6 | live (em_crate_original over its roster node (em_area11_boxes.c)) |
| 0x1c5680 | 6 | runtime spawn ×6 | live (em_indicator_child em_indicator_child_step, per node (em_area11_bindings.c tick_indicator)) |
| 0x156620 | 4 | place ×4 | live (em_drum_original over its roster node (em_area11_boxes.c)) |
| 0x1bc350 | 4 | place ×4 | live (em_door_original em_door_original_tick via em_area11_door (node tick_door; census L18)) |
| 0x826d40 | 3 | sub0 deferred group 0x828a00 ×3 | not in census |
| 0x828850 | 3 | sub0 deferred group 0x828a00 ×3 | not in census |
| 0x1bb860 | 2 | place ×2 | not in census |
| 0x1c4820 | 2 | place ×2 | verified-unbound (em_status_ui_leftovers em_sul_001C4820) |
| 0x1e2560 | 2 | runtime spawn ×2 | live (em_head_sprite_original em_head_sprite_original_tick through em_effects_live on the pool nodes (the player's and Roger's)) |
| 0x219550 | 2 | sub0 deferred group 0x828a00 ×2 | live (em_pickup_owner em_pickup_owner_tick via em_pickup.c and the AREA11 interaction host) |
| 0x8261a0 | 2 | place ×2 | not in census |
| 0x158d30 | 1 | place ×1 | not in census |
| 0x159b90 | 1 | place ×1 | not in census |
| 0x1bffd0 | 1 | runtime spawn ×1 | not in census |
| 0x1c02e0 | 1 | sub0 deferred group 0x828a00 ×1 | not in census |
| 0x1c5930 | 1 | runtime spawn ×1 | verified-unbound (em_status_ui_leftovers em_sul_001C5930) |
| 0x1e7d20 | 1 | place ×1 | not in census |
| 0x823580 | 1 | place ×1 | not in census |
| 0x825350 | 1 | place ×1 | not in census |
| 0x8267c0 | 1 | place ×1 | not in census |
| 0x826cf0 | 1 | place ×1 | not in census |
<!-- area_overview:live_pool end -->

Runtime spawns (no table record): the area title 001C5930, the seven equipment nodes 0018A6B0, the head sprites 001E2560, the indicator children 001C5680 (one per item/door owner, at their positions) and 001BFFD0, which shares its position with group record 0x828A00[36] (0x1C02E0) and is presumably its child (not proven).

### 7.4 Sub 1 (the fan's direct exit; not on the first route)

<!-- area_overview:placements_sub1 begin -->
| # | Class | Model | Flags2 | Param | UID | Kind | Link | Position | Yaw | Behaviour |
|---:|---|---|---|---|---|---|---|---|---:|---|
| 0 | 0x04 | 0x0a | 0x00 | 0x13 | 0x0300 | 0x46 | 0xffff | 25.3, -60, -1226.4 | 0 | 0x156620 |
| 1 | 0x04 | 0x18 | 0x00 | 0x15 | 0x0000 | 0x46 | 0xffff | 15, 0, -270 | -0.68766 | 0x156620 |
| 2 | 0x04 | 0x18 | 0x00 | 0x14 | 0x0100 | 0x46 | 0xffff | 33.8, 0, -293.9 | 0 | 0x156620 |
| 3 | 0x04 | 0x18 | 0x00 | 0x14 | 0x0200 | 0x46 | 0xffff | 25.1, -60, -1217.8 | 0 | 0x156620 |
| 4 | 0x04 | 0x06 | 0x00 | 0x4 | 0x1400 | 0xd | 0xffff | -23, 0, -253 | 0 | 0x1551b0 |
| 5 | 0x04 | 0x06 | 0x00 | 0x4 | 0x1501 | 0xd | 0x0 | -25, 0, -279 | 0.33161 | 0x1551b0 |
| 6 | 0x04 | 0x06 | 0x00 | 0x4 | 0x1600 | 0xd | 0xffff | 14.7, 14, -717.9 | 0 | 0x1551b0 |
| 7 | 0x04 | 0x06 | 0x00 | 0x4 | 0x1700 | 0xd | 0xffff | 23, 0, -772.7 | 0 | 0x1551b0 |
| 8 | 0x04 | 0x06 | 0x00 | 0x4 | 0x1800 | 0xd | 0xffff | -43.1, 0, -641.5 | 0 | 0x1551b0 |
| 9 | 0x85 | 0x03 | 0x80 | 0x6 | 0x1100 | 0x3 | 0x380 | -35.5, -35, -1277 | 0 | 0x1bc350 |
| 10 | 0x08 | 0x02 | 0x01 | 0x12 | 0x0400 | 0x0 | 0xffff | -40, -13, -1277 | 0 | 0x158d30 |
| 11 | 0x85 | 0x15 | 0x81 | 0xf | 0x0b00 | 0x3 | 0x280 | -20.5, 0, -192.5 | 0 | 0x1bc350 |
| 12 | 0x85 | 0x03 | 0x83 | 0x5 | 0x1300 | 0x3 | 0x280 | 50, 0, -220.5 | -1.5708 | 0x1bc350 |
| 13 | 0x85 | 0x09 | 0x04 | 0xb | 0x0e00 | 0x4 | 0x180 | 128.6, 0, -610 | 1.5708 | 0x1bb860 |
| 14 | 0x85 | 0x09 | 0x85 | 0xb | 0x0f00 | 0x4 | 0x180 | 120, 60, -318.8 | 0 | 0x1bb860 |
| 15 | 0x85 | 0x03 | 0x86 | 0x5 | 0x1200 | 0x3 | 0x280 | -109.5, 60, -674.5 | 1.5708 | 0x1bc350 |
| 16 | 0x84 | 0x38 | 0x00 | 0xa | 0x1000 | 0xe | 0xffff | 167, 7, -626.7 | 0 | 0x159b90 |
| 17 | 0x04 | 0x02 | 0x00 | 0x2 | 0x1a00 | 0x46 | 0xffff | 0, 3, -525 | -3.14159 | 0x8261a0 |
| 18 | 0x04 | 0x02 | 0x00 | 0x3 | 0x1900 | 0x46 | 0xffff | 0, 3, -315 | -3.14159 | 0x8261a0 |
| 19 | 0x08 | 0x00 | 0x00 | 0xc | 0x0c00 | 0x0 | 0xffff | 0, 156.9, -502.3 | 0 | 0x826ba0 |
| 20 | 0x08 | 0x00 | 0x00 | 0xc | 0x0d00 | 0x0 | 0xffff | 0, 156.9, -337.9 | -3.14159 | 0x1c4820 |
| 21 | 0x0c | 0x00 | 0x00 | 0x0 | 0x0000 | 0x0 | 0x0 | -30, -28, -1019.5 | 0 | 0x1e7d20 |
| 22 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0000 | 0x0 | 0x1 | -13.854, 0.01, -238.77 | 0 | 0x15a2c0 |
| 23 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0001 | 0x1 | 0x2 | 5.18, -59.956, -1208.22 | 0 | 0x15a2c0 |
| 24 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0002 | 0x3 | 0x2 | -19.703, -59.956, -1193.06 | 0 | 0x15a2c0 |
| 25 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0003 | 0x0 | 0x1 | 28.436, 0.01, -647.953 | 0 | 0x15a2c0 |
| 26 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0004 | 0x3 | 0x2 | 80.588, 0.01, -615.504 | 0 | 0x15a2c0 |
| 27 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0005 | 0x1 | 0x2 | 39.471, 0.01, -580.977 | 0 | 0x15a2c0 |
| 28 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0006 | 0x1 | 0x2 | -25.388, 0.01, -564.429 | 0 | 0x15a2c0 |
| 29 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0007 | 0x4 | 0x2 | 14.835, 0.01, -241.367 | 0 | 0x15a2c0 |
| 30 | 0x08 | 0x52 | 0x00 | 0x0 | 0xff00 | 0x8 | 0xffff | 26.3, 0, -554.1 | 2.56389 | 0x1c1a80 |
| 31 | 0x08 | 0x52 | 0x00 | 0x0 | 0xff00 | 0x8 | 0xffff | 45.1, 0, -562.9 | -2.19039 | 0x1c1a80 |
<!-- area_overview:placements_sub1 end -->

Sub 1's deferred group (behaviour counts):

<!-- area_overview:group_records_other_sub begin -->
- sub1 deferred group 0x829440 (22): 0x219550 ×2, 0x15afa0 ×4, 0x826d40 ×3, 0x828850 ×3, 0x1383c0 ×2, 0x147390 ×4, 0x1bf6b0 ×2, 0x1c06e0 ×2
<!-- area_overview:group_records_other_sub end -->

Compared with sub 0, sub 1 has a plain 001BC350 door at the shaft bottom instead of 0x823580; no class-0x0B records, no 0x825350/0x825740/0x826CF0 records, no room-move door 2, no 0x1C50B0 and no 0x1E3D90 records; five crates instead of six; at the two high fixtures 0x826BA0 (z -502.3) and 0x1C4820 (z -337.9) instead of 0x1C4820 and 0x8267C0; two 0x1C1A80 records; and the sub-1-only group owners (read from the two placement tables above).

## 8. Overlay functions

Columns: "Slot bytes" = from the function's start to the next function's (padding included; the body sizes are in `overview.json`). "Decomp" = the class of the committed file at bdd40fb: C = ordinary C, NM = `// NEARMISS` (links from the original assembly), AI = hybrid asm, AU = no source (links from splat assembly). "Evidence" = the committed C compiled at bdd40fb (`--compile-check`) and resolved as the overlay link does, compared with the original bytes. "Route" = the function's entry was hit in the AREA01 route census (`a01_delta.json`, 12 beats, entries of all 41 functions armed; an absent entry is evidence of absence on that route only). "Reached by" is from the data (section 7), the scripts (section 9) and overlay calls; the lanes are section 11. At the beat-15 arrival the exit census also hit 0x823580 (f743, its entry) and addresses inside 0x823580, 0x8254B0 and 0x826D40 while overlay id 2 was resident (FIRST_LEVEL_EXIT.md section 5).

<!-- area_overview:overlay_functions begin -->
| Engine address | Splat piece(s) | Slot bytes | Decomp | Evidence | Route | Boot calls | Indirect calls | Reached by | Lane |
|---|---|---:|---|---|---|---:|---:|---|---|
| 0x00823540 | `00823500` | 64 | AI | — | — | 0 | 0 | nop sled, no code | - |
| 0x00823580 | `00823540` | 592 | AU | — | ran, 12 beats | 9 | 0 | sub0 place[n] | O5 |
| 0x008237d0 | `00823790` | 304 | C | compiled at rev: identical | — | 5 | 0 | group 0x82a900[n] | O2 |
| 0x00823900 | `008238C0` | 192 | C | compiled at rev: identical | — | 5 | 0 | group 0x82a900[n] | O2 |
| 0x008239c0 | `00823980` | 144 | C | compiled at rev: identical | — | 1 | 0 | group 0x82a900[n] | O2 |
| 0x00823a50 | `00823A10` | 64 | C | compiled at rev: identical | — | 0 | 0 | boot 0x1e7780 (area_dispatch_off0550_state0100) | C4 |
| 0x00823a90 | `00823A50`, `00823A90` | 576 | NM | — | — | 6 | 0 | call from 0x8240e0 | O3 |
| 0x00823cd0 | `00823C90` | 1040 | AU | — | — | 16 | 2 | sub0 deferred group 0x828a00[n] ×5 | O3 |
| 0x008240e0 | `008240A0`, `008240E0` | 608 | C | compiled at rev: identical | — | 6 | 0 | call from 0x823cd0 | O3 |
| 0x00824340 | `00824300`, `00824340` | 1072 | AU | — | — | 7 | 0 | call from 0x823cd0 | O3 |
| 0x00824770 | `00824730`, `00824770` | 1504 | NM | — | — | 12 | 0 | call from 0x823cd0 | O3 |
| 0x00824d50 | `00824D10`, `00824D50` | 544 | C | compiled at rev: identical | — | 13 | 0 | call from 0x823cd0 | O3 |
| 0x00824f70 | `00824F30`, `00824F70` | 112 | C | compiled at rev: identical | — | 1 | 0 | call from 0x824340 | O3 |
| 0x00824fe0 | `00824FA0`, `00824FE0` | 96 | C | compiled at rev: identical | — | 2 | 0 | call from 0x824340 | O3 |
| 0x00825040 | `00825000`, `00825040` | 240 | C | compiled at rev: identical | — | 2 | 0 | call from 0x824340 | O3 |
| 0x00825130 | `008250F0` | 272 | C | compiled at rev: identical | ran, 1 beat | 4 | 0 | script 0x829fa0 op09 record 0x82a020; script 0x829fa0 op09 record 0x82a420 | O1 |
| 0x00825240 | `00825200` | 272 | C | compiled at rev: identical | ran, 1 beat | 3 | 0 | script 0x829fa0 op09 record 0x82a3a0; script 0x829fa0 op09 record 0x82a4e0 | O1 |
| 0x00825350 | `00825310` | 352 | C | compiled at rev: identical | ran, 12 beats | 9 | 1 | sub0 place[n] | O1 |
| 0x008254b0 | `00825470`, `008254B0` | 224 | C | compiled at rev: identical | ran, 8 beats | 3 | 0 | call from 0x825350 | O1 |
| 0x00825590 | `00825550`, `00825590` | 224 | C | compiled at rev: identical | ran, 3 beats | 4 | 0 | call from 0x825350 | O1 |
| 0x00825670 | `00825630`, `00825670` | 208 | C | compiled at rev: identical | ran, 3 beats | 3 | 0 | call from 0x825350 | O1 |
| 0x00825740 | `00825700` | 464 | C | compiled at rev: identical | — | 13 | 1 | sub0 place[n] | O1 |
| 0x00825910 | `008258D0` | 64 | C | compiled at rev: identical | — | 0 | 0 | script 0x82ac10 op09 record 0x82acd0 | O2 |
| 0x00825950 | `00825910` | 656 | C | compiled at rev: identical | — | 5 | 0 | sub0 deferred group 0x828a00[n] ×2 | O2 |
| 0x00825be0 | `00825BA0`, `00825BE0` | 336 | C | compiled at rev: identical | — | 7 | 1 | call from 0x825950 | O2 |
| 0x00825d30 | `00825CF0`, `00825D30` | 368 | C | compiled at rev: identical | — | 6 | 1 | call from 0x825950 | O2 |
| 0x00825ea0 | `00825E60`, `00825EA0` | 96 | C | compiled at rev: identical | — | 2 | 1 | call from 0x825950 | O2 |
| 0x00825f00 | `00825EC0`, `00825F00` | 192 | C | compiled at rev: identical | — | 6 | 0 | call from 0x825950 | O2 |
| 0x00825fc0 | `00825F80`, `00825FC0` | 80 | C | compiled at rev: identical | — | 3 | 1 | call from 0x825950 | O2 |
| 0x00826010 | `00825FD0`, `00826010` | 400 | C | compiled at rev: identical | — | 8 | 0 | call from 0x825be0; call from 0x825d30 | O2 |
| 0x008261a0 | `00826160` | 96 | C | compiled at rev: identical | ran, 12 beats | 0 | 0 | sub0 place[n] ×2; sub1 place[n] ×2 | O1 |
| 0x00826200 | `008261C0`, `00826200` | 576 | C | compiled at rev: identical | ran, 12 beats | 10 | 1 | call from 0x8261a0 | O1 |
| 0x00826440 | `00826400`, `00826440` | 896 | C | compiled at rev: identical | ran, 12 beats | 10 | 1 | call from 0x8261a0 | O1 |
| 0x008267c0 | `00826780` | 400 | C | compiled at rev: identical | ran, 12 beats | 8 | 1 | sub0 place[n] | O1 |
| 0x00826950 | `00826910` | 592 | C | compiled at rev: identical | — | 6 | 0 | script 0x82b590 op09 record 0x82b650 | O1 |
| 0x00826ba0 | `00826B60` | 336 | C | compiled at rev: identical | — | 9 | 1 | sub1 place[n] | S1 |
| 0x00826cf0 | `00826CB0` | 80 | C | compiled at rev: identical | ran, 12 beats | 2 | 0 | sub0 deferred group 0x828a00[n]; sub0 place[n] ×2 | O1 |
| 0x00826d40 | `00826D00` | 5552 | AU | — | ran, 12 beats | 21 | 6 | sub0 deferred group 0x828a00[n] ×3; sub1 deferred group 0x829440[n] ×3 | O4 |
| 0x008282f0 | `008282B0`, `008282F0` | 1232 | NM | — | — | 11 | 0 | call from 0x826d40 | O4 |
| 0x008287c0 | `00828780`, `008287C0` | 144 | C | compiled at rev: identical | — | 4 | 0 | call from 0x826d40 | O4 |
| 0x00828850 | `00828810` | 432 | C | compiled at rev: identical | ran, 12 beats | 8 | 2 | sub0 deferred group 0x828a00[n] ×3; sub1 deferred group 0x829440[n] ×3 | O4 |

Totals (41 functions, 21,696 slot bytes, decomp bdd40fb): C 33 / 10,064 B, AU 4 / 8,256 B, NM 3 / 3,312 B, AI 1 / 64 B.
<!-- area_overview:overlay_functions end -->

Absolute %hi/%lo references of the overlay code (gp-relative globals are not symbolized in the splat pieces and are not listed; the init's D_00275C18..2C stores of section 2 are such):

<!-- area_overview:global_refs begin -->
| Address (%hi/%lo symbol) | Referenced by |
|---|---|
| 0x1c5680 | 0x826d40 |
| 0x1f5040 | 0x8237d0, 0x8287c0 |
| 0x2758c8 | 0x823cd0 |
| 0x8102b0 | 0x826440, 0x826950 |
| 0x810350 | 0x825130, 0x825be0, 0x826200, 0x826440, 0x8267c0 |
| 0x810360 | 0x8282f0 |
| 0x8105e0 | 0x825be0, 0x825d30 |
| scratchpad 0x70003000..0x70003910 (16 addresses) | 9 functions |
<!-- area_overview:global_refs end -->

D_008102B0 is the player block and D_008105E0 the camera's actual target per FINDINGS s56 (claims); 0x1C5680 (the indicator child) and 0x1F5040 are code pointers.

## 9. Scripts

Found by walking every data address the overlay code or data references as a 0x40-byte chain (word 0: bit 31 stop, bit 30 jump to word 1, op = word 0 & 0xFFF ≤ 0x1A, sub = word 2); a candidate with any other flag bit or op is rejected. Ops are written op/sub. The last column is a **dated snapshot**: the opcodes come from the execute() switch of the port's `em_area_script.c` at b7868e1, the sub rules from the hand-read snapshot in `ADMISSION_SNAPSHOT` (same file, same commit). The tool flags the rules stale when the committed file changes; the port's first-level chain is still extending this host (b7868e1 itself admitted op0B subs 0 and 6).

<!-- area_overview:scripts begin -->
| Entry | Records | Started by | Ops | Not admitted by the port host |
|---|---:|---|---|---|
| 0x829860 | 2 | 0x823580 | 0D/3 07/4 | — |
| 0x8298e0 | 19 | 0x823580 | 07/8 00/0 0A/0 05/8 02/0 12/0 0A/0 0A/3 0C/1 00/1 02/0 0A/0 0A/3 0A/0 0C/2 18/0 00/0 0A/0 07/4 | 05/8, 12/0 |
| 0x829e60 | 5 | 0x8254b0 | 07/8 15/0 18/0 04/1 07/4 | — |
| 0x829fa0 | 27 | 0x825590 | 07/8 00/0 09/0 0B/0 0A/0 0A/3 0A/0 0C/1 02/0 0A/0 02/0 00/5 02/0 00/0 02/0 00/0 09/0 02/0 09/0 0B/0 0C/2 09/0 0A/0 0A/3 18/0 04/1 07/4 | — |
| 0x82a660 | 5 | 0x825670 | 07/8 15/56 18/0 04/1 07/4 | — |
| 0x82a7b0 | 5 | 0x825740 | 07/8 15/72 18/0 04/1 07/4 | — |
| 0x82aa90 | 6 | 0x825be0 | 16/0 07/7 01/9 00/0 02/0 00/0 | — |
| 0x82ac10 | 4 | 0x825d30 | 00/0 0B/4 0C/1 09/0 | — |
| 0x82ad10 | 2 | 0x825d30 | 10/3 0F/0 | — |
| 0x82ad90 | 12 | 0x825f00 | 1A/0 13/1 0C/1 0A/1 14/0 00/6 10/2 0D/0 18/0 01/9 0D/2 07/5 | 13/1, 14/0, 1A/0 |
| 0x82b0d0 | 16 | 0x826440 | 07/7 0A/8 0A/4 00/7 06/3 0C/1 00/7 00/5 12/0 17/0 02/0 0A/3 06/3 0A/2 0D/4 07/5 | 0A/8, 12/0, 17/0 |
| 0x82b4d0 | 3 | 0x826200 | 02/0 05/0 02/0 | 05/0 |
| 0x82b590 | 21 | 0x8267c0 | 16/0 07/8 00/0 09/0 0A/0 0A/3 0C/1 00/1 02/0 0A/0 00/0 00/1 02/0 01/9 00/0 0A/0 0A/3 18/0 01/9 0D/2 07/5 | — |
| 0x82bad0 | 10 | 0x826ba0 | 07/12 0C/1 0A/1 00/6 0D/0 18/0 0A/5 01/9 00/0 07/5 | — |

Port column: port commit b7868e1 (2026-09-25 19:52), `src/game/em_area_script.c`. Admitted opcodes (its execute() switch): 00 01 02 04 06 07 09 0A 0B 0C 0D 0F 10 15 16 18. Sub rules (dated snapshot of the file at b7868e1, 2026-09-25; current at this commit): op00 kinds 0-7, 9, 10 (kind 8 and larger kinds fault); op01 kinds 0-7, 9, 10 (kind 8 and larger kinds fault); op0A: sub 8 (001798D0) faults; op0B: subs 0, 4 and 6 (other subs fault at 001B8020).
<!-- area_overview:scripts end -->

Script callbacks (op09 record word +4): 0x825130 and 0x825240 (in 0x829FA0, twice each), 0x825910 (0x82AC10), 0x826950 (0x82B590); section 8 "Reached by". On the recorded route (SECOND_LEVEL_ROUTE.md section 2): 0x829E60 runs at the first NPC conversation, 0x829860 and 0x8298E0 at the locked shaft door, 0x829FA0 at the second NPC conversation (it ends with D_00810759 = 0xFF and D_008107D9 = 0x81); 0x82A660 is the NPC's third branch (not played). FINDINGS s69 labels these as crank and bridge scripts (claims, contradicted for 0x829E60/0x829FA0 by the route). New script-host work for AREA01 at b7868e1: the ops in the last column (05, 12, 13, 14, 17, 1A and op0A sub 8; 001BAC00 for op14 is translated and verified-unbound in the port census at b7868e1, `em_script_door_fan`).

## 10. Boot owner behaviours

Every boot function an AREA01 table or overlay code points at. "Reach" = boot functions outside the first-level census reachable from it (section 11 explains the method). "First-level census row" = the port's row at b7868e1 (Port status and module, whole). The last column quotes labels from decomp comments or FINDINGS: claims, unverified.

<!-- area_overview:boot_roots begin -->
| Behaviour | Bytes | Decomp | Subsystem | Tables | Live (RAM) | First-level census row | Reach | Label (claim, unverified) |
|---|---:|---|---|---|---:|---|---:|---|
| 0x128c10 | 2924 | NM | lowmem | sub0 deferred group 0x828a00 ×12 | 6 | no | 84 | NPC update (decomp SEMANTICS); FINDINGS: bug brain |
| 0x12a5d0 | 2032 | BM | lowmem | sub0 deferred group 0x829220 ×6; nest group link 0 0x829360 ×4 | 0 | no | 104 | FINDINGS: bug brain (nest child) |
| 0x1383c0 | 376 | NM | entity_logic | sub1 deferred group 0x829440 ×2 | 0 | no | 108 | — |
| 0x147390 | 528 | NM | entity_logic | sub1 deferred group 0x829440 ×4 | 0 | no | 128 | entity dispatcher (decomp comment) |
| 0x1551b0 | 5220 | NM | entity_logic | sub0 place ×6; sub1 place ×5 | 6 | live (em_crate_original over its roster node (em_area11_boxes.c)) | 41 | crate (port em_crate_original) |
| 0x156620 | 2320 | NM | entity_logic | sub0 place ×4; sub1 place ×4 | 4 | live (em_drum_original over its roster node (em_area11_boxes.c)) | 50 | drum (port em_drum_original) |
| 0x158d30 | 388 | NM | entity_logic | sub0 place ×1; sub1 place ×1 | 1 | no | 35 | FINDINGS s74: creature-family fixture |
| 0x159b90 | 724 | NM | entity_logic | sub0 place ×1; sub1 place ×1 | 1 | no | 42 | FINDINGS s74: creature-family fixture |
| 0x15a2c0 | 1164 | NM | entity_logic | sub0 place ×8; sub1 place ×8 | 8 | no | 62 | port em_enemy.c names it |
| 0x15afa0 | 140 | BM | entity_logic | sub0 deferred group 0x828a00 ×8; group 0x8291c0 ×1; sub1 deferred group 0x829440 ×4 | 7 | live (em_pickup_owner em_pickup_owner_tick via em_pickup.c and the AREA11 interaction host) | 37 | item pickup (port em_pickup_owner) |
| 0x1bb860 | 628 | NM | math_vector | sub0 place ×2; sub1 place ×2 | 2 | no | 42 | slider door (FINDINGS s45/s63) |
| 0x1bc350 | 516 | BM | math_vector | sub0 place ×4; sub1 place ×4 | 4 | live (em_door_original em_door_original_tick via em_area11_door (node tick_door; census L18)) | 38 | hinged door (port em_door_original) |
| 0x1bf6b0 | 2268 | BM | math_vector | sub1 deferred group 0x829440 ×2 | 0 | no | 119 | actor update (decomp SEMANTICS) |
| 0x1c02e0 | 1016 | NM | math_vector | sub0 deferred group 0x828a00 ×1 | 1 | no | 120 | actor state machine, companion of 001BFFD0 (decomp) |
| 0x1c06e0 | 2380 | NM | anim_runtime | sub1 deferred group 0x829440 ×2 | 0 | no | 51 | decomp name bone_root_pulse |
| 0x1c1a80 | 636 | NM | math_vector | sub1 place ×2 | 0 | no | 44 | actor state machine with bone array (decomp) |
| 0x1c2420 | 8 | BM | math_vector | sub0 place ×2 | 0 | no | 1 | class-0x0B trigger record (8-byte leaf) |
| 0x1c4820 | 156 | BM | math_vector | sub0 place ×2; sub1 place ×1 | 2 | verified-unbound (em_status_ui_leftovers em_sul_001C4820) | 33 | generic placed prop (port em_status_ui_leftovers) |
| 0x1c50b0 | 1212 | BM | math_vector | sub0 place ×1 | 0 | no | 36 | flicker light (decomp comment) |
| 0x1c5680 | 224 | AI | math_vector | code pointer in 0x826d40 ×1 | 6 | live (em_indicator_child em_indicator_child_step, per node (em_area11_bindings.c tick_indicator)) | 31 | indicator child (port em_indicator_child) |
| 0x1e3d90 | 2160 | NM | weapon_equip | sub0 deferred group 0x828a00 ×3; sub0 place ×14 | 17 | no | 15 | muzzle-flash driver (decomp comment; doubtful for 17 scattered placements) |
| 0x1e7d20 | 3608 | NM | stream_archive | sub0 place ×1; sub1 place ×1 | 1 | no | 4 | water surface over D_00275C20 records (decomp comment) |
| 0x1f5040 | 1104 | NM | fx_render | code pointer in 0x8237d0 ×1; code pointer in 0x8287c0 ×1 | 0 | no | 38 | fx_render (port em_weapon.c names it) |
| 0x219550 | 796 | NM | unknown_06 | sub0 deferred group 0x828a00 ×2; sub1 deferred group 0x829440 ×2 | 2 | live (em_pickup_owner em_pickup_owner_tick via em_pickup.c and the AREA11 interaction host) | 37 | item pickup (port em_pickup_owner) |
<!-- area_overview:boot_roots end -->

## 11. Static census delta and proposed lanes

**Method.** Roots: every AREA01 overlay function (its direct boot calls and code-pointer references) and every boot behaviour of section 10. Closure over the decomp's splat tree of the boot ELF: direct calls, tail jumps and code-pointer references (a %hi/%lo symbol that is a function start). Delta = closure minus the first-level census (`route_functions.json`, 1184 functions: startup + beats 00..14). Limits: indirect calls (20 in the overlay, many in the boot code) are not followed, so the set can miss functions; pointer references can add functions a given owner never calls. The route census check in the block below counts the other direction: boot functions the AREA01 route ran for the first time that the static reach does not contain (the route also covers the AREA00 load and arrival, and engine paths and indirect calls that the closure does not follow).

**Lanes.** A delta function belongs to the lane of its owners (the direct-call owners when there are any); functions whose owners span lanes form lane E, and functions reached only from sub-1 owners lane S1. The owner-to-lane map is `LANES` in the tool. Lane titles name addresses and records only; labels from FINDINGS or comments are listed separately as claims.

<!-- area_overview:lanes begin -->
Reach 609 boot functions (534 by direct calls only), 324 already in the first-level census, **delta 285 functions / 154,976 bytes** (240 by direct calls only). Sub 0 and the nest reach 184 of them (90,752 bytes). Decomp status of the delta at bdd40fb: BM 155, NM 81, AI 23, AW 21, CL 4, CN 1. Beat-15 exit census: 50 of its 63 new functions are in the static reach. AREA01 route census (build/s87/census/a01_delta.json): 62 of the delta ran on the recorded route; 78 boot functions it records as new are outside the static reach.

| Lane | Scope (neutral) | Overlay functions (n / slot bytes; statuses) | Boot delta (n / bytes) | Delta that ran at the arrival (beat 15) | Delta that ran on the route | Delta statuses |
|---|---|---|---|---:|---:|---|
| O1 | NPC 0x825350 (placement [36]), 0x825740 ([38]), 0x826CF0, and the 0x8261A0 / 0x8267C0 owners (sub 0) | 13 / 4,656 B; C 13 | 7 / 2,548 | 2 | 0 | BM 5, AI 1, NM 1 |
| O2 | Owner 0x825950 and its spawns (sub 0) | 11 / 2,832 B; C 11 | 6 / 2,600 | 0 | 0 | NM 3, BM 2, AI 1 |
| O3 | Owner 0x823CD0 family (sub 0) | 9 / 5,792 B; C 5, NM 2, AU 2 | 0 / 0 | 0 | 0 | — |
| O4 | Owner pair 0x826D40 / 0x828850 (both subs) | 4 / 7,360 B; C 2, AU 1, NM 1 | 2 / 1,176 | 0 | 0 | AW 1, NM 1 |
| O5 | Scripted shaft door 0x823580 (placement [12], sub 0) | 1 / 592 B; AU 1 | 0 / 0 | 0 | 0 | — |
| C1 | Boot owners 0x15A2C0, 0x128C10, 0x12A5D0, 0x158D30, 0x159B90, 0x1C02E0, 0x1BFFD0 (sub 0) | — | 48 / 34,448 | 13 | 18 | BM 27, NM 14, AW 5, AI 1, CL 1 |
| C2 | 0x1E3D90 placements | — | 5 / 3,036 | 4 | 5 | NM 3, BM 2 |
| C3 | Slider doors 0x1BB860 and the class-0x0B records 0x1C2420 | — | 6 / 1,448 | 3 | 3 | BM 5, NM 1 |
| C4 | 0x1E7D20 (placement [35]) and the overlay init 0x823A50 | 1 / 64 B; C 1 | 3 / 3,776 | 2 | 2 | BM 2, NM 1 |
| C0 | First-level census owners' unexercised paths | — | 1 / 704 | 0 | 1 | BM 1 |
| S1 | Sub-1 owners (later) | 1 / 336 B; C 1 | 108 / 68,540 | 0 | 3 | BM 54, NM 34, AW 10, AI 9, CN 1 |
| E | Shared engine delta (reached from more than one lane) and the code pointer 0x1F5040 | — | 99 / 36,700 | 26 | 30 | BM 57, NM 23, AI 11, AW 5, CL 3 |
<!-- area_overview:lanes end -->

Claims attached to the lanes (unverified):

<!-- area_overview:lane_claims begin -->
- O1: FINDINGS s69/s74 and the retired drawbridge export call [36] a crank, 0x8261A0 the bridge halves and 0x8267C0 a suspension fixture; the AREA01 route capture shows [36] is the control-room NPC (SECOND_LEVEL_ROUTE.md section 2) and no bridge moved on the route.
- O4: SECOND_LEVEL_ROUTE.md guesses the 0x826D40 nodes are sentry guns (from a data page and their wall positions; not observed).
- C1: FINDINGS calls 0x128C10/0x12A5D0 bug brains and 0x158D30/0x159B90 creature-family fixtures.
- C2: decomp comment: muzzle-flash driver (doubtful for 17 scattered placements).
- C3: FINDINGS s45/s63: slider door.
- C4: decomp comment: water surface over the D_00275C20 records.
<!-- area_overview:lane_claims end -->

Lane notes (manual):

- **O1.** Placement [36] 0x825350 (+0x8254B0/0x825590/0x825670, the story-byte branches; callbacks 0x825130/0x825240 from script 0x829FA0), [38] 0x825740 (not spawned on the route), 0x826CF0 ([37], [39] and a group record), the two 0x8261A0 owners ([41], [42]; +0x826200/0x826440, both subs; no state change on the route) and 0x8267C0 ([45]; +0x826950); scripts 0x829E60, 0x829FA0, 0x82A660, 0x82A7B0, 0x82B0D0, 0x82B4D0, 0x82B590; boot 0x1C50B0 ([40]). All 13 overlay functions are byte-identical C at bdd40fb; 11 of them ran on the route (not 0x825740 and 0x826950).
- **O2.** 0x825950 (two cond-3 group records, not live at f801) and its callees, the op09 callback 0x825910 and the op14-spawned 0x8237D0/0x823900/0x8239C0; scripts 0x82AA90, 0x82AC10, 0x82AD10, 0x82AD90; spawns groups 0x829220 and 0x8291C0. All C; none ran on the route.
- **O3.** 0x823CD0 for group records 0x828A00[0..4] (cond 3, not live at f801) and its callees; heavy scratchpad use; reads D_002758C8. No delta of its own (its boot calls are shared, lane E). Two AU jr-table dispatchers (0x823CD0, 0x824340) and two NM remain.
- **O4.** 0x826D40 (5,552 bytes, the largest, AU; +0x8282F0 NM, 0x8287C0 C) and 0x828850 (C): three record pairs live at f801 (slots 9..14); both entry functions ran in all 12 route beats.
- **O5.** The shaft door 0x823580 ([12], door id 0, area change → area 0 entry 0; AU jr-table dispatcher); scripts 0x829860 and 0x8298E0 (05/8 and 12/0 not admitted). It ran in all 12 route beats and is the level exit. Door transit workers are census functions (001BBE40/001BC150).
- **C0.** Paths of first-level census owners (crate, drum, door, pickups, prop, indicator) that the first level never exercised.
- **S1.** Only reachable through the fan's direct exit after Roger's departure (FIRST_LEVEL_EXIT.md section 4); not on the main route.

Delta functions per lane (address, bytes, status at bdd40fb; "arrival" = ran in beat 15 per the exit census; "route" = ran on the AREA01 route per `a01_delta.json`; "port" = the port's `src/game` at b7868e1 names the address, grep only):

<!-- area_overview:lane_deltas begin -->
- **O1** (7): 1BA7F0 (236, BM, port), 1C4FA0 (172, BM, arrival), 1C5050 (88, AI), 1C50B0 (1212, BM, arrival), 1F5490 (68, BM), 1F5F60 (684, NM), 1FC520 (88, BM, port).
- **O2** (6): 19C6F0 (320, NM), 1B0CD0 (164, BM), 1CA3B0 (280, AI), 1F2F90 (940, BM), 1F3340 (728, NM), 1F4010 (168, NM, port).
- **O4** (2): 11C128 (924, AW), 11E520 (252, NM).
- **C1** (48): 128600 (64, BM, route), 128640 (420, BM, route), 128B80 (132, NM, arrival, route), 128C10 (2924, NM, arrival, route), 129F00 (184, BM), 129FC0 (1540, BM), 12A5D0 (2032, BM, route), 12ADC0 (508, NM, route), 12AFC0 (1092, BM, route, port), 12B410 (1076, NM, port), 12B850 (280, AW, port), 12B970 (1188, BM, port), 12BE20 (1644, BM, port), 12C490 (1540, BM, port), 12CAA0 (1952, BM, port), 12D240 (828, NM, port), 12D850 (232, AW, port), 12D940 (1064, BM, port), 12DD70 (288, AI, port), 12E070 (60, CL, port), 12E0B0 (420, BM, port), 12E260 (84, BM), 12E2C0 (224, BM), 153ED0 (56, BM), 153F10 (292, AW), 154040 (212, BM), 154120 (832, NM), 154460 (324, BM), 1545B0 (264, BM), 1546C0 (124, BM), 154740 (628, BM), 1549C0 (1344, BM), 154F00 (676, AW), 157CE0 (592, BM, arrival, route), 158590 (632, NM, arrival, route), 158D30 (388, NM, arrival, route), 159B90 (724, NM, arrival, route), 15A200 (184, BM), 15A2C0 (1164, NM, arrival, route, port), 15A750 (928, NM), 1C02E0 (1016, NM, arrival, route, port), 1C24D0 (104, BM), 1C25E0 (168, BM, arrival, route), 1E9580 (2264, BM, arrival, route), 1E9E60 (932, NM, arrival, route, port), 1F4A10 (480, NM, arrival, route), 1F4CC0 (120, BM, arrival, route), 21BD60 (224, AW).
- **C2** (5): 1CD070 (272, NM, arrival, route), 1CD180 (304, NM, arrival, route), 1CD2B0 (192, BM, arrival, route), 1E3D20 (108, BM, route), 1E3D90 (2160, NM, arrival, route, port).
- **C3** (6): 1BB520 (56, BM, arrival, route), 1BB560 (608, BM, arrival, route), 1BB7C0 (40, BM), 1BB7F0 (108, BM), 1BB860 (628, NM, arrival, route, port), 1C2420 (8, BM).
- **C4** (3): 1E7C60 (68, BM), 1E7CB0 (100, BM, arrival, route), 1E7D20 (3608, NM, arrival, route).
- **C0** (1): 1F0460 (704, BM, route, port).
- **S1** (108): 11BCF8 (1072, AW), 11E0A8 (156, AW), 11E420 (252, NM), 1284E0 (276, BM), 131ED0 (72, BM), 1383C0 (376, NM), 138540 (408, BM), 1386E0 (532, BM), 138900 (788, BM), 138C20 (1560, BM), 139240 (1960, NM), 1399F0 (1040, NM), 139E00 (1444, NM), 13A3B0 (3992, BM), 13B350 (204, AW), 13B420 (392, BM), 13B5B0 (476, BM), 13B790 (516, BM), 13B9A0 (128, BM), 13BA20 (396, BM), 13BBB0 (688, AW), 13BE60 (192, AI), 13BF20 (716, NM), 13C1F0 (720, BM), 13C4C0 (1016, BM), 13C8C0 (1160, NM), 13CD50 (1224, NM), 13D220 (164, BM), 147390 (528, NM), 1475A0 (348, BM), 147700 (600, BM), 147960 (484, BM), 147B50 (1492, NM), 148130 (720, BM), 148400 (280, BM), 148520 (440, BM), 1486E0 (1112, NM), 148B40 (4112, NM), 149B50 (1396, BM), 14A0D0 (628, NM), 14A350 (708, BM), 14A620 (272, NM), 14A730 (612, NM), 14A9A0 (792, BM), 14ACC0 (392, BM), 14AE50 (48, BM), 14AE80 (276, BM), 14AFA0 (2056, CN), 14B7B0 (856, NM), 14BB10 (288, BM), 14BC30 (488, NM), 14BE20 (648, BM), 14C0B0 (124, AW), 14C130 (240, BM), 17C370 (208, AI, route, port), 183010 (116, BM), 187EC0 (32, BM, route), 19A440 (292, NM), 19A6F0 (532, NM), 19B2C0 (512, NM, port), 19DB50 (960, NM), 1A1B80 (2032, NM), 1A56A0 (180, AW, port), 1A58B0 (884, AW, port), 1A7B80 (20, BM), 1A7BA0 (2748, NM), 1B1270 (60, BM), 1B1560 (108, BM), 1B2B10 (112, BM), 1B2B80 (100, BM), 1B2BF0 (260, AW), 1B2D00 (324, BM), 1B2E50 (288, NM), 1B2F70 (368, NM), 1B30E0 (360, NM), 1B3250 (156, AW), 1B32F0 (160, NM), 1B3390 (172, BM), 1B3440 (164, NM), 1B34F0 (140, BM), 1B3580 (228, BM), 1B37D0 (536, NM), 1B39F0 (564, BM), 1B3C30 (736, BM), 1B3F10 (732, NM), 1B4810 (1240, BM), 1B4CF0 (1640, BM), 1B55E0 (424, NM), 1BE5F0 (196, AI), 1BEAC0 (168, BM), 1BF5B0 (128, BM), 1BF6B0 (2268, BM), 1C06E0 (2380, NM), 1C1500 (108, BM), 1C1570 (1284, NM), 1C1A80 (636, NM), 1C6910 (68, BM), 1C81C0 (220, BM), 1C82A0 (476, BM), 1C9570 (152, AI), 1EFEB0 (84, BM, port), 1EFF10 (192, AI, port), 1FB0B0 (16, BM, port), 21BC40 (200, AW, route, port), 21BE40 (144, AI), 21BED0 (184, AI), 21BF90 (176, AI), 21C040 (212, AI).
- **E** (99): 1000C0 (32, BM, port), 100110 (32, BM, port), 102870 (32, AI), 1028E8 (20, AI, port), 11CE20 (2380, NM, port), 11DE60 (48, AI, port), 11DF98 (228, AW, port), 11E148 (352, AI, port), 11E860 (20, BM, port), 128390 (52, CL, arrival, route), 1287F0 (56, CL, arrival, route), 128830 (152, BM), 1288D0 (228, BM), 1289C0 (240, BM, arrival, route), 128AB0 (204, BM, arrival, route), 129780 (1916, BM, arrival, route), 12D580 (708, BM, port), 12DE90 (480, NM), 19AA80 (156, NM), 19B4C0 (512, NM, arrival, route), 19CF50 (992, NM, arrival, route), 1A06A0 (1132, NM, arrival, route), 1A44B0 (412, AW, port), 1A4830 (1248, AW, port), 1A5C30 (2056, NM, port), 1A7280 (1520, NM, port), 1B0C00 (88, BM, arrival, route, port), 1B0D80 (60, BM, arrival, route, port), 1B13F0 (124, BM, arrival, route), 1B1C60 (56, BM, port), 1B1CE0 (56, BM, port), 1B2140 (2500, NM, arrival, route), 1B5360 (640, BM), 1BE6C0 (1012, NM), 1BF630 (124, AI, arrival, route), 1BFF90 (64, BM), 1BFFD0 (52, NM, arrival, route), 1C2430 (148, BM, route), 1C2540 (152, AW, arrival, route), 1C2690 (216, BM), 1C2770 (2176, NM, arrival, route, port), 1C39F0 (488, BM, arrival, route), 1C3BE0 (384, BM, arrival, route), 1C3D60 (68, BM, arrival, route, port), 1C3DB0 (764, NM, arrival, route), 1C6160 (44, BM, route), 1C63D0 (16, BM), 1C69A0 (1016, NM, arrival, route, port), 1CACC0 (356, BM), 1CAE30 (8, BM), 1CAE40 (276, BM), 1CAF60 (8, BM), 1CAF70 (48, BM), 1CAFA0 (188, BM), 1CB060 (8, BM), 1CB070 (184, BM), 1CB130 (8, BM), 1CB140 (164, BM), 1CB1F0 (8, BM), 1CB200 (164, BM), 1CB2B0 (8, BM), 1CB360 (84, BM, arrival, route), 1CB480 (112, AI, route), 1CB4F0 (144, AI, port), 1CB580 (8, BM, port), 1CD940 (1152, NM), 1D0C80 (188, BM, arrival), 1D0D40 (32, BM, arrival), 1D0D60 (444, AI), 1D39A0 (144, BM), 1D3A30 (144, BM), 1D3AC0 (12, BM), 1D3C40 (152, BM), 1D3CE0 (12, BM), 1D3DA0 (152, BM), 1D3F60 (356, NM), 1D40D0 (12, BM), 1D40E0 (504, NM), 1D42E0 (332, NM), 1D4430 (16, BM), 1D4440 (504, NM), 1D4640 (16, BM), 1D6580 (284, BM), 1D80E0 (32, BM, port), 1D8100 (36, BM, route, port), 1E2800 (928, NM), 1E2BA0 (728, NM, port), 1E8B90 (748, NM, route, port), 1EFE00 (172, AI, route, port), 1EFFD0 (136, AI), 1F4A00 (8, BM), 1F4BF0 (200, AI, arrival, route, port), 1F4E20 (20, BM), 1F4F40 (72, CL), 1F4F90 (176, BM), 1F5040 (1104, NM, port), 1F9180 (60, AW, port), 1FAD70 (244, BM, arrival, route, port), 1FC580 (348, BM, port).
<!-- area_overview:lane_deltas end -->

### Recommended order (sub 0 first, smallest risk to the first-level chain)

1. **Data exporters (no port `src/` change):** AREA01 roster (placements, groups, nest, spawn), the 14 scripts, the door table, the area-1 message table and bank, the area-1 sound cells and banks, and the `chunk05` + `chunk05.n0` world/collision/model-table export. New tools only, mirroring the AREA11 exporters, each checked byte for byte against this capture.
2. **Matching lane (decomp only):** the remaining overlay assembly: 0x826D40 (O4, runs every frame on the route), then the three jr-table dispatchers 0x823580 (O5), 0x823CD0 and 0x824340 (O3), which need the overlay link moved to load + 0x40 first (decomp PROGRESS.md 2026-09-25), and the three NEARMISS. The overlay build must stay 19/19 byte-identical (`tools/verify_all.py`).
3. **Translation lanes as new, unbound port modules** (fail-stop, oracle-tested against this capture, not wired until the area load exists): O5 and O1 (the route's story path: shaft door and NPC), O4, C1, C2, C3, C4, then O2/O3 (not live on the first visit) and E as the owners need it. S1 last.
4. **Wiring** (area-1 load in the native area read, the roster binder for area 1, the script-host ops) touches files the first-level chain owns (`em_scene_*`, `em_game*`, `em_area_script.c`) and waits until the lead schedules it.

## 12. Limits and open questions

- Static tables plus recorded census outputs; nothing here was executed in an oracle for this document. The arrival capture is one frame set (f741..f801) of one route.
- The census delta follows direct calls and code-pointer references only; indirect calls are not resolved (section 11).
- Boot statuses use the 2026-09-23 link-route audit with the markers of bdd40fb (section 0); a fresh `tools/decomp/build.py build` + audit would replace it.
- Record-field meanings beyond the spawner layouts (placement +0x00..+0x24, group +0x00..+0x28, spawn position/yaw/+0x1C) are not interpreted here: spawn +0x10/+0x14/+0x20, the op14 table 0x82A900 and the unreferenced group 0x829110 are open.
- The mapping of `chunk05` files to roles (world, collision, model table, text bank, sound banks) is from FINDINGS and needs the exporters' own checks.
- Whether placement [38] (0x825740) runs at all in this load is open (section 7.1).
