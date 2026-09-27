# AREA01 lane "math": actor math, owner behaviours, player-record steps

Lane "math" of the AREA01 (second level) side track, 2026-09-25 (session
s87). Original executable SHA-256
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Scope: the main-line boot functions of the AREA01 route census
(`../Extermination/build/s87/census/a01_delta.json`, rows not
`side_beat_only` and not `exit_change_or_area00_only`) in the subsystems
`math_vector`, `actor_anim` and `anim_runtime`: 30 functions, 16,536 bytes.
Nine already had verified port translations (section 4). The other 21 are
translated here, unwired: nothing in the live port calls them yet, and no
existing port file was edited.

Code:

| File | Contents |
|---|---|
| `src/game/em_area01_math_core.c/.h` | the EE storage view (RAM at 0 and its 0x20000000 / 0x30000000 mirrors, scratchpad at 0x70000000) and the callee dispatcher |
| `src/game/em_area01_math_actor.c/.h` | 001B13F0, 001B2140, 001C25E0, 001C2770, 001C39F0, 001C3BE0, 001C3D60, 001C69A0 (the helpers the 00128C10 / 0012A5D0 behaviours call) |
| `src/game/em_area01_math_owner.c/.h` | 001BB860 + 001BB560, 001C02E0 + 001BF630, 001BFFD0 (with its piece 001C0004), 001CB360, and the script-op routines 001B9CF0, 001BBAE0, 001BBBF0 |
| `src/game/em_area01_math_player.c/.h` | 00183250, 00187DE0, 00187EC0 (player-record steps) |
| `tools/test_area01_math_reference.py` | the original-instruction oracle |

Test: `python3 tools/test_area01_math_reference.py` (quick). Round-6
measurement (2026-09-26, `EM_TEST_JOBS=4`, load average 37 to 45 from
other lanes, by `/usr/bin/time -p`): cold (the private -Werror build
included) 6.4 s wall with 15.2 s user and 1.0 s sys; warm 5.8 s and 4.3 s
wall with 14.9 + 0.9 s and 13.7 + 0.6 s of CPU. The ~10 s CPU target is
not met: the rest is the pinned kill cases, which stay in the default run
(about 3.3 s for 001BFFD0's, whose callees animate the node; about 3 s for
the 00183250 unit cases, each of which runs the player-update callee
00175900; 1.4 s for 001C2770's `latch_cases`; 2 s for the other owner
routines'), plus about 2.5 s of route catches and the start-up. Round 5
took 31.7 s user + 1.0 s sys; section 3 lists what moved behind
`EM_TEST_FULL=1`. Without `EM_TEST_JOBS` the test uses min(8, cores − 2)
workers. `EM_TEST_FULL=1` runs every beat, every perturbation and every
sweep, and poisons every callee call: 348.2 s wall, 1,057.7 s user and
9.5 s sys with 4 workers at load 40 to 80 in the round-6 run. It builds the
four modules privately into `build/area01/math/`.

## 1. Form of the translations

The routines work on original EE addresses. Every pointer they receive or
build is an EE address, every load and store goes through the view in
`em_area01_math_core.c`, and every callee is reached through one dispatcher
`call(ctx, target, a[], na, f[], nf, &v0, &f0)`. The dispatcher receives the
callee's original address and the argument registers the original sets up
for that call (a0.. and f12.., raw 32-bit words), and returns its v0 / f0.
The test measures, at every call, which argument registers the original
callee reads before writing them and fails a call that passes fewer; on
some calls the native side passes more than the callee reads on the path
taken (section 3, "Register inputs"), which is harmless because every
passed register is also compared with the original's. The behaviour method
at node +0x4C is called the same way, with the loaded pointer as the target.
A host binds the dispatcher to port translations of the callees (or to a
fail-stop default). Routines of this lane that call each other do so
directly: 001C2770 calls 001C39F0 and 001C3BE0, 001C3D60 calls 001C3BE0,
001BB860 calls 001BB560, and 001C02E0 calls 001BF630.

Every COP1 operation uses `game/em_ee_float.h` on bit patterns (add, sub,
mul, div, the ACC forms adda / mula / madd, the compares, cvt.s.w, neg).
The VU0 macro operations of 001C69A0 use `em_vu_vec_bits` under their real
forms: VMULbc dest xyz for bc x, y and z; VMULAbc x, VMADDAbc y and z, and
VMADDbc w, all with dest xyzw. The operand order and the re-reads of memory
after each call follow the original instructions. So do stores made in a
call's delay slot, which land before the call.

Store trace. The view can append the EE address and size of every store
to a caller-supplied buffer (`trace`, `trace_cap`, `trace_len`; NULL = off).
Only the reference test uses it (section 3). It changes nothing else.
Store rule: a translation stores only through `em_a01m_sw` / `_sh` / `_sb`,
the only stores the trace records. `em_a01m_where` returns a read-only
(`const`) pointer, and the header states the rule. The test also scans the
three translation sources before building and fails on any use of the
view's `ram` / `spad` fields, of `em_a01m_where`, or of a `(uint8_t *)`
cast.

Fail-stop. This is a port contract; the original has no counterpart (an
access the EE does not map stops the original). The first unmapped
address, missing view or dispatcher, or negative dispatcher result latches
a fault (address and code). After that nothing more is stored or called,
loads read 0, and the routine returns -1. Writes made before the fault
stay. The first fault is the one kept. `core_contract` in the test checks
each of these natively (no original code is involved), since round 6:

- codes and addresses: an unmapped address latches ADDRESS (4) at that
  address; a NULL scratchpad or RAM view latches NULL (1) at the address;
  a missing dispatcher latches NULL at the callee's address (001CB360's
  first callee 001C7420); a negative dispatcher result latches WORKER (2)
  at the callee's address;
- the first fault is kept: a dispatcher callback that faults the same view
  (a load at 0x50000000) and then returns −1 leaves ADDRESS at 0x50000000;
- after a fault: a store changes no byte, the store count or the trace;
  a load reads 0 from mapped memory; 00187EC0 and 001CB360 return −1
  without calling the dispatcher; a store made before the fault stays;
  every one of the 20 routines, entered with a fault already latched,
  returns −1, stores nothing and calls nothing;
- a routine stops at a failing call: 001C3D60 whose first callee (001029C0,
  inside the inline 001C3BE0) fails returns −1 with nothing stored and no
  further call;
- `em_area01_math_clear_fault` resets both the code and the address, and
  001CB360 then runs (four calls, status 0);
- a dispatcher result ≥ 0 is success (a result of 1 gives status 0 and no
  fault), and `em_a01m_call` hands back the dispatcher's v0 / f0, also 0
  over a variable holding another value;
- the store trace appends (address, size) while two words fit in
  `trace_cap`: capacity 6 holds 00187EC0's three stores (length 6),
  capacity 5 holds two and then marks `UINT32_MAX`.

Not tested: that the writes left before a fault are in the original's
order at the moment of a fault inside a real routine (on the non-faulting
cases the oracle comparison checks the order at every call entry); and
the fault behaviour of the view for sizes other than 1, 2 and 4 bytes
(the translations use no other).

## 2. What each function does

Behaviour only; names are addresses. "Node" is a pool node (0x2F0 bytes);
"p" is the player record D_008102B0.

