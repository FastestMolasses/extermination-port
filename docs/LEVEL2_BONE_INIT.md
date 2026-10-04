# AREA01 bone-slot initialization helpers

2026-10-03. The two arrival-only helpers 001D0C80 and 001D0D40 are now
translated in the existing `em_area01_math_owner.c/.h`. They remain unbound
to a live AREA01 world. Their original caller, 001C02E0 state 0, now calls
these implementations directly within the standalone module.

Before adding them, all native `src/` references were checked. The only
references were worker declarations/call-outs in AREA01, AREA04 and level 8;
there was no native definition. The existing bone allocator in
`em_owner_services_original.c` belongs to 001B0EA0/001B0DC0 and tests capacity
against the bone count, while 001D0C80 reserves one additional animation
record. It is not the same original function and was not substituted.

The original sources are the byte-matched
`../Extermination/src/func_001D0C80.c` and `func_001D0D40.c`. The caller is
`src/func_001C02E0.c` in that repository; its NEARMISS label is not evidence,
so the changed native caller is compared with its original instructions.
No decomp source changes were needed.

## Behavior and dependencies

001D0C80 binds the model using 001CA5E0, reads its count through 001C6150,
and stores the count as a byte in the node. If the signed halfword free-slot
count is less than the bone count plus one, it sets lifecycle state 3 and
returns 1. Otherwise it allocates the node's bone slots through 001AF780,
reads the node's count again after every allocation, stores the held count,
calls 001CB5B0, and allocates the extra animation record. It returns 0.

001D0D40 follows the node's animation-record pointer once. It stores the
table address, converts the signed frame count to an EE float, clears the
time word and stores the low-byte loop flag. Conversion uses
`em_ee_cvt_s_w_bits`; every read and write uses the existing `EmA01Math`
view and store tracing. No new global memory or allocator was introduced.

| Original worker | Required binding |
|---|---|
| 001CA5E0 | Existing model binder over the node's canonical record |
| 001C6150 | Existing model count service (`em_owner_services_001C6150`) |
| 001AF780 | The existing canonical slot stack; no AREA01-private stack |
| 001CB5B0 | Existing bone-source setup on the same node and stack |

The live adapter must supply these services and map D_00275BCC to the same
stack's free count. An absent worker or bad address keeps the math core's
latched fail-stop behavior. In 001C02E0, the original continues after
001D0C80 even when its game result is 1; the native caller preserves that
control flow rather than inventing an early return. A port fault still
prevents all later stores/calls through the shared core.

## Verification

`tools/test_area01_math_reference.py` now includes both original ranges and
executes them inline when testing 001C02E0. It compares memory at every
external worker entry, argument registers, worker ordering, final memory
and integer results against the original instructions. The normal
fail-stop contract also exercises both new entries after a latched fault.

The new direct cases use AREA01 captured node records. Designed worker
boundaries exercise signed capacity (including negative and exact-limit
cases), count-byte narrowing, neighboring-byte preservation, an allocator
that changes the count during the loop, and animation-record aliasing with
the node's own pointer field. Conversion cases include zero, negative
counts, both signed integer extremes and values above float's exact-integer
range. Full mode expands to every count byte and additional deterministic
integer patterns. These synthetic cases are not claimed as recorded
gameplay.

Quick run on native arm64 macOS, `EM_TEST_JOBS=4`, passed in **8.1 seconds**:

- 001D0C80: **40 cases, 1,443 worker boundaries, all 4 branch outcomes**.
- 001D0D40: **21 cases**, no calls or conditional branches.
- The changed caller 001C02E0: **57 cases, 363 worker boundaries, all 24
  branch outcomes**, with both new helpers executed inline.
- The complete existing math quick suite also passed, including the
  other actor, owner, script, player and table cases.

The quick receipt is `build/level2/oracles/area01_math_bone.log`. Full mode
is run as `EM_TEST_FULL=1 EM_TEST_JOBS=4 python3
tools/test_area01_math_reference.py`; its receipt is
`build/level2/oracles/area01_math_bone_full.log`.

The complete full suite passed in **458.7 seconds**, using all twelve
original math-lane captures (the direct bone grid uses the eleven AREA01
captures, excluding the AREA00 exit snapshot):

- 001D0C80: **14,135 cases, 760,551 worker boundaries, all 4 branch
  outcomes**.
- 001D0D40: **34,716 cases**; together the new helpers have **48,851**
  direct cases.
- 001C02E0 with both helpers inline: **1,606 cases, 7,656 worker
  boundaries, all 24 branch outcomes**.
- The existing full cases also pass, including all 75,297 actor-table
  cases. Two unrelated pre-existing branch outcomes remain uncovered
  (001BBAE0: 9/10; 001C2770: 119/120); this result does not claim complete
  coverage of every routine in the module.

This proves the standalone functions and changed standalone caller on
these inputs. It does not prove the future AREA01 adapter, a connected
route, live slot ownership or rendering. No emulator was launched.

Shared-file edits in this step: `src/game/em_area01_math_owner.c`,
`src/game/em_area01_math_owner.h`, and
`tools/test_area01_math_reference.py`. The changes add two public entries,
replace the caller's two worker calls with direct same-module calls, and
extend the existing oracle. No Makefile or first-level status file was
changed by this step.
