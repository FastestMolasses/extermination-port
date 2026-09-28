# AREA01 overlay owners (level-2 lane OVL)

Lane OVL of the level-2 (AREA01) side track, 2026-09-25 (fix rounds 1 to 5:
2026-09-25/26; close-out round 6: 2026-09-26). It covers the 14 AREA01
overlay functions that the AREA01 route census ran and the first level never ran
(decomp `build/s87/census/a01_delta.json`, `new_functions`, region
`overlay:AREA01`). All 14 are translated and compared with the original
instructions by `tools/test_area01_overlay_reference.py`. What that
comparison covers is exactly what section 3 states: the comparisons the
test makes, on the cases it runs.

Mutation record (section 3, "Mutation sweep, round 6"): 4,104
single-operation mutants were tried across the rounds, in these sets:
rounds 1-2 (`r1`, `r2`: 187), round 3 (`rev` 56, `hoist` 19, `res` 209,
`wid` 53), round 4 (`s1` = the round-3 review's S01-S62, `hoist4`, `r2x`,
`sign`, `neg`, `swap`, `drop`, `lit`), round 5 (`V` 8, `hdr` 549, `entry`
6, `zsign` 135) and the round-5 review's sweeps (V01-V66, W01-W56 and the
final review's 43: 24 named ones repeated, F01-F18 and Z24r).
Against the round-6 test, 11 no longer apply to the current text and 96
do not compile; of the 3,997 that run, **3,619 are killed** and **378 are
proven equivalent** (listed with their proofs in section 3). No named
survivor remains. The sweeps did not converge (each review so far found
new survivors: 12 after round 3, 8 after round 4, 4 after round 5), so
this covers these mutants only. Nothing is bound: no port code calls
these entries yet.
The binding chain comes after the first level (CLAUDE.md, "Current goal").

| File | What |
|---|---|
| `src/game/em_area01_overlay.h` | public API: 14 entries, the hook table, fault codes |
| `src/game/em_area01_overlay_internal.h` | memory view, fault latch, one typed wrapper per hook |
| `src/game/em_area01_overlay.c` | 0x823580 and the twelve functions with decomp C |
| `src/game/em_area01_overlay_826d40.c` | 0x826D40 (5,544 bytes, translated from the original code) |
| `tools/test_area01_overlay_reference.py` | original-instruction oracle and comparison |

Addresses are runtime addresses. The overlay is linked 0x40 below where it runs,
so each decomp/splat name is 0x40 lower. For example, 0x825350 is
`func_overlay_AREA01_00825310`.

## 1. Interface

- **Memory.** The module keeps no state of its own. Every read or write of
  original memory goes through the hook `bytes(ctx, address, size)`, which
  returns native bytes laid out like the original (actor records, globals,
  overlay data, the scratchpad at 0x70000000). Pointers stored in records stay
  original 32-bit addresses, and they are resolved again at each point where
  the original dereferences them. Fields are re-read exactly where the
  original re-reads them. The test compares the calls and their
  arguments, the memory at each call entry (so every store made before a
  call), the memory accesses between each two calls one for one and in
  order (address, size, and whether the access changed memory), and the
  final memory. Where the original loads the operands of one C expression
  in a fixed order, the translation loads them in separate statements in
  that order, because C does not fix the order in which function arguments
  and operands are evaluated. Section 3 lists what is compared and the
  places that rest on a proof or on a callee's definition instead.
- **Callees.** Each boot function, the overlay functions outside this set
  (0x8282F0, 0x8287C0) and the actor's own +0x4C callback is a hook named by
  its original address. Pointer arguments are passed as original addresses.
  Each hook returns >= 0 on success and writes the callee's original result to
  `*result`.
- **Fail-stop.** A fault is latched when an address cannot be mapped (fault
  address = that address, code 5), when `bytes` itself is NULL (the same),
  when a reached hook is NULL (the original callee's address, code 1; for
  the +0x4C callback, the function read from +0x4C), or when a hook returns
  a negative value (the same address, code 2). Any status >= 0 is a
  success. After a fault, no further hook runs, `bytes` is not called
  again, writes are dropped, the latched fault is kept, an op09 entry does
  not write its result (round 5; before, it wrote whatever the rest of the
  run computed from the zeroed sink), and the entry returns -1. A fault
  already latched on entry makes the entry return -1 at once, with no
  effect; so do a NULL hook table and a NULL fault pointer. `ctx` reaches
  `bytes`, every hook and the callback unchanged (checked since round 6).
  Not covered by any check: a hook that re-enters an entry with the same
  fault record (section 3's round-5 proof of first-fault-wins assumes
  none does). Arguments are evaluated before the latch is tested, so an
  unmapped read inside an argument also stops that call. The test found an
  earlier macro form that got this wrong, and it was replaced by the
  per-hook wrappers. Section 3 ("Hook contract") says how each part is
  checked.
- **EE arithmetic.** Every float operation uses `em_ee_float.h`: EE add/sub
  with the operand pre-trim, truncating mul/madd, nearest div, and the
  compare-key compares.

## 2. What each function does (behaviour, from the code)

Roles are given only where the code or a capture shows them. The captures
supply the node addresses and placements (SECOND_LEVEL_ROUTE.md section 1).

