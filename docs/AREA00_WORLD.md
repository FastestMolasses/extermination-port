# AREA00 lane A00WORLD: the world, entity and area functions AREA00 adds

Level 3 (AREA00) side track, lane A00WORLD, 2026-09-28 (session s88). New files only:
`src/game/em_area00_world.{c,h}` (prefix `em_area00_world_`) and
`tools/test_area00_world_reference.py`. The module is built and tested but not wired:
nothing in the port calls it, the Makefile does not build it, and wiring waits for the
AREA00 load (THIRD_LEVEL_ROUTE.md, AREA00_OVERVIEW.md). Original executable SHA-256
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

## 1. Scope

The rows of `../Extermination/build/s87/census/a00_delta.json` (`new_functions`: the
functions AREA00 runs that neither the first level nor AREA01 ran) in the census
subsystems level_world, entity_logic, entity_update, area_logic, math_vector, actor_anim,
anim_runtime, area_state, obj_registry and entity_sys: **35 rows, 18,928 bytes**. The
subsystem names are census labels, not evidence; the functions are named by address.
(`a01_delta.json`'s 20 `room_beats_new_functions` are functions of the four AREA01 room
beats, not AREA00 rows; the census counts them as already run, so none of them is in this
lane's 35. Nine of them carry these subsystem labels; each of those addresses is named by
the port's AREA01 room / player-closure modules, which this lane did not re-check.)

### 1.1 Reused: a verified translation already exists (20 rows)

Before translating, the port was searched for every address. These 20 already have a
translation checked by an original-instruction oracle; they are **reused, not translated
again**. Several ran in the AREA00 arrival (the end of AREA01 beat a01_07), which is why
the AREA01 lanes already cover them.

| Function | Bytes | Decomp | Census first beat | Port translation | Oracle (doc) |
|---|---:|---|---|---|---|
| 00156F30 | 1072 | NM | arrival | `em_area01_exitb.c` | `test_area01_exitb_reference.py` (AREA01_EXITB.md) |
| 001576E0 | 384 | BM | arrival | `em_area01_exitb.c` | same |
| 001581A0 | 316 | NM | arrival | `em_area01_exitb.c` | same |
| 00158810 | 948 | NM | arrival | `em_area01_exitb.c` | same |
| 00158BD0 | 340 | NM | arrival | `em_area01_exitb.c` | same |
| 0015AB00 | 244 | BM | arrival | `em_area01_exitb.c` | same |
| 001C2430 | 148 | BM | arrival | `em_area01_exitb.c` | same |
| 001C6160 | 44 | BM | arrival | `em_area01_exitb.c` | same |
| 001D0400 | 172 | AI | arrival | `em_area01_exitb.c` | same |
| 001735C0 | 2064 | BM | a00_03 | `em_player_weapon_states_b.c` (state 0x21, live in AREA11) | `test_player_weapon_states_b_reference.py` (PLAYER_WEAPON_STATES_B.md) |
| 00173DD0 | 140 | NM | a00_09 | `em_player_weapon_states_b.c` | same |
| 00173E60 | 872 | BM | a00_09 | `em_player_weapon_states_b.c` (state 0x22) | same |
| 001754E0 | 208 | BM | a00_09 | `em_player_reaction.c` | `test_player_reaction_reference.py` (PLAYER_REACTION.md) |
| 0021F330 | 1312 | BM | a00_09 | `em_player_reaction.c` | same |
| 0021F850 | 748 | BM | a00_09 | `em_player_reaction.c` | same |
| 0021C270 | 212 | BM | a00_09 | `em_player_stage_workers.c` (`em_player_0021C270`, bound live) | `test_player_stage_workers_reference.py` (PLAYER_STAGE_WORKERS.md) |
| 0021D4E0 | 72 | BM | a00_09 | `em_player_stage_workers.c` (`em_player_0021D4E0`) | same |
| 001A58B0 | 884 | AW | a00_05 | `em_actor_collision.c` (`column_ngon`) | `test_actor_collision_reference.py` (ACTOR_COLLISION.md) |
| 001A7280 | 1520 | NM | a00_02 | `em_coll_grid_hull.c` (`em_coll_grid_hull_001A7280`) | `test_coll_grid_hull_reference.py` (COLL_GRID_HULL.md) |
| 001C2FF0 | 2552 | (see note) | a00_02 | `em_area01_math_actor.c` (`em_area01_math_001C2770`) | `test_area01_math_reference.py` (AREA01_MATH.md) |

Each reused oracle was run in its default mode during this lane (section 4.7).

**001C2FF0 is not a function.** The census lists it as a separate row with status
"missing_source" because splat labels it; the decomp's `src/func_001C2770.c` covers it
(the pair was merged with a symbol size of 0x1278; nothing calls 0x1C2FF0, it is a
branch target inside 001C2770). The census breakpoint at that label fired because AREA00
took a path of 001C2770 that reaches the tail. 001C2770 is translated whole by lane MATH
(119 of 120 branch outcomes taken by its oracle, AREA01_MATH.md).

### 1.2 Translated here (15 rows, 4,676 bytes)

| Function | Bytes | Decomp | Census subsystem | First beat | Callers in the decomp (claims) |
|---|---:|---|---|---|---|
| 00189EC0 | 280 | NM | actor_anim | a00_03 | 00189FE0, 0018A1F0 |
| 00189FE0 | 416 | BM | actor_anim | a00_03 | 0018A1F0 |
| 0018A180 | 108 | BM | actor_anim | a00_03 | 0018A1F0 |
| 0019AA80 | 156 | NM | level_world | a00_02 | 00154120, 001B3C30, 001E2E80, 00219870, AREA01 0x8282B0 |
| 0019B2C0 | 512 | NM | level_world | a00_03 | 0018A1F0, 001B3390 |
| 0019C6F0 | 320 | NM | level_world | a00_08 | AREA00 0x823540 / 0x823820, AREA01 0x825910, AREA02 |
| 001B0CD0 | 164 | BM | entity_sys | a00_10 exit | AREA01 0x825FD0 |
| 001B41F0 | 1564 | NM | math_vector | a00_09 | 00189FE0, 001861C0, 00186A60, 0018AF50, 0018B3E0 |
| 001C24D0 | 104 | BM | math_vector | a00_02 | 0012B970, 0012BE20, 0012C490 |
| 001C5050 | 88 | AI | math_vector | a00_10 exit | 001C50B0 |
| 001C6200 | 184 | AI | anim_runtime | a00_09 | 001F2E90 |
| 001C63D0 | 16 | BM | anim_runtime | a00_10 exit | 001BE6C0, 001F5040 |
| 001CA3B0 | 280 | AI | anim_runtime | a00_09 | 001F2F90, 001F3620, 001F4190 |
| 001CA4D0 | 260 | AW | anim_runtime | a00_09 | 001F3620, 001F4190 |
| 0021BD60 | 224 | AW | area_state | a00_04 | 0012E0B0 |

Status abbreviations as THIRD_LEVEL_ROUTE.md section 9 (BM byte-matched C, NM NEARMISS,
AI inline asm, AW word asm). 001C6200 is the decomp's `bone_init_default_0` (a label).
For the NEARMISS and asm rows the translation follows the original instructions (the
splat listings of the pinned ELF, read locally), checked by the oracle; for the BM rows
the C and the original agree.

## 2. Model

- **Memory.** Original EE memory through caller-supplied regions keyed by original
  address: RAM, the scratchpad, and (new against lanes EXITB / SIDE) **the stack**. Four
  routines hand a stack local to a callee (00189FE0: v at sp+0x30 of its 0x40 frame;
  0019B2C0: m at +0x50 and t at +0x60 of 0x70; 001B0CD0: v at +0x40 of 0x50; 001C5050: v
  at +0x30 of 0x40) and 001CA4D0 builds its result in a 0x10-byte frame. Those locals live
  at the original stack addresses (entry `sp` minus the frame plus the offset), so the
  caller maps a stack region there. Saved-register slots are not written.
- **Calls.** Every call leaving the module goes through the one worker `call` with the
  callee's address, the stack pointer at the call (entry `sp` less the frame: 0x10,
  0x40, 0x10, 0x20, 0x70, none, 0x50, 0x80, 0x20, 0x40, 0x40, none, 0x40, 0x10, none in
  table order), and the argument registers the callee reads (`na` / `nf`; 64-bit images of
  32-bit values sign-extended as the original leaves them, raw float bits). 001C63D0 ends
  in a jump, so its callee gets the entry `sp`. The worker returns v0 (compared as the full
  register where the original tests the full register) and f0. 00189FE0 calls the
  translated 00189EC0 and 001B41F0 directly.
