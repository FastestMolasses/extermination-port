# PCSX2 2.8 release post: leads for the port

Read 2026-10-09 by the port-side blog researcher (user request of
2026-10-09: "If theres things we can learn from this to further improve and
maximize the faithfulness of this port, then lets research it and document
it and includes it in our roadmap"). Source: the PCSX2 project's release
post for 2.8.0, https://pcsx2.net/blog/2026/pcsx2-2.8/ (about 287,000
characters, read in full: the introduction, "New Features", "Performance
and Graphics Improvements" with all 41 of its sections, and the five
footnotes).

**What this file is.** Every statement in the post about PS2 hardware or
about the problems of translating it to PC hardware, sorted by subsystem,
with what it means for the port: whether the first level uses the feature,
whether the port models it, how to confirm it before anything changes, and
a priority. **Every item is a lead, not evidence** ("a label is not
evidence"): the post describes PCSX2's changes in prose; nothing here is
confirmed for the PS2 until an original-instruction oracle, a PCSX2
software-renderer capture or public hardware documentation says so.

**Clean room.** Only the post's prose was read. The post links a GitHub pull
request for almost every section; those pages are emulator source diffs.
**Port-side agents must not open them**, nor any emulator source (the port's
clean-room rule, GS_EXACT.md "Clean room"). The claims are paraphrased here,
not quoted, and nothing from the post's code or images is reproduced.
Section headings are cited as the post names them, so a reader can find the
passage.

**Priorities.**
- **P1**: bears on the Original profile's first level or on its reference;
  listed in FIRST_LEVEL_AUDIT.md 1b, block F.
- **P2**: bears on the Enhanced profile, the future D3D12 / Vulkan
  backends, or levels after the first; listed in PORT_PROFILES.md "Queued
  work" item 5.
- **P3**: corroboration of something already measured, or no counterpart in
  the port (recorded so nobody re-reads the post for it).

## 0. The headline: what the post changes, and what it does not

1. **Almost all of the post is about PCSX2's hardware (GPU) renderers.** Of
   the 41 sections under "Performance and Graphics Improvements" (40 GS
   or graphics-API sections and one IPU section), one names the software
   renderer: "GS/HW: Improve texture hazard handling, shuffles + fix CSBW
   typo" fixes a typo in the rect calculation for readbacks in the Software
   renderer fallback (the post's wording; which renderer owns that code the
   post does not say, and this file does not decide it). It is the post's
   one software-renderer-side change, and so a concrete reason the
   reference may have changed (0.2). The
   Original profile does not draw with a GPU: its world frame is the CPU GS
   model (GS_EXACT.md), and that model's **reference is PCSX2's software
   renderer** (GS_EXACT.md "Reference"; 906 designed conformance tests and
   the fb2 route points, recorded with the project's PCSX2 build, which
   the lead's inventory of 2026-10-09 gives as v2.6.3-3-g00d19ccce; confirm
   the build of each capture set from its own records before quoting it). Most of the post
   therefore describes defects of a renderer the port never measured
   against.
2. **The reference itself (P1).** If the project's PCSX2 is updated to 2.8
   or later (the lead's plan of 2026-10-09), the software renderer the
   numbers in GS_EXACT.md sections 1 and 10 were measured against may have
   changed too; the post does not say it did not, and its one change named
   for the Software renderer fallback (the CSBW readback rect, 0.1) is a
   concrete reason to check. Before any capture from a
   new build is used as a reference: regenerate the conformance capture sets
   (decomp `docs/GS_CONFORMANCE.md` sections 3, 8 and 9) and the fb2 route
   frames on the new build and diff them against the v2.6.3 sets. Equal
   sets: the new build is a drop-in reference. Any difference: a change of
   the reference (record which build each number is relative to), never a
   port regression. Until then, quote every pixel number as relative to the
   project's PCSX2 build (v2.6.3-3-g00d19ccce per the installed app and
   decomp `docs/CAPTURES_AUDIO.md`; the capture sets do not record it), and
   record the build in GS_CONFORMANCE.md and the fb2 records from now on.

   **Result (2026-10-09, measured fork-side; decomp
   `docs/PCSX2_FORK_GS_DIFF.md`; FIRST_LEVEL_AUDIT.md 1b F1 has the port
   follow-ups).** The project's new build is a PCSX2 fork based on v2.9.114
   (decomp `docs/PCSX2_FORK.md`), not 2.8. Fed the same GS input, its
   software renderer and v2.6.3's agree on all 906 conformance tests and on
   100 dumped first-level fields. One upstream change moves pixels of this
   game: the rectangle for throwing away primitives wholly outside the
   scissor grew by half a pixel, so an alpha-blended line segment starting
   a quarter row below the field's last row (the lamp's cables) now draws
   one pixel on row 223 (5 or 6 pixels per field); the model's line rules
   (GS_EXACT.md 3.6) predict the new pixel, to be confirmed port-side. The
   CSBW readback typo of 0.1 changed no pixel of this game. Most frame
   differences between the two builds were the game's frame and field
   phase (D_00810E80, D_00810E88), set by the capture tool's host-timed
   START and by the AREA11 load's emulator-specific iteration count; with
   the phase matched the fork reproduces 1,152 of 1,173 v2.6.3 captures,
   and fork runs are stable while a v2.6.3 re-run reproduced only 26.
   **So:** conformance numbers hold for both builds unchanged; the fork is
   the pixel reference for route and frame comparisons from now on; the fb2
   frames and their harness numbers (GS_EXACT.md 10, 10.1) stay relative
   to v2.6.3 until regenerated on the fork, paired by tick and field.
