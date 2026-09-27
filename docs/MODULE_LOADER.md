# MODULE_LOADER: the screen-module loader behind the panel prompt (H7)

Lane b15 "module_loader", 2026-09-27. New files only:
`src/game/em_module_loader.{h,c}`, `tools/export_module_loader.py`,
`tools/test_module_loader_reference.py`, this doc. Nothing is wired yet;
the "Binding" section tells the chain how to wire it.

**What this adds.** The loader's state machine was already translated and
verified in `em_status_scene_original.c` (STATUS_SCENE.md section 2). This
lane reuses that translation as is:
- 001FF080, the request;
- 001FF0D0, the slot-2 task;
- 001FF830, the bank streamer;
- 001FF3F0, the chunk streamer;
- 001FEF70, the bank chaining.

This lane adds:
- translations of the I/O routines the loader calls: 00200780, 00200730
  and 00200830;
- translations of the two player-texture routines in the same range:
  00200890 and 00200970;
- a host "drive" that answers the SDK leaves at host speed from the user's
  exported disc sectors;
- a live em_task slot-2 binding.

So the port runs the loader's own steps. The drive's time goes (user policy,
port CLAUDE.md 2026-09-27): the module-0x21 load takes the loader's
**10 dispatches** at host speed, where the original takes 24. The measured
PCSX2 drive time is available as an option (`EM_MODULE_LOADER_DRIVE_MEASURED`,
off by default). With the option on, the load reproduces all 24 captured
frames exactly.

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
- +8 == 1 runs 001FFCD0 and +8 == 2 runs 00200360. Both are untranslated,
  so they fault.
- +8 == 0x63: D_00275BD8 = 0 and the slot goes idle (001AB7D0).
- Native form: `em_status_scene_loader_001FF0D0`, called by
  `em_module_loader_dispatch` over the em_task record.

**001FF830 / 001FF3F0** (STATUS_SCENE.md section 2) run a load as a sequence
of steps:
1. The header read: one sector of INDEX.IDX.
2. The chunk reads: A entries, each read into D_00275C74 and DMA'd through
   00200830.
3. The payload read.
4. The cursor commit by kind.
5. The section DMAs.
6. The relocation words D_0028A490[e >> 24] = (e & 0xFFFFFF) + base.

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

**The measured option.**
- A read keeps sync busy for its measured number of fields, counted on
  `em_module_loader_field`, which is called once per frame.
- The only measurements are module 0x21's reads, named by file, sector
  inside the file and sector count, so a disc image that places the two
  files elsewhere still matches them:
  - the header, INDEX.IDX sector 0x21 (1 sector), is busy for **6** fields;
  - chunk 0, DATA.DAT sector 0x1C978 = byte 0xE4BC000 (161 sectors), is
    busy for **8**;
  - a zero-sector read, for 0.
  On the rebuilt image these are lsn 0x9D7F1 and 0x9CB50.
- Sources: STATUS_LOAD_WAIT_PROBE.md, and CAPTURES_C7.md section 6 in the
  decomp repo.
- Any other read is answered at host speed and counted as `unmeasured`.
  Nothing is extrapolated.

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
differs, because the next area is loading. The exporter writes these seeds
(`AREA11_SEEDS`) unless it is given `--capture`. They are observed
addresses, not disc data, and the port does not compute them (section 5).

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
4. **The relocation slot table is larger than the 68 words the existing
   translation models.** Real module headers name slots up to 0x9A. Slot
   0x44 is D_0028A5A0 itself (module 0x1D relocates its own kind-2 cursor),
   0x86 is 0x28A6A8 and 0x87 is 0x28A6AC.
   - 14 of the disc's headers stop at such a slot in the native module
     (EM_STATUS_SCENE_RELOC_WORDS = 68): 4..8, 0xB, 0xC, 0x13, 0x14,
     0x16, 0x17, 0x1A, 0x1D and 0x36.
   - The kind-3 banks 0x32..0x35 (slot 0x86) would stop there too, after
     001FB370.
   - In each case the original writes the named global at exactly the
     faulting address; the sweep checks this.
   - None is on the first-level route: 0x21 and 0x1F have no relocations,
     and module 3 uses slots 8..52.
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
   state 2 already sets `asset_busy = 1` there, so the loader needs no
   extra writer.

## 3. Verification

`python3 tools/test_module_loader_reference.py`:
- builds a private dylib under `build/b15/module_loader/`;
- exports two private packs there from the user's ISO: one with
  `--capture` (route 03, its checks and its cursors), and the disc-only one
  an end user makes. The test requires the two to differ only in the
  D_00275C74 seed word;
