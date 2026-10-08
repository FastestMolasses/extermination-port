# AREA01 first-visit crash audit

Worktree `extermination-port-crash`, branch `level2-crash`, starting at
`8ce3067`. Original instructions and local recordings are the behavioral
authority. This ledger distinguishes worker proof, recorded route comparison,
and exploratory input. It does not certify unrestricted play.

The release still has a rendering stop at `0023D930`: the fire owner's third
layer requests kind 6 near the crate stack (recorded `a01_00` row 405).
Completing that path requires the protected `em_chain_page.c` and
`em_vu1_page_programs.h`, plus its locally exported packet. This branch does
not bypass the draw or remove its fault. Runs stopped there cannot prove
the later portions of their routes.

## Current scope

Native input runs now cover status open/close, both knife attacks, aiming and
two shots, fire damage, control/east room transitions, the first NPC talk,
terminal decline and return, and duct entry/crawl/pickup/exit. Their different
proof limits are recorded below. Water workers and impact effects have
original-reference proof, but both continuous water approaches hit the
protected flame renderer first. This branch is **not unrestricted crash-free
AREA01**. The final static inventory and exploration table below give every
remaining known boundary. Earlier verification sections retain intermediate
failures to explain the subsequent repairs; they are not current-status claims.

## Verification receipts

Local output belongs under ignored `build/level2-crashes/` and the individual
reference suites' usual ignored directories. No captured bytes or exported
assets belong in this commit.

Baseline `make -B all` passed with zero warnings. Baseline headless
`EM_STARTUP_TEST=newgame-control` passed: 1,301 locked ticks, zero locked
motion, 30 movement ticks, displacement **9.599849**. The sandbox cannot
create a Metal device; native test processes therefore require host Metal
access while retaining `EM_HEADLESS=1` (muted output, controller ignored).

## Shared-file edits

`em_scene_bindings.c` adds read-only floor observations to the tick log:
surface, depth, puddle, marsh and surface-height bits. The Makefile adds two
opt-in exploration targets. `em_level_smoke_test.c` includes the new driver
and calls its phase-entry/input hooks only when explicitly selected.

## Census promotions

No promotion is justified by a standalone oracle alone. Promote only after a
native reached worker and its compared AREA01 recording window are identified.

## Exploration fixture

`make test-area01-exploration AREA01_EXPLORE_ARGS='--case water'` starts an
ordinary New Game and uses the existing level-smoke approach to AREA01.
`--list` describes each custom input probe and each retained route replay;
`--all` opts into the entire set. `--bin` can select a frozen build.
The new C hook is active only when both exploration environment variables
select the current phase. Movement targets change pad input, never player,
collision, scene, or camera state.

Each run records the binary hash, phase prerequisites, first fault, final
player position/state, health, water fields, room entry and observed actions.
Completed tick logs are compressed. Input completion and observed coverage
are distinct: a script that misses its intended room is
`TARGET-NOT-OBSERVED`, and a navigation target blocked by geometry is
`ROUTE-BLOCKED`. Neither is a gameplay pass. Original parity remains a
separate check by the existing smoke comparator.

The initial staged fixture was built with `make -B all` (zero warnings),
passed New Game control at **9.599849**, and passed `make test-level-smoke`.
Its harness passes ASan/UBSan, malformed-script and bounded-navigation
refusals; the existing smoke harness/checker passes 16 paths, 20,101 pad
frames, 19,565 capture-shaped rows and 150 rejected corruptions. Receipts:
`build/level2-crashes/index-fixture-{build,startup,smoke,references}.log`.

The staged baseline fixture observed the status screen open and close.
Its east water approach reproduced collision close-out `001AAD00` failing
at `001A8734` before reaching water. This is a separate defect: the shared
collision world still routes AREA01 flame callback `001E3D20` through its
AREA11-only handler. The worker already exists in `em_area01_side.c`.
The fixture correctly returns a nonzero result and reports water as
unobserved. Receipt: `build/level2-crashes/index-fixture-exploration/summary.json`.

## Water and effect binding

The known water failure has two layers. The floor service calls
`00187DE0` on first contact, before footsteps can reach `001E8B90`.
The new floor bridge publishes its just-computed depth to the actual player
record, calls the existing `em_area01_math_00187DE0`, then refreshes the
typed floor view. Water workers borrow the in-stage record read-only; they
do not project post-stage position or animation state over it.

The ripple adapter calls the sole `em_area01_render_001E8B90`, using the
loader-owned grid initialized by `001E7780` and the original conversion
owner `001281C0`. There is no new persistent grid, injected capture state,
or new boundary clamp. Contact uses the original splash effect `80000016`
and sound `CA`/`DB`; wading's `8000001D` handler was already live.

`001EC270` now uses the same work-block/packet bridge as the splash
handlers. Its two descriptors were already exported. The old counted-gap
path is removed: every missing effect handler faults. Static follow-up
found water bullet effect `80000026 → 001EB7F0` and ordinary impact effect
`80000019 → 001ECB00`. The first is a short translation of the original
two random rounds; the second calls the existing `em_area00_hud` owner,
including sprite colour, three packet submissions and work-step easing.

Run `python3 tools/export_area01_water_effects.py` to add the two descriptor
windows needed by those impact handlers to the existing local EMET export.
The separate exporter preserves the protected base tool, verifies the
pinned boot ELF and compares immutable descriptor bytes against all 16
AREA01 captures; later target-hit descriptors have original mutable fields. It replaces this worktree's asset symlink before writing.
No exported bytes are committed.

