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

Live open/close (S11b, WP-5; 2026-09-23): the scene coordinator runs the
original frame machine, so a START/TRIANGLE edge in gameplay makes
001AE7E0 return 2, the r == 2 arm calls 0020E060, and state 3 calls
0020CDC0 every frame (world frozen) until it returns nonzero, then state 5
returns to state 1 (the st14 frame order; SCENE_COORDINATOR_DESIGN.md
section 6, S11b). In AREA11 those two positions run the original page core
for every status screen (`em_status_page` in the interaction host's
`em_status_runtime`): the cold entry, this hub, the ITEM root, the BATTERY
page and the 0020E0C0 exit, whose two extra ticks after the close edge
match status_04 (`make test-level-smoke` asserts it). Scenes without the
AREA11 host keep the legacy em_hud screen.

Since the WP-5 fix round this hub is live: the runtime's hub phase
(`em_status_runtime_bind_hub`) runs `em_status_hub_tick` with
`em_status_hub_ui` as its DRAW and `em_status_background` as its
BACKGROUND (the hub tile 0x20045EE59D421E40, taken from the hub atlas).
The runtime owns the UI+0x20 clock (zeroed by the 0020E060 memset,
advanced by 00208AD0 inside `em_status_hub_ui_prepare`) and the shared
trail (reset by 0020E020 at sub-state 0). Sub-state 0 draws nothing; each
sub-state-1 frame steps 0020A7A0 once and prepares 00209DF0 once, and the
render draws the background, then the prepared stream, then the group-0
help line while D_002821B4 == 1. The hub, ITEM and BATTERY adapters share
the UI texture slot, so the page that draws marks the others' uploads
stale. X on hover 4 enters ITEM through the page core; hovers 1-3
(DATABASE, SPR4, MAP) fault there until those pages are translated.

The status draw models are live (WP-5, 2026-09-23). The host's
`hub_models` binds the model workers to `em_status_models`
(docs/STATUS_SCENE.md section 7), which runs the translations of
`em_status_scene_original` over the static actor pool D_0028B020:
`0020E250` (the equipment letter models: `0020E3A0` codes, `0020E1E0`
binds them from the letter bank D_0028A56C, `0020E460` ticks them),
`0020E6F0` (the menu player, clips 0x1C2/0xA, with `0020EC80`) and
`001B0000` (the pool walk). The page events bind 0020DFA0 (001AFE60 and the
UI view D_00810610), 001AFEB0 and 001AFE60. The models are drawn by their
`001CB580` draws on 0020DFA0's UI camera, after the background and before
the 2D layer (`em_status_runtime_render`, section "Draw order" of
STATUS_SCENE.md). `tools/export_status_models.py` exports the menu player,
its two clips and the six glyph models the captured inventory spawns
('/', '@', '0', '1', '2', '8'); any other glyph, variant or costume faults.
`make test-status-models` runs the workers over the status-hub capture:
at the tenth walk the menu player's breathe/yaw equal the capture, pool
records 0..7 equal it in every modelled byte, and all 27 node world
matrices are bit-exact. The level smoke asserts the seven records and one
draw per record per hub frame after the first.

`0020A7A0`, the moving background, is translated: `em_status_background.c`
(pure; the one shared D_002655A0 state lives in
`em_status_background_draw.c`, drawn by the hub, the ITEM page and the
BATTERY page). Its live sine is the translated original sinf 0011E2A8
(`em_sdk_math_original`) over the ELF window D_0026C170..D_0026C658
(`tools/export_sdk_math_tables.py` -> `assets/sdk_math_tables.emsm`).
`make test-status-background-reference` executes the original 0020A7A0 and
0011E2A8 (EE FPU model) over the .data image, consecutive frames, every
pulse boundary, both scroll wraps and a burst sweep, and over the
D_002655A0 blocks of the status-hub and panel RAM captures, whose layer-2
burst offsets are exactly (phase / 0.25 + 1) EE adds of 0.3; the native
step reproduces those captured bits (an IEEE add does not), and a host
sinf in place of 0011E2A8 fails. It is the only D_002655A0 translation.

Sprite orientation and blend (00207E40): UV (0, 0) goes with the bottom
vertex (y0 + 8h), so the GS draws the texture's last memory row at the top.
The atlases (export_ui.py `decode_token_lm`) store row y = memory row
h - 1 - y, so the port's top-left (u, v) is the original's top edge. 00207D00(1,
0) selects GS ALPHA 0x44 (Cs*As + Cd*(1 - As)); the tile's TEX0 has TCC 1,
TFX 0 (modulate), which the backdrop quad's colour/128 reproduces. The
orientation was checked against the status-hub display image (a one-off
measurement, 2026-09-23): the three layers were rendered from the
capture's own D_002655A0 block with the atlas tile in the four U/V
orientations and correlated with hub.png in background-only areas. The
port's orientation correlates 0.90 and 0.94 in the two clean areas
(x 390..420 x y 0..200 and x 160..300 x y 0..20 of the 512x448 canvas);
V flipped gives 0.23 and -0.18, U flipped 0.31 and 0.20, both flipped 0.23
and 0.08.

The live entry is the page route (`em_status_runtime_page_open` and
`_page_tick` at the scene core's 0020E060/0020CDC0, over the canonical
request bytes). `em_status_runtime_open()` belongs to the runtime's own
frame path, which only the sanitizer fixtures still drive. The pure page core now
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
workers are explicit boundaries. The dynamic workers run live through `em_status_hub_ui`.

The readable `tests/status_hub_visual.c` consumes generated, ignored
commands through `tools/test_status_hub_visual.py`. It exercises original
arcs, marker endpoints, sprites and text through the native Metal backend.
Build with that tool, and run (`--run`, which opens a window) only after
reserving the shared GPU. The fixture's line segments expand to
one-GS-pixel parallelograms; precise line endpoint coverage remains a
rasterization boundary. The original models are excluded explicitly, and
the moving background starts at a fresh phase. This fixture is never used
as a frozen live menu. The live hub, models included, is rendered headless
by the level smoke: `EM_LEVEL_SMOKE_UNTIL=status
EM_LEVEL_SMOKE_HUB_CAPTURE=<file.bmp>` writes the hub frame of walk 10.
Compared with hub.png (2026-09-23), the menu player's bright-pixel box is
(371..439, 46..191) against the capture's (372..438, 46..192) in the
640x480 image, and the SPR4 model and ammunition icon sit where the
capture shows them.

The recovered frame requires 2,376 ordered decor records: 758 arc
triangles, 1,080 line triangles, 512 cursor triangles and 26 sprites or
rectangles. The former 1,024-record limit rejected valid original geometry.
`EM_GFX_DECOR_MAX` now gives only Metal's decor queue 4,096 records,
preserving the separate font and untextured budgets. The extra decor
storage is 887,808 bytes. The GPU fixture accepts exactly 4,096 records,
rejects the next one, resets for the following frame and renders all
original commands. It exits successfully and saves an ignored
`build/status_hub_visual/hub.bmp`; its source counterpart is the original
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
commands; `em_status_hub_ui` renders them live.

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
  was not a verified `0020A7A0` (it is deleted since WP-5); the translated
  `em_status_background` is the hub's EM_STATUS_HUB_BACKGROUND worker when
  this hub is bound.

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
it; the prepared stream keeps the original order. Since the WP-5 fix round the runtime calls it as the hub worker's DRAW.

