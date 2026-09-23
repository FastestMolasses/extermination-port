# Message service (WP-8)

`src/game/em_message_service.{h,c}` is one native message machine over the
original `D_002821B0` request block. It is meant to run once per frame at
main-loop step F, where the original calls `001FCA10`. It is built and
tested, but nothing in the live game calls it yet (see "Binding").

## What it translates

| Original | Native | Notes |
|---|---|---|
| 001FCA10 | `em_message_tick` | phase 1: modes 0/1 idle, 2 text, 3 presenter, 4 help/record, 16 reset. Phase 2: teardown. |
| 001FDB80(0) | `text_tick` | Sub-state +0x5C: 0 = fetch, 1 = present. Any other value returns 1, so the phase becomes 2. Handles the D_008106F5 modes 0/1/2. Gated on D_00282155/156. |
| 001FDB80(1) | `teardown` | Clears the clock fields, releases the flag mailbox, then runs 001FAB80. |
| 001FD790 | `fetch` | Record = `D_00264DD0[0]` (bit 31) or `[area+1]`. Skips 0/0 records. Loads +0x6C/+0x50/+0x51. |
| 001FD950 | `present` | Draws on every present tick, including the tick where the timer reaches zero. Handles the flag mailbox (`D_008106D4`) and the slot-0 face talk (game mode 2 only). |
| 001FD580 | `voice_first` | A stream-table row claims the line (D_008106F4 = 0, return 2). Otherwise the service pushes the first voiced record. |
| 001FD6A0 | `voice_next` | Stops if the current record's +5 is exactly 1. Otherwise it scans from index+1. |
| 001FA5A0 | `em_message_voice_ring_push` | Helper for the binder that owns the voice ring. |
| 001FAB80 | in teardown | Calls `stop_lane(1)` and `stop_lane(2)`, then sets D_008106F5 = 0. |
| 001FC9B0 | `em_message_reset` | memset 0x9C. Sets D_00275C50 = D_0026EC10[0], D_00275C54 = 0x80, +0x20 cursor, D_00275C55 = 0. |
| 001FD4C0 | `em_message_stream_request` | 001FD470(-1), D_008106F4 = 2, 001FA790(0, cue). |
| 001B7D60 | `em_message_op0c` | op0C subs 0 to 6 (sub 0/4 poll phase 2; sub 1 has the +0x74 bit-15 handshake). |

The block layout is a C struct with static offset asserts. The block bytes can
be compared directly with RAM. The ranges that only the mode-3 presenter
`001FD0E0` uses are carried and cleared, but never interpreted.

## Verified

`python3 tools/test_message_service_reference.py` runs the original
instructions in the project MIPS oracle over captured AREA11 RAM, using the
user's local captures. It compares the following on every tick:

- the 0x9C block
- the FC9B0 text defaults
- D_008106F4/F5 and the 12-byte mailbox
- the ordered worker calls (draw, face talk, voice push, lane stop, stream
  stop/play, mode-3/4 presenters)

Table sizes. The native service faults on a record at or past the end of
a table, so the oracle sizes each table from the original's own pointers.
`D_00264DD0` is 24 words: the next object, `D_00264E30`, is a separate
variable (001FE8D0 writes `D_00264E30/34/38` on their own). Word 0 is the
global bank and word `area + 1` is the area bank. A table ends where the next
table the original points at begins. The candidates are every non-zero
`D_00264DD0` word, plus the 001FEE60 tables `D_00275848[2]` and
`D_00264E40[0x17]`. The global bank is followed by the `D_00275848` path
strings. The result is area 11 = `[12]` 0x2704D0 up to `[14]` 0x2709E0 = **162
records**, and global = 0x272DF0 up to 0x272FA0 = **54 records**. Both are
asserted. Area 11's last record, 0xA1, is (0,0,0,0).

Last run:

- **10,042 message ticks**. All cases use real area-11/global records below
  the table end, except the two marked synthetic. They include:
  - Director op0C lines 0x97 and 0x99 × delays 0/1/30 × D_008106F5 modes
    0/1/2. VOICE.DAT cues 150/149 are pushed. Records 0x97/0x99 are drawn 119
    and 199 times per case (duration + 1).
  - Global line 0x80000018 across the same matrix.
  - Voice chain 0x7F (143, 144, 145, ...), which takes the 001FD6A0 hit
    path.
  - 0x13 (166 frames, cue 1, slot 1). It pushes one voice. 001FD6A0 then
    scans 0x14 to 0x17 (voice -1) and misses on the 0x18 terminal. There is
    no face talk (slot 1), and the record is drawn 167 times.
  - Slot-1 line 0x9B in game mode 1, and 0x97 in game mode 1 (no face talk).
  - 0x66, where the stream table claims the line (001FD580 returns 2).
  - 0x98, a terminal-only line (001FD580 miss).
  - Busy windows on D_00282155 and D_00282156.
  - One case on a **synthetic** record with +5 = 2. It is marked as synthetic,
    because the game's area tables only use 0/1. It pins the exact `== 1` test
    in 001FD6A0.
  - One case on a **synthetic** voiced zero-duration record (0x98 patched to
    (0, 151, none, 0)). Area 11 has no such record inside a message. 001FD6A0
    pushes 151 after 150, but 001FD790 skips the record and never draws it.
