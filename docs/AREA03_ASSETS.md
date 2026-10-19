# AREA03 recorded asset delivery

Stage 3 checkpoint, 2026-10-18. `tools/export_area03_assets.py` builds the
recorded sub-0 profile in this worktree's private `assets/levels/03/`.
All data comes from the local disc/extract; the two retained snapshots,
`a19e_07_door25` and `a03_00_elevator`, independently check delivery.
This does not bind gameplay or certify the unrecorded subs 1/2.

## Outputs

| Output under `assets/levels/03/` | Source and verification |
|---|---|
| `sub0/level/00_id44.emdl`, material sidecar | 467 static objects; disc-upload texels equal both GS freezes; captured draw-state checks |
| `sub0/level/static_bank.emsc` | Complete directory, grid and object blocks; every byte equals original RAM |
| `sub0/area03.emcl` | Grid collision rebuilt independently from the captured original pointers and tables |
| `sub0/area03_cells.bin`, `cells.json` | 29 UIDs, 17,716 bytes; moved hulls derived by original 001A2370 / 00219F50 machinery |
| `sub0/roster.emro` | 21 placements and one group of 15; independent original table walks and captured records |
| `sub0/world_models.emwm` | 18 complete models; original delivered bytes, header/bone bounds and native parser |
| `sub0/message_data.emmd` | Original global and area message banks; captured pointer/record re-derivation |
| `spawn_table.emsp`, `door_destinations.emsp` | Four spawn rows and twelve door records; original table walks, capture comparisons and native loaders |
| `scripts.emsc`, `overlay_data.emsc` | Five source-named chains, 77 distinct records; full overlay windows and every observed mutation checked |
| `sub0/sfx/area03_banks.bin`, `sfx_registry.emsr` | 1,000 scoped entries, 78 samples; disc containers and full registry re-derived from captured driver tables |

The descriptor relocates no dynamic-object list. Its retained pointer must
equal the preceding AREA19 snapshot, and every recorded display list must
contain no dynamic-kernel draw. No stale pointer is exported as geometry.

`export_area03_common.py` configures the existing AREA13 delivery pipelines.
AREA03's turret owner `00823930` supplies the matrix at
`*(node + 0x11C) + 0x90` to its four cell-hull transforms. The readable
overlay C contains no cell-flag or overlay-door caller; a direct-target scan
also checks the pinned original overlay, including its pre-existing init
unit. Unexpected hull changes, data writes and calls still fail.

## Retained render state

The shared context check initially rejected `D_00251C20..2B`, three colour
words changed during AREA15's preceding event. AREA15 owner `00826850`
installs draw class 12 for its kind-0x0D node. The original `001D4440`, run
over that node in `a15b_00_event` with all twelve destination bytes poisoned,
reproduces the three words exactly in both AREA03 captures. Its original
float-to-integer conversion executes; unrelated display-list workers are
explicit boundaries. No captured colour is copied into the expected result.

Only those independently derived expected bytes replace the initial ELF
values during the context comparison. Every other existing comparison stays
in force, including the original room-block rebuild and its dependency
mutations. A changed retained colour is a required failing control.

## Reference test

`make test-area03-assets-reference` checks one arrival by default;
`EM_TEST_FULL=1` checks both snapshots. It uses the existing independent
per-category readers, runs twelve actual native loaders, and rejects five
controls: changed static-bank, cell and model bytes, a changed captured
retained colour, and a truncated roster. The earlier-area tests are unchanged.

Full export and reference receipts are under
`build/levels/area03-profile-2/`; the detailed full reference report is
`build/levels/area03-assets/reference/report-full.json`. All generated material
and detailed diagnostics stay private and ignored.

## Remaining proof and requests for main

The world texture catalog is separate. Its bank/shared-source probe verifies
384 nonzero texture keys against both freezes, but the tendril resource also
has zero-key records whose draw mode needs separate classification. The
module pack and 467-object shadow bank already passed their full delivery
tests. SPU sample residency, later events, unvisited subareas and live
rendering remain unverified. No shared live source change is requested by
this AREA03 asset checkpoint.
