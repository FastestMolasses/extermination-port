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
`build/levels/resources-existing-1/results.json`. Historical (that
receipt only): AREA00 then had 579 texture keys in each sub and the
unchanged 512-slot registry correctly refused the complete catalog; no
catalog was truncated. The current result is 580 keys per sub, uploaded at
capacity 1024 (see "Texture catalogs and measured capacities" below).

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

## Texture catalogs and measured capacities

`export_level_world_textures.py` preserves every reference from the delivered
static/dynamic/model banks, bound library/resource models, shared producers
and the recorded page draws. Its exporter-only page oracle executes the
original VU programs, including `001E9280`'s additional surface packet. The
shared smoke model and native VU kernels are unchanged. All eight previously
supported program packets retain identical parsed upload/constant bytes;
three false source-address mutations are rejected.

All sixteen profile/subarea exports pass. Every fixed texel comes from the
disc upload replay and matches every selected original GS freeze. AREA13
reload has two mode/costume variants; they are separate EMOT files. AREA15
sub 1 has a model crossing adjacent descriptor ranges, whose entire extent
is checked against original loaded RAM. Catalog JSON retains zero keys as
unresolved draw-class/live-GS references. It does not invent fixed texels or
claim these catalogs can all be bound by the current renderer.

The page replay found a missing `001E9E60` surface material in the earlier
AREA00 catalog. Both canonical AREA00 exports now contain 580 fixed keys
and pass the original GS and native resource-loader checks. The generic
catalog additionally retains model 0's zero reference. This supersedes the
old 579-key completeness claim; no AREA00 gameplay phase is promoted.

| Profile/sub | Fixed textures + unresolved keys | Largest EMOT bytes | SFX entries / samples | Actor snapshot max / 256 | Effect snapshot max / 128 |
|---|---:|---:|---:|---:|---:|
| 00/0 | 580 + 1 | 6,973,040 | 1000 / 46 | 134 | 0 |
| 00/1 | 580 + 1 | 6,973,040 | 1000 / 46 | 130 | 0 |
| 01/0 | 460 + 0 | 5,644,080 | 1000 / 46 | 94 | 0 |
| 02/0 | 419 + 0 | 5,769,048 | 1000 / 45 | 116 | 0 |
| 02/1 | 311 + 0 | 3,401,016 | 1000 / 15 | 32 | 0 |
| 03/0 | 386 + 1 | 4,987,968 | 1000 / 78 | 55 | 0 |
| 04/0 | 535 + 1 | 6,325,816 | 1000 / 42 | 130 | 0 |
| 06/0 | 369 + 0 | 5,534,376 | 1000 / 77 | 120 | 0 |
| 13/0 | 594 + 1 | 7,292,864 | 1000 / 49 | 125 | 0 |
| 13x/0 | 609 + 1 | 8,376,616 | 1000 / 49 | 120 | 0 |
| 15/0 | 381 + 0 | 4,293,576 | 1000 / 19 | 40 | 0 |
| 15/1 | 453 + 0 | 7,195,272 | 1000 / 31 | 47 | 0 |
| 19/0 | 510 + 1 | 6,178,784 | 1000 / 75 | 130 | 0 |
| 19x/0 | 553 + 1 | 6,982,632 | 1000 / 75 | 130 | 0 |
| 19s1/1 | 478 + 1 | 5,825,760 | 1000 / 66 | 79 | 0 |
| 22/0 | 339 + 1 | 4,433,880 | 1000 / 33 | 55 | 0 |

The table measures **retained snapshots**, not whole-route high-water marks.
Actor occupancy is independently checked against the original free counter;
effect occupancy uses `001F40C0`'s live short. Zero captured effect entities
does not establish that the route never allocates them. Whole-bank shadow
sizes are above; the 512-receiver bound applies to one selected shadow list,
whose route peak remains unmeasured. Original actor/effect capacities remain
256/128. All fifteen module deliveries pass with the existing 16 host-region
and 32 MiB read limits; this does not prove every reload sequence.

The largest fixed catalog has 609 keys (AREA13 reload), plus one unresolved
zero key. `EM_GFX_OBJECT_TEX_MAX` is raised to 1024. The Metal registry uses
2048 open-addressed index buckets at at most half load, preserving full
CLD-masked keys, insertion order, replacement and reset behavior. Expected
lookup cost is constant. The real Metal fixture fills all 1024 slots,
exercises 220 bucket collisions, checks pixel replacement through both APIs,
refuses a new key when full, resets/refills, and retries a failed allocation.
The fixture also distinguishes two different full-width keys in the same
bucket and passes ASan/UBSan. Its pixels are synthetic; it opens no window.

