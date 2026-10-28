# AREA00 first-visit binding

2026-10-13, written on branch `level3` (based on port `d86f73c`, worked in
the `../extermination-port-level3` worktree, merged into main 2026-10-14). AREA00 now loads through the shaft exit,
but **arrival and first world frames do not pass**. That checkpoint stopped at
arrival frame 0 because its 579-key texture catalog exceeded the
then-protected 512-slot graphics registry. The 2026-10-18 capacity follow-up
is below; historical smoke rows have not been relabelled as passing. On the
2026-10-13 `level3` branch no protected file, decomp source, README or user
test runner was edited, nothing was merged or pushed, and no emulator was
launched. On the 2026-10-18 `levels` branch, protected-file edits stayed
within the authorized object-texture registry exception, and fork
recordings were made through the decomp's hidden, run-locked `route_capture`.

## Current state

**2026-10-28, iteration 17 (port main b4a4147 and `pretrans-a00` merged):
the C side runs a00_09 to frame 13225** (674 rows; a00_00..a00_08 unchanged),
where a bug's sub-state machine `0012C490` faults (node 0x7BDFE0). Not the
first unit: from row 152 the a00_09 rows differ (creature 0x71 dies late),
and from about row 462 the player is hit and the rows diverge (`hp`, `pos`,
`req`), so the stop sits inside a diverged run. The creature drift
(iteration 16) is the next unit.

- **`001F6FB0`, the glow node, plays through:** its source block
  D_0025D800 (exported, `CREATURE_EFFECT_WINDOWS`; mapped writable in the
  aim/fire composition when present, the glow rewrites +0x10..+0x18), its
  parent's work block (read-only copy, the halfword +0xF4) and the spark
  matrix 0x700036E0..0x7000371F (write-first storage in
  em_aim_fire_runtime) are views. docs/AREA00_PRETRANS.md stop S2.
- **`001ECFB0` (byte-matched decomp C), effect subtype 3, is bound** as an
  em_effects_live handler beside 001EEBA0 / 001EEEB0 (its sources
  D_002571B0 + 0x90 k exported). Stop S1.
- The 0x257870 window grows to 0x2D0 bytes (001EF510's tables D_00257990 /
  D_00257A20 / D_00257AB0, stop S3, adjacent); em_effects_live takes up to
  96 windows (65 exported). A private `effect_tables.emet` is rebuilt from
  scratch (`rm assets/effect_tables.emet` first) because a window changed
  size.

**2026-10-28, iteration 16: the C side plays a00_00..a00_08 and 429 rows of
a00_09, and stops at a00_09 frame 12980** (the creatures' glow node
`001F6FB0`, now bound but faulting on its parent's +0x2E4; next unit).
Ignoring `lock_r54`, a00_05..a00_08 are exact row for row (a00_04's eight
camera rows as in iteration 15). The first differing row is now a00_09 row
152: the original sets taken bit 0x71 there (creature 0x71, 00128C10 state
3, 001B1190 then 001AFC10), the port 57 rows later; and bit 0x1B (crate)
at row 193, the port at 200.

- **The spat hazard's hit test reads the cell record's class.** 001E2E80
  tests the byte +0x1A of the record a 0019A570 hit names (D_700030B0 +
  0x1A = 0x700030CA); the composition's region views now carry that
  halfword (read only) beside +0x1C / +0x20. This was the a00_06 frame 9758
  stop.
- **`001F6FB0` (NEARMISS decomp C, em_area00_fx_glow.c, now in the
  build),** the glow node 00129FC0's 001EFE00(0x8000000F, creature) makes,
  ticks through em_aim_fire_runtime's other-tick hook and
  em_aim_fire_world_live (binding row in em_area11_bindings). Its parent's
  header, +0x60, +0x80 and +0xB0 are the pool's fields and its +0xD0 a
  read-only copy from the AREA01 actor view
  (`em_scene_bindings_actor_snapshot`).
- **Why the creatures diverge (open).** Fork pool dumps (hidden, owned
  sessions, the iteration-14 probe replaying each beat from its recorded
  source state; 0 row differences) show the AREA00 bugs 0x6A / 0x6E already
  differ from the native ones at a00_04 row 0, and the native rand() stream
  is 266 draws ahead of the original's at a00_05 row 0 and 615 at a00_09
  row 0 (the creatures' headings, 00128C10 sub-state 1's
  001B1470(2 pi (rand & 0xF0) / 256), come from it). Over a00_05 the
  per-caller totals agree except 001F4BF0 (the creature glow 001288D0 ->
  001F4A00 -> 001F4BF0, one draw per call): original 327, native 218,
  because a glowing creature is not drawn (+1 = 0) on frames where the
  original's is. Receipts: ignored `build/frontier/fork/a00_00..a00_04`
  (pool.jsonl, rand_calls.bin), `build/frontier/i16c2` (native class-2
  dumps, a temporary EM_DIAG_CLASS2 build).

**2026-10-28, iteration 15 (branch `drawbridge`): the C side plays
a00_00..a00_05 and 181 rows of a00_06, and stops at a00_06 frame 9758**
(001E2E80 reads the hit record's +0x1A at 0x700030CA, which the composition's
views do not carry; iteration 16). Not a capture pass. Ignoring `lock_r54`,
every compared row of a00_00..a00_05 is exact except a00_04 rows 1546..1553
(8 rows: camera `eye` / `tgt` about 0.4 off, then equal again; open
fidelity unit). The a00_04 frame 6540 stop is gone:

- **`001EFFD0` (byte-matched decomp C), the creatures' spray spawn,** is
  bound in `em_area01_light_live.c` over em_area00_fx's translation:
  001EF9D0 through the host (em_effects_live), then node +0xB0 / +0xC0
  copies, +0x20 = f12 and the halfword +0x94 = the tag. The +0x20 word now
  has a home: `EmActor.w20` (no pool function writes it; only the 001AF8E0
  memset clears it), serialized by `em_actor_pool_record_image`, owned by
  the AREA01 actor view (+0x00..+0x23) and mapped by the aim/fire pool view
  and `em_effects_live_node_field`. `test_area01_light_live_reference.py`
  runs the original 001EFFD0 with 001EF9D0 scripted (a free record or 0,
  tags 0xA / 0xB) and compares RAM, scratchpad and v0.
- **`001E2E80` (byte-matched decomp C), the spat hazard (entity
  0x80000008, subtype 3),** ticks through em_aim_fire_runtime's other-tick
  hook and em_aim_fire_world_live (em_area00_hud's translation over the
  composition's views; binding row in em_area11_bindings). Its callees:
  `001D04B0` with an ELF source block (`em_effects_live_001D04B0_elf`;
  D_00253740..D_00253A10 exported by `export_area00_runtime_tables.py`
  HAZARD_WINDOWS), `001F02C0` (em_area00_fx), and `0019AA80` (byte-matched
  decomp C) over the one probe state with `001A7280` against the player
  record (em_coll_grid_hull; the player's hull chain, resource 0x3A at
  D_0028A578[0] = 0xD1B9C0, exported by the new `tools/export_player_hull.py`
  from the user's extract and checked against 12 captures; 0x700031D4 names
  the player as 0x008102B0).
- **The hazard's two effects, `001EEBA0` / `001EEEB0` (byte-matched decomp
  C; entities 0x80000029 launch / 0x8000002A hit),** run as em_effects_live
  subtype handlers over em_area00_hud's translations; their descriptor
  windows D_00257510 (0x1B0) and D_00257870 (0x120) are exported
  (HAZARD_EFFECT_WINDOWS; the captures' rows D_00257530 / D_00257650 hold
  either the ELF words or 001EEEB0's stores, which the exporter checks).
- Assets: this worktree's `assets/` is a private directory (symlinks to the
  primary checkout's entries, private regular files `effect_tables.emet`,
  63 windows, and `player_hull.emom`).
- Receipts: ignored `build/frontier/i15e/` (run log, ticks, rand trace).

**2026-10-21, iteration 14 (port main 74e6c04 plus the creatures' ground
probe): the C side plays a00_00..a00_03 and 143 rows of a00_04, and stops
at a00_04 frame 6540 on the unbound effect spawn `001EFFD0`.** Not a
capture pass. Ignoring `lock_r54` only (no longer `progress`), every
compared row of a00_00..a00_04 is exact. Before this change the run reached
a00_09; the earlier stop now comes from creatures that behave as the
original's do, which reach workers the drifting ones never did.

- **The cause of the AREA00 creature divergences (the taken bits 0x65 in
  a00_05 and 0x71 / 0x72 in a00_09, and the row-462 attack): `001C25E0`
  dropped its result.** A fork replay of the a00_05 and a00_09 beats from
  their recorded source states with the recorded inputs (0 row
  differences, disposable probe, receipts below) dumped the pool every
  frame: the original's creatures 00128C10 / 0012A5D0 stay in sub-state 0
  at their placed positions (on the floor at y -59.6, on crates at -38.1,
  -33.7), while every native one entered sub-state 8 when visible and
  drifted (it fell off the crates: y -38.1 against -59.6). Sub-state 0 sends
  a visible creature to 8 when `001C25E0(e, (0, -1.4, 0, 1))` is 0. A
  native trace of `0019B4C0` showed it returning the original's hit mode
  (4, the floor face 0x01516DEC, the same scratch words as the original
  instructions over the fork's a00_00 RAM), but `em_area01_math_001C25E0`
  never handed it back: the decomp C declares `001C25E0` void, yet nothing
  after its last call writes v0, and the original returns 0019B4C0's v0
  (the original instructions return 4 over the same RAM). The translation
  and the runtime dispatch now return it; `test_area01_math_reference.py`
  compares it (`v0=True`; a translation returning 0 fails at
  `unit:a01_07_level_exit:0x7acbc0`, original v0 4). The live hand-back in
  `em_area01_runtime_dispatch.inc` has its own check:
  `tools/test_area01_runtime_reference.py` runs 001C25E0 through the
  runtime over the a01_07 capture (24 cases: 4 creature nodes x 6 probe
  vectors, hits and misses, every host worker call as original code) and
  compares RAM, scratchpad and the call's v0 with the original
  instructions. A dispatch that drops the hand-back fails at node
  0x7AC2F0, (0, -1.4, 0) (v0 4 expected).
- **`001C24D0` (byte-matched decomp C), the re-aim probe that 0012B970 /
  0012BE20 / 0012C490 run,** now bound in the live composition
  (`em_area01_light_live.c`, over em_area00_world's translation; its three
  callees forwarded; its v0 is 0019AD00's, passed through because nothing
  after that last call writes v0, and those callers ignore it): `make test-area01-light-live-reference` runs the original
  instructions with 001026A0 / 001028B8 as original code and 0019AD00
  scripted (v0 0, 1, 2, 4, 2^32, -1, the high word only), comparing each
  call, the record and scratchpad at each, memory after and the result;
  mutants that drop the v0 or pass a1 for a2 are rejected.
- **`0021BD60` (byte-matched decomp C), the grab test 0012E0B0 calls on
  the player record,** bound the same way (one read-only region, the
  record's bytes +0..+0x236): 104 quick cases (all tested state bytes)
  against the original leaf; a mutant returning 0 is rejected.
- **D_00281F30, the ten cue records,** are now in the AREA00 / AREA01
  runtime's bytes (`em_area01_live.c`, em_stream_live's storage): the
  creatures' 001FC580 (em_area00_low) reads and writes the record its id
  selects (0x1AA / 0x1B5: +0x20). This was also iteration 13's stop.
- **The stop: a00_04 frame 6540**, "owner 0012A5D0 node 007AD490 failed at
  001EFFD0" (byte-matched C: 001EF9D0(kind, a, 1.0), then node +0xB0 / +0xC0
  copies, +0x20 = f12, +0x94 = the tag). Its translation is
  `em_area00_fx_001EFFD0`; binding it needs a home for the effect node's
  +0x20 word, which `em_effects_live_node_field` does not map (next unit on
  front B; check which subtype handler reads it first).

The first level is unchanged up to the AREA01 arrival: the first-level
smokes' tick logs equal 74e6c04's for host 13,754 of 13,813 lines and drive
14,045 of 14,104, and the RNG traces and present logs are identical. The
last 59 lines (the AREA01 arrival frames, drive 14045..14103) differ in two
keys, `owner_units` (59 lines) and `snd` (55 lines), and both come from
the four AREA01 creatures 0x7A8830 / 0x7A8B20 / 0x7A8E10 / 0x7A9100
(00128C10):
  - `owner_units`: they now stay in sub-state 0 and draw (2,128 bytes)
    instead of drifting in sub-state 8 and drawing nothing.
  - `snd`: in 74e6c04 the four drifting creatures ran 00128C10's
    sub-state-8 call 001C2770(e, b, 2), whose not-grabbable branch plays
    001FBD50(e, 0x1AC, 0, 300.0). A native trace (temporary, env-gated,
    removed) shows those four calls at sound tick 13919 and a fifth on
    0x7A8E10 at 13964; no other sound-service call differs between the
    two builds.
    They keyed four voices (8..11, tracks 3..6 = sound 0x1AC, serials
    615..618) and later a fifth; every later `snd` difference is the same
    sounds on shifted voice slots and serials (cur / ser 587 / 619 against
    583 / 615). The new build makes none of these calls.
  - The original makes none either. A hidden fork replay of route 15 from
    its recorded source state with the recorded inputs (all 809 rows equal
    to the recording) logged every original call of 001FBD50 and 001C2770
    (inline log probes on their entries) and dumped the four records.
    00128C10 calls 001C2770 (return address 0x129520) on each of its six
    AREA01 creatures 0x7A8250 .. 0x7A9100 once per tick from tick 754 on,
    always with flags 0, the not-sub-state-8 call; it never passes flags 2.
    No 001FBD50 call in rows 0..809 has id 0x1AC; the arrival rows are
    749..809. The four records are 00128C10 creatures whose sub-state +5
    is 0 at row 749, 1 at 750 and 0 from 753 to 809, never 8. The
    original's 001FBD50 calls in the arrival window are ids 0x411..0x413
    (return address 0x1FC4D8) and one 0x19C (0x1556F0). So the new build's
    arrival sound state is the original's, and 74e6c04's extra voices came
    from the drift bug. Receipts are in
    `build/frontier2_i14_20261021_0300/fr_snd/`.
Both smokes
and their checker pass, newgame-control 9.599849;
`tools/test_level_smoke_area01.py --until a01_07` passes 28 live phases
(403 of 597 drive-replay reads served, as before). Receipts in ignored
`build/frontier2_i14_20261021_0300/`.

**2026-10-21, iteration 13 (port main bb4f5e9 plus the bug probe and the
knife strike): the C side plays 472 rows of a00_09 and stops at frame
13023; rows 0..461 are exact (ignoring `lock_r54` and `progress`), rows
462..471 are not.** Not a capture pass. In the order the run met them:

- **The stop at frame 12884 was a use-after-free, not a missing
  worker.** The bug 0012A5D0's probe (0019B4C0 flags 4 -> 0019CF50,
  NEARMISS decomp C; `em_area01_sys.c` `f_19CF50`) read its face list
  tables 0x70003210.. (in the collision file at 0x01512554..) as
  garbage (face index -4795, then 0x2C9E: the face 0x015C94EC). The
  original's tables there hold indices 0..768 in every AREA00 snapshot.
  Cause: the module loader's read of module 0x1D (sub 1's static bank, at
  D_0028A5A0 = 0x015CE380, inside room 0's resident region) copied the
  region's untouched prefix to new storage and freed the old one, which
  the collision view (bound at the arrival) still read. A diagnostic run
  dumped the view's file bytes: 112,917 of 137,216 bytes differed from
  the original's RAM. Two changes: `em_module_loader.c` `region_for`
  keeps the untouched prefix's storage (the bytes are unchanged); the
  collision view re-resolves its file mapping from the loader on every
  access (`EmArea01CollisionHost.file_rest`, bound in
  `em_area01_live.c`), so its extent ends where the bank begins. Checks:
  `make test-module-loader-replay` (a read inside a region: the prefix
  keeps its storage and bytes; the old loader fails it with an ASan
  heap-use-after-free), `make test-area01-collision-view-reference` (a
  re-mapped file is read through the current mapping, an absent mapping
  refuses; a view that keeps the bind-time mapping fails it).