`test_area01_water_reference.py` compares whole RAM/scratch and ordered
contact boundaries against original instructions. Quick mode checks 8
contact chains, 48 ripple calls and 6 complete native effect packet chains;
full mode checks 30, 180 and 24 respectively. It exercises both depth sounds
and refuses a missing grid. The existing floor, footstep, player-view,
runtime, flame-service, effect API and render references also pass. These
are worker/composition proofs, not a claim that a continuous route has
reached the water. Receipts are `water-reference*.log`, `water-export.log`,
`references-water-existing.log` and `render-reference.log` under
`build/level2-crashes/`.

Shared water edits: `em_scene_bindings.c` registers/clears one water
callback and constructs its original argument lanes. The Makefile adds
`test-area01-water-reference`; gameplay bindings reuse already-built owners.

The isolated staged water tree passed `make -B all` with zero warnings,
New Game control (30 ticks, displacement `9.599849`), default smoke and
all listed reference suites, including 956 render cases / 4,744 entries.
Its exploration still stops at the independent flame close-out
`001A8734` before water; this is recorded as a failure, not a water pass.
Receipts: `build/level2-crashes/index-water-{build,startup,smoke,references,render}.log`
and `index-water-exploration/summary.json`. Independent adapter review
found no concrete defect; continuous water play remains unverified.

## Canonical player and target fields

AREA01 exposed three shared-player omissions. Use predicates read retained
feet at player `+A0`, but the live stage had only refreshed `+B0`. The stage
now follows `0015BCF0`: retain the placement (including preceding carry) at
`+A0`, initialize `+B0`, and publish final feet after footsteps. Original
instruction comparison covers 224 cases over all 16 AREA01 captures,
including carried placement, locked states and callback order.

The rebuilt player also lacked the status-1 publication in `0015C420`.
Without it, the original flame callback requested a reaction but its parent
skipped damage. The exact spawn store is restored; 384 original instruction
cases cover six spawn kinds and four initial status bytes in every capture.

The Use chain's `001AA4E0` can now inspect class-2 targets through canonical
pool fields and the existing `001AA410` / `001AA2A0` translations, using the
original square-root owner. Its 8,654 cases pass, and the running-jump
reference retains 7,706 cases / 26,423 calls / 1,090 fault cuts. Aim target
`00185A10` can read the same canonical `+34` low halfword, while `+36`
keeps its independent owner: 256 original cases / 7,936 ordered events
and five crossing-span refusals pass. These changes add no actor shadows.

Shared Makefile edit: build `em_player_target_live.c` and the existing
`em_level14_port_boot.c`; add `test-player-target-live-reference`.

The player/target changes also require an AREA01 hull provider. The old
provider belongs to AREA11 Roger and refuses AREA01 records, both in aim/
melee and the newly reached Use-distance scan. The new per-world provider
borrows each canonical actor's delivered `+58` chain and current model-slot
bone matrices. It preserves the existing hull walkers and filters, clears
on unload, survives shared-owner rebinding, and never falls back after a
refusal. The original oracle passes 116 queries / 57 hits over 16 captures,
with four refusal cases and eight provider-lifetime contracts. Existing
hull and close-out regressions pass 1,238 cases and 40 runs respectively.
The shared scene file adds only its AREA01 registration, and the Makefile
builds `em_area01_hull_live.c`.

The complete isolated player/target/hull stage passes a zero-warning
`make -B all`, startup displacement `9.599849`, default smoke and the
AREA01 status probe (open, close, resumed input; no fault). The initially
separate player stage exposed the hull dependency at `001764E0`; the
complete stage includes its provider. Touched references pass, including
the repaired ASan/UBSan stage-host harness, which explicitly proves an
absent water owner faults. Receipts: `index-player-{build,startup,smoke,host,hull}.log`,
`index-player-references{,-tail}.log` and
`index-player-hull-exploration/summary.json` under `build/level2-crashes/`.

## Fire contact composition

The AREA01 close-out now calls its existing `001E3D20` contact worker and
`0021BB00` player gate, preserving canonical collision/player views across
nested effects. With the original spawn-status publication, the recorded
`a01_s3` path applies the expected five HP damage and reaction action `3E`.
The untouched full checker passes all 21 live phases through this route,
262 captured rows and the whole camera endpoint. The existing AREA01 RNG
check proves caller/LCG continuity; it does not possess an original per-call
trace, and reports its differing endpoint seeds without installing one.
Receipt: `after-fire-status/strict-check-correct-binary.log`.

The new original-reference suite passes 93 contact cases, 247 ordered
boundaries, 32 effect spawns and three refusals over 16 captures. Existing
effect-chain and AREA01 effect-service suites pass. Temporary diagnostic
traces are removed. The shared scene change is one AREA01 contact callback
registration; the Makefile adds its reference target. The opt-in fixture
now reports the unchanged row checker's exact prefix and observed targets,
including the east room's recorded entry 8.

The isolated staged fire tree passes `make -B all` with zero warnings,
startup displacement `9.599849`, default smoke, its reference suites and
the exploration harness. Fresh `a01_s3` exploration reaches the damage
reaction without a fault and matches all 262 captured rows. Receipts:
`index-fire-{build,startup,smoke,references,harness}.log` and
`index-fire-exploration/summary.json` under `build/level2-crashes/`.

## Aim target model ownership

After hull and health-field binding, aiming reaches `00183C40`, which reads
the target's bone-slot words at `+110` and their current matrices. Those
words already belong to the AREA01 actor/model views. A read-only provider
now borrows that storage for the current target, with the existing shared
slot arena, and exposes the exact regions to the existing aim world owner.
No target record is synthesized; absent, unheld or incompatible regions
still fault. The shared scene file adds only the forwarding accessor.

The original oracle compares 832 cases over 16 captures and eight model
identities, with 2,784 ordered stores/calls and 22 provider refusals. It
executes the actual target-region composition and compares whole RAM.
All 20 original model branches, active/inactive views and private/shared
projections are covered. Existing actor-view references pass; the aim-world
harness passes 100 ASan/UBSan checks, including missing-provider refusal.