- takes about **1.8 s**. `EM_TEST_FULL=1` takes about **6.4 s**.

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
  D_00282157, the seven cursors, 68 relocation words and the header.
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
  kind-3 ids: 31 load through; 14 stop at a slot past the model, at the
  address the original writes.

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
- +8 = 1 or 2: 001FFCD0 / 00200360, NULL_WORKER;
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
- **Equivalent, proved in writing: the `d810CA4` / `d810CA6` views
  ignored.** `em_status_scene_loader_001FF0D0` reads those bytes only in
  its +8 == 1 branch, and only after `w->w_001FFCD0` has returned (the
  001FEF70 call after it). `em_module_loader_open` leaves `w_001FFCD0`
  NULL, so that branch faults at 0x001FFCD0 before the bytes are read. No
  output of this module can depend on them until 001FFCD0 is translated;
  the chain that binds it must add a pinned case for them.

`python3 tools/check_no_disassembly.py` is clean on all five files.

## 4. Binding (for the chain)

1. **Asset.** `python3 tools/export_module_loader.py` needs only the
   user's disc image. It writes `assets/module_loader/modules.emml` and
   `modules.json` (ignored, disc-derived):
   - the file descriptors are the ISO's INDEX.IDX and DATA.DAT extents;
   - the cursor seeds are `AREA11_SEEDS` (section 1.8);
   - module 0x21 by default; add others with `--modules 0x21,0x1F,...`
     (item 8).

   `--capture <folder>` (developer only) adds the capture checks: the
   descriptors, the 0x21 header against D_00289BC0, the chunk against
   D_00275C74, and whether the captured cursors equal `AREA11_SEEDS`. It
   also takes that capture's cursors as the seeds.
2. **Create** the loader once, where the AREA11 interaction host binds the
   status models:
   - `ml = em_module_loader_open(path)`. A NULL result refuses the host
     load, as em_status_models does.
   - `em_module_loader_bind_live(ml)`.
   - At unload: `em_module_loader_bind_live(NULL)`, then
     `em_module_loader_close(ml)`. Unload only while slot 2 is idle; a
     slot-2 dispatch with nothing bound latches the orphan fault.
3. **Faults.** Every frame, after `em_task_dispatch()`, the host calls
   `em_module_loader_failed(ml, &f)` and `em_module_loader_orphaned(&f)`.
   If either returns 1, the host fails at once (the interaction runtime's
   `failed`, with `f.address` and `f.code` in the log). A latched fault
   stops every later dispatch, slot 2 stays running and D_00275BD8 stays 1,
   so without this check a fail-stop shows only as an ITEM root hung in
   state 3.
4. **Views** (`em_module_loader_set_views`):

   | View | Point it at |
   |---|---|
   | `d275BD8` | the one unified D_00275BD8 (below) |
   | `r_00282157` (+ ctx) | a `uint8_t (*)(void *)` reader of the stream lanes' D_00282157. `em_stream_live_read_phase()` is `uint8_t(void)`, so pass a one-line wrapper; em_scene_bindings.c's static `r_00282157` is exactly that wrapper |
   | `d810CA4` / `d810CA6` | the D2 bytes (read only after 001FFCD0, untranslated) |
   | `spad3B90` | the port's 0x70003B90 byte, or NULL (only modules 0x2A/0x2B read it) |

   **D_00275BD8 has more than one storage in the port today.** The
   original has one byte:
   - `EmStatusRuntime.page.item.asset_busy`: em_item_root.c (state 2 raises
     it, state 3 waits on it), em_status_page.c and em_status_runtime.c
     (`begin_module` clears it);
   - `em_scene_state()->d275BD8` (em_scene_bindings.c `s_state`): the scene
     tick log's byte (em_scene_bindings.c:541), 001AE040's world-frame test
     and its state-6 wait (em_scene_frame.c:356 and :468), the scene tasks'
     module-3 / 0x27 waits (em_scene_task.c:262..334) and the bindings'
     resident loads (em_scene_bindings.c:1807..1813, :2635, :2641);
   - `SP_D_00275BD8`, the status-pages lane's memory-model address.

   Before the loader is bound these must become one byte, and `d275BD8`
   must point at it. Until then, from L (f390) to D the scene tick log's
   byte is 0 where the original's is 1, and 001AE040's `D_00275BD8 == 0`
   branches would see 0 during the load. The smoke must then list the scene
   log's `d275BD8` column in L..D as a known divergence, not compare it.