3. **Where the post matters most** is the Enhanced profile's GPU renderer
   and the D3D12 / Vulkan backends (FIRST_LEVEL_AUDIT.md 1b item 16): they
   meet exactly the obstacles the post describes (alpha-test fail modes,
   in-draw ordering of blends, depth precision, feedback reads), and the
   post's list of techniques is a map of the design space for them.
4. **Not in the post at all:** interlace, fields, deinterlacing or the
   CRTC timing (the post's only display item is the merge circuit, 2.1);
   SPU2 or any audio hardware; EE, VU, IOP, DMA or CDVD timing; float
   behaviour; memory-card hardware. No lead for SFX_SEQUENCER.md, the
   field presentation (GS_EXACT.md 11) or the drive-timing switch comes from
   it.

## 1. GS pixel pipeline

### 1.1 Alpha-test fail modes (AFAIL) and depth feedback

- **Post** ("GS/HW: Depth feedback loops and accurate AFAIL."): the GS's
  alpha test lets a game choose whether a failing pixel drops its colour,
  its depth or both; PC GPUs can only drop both. PCSX2 used to draw each
  such call twice (colour only, then depth only), which goes wrong when two
  triangles of one draw overlap, because the colour pass never updated
  depth for the later triangle. The new path tests depth in the shader with
  the hardware depth test off (Vulkan has a dedicated extension; D3D12 and
  OpenGL copy depth to a temporary colour texture and back). The section's
  footnotes 2 to 4 follow that sentence: D3D11, lacking texture barriers,
  copies colour and depth between possibly overlapping triangles (footnote
  2); on Metal, GPUs with framebuffer fetch get special handling and avoid
  the extra copies (footnote 3); because not every Metal GPU has
  framebuffer fetch, the in-place Vulkan-style path is enabled only on a
  whitelist, AMD RDNA and newer and Nvidia, and older AMD and Intel GPUs
  stay on the colour-copy path (footnote 4). These are about depth
  feedback, not blend ordering (1.4).
- **First level uses it.** Three TEST values on the route (port docs):
  the level and object class 0, TEST 0x5000D (alpha test GREATER 0, AFAIL
  KEEP; FIDELITY_FEATURES.md "The level is drawn ..."); the class-2 object
  units, TEST 0x53001 (alpha test NEVER with AFAIL RGB_ONLY: every pixel
  writes RGB only; CHAIN_PAGE.md section 6); the drop shadow's state block
  (2,9), TEST 0x51001 (NEVER with AFAIL FB_ONLY; src/gfx/metal/em_shadow_gs.h).
  The NEVER + fail-mode cases are exactly the colour-without-depth case the
  post describes.
- **Port status.** Original: the CPU model implements all four AFAIL modes
  per pixel, in draw order (GS_EXACT.md 5.3), measured exact against the
  software renderer (decomp GS_CONFORMANCE.md: the 4 AFAIL modes, colour
  and Z). A CPU model has no two-pass problem: each pixel is tested and
  written before the next primitive. Enhanced (Metal): the alpha test is a
  shader discard (`discard_fragment` paths in em_gfx_metal.m) and the
  RGB-only paths use framebuffer fetch.
- **Confirm.** Original: done (conformance). Enhanced and future backends:
  a designed packet with two overlapping triangles in one draw under
  ATST NEVER + FB_ONLY / ZB_ONLY / RGB_ONLY, drawn by the CPU model and by
  the GPU path, compared pixel for pixel (the model is the reference there).
- **Priority.** P3 for the Original profile; P2 for the Enhanced GPU path
  and the D3D12 / Vulkan backends.

### 1.2 Destination alpha test (DATE)

- **Post** ("GS/HW: Improve destination alpha testing handling"): PCSX2's
  destination alpha test now behaves more accurately together with the
  source alpha test, without the ordered-blend (ROV) path the developers
  had thought necessary.
- **First level uses it.** The shadow receivers' state (2,6), TEST 0x5C00D:
  alpha test GREATER 0, DATE with DATM 1, Z GEQUAL, ALPHA 0x44
  (SHADOW_ORIGINAL.md, the receivers row). The shadow's FB_ONLY pass (1.1)
  writes the destination alpha the receivers' DATE then reads.
- **Port status.** The model's DATE rule is measured exact (GS_EXACT.md
  5.3), and the conformance set includes the receivers' own TEST with
  ALPHA 0x44 (decomp GS_CONFORMANCE.md, pixel batch). DATE on a 16-bit
  frame is refused in strict mode (not used).