- **Wide registers.** 00189EC0 passes 00128350's whole 64-bit v0 to 001000C0 together
  with the 64-bit constant 0x3FF921FB60000000 (pi/2 in single precision, widened); both
  run as original soft-float code. 0018A180 tests the LCG result's sign on the full
  register; 0019AA80, 0019B2C0 and 001B0CD0 test their callee's whole v0 against 0.
  001C24D0, 001C5050 and 001C63D0 return their last callee's v0 untouched (a `uint64_t`
  output).
- **Arithmetic.** Every float operation is the EE operation the original executes, through
  `em_ee_float.h`: the accumulator forms of 001CA3B0 and 001CA4D0 (MULA, ADDA, SUBA, then
  MADD / MSUB) as `em_ee_mula/adda/suba_bits` + `em_ee_madd/msub_bits`, the compares as
  `em_ee_c_lt_bits`, the jitter of 001B41F0 as CVT.S.W then DIV.S.
- **Quadword store.** 001CA4D0 writes its 16 bytes to `out & ~0xF` (the EE quadword store
  ignores the low four address bits), after reading all four results back from its frame.
- **Fail-stop.** An address outside every region, a NULL worker, a negative worker result,
  a NULL region list: the first fault is latched (code, entry, address or callee, kept
  together; a worker that re-enters the module with the same context and faults there
  latches first, and the outer entry keeps that record), the call returns -1, and every later call returns -1 before any work until
  `em_area00_world_clear_fault`. A NULL context returns -1 without latching. Bytes written
  before the faulting access stay written. No routine reads a value it never set on any
  input, so there is no UNDEFINED fault.
