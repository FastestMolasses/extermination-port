# Disc-sourced textures (lane TEX, release blocker)

Release blocker (FIDELITY_FEATURES.md, "disc-sourced textures"): the object,
chain-page (decal included), status-model and status-page textures and the
font atlas were decoded from PCSX2 captures or an EE RAM dump, which end
users do not have. This lane finds where the original loads each of them
from the disc and adds a disc-only exporter whose output is **byte-identical**
to every capture-derived file.

Result: **every first-level texture has a disc source.** No texture was
found whose source is not on the disc. The exporter
`tools/export_disc_textures.py` rebuilds all 15 capture-derived texture files
byte for byte from the disc image and the extract. The other exporters that
still decode textures from `opening_gs.bin` (flame, snow, level zones, props,
pickup lights, fence door, Roger, opening actors and faces, `player.emdl`)
get identical texels from the rebuilt GS memory (section 4).

New files (all untracked, lane-private):

| File | Role |
|---|---|
| `tools/export_disc_textures_gs.py` | the library: INDEX.IDX descriptors, each loader's section rule, the upload sequence, the GS image, the residency check |
| `tools/export_disc_textures.py` | the exporter (CLI) |
| `tools/test_disc_textures_reference.py` | the original-instruction oracle and the capture comparisons |
| `docs/DISC_TEXTURES.md` | this file |

No C module was needed: the runtime file formats do not change.

## 1. What each texture is and where it comes from

The GS local memory of the first level holds four uploaded regions (GS
block numbers, 256 bytes each; each upload is a whole-page PSMCT32 sheet,
256 texels wide, DBW 4):

| GS blocks | Sheet | Disc source (DATA.DAT offset, size) | In the user's extract | Uploaded by |
|---|---|---|---|---|
| 0x1B80..0x1BFF | 256x32 | module 3, resource slot 8 (0x214800, 0x8800) | `chunk03/f00_id08.bin` | 00200890 (from 001FFCD0 state 7 and 00200970) |
| 0x1D00..0x247F | 256x480 | module 0x1B, B section 0 = resource slot 0x35 (0xDA78000, 0x78800) | `chunk27/f00_id35.bin` | 001FF1E0(0x1B) at boot; 00200830(D_0028A564) in 001AD1A0 and 00200970 |
| 0x2A00..0x317F and 0x3180..0x377F | 256x480 + 256x384 | area 11 (INDEX.IDX sector 15), A section 1 (0x7732000, 0xD8800) | `chunk15/f00_id43.bin[0x4A800:]` + `f01_id42` + `f02_id46` + `f03_id41` + `f04_id96` + `f05_id97[:0x5000]` | 001FFCD0 state 4 through 001FF590(0xAB, 1) |
| 0x1D00.. (page rows) | 256xN | a status page module's A section (module 0x1F: `chunk31/f00_id00.bin`, 256x96; module 0x21: `chunk33`, 256x320) | the module's chunk | 001FF830(module) through 001FF3F0 |