- **Confirm.** Done for the Original profile. For the Enhanced GPU path the
  same designed packets as 1.1, with DATE on.
- **Priority.** P3 (Original); P2 (Enhanced / backends).

### 1.3 AA1 (the GS's antialiasing)

- **Post** ("GS/HW: Support for HW AA1 lines/triangles"): AA1 draws a
  semi-transparent border around each triangle; the border writes no depth
  while the opaque interior does, so games must sort back to front for it
  to look right. In PCSX2 it was long a software-renderer-only feature; the
  hardware version uses the new shader depth path and is off by default
  for its cost.
- **First level.** The CPU model refuses AA1 in strict mode (GS_EXACT.md
  section 6), the Original profile runs strict and faults on any refusal,
  and the level smoke through Roger passes with the GS frame on: **no
  world frame on the recorded route sets AA1.** The Metal paths refuse it
  too (em_background_gs.h and the GS prim checks in em_gfx_metal.m). Not
  checked: the frames still drawn by the GPU (the 2D overlay pass, the
  status pages), whose packets do not reach the model.
- **Confirm.** When those frames move to the model (FIRST_LEVEL_AUDIT.md
  1b item 2), strict mode reports any AA1. If a level uses it, the post's
  point that the software renderer has long supported AA1 means designed
  AA1 conformance tests can be drawn by it; the coverage rule itself needs
  public GS documentation plus measurement, as for every rule in
  GS_EXACT.md.
- **Priority.** P1 only as part of 1b item 2 (a check, not a feature);
  P2 for later levels.

### 1.4 Blending in draw order (ROV, barriers, framebuffer fetch)

- **Post** ("GS/HW: Support for Rasterizer Order View (ROV)"): many PS2
  blend effects cannot be done with fixed-function GPU blending and must be
  computed in the pixel shader, where overlapping pixels of one draw must
  still blend in the order the game drew them. The post lists four ways to
  get that ordering, with their APIs: barrier loops (D3D12, Vulkan,
  OpenGL, Metal); framebuffer fetch (OpenGL, Vulkan, Metal; the most
  limited support); copy loops (D3D11, OpenGL; universal); ROV (D3D12,
  D3D11, Vulkan). Its ordering covers only the ends: framebuffer fetch is
  fastest and copy loops are slowest; ROV and barrier loops sit in the
  middle, and which of the two is faster depends on the case. (The Metal
  GPU whitelist in the post's footnotes belongs to the depth-feedback
  section, 1.1.)