- **Store trace (test builds only).** `EM_AREA00_WORLD_STORE_TRACE` names a function that
  receives every store (address, size); `wr` is the only store path.

## 3. What each routine does

Full statements are in the comment above each translation; short forms:

- **00189EC0(e)**: by type byte +3: 0x51 gives 2; 0x30, 0x4F, 0x46, 0x1F, 0x1E, 0x50, 0x1C,
  0x06 give 1; 0x0E: t = 0011DF78(001B1470(D_00810374 - e+0xC4)), stored at 0x70003A20,
  then 0 when 001000C0(00128350(t), pi/2) is nonzero, else 3; anything else 0.
- **00189FE0(a0, a1, a2)**: only while e = word 0x700031D4 is nonzero with byte e+0 == 1.
  v = a1 - a2 (001028D0: the caller's a1 and a2 pass through untouched), v.w = 1.0,
  normalised (00102760). Kind = byte e+2 & 0x1F. 2: v.y < 0 becomes +0 and v is normalised
  again; then 001B41F0(e, 0x700031B0, v, word (word 0x700031D0)+0x1C, 0x1000, halfword
  a0+0x36). 4: s = 00189EC0(e); 0 returns; 2: 0x700038D0 = (0, 001B1470(pi +
  D_00810374), 0, 1.0), 001EFD90(0x80000007, 0x700031B0, 0x700038D0); 3: 001EFD20(
  0x80000019, 0x700031B0); then e+0x36 = a0+0x36, e+0x36 |= 0x1000 (re-read),
  00102948(e+0x70, v).
- **0018A180(p)**: byte +0x0A = 1, byte +0 = 2, 001B61C0(0, 0xD0, 0x0A, 1), then a sound
  001FBD50(0x8102B0, 0x180 + b, 0; 300.0) with b = draw & 1 (-1 for a negative odd draw,
  which the LCG never gives, section 4.4).
- **0019AA80(a0, a1, a2)**: 0x70003190[i] = a0[i], 0x700031A0[i] = a1[i] (interleaved, i =
  0..2); 0x700031AC = 0x7000319C = 1.0; 0x700031D4 = 0; r = 001A7280(a2 & 0xFFFF); result 1
  when r != 0, else 0 with 0x700031D0 = 0; 0x700031D8 = result.
- **0019B2C0(a0, a1, flags)**: the segment 0x70003190 = (a0.x, a1.y, a0.z) -> 0x700031A0 =
  a1 (copied also to the local t); 0x700031AC, 0x7000319C, 0x700031D4 cleared; m = the
  unit direction scaled by 0.01; the end pushed out by m; halfword 0x7000324E = -1; flags &
  2: 0x70003254 = 0, mode 2 when 0019FE50() is 0; flags & 4: mode 4 when 0019CB60() is 0;
  the end pulled back by m. With a mode: 0x700031A0 = t again, 0x700031C0 = 0x700031B0 -
  0x700031A0, and with flags bit 31 a0.x += 0x700031C0, a0.z += 0x700031C8. Without: 0x700031D0
  = 0. 0x700031D8 = mode, the result.
- **0019C6F0(key, a1)**: scans the current area's 0x28-byte records (D_0024D7C0[D_00810700]
  word, + 4 * D_00810701, dereferenced): a halfword 0xFF at the scan position ends with 0,
  a record whose halfword +0 is not 0x0B ends with 0; a record whose signed halfword +4
  equals key selects the word at (word 0x70003250) + 4 * (halfword +6 >> 8) + 4, which
  must have bit 29 set for a negative key and clear otherwise; that word then gets bit 30
  cleared (a1 != 0) or set (a1 == 0) and the result is 1.
- **001B0CD0(a0, a1)**: p = 001F4F40(0); if nonzero: 00102948(p+0xB0, a0+0xB0),
  00102958(p+0xD0, (word a0+0x110)+0x90), v = 001026A0(p+0xD0, 0x24D6B0 + 16 * (a1 & 3)),
  p+0x100..0x10C = v.