Each capture-derived texture, by the upload it reads (the lane's
attribution: a texture is attributed to an upload when its decode reads only
that upload's blocks and equals the full decode):

| Asset (capture exporter) | Textures | Source |
|---|---|---|
| `scene_snow/object_textures.emot` (`export_object_textures.py`, route `gs.bin`) | 303 TEX0 | 158 library (module 0x1B), 135 area (sector 15), 10 player texture packet (slot 8: the player model's TBP 0x1B80..0x1BF7 textures) |
| `scene_snow/page_textures.emot` (`export_page_textures.py`, route `gs.bin`) | 6 TEX0, including the 001F8D30 decal 0x2004290511322469 (TBP 0x2469, CBP 0x2148) | all library |
| `status_models/*.emdl` (`export_status_models.py`, status-hub `gs.bin`) | 78 distinct TEX0 | 68 library, 10 player texture packet |
| `scene_snow/panel/status_hub_atlas.emha` (`export_status_hub.py`, status-hub `gs.bin`) | 13 tokens | all library (the hub itself loads no page module) |
| `scene_snow/panel/item_root.emir` (`export_item_root.py`, panel/root `gs.bin`) | 17 tokens | all ITEM root module 0x1F |
| `scene_snow/panel/battery.emba` (`export_panel.py`, panel `gs.bin`) | 27 tokens | all BATTERY module 0x21 |
| `font.emfn` (decomp `export_font.py`, EE RAM dump) | 409 tall + 256 small glyphs | not GS: module 0 resource slots 0 and 1 (`chunk00/f00_id00.bin`, `chunk00/f01_id01.bin`), loaded to 0xB00000 by 001FF1E0(0) in the boot main loop 001AAE40 |

The earlier note in SHADOW_DECAL.md 4 ("the uploader was not identified; no
BITBLTBUF aimed at it") is answered: the decal is a sub-rectangle of the
library sheet that module 0x1B uploads, so no transfer names its TBP.

## 2. Behaviour of the functions (from the decomp C and the executed ELF)

Every texture this lane exports is read from GS blocks written by
**00200830** (test B: every block the model's replay writes equals the
captures, and every exported decode reads only such blocks). 00200830
takes a buffer address and sends it on DMA channel 1 (VIF1) as a source
chain (through 00101F08, bracketed by two 00102468 calls). The buffer is a
chain of CNT tags ending in a RET or END tag; its data are VIF DIRECT
packets whose GIF data set BITBLTBUF / TRXPOS / TRXREG / TRXDIR by A+D and
then send the IMAGE data host-to-local. The replay of one buffer is the
decomp's `export_level._bg_section_chain` / `_bg_gs_upload` (proved
byte-exact for AREA01 in AREA01_ASSETS.md); it refuses any tag, VIF code,
register, transfer direction or pixel format it does not model.

The loaders read an INDEX.IDX sector (0x800 bytes) as a descriptor: +4 the
region's DATA.DAT offset, +8 its size, +0x0C (u16) the first upload entry,
+0x0E (u16) the A count, +0x10 the B count, +0x14 the resident offset,
+0x18 the nested count, +0x1C the pointer-table count, and from +0x20
8-byte {offset, size} entries followed by the pointer table (u24 offset,
u8 slot per word). The loaders differ in which entries they upload:

- **001FF1E0(id)** (boot bank loader; id 0 loads at 0xB00000, others at
  D_0028A734): reads sector `id` into D_00289BC0; reads each A entry into
  the base and sends it (the boot modules 0, 0x1B and 0x1C have none); reads the resident
  region [+0x14, +8) to the base; stores base + the resident size into
  D_0028A734 and D_0028A738; sends each B section (sizes from the entries
  after the A entries) consecutively from the base through 00200830; then
  sets D_0028A490[slot] = base + offset for each pointer entry. The base
  register itself is never advanced, so the B sections and the
  relocations use the region start (see the NEARMISS note below).
- **001FF830(id)** with **001FF3F0** (screen/page modules, through the
  001FF080 task): state 0 picks the destination by id (D_0028A738 for 2/3,
  0x01800000 for 1/0x27/0x28/0x29/0x37, D_0028A5A0 for 0x1D, D_0028A744 for
  0x32..0x35, D_0028A748 for 0x36 and the rest, 0x2A/0x2B by the flag
  0x70003B90) and reads the header; 001FF3F0 reads each A entry 0..count-1
  into the destination and sends it through 00200830; states 3..6 read the
  resident region and commit the cursor by kind; state 7 sends the B
  sections consecutively from the destination and relocates the pointer
  table; the record's status byte becomes 0x63.
- **001FFCD0** with **001FF590** (the area load, record in slot 2): reads the
  area overlay (D_0028A3C0[area]) and relocates it (002009E0); reads sector
  area + 4; 001FF590(0xAB, 0) reads entry 0 (the sound bank) to
  D_0028A73C and hands it to 001FB370; 001FF590(0xAB, 1) reads entries
  +0x0C .. +0x0C + count - 1 one at a time into D_0028A73C and sends each
  through 00200830; state 5 reads the resident region; state 7 sends the B
  sections, relocates the pointer table (after first + count + B entries)
  and calls **00200890**; a nested sub-area repeats states 8..11 with tag
  0xAC.
- **00200890**: sends one player texture packet by D_00810707 (mode) and
  D_00810C60 (costume): mode 0: slot 0xA for costume 2, 0xB for costume 1,
  otherwise 8; mode 1: 0xA, 0xB, otherwise 0xC; any other mode: slot 9.
  (`player_texture_slot` in the library.)
- **00200970(a)**: sends D_0028A564 (slot 0x35, the library packet); with
  a == 0 then calls 001CCB10 and, when the byte 0x70003B90 is 2, 00200890;
  with a != 0 then calls 00200890. 0020CDC0 calls 00200970(1) when a
  status page closes, so the library and player textures come back after a
  page module overwrote them.
- **001AD1A0** (New Game task): case 0 requests module 3 (001FF080(0, 3):
  resource slots 8..52, the player texture packets among them); case 1,
  once the load flag D_00275BD8 is clear, sends D_0028A564 and calls
  001D19D0.
- **001AB7E0** (boot task): step 3 calls 001FF1E0(0x1B) once 001ABC60
  reports done, step 4 calls 001FF1E0(0x1C) once 001ABE10 does. Before
  them the title modules 0x28 (001AB9D0) and 0x29 (001ABC60) upload at
  0x2A00; every block they write is rewritten by the area load.

**Two NEARMISS bodies are wrong.** Both decomp headers claim the body is
correct. The original's behaviour below comes from the executed ELF and
from this lane's synthetic cases:

- `src/func_001FF1E0.c`: the C advances `base` by the resident size before
  the B walk and the relocation loop. The original keeps the base register
  at the region start and only stores base + size into D_0028A734 /
  D_0028A738. The synthetic module case has a nonzero resident offset.
  Executed, it sends the B sections from the region start, as the model
  does.
- `src/func_001FF3F0.c`: state 1 builds the A-entry pointer from
  D_0028A488 (the DATA.DAT handle) + index * 8. The original indexes the
  descriptor D_00275C70, as 001FF590 state 1 does: entry = D_00275C70 +
  index * 8, offset = entry[0x20] + descriptor[4], size = entry[0x24].
  D_0028A488 is only the first argument of the read. In the synthetic
  page case, both A sections have distinct offsets and sizes. Executed,
  it reads them from the descriptor's entries, as the model does.

The chain should correct both headers and docs/NEARMISS.md in the decomp.
These are tracked files, so this lane did not edit them.

**Other callers of the uploaders.** A jal scan of the ELF finds these; the
overlays have none. This lane does not execute them:

- 00200830 is also called by 001ACA20 (a second New Game-style path that
  sends D_0028A564 like 001AD1A0) and by 00200360 (a streamer that uploads
  a buffer it reads).
- 00200970 is also called by 00203350 (the movie driver, argument 0),
  00225AC0 and 0022A650 (save / load screens). 0020CDC0 calls it from four
  sites.
- 00200890 is also called by 0015C1F0 (the transformation visual flip,
  which changes the mode byte). Every route capture 00..14 holds
  D_00810707 = 0 and D_00810C60 = 0, so the route shows slot 8 only.

In every route capture, the blocks the exported textures read hold
exactly the model's bytes (test B). So any upload these callers made on
the route left those blocks as the model has them. A state reached
through them (a movie, the save screens, a transformation) is not
modelled.

The route's sequence, which `FirstLevel.world()` replays:
001FF1E0(0x1B) B0 -> 001AD1A0's 00200830(D_0028A564) -> 001FFCD0's
001FF590(0xAB, 1) (sector 15 A1) -> 001FFCD0 state 7's 00200890 (slot 8).
`FirstLevel.page(state, id)` adds a page module, `page_close` the 00200970(1)
restore.

**Extract finding.** In a chunk whose descriptor has a resident offset
(sector 15: 0x123000), the extract tool placed each file at the pointer
entry's offset measured from the region start, but the loader measures it
from the resident base. The extracted names are therefore shifted by the
resident offset: the real resource slot XX of chunk15 is the region bytes
at 0x123000 + its entry offset. This explains STARTUP.md's note that
resource 0x98 is "embedded in chunk15/f12_id44 at 0xD0800" (0x1E1000 +
0xD0800 = 0x123000 + 0x18E800, slot 0x98's entry). The exporter reads
DATA.DAT through the descriptors and checks each buffer against the extract
by concatenating the chunk's files in order (they tile the region), so it
does not depend on the names. The oracle's area case confirms the rule for
every sector-15 slot: the bytes the original holds at each relocated
pointer equal DATA.DAT at region offset 0x123000 + the entry offset.

## 3. The exporter

```
python3 tools/export_disc_textures.py [--iso FILE | --disc DIR] [--extract DIR] \
    [--assets DIR] [--scratch DIR] [--only objects|page|font|status_models|status_hub|item_root|battery]
```

macOS arm64, pure Python, about 10 s. Defaults: `--iso
../Extermination/Extermination-rebuilt.iso`, `--assets assets/`, `--scratch
build/disc_textures/`. `--disc DIR` (a mounted disc or a copy of DATA/) goes
to the decomp's `BackgroundDisc` unchanged; only `--iso` was exercised in
this lane.

- `objects`: the capture exporter's TEX0 set and form checks
  (`export_object_textures.model_tex0` / `player_tex0`), decoded from the
  rebuilt world image with its `decode`.
- `page`: the TEX0 set from original sources only: the +0x70 TEX0 rows of
  the 001CFBE0 source blocks D_00253670 and D_002565E0 + 0x90 k (the ELF);
  the AREA11 flame descriptor D_00828340's TEX0 row (AREA11.BIN offset
  0x4E40 + 0x70); 001F8D30's decal TEX0 and 001F4D40 / 001F4BF0's glow
  marker TEX0, two values the code builds as immediates (the same constants
  as `EM_SHADOW_DECAL_TEX0` and `EM_STATUS_SCENE_TEX0_001F4BF0`, whose
  modules' tests verify them against the executed original). The capture
  exporter instead collects what the captured pages draw; the two sets are
  equal (test D).
- `font`: module 0's descriptor gives slots 0, 1 and 2 at resident offsets
  0, 0x3000 and 0x5000; the glyph counts are (slot 1 - slot 0) / 30 = 409
  and (slot 2 - slot 1) / 32 = 256, exactly `export_font.py`'s rule over
  the pointers D_0028A490[0..2]; the decoders and layout are `export_font`'s.
- `status_models`: `export_status_models.py` without the capture RAM (its
  RAM reads were checks; its glyph set is its constant GLYPHS); the texels
  through `export_native.build_texture_blob` over the rebuilt image
  written as a freeze blob.
- `status_hub`: the token list is what 00209DF0 passes to its sprite worker
  for hovers 0..4 x infection 0 / 100 plus the fixture pass (executed by
  `export_status_hub.Original`) and the five secondary icons that exporter
  lists; here the drawer runs over the ELF's initial data instead of the
  status-hub RAM. The resulting list equals the capture's (test D).
- `item_root`: `export_item_root.Original` already executes 0020F170 /
  0020F2A0 over the ELF image; texels from world + module 0x1F.
- `battery`: `export_panel.py`'s token table (D_00265C50, D_00265CD0 and
  its two constants) and texts; texels from world + module 0x21.

Every decode must read only uploaded blocks (`reads_only_covered`: the
decode is repeated with every other block filled with 0x00 and with 0xA5
and must not change), otherwise the exporter stops.

The two JSON sidecars (`object_textures.json`, `page_textures.json`) are
reports. No runtime code reads them, and the test reads only the capture
exporter's version (section 5 D). The disc exporter's versions drop the
capture exporters' `captures` key and add `source`. In the page set, each
`users` list names the original producers instead of the capture pages
that drew the texture.

`module_slot`'s extent (the bytes read for a resource slot) runs to the
next greater pointer-table offset, or to the end of the resident region.
The loaders keep no sizes. The test compares this extent with the
original's relocated pointers and cursors (section 5 A).

`build/disc_textures/first_level_gs.bin` is the rebuilt world image in the
freeze layout `gs_vram.read_localmem` reads; `report.json` lists every disc
buffer (caller, DATA.DAT offset, size, extract files) and the output hashes.

## 4. Other exporters that decode first-level textures from a capture

Their textures, recomputed from their own model files, decode identically
from the rebuilt image and read only uploaded blocks (test F):

| Exporter (STARTUP.md step) | Capture it reads today | Textures | Disc source |
|---|---|---|---|
| decomp `export_native.py` (6, `player.emdl`) | a GS dump (`--gsdump`; with `--attach` it refuses `--p2s`) | 80 (51 body + 29 equipment) | player packet + library |
| decomp `export_level.py` level zones (10/11) | per the manifest | 290 TEX0 over the 6 `*.gsmat.json` | area |
| `export_area11_props.py` (15) | `opening_gs.bin` (`--gs`) | 24 | area / library |
| `export_pickup_lights.py` (16) | `opening_gs.bin` (`--gs`) | 13 | library |
| `export_door_original.py` (18) | `opening_gs.bin` (hard-coded) | 5 | area |
| `export_area11_effect.py` (19, flame) | `--gs` | 1 | library |
| `export_snow.py` (21) | `--gs` | 1 | library |
| decomp `export_opening_actors.py` / `export_opening_faces.py` (26/27) | `--gs opening_gs.bin` | all 7 outputs rerun on the rebuilt image are byte-identical | area / library / player packet |
| `export_roger_resources.py` (39) | `opening_gs.bin` (hard-coded) | 68 | area |

Most of these also read capture RAM for their non-texture data or checks
(`opening_ee.bin`, `playable_ee.bin`, the reference VU dump); that is outside
this lane (see Known gaps).

## 5. Verification (`tools/test_disc_textures_reference.py`)

Default run: 4.5 to 6.3 s wall, about 8 s CPU (4 workers). `EM_TEST_FULL=1`:
21 to 25 s wall, 33 to 36 s CPU. A refusal (SystemExit) inside a pool worker is re-raised
as an ordinary exception, so a failing item fails the run instead of
leaving the pool waiting.

- **A. Loader oracle (original instructions).** FallEE executes, unmodified,
  over the route-04 RAM and scratchpad (full: also 00 and 14) the
  following cases:
  - 00200890 for mode x costume in {0, 1, 2, 3, 0xFF}^2;
  - 00200970(0 / 1) x 0x70003B90 in {0, 1, 2};
  - 001AB7E0 steps 3 and 4, and 001AD1A0 cases 0 and 1;
  - 001FF1E0(0, 0x1B, 0x1C);
  - 001FF830 + 001FF3F0 for modules 3, 0x1F and 0x21 (full: every module
    0x1E..0x31 and 0x37);
  - 001FFCD0 + 001FF590 + 00200890 for area 11.

  Each loader runs until its status byte is 0x63.

  **Synthetic descriptors** (kinds `module*`, `page*`, `area*`) are built
  from the user's disc at run time. The same loaders execute over them,
  and the same model reads them. Only the INDEX.IDX sector and its
  DATA.DAT region are replaced, in memory. The sections are the disc's own
  player texture packets. Together they cover every branch the route's
  data leave unexercised:
  - For 001FF1E0 (sector 0x1B) and 001FF830 (sector 0x1F): hdr[0x0C] = 2,
    which the module loaders ignore; two A sections; a nonzero resident
    offset; two B sections whose entry offsets point past the region (only
    their sizes count); four pointer entries, slot 0x35 among them.
  - For 001FFCD0 (sector 15): a sound bank entry 0; A entries 1 and 2; a
    gap; two B sections; three pointer entries; and two nested blocks.
    D_00810701 = 1 selects the second nested block, and each nested block
    has its own region, sound bank, A section, two B sections and pointer
    table.

  Every A and B size differs, so an entry-index slip moves a section.

  Hooks and what is compared:
  - The disc read 00200780 is answered by the handle's {LBA, size} from
    the disc image, or from its patched view in the synthetic cases. The
    polls return ready.
  - 002009E0, 001FB370, 001D19D0, 001CCB10 and the task calls are hooked;
    their arguments are recorded and checked only where stated here.
  - The hooked set equals each routine's jal targets.
  - **Memory is compared at every 00200830 entry**: the chain in memory
    equals the next buffer of the model, in order and in number.

  After each run:
  - every relocated D_0028A490 slot equals base + its entry offset. The
    base is the entry value of D_0028A734 for 001FF1E0, D_00275C74 for
    001FF830, and D_0028A73C / D_0028A740 for the area's top and nested
    blocks;
  - `module_slot`'s extent runs exactly to the next greater relocated
    pointer or to the original's end-of-region cursor, and its bytes equal
    the original's memory at the pointer;
  - the cursors the loaders write equal the model's values:
    D_0028A734 = D_0028A738 = base + the resident size; D_0028A73C for
    module 3; D_0028A740 / D_0028A744 / D_0028A748; the unchanged area base;
  - D_00275C70 equals the descriptor, or the selected nested block;
  - D_00810703 = area, and D_00810701 / D_00810704 are set;
  - the original's other stores stay inside the named globals (the task
    record, the cursors, the pointer table, D_00810701..04);
  - 00200890 and 00200970 store nothing.

  Not compared: 001FF830's choice of destination per module id (the model
  does not need it), and the memory at the other hooked callees' entries.
  Quick: 13 cases, 372 checks; full: 40 cases, 696 checks.