| Entry | Ground truth | Behaviour |
|---|---|---|
| 0x823580 | original code (decomp C byte-identical and linked from C since 2026-09-28: the overlay link now places its jump table at the original address) | Owner of the shaft door (placement [12], node 0x7ABD10). **+0x04:** 0 calls 001BBDA0 and sets +0x00 = 1; 1 runs; 2 and 3 free the node (001AFC10). **Running:** +0x05 selects one of seven steps. Step 0 is the kickoff: 001BBE40(self, self+0x1F0, mode), with mode 0 when the story byte D_008107D9 is 0x81 (the next step is then 3) and mode 1 otherwise (the next step is 1). Step 1 waits on 001BC0E0, then starts script 0x829860. Step 2 waits on 001BC0E0; if the story byte is 0 or 0x80 it stores 0x80, starts script 0x8298E0, ticks it once and moves to step 6; otherwise it clears +0x0B and returns to step 0. Step 3 waits on 001BC0E0. Step 4 calls 001BC240. Step 5 waits on 001BC290, then returns to step 0. Step 6 ticks the script until it ends, then clears +0x0B and +0x05. **Every running frame then** sets byte +0x0B of the node whose address it read from its own +0x1C at entry: 1 when the story byte is 0x81, else 0. It then calls 001BC300. |
| 0x825130 | decomp C | Script op09 callback (owner, script block, record). Phase 0 chooses clip 6 or 5 from 001B1380(D_00810350, self+0xB0, yaw), blend 20. Phase 1 turns yaw (+0xC4) toward the point D_00810350+0x10/+0x18 (001B1240 goal, 001B12B0 at the record's +0x0C rate). It returns 1 when the turned yaw equals the goal. |
| 0x825240 | decomp C | Script op09 callback. Phase 0 chooses clip 5 when the wrapped (yaw − record +0x24) is > 0, else clip 6. Phase 1 turns yaw toward the record's +0x24. On arrival it plays the record's +0x1C clip (blend 20, frame 0) and returns 1. |
| 0x825350 | decomp C | Owner of the control-room NPC (placement [36], node 0x7B0390). **+0x04:** 0 is set-up (it goes to 3 when D_0081075A != 0): 001B10B0, 001C63E0, 001BA8E0, then +0x30 = 0x82A7A0 and +0x58 = D_0028A5C4. 1 runs the talk machine that the story byte selects (0 → 0x8254B0, 0x80 → 0x825590, 0x81 → 0x825670), then calls 001BA580, 001C64F0(1.0), 001C68C0, 001B17A0 and the +0x4C callback. 2 is idle. 3 calls 001BA540 and frees the node. |
| 0x8254B0 / 0x825590 / 0x825670 | decomp C | The three talk machines. Step 0 copies yaw to block +0x44 and plays clip 1 at blend 40 (0x8254B0 also zeroes +0x28). Step 1 waits for bit 2 of +0x0B (the route capture shows the conversations start on the player's Use; SECOND_LEVEL_ROUTE.md), then starts script 0x829E60, 0x829FA0 or 0x82A660. Step 2 ticks the script. When it ends, 0x8254B0 and 0x825670 return to step 1, clear +0x0B and play clip 0 at blend 30. 0x825590 instead stores D_00810759 = 0xFF, calls 001C4760(2, 1), stores **D_008107D9 = 0x81** and clears +0x05, +0x06 and +0x0B. |
| 0x8261A0 | decomp C | Owner of placements [41]/[42]. It sets +0x00 = 1 while +0x04 == 0, then calls 0x826200 when +0x0D == 2 and 0x826440 when +0x0D == 3. +0x0D is read again after the first call. |
| 0x826200 | decomp C | **+0x04:** 0 sets +0xC0 from the story bytes 75E/760/784 (pi/18, pi/3 or 0) and calls 001B0FD0. 1: once D_00810760 != 0xFF and D_008107E0 != 0, it starts script 0x82B4D0, sets +0x240 = −1 and +0x28 = 0, then plays looping sound 0x8A9 (001FC3C0, 3000, 4096) until the script ends (001FC520); after that comes the shared tail. 2 and 3 free the node. |
| 0x826440 | decomp C | **+0x04:** 0 sets +0xC0 (0 or −pi/3) and calls 001B0FD0. 1 starts script 0x82B0D0 when all of these hold: 001BA1C0(self, 8) is 0, scratchpad byte 0x70003B8D is 0, 001B1EA0(0, D_00810350, 0x82CC60, 4) is 1, D_00810350+4 <= 2.0, and 00182BF0(0x8102B0) is 0. D_00810350 is 0x8102B0 + 0xA0 (the words em_fan_original.h reads as the player actor's X, Y, Z). It then sets D_008107E0 = 1 and D_00810374 = 0. While the script runs: D_008107E0 == 0xE0 ramps D_008104A4 to 0.3 at tick 60 and 0.6 at tick 200 (timer +0x240); D_008107E0 == 2 copies the +0x10/+0x18 point to D_00810350 once; +0x2A counts up. At the end it sets D_008107E0 = 0xFF and D_008104A4 = 1.0. Then the shared tail runs. 2 and 3 free the node. |
| (shared tail) | decomp C | 001C6380; header byte +0x01 = 1 when 001B1EA0(0, D_00810350, 0x82CC20, 4) or, failing that, the same call with 0x82CC60 returns 1; then 001B1B70, 001A2370(self, self+0xD0) and the +0x4C callback. |
| 0x8267C0 | decomp C | Owner of placement [45]. **+0x04:** 0 calls 001B0FD0; on 0 it calls 001C6380 and sets +0x00 = 1. 1: while 001BA1C0(self, 15) is 0 and 001BA1C0(self, 7) is non-zero, it starts script 0x82B590 once 001B1EA0(0, D_00810350, 0x82CCA0, 4) and 001B1EA0(2, …) are both non-zero, then ticks the script until it ends. It draws when 001B17A0 != 0. 2 and 3 free the node. |
| 0x826CF0 | decomp C | Owner of placement [37] and of a sub-0 group. Header byte +0x03 == 1 calls 001C5C90; any other value calls 001C4820. |
| 0x826D40 | decomp C (byte-identical, links from C) | Deferred-group owner (three nodes per sub). Its fields are described in the file header. **+0x04:** 0 is set-up: timers, steps, a random +0x28, the B-record angle, and a class-0xC companion node (001AFA90, behaviour 001C5680) stored at +0x220. It moves to 0x64, or to 4 when 001BA1C0(self, 6) is non-zero. 0x64 waits for that result, then goes to 4. 4 sweeps: A+0x74 (clamped to ±1.134464) and the phase +0x1FC drive B+0x78 = −0.829 + 0.305·sin(phase), with random re-seeding and sounds 0x423/0x424/0x428. While +0x208 <= 0 it also calls 0x8282F0 every frame and moves to 1 once +0x204 is non-zero. 1 follows the record at +0x204: from the flattened, normalised direction between A (+0xC0) and that record (+0xB0) it steps A+0x74 by ±0.011635528 or, inside that dead band, sets it from atan (001B1470-wrapped); it steps B+0x78 toward atan(y / sqrt(x² + z²)). When +0x200 reaches 14 and 0x8282F0 returned non-zero it plays sound 0x425, calls 0x8287C0 and requests an effect through 001EFD90 whose id depends on 0x700031D8 and the records at 0x700031D0/0x700031D4 (0x80000003/06/07/26/2C/67; 0019B6C0 decides one branch). It returns to 4 when +0x2A goes negative. 2 winds the phase to −pi/2, then fades the companion (lane chosen by +0x224) over +0x21C frames. 3 and unknown values free the node. In states 4 and 1, while +0x208 is positive it counts down; below 0x1F it fades the companion's +0xA0..+0xAC words instead of sweeping. |
| 0x828850 | decomp C | The partner of 0x826D40 (its +0x18 points at the 0x826D40 node). **+0x04:** 0 calls 001B0FD0; on 0 it sets +0x34 = 1 and +0x00 = 1, and when 001B11E0(+0x9A) is set it sets partner +0x04 = 2 and its own +0x04 = 3. 1: when +0x36 != 0, it sets +0x00 = 2, partner +0x04 = 2 and partner +0x21C = 0x5A, then either 001EFE00(0x80000045, self) → sound 0x426 and state 2, or state 3. Otherwise it animates and draws. 2 counts +0x28 up to 10 (sound 0x427 at 10), then calls 001B1190(+0x9A) and draws. 3 and above free the node. |

### Facts this lane measured

- **The shaft door's +0x1C node is r13.** In all 11 AREA01 captures, the door node
  0x7ABD10 holds 0x7AC000 at +0x1C (the 00158D30 node, placement [13]). Node
  0x7AC000 has +0x0B = 1 in exactly the captures where D_008107D9 = 0x81
  (a01_05, a01_06) and 0 in the others. That matches the door's per-frame tail
  above, and it settles the open lead in SECOND_LEVEL_ROUTE.md section 2
  ("that node being r13 is not proven"): the shaft door's +0x1C is node r13,
  and the door's tail is what sets r13's +0x0B from the story byte. The
  decomp's SECOND_LEVEL_ROUTE.md still lists the lead as open; the lead of the
  side track marks it settled there, citing this section. This lane does not
  edit that document.
- **D_00275B40 while a behaviour runs is actor + 0x110.** The pool loop 001AFD70
  calls 001CB590(actor, 0x2F0, …) before each behaviour. 001CB590 stores
  D_00275B48 = actor and tail-calls anim_bone_array_setup, which sets
  D_00275B40 = D_00275B48 + 0x110. All three are compiled C in the decomp, and
  this was read from their definitions. The end-of-frame captures show
  0x8102F0, which is 0x8101E0 + 0x110: the frame's last context is the camera
  block. The capture cases therefore set D_00275B40 = node + 0x110 before they
  call 0x826D40. For the three 0x826D40 nodes this makes A = +0x118 and
  B = +0x11C. In the a01_01 capture, A's +0xB0/+0xB8 is an axis direction (0, ±1) and
  B+0x78 is −1.1345 for all three.
- 0x8282F0 and 0x8287C0 are not reached on the recorded route (census), so
  0x826D40's state-1 paths never ran there. The captured nodes sit in
  state 0x64.

## 3. Verification

`python3 tools/test_area01_overlay_reference.py` (port root, macOS arm64 or Linux).
It compiles the module into `build/area01/ovl/area01_overlay.dylib` with
`-std=c11 -Wall -Wextra -Werror -Wpedantic -ffp-contract=off`.

- **Oracle.** FallEE is the shared EE interpreter with the measured float model
  (`tools/test_player_fall_reference.py`, docs/EE_FLOAT_MODEL.md). It runs the
  original overlay code resident in the recorded RAM
  (`../Extermination/build/s87/route_a01/<beat>/eeMemory.bin` + `scratchpad.bin`)
  for the 11 beats where AREA01 is resident (a01_00..a01_06, a01_s0..a01_s3;
  a01_07 ends in AREA00). Before any case runs, the test asserts that the
  overlay text in each image equals the user's `extract/OVERLAY/AREA01.BIN` and
  that the boot text below 0x241000 equals the pinned ELF.
- **Callees.** Each callee is intercepted at its entry and logged with its
  arguments. The pure helpers run as original code nested in the oracle:
  00102760/1028D0/102948/102958, 0011DBB8/11DF78/11E2A8/11E520/11E748,
  00122BB8, 001B1240/12B0/1470 and 001BA1A0. All other callees (sound,
  effects, script ticks, model, pool, quad and collision calls, the +0x4C
  callback, 0x8282F0/0x8287C0) are stubbed with each case's scripted result.
  Some stubs also write memory ("scribble") to test the re-reads after a
  call. The native module runs over a byte copy of the same start image. Its
  hooks must be called in the same order with the same arguments, and each
  one then replays the original callee's writes and result.
- **Memory at every call entry.** The oracle logs, in order, every store to
  RAM or the scratchpad: the function's own, those of helpers that run as
  original code, and the writes of scribbling stubs. It also records where
  that log stood when each call was entered. At the entry of every native
  hook or callback, and **before** the callee's writes are replayed, the test
  rebuilds the oracle's memory at the entry of the same original call (start
  image plus the log up to that call) and compares it with the native
  memory. Any difference fails the case with the call index, the callee and
  the first differing address. The default run compares the dirty set: every
  byte the oracle has stored so far, plus every range the native module was
  handed through `bytes`. That is sufficient because the module reaches
  memory only through a pointer that `bytes` returned for exactly the size
  requested (`a01_at`), and replayed callee writes are oracle stores. Every
  other byte therefore still holds the start image on both sides.
  `EM_TEST_FULL=1` compares all 32 MiB and the whole scratchpad at every call
  entry. A wrong or missing store is therefore caught at the first call that
  follows it, even when the callee later overwrites that memory. This was the
  review's blocking issue: before, only the final memory was compared, and
  replaying a callee's writes could hide a wrong store.
- **Memory accesses between calls** (round 4; round 3 compared only
  their byte sets). The oracle logs each load and store the function
  itself makes (not its callees', not its stack's), in program order, with
  its address, its size and whether it changed memory. It keeps a separate
  log for each stretch between calls: entry to the first call, each call
  to the next, the last call to the return. The native module reaches
  memory only through `bytes`, and it uses the bytes of one request before
  it makes the next request or call (`a01_at`). The test logs each request
  in order, and marks it as changed when the bytes it handed out differ at
  the next request or call. In every case, in both runs, the two logs must
  be equal in every stretch, entry for entry. So each of these fails: a
  read moved before a call or before a store; a value kept from an earlier
  read instead of read again; a read of another width or at another
  address; a read or store the original does not make, or a missing one;
  and two accesses in the other order. Before round 4, the round-3 check
  compared only the union of the bytes accessed in each stretch. It could
  not see a cached or narrower read of bytes that were also stored or read
  in the same stretch. The round-3 review's survivors S37, S38, S41, S42,
  S45, S48 and S57 are of that kind.
  - What the log does not show: whether an access that changed nothing is
    a load or a store of the value already there. The `bytes` hook does
    not say which it is. Two such accesses to the same bytes that trade
    places show only through the values the function goes on to use.
  - Excluded: the door's seven-word step table at 0x82CB80, which the
    original loads for its jump. The C switch encodes the table instead.
    The test asserts that the table in every capture equals AREA01.BIN,
    and the step cases compare the dispatch itself.
  - Round 4 changed the translation to satisfy the check. Six places
    loaded their operands in an order the original does not use. That
    order was the host compiler's choice for an unsequenced C expression.
    The six are: 825130's phase-1 arguments (the original loads the rate
    before the yaw), 825240's phase-0 and phase-1 arguments, the phase
    advance (the step before +0x1FC), state 2's wind (the step first), and
    the two fades' store of 0x70003A20 (the value before the companion
    pointer). Each load is now its own statement. In 828850's state 2 the
    translation loaded +0x28 twice, once for the `< 10` test and once for
    the increment. The original loads it once, so the translation now
    does too. None of these changes a stored value or a call.
- **Stops** (round 3; the map and the alignment rule are from round 4). The
  harness models main RAM (0x00000000-0x01FFFFFF) and the scratchpad; the
  native side's `bytes` serves exactly these. The original stops when the
  function's own load or store leaves that map, and when a halfword or word
  access is misaligned: on the EE the first is a TLB miss or an unmodelled
  window, and the second raises an address error. The native module must
  then have latched fault 5 at the same address, after the same calls and
  with the same memory. Until round 3 the oracle also accepted
  0x02000000-0x3FFFFFFF as a RAM mirror while `bytes` refused it, so a
  case reaching those addresses would have failed as a mismatch rather
  than compared a stop. No existing case did. The designed 001AFA90
  pointer cases, the companion-alias cases and the partner-overflow cases
  reach a stop.
- **Floats cross the harness as bit patterns** (round 3). A float hook
  argument arrives as a `c_float` subclass, so ctypes copies the
  argument's bytes instead of converting it to a Python float. Float
  results are written back as 32-bit words. A signalling NaN (exponent
  255, bit 22 clear) therefore arrives unchanged in both directions. Until
  round 2 the harness quieted them (0x7F800001 arrived as 0x7FC00001).
  That could only cause false failures, and the NaN cases then used only
  0x7FC00000 and 0x7F800000. The 12 signalling-NaN cases below fail when
  run through a scratch copy of the test with the old path
  (`build/area01/ovl/round3/old_float_path.py`: 12 of 12 fail) and pass
  with the new one.
- **After the last store.** All 32 MiB of RAM and the 16 KiB scratchpad are
  compared, along with the op09 return value and a clean native fault
  state. A self-check also confirms that the store log rebuilds the oracle's
  final memory exactly, so the log misses no store. Since round 6 the
  oracle runs on a bytearray that records every range assigned to it
  (`WatchedMemory`; the shared EE core writes memory only by slice
  assignment), and the self-check compares the rebuilt image with the
  oracle's memory over those ranges and the log's; every other byte is the
  start image on both sides, so this gives the same answer as comparing all
  32 MiB. The native-versus-original comparison is still all 32 MiB.
