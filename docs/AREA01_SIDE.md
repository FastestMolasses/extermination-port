# AREA01 lane SIDE: the side-beat-only boot functions

Level 2 (AREA01) side track, lane SIDE, 2026-09-26 (session s87). New files only:
`src/game/em_area01_side.{c,h}` (prefix `em_area01_side_`) and
`tools/test_area01_side_reference.py`. The module is built and tested but not wired.
Nothing in the port calls it yet; wiring waits for the AREA01 load (AREA01_OVERVIEW.md
section 11, step 4), as for the wave-1 lanes.

## 1. Scope

These are the census rows of `../Extermination/build/s87/census/a01_delta.json` (field
`new_functions`) in the subsystems area_logic, entity_sys, fx_render, audio, frame_update,
actor_anim, math_vector, anim_runtime, render_vif, weapon_equip, stream_cd and area_state
that are `side_beat_only`: 20 functions, 3,972 bytes. They run for the first time in one of
the side beats a01_s1_sentry_doc, a01_s2_control_room_items or a01_s3_fire_contact. The
subsystem names are census labels, not evidence; the functions are named by address.

Seven have no translation in the port and are translated here. Thirteen already have a
verified translation in another port module; they are not duplicated (section 4).

| Function | Bytes | Decomp | Census subsystem | First beat (frame) | Where |
|---|---:|---|---|---|---|
| 0015B770 | 728 | byte-matched | frame_update | s3 (210) | `em_player_floor.c` `em_player_stage_0015B770` (existing) |
| 0017C370 | 208 | asm | actor_anim | s3 (209) | `em_player_stage_workers.c` `em_player_0017C370` (existing) |
| 001AF7C0 | 52 | byte-matched | entity_sys | s2 (280) | this module |
| 001AFF10 | 120 | byte-matched | entity_sys | s2 (279) | `em_status_scene_original.c` `em_status_scene_alloc_001AFF10` (existing) |
| 001B0000 | 108 | byte-matched | entity_sys | s2 (280) | `em_status_scene_original.c` `em_status_scene_walk_001B0000` (existing) |
| 001C4720 | 52 | byte-matched | math_vector | s2 (246) | this module |
| 001CB480 | 112 | asm | anim_runtime | s2 (280) | this module |
| 001D8100 | 36 | byte-matched | render_vif | s3 (208) | `em_effect_original.c` `em_effect_original_001D8100` (existing) |
| 001E3D20 | 108 | byte-matched | weapon_equip | s3 (208) | this module |
| 001EFE00 | 172 | asm | fx_render | s3 (208) | this module |
| 001F0190 | 256 | byte-matched | fx_render | s3 (209) | this module |
| 001F0290 | 48 | byte-matched | fx_render | s3 (209) | this module |
| 001FCF60 | 44 | byte-matched | audio | s1 (330) | `em_census_standins.c` `em_cs_001FCF60` (existing) |
| 001FCF90 | 328 | NEARMISS | audio | s1 (330) | `em_census_standins.c` `em_cs_001FCF90` (existing) |
| 001FE660 | 80 | byte-matched | stream_cd | s1 (330) | `em_census_standins.c` `em_cs_001FE660` (existing) |
| 0021BC40 | 200 | asm (words) | area_state | s3 (209) | `em_player_stage_workers.c` `em_player_0021BC40` (existing) |
| 0021C350 | 148 | asm | area_logic | s3 (209) | `em_player_stage_workers.c` `em_player_0021C350` (existing) |
| 0021D1A0 | 172 | asm (words) | area_logic | s3 (210) | `em_player_reaction.c` `em_player_reaction_0021D1A0` (existing) |
| 0021D600 | 56 | byte-matched | area_logic | s3 (210) | `em_player_reaction.c` `em_player_reaction_0021D600` (existing) |
| 0021D800 | 944 | NEARMISS | area_logic | s3 (210) | `em_player_reaction.c` `em_player_reaction_0021D800` (existing) |

**IOP / CD.** None of the 20 makes an IOP or CD call itself. The audio and stream_cd rows
(001FCF60, 001FCF90, 001FE660) walk the record bank at `*D_0028A49C` in EE memory and hand
segments to 001FE070 / 001FE530 / 001FC770 (em_census_standins.c, verified against the
original there); their EE-side effect is the calls and the 0x280-byte segment buffer, which
that test compares. 0021B9A0, which 001F0190 and 001F0290 call, rewrites the record at
`*D_00275670` according to its NEARMISS C (+0xB8 / +0xBC and the fields derived from them);
it is a worker here, and the translation reads that record again after the call, as the
original does.

