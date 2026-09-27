# AREA01 lane EXITA: the exit-only lowmem / unknown_07 boot functions

Level 2 (AREA01) side track, lane EXITA, 2026-09-26 (session s87, wave 2). New files only:
`src/game/em_area01_exita.{c,h}` (prefix `em_area01_exita_`) and
`tools/test_area01_exita_reference.py`. The module is built and tested but not wired: nothing
in the port calls it, and the Makefile does not list it (the test builds it privately). Wiring
waits for the AREA01 exit / AREA00 arrival work (AREA01_OVERVIEW.md, SECOND_LEVEL_ROUTE.md
beat a01_07).

## 1. Scope

The rows of `../Extermination/build/s87/census/a01_delta.json` (`new_functions`) in the census
subsystems `lowmem` and `unknown_07` whose flag `exit_change_or_area00_only` is set: 12 boot
functions, 9,272 bytes. Every one ran for the first time in beat a01_07, after the area change
(AREA00 load at f282, the AREA00 arrival from f531). The subsystem names are census labels, not
evidence; the functions are named by address here.

| Function | Bytes | Decomp source | First run (a01_07) |
|---|---:|---|---|
| 00113478 | 180 | NEARMISS C, ee-gcc | f3468, AREA00 |
| 001195A8 | 168 | byte-matched C, ee-gcc | f282, the area change load |
| 00128390 | 52 | C linked from asm (mwcc) | f531, AREA00 |
| 00128600 | 64 | byte-matched C (mwcc 2.3.3) | f4487 |
| 00128640 | 420 | byte-matched C (mwcc 2.3.3) | f4486 |
| 001289C0 | 240 | byte-matched C (mwcc 2.3.3) | f531 |
| 00128AB0 | 204 | byte-matched C (mwcc 2.3.3) | f531 |
| 00129780 | 1916 | byte-matched C, pinned jump table | f532 |
| 0012A5D0 | 2032 | byte-matched C, pinned jump table | f531 |
| 0012ADC0 | 508 | NEARMISS C (mwcc 2.3.3) | f4488 |
| 0012AFC0 | 1092 | byte-matched C, pinned jump table | f4487 |
| 0022DCD0 | 2396 | NEARMISS C (mwcc 2.3.3) | f531 (census subsystem unknown_07) |

The two byte-matched ee-gcc functions and the NEARMISS ones were translated from their C and
then read against the original instructions; where the C and the instructions differ, the
instructions win (section 6).

The captured end-of-beat image of a01_07 (AREA00 resident, area bytes 00 00 00 00) holds two
records whose handler (+0x10) is 0x0012A5D0 (0x7AD490, 0x7ADD60, both in state 1) and one whose
handler is 0x0022DCD0 (0x7B55D0, state 1, six table entries). These are the captured cases.

## 2. Model

The lane-SYS design (AREA01_SYS.md section 2), unchanged except where noted.

- **Memory.** Regions keyed by original address: RAM, the scratchpad and a stack region. Two
  stack items live in the original frame layout below the entry `sp`:
  - 00113478's ninth 0010E8A8 argument, the word 0 at the call's stack pointer
    (`sp - 0x40 + 0`);
  - 0022DCD0's 0x60-byte request block at `sp - 0x1A0 + 0x140`. 001CFB50 is handed its address
    (and fills it), 001CFBE0 reads it; 0022DCD0 itself never writes it.

  Every other local is a C local. 00113478 reads its result word through the EE's uncached mirror
  (0x2027AB40); the module reads RAM 0x0027AB40, the word the mirror maps.
- **Calls.** One worker, `call`, for every call leaving the twelve. It takes the callee address
  (for 0012A5D0's indirect call, the word it loads from +0x4C), a0..t3 as 64-bit register images,
  **f12..f19** as raw bits (lane SYS passed f12..f15; 001CFB50 takes a fifth float argument in
  f16), the counts `na` / `nf` of registers the original sets, and the stack pointer at the call.
  The worker returns all 64 bits of v0, and the module tests the whole register wherever the
  original does: the zero / non-zero tests of 00112E28, 001B10B0 (both 00128AB0 arms),
  001C2430, 0019AD00, 001C2540, 001C25E0, 001B2140, 00128B80 and 001B0D80; the sign test of
  0010E8A8; 001C2770's result against 0 and 8 (0012AFC0 states 2 and the tail, 0012A5D0
  sub-state 8); and 0012D580's third argument, which is 001C2770's whole v0. Each of these
  stubbed results has a case whose non-zero bits are all in the upper half (section 4), and the
  original's outcome on it is what the module does. Three results are narrowed by the original
  itself and the module does the same: 001CCF70's (stored and reloaded as a word, so
  sign-extended from 32 bits), 001B1630's (only its low byte is tested) and 001C64F0's (stored
  as a halfword). The results of run-as-original callees (001B13F0, 001281C0, the LCG) are
  whatever the original leaf returns. Calls among the twelve
  are direct. 001287F0 and 001B0D80 (lane SYS) and 001C2770, 001C3BE0, 001C3D60, 001C69A0,
  001B13F0 (lane MATH) are callees here like any other.