- **B. GS memory.** The 5,504 uploaded blocks equal the GS local memory of
  all 15 route captures 00..14, the status-hub capture and the opening
  capture. World + module 0x21 equals the panel capture, and world + 0x21 +
  0x1F equals the panel/root capture (19 captures in all). The title
  modules 0x28 / 0x29 write only blocks the area load rewrites.
- **C. Outputs.**
  - All 15 files carry the pinned SHA-256 of the capture-derived files
    (`CAPTURE_SHA256`, taken from `assets/` before this exporter existed),
    so the check stays independent once the exporter writes into `assets/`.
  - Every object and page texture decoded from the disc equals its decode
    from route capture 04 (full: all 15).
  - Every hub, BATTERY and ITEM root token equals its decode from its
    capture.
  - The font slots equal the captured RAM at D_0028A490[0] / [1].

  That makes 367 direct decodes quick and 4,707 full. The model's buffers
  for the world, the BATTERY and ITEM root states and the font equal the
  pinned table `PINNED_SOURCES` (7 buffers: caller, DATA.DAT offset, size,
  extract span). The world's step list is pinned too, so 001AD1A0's
  re-upload of slot 0x35 is part of it.
- **D. TEX0 sets.** The disc page set equals the pinned captured set (6).
  It is also compared with `page_textures.json` while that file is still
  the capture exporter's (it has a `captures` key). The hub list written
  equals `export_status_hub.py`'s list in `status_hub_commands.json` (13).
  Full mode also re-executes 00209DF0 over the status-hub RAM and checks
  that the exporter executed all 5 hovers x 2 infections and the fixture
  pass.
