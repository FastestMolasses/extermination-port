# Saving and loading a game on the memory card, and EM_START_AT

Side lane "save" (2026-10-20). The user's decision of 2026-10-19 ("Like LOAD
GAME"): an instant area start for testing, `EM_START_AT=<area>`, that loads a
start-point save through the original's own load-game code, and with it real
memory-card saving and loading for players. This document records what the
original's save code does, as its instructions show it and as the
original-instruction oracle runs it. Addresses are the original's; no
instruction of the original is reproduced here.

## 1. Status

| Part | Original | Port | Checked by |
|---|---|---|---|
| The save writer | 00228A90 and its steps 00228530, 002285A0, 002288A0, 00229640, 00229680, 002284F0, 001AF220 | translated (em_save_original), not wired | test-save-reference |
| The card file commands | 001FE6B0 (one file), 001FE9E0 Open, 001FEB90 Write, 001FEA70 Close, 001FEDD0 Mkdir, 001FEC20 GetDir | translated (em_save_original), not wired | test-save-reference |
| The card screen around them | 00227300 and its screens (002280F0, 002283B0, 00229170, 00227980, 00227010, 00227160, 00226E90, 00226B90, 002267A0, 00226D50, 002293B0) | faults (docs/OPTIONS.md section 4.2) | - |
| The load side | 00228EB0 (the five slots read), 00228B60 (one slot read), 001FE7C0 / 001FEB00 (read), the restore and the area entry | not translated | - |
| The host card | em_memcard: GetInfo, Sync, and the file commands Open 00114360, Mkdir 00114448, Close 00114480, Write 00114708, Read 00114630, GetDir 00114AC0 (section 5) | answered at host speed on data/memcard/slotN; not wired into a live binder yet (the card screen faults) | test-memcard-host |
| Start-point saves, `EM_START_AT` | - | not built | - |

Next steps: section 6.

## 2. The save on the card

What a save writes, read back from the original's own writes in
`test-save-reference` (the whole-save cases check every claim of this
section against the files the original handed to Write):

- One directory per card, `BASCUS-97112-DS00-00` (the name at D_002738E0).
  Every file below is inside it, opened with mode 0x203.
- `BASCUS-97112-DS00-00` (D_00273910): 0x48 bytes from D_00821980: the
  title string at D_002738F8 copied to the start (00123168) and the word
  2.0f at +0x20; the other bytes as that .bss buffer holds them (zero in
  both recordings).
- `EX.ico` (D_002739B0): the icon, `c + c[0] + c[0x10]` for `c[0x18]` bytes
  of the icon container c = *D_0028A4A0 (resident in both recordings; 71,088
  bytes there).
- `icon.sys` (D_00273940): the 0x3C4 bytes of the .data image D_00266B30.
- `EX_DATA.00` .. `EX_DATA.04` (the names D_00267060 + 0x40 n): five save
  slots of 0x640 bytes, each a copy of the progress block D_00810700.

A new directory is written in that order (GetDir, Mkdir, the header file,
EX.ico, icon.sys, the five slots). Over an existing directory only the
chosen slot's file is written (002288A0).

### 2.1 A slot

The chosen slot is the live progress block D_00810700 with byte +0xF = 1
and a check byte at +0x63F: 002284F0 stores 0x100 minus the 8-bit sum of
bytes 0..0x63E, the sum taken before its own store of +0xF. It runs on
every frame of the slot's Open / Write / Close, so the block written has
been summed with +0xF already 1: all 0x640 bytes of a written slot sum to
zero (mod 256). The live block keeps those two bytes.

The other four slots of a new directory are the block New Game makes:
001AF220 keeps the live block at the scratchpad 0x70000640, runs New
Game's progress reset 001AF2C0 on D_00810700, copies the result to the
scratchpad 0x70000000, and copies the kept block back; 002285A0 hands the
copy at 0x70000000 (through D_008219D0) to Write. Those slots have +0xF = 0
(no check byte is computed; their bytes sum to 121 in both recordings) and the area
bytes +0..+2 all zero in both recordings. 00229680 writes the same block
to the chosen slot alone (through D_00822690).

001AF220 also stores D_00810700 = 0xB, D_00810701 = D_00810702 = 0 and
D_00810730[0xB] = 0 between its second and third copies; the third copy
(the kept block back over D_00810700) overwrites all four, so neither the
live block nor the scratchpad copy keeps them. The decomp's NEARMISS
comment on func_001AF220 reads the last copy in the other direction.

A save overwrites the scratchpad 0x70000000..0x70000C7F (the two copies).

