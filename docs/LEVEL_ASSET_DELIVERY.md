# Private later-level asset delivery

Checkpoint: 2026-10-18, branch `levels`, stage 3 in progress. These are local
asset and loader proofs. Later-level gameplay remains unwired. All generated
bytes, captures, detailed logs and binaries remain in ignored directories.

## Existing profiles

`tools/level_asset_audit.py --run build/levels/assets-existing-1` ran the
level, table and SFX exporters, the reload/subarea split where applicable,
and each full `test_area*_assets_reference.py`. All twelve profiles passed:
01, 00, 02, 04, 22, 06, 13, 13x, 19, 19x, 19s1 and 15. The private receipt
is `build/levels/assets-existing-1/results.json`; the tested checkout was
`d6f50b1`. The receipt records command source hashes and exit status, not a
claim of transitive or live certification. The runner refuses asset symlinks.

Each check retains its documented capture selection. AREA00 uses the selected
fork references; many older profiles explicitly read the retained legacy
captures. Setting `EM_REFERENCE=fork` does not rewrite those hardcoded inputs.
AREA03's new level/collision/table/SFX/model/message/script profile now passes
too; its full receipt is `build/levels/area03-profile-2/results.json`.
See [AREA03_ASSETS.md](AREA03_ASSETS.md) for the two-snapshot scope, twelve
native loaders, five failing controls and the original derivation of three
retained render-colour words. It adds 1,000 SFX entries and 78 samples.

The canonical AREA00/AREA01 world-texture and receiver exports also passed,
followed by full AREA00 resource and delivery checks. Receipt:
`build/levels/resources-existing-1/results.json`. AREA00 has 579 texture
keys in each sub; the unchanged registry holds 512 and correctly refuses
the complete catalog. No catalog was truncated.

## Complete module delivery

`tools/export_level_module_pack.py` writes `assets/levels/modules.emml`,
separate from the installed first-level pack. It includes the existing
screen modules and fifteen area/sub pairs: AREA11/0 plus every pair recorded
by levels 2–15. Reload profiles use the same full disc sectors. The pack has
109 ranges and 110,786,544 bytes.

`tools/test_level_module_pack_reference.py` verifies every requested sector
against the local disc, refuses changed bytes and missing coverage, and runs
the unchanged loader oracle for all fifteen pairs in full mode. It compares
every callee entry, frame snapshot, original store, delivered byte, upload
and area latch. The original sound-bank worker remains an explicit scripted
boundary, returning each capture's post-bank cursor; this does not certify
sound processing or a live transition.

The first test exposed a harness assumption: an overlay whose file ends
inside a sector can have its native allocation split by the BSS clear.
The new test joins adjacent public native memory views and compares every
byte with the original interval; a gap still fails. No loader or existing
assertion was changed. Thirteen reads required that comparison view.

Receipts: `build/levels/module-pack-test-3/run.log` and
`build/levels/module-pack-reference/report.json`.

## Shadow receiver delivery

`tools/level_asset_profiles.py` selects one existing capture/load-map profile
per process. AREA03 adds its two retained sub-0 snapshots and checks the
pinned overlay identity and every relocated resource entry against the disc
descriptor. Unrecorded subs are refused.

`tools/export_level_shadow_receivers.py --profile PROFILE` reuses the
existing EMSR serializer. Every grid, static object block and shared box
model is compared byte for byte with every snapshot in that profile. Outputs
are under `assets/levels/PROFILE/subN/`; existing live paths are unchanged.
`tools/test_level_shadow_receivers_reference.py` rebuilds the entire output,
checks equality, loads it through the actual native receiver parser and
requires rejection of a truncated file.

All thirteen profiles passed, covering sixteen profile/sub combinations:

| Profile/sub | Receiver objects | Blocks | Asset bytes | Captures |
|---|---:|---:|---:|---:|
| 00/0 | 1100 | 1802 | 3746296 | 9 |
| 00/1 | 1099 | 1783 | 3707352 | 2 |
| 01/0 | 693 | 1277 | 2658072 | 16 |
| 02/0 | 747 | 921 | 1930712 | 5 |
| 02/1 | 645 | 692 | 1458456 | 2 |
| 03/0 | 467 | 680 | 1428184 | 2 |
| 04/0 | 1012 | 1326 | 2768632 | 10 |
| 06/0 | 885 | 1366 | 2846488 | 8 |
| 13/0 | 460 | 1669 | 3453432 | 6 |
| 13x/0 | 460 | 1669 | 3453432 | 13 |
| 15/0 | 979 | 1355 | 2826968 | 1 |
| 15/1 | 210 | 796 | 1657528 | 1 |
| 19/0 | 807 | 1381 | 2874712 | 4 |
| 19x/0 | 807 | 1381 | 2874712 | 3 |
| 19s1/1 | 983 | 1109 | 2323288 | 2 |
| 22/0 | 480 | 866 | 1809528 | 6 |

