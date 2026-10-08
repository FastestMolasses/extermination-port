# MODULE_LOADER: the screen-module loader behind the panel prompt (H7)

Lane b15 "module_loader" (2026-09-27) translated the loader's disc and DMA
layer; chain C8b LOADER (2026-09-28) bound it live and translated the area
streamer; chain step H7 (2026-09-29) bound the area streamer with its
sound-bank step and put the New Game's two loads on it; chain step
PAGELOADS (2026-09-30) put every page module the first level loads on it.
Files: `src/game/em_module_loader.{h,c}`,
`tools/export_module_loader.py`, `tools/test_module_loader_reference.py`
(`make test-module-loader-reference`), this doc; the state machine is in
`em_status_scene_original.c` (STATUS_SCENE.md section 2).

**What is live (chain C8b LOADER).** Module 0x21, the BATTERY page, loads
through the loader's own steps on every status screen that opens it (the
panel's prompt, route 03, and the battery pop-up, route 01). Since chain
step PAGELOADS every other page module the first level loads does too (the
ITEM root's 0x1F, MAP 0x1E, SPR4 0x2C, DATABASE 0x24, the ITEM children
0x20 / 0x22 / 0x23 and SPR4's part pages 0x2D..0x31; finding 8):
- `em_scene_bindings` boots one loader at start-up over the user's
  exported sectors and binds its slot-2 task (section 4);
- the page's 001FF080(0, module) (0x21: the ITEM root's) registers the task 001FF0D0, which runs
  001FF830 / 001FF3F0 and the I/O routines 00200780, 00200730 and 00200830
  once per frame, after the game task (em_task slot 2);
- its 0x63 step clears D_00275BD8 and stops the slot (001AB7D0), and the
  ITEM root's state 3 advances on the cleared byte.

The drive's time goes (user policy, port CLAUDE.md 2026-09-27): the load
takes the loader's **10 dispatches** at host speed, where the original takes
24. With the PS2 disc-drive timing switch (`EM_PS2_DISC_DRIVE_TIMING=1`,
LAUNCHER_OPTIONS.md) the drive answers with the recorded busy fields and
the load takes the captured 24 frames. The level smoke compares both
(section 4.1).

**The New Game's loads (chain step H7).** New Game's 001AD1A0 requests
module 3 (the player's texture packets, slots 8..52) and 001ADF50 the area
load 001FF080(1, 0) through the same task:
- module 3 loads through 001FF830 (kind 0 from D_0028A738): **8 dispatches**
  at host speed, 68 with the recorded drive (the capture's);
- the area load runs the area streamer 001FFCD0 with 001FF590 (section
  1.9), its workers bound here: the overlay file, the area header, the
  sound bank through **001FB370** (the EE sound library and the IOP's
  command 0x20: `em_sound_bank` on `em_stream_live`'s backend, IOP_STREAM.md
  "The sound-bank transfer"; 8 calls), the A entry (the area's texture
  upload) and the resident region, then 00200890's player packet: **20
  dispatches** at host speed, 185 with the recorded drive (the capture
  takes one more: 001FB370's ninth call, most likely the PS2's SIF DMA
  time);
- D_00275BD8 clears at the area record's 0x63, so 001ADF50's veil 0021B550
  ramps for as many ticks as the load takes (LOAD_VEIL_PARTICLES.md
  section 5): 55 veil frames at host speed, 257 with the switch (the
  captures: 258).
The port's own assets of the area (em_game_legacy_area_load: the
renderer's and the collision's formats) load in the dispatch whose
001FFCD0 step completes the area, where the original's data are all in
memory (em_scene_bindings `loader_area_done`).

## 1. Behaviour (by original address)

All addresses are the boot ELF's. "Faults" means fail-stop: the port reports
it through `em_module_loader_failed` and never substitutes a result.

### 1.1 The request and the task (reused, byte-matched / NEARMISS-verified)

**001FF080(state, module)**
- Calls 001AB740(2, 001FF0D0): slot 2 of the task table goes to state 1,
  and record +8..+0x17 is cleared.
- Then +8 = state and +0xE = module.
- Native form: `em_module_loader_request_001FF080`. It calls
  `em_task_register(2, em_module_loader_task_001FF0D0)`, which is 001AB740
  natively, and then writes `user[0]` and `user[6]`.

**001FF0D0**
- Runs once per frame while slot 2 is in state 2. 001AB6A0 promotes the
  slot 1 → 2 in the same pass as the request, and slot 2 runs after slot 0.
- Does nothing while D_00282157 != 0.
- +8 == 0 runs the bank streamer 001FF830.
- +8 == 1 runs 001FFCD0 (the area streamer, section 1.9: translated, not
  bound in the live loader, so it faults there) and +8 == 2 runs 00200360
  (untranslated: it faults). After 001FFCD0 reports 0x63, 001FEF70 picks
  the inventory's bank module 0x32..0x35 over D_00810CA4 / D_00810CA6 and
  restarts the record on it (+8..+0xC cleared); every AREA11 capture holds
  CA4 = 0xFF, CA6 = 0, so the first level never chains.
- +8 == 0x63: D_00275BD8 = 0 and the slot goes idle (001AB7D0).
- Native form: `em_status_scene_loader_001FF0D0`, called by
  `em_module_loader_dispatch` over the em_task record. Its 0x63 step's
  D_00275BD8 store goes to the loader's view of the byte (section 4) and
  its 001AB7D0 is the record's state byte (em_task slot 2).

**001FF830 / 001FF3F0** (STATUS_SCENE.md section 2) run a load as a sequence
of steps:
1. The header read: one sector of INDEX.IDX.
2. The chunk reads: A entries, each read into D_00275C74 and DMA'd through
   00200830.
3. The payload read.
4. The cursor commit by kind.
5. The section DMAs.
6. The relocation words D_0028A490[e >> 24] = (e & 0xFFFFFF) + base.
   The table is one storage of 0xB0 words, D_0028A490..D_0028A74F (the
   task table D_0028A750 follows), and the loader's cursors are its slots:
   D_0028A5A0 is slot 0x44, D_0028A734..D_0028A748 are slots 0xA9..0xAE
   (`EM_STATUS_SCENE_SLOT_*`). A relocation that names a cursor's slot
   moves that cursor, as in the original. A slot at or past 0xB0 would
   store into the task table: the port faults at that address instead.

### 1.2 00200780: start a disc read

Arguments: (descriptor address, destination, byte offset, size).
- **Read mode.** On its stack it builds the three mode bytes trycount 0,
  spindlctrl 1, datapattern 0.
- **Sector count.**
  - size >= 0: sectors = (size + 0x7FF) >> 11, an arithmetic shift of the
    32-bit sum.
  - size < 0: the whole file, (descriptor size + 0x7FF) >> 11, a logical
    shift.
- **First sector.** lsn = descriptor lsn + (offset >> 11), an arithmetic
  shift.
- **Calls.** 00113280(0) first, then 00112440(lsn, sectors, buffer, mode).
  While 00112440 returns 0, it calls 00113280(0) again and repeats the read.
  The descriptor's lsn is re-read on every attempt.
- **Result.** Returns sectors << 11. The reply of 00113280 is never used.

About 00113280: the decomp's NEARMISS comment calls it sceCdInit. What the
code does is send its mode to the RPC server 0x8000059A and return the
server's reply. Nothing here depends on the name.

### 1.3 00200730: poll

- 00112D18(1) nonzero (busy): returns 0.
- Otherwise returns 1 when 00113680() is 0, else 2.

### 1.4 00200830: one DMA chain on VIF1

In order:
1. base = 00101BB8(1). This is D_00241050[1] = 0x10009000; the whole-load
   run executes the original 00101BB8 and checks this.
2. 00102468(base, 0, 0).
3. 00101F08(base, chain).
4. 00102468(base, 0, 0).

### 1.5 00200890: the player's texture packet

This is 00200830 of one word of D_0028A4B0..C0, chosen as follows:

| D_00810707 | D_00810C60 == 2 | == 1 | otherwise |
|---|---|---|---|
| 0 | 4B8 | 4BC | 4B0 |
| 1 | 4B8 | 4BC | 4C0 |
| any other | 4B4 | 4B4 | 4B4 |

### 1.6 00200970(a0): the player's texture restore

- Always first: 00200830(D_0028A564).
- a0 == 0: 001CCB10(). Then, only when spad 0x70003B90 == 2, 00200890().
  The byte is read after 001CCB10 returns, so the native form takes a
  pointer to the port's storage of it and reads it at that point (a
  pinned case has 001CCB10 change the byte).