- **001B41F0(victim, hit, dir, flags, p5, p6)**: the jump table on byte +3 (read from the
  ELF: 0/16/17/18 -> the spray with its own tail; 1 -> deflect test; 2 -> 0x80000025; 3 ->
  0x80000034; 6, 7 -> 0x80000024; 9 -> 0x80000025 unless flags & 0x40 deflects; 10, 11 ->
  0x80000035; 13 -> the reflected effect 0x80000026; 19 -> the mailbox only; 4, 5, 8, 12,
  14, 15 and 20..255 -> 0x80000007). Deflect: 00102948(victim+0x70, dir), result 0. Type
  1 deflects when byte +0x0D == 3 and (byte +5 == 9 or D_00810354 < 47.0). The spray: three
  LCG draws give the jitter (d >> 17, 13, 19) & 0xFF / 255 at 0x70003680..88, 0x70003610
  = hit - 0.5 + jitter, w = 1.0; 0x70003600 = (0, 001B1470(pi + D_00810374), 0, 1.0);
  00102948(victim+0x70, dir); byte +0 = 3. Tails: flags nonzero -> halfword +0x36 = (types
  0/16/17/18: p5 | p6 * 5 | 0x8000; others: p6 | p5 | 0x8000), effect 0x80000076, sound
  0x15D at 300.0; flags zero -> the type's effect, +0x36 = p6 | p5, then (other types only)
  sound 0x1B1 when p5 & 0x1000, else the ricochet 0x15A / 0x15B by bit 13 of a fourth draw.
  Type 19: byte +0 = 3, 001031E0(victim+0x70, hit), +0x36 = p5 | p6 * 5 | 0x8000 or p6 |
  p5. Every applied path returns 1.
- **001C24D0(a0, a1, a2)**: 0x700038C0 = matrix a2 x vector a1 (001026A0) + a0+0xB0;
  0019AD00(a0, 0x700038C0, 0x80000006).
- **001C5050(a0; k)**: v = the quadword a0+0x80 with v.w = k; 001D7FA0(a0+0xB0, v, 2; 1.0,
  0).
- **001C6200(a0)**: for each i below byte a0+0x0C (re-read), with the pointer b = word
  a0+0x110+4i re-read before every store: halfword +0x64 = -1, +0x88 / +0x8A / +0x8C =
  0x1000, words +0x7C, +0x80, +0x84, +0x70, +0x74, +0x78 = 0, then 001029C0(b).
- **001C63D0(a0)**: 001C9610(a0+0x110, byte a0+0x0C, a0+0xD0), continued into with no
  frame of its own.
- **001CA3B0(out; x, y, z)**: half angles, sines then cosines, and the four products of
  section 3's comment (a quaternion from the three angles, by its algebra; the label is
  not evidence of its use).
- **001CA4D0(out, a, b)**: the Hamilton-style product of the comment (x, y, z, w order),
  built in the frame, then copied to `out & ~0xF`.
- **0021BD60(p)**: 1 when byte +0x236 is set, or bit 1 of byte +0 is set with byte +0x1F0
  != 0x3B; for byte +4 == 1: 0 for byte +5 in {0, 1, 0x21, 0x22}, for 0x1D and 0x1E when
  byte +0x1F1 == 1, else 1; for byte +4 == 2: 0 when byte +5 == 0x0B, else 1; other: 1.

## 4. How it was verified

`tools/test_area00_world_reference.py`. Default run about 6 s of CPU (4 to 7 s wall with the
default 2 worker processes); `EM_TEST_FULL=1 EM_TEST_JOBS=4` about 110 s of CPU, 44 to 66 s
wall depending on machine load (44 s measured 2026-09-28 after review round 1; the reviewer
measured 66 s wall / 97 s CPU under load before it).

### 4.1 Oracle

FallEE (the shared interpreter with the measured COP1 / VU0 model) runs the ORIGINAL
instructions over the end-of-beat captures `../Extermination/build/s87/route_a00/`
(a00_03, a00_08, a00_09, a00_10: AREA00 resident, or the AREA01 arrival with D_0081075E set
for a00_10). The native module runs over a copy of the same bytes plus a private stack
region at the oracle's stack addresses (both filled with 0xA5). An address outside the
32 MiB stops the oracle (the shared core would fold it onto RAM). The design is lane
EXITB's (docs/AREA01_EXITB.md section 4) with lane EXITA's stack region.

### 4.2 Callees

- **Run as original on both sides** (the native side in a second interpreter over the
  native memory): 00102760, 00102948, 00102958, 001026A0, 001028B8, 001028D0, 001029C0,
  001031E0, 00103230; 0011DE90, 0011DF78, 0011E2A8, 001B1470; the LCG 00122BB8; the
  soft-float pair 00128350 and 001000C0 (with the 001274B0 it calls).
- **Stubbed** (recorded, scripted result): 0019AD00, 0019CB60, 0019FE50, 001A7280,
  001B61C0, 001C9610, 001D7FA0, 001EFD20, 001EFD90, 001F4F40, 001FBD50, 001FC580. A case
  can also stub the LCG with scripted draws (`forced`), to reach draws the real LCG never
  gives (negative, upper bits set).
