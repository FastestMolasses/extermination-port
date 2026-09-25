# Player drop shadow (original 001DA6A0)

The first-control screenshot (`original_area11_playable.png`) shows a dark
shadow under the player. It has the silhouette of the legs and rifle, not a
round blob. The port draws no shadow. This document records the original
mechanism, the native module that reproduces its EE side
(`src/game/em_shadow_original.{h,c}`), the translation of the two VU1 clip
kernels (`src/game/em_vu1_shadow_clip.h`), the receiver data exporter, and
how each was verified. Addresses
are boot-ELF addresses. RAM/GS values come from the captures in
`../Extermination/build/startup-reference/`.

## What the original draws

The shadow is a render-to-texture projected shadow with a destination-alpha
volume mask:

1. The actor's shadow proxy mesh is rendered from above into a 128x128
   offscreen target. The mesh is skinned by the actor's own node palette.
2. The level geometry near the actor is drawn a second time. The kernel
   projects the target onto that geometry from above and darkens it.
3. A 16 x 40 x 16 box below the anchor, drawn into destination alpha,
   limits the darkening to visible surfaces inside the box.

### Call path and gates

- **Player:** `0015C160` (player post-step) returns at once when
  `D_008102B1 == 0` (no `001CB590`, no `+0x4C` draw). Otherwise it runs
  `001CB590(player)`, which stores its argument to `D_00275B48` and
  `D_00275B44`. It then calls `001DA6A0(D_00275B44)`, i.e. the player,
  when `D_00810771 != 1` and `player+0x214 == 0`. With `+0x214 != 0` it
  calls `0015BF90(player)` instead. `0015BF90` is a floor-raycast variant:
  `0019A570` mode 6, then `001F9100`. It is not translated, and no capture
  takes it. The `+0x4C` method (`001CAA00` in every capture) follows with
  a0 = `D_00275B44`. `em_shadow_original_route_0015C160` encodes this
  routing; the 0015BF90 route latches `EM_SHADOW_FAULT_UNTRANSLATED`. The
  oracle executes `0015C160` itself over 36 patched gate combinations and
  checks the callee, its order and a0 against the native route.
- **Stage:** gameplay `001AE5E0` calls it at 0x1AE654, after
  `001AFD70(0)` and before `001F0360`. Cutscene `001AE6B0` calls it at
  0x1AE798, after `001AFD70(2)` (docs/ORIGINAL_FRAME_ORDER.md). The shadow
  is therefore drawn after the level and the walked actors, and before the
  player's own `+0x4C` draw.
- **Other actors:** `001BA580(actor, actor+0x0D)` (read from the .s)
  calls `001DA6A0` unconditionally only for the code bytes 0x6C and 0x6A.
  Twenty other codes get a category index s0 into `D_008106D4` and draw
  only when `actor+0x56 != 0`:

  | s0 | codes |
  |---|---|
  | 9 | 0x68 |
  | 8 | 0x66 |
  | 7 | 0x64 |
  | 6 | 0x61 (area 0x0D: `001BA7F0` instead of `001DA6A0`) |
  | 5 | 0x5E, 0x5D |
  | 4 | 0x5A, 0x59 |
  | 3 | 0x55, 0x54 |
  | 2 | 0x51, 0x50, 0x4F |
  | 1 | 0x49, 0x48, 0x47 |
  | 0 | 0x40, 0x3F, 0x3E, 0x3B |

  Any other code draws nothing. After the draw, `D_008106D4[s0]` values 1
  and 2 call `001D06E0(actor, 1 or 0)` and clear the byte; `001D0C70(actor)`
  always follows. `src/func_001BA580.c` (NEARMISS) gets the leading code of
  each group wrong by one category: it sends 0x68 to the unconditional
  call, 0x66 to 9, 0x64 to 8, 0x61 to 7, 0x5E to 6 with the area test,
  0x5A to 5, 0x55 to 4, 0x51 to 3, 0x49 to 2 and 0x40 to 1. The other
  codes, including Roger's 0x47, are right. `00134090` also calls
  `001DA6A0`. The AREA11 overlay calls `001BA580`
  from Roger's code (0x8239C8, 0x823B24, 0x823BFC; the callback is
  0x8237E0). Roger's record has code 0x47, `+0x56 = 1` and kind 0x29, so
  Roger is eligible. The oracle executes `001CB590` + `001DA6A0` for every
  such record (0x7A8830 in all nine captures, plus 0x7A96E0 in opening and
  handoff): all 11 runs return at the clip test, so no capture shows
  Roger's shadow. Each display list holds exactly one `001DA290` chain,
  and in the current list it is the player's. Some lists hold further
  `0x817E20` REFs without an alpha-clear start. In elevator-completed they
  lie past the current list's closing NEXT tag at 0x29E6E0 (the cursor),
  so they are stale bytes from an earlier, longer list. The others were
  not traced. Crates, doors and the other props have
  `+0x96 == 0`, which returns at once.
- **`001DA6A0` gate:**
  - The kind is `actor+0x96` (0x28 in every player capture); 0 returns.
  - The anchor for kind 0x28 is node `actor+0x98` at `+0xC0`, or
    `actor+0xB0` when that byte is 0xFF. Kinds 0x2E, 0x2F, 0x30, 0x31 and
    0x34 use node 2. Every other kind uses node 3 (jtbl_0026E690).
  - Three probes are clip-tested with `vclipw.xyz` against ctx+0x2240
    (`001CD370(0)`): anchor, anchor-(0,40,0), and anchor-(0,5,0) for kind
    0x2E or the anchor again otherwise. Any plane common to all three
    rejects.
  - `actor+0x23C != 0` (kind 0x28 only) selects the variant alpha matrix.
- **Areas:** `001D98A0` multiplies an alpha matrix chosen by
  `D_00810700 << 8 | D_00810701`. Nine keys take `D_0026E590` (alpha
  base 101) and 36 keys take `D_0026E550` (base 71). AREA11 (0x0B00) is
  among the 36. Any other key takes no matrix: the alpha lane stays 1.0,
  the clamp yields 0 and the shadow is invisible. The variant takes
  `D_0026E5D0` (base 51).
- **Status hub and panel:** state 3 runs no world frame
  (ORIGINAL_FRAME_ORDER.md, trace st14), so `0015C160` does not run.
  Executing `001DA6A0` over the status-hub and panel RAM rejects at the
  clip test anyway (flag 0x8 on all three probes). Both RAM images still
  hold chains, but not in the list being built: the chain nearest the DMA
  cursor `ctx+0x10` lies above it (panel 0x2FCC40 vs cursor 0x2F7F10,
  status hub 0x2FCC40 vs 0x2F9390), so it was written in an earlier frame.