The custom input driver now keeps adjacent hold steps continuous. Its
previous extra neutral frame released R1 immediately before Circle, so
completed aim input did not actually fire. A sanitizer-backed harness
checks the exact nine-frame R1 / R1+Circle / R1 sequence. This corrects
test input only; the game's original trigger mapping is unchanged.

The isolated staged aim tree passes a zero-warning build, startup at
`9.599849`, default smoke and all touched references. Its corrected native
probe completes aiming and two trigger presses with both aim and firing
observed, without a fault. This proves the actual probe, not every impact
surface or enemy-damage branch. Receipts:
`index-aim-{build,startup,smoke,references,harness}.log` and
`index-aim-exploration/summary.json` under `build/level2-crashes/`.

## Room interactions and resource boundaries

Control-room and east-door paths now compose existing placement, fade,
sound, room-script and flag-80-list owners (`00182F90`, `001B0C00`,
`001BBD60`, `001BB400 / 001BB7C0 / 001BB7F0`, `001B1DE0`). The terminal's
alignment uses existing `001B6F00`. Original transition proof passes 304
calls / 400 ordered boundaries across 16 captures and four refusals.
The list publication retains the existing allocator/order/refusal rules.

AREA01 music and message voices are added to the local stream-sector
export. Its 23,725 sectors compare byte-for-byte with the user disc; all
12,008 prior sectors remain identical, and the source tables match all
16 captures. Stream forwarding reuses the current lane owners and drive
policy. Re-export with `python3 tools/export_streams.py` in an isolated
asset directory. This worktree's `assets/streams` is a real directory.

The successful spawn now publishes all four original rotation words,
not only the yaw. Actual native publication compares with `001B07C0` in
320 cases over 16 captures. The later camera seed still loses its fourth
lane: original `0018CBD0` writes all four words, but protected
`em_camera.c`'s seed API and `em_camera_live.c`'s publication copy only XYZ.
Four original comparisons prove the stale `camera+3C` word. Repair requires
those protected owners; no compensating camera write is added here.

With PS2 drive timing selected, the control-room conversation matches all
1,435 recorded rows; its whole-camera tail still exposes that protected
fourth-lane defect. Default host-speed audio finishes the line six frames
earlier. Both native runs complete without a fault. Control-room pickups
also complete with captured positions, item fields and progress; request/
message timing differs while uncaptured module reads answer at host speed.
These are scoped observations, not full-checker parity passes.

The terminal's two pose quadwords `00810710..0081072F` now use the existing
canonical progress owner and its original reset. A 192-case original
`00159B90` prefix proof includes 288 actual SDK copies and 16 missing-window
refusals; ASan/UBSan checks aliasing, bounds, lifetime and reset. This does
not enable save serialization or load-game state.

The original `00157F60` request callback now shares one scalar owner with
the existing AREA11 panel wrapper and posts its fields through canonical
views. Full reference coverage is 1,548 cases plus 33 access-failure cuts;
existing panel tests pass. The native east-room route reaches entry 8 and
677 exact captured rows, posting request `6/80` and the terminal owner.
The next status boundary `0020E060` still accepts only the AREA11 panel;
request-6 status composition remains separate work. No decline, return or
accepted save is claimed from this intermediate receipt.

A closed-loop input probe reaches the actual duct trigger before calling
Use. It exposed `00188610`; the row selector now reuses the existing ROOM
owner and an original four-byte table added to the local boot-script
export. The leaf passes 273 existing original cases plus 160 real-runtime
cases over all 16 captures. Regenerate with
`python3 tools/export_area01_boot_scripts.py`; this worktree's
`assets/area01_boot_scripts` is a real directory. The subsequent crawl
path still requires independent proof.

Shared edits for this group: the Makefile builds
`em_area01_transition_services.c` and adds its reference target;
`em_scene_bindings.c` publishes spawn rotation, adds/clears/registers the
row-selector callback, and corrects its comment about terminal pose storage.

The exact staged room tree passes its zero-warning rebuild, startup at
`9.599849`, default smoke, all touched reference suites and the exploration
harness. Its control-room replay completes with all 1,435 recorded row
fields exact under PS2 drive timing; whole-camera parity retains the
protected limitation above. Receipts: `index-rooms-{build,startup,smoke,
references,references-tail}.log` and `index-rooms-exploration/summary.json`
under `build/level2-crashes/`.

## Terminal confirmation and decline

The next status boundary is now bound. Request 6 borrows the actual live
type-38 terminal fields, runs existing `00225A00` through the original
memset boundary on scene-owned `00810040..00810113`, and enters the existing
BATTERY confirmation via the original `0020CDC0` cold branch. Its default
No selection and return use the existing page owner. Accepting still reaches
unbound phase 6 / `00225AC0`; no card I/O or load-game state is provided.

The canonical reset compares 48 original cases over 16 captures. ASan/UBSan
checks exact aliases, bounds, lifetime, 256 actor types, signed costs and
free/reuse. The status-page full reference passes 3,576 cases plus nine
refusal branches; BATTERY covers 438 directed and 147 composed frames,
including terminal decline. Existing status-runtime checks pass. The native
PS2-timed replay matches all 1,225 captured rows, opens the prompt and
returns through entry 9 without a fault. Whole-checker results are recorded
separately from this row comparison.

Shared edit: `em_scene_bindings.c` adds only the terminal owner-read/reset
forwarders and their header include; there is no Makefile change. The
exploration fixture also retains a short, input-only vent entry/crawl probe
and recognizes the primary “no translation” fault before its later generic
coordinator message.