- **Arithmetic.** `em_ee_float.h`: 00128640's squared length is two products summed into the
  accumulator and the third added by the accumulate op (not three separate adds);
  0012AFC0's angle is 2pi times float(draw & 0xF0), then divided by 256; 0022DCD0's
  `1 + draw / 2^31`, `R * ((k + 1) / count)` and `(seed >> 16 & 0xFFFF) / 65535 + 0.0001` follow
  the operand order of the original.
- **Fail-stop.** Unmapped address, NULL worker, negative worker result, NULL output: the fault is
  latched, the entry returns -1, and every later call returns -1 until
  `em_area01_exita_clear_fault`. No UNDEFINED input exists in these twelve: a register scan of
  every path (lane SYS RegScan) shows each routine reads only its argument registers; the other
  registers it reads are saved-register stores in its callees' prologues and, through 001C3DB0 ->
  00102718, vf6.w, which that leaf subtracts from itself (lane SYS VU_READS).
- **Store trace.** `EM_AREA01_EXITA_STORE_TRACE` (test builds only); `wr` is the only store path.

## 3. What each routine does

Full descriptions are in the comment above each translation. Short forms:

- **00113478() -> v0.** 00112E28(0x1E) zero (all 64 bits) returns 0. Otherwise the word
  0x00241D48 = 8, then 0010E8A8(0x0027AF60, 0x16, 0, 0, 0, 0x0027AB40, 4, 0; ninth argument 0 on
  the stack). A negative result (64-bit sign): 0010B840(word D_00241D0C), then 0x00241D48 = 0,
  result 0. Otherwise 0x00241D48 = 0 first, then 0010B840(word D_00241D0C), result the word
  at 0x0027AB40. (The two exits store and call in opposite orders; the module keeps both.)
- **001195A8(a0) -> v0.** -1 unless a0 (the unsigned 64-bit register) is below 0x80 and the
  word 0x0027C6C0 + 12 a0 is 1. Then the 48 records of 0x6A bytes at 0x0027CCC0: one with
  halfword +0 = 1 and halfword +0x22 = a0 gives -1. Otherwise 00121A28(entry, 0, 12), result 0.
- **00128390(a0, a1) -> v0.** D_0081070A zero: 30 when a1 is non-zero, else 15. Otherwise 50 /
  30. a0 is not read.
- **00128600(a0) -> v0.** The byte 0x00242ED0 + (a0 << 4) + (00122BB8() & 15).
- **00128640(p) -> v0.** 0 when the byte 0x70003B8D, the halfword D_0028A9A0 or the word
  D_008104E0 == 0x28 says so. Else 001028D0(0x70003600, 0x00810350, p + 0xB0) and d = 0011E748 of
  the squared length. d not <= 70: 0. d < 30.1: +5 from 00128600(4) (0 -> 2, 1 -> 3, else 5);
  else d < 60.1: +5 from 00128600(0) (0 -> 2, 1 -> 4, else 5); else +5 = 2. Then +6 = +7 = 0,
  result 1.
- **001289C0(a0, a1).** a1's float block (+0x60..+0x8C, +0xD8..+0xEC defaults), 001029C0(a0 +
  0xD0), a0 +0x30 = 0x00275668, +0x0B = 0, halfword +0x34 = 00128390(a0, a1 +0xE1 != 0), +0x36 = 0,
  a1 +0xFA/+0xFB/+0xF6 cleared, a0 +0x58 = the word D_0028A4C8, +0x80..+0x8C = 1.0, +0x0A = 0.
- **00128AB0(a0, a1) -> v0.** 001B10B0(a0, D_00810788 == 0xFF ? 0x10 : 0x0F, 0x11) non-zero
  returns 0; else a1 +0xE1 = 1 / 0. Bit 7 of a0 +0x0D moves to a1 +0xE0 (and is cleared).
  001289C0(a0, a1), 00102948(a1 + 0x50, a0 + 0xB0), result 1.