5. **DMA consumer** (`em_module_loader_set_chain_hook`).
   - For module 0x21 the chain is the BATTERY page's upload, and the port's
     resident `battery.emba` holds exactly those texels (finding 1).
   - The hook must return 0 only when it receives that chain: chain ==
     D_00275C74 while slot 2's +0xE is 0x21, size >= 0x50800. Anything
     else returns -1, which is a fail-stop, never a silent accept.
   - The GS memory before the upload is not modelled. The atlas is resident
     all the time, and the page draws from it only from item state 5, after
     the load.
6. **Clock.** Call `em_module_loader_field(ml)` once per frame, before
   `em_task_dispatch()` (em_frame, before step E). Only the measured option
   reads it.
7. **Request.** In `em_status_runtime.c` `begin_module`, module 0x21 must
   call `em_module_loader_request_001FF080(ml, 0, 0x21)` instead of the
   resident branch. That branch clears `asset_busy` at once. Return 1 on
   success and leave `asset_busy` alone: the loader's 0x63 step clears it,
   and em_item_root's state 3 step 1 advances on it.
   - Module 0x1F stays on the resident branch until item 8 is done.
   - Keep the port-side `em_item_ui_deactivate` / `em_battery_ui_deactivate`
     calls; they are not original calls. Moving them is the chain's call.
8. **Other modules.** Other page loads can use the same request once each
   meets two conditions:
   - its sectors are exported;
   - its upload is proven equal to the atlas the port draws, as in finding
     1. Only 0x21 is proven here.

   The pages are 0x1F (hub), 0x20, 0x22, 0x23, 0x2C and 0x2D..0x31.
   The last six are the status-pages lane's `SP_001FF080`. The New Game
   module 3 (001AD1A0) is verified as a whole load (B), but it has no
   consumer: its data is resident port assets.
9. **Tick log.** Add the 27 first bytes of `em_module_loader_snapshot`
   (slot +0, +8..+0x1F, BD8, gate) as a `loader` field. The smoke needs it
   for the rule below.
10. **Retire when bound.**
    - `begin_module`'s instant path for 0x21.
    - The LEVEL_SMOKE "What the full route does not yet compare" row
      "panel (03) … prompt window", and the matching "known divergence"
      paragraph. Both are replaced by the rule below.
    - FIRST_LEVEL_AUDIT H7's load-wait part.
    - The census rows 00200780 / 00200730 / 00200830 become live through
      this binding. 00200890 / 00200970 stay verified-unbound (gaps).
    - STATUS_SCENE.md section 5: `w_00200780`, `w_00200730` and
      `w_00200830` become bound.
    - Record the measured-drive switch in `docs/LAUNCHER_OPTIONS.md`
      (Original value: off).

### 4.1 The rule for the smoke's panel-prompt check (and the battery pop-up)

**Which drive the smoke runs.** The strict 1:1 comparison of a beat that
contains a 0x21 load runs with the measured drive
(`EM_MODULE_LOADER_DRIVE_MEASURED`): the shift is 0, the 24 load rows and
every later row, rand-derived fields included, compare 1:1 (section 3 C).
The host-speed default (the Original profile as shipped) gets the shift
rule below. Both are required: the measured run proves the steps and
everything after them, and the host-speed run proves that removing the
drive's time removes nothing else.

Use the following names:
- L is the row whose ITEM state is 3 at step 0. The original's is f390 in
  route 03 and f193 in route 01; the port has the same row today.
- R = L + 1 is the request row: the port's tick with `loader` +8 = 0,
  +9 = 1.
- D is the port's first tick after R with the slot idle (+0 = 0) and
  BD8 = 0.

The host-speed rule:
1. **Up to the load.** Compare 1:1 through L, as today (`compare_window`
   'panel open' / 'panel request'; for route 01, 'battery request').
2. **The load.** The port's rows R..D are compared with the capture's
   rows f391..f414 in their `loader` bytes, after dropping each captured
   row that equals its predecessor while the slot state is 2. Those are
   the 14 busy polls f392..f397 and f400..f407; route 01's are f195..f200
   and f203..f210.
   - The source for route 03 is h7's `frames.jsonl`; for route 01 it is
     `loadwait/01_battery/probe_boundary.json`.
   - Require D - R + 1 == 10 == 24 - 14.
3. **From the load's completion on**, the port's tick D + 1 + k is the
   capture's row f415 + k (route 01: f218 + k): a constant shift of
   exactly 14 rows per host-speed load (a run spanning several loads adds
   14 for each).
   - Compare every field that does not come from rand() at this shift
     until the Yes confirmation. That window aligns on its own `yes_*`
     rows, as today.
   - Require prompt_port - posted == prompt_orig - posted - 14. Today's
     numbers become 16 == 30 - 14.
   - For route 01, the battery notice's 239-frame hand-over is measured
     from its own sub-state 3 row, so the shift leaves it unchanged.