The untouched full smoke checker passes 21 live phases through the terminal
route, including all 1,225 rows and the whole ending camera. Native RNG
continuity passes; unavailable original per-call AREA01 RNG and differing
endpoint seeds retain the existing diagnostic-only status. Receipt:
`after-terminal-decline-ps2/a01_s4/check.log`. The exact staged tree also
passes a zero-warning rebuild, startup displacement `9.599849`, default
smoke, all touched references, and a fresh complete 1,225-row terminal
replay. Receipts: `index-terminal-{build,startup,smoke,references}.log` and
`index-terminal-exploration/summary.json` under `build/level2-crashes/`.

## Static first-visit inventory

“Observed” means a native input run reached the path; “recorded” names an original capture; “conditional” means a concrete source branch has not been demonstrated by continuous first-visit play. A bound worker with an original-instruction test is not automatically a completed route. Unknown workers and invalid canonical views still fault.

| Trigger / evidence | Original path | Current status and precise remaining scope |
|---|---|---|
| Close flame draw; recorded `a01_00` row 405 | `001E3D90 → 001CFBE0(kind 6) → D_0023D930` | **Protected blocker.** Implement the original third-layer program and control flow in `em_chain_page.c` / `em_vu1_page_programs.h`, deliver its original packet/table window, and compare original VU execution and emitted primitives. Keep the current refusal; skipping this layer is not a fix. |
| Water first contact, ripple and ordinary skid; recorded `a01_02` | `00187DE0`, `001E8B90`, `001EC270` | Bound and original-reference tested. Continuous water entry remains unproved where an earlier draw blocks its approach. |
| Ground fire contact; observed and recorded `a01_s3` | `001A8660 → 001E3D20 → 0021BB00 / 001EFE00(27)` | **Fixed and recorded-route verified.** AREA01 uses the existing contact owner. Restored the missing successful-player-spawn status store at `0015C6A4`; without it, status 0 suppressed the parent's damage arm. Final run matches all 262 rows, HP 95, reaction action `3E`, and the whole ending camera. |
| Aim or knife attacks in the arrival room; observed | `0018D7B0` / `0019A570 → 001A6440` | **Hull source fixed.** Per-world AREA01 provider resolves canonical `+58` chains and shared slot matrices; it replaces Roger-only ownership without changing lock predicates. Both knife inputs complete. Aim advanced to the separate target `+34` read below. |
| Use with class-2 candidates; recorded control-room approach | `00160220 → 001AA4E0 → 001AA410 / 001AA2A0` | Target fields and existing radius/sight owners are bound. Player stage now retains feet at `+A0` for the original scan; original-reference tests pass. Control-room route reached its talk script. |
| Aim target acquisition and firing; observed | `00185A10 / 00185E30 → 00183C40` | **Fixed bindings.** Canonical target +34, held model-slot words and matrices are available to direct and region-only owners. The corrected native probe observes aim and two shots (magazine 30→28), with no fault. It does not establish a bug hit or all shot surfaces. |
| Enter control-room door or east slider; recorded placements 15/17 | `00182F90`, `001B0C00`, `001BBD60`, `001BB400 / 001BB7C0 / 001BB7F0`, `001B1DE0` | **Fixed and observed.** Native transitions enter control room and east entry 8; terminal return reaches entry 9. The control-room route matches all 1,435 row fields under PS2 timing, with the protected camera-W endpoint defect described above. |
| East-room terminal confirmation and No; recorded `a01_s4` | `00157CE0 → 001B6F00 / 00159B90 / 00157F60 → 0020E060 / 0020CDC0 / 00225A00` | **Fixed and full-checker verified.** Canonical pose/request/terminal/reset storage, existing BATTERY No path and return match all 1,225 rows plus the ending camera. Accepted-save phase 6 / `00225AC0` and load-game state remain unsupported and fault; no memory-card write is performed. |
| Control-room first talk; recorded `a01_s0` | Script sound selectors → `001FA790 / 001FAE70 / 001FABB0 / 001FBC50` | **Bound and observed.** Local export supplies music/voice sectors. All 1,435 row fields match with optional PS2 timing. Host-speed audio ends six frames earlier, as expected from the selected drive policy; the later camera-W mismatch remains separate. |
| Enter/crawl/leave duct and its pickup; input-only probe | `0016D130 / 0016DE40 / 0016EBA0`, row leaf `00188610`, nearby `001C02E0` | **Observed complete after two bindings.** Reuse ROOM row selection and borrow frame counter `70003B64`; 3,381 phase ticks complete, the original taken-block transition matches and exit state 26 (decimal) is observed. Recorded `a01_s5` replay still misses the trigger because of early movement divergence; the authored approach is explicitly separate. Conditional bit-31 collision metadata (`0019AD00 / 0019AFE0`), `001782A0`, direct camera `001B0460` and crawl allocation `001AFA90` remain unproved branches. |
| Shoot ordinary surfaces/water; conditional | Marker global `19 → 001ECB00`, surface `5B` global `26 → 001EB7F0` | Both handlers now bind and pass original packet comparisons. This does not certify every shot/surface combination or the full aim pipeline. |
| Break nest crate placement 7; conditional first-visit action | Crate state 2 → group `00829360`, `001AFA90`, child `0012A5D0` | Still blocked at the existing nest-spawn refusal. Allocate/copy/rebind the four original child records and prove their first ticks; child state 9 reaches `0012D850` and further enemy callees. Allocation alone is insufficient. |
| Other model-6 crates; conditional destruction | Effects `0A`, `15`, husk model `22` | Existing debris/powder/husk owners bind; these five link-negative crates avoid nest spawning. No claim that every crate has been destroyed in a continuous run. |
| Other damage/infection/death reactions; conditional | Shared player `001EFE00`; effects `40 / 48 / 44 / 51` | Player attach remains a fail-stop on these unproved branches. Existing misc/effects services can supply canonical attachment. `48 → 001F8350` lacks an identified owner; `44 → 0021AE90` has an area06 owner but is unbound. Conditional `0015C1F0`, `001FAFD0`, `0021C200` need live composition. Flame's recorded hit does not exercise every reaction type. |
| Ladder top, ledge, hang/sidestep and clip-dependent movement; conditional | `001782A0 / 00178390 / 00178080`, `00188570 / 00188590 / 001885B0`, `0017F1C0 / 0017E6E0 / 00178440 / 001784E0 / 0017F130` | Shared closure stubs remain. Known owners include level14 `001782A0`, area06 `001885B0`; AREA01 room `00188610` is now bound for the reached duct path. Other addresses require original translation/owner search and actual caller proof. |
| Object activation/bypass branches; conditional | `001C02E0 → 001BF6B0 / 001B6660`; `001BFFD0 → 001BFF90` | Existing level8/roster owners need runtime/callback composition if the branches are reached. The duct reached a separate missing `70003B64` read in C02E0, now fixed. These other branches still need their own callback/lifetime proof. |
| Extreme collision query; conditional and outside recorded bound | `0019D770` no-span branch | Preserve refusal: the original uses caller-register state that the port does not model. Existing bound requires z separation at least `1087.16015625` with N=854; recorded 79,192 camera queries max `53.51905`. This bounds recordings, not arbitrary future queries. |