- **Harness memory handling** (round 6; what is compared did not change).
  Each worker process keeps its four images (the case's start image, the
  oracle's working copy, native and expected memory) from case to case
  (`Images`) instead of copying 4 x 32 MiB for every run. After a run that
  passed, it copies back from the start image exactly the bytes that run
  can have changed: the oracle's logged stores, the ranges any code path
  wrote into the oracle's memory, and every range the native side was
  handed; the full final comparison has just shown native and expected
  memory equal everywhere else. A failing run, a change of capture or any
  other load of the buffers makes the next case copy everything again.
  Cases are sent to the workers in chunks of 16 from the same capture.
  Before the switch, `build/area01/ovl/round6/ab.py` ran the round-5
  harness and the round-6 harness side by side on the same 2,119 cases
  against nine libraries: the unchanged translation; two that write
  outside the range they asked `bytes` for (`a01_w8` writing two bytes, and
  `a01_w32` flipping a byte 0x100 past its word when it stores 1.0); and
  S04, S37, S57, W35, W10 and W22. Every per-case outcome (the errors, the
  executed words, the counts and the calls) was identical for all nine
  (0, 843, 343, 13, 71, 85, 0, 249 and 97 failing cases, `ab.txt`).
- **Argument registers.** Every hook's arguments (bit patterns for floats)
  are compared at every call. Before each helper that runs as original code
  runs for real, it is rehearsed with every argument register its hook does
  not pass (a0–a3, t0–t3, f12–f19, and the upper halves) set to a poison
  value. Its writes are then undone. The rehearsal must give the same writes
  and the same result, which proves those helpers read nothing but the hook's
  arguments. For stubbed callees, the hook's argument list is the set of
  registers that the callee's decomp definition reads (section 4).
- **Poisoned second run.** Every case runs a second time from a poisoned
  start image, which changes two kinds of byte. The first kind is every byte
  the function writes before it reads it; it starts with a value that
  differs from the first value stored there. The second kind is every byte
  the function never touches that shares an aligned 8-byte group with one of
  its stores. With this image, a store that is moved across a call, dropped,
  aimed at the wrong byte or made the wrong width changes memory that the
  call-entry or final comparison sees, even where the captured bytes happen
  to equal the stored value. A case has no second run when the function
  itself stores nothing on that path.
- **Coverage is enforced.** The run fails if any reachable original word was
  not executed, in both modes.
- **Cases** (default run / `EM_TEST_FULL=1`):
  - 231 capture cases: every owner node of every beat exactly as captured,
    plus the talk machines and op09 callbacks on the NPC.
  - 1,773 designed cases, all of them in the default run (10,272 with
    EM_TEST_FULL=1, which runs all 256 values at each byte site). They
    cover:
    - every state and step value, with both results of every gating
      callee;
    - **every byte the functions dispatch on or compare**, at 36 sites: the
      state byte +0x04 of the door, the NPC, 8261A0, 826200, 826440,
      8267C0, 828850 and 0x826D40, the step byte +0x05 of the door, the
      three talk machines, 826200, 826440 and 8267C0, the op09 phase
      bytes, 8261A0's +0x0D, 826440's +0x06 and +0x07, 826CF0's +0x03,
      the story bytes
      D_008107D9 (door step 0, door step 2, the NPC), D_0081075A,
      D_0081075E, D_00810760, D_00810784 and D_008107E0 where they are
      tested, the pad byte 0x70003B8D, and the surface byte +0x1A in
      0x826D40's period effect. Each gets every handled value, every
      handled value with each of its eight bits flipped, and 0x7F, 0x80
      and 0xFF. A test that masks, widens or sign-extends the byte maps
      one of these values onto a handled one. EM_TEST_FULL=1 runs all 256
      values at each site;
    - **every test of a callee's integer result**, at 35 sites: door steps
      0 (both story branches), 1, 2 (both), 3, 5 and 6; the talk machines'
      step 2; 825130's pick; 826200's step 1 and quad tail; 826440's step
      1, the gate's 001BA1C0, 001B1EA0 and 00182BF0 tests and both quad
      tests of the tail; 8267C0's set-up, bit 15, bit 7, both quad tests,
      step 1 and draw; 828850's set-up, 001B11E0 and 001EFE00; 0x826D40's
      set-up (001B0FD0 and 001BA1C0), 0x64 and state 2; and the tracker's
      0x8282F0 result (`== 2`, and `!= 0` at the period) and 0019B6C0.
      Each gets 2, 3, 0x100, 0x10000, 0x80000000 and 0xFFFFFFFF, plus
      0x101 and 0x10001 at the three `== 1` quad tests and 0x102 and
      0x10002 at the tracker's `== 2`. The original tests each result as a
      whole word against 0, 1 or 2. Some callees cannot return these
      values: 001BA1C0's decomp C returns only `D_00810758[a1] == 0xFF`,
      0 or 1. So these cases pin the form of the original's test, not a
      reachable input;
    - **001AFA90's companion pointer** (tested against zero), as
      0x80000000 | node, 0xC0000000 | node and 0x80000000 (bit 31 set),
      and as 0x40000000 and 0xFFFFFF00 (non-zero with a zero low halfword
      or byte). The original stores through it, the harness maps no
      address there, and both sides stop at the same store (see Stops);
    - story byte D_008107D9 = 0, 1, 0x7F, 0x80, 0x81, 0x82 and 0xFF in the
      door's steps and tail and in the NPC dispatch;
    - the talk machines' use byte +0x0B = 0x01, 0x02, 0x04, 0xFB and 0xFF
      (the test is bit 2 alone);
    - 0x8261A0's +0x0D = 1, 4 and 0xFF next to 2 and 3;
    - the compared word exactly on the constant (or both keys equal) at
      these float compares: the phase wrap `!(phase <= pi)`, sweep_a's
      two clamps, the idle step sign and both idle clamps, the tracker's
      side band `!(f2 <= STEP)`, heading magnitude, heading `x < 0`, the
      A clamps `A+0x74 < -LIM` (round 6) and `!(A+0x74 <= LIM)`, both
      elevation steps, the B clamps `B+0x78 < -LIM` and
      `!(B+0x78 <= BMIN)` (both round 6), 825240's `0 < wrapped` and
      826440's height `<= 2.0`. These are the compares whose `<`/`<=`
      exchange the sweeps tried (W28-W42, F01, F12, F17, O1, O2); all of
      those exchanges are killed. The side band's `f2 < -STEP` and state
      2's `phase < -pi/2` and `!(v <= -pi/2)` had no such mutant, and no
      claim is made that a case sits on their boundary;
    - EE-versus-host operands: for every EE add, sub, mul, madd and div in
      the translation, a case where a host operation would store or pass a
      different word, except the four operations proven equivalent below;
      and for every EE compare that reads a raw memory word or a callee
      result, a case with a denormal or an exponent-255 word on which a host
      compare decides differently (see the mutation sweep for the list);
    - a NaN/Inf operand with an accumulator of −1e38 in the tracker's side
      MADD, where an EE add of an EE mul would give a different result;
    - denormal, NaN and Inf goal/turn pairs for the op09 EE equality, which
      are equal on the EE and unequal for a host `==`;
    - negative random words and negative clip halfwords;
    - header byte +0x00 = 3 on paths that do not store it;
    - the 0x826440 ramp timers at 0xC7, 0xC8 and 0xC9, and +0x2A = 0x00FF
      and 0xFFFF (the carry out of the low byte);
    - the tracker's +0x200 word at 0xFFFFFFFF, 0x80000000, 0xFFFFFFF2 and
      0x7FFFFFFF in states 4 and 1 (its first test is an unsigned `!= 0`,
      the period test a signed `>= 0xE`), and +0x28 = 0xFFFF at the hand-off;
    - +0x36 raised with +0x208 = 0xFFFFFFFF or 0x80000000 in states 4 and 1
      (the re-arm needs +0x208 == 0), and random words that make the
      re-arm's values 4 and 8;
    - the frame word 0x70003B68 = 0x10, 0x1F, 0x20, 0x3F and 0x80 (the
      sound tests use its low six bits);
    - +0x2A landing exactly on 0 or −1 at the tracker's return to state 4;
    - A records whose +0x74 aliases the pointer word at block+8, so the
      sweep's re-reads of A follow the new pointer (countdown sweep and both
      idle sweep directions, the new A reading 2.0 or 0xFFC00000);
    - a companion record placed so that its +0xA0 is the scratchpad word
      0x70003A20, which the countdown and state 2 fades store and then read
      again;
    - a scribbling callee at every re-read point of 0x826D40's period effect.
      On the 0x80000007 and 0x80000006 branches the pointer at 0x700031D4
      changes after 001EFD90, after 001028D0 and after 00102760, and on
      0x80000006 byte +0x00 of the record changes as well. The pointer at
      0x700031D0 changes after 0019B6C0; the words at 0x700031D0, 0x31D4 and
      0x31D8 change after 008287C0; 0x31D0 and 0x31D8 change after
      001FBD50. On 0x80000006, the points at 0x700031A0 and 0x70003190 also
      differ in z and w, so the zero stored at 0x7000391C is visible;
    - the period effect's kind word 0x700031D8 = 3, 5, 0x101, 0x80000001
      and 0xFFFFFFFF (the test is `kind == 1`, not a bit test), the record's
      +0x02 = 0x01, 0x0F, 0x10 and 0xE0 (the 0x80000007 test is its low five
      bits), and +0x00 = 0x00, 0x04 and 0x0C on the 0x80000006 branch (its
      store is `+0x00 | 2`);
    - the tracker tail's re-read of B+0x78 after 0x8282F0: the stub changes
      it to values outside the clamp range on which an EE and a host
      subtraction disagree, and to ±MAX, where EE DIV saturates the
      quotient and a host division overflows;
    - more scribbling callees (round 3): 001B1EA0 moves the 826440 gate's
      height D_00810350+4 across 2.0 (1.0 to 5.0, 5.0 to 1.0, 2.0 to
      2.0000002 and back), and 001BA1C0 flips 0x70003B8D both ways;
      001BBE40, 001BC0E0 and 001BC240 change the door's step byte before
      the step increments, and 001BC0E0 and 001BC290 change the story byte
      before step 2's gate and the tail;
    - 0x826D40 state 2 at −pi/2 with +0x21C = 0xFFFFFFFF, 0x80000000,
      0x7FFFFFFF, 0x100 and 0x10000 (the count test is a signed `> 0`),
      and +0x224 = 0x100, 0x10000, 0x80000000 and 0xFFFFFFFF (the lane
      test is a whole-word `!= 0`); +0x36 = 0x100 and 0x8000 in 0x826D40's
      states 4 and 1 and in 828850's state 1;
    - words a function stores and then tests before the next call, with
      values whose low byte or halfword alone would decide the test
      differently: 826440's ramp timer +0x240 = 0x1003B, 0x100C7,
      0x8000003B and 0xFFFF003B; 828850's +0x28 = 0xFF09, 0x8009 and
      0x0109; 0x826D40's idle +0x28 = 0x81, 0xFF01, 0x100 and 0x8000; the
      tracker's +0x200 = 0x10000, 0x8000, 0xFFFF and 0x1000D and its +0x28
      = 0x8000, 0xFFFE, 0x7FFF and 0x00FF;
    - signalling NaNs 0x7F800001, 0x7FA00000 and 0xFF800001: as float
      arguments read from memory (825130's yaw in phases 0 and 1, 825240's
      record yaw and rate), and as stubbed float results that are stored
      or passed on (001B1240 and 001B12B0 in 825130, 001B12B0 in 825240,
      and 0011E520 into 001B1470 into +0x1FC in the tracker's return to
      state 4).
    - round 4, the round-3 review's killing inputs, widened to every site
      of the same kind: the door's step 0 on both story branches with
      001BBE40 writing +0x05 (0x40, 0x03, 0xFF); the NPC's +0x0D = 0x7F,
      0x80 and 0xFF in state 1 (001BA580) and 0x80 and 0xFF in set-up
      (001B10B0, 001BA8E0), which pass the byte zero-extended; the
      countdown's +0x20C and +0x214 at 0, 0xFFFFFFF0 and 0x80000000 (signed
      `> 0` from the register) and at 0x10000 and 0x8001 (positive words
      whose low halfword is 0xFFFF or 0x8000);
    - a companion placed so that its +0xA0, +0xA4, +0xA8 or +0xAC is the
      pointer word +0x220 itself, at all seven sites that store the four
      colour words (idle, tracker, countdown sweep, both fades, both state-2
      lanes), 28 cases: the original loads +0x220 again for every store and
      follows the new pointer (to 0xA4, or to an unmapped address where it
      stops);
    - the idle sweep with A placed so that A+0x74 is the step +0x1F4 itself
      and steps 0xFF800001, 0xFFC00000 and 0xFF800000; the tracker with
      D_00275B40 = node + 0x20, so that the A pointer is the word at +0x28
      and the `+0x2A -= 4` store moves it (both clamp directions); the
      period effect with the hit pointer 0x70003888, so that hit+0x28 is
      0x700038B0, which the first copy overwrites (kinds 1 and 3 on the
      first copy, and the second copy after a non-zero 0019B6C0);
    - every EE negation on words where a subtraction from +0 or a raw sign
      flip gives another word: sweep_a's two clamps with +0x210 = +0, 1,
      0x7FC00000 (upper) and +0, 1, 0xFFC00000, 0xFF800000 (lower; EE ADD
      gives +MAX for any NaN and -MAX for -Inf, so only -Inf reaches the
      lower clamp with an exponent-255 step); the re-arm's and the
      countdown reseeds' negations of +0x210 and +0x218 with +0, 1 and
      0x7FC00000; the idle clamps with +0x1F4 = 0x7FC00000, 0xFFC00000,
      0xFF800000, +0, 1 and 0x80000001; the tracker's side negations of
      A+0xB0 and A+0xB8 (an exponent-255 A+0xB0 times a direction of 1e-10,
      and all four side operands zero or denormal); the heading's
      negation of the 0011DBB8 result at +0, 1, -0, 0x7FC00000 and
      0xFFA00000;
    - words next to 0x7FFFFFFF where a signed addition in a translation
      would overflow: 826440's ramp timer 0x7FFFFFFF, and 828850's partner
      pointer +0x18 at 0x7FFFFFFC-0x7FFFFFFF, 0x7FFFFDE4 and 0x7FFFFF00 in
      states 0 and 1 (the original stops at the partner store).
    - round 5: the idle hand-off's whole-word test `+0x204 != 0` with
      +0x204 = 0x80000000, 0xFFFFFFFF, 0x100 and 0x10000 (V46; a signed
      `> 0` or a narrower read decides differently); the re-arm's
      `+0x208 == 0`, which the original reads again after 0x8282F0, with a
      0x8282F0 that moves +0x208 from 0 to 1, 0x7FFFFFFF or 0x80000000, in
      states 4 and 1 (a signed `>= 0` or `<= 0` decides differently); and
      the random word 0xC0000000 at the re-arm and at the countdown
      reseeds of +0x20C and +0x214. It scales to -15 (0xFFFFFFF1): bits 2
      and 3 clear, bit 7 set, so a reseed mask of `& 0x84` or `& 0x88`
      negates the step where the original does not (V41, which before
      round 5 only the full-mode random case '826D40 s1 31' killed).
    - round 6 (the round-5 review's sweep 3, W35, W39, W40 and W45): the
      tracker with A+0x74 = -LIM (0xBF91361E) itself at the A lower clamp
      (no clamp, no `+0x2A -= 4`); with B+0x78 = 0xBF8FB8D9 and an atan
      result of -LIM, where the EE step `sub(cur, STEP)` lands exactly on
      -LIM at the B lower clamp; with B+0x78 = 0xBF09051E and an atan
      result of 0.5, where `add(cur, STEP)` lands exactly on BMIN
      (0xBF060A92) at the B upper clamp (both sums checked with
      `tools/ee_float_model.py`); and a phase of -0 (0x80000000) into the
      sine, in the idle sweep (phase -0, step -0: EE add gives -0) and in
      state 2's wind (phase -0, step +0: EE sub gives -0). The clamp
      itself stores -LIM, so the first input is reachable in play.
  - A seeded random sweep of 2,400 cases, run in full only with
    EM_TEST_FULL=1 (12,903 cases in all). Since round 6 the default run
    samples none of them (`QUICK_RANDOM = 0`) and keeps only the random
    cases in `PINNED_RANDOM` (section "Mutation sweep, round 6" says why
    each is there): 2,008 cases in all (231 capture + 1,773 designed + 4 pinned random).
- **Coverage.** Of the 2,016 non-branch words in the 14 functions, 23 are
  dead copies the compiler places after an unconditional branch's delay
  slot; static reachability over the original words proves they cannot be
  reached. Both modes execute **1,993 / 1,993 reachable words (100%)**.
- **Native fail-stop checks** (`fault_checks`): on 0x825350, a NULL
  001BA580, a failing 001C64F0, an unmapped address inside an argument, a
  fault latched before the call, and a NULL hook table. Every one of the 14 entries is also called with a fault
  already latched, with a NULL hook table and with a NULL fault pointer.
  Each must return -1 without calling a hook or `bytes`, without changing
  the latched fault and, for the op09 entries, without writing the result.
- **Hook contract** (`hook_sites` + `hook_contract_site`, round 5). Until
  round 4 only one hook was ever NULL (001BA580) and one ever failed
  (001C64F0), every hook returned only 0 or -1, and `bytes` was never NULL,
  so the fault address and code of the other wrappers, their `< 0` test
  and the NULL-`bytes` path were not checked (the round-4 review's V03 and
  V05-V09). Now the test picks, from the passing default cases, a set of
  cases (26 in the default run) that together call all 53 hooks and the
  +0x4C callback and run all 14 entries, and runs the native module on each
  of them again against the original's run, with these changes:
  - at **every** call of the case, the hook (or callback) returns -1: the
    entry must return -1 with fault (the original callee's address, 2; for
    the callback, the function read from +0x4C), after exactly the calls
    up to that one, with no `bytes` request after it, with memory equal to
    the original's at the entry of that call (the failed callee's writes
    are not replayed) and no op09 result written;
  - at **every** memory access of the case, `bytes` refuses that request:
    -1 with fault (that address, 5), after exactly the original's calls
    before that access, no later request and no op09 result;
  - at the first call of each hook and of the callback: the table has that
    hook NULL (-1 with fault (callee, 1), exactly the calls before it, the
    accesses since the previous call exactly the original's, memory as at
    the entry of that call, no op09 result); the hook returns INT32_MIN (a
    failure, as for -1); the hook returns 1 and then INT32_MAX (successes:
    the whole case must compare exactly as with 0);
  - on every entry, `bytes` itself NULL: -1 with fault (the original's first
    access, 5) after exactly the calls the original makes before it;
  - (round 6) in every case and every contract run, every call of `bytes`,
    of each hook and of the callback receives the table's `ctx` (a
    non-NULL value, 0xA01C7C00) unchanged; before, `ctx` was NULL
    throughout, so passing NULL instead would not have shown.

  After a fault, every read comes from the zeroed sink and the function's
  control flow runs on, so the every-call and every-access runs reach later
  wrappers with the fault latched, which is where a wrapper's own
  `a01_failed` test shows. Six wrappers cannot be reached that way; the
  round-5 sweep below proves why.
