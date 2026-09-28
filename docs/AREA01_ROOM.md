# AREA01 lane A01ROOM: the room-beat-only boot functions

Level 2 (AREA01) side track, lane A01ROOM, 2026-09-28 (session s88). New files only:
`src/game/em_area01_room.{c,h}` (prefix `em_area01_room_`) and
`tools/test_area01_room_reference.py`. The module is built and tested but not wired;
nothing in the port calls it yet (section 6).

## 1. Scope

The census field `room_beats_new_functions` of
`../Extermination/build/s87/census/a01_delta.json`: the 20 functions (18,740 bytes) that
only the AREA01 room beats run for the first time (SECOND_LEVEL_ROUTE.md section 9.6):
a01_s4_east_room (door [17] and the save terminal), a01_s5_duct (the control-room duct,
its pickup and the healing-item page) and a01_s6_bridge_blocked (the untaken Use at the
raised bridge). a01_s7_npc_third_talk adds none. Census subsystem labels are claims; the
functions are named by address.

Seven had no translation in the port and are translated here. Thirteen already have a
verified translation in another port module; they are not duplicated, and this lane re-ran
their oracles over the AREA01 room captures (section 4).

| Function | Bytes | Decomp | First (beat, census frame) | Where |
|---|---:|---|---|---|
| 0015FDF0 | 1068 | NEARMISS | a01_s6 f158 | `em_player_running_jump.c` (existing; `em_player_running_jump_aim`) |
| 0016D130 | 3332 | byte-matched | a01_s5 f136 | `em_player_closure_0e_18.c` `em_player_closure_state18` (existing) |
| 0016DE40 | 3420 | byte-matched | a01_s5 f282 | `em_player_closure_10_12_19.c` `em_player_closure1019_0016DE40` (existing) |
| 0016EBA0 | 940 | NEARMISS | a01_s5 f3021 | `em_player_closure_10_12_19.c` `em_player_closure1019_0016EBA0` (existing) |
| 00179010 | 152 | byte-matched | a01_s5 f282 | `em_player_closure_10_12_19.c` (existing, static `e00179010`) |
| 001790B0 | 156 | asm (words) | a01_s5 f376 | `em_player_closure_0e_18.c` `em_player_closure_001790B0` (existing) |
| 00179150 | 128 | byte-matched | a01_s5 f376 | `em_player_closure_0e_18.c` `em_player_closure_00179150` (existing) |
| 00179910 | 640 | NEARMISS | a01_s5 f376 | `em_player_closure_10_12_19.c` (existing, static `e00179910`) |
| 001823E0 | 68 | byte-matched | a01_s5 f346 | `em_player_major2.c` `em_player_major2_001823E0` (existing) |
| 00188610 | 28 | byte-matched | a01_s5 f282 | **this module** |
| 00198D90 | 372 | NEARMISS | a01_s5 f282 | **this module** |
| 001AA4E0 | 344 | byte-matched | a01_s6 f158 | `em_player_running_jump.c` (existing; `em_player_running_jump_target`) |
| 001BB400 | 284 | byte-matched | a01_s4 f250 | **this module** |
| 001BB7C0 | 40 | byte-matched | a01_s4 f246 | **this module** |
| 001BB7F0 | 108 | byte-matched | a01_s4 f334 | **this module** |
| 001D0D60 | 444 | asm (inline) | a01_s5 f904 | **this module** |
| 002082B0 | 1180 | NEARMISS | a01_s5 f1633 | `em_item_geometry.c` `em_item_geometry_arc` (existing) |
| 00208AD0 | 1960 | byte-matched | a01_s5 f1633 | `em_status_draw.c` `em_status_health_draw` (existing) |
| 002160B0 | 4056 | byte-matched | a01_s5 f1632 | `em_status_pages_item.c` `em_status_pages_002160B0` (existing) |
| 00225A00 | 20 | byte-matched | a01_s4 f679 | **this module** |

Before translating, each address was looked up in the port (definitions, not mentions).
Six of the seven were only workers or stand-ins elsewhere: 00188610 is the stub
`x_00188610` of `em_player_closure_live.c` (the `clip_88610` worker of
`em_player_closure_10_12_19.c`); 00198D90 is the worker `w_00198D90` of
`em_camera_leftovers.c`; 001BB7C0, 001BB7F0 and 001D0D60 are call-outs of
`em_area01_math_owner.c` (F_001BB7C0 / F_001BB7F0 / F_001D0D60); 00225A00 is the worker
`w_00225A00` of `em_startup_load_gaps.c`. 001BB400 is described in `em_door.c` comments and
replaced there by a frame count (section 7); no module translates it.

**IOP / CD.** None of the seven makes an IOP or CD call.

## 2. Model

The lane SIDE / SYS model (AREA01_SIDE.md section 2, AREA01_SYS.md section 2), unchanged:

- **Memory.** Caller-supplied regions keyed by original address (RAM and the scratchpad).
  None of the seven hands a stack local's address to a callee, so no stack region is
  needed; every value is a C local.
- **Calls.** Every call that leaves the module goes through the single worker `call` with
  the callee's address, the argument registers the original sets (64-bit images; `na` /
  `nf`) and the stack pointer at the call. It returns v0 (a 64-bit image) and f0.
  00225A00 enters its callee by a jump, so the callee gets the entry stack pointer.
- **Register images.** A 32-bit argument is the sign-extended register; an address formed
  from a register plus an offset is the sign-extended 32-bit sum. 001BB7C0 tests
  001BA1F0's whole v0 register, so the worker's 64-bit image is used as is.
- **Arithmetic.** Every float operation goes through `em_ee_float.h` (docs/EE_FLOAT_MODEL.md):
  00198D90's add, 001BB400's add, subtracts and compares, and all of 001D0D60 (adds,
  subtracts, compares, the int-to-float conversions and the MULA / MADD pairs).
- **Store trace (test builds only).** `EM_AREA01_ROOM_STORE_TRACE` reports every store
  (`wr` is the only store path) so the test can compare memory at every call.
- **Fail-stop.** An unmapped address, a NULL worker, a negative worker result or a NULL
  output latches the first fault (code, entry, address); every entry then refuses until
  `em_area01_room_clear_fault`. Stores made before the fault stay.

## 3. What each routine does

- **00188610(a0).** The halfword D_002754D8[bit 0 of the byte a0+0x235], sign-extended.
  No store, no call.
- **00198D90(a0, a1)** (0x30-byte frame; 0018BC20 hands it the camera block 0x008101E0
  and the player, CAMERA_LEFTOVERS.md action 10). In order: 00102948(a0+0x10, a1+0xA0);
  the float a0+0x14 = its value + 3.0 (the value is the player's +0xA4 the copy just
  wrote, so what reaches the EE add is a1+0xA4, never the old a0+0x14);
  001029C0(0x70003400); 00102C58(0x70003400,
  0x70003400, a1+0xC0); the scratchpad words 0x70003600..0x7000360C = 0, 0, 5.0, 0;
  001026A0(a0+0x20, 0x70003400, 0x70003600); 001028B8(a0+0x20, a0+0x20, a0+0x10). Then
  the byte a0+1, read after that call: 0 gives a0+1 = 1, a0+2 = 0,
  00102948(D_008105E0, a0+0x20), 00102948(D_008105D0, a0+0x10); 1 gives, when the word
  a1+0x230 is 0x12, a0+6 = 0x0B then a0+1 = 0, and always 0018C4B0(D_008105E0;
  f12 = the float a0+0x24 read after the stores, f13 = 0.2), 0018C6A0(a0+0x20,
  D_008105E0; f12 = 0.2), 00102948(D_008105D0, a0+0x10); any other value does nothing
  more. The decomp's NEARMISS C says the same; the translation follows the instructions.
- **001BB400(a0)** (entry 0x51 of the script-command table at 0x24D880, which the script
  pump 001BA1F0 calls with the actor): by the kind byte a0+3. Kind 8 or 0x16: panel 0's
  float = its value - 0.2; the result is 1 when panel 0's float, read again, is below
  -9.0. Any other kind: panel 1's float = its value - 0.2, panel 2's float = its value +
  0.2, the kind is read again, and the result is 1 when panel 1's float is below -13.0
  (kinds 0x3E, 0x3D) or below -9.0 (all others). A panel is the float at +0x7C of the
  bone record (*D_00275B40)[slot]; the array pointer and the bone pointer are read again
  at every access. The constant 0.2 is the one the original loads (0x3E4CCCCD).
- **001BB7C0(a0)** (0x10-byte frame): 001BA1F0(a0); 1 when its whole v0 is non-zero,
  else 0.
- **001BB7F0(a0).** When the byte D_008106B8 is non-zero: result 0, nothing written.
  Otherwise the panel words (+0x7C) are zeroed, panel 0 for kinds 8 / 0x16, panels 1 then
  2 for any other kind (pointers read again each time), the byte a0+0x0B = 0, result 1.
- **001D0D60(a0; f12)** (0x60-byte frame). a0 is a record {+0 a table of 7-float rows,
  +4 a length L, +8 a time t, byte +0x0C a loop flag, +0x40 seven outputs}:
  t = t + f12 (stored). With the flag: while t is not below L, t = t - L (stored; t and L
  read again each round); n = float_to_int(1.0 + t); next = n when (float)n < L, else 0.
  Without it: when t is not below L, t = L - 1.0 (stored); n = float_to_int(1.0 + t);
  L is read again; next = n when (float)n < L, else float_to_int(L - 1.0). Then t is read
  again, frac = t - (float)float_to_int(t) (the table word +0 is read between that call
  and the subtraction), w = 1.0 - frac, i = float_to_int(t), and for k = 0..6 output k =
  w * row[i][k] + frac * row[next][k] (the first product into ACC, then MADD), loading
  row[i][k] then row[next][k] before output k is stored. The result is 0x1000 when the
  time wrapped or was clamped, else 0. A loop flag with L <= 0 never leaves the wrap loop
  in the original; the translation does the same (there is no bound to invent).