| Address | Size | Decomp | Behaviour |
|---|---:|---|---|
| 001B13F0 | 0x7C | BM | 001028D0(0x70003600, a0, a1). Then r = 0011E748(x*x + y*y + z*z) over the three result words (two products, ACC = their sum, ACC + z*z). Returns 0 when the float argument is below r, else 1. |
| 001B2140 | 0x9C4 | NM | Returns 0 when D_008104E0 == 0x11. Otherwise it applies a per-area table of tests (areas 0..22, jump table) comparing the area bytes D_00810701 / D_00810702 with the node's bytes +0x9D / +0x9E. Areas without a case give 0. |
| 001C25E0 | 0xA8 | BM | SPR 0x700038B0 = (0, -4, 0, 1). Then 001026A0(0x700038C0, node+0xD0, a1), 001028B8(0x700038C0, 0x700038C0, node+0xB0), 001026A0(0x700038D0, node+0xD0, 0x700038B0) and 0019B4C0(node, 0x700038C0, 0x700038D0, 6). |
| 001C2770 | 0x1278 | NM (split pair 001C2770 + 001C2FF0) | Per-frame machine on a state block (+0xE4 packs the phase in its low 4 bits and a sub-state in bits 8 and up). Phase 0 runs 001C39F0 with the +0xD8 distance, then 001C2540 / 001C2690 / 0019AB20 calls from SPR 0x700038A0 / 0x700038B0 origins and directions. The flag halfword of the hit record (the word at SPR 0x700031D0, its +0x1A) selects the next phase: 3 for bit 0x8000, 2 for 0x2800, else 1; 4 when the 0019AB20 test and the last probe both miss. The latched hit then sets the state's +0x70 / +0x80 vectors (001C3DB0, and 0011E620 / 001B1470 for node +0xC4). A latch with bit 0 runs 001C3BE0 and 001026A0, then either adds the offset to node +0xB0..B8 (latch bit 3) or stages the target in state +0xC0 and node +0x100. Sub-states 4..6 integrate the +0xF0 vertical speed (−0.04 or −0.06 per frame, floor −0.8) into node +0xB4. Phases 1..6 advance a 001C9D50 blend by 0.1 per frame; past 1.0 they restore node +0xB0 from +0x100 and set the sub-state to the phase. Returns 1 while latched or blending, 8 for the early "no grab" exits, else 0. `flags` bits 0, 1 and 2 pick the variants. |
| 001C39F0 | 0x1E8 | BM | Nothing when the distance f12 is 0. SPR 0x70003600 = (side, 0, f12, 1), and node +0xB0..B8 += 001026A0(node+0xD0, 0x70003600). For modes (state +0xE4 >> 8) other than 0 and 4: side = (f12 / 2) · 0011E2A8(state+0xDC); state +0xDC = 001B1470(2π · ((n − node+0x3C) / n)) with n = (float)001C6160(node); afterwards node +0xB0..B8 −= state +0x80..88 / 2. For modes 0 and 4, side = 0. |
| 001C3BE0 | 0x180 | BM | 001029C0(0x70003000); matrix rows from state +0x70 / +0x80; 00102718 of the two into 0x70003600, copied to row 0; then 001029C0(0x70003440), 00102B08 / 00102BB0 by node +0xC0 / +0xC4, and 001026D0(0x70003000, 0x70003000, 0x70003440). |
| 001C3D60 | 0x44 | BM | 001C3BE0(node, a1), copy_qw4(node+0xD0, 0x70003000), 001031E0(0x70003030, node+0xB0). |
| 001C69A0 | 0x3F8 | NM | Scales SPR 0x70003400 rows 0..2 by model +0x60 x / y / z. For each of the model's byte(+0x0C) bones (table at +0x110): quat_nlerp(0x70003600, bone+0x30, bone+0x40, bone+0x50) and quat_to_mat3(0x70003440, 0x70003600, bone); rows scaled by bone +0x18 / +0x1C / +0x20; rest matrix 0x70003480 = 001029C0, 00102C58(bone+0x70), translation bone +0x7C..84, rows scaled by s16 +0x88 / +0x8A / +0x8C ÷ 4096; 0x3480 = rest · anim. Bone +0x90 = 0x3480 · the parent's +0x90 matrix (s16 +0x64), or · 0x3400 for a root. |
| 001BB860 | 0x274 | NM | Node behaviour (nodes 0x7ACBC0 / 0x7ACEB0 on beats a01_00..a01_06 and the four side beats; node 0x7B8DA0 at a01_07). State +4 = 0: 001BB520, and +0 = 1. States 2 and 3: 001AFC10. State 1: sub-state +5. Sub-state 0 calls 001BB560; for kinds 0x16 / 0x17 / 0x3E the flag comes from bit (+0x34 & 31) of D_00810841[area]. Sub-states 1 and 2 wait for 001BB7C0, 3 calls 001BC150, 4 waits for 001BB7F0. Then 001C6380, the +0x4C method, and when the point D_00810350..58 is within 20 of node +0xB0: +1 = 1, and with bit 0x80 of +2, 001B1DE0. |
| 001BB560 | 0x260 | BM | With bit 2 of node +0xB: picks the side from 001B1240(node+0xB0, p.A0, p.A8) − node.C4 (wrapped, then fabs ≤ π/2) and sets p.C4 and node half +0x2E; kinds 8 and 0x16 invert +0x2E. The point SPR 0x700038A0 = (node.B0 − 6 sin p.C4, p.A4, node.B8 − 6 cos p.C4, 1), then 00182F90(p, 0x700038A0). a2 == 0 calls 001BBD60(node, 0x24D980) and 001BA1A0(a1, 0x24D900); in area 0x16 it also sets or clears bit 0x80 of D_008106C8 from the D_0024E140 byte. a2 != 0 calls 001BA1A0(a1, 0x24DA40). Then 001BA1F0(node); returns 1. Returns 0 without the bit. |
| 001C02E0 | 0x3F8 | NM | Node behaviour (node 0x7A93F0 on beats a01_00..a01_06 and the four side beats). State 0 with bit 0x20 of D_00810845: D_00810766 = 0xFF, behaviour = 001BF6B0, 001B6660(0x829110, 2, 0). Otherwise state 0 spawns a companion with 001AFA90(2, 2): copies of +0xB0 / +0xC0, kind 0x12, +0xD = 2, behaviour 001BFFD0, cross links +0x20 / +0x24. It then sets up the node (001D0C80, 001D0D40, bone_init_default_1, +0x58, +0x30 = 0x275638, tail +0x18 = 0x275648) and builds two direction vectors (±30 in y through 001026A0 / 001028B8 / 001028D0 / 00102760) and their 00102738 dot into tail +8..+0x14. No companion: +4 = 3. State 1: when 001BF630 answers, sound 0x444 (300.0) unless SPR byte 0x70003B64 is set, and half +0x28 = 1; then +0x36, 001C6380, 001D0D60(+0x90, 1.0) into tail +0, 001B17A0 and the +0x4C method. State 2: +4 = 3. State 3: 001AF890(+0x90), 001AFC10. |
| 001BF630 | 0x7C | asm | Returns 0 when SPR byte 0x70003B8D is set. Otherwise returns 1 when sqrt((p.A0 − node.B0)² + (p.A8 − node.B8)²) (0011E748) ≤ the float that tail +0x18 points at, else 0. |
| 001BFFD0 | 0x308 | NM (split pair 001BFFD0 + 001C0004) | Behaviour of the companion node (node 0x7B6190 on the same beats). State 0 waits for 001B10B0(node, 0x20, 0x21) == 0, then initialises the node (bone_init_default_2, +0x58 = D_0028A520, +0 by +0xD, the +0x110 record's +0x80 = 0xC0900000, +0x30 = 0x275638, +0x34 = 0x64). State 1 follows the parent: animation step 1.0, 001BFF90, and the +0x36 flag hand-over (0x5000 bits gated by both tails' +0x1C counters, 0x4000 also starts effect 0x80000027, counter = 0x3C). It then calls 001C68C0, 001B17A0 and the method. State 2 fades +0x64 by 0.01 to 0, then state 3. State 3: 001AFC10. |
| 001CB360 | 0x54 | BM | 001C7420(n, 0x3F5, 0), 001CB2C0(n, 0x3F3, 0), 001D1F80(0, 1, 0), then a tail call of 001D3F50(n +0x44 word). |
| 001B9CF0 | 0x2FC | BM | Script op. Lane n = op +8 (< 14): steps one value toward a target with 001B12B0(target, current, op+0xC). Lanes 0..2 step node +0xC0+4n toward op +0x20+4n; 3 steps all three (1 when all arrived); 4..6 step node +0xB0+4n toward a1 +0x30+4n; 7..9 step D_00810354[n] toward op +4+4n; 11..13 first set op +0x10 = 001B1470(node +0x94+4n + op[4n−0xC]) (a1 byte +4 = 1), then step node +0x94+4n toward it. Returns 1 on arrival (exact float compare). |
| 001BBAE0 | 0x110 | BM | Script op. First call (st[4] == 0): request block D_002821B0 = 2 and D_002821B4 = 1. The selector D_002821B8 comes from node byte +0x56 & 0x3F (0..5 → 0x80000006, 0x80000000, 0x80000002, 0x80000008, 0x8000000A, 0x80000004); D_002821BC = 0 and st[4] = 1. Kinds ≥ 6 return 1 at once. Then polls: 1 when D_002821B4 == 2. |
| 001BBBF0 | 0x12C | NM | Script op. SPR 0x700038A0 = (node.B0 − 8 cos C4, 10 + node.B4, node.B8 + 8 sin C4, 1), copied by 00102948 to D_008105E0. Then x −= 13 sin a, y = 12 + node.B4, z −= 13 cos a (a = D_00810374), copied to D_008105D0. Returns 1. |
| 00183250 | 0x1A0 | asm | Player sub-machine: phase byte +6, timer half +0x28 (read, stored decremented, old value tested). Phase 0: +0x38 = 0.3, +0x25C = 2, clip = s16 0017B490(p, 1, +0x235, +0x25C), 001749A0(p, clip, 0, 1.0), timer 50. Phase 1: next phase after the timer (timer 30). Phase 2: 00178B90(p, 0) until the timer runs out. Phase 3: 00178B90, +0x38 −= 0.01137, and below 0 → 00174A50(p, 12.0); at timer 0 it returns p to state 1/0 and clears SPR byte 0x70003B8D. Every phase then adds −0.2 to +0xB4 and calls 00175900(p, 1). |
| 00187DE0 | 0xB4 | BM | 001031E0(0x700038B0, 0x700031B0), effect 001EFD90(0x80000016, 0x700038B0, p+0xC0), 001E8B90(0x700038B0, 5.0) unless area byte D_00810700 == 0x15, then 001FB9F0(0xCA if p +0x23C == 1 else 0xDB, 0x1000, 0x1000, 0x1000). |
| 00187EC0 | 0x20 | BM | D_008102BB = 1, D_008104EA = a0, D_008105CE = a1 (low bytes). |

BM = byte-matched decomp C, NM = NEARMISS C (the logic was checked against
the original instructions), asm = asm-only in the decomp (read from the
original instructions).

Which original code reaches them on the route (all measured by running the
original owners over the captures, section 3): 00128C10 / 0012A5D0 call
001B2140, 001B13F0, 001C2770 (with 001C39F0 and 001C3BE0), 001C3D60 and
001C69A0 for each of their nodes. 001BB860, 001C02E0 and 001BFFD0 are
behaviours at node +0x10. 001BB560 has direct callers outside 001BB860 too:
a scan of the boot ELF and of the 19 overlay files in `extract/OVERLAY/`
for a direct call to it finds the three sites inside 001BB860 and one site
each in AREA04.BIN and AREA08.BIN. On those two paths a1 need not be node
+ 0x1F0. The tests reach 001BB560 only through 001BB860 (a1 = node +
0x1F0); the translation passes a1 through unchanged, and no equivalence
argument in this doc relies on what a1 points at (the +0x2E re-read after
its callees is pinned by a case, section 3). 001CB360 is reached from 001C02E0's +0x4C method
(001CA5F0). 001B9CF0, 001BBAE0 and 001BBBF0 are op callbacks of the script
table caller 0024CCC8. 00183250 is called by 0015B610, 00187DE0 by 00175900,
and 001C25E0 by 00128C10 / 0012A5D0 (on the route only at a01_07, nodes
0x7AD780 and 0x7ADD60).

## 3. Verification

`tools/test_area01_math_reference.py` runs the original instructions of each
routine in the shared EE interpreter with the measured float model
(`test_player_fall_reference.FallEE`, every COP1 and VU0 op through
`tools/ee_float_model.py`). It works over the AREA01 route captures
(`../Extermination/build/s87/route_a01/<beat>/eeMemory.bin` +
`scratchpad.bin`). Nothing original is embedded in the test; the pinned ELF
and the captures supply it all.

Each call the routine makes outside the translated set is a boundary. The
original callee runs unmodified. The test records its argument registers,
which argument registers it reads before writing them, the v0 / f0 it
writes, every RAM or scratchpad byte it writes, and where the call falls in
the log of all stores. The native routine then runs over a second copy of
the same memory. **At every boundary call**, before anything is replayed,
its dispatcher checks, in order:

1. the target, and every argument register the native call passes against
   the original's value;
2. that the native call passes every argument register the original callee
   read (and that none of those held a 64-bit value a 32-bit word cannot
   carry);
3. **the native side's whole RAM and scratchpad against the oracle's memory
   at the entry of the same call** (the oracle's memory at that entry is
   rebuilt from the store log). A failure names the call index, the callee
   and the first differing address with both bytes.

Only then does it apply the callee's recorded writes and return its recorded
results. A result register the callee did not write comes back as a marker
value (0x7FBADBAD), so a translation that used a stale v0 / f0 would
differ. After the routine the test compares the whole RAM and scratchpad
again, plus the return value, the call count and the absence of a native
fault.

This closes the hole the review found: before, only the argument registers
were checked at a call, so a wrong value the native side stored for a
callee to read, which the callee (or a later replayed write) then
overwrote, was invisible to the final comparison.

