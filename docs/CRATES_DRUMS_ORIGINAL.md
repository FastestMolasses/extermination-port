# AREA11 crates and drums: original owners 001551B0 and 00156620

Lane k5-crates-drums (WP-18: W19, W20, W21, INV-19). Modules:
`src/game/em_crate_original.{h,c}` (001551B0, records area11[3..6]) and
`src/game/em_drum_original.{h,c}` (00156620, records area11[14]/[15]).
Both are one owner call per tick. They take the owner state, a small input
block and a worker table, and have no hidden globals.

## What the records are (from the code and the captures)

In all three AREA11 captures the live list holds four 001551B0 nodes and two
00156620 nodes:

| node | owner | model byte | class byte | position | +0x52 | +0x0E / link |
|---|---|---|---|---|---|---|
| 0x7A7980 | 001551B0 | 6 | 0x04 | 214.7, 203.8, 292.8 | **1** | 0x700 / -1 |
| 0x7A7C70 | 001551B0 | 6 | 0x04 | 228.8, 189.8, 292.8 | 0 | 0x800 / -1 |
| 0x7A7F60 | 001551B0 | 6 | 0x04 | 214.7, 189.8, 292.8 | 0 | 0x900 / -1 |
| 0x7A8250 | 001551B0 | 6 | 0x04 | 311.4, 249.7, 328.1 | 0 | 0xA00 / -1 |
| 0x7A99D0 | 00156620 | 0x18 | 0x04 | 300.0, 249.7, 327.9 | - | - |
| 0x7A9CC0 | 00156620 | 0x18 | 0x04 | 292.2, 249.7, 326.1 | - | - |

**001551B0 is a breakable, stackable box, not a crawler.** It never reads the
player. The only inputs that change it are its own +0x36 damage word and the
+0x0A byte other boxes write. The code, by state:

- **INIT (state 0).** The owner runs 001B0FD0 and sets HP 1. It snapshots the
  world matrix to +0x1F0 and builds four corner points: the local offset
  (r, 0, r) is taken through the world matrix, with r = 4.5961943 for models
  6/0x1E and 2.1213202 for 0x1C/0x1F/0x50. A floor probe (0019AB20, mode 7,
  from y-2, dy -3) then sets +0x52 = 0 when the result is 4 (world ground)
  and 1 otherwise. In the captures only 0x7A7980 has +0x52 = 1. That box
  sits 14 units above 0x7A7F60 at the same x/z, so it rests on the other box.
- **REST (state 4).** Nonzero +0x36 sets +0x00 = 2 and state 2. The owner
  then walks the whole D_00275BC0 list and writes +0x0A = 1 on every node
  whose model byte is 6, 0x1C, 0x1E, 0x1F or 0x50 and whose +0x52 is
  nonzero. There is no radius check. It clears its own +0x0A last. A set
  +0x0A moves the box to state 1 with +0x2A = 6.
  - The rattle runs only while +0x0E bit 0 is set, which means the box still
    owes its nest group a child. It plays sound 0x19C at radius 300, waits
    for an RNG draw of 0..255 ticks, then shudders on wait values 1/3/5/7
    from the D_002468B0 table. The AREA11 boxes have bit 0 clear.
  - The state then copies the world matrix to *D_00275B40 + 0x90, calls
    +0x4C, and publishes through 001B1B70. For class 4
    that is 001B1D20.
- **ALERTED (state 1).** Phase 0 probes the four corners (mode 7, from y-1,
  dy -2). A corner counts as supported when the result is 2 and D_700031D4,
  the hit actor, is nonzero. The box holds when three or more corners are
  supported, or when both corners of an opposite pair are, or when the
  centre probe is supported. It returns to state 4 when +0x2A reaches 0.
  Otherwise it tips:
  - One or two supported corners: +0x28 = 30 counts down, and while it
    stays >= 2 the world matrix is multiplied by a ±0.0524 rad step about X
    or Z. That is 28 steps, about 84°, with no translation. The launch tick
    counts as one of the 28.
  - No supported corner: +0x28 = 0 and the step is a random euler kick.
  - After the tilt the fall is ballistic. The horizontal velocity is 11 × the
    tilt angle, the vertical velocity loses 0.052000001 per frame
    (0x3D54FDF4), and the step keeps turning at angle/1.4.
  - Result 4 on the landing probe, or +0x2A < 0, clears +0x36 and moves the
    box to state 2. The +0x2A limit is 60·y/12 frames for model 6 and 180
    for the other models.