- **00225A00().** 00121A28(D_00810040, 0, 0xD4) by a jump (the C runtime's memset, which
  run as original code clears the 212 bytes); no result is defined.

In the captures: the two 001BB860 nodes 0x7ACBC0 and 0x7ACEB0 (AREA01_MATH.md) have kind
+3 = 0x09 in all four room captures, and each node's +0x14 word holds its own address;
D_00275B40 is 0x8102F0 (the player's array) at every end of beat. The sampler record of
node 0x7A93F0 (behaviour 001C02E0) is 0x7DCD40: table 0x24FD50, L = 91.0, loop flag 1;
t = 51.0 with non-zero outputs at the end of a01_s5, 0.0 in the other three. The census
logs entry hits, not arguments, so which records the hits came from is not recorded.

## 4. Existing translations (not duplicated) and their re-run over the room captures

| Address | Port module | Its own oracle (passes, default mode, this session) |
|---|---|---|
| 0016D130, 00179150, 001790B0 | `em_player_closure_0e_18.c` | `tools/test_player_closure_0e_18_reference.py` (16,000 cases, 470/470 branch outcomes) |
| 0016DE40, 0016EBA0, 00179010, 00179910 | `em_player_closure_10_12_19.c` | `tools/test_player_closure_10_12_19_reference.py` (16,000 cases, 719/719 outcomes, 3 impossible) |
| 001823E0 | `em_player_major2.c` | `tools/test_player_major2_reference.py`; also executed inside the 10/12/19 oracle |
| 0015FDF0, 001AA4E0 | `em_player_running_jump.c` | `tools/test_player_running_jump_reference.py` (7,706 cases) |
| 002082B0 | `em_item_geometry.c` | `tools/test_item_geometry_reference.py` (500 arcs) |
| 00208AD0 | `em_status_draw.c` | `tools/test_status_draw_reference.py` (360 health cases) |
| 002160B0 | `em_status_pages_item.c` | `tools/test_status_pages_reference.py` |

Those oracles ran over first-level images (the AREA11 `playable_ee.bin`, the status-hub
capture, the s87 route) or over synthetic memory. Section B of this lane's test runs them
again with AREA01 room captures in their place (section 5).

## 5. How it was verified

`tools/test_area01_room_reference.py` (docs: its header).

### A. The seven translations

**Oracle.** FallEE (the shared EE interpreter with the measured COP1 / VU0 model) runs the
original code of each routine over the end-of-beat images
`../Extermination/build/s87/route_a01/<beat>/{eeMemory,scratchpad}.bin` of a01_s4, a01_s5,
a01_s6 and a01_s7; the native module runs over a copy of the same bytes. The captured code
of the seven routines is checked equal to the pinned ELF first. The harness is lane SIDE's,
with PCPYH added to the interpreter for the memset 00121A28 (as lane UI):

- **Callees.** Every direct call and jump target of the seven routines has a policy
  (asserted from the ELF). `run`: 00102948, 001029C0, 00102C58, 001026A0, 001028B8,
  float_to_int 001281C0 and 00121A28 execute as original code, in the oracle and, on the
  native side, in a second interpreter bound to the module's memory. `stub`: 0018C4B0,
  0018C6A0 and 001BA1F0 are recorded with a scripted v0 (a whole 64-bit image for
  001BA1F0).
- **Lockstep.** At every call: the callee, the stack pointer, the integer argument
  registers the original sets (64-bit images; the native's `na` / `nf` checked against
  them), the float argument registers, and every RAM / scratchpad line (64 bytes) either
  side stored to since the previous call. After the last store the same check, then the
  result (low word), the call count, all 32 MiB of RAM and the scratchpad. Both stacks
  start filled with 0xA5 and the interpreter's registers with a pattern.
- **Memory model.** The shared interpreter mirrors every address below 0x40000000 onto
  the 32 MiB of RAM; the module maps RAM at 0..0x1FFFFFF only and faults UNMAPPED
  elsewhere. The test's interpreter (`RoomEE._where`) turns that mirror off, so an access
  in 0x02000000..0x3FFFFFFF stops the original exactly where it stops the module. This
  is what lets a pointer rewritten by a float store (a RAM pointer + 0.2 is 0x3E4CCCCD)
  be compared (the 001BB400 re-read cases below). Neither side maps the hardware's
  0x20000000 / 0x30000000 views of RAM; no captured pointer uses them.
- **fx cases** (00198D90, 001D0D60): the SIDE design applied at every call, stubbed or
  not. 'after': pass 1 records the data loads the routine makes after each call; pass 2
  flips exactly those fields when that call returns, on both sides. 'before': the fields
  loaded since the previous call are flipped when the call returns (the n-th change of a
  field flips bits no earlier change flipped).
- **Crash cases** (001BB400, six): a panel store that rewrites the pointer array's next
  word, or D_00275B40 itself, makes the original's next access go through a float bit
  pattern that is no address. Both sides must stop there: the oracle at an unmapped
  access, the module with FAULT_UNMAPPED at the same address, with the same bytes written
  before it. A translation that cached the array or the pointer would not stop. Three
  cases rewrite with panel 0's or panel 1's store (the result is a negative float, above
  0x80000000). Three rewrite with panel 2's store, array word 1 (kinds 9 and 0x3D) or
  D_00275B40 (kind 9). The original then reads the array pointer and panel 1's pointer
  again for its final compare and stops at 0x3E4CCD49 or 0x3E4CCCD1. This pins the
  re-read of panel 1 after panel 2's store. The reviewer's load trace also showed it.
- **Branches.** Both outcomes of all 19 conditional branches of the seven routines are
  taken; asserted in every run.
- **api_checks.** NULL context; NULL worker (00225A00 names 00121A28); the latch and
  `clear_fault`; a failing worker (001D0D60 names float_to_int, and the time it stored
  before the call stays); unmapped addresses (00188610's byte, 001BB400's and 001BB7F0's
  bone pointers, with the address and the entry); NULL output for the five routines with a result; a failing worker
  names the entry and the callee for 00198D90, 001BB7C0 and 00225A00; the first fault is
  kept.

**Cases** (all compared the same way):

| Routine | Cases (full) | What they cover |
|---|---:|---|
| 00188610 | 273 | the player record in all four captures; every +0x235 value 0..255 with the table as captured or 0x8000 / 0x7FFF, 0xFFFF / 0x0001, 0x1234 / 0xFEDC; a0 = both door nodes and a scratch record; the neighbours of the table and of the byte non-zero |
| 00198D90 | 110 | the camera block and the player in all four captures with +1 as captured, 0, 1; +1 = 0, 1, 2, 0xFF x +0x230 = 0x12, 0x11, 0, 0x112, 0x12000000 x the player's +0xA4 (the float the +3.0 add sees; the first call copies a1+0xA0..+0xAF over a0+0x10..+0x1F) = captured, +-0, -3.0, +-max, denormal, 1e7, the NaN pattern, and 0x3F800003 / 0x3F800007 / 0x40A00003 (sums that truncate and round to nearest differently); with +0xA4 as captured, the bytes +3..+5 and +7..+9 around the three byte stores are non-zero; a scratch record with a door node as the actor. Round 1 wrote these values at a0+0x14 instead, where the copy overwrote them, so the add then saw only the captured +0xA4 (0.0, or 0.01 in a01_s5) and the fx flips |
| 001BB400 | 233 | both nodes in all four captures (D_00275B40 = node + 0x110); kinds captured 9, 8, 0x16, 0x3D, 0x3E, 0x17, 0, 0xFF, 0x3C, 0x3F, 0x15 x 18 panel values (the thresholds one ulp either side, -9.0 / -13.0 exactly, +-0, +-max, NaN and all-ones patterns); six crash cases (above); the kind re-read after panel 2's store for 0x3D / 0x3E / 0x3C |
| 001BB7C0 | 44 | both nodes in all four captures; v0 = 0, 1, 2, 3, -1, 0x80000000, 0x1_00000000, 0xFFFFFFFF_00000000, 0x7FFFFFFF x four records |
| 001BB7F0 | 64 | both nodes in all four captures; every kind above x D_008106B8 = 0, 1, 0x80, 0xFF with the neighbour bytes of +0x0B non-zero; D_008106B8 = 0 with D_008106B9..BB non-zero; the array aliased (panel 1's word is the array's word 2, so panel 2 is zeroed through the new pointer 0) for kinds 8 and 9. The array is the node itself for kinds 9 and 0x3D: the byte +0x0B is the high byte of panel 2's pointer 0x70000200, so the byte store must come after the panel stores (the original zeroes 0x7000027C and leaves RAM 0x27C). D_008106B8 set with a0 unmapped (0x50000000, 0x03000000): the original returns 0 without reading the kind byte |
| 001D0D60 | 473 | the sampler in all four captures with steps 1.0 and 0.5; the loop flag 0 and 1 with its neighbours +0x0D..+0x0F non-zero; flag 1 / 0 / 0x80 x t over the captured length 91 (0, fractions, 89.999, 90, 91, 91.25, 181.9, 182, 300.5, negatives, -0, 1e-30) x steps (0, +-0.5, 1, 2, 91); a scratch table (rows -2..12) with L = 0.75..10.25 and t across and below the range; EE edge patterns for t and L; outputs overlapping the rows and the record; Inf / NaN / MAX patterns in rows 1 and 2 at t = 1.5 (w = frac = 0.5): the MULA product saturates, the MADD product does not |
| 00225A00 | 5 | all four captures; the bytes around the 212 pre-filled with 0xFF |

Full mode: 1,202 cases and 1,166 fx cases, 6,714 compared calls, 9,441 fx field
changes. The default run keeps every captured case, every edge and crash case, the
pinned cases (00198D90: the three rounding sums and the two +0x230 word cases with st 1;
001BB400: -9.0 / -13.0 exactly, panel 1 ending below -13.0 for kind 0x3D, the NaN
pattern for kind 8 and the captured kind; 001BB7C0: the v0 images 0x1_00000000 and
0xFFFFFFFF_00000000 for all four records; 001BB7F0: kind 0x16 with flag 0) and a
fixed-seed sample: 180 cases and 60 fx cases in all.

### B. The existing translations over the room captures

EM_AREA01_ROOM_EXISTING_ROOT picks the tree the modules come from (default: this
repository); their libraries are rebuilt only when a source or header is newer.

- **Closure states** (0016D130 with 00179150 / 001790B0; 0016DE40 / 0016EBA0 with 00179010 /
  00179910 and 001823E0 inside). Each module's own `run_case` (its interpreter, scripted
  callees, native build and comparisons: every worker call with its arguments, all 0x320
  record bytes, the scene and scratchpad words at every call and after) with its captured
  image replaced by a room capture and its player record by that capture's (the executed
  code and tables are first checked equal to the ELF in that capture). Default: 600 seeds
  each over a01_s5_duct; full: 4,000 each over all four. What the room capture contributes:
  the captured player record (a quarter of the cases start from it), the node words behind
  D_00275B40 and every RAM byte the state routines read directly. Every callee stays
  scripted, as in those tests.