- The test asserts every direct call or jump target of the 15 routines has a policy.

**Register reads.** RegScan (imported unchanged from `test_area01_sys_reference.py`)
measures what each callee reads before writing; the test fails if any callee reads an
argument register its policy does not compare. Measured: 001A7280 reads a0 only (0019AA80
also leaves a1 + 12, a2 and 0x7000319C in a1..a3; none is read); 0019FE50 and 0019CB60 read
no argument register (0019CB60 saves s1, s2, s4); 001D7FA0 reads a0..a2 and the float registers f12 and f13;
001FBD50 a0..a2 and f12; 001B61C0 a0..a3; 001000C0 a0, a1; 00128350 f12 only.

### 4.3 Lockstep and variants

At every call leaving the module the native side must match the callee, `sp`, every
compared argument register, the 16 bytes behind every compared argument that points into
the stack (the locals of section 2), every register the native says it sets, and all of
RAM and the scratchpad (the lines either side stored to since the previous check). The
same check after the last store, then all 32 MiB, the scratchpad, the call log and the
result. Every base case also runs as the lane-EXITB variants: fx-after and fx-before
(fields loaded after / before a stub change at that stub), prestore (bytes stored after a
stub pre-set to their complement), poison (bytes stored before being loaded start as their
complement), edge-byte (the byte after every byte / halfword access is 0xA5) and edge-top
(the top bit of every byte / halfword loaded flipped). fx and prestore are skipped for the
six routines with no stubbed call and for cases that stub the LCG.

### 4.4 Cases

1,033 base cases, 4,005 variants. Highlights (all in `all_cases()`; the ten review-round
cases are in `cases_pinned()`, which uses its own rng so every other case keeps its inputs):

- 00189EC0: all 256 type bytes; 20 angle differences around +-pi/2 and +-pi at two yaws;
  denormal and signed-zero angles; captured pool nodes.
- 00189FE0: no record / byte +0 != 1; 13 kind bytes (bits 5..7 set or not, low five bits
  0..5 and 0x1F) x the y sign, plus 0x12 and 0x14 x the y sign (bit 4 set over the low
  nibbles 2 and 4: the kind is the byte's low five bits, not four);
  kind 2 over seven victim types (so 001B41F0 runs from it) with a negative +0x36; kind 4
  over 00189EC0's four statuses x three +0x36 values; the captured 0x700031D0 record and a
  scratch one; a zero direction, y exactly 0 and -0.
- 0018A180: even and odd real draws; scripted negative and 64-bit draws.
- 0019AA80: every stub result class (0, 1, -1, upper bits only), masks 0x20 / 0x12345 /
  -1; three inputs overlapping the destination, (0x700031A0, 0x70003190), (0x70003194,
  0x700031A4) and (0x70003190, 0x70003190), which give the same bytes under the
  interleaved order and a split one (all a0 words, then all a1 words); and two that cross
  the streams (`19AA80 cross overlap 0/1`): a1 = 0x70003194 reads words of the a0
  destination (0x70003194, 0x70003198) before the interleaved copy stores them, and
  a0 = 0x7000319C reads 0x700031A0 / 0x700031A4 after the interleaved copy has stored
  a1[0] / a1[1] there. Only the original's interleaved order gives its bytes on both.
- 0019B2C0: ten flag words x the two stub results (including -1 and upper bits only; 0019FE50
  with only the upper word set for flags 2 and 0x80000006), a vertical segment, a0 at 0x700031C0 and a1 at 0x700031A0.
- 0019C6F0: the captured record lists of a00_08 (area 0 sub 1) and a00_10 (area 1 sub 0)
  with found and missing keys; synthetic lists with both signs of key and both states of
  bits 29 / 30, keys 0x7FFF / -0x8000 / 0x10003, a1 = 0x100 and 0x80000000 (nonzero with
  a zero low byte: the whole register is tested), an empty list, a class other than 0x0B, a
  class 0x10B, an end marker 0x1FF, an index byte 0xFF with area 3 sub 2.
- 001B0CD0: captured a00_10 nodes x selectors including 6 and -1; a null result, a result
  with only the upper word set, a new record overlapping the source.
- 001B41F0: types 0..0x13, 0x14, 0x80, 0xFF x four flag / p5 / p6 sets x both ricochet
  bits (LCG seeds solved); type 1's three tests (47.0 and its neighbours); p5 bit 12 and
  sign, p6 * 5 overflow, flags bit 31; scripted draws with upper bits; the hit point
  overlapping 0x70003680 / 0x70003684; captured victims.
- 001C24D0, 001C5050, 001C63D0: every stub result class; the vector inside 0x700038C0.
- 001C6200: every bone count of the a00_09 and a00_10 pools; count 0; a bone whose +0x70
  word is its own pointer slot, and bones whose +0x64 / +0x8C store hits the count byte
  (the re-reads show).
- 001CA3B0: 15 angle triples, NaN / Inf / denormal / MAX lanes, out in the scratchpad
  (0x70003A20).
- 001CA4D0: ten random pairs; each of the eight input lanes in turn with exponent 255
  (0xFF800000, 0x7F800001); four cancellation pairs, one per result, where an
  exponent-255 factor's raw product (MADD / MSUB) meets a -MAX accumulator (a separate
  MUL would saturate first and give another sum); out misaligned by 4 and 0xC, out equal
  to a or b.
