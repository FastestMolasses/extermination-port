# AREA01 math workers over canonical memory views

The optional `EmA01Math.view` callback lets the existing AREA01 math and
light translations access their native memory owners directly. It does
not allocate an EE RAM image, copy captured state, bind a worker, or enable
AREA01 world frames. Existing reference tests retain the linear RAM and
scratchpad mode by leaving the callback null.

The core removes the two existing RAM mirror prefixes before asking the
host for a span. Scratchpad addresses keep their original prefix. The host
must return the entire span in its canonical owner, or refuse it. Reads
and stores carry distinct access flags so immutable assets can reject
stores. A refused mapping latches the original requested address, including
its mirror prefix. No linear-memory fallback follows a refused callback.
The existing store count and ordered store trace remain active.

This is a storage adapter, not a translation of another original function.
The first intended callers are `001C02E0`, `001D0C80`, `001D0D40` and
`001C50B0` through their existing math core. Their actor, slot, loader,
scene and scratchpad owners must supply the eventual spans. Returning a
mutable pointer is not permission to duplicate any of those owners or to
retain a loader pointer across a module load.

`make test-area01-math-views` passes with ASan, UBSan and warnings as errors.
Its synthetic fixture runs the real `001D0D40`, checks direct alias identity,
both RAM mirrors, scratchpad boundaries, canonical worker writes becoming
visible immediately, read-only resource refusal, wrapping/out-of-range
requests, trace addresses and refusal of all later work after a fault.
The default linear mapping is checked in the same executable.

The complete existing original-instruction math suite was rerun:

- Quick: passed in **5.02 seconds**.
- Full: passed in **287.86 seconds**, including **48,851 direct bone-helper
  cases**, **1,606 cases / 7,656 boundaries** for their caller, and all
  existing full math cases. The two previously recorded unrelated branch
  gaps in LEVEL2_BONE_INIT.md are unchanged.
- The light oracle also passes: quick **161 cases / 216 boundaries**;
  full **2,309 cases / 2,536 boundaries**, plus its six fail-stop contracts.

Receipts: `build/level2/oracles/math_views_summary.json` and the four logs
named there. The original-instruction sweeps exercise the existing linear
oracle storage; the sanitizer fixture proves the new direct-view contract.
Neither result certifies a future live actor adapter.

Shared edits: `em_area01_math_core.c/.h` (optional direct view and bounded
scratchpad span test), `tools/test_area01_math_reference.py` (matching
ctypes layout), and `Makefile` (math-view and light-oracle targets). New
file: `tests/area01_math_views_test.c`.