- **001823E0 and the other +4 = 2 states** (full mode only): `test_player_major2_reference`'s
  `main` over each room capture (its fixed scenarios, 30,000 random cases, path assertions).
- **001AA4E0 / 0015FDF0 over the AREA01 world.** The original routine against the native
  one hooked at its address with the running-jump test's world workers (`WorldJump`: every
  worker runs the ORIGINAL callee in the same interpreter), over each room capture; v0,
  all 32 MiB of RAM and the scratchpad must be identical. The captured target lists
  D_00275B8C: a01_s4 none, a01_s5 two entries of type 0x12, a01_s6 four of type 0 (at
  y 60.4), a01_s7 one with flags 0xAA; none qualifies (flags & 0x1F must be 2 and the type
  one of 1, 2, 4, 5, 6, 7, 8, 0xC), which is what a01_s6's untaken Use met. Cases: the
  captured player, and the player's feet placed at each entry (0..30 units away, four
  headings, feet 0 / -5 / -9.5 / +12 / -40 relative to the entry's +B4) with the entries
  as captured and made to qualify (flags 2, type 1 / 4 / 0xC); 0x70003B8D set; the count
  0. Full: 1,906 cases, 702 with a target found; default: 60 (every captured player and
  edge case plus a fixed-seed sample; 14 with a target). These are per-case comparisons
  of the final state, not a replay, and memory is not compared at each callee entry.
