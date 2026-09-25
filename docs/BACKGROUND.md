# AREA11 level background (R09)

FIRST_LEVEL_AUDIT R09/ORCH-27 asked what the original clears the frame to.
The native first-control view showed pure black wherever no level geometry
covers the screen. The original shows grey (48,48,48) there. This document
records the original mechanism, the evidence for it, and the port's
translation.

## What the original does

**The colour buffer is not cleared in a world frame.**
- 001D2300 builds each frame's main list at `D_0028F700 + slot << 14`:
  - first the draw-env REF (`D_00275674 + slot*0x190 + 0x20`, 25 qwords);
  - then the clear packet REF.
- The clear packet is `D_00275674 + 0x3A0` (8 qwords). It holds one sprite
  over the whole 512x224 field with RGBAQ (0,0,0,0x80) and Z 0.
  - Its TEST_1 is 0x32001: ATE 1, ATST NEVER, AFAIL ZB_ONLY. Every pixel
    fails the alpha test and only Z is written.
  - A TEST_1 of 0x50000 is restored afterwards.
- The variant at `+0x420` writes black colour and Z (TEST_1 0x30000). It is
  used only when render flag 3 is set, and 001D2300 then clears the flag.
  In every AREA11 capture flag 3 is clear.

**The frame is filled by a background draw instead.**
- 001D2300 CALLs render channel 3's list right after the clear, before the
  level. It does so when D_008106C4 is 0, flag 4 is clear and flag 0x20 is
  set. The call goes through 001E0DF0 -> 001D21B0 with the node in
  ctx+0x1D8.
- That list is built every world frame. 001C1D00 (the camera apply in both
  frame variants) calls 001E0CF0, which calls 001E1E60(ctx+0x180, 3)
  under flags 0x20 and 0x21.
- The flags are armed at area load by 001C1F50:
  - AREA11 (key 0x0B00) arms 0x20 and 0x21 and clears 0x22.
  - 001E2260 stores the area's TEX0 `0x20069F0121323200` at ctx+0x1D0.
  - 001E2270 copies the colour D_00250F30 (1.0, 1.0, 1.0, 1.0) to ctx+0x1C0.

**001E1E60 (render channel 3):**
- 001D1F80(3,0,7): env set 0, class 7.
  - TEST_1 0x30003: ATE 1, ATST ALWAYS, ZTE 1, ZTST ALWAYS.
  - ZBUF_1 ZMSK 1: no Z write.
  - TEX1_1 0x60: MMAG and MMIN LINEAR.
  - ALPHA_1 and CLAMP 0.
- 001D1FF0(3,0): CLAMP_1 5, which is CLAMP in S and T.
- 001D6F60: TEXFLUSH, TEX0 from ctx+0x1D0, and TEXA 0x80.
- 001D7080: RGBAQ = int(128 * ctx+0x1C0..0x1CC), which is 0x80808080, with
  Q = 1.0.
- The matrix D_00253570:
  - 00102798 transposes the view copy ctx+0x2380.
  - The row D_00253580..8C is doubled.
  - 001026D0 multiplies by the X/Y swap matrix.
- D_002535B8 = the zoom ctx+0x2468.
- The VU1 uploads (001D7100):
  - the GIF tag D_00253560 at dmem 0, 0x81 and 0x102;
  - D_00253570..EF (8 qwords) at dmem 0x200.
- Then REF kernel packet 0x0023C990 (MPG) and MSCAL 0.
- The sound branch under flag 0x23 is not taken in AREA11.
- 001D2040(3,1) restores TEST_1 0x5000D and ZBUF ZMSK 0.

**Kernel 0x0023C990** runs a 32 x 32 grid over the screen:
- x starts at -256 and steps by 16.516129; y starts at -112 and steps by
  7.2258062 (D_002535B0/C0). x is reloaded at every row, and both steps are
  accumulated additions.
- For each grid point it forms d = x*row0 + y*row1 + zoom*row2 using the
  matrix rows vf28..vf30.
- ERLENG gives p = 1/|d|.
- ST = 0.5 + 0.49 * (d.xy * p) (D_002535D0/E0).
- XYZ2 holds the float bits of (x, y) + 526336.0. The low 16 bits are then
  the GS 12.4 coordinate around 2048, and Z is 0.
- Three output buffers rotate. Each row pair is kicked as one PACKED GIF
  primitive: PRIM 0x1C (triangle strip, Gouraud, textured, no fog, no
  blending), NLOOP 32, and registers ST/XYZ2/ST/XYZ2. That gives 31 strips of
  64 vertices covering the whole field.