- **BREAK (state 2).** Phase 0 does the following in order:
  1. When link >= 0, spawns the nest group: the D_0024A850/D_0024D820
     records not yet taken, allocated through 001AFA90, with 12 fields
     copied.
  2. Plays the model's sound and two effects. Model 6: 0x19D with
     0x8000000A/0x80000015.
  3. Only on a damage break (+0x36 != 0) of model 6 or 0x1E: turns the box
     by an RNG draw of 0, 90, 180 or 270 degrees about Y, with its
     translation kept. It then rebinds the box to model 0x22 (0x29 for 0x1E)
     through 001C6120/001CA6E0, followed by bone_init. Every other break
     goes to state 3.
  4. Phase 1 applies only while +0x52 != 0: it re-probes the corners in
     mode 6 and frees the husk when fewer than three are blocked.

  So the husk of a shot ground box stays drawn for good. The only box that
  can later drop its husk is the raised box 0x7A7980.
- **FREE (state 3 or any other value).** Model 0x50 first sets its taken bit
  (001B1190), then the owner calls 001AFC10.

Oracle confirmation over the real captured list: damage on any AREA11 box
wakes exactly 0x7A7980 (`broadcast_playable`/`broadcast_handoff` in the
report).

**00156620 is an HP-1 fixture; model 0x18 has no debris flight.**

- **INIT.** HP 1, and +0x200/+0x210 take a copy of the position and rotation.
- **State 1 (armed).**
  - On +0x36: +0x00 = 2 and state 2. A vertical segment test (0019A570,
    mask 4, pos ±4) runs; if it hits, the owner calls 001F0460(4, M): effect
    preset 4, with M = identity · Rx(π/2), translated to the position with
    y + 0.2. That is a flat effect under the drum; which preset 4 is has not
    been checked.
  - Every armed tick then calls +0x4C and runs the distance test.
    - If dist² to D_00810350 is <= 50·50, the owner sets +0x01 = 1 and calls
      001B1D20 (class-4 list D_00275B80).
    - Otherwise it calls 001B17A0, which writes +0x01 from 001B1630 and,
      when visible, publishes through 001B1B70. For class 4 that is the same
      001B1D20.
  - So the 50-unit compare is a **visibility override**, not a shootability
    gate.
  - The auto-aim walk 00199220 reads D_00275B8C. That list is the published
    copy of D_00275B90 (001AAD00). Only 001B1CA0 pushes onto it, and only
    001B1B70 calls 001B1CA0, for classes 2/0xA. **These class-4 boxes and
    drums are never auto-aim candidates.**
- **State 2.**
  - Phase 0 fires FX 0x80000013 at pos + (0,7,0). Model 0x18/0x2A adds FX
    0x8000001C and sound 0x1A1; the other models add FX 0x8000002E and sound
    0x19F.
  - Phase 0 also sets +0x28 = 2 and draws the speed (D_00246A00) and lift
    (D_00246A10) values. Model 0xA/0xC also draw a heading.
  - Phase 1 counts down two ticks, then model 0x18/0x2A goes to state 3 and
    is freed on the following tick.
  - Phases 2 and 3 are the flight arm, used only by models 0xA/0xC, which
    AREA11 does not place:
    - It uses sin/cos of the heading, a 0019AD00 sweep toward pos + 7·(sin, ·, cos) at y + 7 (a hit zeroes the speed) and lift decay of
      0.06 per frame, clamped at -4.
    - A floor probe from 0019AB20 (mode 0x80000007) ends the flight. So does
      a second hit without bit 0x2000, the 001B0D80 check (y < -200) on ticks where
      (frame + dispatch index) & 0x3F == 0, or area 0x15 with y < 5.
    - Each of these ends in FX 0x80000013 + 0x8000002E and sound 0x1A0 at pos
      + (0,4,0), except the area-0x15 arm, which uses FX 0x8000005F at
      (x, 10, z) and 3D sound 0xDB at 800.
  - Every state-2 tick ends with 001C6380, 001A2370, 001B17A0 and +0x4C.

## Verified

- `python3 tools/test_crate_original_reference.py` runs 5324 ticks: 536 from
  the four captured records in playable_ee.bin and handoff_ee.bin, plus 1500
  synthetic sequences. It also runs 3600 SDK math cases.
- `python3 tools/test_drum_original_reference.py` runs 5638 ticks: 300 from
  the two captured records, plus 1500 synthetic sequences, including the
  0xA/0xC flight arm and the area-0x15 low-y arm.