4. **Fields that do not follow the shift** are reported, not compared, in
   the host-speed run:
   - the frame counters (`counter`, `f`, vsync) run 14 behind;
   - **every rand()-derived value, for the rest of the run.** During the
     wait only 001D7C30 draws, twice per frame (return addresses 0x1D7D44
     and 0x1D7DD4; route 01's rand capture shows only those two from f192
     to f484). The 14 dropped frames therefore leave the port's rand()
     sequence 28 draws behind at every aligned row after the load. When
     the page closes, the world's callers resume (return addresses
     0x1F4D74, 11 per frame, and 0x1F54FC, 9 then 8 per frame, from f486
     in route 01), and their values (particles, effects, anything seeded
     from rand()) differ from the capture's at the aligned rows until the
     run ends. `check_rand_order` holds in index form: counted from the
     request, the port's n-th draw after the load equals the capture's
     (n - 28)-th (for n < 28, a capture draw inside the load);
   - anything that counts fields in real time (the stream lanes' refill
     phase) is 14 fields behind at that point.

## 5. Known gaps

- **Untranslated loader paths.** These are reached only off the
  module-0x21 path, and every one faults:
  - 001FFCD0, the area streamer (+8 = 1), with its 001FF590 and 002009E0;
  - 00200360, the bank-set streamer (+8 = 2);
  - 001FB370, the kind-3 finaliser (modules 0x32..0x35);
  - 001FF1E0 / 00200700, the boot-time synchronous loader.
- **The cursor seeds are observed, not computed** (section 1.8). The loads
  that produce them (001FF1E0, the New Game module-3 load, 001FFCD0) do
  not run through this loader in the port. The seeds are the values every
  first-level capture shows; they hold for AREA11 only. Translating and
  binding the area streamer replaces them. For module 0x21 only D_0028A748
  matters.
- **D_00275BD8 has three storages in the port** (Binding item 4). The
  loader writes the byte its view names; unifying them is the chain's
  first step.
- **The relocation table** stops at 68 words (finding 4). When 001FFCD0 is
  bound, its 001FEF70 chaining into 0x32..0x35 needs the table widened to
  the real slot range, with the named globals it aliases (at least up to
  slot 0x9A, including D_0028A5A0 at 0x44). That change belongs to
  `em_status_scene_original.c` and its test.
- **The `d810CA4` / `d810CA6` views are unexercised**: nothing reaches
  them before 001FFCD0 is translated (section 3, negative controls).
- **00200890 / 00200970** are translated and verified, but have no
  consumer. Their packets are module 3 / 0x1B relocation slots, which the
  drive only delivers if those modules load through it, and 001CCB10 is
  untranslated. The page core's `EM_STATUS_PAGE_PLAYER_TEXTURE` and the
  player step 0015C1F0 keep their current boundaries.
- **Measured timing** exists for module 0x21's two reads only (two data
  points, PCSX2 on the rebuilt image). The option answers every other read
  at host speed and counts it.
- **Module 0x1F and the other page modules** have no upload-equals-atlas
  proof yet (Binding item 8).
- **Route 03 has no rand capture.** The 2-draws-per-frame figure is route
  01's identical load.
- **The consumer hook is port-side.** It accepts a proven upload and
  models no GS memory. A GS-memory model would be needed only if a page
  sampled what was in that region before its upload; none on the route
  does, because pages draw after the load.

## 6. Reproduce

```
python3 tools/export_module_loader.py                 # assets/module_loader/modules.emml (disc only)
python3 tools/test_module_loader_reference.py         # ~1.8 s
EM_TEST_FULL=1 python3 tools/test_module_loader_reference.py   # ~6.4 s
python3 tools/check_no_disassembly.py src/game/em_module_loader.c src/game/em_module_loader.h \
    tools/export_module_loader.py tools/test_module_loader_reference.py docs/MODULE_LOADER.md
```
Inputs, all local and never committed. The exporter needs only the disc
image; the test also needs:
- the pinned ELF;
- `../Extermination/Extermination-rebuilt.iso`;
- `build/s87/route/{01_battery,03_panel_power}`;
- `build/s87/c7cap/{h7,rng/r01}` and `build/s87/loadwait/01_battery`;
- `build/startup-reference/panel/gs.bin`.

The test's `upload` check imports the decomp's `tools/export_level.py`
(`_bg_section_chain`, `_bg_gs_upload`) and `tools/gs_vram.py`.