- **Mutation sweep, round 6: the close-out re-run** (2026-09-26; scratch
  under the ignored `build/area01/ovl/round6/`: `batch6.py` runs,
  `run_all.txt` is the complete run, `rows_quick.json` its outcomes,
  `stats.py` counts them, `kill_random.py` / `kill_random.json` and
  `run_confirm.txt` are the pinned random cases below; the lane sources
  were never edited by it). Every mutant of every earlier set was
  regenerated by its own generator from the current sources and run
  against the round-6 test. A mutant whose mutated source equals another's
  is built and run once and counted under each name. Stage 1: each case of
  the default selection runs the original once per start image (as given,
  then poisoned), then every mutant not yet killed, with the test's own
  `Replay` and `finish()`; as in rounds 2 to 5 the batch compares the final
  memory over the dirty set (the test compares all 32 MiB), and after a
  failing mutant it reloads the buffers. Stage 2, for the stage-1
  survivors: `fault_checks()` and `hook_contract_site()` on all 26 contract
  cases, in a child process per mutant (a crash is a kill). The unchanged
  control survives.

  **Sets** (4,104 names; 4,093 apply to the current text, which are 3,948
  distinct programs):

  | Set | Tried | Killed | Equivalent | Not applied / no build |
  |---|---|---|---|---|
  | rounds 1-2: `r1` (76), `r2` (111) | 187 | 165 | 14 | 8 / 0 |
  | round 3: `rev`, `hoist`, `res`, `wid` | 337 | 333 | 1 | 3 / 0 |
  | round 4: `s1` (the round-3 review's S01-S62), `hoist4`, `r2x`, `sign`, `neg`, `swap`, `drop`, `lit` | 2,719 | 2,290 | 333 | 0 / 96 |
  | round 5: `V`, `hdr`, `entry`, `zsign` | 698 | 677 | 21 | 0 / 0 |
  | round-5 review, sweep 2: V01-V66 | 64 | 56 | 8 | 0 / 0 |
  | round-5 review, sweep 3: W01-W56 | 56 | 55 | 1 | 0 / 0 |
  | round-5 review, final: 24 named repeats, F01-F18, Z24r | 43 | 43 | 0 | 0 / 0 |
  | **all** | **4,104** | **3,619** | **378** | **11 / 96** |

  By distinct program: 96 do not compile, 3,476 are killed and 376 are
  equivalent (W43 is the same program as `r1 N39`, and V02 as
  `hdr at sink not cleared`). The 11 that no longer apply are round-1/2
  and round-3 text that round 4 changed; `r2x` and `hoist4` re-express
  them (all killed), and `rev Z24` is re-expressed as `r2x M25` and the
  final review's Z24r (killed). The 96 that do not compile are 74 `lit`
  (duplicate `case` labels and the like) and 22 `swap` (a use moved above
  its declaration), as in round 4.

  **The random sample, and the four pinned cases.** Round 6 takes the
  random sample out of the default run (`QUICK_RANDOM = 0`). Against
  capture and designed cases only, five mutants that round 5 had killed
  only through its 120 sampled random cases survived: `lit D:521 0xE-1`
  (the period test `+0x200 >= 0xE` as `>= 0xD`), `lit D:542 300+1` (the
  tracker's +0x28 reseed `rand_scaled(300)` as 301), `rev Z36` (the idle
  sound test's `0x70003B68 & 0x3F` as `& 0x7F`), `sign O:749` and
  `sign O:785` (828850's +0x9A byte passed to 001B11E0 and 001B1190 read
  as `s8`). Run against all 2,400 random cases, they are killed by 10,
  20, 13, 24 and 60 of them (`kill_random.json`). `PINNED_RANDOM` keeps
  four of those in the default run: #14 '826D40 s4 0' (Z36), #24 '828850 1'
  (O:785), #39 '828850 2' (O:749) and #119 '826D40 s1 7' (both `lit`
  mutants). The numbers are positions in the random sweep, which has its
  own seed, so adding designed cases does not move them. Against the
  final default selection (2,008 cases) all five are killed
  (`run_confirm.txt`), and `hook_sites` picks the same 26 contract cases
  as without the pinned four (the two site lists compared equal), so the
  stage-2 results above stand. The other outcomes above are from the
  2,004-case selection; four added cases can only add kills.

  **Named survivors of the round-5 review, re-run** (each against the
  round-6 test; the killing case is the first one the batch reports):

  | Mutant | Named in | Outcome | How |
  |---|---|---|---|
  | S04 | sweep 1 | killed | door step 0 on the 0x81 branch with a scribbling 001BBE40 ('door step 0 81 1', access log) |
  | S17 | sweep 1 | killed | NPC +0x0D = 0x80 ('npc s1 +0xD 80', the 001BA580 argument) |
  | S35, S36 | sweep 1 | killed | countdown counts 0 ('826D40 count signs 20C 0 214 2' and '... 20C 2 214 0') |
  | S37, S38 | sweep 1 | killed | the ordered access log (narrower re-read), '826D40 count s4 1F 22 1.13' |
  | S41, S42 | sweep 1 | killed | access log, countdown cases ('count s4 1F/1E 22 1.13') |
  | S43 | sweep 1 | killed | '826D40 sweep clamp 2 step 00000000' (EE negation of +0) |
  | S45 | sweep 1 | killed | access log, '826D40 reseed 0' |
  | S48 | sweep 1 | killed | access log, '826D40 band 0 -0.3' |
  | S57 | sweep 1 | killed | access log, '826D40 tracker re-reads' |
  | V03 | sweep 2 | killed | hook contract (`bytes` NULL: the mutant crashes) |
  | V05, V06, V07, V08, V09 | sweep 2 | killed | hook contract |
  | V41 | sweep 2 | killed | '826D40 reseed random C0000000' |
  | V46 | sweep 2 | killed | '826D40 idle hand-off +0x204 80000000' |
  | W35 | sweep 3, final | killed | '826D40 track clamp boundary A+0x74 = -LIM' (round 6) |
  | W39 | sweep 3, final | killed | '826D40 track clamp boundary B+0x78 steps onto -LIM' (round 6) |
  | W40 | sweep 3, final | killed | '826D40 track clamp boundary B+0x78 steps onto BMIN' (round 6) |
  | W45 | sweep 3, final | killed | '826D40 idle phase -0 into the sine' and the wind case (round 6) |

  Four mutants the reviews had called equivalent are killed as well:
  S24, S31 and S60 (sweep 1), each first by the ordered access log (in
  '826440 ramp 0', '828850 1 1 0' and '826D40 s0 0 0'), and V04 (sweep 2). V04's proof was wrong: with the test moved before
  the +0x4C read, a fault latched BY that read (the contract's
  every-access-refused run) no longer returns before the callback, so the
  mutant calls the callback after a fault.

  **The 378 equivalents**, each with its proof:
  - the 333 of round 4's list below, over the round-3 and round-4
    families (188 that compile to the same machine code, among them
    `res companion <= 0`; 98 dead initial values; the countdown flag and
    the unused return value, 3; 26 swaps; 16 sign changes; S39; the
    tail's NEG.S);
  - `lit O:58 1-1` (`a01_begin` ignoring a latched fault; round 5's
    `entry` proof) and round 5's 21 (listed under round 5 below);
  - rounds 1-2's 14 (R16, N26, N39, N44 and the ten generated EE compare
    and division sites; round 2's proofs below);
  - W43 (sweep 3): the same program as `r1 N39`;
  - sweep 2's eight:
    - *V01* (`a01_latch` always overwrites) and *V02* (`a01_at` does not
      clear the sink): round 5's proofs for `hdr latch overwrites` and
      `hdr at sink not cleared` (V02 is that program; V01 writes the
      same change as `if (1)`).
    - *V15* (825240's `c_lt(0, w)` as `!c_le(w, 0)`), *V39* (the phase
      wrap `!c_le(p, PI)` as `c_lt(PI, p)`) and *V65* (state 2's
      `c_eq(-pi/2, p)` as `c_le(-pi/2, p) && !c_lt(-pi/2, p)`).
      `em_ee_c_lt_bits`, `_le_bits` and `_eq_bits` compare
      `em_eei_compare_key` values, which are int64. For integers,
      `a < b` is `!(b <= a)`, and `a <= b && !(a < b)` is `a == b`, so each
      mutant computes the same bit for every pair of words.
    - *V51* (the elevation's `add(k, cur)` as `add(cur, k)`).
      `em_ee_add_bits` is symmetric for every pair of words, by its
      definition in `em_ee_float.h`: both operands get the same DAZ; in
      `em_eei_ee_sum` the exponent-255 branch returns MAX for a NaN in
      either operand, for two Infs a result that depends only on their
      (equal or opposite) signs, and otherwise the Inf operand's sign;
      the pre-trim is applied to whichever operand has the smaller
      exponent; and `em_eei_exact_sum` returns the other operand when
      one is zero, -0 only for two -0s, swaps so that the larger
      exponent comes first, and for equal exponents with opposite signs
      returns the larger magnitude's sign (0 when equal). The round-5
      review's scratch check (`build/area01/sweep_OVL_2/addsym.c`: 200M
      random and near-exponent pairs plus all special pairs) found no
      asymmetric result.
    - *V34* (828850 state 2's re-read `== 0xA` as `>= 0xA`). The branch
      runs only for `count = s16(+0x28) < 10`, so `count + 1` is in
      [-32767, 10]; it is stored as a halfword and loaded again at once
      with no call and no other access in between, so the loaded value is
      `count + 1` (if the store faulted, the load reads 0 from the sink).
      On [-32767, 10], `>= 10` is `== 10`.
    - *V38* (`asr32`'s fill mask `~(0xFFFFFFFF >> n)` as
      `~(0x7FFFFFFF >> n)`). The two masks differ only in bit 31-n, and
      the fill is ORed only when bit 31 of v is set, in which case
      `v >> n` already has bit 31-n set. So the result is the same for
      every v and every n in 0..31 (the review also checked all 2^32
      words for n = 15 and 16, the only counts used, `asr.c`).

  **Round 5's claim corrected.** Round 5 said every mutant it tried was
  killed or proven. The round-5 review's sweep 3 found four that survive
  both modes: W35, W39 and W40, the tracker's A lower clamp and B lower
  and upper clamps with `<` and `<=` exchanged, because no case put the
  compared word exactly on the constant (the doc's "exact EE-model
  boundaries of each float compare" was false; section 3's case list now
  names the compares that have such a case); and W45, a host `+ 0.0f` on
  the sine argument, because no case passed a -0 phase. Round 6 adds the
  five cases listed under "round 6" in the case list; all four are killed.
  The round-5 review also showed that round 5's default run leaned on its
  random sample for five kills; those are now pinned (above).

- **Mutation sweep, round 5** (history; its families are re-run in round 6 above) (2026-09-26; scratch under the ignored
  `build/area01/ovl/mutation5/`: `sweep5.py` generates the new families,
  `batch5.py` runs them, logs `run_*.txt`; the lane sources were never
  edited by the sweep). `batch5.py` is round 4's `batch4.py` with two
  changes. Each mutant is built from a copy of all the lane sources, so a
  change to `em_area01_overlay_internal.h` reaches both translation units.
  And every mutant that no case kills then runs the test's `fault_checks()`
  and `hook_contract_site()` on all the contract cases, in a child process
  (a crash counts as a kill).

  **New families** (699 mutants with the control; `run_all2.txt`, and
  `run_entry.txt` for the two `entry` mutants added last):
  - `V`: the round-4 review's V03, V05, V06, V07, V08, V09, V41 and V46,
    as the review describes them. **All 8 are killed**: V46 and V41 by the
    new cases, the other six by the hook contract (V03 crashes).
  - `hdr` (549): in each of the 53 hook wrappers, the NULL-path and the
    failure-path address +1 and -1, code 1 and code 2 swapped, the failure
    test `< 0` as `!= 0`, `<= 0` and `== -1`, and the wrapper's own
    `a01_failed` test removed; the +0x4C callback's address (`fn` as
    `self`, +0x48 read), codes, test, guard and NULL return; `a01_at`'s
    guard, latch, code, address, size and sink; `a01_latch`'s
    first-fault-wins test and its return value; `a01_failed` inverted.
  - `entry` (6): `a01_begin` without the NULL-table test, without the
    NULL-fault test, ignoring a latched fault; `a01_end` returning 0; the
    two op09 entries writing their result after a fault.
  - `zsign` (135): every test of a memory read against 0, with each other
    relational operator and the read cast to `int32_t`.

  **Results.** 677 are killed in the default run. The 21 survivors are
  equivalent:
  - *10 `zsign` changes on byte reads* (O:383, 464, 466, 467, 473, 518,
    520, 521 `!= 0` as `> 0`; O:531 and O:627 `== 0` as `<= 0`). A byte
    read with `a01_u8` is 0 to 255, so `> 0` is `!= 0` and `<= 0` is
    `== 0` for every value.
  - *The wrapper guard removed in 001BA540, 001BC0E0, 001BC240, 001BC290,
    001C5C90 and 001FC3C0.* Such a wrapper is observable only when it is
    entered with a fault latched. After a fault every read returns 0 (the
    sink is zeroed on every `a01_at`). Each call site of these six sits in
    a branch that a byte read chooses with a non-zero value, and no memory
    access or call lies between that read and the wrapper: the door's
    +0x05 cases 1 to 5 (001BC0E0, 001BC240, 001BC290; the door's state
    must also read 1), the NPC's +0x04 case 3 (001BA540), 826CF0's
    `+0x03 == 1` (001C5C90) and 826200's +0x04 = 1, +0x05 = 1 (001FC3C0).
    If a fault is latched before or at that read, the read gives 0 and the
    branch is not taken. After the read nothing can latch one before the
    call. So these six wrappers are never entered with a fault latched.
  - *`a01_latch`'s first-fault-wins test removed.* Every call of
    `a01_latch` (in `a01_at`, the 53 wrappers and `a01_callback`; the .c
    files have none) comes right after a test that no fault is latched,
    with nothing between them that can latch one: a hook receives only
    `ctx` and its arguments, not the module's state. So the test is always
    true. This assumes that no hook re-enters an entry with the same fault
    record; nothing binds the hooks yet, and a binding that did would need
    this re-checked.
  - *`a01_latch` returning 0.* No caller uses the value: every wrapper call
    in the .c files is cast to `void` (grep), and `a01_at` and
    `a01_callback` ignore it.
  - *The sink not zeroed (in `a01_at` or in `a01_open`) and `a01_begin`
    ignoring a latched fault.* After a fault, or with one latched on
    entry, every `a01_at`, wrapper and callback tests the latch first, so
    no hook or `bytes` call follows, writes go to the sink, the latched
    fault is kept, `a01_end` returns -1 and (round 5) an op09 entry does
    not write its result. The values read from the sink then reach
    nothing observable. The translation has no loop, no array index and no
    integer division, so those values cannot change termination or crash
    it. Before round 5 the op09 entries wrote their result even after a
    fault, so these mutants were not equivalent then (the first run of
    the family, `run_all.txt`, killed `a01_begin` ignoring a latched
    fault through that result). The entries now leave the result
    unwritten when they return -1, and the contract checks require it.

  **The round-4 families, re-run** against the round-5 test and the
  round-5 C (MUT_SWEEP=4, `run_sweep4.txt`): of the 3,057 generated
  mutants, 99 do not compile or no longer apply, as in round 4. Of the
  2,957 that compile, **2,623 are killed** and **334 survive**: exactly
  round 4's 333 proven equivalents (listed below) plus `lit O:58 1-1`.
  That one makes `a01_begin` return 0 with a fault latched, which is the
  `entry` mutant "ignoring a latched fault" above, equivalent by the same
  proof since the op09 result is no longer written after a fault. No
  round-4 kill was lost, although the round-5 cases change which random
  cases the default run samples.

  **The round-2 review's items**, re-checked in round 5. All were fixed in
  round 3 and are re-run as the `rev` family above. Of the round-2
  reviewer's 56 mutants, 55 apply as written and are killed in the
  default run. `rev Z24` (828850 state 2's count read as a u16) has not
  applied since round 4, when state 2 came to read +0x28 once; the same
  change on that single load is `r2x M25` (and the round-5 review's
  `Z24r`), which is killed (round 6 re-ran all of them, below):
  - the 11 result-sign survivors (Z3, Y1-Y9, Y11) and Y12: callee results
    0x80000000 and 0xFFFFFFFF (with 2, 3, 0x100, 0x10000) at all 35 tests
    of a callee's integer result;
  - Z29: the tracker's 0x8282F0 result 3, 0x102, 0x10002, 0x80000000 and
    0xFFFFFFFF at `== 2` (no range proof is used; 0x8282F0 is now byte-matched decomp C, which returns only 0, 1 or 2);
  - Z18: 001B1EA0 scribbles move D_00810350+4 across 2.0 both ways, and
    the ordered access log sees the moved read;
  - Z14: the three talk machines' +0x05 gets 0, 1, 2 with each bit
    flipped (so 3, 4, 5, 6 and more), 0x7F, 0x80 and 0xFF in the default
    run, all 256 values with EM_TEST_FULL=1;
  - Z34/Z39: 826D40 state 2 with +0x224 = 0x100, 0x10000, 0x80000000 and
    0xFFFFFFFF, and +0x21C = 0xFFFFFFFF, 0x80000000, 0x7FFFFFFF, 0x100 and
    0x10000;
  - signalling NaNs: floats cross the harness as bit patterns (the
    "Floats cross the harness" bullet above);
  - timing is given as measured ranges with CPU time (below);
  - the summary line names what each mode compares at a call entry.

  **Round 4's claim corrected.** Round 4 said that every survivor of its
  sweep was killed or proven. The round-4 review then found V46 (both
  modes) and V03 and V05-V09 (the fail-stop plumbing, both modes), and V41
  survived the default run. The reasons: no case gave the idle hand-off
  a word with bit 31 set; the fail-stop checks made only one hook NULL and
  one fail, never returned a positive status and never made `bytes` NULL;
  and the one case that killed V41 was in the full-mode random sample
  only. The round-4 sweep had no family for the header's plumbing or for
  whole-word zero tests; round 5 adds both.

  **V41, also proven for real inputs.** The random words reach the reseed
  only through 00122BB8, whose decomp C is byte-matched
  (`src/func_00122BB8.c`, compiled by ee-gcc): its single return is the
  new state masked with 0x7FFFFFFF, so it never returns a word with bit
  31 set. Every non-negative word scales to 0..29, where bit 7 is clear,
  so `& 8` and `& 0x88` agree there. The case with 0xC0000000 pins the
  form of the mask; it is not a reachable input.

- **Mutation sweep, round 4** (history; the families are re-run in round 5 above) (2026-09-26; scratch under the ignored
  `build/area01/ovl/mutation4/`: `sweep4.py` generates the mutants,
  `batch4.py` runs them, `objeq.py` compares machine code, and the logs
  are `run_*.txt`; the lane sources were never edited). Each mutant is one
  single-operation change to a temporary copy of the translation, compiled
  (`-O2`, as the test builds) to its own library. Each case of the default
  selection (2,107 cases, as given and poisoned) runs once through the
  original, then through every mutant, using the test's own `Replay` and
  `finish()` comparison. That comparison covers memory at every call entry,
  the calls and arguments, the ordered access log, returns, the stop
  address and final memory. The one difference from the test: final memory
  is compared over the dirty set, which covers every byte the module can
  reach. The test's `fault_checks()` also runs once per mutant, in a child
  process, so a mutant that crashes counts as killed. The families:
  - `s1`: the round-3 reviewer's 62 (`build/area01/sweep_OVL_1/sweep_s1.py`);
  - `rev`, `hoist`, `res`, `wid`: round 3's families (56 + 19 + 209 + 53),
    re-run; `hoist4`: the two round-3 hoists whose text changed, re-expressed;
    `r2x`: the eight round-1/2 mutants whose text changed, re-expressed;
  - `sign`: every field read (192) read with the other signedness of the
    same width;
  - `neg`: every EE negation (13 sites) as a subtraction from +0 and as a
    raw sign flip;
  - `swap`: every two adjacent one-line statements exchanged (306);
  - `drop`: every one-line statement with an effect removed (339);
  - `lit`: every integer literal in the two files, +1 and -1 (1,784).

  **Results.** Of 3,057 generated mutants, 96 do not compile (duplicate
  `case` labels, a use moved before its declaration) and 3 are round-3
  mutants whose text no longer exists, all three re-expressed (`hoist4`)
  or covered by the `sign` family. Of the 2,957 that compile (the control
  excluded), **2,624 are killed in the default run**, and **333
  survive. Every survivor is proven equivalent**, as follows. All
  12 round-3 review survivors (S04, S17, S35, S36, S37, S38, S41, S42, S43,
  S45, S48, S57) are killed in the default run. With the access check
  switched off (`NO_ACCESS=1`), all 12 are still killed by behaviour
  alone, by the round-4 cases (`run_s1_noaccess.txt`). The unchanged
  control survives every batch.
  A note on the logs. In the complete re-run (`run_all_final.txt`) the
  per-mutant fault checks ran before the captures were loaded. So every
  mutant, the control included, shows a spurious first kill labelled
  `fault_checks`. The case kills in that log are valid: they are the
  count beyond that first entry. The 363 mutants that no case killed (and the control)
  were then run again with the fault checks fixed and run in a child
  process (`run_survivors_final.txt`), and the numbers above come from
  that re-run.

  **Round-4 equivalent survivors** (333):
  - *188 compile to the same machine code as the translation.* The
    object file's `__text` and relocations are identical (`objeq.py`,
    same flags as the test), so the mutant is the same program. These
    are 156 `sign`, 27 `swap`, 4 `lit` and `companion <= 0` (res).
  - *98 `lit` changes to a dead initial value* (`r = 0`,
    `int32_t r = 0`, `companion = 0`, `result = 0` set to 1 or -1). On
    every path the variable is next written through its address by a
    hook, and only then read. `rinit_uses.txt` lists each site's next
    uses. A successful hook writes it (hook contract, section 1). After
    a failed hook the fault is latched, and nothing that follows is
    observable: no hook runs, `bytes` is not called, writes are dropped,
    and the entry returns -1. Only the r of 825130 lies in an op09
    function, and it feeds only the clip pick.
  - *`countdown(o, self, 1)` as 2* (lit): the flag is only tested as a
    truth value. *`a01_ovl_826d40`'s `return ... ? -1 : 0` with -2 or 1*
    (lit, 2): its only caller, `em_area01_ovl_00826D40`, ignores the value
    and returns `a01_end`.
  - *26 `swap`s.* In 23 of them, one of the two statements neither
    accesses memory nor calls anything: it initialises or computes
    locals. Neither statement reads or writes a variable that the other
    writes, so C gives the same result in either order. Those 23 are
    door 73/74, 825130 170/171, 828850 739/740, 826D40 148, 157, 197, 244,
    245, 265, 295, 431, 433, 448, 449, 477, 478 and 533, and `r = 0`
    moved across a statement that does not mention `r` (door 115, 825590
    324, 826440 579, 828850 747 and 760, 826D40 331). In the other 3,
    `r = 0` moves after the hook call that writes `r` (door 116, 825590
    325, 826D40 idle 332). `r` is not read again before its next write,
    so the store is dead as above.
  - *16 `sign` changes that do not compile to identical code.* Five are
    words that are only converted back to `uint32_t`: the door's +0x1C
    node, the talk machines' +0xC4 copied to block +0x44, the NPC's
    D_0028A5C4, and 826440's two copies from D_00810350. C converts
    int32 to uint32 modulo 2^32, so the value is the same. Three are
    step bytes incremented and stored as a byte (door steps 0, 1 and 3):
    (s8 + 1) and (u8 + 1) agree modulo 256. Five are tests that treat
    the byte or word the same either way: 826200's `D_008107E0 != 0`,
    826440's `timer == 0x3C` and `== 0xC8`, 8261A0's `+0x04 == 0`, and
    8267C0's `switch` on +0x05, whose labels 0 and 1 are both below
    0x80, so a byte of 0x80 or more reaches `default` either way. The
    last three add a small constant to a signed word, which overflows
    only for the words just below 0x80000000: 826440's timer + 1, and
    828850's partner pointer + 4 in states 0 and 1. The test runs every
    such word (timer 0x7FFFFFFF; partner 0x7FFFFFFC to 0x7FFFFFFF in
    both states), and the compiled mutant agrees with the original on
    each. 828850's partner + 0x21C would overflow only for partner >=
    0x7FFFFDE4. For those, the partner + 4 store just before it lies
    outside RAM, so the fault is already latched, and the re-read
    returns 0 from the sink. The cases at 0x7FFFFDE4 and 0x7FFFFF00
    show this.
  - *S39, the countdown's `left < 0x1F` compared unsigned:* the dispatch
    has just tested +0x208 `> 0` (signed), and nothing is stored between
    that test and the decrement, so left = +0x208 - 1 lies in
    [0, 0x7FFFFFFE], where the signed and unsigned tests agree.
  - *The tail's `neg(sub(B+0x78, MLIM))` as a raw sign flip* (neg): the
    operand is an EE SUB result. `em_eei_ee_sum` returns +-MAX for any
    exponent-255 operand, and `em_eei_pack` saturates any result whose
    exponent reaches 255 to +-MAX, so the operand's exponent is never
    255. NEG.S flips the sign bit alone for every such word.

  **The rounds 1-2 mutants**, re-run (`MUT_R2=1`, `run_r2.txt`,
  `run_r2x.txt`): the 8 whose text changed are re-expressed (`r2x`) and
  killed; of the others, 165 are killed and 14 survive. The 14 are the
  equivalents proved below (R16, N26, N39, N44 and the ten generated EE
  compare/div sites). O1 and O2, which rounds 1 to 3 proved equivalent,
  are now killed: the extra store each makes shows in the ordered access
  log.

  **Round 3's claim corrected.** The round-3 text said that all 14 were
  verified, and the round-3 sweep reported no non-equivalent survivor.
  The round-3 review's sweep then found 12 changes that survived:
  - 9 in both modes: S04, S37, S38, S41, S42, S43, S45, S48 and S57;
  - 3 in the default run: S17, S35 and S36.

  The reasons were these. The round-3 check compared only the set of
  bytes accessed between two calls. No case aliased a pointer with the
  record it points into. No case had a zero or negative countdown count.
  No case gave NPC +0x0D >= 0x80. No case negated +0, a denormal or an
  exponent-255 word. Round 3's list of equivalent width survivors (8
  narrower reads of +0x20C/+0x214) was right, but those mutants are now
  killed outright, because the ordered log records the access size.
- **Mutation sweep, round 3** (history; superseded by round 4 above) (2026-09-26; scratch under the ignored
  `build/area01/ovl/mutation3/`: `sweep3.py`, `batch3.py` and the logs
  `run_*.txt`; lane sources never edited). The method is round 2's (below),
  but the batch now calls the test's own `finish()`, so the bytes accessed
  between calls and the stop address are compared as well. All runs use
  the final test's default selection of 1,993 cases.
  - **The round-2 reviewer's 56 mutants** (Z1–Z40 without the control, P1–P4,
    Y1–Y12): **all 56 are killed** in the default run. That includes the 17
    survivors of round 2: Z3, Z14, Z18, Z29, Z34, Z39, Y1–Y9, Y11 and Y12.
    Z3, Y1–Y9 and Y11/Y12 fall to the result cases (0x80000000 first), Z14
    to the talk step byte = 4, Z18 to the access check (the native module
    reads 0x810354 before 001B1EA0), Z29 to the tracker result 3, Z34 to
    the access check (a one-byte read of the word +0x224), and Z39 to
    +0x21C = 0xFFFFFFFF. Z3, Z14, Z18, Z29, Z34, Z39, Y1, Y7 and Y11 had
    also survived EM_TEST_FULL=1 in round 2. A full-mode batch of those
    nine (MUT_FULL=1, 12,772 cases) kills all nine, and the control
    survives it. Z18 and Z34 are also killed by behaviour alone: with the
    access check switched off in a scratch run (NO_ACCESS=1), the four
    826440 height scribbles kill Z18 and the lane word 0x100 kills Z34.
  - **19 hoist and cache mutants**: a read that the original makes after a
    callee is moved before it, or taken from a value read before it. They
    cover the door's step increments (steps 0, 1, 3, 4, and step 4 read
    before the call), the tail's and step 2's story byte, the NPC's
    D_0028A5C4, 825130's yaw, 826440's pad byte, 8267C0's step byte,
    828850's +0x9A in states 0 and 2, and 0x826D40's idle +0x204 and
    +0x36, the tracker's +0x200 and +0x2A, state 2's phase and the period
    effect's kind word. **All 19 are killed.** With the access check
    switched off, 9 of them are still killed by behaviour (the door's
    five step-byte mutants and two story-byte mutants, 826440's pad byte,
    and the period effect's kind word). The other 10 are killed by the
    access check alone, which compares against the bytes the original was
    observed to access; no scribbling case changes those fields.
  - **209 generated result-form mutants.** At each of the 35 tests of a
    callee's result in the C (`r`, `result` or `companion` against 0, 1 or
    2), each form a translation could wrongly use: `> 0` / `<= 0`, the low
    byte or halfword, `== 1` / `!= 1`, bit 0, a signed cast, and for
    `== 1` / `== 2` also `!= 0`, `> 0`, `>= 1`, `>= 2`, `> 1` and bit 1.
    **208 are killed.** The survivor is `companion <= 0` for
    `companion == 0`. `companion` is `uint32_t`, so the two are the same
    test for every value. The signed form `(int32_t)companion <= 0` is
    killed by the stop cases.
  - **55 generated width and sign mutants.** At each of the 31 field reads
    that feed a comparison or a mask test, the field is read with another
    width or signedness: u32 to s32, u16 or u8; s32 to u32 or s16; s16 to
    u16 or s8; u16 to u8 or s16; u8 to s8. A same-width sign change under
    `==`, `!=` or `&` against a constant inside both ranges is the same C
    expression, so it is not generated. **46 are killed.** The 9 survivors
    are equivalent for every value (see the proofs below).
  - **The 187 mutants of rounds 1 and 2**, re-run: 171 killed. The same 16
    survivors as before (R16, N26, N39, N44, O1, O2 and the ten generated
    EE ones) are the equivalents proved below.
  - The unchanged control survives in every batch, as it must.

  (Round 3's equivalent survivors are covered by round 4: `companion <= 0`
  compiles to identical code, and the 8 narrower reads and the s8 tick
  read are killed or identical under the ordered access check.)