- **Every capture of a world frame draws it.** Every capture whose current
  list is a world frame holds the chain in that list: playable,
  panel-animation, elevator-completed, elevator/clip47, roger-encounter,
  opening and handoff. The first version of this document said opening,
  handoff and clip47 held no chain. That was a detector bug in the oracle,
  not a behaviour: the tag writers store only the QWC halfword and the ID
  byte, so tag bits 16..23 keep stale buffer bytes (the REF at 0x2FC670
  in handoff reads 0x304C0009), and the old pattern required them to be
  0. With those bits masked the chain is found in both display lists of
  every capture. No capture shows a world frame whose gates pass and whose
  list lacks the chain.

### Steps (001DA6A0)

| Step | Original | Effect (row-vector matrices) |
|---|---|---|
| light | `001D98A0(anchor, variant, 8.0)` | L = `D_00817F70` = (0,-1,0,0), the result of rotations by the constant angle 0. `D_00817F20` = V, the view from anchor+(0,6,0): x' = anchor.z - z, y' = x - anchor.x, z' = anchor.y + 6 - y. ctx+0x24B0 = V x diag(1/16,1/16,1,1) x T(0.5,0.5) x alpha matrix, which gives u = x'/16 + 0.5, v = y'/16 + 0.5 and w = 8388608 + base - 2z'. `D_00817FF0` = L; `D_00817FC0` = up x (previous L x up), which is 0 in practice. |
| spread | inline | The loop is meant to measure the node spread, but all four accumulators keep the minimum (`c.lt.s` with `bc1f`/`bc1fl`). The spread is therefore 0 and the box size stays 16. Unit = 128/16 = 8. |
| picks | `001DA080` | `D_00817FB0` = the node with the smallest dot with L (the highest node). `D_00817FA0` = the far pick, which stays node 1 while `D_00817FC0` is 0. |
| alpha clear | `001DA290`, `001DA1E0` | A DIRECT two-triangle strip (PRIM 0x044, REGS RGBAQ + 4 XYZF2) over the whole field (1792..2304, 1936..2160, Z word 0xFFFFFFFF, RGBAQ 0) in state block (2,9): ALPHA 0xA9 (Cd kept), TEST 0x51001 (alpha test NEVER, fail FB_ONLY), ZTST GEQUAL, ZMSK. The frame's destination alpha becomes 0 and its colour is unchanged. (Earlier versions of this table called it a sprite.) |
| box x2 | `001DA310` | Outward cube (model 0x14, chunk27 library, ±5) with RGBAQ (0,128,0,128), then inward cube (model 0x15) with RGBAQ (128,0,0,1). The RGBAQ bytes come from `00128250` (0x1DA5BC..0x1DA5E8), the soft-float float-to-unsigned conversion: NaN, zero and negatives give 0, values of 2^32 and up give 0xFFFFFFFF, the rest truncate; the bytes are shifted and OR-ed without masking. Both use W = scale(1.6,4,1.6) + (0,-20,0) + anchor. Two kernels run: 00237180, then 00239C90 (clipping). Both receive (W x V) x P. The first uploads of W x `D_70003AC0` and W x 0 are overwritten before any MSCAL. Result: destination-alpha bit 7 marks visible surfaces between the box's front and back faces. |
| silhouette | `001D9EE0` | Packet `D_00817E20`: FRAME FBP 0x12C (GS byte 0x258000), FBW 2, PSMCT32; XYOFFSET 1984; SCISSOR 128x128; TEST ZTST ALWAYS; a sprite clears the target to (128,128,128,0). The VP is `D_70003AC0` = V x diag(8,8,1,1) x [GS rows: centre 2048, Z 0.8996 / 1677721.5]. `001C7420` uploads the actor's 21 world-space node matrices (node+0x90 of the `+0x110` nodes) x VP. Around that call 001D9EE0 saves the actor qword `+0x80`, zeroes `+0x80/+0x84/+0x88` and restores it afterwards (0x1D9FFC..0x1DA024). `001D4740(D_0028A490[kind])` draws the proxy mesh; for the player that is `extract/chunk03/f32_id28.bin`. The template has only XYZ2 per vertex and no ABE. Every pixel is the A+D RGBAQ 0xFFFFFF80 = (128,255,255,255). |
| receivers | `001D5C80(D_00817FB0)` | Level grid (ctx+0x140, stride +0x148, cell +0x150/+0x154, origin +0x158/+0x15C). The cells cover ±15 around the highest node, never fewer than 3x3, clamped to 0..31, z-major, 4 slots each. Each object with id > 0 is clip-tested with its AABB against 1024x448 x `D_00810610` (trivial reject), then against the 4096 guard band. Kernel 0023C200 draws each object; an object leaving the guard band (class 2) gets a second pass with 0023E8A0 over the same strips, which draws only the triangles 0023C200 left undrawn for CLIP (clipped; see GS side). Before the loop, `001D4CD0` sets state block (2,6): TEST 0x5C00D (alpha > 0, DATE with DATM=1, ZTST GEQUAL), ALPHA 0x44 = (Cs - Cd) x As / 128 + Cd, ZMSK. It also sets CLAMP 5, TEX1 bilinear, and TEX0 0x5DC00A580 (TBP 0x2580, TBW 2, PSMCT32, 128x128, TCC 1, TFX MODULATE). ctx+0x24B0 goes to dmem 8. |

Receiver kernel 0023C200, per vertex:

- Colour: RGBAQ = (0, 0, 0, a). a is the w lane of ctx+0x24B0 x p,
  clamped to [8388608, 8388863]; its low byte is the alpha.
- Texture: ST = (u, v) x Q, with Q = 1 / clip w.
- Position: XYZF2 goes through the standard camera. Fog comes from the
  same constants as the object kernel (template qword 1021 = (A, B, 255,
  2048)).
- ADC is set on strip restart or when the vertex is clipped.

Per pixel, the GS combines these:

- Texture sample: Ct = (128, 255, 255) and At = 255 inside the silhouette;
  At = 0 outside it, where the alpha test fails.
- Source colour: Cs = Ct x 0 = black, then fogged toward FOGCOL.
  Source alpha: As = (255 x a) >> 7.
