# AREA01 lane SYS: the remaining main-route boot functions

Level 2 (AREA01) side track, lane SYS, 2026-09-25 (session s87); verification revised in
rounds 4 to 7, 2026-09-26 (section 4; round 7 is the close-out). New files only:
`src/game/em_area01_sys.{c,h}` (prefix `em_area01_sys_`) and
`tools/test_area01_sys_reference.py`. The module is built and tested but not wired.
Nothing in the port calls it yet, and wiring waits for the AREA01 load (AREA01_OVERVIEW.md
section 11, step 4).

## 1. Scope

These are the census rows of `../Extermination/build/s87/census/a01_delta.json` (field
`new_functions`) in the subsystems entity_logic, entity_sys, level_world, lowmem,
stream_archive, stream_music, weapon_equip, input_io, init_io, frame_main, unknown_01 and
unknown_02. A row qualifies when it is neither side-beat-only nor exit/AREA00-only. There
are 24 functions and 17,300 bytes.

| Function | Bytes | Decomp | Census subsystem | First beat (beats) | Where |
|---|---:|---|---|---|---|
| 001287F0 | 56 | C linked from asm | lowmem | a01_00 (10) | this module |
| 00128B80 | 132 | NEARMISS | lowmem | a01_00 (10) | this module |
| 00128C10 | 2924 | NEARMISS | lowmem | a01_00 (12) | this module |
| 00157CE0 | 592 | byte-matched | entity_logic | a01_00 (12) | this module |
| 00158590 | 632 | NEARMISS | entity_logic | a01_00 (12) | this module |
| 00158D30 | 388 | NEARMISS | entity_logic | a01_00 (12) | this module |
| 00159B90 | 724 | NEARMISS | entity_logic | a01_00 (12) | this module |
| 0015A2C0 | 1164 | NEARMISS | entity_logic | a01_00 (12) | this module |
| 00191120 | 236 | asm | init_io | a01_00 (3) | em_camera_follow_original (existing) |
| 0019B4C0 | 512 | NEARMISS | level_world | a01_00 (5) | this module |
| 0019CF50 | 992 | NEARMISS | level_world | a01_00 (5) | this module |
| 001A06A0 | 1132 | NEARMISS | level_world | a01_00 (5) | this module |
| 001A8840 | 304 | byte-matched | unknown_01 | a01_00 (11) | this module |
| 001A9E00 | 352 | byte-matched | unknown_01 | a01_00 (5) | this module |
| 001AA000 | 312 | asm | frame_main | a01_00 (3) | this module |
| 001B0300 | 344 | byte-matched | entity_sys | a01_04 (4) | this module |
| 001B0C00 | 88 | byte-matched | entity_sys | a01_07 (1) | em_script_host_workers (existing) |
| 001B0D80 | 60 | byte-matched | entity_sys | a01_00 (10) | this module |
| 001B6D70 | 208 | byte-matched | input_io | a01_03 (2) | this module |
| 001B76D0 | 36 | byte-matched | unknown_02 | a01_03 (1) | this module |
| 001E3D90 | 2160 | NEARMISS | weapon_equip | a01_00 (12) | this module |
| 001E7CB0 | 100 | byte-matched | stream_archive | a01_00 (12) | this module |
| 001E7D20 | 3608 | NEARMISS | stream_archive | a01_00 (12) | this module |
| 001FAD70 | 244 | byte-matched | stream_music | a01_07 (1) | em_stream_lanes_original (existing) |

001E7C60 (68 bytes) is also translated here. 001E7D20 calls it in area 0x13 only, and the
census does not list it because the route never reaches that area.

**IOP / CD.** Only the stream_* rows are named after streaming. 001E7CB0 and 001E7D20 make no
IOP or CD call; they are pure EE code. 001FAD70 is verified at its EE-side effect, the two
fields it writes in a stream lane record (fade step +0x54, release byte +0x5C). 001B6D70
issues stream and sound requests (001FA790, 001FABB0, 001FAE70, 001FB9F0, 001FBC50,
001FBD50), and they are verified as the calls it makes: callee, arguments and order. The
IOP side of every one of these is outside this lane.

## 2. Model

- **Memory.** The routines address original EE memory through caller-supplied regions,
  keyed by original address: RAM, the scratchpad and a stack region. Some locals have
  their address passed to a callee:
  - the vector at sp+0x40 in 00158590;
  - the vector at sp+0x50 in 0019B4C0;
  - the 0x60-byte block at sp+0x80 in 001E3D90;
  - the point at sp+0xA0 in 001E7D20.

  These locals live in the original frame layout, below the caller's stack pointer `sp`, so
  the callee sees the original's addresses. Every other local is a C local.

  **Limit: records in the stack below `sp`.** The original keeps its saved registers in its
  frame, and each callee saves the caller's registers (and its return address) in the frame
  it opens below the caller's `sp`. The module keeps neither. So for an input in which a
  record the module reads, or a pointer it follows, lies in the stack below the entry `sp`,
  the module does not reproduce what the original reads there, and nothing is claimed for
  it. Whether the original can receive such an input depends on its callers; section 4
  ("Round 5", F20) names the one survivor whose equivalence rests on it.
- **Calls.** A call that leaves the module goes through the single worker `call`. The worker
  gets the callee's address, the argument registers the original sets (a0..t3 as 64-bit
  register images, f12..f15 as raw bits) and the stack pointer. It returns v0/f0. For an
  indirect call (the pointer at +0x4C), the address passed is the pointer the original
  loads. Callees translated in this module are called directly.
- **Arithmetic.** All of it goes through `em_ee_float.h`, the measured model.
- **Store trace (test builds only).** When `EM_AREA01_SYS_STORE_TRACE` is defined as a
  function name, every store is reported to it (original address, size) once the address is
  known to be mapped. `wr` and `st64` are the only store paths. The oracle test uses the hook
  to compare memory at every call. Ordinary builds compile it out.
- **Fail-stop.** The module faults on any of these:
  - an unmapped address;
  - a missing worker;
  - a negative worker result;
  - `EM_AREA01_SYS_FAULT_UNDEFINED`, on the three inputs where the original reads a value it
    never set:
    - 0019CF50 when no cell span beats the word 0x7000320C (it would read its caller's
      s1/s2/s4);
    - 001E3D90 with a record byte +0x0D above 2 (row set, pair and phase steps unset);
    - 001E7D20 when the word D_00275C10 is outside 0..14 and is not re-clamped (it would
      read past its 15-entry local table).

## 3. What each routine does

The full description is in the comment above each translation in `em_area01_sys.c`. These
are short forms, by address:

- **001287F0(a0, a1, a2; f12).** When the halfword a1+0xF8 differs from (short)a2, stores
  a2 there. It then calls 001C67E0(a0, a2, a2; f12 passed through, f13 = 0).
- **00128B80(a0, a1).** Returns 0 when both halfword +0x36 and D_0081080F are 0. Otherwise it:
  - sets bytes +0 = 3 and +4 = 2 and clears +5..+7;
  - calls 0012E070(a1);
  - copies +0x34 to +0x36 when D_0081080F is set;
  - returns 1.
- **00128C10(e).** The owner of the 0x828A00 group records, five states:
  - 0 and 4: set-up through 00128AB0 and 00129780;
  - 1: the per-frame machine by kind +0x0D and sub-state +5. It uses distance tests against
    the point D_00810350 (001B13F0 with radii 10/20/24/40/100/150), heading changes
    (00122BB8, 001B1470, 001B12B0) and countdown tables D_00242EB0/B6/BC. The tail calls
    001C2770, 001C3D60, 001C64F0, 00102958 and 001C69A0, then calls +0x4C;
  - 2: 00129FC0;
  - 3: release through 001AFC10.
- **00157CE0(a0, a1).** Only when bit 2 of +0x0B is set. It sets the scratch vector and
  calls 001B6F00. It then sends one of four events (D_00246FB4, D_00247274/D_002472B4,
  D_00247734) through 001BA1A0/001BA1F0, depending on type +3, D_00810C7F, D_00810CB2,
  +0xB4 against 6 + D_00810354, and D_00810C63*30 against D_00810CB4. Results are 0..3.
- **00158590(p, a1, mode).** Unless the sign test (001028D0 then the 00102738 product with p+0xF0)
  is negative, it fills 0x700038A0 in one of four ways by mode. It then sets the four words
  at 0x700038B0 by a1 and calls 001F4CC0.
- **00158D30(p).**
  - State 0: set-up (001B0FD0, +0x80..0x88 = 2.0, 001C6380).
  - State 1: the four words 0x700038B0 by halfword +0x2E, 0x700038C0 = 1.0, then +0x4C when 001B17A0
    agrees, then 001F4A10.
  - States 2/3: 001AFC10.
- **00159B90(p).**
  - State 0: set-up through 001B0FD0, 001C6380 and 001C5570.
  - State 1: sub-state machine over 00157CE0(p, p+0x1F0), 001BA1A0 and 001BA1F0, then two
    00158590(p, 1, -2) requests at (±1.75, 1.7, 2.03). It then calls 001B17A0 and +0x4C.
  - State 2: +4 = 3.
  - State 3: 001AFC10.
- **0015A2C0(p).**
  - State 0: set-up (row D_00248120, 001E9580). It also picks the link mode from
    D_002481B0/D_002481D0 with the counters D_008106EC/ED and runs 0015A200 twice for
    mode 1.
  - State 1: 001E9E60(p, +0x0E), then the link-1 timer (100 up, 60 down, 001FBD50 every 128
    frames, 0015A750) or the link-2 timer (120 up, 0015A200 kind 13, halfword +0x2E, the
    random reload from D_002481F0). It ends with 001B17A0.
  - States 2/3: 001AFC10.
- **0019B4C0(a0, a1, a2, flags).** Builds the scratch segment 0x70003190 → 0x700031A0 and
  pulls it back by 0.01 along its direction. It runs 001A06A0 (flags & 2) and/or 0019CF50
  (flags & 4), restores the segment and records the hit delta at 0x700031C0. With bit 31
  set it adds that delta to a0+0xB0. Returns mode 0/2/4 (also stored at 0x700031D8).
- **0019CF50().** Picks the smallest cell span of the six bounds that 0019F1A0 fills. Each
  face on it that passes the halfword box test and the kind filter (below 0x5A, not
  0x51..0x53) goes to 0019ED80. Keeps the last hit point. Returns 0 on a hit, 1 otherwise.
- **001A06A0().** For each list record at D_00275B7C whose box overlaps the segment bounds,
  it walks the record's hit shapes: 001A4030 / 001A50A0 / 001A5C30 by kind 0x1000, 0x2000,
  0x4000 and 0x8000, each with its own size rule. The first hit copies the point, records
  the record and narrows the bounds. Returns 0 on any hit, 1 otherwise.
- **001A8840(a0, a1).** Box contact of a0+0xA0 with a1+0xB0 against the extents at a1+0x30.
  It then acts by a1+0x0D: 00187EC0(6, +0x56) and the a0 state change (a0+0x22C = 5.0,
  a0+0 = 3), or 00187EC0(7, 0). Clears the halfword 0x70003B86.
- **001A9E00 and 001AA000.** Circle push-outs. They use the radius at *(+0x30); 001AA000
  also takes a height sum and the +0xE4 key pair, where 0x200 only clears 0x70003B86. They
  use the atan2 / wrap / sin / cos leaves 0011E620, 001B1470, 0011DE90 and 0011E2A8.
- **001B0300().** Sets up the D_008101E0 record from the scene entry
  D_0024D650[D_00810700][D_00810701] + D_00810702 * 0x30. It fills the enable byte, the
  low bits, the row D_0024A8D0, the +15 offset and the three quadword copies, then calls
  001DD980.
- **001B0D80(a0).** When +0xB4 < -200: sets +4 = 3 and returns 1. Otherwise returns 0.
- **001B6D70(a0, a1, a2).** Dispatches the opcode at a2+8 to the six callees above.
  Returns 1.
- **001B76D0(a0, a1, a2).** Calls 001B1E20(a2+0x14, a2+0x18) and returns 1.
- **001E3D90(p).**
  - State 0: seeds (00122BB8), identity plus translation matrix, row pointer, and
    +0x34 = 0x001E3D20.
  - State 1: the area/sub-state early-outs, three matrix copies, per-variant translation
    steps, 0021B9A0 in five area keys, and the 001CD070 / 001CD2B0 / 001CFAE0 / 001CFBE0
    requests with the LCG texture offsets. After that come the countdown, the variant sound
    call 001FC3C0 (0x411/0x412/0x413), the three phases and 001B17A0.
  - States 2/3: 001AFC10.
- **001E7CB0().** Returns 0 in area 0x13 when D_00810702 is 4, 5, 7, 8 or 9. Otherwise
  returns 1.
- **001E7D20(p).** The record at D_00275C20 + byte +0x0D * 0xA060, a 32 x 32 grid of
  points, texture pairs, heights and rates.
  - State 0: fills the grid and the per-area constants.
  - State 1: the area-0x13 level fill (001E7C60) and the random point request 001E8B90.
    Then the neighbour relaxation of the rates (damping s+0x20, coefficient s+0x24,
    4 + s+0x28), the texture pairs from height differences, and an impulse at a random
    cell. Then heights += rates and point height = s+4 + 0.02 * H. When 001E7CB0 allows it,
    the packets are built: 30 strips plus two blocks via 001CB5F0, then 001CB950 (area 0x13:
    the D_002553B0 entry chosen by D_00275C10 / D_00275C14), 001CB6B0 and 001CB760.
- **001E7C60(s; v).** Sets all 1,024 grid heights, and the word s+4, to v.

## 4. How it was verified

`tools/test_area01_sys_reference.py`

**Oracle.** FallEE is the shared EE interpreter with the measured COP1/VU0 model. It runs
the original code of each routine from the captured AREA01 RAM. The captures are the
end-of-beat images `../Extermination/build/s87/route_a01/<beat>/eeMemory.bin` and
`scratchpad.bin` for a01_00..a01_07. The native module runs over a copy of the same bytes.

**Callees.** Every call leaving the translated set is caught on both sides and handled by
one Python policy:

- **Run as original.** On the native side this happens in a second interpreter bound to the
  module's own memory. It covers the vector and quadword leaves 00102948, 00102958,
  001026A0, 00102738, 00102760, 001028B8, 001028D0, 00102918, 001029C0 and 00103230; the
  math leaves 0011DE90, 0011DF78, 0011E2A8, 0011E620, 0011E748, 001B1470 and 001B12B0; the
  LCG 00122BB8; 00128600 and 001B13F0; and the collision leaves 0019ED80, 0019F1A0,
  001A4030, 001A50A0 and 001A5C30.
- **Stub.** Every other callee is recorded and returns a scripted value. In the ordinary cases
  stubs have no side effects. In the "fx" cases (below) every stub has a scripted side effect,
  and it is identical on both sides.

The test reads every direct call target of the translated routines from the pinned ELF and
asserts that each one has a policy.

**Lockstep: what is compared at every call.** The original runs first. At each call that
leaves the translated set it records the callee entry and the bytes of every RAM and
scratchpad line (64 bytes) it has stored to since the previous call. The native module is
then built with its store trace (`EM_AREA01_SYS_STORE_TRACE`, a compile-time hook in
`wr`/`st64`, the module's only two store paths; ordinary builds compile it out) and run. At
the same call index it must match:

- the callee address and the stack pointer at the call;
- every integer argument register the callee reads, as full 64-bit register images;
- (round 6) every register the native module tells the worker it sets (`na` integer and `nf`
  float registers; the header's contract is "the ones the original sets for this call")
  holds the original's register value at that call. The counts must be at least the
  policy's and at most 8 / 4. Before round 6 the test only checked `na >= policy`, so a
  translation that claimed eight set registers passed;
- every float argument register the callee reads;
- the 16 bytes behind every argument that points at a stack local (the escaping locals of
  section 2), and for stubs the bytes behind their pointer arguments;
- **all of RAM and the scratchpad**. Both sides were equal at the previous check, so only
  lines that either side stored to since then can differ. Exactly those lines are compared:
  the native side's lines come from the store trace plus the stores of its run-policy
  callees, and the original's come from its interpreter. A difference fails the case with
  the call index, the callee and the first differing address.

The same memory check runs after the last store. Then the result, the call count, all
32 MiB of RAM and the scratchpad are compared in full, as before. A wrong native store is
therefore caught at the first call that could see it, even when a later store in the module
or a run-policy callee overwrites it before the end. The rest of the two stacks is not
compared: the original keeps saved registers there and the translation keeps C locals.
No callee write is replayed from the original onto the native side. Run-policy callees
execute as original code over each side's own memory, so their stores are traced and
compared like the module's. Stub side effects (fx cases) are applied at the stub, after the
entry check has shown both memories equal. They are traced on both sides and compared at
the next call.

**Starting state (round 4).** Both stacks start filled with the byte 0xA5, not zero, so a
translation that leaves part of an escaping local unset hands the callee 0xA5 where the
original stored a value. The interpreter's registers start with a pattern (the integer
registers the caller does not set hold 0x01F0A000 + 0x40 * n, mapped RAM that is zero in
every beat; the float registers a normal value), so an original that reads a register it
never set (the UNDEFINED paths) sees a value no translation constant stands in for.
Since round 5 a case may also write both stacks identically (`stack`): a record placed
below the entry stack pointer, where a run-policy callee's frame lies.

**Stub side effects ("fx" cases).** A stub writes nothing. So a translation that keeps a
value in a local across a stubbed call, where the original loads it again after the call,
would pass every ordinary case: the round-2 review described two such edits (00128B80 reading
D_0081080F once before 0012E070, and 00159B90 loading its +0x4C handler before 001B17A0).
An fx case is a variant of an ordinary case, and it runs in two passes:

1. The original runs once with a recording interpreter. It notes every data load the
   translated routines make after each stubbed call and before the next one, by address and
   width. Stack addresses and the code range are left out. A loaded stack local, such as one
   of the escaping locals of section 2 that a callee fills, is therefore never changed. A
   value cached across such a call is not covered by the fx cases.
2. The original runs again. After the k-th stubbed call, the stub changes exactly those
   fields: bytes and halfwords flip bit 0, words and doublewords flip bit 4. The exact
   bytes written are recorded, and the native run writes the same bytes after its own k-th
   stubbed call.

Words that hold a code address (a jump-table entry, a handler) are left alone, with one
exception: the record's handler pointer (the case's `indirect` pointer) is moved to a
second address that is stubbed on both sides. A translation that calls a handler it loaded
before the stub then calls the wrong one and fails at that call's entry.

Changing every loaded field at once can turn the path away before the cached value is used.
Examples are a state byte, the link mode, a type byte, or a guard such as `+0x0B != 0` in
front of the read under test. Five cache edits of the round-3 sweep survived that design. So
there is a second kind, the **single-load-site variant**. For every load instruction the
translated routines execute after a stubbed call (a "site"), the field it reads is the only
one changed, at every stub it follows. Each site gets several fx cases (below): the first
ones in which that load reads a distinct value. A flip is visible only for some values: a
byte compared with 1 shows the flip of 1 or 0, not of 4.

One field is held inside its defined range instead of flipped at bit 4 (`FX_RANGE`).
D_00275C10 is 001E7D20's index into its 15-entry local table. Outside 0..14 it is one of the
UNDEFINED inputs of section 2, so it gets bit 0 flipped only when the result stays in
0..14. With that rule, every fx case passes on the unmutated module.

There is an fx variant of every non-fault case of the 11 routines that load memory after a
stubbed call: 00128B80, 00128C10, 00157CE0, 00158D30, 00159B90, 0015A2C0, 001A8840,
001E3D90 and 001E7D20, plus 0019CF50 / 001A06A0 in their forced-leaf synthetic cases. That
is 1,518 variants in round 6 (1,506 in round 5, 1,488 before). The other routines either make no stubbed call or return right after
their last one. In full mode each load site gets up to twelve single-site variants (the
first cases in which it reads distinct values; since round 7 the default run builds only
the pinned ones), and one more that flips
the field's top bit instead of bit 0 (a signed and an unsigned reading differ, and so do a
narrower and the full one; pointer words and FX_RANGE fields are left alone). That is 859
single-site variants in full mode.

**Deferred-read variants (round 5, "fx before").** The kinds above change a field after a
stub only when the original loads it after that stub. They cannot see the opposite error:
a translation that reads a field after a stub where the original read it before (the
round-4 review's F34, 001E3D90's seed word read after 001CD2B0 instead of before 001CD070,
and F38, 001E7D20 recomputing its grid pointer after the 001E8B90 splash stub). Every fx
case therefore has a second variant. At the k-th stub it changes every field the original
loaded since the previous stub (pass 1 records them the same way; stack addresses and code
words excluded, the handler pointer moved as above, FX_RANGE kept in range). The n-th change
of a field flips bits that no earlier change of it flipped: a byte bit n, a halfword bits n
and 8 + n, a word bits 4 + n and 16 + n (a pointer word only bit 4 + n), a doubleword bits
4 + n and 36 + n. So after the change, every value the field holds differs from the one the
original read, in both halves (a consumer of only the high half, like 001E3D90's
`seed >> 16`, sees it). A translation that defers the read across that stub, or across any
later one, reads a different value. These variants count as variants for the not-comparable
and UNDEFINED rules below. That is 1,518 more fx cases in full mode (3,036 in all).

