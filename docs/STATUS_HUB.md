# Original normal status hub

`em_status_hub.c` implements the normal phase-1 branch of original
`0020CDC0`, with the same UI object used by ITEM and BATTERY. It preserves
the original draw-record initialization, message group 0, hover selection,
sound order, input precedence and child screen mapping. Back from ITEM
selects this hub; it does not close the status screen. Triangle/Start/Back
on the hub enters the existing phase-5 exit sequence.

`make test-status-hub-reference` compares 17,520 state and worker-call
cases with the original instructions, including `0020D930` table 0 and
the complete software-double magnitude comparison. Health/infection
message thresholds and simultaneous-button precedence are included.
Draw models, the background and renderer remain explicit workers. The
medicinal phase-2 path and unimplemented children return a fault.

The graphics work uses the original `00209DF0` draw order. Its four
markers are nine line strips each (`00208750`), and its wheels and health
gauge use untextured Gouraud triangle strips (`002082B0`). They must not
be substituted with the old port's menu artwork or generic circles.

`em_item_geometry_arc` is a pure, bounded implementation of `002082B0`.
It retains original GS fixed-point coordinates and packed RGBA, two
separately stepped color ramps, fractional final angular step, VU
reciprocal and the original UI SDK trigonometry. The renderer owns blend
and ordering and converts untextured colors with the original /255
convention. `make test-item-geometry-reference` compares 500 descriptor
cases and 5,562 vertices against the original function, its vector
helpers and full SDK bodies. The remaining boundary is GS/Metal
rasterization, not a claimed hardware-equivalent renderer.

Live open/close (S11b, 2026-09-23): the scene coordinator runs the
original frame machine, so a START/TRIANGLE edge in gameplay makes
001AE7E0 return 2, the r == 2 arm calls 0020E060, and state 3 calls
0020CDC0 every frame (world frozen) until it returns nonzero, then state 5
returns to state 1 (the st14 frame order; SCENE_COORDINATOR_DESIGN.md
section 6, S11b). Until WP-5 those two positions are bound to the legacy
`em_hud` (`em_hud_status_open`/`em_hud_status_tick`), not to this hub.
`EM_STARTUP_TEST=newgame-control EM_CONTROL_STATUS_TEST=1` exercises the
open, 30 status frames and the TRIANGLE close from first control.

The normal hub is not yet bound into the live status adapter. Its 2D
layer now exists as `em_status_hub_ui` (below), but the status draw-model
workers `0020E250`, `0020E3A0`, `0020E1E0` and `0020E6F0` and the
`0020A7A0` moving background are still required separate workers. They
must be provided before advertising a complete normal status menu.

The stable host entry is `em_status_runtime_open()`. The host must first
apply the original gameplay input gates; this function queues B0=0/C5=0
only when both actual hub workers are installed. The pure page core now
accepts that original cold-entry branch. The page oracle passes 1,460
state/call cases, and the sanitizer lifecycle fixture covers normal open,
hub selection of ITEM, ITEM Back to the hub, and the real phase-5 exit.
Its model/render workers are explicit fixture boundaries, not live stubs.

A fresh original capture from preserved ITEM state12 reaches the hub by
pressing Back and saves disposable state14. The source12 hash is unchanged;
the original process exited normally. Ignored evidence is under the decomp
repository's `build/startup-reference/status-hub/`, including `hub.png`,
EE RAM, GS data, scratchpad and the transition trace. The image confirms
the full-body Dennis and SPR4 models, original profile text, health gauge
and inventory readouts. `tools/export_status_hub.py` is ongoing artwork
recovery from that actual resident GS state. It executes the original main
drawer into ten hover/terminal-infection layouts and exports thirteen
textures (including every `00209860` secondary icon) and ten help lines. A separate frozen trace expands the original health,
battery and ammunition workers into 70 ordered commands. The original
numeric formatter runs; byte string copy/append/length and font packet
workers are explicit boundaries. The live dynamic workers remain unbound.

The readable `tests/status_hub_visual.c` consumes generated, ignored
commands through `tools/test_status_hub_visual.py`. It exercises original
arcs, marker endpoints, sprites and text through the native Metal backend.
Build with that tool, and run only after reserving the shared GPU. The
fixture's line segments expand to one-GS-pixel parallelograms; precise
line endpoint coverage remains a rasterization boundary. The original
models are excluded explicitly, and the moving background starts at a
fresh phase. This fixture is never used as a frozen live menu.

The recovered frame requires 2,376 ordered decor records: 758 arc
triangles, 1,080 line triangles, 512 cursor triangles and 26 sprites or
rectangles. The former 1,024-record limit rejected valid original geometry.
`EM_GFX_DECOR_MAX` now gives only Metal's decor queue 4,096 records,
preserving the separate font and untextured budgets. The extra decor
storage is 887,808 bytes. The GPU fixture accepts exactly 4,096 records,
rejects the next one, resets for the following frame and renders all
original commands. It exits successfully and saves ignored
`build/status_hub_visual/hub.png`; its source counterpart is the original
state14 image above. Vulkan and D3D12 remain unimplemented backend
skeletons; this check makes no cross-platform rendering claim.