- Each tick executes the owner and its pure helpers from the pinned ELF:
  - 001B0FD0 and 001B0D80
  - copy_qw4 and 00102948
  - the rotation helpers 001029C0/001029E8/00102A60/00102B08/00102BB0/00102C58
  - the matrix helpers 001026A0/001026D0/00102918/001028D0/00102738
  - float_to_int with 001278C0, and 001B1470
  - the full sin/cos routines 0011E2A8/0011DE90
- Each tick checks the following:
  - every modelled field, bit-for-bit
  - the ordered worker log with all arguments (vectors and matrices as bit
    patterns)
  - the +0x0A byte of every list node
  - the fields of every spawned child
  - that the original wrote nothing outside the modelled fields, list-node
    +0x0A bytes, children, the bone matrix, the stack and the scratchpad
- Captured-run code bytes are checked against the ELF.
- Coverage:
  - Crate: 1294 of 1305 instructions run.
  - Drum: 576 of 580 instructions run.
  - Both tests assert that every instruction not run is a dead duplicate
    after an unconditional `b`, and that nothing branches to it.
- `tests/crate_drum_original_test.c` covers the fault contract and the
  AREA11 shapes: the shot box keeps its 0x22 husk for 100 ticks, the stacked
  box holds or tips, and the drum's preset-4 effect, FX and free timing hold.

EE float rules follow the existing oracles: products, sums and cvt truncate,
and division rounds. The objects show no fused multiply-add under the default
Makefile flags.

## Boundaries (explicit workers; a missing one faults before any write)

- **Crate.** The crate's workers are:
  - allocate_model (001B0EA0) and bone_init
  - publish (001B1B70) and place (001C6380, which writes the world matrix)
  - probe (0019AB20: result plus D_700031D4; mode bit 31 may snap y)
  - random (00122BB8), sound (001FC580), sound3d (001FBD50) and effect
    (001EFD90)
  - taken (001B11E0), spawn (001AFA90 plus the field copy) and rebind
    (001C6120 → 001CA6E0)
  - bone_matrix (*D_00275B40 + 0x90; 001AFD70 → 001CB590 points D_00275B48
    at the node, and FINDINGS derives D_00275B40 = node + 0x110, the bone
    pointer array), draw (+0x4C), set_taken (001B1190)
    and free (001AFC10)
- **Drum.** The drum's workers are:
  - allocate_model, bone_init and place
  - segment (0019A570) and effect_matrix (001F0460)
  - draw, contact (001B1D20) and visibility (001B17A0, which writes +0x01)
  - effect (001EFD20), sound, random and sweep (0019AD00)
  - probe (0019AB20), sound3d, hull (001A2370) and free