- **The knife strike `00189FE0` (byte-matched decomp C) on the first
  bug** (a00_09 row 395, after the Square at row 383): kind 2 passes the word at
  (word 0x700031D0) + 0x1C to 001B41F0. After an entity hit the record is
  D_700030B0, so the word is 0x700030CC (the hull walkers' word). The
  aim / fire composition's views now include 0x700030CC and 0x700030D0,
  read only, when the record word is 0x700030B0
  (`em_aim_fire_world_live.c` `refresh`; `make
  test-aim-fire-world-live`: the strike's +0x36 follows the word (0x9000
  with 1, 0x1000 with 0); the old binding faults at 700030CC).
- **The bug's debris, model 0x06** (effect node 001F2BA0 -> 001F3E30:
  `D_0025A350` rows 2 / 10 / 19 / 23 have table 0x37, +0x54 = +0x58 = 6;
  the run's model handle 0x00BCB340 is entry 6 of D_0028A56C):
  `export_roger_banks.py` grows the span 0x07..0x0F to 0x06..0x0F (model
  0x06 precedes 0x07 in file order; the region count stays 16; the
  non-library regions are unchanged). The region equals RAM in all 16
  AREA11, 16 AREA01 and 12 AREA00 captures; `export_area00_library.py`
  re-applied after it.
- **`001CD390` (NEARMISS decomp C) from the piece's 001F3620 state 2**
  (001CD390(0x700036A0, piece + 0x50)): `em_aim_fire_sdk_memory.c` runs
  em_effect_original's translation (read from the .s) over the out matrix
  and the scratchpad 0x70003600..0x7000363F it builds the rows in;
  refused for an unaligned out or an out / v inside that block.
  `test_aim_fire_sdk_memory_reference.py`: 72 cases (8 with signed zeros
  and the x = z = 0 branch, 64 random) against the original instructions,
  the out matrix and all 16 scratchpad words, poisoned starts, 3
  refusals; a mutant that skips the last scratch quadword and one that
  tests v.y instead of v.z are both rejected. No dispatcher handled
  0x1CD390 before (every caller faulted), so no earlier path changes.
- **Rows 462..471 differ: the recording's player enters state 11 at row
  462 (a bug's attack: `player` (11, 59, ..) and `req` byte 3 = 1); the
  native player does not** and the positions drift from there. Not
  established: the likely cause is the kept divergence below (the
  recording's bug at record 49 dies at row 152, the native's at record
  50 at row 244), so the attacking bug's state differs; that needs the
  fork trace of the two bugs before any change.
- **The stop: a00_09 frame 13023** (row 472), "owner 0012A5D0 node
  007BD710 failed at 00281F50": the bug's runtime reads D_00281F30 + 0x20
  (the stream cue table, mapped for the aim / fire target sound but not in
  the AREA00 / AREA01 runtime's bytes). It lies after the row-462
  divergence, so it is not the next unit by itself.
- **Next unit on front B:** a00_09 row 152 (the taken bit 0x71: why the
  original's bug at record 49 dies there and the native's does not),
  from a fork trace of records 49 / 50 over rows 100..160; then rows 462+.

Windowed against route_a00 ignoring `lock_r54` and `progress`,
a00_00..a00_03 and a00_05..a00_08 stay exact, a00_04 differs only in the
shake rows 1546..1553, and a00_09's rows 0..461 are exact. The first-level
smokes' tick, RNG and present logs equal port main bb4f5e9's (identical
SHA-256 to iteration 12's receipts); newgame-control 9.599849. Receipts in
ignored `build/frontier2_i13_20261021_001202/`.

**2026-10-20, iteration 12 (port main c40818b plus the barricade crate's
break): the C side plays 333 rows of a00_09 and stops at frame 12884, in
the second crate child's collision probe.** Not a capture pass. In the
order the run met them:

- **The crate's nest children, `001AFA90` + the record copy** (001551B0
  state 2 at 00155FC0, NEARMISS decomp C; `001AFA90` byte-matched). Crate
  uid 2201 (model 0x50) has link 3: `D_0024D820[0]` = 0x8284C0, row
  `D_0024A850[0]` + 3 = 6, three records (puids 0x7D, 0x7E, 0x7F; class 2;
  behaviours 00128C10 / 0012A5D0 / 00128C10: the bugs). `em_area11_boxes.c`
  `h_spawn` now allocates from the canonical pool
  (`em_actor_pool_alloc_001AFA90`) and copies the fields in the original's
  order (+0x9A, +0x03, +0x2E, +0x0D, +0x9D / +0x9E or +0x0E, +0x54, +0x56,
  +0xB0.., +0xC0.., +0x10); the child fields themselves are
  `em_crate_original`'s, checked against the original instructions by
  `test_crate_original_reference.py` (`oracle_child`). The new binder hook
  `em_area11_boxes_bind_children` (bound with the registry in
  `area01_bind_live`) gives the child the AREA00 / AREA01 runtime binding
  (`area00_bind_deferred`'s); without it a child still fail-stops (AREA11).
- **Its taken bit, `001B1190`** (byte-matched): `h_set_taken` calls
  `em_area11_bindings_001B1190` (the verified `em_gun_rest_001B1190`).
  The run sets bits 0x1C (row 192), 0x1B (193) and 0x1D (266), each at the
  recorded row.
- **The break lift `001EBC30`** (effect 0x80000014, subtype 8; NEARMISS
  decomp C): wired into `em_effects_live.c` (`handler_001EBC30`, the
  translated `em_area00_hud_001EBC30`, 0x70003400 as the module's
  `spad3400_lift`). Its sources `D_00256280` / `D_00256310` are exported
  (`export_area00_runtime_tables.py` `BOX_BREAK_WINDOWS`, equal to the ELF
  in all 13 retained AREA00 snapshots). `make
  test-area00-box-lift-reference`: 96 packet chains (576 with
  `EM_TEST_FULL=1`, 12 captures) against the original instructions, whole
  RAM and scratchpad, both +0x38 values, two refusals.
- **The debris models 0x1F / 0x21** (effect 0x8000000B's node `001F2BA0`,
  subtype 1: `D_0025A350` row 1, table 0x37): `export_roger_banks.py`'s
  `BREAK_SPANS` now holds 0x1F..0x22 as one span (with the husk 0x22;
  still 16 regions), equal to RAM in all 16 AREA11, 16 AREA01 and 12 AREA00
  captures; `export_area00_library.py` re-applied after it.
- **The bug's sub-state machines** (0012A5D0, em_area01_exita, by +5):
  `em_area01_live.c` `low_entry` dispatches em_area00_low's 0012B410,
  0012B970, 0012BE20, 0012C490, 0012CAA0, 0012D240, 0012D850, 00129FC0 and
  0012E070 (`make test-area00-low-reference`, full sweep 2,141 cases); the
  run reached 0012D850 (word assembly).
- **`001027E0`** (asm-word SDK leaf) from 0012ADC0: `sdk_001027E0` over
  `em_rvr_001027E0` (`test_render_verify_rest_reference.py`).
- **The stop: a00_09 frame 12884** (row 333): the bug 0012A5D0 at node
  007BD710 calls `0019ED80(0x70003190, 0x015C94EC)`; 0x015C94EC lies in
  the static cell data (`D_0028A5A8` = 0x1523380 + 0xA616C), not in the grid
  hull nodes, and the collision view refuses it (code 3). Next unit on
  front B: 0019ED80 over a static cell record.
- **Divergence kept (not new):** a00_09 row 152, the recording sets taken
  bit 0x71 (the bug 00128C10 at record 49 dies); the native run does not,
  and at row 244 sets 0x72 (the bug at record 50). Hidden in the windowed
  counts because `progress` is ignored with `lock_r54`; also present in
  iteration 11's 191 rows.

Windowed against route_a00 ignoring `lock_r54` and `progress`, a00_00..
a00_03 and a00_05..a00_09 (333 rows) are exact and a00_04 differs only in
the shake rows 1546..1553, as in iteration 11. The first-level smokes'
tick, RNG and present logs equal port main c40818b's; newgame-control
9.599849. Receipts in ignored `build/frontier2_i12_20261020_212528/`.

**2026-10-20, iteration 11 (port main 8284849 plus AREA00's sub-area
switch): the C side plays a00_08 to its end, aligns a00_09 and stops 191
rows into it, at the first barricade crate's break.** Not a capture pass.
In the order the run met them:

- **Op 0x11, `001B7700` (NEARMISS decomp C).** `em_area_script.c`'s
  `execute` now sends op 0x11 to the area binding's handler, as ops 0x03,
  0x05, 0x0E, 0x12, 0x13, 0x17 and 0x1A; the AREA01 / AREA00 script
  worker already ran `em_area00_low_001B7700` (`make
  test-area00-low-reference`: 12 of 12 conditional outcomes). Record
  0082A600's mode word 2 stores D_008106CE = 2 and D_008106CF = 0x81 and
  raises D_00275BD8; the classifier `001AE7E0` (byte-matched) then returns
  3 and the frame machine enters state 6 (a00_08 row 362, as recorded).
- **State 6's `001FF030` (byte-matched decomp C).** `em_scene_bindings.c`
  binds it (state 6 only) to `em_area00_low_001FF030` over the scene
  state's bytes (`em_area01_scene_view`): D_00810730[0] = 0x81,
  D_00810701 = 1, D_00275BD8 = 1, and its tail `001FF080(0, 0x1D)` is the
  loader task's request. `001FEFE0` (D_008106CE other than 2:
  `001FF080(2, 0)`, the untranslated bank-set streamer `00200360`) stays
  unbound. The SFX registry scope follows D_00810701 there
  (`em_sfx_set_area`; 001FB9F0 pages its ids by D_00810700/701). State 6's
  return calls `001FAE70(1)`, now bound (`w_001FAE70`).
- **Module 0x1D.** Its header (read locally from the user's disc) names
  no chunks and no sections, a 1,873-sector payload and one relocation,
  slot 0x44 (D_0028A5A0) at offset 0: 001FF830's kind 2 delivers it at
  D_0028A5A0 and relocates the slot onto it. The exporter's
  `SUBAREA_MODULES` adds it to the module pack (MODULE_LOADER.md section
  4; 57 ranges, 43,201,712 bytes; ignored, never committed).
  Its 0x3A8800 payload bytes equal, byte for byte, the last 0x3A8800 bytes
  of AREA00 room 1's resident region (offset 0x1ED800; checked locally
  on the user's disc; nowhere in room 0's): AREA00_ASSETS.md's two nested
  blocks differ only in id 0x44, the static bank, so module 0x1D is sub
  1's static-object bank; the grid, the cell directory and the model bank
  are shared. After the load the first
  original reader is state 6's `001C1DC0` (001C1E70 -> 001D52E0 reads the
  bank's grid header): there `w_001C1DC0` rebinds the render context's
  copy of the bank from the drive's delivered bytes and selects the
  object-texture catalog and the shadow receivers of AREA00 sub 1 (no GS
  upload: module 0x1D has none).
- **The load's timing: the comparison-only drive replay now covers the
  module loader's bank loads** (IOP_STREAM.md "Drive replay"; the user's
  decision of 2026-10-19 covers each disc read). The a00_08 recording's
  slot-2 record (sampled once per tick, the task dispatching once per
  tick) shows the header read issued at row 363 and done at row 370 (6
  busy polls) and the payload issued at row 372 and done at 448 (75); no
  stream read is in flight then (the stream table's reads at counters
  40047 and 40172), so the gate D_00282157 is clear. `tools/drive_replay.py
  loader_reads` derives these "L" entries from the fork trace and the
  disc's module header; the AREA00 harness adds them to its table; the
  loader takes an entry with its file, sector and count within 2 counters
  (`em_module_loader_set_drive_replay`; `make test-module-loader-replay`).
  The run: state 6 at row 362, sub-step 1 at 363, D_00275BD8 clear at
  451, state 1 at 452, all as recorded.
- **The switch owner's puffs `00823820`** (func_overlay_AREA00_008237E0,
  byte-identical C; `em_area00_ovl_00823820`, `python3
  tools/test_area00_overlay_reference.py`: 22 cases, 235 of 235 reachable
  words). The switch 00825920's `001EFD20` (a00_08 row 456) allocates an
  effect node whose callback is this AREA00 owner; the effect binder only
  knew AREA11 rows. `em_area11_bindings_set_spawn_fallback` now offers an
  unknown callback to the scene's area fallback: in AREA00 a resident-overlay
  callback of `area01_rebind`'s AREA00 list runs through the same AREA00
  runtime as the roster's owners; elsewhere nothing changes. Its packets'
  sources D_008287E0 / D_00828870 are overlay tables: `001CFBE0` through
  `em_effects_live_001CFBE0_bytes` now keeps an otherwise unmapped
  overlay source block by address (as 001D04B0's), so the chain page's REF
  reads them.
- **The stop: a00_09 frame 12742** (row 191, counter 39759), "001551B0
  at record uid 2201 faulted": the barricade crate's break reaches
  `001AFA90` and its nest child copy (`em_area11_boxes.c` `h_spawn`,
  unbound). Next unit on front B: that child spawn.

Windowed against route_a00 (owners included), the counts before a00_08
are those of iteration 10 (a00_00 242/363 .. a00_05 655/1,160); ignoring
`lock_r54` and `progress`, a00_00..a00_03 and a00_05..a00_09 (487 and 191
rows) are exact row for row and a00_04 differs only in the shake rows
1546..1553. The first-level smokes' tick, RNG and present logs equal port
main 8284849's (sha256 2866e0de... / f8a37cb8... full, d2cb9bed... /
2fa87dc2... drive switch); newgame-control 9.599849;
`tools/test_level_smoke_area01.py --until a01_07` passes 28 live phases
with the replay's AREA01 counts unchanged (403 of 597 served). Receipts in
ignored `build/frontier2_i11_20261020_184329/`.