- **Port status.** Original: the CPU model draws primitives in list order
  and its worker bands partition pixels (GS_EXACT.md section 9), so
  in-draw order is exact by construction. Enhanced (Metal): the GS blend
  runs in the shader on the frame pixel through framebuffer fetch, and the
  path refuses GPUs without it (`GS_WARN_FETCH`; FIDELITY_FEATURES.md
  "Needs framebuffer fetch (Apple GPUs)"). Consequence: the Enhanced GPU
  path does not run on Intel- or AMD-GPU Macs today; the Original profile
  does (CPU model).
- **For the backends (1b item 16).** Per the post's lists, D3D12 has
  barrier loops and ROV; Vulkan has barrier loops, ROV and framebuffer
  fetch; only the copy approach is universal. The post's experience (ROV tends to
  work well when barriers would be excessive, for example with AA1; a
  heuristic decides when to activate ROVs, which can be switched within a
  frame) is a design input, not a requirement: the
  Original profile on those backends only presents the CPU model's field,
  which needs none of this.
- **Priority.** P2.

### 1.5 Fog weight (256, not 255)

- **Post** ("GS/HW: Improve fog accuracy in HW Renderers"): PCSX2's
  hardware renderers used a 255-based fog weight where the PS2 uses a
  256-based one; interpolation of the colour across a primitive is still
  not modelled there.