The nest group contains four class-2/model-0 bugs, IDs `70..73`, param 4, callback `0012A5D0`, with parent-relative positions `(-1,1,-1)`, `(-1,1,0)`, `(-1,1,1)`, `(1,1,1)` and Y rotations `0`, `pi/2`, `pi`, `-pi/2`. They are not pickups. Existing `0012A5D0` dispatch selects substate 9; required follow-up includes `0012D850`, its enemy worker dependencies, original pool/list order, taken masks and allocation failure. The existing crate-registry test covers INIT/taken lookup and explicitly does not spawn children. Conditional actor probe `001C2770` state 6 calls `001C2690`, for which no port owner was found.

Guards narrow this inventory. First-visit event 6 is zero: the security gun stays in dormant state `64`; active gun effects/sounds and revisit controllers `00823CD0 / 00825950` are not established first-visit triggers. The cable can still be damaged, and upper-catwalk bugs can potentially be shot. The first-visit bridge flags remain clear; the north-gap and upper-catwalk doors are not reached by the kept ground routes. Water actor link-1/2 helper gaps `0015A200 / 0015A750` are excluded by the placed link-0 roster. Camera NULL hooks for other area/subarea guards are excluded from AREA01; no protected camera change is justified by those guards. The direct shared `001B0460` stub is a separate conditional issue.

## Effect inventory

IDs and original addresses in these inventories are hexadecimal. These are **43 distinct global effect records** (the earlier 38 plus five transitive target-hit selections) from the audited AREA01/shared player, shot, crate and secondary-effect callers. High-bit script message IDs are excluded. All records have radius 300 and volume 4096. `B` means an existing live owner; `F` means an unbound handler/callback; `C` means an unproved conditional trigger; `X` means the direct source branch is excluded by the first-visit guard. A live owner is not proof of every listed trigger. The subtype handler applies only to callback `001EA240`.

|ID|callback|subtype / handler|light|sound|binding / direct trigger|
|---|---|---|---|---|---|
|03|001EA240|0 / 001EAB50|1|-1|B: shot/marker or active gun|
|05|001EA240|2 / 001EAF00|0|-1|B: floor surface6 splash|
|06|001EA240|3 / 001ECFB0|0|-1|F/X: active gun; handler area00_hud exists|
|07|001EA240|4 / 001ED7A0|0|-1|B/C: placed class-2/model-0 bug hit through shared 001B41F0, independently of dormant gun; existing HUD owner now bound and packet-tested|
|0A|001F2BA0|0 / —|0|-1|B: model6 crate debris|
|0D|001F18C0|0 / —|0|-1|B: knife trail|
|0E|001EA240|7 / 001EBBB0|0|-1|B: 001F0460 ring decal preset0 secondary|
|10|001E2560|0 / —|0|-1|B: 001F0120 head sprite secondary|
|11|001EA240|A / 001EC1F0|0|-1|B: ordinary steps/climb|
|12|001EA240|B / 001EC270|0|-1|B: ordinary skid/landing; original packet proof|
|15|001EA240|D / 001EBD20|0|-1|B: model6 crate powder|
|16|001EA240|E / 001EB020|0|-1|B: floor contact/climb splash|
|19|001EA240|F / 001ECB00|1|-1|B/C: impact marker; existing HUD owner now bound|
|1B|001EA240|11 / 001ECEF0|0|-1|F/C: player surface response1B; area02_misc owner exists|
|1D|001EA240|9 / 001EAF80|0|-1|B: surface5B step/wade|
|23|001EA240|15 / 001ED450|0|-1|F/C: player reaction23; no owner found|
|24|001EA240|16 / 001EDAF0|0|-1|F/C: shared target-hit models1/6/7; existing area02_misc owner, not the placed class2/model0 bugs|
|25|001EA240|17 / 001EDE40|0|-1|F/C: shared target-hit models2/9; existing level14 owner, not a proven initial-roster hit|
|26|001EA240|18 / 001EB7F0|0|-1|B/C: water5B bullet impact; translated and packet-tested|
|27|0022BBC0|9 / —|2|14A|B: fire contact; bone burst|
|28|001EA240|5 / 001EC3F0|0|-1|B: surface5/climb effect|
|2C|001EA240|1B / 001EBA20|0|-1|B: surface5A bullet impact|
|33|001EA240|1 / 001EAD70|0|-1|B: water/skid landing|
|34|001EA240|1C / 001EE190|0|-1|F/C: shared target-hit model3; no owner found, no initial-roster hit proven|
|35|001EA240|1D / 001EE4E0|0|-1|F/C: shared target-hit models10/11; no owner found, no initial-roster hit proven|
|36|001F2BA0|A / —|0|-1|B/C: secondary debris from unbound EF510 handler|
|39|001E3630|2 / —|4|-1|B: equipment sprite|
|3B|0021A500|0 / —|1|-1|B: cable45 secondary strip|
|40|0022BBC0|2 / —|0|-1|B/C: reaction attach40 blocked earlier at player001EFE00|
|43|001F77B0|2 / —|0|-1|B: death decal|
|44|0021AE90|0 / —|1|14B|F/C: surface mode0A attach44; area06_port callback exists|
|45|0021AAC0|0 / —|1|-1|B/C: damage cable; secondary3B|
|48|001F8350|0 / —|0|-1|F/C: reaction attach48; callback no owner found|
|51|0022BBC0|3 / —|0|-1|B/C: infected/death attach51 blocked earlier001EFE00|
|60|001EA240|23 / 001EACF0|1|-1|B: impact marker|
|61|001E7310|0 / —|0|-1|F/C: player stage special61; area00_low callback exists|
|62|001E7440|0 / —|0|-1|F/C: player stage special62; no owner found|
|63|001E7570|0 / —|0|-1|F/C: player stage special63; no owner found|
|65|001EA240|24 / 001EC470|0|-1|B: surface5A steps/skid|
|66|001EA240|25 / 001EC5F0|0|-1|F/C: surface8/5C steps/skid; area02_misc handler exists|
|67|001EA240|26 / 001EC820|0|-1|F/C: surface5C steps/shots; no EC820 owner found|
|68|001EA240|27 / 001EB980|0|-1|F/C: surface7 steps; area00_hud handler exists|
|76|001EA240|29 / 001EF1C0|0|-1|F/C: shared target-hit flags nonzero; no owner found; direct placed bug hull flags are zero|