- **E. Controls**, all caught:
  - the costume-1 player packet;
  - the area's sound-bank entry 0 uploaded as a section (the replay
    refuses it);
  - no library upload;
  - module 0x1E in place of 0x1F for the ITEM root state;
  - swapped font slots;
  - an uncovered block holding 0x00, 0xA5 or its real texels (the
    residency check must refuse each);
  - a one-byte extract difference, and a span past the chunk's files (the
    extract check must refuse both).

  10 controls in all.
- **F. (full)** The other exporters of section 4: 453 textures are equal
  and resident, the 7 opening actor / face files of the reruns are
  byte-identical, and each of `player.emdl`'s 80 textures equals a disc
  decode.

`python3 tools/check_no_disassembly.py` is clean on the three new tools and
this document. The pinned digests are labelled as SHA-256 values, which is
what the guard's digest rule expects.

## 6. Binding (for the chain)

The live code does not change, because the file formats are identical.
The chain should:

1. Add a make target, e.g.
   `test-disc-textures-reference: ; python3 tools/test_disc_textures_reference.py`
   (the disc image is required; without it the test stops with a message).
2. STARTUP.md: replace these steps with one step,
   `python3 tools/export_disc_textures.py --iso /path/to/owned.iso`
   (class R):
   - step 4 (the font from a RAM dump);
   - step 34 (status models);
   - the texture part of step 32 (`battery.emba`, `item_root.emir`,
     `status_hub_atlas.emha`);
   - step 51 (object textures);
   - step 52 (page textures).

   Keep running `export_panel.py`, `export_item_root.py` and
   `export_status_hub.py`, but only for their non-texture outputs
   (`scripts.emsc`, `player_15c.bin`, `status_hub.emhs`, the JSON reports),
   until those are made disc-only. Run them before the disc exporter:
   they overwrite its files, with identical bytes. The disc exporter
   rewrites the two JSON sidecars of section 3. No runtime code reads them.