**What this means in practice:**
- d.x (S) is the world Y component of each pixel's view ray. d.y (T) is the
  world X component.
- The AREA11 texture is PSMT8, 256x16, at TBP0 0x3200. Its CLUT at CBP
  0x34F8 is CSM1 CT32.
- Every row of texel indices is the ramp 0..255, so the texture is a 1D
  gradient in S:
  - (48,48,48) for S below about 0.53;
  - then a slightly blue ramp up to (143,143,143).
- Rays up to about 3.5 degrees above horizontal still land on the
  (48,48,48) end. The grey in `original_area11_playable.png` (the camera
  looks down at the player) is that end of the gradient.

## Evidence

- **Captured lists.** `tools/test_background_reference.py` walks both
  display lists of six AREA11 captures: playable, opening, handoff,
  roger-encounter, elevator clip47 and elevator completed. It walks them as
  the DMAC, VIF1 and GIF would. In every list:
  - tag 1 is the draw env;
  - tag 2 is the Z-only clear, checked field by field;
  - tag 3 is the CALL of channel 3.

  The GS state at channel 3's single MSCAL equals the exported asset. The
  status-hub capture (D_008106C4 path) has no such CALL.
- **Original PCSX2 experiments** (2026-09-23, save state 04, run through
  `pcsx2_session.py`; the outputs were not kept):
  - Writing FOGCOL (ctx+0xB0) = red for three frames turned every fogged
    level pixel red. The background stayed exactly (48,48,48), so the
    background is not fogged geometry.
  - Patching the 0x3A0 clear to write red colour for three frames left the
    background at (48,48,48). Something is drawn over the clear every
    frame, and that is the channel 3 grid.
- **Texel origin: the disc.** Both the PSMT8 indices at TBP 0x3200 and the
  CSM1 CLUT at CBP 0x34F8 are written by AREA11's level-load GS upload. The
  previous round reported the CLUT as "runtime-synthesised". That was wrong:
  the per-file upload replay stopped each IMAGE payload at its file's end,
  and the contiguous-run search could not find a CLUT stored as a 16x16
  sub-rectangle of a 256-wide image. The loader path, from the decomp:
  - 001FFCD0 state 2 reads INDEX.IDX sector `D_00810700 + 4` (15 for
    AREA11) into D_00289BC0.
  - State 4 calls 001FF590(0xAB, 0), then 001FF590(0xAB, 1).
    - Call 0 loads table entry 0 (`+0x20 {u32 off, u32 size}`, off relative
      to `+0x04`) for 001FB370/001FB3E0, the IOP stream loader. No GS
      write.
    - Call 1 loads each group-A entry `u16[+0x0C] .. + u16[+0x0E] - 1` into
      the buffer D_0028A490[0xAB] and calls 00200830.
  - 00200830 = dmac_channel_base(1) (VIF1), 00102468, then 00101F08
    (TADR = buffer, QWC 0, CHCR DIR | chain mode | STR, TTE 0), then
    00102468 again.
  - State 7 DMAs the `u32[+0x10]` group-B entries from the resident region.
    For sector 15 there are none, and there is no nested block.

  **Sector 15's one group-A section** is region `0x4A800 + 0xD8800`. It is a
  CNT tag (QWC 0x7807) inside the directory's id 0x43 file, then a RET tag
  (QWC 0x6007) inside the id 0x42 file. Each tag carries a VIF DIRECT with
  one host-to-local PSMCT32 transfer:
  - BITBLTBUF DBP 0x2A00, DBW 4, 256x480;
  - BITBLTBUF DBP 0x3180, DBW 4, 256x384, TRXPOS (0,0).

  The second transfer's IMAGE payload (0x60000 bytes) runs from the id 0x42
  file across the whole id 0x46 and id 0x41 files, the id 0x96 file, and
  into the id 0x97 file. The directory's file boundaries are not packet
  boundaries. That payload places:
  - the TBP 0x3200 index ramp: transfer rows 32..63, from the id 0x42
    file;
  - CLUT rows 0-2 (all (48,48,48)): transfer rows 208..210, x 224..239,
    from the id 0x42 file;
  - CLUT rows 3-15 from the id 0x46 file. That file starts at transfer
    pixel (192, 211), so viewed as 256-pixel rows its x = 32..47 column is
    the CLUT.

  Replaying the section from the disc gives texels identical to all three GS
  freezes (opening, roger-encounter, status-hub), and to the texels the
  previous round read from a capture.

## Port