### 2.2 The card screen's record

The writer runs on the card screen's record D_00810040 (docs/OPTIONS.md):

| Offset | Use |
|---|---|
| +3 | the writer's state: 0 look for the directory, 1 a new directory, 2 the slot over an existing one, 3 done |
| +4 | the step inside 002285A0 (0..4) or 002288A0 (0..1) |
| +5 | cleared when the writer finishes |
| +0xA | the chosen slot (0..4) |
| +0x48, +0x4C | the card port (0 or 1) and the card slot (0) |
| +0x50, +0x54 | the Sync command and result cells |
| +0x64 | 001FE6B0's file phase (0 open, 1 write, 2 close, -1 after) |
| +0x68 | 002285A0's slot iterator |
| +0x6C + n | slot n's presence byte (1 once written) |
| +0x74 + 12 n | slot n's header: the block's bytes 0..5, +6 its byte 0xA (002288A0 only), +8 its word 0x50 |

## 3. The translations and their oracle

`src/game/em_save_original.c` translates 00228A90, 00228530, 002285A0,
002288A0, 00229640, 00229680, 002284F0, 001AF220, 001FE6B0, 001FE9E0,
001FEB90, 001FEA70, 001FEDD0 and 001FEC20 (the decomp's C where it is
byte-matched; the instructions for the NEARMISS 002285A0, 002288A0,
001AF220 and 001FE9E0). The Sync poll 001FE9A0 runs as its options
translation, em_options_001FE9A0. Every other original is a callee by its
address on the AREA01 UI lane's memory model (em_area01_ui.h): the libmc
SDK 00114360 / 00114448 / 00114480 / 00114708 / 00114AC0, block_copy
00121870, 00121A28, 00123168 and 001AF2C0.

The five command routines share one shape on the card helpers' phase
D_00275C5C: at phase 0 they set their phase (Open 2, Close 3, Write 6,
Mkdir 0xB, GetDir 0xD) and issue the SDK call (a nonzero v0 sets the error
flag D_00275C58); at their phase they take the result 001FE9A0 kept in
D_00275C64 (Open, Write and GetDir accept a nonnegative result and return
it, Close and Mkdir accept 0), or set the error flag; any other phase sets
the error flag. They return -1 until a result is accepted. 001FE6B0 runs
one file over three of them, one command per frame, and reports 1 only when
Write's count equals the size it was given.

`make test-save-reference` (`tools/test_save_reference.py`) executes the
original routines over two recordings' RAM (the fork's
`c10/options/opt_07_load_cancel` and `s87/route_a01/a01_s4_east_room`, the
room of AREA01's save terminal) with every callee outside the lane run as
original code too, and compares RAM, the scratchpad and the arguments at
every callee entry and at the end. The SDK calls are the scripted boundary:
a small card model answers them identically on both sides (the IOP's card
server is not EE code, so its answers are inputs here, not claims). Cases:
every command routine and 001FE6B0 on every phase and result; 002284F0
over filled blocks and 001AF220 on both recordings; every writer step and
state; 110 whole saves frame by frame from 00228A90's first call (a new
directory and an existing one, every slot, both ports, busy Syncs, a
failing result or a refused command at each of the 26 commands of a new
directory, short writes) and 00229680's slot write. 120 of the 124 branch
outcomes are taken; the four others are unreachable (001FE6B0 returns only
-1, 0 or 1, and 0 only with the error flag clear) and listed in the test.
Run 2026-10-20: quick mode PASS (under 1 s), EM_TEST_FULL=1 PASS (718
cases, 18,106 callee entries equal). Mutants checked: the Open mode, the
check byte's range, the iterator bound, each header store: all fail the
quick run.

## 5. The host card's file commands (em_memcard)

The card server is the IOP's (mcserv / mcman), not EE code; the port's
stand-in is `src/game/em_memcard.c`, which answers every libmc call the save
and load code makes, at host speed, on the host card folders
`data/memcard/slot1` (port 0) and `slot2` (port 1). The EE side of each call
is the original SDK's (the decomp's C: 00114360, 00114448, 00114480,
00114708, 00114630, 00114AC0, 00114848, all ee-gcc; 00114360 and 00114708
NEARMISS): with a command pending (D_00241D68) the call returns its number
and issues nothing; otherwise the command is issued with v0 0, and the next
Sync stores its number and the result word D_0027C680. Mkdir is 00114360's
Open with mode 0x40, its pending number then set to 0xB.

