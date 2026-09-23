# Player drop shadow (original 001DA6A0)

The first-control screenshot (`original_area11_playable.png`) shows a dark
shadow under the player. It has the silhouette of the legs and rifle, not a
round blob. The port draws no shadow. This document records the original
mechanism, the native module that reproduces its EE side
(`src/game/em_shadow_original.{h,c}`), and how it was verified. Addresses
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
| alpha clear | `001DA290`, `001DA1E0` | Full-frame sprite (1792..2304, 1936..2160, Z 0xFFFFFF) in state block (2,9): ALPHA 0xA9 (Cd kept), TEST 0x51001 (alpha test NEVER, fail FB_ONLY), ZTST GEQUAL, ZMSK. The frame's destination alpha becomes 0 and its colour is unchanged. |
| box x2 | `001DA310` | Outward cube (model 0x14, chunk27 library, ±5) with RGBAQ (0,128,0,128), then inward cube (model 0x15) with RGBAQ (128,0,0,1). The RGBAQ bytes come from `00128250` (0x1DA5BC..0x1DA5E8), the soft-float float-to-unsigned conversion: NaN, zero and negatives give 0, values of 2^32 and up give 0xFFFFFFFF, the rest truncate; the bytes are shifted and OR-ed without masking. Both use W = scale(1.6,4,1.6) + (0,-20,0) + anchor. Two kernels run: 00237180, then 00239C90 (clipping). Both receive (W x V) x P. The first uploads of W x `D_70003AC0` and W x 0 are overwritten before any MSCAL. Result: destination-alpha bit 7 marks visible surfaces between the box's front and back faces. |
| silhouette | `001D9EE0` | Packet `D_00817E20`: FRAME FBP 0x12C (GS byte 0x258000), FBW 2, PSMCT32; XYOFFSET 1984; SCISSOR 128x128; TEST ZTST ALWAYS; a sprite clears the target to (128,128,128,0). The VP is `D_70003AC0` = V x diag(8,8,1,1) x [GS rows: centre 2048, Z 0.8996 / 1677721.5]. `001C7420` uploads the actor's 21 world-space node matrices (node+0x90 of the `+0x110` nodes) x VP. Around that call 001D9EE0 saves the actor qword `+0x80`, zeroes `+0x80/+0x84/+0x88` and restores it afterwards (0x1D9FFC..0x1DA024). `001D4740(D_0028A490[kind])` draws the proxy mesh; for the player that is `extract/chunk03/f32_id28.bin`. The template has only XYZ2 per vertex and no ABE. Every pixel is the A+D RGBAQ 0xFFFFFF80 = (128,255,255,255). |
| receivers | `001D5C80(D_00817FB0)` | Level grid (ctx+0x140, stride +0x148, cell +0x150/+0x154, origin +0x158/+0x15C). The cells cover ±15 around the highest node, never fewer than 3x3, clamped to 0..31, z-major, 4 slots each. Each object with id > 0 is clip-tested with its AABB against 1024x448 x `D_00810610` (trivial reject), then against the 4096 guard band. Kernel 0023C200 draws each object; objects outside the guard band are drawn again by 0023E8A0. Before the loop, `001D4CD0` sets state block (2,6): TEST 0x5C00D (alpha > 0, DATE with DATM=1, ZTST GEQUAL), ALPHA 0x44 = (Cs - Cd) x As / 128 + Cd, ZMSK. It also sets CLAMP 5, TEX1 bilinear, and TEX0 0x5DC00A580 (TBP 0x2580, TBW 2, PSMCT32, 128x128, TCC 1, TFX MODULATE). ctx+0x24B0 goes to dmem 8. |

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
route latches `EM_SHADOW_FAULT_UNTRANSLATED` at 0x0015BF90 and returns -1. `em_shadow_original_receiver_vertex` is the kernel's (u, v, 1, a)
slice.

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
    same alpha matrix natively.
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

## Asset

`../Extermination/tools/export_shadow_proxy.py` exports
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
- **`src/func_001D2960.c` calls the ctx+0x2240..0x2300 variants
  "offscreen/shadow/reflection".** That is unsupported. The shadow uses
  only 0x2240, as the gate's clip matrix.

## Limits and open items

- The GS side is not implemented in `em_gfx`: offscreen target, box mask,
  receiver blend. See the binding notes in the lane report.
- The 0023E8A0 clip kernel (guard-band objects) is not executed per
  vertex. Its uploads (UV to dmem 4, camera) are checked.
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