**Asset.**
- Command (decomp root). `--iso` reads the user's disc image in place;
  `--disc DIR` takes a mounted disc, or a directory holding
  `DATA/INDEX.IDX` and `DATA/DATA.DAT`. `--capture-gs` is optional: the
  disc texels must equal that freeze's texture.
  ```
  python3 tools/export_level.py --background ../extermination-port/assets/scene_snow \
      --area 11 --sub 0 --iso Extermination-rebuilt.iso \
      --capture-ee build/startup-reference/roger-encounter/eeMemory.bin \
      --capture-gs build/startup-reference/roger-encounter/gs.bin
  ```
- It writes `assets/scene_snow/background.embg` and the manifest line
  `background background.embg`.
- It checks that the capture is AREA11 with flags 0x20 and 0x21 armed. It
  replays channel 3's list and takes the GS state at the MSCAL, then
  requires the following:
  - TEX0 = ctx+0x1D0;
  - RGBAQ = int(128 * ctx+0x1C0);
  - the D_00253560 template at dmem 0, 0x81 and 0x102;
  - the uploaded constants = the ELF's D_002535B0..EF;
  - TEST_1 ZTE 1 / ZTST ALWAYS, no alpha or destination-alpha test that can
    drop a pixel, and ZBUF_1 ZMSK 1. Other values exit with the reason.
- It then replays the area's level-load upload from the disc (see Evidence)
  and decodes the PSMT8 texels through the CSM1 CLUT.
  - The replay refuses anything it does not model: DMA tags that carry an
    address, VIF codes other than NOP/FLUSH/DIRECT, GIF data other than A+D
    writes to TEXFLUSH/BITBLTBUF/TRXPOS/TRXREG/TRXDIR, and transfers that
    are not host-to-local PSMCT32.
- The EE capture is still needed for the draw-state checks (TEX0, RGBAQ,
  the channel-3 list). These values are ELF constants that 001C1F50,
  001E2260 and 001E2270 store, and the capture is how they are checked; the
  texels no longer come from it.
- Layout (104-byte header, version 2, then RGBA8): see
  `src/gfx/metal/em_background_gs.h`. The header now stores TEST_1, ZBUF_1
  and a texel-source word (1 = disc replay).

**Translation.**
- `src/gfx/metal/em_background_gs.h` holds the whole translation:
  - 001E1E60's matrix, including the literal swap multiply, which keeps
    001026D0's signs of zero;
  - kernel 0x0023C990's grid.
- Every product and sum uses binary32 truncation, as the VU does.
- The Metal backend (`em_gfx_background_load/_draw/_unload/_ready`) draws
  the 31 strips as triangle strips with this state:
  - depth test and write off;
  - opaque;
  - linear clamp-to-edge sampling;
  - fragment = texel * RGBAQ/128 with alpha = RGBAQ A (TFX MODULATE, TCC 0).
- The backend refuses an asset it does not reproduce and prints the field;
  nothing is drawn. It checks:
  - PRIM, TEX0 TFX/TCC and size, TEX1 and CLAMP_1;
  - TEST_1: ZTE/ZTST ALWAYS, a pixel-dropping alpha test, DATE;
  - ZBUF_1 ZMSK;
  - the texel-source word.
- **GS to NDC** (`em_background_gs_ndc`): `ndc = ((X - 2048) / 256,
  -(Y - 2048) / 112)`. This is the port's world-projection convention
  (em_math.h: `x_gs = 0.8s*x/z + 2048`, `ndc_x = (x_gs - 2048)/256`), so
  the background lines up with the level drawn over it.
  - GS field pixel i's footprint [1792 + i, 1793 + i) becomes the i-th
    1/512 of the viewport.
  - At the GS's own resolution this samples half a pixel right of and below
    the GS sample point (X = 1792 + i). On this gradient that is a sub-texel
    ST shift.
  - The sample-point alternative, a +0.5 offset, was tried and rejected.
    The kernel's grid starts exactly on the field's left and top edges, so
    under upscaling that offset leaves a half-pixel strip uncovered: 192 of
    6,912 sky-box samples and 417 whole-frame samples were black.
- **View input.** The draw takes the port's native view and negates rows 1
  and 2 to get ctx+0x2380. The native view is em_cs_view_to_native of the
  original look-at 00102CD0 (the commit 0018C0D0 builds it), which is
  exactly the original with those rows negated
  (`tools/test_census_standins_reference.py`).
  The background test does not re-prove it.

