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
| The card screen past the slot choice | 00227300 and the routines below it (002280F0, 00228320, 002293B0, 00229170, 00227980, 00227010, 00227160, 00226E90, 00227C60, 00227DE0, 00228070, 001FED40, 00227270, 00226B90, 002267A0, 00226D50; section 6) | translated (em_card_original), not wired: the options binding still faults at 002267A0 / 00227300 (docs/OPTIONS.md section 4.2) | test-card-screen-reference |
| The load side | 002283B0 (the load enumeration), 00228EB0 (the five slots read), 00228B60 (LOAD GAME's read), 00228C90, 00228C50, 001FE7C0 / 001FEB00 (read) | translated (em_save_original), not wired (section 4) | test-load-reference |
| LOAD GAME after the read | what the callers of the card screen (0022A650 in the options, 001AC070 on the title) do with its result 2, the restore and the area entry | not translated | - |
| The host card | em_memcard: GetInfo, Sync, and the file commands Open 00114360, Mkdir 00114448, Close 00114480, Write 00114708, Read 00114630, GetDir 00114AC0 (section 5) | answered at host speed on data/memcard/slotN; not wired into a live binder yet (the card screen faults) | test-memcard-host |
| Start-point saves, `EM_START_AT` | - | not built | - |

Next steps: section 7.

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

The load side (section 4) uses +2 (the load enumeration's state), +5 (the
slot enumeration's state; the writer's +5 is the same byte), +6 (the slot
cursor), +0x58 (GetInfo's type word), +0x64 (001FE7C0's file phase), +0x68
(the count of saved slots found) and the same +0x6C / +0x74 tables.

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

## 4. The load side

What the original's load code does, as test-load-reference runs it (the
translations are in em_save_original, prefix em_save_, not wired yet; they
follow the decomp's byte-matched C, and the instructions for 00228C90,
NEARMISS there as a register permutation):

- **The load enumeration 002283B0** (the card screen's, on the record's +2,
  gated by the Sync poll 001FE9A0): GetDir of the game's directory by its
  plain name (D_002738E0, at most one entry). None: the five presence bytes
  +0x6C.. cleared and 1 (a card without a save). One: +2 = 1, then 00228EB0
  every frame until it reports: 3 (a bad card) and 2 (an error) are
  returned, 1 sets +2 = 2, and +2 = 2 returns 1. Every other frame returns
  2 when the card helpers' error flag D_00275C58 is set, else 0.
- **The slot enumeration 00228EB0** (on +5): state 0 clears the presence
  bytes, GetDir's 0x480-byte table D_00821500, the cursor +6, the count
  +0x68 and the file phase +0x64. State 1 lists the directory
  (`BASCUS-97112-DS00-00/*`, D_00273980, at most 0x12 entries) and runs the
  name check 00228C90 over the entries returned: all eight files must be
  there (the header file, EX.ico, icon.sys, EX_DATA.00 .. 04; strcmp 00123020
  against the name at +0x20 of each 0x40-byte entry; their order and any
  other entry do not matter), else 3. State 2 reads each slot file in turn
  (001FE7C0: Open mode 1, 0x640 bytes into the staging block D_00822050): a
  failed command or a short read gives 001FE8D0 (the card helpers reset)
  and 3; a slot whose +0xF is 1 must have all 0x640 bytes summing to zero
  (00228C50), else 3, and is then present (+0x6C + n = 1; its record +0x74 +
  12 n = bytes 0..5 and 0xA, its word 0x50 at +8; the count +0x68 + 1); a
  slot whose +0xF is not 1 (the New Game block the writer puts in the other
  four slots) is read, accepted and not present. State 3 asks GetInfo
  (001FECB0, +0x58 the type pointer): result 0 clears the record's bytes
  +5..+7 (00225CF0) and reports 1; any other result sets the error flag and
  reports 2.
- **LOAD GAME's read 00228B60** (gated by the Sync poll): the chosen slot's
  path `BASCUS-97112-DS00-00/EX_DATA.%02d` (sprintf 00122D10 with the format
  D_00273960 and the slot +0xA) into D_00822010, then 001FE7C0 reads that
  file's 0x640 bytes straight into the live progress block D_00810700. 1
  when done, 2 on the error flag or a failed or short read, else 0. It does
  not test the check byte: the enumeration's test is the only one. (The
  decomp's comment on sub_s_EX_DATA_02d calls the sprintf a blit into a work
  buffer and the read an upload; the instructions show a path and a file
  read.)
- **The file reader 001FE7C0** is 001FE6B0's twin on the read side's
  descriptor D_00275844 and count D_00275C6C: Open (mode 1), Read (001FEB00,
  phase 5 of the helpers' D_00275C5C, over the SDK's 00114630), Close, one
  command per frame; 1 only when Read's count equals the size.

