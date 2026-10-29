# AREA02 pre-translation survey (a01r_03, a02_00 .. a02_05, a02_s0)

2026-10-29, revisit agent R1, branch `revisit`. Purpose: get ahead of the
frontier (F1 moves from a01r_02 into a01r_03 and AREA02) by listing every
original function the original runs on the fourth level's remaining route
(FOURTH_LEVEL_ROUTE.md: a01r_03 into AREA02, the AREA02 main line and its
side beat) and translating the ones the port lacks.

**Result: no translation is missing.** Every function the census saw has
one of the following:
- a port definition that names its address;
- a native boundary;
- a translation under another name, or as part of a split function.

No new translation files, Makefile entries or reference tests were needed.
None of the functions lacks readable C. What remains is **binding**:
AREA02 has no live composition yet (section 2).

## 1. Method

**Census.** A hidden, owned fork census, run under the run lock:

    route_census.py run --segments a01r_03_door16,a02 --pass A02F

The outputs are addresses and frames only, under the decomp's
`build/fork_refs/s87/census/runs/A02F/`; the log is
`build/fork_refs/probes_1028_a01r_survey/a02_census.log`. The census
arms one-shot entry breakpoints on the census candidates: the boot ELF
plus the AREA01 overlay for a01r_03, and the boot ELF plus the AREA02
overlay for the a02 beats (runtime address = splat label + 0x40). It
re-arms them for every beat. Every beat completed:

| Beat | Frames | Functions | Rows equal to the recording |
|---|---|---|---|
| a01r_03_door16 | 941 | 742 | 731 / 941 |
| a02_00_duct | 1422 | 691 | 1423 / 1423 |
| a02_01_switch | 629 | 849 | 630 / 630 |
| a02_02_ladder_escape | 1013 | 606 | 3 / 1014 |
| a02_03_over_wreck | 830 | 582 | 831 / 831 |
| a02_04_panel | 1576 | 707 | 1577 / 1577 |
| a02_05_progression_exit | 1725 | 807 | 1726 / 1726 |
| a02_s0_mts_bed | 1007 | 595 | 1008 / 1008 |

Two replays left their recorded rows early: a01r_03 at row 731, and
a02_02 from its first rows. Both still finished their beats, so their
function sets are what the original ran on the replayed paths. The
legacy census pass A02 (`a02_delta.json`) lists the same AREA02-first
functions.

**Union: 1,331 functions.** In the a02 beats, AREA02 overlay hits are
keyed to the function that contains them. The a01r_03 hits in AREA01
code are the ones AREA01_REVISIT.md's survey covers. Four hits at the
end of a02_05 fall in the next area's overlay (id 5); they belong to
the fifth level's census and are not counted here.

**Port coverage.** A function counts as translated when a function
definition names its address, using level_readiness.py's
source_definitions and its ALIASES:
- 861 are named that way.
- 341 are native boundaries, by the census classification.
- 116 are translated under names without the address: the classified
  module exists in `src/game`.
- 13 were checked by hand:
  - 0016D130: a player state function.
  - 001790B0: in the em_player_closure_0e_18 group.
  - 001A58B0: em_actor_collision's column_round.
  - 001B62C0: em_item_stick_sample (AREA02_MISC.md).
  - 001B7A30: em_area_script op 0x0F.
  - 001C0004 / 001C2FF0: the false-split tails of 001BFFD0 / 001C2770.
  - 001CD070: em_area01_render_001CD070.
  - 002082B0 / 00208AD0: the status pages' arc and health gauge.
  - 00209860: em_status_ammo_draw (AREA02_UI.md).
  - 0020AC70: em_item_trail's analog trail.
  - AREA01 0x8240E0, a02_05's return into AREA01:
    em_area00_return_008240E0.

## 2. Binding stops ahead

AREA02's 16 overlay entries are translated and compared with the original
in `em_area02_overlay.c` (AREA02_OVERLAY_PORT.md, test_area02_overlay_reference.py),
but nothing calls them: there is no AREA02 runtime or live composition like
AREA01's (em_area01_runtime / em_area01_live). The census confirms which of
them the route reaches: 0x823580, 0x823900 (the area init), 0x823930,
0x823980, 0x823D70, 0x824020, 0x8242F0, 0x824800, 0x824910, 0x824AC0,
0x824C40, 0x824CD0, 0x824D50, 0x824FA0, 0x825100 (0x825520 is reached only
through 0x825100's D_0081083F == 2, not on the route).

A binding lane needs the following:

- **Memory.** An AREA02 host that serves `bytes` over the scene's storage:
  - the actor pool and the 0x810000 story block;
  - the boot data and the overlay data;
  - the scratchpad, and the stack frames below `sp`.
- **Dispatch.** The area init 0x823900 called where the original runs it.
  The pool owners dispatched by their +0x10 word: 0x823930, 0x823D70,
  0x824FA0, 0x825100, and 0x823580 for the spawned node.
- **Hooks.** Each of the owners' hooks paired with its existing port
  translation by address, as em_area01_runtime does for AREA01. The list
  is in AREA02_OVERLAY_PORT.md "Binding". Every callee is among the
  1,331 above, so each has a translation or is a native boundary.

Until that host exists, an EM_START_AT run (or the New Game route) stops at
AREA02's area init. The first stop F1's frontier will meet is a01r_03's
area change; the survey shows nothing to translate before it.