3. Pass `build/disc_textures/first_level_gs.bin` in place of a capture to
   the exporters that already accept one: `--gs` for
   `export_area11_props.py`, `export_pickup_lights.py`, `export_snow.py`,
   `export_area11_effect.py`, and the decomp's `export_opening_actors.py` /
   `export_opening_faces.py` (test F reruns the last two with it); `--p2s`
   for the decomp's `export_level.py` level zones. Three exporters cannot
   take it without editing tracked files, and so stay capture-bound (see
   Known gaps):
   - `export_door_original.py` and `export_roger_resources.py` hard-code
     `opening_gs.bin`;
   - `player.emdl`: `export_native.py --attach` accepts only `--gsdump`.
4. Retire the capture decode in `export_object_textures.py` and
   `export_page_textures.py` (their `main`), and `export_status_models.py`'s
   `main`. Alternatively, keep them as optional cross-checks that run only
   when captures exist. Keep the modules themselves: the disc exporter
   imports `model_tex0`, `player_tex0`, `decode`, `tex0_fields` and
   `drawable` from them. For the port, the decomp's `export_font.py --ee`
   path is superseded; its decoders stay in use.
5. FIDELITY_FEATURES.md: once steps 2 and 3 are bound, the
   "disc-sourced textures" blocker is met for the object, page, decal,
   status-model and status-page textures and the font. Evidence: section 5
   of this document. The fence door, Roger and `player.emdl` textures
   still come from captures until the edits named in step 3 are made.
   Also update:
   - the Visuals notes that say "decoded from PCSX2 capture GS memory",
     "Decal texture from PCSX2 captures" and "Page and decal textures come
     from PCSX2 captures";
   - SHADOW_DECAL.md 4 ("the uploader was not identified");
   - the decomp's NEARMISS headers for 001FF1E0 and 001FF3F0 and
     docs/NEARMISS.md (section 2).