- a0 != 0: 00200890().

### 1.7 The host drive

This is the platform side, not game code. It is one implementation of
`EmModuleLoaderSdk`.

| Leaf | Host answer |
|---|---|
| 00113280 | Reply 2; it is unused. |
| 00112440 | Copies `sectors` whole sectors from the pack to the host region of the destination. Accepted = 1. A sector range the pack does not hold faults (0x00112440, BAD_INDEX). A read issued while the measured drive is busy faults; the loader never does this. |
| 00112D18(1) | Busy only while the measured option holds the read, else 0. The blocking form 00112D18(0) is not on this path and faults. |
| 00113680 | 0. |
| 00101BB8(1) | 0x10009000. Any other channel faults. |
| 00102468 | Nothing: the host consumer is synchronous. |
| 00101F08 | Hands the consumer hook the host bytes from `chain` to the end of the region the drive delivered there. A chain the drive did not deliver faults, and so does a missing hook. |

Destinations:
- D_00289BC0 (the 0x800-byte header) is the loader's own `ld.header`.
- Every other destination is a host region keyed by its original address.
  A later read that overlaps a region replaces it.

**The measured option** (the PS2 disc-drive timing switch,
`EM_PS2_DISC_DRIVE_TIMING=1`; `em_settings`). The file names include the
area files of D_0028A3C0 (file 0x10 + area in the table).
- A read keeps sync busy for its measured number of fields, counted on
  `em_module_loader_field`, which main.c's field hook calls once per frame
  at the top of the frame, before the task dispatch.
- The measurements are module 0x21's reads and the New Game's (below),
  each named by file, sector inside the file and sector count, so a disc
  image that places the files elsewhere still matches them. Module 0x21's:
  - the header, INDEX.IDX sector 0x21 (1 sector), is busy for **6** fields;
  - chunk 0, DATA.DAT sector 0x1C978 = byte 0xE4BC000 (161 sectors), is
    busy for **8**;
  - a zero-sector read, for 0.
  On the rebuilt image these are lsn 0x9D7F1 and 0x9CB50.
- Sources: STATUS_LOAD_WAIT_PROBE.md, and CAPTURES_C7.md section 6 in the
  decomp repo.