The local effect table pointer is zero; these callers select global records. Effects `61/62/63` require their special player modes, and `44` requires surface mode `0A`; water surface `5B` alone does not trigger them. The static-grid inventory compares all 854 polygon attributes and plane coefficients against all 16 captures: attributes `07/5C` are absent, attribute `08` is one upper-catwalk floor at y=60, x=121..140, z=-456.95..-440, and attribute `5B` is one water polygon at y=-27.923, x=-30..32, z=-1021.5..-980. Actor/cell attributes are separate; this bounds the static ground-grid claims without establishing every possible contact. Receipt: `build/level2-crashes/surface-inventory.json`. Other crate-model effects `0B/14/31/32` belong to models `1C/50/1E/1F`, not the six placed model-6 crates. Effect `36` is secondary to an unbound `001EF510` branch. Newly enabled enemy branches require a further transitive effect audit.


## Sound inventory

The original caller audit covers 867 AREA01-tagged functions and 85 sites: 75 constant sites, **57 distinct constant IDs**, eight computed sites and two forwarding sites. Every constant resolves in the registry; resolution does not imply audible support. Registry state 1 is audible, 2 original absence, 3 unsupported.

|IDs|scope|state|
|---|---|---|
|1, 6, A, B, C, D, E, F, 1F, CA, DB, FE, FF, 119, 122, 12B, 12C, 137, 138, 139, 13A, 13B, 13C, 13D, 13F, 140, 141, 142, 143, 146, 147, 151, 152, 153, 159, 187, 194, 19C, 19D, 19E, 19F, 1A0, 1A1, 1AC|(-1, -1)|1 |
|3E8, 411, 412, 413, 423, 425, 426, 427, 444, 8A9|(1, 0)|1 |
|424, 428|(1, 0)|3 modulation|
|42F|(1, 0)|2 |

Additional/computed paths retain limits:

- The shared `001B41F0` adds global `15A/15B` (unarmored ricochet), `15D` (armored hit) and `1B1` (common-tail flag `1000`), all registry state 1. The 57-ID number above remains the AREA01-tagged caller subtotal, not the transitive union. The placed model-0 bug direct-hull branch selects `15A/15B`; other sound branches remain conditional.
- Fire effect `27` uses global `14A`; conditional `44` uses `14B`, both audible. Impact ricochets `188..18B` are audible.
- Footsteps `00182430` derive IDs from floor/tier tables. Recorded coverage exists; all future floor/tier combinations were not enumerated.
- `001B6D70` selects stream/scene sound workers by its script operand. Door `001B8020`, effect `001EF940`, room `001FC280`, queue `001FC6E0`, and forwarding `001FBD50 / 001FC3C0` add data-dependent IDs. AREA01 ambient `44E` is audible.
- Effect `26/2C/67` randomization writes **global effect record 0 +24**, not the selected record, to `18E..191 / 18C..18D / 192..193`. These IDs lack a scoped registry entry, but the selected effect records still have sound -1; they are not immediate missing sounds for each water shot.
- Flame variants use `411/412/413`; cable uses `426/427`; terminal/mechanic use `3E8/444`, all audible. Bridge `8A9` is audible but its activation is not established on this visit. Generator `42F` is an original absence. Dormant gun activation is needed before unsupported modulation `424/428` matters.
- There are 36 unsupported AREA01 IDs: modulation `40F,414,424,428,4A2,4A6,4AA,4AE,4B2,4B6,4CE,4D2,4D6,4DA,4DE,559,55E,59A,59E,5A2,5A6,5AA,5D6`, and unbound bank `9B7..9C3`. Only `424/428` are direct audited constants; others require computed-script reachability.

The existing sound acceptor returns zero for unsupported/unscoped/unmapped IDs, with one-time diagnostics. That remains a fidelity gap, not a gameplay fail-stop. Muted headless execution is not audible-content proof. The stream-sector export is separate from this SFX registry and does not repair unsupported modulation.