**2026-10-20, iteration 10 (port main b804f8c plus AREA00's switch movie):
the C side plays a00_08's movie and stops three frames after it, at the
area script host's op 0x11.** Not a capture pass.

- **`\MOVIE\E006S1.PSS`, selector 2** (the ELF's name table
  D_00264FB0[2], read locally: E900 / E001 / E006S1 / E007 at entries
  0..3). Exported from the user's disc with `tools/export_movie.py
  --disc-path /MOVIE/E006S1.PSS --out assets/movies/e006s1.mov` (STARTUP.md
  step 70; ignored, never committed): 741 pictures 640 x 480, 24.7 s of
  video, 27.7 s of 48 kHz PCM. `em_frontend.c`'s `k_movies[2]` names it.
- **Its field-clock facts, measured on the fork** (decomp
  `build/fork_refs/probes_1020_e006s1/`, its `tools/probe_e006s1.py` and
  `analyze_e006s1.py`; hidden, run-locked, the a00_08 beat replayed from
  a00_07's end state with the recorded inputs and pad correction, all 363
  replayed rows equal to the recording): 741 field-0 / field-1 send pairs,
  none repeated; the first wait's field 0; 002036E0's entry at vblank 359,
  the gate at 372; the last regular field-1 send 1,851; the feed loop's end
  1,976 (`feed_end` 1,604); the 2 held pictures sent from 1,978 after a
  127-vblank stall (D_007A55F8 125); the play loop's end 1,981; the closing
  waits on fields 0 and 1 (1,982 / 1,983). `k_movies[2]` = {741, 2, 1604};
  `tests/movie_field_test.c` reproduces every one of those offsets from the
  gate (`make test-movie-field`, which also decodes the export's 741
  pictures in order).
- **The level smoke skips it** after 2 s (the frontend's test skip, 63
  pictures, as for E900 and E001; the fork recording played it whole). The
  game sees one main-loop step either way: a00_08 rows 356 -> 357 and the
  rows after it compare exact.
- **The stop: op 0x11 (`001B7700`, NEARMISS decomp C) at a00_08 frame
  12515**, "001BA1F0 of 00825920 faulted at 0024D8C4 (record 0082A600)":
  the dispatch read of `ftab_0024D880[0x11]`; `em_area_script.c`'s
  `execute` has no case for it. Its translation `em_area00_low_001B7700`
  and the AREA01 / AREA00 script worker for it already exist (the binding
  admits `001B7700`). Record 0082A600 (a00_07's end snapshot): mode word 2,
  byte +0x14 = 1, so the command sets D_008106CE = 2, D_008106CF = 0x81 and
  D_00275BD8 = 1, then waits for D_00275BD8 == 0. The game task's
  classifier `001AE7E0` (byte-matched) then returns 3 and the frame machine
  enters state 6 (`em_scene_frame.c`, the unported arm): `001FF030(0x81)`
  (em_area00_low's checked translation: D_00810730[area] = 0x81,
  D_00810701 = 1, D_00275BD8 = 1, then 001FF080(0, 0x1D)). The recording
  shows the result: the area bytes become 00 / 01 at a00_08 row 363
  (counter 40082), six rows after the movie, AREA00 sub 1. Next unit: the
  op 0x11 dispatch, state 6's `w_001FF030` / `w_001FEFE0` and the sub-1
  load they start (module 0x1D through the loader task).

Windowed against route_a00 (owners included), the counts before the movie
are unchanged from iteration 9 (a00_00 242/363 .. a00_05 655/1,160) and
a00_08 now compares 360 rows (to the stop); ignoring `lock_r54` and
`progress`, a00_00..a00_03 and a00_05..a00_08 are exact row for row and
a00_04 differs only in the shake rows 1546..1553. The first-level smokes'
tick, RNG and present logs equal port main b804f8c's (sha256 2866e0de... /
f8a37cb8... full, d2cb9bed... / 2fa87dc2... drive switch); newgame-control
9.599849. Receipts in ignored `build/frontier2_i10_20261020/`.

**2026-10-20, iteration 9 (port main e88dc26 plus the workers a00_05 ..
a00_07 reach): the C side plays a00_05, a00_06 and a00_07 and aligns
a00_08; the stop moved to a00_08's movie.** Not a capture pass. In the
order the run met them:

- **`001288D0` (byte-matched decomp C, src/func_001288D0.c), 00128C10's
  glow** (a00_05 frame 9075: "owner 00128C10 node 007AC5E0 failed at
  001288D0"). `em_area01_live.c` now hands it, as `0012D580` already is,
  to its checked translation `em_area00_low_001288D0` (`make
  test-area00-low-reference`) over the live composition's byte views; its
  callees re-enter the runtime. Its tail call `001F4A00` (byte-matched,
  `src/func_001F4A00.c`: a frameless jump to `001F4BF0`) is served by the
  flame services as `001F4BF0` on the caller's stack pointer and
  arguments. `make test-area01-glow-services-reference` now also runs
  `001F4A00` through the services against the original instructions
  (`001F4A00` -> `001F4BF0` -> `00122BB8` / `001CD520`): every capture
  with 001288D0's two colour sets (byte +0xE1 zero / non-zero), the
  controlled clips and three refusals (quick 36 tail-call bodies); a frame
  of 0x50 instead of 0x40 for the tail call fails it.
- **Camera actions 9 and 11 (`00198CE0`, byte-matched; `00198F10`,
  NEARMISS) outside AREA11** (a00_07 frame 11842: "FAULT at 00198CE0").
  With the AREA01 / AREA00 camera worker set, `em_camera_live.c` bound only
  mode 1's `001B0300` and action 10 (forwarded). The recording runs the
  duct's 9 -> 10 -> 11 -> 0 (a00_07 rows 888, 1034, 1035, 1098; player
  states 44 / 45 / 46): 9 and 11 are now bound there to the same
  `low_run` as in AREA11 (CAMERA_LIVE.md section 5); 14 stays unbound.
- **`001B0460(1)` from the player closures** (a00_07 frame 11988: "player
  closure: reached 001B0460 (camera re-init)"). `0016D130` state 13 and
  `0016DE40` states 0 / 0x17 (both byte-matched C) re-seat the camera in
  area 0 after placing the player. `em_player_closure_live.c` binds both
  slots to `em_scene_bindings_001B0460` with the record's +0xA0 quad
  (D_00810350, the previous feet the stage keeps there), +0xC0 quad
  (D_00810370, the heading the state just stored) and +0x230; the 10 / 12
  / 19 / 1A closure publishes its scene copy first (0016DE40 stored
  D_00810702 = 5 and D_008106BE = 2 in it) and reloads D_00810702,
  D_008106BE, D_008106B5..B8 and D_008106C8 after.
- **001B0080's rotation 0x70003B50.** The first such re-seat (a00_07 row
  1035) seated the camera 90 degrees off: `rw_001B0080` read the scene
  state's spad3B40[4..7], 001B07C0's store at the AREA00 arrival. Only
  inside 001B07C0 does 001B0080 see that store; otherwise 0x70003B50 holds
  0015BCF0's tail copy of +C0..+CC (byte-matched src/func_0015BCF0.c),
  which the camera already loads per entry (`C.s3B50`). `EmCameraLiveRoom`
  now carries the quad: the spawn binding passes spad3B40[4..7], the script
  host and the closures NULL (the tail publication). Rows 1035..1036 are
  then exact.

Proof. The first-level smokes' tick, RNG and present logs equal port main
e88dc26's (sha256 2866e0de... / f8a37cb8... full, d2cb9bed... /
2fa87dc2... drive switch); newgame-control 9.599849; `make
test-level-smoke-full` and `make test-level-smoke-ps2-drive` pass;
`tools/test_level_smoke_area01.py --until a01_07` passes 28 live phases.
The AREA00 run's tick log equals the previous build's line for line up to
its faulting tick (34,744 lines), and the 0x70003B50 change alters nothing
before a00_07 row 1035. `make test-area00-low-reference`,
`test-area01-glow-services-reference`, `test-area01-flame-services-reference`,
`test-camera-live-reference`, `test-camera-interaction-fixture`,
`test-script-host-workers-reference`, `test-player-closure-0e-18-reference`,
`test-player-closure-10-12-19-reference`, `test-area-script-reference` and
`test-area01-script-workers-reference` pass. Receipts in ignored
`build/frontier2_i9_20261020_122303/`.

`--run --until a00_10` now plays a00_05 (1,159 frames), a00_06 (1,374)
and a00_07 (1,201), aligns a00_08 at counter 39081 and fault-stops at
frame 12512: the area script host's op 0x0F requests movie selector 2,
which the port does not export (`movie request D_00821058=1 with selector
2 is not exported`, owner 00825920, the switch). Selector 2 is
`\MOVIE\E006S1.PSS` (the ELF's name table D_00264FB0[2]); the recording
plays it whole (a00_08 rows 356 -> 357: one main-loop step over 1,626
vblanks). Next unit: export it (`tools/export_movie.py`) and record its
field-clock facts on the fork (pictures, held pictures and the feed loop's
end after the gate, as `k_movies` holds for E900 / E001; GS_EXACT.md
"The end-of-movie stall").

**Every difference the C side shows from the arrival's end through a00_08's
movie is in four classes** (windowed against route_a00, owners included):
a00_00 242/363, a00_01 938/1273, a00_02 424/601, a00_03 92/144, a00_04
1,797/2,017, a00_05 655/1,160, a00_06 0/1,375, a00_07 0/1,202, a00_08
0/357 rows exact. Ignoring the padlock [54]'s slot observation
(`lock_r54`) every row of a00_00..a00_03 is exact and a00_04 differs only
in the shake rows 1546..1553 (the RNG drift below). Ignoring `lock_r54`
and `progress` as well, a00_05..a00_08 (to the movie) are exact row for
row. The classes:

1. `lock_r54`: the observation of the padlock [54]'s pool slot (from
   a00_00 row 0; after the break, a00_05 rows 91..252, the original
   allocates a class-2 record there that the native allocates elsewhere).
2. The RNG drift (a00_04 rows 1546..1553; below).
3. **A taken bit the original never sets** (new finding): from a00_05
   row 815 (native counter 36158) the native's D_00810860 row 0 byte 12
   bit 5 (taken id 0x65) is set, so `progress` differs in every later row;
   the original sets no taken bit in AREA00 before a00_09 row 152. The
   setters reached in AREA00 are the class-2 creature brains' state 3
   (00128C10 / 0012A5D0: 001B1190(+0x9A) then 001AFC10): a native creature
   dies that the original's does not (likely downstream of the RNG drift;
   its record is not in the observed owner list, so which one is open).
4. The arrival's rows 0..80 and 3750..3754 (below).

**2026-10-20, iteration 8 (port main 6798a81 plus the a00_04 script
commands): the C side plays a00_04 to its end; the stop moved to a00_05.**
Not a capture pass (windowed counts below). The ferry owner `00825600`'s
scripts reached two script commands the area script host did not run, in
this order:

- **op 0x03 (`001B8AB0`, `ftab_0024D880[3]`; NEARMISS decomp C, src/func_001B8AB0.c).**
  The fault "001BA1F0 of 00825600 faulted at 0024D88C (record 00829DE0)"
  was the dispatch read of `ftab_0024D880[3]`: em_area_script.c had no
  case for it. The command is the camera shake: it saves D_008105D0 /
  D_008105E0 at rec +0x20 / +0x30, then for rec +0x0C counts adds a random
  offset (by the mode word rec +0x08 and the word rec +0x14, drawn with
  00122BB8) turned by D_00810650 on every odd count, and restores them.
  Its translation `em_area00_low_001B8AB0` already existed (checked against
  the original instructions by `make test-area00-low-reference`, 16/16
  conditional branches). It now runs as the other original-handler owners
  do: `execute` hands op 0x03 to the area binding's handler, the AREA01 /
  AREA00 binding (`em_area01_script_live.c` `record`) admits `001B8AB0`,
  and `em_area01_script_workers.c` runs the translation over rec
  +0x08..+0x17, the phase byte (block +4) and the scratch quadword
  0x70003600, its quadword copies, 001281C0, 00122BB8 (the live
  `em_random_next`), 001026A0 and 001028B8 going through the host's
  worker. Without a handler (AREA11) op 0x03 still fails stop.
- **op 0x0D sub 1 (`001B0460(1)` from `001B7B30`; byte-matched decomp C,
  src/func_001B0460.c).** Then "001BA1F0 of 00825600 faulted at 001B0460
  (record 0082A020)": the script host's `w_001B0460` was NULL. It is now
  bound (`em_area11_script_host.c`) to the camera re-seat the room move
  already runs, `em_camera_live_001B0460` (its translation
  `em_script_host_001B0460`, `make test-script-host-workers-reference`),
  through the new `em_scene_bindings_001B0460`: the room camera record of
  D_0024D650 from the spawn table window, 001B0250 on the canonical
  D_008106C8, and the player's D_00810350 (+0xA0, w +0xAC), D_00810370
  (+0xC0 with the heading) and D_008104E0 (+0x230) from the live record,
  published before the call and reloaded after it.

Proof. `make test-area01-script-workers-reference` now also runs 225
op 0x03 cases per capture (every third in quick mode) (modes 0, 1, 2, 3, 0xFF; phases 0..2; rec +0x14
0 / 7 / 0x40; timers 0, 1, 78, 79, 80 against 80) through the binding
(`em_area01_script_worker_call`) against the original instructions,
comparing every worker boundary (callee, arguments, stack pointer and the
compared memory, now including 0x70003600..0x7000361F and the rand state
word) and the result: quick 306 cases / 564 boundaries, `EM_TEST_FULL=1`
4,736 cases / 8,576 boundaries. Scratch negative controls each fail it:
mode 1's draw `& 7) - 4` changed to `- 3` in the translation, the scratch
region cut to 8 bytes, and the result inverted. The re-seat is checked by
the recording: the camera jumps at a00_04 row 1984 (eye (66.8, 11.7,
-1538.7) to (255, -24, -1694)) and rows 1984..2016 are exact, camera
included. The first-level smokes' tick, RNG and present logs equal port
main 6798a81's (sha256 2866e0de... / f8a37cb8... full, d2cb9bed... /
2fa87dc2... drive switch); newgame-control 9.599849;
`tools/test_level_smoke_area01.py --until a01_07` passes 28 live phases;
`make test-area-script`, `test-area-script-reference`,
`test-area01-script-host-reference`, `test-script-host-workers-reference`,
`test-area00-low-reference` and `tools/test_area00_script_binding_reference.py`
pass. The AREA00 run's tick log equals the previous build's up to the
faulting tick. Receipts in ignored `build/frontier2_i8_20261020_083957/`.

`--run --until a00_10` now plays a00_04 (2,016 frames), aligns a00_05 at
counter 35343 and fault-stops at frame 9075: "owner 00128C10 node 007AC5E0
failed at 001288D0", an unbound worker of the live composition (next
unit; `em_area00_low_001288D0` is its checked translation, and it calls
001F4A00, the tail call to 001F4BF0). Windowed (owners included): a00_04
1,797 of 2,017 rows exact (differing ranges 0..59, 75..112, 153..190,
444..481, 484..521, the carried [54] slot / lock_r54 class; and
1546..1553, the shake); a00_05 498 of 658 rows reached exact.

**The native RNG stream runs behind the original's (found 2026-10-20).**
The shake rows 1546..1553 differ only in eye / target (by up to about 0.9),
the rows before and after are exact: the offsets come from 00122BB8's
draws. At each fork beat end the original's rand state (the word +0x58 of
the block D_0024295C names, in the beat's eeMemory.bin) is the native
state at the same counter advanced by N draws of the LCG: a01_00 4,
a01_01 -1 (native one ahead), a01_02 12, a01_03 18, a01_04 27, a01_05 34,
the arrival's end 94, a00_00 79, a00_01 57, a00_02 129, a00_03 198 (the
native draws about 26..42 per frame there). The first values are within a
frame (the snapshot's place in the frame); from a01_02 on the native makes
fewer draws than the original, and the AREA01 rows do not observe them.
Finding which draws are missing (a consumer the port does not run, or runs
fewer times) is a fidelity unit before any phase that shows a random value
can pass; record the original's per-frame draw count on the fork around
a01_02 to locate it.

**2026-10-20, later (port main c4aa4a5 plus the knife's reach in a00_03):
the C side runs a00_03 without a fault; the stop moved to a00_04.** That is
not a capture pass: windowed against route_a00 with `compare_route_row`,
a00_03 has 92 of 144 rows exact (measured below). a00_03 (break the cage
padlock) reached three things on the knife's path, in this order:

- `001AA840` (NEARMISS decomp C) refreshes the aim / fire composition,
  whose host view list (1,024 entries, seven arrays on the C stack) was
  full. The list is host bookkeeping, not an original table: it is now
  `EM_AIM_FIRE_WORLD_VIEWS` = 256 fixed views + 256 pool records x 104
  (the enumerate's most for one record: an effect record's 40 node
  fields, 57 trail, 6 burst views and its +0x14 word; a `_Static_assert`
  in em_aim_fire_runtime.c ties the bound to those arrays), on the heap,
  one set per nesting depth of `em_aim_fire_world_live_call` and kept, so
  a nested call never touches its caller's views.
- `00189EC0` (NEARMISS) type 0x0E (the padlock [54], callback 001581A0):
  its facing test `001000C0(00128350(t), pi/2)` faulted in the aim / fire
  binding, which had no leaf for either. `00128350` (byte-matched eegcc
  C, libgcc __extendsfdf2) and `001000C0` (byte-matched C: 1 when
  `001274B0` of its two double words is negative) are now bound to
  `em_sdk_soft_float_00128350` / `em_sdk_soft_float_001274B0`, their
  existing translations (`make test-sdk-soft-float-reference`: 27,324
  cases equal to the original instructions). The binding's case dispatch
  is `em_aim_fire_soft_float_call` (src/game/em_aim_fire_soft_float.c):
  f12 in, the 64-bit v0 out for 00128350, a0 / a1 passed through as two
  64-bit double words for 001000C0. `make
  test-aim-fire-soft-float-reference` runs that dispatch, through the
  binding's EmAimFireTargetCall, against the original instructions of
  both functions and of the chain 001000C0(00128350(t), 0x3FF921FB60000000)
  (t below, at and above pi/2: 1, 0, 0; zeros, infinities, NaNs, random
  words and double pairs). Its negative controls (the comparison flipped,
  a0 / a1 swapped, a0 truncated to 32 bits) each fail it.
- The melee hit frees the padlock (001AFC10, byte-matched), and the same
  frame's `001AA840` walks the class-4 list published before the free, so
  it reads the freed record's +0, +2 and +3. The original memory then
  holds what 001AFC10 left (the header words +0x00..+0x0F cleared; the
  entry's class byte reads 0, so 001AA7A0 is not called). For a published
  class-4 entry that is no longer allocated, the composition's effect
  owners (the `EmArea00Fx` path, which runs 001AA840) resolve memory
  through one provider, `owner_view` in em_aim_fire_world_live.c: reads
  inside exactly those 16 bytes of the native record (which
  `em_actor_pool_free_001AFC10` clears the same way) succeed, a write to
  them or an access reaching past them gets nothing (a fault), and every
  other address resolves through the ordinary views as before. The other
  region-only owners (hit, side, math, misc) never see a freed header.
  `em_aim_fire_world_live_owner_view` exposes the innermost running
  call's resolution (for tests). Found with a temporary local diagnostic
  (record 102 = 0x7B81E0, class 0x44, type 0x0E, freed before the walk),
  removed.

Proof: `make test-aim-fire-world-live` (175 checks). Inside a 001AA840
call, a test 001AA7A0 worker probes the freed record: its 16 bytes read as
the cleared header, +0x10 and accesses reaching past either end fault,
and every write to +0x00..+0x0F faults; the worker then re-enters the
composition with other views for the same address, and afterwards the
outer call still resolves its own views and its walk reaches the second
allocated entry. A freed entry alone is walked without 001AA7A0 and is not
a general map; an allocated one without a view still faults. Negative
controls in scratch copies each fail it: the freed header widened to 0x40
bytes, made writable, and every nesting depth sharing one view set. The
change to a provider leaves the AREA00 run's tick and RNG logs
byte-identical to the previous round's build (sha256 ed83ac95...).
`--run --until a00_10` (drive replay) now runs a00_00 (362 frames),
a00_01 (1,272), a00_02 (600) and a00_03 (143) on the C side without a
fault (in-process phase lines, not capture passes), aligns a00_04
at counter 33323 and fault-stops at frame 7943 in the script host:
`001BA1F0 of 00825600 faulted at 0024D88C (record 00829DE0)` (next unit).
The arrival's capture check is unchanged (first difference row 539, tick
25667, the clock). The first-level smokes' tick, RNG and present logs
equal port main c4aa4a5's (sha256 2866e0de... / f8a37cb8... full,
d2cb9bed... / 2fa87dc2... drive switch); newgame-control 9.599849;
`make test-level-smoke-full` (its eleven aim side runs drive this
composition) and `make test-level-smoke-ps2-drive` pass;
`tools/test_level_smoke_area01.py --until a01_07` passes 28 live phases;
the bridge regressions `test_area01_target_hit_reference.py`,
`test_area01_water_reference.py`, `test_area00_world_reference.py` and
`test_player_equipment_reference.py` and the aim / fire live tests pass.
Receipts in ignored `build/frontier2_i7_20261020_024700/` and
`build/frontier2_i7_fix_20261020/`.

Measured comparison of that run, each phase windowed from its aligned
counter against route_a00 with `compare_route_row` (owners included),
counting every row rather than stopping at the first difference:

| phase | rows exact | first differing row ranges | first differing field |
|---|---|---|---|
| a00_00 | 242 / 363 | 0..2, 51..71, 121..141, 146..166, 218..238, 286..306, 350..362 | lock_r54 (the +0x2DC byte toggle) |
| a00_01 | 938 / 1,273 | 0..7, 60..80, 151..159, 172..180, 282..299, 303..320, ... | lock_r54 (the same toggle) |
| a00_02 | 424 / 601 | 19..38, 40..59, 144..164, 194..214, 308..328, 347..367, 441..461, 516..531, 537..552 | lock_r54 (the same toggle) |
| a00_03 | 92 / 144 | 49..55, 70..76; 106..143 | lock_r54 (the toggle); from row 106 the post-break record |
| a00_04 | 1,334 / 1,546 compared (2,017 recorded) | 0..59, 75..112, 153..190, 444..481, 484..521 | lock_r54 = the post-break record (z) |

Only the first differing rows of each phase were inspected field by field;
the later ranges are counted, not classified.

a00_03 row 106 (tick 32027) is the first divergence after the break: the
record re-allocated in [54]'s slot (header `02000c00 01...`, callback
0x1EA240) has z -1687.31641 where the recording has -1687.104, and its
+0x1F0 words read `ee5d880f ee5d880f` where the recording has `a6db8e6d
a6db8e6d` (the next two words agree). This is an open fidelity unit,
next to the a00_04 script-host stop (see "Remaining differences").

**2026-10-20 (port main e600fc5 plus the footstep handler): the C side
plays a00_00, a00_01 and a00_02; the stop moved to a00_03.** a00_00's
33rd frame reached the effect pass's handler `001EB980` (effect subtype
0x27, the footstep variant of entity 0x80000068; byte-matched decomp C,
`src/func_001EB980.c`): one 001CFB50 of the node's matrix with the work
block's +0x54 / +0x5C (accumulator, fraction) and 3.0, then 001CFBE0 mode 1
of `D_00255FB0` and of `D_00256040`. Its translation already existed and
was oracle-checked (`em_area00_hud_001EB980`, test_area00_hud_reference.py);
it is now wired into `em_effects_live.c` (`handler_001EB980`, the same view
pattern as 001ECB00: the pointer `D_00275C34` = node + 0x1F0 and the work
block's +0x244 / +0x24C), and its two source blocks are exported
(`tools/export_area00_runtime_tables.py` `FOOTSTEP_WINDOWS`, one 0x120-byte
window at `D_00255FB0`, equal to the pinned ELF in all 34 retained AREA00,
AREA01 and revisit captures; the export now holds 59 of the loader's 64
windows). Oracle: `make test-area00-footstep-reference`
(`tools/test_area00_footstep_reference.py`) executes the original
001EB980 with every transform and packet callee over the retained AREA00
captures' effect nodes (four accumulator / fraction variants, two depths)
and compares the production adapter's whole RAM and scratchpad (quick 48
cases, EM_TEST_FULL=1 288 over 12 captures), plus two refusals; swapping
the two emits fails it. No retained capture holds a subtype-0x27 node, so
the cases are synthetic work values over real matrices, not a recorded
footstep.

Result: `--run --until a00_10` (drive replay) plays a00_00 (362 frames),
a00_01 (1,272) and a00_02 (600) on the C side, aligns a00_03 at counter
33179 and fault-stops about 100 frames in at `001AA840`: `aim/fire runtime:
view list full at 007BD15E` / `aim/fire world: enumerate failed after 994`
/ `001AA840 refresh failed (30 views)`. The aim / fire world's host view
list (`EM_AIM_FIRE_WORLD_VIEWS` = 1,024 in em_aim_fire_world_live.h; the
Bridge's seven view arrays are on the C stack) is host bookkeeping, not an
original table: the next unit is to size it from the actor pool (the
enumerate adds at most 104 views for one effect record: 40 node fields, 57
trail, 6 burst and one +0x14 word) and move the arrays off the stack, with
proof the first level and AREA01 stay byte-identical. The arrival's 86
differing rows (below) are unchanged; no AREA00 phase is a capture pass
yet. Proof of this step: the first-level smokes' tick, RNG and present logs
equal port main e600fc5's (sha256 2866e0de... / f8a37cb8... full,
d2cb9bed... / 2fa87dc2... drive switch); newgame-control 9.599849; `make
test-level-smoke-full` and `make test-level-smoke-ps2-drive` pass;
`tools/test_level_smoke_area01.py --until a01_07` passes; the bridge
regressions `test_area01_target_hit_reference.py` and
`test_area01_water_reference.py` pass. Receipts in ignored
`build/frontier2_i6_20261020_002849/`.

**2026-10-19, night (port main 9b9c26b plus the arrival's script, bank,
voice and class-2 reads): the arrival runs to its end; the stop moved 33
frames into a00_00.** One change supplied what the arrival reached after
frame 203, in the order the run met it:

- `001B8020` op 0x0B (byte-matched C) subs 2 and 3 in `em_area_script.c`
  (the owner `00823580`'s records `008285E0` = sub 3, count 0x8C, and
  `00828660` = sub 2, mask 0x1000): st +0x0E = `001C64F0`(owner, rec
  +0x0C) through the new worker `w_001C64F0` (the area binding's
  `advance` hook, `em_area01_script_live.c`, to the live runtime's
  anim_advance_time); sub 3 sets rec +0x10 = (float) rec +0x14 and the
  phase byte to 1, then counts rec +0x10 down by 1.0 and advances at 0.0;
  sub 2 advances on a bit shared with rec +0x14; values outside 0..6
  advance without a call (sub 5 still faults). Oracle: `make
  test-area-script-reference` (the synthetic 'owner anim advance' script,
  12 lockstep ticks against the executed 001BA1F0 + 001B8020; quick 57
  scripts / 1,394 ticks, EM_TEST_FULL=1 6,068 ticks).
- `001C6120` (byte-matched C) over a bank other than Roger's library (the
  arrival owner `00824EA0`'s 001C2770 -> 001C39F0 -> 001C6160 with clip
  0x8006 over its bank `0113D1C0`): the live runtime now serves it with
  `em_pose_host_001C6120` (oracle-checked in
  `test-pose-host-workers-reference`) over the one directory word.
- The arrival's voices: `tools/export_streams.py` now also exports AREA00's
  `D_0026EC60` rows (music 45, 47) and every voice of its message table
  `D_00264DD0[1]` (19..28, 30, 31, 32); before, the first arrival voice
  read faulted at `00112610` (code 8). 14 extents, 25,603 sectors; the
  23,725 earlier sectors unchanged (`tools/test_area01_stream_export.py
  --previous`, every sector against the disc).
- `D_00242EB0..D_00242F20` (00128C10's halfword clip tables and
  00128600's byte rows, both byte-matched C): the class-2 owner
  `0012A5D0` -> `0012AFC0` -> `00128600(0)` read `00242EDB` at arrival
  frame 3956. Exported by `tools/export_area00_runtime_tables.py`
  (`CLASS2_WINDOWS`), equal to the ELF in all 52 retained captures
  (startup, AREA11 route, AREA01, AREA00, revisit); `make
  test-area00-arrival-bindings` serves it through the production loader.

The C side now plays the arrival's 4,017 rows and aligns a00_00 at counter
30942 (drive replay), then fault-stops 33 frames in at the untranslated
effect handler `001EB980` (`effects: untranslated effect handler at
001EB980`). The arrival's capture comparison is not a pass: 86 of its 4,017
rows differ, in two runs. Rows 0..80: the original's player runs an
arrival stance first (row 1: +0x1F0 = 1, byte 5 = 1, scratchpad 0x70003B8D
word 0x0400; row 2: clip 2, x -40.0 where the port has -40.5, camera
target and forward to match; row 3: +0x1F1 = 65), and its clock reads 0, 1,
8, 7 .. 1 before its 80 countdown starts at row 11, while the port's clock
counts down from 80 at row 0. Rows 3750..3754: the message phase (the
original's block goes 2 -> 0 at row 3751; the port's stays 1 until row
3754). Next AREA00 units: the arrival stance (read the original player
code the arrival's first rows run; a label is not evidence), the message
phase at row 3750, then `001EB980`. Proof of this step: the first-level
smokes' tick and present logs equal port main 9b9c26b's
(sha256 2866e0de... / f8a37cb8... full, d2cb9bed... / 2fa87dc2... drive
switch); newgame-control 9.599849; `make
test-level-smoke-full` and `make test-level-smoke-ps2-drive` pass;
`tools/test_level_smoke_area01.py --until a01_07` passes a01_arrival..a01_07
row for row. Receipts in ignored `build/frontier2_i5_20261019_205006/`.

**2026-10-19, evening (port main 0deae8b plus the surface program): the
arrival runs 203 frames; the stop moved to a script op.** 001E9280 (the
surface owner `0015AB00`'s packet, NEARMISS C in the decomp; its packet
bytes follow the C) CALLs the static program packet `D_00234FE0` (0x4B0
bytes with its RET), which is now exported from the pinned ELF into the
effect export (`tools/export_effect_tables.py` BLOCKS, installed through
`tools/export_area00_runtime_tables.py`; the window equals RAM in all 15
AREA11 route captures and the 13 AREA00 snapshots) and walked by the native
chain page (MPG of 142 instructions from ELF 0x00235010, FLG UNPACKs and
MSCNT after a batch of its own, count `mscal_surface`). Its VU1 program is
translated as `em_vu1_surface_program_mscal` (em_vu1_page_programs.h): the
batch code is the floor program's instruction for instruction; the vertex
is the floor vertex with the colour's alpha taken from the point's w (no
depth fade), the texture pair offset by VF20.x / VF20.y and no depth
scale. Oracle: `make test-area00-surface-vu-reference`
(`tools/test_area00_surface_vu_reference.py`) runs the original microcode
on the shared VU1 machine (tools/chain_page_model.py, which now knows the
packet) and compares every surface run of the retained snapshots through
the model and the native page (EM_TEST_FULL=1: 11 snapshots with surfaces,
88 runs, 528 batches and kicks, 4,020 primitives, every GIF byte), 1,500
synthetic MSCAL + MSCNT pairs on both arithmetic paths (all data memory and
registers; 14 cases where the original faults on an exponent-255 operand,
the native too; exponent-255 alphas pass through MOVE / MAX / MINI / FTOI
equal) and three fail-stops. Receipts in ignored
`build/frontier2_i4_20261019_184516/`.

The arrival now stops at frame 203: the AREA00 overlay owner `00823580`
(node `007B7C00`) runs its script through `001BA1F0`, and record
`008285E0` is op 0x0B with mode 3, which `em_area_script.c`'s `op0B`
(001B8020, byte-matched in the decomp) does not admit yet (it admits modes
0, 1, 4 and 6): `em_area11 script host: 001BA1F0 of 00823580 faulted at
001B8020 (record 008285E0)`. Next AREA00 unit: admit 001B8020's mode 3
(anim_advance_time, then the sub-phase byte +4; read the decomp C) with
its oracle cases, then rerun the arrival. Proof of this step: the
first-level smokes' tick, RNG and present logs are byte-identical to port
main 0deae8b's (EM_TEST_FULL main line and the drive switch to
a01_arrival); newgame-control 9.599849; `make test-level-smoke-full` and
`make test-level-smoke-ps2-drive` pass; the AREA00 harness (`--run --until
a00_00`, drive replay) passes a01_arrival..a01_07 again, and its tick
records before the old frame-1 stop equal the old build's.

**2026-10-19, later (port main ffd515c plus the arrival read views): frame
0 of the arrival completes; the stop moved to frame 1's chain page.** One
change supplied every read and worker the arrival's frames 0 and 1 reached
(each in the order the run met it):

- `0022DCD0` (`em_area01_exita`, NEARMISS C; oracle
  `tools/test_area01_exita_reference.py`, which covers its key-0 fill): its
  AREA00 record list `D_00269400` (six 64-byte records and the terminator
  word) and the 0x90-byte record `D_0026A900` that `001D0400` scales are
  exported from the pinned ELF into the effect export
  (`tools/export_area00_runtime_tables.py` `LIGHT_WINDOWS`, checked against
  the ELF and all 13 retained AREA00 snapshots); its tables live in the
  boot BSS `D_00822CF0..D_008234C0`, which no other decomp function names,
  now one process-lifetime block in `em_area01_live.c`; `001D0400`'s
  `00121870` block copy (assembly in the decomp) is a live worker with
  `em_static_world`'s piece order; `001CFB50` goes to
  `em_effects_live_001CFB50` over the stack block (its `001CFBE0` kinds 1
  and 6 were already live).
- `0015A2C0` (`em_area01_sys`, NEARMISS C): AREA00's floor fields use config
  rows 3 and 4 of `D_00248120` besides AREA01's 0, 1 and 5; rows 3 and 4 are
  exported (`FIELD_WINDOWS`; every retained owner names an exported row).
- `001F6BB0` (`em_effect_manager`, NEARMISS C): key 0 reads `D_0081075D`,
  canonical in the progress region since A01 (0 at the arrival, 0xFF from
  `a00_08`'s switch in the fork snapshots); `em_effects_live` refused key 0
  as if it had no owner. Key 0x1301 still refuses (no canonical copy of
  `D_00810778 / 77B / 79E`). Key 0's 0xFF path (`001F6760`, `001F66F0`) has
  no worker yet and fail-stops when `a00_08` reaches it.
- `0019AD00` (`em_coll_move_original`, NEARMISS C), called by `00129780`
  (00128C10's selectors 2 / 7): bound through `em_area01_collision_view`,
  which hands the walker the probe representation of the one scratchpad
  (`export_move`, the inverse of the existing `import_move`), marks the
  move scratch the last writer and writes bit 31's x / z back. Oracle:
  `tools/test_area01_collision_view_reference.py` now runs 120 original
  `0019AD00` calls (masks 2, 4, 6, 0x80000006) on the 15 AREA01 snapshots
  through the view and compares the scratchpad and the mover's position.
- `em_owner_draw_live`: `EM_OWNER_DRAW_LIVE_UNITS` 64 refused frame 1's
  draws; it is host bookkeeping (the original is bounded by the packet
  arena, which the module checks) and is now 512.

The arrival now stops at frame 1 in the chain page: `001E9280` (the
surface owner `0015AB00`'s packet, NEARMISS C) CALLs the program packet
`00234FE0`, which is neither exported nor translated (`chain page:
unmapped address fault at 00234FE0`). It uploads one MPG of 142
instructions (ELF 0x00235010; `tools/level_texture_page.py` already
replays it on the original microcode for the texture export) and runs six
batches like the floor program `D_002345E0`. Next AREA00 unit: export the
packet and translate that VU1 program into `em_vu1_page_programs.h` with
an original-microcode reference test like
`tools/test_level2_floor_vu_reference.py`. Proof of this step (receipts in
ignored `build/frontier2_i3_20261019_144851/`): against the build without
it (port main ffd515c, binary 2da192b5...), the first-level smokes' tick
and present logs are byte-identical and their RNG draws equal in frame and
value (EM_TEST_FULL main line 273,855 draws; the drive switch 274,032);
`make test-level-smoke-full` and `make test-level-smoke-ps2-drive` pass;
newgame-control 9.599849; the AREA00 harness run (`--run --until a00_00`,
drive replay) passes a01_arrival..a01_07 again and its first 25,669 tick
records equal the old build's, up to the record where the old build
fault-stopped (counter 26926). `make test-area00-arrival-bindings` checks
the added windows (30,836 slices, 8 refusals, 6 corruptions refused);
`make test-area01-collision-view-reference` the 0019AD00 binding.

**2026-10-19 (port main 479f387 plus the owner-view raise): AREA00 does
not play yet; the stop moved.** `em_area11_boxes.c`'s owner-view table now
holds `BOX_MAX = EM_ACTOR_POOL_CAPACITY` (256) views, one per record of the
original's actor pool, instead of 32 (the original arrival holds 26 crates
and 13 drums, at least 39 views; "Owner-view capacity (resolved)" below).
Ordinary New Game still reaches the arrival alignment (counter 26925 with
the drive-timing switch) and now passes the crate and drum callbacks; it
fault-stops at arrival frame 0 in the `[39]` owner `0022DCD0`
(`em_area01_exita`, NEARMISS C in the decomp), which reads an unexported
runtime address `00269400` (`em_area01: owner 0022DCD0 node 007B55D0
failed at 00269400`; ignored `build/frontier2_i1_20261019_065540/a00-arrival-new/`).
That read view is the next AREA00 unit. Zero AREA00 rows are certified: arrival, a00_00..a00_10 and the
side return a00_s0 are all open, and so is the AREA01 revisit that follows
a00_10 (AREA01_REVISIT.md). The harness itself passes its contract checks
(`tools/test_level_smoke_area00.py --verify-harness`: 13 C branch paths,
2,731 input commands, 16,504 capture-shaped rows, 179 rejected corruptions;
`build/merge-1019-post/area00-harness.log`), which is no gameplay claim.
The AREA01 prerequisite passes since the user's decision of 2026-10-19
("Comparison-only replay"): with the drive replay `EM_PS2_DRIVE_REPLAY`
a01_00..a01_07 pass row for row through the AREA00 arrival state 0
(LEVEL2_BINDING.md "State (2026-10-19, the drive replay)").
`tools/test_level_smoke_area00.py --run` now runs with the replay by
default (tables for a00_00..a00_10 and a00_s0 recorded; IOP_STREAM.md
"Drive replay"); its run to a00_00 passes every AREA01 phase and stops, as
before, at the arrival frame 0 in 0022DCD0 at 00269400 (aligned counter
26925; ignored `build/frontier2_i2_20261019_092911/a00_00_replay.out`).
The four unsampled AREA00 ticks (below) are lead-in frames of the fork
re-drives; the drive tables sample them, but their route rows still need
fork boundary receipts.

2026-10-18 asset follow-up: original page replay found an additional
`001E9E60` surface texture. Both canonical AREA00 catalogs now hold 580 fixed
keys, checked against every selected GS freeze and uploaded by the native
resource-loader fixture. The explicitly authorized graphics change raises
the registry to 1024 with a hash index. All four default/GPU smokes pass
with byte-identical ticks, RNG and presented-frame logs; control is 9.599849.
Metal-drawn (`gpu`) frames carry no pixel hash, so they are compared by
tick, RNG and frame order; the registry's pixel behaviour is proven by the
Metal fixture `tests/level_texture_registry_test.m` (LEVEL_ASSET_DELIVERY.md). This does not certify an AREA00 frame. The subsequent
surface and retained-library delivery work is described below. Full asset receipts and retained runtime-material limits are in
[LEVEL_ASSET_DELIVERY.md](LEVEL_ASSET_DELIVERY.md).

The stage-5 continuation adds the 40-byte `D_00248290` read view through
the existing effect pack, preserving its 50 earlier windows. The full
`0015AB00` oracle passes 160 cases and 30,000 worker calls. The unchanged
production read provider passes 820 exact subrange checks and five bounds
refusals under ASan/UBSan. All 13 retained fork snapshots match the table.

The next native fault was model `0x4F` at `00C7CBC0`, called by
`0015AFA0` through `001B1020`. AREA00's roster requires five library
models, of which `4F/59` were missing from the retained export. Extending
two existing read-only spans supplies both while preserving all earlier
bytes, flags and the 16-region limit. All five models match 13 snapshots;
55 original-instruction binding cases pass over the 11 AREA00-resident
snapshots. No native game source changed for either addition.
See [AREA00_ASSETS.md](AREA00_ASSETS.md) for reproduction and exact scope.

The following native attempt passed those resource reads and stopped at
the crate registry's AREA00 selector `0024A850` inside `001551B0`. Adding
that halfword and `0024D820`'s pointer to the same export supplies the
existing lazy resolver; group data remains loader-owned. The full crate
oracle passes 86 ticks and 442 worker calls, with both lazy access and
sticky missing-root faults checked. These additions still change no
native game source and do not promote an AREA00 phase by themselves.

With all three additions, ordinary New Game reaches arrival alignment at
counter 26925 with the drive-timing switch (`EM_PS2_DISC_DRIVE_TIMING=1`,
the documented command) or 26634 at host speed
(`build/levels/area00-arrival-hostspeed-review-1/`), then fails at arrival frame 0 in drum callback `00156620`:
the protected `em_area11_boxes.c` owner table has 32 slots. The original
arrival has 26 crates and 13 drums, so those callbacks alone require 39
views. That was the stop until 2026-10-19, when main raised the table (the
2026-10-13 branch's ownership rule had kept this AREA11 file unchanged);
see "Owner-view capacity (resolved 2026-10-19)". The 512-texture refusal
below is historical.

2026-10-18 standalone follow-up on branch `levels`: four missing AREA01 routines
on the AREA00 return (`00823A90`, `00823CD0`, `008240E0`, `00826010`) now have an
unwired translation in `em_area00_return.c`. Its original-instruction test
passes 521 cases and all 534 reachable non-branch words, including 30 captured
actor cases and the full fail-stop sweep. This is no AREA00 phase promotion.
See [LEVEL_ORACLE_AUDIT.md](LEVEL_ORACLE_AUDIT.md) for exact scope and receipts.

The historical capacity diagnostic's native scene selection reaches AREA00 after ordinary New Game, AREA11
and AREA01. The original load completes and selects AREA00 collision and
scene resources. At counter 26922 the existing shaft phase observes
AREA00 state 0, before rebuild. The following page-render request faults
at `001CB800` because the catalog cannot fit. This replaces the previous
`001FF080` “area 00 room 0 is not exported” stop; it is not a gameplay pass.

The current composition selects AREA00's original initializer, borrowed
loader data/BSS, roster, callbacks, scripts, messages, collision, static
bank and per-subarea resources. AREA01 wrappers keep their existing area-1
behavior. The shared owner remains authoritative for each global, actor,
player, weapon, model and collision span. Unknown workers still fault.

Completed prerequisites now include both verified 580-key texture catalogs,
shadow banks (sub 0: 1,100 objects / 1,802 blocks; sub 1: 1,099 / 1,783),
the full module deliveries and canonical SFX scopes for subareas 0/1.
The complete sound registry preserves the earlier entries and samples;
unsupported original sound cases remain explicitly unsupported.
The scene logger now emits the strict AREA00 observations from the
canonical owners. No captured state is installed in a native run.

[THIRD_LEVEL_CENSUS.md](THIRD_LEVEL_CENSUS.md) retains all 141 delta entries
and 19,012 instructions. None is promoted to AREA00-route live: the
required native row comparisons have not passed.

## Binding ledger

| Boundary | Existing owner / required binding | State |
|---|---|---|
| AREA01 shaft exit / module read | `em_scene_task`, `em_module_loader`, full AREA00 sub-0/1 pack entries | Native sub-0 delivery and scene selection reached; sub-1 sectors independently checked |
| Texture delivery | `em_world_textures_live`, the single graphics registry | Both selectors wired; corrected 580-key catalogs pass resource upload at capacity 1024; required default/GPU registry regressions pass |
| Shadow receivers | `em_shadow_live`, `em_shadow_original` | Both selectors wired and checked, with transactional failure cases |
| Collision and static bank | `em_collision_world`, loader resource `0028A5A0`, `em_render_context_live` | AREA00 paths selected; static bank remains loader-owned |
| Overlay initialization | existing `001E7780` plus `em_area00_state_00824E00` | Selected initializer, six common resets and both record clears verified against original instructions |
| Overlay data/BSS | AREA00 MWo3 id 1, data `826F80/6500`, BSS `82D480/295900` | Checked borrowed loader views; the existing six boot globals remain the sole storage |
| Progress | `EmProgress`, `em_scene_progress_at` | `75B/75C`, `7DA/7DB/7DC` and AREA00 lock byte `841` admitted with original writer evidence; no new storage/reset |
| Placed roster / actors | `em_actor_roster_spawn_001B6990`, canonical actor/model/collision adapters | Per-subarea placements/deferred records selected; no separate pool |
| Overlay behavior | `em_area00_overlay` | All 13 prepared entry adapters selected before AREA01 address dispatch; unknown overlay entries fault |
| Scripts | `em_area_script`, existing script host | AREA00 window `8284E0..82BB50`, mutable loader data and area-specific callbacks selected; arrival `824E40` checked against original instructions |
| Messages | `em_message_live` | AREA00 EMMD selected during rebuild; canonical request/presenter state retained |
| Sound | existing stream/SFX owners | Complete AREA00 sub-0/1 registry scopes; 3,278 combined entries / 234 samples; no unsupported-case bypass |
| Surface initialization | existing effect-table provider and `em_area01_exitb` | `D_00248290` added from pinned ELF; full initialization/packet-state oracle and production read bounds pass |
| Pickup models | existing retained library provider and shared model adapter | Five roster models delivered exactly within 16 regions; production provider and 55 original binding cases pass |
| Crate registry | existing box owner, effect windows and module loader | Six-byte AREA00 roots exported exactly; production lazy resolver passes 86 original crate ticks |
| Ferry / cab / shaft workers | existing roster, WORLD, FX and effect services | Direct `001B6660`, `0019C6F0`, `001B0C60`, marker/depth/transform adapters installed; standalone comparisons pass; native phases remain uncertified |
| Player and camera | existing player closures and camera owners | Canonical selected-area views installed; no AREA00 native row pass |
| Return / bridge | AREA01 overlay plus revisit translations | Main and plain side returns remain separate strict phases; neither reached |

Common `001E7780` clears its six globals, calls the selected initializer,
then clears `+54/+58` for each initialized record with stride `A060`.
The AREA00 initializer borrows those existing globals and the loader's
memory. It never clears a replacement BSS image or owns a duplicate reset.
The optional AREA00 read/write view preserves canonical permissions;
a refused write cannot fall back to a mutable legacy byte view.

## Phases and comparison results

| Requested phase | New smoke scope | Native comparison result |
|---|---|---|
| 1. Arrival / load / bindings | AREA01 exit boundary, then `a00_arrival` | Not passed: alignment at counter 26925 (drive timing) / 26634 (host speed), then (since the owner-view raise, 2026-10-19) a frame-0 fault in `0022DCD0` at the unexported read `00269400`; before it, the `00156620` fault of the 32-view table |
| 2. First world and neutral frames | Fork a01_07 f539..4555, including final 60 neutral rows | 0/4,017 rows certified; alignment reached, but the first owner walk still fails |
| 3. Movement / collision | `a00_00..a00_10` | Not reached |
| 4. Camera | Per-row eye/target/forward/flags plus whole endpoint camera RAM | Checker contract passes; native AREA00 camera not reached |
| 5. Drawing | Future original packet and displayed-field comparisons | Not implemented/certified; exact exported texels are not a pixel comparison |
| 6. Interactions | `a00_01` locked door, `03/04` padlock/terminal, `05` ferry, `06` cab, `07` stack/crate tops/duct, `08` switch, `09` barricades/door | Not reached |
| 7. Return to AREA01 | `a00_10` progression return; `a00_s0` plain side return | Not reached; separate loader/state/progress checks prepared |

Original route correction: `a00_s0` branches from the initial AREA00
arrival and returns without the progression flag. It is not a continuation
after `a00_10`. The main progression return is `a00_10`, with
`D_0081075E=FF`; `a00_s0` ends with that flag 0. The original endpoint
snapshot proves the lowered bridge after the main return; a full native
bridge/revisit sequence remains separate verification work.

## Smoke contracts and limits

The default `test_level_smoke_area00.py` is a short **harness contract
test**, not a game run. `--run` explicitly requests the long replay from
ordinary New Game. Pads contain only input and reference timing metadata;
no captured game state is installed into the native run. `--prepare`
exports pads and does not certify anything.

The C phases reuse the existing strict EMA1 reader. AREA01 still ends at
its existing boundary when selected alone. A level-3 continuation keeps
that exact boundary check, then checks the following actual scene tick.
Return phases detect actual AREA01 state 0. Only repeated loader waits
may be shortened; no gameplay tick may be dropped or realigned. The
existing original loader/veil oracle is reused on return runs. Recorded
pad corrections preserve the late first commands and the one-frame
dropout in `a00_04`; their source metadata is checked.

The scene logger supplies `a00` observations from canonical
owners: `hp` bits; progress windows `8107D8/64`, `810758/8`, `810860/64`,
`810D00/32`; locks `810840/8`; weapon bytes `810C60/8`; and the ten
owner records named in `level_smoke_area00.OWNERS` (header, position,
`+1F0`, `+2DC`, callback). `C61/C62` come from `em_weapon`, not the
progress region, which correctly refuses those separately owned bytes. The new checker rejects absent observations.
The arrival recording has only AREA01 owner samples, so AREA00 owner
comparison is explicitly absent during `a00_arrival`; gameplay phases
require all ten samples. Existing per-row and whole endpoint camera
observations are reused. An eventual row pass still does not certify
rendered pixels, sound, unrecorded owner fields or off-route behavior.

AREA01 currently has a known prerequisite difference at a01_03 f957
(voice ends one tick early with drive timing; LEVEL_SMOKE.md). The new
runner does not waive it. Even after opening AREA00, the complete
New-Game-to-AREA00 comparison must fix this difference at its original
owner or continue to report failure. The continuation's saved replay
confirms it with the existing strict prefix diagnostic: 957 of 992 rows
match, then message state is `(2, 2, 64)` instead of `(2, 1, 64)` at row
957 (`build/level3/area01-prefix-live.log`). This diagnostic is not a
complete phase pass.

## Historical capacity request and remaining main prerequisites

The following records the 2026-10-13 refusal. The user authorized this
capacity/index exception on 2026-10-18; its implementation and passing
regression proof are described above and in LEVEL_ASSET_DELIVERY.md.

**Historical texture registry refusal, confirmed by the production catalog parser.**
`src/em_gfx.h` then defined `EM_GFX_OBJECT_TEX_MAX=512`; the protected Metal
registry enforced that limit. Each AREA00 export then contained 579
distinct keys, following AREA01's eager whole-bank/shared-resource method.
`em_world_textures_live.c::catalog` rejects the header before uploading
anything. `test_area00_resources_reference.py` reproduces that refusal
using the production parser and records zero registry mutations. The
actual input-only replay now reaches the same refusal: after AREA00
selection, `chain page` reports no page textures and the scene faults at
`001CB800`, arrival frame 0. See `build/level3/live-arrival-1/run.log` and
its binary-hash receipt; no arrival alignment is reached.

Requested change: raise `EM_GFX_OBJECT_TEX_MAX` to **1024**, audit every
array/count consumer (including Metal), then run the existing registry,
GS and pixel regressions and this resource fixture. No change to original
game behavior, sampled texels or assertions is requested. The earlier `level3` branch
did not edit that header or any renderer file; the present `levels` exception
is limited to capacity and lookup.

This is a capacity requirement of the existing **eager export policy**,
not a proof that 579 textures are simultaneously needed by gameplay.
Removing only world models absent from the endpoint snapshots still
leaves 527 keys; equipment/page sources contain additional eager keys.
A smaller catalog would require a complete binding/reachability proof
(including intermediate frames and side actions), not silent truncation
to fit 512. The source audit found 505 non-equipment keys; valid equipment
variants can add nine or twelve more, and queued draws retain their own
model references. Thus choosing the actor's current equipment alone is
neither a complete capacity solution nor a proven lifetime policy. No
catalog pruning or residency redesign is claimed here.

No additional protected VU program gap was established. Current page
support includes the known sprite/streak/kind-2/kind-6/floor/ripple paths.
Later channel-9 workers reached in `a00_09` remain an integration audit
item, not a substantiated request to edit a protected file.

## Owner-view capacity (resolved 2026-10-19)

Applied on port main (iteration frontier2 i1): `BOX_MAX = EM_ACTOR_POOL_CAPACITY`
in `src/game/em_area11_boxes.c`, so the host view table can never refuse an
owner the original's 256-slot pool holds. The crate and drum callbacks
(`001551B0`, `00156620`, both NEARMISS C in the decomp) are unchanged; the
table is host bookkeeping. Slots are taken first-free in index order and
reused by the freed / generation checks, so every owner the first level and
AREA01 bind lands in the same slot as before (the fixture asserts slot n
for the n-th owner). Proof (ignored `build/frontier2_i1_20261019_065540/`):
`tools/test_area00_owner_capacity.py --require-supported --out ...` passes
(bound 256, 39 needed, ASan/UBSan refusal and reuse); newgame-control
9.599849; the AREA01 replay through a01_02 passes with its tick log
byte-identical to the 789b4b4 receipt; the drive-switch New Game replay to
the AREA00 arrival has every tick record byte-identical to the unchanged
binary's (25,669 of 25,670 rows; the last is the faulting arrival frame
itself, which now runs further) and identical RNG values; the full and
drive-switch level smokes pass (receipts in the same folder).

The original request follows for the record.

**Raise the protected shared owner-view capacity.** In
`src/game/em_area11_boxes.c`, `BOX_MAX=32` limits both the `Box` views and
draw-order indices. `box_for` refuses the 33rd simultaneously held view.
This is host bookkeeping; the original actor pool still has 256 slots.
The AREA00 arrival and nine subsequent resident captures contain 26 crates
and 13 drums (39 views before counting doors and other world owners).
The last resident capture retains 32 crates/drums. The live failure is
`more AREA11 boxes than slots`, from `00156620`, at arrival frame 0.

Proposed diff for main (applied 2026-10-19, above):

```diff
--- a/src/game/em_area11_boxes.c
+++ b/src/game/em_area11_boxes.c
@@
-    /* four crates, two drums, the truck and the door, and the other world
-     * owners bound through em_area11_boxes_owner_* (the terminal, the panel,
-     * the prop 001C4820, the item owners; room for the rest) */
-    BOX_MAX = 32,
+    /* At most one host view per live canonical actor slot. Generation and
+     * freed-slot checks retain the existing ownership and reuse rules. */
+    BOX_MAX = EM_ACTOR_POOL_CAPACITY,
```

`tools/test_area00_owner_capacity.py` compiles the unchanged protected
provider under ASan/UBSan. It confirms 32 successful views, refusal of the
33rd, and correct freed/generation reuse. Its independent capture census
confirms the 39-view minimum. `--require-supported` correctly fails with
the present capacity; its default diagnostic mode reports the mismatch.
The fixture exercises the configured bound and does not lock in 32 as
correct game behavior. Main should require acceptance through the
canonical pool capacity, then exercise full/reused draw-order
storage, default/GPU AREA11 and AREA01 `--until a01_02` exact logs and pixels,
control 9.599849, and this same AREA00 arrival replay. Neither increasing
the actor/bone pool nor reassigning or omitting owners is requested.
Private evidence: `build/levels/area00-owner-capacity/receipt.json` and
`build/levels/area00-arrival-crate-1/`.

**Keep the AREA01 voice prerequisite explicit.** The existing a01_03
row-957 discrepancy remains a strict failure. Its audio/movie/frame owner
is outside this worktree's authorization. No checker assertion or replay
row has been dropped or realigned to bypass it.

## Verification and reproduction

Run on native arm64 macOS in the port checkout (the receipts below were made
in the `level3` worktree and stayed there). Heavy jobs are
serialized at nice priority 15 using the approved host path. Two initial
quick state/runtime attempts printed a sandbox `setpriority` warning and
continued; the reported full tests below supersede them at actual nice 15.
Do not treat `nice`'s warning as successful priority control.

```sh
# in the port checkout
nice -n 15 make -j4
# Generated outputs must be private, never written through checkout symlinks.
nice -n 15 python3 tools/export_sfx_registry.py
nice -n 15 python3 tools/export_module_loader.py
# Full table export goes to private scratch; installed root/table delivery is tested below.
nice -n 15 python3 tools/export_area00_tables.py --out build/level3/area00-tables
nice -n 15 python3 tools/test_area00_delivery_reference.py
nice -n 15 python3 tools/test_area00_assets_reference.py
nice -n 15 python3 tools/test_area00_resources_reference.py
nice -n 15 env EM_TEST_FULL=1 python3 tools/test_area00_state_reference.py
nice -n 15 python3 tools/test_area00_runtime_reference.py
nice -n 15 python3 tools/test_area00_effects_binding_reference.py
nice -n 15 python3 tools/test_area00_script_binding_reference.py
nice -n 15 python3 tools/test_area00_spawn_binding_reference.py
nice -n 15 python3 tools/test_area01_state_reference.py
nice -n 15 python3 tools/test_area01_runtime_reference.py
nice -n 15 python3 tools/test_area01_effects_services_reference.py
nice -n 15 env EM_TEST_FULL=1 python3 tools/test_area00_world_reference.py
nice -n 15 python3 tools/test_area01_door_live_reference.py
nice -n 15 python3 tools/test_area01_sfx_registry.py
nice -n 15 python3 tools/test_world_textures_reference.py
nice -n 15 python3 tools/test_scene_no_shadow.py
nice -n 15 env EM_HEADLESS=1 EM_STARTUP_TEST=newgame-control build/extermination
# Long diagnostic: the arrival plays out (86 of 4,017 rows differ, LEVEL3_BINDING.md), then a00_00 stops 33 frames in at effect handler 001EB980.
nice -n 15 env EM_PS2_DISC_DRIVE_TIMING=1 python3 tools/test_level_smoke_area00.py --run --until a00_arrival --out build/level3/live-arrival-1
# Later full-route and independent side-return checks:
nice -n 15 env EM_PS2_DISC_DRIVE_TIMING=1 python3 tools/test_level_smoke_area00.py --run --until a00_10 --out build/level3/progression
nice -n 15 env EM_PS2_DISC_DRIVE_TIMING=1 python3 tools/test_level_smoke_area00.py --run --side a00_s0 --out build/level3/side-return
python3 tools/check_no_disassembly.py --staged
# Build each staged topic in isolation before committing; remove scratch afterward.
git checkout-index -a --prefix="$PWD/build/level3/index/"
(cd build/level3/index && nice -n 15 make -j4)
```

Continuation results of the 2026-10-13 `level3` branch (historical; ignored receipts under `build/level3/`):

| Check | Result | Receipt |
|---|---|---|
| AREA00 selected initializer/common dispatcher, full | PASS: 10 captures, 120 cases, 328,276,800 compared bytes; ordered stores, aliases, lifetime, bounds/faults | `state-dispatch-full.log` |
| AREA00 runtime overlay selection | PASS: 16 original paths, 13 worker boundaries, 5 canonical writes; overlap refusal, permissions and fault latch | `runtime-full.log` |
| AREA00 shared runtime/effects adapters | PASS: 19 original directory paths and marker tail call; 13 marker/depth/transform cases, 31 compared workers; canonical publish/partial-store/fault contracts under ASan/UBSan | `shared-runtime-full.log`, `effects-binding-full.log` |
| AREA00 script binding | PASS: 36 original worker cases, four exact clip boundaries, three failure cases; both AREA01/AREA00 resource fixtures under ASan/UBSan | `script-binding-live.log` |
| AREA00 direct deferred spawn | PASS: 28 original cases, 12 spawned records, four original allocation failures; whole pool/progress/list/return equality and live actor-view fixture | `spawn-binding.log` |
| Final AREA00 assets/root lookup | PASS: 15 loaders, 70 canary sections, 99 controls; immutable root/table comparison over all 11 retained captures | `placement-root-export.log`, `placement-root-assets-test.log` |
| AREA00 overlay view regression | PASS: 1,090 cases, 1,728 runs, 5,905 calls, all 1,298 reachable words | `overlay-view-regression.log` |
| AREA01 state/runtime/door regressions | PASS: 160 state cases / 219,528,960 bytes; runtime 32 paths + 6 camera + 34 sampler cases / 209 boundaries; door 60 cases / 172 workers / 4 fault cases | `area01-state-regression.log`, `area01-runtime-regression.log`, `area01-door-regression.log` |
| Texture/shadow selectors | PASS: real AREA00 paths and transactional receiver checks; both texture catalogs explicitly BLOCKED before registry mutation | `resources-live-test.log` |
| Shared texture regression | PASS: AREA11 487 keys, AREA01 460, six changed reused keys and eleven rejection cases | `world-textures-regression.log` |
| SFX/module delivery | PASS: 3,278 entries / 234 samples, all 1,278 prior entries / 188 samples unchanged; complete six disc reads per AREA00 subarea | `delivery-test.log` |
| AREA01 SFX regression | PASS: 16 bank captures, 1,001 lookups, 64 original dispatches, 6 sequencer cases / 10 key-ons; 4,096 accepted / 4,097 refused and malformed cases | `area01-sfx-regression.log` |
| AREA00 WORLD full regression | PASS: 1,033 cases, 4,005 variants, 14,679 compared workers, 3,203 field changes, all 64 branches; 74 existing variants incomparable because original execution left RAM | `shared-world-full.log` |
| Final AREA01 shared regressions | PASS: runtime 32 paths + 6 camera + 34 sampler cases; effects nine original spawn/reset/allocator chains and sanitized contracts | `shared-area01-runtime-regression.log`, `shared-area01-effects-regression.log` |
| Canonical-state audit G6 | PASS: 755 files, 26 migrated progress/request bytes | `scene-no-shadow-final.log` |
| Final native source build | PASS, zero compiler warnings | `live-build-final.log` |
| Final first-control regression | PASS: 1,301 locked ticks, zero motion, 30 movement ticks, displacement 9.599849 | `newgame-control-live.log` |
| Resource isolated staged-index build | PASS, zero compiler warnings | `index-resources-build.log` |
| Binding isolated staged-index build | PASS, zero compiler warnings | `index-bindings-build.log` |
| Integration build before native diagnostic | PASS, zero compiler warnings | `live-build-3.log` |
| Native arrival diagnostic | FAIL at `001CB800`, protected texture capacity; area-0 load completes, no arrival alignment/rows | `live-arrival-1.log`, `live-arrival-1/run.log`, `live-arrival-1/receipt.json` |

The diagnostic ran for 277.709 seconds, returned 1 and recorded
`native_parity=false`. The executable SHA-256 was
`5ddc7487154695900b96ba480c82dd42e551b709abc59128da175e23632be23f`.
Its in-process AREA01 phase completions are not independent capture
passes. The subsequent direct-spawn and shared WORLD/FX bindings are
standalone-tested additions beyond this first capacity stop; that
recording does not exercise them.

The earlier harness receipts remain valid because the driver and checker
assertions have not changed: fork AREA00 has 13 paths, 2,731 input commands,
18,331 driver frames, 16,504 capture-shaped rows and 179 corruption
rejections (`verify-fork/`); v2.6.3 has 18,303 frames and 16,490 rows with
the same paths/commands/rejections (`verify-v263/`). AREA01's harness has
16 paths, 4,447 commands, 20,110 frames, 19,565 rows and 150 rejections
(`verify-area01/`). These are harness contracts, not native gameplay.
The old load-refusal diagnostic remains historical in `native/`.

Generated assets and receipts stay ignored. `assets/` is a real directory;
changed SFX/module packages and AREA00 delivery files are private. The
remaining existing assets are linked to the primary checkout and are not
written through. `data/memcard/slot1`, `slot2` and `data/save` are private.

## Remaining differences and next step

The current reproducible stop (2026-10-20, iteration 8) is a00_05 frame
9075: the owner `00128C10` (node `007AC5E0`) reaches `001288D0`, a worker
the live composition does not bind ("Current state"). Before it the C side
plays a00_04 to its end (op 0x03 and op 0x0D sub 1 run since iteration 8).
Earlier, a00_04 frame 7943 in the script host: `001BA1F0` of the owner
`00825600` faulted at the read `0024D88C` (record `00829DE0`), the dispatch
of `ftab_0024D880[3]`. Before it the C side
plays the arrival's 4,017 rows (86 differ: the arrival stance in rows 0..80
and the message phase in rows 3750..3754) and a00_00..a00_03; the
footstep handler `001EB980` (a00_00 frame 33) and the knife's reach in
a00_03 (the composition's view list, the soft-float facing test of
00189EC0 and the freed padlock in the class-4 list) no longer fault.
Open fidelity units preserved from the measured comparison ("Current
state"): the lock_r54 +0x2DC toggle class (a00_00..a00_03, first at
a00_03 row 49) and the post-break record in [54]'s slot (callback
0x1EA240; first at a00_03 row 106, tick 32027: z -1687.31641 vs the
recording's -1687.104 and the +0x1F0 words), which carries into a00_04
from its row 0. Op 0x0B modes 2 / 3 (001B8020, record
`008285E0`), 001C6120 over the arrival owner's bank, the arrival's voice
sectors and `00242EDB` (00128600) are resolved, as are the chain page's
surface packet `00234FE0` (frame 1), the
`0022DCD0` read `00269400` (frame 0) and the `00156620` stop (the 32-view
owner table). Surface dimensions, retained pickup
models and crate roots are now supplied exactly, with no native game-code
changes. Arrival aligns and plays out, but still has zero certified rows
(the harness's comparison stops at its first differing row, row 0). Movement,
camera, interactions, drawing and both returns remain uncertified.
Standalone tests do not establish live call order or eliminate later
missing workers. The inherited AREA01 a01_03 row-957 difference (the disc
drive's per-read seek time) no longer applies to the comparison: with the
comparison-only drive replay (the user's decision of 2026-10-19,
"Comparison-only replay"; LEVEL2_BINDING.md) a01_00..a01_07 pass row for
row.

With the owner-view capacity, the arrival read views and the surface
program resolved (frames 0..202 run), admit 001B8020's mode 3 and rerun
arrival before expanding downstream phases. Preserve the first fault and bind remaining
workers to their existing owners. Then compare all
4,017 arrival/neutral rows and the ending camera memory, followed by each
route phase and the independent side return. Drawing still requires
original packet and displayed-field comparisons. Promote census rows
only after their native AREA00 behavior is exercised and compared.

Unsampled main-line ticks (review 2026-10-19). Four ticks of the fork
AREA00 main line have no row: counters 35978-35980 (a00_04 ends at 35977,
a00_05's four-frame lead-in starts its rows at 35981) and 38516 (a00_06
ends at 38515, a00_07's two-frame lead-in starts at 38517). Before a00_05
and a00_07 are certified, record them on the fork, hidden and under the
run lock:

```sh
# native arm64 macOS; the decomp's .venv supplies zstandard
nice -n 10 ../Extermination/.venv/bin/python tools/record_level_boundaries.py \
  --route-set build/fork_refs/s87/route_a00 --beats a00_05_ferry_deck,a00_07_duct_to_ne_room \
  --out-root ../Extermination/build/fork_refs/levels-boundaries-<new>