The dynamic preparation core `em_status_draw.c` now recovers the actual
health `208AD0`, battery `209280` and ammunition `209860` calls. It keeps
original GS coordinates, styles, text, TEX0 values, blend changes and call
order. The health counter advances once per accepted draw preparation;
it is the original UI+20 clock, separate from the trail reset. Battery
charge/capacity remain half-units, including the compact/large layouts,
12-cell rows and original vector color increments. Resource labels and
arc records come from the user's original assets. These functions prepare
commands; the live menu renderer and status model actors remain unbound.

`make test-status-draw-reference` compares 1,080 health cases, 144 battery
cases and 528 supported ammunition cases with original instruction
execution, for 22,206 ordered commands. The ammo cases also compile and
execute the corrected canonical readable PS2 source through a documented
64-bit host pointer shim. The numeric formatter runs in the original
oracle, including negative and over-width inventory probes; byte string
copy/append/length and final font/graphics workers remain boundaries.
All 219 injected worker failures stop preparation and report failure.

The ammo audit corrected the readable source's missing initial UI argument
and ordinary-secondary field width (three places, while reserve and fuel
percentage use four). Unknown secondary values with primary other than2
reach an original draw with the caller's incoming `s0` as TEX0. Six seeded
register cases prove that this is not a zero-texture default. The native
core rejects those unsupported selectors before emitting commands; 144
such cases verify rejection. It does not fabricate an icon or close the
status page. The corrected PS2 source remains an assembly-backed near
match, measured separately in the decomp repository.

## Live 2D adapter (`em_status_hub_ui`)

`em_status_hub_ui.c` prepares the complete `00209DF0` call stream once per
original call and renders it on the 512x448 status canvas. It owns no
layout: `tools/export_status_hub.py` writes ignored version-2
`status_hub.emhs` records from executing `00209DF0` for every hover and
the terminal-infection branch, including each `00207D00` mode call. At
prepare time the health `00208AD0`, battery `00209280` and ammunition
`00209860` records run through `em_status_draw.c`, the infection figure is
rebuilt as `001C5FB0(n,3,1)` plus the resident `00273570` string, and the
`0020AC70` trail at base (432,272) runs through `em_item_trail.c`. The
exporter also executes the `001FCA10` mode-4 presenter
(`001FCB90(0x8A,0xA8,0,line)` through `001FE070`/`001FC7B0`) over the
captured RAM, so the ten group-0 help lines are original tall-font calls,
split per line 12 GS half-lines apart, in style `0x606060`.

The earlier uncommitted header was corrected against the original code:

* The UI+20 clock is shared status UI state, not page state. `00208AD0`
  also runs from `0020AE40` pages with flag 8 (`002160B0`), and only the
  UI memsets `0020E060` (outer reset) and `001AF690` clear it. The caller
  passes the clock to `em_status_hub_ui_prepare`.
* The trail is the shared `D_00821300` ring and `D_00275C90` cursor. It
  is reset by `0020E020` on hub (`0020CDC0` step 0), ITEM (`0020EE50`) and
  other page entry (`00211970`, `0020DFA0`). The caller passes it too.
* The proposed background layer was removed. `em_hud_background_sprite`
  is not a verified `0020A7A0` and paints an opaque fill that would cover
  the status models. The background stays an explicit required worker.

Reference check: `python3 tools/test_status_hub_ui_reference.py`
compares 167 prepared streams (19,968 ordered calls) with original
`00209DF0` execution. The cases cover five hovers, every infection figure
0..100 (including truncation), health/warning thresholds, clock wrap,
battery charge/capacity/equipment and every ammunition selector. It also
compares 22 help calls with `001FCB90`, and 20 trail frames (10,240
triangles plus ring state) with `001B62C0`/`0020AC70` at the hub base, with
a `0020E020` reset. One rendered frame (2,434 renderer calls) is compared
with the original records and executed `002082B0` arcs. The clock
ownership check executes `0020AE40` (flags 8/2/1), `0020E060` and
`001AF690`. The test rejects 21 malformed record/atlas files. The
asset-backed ASan/UBSan fixture `tests/status_hub_ui_test.c` covers the
lifecycle, atlas invalidation, rejected display values (including the
inherited-TEX0 selector) and injected font/texture/triangle failures, all
latched.

Boundaries: SDK transcendental values (host libm on both sides), byte
string copy/append/length/memset workers, glyph metrics, one-GS-pixel
marker line coverage and final GS/Metal pixels. Metal flushes glyphs after
all decor, so text is composited above sprites the original submits after
it; the prepared stream keeps the original order. The adapter is not yet
called by `em_status_runtime`.

