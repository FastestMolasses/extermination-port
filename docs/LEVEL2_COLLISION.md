# AREA01 collision prerequisite audit

2026-10-03, level2 worktree. No emulator was launched. This audit reads the
original C, the locally generated original instructions, the existing native
owners and the recorded route. No gameplay return value was changed.

## Cell directory

The reported uid-0 bit-29 rejection is already fixed. The single owner is
`em_actor_cells_hull_offset` in `src/game/em_actor_collision.c`: it removes the
directory flag bits, then maps the uncached main-RAM mirror to its physical
offset. `em_actor_cells_init`, the hull readers and the transform use that
owner. AREA01's arrival already loads this directory through
`w_001AFCA0` in `em_scene_bindings.c`.

## 0019D770 no-span path

The discrepancy is real, but replacing the port's fault with a miss would be
wrong. `src/game/em_coll_segment_walkers.c:grid_walk` returns -1 when none of
six spans is shorter than the node count. Original `func_0019D770.c` is
NEARMISS; the original instruction body in the decomp's ignored
`build/asm/matchings/main/code/func_0019D770.s` confirms that its start, end and
column registers are assigned only when a strictly shorter span wins. The
choice finishes at 0019D978. The only direct caller is 0019A910, at 0019AA04.
Its incoming values are the mode byte, a stack address and an inherited
register. These are not a defined empty span. The existing native API has no
representation of those caller registers.

The existing oracle now checks a narrower, useful fact about the recorded
AREA01 route. It also constructs a whole-grid segment and executes the
original selector to demonstrate that the undefined arm really exists for
this data. Thus the data alone does not prove general unreachability.

### Bound and evidence

The original rank search 0019F1A0 returns ranks in [0, N-1]. The recorded
AREA01 sub-0 grid has N=854; all six helper columns lie in [0, N]. Therefore
a failure to choose any span requires every span to have length N:

- For each even column, the live upper rank must be N-1 and its helper 0.
- For each odd column, the live lower rank must be 0 and its helper N.
- Since the coordinate columns are sorted, the Z endpoints must then cover
  at least the interval from the second odd-column coordinate to the last
  even-column coordinate: a Z separation of **1087.16015625** or greater.

`tools/test_coll_segment_walkers_reference.py:area01_span_domain` checks the
entire rank/helper data and all coordinate columns in **15 AREA01 snapshots**
against this grid. It checks **19,798 recorded AREA01 boundary rows**, including
main and side beats and excluding AREA00 rows after the exit, using the same
four camera-query shapes as the existing camera reference tests: camera
target to eye, eye to target, and eye to 200 units above/below. All **79,192
query shapes** have Z separation at most **53.519050**, far below the bound.
A 0.01 allowance covers decimal rounding in the trace; it is a test allowance,
not a gameplay clamp.

The original 0019D770 instructions, including all four original rank-search
calls, are executed through 0019D978 for the widest recorded query and the
whole-grid witness. The former chooses a span; the latter retains three
distinct seeded caller-register values. The witness stops before the unsafe
walk, and is not presented as a native/original outcome comparison.

This proves the no-span arm unreachable for those recorded boundary query
shapes. It does **not** prove the same of every mid-frame camera call, a later
area, arbitrary endpoints, malformed rank tables or camera paths added by
future binding work. The native fail-stop remains. Full live AREA01 smoke
must exercise the actual query calls before this is called a live guarantee.

### Verification

The domain check runs within the existing
`make test-coll-segment-walkers-reference` target; it adds no new target and
does not change gameplay code. If the AREA01 captures are absent it reports
that portion as skipped explicitly. The regular first-level comparisons stay
unchanged. Quick/full results are recorded by the parent binding task.

The quick run passed in 13.8 s. The full run passed in 1,373.7 s: all
12,439 first-level rows (seven queries each), 15 captured re-runs, 3,000
cases for each of three unit families and 600 synthetic worlds. It checked
25,598 original hull-lock calls and 12,497 hit views, plus the AREA01 domain
and original selector witnesses described above.

## Remaining collision binding work

AREA01 owners first reach 0019B4C0 -> 001A06A0 / 0019CF50. Their standalone
owners are in `em_area01_sys.c`; they need the canonical collision
scratchpad, static grid, current class lists and actor record resolver.
`001AA000` already has a live owner in `em_coll_list_passes.c`; use it rather
than the standalone duplicate in `em_area01_sys.c`. The shared player,
camera, segment and list-pass modules remain the owners of functions already
bound for AREA11. Arrival already provides AREA01's EMCL and cell directory.