What the card screen does with the block read (the restore of the rest of
the game's state and the area entry) is in 00227300 and below, not yet
translated (section 6).

`make test-load-reference` (`tools/test_load_reference.py`) runs the
original routines over the opt_07 and a01_s4 recordings' RAM against the
translations as test-save-reference does (every callee outside the lane,
00121A28, strcmp 00123020 and sprintf 00122D10, as original code; RAM, the
scratchpad and the arguments equal at every callee entry and at the end).
The SDK is that test's scripted card model, extended with Read, GetInfo and
GetDir's table; its answers are inputs, not claims about the server. Cases:
the Read command and the reader on every phase and result; 00228C50 and
00228C90 over constructed blocks and tables (each of the eight names missing
in turn, short counts, a negative count, near-miss names); every
enumeration state; 82 whole enumerations frame by frame (no directory, saved
and New Game slots mixed, both ports, busy Syncs, a failing result or a
refused command at each of the 18 commands, a short read, a changed slot
byte, each file missing, a failing GetInfo), each result and slot record
checked against what the files imply; 34 LOAD GAME reads (every slot, both
ports, the failures), D_00810700 checked equal to the slot file. 156 of the
160 branch outcomes are taken; the four others are unreachable and listed in
the test (001FE7C0 returns only -1, 0 or 1; 00228EB0 only 0..3; 00225CF0 is
only called with 5..7). Run 2026-10-20: quick PASS (76 cases, about 1 s),
EM_TEST_FULL=1 PASS (554 cases, 11,922 callee entries equal, about 6 s).
Mutants checked (the check byte test's sense, the name check's fifth slot
name, the record's +6 source, the Open mode, the count compare, the
no-directory clear, the count increment, sprintf's slot argument, the read
error's reset 001FE8D0): all fail the quick run.

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

## 6. The card screen past the slot choice

What the code does from the slot screen's state 2 (00225D20 calls
00227300(m) once per frame, m = D_00810040; m[0x14] is the mode, 1 save
and 2 load, m[0x48] the port chosen in 00226070). The translations are in
`src/game/em_card_original.c` (prefix em_card_; the decomp's byte-matched C,
the instructions for the NEARMISS 002280F0, 00228320, 002293B0, 002267A0 and
the asm body 00227C60). Line numbers below are the help container's group
7 lines that 001FCBD0 draws; their text is not read here.

- **00227300** draws the port's line (3 or 4), then runs the screen m[1]:
  0 the card check, 1 the load enumeration 002283B0 (section 4), 2 the slot
  list, 4 an error screen chosen by m[2]. Its result is 00225D20's: 1 back
  to the card choice, 2 out of the card screen, 3 done (00225D20 then
  returns 2, a game saved or loaded).
- **The card check 002280F0** (gated by the Sync poll, on m[2]): GetInfo
  with the format word (+0x60): a type other than 2 or a failed poll gives
  4; the format word 1 moves on; any other format word is asked again up to
  seven times (the count +0x20), then 2. Then GetDir of the game's
  directory (at most one entry): there gives 1; none gives 1 in load mode,
  and in save mode a GetInfo with the free count (+0x5C), whose result
  00228320 tests: free >= 18 + the icon's size in KiB (rounded up) gives
  1, else 3. 1 goes to the load enumeration; 2, 3 and 4 to the error
  screens 0, 1 and 2. The enumeration's 1 goes to the slot list, its 2 to
  error screen 2 and its 3 to error screen 3.
- **The slot list** draws the five rows every frame (002293B0: the number;
  an empty slot line 0, an unusable one line 1 (presence 2), a saved one
  its place name, D_002671C0[record byte 1 + the first index the area's
  entry in D_00289B40 holds], and its play time, the block's frame counter
  +0x50, as hours / minutes / seconds, plus the glyph 0x9E when the
  record's byte 6 (the block's byte 0xA) is set). Its steps m[2]:
  0 the cursor 00229170: Up / Down (the repeat word D_00810E78) move m[0xA]
  within 0..4; Cross takes the slot (in load mode only a saved one);
  Circle backs out to the card choice, Triangle out of the screen; a card
  whose GetInfo no longer reports type 2 sends the slot screen back to its
  card poll (m[0x15] = 1) at 00226070's step 1, 002267A0 (m[1] = 1).
  1 the confirmation 00227980: the question (save mode by the slot: empty
  0x14, saved 0x15, unusable 0x27; load mode 0x1A), Yes / No moved by Left
  and Right (D_00810E74 0x2000 / 0x8000; m[9] = 1 is Yes). Yes goes on
  (and in save mode takes 4 off D_00810CB2); No, Circle back to the cursor;
  Triangle out.
  2 the save or the load: save mode over an unusable slot 00229680 (the
  New Game block, line 0x22), save mode 00228A90 (line 0x10), load mode
  LOAD GAME's read 00228B60 (line 0x16). Done goes to 3; a failure to 4,
  and in save mode gives the 4 back.
  3 the done message 00227010 (line 0x12 saved, 0x18 loaded, 0x24 after
  00229680) for 45 frames, then 3; after 00229680 the card check runs
  again instead.
  4 the failure message 00227160 (0x13 / 0x19 / 0x25): Cross or Circle 1,
  Triangle 2.