```

`--route-set` reads the set's `manifest.json` instead of the supplemental
index and applies the same replay and full endpoint check. Status:
**blocked**. The 2026-10-19 attempt
(`Extermination/build/fork_refs/levels-boundaries-20261019-2/`) resolved
a00_05's gap (4, three missing counters) and its source/target hashes, then
waited in the run lock: `build/.pcsx2.lock` is a 0-byte regular file dated
2026-10-18 13:33 with no `owner.json` and no open handle, not the lock
directory the session takes. The waiting run (no emulator started) was
stopped by its own process id. Only the lock's holder or the user may
remove that file.

Stage-5 native evidence, all on the unchanged executable built from
`11d2f10` (same native source as `ab1cd94`), with input-only replays:

| Private receipt under `build/levels/` | Result |
|---|---|
| `area00-arrival-before-1/` | `00248290` read refusal, arrival frame 0 |
| `area00-arrival-surface-1/run.log`, `receipt.json` | Surface read passes; `001B1020` reports missing retained model |
| `area00-arrival-model-diagnostic-1/run.log`, `area00-readview-preflight-1/diagnostic-parity.json` | Private logging build identifies caller `0015AFA0`, missing model address `00C7CBC0`; ticks and RNG counter/state match the preceding run |
| `area00-arrival-library-1/run.log`, `receipt.json` | Model construction passes; crate selector `0024A850` is missing |
| `area00-arrival-crate-1/` | Arrival aligns at 26925 (drive timing; 26634 at host speed in `area00-arrival-hostspeed-review-1/`, the same 291-tick offset as `a01_arrival`'s 15299 / 15008); 32-view capacity fails at `00156620`, frame 0; 254.403 seconds, native parity false |

The final resource pack has 53 effect windows and 16 retained model
regions. Generated assets are private regular files in this worktree.
No protected AREA11 or rendering source was changed during stage 5.
Intermediate native tick/RNG files are superseded by the final replay;
their hashes and the diagnostic comparison are retained in the private
preflight receipts. Original captures and the before/final native pair remain.

Final stage-5 regressions pass with the final private assets installed:
AREA11 through `a01_arrival` and AREA01 through `a01_02`, each with default
and GPU rendering. All twelve tick, RNG and presented-frame files are
byte-identical to the stage-3 after-capacity baseline (`gpu` frames compared
by tick, RNG and frame order, as above). Asset hashes stayed
unchanged throughout all four final runs. New Game control is 9.599849.
Combined receipt: `build/levels/area00-readview-preflight-1/final-regressions.json`;
default traces: `build/levels/area00-final-defaults-2/`; GPU traces:
`build/levels/area00-final-regressions-1/gpu1-a01_arrival/` and `gpu1-a01_02/`.
Those runs' `ticks.jsonl` files were byte-identical to
`capacity-before-smokes-2/`'s and were deleted by name; their hashes stay in
the combined receipt and in LEVEL_ASSET_DELIVERY.md. Since the 2026-10-19
review, every `build/levels/` receipt lives in the port checkout's ignored
`build/levels/` (moved out of the `levels` worktree).
The isolated staged-index native build has zero warnings
(`build/levels/area00-index-build-1.log`). Eighteen readiness tests and the
unchanged AREA00 harness contract also pass. These regressions do not
certify an AREA00 gameplay frame.

## Topic commits

Earlier prerequisite checkpoint:

- `d8b1916`: borrowed AREA00 initializer and canonical progress access.
- `b3f3b16`: verified texture/shadow exports and production parser checks.
- `c654a3b`: strict replay phases, checker and positive/negative harness.
- `595cea7`: binding ledger, census and smoke documentation.

Continuation:

- `74c2844`: AREA00 texture/shadow selectors, sound scopes, module deliveries,
  immutable placement lookup root, strict delivery/parser regressions.

- `6e43d7f`: canonical AREA00 scene/state/runtime/script bindings, shared
  spawn/world/effect adapters, logger and original-reference tests.

This ledger and the smoke/census updates form the following documentation
commit. Both isolated source checkouts and private fixture binaries were
removed after verification; cited logs, reports and native-run receipts
remain. No completed playable level is claimed.
