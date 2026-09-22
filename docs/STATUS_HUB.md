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
recovery from that actual resident GS state; its dynamic workers remain
unbound while their geometry and lifecycle are being verified.