- **Inputs.**
  - Area and sub-area.
  - The live list as (model, +0x52, &+0x0A) entries.
  - The registry view (D_0024A850/D_0024D820), which is needed only when
    link >= 0.
  - The rattle table (D_002468B0, 7 rows).
  - For the drum:
    - the player mirror D_00810350
    - the frame word 0x70003B68
    - the dispatch index 0x70003B8A (001AFD70's 1-based walk count)
    - the D_00246A00 and D_00246A10 tables
  - The tables are ELF data, so the caller supplies them from the local
    export and nothing is checked in.
- **Original quirks the module keeps.**
  - The +0x238/+0x23C rattle ints overlay words 2/3 of the +0x230 step
    matrix. They are accessed through `em_crate_original_rattle_*`.
  - When INIT runs with +0x0E bit 0 set and link < 0, it reads s0
    uninitialised. 001AFD70 holds node->next there, so the input is
    `dispatch_has_next`.
- **Faults instead of stray reads.** The module faults on a registry or
  rattle index outside the supplied view. The drum faults on a heading with
  |heading| > 4π, the domain of the verified sin/cos translation.

## Binding (coordinator)

- The coordinator should call these owners from the owner walk:
  - Crates: order #12-15, `em_crate_original_tick(&state, &input, &hooks)`.
  - Drums: order #22-23, `em_drum_original_tick`.
- Only the AREA11 bindings or the coordinator should own the worker tables.
- The collision workers have to reproduce the 0019AB20 result codes: 2 means
  an actor hull and D_700031D4 is the hit actor, 4 means world ground.
  The corner support test depends on those codes.
- Whether a broken husk still supports a box above it is decided by the
  collision worker, not by these owners. The husk stops publishing, because
  state 2 never calls 001B1B70.

### Status (census L25, 2026-09-24): blocked, not bound

The chain step "Census L02 + L25" found that these owners cannot go live
faithfully yet. Each item below is a missing worker or datum; none may be
replaced by a stand-in (fail-stop rule). The crate's first ticks reach
items 1 to 4, the drum's items 1, 2 and 4 (its probe runs only in flight):

1. **Model allocation (001B0FD0 state 0: `allocate_model` / `bone_init`).**
   `em_owner_services_001B0EA0` needs 001C6120 / 001CA6E0 over the model
   bank `*D_0028A59C`, 001AF780 bone slots and 001CB5B0. The port holds no
   AREA11 world model bank (the status models bind their own letter bank
   from a local export, WP-5 decision (b)); OWNER_SERVICES.md "Not ready to
   go live" still applies.
2. **The draw method (+0x4C = 001CAA00).** Its 001CA990 needs 001CA7B0,
   001CA940 and 001D1F80 (untranslated) and ends in VU1 packets. The native
   draw of an owner's model at its +0xD0 matrix is a renderer-boundary
   decision that has not been made for world owners.
3. **The floor probe 0019AB20 (crate INIT, the corner probes; drum
   landing).** `em_actor_collision_owner_probe` is the query half that
   ACTOR_COLLISION.md section 7 item 4 keeps off the live path until its
   prim tests are reduced to em_coll_probe_original's and its float helpers
   and oracle are harmonized (EE_FLOAT_MODEL.md 5c).
4. **The owner walk.** Both owners are members of the legacy
   `em_enemy_update` group whose head is 00825940 (census L24); em_enemy.c
   ticks, draws and breaks all of its records in one pass, so the group
   must be split with L24.
5. **Tables.** D_002468B0 (rattle), D_00246A00 and D_00246A10 (drum speed
   and lift) have no local exporter; the reference tests read them from the
   ELF.
6. **Drum workers.** `sweep` is 0019AD00, whose grid pass 0019CB60 and hull
   lock 001A6440 are untranslated (census L05, blocked); `segment` 0019A570
   (em_coll_segment_walkers, translated), `effect_matrix` 001F0460 and
   `effect` 001EFD20 are reached only after damage.
7. **Damage.** No live port code writes either owner's damage word; the
   legacy break runs on em_enemy.c's own records. Once the owners are
   bound, what writes +0x36 on the live path must be the original's hit
   path, or the boxes stay intact (inert), never a port break.

## Legacy em_enemy.c on these records (read-only comparison)

- **Break.** For model 6, legacy `crate_burst` frees the slot at once and
  launches 3-5 port gib instances. The original keeps the actor, turns it a
  random quarter-turn, rebinds it to model 0x22 and keeps drawing it. The
  original frees a box only on a non-damage break, or when a raised husk
  loses support.
- **Alert.**
  - Legacy runs a repeated forward hop: ENEMY_HOP_SPEED 0.32, ENEMY_HOP_VY
    0.42, and a yaw re-steer.
  - The original never moves forward on its own. It holds when supported, or
    tips through 28 steps of matrix rotation only, then falls ballistically with
    11 × the tilt angle and breaks without a husk when it lands.
  - Legacy's `bl[]` probes are horizontal wall probes along yaw ±45° and
    ±135°. The original's corner probes point down: from y-1 with dy -2, in
    mode 7. A corner counts only when the result is 2 (actor hull) and the
    hit actor is nonzero. So the original tests support underneath, not
    walls around. Both pair geometrically opposite corners.
- **+0x52.** Legacy defaults `on_surface` to 0 for every box. The original
  sets it from the INIT floor probe, and in AREA11 box 0x7A7980 has it set.
  Breaking any box wakes 0x7A7980.
- **Drum.**
  - Legacy frees the drum at once with `egg_explode` (a fireball quad plus
    5-8 debris chunks).
  - The original spawns the flat 001F0460 preset-4 effect on the hit tick
    when the segment test hits. It fires FX 0x80000013/0x8000001C and sound 0x1A1 on the next
    tick, and keeps drawing until 001AFC10 frees it on the fourth tick after
    the hit.
- **Auto-aim.** Legacy `enemy_victim` makes crates and drums (kinds CRATE and
  EGG) auto-aim candidates through `em_enemy_targetable` and
  `em_enemy_acquire`. The original never offers class-4 actors to 00199220.
- **Constants.** FINDINGS s62 quotes the fall gravity as 0.0519999. The
  constant is 0x3D54FDF4 = 0.052000001. The module uses hex-float literals
  for every constant.