- The pixel draws only where destination-alpha bit 7 is set.
- The pixel is darkened by As/128.

**Consistency with the screenshot.** In the playable capture, the snow
receivers under the player carry a = 36 (270 vertices) and F = 124..141.
AREA11 FOGCOL is 48. Snow (91,106,106) therefore becomes about (53,60,60).
The screenshot's shadow reads (55,62,62) against snow at (91,106,106).

## Native module

`em_shadow_original_001DA6A0(actor, nodes, slots, scene, state, plan,
workers, fault)` reads only these original record bytes:

- actor `+0x09`, `+0x96`, `+0x98`, `+0xB0`, `+0x23C`
- node `+0xC0` qwords

The scene view carries ctx+0x2240, +0x2340, +0x2380, `D_70003AC0` (==
ctx+0x23C0 in every capture with a scratchpad dump), `D_00810610`, the zoom
at ctx+0x2468, the area bytes and the level grid. `state` is
`D_00817FF0`.

The module computes everything on the EE/VU0 arithmetic: truncated binary32
operations and rounded EE `div.s`. It then calls the workers in the
original order:

`w_alpha_clear`, `w_box` (0x14), `w_box` (0x15), `w_silhouette(kind, vp)`,
`w_receiver_begin(uv)`, per grid id `w_object_bounds`, per accepted object
`w_receiver(id, class)`, `w_receiver_end`.

A missing worker or view, a negative worker result, a node or grid read
outside its view, or more than 512 receivers latches a fault and returns
-1. `em_shadow_original_route_0015C160(b1, d771, w214, fault)` returns 0
(no shadow call) or 1 (call the module), and for the untranslated 0015BF90
route latches `EM_SHADOW_FAULT_UNTRANSLATED` at 0x0015BF90 and returns -1.
`em_shadow_original_receiver_vertex` is the kernel's (u, v, 1, a) slice.
`em_shadow_receivers_*` load the receiver asset (Assets) and give the grid
fields of the scene view, `w_object_bounds`, and each object's and box
model's VU1 vertex list.

## Verification

`python3 tools/test_shadow_original_reference.py` (report:
`build/shadow_original_reference/report.json`):

- **A. Execution.** `001CB590` + `001DA6A0` run as original instructions,
  every callee executed. The DMA cursor is set to the start of the chain
  in the list being built (the greatest chain start at or below
  `ctx+0x10`). The rebuilt chain is byte-identical in four captures:
  - playable: 569 qwords, 7,898 written bytes
  - panel-animation: 372 qwords, 5,277 bytes
  - elevator-completed: 372 qwords, 5,277 bytes
  - elevator/clip47: 380 qwords, 5,333 bytes

  In the two cutscene captures every written byte matches except inside
  the silhouette's bone palette: roger-encounter 225 of 10,470, opening
  182 of 10,477. The pose advanced after those lists were built.

  In handoff (569 qwords, 7,898 bytes) the tag stream up to the receiver
  section is identical and 225 palette bytes differ. The receiver section
  differs in 2,224 bytes: the captured list draws 18 receiver passes, the
  capture-time RAM produces 19. The captured list has object 0x1599780,
  which the RAM state does not accept, and lacks the 0023E8A0 re-passes
  of 0x1576C80 and 0x1588B00. So that list was built from a different
  pose and view than the RAM now holds. Which later step changed them is
  not established.
- **B. Native, bit-exact against the executed original:**
  - 1,792 bytes of light globals and ctx+0x24B0
  - 448 bytes of silhouette VP
  - 5,376 bytes of the silhouette bone palette (world-space node+0x90 x
    VP), in the four exact captures
  - 4,536 bytes of box uploads, including RGBAQ and both kernels' matrix
  - 3,648 bytes of UV and camera uploads
  - TEX0
  - the receiver object sequence with its 0023E8A0 re-passes
  - the worker order
  - 24 synthetic variations: kinds, areas, variant, sub 0xFF, clip reject
    and four random previous lights. These add 3,872 bytes and the
    receiver lists.
  - The area switch: its 45 keys are read from the ELF's addiu/beq pairs
    (0x1D9C4C..0x1D9E74), and all 6,144 keys 0x0000..0x17FF select the
    same alpha matrix natively (`EM_TEST_FULL=1`; the default run checks
    256 of them, see F).
- **C. Kernel.** The 0023C200 VU1 program runs after a VIF replay (BASE
  0x190, OFFSET 0x101) on all 108 receiver batches of the seven captures:
  3,456 vertices and 96,768 bytes. Every ST and RGBAQ equals the native slice scaled by the
  kernel's Q.
- **D. Probe.** The angle-0 rotations 00102A60, 00102BB0 and 00102B08 are
  executed on a signed-zero probe; each equals a VU0 product with the exact
  identity.
- **E. GS and RAM state.**
  - The 128x128 target in all three GS dumps (roger-encounter, status-hub,
    panel) is 16,384 texels of exactly 0x00808080, the setup clear. The
    dumps were taken with FBP 0 freshly cleared, so no drawn shadow is
    present to compare.
  - 37 GS register values of the setup, (2,9), (2,6), clamp and silhouette
    state blocks are decoded from RAM and checked.
- **Routing.** `0015C160` is executed over playable_ee.bin with
  `D_008102B1` in {0,1,2}, `D_00810771` in {0,1,2,0xFF} and `+0x214` in
  {0,1,0x80000000}; `001DA6A0`, `0015BF90` and the `+0x4C` method are
  recorded and skipped. All 36 cases name the same callee and order as the
  native route, and every recorded a0 is the player 0x8102B0.
- **Unit test.** `tests/shadow_original_test.c` (ASan/UBSan) covers
  routing (including the latched 0015BF90 fault), the worker order,
  box/VP/UV geometry, alpha 39 at 16 below the light, clamps, the area and variant switch, the kinds, early returns,
  every NULL and failing worker, a missing node, the grid bounds and the
  overflow.