- **Mutation sweep, round 2** (2026-09-26; scratch under the ignored
  `build/area01/ovl/mutation2/`, lane sources never edited). Each mutant is
  one single-operation change to a temporary copy of the translation,
  compiled to its own library. A batch harness (`batch.py`) runs every case
  of the default selection once through the original and then through every
  mutant, using the test's own `Replay` comparison: memory at every call
  entry, the calls and arguments, returns, and final memory. It differs
  from the test in one way: it compares the dirty set after the last store
  instead of all 32 MiB. That is equivalent for this module, which reaches
  memory only through `bytes`. A sample of its kills (B23, B24, B26, N43,
  M38, the C 488 and C 490 compare mutants, H 531, plus an unchanged
  control that passes) was confirmed by running the real, unfiltered test.
  The sweep has 111 mutants:
  - **The round-2 reviewer's 7:** N43 (the tail's sub done on the host),
    B23 (`| 3` for `| 2`), B24 (`kind & 1`), B25 (set-up `r == 1`), B26
    (signed `> 0` for the unsigned `!= 0` on +0x200), B27 (re-arm on
    `+0x208 <= 0`) and A7 (use mask `& 6`). **All 7 are killed** in the
    default run.
  - **52 new hand-written mutants (M1–M52)** over all 14 functions. They
    cover flipped and widened compares, off-by-one steps and constants,
    wrong fields, offsets and widths, dropped stores, wrong call arguments,
    callee results tested with `== 1` instead of `!= 0`, and cached values
    in place of re-reads (for example the fades' re-read of 0x70003A20).
    **All 52 are killed.**
  - **52 generated EE-versus-host mutants:** every EE add, sub, mul, mula,
    madd and div (28 sites) replaced by the host operation, and every EE
    compare (24 sites) replaced by the host compare. **42 are killed.** The
    10 survivors are proven equivalent below.
  - The unchanged control survives, as it must.

  Separately, the round-1 sweep's 76 mutants (R0–R22, N1–N51, O1, O2) were
  re-run against the current test: 70 are killed, including N43, and the 6
  survivors (R16, N26, N39, N44, O1, O2) are the equivalents below.

  **Equivalent survivors.** Each proof was checked exhaustively in a scratch
  C program using the port's own EE functions (`build/area01/ovl/equiv*.c`):
  - *A compare of an EE arithmetic result with a normal constant* (host
    compare at the phase wrap `!(phase <= pi)`, the side band `f2 < −STEP`
    and `!(f2 <= STEP)`, and state 2's `!(v <= −pi/2)` and `v < −pi/2`).
    The EE ADD/SUB/MADD result is always ±0 or a normal number no larger
    than MAX. In the model this holds by construction, because the sum
    saturates to ±MAX and flushes to zero. A 400M-pair random check with
    special patterns also found no other result. For every such word, the
    key compare against these constants equals the IEEE compare: 0
    differences over all 4,261,412,866 zero or normal words.
  - *The tracker's A and B clamps after the low clamp* (host `!(A <= LIM)`
    after the EE `A < MLIM` clamp, and host `!(B <= BMIN)` after the EE
    `B < MLIM` clamp). Once the EE low clamp has run, the host and EE upper
    tests agree for every 32-bit word (0 of 2^32). An aliasing store in
    between is not possible: storing MLIM into a pointer word gives an
    unmapped pointer.
  - *State 2's `c_eq(−pi/2, phase)` as a host `==`* (and R16 as a bitwise
    `==`). −pi/2 is a normal number, so key equality is bit equality: 0 of
    2^32.
  - *The two fade divisions as host divisions* (by 128 in the countdown,
    N26; by 90 in state 2, N44). The numerator is a converted integer, and
    EE DIV rounds to nearest like the host: 0 of 2^32 for each.
  - *N39: the elevation's `madd(x·x, z, z)` as `add(x·x, mul(z, z))`.*
    Both terms are non-negative, so the unsaturated and the saturated
    product give the same saturated sum. No difference over the special
    patterns plus 400M random pairs (round 1).
  - (O1 and O2 below are killed since round 4: each makes an extra or
    different access that the ordered access log shows. Their proofs are
    kept as the record of round 2.)
  - *O1: state 2's clamp `v < −pi/2` as `<=`.* v is an EE sum. When the
    two tests differ, v has the key of −pi/2 and therefore its bits, so the
    extra store writes the same word.
  - *O2: the elevation's else-if `<` as `<=`.* The two differ only for an
    f3 whose key equals `add(k, cur)` after `f3 <= sub(cur, k)` held. The
    original then stores f3 and the mutant stores `sub(cur, k)`. For every
    one of the 1,862,270,976 cur words that reach this point, both stored
    words fall on the same side of the clamps that follow (0 exceptions),
    so both are overwritten by the same constant.

  **Corrections to the earlier claims.** Round 1 listed N43 (the tail's
  sub) as equivalent, arguing that B had just been clamped. That was
  wrong: the tail re-reads B through D_00275B40 after 0x8282F0, which can
  change it. N43 is now killed by the "tail B from 8282F0" cases. The
  earlier claim that every add, sub, mul and madd had disagreeing operands
  was also false for several sites: the set-up pitch, both heading subs, the
  side sub, the tail sub and the tail division. Each of these now has a
  case, as the generated sweep shows. The M38 and C 488/490/497
  compare mutants were also alive until the elevation cases with
  exponent-255 words (for example B+0x78 = 0xFFC00000, where EE ADD and
  SUB return +MAX) were added.