- 0021BD60: 264 combinations of bytes +0x236, +0, +0x1F0, +4, +5, +0x1F1, and the captured
  player record; and the record placed so its first read (+0x236) is the last byte of the
  scratchpad (0x70003FFF) and of RAM (0x01FFFFFF), so a region's last byte is mapped (the
  edge-byte variant does not write past a region's end).

Inputs outside the comparison: 001B1470 never returns for a NaN or infinite angle or one
near FLT_MAX (the original loops); those angles are not used (they stopped the oracle at
its step limit).

**UNREACHABLE.** None. The one branch no real input takes, 0018A180's negative-draw
adjustment, is taken by the scripted draws; the real LCG never gives it (the test checks
that 00122BB8 returns the new state & 0x7FFFFFFF on 64 seeds).

### 4.5 Result of the full run

`EM_TEST_FULL=1 EM_TEST_JOBS=4` (2026-09-28, after review round 1): **1,033 cases and
4,005 variants identical to the original**; 14,679 callee entries compared in lockstep;
3,203 fx field changes; all 64 conditional branches of the 15 routines taken both ways. 74
variants were not
comparable (an edge / poison / fx change sent the original outside the 32 MiB, e.g. a
flipped pointer byte of 001C6200's bone list; the test counts and skips them). The API
checks (NULL context, NULL regions, NULL worker, worker error, the latch, the latch under a
re-entrant worker (the inner 0021BD60 fault survives the outer worker error), clear_fault, an
unmapped read reported at its address, an unmapped stack store in 001CA4D0) and the LCG
property pass in every mode.

### 4.6 Default run

The default run is every case with `pin` (a representative set per routine and path,
the first captured record of each), the variants of the base cases in `VPIN`, every case
or variant that killed a mutant of section 5 (`QUICK_KILLERS`), and one branch-cover case
(`QUICK_BRANCH`): 271 of 1,033 cases and 145 of 4,005 variants, 1,544 callee entries,
all 64 branches both ways, every non-equivalent mutant of section 5 killed (the lane's 91
and the review's 48).

### 4.7 The reused oracles

Default runs, 2026-09-28, all passing: `test_area01_exitb_reference.py` (147 of 467 cases,
52 variants, 108/108 branches), `test_player_weapon_states_b_reference.py` (3,000 of 16,000
cases), `test_player_reaction_reference.py` (8,000 state and 400 helper cases, every
reachable original instruction), `test_player_stage_workers_reference.py`,
`test_actor_collision_reference.py`, `test_coll_grid_hull_reference.py` (1,238 cases) and
`test_area01_math_reference.py`. Those oracles run over first-level or AREA01 captures (the
EXITB one over the a01_07 end, which is the AREA00 arrival); none was re-run over the
route_a00 captures by this lane.

## 5. Mutation sweep

One bounded sweep of 99 single-change mutants of `em_area00_world.c` (the driver was a
scratch script; the mutant sources and libraries were deleted after the sweep), each run
with `EM_TEST_FULL=1 EM_AREA00_WORLD_FAILFAST=1`. They cover every routine: wrong
constants, masks, shifts and comparison senses; swapped operands and call arguments;
dropped stores, calls and re-reads; cached loads; reordered stores and loads; 32-bit
tests of 64-bit results; signed / unsigned and width changes; MADD / MSUB replaced by MUL
then ADD; the quadword alignment.

- First pass: 88 killed, 11 survived. Three survivors were real gaps and got new pinned
  cases: 0019FE50 returning only an upper word (`19B2C0 f 2 upper word`), and 001CA4D0's
  exponent-255 lanes and cancellation pairs (`1CA4D0 b0 ff800000`, `1CA4D0 cancel x`).
  With them: **91 of 99 killed**, and the default run alone kills the same 91.
- The 8 survivors are equivalent:
  - H11 (001B41F0 reads 0x70003684 after storing 0x70003688): different addresses.
  - H20 (the jitter shifts done unsigned): bits 13..27 of the draw are below bit 31, so a
    logical and an arithmetic shift agree on the eight bits kept (and on bit 13 for the
    ricochet).
  - F4 (00189FE0's +0x36 halfword read unsigned): only its low 16 bits reach memory (the
    mailbox halfword).
  - F12 (e+0x36 |= 0x1000 from a0+0x36 instead of re-reading e+0x36): the re-read returns
    the halfword just stored; the two could differ only for a misaligned record, which
    the EE's halfword access does not allow.
  - B10 (0019B2C0 subtracts t[i] instead of the re-read 0x700031A0[i]): the word was just
    stored from t[i]; t is a stack local nothing else can reach.
  - C7 (0019C6F0 tests the low byte of the scan halfword for 0xFF): any halfword with low
    byte 0xFF is also not 0x0B, so the record test that follows returns 0 on the same
    records.
  - K2 (001CA3B0 halves by MUL 0.5 instead of DIV 2): `em_ee_div_bits(x, 2.0) ==
    em_ee_mul_bits(x, 0.5)` for all 2^32 patterns (checked exhaustively with the header).
  - K3 (001CA3B0's w by MUL then ADD instead of MADD): they differ only when a multiplied
    operand has exponent 255; the other factor is a MUL result (saturated) and sx is a
    0011E2A8 result, which gave no exponent-255 output for 411 inputs run through the
    original (every exponent-255 class, huge and random values). Measured, not proven for
    every input.
- E6 (the exact double pi/2 in 00189EC0) is killed only by the a1 register comparison at
  the 001000C0 call: no single-precision t lies between the two constants, so the result
  cannot differ (section 6).

**Review round 1 (2026-09-28).** The reviewer's own sweep of 52 mutants (generated from
the pre-fix source) left four non-equivalent survivors, all test gaps, not
mistranslations; each now has pinned cases (section 4.4), and each mutant, regenerated from
the current source, is killed by the default run:

- r05 (00189FE0 kind mask 0x1F -> 0x0F): `189FE0 kind 0x12 / 0x14 y +-3.0`.
- r17 (0019AA80 copy as an a0 loop then an a1 loop): `19AA80 cross overlap 0 / 1`.
- r27 (0019C6F0 tests only a1's low byte): `19C6F0 a1 0x100 / 0x80000000`.
- r51 (a region's last `size` bytes treated as unmapped): `21BD60 region end 0x70004000 /
  0x2000000`.

Two further mutants died only in full mode, r36 (001B41F0 type 8 given the path of types 6 / 7)
and r40 (001C24D0's result sign-extended from 32 bits); their killers `1B41F0 t8 f0 23 3 b0`
and `1C24D0 3` are now in `QUICK_KILLERS`, so the default run kills them too.

The reviewer's r52 (the fault latch always overwrites) exposed a latent defect: the latch
was reachable (a worker may re-enter the module with the same context, fault there, and
return an error), and on that path the old code kept the inner fault's code and address
but overwrote `fault_function` with the outer entry. `fault()` now latches code, entry and
address together (the Run carries its entry), a new API check pins it, and r52 is killed.
This touches only the fail-stop plumbing, no translated routine.

The review's other survivors are equivalent:

- r20 (0019B2C0's `add.s` operands swapped) and r46 (001CA3B0's `mul.s` operands swapped):
  `em_ee_add_bits` and `em_ee_mul_bits` are symmetric in their operands for every input
  (the exponent-255 tests, DAZ, the pre-trim of the smaller exponent, `em_eei_exact_sum`,
  which orders its operands by exponent, and `em_eei_exact_product` are all symmetric).
- r45 (001CA3B0's z by MADD with NEG(p3) instead of MSUB with p3): p3 is a MUL result, so
  its exponent is never 255 and NEG only flips its sign; the raw product is then the
  sign-flipped one for every finite cx, so the two differ only when cx (a 0011DE90
  result) has exponent 255 (the NaN / Inf product classes return a sign-less quiet
  pattern). Measured: 0011DE90 and 0011E2A8 run as original over 3,048 inputs each (every
  exponent 0..253, i.e. every class a DIV.S by 2 can produce, both signs, six mantissas
  per exponent) gave no exponent-255 result. Measured, not proven for every input; this
  also backs K3 above.

## 6. Decomp notes (for the lead; no decomp file was edited)

- **00189FE0** (byte-matched C): the C declares `func_001028D0(void *)` with one argument
  and calls it with the local only. The compiled call leaves the caller's a1 and a2 in
  place, so the original computes the local as a1 - a2 of 00189FE0's own second and third
  arguments; 00189FE0 therefore takes three arguments (the port's
  `em_player_equipment.h` already models it as `(node, a, b)`). The comment "func_001028D0
  seeds it" should say so.
- **00189EC0** (NEARMISS): the C's behaviour agrees with the original on all 256 type bytes
  and every angle tested. The float pi/2 widened to double and the exact double pi/2 give
  the same result for every single-precision t (no float lies between them); only the
  register image passed to 001000C0 shows the constant (mutant E6).
- **0019B2C0** (NEARMISS): **the C is wrong in its first loop.** `src/func_0019B2C0.c`
  copies a1.xyz with `D_700031A0[4 + i] = t`; `D_700031A0` is linked at 0x700031A0
  (SCUS_971.12.lcf), so the C stores a1.xyz to 0x700031B0..0x700031B8. The original stores
  them to 0x700031A0..0x700031A8 (the translation does, and the oracle compares every
  written byte against the original). The C therefore clobbers the hit point at
  0x700031B0 and leaves 0x700031A0 stale for the 001028D0 call that follows. The fix is
  `D_700031A0[i] = t` (or `D_70003190[4 + i]`, as the C's later copy-back loop already
  writes). The file's header comment repeats the error ("copies a1.xyz into
  D_700031A0[4..6]") and should say 0x700031A0..0x700031A8. The NEARMISS link fills the
  function from the original, so the boot ELF is unaffected; the C is wrong as port ground
  truth. The review found this; the lane's first report missed it.
- **0019AA80, 0019C6F0, 001B41F0** (NEARMISS): reading the C against the original found no
  behavioural difference (0019AA80's copy is interleaved, as the original's; 0019C6F0
  tests the whole a1); no test runs the decomp C. 001B41F0's jump table (20 entries at
  0x26DDF0) maps exactly as the C's case labels.
- FINDINGS.md's `func_001C6200` row (bone reset) agrees with the translation's stores; its
  "probably matrix-identity" for 001029C0 is not checked here (001029C0 runs as original).

## 7. Binding

Nothing calls these entries yet. Each is `int em_area00_world_XXXXXXXX(EmArea00World *s,
<32-bit arguments>, <optional out>)` over EE-memory regions; a binding supplies the scene's
original-byte storage (RAM, scratchpad) plus a stack region, a worker for the callees of
section 4.2, and the entry `sp`. Where the port already calls these addresses through
workers, the binding is an adapter from that worker to the entry:

- `em_player_equipment.h` / `em_equipment_live.c`: `w_00189EC0`, `w_00189FE0`,
  `w_0018A180` and `w_0019B2C0` (the equipment module; `em_equipment_live.c` binds all
  four to fail-stop stand-ins today, `x_00189EC0`, `x_00189FE0`, `x_node`, `x_0019B2C0`,
  as callees no first-level route reaches, PLAYER_EQUIPMENT.md 4.2). The live module holds native records, not EE bytes;
  the adapter needs the EE view of the node (the same need as lane EXITB's owners).
- `em_security_gun_rest.h`: `w_0019AA80` (the revisit-only probe of the AREA11 gun,
  SECURITY_GUN.md section 6).
- The AREA00 / AREA01 overlay lanes (0x823820 and 0x825910 call 0019C6F0; AREA01 0x825FD0
  calls 001B0CD0).
- 001B41F0's other callers (001861C0, 00186A60, 0018AF50, 0018B3E0) are weapon hit paths;
  `em_weapon.c` describes 001B41F0 in comments only.

The callees stay the binder's: 001A7280 has a verified native translation
(`em_coll_grid_hull_001A7280`); 001C2770 (via 001C2FF0) likewise in lane MATH. The stubbed
callees of section 4.2 (001EFD90 effects, 001FBD50 / 001FC580 sounds, 001F4F40 spawns,
001D7FA0, 001C9610, 0019AD00, 0019CB60, 0019FE50, 001B61C0) must be bound to verified
translations or fail-stop stand-ins.

## 8. Known gaps

- **Stubbed callees are not verified here**; their behaviour is other lanes' work. The
  scripted results cover values outside their real ranges (-1, 0x80000000, upper-word-only
  64-bit values).
- **Unread leftover registers.** 0019AA80 leaves a1 + 12, a2 and 0x7000319C in a1..a3 for
  001A7280, and 0018A180 leaves whatever 00122BB8 left in a3 for 001FBD50; RegScan shows
  the callees do not read them, so the entries do not pass them.
- **Stack slots.** The saved-register area of each original frame is not written by the
  module; nothing reads it (the stack lines are not compared, only the locals behind stack
  arguments are).
- **Captured coverage.** 001B0CD0, 001C5050 and 001C63D0 first ran in the a00_10 exit
  phase (the AREA01 arrival); their captured inputs come from the a00_10 end snapshot, not
  from the exact frames the census hit.
- Not wired; the Makefile does not build the module (isolation rule of this lane).

## 9. Reproduce

```sh
# port repo root
python3 tools/test_area00_world_reference.py                                  # default (~10 s)
EM_TEST_FULL=1 EM_TEST_JOBS=4 python3 tools/test_area00_world_reference.py    # every case and variant
EM_AREA00_WORLD_ONLY=1B41F0 EM_AREA00_WORLD_GAPS=1 python3 tools/test_area00_world_reference.py
EM_AREA00_WORLD_SOURCE=<mutant.c> EM_AREA00_WORLD_FAILFAST=1 EM_TEST_FULL=1 \
    python3 tools/test_area00_world_reference.py                              # one mutant
```