What the fx kinds cannot see: a read moved across a run-policy callee, or across one of the
module's own stores, when the callee or the store writes that field. That happens only when
records alias, or when a record lies where a run-policy callee writes (a copy destination, a
callee frame). Round 5 added targeted cases for the five aliasing survivors of the round-4
review (F09, F17, F19, F26, F42). The round-5 review showed ten more such edits at other
sites, so round 6 adds a generic kind, the **aliasing variants** ("Round-6 variants",
below). Section 2's limit still covers records in the stack.

Since round 7 the default run runs only the pinned fx cases and single-site variants
("What the default run runs", below); every other fx case runs in full mode.
`EM_AREA01_SYS_FX=0` skips the fx cases, and `=only` runs only them.

**Round-4 variants (full mode only).** These make one kind of translation error observable
at every site of that kind, instead of case by case. Each is built from the cases that ran,
while the test runs:

- **Store-site variants.** A store instruction of the original is covered when, in some
  case, every byte it stores differs from the byte that was there. For every uncovered
  store site, the first two ordinary cases that reach it get a variant in which every byte
  that site stores is set beforehand to the complement of the byte it stores. A translation
  that drops the store, or stores fewer bytes, leaves the complement in memory. Every byte
  or halfword store site also gets a variant in which the bytes just after the field (as many
  as it has) are 0xA5: a translation that stores a wider field writes them with the zero or
  sign bits of a small value, never 0xA5. The two kinds are separate variants, because
  changing one field can turn the path away before the store.
- **Load-site variants.** For every load instruction, the first three ordinary cases that
  reach it with distinct loaded values get a variant with the loaded field's top bit flipped
  in the starting memory (not for pointer words), and for byte loads one with the byte after
  it XOR 0xA5. A wider, narrower or differently signed reading then computes with another
  value.
- **Stub-result variants.** For every routine and every stubbed callee it calls, the first
  four cases that call it along distinct paths get one variant per result -1, 2,
  0x80000000 and 0x7FFFFFFF, in which every call to that callee returns it. A translation
  that tests a callee's result with a neighbouring comparison (`> 0` for `!= 0`, `<= 0` for
  `== 0`) then takes another path. No assumption is made about what a real callee returns.
- **Stub pre-store variants.** For every fx case, two variants in which each stub, instead of
  flipping loaded fields, sets every field the original stores after it (and before the next
  stub) to the complement of what it stores there, or the bytes after each byte / halfword
  field it stores to 0xA5. A translation that drops a store after a stubbed call, or stores a
  narrower or wider field, then leaves the stub's bytes where the original overwrote them.

Some variants change a field whose other values select a documented UNDEFINED input
(D_00275C10, and 001E3D90's record byte +0x0D): those fields are left alone. A variant that
still makes the input UNDEFINED passes only when the native module refuses it and the
original's branch outcomes show the documented path (0019CF50's span test never finding a
new best; 001E3D90's variant switch taking the "above 2" arm). A variant that sends the
original outside the memory the test maps (an address the interpreter does not map, or one
it folds onto RAM as a mirror while the native module faults) is not comparable: it is
counted and not compared (24 of 5,977 in the round-6 full run; 24 of 5,927 in round 5 and
24 of 5,891 in round 4).

**Round-6 variants (full mode only).** Two generic kinds, built like the round-4 ones from the
ordinary cases that ran. In full mode those cases run with `SysEE.track` set, which records
what the two kinds need.

- **Aliasing variants ("alias").** A translation that reads a field before a store where the
  original reads it after, or that keeps an earlier read where the original reads the field
  again, can differ only when that store writes the field. The store may be the routine's own
  or a run-policy callee's. That happens only when two records alias, or when a record lies
  on a global that the routine or a callee writes. The tracking pass records, for every load
  instruction of the translated routines, the `ALIAS_RECENT` (4) distinct store instructions
  that ran last before it at another address. Stores in run-policy callees count; the stack
  does not. For each such (load, store) pair, the first ordinary case that shows it gets one
  variant. In it, one record moves so that the loaded field lies on the stored bytes, keeping
  the natural alignment of both accesses.
  - Which record moves: the load's record when it has a base the case controls, otherwise the
    store's record.
  - What counts as a base: a pointer argument, or a pointer word that the routines or their
    callees load and that held this value in the starting memory. For a pointer word, the
    word itself is rewritten.
  - How an address is attributed: to the nearest base at most `ALIAS_REACH` (0x1000) below
    it. An address with no such base is fixed (a global, a table).
  - Pairs that get no variant: pairs whose load and store have the same base, and pairs whose
    load and store are both fixed. Neither can alias.
  - What the moved record keeps: every byte the case loaded through its base is copied to
    the new place. The exception is a byte that the case also loads through another base or
    at a fixed address. That byte keeps its own value, so the loaded field still holds its
    old contents until the store writes it.

  1,072 variants. They can find translation errors in the module itself, not only in mutants:
  on the unmutated module every one either passes or is not comparable (below).
- **Float-compare variants ("float site").** The EE compare and a host float compare differ
  on raw words: the EE flushes a denormal to zero, and it orders an exponent-255 pattern as a
  finite value beyond MAX, where the host sees a NaN. The tracking pass follows, inside the
  translated routines, each word loaded by lwc1 (or by lw and then moved with mtc1). A load
  site counts when its word reaches a COP1 compare (c.eq, c.lt or c.le) while the compared
  register still holds the loaded bits. The first two ordinary cases that reach such a site
  with distinct values get one variant for each of 0x7FC00000, 0xFFC00000, 0x80000001 and
  0x00000001, written at every address that site loads. For `a < b` or `a <= b` with a raw
  operand, one of the two exponent-255 words gives the EE compare and the host compare
  different results, whatever the other operand is (unless it is exponent-255 too). The
  denormals separate the two only against a zero operand. 356 variants.

Round 6 also adds these not-comparable rules, each counted with the others:
- In an aliasing variant, a misaligned halfword, word or doubleword access stops the original.
  The EE raises an address error there.
- In an aliasing variant, the translated routines may load a word of the ELF image that held a
  code address (a jump-table entry) and was changed by the move. The translation's compiled
  switch does not read that table.
- In an aliasing variant, the original may jump through a data word that is not code.
- In any variant, a run-policy callee may not return within 5,000,000 instructions (an
  ordinary run allows 50 million). All five seen stop inside 001B1470, the angle wrap. Four
  are float-compare variants that give it an exponent-255 angle; one is an aliasing variant.
  The test does not prove that these loops never end. It only counts such variants as not
  compared and makes no claim for them.

The jump-table rule comes from a round-6 failure on the unmutated module. In `alias 128DE8
128D98 128C10 a01_00_train_room 7a8250 as captured`, the moved 00128C10 record lay over
00128C10's own jump table in the ELF data, and the original then dispatched through the
changed entry. That is a test artifact, not a translation error.

The variant step limit also keeps the full run from spending ten minutes of CPU in those
loops.

**Fault cases.** A fault case (an UNDEFINED input) now also runs the original, with the
pattern registers above standing in for the unset ones. The native module must refuse in the
routine that reads the unset value (the fault address names it: 0019CF50, 001E3D90 or
001E7D20). When the original's run completes in the interpreter (16 of the 19 fault
cases; in the other three the original goes on to an address the test does not map: both
0019CF50 cases, through its caller's registers, and D_00275C10 = 0x10005 in 001E7D20, through
its local table), the native module must also have made exactly the calls the original makes
before that point, compared call by call with memory, and the original's branch outcomes
must show the documented path (0019CF50, 001E3D90).

**What "verified" means here.** Every case is compared as described above: each store at
the first call that could read it and again at the end, each call's inputs, the result, and
all of memory. The fx, round-4 and round-6 variants add the side effects and starting states
described above. The mutation standard of this track asks that every single-operation change
to the translation either make the default or the full run fail, or be proven equivalent to
the original for every input the original can receive at that site. What was measured is
exactly this, and nothing more:

8,336 single-operation mutants were tried across the rounds, in these sets:

