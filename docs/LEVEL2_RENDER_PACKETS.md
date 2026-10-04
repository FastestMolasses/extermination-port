# AREA01 dynamic packet binding

2026-10-03. The canonical render-context owner now composes AREA01's
dynamic packet builders with its existing static pass. This is packet
construction, verified against the original instructions. Presentation of
the dynamic VU programs remains a separate, required dependency. The
AREA01 gameplay gate must stay until that dependency and the actor binding
are complete.

## Original call chain and shared owners

`001C1D00(D_008101D0)` calls `001E0CF0` and `001D5370`. The latter's
AREA01 keys 0x0100/0x0101 reach `001D5BD0`, which walks the dynamic table
at `*D_0028A5A4`. `001D5A70` classifies clipping; `001D4FC0` and
`001D5170` build the unclipped and partially clipped packets respectively.
They call `001CAAC0`, then `001CB760`, to insert packets into the same
canonical chain page. The original byte-matched 001C1D00 state-zero arm
initializes its state and falls through into the draw body on that call.

`em_render_context_live.c:dynamic_pass` binds the existing translations:

| Original | Translation owner used by the adapter |
|---|---|
| 001D5BD0 / 001D5A70 / 001D4FC0 / 001D5170 | `em_area01_render_vif.c` |
| 00121870 / 001D2090 / 001D4750 | `em_static_world.c`, through `em_swc_with_static_world` |
| 001D1F80 | existing `em_rcl_001D1F80` over the canonical context |
| 001CAAC0 | `em_anim_runtime_rest.c` |
| 001CB760 | `em_packet_chain_original.c` over the canonical page |

There is no duplicate packet, depth or clipping implementation. The
001CAAC0 adapter supplies a transient 16-byte position view and a read-only
copy of the canonical 0x70003AC0 matrix because that existing owner expects
those arguments through its pose-memory contract. Page stores still go
directly to the render-context owner's `EmPacketChain`.

## Memory ownership and binder changes

The existing render context, packet arena, page, skin records, constants
and scratchpad remain their single canonical owners. The loader supplies
two additional borrowed, read-only windows with
`em_rcl_dynamic_world_bind(table_word, bytes, size)`:

| Input | AREA01 original address / extent |
|---|---|
| loader resource slot 0x45, the address word | D_0028A5A4, four bytes |
| dynamic table | 0x0177A940, size 0x6490; header plus 12 records of 0x860 bytes |
| existing static bank, loader resource slot 0x44 | 0x014DC940, size 0x29D960; 694 objects |

The word and table must stay alive until the next area bind. The adapter
copies neither, exposes neither through `em_rcl_bytes_mut`, and rejects
test-hook writes into them. `em_rcl_bind` invalidates the prior area's
borrowed windows. Install the dynamic windows after that call. A reached
dynamic pass without its table latches a fault at 0x0028A5A4; an already
faulted owner cannot be repaired by supplying a late table.

The required scene integration is deliberately left to the scene owner:

1. In `scene_bindings.c:rcl_bind`, retain `em_rcl_static_world_load(NULL)`
   only for AREA11's `roster_scene()`. Calling it after AREA01's bank has
   been delivered discards that bank and reloads the AREA11 export.
2. Bind the render context for `world_scene()` in the 001AFCA0 worker,
   after the delivered area's static bank is installed.
3. After successful `em_rcl_bind`, for AREA01 obtain the original address
   from `ld->d28A490[0x45]`, resolve its bytes and extent through
   `em_module_loader_memory_rest`, and pass those bytes plus the canonical
   slot word to `em_rcl_dynamic_world_bind`. Route a failure through the
   existing scene fail-stop path. Keep the loader alive through the area.
4. Keep the gameplay gate while any reached actor or presentation worker
   remains missing. No synthetic empty table is an acceptable binding.

The application needs `em_area01_render_vif.c` in its source list. The RCL
standalone oracle also links the existing `em_anim_runtime_rest`,
`em_pose_host_workers`, `em_player_floor`, `em_player_reaction` and
`em_player_fall` dependencies. Its source list has been updated here;
the parent owns the application Makefile. No scene binder or presentation
file was edited for this adapter.

## Instruction and regression verification

Run `python3 tools/test_level2_render_packets_reference.py`, with
`EM_TEST_FULL=1` for the full corpus. The test compiles the real RCL owner
and all reached packet/depth workers. It uses the existing original EE
interpreter with the AREA01 VCLIP extension, checks the relevant original
code against the pinned ELF, and executes the original packet/depth
instructions rather than replacing them with stubs.

The inputs are the existing AREA01 arrival and route RAM captures. Each
capture's static bank and dynamic table must exactly match the existing
disc-derived EMSC assets. The native pass borrows the same dynamic data.
After each area-init, frame-begin, frame-head, static/dynamic-pass and
page-close entry, the test compares every RCL-owned byte, including the
packet arena, page, context, skin records, data and clip scratchpad, and
the remaining recorded host boundaries. It alternates both frame slots.
A targeted matrix variation on the arrival table reaches the partial-clip
arm while preserving the real depth projection. This is explicitly a
synthetic clip input, not a claim about captured camera motion.

| Test | Result |
|---|---|
| AREA01 quick | PASS: 5/16 snapshots, 49 composed entries, 370 original calls across the six tracked dynamic/depth/page functions; 11.8 s |
| AREA01 full | PASS: 16 snapshots, 276 composed entries, 2,278 tracked original calls; 23.0 s |
| Existing RCL quick | PASS: 3 first-level beats, 195 entries; 13.4 s |
| Existing RCL full | PASS: all 15 first-level beats, 1,335 entries; 15.0 s |

The AREA01 full clip classifier outcomes are 240 inside, 108 partial and
432 outside (0, 1 and 255). The tests also check borrowed-view visibility,
read-only access, area invalidation, null/empty input rejection and the
missing-table fail-stop. No emulator was launched.

## Presentation and live-proof limits

At this checkpoint, `em_static_world_draw.c:call_kernel` recognizes only
0x00237180 and 0x00239C90. The chain-page MPG dispatch does not recognize
AREA01's dynamic programs 0x00237450 and 0x00237720, and the live page-unit
adapter accepts only the existing class-2 actor units. Those paths must
continue to fail-stop until the original dynamic microcode is translated
and connected to the existing GIF/GS consumer. Packet equality does not
prove transformed vertices or pixels.

The arrival capture already contains 24 dynamic kicks. The a01_00,
a01_01 and a01_02 endpoint captures contain zero, which does not establish
unreachability: later a01_04 and a01_05 contain 16 and 24. These programs
are a real arrival dependency. The background enable flag is clear in all
16 tested AREA01 sub-zero captures; this does not prove it is always clear.

The existing `EmRclStaticSample` and level-smoke serializer omit the
dynamic table. The standalone test proves this native composition over
captured inputs, not the future live loader hookup or a live gameplay
frame. Extend live smoke's dynamic inputs and checks when the scene owner
connects the pass. Subsequent VU presentation work should record its own
instruction oracle and packet/GS-state evidence before this limit is
considered resolved.