## 7. Known gaps

- The exporter needs INDEX.IDX, from the disc image or a copy of DATA/.
  The extract alone keeps neither the upload section tables nor the
  resident offsets. The section bytes are also checked against the
  extract. Only `--iso` is exercised: no test covers `--disc`.
- Three capture-textured exporters still need a tracked-file edit before
  they can use the rebuilt image:
  - `export_door_original.py` and `export_roger_resources.py` need a
    `--gs` option;
  - `player.emdl` needs `export_native.py --attach` (or `export_props.py`'s
    blob builder, which it reuses) to accept a freeze blob instead of a
    `--gsdump` GS dump.

  Their textures are shown equal to the rebuilt image by content (test F).
  `player.emdl` itself was not re-exported: STARTUP.md step 6 does not
  record its full argument list.
- Only the states the route shows are modelled and checked against
  captures:
  - the world after a New Game (D_00810707 = 0, costume 0);
  - the BATTERY page (module 0x21);
  - the ITEM root page opened from the panel (0x21 then 0x1F). Its tokens
    read only module 0x1F's rows, so the texels are the same when it is
    opened from the hub.

  The other 00200890 variants are checked by the oracle (which packet is
  sent) but have no capture. The callers listed in section 2 (001ACA20,
  00200360, 00203350, 00225AC0, 0022A650, 0015C1F0) are not executed. The
  MAP, SPR4 and DATABASE page modules load correctly under the oracle
  (full mode), but their textures are not exported, because those pages
  are untranslated.