## Duct counter, traversal and pickup evidence

The extended probe stopped at owner `001C02E0`, node `007A93F0`, while
reading scratch `70003B64` near position `(166.5,1.5,-480.0923)`. That word
already belongs to the frame loop. The live AREA01 view now borrows its
const storage for contained reads only; writes and crossing spans refuse.
The rerunnable counter test checks 265 values, 2,650 alias reads and 4,240
refusals, then compares 146 original C02E0 cases / 696 ordered boundaries.

The next input-only run traverses to z=-416.5 and exits at
`(133.21881,0.01,-529.5)`, observing states 24/25/26 and exit action 46 / clip
339 over 3,381 phase ticks, with no fault (state/action/clip numbers in this paragraph are decimal). Its entire 64-byte taken block
matches the original duct pickup change: only byte 32 changes from `0x00` to
`0x10`. This proves the pickup mutation and traversal, not whole-route timing
or camera parity. Receipts: `vent-full-after-counter/receipt.json` and
`vent-full-after-counter/pickup-inspection.json` under `build/level2-crashes/`.

The permanent `--case vent` reproduces the same input sequence: an authored
navigation approach followed by the locally loaded a01_s5 pad tail from
frame 376. It embeds no captured gameplay bytes and writes only pad input.
The unchanged recorded replay still diverges at its third row and misses
the trigger; that separate unresolved motor/input-alignment result remains
in `interaction-vent-ps2/diagnosis.json`.

## Census promotion scope

Baseline census: 48 live / 10,913 instructions, 129 verified-unbound /
18,503, zero missing, and two boundary / 131. Verified-unbound describes
available evidence; it does not mean 129 absent translations. The protected
census file is unchanged. Proposed folds are bounded by these receipts:

| Rows / owners | Promotion evidence and limit |
|---|---|
| `001E3D20` and its reached contact helpers | Original 93-case contact proof plus recorded `a01_s3` full checker; HP 95 and reaction 3E. Successful-spawn status is a subpath of `0015C420`, not every initializer. |
| `001AA410 / 001AA2A0`, hull locks, target `00183C40` | Original target/hull/model proofs plus native Use, melee and aiming. Do not promote all enemy/damage workers from this. |
| Door/room/stream workers listed above | Actual control/east transitions and 1,435-row conversation; qualify the protected camera-W endpoint mismatch. |
| `001B6F00 / 00159B90 / 00157F60`, terminal status cold entry and `00225A00` | Full 1,225-row terminal No route and original owner tests. Exclude accepted `00225AC0`, save serialization and load-game paths. |
| `00188610` and reached duct owners `0016D130 / 0016DE40 / 0016EBA0`, counter read in `001C02E0` | Original ROOM/counter tests and continuous entry/crawl/pickup/exit probe. Mark native reached with worker proof; no full recorded-route parity claim. |
| Water `00187DE0 / 001E8B90`, skid `001EC270`, impacts `001EB7F0 / 001ECB00`, target hit `001B41F0 / 001ED7A0` | Keep worker/oracle evidence distinct. Continuous water entry and an actual native bug-hit receipt are absent; do not promote these solely from standalone tests. |

## Shared target-hit boundary

Original rifle `001861C0` calls `001B41F0(target,hit,direction,flags,0,5)` for an eligible direct class-2 hit; its locked-target fallback can call the same owner. The six placed class-2/model-0 bugs are held at `007A8250 / 007A8540 / 007A8830 / 007A8B20 / 007A8E10 / 007A9100`, callback `00128C10`, HP 15, chain `011349C0`, in all **15 pre-exit captures**. `a01_07_level_exit` has reused the pool for the next scene; it must not be counted as these six live identities. `007A8E10` is one of these bugs. Each of the five original quad faces has header bytes `00 01 00`; actor+5D is 1, so original `001A6440` produces `face[1] & actor[5D] & FE = 0`. These direct hits select effect 07 and sound 15A/15B independently of the dormant gun. A live bug-hit input has not been observed.

The shared owner reads victim header/model and player+C4, writes victim status/h36, and copies direction to victim+70 through SDK `00102948`. The existing pool_field(f60) already owns that SDK destination; adding a shadow actor image or another region for it is unnecessary. Its first missing production view was `70003680`, reproduced by the sparse actual world composition after two calls. `em_aim_fire_world_live` now borrows collision ratio at 3680 and cross lanes at 3684/3688 with write permission for `001B41F0` and its `00189FE0` caller only, refreshing the same pointers after each worker. Other world calls retain their existing view-capacity behavior. The `001FC580` callback now invokes its sole `em_area00_low` owner over stream-owned `D_00281F30`, with the existing positional-gain solver and request-word conversion over canonical actor+B0. No alternate cue queue or sound algorithm is introduced.

Effect 07 uses the existing `em_area00_hud_001ED7A0` through the production dispatch. The supplemental exporter adds descriptor window `00257360+1B0`; each original descriptor's mutable +20..3F and its CFBE0 read share the delivered canonical bytes. Immutable parts match all 16 captures; initialization comes from the pinned ELF. Three kind-1 packet chains use existing transform/packet owners, without a protected chain-page edit.

`python3 tools/test_area01_target_hit_reference.py`: PASS 90 original bug-hit selections, 720 ordered boundaries, 60 production effect packet chains and 2 refusals over 15 pre-exit captures. `python3 tools/test_area01_target_hit_live_reference.py`: PASS 90 actual world calls, 720 ordered boundaries and 120 original delayed-cue policy cases, with 2 missing-owner refusals. An additional 90 cases compare the actual existing native gain solver and resulting delayed-cue words against full original `001FC580 / 001FBF50`; all produced cue words match. Full RAM/scratch equality covers the target/world boundary and the explicit cue-policy boundary; the already-existing gain solver is separately identified rather than claiming original FBF50 scratch parity. Proof receipts are `build/level2-crashes/target-hit-reference/report.json` and `live-report.json`. No live census promotion is justified for 07 or B41F0 until an actual bug-hit receipt exists. Effect 76 has no port owner; no new armored-hit behavior was introduced.

