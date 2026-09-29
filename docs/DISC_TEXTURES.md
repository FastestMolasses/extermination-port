# Disc-sourced textures (lane TEX, release blocker)

Release blocker (FIDELITY_FEATURES.md, "disc-sourced textures"): the object,
chain-page (decal included), status-model and status-page textures and the
font atlas were decoded from PCSX2 captures or an EE RAM dump, which end
users do not have. This lane finds where the original loads each of them
from the disc and adds a disc-only exporter whose output is **byte-identical**
to every capture-derived file.

Result: **every first-level texture has a disc source**, and since the
chain step "every first-level asset from the disc" (2026-09-28, section 9)
every texture exporter reads the disc only: no texture needs a PCSX2
capture or a RAM dump. `tools/export_disc_textures.py` rebuilds all 15
formerly capture-derived texture files byte for byte from the disc image and
the extract, and the exporters that decoded from `opening_gs.bin` or a GS
dump (props, pickup lights, fence door, Roger, `player.emdl`, the weapon
sprite sheets) take their texels from the rebuilt GS memory by default
(section 4). Section 9 extends this to the other assets: only
`interaction.emis` and `background.embg` still read a capture.

Files:

| File | Role |
|---|---|
| `tools/export_disc_textures_gs.py` | the library: INDEX.IDX descriptors, each loader's section rule, the upload sequence, the GS image, the residency check; since section 9 also the resource table D_0028A490 (`ResourceTable`) and the EE memory the loads leave (`first_level_memory`) |
| `tools/export_disc_textures.py` | the one-command texture exporter (CLI); each part calls its tool's own writer |
| `tools/test_disc_textures_reference.py` | the original-instruction oracle and the capture comparisons (`make test-disc-textures-reference`) |
| `tools/test_disc_assets_reference.py` | section 9's checks (`make test-disc-assets-reference`) |
| `docs/DISC_TEXTURES.md` | this file |

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