The canonical AREA11/AREA01/AREA00 SFX registry has 3278 entries and 234
samples. The union of available area registries plus that canonical file
has 14274 distinct scope/id entries and 348 distinct sample payloads. It
exceeds the 4096-entry bound; samples fit the 1024 bound. Two entries conflict
with an older inferred office binding, detailed under Requests for main.
The tool reports that conflict instead of choosing a source silently. Each
per-area 1000-entry registry fits. No SFX capacity or live binding changes.

Receipts: `build/levels/world-textures-all-1/results.json`,
`build/levels/world-texture-reference/PROFILE/full.json`,
`build/levels/world-textures-all-1/page-parser.log`,
`build/levels/area00-textures-complete-1/results.json`,
`build/levels/capacities/report.json`, and `build/levels/texture-registry/`.
The production EMOT parser also accepts all seventeen variant files with
exact keys and texel bytes and rejects truncation for each:
`build/levels/texture-catalog-parser/full.json`.
The AREA03 quick texture check compares both freezes and rejects malformed
layout, truncation and a changed texel; full mode repeats all profiles.

## Registry regression proof

First-level prerequisites were rebuilt from the disc into this worktree's
real private `assets/`. Recipes and logs remain under
`build/levels/bootstrap-private-{1,2,3,4}/` and
`build/levels/area01-boot-export-1/`. The native before/after builds have zero
warnings. Both headless newgame-control runs pass at 9.599849:
`build/levels/capacity-before-control-2/run.log` and
`build/levels/capacity-after-control-1/run.log`.

All four before/after runs pass their strict and present-again checks:
first-level exit/AREA01 arrival and AREA01 through `a01_02`, each with default
and GPU rendering. All twelve before/after comparisons are byte-identical:
ticks, RNG counter/state trace, and presented-pixel hash log for each run.
What the presented-pixel log proves differs by frame kind: software-drawn
frames carry a pixel hash, but Metal-drawn (`gpu`) lines carry none (all
of them under `EM_GPU_RENDERER=1`, and 2,125 frames of the default a01_02
run). GPU-drawn frames are therefore compared by tick, RNG and frame kind
and order only, not by object-texture pixels. The registry's own pixel
behaviour is proven by the real Metal fixture
(`tests/level_texture_registry_test.m`: all 1024 slots, 220 hash collisions,
replacement and reset). A readback hash for `gpu` frames is future work for
a change owned by main.
Receipts and hashes are under `build/levels/capacity-before-smokes-2/` and
`build/levels/capacity-after-smokes-1/`. The "before" binary is
`build/levels/capacity-baseline-build/extermination`, built from the
registry change's parent `b2c1026` (same `src/` tree as `d6f50b1`; recorded
after the fact in `results.json`, with the rebuild check in
`capacity-baseline-build/rebuild-check.json`). The direct comparison of port
main `2f9cf6c` against the levels tip `61cd21c` on `a01_02`, default and GPU,
is `build/levels/registry-main-vs-tip-review-1/results.json`: identical
ticks, presented-frame logs and checker output (including the rand-order
verdicts) in both modes.

SHA-256 of each run's traces. The before run, the after run
(`capacity-after-smokes-1/`) and the stage-5 final regressions
(`area00-final-defaults-2/`, `area00-final-regressions-1/`, receipt
`area00-readview-preflight-1/final-regressions.json`) all produced these
same bytes:

