# Level-4 lane L4MATH: the new boot functions of math_vector, entity_logic, actor_anim, anim_runtime

Level 4 (AREA01 revisit + AREA02) side track, lane L4MATH, 2026-09-28 (session s88). New
files only: `src/game/em_area02_math.{c,h}` (prefix `em_area02_math_`) and
`tools/test_area02_math_reference.py`. The module is built and tested but not wired: nothing
in the port calls it and the Makefile does not list it (the test builds it privately into
`build/area02/math/`). Section 7 says where each routine would bind. Original executable
SHA-256 `ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

## 1. Scope

The rows of `../Extermination/build/s87/census/a02_delta.json` (`new_functions`: what the
revisit and AREA02 run that neither the first level, AREA01 nor AREA00 ran;
FOURTH_LEVEL_ROUTE.md section 9) with region `boot` and census subsystem math_vector,
entity_logic, actor_anim or anim_runtime: **28 rows, 12,988 bytes**. The subsystem names are
census labels, not evidence (most of these rows are node behaviours, not vector math); the
routines are named by address.

### 1.1 Reused: a verified translation already exists (4 rows)

The port was searched for every address before translating. These four already have a
translation checked by an original-instruction oracle and are **reused, not translated
again**; each oracle's default run passed during this lane (section 4.8).

| Function | Bytes | Decomp | First beat | Port translation | Oracle (doc) |
|---|---:|---|---|---|---|
| 00176DC0 | 460 | BM | a02_02 | `em_player_ladder_entry.c` (`em_player_ladder_00176DC0`, bound live through `em_player_closure_live.c`) | `test_player_ladder_entry_reference.py` (PLAYER_LADDER_ENTRY.md) |
| 0017FD40 | 64 | BM | a02_02 | `em_player_ladder_climb.c` (`em_player_ladder_climb_0017FD40`) | `test_player_ladder_climb_reference.py` (PLAYER_LADDER_CLIMB.md) |
| 00180530 | 196 | BM | a02_02 | `em_player_ladder_climb.c` (`em_player_ladder_climb_00180530`) | same |
| 001C47E0 | 60 | BM | a02_s0 | `em_pickup_items_original.c` (`em_pickup_items_001C47E0`) | `test_pickup_items_reference.py` (STATUS_PAGES.md) |

Addresses that the port names but does **not** translate (so they are translated here):
00183C40 (`em_enemy.c`'s `em_enemy_aim_point` is a legacy native aim height, not the
original; `em_weapon.c` cites the address in comments only), 001C8140
(`em_script_door_fan.c` reaches it through a worker, `w_001C8140`), 001CAF60 (an address in
`em_status_models.c`'s method table), 001CB4F0 / 001CB580 (`em_status_models.c` draws the
status-page models natively for them, STATUS_SCENE.md; that is a render stand-in, not a
translation of the call sequence).

### 1.2 Translated here (24 rows, 12,208 bytes)

| Function | Bytes | Decomp | Census subsystem | First beat | Where it runs (captures, code scan) |
|---|---:|---|---|---|---|
| 001575E0 | 244 | NM | entity_logic | a02_s0 | callback word of the script block D_00247FA0 (+0x44) |
| 00157B30 | 432 | BM | entity_logic | a01r_03 (arrival) | called by 00159620 (the bed) |
| 00157F30 | 44 | BM | entity_logic | a02_s0 | callback word at 0x247F64 (script block D_00247EE0) |
| 00158050 | 100 | BM | entity_logic | a02_04 | callback words at 0x246DA4, 0x247A24 |
| 001582E0 | 336 | NM | entity_logic | a02_00 | behaviour of AREA02 node [21] 0x7AD780 |
| 00158EC0 | 840 | BM | entity_logic | a02_00 | behaviour of the battery panel [24] 0x7AE050 |
| 00159620 | 840 | BM | entity_logic | a01r_03 | behaviour of the bed [0] 0x7A5F10 (AREA02 sub 1) |
| 00159970 | 532 | NM | entity_logic | a01r_03 | behaviour of [2] 0x7A64F0 (AREA02 sub 1) |
| 00159E70 | 324 | BM | entity_logic | a02_05 exit | behaviour of AREA04 node 0x7B3E50 |
| 00183C40 | 676 | BM | actor_anim | a02_02 | called by 0017B058 and 00185BF0.. (aim code) |
| 001B18F0 | 568 | NM | math_vector | a02_00 | called twice by the AREA02 overlay (file offsets 0x1DC8, 0x1E34) |
| 001BC960 | 2076 | BM | math_vector | a02_05 exit | behaviour of AREA04 nodes 0x7B3870 / 0x7B3B60 |
| 001BD560 | 1168 | NM | math_vector | a02_05 exit | behaviour of AREA04 nodes 0x7B3290 (+3 0x0B) / 0x7B4140 (+3 0x0D) |
| 001BDFC0 | 1584 | BM | math_vector | a02_05 exit | behaviour of AREA04 nodes 0x7B4720 / 0x7B4A10 |
| 001C48C0 | 156 | AW | math_vector | a02_05 exit | behaviour of AREA04 node 0x7B6D50 |
| 001C4960 | 152 | AW | math_vector | a02_00 | behaviour of AREA02 nodes 0x7B23E0 / 0x7B26D0 / 0x7B29C0, AREA04 0x7B87C0 |
| 001C4AF0 | 172 | AW | math_vector | a01r_03 | behaviour of [1] 0x7A6200 (AREA02 sub 1) |
| 001C4CB0 | 752 | BM | math_vector | a02_00 | behaviour of AREA02 node 0x7AEF00 (+3 2), AREA04 0x7B84D0 (+3 3) |
| 001C7EB0 | 652 | NM | anim_runtime | a01r_01 | behaviour 001C8140 installs (the event's spawned nodes) |
| 001C8140 | 124 | BM | anim_runtime | a01r_01 | called from inside 001BAC00 (one call, at 0x1BAC4C) |
| 001CAE40 | 276 | BM | anim_runtime | a01r_03 | only through 001CAF60 |
| 001CAF60 | 8 | BM | anim_runtime | a01r_03 | node method (+0x4C) of 0x7A6200 and AREA04 0x7B26D0 |
| 001CB4F0 | 144 | AI | anim_runtime | a02_s0 | only through 001CB580 |
| 001CB580 | 8 | BM | anim_runtime | a02_s0 | a node method (+0x4C) word; no pool node of the captures carries it (the census saw it on a02_s0's item page) |

BM byte-matched C, NM NEARMISS C, AW word assembly, AI inline assembly (the decomp's
statuses). For NM / AW / AI rows the translation follows the original instructions (the
splat listings of the pinned ELF, read locally); for BM rows the C and the original agree.
"Where it runs" comes from the pool nodes of the end-of-beat captures (their +0x10 behaviour
and +0x4C method words) and a scan of the boot ELF and the 19 overlay files for direct calls
and data words; a label in a comment was not used.

## 2. Model

- **Memory.** Original EE memory through caller-supplied regions keyed by original address
  (RAM and the scratchpad). No routine hands a stack address to a callee or builds a local
  in memory, so no stack region is needed; each routine still lowers `sp` by its original
  frame (0x10 .. 0x50; 00157F30 has none, 001CAF60 / 001CB580 continue into their target
  with the entry `sp`) so that every callee sees the original stack pointer.
- **Calls.** Every call leaving the module goes through the one worker `call` with the callee
  address, the stack pointer and the argument registers the original sets up (a0..a3 as
  64-bit images of the 32-bit values, f12..f14 raw bits; at least the ones the callee reads,
  section 4.2). A node method is the word at node +0x4C and 001C7EB0's callback the word at
  node +0x2EC; both are called through the worker with the loaded pointer as `fn` and a0 =
  the node. 00159620 calls the translated 00157B30 directly; 001CAF60 / 001CB580 continue
  into the translated 001CAE40 / 001CB4F0 with a1 = word +0x44.
- **Whole registers.** The routines test a callee's whole 64-bit v0 wherever the original
  does: 001B0FD0, 001BA1F0, 001BC740, 001BD180..460, 001B1020, 001BA1C0 (== 0 / != 0),
  00157860 / 00157CE0 (compared with 2 and 3), 001AFA90 (001C8140), 001CA7B0 (001CAE40: the
  sign bit 63, zero, bit 0). 001CB4F0 passes 001D2910's whole v0 as a1 of its last
  001D2830. 001B1630's result is stored as a byte and its low byte tested.
- **Register hand-overs.** The original keeps a1 = 1 through the whole of 001BD560's state
  1; after 001BD270 (keyed sub-state 3) and 001BD460 (plain sub-state 3) it passes on the a0
  and a1 those leaves leave: 001BA1A0's a0, and the byte stored to D_0081076C. Both leaves
  write neither register (checked statically by the test, `check_leaf_keeps`), so the
  translation uses s (node + 0x1F0) and 1.
- **Arithmetic.** Every float operation is the EE one through `em_ee_float.h`: ADD.S
  (001575E0's 11 + y, 00183C40's 6 + y), NEG.S (001B18F0), the compares C.EQ.S / C.LT.S
  (00157B30, 001BD560, 001BC960, 001C7EB0).
- **Quadwords.** 001C7EB0 copies four quadwords from the pose record; each load and store
  ignores address bits 0..3, all 16 bytes are read before any is stored, in the original
  order, and the word +0x10C (inside the last quadword) is stored after them.
- **Fail-stop.** An address outside every region, a NULL worker, a negative worker result, a
  NULL region list: the first fault is latched (code, entry, address together, also under a
  re-entrant worker), the call returns -1 and every later call returns -1 before any work
  until `em_area02_math_clear_fault`. A NULL context returns -1 without latching. Bytes
  written before the faulting access stay written.
- **Store trace (test builds only).** `EM_AREA02_MATH_STORE_TRACE` names a function that
  receives every store (address, size); `wr` / `copy_qw` are the only store paths.

## 3. What each routine does

The full statements are the comments above each translation; short forms. "Mask bit k" is
bit (k & 31) of the room-mask byte D_00810841[D_00810700]; s = node + 0x1F0.

- **001575E0(node)** script callback: SPR 0x700038A0 = (10, 0, -5, 1); 001B6F00(a0 = node,
  a1 = 0x700038A0, f12 = pi/2); then D_008105E0..EC = (D_00810350, 11 + D_00810354,
  D_00810358, 1.0) with 0x700038A0 = (20, 14, 15, 1); 001026A0(D_008105D0, node + 0xD0,
  0x700038A0). Returns 1.
- **00157B30(node, a1)** the use test: 0 without bit 2 of +0x0B. Else D_00810374 =
  001B1240(D_00810350; +0xB0, +0xB8); D_00810C84 == 0 (the count of item 0x20, FOURTH_LEVEL_ROUTE.md
  section 1): selector 0x8000000E in D_00247E74, script D_00247E20, result 2; D_0081085C ==
  0.0 and D_00810858 not below 100.0: with bit 0 a sound 0x3EC and script D_00247FA0, else selector 0x80000020 and
  D_00247E20, result 2; else bit 0: +0x0A = 1, result 3; else selector 0x8000000E in
  D_00247F34, script D_00247EE0, result 1. Each script start is 001BA1A0(a1, block) then
  001BA1F0(node); results 2 set byte +0 = 2.
- **00157F30(node)** D_008106B0 = 4, D_008106D0 = word +0x14, +0x0A = +0x0B = 0, +0 = 1; 1.
- **00158050(a0)** SPR 0x700038A0 = the position of node (word (word a0+0x1C)+0x1C), w = 1;
  00102948(D_008105E0, 0x700038A0); 1.
- **001582E0(node)** state 0: mask bit (halfword +0x2E) set (low byte of the mask only) ->
  +4 = 3; else 001B0FD0, 001C6380, +0 = 1, 001F1110(node, 2). State 1: halfword +0x36
  nonzero -> +0 = +4 = 2, mask bit (byte +0x2E) set, D_00810842 |= 2, sound 0x3F1; then
  001F1180, 001B17A0, the method. States 2, 3: 001AFC10.
- **00158EC0 / 00159620 / 00159970** the use machines (state 0 init with 001C5570 effect
  points; state 1 sub-states 0..6 around 00157860 / 00157B30 / 00157CE0, 001BA1A0 scripts
  and 001BA1F0 polls; then 001B17A0 or 001B1B70 and the method; state 2 +4 += 1; state 3
  001AFC10). The comments list every sub-state.
- **00159E70(node)** state 0: 001B0FD0 == 0 -> 001C6380, halfword +0x28 = 0x29 (area 4) or
  0x2A. State 1 sub 0: item count D_00810C64[signed +0x28] nonzero -> +5 = 1, the float +0x78
  of the record at word (word D_00275B40)+4 = pi/2, mask bit (byte +0x2E) set, 001C6380;
  then 001B17A0. Every sub-state then the method. States 2, 3: 001AFC10.
- **00183C40(p, out)** aim point: (byte +2 & 0x1F) != 2 -> p+0xB0..B8; else by byte +3:
  the +0xC0 triple of the record at +0x118 (0, 3, 4, 7, 8), +0x11C (1, 2), +0x130 (5), +0x124
  (6), +0x120 (10), +0x148 (11), pointer re-read per component; 16 -> y + 6.0; 18 -> (0, 6,
  0, 1) through 001026A0 with the matrix (word +0x110) + 0x90; others the position.
- **001B18F0(node, a1, a2)** four probes (a1, -a1, a2, -a2) through 001026A0 with the node
  matrix +0xD0 into 0x70003600 and 001B1630(f12..f14); byte +1 = each result; the first
  nonzero low byte gives 001B1B70(node) and 1; else 0.
- **001BC960 / 001BDFC0** door-pair behaviours (state 0 area / room / story set-up, state 1
  the shared sub-machine 0..7 with 00158590(node, open, 0 / 1), 001BC740, partner bytes
  +0x0B through word +0x18 chains, a 300-frame timer at +0x28; 001BC960's state 2 waits on
  the mask, SPR halfword 0x70003B84 == 0x208, D_0081076A / D_008107EA and the D_008106C0
  record (byte +4 < 2 and +0xB0 < 554.0); state 4 the script). Comments give every test.
- **001BD560(node)** state 0: scales +0x80..88 = 1.5 / 2.0 / 1.0 by bits 0x40 / 0x80 of
  halfword +0x56, +0x34 = byte +0x2E, +0x2E = 0; state 1 the keyed (+3 == 0x0B, with the
  SPR word 0x70003258 and D_0081076C) or plain machine, then 001C6380, the method, and the
  18.0 proximity test on (word +0x114)+0x80 -> +1 = 1, 001B1D20.
- **001C48C0 / 001C4960 / 001C4AF0** states 0..3 (001C4960 sends state 2 and every value
  above 3 to 001AFC10, the other two do nothing above 3; 001C4960's init is 001B1020(node,
  byte +0x0D, -1, 0); 001C4AF0's is 001CA5E0(node, word +0x44, 1)).
- **001C4CB0(node)** state 0 -> 1; state 1 by byte +3: 2 -> 001BA1C0(node, 9), else six
  001F4E40 points from the float table D_00250F40 when D_00810761 == 1; 3 -> 001BA1C0(node,
  12), else two points from D_00250F90; 4 -> one point (-17.457, 18.783, -11.908) through the
  matrix of the node at +0x18 when its halfword +0x2E is nonzero.
- **001C7EB0(node)** state 0: one bone slot per count byte +0x0C (set to 1) from 001AF780
  (unless the signed D_00275BCC is below the count: +4 = 3), 001CB5B0, the callback,
  001C63E0; state 1: the kill test through word +0x24, the countdown +0x2E4 and 001C64F0,
  the pose copy from word (word D_00275B40) (+0xC0.. position, four matrix quadwords), and
  the callback when 001C64F0's result has bit 12 or 13 (+4 = 3) or a pose scale is at least
  0.9.
- **001C8140(a0, a1, a2)** 001AFA90(0x0C); a nonzero node gets +3 = 0x0C, +0x0D = 0x63,
  halfword +0x94 = a1, +0x40 = a0, behaviour 001C7EB0, +0x24 = 0, +0x2E8 = 0.5, +0x2EC = a2.
- **001CAE40(node, a1)** 001CA7B0(node + 0xB0; the float at (word +0x44)+0x20 or 20.0); a
  non-negative result runs 001D8C20(1), 001C7420(node, 0x3F5, 3), 001D3CE0(a1) for odd
  results else 001D3AC0(a1), one display record at word (word D_00275670)+0x1C (byte +3 =
  0x60, word +4 = 0, halfword +0 = 0, pointer advanced by 0x10), 001D8C20(0) and
  001CAAC0((word D_00275B44)+0xB0, the pointer read before 001D8C20(1)).
- **001CB4F0(a0, a1)** 001D2910(0), 001D2830(0, 0), 001D8C20(1), 001C7420(a0, 0x3F5, 0),
  001D1F80(0, 1, 0), 001D38F0(a1), 001D8C20(0), 001D2830(0, the first result).

## 4. How it was verified

`tools/test_area02_math_reference.py`. Default run 9.8 s of CPU (user; 14.7 s wall at a
load average of about 185 from other lanes, 4.7 to 6.4 s wall earlier at lower load, the
default 2 worker processes); `EM_TEST_FULL=1 EM_TEST_JOBS=4` 248 s of CPU, 161 s wall at the
same load (66 to 79 s wall at lower load).

### 4.1 Oracle and captures

FallEE (the shared interpreter with the measured COP1 / VU0 model) runs the ORIGINAL
instructions over the end-of-beat captures `../Extermination/build/s87/route_a01r/`
(a01r_00, a01r_01, a01r_03) and `route_a02/` (a02_00, a02_02, a02_04, a02_05, a02_s0). The
native module runs over a copy of the same bytes. The design is lane A00WORLD's
(docs/AREA00_WORLD.md section 4, itself lane EXITB's and SYS's), reused as a copy of its
generic machinery with this lane's policies and cases. The test asserts that the scratch
area 0x01E00000..0x01E10000 is zero and the pose pointer chain (D_00275B40 -> 0x8102F0 ->
0xD689C0) is the same in every capture used.

### 4.2 Callees

- **Run as original on both sides** (the native side in a second interpreter over its own
  memory): 001026A0, 00102948 and 001B1240 (pure).
- **Stubbed** (recorded, scripted result, default 0): the other 45 direct callees and the
  two method / callback targets used by the cases (0x1CAA00, 0x1CAF60). The routine under
  test is never hooked (0x1CAF60 is both).
- **Register reads.** RegScan (read-only reuse from `test_area01_sys_reference.py`) measures
  what each callee reads before writing; the test fails if a callee reads an argument
  register its policy does not compare, or reaches an unresolved indirect jump other than
  the documented script command 9 handler (001B99F4, reached from 001BA1F0 and from
  00157860 / 00157CE0 / 001BC740 through it; AREA01_SYS.md section 4). Measured: 001B1020,
  001C5570, 001FB9F0 read a0..a3; 00158590, 001C7420, 001CA5E0, 001D1F80, 00157860 a0..a2;
  001BA1A0, 001BBD20, 001BC740, 001C63E0, 001CAAC0, 001D2830, 001F1110, 00102948, 00157CE0
  a0..a1; 001BA1C0 a1 only; 001B6F00 a0, a1, f12; 001B1240 a0, f12, f13; 001B1630
  f12..f14; 001C64F0 and 001CA7B0 a0, f12; 001F4E40 a0..a2, f12; 001AF780 and 001CB5B0
  nothing; the rest a0. Where the original leaves more registers than the callee reads
  (00159970's 001B0FD0 / 00157CE0 with a1..a3 = s, 2, 3; 001582E0's 001B0FD0 with a1..a3 =
  the mask bit values), the translation passes only the read ones.

### 4.3 Lockstep and variants

At every call leaving the module the native side must match the callee, `sp`, every
compared argument register, every register the native says it sets, and all of RAM and the
scratchpad (the lines either side stored to since the previous check). The same check after
the last store, then all 32 MiB, the scratchpad, the call log and the result. Every base case
also runs as the variants fx-after, fx-before, prestore (not for 00157F30, 00158050, 00183C40:
no stubbed call), poison, edge-byte and edge-top. A variant whose change sends the original
outside the 32 MiB (a flipped pointer) is counted as not comparable and skipped.

### 4.4 Cases

2,081 base cases (and 12,258 variants), all built from the captures plus written bytes.
Per routine (total / in the default run): 001575E0 11 / 5, 00157B30 88 / 22, 00157F30 7 / 4,
00158050 5 / 3, 001582E0 46 / 22, 00158EC0 53 / 36, 00159620 31 / 28, 00159970 30 / 20,
00159E70 35 / 21, 00183C40 64 / 40, 001B18F0 25 / 14, 001BC960 637 / 153, 001BD560 264 / 117,
001BDFC0 532 / 115, 001C48C0 9 / 6, 001C4960 20 / 10, 001C4AF0 10 / 7, 001C4CB0 21 / 18,
001C7EB0 124 / 53, 001C8140 15 / 9, 001CAE40 37 / 10, 001CAF60 6 / 6, 001CB4F0 8 / 8,
001CB580 3 / 3. Highlights:

- every state byte 0..5 and 0xFF of every behaviour, and every sub-state including the
  values past each jump table (7 / 8 / 9), on the captured nodes of section 1.2 and their
  captured state;
- every stub result class: 0, 1, the values 2 / 3 / 4 the callers compare, -1, a v0 with
  only its upper word set (1 << 32), 2 | 1 << 32 (a 32-bit compare would see 2), 1 << 63
  (001CAE40's sign test);
- 00157B30 over its three flags and the float bytes D_0081085C / D_00810858: +-0, a
  denormal, 100.0 and its neighbours, +-exponent-255 patterns (C.EQ.S / C.LT.S as the EE
  compares them); 00159620's sub-state 0 through each 00157B30 result;
- the room-mask tests with shift counts 0..31 and above (0x13, 0x1F, 0x21, 0x103: only the
  low five bits shift; a count of 16..31 must not reach the low byte), the byte / halfword
  width of +0x2E (0x1FF against the 0xFF test), bit 7 of the mask byte, D_00810842 with bit 0
  set and clear;
- the door pair: state-0 area / room / byte +3 combinations (areas 0x0D, 4, 7, 8, 2, 1 and
  rooms 0, 1, 7, 0x0A), the partner chains through +0x18 written in scratch (bytes 0 / 1 / 3
  on either link), byte +3 = 2, the D_0081083E / story flags in areas 4, 7 and 2, the timer
  +0x28 at 1, 2, 0, -0x8000 and 0x7FFF, 001BC960's state 2 with 0x70003B84 = 0x208 / 0x209 /
  0x1208, D_008106C0 null or a record whose byte +4 is 0..3 and whose +0xB0 is 553 / 554 /
  555 / -1e9;
- 001BD560: the +0x56 bits, both machines (and +3 = 0x8B), +0x0B values 0 / 2 / 3 / 4,
  D_0081076C 0 / 1 / 0xFF, D_0081083E / D_00810836 / D_00810837 at 0, 1, 2 and 0xFF, areas 4 /
  7 / 2, the proximity float at 17, 18, 18 - 1 ulp, 19 and -0;
- 00183C40: every +3 value 0..19, 0x7F, 0xFF under +2 = 2 and 0xE2, and 0x22 / 0x12 / 3 / 0x42;
  out overlapping the node (p + 0xB0, p + 0xB4: each component stored before the next is
  read) and out on the +0x118 slot pointer (the re-read follows the stored x);
- 001B18F0: results whose low byte is zero but not the whole value (0x100), 1 << 32,
  +-0 / exponent-255 / denormal input bits (NEG.S), a1 inside the scratch vector, a2 == a1;
- 001C7EB0: D_00275BCC against the count (731, 0, 1, -1, 3), allocator results written over
  the count, kill masks through word +0x24, countdowns 0 / 1 / 2 / -5 / 0x80000000, 001C64F0
  results with each of bits 12 / 13, the three 0.9 tests (0.9 itself, one ulp below), a
  misaligned pose record, pose quadwords overlapping the node, and a node whose +0xB0 is the
  pose pointer word 0x8102F0 (the pointer is re-read after each store);
- 001C8140 with an allocator result 0, 1 << 32, 1 << 32 | node and a node at the end of RAM;
  001CAE40 with and without the +0x44 record and a display record that aliases the D_00275670
  +0x1C word; 001CB4F0 with a 64-bit 001D2910 result.

### 4.5 Result of the full run

`EM_TEST_FULL=1 EM_TEST_JOBS=4` (2026-09-28): **2,081 cases and 12,258 variants identical to
the original**; 33,590 callee entries compared in lockstep; 28,924 fx field changes; all 245
conditional branches of the 24 routines taken both ways (UNREACHABLE is empty). 56 variants
were not comparable (a flipped pointer sent the original outside the 32 MiB).

### 4.6 Default run

Every case with `pin` (a representative set per routine and path), the variants of the
base cases in `VPIN`, the branch-cover cases and the mutant killers in `QUICK_EXTRA`: 730
of 2,081 cases and 113 of 12,258 variants, 2,242 callee entries, all 245 branches both ways,
and every non-equivalent mutant of section 5 killed.

### 4.7 Static checks and the API

Every direct call target of the 24 routines has a policy; the callee register reads match
(4.2); `check_leaf_keeps`: 001BD270 (0x1BD270..0x1BD370) and 001BD460 (0x1BD460..0x1BD560)
contain no call and no instruction writing a0 or a1 (the hand-over of section 2). API
checks, natively: NULL context; NULL regions (NULL latched at 0 for 001C8140); NULL worker
and a worker error (latched at 001AFA90); the latch refuses while set; clear_fault; a
re-entrant worker whose inner 00157F30 faults on an unmapped D_008106B0 keeps (3, 0x157F30,
0x8106B0); an unmapped first store (001C8140 with a new node outside the region, at node +3);
an output left alone on a fault; a NULL output pointer allowed.

### 4.8 The reused oracles

Default runs, 2026-09-28, all passing: `test_player_ladder_entry_reference.py` (5,000 of
40,000 cases, 124 branches both ways), `test_player_ladder_climb_reference.py` (14,000
001662D0 and 5,000 helper cases, 378/378 branch outcomes; 0017FD40 and 00180530 are among its
helpers) and `test_pickup_items_reference.py` (60 of 240 001C47E0 cases). Those oracles run
over first-level captures; none was re-run over the a01r / a02 captures by this lane.

## 5. Mutation sweep

One bounded sweep of 124 single-change mutants of `em_area02_math.c` (the driver and the
mutant sources were scratch and deleted afterwards), each run with
`EM_AREA02_MATH_FAILFAST=1` against the default run. They cover every routine: constants,
masks, shift widths, comparison senses and widths (32-bit tests of 64-bit results, C.LT.S as
C.LE.S, C.EQ.S as a bit compare), swapped or dropped stores, calls and arguments, stores
moved across calls, loads cached where the original re-reads (pointer chains, the count
byte, the pose pointer, the +0x0B byte after the method), the quadword alignment, the
register images, the fault latch.

- First pass: 102 of 124 killed. 18 of the 22 survivors were test gaps; the cases added
  for them (section 4.4: shift count 0x13, D_00810842 = 0x40, out = p + 0xB4, +0x2E = 0x1FF,
  byte +3 = 2 with an unready partner, the toggles over 2 / 0xFF, the misaligned pose and the
  pose pointer alias) and the pins in `QUICK_EXTRA` (bit 7 of the mask, 2 | 1 << 32, index
  -1, a prestore variant of the shut path, 0x70003B84 = 0x1208, D_00810761 = 2, D_00275BCC
  = 1, the fx-after variant of the allocator loop, the 0.9 test with a callback, 1 << 63)
  kill them. **Final: 120 of 124 killed by the default run.**
- The 4 survivors are equivalent:
  - E1 / E2 (+0x2E read as a byte in 001582E0's state 0, as a halfword in state 1): only
    the low five bits reach the shift, and they are the same in the byte and the halfword.
  - G3 (00159620 treats every 00157B30 result >= 3 as 3): 00157B30 (translated here and
    verified) returns only 0..3.
  - L11 (001BC960 reads word +0x18 at the sub-machine instead of at entry): no store or call
    lies between the two points.

## 6. Decomp notes (for the lead; no decomp file was edited)

- **001575E0** (NEARMISS C): the C calls `func_001B6F00(&D_700038A0, pi/2)`. The original
  leaves the node in a0 and passes the vector in **a1** (with f12 = pi/2); 001B6F00 reads a0,
  a1 and f12. The C's first argument is wrong (the port's lane-EXITB policy for 001B6F00 also
  compares a0 and a1).
- **001582E0** (NEARMISS C): state 0 calls `func_001B0FD0(bit, sh, 1)`; the original's a0 is
  the node (a1..a3 hold the mask values the C passes), and 001B0FD0 reads a0 only, so the C
  passes the wrong first argument. State 1 reads +0x2E as a halfword in the C and a byte in
  the original; only the low five bits reach the shift, so this one is not behavioural.
- **00159970** (NEARMISS C): `func_001B0FD0(anim, 2, 3)`, `func_00157CE0(anim, 2, 3)`,
  `func_001BA1F0(anim, 2, 3)` and `func_001AFC10(anim, 3)` all have a0 = the **node** in the
  original (anim = node + 0x1F0 is a1; 2 and 3 are a2 / a3 left from the state compares).
  00157CE0 reads a0 and a1; the others read a0. The C passes the wrong a0 in four calls.
- **001C7EB0** (NEARMISS C): `n` is uninitialised in the C when the countdown does not run
  001C64F0; the original clears that register at entry, so n = 0 (no 0x3000 bits).
- **001BD560** (NEARMISS C): behaviour agrees; the C's `func_001BA1A0(sub, ...)` after
  001BD270 / 001BD460 is right because those leaves keep a0 (section 2).
- **001C4960** (word assembly): unlike 001C48C0 / 001C4AF0, state 2 and every state above
  3 go to 001AFC10.
- **001B18F0** (NEARMISS C): no behavioural difference found.

## 7. Binding

Nothing calls these entries yet. Each is `int em_area02_math_XXXXXXXX(EmArea02Math *s,
<32-bit arguments>, <optional out>)` over EE-memory regions (RAM and scratchpad); a binding
supplies the scene's original-byte storage, a worker for the callees of section 4.2 (to
verified translations or fail-stop stand-ins) and the entry `sp`. The worker must route a
method / callback call by its loaded address: 0x1CAF60 to `em_area02_math_001CAF60`, 0x1CB580
to `em_area02_math_001CB580`.

- **Node behaviours** (001582E0, 00158EC0, 00159620, 00159970, 00159E70, 001BC960,
  001BD560, 001BDFC0, 001C48C0, 001C4960, 001C4AF0, 001C4CB0, 001C7EB0): the AREA02 / AREA04
  pool update, when those loads exist in the port (overlay tables name them; section 1.2).
- **Script callbacks** (001575E0, 00157F30, 00158050) and 001C8140: the script runner's
  worker (`em_script_door_fan.c`'s `w_001C8140` is the existing slot for 001C8140, today NULL
  for the opening).
- **00183C40**: the aim code (0017B058, 00185BF0..); `em_enemy_aim_point` is a legacy native
  stand-in, not this routine.
- **001B18F0**: the AREA02 overlay lane.
- **001CAF60 / 001CB580**: node methods; `em_status_models.c` draws the 001CB580 records
  natively (a render stand-in); binding the call sequence there is a lead decision.

## 8. Known gaps

- **Stubbed callees are not verified here** (45 direct callees and the two method targets); their behaviour is other lanes'
  work. The scripted results include values outside their real ranges (upper-word-only
  64-bit values, -1, 0x80000000, 1 << 63).
- **Saved registers.** 001BA1F0, 00157860, 00157CE0 and 001BC740 can read s4 / f20 on the
  script command 9 path (the RegScan finding of AREA01_SYS.md); these routines never write
  s4 / f20, so the callee sees the caller's; the native module does not model them.
- **Captured coverage.** The inputs are end-of-beat images plus synthetic state bytes; they
  are not the exact frames at which the census first saw each routine. 001C7EB0 / 001C8140
  run on synthetic nodes (the event's spawned nodes are freed by the a01r_01 end).
- **Method and callback targets.** Only 0x1CAA00 and 0x1CAF60 are used as targets; any
  other target must get a policy.
- Not wired; the Makefile does not build the module (isolation rule of this lane).

## 9. Reproduce

```sh
# port repo root
python3 tools/test_area02_math_reference.py                                  # default (~10 s)
EM_TEST_FULL=1 EM_TEST_JOBS=4 python3 tools/test_area02_math_reference.py    # every case and variant
EM_AREA02_MATH_ONLY=1BD560 EM_AREA02_MATH_GAPS=1 python3 tools/test_area02_math_reference.py
EM_AREA02_MATH_SOURCE=<mutant.c> EM_AREA02_MATH_FAILFAST=1 \
    python3 tools/test_area02_math_reference.py                              # one mutant
```