- The multi-section, resident-offset and nested branches are verified on
  synthetic descriptors only. No first-level data use them.
- The hub token list is computed over the ELF's initial data, not a
  runtime state, and it equals the capture's list. A future page whose
  tokens depend on the equipment state must derive them from that state.
- The page TEX0 set lists its producers explicitly: source blocks, the
  flame, the decal and the glow marker. A producer bound later that draws
  another TEX0 must add its original source here. The capture exporter
  would have found such a TEX0 only if a capture showed it.
- Some capture dependencies remain that are not textures and are not in
  this lane:
  - `status_hub.emhs` and `status_hub_commands.json` (00209DF0 layouts
    and strings over captured RAM);
  - the RAM checks of `export_panel.py`, `export_item_root.py`,
    `export_door_original.py`, `export_roger_resources.py`, and the
    opening actors / faces;
  - the snow / flame reference RAM and VU dumps.
- The two NEARMISS corrections (section 2) and the extract naming shift
  are recorded here only. The decomp's NEARMISS.md and FINDINGS.md are
  tracked files, so this lane did not edit them.
- A parallel lane is translating the same loaders natively
  (`src/game/em_module_loader.c`, untracked, MODULE_LOADER.md). This lane
  does not depend on it. Its texture restore (00200970) and player packet
  (00200890) are the same routines this lane's oracle executes.