| Run | `ticks.jsonl` | `rand.trace` | `present-pixels.log` |
|---|---|---|---|
| GPU 0 `a01_arrival` SHA-256 | `2866e0def000ebe612b999c15535688f05f93a299663a148f6ad2001f6ce9425` | `65f7d6753717b2322528ff153ebaa3e3c137b5dbb4904d988a8d23c7857e37f6` | `d026afefc66168659e3c3f9c8748cf42bdc5f094d1c23281013dfd6cee5c3c12` |
| GPU 0 `a01_02` SHA-256 | `4f2dfca62fc141df16917562f9c0630da0f37660738f2a354a8cddf8abd085dc` | `806f13fcd18be1d4d48f7512dbcfbec333e3dd6cfe9470d600c15eec76c2ebdd` | `aeffbf0a8efa6241fab37192048649d33075a78d8729561afe445fdd18b61d47` |
| GPU 1 `a01_arrival` SHA-256 | `081bdf74b28e9e777f2970db48533086fcc1993903cfcfa5232273fa33a4400d` | `65f7d6753717b2322528ff153ebaa3e3c137b5dbb4904d988a8d23c7857e37f6` | `5fe82ab85f775a9e5e03461b432fdb2e6e2e637556ffccf49f88fd89eadc831c` |
| GPU 1 `a01_02` SHA-256 | `ff396a631e371f1f85befa00a1c7211ba144ce8605086869c5085bdd1bdcb713` | `806f13fcd18be1d4d48f7512dbcfbec333e3dd6cfe9470d600c15eec76c2ebdd` | `02973efa0398921f02c784a98291cb282ddb301d24b43fe2efeb368eb892f575` |

Only the before run's `ticks.jsonl` copies are retained; the byte-identical
after-run and stage-5 copies were deleted by name once these hashes were
recorded (each receipt's `results.json` keeps them). The receipts live in
the port checkout's ignored `build/levels/`.

The isolated staged-index native build
also passes with zero warnings (`build/levels/texture-capacity-index-build-1.log`);
its temporary checkout was removed by enumerated absolute paths.
The capacity/index exception meets its required regression proof.

Stage 3 now has recorded-area data exports and measured capacities; full
native later-area drawing, unsampled runtime peaks, additional events/reloads
and unrecorded branches remain separate proof. Stage 4 subsequently completed
the retained fork route gaps (LEVEL_ROUTE_EVIDENCE.json). Stage 5 supplies the
AREA00 surface/crate roots and retained pickup models, then reached a
32-view owner limit (raised to the actor pool's 256 on 2026-10-19; the
arrival now stops at `0022DCD0`'s read `00269400`); see LEVEL3_BINDING.md.

## Reproduce (native arm64 macOS)

```sh
python3 tools/level_asset_audit.py --run build/levels/assets-new-run
python3 tools/export_level_module_pack.py
EM_HEADLESS=1 EM_REFERENCE=fork EM_TEST_FULL=1 make test-level-module-pack-reference
# Repeat this export for each profile in the table above (one call per profile).
python3 tools/export_level_shadow_receivers.py --profile 03
python3 tools/export_level_world_textures.py --profile 03
EM_HEADLESS=1 EM_TEST_FULL=1 make test-level-world-textures-reference
python3 tools/level_asset_capacities.py
EM_HEADLESS=1 make test-level-texture-registry
EM_HEADLESS=1 EM_REFERENCE=fork EM_TEST_FULL=1 make test-level-shadow-receivers-reference
PYTHONPATH=tools python3 -m unittest test_level_asset_audit
```

The module-pack and shadow checks are opt-in Make targets. Default quick
mode samples three loader layouts and AREA02's two receiver banks; full mode
keeps the same comparisons and covers every listed case.

## Requests for main

Only the explicitly permitted texture capacity/index changes touch graphics.
The later AREA00 continuation also proves a host owner-view capacity issue:
`em_area11_boxes.c` allowed 32 views, while its original arrival contains 26
crates and 13 drums, requiring at least 39 before other world owners.
Resolved 2026-10-19: the bound is now the actor pool's 256
(LEVEL3_BINDING.md "Owner-view capacity (resolved 2026-10-19)").

One further later-area renderer requirement is established: AREA04's static bank contains zero-TEX0
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

SFX: the all-scope union exceeds the current entry limit. Main must either
select a complete per-area registry with the shared scopes on each load or
raise the combined bound with live delivery proof. Do not truncate it. The
two conflicting entries are AREA02 sub 1 IDs 1021 and 1022: the canonical
`export_sfx_registry.area_bindings()` still uses an office coverage-match
bank without a capture, while the AREA02 exporter proves the bound banks
against its recorded driver headers. The sample payloads agree, but note /
center differ for both and scalar differs for 1022. Required source change:
replace that inferred `(2, 1)` binding with the verified AREA02 sub-1 binding;
preserve other scopes and verify the shared live path. Receipts:
`build/levels/sfx-union-conflicts-1/{summary,classification,fields}.json`.
This is an existing-entry correction, outside the additive shared-file rule.

The source blockers and shared proof gaps from stage 2 remain listed in
`LEVEL_READINESS.md`; the asset proofs do not clear them.
