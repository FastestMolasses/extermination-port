# AREA01 message-bank binding

The AREA01 rebuild now selects `assets/area01/message_data.emmd` through
`em_message_live_select_area`; AREA11 selects its existing message export.
This changes the resource and record-table views without reinstalling the
frame service. The request block, text style, draw buffers, glyph state,
host, streams and presenters keep their existing storage and values.
Missing or mismatched data latches a fault. Message reset remains at the
original `001FC9B0` call sites.

Original callers: `001FCA10 -> 001FDB80 -> 001FD790` fetches records from
`D_00264DD0[D_00810700+1]` for an area line. `001FD950` draws that line
through the loaded bank at `D_0028A594`; global lines keep `D_0028A4E8`.
The original instruction oracle checks these service paths. The native
resource selection is in `w_001AFCA0`, after the area's collision load;
it is not an invented original function call. It runs before any AREA01
owner can post a message. It does not enable the still-unbound world frame.

The EMMD loader previously accepted area 23 into an array of length 23
and rejected area 0. The range now accepts indices 0–22, matching the
allocated views, and rejects any index outside them. The first-visit
binding uses only 1 and 11; no additional area is claimed live.

## Verification

`make test-message-area-reference` builds the actual live service with
test-only frame, font-upload and renderer boundaries. Its bridge inspects
the service without adding a production introspection API. The streams,
face and presenter boundaries record calls, not replacement game logic.

The test switches AREA11 -> AREA01 -> AREA11 -> AREA01 and reselects AREA01,
checking that each selection preserves the complete dynamic service/draw/
glyph state and all bindings, without reinstalling step F. AREA01's global
and area bank bytes (**11,956 bytes**) equal the original NPC-conversation
capture at its actual resource pointers. It checks a missing-bank failure
and the persistent fault latch.

The preservation fixture combines a recorded busy request prefix (a01_05
f300) with the end snapshot's remaining request bytes, synthetic nonzero
draw-buffer sentinels, and glyph records produced by a test string through
the real glyph owner. It deliberately makes accidental resets visible;
it is not presented as one captured original whole-frame state.

The original-instruction service comparison uses
`build/s87/route_a01/a01_05_npc_bridge_talk` in the decomp:

- Quick: **18 cases / 54 ticks**, recorded lines 0x0A and 0x40 and the
  global shaft-door refusal 0x80000008, both game modes and three voice modes.
- `EM_TEST_FULL=1`: **1,110 cases / 3,330 ticks**, every area-table starting
  record plus the refusal, with the same mode combinations.
- Every case compares all 156 request-block bytes, all 14 shared mode/
  mailbox bytes, and every non-draw worker call on each tick. The native
  glyph/layout routines run; the service oracle's draw boundary is stubbed,
  so this test does not claim a pixel or glyph-packet comparison.

Both runs pass. This is service/bank integration evidence, not the main
AREA01 route smoke: the frame-machine guard is still present.

Shared edits: `em_message_live.c/.h` (preserving resource switch),
`em_scene_bindings.c` (area-aware resource selection), `Makefile` (quick
oracle target). New files: `tests/message_area_bridge.c`,
`tools/test_message_area_reference.py`, this document. No launcher option
was added and no disc-derived bytes are embedded in source.