- **00208AD0 and 002082B0.** `test_status_draw_reference.expected` (the original 00208AD0
  over the capture's RAM, every mode / text / arc call recorded) against
  `em_status_health_draw` given the data read from the same capture (the arc records at
  0x265390.., the text styles, the label strings and its width from the original
  001CC170); the captured counter UI+0x20, health D_00810858 and warning 0x8104E4 plus
  the counter wrap points, the health thresholds and every warning value. Each
  capture's four arc records go through the original 002082B0
  (`test_item_geometry_reference.original_arc`) against `em_item_geometry_arc`. Full:
  452 health cases and 64 arcs; default: 24 and 16.
- **002160B0.** `test_status_pages_reference`'s own runner (callees as original code, all
  of RAM, the scratchpad and the arguments at every callee entry) with its 318 HEALING
  cases moved from the status-hub capture onto the room captures. On a01_s5_duct 312
  compare and 6 are not applicable: the original cannot complete them on that image (a
  pointer the case relies on is unset when the hub is not open); they are counted, not
  compared. Full: all 318 on a01_s5 and 40 on each other capture; default: 8 on a01_s5.

**Tried and dropped: a route replay of the room beats.** The player-stage replay the
first-level tests use (RouteReplay; running-jump world mode) was run over a01_s5 from its
recorded start (the a01_s0 end image) and over a01_s6 (from the 15_level_exit end image).
Both leave the trace within a few frames while the player still stands idle (a01_s5 at
counter 18001: clip 1 four frames before the trace's 18005; a01_s6 at 16569), before any
routine of this lane runs. The stage-only replay does not run the rest of the frame, and
in these AREA01 beats that difference shows in the idle clip's timing; the replays are not
used as evidence here.

### Timings

macOS arm64 (M1), four worker processes at most, while other lanes shared the machine
(load average 6 to 15 in the fix round). `EM_AREA01_ROOM_TIMES=1` prints each part's wall
and CPU time.

| Run | Result | Wall / user / sys (s) |
|---|---|---:|
| default (`python3 tools/test_area01_room_reference.py`) | PASS: A 180 of 1,202 cases and 60 of 1,166 fx cases (636 compared calls, 526 fx field changes, 19 branches both ways); B 600 + 600 closure cases over a01_s5, 60 of 1,906 jump cases (14 with a target), 24 health and 16 arc cases, 8 HEALING cases | 3.7 / 8.3 / 1.5 |
| `EM_TEST_FULL=1` | PASS: A 1,202 cases and 1,166 fx cases (6,714 compared calls, 9,441 fx field changes); B 16,000 + 16,000 closure cases over the four captures, major2's main over the four, 1,906 jump cases (702 with a target), 452 health and 64 arc cases, 438 of 1,272 HEALING cases (430 compared, 8 not applicable) | 192.9 / 538.4 / 11.2 |

The default run's CPU was 15 to 17 s user in round 1. The fix round trimmed section B's
default sample (closures 1,200 to 600 seeds each, jump 140 to 60 cases, HEALING 24 to 8)
and section A's sample (220 to 180, with the pins above). The split is now about 4.5 s
CPU for A and 6 s for B.

The existing modules' libraries are rebuilt only when a source or header is newer
(the first run after an edit takes a few seconds more). The existing modules' own tests
were also run unchanged this session (default mode, all PASS): closure 0E/18 4 s,
closure 10/12/19 5 s, running jump 4 s, major2 3 s, item geometry 7 s, status draw 3 s,
status pages 7 s, status hub UI 11 s (wall).

### The mutation sweep (bounded, one round plus the gap re-run)

A scratch generator (`build/area01/room/mutate.py` with the in-process runner
`mutate2.py`, not committed) made 594 single-operation mutants of `em_area01_room.c` from
the `reg` helper to the end of the file (the call helpers, the seven translations and the
public entries): every integer constant +-1, `==`/`!=`, `<`/`<=`, `&&`/`||`, `? 1 : 0`
inverted, a removed `!`, the EE add / subtract, compare (lt/le), MADD / MSUB and MULA /
ADDA swapped, load and store widths, zero- for sign-extension of each register image,
`return 0`/`return 1` swapped, every statement deleted and every statement swapped with
the next. Each ran against the default case set of section A: the cases of the routine
whose body holds the mutated line (every case, the branch assertion and api_checks for a
line in the shared helpers or the public entries), stopping at the first difference, with
a 240 s limit (a mutant still running then is stopped).

First round: 63 do not compile, 466 are killed (8 of them by the time limit: wrap loops
of 001D0D60 that never end where the original returns), 62 survive, and 3 reach the time
limit with undefined C (reads past the argument array in `call`, `i <= na` and
`i < na || i < 8`; a NULL argument array read in `call_f2i` with `na` 1). Six survivors
showed gaps, closed by new cases now in every run: a word store at the camera block's +1
and at +6 (the neighbours of the three byte stores of 00198D90 are now non-zero), a word
read of D_008106B8 (`1BB7F0 edge flag neighbours`), a word read of 001D0D60's loop flag
(`1D0D60 edge flag neighbours`), and the entry address in 001BB7F0's fault record (an
api check of an unmapped 001BB7F0 access). All six are killed by the final default run.

