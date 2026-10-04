# AREA01 static collision kinds

The native crate initializer failed because the collision world had no
placement-kind view. Original `0019F730` reads that byte before testing a
static hull. The existing walker correctly refused the missing data.

The original C and matching-assembly source of `0019F730` were inspected.
Its first pass stops on a directory entry without bit 31, skips bit 30,
then reads the unsigned byte at `placement[i * 0x28 + 8]` through
`D_0024D7C0[area][sub]`. It stages that byte in `70003B88` and applies the
kind gates before the hull bounds. Kind `0x52` requires query class two.

Every one of the 16 AREA01 captures has 38 cell entries and resolves the
placement pointer to delivered overlay address `0082BD50`. Entry zero is
`A000009C`, kind `0x52`; entry one is `C0000448`, kind `0x51`, skipped by
bit 30. Entry two ends the static prefix. The complete 38 placement
records equal the original overlay in every capture: 24,320 bytes checked.
The existing cell owner already handles entry zero's uncached RAM mirror.

Crate node `007AAB70`, uid `1900`, calls `0019AB20` from its `001551B0`
initializer. Its query class is four, so the original skips entry zero
after reading kind `0x52`. A null native kind view instead fails before
that gate, regardless of the crate's position.

The integrating lane added
`em_collision_world_bind_static_kinds(placements, count)`. It derives the
bounded byte projection required by the existing walkers from the roster's
immutable records, inside the existing world owner. Unload clears it.
The AREA01 scene binds it from `s_roster01`; no actor or collision world is
duplicated. AREA11 continues without a kind view because its static pass
stops at entry zero.

`tools/test_area01_static_ground_reference.py` and
`tests/area01_static_ground_bridge.c` exercise this actual public setter
and the canonical live collision world. The fixture loads the real AREA01
EMCL and directory, then supplies captured hull transforms and published
actors. It executes the original crate initializer with only its state
byte reset to zero, catches its first query, and compares that query with
all original ground-query callees running. This is an initializer
perturbation, not an unmodified route replay. Probe X/Z scratch values are
preserved from the executed caller.

- Quick: PASS, 144 ground cases in 3.857 s.
- Full: PASS, 928 ground cases in 7.978 s.
- Both include all 16 captures, the missing-kind failure, all projected
  bytes, class 0/2/4 gates, upward/downward and inside/outside queries,
  real grid results, rejected bindings preserving the valid view, unload,
  and a later AREA11 load. There are 22 failure/cleanup contracts.
- Every ground result field is compared bit for bit using the existing
  actor-collision oracle. Observed return kinds are clear/grid (0/4);
  this test does not claim a static-cell contact. The original static-hull
  paths execute for admitted class-two queries.

Strict C compilation, Python compilation, scoped whitespace and
no-disassembly checks pass. Receipts are in ignored
`build/level2/static-ground/` (the first quick console log is under
`build/level2/runtime/`). No capture, original bytes or assets were added
to source. This focused proof does not establish a native AREA01 route
milestone; that requires the continuing live probe.

The checkpoint includes the public collision-kind API and focused proof;
the scene call described above belongs to the larger guarded integration.