- **F. GS side** (`em_shadow_gs.h`, the Metal backend). For each of the
  seven captures:
  - The executed chain is replayed as the DMAC/VIF1/GIF would, from the
    head of its display list (an END tag written at the chain's end in a
    copy); the registers in force at all 374 draws of the chains and 61
    clip-kernel kicks equal the header's state (table above), and
    `em_shadow_gs_unsupported` accepts each.
  - Every program the chain CALLs runs as original VU1 instructions on one
    persistent VU1: the kernel packet's MPG blocks are loaded into micro
    memory and its STCYCL/BASE/OFFSET applied, then MSCAL and MSCNT run.
    Model: in-order issue, a VF operand still in the 4-cycle pipeline
    stalls, MAC/status/clip flags visible 4 cycles after the op, Q 7
    cycles after DIV, a pair's lower op reads before its upper op writes,
    XGKICK snapshots its packet. The box, silhouette and receiver kernels:
    448 box, 4,704 silhouette and 6,368 receiver vertices. Every kicked
    XYZF2/XYZ2 word, ST and RGBAQ equals `em_shadow_gs_level_batch`,
    `_object_batch` and `_receiver_batch`, including every ADC bit and
    reason (84 box triangles culled, 84 drawn, none clipped; 66 receiver
    vertices left to 0023E8A0). The one exception is three words of
    guard-band-clipped vertices whose screen value overflows ftoi4: the
    header saturates, the interpreter wraps, and the hardware result is
    not established; those vertices are never drawn by the kernel. The
    earlier receiver replay (C) runs only the MSCAL batches (108); this one
    also runs the MSCNT batches (199).
  - The clip kernels 00239C90 (box) and 0023E8A0 (receivers) run on every
    batch the chains give them (61 in the captures). Each clip batch holds
    the same 32 vertex qwords, dmem 0..3 matrix and template rows as the
    kernel batch before it, and the triangles its loop sends to the
    clipping code (micro 0x070 / 0x06E reached, vertex i = 32 - vi11) are
    exactly `em_shadow_gs_needs_clip` of that batch. Every other kick is
    the empty packet at dmem 1019 (NLOOP 0). Class 0/1 receivers of the
    captures have no triangle for a clip kernel. The captures have no such
    triangle at a strip's first two vertices, so the first clip batch of
    each kernel is run again with vertices 0 and 1 moved outside the guard
    band: the loop starts at i = 2 as the header says, and the translation
    kicks what the interpreter kicks there too. Each clip batch then goes
    through the translation (G).
  - The template rows dmem 1021..1023 of every batch (17,280 bytes) and
    001C7420's bone rows (5,376 bytes, via `em_shadow_gs_bone`).
  - `assets/player_shadow.emdl`'s 394 triangles equal the 394 triangles
    the silhouette kernel kicks, as (bone slot, position) sets, in the four
    exact captures.
  - Metal: `em_gfx_shadow_silhouette` in a headless window renders each
    exact capture's target; all 65,536 texels are one of the two original
    values and equal the kernel's kicked triangles rasterized with the GS
    sample points and the top-left rule: 0 differing texels (1,510,
    1,427, 1,512 and 1,276 covered). The receiver pipeline over a
    (91,106,106) frame gives the `em_shadow_gs_receiver_pixel` result for
    A = 36, 90, 200, once and twice (A 36: (52,58,58), As 71, the second
    draw fails the destination-alpha test). The clip-kernel draws: G.
  - Quick mode samples 256 of the 6,144 area keys (every listed key, its
    neighbours and both ends); `EM_TEST_FULL=1` runs all. Captures, route
    beats and synthetic clip batches run in one pool of forked workers.