These are whole-bank object counts. `EM_SHADOW_RECEIVER_MAX = 512` bounds a
single shadow's selected receiver list, not this bank; the native parser
accepts all sixteen full banks. Per-frame selection capacity remains a
separate runtime measurement.

Receipts: `build/levels/shadows-all-1/results.json` and the per-profile
`build/levels/shadow-reference/PROFILE/report.json` files.

## Capacity and remaining work

| Resource | Measured now | Native bound | Remaining proof |
|---|---|---|---|
| Object textures | AREA00 579 keys/sub; AREA01 460; bank/shared probe reaches 607 nonzero keys in AREA13 reload | 512 | Complete area-effect discovery and runtime material classification, choose maximum, O(1) lookup and after-change smokes |
| SFX entries | Each recorded area/sub export has 1000 | 4096 per loaded registry | Later event coverage and combined scope delivery |
| SFX samples | Recorded area/sub exports range from 15 to 78 | 1024 per registry | The full combined catalog |
| Actor pool | Original and native capacity 256 | 256 | Peak occupancy and retained-node coverage per route |
| Receiver list | Full banks above accepted | 512 selected per shadow | Peak selected list per route |
| Loader storage | All 15 complete loads pass | 16 host regions; 32 MiB per read | Reload sequences and remaining boundary workers |

The canonical AREA11/AREA01/AREA00 SFX registry passed the AREA00 delivery
check with 3278 entries and 234 samples. That is not the all-area union.

First-level prerequisites were rebuilt from the disc into this worktree's
real private `assets/`, using the documented exporters. The initial manifest
needed source-backed canopy/panel/elevator entries before the patching
exporters, and AREA01 required the separate boot-script export. Recipes and
logs remain under `build/levels/bootstrap-private-{1,2,3,4}/` and
`build/levels/area01-boot-export-1/`. The native baseline builds with zero
warnings; headless newgame-control passes at 9.599849
(`build/levels/capacity-before-control-2/run.log`). The texture capacity has
not changed. All four baseline runs pass: first-level exit/AREA01 arrival
and AREA01 through `a01_02`, each with default and GPU rendering. Their
strict checkers and present-again checks pass. Tick, RNG and presented-pixel
hash logs are retained under `build/levels/capacity-before-smokes-2/`;
`results.json` records their hashes. The first default check was rerun with
`EM_LEVEL_SMOKE_BIN` pointing at the private binary.

The later-area bank/shared texture probe is diagnostic, not a completed
catalog exporter. Nonzero texels verify against every selected freeze for
all profiles. AREA15 sub 1 required reading a model across adjacent
descriptor ranges rather than stopping at an extracted file boundary.
AREA13 reload required each capture's original player texture mode/costume;
the default mode did not reproduce all thirteen freezes. Receipts are in
`build/levels/catalog-probe-{1,3,4}/`. Area-specific effect discovery and
zero-key classifications remain open; no complete live catalog is claimed.

Remaining stage-3 work: full later-area texture discovery and residency
checks, later reload/event coverage, complete
capacity tables, then the explicitly permitted registry change and both
renderer proofs. Stage 4 recordings and stage 5 AREA00 binding have not begun.

## Reproduce (native arm64 macOS)

```sh
python3 tools/level_asset_audit.py --run build/levels/assets-new-run
python3 tools/export_level_module_pack.py
EM_HEADLESS=1 EM_REFERENCE=fork EM_TEST_FULL=1 make test-level-module-pack-reference
# Repeat this export for each profile in the table above (one call per profile).
python3 tools/export_level_shadow_receivers.py --profile 03
EM_HEADLESS=1 EM_REFERENCE=fork EM_TEST_FULL=1 make test-level-shadow-receivers-reference
PYTHONPATH=tools python3 -m unittest test_level_asset_audit
```

The module-pack and shadow checks are opt-in Make targets. Default quick
mode samples three loader layouts and AREA02's two receiver banks; full mode
keeps the same comparisons and covers every listed case.

## Requests for main

No shared live source or renderer changed. One concrete later-area renderer
requirement is now established: AREA04's static bank contains zero-TEX0
vertices that the original actually draws with texturing enabled. Replaying
the original level/clip programs over the retained display lists produces
two such triangles in each buffer of `a02_05_progression_exit`. A third
checked display list (`a04_s1_reel_blocks`) submits zero-key source blocks
but emits no zero-key triangle. Receipt:
`build/levels/catalog-zero-oracle-1/report.json`.

The current decomp geometry exporter's `walk_records` rejects a zero TEX0,
so its mesh reconstruction misses these records. The raw static bank export
preserves every byte. A complete port path must keep these vertices and
honour the original key's live GS-memory sampling; it must not substitute
disc texels or delete the key. Required change for main: supply runtime GS
texture data for such primitives through the protected GS/render path and
prove their pixels against the original. The registry-capacity exception
does not authorize that renderer change. Tendril and other zero-key users
still need their own draw-mode classifications.

The source blockers and shared proof gaps from stage 2 remain listed in
`LEVEL_READINESS.md`; the asset proofs do not clear them.