- **The error screens** (m[1] = 4): 0 (the format word never 1): in save
  mode the format prompt 00227C60 / 00227DE0 (line 0xD, Cross, then line
  0x1F with Yes / No); Yes runs the libmc Format 00114D18 through 001FED40
  (the helpers' phase 0x10) and 00228070, line 0x1B while it runs; done:
  the save goes straight to slot 0 (m[1] = m[2] = 2, the presence bytes
  cleared, 4 taken off D_00810CB2); a failed format shows line 0x1E
  (00227270). In load mode screen 0 draws the slot list and runs the
  cursor, but a chosen slot does nothing there: only Circle (1), Triangle
  (2) or a card change leave it.
  1 (the free count short) 00226B90: line 5, Cross, line 7, Cross: 1.
  2 (no card of type 2) 002267A0: line 0xE (port 0) or 0xF (port 1); it
  polls GetInfo on the chosen port and the other in turn and returns 1
  once one reports type 2 (setting its flag +0x28 / +0x2C); Cross 1,
  Circle or Triangle 2. 3 (the enumeration refused the card) 00226D50:
  line 0x28, Cross or Circle 1. 00226B90 and 00226D50 take Circle as 1
  and Triangle as 2, and return 1 when the card's GetInfo stops reporting
  type 2.

D_00810CB2 is the battery charge in half-units (decomp docs/FINDINGS.md
"BATTERY"): a save takes 4 half-units, 2 on the status screen, as its
confirmation's Yes is taken (or after a format), and gives them back when
the write fails; over an unusable slot (presence 2, 00229680's path) they
are given back whether its write succeeds or fails. The block written
includes the reduced charge. The load never touches it except through the
block it reads. No routine here tests the charge before taking it (what
the save terminal checks before it opens the screen is not translated).

`make test-card-screen-reference` (`tools/test_card_screen_reference.py`)
runs the original routines over the opt_07 and dmg_05 recordings (both on
the card screen's load row) against the translations, with the save, load
and options lanes' translations as the callees they are and every other
callee as original code (RAM, the scratchpad and the arguments equal at
every callee entry and at the end). The libmc SDK is test_load_reference's
card model plus GetInfo's free and format words and Format (the card
emptied); its answers are inputs, not claims about the IOP's server.
Before each frame of a whole screen the pad words and the text's packet
pointer D_00811CD4 are poked on both sides (the main loop resets that
pointer every frame; outside the lane). Cases: every routine on its states
with the pad words, the poll results and types, the Sync gate and the
error flag (1,012 single calls, each on both recordings); 76 whole card
screens frame by frame from 00227300's first call, each ending with the
result it is built for, with a pad script made on the original alone from
the screen's state (a press every other frame): LOAD GAME of every slot
(D_00810700 checked equal to the slot file), saves to a new card and over
an existing directory (the written slot equal to the live block, its check
byte, D_00810CB2 4 lower, a new card's eight files), a declined
confirmation, Circle / Triangle out, no directory, an unformatted card
(Yes then the save, No, Triangle, a failing format), a card too full, a
card removed and put back, a rejected card, a failing read, write and
GetInfo, and the unusable slot's path (00229680, then the card check and
the save again). 444 of the 458 branch outcomes are taken; the 14 others
are unreachable and listed in the test (a GetInfo poll result other than
0 / -1 always comes with the error flag, which is tested first; switch
arms for results a callee never returns; 002280F0's repeated type test).
Run 2026-10-20: quick PASS (132 cases, about 20 s; the text leaves
001CC1E0 / 001FC770 and sprintf memoized on the original's side with every
passed argument in the key and replayed on the native side),
EM_TEST_FULL=1 PASS (1,088 cases, 108,150 callee entries equal, about 19 min). Mutants checked (the card check's
retry bound and GetDir count, the free count's bound, the number row's y,
the minutes' divisor, the load-mode Cross test, the Yes test, the done
message's 45 frames, the battery's 4, 002267A0's turn, 00227DE0's verdict,
001FED40's phase): all fail the quick run.

## 7. Next steps

In order: wire the card screen: em_options_live's 002267A0 / 00227300
dispatched to em_card_*, the libmc file commands and Format through
`em_memcard_call`, em_memcard answering GetInfo's free and format words
(today it faults on them: no recording shows the server's values); a fork
recording of the original saving at AREA01's save terminal on a separate
card file (the user's decisions of 2026-10-09, HANDOFF item 9: a dedicated
card, never the user's own), whose libmc results (the Sync result words,
GetInfo's free and format words, the GetDir table) check em_memcard's
answers and whose card files check section 2 against the card the IOP
wrote; what the card screen's callers do with its result 2 (0022A650's
r == 2, 001AC070's title load: the restore and the area entry) and the
save terminal's Yes path (0020CDC0, request 6); the start-point save
builder (the fork recordings route_a01 / route_a00 / route_a01r at each
area arrival, kept in ignored build/); `EM_START_AT` (docs/STARTUP.md
"Developer switches").

## 8. Re-exports

None: the save code is in the boot ELF, and the test reads the recordings
in the reference `tools/reference_root.py` selects.