- **00129780(a, b, sel) -> v0.** By sel & 0xFF: probes through 001C2430 (0, 1, 5, 6), two
  transformed side points through 0019AD00 (2, 7), six axis offsets through 001C2540 with the
  hit record's flags at the word 0x700031D0 (3, 4, 8, 9), or the turn commits (10, 11, 12:
  001C3DB0, 001031E0, 001C3BE0, 00102958, 00128830, 001287F0 / 001C69A0; result 1). Then the
  +0x28 timer (over 0x28: reset, +4 = 3, result 0) and, when a probe hit, the re-seat from the
  record at 0x700031D0 (pointer read again for each of the three words), the -4 push-back and
  the (0, -8, 0) probe; b +0xE4 = st << 8, result 1.
- **0012A5D0(p).** States 0..4 (byte +4) of a record with its sub-block at +0x1F0: 0 (by +5:
  00128AB0, then 00129780 and the +0x0D 4 / 9 test), 1 (the 001B2140 gate, the 0x70003B8D /
  +0xF6 gate, the 64-frame 001B0D80 test, 001B17A0, 00128B80, the 14 jump-table arms by +5, the
  (0, -1.4, 0, 1) test through 001C25E0 after arms 1, 2 and 13, +0xEC = 1.0 or 1.8 by +0xD8,
  001C64F0, the 00102958 copy 0x70003000 -> 0x70003400 and 001C69A0 unless +5 is 7, 001288D0 and
  the +0x4C handler when +1 is set), 2 (the 001B0D80 test, D_0081078F, 00129FC0), 3 (0012E070,
  the word 0x700031F4 - 1 for +0x0D 10..12, else the +0xE0 copy back to state 4, then 001B1190
  and 001AFC10) and 4 (by +5: 001289C0, 00129780 with the hold time from the halfword table
  0x00275380, the +0x28 countdown, 001B13F0 and 001B1630).
- **0012ADC0(a0, a1, a2; f) -> v0.** f > 0: 001B13F0(a2, a1; f) must be non-zero; otherwise
  001B13F0(a2, a1; -f) must be zero; else result 0. Then the scratch matrices from s +0x70 / +0x80
  (s = a0 + 0x1F0), 00102718, 001027E0, 001028D0(0x700038B0, a1, a2), 001026A0, and s +0xE8 =
  001B1240(0x700038C0; -x, -z). Result 1.