## 8. Review survivors

The review's single-operation mutants that survived, and how each is now
handled. A recheck of these mutants (harness in
`build/b15/disc_textures/mutfix/`, quick mode except m39) kills every one:

| Mutant | Handling | Killed by |
|---|---|---|
| m00 module B walk from region offset 0 | pinned case: synthetic module with a resident offset | `module*` upload bytes |
| m05 area B sizes indexed without hdr[0x0C] | pinned case: synthetic area with first = 1 and two B sections of distinct sizes | `area*` (the second B section moves; the replay refuses it) |
| m06 module_slot without the resident offset | pinned case: slot bytes against the original's memory at every relocated pointer, on the synthetic module's nonzero resident offset | `module*` slot 0x35 bytes |
| m07 module_slot extent 0x100 short | pinned case: extent against the next relocated pointer / cursor; pinned source table | pinned sources (and the extent check) |
| m08 area pointer table without the B count | pinned case: synthetic area with two B entries before the table | `area*` slot check |
| m16 resident offset forced to 0 | as m00 | `module*` upload bytes |
| m20 world() without 001AD1A0's re-upload | pinned world step list | pinned step list |
| m25 residency check without the 0x00 probe | control: an uncovered block holding 0xA5 | control "uncovered 0xA5" |
| m27 extract comparison disabled | controls: a one-byte difference, a span past the files | controls "extract byte" / "extract span" |
| m39 hub states without hover 4 | pinned case (full mode): the executed states must be 5 x 2 + fixture | full-mode state list |

m39 is equivalent in its output: hover 4 adds no TEX0 that hovers 0..3
lack, so the atlas does not change. Its pinned check guards the set of
states the exporter executes, because a later ELF state could make hover
4 matter. The review's hang mutants (m02, m18 and others) now fail within
a few seconds with a RuntimeError naming the item.