How "whole memory" is compared. Both copies start from the same bytes.
After that the oracle's copy changes only at its logged stores, and the
native copy only at the stores the view traces (the `trace` buffer of
section 1; every native store goes through that one function, which the
store rule and the test's source scan enforce) and at the
replayed callee writes. Bytes outside those ranges are therefore equal on
both sides, and comparing those ranges compares the whole memory. The
scratchpad (16 KiB) is always compared whole. The first comparison on each
memory image compares all 32 MiB at every call entry and at the end, which
checks the shared-start invariant. A trace overflow also falls back to the
full compare. `EM_A01_FULLMEM=1` compares all 32 MiB at every call entry and
at the end of every case. A validation run in that mode gave the same
verdicts (below).

Register inputs. The argument-register reads are recorded conservatively:
every register access the interpreter makes counts. To prove the callee has
no other register input, each original callee also runs a second time from
the same entry memory with every other register state poisoned:

- all other GPRs, and the upper 96 bits of every GPR;
- all other FPRs;
- VU0 vf1..vf31, its ACC and Q;
- the COP1 ACC and condition;
- HI / LO.

zero, gp, sp and ra are the only registers kept. Any change in the callee's
writes, or in a v0 / f0 it sets, fails the case. So each callee's complete
input is memory, compared at the call's entry, plus the argument registers
it read, compared by the dispatcher. Full mode poisons every call. Quick
mode poisons the first two calls of each callee per routine per job, and
the register-read check still runs on every call. A callee that touches its
caller's stack frame (a native caller has no EE stack) fails the case.
Scripted boundaries (below) are Python answers and are not poisoned.

`EM_A01_REGREPORT=1` prints, per (routine, callee), the registers the
native side passes against those the callee read. Round-6 full run
(2026-09-26): 119 pairs, 53 of them flagged as passing more on some call.
No call passes fewer registers than the callee reads (the check fails any
that does). Three real-callee pairs pass more registers than the callee
reads on the paths taken:

- 001C02E0 → 001B6660: passes a0..a2, reads a0;
- 001C02E0 → 001AFA90: passes a0 and a1, reads a0;
- 001C2770 → 00103230: passes a0..a2 and f12, reads a0, a1 and f12.

The other 50 pairs are flagged only because of scripted boundaries (on
their real calls the callee reads what is passed): the scripted answers
are Python functions that read nothing, or only a0 for the scripted
001028D0 and 00102718. Round 5 had 42 flagged pairs; the 11 new ones are
the round-6 scripted callees (001BB560 → 001B1240; 001BB860, 001BF630 and
001C02E0 → 0011E748; 001C02E0 → 001C62C0 and → 001D0D60; 001C3BE0 →
001029C0 and → 00102B08; 001C69A0 → 001029C0, → 00102C58 and → 001CA0A0),
each listed with a real-call entry that reads exactly what is passed. A
STOP boundary never runs, so it is left out of the audit and of check 2.
The extra registers are the ones the original sets up, and they are
compared too, so this is harmless. The earlier wording "exactly the
argument registers that callee reads" was inaccurate.

Case sources:

- **route**: the original owners of each capture run as original code
  (00128C10 and 0012A5D0 nodes; 001BB860, 001C02E0 and 001BFFD0 nodes). Every
  entry into a routine under test is caught with the whole machine state and
  compared from there. All three quick beats and all 12 AREA01 beats (full)
  are used.
- **perturb**: the same caught entries with state and argument fields varied.
  The variations cover 001C2770 phases, sub-states, flags, distances, speeds
  and blend values; 001C39F0 modes and distances; and 001B13F0 limits,
  including the measured distance itself and ±1 ulp. For 001C69A0 they cover
  model and bone scales and rest values, plus Inf / NaN / −MAX operands that
  exercise the VU0 clamp forms. For 001BB860 / 001BB560 / 001C02E0 /
  001BF630 / 001BFFD0 they cover every state and sub-state, every kind, the
  D_00810841 bit, player near or far (and exactly 20), the scratchpad bytes
  with their neighbours set to the opposite value, and the hand-over matrix.
- **scripted** (001C2770): the probes 001C2540 / 001C2690 / 0019AB20 are
  scripted boundaries. The answers are a hit or a miss with each surface
  class (0x8000, 0x2800, 0x2000, 0x0800, 0x5000, 0x1000, 0x4000, none), with a synthetic
  hit record (scratchpad 0x70003C00) whose pointer is stored at 0x700031D0.
  A miss answers v0 = 0 and a hit v0 = 2 or 4 (both are used): the values
  the real probes return. Read from the original instructions, 0019B4C0
  and 0019AB20 set their result register only to 0, 2 or 4, and 001C2540
  / 001C2690 end with a call of 0019B4C0 and return its v0 unchanged. (Up
  to round 4 a hit answered 1, outside that range, so a translation that
  tested a probe with `== 1` passed every scripted case; the independent
  sweep's A33 showed it.) The wall 0019AB20 answers 0, 2 or 4, and one
  case answers 0x80000000 (the routine tests it `== 0`).
  Each answer and write is recorded and replayed exactly like an original
  callee. The route geometry alone reaches only part of the outcomes. The
  same device supplies the answers 0, 1 and 2 of 001BB7C0 / 001BB7F0 (001BB860),
  001B10B0 (001BFFD0) and a failing 001AFA90 (001C02E0).
  Out-of-range inputs. Some cases give a routine a value that its real
  callers or callees never produce. The byte-matched decomp C of 001B10B0
  returns only 0 or 1, so its scripted answers 2, 0x100 and 0x80000000 are
  outside that range. A caller scan of the boot ELF and all 19 overlays
  (17 direct calls of 001C2770, each with a constant a2 set by a dominating
  instruction or in the delay slot, and no data-word or `%lo` reference)
  gives 001C2770's flags only the values 0, 1, 2 and 6, so flags 4 and 7
  in the perturbations are outside the callers' range. The range of
  001BB7C0 / 001BB7F0 was not established. A case with such an input still
  compares the translation with the original instructions on it, but a
  mutant killed only by such an input differs from the original only on
  inputs the game does not produce: T38 and S46 (001B10B0 `!= 0` as
  `(int) > 0` / `== 1`, killed by the answers 0x80000000 / 2) are of this
  kind, and full mode kills T20 and T21 only with flags 4 (see the
  equivalence table in "Mutation testing"). Both modes also
  run a fixed list of 001C2770 cases (`latch_cases`):
  - at least one case per commit path: latch 1 (sub-states 0, 1 and 4), 2,
    3, 5 (sub-states 4, 5 and 6), 9 (4, 5 and 6) and 0x11 (→ 5), each with
    the hit normal y at 1.0 and at 0.8;
  - the flag-1 early exit, the flag-4 slide push and the no-latch wall answer;
  - every surface-mask test site with only one bit of its two-bit mask:
    0x2000 and 0x0800 alone at each 0x2800 test (phase-0 sub-state 0, the
    sub-state-2 keep, the sub-state 1..3 and latch-3 +0xE4 commits, both
    sub-state-4 probes with and without flag 4, the sub-state-5 / 6 slide
    push), and 0x4000 and 0x1000 alone at the 0x5000 tests (the
    sub-state-1 keep, both sub-state-5 / 6 probes);
  - sub-states 0x101, 0x104, 0x105 and −0xFC, which test the arithmetic
    `>> 8` of +0xE4;
  - the blend value 0x3F666667, which plus 0.1 is exactly 1.0 in the EE
    model (the test asserts this), and one ulp either side, in phases 1, 4
    and 6;
  - three sub-state-4 speeds where π·(56.25·t) and (π·56.25)·t differ;
  - sub-states 5 and 6 entered with +0xF0 exactly 0.04 / 0.06 (0x3D23D70A /
    0x3D75C28F), so the step leaves exactly +0 and both `+0xF0 < 0` tests
    (after the first probe and after the second) see 0 while the probe
    hits: landing surfaces 0x5000 / 0x4000 / 0x1000 and the 0x2800 slide
    at the first test, 0x5000 / 0x1000 at the second, flags 0 and 2. Also
    one ulp either side (a tiny negative, which lands, and a tiny
    positive). The test asserts in the float model that k − k is +0, and
    that the ulp neighbours give a nonzero negative and a nonzero positive;
  - a scripted 001C3DB0 that moves the hit-record pointer at 0x700031D0, so
    the native side must re-read it after the call where the original does.
- **edge and width cases** added after the reviews (all in both modes):
  - 001BB860: bit indexes 8, 9, 15, 31, 0x21, 0x28 and −0x7FF8 against the
    D_00810841 byte flag. Also indexes 0x10..0x17, 0x30, 0x37 and −0x10,
    for which `& 31` gives bits 16..23 (never in the byte) while a
    narrower mask such as `& 15` gives bits 0..7. Each runs with the byte's
    bit (index & 31) − 16 set, and with only that bit clear (all always
    run).
  - 001BB560 with node +0xB0 / +0xB8 at 0 or small (|x| ≤ 0.5), so the
    point's x = B0 − 6·sin and z = B8 − 6·cos keep the last bit of each
    product. A large position absorbs a 1-ulp product difference in the
    subtraction, and every route node has one. Two kinds, all always run:
    - the real 0011E2A8 / 0011DE90 at three node +0xC4 yaws, found with
      the oracle, where both 6·sin and 6·cos round differently in the EE
      model and in round-to-nearest binary32 and each difference survives
      the subtraction (the test asserts that three are found);
    - scripted sine / cosine answers (`six_products`) with only the sine
      product differing, only the cosine product, and both, at B0 = B8 =
      0 and at small nonzero positions.
  - 001BB560: +0x34 indexes 0x40, 0x41 and 0x7F in area 0x16, with the
    table bytes of both the `& 0x7F` and the `& 0x3F` rows set to opposite
    answers and D_008106C8 preset to the other state. Table bytes 0, 3
    and 0xFF (neither 1 nor 2: the bit is left alone), each with bit 0x80
    preset set and clear. Also a scripted 0011DF78 answering exactly π/2
    (0x3FC90FDB) and one ulp either side.
  - 001C02E0: +0x36 = 0x5000, 0x0100 and −0x8000 (zero low byte), and
    +0x28 preset to 0x100 (a byte store would differ from the half store).
    A state-0 spawn case with the neighbours of the spawn's half and byte
    stores preset nonzero: half +0x34 = 0xA55A (so +0x35 is 0xA5 when
    the half 0x50 is stored), +0x36 = 0x0101, tail +0..3 = 0xA5A5A5A5
    (the two tail half stores), and the companion's bytes +2, +4, +0xC and
    +0xE (around its byte stores at +3 and +0xD).
  - 001BF630: the limit set to the measured distance and one ulp either
    side, with the scratchpad bytes next to 0x70003B8D nonzero.
  - 001BF630 and 001BB860: player / D_00810350 positions whose
    player − node subtraction is inexact and where the EE result differs
    from round-to-nearest (`inexact_player`: the node moved near the
    origin, the player 25..60 away or within 6, found with the float
    model), near and far, and for 001BF630 also at the limit edge measured
    on that geometry. `near_player`'s offsets keep both operands within a
    factor of 2 of each other, so that subtraction is exact (Sterbenz) and
    a host subtraction could not differ there.
  - 001BFFD0: e1 half +2 = 2, 0x100 and −0x8000 (the routine passes a
    boolean); a tail counter with bit 31 set; hand-over flags 0x4100 and
    0x5001.
  - 00183250: the scratchpad bytes 0x70003B8C..8E preset nonzero (so the
    phase-3 clear of 0x70003B8D is observable) and timer −1. In every
    00183250 case, the half after the timer (player +0x2A..2B) is preset
    to 0xA55A. A timer store (50, 30 or the decremented value) or a timer
    read of another width would then differ. Scripted
    0017B490 answers 0x00018005, 0xFFFF7FFF and 0x0000FFFF check that
    001749A0 receives the s16; 001749A0 is scripted in those three cases.
  - 00187DE0: +0x23C bytes 0 and 3.
  - 001B9CF0: a scripted 001B12B0 that also changes the lane index at
    op +8, so the store and the arrival test must use the re-read index.
    The quick sample covers every (lane, sub-state byte 0..2) pair as well
    as every (lane, target) pair, so lanes 11..13 with sub-state byte 2
    (return 0, no 001B12B0 call) run in the default mode. Lane 3 also runs
    three fixed cases in both modes, in each of which exactly one of the
    three axes does not arrive. Each of the three arrival compares then
    decides the result on its own.
  - 001C69A0: rest translations Inf / NaN / MAX (the ACC chains reach Inf /
    NaN, where VMADDbc and VMADDAbc clamp differently), and Inf / NaN / MAX
    row scales; both always run (`!scales3`, `!scales4`). These saturate every
    lane alike, so they do not tell the three translation fields apart.
    That is the job of `!translations`: finite, distinct +0x7C / +0x80 /
    +0x84 on every bone. Also `!parent -2` (the last bone's s16 parent =
    −2 with the table word at model +0x108 pointing at bone 0; the
    original indexes the table with any value but −1), `!count 0x80` /
    `0xFF` (the bone count byte is unsigned; compared up to the first
    quat_nlerp call, see "Prefix cases" below), and `!unaligned model`: a
    copy of the model at scratchpad 0x70003C04 with its last bone copied to
    0x70003E04. The quadword loads and stores ignore address bits 0..3, so
    the scale quadword comes from model +0x5C (the copy holds distinct
    scales there) and that bone's matrix is stored at bone +0x8C.
  - 001C3BE0: a scripted 00102718 that leaves a nonzero w at 0x7000360C;
    the routine copies only x, y and z into row 0.
  - 001C2770 (`latch_cases`, both modes): flag halfwords with two classes
    at once (0xA800, 0x8800, 0xA000 at every site that tests 0x8000
    before 0x2800; 0x7800, 0x1800, 0x6000 at the sub-state 5 / 6 sites that
    test 0x5000 before 0x2800); +0xD8 = −0 and denormals in sub-states 1
    and 3 (C.EQ.S sees them as 0); sub-state-4 speeds +0, −0, denormals and
    exponent-255 patterns, where NEG.S and 0 − x differ (latch 0x11 itself
    stores +0xF0 = +0 with sub-state 4); scripted probes that also store
    +0xF0 with the other sign, at the first and at the second `+0xF0 < 0`
    test of sub-states 5 / 6 (the probes reach 0019B4C0 with the node, and
    the state block lies inside the node); node matrix +0x10C = 2.0 or
    +0xDC = 0.5, so the 001026A0 product's w is not 1.0 before the
    commit's store of 1.0 at 0x700038AC.
  - 001C39F0: distances −0, 0x00000001 and 0x807FFFFF (equal to 0 for
    C.EQ.S) in modes 1 and 0.
  - 001B2140: the exit word D_008104E0 = 0x111, 0x1111, 0x80000011 and
    0x00110000 in area 11 (the test compares the whole word).
  - 001BB860: area byte 0x80 and 0xFF with the flag bit set at
    D_00810841[area] and clear at D_00810841[area − 0x100]; a scripted
    001BC150 (sub-state 3) and a scripted 001BA1F0 (the last callee of the
    inline 001BB560, flag bit clear) that store +5, which the routine
    re-reads before its increments.
  - 001BB560: +0xB = 0x02, 0xFB and 0x80 (no bit 2); node +0xC4 values
    where the EE π + C4 differs from round-to-nearest, on the fabs ≤ π/2
    side (0011DF78 scripted to 0); area 0x16 with a scripted 001BA1A0
    that stores +0x2E = 1 after the routine stored 0, and the two row bytes
    at opposite answers.
  - 001BF630: SPR byte 0x70003B8D = 0x80 and 0xFF.
  - 001C02E0: D_00810845 = 0x10 and 0xDF (the spawn path); node +0xBC =
    0.5 (00102948 copies it to 0x700038AC before the routine's own store
    of 1.0); 001AFA90 answers 0x80000000 and 0xFFFFFF00 (compared up to the
    first 00102948 call, see "Prefix cases"); half +0x28 = 0x100 in
    sub-states 0 and 2, with tail +2..3 preset nonzero.
  - 001BFFD0: e0 state bytes 4 and 0xFF; 001B10B0 answers 2, 0x100 and
    0x80000000; the state-0 initialisation with half +0x34, tail half +2,
    the words +0x30 / +0x58 / tail +0x1C and byte +1 preset nonzero; tail
    counters 0x10000 and 0x80000000 (read as words); a scripted 001EFE00
    that changes node +0x36 before the routine copies it to e0 +0x36; a
    scripted 001C68C0 that leaves +0x64 = −0 or 0x00000001 (the method gate
    is C.EQ.S).
  - 001CB360: a scripted 001C7420 or 001D1F80 that stores another word at
    node +0x44 (001D3F50 scripted too): the routine reads it after them.
  - 001B9CF0: lane words 0x10000, 0x10003, 0x10004, 0x10007, 0x1000B,
    0x80000000, 0xFFFFFFFF, 10 and 14 (the default: return 0, no call), and
    the a1 block at 0x70003FB4 and 0x01FFFFB4, so lane 6 reads the last
    scratchpad word and the last RAM word.
  - 001BBAE0: the poll with D_002821B4 = 0x102, 0x10002 and 0x80000002.
  - 001BBBF0: a scripted 0011DE90 answering the denormals 0x00100000 /
    0x80100000 (8 times either is normal) and 0x7F800000 / 0x7FC00000, at
    node +0xB0 / +0xB8 = 0.
  - 00183250: byte +0x235 = 0x80 and 0xFF (passed zero-extended; 0017B490
    and 001749A0 scripted); a scripted 00178B90 (phase 3) that stores
    another +0x38, which the decay re-reads.
  - 00187DE0: area 0x15 with the next byte D_00810701 = 1 and 0xFF (the
    area test reads one byte).
- **Round-5 additions** (the independent sweep 2's survivors; all run in
  both modes, pinned `!` cases, `latch_cases` entries or fixed unit
  cases; the helpers are `special_1C39F0`, `special_1BB860`,
  `special_1BB560`, `special_1C02E0`, `special_1BFFD0`,
  `fixed_script_ops`, `fixed_player` and `RANGE_1B2140`):
  - 001C2770 (`latch_cases`): flags 2 at the sub-state 1 / 3 commits (no
    keep test: only bit 0 selects it) and at both sub-state-4 0x2800 hits
    (no slide: only bit 2 selects it); sub-state-4 speed 0x80800000, whose
    act +0xC0 quotient t / 180 is below the smallest normal (DIV.S flushes
    it to 0; the test asserts this in the float model); a scripted
    001C9D50 that stores +0xD4 = 0.95 over 0.5 (re-read for the +0.1
    step: the blend ends); latch 9 with a scripted 001026A0 that stores
    +0xE6 before the word store of 0x100 at +0xE4; the latch-0x11 yaw sum
    act +0xC4 = 1.0 plus a scripted 0011E620 answer 0x3E4CCCCD (EE
    0x3F999999, round-to-nearest 0x3F99999A, asserted); the blend end
    over +0xE4 = 0x00010001 (the word store of phase << 8 clears bits
    16..31); the wall answers above.
  - 001C39F0 (`special_1C39F0`): modes 0 and 4 with a real step; a
    scripted 001C6160 answering 2 with two node +0x3C values (fixed-seed
    search) where the EE n − +0x3C truncates differently from
    round-to-nearest and the difference survives the division by n and
    the product by 2π, so the f12 of 001B1470 differs (a first choice,
    0x3DCCCCCD, differed in the subtraction but not after the product,
    and did not kill the host-subtraction mutant); answering 0x01000003
    (CVT.S.W truncates to 0x4B800001, rounding gives 0x4B800002) with
    node +0x3C = 2^24, so n − +0x3C is 2 against 4 (asserted); and storing
    node +0x3C (re-read after it); a scripted
    001026A0 storing node +0xB0 (re-read after the calls) in modes 0 and
    1; the post-step node +0xB0..B8 − state +0x80..88 / 2 at values where
    the EE subtraction truncates differently (the 001026A0 product
    scripted to 0, so the position keeps its value through the add).
  - 001C3BE0: a scripted 00102718 that stores node +0xC0 (00102B08's f12
    is read after it).
  - 001B13F0: a0 == a1 (distance +0) with limits 0x80000001 and
    0xFFC00000, which C.LT.S treats as −0 and −MAX.
  - 001C69A0: a scripted quat_to_mat3 that stores the bone count 1 (the
    count byte is re-read every iteration); the last bone's parent = its
    own index, with distinct values in its +0x90 matrix (the parent rows
    are loaded once, before any output row is stored).
  - 001B2140 (`RANGE_1B2140`, 28 cases): area bytes 0x17, 0x18, 0x2B,
    0x2D, 0x4B, 0x8B, 0xFF; third bytes 0x0E, 0x15, 0x25, 0x45, 0x85,
    0xFF; rooms 0x0E, 0x15, 0x16, 0x25, 0x45, 0x85, 0xFF; sub bytes 4, 5,
    9, 0x11, 0x21, 0x41, 0x81, 0xFF. Each is placed so that the value
    under a mask (& 3 … & 0x7F) lands on a case with the other answer.
  - 001BB860 (`special_1BB860`): state 0 with byte +1 preset; 001BB7C0 /
    001BB7F0 answering 2; a scripted 001BB7C0 (sub-state 2) that stores
    +5 = 7 and answers 1; a scripted 001C6380 and a scripted +0x4C method
    that each move the node from 5 to 105 units away (the distance reads
    node +0xB0 after both); kinds 0x56, 0x57, 0x7E and 0x96 with the flag
    bit clear.
  - 001BB560 (`special_1BB560`): +0x2F preset on both sides of the fabs
    test (the half stores of +0x2E); a2 = 0x100; a scripted 0011E2A8 that
    stores p.C4 (the cosine's argument is re-read); a scripted 001BA1A0
    that stores the area byte 0x16 over 0x15 (re-read for the table
    test).
  - 001BF630: a player record other than D_008102B0 (a scratchpad copy
    with other +0xA0 / +0xA8): both fields are read from a0.
  - 001C02E0 (`special_1C02E0`, with the 001BF630 limit set to 1e6 or −1):
    D_00810845 = 0x20 with D_00810767 preset; state 2 with +5 preset; a
    scripted 001FBD50 that stores +0x36 = 0x5000; a scripted 001C6380
    that stores +0x28 = 1 after the routine stored 0; SPR byte 0x70003B64
    = 0x80.
  - 001BFFD0 (`special_1BFFD0`): +0xD = 0 in state 0 and 3 / 0xFF in
    state 1; e0 half +0x28 = 0x100 and −0x8000 with +0xD = 2; scripted
    001EFE00 storing e2 +0x1E before the word store of 0x3C at e2 +0x1C;
    scripted 001BFF90 storing +0x36, e2 +0x1C, +0x64 (state 2) or node
    +0x20 (another node: the hand-over tests the e0 read at entry); a
    scripted bone_init_default_2 (001C63E0) storing +0xD.
  - Script ops (`fixed_script_ops`): 001B9CF0 lanes 11 and 13 with a1 +5
    preset; 001B9CF0 lane 0 with node = op − 0xB8, so node +0xC0 is the
    lane word op +8: the stored step result (a scripted 001B12B0 answering
    the bits 0x00000001) becomes the lane word, and the original's
    arrival compare re-reads node +0xC4 (= op +0xC) against op +0x24;
    001BBAE0's first call with D_002821BC = 0x01020304 and st +5 preset;
    001BBAE0 with st +4 at 0x20000000, 0x30000000 and 0x70000000 (the
    first byte of each RAM mirror and of the scratchpad); 001BBBF0 with a
    scripted 0011E2A8 that also stores node +0xB4 or SPR 0x700038A0.
  - 00183250 / 00187DE0 (`fixed_player`): phase 0 with +0x25D preset;
    the phase-3 reset over +0x1F0..1F3 = 0xA5A5A5A5 (the store is one
    byte); the phase-3 decay at +0x38 = 0x3E800000 and two more values
    where the EE subtraction of 0.01137 truncates differently (00178B90
    scripted to store nothing); a scripted 00178B90 (phase 2) storing
    +0xB4; 00187DE0 with a scripted 001EFD90 storing +0x23C = 1 over 0 or
    the area byte 0x15 over 0x14.
  - The view (`core_contract`, native only): a dispatcher callback that
    loads from an unmapped address on the same view and then returns −1
    must leave the first fault latched (code ADDRESS at that address, not
    WORKER).
- **Round-6 additions** (the independent sweep 3's 33 open survivors,
  the final review's Z04 / Z09 / Z10 / Z11 / Z15 / Z02, its quick-only T18
  / T22, and 13 earlier mutants that the round-5 quick run killed only
  through random perturbations or random selections, which quick mode no
  longer runs; all pinned, both modes; helpers `close_1C39F0`,
  `close_1C3BE0`, `close_1C69A0`, `close_1BB860`, `close_1BB560`,
  `close_1BF630`, `close_1C02E0`, `close_1BFFD0`, entries at the end of
  `latch_cases`, `fixed_script_ops`, `fixed_player`):
  - the view (`core_contract`): every fail-stop rule of section 1 (T02..T07,
    Z02);
  - 001C39F0: a scripted 001026A0 that stores state +0x80 (the post-step
    subtraction re-reads it; T10); mode 0 with the 001026A0 product
    scripted to (0.3, −0.7, 1.1) and node +0xB0..B8 at values where the EE
    sum differs from round-to-nearest (N07, M49, A22);
  - 001C3BE0: a scripted 001029C0 that stores state +0x70 (T14) and a
    scripted 00102B08 that stores node +0xC4 (T15);
  - 001C69A0: a one-bone model copy at 0x70003C00 whose bone record is at
    0x70003400, so the second row pass writes bone +0x90 = 0x70003490, row
    1 of its own operand 0x70003480 (quat_nlerp, quat_to_mat3, 001029C0 and
    00102C58 scripted; quat_to_mat3 stores MAX at 0x70003464, which the row
    scale 1.0 keeps, so bone +0x64 reads −1; T24);
  - 001C2770 (`latch_cases`): flags 2 at the sub-state-4 0x8000 hit (T18);
    flags 1 at the sub-state-6 second landing (T22) and at the sub-state-4
    landing (A16); a scripted 0011E620 that stores act +0xC4 on the
    latch-0x11 path (T17); the −0.8 speed floor in sub-states 4, 5 and 6
    (N52, S16); phases 8 and 0xF (N56);
  - 001BB860: a scripted 0011E748 that stores bit 0x80 of node +2 (T32); a
    scripted 001C6380 and a scripted +0x4C method that move the point
    D_00810350 100 units (T33); sub-state 1 with +0xA preset (Z04) and
    sub-state 4 with +6 preset (Z11);
  - 001BB560: a scripted 001BA1A0 that stores half +0x2E = 0x8000 or 0x0100,
    with the table bytes at q + 0x8000 / q − 0x8000 and q + 0x100 / q at
    opposite answers (T01, T30); row byte 2 over D_008106C8 = 0x00010080
    (T31); scripted 001B1240, 0011E2A8 and 0011DE90 that store node +0xC4,
    p.A4 and node +0xB8 (T25, T28, T29);
  - 001BF630: a scripted 0011E748 answering 15 that stores the limit 20
    over 10 (T34);
  - 001C02E0: a scripted 001D0D60 answering 0x1234 over tail +0..1 =
    0xA5A5 (T36); a scripted 0011E748 that stores the scratchpad byte
    0x70003B64 (T49); a scripted bone_init_default_1 (001C62C0) that
    stores D_0028A51C (T37); the spawn with tail +4..5 preset (Z15);
  - 001BFFD0: a scripted 001C63E0 that points node +0x110 at a scratchpad
    record (T39); e0 byte +1 = 0x80 (T40); e0 byte +0 = 3 with +0xD = 0 and
    +0x36 = 0x0100 (T41; 3 is a value 001C02E0 stores); +0xD = 0x11 in state
    0 (T50); e0 state 3 in state 1 (Z09);
  - script ops: 001B9CF0 lane 11 with a1 = op + 5, so the a1 +4 byte store
    turns the lane word into 0x10B (T44), and a1 byte +4 = 3 (Z10);
    001BBBF0 with a scripted 0011E2A8 that stores D_00810374 on its second
    call (T47) and a scripted 0011DE90 that stores 0x700038A8 (T48);
  - 00183250: phase 0 with +8 preset (T53), phase 1 at timer 0 with +7
    preset (T56), the phase-3 reset with +7 preset (T52), and phase 3 with
    +0x38 exactly the decay step 0x3C3A2E8C (the decay gives 0; R26, M36,
    X02).
- **Prefix cases** (`STOP`). A scripted boundary may be the marker `STOP`:
  the oracle stops at that call's entry, and the native side must reach the
  same call with the same target, arguments and whole memory; the
  dispatcher then stops it, and nothing after that call is compared. It is
  used where the original's execution past that call leaves what the
  harness can model (001C02E0 with an 001AFA90 answer above 0x7FFFFFFF: its
  next stores go to that answer + 3 etc., outside RAM) or where the first
  call already separates the outcomes (001C69A0 with 128 or 255 bones). A
  prefix case checks only that prefix of the original's behaviour.
- **unit**: direct calls over the capture RAM. 001B2140 runs over every
  area 0..23 × sub 0..3 × third byte 0..13 × rooms 0..13 × +0x9D 0..3 (full)
  plus the 0x11 exit, four other exit words and the 28 out-of-range
  bytes of `RANGE_1B2140` (above). 001C25E0 runs on every 00128C10 / 0012A5D0 node. The
  script ops use scratchpad op records for every lane 0..15 with targets
  equal to, near and far from the current value, every request-block state
  and node kind byte. 00183250 runs every phase with timers 0 / 1 / 40 / −1
  and +0x38 at, above and exactly at the decay step. 00187DE0 and 00187EC0 run on
  the player record.

What the quick (default) run contains (since round 6; full mode is
unchanged and runs everything above):

- the three quick beats (a01_00, a01_03, a01_07), every route catch
  compared as caught (actor owners limited to their first two nodes per
  beat);
- the pinned (`!`) perturbations of each routine once, at its first catch
  on the first quick beat (a01_00; every perturbed routine is caught
  there). No random perturbation runs in quick mode (up to round 5 each
  catch also drew a fixed-seed random sample, and the pinned cases ran at
  every catch of every quick beat);
- 001C2770: the deterministic `latch_cases` only (the fixed-seed selection
  over phases × sub-states × flags × probe sequences runs in full mode);
- 001B2140: the covering set only (every (area, sub, third), (area,
  third, room) and (area, sub, +0x9D) combination once: 6,033 of 75,297)
  plus the exit words and `RANGE_1B2140`;
- 00183250: phases 1..3 with every timer, phase 3 (the only phase that
  reads +0x38) with every +0x38 value, phases 0 and 4 once, plus the fixed
  cases;
- the script-op and 001C25E0 units as before, and `core_contract`.

The 13 mutants whose only quick-mode kill was a random case (listed in
"Round-6 additions") have pinned cases now, and every mutant of the
history was re-run against the final test (below).

Results (round-6 runs 2026-09-26, all with `EM_TEST_JOBS=4`, load average
37 to 80 from other lanes; user + sys CPU from `/usr/bin/time -p`):

```
$ python3 tools/test_area01_math_reference.py
mode quick: 3 of 12 beats, 42 actor route, 29 actor perturb, 18 owner route, 262 owner perturb,
  609 script-op unit, 53 player unit, 26 001C25E0 unit, 186 scripted 001C2770,
  6,033 of 75,297 001B2140 table cases (EM_TEST_FULL=1 runs the exhaustive sweep)
area01 math vs original instructions: PASS (5.7 s)      [14.9 s user, 0.9 s sys]
  cold, with the build: PASS (6.3 s)                   [15.2 s user, 1.0 s sys]

$ EM_A01_FULLMEM=1 EM_A01_POISON_ALL=1 python3 tools/test_area01_math_reference.py
(all 32 MiB compared at every call entry and at the end; every callee call poisoned)
area01 math vs original instructions: PASS (12.6 s)     [47.1 s user, 0.8 s sys]

$ EM_TEST_FULL=1 EM_A01_UNCOVERED=1 EM_A01_REGREPORT=1 python3 tools/test_area01_math_reference.py
mode full: 12 beats, 407 actor route, 16,966 actor perturb, 90 owner route, 8,428 owner perturb,
  3,204 script-op unit, 1,140 player unit, 80 001C25E0 unit, 22,612 scripted 001C2770,
  75,297 001B2140 table cases
  register audit: 119 (routine, callee) pairs; native passes more registers than the callee read on some call for 53
area01 math vs original instructions: PASS (348.2 s)    [1,057.7 s user, 9.5 s sys]
```

Per routine (full run): cases compared, boundary calls checked (each one
with the entry-memory and register checks), and the conditional-branch
outcomes of the routine's own instructions taken.

| Routine | Cases | Boundary calls | Outcomes | Sources |
|---|---:|---:|---|---|
| 001B13F0 | 605 | 1,210 | 2 of 2 | route, perturb |
| 001B2140 | 75,377 | 0 | 322 of 322 | route, unit (every combination, plus out-of-range bytes) |
| 001C25E0 | 82 | 328 | no branches | route (a01_07), unit |
| 001C2770 | 36,436 | 192,717 | 119 of 120 | route, perturb, scripted |
| 001C39F0 | 1,782 | 3,244 | 6 of 6 | route, perturb |
| 001C3BE0 | 270 | 1,620 | no branches | route, perturb |
| 001C3D60 | 54 | 432 | no branches | route |
| 001C69A0 | 756 | 32,940 | 4 of 4 | route, perturb |
| 001BB860 | 2,346 | 21,711 | 42 of 42 | route, perturb |
| 001BB560 | 2,553 | 23,621 | 18 of 18 | route, perturb |
| 001C02E0 | 1,606 | 7,172 | 24 of 24 | route, perturb |
| 001BF630 | 187 | 143 | 4 of 4 | route, perturb |
| 001BFFD0 | 1,793 | 6,842 | 42 of 42 | route, perturb |
| 001CB360 | 33 | 132 | no branches | route, perturb |
| 001B9CF0 | 2,016 | 1,752 | 28 of 28 | unit |
| 001BBAE0 | 948 | 0 | 9 of 10 | unit |
| 001BBBF0 | 240 | 1,440 | no branches | unit |
| 00183250 | 960 | 1,788 | 16 of 16 | unit |
| 00187DE0 | 144 | 492 | 4 of 4 | unit |
| 00187EC0 | 36 | 0 | no branches | unit |

Cases include the prefix (STOP) cases, whose boundary calls count up to
the STOP call.

The quick run takes fewer outcomes. In the round-6 run: 001C2770 109 of 120, 001B2140 291 of 322, 001BB560 15 of 18, 001BB860 41 of 42 and 001BBAE0 9 of 10 (its unreachable one); all others are complete. Every outcome that quick mode misses is taken in full mode (except the two below), and every mutant of the history is killed in quick mode or proven equivalent (below).

The two outcomes not taken cannot occur:

- 001BBAE0 at 0x1BBB28, the `kind == 0xFF` test of a value masked to
  0..0x3F (dead code the original compiler kept; the decomp's C says so);
- 001C2770 at 0x1C29D8, the not-taken side: the default of the inner
  sub-state switch, inside an outer case that admits only sub-states 1..3.

Mutation testing (close-out, round 6, 2026-09-26). The claim, exactly:

**501 single-operation mutants were tried across the rounds; 474 are
killed by the default (quick) run of the final test and 27 are proven
equivalent (the table below; two of them are no-change controls); no
named survivor remains. The independent sweeps did not converge (each
found new survivors), so this covers these 501 mutants only.** It is not
a proof that every possible mistranslation fails: a behaviour that no
mutant targeted is verified only by the oracle comparison over the cases
above.

The 501, by set:

- R00..R33 (34): the first review;
- N01..N43 (43): this lane's first round;
- M01..M42, M47..M55 and P43..P46 (55): the second review;
- N44..N85 (42): the lane's round-2 additions;
- X01..X23, X25..X60 (59): the round-2 review;
- N86..N99 (14): the lane's round-3 additions;
- S01..S63 (63): the round-3 review (independent sweep 1);
- N100..N110 (11): the lane's round-4 additions;
- A01..A42, O01..O51, P01..P12, C01..C04 (109): independent sweep 2;
- T01..T56 (56): independent sweep 3;
- Z01..Z15 (15): the final review's fresh mutants.

(Round 5's count "M01..M55 and P43..P46: the second review's 59" was
wrong: M43..M46 do not exist, so that set has 55.)

How they were run. The scratch runner `build/area01/math/r6/run6.py`
(ignored build tree) reads the mutant tables read-only (round 4's
`build/area01/math/r4/run4.py`, `build/area01/sweep_MATH_2/muts2.py`,
`build/area01/sweep_MATH_3/muts3.py`, `build/area01/final_MATH/mine.py`),
applies one textual change to a scratch copy of the lane sources, and runs
the default test against it (`EM_A01M_SRC` / `EM_A01M_LIB` point the
test's private build there). Four mutants ran at a time, each test with
`EM_TEST_JOBS=1`. Every one of the 501 was run once against the final test
and final sources (sha1s in `build/area01/math/r6/sha_final_run.txt`):
1,278 s wall, 3,454 s user and 178 s sys over the 501 runs (load average
about 40 to 80 from other lanes). Log: `build/area01/math/r6/all_quick.log`
(one line per mutant, with the first failing case). A first pass over the
501, on the test before the 13 round-6 re-pins, is
`all_quick_first.log`: the 13 then survived, since quick mode no longer
runs random perturbations or the random 001C2770 selection; they are
pinned now (below). No run made a pattern error or a build error.

Equivalent (27). "Proven" means an argument over every input the site
can receive, from the code of the translation and of `game/em_ee_float.h`
(the float model both sides of each proof share), or a check over every
input of the site where it has few inputs (A29, A30: all 65,536
halfwords; A31: every pattern an ADD.S can return), or, for T16, T20 and
T21, the values the original code can pass (read from the original
instructions with local scratch readers; nothing copied). Other
exhaustive or sampled runs are cited as measurements, not as the proof
(`build/area01/math/r4/eqcheck4.c`, `build/area01/math/r5/eqcheck5.c`).
Where an equivalence depends on a memory access that could fault, the
proof says so: the original cannot complete an access to an address the
EE does not map, so such inputs are not executions the translation has to
reproduce.

| Mutant | Why it is equivalent |
|---|---|
| R11 (001C69A0 `acc[3] = acc[3]`) | a no-change control: the text changes, the operation does not. |
| R27 (00183250 duplicate store of +7) | a control: the same byte stored twice in a row, with nothing in between (same memory, same trace ranges). |
| R01 (001C2770 floor `< −0.8` to `<= −0.8`) | equivalent. The two differ only when +0xF0 already holds −0.8 (0xBF4CCCCD, the only encoding of that value), where the mutant stores the same bits again. |
| R20 (001BF630 `mula` + `madd` to `mul` + `add`), N02 (001B13F0 `madd` to `add` of `mul`) | equivalent. In `em_ee_float.h`, MADD.S is the EE sum of the ACC and the raw product, and ADD.S of MUL.S is the EE sum of the ACC and the DAZ'd, saturated product (MULA.S is MUL.S by definition). A product of two finite operands is a `pack` result, which is never denormal (it flushes to a signed zero) and never has exponent 255 (it saturates to ±MAX), so DAZ and saturation leave it unchanged: the two forms differ only when a factor has exponent 255. R20: the factor dz is a SUB.S result, and the EE sum never returns exponent 255 (±MAX instead; its one pass-through returns the finite other operand), so the forms agree for every input. N02: z is the raw word at 0x70003608, any pattern. For exponent-255 z, MADD.S gives the EE sum of the ACC with +Inf or a NaN, which is +MAX; the MUL.S form gives the EE sum of the ACC with +MAX. The ACC is the ADDA.S of two MUL.S squares, each +0, positive finite or +MAX, so it is ≥ +0 and finite, and a truncated sum of it with +MAX is +MAX. Measured as well: 0 differences for ACC ∈ {+0, MAX, 1.0} over every finite y. |
| R22 (001B9CF0 commuted add) | equivalent. `em_ee_add_bits` is commutative for all inputs: DAZ is applied to each operand alike; the exponent-255 branch is symmetric (both NaN / both Inf / the Inf one's sign); the pre-trim trims whichever operand has the smaller exponent; `em_eei_exact_sum` orders the operands by exponent before adding and its zero cases are symmetric. |
| X60 (001BB560 `1 − x` to `x ^ 1` on half +0x2E) | equivalent. The routine itself stores +0x2E just before the read: half 0 on one side of the `fabs ≤ π/2` test and half 1 on the other. Between that store and the read there is no other store and no call, only the load of the kind byte +3 (the byte-matched decomp C, `../Extermination/src/func_001BB560.c`, and the translation have the same order). So the value read is 0 or 1, and for those two values 1 − x and x ^ 1 are equal. |
| S02 (001B13F0 `adda` to `add`) | equivalent: `em_ee_adda_bits` is defined as `em_ee_add_bits`. |
| S08 (001C39F0 mode `>> 8` logical instead of arithmetic) | equivalent. The mode is used only in `== 0` and `== 4` tests. Both shifts give 0 exactly for words 0..0xFF and 4 exactly for 0x400..0x4FF; for a word with bit 31 set the arithmetic shift gives a negative value and the logical one a value ≥ 0x800000, neither 0 nor 4. |
| S10, S11 (001C39F0 `x / 2` as `x · 0.5`, both sites) | equivalent. DIV.S by 2: the dividend's significand shifted left by 39 divides by 2^23 exactly (no remainder), so `pack` returns x · 2^−1 exactly, or a signed zero below the smallest normal. MUL.S by 0.5: the product of the significands is exact and `pack` truncates nothing, with the same exponent and the same flush. Zeros and denormals (DAZ) give the signed zero in both, NaN gives +MAX and ±Inf gives ±MAX in both. Measured: 0 differences over all 2^32 x. |
| S14 (001C2770 hit flags `lh` as `lhu`) | equivalent. Every use of the value `surf` returns is masked with 0x8000, 0x2800 or 0x5000 (bits 0..15); the sign extension changes only bits 16..31. |
| S29 (001BB560 `D_008106C8 |= 0x80` as a byte read-modify-write) | equivalent. The word form rewrites bytes 1..3 with the values it read and changes only bit 7 of byte 0, which the byte form changes the same way: the memory after is identical for every content. |
| S62 (the view's `lb` zero-extending) | equivalent. `em_a01m_lb` has one use, 001BBAE0's `lb(node + 0x56) & 0x3F`; the extension affects bits 8..31 only. |
| S63 (the view's mirror upper bound dropped) | equivalent for the EE's RAM. For an address in a mirror (base 0x20000000 or 0x30000000), the bound `a < base + 0x10000000` fails only when a − base ≥ 0x10000000, which the other test (`a − base <= ram_size − size`) already rejects for every ram_size ≤ 256 MiB. The view is specified for the EE's 32 MiB (`em_area01_math_core.h`). |
| A17 (001C39F0 `(n − +0x3C) / n` as a host division) | equivalent. The dividend d is an EE SUB.S result: never denormal (`pack` flushes) and never exponent 255 (saturated). n is a CVT.S.W result: +0, or a normal with \|n\| ≥ 1. For n = +0, DIV.S gives ±MAX by the XOR of the signs, and the host gives ±Inf (d ≠ 0) or a NaN (d = 0, which is then +0, so DIV.S gives +MAX). The quotient's only use is MUL.S by 2π, which maps ±Inf and ±MAX to ±MAX and any NaN to +MAX. For \|n\| ≥ 1: d = 0 gives the same signed zero in both forms. Otherwise \|d\| ≥ 2^−24: for x of n's sign within a factor of 2 of n the EE subtraction is exact (at an exponent distance of at most 1 the pre-trim clears nothing) and a nonzero multiple of 2^−24; in every other case \|d\| > \|n\| / 4. So the quotient is normal (2^−55 ≤ \|q\| ≤ \|d\| ≤ MAX). For normal operands and a normal quotient, `em_eei_quotient` forms the quotient of the significands with the remainder as a sticky bit, and `em_eei_pack` rounds it to nearest-even: IEEE binary32 division, which is what the host's `/` computes. Sampled as well: 0 differences for n = −300..300 and every 4,099th x (`eqcheck5.log`). |
| A29 (001C69A0 `(1/4096) · w` as a host product) | equivalent. w is CVT.S.W of a sign-extended halfword: an integer of magnitude ≤ 32,768, exact in both forms (A30). (1/4096) · w is a power-of-two scaling whose nonzero results are ≥ 2^−12, so both products are exact. Checked on all 65,536 halfwords: 0 differences. |
| A30 (001C69A0 CVT.S.W of the s16 as a host conversion) | equivalent. Every halfword is exactly representable in binary32, so the truncating CVT.S.W and the host's conversion agree. Checked on all 65,536: 0 differences. |
| A31 (001C2770 blend `f <= 1.0` as a host compare) | equivalent. f is an ADD.S result, never denormal and never exponent 255. For those patterns C.LE.S applies no DAZ or saturation, and its key orders values as IEEE does (±0 equal). Checked against 1.0 on every such pattern: 0 differences. |
| A37 (001B2140 room byte read signed) | equivalent. The room value is used only in equality tests against constants 0..12 and in area 13's `room >= 8` on the uint32 value. Bytes below 0x80 read the same. For 0x80..0xFF the unsigned read gives 0x80..0xFF and the signed one 0xFFFFFF80..0xFFFFFFFF: neither equals a constant 0..12, and both are ≥ 8. |
| O50 (001BB560 kind byte +3 read signed) | equivalent. The kind is used only in `kind == 8 \|\| kind == 0x16`, and bytes 0x80..0xFF equal neither, signed or unsigned. |
| P11 (00183250: 0017B490's a3 as the constant 2) | equivalent. The routine stores byte 2 at +0x25C and loads +0x25C back for a3, with only the load of +0x235 between them (another byte; no store, no call). So the loaded value is 2 whenever the store completes. If p + 0x25C is unmapped, the original's store cannot complete, and the translation latches a fault there and makes no call in either form. |
| P12 (00183250: the +0x235 load moved before the +0x25C store) | equivalent. p + 0x235 and p + 0x25C are different bytes for every p, so the moved load reads the same value whenever both accesses complete. When either address is unmapped, the original cannot complete that access. |
| T16 (001C2770 sub-states 1..3 first probe `!= 0` as `(int) > 0`) | equivalent. The probe is 001C2540, which ends with a call of 0019B4C0 and returns its v0 unchanged (nothing writes v0 between that call and its only return); 0019B4C0 sets its result register only to 0, 2 or 4 and copies it to v0 at its single return (both read from the original instructions with a local scratch reader; nothing copied). For v0 ∈ {0, 2, 4}, `!= 0` and `> 0` agree. |
| T20, T21 (001C2770 `flags & 2` as `flags & 6` at the sub-state-4 landing and the sub-state-5 / 6 first landing) | equivalent for every flags value the game passes. The two tests differ only when bit 2 is set and bit 1 clear (flags 4, 5, …). A scan of the boot ELF and the 19 overlay files (round 6, repeating sweep 3's) finds 17 direct calls of 001C2770 (14 in the boot ELF, one in AREA00.BIN, two in AREA01.BIN); each passes a2 as a constant from a dominating setter or its delay slot, with no branch landing between the setter and the call, and there is no data-word or `%lo` reference to 001C2770 (no indirect call). So flags ∈ {0, 1, 2, 6}, where the tests agree. Full mode kills both with flags 4, a value outside that set (see "Out-of-range inputs"). |
| T26 (001BB560 node +0xC4 for p.C4 taken from the first read, across 001B1470 and 0011DF78) | equivalent. Between the two reads the translation makes no store and calls only 001B1470 and 0011DF78. Both are closed leaves: no store instruction, no call or jump other than their return, every branch inside their range (round 6 re-ran sweep 3's local check: 0011DF78 + 0x1C and 001B1470 + 0x9C). Memory is unchanged, so both reads return the same word. If a fault is latched in between, the following call and store are suppressed in both forms. |

Killed (474). The case that killed each named survivor of the reviews
and sweeps, in the round-6 run (quick mode; `!` cases, `latch_cases` and
the fixed unit cases also run in full mode). The other killed mutants and
their cases are in the log.

Sweep 3 (the 52 of its 56 that are not equivalent):

| Mutant | Change | Killed by |
|---|---|---|
| T01 | core lhu sign-extends | `!001BA1A0 stores +0x2E=0x8000` |
| T02 | core where_rw ignores a latched fault (loads/stores go on) | `core_contract` (fail-stop) |
| T03 | core dispatcher success test < 0 -> != 0 | `core_contract` (result 1 is success) |
| T04 | core trace fit >= 2 -> > 2 | `core_contract` (trace) |
| T05 | core NULL scratchpad faults ADDRESS instead of NULL | `core_contract` (NULL scratchpad) |
| T06 | core missing dispatcher fault address 0 instead of target | `core_contract` (no dispatcher) |
| T07 | core clear_fault keeps fault_address | `core_contract` (clear_fault) |
| T08 | core call after a latched fault still dispatches | `core_contract` (fail-stop) |
| T09 | 001C39F0 mode = (e4 >> 8) & 0xFF | the `latch_cases` entry for sub-state 0x104 |
| T10 | 001C39F0 state+0x80..88 read at entry (cached across calls/stores) | `!001026A0 stores state+0x80` |
| T11 | 001C39F0 +0xDC store moved after 001026A0 | `!n-3C 001C6160=2 3C=0xbefb5aae` |
| T12 | 001C39F0 f12 == 0 test ignores denormals (sign-masked bit test) | `!mode=1 d bits 0x1` |
| T13 | 001C2770 sub1-3 +0xD8 == 0 ignores denormals (sign-masked bit test) | `d8 0x1 sub1` |
| T14 | 001C3BE0 state+0x70..78 read before the first 001029C0 (cached) | `!001029C0 stores state+0x70` |
| T15 | 001C3BE0 node+0xC4 read before 00102B08 (cached) | `!00102B08 stores +0xC4` |
| T17 | 001C2770 yaw_step act+0xC4 read before 0011E620 (cached) | `yaw 0011E620 stores C4` |
| T18 | 001C2770 sub4 first probe 0x8000 site flags & 4 -> & 6 | `flags2 sub4 8000` |
| T19 | 001C2770 sub4 second probe 0x2800 site flags & 4 -> & 6 | `flags2 sub4 2800 second` |
| T22 | 001C2770 sub6 second landing flags & 2 -> & 3 | `flags1 sub6 second 5000` |
| T23 | 001C69A0 parent index masked & 0xFF | `!parent -2` |
| T24 | 001C69A0 mat_rows loads all b rows before the first store (aliasing) | `!bone at 0x70003400` |
| T25 | 001BB560 node+0xC4 read before 001B1240 (cached) | `!001B1240 stores +0xC4` |
| T27 | 001BB560 kind byte & 0x3F | `!kind 0x56 bit clear` |
| T28 | 001BB560 p.A4 read before the calls (cached) | `!0011E2A8 stores p.A4` |
| T29 | 001BB560 node+0xB8 read before 0011E2A8/0011DE90 (cached) | `!0011DE90 stores +0xB8` |
| T30 | 001BB560 table index +0x2E read as a byte | `!001BA1A0 stores +0x2E=0x8000` |
| T31 | 001BB560 D_008106C8 clear & ~0x80 -> & 0xFF7F | `!area16 clear over 0x10080` |
| T32 | 001BB860 node+2 read before 0011E748 (cached) | `!0011E748 stores +2` |
| T33 | 001BB860 D_00810350 read before 001C6380/method (cached) | `!001C6380 moves the point` |
| T34 | 001BF630 limit read before 0011E748 (cached) | `!0011E748 stores the limit` |
| T35 | 001C02E0 001AFA90 c == 0 -> (int) c <= 0 | `!st=0 c=0x80000000` |
| T36 | 001C02E0 001D0D60 result half -> byte store | `!st=1/1 001D0D60 0x1234 tail preset` |
| T37 | 001C02E0 D_0028A51C read before bone_init_default_1 (cached) | `!st=0 001C62C0 stores 0x28A51C` |
| T38 | 001BFFD0 001B10B0 != 0 -> (int) > 0 | `!st=0 busy 0x80000000` |
| T39 | 001BFFD0 node+0x110 read before bone_init_default_2 (cached) | `!st=0 001C63E0 stores +0x110` |
| T40 | 001BFFD0 e0 +1 test signed (lb <= 0) | `!st=1 e0+1=0x80` |
| T41 | 001BFFD0 e0 +0 == 1 -> & 1 | `!st=1 e0=3 d=0 flags 0x100` |
| T42 | 001BFFD0 st2 +0x64 method test cached before advance/001C68C0 | `!st=2 001C68C0 stores +0x64=0x80000000` |
| T43 | 001CB360 001CB2C0 passes a0..a1 only (na 3 -> 2) | route catch (a01_00_train_room, node 0x7a93f0) |
| T44 | 001B9CF0 lane 11 sub0 index from n (no re-read after a1+4 store) | `9CF0:11 a1 = op+5` |
| T45 | 001B9CF0 lane 10 joins lanes 11-13 | `9CF0:10/0/equal` |
| T46 | 001BBAE0 poll word -> & 0xFFFF | `BAE0:1/0/0x10002` |
| T47 | 001BBBF0 D_00810374 cached across 0011E2A8 | `BBF0:0011E2A8 stores 374 on call 2` |
| T48 | 001BBBF0 0x38A8 not re-read (first-part value reused) | `BBF0:0011DE90 stores 0x700038A8` |
| T49 | 001C02E0 st1 0x70003B64 read before 001BF630 (cached) | `!st=1/1 0011E748 stores b64` |
| T50 | 001BFFD0 st0 +0xD == 1 -> (& 0xF) == 1 | `!st=0 d=0x11` |
| T51 | 00183250 ph3 0x70003B8D = 0 byte -> half | `183250:3/0/0.3` |
| T52 | 00183250 ph3 +6 = 0 byte -> half | `183250:ph3 reset +7 preset` |
| T53 | 00183250 ph0 +7 = 0 byte -> half | `183250:ph0 +8 preset` |
| T54 | 00187DE0 +0x23C == 1 -> & 1 | `187DE0:3/None` |
| T55 | 00183250 ph0 +0x38 = 0.3 stored after 0017B490 | `183250:clip 0x18005` |
| T56 | 00183250 ph1 +6 += 1 byte -> half | `183250:ph1 +7 preset` |

The final review's fresh mutants:

| Mutant | Change | Killed by |
|---|---|---|
| Z01 | 00183250 ph3 clamp stores -0 instead of +0 | `183250:reset presets 1` |
| Z02 | core call writes v0 only when nonzero | `core_contract` (v0 / f0 of 0 delivered) |
| Z03 | 001BB860 state 2/3 test -> >= 2 | `!st=4/0 kind=None bit=0 near=True` |
| Z04 | 001BB860 sub1 +0xB = 0 byte -> half at +0xA | `!st=1/1 +0xA preset` |
| Z05 | 001C02E0 st0 half +0x34 = 0x50 -> word | `!st=0 spawn presets` |
| Z06 | 001C69A0 mat_rows VMADDbc dest xyzw -> xyz | route catch (a01_00_train_room, node 0x7a8250) |
| Z07 | 001C2770 sub4 pi/56.25 product reassociated | `sub4 speed 0xbf3c9de0` |
| Z08 | 001BFFD0 st0 e2+0x1C = 0 word -> half | `!st=0 d=1 presets` |
| Z09 | 001BFFD0 st1 e0 state >= 2 -> == 2 | `!st=1 e0 state 3` |
| Z10 | 001B9CF0 lanes 11-13 sub == 1 -> sub & 1 | `9CF0:11 sub 3` |
| Z11 | 001BB860 sub4 +5 = 0 byte -> half | `!st=1/4 +6 preset` |
| Z12 | 001C25E0 0019B4C0 a3 6 -> 7 | 001C25E0 unit (node 0x7a8250) |
| Z13 | 001C2770 latch 0x10 st+0x88 = 0 dropped | `latch11 sub1` |
| Z14 | 001C02E0 st1 miss +0x28 = 0 dropped | `!st=1 h28=0x100 near=0` |
| Z15 | 001C02E0 st0 tail half +2 = 0 -> word | `!st=0 spawn tail+4 preset` |

Killed in round 5 only by a random quick case, pinned in round 6:

| Mutant | Change | Killed by |
|---|---|---|
| R26 | 00183250 decay clamp test `< 0` as `<= 0` (first review) | `183250:decay to 0` |
| M36 | 183250 decay lt->le | `183250:decay to 0` |
| X02 | 00183250 decay lt->le | `183250:decay to 0` |
| N04 | 1B2140 area13 room >= 8 -> > 8 (boundary) | `1B2140:13/0/8/8/0` |
| X08 | 001B2140 area13 room>=8 -> >8 | `1B2140:13/0/8/8/0` |
| N46 | 1B2140 area4 IN(9,10) -> IN(9) (set) | `1B2140:4/0/10/10/0` |
| N07 | 1C39F0 host add for node +0xB0 (EE vs host) | `!mode=0 add EE` |
| M49 | 1C39F0 host add B0 | `!mode=0 add EE` |
| A22 | 001C39F0 B8 + 0x3618 host add | `!mode=0 add EE` |
| N52 | 1C2770 floor store dropped (store) | `sub4 floor` |
| S16 | 001C2770 B4 += cached sp (pre-clamp) | `sub4 floor` |
| N56 | 1C2770 phase mask 0xF -> 0x7 (mask) | `phase 8` |
| A16 | 001C2770 sub4 land flags & 2 -> & 3 | `flags1 sub4 land 1000` |

The round-3 review's list:

| Mutant | Change | Killed by |
|---|---|---|
| S03 | 001B2140 exit word -> byte | `1B2140:0x111` |
| S09 | 001C39F0 f12==0 as bit compare | `!mode=1 d bits 0x80000000` |
| S13 | 001C2770 sub1-3 commit priority 0x2800 before 0x8000 | `combo 0xa800 sub1 commit` |
| S15 | 001C2770 sub4 neg -> 0-x | `sub4 speed bits 0x0` |
| S17 | 001C2770 sub5/6 first <0 uses cached sp (across probe) | `reread F0 sub5 first -0.5` |
| S20 | 001C2770 +0xD8 == 0 as bit compare | `d8 0x80000000 sub1` |
| S21 | 001C2770 commit 0x38AC store dropped | `latch1 sub0 8000 w` |
| S22 | 001C69A0 parent != -1 -> >= 0 | `!parent -2` |
| S23 | 001C69A0 bone count signed byte | `!count 0x80` |
| S24 | 001C69A0 lq alignment mask dropped | `!unaligned model` |
| S25 | 001C69A0 0x34B8 from bone +0x80 | `!translations` |
| S27 | 001BB560 +0xB & 4 -> & 6 | `!+0xB=0x2` |
| S28 | 001BB560 table index +0x2E cached across calls | `!area16 001BA1A0 stores +0x2E preset 0x40` |
| S30 | 001BB560 pi + C4 host add | `!pi+C4 0x3fa78189` |
| S32 | 001BB860 area index signed byte | `!area byte 0x80` |
| S35 | 001BB860 sub3 +5 = 4 (no re-read after 001BC150) | `!st=1/3 001BC150 stores +5` |
| S36 | 001BB860 sub0 flag-clear +5 = 1 (no re-read after 001BB560) | `!st=1/0 bit clear 001BA1F0 stores +5` |
| S37 | 001BF630 0x3B8D test signed >0 | `!b=0x80` |
| S38 | 001C02E0 D_00810845 & 0x20 -> & 0x30 | `!st=0 845=0x10` |
| S39 | 001C02E0 c == 0 -> (int)c <= 0 | `!st=0 c=0x80000000` |
| S40 | 001C02E0 sub0 +0x28 = 0 half -> byte | `!st=1/0 h28=0x100 tail+2 near=0` |
| S42 | 001C02E0 +0x28 test half -> byte | `!st=1/2 h28=0x100 tail+2 near=0` |
| S43 | 001C02E0 tail half store -> word | `!st=1/0 h28=0x100 tail+2 near=1` |
| S44 | 001C02E0 st0 0x38AC = 1 dropped | `!st=0 spawn +0xBC` |
| S45 | 001BFFD0 e0 state ==3 -> >=3 | `!st=0 e0=0x4` |
| S46 | 001BFFD0 001B10B0 != 0 -> == 1 | `!st=0 busy 0x2` |
| S47 | 001BFFD0 e2 +2 half -> byte | `!st=0 d=1 presets` |
| S48 | 001BFFD0 +0x34 = 0x64 half -> byte | `!st=0 d=1 presets` |
| S49 | 001BFFD0 e1 counter word -> half | `!st=1 counters 0x0/0x10000` |
| S50 | 001BFFD0 e0+0x36 from cached flags (across 001EFE00) | `!st=1 001EFE00 stores +0x36` |
| S51 | 001BFFD0 fade +0x64 == 0 as bit compare | `!st=2 001C68C0 stores +0x64=0x80000000` |
| S52 | 001B9CF0 lane index word -> half | `9CF0:lane word 0x10000` |
| S54 | 001BBAE0 poll word -> byte | `BAE0:1/0/0x102` |
| S56 | 001CB360 +0x44 read before the calls (cached) | `!001C7420 stores +0x44` |
| S57 | 00183250 +0x235 signed byte | `183250:235=0x80` |
| S58 | 00183250 ph3 +0x38 read before the move (cached) | `183250:move stores +0x38 0.001` |
| S59 | 00187DE0 area byte -> half | `187DE0:area 0x15 next 0x1` |
| S61 | core scratchpad bound <= -> < | `9CF0:a1 at 0x70003fb4` |
| N73 | 1BBBF0 first 38A0 host mul (EE vs host) | `BBF0:cos 0x100000` |

Independent sweep 2 (its 51 open survivors, six full-only kills and six more quick survivors):

| Mutant | Change | Killed by |
|---|---|---|
| A01 | 001C2770 blend +0xD4 read before 001C9D50 (cached) | `blend reread D4` |
| A03 | 001C39F0 node+0x3C read before 001C6160 (cached) | `!001C6160 stores +0x3C` |
| A04 | 001C39F0 node+0xB0 read at entry (cached across calls) | `!mode=0 001026A0 stores +0xB0` |
| A05 | 001C3BE0 node+0xC0 read at entry (cached across calls) | `!00102718 stores +0xC0` |
| A06 | 001C69A0 bone count read once (cached across the loop) | `!quat_to_mat3 stores count 1` |
| A07 | 001C69A0 mat_rows re-loads the a rows per output row (aliasing) | `!parent self` |
| A08 | 001C2770 latch&8 E4 = 0x100 word -> half | `latch9 e6` |
| A09 | 001C2770 blend end E4 = phase<<8 word -> half | `blend e4 upper` |
| A14 | 001C2770 sub4 slide flags & 4 -> & 6 | `flags2 sub4 2800` |
| A15 | 001C2770 sub1-3 flags & 1 -> & 3 | `flags2 sub1 8000` |
| A18 | 001C39F0 n - 0x3C host sub | `!n-3C 001C6160=2 3C=0xbefb5aae` |
| A20 | 001C39F0 cvt.s.w of 001C6160 result host (round-nearest) | `!cvt 001C6160=0x1000003` |
| A21 | 001C39F0 post-step - half host sub | `!post-step sub` |
| A23 | 001C2770 sub4 / 180 host div | `sub4 speed bits 0x80800000` |
| A28 | 001C2770 yaw C4 + f0 host add | `yaw add` |
| A32 | 001B13F0 f12 < r host compare | `!a0=a1 limit 0x80000001` |
| A33 | 001C2770 sub0 probe != 0 -> == 1 | `latch1 sub0 8000` |
| A34 | 001C2770 0019AB20 == 0 -> (int) <= 0 | `wall 0x80000000` |
| A38 | 001B2140 room byte & 0xF | `1B2140:range 0x0/0x0/0x5/0x15` |
| A39 | 001B2140 area byte & 0x1F | `1B2140:range 0x2b/0x0/0x8/0x8` |
| A40 | 001B2140 third byte & 0xF | `1B2140:range 0x0/0x0/0x15/0x5` |
| A41 | 001B2140 sub byte & 3 | `1B2140:range 0x2/0x5/0x0/0x0` |
| O01 | 001BB560 +0x2E = 0 half -> byte | `!+0x2F preset near` |
| O02 | 001BB560 +0x2E = 1 half -> byte | `!+0x2F preset far` |
| O04 | 001BB860 st0 +0 = 1 byte -> half | `!st=0 +1 preset` |
| O08 | 001C02E0 D_00810766 = 0xFF byte -> half | `!st=0 845=0x20 767 preset` |
| O09 | 001C02E0 st2 +4 = 3 byte -> half | `!st=2 +5 preset` |
| O11 | 001BFFD0 e2+0x1C = 0x3C word -> half | `!st=1 001EFE00 stores e2+0x1E` |
| O16 | 001B9CF0 a1+4 = sub+1 byte -> half | `9CF0:11 a1+5 preset` |
| O17 | 001BBAE0 D_002821BC = 0 word -> byte | `BAE0:0 presets` |
| O18 | 001BBAE0 st+4 = 1 byte -> half | `BAE0:0 presets` |
| O19 | 001BFFD0 e0 half +0x28 test -> byte | `!st=1 d=2 e0+28=0x100` |
| O21 | 001BFFD0 st1 +0xD == 2 -> >= 2 | `!st=1 d=0x3` |
| O22 | 001BFFD0 st0 +0xD == 1 -> <= 1 | `!st=0 d=0` |
| O23 | 001BB860 sub4 001BB7F0 != 0 -> == 1 | `!st=1/4 answer=2` |
| O24 | 001BB860 sub2 001BB7C0 != 0 -> == 1 | `!st=1/2 answer=2` |
| O32 | 001B9CF0 lanes 0-2 compare with step result (no re-read) | `9CF0:0 node+0xC0 = op+8` |
| O33 | 001BB560 cos arg p.C4 read before 0011E2A8 (cached) | `!0011E2A8 stores p.C4` |
| O34 | 001BB560 area byte read before 001BBD60/001BA1A0 (cached) | `!001BA1A0 stores area 0x16` |
| O35 | 001BB860 node+0xB0 read before 001C6380 / method (cached) | `!001C6380 moves the node` |
| O36 | 001BB860 sub2 +5 = 3 (no re-read after 001BB7C0) | `!st=1/2 001BB7C0 stores +5` |
| O37 | 001C02E0 st1 +0x36 read before 001BF630/001FBD50 (cached) | `!st=1/1 001FBD50 stores +0x36` |
| O38 | 001C02E0 st1 +0x28 read before 001C6380 (cached) | `!st=1/1 001C6380 stores +0x28` |
| O39 | 001BFFD0 st1 flags read before advance/001BFF90 (cached) | `!st=1 001BFF90 stores +0x36` |
| O40 | 001BFFD0 st1 e2+0x1C read before advance/001BFF90 (cached) | `!st=1 001BFF90 stores e2+0x1C` |
| O41 | 001BFFD0 st2 +0x64 read before 001BFF90 (cached) | `!st=2 001BFF90 stores +0x64` |
| O42 | 001BFFD0 st0 +0xD read before bone_init_default_2 (cached) | `!st=0 001C63E0 stores +0xD` |
| O43 | 001BFFD0 e0 re-read from node+0x20 after the calls | `!st=1 001BFF90 stores +0x20` |
| O44 | 001BBBF0 node+0xB4 cached across the calls | `BBF0:0011E2A8 stores +0xB4` |
| O45 | 001BBBF0 0x38A0 not re-read after 00102948 (cached) | `BBF0:0011E2A8 stores 0x700038A0` |
| O47 | 001BF630 player arg replaced by D_008102B0 for +0xA8 | `!player record copy` |
| O48 | 001BB560 a2 == 0 -> (a2 & 0xFF) == 0 | `!a2=0x100` |
| O49 | 001C02E0 0x3B64 == 0 -> (& 0x7F) == 0 | `!st=1/1 b64=0x80` |
| O51 | 001BB860 kind byte +3 & 0x3F | `!kind 0x56 bit clear` |
| P01 | 00183250 +0x25C = 2 byte -> half | `183250:25D preset` |
| P02 | 00183250 +0x1F0 = 0 byte -> word | `183250:reset presets 0` |
| P06 | 00183250 decay host sub | `183250:decay 0x3e800000` |
| P07 | 00187DE0 +0x23C read before the calls (cached) | `187DE0:001EFD90 stores +0x23C` |
| P08 | 00187DE0 area byte read before the calls (cached) | `187DE0:001EFD90 stores area 0x15` |
| P09 | 00183250 +0xB4 read at entry (cached across the calls) | `183250:move stores +0xB4` |
| C01 | core accel base >= -> > | `BAE0:st 0x2ffffffc` |
| C02 | core scratchpad base >= -> > | `BAE0:st 0x6ffffffc` |
| C04 | core fault keeps the last fault (first-fault guard dropped) | `core_contract` (first fault) |


Scripted callees in these cases (001BA1A0, 001BA1F0, 001BC150, 001C6380,
001C68C0, 001C7420, 001D1F80, 001EFE00, 00178B90, 00102718, 0011DE90, a
probe that stores +0xF0, 001C9D50, 001026A0, 0011E620, 001C6160,
quat_to_mat3, 001BB7C0, the +0x4C method, 0011E2A8, 001FBD50, 001BFF90,
001C63E0, 001B12B0, 001EFD90; round 6 adds 001029C0, 00102B08, 001B1240,
0011E748, 001D0D60, 001C62C0, 00102C58 and quat_nlerp) are a test device:
they make a callee store a field that the original reads again after the
call, so a translation that kept the old value differs. Whether the real
callee ever stores that field was not established; the cases only show
that the translation reads again where the original does.

History. Rounds 3 to 5 ran 247, 321 and 430 of these mutants against the
test of their round (logs under `build/area01/math/r3`..`r5`); independent
sweep 3 found 33 open survivors (T01..T56, 56 new) and the final review
six more (Z02, Z04, Z09, Z10, Z11, Z15) plus the quick-only T18 / T22.
Round 6 pinned all of them, made the fail-stop contract a tested one
(`core_contract`), cut the quick run from about 33 s to about 15 s of
CPU (section "What the quick run contains"), re-pinned the 13 mutants
that cut exposed, and re-ran all 501.

Interpreter additions (test only): the MMI word interleaves PEXTLW / PEXTUW,
which the SDK matrix routines 00102798 / 001027E0 use. These are integer
operations, not part of the float model.

## 4. Functions already translated in the port (not duplicated)

| Address | Port module | Verified by (run this session, quick mode) |
|---|---|---|
| 001755B0, 001776E0, 00177CF0, 0017F320, 00188550 | `em_player_record_helpers.c` | `tools/test_player_record_helpers_reference.py`: PASS, 2,700 cases, every one of 38 conditional branches both ways (3 unreachable excluded), plus 82 route-capture runs |
| 0017F240 | `em_player_hang.c` | `tools/test_player_hang_reference.py`: PASS, 3,000 0017F240 cases bit-identical, 224/224 branch outcomes of 001647D0 + 0017F240 |
| 0017E250, 0017E510, 00182250 | `em_player_misc_workers.c` | `tools/test_player_misc_workers_reference.py`: PASS, 2,400 cases (entries `ahead`/`ahead_stack` = 0017E250, `above` = 0017E510, `aim` = 00182250), every one of 91 conditional branches both ways |

These tests run over the first-level captures. The routines are the same
boot code, so AREA01 adds no new instruction paths to them. A second
AREA01-capture oracle for them in this lane's file would have to bind their
host structures, so it was not added.

Overlaps and mentions (not translations of the same thing; nothing edited):

- 001C69A0: `em_status_models.c` `em_status_models_pose_001C69A0` is a
  recomposition over the status screen's own bone data model (through
  `em_pose_channels_matrix` and owner services). It is live for the status
  screen. `test_status_scene_reference.py` hooks 001C69A0 as a worker and
  does not execute it. This lane's `em_area01_math_001C69A0` is the EE-level
  routine, compared with the original instructions in section 3 on the
  cases listed there (and measured by the mutation sweep; nothing broader). A later session should decide whether the status
  screen should call the verified one.
- 00183250: `em_door.c` / `em_player.c` have a legacy "arrival walk-out"
  whose timer shape the comments say was copied from it. That is legacy
  code, not a translation.
- 001B13F0, 001C25E0, 001C2770, 001C3D60, 00187EC0: named only in comments
  of the legacy `em_enemy.h/.c`. 001BB560, 001BB860, 001BBAE0, 001BBBF0:
  named in the legacy `em_door.c/.h`, `em_camera.c` and
  `em_game_internal.h`. 00187DE0: comments in `em_sfx.h`, `em_player.c` and
  `em_player_floor.h`. 001C02E0: a comment in `em_main_loop_and_gap.h`.
  001CB360: an address in a table of `em_status_models.c`. None of these
  contains a translation.

## 5. What is left

- **Binding.** Nothing is wired. To run live, a host must give the view the
  port's single EE-byte storage (or a translated slice of it) and bind the
  dispatcher to the callees below, faulting on any other address. Callees
  already translated elsewhere in the port include 001026A0
  (`em_effect_original`), 001029C0 / 00102B08 / 00102BB0 (owner services),
  001026D0 (`em_loco`), 00102718, 001B1470, the SDK sin / cos / fabs / sqrt /
  atan2 (`em_sdk_math_original`), quat_nlerp / quat_to_mat3
  (`em_pose_host_workers`), 001C9D50 (`em_anim_runtime_rest`), 001B1240
  (script host), and 001749A0 / 00174A50 / 00175900 / 00178B90 through the
  player modules. Callees with no port translation found: 001028B8 /
  001028D0 / 00102760 / 00102738 / 00102948 / 001031E0 / copy_qw4 / 00102C58
  / 00103230 (SDK vector leaves; some exist as private helpers), 0019AB20,
  0019B4C0, 001C2540, 001C2690, 001C3DB0, 001C6160, 001C6380, 001C68C0,
  001B10B0, 001B12B0, 001B17A0, 001B1DE0, 001B6660, 001BA1A0, 001BA1F0,
  001BB520, 001BB7C0, 001BB7F0, 001BBD60, 001BC150, 001BFF90, 001AF890,
  001AFA90, 001AFC10, 001D0C80, 001D0D40, 001D0D60, 001D1F80, 001D3F50,
  001C7420 (owner services), 001CB2C0 (`em_anim_runtime_rest`), 001EFD90,
  001EFE00, 001E8B90, 001FB9F0, 001FBD50, 0017B490 and 00182F90. The census
  lanes for those subsystems should grep this list.
- **Callers.** The owners that reach these routines (00128C10 and siblings,
  0015B610, 00175900, the script caller 0024CCC8, 001CA5F0) are other lanes'
  work. None of them is translated for AREA01 yet.
- **001C25E0** is reached on the route only at a01_07 (two catches); the
  unit calls cover the other beats' nodes.
- **The side-beat and exit-only rows** of the three subsystems (0017C370,
  001C4720, 001CB480, 001BB520, 001C2430, 001C2540, 001C3DB0, 001C6160) were
  out of this lane's scope.

## Known gaps from the close-out spot check (2026-09-26)

The close-out verifier's fresh single-operation mutants that neither run kills. None is a mistranslation (the module passes each killing input); each is a test blind spot, recorded here instead of opening another sweep round.

- Y05 00187DE0 `lbu(D_00810700) != 0x15` changed to `< 0x15`. It survives quick and full mode (full: Y05 alone, EM_TEST_FULL=1, PASS). Killing input: area byte 0x16 or higher, where the original calls 001E8B90 and the mutant does not. This is a coverage gap in the area-byte range, not a mistranslation.
- Y06 001C2770 slide_push, the store of 1.0 to SPR 0x361C dropped. It survives quick mode. Full mode kills it only with the out-of-range flags-4 scripted case '4/0 fl=4 q=6'. Whether some in-range case (flags 6, 0x361C != 1.0 on entry) kills it is unmeasured.