## Exploration coverage at handoff

| Probe / retained route | Observed result and limits |
|---|---|
| `status`, `melee`, `aim-fire` | Status opens/closes, both knife inputs complete, and two shots are observed without faults. Custom input has no original route-parity claim. |
| `a01_s0` | Control room and first talk complete; 1,435 row fields exact with PS2 drive timing. Whole-camera W defect remains in protected files. |
| `a01_s2` | Control-room pickups complete; captured position/item/progress fields match. Request/message timing differs under unrecorded module-read timing, so no full parity pass. |
| `a01_s3` | Fire damage full checker passes 262 rows and whole camera. |
| `a01_s4` | Terminal No and east-room return full checker passes 1,225 rows and whole camera. |
| `vent` | Continuous duct traversal, pickup mutation and exit observed; see separate evidence above. Raw `a01_s5` misses its trigger and is not a pass. |
| `a01_s6` | Raised-bridge boundary full checker passes 229 rows and ending camera. The north room itself is not entered. |
| `a01_00` | 405 exact prefix rows, hang and pull-up observed, then protected kind-6 fault at `0023D930`. |
| `water`, `water-west`, ladder prefix | East detour stops at `0023D930(10)` near `(0.106752,0.036,-716.356995)` on the final build; west stops at `0023D954(0023D958)` near `(-31.941597,0,-646.616211)`. Water and ladder contact remain unobserved. Ladder is an inferred identical-prefix blocker, not a separate completed run. |
| `a01_01..07`, `a01_s1`, `a01_s7` | Downstream of the failed recorded a01_00 prerequisite. Not separately rerun past that fault; no later coverage claimed. |
| North interior, upper catwalk, every destructible object/surface, enemy-attack reactions | No exhaustive first-visit coverage. Static branches and known remaining owners are listed above. |

`build/level2-crashes/exploration-coverage-matrix.json` maps frozen runs to
receipts and explicitly separates direct runs from inferred prerequisite
blockers. The final staged receipts below supersede earlier outcomes for
those same probes. No emulator was launched, no save slot or memory card
was written, and no decompilation build was run during this work.

## Complete shared-file edit list

`em_scene_bindings.c`: read-only floor tick observations; water and row-clip
callback registration/clear/adapters; AREA01 contact and hull provider
registration; target model forwarding; four-lane spawn rotation publication;
terminal owner-read/reset forwarders and include; corrected storage comment.

Makefile: build new `em_player_target_live.c`, `em_area01_hull_live.c`,
`em_area01_transition_services.c` and existing `em_level14_port_boot.c`;
add opt-in exploration/harness, water, target, fire, transition, terminal,
frame-counter and target-hit reference targets. Protected renderer/camera/
checker/exporter/docs files are unchanged. Main and the other worktrees
remain untouched; no push or main rebase is performed.

## Final staged verification

The final staged source rebuild has zero warnings. New Game control passes
with 30-tick displacement `9.599849`; default smoke passes. The original
target-hit/cue/packet, water, frame-counter, runtime, player-view, effect
service and SFX-registry suites pass, as do the shared aim-world and
frame-input sanitizer checks, no-shadow audit and exploration harness.
The separate exporter verifies 48 delivered windows and 14,592 new equal
immutable bytes over all 16 captures.

Fresh final-binary probes observe two shots (324 ticks, magazine 30→28)
and complete duct entry/crawl/pickup/exit (3,381 ticks). The final duct
pickup proof again compares the full 64-byte original before/after blocks.
The repeated water approach is still **FAULT**, at `0023D930(00000010)`,
with water depth zero after 452 observed phase ticks; the protected drawing
blocker is reproduced on this final binary. No fault is hidden or treated
as a passing water run.

Receipts under `build/level2-crashes/`: `index-final-{build,startup,smoke,
references,effects,export}.log`; `index-final-aim/aim-fire/receipt.json`;
`index-final-vent/vent/{receipt,pickup-inspection}.json`; and
`index-final-water/water/receipt.json`. The temporary pre-correction
capacity-test failure is preserved separately as
`index-final-references-before-capacity-fix.log`; the corrected final
reference run passes.

`EM_HEADLESS=1 make test-level-smoke-full` finishes with exit 0: the
20-phase main route through AREA01 arrival, three menu/door side runs,
11 aim/fire runs, three damage/death/restart runs, and ten optional-branch
runs all pass (28 full capture comparisons). This target does not include
the opt-in AREA01 routes beyond arrival; their separate outcomes above
remain in force. Receipt: `index-final-smoke-full.log`.

A final source review parenthesized the counter's integer address offset
(using size_t) before pointer addition. Both the actual worktree and exact
staged snapshot were rebuilt with zero warnings; both are byte-identical
to the binary that passed the complete suite, SHA256
`dc25a4cf616cb86e233a73e74b634071121445c60a417fc7154b633e596779d0`.
Counter, runtime and player-view references pass again. Receipts:
`final-worktree-build.log`, `index-final-build-final.log`,
`index-final-pointer-reference.log`, and `final-verification.json`.

Completed regression traces are retained as gzip archives with verified
round-trip hashes; each `index-final/port/build/level_smoke*` group has
`gzip-archive-manifest.json`. Logs, pads, receipts and the frozen executable
remain available. Temporary staged source trees and unreferenced compiled
fixtures are removed after verification; the committed source and test
commands reproduce them. No disc-derived material is staged.
