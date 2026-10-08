# AREA01 rendering and models

World textures, dynamic VU programs and packets, the world model bank, generic model/animation workers, drawing and shadows, the C69A0 pose stages, lights, the channel-3 matrix service, RCL workers, placed props and the global library models.

Contents:

- [AREA01 world texture delivery](#area01-world-texture-delivery)
- [AREA01 dynamic VU programs](#area01-dynamic-vu-programs)
- [Floor-field and ripple programs](#floor-field-and-ripple-programs)
- [Kind-6 near-fire program](#kind-6-near-fire-program)
- [AREA01 world in the level smoke and its pixels](#area01-world-in-the-level-smoke-and-its-pixels)
- [AREA01 dynamic packet binding](#area01-dynamic-packet-binding)
- [AREA01 world model bank selection](#area01-world-model-bank-selection)
- [AREA01 generic model and animation workers](#area01-generic-model-and-animation-workers)
- [AREA01 generic model drawing and shadows](#area01-generic-model-drawing-and-shadows)
- [C69A0 live pose binding and shared status stages](#c69a0-live-pose-binding-and-shared-status-stages)
- [AREA01 light worker binding](#area01-light-worker-binding)
- [AREA01 channel-3 matrix upload](#area01-channel-3-matrix-upload)
- [AREA01 packet and context workers](#area01-packet-and-context-workers)
- [AREA01 placed-prop adapter](#area01-placed-prop-adapter)
- [Shared library models for AREA01 pickups](#shared-library-models-for-area01-pickups)

## AREA01 world texture delivery

2026-10-04. Object units, the static world and the chain page share one
area-selected TEX0 catalog owner, `em_world_textures_live.c`. AREA11/0
retains its existing object/page exports (487 distinct keys). AREA01/0
uses a merged 460-key export. Six common keys have different pixels in
the two areas, so retaining the old registry is observably incorrect.

### Original delivery and source data

The source is the user's disc, with the existing loader replay and GS
decoder in `export_disc_textures_gs.py` and `export_object_textures.py`.
`func_001FFCD0.c` and its original assembly were read: the readable C is
NEARMISS. States 4 and 8 deliver the top/nested A sections through
001FF590; states 7 and 11 deliver B sections through 00200830. State 7
also calls 00200890 to select and deliver the player texture slot.
Completion sets the loader status byte to 0x63 after the applicable
delivery and relocation loops. 00200830 sends its DMA chain through the
existing channel-1 helpers. No loader timing is reproduced by this export.

For AREA01 sub-0 the nonempty texture sources are the resident library
slot from `chunk27/f00_id35.bin`, player slot 8 from
`chunk03/f00_id08.bin`, and nested area A data from
`chunk05.n0/f00_id44.bin`, offsets 0x75000 through 0x14D800. The shared
replay checks these extracted bytes against DATA.DAT and applies the
original transfers in delivery order. The library's initial and slot
deliveries both run, as in the original startup/area route.

`tools/export_area01_world_textures.py` collects TEX0 references from:

- The delivered model bank named by D_0028A59C, through the existing
  model-record/block parser.
- The delivered static bank named by D_0028A5A0, through the existing
  AREA01 bank parser and all vertices of each original block.
- The twelve 0x860-byte dynamic records named by D_0028A5A4; their original
  dynamic programs read three vertices per record. This includes the
  previously absent material 0x0006E305954234F8.
- The global library models (`chunk27/f01_id37.bin`, at D_0028A56C) that
  AREA01's own owners bind: every allocated pool record's +0x44 in the
  recorded AREA01 capture that starts a library entry. This adds the
  pickups' 0x4D and 0x58 (0015AFA0), the overlay owner 00826CF0's 0x6B
  and the companions' 0x73 / 0x76 / 0x7A (001C5680): 16 materials the
  first level's id lists do not name (0x6B's are labelled Roger's there
  and were filtered). Found by the binding probe's first presented
  frame: the pickup unit at 0x00C90800 (library 0x58 + 0x40) referenced
  TEX0 0x0004821555422274.
- The resource-table models AREA01's owners bind (step FRAMES): every
  allocated pool record's +0x44 equal to a nonzero D_0028A490[id]. In
  every AREA01 capture: 00128C10's 0x0F, 001BFFD0's 0x20, 001C02E0's
  0x22 (a face resource, 4 blocks of 0x163 quadwords, its TEX0s at qword
  11 i) and the NPC 00825350's 0x47. Ids 0x08..0x34 are the boot's
  resident sector-3 files: chunk03/fNN_idXX.bin lie in file order from
  0x10E99C0 (D_0028A490[0x08..0x34] in every capture), and the exporter
  requires the entry to hold the file's bytes in the capture; 0x47 lies in
  the area load map (chunk05.n0/f14_id88.bin). Found by the probe: the
  unit at 0x012C2200 (resource 0x22 + 0x40) referenced TEX0
  0x2004661599421F48.
- The face 001BA8E0 binds: the NPC 00825350 calls 001BA8E0(self,
  self[0xD]) in its state 0 (func_overlay_AREA01_00825310.c); +0x0D is
  0x47 in every capture, which selects D_0028A490[0x88]
  (func_001BA8E0.c; 0x1955140 in the load map, a 0x32-block face). Other
  types are refused. Found by the probe: unit 0x01955180.
- The TCC 0 (RGB) MODULATE words of 001E9E60 (the floor fields:
  0x20048CC155422242, and 0x20048E4155422256 when the field record's byte
  +0x5C is 1) and of 001E7D20 outside area 0x13 (the ripple surface:
  0x20048BA199422040), drawn by the programs of section "Floor-field and
  ripple programs". Their texels decode like the others; the page pixel
  path ignores their alpha (TCC 0: Af = Av).
- Existing shared player, face, equipment and effect source collectors.
  AREA11 Roger references and the AREA11 flame descriptor are excluded;
  the existing dormant equipment model's invalid zero references are not
  texture materials.

The loader's disc data supplies every exported pixel. Residency poisoning
proves every texture/CLUT read was written by the replayed uploads.
All 460 decoded images match all sixteen recorded AREA01 GS freezes,
including arrival, a01_02 and the side routes. Captured pixels are only
comparison inputs. The EMOT is 5,644,080 bytes of raw RGBA/GS-alpha
pixels plus its table and header. No alpha rescaling occurs.

The exporter writes `assets/area01_world_textures.emot` and its JSON
report (default `--out`); both are ignored, and no original data belongs
in a commit. It refuses symlink ancestors and replaces a target symlink
without writing through it.

### Native binding and invalidation

`em_world_textures_live_bind(area, subarea)` selects a completed delivery.
Each invocation invalidates the cache, including a reload of the same
area. Supported pairs are 11/0 and 1/0; unknown pairs fail closed. The
initial selection is 11/0 for the existing first-level entry path.

Both `em_owner_draw_live_textures` (also used by the static world) and
`em_chain_page_live_textures` call `em_world_textures_live_ensure`.
That function validates every selected EMOT before a registry mutation,
checks dimensions, extents, reserved fields and duplicate-key pixel
agreement, then clears the shared registry once and installs all keys.
Keys ignore only TEX0's CLD bits, matching the existing backend lookup.
The device copies pixel storage. Repeated calls on that device do no work;
a device change loads the selected catalog. Failed loads remain failed
until the next delivery, and a failed GPU upload clears partial results.

`em_gfx_world_textures_reset` releases only the object/chain TEX0 registry
in Metal. UI textures, background textures, meshes, and GS render surfaces
have independent ownership. D3D12 and Vulkan preserve their existing
fail-stop world-renderer status. No shader or rasterization arithmetic
changes are included.

The root integration needs to add `src/game/em_world_textures_live.c` to
the native source list and call the selector once at the completed area
loader binding, before static/object/page draws. It must also select 11/0
on a fresh first-level delivery after any previous area session. Do not
bind once per frame: a bind means the original resources were delivered.
`em_scene_bindings.c` and the Makefile remain owned by the root integrator.

### Verification and scope

`python3 tools/test_world_textures_reference.py` passes in default mode
(first and arrival GS freezes) and with `EM_TEST_FULL=1` (all sixteen).
Its native bridge verifies all 487 AREA11 and 460 AREA01 keys, dimensions
and pixel hashes, then AREA11 -> AREA01 -> AREA11 transitions, same-area
reload, another device and the unchanged cache path. It exercises the
six reused keys with changed pixels and verifies removed keys disappear.
Eleven rejection cases cover unsupported deliveries, absent/malformed
catalogs, conflicting CLD aliases and an interrupted GPU upload.

The native module builds with `-Wall -Wextra -Werror`; Metal syntax checks
pass without warnings. Scoped no-disassembly and whitespace checks pass.
The complete game/first-level smoke remains the integrator's regression.
This proves pixel source selection and registry lifecycle; it does not
claim an AREA01 gameplay screenshot or rasterization comparison. Other
areas, subareas and player texture modes require their own verified
delivery catalogs. The AREA01 gameplay gate remains controlled by the
remaining owner bindings.

## Floor-field and ripple programs

2026-10-04 (step FRAMES). Two static ELF packets the AREA01 page CALLs
through 001CB760; each is one CNT (FLUSHE, STCYCL 4,4, STMASK 0, STMOD 0,
BASE 0x20, OFFSET 0x190, one MPG to micro 0) and a RET:

| Packet | Caller | MPG | Batches |
| --- | --- | --- | --- |
| D_002345E0 (0x510 bytes with its RET) | 001E9E60, for each of 0015A2C0's 8 floor fields | 153 instructions from ELF 0x00234610 | 6 x 24 qwords (3 rows of 8 points) |
| D_00234B00 (0x4D0) | 001E7D20, the ripple surface (state 1, when 001E7CB0 is non-zero) | 145 instructions from ELF 0x00234B30 | 30 x 96 qwords (3 rows of 32 points) |

The page is built in reverse, so the DMA order is: the CALL of the
packet, the GS state REF (D_00275674 + 0x720, a DIRECT of 7 A+D writes),
001CB950's TEX0 DIRECT, the 9-quadword constant block (UNPACK to
0x3F8..0x3FF: the GIF tag, NLOOP 16 / 64, PRIM 0x7C, REGS ST, RGBAQ,
XYZF2; the colour, light, fog, clip and texture rows), the camera rows 0x70003AC0 (UNPACK to
0..3), then the batches: UNPACK with FLG (to TOPS), the first ended by
MSCAL 0 and the others by MSCNT.

The microcode (read with the decomp's `tools/disasm_vu.py`; not
reproduced here): the batch entry reads TOP, loads the constant rows and
for each column j emits the vertices TOP + j and TOP + row + j (a strip),
calling a subroutine per vertex; it then copies the GIF tag to TOP +
3·row and kicks it (end bit). MSCNT resumes after the end bit's delay
slot, whose branch returns to micro 0, so every batch runs the same code.
Per vertex: d = point - light row (VF16), P = ERLENG(d), n = d·P; the
colour is the colour row × n.y × VF21.z and its alpha (1 - n.y)·VF21.x +
VF21.y times the point's w, all clamped to 0..255 (FTOI0); the texture
pair is (n.x, n.z)·VF20.z (the floor program adds VF0, the ripple program
VF20.x / VF20.y, then the floor program scales it by a depth term
(VF21.w - y) / n.y + VF16.y - y), plus the height differences to the
next point in the row and in the next row times VF20.w, with 1 as its
third lane; the position goes through the camera rows, then the clip rows
VF17 / VF18 (CLIP; FCAND 0x3FFFF over this and the two previous
vertices), Q = 1 / w, XYZ = position·Q, ST = pair·Q, the fog (VF19) clamped
and, for a clipped triangle, raised by VF19.y (which sets the ADC bit, so
the GS does not draw it). VF02.w (ST's w lane) is never written.

Native: `em_vu1_floor_program_mscal` / `em_vu1_ripple_program_mscal`
(em_vu1_page_programs.h, one body with a `wide` switch), and em_chain_page
recognises the two MPGs (fault on any other), allows FLG UNPACKs and MSCNT
only after a batch of the same program (counts `mscal_floor` /
`mscal_ripple`). The packets come from `assets/effect_tables.emet`
(windows 0x002345E0 and 0x00234B00). Both TEX0 words are TCC 0 MODULATE;
the Metal page path implements Af = Av for TCC 0 (MODULATE only;
HIGHLIGHT with TCC 0 is refused).

Verification: `make test-level2-floor-vu-reference` (`tools/test_level2_floor_vu_reference.py`)
executes the original microcode on the chain-page model's VU1 machine
(tools/chain_page_model.py VuOracle, which gained the vector MULA the
programs use and the MSCNT / FLG walk) and compares: every floor and
ripple run of each AREA01 capture's page, re-linked as REF transfers into
a page of their own, through the model and the native page (every kicked
GIF byte, every GS primitive, the batch counts); synthetic batches
(perturbed points, camera, colour, fog and light rows, random starting
registers; MSCAL then MSCNT) comparing all data memory and every register
(VF, VI, ACC, Q, I, P, the clip history); and five fail-stops (MSCNT
before a batch of either program, FLG without the program, a truncated
MPG, the packet unmapped). Quick: 3 captures, 40 synthetic items per
program (5 s). Full (`EM_TEST_FULL=1`, receipt
`build/level2/frames/floor-vu-full.log`): 17 captures, 136 floor fields,
18 ripple surfaces, 1,356 kicks, 15,960 primitives, 5,338 synthetic
batches (662 both-faulted operand cases). The ERLENG result is the shared
EFU model of the streak and grid programs (no capture holds an EFU
result). `make test-chain-page-gpu` checks the TCC 0 pixels against the
GS pixel model. Limits: no AREA01 frame has been compared with a capture
pixel by pixel.

## Kind-6 near-fire program

2026-10-04 (step DRAWN). AREA01's fire owner 001E3D90 asks 001CFBE0 for a
third layer of kind 6 once its projected size D_00275C00 exceeds 0x100 (the
camera close to the fires on the crate stack, route a01_00 from f405).
001CFBE0 kind 6 hands 001CB760 the program packet D_0023D930 (with the
blend row D_00251260 entry 2 or 3, as kind 1): the sprite program's packet
layout (STMASK, STMOD, BASE 0, OFFSET 0, MPGs of 256 instructions from ELF
0x0023D958 to micro 0 and 130 from 0x0023E160 to micro 0x100, the 128-word
lookup and 17 constant rows to dmem 0x6E..0x7E, rows 125 / 126 being the GS
window's min and max x, y), 0xF70 bytes with its RET.

The microcode was read with the decomp's `tools/disasm_vu.py` and compared
with the sprite program's instruction by instruction (not reproduced here):
micro 0x000..0x10A are the sprite program's except the batch size (one
source particle per batch: the immediates at 0x028 / 0x035 / 0x03A), the
two set-up instructions at 0x0F5 / 0x0F6 (I = 1, VF11 = 1) that kind 6
leaves as NOPs, and the branch offsets; the emission is new: CHAIN_PAGE.md
section 3 describes it (a screen-space square cut to the window, 5 x 5
SPRITE tiles, an empty tag when the square is z-rejected or outside the
window) and lists the producer of every Q / MAC / clip read.

Native: `em_vu1_kind6_program_mscal` (em_vu1_page_programs.h: the sprite
program's flow with a variant switch, the emission emvup_kind6_particle;
`emvup_sprite_batch` takes the batch size), recognised by em_chain_page.c
from the two exact MPG uploads (counts `mscal_kind6`, `kind6_prims`; any
other second part faults). The packet comes from `assets/effect_tables.emet`
(the new window 0x0023D930 + 0xF70, `tools/export_effect_tables.py`; equal
to the ELF in the 17 export captures). The model (tools/chain_page_model.py)
knows the program as PROGRAM_KIND6.

Verification: `make test-level2-kind6-vu-reference`
(`tools/test_level2_kind6_vu_reference.py`) runs the ORIGINAL microcode on
VuOracle and compares the native translation and page:
- captured pages: every kind-6 run of the AREA01 captures that hold one
  (a01_00, a01_01, a01_07, a01_s1, a01u_02; quick: a01_00 and a01_07),
  re-linked into a page of their own: each MSCAL's registers (VF, VI, ACC,
  Q, I, R, P, the clip history), all data memory, every kicked GIF byte and
  the producer of every non-interlocked read, then the page's GS primitives,
  kicks and MSCAL counts;
- synthetic MSCALs from the captured memories (counts, ages, rates, tile
  matrices crossing each window edge or behind the near plane, size rows,
  narrowed windows, random starting registers); every conditional branch
  both ways (0x045, the batch loop, is never taken with one particle per
  batch, and the test asserts that);
- fail-stops: exponent-255 operands (both sides fault), the second MPG
  without the first, a truncated second MPG, the packet unmapped.
Quick (7 s): 2 captures (4 runs, 600 sprites), 120 synthetic. Full
(`EM_TEST_FULL=1`, 47 s; receipt `build/level2/kind6/full.log`): 5 captures,
9 runs, 1,800 sprites, 1,200 synthetic MSCALs, 14,508 kicks, 9,424,128
packet bytes. `make test-chain-page-reference` passes quick and full with
the batch-size parameter (the sprite, snow, streak and kind-2 programs
unchanged).

Live: the a01_00 phase now runs all 780 frames (LEVEL_SMOKE.md "a01_00"),
and the smoke's `check_kind6_area01` requires a page that ran the program
(measured at the merge, 2026-10-07: 374 pages, 99,000 primitives, the
first at a01_00 f405).

## AREA01 world in the level smoke and its pixels

2026-10-04 (step DRAWN). Three checks of the drawn AREA01 world, in the
level smoke after the AREA01 phases and in the fb2 pixel harness:

- **The static world and the dynamic table**
  (`tools/level_smoke_static_world.py check_area01`): every AREA01 001C1D00
  call is drawn in its tick; the sampled calls (one in 400; quick: the
  first, `EM_TEST_FULL=1`: all) carry, besides the AREA11 sample's inputs,
  the dynamic table's identity (D_0028A5A4, the extent 001D5BD0 reads:
  0x10 + count x 0x860, and its FNV-1a), the chain table and D_00250F30..
  before the call, and the call's whole output (chain table, render
  context, 0x70003400..7F, D_00817240.., D_00250F30.., the arena bytes from
  each cursor +0x10..+0x1C that moved, +0x18's 0x100 further on where
  001CB5F0 / 001CB760 build their blocks). The checker lays them over route
  15's AREA01 capture (f801), requires the capture's table to be the
  port's, executes the ORIGINAL 001C1D00 (its 001D5BD0 on the AREA01 render
  test's interpreter, A01EE) and requires every byte it writes to be in the
  port's output and equal to it, every logged arena byte to be written by
  it (tag bytes excepted), then the channel-0 run's triangles through the
  original microcode. a01_00 run: 842 calls, 2 samples (13201, 13601);
  full: 5,833 / 8,317 written bytes equal, 2,012 / 3,692 triangles; the
  sampled calls reach 001D5BD0, 001D5A70 (24 records) and 001D4FC0 (12),
  not the partial-clip 001D5170.
- **The shadows** (`tools/level_smoke_shadow.py check_shadow_area01`): the
  shadow samples restart when the AREA01 composition binds
  (em_scene_bindings.c), so AREA01 has its own from its first frame; over the AREA01 ticks 0015C160's route for the gate bytes, every
  drawn 001DA6A0 and decal flushed, and the sampled player and owner-walk
  actor shadows replayed with the ORIGINAL 001CB590 + 001DA6A0 (and
  0015BF90 + 001CE300 for decals) over route 15's AREA01 capture (its
  static bank holds the receivers). a01_00 run, full: 822 player shadows
  (all drawn), 9 sampled plans equal (161 receivers), 1 decal sample, 841
  actor-shadow calls (121 drawn), 9 sampled plans equal; the a01_arrival
  run (quick): 61 player shadows, 1 sampled plan (17 receivers).
- **Pixels** (`tools/test_fb2_pixels.py --point 15_level_exit`, `make
  test-fb2-pixels-area01`, about 2.5 min; also in `EM_TEST_FULL=1`): the
  decomp's fb2 field of route 15's end (counter 16564, two neutral frames
  after f801: the AREA01 arrival) against the port's Metal frame of the same
  tick, point-sampled as GS_EXACT.md section 10: **camera exact; 54,378 of
  114,688 pixels exact (47.41 %), mean channel error 0.67, per-pixel
  maximum error p50 / p90 / p99 1 / 1 / 11**, the best of the 8 compared
  points. The difference image is the ±1 floor of the first level's points
  over every surface; geometry, textures, the title text, the player and
  his shadow, the crates, the door and the lit window are in place.
  The route_a01 save states hold GS memory (`gs.bin`) but no record of
  which buffer is displayed or which row it shows, so their frames are not
  compared (a fb2-style capture of an a01 beat would need a new PCSX2
  recording).

## AREA01 dynamic VU programs

2026-10-03. AREA01's two dynamic VU programs now run through the existing
chain-page consumer. This extends the shared level and shadow-clip owners;
it does not introduce another kernel implementation. The native packet
builders are described in section "AREA01 dynamic packet binding".

### Original program evidence

The original DMA program packets at 0x00237450 and 0x00237720 were read from
the pinned boot ELF. Their VIF setup equals the existing level and box-clip
packets respectively: cycle 4/4, base 0x190, offsets 0x109 / 0x101, then
the program upload and return. The normal program has 79 instructions;
the clip program has 1,183, delivered in five uploads.

Comparing every original instruction finds only these differences:

| Existing owner | Dynamic variant | Differences |
|---|---|---|
| `em_vu1_level_kernel.h`, 0x00237180 | 0x00237450 | 11 immediate fields: three vertices instead of 32, output and kick offset 0x10 instead of 0x84 |
| `em_vu1_shadow_clip.h`, box 0x00239C90 | 0x00237720 | 3 immediate fields: three vertices, the matching first-triangle bound, output work origin 1069 instead of 1185 |

The test asserts the exact differing instruction indices, identical upper
words and non-immediate bits, and the old/new immediate values. No
microcode or disassembly is stored in the repository. Existing callers keep
their 32-vertex defaults and existing arithmetic paths.

`em_vu1_dynamic_kernel_batch` uses the shared level arithmetic and its
carried registers. `em_vu1_dynamic_clip_run` uses the box clip arithmetic,
with a caller-provided VF register image for neighboring page producers.
The dynamic consumer transfers the relevant carried registers to and from
the page's existing register owner. It does not add an actor memory arena.

### Chain page and resources

`em_chain_page.c` recognizes the two exact upload sequences, implements
their TOPS double buffering and MSCNT continuation, and feeds every emitted
packet through its existing GIF/GS decoder in page order. The dynamic
units remain ordinary nested DMA CALLs; they are not actor-unit shortcuts.
The normal and clip batch counts are appended to `EmChainPageCounts`.
The reference-test ctypes layout is updated correspondingly.

The original class-1 GS state writes masked ZBUF state before drawing. The
page accepts masked depth writes under the existing frame-depth-buffer
contract used by class-2 object units; an unmasked write faults. The
captured dynamic state is PRIM 0x7C, TEST 0x53001, ALPHA 0x44, TEX1 0x60,
CLAMP 0 and COLCLAMP 1. Its primitive/state output goes to the existing
`em_gfx_gs_prims` backend. This work does not prove pixel rasterization.

`tools/export_effect_tables.py` adds only the two required program windows:
0x00237450 / 0x2D0 bytes and 0x00237720 / 0x2570 bytes. The existing
effect-window loader and chain-page reader already map arbitrary declared
export windows; no additional native loader implementation was needed.
The export now has 26 blocks and 59,964 bytes of payload. Every block was
verified against all 17 existing first-level captures. The new immutable
program windows additionally match all 17 AREA01 captures (175,168 bytes
compared). Other writable ELF data changes during AREA01 gameplay, so
that second comparison deliberately covers only the two new windows.

The export is the ignored `assets/effect_tables.emet` (rerun
`tools/export_effect_tables.py`; an older export lacks these windows).

### Verification

`python3 tools/test_level2_dynamic_vu_reference.py` runs natively. Full
mode is `EM_TEST_FULL=1`. No emulator was launched. Each batch executes
the original uploaded microcode; the test compares all 16 KiB of VU memory
afterward and every emitted packet byte, with no ignored ST lanes. The
page test also compares the native chain consumer's packet sequence and
decoded GS primitives and state. Synthetic inputs retain captured material
templates but vary matrices, vertex positions, winding, near-plane and
screen-plane crossings, color and carried registers.

| Check | Result |
|---|---|
| Dynamic quick | 5 captures, 56 emitted packets, 22 GS primitives; 80 synthetic batches / 138 packets, including 20 clipped-triangle packets |
| Dynamic full | 17 captures, 108 emitted packets, 42 GS primitives; 2,400 synthetic batches / 4,051 packets, including 536 clipped-triangle packets; 5.3 s |
| Mixed synthetic page | Alternating normal / clip / normal programs: 8 packets and 2 GS primitives, all exact |
| Fail-stop contracts | Unknown program, absent program window, incomplete clip upload and unmasked depth writes all rejected |
| Existing static-world full | PASS, all 17 captured runs and 576 synthetic batches; 11.0 s |
| Existing chain-page full | PASS, all 15 first-level pages; 18,765 kicks / 6,466,096 packet bytes, 12 operand faults and 9 page fault cases |

The shared Python oracle documented saturation for finite overflow but
could raise while packing an overflowing dead look-ahead result. This test
applies that documented finite-overflow clamp before packing. It does not
consult native output to determine an expected value. Existing VU arithmetic
assumptions and unsupported operand/fixed-conversion limitations remain
those documented by the shared kernel owners.

### Remaining live dependencies at this checkpoint

The AREA01 texture dependency is now supplied by the shared catalog owner
and disc-derived export documented in section "AREA01 world texture delivery", including
dynamic TEX0 key 0x0006E305954234F8. Its area selector must be bound at
resource delivery by the root integration. The backend already supports
this dynamic material's PSMT4 / CT32-CLUT / MODULATE format; an absent
registered texture still faults.

The existing first-level live-smoke model also has no dynamic-program
dispatch. This dedicated oracle proves the composed native consumer over
captured and synthetic page inputs. It does not claim an AREA01 gameplay
frame, texture installation or screenshot comparison. The parent retains
the AREA01 gameplay gate while other reached owner bindings are incomplete.

## AREA01 dynamic packet binding

2026-10-03. The canonical render-context owner now composes AREA01's
dynamic packet builders with its existing static pass. This is packet
construction, verified against the original instructions. Presentation of
the dynamic VU programs remains a separate, required dependency. The
AREA01 gameplay gate must stay until that dependency and the actor binding
are complete.

### Original call chain and shared owners

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

### Memory ownership and binder changes

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

### Instruction and regression verification

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

### Presentation and live-proof limits

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

Since step DRAWN the live smoke checks the connected pass too: the
static-world sample of an AREA01 frame carries the dynamic table's
identity and the call's whole output, and the level smoke re-executes the
original over it (section "AREA01 world in the level smoke and its
pixels"). Subsequent VU presentation work should record its own
instruction oracle and packet/GS-state evidence before this limit is
considered resolved.

## AREA01 world model bank selection

### Scope and ownership

`em_area11_boxes_bind_world_bank(path, resource_word)` selects an exported
world model bank during area reset. `resource_word` is the canonical module
loader's `EmStatusSceneLoader.d28A490[0x43]`: the original `D_0028A59C`, not a
constant inferred from an area filename. The export's table address must
match exactly. Invalid input, a failed read/parse, a mismatched token, or a
current-generation owner still holding a model returns failure and leaves
the old bank intact. A successful bind expires borrowed world-bank/model
views. The caller must release those external borrowers before rebinding;
the adapter can check its own tracked owners only.

The sole existing owner remains `em_area11_boxes`: the parsed model bank,
`em_owner_services` translations, and the shared `001AF710` bone-slot stack
used by Roger and the other binders. Original callers `001B0FD0`,
`001B0EA0`, and `001C6120` continue through their existing translations;
there is no AREA01 copy of the allocator or model lookup. Parsing happens
before replacement, and skeleton pointers are rebased into the stable bank
storage after the parser's temporary result is installed.

`em_area11_boxes_reset` drops owners and resets the shared slots; it keeps
the selected world bank until an explicit bind succeeds. The unchanged
first-use fallback still selects the AREA11 export. The separate global
library (`D_0028A56C`, Roger's resource table), table-less library models,
and Roger exports keep their existing semantics. Bank selection does not
change them; the existing area reset still clears this adapter's library
view.

The nonallocating `em_area11_boxes_owner_fields` projects an already bound
world/library owner's canonical `+0x40`, `+0x44`, and `+0x4C` words. It
returns absent for an unbound or stale generation and does not create a
model. The existing `owner_world` and `owner_slot` APIs expose the same
owner's world matrix, bone records, and original slot addresses.

### Caller integration

After `001AFCA0` has reset the actor pool and `em_area11_bindings_reset`
(and the arrival adapter) has released the previous area, before any owner
allocates a model:

1. Obtain the current `EmModuleLoader` and its `EmStatusSceneLoader`.
2. Read `ld->d28A490[0x43]` after the area load has completed.
3. Call `em_area11_boxes_bind_world_bank` with
   `assets/scene_snow/world_models.emwm` for AREA11 or
   `assets/area01/world_models.emwm` for AREA01 and that word.
4. Propagate failure through the scene fault path. Continue owner creation
   only after a successful bind.

The bank-selector lane does not change scene bindings or Makefile targets;
those integrations are coordinated separately. No source capture is a live
input. Only the user's exported EMWM is opened by this API.

### Loader overlap correction

The new AREA11-to-AREA01 loader check exposed lost resident data in
`em_module_loader.c:region_for`. A shorter read at the same base retained
the old region's larger size. A later nested read then intersected that
obsolete extent, causing the whole region to be freed, including the
AREA01 top resources that the new read had not overwritten. Both
`em_module_loader_memory` and `memory_rest` returned no view at the top
resource base: this was data loss, not merely an unavailable contiguous
view.

The corrected storage replaces exactly the incoming interval and preserves
untouched prefixes/suffixes. It prepares the new mapping before changing
the old one, so allocation/capacity failure leaves existing memory intact.
Each new read retains its exact extent, preserving the DMA consumer's
boundary. The existing 16-region limit and fail-stop behavior remain.
`002009E0` is a store into existing memory, not a new disc delivery:
its BSS clear first uses a containing canonical allocation, preserving its
untouched guard bytes and extent, and allocates only if that span is not
already mapped. The AREA01 state full oracle checks the original clear,
32 untouched guard bytes, and 416 init cases over 16 captures (570,775,296
compared bytes). The original-instruction loader test compares every modelled store,
callee entry, frame state, delivered span, and DMA payload after the
transition. The formerly missing top span now remains readable and equal.
The memory APIs still return a contiguous view within one region; they do
not promise to concatenate adjacent independently delivered regions.

### Evidence

The new `tools/test_world_model_bank_reference.py` and narrow
`tests/world_model_bank_bridge.c` compare the actual shared adapter with
original instructions from the pinned boot ELF and the user's captures.
The bridge supplies no substitute model/allocator logic. Unexpected calls
to its out-of-scope Roger resource provider abort.

| Check | Quick | Full |
|---|---:|---:|
| Parsed model views compared byte-for-byte | 64 | 416 |
| Skeleton records compared to original resource bytes | 121 | 681 |
| Original `001C6120` executions (four masked IDs per model) | 256 | 1,664 |
| Original `001B0FD0` / `001B0EA0` executions | 128 | 832 |
| Allocated bone records compared, including matrices | 242 | 1,362 |
| Invalid-input / active-owner bind rejections | 24 | 24 |
| Relocated loader-token rejection | 1 | 1 |
| Additional AREA01 captures | 0 | 16 |

Quick checks AREA11 -> AREA01 -> AREA11 selection and both original owner
initializers for every model. Full repeats all AREA01 models against every
available sub-0 capture returned by `export_area01_common.captures`,
including the first-level exit arrival. Both check the library view and
library word remain unchanged across a successful world-bank selection,
and validate owner metadata projection for every world-owner bind.

The native and original loader execute module 3 in 8 dispatches, then
AREA11 in 13, AREA01 in 17, and a direct AREA11 return in 13. The sound
transfer is an explicitly supplied boundary: its returned container end
comes from the delivered file's length expression, independently on both
sides. Four sound-boundary calls occur across the four area loads below;
this does not establish sound-transfer timing or sound residency.

| Delivered bank | Canonical table | Models | EMWM resource span bytes |
|---|---:|---:|---:|
| AREA11, after module 3 | `0x01335F40` | 21 | 544,928 |
| AREA01, after AREA11 | `0x01781140` | 22 | 288,448 |

For each accepted selection, the loader's slot `0x43`, original loader's
word, capture word, and EMWM header agree. Every exported span byte equals
both native delivery and the original loader's delivered memory. This
checks the actual loader-to-bank identity, not just the filename/header.

A direct AREA01 -> AREA11 load without reloading module 3 produces table
`0x01336CC0` in **both** original and native execution. The fixed-address
AREA11 export is rejected. Reloading module 3 (8 dispatches) and then
AREA11 (13 dispatches) restores `0x01335F40`; its bank is accepted and
byte-equal. The selector does not invent a relocation or silently reuse
old handles. A broader route that legitimately uses another placement
needs corresponding exported-resource/address handling.

Observed runs (all PASS):

- New bank quick: 4.1 seconds; full: 4.83 seconds.
- Existing owner-services quick: 120 bind cases, 12 bone initializations,
  16 placements, 200 SDK sets, 416 publication cases, 24 draws, 176 rumble
  cases, 18 countdown cases, 40 captured owners, and 4 captured palette
  uploads; 1.3 seconds.
- Existing owner-draw quick: 700 culling cases, 160 packet cases,
  84 lookups, 60 captured owner draws, and 32 indicator draws; 1.4 seconds.
- Existing module-loader quick: 120 read cases, 25 polls, 12 DMA cases,
  184 packet/restore cases, and 3 additional whole modules; 7.9 seconds.
  Full: 5,544 read cases and 45 additional whole modules; 12.1 seconds
  after the BSS store-path correction.
  Both retain the 13 page-module checks, 16,000 GS-block comparisons,
  17 ITEM-atlas sprite checks, pinned loads, measured-drive checks,
  and sanitizer harness checks reported by that suite.

Logs are ignored outputs under `build/level2/oracles`: `model_bank.log`,
`model_bank_full.log`, `module_loader_bank_fix.log`,
`module_loader_bank_fix_full.log`, `owner_services_bank.log`, and
`owner_draw_bank.log`. The new structured report is
`build/level2/oracles/model_bank/report.json`. Reports contain counts and
metadata, not original instruction/data dumps.

This evidence establishes bank selection, original owner initialization,
and the loader handoff on the tested transitions. It does not establish
AREA01 actor scheduling, gameplay closure, final draw composition, or a
completed live first visit; those need the separate adapter integration.

## AREA01 generic model and animation workers

`em_area01_model_live.c` adapts the original model/pose owners to any live
AREA01 pool record through `EmArea01ActorView`. It does not select Roger's
pair zero, create another actor image, or allocate a second bone stack.
The physical slots, free-slot array, count and head are the existing
`em_area11_boxes_slot_world()` views. Temporary typed structures serialize
the fields an existing translation reads; the actor view owns all other
record bytes.

### Original ownership and bindings

| Original workers | Sole existing translation used |
| --- | --- |
| `001B0FD0`, `001B0EA0` | boxes' existing owner-services bind and selected world bank |
| `001B1020`, `001C6380` | `em_owner_services_original` using call-local record/skeleton views, canonical loader library bytes and the same shared slot allocator |
| `001B10B0`, `001C6150`, `001AF780`, `001AF890`, `001AF800`, `001CA6E0`, `001CA6F0` | `em_roger_actor_original` over the generic actor and shared slots |
| `001CA5E0`, `001CA5F0` | `em_owner_services_original`; the method table formerly private to status models is now shared, and status models calls that same helper |
| `001CB5B0` | existing `em_anim_rest_001CB5B0` over the canonical global pointers |
| `001C2360`, `001C22A0` | existing `em_rvr` indicator binds; see [LEVEL2_SERVICES.md ("AREA01 canonical indicator children")](LEVEL2_SERVICES.md#area01-canonical-indicator-children) |
| `001C5C90` | existing Roger equipment owner over its actual parent record; see [LEVEL2_SERVICES.md ("AREA01 equipment child")](LEVEL2_SERVICES.md#area01-equipment-child) |
| `001C62C0` | `em_owner_services_001C62C0`, with temporary skeleton/bone views copied back to the canonical slot bytes |
| `001C63E0`, `001C67E0`, `001C68C0`, `001C64F0` | `em_pose_host_workers` and `em_player_stage_anim_advance`, using the actor, shared slots and loader clip bytes |
| `001BA8E0`, `001BA580`, `001BA540`, `001CA700`, `001CA770`, `001D06D0`, `001D06E0`, `001D8BF0` | existing Roger actor translations with generic record/resource views |
| `001D0C70` / `001D0720` | existing opening-face kernel and caller-supplied canonical RNG |

`001D0C80` and `001D0D40` remain in the AREA01 math owner. Their model and
allocation calls can use this adapter, including their direct writes to the
generic record's `+0x110` slot words. Generic animated records retain their
metadata in the actor view; existing boxes-owned world/library records use
the boxes projection. The world bind initially publishes typed slots to the
raw arena; generic workers then adopt raw results into the typed view. They
never overwrite direct overlay bone stores with an older typed snapshot.

Each native worker ends an active actor-view segment, executes, commits its
changes and resumes the segment if needed. External `001F0120`, `001DA6A0`
and area-13 `001BA7F0` calls are explicit callbacks with the same commit and
refresh boundary. They are required only when reached. World binds preserve
the old `+0x40` animation word and `+0xD0` matrix, which the original binds
do not write, when the actor first gains a boxes service view.

`EmArea01ModelSource` receives the canonical table, loader resource views,
scene bytes, `D_00275B40` / `D_00275B48`, RNG and `EmPoseGlobals`. The latter
contains the root binder's canonical sampler and scratch views. No private
persistent scratch image is introduced. `001C68C0` maps the actor and slots
without requiring an animation bank, matching its original reads.

### Checked evidence

`tools/test_area01_model_live_reference.py` builds
`tests/area01_model_bridge.c` with the production adapter, real actor pool,
actor view, shared stack and the existing translations. Captured resource
bytes are inputs to this test only. The production adapter reads its
caller-supplied canonical loader views.

The latest default quick run passed **49 cases**, **2 external boundaries**, no RNG
boundaries, in **3.65 seconds**. The full run including library-model allocation and placement passed **735 cases over 15
AREA01/subarea-0 captures**, with **30 external worker boundaries** and **1
RNG boundary**, in **18.18 seconds**. Both runs additionally pass **2 retained-library
lookup cases**, **2 pool free/free-then-reuse cases** and **4 resource-failure
boundary cases**, plus **2 world-bind/direct-bone-store/placement cases**.
Every original-instruction case compares the complete
32 MiB RAM image and 16 KiB scratchpad after executing original instructions
and native code, including shared-slot allocation/free state, actor bytes,
canonical bone-array publication and pose scratch. Integer return bits are
also compared for the functions with defined results. The external head,
shadow and area-13 workers are recorded no-op boundaries in this oracle;
these counts do not establish their live binding or rendering.

Cases include all 13 method-table entries and out-of-range unsigned kinds;
animated/unanimated `001B10B0`; default bones, clip initialization, sampling,
blend and matrix propagation; face setup/tick/teardown; slot release; six
world-bind combinations; four library-model ids through `001B1020`; and `001D0C80` followed by `001D0D40` through sparse
actor memory and the same model workers. Full logs are ignored files under
`build/level2/oracles/model_equipment_regression_full.log`.

The library cases deliberately empty the area loader's table row `0x37` and
borrow a separate canonical `source.library_word`, including model `0x72`.
The live word is the existing Roger global-library owner's `D_0028A56C`, whose
resource storage is retained across area delivery. A library table copy is
used only inside the oracle fixture to distinguish those two owners.

`em_area01_model_release_prepare` captures a call-local projection of private
model slot words while the actor view is active. After commit, the actual
pool free clears `self` and invokes `em_area01_model_release_native` from its
bone hook. This calls the sole `em_roger_actor_001AF800` translation over the
existing slot stack, updates canonical private `+110` words and native bone
counts, and never begins a transaction on the no-longer-live record. The
token is consumed once and rejects a different actor/generation or projected
shared slot words. Typed Box, face and equipment owners keep their existing
release paths. The tests execute original `001AFC10`, then `001AFA90` in the
reuse case, and check full RAM/scratch equality including retained private
bytes. Resource failures verify that the model call restores the caller's
active/inactive actor-view state while keeping the original missing resource
address as the first failure.

### Integration limits

This is standalone adapter evidence, not a live AREA01 smoke result. The
root AREA01 binder must supply all source pointers and resource views and
route named model workers here. It must reset this view after the actual
pool/shared-stack reset, never reset the shared allocator independently.
Resource storage must outlive any call and render unit that references it.
Clip reads currently require the bank range to fit a delivered loader span;
missing or fragmented reached spans fail rather than borrow captured bytes.
Model projections validate separate actor spans around `+0x24`; no worker here
reads the effects owner's potentially uninitialized word at that offset.
Lifetime-only observation uses `em_area01_actor_view_touch`.
Unknown workers, absent resources and invalid slots latch a fault. Structural
view validation can precede the original's first write; the oracle establishes
exact behavior for valid mapped cases, not identical exception ordering for
invalid original pointers. Generic drawing and shadow selection are separate
bindings and are not established by the model/pose test above.

## AREA01 generic model drawing and shadows

`em_area01_model_draw` builds temporary typed views over an AREA01 actor
record, the existing shared slots and canonical loader resource bytes.
`em_area01_model_draw_call(draw, function, record)` brackets the actor-view
transaction and dispatches `001CAA00`, `001CACB0`, `001CB360` or `001DA6A0`.
No Roger pair is selected and no model, bone or texture allocation is added.
The reusable bank structure is rebuilt each draw; resource bytes remain
loader-owned and must stay alive through that frame's flush.

`001CAA00` and its attachment use `em_owner_draw_live_001CAA00_attached`.
`001CACB0` forwards its record's model to the existing live `001CABA0` owner.
`001CB360` still executes the AREA01 math owner's four-call sequence. Its
new live adapter binds `001C7420`, `001CB2C0`, `001D1F80` and `001D3F50` to
the existing owner-services, animation-rest, render-context and face-attach
translations over the same channel, light owner and packet arena. The unit
is parsed immediately and retained in the existing frame draw list.

The captured `001C02E0` carrier at `0x007A93F0` uses `001CB360` with two
bones. The object-unit parser and runner previously required exactly one
bone for a face/morph unit. They now allow the four 8-qword nodes that fit
before the original face program's first batch buffer at dmem `0x20`.
The same original face kernel processes them; no new rendering path exists.

### Draw evidence

`tools/test_area01_model_draw_reference.py` exercises the production model
view adapter and production live draw owner. A test-only render-context
provider supplies captured inputs; light, face, veil and packet workers run
their existing translations, with no expected-output callbacks. Each case
executes the original draw method and compares all 32 MiB of RAM and the
complete 16 KiB scratchpad. Candidates are active captured pool records with
nonzero bone count and a `001CAA00` or `001CB360` method/model. This is a
worker oracle, not a claim that every candidate's behavior requests a draw
on the captured tick.

| Run | Captures | Draw cases | Parsed units | Packet bytes | Morph VU1 cases | Morph triangles | Seconds |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| quick | 1 | 2 | 2 | 928 | 1 | 0 | 5.80 |
| full | 15 | 836 | 365 | 207,888 | 15 | 450 | 28.17 |

The carrier and NPC cases also execute `001C68C0` before and after the draw,
using one physical scratch view for pose and draw. All RAM and scratch bytes
match after **30 full / two quick pose → draw → pose sequences**.
Both modes additionally pass **four failure-boundary cases**: a missing model
resource and an unknown draw method, each entered with the actor view active
and inactive. Failure restores that entry state unless the actor view itself
is faulty, so the outer caller can commit earlier writes while preserving the
primary model/draw fault. Logs: `build/level2/oracles/model_draw_boundary_quick.log`
and `model_draw_boundary_full.log`.

Morph packets are also executed through the original VU1 program and the
same native object-unit renderer; every triangle agrees. The quick capture
clips all carrier triangles, while the full capture sweep includes 450
visible triangles. Existing object-unit quick checks passed 35 ordinary
units / 12,777 triangles, 13 class-2 units / 50 triangles, two single-bone
face units / 2,172 triangles and 13 parser refusals. Existing face-attach
quick checks passed 21 captured draws, 30 synthetic cases, 645 callee-entry
comparisons, three VU1 units / 3,220 triangles and 24 fail-stop checks.

### Shadow resource selection and evidence

`em_shadow_live_select_area(area, subarea)` transactionally selects the
existing shadow owner's receiver data for AREA11/0 or AREA01/0. Unsupported
keys or bad files leave the prior receiver set intact. A swap is refused
while a current-frame pass remains unflushed. Selection preserves the
shared player/Roger proxy models and persistent original `D_00817FF0`.
Every shadow draw checks that the selected receiver key matches the scene.
Call the selector after completed original area resource delivery, before
shadow bind/draw.

`tools/export_area01_shadow_receivers.py` reuses the existing EMSR serializer
and original static-object validation. It reads the AREA01 bank through the
disc-derived loader map, plus the original library's two box models. Capture
bytes only verify the result. The ignored asset is
`assets/area01_shadow_receivers.emsr` (`tools/export_area01_shadow_receivers.py`);
it is deliberately outside `assets/area01`.

The export contains **693 receiver objects**, **1,277 blocks**, **4,096 grid
words** and **two box models** in **2,658,072 bytes**. Its source table is
`0x014DC940`. All source grid/object/box bytes (**43,539,456 compared bytes**)
match **16 AREA01 captures**. This does not independently prove that an
arbitrary future live loader delivery selected the right key; the root
binder supplies the completed delivery's canonical area/subarea.

`tools/test_area01_shadow_live_reference.py` exercises the generic actor
hook and shared shadow owner with that exported receiver set, then executes
the original `001DA6A0` instructions. It compares light/state matrices,
original box uploads, worker order and receiver ids/classes. Each capture
runs its actual clip matrix and a synthetic zero clip matrix to reach the
full shadow path. The selector is also checked for AREA01 → AREA11 → AREA01,
unsupported-key refusal and refusal while a drawn pass is pending.

| Run | Captures | Cases | Drawn plans | Receiver calls | Original packet bytes | Seconds |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| quick | 1 | 3 | 2 | 0 | 11,360 | 5.47 |
| full | 15 | 60 | 49 | 214 | 320,816 | 10.36 |

These totals include **30 full / one quick `001BA580` → actual shared
`001DA6A0` → face-step sequences**. The native model worker commits its actor
before the shadow adapter, which enters with the actor view inactive, then
refreshes the same private actor bytes before the original face tick. The
full sweep exercises both activity-byte values 1 and 2. The complete actor,
ten activity bytes and entire shared bone arena agree with the original
`001BA580`/face instructions, whose shadow call is checked by the separate
complete shadow-chain oracle in that same case. Thus the shadow output and
the face continuation are independently checked at their exact boundary;
this does not claim whole-RAM equality across the shadow renderer. RNG is an
explicit fixed-return boundary, as in the model test. Logs:
`build/level2/oracles/shadow_face_quick.log` and `shadow_face_full.log`.

The shadow test is headless: it proves the existing plan/pass binding and
receiver selection, not GPU pixels. The current shared actor-shadow API
supports its existing 21-node player/Roger form; unsupported proxies or
other node layouts fail. Head-spawn and area-13 alternatives remain explicit
boundaries in the model oracle.

### Remaining integration scope

The new adapters are independently checked and still require the root live
composition and native AREA01 smoke. Root must route `001CB360` to the draw
adapter rather than execute the math sequencer with unbound packet workers.
Texture selection uses the shared world-texture owner. Unknown draw methods
fail. `em_owner_draw_live_memory` exposes the existing draw owner's `3400..34BF`
scratch and light rig without allocation or copying. `em_rcl_scratch_3400_bind`
borrows the same first 128 bytes; it neither initializes them nor chooses a
last writer. The caller explicitly seeds from the original routine that ran
last and repoints the existing pose globals to the same matrices. In AREA01's
first pool callback, frame order makes RCL head the last writer. Binding NULL
hands the current borrowed bytes back to RCL's existing default storage before
area reset. Unaligned pointers are rejected without changing the active view.

`tools/test_area01_scratch_views.py` passes **four handoffs, five cross-view
writes, one invalid-alignment refusal and 128 restored bytes** (4.8 s). It
checks pointer identity as well as contents. The existing render-context
quick oracle also passes **three captures × 65 entries**, with seven worker
calls and eight chain kicks per capture (5.9 s). No new persistent scratch
owner was introduced here. Logs are ignored files under
`build/level2/oracles/model_draw_*.log` and `shadow_live_*.log`.

## C69A0 live pose binding and shared status stages

`em_area01_model_call(..., 0x001C69A0, ...)` now runs the existing complete
`em_area01_math_001C69A0` translation. Its root scaling and post-blend bone
matrix stages are also the status renderer's implementation. There is one
matrix calculation, with two storage adapters. This is worker and original
instruction evidence; it does not establish a completed native AREA01 route.

### Original callers and storage

The original `00128C10` state-1 tail advances animation, copies `70003000`
to `70003400`, and calls C69A0 before visibility-dependent drawing.
`0012A5D0` does the same except in substate 7; `00129FC0` has the equivalent
tail. The status caller `0020EC80` constructs its object transform, saves a
copy at `700036A0`, then calls C69A0. These callers were checked in the
decomp's corresponding C files. C69A0 is NEARMISS: both
`src/func_001C69A0.c` and its original matching-assembly file were read;
the tests execute the original ELF instructions.

C69A0 reads actor `+60` scale, unsigned `+0C` count and the `+110` bone
pointer table. It writes each bone's `+90` matrix and the root, animation,
rest, blended quaternion and quaternion-product scratch windows at
`70003400`, `70003440`, `70003480`, `70003600` and `70003760`. Root row w
lanes and quaternion-product padding stay unchanged. The count is read
again each iteration; signed parents other than -1 are used as indices,
including a mapped negative, forward or self reference.

The AREA01 adapter borrows the active actor serialization view, the sole
boxes/Roger raw slot arena, and existing pose globals. Its appended
`EmArea01ModelSource.scratch3480` borrows the render owner's 64-byte rest
matrix. It creates no arena, model cache, bone allocation or persistent
pose projection. At the existing native transaction boundary, raw slots
are imported into any Box view before a subsequent typed draw. They are
never overwritten from a stale typed snapshot at C69A0 entry.

The four nested leaves are existing owners: `em_pose_host_001CA0A0`,
`em_pose_host_001CA1C0`, and owner-services identity/Euler. Every other
callee or unavailable span faults. A missing rest span does not fault a
zero-bone actor; only the root stage runs. A failure retains preceding
writes, preserves the caller's active/inactive transaction state, and
blocks later calls through the same adapter.

### Status consolidation

The former `em_status_models_pose_001C69A0` recomposed the calculation via
`em_pose_channels_matrix` and C9610; its live wrapper separately repeated
root scaling. Both calculations are removed. The status storage bridge
calls `em_area01_math_001C69A0_root` and `_bone`, the same stages used by
the complete C69A0 entry. It supplies the quaternion already computed by
the existing status channel evaluator, explicitly at the post-nlerp
boundary. It does not invent raw keys or a blend fraction.

Live status uses its existing slot world matrices and publishes root and
animation scratch into its existing `EmStatusSceneScratch`. The remaining
rest/quaternion/product scratch has explicit storage in the same status
owner. The public compatibility helper retains its previous input/output
contract: immutable object input, output world matrices with a zero initial
parent palette. The live wrapper uses actual previous slot matrices.

### Verification

- `tools/test_area01_model_live_reference.py`: quick PASS, 50 existing
  model/pose/face cases, plus one caught original actor C69A0 call, 17 C69A0
  field variants, two raw/typed slot continuity cases and 12 scratch/fault
  contracts. Default run remained about five seconds.
- Full mode: PASS, 750 existing cases over all 15 AREA01 endpoint captures;
  54 C69A0 calls caught while original actor owners execute, 255 field
  variants, 30 raw/typed continuity cases and 12 contracts. Runtime 43.03 s
  while other verification ran. Six endpoints did not reach a C69A0 call
  through those actor owners from the endpoint state: a01_04, a01_05,
  a01_s0, a01_s2, a01_s5, a01_s7. Their field cases still execute the full
  original function. No caller catch is claimed for them.
- `tools/test_status_pose69_reference.py`: quick PASS, 17 complete original
  C69A0 cases, four compatibility cases and three refusal contracts in
  3.12 s; full PASS, 145 complete original cases, four compatibility cases
  and three refusal contracts in 10.4 s. Refusals preserve the public helper's output buffer. The actual
  live status worker is called.
  Its fixture supplies already blended channels through the existing
  native nlerp owner; the oracle executes C69A0 and every leaf unmodified.
  Complete RAM and scratchpad bytes are compared. Cases cover count zero,
  count one, distinct/negative/saturating scales and translations, both
  quaternion signs, blend below zero/at zero/in range/at one/above one,
  and forward/self parents.
- `EM_TEST_FULL=1 EM_TEST_JOBS=2 tools/test_area01_math_reference.py`: full
  PASS in 539.1 s over 12 beats. The existing complete C69A0 owner passed
  756 cases, 32,940 worker boundary calls and all four branch outcomes.
  The wider unchanged suite also passed its actor, owner, script, player,
  bone-init/bind and table cases; this is independent of the adapter tests.
- Existing `status_models_test` under ASan/UBSan: PASS at captured walk 10;
  all 27 node world matrices bit-exact; 63 mode-1 light operations and
  their resets preserved. The fixture's expected failure cases still fail.
- `tools/test_census_unverified_reference.py`: quick PASS, 4,451 equal
  comparisons over seven rows, with its pre-existing documented divergences
  unchanged. The isolated model link includes the new shared dependencies.

Receipts are under ignored `build/level2/model-live/` and
`build/level2/status-pose69/`. No capture or original bytes are source files.

### Shared edits and integration

- `em_area01_math_actor.c/.h`: extract the unchanged root and post-nlerp
  matrix operations into shared stages; complete C69A0 retains its blend
  calls and loop order.
- `em_status_models.c/.h`: replace the status calculation with the field
  view above; document the compatibility versus live storage contracts.
- `em_area01_model_live.c/.h`: bind complete C69A0 with borrowed storage and
  original leaf owners. The existing model bridge/reference test adds
  composition and failure evidence.
- `test_census_unverified_reference.py`: link math-core, math-actor and
  pose-host objects for its isolated status-model build. Other unused
  workers retain that test's existing explicit traps.
- New `tests/status_pose69_bridge.c` and
  `tools/test_status_pose69_reference.py`: direct original-instruction
  evidence for the consolidated status bridge.

The integrating host must set `.scratch3480` from
`em_owner_draw_live_memory(0x70003480, 64)` and dispatch C69A0 to the model
adapter. The status-model test link now requires math-core, math-actor and
the existing pose-host dependency set; the main program already links them.
No scene, live-adapter or Makefile edits were made by this lane. Those
integration edits and the route smoke remain with the integrating lane.

## AREA01 light worker binding

The new `em_area01_light_live.c/.h` composes existing owners. It allocates
no actor, light, resource, scratch, or packet storage. `001C4FA0/001C50B0`
remain in `em_area01_light_owner`; their runtime caller already supplies
the owner's `entry_sp - 0x40` to its workers.

| Entry | Existing owner | Nested boundary |
|---|---|---|
| 001F5490 | `em_area00_fx_001F5490` | 001C22A0, then 001C6380 only on setup success; sp minus 0x20 |
| 001C5050 | `em_area00_world_001C5050` | 00102948, then 001D7FA0; sp minus 0x40, local vector at entry sp minus 0x10 |
| 001F5F60 | `em_area00_fx_001F5F60` | existing SDK workers, 001D3990, 001CAAC0; sp minus 0xA0 |
| 001D7FA0 | `em_point_light_register` | the sole `em_rcl_point_lights()` pool; signed handle preserved |
| 001D80B0 | `em_rcl_001D80B0` / `em_frh_001D80B0` | release in that same pool |
| 001D3990 | `em_owner_draw_001D3900` | selector 3, existing RCL veil GS state worker and channel cursor |
| 001CAAC0 | `em_anim_rest_001CAAC0` | existing packet-chain 001CB760, same page and parsed-unit cache |

The original C and original assembly for 001F5490, 001C5050 and NEARMISS
001F5F60 were read. The last helper writes its own colour and single-node
CNTs, a normal class-2 object submit, and RET. Its colour/header VIF command
ordering differs from 001C7420. `em_object_unit_parse_light` accepts exactly
that form, one node and the object kernel, while the existing parser entries
retain their original acceptance rules. No packet is rewritten into another
form. Both paths use the same existing VU kernel and GS state interpretation.

### Canonical bytes and integration

`em_area01_light_call(host, call, fault)` handles F5490/C5050/F5F60 while
the runtime byte transactions are active. Its memory provider is authoritative;
a refusal faults without a secondary arena. C5050 borrows only the four
directly stored bytes of its stack local; its SDK and register workers use
the host's ordinary original-address resolver. Stack storage must stay at
the same host address for the duration of nested calls. Every worker retains
the actual original argument lanes and nested stack pointer.

For D7FA0/D80B0/D3990/CAAC0, call `em_area01_light_service_prepare` while
views are active, suspend the transactions, call
`em_area01_light_service_invoke`, then resume them. Prepare retains only
scalar inputs and call-local vectors, never an actor-view pointer. D3990
copies model +04; its body references the canonical model address. CAAC0
copies its stack position and resolves immutable REF resources through RCL
then the supplied host. These resource bytes must survive the frame flush.

Required memory is the actor's +80/+B0/+C0 vectors and +44 model pointer;
the nested stack; D275670, the live context channel-3 cursor, packet arena,
GS and skin blocks; scratch 70003400..347F and 70003AC0..3AFF; and the
existing effect constant at 0026EB50. Rendering keeps the usual combined
16-unit channel-3 cache limit and refuses unsupported packets or resources.

Root integration edits, deliberately outside this adapter's ownership:

- Add the new adapter source plus existing `em_area00_fx_exit.c` and
  `em_area00_world.c` to the native build if absent.
- Route the three helper addresses through `em_area01_light_call` with
  the existing runtime host and fault latch.
- Route the four native services through prepare/suspend/invoke/resume.
- Keep the existing SDK, C22A0/C6380, RNG, actor free, loader, and RCL
  owners. No parallel pose or point-light pool is needed.

Shared edits are limited to raw-entry/page helpers in
`em_owner_draw_live.c/.h`, the explicit parser entry in
`em_object_unit.c/.h`, and correction of the arrival-fixture description in
LEVEL2_RUNTIME.md ("AREA01 flicker-light owner"). The adapter and test files are new. No scene, live
dispatcher, Makefile, asset, or main-checkout file is edited by this change.

### Reachability

AREA01 placement record 40 is at 0082C390 in the delivered overlay's
0082BD50 table. Its subtype byte becomes actor +3 = 7, its parameter +0D
is zero, and its callback is 001C50B0. Arrival has D0081075D = 0.
Consequently the original C4FA0 predicate is true at setup: C50B0 writes
lifecycle 3 and skips F5490/C5050/F5F60. On teardown it conditionally
releases the existing +20 handle, then frees the actor. These writes are
preserved; the binding never forces this actor into its active branch.

Both arrival endpoint captures have the record already freed with its old
callback word remaining. The recorded AREA01 beat census contains no
F5490/C5050/F5F60 calls. Its first D80B0 call is a01_07 row 530 in AREA00,
after the authorized row-529 arrival boundary. This does not prove that
D80B0 is absent from the earlier arrival-build interval. The adapter covers
the complete guarded helper path so an original state change stays supported.

### Verification

`python3 tools/test_area01_light_live_reference.py` compares original ELF
instructions with the actual adapter and existing SDK, pool, release, GS,
packet and depth owners. The fixture maps captured RAM as canonical test
storage; it is not a native capture bootstrap. Only C22A0/C6380 are explicit
scripted boundaries in this test, including successful and refused setup
and metadata writes. Their full bodies have the model adapter's oracle.

The test checks every worker entry's exact arguments, sp, actor/context,
packet/page bytes, scratch and local-vector contents. Full RAM and scratch
are compared after each call. It exercises both frame slots, fog-REF states,
free/full light pools, wrapping handles, found/missing releases, missing
views, failed workers, malformed calls and the persistent first fault.
The complete F5F60 packets are additionally executed by the original VU
microcode and the existing native kernel; every emitted triangle's GS
coordinates, depth, fog, texture, colour and STQ bits must agree. Altered
light headers fail and the default parser still rejects the light form.

Quick passes 16 cases, 46 exact worker boundaries, 1,440 packet bytes and
144 VU triangles in 6.28 seconds. Full passes 256 cases over 16 captures,
736 boundaries, 23,040 packet bytes and 2,364 triangles in 12.16 seconds.
Both check 15 adapter/service refusal contracts; quick/full additionally
reject 36/576 parser variants. Counts are saved in
`build/level2/light-live/quick.json` and `full.json`.
`EM_TEST_FULL=1` covers arrival and all 15 AREA01 endpoint captures.

The existing first-level `test_owner_draw_reference.py` quick regression
passes (700 cull cases, 160 submits, 84 lookups, 60 captured owner draws,
32 indicator draws). `test_object_unit_reference.py` also passes its
default suite: 12,777 ordinary-unit triangles, 50 class-2 triangles and
2,172 face triangles match the original microcode, with all 13 existing
parser refusal classes retained. Logs are beside the light reports.

This proves the composed helper path on those inputs and crafted boundary
variants. It is not a native AREA01 route-pass assertion.

## AREA01 channel-3 matrix upload

`00158D30` reaches `001F4A10(matrix, vector)` during its ordinary state-1
draw. The original caller produces a local color vector, uploads the supplied
matrix through `001C7900(matrix, local, 0x3F5, 3)`, looks up retained library
model `0x0C` with `001C6120(*D_0028A56C, 0x0C)`, submits it with `001D3990`,
writes RET, and inserts the completed packet with `001CB760`.

The existing `em_owner_draw_live_001C7900` allowed channel 0 only. It now
selects the existing channel-3 cursor and channel view for channel 3, then
runs the same `em_anim_rest_001C7900` owner and
`em_face_attach_w_001D88B0` → frame-render-head/light chain. Channels 1, 2
and other values remain refused. Channel 0 retains its existing unit
bookkeeping; channel 3 does not close, reset or replace a pending channel-0
unit. Both use the existing RCL arena, draw scratch at `70003400..700034BF`
and light rig at `00817BC0`. No second packet, rig or scratch owner exists.

`em_area01_matrix_service_prepare(host, call, service, fault)` validates the
four integer arguments and borrows exactly 64 matrix bytes and 16 token
bytes while canonical views are active. These read inputs are copied into a
call-local service object so the caller may suspend its views. Invoke
`em_area01_matrix_service_invoke(service, call)`, then resume views even on
failure. The adapter retains no state. `001F4A10` does not consume the
original upload's returned packet pointer; the existing live API returns
status only. This adapter therefore does not publish an emulated `v0`.

The host also calls `em_area01_matrix_service_page_begin(&start, &fault)`
before `001F4A10` and `em_area01_matrix_service_page_finish(host, start,
&fault)` after its successful return. Finish registers the completed
canonical packet through `em_owner_draw_live_page_register`; it does not
repeat the original `001CB760` insertion. It validates the final RET and
parses the existing object-unit layout into the sole shared page cache.
Registration writes no RAM, packet, cursor, chain or scratch bytes and
borrows model REF data for the remainder of the frame. The existing
`001CAAC0` light-unit path shares the same cache parser while retaining its
different original VIF layout and its own original depth insertion.

The retained-library lookup belongs to the existing Roger resource owner.
Its public `em_area11_roger_001C6120` checks the exported directory extent
and applies the original index/alignment rule. The AREA01 binding must also
check that its bank argument equals the canonical library table word; it
must not substitute a different bank when bytes are missing.

### Original evidence

`tools/test_area01_matrix_service_reference.py` builds the production live
owner and adapter against a test-only borrowed RCL memory provider. It runs
the pinned original instructions, including every lighting worker beneath
`001C7900` and every veil/packet worker beneath `001D3990`. Each boundary
compares the complete 32 MiB RAM and 16 KiB scratchpad byte for byte.

Actual captured `00158D30`/`00158BD0` records supply the matrices. Original
`001F4A10` itself creates the local vector and stack address before each
comparison. The fixture supplies input vectors `(0,128,0,128)` and, in full
mode, `(24,96,32,64)`; these are argument variations, not a claim that both
were recorded. Direct calls cover both channels and lighting modes
`-1, 0..7`, with channel 0 followed by channel 3 to verify cursor and
pending-unit isolation. No original instruction or capture data is embedded
in tracked files.

| Run | Captures | Direct uploads | Original caller cases | Exact worker boundaries | Refusal checks | Seconds |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| quick | 1 | 6 | 1 | 2 | 10 | 4.02 |
| full | 16 | 306 | 34 | 68 | 10 | 8.29 |

The ten adapter checks cover argument counts, unsupported channels, each
missing input span, a preexisting first fault and unknown-function routing.
The existing generic model-draw quick regression also passes: two draws,
two parsed units, 928 packet bytes, two pose → draw → pose sequences,
four failure-boundary cases, 6.10 s. Its standalone link list now includes
the existing math-actor owner needed by the model adapter's new `001C69A0`
route.

After sharing the page-cache parser, the existing light live quick regression
passes 16 cases / 46 worker boundaries, 1,440 packet bytes, 144 VU triangles,
36 parser refusals and 15 service contracts in 6.17 s. This checks that the
light unit's distinct VIF layout and original depth insertion still work.

Reports and logs are ignored files under `build/level2/matrix-service/` and
`build/level2/matrix-service-{quick,full}.log`. Suggested target:
`test-area01-matrix-service`.

### Current limit

This proves upload/submission worker memory behavior, not GPU pixels.
The completed packet's parsed page unit is also executed by the existing
native object-unit renderer and the original VU1 kernel, comparing every
triangle's vertex and texture words. The quick run passes one unit / two
triangles; full passes 34 units / 40 triangles. This final registration test uses the original caller's completed
packet: the exact native C7900/D3990 worker bytes are checked earlier in the
same case, while `001F4A10`'s remaining RET and CB760 work is independently
checked by the flame-service oracle. Five page checks refuse malformed RETs,
an invalid first packet and exhausted cache capacity without memory writes.
Root integration must bracket the actual caller with page begin/finish;
this proof does not substitute an end-to-end native frame test.
No arrival or full-frame fidelity claim follows from this worker oracle.

## AREA01 packet and context workers

`em_area01_rcl_workers` connects six original calls to existing owners over
the live render context. It owns no packet arena, chain table, context word,
resource bytes or persistent state. This is a standalone adapter proof;
it does not establish an AREA01 frame or route comparison pass.

| Original | Existing translation and canonical storage |
| --- | --- |
| `001CB5F0` | `em_packet_chain_001CB5F0`, using `em_rcl_packet_chain()`; returns the original packet address. |
| `001CB6B0` | `em_packet_chain_001CB6B0` on that same chain; preserves the full 64-bit address argument before the owner's original low-28-bit mask. |
| `001CB760` | `em_packet_chain_001CB760` on that same chain. |
| `001CB950` | `em_shadow_decal_001CB950`; its allocator worker invokes the sole packet-chain allocator and retains the original returned address. The TEX0 argument remains 64 bits. |
| `001D2E00` / `001D2DE0` | `em_render_context_001D2E00` / `em_render_context_001D2DE0` over a call-local view of the existing RCL slot word. No context bytes are copied. |

The current context span provides eight slot words at `context + 2520`.
Indices outside 0..7 are explicitly refused before the existing owner's
signed shift. The AREA01 water owner uses index 2. This does not claim the
original executable rejects out-of-range indices; they are outside the
adapter's supported canonical span.

The original sources are `func_001CB5F0.c`, `func_001CB6B0.c`,
`func_001CB760.c`, `func_001CB950.c`, `func_001D2E00.c`, and
`func_001D2DE0.c` in the read-only sibling decompilation. The packet builders
were already translated and original-instruction verified. The adapter
does not reproduce their clamping, DMA tags, cursor updates or TEX0 writes.

### Original AREA01 caller

Placement 35 at `0082C2C8` in table `0082BD50` has callback `001E7D20` and
model byte +0D = 0. Its state-0 setup initializes the grid through the
existing AREA01 state provider: `D_00275C20` names loader-owned BSS at
`0082CD00`. State 0 does not call these packet workers.

State 1 relaxes that grid, then its packet path opens 30 strips of 0x62
quadwords and two blocks of 5 / 9 quadwords. It appends the TEX0 packet
with **`0x20048BA199422040`**, reads context slot 2, appends the GS block
at `D_00275674 + 720` when zero or `+8A0` when nonzero, clears a nonzero
slot through `001D2DE0(2,0)`, and appends the `00234B00` CALL. This is
36 adapter boundaries for the zero branch and 37 for the nonzero branch.
The shared depth key is `0x1000` and table is `007635C0`.

AREA19's `7F5` / `7F6` progress branches are not executed by this AREA01
path. The oracle preserves AREA01's actual area key; it does not enable
those branches or provide a replacement water implementation.

### Integration

The stable interface is:

```c
int em_area01_rcl_workers_handles(uint32_t function);
int em_area01_rcl_workers_call(EmArea01Call *, uint32_t *fault_address);
```

Dispatch recognized functions directly inside the active byte transaction,
then propagate the returned fault through the composite's existing first
fault mechanism. These six functions touch only RCL memory and invoke no
actor or player callbacks, so they require no transaction suspension. A
returned packet address is resolved through `em_rcl_bytes_mut`; later
translated stores fill that same allocation. REF/CALL targets retain the
existing frame/resource lifetime rules.

The caller starts its fault word at zero. Success returns 0, an unknown
function returns 1, and a refusal returns -1 with the first failing original
address. Argument counts and floating-point lanes are checked. Missing RCL
storage is refused, and the existing packet owner verifies all append
spans before writing. A latched caller fault prevents further calls.

### Verification

`tools/test_area01_rcl_workers_reference.py` compiles the actual
`em_render_context_live` owner with this adapter. Captures seed test
fixtures only through the existing test hook. No live code loads captures.

Direct calls execute the pinned original instructions and compare the
canonical context, chain table and packet bytes after every call. They
cover negative and clamped depth keys, the `0xFFF000` exception, empty and
occupied slots, packet counts including the existing negative-count
semantics, full 64-bit REF/CALL/TEX0 arguments, all eight context slots,
and sign extension of returned slot words. Caller payload writes use the
returned canonical packet pointer on the native side.

The composed cases run the existing SYS translation of **the whole water
state-1 owner** against original `001E7D20`. At every RCL boundary they
compare full argument registers, stack address, packet/context/chain bytes
before and after the call, and every defined return value. Original nested
`001CB5F0` inside `001CB950` executes normally; it is not mistaken for a
second caller boundary. Final comparison covers all 32 MiB of RAM and
16 KiB of scratchpad. The RNG is an explicit shared scripted boundary;
native harness quadword copies use the exact copy contract. These tests
do not claim a new RNG or SDK-copy proof.

- Quick: **1 capture, 52 direct calls, 1 water case, 36 water boundaries,
  23 refusal/contract checks; 4.99 seconds**.
- Full (`EM_TEST_FULL=1`): **16 captures, 832 direct calls, 32 water cases,
  1,168 water boundaries, 23 refusal/contract checks; 12.27 seconds**.
- Contract checks cover unloaded RCL, missing/excess arguments, an unwanted
  float lane, unsupported slot indices, unmapped packet append with no
  writes, preservation of the first fault, and unknown-function routing.
- Receipts: `build/level2/rcl-workers/{quick,full}.json` and
  `build/level2/rcl-workers-{quick,full}.log`.

Suggested target: `test-area01-rcl-workers`, running
`python3 tools/test_area01_rcl_workers_reference.py`. No Makefile or live
composition files were edited by this adapter lane.

## AREA01 placed-prop adapter

AREA01 `00826CF0` calls `001C4820` when the actor's +3 byte is not one.
The generic adapter calls the sole existing `em_sul_001C4820` translation.
It borrows a call-local read view of the actor header and method word,
refreshing that view after model binding, placement and publication. The
method word is therefore read after `001B17A0`, as in the original. Free
does not reload the invalidated record. The adapter creates no persistent
model or actor owner and makes no record stores of its own.

The original reserves 32 stack bytes before calling its workers; the
adapter passes that same relative stack offset. Worker failures and missing
record spans fail explicitly. The existing directly bound AREA11 prop
keeps its native Box adapter.

`EM_TEST_FULL=1 python3 tools/test_area01_prop_live_reference.py` passes
**1,536 original caller cases**, all 256 lifecycle byte values, **32 exact
worker boundaries**, and two missing-memory/worker refusals. The test
changes the lifecycle and method at worker boundaries and compares the
entire actor record and exact worker entry/arguments/stack. Model, draw,
publication and free are explicit boundary effects; their separate
adapter proofs establish those workers. Receipt:
`build/level2/prop-full.log`. No integrated AREA01 route pass is claimed.

## Shared library models for AREA01 pickups

The first live AREA01 probe reached library lookup 001B1020 with a missing
global table word. The module loader's isolated EMML initial state only seeded
its cursor rows; it did not own the bootstrap library row 0x37. Root now supplies
the existing global library owner through the model source. A subsequent probe
resolved the table and exposed model 0x4D at 0xC799C0, whose body was absent
from the retained resource export.

`tools/export_roger_banks.py` now includes exactly two extra model spans from
the original global library `chunk27/f01_id37.bin`:

| Model ID | Runtime header | Header, blocks and skeleton bytes |
|---|---|---|
| 0x4D | 0xC799C0 | 6,384 |
| 0x58 | 0xC907C0 | 4,304 |

The AREA01 roster has library pickup IDs 0x4D, 0x58, 0x6C and 0x72. The last
two already lie in the equipment span 0x6A..0x7A. This dependency follows the
original 0015AC00/0015AFA0 and 00219550 initialization branches: library model
ID is actor +0x0D, with their existing class/item exceptions selecting the
world bank. Both NEARMISS initializers were checked against their original
assembly. The exporter walks original AREA01 group and placement descriptors
and asserts the census; it does not derive required resources only from which
pickups happen to remain in the final captured frame.

The two new spans carry the existing read-only library flag. They add no
animation-bank regions to the player's pose host. The EMRS now has 15 regions,
within the existing 16-region bound, and is 4,986,616 bytes. Every prior region's
metadata and bytes remain identical, including model 0x16's original disc
weights: the live sole owner still performs the original weight rewrite.

Validation command:

```
python3 tools/export_roger_banks.py --out build/level2/roger-library/resources.emrs --verify-area01
```

It passes the existing complete table/region checks in 16 AREA11 captures and
retained global table/library checks in all 16 AREA01 captures. For AREA01 it
also checks each live library pickup's +0x44 against the original table lookup.
Model 0x16 differences remain restricted to the original rewritten weight
bytes; no captured mutation is used as initial data. The existing AREA11
animation banks are area-specific and are not falsely compared with AREA01's
replacement delivery.

The exporter refuses writes through any symlink path component. The
installed `assets/scene_snow/roger/resources.emrs` is ignored and must not
be committed; an export older than this change lacks models 0x4D / 0x58. Local receipts are
`build/roger_banks/export.json`, `build/level2/roger-library/install.json` and
`build/level2/roger-library-export.log`.

### Retained door animation bank

Connected probe 13 passed the crate owners and stopped in the shaft-door
initializer at `001C63E0`. Original `001B0F60` reads the retained bootstrap
word `D_0028A574` (row `0x39`), while the area loader's isolated table does
not own that bootstrap row. The existing Roger resource owner now exposes
that exact word and the immutable door-animation bank it names. AREA01
borrows them through its resource view; the original model/pose worker is
unchanged. The bank is excluded from automatic AREA11 pose-host region
registration, so unrelated host region counts do not increase.

`export_roger_banks.py` adds the original `chunk27/f02_id39.bin`, 10,240
bytes at `00D191C0`. All 15 earlier region descriptors and bytes are unchanged.
The export now contains 16 regions and 4,996,872 bytes. The full existing
export verification passes in 16 AREA11 captures; the retained bank word and
bytes also equal all 16 AREA01 captures. Receipts: `build/level2/door-bank-export.log`,
`build/level2/door-bank/install.json`; exported file SHA-256:
`9c81f62a7e50081b2b69a316b9cbc8043466d5d8db58c09158e7abd79a76057d`.

Shared edits are the existing Roger resource getter API and exporter;
`em_area01_live.c` supplies strict read-only access to the word and span.
This resource proof does not establish a completed AREA01 world frame.