- **The New Game's reads** (chain step H7), from the PCSX2 New Game capture
  (the decomp's `build/startup-reference/newgame_samples.jsonl`: the task
  records sampled about twice a frame while the VM ran; each busy count is
  the frames from the read's issue row to its done row, less the done poll):

  | Read | File, sector, sectors | Busy fields |
  |---|---|---:|
  | module 3 header | INDEX.IDX 3, 1 | 6 |
  | module 3 payload | DATA.DAT 0x429 (byte 0x214800), 1175 | 54 |
  | the AREA11 overlay | \OVERLAY\AREA11.BIN 0, 15 | 6 |
  | the AREA11 header | INDEX.IDX 0x0F, 1 | 1 |
  | the AREA11 sound bank | DATA.DAT 0xEDCF (byte 0x76E7800), 149 | 11 |
  | the AREA11 A entry | DATA.DAT 0xEE64 (byte 0x7732000), 433 | 16 |
  | the AREA11 resident region | DATA.DAT 0xF015 (byte 0x780A800), 3292 | 131 |

  The test derives the same table from the capture itself and requires the
  whole load's rows to reach every captured state at the captured frame
  (relative to each load's first dispatch), except one frame early after
  the sound-bank step (above). A sample's frame label is only known within
  its frame (two samples per frame, read while the VM runs), which the
  veil's phase bounds: the switch's New Game load leaves the veil 257
  steps, the captures 258, the one being 001FB370's ninth call.
- Any other read is answered at host speed and counted as `unmeasured`.
  Nothing is extrapolated. The page modules other than 0x21 have no
  recording (no capture shows a status page open): with the switch their
  loads take the host-speed 10 dispatches, and their reads are counted.

### 1.8 The loader's cursor seeds

The loader reads cursors that earlier loads left behind: D_0028A5A0,
D_0028A738, D_0028A73C, D_0028A744 and D_0028A748. The earlier loads are
the boot loader 001FF1E0, the New Game module-3 load and the area streamer
001FFCD0, and the port runs none of them through this loader. Every bank
load writes D_00275C70 and D_00275C74 before it reads them.

For module 0x21 (kind 1, the default case of 001FF830 state 0) the only
cursor that changes behaviour is D_0028A748: it becomes D_00275C74, the
chunk's destination. That is the host region's key and the address the
chain hook checks.

The pack carries the seeds (EMML header 0x20). Their values:

| Global | Seed | Evidence |
|---|---|---|
| D_00275C70 | 0x289BC0 | all 21 AREA11 captures |
| D_00275C74 | 0x10E99C0 | route 00, before the level's first page load; 0x19A3F40 after it |
| D_0028A5A0 | 0x1516F40 | all 21 AREA11 captures |
| D_0028A738 | 0x10E99C0 | all 21 |
| D_0028A73C | 0x1335F40 | all 21 |
| D_0028A744 | 0x19A3F40 | all 21 |
| D_0028A748 | 0x19A3F40 | all 21 |

The 21 captures are `build/s87/route/00..14`, the five
`startup-reference` RAM images and `c7cap/door1`. Route 15 (the level exit)
differs, because the next area is loading. Without `--capture` the
exporter computes the seeds from the disc (`disc_seeds`: the ResourceTable
of DISC_TEXTURES.md 9.1, which replays the boot, title, New Game and area
loads' cursor rules) and requires them to equal `AREA11_SEEDS`; the live
loader still does not run those loads itself (section 5).

Since chain step H7 the New Game's module-3 and area loads run through
this loader and recompute all of them but D_0028A738 (the boot loader's):
the whole-load test ends with exactly the captured values. The seeds still
matter for a load before them (none on the route) and for module 0x21's
D_0028A748, which the area load writes to the same value. How the loads
produce them (section 1.9): module 3 (kind 0, from D_0028A738 =
0x10E99C0) ends at 0x13351C0; the area load's sound bank there returns
0x1335F40 = D_0028A73C (001FB370's aligned end of the bank's resident
part); the area's resident region (0x66E000 bytes) puts D_0028A740 =
D_0028A744 = D_0028A748 at 0x19A3F40; its pointer word for slot 0x44
makes D_0028A5A0 = 0x1335F40 + 0x1E1000 = 0x1516F40; module 3's load
leaves D_00275C74 = 0x10E99C0 (route 00's value). Only D_0028A738 (the boot
loader 001FF1E0's) is left once those loads run through the loader. These
numbers are the disc's (INDEX.IDX sectors 3 and 0x0F, the bank's +0x10 word)
through the translated steps; the oracle's AREA11 case reproduces D_0028A5A0
from the captured D_0028A73C.

### 1.9 001FFCD0 and 001FF590: the area streamer (live since chain step H7)

`em_status_scene_area_001FFCD0` (em_status_scene_original.c; 001FFCD0 is
NEARMISS C, 001FF590 byte-matched C, both read against the original
instructions by the oracle, section 3 F). The record's +8 is the status
(0x63 at the end), +9 the state, +0xA the open phases' sub-state, +0xB
001FF590's state, +0x14 / +0x16 its chunk count and index. Its own inputs
and outputs (`EmStatusSceneArea`): the area and room D_00810700 /
D_00810701, the latches D_00810703 / D_00810704, the overlay arena word
D_00275304[0] and the descriptor table D_0028A3C0 (0x17 x {lsn, size},
which the boot's 001FEE60 fills from the disc directory).

| State | What it does |
|---|---|
| 0 | Reads the area's overlay file whole (size -1) to D_00275304[0] through the descriptor D_0028A3C0[area] |
| 1 | Poll: done calls 002009E0(D_00275304[0], the descriptor's size word): FlushCache(2), then the overlay's +0x14 bytes at the file's end are cleared; an error goes back to state 0 |
| 2 | Reads INDEX.IDX sector area + 4 (the area's header) to D_00289BC0 |
| 3 | Poll: done latches D_00810703 = D_00810700 |
| 4 | Open phase A: D_00275C70 = the header; 001FF590(0xAB, 0), then 001FF590(0xAB, 1) |
| 5 | D_0028A740 = D_0028A73C + (+8 - +0x14); reads the resident region (+4 + +0x14, size +8 - +0x14) to D_0028A73C |
| 6 | Poll |
| 7 | The B sections from D_0028A73C through 00200830, the pointer words D_0028A490[e >> 24] = D_0028A73C + (e & 0xFFFFFF) (the cursor is re-read for every word), then 00200890. +0x18 == 0: D_00810701 = D_00810704 = 0, D_0028A744 = D_0028A748 = D_0028A740, status 0x63. Otherwise D_00810704 = D_00810701 and D_00275C70 = the nested block D_00289BC0 + 0x100 + room * 0x70 |
| 8 | Open phase B on the nested block: 001FF590(0xAC, 0), then (0xAC, 1) |
| 9 | D_0028A744 = D_0028A740 + (+8 - +0x14); reads the nested block's resident region to D_0028A740 |
| 10 | Poll: done sets D_0028A748 = D_0028A744 |
| 11 | The nested block's B sections and pointer words from D_0028A740; status 0x63 |

A poll that reports an error (not 0 or 1) repeats the step before it.

001FF590(slot, mode) over the descriptor D_00275C70:
- mode 0: when +0x0C != 0 it reads entry 0 (the area's sound bank) to
  D_0028A490[slot], polls it (an error starts over) and hands it to
  001FB370 once per call until that returns an address, which becomes
  D_0028A490[slot];
- mode 1: it reads the entries +0x0C .. +0x0C + (+0x0E) - 1 one at a time
  to D_0028A490[slot] and sends each through 00200830 (an error re-reads
  the entry).
The slots 0xAB / 0xAC are D_0028A73C / D_0028A740: the bank and the A
entries land at the cursor, and the bank's end moves it.

**AREA11** (INDEX.IDX sector 0x0F): one bank entry (0x4A800 bytes), one A
entry (0xD8800, the area's texture upload: DISC_TEXTURES.md), no B
section, a resident region of 0x66E000 bytes from DATA.DAT byte 0x76E7800
+ 0x123000, 19 pointer words (slots 0x41..0x98, D_0028A5A0 among them), no
nested block. With every poll done at once and 001FB370 scripted to
finish on its third call, the oracle's AREA11 case takes 14 dispatches of
001FFCD0. Live, 001FB370 takes 8 calls (its own steps: IOP_STREAM.md "The
sound-bank transfer"), so the area load takes 20 dispatches at host speed.

**AREA01 sub 0** (INDEX.IDX sector 5; the level exit's load, chain C11 EXIT,
FIRST_LEVEL_EXIT.md section 7): the top block has no bank and no A entry
(+0x0C = +0x0E = 0), a resident region of 0x1A5000 bytes from DATA.DAT byte
0x199F800, six pointer words (slots 0x41, 0x96..0x98, 0x71, 0x73) and two
nested blocks; room 0's (D_00289BC0 + 0x100) holds one bank entry (0x75000
bytes, the area's SShd container, through 001FB370), one A entry (0xD8800,
the room's texture upload, sent from D_0028A740 in state 8) and a resident
region from +0x14D800 (0x4BE000 bytes) with fifteen pointer words. The
exporter's default areas are `0xb:0,0x1:0,0x0:0` (`--areas area[:room]`;
AREA00 sub 0, INDEX.IDX sector 4, since 2026-10-08: the open shaft door's
area change at the end of the second level); the pack holds every sector
of those loads. Its header sector is also module 4's, so the reference
test's missing-sector cases ask for module 6. Live, the load takes 37 dispatches
at host speed (the capture's 212 frames: its drive) and passes the
capture's states in order (LEVEL_SMOKE.md "exit").

**The live workers** (em_module_loader.c):
- 001FB370: the binder's bank hook (`em_module_loader_set_bank_hook`) with
  the file as the drive delivered it, from its address to the region's
  end; em_scene_bindings binds `em_stream_live_001FB370`.
- 00200890: `em_module_loader_packet_00200890` over the views D_00810707
  and D_00810C60 and the slot words 8..12 (module 3's relocations).
- 002009E0: FlushCache(2) has no host effect; the overlay's bss (its
  header's word +0x14 bytes after the file) is cleared in the drive's
  memory, where the loaded overlay is. The port's AREA11 overlay code is
  native and keeps its own storage; nothing reads these bytes.
- The area bytes D_00810700 / 701 / 703 / 704 are views of the scene state
  (D_00810703 / 704 migrated as progress bytes in this step), copied in
  before and out after each 001FFCD0 call.
- The reads of D_0028A3C0's descriptors (the area files) go through the
  same host drive; the pack carries the table and D_00275304[0].
- The area's two DMA sends go to the area consumer
  (`em_module_loader_set_area_chain_hook`; em_scene_bindings
  `loader_area_chain`): it accepts exactly 001FF590(0xAB, 1)'s A entry at
  D_0028A73C, 001FF590(0xAC, 1)'s at D_0028A740 (a nested block's: AREA01
  room 0's, since chain C11 EXIT) and 00200890's packet (one of the slot
  words 8..12) and applies nothing: the port's renderer draws AREA11's
  texels from its disc export, which DISC_TEXTURES test B proves equal to
  what these uploads write in every route capture, and nothing in the first
  level draws AREA01's. Anything else (a B section) is refused.
- An area-done hook (`em_module_loader_set_area_done_hook`) runs in the
  dispatch whose 001FFCD0 step reaches 0x63; the binder loads the port's own
  assets there.



## 2. Findings (evidence in section 3)

1. **The module-0x21 load is the BATTERY page's texture upload.**
   - Its header has A = 1, B = 0, C = 0: one chunk (0x50800 bytes), no
     sections, no relocations and a zero-byte payload.
   - The chunk is one VIF1 chain carrying one PSMCT32 transfer: 256x320
     texels at block 0x1D00, buffer width 4.
   - The transfer's 81,920 words equal the GS memory of
     `startup-reference/panel/gs.bin`, which is the capture
     `tools/export_panel.py` decodes the port's BATTERY atlas from.
   - All 27 atlas TEX0 words have TBP0 and CBP inside the transfer: page
     tables D_00265C50 / D_00265CD0, plus the highlight and background words.
   - The captured RAM of routes 01 and 03 still holds the chunk at
     D_00275C74 = 0x19A3F40, byte-identical to DATA.DAT.
2. **The 24 dispatches are the loader's 10 steps plus 14 busy polls.**
   - The host-speed rows (slot-2 record and D_00275BD8 after each frame)
     equal the captured h7 rows f391..f414 with the 14 busy rows removed:
     f392..f397 (header) and f400..f407 (chunk).
   - The 10 rows kept are f391, f398, f399, f408, f409, f410, f411, f412,
     f413 and f414.
3. **The wait freezes the world.**
   - In route 03's trace, rows f392..f414 differ from their predecessors
     only in `counter` and `f`.
   - Each wait frame calls rand() exactly twice, from 001D7C30 (return
     addresses 0x1D7D44 and 0x1D7DD4). This is measured on route 01's
     identical load, f194..f217. There is no route-03 rand capture.
4. **The relocation slot table is D_0028A490..D_0028A74F, and the
   loader's cursors are its slots.** Real module headers name slots up to
   0x9A; slot 0x44 is D_0028A5A0 itself (module 0x1D and the AREA11 area
   load relocate it), 0x86 is 0x28A6A8 and 0x87 is 0x28A6AC; 001FFCD0
   passes 0xAB (D_0028A73C) and 0xAC (D_0028A740) to 001FF590. Since chain
   C8b LOADER the native table is that whole range (0xB0 words, the
   cursors as named slots): the 14 disc headers that stopped at the old
   68-word model (4..8, 0xB, 0xC, 0x13, 0x14, 0x16, 0x17, 0x1A, 0x1D and
   0x36) and every other self-naming header under 6 MiB load through and
   write the original's words (the full sweep, section 3 B).
5. **00200890 / 00200970 read loader relocation slots.** This is checked in
   the captured RAM of route 03:
   - D_0028A4B0..C0 are module 3's slots 8..12, at base D_0028A738 =
     0x10E99C0 (the New Game load 001FF080(0, 3)).
   - D_0028A564 is module 0x1B's slot 0x35, at base 0xB319C0.
   - Slots 0..5 are module 0's, at base 0xB00000.
   - So the player's texture packets are resident module data that the
     port's drive only delivers if those modules load through it.
6. **D_00275BD8 = 1 is raised by the ITEM root one frame before its
   request.** 0020EE50 state 2 writes it, and the store is captured at
   0x20F06C in f390 (CAPTURES_C7.md section 6). The port's `em_item_root`
   state 2 sets its busy view there (section 4 item 4), so the loader needs
   no extra writer.
7. **The live load reproduces the capture** (chain C8b LOADER, the level
   smoke). At host speed the port's rows R..D of routes 01 and 03 equal the
   captured rows without the 14 busy polls, and the BATTERY prompt takes the
   request 16 ticks after the post, against the original's 30 = 16 + 14;
   with the PS2 disc-drive timing switch the 24 rows and the 30 ticks are
   the capture's.

8. **Every page module is one chunk, and its upload is what the port
   draws** (chain step PAGELOADS). The INDEX.IDX headers of 0x1E, 0x1F,
   0x20..0x24 and 0x2C..0x31 all name themselves (word 0) and have one
   chunk (h[0x0E] = 1, sizes 0x18800..0x78800), no B section and, but for
   0x1E, an empty payload; 0x1E's payload (0x32000 bytes) is the MAP
   model bank, and its one relocation word makes slot 0x38 (D_0028A570)
   = C74 + 0 = 0x19A3F40. Each chunk is one VIF1 chain of whole-page
   PSMCT32 transfers whose blocks and bytes are exactly the module's step
   in the port's status-pages GS data (`em_gs_texture`, EMSP: 384 blocks
   for 0x1F up to 1,920 for 0x2C, 16,000 in all); every sprite of the
   port's ITEM atlas (`item_root.emir`) decodes from module 0x1F's upload
   over the world image, opened from the hub or after module 0x21, and
   from neither the world alone nor module 0x21's upload (section 3 I).
   At host speed each load takes the same 10 dispatches as module 0x21's.

## 3. Verification

`python3 tools/test_module_loader_reference.py` (`make
test-module-loader-reference`):
- builds a private dylib under `build/b15/module_loader/`;
- exports two private packs there from the user's ISO: one with
  `--capture` (route 03, its checks and its cursors), and the disc-only one
  an end user makes. The test requires the two to differ only in the
  D_00275C74 seed word;
- takes about **8 s** (part H is most of it). `EM_TEST_FULL=1` takes
  longer (the leaf sweep and every eligible header).

Every check below runs in both modes.

**A. Leaves (original instructions of the pinned ELF, SHA256 asserted).**
- **Routines.** 00200780, 00200730, 00200830, 00200890 and 00200970.
- **Hooks.** Every SDK callee is hooked, scripted and recorded, and the
  hooked set must equal each routine's jal targets.
- **Compared:**
  - every call and argument, the three read-mode bytes the original builds
    on its stack included;
  - every result;
  - that the original stores nothing outside its stack (hook stores aside).
- **Bounded.** An exhausted script, or more than 64 native leaf calls,
  makes the native leaf return a port failure, so a wrong retry loop fails
  the case instead of hanging the run.
- **Cases:**
  - 00200780: 120 in the quick run, 5,544 in full. They cover sizes and
    offsets across the sign and rounding edges, seven descriptors (two
    whose whole-file rounding crosses bit 31), and accept scripts with 0 to
    3 retries.
  - 00200730: 25.
  - 00200830: 12.
  - 00200890 and 00200970: 184. Four of them are pinned: 001CCB10 changes
    spad 0x70003B90 (0→2, 2→0, 1→2, 2→3), and the packet send follows the
    new value.

**B. Whole loads over captured RAM.** The original code runs over route
03's `eeMemory.bin` and `scratchpad.bin`:
- 001FF080 and 001AB740;
- 001FF0D0, 001FF830, 001FF3F0 and 001AB7D0;
- 00200780, 00200730, 00200830 and 00101BB8.

The test first asserts that each capture's code range 0x100000..0x240000
is byte-identical to the pinned ELF, so "the original instructions run" is
checked, not assumed. The dispatcher 001AB6A0 is modelled in Python (it
promotes the slot and calls 001FF0D0); it is not original code.

Only the libcdvd RPC leaves and the DMA hardware leaves answer from the
drive model. The oracle's drive reads its sectors from the ISO itself, not
from the pack under test. The native side is em_task slot 2 with
`em_module_loader` and the reused loader.

Setup:
- D_00275BD8 = 1, as after f390.
- D_00282157 = 0, as in every captured wait frame, except in the pinned
  gate case.
- The header buffer is scribbled, so the load must fill it.

Compared:
- **At every callee entry:** the callee, its arguments and the whole
  modelled memory. The callees are 00200780, 00200730, 00200830 and each
  SDK leaf. The modelled memory is the slot record, D_00275BD8,
  D_00282157, D_00275C70 / D_00275C74, the whole slot table D_0028A490..
  D_0028A74F (0xB0 words, the five cursors among them) and the header.
- **After every frame:** the same memory.
- **Every store** the original instructions make must be inside the
  modelled set, and every store the drive makes must be inside a delivered
  range.
- **The delivered bytes**, and the bytes each DMA hands the consumer (by
  hash).

Runs:
- Module 0x21: host speed and measured.
- Modules 0x1F, 3 and 4 in the quick run.
- In full, all 45 self-naming disc headers under 6 MiB except 0x21 and the
  kind-3 ids: all 45 load through and write the original's words (before
  the table was widened, 14 stopped at a slot past the 68-word model). The
  sweep requires that no real header names a slot past the table.

**C. Captures.**
- **Host speed:** 10 dispatches, whose rows are the captured rows without
  the 14 busy rows (finding 2). Route 01, from its own RAM: the same 10
  rows, and the dropped probe rows are exactly f195..f200 and f203..f210.
- **Measured drive:**
  - route 03: the slot-2 record (+0, +8..+0x1F) and D_00275BD8 of all 24
    frames f391..f414 equal `c7cap/h7/fields/frames.jsonl`;
  - route 01: from its own RAM, all 24 frames f194..f217 equal
    `loadwait/01_battery/probe_boundary.json` (state, steps, module, kind,
    count, index, BD8);
  - no read is unmeasured.
- **Findings 1 and 3** (the upload equals the panel capture; the frozen
  wait rows and two rand() calls per frame).

**D. Pinned cases.** Each one kills a mutant the review found alive.
- **Gate.** A host 0x21 load with D_00282157 = 1, 2 and 0x80 on frames 0, 3
  and 4, on both sides. It takes 13 frames; a gated frame changes nothing.
  The other 10 rows are the host rows.
- **Relocated pack.** Every lsn is moved by 0x40 sectors (descriptors
  included), and every range is widened by 3 sectors before and 2 after,
  so each read starts inside a range. D_0028A748 is seeded 0x1C00000,
  unlike D_0028A744. Under the measured drive the 24 rows still equal
  h7, no read is unmeasured, and the chunk lands at 0x1C00000.
- **Disc-only pack.** A host 0x21 load from the pack exported without
  `--capture` (its seeds written into the oracle's RAM): the same 10 rows.
- **Module 0x2B** (kind 1, one 0x50800 chunk) with spad 0x70003B90 = 0, 1
  and 2 through the `spad3B90` view: the chunk goes to 0x1800000, then
  0x19A3F40 twice, on both sides.
- **00200890 after module 3's whole load.** Its five packet words are the
  relocation slots 8..12 the load just wrote. All 9 selections of
  (D_00810707, D_00810C60) are run on both sides; the SDK leaf entries, the
  memory at each and the bytes handed to the consumer are compared. Slot 8
  is the payload's first byte, and the other 8 selections start inside the
  delivered region.
- **Orphan.** A request, then an unbind, then a dispatch: the fault is
  {0x001FF0D0, NULL_WORKER}; a new binding clears it.

**E. Native under ASan/UBSan.** A generated driver in `build/` runs:
- host and measured 0x21 loads (10 / 24 frames);
- a missing header (fail-stop);
- module 3's whole load;
- a consumer that reads every byte it is handed.

**Fault checks:**
- a header the pack lacks: 0x00112440, BAD_INDEX;
- +8 = 1 without the area views: 0x001FFCD0, NULL_WORKER; +8 = 2:
  00200360, NULL_WORKER (not translated);
- no DMA consumer: 0x00101F08, NULL_WORKER;
- the orphan fault (D);
- an unbound request;
- a missing pack.

**Negative controls.** Each mutant of `em_module_loader.c` below fails the
default run. The file is restored afterwards.
- The first round:
  - a logical shift of the offset;
  - poll status 3 for 2;
  - the 00200890 fallback word 4B0 for 4C0;
  - the measured busy count one field short;
  - read mode {0,0,0};
  - 00200970's spad test against 3;
  - an arithmetic shift in the whole-file rounding (this one needed the
    kept bit-31 descriptors, which were added for it);
  - a one-byte short copy;
  - a missing 00200730 entry.
- The review's survivors and the fix round:
  - gate forced 0 (killed by the gate case);
  - the pack ignoring the lsn's offset inside a range (relocated pack);
  - the DMA handing the region's start (00200890 after module 3);
  - the `spad3B90` view ignored (module 0x2B);
  - D_0028A748 seeded from the D_0028A744 word (relocated pack);
  - `accepted > 0` in 00200780's retry test (now fails in the leaf rig
    instead of hanging);
  - the orphan fault not latched;
  - spad 0x70003B90 sampled before 001CCB10;
  - the measured table keyed by the rebuilt image's absolute lsns
    (relocated pack).
- **The `d810CA4` / `d810CA6` views** are read by 001FF0D0's 001FEF70
  after the area streamer's 0x63 step; since chain step H7 a pinned case
  (part H) runs the area load with D_00810CA6 = 1 on both sides and
  requires the record to become module 0x32's load (+8..+0xC cleared).

`python3 tools/check_no_disassembly.py` is clean on all five files.

**F. The area streamer (`tools/test_status_scene_reference.py`, `make
test-status-scene-reference`).** 001FFCD0 runs as original code over the
pinned ELF with the record at D_0028A790 (spad 0x70003B6C) and 001FF590 as
original code under it; 00200780, 00200730, 00200830, 001FB370, 00200890
and 002009E0 are hooked and scripted on both sides. After every dispatch
the record, D_00275C70 / D_00275C74, the whole slot table, D_00810700 /
701 / 703 / 704 and the header are compared, with every callee and its
arguments, and every store the original makes must be inside that set.
- AREA11's own header (INDEX.IDX sector 0x0F from the user's disc): 14
  dispatches (001FB370 scripted to answer on its third call); its pointer word for
  slot 0x44 makes D_0028A5A0 = D_0028A73C + 0x1E1000.
- Synthetic headers that reach every state: errors on the overlay, header,
  bank and A-entry polls; no bank and no A entry; two A entries, two B
  sections and pointer words into slot 0xAB (the later words follow the
  moved D_0028A73C) and 0xAF; a nested block per room (states 8..11) for
  rooms 0 and 1, the first with its own bank. Busy polls in full mode.
- An area index past D_0028A3C0's 0x17 descriptors is refused (the
  original would read D_0028A480 as a descriptor).
- Negative controls (a scratch copy, restored): the cursor not re-read in
  the pointer loop, D_00810703 from D_00810701, the A entries counted from
  0 instead of +0x0C, a 0x60 nested-block stride, a bank-poll error to
  state 3: each fails the default run. State 1's error to "state - 1" is
  the same as the original's 0 there (equivalent).

**H. The New Game's loads (chain step H7).** Over the route-03 RAM
(which holds AREA11 and the pre-load cursors), module 3's whole load and
then 001FF080(1, 0)'s: the original 001FF0D0 + 001FF830 + 001FFCD0 +
001FF590 + 00200780 / 00200730 / 00200830 / 00200890 + 002009E0 (its
FlushCache hooked, its memset 00121A28 as original code) against
em_module_loader with its area workers. 001FB370 answers alike on both
sides (7 pending calls, then the bank's end, what test_sound_bank_reference
shows the real chain does). Compared as in part B, plus the area bytes
D_00810700 / 701 / 703 / 704 after every frame, the overlay's bss clear
(original stores inside it allowed, its bytes equal and zero) and the two
DMA sends (the A entry at 0x1335F40 and the player packet at D_0028A4B0).
The final cursors equal every route capture's (D_0028A73C = 0x1335F40,
D_0028A740 / 744 / 748 = 0x19A3F40, D_0028A5A0 = 0x1516F40). Host speed:
8 + 20 dispatches. With the recorded drive (the test derives the reads'
busy counts from `newgame_samples.jsonl` itself): 68 + 185, and every
captured loader state appears at its captured frame relative to its load's
first dispatch, one frame early after the sound-bank step. A pinned case
with D_00810CA6 = 1 checks the 001FEF70 chaining. The run takes about 8 s
in all.

**I. The page modules (chain step PAGELOADS).** `check_page_modules`:
for each of the 13 page modules (0x21 included), a whole load over route
03's RAM as in part B (the original 001FF080 / 001FF0D0 / 001FF830 /
001FF3F0 and the I/O routines against the native loader: every callee,
the modelled memory, the delivered bytes and the chain each send hands
the consumer): 10 dispatches, one send. Then:
- the send's bytes through the decomp's GS upload model
  (`export_level._bg_gs_upload`, the one the exporters use) write exactly
  the blocks and bytes of that module's step in the installed
  `assets/status_pages/status_pages.emsp`;
- module 0x1E leaves D_0028A490[0x38] = the EMSP's relocation for
  D_0028A570 = 0x19A3F40 (the only relocation the EMSP holds);
- every sprite of the installed `assets/scene_snow/panel/item_root.emir`
  decodes (the decomp's `decode_token_lm`) from the EMSP world image with
  module 0x1F's upload over it, alone (the hub's order) and after module
  0x21's (the panel's); without module 0x1F's upload none of the 17 does;
- native loads with the measured drive: 10 dispatches (0x21: 24), each
  read other than module 0x21's counted as unmeasured.

**G. Live (the level smoke).** `check_module_load` (tools/test_level_smoke.py,
every run through the battery and the panel): the loader bytes the tick
log records after each frame (`loader_pre`: slot +0, +8..+0x1F,
D_00275BD8, D_00282157) against h7 and the route-01 probe, by the run's
drive mode (section 4.1). The run log's "module loader:" line gives the
counters.

## 4. Binding (live since chain C8b LOADER)

1. **Asset.** `python3 tools/export_module_loader.py` (STARTUP.md) needs
   only the user's disc image and the pinned ELF. It writes
   `assets/module_loader/modules.emml` (EMML version 2) and `modules.json`
   (ignored, disc-derived): the ISO's INDEX.IDX and DATA.DAT extents, the
   cursor seeds `AREA11_SEEDS` (section 1.8), the boot's tables from the
   ELF (D_0028A3C0 with the area files looked up in the ISO's directory,
   D_00275304[0], D_00264890), and the sectors of module 3, of the page
   modules (`PAGE_MODULES`: 0x1E..0x24 and 0x2C..0x31), of the game-over
   screen module 0x27 (`SCREEN_MODULES`; DAMAGE.md section 5), of area 0x0B
   (the overlay file, the header, the bank, the A entry and the resident
   region), of area 1 room 0 (AREA01 sub 0, the level exit's load) and of
   area 0 room 0 (AREA00 sub 0, the second level's exit load, since
   2026-10-08): `DEFAULT_AREAS`; 31.3 MB in all. Add others with
   `--modules` / `--areas`.
   `--capture <folder>` (developer only) adds the capture checks and takes
   that capture's cursors as the seeds. The game does not start without the
   pack (fail-stop, like the stream export).
2. **One loader for the game.** `em_scene_bindings_module_loader_boot`
   (main.c, at start-up after the stream boot) opens the pack, sets the
   views and the drive mode, and binds the slot-2 task live;
   `em_scene_bindings_module_loader_shutdown` unbinds and closes it at exit.
   The boot also binds the area's workers: the bank hook to
   `em_stream_live_001FB370` (after `em_stream_live_bind_sound_bank` with the
   pack's D_00264890), the area consumer and the area-done hook.
3. **Faults.** After every task dispatch (step E) the frame loop runs
   `em_scene_bindings_module_loader_check` (`em_frame_set_task_check`): a
   latched loader fault or the orphan fault prints its address and code
   once and stops the frame loop (fail-stop, not a hung ITEM root); the
   run's exit status reports it.
4. **Views** (the one storage of each byte):

   | View | Storage |
   |---|---|
   | `d275BD8` | the scene state's `d275BD8` (D_00275BD8, below) |
   | `r_00282157` | em_scene_bindings' `r_00282157` over `em_stream_live_read_phase()` |
   | `d810CA4` / `d810CA6` | the scene state's progress bytes D_00810CA4 / CA6 |
   | `spad3B90` | the scene state's `spad3B90` (0x70003B90) |

   **D_00275BD8 is one byte**: the scene state's (em_scene_state()->
   d275BD8), which 001ADF50 / 001AD4E0 / 001AD1A0, 0x1AE040's tests and
   the scene tick log read. The status runtime binds it
   (`em_status_runtime_bind_busy`, at the AREA11 interaction host's load);
   the page core's `EmItemRoot.asset_busy` is a per-call view of it,
   loaded before 0020CDC0 runs and stored after (as the request bytes
   are), and the status pages' `SP_D_00275BD8` is a view of that view
   inside the call. The loader reads and writes the byte through its view.
5. **DMA consumer.** `em_status_runtime_bind_loader` installs the status
   runtime as the chain hook. It accepts only a page module's chunk: the
   record's +8 = 0 and +0xE a page module, chain == D_00275C74, the loaded
   header naming that module with one chunk and no B section, and at
   least the chunk's size (h[0x24]) delivered (findings 1 and 8); with the
   status pages bound it applies the module's GS blocks to their GS memory
   then (`em_gs_texture`, the same upload: section 3 I), at the chunk step.
   Without the status pages only the ITEM root and the BATTERY page can
   open, and their atlases (EMIR / EMBA) already hold those uploads'
   texels. Anything else is refused (a loader fault).
6. **Clock.** main.c's field hook (the top of every frame) calls
   `em_scene_bindings_module_loader_field` before the dispatch. Only the
   measured drive reads it.
7. **Requests.** New Game's 001AD1A0 (em_scene_bindings `w_001AD1A0`)
   calls `em_module_loader_request_001FF080(loader, 0, 3)` and 001ADF50's
   001FF080(1, 0) (`w_001FF080`) `(loader, 1, 0)`; both leave D_00275BD8 as
   their callers set it (1) for the task's 0x63 step to clear.
   `em_status_runtime.c` `begin_module`: every page module (the page
   core's phase 3, the ITEM root's state 3, SPR4's state 3) calls
   `em_module_loader_request_001FF080(loader, 0, module)` and leaves the
   busy byte as its caller set it; the loader's 0x63 step clears it.
   Without a bound loader a page module faults (no instant path is left).
   The port-side `em_item_ui_deactivate` / `em_battery_ui_deactivate` calls
   stay for 0x1F and 0x21 (they are not original calls). The MAP page's
   D_0028A570 view is the loader's slot 0x38 (`EmStatusPagesFrame.
   d28A570`), which 001FF830 state 7 writes when module 0x1E loads (one
   storage; em_gs_texture's copy of the relocation serves only callers
   without a loader).
8. **Every page module** goes through the loader since chain step
   PAGELOADS (finding 8, section 3 I), as module 0x21 since C8b LOADER;
   the New Game module 3 (001AD1A0) and the area load since chain step H7.
   Module 0x27 (the game-over screen, 001AD4E0's 001FF080(0, 0x27)) is in
   the pack since chain step DAMAGE (tools/export_module_loader.py
   SCREEN_MODULES; one chunk, the screen's GS upload, which
   em_status_runtime's chain step counts and em_render_001ABF90 draws from
   its export; DAMAGE.md section 5). The exit's bank modules 0x32..0x35
   are not exported.
9. **Tick log.** Every tick carries `loader_pre`: the first 27 bytes of
   `em_module_loader_snapshot` (slot +0, +8..+0x1F, D_00275BD8,
   D_00282157) after the previous frame's slot-2 dispatch (the task runs
   after the game task that logs the tick). The run log's "module loader:"
   line (level smoke, newgame-control) gives the drive mode and the
   dispatch / read counters.
10. **The PS2 disc-drive timing switch** selects the measured drive
    (em_settings; LAUNCHER_OPTIONS.md "PS2 disc-drive timing").

### 4.1 The level smoke's rule for a module-0x21 load (the panel prompt and the battery pop-up)

`check_module_load` (tools/test_level_smoke.py) runs in both checks that
contain the load, with these names:
- L is the row whose ITEM state becomes 3: route 03 f390, route 01 f193
  (asserted); the port's tick is the one the preceding row-for-row window
  ends on.
- R = L + 1 is the request row (001FF080(0, 0x21) and the loader's first
  dispatch in the same frame).
- D is the port's first tick after R whose `loader` bytes show slot 2
  idle and D_00275BD8 = 0.

1. **Up to the load**: 1:1 through L (`compare_window` 'panel open' /
   'panel request'; for route 01, 'battery request').
2. **The load.** The port's `loader` rows R..D against the capture's
   f391..f414 (h7's `frames.jsonl`; route 01: f194..f217 of the load-wait
   probe, in its field form). At host speed the capture's rows that equal
   their predecessor while the slot runs are dropped (the 14 busy polls):
   10 rows, a shift of 14. With the PS2 disc-drive timing switch all 24
   rows, shift 0.
3. **From the load's completion to the Yes press** (route 03): the port's
   tick D + 1 + k against the capture's f415 + k in spad, camera byte,
   letterbox, message, power, B0/B1, the retained placement and heading,
   up to the first Yes press of either side (navigation input: the
   runner's press schedule starts at the prompt); and prompt_port - posted
   == prompt_orig - posted - shift (16 == 30 - 14 at host speed, 30 == 30
   with the switch). From the Yes confirmation on, the existing window
   aligns on its own rows. Route 01's notice (239 frames) is measured from
   its own sub-state 3 row, so the shift leaves it unchanged.
4. **Not compared across a host-speed load** (reported by their own
   checks): the frame counters run 14 behind; rand()-derived values:
   during the wait only 001D7C30 draws, twice per frame (route 01's rand
   capture), so the port's sequence is 28 draws behind the capture's at
   every aligned row after the load, and `check_rand_order` compares the
   aligned windows' callers, not the values; the stream lanes' refill
   phase counts fields in real time and is 14 behind.

## 5. Known gaps

- **001FB370's ninth call.** The PS2 took one more call on AREA11's bank
  than the port (section 1.9; IOP_STREAM.md "The sound-bank transfer"):
  most likely its SIF DMA's hardware time. Neither the Original profile nor
  the PS2 disc-drive timing switch reproduces it, so with the switch the
  New Game's veil runs 257 frames against the captures' 258.
- **The recorded New Game reads are frame-exact only within the capture's
  sampling** (section 1.7): two samples a frame, read while the VM ran.
  Only the New Game's reads of module 3 and AREA11 and module 0x21's are
  measured; any other read is answered at host speed and counted.
- **The area load after New Game.** Area changes (the level exit, the
  area-change test's reload) run the same loader. Only AREA11 is exported:
  another area's reads fault (fail-stop). A reload of AREA11 frees the
  bank's handle through 001195A8, which reads the SFX driver's published
  voice records.
- **Other untranslated loader paths**, each a fault if reached (none is on
  the first-level route): 00200360, the bank-set streamer (+8 = 2);
  001FF1E0 / 00200700, the boot-time synchronous loader. The kind-3
  modules 0x32..0x35 reach 001FB370 through 001FF830 state 6, which is now
  bound too, but none is exported (their reads fault).
- **The cursor seeds are exported** (section 1.8). The New Game's loads
  now recompute every one but D_0028A738 (the boot loader 001FF1E0's,
  which the port does not run); the exporter computes them from the disc
  with the loads' rules (`disc_seeds`, DISC_TEXTURES.md 9.1), equal to the
  values every first-level capture shows. They hold for AREA11 only.
- **00200970** is translated and verified but has no consumer (001CCB10 is
  untranslated). 00200890 runs live in the area load (state 7); the page
  core's `EM_STATUS_PAGE_PLAYER_TEXTURE` and the player step 0015C1F0 keep
  their current boundaries.
- **The area consumer applies nothing** (section 1.9): the port's textures
  are its disc export (DISC_TEXTURES test B); the loaded resident region
  and overlay are the drive's bytes, which the port's native code does not
  read.
- **Measured timing** exists for module 0x21's two reads and the New
  Game's (PCSX2 on the rebuilt image). With the switch on every other read
  (the other page modules' among them: no capture shows a page open) is
  answered at host speed and counted (`unmeasured`).
- **The page modules' payloads are not read by the port.** Module 0x1E's
  payload (the MAP model bank) lands in the drive's memory at D_0028A570;
  the MAP page draws its models from `tools/export_status_map.py`'s export
  of the same disc bytes (not compared byte for byte with the delivered
  payload).
- **Route 03 has no rand capture.** The 2-draws-per-frame figure is route
  01's identical load.
- **The consumer hook is port-side.** It accepts the proven upload and
  applies the module's GS blocks from the status pages' GS image; it does
  not decode the chain's transfer itself (section 3 I proves the two
  equal for the user's disc).
- **The loader bytes are logged after the dispatch.** The scene tick log's
  `post` byte D_00275BD8 is sampled when the game task ends, before slot 2
  runs, so on the load's last row it still shows 1 where the capture's
  frame-end sample shows 0; the smoke compares the `loader_pre` bytes,
  which are sampled after the dispatch.

## 6. Reproduce

```
python3 tools/export_module_loader.py                 # assets/module_loader/modules.emml (disc only)
python3 tools/test_module_loader_reference.py         # ~2.2 s (make test-module-loader-reference)
EM_TEST_FULL=1 python3 tools/test_module_loader_reference.py   # ~7 s
python3 tools/test_status_scene_reference.py          # the area streamer (section 3 F), ~2.5 s
python3 tools/test_sound_bank_reference.py            # 001FB370's chain (IOP_STREAM.md), ~3 s
python3 tools/check_no_disassembly.py src/game/em_module_loader.c src/game/em_module_loader.h \
    tools/export_module_loader.py tools/test_module_loader_reference.py docs/MODULE_LOADER.md
```
Inputs, all local and never committed. The exporter needs only the disc
image; the tests also need:
- the pinned ELF;
- `../Extermination/Extermination-rebuilt.iso`;
- `build/s87/route/{01_battery,03_panel_power}`;
- `build/s87/c7cap/{h7,rng/r01}` and `build/s87/loadwait/01_battery`;
- `build/startup-reference/panel/gs.bin`.

The test's `upload` check imports the decomp's `tools/export_level.py`
(`_bg_section_chain`, `_bg_gs_upload`) and `tools/gs_vram.py`.