- **Port status.** Corroborates GS_EXACT.md 5.2, already measured: C' =
  FOGCOL + ((C - FOGCOL) * F7 >> 15), which for a constant F is the
  256-based form, exact on the conformance tests; the 255-based formula
  CHAIN_PAGE.md and OWNER_DRAW.md once stated was refuted by measurement.
  The Original profile also models the in-triangle 8.7 weights (3.2); the
  Metal Enhanced path still interpolates F in float (5.2, "Still not the
  GS").
- **Priority.** P3 (corroboration only).

### 1.6 Integer depth

- **Post** ("GS/HW: Floor depth writes to improve Z testing"): the PS2 has
  integer depth only, so interpolated fractional depth must be floored;
  doing so removed false depth failures and much Z-fighting.
- **Port status.** Corroborates GS_EXACT.md 3.3 (Z is floored per pixel,
  measured; the open Z residue of 8.1 is about the row start, not the
  floor). The Metal Enhanced path uses float depth with LESS-EQUAL against
  the game's GEQUAL integer test (em_gfx_metal.m depth states): coplanar
  draws can resolve differently there.
- **Confirm (Enhanced).** Designed coplanar-Z packets through the model and
  the GPU path.
- **Priority.** P3 (Original); P2 (Enhanced).

### 1.7 Reading and writing the same texture in one draw (hazards)

- **Post** ("GS/HW: Improve texture hazard handling, shuffles + fix CSBW
  typo"): a hazard is a draw that reads and writes the same texture; PCSX2
  used to copy whenever texture and target shared an address, and now
  copies only when the draw actually reads and writes the same texels.
  Separately, "GS/TC: Check valid area overlap on exact match Tex is RT"
  is about the texture cache: a target drawn with an offset was matched to
  an earlier upload inside it by memory pointer alone, so uninitialised
  black pixels were sampled instead of the upload; checking overlap with
  the target's valid area fixed it (a GPU texture-cache matter, 1.12).
- **First level: no hazard (settled 2026-10-09, step DOF).** 001DDE10's
  depth-of-field pass (slot 0xFFF) runs every world frame, but none of its
  draws reads the buffer it writes (CHAIN_PAGE.md section 6.2): each copy
  reads the field and draws the 256x256 buffer at D_0027568C, each
  full-field blend reads that buffer (001D6BA0's TEX0; 001D6C90 sets only
  TEXA, TEST_1 and ALPHA_1) and draws the field. The original's packets on
  every captured page show it (`make test-chain-page-reference` part G),
  so the question this item raised (what the GS returns for texels a draw
  has already overwritten) does not arise; the model draws the pass since
  step DOF. Its large UV sprites (the 2:1 shrink, the stretch back) are
  bit-exact against the fork's capture of the pass since 2026-10-10 (decomp
  build/dof_capture, `make test-dof-pass-reference`; the row coordinate of
  a UV sprite is accumulated in binary32, GS_EXACT.md 3.4).
- **Priority.** Closed for the first level (1b block F, F2's premise).

### 1.8 Local-to-local transfers and the GS page cache

- **Post** ("GS: Disallow flipped GS->GS transfer when destination
  overwrites source"; "GS/TC: Allow creation of target during GS->GS
  transfer with offset"): local-to-local transfers are copies inside GS
  memory; when a reversed copy's destination overlaps its unread source,
  the PS2 avoids corruption because of its page cache; PCSX2, having no
  such cache, now forces a forward copy.
- **First level.** The model refuses LOCAL -> LOCAL in strict mode
  (GS_EXACT.md 2.2 and 6), and a transfer in a recorded world frame faults
  (section 9): the route's world frames have none. The status pages'
  uploads and the region restore 00200970 go to the GPU path, not the model.
- **Confirm.** If a frame that moves to the model needs one: the transfer
  direction field of TRXPOS (public GS register documentation) and designed
  overlapping copies drawn by the software renderer. The page-cache claim
  is the post's; no public timing or ordering rule for it is known to the
  project.
- **Priority.** P2 (the status frames, later levels).

### 1.9 CLUT storage mode 2

- **Post** ("GS/CLUT: Handle special case for CSM2 32bit mode switch"):
  storage mode 1 stores palette entries swizzled, mode 2 linear; a game
  switching from a mode-1 load to a mode-2 load must read the palette from
  GS memory again rather than reuse the swizzled buffer.
- **Port status.** The model implements CSM1 (GS_EXACT.md 4.6; its 16x16
  T8 layout with bits 3 and 4 swapped is the post's "swizzle") and refuses
  CSM2 (`EM_GS_REFUSE_FORMAT`, "CSM2 CLUT storage"): not used in the first
  level's world frames.
- **Priority.** P2 (later levels: strict mode reports it; then measure).

### 1.10 Channel shuffles, 24-bit targets, depth as colour

- **Post** ("GS/HW: Improve channel shuffle detection and use on 24bit
  sources"; "GS/HW: Support offsetting for channel shuffle instead of
  copying"; "GS/HW: Improve iRem CRC hack"): the post does not define a
  channel shuffle (it links a separate article for that). These sections
  say: shuffle lookup now also matches 24-bit sources, whose top 8 bits are
  not valid, by checking which bits a shuffle needs (previously only fully
  valid 32-bit textures matched); reads during shuffles now use an offset
  instead of copying memory pages for every draw; and the Irem-specific
  fix covers more cases where depth-buffer channels are used for
  post-processing effects (lighting, sky colour, water).
- **Port status.** The port's own reading, from public GS documentation
  and not from the post: a shuffle reads a buffer through another pixel
  format (for example a 32- or 24-bit frame or a depth buffer through a
  16-bit or 8-bit texture format). The CPU model works on the one swizzled
  local memory, so any such reinterpretation is just another address
  mapping (the measured layouts of GS_EXACT.md 2.0). Strict mode refuses CT16 / CT24 textures
  and T8H / T4HL / T4HH, so none occurs in the first level's world frames.
- **Priority.** P2 (later levels; the Enhanced GPU path, which would face
  exactly the post's problem).

### 1.11 Uploads (EE -> GS)

- **Post** ("GS/HW: On EE->GS transfer only invalidate area actually
  transferred"; "GS/HW: Correct buffered rect size of corrected EE->GS
  transfers"; "GS/TC: Update dirty target linked to source when dirty
  overlaps the read"): a game may declare a transfer rectangle far larger
  than the data it sends; consecutive uploads to the same address must
  extend, not replace, the updated area.
- **Port status.** The model writes uploaded pixels in raster order over
  the TRXREG rectangle as they arrive (GS_EXACT.md 2.2); the first level's
  uploads leave all 5,504 blocks equal to the disc model
  (`make test-gs-memory-reference`). A transfer whose data ends before its
  rectangle is not a designed conformance case.
- **Confirm.** One designed packet with a TRXREG larger than its IMAGE
  data, through the software renderer and the model.
- **Priority.** P3.

### 1.12 Texture-cache and GPU-only items (no counterpart in the port)

These sections describe PCSX2's GPU texture cache (matching GS memory to
GPU textures) or GPU performance. A CPU model over one 4 MiB local memory
has none of them: "GS/Texture Cache: Improve depth target lookup
behaviour", "GS/HW: Fix some target detection and format issues.", "GS/HW:
Adjust depth copies when doing Texture Cache moves", "GS/HW/COLCLIP: Check
tex mapping is enabled when checking for recursion", "GS/HW: Improve inside
target wrapping and clear draw behaviour" (unwritten GS memory left as
garbage by the GPU renderer), "GS/HW: Properly scale depth up if required
when native scaling is in use", "GS/TC: Only bilinear resize depth if
downscaled", "GS/TC: Delete empty target after height adjust", "GS/TC: Add
new HW Readback option to force full download", "GS/TC: Check format matches
on invalidation rect translation", "GS/HW: Remove zero clear from
GSUploadQueue, use transfer_type instead", "GS/HW: Merge together adjacent
tristrips in drawlist.", "GS: Add draw buffering option", "GS: Optimize draw
buffering and pointer logic", "GS/HW: Framebuffer switching optimizations",
"GS/HW: Further improve framebuffer copies.", "GS/HW: Enable non recursive
sw blending on minimum level.", the Direct3D 11 / 12 sections. One useful
fact for the Enhanced GPU path, from "GS: Add draw buffering option": some
games alternate draws every triangle (a triangle textured, then a
reflection map applied, then a blend), which causes heavy context
switching on a GPU path; the port's own frame cost is the CPU model's (1b
item 18). P3.

### 1.13 Texture filtering (Enhanced only)

- **Post** ("GS/HW: Add SW (Shader Based) Anisotropic Filtering"): the PS2
  has no anisotropic filtering. Forcing it on for point-sampled textures
  (games use point sampling for a pixel-art look) broke them, for example
  black pixels bleeding in from outside a texture's clamped region; a
  shader version keeps point-sampled textures point-sampled.
- **Port status.** LAUNCHER_OPTIONS.md "Texture filtering" (Original:
  nearest; Enhanced CANDIDATE). The lesson for the Enhanced option: filter
  only what the game samples bilinearly (TEX1 MMAG / MMIN), never a
  texture the game samples nearest, and respect REGION_CLAMP bounds
  (GS_EXACT.md 4.3) in any filter footprint.
- **Priority.** P2.

## 2. Display

### 2.1 The CRTC merge circuit

- **Post** ("GS: Fixes to CRTC merging"): the PS2's CRTC merge circuits
  allow limited post-processing and compositing before output (one game
  overlays a loading bar this way, another left merging on with invalid
  settings); for the loading bar, PCSX2 was not updating the GS registers
  in the correct order, which showed the wrong image.
- **Port status.** Public GS documentation (not the post): the CRTC has
  two read circuits, and PMODE selects and merges them. The first level's PMODE is 0x66, circuit 2 alone,
  measured at all 19 route points and the movie driver's environment; the
  presenter faults on any other configuration (GS_EXACT.md 11). The port
  applies 00100550's stores as the code issues them and presents from the
  values in effect at the frame.
- **Confirm (later levels).** If the fault fires: the PMODE / ALP / MMOD /
  SLBG fields from public GS documentation, and a software-renderer
  capture of that frame.
- **Priority.** P3 (first level); P2 (later levels).

## 3. IPU and movies

- **Post** ("IPU: Reset dc dct precision on soft reset"): the IPU is the
  PS2's MPEG decoder, used mainly for movies; PCSX2 did not reset the
  intra DC precision state on an IPU reset, which made one game's movies
  flash.
- **Port status.** The port does not run the IPU path: `src/em_movie.h`
  plays a lossless remux of the user's PSS streams through the operating
  system's MPEG-2 decoder. The path into and through the first level
  shows movies (E900, which New Game requests again before the opening,
  STARTUP.md; E001.PSS at the level exit), and their pictures are not
  compared with the original (FIDELITY_FEATURES.md, the level-exit entry:
  "Not compared: the movie's pictures and sound").
- **What could differ.** The original's pictures are the IPU's output
  (the decoder plus its colour-space conversion, uploaded to GS memory and
  drawn): its YCbCr to RGB rule, chroma upsampling, rounding and any
  dithering can differ from an OS decoder's, even with the same bitstream.
  The DC precision itself is a stream header field the OS decoder reads.
- **Confirm.** A software-renderer capture of a movie frame (the displayed
  buffer from GS memory, as the fb2 frames are taken) against the port's
  decoded picture of the same frame; the IPU's documented conversion as
  the rule if they differ.
- **Priority.** P1 (1b block F, F4: the movies are on the route).

## 4. Nothing for these subsystems

The post has no hardware statement on SPU2 (voices, ADSR, reverb,
interpolation), EE / VU / IOP behaviour or timing, DMA, CDVD, float rules,
interlace, or memory cards (its only memory-card item is sorting the card
list in the UI). 1b item 1 (audio output), SFX_SEQUENCER.md, EE_FLOAT_MODEL.md
and IOP_STREAM.md get no lead from it.

## 5. For the emulator tooling (the lead's fork work)

What the post says about the questions the lead asked (it was read for this;
the fork itself is out of this lane):
- **ARM64 / Apple Silicon:** nothing. The post does not mention ARM64
  builds, Apple Silicon, Rosetta or a native macOS arm64 binary. macOS is
  mentioned for EyeToy camera support ("USB: Implement EyeToy camera
  support on macOS") and in the Metal footnotes (1.1). Whether 2.8 runs
  natively on arm64 must be established from the project's own build, not
  from this post.
- **Debugging, IPC, automation:** nothing about the debugger, PINE /
  IPC, breakpoints or scripting.
- **Capture:** FFmpeg is now bundled with the Windows build, with a
  hand-picked codec set, for the built-in video capture ("Deps: Bundle
  FFmpeg on Windows"); the post says nothing about other platforms.
  Unverified for macOS: presumably the project's Media Capture still needs
  an FFmpeg of the emulator's architecture (decomp
  `docs/CAPTURES_AUDIO.md`), to be checked on the new build itself. Video captures can be stored per game
  ("Qt/FSUI: Miscellaneous UI Changes").
- **Scratch data directories:** a new `-datapath` launch argument
  overrides the data folder ("Qt: Add `-datapath` launch argument"),
  which fits the rule that PCSX2 runs use scratch data directories only.
- **Other UI facts:** the status bar shows the renderer, the internal
  resolution multiplier and host GPU use; a hotkey cycles the blending
  accuracy; a shader-compile indicator. GS dumps are the developers'
  benchmark inputs ("GS: Add draw buffering option" lists its draw-call
  and barrier reductions per dump).
- **Recommendation for port captures:** keep using the software renderer
  (the reference of every number in GS_EXACT.md); the hardware renderers'
  new accuracy options (accurate alpha test, AA1, ROV) do not make them a
  reference.

## 6. Where the leads went

| Lead | Priority | Roadmap |
|---|---|---|
| 0.2 re-measure the reference on a new PCSX2 build | P1 (diff done 2026-10-09; fb2 re-capture open) | FIRST_LEVEL_AUDIT.md 1b F1 |
| 1.7 001DDE10's pass reads the frame | P1 | 1b F2 |
| 1.3 / 1.8 / 1.9 strict refusals once the overlay and status frames reach the model | P1 (check) | 1b F3 |
| 3 movie pictures (IPU against the OS decoder) | P1 | 1b F4 |
| 1.1, 1.2, 1.4, 1.6, 1.10, 1.13 GPU path and backends | P2 | PORT_PROFILES.md "Queued work" 5 |
| 1.3, 1.8, 1.9, 1.10, 2.1 later levels | P2 | PORT_PROFILES.md "Queued work" 5 |
| 1.5, 1.6 (Original), 1.11, 1.12, 2.1 (first level) | P3 | none |

FIDELITY_FEATURES.md is unchanged: no claim there changes. The post
corroborates two measured rules (the 256-based fog weight, the floored
integer Z) but is not evidence for either, and it refutes none.