| Asset (exporter; the capture it read before section 9) | Textures | Source |
|---|---|---|
| `scene_snow/object_textures.emot` (`export_object_textures.py`, formerly route `gs.bin`) | 303 TEX0 when this lane ran (465 since chain C8b FACE and the static-world step: the face resources and 119 static-bank MODULATE textures, all area / library / player packet) | 158 library (module 0x1B), 135 area (sector 15), 10 player texture packet (slot 8: the player model's TBP 0x1B80..0x1BF7 textures) |
| `scene_snow/page_textures.emot` (`export_page_textures.py`, formerly route `gs.bin`) | 7 TEX0 (since chain C8b FLAMESNOW: + the weather descriptor D_00255170's, the snow), including the 001F8D30 decal 0x2004290511322469 (TBP 0x2469, CBP 0x2148) | all library |
| `status_models/*.emdl` (`export_status_models.py`, formerly status-hub `gs.bin`) | 78 distinct TEX0 | 68 library, 10 player texture packet |
| `scene_snow/panel/status_hub_atlas.emha` (`export_status_hub.py`, formerly status-hub `gs.bin`) | 13 tokens | all library (the hub itself loads no page module) |
| `scene_snow/panel/item_root.emir` (`export_item_root.py`, formerly panel/root `gs.bin`) | 17 tokens | all ITEM root module 0x1F |
| `scene_snow/panel/battery.emba` (`export_panel.py`, formerly panel `gs.bin`) | 27 tokens | all BATTERY module 0x21 |
| `font.emfn` (formerly the decomp's `export_font.py --ee`, an EE RAM dump; now `export_disc_textures.py`) | 409 tall + 256 small glyphs | not GS: module 0 resource slots 0 and 1 (`chunk00/f00_id00.bin`, `chunk00/f01_id01.bin`), loaded to 0xB00000 by 001FF1E0(0) in the boot main loop 001AAE40 |

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

Each part calls its tool's own writer, so one owner builds each file
(since section 9; the tools themselves are disc-first too):

- `objects`: `export_object_textures.tex0_set` (the TEX0 set and form
  checks), `disc_texels` and `write`, over the rebuilt world image.
- `page`: the TEX0 set from original sources only: the +0x70 TEX0 rows of
  the 001CFBE0 source blocks D_00253670 and D_002565E0 + 0x90 k (the ELF);
  the AREA11 flame descriptor D_00828340's TEX0 row (AREA11.BIN offset
  0x4E40 + 0x70); 001F8D30's decal TEX0 and 001F4D40 / 001F4BF0's glow
  marker TEX0, two values the code builds as immediates (the same constants
  as `EM_SHADOW_DECAL_TEX0` and `EM_STATUS_SCENE_TEX0_001F4BF0`, whose
  modules' tests verify them against the executed original);
  `export_page_textures.tex0_set` / `write`. Its optional
  `--route-captures` also walks what the captured pages draw and requires
  it to be in this set (test D pins the two equal).
- `font`: module 0's descriptor gives slots 0, 1 and 2 at resident offsets
  0, 0x3000 and 0x5000; the glyph counts are (slot 1 - slot 0) / 30 = 409
  and (slot 2 - slot 1) / 32 = 256, exactly `export_font.py`'s rule over
  the pointers D_0028A490[0..2]; the decoders and layout are `export_font`'s.
- `status_models`: `export_status_models.write` (its glyph set is its
  constant GLYPHS; the former RAM reads are its optional `--capture`
  check); the texels through `export_native.build_texture_blob` over the
  rebuilt image written as a freeze blob.
- `status_hub`: the token list is what 00209DF0 passes to its sprite worker
  for hovers 0..4 x infection 0 / 100 plus the fixture pass (executed by
  `export_status_hub.Original`) and the five secondary icons that exporter
  lists; here the drawer runs over the ELF's initial data. The resulting
  list equals the capture's (test D); the atlas is
  `export_status_hub.atlas_emha`.
- `item_root`: `export_item_root.item_root_emir` (its `Original` executes
  0020F170 / 0020F2A0 over the ELF image); texels from world + module
  0x21 + module 0x1F.
- `battery`: `export_panel.battery_emba` (the token table D_00265C50,
  D_00265CD0 and its two constants, and the texts); texels from world +
  module 0x21.

Every decode must read only uploaded blocks (`reads_only_covered`: the
decode is repeated with every other block filled with 0x00 and with 0xA5
and must not change), otherwise the exporter stops.

The two JSON sidecars (`object_textures.json`, `page_textures.json`) are
reports. No runtime code reads them. They carry `source` and the list of
captures an optional cross-check ran against (`cross_checked_captures`). In
the page set, each `users` list names the original producers.

`module_slot`'s extent (the bytes read for a resource slot) runs to the
next greater pointer-table offset, or to the end of the resident region.
The loaders keep no sizes. The test compares this extent with the
original's relocated pointers and cursors (section 5 A).

`build/disc_textures/first_level_gs.bin` is the rebuilt world image in the
freeze layout `gs_vram.read_localmem` reads; `report.json` lists every disc
buffer (caller, DATA.DAT offset, size, extract files) and the output hashes.

## 4. The other exporters that decode first-level textures

Since section 9 each takes its texels from the rebuilt GS memory by
default (`FirstLevel.world()`, written to
`build/disc_textures/first_level_gs.bin`), refusing a texture that reads a
block no disc upload writes (`require_resident`); `--gs FILE` (or the
decomp's `--p2s FILE`) still accepts another freeze:

| Exporter (STARTUP.md step) | Capture it read before | Textures | Disc source | Proof |
|---|---|---|---|---|
| decomp `export_native.py --attach` (6, `player.emdl`) | `--gsdump extract/gsdump/frame1.gs` | 80 (51 body + 29 equipment) | player packet + library | `--p2s` over the rebuilt memory gives a byte-identical bake; the installed file's 80 textures equal it (test-disc-assets-reference full) |
| decomp `export_props.py` (12: `--fx`, `--cone`, `--area-items`, `--gibs`, `--pickup-items`, `--attach`) | `--gsdump` | 4 sprite sheets, the cone, the area items | library / area | the four `--fx` sheets byte-identical (full test); the cone bake identical with `--gsdump` and `--p2s`; the texels of `area_item_0b`, `area_item_13`, `area_parachute` equal the disc decode |
| decomp `export_level.py` level zones (10/11) | per the manifest (`--p2s`) | 290 TEX0 over the 6 `*.gsmat.json` | area | test F (not read by the first level since the static-world step) |
| `export_area11_props.py` (15) | `opening_gs.bin` | 24 | area / library | byte-identical outputs (test-disc-assets-reference) |
| `export_pickup_lights.py` (16) | `opening_gs.bin` | 13 | library | byte-identical outputs |
| `export_door_original.py` (18) | `opening_gs.bin` (hard-coded) | 5 | area | byte-identical outputs |
| `export_area11_effect.py` (19, flame), `export_snow.py` (21) | none since chain C8b FLAMESNOW (their textures are page textures) | 0 | | |
| decomp `export_opening_actors.py` / `export_opening_faces.py` (26/27, retired in chain C8b OPENING) | `--gs opening_gs.bin` | all 7 outputs rerun on the rebuilt image are byte-identical | area / library / player packet | test F |
| `export_roger_resources.py` (39) | `opening_gs.bin` (hard-coded) | 68 | area | byte-identical outputs |

`tools/export_status_map.py` (STARTUP.md step 55, the MAP page's 22 map
models of module 0x1E's bank D_0028A570) builds its texels from this lane's
disc model only: the first level's world image with `FirstLevel.page(world,
0x1E)` applied (module 0x1E's one A section is the page's upload), and it
refuses a model TEX0 that reads a block no upload wrote.

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
    (`CAPTURE_SHA256`, taken from `assets/` before this exporter existed;
    the object textures' pin was renewed on 2026-09-28 from the capture
    exporter's output after chain C8b FACE and the static-world step added
    165 textures), so the check stays independent of `assets/`.
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
  equals the capture exporter's list, pinned as `CAPTURE_HUB_TOKENS` (13;
  `status_hub_commands.json` is disc-derived since section 9).
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

## 6. Binding (done, 2026-09-28)

The live code reads the same file formats; the binding is in the export
steps (section 9 lists the whole step):

1. `make test-disc-textures-reference` (this document's test) and
   `make test-disc-assets-reference` (section 9's).
2. STARTUP.md step 4 is `python3 tools/export_disc_textures.py` (the font
   and every texture file); steps 32, 34, 51 and 52 name their own tools,
   which are disc-first and write the same bytes.
3. `build/disc_textures/first_level_gs.bin` is the default texel source of
   `export_area11_props.py`, `export_pickup_lights.py`,
   `export_door_original.py` and `export_roger_resources.py` (each builds
   it when no `--gs` is given), and the decomp's `export_native.py
   --attach` and `export_props.py` take it with `--p2s` (the decomp's
   `export_props.finish_textures` passes `--p2s` to export_level's builder,
   and `export_native.py` no longer refuses `--p2s` with `--attach`).
4. The capture paths of `export_object_textures.py`,
   `export_page_textures.py`, `export_status_models.py`, `export_panel.py`,
   `export_item_root.py` and `export_status_hub.py` are optional
   cross-checks (`--route-captures`, `--capture`). The decomp's
   `export_font.py --ee` is superseded for the port.
5. FIDELITY_FEATURES.md "disc-sourced textures" is met; SHADOW_DECAL.md 4
   names the uploader (the library sheet of module 0x1B).

Not done here: the decomp's NEARMISS headers of `src/func_001FF1E0.c` and
`src/func_001FF3F0.c` and its docs/NEARMISS.md still carry the two wrong
body claims of section 2 (a decomp-side edit for the lead).

## 7. Known gaps

- The exporters need INDEX.IDX, from the disc image or a copy of DATA/.
  The extract alone keeps neither the upload section tables nor the
  resident offsets. The section bytes are also checked against the
  extract. Only `--iso` is exercised: no test covers `--disc`.
- `player.emdl`: the step-6 bake with `--p2s` over the rebuilt memory is
  byte-identical to the bake with the GS dump, and the installed file's 80
  textures equal it; the installed file itself is not reproduced whole,
  because eight of its clips differ from any fresh bake (STARTUP.md
  "Honest limits", an older clip-append history).
- `fx/light_cone.emdl`: the `--cone` bake is identical with `--gsdump` and
  with `--p2s`, but the installed file carries flags 1 where the current
  exporter writes `NORMAL_FLAGS` 0 (an export older than the decomp's
  normals change); its texels are the disc's either way.
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
  (full mode); MAP's models are exported by `export_status_map.py`.
- The multi-section, resident-offset and nested branches are verified on
  synthetic descriptors only. No first-level data use them.
- The hub token list is computed over the ELF's initial data, not a
  runtime state, and it equals the capture's list. A future page whose
  tokens depend on the equipment state must derive them from that state.
- The page TEX0 set lists its producers explicitly: source blocks, the
  weather descriptor (the snow, since chain C8b FLAMESNOW), the
  flame, the decal and the glow marker. A producer bound later that draws
  another TEX0 must add its original source here; `--route-captures`
  refuses a captured page TEX0 that is not in the set.
- Two non-texture assets still need a capture (section 9.4).
- The two NEARMISS corrections (section 2) and the extract naming shift
  are recorded here only. The decomp's NEARMISS.md and FINDINGS.md are
  tracked files this chain step did not edit.

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

## 9. Every first-level asset from the disc (chain step, 2026-09-28)

The release blocker's second half: no first-level asset may need a PCSX2
capture or a RAM dump. The textures are sections 1 to 8; this section is
the rest. `tools/test_disc_assets_reference.py` (`make
test-disc-assets-reference`; quick about 3 s, `EM_TEST_FULL=1` about 10 s
wall) checks it.

### 9.1 The resource table D_0028A490

`export_disc_textures_gs.ResourceTable` rebuilds D_0028A490[0 .. 0xAF)
(the 0xAF words 001AB430 clears at boot: the resource slots and the loader
cursors D_0028A734 .. D_0028A748 at words 0xA9 .. 0xAE) after a New Game
into AREA11, from the disc with the loaders' own rules, in the order the
boot and New Game reach them:

| Step | Caller | Load | Cursor effect |
|---|---|---|---|
| 1 | 001AB430 | clears the 0xAF words | D_0028A734 = 0xB00000 |
| 2 | 001AAE40 (boot main loop) | 001FF1E0(0) at 0xB00000 | D_0028A734 = D_0028A738 = its end |
| 3 | 001AB7E0 step 1 | 001FB370(D_0028A4A4): 001FB3E0 returns the bank address + the bank's +0x10 size + 0x40, rounded down to 0x40 | D_0028A734 = D_0028A738 = that return |
| 4 | 001AB9D0, 001ABC60, 001AC480 | 001FF830(0x28 / 0x29 / 1) kind 1 at 0x01800000 (001AC480's 001FF080(0, 1) sets slot 6) | none |
| 5 | 001AB7E0 steps 3 / 4 | 001FF1E0(0x1B), 001FF1E0(0x1C) from D_0028A734 | D_0028A734 = D_0028A738 = each end |
| 6 | 001AD1A0 | 001FF080(0, 3) -> 001FF830 kind 0 from D_0028A738 | D_0028A73C = base + resident size (state 5) |
| 7 | 001FFCD0 (area 11, sector 15) | entry 0 (the sound bank) at D_0028A73C through 001FB370; the resident region and its pointer words from the new D_0028A73C | D_0028A73C = the bank's end; D_0028A740 = + resident size; D_0028A744 = D_0028A748 = D_0028A740 |

The pointer words are D_0028A490[e >> 24] = base + (e & 0xFFFFFF) of each
load's pointer table (section 2). The model equals D_0028A490[0 .. 0xAF) of
all 18 AREA11 captures (the playable, opening and handoff images, route
beats 00..14) word for word (slot 6 = 0x01800000 is the title's module 1,
step 4).
Uses:

- `export_roger_banks.py` writes the table from the model (EMRS version 2:
  the 0xAF words; version 1 carried 0xC0 words from a capture, the last 17
  being D_0028A74C and the task table D_0028A750's first slot, which are
  not resource words). `em_area11_roger.c` loads version 2 only, and
  `em_scene_bindings.c`'s 0015C1F0 table copy reads the export's 0xAF words
  (`EM_AREA11_ROGER_TABLE_WORDS`). The full smoke passes with it.
- `export_player_model.py` (D_0028A490[0x3B]), `export_world_models.py`
  (D_0028A490[0x43] = *D_0028A59C), `export_static_world.py`
  (D_0028A490[0x44] = *D_0028A5A0, and the bank bytes the area load leaves
  there) check their addresses against the model instead of requiring a
  capture.
- `export_module_loader.py` computes its cursor seeds from the model
  (`disc_seeds`: D_00275C70 = the header buffer D_00289BC0, D_00275C74 =
  module 3's destination, D_0028A5A0, D_0028A738 .. D_0028A748) and
  requires them to equal `AREA11_SEEDS`; MODULE_LOADER.md 1.8's "observed,
  not computed" is answered.
- `first_level_memory(elf, table)` is the ELF image with each load's
  resident region at its base (a later load overwrites an earlier one) and
  the table words. `export_status_hub.py` runs over it.

### 9.2 The other exporters

| Asset (STARTUP.md step) | Capture it read | Disc source now | Proof |
|---|---|---|---|
| `door_original/*` (18) | the playable RAM: the actor search, the door's descriptor D_002755F0, its destination row D_0024E140[11] and sound row D_0024DB80 | the ELF's .data (unchanged in RAM: checked when the capture is present), the rebuilt GS memory | the three files byte-identical to the pinned capture-derived ones |
| `roger/*` (39) | the playable / opening RAM (checks only) and `opening_gs.bin` | the extract, the overlay, the ELF; the rebuilt GS memory | 8 files byte-identical |
| `area11_effect.emef` (19) | `--ee` / `--vu` (required, checks only) | the overlay, the ELF | byte-identical |
| `snow.emsn` and the manifest's `weather` bits (21) | D_008106C8 from a capture | 001B0250's room record D_0024D650[11][0] + 0x1C (entry 0, the New Game's), masked 0x0E000070; 001AF2C0 clears D_00810788 on New Game, so 001B0250's area-11 override is not taken | byte-identical; the bits equal every capture's (route 09 at entry 2 too) |
| `panel/scripts.emsc`, `battery.emba`, `player_15c.bin`, `item_root.emir` (32) | the panel captures | the ELF, the extract, the page states' GS memory | byte-identical |
| `panel/status_hub.emhs`, `status_hub_atlas.emha` (32) | the status-hub capture's RAM and GS | the ELF image + the loads (9.1), after the message reset 001FC9B0 the boot's 001AB430 runs (the style D_00275C50 / 54 the help presenter reads); the world's GS memory | the atlas byte-identical; the EMHS differs only in 12 words of the arc block D_00265390 (centres and angles the capture held mid-hub), all words 00208AD0 writes before 002082B0 reads them (`ARC_WORDS_WRITTEN`, em_status_draw.c); the layouts and the ten help lines are identical |
| `roger/resources.emrs` (47) | the table from a capture | 9.1 | the 0xAF words and the regions equal the v1 file's |
| props, pickup bodies / lights, panel cell (15, 16) | `opening_gs.bin` | the rebuilt GS memory | 7 files byte-identical |
| `player_clips_full.*` (7), the step-8 clip tools | RAM checks (required) | the disc bank | unchanged outputs; the checks run when captures exist |
| `player.emdl` (6), `fx/*` (12) | `--gsdump` | `--p2s` over the rebuilt GS memory | section 4 |
| `snow.emcl` (13), `player_shadow.emdl` (42), `roger_shadow.emdl` (57), `shadow_receivers.emsr` (41), `world_models.emwm` (44), `render_context.emrc` (49), `effect_tables.emet` (50), `player_model.emom` (53), `static_world.emsw` (58), `modules.emml` (56) | optional `--verify-ram` / default captures | unchanged (their captures were checks) | byte-identical without captures (9.3) |

### 9.3 The whole route on disc-only assets

A copy of both trees was made in which `../Extermination` held the
extract, the ELF, the disc image and the tools but no `build/` (no
capture, no RAM dump, no GS dump). The files the full level smoke opens
(116, logged by an `open()` interposer over the four smoke runs) were
seeded from `assets/`, the ones whose exporter had read a capture were
deleted, and 57 export steps were run there (every STARTUP.md step except
6, 8, 10, 11, 12, 30 and 40): all succeeded. Of the 116 asset files the
smoke opens, 113 were byte-identical to the installed ones; the three
others are `roger/resources.emrs` (version 2), `panel/status_hub.emhs`
(the arc words) and `scene.txt` (the same non-comment lines, in another
order, with the props exporter's comment lines repeated). With those
assets `make test-level-smoke-full` passed: the main route through roger
(18 live phases, `--require-through`) and the side runs 00, status_pages
and 09 with side 1.

### 9.4 Still capture-bound

- **`interaction.emis` (step 30).** The eleven use-owners' records are the
  playable capture's actor list. Executing the original state-0 spawners
  (001AF8E0, 001B6990, 001C5C50; tools/test_actor_census_reference.py's
  oracle) over the disc tables with New Game's zero progress bytes gives
  the same nodes, ranks (after the self-freeing record 13), uid, class,
  subtype, +0x2E item type, placement and angles; the three fields it does
  not give are written by each owner's first tick: status 1, the selector
  +8 (3 for the pickups, 1 for the elevator) and the descriptor pointer
  +0x30. Deriving them needs the six owners' state 0 (00219550, 0015AFA0,
  00159210, 001BC350, the overlay's 00827B10 and 008237E0) executed over
  the spawned pool; not done here.
- **`background.embg` (step 40).** The draw state (TEX0, CLAMP_1, TEX1_1,
  TEST_1, ZBUF_1, RGBAQ) is read from render channel 3's captured list.
  Its sources are code and ELF data (001C1F50's TEX0 immediate for key
  0x0B00 through 001E2260, D_00250F30 through 001E2270, 001E1E60's
  001D1F80(3, 0, 7) / 001D1FF0(3, 0) environment), so a disc path is
  001E1E60 executed over a render context 001C1F50 built on the disc
  memory; not done here. Its texels are already the disc's.