- **Table-end fault, real data**: line 0xA1 with D_008106F5 = 0/1/2. 001FD790
  skips the (0,0,0,0) record and walks to 0xA2, which is the first record of
  `D_00264DD0[14]`. Every tick agrees with the original up to that walk. On
  the tick where the original reads past the table (ticks 5/1/4), the native
  service latches `message record outside table` with +0x34 = 0xA2. After
  that, ticks and op0C return -1. With F5 = 0, 001FD580 first pushes the
  padding record's voice word, 0.
- Voice cues pushed across all cases: 0, 1, 143 to 151.
- **Captured resumes** until teardown:
  - opening mid-message: 1,013 ticks
  - Roger encounter mid-message: 1,330 ticks
  - panel mode 4: 30 ticks (it never completes inside the service)
- **48** mode-dispatch ticks: modes 0/1/3/4 (group 5 and 0x64)/9/16, phases
  0/2/3, and sub-state 2. Each runs with game modes 1 and 2.
- **864** op0C cases: sub 0–7 × handshake 0/1/2 × phase × mode × +0x74 ×
  flag. The oracle's new `movz`/`movn` are only reached here, and are
  cross-checked against the byte-matched 001B7D60 C.
- **12** 001FD4C0 cases and **24** 001FA5A0 ring cases.
- **420** ticks agree field for field with the already-verified
  `em_opening_dialogue_*` clock (+0x60/+0x6C/+0x5C) on global messages
  0x18/0x1A/0x0A. Routing the panel, opening and Roger paths into this
  service therefore keeps their verified timing.
- Mutation check (previous round): 11 of 12 targeted mistranslations fail the
  oracle. This round, removing the `index >= count` bound from `record_at`
  also fails it, in the table-end case.
  They include the NEARMISS C's extra `flags` test before 001FD580, `!= 0`
  in place of `== 1` in 001FD6A0, the face-talk game-mode gate, the
  sub-state-2 return, and the 156 busy gate. The one survivor is
  equivalent: the teardown clear of +0x74 is overwritten by the FC9B0
  memset right after it, so it cannot be observed.

`tests/message_service_test.c` uses synthetic tables. It checks that missing
or failing workers, a missing area table, a record outside a table, a slot of
12 or more, missing shared state and a NULL op0C record all latch a fault.
Ticks and `em_message_op0c` return -1 after a fault and leave the block alone.
`em_message_init` refuses a nonzero area, stream or global count that has no
table. The checks use an always-compiled `CHECK` macro, so the test still
works under `-DNDEBUG`.

## Boundaries (not simulated)

- Glyph layout and drawing. `001FE480/001FE530/001CC170` and `001FE070` are
  the `draw_line(global, index)` worker. The oracle asserts x = 0x100 −
  max(w0, w1)/2 and y = 0xC2. The renderer does the centring
  (`em_hud_subtitle`).
- The voice lane `001F9CF0`. It consumes the `D_00281CF0` ring and writes
  D_008106F5 (2 → 1 when a voice starts). In the oracle the harness makes this
  write after 3 ticks, on both sides. The busy bytes D_00282155/156 are also
  harness inputs.
- Who sets bit 15 of `+0x74` for op0C sub 1 is not established.
- The mode-3 presenter `001FD0E0` and the mode-4 presenters
  `001FCB90/001FCF90/001FCF60` are workers.
- The service faults instead of reading past a table, or reading a zero table
  pointer. The original would read whatever memory is there; for area 11 that
  is the next area's table.
- `em_message_op0c` returning -1 after a fault is a native contract. The
  original 001B7D60 only returns 0/1.

## Binding (later, by the coordinator chain)

- Tick `em_message_tick(&service, &shared)` at step F in `em_frame.c`. Pass an
  `EmMessageShared` holding:
  - area D_00810700 and game mode 0x70003B8F
  - the busy bytes
  - pointers to the shared D_008106F5, D_008106F4 and D_008106D4[12]
- Data (`EmMessageData`) must be exported from the user's own ELF by a new
  exporter:
  - the `D_00264DD0` table pointers and records, sized as described under
    "Table sizes" (never a fixed count)
  - the `D_0026EC60` rows up to the −1 terminator
  - `D_0026EC10[0]`
  - the address token for `D_00264D10`

  `tools/export_opening_media.py` already reads these locations.
- Requests to route into the service:
  - opening op0C sub 1 line 0x66 (`em_opening_runtime` case 12)
  - panel program op0C sub 0 (`em_panel_program` case 12)
  - director beats op0C sub 0 lines 0x97/0x99 (WP-10)
  - Roger message command
  - examine and door refusals. Their original writers store the block fields
    directly (for the door: mode 2, phase 1, line, delay; see `em_door.h`).
    Check each writer's exact stores before routing it.

  All of these go through `em_message_op0c` or direct block writes.
- Workers:
  - `draw_line` → `em_hud_subtitle` with the record's fill, outline and skew
  - `face_talk` → the player face host
  - `voice_push` → the voice lane's ring (`em_message_voice_ring_push`)
  - `stop_lane` / `stream_stop` / `stream_play` → the BGM/stream lanes
  - `help_draw` / `record_setup` / `record_draw` → the status hub and item UI
    presenters
  - `mode3_present` → a future 001FD0E0 port