- **0012AFC0(a0, a1).** mode = 001C2770(a0, a1, +6 < 4). By +6: 0 pick a wait (0x78 / 0xF0 by
  the draw's low bit) and either 001287F0(.., 1; 8.0) with +6 = 3 or a random heading (001B1470)
  with +6 + 1 and 001287F0(.., 2; 8.0); 1 / 4 turn toward a1 +0xE8 at 0.06981317 per call
  (001B12B0), +6 + 1 and speed 0.2 on arrival; 2..5 the dwell step (0012ADC0 against
  D_00810350 at 120.0: the counter +0x28, 00128640 at 0x5B), then 2 turns back (mode 8: pi +
  heading) or probes a1 + 0x50 at -30.0 or counts down, 3 / 5 count down. mode 0 or 8:
  001C3D60(a0, a1).
- **0022DCD0(p).** State 0 picks one of 13 tables by (D_00810700 << 8) + D_00810701 (other keys:
  +4 = 3) and fills the table 0x00822CF0.. (matrices, colours, per-entry words; T = 1 + draw /
  2^31), stores a seed at c +4 and runs state 1 in the same call. State 1 walks the entries:
  area 0x1300 / 0xD00 skips or stops, 001D0400, the 00102948 colour copies, the count class
  from 001281C0 (8, 10 / 14, 17 / 28 / 35 / other), the 001026A0 / 001CCF70 point, then per
  count step the 001CFB50 / 001CFBE0 requests with the local seed (never stored back), then
  T += PH (minus 1 above 2.0), p +0xB0..+0xBC and 001FC3C0(p, &TEX[i], 0x420; 100, 4096).
  States 2 / 3: 001AFC10(p).

## 4. How it was verified

`tools/test_area01_exita_reference.py`, `python3 tools/test_area01_exita_reference.py`.

**Oracle.** FallEE (the measured COP1 / VU0 model) with lane SYS's recording interpreter
(SysEE) runs the original code of each routine over the captured a01_07 RAM and scratchpad
(`../Extermination/build/s87/route_a01/a01_07_level_exit/`); the native module runs over a copy
of the same bytes. Calls among the twelve are not intercepted on either side.

**Callees.** Every call leaving the twelve has a policy; the test asserts it for every direct
call target in the ELF, and checks each policy against the registers the callee reads on all
its paths (lane SYS RegScan): a callee that reads an argument register the policy does not
compare fails the test (a stub's saved-register prologue reads excepted).

- **Run as original** on both sides (the native side through a second interpreter bound to the
  native memory): 001026A0, 00102718, 001028B8, 001028D0, 00102948, 00102958, 001029C0,
  00102B08, 00102BB0, 001031E0, 00103230, 0011E748, 00121A28, 00122BB8 (the LCG), 001281C0,
  001B12B0, 001B13F0, 001B1470, 001B1240.
- **Stub** (recorded, scripted v0 / f0): everything else, 44 callees. 001027E0 and 001C3DB0 are
  stubs because they reach a parallel-extend MMI instruction the interpreter does not implement;
  their pointer arguments are snapshotted at entry instead.

**Lockstep.** As lane SYS: at every call the callee, the stack pointer, the compared argument
registers (64-bit images), the float arguments, every register the native says it sets, the
stack bytes a stub reads (0010E8A8's ninth argument; the whole 0x60-byte block at both
001CFB50 and 001CFBE0), the bytes behind stub pointer arguments, and every RAM / scratchpad line
either side stored to since the last check. This runs **before** any stub side effect is
applied. After the last store: the same check, the result, the call log, all 32 MiB and the
scratchpad.

**Cases** (656 enumerated and captured, plus 180 random in full mode). `cap`: the three captured records (0012A5D0 twice, 0012AFC0 on both
records, 0022DCD0, 00128640). `t`, enumerated from the code:
- 00113478: the gate 0 / 1 / -1 / 2^32 (upper half only) and the request result 0 / 5 / -1 /
  -2^40 (negative only in the upper half) / 0x80000000.
- 001195A8: a0 0, 3, 4, 6, 7, 0x7F, 0x80, 0xFFFFFFFF, 0x80000000; named records first / middle /
  last, a record past the 48th, table words 1 / 2, and a named record whose +0x22 halfword is
  0x0105 against a0 5 (its low byte equals a0; the original compares the halfword and goes on).
- 00128390 and 00128600: every table row and flag, negative and upper-bit arguments.
- 00128640: the three gates, distances on both sides of every threshold including the six
  neighbouring floats of 70.0, 30.1 and 60.1, and LCG seeds solved so the 00128600 draw picks
  each outcome.
- 001289C0 and 00128AB0: both records, +0xE1 (0 / 1 / 0x80) / +0x0D / D_00810788, 001B10B0
  0 / 1 / -1, and 001B10B0 = 2^32 on both D_00810788 arms.
- 00129780: every selector 0..14 plus 0x10D / 0xFF, first / second side point, the k-th of six
  axis probes with each flag class, 001C2430 / 0019AD00 (first and second) / 001C2540 (first
  and sixth probe) returning 2^32, the timer at 0x27 / 0x28 / 0x7FFF / -5, a record at the word 0x700031D0 with
  distinct words and a different word after its pointer.
- 0012ADC0: f 120, -30, 0, -0, a denormal, 3e38 at three distances; non-zero orientation vectors.
- 0012AFC0: every state 0..7 with mode 0, 8, 1 and 2^33 on both records, state 2 with mode
  2^32 + 8 (the original's mode != 8 test sees the upper bit), both draw parities,
  both 00128600 outcomes, turns that arrive or not, the dwell counter 0 / 0x59 / 0x5A / 0x7FFF
  near and far, the -30.0 probe inside / outside with the countdown at 1 and 3.
- 0012A5D0: every state and sub-state (0..15 in state 1, both 0xD8 values, byte +1 set or not),
  001B2140 0 / 1 / 2^32, the byte 0x70003B8D 1 / 2 / 0xFF with +0xF6 0 / 1, the 001B0D80 poll
  at phases 0, 5, 0x20 (skipped) and 0x40 (polled) of its 64-frame mask with 001B0D80 0 / 1 /
  2^32, 00128B80 1 and 2^32, the tail's signed +0xFA test with +0xFA 0x80 / 1 / 0 (00128B80
  non-zero, so +0xFA keeps its memory value), the probe hit / miss after sub-states 1, 2, 13
  (001C25E0 1 and 2^32), sub-state 8 with v 0 / 1 / -1 / 2^32,
  state 3 for kinds 10..13 and 9 with +0xE0 set or not, state 4 sub-states 0..4 with the four
  hold-time draws, the countdown at 1 / 2 / 0 / 0x8000, the 001B13F0 / 001B1630 outcomes (0,
  1, 0x100, 0x1FF).
- 0022DCD0: all 13 area keys and two unknown ones, the count classes of 001281C0 (8, 10.5, 14,
  17.9, 28, 35.2, 3, -1.5), key 0x1300 with D_008101E4 3 / 2, D_0081024E 8 / 7, D_008105D0
  700 / 770 / 800 against entry positions on both sides of 770, key 0xD00 with D_00810702 4 / 6
  / 5 / 0, the T wrap (T + PH at 1.995 .. 2.5, and exactly 2.0, which is kept, and the next
  float above, which wraps), Q against 4.0, entry counts 0 / 1 / -1, states 2..4, and 001CCF70
  results with upper bits.
`r` (full mode only): 180 random perturbations of the owner records and the globals they read.

**Stub side effects ("fx", lane SYS rounds 1 and 5).** For every case of 00113478, 00128AB0,
00129780, 0012A5D0, 0012AFC0 and 0022DCD0: an `fx` variant (after the k-th stub every field
the twelve load after it changes: bytes / halfwords bit 0, words bit 4, the +0x4C handler moved
to a second stubbed address) and an `fx before` variant (at the k-th stub every field loaded
since the previous stub changes, new bits each time). 0022DCD0's entry count is kept in 0..8
(it bounds the loop). Full mode: 1,410 fx cases; the default run a fixed sample of 40 that
always includes `fx t 113478 gate 0x1 rpc 0x0` and `fx t 12AFC0 7ad490 st 0 mode 0x200000000`
(QUICK_FX: each killed a mutant, section 5).

**Store-site variants (lane SYS round 4).** A store instruction is covered when some case
stores bytes that all differ from what was there. For each uncovered site, the first case that
reaches it runs again with the stored bytes pre-set to their complement; every byte / halfword
site also runs with the bytes after the field set to 0xA5. A dropped store, a narrower or wider
store, or a store to a neighbouring field then shows at the next call. 269 variants in full
mode (311 store sites, 163 covered by the cases themselves), 286 in the default run (146
covered by its cases).

**Branch coverage.** Full mode asserts that both outcomes of every conditional branch in the
twelve routines are taken, except one (UNREACHABLE, with the proof in the test): 0022DCD0's
per-entry request loop is always entered, since every (first, count) pair its switch assigns
has first < count. The default run reaches the same coverage through the pinned greedy cover
(119 cases, computed once with a scratch script and listed in the test) plus the mutant
killers (QUICK_KILLERS, 68 cases, section 5): QUICK_PINNED is their union, 178 cases, and with
the 6 captured cases the default run is 184 of the 656. That is more than QUICK_CASES (170), so
the default run has no sampled filler: every default case is captured or pinned by name, and
the test asserts that every pinned name exists.

**API checks.** NULL context, NULL output (fault NULL, the function named), the latch and its
refusal, clear_fault (also of NULL), an unmapped record (fault UNMAPPED with the exact address),
a NULL worker (fault NULL naming the callee) and a worker returning -1 (fault WORKER).

**Timings (M1, 4 workers, cached library).** Default: 6.4 to 7.5 s user + 0.3 to 0.4 s sys,
2.0 to 6.9 s wall over three runs (last: 6.44 user, 0.29 sys, 2.0 s wall; the slower runs
shared the machine with another lane's sweep). Full (`EM_TEST_FULL=1`): 38.3 s user,
1.1 s sys, 15.2 s wall.

## 5. Mutants

Bounded, one round each (this track does not loop sweeps until dry). A scratch generator
(`build/area01/exita/mutate.py`) makes single-operation mutants of the routine bodies of
`em_area01_exita.c` (1,999 candidates): `==` / `!=` exchanged; `<`, `<=`, `>`, `>=` moved by one;
each hex constant with its low bit flipped or plus 4 (offsets, addresses, float bits); EE compare
le / lt, add / sub, mul / div exchanged; store width changed (st8 -> st16, st16 -> st32, st32 ->
st16); signed / unsigned loads exchanged; a register image built without sign extension; a
store or call deleted; two adjacent stores or calls swapped. Each mutant is compiled into its own
library and the test runs with `EM_AREA01_EXITA_SOURCE`.

| Sweep | Mutants | Killed | Survived | Did not compile |
|---|---:|---:|---:|---:|
| seed 11, test before the store-site variants and extra cases (all cases) | 150 | 103 | 46 | 1 |
| seed 11, full mode, final test | 150 | 123 | 26 | 1 |
| seed 11, default mode, final test | 150 | 123 | 26 | 1 |
| seed 23 (fresh sample), default mode | 100 | 86 | 13 (then 12) | 1 |
| seed 11, default mode, re-run on the review-round-2 test | 150 | 123 | 26 (the same 26) | 1 |
| seed 23, default mode, re-run on the review-round-2 test | 100 | 87 | 12 | 1 |
| reviewer's 51 hand-picked mutants, default mode, review-round-2 test | 51 | 51 | 0 | 0 |

The first round's survivors showed three test gaps, closed before the later rounds: fields the
routines store already held the stored value in the capture (fixed by the store-site variants);
0012ADC0's orientation vectors were zero in the capture, so its scratch words were zero whatever
was read (fixed by the `vectors` cases); and the neighbouring floats of the 70.0 threshold, the
49th record past 001195A8's scan, and a record at the word 0x700031D0 whose words differ from the word after
its pointer were missing (fixed by the `bound`, `past the scan` and `camera` cases; the case name is only a tag). The seed-23
round left one killable mutant: 0012A5D0 storing +0xFA as a halfword clears +0xFB, which the
movement sub-states 0..13 then overwrite, but 14 and 15 do not; the case
`t 12A5D0 st1 sub14 fb kept` now kills it.

**Review round 2.** An independent review ran 51 hand-picked single-operation mutants. The
round-1 test killed 32 of them in the default run and 44 in full mode. That disproved two
claims of this document: that the default run kills what the full run kills (true only for
the lane's own sweeps, whose killers were pinned), and that the upper-half 64-bit tests were
exercised. Seven mutants survived both modes and none of them is equivalent. Each got a case,
and each case passes on the module and kills its mutant:

| Mutant | Site | Killing case (pinned) |
|---|---|---|
| B2 | 001195A8 compares the +0x22 halfword as a byte | `t 1195A8 named halfword 0x105` |
| G2 | 00128AB0 tests 001B10B0's result in 32 bits | `t 128AB0 7ad490 gff gate upper d83` |
| J6 | 0012AFC0 state 2, `mode != 8` in 32 bits | `t 12AFC0 st2 mode 0x100000008` |
| K2 | 0012A5D0 poll mask 0x3F narrowed to 0x1F | `t 12A5D0 st1 poll 32 1` |
| K5 | 0012A5D0 tail, signed +0xFA test made `> 0` | `t 12A5D0 st1 fa 0x80` |
| K6 | 0012A5D0 sub-state 8, `v == 0` in 32 bits | `t 12A5D0 st1 sub8 4294967296` |
| L3 | 0022DCD0 T wrap, `x > 2.0` made `x >= 2.0` | `t 22DCD0 st1 wrap T1.5 PH0.5` |

The earlier case `t 12A5D0 st1 fa neg` never produced a negative +0xFA: sub-state 3 stores
+0xFA = 1 before the test. It is renamed `t 12A5D0 st1 sub3 p1`. The `fa` cases now reach the
test with 00128B80 non-zero, so +0xFA keeps its memory value. Twelve mutants were killed only in
full mode. Their full-mode killers are now pinned into the default run (QUICK_KILLERS, or
QUICK_FX for the fx one):
- A1 (00113478 sign test in 32 bits): `t 113478 gate 0x1 rpc -0x10000000000`
- A2 (32-bit gate): `t 113478 gate 0x100000000 rpc 0x5`
- A4 (0x0027AB40 read before 0010B840): `fx t 113478 gate 0x1 rpc 0x0`
- B3 (47 records scanned): `t 1195A8 named last`
- F2 (+0xE1 tested as signed > 0): `t 1289C0 7ad490 e1 128 d 0`
- H3 (timer >= 0x28): `t 129780 timer 39 sel 3`
- J1 (32-bit mode test at the tail): `t 12AFC0 7ad490 st 0 mode 0x200000000`
- J2 (dwell >= 0x5A): `t 12AFC0 st2 d5.0 t0x59 dc5`
- K4 (hold-time shift 16): `t 12A5D0 st4 sub1 draw 1`
- K8 (001B1630 mask 0x1FF): `t 12A5D0 st4 sub3 d200.0 v0x100`
- L4 (770 bound with c.lt): `t 22DCD0 st1 1300 e4 2 h 0 d 770.0`
- L8 (001CCF70 result truncated to 16 bits): `t 22DCD0 st1 h`

The reviewer's default run killed two more mutants only through its sampled filler: H7
(001C2430's result tested in 32 bits, `t 129780 sel 0 hit64`) and I2 (0012ADC0's f12 test with
c.lt, `t 12ADC0 f1e-39 d100.0`). Both cases are pinned as well. The same round added
upper-half-only results for every other stubbed callee result that is tested (section 2):
001B10B0 on the D_00810788 != 0xFF arm, 0019AD00, 001C2540, 001C25E0, 00128B80 and 001B0D80.
It also added poll phase 0x40 (a wider mask) and T + PH one float above 2.0. With the grown
pinned set the default run has no sampled filler (section 4).

Re-checked on the final test (the same mutants; no new sweep):
- the reviewer's 51, default mode: all killed;
- the seven named survivors, full mode: all killed;
- the lane's seed-11 and seed-23 sets, default mode: each mutant has the status it had before,
  except seed-23 #45 (the +0xFA halfword store), which is now killed.

The full-mode case set only grew in this round, so a mutant it killed before it still kills.
On the reviewer's 51 and seed 11's 150, the default run now kills exactly what the full run
kills (51 of 51; 123 of 150, the other 26 being the equivalent survivors below and one that
does not compile). Seed 23 was run in default mode only. This is measured on these mutants; it
is not a general property of the default run.

**Survivors of the lane's own sweeps, all argued equivalent (38: seed 11: 26, seed 23: 12).**
None of them is from the reviewer's set; every reviewer mutant is now killed.
- *Adjacent stores swapped* (seed 11: 14, seed 23: 7): two stores to different bytes with no load,
  store or call between them. Equivalent for any input in which the stored fields do not overlap
  the fields read by the second statement. That holds by construction where both addresses are
  fixed scratch / table words or offsets of one record (11 + 6 of them). Four depend on records
  not aliasing the scratchpad (0012ADC0's copies from s +0x74 / +0x78 / +0x88 into 0x700036C4 /
  0x700038C8 / 0x700038B8, and 00129780's reloads of the word 0x700031D0 after the b +0x80 store);
  one (0012AFC0 state 2, a1 +0xD0 against a0 +6) depends on the two records not overlapping.
  Section 8 lists aliasing as a gap.
- *Register image without sign extension* (seed 11: 4, seed 23: 3): the pointer or constant is
  below 0x80000000 in every input the memory model maps (RAM, scratchpad, stack), so the 64-bit
  image is the same.
- *A byte store widened to a halfword whose second byte the next statement overwrites* (seed 11:
  6): 00128640's +5 then +6 = 0; 00129780's +4 then +5; 0012A5D0's +5 then +6 and 001289C0's +0xFA
  then +0xFB. No read between, so the final bytes are equal.
- *Signed / unsigned exchanged where only equality with small values or zero is tested* (seed 11:
  2, seed 23: 2): 0012A5D0's state switch (every value of 0x80 and above is the default arm
  either way), the halfword D_0028A9A0 against zero, D_00810702 against 4 / 6, and 0012AFC0's
  countdown (the halfword stored and the zero test use only the low 16 bits).

## 6. Findings for the lead (decomp side; no decomp file was edited)

- **0012ADC0, NEARMISS C, the 001B13F0 calls.** The C declares
  `func_001B13F0(int a, float b)` and calls it with (arg2, +-fparg0). The original passes its own
  a1 through unchanged as the callee's second integer argument, and 001B13F0 reads a0, a1 and
  f12 on every path (measured). The call is therefore 001B13F0(arg2, arg1, +-fparg0); the C
  compiles to the same instructions only because nothing overwrites a1 first. The C should
  declare and pass the second argument.
- **00113478, the callee 0010B840.** The decomp used to call it `CreateSema`. 0010B840 is a
  two-word syscall stub with call number 0x42; in the EE kernel's numbering 0x40 is CreateSema and
  0x42 SignalSema, and 00113478 passes it an existing semaphore id (the word D_00241D0C). The
  decomp's syscall-stub labels were relabelled from the numbers they load (2026-09-27, decomp
  commit 2f8227f and the whole-table pass after it), and 0010B840 is now `SignalSema` there. The
  port names the callee by address.
- **0022DCD0, NEARMISS C.** Confirmed against the instructions: state 1 keeps the seed from
  c +4 in a register and never stores it back, and the state-0 fill runs state 1 in the same
  call. The C is right on both.
- **00129780 / 0012A5D0 / 0012AFC0.** Their byte-matched C matched the original on every case.
  The comments above them in the decomp name roles ("player actor main tick", "camera flags");
  those are labels, not verified here.

## 7. Overlaps with existing port files (for the lead)

No verified translation of any of the twelve existed in the port; nothing is duplicated.

- **00113478**: `em_iop_stream.c` (`em_iop_stream_00113478`, a worker adapter of
  `em_stream_lanes_original`) is a model of the drive side of that request (it abandons the
  modelled read). It is not a translation of the EE routine. Wiring should decide whether the
  stream lanes call this translation (with an IOP-side worker for 00112E28 / 0010E8A8 / 0010B840)
  or keep the model. Note for that decision: `em_stream_lanes_original.c` calls the slot as
  `w_00113478(ctx, 1)` and `em_iop_stream_00113478` takes a `mode`, but the original routine
  takes no argument: it sets a0 = 0x1E before its first call and never reads the incoming a0
  (the register scan confirms it). `em_area01_exita_00113478(ctx, &out)` therefore has no
  argument, and wiring it into that slot drops the 1; whatever the 1 selects in the model is
  not part of this EE routine.
- **001195A8**: `em_startup_load_gaps.h` has only a worker slot `w_001195A8` (bound by its caller);
  this translation can back that slot.
- **00128390**: `em_enemy.c` hard-codes the results 15 / 30 (`BUG_HP_A`) as a stand-in; this is the
  translation to replace it with.
- **00128600, 001289C0, 00128AB0, 00129780**: lane SYS's 00128C10 (`em_area01_sys.c`) calls them
  through its worker (00128600 as a run-policy callee in its test); these translations can be
  those workers.
- **00128640, 0012A5D0, 0012AFC0**: named only in comments (`em_enemy.h`,
  `em_area01_math_actor.h`).
- `tools/test_scene_no_shadow.py` passes with these files: they read 0x70003B8D but assign no
  canonical field and name no migrated progress / request byte, so no REACHERS entry is needed.

## 8. Known gaps

- **Aliasing.** Records that overlap the scratch words the routines write (0x700036A0..0x700038CC,
  0x70003600..0x7000361C) or each other are not tested. The module keeps the original's order
  of every load and store, but stores to distinct fields with no load or call between them are
  equivalent only for non-aliasing inputs (section 5 lists the swaps this covers).
- **Pointers above 0x7FFFFFFF.** The memory model maps RAM, the scratchpad and the stack. A
  record passed through a kseg0 / uncached mirror address is outside it; the module faults
  (UNMAPPED) where the original would reach RAM, and the register image of such a pointer is
  sign-extended as the original's would be but was not exercised.
- **Callee side.** 001027E0 and 001C3DB0 run as stubs (interpreter gap), so their effect on later
  loads is covered only by the fx side effects. Every IOP / SIF request (00112E28, 0010E8A8,
  0010B840, 001D0400's and 001CFB50 / 001CFBE0's GS work, 001FC3C0) is verified at its EE-side
  call only.
- **Inputs.** Only the a01_07 image; the routines' first runs were at earlier frames of that beat
  (the census frames in section 1), which no capture holds.

## 9. Reproduce

macOS arm64, port repo root:

```
python3 tools/test_area01_exita_reference.py                 # default (~10 s CPU)
EM_TEST_FULL=1 python3 tools/test_area01_exita_reference.py  # full sweep
python3 tools/check_no_disassembly.py src/game/em_area01_exita.c src/game/em_area01_exita.h \
    tools/test_area01_exita_reference.py docs/AREA01_EXITA.md
python3 tools/test_scene_no_shadow.py
```

The mutation sweep script is scratch (`build/area01/exita/mutate.py`, not committed): it
writes each mutant to `build/area01/exita/mut/` and runs the test with
`EM_AREA01_EXITA_SOURCE=<mutant>`.

## Known gaps from the close-out spot check (2026-09-26)

Not mistranslations; test blind spots recorded instead of another sweep round.

- V3: in 00128640, changing the 70.0 range test from c.le to c.lt survives the default run and is killed only in full mode (no pinned quick case sits exactly on 70.0).
