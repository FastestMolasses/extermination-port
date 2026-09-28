# AREA00 static overview (the level after AREA01)

Date: 2026-09-28 (session s88, lane "census"). Static analysis of the pinned boot ELF, the AREA00 overlay (`OVERLAY/AREA00.BIN`), the extracted `chunk04` blocks, the decomp's committed sources and splat trees, and the captured RAM of the AREA00 arrival (`../Extermination/build/s87/route_a01/a01_07_level_exit/eeMemory.bin`, the end snapshot of SECOND_LEVEL_ROUTE.md beat a01_07: area bytes 00 00 00 00, overlay id 1 resident, the player in control after the arrival script). Execution facts are quoted from recorded census outputs (the AREA01 exit replay's AREA00 phase, `a00_delta.json`); no emulator was run for this document. It lists addresses, sizes, counts, statuses and table values only: no original code, no disassembly and no on-screen text.

**Scope and isolation.** Preparation for the level after AREA01 (THIRD_LEVEL_ROUTE.md records how the original plays it). Nothing here changes the live port. Per the project rule, a label quoted from FINDINGS.md, a decomp comment or a name is a claim, not evidence, and is marked as such. The companion document for AREA01 is AREA01_OVERVIEW.md; this one uses the same tool and the same block names, so the two read side by side.

## 0. How this document is made