| Call | Command | Result | Host |
|---|---|---|---|
| Open(port, slot, name, mode) | 2 | a descriptor 0..2 (the lowest free) | opens `slotN/<name>`; mode 1 / 2 / 3 (read / write / both), 0x200 creates; no truncation |
| Mkdir(port, slot, name) | 0xB | 0 | makes the directory |
| Close(fd) | 3 | 0 | closes the file |
| Write(fd, buf, size) | 6 | size | writes the EE bytes at the descriptor's offset |
| Read(fd, buf, size) | 5 | the bytes read (0 at the end) | the bytes reach EE memory at buf |
| GetDir(port, slot, name, 0, max, table) | 0xD | the entries stored | see below |

GetDir with a plain name stores that one entry if it exists (result 1) and
nothing if not (0): 00228530 / 002283B0 / 00229A70 ask
`BASCUS-97112-DS00-00` this way. With a pattern (`D/*`, the load
enumeration 00228EB0 asks `BASCUS-97112-DS00-00/*` for at most 0x12) it
stores "." and "..", then D's entries in byte order of their names (host
names beginning with '.', such as Finder's .DS_Store, are not the card's).
An entry is 0x40 bytes with the name at +0x20; the port leaves the first
0x20 bytes (the server's dates, size, attributes) zero, which the only
reader, 00228C90, never looks at (it compares names). Every condition no
recording shows the server's answer for faults (latched, as GetInfo's
policy): a name that is not a relative path of card names (`..`, a leading
'/', a component over 31 bytes or with other characters), a wildcard at the
card's root, a GetDir that continues a listing, any other Open mode, a
missing file without create, a file in a missing directory, a directory
opened as a file, a Mkdir of an existing name, a fourth descriptor, a
descriptor not open, a Write on a read-only or a Read on a write-only one,
a size outside 0..8 MiB, and memory outside the caller's. The bytes Write is
handed are read, and the bytes Read and GetDir store are written, when the
command is issued; nothing reads them before the Sync that completes it.
`em_memcard_call(target, registers, ...)` answers any of the eight entries
by address, for the binder the card screen will use.

What is a claim and what is checked. `make test-memcard-host`
(`tools/test_memcard_host.py`) runs the ORIGINAL save writer 00228A90
frame by frame on both sides of test-save-reference's harness (the
original's instructions over the a01_s4 / opt_07 recordings' RAM, and the
em_save_* translations) with em_memcard answering both sides, each on its
own host folder: RAM, the scratchpad and the arguments equal at every
callee entry, the two host cards byte-identical, and the folder holding
section 2's layout. Then the ORIGINAL code reads the card back: a second
save into the existing directory, and the card screen's load enumeration
002283B0 (GetDir, 00228EB0: the listing, the name check 00228C90, five
Open / Read / Close of the EX_DATA slots, the check byte 00228C50,
GetInfo), which must report 1 with both saved slots present and their
records equal to the saved blocks' headers; with one byte of a slot
changed, or icon.sys missing, the original rejects the card (3). Plus 89
direct calls covering every answer and refusal. Run 2026-10-20: quick
PASS (2 saves, 2 round trips, 3.7 s), EM_TEST_FULL=1 PASS (40 saves on
both recordings, every slot and port, 12 round trips, 852 libmc calls).
Eight mutants of em_memcard (Read's count, GetDir's names, its plain-name
count, the dots, Mkdir's result, the Write offset, the first descriptor)
all fail it. The server's own answers (the descriptor numbers, GetDir's
entry order and its first 0x20 bytes, the result on a fresh card) are
what the original EE code accepts, not yet what the IOP was recorded
answering: that is section 6's first step.

## 6. Next steps

In order: a fork recording of the original saving at AREA01's save
terminal on a separate card file (the user's decisions of 2026-10-09, HANDOFF
item 9: a dedicated card, never the user's own), whose libmc results (the
Sync result words, the GetDir table) check em_memcard's answers and whose
card files check section 2 against the card the IOP wrote; the load side's
translations (00228EB0, 00228B60, 001FE7C0, 001FEB00, 00228C90, 00228C50
and LOAD GAME's restore and area entry); the card screen 00227300 and the
save terminal's Yes path, wired through `em_memcard_call`; the start-point
save builder (the fork recordings route_a01 / route_a00 / route_a01r at
each area arrival, kept in ignored build/); `EM_START_AT` (docs/STARTUP.md
"Developer switches").

## 7. Re-exports

None: the save code is in the boot ELF, and the test reads the recordings
in the reference `tools/reference_root.py` selects.