**Wiring (live since the render + UI step, 2026-09-25).**
- `scene_manifest_load` and `scene_unload` (em_scene.c) call
  `em_gfx_background_unload`. A `background <file>` line calls
  `em_gfx_background_load(gfx, "<scene_dir>/<file>")`; a failure is a
  required-asset fault (the game quits, naming STARTUP.md step 40). The line
  stands in for render flag 0x20, which 001C1F50 arms for key 0x0B00: only
  AREA11's manifest has it.
- The world branch of `frame_close_out` (em_render_frame.c) calls
  `em_gfx_background_draw(gfx, g.cam.view, zoom)` before the fog and every
  other draw, when `D_008106C4 == 0` (the canonical request byte
  `req[EM_SCENE_REQ_C4]`) and no movie played in the frame
  (`em_frame_movie_active()`, the D_00821058 == 1 mirror: 001D1C10 sets
  flag 4 only in such a frame, and 001D1C50 clears it at the next head).
- The status-screen scenes (the hub, the request pages, the UI scene) do not
  draw it: the original hub frame has no background CALL.

## Verification

- `python3 tools/test_background_reference.py`: about 1.2 s. It covers:
  - 6 captures and 12 Z-only clears;
  - the asset's TEST_1/ZBUF_1 equal to channel 3's state in every capture;
  - the native matrix equal to the uploaded D_00253570 bit for bit in all
    six;
  - the original kernel executed over each captured upload, giving 11,904
    vertices whose ST and XY equal the native grid bit for bit, with the
    grid spanning X 1792..2303.9375 and Y 1936..2159.9375;
  - a quick sweep of 2 synthetic views (EM_TEST_FULL=1: 64 views, 126,976
    vertices, about 2 s);
  - the disc replay, run with the decomp exporter's own code (`--iso`,
    default `<decomp>/Extermination-rebuilt.iso`, or `--disc`): 2
    transfers, texels equal to the asset and to three GS freezes (opening,
    roger-encounter, status-hub);
  - the GS-footprint NDC mapping for every field-pixel edge;
  - refusal of FGE, ZTST GREATER, ATST GEQUAL, DATE, ZMSK 0 and
    capture-sourced texels.
- The test has been mutation-checked:
  - dropping the row doubling fails the matrix check;
  - an ST offset fails at strip 0, vertex 0;
  - a 1/16-pixel XY shift also fails at strip 0, vertex 0;
  - one flipped texel bit fails the disc-replay comparison;
  - an asset with ZTST GREATER is refused;
  - a +0.5 NDC offset fails the footprint check.
- **Sky metric (the same test with `--native <bmp> --original <png>`).**
  - Original region: mean (48.0, 48.0, 48.0).
  - Native first control, background box, before: 6,843 of 6,912 samples
    black (mean 0.2). After (wiring applied in a scratch tree, disc-sourced
    v2 asset): 0 black, mean (48.2, 48.2, 48.2).
  - Local pair (ignored, overwritten per the screenshot policy):
    `build/captures/background/before.png` and `after.png`.
  - Whole frame (1920x1440 capture): 24,297 black samples before, 0 after.
    Per pixel, all 96,944 formerly black pixels are now exactly (48,48,48).
    Of the pixels that were not black, 687 changed; these are additive snow
    sprites and cut-out edges over the new background.
- Fog and level materials are unchanged. The draw runs before them, and
  neither the fog nor the material state is touched.

## Open

- The draw-state checks still read an EE capture taken inside AREA11:
  TEX0 at ctx+0x1D0, RGBAQ from ctx+0x1C0, and the channel-3 list. These
  are ELF constants from 001C1F50, 001E2260/D_00250F30 and 001D1F80. A
  capture-free exporter would translate 001C1F50's per-area TEX0 table
  directly. The texels are disc-only.
- The disc replay models only what sector 15 contains: CNT/RET/END chains,
  VIF NOP/FLUSH/DIRECT, and A+D transfer registers with PSMCT32 IMAGE data.
  Anything else fails with a message. GS writes made after the area load
  (other chunks, render targets) are not modelled. The three GS freezes
  show the TBP 0x3200 and CBP 0x34F8 blocks intact in AREA11.
- ERLENG is modelled as 1/sqrt in double, truncated to binary32. The test
  applies the same model on both sides. The hardware EFU result is not
  verified bit for bit, because no capture holds the kernel's output.
- **The movie frame.** 001D2300 skips the draw in the frame where 00203350
  played (flag 4). The port's movie frames do not reach frame_close_out's
  world branch, and the gate reads the movie mirror for the frame that
  starts one; no AREA11 world frame with a movie has been captured.