**One tree state per repository.** Every decomp status here is read from decomp commit **fa4b42c** (2026-09-28) through git, never from the working tree; every port fact (first-level census rows, the script host's admitted ops, the address grep) is read from port commit **52caca6** (2026-09-28 05:28). The decomp working tree did not equal fa4b42c for this run: other lanes had uncommitted AREA00 and AREA01 overlay sources in it (`overview.json` `trees`, `overlay_source`); those are not counted here.

**Reproduce** (decomp repo root, macOS arm64; `--compile-check` runs the committed overlay C through mwcc in the `exterm-toolchain` container, the rest is host Python):

```
.venv/bin/python tools/route_census.py a00-delta --passes A00 --a01-passes A01
.venv/bin/python tools/area_overview.py --area 0 \
    --ram build/s87/route_a01/a01_07_level_exit/eeMemory.bin \
    --exit-delta build/s87/census/a00_arrival.json --arrival-name "a01_07 from 001AD010" \
    --compile-check --doc ../extermination-port/docs/AREA00_OVERVIEW.md
# the same with --check-doc instead of --doc exits 1 when a block is stale
```

It writes `build/s87/area00/overview.json` and `build/s87/area00/tables.md` (both ignored). **Every table and every block between `<!-- area_overview:NAME -->` markers in this document is printed by the tool**; everything outside the markers is manual prose, and where prose states a number its source is named. The lane map of section 11 (`LANES[0]`), the claim labels of sections 10 and 11 (`CLAIMS[0]`, the lanes' `claim`) and the dated script-host admission snapshot of section 9 are constant tables inside `tools/area_overview.py`.

**Tool changes for this area (2026-09-28, additive; the AREA01 and AREA11 tables print byte for byte as before, checked by diffing `tables.md` of both areas against the previous tool).** (1) AREA00's subs 0 and 1 share one placement table and one deferred group, so records are labelled `sub0+1` (the old code labelled them with the last sub, which made every sub-0 owner look sub-1-only). (2) The static census delta of section 11 is measured against the first-level census **plus** the boot functions that ran before AREA00: beat 15 (the AREA11 exit and AREA01 arrival) and every AREA01 beat in its AREA01 phase (`a00_delta.json` `prior_boot`, 1291 functions). (3) The "arrival" columns name AREA00's own arrival (`--exit-delta` / `--arrival-name`): the AREA01 exit replay a01_07 from its area-change consumer 001AD010 (census f233) on, written by `route_census.py a00-delta` as `a00_arrival.json`.

Inputs besides the two commits: `build/s87/census/route_functions.json` (the first-level census), `runs/A/15_level_exit.json` (beat 15), `runs/A01/*.json` (the AREA01 census replays, 12 beats), `a00_delta.json` / `a00_arrival.json` (the AREA00 route census, below) and `provenance.json` (the boot link-route audit of 2026-09-23 13:08; 513 boot sources changed after it, 104 functions were renamed, none changed its marker class, checked per address).

**The AREA00 route census so far.** THIRD_LEVEL_ROUTE.md's six a00 beats have **not** been replayed under the census yet (this lane runs no emulator; the command is in section 12). The only execution data for AREA00 is therefore the AREA00 phase of the AREA01 exit replay (a01_07 f233..f4546: load, arrival script, first control). That replay armed the boot functions and the **AREA01** overlay's entries, so for AREA00 code it recorded only the points where an AREA01 entry address happens to fall inside an AREA00 function while overlay id 1 was resident (13 hits in 6 functions, plus one pause the replay logged as unexpected; `a00-delta` maps the hits by address range). A "ran" in the Route column of section 8 means such a point was executed; an empty cell means nothing, since no AREA00 entry was armed.

## 1. Summary

<!-- area_overview:summary begin -->
| Item | Value |
|---|---|
| Overlay | `AREA00.BIN`, MWo3 id 1, text 0x823540..0x826f80, data ..0x82d480, BSS 0x82d480..0xac2d80 |
| Sub-states | 3 (spawn descriptor entries); placement tables sub 0: 0x82bb50, sub 1: 0x82bb50, sub 2: 0x82c640 |
| Title (RAM) | D_00289B40[0] = 0x00030000; sub 0: D_002671C0[0] -> 0x2739d0, sub 1: D_002671C0[1] -> 0x2739d0, sub 2: D_002671C0[2] -> 0x2739d0 |
| Level data | INDEX sector 4: `chunk04` (7 files); `chunk04.n0` (7 files); `chunk04.n1` (7 files); `chunk04.n2` (16 files) |
| Overlay functions | 34 (from 36 splat pieces), 14,912 bytes; at decomp fa4b42c: AU 26, AI 4, C 4 |
| Sub 0 roster | 69 placement records; deferred groups 0x826f80 (61) |
| Sub 1 roster | 69 placement records; deferred groups 0x826f80 (61) |
| Sub 2 roster | 41 placement records; deferred groups 0x828060 (24), 0x827fb0 (3) |
| Nest groups | link 0: 0x827a90 (5), link 1: 0x827ba0 (2), link 2: 0x827c30 (3), link 3: 0x827ce0 (3), link 4: 0x827d90 (7) |
| Live pool (RAM) | 130 nodes, 34 behaviours |
| Scripts | 15 chains, started by 12 overlay functions |
| Doors | destination table 0x24df80, 8 records |
| BGM at the capture | lane 0 cue 13 (state 2), D_008106C8 = 0xd00 |
| Messages | D_00264DD0[1] = 0x26eeb0..0x26f060, 54 records |
| Static census delta | 160 boot functions (73,708 bytes) not in the first-level, beat-15 and AREA01 route censuses; 111 (47,424 bytes) from sub 0 / nest owners |
<!-- area_overview:summary end -->

Manual notes to the summary:

- **Arrival from AREA01:** sub 0 spawn entry 0 (the AREA01 shaft door's 001B0C60 request; SECOND_LEVEL_ROUTE.md a01_07, THIRD_LEVEL_ROUTE.md section 1). The capture has D_00810700..04 = 00 00 00 00 00 and BGM lane 0 on cue 13 (the summary's BGM row; spawn entry 0 +0x1C = 0x0D00, section 5).
- **Three sub-states, two rosters.** Subs 0 and 1 use the same placement table 0x82BB50 and the same deferred group 0x826F80 (registries table); they differ only in their spawn tables (0x24AA50 / 0x24ACC0), whose values are equal entry for entry (the spawn table's sub-0 and sub-1 columns). Sub 2 has its own table 0x82C640, its own deferred groups and cue 22 in every spawn record. What selects sub 1 or sub 2 was not found (THIRD_LEVEL_ROUTE.md section 2.1).
- **Title:** the three subs point D_002671C0[0..2] at the same string (Title row); the text itself is not reproduced here.
- **Decomp status at fa4b42c:** C 4 (0x8247C0, 0x824E00, 0x8260B0, 0x826CB0; each compiled with `--compile-check` and, relocated as the overlay link does, equal to the original bytes: section 8), AI 4 (the leading nop sled 0x823540 and the hybrid 0x824E40, 0x8253E0, 0x825E80), AU 26. Every placement owner of the route (the shaft door, door [51], the terminal, the ferry, the 0x825920 trio) is still AU at this commit; THIRD_LEVEL_ROUTE.md section 2 describes their behaviour from the original instructions.

## 2. Overlay, naming and residency

- The MWo3 header (0x40 bytes) loads with the file at 0x823500, so `.text` starts at 0x823540, and engine address = splat label + 0x40 (as for AREA01; AREA01_OVERVIEW.md section 2). All addresses here are engine addresses. The decomp's `tools/overlay/overlay_match.py` `true_functions` groups AREA00's 36 splat pieces into 34 functions: 0x825E80 and 0x825FC0 are each two pieces (an intra-overlay call target 0x40 into the function).
- The captured RAM holds the overlay: header id 1, text byte-identical to the disc file. Eight data words differ from the file at the capture, all between 0x828E70 and 0x829330, inside the records of the arrival script 0x828D60 (section 9) that had just run (`overview.json` `ram.data_words_changed`); every other table below is identical in the file and in the live game.
- Boot → overlay entry: the only boot call to an AREA00 function entry is 001E7780's area dispatch for key 0x0000 (`area_dispatch_off1900_state0000` = 0x824E00, a committed, byte-identical C function; `overview.json` `boot_calls_into_arena`). The mode-0 camera director's fixed hook 00195130 → 0x823FE0 lands inside 0x823EB0 (0x823EB0..0x824130), not on an entry, as it does inside an AREA01 function (AREA01_OVERVIEW.md section 2).
- Everything else is reached through data: placement and group behaviours, script op09 callbacks and overlay-internal calls (section 8). Eight functions (lane O6) have no reference the tool found; two of them (0x823EB0, 0x824130) are the behaviours of the unreferenced group 0x82CDD0 (section 7.2).

## 3. Level data the area loads

001FFCD0 reads INDEX.IDX sector `D_00810700 + 4` (= 4 for area 0). The method is AREA01_OVERVIEW.md section 3 (extracted blocks, the loaded descriptors from the capture, a residency sample of eight 64-byte windows per file).

<!-- area_overview:level_data begin -->
| Block | Files (index: id, bytes; resident windows found/sampled) |
|---|---|
| `chunk04` | f00: 41, 4,096 (6/6); f01: 96, 333,824 (7/7); f02: 97, 71,680 (7/7); f03: 98, 67,584 (7/7); f04: 99, 124,928 (7/7); f05: 9a, 88,064 (7/7); f06: 6b, 2,048 (7/7) |
| `chunk04.n0` | f00: 43, 1,183,744 (1/8); f01: 42, 137,216 (0/8); f02: 46, 61,440 (0/8); f03: 73, 4,096 (0/2); f04: 72, 139,264 (1/4); f05: 71, 495,616 (8/8); f06: 44, 5,398,528 (6/6) |
| `chunk04.n1` | f00: 43, 1,183,744 (1/8); f01: 42, 137,216 (0/8); f02: 46, 61,440 (0/8); f03: 73, 4,096 (0/2); f04: 72, 139,264 (1/4); f05: 71, 495,616 (8/8); f06: 44, 5,357,568 (1/5) |
| `chunk04.n2` | f00: 44, 2,170,880 (0/7); f01: 43, 724,992 (0/8); f02: 42, 280,576 (0/8); f03: 46, 55,296 (0/8); f04: 7a, 4,096 (0/8); f05: 81, 4,096 (0/8); f06: 9b, 6,144 (0/8); f07: 7c, 129,024 (0/8); f08: 84, 399,360 (0/8); f09: 79, 57,344 (0/8); f10: 7b, 57,344 (0/8); f11: 82, 165,888 (0/8); f12: 83, 194,560 (0/8); f13: 9c, 235,520 (0/8); f14: 9d, 180,224 (0/8); f15: 9e, 2,123,776 (0/6) |

| Descriptor | At | Disc offset | Size | Nested count | File count | Block |
|---|---|---|---|---:|---:|---|
| top (D_00289BC0) | 0x289bc0 | 0x460000 | 0xa9000 | 3 | 7 | `chunk04` |
| nested 0 (+0x100 + 0x70 * 0) | 0x289cc0 | 0x509000 | 0x713800 | 0 | 7 | `chunk04.n0` |
| nested 1 (+0x100 + 0x70 * 1) | 0x289d30 | 0xc1c800 | 0x709800 | 0 | 7 | `chunk04.n1` |
| nested 2 (+0x100 + 0x70 * 2) | 0x289da0 | 0x1326000 | 0x679800 | 0 | 16 | `chunk04.n2` |

D_00275C70 = 0x289cc0 (nested descriptor 0); load cursors D_0028A73C..48 = 0x1335f40, 0x13e0b80, 0x1980b80, 0x1980b80.
<!-- area_overview:level_data end -->

Reading of the block above: the top descriptor is `chunk04` with 3 nested blocks, one per sub-state; D_00275C70 points at nested descriptor 0 (`chunk04.n0`), the sub loaded at the capture. Every sampled `chunk04` window is resident, and so are `chunk04.n0` f05 (id 0x71) and f06 (id 0x44); n0 f00..f04 are almost never found, and `chunk04.n2` not at all. The n1 sample gives the same counts as n0 for f00..f05, which have the same ids and sizes in both blocks (whether their bytes are equal was not checked). By analogy with AREA01 (AREA01_ASSETS.md "Load map": the nested block's first files are upload-table entries consumed at load), the non-resident n0 files may be uploads, but this document does not establish that. File roles are not derived here; nothing in the port loads AREA00 data.

## 4. Registries, sub-states and title

<!-- area_overview:registries begin -->
| Registry | Area entry | Sub 0 | Sub 1 | Sub 2 |
|---|---|---|---|---|
| D_0024D7C0 placements (0x28-byte records, 0xFF end) | 0x82ccd0 | 0x82bb50 (69) | 0x82bb50 (69) | 0x82c640 (41) |
| D_0024D820 deferred groups (0x2C-byte records, -1 end) | 0x8284c0 | list 0x2758a0: 0x826f80 (61) | list 0x2758a0: 0x826f80 (61) | list 0x8284b0: 0x828060 (24), 0x827fb0 (3) |
| D_0024D820 nest slots (D_0024A850[0] = 3) | 0x8284c0 | [3] link 0: 0x827a90 (5), [4] link 1: 0x827ba0 (2), [5] link 2: 0x827c30 (3), [6] link 3: 0x827ce0 (3), [7] link 4: 0x827d90 (7) | | |
| D_0024D650 spawn entries (0x30-byte records) | 0x24d600 | 0x24aa50 (13) | 0x24acc0 (13) | 0x24af30 (13) |
| D_0024E140 door destinations (4 bytes) | 0x24df80 | 8 records, shared (ends at 0x24dfa0) | same | same |
| D_00264DD0[1] message line records (8 bytes) | 0x26eeb0 | 54 records (53 nonzero), shared, end 0x26f060 | same | same |
| D_00289B40 title base/count (RAM) | 0x00030000 | D_002671C0[0] | D_002671C0[1] | D_002671C0[2] |
| D_0026EC60 area music rows | 2 rows | trigger 49 → cue 45; trigger 50 → cue 47 |  |  |
<!-- area_overview:registries end -->

- D_0024A850[0] = 3, so the nest slots start at D_0024D820[0][3]: five nest groups (links 0..4), all of 0x128C10 / 0x12A5D0 records (section 7.2).
- Area music rows (D_0026EC60): trigger 49 → cue 45 and trigger 50 → cue 47; what raises those triggers was not checked (the capture plays cue 13).

## 5. Spawn entries, doors and exits

Spawn records: word +0x1C is copied to D_008106C8 by 001B0250; bits 8..14 are the BGM cue (AREA01_OVERVIEW.md section 5).

<!-- area_overview:spawn begin -->
| Entry | Position | Yaw | +0x10 | +0x14 | +0x1C sub 0 | +0x1C sub 1 | +0x1C sub 2 | +0x20 |
|---:|---|---:|---|---|---|---|---|---|
| 0 | -40, -35, -1290 | 3.14159 | 0x0 | 1 | 0x0d00 (cue 13) | 0x0d00 (cue 13) | 0x1600 (cue 22) | 0x044e3999 (subs differ elsewhere) |
| 1 | -55, -60, -1492 | -1.5708 | 0x0 | 1 | 0x8d00 (cue 13) | 0x8d00 (cue 13) | 0x9600 (cue 22) | 0x044e3999 (subs differ elsewhere) |
| 2 | -36, -60, -1492 | 1.5708 | 0x0 | 0 | 0x8d00 (cue 13) | 0x8d00 (cue 13) | 0x9600 (cue 22) | 0x044e3999 (subs differ elsewhere) |
| 3 | 222, -60, -1686 | 1.5708 | 0x80 | 0 | 0x8d01 (cue 13) | 0x8d01 (cue 13) | 0x9601 (cue 22) | 0x044e0ccc (subs differ elsewhere) |
| 4 | 208, -60, -1686 | -1.5708 | 0x0 | 1 | 0x8d00 (cue 13) | 0x8d00 (cue 13) | 0x9600 (cue 22) | 0x044e3999 (subs differ elsewhere) |
| 5 | 183, -60, -1438 | 0 | 0x0 | 0 | 0x8d01 (cue 13) | 0x8d01 (cue 13) | 0x9601 (cue 22) | 0x044e3999 (subs differ elsewhere) |
| 6 | 118, -60, -1445 | 0 | 0x0 | 0 | 0x8d01 (cue 13) | 0x8d01 (cue 13) | 0x9601 (cue 22) | 0x044e3999 (subs differ elsewhere) |
| 7 | 118, -60, -1467 | 3.14159 | 0x0 | 1 | 0x8d00 (cue 13) | 0x8d00 (cue 13) | 0x9600 (cue 22) | 0x044e3999 (subs differ elsewhere) |
| 8 | -72.5, -90, -1530 | -1.5708 | 0x180 | 0 | 0x8d02 (cue 13) | 0x8d02 (cue 13) | 0x9602 (cue 22) | 0x044e3999 (subs differ elsewhere) |
| 9 | 33.5, -70, -1530 | 1.5708 | 0x0 | 0 | 0x8d00 (cue 13) | 0x8d00 (cue 13) | 0x9600 (cue 22) | 0x044e3999 (subs differ elsewhere) |
| 10 | 186, -19.5, -1463.5 | 3.14159 | 0x0 | 0 | 0x8d00 (cue 13) | 0x8d00 (cue 13) | 0x9600 (cue 22) | 0x044e3999 (subs differ elsewhere) |
| 11 | -197.7, -60, -1492.2 | -1.5708 | 0x480 | 0 | 0x8d01 (cue 13) | 0x8d01 (cue 13) | 0x9601 (cue 22) | 0x044e3999 (subs differ elsewhere) |
| 12 | -173.7, -60, -1492.2 | 1.5708 | 0x0 | 0 | 0x8d00 (cue 13) | 0x8d00 (cue 13) | 0x9600 (cue 22) | 0x044e3999 (subs differ elsewhere) |
<!-- area_overview:spawn end -->

The three subs share every position, yaw, +0x10 and +0x14; +0x1C is cue 13 for subs 0 and 1 and cue 22 for sub 2. The roles of entries 0..12 on the route (arrival, room moves, the attribute-0x37 squares at 8, 9 and 10, entry 5 inside the north-east room) are THIRD_LEVEL_ROUTE.md section 2.1.

<!-- area_overview:doors begin -->
| Door id | Record | Placements using it | Meaning |
|---:|---|---|---|
| 0 | 01 00 00 00 | s0+1[52] 0x823580 fl 0x80 m 0x03 (-35.5, -35, -1281.8)<br>s2[15] 0x1bc350 fl 0x80 m 0x03 (-35.5, -35, -1282.5) | area change → area 1 entry 0 sub 0 |
| 1 | 01 02 00 00 | s0+1[56] 0x1bc350 fl 0x01 m 0x03 (-48, -60, -1495.6) | room move → entry 1 (side 0) / 2 (side 1) |
| 2 | 03 04 00 00 | s0+1[55] 0x1bc350 fl 0x02 m 0x15 (215.3, -60, -1681.2) | room move → entry 3 (side 0) / 4 (side 1) |
| 3 | 07 06 00 00 | s0+1[51] 0x825170 fl 0x03 m 0x03 (123, -60, -1458.3) | room move → entry 7 (side 0) / 6 (side 1) |
| 4 | 0C 0B 00 00 | s0+1[58] 0x1bb860 fl 0x04 m 0x16 (-185.7, -60, -1492.2)<br>s2[20] 0x1bb860 fl 0x04 m 0x16 (-185.7, -60, -1492.2) | room move → entry 12 (side 0) / 11 (side 1) |
| 5 | 00 00 00 00 | — | no placement uses it |
| 6 | 00 00 00 00 | — | no placement uses it |
| 7 | 00 00 00 00 | — | no placement uses it |
<!-- area_overview:doors end -->

- **Door 0** is the only area change. Its record has no sub byte, so the Meaning column above reads sub zero; the measured request (THIRD_LEVEL_ROUTE.md a00_s0) carries sub 0xFF, which the loader resolves from D_00810730[1]. In subs 0/1 the door is the overlay owner 0x823580 ([52]); in sub 2 a plain 001BC350 door ([15]). The shaft door's progression branch (script 0x8286E0, then 001B0C60(1, 0xFF, 0)) is THIRD_LEVEL_ROUTE.md section 2.3.
- Doors 1..4 are room moves; door 2 ([55], model 0x15) and door 4 ([58], slider model 0x16) are lock-gated on D_00810841[0] bits 2 and 4 (THIRD_LEVEL_ROUTE.md section 2.1; bit 2 set by the padlock in a00_03). Door ids 5..7 are unused records.
- The only other area change in the overlay is the sub-2 owner 0x826790 (script 0x82D070 → AREA14; THIRD_LEVEL_ROUTE.md section 2.1).

## 6. Music, sound scope and messages

- **BGM.** Subs 0/1 cue 13 (the AREA01 cue continues), sub 2 cue 22 (section 5). The capture's lane 0 plays cue 13, state 2.
- **Sound scope** (001FB9F0 area-paged ids; D_00264A70 / D_00264AD0 remap tables, 0xFF = absent). Subs 0 and 1 have the same scope:

<!-- area_overview:sound_scope begin -->
| Sub | 0x3E8..0x5DB present | 0x7D0..0x9C3 present |
|---|---|---|
| 0 | 334 (remap 0x25f480): 0x3ee, 0x3f0..0x3f2, 0x3f4, 0x3f6, 0x3fb..0x402, 0x40f..0x414, 0x41a..0x41c, 0x420..0x421, 0x449..0x44f, 0x451..0x454, 0x45f..0x4f8, 0x4fe, 0x502, 0x50c..0x510, 0x51f..0x524, 0x533..0x538, 0x554..0x55e, 0x561..0x565, 0x56f..0x5db | 144 (remap 0x261590): 0x7d0..0x7e1, 0x89e..0x8a1, 0x938..0x9af, 0x9c2..0x9c3 |
| 1 | 334 (remap 0x25f480): 0x3ee, 0x3f0..0x3f2, 0x3f4, 0x3f6, 0x3fb..0x402, 0x40f..0x414, 0x41a..0x41c, 0x420..0x421, 0x449..0x44f, 0x451..0x454, 0x45f..0x4f8, 0x4fe, 0x502, 0x50c..0x510, 0x51f..0x524, 0x533..0x538, 0x554..0x55e, 0x561..0x565, 0x56f..0x5db | 144 (remap 0x261590): 0x7d0..0x7e1, 0x89e..0x8a1, 0x938..0x9af, 0x9c2..0x9c3 |
| 2 | 369 (remap 0x260bf0): 0x3e8, 0x3ee, 0x3f4, 0x3fb..0x400, 0x40f..0x414, 0x42d..0x441, 0x444..0x44f, 0x45f..0x529, 0x52f..0x535, 0x537, 0x53b, 0x53d..0x542, 0x545..0x548, 0x54b..0x54c, 0x56d..0x581, 0x584..0x58e, 0x591..0x594, 0x59f..0x5db | 188 (remap 0x261770): 0x7e2..0x7f3, 0x816..0x826, 0x85c..0x868, 0x938..0x9c3 |
<!-- area_overview:sound_scope end -->

- **Messages.** D_00264DD0[1] = 0x26EEB0, 54 eight-byte records (registries table). Not exported or loaded by the port.

## 7. Owner roster

Behaviour = the actor callback (+0x10). "Live" = the pool slot at the capture that holds the record (matched by behaviour and position, else by behaviour and +0x9A); "—" = not live at the capture.

### 7.1 Subs 0 and 1: placement table 0x82BB50

<!-- area_overview:placements_sub0 begin -->
| # | Class | Model | Flags2 | Param | UID | Kind | Link | Position | Yaw | Behaviour | Live |
|---:|---|---|---|---|---|---|---|---|---:|---|---|
| 0 | 0x0b | 0x00 | 0x00 | 0x5 | 0x0000 | 0x52 | 0xffff | 29.8, -65.6, -1529.4 | 1.5708 | 0x1c2420 | — |
| 1 | 0x0b | 0x00 | 0x00 | 0x2 | 0x0100 | 0x51 | 0xffff | 0, 0, 0 | 0 | 0x1c2420 | — |
| 2 | 0x0b | 0x00 | 0x00 | 0x0 | 0x0200 | 0x51 | 0xffff | 0, 0, 0 | 0 | 0x1c2420 | — |
| 3 | 0x04 | 0x1c | 0x00 | 0xa | 0x2901 | 0xd | 0x0 | -121.4, 19.9, -1710.9 | 0 | 0x1551b0 | 51 |
| 4 | 0x04 | 0x06 | 0x00 | 0x6 | 0x2c00 | 0xd | 0xffff | -101.1, -60, -1618.2 | 0.09948 | 0x1551b0 | 52 |
| 5 | 0x04 | 0x06 | 0x00 | 0x6 | 0x2d00 | 0xd | 0xffff | -39.6, -44, -1606.8 | 0 | 0x1551b0 | 53 |
| 6 | 0x04 | 0x06 | 0x00 | 0x6 | 0x2e00 | 0xd | 0xffff | -71.5, -60, -1631.4 | -0.14835 | 0x1551b0 | 54 |
| 7 | 0x04 | 0x06 | 0x00 | 0x6 | 0x2f01 | 0xd | 0x1 | -124, -32, -1665 | 0 | 0x1551b0 | 55 |
| 8 | 0x04 | 0x06 | 0x00 | 0x6 | 0x3000 | 0xd | 0xffff | -96, -46, -1679 | 0 | 0x1551b0 | 56 |
| 9 | 0x04 | 0x06 | 0x00 | 0x6 | 0x3100 | 0xd | 0x2 | 22.1, -60, -1720 | 0.10472 | 0x1551b0 | 57 |
| 10 | 0x04 | 0x06 | 0x00 | 0x6 | 0x3200 | 0xd | 0xffff | 82.1, -60, -1692.9 | 0 | 0x1551b0 | 58 |
| 11 | 0x04 | 0x06 | 0x00 | 0x6 | 0x3300 | 0xd | 0xffff | 144, -60, -1693.2 | 0 | 0x1551b0 | 59 |
| 12 | 0x04 | 0x06 | 0x00 | 0x6 | 0x3400 | 0xd | 0xffff | 140.9, -60, -1679.2 | 0 | 0x1551b0 | 60 |
| 13 | 0x04 | 0x06 | 0x00 | 0x6 | 0x3500 | 0xd | 0xffff | 43.3, -27.9, -1700.1 | 0.09948 | 0x1551b0 | 61 |
| 14 | 0x04 | 0x06 | 0x00 | 0x6 | 0x3600 | 0xd | 0xffff | 60.2, -27.9, -1707 | -0.10821 | 0x1551b0 | 62 |
| 15 | 0x04 | 0x06 | 0x00 | 0x6 | 0x3700 | 0xd | 0xffff | -138, -21, -1456.4 | 0 | 0x1551b0 | 63 |
| 16 | 0x04 | 0x06 | 0x00 | 0x6 | 0x3800 | 0xd | 0xffff | 48, -60, -1410.9 | 0.12392 | 0x1551b0 | 64 |
| 17 | 0x04 | 0x06 | 0x00 | 0x6 | 0x3900 | 0xd | 0xffff | 63.1, -60, -1395 | 0 | 0x1551b0 | 65 |
| 18 | 0x04 | 0x06 | 0x00 | 0x6 | 0x3a00 | 0xd | 0xffff | 71, -46, -1438 | 0 | 0x1551b0 | 66 |
| 19 | 0x04 | 0x06 | 0x00 | 0x6 | 0x3b01 | 0xd | 0x4 | 87, -60, -1413.1 | 0 | 0x1551b0 | 67 |
| 20 | 0x04 | 0x06 | 0x00 | 0x6 | 0x3c00 | 0xd | 0xffff | 63.1, -60, -1380.9 | 0 | 0x1551b0 | 68 |
| 21 | 0x04 | 0x0a | 0x00 | 0x1e | 0x0f00 | 0x46 | 0xffff | 47.9, -60, -1327 | 0 | 0x156620 | 69 |
| 22 | 0x04 | 0x0a | 0x00 | 0x1e | 0x1000 | 0x46 | 0xffff | 14.8, -60, -1524.4 | 0 | 0x156620 | 70 |
| 23 | 0x04 | 0x0a | 0x00 | 0x1e | 0x1100 | 0x46 | 0xffff | -40.7, -60, -1755.3 | 0.47473 | 0x156620 | 71 |
| 24 | 0x04 | 0x0a | 0x00 | 0x1e | 0x1200 | 0x46 | 0xffff | -40.3, -60, -1747.3 | 0 | 0x156620 | 72 |
| 25 | 0x04 | 0x18 | 0x00 | 0x23 | 0x0300 | 0x46 | 0xffff | -138.6, -60, -1589.7 | 0 | 0x156620 | 73 |
| 26 | 0x04 | 0x18 | 0x00 | 0x23 | 0x0400 | 0x46 | 0xffff | -0.8, -60, -1614.8 | 0 | 0x156620 | 74 |
| 27 | 0x04 | 0x18 | 0x00 | 0x23 | 0x0500 | 0x46 | 0xffff | 15, -60, -1589.9 | 0 | 0x156620 | 75 |
| 28 | 0x04 | 0x18 | 0x00 | 0x23 | 0x0600 | 0x46 | 0xffff | -0.2, -60, -1564.8 | 0 | 0x156620 | 76 |
| 29 | 0x04 | 0x18 | 0x00 | 0x23 | 0x0700 | 0x46 | 0xffff | 16.1, -60, -1543.4 | 0 | 0x156620 | 77 |
| 30 | 0x04 | 0x18 | 0x00 | 0x23 | 0x0800 | 0x46 | 0xffff | -139.5, -60, -1619.5 | 0 | 0x156620 | 78 |
| 31 | 0x04 | 0x18 | 0x00 | 0x23 | 0x0900 | 0x46 | 0xffff | 59.9, -60, -1355.9 | 0 | 0x156620 | 79 |
| 32 | 0x04 | 0x18 | 0x00 | 0x23 | 0x0a00 | 0x46 | 0xffff | 42.3, -60, -1318.8 | 0 | 0x156620 | 80 |
| 33 | 0x04 | 0x0c | 0x00 | 0x20 | 0x0e00 | 0x46 | 0xffff | 43.6, -60, -1347.8 | 0.1885 | 0x156620 | 81 |
| 34 | 0x04 | 0x2b | 0x00 | 0x22 | 0x0b00 | 0x46 | 0xffff | 0, -60, -1397 | -0.67544 | 0x156f30 | 82 |
| 35 | 0x04 | 0x2b | 0x00 | 0x22 | 0x0c00 | 0x46 | 0xffff | 20, -60, -1370 | 0.9704 | 0x156f30 | 83 |
| 36 | 0x04 | 0x2b | 0x00 | 0x22 | 0x0d00 | 0x46 | 0xffff | 7, -60, -1357 | -0.3194 | 0x156f30 | 84 |
| 37 | 0x0c | 0x00 | 0x00 | 0x0 | 0x0000 | 0x0 | 0x0 | 30, -79, -1640 | 0 | 0x1e7d20 | 85 |
| 38 | 0x0c | 0x00 | 0x00 | 0x1 | 0x0000 | 0x0 | 0x0 | 54.5, -65, -1514 | 0 | 0x1e7d20 | 86 |
| 39 | 0x0c | 0x63 | 0x00 | 0x0 | 0x0000 | 0x0 | 0xffff | 0, 0, 0 | 0 | 0x22dcd0 | 87 |
| 40 | 0x84 | 0x0f | 0x00 | 0x8 | 0x2a00 | 0x3 | 0xffff | 219, -34.5, -1655 | 0 | 0x825480 | 88 |
| 41 | 0x08 | 0x00 | 0x00 | 0xe | 0x1c00 | 0x0 | 0xffff | 152.5, -45, -1410.9 | 0 | 0x825920 | 89 |
| 42 | 0x08 | 0x00 | 0x00 | 0xe | 0x1d00 | 0x0 | 0xffff | 150.9, -45, -1410.9 | 0 | 0x825920 | 90 |
| 43 | 0x88 | 0x00 | 0x00 | 0xe | 0x1e00 | 0x0 | 0xffff | 149.4, -45, -1410.9 | 0 | 0x825920 | 91 |
| 44 | 0x89 | 0x00 | 0x00 | 0x0 | 0x0000 | 0x0 | 0xffff | 200, -60, -1654 | -0.46251 | 0x825c80 | 92 |
| 45 | 0x89 | 0x00 | 0x00 | 0x0 | 0x0000 | 0x0 | 0xffff | -38, -44, -1631 | -0.46251 | 0x8261e0 | 93 |
| 46 | 0x89 | 0x00 | 0x00 | 0x0 | 0x0000 | 0x0 | 0xffff | -43.9, 40, -1729.8 | 0 | 0x8262d0 | 94 |
| 47 | 0x84 | 0x00 | 0x00 | 0x11 | 0x1a00 | 0x4 | 0xffff | 50, -59, -1590 | 0 | 0x8263c0 | 95 |
| 48 | 0x04 | 0x01 | 0x00 | 0x2 | 0x3e00 | 0x4 | 0xffff | 50, -60, -1590 | 0 | 0x825600 | 96 |
| 49 | 0x04 | 0x01 | 0x00 | 0x15 | 0x1700 | 0x0 | 0xffff | 34.6, -60, -1628.9 | 1.5708 | 0x825600 | 97 |
| 50 | 0x04 | 0x01 | 0x00 | 0x7 | 0x2b00 | 0x4 | 0xffff | 155, -85, -1590 | 0 | 0x825600 | 98 |
| 51 | 0x85 | 0x03 | 0x03 | 0x3 | 0x3d00 | 0x3 | 0x200 | 123, -60, -1458.3 | 0 | 0x825170 | 99 |
| 52 | 0x85 | 0x03 | 0x80 | 0xb | 0x2000 | 0x0 | 0x300 | -35.5, -35, -1281.8 | 0 | 0x823580 | 100 |
| 53 | 0x08 | 0x02 | 0x01 | 0x1c | 0x1500 | 0x0 | 0xffff | -40, -13, -1283 | -3.14159 | 0x158d30 | 101 |
| 54 | 0x44 | 0x0e | 0x02 | 0xc | 0x1f00 | 0x0 | 0xffff | 214.8, -52.2, -1688.6 | -1.5708 | 0x1581a0 | 102 |
| 55 | 0x85 | 0x15 | 0x02 | 0x13 | 0x1800 | 0x2 | 0x400 | 215.3, -60, -1681.2 | -1.5708 | 0x1bc350 | 103 |
| 56 | 0x85 | 0x03 | 0x01 | 0x13 | 0x1900 | 0x2 | 0x400 | -48, -60, -1495.6 | 1.5708 | 0x1bc350 | 104 |
| 57 | 0x86 | 0x13 | 0x04 | 0x10 | 0x1b00 | 0x0 | 0xffff | -185, -45, -1502 | 1.5708 | 0x158810 | 105 |
| 58 | 0x85 | 0x16 | 0x04 | 0x1d | 0x1300 | 0x4 | 0x102 | -185.7, -60, -1492.2 | 1.5708 | 0x1bb860 | 106 |
| 59 | 0x08 | 0x02 | 0x04 | 0x1c | 0x1400 | 0x0 | 0xffff | -185, -38, -1492.3 | 1.5708 | 0x158bd0 | 107 |
| 60 | 0x04 | 0x00 | 0x00 | 0x19 | 0x1600 | 0x3 | 0xffff | 0, -5, -1389 | 0 | 0x8266a0 | 108 |
| 61 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0000 | 0x3 | 0x0 | 23.396, -59.831, -1490.03 | 0 | 0x15a2c0 | 109 |
| 62 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0001 | 0x3 | 0x0 | -112.176, -59.831, -1551.86 | 0 | 0x15a2c0 | 110 |
| 63 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0002 | 0x4 | 0x0 | 14.549, -59.831, -1650.1 | 0 | 0x15a2c0 | 111 |
| 64 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0003 | 0x5 | 0x0 | -26.357, -59.831, -1493.58 | 0 | 0x15a2c0 | 112 |
| 65 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0004 | 0x5 | 0x0 | 53.503, -59.831, -1452.7 | 0 | 0x15a2c0 | 113 |
| 66 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0005 | 0x5 | 0x0 | -90.419, -59.831, -1513.69 | 0 | 0x15a2c0 | 114 |
| 67 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0006 | 0x1 | 0x0 | -143.629, -59.831, -1611.59 | 0 | 0x15a2c0 | 115 |
| 68 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0007 | 0x1 | 0x0 | -18.796, -59.831, -1555.17 | 0 | 0x15a2c0 | 116 |
<!-- area_overview:placements_sub0 end -->

<!-- area_overview:placements_sub1 begin -->
Sub 1 uses the sub 0 placement table 0x82bb50 (the same 69 records).
<!-- area_overview:placements_sub1 end -->

- Records 0..2 are class 0x0B (001C2420; skipped at load, queried later, as in AREA01).
- The records the route used (THIRD_LEVEL_ROUTE.md section 1, owners table): [40] terminal 0x825480, [41]..[43] the 0x825920 trio, [47] 0x8263C0 and [48]..[50] 0x825600 (the ferry and its companions), [51] door 0x825170, [52] shaft door 0x823580, [54] padlock 001581A0, [55] and [56] doors 001BC350, [58] slider 001BB860, [60] 0x8266A0.

### 7.2 Groups (deferred, nest, and groups reached from code or scripts)

<!-- area_overview:groups begin -->
| Group | Records | How it is reached | Behaviours |
|---|---:|---|---|
| 0x826f80 | 61 | sub0+1 deferred group 0x826f80; D_0024D820[0][0] list and D_0024D820[0][1] list | 0x128c10, 0x12a5d0, 0x1551b0, 0x15ab00, 0x15afa0, 0x1e3d90, 0x219550, 0x824ea0 |
| 0x827a30 | 1 | group 0x827a30; code 0x8263c0 | 0x15b030 |
| 0x827a90 | 5 | nest group link 0 0x827a90; D_0024D820[0][3+0] | 0x128c10, 0x12a5d0 |
| 0x827ba0 | 2 | nest group link 1 0x827ba0; D_0024D820[0][3+1] | 0x12a5d0 |
| 0x827c30 | 3 | nest group link 2 0x827c30; D_0024D820[0][3+2] | 0x128c10 |
| 0x827ce0 | 3 | nest group link 3 0x827ce0; D_0024D820[0][3+3] | 0x128c10, 0x12a5d0 |
| 0x827d90 | 7 | nest group link 4 0x827d90; D_0024D820[0][3+4] | 0x128c10, 0x12a5d0 |
| 0x827ef0 | 1 | group 0x827ef0; code 0x825d70 | 0x14d260 |
| 0x827f50 | 1 | group 0x827f50; code 0x825e80 | 0x15afa0 |
| 0x827fb0 | 3 | sub2 deferred group 0x827fb0; D_0024D820[0][2] list | 0x1e3d90 |
| 0x828060 | 24 | sub2 deferred group 0x828060; D_0024D820[0][2] list | 0x12a5d0, 0x15afa0, 0x1becc0, 0x1bf6b0, 0x1c06e0, 0x219550 |
| 0x82a920 | 14 | group 0x82a920; word 0x82ad14 (script 0x82abc0) | 0x0, 0x823c50, 0x823e10 |
| 0x82cdd0 | 14 | unreferenced (no pointer found) | 0x823eb0, 0x824130 |
<!-- area_overview:groups end -->

Records of the groups that belong to sub 0 (the current sub of the capture) or to no sub list:

<!-- area_overview:group_records begin -->
| Group record | Cond (+0, +2) | Class | Model | Param | UID | Kind | Link | Position | Behaviour | Live |
|---|---|---|---|---|---|---|---|---|---|---|
| 0x826f80[0] | 2, 1280 | 0x0d | 0x01 | 0x2 | 0x0000 | 0x0 | 0xffff | 37.9, -60, -1487.5 | 0x1e3d90 | 0 |
| 0x826f80[1] | 2, 1280 | 0x0d | 0x01 | 0x2 | 0x0000 | 0x0 | 0xffff | 19.2, -60, -1483.7 | 0x1e3d90 | 1 |
| 0x826f80[2] | 2, 1280 | 0x0d | 0x01 | 0x2 | 0x0000 | 0x0 | 0xffff | 1.2, -60, -1487.2 | 0x1e3d90 | 2 |
| 0x826f80[3] | 2, 1280 | 0x0d | 0x01 | 0x1 | 0x0000 | 0x0 | 0xffff | 51.6, -60, -1482.9 | 0x1e3d90 | 3 |
| 0x826f80[4] | 2, 1280 | 0x0d | 0x01 | 0x1 | 0x0000 | 0x0 | 0xffff | 8.6, -60, -1473.4 | 0x1e3d90 | 4 |
| 0x826f80[5] | 2, 1280 | 0x0d | 0x01 | 0x1 | 0x0000 | 0x0 | 0xffff | -9.5, -60, -1479.4 | 0x1e3d90 | 5 |
| 0x826f80[6] | 2, 1280 | 0x0d | 0x01 | 0x0 | 0x0000 | 0x0 | 0xffff | -19.8, -60, -1483.4 | 0x1e3d90 | 6 |
| 0x826f80[7] | 3, 1536 | 0x0d | 0x01 | 0x1 | 0x0000 | 0x0 | 0x0 | -61.426, -34.993, -1383.9 | 0x1e3d90 | — |
| 0x826f80[8] | 3, 1536 | 0x0d | 0x01 | 0x1 | 0x0000 | 0x0 | 0x0 | -49.026, -34.993, -1390.3 | 0x1e3d90 | — |
| 0x826f80[9] | 3, 1536 | 0x0d | 0x01 | 0x2 | 0x0000 | 0x0 | 0x0 | -12.337, -58.77, -1395.9 | 0x1e3d90 | — |
| 0x826f80[10] | 3, 1536 | 0x0d | 0x01 | 0x0 | 0x0000 | 0x0 | 0x0 | -25.037, -58.77, -1385.7 | 0x1e3d90 | — |
| 0x826f80[11] | 3, 1536 | 0x0d | 0x01 | 0x1 | 0x0000 | 0x0 | 0x0 | 14.163, -58.77, -1401 | 0x1e3d90 | — |
| 0x826f80[12] | 3, 1536 | 0x0d | 0x01 | 0x0 | 0x0000 | 0x0 | 0x0 | 30.163, -58.77, -1396.5 | 0x1e3d90 | — |
| 0x826f80[13] | 3, 1536 | 0x0d | 0x01 | 0x0 | 0x0000 | 0x0 | 0x0 | 42.463, -58.77, -1373.3 | 0x1e3d90 | — |
| 0x826f80[14] | 3, 1536 | 0x0d | 0x01 | 0x2 | 0x0000 | 0x0 | 0x0 | 22.463, -58, -1417.6 | 0x1e3d90 | — |
| 0x826f80[15] | 3, 1536 | 0x0d | 0x01 | 0x2 | 0x0000 | 0x0 | 0x0 | 59.426, -58, -1380.9 | 0x1e3d90 | — |
| 0x826f80[16] | 3, 1536 | 0x0d | 0x01 | 0x2 | 0x0000 | 0x0 | 0x0 | 45.026, -58, -1399.3 | 0x1e3d90 | — |
| 0x826f80[17] | 2, 1536 | 0x0d | 0x03 | 0x1 | 0x0000 | 0x0 | 0x0 | -52.1, -34.9, -1364.1 | 0x15ab00 | 7 |
| 0x826f80[18] | 2, 1536 | 0x0d | 0x03 | 0x1 | 0x0001 | 0x1 | 0x0 | 59.6, -59.7, -1369.8 | 0x15ab00 | 8 |
| 0x826f80[19] | 2, 1536 | 0x0d | 0x03 | 0x1 | 0x0002 | 0x1 | 0x0 | 26.5, -59.7, -1387.4 | 0x15ab00 | 9 |
| 0x826f80[20] | 2, 1536 | 0x0d | 0x03 | 0x1 | 0x0003 | 0x1 | 0x0 | 14.6, -59.7, -1418.6 | 0x15ab00 | 10 |
| 0x826f80[21] | 2, 1536 | 0x0d | 0x03 | 0x1 | 0x0004 | 0x1 | 0x0 | -25, -59.7, -1400.3 | 0x15ab00 | 11 |
| 0x826f80[22] | 2, 1536 | 0x0d | 0x03 | 0x1 | 0x0005 | 0x0 | 0x0 | 47.2, -59.7, -1419.8 | 0x15ab00 | 12 |
| 0x826f80[23] | 2, 1536 | 0x0d | 0x03 | 0x1 | 0x0006 | 0x0 | 0x0 | 75.5, -59.7, -1398.6 | 0x15ab00 | 13 |
| 0x826f80[24] | 2, 1536 | 0x0d | 0x03 | 0x1 | 0x0007 | 0x0 | 0x0 | -50.2, -34.9, -1391.9 | 0x15ab00 | 14 |
| 0x826f80[25] | 2, 512 | 0x02 | 0x00 | 0x0 | 0x0000 | 0x0 | 0x0 | -46.6, -30.5, -1291.4 | 0x824ea0 | — |
| 0x826f80[26] | 1, 1 | 0x84 | 0x00 | 0x72 | 0x3f00 | 0x46 | 0xffff | -68.6, -89.9, -1562.7 | 0x219550 | 16 |
| 0x826f80[27] | 1, 3 | 0x84 | 0x00 | 0x72 | 0x4000 | 0x46 | 0xffff | 60.3, -28, -1706.4 | 0x219550 | 17 |
| 0x826f80[28] | 1, 2 | 0xc7 | 0x02 | 0x58 | 0xff00 | 0x46 | 0xce | -39.7, 40.1, -1723.5 | 0x15afa0 | 18 |
| 0x826f80[29] | 1, 4 | 0xc7 | 0x00 | 0x4d | 0xff00 | 0x46 | 0xffff | 225.7, -46, -1664.3 | 0x15afa0 | 19 |
| 0x826f80[30] | 1, 11 | 0xc7 | 0x02 | 0x58 | 0xff00 | 0x46 | 0xcd | 212.4, -59.9, -1665.3 | 0x15afa0 | 20 |
| 0x826f80[31] | 1, 12 | 0xc7 | 0x02 | 0x58 | 0xff00 | 0x46 | 0x65 | 87, -64.8, -1505.9 | 0x15afa0 | 21 |
| 0x826f80[32] | 1, 15 | 0xc7 | 0x00 | 0x4d | 0xff00 | 0x46 | 0xffff | 87.2, -59.6, -1429.1 | 0x15afa0 | 22 |
| 0x826f80[33] | 1, 16 | 0xc7 | 0x00 | 0x4f | 0xff00 | 0x46 | 0xffff | -89.7, -99.7, -1533.1 | 0x15afa0 | 23 |
| 0x826f80[34] | 1, 18 | 0xc7 | 0x02 | 0x58 | 0xff00 | 0x46 | 0xcc | -46.7, -43.9, -1620.8 | 0x15afa0 | 24 |
| 0x826f80[35] | 1, 20 | 0xc7 | 0x02 | 0x58 | 0xff00 | 0x46 | 0xcb | 133.6, -42.9, -1449.9 | 0x15afa0 | 25 |
| 0x826f80[36] | 1, 21 | 0xc7 | 0x00 | 0x59 | 0xff00 | 0x46 | 0xffff | 106.3, -84.7, -1598.5 | 0x15afa0 | 26 |
| 0x826f80[37] | 1, 23 | 0xc7 | 0x00 | 0x4d | 0xff00 | 0x46 | 0xffff | -124.2, -31.4, -1665.9 | 0x15afa0 | 27 |
| 0x826f80[38] | 1, 25 | 0xc7 | 0x00 | 0x59 | 0xff00 | 0x46 | 0xffff | -107.6, -89.9, -1531.6 | 0x15afa0 | 28 |
| 0x826f80[39] | 1, 27 | 0x04 | 0x50 | 0xa | 0x2100 | 0xd | 0xffff | 118.7, -53, -1452.2 | 0x1551b0 | 29 |
| 0x826f80[40] | 1, 28 | 0x04 | 0x50 | 0xa | 0x2201 | 0xd | 0x3 | 122.4, -53, -1442.9 | 0x1551b0 | 30 |
| 0x826f80[41] | 1, 29 | 0x04 | 0x50 | 0xa | 0x2300 | 0xd | 0xffff | 114.8, -53, -1445.5 | 0x1551b0 | 31 |
| 0x826f80[42] | 1, 30 | 0x04 | 0x50 | 0xa | 0x2400 | 0xd | 0xffff | 113.1, -60, -1446.4 | 0x1551b0 | 32 |
| 0x826f80[43] | 1, 31 | 0x04 | 0x50 | 0xa | 0x2500 | 0xd | 0xffff | 118.6, -60, -1453.9 | 0x1551b0 | 33 |
| 0x826f80[44] | 1, 32 | 0x04 | 0x50 | 0xa | 0x2600 | 0xd | 0xffff | 124.3, -60, -1446.5 | 0x1551b0 | 34 |
| 0x826f80[45] | 1, 33 | 0x04 | 0x50 | 0xa | 0x2700 | 0xd | 0xffff | 123, -60, -1439.5 | 0x1551b0 | 35 |
| 0x826f80[46] | 1, 34 | 0x04 | 0x50 | 0xa | 0x2800 | 0xd | 0xffff | 114.5, -60, -1438.8 | 0x1551b0 | 36 |
| 0x826f80[47] | 1, 100 | 0x02 | 0x00 | 0x6 | 0x0000 | 0x0 | 0x0 | 195, -60, -1655 | 0x128c10 | 37 |
| 0x826f80[48] | 1, 101 | 0x02 | 0x00 | 0x6 | 0x0000 | 0x0 | 0x0 | 201, -60, -1654 | 0x128c10 | 38 |
| 0x826f80[49] | 1, 102 | 0x02 | 0x00 | 0x6 | 0x0000 | 0x0 | 0x0 | 205, -60, -1659 | 0x128c10 | 39 |
| 0x826f80[50] | 1, 103 | 0x02 | 0x00 | 0x6 | 0x0000 | 0x0 | 0x0 | 202, -60, -1656 | 0x128c10 | 40 |
| 0x826f80[51] | 1, 104 | 0x02 | 0x00 | 0x6 | 0x0000 | 0x0 | 0x0 | 197, -60, -1656 | 0x128c10 | 41 |
| 0x826f80[52] | 1, 105 | 0x02 | 0x00 | 0x6 | 0x0000 | 0x0 | 0x0 | 204.7, -61.6, -1654.9 | 0x128c10 | 42 |
| 0x826f80[53] | 1, 106 | 0x02 | 0x00 | 0x3 | 0x0000 | 0x0 | 0x0 | 193.3, -61.8, -1651 | 0x12a5d0 | 43 |
| 0x826f80[54] | 1, 107 | 0x02 | 0x00 | 0x8 | 0x0000 | 0x0 | 0x0 | -131.6, -12.2, -1445.5 | 0x128c10 | 44 |
| 0x826f80[55] | 1, 108 | 0x02 | 0x00 | 0x8 | 0x0000 | 0x0 | 0x0 | 23.1, -51.8, -1714.7 | 0x128c10 | 45 |
| 0x826f80[56] | 1, 110 | 0x02 | 0x00 | 0x3 | 0x0000 | 0x0 | 0x0 | 42.5, -50.9, -1410.8 | 0x12a5d0 | 46 |
| 0x826f80[57] | 1, 111 | 0x02 | 0x00 | 0x8 | 0x0000 | 0x0 | 0x0 | 96.6, -33.7, -1413.6 | 0x128c10 | 47 |
| 0x826f80[58] | 1, 112 | 0x02 | 0x00 | 0x8 | 0x0000 | 0x0 | 0x0 | 65.6, -38.1, -1437.5 | 0x128c10 | 48 |
| 0x826f80[59] | 1, 113 | 0x02 | 0x00 | 0x7 | 0x0005 | 0x0 | 0x0 | 122.2, -50.2, -1441.3 | 0x128c10 | 49 |
| 0x826f80[60] | 1, 114 | 0x02 | 0x00 | 0x7 | 0x0005 | 0x0 | 0x0 | 114.9, -56.8, -1434.8 | 0x128c10 | 50 |
| 0x827a30[0] | 1, 7 | 0xc7 | 0x02 | 0x58 | 0xff00 | 0x46 | 0x66 | 49.3, -58.9, -1535 | 0x15b030 | 129 |
| 0x827a90[0] | 1, 115 | 0x02 | 0x00 | 0x4 | 0x0000 | 0x0 | 0x0 | -1, 1, 1 | 0x128c10 | — |
| 0x827a90[1] | 1, 116 | 0x02 | 0x00 | 0x4 | 0x0000 | 0x0 | 0x0 | 1, 1, -1 | 0x128c10 | — |
| 0x827a90[2] | 1, 117 | 0x02 | 0x00 | 0x4 | 0x0000 | 0x0 | 0x0 | -1, 1, 1 | 0x12a5d0 | — |
| 0x827a90[3] | 1, 118 | 0x02 | 0x00 | 0x4 | 0x0000 | 0x0 | 0x0 | 1, 1, -1 | 0x128c10 | — |
| 0x827a90[4] | 1, 119 | 0x02 | 0x00 | 0x4 | 0x0000 | 0x0 | 0x0 | -1, 1, 1 | 0x128c10 | — |
| 0x827ba0[0] | 1, 120 | 0x02 | 0x00 | 0x4 | 0x0000 | 0x0 | 0x0 | -5, 1, -6 | 0x12a5d0 | — |
| 0x827ba0[1] | 1, 121 | 0x02 | 0x00 | 0x4 | 0x0000 | 0x0 | 0x0 | -5, 1, -5 | 0x12a5d0 | — |
| 0x827c30[0] | 1, 122 | 0x02 | 0x00 | 0x4 | 0x0000 | 0x0 | 0x0 | 1, 1, -1 | 0x128c10 | — |
| 0x827c30[1] | 1, 123 | 0x02 | 0x00 | 0x4 | 0x0000 | 0x0 | 0x0 | -1, 1, -1 | 0x128c10 | — |
| 0x827c30[2] | 1, 124 | 0x02 | 0x00 | 0x4 | 0x0000 | 0x0 | 0x0 | -1, 1, 1 | 0x128c10 | — |
| 0x827ce0[0] | 1, 125 | 0x02 | 0x00 | 0x4 | 0x0005 | 0x0 | 0x0 | -1, 1, -1 | 0x128c10 | — |
| 0x827ce0[1] | 1, 126 | 0x02 | 0x00 | 0x4 | 0x0005 | 0x0 | 0x0 | -1, 1, 0 | 0x12a5d0 | — |
| 0x827ce0[2] | 1, 127 | 0x02 | 0x00 | 0x4 | 0x0005 | 0x0 | 0x0 | -1, 1, 1 | 0x128c10 | — |
| 0x827d90[0] | 1, 128 | 0x02 | 0x00 | 0x4 | 0x0000 | 0x0 | 0x0 | -1, 1, -1 | 0x128c10 | — |
| 0x827d90[1] | 1, 129 | 0x02 | 0x00 | 0x4 | 0x0000 | 0x0 | 0x0 | 1, 3, 0 | 0x128c10 | — |
| 0x827d90[2] | 1, 130 | 0x02 | 0x00 | 0x4 | 0x0000 | 0x0 | 0x0 | -1, 1, 1 | 0x128c10 | — |
| 0x827d90[3] | 1, 131 | 0x02 | 0x00 | 0x4 | 0x0000 | 0x0 | 0x0 | 1, 1, -1 | 0x128c10 | — |
| 0x827d90[4] | 1, 132 | 0x02 | 0x00 | 0x4 | 0x0000 | 0x0 | 0x0 | -1, 3, 0 | 0x128c10 | — |
| 0x827d90[5] | 1, 133 | 0x02 | 0x00 | 0x4 | 0x0000 | 0x0 | 0x0 | 1, 1, 1 | 0x128c10 | — |
| 0x827d90[6] | 1, 134 | 0x02 | 0x00 | 0x4 | 0x0000 | 0x0 | 0x0 | -1, 3, -1 | 0x12a5d0 | — |
| 0x827ef0[0] | 1, 0 | 0x02 | 0x09 | 0x0 | 0x0000 | 0x0 | 0x0 | -129, -40.491, -1579 | 0x14d260 | — |
| 0x827f50[0] | 4, 11032 | 0xc7 | 0x00 | 0x34 | 0xff00 | 0x46 | 0x4 | -108.6, -59.5, -1597.5 | 0x15afa0 | — |
| 0x82a920[0] | 10, 1 | 0x9e | 0x99 | 0x2 | 0x0000 | 0x0 | 0x3f00 | 0, 0, 0 | 0x0 | — |
| 0x82a920[1] | 9, 1 | 0x9d | 0x99 | 0x3 | 0x0006 | 0x0 | 0x3f00 | 0, 0, 0 | 0x0 | — |
| 0x82a920[2] | 9, 2 | 0x61 | 0x99 | 0x4 | 0x0002 | 0x0 | 0x3f00 | 0, 0, 0 | 0x0 | — |
| 0x82a920[3] | 9, 4 | 0x270e | 0x99 | 0x5 | 0x0007 | 0x0 | 0x3f00 | 0, 0, 0 | 0x823c50 | — |
| 0x82a920[4] | 9, 4 | 0x270e | 0x99 | 0x6 | 0x0007 | 0x0 | 0x3f00 | 0, 0, 0 | 0x823c50 | — |
| 0x82a920[5] | 9, 4 | 0x270e | 0x99 | 0x7 | 0x0007 | 0x0 | 0x3f00 | 0, 0, 0 | 0x823c50 | — |
| 0x82a920[6] | 9, 4 | 0x270e | 0x99 | 0x8 | 0x0007 | 0x0 | 0x3f00 | 0, 0, 0 | 0x823c50 | — |
| 0x82a920[7] | 9, 4 | 0x270e | 0x99 | 0x9 | 0x0007 | 0x0 | 0x3f00 | 0, 0, 0 | 0x823c50 | — |
| 0x82a920[8] | 9, 4 | 0x270e | 0x99 | 0xa | 0x0007 | 0x0 | 0x3f00 | 0, 0, 0 | 0x823e10 | — |
| 0x82a920[9] | 9, 4 | 0x270e | 0x99 | 0xb | 0x0007 | 0x0 | 0x3f00 | 0, 0, 0 | 0x823e10 | — |
| 0x82a920[10] | 9, 4 | 0x270e | 0x99 | 0xc | 0x0007 | 0x0 | 0x3f00 | 0, 0, 0 | 0x823e10 | — |
| 0x82a920[11] | 9, 4 | 0x270e | 0x99 | 0xd | 0x0007 | 0x0 | 0x3f00 | 0, 0, 0 | 0x823e10 | — |
| 0x82a920[12] | 9, 4 | 0x270e | 0x99 | 0xe | 0x0007 | 0x0 | 0x3f00 | 0, 0, 0 | 0x823e10 | — |
| 0x82a920[13] | 9, 4 | 0x270e | 0x99 | 0xf | 0x0007 | 0x0 | 0x3f00 | 0, 0, 0 | 0x823e10 | — |
<!-- area_overview:group_records end -->

- 0x826F80 is the subs-0/1 deferred group (61 records); its record [25] is the only way 0x824EA0 is reached, and 0x824EA0 starts the arrival script 0x828D60 (section 9).
- 0x827A30 (one 0x15B030 record) is referenced by 0x8263C0, 0x827EF0 (0x14D260) by 0x825D70 and 0x827F50 (0x15AFA0) by 0x825E80; 0x82A920 is the op14 table of script 0x82ABC0 (sub 2) and carries 0x823C50 and 0x823E10. The group at 0x82CDD0 is referenced by no pointer the tool found (unreferenced, or reached by an address computation it does not see); it is the only place 0x823EB0 and 0x824130 appear.

### 7.3 Live pool at the arrival

The last column is the port's first-level census row for that behaviour at port 52caca6 (Port status and the module part of the Module / test cell; `overview.json` `port_census_rows` keeps every cell). Those rows are AREA11 bindings; no AREA00 behaviour is bound in the port.

<!-- area_overview:live_pool begin -->
| Behaviour | Nodes | From | First-level census row (port status and module) |
|---|---:|---|---|
| 0x1551b0 | 26 | sub0+1 deferred group 0x826f80 ×8; place ×18 | live (em_crate_original over its roster node (em_area11_boxes.c)) |
| 0x156620 | 13 | place ×13 | live (em_drum_original over its roster node (em_area11_boxes.c)) |
| 0x128c10 | 12 | sub0+1 deferred group 0x826f80 ×12 | in census, no row |
| 0x15afa0 | 11 | sub0+1 deferred group 0x826f80 ×11 | live (em_pickup_owner em_pickup_owner_tick via em_pickup.c and the AREA11 interaction host) |
| 0x15a2c0 | 8 | place ×8 | in census, no row |
| 0x15ab00 | 8 | sub0+1 deferred group 0x826f80 ×8 | not in census |
| 0x18a6b0 | 7 | runtime spawn ×7 | live (em_player_equipment em_player_equipment_tick through em_equipment_live on the pool nodes (em_area11_bindings)) |
| 0x1e3d90 | 7 | sub0+1 deferred group 0x826f80 ×7 | in census, no row |
| 0x156f30 | 3 | place ×3 | not in census |
| 0x1c5680 | 3 | runtime spawn ×3 | live (em_indicator_child em_indicator_child_step, per node (em_area11_bindings.c tick_indicator), its bind 001C2360 and placement 001C6380 through em_indicator_bind_live) |
| 0x825600 | 3 | place ×3 | live (em_director_original (the beats' gates and bodies, node #21 since WP-8b)) |
| 0x825920 | 3 | place ×3 | not in census |
| 0x12a5d0 | 2 | sub0+1 deferred group 0x826f80 ×2 | not in census |
| 0x1bc350 | 2 | place ×2 | live (em_door_original em_door_original_tick via em_area11_door (node tick_door; census L18)) |
| 0x1e7d20 | 2 | place ×2 | in census, no row |
| 0x219550 | 2 | sub0+1 deferred group 0x826f80 ×2 | live (em_pickup_owner em_pickup_owner_tick via em_pickup.c and the AREA11 interaction host) |
| 0x1581a0 | 1 | place ×1 | not in census |
| 0x158810 | 1 | place ×1 | not in census |
| 0x158bd0 | 1 | place ×1 | not in census |
| 0x158d30 | 1 | place ×1 | in census, no row |
| 0x15b030 | 1 | group 0x827a30 ×1 | not in census |
| 0x1bb860 | 1 | place ×1 | in census, no row |
| 0x1c5760 | 1 | runtime spawn ×1 | live (em_indicator_child em_indicator_child_step, per node; its bind 001C22A0 and placement 001C6380 through em_indicator_bind_live; the terminal's colour tail 0x827EAC (em_indicator_00827B10_colour)) |
| 0x1c5930 | 1 | runtime spawn ×1 | verified-unbound (em_status_ui_leftovers em_sul_001C5930) |
| 0x1e2560 | 1 | runtime spawn ×1 | live (em_head_sprite_original em_head_sprite_original_tick through em_effects_live on the pool nodes (the player's and Roger's)) |
| 0x22dcd0 | 1 | place ×1 | not in census |
| 0x823580 | 1 | place ×1 | not in census |
| 0x825170 | 1 | place ×1 | not in census |
| 0x825480 | 1 | place ×1 | not in census |
| 0x825c80 | 1 | place ×1 | not in census |
| 0x8261e0 | 1 | place ×1 | not in census |
| 0x8262d0 | 1 | place ×1 | not in census |
| 0x8263c0 | 1 | place ×1 | not in census |
| 0x8266a0 | 1 | place ×1 | not in census |
<!-- area_overview:live_pool end -->

Runtime spawns (no table record): the area title 001C5930, the seven equipment nodes 0018A6B0, the head sprite 001E2560 and the indicator children 001C5680 / 001C5760, as at the AREA01 arrival.

### 7.4 Sub 2 (not on the recorded route)

<!-- area_overview:placements_sub2 begin -->
| # | Class | Model | Flags2 | Param | UID | Kind | Link | Position | Yaw | Behaviour |
|---:|---|---|---|---|---|---|---|---|---:|---|
| 0 | 0x0b | 0x00 | 0x00 | 0x6 | 0x0000 | 0x51 | 0xffff | 0, 0, 0 | 0 | 0x1c2420 |
| 1 | 0x04 | 0x06 | 0x00 | 0x3 | 0x1900 | 0xd | 0xffff | 188.3, -60, -1604 | 0 | 0x1551b0 |
| 2 | 0x04 | 0x06 | 0x00 | 0x3 | 0x1a00 | 0xd | 0xffff | 161.3, -60, -1692.5 | 0.06458 | 0x1551b0 |
| 3 | 0x04 | 0x06 | 0x00 | 0x3 | 0x1b00 | 0xd | 0xffff | 143.3, -60, -1692.5 | 0.06458 | 0x1551b0 |
| 4 | 0x04 | 0x06 | 0x00 | 0x3 | 0x1c00 | 0xd | 0xffff | 59, -60, -1411 | 0 | 0x1551b0 |
| 5 | 0x04 | 0x06 | 0x00 | 0x3 | 0x1d00 | 0xd | 0xffff | 153, -46, -1692.5 | 0.06458 | 0x1551b0 |
| 6 | 0x04 | 0x06 | 0x00 | 0x3 | 0x1e00 | 0xd | 0xffff | 22, -60, -1720.1 | 1.69122 | 0x1551b0 |
| 7 | 0x04 | 0x0a | 0x00 | 0x19 | 0x0400 | 0x46 | 0xffff | -29.8, -60, -1374.2 | 0 | 0x156620 |
| 8 | 0x04 | 0x0a | 0x00 | 0x19 | 0x0500 | 0x46 | 0xffff | 34.1, -60, -1387.2 | 0 | 0x156620 |
| 9 | 0x04 | 0x0a | 0x00 | 0x19 | 0x0600 | 0x46 | 0xffff | -27.3, -60, -1410.2 | 0 | 0x156620 |
| 10 | 0x04 | 0x0c | 0x00 | 0x1b | 0x0100 | 0x46 | 0xffff | -17.1, -60, -1460.7 | 2.522 | 0x156620 |
| 11 | 0x04 | 0x0c | 0x00 | 0x1b | 0x0200 | 0x46 | 0xffff | 17.2, -60, -1504.2 | -2.08392 | 0x156620 |
| 12 | 0x04 | 0x0c | 0x00 | 0x1b | 0x0300 | 0x46 | 0xffff | -22.8, -60, -1391.6 | -0.73653 | 0x156620 |
| 13 | 0x0c | 0x63 | 0x00 | 0x0 | 0x0000 | 0x0 | 0xffff | 0, 0, 0 | 0 | 0x22dcd0 |
| 14 | 0x0c | 0x00 | 0x00 | 0x0 | 0x0000 | 0x0 | 0x0 | 30, -79, -1640 | 0 | 0x1e7d20 |
| 15 | 0x85 | 0x03 | 0x80 | 0x7 | 0x1600 | 0x3 | 0x300 | -35.5, -35, -1282.5 | 0 | 0x1bc350 |
| 16 | 0x08 | 0x02 | 0x01 | 0x10 | 0x0e00 | 0xe | 0xffff | -40, -12.5, -1283 | -3.14159 | 0x158d30 |
| 17 | 0x08 | 0x03 | 0x00 | 0xa | 0x1400 | 0xe | 0xffff | -188.5, -45, -1483.2 | -1.5708 | 0x1c4820 |
| 18 | 0x08 | 0x02 | 0x04 | 0x10 | 0x0d00 | 0xe | 0xffff | -189, -37.6, -1492.2 | -1.5708 | 0x158bd0 |
| 19 | 0x86 | 0x13 | 0x04 | 0xa | 0x1300 | 0xe | 0xffff | -185, -45, -1499.1 | 1.5708 | 0x158810 |
| 20 | 0x85 | 0x16 | 0x04 | 0x17 | 0x0800 | 0x4 | 0x182 | -185.7, -60, -1492.2 | 1.5708 | 0x1bb860 |
| 21 | 0x08 | 0x02 | 0x04 | 0x10 | 0x0f00 | 0xe | 0xffff | -185, -37.6, -1492.2 | 1.5708 | 0x158bd0 |
| 22 | 0x84 | 0x38 | 0x00 | 0x9 | 0x1500 | 0xe | 0xffff | -219.9, -49, -1476.4 | 3.14159 | 0x159b90 |
| 23 | 0x04 | 0x00 | 0x00 | 0xf | 0x1000 | 0x3 | 0xffff | 34.6, -60, -1628.9 | 1.5708 | 0x1c4820 |
| 24 | 0x08 | 0x03 | 0x00 | 0x6 | 0x1700 | 0x3 | 0xffff | 219, -34.5, -1655 | 0 | 0x1c4820 |
| 25 | 0x08 | 0x03 | 0x00 | 0xd | 0x1100 | 0x2 | 0xffff | 215.3, -60, -1681.2 | -1.5708 | 0x1c4820 |
| 26 | 0x04 | 0x00 | 0x00 | 0x5 | 0x1800 | 0x3 | 0xffff | 155, -85, -1590 | 0 | 0x1c48c0 |
| 27 | 0x04 | 0x00 | 0x00 | 0x0 | 0x1f00 | 0x3 | 0xffff | 50, -60, -1590 | 0 | 0x1c48c0 |
| 28 | 0x84 | 0x49 | 0x00 | 0xb | 0x1200 | 0x4 | 0xffff | 50, -59, -1590 | 0 | 0x826790 |
| 29 | 0x04 | 0x00 | 0x00 | 0x11 | 0x0c00 | 0x8 | 0xffff | 0, -60, -1770.5 | 0 | 0x826be0 |
| 30 | 0x04 | 0x00 | 0x00 | 0x14 | 0x0b00 | 0x46 | 0xffff | -138.7, -40.7, -1500.2 | 1.5708 | 0x825d70 |
| 31 | 0x2a | 0x01 | 0x00 | 0x9e | 0x0000 | 0x0 | 0x0 | -74.9, 37.6, -1602.8 | 1.06465 | 0x8260f0 |
| 32 | 0x04 | 0x00 | 0x00 | 0x16 | 0x0900 | 0x8 | 0xffff | -120.9, -60, -1578.5 | 1.5708 | 0x826cc0 |
| 33 | 0x04 | 0x00 | 0x00 | 0x15 | 0x0a00 | 0x46 | 0xffff | 211.9, -47.7, -1689.8 | -1.5708 | 0x1c4820 |
| 34 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0000 | 0x0 | 0x1 | -13.872, -59.99, -1299.42 | 0 | 0x15a2c0 |
| 35 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0001 | 0x3 | 0x1 | 14.094, -59.99, -1632.92 | 0 | 0x15a2c0 |
| 36 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0002 | 0x3 | 0x2 | -3.782, -59.99, -1585.1 | 0 | 0x15a2c0 |
| 37 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0003 | 0x5 | 0x2 | 16.2, -59.99, -1466.4 | 0 | 0x15a2c0 |
| 38 | 0x0d | 0x03 | 0x00 | 0x0 | 0x0004 | 0x0 | 0x1 | 25.059, -59.99, -1385.19 | 0 | 0x15a2c0 |
| 39 | 0x08 | 0x52 | 0x00 | 0x0 | 0xff00 | 0x8 | 0xffff | 16.3, -60, -1488.8 | 0 | 0x1c1a80 |
| 40 | 0x08 | 0x52 | 0x00 | 0x0 | 0xff00 | 0x8 | 0xffff | -92.5, -60, -1585.7 | 0 | 0x1c1a80 |
<!-- area_overview:placements_sub2 end -->

Sub 2's deferred groups (behaviour counts):

<!-- area_overview:group_records_other_sub begin -->
- sub2 deferred group 0x827fb0 (3): 0x1e3d90 ×3
- sub2 deferred group 0x828060 (24): 0x219550 ×1, 0x15afa0 ×10, 0x12a5d0 ×5, 0x1bf6b0 ×3, 0x1c06e0 ×2, 0x1becc0 ×3
<!-- area_overview:group_records_other_sub end -->

Compared with subs 0/1, sub 2 has a plain 001BC350 door ([15]) at the shaft door's position, the 00159B90 record [22] (THIRD_LEVEL_ROUTE.md calls it the save terminal, sub 2 only), 001C4820 records at the positions of the terminal [40] and the cage door [55] and at [49]'s position, 001C48C0 records at [48]'s and [50]'s positions, 0x826790 ([28]) at [47]'s position, five 0x15A2C0 records and its own overlay owners 0x825D70, 0x8260F0, 0x826BE0 and 0x826CC0 (read from the two placement tables above).

## 8. Overlay functions

Columns as in AREA01_OVERVIEW.md section 8. "Evidence" = the committed C compiled at fa4b42c (`--compile-check`) and resolved as the overlay link does, compared with the original bytes. "Route" = a point inside the function was executed in the AREA00 route census (`a00_delta.json`; so far only the a01_07 arrival phase, see section 0: entries were not armed, so "—" is no evidence of absence).

<!-- area_overview:overlay_functions begin -->
| Engine address | Splat piece(s) | Slot bytes | Decomp | Evidence | Route | Boot calls | Indirect calls | Reached by | Lane |
|---|---|---:|---|---|---|---:|---:|---|---|
| 0x00823540 | `00823500` | 64 | AI | — | — | 0 | 0 | nop sled, no code | - |
| 0x00823580 | `00823540` | 672 | AU | — | — | 12 | 0 | sub0+1 place[n] | O1 |
| 0x00823820 | `008237E0` | 1072 | AU | — | — | 10 | 0 | — | O6 |
| 0x00823c50 | `00823C10` | 160 | AU | — | — | 1 | 0 | group 0x82a920[n] ×5 | S2 |
| 0x00823cf0 | `00823CB0` | 288 | AU | — | — | 6 | 0 | — | O6 |
| 0x00823e10 | `00823DD0` | 160 | AU | — | — | 3 | 0 | group 0x82a920[n] ×6 | S2 |
| 0x00823eb0 | `00823E70` | 640 | AU | — | — | 11 | 0 | — | O6 |
| 0x00824130 | `008240F0` | 128 | AU | — | — | 2 | 0 | — | O6 |
| 0x008241b0 | `00824170` | 1552 | AU | — | — | 7 | 0 | — | O6 |
| 0x008247c0 | `00824780` | 16 | C | compiled at rev: identical | — | 0 | 0 | code pointer in 0x8247d0 | O6 |
| 0x008247d0 | `00824790` | 992 | AU | — | — | 14 | 0 | — | O6 |
| 0x00824bb0 | `00824B70` | 592 | AU | — | — | 8 | 0 | — | O6 |
| 0x00824e00 | `00824DC0` | 64 | C | compiled at rev: identical | — | 0 | 0 | boot 0x1e7780 (area_dispatch_off1900_state0000) | O2 |
| 0x00824e40 | `00824E00` | 96 | AI | — | — | 1 | 0 | script 0x828d60 op09 record 0x828fa0 | O2 |
| 0x00824ea0 | `00824E60` | 720 | AU | — | ran, 1 beat | 15 | 1 | sub0+1 deferred group 0x826f80[n] | O2 |
| 0x00825170 | `00825130` | 624 | AU | — | — | 10 | 0 | sub0+1 place[n] | O3 |
| 0x008253e0 | `008253A0` | 160 | AI | — | — | 1 | 0 | script 0x8299e0 op09 record 0x829b60 | O4 |
| 0x00825480 | `00825440` | 384 | AU | — | ran, 1 beat | 7 | 1 | sub0+1 place[n] | O4 |
| 0x00825600 | `008255C0` | 800 | AU | — | ran, 1 beat | 12 | 3 | sub0+1 place[n] ×3 | O4 |
| 0x00825920 | `008258E0` | 864 | AU | — | ran, 1 beat | 11 | 1 | sub0+1 place[n] ×3 | O3 |
| 0x00825c80 | `00825C40` | 240 | AU | — | — | 4 | 0 | sub0+1 place[n] | O5 |
| 0x00825d70 | `00825D30` | 272 | AU | — | — | 5 | 0 | sub2 place[n] | S2 |
| 0x00825e80 | `00825E40`, `00825E80` | 320 | AI | — | — | 7 | 1 | call from 0x825d70 | S2 |
| 0x00825fc0 | `00825F80`, `00825FC0` | 240 | AU | — | — | 6 | 1 | call from 0x825d70 | S2 |
| 0x008260b0 | `00826070` | 64 | C | compiled at rev: identical | — | 1 | 0 | script 0x82b080 op09 record 0x82b400 | S2 |
| 0x008260f0 | `008260B0` | 240 | AU | — | — | 7 | 1 | sub2 place[n] | S2 |
| 0x008261e0 | `008261A0` | 240 | AU | — | ran, 1 beat | 4 | 0 | sub0+1 place[n] | O5 |
| 0x008262d0 | `00826290` | 240 | AU | — | — | 4 | 0 | sub0+1 place[n] | O5 |
| 0x008263c0 | `00826380` | 736 | AU | — | ran, 1 beat | 10 | 1 | sub0+1 place[n] | O4 |
| 0x008266a0 | `00826660` | 240 | AU | — | — | 6 | 1 | sub0+1 place[n] | O3 |
| 0x00826790 | `00826750` | 1104 | AU | — | — | 12 | 2 | sub2 place[n] | S2 |
| 0x00826be0 | `00826BA0` | 208 | AU | — | — | 3 | 1 | sub2 place[n] | S2 |
| 0x00826cb0 | `00826C70` | 16 | C | compiled at rev: identical | — | 0 | 0 | script 0x82d070 op09 record 0x82d1f0 | S2 |
| 0x00826cc0 | `00826C80` | 704 | AU | — | — | 6 | 1 | sub2 place[n] | S2 |

Totals (34 functions, 14,912 slot bytes, decomp fa4b42c): AU 26 / 14,112 B, AI 4 / 640 B, C 4 / 160 B.
<!-- area_overview:overlay_functions end -->

Absolute %hi/%lo references of the overlay code (gp-relative globals are not symbolized in the splat pieces and are not listed):

<!-- area_overview:global_refs begin -->
| Address (%hi/%lo symbol) | Referenced by |
|---|---|
| 0x246f20 | 0x8263c0, 0x826790 |
| 0x2758a8 | 0x825c80 |
| 0x2758b0 | 0x8261e0 |
| 0x2758b8 | 0x8262d0 |
| 0x8102b0 | 0x825920 |
| 0x810350 | 0x825e80 |
| 0x810600 | 0x823eb0 |
| scratchpad 0x70003000..0x700038b0 (6 addresses) | 9 functions |
<!-- area_overview:global_refs end -->

D_008102B0 is the player block per FINDINGS s56 (claim). 0x2758A8 / 0x2758B0 / 0x2758B8 (read by [44], [45] and [46]'s owners) lie just after the subs-0/1 deferred list at 0x2758A0 (registries table); what they hold is not established here.

## 9. Scripts

Found by walking every data address the overlay code or data references as a 0x40-byte chain (method: AREA01_OVERVIEW.md section 9). The last column is the dated admission snapshot of the port's `em_area_script.c`.

<!-- area_overview:scripts begin -->
| Entry | Records | Started by | Ops | Not admitted by the port host |
|---|---:|---|---|---|
| 0x8284e0 | 8 | 0x823580 | 06/4 02/0 0B/0 17/0 0B/3 02/0 0B/2 06/3 | 0B/2, 0B/3, 17/0 |
| 0x8286e0 | 4 | 0x823580 | 07/0 10/3 0F/1 06/1 | — |
| 0x828d60 | 30 | 0x824ea0 | 07/8 0C/1 01/1 06/0 00/0 10/4 06/3 0A/1 02/0 09/0 0A/3 00/0 0A/1 02/0 00/1 02/0 00/0 00/1 02/0 00/0 00/1 00/0 02/0 00/1 0A/3 18/0 00/0 01/1 04/8 07/5 | — |
| 0x8294e0 | 19 | 0x825170 | 07/3 00/0 0A/1 0B/0 0B/3 17/0 0B/3 02/0 00/0 0C/1 02/0 00/0 00/1 01/9 00/2 18/0 00/0 01/9 07/4 | 0B/3, 17/0 |
| 0x8299a0 | 1 | 0x825170 | 07/4 | — |
| 0x8299e0 | 8 | 0x825480 | 07/2 06/1 04/8 00/0 0A/1 02/0 09/0 02/0 | — |
| 0x829be0 | 20 | 0x825600 | 13/1 00/0 00/0 02/0 0C/1 17/2 01/4 12/0 03/0 02/0 00/0 17/2 01/2 12/0 02/0 18/0 01/0 0D/1 06/3 07/4 | 03/0, 12/0, 13/1, 17/2 |
| 0x82a0e0 | 17 | 0x825600 | 13/1 00/0 17/2 01/4 12/0 03/0 00/0 17/2 01/4 12/0 03/0 02/0 18/0 01/0 0D/1 06/3 07/4 | 03/0, 12/0, 13/1, 17/2 |
| 0x82a540 | 7 | 0x825920 | 07/0 10/3 0F/1 11/2 10/0 0B/0 07/5 | 11/2 |
| 0x82a720 | 8 | 0x825c80 | 07/2 0C/1 00/0 00/1 02/0 0C/2 0D/5 07/5 | — |
| 0x82abc0 | 19 | 0x825e80 | 16/0 07/12 06/3 0C/1 0A/1 14/0 00/6 02/0 12/0 0D/0 18/0 0A/5 0A/2 01/9 0D/5 06/3 10/0 02/0 07/4 | 12/0, 14/0 |
| 0x82b080 | 27 | 0x825fc0 | 16/0 07/2 06/3 01/9 00/0 00/0 00/0 00/5 10/5 0A/2 00/0 06/3 02/0 10/6 09/0 0A/0 00/0 10/4 02/0 00/5 02/0 10/1 10/5 02/0 00/0 10/0 07/5 | — |
| 0x82b790 | 8 | 0x8261e0 | 07/2 0C/1 00/0 00/1 02/0 0C/2 0D/5 07/5 | — |
| 0x82b990 | 7 | 0x8262d0 | 07/2 0C/1 00/0 00/1 02/0 0D/5 07/5 | — |
| 0x82d070 | 12 | 0x826790 | 07/12 0C/1 10/1 10/0 01/1 14/0 09/0 02/0 12/0 0D/0 18/0 07/5 | 12/0, 14/0 |

Port column: port commit 52caca6 (2026-09-28 05:28), `src/game/em_area_script.c`. Admitted opcodes (its execute() switch): 00 01 02 04 06 07 09 0A 0B 0C 0D 0F 10 15 16 18. Sub rules (dated snapshot of the file at 6e659ac, 2026-09-27; current at this commit): op00 kinds 0-7, 9, 10 (kind 8 and larger kinds fault); op01 kinds 0-7, 9, 10 (kind 8 and larger kinds fault); op0A: sub 8 (001798D0) faults; op0B: subs 0, 4 and 6 (other subs fault at 001B8020).
<!-- area_overview:scripts end -->

On the recorded route (THIRD_LEVEL_ROUTE.md): 0x828D60 is the arrival script (a01_07); 0x8294E0 is door [51]'s locked script (a00_01); 0x8299E0 the terminal's (a00_04, op09 callback 0x8253E0); 0x829BE0 the ferry's trip east (a00_04; 0x82A0E0 the trip back, exploratory run); 0x8284E0 the shaft door's arrival wait. Not played: 0x82A540 (the D_0081075D script of [43]) and 0x8286E0 (the shaft door's progression script). Scripts 0x82A720, 0x82B790 and 0x82B990 (started by [44], [45] and [46]) were not played. Ops the port host does not admit at 52caca6, in the scripts the route played or the progression needs: 0B/2, 0B/3, 17/0 (0x8284E0, 0x8294E0), 03/0, 12/0, 13/1, 17/2 (the ferry's 0x829BE0 / 0x82A0E0) and 11/2 (0x82A540).

## 10. Boot owner behaviours

Every boot function an AREA00 table or overlay code points at. "Reach" = boot functions outside the censuses that ran before AREA00 (section 11) reachable from it. "First-level census row" = the port's row at 52caca6. The last column quotes labels from decomp comments or FINDINGS (claims, unverified), or a route measurement where one exists (marked "route").

<!-- area_overview:boot_roots begin -->
| Behaviour | Bytes | Decomp | Subsystem | Tables | Live (RAM) | First-level census row | Reach | Label (claim, unverified) |
|---|---:|---|---|---|---:|---|---:|---|
| 0x128c10 | 2924 | NM | lowmem | sub0+1 deferred group 0x826f80 ×12; nest group link 0 0x827a90 ×4; nest group link 2 0x827c30 ×3; nest group link 3 0x827ce0 ×2; nest group link 4 0x827d90 ×6 | 12 | in census, no row | 58 | NPC update (decomp SEMANTICS); FINDINGS: bug brain |
| 0x12a5d0 | 2032 | BM | lowmem | sub0+1 deferred group 0x826f80 ×2; nest group link 0 0x827a90 ×1; nest group link 1 0x827ba0 ×2; nest group link 3 0x827ce0 ×1; nest group link 4 0x827d90 ×1; sub2 deferred group 0x828060 ×5 | 2 | no | 79 | FINDINGS: bug brain (nest child) |
| 0x14d260 | 324 | BM | entity_logic | group 0x827ef0 ×1 | 0 | no | 68 | — |
| 0x1551b0 | 5220 | NM | entity_logic | sub0+1 deferred group 0x826f80 ×8; sub0+1 place ×18; sub2 place ×6 | 26 | live (em_crate_original over its roster node (em_area11_boxes.c)) | 38 | crate (port em_crate_original) |
| 0x156620 | 2320 | NM | entity_logic | sub0+1 place ×13; sub2 place ×6 | 13 | live (em_drum_original over its roster node (em_area11_boxes.c)) | 45 | drum (port em_drum_original) |
| 0x156f30 | 1072 | NM | entity_logic | sub0+1 place ×3 | 3 | no | 36 | — |
| 0x1581a0 | 316 | NM | entity_logic | sub0+1 place ×1 | 1 | no | 36 | route (measured, a00_03): a melee hit sets the door-lock bit and frees it |
| 0x158810 | 948 | NM | entity_logic | sub0+1 place ×1; sub2 place ×1 | 1 | no | 33 | — |
| 0x158bd0 | 340 | NM | entity_logic | sub0+1 place ×1; sub2 place ×2 | 1 | no | 32 | — |
| 0x158d30 | 388 | NM | entity_logic | sub0+1 place ×1; sub2 place ×1 | 1 | in census, no row | 31 | FINDINGS s74: creature-family fixture |
| 0x159b90 | 724 | NM | entity_logic | sub2 place ×1 | 0 | in census, no row | 35 | FINDINGS s74: creature-family fixture |
| 0x15a2c0 | 1164 | NM | entity_logic | sub0+1 place ×8; sub2 place ×5 | 8 | in census, no row | 55 | port em_enemy.c names it |
| 0x15ab00 | 244 | BM | entity_logic | sub0+1 deferred group 0x826f80 ×8 | 8 | no | 6 | — |
| 0x15afa0 | 140 | BM | entity_logic | sub0+1 deferred group 0x826f80 ×11; group 0x827f50 ×1; sub2 deferred group 0x828060 ×10 | 11 | live (em_pickup_owner em_pickup_owner_tick via em_pickup.c and the AREA11 interaction host) | 35 | item pickup (port em_pickup_owner) |
| 0x15b030 | 248 | AW | frame_update | group 0x827a30 ×1 | 1 | no | 36 | — |
| 0x1bb860 | 628 | NM | math_vector | sub0+1 place ×1; sub2 place ×1 | 1 | in census, no row | 35 | slider door (FINDINGS s45/s63) |
| 0x1bc350 | 516 | BM | math_vector | sub0+1 place ×2; sub2 place ×1 | 2 | live (em_door_original em_door_original_tick via em_area11_door (node tick_door; census L18)) | 34 | hinged door (port em_door_original) |
| 0x1becc0 | 1788 | NM | math_vector | sub2 deferred group 0x828060 ×3 | 0 | no | 50 | — |
| 0x1bf6b0 | 2268 | BM | math_vector | sub2 deferred group 0x828060 ×3 | 0 | no | 90 | actor update (decomp SEMANTICS) |
| 0x1c06e0 | 2380 | NM | anim_runtime | sub2 deferred group 0x828060 ×2 | 0 | no | 45 | decomp name bone_root_pulse |
| 0x1c1a80 | 636 | NM | math_vector | sub2 place ×2 | 0 | no | 39 | actor state machine with bone array (decomp) |
| 0x1c2420 | 8 | BM | math_vector | sub0+1 place ×3; sub2 place ×1 | 0 | no | 1 | class-0x0B trigger record (8-byte leaf) |
| 0x1c4820 | 156 | BM | math_vector | sub2 place ×5 | 0 | live (em_status_ui_leftovers em_sul_001C4820 at the area11[20] node (em_area11_bindings.c tick_prop_001C4820: 001B0FD0 / 001C6380 / +0x4C through em_area11_boxes_owner_*, 001B17A0 through the host's services)) | 31 | generic placed prop (port em_status_ui_leftovers) |
| 0x1c48c0 | 156 | AW | math_vector | sub2 place ×2 | 0 | no | 31 | — |
| 0x1e3d90 | 2160 | NM | weapon_equip | sub0+1 deferred group 0x826f80 ×17; sub2 deferred group 0x827fb0 ×3 | 7 | in census, no row | 8 | muzzle-flash driver (decomp comment; doubtful: THIRD_LEVEL_ROUTE.md calls its sub-0 nodes the fire row) |
| 0x1e7d20 | 3608 | NM | stream_archive | sub0+1 place ×2; sub2 place ×1 | 2 | in census, no row | 1 | water surface over D_00275C20 records (decomp comment) |
| 0x219550 | 796 | NM | unknown_06 | sub0+1 deferred group 0x826f80 ×2; sub2 deferred group 0x828060 ×1 | 2 | live (em_pickup_owner em_pickup_owner_tick via em_pickup.c and the AREA11 interaction host) | 35 | item pickup (port em_pickup_owner) |
| 0x22dcd0 | 2396 | NM | unknown_07 | sub0+1 place ×1; sub2 place ×1 | 1 | no | 6 | — |
<!-- area_overview:boot_roots end -->

## 11. Static census delta and proposed lanes

**Method.** As AREA01_OVERVIEW.md section 11: roots are every AREA00 overlay function (its direct boot calls and code-pointer references) and every boot behaviour of section 10; the closure follows direct calls, tail jumps and code-pointer references over the decomp's splat tree of the boot ELF. The delta is the closure minus everything that already ran before AREA00: the first-level census (1184 functions), beat 15 and the AREA01 beats in their AREA01 phase (section 0). Indirect calls are not followed. The route check counts the arrival census's new boot functions the static reach does not contain: one, 00113478 (a01_07 f3468).

**Lanes.** The owner-to-lane map is `LANES[0]` in the tool: O = overlay owner families of subs 0/1, C = boot owners of subs 0/1, S2 = sub 2, E = shared. A delta function belongs to the lane of its owners (the direct-call owners when there are any); owners in more than one lane put it in E.

<!-- area_overview:lanes begin -->
Reach 497 boot functions (408 by direct calls only), 337 already in the first-level, beat-15 and AREA01 route censuses, **delta 160 functions / 73,708 bytes** (115 by direct calls only). Sub 0 and the nest reach 111 of them (47,424 bytes). Decomp status of the delta at fa4b42c: BM 97, NM 33, AW 16, AI 13, CL 1. Arrival census (a01_07 from 001AD010): 18 of its 19 new functions are in the static reach. AREA00 route census (build/s87/census/a00_delta.json): 18 of the delta ran on the recorded route; 1 boot functions it records as new are outside the static reach.

| Lane | Scope (neutral) | Overlay functions (n / slot bytes; statuses) | Boot delta (n / bytes) | Delta that ran at the arrival (a01_07 from 001AD010) | Delta that ran on the route | Delta statuses |
|---|---|---|---|---:|---:|---|
| O1 | Shaft door 0x823580 (placement [52], door id 0; subs 0/1) | 1 / 672 B; AU 1 | 1 / 320 | 0 | 0 | NM 1 |
| O2 | Deferred record 0x826F80[25] 0x824EA0, the script-0x828D60 callback 0x824E40 and the overlay init 0x824E00 | 3 / 880 B; C 1, AI 1, AU 1 | 1 / 16 | 0 | 0 | AI 1 |
| O3 | 0x825170 ([51]), the 0x825920 records [41]..[43] and 0x8266A0 ([60]) | 3 / 1,728 B; AU 3 | 0 / 0 | 0 | 0 | — |
| O4 | 0x825480 ([40]; op09 callback 0x8253E0), 0x825600 ([48]..[50]) and 0x8263C0 ([47]) | 4 / 2,080 B; AU 3, AI 1 | 0 / 0 | 0 | 0 | — |
| O5 | 0x825C80 ([44]), 0x8261E0 ([45]) and 0x8262D0 ([46]) | 3 / 720 B; AU 3 | 0 / 0 | 0 | 0 | — |
| O6 | Overlay functions no table, script or overlay call found by the tool reaches (0x823EB0 and 0x824130 are behaviours of the unreferenced group 0x82CDD0) | 8 / 5,280 B; AU 7, C 1 | 1 / 52 | 0 | 0 | BM 1 |
| C1 | Boot owners 0x128C10, 0x12A5D0 (deferred and nest records) and 0x15A2C0 ([61]..[68]) | — | 39 / 24,484 | 5 | 5 | BM 25, NM 7, AW 5, AI 1, CL 1 |
| C2 | Boot owners 0x1581A0 ([54]), 0x158810 ([57]), 0x158BD0 ([59]), 0x158D30 ([53]), 0x156F30 ([34]..[36]), 0x22DCD0 ([39]), 0x15AB00 (deferred records), 0x15B030 (group 0x827A30, from 0x8263C0) | — | 12 / 7,896 | 11 | 11 | NM 7, BM 3, AW 1, AI 1 |
| C3 | Slider 0x1BB860 ([58]) and the class-0x0B records 0x1C2420 | — | 3 / 156 | 0 | 0 | BM 3 |
| C4 | 0x1E3D90 (deferred records) and 0x1E7D20 ([37], [38]) | — | 1 / 68 | 0 | 0 | BM 1 |
| C0 | First-level census owners' unexercised paths | — | 0 / 0 | 0 | 0 | — |
| S2 | Sub-2 owners (placement table 0x82C640, deferred groups 0x827FB0 / 0x828060, group 0x82A920) | 11 / 3,488 B; AU 8, C 2, AI 1 | 46 / 26,024 | 0 | 0 | BM 25, NM 11, AW 6, AI 4 |
| E | Shared engine delta (reached from more than one lane) | — | 56 / 14,692 | 2 | 2 | BM 39, NM 7, AI 6, AW 4 |
<!-- area_overview:lanes end -->

Claims attached to the lanes (route measurements are marked as such; the rest unverified):

<!-- area_overview:lane_claims begin -->
- O1: THIRD_LEVEL_ROUTE.md a00_s0 (measured): before D_0081075D is set it acts as a plain door to AREA01 entry 0; its progression branch (sub-state 6, script 0x8286E0, D_0081075E) is read from the undecompiled instructions (section 2.3 there), not executed.
- O2: THIRD_LEVEL_ROUTE.md section 2.2 reads 0x828D60 as the arrival script (it wrote D_0081075A / D_008107DA in a01_07).
- O3: THIRD_LEVEL_ROUTE.md: [51] played its locked script and set D_0081075B = 1 (a00_01, measured); [43] starting script 0x82A540 (D_0081075D) and [60] reading D_0081075E are read from instructions (section 2.5), not executed.
- O4: THIRD_LEVEL_ROUTE.md a00_04 (measured): a Use at [40] ran script 0x8299E0 (D_0081075C, D_008107DC) and [48] moved east and rose.
- C1: FINDINGS calls 0x128C10/0x12A5D0 bug brains (AREA01 claims).
- C2: THIRD_LEVEL_ROUTE.md a00_03 (measured): a light melee hit on [54] set D_00810841[0] bit 2 and freed the node.
- C3: FINDINGS s45/s63: slider door.
- C4: decomp comments: muzzle-flash driver (0x1E3D90), water surface (0x1E7D20); THIRD_LEVEL_ROUTE.md calls the seven sub-0 0x1E3D90 nodes (z -1473..-1488) the fire row, near which the player lost health in exploratory runs (not attributed to a function).
- S2: THIRD_LEVEL_ROUTE.md section 2.1: 0x826790 ([28] of 0x82C640) ends script 0x82D070 with a move to AREA14; what selects sub 2 is not known.
<!-- area_overview:lane_claims end -->

Delta functions per lane (address, bytes, status at fa4b42c; "arrival" = new in the arrival census, `a00_arrival.json`; "route" = executed in the AREA00 route census, `a00_delta.json`, which is the same arrival phase until the a00 beats are replayed; "port" = the port's `src/game` at 52caca6 names the address, grep only):

<!-- area_overview:lane_deltas begin -->
- **O1** (1): 19C6F0 (320, NM).
- **O2** (1): 10BBF0 (16, AI).
- **O6** (1): 1C6190 (52, BM).
- **C1** (39): 128600 (64, BM, arrival, route, port), 128640 (420, BM, arrival, route, port), 1288D0 (228, BM, port), 129F00 (184, BM), 129FC0 (1540, BM, port), 12A5D0 (2032, BM, arrival, route, port), 12ADC0 (508, NM, arrival, route, port), 12AFC0 (1092, BM, arrival, route, port), 12B410 (1076, NM, port), 12B850 (280, AW, port), 12B970 (1188, BM, port), 12BE20 (1644, BM, port), 12C490 (1540, BM, port), 12CAA0 (1952, BM, port), 12D240 (828, NM, port), 12D580 (708, BM, port), 12D850 (232, AW, port), 12D940 (1064, BM, port), 12DD70 (288, AI, port), 12DE90 (480, NM), 12E070 (60, CL, port), 12E0B0 (420, BM, port), 12E260 (84, BM), 12E2C0 (224, BM), 153ED0 (56, BM), 153F10 (292, AW), 154040 (212, BM), 154120 (832, NM), 154460 (324, BM), 1545B0 (264, BM), 1546C0 (124, BM), 154740 (628, BM), 1549C0 (1344, BM), 154F00 (676, AW), 15A200 (184, BM, port), 15A750 (928, NM, port), 19AA80 (156, NM), 1C24D0 (104, BM), 21BD60 (224, AW).
- **C2** (12): 156F30 (1072, NM, arrival, route, port), 1576E0 (384, BM, arrival, route, port), 1581A0 (316, NM, arrival, route, port), 158810 (948, NM, arrival, route, port), 158BD0 (340, NM, arrival, route, port), 15AAF0 (8, BM, port), 15AB00 (244, BM, arrival, route, port), 15B030 (248, AW, arrival, route, port), 1D0400 (172, AI, arrival, route, port), 1E8E80 (1012, NM, arrival, route, port), 1E9280 (756, NM, arrival, route, port), 22DCD0 (2396, NM, arrival, route, port).
- **C3** (3): 1BB7C0 (40, BM, port), 1BB7F0 (108, BM, port), 1C2420 (8, BM).
- **C4** (1): 1E7C60 (68, BM, port).
- **S2** (46): 11E0A8 (156, AW), 1284E0 (276, BM), 14D260 (324, BM), 14D3B0 (192, BM), 14D470 (384, AW), 14D5F0 (460, BM), 14D7C0 (1132, BM), 14DC30 (1044, NM), 14E050 (624, NM), 14E2C0 (168, BM), 14E370 (176, AW), 14E420 (196, AW), 14E4F0 (328, BM), 14E640 (48, BM), 14E670 (304, AW), 14E7A0 (240, BM), 14E890 (300, AI), 14E9C0 (576, BM), 183010 (116, BM), 1A7B80 (20, BM), 1A7BA0 (2748, NM), 1B4810 (1240, BM), 1B4CF0 (1640, BM), 1B55E0 (424, NM), 1BE5F0 (196, AI), 1BE6C0 (1012, NM), 1BEAC0 (168, BM), 1BEB70 (208, BM), 1BEC40 (128, BM), 1BECC0 (1788, NM), 1BF3C0 (496, NM), 1BF5B0 (128, BM), 1BF6B0 (2268, BM, port), 1BFF90 (64, BM, port), 1C06E0 (2380, NM), 1C1500 (108, BM), 1C1570 (1284, NM), 1C1A80 (636, NM), 1C47E0 (60, BM, port), 1C48C0 (156, AW), 1C63D0 (16, BM), 1D0D60 (444, AI, port), 1F6AD0 (36, BM), 1F91C0 (1172, NM, port), 1FB0B0 (16, BM, port), 21BE40 (144, AI).
- **E** (56): 1000C0 (32, BM, port), 100110 (32, BM, port), 1028E8 (20, AI, port), 11CE20 (2380, NM, port), 11DE60 (48, AI, port), 11DF98 (228, AW, port), 11E148 (352, AI, port), 11E860 (20, BM, port), 128830 (152, BM, port), 1A44B0 (412, AW, port), 1A4830 (1248, AW, port), 1A5C30 (2056, NM, port), 1A7280 (1520, NM, port), 1B1C60 (56, BM, port), 1B1CE0 (56, BM, port), 1B5360 (640, BM), 1C2430 (148, BM, arrival, route, port), 1C2690 (216, BM, port), 1C6160 (44, BM, arrival, route, port), 1CACC0 (356, BM), 1CAE30 (8, BM), 1CAE40 (276, BM), 1CAF60 (8, BM), 1CAF70 (48, BM), 1CAFA0 (188, BM), 1CB060 (8, BM), 1CB070 (184, BM), 1CB130 (8, BM), 1CB140 (164, BM), 1CB1F0 (8, BM), 1CB200 (164, BM), 1CB2B0 (8, BM), 1CB4F0 (144, AI, port), 1CB580 (8, BM, port), 1D39A0 (144, BM), 1D3A30 (144, BM), 1D3AC0 (12, BM), 1D3C40 (152, BM), 1D3CE0 (12, BM), 1D3DA0 (152, BM), 1D3F60 (356, NM), 1D40D0 (12, BM), 1D40E0 (504, NM), 1D42E0 (332, NM), 1D4430 (16, BM), 1D4440 (504, NM), 1D4640 (16, BM), 1D6580 (284, BM), 1D80E0 (32, BM, port), 1EFEB0 (84, BM, port), 1EFFD0 (136, AI), 1F00A0 (124, AI, port), 1F4A00 (8, BM), 1F4E20 (20, BM), 1F9180 (60, AW, port), 1FC580 (348, BM, port).
<!-- area_overview:lane_deltas end -->

Lane notes (manual):

- **O1..O4 are the progression path.** The shaft door (O1), the arrival (O2), door [51] and the north-east room's owners (O3), and the terminal and ferry (O4) are all AU at fa4b42c except the O2 init and callback. Their own boot delta is small (O1 one function, O2 one, O3 and O4 none): what they call directly or by code pointer already ran in AREA11 or AREA01.
- **C1, C2** hold 16 of the 18 delta functions that ran at the arrival (the lanes table). C2's roots 001581A0, 00158810, 00158BD0, 00158D30, 00156F30 and 0022DCD0 are NEARMISS C in the decomp, 0015AB00 byte-matched C and 0015B030 word assembly (section 10).
- **S2** (46 boot functions, 11 overlay functions) is reached only from sub 2; the recorded route never loads sub 2.
- **E** (56) is reached from more than one lane.

### Recommended order

1. **Replay the a00 beats under the census** (section 12). Until then the route data is the arrival only.
2. **Matching lane (decomp only):** the AU owners of O1..O4 (0x823580, 0x825170, 0x825480, 0x825600, 0x825920, 0x8263C0, 0x824EA0), which the route runs; the overlay build must stay 19/19 byte-identical (`tools/verify_all.py`).
3. **Data exporters and translations** as for AREA01 (AREA01_OVERVIEW.md section 11), after the capture lane reaches the progression exit (THIRD_LEVEL_ROUTE.md section 7).

## 12. Route census status and limits

- **Recorded:** the AREA00 phase of the AREA01 exit replay (`runs/A01/a01_07_level_exit.json`, from 001AD010 at f233). `route_census.py a00-delta` (2026-09-28) counts **284 functions** executed there (278 boot, 6 AREA00 overlay by inside points), of which **25 are new** beyond the first level, beat 15 and AREA01 play (**16,080 bytes: 19 boot, 6 overlay**). The 259 that already ran: 239 first-level census, 15 beat 15, 5 AREA01 play. The 19 new boot functions are the same 19 boot functions that SECOND_LEVEL_ROUTE.md section 6 lists under "Only in the exit's area change, load or AREA00 arrival" without a "Beat 15" mark. Details: THIRD_LEVEL_ROUTE.md section 9.
- **Not recorded:** a census replay of the a00 beats (a00_00..a00_04, a00_s0). `route_census.py` now has the segment; the command (hidden PCSX2, one beat per session, about the AREA01 replay's per-frame cost) is `.venv/bin/python tools/route_census.py run --segments a00 --pass A00`, then the `a00-delta` and `area_overview.py` lines of section 0.
- Static tables plus recorded census outputs; the arrival capture is one frame of one route.
- The census delta follows direct calls and code-pointer references only; indirect calls are not resolved.
- Boot statuses use the 2026-09-23 link-route audit with the markers of fa4b42c, joined by address.
- The eight O6 functions have no reference the tool found; how the original reaches them (the unreferenced group 0x82CDD0, or pointers built at run time) is open.
- File roles of `chunk04` and whether n0 and n1 share files byte for byte are open (section 3).