The other 56 survivors, each shown equivalent or undefined:

- **Worker contract (11):** the call record's `memset` removed or filling 1; register
  images beyond `na` given 1 (00102948, 001029C0, 001BA1F0 calls); 0018C6A0 handed a
  second float register (it takes one; the value there is the 0.2 still in the record).
  The worker reads only the first `na` / `nf` registers.
- **Bounds and sizes above every use (9):** the copy-loop limits 8 / 4 moved or made
  inclusive, the local arrays enlarged; `na` is at most 3 and `nf` at most 2 in every call.
- **Zero- for sign-extension of addresses below 0x80000000 (6):** the three `calli`
  images and the three 00198D90 addresses for 0018C4B0 / 0018C6A0. Every address the
  module maps is below 0x80000000.
- **Equivalent read width (1):** 00188610 reading +0x235 as a word keeps only bit 0, the
  bit 0 of the first (little-endian) byte.
- **Independent statements swapped (23):** assignments to different fields of the call
  record or of `a[]` / `f[]`; `sp` adjusted before or after a store; the result code set
  before or after a store; loads at distinct fixed addresses with no call between
  (001D0D60's +8 / +4, the table word and the fraction, the two row bases); byte stores to
  distinct fields of 00198D90's record; the scratchpad words 0x70003604 / 608 / 60C; the
  fault fields of `out_null`; `w` computed after a call it does not feed.
- **Comment lines swapped (5):** lines of 00198D90's comment ending in `;` (no code change).
- **Undefined C (1):** `f[1]` read uninitialised at the 0018C4B0 call (its store moved
  after the call).

**Round-1 re-run.** The whole set of 594 was run again against the round-1 case set (the
default sample was then 220 cases): 63 do not compile, 472 are killed (8 of them by the
time limit, the 001D0D60 wrap-loop hangs), 3 reach the time limit with undefined C (the
three above), and 56 survive, exactly the 56 classified here. No mutant killed in the
first round survived that run. The 594 were not run again after the fix round below (no
new sweep); the fix round changed the case set, as described there.

**Fix round (review).** The reviewer's 43 hand-targeted mutants were run against the
round-1 default set. 35 were killed and 8 survived. Three of the eight were not
equivalent:

- `198D90_ieee` (the +3.0 as an IEEE add). Section A's +0x14 values never reached the add
  (section 5 table). The edge values now sit at the player's +0xA4. The pinned case
  `198D90 t st 1 mode 12 y 3f800007` kills it: the original stores 0x40800001 at 0x8101F4,
  the mutant 0x40800002.
- `1BB7F0_bfirst` (the byte +0x0B zeroed before the panels). The case
  `1BB7F0 edge byte order kind 9` kills it: the mutant zeroes RAM 0x27C.
- `1BB400_cachep1` (panel 1's address kept from before panel 2's store). The reviewer could
  not turn it into a case because the interpreter mirrored 0x3E4CCD49. With the mirror
  off (memory model above), `1BB400 edge crash array word 1 (panel 2 store)` kills it: the
  original stops at 0x3E4CCD49, the mutant does not stop.

Three survivors the review had called equivalent were in fact killable, and are now
killed:

- `1BB7F0_kindfirst` (the kind byte read before the D_008106B8 test). It is not
  equivalent: with the flag set and a0 unmapped the original returns 0, the mutant faults.
  `1BB7F0 edge flag set a0 3000000` kills it.
- `1D0D60_madd_split` (MADD as MUL then ADD) and `1D0D60_madd_swap` (the two products
  exchanged between MULA and MADD). They differ where a row word has the exponent field
  0xFF. MUL / MULA saturate an Inf product to +-MAX, but the MADD product stays +-Inf, and
  an Inf addend makes the sum the Inf's sign with MAX. `1D0D60 edge inf rows ff800000
  7f800000` kills both (the original gives 0x7F7FFFFF at +0x40, split gives 0, swap gives
  0xFF7FFFFF).

Two remain, and each is equivalent:

- `198D90_commute` (`em_ee_add_bits(F_THREE, x)` for `(x, F_THREE)`). The EE add is
  symmetric in em_ee_float.h:
  - DAZ is applied to both operands.
  - The Inf / NaN branch depends only on the operand set.
  - The pre-trim always trims the operand with the smaller exponent field.
  - `em_eei_exact_sum` orders by exponent, and at equal exponents it picks the sign by
    magnitude.
- `198D90_f24early` (an extra read of a0+0x24 right after the 001028B8 call). The value
  goes into `f[0]`, which is assigned again from a0+0x24 before its only use, and nothing
  else reads it. The extra read could only differ by faulting, and it cannot fault:
  001028B8 has just stored a0+0x20..+0x2F through the same memory.

Final state, against the final default set: all 43 were run again, 41 are killed and the
two equivalent ones above survive. Trimming the default sample (220 to 180 cases, for the
CPU budget) at first let four killed mutants through: `198D90_byte230`, `1BB400_le13`,
`1BB7C0_trunc` and `1BB400_ieee`. Their killing cases are now pinned (the pinned list
above), and all four are killed.

### Reproduce

```sh
# port repo root; the decomp checkout as its sibling with the room captures
python3 tools/test_area01_room_reference.py                    # default, about 4 s wall / 10 s CPU
EM_TEST_FULL=1 python3 tools/test_area01_room_reference.py     # about 3 minutes
EM_AREA01_ROOM_EXISTING=0 python3 tools/test_area01_room_reference.py   # section A only
```

## 6. Binding

None of the seven is bound. What binding each needs (named here, not done):

- **00188610** replaces the stub `x_00188610` of `em_player_closure_live.c` (the
  `clip_88610` worker of `em_player_closure_10_12_19.c`, called by 0016DE40): the byte
  +0x235 of the live record and the two halfwords D_002754D8 (boot data, from the ELF).
  0016EF50 and 002236F0 also call it.
- **00198D90** binds `w_00198D90` of `em_camera_leftovers.c` (0018BC20 action 10). Its
  callees: the SDK leaves (`em_sdk_math_original.c` and the port's copies), 0018C4B0 /
  0018C6A0 (the camera approach routines; bind to whatever translation the camera lane
  uses), and the scratchpad 0x70003400..0x7000360F.
- **001BB400** is the script command 0x51 of the table 0x24D880; binding means the
  script host's command table (AREA_SCRIPT.md) calling it with the actor, and replacing the
  frame-count stand-in of `em_door.c` (section 7).
- **001BB7C0 / 001BB7F0 / 001D0D60** bind the call-outs F_001BB7C0 / F_001BB7F0 /
  F_001D0D60 of `em_area01_math_owner.c` (001BB860 sub-states 1, 2, 4; 001C02E0 state 1).
  001D0D60 also serves 001BECC0, 001BF6B0 and 001C1A80. 001BB7C0's callee 001BA1F0 is the
  script pump (the port's `em_script.c` sequencer, AREA_SCRIPT.md).
- **00225A00** binds `w_00225A00` of `em_startup_load_gaps.c` and request 6 of
  `em_status_page.c`; its callee is the C runtime memset over D_00810040..D_00810113.

The existing translations of section 4 are bound (or not) as their own docs say; nothing
here changes them.

## 7. Notes for the lead

- `em_door.c` says model byte 0x09 "appears in NO recovered door function" and keeps it as
  an exporter convention. In all four room captures the two 001BB860 nodes (one of them
  door [17], SECOND_LEVEL_ROUTE.md section 9.1) have +3 = 0x09, which takes 001BB400's
  two-panel branch with the -9.0 limit. Executed from panels at 0.0, 001BB400 reports done
  on its 46th call for kinds 9 and 8 (panel -9.199992) and on its 66th for 0x3D (panel
  -13.199988): em_door.c's 46 / 66 frame counts agree; its single-leaf / wide sets agree
  with the instructions. Its comment that 45 steps land exactly on -9.0 is inexact: the
  original's panel is -8.9999924 after 45 calls (the EE subtract truncates) and
  -9.199992 (0xC113332B) after the 46th, which is the call that reports done. Only that
  comment's reasoning is off, not the count (the reviewer confirmed both by running the
  original).
- 001D0D60 is `asm` in the decomp (hybrid words); section 3 is the readable account, read
  from the instructions and confirmed by the oracle.

## 8. Known gaps

- **No real-frame comparison.** The census logs entry hits, not arguments, and the
  captures are end-of-beat images, so every case of section A (and of B) is a designed
  input over a captured image, not a recorded call. The stage-only route replay that
  would give real frames leaves the AREA01 traces within a few frames (section 5 B); a
  replay of the room beats needs the whole frame (the scene coordinator's), not the
  player stage alone.
- **The closure re-run (B) keeps every callee scripted**, as the closure tests do: the
  crawl's probes and collision answers over the AREA01 duct geometry are not exercised by
  it. The room capture enters through the record, the node words and the bytes the state
  routines read directly.
- **001BB7C0** hands 001BA1F0 whatever a1..a3 its caller left; the module passes a0 only
  (001BA1F0's decomp C and the script-pump translation take one argument). Register reads
  of stubbed callees are not measured (lane SYS's RegScan was not ported), as in lane SIDE.
- **00225A00** leaves the memset's v0 in v0; the module defines no result.
- **001D0D60**: times whose float_to_int index reaches outside RAM (t = +-MAX, NaN) are
  not in the case set; both sides would stop at an unmapped row address, and the test
  does not carry them as crash cases. A loop flag with a length that never brings t below
  it never returns, in the original and here.
- **Memory model of the test.** The oracle's RAM mirror of 0x02000000..0x3FFFFFFF is off
  (section 5 A) so that it faults where the module faults. Neither side models the
  hardware's 0x20000000 / 0x30000000 views of RAM. A caller handing the module such a
  pointer would get FAULT_UNMAPPED where the original works; no captured pointer is one.
- **00198D90**'s callees 0018C4B0 / 0018C6A0 are stubs here (their effect belongs to the
  worker); the SDK leaves run as original code.
- **002160B0 (B)**: 8 of the 438 HEALING cases of the full run are not applicable on the
  room images (the status hub is not open there); they are counted, not compared.
- **001AA4E0 / 0015FDF0 (B)**: no entry of the captured target lists qualifies; the target
  and aim paths run with the captured entries' flags and type edited (section 5 B).
- Section B imports the existing tests' internals (`run_case`, `item_case`,
  `WorldJump`, `expected`, `original_arc`); a change to those names breaks this test,
  not the modules.
- The mutation sweep does not cover the region plumbing (`span`, `rd`, `wr`, the fault
  latch; lane SIDE / SYS code with the names changed, swept there) or operators other than
  those listed.
