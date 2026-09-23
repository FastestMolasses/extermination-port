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

The normal hub is not yet bound into the live status adapter. It still
requires original resident artwork and the status draw-model workers
`0020E250`, `0020E3A0`, `0020E1E0`, and `0020E6F0`. Those workers must be
provided before advertising a complete normal status menu.

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
drawer into ten hover/terminal-infection layouts and exports nine textures
and ten help strings. A separate frozen trace expands the original health,
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