- **G. Clip kernels, translated** (`src/game/em_vu1_shadow_clip.h`).
  - **Kick for kick.** On every clip batch the test executes (the seven
    captures and all 15 route beats: 256 batches, 453 kicks, 79 drawing
    packets, 34,400 packet bytes, 513 GS vertices) the translation runs over
    a copy of the batch's data memory and must kick what the interpreter
    kicks: the same dmem address, in the same order, every packet byte
    (tags, the vertex-i qword, every ST / RGBAQ / XYZF2 qword), and reach
    the same clip entries.
  - **Route beats.** For each of the 15 beats of FIRST_LEVEL_ROUTE.md the
    native 001DA6A0 over the beat's RAM draws without a fault; 001CB590 +
    001DA6A0 executed over the same RAM build the chain whose box uploads,
    receiver objects, clip classes and worker order equal the plan's; the
    box, silhouette and receiver kernels match the header (kernel_checks),
    then every clip batch as above. Per beat (clip batches / drawing
    packets / clip vertices): 00 2/0/0, 01 2/0/0, 02 21/2/15, 03 2/0/0,
    04 8/5/24, 05 6/0/0, 06 26/0/0, 07 48/18/123, 08 2/0/0, 09 36/8/45,
    10 13/0/0, 11 6/2/12, 12 3/0/0, 13 4/4/30, 14 16/13/81. No captured or
    route box needs 00239C90 to draw (`gs_box_clip` 0).
  - **Synthetic batches** (quick: 2 per style and kernel = 32; full: 192)
    built in GS space and solved back through the playable camera / box
    matrix, with random qwords 0..2: near (w around 0.1), wide (far outside
    the guard band), mixed, behind (w in (0, 0.1) with the camera's z
    column zeroed; with the real camera every such triangle is already
    rejected by the +z guard plane), huge (more than 9 triangles), far,
    negated z column (-z reject) and FTOI overflow. The interpreter runs
    with every micro address traced: both outcomes of all 53 conditional
    branches of each program are reached except the taken side of R 0x2E0
    / B 0x2E6, the triangle cap inside the x planes' one-vertex-out case,
    which no input reaches (the w plane leaves at most 2 triangles and
    each plane at most doubles them, so the x planes end with at most 8).
    Full run: 4,521 kicks and 1,415 drawing packets equal; 12 batches
    whose FTOI saw a result outside int32 fault the translation exactly
    at that kick (the kicks before it equal).
  - **Backend image.** For every real clip batch the backend's own data
    memory (`em_shadow_gs_clip_dmem`: the plan's matrices, the template,
    em_gfx_fog's fog row, qword 3 only) gives the same GS vertices
    (`em_shadow_gs_clip_vertices`) as the original data memory: all
    fields for receivers, X, Y, Z and the kernel's Q for the box. dmem
    1017..1020 equal the ELF's D_00251750 / D_00251550 +0x10..+0x40 in
    every batch, dmem 0..3 the plan's camera / clip-pass matrix, dmem
    4..7 (receivers) ctx+0x24B0. Every vertex unprojects
    (`em_shadow_gs_clip_unproject`) to a point that projects back onto it
    within 1/16 pixel.
  - **Receiver asset.** `assets/scene_snow/shadow_receivers.emsr` loaded
    by `em_shadow_receivers_load` in all 22 captures and beats: grid and
    ctx+0x144..+0x164 equal RAM; the native 001DA6A0 with the asset's grid
    and `em_shadow_receivers_bounds` gives the same receivers, classes and
    clip bits; every receiver and box batch the chain uploads (819) equals
    the asset's 128 qwords. Skipped (counted) when the asset is missing.
  - **Metal.** A strip whose second triangle has a vertex at GS X 7168
    (outside +x only): a class-2 receiver shadows NDC (0.5, 0.5), which
    lies only in that triangle, with `em_shadow_gs_receiver_pixel`'s value;
    class 0 leaves it; after `em_gfx_shadow_box` over the same strip (alpha
    128) a receiver over the whole frame passes DATE there, and without the
    box it does not.
  - Default run 8.2 s wall measured with the machine at load ~10 (30 s CPU in 8 workers);
    `EM_TEST_FULL=1` 23 s.
- **Capture metric** (`python3 tools/test_shadow_original_reference.py
  --capture BEAT`, not in the default run). It renders the beat's frame
  headless (background, the six zone meshes with the area fog, then the
  chain through `em_gfx_shadow_*` with the native module's plan over the
  beat's RAM; box and receiver strips are the original objects' vertex
  lists from the chain executed over that RAM, each receiver object with
  its class) and writes
  `build/captures/shadow/native_{noshadow,shadow,player}.bmp`, then
  compares the shadow region (pixels the shadow changed, the player's
  excluded) with the beat's `original.png`. It fails on any worker fault
  and outside `capture_bounds`: IoU with the original's dark pixels >=
  0.80, shadowed/lit luminance ratio within 0.05 of the original's.

  With the clip kernels translated, no beat faults (all 15). In bounds:

  | Beat | shadow px | IoU | ratio native / original |
  |---|---|---|---|
  | 01_battery | 847 | 0.841 | 0.601 / 0.600 |
  | 06_hill_slide | 988 | 0.853 | 0.590 / 0.602 |
  | 08_truck_crossing | 785 | 0.837 | 0.585 / 0.590 |
  | 12_crevice_jump | 805 | 0.861 | 0.605 / 0.593 |

  Outside the bounds (the harness's frame, not a fault): 00 and 03
  (original region black), 02 (0 shadow pixels: the player stands on the
  elevator, which the harness does not draw), 04 (IoU 0.555), 05 (0.195:
  the shadow falls on the crates, not drawn), 07 (0.242), 09 (IoU 0.832,
  ratio 0.602 vs 0.543), 10 (0.700, 0.667 vs 0.496), 11 (0.441), 13
  (0.291), 14 (0.272, original ratio 0.129: a cutscene frame). The
  harness draws only the background, the six zone meshes with fog and the
  shadow: no level lighting, props, elevator, crates or actors, so where
  the original's shadow lies on those or the lit colour differs, the
  metric cannot pass. This is the harness's rendering, not the live port:
  the port draws no shadow until the binding lands.

## GS side (native)

`src/gfx/metal/em_shadow_gs.h` holds the GS state of every draw of the
chain and the three kernels' per-vertex arithmetic; the Metal backend
(`em_gfx_shadow_*` in `src/em_gfx.h`, `src/gfx/metal/em_gfx_metal.m`)
draws with it. What the captured chains carry (checked in all seven
captures, section F below):

| Draw | GIF PRIM / REGS | GS state |
|---|---|---|
| alpha clear | DIRECT 0x044, RGBAQ + 4 XYZF2 | frame env, ZBUF ZMSK 1, TEST 0x51001, ALPHA 0x80000000A9 |
| box (x2) | template D_008166C0: 0x044, XYZF2, NLOOP 32 | as above; RGBAQ per call (A+D) |
| target clear | A+D sprite (PRIM 6) | FRAME 0x2012C, ZBUF 0x102000010, XYOFFSET 1984, SCISSOR 0..127, TEST 0x30000, RGBAQ (128,128,128,0) |
| silhouette | template D_008168C0: 0x004, XYZ2, NLOOP 32 | target FRAME/XYOFFSET/SCISSOR, ZBUF 0x101000070, TEST 0x3000D, RGBAQ 0xFFFFFF80 |
| receivers | template D_008169C0: 0x07C, ST + RGBAQ + XYZF2, NLOOP 32 | frame env, ZBUF ZMSK 1, TEST 0x5C00D, ALPHA 0x44, TEX0 0x5DC00A580, TEX1 0x60, CLAMP 5; COLCLAMP 1, DTHE 0, PABE 0, FBA 0; FOGCOL 0x303030 |

"Frame env" is the list's D_008143D0: FBW 8 PSMCT32 with FBP 0 or 0x38
(the two buffers alternate) and XYOFFSET (1792, 1936) plus the field's
half line on alternate fields. Every main-frame draw of a chain uses
that one environment.

Kernels (MPG at 0x2371B0, 0x23C780, 0x23C230):

- **00237180** (box): clip = p x (W x V) x P, XYZ = ftoi4(clip / w),
  F from the template fog row. It **culls**: ADC is set on a vertex when
  its data word has bit 15, when any of the last three vertices is
  outside the guard band (the clip kernel 00239C90 takes those), or when
  (e_i.x * e_{i-1}.y - e_{i-1}.x * e_i.y) * w_i is negative (e = screen
  edge, w_i = the data word as a float, the strip's winding sign;
  msubbcy, mulbcw and `fsand` 0x2 at 0x237378). The models 0x14 and 0x15
  are ±5 cubes of six 4-vertex strips; 0x15 has the opposite winding, so
  0x14 draws the three faces that face the camera (alpha 128) and 0x15 the
  three that face away (alpha 1). Every capture draws 12 of the 24
  triangles per model pair.
- **0023C750 / 0023C780** (silhouette): the bone matrix comes from the
  dmem address in the data word's low 10 bits (the other bits are flags);
  ADC from bit 15 and the guard band only. It never culls.
- **0023C200** (receivers): ADC from data-word bits 15 and 13 (vi12 =
  0xA000) and the guard band; no cull. 5,999 of the 6,368 receiver
  vertices in the captures carry bit 13 or 15, so only a few triangles
  of each receiver object are drawn.

- **00239C90 / 0023E8A0** (clip kernels, 1,183 and 1,235 instructions in
  five MPG blocks each, micro 0x000.., BASE 0x190 OFFSET 0x101), translated
  in `src/game/em_vu1_shadow_clip.h` (header-only; its comments cite every
  micro address). 001DA310 runs 00239C90 after every box (`001D4FB0` then
  `001D4C20`); 001D5C80 runs 0023E8A0 only for a class-2 receiver
  (`001D4FB0`, `001D1F80(0,2,6)`, `001D4B50`, `001D4CD0`). Each gets the
  same batch and matrix as the kernel before it and repeats its transform,
  guard rows and CLIP per vertex. The loop sends a triangle (strip vertices
  i-2..i) on to the clipping code only when vertex i has no data ADC
  (0x8000 box, 0xA000 receivers), the CLIP history is non-zero (`fcand`
  0x03FFFF), the three vertices are not all outside one guard plane (six
  `fcor` tests) and i >= 2: exactly the triangles the first kernel left
  undrawn for CLIP, minus the rejected ones (`em_shadow_gs_needs_clip`).
  The clipping code, per such triangle:
  - kicks the empty packet at dmem 1019, writes a packet header at 1185 -
    TOP (dmem 1018, vertex i's qword 0, dmem 1017), and runs vertices i-2,
    i-1, i through its vertex routine with the matrix at each data word's
    address (receivers: camera and the ST matrix after it; the ST, the
    clamped alpha and the projected position; box: the position, qword 1
    as ST with z = 1, qword 2 x 128 as RGBAQ);
  - clips against w = 0.1 in clip space from the ORIGINAL vertices (all
    three behind: the entry kicks nothing; one behind: a second triangle),
    rejects a back face (screen cross product x the float value of vertex
    i's data word < 0: nothing kicked), then against X = 4088, X = 4,
    Y = 4088, Y = 4 in screen space (one vertex out splits, two out move,
    three out collapse onto (2048, 2048); more than 9 triangles: nothing
    kicked);
  - converts (fog F = max(min(A + B w, 255), 0), ftoi4 of X, Y, Z, F; the
    box first clamps Z to 8388607; ftoi0 of the RGBAQ slot) and kicks one
    triangle list: [1018 tag (receivers REGS NOP, box REGS TEX0_1) + vertex
    i's qword 0, 1017 tag with NLOOP 3n | EOP (receivers PRIM 0x07B, ST
    RGBAQ XYZF2; box PRIM 0x043, NOP NOP XYZF2), 3n vertices]. The box
    packet therefore also writes TEX0_1 with the vertex's qword 0; no
    textured draw follows before 001D4CD0 sets TEX0 for the receivers.
  The loop then goes on with the matrix the entry left in vf28..31
  (vertex i's, or dmem 0..3 after a w-plane case). The screen-space
  interpolation runs in the VU's truncated binary32: a vertex very far
  outside (GS X in the 10^8 range) lands a few pixels past the plane, and
  the GS's 16-bit X then wraps (seen only in a synthetic probe, drawn as
  kicked).

The template rows dmem 1021..1023 are (255, 2048, A, B) with AREA11's fog
coefficients (em_fog_gs_coefficients(-209, 304), bit-exact) and the guard
rows (x / 2040 - (256/255) w: the guard band is GS 8..4088).

Backend (`em_gfx_shadow_*`):

- **alpha clear / box**: an alpha-only colour write (RGB mask off), depth
  test GEQUAL (the port's LessEqual) without write. The box triangles are
  the ones 00237180 kicks, decided on the CPU with the kernel's own
  arithmetic from the model's strips and `clip`, then drawn at W x p
  through the frame's native viewproj. Then 00239C90 over the same
  batches (below); none of the captured or route boxes kicks a triangle
  from it.
- **silhouette**: 001C7420's bone rows (node+0x90 x VP) and the kernel's
  XYZ2 are computed on the CPU bit for bit; the 128x128 RGBA8 target is
  cleared to (128,128,128,0) and the triangles are drawn at the GS 12.4
  positions, mapped so that Metal pixel centres are the GS sample points.
  Each silhouette has its own target, rendered by a command buffer that
  is committed at once, so it runs before the frame buffer samples it.
- **receivers**: per vertex, (u, v) = (S/Q, T/Q), the RGBAQ A and F come
  from 0023C200's arithmetic on the CPU; the position goes through
  `v_skin` with an identity palette, the same vertex function and matrix
  the level zones use, so the level's own depth passes GEQUAL. The
  fragment function reproduces the GS pixel pipeline with framebuffer
  fetch: 4-bit-weight bilinear of the target alpha (texel grid 1/16,
  half-texel offset, CLAMP), MODULATE (Cf = 0, Af = At * A >> 7), fog
  (255 - F) * FOGCOL >> 8, alpha test As > 0, destination alpha bit 7,
  ALPHA 0x44 with COLCLAMP, written alpha As. A second receiver on the
  same pixel therefore passes only where the first wrote As >= 128, as
  on the GS. The per-pixel A and F are an APPROXIMATION: floor(x * 128 +
  0.001) of Metal's screen-linear float interpolation. The GS's own
  Gouraud stepping is not modelled, the 0.001 is a heuristic, and no GS
  dump of a drawn shadow checks it (open items).
- **receiver classes**: `em_gfx_shadow_receiver(gfx, strips, cls)`. The
  triangles 0023C200 leaves undrawn for CLIP are drawn by nobody for
  class 0/1 (skipped) and by 0023E8A0 for class 2.
- **clip kernels in the backend** (box and class-2 receivers): per batch,
  `em_shadow_gs_clip_dmem` builds the data memory from what the backend
  has (camera / clip-pass matrix, ctx+0x24B0, the template, the fog row,
  qword 3 of each vertex; qwords 0..2 zero: the test shows the GS
  vertices do not depend on them), `em_vu1_shadow_clip_run` runs the
  translation at TOP 0x190 and `em_shadow_gs_clip_vertices` decodes its
  kicks as the GS takes them (PACKED ST / RGBAQ / XYZF2, triangle list,
  NOP and TEX0_1 skipped). Each vertex is drawn from the point
  `em_shadow_gs_clip_unproject` gives: the solution of the camera's x, y
  and w columns for the GS pixel (X, Y) at w = 1/Q (the kernel's Q, which
  both kernels leave in the vertex's first slot), within 1/16 pixel. So the
  clipped triangles go through the same native view-projection as the
  rest and meet the level's depth as they do; their per-vertex A, F, S/Q,
  T/Q are the kernel's. APPROXIMATION: the GS rasterizes the kicked
  12.4 / 24-bit words; the port rasterizes the unprojected binary32
  points with its own projection and depth. Coplanar depth ties along the
  clipped triangles are therefore not the GS's (not checked against a GS
  dump). The clipped triangles are appended after the object's (box's)
  own, as the GS draws them. -1 only when the translation faults (an FTOI
  outside int32), a data word names a matrix other than dmem 0, a packet
  does not decode, or a point cannot be unprojected.
- The frame's alpha channel is the GS destination alpha; the CAMetalLayer
  is set opaque so it is never shown.

### Binding (for the scene coordinator)

**Render-stage call.** The coordinator's worker slot `w_0015C160`
(`em_scene_workers.h`), called by `em_sf_001AE5E0` at 0x1AE654 (after
`walk_001AFD70(0)`, before `w_001F0360`) and by `em_sf_001AE6B0` at
0x1AE798 (after `walk_001AFD70(2)`, before `001CB590(&D_008101E0)`), is
the whole of 0015C160 (byte-matched `src/func_0015C160.c`):

```c
static int w_0015C160(void *ctx)          /* 0015C160, player post-step */
{
    Bind *b = ctx;
    const uint8_t *pl = b->player;        /* D_008102B0 record, 0x320 bytes */
    uint32_t w214;
    if (b->d8102B1 == 0) return 0;        /* no 001CB590, no +0x4C draw */
    /* 001CB590(player, 0x320, player[9]); a3 is not set up (src/func_0015C160.c) */
    if (b->w_001CB590(b, 0x8102B0u, 0x320, pl[9], 0) < 0) return -1;  /* D_00275B44 = player */
    memcpy(&w214, pl + 0x214, 4);
    const int r = em_shadow_original_route_0015C160(b->d8102B1, b->d810771, w214,
                                                    &b->shadow_fault);
    if (r < 0) return -1;                 /* 0015BF90 route: fault, after 001CB590 */
    if (r == 1 &&
        em_shadow_original_001DA6A0(pl, b->player_nodes, b->player_node_slots,
                                    &b->shadow_scene, &b->shadow_state,
                                    &b->shadow_plan, &b->shadow_workers,
                                    &b->shadow_fault) < 0)
        return -1;
    return b->draw_player(b);             /* the +0x4C method (001CAA00) */
}
```

`draw_player` is the player's own draw, moved out of `frame_close_out`
(today the last entry of its draw list): the shadow must come after the
level and the walked actors and before the player. `player_nodes[i]` =
the node record `*(player+0x110+4i)` (>= 0xD0 bytes; +0xC0 is read) for
i < player+0x09. `shadow_state` persists across frames (D_00817FF0, zero at
boot). `shadow_scene`: ctx+0x2240, +0x2340, +0x2380, D_70003AC0
(ctx+0x23C0), D_00810610, ctx+0x2468, D_00810700/701 of the frame, and the
grid fields from `em_shadow_receivers_scene(&receivers, &shadow_scene)`.

**Workers** (`ctx` = the coordinator's binding; `gfx` the frame device;
`vp` the frame's native P*V, the matrix the zones are drawn with;
`receivers` = `em_shadow_receivers_load(&receivers,
"assets/scene_snow/shadow_receivers.emsr")` once per area):

```c
w_object_bounds(ctx, id, lo, hi) -> em_shadow_receivers_bounds(&receivers, id, lo, hi)
w_alpha_clear(ctx)               -> em_gfx_shadow_alpha_clear(gfx)
w_box(ctx, box)                  -> m = em_shadow_receivers_box(&receivers, box->model);
                                    EmGfxShadowStrips st = { m->qw3, 32 * m->batches };
                                    em_gfx_shadow_box(gfx, &st, box->world, box->clip_pass,
                                                      box->rgbaq, vp)
w_silhouette(ctx, kind, sil_vp)  -> kind 0x28 only (else -1):
                                    em_gfx_shadow_silhouette(gfx, proxy->verts, proxy->vert_count,
                                        proxy->indices, proxy->index_count,
                                        nodes /* node+0x90 of each player+0x110 node */,
                                        player[0x0C] /* node count */, sil_vp)
w_receiver_begin(ctx, uv)        -> em_gfx_shadow_receiver_begin(gfx, uv, shadow_scene.camera_3AC0, vp)
w_receiver(ctx, r)               -> o = em_shadow_receivers_object(&receivers, r->id);
                                    EmGfxShadowStrips st = { o->qw3, 32 * o->batches };
                                    em_gfx_shadow_receiver(gfx, &st, r->cls)
w_receiver_end(ctx)              -> em_gfx_shadow_receiver_end(gfx)
```

A NULL object or box model is -1 (fault). The receiver call covers the
whole original sequence for its class (class 2: 0023C200, then 0023E8A0
over the same strips) and `em_gfx_shadow_box` stands for 00237180 and
00239C90 together; the worker passes `r->cls` unchanged. Every call
returns -1 when it cannot draw what the original draws; the worker returns
that and em_shadow_original latches the fault. `proxy` is
`assets/player_shadow.emdl` (kind 0x28). `em_gfx_fog` must be set for the
frame before the receivers (the world flush does). Only the Metal backend
implements `em_gfx_shadow_*`.

## Assets

**Receivers and boxes.** `../Extermination/tools/export_shadow_receivers.py
--out ../extermination-port/assets/scene_snow/shadow_receivers.emsr
[--verify-ram <eeMemory.bin>]...` reads the user's extracted disc:

- the static-object bank *D_0028A5A0 = resource 0x44 (001FF830 state 7:
  D_0028A490[0x44]) lies 0x123000 bytes into `extract/chunk15/f12_id44.bin`;
  chunk15's files load contiguously and the bank's objects run on into
  f13..f17, so the bank is read from f12..f17 concatenated;
- object 0 is the grid block 001D52E0 publishes: words 0/1 = ctx+0x144 /
  +0x148 (32 x 32), floats +0x08..+0x1C = ctx+0x150..+0x164, ids from
  +0x20 = ctx+0x140 (4 per cell);
- objects 1..701 (001C6120: table + (word[1 + id] >> 2 << 2)): the AABB
  +0x14 / +0x24 and the blocks at +0x40 (001D4F30: word 0 = count, 0x82
  qwords each: STCYCL 4,4 + UNPACK V4-32 of 128 qwords + MSCAL / MSCNT,
  checked per block); 1,389 batches;
- the chunk27 library *D_0028A56C = `extract/chunk27/f01_id37.bin`, models
  0x14 and 0x15 in the same record form.

`--verify-ram` checks D_0028A5A0 = f12's RAM address + 0x123000, that the
RAM bank equals the files byte for byte, the grid, every object block and
both box models (all 15 route beats and playable_ee.bin pass). Output
format "EMSR" v1 (header in the tool); loader `em_shadow_receivers_load`.
The 0x123000 placement is taken from the RAM pointer, as the opening-actor
exporter takes its 0xD0800: the chunk descriptor's pointer table that
001FF830 relocates is not in the extracted files.

**Shadow proxy.** `../Extermination/tools/export_shadow_proxy.py` exports
`D_0028A490[kind]` to an untextured EMDL. The table slot equals the
chunk03 file id. For the player that is `extract/chunk03/f32_id28.bin`:
485 vertices, 394 triangles, 21 nodes.

The tool mirrors player.emdl's clip table and checks that the palette
layout matches. `--verify-ram` content-matches the file against
`D_0028A490[kind]` in a capture and finds the captured silhouette REF.

Output: `assets/player_shadow.emdl` (ignored). 3,073 of 3,909 baked frames
equal the port's current player.emdl. The 836 that differ are exactly
eight whole clips (0, 64, 65, 66, 67, 69, 71, 348). Re-baking the player
mesh `extract/chunk28/f00_id3b.bin` with today's `export_native.py`, the
same anim and the same 57 clip ids (with or without `--attach --no-glow
--gsdump`) gives a palette equal to the proxy's in all 3,909 frames and to
player.emdl's in the same 3,073. So the difference lies in how the current
player.emdl was produced for those eight clips, not in the proxy; what
produced it is not established. It does not affect the shadow: the
original skins the proxy with the actor's world-space node matrices
(node+0x90 of the `+0x110` nodes, with actor `+0x80..+0x88` zeroed for the
`001C7420` call), so the port must pass the player's live palette, never a
baked frame.

## Corrections to earlier labels

- **FINDINGS s7b/s43 "player aura" (models 20/21 additive, green
  pulsing).** At the player these draws are the shadow's destination-alpha
  box: ALPHA 0xA9 keeps Cd and the alpha test fails to FB_ONLY. The claim
  "the engine draws no subtractive shadow primitive under the player" is
  wrong. The shadow is the receiver re-draw.
- **`src/func_001DA6A0.c` (NEARMISS) swaps the two constants.** f20 = 16.0
  is the box size and f21 = 128/16 = 8.0 is the unit. The C has 8 and 16.
  Its "billboards" are the box passes.
- **`src/func_001D9EE0.c` "scaled overlay pass"** is the silhouette pass.
- **`src/func_001BA580.c` (NEARMISS) category map** is off by one for the
  leading code of each group (table under "Other actors"). Only 0x6C and
  0x6A call `001DA6A0` unconditionally; 0x68 is category 9 and needs
  `+0x56 != 0`; the area-0x0D `001BA7F0` route belongs to 0x61, not 0x5E.
- **LEVEL_MATERIALS.md "21 untextured object-kernel kicks per frame with
  TEST 0x3000D (depth ALWAYS) and ZMSK 1, source blob at 0x012D0000, not
  yet identified"** are the silhouette: the proxy mesh
  D_0028A490[0x28] at 0x012D1A00 (2,730 qwords, 21 batches) drawn into
  the 128x128 target.
- **`src/func_001D2960.c` calls the ctx+0x2240..0x2300 variants
  "offscreen/shadow/reflection".** That is unsupported. The shadow uses
  only 0x2240, as the gate's clip matrix.

## Limits and open items

- Roger's drop shadow (census L22): Roger is live on his original owner
  since 2026-09-24 and his 001BA580 reaches `001DA6A0` every frame (kind
  0x29); the port reports it as a no-effect binding (UM_001DA6A0,
  em_scene_bindings.c), as it does the player's post-step. The captured
  runs all return at the clip test, but during the encounter Roger is on
  screen, where the original may draw his shadow: no capture of that frame
  has been checked.
- The GS side is implemented in the Metal backend but not bound (Binding
  above). D3D12/Vulkan do not implement it.
- The clip kernels are translated and kick for kick equal to the executed
  kernels on every batch the captures and route beats give them and on
  the synthetic sweep. The interpreter's timing model (stalls, flag and Q
  latency) is checked through the kernels whose kicks it reproduces; the
  clip kernels read every flag 4 cycles after its producer, so their
  results do not depend on it. The backend draws their triangles from
  unprojected points (GS side, clip kernels in the backend): coplanar
  depth ties along clipped triangles are the port's, not the GS's.
- The clip kernels' DIV-by-zero result and the denormal/Inf operand reads
  (`emvu_div`, `emvu_rd` in em_vu1_shadow_clip.h) mirror the VU interpreter
  but no captured, route or synthetic batch reaches them (a sign mutation of
  the DIV-by-zero result passes the suite). docs/EE_FLOAT_MODEL.md settles
  the VU0 rules; VU1 is assumed to follow them until a batch exercises it.
- em_shadow_gs.h (gfx) includes game/em_vu1_shadow_clip.h, so this one
  header dependency runs gfx -> game; move the header to a neutral place
  when the backends for D3D12/Vulkan need it.
- FTOI outside int32 is not established (the interpreter wraps, the
  em_shadow_gs.h model saturates): the translation faults there instead;
  no captured or route batch reaches it.
- The per-pixel A/F interpolation (floor of Metal's float value plus a
  0.001 epsilon) is an approximation; the GS bilinear 4-bit weights are
  modelled from the GS behaviour. Neither is checked against a GS dump
  of a drawn shadow (none exists).
- ftoi4 outside int32: not established (the header saturates, the
  interpreter wraps); it only affects guard-band vertices that are never
  drawn (3 words in the captures).
- Framebuffer fetch (the destination-alpha test and the GS blend) needs an
  Apple GPU; elsewhere the receivers return -1.
- handoff: the receiver section of the captured list differs from what
  the capture-time RAM produces (see A). The later step that moved the
  pose and view is not identified.
- `0015BF90` (the `+0x214 != 0` route) and `001BA7F0` (area 0x0D NPC
  route) are not translated.
- The suppression list (frame variant not run, `D_008102B1 == 0`,
  `D_00810771 == 1`, kind 0, clip reject, area not listed) is what the code
  shows and what the nine captures agree with. It is not proven complete:
  no capture has `D_008102B1 == 0`, `D_00810771 == 1` or `+0x214 != 0`,
  and the frame-machine states other than selector 0/2/3 and the status
  hub were not traced.