## 2. Model

The module is the lane-SYS model (AREA01_SYS.md section 2), unchanged:

- **Memory.** The routines address original EE memory through caller-supplied regions keyed
  by original address: RAM, the scratchpad and a stack region. 001EFE00's point local (at
  its sp+0x30) lives in the original frame layout below the caller's `sp`, so its callees
  (00102948, 001EF9D0) see the original's address. Every other value is a C local.
- **Calls.** A call that leaves the module goes through the single worker `call` with the
  callee's address, the argument registers the original sets (64-bit images; `na` / `nf`
  say how many) and the stack pointer. It returns v0 (a 64-bit image) and f0. 001E3D20
  calls 001EFE00 directly.
- **Register images.** A 32-bit argument is the sign-extended register the original's
  callers pass. Where the original passes a callee's whole v0 on (001CB480 hands
  001D2910's v0 to its last 001D2830 as a1) or tests the whole v0 against zero (001E3D20's
  0021BB00 gate, 001EFE00's 001EF9D0 result), the module uses the 64-bit image the worker
  returns, not its low word.
- **Arithmetic.** The one float operation (001EFE00's y + 10.0) and the two compares
  (001F0190) go through `em_ee_float.h`.
- **Store trace (test builds only).** `EM_AREA01_SIDE_STORE_TRACE` reports every store
  (`wr` is the only store path) so the test can compare memory at every call.
- **Fail-stop.** An unmapped address, a NULL worker, a negative worker result or a NULL
  output latches the first fault (code, entry, address); every entry then refuses until
  `em_area01_side_clear_fault`. None of the seven routines reads a value it never set, so
  there is no UNDEFINED fault here.

## 3. What each routine does

- **001AF7C0().** When the signed halfword D_00275BCC is positive: D_00275BCC = count - 1,
  D_00275BD0 = old pointer + 4, and the result is the word at the old pointer, read after
  both stores (so a pointer aimed at D_00275BCC / D_00275BD0 reads the new values).
  Otherwise the result is 0 and nothing is written.
- **001C4720(a0, a1).** The byte at D_00810CB8 + a0 (a 32-bit sum) = its value + a1 (low 8
  bits); then D_008106B0 = 2 and D_008106B1 = a0's low byte, in that order. Result 0.
- **001CB480(a0).** 001D2910(0) (v0 kept whole), 001D8C20(2), 001D2830(0, 0),
  001C7420(a0, 0x3F5, 1), 001D3BA0(1, word a0+0x44 read after 001C7420 returns),
  001D2830(0, the kept v0). No result.
- **001E3D20(a0, a1).** Nothing when bit 1 (value 2) of byte a1+0 is set. Otherwise
  0021BB00(D_008102B0); a non-zero v0 ends it. Otherwise 001EFE00(0x80000027, a1), byte
  a1+0x0F = 0x0C, word a0+0x1F0 = 0x3C (in that order).
- **001EFE00(a0, a1).** p = the quadword at a1+0xB0 (00102948 into the local); for a0 ==
  0x80000027, p.y = p.y + 10.0. r = 001EF9D0(a0, &p; f12 = 1.0). When r is non-zero: word
  r+0x24 = word a1+0x14, then quadwords a1+0xB0 -> r+0xB0 and a1+0xC0 -> r+0xC0 (00102948
  each). The result is r.
- **001F0190(f12, f13).** key = D_00810700 * 256 + D_00810701 (bytes); D_00275C3C = 0. Key
  0x1600 or 0x800: 0021B9A0(2; 1.0, 10.0), 0021B9A0(3; 1.0, 10.0), D_00275C3C + 1 (read
  again). Other keys: unless float (*D_00275670)+0xB8 <= f12 (EE compare), 0021B9A0(2; 0.0,
  f12) and D_00275C3C + 1; then D_00275670 is read again and, unless its +0xBC <= f13,
  0021B9A0(3; 0.0, f13) and D_00275C3C + 1.
- **001F0290().** 0021B9A0(1; 0.0, 0.0) when the word D_00275C3C is non-zero.

In the captures: seventeen records hold 0x001E3D20 at +0x34 in each of the eleven
AREA01-resident captures (0x7A96E0, 0x7A99D0, 0x7A9CC0, and 0x7AD780 + 0x2F0 * k for
k = 0..13), every one with its +0x30 in D_00253CA0..D_00253CB0 and byte +2 = 0x0D. a01_07,
which ends with AREA00 loaded, holds seven (0x7A5640 + 0x2F0 * k for k = 0..6). The test
calls 001E3D20 with four of the AREA01 records as a0 and, in most cases, the player record
D_008102B0 as a1; ten cases pass another record as a1 (section 5). In a01_s3_fire_contact the player's +0x1F0 / +0x1F1
become 0x3E / 4 from trace row 16772 on. D_00275670 is 0x811CC0 in every AREA01 capture.
Which records the census hits came from is not recorded (the census logs entry hits, not
arguments), so these are the inputs the captures hold, not a claim about the calls.

## 4. Existing translations (not duplicated)

| Address | Port module | Verified by |
|---|---|---|
| 0021BC40, 0021C350, 0017C370 | `em_player_stage_workers.c` | its `tools/test_player_stage_workers_reference.py`; 0021BC40 and 0021C350 again in this lane's test (section 5, B) |
| 0021D800, 0021D1A0, 0021D600 | `em_player_reaction.c` | its `tools/test_player_reaction_reference.py`; 0021D800 (with 0021D1A0 / 0021D600 executed inside) and 0021D600 again in this lane's test (section 5, B) |
| 0015B770 | `em_player_floor.c` | `tools/test_player_floor_reference.py` (executes 0015B770; its scenarios include +5 = 0xD with +0xD = 0..5) |
| 001AFF10, 001B0000 | `em_status_scene_original.c` | `tools/test_status_scene_reference.py` (executes both originals) |
| 001D8100 | `em_effect_original.c` | `tools/test_effect_original_reference.py` |
| 001FCF60, 001FCF90, 001FE660 | `em_census_standins.c` | `tools/test_census_standins_reference.py` section D over the record bank; this lane's test asserts that the bank those routines read (the container at `*D_0028A49C`, 0xF500 bytes, and the config D_00264CF0) is byte-identical in all twelve AREA01 captures to the capture that test uses, so its every-line / every-page sweep covers the AREA01 data exactly |

Section 5 lists what this lane ran for each of them.

Overlaps for the lead:

- **001C4720** is also inlined, with a1 = 1, in `em_pickup_owner.c`
  `em_pickup_owner_take` (subtype 1: `++maps[type]`, request kind 2), which the
  interaction host turns into the scene request bytes `EM_SCENE_REQ_B0/B1`. This lane
  translates the general routine (any a1, the raw D_00810CB8 byte and the D_008106B0 /
  D_008106B1 stores). Binding it later means mapping its EE view onto the canonical item
  block and request bytes, not a second copy.
- **001AF7C0** is a worker (`w_001AF7C0`) of `em_status_scene_original.c`, whose header
  notes the port has no translation of it. This module's `em_area01_side_001AF7C0` is that
  translation, over an EE view.
- **001EFE00** is a worker in `em_spawn_table.c` (`w_001EFE00`, effect 0x80000018),
  `em_player_stage_workers.h` (`w001EFE00`) and `em_area01_overlay_internal.h`
  (`a01_c_001EFE00`, a call-out). None translates it; this module does.
- **001E3D20** is only named in `em_area01_sys.c` (001E3D90 stores it at +0x34).
- **001CB480** is only named in a comment of `em_render_frame.c`.
- `em_player_stage_workers.{c,h}` and `em_player_floor.{c,h}` are being edited by chain
  C7 in the working tree (they add 00182DF0 and a floor hook; the functions above are
  untouched by that diff). This lane compiled them from a HEAD export (`git archive HEAD
  src/game` into `build/area01/side/head`, `EM_AREA01_SIDE_EXISTING_ROOT`) so C7's
  work in progress did not enter its results.

**Decomp C that differs from the original (for FINDINGS).** 0021D800's NEARMISS C returns
from its sub-state 1 in four places (after 0021D530, after 0017C540, after the +4 1 +5 7
+1F0 0xD stores, and after the +1F0 0x3E / 0xD store), but the original branches from
those points (0x21DA8C, 0x21DAD8, 0x21DB00, 0x21DB74) to the common tail at 0x21DB84,
which calls 0021D250(p, 0) when byte +0x23A is 0x5D (read from the listing of the pinned
ELF; the branch targets are the evidence). The port's
`em_player_reaction.c` already follows the instructions (its header says so, and
PLAYER_REACTION.md records it). The decomp's `docs/NEARMISS.md` entry for 0021D800 lists
only the unset a0 of the two 001FBD50 calls; the missing tail is not recorded there. On
the unset a0: 0021D600 does not change a0, so both calls receive the record pointer, which
is what the NEARMISS C passes.

## 5. How it was verified

`tools/test_area01_side_reference.py`

**Oracle.** FallEE (the shared EE interpreter with the measured COP1 / VU0 model, as in
the wave-1 lanes) runs the original code of each routine from the captured AREA01 RAM:
the end-of-beat images `../Extermination/build/s87/route_a01/<beat>/eeMemory.bin` and
`scratchpad.bin` of all twelve AREA01 beats (a01_00..a01_07, a01_s0..a01_s3). The native
module runs over a copy of the same bytes. The harness is lane SYS's, reduced to what these
seven routines need:

- **Callees.** Every call that leaves the translated set is caught on both sides and
  handled by one policy. 00102948 (the quadword copy) runs as original code, in the
  oracle and, on the native side, in a second interpreter bound to the module's own
  memory. 001D2910, 001D8C20, 001D2830, 001C7420, 001D3BA0, 0021BB00, 001EF9D0 and
  0021B9A0 are stubs: recorded, with a scripted v0 (a 32-bit value sign-extended, or a
  whole 64-bit image). The test reads every direct call target of the seven routines from
  the pinned ELF and asserts each has a policy (001EFE00, called by 001E3D20, is in the
  translated set).
- **Lockstep.** The original runs first and records, at each call, the callee entry and
  every RAM / scratchpad line (64 bytes) it stored to since the previous call. At the same
  call index the native side must match: the callee and stack pointer; the integer
  argument registers the original sets (64-bit images; the native's `na` / `nf` claims are
  checked against the original's registers); the float argument registers; the 16 bytes
  behind a stack-pointer argument (001EFE00's point, as 001EF9D0 is handed it); the 16
  bytes behind 001EF9D0's point argument; and every line either side stored to since the
  last check. The same memory check runs after the last store; then the result (low word;
  001AF7C0, 001C4720, 001EFE00), the call count, all 32 MiB of RAM and the scratchpad.
  Both stacks start filled with 0xA5, and the interpreter's unset registers hold a
  pattern.
- **fx cases.** For every case of 001CB480, 001E3D20, 001EFE00 and 001F0190 (the routines
  that load memory after a stubbed call) there are two variants. 'after': pass 1 records
  the data loads the routines make after each stub; pass 2 changes exactly those fields at
  that stub (bytes and halfwords flip bit 0, words bit 4), identically on both sides, so a
  value cached across a stub differs. 'before': at each stub the fields loaded since the
  previous stub change (the n-th change of a field flips bits no earlier change flipped, in
  both halves), so a read deferred across a stub differs. Stack addresses and code words
  are left alone.
- **Branches.** Both outcomes of all 10 conditional branches of the seven routines are
  taken; the test asserts it in every run (quick and full).
- **Fail-stop (`api_checks`).** NULL context (-1, nothing latched); NULL worker (FAULT_NULL,
  the entry, the callee); a failing worker (FAULT_WORKER, the entry, the callee, the stores
  made before kept: 001F0190's counter reset); an unmapped address (001AF7C0's pointer and
  001C4720's byte: FAULT_UNMAPPED, the entry, the address); NULL output for the three
  routines with a result (FAULT_NULL, the entry, address 0, no byte written); the latch
  (every entry refuses, memory untouched) and `clear_fault`; the first fault kept.

**Cases** (all compared the same way):

| Routine | Cases (full) | What they cover |
|---|---:|---|
| 001AF7C0 | 47 | all twelve captured beats as captured; count 0, 1, 2, -1, 0x7FFF, -0x8000 x pointer = captured, D_00275BD0 itself, D_00275BCC, a scratchpad word, a RAM word |
| 001C4720 | 145 | a0 = 0, 1, 5, 0x0B, 0x2F, 0xFF, 0x100, -1, -0x608 (the byte is D_008106B0), -0x607 (D_008106B1), 0x1000, -0x10CB8 x a1 = 1, 0, -1, 0xFF, 0x100, 0x101, 0x7FFFFFFF, -0x80000000 over the three side beats; the last RAM byte |
| 001CB480 | 63 | a0 = the player record, an owner record, a scratch record, over the three side beats x the kept v0 = 0, 5, -1, 0x80000000, 0x1_23456789, 0xFFFFFFFF_00000000, 0x7FFFFFFF |
| 001E3D20 | 106 | a0 = four of the owner records; byte a1+0 = 3 (captured), 0, 1, 2, 0xFD, 0xFF; 0021BB00 v0 = 0, 1, -1, 0x1_00000000; 001EF9D0 v0 = 0, a scratch record, the player record; a0+0x1F0 aliasing a1+0x0F; the neighbour bytes of both stores non-zero; the three side beats as captured; a1 = owner record 0x7A99D0 or a scratch record (not D_008102B0), byte a1+0 = 0 or 3 with D_008102B0's byte +0 set to the other value, 0021BB00 v0 = 0 or 1, 001EF9D0 v0 = 0 or a scratch record (10 'edge a1' cases, all in the default run, no fx variants) |
| 001EFE00 | 60 | ids 0x80000027, 0x80000018, 0x80000026, 0x80000028, 0x27, -1, 0x7FFFFFFF; p.y = captured, +-max, denormal, -0, +inf pattern, -10.0, 2^23, NaN pattern; 001EF9D0 v0 = 0, a scratch record, a1, a1 +- 0x10 (the copies overlap the source), and a v0 whose low word is 0 but upper word is not |
| 001F0190 | 336 | keys 0x0100 (captured), 0x0800, 0x1600, 0x0801, 0x1601, 0x0008, 0x07FF, 0x15FF, 0x0016; thresholds captured (-110 / 330), 0, -0, denormal, +-inf patterns, NaN pattern, x 15 argument values (equal, one ulp either side, 0, -0, NaN / inf / denormal / max patterns) for f12 and f13; the counter's neighbour bytes non-zero; all twelve beats as captured |
| 001F0290 | 18 | counter 0, 1, 0x80000000, 0x100, 0xFFFFFFFF, 0x10000; all twelve beats as captured |

Full mode: 775 cases and 1,110 fx cases, 2,869 compared calls, 1,555 fx field changes.
The default run keeps every captured case, every edge case, the key cases (and their 'after'
fx variants), the 001AF7C0 count-1 cases and all 001F0290 cases, plus a fixed-seed sample
(270 cases, 90 fx cases in all; the fixed-seed sample is the same as before the ten
001E3D20 'edge a1' cases were added, since pinned cases are not sampled and the count grew
by ten).

**B. Existing translations, from this lane's test.** Built from `EXISTING_ROOT` (this run:
the HEAD export `build/area01/side/head`, commit e21bd95) exactly as their own tests build
them. On the AREA01 fire-contact player record (the 0x320 bytes at D_008102B0 in
a01_s3_fire_contact), compared with the original executed by FallEE over the same capture:

- `em_player_0021BC40`: every +0x1F0 value 0..255 x +0x0D = captured, 2, 0: 768 results.
- `em_player_reaction_0021D600`: every +0x1F1 value x the same +0x0D values: 768 results.
- `em_player_0021C350`: +0x220 and +0x224 each over 13 values (0, -0, denormal, 35.0 and
  one ulp either side, 1, -1, 100, +inf pattern, -max, NaN pattern, captured) x +0x235 =
  captured, 0, 0xFF: 507 cases, all 0x320 record bytes compared.
- `em_player_reaction_0021D800` through `test_player_reaction_reference.py`'s own oracle
  (ReactionEE), workers and `compare` (every worker call, the 0x320 actor bytes and the
  scene bytes at every call, the scratchpad words): the captured record with +6 = 0, 1, 2
  and +0x1F1, +0x200, +0x23A, +0x236, +0x234, +0x319, +5 varied; 240 cases in full, 60
  in the default run. 0021D1A0 and 0021D600 run inside it as original code on the oracle
  side and as the module's translations on the native side.
- The record bank of 001FCF60 / 001FCF90 / 001FE660 is byte-identical in all twelve
  AREA01 captures to the capture `test_census_standins_reference.py` sweeps (section 4).

The existing modules' own tests were also run from a HEAD export
(`build/area01/side/tree/port`, commit e21bd95, with the decomp as its sibling), so that
chain C7's uncommitted edits did not enter:

| Test (HEAD export, default mode) | Result | Wall / user / sys (s) |
|---|---|---:|
| `test_player_floor_reference.py` | PASS; 30 `0015B770` stage cases among 150 stage cases, 28 stage paths | 20.5 / 14.3 / 0.3 |
| `test_player_stage_workers_reference.py` | PASS; 6,912 predicate cases (0021BC40 among them), 135 0021C440 scenarios, all 1,935 reachable original instructions of 23 functions executed (0017C370, 0021BC40, 0021C350 among them) | 9.8 / 6.6 / 0.2 |
| `test_status_scene_reference.py` | PASS (001AFF10 and 001B0000 executed as original) | 3.9 / 2.7 / 0.1 |
| `test_effect_original_reference.py` | PASS; 12 point-light binding cases (001D80E0 / 001D8100) | 3.4 / 2.2 / 0.3 |
| `test_census_standins_reference.py` | PASS | 6.6 / 4.4 / 0.6 |
| `test_player_reaction_reference.py` | PASS | 6.9 / 5.2 / 0.1 |

**Timings** (macOS arm64, M1, four workers at most, while other lanes' sweeps shared the
machine; load average about 30): default run 3.8 s wall, 6.1 s user + 1.3 s sys (270 cases,
90 fx cases, 535 compared calls, 116 fx field changes, 2,115 section-B checks, the existing libraries
rebuilt each run); `EM_TEST_FULL=1` 17.9 s wall, 30.2 s user + 8.1 s sys (775 cases,
1,110 fx cases, 2,869 compared calls, 1,555 fx field changes, 2,295 section-B checks). With
the default `EXISTING_ROOT` (the working tree, including C7's uncommitted edits at the time)
the default run also passed (3.6 s wall, 5.3 s user + 1.2 s sys). A first run after an
edit also compiles the module and takes about 2 s more wall.

**Mutation sweep (bounded, one round).** A scratch generator
(`build/area01/side/mutate.py`, not committed) made 411 single-operation mutants of
`em_area01_side.c` from the `reg` helper to the end of the file (the seven translations,
the call helpers and the public entries): every integer constant +-1, `<`/`<=`, `==`/`!=`,
`&&`/`||` flips, a removed `!`, the EE compare and add swapped for their neighbours
(c.le/c.lt, add/sub), store and load widths, zero- for sign-extension of each register
image, a low-word test of 001EF9D0's v0, every statement deleted and every statement
swapped with the next. Each ran through this test (`EM_AREA01_SIDE_SOURCE`, section B
skipped). 45 do not compile. First round: 293 killed by the default run; of the 73
survivors, 4 were killed by the full run (001AF7C0 count 1; the counter address in
001F0190's increment, twice; 001F0290's counter address). The round showed six gaps,
closed by new cases and pinned into the default run: a wider read of 001C4720's byte (the
last RAM byte), a wider store at a1+0x0F (non-zero neighbour bytes), a shifted counter
store in 001F0190 (non-zero neighbour bytes), 001EFE00 testing only v0's low word (a v0
with a zero low word), and the fault fields of the NULL-output and worker-failure paths
(the entry named in `fault_function` for every entry, address 0 for NULL output). Final:
the whole set of 411 re-run through the final default run: 314 killed, 45 not compiled,
52 survivors. The 52 are a subset of the 73 first-round survivors: the other 21 (the 4 the
full run had killed among them) are killed by the pinned and added cases, and no mutant
killed in the first round survives the final run. Each of the 52 is classified below. No
survivor needed the full run.

The 52 survivors, each shown equivalent or undefined:

- **Undefined C (5; not verified, C gives them no meaning):** reading `a[na]` past the
  array in `call3` / `channel` (`i <= na`), reading `a[]` past its end (`&&` to `||` in the
  copy loop), `f[1]` / `f[0]` read uninitialised at the call (the store moved after the
  0021B9A0 / 001EF9D0 call), and `*out` stored from an uninitialised `v` (001C4720's
  entry).
- **Equivalent under the worker contract (5):** the `memset` of the call record removed or
  filling 1 (the worker reads only the first `na` / `nf` registers and writes v0 / f0), and
  the second integer register given 1 where `na` is 1 (001D2910, 001D8C20, 0021BB00).
- **Independent statements swapped (20):** assignments to different fields of the call
  record or of `a[]` / `f[]`; `sp` adjusted before or after a load; `rp` and the
  a1+0x14 load; the fault fields of `out_null`; and swaps of loads and stores at distinct
  fixed addresses with no call between (D_00275BD0 and D_00275BCC..BCD; D_008106B0 and
  D_008106B1; D_00810700..701 and D_00275C3C..C3F).
- **Bounds and sizes above every use (12):** the copy-loop limits 8 / 4 moved or made
  inclusive, and the local arrays enlarged; `na` is at most 3 and `nf` at most 2 in every
  call.
- **Overwritten store (1):** D_008106B0 stored as a halfword; its second byte is D_008106B1,
  which the next store overwrites with no load or call between.
- **Zero- for sign-extension of an address below 0x80000000 (9):** 001C7420's a0, the
  00102948 arguments of 001EFE00 (p, a1+0xB0, r+0xB0, a1+0xC0, r+0xC0), 001EF9D0's point
  argument, and the constant D_008102B0 (the same value). Every address the module maps
  is below 0x80000000. p is the stack; 001CB480 reads a0+0x44 through its regions; the
  other addresses are the source and destination of a 00102948 copy (and r+0x24 is stored
  by the module before r's copies), so a worker that maps the same memory must fault on
  them too. On every input for which the module and its copies complete, the two images
  are equal; this rests on that worker property for the copy arguments.

**Review 2 survivor, killed.** The independent review's 47 mutants found one survivor
the generator above does not make (it substitutes an operand, not an operator): 001E3D20
passing a1 to 0021BB00 instead of the constant D_008102B0. Every earlier 001E3D20 case
had a1 = D_008102B0, so it could not be told apart, nor could the reverse substitutions
(the constant for a1 in the gate byte read, the 001EFE00 argument or the a1+0x0F store).
The ten 'edge a1' cases (section 5 A table) pass a1 = 0x7A99D0 or a scratch record and set
D_008102B0's byte +0 opposite to a1's. The four mutants (scratch copies in
`build/area01/side/fix2/`) all survive the old 1E3D20 cases in full mode and are all
killed by the default run, each by `1E3D20 edge a1 7a99d0 flag 0 gate 0 spawn 0`: the
0021BB00 argument at call 0 and the 001EFE00 argument at the first 00102948 copy differ in
the lockstep, the constant store leaves a1+0x0F = 0 where the original stores 0x0C, and
the constant gate returns before the original's three calls. The real module passes both
modes.

Not checked by this sweep: mutations of the region plumbing (`span`, `rd`, `wr`, the fault
latch), which is lane SYS's code with the names changed and was swept there; operators
other than those listed.

## 6. What is left

- Wiring: none of the seven is bound. The worker calls have port counterparts to bind
  (named here, not re-verified by this lane): 0021B9A0 `em_packet_chain_0021B9A0`
  (em_packet_chain_original.c), 001D2910 `em_render_context_001D2910`, 001D2830
  `em_frh_001D2830` (em_frame_render_heads.c), 001C7420 `em_owner_services_001C7420`,
  001D3BA0 `em_owner_draw_001D3BA0`, 0021BB00 `em_player_0021BB00`, 001EF9D0
  `em_effect_original_001EF9D0`; 001D8C20 appears only as worker adapters (for example
  `em_owner_draw_live.c` stores the mode at the render context's +0x246C). 001E3D20's
  caller is the +0x34 handler slot filled by 001E3D90 (lane SYS). 001C4720 must write the
  canonical item block and scene request bytes; D_00810CB8[a0] with a0 >= 0x0B reaches
  D_00810CC3 (the migrated key bytes, em_scene_state.h), so the binding must add a
  REACHERS entry for 0x00810CC3 in tools/test_scene_no_shadow.py (or a REACH_CALL); none
  is needed today because the new files name only 0x00810CB8; 001AF7C0 would replace the status scene's
  `w_001AF7C0` worker.
- 001EFE00 with a 001EF9D0 v0 whose upper word is non-zero: the module (like the
  interpreter) tests the whole register but addresses r+0x24 / r+0xB0 / r+0xC0 with its
  low word. What the EE does with such an address was not measured; a callee returning a
  pointer never produces one.
- Register reads of the stubbed callees are not measured (lane SYS's RegScan was not
  ported): a callee reading a register beyond the ones its caller sets is not checked.
- The existing translations of section 4 were re-run from HEAD but only four of the
  thirteen are exercised on AREA01-captured records by this lane's test; the rest rely on
  their own tests over the first-level captures (the same boot code; for the record-bank
  routines the data is shown identical).