- **Sanitizers.** The first session's scratch ASan/UBSan fuzz (200,000
  random calls over random memory) found nothing. It was not repeated
  after the C changed in round 4 (access order) and round 5 (no op09
  result after a fault). Round 6 changed only comments in the C (the
  compiled objects are byte-identical to round 5's, checked with `cmp`).

Results (2026-09-26, close-out round 6). The machine was shared with other
lanes (10 cores; load average 22 to 45 during the test runs below); every
run used at most 4 worker processes. CPU time is user + sys over all
processes and grows with the number of workers on this machine (memory
traffic and shared cores), so it is given per worker count:

```
$ EM_TEST_JOBS=4 python3 tools/test_area01_overlay_reference.py
  # round 6, 4 workers: 5.3 to 6.7 s wall, 13.0 to 16.8 s CPU (six runs);
  # 2 workers: 7.7 s wall, 13.4 s CPU; 1 worker: 10.9 to 11.7 s wall,
  # 10.7 to 11.5 s CPU
mode quick: 2,008 of 2,364 cases, 231 capture + 1773 designed cases kept (EM_TEST_FULL=1 runs the exhaustive sweep)
  823580   277 cases   102/ 102 reachable non-branch words executed
  825130    67 cases    50/  50 ...     825240    65 cases    50/  50 ...
  825350   100 cases    59/  59 ...     8254B0    62 cases    40/  40 ...
  825590    62 cases    42/  42 ...     825670    62 cases    39/  39 ...
  8261A0    66 cases    18/  18 ...     826200   150 cases   103/ 103 ...
  826440   246 cases   166/ 166 ...     8267C0   123 cases    66/  66 ...
  826CF0    28 cases    11/  11 ...     826D40   585 cases  1175/1175 ...
  828850   115 cases    72/  72 ...
  coverage 1993/1993 reachable words
  native fail-stop checks: ok (NULL hook, failing hook, unmapped address inside an argument, latched fault, NULL hooks; every entry refuses a latched fault, a NULL hook table and a NULL fault pointer)
  hook contract: ok (636 native runs on 26 cases: every call failing and every memory access refused; each of the 53 hooks and the +0x4C callback NULL, returning INT32_MIN, 1 and INT32_MAX; `bytes` NULL on all 14 entries)
  3267 runs (2008 as given + 1259 poisoned; ...), 17268 call entries compared (dirty set), 4294 helper register rehearsals
all 2008 cases identical: callee calls, arguments and results; memory at every call entry (dirty set); the memory accesses between calls (one for one, in order); memory after the last store (all 32 MiB + scratchpad); the table's ctx at every hook, `bytes` and callback call

$ EM_TEST_JOBS=4 EM_TEST_FULL=1 python3 tools/test_area01_overlay_reference.py
  # round 6: 77.5 s wall / 285.9 s CPU (284.4 user + 1.5 sys) at load ~31-40
  # (round 5's test: 171.7 to 244.3 s wall, 423 to 532 s CPU)
mode full: 12,903 cases, 231 capture + 10272 designed cases kept
  coverage 1993/1993 reachable words
  hook contract: ok (636 native runs on 26 cases: ...)
  19632 runs (12903 as given + 6729 poisoned; ...), 68884 call entries compared (all 32 MiB + scratchpad), 12798 helper register rehearsals
all 12903 cases identical: ...; memory at every call entry (all 32 MiB + scratchpad); ...
```

("2,364" is the number of cases a default run generates: its random part
is the sweep's first 24 rounds, 360 cases, of which only the four pinned
ones run. The pinned numbers all fall in that part.)

The summary line says what each mode compares at a call entry. The
default run compares the dirty set there; EM_TEST_FULL=1 compares all
32 MiB and the scratchpad. The access log between calls and the memory
after the last store are compared in the same way in both modes. The
hook contract runs the same checks in both modes, on the cases each mode
picks.

The ~10 s target. The default run needs 10.7 to 11.5 s of CPU with one
worker and 13 to 17 s with four; round 5 needed 43 to 62 s. Two changes
did it: the harness memory handling above (no compared field changed;
about 30 s to 11 s of CPU on one worker) and dropping the random sample
from the default run (120 cases; the five kills that depended on it are
pinned instead). Every capture case, every designed case and every
pinned kill case stays in the default run. What is left is mostly the
full final comparison (32 MiB of native against expected memory per run,
about half the CPU) and the oracle's own interpretation. The default run
keeps that comparison, so it was not cut further.

Mutation-sweep costs (scratch, at most 4 workers; compile time included):
- round 6, every set re-run (`round6/run_all.txt`, 4,107 entries: the
  4,104 mutants and three controls; 3,854 libraries built): 2,401 s wall, 4,575 s CPU (4,140 user + 434 sys) at
  load 36 to 100; the build took 1,183 s of that;
- round 6, the five lost kills against the full random sweep
  (`kill_random.py`): 23 s wall, 58 s CPU; their confirmation on the
  final selection (`run_confirm.txt`): 13 s, 43 s;
- round 5, new families (`run_all2.txt`, 697 mutants): 557 s wall,
  1,210 s CPU; the two added `entry` mutants: 26 s, 42 s;
- round 5, the round-4 families re-run (`run_sweep4.txt`, 2,958 built):
  1,713 s wall, 3,828 s CPU (load 50 to 270; the build alone took 978 s);
- main round-4 families: 827 s wall, 1,638 s CPU;
- the complete re-run of all 2,950 mutants: 1,513 s, 3,465 s;
- the survivor re-run with the fault checks: 302 s, 623 s;
- rounds 1-2: 127 s, 239 s;
- the `NO_ACCESS=1` batch on the 12 review survivors: 25 s, 43 s.

## 4. What is left

- **Binding (phase 3).** Nothing calls these entries. A future AREA01 binding
  chain supplies `bytes` over the scene's original-byte storage and binds each
  hook to its verified port translation, or to a fail-stop stand-in.
- **The callees are not verified here.** Their behaviour is the other lanes' work
  and the port's existing modules. Of the ones reached only from this set:
  0x8282F0 and 0x8287C0 (both byte-matched decomp C) are called only by
  0x826D40, and neither ran on the recorded route. The
  pool-side 001AFA90 companion spawn and 001C5680 are also untranslated here.
- **Unused register arguments (not proven by the test).** Where the original
  leaves an unrelated value in an argument register, the hook does not take
  it. This applies to a1 = 6 for 001B0FD0/001AFC10 in 0x826D40 and a2 for the
  two-argument 001BA1A0 calls. For 001BA1A0, which runs as original code,
  the register rehearsal proves it reads only a0 and a1. For the stubbed
  callees, the claim rests on reading their definitions: the compiled decomp
  C of 001B0FD0 and 001AFC10 reads only the arguments the hooks pass.
  0019B6C0 is NEARMISS in the decomp, so its hook also receives the leftover
  a2 (the pointer at 0x700031D0). Verifying a stubbed callee's inputs belongs
  to the lane that translates that callee.
- **Callee result ranges.** Stubbed callees are scripted with results
  outside their real range as well. At every test of an integer result
  they get 2, 3, 0x100, 0x10000, 0x80000000 and 0xFFFFFFFF (and 0x101,
  0x10001, 0x102, 0x10002 at the `== 1` and `== 2` tests). 001AFA90's
  companion gets pointers with bit 31 set, and 0011DF78 a negative
  exponent-255 magnitude. These cases pin the form of each test the
  original makes (for example set-up's `001BA1C0(self, 6) != 0`, where a
  translation testing `== 1` now fails). No proof of a callee's range is
  used anywhere, so none rests on the NEARMISS callees (001BA1F0,
  0019B6C0, 00182BF0, 001B11E0; 0x8282F0 has been byte-matched C since
  the decomp's A01C lane, 2026-09-28). What the real callees return is
  still their definitions' business. 001BA1C0's compiled decomp C returns
  `D_00810758[a1] == 0xFF` (only 0 or 1), and 0011DF78 clears the sign
  bit, so with the real callees some of those forms could not be told
  apart.
- **What the harness cannot see.** Whether an access that changed nothing
  is a load or a store of the value already there. The `bytes` hook does
  not say which, so two such accesses to the same bytes that trade places
  show only through the values used afterwards. What the hardware would do
  past a stop: where the original's own access leaves main RAM and the
  scratchpad, or is misaligned, the test compares only the stop address,
  and the calls and stores up to it. Among the addresses it stops at are
  the uncached RAM mirrors at 0x20000000 and 0x30100000, where the real EE
  would go on. The door's step table is the one set of original loads the
  translation does not make (section 3).
- **Which fail-stop statements the test checks, and how far.** Success
  statuses: 0 at every call of every case, 1 and INT32_MAX at the first
  call of each hook in the 26 contract cases. Failures: -1 at every call
  of those cases, INT32_MIN at the first call of each hook. A NULL hook:
  at the first call of each hook in those cases. Refused addresses: at
  every memory access of those cases, plus `bytes` NULL on every entry.
  A fault latched on entry, a NULL hook table and a NULL fault pointer:
  on every entry, with every hook refusing. Other status values, other
  call positions and a hook that re-enters the module are not tested.
- **The equivalence proofs rest on the EE model.** The fourteen float
  equivalents of section 3 (rounds 1 and 2), and round 4's NEG.S one,
  are equivalent under
  `em_ee_float.h`, which is the
  measured EE model (docs/EE_FLOAT_MODEL.md). They would need re-checking
  if that model changed.
- **Live evidence for 0x826D40's states 4, 1 and 2** would need a route on which
  001BA1C0(node, 6) becomes non-zero. The recorded route leaves the three nodes in
  0x64. (SECOND_LEVEL_ROUTE.md called them sentry guns as a guess. Since
  2026-09-27 the decomp's FINDINGS (s78 section 7 correction) and CURIOSITIES
  26 decode AREA11's 0x825940 / 0x827490 as a security gun, dormant in state
  0x64 until a story flag is set, and its power cable, and report the same
  pair in AREA01; the overlay scan they cite (decomp
  build/workflows/verify-area11-husks.output.json) has AREA01's gun gated on
  story flag 6. That fits these nodes and the 001BA1C0(node, 6) gate above, but the identity was not
  checked against this translation and no capture shows them switched on.)

## Known limitation of the parallel test runner (2026-09-26)

If a native module crashes (e.g. a mutant that calls a NULL `bytes` hook), a hook-contract worker dies with SIGSEGV and `reference_mode.parallel_map`'s pool does not return: with `EM_TEST_JOBS>1` the run hangs instead of failing. With `EM_TEST_JOBS=1` the crash ends the run (a clean failure). Mutation runs therefore execute each mutant in its own process.