| Set | Mutants | Killed | Proven equivalent | Other |
|---|---:|---:|---:|---|
| Rounds 1-3, hand-picked (`build/area01/sys/scratch/`, `mut/`, `mut3/`) | 182 | 176 | 6 | killed against the round-3 test, not re-run (three re-checked in round 7: Y04, Y06, Y31 fail the round-7 default run) |
| Round 4, twelve operator classes (`build/area01/sys/r4/`) | 7,974 | 7,374 | 538 (290 compile to the original's object, 248 by rule) | 15 undefined C (no claim), 47 do not compile |
| Round-4 review (`build/area01/sweep_SYS_1/`) | 55 | 37 (31 by the default run, 6 by the full run only) | 17 | F20 open |
| Round-5 review (`sweep_SYS_2/`) | 55 | 46 (42 default, 4 full only) | 9 | |
| Round-6 review (`sweep_SYS_3/`) | 55 | 44 (33 default, 11 full only) | 11 | |
| Final review of round 6 (`final_SYS/`, U01..U15) | 15 | 15 (11 default, 4 full only) | 0 | |
| **All** | **8,336** | **7,692** | **581** | 15 undefined C, 47 not compiling, 1 open (F20) |

The equivalents are listed with their proofs: round 4's in the appendix "round-4
survivors", the review sweeps' in the appendix "review-sweep equivalents", and rounds 1-3's
six in "Mutation sweeps, rounds 1 to 3" below. No named survivor remains except F20, which
has been open since round 5 ("F20, proven only under section 2's limit", below). The sweeps
did not converge (every review sweep found survivors the earlier ones had not), so this
covers these mutants only: it is not a claim about single-operation changes in general.

How the kills were measured:

- **Review sweeps (rounds 5 to 7, 180 mutants).** In round 7 every one of them was run again
  against the final test (`build/area01/sys/r7/verify7.py`, the reviewers' mutant sources
  in each sweep's `mut/`; the module source is unchanged since round 3). The default run,
  unfiltered, fails on 120 of them (`verify7_quick.jsonl`, with the three round-2 re-checks
  Y04 / Y06 / Y31, which are counted in rounds 1-3). The 26 others that an earlier run had
  killed were run in full mode filtered to the cases built from their recorded killing
  case's base (`verify7.py only`, `EM_AREA01_SYS_ONLY`), and all 26 fail
  (`verify7_only.jsonl`). For 25 of them the case they fail on is one the unfiltered full
  run builds (checked by name against `r7/final_names.json`). T09's was a single-site
  variant (`fx site 15A5B0 15A2C0 t zero edge`) that the unfiltered full run does not build,
  so it is now pinned (`QUICK_SITE_PINS`) and T09 fails the default run. The rest are the 37
  proven equivalents and F20.
- **Round 4.** Measured against the round-4 test, confirmed then with each kill's recorded
  killing case (below). Since then the test has only added cases, checks and variants: the
  round-6 full run was checked to contain the recorded killing case of every round-4 kill
  ("Round-4 kills in the round-6 test"), and the round-7 full run contains every case and variant name of the round-6 full
  run (`r7/names7.py`). The 132 kills whose killing case changed input in round 6 were
  re-run then. The others were not re-run.
- **Rounds 1 to 3.** Not re-run since round 3 (see "Mutation sweeps, rounds 1 to 3").

What the generic variant kinds do and do not show (this is coverage, not a kill claim):

- The aliasing variants relocate one record per (load, store) pair, only for the four
  store instructions that ran last before each load, and only records reached through a
  pointer argument or a pointer word of the starting memory. A pair can be covered and a
  mutant at that site survive them: N06 (value-dependent) and T31 / T32 (a block pointer
  returned by a stub) are killed only by targeted cases.
- The float-compare variants are built for every load whose raw word reaches a COP1 compare
  (through lwc1, or lw then mtc1) in the cases that ran. "Built" does not mean a mutant
  there dies: for c.eq the special words separate the EE compare from a bit or host
  compare only against a zero or saturated partner, which the variants do not arrange
  (T18), and a compare whose effect shows only in a later iteration needs a case that
  reaches that iteration (T44). Both are killed by targeted cases only.

The three lane functions in other port modules (00191120, 001B0C00, 001FAD70) are checked
only by their end state or call log (below) and are not part of any sweep.


**Contracts (em_area01_sys.h).** Tested by `api_checks` (both runs) or by every case:

- UNMAPPED: code 3, the access address and the entry, for reads past the end of RAM and of
  the scratchpad, reads straddling either end, and a region smaller than the access (at its
  base and at base + 1); a region with NULL bytes is skipped; an access that is exactly a
  whole region is served (round 7); the region-end reads that succeed are ordinary cases.
- A NULL worker gives code 1 and a failing worker code 2, each with the callee address; a
  worker failure after stores leaves those bytes written (round 7).
- A NULL context returns -1 for every entry. A NULL output gives (1, the entry, 0) with no
  worker call, for every entry with a result (checked from a context whose fault address
  is 0).
- While a fault is latched every call returns -1 with no worker call and the fault fields
  unchanged, for the codes the module sets and (round 7) for -1, -5 and INT_MIN;
  em_area01_sys_clear_fault resets the three fields; clear_fault(NULL) does nothing; the
  first fault is kept (a worker that re-enters the module, faults there, then fails).
- UNDEFINED: code 4 with the fault address naming the routine that reads the unset value
  (the fault cases, both runs since round 7).
- The result and `*out` (the low 32 bits of v0) are compared in every case; so are the
  callee, the stack pointer at the call, the 64-bit images of every argument register the
  callee reads, and the float argument registers.
- `na` / `nf`: at least the registers the callee reads, at most 8 / 4, and every register
  the module claims to set holds the original's value at that call.

Not tested: that `na` / `nf` equal exactly the number of registers the original sets (a
smaller count that still covers what the callee reads passes, as T27 shows); that a NULL
context latches nothing (there is no context to inspect); a NULL output from a context whose
fault address is not 0; which bytes a fault leaves written other than in the one API check
above.

**Register reads, measured.** `RegScan` in the test measures, from the pinned ELF's
instruction words, which registers each callee reads before writing them. It tracks the
GPRs and FPRs, and also the hidden state a caller could leave behind:

- HI/LO of both multiply pipelines;
- the COP1 condition flag and the COP1 accumulator;
- the VU0 state, per lane: vf0..31 x/y/z/w, vi0..15, the VU0 accumulator, Q, I, R and the
  MAC/status/clip flags. vf0 and vi0 are constants. A write with a partial dest mask
  defines only its lanes.

The scan covers every path from the callee's entry to its return: delay slots (a likely branch's slot only when
taken), jump tables resolved from their bound checks, and everything the callee calls. The
test fails when any of these holds:

- a callee reads an argument register that its CALLEES policy does not compare;
- a callee reads a caller temporary (at, v0, v1, t4..t9, f0..f11, f16..f19) or any of the
  hidden state above;
- a callee the test executes (run policy) reads a caller-saved register (s0..s7, fp,
  f20..f31) other than to save it.

The scan covers all 78 policy callees and the one +0x4C pointer that the cases hand over
(001CAA00). All of them read only registers that their policy compares. Three policies
compare a register the callee does not read: 001C67E0 (a2), 001F4A10 (a2) and 001288D0 (a0).
That is harmless. Two findings are listed in the test and enforced exactly, so any new case fails:

- **One data-driven jump (`UNRESOLVED_JUMPS`).** Script command 9's handler 001B99F0 is
  reached from 001BA1F0 through the command table at 0x0024D880. It jumps to a function
  pointer held in the script record (word +4), with a0..a2 set by the dispatcher. That
  target is script data, so the scan takes it to read a0..a2 only. This is an assumption,
  not a measurement.
- **Caller-saved reads of stubbed callees (`CALLER_REG_READS`).** Each is on a path the scan
  cannot rule out, and none is reached from a run-policy callee. The native module does not
  model saved registers, so a future binder must keep these paths unreachable:
  - 0015A750: s1, when its record halfword +0x54 is 7 or more.
  - 0017B490: s0, when its switch index is 7 or more.
  - 0018CBD0: f20, on one float-compare path.
  - 0019C830: s0/s2/s3, on its tail paths.
  - 0019CB60, 0019D330, 0019D770 and 0019E280: s1/s2/s4, the same pattern as 0019CF50.
  - 001CFBE0: s0/s1, when a1 is 7 or more. This module passes 1 or 6.
- **VU0 lane reads of two stubbed callees (`VU_READS`).** 00129780 and 0012D580 reach the
  cross-product leaf 00102718. It reads vf6.w only to subtract it from itself, which clears
  the w lane of its result. VU0 arithmetic has no NaN or infinity, so the lane is +0 whatever
  vf6.w held. This is not an input.

The measurement found no callee that reads HI/LO, the COP1 flag or accumulator, or any other
VU0 state before writing it. The flags read by vclipw, cfc2 and bc2 are counted as reads.
Ordinary arithmetic only sets them. Removing either `VU_READS` entry, or documenting a read
the scan no longer measures, fails the test.

One indirect call has no table: 00129FC0 calls the record's +0x4C pointer at 0x12A5A4. The
scan treats that call conservatively, as reading every argument register. At that point
every argument register is already written or clobbered, so the call adds no input.

**Case classes:**

- **Captured.** Every live owner record of the eight beats, as captured.
- **Random.** Perturbed state bytes, flags, positions, LCG seeds (00128C10, from a separate
  stream) and scripted results. For 0019B4C0 (and through it 001A06A0 / 0019CF50) this means
  probe segments over the captured AREA01 collision world, with its leaves run as original.
- **Enumerated ("t").** Every sub-state and both sides of each test. LCG seeds are found by
  search so that the original's own draws hit the grid edges, bit 4, the heading bits 3..7
  and the carries of the texture offsets. Where a case would otherwise start with the value
  the original stores, the field is **poisoned**. This was done case by case in rounds 1 to
  3, where the sweeps found a store the cases could not see; round 4 adds the store-site and
  pre-store variants, which do it for every store site the cases reach. The case-by-case
  poisoned fields:
  - 00128B80 +5..+7;
  - 00158590 +0xBC;
  - 00158D30 +0x80..+0x88;
  - 00159B90 +0x0B (sub-states 2 and 3), and in state 0 +0x0A, +0x30, the halfword +0x34,
    byte +0 and 0x700038A0..AC;
  - 00158D30 state 1: 0x700038B0..CC;
  - 0015A2C0 state 0: every field it clears;
  - 001B0300 D_008101E7;
  - the 001E7D20 packet blocks.

  Boundaries are hit exactly: a zero and a negative-zero dot product in 00158590 (the
  negative-zero cases were wrong until round 6: their local vector had mixed signs, so the
  VU0 dot product was +0. Round 6 sets every local component to +1 with a -0 direction, or
  to -1 with a +0 direction; `build/area01/sys/r6/negzero.py` shows that 00102738 returns
  0x80000000 in all eight); the
  `+0xD0 == 0x5A` idle count in 00128C10; frame words whose low five bits are 0 but not the
  low six; D_00810707 = 0/1/2 in 001A8840; and an 001A9E00 contact with a1+0x0B clear.
- **Edges ("t"/"s", `cases_edges`).** These were added in round 3. Each makes one original
  operation observable, so that a narrower or wider operation gives a different result:
  - 00158D30: byte +0x0B = 2/4/0x80/0xFE (a whole-byte test);
  - 00159B90 state 0: every field poisoned (the +0x34 store is a halfword);
  - 001A06A0: list records with type byte 0x14/0x34/0x24/0xE4 (a five-bit mask);
  - 001B0300: a synthetic scene entry whose word +0x10 has bit 6 set, or is 0xFFFFFFC5.
    That gives row -1, because the row index is an arithmetic shift;
  - 001E3D90: D_00275C00 negative (a signed compare), and +0xB8 at -700.0 and one ulp
    either side (a strict float bound);
  - 00157CE0: D_00810CB2 between the signed and the unsigned reading of +0x34 * 2;
  - 001287F0: a negative +0xF8 equal to a2 (a signed halfword compare);
  - 00128B80: +0x36 with only its high byte set (a halfword test).
- **Round-4 edges ("t"/"s", `cases_edges4`, `cases_edges4b`, `cases_edges4c`,
  `walker_edges`).** Added for the round-4 sweep; each makes one original operation
  observable that no other case separated from its single-operation neighbours:
  - 00157CE0: byte +0x0B bit by bit on both type paths; D_00810CB2 and +0x34 with only
    their high byte set; the float test +0xB4 < 6.0 + D_00810354 one ulp below 6.0, with
    D_00810354 = 1.0 and with D_00810354 = 0.75 ulp(6.0) (the EE sum truncates to 6.0);
  - 00158590: modes -3, INT_MIN, 3, INT_MAX, -INT_MAX (the entry takes any word); a1 = -1;
    mode -1 with inexact sums; mode 2 at the origin (the scaled sine / cosine reach the sums
    unchanged);
  - 00128B80: D_0081080F clear with the byte after it set; the bytes after +4..+7 and
    +0x36 poisoned;
  - 001A8840: each box test at its edge (equal to the extent, 0.75 ulp above it, the height
    bound 1.5 + extent at 1.5, one ulp above, and with extent 0.75 ulp(1.5)); D_00810707 and
    a0's byte +0 with the byte after them set;
  - 001A9E00 / 001AA000: ten contacts near the origin with non-round radii and positions
    (inexact subtractions, sums and products), a 3-4-5 contact at d == sum, the height test
    at its float edges (0.75 ulp above the limit, a first sum 1.5 ulp above 0.5, equal to the
    limit, a second half height 0.75 ulp above 0), an infinite first or second height with
    a finite other one (the EE halving saturates to MAX; an infinite value reaches the
    following EE add differently), a0's byte +0 = 8 / 0x88 / 0xFB, and keys equal only in
    their low halfword;
  - 0015A2C0: a row that overflows when doubled; the timer +0x20 at 99.99999 + 1, 119.00001
    + 1, 1.0 - 1, 99.0 + 1 and infinite; the frame word 0x70003B64 one bit at a time; the
    halfword +0x2E at 0xFF (its increment carries); the record placed so that +0x56 overlaps
    the link-1 counter D_008106EC (the counter increment changes the halfword before it is
    tested); the byte 5 before +5 set;
  - 00159B90: a non-identity matrix at +0xD0 (the w of the matrix product is not 1.0);
  - 001E7CB0: D_00810700 = 0x13 with D_00810701 set;
  - 00128C10: 0x70003B8D one bit at a time and with the byte after it set; state 0
    sub-state 1 for kinds 3, 4, 5, 8, 9, 10, 0x84, with the byte after the kind set, and
    with 00129780 returning -1; sub-states 8, 9, 0x80 at the "below 9" gate; 001B1630
    returning one bit at a time; +0x0A = 2 / 6 / 0xFE; b+0xD0 at 0xFF, 0x60 and 0x7FFF;
    kind 0x84 in state 1; the table D_00242F20 with a distinct entry per kind; +0x28 = 0x100
    and 0x8000; the byte before +6 set; the clip halfword b+0xF8 not 1 (so 001C67E0 is
    called before the kind is read again) per kind, with the byte after the kind set;
    001C2770 returning -1;
  - 0019B4C0: every flag bit but the tested ones; bit 31 with a hit and the word after the
    delta poisoned;
  - 0019CF50: bounds at 0xFF (the span end increment carries);
  - 001A06A0: shape headers with every low bit set and a callable shape after each kind;
    a record past the count; first byte 0 with the next byte set; type bytes 6 and 0x0C;
    a box offset of 0x10000; an index equal to the box count; index 0 with an overlapping
    box; each box face on the segment bound; +0x54 = 0x4F; a count byte 0x80; after a hit,
    a second record that each narrowed bound excludes (segments running down from 12.0,
    starting on the hit, and below it); a record whose box offset is 0 with the table
    header shaped like an overlapping box;
  - 001E3D90: the three phases at 0, just below and at 2.0, and at 16777220.0; the word
    after them above 2.0; D_008101E4 = 4 and 3 with the byte after it set; the key (0, 2)
    with D_00810702 = 5; seeds whose texture offsets carry differently for +10 / +11 and
    -11 / +11; a negative 001CD2B0 result; the byte after +0x0D set (states 0 and 1); a
    variant byte 0x83 in state 0 (UNDEFINED); the counter s3+4 at 0xFFFF; state 0 with the
    bytes it clears poisoned and with the first draw 0x7FFFFFFF; a matrix translation word
    just below 32.0 with its last bit set (each EE step crosses 32 and truncates);
  - 001E7D20: the level fill on each floor and at 16777220.0 on the 1.0-step floor; the
    grid clamp edges (z = 921.0 and one ulp above, x one ulp either side of 930.0, areas
    0x12 / 0x14, D_00810701 set); the byte after +0x0D set; non-round extents; a
    non-round +0x28; infinite and near-MAX heights; the splash coefficient infinite,
    MAX and denormal; a splash draw of exactly 0; frame word and D_00275C14 equal only in
    their low halfword; a table entry with every bit set; D_00275C10 = -2, -1, 15, 0x10005
    unclamped (UNDEFINED) and 13 clamped; area 0x13 record 1 with seeds whose random point
    differs in its last bit between the EE product and a neighbouring constant or an IEEE
    one.
- **Probe ("t", `cases_probe`).** For 0019B4C0: type bytes with bit 4 set, and a first point
  that is the scratch segment itself. The original reads a1[i] again after storing
  0x70003190[i], and this case makes that visible.
- **Synthetic ("s").** Lists, boxes, cells and faces written into zeroed RAM at 0x01E00000.
  These are the test's own inputs, not disc data. The leaf tests are scripted so that every
  walker branch is taken.
- **Fault.** The UNDEFINED inputs, which the native module must refuse (see "Fault cases"
  above).

**Branch coverage, asserted.** `EM_TEST_FULL=1` asserts that both outcomes of every
conditional branch in the 22 routines are taken, except the two listed in `UNREACHABLE`
(with proof), and that those two stay untaken:

- **001A06A0 at 0x1A0804, taken.** This is the negative-index test. The index is the byte
  (halfword +0x0E >> 8) & 0xFF, computed just before the test and unchanged, so it lies in
  0..255.
- **001E3D90 at 0x1E3FAC, not taken.** This is the variant switch for a record byte +0x0D
  above 2. On that path the original goes on to use the row set, pair and phase steps it
  never set (its caller's registers). The translation refuses with UNDEFINED, and the fault
  cases check that. No stubbed call comes before the switch, so no fx case can change +0x0D
  there.

**Correction to round 2.** Round 2 also listed **0x1E4480 not taken** (the sound-id choice)
as reachable only on the UNDEFINED path. That was wrong. The original reads +0x0D again for
the sound id after its stubbed calls, so a callee that changes the byte takes the branch. An
fx case now does this; the original then skips the sound call, and the translation, which
reads the byte again, does the same. The branch is covered and asserted.

The default run prints its coverage but does not assert it. Since round 7 it runs only the
pinned killing cases and one captured record per routine ("What the default run runs",
below), so its coverage is partial by design.

**Existing translations.** These are checked through a bridge that calls the port functions
unchanged, with the original running beside them:

- `em_camera_follow_00191120`, 300 cases. Its wrap worker runs the original 001B1470.
  Result bits and the wrap arguments are compared.
- `em_script_host_001B0C00`, 5 cases, call logs compared.
- `em_stream_lanes_001FAD70`, 240 cases over the captured lane records, all RAM compared.
  Only lanes 0..2 are tested: the port module faults on a lane index above 2, where the
  original would index past its three records.

These three are not under the lockstep. Only their end state or call log is compared.

**Mutation sweeps, rounds 1 to 3 (hand-picked lists).** 182 distinct single-operation edits
(the lane's 82 of round 2, the round-2 review's 52, and 48 of round 3): 176 killed, 6 proven
equivalent. Their proofs:
- the 001A06A0 minmax `<=` -> `<`: R-MINMAX (the two orders differ only for equal compare
  keys, and min / max only enter later compares, where equal keys give equal results);
- the first of two stores to a rate cell dropped (001E7D20's relaxation stores d1 at the
  cell and then d1 + d2 at the same address, with no load or call between): R-OVERWRITE;
- the 001E7D20 clamp `i + 1 >= 0x20` -> `>= 0x1F`: both give 0x1F at i = 30 and i = 31, and
  i + 1 otherwise; the D_00275C10 clamp `n >= 0xE` -> `n > 0xE`: both store 0xE at n = 0xE;
- the 001A8840 extent word read once before its 0011DF78 calls: 0011DF78 is a leaf with no
  store and no call (N62's proof);
- review M03 as written (00128B80 reads D_0081080F into a local before testing +0x36, and
  uses the local only in that first test): the two loads are only reordered, with no store
  or call between; D_0081080F is a fixed RAM global, so the extra read when +0x36 is non-zero
  reads mapped memory and changes nothing. Its true form (the local reused after the
  0012E070 stub, X41 of round 3) is killed.

Their
tooling and logs are under `build/area01/sys/scratch/`, `build/area01/sys/mut/` and
`build/area01/sys/mut3/`. Those results were measured against the round-3 test and were not
re-run: round 4 changed the test's starting state (the stack and register patterns) and the
fault-case checks. Their edits that fall in the round-4 classes were measured again by the
round-4 sweep below. The others (a value cached across a stubbed call, a hoisted read) were
killed by the round-3 fx cases and not re-run. The round-3 fx design had two blind spots
that the round-4 review showed with survivors: a read made after a stub where the original
read it before (F34, F38), and a re-read or cache across the module's own store, or across a
run-policy copy, when records alias (F09, F17, F19, F26, F42). Round 5 closes the first with
the deferred-read variants and the second only at the reviewed sites (targeted cases).

**The round-2 review's 14 survivors (round 4).** Each is one mutant of the round-4 classes,
and each now fails the full run (`build/area01/sys/r4/review14.py` maps them):

| Review | Edit | Killed by |
|---|---|---|
| Y04 | 00157CE0 `& 4` -> `& 0xC` | `157CE0 t byte0B fb t12` |
| Y06 | 00158590 `mode != -2` -> `> -2` | `158590 t mode -2147483648 ...` |
| Y31, Z10, Z11 | 001E3D90 phase steps B / A / C one ulp up | `1E3D90 t phases 0.0 d1` |
| Y41 | 001E3D90 state 0 drops `+0x0C = 0` | `fx 1E3D90 t st0 poisoned` |
| Z01 | 001E3D90 state 0 drops `+9 = 0` | a store-site variant (site 1E3E98) |
| Z02, Z03 | 001E7D20 state 0 drops `+0x0C` / `+9 = 0` | store-site variants (1E8100 / 1E8108) |
| Z04 | 001E7D20 state 0 drops `+0x20 = 0` | a store-site variant (1E7DF8) |
| Z08, Z16 | 00128C10 drops the halfword `+0x54 = 0` (state 1 / state 0 sub 1) | store-site variants (128D44 / 128CDC) |
| Z14, Z15 | 00128C10 drops `+7 = 0` (state 4 sub 3 / ground test) | store-site variants (12975C / 129160) |

**Round-4 sweep: every mutant of twelve operator classes.** `build/area01/sys/r4/`
(`mutgen4.py` generates, `sweep4.py` runs, `equiv4.py` / `equiv4_manual.py` classify,
`docgen4.py` prints the tables below; `results.jsonl` holds every verdict).

- **Region.** The translation: the constant macros (from `F_ZERO`) and every translated
  routine and helper, up to the public entry wrappers; comments excluded. Not swept: the
  module's plumbing before it (memory access helpers, fail-stop, call marshaling), the
  store-trace hook and the entry wrappers. They are not original operations; every case
  exercises them, but no single-operation claim is made for them.
- **Classes** (each at every site in the region; identical sources generated once):
  SDL-store (delete a store statement), SDL-call (delete a call statement), CRP (each
  integer literal +1 and -1, not array sizes in declarations), MASK (each bit 0..bitlen of a
  literal operand of `&` flipped), ROR (relational operators, the sufficient subset of
  Kaminski et al.: `<` to `<=`, `!=`, false; `>` to `>=`, `!=`, false; `<=` to `<`, `==`, true;
  `>=` to `>`, `==`, true; `==` to `<=`, `>=`, false; `!=` to `<`, `>`, true), EEC (the EE
  compare helpers, the same subset), LCR (`&&` / `||`), AOR (binary `+`/`-`, `*` to `+`,
  `/` and `%` to `*`, `<<`/`>>`, `&`/`|`, `^` to `|`, compound assignments, `++`/`--`), EEF
  (EE add / sub swapped, mul / div swapped, each EE add / sub / mul / div / int-to-float /
  negate replaced by the host IEEE operation, the accumulator forms split into separate
  operations), WIDTH (loads `u8`<->`s8`, `u16`<->`s16`, `u8`->`u16`, `u16`->`u8`,
  `s16`->`s8`, `w32`->`u16`; stores `st8`->`st16`, `st16`->`st8`, `st16`->`st32`,
  `st32`->`st16`), CAST (`(intN_t)` <-> `(uintN_t)`), NEG (delete a unary `!` or `~`).
- **Method.** Each mutant is compiled with the test's flags (and the store trace) and run
  against every case, fx case and full-mode variant of the entry routines that can reach the
  edited line (a static call graph of the C), until the first failure. The original's side
  of each case is recorded once and replayed exactly as `run_case` compares it (validated:
  every case passes on the unmutated module, and each round's replay disagreements with the
  real test were traced to a stale recording and re-run). Every kill is then confirmed by
  the test's own `run_case` on the killing case. A mutant that has survived 200 cases is
  compiled again without the trace and compared with the original's object file; an
  identical object is the same program. The final pass ran every killed mutant again with
  the final test (its killing case first): see "Confirmation" below.
- **Result** (7,974 mutants):

  | Class | Mutants | Killed | Identical object | Proven (rule) | Undefined C | Does not compile | Unresolved |
  |---|---:|---:|---:|---:|---:|---:|---:|
  | SDL-store | 409 | 408 | 0 | 1 | 0 | 0 | 0 |
  | SDL-call | 140 | 140 | 0 | 0 | 0 | 0 | 0 |
  | CRP | 4100 | 3949 | 17 | 74 | 14 | 46 | 0 |
  | MASK | 233 | 214 | 15 | 3 | 0 | 1 | 0 |
  | ROR | 672 | 569 | 80 | 22 | 1 | 0 | 0 |
  | EEC | 105 | 102 | 0 | 3 | 0 | 0 | 0 |
  | LCR | 37 | 37 | 0 | 0 | 0 | 0 | 0 |
  | AOR | 934 | 913 | 19 | 2 | 0 | 0 | 0 |
  | EEF | 209 | 193 | 0 | 16 | 0 | 0 | 0 |
  | WIDTH | 989 | 823 | 42 | 124 | 0 | 0 | 0 |
  | CAST | 124 | 4 | 117 | 3 | 0 | 0 | 0 |
  | NEG | 22 | 22 | 0 | 0 | 0 | 0 | 0 |
  | **All** | **7974** | **7374** | **290** | **248** | **15** | **47** | **0** |

  "Killed" includes 7 mutants whose run crashes the test process (a write past a local
  array or through a NULL argument array). "Does not compile": 47 edits that are not valid
  C (for example a count raised past a NULL array). "Undefined C": 15 edits that write
  outside a local array or leave an element of it unset (listed below); C gives them no
  meaning, their compiled behaviour happened to match, and nothing is claimed for them.
  **Among these twelve classes there are no unresolved survivors: every other mutant fails
  the full run, compiles to the original's object, or is proven equivalent by one of the
  rules below.** (The round-4 review's own edits, outside these classes, are in "Round 5";
  one of them, F20, is open.)

**Equivalence rules.** Each survivor that compiles to a different object is assigned one
rule; `equiv4.py` checks the preconditions of the first five on the source text, and the
others are assigned per site in `equiv4_manual.py` with the facts they need at that site.
The per-mutant lists are in the appendix.

- **R-ARGS.** The edit raises the integer or float register count of a call, or changes the
  value of a register beyond the count (001CB950's a3 when three are passed). The extra
  register reaches the worker, but no callee reads it: RegScan measures from the ELF every
  register each callee reads on every path, the test fails when a callee reads a register
  its policy does not compare, and no policy compares more than the original passes. Where
  the raised count also reads one element past the local argument array, that C read is
  undefined, but its value only fills the register no callee reads.
- **R-SIGN-EQ.** A load changed only in signedness (`u8`<->`s8`, `u16`<->`s16`) and compared
  `==` / `!=` with a constant K, 0 <= K < 2^(w-1): the readings agree below the sign bit,
  and a value with the sign bit set equals K in neither.
- **R-MASK-LOW.** A load changed in width or signedness and masked at once with a constant
  below 2^w, w the narrower width: the two readings share every bit below w.
- **R-LOW-STORE.** A load (plus or minus a constant) that only reaches a store no wider than
  the narrower reading: the stored bytes depend only on the low bytes, which the readings
  share (arithmetic modulo 2^w).
- **R-SWITCH-SIGN.** A switch on a load changed only in signedness, every case label below
  the sign bit: a value with the sign bit set reaches `default` in both readings.
- **R-UNSIGNED.** `== 0` to `<= 0`, or `!= 0` to `> 0`, on an unsigned operand.
- **R-SIGN-LOCAL.** A load changed in signedness and assigned to a local whose every use is
  a comparison that cannot tell the readings apart (listed per site: `==` / `!=` with small
  constants, or an unsigned `<` against a constant below 0x80, which a sign-extended byte
  fails exactly as the byte does).
- **R-OVERWRITE.** The bytes a wider (or the dropped) store writes are stored again by the
  next statement, with no call and no load of them in between.
- **R-ALREADY.** The extra byte a wider store writes already holds that value: the
  statement before stored it, or the test just before read it, with no call or store in
  between.
- **R-RANGE.** The operand's range at that site is fixed by the code before it (named per
  site: the switch arm, a just-stored byte, a byte times 30, two bytes both 1).
- **R-DAZ.** 00158590: the word 0x700038B4 becomes the denormal 1 instead of +0; before it
  is stored again (both a1 branches) its only reader is an EE add, which flushes a denormal
  operand to 0.
- **R-BOOL.** The argument is used only as a truth value.
- **R-NEGZERO.** 001A9E00: e == 0 is negated to -0; e then only enters a compare, where -0
  and +0 have the same key.
- **R-EE-FINITE.** 001A9E00: the negated value is an EE subtraction result (finite, never
  denormal); on such a value the EE negation is a sign flip.
- **R-EXACT.** The operation is exact for every operand the site can receive (an integer
  below 2^24 converts exactly; scaling a finite normal value by a power of two with a normal
  result is exact; the EE divide rounds to nearest-even like IEEE on finite normal operands
  with a normal result), so the EE and IEEE forms agree.
- **R-EE-DIV.** 001E7D20: the dividend is an EE product (finite, never denormal) and the
  divisor 30.0; the two divides agree except on a denormal quotient, which IEEE keeps and
  the EE flushes, and that quotient only reaches an EE add, which flushes it.
- **R-EXACT-HALF.** 001E7D20: halving an EE product is exact except below the normal range
  (as above), and the value only reaches EE adds.
- **R-EE-SQUARES.** 001A9E00 / 001AA000: dx*dx + dz*dz. Both products are squares and the
  accumulator a saturated square: the raw product differs from the saturated one only when
  it is +Inf or NaN (the EE packing saturates finite overflow), where the multiply-add gives
  +MAX and acc + MAX saturates to +MAX.
- **R-KEY.** The area key (byte << 8) + byte is compared only with 0x100, 0x1301, 0x1100,
  0xE00, 0x202, 0x200, 1 and 0, whose two bytes are below 0x80. Under each mutated formula
  (a sign-extended byte, a halfword, a shift by 7 or 9, a subtraction) the only byte pairs
  that give one of them are the original's pairs.
- **R-SHIFT-FILL.** The arithmetic-shift helper's fill bits never reach a result: one caller
  shifts non-negative words (the LCG draw), the other masks to 16 bits after a 16-bit shift.
- **R-DEAD-INIT.** An initial value that is never read: every read follows an assignment on
  all paths, and the path without one ends in the UNDEFINED fault.
- **R-DECL.** An array size in a declaration.
- **R-LOOP-NE.** A counter from 0 stepping by 1 with no other write: `<` and `!=` end the loop
  at the same count.
- **R-CLAMP.** 001E7D20: i, j, row and col are 0..31, where the clamped neighbour index is
  the same; and the grid clamp thresholds differ only at x == 930.0, which is clamped to
  itself.
- **R-MINMAX.** 001A06A0: the round-3 proof (the orders differ only for equal compare keys;
  min / max only enter later compares).
- **R-IDENT-ARITH.** `x - 0` is `x + 0`.
- **R-LOW16.** A value that only reaches uses of its low 16 bits.
- **R-MASK-USE.** 00128C10: `t != 0 && (t & 0xFF) >= 2` holds exactly when the low byte is 2 or
  more, whatever the byte after it holds.
- **R-SWITCH-DEFAULT.** 00128C10: kind_step switches on the same byte +5 (read again with no
  call or store in between) and does nothing for 9 and above.
- **R-RMW-BYTE.** 001A06A0: the round-3 proof (the halfword keeps its high byte and gets the
  low byte).
- **R-LOWBITS-SUM.** Only the low 6 bits of a sum are used; they depend only on the low
  6 bits of the addends.
- **R-CAST.** The value is converted to the parameter type either way.
- **R-RESULT-01.** The callee returns 0 or 1: 001B13F0 (run as original; its two return
  paths set v0 from the zero register or to 1) and the translation's own 001B0D80.
- **R-KIND-BITS.** 001A06A0: on these arms the kind's bit 12 is clear.
- **R-BYTE-MASK.** 001B0300: the operand is a byte (0..255), so bit 8 of the mask never
  matters.
- **R-TRIM.** 001E7D20 record 0's level step: the call runs only above the floor 132.0
  (exponent field 134 or more, or 255); the EE subtraction first clears the low (d - 1) >= 10
  bits of the step, and 0x3DCCCCCD and its two neighbours differ only in the low 2 bits.

**Confirmation.** The test changed during the sweep (cases and variants added; the stack and
register patterns; the fault-case lockstep; variants that reach an UNDEFINED input or leave
the mapped memory counted instead of failing). At the end every killed mutant was run again
against the final test, its recorded killing case first, with each kill confirmed by the
test's own `run_case`. 14 kills did not hold. 13 had come from load-site variants of
001B0300 that the final test counts as not comparable (the flipped scene word sends the
original to a RAM mirror the native module does not map). One (0019CF50 reading the span
bound word one byte early) had come from a load-site variant that the final test accepts as
the documented UNDEFINED path. New cases kill 6 of the 14 again (three 001B0300 scene
entries and the 0019CF50 fault case with the span bound 3), and the other 8 are proven
equivalent (R-MASK-LOW, R-OVERWRITE, R-ALREADY, R-RANGE, R-CAST, R-BYTE-MASK, R-SIGN-EQ).
The table above is the state after that: every killed mutant in it fails the final test.

**Round 5 (2026-09-26): the round-4 review's survivors.** The review swept 55 hand-written
edits of its own (`build/area01/sweep_SYS_1/mutants.py`, sources in its `mut/`) against the
round-4 test and listed 16 that neither failed nor were equivalent. Each is now run against
the final test by `build/area01/sys/r5/verify5.py` (full mode, cases of the named entries;
and the default run), and each kill was also checked with `run_case` on the unmutated module
(it passes) and on the mutant (it fails):

| Review | Edit | Killed by (full and default run) |
|---|---|---|
| F34 | 001E3D90 reads the seed word s3+8 after 001CD2B0 instead of before 001CD070 | `fx before 1E3D90 a01_00_train_room 7a96e0 as captured` |
| F38 | 001E7D20 recomputes its grid pointer from D_00275C20 / +0x0D after the 001E8B90 stub | `fx before 1E7D20 t 13 d1 p5 0 y200.0 f0` (full: `fx before 1E7D20 #5 s1 13/0 d1` first) |
| F42 | 001E7D20 drops the third read of D_00275670 (after the copy into blk+0x70) | `1E7D20 r5 block over the context word` |
| F41 | 001E7D20 adds the centre impulse after the four half-rate edge adds | `1E7D20 t splash 0,0 r5 centre rate 3f800000` |
| F29 | 001A06A0 resets its shape-hit flag once per call, not per record | `1A06A0 r5 hit then no shape` (and `... unknown kind`) |
| F26 | 0019B4C0 restores 0x700031A0 from a1 again instead of its kept copy | `19B4C0 r5 a1 is the segment end` |
| F27 | 0019CF50 tests the y direction with a host float compare | `19CF50 r5 denormal end y 80000001` (and 807fffff; start y 00000001) |
| F09 | 0015A2C0 increments the link counter from the byte read before the +0x56 store | `15A2C0 r5 counter low byte` |
| F17 | 001A9E00 reads a0+0xA8 before its a1+0xB0 store | `1A9E00 r5 alias a1 = a0 - 8` |
| F19 | 001AA000 reads a1+0xB8 before its a0+0xB0 store | `1AA000 r5 alias a0 = a1 + 8` |
| F16 | 001A8840 reads a1's kind byte before the three 0011DF78 calls | `1A8840 r5 unmapped kind byte, no contact` |
| F03 | 00158590 mode 2 reads p+0xC4 once for both 0011E2A8 and 0011DE90 | `158590 r5 record in the callee frame 2.0` / `100.0` |
| S01 | the region end bound `<=` -> `<` (the last bytes of a region refused) | `1B0D80 r5 region end 01ffff48` (and 70003f48) |
| S02 | fault() without its first-fault latch | the fail-stop API checks (first fault kept) |
| S08 | the entry macro without the latched-fault refusal | the fail-stop API checks (refusal while latched) |
| F20 | 001AA000 drops the second reads of the +0x30 words (after the second 0011E748) | not killed; proof below, open exception |

S03 (an unmapped access reported as a worker fault), which the round-4 test killed only
incidentally in full mode, now fails both runs through the API checks.

How the new cases work:

- **Aliasing records** (F09, F17, F19, F26). The records are placed so that one routine's
  store lands on a field it reads again: 0015A2C0's record at D_008106EC - 0x56, so its
  halfword +0x56 is the link-1 counter byte; 001A9E00 with a1 = a0 - 8 and 001AA000 with
  a0 = a1 + 8 (the push-out store is the other record's field, at non-axis-aligned
  positions so the stored value differs); 0019B4C0 with a1 = 0x700031A0, the scratch segment
  end that a walker hit overwrites.
- **A run-policy copy over a global** (F42). The 32nd 001CB5F0 result (the 9-quadword
  header) is 0x275600, so the run-policy copy into blk+0x70 overwrites D_00275670, which
  points at a second render context written into test RAM.
- **A record in a callee's frame** (F03). Cases may now write both private stacks
  identically (`stack` in the case; `stack_image`). The run policy runs a callee 0x400 below
  the call's stack pointer on both sides, and 0011E2A8 stores values that depend on its
  argument into its frame. With p+0xC4 on such a word, the original, which loads p+0xC4
  again, passes 0011DE90 the value 0011E2A8 left; F03 passes the old one. This input lies in
  section 2's limit: the case shows the difference in the test's model of the stack, where
  both sides agree on the unmutated module.
- **Clamped splash cells** (F41). At row and column 0 two of the half-rate neighbours are the
  centre cell itself, so the order of the EE additions shows. A rate of 1.0 (0x32800000 and
  0x3E99999A in full mode) at the centre separates the orders. The (31, 31) corner cases,
  full mode, do not separate them for these values and are kept as coverage only.
- **Denormal segment ends** (F27). The EE compare flushes a denormal to zero, so +0 against
  -denormal (and a denormal start against +0) compares equal, where the host compare does
  not.
- **A hit followed by an empty box** (F29): a second qualifying record whose box has no
  shape, or a first shape of an unknown kind.
- **An unmapped kind byte** (F16): a1 = 0x6FFFFFE0, so a1+0x0D is unmapped while a1+0x30
  and a1+0xB0 are scratchpad; with no contact the original never reads +0x0D and returns.
- **Region ends** (S01): 001B0D80 reads its float at the last word of RAM (0x01FFFFFC) and of
  the scratchpad (0x70003FFC), below and not below -200.
- **Fail-stop API checks** (`api_checks`, 21 checks, both runs, native only; the original
  has no counterpart). An unmapped read past the end of RAM and of the scratchpad, and one
  straddling each end, gives code 3 (UNMAPPED) with that address and the entry; while that
  fault is latched the next entry returns -1 without calling the worker and leaves the three
  fault fields unchanged; em_area01_sys_clear_fault resets them and the next entry works. A
  NULL worker gives code 1 and a failing worker code 2, each with the callee address; a NULL
  output gives code 1; a NULL context returns -1. A worker that re-enters the module (which
  faults unmapped) and then fails leaves the inner fault latched: the first fault is kept.

**F20, proven only under section 2's limit.** Between the first and the second read of the
words a0+0x30 and a1+0x30, the only operation is the second 0011E748 call (run as original),
whose argument dy * dy is an EE product of a value with itself, so its sign bit is clear.
0011E748 opens a frame and saves f20, f21, s0, s1 and ra in five slots between 0x40 and 0x8
bytes below the stack pointer it receives. It then calls 0011CB90 and 0011E080, which contain
no store instruction and no call, and returns, unless its EE compare finds the argument
below zero, which needs the sign bit set. Only that error path stores elsewhere (a local
exception record, and the word 0x242670 through 0011FD78) or calls more. So the second call
changes nothing outside the 0x40 bytes below 001AA000's frame (its entry stack pointer minus
0x40), and F20 differs from the original only when a +0x30 word of a0 or a1 lies there, in
the stack below `sp`. Within section 2's limit F20 is equivalent. Whether the original can
receive such a record: 001AA000 has one reference in the boot ELF and the 21 overlay files
(the call in 001AA140; no jump, no word holding its address, no two-instruction constant
forming it). 001AA140 passes entries of the pointer list at D_00275B90 as a0 and a1. That
list is filled by 001B1CA0, which pushes the word +0x14 of the record 001B1B70 hands it. The
writers of that word were not traced, so the premise is not proven, and F20 stays an open
exception. The test cannot show the difference either: the run policy runs both 0011E748
calls at the same address with the same return address, and the saved s0 / s1 / f20 / f21
values differ between the original and the native side by design (the module does not
model saved registers), so no case both passes on the unmutated module and fails on F20.

**Quick-mode pins.** Seven survivors of the round-4 default run that its full run killed now
fail the default run too: S03 (API checks), S10 (`19B4C0 a01_00_train_room #9 long
80000004`, flags bit 31), F06 (`159B90 r5 sub-state byte a5`, the captured record with its
sub-state byte XOR 0xA5; the review's load-site variant is full-mode only), F14 (`fx site
1292B4 128C10 t sub3 e6 0 d0 1 bit 0 e1 0`), F30 (`fx site high 1A09C0 1A06A0 s synthetic
hit0`), F35 (`fx 1E3D90 #0 s0 d0 2/0`) and F43 (`fx 1E7D20 #6 s1 13/1 d0`). All 16 review
survivors but F20 also fail the default run (`verify_quick.jsonl`). The pins are
`QUICK_PINNED`, `QUICK_SITE_PINS` and the `core` flag of the round-5 cases (the slower
round-5 001E7D20 variants are full mode only).

**Round-4 kills in the round-5 test.** The full run was repeated with the name of every case
and variant recorded (`build/area01/sys/r5/names5.py`). The recorded killing case of every
round-4 kill still exists, except one: the store-site variant `store site 15A5E0 15A2C0
#86 s1 l1 sub1 f119.5` was no longer built, because a round-5 case covered that store site
(every byte it stores differs from the byte before). The check compared only the recorded
killing case of each kill, not every case and variant of the round-4 test. The two round-4
kills that variant made (line 914: the EE subtraction replaced by the host one, and +0x20 ->
+0x1F) were regenerated from `mutgen4.py`, and both fail the round-5 full run (`15A2C0 #86 s1
l1 sub1 f119.5` and `fx store 15A2C0 #86 s1 l1 sub1 f119.5`). Round 6 found the cause (new
cases changing store-site coverage) and removed it; see "Round 6".

**Round 6 (2026-09-26): the round-5 review's survivors.** The review swept 55 hand-written
edits of its own (`build/area01/sweep_SYS_2/mutants.py`, with sources in its `mut/`). It
listed 26 that neither failed nor were equivalent, and 11 more that only the full run
killed. `build/area01/sys/r6/verify6.py` ran every one against the final test: the default
run unfiltered, and the full run filtered to the cases of the edit's entries (the reviewer's
list). All 37 fail the default run, and all 26 fail the full run
(`verify_quick.jsonl` and `verify_full.jsonl`). The "Generic kill" column is the first
round-6 variant that fails on the mutant with no round-6 targeted case involved
(`gen_kill.sh`, which runs the full run's aliasing and float-compare variants on each
mutant).

| Review | Edit | Killed by (default and full run) | Generic kill |
|---|---|---|---|
| N02 | 0019B4C0 reads a0's byte +2 at entry | `19B4C0 r6 a0 on the segment start` | `alias 19B59C 19B55C 19B4C0 a01_00_train_room #0 zero 80000004` |
| N03 | 0019B4C0 reads a0+0x14 before the 0x7000324E store | `19B4C0 r6 a0+0x14 on 0x7000324C` | `alias 19B5B4 19B5B0 19B4C0 a01_00_train_room #1 down 6` |
| N04 | 0019B4C0 bit-31 adds use the kept deltas | `19B4C0 r6 a0+0xB0 on the delta, bit 31` | `alias 19B668 19B63C 19B4C0 a01_00_train_room #9 long 80000004` |
| N06 | 001A9E00 reads a0's byte +0 before the push-out stores | `1A9E00 r6 a0 = a1 + 0xB8 z0.7` (and three more z) | none |
| N07 | 001B0300 clears D_008101E7 after the +0x18 read | `1B0300 r6 entry at 0x008101CC` | `alias 1B0364 1B035C 1B0300 a01_00_train_room None` |
| N08 | 001B0300 reads +0x10 before the D_008101EC store | `1B0300 r6 entry at 0x008101DC` | `alias 1B0378 1B036C 1B0300 a01_00_train_room None` |
| N09 | 001E7D20 state 0 does not re-read D_00810700 | `1E7D20 r6 p+0x20 on the area byte` | `alias 1E7DFC 1E7DEC 1E7D20 #0 s0 0/0 d0` |
| N10 | 001E7D20 grid clamp tests the local area | `1E7D20 r6 p+0xB4 on the area byte` | `alias 1E7FEC 1E7F78 1E7D20 t init 13/0 d1 fff` |
| N11 | 00158590 reads p+0xB0..B8 before the first sum store | `158590 r6 p+0xB4 on the sum 0x700038A0` | `alias 158640 15863C 158590 7ad490 m-1 a0 fTrue` |
| N63 | 0015A2C0 reads the row word before 00122BB8 | `15A2C0 r6 LCG state on the row word` | `alias 15A344 15A334 15A2C0 #6 s0 l2 sub2 f0.0` |
| N19 | 00158590 dot sign test by the sign bit | `158590 t negative zero dot m1` / `n m-1` (corrected cases) | (not a generic kind) |
| N20 | 001A8840 first box test by host compare | `1A8840 r6 extent x -denormal` | `float site 1A886C 7fc00000 1A8840 #0` |
| N21 | 001A06A0 minmax by host compare | `1A06A0 r6 end x 7fc00000` | `float site 1A06F0 ffc00000 1A06A0 a01_00_train_room #0` |
| N30 | 001A06A0 x box overlap by host compare | `1A06A0 r6 box min x 7fc00000` | `float site 1A0810 7fc00000 1A06A0 a01_00_train_room #0` |
| N22 | 0019CF50 x direction by host compare | `19CF50 r6 end x 80000001` | `float site 19CF78 ffc00000 19CF50 a01_00_train_room #0` |
| N23 | 0019CF50 z direction by host compare | `19CF50 r6 end z 80000001` | `float site 19CFE0 ffc00000 19CF50 a01_00_train_room #0` |
| N25 | 001B0D80 -200 test by host compare | `1B0D80 r6 y ffc00000` | `float site 1B0D80 ffc00000 1B0D80 y=-200.0` |
| N26 | 001E3D90 -700 test by host compare | `1E3D90 r6 key 100 z ffc00000` | `float site 1E3EE4 ffc00000 1E3D90 #40 s1 d2 1/0` |
| N28 | 001E7D20 floor test by host compare | `1E7D20 r6 level ffc00000` | `float site 1E816C ffc00000 1E7D20 t 13 d0 p5 1 y200.0 f0` |
| N42 | clear_fault without its NULL check | API check `clear_fault(NULL)` (the mutant crashes the test) | - |
| N43 | 0019B4C0 entry without its NULL-output check | API check NULL output, every entry with a result (crash) | - |
| N56 | 00128B80 entry without its NULL-output check | the same (crash) | - |
| N44 | 001E7CB0's NULL output names 001E7C60 | the same (`api: NULL output 0x1e7cb0`) | - |
| N45 | invoke tells the worker all eight registers are set | the lockstep register check (first at `1287F0 7a8250 clip 2`) | - |
| N46 | span without the NULL-bytes skip | API check NULL-bytes region (crash) | - |
| N47 | span without the size bound | API check region smaller than the access | - |

The generic kinds kill 17 of the 18 aliasing and float-compare edits on their own. N06
does not die that way: its only (load, store) pair variant comes from the one ordinary
case with a push-out (`#1`), and in that case bit 2 of the pushed z's low byte equals bit 2
of the old one.

What changed in the test:

- **Targeted cases (`cases_review6`, 23 cases, both runs).** These are the reviewer's inputs,
  written as cases. A record is placed on a field that the routine, or 00122BB8 (run as
  original), stores before reading it again. Raw denormal and exponent-255 words are placed
  at an EE compare. Each case passes on the unmutated module and fails on its mutant (the
  `verify_*` logs).
- **The negative-zero dot cases were wrong** (the review's first blocking item). They set a
  -0 direction, but their local vector had mixed signs, so the dot product 00102738 returned
  +0, and N19 survived. The cases now make every product -0 (see "Enumerated" above), and
  `negzero.py` shows that the dot is 0x80000000 in all eight. The round-4 results that
  relied on these cases were re-checked (`recheck_negzero.py`, `recheck_w.py`):
  - At the sign test (line 395) there are three EEC mutants: `<=`, `!=` and constant false.
    The first is killed by `158590 t negative zero dot m-1` on its new input. The other two
    were killed by other cases.
  - All 132 round-4 kills whose killing case is a negative-zero dot case fail on that case's
    new input. Five of them had to be compiled without -Werror, as the round-4 sweep did.
- **Register counts.** The native module's `na` / `nf` are no longer checked only against the
  policy's minimum. At every call, each register the module says it sets must hold the
  original's value (lockstep list above). Exact equality with the policy counts was tried
  first and is wrong: 001B0300 passes three registers to 00102948, which reads two, because
  the original holds a third value in a2 at that call.
- **API checks (`api_checks`, 54, both runs).** New since round 5:
  - a NULL context for each of the 22 entries;
  - a NULL output for each of the 9 entries with a result, which must give (NULL, that entry,
    0) with no worker call;
  - `clear_fault(NULL)`;
  - a region with NULL bytes, which must be skipped;
  - a 2-byte region under a 4-byte access, at its base and at base + 1, which must give
    UNMAPPED at the access address.
- **Store-site coverage no longer depends on the newest cases.** A store site that some case
  covers gets no complement variant. So the round-5 and round-6 cases, by covering more
  sites, removed variants that earlier kills came from:
  - `store site 15A5E0 ...` (round 5);
  - in round 6 before this fix, the variants of 1E8100, 1E7DF8 and 19B5B0, which were the
    recorded killing cases of 297 round-4 kills.

  The cases of `cases_review5` and `cases_review6` are now marked `late` and do not count
  for coverage. With that, the round-6 full run builds every variant the round-4 kills name
  (see "Round-4 kills in the round-6 test" below).
- **Quick-mode pins.** Each of the 11 full-run-only kills of the review is now in the default
  run:
  - `QUICK_PINNED`: N05 `fx 1A06A0 s synthetic hit1`, N15 `fx before 128C10 t sub3 e6 0 d0 1
    bit 0 e1 1` and N17 `fx before 128C10 t st0 sub1 kind 4`.
  - `QUICK_SITE_PINS`: N13 (site 129060 on `128C10 t sub1 near heading draw 18`, top bit) and
    N16 (site 128E84 on `128C10 t sub0 clip kind 0 next 0`, top bit).
  - `QUICK_VARIANT_PINS`, each built from its base case alone: N12 `load site 128EC4 sign
    128C10 a01_00_train_room 7a8830 as captured`, N34 `stub result 1A4030=ffffffff 19B4C0 r5
    a1 is the segment end`, N35 `stub result 19ED80=ffffffff 19CF50 s synthetic hit`, N40
    `store site 159E44 after 159B90 s2 sub0 r1 b0`, N50 `load site 1B6D78 sign 1B6D70 op 0`
    and N51 `load site 1B76E0 sign 1B76D0`.

  All 11 fail the default run (`verify_quick.jsonl`).

**Round-4 kills in the round-6 test.** The final full run was repeated with the name of
every case and variant recorded (`build/area01/sys/r6/names6.py`, `final_names.json`). The
recorded killing case of every one of the 7,413 round-4 kills is among them. The variant
`store site 15A5E0 ...` that round 5 lost is built again, now that the round-5 deferred-read
variants and the round-5 and round-6 cases do not count for store-site coverage. Every name
the round-5 full run ran is also in the round-6 run. This checks that each recorded killing
case still exists. For 132 of those cases the input changed in round 6, and those kills
were re-run (above). The other killing cases have the same inputs as before, so their kills
were not re-run.

**Round 7 (2026-09-26): close-out.** The round-6 review (`build/area01/sweep_SYS_3/`, 55
edits T01..T56 without T29) and the final review of round 6 (`build/area01/final_SYS/`, 15
edits U01..U15) left eight edits that neither failed nor were equivalent, and 16 more that
only the full run killed. Round 7 kills each of the eight with a targeted case or an API
check, pins those killing cases into the default run, cuts the default run down to the pins,
and re-runs every review mutant of rounds 5 to 7 against the final test. No new sweep was
run. The module source is unchanged (em_area01_sys.c is byte-identical to its round-3
version); em_area01_sys.h changed in comments only (the fail-stop contract is now stated
exactly).

| Review | Edit | Killed by (default and full run) |
|---|---|---|
| T18 | 00128C10 kind_step 2: the EE c.eq of the approach result and b+0xE8 replaced by bit equality | `128C10 r7 goal 00000001 heading 00000000` (and `80000000 / 00000000`, `80000001 / 00000001`) |
| T55 | 00158590: `a1 == 0` tested on a1's low halfword | `158590 r7 a1 00010000 mode -2` (and `7fff0000 mode 1`) |
| T32 | 001E7D20 packets: the 001CB950 area test uses the area byte read before the block stores | `1E7D20 r7 block over the area byte 13` |
| T31 | 001E7D20 packets: the area key uses the area byte read before the block stores | `1E7D20 r7 block over the area byte 6` |
| T44 | 001A06A0: the x narrowing after a hit by a host `<=` | `1A06A0 r7 start x ffc00000 narrowed x from hit` |
| U13 | 001AA000: `k == 0x200` changed to `(k >> 8) == 2` | `1AA000 r7 keys 201/201` (and `2ff/2ff`) |
| U14 | 001A9E00: `a1+0x0B = 1` changed to `a1+0x0B \|= 1` | `1A9E00 r7 a1+0x0B 2` (and `80`) |
| T54 | the entry macro refuses only `fault > NONE` | API check: fault -1, -5 and INT_MIN are refused with no worker call |

How the new inputs work (`cases_review7`, 12 cases, both runs):

- **T18.** Sub-state 2 compares 001B12B0's approach result with the goal b+0xE8 by the EE
  c.eq, which treats +0, -0 and denormals as equal. With the goal 0x00000001 or 0x80000000
  and the current heading e+0xC4 = 0, 001B12B0 (run as original) returns 001B1470(0) = 0,
  which the EE compare finds equal to the goal and bit equality does not. The original then
  sets +5 = 3 and calls 00128600.
- **T55.** a1 = 0x00010000 or 0x7FFF0000 is non-zero, but its low halfword is 0. The entry
  takes any int32; the boot ELF's 47 call sites of 00158590 include ones that move a
  caller's register into a1, so a1 is not bounded by constant arguments.
- **T31 / T32.** The 32nd 001CB5F0 result (the 9-quadword header) is 0x008106F0, so the
  block's own store to blk+0x10 rewrites D_00810700 / D_00810701 before the original reads
  them again: in area 6 the stored 24.0 clears both bytes (key 0: the original stores
  47 / 44 / 44), in area 0x13 the stored 34.8 makes the area byte 0x33 (not 0x13: the
  original takes the default 001CB950 word).
- **T44.** The segment start x is 0xFFC00000: -MAX to the EE compare, NaN to a host
  compare. After the hit on record 1 the EE narrows maxx to the hit x (5.0), and record 2's
  box (x 7..9) is skipped; the host compare narrows minx instead and walks record 2 (an
  extra leaf call).
- **U13.** Key words 0x201 / 0x2FF have high byte 2 but are not 0x200: the original takes
  the push-out path (two 0011E748 calls); the edit only clears 0x70003B86.
- **U14.** a1+0x0B preset to 2 or 0x80: the original stores 1; `|= 1` leaves 3 or 0x81.

**Full-run-only kills pinned.** The 16 edits of these two reviews that only the full run
killed now fail the default run too, through these pins (each fails on the mutant with the
unmutated module passing; `build/area01/sys/r7/verify7_quick.jsonl`):

- `QUICK_PINNED` (fx cases): T12 `fx 128C10 t sub3 e6 0 d0 5 bit 0 e1 1`, T14 `fx 128C10 #42
  s0 sub0 k8`, T16 `fx before 128C10 t sub2 e8 +0.0`, T46 `fx before 1A06A0 s edge narrowed x
  from hit`.
- `QUICK_SITE_PINS`: T40 (site 1E4440 on `1E3D90 #0 s0 d0 2/0`), U11 (site 1E442C, top bit, on
  `1E3D90 a01_00_train_room 7a96e0 as captured`), and T09 (site 15A5B0 on `15A2C0 t zero
  edge`; see "What verified means").
- `QUICK_VARIANT_PINS`, each built from its base alone: T04 `float site 157DFC ffc00000 157CE0
  b4 t38 1 3/6 y0.0 2/60`, T22 `float site 1A8894 7fc00000 1A8840 #0`, T26 `alias 1B03B0
  1B039C 1B0300 a01_00_train_room 1`, T30 `alias 1E8004 1E7F70 1E7D20 t init 13/0 d1 fff`,
  T07 `stub result 1B0FD0=ffffffff 159B90 s0 sub0 r0 b0`, T19 `stub result 1B2140=80000000
  128C10 a01_00_train_room 7a8250 as captured`, T38 `stub result 1CD070=ffffffff 1E3D90
  a01_00_train_room 7a96e0 as captured`, U03 `store site 159C04 after 159B90 s0 sub0 r0 b0`.
  The bases of the aliasing and float-compare pins run with `SysEE.track` in the default
  run too (`TRACK_BASES`).
- `DEFAULT_CASES`: T42 `19B4C0 a01_07_level_exit #14 zero 80000006`.
- API checks: T50 (an access that is exactly a whole region, from its base to its end, is
  served). In the default run T50 fails first on `fx before 1A06A0 s edge narrowed x from
  hit`, which reads address 0 (the RAM region's base); the API check alone also fails on it
  (checked with every case filtered out).

T49 (0019ED80's result narrowed to a byte) fails only the full run, through a stub-result
variant (0x80000000). Every exit of the original 0019ED80 returns 0 or 1, so T49 is
equivalent for every input the original can receive; it is counted as killed because the
test fails on it, and it is not pinned.

**What changed in the test (round 7).**

- **The default run is the pins.** See "What the default run runs" below. The old default
  run sampled about half the cases (1,111 of 2,053) and cost about 38 s of CPU; the new one
  costs about 10 s (Results).
- **Pins run in both modes.** Full mode now adds any pinned single-site variant or pinned
  variant that its own generation did not build, so every pinned kill holds in both runs by
  construction. The added site pins are marked `late` (they do not count for store-site
  coverage), so the round-4 variant set is unchanged.
- **API checks** (59, both runs; 54 before): an access that is exactly a whole region is
  served (T50); a context whose fault holds -1, -5 or INT_MIN is refused with no worker call
  and its fault fields unchanged (T54); a worker failure after stores leaves those bytes
  written (the header's "bytes written before the faulting access stay written").
- **Plumbing fix, test only.** With `EM_AREA01_SYS_ONLY` in the default mode, a pinned site
  variant whose base case the filter removed made the test fail with a KeyError before any
  case ran; pins whose base is filtered out are now skipped. With `EM_AREA01_SYS_ONLY` every
  matching case runs (no sampling).
- **The fault cases** (15 of the 19 UNDEFINED inputs; the other 4 are slow 001E7D20 edges
  and stay full-mode only) run in the default run too, so the UNDEFINED contract is checked
  in both runs.

**What the default run runs (round 7).** Nothing sampled; only these, all of which also run
in full mode:

- every targeted case of rounds 5 to 7 that is not full-mode only (`cases_review5` with
  `core`, `cases_review6`, `cases_review7`);
- `DEFAULT_CASES`: the other ordinary killing cases of the review tables (the eight
  negative-zero dot cases, N45's register-check case `1287F0 7a8250 clip 2`, T42's world
  case, and the round-2 cases that kill Y04, Y06 and Y31);
- `QUICK_PINNED` (11 fx cases and one ordinary case), `QUICK_SITE_PINS` (7 single-site fx
  variants) and `QUICK_VARIANT_PINS` (14 variants, each built from its base case, which runs
  too; the aliasing and float-compare bases with tracking);
- the 15 fault cases that are not full-mode only;
- the first captured record of every routine, as a smoke check;
- the ELF policy check, the register-read scan (`RegScan`), the 59 API checks and the 113
  cases of the three existing translations.

That is 227 of the 2,065 cases that are not full-mode only (2,108 in all), 11 of 2,984 fx
cases, 7 single-site and 14 other variants. Everything else (the captured, random, enumerated, synthetic, edge and
probe cases, every other fx and deferred-read case, and every round-4 and round-6 variant
kind) runs only with `EM_TEST_FULL=1`. Round 6's statement that the fx keep-set was not
trimmed no longer applies: the default run no longer claims to kill anything beyond the
pinned mutants, and every kill counted in "What verified means" was measured on the default
or the full run of the round-7 test as stated there.

**Results (round 6, 2026-09-26; the default run then sampled, see round 7 for the current
numbers).** Measured with `EM_TEST_JOBS=4` (this lane's limit; the
default is up to eight workers). Other lanes kept the machine at a load average of 6 to 20
on 10 cores. CPU is user + sys over all worker processes. The first full run of the round,
at loads of 50 to 79 and before the variant step limit existed, took 646 s wall and 1,451 s
of CPU. Nearly all of the difference was the 001B1470 loops.

```
$ rm build/area01/sys/*.dylib; EM_TEST_JOBS=4 python3 tools/test_area01_sys_reference.py   # cold
mode quick: 1,111 of 2,053 cases, 233 of 2,976 stub side-effect cases and 163
  single-load-site variants (5752 scripted writes), 6 store-site, load-site, stub-result,
  stub pre-store, aliasing and float-compare variants (the six QUICK_VARIANT_PINS; 0 not
  comparable), 130964 worker calls compared ..., 22 routines, 113 cases of 00191120 /
  001B0C00 / 001FAD70 in their port modules, 54 fail-stop API checks
test_area01_sys_reference: OK (12.8 s)     # real 12.9 s, user 38.7 s, sys 1.9 s (40.6 s CPU), load 11
$ EM_TEST_JOBS=4 python3 tools/test_area01_sys_reference.py                              # warm, twice
test_area01_sys_reference: OK (10.4 s)     # real 10.5 s, user 36.7 s, sys 1.8 s (38.5 s CPU), load 12
test_area01_sys_reference: OK (10.2 s)     # real 10.3 s, user 36.3 s, sys 1.8 s (38.1 s CPU), load 20
$ EM_TEST_FULL=1 EM_AREA01_SYS_GAPS=1 EM_TEST_JOBS=4 python3 tools/test_area01_sys_reference.py
branch coverage (both outcomes / conditional branches): 1287F0 1/1, 128B80 3/3,
  128C10 59/59, 157CE0 7/7, 158590 8/8, 158D30 8/8, 159B90 14/14, 15A2C0 26/26,
  19B4C0 9/9, 19CF50 22/22, 1A06A0 31/32, 1A8840 8/8, 1A9E00 5/5, 1AA000 4/4,
  1B0300 2/2, 1B0D80 1/1, 1B6D70 1/1, 1B76D0 0/0, 1E3D90 33/34, 1E7C60 2/2,
  1E7CB0 6/6, 1E7D20 58/58
  1A06A0 missing outcomes: 1A0804T
  1E3D90 missing outcomes: 1E3FACF
full-mode branch coverage asserted: every outcome taken except the 2 documented
  unreachable ones
mode full: 2,096 cases, 3,036 stub side-effect cases and 859 single-load-site variants
  (207921 scripted writes), 7405 store-site, load-site, stub-result, stub pre-store,
  aliasing and float-compare variants (279 not comparable: they left the memory the test
  maps; by kind, run / not comparable: alias 1072/249, float 356/6, fx 3036/0, load
  1354/11, store 579/1, stub 1008/12), 2606437 worker calls compared ..., 22 routines, 545
  cases of 00191120 / 001B0C00 / 001FAD70, 54 fail-stop API checks
test_area01_sys_reference: OK (223.0 s)    # real 223.2 s, user 851.0 s, sys 10.2 s (861.3 s CPU), load 8..11
```

The default run costs about 38 s of CPU warm and 40.5 s cold. Round 5 measured 37.5 s warm
and 40 s cold. The cold build compiles the module with its store trace, and the bridge
over the six existing sources. The default run reaches the ~10 s target only with four
mostly idle cores (10.3 to 10.5 s warm here, at loads of 12 to 20). Full mode costs about
861 s of CPU, up from 663 s in round 5. The 1,428 aliasing and float-compare variants
account for about 200 s of that (`run_variants.py`: 200 s of CPU for all of them).
`nc_reasons.py` gives the reason for each of the 255 of them that are not comparable:
- 142 stop the original at an address the interpreter does not map (140 aliasing, 2 float);
- 96 aliasing variants hit a misaligned access;
- 5 aliasing variants make the original execute data;
- 4 aliasing variants reach a RAM mirror that the native module faults on;
- 3 aliasing variants change a jump-table word;
- 5 hit the 001B1470 step limit (1 aliasing, 4 float).

Quick-mode kill coverage was measured for the round-5 and round-6 review lists only:
- round 5: all 16 survivors but F20, and the seven pinned full-run-only kills;
- round 6: all 26 survivors and the 11 pinned full-run-only kills.

**Results (round 7, 2026-09-26, the final test).** `EM_TEST_JOBS=4`; CPU is user + sys over
all worker processes; other lanes kept the load average between about 20 and 120 on 10
cores during the round.

```
$ rm build/area01/sys/*.dylib; EM_TEST_JOBS=4 python3 tools/test_area01_sys_reference.py   # cold
mode quick: 227 of 2,065 cases, 11 of 2,984 stub side-effect cases and 7 single-load-site
  variants (2272 scripted writes), 14 store-site, load-site, stub-result, stub pre-store,
  aliasing and float-compare variants (0 not comparable ...), 27498 worker calls compared
  ..., 22 routines, 113 cases of 00191120 / 001B0C00 / 001FAD70 in their port modules,
  59 fail-stop API checks
test_area01_sys_reference: OK (5.6 s)      # real 5.7 s, user 9.6 s, sys 0.9 s (10.5 s CPU), load 54
$ EM_TEST_JOBS=4 python3 tools/test_area01_sys_reference.py                              # warm, twice
test_area01_sys_reference: OK (2.9 s)      # real 3.0 s, user 7.2 s, sys 0.8 s (7.9 s CPU), load 51
test_area01_sys_reference: OK (2.9 s)      # real 3.0 s, user 7.1 s, sys 0.8 s (7.8 s CPU), load 51
$ EM_TEST_FULL=1 EM_AREA01_SYS_GAPS=1 EM_TEST_JOBS=4 python3 tools/test_area01_sys_reference.py
  1A06A0 missing outcomes: 1A0804T
  1E3D90 missing outcomes: 1E3FACF
full-mode branch coverage asserted: every outcome taken except the 2 documented
  unreachable ones
mode full: 2,108 cases, 3,048 stub side-effect cases and 867 single-load-site variants
  (212188 scripted writes), 7421 store-site, load-site, stub-result, stub pre-store,
  aliasing and float-compare variants (282 not comparable ...; by kind, run / not
  comparable: alias 1072/249, float 356/6, fx 3048/3, load 1358/11, store 579/1, stub
  1008/12), 2644698 worker calls compared ..., 22 routines, 545 cases of 00191120 /
  001B0C00 / 001FAD70, 59 fail-stop API checks
test_area01_sys_reference: OK (235.8 s)    # real 235.9 s, user 910.3 s, sys 8.0 s (918.2 s CPU), load 21..65
```

The default run meets the ~10 s CPU target (about 8 s warm, 10.5 s cold including the two
compiles) with every pinned killing case in it. The full run costs about 918 s of CPU (861 s
in round 6: the round-7 cases and their variants). Not comparable in the full run: 282, the
279 of round 6 (reasons above) and 3 more stub pre-store variants ("fx" kind) of round-7
cases, whose reason was not classified.


The private builds go to `build/area01/sys/`: `area01_sys.dylib` with the store trace
(`store_trace.c`), and the bridge. They are reused while they are newer than the sources,
the headers in src/game and the test.

## 5. Decomp NEARMISS C that differed from the original

The oracle found the mismatches below. This lane does not edit the decomp; it reported them.
As of 2026-09-26, the decomp working tree has corrected bodies for all five files. The
corrections are uncommitted and are another lane's work. Each file's header says "Body
corrected 2026-09-25 against the original instructions". I re-read the current C for every
item:

- **func_00159B90.c.** Corrected in the working tree.
  - 001B0FD0, 00157CE0, 001BA1F0 and 001AFC10 now take the record p.
  - 00157CE0's second argument is e = p+0x1F0.
  - Sub-state 3 stores +5 = 1.
- **func_0015A2C0.c.** Corrected: 001E9E60 gets (self, halfword +0x0E).
- **func_0019CF50.c.** Corrected. The six bounds at 0x70003240 now use a halfword stride, the
  column table at 0x70003228 a word stride, and the cell list a halfword stride. Its header
  notes that the twin func_0019D770 still has the same three stride defects.
- **func_001A06A0.c.** Corrected: a list entry is skipped when the record's first byte is 0.
- **func_001E7D20.c.** Corrected.
  - The static 001E7C60 now stores only the grid heights and s+4.
  - The relaxation sum now carries a comment. The original uses the accumulator
    add/multiply-subtract forms. Under the measured model they are not always equal to the
    plain operations: when a product is infinite or NaN (an infinite height), the
    multiply-subtract gives -MAX where a separate multiply and subtract give MAX - MAX. The
    round-4 case `1E7D20 t relax inf` separates them, and the port keeps the accumulator
    form.
  - Outside area 0x13 the 001CB950 doubleword is 0x20048BA199422040; the old C
    sign-extended its low word (0xFFFFFFFF99422040). The header of func_001E7D20.c states the
    correction, and the port passes the correct value.

Until those corrections are committed, the committed decomp C still carries the old bodies.
The port translation in `em_area01_sys.c` follows the original instructions, not either
version of the C.

## 6. What is left

- **Wiring.** The module is unbound. A binder must supply:
  - the regions: the port's RAM image of the area, the scratchpad block, and a stack
    region under `sp`;
  - a worker that maps each callee address to its port module, or faults.
  Several callees already have port translations: the em_stream_lanes entries, the
  em_packet_chain builders 001CB5F0/001CB6B0/001CB760, 001CB950 in
  em_shadow_decal_original, 001B1470 in em_player_stage_workers, 001B12B0 in
  em_script_host_workers, and the SDK leaves 0011DE90/0011E2A8/0011E620/0011E748 in
  em_sdk_math_original. The rest are new AREA01 work in other lanes, or E-lane engine
  functions. The math lane's `em_area01_math_actor.c` already calls 0019B4C0 through its
  own worker, and that call can be bound to `em_area01_sys_0019B4C0`.
- **Live-state check.** Captured-state replays prove the routines as called. What they do
  not prove is that the AREA01 frame calls them with these arguments. That needs the
  route-level comparison once the area load exists (a per-frame whole-RAM replay of beats
  a01_00..a01_07).
- **UNDEFINED inputs.** The three inputs of section 2 stay fail-stop. The route never
  produced them (they depend on records or globals outside the captured values).
- **Outside the claim.** Nothing is claimed for the following:
  - any mutant outside the sets listed in section 4 ("What verified means"): the review
    sweeps did not converge (each new sweep found new survivors), so the kill counts cover
    those mutants only, not single-operation changes in general;
  - F20, which is open: it is equivalent only under section 2's stack limit, and the
    limit's premise (the writers of the actor words +0x14 that 001B1CA0 pushes) was not
    traced;
  - reads moved across a store outside the aliasing variants' four-store window, or where
    their single relocation leaves the stored and old values equal in the bits used (N06
    shows it) and no targeted case makes the records alias;
  - reads cached across a store made through a pointer that a stub returns: the aliasing
    variants relocate only pointer arguments and pointer words of the starting memory, never
    a stub result such as 001E7D20's 001CB5F0 blocks. T31 and T32 are killed by their
    targeted cases only (`1E7D20 r7 block over the area byte 6` / `13`);
  - EE-compare sites where the float-compare variants were built but cannot separate the EE
    compare from a host or bit compare: c.eq sites, where the special words differ only
    against a zero or saturated partner the variants do not arrange (T18), and sites whose
    effect shows only in a later iteration (T44). Both are killed by targeted cases only;
  - raw words that reach an EE compare through something other than lwc1 or lw + mtc1;
  - value ranges no case exercises. Many entry arguments and record fields are tested only
    at a few values: 00158590's a1 at 0, 1, -1 and (round 7) 0x00010000 / 0x7FFF0000; the
    001AA000 key words at 0x100, 0x200, 0x300, 0x10100, 0x10200 and (round 7) 0x201 /
    0x2FF; flag bytes such as 001A9E00's a1+0x0B at 0, 1 and (round 7) 2 / 0x80. A
    translation that is right on those values and wrong on others passes (T55, U13 and U14
    were such edits until round 7);
  - records in the stack below `sp` (section 2);
  - the 15 undefined-C edits of round 4;
  - the fail-stop contract parts listed as untested in section 4 ("Contracts");
  - inputs the test cannot compare: the full run's not-comparable variants (section 4 gives
    the count and the reasons). The native module's fail-stop there is a documented limit,
    not an original behaviour.

## Appendix: round-4 survivors of `em_area01_sys.c`

Round 5 adds one open item outside these lists: the review's F20 (001AA000 without the
second reads of the +0x30 words, lines 591-592), equivalent only under section 2's stack
limit (section 4, "Round 5"). Rounds 6 and 7 add no item here: the review sweeps'
survivors are killed or proven in the next appendix, and the module source is unchanged.

Line numbers are those of `src/game/em_area01_sys.c` as swept (unchanged this round).

### Rule-proven equivalents (by rule; line: the mutated line)

**R-ALREADY** (5):
- 627 WIDTH: `st16(r, base + 6, v1 & 0x7F);`
- 837 WIDTH: `st8(r, p + 0x56, v);`
- 1177 WIDTH: `st16(r, e + 0, 2);`
- 1277 WIDTH: `st16(r, e + 0xA, 0);`
- 1284 WIDTH: `st16(r, e + 0xA, 0);`

**R-ARGS** (33):
- 265 CRP: `callf(r, sp, 0x001C67E0u, 3, a, 3, f);`
- 265 CRP: `callf(r, sp, 0x001C67E0u, 4, a, 2, f);`
- 319 CRP: `callf(r, sp, 0x001B6F00u, 2, a, 2, f);`
- 319 CRP: `callf(r, sp, 0x001B6F00u, 3, a, 1, f);`
- 394 CRP: `c = callf(r, sp, FN_DOT3, 3, a, 0, NULL);`
- 676 CRP: `callf(r, sp, 0x001FBD50u, 3, a, 2, f);`
- 676 CRP: `callf(r, sp, 0x001FBD50u, 4, a, 1, f);`
- 907 CRP: `callf(r, sp, 0x001FBD50u, 3, a, 2, fl);`
- 907 CRP: `callf(r, sp, 0x001FBD50u, 4, a, 1, fl);`
- 1027 CRP: `return (int32_t)(uint32_t)callf(r, sp, 0x001B13F0u, 2, a, 2, f).v0;`
- 1027 CRP: `return (int32_t)(uint32_t)callf(r, sp, 0x001B13F0u, 3, a, 1, f).v0;`
- 1043 CRP: `return callf(r, sp, FN_APPROACH, 0, NULL, 4, f).f0;`
- 1267 CRP: `st16(r, b + 0xF4, (uint32_t)callf(r, sp, 0x001C64F0u, 1, a, 2, f).v0);`
- 1267 CRP: `st16(r, b + 0xF4, (uint32_t)callf(r, sp, 0x001C64F0u, 2, a, 1, f).v0);`
- 1331 CRP: `if ((callf(r, sp, 0x001B1630u, 0, NULL, 4, f).v0 & 0xFF) == 0) {`
- 1627 CRP: `callf(r, sp, FN_VSCALE, 2, a, 2, f);`
- 1627 CRP: `callf(r, sp, FN_VSCALE, 3, a, 1, f);`
- 1710 CRP: `callf(r, sp, 0x001CFAE0u, 3, a, 5, f);`
- 1710 CRP: `callf(r, sp, 0x001CFAE0u, 4, a, 4, f);`
- 1721 CRP: `callf(r, sp, 0x001CFBE0u, 6, a, 0, NULL);`
- 1731 CRP: `callf(r, sp, 0x0021B9A0u, 1, a, 3, f);`
- 1731 CRP: `callf(r, sp, 0x0021B9A0u, 2, a, 2, f);`
- 1840 CRP: `st32(r, 0x70003A20u, callf(r, sp, 0x001CD2B0u, 0, NULL, 5, f).f0);`
- 1877 CRP: `callf(r, sp, 0x001FC3C0u, 3, a, 3, f);`
- 1877 CRP: `callf(r, sp, 0x001FC3C0u, 4, a, 2, f);`
- 2120 CRP: `callf(r, sp, 0x001E8B90u, 1, a, 2, f);`
- 2120 CRP: `callf(r, sp, 0x001E8B90u, 2, a, 1, f);`
- 2253 CRP: `callv(r, sp, 0x001CB950u, 3, reg((int32_t)0x007635C0u), 0x1000, word, (-1));`
- 2253 CRP: `callv(r, sp, 0x001CB950u, 3, reg((int32_t)0x007635C0u), 0x1000, word, 1);`
- 2253 CRP: `callv(r, sp, 0x001CB950u, 4, reg((int32_t)0x007635C0u), 0x1000, word, 0);`
- 2255 CRP: `callv(r, sp, 0x001CB950u, 3, reg((int32_t)0x007635C0u), 0x1000, UINT64_C(0x20048BA199422040), (-1));`
- 2255 CRP: `callv(r, sp, 0x001CB950u, 3, reg((int32_t)0x007635C0u), 0x1000, UINT64_C(0x20048BA199422040), 1);`
- 2255 CRP: `callv(r, sp, 0x001CB950u, 4, reg((int32_t)0x007635C0u), 0x1000, UINT64_C(0x20048BA199422040), 0);`

**R-BOOL** (3):
- 472 CRP: `b0_words(r, 2);`
- 797 CRP: `f_158590(r, sp, p, 2, -2);`
- 803 CRP: `f_158590(r, sp, p, 2, -2);`

**R-BYTE-MASK** (1):
- 626 MASK: `st8(r, base + 5, (v1 & 0x180) ? 1 : 0);`

**R-CAST** (3):
- 629 CAST: `uint32_t m = 0x0024A8D0u + (int32_t)(((int32_t)w32(r, p + 0x10) >> 8) * 0xC);`
- 1265 CAST: `a[0] = reg((uint32_t)e);`
- 1267 CAST: `st16(r, b + 0xF4, (int32_t)callf(r, sp, 0x001C64F0u, 1, a, 1, f).v0);`

**R-CLAMP** (10):
- 2081 CRP: `!em_ee_c_lt_bits(x, 0x44688001u))`
- 2081 EEC: `!em_ee_c_le_bits(x, 0x44688000u))`
- 2127 CRP: `uint32_t im1 = i == 0 ? 0 : i - 1, ip1 = i + 1 >= 0x1F ? 0x1F : i + 1;`
- 2127 CRP: `uint32_t im1 = i == 0 ? 0 : i - 1, ip1 = i + 2 >= 0x20 ? 0x1F : i + 1;`
- 2127 ROR: `uint32_t im1 = i == 0 ? 0 : i - 1, ip1 = i + 1 == 0x20 ? 0x1F : i + 1;`
- 2129 CRP: `uint32_t jm1 = j == 0 ? 0 : j - 1, jp1 = j + 1 >= 0x1F ? 0x1F : j + 1;`
- 2129 CRP: `uint32_t jm1 = j == 0 ? 0 : j - 1, jp1 = j + 2 >= 0x20 ? 0x1F : j + 1;`
- 2129 ROR: `uint32_t jm1 = j == 0 ? 0 : j - 1, jp1 = j + 1 == 0x20 ? 0x1F : j + 1;`
- 2152 ROR: `int32_t cm = col - 1 < 0 ? 0 : col - 1, cp = col + 1 != 0x20 ? col + 1 : 0x1F;`
- 2153 ROR: `int32_t rm = row - 1 < 0 ? 0 : row - 1, rp = row + 1 != 0x20 ? row + 1 : 0x1F;`

**R-DAZ** (1):
- 407 CRP: `st32(r, 0x700038B4u, 1);`

**R-DEAD-INIT** (21):
- 1373 CRP: `uint32_t s5, s6, hit = 0, keep[3] = {(-1), 0, 0};`
- 1373 CRP: `uint32_t s5, s6, hit = 0, keep[3] = {0, (-1), 0};`
- 1373 CRP: `uint32_t s5, s6, hit = 0, keep[3] = {0, 0, (-1)};`
- 1373 CRP: `uint32_t s5, s6, hit = 0, keep[3] = {0, 0, 1};`
- 1373 CRP: `uint32_t s5, s6, hit = 0, keep[3] = {0, 1, 0};`
- 1373 CRP: `uint32_t s5, s6, hit = 0, keep[3] = {1, 0, 0};`
- 1374 CRP: `int32_t kept[6], best, lo = (-1), hi = 0, which = -1, i;`
- 1374 CRP: `int32_t kept[6], best, lo = 0, hi = (-1), which = -1, i;`
- 1374 CRP: `int32_t kept[6], best, lo = 0, hi = 0, which = -2, i;`
- 1374 CRP: `int32_t kept[6], best, lo = 0, hi = 1, which = -1, i;`
- 1374 CRP: `int32_t kept[6], best, lo = 1, hi = 0, which = -1, i;`
- 1742 CRP: `uint32_t s3 = p + 0x1F0, st, key, local, rows = (-1), pair = 0, stepA = 0, stepB = 0, stepC = 0;`
- 1742 CRP: `uint32_t s3 = p + 0x1F0, st, key, local, rows = 0, pair = (-1), stepA = 0, stepB = 0, stepC = 0;`
- 1742 CRP: `uint32_t s3 = p + 0x1F0, st, key, local, rows = 0, pair = 0, stepA = (-1), stepB = 0, stepC = 0;`
- 1742 CRP: `uint32_t s3 = p + 0x1F0, st, key, local, rows = 0, pair = 0, stepA = 0, stepB = (-1), stepC = 0;`
- 1742 CRP: `uint32_t s3 = p + 0x1F0, st, key, local, rows = 0, pair = 0, stepA = 0, stepB = 0, stepC = (-1);`
- 1742 CRP: `uint32_t s3 = p + 0x1F0, st, key, local, rows = 0, pair = 0, stepA = 0, stepB = 0, stepC = 1;`
- 1742 CRP: `uint32_t s3 = p + 0x1F0, st, key, local, rows = 0, pair = 0, stepA = 0, stepB = 1, stepC = 0;`
- 1742 CRP: `uint32_t s3 = p + 0x1F0, st, key, local, rows = 0, pair = 0, stepA = 1, stepB = 0, stepC = 0;`
- 1742 CRP: `uint32_t s3 = p + 0x1F0, st, key, local, rows = 0, pair = 1, stepA = 0, stepB = 0, stepC = 0;`
- 1742 CRP: `uint32_t s3 = p + 0x1F0, st, key, local, rows = 1, pair = 0, stepA = 0, stepB = 0, stepC = 0;`

**R-DECL** (1):
- 1373 CRP: `uint32_t s5, s6, hit = 0, keep[4] = {0, 0, 0};`

**R-EE-DIV** (2):
- 2078 EEF: `x = em_ee_add_bits(w32(r, s + 0x0), host_div(em_ee_mul_bits(w32(r, s + 0x30), fj), 0x41F00000u));`
- 2079 EEF: `z = em_ee_add_bits(w32(r, s + 0x8), host_div(em_ee_mul_bits(w32(r, s + 0x34), fi), 0x41F00000u));`

**R-EE-FINITE** (1):
- 546 EEF: `e = host_neg(e);`

**R-EE-SQUARES** (2):
- 537 EEF: `d = fcall1(r, sp, FN_SQRT, split_madd(em_ee_mula_bits(dx, dx), dz, dz));`
- 583 EEF: `d = fcall1(r, sp, FN_SQRT, split_madd(em_ee_mula_bits(dx, dx), dz, dz));`

**R-EXACT** (10):
- 1033 EEF: `uint32_t x = em_ee_mul_bits(0x40C90FDBu, host_cvt((uint32_t)v));`
- 1034 EEF: `return fcall1(r, sp, FN_WRAP, host_div(x, 0x43800000u));`
- 1736 EEF: `uint32_t q = em_ee_div_bits(host_cvt((uint32_t)(sra32(v, 16) & 0xFFFF)), 0x477FFF00u);`
- 1736 EEF: `uint32_t q = host_div(em_ee_cvt_s_w_bits((uint32_t)(sra32(v, 16) & 0xFFFF)), 0x477FFF00u);`
- 1753 EEF: `uint32_t f = host_div(em_ee_cvt_s_w_bits((uint32_t)CALL0(r, sp, FN_RAND)), 0x4F000000u);`
- 1967 EEF: `return host_mul(0x30000000u, em_ee_cvt_s_w_bits((uint32_t)CALL0(r, sp, FN_RAND)));`
- 2074 EEF: `uint32_t fi = em_ee_cvt_s_w_bits(i), vi = host_div(fi, 0x42000000u);`
- 2074 EEF: `uint32_t fi = host_cvt(i), vi = em_ee_div_bits(fi, 0x42000000u);`
- 2077 EEF: `uint32_t fj = host_cvt(j), x, z;`
- 2087 EEF: `st32(r, c1 + 0x4060, host_div(fj, 0x42000000u));`

**R-EXACT-HALF** (1):
- 2162 EEF: `k = host_mul(k, F_HALF);`

**R-IDENT-ARITH** (1):
- 1288 AOR: `st8(r, e - 0, 2);`

**R-KEY** (16):
- 1774 AOR: `if (((u8(r, 0x00810700u) << 8) - u8(r, 0x00810701u)) == 0x100 &&`
- 1774 WIDTH: `if (((s8(r, 0x00810700u) << 8) + u8(r, 0x00810701u)) == 0x100 &&`
- 1774 WIDTH: `if (((u16(r, 0x00810700u) << 8) + u8(r, 0x00810701u)) == 0x100 &&`
- 1774 WIDTH: `if (((u8(r, 0x00810700u) << 8) + s8(r, 0x00810701u)) == 0x100 &&`
- 1778 CRP: `key = (u8(r, 0x00810700u) << 7) + u8(r, 0x00810701u);`
- 1778 CRP: `key = (u8(r, 0x00810700u) << 9) + u8(r, 0x00810701u);`
- 1778 WIDTH: `key = (s8(r, 0x00810700u) << 8) + u8(r, 0x00810701u);`
- 1778 WIDTH: `key = (u8(r, 0x00810700u) << 8) + s8(r, 0x00810701u);`
- 1822 WIDTH: `key = (s8(r, 0x00810700u) << 8) + u8(r, 0x00810701u);`
- 1822 WIDTH: `key = (u8(r, 0x00810700u) << 8) + s8(r, 0x00810701u);`
- 1857 WIDTH: `key = (s8(r, 0x00810700u) << 8) + u8(r, 0x00810701u);`
- 1857 WIDTH: `key = (u8(r, 0x00810700u) << 8) + s8(r, 0x00810701u);`
- 2209 CRP: `key = (u8(r, 0x00810700u) << 7) + u8(r, 0x00810701u);`
- 2209 CRP: `key = (u8(r, 0x00810700u) << 9) + u8(r, 0x00810701u);`
- 2209 WIDTH: `key = (s8(r, 0x00810700u) << 8) + u8(r, 0x00810701u);`
- 2209 WIDTH: `key = (u8(r, 0x00810700u) << 8) + s8(r, 0x00810701u);`

**R-KIND-BITS** (2):
- 1551 MASK: `shape += (s16(r, shape) & 0x1800) ? 0x2C : 0x18;`
- 1554 MASK: `shape += (s16(r, shape) & 0x1800) ? 0x24 : 0x14;`

**R-LOOP-NE** (4):
- 1614 ROR: `for (i = 0; i != 3; i++) {`
- 1641 ROR: `for (i = 0; i != 3; i++) {`
- 2073 ROR: `for (i = 0; i != 0x20; i++) {`
- 2075 ROR: `for (j = 0; j != 0x20; j++) {`

**R-LOW-STORE** (28):
- 286 WIDTH: `st16(r, a0 + 0x36, (uint32_t)u16(r, a0 + 0x34));`
- 768 WIDTH: `st8(r, p + 5, s8(r, p + 5) + 1);`
- 768 WIDTH: `st8(r, p + 5, u16(r, p + 5) + 1);`
- 838 WIDTH: `st8(r, counter, u16(r, counter) + 1);`
- 891 WIDTH: `st8(r, p + 5, s8(r, p + 5) + 1);`
- 891 WIDTH: `st8(r, p + 5, u16(r, p + 5) + 1);`
- 939 WIDTH: `st16(r, p + 0x2E, s16(r, p + 0x2E) + 1);`
- 1082 WIDTH: `st8(r, e + 5, u16(r, e + 5) + 1);`
- 1088 WIDTH: `st8(r, e + 5, u16(r, e + 5) + 1);`
- 1105 WIDTH: `st16(r, b + 0xD0, (uint32_t)(u16(r, b + 0xD0) + 1));`
- 1113 WIDTH: `st8(r, e + 5, u16(r, e + 5) + 1);`
- 1128 WIDTH: `st8(r, e + 5, u16(r, e + 5) + 1);`
- 1133 WIDTH: `st16(r, b + 0xD0, (uint32_t)u16(r, 0x00242EB6u + (uint32_t)(c * 2)));`
- 1157 WIDTH: `st16(r, b + 0xD0, (uint32_t)u16(r, 0x00242EB0u + (uint32_t)(c * 2)));`
- 1158 WIDTH: `st8(r, e + 6, s8(r, e + 6) + 1);`
- 1158 WIDTH: `st8(r, e + 6, u16(r, e + 6) + 1);`
- 1170 WIDTH: `st16(r, b + 0xD0, (uint32_t)u16(r, 0x00242EBCu + (uint32_t)(c * 2)));`
- 1217 WIDTH: `st8(r, e + 5, s8(r, e + 5) + 1);`
- 1217 WIDTH: `st8(r, e + 5, u16(r, e + 5) + 1);`
- 1308 WIDTH: `st8(r, e + 5, s8(r, e + 5) + 1);`
- 1308 WIDTH: `st8(r, e + 5, u16(r, e + 5) + 1);`
- 1316 WIDTH: `st16(r, e + 0x28, (uint32_t)u16(r, 0x00275380u + (uint32_t)(c * 2)));`
- 1409 WIDTH: `st16(r, 0x70003B86u, (uint32_t)u16(r, 0x70003240u + (uint32_t)i * 2));`
- 1410 WIDTH: `st16(r, 0x70003B88u, (uint32_t)u16(r, column));`
- 1412 WIDTH: `st16(r, 0x70003B86u, (uint32_t)u16(r, column));`
- 1413 WIDTH: `st16(r, 0x70003B88u, (uint32_t)u16(r, 0x70003240u + (uint32_t)i * 2));`
- 1414 WIDTH: `st16(r, 0x70003B88u, (uint32_t)(u16(r, 0x70003B88u) + 1));`
- 1980 WIDTH: `st8(r, p + 5, u16(r, p + 5) + 1);`

**R-LOW16** (4):
- 1146 WIDTH: `c = u16(r, b + 0xD0) - 1;`
- 1162 WIDTH: `c = u16(r, b + 0xD0) - 1;`
- 1186 WIDTH: `c = u16(r, b + 0xD0) - 1;`
- 1320 WIDTH: `c = u16(r, e + 0x28) - 1;`

**R-LOWBITS-SUM** (2):
- 1063 WIDTH: `int32_t c = s8(r, 0x70003B8Au);`
- 1064 WIDTH: `return ((u16(r, 0x70003B68u) + (uint32_t)c) & 0x3F) == 0;`

**R-MASK-LOW** (19):
- 310 WIDTH: `if (!(s8(r, a0 + 0xB) & 4))`
- 310 WIDTH: `if (!(u16(r, a0 + 0xB) & 4))`
- 351 WIDTH: `if (s8(r, a0 + 0xB) & 1) {`
- 351 WIDTH: `if (u16(r, a0 + 0xB) & 1) {`
- 556 WIDTH: `if (!(s8(r, a0) & 4))`
- 556 WIDTH: `if (!(u16(r, a0) & 4))`
- 625 WIDTH: `v1 = s8(r, p + 0x10);`
- 625 WIDTH: `v1 = u16(r, p + 0x10);`
- 835 WIDTH: `uint32_t row = u16(r, 0x70003B68u) & 3;`
- 836 WIDTH: `uint32_t v = u8(r, table + row * 8 + (u16(r, counter) & 7));`
- 900 WIDTH: `if ((u16(r, 0x70003B64u) & 0x7F) == 0) {`
- 1075 WIDTH: `if (u16(r, e + 0xA) & 1) {`
- 1099 WIDTH: `if (u16(r, e + 0xA) & 1) {`
- 1516 WIDTH: `if ((u16(r, e + 2) & 0x1F) != 4)`
- 1545 WIDTH: `shape += (u16(r, shape) & 0x800) ? 0x24 + n * 0x30 : 0x14 + n * 0x18;`
- 1551 WIDTH: `shape += (u16(r, shape) & 0x800) ? 0x2C : 0x18;`
- 1554 WIDTH: `shape += (u16(r, shape) & 0x800) ? 0x24 : 0x14;`
- 1567 WIDTH: `st16(r, 0x700030CAu, (s16(r, 0x700030CAu) & 0xFF00) | u8(r, e + 0x54));`
- 1629 WIDTH: `st16(r, 0x7000324Eu, u16(r, a0 + 2) & 0x1F);`

**R-MASK-USE** (1):
- 1234 WIDTH: `t = u16(r, 0x70003B8Du);`

**R-MINMAX** (1):
- 1491 EEC: `if (em_ee_c_lt_bits(va, vb)) {`

**R-NEGZERO** (1):
- 545 EEC: `if (em_ee_c_le_bits(e, F_ZERO))`

**R-OVERWRITE** (18):
- 280 WIDTH: `st16(r, a0 + 4, 2);`
- 281 WIDTH: `st16(r, a0 + 5, 0);`
- 282 WIDTH: `st16(r, a0 + 6, 0);`
- 626 WIDTH: `st16(r, base + 5, (v1 & 0x80) ? 1 : 0);`
- 1053 WIDTH: `st16(r, e + 5, 8);`
- 1054 WIDTH: `st16(r, e + 6, 0);`
- 1128 WIDTH: `st16(r, e + 5, u8(r, e + 5) + 1);`
- 1178 WIDTH: `st16(r, e + 4, 3);`
- 1179 WIDTH: `st16(r, e + 5, 0);`
- 1289 WIDTH: `st16(r, e + 4, 4);`
- 1312 WIDTH: `st16(r, e + 4, 4);`
- 1333 WIDTH: `st16(r, e + 4, 1);`
- 1334 WIDTH: `st16(r, e + 5, 0);`
- 1335 WIDTH: `st16(r, e + 6, 0);`
- 1409 WIDTH: `st32(r, 0x70003B86u, (uint32_t)s16(r, 0x70003240u + (uint32_t)i * 2));`
- 1412 WIDTH: `st32(r, 0x70003B86u, (uint32_t)s16(r, column));`
- 2139 SDL-store: `(void)0;`
- 2139 WIDTH: `st16(r, v, d1);`

**R-RANGE** (7):
- 344 WIDTH: `if ((int32_t)(int16_t)(u8(r, 0x00810C63u) * 30) == u16(r, 0x00810CB4u)) {`
- 628 ROR: `if (u8(r, base + 5) >= 1) {`
- 675 ROR: `f[0] = op >= 1 ? w32(r, a2 + 0x20) : 0x43960000u;`
- 1097 WIDTH: `f_1287F0(r, sp, e, b, table_2F20(r, s8(r, e + 0xD)), 0x40800000u);`
- 1247 ROR: `st8(r, e + 0xD, t <= 4 ? 3 : 8);`
- 1445 WIDTH: `kind = u16(r, 0x70003B88u);`
- 1980 CRP: `st8(r, p + 5, u8(r, p + 4) + 1);`

**R-RESULT-01** (6):
- 1081 ROR: `if (near_test(r, sp, e, 0x42C80000u) > 0) {`
- 1087 ROR: `if (near_test(r, sp, e, radius) > 0) {`
- 1104 ROR: `if (near_test(r, sp, e, 0x43160000u) <= 0) {`
- 1112 ROR: `if (near_test(r, sp, e, radius) > 0) {`
- 1280 ROR: `if (every_64th(r) && f_1B0D80(r, e) > 0)`
- 1326 ROR: `if (near_test(r, sp, e, 0x42C80000u) <= 0) {`

**R-RMW-BYTE** (1):
- 1567 WIDTH: `st8(r, 0x700030CAu, (u16(r, 0x700030CAu) & 0xFF00) | u8(r, e + 0x54));`

**R-SHIFT-FILL** (2):
- 1659 CRP: `return v < (-1) ? (int32_t)~(~(uint32_t)v >> n) : (int32_t)((uint32_t)v >> n);`
- 1659 ROR: `return (0) ? (int32_t)~(~(uint32_t)v >> n) : (int32_t)((uint32_t)v >> n);`

**R-SIGN-EQ** (19):
- 277 WIDTH: `if (s16(r, a0 + 0x36) == 0 && s8(r, 0x0081080Fu) == 0)`
- 277 WIDTH: `if (u16(r, a0 + 0x36) == 0 && u8(r, 0x0081080Fu) == 0)`
- 285 WIDTH: `if (s8(r, 0x0081080Fu) != 0)`
- 320 WIDTH: `if (s8(r, a0 + 3) == 0x38) {`
- 321 WIDTH: `if (s8(r, 0x00810C7Fu) == 0) {`
- 469 WIDTH: `b0_words(r, s8(r, p + 0xB) != 0);`
- 512 WIDTH: `if (s8(r, a1 + 0xB) != 0 && u8(r, 0x00810707u) != 1 && u8(r, a0) == 1) {`
- 512 WIDTH: `if (u8(r, a1 + 0xB) != 0 && s8(r, 0x00810707u) != 1 && u8(r, a0) == 1) {`
- 512 WIDTH: `if (u8(r, a1 + 0xB) != 0 && u8(r, 0x00810707u) != 1 && s8(r, a0) == 1) {`
- 549 WIDTH: `if (s8(r, a1 + 3) != 0)`
- 628 WIDTH: `if (s8(r, base + 5) == 1) {`
- 702 WIDTH: `if (s8(r, 0x00810700u) != 0x13)`
- 839 WIDTH: `if (u16(r, p + 0x56) == 1) {`
- 887 WIDTH: `if (s8(r, p + 0xA) != 0) {`
- 911 WIDTH: `if (s8(r, p + 0xA) != 0) {`
- 932 WIDTH: `if (s8(r, p + 0xA) == 0) {`
- 1271 WIDTH: `if (s8(r, e + 1) != 0) {`
- 1272 WIDTH: `if (u8(r, b + 0xFA) != 0)`
- 1287 WIDTH: `if (s8(r, b + 0xE0) != 0) {`

**R-SIGN-LOCAL** (10):
- 452 WIDTH: `t = s8(r, p + 4);`
- 508 WIDTH: `t = s8(r, a1 + 0xD);`
- 704 WIDTH: `w = s8(r, 0x00810702u);`
- 870 WIDTH: `link = u16(r, p + 0x56);`
- 882 WIDTH: `link = u16(r, p + 0x56);`
- 1214 WIDTH: `t = s8(r, e + 5);`
- 1225 WIDTH: `t = s8(r, e + 0xD);`
- 1234 WIDTH: `t = s8(r, 0x70003B8Du);`
- 1244 WIDTH: `t = s8(r, e + 0xD);`
- 1780 WIDTH: `uint32_t sub = s8(r, 0x00810702u);`

**R-SWITCH-DEFAULT** (5):
- 1255 CRP: `} else if (u8(r, e + 4) < 9) {`
- 1255 CRP: `} else if (u8(r, e + 5) < 10) {`
- 1255 ROR: `} else if (u8(r, e + 5) != 9) {`
- 1255 ROR: `} else if (u8(r, e + 5) <= 9) {`
- 1255 WIDTH: `} else if (s8(r, e + 5) < 9) {`

**R-SWITCH-SIGN** (3):
- 467 WIDTH: `switch (s16(r, p + 0x2E)) {`
- 1212 WIDTH: `switch (s8(r, e + 4)) {`
- 1298 WIDTH: `switch (s8(r, e + 5)) {`

**R-TRIM** (2):
- 2105 CRP: `level_fill(r, s, p, 0x43040000u, 0x008107F6u, 0x3DCCCCCCu);`
- 2105 CRP: `level_fill(r, s, p, 0x43040000u, 0x008107F6u, 0x3DCCCCCEu);`

**R-UNSIGNED** (2):
- 1271 ROR: `if (u8(r, e + 1) > 0) {`
- 1287 ROR: `if (u8(r, b + 0xE0) > 0) {`

### Undefined C (not verified: C gives these edits no meaning)

- 392 CRP: `a[(-1)] = reg((int32_t)local);`
- 392 CRP: `a[1] = reg((int32_t)local);`
- 674 CRP: `a[3] = 0;`
- 1265 CRP: `a[(-1)] = reg((int32_t)e);`
- 1265 CRP: `a[1] = reg((int32_t)e);`
- 1400 CRP: `for (i = (-1); i < 6; i++)`
- 1400 CRP: `for (i = 0; i < 7; i++)`
- 1400 ROR: `for (i = 0; i <= 6; i++)`
- 1624 CRP: `a[(-1)] = reg((int32_t)local);`
- 1624 CRP: `a[1] = reg((int32_t)local);`
- 1625 CRP: `a[0] = reg((int32_t)local);`
- 1625 CRP: `a[2] = reg((int32_t)local);`
- 1720 CRP: `a[5] = 0;`
- 2119 CRP: `f[0] = em_ee_add_bits(0x3DCCCCCCu, em_ee_mul_bits(0x3F8CCCCDu, rnd_unit(r, sp)));`
- 2119 CRP: `f[0] = em_ee_add_bits(0x3DCCCCCEu, em_ee_mul_bits(0x3F8CCCCDu, rnd_unit(r, sp)));`

### Identical object (line: the mutated lines)

- 257: `if (s16(r, a1 + 0xF8) == (uint32_t)(int16_t)a2)`
- 259: `st16(r, a1 + 0xF8, (int32_t)a2);`
- 260: `a[0] = reg((uint32_t)a0);`
- 277: `if (s16(r, a0 + 0x36) == 0 && u8(r, 0x0081080Fu) <= 0)`
- 279: `st8(r, a0 - 0, 3);`
- 285: `if (u8(r, 0x0081080Fu) > 0)`
- 286: `st16(r, a0 + 0x36, (int32_t)s16(r, a0 + 0x34));`
- 316: `a[0] = reg((uint32_t)a0);`
- 317: `a[1] = reg((uint32_t)0x700038A0u);`
- 321: `if (u8(r, 0x00810C7Fu) <= 0) {`
- 344: `if ((int32_t)(uint16_t)(u8(r, 0x00810C63u) * 30) == s16(r, 0x00810CB4u)) {`; `if ((uint32_t)(int16_t)(u8(r, 0x00810C63u) * 30) == s16(r, 0x00810CB4u)) {`
- 392: `a[0] = reg((uint32_t)local);`
- 393: `a[1] = reg((uint32_t)(p + 0xF0));`
- 457: `if (t <= 0) {`
- 465: `if (t > 1)`
- 469: `b0_words(r, u8(r, p + 0xB) > 0);`
- 509: `if (t <= 0) {`
- 512: `if (u8(r, a1 + 0xB) > 0 && u8(r, 0x00810707u) != 1 && u8(r, a0) == 1) {`
- 516: `} else if (t <= 1) {`
- 549: `if (u8(r, a1 + 3) > 0)`
- 672: `a[0] = reg((uint32_t)a0);`
- 673: `a[1] = reg((uint32_t)w32(r, a2 + 0x18));`
- 732: `st = s8(r, p + 4);`
- 741: `if (st <= 0) {`
- 748: `st8(r, p - 0, 1);`
- 756: `if (st > 1)`
- 758: `switch (s8(r, p + 5)) {`
- 760: `int32_t v = f_157CE0(r, sp, p, (uint32_t)e);`
- 761: `if (v > 0) {`
- 767: `else if (v >= 3)`
- 781: `st8(r, p - 0, 1);`
- 836: `uint32_t v = u8(r, table + row * 8 + (s8(r, counter) & 7));`
- 838: `st8(r, counter, s8(r, counter) + 1);`
- 850: `st = s8(r, p + 4);`
- 855: `if (st <= 0) {`
- 856: `tbl = 0x00248120u + (int32_t)(s16(r, p + 0x54) * 20);`
- 860: `st8(r, p - 0, 1);`
- 863: `st32(r, p + 0x1F4, (int32_t)CALL0(r, sp, FN_RAND));`
- 879: `if (st > 1)`
- 884: `uint32_t sub = s8(r, p + 5);`
- 885: `if (sub <= 0) {`
- 887: `if (u8(r, p + 0xA) > 0) {`
- 899: `} else if (sub <= 1) {`
- 900: `if ((w32(r, 0x70003B64u) & 0x7F) <= 0) {`
- 903: `a[0] = reg((uint32_t)p);`
- 911: `if (u8(r, p + 0xA) > 0) {`
- 925: `uint32_t sub = s8(r, p + 5);`
- 931: `} else if (sub <= 0) {`
- 932: `if (u8(r, p + 0xA) <= 0) {`
- 940: `if ((uint32_t)u16(r, p + 0x2E) >= 4) {`
- 947: `st32(r, p + 0x20, w32(r, 0x002481F0u + (int32_t)(v * 4)));`
- 1024: `a[0] = reg((uint32_t)E_POINT);`
- 1025: `a[1] = reg((uint32_t)(e + 0xB0));`
- 1027: `return (int32_t)(int32_t)callf(r, sp, 0x001B13F0u, 2, a, 1, f).v0;`; `return (uint32_t)(uint32_t)callf(r, sp, 0x001B13F0u, 2, a, 1, f).v0;`
- 1033: `uint32_t x = em_ee_mul_bits(0x40C90FDBu, em_ee_cvt_s_w_bits((int32_t)v));`
- 1063: `int32_t c = u16(r, 0x70003B8Au);`
- 1064: `return ((w32(r, 0x70003B68u) + (int32_t)c) & 0x3F) == 0;`; `return ((w32(r, 0x70003B68u) + (uint32_t)c) & 0x3F) <= 0;`
- 1070: `switch (s8(r, e + 5)) {`
- 1075: `if (s8(r, e + 0xA) & 1) {`
- 1080: `if ((uint32_t)u8(r, e + 0xD) < 4) {`
- 1082: `st8(r, e + 5, s8(r, e + 5) + 1);`
- 1086: `uint32_t radius = ((uint32_t)w32(r, b + 0xE4) >> 8) == 1 ? 0x41A00000u : 0x42200000u;`
- 1088: `st8(r, e + 5, s8(r, e + 5) + 1);`
- 1092: `if (u8(r, e + 1) > 0)`; `if (s8(r, e + 1) != 0)`
- 1099: `if (s8(r, e + 0xA) & 1) {`
- 1105: `st16(r, b + 0xD0, (int32_t)(s16(r, b + 0xD0) + 1));`
- 1111: `radius = ((uint32_t)w32(r, b + 0xE4) >> 8) == 1 ? 0x41200000u : 0x41C00000u;`
- 1113: `st8(r, e + 5, s8(r, e + 5) + 1);`
- 1117: `if ((uint32_t)u8(r, e + 5) < 2 && u8(r, e + 1) != 0)`; `if ((int32_t)u8(r, e + 5) < 2 && u8(r, e + 1) > 0)`; `if ((int32_t)u8(r, e + 5) < 2 && s8(r, e + 1) != 0)`
- 1122: `f_1287F0(r, sp, e, b, table_2F20(r, kind & 0x1FF), 0x40800000u);`; `f_1287F0(r, sp, e, b, table_2F20(r, kind & 0x7F), 0x40800000u);`; `f_1287F0(r, sp, e, b, table_2F20(r, kind & 0xBF), 0x40800000u);`; `f_1287F0(r, sp, e, b, table_2F20(r, kind & 0xDF), 0x40800000u);`; `f_1287F0(r, sp, e, b, table_2F20(r, kind & 0xEF), 0x40800000u);`
- 1128: `st8(r, e + 5, s8(r, e + 5) + 1);`
- 1133: `st16(r, b + 0xD0, (int32_t)s16(r, 0x00242EB6u + (uint32_t)(c * 2)));`; `st16(r, b + 0xD0, (uint32_t)s16(r, 0x00242EB6u + (int32_t)(c * 2)));`
- 1139: `switch (s8(r, e + 6)) {`
- 1147: `st16(r, b + 0xD0, (int32_t)c);`
- 1148: `if ((uint16_t)c == 0) {`
- 1157: `st16(r, b + 0xD0, (int32_t)s16(r, 0x00242EB0u + (uint32_t)(c * 2)));`; `st16(r, b + 0xD0, (uint32_t)s16(r, 0x00242EB0u + (int32_t)(c * 2)));`
- 1163: `st16(r, b + 0xD0, (int32_t)c);`
- 1164: `if ((uint16_t)c == 0) {`
- 1170: `st16(r, b + 0xD0, (int32_t)s16(r, 0x00242EBCu + (uint32_t)(c * 2)));`; `st16(r, b + 0xD0, (uint32_t)s16(r, 0x00242EBCu + (int32_t)(c * 2)));`
- 1176: `if (u8(r, e + 1) <= 0) {`; `if (s8(r, e + 1) == 0) {`
- 1177: `st8(r, e - 0, 2);`
- 1187: `st16(r, b + 0xD0, (int32_t)c);`
- 1188: `if ((uint16_t)c == 0) {`
- 1215: `if (t <= 0) {`
- 1222: `} else if (t <= 1) {`
- 1227: `st8(r, e - 0, 1);`
- 1235: `if (t != 0 && (uint32_t)(t & 0xFF) >= 2)`; `if (t != (-1) && (int32_t)(t & 0xFF) >= 2)`; `if (t != 0 && (int32_t)(t & 0xFE) >= 2)`; `if (t != 1 && (int32_t)(t & 0xFF) >= 2)`; `if (t != 0 && (int32_t)(t & 0x1FF) >= 2)`; `if ((1) && (int32_t)(t & 0xFF) >= 2)`; `if (t > 0 && (int32_t)(t & 0xFF) >= 2)`
- 1239: `if (every_64th(r) && f_1B0D80(r, e) > 0)`
- 1242: `if (f_128B80(r, sp, e, b) <= 0) {`
- 1246: `if (t == 4 || t >= 9) {`
- 1250: `st16(r, b + 0xD0, (int32_t)((c & 0x30) + 0x3C));`
- 1253: `st8(r, e - 0, 1);`
- 1260: `if (s8(r, e + 5) != 8 && CALL3(r, sp, 0x001C2770u, e, b, 0) == 0)`
- 1316: `st16(r, e + 0x28, (int32_t)s16(r, 0x00275380u + (uint32_t)(c * 2)));`; `st16(r, e + 0x28, (uint32_t)s16(r, 0x00275380u + (int32_t)(c * 2)));`
- 1321: `st16(r, e + 0x28, (int32_t)c);`
- 1322: `if ((uint16_t)c == 0)`
- 1331: `if ((callf(r, sp, 0x001B1630u, 0, NULL, 3, f).v0 & 0xFF) <= 0) {`
- 1332: `st8(r, e - 0, 1);`
- 1400: `for (i = 0; i != 6; i++)`
- 1401: `kept[i] = s16(r, 0x70003240u + (int32_t)i * 2);`
- 1404: `best = (uint32_t)w32(r, 0x7000320Cu);`
- 1405: `for (i = 0; i != 6; i++) {`
- 1406: `uint32_t column = w32(r, 0x70003228u + (int32_t)i * 4) + (uint32_t)(kept[i] * 2);`; `uint32_t column = w32(r, 0x70003228u + (uint32_t)i * 4) + (int32_t)(kept[i] * 2);`
- 1409: `st16(r, 0x70003B86u, (int32_t)s16(r, 0x70003240u + (uint32_t)i * 2));`; `st16(r, 0x70003B86u, (uint32_t)s16(r, 0x70003240u + (int32_t)i * 2));`
- 1410: `st16(r, 0x70003B88u, (int32_t)s16(r, column));`
- 1412: `st16(r, 0x70003B86u, (int32_t)s16(r, column));`
- 1413: `st16(r, 0x70003B88u, (int32_t)s16(r, 0x70003240u + (uint32_t)i * 2));`; `st16(r, 0x70003B88u, (uint32_t)s16(r, 0x70003240u + (int32_t)i * 2));`
- 1414: `st16(r, 0x70003B88u, (int32_t)(s16(r, 0x70003B88u) + 1));`
- 1418: `if ((int32_t)((int32_t)b - (uint32_t)a) < best) {`; `if ((int32_t)((uint32_t)b - (int32_t)a) < best) {`
- 1419: `best = (int32_t)((int32_t)b - (uint32_t)a);`; `best = (int32_t)((uint32_t)b - (int32_t)a);`; `best = (uint32_t)((uint32_t)b - (uint32_t)a);`
- 1427: `node = w32(r, 0x70003210u + (int32_t)which * 4) + (uint32_t)(lo * 2);`; `node = w32(r, 0x70003210u + (uint32_t)which * 4) + (int32_t)(lo * 2);`
- 1429: `uint32_t face = w32(r, 0x70003208u) + ((int32_t)s16(r, node) << 6);`
- 1452: `for (k = 0; k != 3; k++) {`
- 1459: `if (hit > 0) {`
- 1462: `for (k = 0; k != 3; k++)`
- 1514: `if (u8(r, e) <= 0)`; `if (s8(r, e) == 0)`
- 1516: `if ((s8(r, e + 2) & 0x1F) != 4)`
- 1520: `idx = (uint32_t)((u16(r, e + 0xE) >> 8) & 0xFF);`; `idx = (int32_t)((u16(r, e + 0xE) >> 8) & 0x1FF);`; `idx = (int32_t)((s16(r, e + 0xE) >> 8) & 0xFF);`
- 1521: `if (idx >= 0xFF)`
- 1524: `off = w32(r, base + (int32_t)idx * 4 + 4);`
- 1525: `if (off <= 0)`
- 1527: `if (!(idx < s16(r, 0x7000324Cu)) || idx < (-1))`; `if (!(idx < s16(r, 0x7000324Cu)) || (0))`
- 1530: `if (em_ee_c_lt_bits(maxx, w32(r, box - 0x0)) || !em_ee_c_le_bits(minx, w32(r, box + 0xC)))`
- 1536: `if ((uint32_t)u8(r, e + 0x54) >= 0x50)`
- 1540: `uint32_t kind = (int32_t)s16(r, shape) & 0xF000;`; `uint32_t kind = (uint32_t)u16(r, shape) & 0xF000;`
- 1564: `for (k = 0; k != 3; k++)`
- 1567: `st16(r, 0x700030CAu, (u16(r, 0x700030CAu) & 0x1FF00) | u8(r, e + 0x54));`
- 1624: `a[0] = reg((uint32_t)local);`
- 1625: `a[1] = reg((uint32_t)local);`
- 1629: `st16(r, 0x7000324Eu, s8(r, a0 + 2) & 0x1F);`
- 1632: `if (f_1A06A0(r, sp) <= 0)`
- 1636: `if (f_19CF50(r, sp) <= 0)`
- 1640: `if (mode > 0) {`
- 1645: `if ((int32_t)flags & 0x80000000u) {`; `if ((uint32_t)flags & 0x180000000u) {`
- 1646: `for (i = 0; i != 3; i++)`
- 1652: `st32(r, 0x700031D8u, (int32_t)mode);`
- 1659: `return v < 0 ? (int32_t)~(~(int32_t)v >> n) : (int32_t)((uint32_t)v >> n);`; `return v < 0 ? (int32_t)~(~(uint32_t)v >> n) : (int32_t)((int32_t)v >> n);`; `return v < 0 ? (int32_t)~(~(uint32_t)v >> n) : (uint32_t)((uint32_t)v >> n);`; `return v < 0 ? (uint32_t)~(~(uint32_t)v >> n) : (int32_t)((uint32_t)v >> n);`
- 1703: `a[0] = reg((uint32_t)local);`
- 1705: `a[2] = reg((uint32_t)src);`
- 1717: `a[1] = reg((uint32_t)kind);`
- 1718: `a[2] = reg((uint32_t)rows);`
- 1719: `a[3] = reg((uint32_t)local);`
- 1736: `uint32_t q = em_ee_div_bits(em_ee_cvt_s_w_bits((int32_t)(sra32(v, 16) & 0xFFFF)), 0x477FFF00u);`
- 1747: `st = s8(r, p + 4);`
- 1752: `if (st <= 0) {`
- 1753: `uint32_t f = em_ee_div_bits(em_ee_cvt_s_w_bits((int32_t)CALL0(r, sp, FN_RAND)), 0x4F000000u);`
- 1758: `st32(r, s3 - 0, 0);`
- 1760: `st32(r, s3 + 8, (int32_t)CALL0(r, sp, FN_RAND));`
- 1768: `st8(r, p - 0, 1);`
- 1770: `} else if (st > 1) {`
- 1773: `if (s8(r, 0x008101E4u) == 3) {`
- 1779: `if (key <= 1 || key == 0) {`; `if (key == 1 || key <= 0) {`
- 1785: `seed = (uint32_t)w32(r, s3 + 8);`
- 1789: `switch (s8(r, p + 0xD)) {`
- 1791: `have = 2;`
- 1799: `have = 2;`
- 1807: `have = 2;`
- 1841: `n2 = (int32_t)((int32_t)seed * 37u + 11u);`; `n2 = (uint32_t)((uint32_t)seed * 37u + 11u);`
- 1844: `st32(r, 0x70003A2Cu, unit16((int32_t)((int32_t)n2 * 37u + 11u)));`; `st32(r, 0x70003A2Cu, unit16((uint32_t)((uint32_t)n2 * 37u + 11u)));`
- 1855: `emit(r, sp, id, (int32_t)n & 0xFF, rows + 0x120, local);`; `emit(r, sp, id, (uint32_t)n & 0x1FF, rows + 0x120, local);`; `emit(r, sp, id, (uint32_t)n & 0x7F, rows + 0x120, local);`; `emit(r, sp, id, (uint32_t)n & 0xBF, rows + 0x120, local);`; `emit(r, sp, id, (uint32_t)n & 0xDF, rows + 0x120, local);`; `emit(r, sp, id, (uint32_t)n & 0xEF, rows + 0x120, local);`; `emit(r, sp, id, (uint32_t)n & 0xF7, rows + 0x120, local);`
- 1860: `n = (int32_t)w32(r, s3 - 0);`; `n = (uint32_t)w32(r, s3 + 0);`
- 1862: `st32(r, s3 - 0, (uint32_t)(n - 1));`; `st32(r, s3 + 0, (int32_t)(n - 1));`
- 1863: `st8(r, p - 0, 2);`
- 1865: `st8(r, p - 0, 1);`
- 1868: `uint32_t ids[3] = {0x411, 0x412, 0x413}, t = s8(r, p + 0xD);`
- 1872: `a[0] = reg((uint32_t)p);`
- 1873: `a[1] = reg((uint32_t)(s3 + 0xC));`
- 1888: `for (k = 0; k != 3; k++) {`
- 1904: `for (i = 0; i != 0x20; i++) {`
- 1905: `for (j = 0; j != 0x20; j++) {`
- 1967: `return em_ee_mul_bits(0x30000000u, em_ee_cvt_s_w_bits((int32_t)CALL0(r, sp, FN_RAND)));`
- 1978: `if (em_ee_c_le_bits(f, floor_bits) || u8(r, flag) >= 0xFF) {`
- 1980: `st8(r, p + 5, s8(r, p + 5) + 1);`
- 1991: `uint32_t blk = (int32_t)CALL3(r, sp, 0x001CB5F0u, 0x007635C0u, 0x1000, count);`
- 1992: `st32(r, blk - 0, 0);`
- 2004: `st = s8(r, p + 4);`
- 2005: `area = s8(r, 0x00810700u);`
- 2006: `if (st <= 0) {`
- 2008: `st32(r, s - 0x0, w32(r, p + 0xB0));`
- 2019: `area = s8(r, 0x00810700u);`
- 2029: `d = s8(r, p + 0xD);`
- 2031: `if (u8(r, 0x008107F5u) >= 0xFF) {`
- 2035: `} else if (d <= 0) {`
- 2036: `if (u8(r, 0x008107F6u) >= 0xFF) {`
- 2049: `} else if (area <= 0) {`
- 2050: `if (s8(r, 0x00810701u) == 1) {`
- 2078: `x = em_ee_add_bits(w32(r, s - 0x0), em_ee_div_bits(em_ee_mul_bits(w32(r, s + 0x30), fj), 0x41F00000u));`
- 2080: `if (s8(r, 0x00810700u) == 0x13 && u8(r, p + 0xD) == 1 && em_ee_c_le_bits(z, 0x44664000u) &&`; `if (u8(r, 0x00810700u) == 0x13 && s8(r, p + 0xD) == 1 && em_ee_c_le_bits(z, 0x44664000u) &&`
- 2098: `if (st > 1)`
- 2102: `uint32_t d = s8(r, p + 0xD);`
- 2103: `if (d <= 0) {`
- 2104: `if (s8(r, p + 5) == 1)`
- 2106: `} else if (d <= 1) {`
- 2110: `if (s8(r, p + 5) == 1)`
- 2113: `st32(r, local - 0, x);`
- 2118: `a[0] = reg((uint32_t)local);`
- 2126: `for (i = 0; i != 0x20; i++) {`
- 2127: `uint32_t im1 = i <= 0 ? 0 : i - 1, ip1 = i + 1 >= 0x20 ? 0x1F : i + 1;`
- 2128: `for (j = 0; j != 0x20; j++) {`
- 2129: `uint32_t jm1 = j <= 0 ? 0 : j - 1, jp1 = j + 1 >= 0x20 ? 0x1F : j + 1;`
- 2152: `int32_t cm = col - 1 < 0 ? 0 : col - 1, cp = col + 1 < 0x1F ? col + 1 : 0x1F;`; `int32_t cm = col - 1 < 0 ? 0 : col - 1, cp = col + 2 < 0x20 ? col + 1 : 0x1F;`; `int32_t cm = col - 1 < 1 ? 0 : col - 1, cp = col + 1 < 0x20 ? col + 1 : 0x1F;`; `int32_t cm = col - 2 < 0 ? 0 : col - 1, cp = col + 1 < 0x20 ? col + 1 : 0x1F;`; `int32_t cm = col - 1 <= 0 ? 0 : col - 1, cp = col + 1 < 0x20 ? col + 1 : 0x1F;`
- 2153: `int32_t rm = row - 1 < 0 ? 0 : row - 1, rp = row + 1 < 0x1F ? row + 1 : 0x1F;`; `int32_t rm = row - 1 < 0 ? 0 : row - 1, rp = row + 2 < 0x20 ? row + 1 : 0x1F;`; `int32_t rm = row - 1 < 1 ? 0 : row - 1, rp = row + 1 < 0x20 ? row + 1 : 0x1F;`; `int32_t rm = row - 2 < 0 ? 0 : row - 1, rp = row + 1 < 0x20 ? row + 1 : 0x1F;`; `int32_t rm = row - 1 <= 0 ? 0 : row - 1, rp = row + 1 < 0x20 ? row + 1 : 0x1F;`
- 2154: `uint32_t vc = s + 0x9060 + (int32_t)row * 0x80, vm = s + 0x9060 + (uint32_t)rm * 0x80;`; `uint32_t vc = s + 0x9060 + (uint32_t)row * 0x80, vm = s + 0x9060 + (int32_t)rm * 0x80;`
- 2155: `uint32_t vp = s + 0x9060 + (int32_t)rp * 0x80, k = w32(r, s + 0x2C);`
- 2156: `add_to(r, vc + (int32_t)col * 4, k);`
- 2158: `add_to(r, vm + (int32_t)col * 4, k);`
- 2159: `add_to(r, vp + (int32_t)col * 4, k);`
- 2160: `add_to(r, vc + (int32_t)cm * 4, k);`
- 2161: `add_to(r, vc + (int32_t)cp * 4, k);`
- 2163: `add_to(r, vm + (int32_t)cm * 4, k);`
- 2164: `add_to(r, vp + (int32_t)cm * 4, k);`
- 2165: `add_to(r, vm + (int32_t)cp * 4, k);`
- 2166: `add_to(r, vp + (int32_t)cp * 4, k);`
- 2168: `for (i = 0; i != 0x20; i++) {`
- 2169: `for (j = 0; j != 0x20; j++) {`
- 2176: `if (f_1E7CB0(r) <= 0) /* called with s, which it does not read */`
- 2178: `for (i = 0; i != 0x1E; i++) {`
- 2180: `for (j = 0; j != 0x20; j++) {`
- 2185: `st32(r, blk + 0x610, i >= 0x1D ? 0x14000000u : 0x17000000u);`
- 2194: `area = s8(r, 0x00810700u);`
- 2214: `} else if (key <= 0) {`
- 2237: `if (s8(r, 0x00810700u) == 0x13) {`
- 2241: `n = (uint32_t)w32(r, 0x00275C10u);`
- 2243: `if (n <= (-1))`; `if (n < 0)`
- 2245: `if (n >= 0xF)`; `if (n > 0xE)`
- 2247: `st32(r, 0x00275C10u, (int32_t)n);`
- 2249: `n = (uint32_t)w32(r, 0x00275C10u);`
- 2252: `word = ld64(r, 0x002553B0u + (int32_t)n * 8) & UINT64_C(0xFFFFFFFBFFFFFFFF);`
- 2253: `callv(r, sp, 0x001CB950u, 3, reg((uint32_t)0x007635C0u), 0x1000, word, 0);`
- 2255: `callv(r, sp, 0x001CB950u, 3, reg((uint32_t)0x007635C0u), 0x1000, UINT64_C(0x20048BA199422040), 0);`

## Appendix: review-sweep equivalents (rounds 5 to 7), with proofs

These are the survivors of the three review sweeps (`build/area01/sweep_SYS_1`, `_2`, `_3`,
mutant sources in each `mut/`) that are proven equivalent. Each proof covers every input the
original can receive at that site; where a proof rests on a callee's result range, the callee
and why its range holds are named. Line numbers are those of `src/game/em_area01_sys.c`
(unchanged since round 3).

### Round-4 review (sweep_SYS_1): 17

- **S06** (callf copies at most a0..a3): the only callf with more than four integer
  registers is the packet emit with five, whose fifth is 0; the call record is zeroed
  first, so that register is 0 either way.
- **S09** (the 001B0D80 entry stores `v != 0` instead of v): f_1B0D80 returns only 0 or 1.
- **F08** (0015A2C0 link-2 reload index with unsigned shifts): v is 00122BB8's result,
  which is its state masked with 0x7FFFFFFF on its return path (the decomp C of 00122BB8 is
  byte-matched), so v is in [0, 2^31); (v >> 16) * 3 is at most 0x17FFD, and the signed
  and unsigned shifts agree on it.
- **F10** (00128C10: ((c >> 16) * 4) >> 15 written as c >> 29): for every int32 c with
  arithmetic shifts, c >> 16 lies in [-2^15, 2^15), times 4 does not overflow, and
  ((c >> 16) * 4) >> 15 = (c >> 16) >> 13 = c >> 29.
- **F11** (00128C10 state 1 drops `t != 0 &&`): t is a byte; t >= 2 implies t != 0.
- **F13** (00128C10 kind_step 2 re-reads +0x0D instead of the kind argument): the kind is
  that byte, read in f_128C10; between that read and the argument there is no store (the
  store to b+0xFA comes before the read) and no call.
- **F15** (the random heading x / 256 written as x * 2^-8): em_ee_div_bits(x, 256.0) equals
  em_ee_mul_bits(x, 2^-8) for all 2^32 words x under the EE model (exhaustive check,
  `sweep_SYS_1/f15.c`).
- **F18** (001A9E00 sign test by the sign bit instead of the EE compare with 0): e is used
  only in em_ee_c_le_bits(e, limit). The sign-set values the EE compare does not treat as
  negative are -0 and the negative denormals; negating them gives +0 or a positive
  denormal, whose compare key is also 0, so every comparison has the same result.
- **F21** (001B0300 enable test from (v1 & 0x80) instead of reading D_008101E5 back): the
  byte was just stored as (v1 & 0x80) ? 1 : 0, with only a store to 0x8101E6 and no call
  in between.
- **F24** (001E7CB0: the set {4, 5, 7, 8, 9} written as 4 <= w <= 9 && w != 6): the same set.
- **F25** (001E7C60 stores s+4 before the loops instead of after): s+4 differs modulo 2^32
  from every grid address (offsets 0x64..0x3E64), and the loops contain no load and no
  call, so the final memory is identical. They differ only in the partial state left by a
  store that faults, where the original cannot complete (TLB) and the contract discards the
  state.
- **F28** (0019CF50 reads keep[k] back from 0x700031A0 + 4k): that word was just stored from
  0x700031B0 + 4k; the two ranges are disjoint.
- **F32** (001A06A0 drops `idx < 0`): idx = (halfword >> 8) & 0xFF lies in 0..255 (the
  documented unreachable outcome 1A0804T).
- **F33** (001A06A0 drops `& 0xFF`): a 16-bit value >> 8 is at most 0xFF already.
- **F39** (001E7D20 state 0 reads s+4 once before the grid loop): it was stored at the start
  of state 0; the loop stores only at s+0x60.., +0x4060.., +0x8060.., +0x9060.. (never s+4)
  and makes no call.
- **F40** (001E7D20 ripple: the texture u difference from the cached neighbours): the re-reads
  are the same H addresses (s+0x8060..0x905C); the only stores in between are to V
  (s+0x9060..0xA05C, disjoint); there is no call.
- **F44** (001E7D20 rnd_cell as (uint32)v >> 26): 00122BB8's result lies in [0, 2^31) (as for
  F08), so the arithmetic-shift form equals v >> 26.

### Round-5 review (sweep_SYS_2): 9

- **N24** (0015A2C0 timer test by the host compare): f is an em_ee_add_bits result. In the
  model the sum is always finite and never denormal: an exponent-255 operand gives +-MAX, and
  em_eei_pack returns +-0 below the normal range and +-MAX above it (em_ee_float.h). On finite
  normal values and +-0 the EE compare key orders like IEEE (-0 == +0 in both), so the two
  tests agree. (A check of 150 million sums found no other output, as a cross-check.)
- **N27** (001E3D90 phase wrap by the host `<=`): v is re-read from s3+0x10+4k, which holds
  the em_ee_add_bits result stored just before; the loop's earlier iterations store only to
  lower k. The argument of N24 applies.
- **N29** (001A8840 height test by the host `<=`): v is 0011DF78 (sign-bit clear, run as
  original) of an EE subtraction, so a finite normal value or +0; the right side is an
  em_ee_add_bits result. The argument of N24 applies.
- **N33** (00128C10 `near_test != 0` changed to `> 0`): R-RESULT-01: 001B13F0 returns only 0
  or 1.
- **N36** (001E7D20 `col - 1 < 0` changed to `col == 0`): rnd_cell gives 0..31 (00122BB8's
  result range, as for F08), where the two tests agree (R-CLAMP).
- **N37** (001E7D20 `row + 1 < 0x20` changed to `row != 0x1F`): row is 0..31 by the same
  range.
- **N38** (00128C10 ((c >> 16) * 4) >> 15 masked with & 3): c <= 0x7FFFFFFF by the same range,
  so the index is 0..3 and & 3 is the identity.
- **N55** (001A06A0 halfword read-modify-write replaced by a byte store of the low byte): the
  halfword's high byte is written back with the value read in the same statement, with no
  call or store between (R-RMW-BYTE).
- **N62** (001A8840 reads the extents pointer a1+0x30 once instead of after each 0011DF78):
  0011DF78 is a leaf with no frame, no store and no call, and the module stores nothing
  before the last read, so the word cannot change between the reads for any input, a
  record in the stack included.

### Round-6 review (sweep_SYS_3): 11

- **T02** (00128B80 stores +4 = 2 as a word): the next three statements store 0 at +5, +6 and
  +7 with no call or load between, so the final bytes are identical. If +5..+7 were
  unmapped, the original's byte stores there would fault (TLB), so it cannot complete that
  input either.
- **T17** (00128C10 kind_step 1 table index from the kind argument): kind is u8(e+0xD), read
  after the store to b+0xFA; the only store before the re-read is to b+0xFB = e+0x2EB, the
  same base, never e+0xD.
- **T20** (00128C10 state 4 sub 3: (int8_t)v0 == 0 for (v0 & 0xFF) == 0): both hold exactly
  when the low byte is zero.
- **T24** (001A9E00 mula / madd operands swapped): dx and dz are EE subtraction results,
  finite and saturated. A raw product of finite operands is never Inf or NaN (the pack
  saturates at MAX), so mula(a, a) is the raw product, and em_eei_ee_sum is symmetric in its
  operands (it orders them by exponent; x == y gives 0; the sign follows the larger
  magnitude). So madd(mula(dx, dx), dz, dz) == madd(mula(dz, dz), dx, dx) for every input.
  This holds within the measured EE model (docs/EE_FLOAT_MODEL.md).
- **T27** (001B0300's first 00102948 call told na = 2): 00102948 reads only a0 and a1 (RegScan
  measures it), so the callee behaves identically. Only the header's "registers the original
  sets" count differs; the test checks that every claimed register holds the original's
  value, not that the count is exact (section 4, contracts).
- **T36** (001E7D20 level fill stores +5 = 2): the caller has just tested u8(p+5) == 1; the
  only store before this one is st32(p+0xB4) (bytes +0xB4..+0xB7), so the re-read plus 1 is 2.
- **T37** (001E3D90 state 0: s3+0x14 = f): the two stores before the re-read both write f, at
  0x70003A20 and s3+0x10. A partial overlap needs s3+0x10 unaligned, where the original's
  word store raises an address error, so w32(0x70003A20) == f whenever the original completes.
- **T39** (001E3D90 unit16 with a logical shift): bits 0..15 of v >> 16 are bits 16..31 of v
  for both shifts, and & 0xFFFF keeps only those.
- **T43** (0019B4C0 0x700031C0 delta from the kept copy): 0x700031A0 + 4i was stored with
  kept[i] in the statement just before, with no store between (fixed addresses).
- **T48** (0019CF50 kind from u8(face+0x1A)): the halfword store at 0x70003B88 can change the
  byte only if face+0x1A == 0x70003B89; then face is odd, and the original's earlier
  halfword loads at face+0xC..+0x16 raise an address error first. If face+0x1A ==
  0x70003B88, the stored low byte is that byte.
- **T56** (00157CE0 passes 001B6F00's a1 zero-extended): 0x700038A0 has bit 31 clear, so its
  sign-extended and zero-extended register images are identical.
